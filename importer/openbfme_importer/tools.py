"""External conversion tool discovery and reproducible command execution."""

from __future__ import annotations

from dataclasses import dataclass
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
from typing import Sequence


def directory_tree_sha256(root: Path, *, ignore_python_cache: bool = False) -> str:
    """Hash every path, size, and byte in a tool tree deterministically."""

    base = root.expanduser().resolve()
    digest = hashlib.sha256()
    candidates = (
        item
        for item in base.rglob("*")
        if item.is_file()
        and not (
            ignore_python_cache
            and (item.suffix.casefold() == ".pyc" or "__pycache__" in item.parts)
        )
    )
    for path in sorted(
        candidates,
        key=lambda item: item.relative_to(base).as_posix().casefold(),
    ):
        if path.is_symlink():
            raise RuntimeError(f"tool tree contains a symbolic link: {path}")
        relative = path.relative_to(base).as_posix()
        file_digest = hashlib.sha256()
        with path.open("rb") as stream:
            while chunk := stream.read(1024 * 1024):
                file_digest.update(chunk)
        digest.update(relative.encode("utf-8"))
        digest.update(b"\0")
        digest.update(str(path.stat().st_size).encode("ascii"))
        digest.update(b"\0")
        digest.update(file_digest.hexdigest().encode("ascii"))
        digest.update(b"\n")
    return digest.hexdigest()


@dataclass(frozen=True, slots=True)
class ToolInfo:
    name: str
    path: str | None
    version: str | None

    @property
    def available(self) -> bool:
        return self.path is not None


def discover_executable(name: str, env_name: str | None = None) -> Path | None:
    """Resolve a conversion tool: env override, then pinned copy, then PATH.

    Every branch is reported to diagnostics because this function is where the
    project's recorded failure class lives: when the pinned ffmpeg was missing,
    the PATH binary was picked silently, failed the pinned-hash attest on every
    audio job, and the run still produced confident-looking numbers. Which of
    the three branches won is therefore evidence, not trivia.
    """

    from .diagnostics import active_run

    run = active_run()
    configured = os.environ.get(env_name or "", "").strip() if env_name else ""
    if configured and Path(configured).is_file():
        chosen = Path(configured).resolve()
        run.decision(
            "tool",
            chosen=str(chosen),
            reason=f"{env_name} environment override",
            candidates=[f"env:{env_name}"],
            tool=name,
            source="environment",
        )
        return chosen
    pinned_path: Path | None = None
    if name.casefold() in {"ffmpeg", "ffprobe"}:
        from .paths import default_state_root

        pinned = default_state_root() / "tools" / "ffmpeg-8.1.1" / "bin" / f"{name}.exe"
        pinned_path = pinned
        if pinned.is_file():
            chosen = pinned.resolve()
            run.decision(
                "tool",
                chosen=str(chosen),
                reason="pinned tool tree under the state root",
                candidates=[str(pinned)],
                tool=name,
                source="pinned",
            )
            return chosen
    found = shutil.which(name)
    if found:
        resolved = Path(found).resolve()
        # A PATH hit for a tool that is supposed to be pinned is the exact
        # silent fallback AGENTS.md rule 5 forbids. Say so at warning level.
        pinned_expected = pinned_path is not None
        run.event(
            "decision",
            level="warning" if pinned_expected else "info",
            kind="tool",
            chosen=str(resolved),
            reason=(
                "PATH fallback: the pinned copy is absent"
                if pinned_expected
                else "resolved from PATH"
            ),
            candidates=[str(pinned_path)] if pinned_path else None,
            tool=name,
            source="path",
            pinned_expected=pinned_expected,
        )
        return resolved
    run.decision(
        "tool",
        chosen=None,
        reason="not found in the environment, the pinned tree, or PATH",
        candidates=[str(pinned_path)] if pinned_path else None,
        tool=name,
        source="missing",
    )
    return None


def inspect_tool(name: str, env_name: str | None = None, version_args: Sequence[str] = ("-version",)) -> ToolInfo:
    executable = discover_executable(name, env_name)
    if not executable:
        return ToolInfo(name, None, None)
    try:
        result = subprocess.run(
            [str(executable), *version_args],
            check=False,
            capture_output=True,
            text=True,
            timeout=15,
        )
        lines = (result.stdout or result.stderr).splitlines()
        version = lines[0].strip() if lines else f"exit {result.returncode}"
    except (OSError, subprocess.TimeoutExpired) as exc:
        version = f"unreadable: {exc}"
    return ToolInfo(name, str(executable), version)


def git_revision(repository: Path, relative: str | None = None) -> str | None:
    git = shutil.which("git")
    if not git:
        return None
    command = [git]
    if relative:
        command.extend(["-C", relative])
    command.extend(["rev-parse", "HEAD"])
    try:
        result = subprocess.run(
            command,
            cwd=repository,
            capture_output=True,
            text=True,
            check=False,
            timeout=30,
        )
    except (OSError, subprocess.TimeoutExpired):
        return None
    return result.stdout.strip().casefold() if result.returncode == 0 else None


def _git_exact_root(repository: Path) -> Path | None:
    """Resolve ``repository`` and return it only if it is the Git top-level.

    Git discovers its repository by searching upwards, so any per-directory
    question asked from a directory that is not itself a repository is
    silently answered by whatever unrelated checkout happens to enclose it.
    Returns the resolved root when ``git rev-parse --show-toplevel`` names
    exactly that directory, and ``None`` otherwise.
    """

    git = shutil.which("git")
    if not git:
        return None
    try:
        root = repository.expanduser().resolve()
    except (OSError, ValueError):
        return None
    if not root.is_dir():
        return None
    try:
        top = subprocess.run(
            [git, "rev-parse", "--show-toplevel"],
            cwd=root,
            capture_output=True,
            text=True,
            check=False,
            timeout=30,
        )
    except (OSError, subprocess.TimeoutExpired):
        return None
    discovered_raw = top.stdout.strip()
    if top.returncode != 0 or not discovered_raw:
        return None
    try:
        discovered = Path(discovered_raw).resolve()
    except (OSError, ValueError):
        return None
    if discovered != root:
        return None
    return root


def git_revision_at_exact_root(repository: Path) -> str | None:
    """Return HEAD only when ``repository`` is itself the Git top-level.

    ``git rev-parse HEAD`` searches upwards, so a directory that is not a
    repository -- an unpacked release bundle, say -- silently answers with the
    commit of whatever unrelated checkout happens to enclose it. An artifact
    that claims an origin it does not have is worse than one that admits it
    has none, so this refuses to answer unless the discovered top-level is the
    requested root exactly. Callers that want a stamped identity must carry
    one; they must not re-derive it from wherever they were installed.
    """

    root = _git_exact_root(repository)
    if root is None:
        return None
    return git_revision(root)


def git_worktree_clean_at_exact_root(repository: Path) -> bool:
    """Report cleanliness only when ``repository`` is itself the top-level.

    ``git status`` walks up exactly like ``git rev-parse``, so a directory
    that is not a repository inherits the clean/dirty verdict of whatever
    checkout encloses it -- the same class of confidently wrong answer as a
    borrowed commit. This fails closed to "not clean" unless the discovered
    top-level is the requested root exactly.
    """

    root = _git_exact_root(repository)
    if root is None:
        return False
    return git_worktree_clean(root)


def git_worktree_clean(repository: Path) -> bool:
    git = shutil.which("git")
    if not git:
        return False
    try:
        result = subprocess.run(
            [git, "status", "--porcelain", "--untracked-files=all"],
            cwd=repository,
            capture_output=True,
            text=True,
            check=False,
            timeout=30,
        )
    except (OSError, subprocess.TimeoutExpired):
        return False
    return result.returncode == 0 and not result.stdout.strip()


def run_checked(
    command: Sequence[str],
    *,
    cwd: Path | None = None,
    env: dict[str, str] | None = None,
) -> subprocess.CompletedProcess[str]:
    try:
        result = subprocess.run(
            list(command),
            cwd=cwd,
            check=False,
            capture_output=True,
            text=True,
            timeout=600,
            env=env,
        )
    except subprocess.TimeoutExpired as exc:
        rendered = " ".join(command)
        raise RuntimeError(f"conversion command timed out after 600 seconds: {rendered}") from exc
    if result.returncode:
        rendered = " ".join(command)
        raise RuntimeError(
            f"conversion command failed ({result.returncode}): {rendered}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    return result
