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
    configured = os.environ.get(env_name or "", "").strip() if env_name else ""
    if configured and Path(configured).is_file():
        return Path(configured).resolve()
    if name.casefold() in {"ffmpeg", "ffprobe"}:
        from .paths import default_state_root

        pinned = default_state_root() / "tools" / "ffmpeg-8.1.1" / "bin" / f"{name}.exe"
        if pinned.is_file():
            return pinned.resolve()
    found = shutil.which(name)
    return Path(found).resolve() if found else None


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
