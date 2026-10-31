from __future__ import annotations

import json
import os
import selectors
import shutil
import hashlib
import struct
import subprocess
import sys
import zipfile
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import audit_package  # noqa: E402
import make_archive  # noqa: E402
import package_macos  # noqa: E402
import test_macos_preview as preview_tests  # noqa: E402

SUBMODULE_COMMIT = "12" * 20


def template_fixture(*, section_offset: int = 184, section_size: int | None = None) -> bytes:
    base = b"/Users/u/.cargo/registry/src/index.crates.io-"
    prefix = base + b"a" * (67 - len(base) - 1) + b"/"
    payload = prefix * 33 + b"unchanged"
    command_size = 72 + 80
    header = struct.pack("<IiiIIIII", 0xFEEDFACF, 0x0100000C, 0, 2, 1, command_size, 0, 0)
    segment = struct.pack("<II16sQQQQiiII", 0x19, command_size, b"__TEXT", 0, len(payload),
                          0, len(payload) + 184, 7, 5, 1, 0)
    section = struct.pack("<16s16sQQIIIIIIII", b"__cstring", b"__TEXT", 0x1000,
                          len(payload) if section_size is None else section_size,
                          section_offset, 0, 0, 0, 0, 0, 0, 0)
    return header + segment + section + payload


def git_repo(path: Path) -> Path:
    path.mkdir()
    subprocess.run(["git", "init", "-q", "--initial-branch=unrelated"], cwd=path, check=True)
    (path / "source.txt").write_text("committed\n")
    subprocess.run(["git", "add", "source.txt"], cwd=path, check=True)
    subprocess.run(
        ["git", "-c", "user.name=Packager Test", "-c", "user.email=packager@example.invalid",
         "commit", "-qm", "test source"],
        cwd=path, check=True,
    )
    return path


def generic_package(tmp_path: Path, version: str = "v1.2.3-preview.1") -> tuple[Path, dict[str, bytes]]:
    stem = f"openbfme-{version}-macos-arm64"
    files = preview_tests.preview_files()
    files["OpenBFME Launcher.app/Contents/MacOS/openbfme-bundle-swap"] = preview_tests.thin_macho(cpu=0x0100000C)
    files["VERSION"] = f"OpenBFME {version} Mac Unstable Preview (Apple Silicon)\ncommit {preview_tests.COMMIT}\n".encode()
    root = tmp_path / stem
    for name, data in files.items():
        target = root / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
        if name.endswith("/MacOS/OpenBFME") or name.endswith("/MacOS/OpenBFME Launcher") or name.endswith(".dylib"):
            target.chmod(0o755)
    return root, files


@pytest.mark.parametrize("version", ["v1.2.3-preview.1", "0123456789abcdef0123456789abcdef01234567", "release_1+arm64"])
def test_safe_filename_versions(version: str):
    assert package_macos.validate_version(version) == version


@pytest.mark.parametrize("version", ["", "../v1", "a/b", "..", "a\"b", "line\nbreak", "-leading"])
def test_unsafe_versions_are_rejected(version: str):
    with pytest.raises(SystemExit, match="unsafe version"):
        package_macos.validate_version(version)


def test_checkout_accepts_detached_unrelated_and_shallow_commits(monkeypatch, tmp_path: Path):
    repo = git_repo(tmp_path / "repo")
    subprocess.run(["git", "checkout", "-q", "--detach", "HEAD"], cwd=repo, check=True)
    detached = package_macos.checkout_metadata(repo, "v1.2.3-preview.1")
    assert detached[2:] == ("v1.2.3-preview.1", False)

    subprocess.run(["git", "switch", "-q", "-c", "feature/unrelated"], cwd=repo, check=True)
    assert package_macos.checkout_metadata(repo, "v1.2.3-preview.1")[2] == "v1.2.3-preview.1"

    real_git = package_macos.git

    def shallow_git(root: Path, *args: str) -> str:
        if args == ("rev-parse", "--is-shallow-repository"):
            return "true"
        return real_git(root, *args)

    monkeypatch.setattr(package_macos, "git", shallow_git)
    metadata = package_macos.checkout_metadata(repo, "v1.2.3-preview.1")
    assert metadata[3] is True


def test_dirty_main_worktree_is_rejected(tmp_path: Path):
    repo = git_repo(tmp_path / "repo")
    (repo / "source.txt").write_text("uncommitted\n")
    with pytest.raises(SystemExit, match="main worktree is dirty"):
        package_macos.checkout_metadata(repo, "v1.2.3-preview.1")


def test_mismatched_and_dirty_godot_cpp_submodules_are_rejected(monkeypatch, tmp_path: Path):
    repo = tmp_path / "repo"
    checkout = repo / "engine/thirdparty/godot-cpp"
    checkout.mkdir(parents=True)
    (checkout / ".git").write_text("gitdir: placeholder\n")
    relative = "engine/thirdparty/godot-cpp"
    listing = f"160000 commit {SUBMODULE_COMMIT}\t{relative}"

    def git_clean(_repo: Path, *args: str) -> str:
        if args == ("ls-tree", "HEAD", relative):
            return listing
        if args[-2:] == ("rev-parse", "HEAD"):
            return SUBMODULE_COMMIT
        return ""

    monkeypatch.setattr(package_macos, "git", git_clean)
    assert package_macos.validate_submodule(repo) == SUBMODULE_COMMIT

    def git_mismatch(_repo: Path, *args: str) -> str:
        if args == ("ls-tree", "HEAD", relative):
            return listing
        if args[-2:] == ("rev-parse", "HEAD"):
            return "f" * 40
        return ""

    monkeypatch.setattr(package_macos, "git", git_mismatch)
    with pytest.raises(SystemExit, match="but HEAD pins"):
        package_macos.validate_submodule(repo)

    def git_dirty(_repo: Path, *args: str) -> str:
        if args == ("ls-tree", "HEAD", relative):
            return listing
        if args[-2:] == ("rev-parse", "HEAD"):
            return SUBMODULE_COMMIT
        if args[-1:] == ("--untracked-files=all",):
            return " M changed.cpp"
        return ""

    monkeypatch.setattr(package_macos, "git", git_dirty)
    with pytest.raises(SystemExit, match="is dirty"):
        package_macos.validate_submodule(repo)


def test_pinned_template_source_paths_are_replaced_only_in_cstrings():
    source = template_fixture()
    base = b"/Users/u/.cargo/registry/src/index.crates.io-"
    prefix = base + b"a" * (67 - len(base) - 1) + b"/"
    replacement = package_macos.preview.TEMPLATE_PATH_REPLACEMENT
    assert len(prefix) == len(replacement) == 67
    sanitized = package_macos.preview.sanitize_template_slice(
        source, expected_sha256=hashlib.sha256(source).hexdigest(),
        prefix_sha256=hashlib.sha256(prefix).hexdigest(),
    )
    assert sanitized == source.replace(prefix, replacement)
    assert len(sanitized) == len(source)

    with pytest.raises(ValueError, match="slice hash"):
        package_macos.preview.sanitize_template_slice(source, expected_sha256="0" * 64)
    with pytest.raises(ValueError, match="outside __TEXT,__cstring"):
        outside = template_fixture(section_size=10)
        package_macos.preview.sanitize_template_slice(
            outside, expected_sha256=hashlib.sha256(outside).hexdigest(),
            prefix_sha256=hashlib.sha256(prefix).hexdigest(),
        )
    with pytest.raises(ValueError, match="expected 33"):
        missing = source.replace(prefix, b"x" * len(prefix), 1)
        package_macos.preview.sanitize_template_slice(
            missing, expected_sha256=hashlib.sha256(missing).hexdigest(),
            prefix_sha256=hashlib.sha256(prefix).hexdigest(),
        )


def test_dry_run_is_deterministic_and_does_not_create_output(monkeypatch, capsys, tmp_path: Path):
    repo = git_repo(tmp_path / "repo")
    godot = tmp_path / "Godot editor"
    templates = tmp_path / "official templates.tpz"
    godot.write_text("not executed")
    templates.write_bytes(b"not read by editor")
    output = tmp_path / "external package output"
    monkeypatch.setattr(package_macos, "REPO", repo)
    monkeypatch.setattr(package_macos, "validate_submodule", lambda _repo: SUBMODULE_COMMIT)
    monkeypatch.setattr(package_macos, "validate_preview_sources", lambda _repo: None)
    monkeypatch.setattr(package_macos, "hash_file", lambda _path: package_macos.preview.TEMPLATE_SHA256)
    monkeypatch.setattr(package_macos, "run_checked", lambda *_args, **_kwargs: pytest.fail("dry-run invoked a build"))
    monkeypatch.setattr(package_macos.preview, "package", lambda *_args, **_kwargs: pytest.fail("dry-run invoked Godot"))
    real_git = package_macos.git

    def shallow_git(root: Path, *args: str) -> str:
        if args == ("rev-parse", "--is-shallow-repository"):
            return "true"
        return real_git(root, *args)

    monkeypatch.setattr(package_macos, "git", shallow_git)
    argv = [
        "--godot", str(godot), "--templates", str(templates), "--out", str(output), "--jobs", "3", "--dry-run",
    ]
    assert package_macos.main(argv) == 0
    first_capture = capsys.readouterr()
    assert package_macos.main(argv) == 0
    second_capture = capsys.readouterr()
    assert first_capture.out == second_capture.out
    assert "shallow clone" in first_capture.err
    assert "shallow clone" in second_capture.err
    plan = json.loads(first_capture.out)
    assert plan["commit"] == package_macos.git(repo, "rev-parse", "HEAD")
    assert package_macos.validate_version(plan["version"]) == plan["version"]
    assert plan["template_sha256"] == package_macos.preview.TEMPLATE_SHA256
    assert all(step["source_date_epoch"] == plan["commit_timestamp"] for step in plan["steps"][:3])
    assert [step["name"] for step in plan["steps"]] == [
        "configure", "build", "compile-updater-helper", "export", "sign", "audit", "archive",
    ]
    assert not output.exists()


def test_generic_readme_uses_safe_archive_stem():
    version = "v0.4.7-preview.1 Mac Unstable Preview (Apple Silicon)"
    stem = "openbfme-v0.4.7-preview.1-macos-arm64"
    source = (HERE / "README-MAC.txt").read_text(encoding="utf-8")
    rendered = package_macos.preview.render_readme(source, version, stem)
    assert f"shasum -a 256 {stem}.zip" in rendered
    assert package_macos.preview.STEM not in rendered
    assert "-unstable-preview.zip" not in rendered
    assert f"OpenBFME {version}" in rendered
    generic = package_macos.preview.render_readme(source, version, stem, macos_updates=True)
    assert "SIGNED MAC UPDATES" in generic
    assert f"shasum -a 256 {stem}.zip" in generic
    assert "It has no\nautomatic updates" not in generic
    assert "the launcher update handover." in generic


def test_arm64_build_arguments_include_reproducibility_inputs(tmp_path: Path):
    build = tmp_path / "build path"
    extension = tmp_path / "extension path"
    args = package_macos.cmake_configure_args(build, extension, "v1.2.3-preview.1 Mac Unstable Preview (Apple Silicon)")
    assert "-DCMAKE_BUILD_TYPE=Release" in args
    assert "-DOPENBFME_BUILD_TESTS=OFF" in args
    assert "-DOPENBFME_BUILD_GODOT=ON" in args
    assert "-DCMAKE_OSX_ARCHITECTURES=arm64" in args
    assert "-DCMAKE_OSX_DEPLOYMENT_TARGET=13.0" in args
    assert "-DOPENBFME_VERSION_OVERRIDE=v1.2.3-preview.1 Mac Unstable Preview (Apple Silicon)" in args
    assert f"-DOPENBFME_GODOT_BIN={extension}" in args
    cmake = (HERE.parents[1] / "engine/CMakeLists.txt").read_text(encoding="utf-8")
    assert "-ffile-prefix-map=${OPENBFME_REPO_ROOT}/=" in cmake
    assert "-fdebug-prefix-map=${CMAKE_BINARY_DIR}=build" in cmake
    assert "CACHE PATH \"OpenBFME Godot extension output directory\"" in cmake


def test_swap_helper_maps_the_checkout_in_the_plan_and_the_build():
    """DEVPORT-6: the build's clang line passed the literal text "{repo}" to -fmacro-prefix-map (no f prefix), so __FILE__ in the
    helper kept the checkout's path; the plan and the build now share one argument list"""
    repo = Path("/src/open bfme")
    assert package_macos.helper_compile_args(repo, "tools/release/macos_bundle_swap.c", "out/swap") == [
        "clang", "-arch", "arm64", "-mmacosx-version-min=13.0", "-O2", "-fdebug-compilation-dir=.",
        "-ffile-prefix-map=/src/open bfme=.", "-fmacro-prefix-map=/src/open bfme=.",
        "tools/release/macos_bundle_swap.c", "-o", "out/swap",
    ]
    source = (HERE / "package_macos.py").read_text(encoding="utf-8")
    assert source.count("helper_compile_args(") == 3, "the definition, the plan and the build"


def test_build_environment_is_isolated_and_uses_commit_epoch(monkeypatch, tmp_path: Path):
    monkeypatch.setenv("ROTWK_INSTALL", "ignored-game-path")
    monkeypatch.setenv("CSC_LINK", "ignored-signing-key")
    work = tmp_path / "work"
    work.mkdir()
    env = package_macos.sanitized_environment(work, 1_700_000_000)
    assert env["SOURCE_DATE_EPOCH"] == "1700000000"
    assert env["GIT_LFS_SKIP_SMUDGE"] == "1"
    assert env["HOME"] == str(work / "home")
    assert env["TMPDIR"] == str(work / "tmp")
    assert "ROTWK_INSTALL" not in env
    assert "CSC_LINK" not in env


def test_generic_archive_layout_roundtrips_strict_allowlist(tmp_path: Path):
    root, files = generic_package(tmp_path)
    archive = tmp_path / f"{root.name}.zip"
    extracted = tmp_path / "extract" / root.name
    rebuilt = tmp_path / "rebuilt.zip"
    make_archive.zip_file(root, archive, 1_700_000_000)
    assert audit_package.check_release(package_macos.PLATFORM, archive) == []
    top, contents, problems = audit_package.read_release_zip(archive.read_bytes(), archive.name, nested=True)
    assert (top, contents, problems) == (root.name, files, [])
    assert make_archive.extract_all(archive, extracted) == 0
    package_folder = extracted
    assert {p.relative_to(package_folder).as_posix(): p.read_bytes() for p in package_folder.rglob("*") if p.is_file()} == files
    make_archive.zip_file(package_folder, rebuilt, 1_700_000_000)
    assert rebuilt.read_bytes() == archive.read_bytes()
    with zipfile.ZipFile(archive) as zipped:
        assert all(name.startswith(root.name + "/") for name in zipped.namelist())


def test_generic_audit_rejects_extras_wrong_architecture_and_embedded_paths(tmp_path: Path):
    _, files = generic_package(tmp_path)
    extra = dict(files, **{"unexpected.txt": b"extra"})
    assert any("unexpected.txt is not a file" in p for p in audit_package.release_file_problems("macos-arm64", extra, "pkg"))

    wrong_arch = dict(files)
    wrong_arch["OpenBFME.app/Contents/MacOS/OpenBFME"] = preview_tests.thin_macho(cpu=0x01000007)
    assert any("CPU type is not arm64" in p for p in audit_package.release_file_problems("macos-arm64", wrong_arch, "pkg"))

    local_path = b"/" + b"Users/" + b"builder/work/build"
    binary_path = dict(files)
    binary_path["OpenBFME.app/Contents/Resources/icon.icns"] += local_path
    assert any("developer-machine path" in p for p in audit_package.release_file_problems("macos-arm64", binary_path, "pkg"))

    pck_path = dict(files)
    embedded = b"/" + b"Users/" + b"builder/work/stage"
    pck_path["OpenBFME.app/Contents/Resources/OpenBFME.pck"] = preview_tests.game_pck(
        content=b"extends Node\n# " + embedded + b"\n",
    )
    assert any("developer-machine path" in p for p in audit_package.release_file_problems("macos-arm64", pck_path, "pkg"))

    utf16_path = dict(files)
    utf16_path["OpenBFME.app/Contents/Resources/icon.icns"] += ("/Users/" + "builder/work/build").encode("utf-16le")
    assert any("developer-machine path" in p for p in audit_package.release_file_problems("macos-arm64", utf16_path, "pkg"))

    prefix = "staged-checkout-" + "root"
    exact_prefix = dict(files)
    exact_prefix["OpenBFME.app/Contents/Resources/icon.icns"] += prefix.encode()
    assert any("exact checkout/build/stage path" in p for p in audit_package.release_file_problems(
        "macos-arm64", exact_prefix, "pkg", forbidden_prefixes=(prefix,),
    ))
    assert audit_package.developer_path_problem(b"literal /Users/") == ""


# ---- RELCAND-1 / RELGH-2: the Mac clone tests went with build_macos_release.sh (the Mac host is in no release's path) -------------------
def test_no_release_script_clones_a_repository_locally():
    """every clone in the release's shell scripts goes over git's transport or is a mirror reference, never a hardlinking local clone"""
    import re  # noqa: PLC0415
    for f in sorted(HERE.glob("*.sh")):
        for n, line in enumerate(f.read_text().splitlines(), 1):
            if re.search(r"\bgit(\s+-\S+(\s+[^-\s]\S*)?)*\s+clone\b", line) and "--no-local" not in line and "--no-hardlinks" not in line:
                assert "file://" in line or "https://" in line, f"{f.name}:{n}: {line.strip()}"
