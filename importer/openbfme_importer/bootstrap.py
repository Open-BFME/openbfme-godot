"""Provision and verify immutable external conversion tools.

No third-party binary or source is stored in the Open BFME repository.  Tools
are downloaded/copied into the user's external importer cache and are accepted
only when their pinned hashes/commits match this module.
"""

from __future__ import annotations

import os
from pathlib import Path
import shutil
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
PLUGIN_REPOSITORY = "https://github.com/OpenSAGE/OpenSAGE.BlenderPlugin.git"
PLUGIN_COMMIT = "2de84023cb632a79a853b2a52f97c8002ed85142"
PLUGIN_SUBMODULE_COMMIT = "981aa2984117a1c686b7fa40d086794ce1c7665e"
FFMPEG_VERSION = "8.1.1"
FFMPEG_EXE_SHA256 = "228d7a8556258de907fdb55f36850078ebc7680b84ec30d84ea02e99bec1d1eb"
FFPROBE_EXE_SHA256 = "0fde260f5abd35c9cafd96f594cc76365a780c1b73a90e35b6a3409ea1db1bf0"
PILLOW_TREE_SHA256 = "18c02c91b31a5b2619eb1542144f0ef1f7ac4065eab7c5924f2640b3010fd7b0"
PYTHON_VERSION = "3.12.10"
PYTHON_LAUNCHER_SHA256 = "0b471133e110cfb53a061cad528ce8e517d7b9ac41a0a396c39ad795a487fc14"
PYTHON_BASE_DLL_SHA256 = "9a0e3435aaa680d868150f87ab3e388ad2eebc22f87e036155c7b4eda8cd2120"
PYTHON_RUNTIME_TREE_SHA256 = "98348e31da2e14c684372bf02fee52b71984d28d8a91b82dbe0fe9aa2f6561d7"
PYTHON_RUNTIME_MAX_FILES = 20_000
PYTHON_RUNTIME_MAX_BYTES = 512 * 1024 * 1024
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
    for path in root.rglob("*"):
        is_junction = getattr(path, "is_junction", None)
        if path.is_symlink() or bool(is_junction and is_junction()):
            raise RuntimeError(f"{label} contains a link or junction: {path}")


def _reject_python_bytecode(root: Path, label: str) -> None:
    """Reject generated Python caches that could shadow pinned source."""

    for path in root.rglob("*"):
        folded_parts = tuple(part.casefold() for part in path.parts)
        if path.is_file() and (
            path.suffix.casefold() in {".pyc", ".pyo"} or "__pycache__" in folded_parts
        ):
            raise RuntimeError(f"{label} contains generated Python bytecode: {path}")


def python_runtime_attestation() -> dict[str, Any]:
    """Hash the executable runtime surface used by the importer venv.

    The base interpreter DLL, stdlib sources, and extension modules live outside
    a Windows venv.  Hashing only ``Scripts/python.exe`` therefore does not bind
    the code that actually executes the importer.  Site packages are excluded:
    Pillow is pinned independently and no other package is part of the contract.
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


def _require_hash(path: Path, expected: str, label: str) -> None:
    actual = sha256_file(path)
    if actual.casefold() != expected.casefold():
        raise RuntimeError(f"{label} hash mismatch: expected {expected}, got {actual}")


def _download_blender(tools_root: Path) -> tuple[Path, str]:
    zip_path = tools_root / "blender-4.2.0-windows-x64.zip"
    destination = tools_root / "blender-4.2.0-windows-x64"
    executable = destination / "blender.exe"
    if executable.is_file():
        _require_hash(executable, BLENDER_EXE_SHA256, "Blender executable")
        _reject_tree_links(destination, "Blender portable tree")
        tree_digest = directory_tree_sha256(destination)
        if tree_digest != BLENDER_TREE_SHA256:
            raise RuntimeError(
                "Blender portable tree differs from the pinned 4.2.0 distribution; "
                "remove the external tool directory and bootstrap it again"
            )
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
        _require_hash(extracted / "blender.exe", BLENDER_EXE_SHA256, "Blender executable")
        _reject_tree_links(extracted, "Blender portable tree")
        tree_digest = directory_tree_sha256(extracted)
        if tree_digest != BLENDER_TREE_SHA256:
            raise RuntimeError("extracted Blender portable tree hash mismatch")
        os.replace(extracted, destination)
    return executable, tree_digest


def _checkout_plugin(tools_root: Path) -> Path:
    destination = tools_root / "OpenSAGE.BlenderPlugin"
    git = shutil.which("git")
    if not git:
        raise FileNotFoundError("git is required to provision the OpenSAGE W3D plugin")
    if (destination / ".git").exists():
        try:
            current = _run([git, "rev-parse", "HEAD"], cwd=destination)
            current_submodule = _run(
                [git, "-C", "io_mesh_w3d/blender_addon_updater", "rev-parse", "HEAD"],
                cwd=destination,
            )
            if current.casefold() == PLUGIN_COMMIT and current_submodule.casefold() == PLUGIN_SUBMODULE_COMMIT:
                _reject_python_bytecode(destination, "OpenSAGE W3D plugin")
                if _run([git, "status", "--porcelain", "--untracked-files=all"], cwd=destination):
                    raise RuntimeError(
                        "OpenSAGE W3D plugin cache is dirty; repair or remove it before bootstrap"
                    )
                return destination
        except RuntimeError:
            pass
    if not (destination / ".git").exists():
        _run([git, "clone", "--no-checkout", PLUGIN_REPOSITORY, str(destination)])
    _run([git, "fetch", "--depth", "1", "origin", PLUGIN_COMMIT], cwd=destination)
    _run([git, "checkout", "--detach", PLUGIN_COMMIT], cwd=destination)
    _run([git, "submodule", "update", "--init", "--depth", "1"], cwd=destination)
    actual = _run([git, "rev-parse", "HEAD"], cwd=destination)
    if actual.casefold() != PLUGIN_COMMIT:
        raise RuntimeError(f"OpenSAGE plugin commit mismatch: {actual}")
    submodule = _run(
        [git, "-C", "io_mesh_w3d/blender_addon_updater", "rev-parse", "HEAD"],
        cwd=destination,
    )
    if submodule.casefold() != PLUGIN_SUBMODULE_COMMIT:
        raise RuntimeError(f"OpenSAGE updater submodule mismatch: {submodule}")
    _reject_python_bytecode(destination, "OpenSAGE W3D plugin")
    if _run([git, "status", "--porcelain", "--untracked-files=all"], cwd=destination):
        raise RuntimeError("OpenSAGE W3D plugin cache is dirty after checkout")
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
            Path(r"C:\AudioGen\tools\ffmpeg-portable\ffmpeg-8.1.1-essentials_build\bin\ffmpeg.exe"),
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
    except ImportError as exc:
        raise FileNotFoundError(
            "Pillow 12.2.0 is missing; run tools/bootstrap-importer-python.ps1"
        ) from exc
    if sys.version.split()[0] != PYTHON_VERSION or PIL.__version__ != "12.2.0":
        raise RuntimeError(
            f"expected Python {PYTHON_VERSION} + Pillow 12.2.0, "
            f"found {sys.version.split()[0]} + {PIL.__version__}"
        )
    python_runtime = python_runtime_attestation()
    if (
        python_runtime["launcher_sha256"] != PYTHON_LAUNCHER_SHA256
        or python_runtime["base_dll_sha256"] != PYTHON_BASE_DLL_SHA256
        or python_runtime["tree_sha256"] != PYTHON_RUNTIME_TREE_SHA256
    ):
        raise RuntimeError(
            "Python base runtime differs from the pinned 3.12.10 surface; "
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
        },
    }
    manifest_path = tools_root / "tool-manifest.json"
    write_json_atomic(manifest_path, manifest)
    return {"ready": True, "manifest": str(manifest_path), **manifest}


def tool_status(state_root: Path) -> dict[str, Any]:
    tools_root = state_root.expanduser().resolve() / "tools"
    blender = tools_root / "blender-4.2.0-windows-x64" / "blender.exe"
    plugin = tools_root / "OpenSAGE.BlenderPlugin"
    ffmpeg = tools_root / "ffmpeg-8.1.1" / "bin" / "ffmpeg.exe"
    git = shutil.which("git")
    plugin_commit = ""
    submodule_commit = ""
    if git and (plugin / ".git").exists():
        try:
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
    if git and (plugin / ".git").exists():
        try:
            _reject_python_bytecode(plugin, "OpenSAGE W3D plugin")
            plugin_clean = not bool(
                _run([git, "status", "--porcelain", "--untracked-files=all"], cwd=plugin)
            )
        except RuntimeError:
            plugin_clean = False
    blender_tree_ready = False
    if blender.is_file():
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
    }
    return {
        "ready": all(checks.values()),
        "checks": checks,
        "python_runtime": python_runtime,
        "manifest": str(tools_root / "tool-manifest.json"),
    }
