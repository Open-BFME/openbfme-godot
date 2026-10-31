from __future__ import annotations

import hashlib
import plistlib
import io
import re
import struct
import sys
import tarfile
import zipfile
from pathlib import Path
from types import SimpleNamespace

import pytest

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent))

import audit_package  # noqa: E402
import lib_provenance  # noqa: E402
import make_archive  # noqa: E402
import ed25519  # noqa: E402
import package_macos_preview  # noqa: E402
import precommit  # noqa: E402
from test_release_tools import pck  # noqa: E402

PLATFORM = "game-macos-arm64-unstable-preview"
STEM = "openbfme-v0.4.6-macos-arm64-unstable-preview"
VERSION = "v0.4.6 Mac Unstable Preview (Apple Silicon)"
COMMIT = "c0ffee" + "0" * 34
ENGINE_ID = "ab" * 32


def test_preview_branding_pins_cover_exactly_the_three_approved_resources():
    assert set(audit_package.PINNED_GAME_BRANDING) == {
        ".godot/imported/poweredby_openbfme.png-8e49593c1d11f096cae531ecedee93ce.ctex",
        "assets/brand/online_branding.tres",
        "assets/brand/poweredby_openbfme.png.import",
    }
    descriptor = "assets/brand/online_branding.tres"
    content = (HERE.parents[1] / "godot" / descriptor).read_bytes()
    assert hashlib.sha256(content).hexdigest() == audit_package.PINNED_GAME_BRANDING[descriptor]
    assert audit_package.pck_release_problems("game.pck", pck([(descriptor, content)])) == []


def test_the_platform_icon_pins_cover_exactly_the_three_icons():
    """DEVPORT-6: each PLATFORMICON-1 icon's exported texture and import file, nothing else"""
    icons = sorted(p.stem for p in (HERE.parents[1] / "godot/assets/platform_icons").glob("*.png"))
    assert icons == ["linux", "macos", "windows"]
    assert sorted(audit_package.PINNED_GAME_ICONS) == sorted(
        [f".godot/imported/{n}.png-{hashlib.md5(f'res://assets/platform_icons/{n}.png'.encode()).hexdigest()}.ctex" for n in icons]
        + [f"assets/platform_icons/{n}.png.import" for n in icons])


@pytest.mark.parametrize("path", audit_package.PINNED_GAME_ICONS)
def test_a_changed_platform_icon_resource_is_refused(path, monkeypatch):
    entries = [(name, f"icon fixture {i}".encode()) for i, name in enumerate(audit_package.PINNED_GAME_ICONS)]
    for name, content in entries:
        monkeypatch.setitem(audit_package.PINNED_GAME_ICONS, name, hashlib.sha256(content).hexdigest())
    assert audit_package.pck_release_problems("game.pck", pck(entries)) == []
    changed = [(name, bytes([content[0] ^ 1]) + content[1:] if name == path else content) for name, content in entries]
    assert [e for e in audit_package.pck_release_problems("game.pck", pck(changed))] == \
        [f"game.pck:{path}: not the hash-pinned platform icon resource"]
    assert any("not a resource type" in e for e in audit_package.pck_release_problems(
        "launcher.pck", pck(entries), audit_package.LAUNCHER_PCK_RULES))


@pytest.mark.parametrize("path", audit_package.PINNED_GAME_BRANDING)
def test_preview_branding_rejects_changed_bytes_and_unlisted_files(path, monkeypatch):
    entries = [(name, f"owned resource fixture {i}".encode())
               for i, name in enumerate(audit_package.PINNED_GAME_BRANDING)]
    for name, content in entries:
        monkeypatch.setitem(audit_package.PINNED_GAME_BRANDING, name, hashlib.sha256(content).hexdigest())
    assert audit_package.pck_release_problems("game.pck", pck(entries)) == []
    changed = [(name, bytes([content[0] ^ 1]) + content[1:] if name == path else content)
               for name, content in entries]
    assert any("not the hash-pinned owner artwork" in error
               for error in audit_package.pck_release_problems("game.pck", pck(changed)))
    extra = entries + [(path + ".extra", dict(entries)[path])]
    assert any("not a resource type" in error
               for error in audit_package.pck_release_problems("game.pck", pck(extra)))
    assert any("not a resource type" in error for error in audit_package.pck_release_problems(
        "launcher.pck", pck(entries), audit_package.LAUNCHER_PCK_RULES,
    ))


def test_preview_requires_explicit_source_branch_and_clean_tree(monkeypatch):
    answers = {
        ("branch", "--show-current"): "devin/mac-13",
        ("status", "--porcelain=v1", "--untracked-files=all"): "",
        ("rev-parse", "HEAD"): COMMIT,
        ("rev-parse", "--is-shallow-repository"): "false",
        ("show", "-s", "--format=%ct", COMMIT): "1700000000",
    }
    monkeypatch.setattr(package_macos_preview, "git_text", lambda *args: answers[args])
    with pytest.raises(SystemExit, match="expected branch"):
        package_macos_preview.clean_commit()
    assert package_macos_preview.clean_commit("devin/mac-13") == (COMMIT, 1700000000)
    answers[("status", "--porcelain=v1", "--untracked-files=all")] = " M source.py"
    with pytest.raises(SystemExit, match="working tree is not clean"):
        package_macos_preview.clean_commit("devin/mac-13")


def test_preview_archive_materializes_only_verified_committed_lfs_content(tmp_path, monkeypatch):
    content = b"owned artwork fixture, not retail data"
    pointer = ("version https://git-lfs.github.com/spec/v1\n"
               f"oid sha256:{hashlib.sha256(content).hexdigest()}\nsize {len(content)}\n").encode()
    archive = io.BytesIO()
    with tarfile.open(fileobj=archive, mode="w") as stream:
        entry = tarfile.TarInfo("brand.png")
        entry.size = len(pointer)
        stream.addfile(entry, io.BytesIO(pointer))
    monkeypatch.setattr(package_macos_preview, "REPO", tmp_path)
    monkeypatch.setattr(package_macos_preview, "run_bytes", lambda args: archive.getvalue())
    source = tmp_path / "brand.png"
    source.write_bytes(content)
    package_macos_preview.extract_git_archive(["brand.png"], tmp_path / "stage")
    assert (tmp_path / "stage/brand.png").read_bytes() == content
    for wrong in (pointer, content + b"tampered"):
        source.write_bytes(wrong)
        with pytest.raises(SystemExit, match="does not match committed pointer"):
            package_macos_preview.extract_git_archive(["brand.png"], tmp_path / "bad")


def thin_macho(cpu: int = 0x0100000C, *, cmdsize: int = 72, sizeofcmds: int = 72, filesize: int = 104) -> bytes:
    header = struct.pack("<IiiIIIII", 0xFEEDFACF, cpu, 0, 2, 1, sizeofcmds, 0, 0)
    command = struct.pack("<II16sQQQQiiII", 0x19, cmdsize, b"__TEXT", 0x1000, 104, 0, filesize, 7, 5, 0, 0)
    return header + command


def game_pck(resource: str = "scripts/game.gd", content: bytes = b"extends Node\n") -> bytes:
    path = resource.encode() + b"\0"
    path += b"\0" * (-len(path) % 4)
    header_size = 4 + 5 * 4 + 8 + 8 + 64
    directory = (
        struct.pack("<I", 1)
        + struct.pack("<I", len(path))
        + path
        + struct.pack("<QQ", 0, len(content))
        + hashlib.md5(content).digest()   # as Godot writes it (audit_package checks each entry's MD5)
        + struct.pack("<I", 0)
    )
    header = (
        b"GDPC"
        + struct.pack("<5I", 4, 4, 7, 2, 2)
        + struct.pack("<QQ", header_size, header_size + len(content))
        + b"\0" * 64
    )
    return header + content + directory


def fat_macho() -> bytes:
    arm = thin_macho()
    x86 = thin_macho(cpu=0x01000007)
    header_size = 8 + 2 * 20
    arm_offset = header_size
    x86_offset = (arm_offset + len(arm) + 7) & ~7
    header = struct.pack(">II", 0xCAFEBABE, 2)
    header += struct.pack(">IIIII", 0x0100000C, 0, arm_offset, len(arm), 3)
    header += struct.pack(">IIIII", 0x01000007, 3, x86_offset, len(x86), 3)
    return header + arm + b"\0" * (x86_offset - arm_offset - len(arm)) + x86


def preview_files() -> dict[str, bytes]:
    return {
        "OpenBFME.app/Contents/MacOS/OpenBFME": thin_macho(),
        "OpenBFME.app/Contents/Resources/OpenBFME.pck": game_pck(),
        "OpenBFME.app/Contents/Resources/icon.icns": b"\0\xfficon",
        "OpenBFME.app/Contents/Resources/PrivacyInfo.xcprivacy": b"<plist/>\n",
        "OpenBFME.app/Contents/Info.plist": b"<plist/>\n",
        "OpenBFME.app/Contents/PkgInfo": b"APPL????",
        "OpenBFME.app/Contents/_CodeSignature/CodeResources": b"signature",
        "OpenBFME.app/Contents/Frameworks/openbfme.macos.template_debug.arm64.dylib": thin_macho(),
        "OpenBFME Launcher.app/Contents/MacOS/OpenBFME Launcher": thin_macho(),
        "OpenBFME Launcher.app/Contents/Resources/OpenBFME Launcher.pck":
            game_pck("build_info.json", b'{"macos_preview": true}\n'),
        "OpenBFME Launcher.app/Contents/Resources/icon.icns": b"\0\xfflauncher-icon",
        "OpenBFME Launcher.app/Contents/Resources/PrivacyInfo.xcprivacy": b"<plist/>\n",
        "OpenBFME Launcher.app/Contents/Info.plist": b"<plist/>\n",
        "OpenBFME Launcher.app/Contents/PkgInfo": b"APPL????",
        "OpenBFME Launcher.app/Contents/_CodeSignature/CodeResources": b"signature",
        "README-MAC.txt": b"Apple Silicon preview\n",
        "LICENSE": b"license\n",
        "NOTICE": b"notice\n",
        "VERSION": f"OpenBFME {VERSION}\ncommit {COMMIT}\n".encode(),
    }


def write_preview(folder: Path, files: dict[str, bytes]) -> Path:
    root = folder / STEM
    for name, data in files.items():
        path = root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        if (
            name.endswith("/MacOS/OpenBFME")
            or name.endswith("/MacOS/OpenBFME Launcher")
            or name.endswith(".dylib")
        ):
            path.chmod(0o755)
    return root


def test_preview_thin_macho_requires_native_arm64_and_complete_image():
    native = thin_macho()
    assert audit_package.macho_arm64_extent(native) == len(native)
    assert audit_package.binary_problems("native", "macho-arm64", native) == []

    with pytest.raises(ValueError, match="CPU type is not arm64"):
        audit_package.macho_arm64_extent(thin_macho(cpu=0x01000007))
    assert any("not a thin" in problem for problem in audit_package.binary_problems("fat", "macho-arm64", fat_macho()))
    with pytest.raises(ValueError, match="not a thin"):
        audit_package.macho_arm64_extent(b"\xca\xfe\xba\xbe" + b"\0" * 64)
    with pytest.raises(ValueError, match="load command bounds"):
        audit_package.macho_arm64_extent(thin_macho(cmdsize=80))
    with pytest.raises(ValueError, match="sizes do not sum"):
        audit_package.macho_arm64_extent(thin_macho(sizeofcmds=80) + b"\0" * 8)
    with pytest.raises(ValueError, match="segment exceeds"):
        audit_package.macho_arm64_extent(thin_macho(filesize=105))
    assert any("bytes after the Mach-O image" in problem for problem in audit_package.binary_problems(
        "native", "macho-arm64", native + b"trailing",
    ))


def test_preview_allows_launcher_and_rejects_unknown_retail_and_signature_files():
    files = preview_files()
    assert audit_package.release_file_problems(PLATFORM, files, "preview") == []

    files["Retail.big"] = b"not a real archive"
    files["OpenBFME.app/Contents/Resources/icon.icns"] = b"BIGF" + b"\0" * 32
    files["payload.json"] = b"{}"
    files["payload.json.sig"] = b"\0" * 64
    files["OpenBFME Launcher.app/Contents/_CodeSignature/extra"] = b"signature"
    problems = audit_package.release_file_problems(PLATFORM, files, "preview")
    assert any("Retail.big is not a file of the release" in problem for problem in problems)
    assert any("icon.icns: retail-format bytes" in problem for problem in problems)
    assert any("payload.json is not a file of the release" in problem for problem in problems)
    assert any("payload.json.sig is not a file of the release" in problem for problem in problems)
    assert any("_CodeSignature/extra is not a file of the release" in problem for problem in problems)


def test_preview_zip_roundtrip_uses_exact_top_folder_and_allowlist(tmp_path: Path):
    root = write_preview(tmp_path, preview_files())
    archive = tmp_path / f"{STEM}.zip"
    roundtrip = tmp_path / "roundtrip" / STEM
    rebuilt = tmp_path / "rebuilt.zip"
    make_archive.zip_file(root, archive, 1_700_000_000)
    assert audit_package.check_release(PLATFORM, archive) == []

    top, files, problems = audit_package.read_release_zip(archive.read_bytes(), archive.name, nested=True)
    assert problems == []
    assert top == STEM
    assert files == preview_files()
    assert make_archive.extract_all(archive, roundtrip) == 0
    make_archive.zip_file(roundtrip, rebuilt, 1_700_000_000)
    assert rebuilt.read_bytes() == archive.read_bytes()

    wrong_top = tmp_path / "openbfme-v0.4.7-macos-arm64-unstable-preview"
    (wrong_top / "app").mkdir(parents=True)
    (wrong_top / "app" / "file").write_bytes(b"not a preview")
    with pytest.raises(SystemExit, match="nested package files"):
        make_archive.zip_file(wrong_top, tmp_path / "wrong.zip", 1_700_000_000)
    with zipfile.ZipFile(archive) as z:
        assert all(member.filename.startswith(f"{STEM}/") for member in z.infolist())


def test_preview_staging_manifest_keeps_only_arm64_libraries():
    source = """[configuration]
entry_symbol = "openbfme_library_init"

[libraries]
windows.debug.x86_64 = "res://bin/openbfme.windows.template_debug.x86_64.dll"
macos.debug = "res://bin/openbfme.macos.template_debug.universal.dylib"
macos.release = "res://bin/openbfme.macos.template_debug.universal.dylib"
macos.debug.arm64 = "res://bin/openbfme.macos.template_debug.arm64.dylib"
macos.release.arm64 = "res://bin/openbfme.macos.template_debug.arm64.dylib"
"""
    filtered = package_macos_preview.arm64_manifest_text(source)
    assert "macos.debug.arm64 =" in filtered
    assert "macos.release.arm64 =" in filtered
    assert "universal" not in filtered
    assert "x86_64" not in filtered
    assert "windows.debug" not in filtered


def test_launcher_preset_and_build_info_are_preview_only(tmp_path: Path):
    source_preset = (HERE.parent.parent / "launcher/export_presets.cfg").read_text()
    assert len(re.findall(r"^\[preset\.\d+\]$", source_preset, re.M)) == 3
    filtered = package_macos_preview.arm64_launcher_preset_text(source_preset)
    assert 'name="macOS Apple Silicon Preview"' in filtered
    assert 'binary_format/architecture="arm64"' in filtered
    assert "binary_format/embed_pck=false" in filtered
    assert 'application/min_macos_version_arm64="13.00"' in filtered
    assert "macOS Universal" not in filtered

    launcher = tmp_path / "launcher"
    launcher.mkdir()
    key = ed25519.public_key(b"\x01" * 32).hex()
    kms = ed25519.public_key(b"\x02" * 32).hex()
    (launcher / "release_key.pub").write_text(key)
    (launcher / "release_keys.pub").write_text(f"# test list\n{key}  deck-2026\n{kms}  kms-2026\n")
    info = package_macos_preview.write_launcher_build_info(launcher, COMMIT, 1_700_000_000)
    assert info == {
        "version": VERSION,
        "display_version": VERSION,
        "commit": COMMIT,
        "date": "2023-11-14",
        "repo": "Open-BFME/openbfme-godot",
        "release_key": key,
        "release_keys": [key, kms],
        "nightly_keys": [],
        "test_build": False,
        "local_trial": False,
        "macos_preview": True,
        "macos_updates": False,
    }
    # RELGH-2: the staged launcher's nightly_keys.pub is carried; a nightly key that is also a release key is refused
    nk = ed25519.public_key(b"\x03" * 32).hex()
    (launcher / "nightly_keys.pub").write_text(f"# nightly\n{nk}  nightly-2026\n")
    assert package_macos_preview.write_launcher_build_info(launcher, COMMIT, 1_700_000_000)["nightly_keys"] == [nk]
    (launcher / "nightly_keys.pub").write_text(f"{kms}  nightly-2026\n")
    with pytest.raises(SystemExit):
        package_macos_preview.write_launcher_build_info(launcher, COMMIT, 1_700_000_000)
    (launcher / "nightly_keys.pub").unlink()
    # SIGNKMS-1: the list must begin with release_key.pub's key, and parse; a test key alone is a one-key list
    (launcher / "release_keys.pub").write_text(f"{kms}  kms-2026\n{key}  deck-2026\n")
    with pytest.raises(SystemExit):
        package_macos_preview.write_launcher_build_info(launcher, COMMIT, 1_700_000_000)
    (launcher / "release_keys.pub").write_text(f"{key}  Deck\n")
    with pytest.raises(SystemExit):
        package_macos_preview.write_launcher_build_info(launcher, COMMIT, 1_700_000_000)
    (launcher / "release_keys.pub").unlink()
    with pytest.raises(SystemExit):
        package_macos_preview.write_launcher_build_info(launcher, COMMIT, 1_700_000_000)
    test = package_macos_preview.write_launcher_build_info(launcher, COMMIT, 1_700_000_000, test_key=kms)
    assert test["release_key"] == kms and test["release_keys"] == [kms] and test["local_trial"] and test["test_build"]
    other = tmp_path / "keys.pub"
    other.write_text(f"{kms}  kms-test\n{key}  deck-test\n")
    test = package_macos_preview.write_launcher_build_info(launcher, COMMIT, 1_700_000_000, test_key=kms, keys_file=other)
    assert test["release_keys"] == [kms, key]


def test_macos_build_version_accepts_11_and_13(monkeypatch, tmp_path: Path):
    binary = tmp_path / "OpenBFME"
    for minimum in ("11.0", "13.0"):
        output = (
            "Load command 2\n"
            "      cmd LC_BUILD_VERSION\n"
            "  cmdsize 32\n"
            " platform 1\n"
            f"    minos {minimum}\n"
            "      sdk 26.1\n"
        )
        monkeypatch.setattr(
            package_macos_preview, "run", lambda *_args, _output=output, **_kwargs: SimpleNamespace(stdout=_output),
        )
        package_macos_preview.verify_minimum_macos(binary)


@pytest.mark.parametrize(
    ("output", "message"),
    [
        (
            "Load command 2\n      cmd LC_BUILD_VERSION\n  cmdsize 32\n platform 1\n    minos 14.0\n      sdk 26.1\n",
            "later than 13.0",
        ),
        (
            "Load command 2\n      cmd LC_BUILD_VERSION\n  cmdsize 32\n platform 2\n    minos 11.0\n      sdk 26.1\n",
            "non-macOS build platform",
        ),
        ("Load command 2\n      cmd LC_UUID\n", "does not declare LC_BUILD_VERSION"),
    ],
)
def test_macos_build_version_rejects_unsupported_metadata(monkeypatch, tmp_path: Path, output: str, message: str):
    monkeypatch.setattr(
        package_macos_preview, "run", lambda *_args, **_kwargs: SimpleNamespace(stdout=output),
    )
    with pytest.raises(SystemExit, match=message):
        package_macos_preview.verify_minimum_macos(tmp_path / "OpenBFME")


def test_app_minimum_requires_arm64_plist_version_13(tmp_path: Path):
    app = tmp_path / "OpenBFME.app"
    contents = app / "Contents"
    contents.mkdir(parents=True)
    info_plist = contents / "Info.plist"
    info_plist.write_bytes(plistlib.dumps({
        "LSMinimumSystemVersionByArchitecture": {"arm64": "13.0", "x86_64": "11.0"},
    }))
    package_macos_preview.verify_app_minimum(app)

    for minimum in ("12.6", "14.0"):
        info_plist.write_bytes(plistlib.dumps({
            "LSMinimumSystemVersionByArchitecture": {"arm64": minimum, "x86_64": "11.0"},
        }))
        with pytest.raises(SystemExit, match="required macOS 13.0 minimum"):
            package_macos_preview.verify_app_minimum(app)


def test_readme_examples_pass_unchanged_precommit():
    readme = (HERE / "README-MAC.txt").read_text()
    assert readme.startswith(
        "This is our first Mac build: an unstable preview. Expect bugs.\n\n"
        "OpenBFME v0.4.6 Mac Unstable Preview (Apple Silicon)\n\n"
    )
    assert "Apple Silicon Macs only (M1 or newer). There are no plans at this time to release for Intel Macs" in readme
    placeholder = b"Install under /Volumes/Games/RotWK."
    developer_path = b"Install under /Users/" + b"Devin/Games/RotWK."
    assert precommit.check_file("tools/release/README-MAC.txt", placeholder) == ""
    assert "developer-machine path" in precommit.check_file("tools/release/README-MAC.txt", developer_path)


def test_spaced_preview_version_passes_provenance():
    record = (
        f"OPENBFME-BUILD-INFO 1 version={VERSION} commit={COMMIT} dirty=0 "
        f"engine-id={ENGINE_ID} x86-32=OFF id-options="
    ).encode()
    assert lib_provenance.records(record) == [{
        "version": VERSION,
        "commit": COMMIT,
        "dirty": "0",
        "engine-id": ENGINE_ID,
        "x86-32": "OFF",
        "id-options": "",
    }]
    assert lib_provenance.check(record, VERSION, COMMIT, ENGINE_ID) == []


def _mac_package_zip(files: dict[str, bytes]) -> tuple[str, bytes]:
    top = "openbfme-v1.2.3-preview.1-macos-arm64"
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", compression=zipfile.ZIP_DEFLATED) as z:
        for name, data in sorted(files.items()):
            info = zipfile.ZipInfo(top + "/" + name, (2026, 10, 16, 0, 0, 0))
            info.create_system = 3
            info.external_attr = (0o100644) << 16
            z.writestr(info, data, compress_type=zipfile.ZIP_DEFLATED)
    return top, buf.getvalue()


UPSTREAM_PATH = b"\0/root/winrt/winrt/Windows.Foundation.h\0"
PRIVATE_UPSTREAM_PATH = b"\0/root/winrt/winrt/private-source.cpp\0"
SWAP = "OpenBFME Launcher.app/Contents/MacOS/openbfme-bundle-swap"


_EXEMPTION_CASES = ["r2-mz-readme", "macho-in-readme", "magic-only", "path-after-image", "r3-private-path-in-swap-helper",
                    "upstream-path-in-swap-helper", "private-path-in-game", "upstream-path-in-launcher", "upstream-path-in-gdextension"]


@pytest.mark.parametrize("slot, data", [
    # Sol r2 (P2), the reviewer's reproduction: a text file that starts with "MZ"
    ("README-MAC.txt", b"MZ/root/winrt/winrt/private-source.cpp\n"),
    # a whole valid Mach-O in a text slot
    ("README-MAC.txt", thin_macho(filesize=104 + len(UPSTREAM_PATH)) + UPSTREAM_PATH),
    # an executable slot holding bytes that only start like an executable
    ("OpenBFME.app/Contents/MacOS/OpenBFME", b"\xcf\xfa\xed\xfe" + UPSTREAM_PATH),
    # an executable slot, a valid image, the path in bytes after what its headers describe
    ("OpenBFME.app/Contents/MacOS/OpenBFME", thin_macho() + UPSTREAM_PATH),
    # Sol r3 (P2), the reviewer's reproduction: a private path inside a synthetic Mach-O segment of the project-built swap helper
    (SWAP, thin_macho(filesize=104 + len(PRIVATE_UPSTREAM_PATH)) + PRIVATE_UPSTREAM_PATH),
    # the same with a path the real Windows templates hold: still not upstream bytes
    (SWAP, thin_macho(filesize=104 + len(UPSTREAM_PATH)) + UPSTREAM_PATH),
    # and in the game / launcher executables and the GDExtension: no macOS slot is an upstream-template slot (r3 exempted these)
    ("OpenBFME.app/Contents/MacOS/OpenBFME", thin_macho(filesize=104 + len(PRIVATE_UPSTREAM_PATH)) + PRIVATE_UPSTREAM_PATH),
    ("OpenBFME Launcher.app/Contents/MacOS/OpenBFME Launcher", thin_macho(filesize=104 + len(UPSTREAM_PATH)) + UPSTREAM_PATH),
    ("OpenBFME.app/Contents/Frameworks/openbfme.macos.template_debug.arm64.dylib",
     thin_macho(filesize=104 + len(UPSTREAM_PATH)) + UPSTREAM_PATH),
], ids=_EXEMPTION_CASES)
def test_template_exemption_needs_verified_upstream_template_bytes(slot: str, data: bytes):
    """MACPLAY-1 (Sol r2 / r3, P2): the template-path exemption went to any blob whose first bytes were an executable magic (r2), then to
    any whole parsed image in an executable slot (r3), the project-built swap helper included. Now only to the pinned read-only data
    section of an official Linux / Windows Godot template (audit_package.UPSTREAM_TEMPLATE_SECTIONS) in a slot an exported template
    fills: every one of these is a finding in both the strict archive audit (read_release_zip + release_file_problems) and the
    recursive content audit (Audit.blob)"""
    files = preview_files()
    files[SWAP] = thin_macho()
    files[slot] = data
    top, body = _mac_package_zip(files)
    read_top, read_files, problems = audit_package.read_release_zip(body, top + ".zip", nested=True)
    strict = problems + audit_package.release_file_problems("macos-arm64", read_files, top)
    audit = audit_package.Audit()
    audit.blob(top + ".zip", body)
    for found in (strict, audit.problems):
        assert any("developer-machine path" in p and slot in p for p in found), found


def test_the_clean_mac_package_has_no_path_finding():
    """the control of the test above: the same package without an upstream path passes both path audits"""
    files = preview_files()
    files[SWAP] = thin_macho()
    top, body = _mac_package_zip(files)
    read_top, read_files, problems = audit_package.read_release_zip(body, top + ".zip", nested=True)
    strict = problems + audit_package.release_file_problems("macos-arm64", read_files, top)
    audit = audit_package.Audit()
    audit.blob(top + ".zip", body)
    assert [p for p in strict + audit.problems if "path" in p] == []
