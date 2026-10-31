#!/usr/bin/env python3
"""Build the combined game-and-launcher Apple Silicon preview from a clean committed tree."""
from __future__ import annotations

import hashlib
import io
import json
import os
import plistlib
import re
import shutil
import struct
import subprocess
import sys
import tarfile
import tempfile
import zipfile
from datetime import datetime, timezone
from pathlib import Path, PurePosixPath

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
sys.path.insert(0, str(HERE))

import audit_package  # noqa: E402
import crash_key  # noqa: E402
import lib_provenance  # noqa: E402
import make_archive  # noqa: E402
import release_keys  # noqa: E402

VERSION = "v0.4.6 Mac Unstable Preview (Apple Silicon)"
STEM = "openbfme-v0.4.6-macos-arm64-unstable-preview"
PLATFORM = "game-macos-arm64-unstable-preview"
BRANCH = "devin/mac-preview-0.4.6"
PRESET = "macOS Apple Silicon"
LAUNCHER_PRESET = "macOS Apple Silicon Preview"
LAUNCHER_APP = "OpenBFME Launcher.app"
LAUNCHER_EXECUTABLE = "OpenBFME Launcher"
LAUNCHER_PACK = "OpenBFME Launcher.pck"
TEMPLATE_SHA256 = "f298490b8d44d934be425a5a65a51bf15f422428b229a06a6e11d9ffea248011"
TEMPLATE_VERSION = "4.7.2.stable"
ARM64_LIBRARY = "openbfme.macos.template_debug.arm64.dylib"
TEMPLATE_PATH_PREFIX_SHA256 = "48b62f909b7d3a93f25f72ae140fad11c0f8b1d55e397f776bef2dd76bcccbd3"
TEMPLATE_ARM64_SHA256 = {
    "godot_macos_release.arm64": "e914bf38d2369aa0066dc3dbb825744fce98b10fed3a7fc66305875ded0e6b06",
    "godot_macos_debug.arm64": "a379dbff1a9db1a45b28d28addbeea74b8d54ecf749b3d6b3ce113d4ad40d2fd",
}
TEMPLATE_PATH_PATTERN = re.compile(
    rb"/Users/[^/\x00]+/\.cargo/registry/src/index\.crates\.io-[a-z0-9]+/"
)
TEMPLATE_PATH_REPLACEMENT = (
    b"/godot-upstream/"
    + b"_" * (67 - len(b"/godot-upstream/") - 1)
    + b"/"
)
MANIFEST_ENTRIES = {
    "macos.debug.arm64": f"res://bin/{ARM64_LIBRARY}",
    "macos.release.arm64": f"res://bin/{ARM64_LIBRARY}",
}


def fail(message: str) -> None:
    raise SystemExit(f"PREVIEW PACKAGE FAIL: {message}")


def run(args: list[str], *, cwd: Path = REPO, env=None, allow_failure: bool = False) -> subprocess.CompletedProcess:
    try:
        result = subprocess.run(args, cwd=cwd, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, check=False)
    except OSError as exc:
        fail(f"cannot run {args[0]}: {exc}")
    if result.returncode and not allow_failure:
        fail(f"{args[0]} exited {result.returncode}:\n{result.stdout[-5000:]}")
    return result


def run_bytes(args: list[str]) -> bytes:
    try:
        result = subprocess.run(args, cwd=REPO, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
    except OSError as exc:
        fail(f"cannot run {args[0]}: {exc}")
    if result.returncode:
        fail(f"{args[0]} exited {result.returncode}: {result.stderr.decode('utf-8', 'replace')[-4000:]}")
    return result.stdout


def repo_path(value: str) -> Path:
    path = Path(value).expanduser()
    return path.resolve() if path.is_absolute() else (REPO / path).resolve()


def git_text(*args: str) -> str:
    return run(["git", "-C", str(REPO), *args]).stdout.strip()


def clean_commit(expected_branch: str = BRANCH, *, require_preview_branch: bool = True, require_full_history: bool = True) -> tuple[str, int]:
    if require_preview_branch:
        branch = git_text("branch", "--show-current")
        if not expected_branch or branch != expected_branch:
            fail(f"expected branch {expected_branch}, found {branch}")
    status = git_text("status", "--porcelain=v1", "--untracked-files=all")
    if status:
        fail("the working tree is not clean; commit all source changes first")
    commit = git_text("rev-parse", "HEAD")
    if not re.fullmatch(r"[0-9a-f]{40}", commit):
        fail("HEAD is not a full Git commit")
    if require_full_history and git_text("rev-parse", "--is-shallow-repository") != "false":
        fail("a non-shallow repository is required for reproducible packaging")
    epoch = int(git_text("show", "-s", "--format=%ct", commit))
    return commit, epoch


def extract_git_archive(paths: list[str], destination: Path) -> None:
    data = run_bytes(["git", "-C", str(REPO), "archive", "--format=tar", "HEAD", *paths])
    destination.mkdir(parents=True, exist_ok=True)
    with tarfile.open(fileobj=io.BytesIO(data), mode="r:") as archive:
        for member in archive.getmembers():
            name = PurePosixPath(member.name)
            if name.is_absolute() or "\\" in member.name or any(part in ("", ".", "..") for part in name.parts):
                fail(f"unsafe path in committed archive: {member.name}")
            target = destination.joinpath(*name.parts)
            if member.isdir():
                target.mkdir(parents=True, exist_ok=True)
            elif member.isfile():
                target.parent.mkdir(parents=True, exist_ok=True)
                source = archive.extractfile(member)
                if source is None:
                    fail(f"cannot read committed source: {member.name}")
                content = source.read()
                if content.startswith(b"version https://git-lfs.github.com/spec/v1\n"):
                    pointer = re.fullmatch(
                        rb"version https://git-lfs.github.com/spec/v1\noid sha256:([0-9a-f]{64})\nsize ([0-9]+)\n", content,
                    )
                    if not pointer:
                        fail(f"unsupported Git LFS pointer: {member.name}")
                    working_file = REPO / member.name
                    if not working_file.is_file() or working_file.is_symlink():
                        fail(f"missing Git LFS object: {member.name}; run git lfs pull first")
                    content = working_file.read_bytes()
                    if len(content) != int(pointer[2]) or hashlib.sha256(content).hexdigest().encode() != pointer[1]:
                        fail(f"Git LFS content does not match committed pointer: {member.name}; run git lfs pull first")
                target.write_bytes(content)
                os.chmod(target, (member.mode & 0o111) | 0o644)
            else:
                fail(f"committed archive contains a non-regular member: {member.name}")


def arm64_manifest_text(source: str) -> str:
    output = []
    section = ""
    seen: dict[str, int] = {}
    for line in source.splitlines(keepends=True):
        stripped = line.strip()
        if stripped.startswith("[") and stripped.endswith("]"):
            section = stripped[1:-1]
            output.append(line)
            continue
        if section == "libraries" and "=" in line and not stripped.startswith(";"):
            key, value = (part.strip() for part in line.split("=", 1))
            if key not in MANIFEST_ENTRIES:
                continue
            if value.strip('"') != MANIFEST_ENTRIES[key]:
                fail(f"unexpected arm64 library path for {key}")
            seen[key] = seen.get(key, 0) + 1
        output.append(line)
    if seen != {key: 1 for key in MANIFEST_ENTRIES}:
        fail("the committed GDExtension manifest must contain each arm64 library entry exactly once")
    return "".join(output)


def arm64_launcher_preset_text(source: str) -> str:
    output = []
    section = ""
    seen: set[str] = set()
    for line in source.splitlines(keepends=True):
        stripped = line.strip()
        if stripped.startswith("[") and stripped.endswith("]"):
            section = stripped[1:-1]
            output.append(line)
            continue
        if section == "preset.2" and stripped == 'name="macOS Universal"':
            output.append(line.replace("macOS Universal", LAUNCHER_PRESET))
            seen.add("name")
        elif section == "preset.2.options":
            if stripped == "binary_format/embed_pck=true":
                output.append(line.replace("=true", "=false"))
                seen.add("embed")
            elif stripped == 'binary_format/architecture="universal"':
                output.append(line.replace('"universal"', '"arm64"'))
                seen.add("architecture")
            elif stripped.startswith("application/min_macos_version_x86_64="):
                seen.add("minimum_x86")
            elif stripped.startswith("application/min_macos_version_arm64="):
                output.append(line)
                seen.add("minimum_arm64")
            else:
                output.append(line)
        else:
            output.append(line)
    if seen != {"name", "embed", "architecture", "minimum_x86", "minimum_arm64"}:
        fail("the staged launcher must have the expected universal macOS preset options")
    result = "".join(output)
    if "binary_format/architecture=\"universal\"" in result or "min_macos_version_x86_64" in result:
        fail("the staged launcher preset still contains universal macOS options")
    return result


def write_launcher_build_info(
    project: Path, commit: str, epoch: int, version: str = VERSION, *, macos_updates: bool = False, test_key: str | None = None,
    repo: str = "Open-BFME/openbfme-godot", keys_file: Path | None = None,
) -> dict:
    """`test_key` (MACPLAY-1, tests only): a test package that trusts that key, accepts the test options and says LOCAL TRIAL
    (make_macos_manifest.py --verify-package refuses such a launcher, so a release never carries one).
    Lane SIGNKMS-1: release_keys, the trusted key list, from `keys_file` (default: the staged launcher's release_keys.pub; with
    `test_key` and no `keys_file`: that key alone); its first key must be release_key (the legacy key: release_key.pub or `test_key`). Lane RELGH-2: nightly_keys, from the staged
    launcher's nightly_keys.pub (none without one)."""
    key = test_key if test_key is not None else (project / "release_key.pub").read_text(encoding="ascii").strip()
    if not re.fullmatch(r"[0-9a-f]{64}", key):
        fail("the committed launcher/release_key.pub is not a 32-byte Ed25519 public key")
    if keys_file is None and test_key is not None:
        keys = [key]
    else:
        list_file = keys_file if keys_file is not None else project / "release_keys.pub"
        try:
            keys = [k.hex() for _, k in release_keys.load(list_file, legacy=False)]
        except release_keys.KeyListError as e:
            fail(f"the release key list is not valid: {e}")
        if keys[0] != key:
            fail(f"the first key of {list_file.name} is not the launcher's release_key")
    # lane RELGH-2: the nightly-only keys of the staged launcher's nightly_keys.pub (none for a source without it); none may be a release key
    nightly_file = project / "nightly_keys.pub"
    nightly: list[str] = []
    if nightly_file.is_file():
        try:
            nightly = [k.hex() for _, k in release_keys.load_nightly(nightly_file, None)]
        except release_keys.KeyListError as e:
            fail(f"the nightly key list is not valid: {e}")
        if set(nightly) & set(keys):
            fail(f"{nightly_file.name} lists a release key")
    info = {
        "version": version.split(" ", 1)[0] if macos_updates else version,
        "display_version": version,
        "commit": commit,
        "date": datetime.fromtimestamp(epoch, timezone.utc).strftime("%Y-%m-%d"),
        "repo": repo,
        "release_key": key,
        "release_keys": keys,
        "nightly_keys": nightly,
        "test_build": test_key is not None,
        "local_trial": test_key is not None,
        "macos_preview": not macos_updates,
        "macos_updates": macos_updates,
    }
    (project / "build_info.json").write_text(json.dumps(info, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return info


def sanitize_template_slice(
    data: bytes,
    *,
    expected_sha256: str,
    expected_count: int = 33,
    prefix_sha256: str = TEMPLATE_PATH_PREFIX_SHA256,
) -> bytes:
    if hashlib.sha256(data).hexdigest() != expected_sha256:
        raise ValueError("arm64 template slice hash does not match the pinned upstream slice")
    if len(TEMPLATE_PATH_REPLACEMENT) != 67:
        raise ValueError("template path replacement is not the expected fixed length")
    if len(data) < 32 or data[:4] != b"\xcf\xfa\xed\xfe":
        raise ValueError("template is not a thin little-endian 64-bit Mach-O")
    cpu, = struct.unpack_from("<I", data, 4)
    if cpu != 0x0100000C:
        raise ValueError("template Mach-O slice is not arm64")
    ncmds, sizeofcmds = struct.unpack_from("<II", data, 16)
    command_limit = 32 + sizeofcmds
    if sizeofcmds % 8 or command_limit > len(data):
        raise ValueError("template Mach-O load commands exceed slice bounds")
    cstring_ranges: list[tuple[int, int]] = []
    pos = 32
    for _ in range(ncmds):
        if pos + 8 > command_limit:
            raise ValueError("template Mach-O load command is truncated")
        cmd, cmdsize = struct.unpack_from("<II", data, pos)
        if cmdsize < 8 or cmdsize % 8 or pos + cmdsize > command_limit:
            raise ValueError("template Mach-O load command bounds are invalid")
        if cmd == 0x19:
            if cmdsize < 72:
                raise ValueError("template Mach-O segment command is truncated")
            segment = data[pos + 8:pos + 24].split(b"\0", 1)[0]
            nsects, = struct.unpack_from("<I", data, pos + 64)
            if 72 + nsects * 80 > cmdsize:
                raise ValueError("template Mach-O section table exceeds its segment command")
            for index in range(nsects):
                section = pos + 72 + index * 80
                sectname = data[section:section + 16].split(b"\0", 1)[0]
                segname = data[section + 16:section + 32].split(b"\0", 1)[0]
                if segment == b"__TEXT" and segname == b"__TEXT" and sectname == b"__cstring":
                    size, = struct.unpack_from("<Q", data, section + 40)
                    offset, = struct.unpack_from("<I", data, section + 48)
                    if offset + size > len(data):
                        raise ValueError("template __TEXT,__cstring section exceeds the slice")
                    cstring_ranges.append((offset, offset + size))
        pos += cmdsize
    if pos != command_limit:
        raise ValueError("template Mach-O load command sizes do not sum to sizeofcmds")
    if not cstring_ranges:
        raise ValueError("template Mach-O has no __TEXT,__cstring section")

    matches = list(TEMPLATE_PATH_PATTERN.finditer(data))
    if len(matches) != expected_count:
        raise ValueError(f"expected {expected_count} pinned template paths, found {len(matches)}")
    for match in matches:
        prefix = match.group()
        if len(prefix) != 67 or hashlib.sha256(prefix).hexdigest() != prefix_sha256:
            raise ValueError("template source-path prefix does not match the pinned upstream literal")
        if not any(start <= match.start() and match.end() <= end for start, end in cstring_ranges):
            raise ValueError("template source-path literal is outside __TEXT,__cstring")

    result = bytearray(data)
    for match in matches:
        result[match.start():match.end()] = TEMPLATE_PATH_REPLACEMENT
    if len(result) != len(data):
        raise ValueError("template path sanitization changed the slice length")
    return bytes(result)


def install_templates(archive_path: Path, destination: Path, *, sanitize_paths: bool = False) -> None:
    hasher = hashlib.sha256()
    with archive_path.open("rb") as source:
        for chunk in iter(lambda: source.read(1 << 20), b""):
            hasher.update(chunk)
    digest = hasher.hexdigest()
    if digest != TEMPLATE_SHA256:
        fail(f"Godot export template SHA-256 is {digest}, not the official 4.7.2 checksum")
    try:
        with zipfile.ZipFile(archive_path) as archive:
            version = archive.read("templates/version.txt")
            macos = archive.read("templates/macos.zip")
    except (OSError, zipfile.BadZipFile, KeyError) as exc:
        fail(f"cannot read the official Godot export template archive: {exc}")
    if version.decode("ascii", "replace").strip() != TEMPLATE_VERSION or not macos.startswith(b"PK\x03\x04"):
        fail("the template archive has an unexpected version or macOS template")
    destination.mkdir(parents=True)
    (destination / "version.txt").write_bytes(version)
    source = destination / "macos-original.zip"
    source.write_bytes(macos)
    rewritten = destination / "macos.zip"
    work = destination / "thin"
    work.mkdir()
    seen = set()
    with zipfile.ZipFile(source) as original, zipfile.ZipFile(rewritten, "w") as arm64:
        arm64.comment = original.comment
        for info in original.infolist():
            name = info.filename
            data = original.read(info)
            if name.endswith(("godot_macos_debug.universal", "godot_macos_release.universal")):
                thin_name = name.removesuffix(".universal") + ".arm64"
                input_path = work / Path(name).name
                output_path = work / Path(thin_name).name
                input_path.write_bytes(data)
                run(["lipo", str(input_path), "-thin", "arm64", "-output", str(output_path)])
                verify_architecture(output_path)
                data = output_path.read_bytes()
                if sanitize_paths:
                    expected = TEMPLATE_ARM64_SHA256.get(Path(thin_name).name)
                    if expected is None:
                        fail(f"no pinned source-path policy for template slice {thin_name}")
                    try:
                        data = sanitize_template_slice(data, expected_sha256=expected)
                    except ValueError as exc:
                        fail(f"cannot sanitize pinned Godot template {thin_name}: {exc}")
                name = thin_name
                seen.add(name)
            rewritten_info = zipfile.ZipInfo(name, info.date_time)
            rewritten_info.compress_type = info.compress_type
            rewritten_info.create_system = info.create_system
            rewritten_info.external_attr = info.external_attr
            rewritten_info.internal_attr = info.internal_attr
            rewritten_info.create_version = info.create_version
            rewritten_info.extract_version = info.extract_version
            rewritten_info.flag_bits = info.flag_bits
            rewritten_info.volume = info.volume
            rewritten_info.extra = info.extra
            rewritten_info.comment = info.comment
            arm64.writestr(rewritten_info, data)
    if seen != {
        "macos_template.app/Contents/MacOS/godot_macos_debug.arm64",
        "macos_template.app/Contents/MacOS/godot_macos_release.arm64",
    }:
        fail("the official macOS template did not contain both universal debug and release binaries")
    source.unlink()
    shutil.rmtree(work)


def verify_build_cache() -> None:
    cache = REPO / "workspace/mac-preview-build/CMakeCache.txt"
    if not cache.is_file():
        fail("workspace/mac-preview-build is missing; configure the required native build first")
    values = {}
    for line in cache.read_text(encoding="utf-8").splitlines():
        if line and not line.startswith(("//", "#")) and "=" in line:
            key, value = line.split("=", 1)
            values[key.split(":", 1)[0]] = value
    wanted = {
        "CMAKE_BUILD_TYPE": "Release",
        "CMAKE_OSX_ARCHITECTURES": "arm64",
        "CMAKE_OSX_DEPLOYMENT_TARGET": "13.0",
        "OPENBFME_LAME": "OFF",
        "OPENBFME_VERSION_OVERRIDE": VERSION,
    }
    wrong = [f"{key}={values.get(key)!r} (wanted {value!r})" for key, value in wanted.items() if values.get(key) != value]
    if wrong:
        fail("workspace/mac-preview-build does not match the requested configuration: " + "; ".join(wrong))


def verify_provenance(library: Path, engine: Path, commit: str, version: str = VERSION) -> None:
    records = lib_provenance.records(library.read_bytes())
    if len(records) != 1 or "malformed" in records[0]:
        fail(f"{ARM64_LIBRARY} must contain exactly one valid build record")
    record = records[0]
    engine_id = lib_provenance.engine_id_of(engine, record["x86-32"], record["id-options"])
    problems = lib_provenance.check(library.read_bytes(), version, commit, engine_id)
    if problems:
        fail(f"{ARM64_LIBRARY} provenance failed: {'; '.join(problems)}")
    print(f"PACKAGE provenance ok ({commit[:10]}, engine id {engine_id[:16]}...)")


def verify_architecture(binary: Path) -> None:
    result = run(["lipo", "-archs", str(binary)])
    if result.stdout.split() != ["arm64"]:
        fail(f"{binary.name} contains architectures {result.stdout.strip()!r}, not exactly arm64")


def version_tuple(value: str) -> tuple[int, ...]:
    return tuple(int(part) for part in value.split("."))


def verify_minimum_macos(binary: Path) -> None:
    output = run(["otool", "-l", str(binary)]).stdout
    commands = re.split(r"(?m)^[ \t]*Load command \d+[ \t]*$", output)
    build_versions = [
        command for command in commands
        if re.search(r"(?m)^[ \t]*cmd LC_BUILD_VERSION[ \t]*$", command)
    ]
    if not build_versions:
        fail(f"{binary.name} does not declare LC_BUILD_VERSION metadata")
    for command in build_versions:
        platform = re.search(r"(?m)^[ \t]*platform[ \t]+(\d+)[ \t]*$", command)
        minimum = re.search(r"(?m)^[ \t]*minos[ \t]+([0-9.]+)[ \t]*$", command)
        if platform is None or minimum is None:
            fail(f"{binary.name} has incomplete LC_BUILD_VERSION metadata")
        if platform.group(1) != "1":
            fail(f"{binary.name} declares a non-macOS build platform")
        if version_tuple(minimum.group(1)) > (13, 0):
            fail(f"{binary.name} requires macOS {minimum.group(1)}, later than 13.0")


def verify_app_minimum(app: Path) -> None:
    plist = plistlib.loads((app / "Contents/Info.plist").read_bytes())
    by_architecture = plist.get("LSMinimumSystemVersionByArchitecture")
    minimum = by_architecture.get("arm64") if isinstance(by_architecture, dict) else None
    if minimum is None:
        minimum = plist.get("LSMinimumSystemVersion") or plist.get("MinimumOSVersion")
    if not minimum or version_tuple(str(minimum)) != (13, 0):
        fail(f"{app.name} does not declare the required macOS 13.0 minimum")


def verify_adhoc_signature(app: Path) -> None:
    run(["codesign", "--verify", "--deep", "--strict", str(app)])
    details = run(["codesign", "-dv", "--verbose=4", str(app)]).stdout
    if "Signature=adhoc" not in details or "Authority=" in details:
        fail(f"{app.name} is not signed only with an ad-hoc identity")


def expand_runtime_path(value: str, binary: Path, executable_dir: Path, rpaths: list[str], app: Path) -> list[Path]:
    if value.startswith("@loader_path/"):
        return [binary.parent / value.removeprefix("@loader_path/")]
    if value.startswith("@executable_path/"):
        return [executable_dir / value.removeprefix("@executable_path/")]
    if value.startswith("@rpath/"):
        suffix = value.removeprefix("@rpath/")
        bases = [app / "Contents/Frameworks", app / "Contents/MacOS"]
        for rpath in rpaths:
            base = rpath.replace("@loader_path", str(binary.parent)).replace(
                "@executable_path", str(executable_dir),
            )
            if not base.startswith("@"):
                bases.append(Path(base))
        return [base / suffix for base in bases]
    return [Path(value)]


def verify_dependencies(binary: Path, app: Path) -> None:
    load_commands = run(["otool", "-l", str(binary)]).stdout
    rpaths = re.findall(r"^\s*path (.+?) \(offset \d+\)$", load_commands, re.M)
    dependencies = run(["otool", "-L", str(binary)]).stdout.splitlines()[1:]
    executable_dir = app / "Contents/MacOS"
    for line in dependencies:
        value = line.strip().split(" (compatibility version ", 1)[0]
        if value.startswith(("/System/Library/", "/usr/lib/")):
            continue
        candidates = expand_runtime_path(value, binary, executable_dir, rpaths, app)
        if not any(candidate.is_file() for candidate in candidates):
            fail(f"{binary.name} has an unresolvable dylib dependency: {value}")


def verify_pack_manifest(pack: Path) -> None:
    try:
        entries = audit_package.pck_entries(pack.read_bytes())
    except (ValueError, OSError, struct.error) as exc:
        fail(f"cannot inspect the exported Godot pack: {exc}")
    manifests = [data for name, data in entries if name == "openbfme.gdextension"]
    if len(manifests) != 1:
        fail("the exported pack must contain exactly one openbfme.gdextension manifest")
    text = manifests[0].decode("utf-8", "strict")
    if "universal" in text.lower() or "x86_64" in text.lower():
        fail("the exported GDExtension manifest still names a universal or Intel library")
    for path in MANIFEST_ENTRIES.values():
        if path not in text:
            fail(f"the exported manifest does not resolve {path}")


def verify_launcher_pack(pack: Path, expected_info: dict) -> None:
    try:
        entries = audit_package.pck_entries(pack.read_bytes())
    except (ValueError, OSError, struct.error) as exc:
        fail(f"cannot inspect the exported launcher pack: {exc}")
    matches = [data for name, data in entries if name == "build_info.json"]
    if len(matches) != 1 or matches[0] != json.dumps(expected_info, indent=2, sort_keys=True).encode() + b"\n":
        fail("the launcher pack does not contain the required build_info.json")


def import_project(godot_cmd: list[str], project: Path, env, description: str) -> None:
    args = godot_cmd + ["--headless", "--path", str(project), "--editor", "--import", "--quit"]
    imported = run(args, env=env, allow_failure=True)
    if imported.returncode:
        imported = run(args, env=env, allow_failure=True)
    if imported.returncode:
        fail(f"fresh arm64 {description} import failed:\n{imported.stdout[-5000:]}")
    print(f"PACKAGE fresh arm64 {description} import ok")


def audit(
    path: Path,
    *,
    release: bool,
    platform: str = PLATFORM,
    forbidden_prefixes: tuple[str, ...] = (),
) -> None:
    args = [sys.executable, str(HERE / "audit_package.py")]
    for prefix in forbidden_prefixes:
        args += ["--forbid-prefix", prefix]
    args += ["--release", platform] if release else []
    args.append(str(path))
    result = run(args, allow_failure=True)
    if result.returncode:
        fail(result.stdout[-5000:])
    print(result.stdout.strip())


def write_inventory(archive: Path, destination: Path, digest: str, tool_versions: tuple[str, ...] = ()) -> None:
    with zipfile.ZipFile(archive) as package:
        infos = package.infolist()
    total_size = sum(info.file_size for info in infos)
    lines = [
        f"Archive: {archive.name}",
        f"SHA-256: {digest}",
        f"Archive bytes: {archive.stat().st_size}",
        f"Uncompressed bytes: {total_size}",
        *tool_versions,
        "",
        "Uncompressed bytes  Compressed bytes  Path",
    ]
    lines.extend(f"{info.file_size:17d}  {info.compress_size:16d}  {info.filename}" for info in infos)
    destination.write_text("\n".join(lines) + "\n", encoding="utf-8")


def render_readme(source: str, version: str, stem: str, *, macos_updates: bool = False) -> str:
    readme = source.replace(STEM, stem)
    readme = readme.replace("OpenBFME " + VERSION, "OpenBFME " + version)
    readme = readme.replace("v0.4.6", version.split(" Mac Unstable Preview", 1)[0])
    if macos_updates:
        readme = readme.replace(
            "This preview contains the game and the OpenBFME Launcher. It has no\n"
            "automatic updates and includes no retail game files. Keep both apps together.",
            "This package contains the game and the OpenBFME Launcher. It includes no\n"
            "retail game files. Keep both apps together.",
        )
        start = readme.index("MAC PREVIEW UPDATES")
        end = readme.index("\nKNOWN ISSUES", start)
        readme = readme[:start] + """SIGNED MAC UPDATES
The launcher checks a separately signed macOS package manifest. When a
verified update is available, restart the launcher to update both apps as
one package. The exchange is deferred while the bundled game is running.
If the signed release has no Mac package, use the releases page to download
a complete package. Invalid or incomplete signatures are errors, never a
manual-update fallback. The package carries a signed inventory of its files
(payload.json): Play starts OpenBFME.app only when every file matches it.
If OpenBFME.app is not beside the launcher, the launcher downloads the
signed Mac package into its own data folder and plays that copy.
Launching OpenBFME.app directly does not perform
the launcher update handover.

IF PLAY SAYS "APP TRANSLOCATION"
macOS runs a quarantined app that was not moved since it was unpacked
from a temporary read-only copy, without OpenBFME.app beside it. Quit the
launcher, move the folder that holds both apps (the unpacked
openbfme-...-macos-arm64 folder) out of Downloads, for example into your
home folder, and open the launcher in it again. Keep the two apps in that
folder: the launcher updates them there. If it still happens, remove the
quarantine from that folder:
  xattr -dr com.apple.quarantine "/path/to/the folder of the apps"
""" + readme[end:]
        readme = readme.replace("preview launcher (keep beside OpenBFME.app)", "launcher (keep beside OpenBFME.app)")
    return readme


def package(
    godot_arg: str,
    templates_arg: str,
    out_arg: str,
    source_branch: str = BRANCH,
    *,
    version: str = VERSION,
    stem: str = STEM,
    platform: str = PLATFORM,
    library_path: Path | None = None,
    commit_epoch: tuple[str, int] | None = None,
    allow_external_out: bool = False,
    forbidden_prefixes: tuple[str, ...] = (),
    allowed_output_entries: tuple[Path, ...] = (),
    tool_versions: tuple[str, ...] = (),
    base_env: dict[str, str] | None = None,
    sanitize_template_paths: bool = False,
    macos_updates: bool = False,
    macos_updates_helper: Path | None = None,
    test_key: str | None = None,
    test_repo: str | None = None,
    keys_file: Path | None = None,
) -> None:
    if sys.platform != "darwin":
        fail("the Apple Silicon preview packager must run on macOS")
    commit, epoch = commit_epoch or clean_commit(source_branch)
    if library_path is None:
        verify_build_cache()
    godot = repo_path(godot_arg)
    templates = repo_path(templates_arg)
    out = repo_path(out_arg)
    if not allow_external_out:
        try:
            out.relative_to((REPO / "workspace").resolve())
        except ValueError:
            fail("--out must be inside the ignored workspace directory")
    if not godot.is_file() or not templates.is_file():
        fail("--godot and --templates must name existing files")
    allowed = {entry.resolve() for entry in allowed_output_entries}
    if out.exists() and (not out.is_dir() or any(child.resolve() not in allowed for child in out.iterdir())):
        fail(f"the output directory is not empty: {out}")
    godot_version = run([str(godot), "--version"], env=base_env)
    if not godot_version.stdout.startswith("4.7.2."):
        fail(f"Godot 4.7.2 is required, found {godot_version.stdout.strip()}")
    out.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory(prefix=".work.", dir=out) as temp:
        work = Path(temp)
        godot_home = work / "godot-home"
        template_dir = godot_home / "Library/Application Support/Godot/export_templates" / TEMPLATE_VERSION
        install_templates(templates, template_dir, sanitize_paths=sanitize_template_paths)
        env = dict(base_env if base_env is not None else os.environ, HOME=str(godot_home))

        engine_root = work / "source"
        extract_git_archive(["engine"], engine_root)
        library = library_path or (REPO / "godot/bin" / ARM64_LIBRARY)
        if not library.is_file():
            fail(f"native extension not found: {library}")
        verify_architecture(library)
        verify_provenance(library, engine_root / "engine", commit, version)

        stage = work / "stage"
        extract_git_archive(["godot", "launcher", "LICENSE", "NOTICE", "tools/release/README-MAC.txt"], stage)
        project = stage / "godot"
        # lane CRASHACT-1: the crash service key (CRASH_CLIENT_KEY) into both staged projects, or nothing (no crash report is sent)
        for staged in (project, stage / "launcher"):
            print(crash_key.write(staged))
        manifest = project / "openbfme.gdextension"
        manifest.write_text(arm64_manifest_text(manifest.read_text(encoding="utf-8")), encoding="utf-8")
        trust = (project / "scripts/release/release_trust.gd").read_text(encoding="utf-8")
        if trust.count('const RELEASE_KEY := ""') != 1 or trust.count('const RELEASE_KEYS := ""') != 1:
            fail("release_trust must remain empty in the macOS preview")
        (project / "bin").mkdir(parents=True, exist_ok=True)
        shutil.copyfile(library, project / "bin" / ARM64_LIBRARY)
        audit(project, release=False, platform=platform, forbidden_prefixes=forbidden_prefixes)

        launcher_project = stage / "launcher"
        launcher_presets = launcher_project / "export_presets.cfg"
        launcher_presets.write_text(
            arm64_launcher_preset_text(launcher_presets.read_text(encoding="utf-8")), encoding="utf-8",
        )
        launcher_info = write_launcher_build_info(launcher_project, commit, epoch, version, macos_updates=macos_updates,
                                                  test_key=test_key, repo=test_repo or "Open-BFME/openbfme-godot",
                                                  keys_file=keys_file)
        audit(launcher_project, release=False, platform=platform, forbidden_prefixes=forbidden_prefixes)

        godot_cmd = ["arch", "-arm64", str(godot)]
        import_project(godot_cmd, project, env, "game")
        import_project(godot_cmd, launcher_project, env, "launcher")

        export_dir = work / "export"
        export_dir.mkdir()
        app = export_dir / "OpenBFME.app"
        exported = run(godot_cmd + ["--headless", "--path", str(project), "--export-debug", PRESET, str(app)],
                       env=env, allow_failure=True)
        if exported.returncode:
            fail(f"Godot export failed:\n{exported.stdout[-5000:]}")
        executable = app / "Contents/MacOS/OpenBFME"
        extension = app / "Contents/Frameworks" / ARM64_LIBRARY
        if not executable.is_file() or not extension.is_file():
            fail("the Apple Silicon export is missing its executable or arm64 GDExtension")
        for binary in (executable, extension):
            verify_architecture(binary)
            verify_minimum_macos(binary)
            verify_dependencies(binary, app)
        verify_pack_manifest(app / "Contents/Resources/OpenBFME.pck")
        verify_adhoc_signature(app)
        print("PACKAGE arm64 architecture, macOS minimum, dylib dependencies and ad-hoc signature ok")

        verify_app_minimum(app)

        launcher_export_dir = work / "launcher-export"
        launcher_export_dir.mkdir()
        launcher_app = launcher_export_dir / LAUNCHER_APP
        launcher_exported = run(
            godot_cmd + ["--headless", "--path", str(launcher_project), "--export-release", LAUNCHER_PRESET, str(launcher_app)],
            env=env, allow_failure=True,
        )
        if launcher_exported.returncode:
            fail(f"Godot launcher export failed:\n{launcher_exported.stdout[-5000:]}")
        launcher_binary = launcher_app / "Contents/MacOS" / LAUNCHER_EXECUTABLE
        launcher_pack = launcher_app / "Contents/Resources" / LAUNCHER_PACK
        if not launcher_binary.is_file() or not launcher_pack.is_file():
            fail(f"the launcher export is missing {LAUNCHER_EXECUTABLE} or {LAUNCHER_PACK}")
        if macos_updates:
            if macos_updates_helper is None or not macos_updates_helper.is_file():
                fail("the generic Mac updater helper was not built")
            helper = launcher_app / "Contents/MacOS/openbfme-bundle-swap"
            shutil.copyfile(macos_updates_helper, helper)
            helper.chmod(0o755)
            run(["codesign", "--force", "--sign", "-", str(helper)], env=env)
            run(["codesign", "--force", "--deep", "--sign", "-", str(launcher_app)], env=env)
        if audit_package.embedded_pck_base(launcher_binary.read_bytes()) is not None:
            fail("the launcher PCK must be a separate resource, not embedded in its executable")
        verify_architecture(launcher_binary)
        verify_minimum_macos(launcher_binary)
        verify_dependencies(launcher_binary, launcher_app)
        verify_launcher_pack(launcher_pack, launcher_info)
        if macos_updates:
            verify_architecture(launcher_app / "Contents/MacOS/openbfme-bundle-swap")
        verify_adhoc_signature(launcher_app)
        verify_app_minimum(launcher_app)
        print("PACKAGE arm64 launcher architecture, separate PCK, metadata, dependencies and ad-hoc signature ok")

        package_dir = work / stem
        package_dir.mkdir()
        shutil.copytree(app, package_dir / "OpenBFME.app")
        shutil.copytree(launcher_app, package_dir / LAUNCHER_APP)
        readme = render_readme(
            (stage / "tools/release/README-MAC.txt").read_text(encoding="utf-8"),
            version, stem, macos_updates=macos_updates,
        )
        (package_dir / "README-MAC.txt").write_text(readme, encoding="utf-8")
        shutil.copyfile(stage / "LICENSE", package_dir / "LICENSE")
        shutil.copyfile(stage / "NOTICE", package_dir / "NOTICE")
        committed_date = datetime.fromtimestamp(epoch, timezone.utc).strftime("%Y-%m-%d")
        (package_dir / "VERSION").write_text(
            f"OpenBFME {version}\ncommit {commit}\ncommitted {committed_date}\n", encoding="utf-8",
        )
        audit(package_dir, release=True, platform=platform, forbidden_prefixes=forbidden_prefixes)

        archive_path = out / f"{stem}.zip"
        repeat_path = work / "repeat.zip"
        make_archive.zip_file(package_dir, archive_path, epoch)
        make_archive.zip_file(package_dir, repeat_path, epoch)
        if archive_path.read_bytes() != repeat_path.read_bytes():
            fail("the Mac ZIP is not byte-for-byte reproducible")
        audit(archive_path, release=True, platform=platform, forbidden_prefixes=forbidden_prefixes)
        readme_bytes = (package_dir / "README-MAC.txt").read_bytes()
        readme_sidecar = out / "README-MAC.txt"
        readme_sidecar.write_bytes(readme_bytes)
        if readme_sidecar.read_bytes() != readme_bytes:
            fail("the sidecar README is not byte-identical")
        digest = hashlib.sha256(archive_path.read_bytes()).hexdigest()
        (out / f"{stem}.zip.sha256").write_text(f"{digest}  {archive_path.name}\n", encoding="ascii")
        inventory = out / f"{stem}.inventory.txt"
        write_inventory(archive_path, inventory, digest, tool_versions)

    print(f"PACKAGE version {version} ({commit})")
    print(f"PACKAGE ZIP {archive_path} ({archive_path.stat().st_size} bytes)")
    print(f"PACKAGE SHA-256 {digest}")
    print(f"PACKAGE README {readme_sidecar}")
    print(f"PACKAGE inventory {inventory}")


def main() -> int:
    import argparse

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--godot", required=True, help="Godot 4.7.2 editor executable")
    parser.add_argument("--templates", required=True, help="official Godot 4.7.2 templates .tpz")
    parser.add_argument("--out", required=True, help="empty output directory inside workspace/")
    parser.add_argument("--source-branch", default=BRANCH, help="required checked-out branch (default: %(default)s)")
    parser.add_argument("--keys-file", type=Path, default=None,
                        help="SIGNKMS-1: the trusted release key list the launcher is built with (default: the committed "
                             "launcher/release_keys.pub of the packaged commit); its first key must be launcher/release_key.pub's")
    args = parser.parse_args()
    package(args.godot, args.templates, args.out, args.source_branch, keys_file=args.keys_file)
    return 0


if __name__ == "__main__":
    sys.exit(main())
