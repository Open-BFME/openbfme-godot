#!/usr/bin/env python3
"""Build a reproducible, ad-hoc-signed Apple Silicon game-and-launcher preview from any clean commit."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
sys.path.insert(0, str(HERE))

import package_macos_preview as preview  # noqa: E402

SAFE_VERSION = re.compile(r"^[A-Za-z0-9][A-Za-z0-9.+_-]{0,126}$")
PLATFORM = "macos-arm64"
DISPLAY_SUFFIX = "Mac Unstable Preview (Apple Silicon)"


def fail(message: str) -> None:
    raise SystemExit(f"MACOS RELEASE FAIL: {message}")


def git(repo: Path, *args: str) -> str:
    try:
        result = subprocess.run(
            ["git", "-C", str(repo), *args], check=False, capture_output=True, text=True,
        )
    except OSError as exc:
        fail(f"cannot run Git: {exc}")
    if result.returncode:
        fail(f"git {' '.join(args)} exited {result.returncode}: {result.stderr.strip()}")
    return result.stdout.strip()


def validate_version(version: str) -> str:
    if not SAFE_VERSION.fullmatch(version) or version in (".", ".."):
        fail(f"unsafe version identifier {version!r}; use letters, digits, dot, plus, underscore or hyphen")
    return version


def validate_submodule(repo: Path) -> str:
    relative = "engine/thirdparty/godot-cpp"
    listing = git(repo, "ls-tree", "HEAD", relative)
    match = re.fullmatch(r"160000 commit ([0-9a-f]{40})\t" + re.escape(relative), listing)
    if not match:
        fail(f"{relative} is not a pinned Git submodule in HEAD")
    expected = match.group(1)
    checkout = repo / relative
    if not checkout.is_dir() or not (checkout / ".git").exists():
        fail(f"{relative} is not initialized; run git submodule update --init --recursive")
    actual = git(repo, "-C", str(checkout), "rev-parse", "HEAD")
    if actual != expected:
        fail(f"{relative} is at {actual}, but HEAD pins {expected}; run git submodule update --init --recursive")
    status = git(repo, "-C", str(checkout), "status", "--porcelain=v1", "--untracked-files=all")
    if status:
        fail(f"{relative} is dirty; clean it and run git submodule update --init --recursive")
    return expected


def checkout_metadata(repo: Path, requested_version: str | None) -> tuple[str, int, str, bool]:
    status = git(repo, "status", "--porcelain=v1", "--untracked-files=all")
    if status:
        fail("the main worktree is dirty; commit changes before packaging")
    commit = git(repo, "rev-parse", "HEAD")
    if not re.fullmatch(r"[0-9a-f]{40}", commit):
        fail("HEAD is not a full Git commit")
    epoch_text = git(repo, "show", "-s", "--format=%ct", commit)
    try:
        epoch = int(epoch_text)
    except ValueError:
        fail(f"invalid commit timestamp {epoch_text!r}")
    shallow = git(repo, "rev-parse", "--is-shallow-repository") == "true"
    if requested_version is None:
        version = git(
            repo, "describe", "--tags", "--match", "v[0-9]*", "--exclude", "*-playtest.*",
            "--always", "--dirty", "--abbrev=10",
        )
    else:
        version = requested_version
    return commit, epoch, validate_version(version), shallow


def validate_preview_sources(repo: Path) -> None:
    preset = (repo / "launcher/export_presets.cfg").read_text(encoding="utf-8")
    updater = (repo / "launcher/scripts/core/updater.gd").read_text(encoding="utf-8")
    launcher = (repo / "launcher/scripts/main.gd").read_text(encoding="utf-8")
    self_update = (repo / "launcher/scripts/core/self_update.gd").read_text(encoding="utf-8")
    manifest = (repo / "launcher/scripts/core/manifest.gd").read_text(encoding="utf-8")
    macos_update = repo / "launcher/scripts/core/macos_update.gd"
    helper = repo / "tools/release/macos_bundle_swap.c"
    if 'name="macOS Universal"' not in preset or "macos_preview" not in updater or "macos_preview()" not in launcher:
        fail("HEAD lacks the required macOS preview export and launcher source")
    if ("macos_updates_enabled" not in updater or "macos_release_manifest" not in updater or
            "macos_update_on_start" not in updater or "updater.macos_updates_enabled()" not in self_update or
            'schema_mode == "macos"' not in manifest or "no_macos_entry" not in launcher or
            "_set_macos_releases_link" not in launcher or not macos_update.is_file() or not helper.is_file()):
        fail("HEAD lacks the signed macOS manifest, package updater, or atomic swap helper sources")
    trust = (repo / "godot/scripts/release/release_trust.gd").read_text(encoding="utf-8")
    if trust.count('const RELEASE_KEY := ""') != 1 or trust.count('const RELEASE_KEYS := ""') != 1 \
            or trust.count('const NIGHTLY_KEYS := ""') != 1:
        fail("release_trust must remain empty for the unsigned-payload Mac preview")


def hash_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def path_arg(value: str, repo: Path) -> Path:
    candidate = Path(value).expanduser()
    return candidate.resolve() if candidate.is_absolute() else (repo / candidate).resolve()


def ensure_empty_output(path: Path) -> None:
    if path.exists() and (not path.is_dir() or any(path.iterdir())):
        fail(f"--out must be an empty directory: {path}")
    if not path.parent.is_dir():
        fail(f"the parent of --out must already exist: {path.parent}")


def cmake_configure_args(build: Path, extension_dir: Path, version: str) -> list[str]:
    return [
        "cmake", "-S", str(REPO / "engine"), "-B", str(build), "-G", "Ninja",
        "-DCMAKE_BUILD_TYPE=Release", "-DOPENBFME_BUILD_TESTS=OFF", "-DOPENBFME_BUILD_GODOT=ON",
        "-DCMAKE_OSX_ARCHITECTURES=arm64", "-DCMAKE_OSX_DEPLOYMENT_TARGET=13.0",
        f"-DOPENBFME_VERSION_OVERRIDE={version}", f"-DOPENBFME_GODOT_BIN={extension_dir}",
    ]


def helper_compile_args(repo: Path, source: str, output: str) -> list[str]:
    """the swap helper's clang command, one list for the plan and the build (DEVPORT-6: the build's copy lacked the f of its
    -fmacro-prefix-map, so __FILE__ kept the checkout's path)"""
    return [
        "clang", "-arch", "arm64", "-mmacosx-version-min=13.0", "-O2",
        "-fdebug-compilation-dir=.", f"-ffile-prefix-map={repo}=.",
        f"-fmacro-prefix-map={repo}=.", source, "-o", output,
    ]


def run_checked(args: list[str], *, env: dict[str, str], cwd: Path = REPO) -> str:
    try:
        result = subprocess.run(args, cwd=cwd, env=env, check=False, capture_output=True, text=True)
    except OSError as exc:
        fail(f"cannot run {args[0]}: {exc}")
    if result.returncode:
        fail(f"{args[0]} exited {result.returncode}:\n{result.stdout[-6000:]}")
    return result.stdout


def tool_version(args: list[str]) -> str:
    try:
        result = subprocess.run(args, check=False, capture_output=True, text=True)
    except OSError:
        return "unavailable"
    if result.returncode:
        return "unavailable"
    return result.stdout.strip().splitlines()[0] if result.stdout.strip() else "unknown"


def make_plan(
    commit: str,
    epoch: int,
    version: str,
    templates: Path,
    godot: Path,
    out: Path,
    jobs: int,
) -> dict:
    display_version = f"{version} {DISPLAY_SUFFIX}"
    stem = f"openbfme-{version}-macos-arm64"
    build = "<fresh temporary build directory>/build"
    extension = "<fresh temporary build directory>/godot-bin"
    configure = [
        "cmake", "-S", str(REPO / "engine"), "-B", build, "-G", "Ninja",
        "-DCMAKE_BUILD_TYPE=Release", "-DOPENBFME_BUILD_TESTS=OFF", "-DOPENBFME_BUILD_GODOT=ON",
        "-DCMAKE_OSX_ARCHITECTURES=arm64", "-DCMAKE_OSX_DEPLOYMENT_TARGET=13.0",
        f"-DOPENBFME_VERSION_OVERRIDE={display_version}", f"-DOPENBFME_GODOT_BIN={extension}",
    ]
    return {
        "commit": commit,
        "commit_timestamp": epoch,
        "version": version,
        "display_version": display_version,
        "template_sha256": preview.TEMPLATE_SHA256,
        "templates": str(templates),
        "godot": str(godot),
        "output_directory": str(out),
        "source_date_epoch": epoch,
        "steps": [
            {"name": "configure", "argv": configure, "source_date_epoch": epoch},
            {
                "name": "build",
                "argv": ["cmake", "--build", build, "--target", "openbfme", "-j", str(jobs)],
                "source_date_epoch": epoch,
            },
            {
                "name": "compile-updater-helper",
                "argv": helper_compile_args(REPO, "tools/release/macos_bundle_swap.c",
                                            "<fresh temporary build directory>/openbfme-bundle-swap"),
                "source_date_epoch": epoch,
            },
            {
            "name": "export",
            "godot": str(godot),
            "architecture": "arm64",
            "presets": ["macOS Apple Silicon", "macOS Apple Silicon Preview"],
            "template_path_sanitizer": "fixed-length pinned __TEXT,__cstring source-path redaction",
            "source_date_epoch": epoch,
            },
            {"name": "sign", "method": "ad-hoc only; no Developer ID, notarization, or stapling"},
            {
                "name": "audit",
                "platform": PLATFORM,
                "checks": ["strict two-app allowlist", "architecture", "PCK resources", "local build paths"],
            },
            {"name": "archive", "path": str(out / f"{stem}.zip"), "sha256_sidecar": str(out / f"{stem}.zip.sha256")},
        ],
    }


def sanitized_environment(work: Path, epoch: int) -> dict[str, str]:
    env = dict(os.environ)
    for name in (
        "ROTWK_INSTALL", "BFME2_INSTALL", "RW_GAME_DAT", "GODOT", "OPENBFME_GAME_FILES",
        "CSC_LINK", "CSC_KEY_PASSWORD", "APPLE_ID", "APPLE_APP_SPECIFIC_PASSWORD",
        "APP_STORE_CONNECT_API_KEY", "APPLE_TEAM_ID", "CODESIGN_IDENTITY", "MACOS_SIGNING_IDENTITY",
    ):
        env.pop(name, None)
    home = work / "home"
    tmp = work / "tmp"
    home.mkdir()
    tmp.mkdir()
    env.update({"HOME": str(home), "TMPDIR": str(tmp), "SOURCE_DATE_EPOCH": str(epoch), "GIT_LFS_SKIP_SMUDGE": "1"})
    return env


def package(args: argparse.Namespace) -> int:
    repo = REPO.resolve()
    commit, epoch, version, shallow = checkout_metadata(repo, args.version)
    validate_submodule(repo)
    validate_preview_sources(repo)
    godot = path_arg(args.godot, repo)
    templates = path_arg(args.templates, repo)
    out = path_arg(args.out, repo)
    ensure_empty_output(out)
    if not godot.is_file() or not templates.is_file():
        fail("--godot and --templates must name existing files")
    template_hash = hash_file(templates)
    if template_hash != preview.TEMPLATE_SHA256:
        fail(f"Godot template SHA-256 is {template_hash}, not the official 4.7.2 checksum")
    if shallow and args.version is None:
        print("MACOS RELEASE note: shallow clone; pass --version explicitly for a stable package version", file=sys.stderr)
    if args.dry_run:
        plan = make_plan(commit, epoch, version, templates, godot, out, args.jobs)
        print(json.dumps(plan, indent=2, sort_keys=True))
        return 0
    if sys.platform != "darwin":
        fail("a real arm64 package build requires macOS; --dry-run works on other platforms")
    if not godot.is_file():
        fail(f"Godot editor not found: {godot}")
    out.mkdir(parents=True, exist_ok=True)
    display_version = f"{version} {DISPLAY_SUFFIX}"
    stem = f"openbfme-{version}-macos-arm64"
    with tempfile.TemporaryDirectory(prefix=".build.", dir=out) as temp:
        work = Path(temp)
        build = work / "build"
        extension_dir = work / "godot-bin"
        extension_dir.mkdir()
        env = sanitized_environment(work, epoch)
        run_checked(cmake_configure_args(build, extension_dir, display_version), env=env)
        run_checked(["cmake", "--build", str(build), "--target", "openbfme", "-j", str(args.jobs)], env=env)
        library = extension_dir / preview.ARM64_LIBRARY
        if not library.is_file():
            fail(f"fresh CMake build did not produce {library}")
        helper = work / "openbfme-bundle-swap"
        run_checked(helper_compile_args(repo, str(repo / "tools/release/macos_bundle_swap.c"), str(helper)), env=env)
        forbidden = tuple(str(path) for path in (repo, work, build, extension_dir, out))
        versions = (
            "Tool versions:",
            f"  Python: {sys.version.split()[0]}",
            f"  CMake: {tool_version(['cmake', '--version'])}",
            f"  Ninja: {tool_version(['ninja', '--version'])}",
            f"  Godot: {tool_version([str(godot), '--version'])}",
            f"  Xcode: {tool_version(['xcodebuild', '-version'])}",
            f"  Clang: {tool_version(['clang', '--version'])}",
            f"  macOS SDK: {tool_version(['xcrun', '--sdk', 'macosx', '--show-sdk-version'])}",
            "Template path transformation: 33 fixed-length prefixes in pinned arm64 __TEXT,__cstring sections",
            "Template release/debug arm64 SHA-256: " + ", ".join(
                f"{name}={digest}" for name, digest in sorted(preview.TEMPLATE_ARM64_SHA256.items())
            ),
            f"Template source-path prefix SHA-256: {preview.TEMPLATE_PATH_PREFIX_SHA256}",
        )
        preview.package(
            str(godot), str(templates), str(out), version=display_version, stem=stem, platform=PLATFORM,
            library_path=library, commit_epoch=(commit, epoch), allow_external_out=True,
            forbidden_prefixes=forbidden, allowed_output_entries=(work,), tool_versions=versions,
            base_env=env, sanitize_template_paths=True, macos_updates=True, macos_updates_helper=helper,
            test_key=args.test_key, test_repo=args.test_repo, keys_file=args.keys_file,
        )
    return 0


def positive_jobs(value: str) -> int:
    try:
        jobs = int(value)
    except ValueError:
        raise argparse.ArgumentTypeError("jobs must be a positive integer")
    if jobs <= 0:
        raise argparse.ArgumentTypeError("jobs must be a positive integer")
    return jobs


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", help="safe filename version (default: BuildVersion.cmake git-describe version)")
    parser.add_argument("--godot", required=True, help="Godot 4.7.2 editor executable")
    parser.add_argument("--templates", required=True, help="official Godot 4.7.2 templates .tpz")
    parser.add_argument("--out", required=True, help="empty output directory; may be outside the repository")
    parser.add_argument("--jobs", type=positive_jobs, default=6)
    parser.add_argument("--dry-run", action="store_true", help="print a deterministic plan without building or exporting")
    parser.add_argument("--test-key", help="MACPLAY-1, tests only: a 64-hex Ed25519 public key the launcher trusts instead of "
                        "launcher/release_key.pub; the launcher is a test build (test options on, LOCAL TRIAL), which the release refuses")
    parser.add_argument("--test-repo", help="tests only, with --test-key: the <owner>/<name> the test launcher updates from")
    parser.add_argument("--keys-file", type=Path, default=None,
                        help="SIGNKMS-1: the trusted release key list the launcher is built with (default: the committed "
                             "launcher/release_keys.pub of the packaged commit; with --test-key: that key alone); its first key "
                             "must be the launcher's release key (launcher/release_key.pub or --test-key)")
    args = parser.parse_args(argv)
    if args.test_key is not None and not re.fullmatch(r"[0-9a-f]{64}", args.test_key):
        fail("--test-key must be 64 lower-case hex digits")
    if args.test_repo is not None and (args.test_key is None or not re.fullmatch(r"[A-Za-z0-9-]{1,39}/[A-Za-z0-9._-]{1,100}", args.test_repo)):
        fail("--test-repo needs --test-key and an <owner>/<name>")
    return package(args)


if __name__ == "__main__":
    sys.exit(main())
