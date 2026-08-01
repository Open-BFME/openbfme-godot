"""Provision and verify immutable external conversion tools.

No third-party binary or source is stored in the Open BFME repository.  Tools
are downloaded/copied into the user's external importer cache and are accepted
only when their pinned hashes/commits match this module.
"""

from __future__ import annotations

import os
from pathlib import Path
import shutil
import stat
import subprocess
import sys
import tempfile
import urllib.request
import zipfile
from typing import Any

from .big import sha256_file
from .paths import safe_relative_parts
from .tools import directory_tree_sha256
from .util import write_json_atomic


BLENDER_VERSION = "4.2.0"
BLENDER_URL = "https://download.blender.org/release/Blender4.2/blender-4.2.0-windows-x64.zip"
BLENDER_ZIP_SHA256 = "b6e72874f8cb5c4ed77f9b03d7f1fde851b9455a7ff02a1e1119c876318ebc65"
BLENDER_EXE_SHA256 = "80fb653019a0afb3bda0947ec74e84dc0a94d0d388f9b3849433c0e1a4efdabe"
BLENDER_TREE_SHA256 = "81e0cfb0d56ff5e33c2c562b13cc88257b9b34e072efa7ae054a6c87f13f2aa4"
PYTHON_VERSION = "3.12.13"
PYTHON_BUILD_TAG = "20260718"
PYTHON_URL = "https://github.com/astral-sh/python-build-standalone/releases/download/20260718/cpython-3.12.13%2B20260718-x86_64-pc-windows-msvc-install_only.tar.gz"
PYTHON_ARCHIVE_SHA256 = "56c9dd9681c4810cb8bfdec277ee2606d8ab17e678e5bc2bd138eb8098e330b6"
PYTHON_EXE_SHA256 = "32783151cd5dcf5196ff2fa342c11fc0909436531d4deec7824cbc29fd8c1a0c"
PLUGIN_REPOSITORY = "https://github.com/OpenSAGE/OpenSAGE.BlenderPlugin.git"
PLUGIN_COMMIT = "2de84023cb632a79a853b2a52f97c8002ed85142"
PLUGIN_SUBMODULE_COMMIT = "981aa2984117a1c686b7fa40d086794ce1c7665e"
FFMPEG_VERSION = "8.1.1"
FFMPEG_EXE_SHA256 = "228d7a8556258de907fdb55f36850078ebc7680b84ec30d84ea02e99bec1d1eb"
FFPROBE_EXE_SHA256 = "0fde260f5abd35c9cafd96f594cc76365a780c1b73a90e35b6a3409ea1db1bf0"
PILLOW_TREE_SHA256 = "18c02c91b31a5b2619eb1542144f0ef1f7ac4065eab7c5924f2640b3010fd7b0"
FONTTOOLS_VERSION = "4.61.1"
FONTTOOLS_TREE_SHA256 = "7903daa0e6e9be7c6d7bed6e39eed52fe2ba0f17107f4e398cd806f54dafecc3"
DEFUSEDXML_VERSION = "0.7.1"
DEFUSEDXML_TREE_SHA256 = "4a5bc129bad371fd21f6bb07621d2d331a1d2b192fef9b2bf78656b928c7738d"
PYTHON_LAUNCHER_SHA256 = "4ee5a32ca0fbfc6f4dd604fde80185cb0039d5a4bdca02ca698fffc5d9da52c7"
PYTHON_BASE_DLL_SHA256 = "60a12f6f0bdc0363544fcb3c824decf97d843ea7c3a9732f4ba02fa8b33cd6df"
PYTHON_RUNTIME_TREE_SHA256 = "af162c36194d692391e2a972537bfa57d6576d8ffd701b731aee1ee282b6b013"
PYTHON_RUNTIME_MAX_FILES = 20_000
PYTHON_RUNTIME_MAX_BYTES = 512 * 1024 * 1024
BLENDER_CACHE_PURGE_MAX_FILES = 10_000
BLENDER_CACHE_PURGE_MAX_DIRECTORIES = 2_000
BLENDER_CACHE_PURGE_MAX_BYTES = 512 * 1024 * 1024
PYTHON_RUNTIME_EXCLUDED_STDLIB = {
    "ensurepip",
    "idlelib",
    "lib2to3",
    "pydoc_data",
    "site-packages",
    "test",
    "tkinter",
    "turtledemo",
}


def _reject_tree_links(root: Path, label: str) -> None:
    requested_root = Path(root).expanduser()
    if _is_link_or_junction(requested_root):
        raise RuntimeError(f"{label} root is a link or junction: {requested_root}")
    try:
        resolved_root = requested_root.resolve(strict=True)
    except OSError as exc:
        raise RuntimeError(f"{label} root is unavailable: {requested_root}") from exc
    pending = [resolved_root]
    while pending:
        directory = pending.pop()
        try:
            with os.scandir(directory) as entries:
                children = sorted(entries, key=lambda item: item.name.casefold())
        except OSError as exc:
            raise RuntimeError(f"{label} tree scan failed: {directory}") from exc
        for child in children:
            path = Path(child.path)
            try:
                metadata = child.stat(follow_symlinks=False)
            except OSError as exc:
                raise RuntimeError(f"{label} tree scan failed: {path}") from exc
            if _is_link_or_junction(path):
                raise RuntimeError(f"{label} contains a link or junction: {path}")
            try:
                path.relative_to(resolved_root)
            except ValueError as exc:
                raise RuntimeError(
                    f"{label} path escaped its pinned root: {path}"
                ) from exc
            if stat.S_ISDIR(metadata.st_mode):
                pending.append(path)
            elif not stat.S_ISREG(metadata.st_mode):
                raise RuntimeError(f"{label} contains an unsupported path: {path}")


def _reject_python_bytecode(root: Path, label: str) -> None:
    """Reject generated Python caches that could shadow pinned source."""

    for path in root.rglob("*"):
        folded_parts = tuple(part.casefold() for part in path.parts)
        if path.is_file() and (
            path.suffix.casefold() in {".pyc", ".pyo"} or "__pycache__" in folded_parts
        ):
            raise RuntimeError(f"{label} contains generated Python bytecode: {path}")


def _is_link_or_junction(path: Path) -> bool:
    """Reject links, junctions, and any other Windows reparse point."""

    try:
        metadata = path.lstat()
    except OSError:
        return False
    reparse_flag = getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400)
    file_attributes = getattr(metadata, "st_file_attributes", 0)
    return stat.S_ISLNK(metadata.st_mode) or bool(file_attributes & reparse_flag)


def _contained_tool_path(path: Path, root: Path, label: str) -> Path:
    """Resolve a non-link tool path and prove that it remains below ``root``."""

    try:
        resolved = path.resolve(strict=True)
        resolved.relative_to(root)
    except (OSError, ValueError) as exc:
        raise RuntimeError(f"{label} path escaped its pinned root: {path}") from exc
    return resolved


def _purge_python_caches(root: Path, label: str) -> dict[str, int]:
    """Remove only bounded generated Python caches from a pinned tool tree.

    Blender's bundled Python can materialize bytecode in its portable directory
    even when no pinned source changed.  Those bytes must be removed *before*
    the full-tree hash is checked or Blender is executed.  The walk is
    deliberately two phase: every removal candidate and every traversed path
    is first proven to be an ordinary contained path, so a cache-shaped link or
    junction can never redirect deletion outside the pinned tree.
    """

    requested_root = Path(root).expanduser()
    if _is_link_or_junction(requested_root):
        raise RuntimeError(f"{label} root is a link or junction: {requested_root}")
    try:
        resolved_root = requested_root.resolve(strict=True)
    except OSError as exc:
        raise RuntimeError(f"{label} root is unavailable: {requested_root}") from exc
    if not resolved_root.is_dir():
        raise RuntimeError(f"{label} root is not a directory: {resolved_root}")

    cache_roots: list[Path] = []
    standalone_cache_files: list[Path] = []
    pending = [resolved_root]
    while pending:
        directory = pending.pop()
        try:
            with os.scandir(directory) as entries:
                children = sorted(entries, key=lambda item: item.name.casefold())
        except OSError as exc:
            raise RuntimeError(f"{label} cache scan failed: {directory}") from exc
        for child in children:
            path = Path(child.path)
            try:
                metadata = child.stat(follow_symlinks=False)
            except OSError as exc:
                raise RuntimeError(f"{label} cache scan failed: {path}") from exc
            if _is_link_or_junction(path):
                raise RuntimeError(f"{label} contains a link or junction: {path}")
            _contained_tool_path(path, resolved_root, label)
            if stat.S_ISDIR(metadata.st_mode):
                if child.name.casefold() == "__pycache__":
                    cache_roots.append(path)
                else:
                    pending.append(path)
            elif stat.S_ISREG(metadata.st_mode):
                if path.suffix.casefold() in {".pyc", ".pyo"}:
                    standalone_cache_files.append(path)
            else:
                raise RuntimeError(f"{label} contains an unsupported path: {path}")

    cache_files = list(standalone_cache_files)
    cache_directories: list[Path] = []
    for cache_root in cache_roots:
        cache_pending = [cache_root]
        while cache_pending:
            directory = cache_pending.pop()
            cache_directories.append(directory)
            try:
                with os.scandir(directory) as entries:
                    children = sorted(entries, key=lambda item: item.name.casefold())
            except OSError as exc:
                raise RuntimeError(f"{label} cache scan failed: {directory}") from exc
            for child in children:
                path = Path(child.path)
                try:
                    metadata = child.stat(follow_symlinks=False)
                except OSError as exc:
                    raise RuntimeError(f"{label} cache scan failed: {path}") from exc
                if _is_link_or_junction(path):
                    raise RuntimeError(f"{label} contains a link or junction: {path}")
                _contained_tool_path(path, resolved_root, label)
                if stat.S_ISDIR(metadata.st_mode):
                    cache_pending.append(path)
                elif stat.S_ISREG(metadata.st_mode):
                    cache_files.append(path)
                else:
                    raise RuntimeError(f"{label} contains an unsupported cache path: {path}")

    unique_files = sorted(
        set(cache_files), key=lambda path: path.relative_to(resolved_root).as_posix().casefold()
    )
    unique_directories = sorted(
        set(cache_directories),
        key=lambda path: (
            -len(path.relative_to(resolved_root).parts),
            path.relative_to(resolved_root).as_posix().casefold(),
        ),
    )
    byte_count = 0
    for path in unique_files:
        try:
            metadata = path.lstat()
        except OSError as exc:
            raise RuntimeError(f"{label} cache scan failed: {path}") from exc
        if _is_link_or_junction(path) or not stat.S_ISREG(metadata.st_mode):
            raise RuntimeError(f"{label} cache changed into an unsafe path: {path}")
        byte_count += metadata.st_size
    if (
        len(unique_files) > BLENDER_CACHE_PURGE_MAX_FILES
        or len(unique_directories) > BLENDER_CACHE_PURGE_MAX_DIRECTORIES
        or byte_count > BLENDER_CACHE_PURGE_MAX_BYTES
    ):
        raise RuntimeError(
            f"{label} generated cache exceeds the bounded purge: "
            f"files={len(unique_files)}, directories={len(unique_directories)}, "
            f"bytes={byte_count}"
        )

    for path in unique_files:
        try:
            metadata = path.lstat()
        except OSError as exc:
            raise RuntimeError(f"{label} cache file became unavailable: {path}") from exc
        if _is_link_or_junction(path) or not stat.S_ISREG(metadata.st_mode):
            raise RuntimeError(f"{label} cache changed into an unsafe path: {path}")
        _contained_tool_path(path, resolved_root, label)
        try:
            path.unlink()
        except OSError as exc:
            raise RuntimeError(f"{label} cache file could not be removed: {path}") from exc
    for path in unique_directories:
        try:
            metadata = path.lstat()
        except OSError as exc:
            raise RuntimeError(
                f"{label} cache directory became unavailable: {path}"
            ) from exc
        if _is_link_or_junction(path) or not stat.S_ISDIR(metadata.st_mode):
            raise RuntimeError(f"{label} cache changed into an unsafe path: {path}")
        _contained_tool_path(path, resolved_root, label)
        try:
            path.rmdir()
        except OSError as exc:
            raise RuntimeError(f"{label} cache directory could not be removed: {path}") from exc
    return {
        "removed_files": len(unique_files),
        "removed_directories": len(unique_directories),
        "removed_bytes": byte_count,
    }


def _purge_blender_python_caches(root: Path, label: str) -> dict[str, int]:
    """Backward-compatible name for the shared pinned-tool cache purge."""

    return _purge_python_caches(root, label)


def _attest_blender_portable_tree(executable: Path) -> str:
    """Read-only attestation of the exact pinned Blender bytes."""

    blender = Path(executable).expanduser()
    root = blender.parent
    _require_hash(blender, BLENDER_EXE_SHA256, "Blender executable")
    _reject_tree_links(root, "Blender portable tree")
    tree_digest = directory_tree_sha256(root)
    if tree_digest != BLENDER_TREE_SHA256:
        raise RuntimeError(
            "Blender portable tree differs from the pinned 4.2.0 distribution"
        )
    return tree_digest


def prepare_blender_portable_tree(
    state_root: Path,
    executable: Path | None = None,
) -> str:
    """Recover caches only in the state root's pinned tree, then attest it.

    An ``OPENBFME_BLENDER`` override is still attested before execution, but is
    never modified unless it resolves to this exact state-root-owned pin.
    """

    resolved_state_root = Path(state_root).expanduser().resolve()
    pinned = (
        resolved_state_root
        / "tools"
        / "blender-4.2.0-windows-x64"
        / "blender.exe"
    )
    selected = Path(executable or pinned).expanduser().resolve(strict=True)
    if selected == pinned:
        _purge_python_caches(selected.parent, "Blender portable tree")
    return _attest_blender_portable_tree(selected)


def _attest_opensage_plugin_checkout(plugin: Path) -> dict[str, str]:
    """Read-only attestation of the Git-pinned OpenSAGE plugin checkout."""

    checkout = Path(plugin).expanduser().resolve(strict=True)
    git = shutil.which("git")
    if not git:
        raise FileNotFoundError("git is required to attest the OpenSAGE W3D plugin")
    if not (checkout / ".git").exists():
        raise RuntimeError(f"OpenSAGE W3D plugin is not a Git checkout: {checkout}")
    _reject_tree_links(checkout, "OpenSAGE W3D plugin")
    # A present-but-invalid .git passes the existence check above while git
    # still walks up, so the commits below could otherwise be an enclosing
    # checkout's HEAD. Identity is only read at the exact root.
    _require_exact_git_root(git, checkout, "OpenSAGE W3D plugin")
    _require_exact_git_root(
        git,
        checkout / "io_mesh_w3d" / "blender_addon_updater",
        "OpenSAGE W3D plugin updater submodule",
    )
    commit = _run([git, "rev-parse", "HEAD"], cwd=checkout)
    submodule_commit = _run(
        [git, "-C", "io_mesh_w3d/blender_addon_updater", "rev-parse", "HEAD"],
        cwd=checkout,
    )
    if commit.casefold() != PLUGIN_COMMIT or submodule_commit.casefold() != PLUGIN_SUBMODULE_COMMIT:
        raise RuntimeError(
            "OpenSAGE W3D plugin or required updater submodule does not match "
            "the pinned commit"
        )
    _reject_python_bytecode(checkout, "OpenSAGE W3D plugin")
    if _run([git, "status", "--porcelain", "--untracked-files=all"], cwd=checkout):
        raise RuntimeError(
            "OpenSAGE W3D plugin worktree is dirty; conversion is not reproducible"
        )
    return {"commit": commit, "submodule_commit": submodule_commit}


def prepare_opensage_plugin_checkout(
    state_root: Path,
    plugin: Path | None = None,
) -> dict[str, str]:
    """Recover caches only in the state root's pinned plugin, then attest Git."""

    resolved_state_root = Path(state_root).expanduser().resolve()
    pinned = resolved_state_root / "tools" / "OpenSAGE.BlenderPlugin"
    selected = Path(plugin or pinned).expanduser().resolve(strict=True)
    if selected == pinned:
        _purge_python_caches(selected, "OpenSAGE W3D plugin")
    return _attest_opensage_plugin_checkout(selected)


def python_runtime_attestation() -> dict[str, Any]:
    """Hash the executable runtime surface used by the importer venv.

    The base interpreter DLL, stdlib sources, and extension modules live outside
    a Windows venv.  Hashing only ``Scripts/python.exe`` therefore does not bind
    the code that actually executes the importer.  Site packages are excluded
    here because Pillow, fontTools, and defusedxml are attested independently.
    Reproducible bytecode caches are excluded in favor of their source modules.
    """

    base = Path(sys.base_prefix).expanduser().resolve()
    version_tag = f"python{sys.version_info.major}{sys.version_info.minor}"
    base_dll = base / f"{version_tag}.dll"
    if os.name != "nt" or not base_dll.is_file():
        raise RuntimeError("the pinned importer Python runtime requires the Windows python312.dll layout")
    _reject_tree_links(base / "DLLs", "Python DLL tree")
    _reject_tree_links(base / "Lib", "Python stdlib tree")

    candidates: list[Path] = []
    for name in ("python.exe", "python3.dll", base_dll.name, "vcruntime140.dll", "vcruntime140_1.dll"):
        path = base / name
        if path.is_file():
            candidates.append(path)
    for directory_name in ("DLLs", "Lib"):
        directory = base / directory_name
        if not directory.is_dir():
            raise RuntimeError(f"Python runtime directory is missing: {directory}")
        for path in directory.rglob("*"):
            if not path.is_file():
                continue
            relative = path.relative_to(base)
            folded_parts = tuple(part.casefold() for part in relative.parts)
            if "__pycache__" in folded_parts or path.suffix.casefold() in {".pyc", ".pyo"}:
                continue
            if (
                len(folded_parts) >= 2
                and folded_parts[0] == "lib"
                and folded_parts[1] in PYTHON_RUNTIME_EXCLUDED_STDLIB
            ):
                continue
            candidates.append(path)

    unique = sorted(
        {path.resolve() for path in candidates},
        key=lambda path: path.relative_to(base).as_posix().casefold(),
    )
    if len(unique) > PYTHON_RUNTIME_MAX_FILES:
        raise RuntimeError("Python runtime tree exceeds the pinned file-count bound")
    total_bytes = sum(path.stat().st_size for path in unique)
    if total_bytes > PYTHON_RUNTIME_MAX_BYTES:
        raise RuntimeError("Python runtime tree exceeds the pinned byte bound")

    import hashlib

    digest = hashlib.sha256()
    for path in unique:
        is_junction = getattr(path, "is_junction", None)
        if path.is_symlink() or bool(is_junction and is_junction()):
            raise RuntimeError(f"Python runtime tree contains a link or junction: {path}")
        relative = path.relative_to(base).as_posix()
        digest.update(relative.encode("utf-8"))
        digest.update(b"\0")
        digest.update(str(path.stat().st_size).encode("ascii"))
        digest.update(b"\0")
        digest.update(sha256_file(path).encode("ascii"))
        digest.update(b"\n")
    return {
        "version": sys.version.split()[0],
        "launcher_sha256": sha256_file(Path(sys.executable).resolve()),
        "base_dll_sha256": sha256_file(base_dll),
        "tree_sha256": digest.hexdigest(),
        "file_count": len(unique),
        "total_bytes": total_bytes,
        "excludes": [
            *[f"Lib/{name}" for name in sorted(PYTHON_RUNTIME_EXCLUDED_STDLIB)],
            "__pycache__",
            "*.pyc",
            "*.pyo",
        ],
    }


def _run(command: list[str], *, cwd: Path | None = None) -> str:
    try:
        result = subprocess.run(
            command,
            cwd=cwd,
            capture_output=True,
            text=True,
            check=False,
            timeout=600,
        )
    except subprocess.TimeoutExpired as exc:
        raise RuntimeError(f"tool bootstrap timed out: {' '.join(command)}") from exc
    if result.returncode:
        raise RuntimeError(
            f"tool bootstrap command failed ({result.returncode}): {' '.join(command)}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    return result.stdout.strip()


def _require_exact_git_root(git: str, root: Path, label: str) -> None:
    """Refuse Git identity questions that would be answered by an enclosing repo.

    A ``.git`` entry that exists but is not a valid repository does not stop
    Git's upward discovery, so ``git rev-parse HEAD`` and ``git status`` run
    from such a directory report the identity and cleanliness of whatever
    unrelated checkout encloses it -- a confidently wrong answer, not an
    error. An attestation may only trust answers read at the exact requested
    root.
    """

    if not root.is_dir():
        raise RuntimeError(f"{label} directory is missing: {root}")
    discovered_raw = _run([git, "rev-parse", "--show-toplevel"], cwd=root)
    try:
        discovered = Path(discovered_raw).resolve() if discovered_raw else None
    except (OSError, ValueError):
        discovered = None
    if discovered != Path(root).resolve():
        raise RuntimeError(
            f"{label} is not itself a Git repository root: {root} "
            f"(git top-level: {discovered_raw or 'unknown'}); refusing to "
            "inherit an enclosing checkout's identity"
        )


def _require_hash(path: Path, expected: str, label: str) -> None:
    actual = sha256_file(path)
    if actual.casefold() != expected.casefold():
        raise RuntimeError(f"{label} hash mismatch: expected {expected}, got {actual}")


def _download_blender(tools_root: Path) -> tuple[Path, str]:
    zip_path = tools_root / "blender-4.2.0-windows-x64.zip"
    destination = tools_root / "blender-4.2.0-windows-x64"
    executable = destination / "blender.exe"
    if executable.is_file():
        tree_digest = prepare_blender_portable_tree(tools_root.parent, executable)
        return executable, tree_digest
    if zip_path.is_file():
        _require_hash(zip_path, BLENDER_ZIP_SHA256, "Blender archive")
    else:
        temporary = zip_path.with_suffix(".zip.downloading")
        temporary.unlink(missing_ok=True)
        urllib.request.urlretrieve(BLENDER_URL, temporary)
        _require_hash(temporary, BLENDER_ZIP_SHA256, "Blender archive")
        os.replace(temporary, zip_path)
    with tempfile.TemporaryDirectory(dir=tools_root, prefix="blender-extract-") as raw:
        staging_root = Path(raw)
        with zipfile.ZipFile(zip_path) as archive:
            members = archive.infolist()
            if len(members) > 50_000 or sum(item.file_size for item in members) > 2 * 1024 * 1024 * 1024:
                raise RuntimeError("Blender ZIP exceeds bootstrap safety limits")
            for member in members:
                name = member.filename.replace("\\", "/")
                if name.endswith("/"):
                    name = name.rstrip("/")
                try:
                    parts = safe_relative_parts(name)
                except ValueError as exc:
                    raise RuntimeError(f"Blender ZIP contains an unsafe path: {name}") from exc
                target = (staging_root / Path(*parts)).resolve()
                try:
                    target.relative_to(staging_root.resolve())
                except ValueError as exc:
                    raise RuntimeError(f"Blender ZIP contains an unsafe path: {name}") from exc
            archive.extractall(staging_root)
        extracted = staging_root / "blender-4.2.0-windows-x64"
        tree_digest = _attest_blender_portable_tree(extracted / "blender.exe")
        os.replace(extracted, destination)
    return executable, tree_digest


def _checkout_plugin(tools_root: Path) -> Path:
    destination = tools_root / "OpenSAGE.BlenderPlugin"
    git = shutil.which("git")
    if not git:
        raise FileNotFoundError("git is required to provision the OpenSAGE W3D plugin")
    if (destination / ".git").exists():
        prepare_opensage_plugin_checkout(tools_root.parent, destination)
        return destination
    if not (destination / ".git").exists():
        _run([git, "clone", "--no-checkout", PLUGIN_REPOSITORY, str(destination)])
    _run([git, "fetch", "--depth", "1", "origin", PLUGIN_COMMIT], cwd=destination)
    _run([git, "checkout", "--detach", PLUGIN_COMMIT], cwd=destination)
    _run([git, "submodule", "update", "--init", "--depth", "1"], cwd=destination)
    _attest_opensage_plugin_checkout(destination)
    return destination


def _ffmpeg_candidates(configured: Path | None, tools_root: Path) -> list[Path]:
    candidates: list[Path] = []
    if configured:
        candidates.append(configured)
    env_value = os.environ.get("OPENBFME_FFMPEG", "").strip()
    if env_value:
        candidates.append(Path(env_value))
    candidates.extend(
        [
            tools_root / "ffmpeg-8.1.1" / "bin" / "ffmpeg.exe",
        ]
    )
    found = shutil.which("ffmpeg")
    if found:
        candidates.append(Path(found))
    return candidates


def _pin_ffmpeg(tools_root: Path, configured: Path | None) -> tuple[Path, Path]:
    source: Path | None = None
    for candidate in _ffmpeg_candidates(configured, tools_root):
        path = candidate.expanduser().resolve()
        if path.is_file() and sha256_file(path).casefold() == FFMPEG_EXE_SHA256:
            source = path
            break
    if not source:
        raise FileNotFoundError(
            "pinned FFmpeg 8.1.1 was not found; pass bootstrap-tools --ffmpeg <ffmpeg.exe>"
        )
    source_probe = source.with_name("ffprobe.exe")
    if not source_probe.is_file():
        raise FileNotFoundError(f"ffprobe.exe is missing beside {source}")
    _require_hash(source_probe, FFPROBE_EXE_SHA256, "FFprobe executable")
    destination_dir = tools_root / "ffmpeg-8.1.1" / "bin"
    destination_dir.mkdir(parents=True, exist_ok=True)
    destination = destination_dir / "ffmpeg.exe"
    destination_probe = destination_dir / "ffprobe.exe"
    if source != destination:
        shutil.copyfile(source, destination)
        shutil.copyfile(source_probe, destination_probe)
    _require_hash(destination, FFMPEG_EXE_SHA256, "FFmpeg executable")
    _require_hash(destination_probe, FFPROBE_EXE_SHA256, "FFprobe executable")
    return destination, destination_probe


def bootstrap_tools(state_root: Path, ffmpeg_source: Path | None = None) -> dict[str, Any]:
    tools_root = state_root.expanduser().resolve() / "tools"
    tools_root.mkdir(parents=True, exist_ok=True)
    blender, blender_tree_sha256 = _download_blender(tools_root)
    plugin = _checkout_plugin(tools_root)
    ffmpeg, ffprobe = _pin_ffmpeg(tools_root, ffmpeg_source)
    try:
        import PIL
        import defusedxml
        import fontTools
    except ImportError as exc:
        raise FileNotFoundError(
            "a pinned importer Python dependency is missing; "
            "run tools/bootstrap-importer-python.ps1"
        ) from exc
    dependency_versions = (
        PIL.__version__,
        fontTools.__version__,
        defusedxml.__version__,
    )
    if sys.version.split()[0] != PYTHON_VERSION or dependency_versions != (
        "12.2.0",
        FONTTOOLS_VERSION,
        DEFUSEDXML_VERSION,
    ):
        raise RuntimeError(
            f"expected Python {PYTHON_VERSION}, Pillow 12.2.0, "
            f"fontTools {FONTTOOLS_VERSION}, and defusedxml {DEFUSEDXML_VERSION}; "
            f"found {sys.version.split()[0]}, {', '.join(dependency_versions)}"
        )
    python_runtime = python_runtime_attestation()
    if (
        python_runtime["launcher_sha256"] != PYTHON_LAUNCHER_SHA256
        or python_runtime["base_dll_sha256"] != PYTHON_BASE_DLL_SHA256
        or python_runtime["tree_sha256"] != PYTHON_RUNTIME_TREE_SHA256
    ):
        raise RuntimeError(
            f"Python base runtime differs from the pinned {PYTHON_VERSION} surface; "
            "reinstall the pinned interpreter before recreating the importer environment"
        )
    pillow_root = Path(PIL.__file__).resolve().parent
    _reject_tree_links(pillow_root, "Pillow package tree")
    pillow_tree_sha256 = directory_tree_sha256(
        pillow_root,
        ignore_python_cache=True,
    )
    if pillow_tree_sha256 != PILLOW_TREE_SHA256:
        raise RuntimeError(
            "Pillow package tree differs from the pinned 12.2.0 wheel; "
            "recreate the external importer Python environment"
        )
    fonttools_root = Path(fontTools.__file__).resolve().parent
    defusedxml_root = Path(defusedxml.__file__).resolve().parent
    _reject_tree_links(fonttools_root, "fontTools package tree")
    _reject_tree_links(defusedxml_root, "defusedxml package tree")
    fonttools_tree_sha256 = directory_tree_sha256(
        fonttools_root,
        ignore_python_cache=True,
    )
    defusedxml_tree_sha256 = directory_tree_sha256(
        defusedxml_root,
        ignore_python_cache=True,
    )
    if fonttools_tree_sha256 != FONTTOOLS_TREE_SHA256:
        raise RuntimeError(
            f"fontTools package tree differs from the pinned {FONTTOOLS_VERSION} wheel; "
            "recreate the external importer Python environment"
        )
    if defusedxml_tree_sha256 != DEFUSEDXML_TREE_SHA256:
        raise RuntimeError(
            f"defusedxml package tree differs from the pinned {DEFUSEDXML_VERSION} wheel; "
            "recreate the external importer Python environment"
        )
    manifest = {
        "format": 1,
        "tools": {
            "blender": {
                "version": BLENDER_VERSION,
                "source": BLENDER_URL,
                "archive_sha256": BLENDER_ZIP_SHA256,
                "executable_sha256": BLENDER_EXE_SHA256,
                "tree_sha256": blender_tree_sha256,
                "license": "GPL-3.0-or-later",
                "path": str(blender),
            },
            "opensage_w3d_plugin": {
                "source": PLUGIN_REPOSITORY,
                "commit": PLUGIN_COMMIT,
                "python_bytecode_free": True,
                "license": "LGPL-3.0",
                "path": str(plugin),
                "submodules": {
                    "blender_addon_updater": {
                        "commit": PLUGIN_SUBMODULE_COMMIT,
                        "license": "GPL-3.0",
                    }
                },
            },
            "ffmpeg": {
                "version": FFMPEG_VERSION,
                "executable_sha256": FFMPEG_EXE_SHA256,
                "ffprobe_sha256": FFPROBE_EXE_SHA256,
                "license": "GPLv3 build",
                "path": str(ffmpeg),
                "ffprobe_path": str(ffprobe),
            },
            "python": {
                **python_runtime,
                "executable": str(Path(sys.executable).resolve()),
            },
            "pillow": {
                "version": PIL.__version__,
                "tree_sha256": pillow_tree_sha256,
                "license": "MIT-CMU",
            },
            "fonttools": {
                "version": fontTools.__version__,
                "tree_sha256": fonttools_tree_sha256,
                "license": "MIT",
            },
            "defusedxml": {
                "version": defusedxml.__version__,
                "tree_sha256": defusedxml_tree_sha256,
                "license": "PSFL",
            },
        },
    }
    manifest_path = tools_root / "tool-manifest.json"
    write_json_atomic(manifest_path, manifest)
    return {"ready": True, "manifest": str(manifest_path), **manifest}


def tool_status(
    state_root: Path, *, skip_w3d_attestation: bool = False
) -> dict[str, Any]:
    tools_root = state_root.expanduser().resolve() / "tools"
    blender = tools_root / "blender-4.2.0-windows-x64" / "blender.exe"
    plugin = tools_root / "OpenSAGE.BlenderPlugin"
    ffmpeg = tools_root / "ffmpeg-8.1.1" / "bin" / "ffmpeg.exe"
    git = shutil.which("git")
    plugin_commit = ""
    submodule_commit = ""
    if not skip_w3d_attestation and git and (plugin / ".git").exists():
        try:
            # Exact-root only: a walking rev-parse would report an enclosing
            # checkout's HEAD as this plugin's commit. Missing stays "".
            _require_exact_git_root(git, plugin, "OpenSAGE W3D plugin")
            _require_exact_git_root(
                git,
                plugin / "io_mesh_w3d" / "blender_addon_updater",
                "OpenSAGE W3D plugin updater submodule",
            )
            plugin_commit = _run([git, "rev-parse", "HEAD"], cwd=plugin)
            submodule_commit = _run(
                [git, "-C", "io_mesh_w3d/blender_addon_updater", "rev-parse", "HEAD"],
                cwd=plugin,
            )
        except RuntimeError:
            pass
    ffprobe = ffmpeg.with_name("ffprobe.exe")
    try:
        import PIL

        _reject_tree_links(Path(PIL.__file__).resolve().parent, "Pillow package tree")
        pillow_ready = PIL.__version__ == "12.2.0"
        pillow_tree_ready = (
            directory_tree_sha256(
                Path(PIL.__file__).resolve().parent,
                ignore_python_cache=True,
            )
            == PILLOW_TREE_SHA256
        )
    except (ImportError, OSError, RuntimeError):
        pillow_ready = False
        pillow_tree_ready = False
    try:
        import fontTools

        fonttools_root = Path(fontTools.__file__).resolve().parent
        _reject_tree_links(fonttools_root, "fontTools package tree")
        fonttools_ready = fontTools.__version__ == FONTTOOLS_VERSION
        fonttools_tree_ready = (
            directory_tree_sha256(fonttools_root, ignore_python_cache=True)
            == FONTTOOLS_TREE_SHA256
        )
    except (ImportError, OSError, RuntimeError):
        fonttools_ready = False
        fonttools_tree_ready = False
    try:
        import defusedxml

        defusedxml_root = Path(defusedxml.__file__).resolve().parent
        _reject_tree_links(defusedxml_root, "defusedxml package tree")
        defusedxml_ready = defusedxml.__version__ == DEFUSEDXML_VERSION
        defusedxml_tree_ready = (
            directory_tree_sha256(defusedxml_root, ignore_python_cache=True)
            == DEFUSEDXML_TREE_SHA256
        )
    except (ImportError, OSError, RuntimeError):
        defusedxml_ready = False
        defusedxml_tree_ready = False
    try:
        python_runtime = python_runtime_attestation()
        python_runtime_ready = (
            python_runtime["version"] == PYTHON_VERSION
            and python_runtime["launcher_sha256"] == PYTHON_LAUNCHER_SHA256
            and python_runtime["base_dll_sha256"] == PYTHON_BASE_DLL_SHA256
            and python_runtime["tree_sha256"] == PYTHON_RUNTIME_TREE_SHA256
        )
    except (OSError, RuntimeError):
        python_runtime = {}
        python_runtime_ready = False
    plugin_clean = False
    if not skip_w3d_attestation and git and (plugin / ".git").exists():
        try:
            _reject_tree_links(plugin, "OpenSAGE W3D plugin")
            _reject_python_bytecode(plugin, "OpenSAGE W3D plugin")
            # git status discovers its repository by walking up exactly like
            # rev-parse; only the exact root's verdict is this plugin's own.
            _require_exact_git_root(git, plugin, "OpenSAGE W3D plugin")
            plugin_clean = not bool(
                _run([git, "status", "--porcelain", "--untracked-files=all"], cwd=plugin)
            )
        except RuntimeError:
            plugin_clean = False
    blender_tree_ready = False
    if not skip_w3d_attestation and blender.is_file():
        try:
            _reject_tree_links(blender.parent, "Blender portable tree")
            blender_tree_ready = directory_tree_sha256(blender.parent) == BLENDER_TREE_SHA256
        except (OSError, RuntimeError):
            blender_tree_ready = False
    checks = {
        "blender": blender.is_file() and sha256_file(blender).casefold() == BLENDER_EXE_SHA256,
        "blender_tree": blender_tree_ready,
        "opensage_w3d_plugin": (
            (plugin / "io_mesh_w3d" / "__init__.py").is_file()
            and plugin_commit.casefold() == PLUGIN_COMMIT
            and submodule_commit.casefold() == PLUGIN_SUBMODULE_COMMIT
            and plugin_clean
        ),
        "ffmpeg": ffmpeg.is_file() and sha256_file(ffmpeg).casefold() == FFMPEG_EXE_SHA256,
        "ffprobe": ffprobe.is_file() and sha256_file(ffprobe).casefold() == FFPROBE_EXE_SHA256,
        "python": sys.version.split()[0] == PYTHON_VERSION,
        "python_runtime": python_runtime_ready,
        "pillow": pillow_ready,
        "pillow_tree": pillow_tree_ready,
        "fonttools": fonttools_ready,
        "fonttools_tree": fonttools_tree_ready,
        "defusedxml": defusedxml_ready,
        "defusedxml_tree": defusedxml_tree_ready,
    }
    return {
        "ready": all(checks.values()),
        "checks": checks,
        "python_runtime": python_runtime,
        "manifest": str(tools_root / "tool-manifest.json"),
    }
