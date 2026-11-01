"""Lane RELEASE-1: the package audit, the testers' README and the privacy rule "no network calls besides LAN play"."""
from __future__ import annotations

import configparser
import fnmatch
import hashlib
import io
import json
import os
import re
import shutil
import stat
import struct
import subprocess
import sys
import tarfile
import zipfile
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
sys.path.insert(0, str(HERE))

import audit_package  # noqa: E402
import ed25519  # noqa: E402
import make_archive  # noqa: E402
import readme_testers  # noqa: E402
import seal_package  # noqa: E402

BIG = b"BIGF" + b"\x00" * 60
DDS = b"DDS " + b"\x00" * 60
WAV = b"RIFF\x00\x00\x00\x00WAVEfmt " + b"\x00" * 40


def test_release_report_messages(tmp_path):
    godot = os.environ.get("GODOT") or shutil.which("godot")
    if not godot:
        pytest.skip("Godot is not available for the release message test")
    env = dict(os.environ, HOME=str(tmp_path), XDG_DATA_HOME=str(tmp_path / "data"), XDG_CONFIG_HOME=str(tmp_path / "config"))
    r = subprocess.run([godot, "--headless", "--path", str(REPO / "godot"), "--script", "res://tests/release_message_test.gd"],
                       env=env, capture_output=True, text=True, timeout=60)
    assert r.returncode == 0 and "RELEASE MESSAGES OK" in r.stdout, r.stdout + r.stderr
    assert "SCRIPT ERROR" not in r.stderr, r.stderr


def pck(entries: list[tuple[str, bytes]], fmt: int = 4) -> bytes:
    """A Godot 4 pack in the layout audit_package reads (header, files, directory at the end for format 3), each entry with its content's
    MD5 as Godot writes it (PUBLISH-2 r4: the audit checks it)."""
    header_size = 4 + 5 * 4 + 8 + (8 if fmt >= 3 else 0) + 16 * 4
    file_base = header_size if fmt >= 3 else 0
    blobs, offsets, pos = b"", [], 0
    for _, data in entries:
        offsets.append(pos)
        blobs += data
        pos += len(data)
    directory = struct.pack("<I", len(entries))
    for (path, data), ofs in zip(entries, offsets):
        p = path.encode() + b"\0"
        p += b"\0" * (-len(p) % 4)
        directory += struct.pack("<I", len(p)) + p + struct.pack("<QQ", ofs, len(data)) + hashlib.md5(data).digest() + struct.pack("<I", 0)
    if fmt >= 3:
        head = b"GDPC" + struct.pack("<5I", fmt, 4, 7, 2, 2) + struct.pack("<QQ", file_base, header_size + len(blobs)) + b"\0" * 64
        return head + blobs + directory
    # format 2: directory right after the header, absolute file offsets
    head_len = 4 + 5 * 4 + 8 + 64
    base = head_len + len(directory)
    directory = struct.pack("<I", len(entries))
    for (path, data), ofs in zip(entries, offsets):
        p = path.encode() + b"\0"
        p += b"\0" * (-len(p) % 4)
        directory += struct.pack("<I", len(p)) + p + struct.pack("<QQ", base + ofs, len(data)) + hashlib.md5(data).digest() + struct.pack("<I", 0)
    head = b"GDPC" + struct.pack("<5I", 2, 4, 3, 0, 0) + struct.pack("<Q", 0) + b"\0" * 64
    return head + directory + blobs


def universal_macho(*, gap: int = 0, gap_byte: int = 0, command_padding: int = 0, ncmds: int = 1) -> bytes:
    def thin(cpu: int, subtype: int) -> bytes:
        image_size = 104 + command_padding
        command = struct.pack("<II16sQQQQiiII", 0x19, 72, b"__TEXT", 0x1000, image_size, 0, image_size, 7, 5, 0, 0)
        header = struct.pack("<IiiIIIII", 0xFEEDFACF, cpu, subtype, 2, ncmds, 72 + command_padding, 0, 0)
        return header + command + b"\0" * command_padding

    thin_x86 = thin(0x01000007, 3)
    thin_arm = thin(0x0100000C, 0)
    header_size = 8 + 2 * 20
    arm_offset = header_size + len(thin_x86) + gap
    records = (struct.pack(">IIIII", 0x01000007, 3, header_size, len(thin_x86), 3)
               + struct.pack(">IIIII", 0x0100000C, 0, arm_offset, len(thin_arm), 3))
    return b"\xca\xfe\xba\xbe" + struct.pack(">I", 2) + records + thin_x86 + bytes([gap_byte]) * gap + thin_arm


def macos_release_files() -> dict[str, bytes]:
    game_pck = pck([("scripts/game.gd", b"extends Node\n")])
    launcher_pck = pck([("build_info.json", b'{"version":"v1.0.0"}\n')])
    files = {
        "OpenBFME.app/Contents/MacOS/OpenBFME": universal_macho(),
        "OpenBFME.app/Contents/Resources/OpenBFME.pck": game_pck,
        "OpenBFME.app/Contents/Resources/icon.icns": b"\0\xfficon",
        "OpenBFME.app/Contents/Resources/PrivacyInfo.xcprivacy": b"<plist/>\n",
        "OpenBFME.app/Contents/Info.plist": b"<plist/>\n",
        "OpenBFME.app/Contents/PkgInfo": b"APPL????",
        "OpenBFME.app/Contents/_CodeSignature/CodeResources": b"signature",
        "OpenBFME.app/Contents/Frameworks/openbfme.macos.template_debug.universal.dylib": universal_macho(),
        "OpenBFMELauncher.app/Contents/MacOS/OpenBFME Launcher": universal_macho(),
        "OpenBFMELauncher.app/Contents/Resources/OpenBFME Launcher.pck": launcher_pck,
        "OpenBFMELauncher.app/Contents/Resources/icon.icns": b"\0\xfficon",
        "OpenBFMELauncher.app/Contents/Resources/PrivacyInfo.xcprivacy": b"<plist/>\n",
        "OpenBFMELauncher.app/Contents/Info.plist": b"<plist/>\n",
        "OpenBFMELauncher.app/Contents/PkgInfo": b"APPL????",
        "OpenBFMELauncher.app/Contents/_CodeSignature/CodeResources": b"signature",
        "README_TESTERS.txt": b"test package\n",
        "README_LAUNCHER.txt": b"launcher notes\n",
        "LICENSE": b"license\n",
        "NOTICE": b"notice\n",
        "VERSION": b"OpenBFME v1.0.0\ncommit " + b"0" * 40 + b"\n",
    }
    return files


def macos_folder(tmp_path: Path, files: dict[str, bytes]) -> Path:
    folder = tmp_path / "openbfme-v1.0.0-macos-universal"
    for rel, data in files.items():
        path = folder / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        if rel.endswith("/MacOS/OpenBFME") or rel.endswith("/MacOS/OpenBFME Launcher") or rel.endswith(".dylib"):
            path.chmod(0o755)
    return folder


def test_macos_universal_macho_bounds_and_launcher_pack():
    binary = universal_macho()
    launcher_pack = pck([("build_info.json", b'{"version":"v1.0.0"}\n')])
    assert audit_package.macho_extent(binary) == len(binary)
    assert audit_package.binary_problems("app", "macho", binary) == []
    assert audit_package.embedded_pack_problems(
        "launcher", "macho+pck", binary + launcher_pack + struct.pack("<Q", len(launcher_pack)) + b"GDPC",
    ) == []
    assert audit_package.binary_problems("app", "macho", binary + b"appended")
    with pytest.raises(ValueError, match="architecture bounds"):
        audit_package.macho_extent(binary[:-1])


def test_macos_universal_macho_structural_checks():
    assert audit_package.macho_extent(universal_macho(gap=8)) == len(universal_macho(gap=8))

    mismatch = bytearray(universal_macho())
    struct.pack_into("<I", mismatch, 48 + 4, 0x0100000C)
    with pytest.raises(ValueError, match="CPU type"):
        audit_package.macho_extent(bytes(mismatch))

    with pytest.raises(ValueError, match="padding is not zero"):
        audit_package.macho_extent(universal_macho(gap=8, gap_byte=0x41))

    with pytest.raises(ValueError, match="load command is truncated"):
        audit_package.macho_extent(universal_macho(ncmds=2))

    with pytest.raises(ValueError, match="sizes do not sum"):
        audit_package.macho_extent(universal_macho(command_padding=8))

    misaligned_size = bytearray(universal_macho())
    struct.pack_into("<I", misaligned_size, 48 + 32 + 4, 71)
    with pytest.raises(ValueError, match="load command bounds"):
        audit_package.macho_extent(bytes(misaligned_size))

    outside_command_region = bytearray(universal_macho())
    struct.pack_into("<I", outside_command_region, 48 + 20, 64)
    with pytest.raises(ValueError, match="load command bounds"):
        audit_package.macho_extent(bytes(outside_command_region))


def test_nested_zip_support_is_macos_only(tmp_path: Path):
    folder = tmp_path / "openbfme-v1.0.0-windows-x64"
    (folder / "nested").mkdir(parents=True)
    (folder / "nested/file.txt").write_text("file\n")
    with pytest.raises(SystemExit, match="only for macOS universal apps"):
        make_archive.zip_file(folder, tmp_path / "package.zip", 1_700_000_000)

    archive = tmp_path / "openbfme-v1.0.0-windows-x64.zip"
    with zipfile.ZipFile(archive, "w") as z:
        z.writestr(f"{folder.name}/nested/file.txt", "file\n")
    with pytest.raises(SystemExit, match="nested package files are supported only"):
        seal_package.read_archive(archive)


def test_macos_archive_rejects_links_and_unsafe_paths():
    top = "openbfme-v1.0.0-macos-universal"
    buffer = io.BytesIO()
    with zipfile.ZipFile(buffer, "w") as z:
        link = zipfile.ZipInfo(f"{top}/OpenBFME.app/Contents/Info.plist")
        link.create_system = 3
        link.external_attr = (stat.S_IFLNK | 0o777) << 16
        z.writestr(link, "target")
        z.writestr(f"{top}/../escape", "not a package member")
    _, _, problems = audit_package.read_release_zip(buffer.getvalue(), "macos.zip", nested=True)
    assert any("a link or another special file" in p for p in problems), problems
    assert any("unsafe member path" in p for p in problems), problems


def test_macos_nested_package_allowlist_seal_and_tampering(tmp_path: Path):
    folder = macos_folder(tmp_path, macos_release_files())
    archive = tmp_path / "openbfme-v1.0.0-macos-universal.zip"
    repeat = tmp_path / "repeat.zip"
    make_archive.zip_file(folder, archive, 1_700_000_000)
    make_archive.zip_file(folder, repeat, 1_700_000_000)
    assert archive.read_bytes() == repeat.read_bytes()
    assert audit_package.check_release("macos-universal", archive) == []

    files = macos_release_files()
    files["OpenBFME.app/Contents/MacOS/unexpected"] = b"unexpected"
    problems = audit_package.release_file_problems("macos-universal", files, archive.name)
    assert any("not a file of the release" in p for p in problems), problems
    files = macos_release_files()
    files["OpenBFME.app/Contents/MacOS/OpenBFME"] += b"appended"
    problems = audit_package.release_file_problems("macos-universal", files, archive.name)
    assert any("bytes after the Mach-O image" in p for p in problems), problems

    secret = bytes(range(32))
    seal_package.seal_archive(archive, secret, "Test/repo")
    assert seal_package.check(tmp_path, ed25519.public_key(secret), "Test/repo") == []
    sealed, modes, mtime = seal_package.read_archive(archive)
    sealed["OpenBFME.app/Contents/Info.plist"] += b"tampered"
    seal_package.write_archive(archive, archive.stem, sealed, modes, mtime)
    problems = seal_package.check(tmp_path, ed25519.public_key(secret), "Test/repo")
    assert problems and any("not the inventory of the archive's files" in p for p in problems), problems


def test_macos_launcher_app_extract_preserves_bundle_paths(tmp_path: Path):
    folder = macos_folder(tmp_path, macos_release_files())
    archive = tmp_path / "openbfme-v1.0.0-macos-universal.zip"
    unpack = tmp_path / "unpacked"
    make_archive.zip_file(folder, archive, 1_700_000_000)
    assert make_archive.extract(archive, "OpenBFMELauncher.app", unpack) == 0
    assert (unpack / "OpenBFMELauncher.app/Contents/MacOS/OpenBFME Launcher").is_file()
    assert (unpack / "OpenBFMELauncher.app/Contents/Resources/OpenBFME Launcher.pck").is_file()
    assert not (unpack / "OpenBFMELauncher.app/OpenBFMELauncher.app").exists()


def audit(tmp_path: Path, files: dict[str, bytes]) -> list[str]:
    root = tmp_path / "pkg"
    for name, data in files.items():
        (root / name).parent.mkdir(parents=True, exist_ok=True)
        (root / name).write_bytes(data)
    a = audit_package.Audit()
    a.path(root)
    return a.problems


def test_clean_package_passes(tmp_path):
    assert audit(tmp_path, {"README_TESTERS.txt": b"hello", "OpenBFME.pck": pck([("res://a.gd", b"extends Node")])}) == []


def test_retail_bytes_under_any_name(tmp_path):
    problems = audit(tmp_path, {"data.bin": BIG, "x/notes.txt": DDS, "sound.ogg": WAV, "INI.big": b"x"})
    assert any("BIG archive" in p and "data.bin" in p for p in problems)
    assert any("DDS texture" in p for p in problems)
    assert any("RIFF/WAVE" in p for p in problems)
    assert any("retail-format file name" in p and "INI.big" in p for p in problems)


@pytest.mark.parametrize("fmt", [2, 3, 4])
def test_inside_a_godot_pack(tmp_path, fmt):
    problems = audit(tmp_path, {"OpenBFME.pck": pck([("res://ok.gd", b"pass"), ("res://hidden.res", BIG)], fmt)})
    assert len(problems) == 1 and "hidden.res" in problems[0] and "BIG archive" in problems[0]


def test_pack_appended_to_an_executable(tmp_path):
    pack = pck([("res://w3d.bin", DDS)])
    exe = b"\x7fELF" + b"\x00" * 100 + pack + struct.pack("<Q", len(pack)) + b"GDPC"
    problems = audit(tmp_path, {"OpenBFME.x86_64": exe})
    assert any("embedded pack" in p and "DDS" in p for p in problems)


def test_inside_archives(tmp_path):
    buf = io.BytesIO()
    with tarfile.open(fileobj=buf, mode="w:gz") as t:
        info = tarfile.TarInfo("pkg/innocent.txt")
        info.size = len(WAV)
        t.addfile(info, io.BytesIO(WAV))
    zbuf = io.BytesIO()
    with zipfile.ZipFile(zbuf, "w") as z:
        z.writestr("pkg/OpenBFME.pck", pck([("res://m.mp3data", b"ID3" + b"\0" * 20)]))
    problems = audit(tmp_path, {"a.tar.gz": buf.getvalue(), "b.zip": zbuf.getvalue()})
    assert any("a.tar.gz[gzip]:pkg/innocent.txt" in p for p in problems)
    assert any("b.zip:pkg/OpenBFME.pck:res://m.mp3data" in p and "ID3" in p for p in problems)


def test_developer_paths_and_encrypted_packs(tmp_path):
    enc = bytearray(pck([("res://a.gd", b"x")]))
    struct.pack_into("<I", enc, 20, 1)  # PACK_DIR_ENCRYPTED
    problems = audit(tmp_path, {"VERSION": b"built in /ho" b"me/alice/x", "e.pck": bytes(enc)})
    assert any("developer-machine path in" in p for p in problems)
    assert any("encrypted pack directory" in p for p in problems)


def test_known_issues_resolve_against_stops():
    lines, errors = readme_testers.known_issues(HERE / "known_issues.json", REPO / "docs/STOPS.md")
    assert errors == []
    assert len(lines) >= 5
    text = readme_testers.render("v9.9.9-test", "linux", lines)
    assert "OpenBFME v9.9.9-test" in text and "#feedback" in text and "KNOWN ISSUES" in text
    assert "NO game files" in text


def test_known_issues_have_a_known_area():
    """NOTES-1: the patch notes group the known issues by area (autorelease.AREAS); a missing or unknown one would land under General"""
    sys.path.insert(0, str(HERE))
    import autorelease
    data = json.loads((HERE / "known_issues.json").read_text(encoding="utf-8"))
    for item in data["issues"]:
        assert item.get("area") in autorelease.AREAS, (item.get("area"), item["text"][:50])
    text, _ = autorelease.release_notes("v0.4.0-alpha.9", [], autorelease.load_known_issues(HERE / "known_issues.json"))
    assert "**General**" not in text.split("## Known issues", 1)[1]


def test_known_issue_naming_a_vanished_stop_fails(tmp_path):
    issues = tmp_path / "issues.json"
    issues.write_text('{"issues": [{"stops": ["S-9999999"], "text": "gone"}]}')
    _, errors = readme_testers.known_issues(issues, REPO / "docs/STOPS.md")
    assert errors and "S-9999999" in errors[0]


# ---- review r1: containers are found by their bytes, not their names ---------------------------------------------------------------------

def tar_bytes(members: dict[str, bytes], compression: str = "") -> bytes:
    buf = io.BytesIO()
    # gzip headers carry a timestamp: compress separately with mtime 0 so the bytes (parametrize ids) never change
    with tarfile.open(fileobj=buf, mode="w:" + ("" if compression == "gz" else compression)) as t:
        for n, data in members.items():
            info = tarfile.TarInfo(n)
            info.size = len(data)
            t.addfile(info, io.BytesIO(data))
    return __import__("gzip").compress(buf.getvalue(), mtime=0) if compression == "gz" else buf.getvalue()


def zip_bytes(members: dict[str, bytes]) -> bytes:
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED) as z:
        for n, data in members.items():
            info = zipfile.ZipInfo(n, date_time=(1980, 1, 1, 0, 0, 0))  # fixed time: the bytes are parametrize ids
            info.external_attr = 0o600 << 16
            info.compress_type = zipfile.ZIP_DEFLATED
            z.writestr(info, data)
    return buf.getvalue()


@pytest.mark.parametrize("name,blob", [
    ("notes.txt", zip_bytes({"art/x.bin": DDS})),                        # a renamed ZIP
    ("data.bin", tar_bytes({"pkg/y.bin": BIG}, "gz")),                   # a renamed .tar.gz
    ("data.bin", tar_bytes({"pkg/y.bin": BIG}, "bz2")),                  # a renamed .tar.bz2
    ("data.bin", tar_bytes({"pkg/y.bin": BIG}, "xz")),                   # a renamed .tar.xz
    ("plain.dat2", tar_bytes({"y.bin": WAV})),                           # an uncompressed tar
    ("stream.res", __import__("gzip").compress(BIG, mtime=0)),                    # a gzip stream of a raw archive
    ("deep.txt", zip_bytes({"a.bin": zip_bytes({"b.bin": tar_bytes({"c.bin": DDS}, "gz")})})),  # nested three deep
])
def test_renamed_containers_are_opened(tmp_path, name, blob):
    problems = audit(tmp_path, {name: blob})
    assert problems and any("retail-format bytes" in p for p in problems), problems


def test_renamed_containers_inside_a_pack(tmp_path):
    pack = pck([("res://ok.gd", b"pass"), ("res://music.res", tar_bytes({"x.bin": BIG}, "gz")), ("res://ui.res", zip_bytes({"t": DDS}))])
    problems = audit(tmp_path, {"OpenBFME.pck": pack})
    assert any("music.res[gzip]:x.bin" in p and "BIG" in p for p in problems)
    assert any("ui.res:t" in p and "DDS" in p for p in problems)


def test_zip_appended_to_an_executable(tmp_path):
    exe = b"MZ" + b"\x00" * 200 + zip_bytes({"payload.bin": BIG})
    problems = audit(tmp_path, {"setup.exe": exe})
    assert any("payload.bin" in p for p in problems) and any("ZIP prefix" in p for p in problems)


@pytest.mark.parametrize("magic", [b"7z\xbc\xaf\x27\x1c", b"\x28\xb5\x2f\xfd", b"Rar!\x1a\x07\x00"])
def test_containers_the_audit_cannot_open_are_findings(tmp_path, magic):
    problems = audit(tmp_path, {"blob.bin": magic + b"\x00" * 64})
    assert problems and "cannot open" in problems[0]


def test_member_names_with_retail_suffixes_and_broken_containers(tmp_path):
    problems = audit(tmp_path, {"a.zip": zip_bytes({"INI.big": b"not really"}), "b.bin": b"\x1f\x8b\x08garbage"})
    assert any("retail-format file name" in p and "INI.big" in p for p in problems)
    assert any("unreadable gzip data" in p for p in problems)


# ---- review r2: every container is consumed to its end, nothing unexplained -------------------------------------------------------------

import bz2 as _bz2  # noqa: E402
import gzip as _gzip  # noqa: E402
import lzma as _lzma  # noqa: E402

COMPRESS = {"gzip": _gzip.compress, "bzip2": _bz2.compress, "xz": _lzma.compress}


@pytest.mark.parametrize("kind", sorted(COMPRESS))
def test_every_member_stream_is_audited(tmp_path, kind):
    c = COMPRESS[kind]
    blob = c(b"harmless text\n") + c(BIG)
    problems = audit(tmp_path, {"notes.bin": blob})
    assert any("#2]" in p and "BIG" in p for p in problems), problems
    # the same inside a pack
    problems = audit(tmp_path / "p", {"OpenBFME.pck": pck([("res://a.res", blob)])})
    assert any("a.res" in p and "BIG" in p for p in problems), problems


@pytest.mark.parametrize("kind", sorted(COMPRESS))
def test_truncated_streams_are_findings(tmp_path, kind):
    blob = COMPRESS[kind](tar_bytes({"x.txt": b"hello" * 1000}))
    problems = audit(tmp_path, {"data.bin": blob[:len(blob) // 2]})
    assert any("truncated" in p for p in problems), problems


@pytest.mark.parametrize("kind", sorted(COMPRESS))
def test_trailing_bytes_after_a_stream_are_findings(tmp_path, kind):
    for tail in (BIG, b"garbage", b"\x00\x00\x00"):
        problems = audit(tmp_path, {"data.bin": COMPRESS[kind](b"ok") + tail})
        assert any("trailing bytes" in p or "padding" in p for p in problems), (tail, problems)


def test_xz_stream_padding_is_explained(tmp_path):
    assert audit(tmp_path, {"data.bin": _lzma.compress(b"ok") + b"\0" * 8 + _lzma.compress(b"ok2")}) == []


def v7_tar(members: dict[str, bytes]) -> bytes:
    """An old V7 tar (no "ustar" magic), written by hand: Python's tarfile cannot write this format."""
    out = b""
    for name, data in members.items():
        h = bytearray(512)
        h[0:len(name)] = name.encode()
        h[100:108] = b"0000644\0"
        h[108:116] = b"0000000\0"
        h[116:124] = b"0000000\0"
        h[124:136] = b"%011o\0" % len(data)
        h[136:148] = b"00000000000\0"
        h[148:156] = b" " * 8
        h[156] = ord("0")
        h[148:156] = b"%06o\0 " % sum(h)
        out += bytes(h) + data + b"\0" * (-len(data) % 512)
    return out + b"\0" * 1024


def test_v7_tar_is_recognised(tmp_path):
    problems = audit(tmp_path, {"old.dat2": v7_tar({"a.txt": b"fine", "b.bin": BIG})})
    assert any("b.bin" in p and "BIG" in p for p in problems), problems
    assert audit(tmp_path / "ok", {"old.dat2": v7_tar({"a.txt": b"fine"})}) == []


def test_data_after_the_end_of_a_tar_is_a_finding(tmp_path):
    for tail in (tar_bytes({"hidden.bin": DDS}), BIG + b"\0" * 508):
        problems = audit(tmp_path, {"x.tar": tar_bytes({"ok.txt": b"ok"}) + tail})
        assert any("not zero blocks" in p for p in problems), problems


def test_zip_bytes_outside_the_listed_entries_are_findings(tmp_path):
    good = zip_bytes({"a.txt": b"fine"})
    eocd = good.rfind(b"PK\x05\x06")
    cd_off, = struct.unpack_from("<I", good, eocd + 16)
    hidden = b"PK\x03\x04" + b"\0" * 26 + BIG              # an unlisted local entry before the central directory
    sneaky = bytearray(good[:cd_off] + hidden + good[cd_off:])
    struct.pack_into("<I", sneaky, eocd + len(hidden) + 16, cd_off + len(hidden))
    problems = audit(tmp_path, {"a.zip": bytes(sneaky)})
    assert any("belong to no listed entry" in p or "not explained" in p for p in problems), problems
    problems = audit(tmp_path / "t", {"b.zip": good + BIG})
    assert problems, "bytes after the end record"
    assert audit(tmp_path / "ok", {"c.zip": good}) == []


def test_malformed_zip_signatures_are_findings(tmp_path):
    for blob in (b"PK\x03\x04" + b"\x14" * 40, b"PK\x05\x06" + b"\0" * 10):
        problems = audit(tmp_path, {"weird.bin": blob})
        assert any("malformed ZIP" in p for p in problems), problems


def test_a_zip_prefix_is_a_finding(tmp_path):
    problems = audit(tmp_path, {"setup.bin": b"MZ" + b"\0" * 62 + BIG + zip_bytes({"a.txt": b"fine"})})
    assert any("ZIP prefix" in p for p in problems), problems


def test_bytes_in_a_pack_outside_its_entries_are_findings(tmp_path):
    good = pck([("res://a.gd", b"pass")])
    dir_offset, = struct.unpack_from("<Q", good, 32)
    bad = bytearray(good[:dir_offset] + BIG + good[dir_offset:])
    struct.pack_into("<Q", bad, 32, dir_offset + len(BIG))
    problems = audit(tmp_path, {"OpenBFME.pck": bytes(bad)})
    assert any("belong to no entry" in p for p in problems), problems
    assert audit(tmp_path / "ok", {"OpenBFME.pck": good}) == []


# ---- privacy: no network calls besides LAN play ----------------------------------------------------------------------------------------

import net_guard  # noqa: E402


def network_call_sites(text: str) -> list[str]:
    return net_guard.native_findings(text)


def test_game_scripts_make_no_network_calls():
    """The shipped GDScript (everything but the tests, which only start sub-processes) uses no Godot networking class: LAN play is native
    (GameNetwork/Transport.cpp), nothing else talks to the network. Lane ONLINE-2: except the online lobby client the player starts
    (net_guard.ALLOWED_GAME, its one request to the named lobby: test_net_guard_files.py)."""
    found = {f: [x for x in v if not net_guard.game_finding_allowed(f, x)] for f, v in net_guard.scan().items() if f.endswith(".gd")}
    assert {f: v for f, v in found.items() if v} == {}


@pytest.mark.parametrize("line, allowed", [
    ('\tvar pid := OS.create_process("perf", ["stat", "-t", str(OS.get_process_id())])', True),
    ('\tOS.execute("kill", ["-INT", str(p)])', True),
    ('\tOS.execute("curl", ["https://example.com"])', False),
    ('\tOS.create_process(tool, ["stat"])', False),
    ('\tOS.execute("kill", []); OS.execute("wget", ["x"])', False),
    ('\tOS.shell_open("https://example.com")', False),
])
def test_only_the_profiler_may_start_processes(line, allowed):
    """PERF-3's --perf-stat starts `perf stat` and stops it with `kill -INT`: those literal calls are allowed, every other process call is a finding."""
    assert (net_guard.gdscript_findings("func f():\n" + line + "\n") == []) == allowed


def test_only_the_lan_transport_opens_sockets():
    """Native network references live in GameNetwork/Transport.cpp (UDP: the LAN lobby's broadcast and the game's peers) and nowhere else."""
    found = {f for f in net_guard.scan() if not f.endswith(".gd")}
    assert found <= net_guard.ALLOWED_NATIVE, f"network references outside the LAN transport: {sorted(found - net_guard.ALLOWED_NATIVE)}"
    assert "engine/src/GameNetwork/Transport.cpp" in found
    transport = (REPO / "engine/src/GameNetwork/Transport.cpp").read_text(encoding="utf-8")
    assert "SOCK_STREAM" not in transport, "the LAN transport is UDP only"


@pytest.mark.parametrize("snippet", [
    "int s = ::socket(AF_INET, SOCK_STREAM, 0);",
    "socketpair(AF_UNIX, SOCK_STREAM, 0, fd);",
    "SOCKET s = WSASocketW(AF_INET, SOCK_STREAM, 0, 0, 0, 0);",
    "WSASocketA(AF_INET, SOCK_DGRAM, 0, 0, 0, 0);",
    "if (::connect(fd, addr, len) == 0) {}",
    "getaddrinfo(host, port, &hints, &res);",
    "WSAConnectByNameW(s, name, port, 0, 0, 0, 0, 0, 0);",
    "auto h = InternetOpenUrlA(i, url, 0, 0, 0, 0);",
    "socket (AF_INET6, SOCK_DGRAM, 0);",
    "int fds[2]; if (socketpair(AF_UNIX, SOCK_DGRAM, 0, fds)) {}",
    # review r2: references that are no call, and calls the comment stripper hid
    "int s = (::socket)(AF_INET, SOCK_STREAM, 0);",
    "auto open = &socket; int s = open(AF_INET, SOCK_STREAM, 0);",
    "int (*open)(int, int, int) = socket;",
    "const char *url = \"http://example.org\"; int s = socket(AF_INET, SOCK_STREAM, 0);",
    "const char *c = \"/*\"; int s = socket(2, 1, 0); // */",
    "auto r = R\"x(\" // )x\"; connect(fd, a, n);",
    "auto f = (decltype(&::connect))GetProcAddress(h, \"connect\");",
    "auto f = GetProcAddress(LoadLibraryA(\"ws2_32.dll\"), \"WSASocketW\");",
    "void *lib = dlopen(\"libcurl.so.4\", RTLD_NOW);",
    "return socket(1, 2, 0);",
])
def test_the_network_guard_sees_every_socket_reference(snippet):
    assert network_call_sites("void f() { " + snippet + " }"), snippet


@pytest.mark.parametrize("source", ["#include <sys/socket.h>\n", "#  include <winsock2.h>\n", "#include \"curl/curl.h\"\n", "#include <netdb.h>\n"])
def test_the_network_guard_sees_network_headers(source):
    assert network_call_sites(source), source


@pytest.mark.parametrize("snippet", [
    "rs->connect(\"frame_post_draw\", c);",
    "button.connect(signal, cb);",
    "ClassDB::bind_method(D_METHOD(\"x\"), &X::x);",
    "auto f = std::bind(&A::b, this);",
    "Signal::connect(a);",
    "// socket(AF_INET, SOCK_STREAM, 0) in a comment",
    "/* ::socket(1, 2, 3) */",
    "log(\"socket(AF_INET) is not called here\");",
    "UDP &socket = lan.take(); m_socket = &socket; send(socket);",
    "std::unique_ptr<UDP> socket = take(); use(*socket);",
    "int n = 1'000; char q = 'x';",
])
def test_the_network_guard_ignores_members_locals_comments_and_strings(snippet):
    assert network_call_sites("void f() { " + snippet + " }") == [], snippet


@pytest.mark.parametrize("source", [
    'var s := "#"; var h := HTTPRequest.new()',
    'var p = ClassDB.instantiate("StreamPeerTCP")',
    'OS.execute("curl", ["x"])',
    'var u = \'http://x\'; var t = TCPServer.new()',
    'var doc = """ # """; OS.shell_open("http://x")',
])
def test_the_gdscript_guard(source):
    assert net_guard.gdscript_findings(source), source


def test_the_gdscript_guard_ignores_comments_and_text():
    assert net_guard.gdscript_findings('# HTTPRequest.new()\nprint("no HTTPRequest here")\nvar x = node.execute') == []


# ---- review r1: library provenance (tools/release/lib_provenance.py) ---------------------------------------------------------------------

import lib_provenance  # noqa: E402
import make_archive  # noqa: E402

GOOD_ID = "ab" * 32
COMMIT = "c0ffee" + "0" * 34


def library(*records: str) -> bytes:
    body = b"\x7fELF" + b"\x00" * 64
    for r in records:
        body += r.encode() + b"\x00" + b"\x01" * 16
    return body


def record(version="v1.0.0", commit=COMMIT, dirty="0", engine=GOOD_ID) -> str:
    return f"OPENBFME-BUILD-INFO 1 version={version} commit={commit} dirty={dirty} engine-id={engine} x86-32=OFF id-options="


def test_the_build_of_the_commit_passes():
    assert lib_provenance.check(library(record()), "v1.0.0", COMMIT, GOOD_ID) == []


@pytest.mark.parametrize("data,why", [
    # review r1: an old library with the commit hash planted in it (the former grep accepted it)
    (library("OpenBFME old build", COMMIT), "0 build records"),
    (library(record(version="v0.9.0")), "version v0.9.0"),
    (library(record(commit="d" * 40)), "built from commit"),
    (library(record(dirty="1")), "local changes"),
    (library(record(engine="cd" * 32)), "not the committed ones"),
    (library(record(), record()), "2 build records"),
    (library("OPENBFME-BUILD-INFO 1 version=x"), "malformed"),
])
def test_mismatched_libraries_are_refused(data, why):
    problems = lib_provenance.check(data, "v1.0.0", COMMIT, GOOD_ID)
    assert problems and any(why in p for p in problems), problems


def test_engine_id_matches_cmake():
    """lib_provenance's manifest is cmake/EngineId.cmake's: pinned on a small tree and, when CMake runs here, on the real engine."""
    import shutil
    import subprocess
    import tempfile
    # under $HOME: the Deck's cmake is a flatpak wrapper that sees only the home folder
    base = Path(tempfile.mkdtemp(prefix="openbfme-engine-id-", dir=Path.home()))
    try:
        _engine_id_matches_cmake(base / "engine", shutil, subprocess)
    finally:
        shutil.rmtree(base, ignore_errors=True)


def _engine_id_matches_cmake(root, shutil, subprocess):
    (root / "src/Common").mkdir(parents=True)
    (root / "src/Common/a.cpp").write_bytes(b"int a;\r\n")  # CRLF hashes as LF
    (root / "cmake").mkdir()
    (root / "cmake/x.cmake").write_text("set(X 1)\n")
    (root / "CMakeLists.txt").write_text("project(x)\n")
    lf = lib_provenance.engine_id_of(root)
    (root / "src/Common/a.cpp").write_bytes(b"int a;\n")
    assert lib_provenance.engine_id_of(root) == lf
    assert lib_provenance.engine_id_of(root, "ON") != lf
    (root / "src/Common/b.cpp").write_text("// an uncommitted file changes the id\n")
    assert lib_provenance.engine_id_of(root) != lf
    cmake = shutil.which("cmake")
    if not cmake:
        pytest.skip("SKIPPED LOUDLY: no cmake to compare with cmake/EngineId.cmake")
    script = REPO / "engine/cmake/EngineId.cmake"
    for tree in (root, REPO / "engine"):
        p = subprocess.run([cmake, f"-DOPENBFME_ID_ROOT={tree}", "-P", str(script)], capture_output=True, text=True)
        m = re.search(r"engine-id ([0-9a-f]{64})", p.stdout + p.stderr)
        assert m, p.stdout + p.stderr
        assert m.group(1) == lib_provenance.engine_id_of(tree), tree


def test_the_windows_readme_says_to_attach_the_log_not_the_console():
    """Review r2: the Windows console is not filtered, so the testers' README must say so (the Linux one asks for the log too)."""
    lines, _ = readme_testers.known_issues(HERE / "known_issues.json", REPO / "docs/STOPS.md")
    win = readme_testers.render("v9", "windows", lines)
    assert "NOT a copy of the console window" in win and "OpenBFME.console.exe" in win and "not filtered" in win
    assert "rather than a copy of the terminal output" in readme_testers.render("v9", "linux", lines)


# ---- review r3: the tricks of r3 in the content audit, and the release ALLOWLIST -----------------------------------------------------------

def zip_with_dir_payload() -> bytes:
    """A ZIP whose directory entry "art/" carries a BIG payload (zipfile.writestr of a name ending in "/")."""
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", zipfile.ZIP_STORED) as z:
        z.writestr(zipfile.ZipInfo("art/"), BIG)
        z.writestr("ok.txt", b"fine")
    return buf.getvalue()


def zip_with_deflate_tail() -> bytes:
    """A deflated member whose compressed region carries a BIG after the end of its deflate stream (zipfile stops at the stream's end)."""
    good = bytearray(zip_bytes({"a.txt": b"fine" * 10}))
    eocd = good.rfind(b"PK\x05\x06")
    cd_off, = struct.unpack_from("<I", good, eocd + 16)
    tail = BIG
    out = good[:cd_off] + tail + good[cd_off:]
    out = bytearray(out)
    # the member's compressed size, in its local header (offset 18) and its central directory record (offset 20), grows by the tail
    csize, = struct.unpack_from("<I", out, 18)
    struct.pack_into("<I", out, 18, csize + len(tail))
    cd = cd_off + len(tail)
    struct.pack_into("<I", out, cd + 20, csize + len(tail))
    e = len(out) - 22
    struct.pack_into("<I", out, e + 16, cd)
    return bytes(out)


def tar_with_padding_payload() -> bytes:
    t = bytearray(tar_bytes({"a.txt": b"x"}))
    t[512 + 1:512 + 1 + len(BIG)] = BIG  # inside the 511 padding bytes after the 1-byte member
    return bytes(t)


@pytest.mark.parametrize("make", [zip_with_dir_payload, zip_with_deflate_tail, tar_with_padding_payload])
def test_r3_tricks_are_content_findings(tmp_path, make):
    blob = make()
    assert audit(tmp_path, {"x.bin": blob}), make.__name__
    assert audit(tmp_path / "p", {"OpenBFME.pck": pck([("res://a.res", blob)])}), make.__name__ + " in a pack"


def elf64(extra: bytes = b"") -> bytes:
    """A minimal ELF64 header (no segments, no sections): its image is exactly its 64 header bytes."""
    h = bytearray(64)
    h[0:4] = b"\x7fELF"
    h[4], h[5], h[6] = 2, 1, 1
    struct.pack_into("<HHHH", h, 52, 64, 56, 0, 64)
    return bytes(h) + extra


def launcher_pack(entries=None) -> bytes:
    return pck(entries or [("project.binary", b"ECFG" + b"\0" * 4), ("scripts/main.gd", b"extends Control\n"),
                           ("scenes/main.tscn", b"[gd_scene format=3]\n"), ("build_info.json", b"{}\n")])


def elf_with_pack(pack: bytes, extra: bytes = b"") -> bytes:
    """an ELF64 whose one section holds the pack and its trailer, as Godot's export embeds it (header, section table, pack, u64 size,
    "GDPC")"""
    body = pack + struct.pack("<Q", len(pack)) + b"GDPC"
    h = bytearray(64)
    h[0:4] = b"\x7fELF"
    h[4], h[5], h[6] = 2, 1, 1
    struct.pack_into("<Q", h, 40, 64)                     # e_shoff
    struct.pack_into("<HHHH", h, 52, 64, 56, 0, 64)       # e_ehsize, e_phentsize, e_phnum, e_shentsize
    struct.pack_into("<H", h, 60, 1)                      # e_shnum
    sh = bytearray(64)
    struct.pack_into("<I", sh, 4, 1)                      # SHT_PROGBITS
    struct.pack_into("<QQ", sh, 24, 128, len(body))       # sh_offset, sh_size
    return bytes(h) + bytes(sh) + extra + body


def release_files(**override) -> dict:
    """the release's package of a platform (lane ONEZIP-1: the game's files and the launcher's executable with its pack embedded)"""
    files = {"OpenBFME.x86_64": elf64(), "openbfme.linux.template_debug.x86_64.so": elf64(),
             "OpenBFME.pck": pck([("scripts/game.gd", b"extends Node\n"), ("project.binary", b"ECFG" + struct.pack("<I", 0))]),
             "README_TESTERS.txt": b"read me\n", "LICENSE": b"GPL\n", "NOTICE": b"notice\n", "VERSION": b"OpenBFME v1\n",
             "OpenBFMELauncher.x86_64": elf_with_pack(launcher_pack()), "README_LAUNCHER.txt": b"the launcher\n"}
    files.update(override)
    return {k: v for k, v in files.items() if v is not None}


def write_release(tmp_path: Path, files: dict, top="openbfme-v1-linux-x64") -> Path:
    d = tmp_path / top
    d.mkdir(parents=True)
    for n, data in files.items():
        (d / n).parent.mkdir(parents=True, exist_ok=True)
        (d / n).write_bytes(data)
    return d


def release_problems(tmp_path, **override):
    return audit_package.check_release("linux", write_release(tmp_path, release_files(**override)))


def test_the_release_set_passes(tmp_path):
    assert release_problems(tmp_path) == []
    # lane ONEZIP-1: the release's package holds the launcher; the game alone is only package.sh --no-launcher's trial set
    alone = {"OpenBFMELauncher.x86_64": None, "README_LAUNCHER.txt": None}
    assert [p.split(": ", 1)[1] for p in release_problems(tmp_path / "alone", **alone)] == [
        "OpenBFMELauncher.x86_64 is missing", "README_LAUNCHER.txt is missing"]
    assert audit_package.check_release("game-linux", write_release(tmp_path / "trial", release_files(**alone))) == []
    assert [p.split(": ", 1)[1] for p in audit_package.check_release("game-linux", write_release(tmp_path / "both", release_files()))] == [
        "OpenBFMELauncher.x86_64 is not a file of the release", "README_LAUNCHER.txt is not a file of the release"]
    # the launcher in the package is checked like the launcher package's: its pack embedded, only the launcher's resource types
    assert any("no embedded Godot pack" in p for p in release_problems(tmp_path / "nopack", **{"OpenBFMELauncher.x86_64": elf64()}))
    bad = elf_with_pack(launcher_pack([("tests/unit_test.gd", b"extends SceneTree\n")]))
    assert any("not a resource type" in p for p in release_problems(tmp_path / "badpack", **{"OpenBFMELauncher.x86_64": bad}))
    # printable signatures in prose are text, not content ("BIG4 archives" in NOTICE)
    assert release_problems(tmp_path / "prose", NOTICE=b"Uses BIG4 archives, ustar and MSCF in prose\n") == []


@pytest.mark.parametrize("override,why", [
    ({"extra.txt": b"hi"}, "not a file of the release"),
    ({"VERSION": None}, "is missing"),
    ({"sub/notes.txt": b"x"}, "not a file of the release"),
    ({"OpenBFME.x86_64": elf64(BIG)}, "after the ELF image"),
    ({"OpenBFME.x86_64": b"#!/bin/sh\n"}, "not a valid ELF"),
    ({"README_TESTERS.txt": b"see " + zip_bytes({"a": b"b"})}, "signature"),
    ({"LICENSE": b"\x1f\x8b\x08\x00 hidden"}, "not UTF-8"),
    ({"OpenBFME.pck": pck([("scripts/game.gd", b"extends Node\n"), ("assets/music.bin", b"x")])}, "not a resource type"),
    ({"OpenBFME.pck": pck([("scripts/x.gd", b"# " + BIG)])}, "control characters"),
    ({"OpenBFME.pck": pck([(".godot/exported/1/export-" + "0" * 32 + "-a.scn", b"RSRC" + BIG)])}, "BIG archive signature"),
    ({"OpenBFME.pck": pck([("scripts/x.gd", b"x" * (600 << 10))])}, "bytes of text"),
    ({"OpenBFME.pck": pck([("scripts/x.gd", b"var s = '" + b"PK\x03\x04" + b"'")])}, "ZIP entry signature"),
    ({"OpenBFME.pck": pck([("scripts/x.gdc", b"GDSC" + b"\x28\xb5\x2f\xfd")])}, "not a resource type"),
    ({"OpenBFME.pck": pck([(".godot/exported/1/export-" + "0" * 32 + "-a.scn", b"RSCC" + b"\0" * 8)])}, "RSRC"),
    ({"OpenBFME.pck": pck([("project.binary", b"ECFG" + struct.pack("<I", 0) + BIG)])}, "after the last setting"),
    ({"OpenBFME.pck": pck([("scripts/x.gd", zip_with_dir_payload())])}, "signature"),
    ({"OpenBFME.pck": pck([("scripts/x.gd", tar_with_padding_payload())])}, "control characters"),
    ({"OpenBFME.pck": pck([(".godot/exported/1/export-" + "0" * 32 + "-a.scn", b"RSRC" + tar_with_padding_payload())])}, "signature"),
])
def test_anything_outside_the_release_is_a_finding(tmp_path, override, why):
    problems = release_problems(tmp_path, **override)
    assert any(why in p for p in problems), problems


def release_tar_gz(tmp_path: Path, files: dict, extra=None, tail=b"", fmt=tarfile.GNU_FORMAT) -> Path:
    d = write_release(tmp_path / "src", files)
    out = tmp_path / (d.name + ".tar.gz")
    if extra is None and not tail and fmt == tarfile.GNU_FORMAT:
        make_archive.tar_gz(d, out, 1700000000)  # what package.sh writes
        return out
    buf = io.BytesIO()
    with tarfile.open(fileobj=buf, mode="w", format=fmt) as t:
        t.add(d, arcname=d.name, recursive=False)
        for n in sorted(files):
            t.add(d / n, arcname=f"{d.name}/{n}")
        if extra:
            extra(t)
    out.write_bytes(_gzip.compress(buf.getvalue() + tail))
    return out


def test_the_release_archive_passes(tmp_path):
    assert audit_package.check_release("linux", release_tar_gz(tmp_path, release_files())) == []


def test_release_archive_tricks_are_findings(tmp_path):
    files = release_files()
    def symlink(t):
        info = tarfile.TarInfo("openbfme-v1-linux-x64/link")
        info.type = tarfile.SYMTYPE
        info.linkname = "/etc/passwd"
        t.addfile(info)
    cases = {
        "a second gzip member": lambda p: p.write_bytes(p.read_bytes() + _gzip.compress(BIG)),
        "bytes after the gzip stream": lambda p: p.write_bytes(p.read_bytes() + BIG),
    }
    for why, mutate in cases.items():
        p = release_tar_gz(tmp_path / why.replace(" ", "_"), files)
        mutate(p)
        assert audit_package.check_release("linux", p), why
    assert audit_package.check_release("linux", release_tar_gz(tmp_path / "sym", files, extra=symlink))
    assert audit_package.check_release("linux", release_tar_gz(tmp_path / "after", files, tail=tar_bytes({"x": BIG})))
    assert audit_package.check_release("linux", release_tar_gz(tmp_path / "pax", files, fmt=tarfile.PAX_FORMAT))
    # padding with data in the package tar
    p = release_tar_gz(tmp_path / "pad", files)
    tar = bytearray(_gzip.decompress(p.read_bytes()))
    pos = tar.find(b"read me\n")
    tar[pos + 8:pos + 8 + len(BIG)] = BIG
    p.write_bytes(_gzip.compress(bytes(tar)))
    assert any("padding" in x for x in audit_package.check_release("linux", p))


def test_release_zip_structure(tmp_path):
    top = "openbfme-v1-windows-x64"
    good = zip_bytes({f"{top}/README_TESTERS.txt": b"read me\n"})
    _, files, problems = audit_package.read_release_zip(good, "w.zip")
    assert problems == [] and files == {"README_TESTERS.txt": b"read me\n"}
    for blob, why in ((zip_with_dir_payload(), "directory entry"), (zip_with_deflate_tail(), "after the deflate stream"),
                      (good + b"x", "after the end"), (b"MZ" + good, "before the first entry")):
        _, _, problems = audit_package.read_release_zip(blob, "w.zip")
        assert any(why in p for p in problems), (why, problems)


def test_the_checksum_file(tmp_path):
    import hashlib
    a = tmp_path / "openbfme-v1-linux-x64.tar.gz"
    a.write_bytes(b"archive")
    sums = tmp_path / "SHA256SUMS-v1.txt"
    sums.write_text(f"{hashlib.sha256(b'archive').hexdigest()}  {a.name}\n")
    assert audit_package.check_release("linux", sums) == []
    sums.write_text(f"{'0' * 64}  {a.name}\nextra line\n")
    problems = audit_package.check_release("linux", sums)
    assert any("does not match" in p for p in problems) and any("unexpected line" in p for p in problems)


# ---- review r3: the global qualifier after a keyword, and shadowing limited to the local's scope ---------------------------------------------

@pytest.mark.parametrize("source", [
    "int f() { return ::socket(AF_INET, SOCK_STREAM, 0); }",
    "int f() { if (x) return ::connect(fd, a, n); return 0; }",
    "bool f(int x) { return x > ::socket(1, 2, 0); }",
    "void a(UDP &socket) { use(socket); }\nvoid b() { auto p = socket; }",
    "void f() { { UDP socket; use(socket); } auto p = &socket; }",
    "void f() { for (UDP socket : list) { use(socket); } auto q = socket; }",
    "struct S { void g(UDP &socket); }; void h() { auto r = socket; }",
])
def test_r3_guard_cases_are_findings(source):
    assert net_guard.native_findings(source), source


@pytest.mark.parametrize("source", [
    "A::A(UDP &socket, int n) : m_socket(socket), m_n(n) { use(socket); }",
    "struct T { T(UDP &socket, int x); };",
    "void f() { UDP socket; socket.open(1); use(socket); }",
    "void f() { std::unique_ptr<UDP> socket = take(); if (socket) use(*socket); }",
    "void f() { for (UDP &socket : list) { use(socket); } }",
])
def test_locals_in_their_scope_are_no_findings(source):
    assert net_guard.native_findings(source) == [], source


# ---- review r4: the cheap concrete checks, and reproducibility (a tampered package is not what the commit builds) ----------------------------

import zlib  # noqa: E402


def _retar_header(tar: bytearray, offset: int, field: slice, value: bytes) -> None:
    """Writes `value` into a tar header field and fixes the header checksum (what a hand-edit would do)."""
    tar[field] = value.ljust(field.stop - field.start, b"\0")[:field.stop - field.start]
    tar[offset + 148:offset + 156] = b" " * 8
    tar[offset + 148:offset + 156] = b"%06o\0 " % sum(tar[offset:offset + 512])


def test_r4_text_scenes(tmp_path):
    """Scenes ship as their committed .tscn text; binary bytes appended to one (the review's game.scn case) are no text."""
    good = b'[gd_scene format=3 uid="uid://x"]\n\n[node name="Game" type="Node"]\n'
    assert release_problems(tmp_path / "ok", **{"OpenBFME.pck": pck([("scenes/game.tscn", good)])}) == []
    problems = release_problems(tmp_path / "bad", **{"OpenBFME.pck": pck([("scenes/game.tscn", good + zlib.compress(BIG))])})
    assert problems, problems
    problems = release_problems(tmp_path / "nohdr", **{"OpenBFME.pck": pck([("scenes/game.tscn", b"extends Node\n")])})
    assert any("gd_scene" in p for p in problems), problems


def test_r4_bytes_after_a_scene_footer(tmp_path):
    scene = b"RSRC" + b"\0" * 40 + b"RSRC"
    name = ".godot/exported/1/export-" + "0" * 32 + "-game.scn"
    assert release_problems(tmp_path / "ok", **{"OpenBFME.pck": pck([(name, scene)])}) == []
    problems = release_problems(tmp_path / "bad", **{"OpenBFME.pck": pck([(name, scene + zlib.compress(BIG))])})
    assert any("after the resource's RSRC footer" in p for p in problems), problems


def test_r4_a_tar_linkname_payload(tmp_path):
    p = release_tar_gz(tmp_path, release_files())
    tar = bytearray(_gzip.decompress(p.read_bytes()))
    off = 512  # the first file's header, after the folder's
    _retar_header(tar, off, slice(off + 157, off + 257), BIG[:64])
    p.write_bytes(_gzip.compress(bytes(tar)))
    problems = audit_package.check_release("linux", p)
    assert any("linkname" in x for x in problems), problems


def test_r4_zip_extra_fields_and_entry_comments(tmp_path):
    top = "openbfme-v1-windows-x64"
    for kw in ({"extra": struct.pack("<HH", 0xCAFE, len(BIG)) + BIG}, {"comment": BIG}):
        buf = io.BytesIO()
        with zipfile.ZipFile(buf, "w") as z:
            info = zipfile.ZipInfo(f"{top}/README_TESTERS.txt")
            for k, v in kw.items():
                setattr(info, k, v)
            z.writestr(info, b"read me\n")
        _, _, problems = audit_package.read_release_zip(buf.getvalue(), "w.zip")
        assert any("extra fields or an entry comment" in p for p in problems), (kw.keys(), problems)


def test_r4_the_checksum_file_lists_every_archive_exactly_once(tmp_path):
    import hashlib
    names = ["openbfme-v1-linux-x64.tar.gz", "openbfme-v1-windows-x64.zip"]
    for n in names:
        (tmp_path / n).write_bytes(n.encode())
    line = {n: f"{hashlib.sha256(n.encode()).hexdigest()}  {n}\n" for n in names}
    sums = tmp_path / "SHA256SUMS-v1.txt"
    for text, why in (("", "lists no archive"), (line[names[0]], "is not listed"), (line[names[0]] * 2 + line[names[1]], "listed 2 times")):
        sums.write_text(text)
        problems = audit_package.check_release("linux", sums)
        assert any(why in p for p in problems), (why, problems)
    sums.write_text(line[names[0]] + line[names[1]])
    assert audit_package.check_release("linux", sums) == []
    assert audit_package.sums_problems("", sums, expected=names[:1])  # the archives of the run, not more


@pytest.mark.parametrize("ext", ["tar.gz", "zip"])
def test_archives_are_byte_for_byte_reproducible(tmp_path, ext):
    files = release_files()
    a = write_release(tmp_path / "a", files, top="openbfme-v1-linux-x64")
    b = write_release(tmp_path / "b", files, top="openbfme-v1-linux-x64")
    (b / "OpenBFME.x86_64").chmod(0o755)
    (a / "OpenBFME.x86_64").chmod(0o755)
    import os
    os.utime(b / "LICENSE", (1, 1))  # file times on disk do not matter
    out_a, out_b = tmp_path / f"a.{ext}", tmp_path / f"b.{ext}"
    getattr(make_archive, "tar_gz" if ext == "tar.gz" else "zip_file")(a, out_a, 1700000000)
    getattr(make_archive, "tar_gz" if ext == "tar.gz" else "zip_file")(b, out_b, 1700000000)
    assert out_a.read_bytes() == out_b.read_bytes()
    getattr(make_archive, "tar_gz" if ext == "tar.gz" else "zip_file")(b, out_b, 1700003600)  # ZIP dates have a 2 s resolution
    assert out_a.read_bytes() != out_b.read_bytes(), "the time stamp is the commit's"


def test_a_tampered_package_is_not_the_rebuild(tmp_path, capsys):
    """package.sh --verify rebuilds the package from the commit and compares it byte for byte; each of the review's r4 tampers (and any
    other) makes the archive differ from the rebuild. make_archive.py --diff names the layer."""
    d = write_release(tmp_path / "src", release_files())
    good = tmp_path / "good.tar.gz"
    make_archive.tar_gz(d, good, 1700000000)
    tar = bytearray(_gzip.decompress(good.read_bytes()))
    tampers = {}
    t1 = bytearray(tar)
    _retar_header(t1, 512, slice(512 + 157, 512 + 257), BIG[:64])
    tampers["linkname"] = bytes(t1)
    t2 = bytearray(tar)
    pos = t2.find(b"read me\n")
    t2[pos + 8:pos + 8 + len(BIG)] = BIG
    tampers["padding"] = bytes(t2)
    tampers["appended"] = bytes(tar) + BIG
    for why, raw in tampers.items():
        bad = tmp_path / f"{why}.tar.gz"
        bad.write_bytes(_gzip.compress(raw, mtime=1700000000))
        assert make_archive.diff(bad, good) == 1, why
    assert "DIFFERENT" in capsys.readouterr().out
    assert make_archive.diff(good, good) == 0


def tracked_project_files(project: Path) -> list[str]:
    output = subprocess.run(["git", "-C", str(project), "ls-files", "-z", "."], capture_output=True, text=True, check=True).stdout
    files = [f for f in output.split("\0") if f]
    assert files, f"git ls-files lists the {project.name} project"
    ignored = {Path(f).parent.as_posix() for f in files if Path(f).name == ".gdignore"}
    return [f for f in files if not any(p.startswith(".") for p in Path(f).parts)
            and not any(d == "." or f.startswith(d + "/") for d in ignored)]


def export_matches(path: str, patterns: list[str]) -> bool:
    # Godot's String.matchn is case insensitive and * spans folders.
    return any(fnmatch.fnmatchcase(path.lower(), p.removeprefix("res://").lower()) for p in patterns)


def exported_resource_paths(project: Path, path: str, binary: bool) -> list[str]:
    """Pack names, not resource bytes: no editor, import cache or game assets needed."""
    suffix = Path(path).suffix.lower()
    if path == "project.godot":
        return ["project.binary"]
    if binary and suffix in (".tscn", ".tres"):
        # Godot generates the preset id and export hash; the rules check their shape, not their value.
        ext = ".scn" if suffix == ".tscn" else ".res"
        return [path + ".remap", f".godot/exported/0/export-{'0' * 32}-{Path(path).stem}{ext}"]
    metadata = project / (path + ".import")
    if metadata.is_file():
        text = metadata.read_text(encoding="utf-8")
        if 'importer="keep"' in text:
            return [path]
        targets = re.findall(r'^path(?:\.\w+)?="res://([^"]+)"$', text, re.M)
        assert targets, f"{metadata}: model this importer's exported paths"
        return [path + ".import", *targets]
    imported = {".png": "ctex", ".jpg": "ctex", ".jpeg": "ctex", ".svg": "ctex", ".webp": "ctex", ".bmp": "ctex", ".tga": "ctex",
                ".ttf": "fontdata", ".otf": "fontdata", ".woff": "fontdata", ".woff2": "fontdata"}
    if suffix in imported:
        digest = hashlib.md5(("res://" + path).encode()).hexdigest()
        return [path + ".import", f".godot/imported/{Path(path).name}-{digest}.{imported[suffix]}"]
    return [path]


def assert_export_rules_cover_project(project: Path, rules: list) -> None:
    files = tracked_project_files(project)
    config = configparser.ConfigParser(interpolation=None)
    config.read_string((project / "export_presets.cfg").read_text(encoding="utf-8"))
    presets = [config[s] for s in config.sections() if re.fullmatch(r"preset\.\d+", s)]
    assert presets, f"{project}: export presets are present"
    settings = (project / "project.godot").read_text(encoding="utf-8")
    conversion = re.search(r'^export/convert_text_resources_to_binary=(true|false)$', settings, re.M)
    assert conversion, f"{project}: explicitly configure text resource conversion"
    skip: list[str] = []
    add: list[str] = []
    if project.name == "launcher":
        assert 'enabled=PackedStringArray("res://addons/pack_files/plugin.cfg")' in settings
        plugin = (project / "addons/pack_files/export_plugin.gd").read_text(encoding="utf-8")
        for key, patterns in (("SKIP", skip), ("ADD", add)):
            match = re.search(rf'^const {key}: PackedStringArray = \[(.*?)\]$', plugin, re.M)
            assert match, f"{project}: model the pack_files plugin's {key} patterns"
            patterns.extend(re.findall(r'"([^"]+)"', match[1]))
    # ResourceLoader/importer extensions, not an allowlist of shipping paths: a resource in tests/ must be checked too.
    resources = set("gd gdshader gdshaderinc gdextension tscn tres scn res png jpg jpeg svg webp bmp tga "
                    "ttf otf woff woff2 fnt font wav ogg mp3 gltf glb blend fbx obj dae mesh animation "
                    "json pub crt translation po mo csv pvr ktx hdr exr mp4 ogv".split())
    problems = set()
    for preset in presets:
        assert preset["export_filter"] == '"all_resources"', f"{project}: model the new export_filter"
        assert preset["script_export_mode"] == "0", f"{project}: model the new script export mode"
        include = [p.strip() for p in preset["include_filter"].strip('"').split(",") if p.strip()] + ["*.ico", "*.icns"]
        exclude = [p.strip() for p in preset["exclude_filter"].strip('"').split(",") if p.strip()]
        for path in files:
            resource = Path(path).suffix.lower().lstrip(".") in resources or path == "project.godot" or path + ".import" in files
            included = export_matches(path, include + add)
            if not (resource or included) or export_matches(path, exclude + skip):
                continue
            paths = exported_resource_paths(project, path, conversion[1] == "true") if resource else [path]
            for packed in paths:
                if not any(rx.match(packed) for rx, _ in rules):
                    problems.add(f"{project.name}/{path} -> {packed} ({preset['name']})")
    assert not problems, ("Exported files have no package audit rule; add a rule to PCK_RULES / LAUNCHER_PCK_RULES "
                          "or keep the file out of the export:\n" + "\n".join(sorted(problems)))


@pytest.mark.parametrize("project,rules", [
    ("godot", audit_package.PCK_RULES), ("launcher", audit_package.LAUNCHER_PCK_RULES),
], ids=["game", "launcher"])
def test_exported_files_have_pck_rules(project, rules):
    assert_export_rules_cover_project(REPO / project, rules)


def synthetic_export_project(tmp_path: Path, name: str, files: dict[str, str]) -> Path:
    project = tmp_path / name
    configs = ["export_presets.cfg", "project.godot"]
    if name == "launcher":
        configs.append("addons/pack_files/export_plugin.gd")
    for path in configs:
        files[path] = (REPO / name / path).read_text(encoding="utf-8")
    for path, content in files.items():
        target = project / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(content, encoding="utf-8")
    subprocess.run(["git", "init", "-q", str(tmp_path)], check=True)
    subprocess.run(["git", "-C", str(tmp_path), "add", name], check=True)
    return project


@pytest.mark.parametrize("name,path,rules", [
    ("godot", "tests/x.tres", audit_package.PCK_RULES),
    ("godot", "tests/x.png", audit_package.PCK_RULES),
    ("launcher", "scenes/x.tres", audit_package.LAUNCHER_PCK_RULES),
    ("launcher", "x.gd", audit_package.LAUNCHER_PCK_RULES),
    ("launcher", "fonts/New.ttf", audit_package.LAUNCHER_PCK_RULES),
])
def test_export_rules_reject_a_tracked_bogus_resource(tmp_path, name, path, rules):
    project = synthetic_export_project(tmp_path, name, {path: "synthetic resource\n"})
    with pytest.raises(AssertionError) as error:
        assert_export_rules_cover_project(project, rules)
    assert f"{name}/{path} ->" in str(error.value)
    assert "add a rule" in str(error.value) and "keep the file out of the export" in str(error.value)


@pytest.mark.parametrize("name,rules", [("godot", audit_package.PCK_RULES), ("launcher", audit_package.LAUNCHER_PCK_RULES)])
def test_export_rules_respect_ignored_folders_and_non_resources(tmp_path, name, rules):
    project = synthetic_export_project(tmp_path, name, {
        "ignored/.gdignore": "", "ignored/nested/x.tres": "synthetic\n", ".hidden/x.tres": "synthetic\n",
        "README.txt": "not included\n", "scripts/x.gd.uid": "uid://synthetic\n", "scripts/x.gd": "extends Node\n",
        "scenes/x.tscn": '[gd_scene format=3]\n[node name="Test" type="Node"]\n',
        **({"tests/x.tres": "skipped by the launcher plugin\n", "fonts/OFL-Test.txt": "synthetic licence\n"}
           if name == "launcher" else {"tests/x.txt": "included by the preset\n"}),
    })
    (project / "untracked.tres").write_text("not part of the tracked export\n", encoding="utf-8")
    assert_export_rules_cover_project(project, rules)


def test_export_resource_paths_model_imports_remaps_and_binary_scenes(tmp_path):
    assert exported_resource_paths(REPO / "godot", "assets/brand/poweredby_openbfme.png", False) == [
        "assets/brand/poweredby_openbfme.png.import",
        ".godot/imported/poweredby_openbfme.png-8e49593c1d11f096cae531ecedee93ce.ctex",
    ]
    assert exported_resource_paths(REPO / "launcher", "fonts/Inter.ttf", False) == ["fonts/Inter.ttf"]
    assert exported_resource_paths(tmp_path, "scenes/x.tscn", True) == [
        "scenes/x.tscn.remap", f".godot/exported/0/export-{'0' * 32}-x.scn",
    ]
    for path in exported_resource_paths(tmp_path, "scenes/x.tscn", True):
        assert any(rx.match(path) for rx, _ in audit_package.PCK_RULES), path
    assert exported_resource_paths(tmp_path, "scenes/x.tscn", False) == ["scenes/x.tscn"]


def test_export_rules_apply_case_insensitive_preset_filters(tmp_path):
    project = synthetic_export_project(tmp_path, "godot", {"other/nested/X.BIN": "synthetic\n"})
    presets = project / "export_presets.cfg"
    text = presets.read_text(encoding="utf-8").replace('include_filter="tests/*.txt"', 'include_filter="other/*.bin"')
    presets.write_text(text, encoding="utf-8")
    with pytest.raises(AssertionError, match="other/nested/X.BIN"):
        assert_export_rules_cover_project(project, audit_package.PCK_RULES)
    presets.write_text(text.replace('exclude_filter=""', 'exclude_filter="other/*.BIN"'), encoding="utf-8")
    assert_export_rules_cover_project(project, audit_package.PCK_RULES)


def test_game_test_scenes_still_need_valid_scene_text():
    path = "tests/online_social_node.tscn"
    assert audit_package.pck_release_problems("pack", pck([(path, b'[gd_scene format=3]\n')])) == []
    assert audit_package.pck_release_problems("pack", pck([(path, b'not a scene\n')])) == [
        f"pack:{path}: not a text scene ([gd_scene ...] header)",
    ]


def test_the_game_presets_filters_cannot_reorder_the_pack():
    """REPRO-1: Godot 4.7.2 applies a preset's include_filter / exclude_filter in the file system's directory order (ext4: a hash seeded per
    file system), on the set of paths whose order is the pack's. One included file and no exclusions keep godot/'s packages the same on
    every host; a second match (another tests/*.txt) or an exclusion would make them differ the way v0.3.0-preview.5's launchers did:
    pick such files in an export plugin in sorted order instead (launcher/addons/pack_files/export_plugin.gd)."""
    text = (REPO / "godot" / "export_presets.cfg").read_text(encoding="utf-8")
    # lane OWNERFIX-3: Godot's filter walk skips a folder that holds a .gdignore (assets/app_icon: the icons the Windows / macOS export reads by
    # path stay out of the pack); a launcher export with that folder stored no icon file
    files = tracked_project_files(REPO / "godot")
    for value in re.findall(r'^exclude_filter="(.*)"$', text, re.M):
        assert value == "", f"exclude_filter={value!r}: its erases follow the directory order"
    includes = re.findall(r'^include_filter="(.*)"$', text, re.M)
    assert len(includes) == 4, "Linux, Windows, universal macOS and Apple Silicon macOS presets"
    for value in includes:
        patterns = [p.strip().lower() for p in value.split(",") if p.strip()] + ["*.ico", "*.icns"]  # Godot adds the icons the same way
        matched = [f for f in files if any(fnmatch.fnmatchcase(f.lower(), p) for p in patterns)]  # Godot's matchn: * spans folders
        assert len(matched) <= 1, f"include_filter={value!r} matches {matched}: their order in the pack would follow the host"


@pytest.mark.parametrize("text,flagged", [
    # MACPLAY-1 (Sol r3): a synthetic image is not upstream bytes; even the paths the real templates carry are findings in it
    (b"\0/" + b"home/runner/.cargo/registry/src/index.crates.io-1949cf8c6b5b557f/accesskit-0.19.1/src/tree.rs\0", True),
    (b"\0/root/winrt/winrt/Windows.Foundation.h\0", True),
    (b"\0/" + b"home/runner/work/godot/godot/core/a.cpp\0", True),          # upstream's runner, but not the pinned crate sources
    (b"\0/" + b"home/runner/.cargo/registry/src/index.crates.io-0000000000000000/x.rs\0", True),
    (b"\0/root/build/x.cpp\0", True),
    (b"\0/" + b"home/builder/ci-work/src/x.cpp\0", True),             # a CI host's (built here: this file passes the commit gate)
    (b"\0/Users/someone/.cargo/registry/src/index.crates.io-1949cf8c6b5b557f/x.rs\0", True),
])
def test_only_the_official_templates_build_paths_are_exempt(text, flagged):
    """DEVPORT-6: LOCAL_PATH scans binaries since 7c731b971; the official Godot 4.7.2 templates carry their build host's paths (every
    exported executable is one), exempted by exact prefix (audit_package.UPSTREAM_TEMPLATE_PATHS) inside the pinned upstream section
    of a template slot (MACPLAY-1 Sol r3: test_the_template_exemption_is_upstream_bytes_only); anything else is still a finding"""
    assert (audit_package.developer_path_problem(loadable_elf(text), template_slot=True) == "developer-machine path") is flagged


CARGO = "/" + "home/runner/.cargo/registry/src/index.crates.io-1949cf8c6b5b557f/"


def loadable_elf(payload: bytes, machine: int = 62, e_type: int = 3) -> bytes:
    """an x86-64 shared object whose one loadable segment covers the whole file: header, program header, `payload`"""
    size = 64 + 56 + len(payload)
    h = bytearray(64)
    h[0:4] = b"\x7fELF"
    h[4], h[5], h[6] = 2, 1, 1
    struct.pack_into("<HHI", h, 16, e_type, machine, 1)
    struct.pack_into("<Q", h, 32, 64)                     # e_phoff
    struct.pack_into("<HHHH", h, 52, 64, 56, 1, 64)       # e_ehsize, e_phentsize, e_phnum, e_shentsize
    ph = struct.pack("<IIQQQQQQ", 1, 5, 0, 0, 0, size, size, 0x1000)
    return bytes(h) + ph + payload


@pytest.mark.parametrize("text, why", [
    # Sol r1 on DEVPORT-6 (P2): escapes out of an upstream prefix
    (b"\x7fELF\0/root/winrt/winrt/../../private/source.cpp\0", "developer-machine path"),
    (b"\x7fELF\0" + CARGO.encode() + b"../../../../builder/x.rs\0", "developer-machine path"),
    (b"\x7fELF\0/root/winrt/winrt/./../build/x.h\0", "developer-machine path"),
    (b"\x7fELF\0/root/winrt/winrt/a\\..\\..\\x.h\0", "developer-machine path"),
    # an upstream prefix outside an executable (a pack's script, a text file): no exemption
    (b"extends Node\n# build source: /root/winrt/winrt/Windows.Foundation.h\n", "developer-machine path"),
    # a forbidden build prefix below an upstream one: checked on the original text
    (b"\x7fELF\0/root/winrt/winrt/stage-7/x.h\0", "exact checkout/build/stage path"),
    # a local path after an exempt one in the same blob is still found
    (b"\x7fELF\0/root/winrt/winrt/a.h\0/root/private/b.cpp\0", "developer-machine path"),
])
def test_template_exemption_has_no_escapes(text, why):
    """the exemption covers whole upstream paths inside executables only: '..' / '.' / backslash escapes, the prefix in non-executable
    content, a forbidden prefix below an upstream one and a second path beside an exempt one are all findings"""
    blob = loadable_elf(text[4:]) if text.startswith(b"\x7fELF") else text
    assert audit_package.developer_path_problem(blob, ("/root/winrt/winrt/stage-7",), template_slot=True) == why


def test_template_exemption_escape_inside_a_game_pack_is_found():
    """the reviewer's reproduction: the escaping path in a script of an otherwise valid game pack"""
    for path in ["/root/private/source.cpp", "/root/winrt/winrt/../../private/source.cpp", "/root/winrt/winrt/Windows.Foundation.h"]:
        pack = pck([("scripts/game.gd", ("extends Node\n# build source: " + path + "\n").encode())])
        assert audit_package.pck_release_problems("OpenBFME.pck", pack) == ["OpenBFME.pck:scripts/game.gd: developer-machine path"], path


def _template_homes() -> list[Path]:
    """where Godot 4.7.2's official templates are installed: Godot's own data folder ($XDG_DATA_HOME, which the CI hosts link to their
    template copy), then the default one"""
    homes = [Path(x) / "godot/export_templates/4.7.2.stable" for x in [os.environ.get("XDG_DATA_HOME", "")] if x]
    return homes + [Path.home() / ".local/share/godot/export_templates/4.7.2.stable"]


def test_the_official_export_templates_pass_the_path_audit():
    """the real x86-64 Linux / Windows templates the releases export with (SKIP LOUDLY where none is installed)"""
    homes = _template_homes()
    files = [p for h in homes if h.is_dir() for p in sorted(h.iterdir())
             if p.name.startswith(("linux_", "windows_")) and "x86_64" in p.name and p.is_file()]   # the release presets' (x86-64 only;
    # a host with every template also has arm64 Windows ones, whose build paths no release carries)
    if not files:
        pytest.skip("SKIP LOUDLY: no Godot 4.7.2 Linux / Windows export template here")
    assert {p.name: audit_package.developer_path_problem(p.read_bytes(), template_slot=True) for p in files} == {p.name: "" for p in files}


def _official_template(name: str) -> bytes:
    for home in _template_homes():
        if (home / name).is_file():
            return (home / name).read_bytes()
    pytest.skip(f"SKIP LOUDLY: no Godot 4.7.2 export template {name} here")


def _pinned_section(data: bytes) -> tuple[int, int]:
    found = [(off, size) for fmt, name, off, size in audit_package._sections(data)
             if (fmt, name, size) in audit_package.UPSTREAM_TEMPLATE_SECTIONS]
    assert len(found) == 1, found
    return found[0]


def _upstream_path_at(data: bytes, prefix: bytes) -> tuple[int, int]:
    off, size = _pinned_section(data)
    at = data.index(prefix, off, off + size)
    return at, data.index(b"\0", at)


TEMPLATES = ["linux_debug.x86_64", "linux_release.x86_64", "windows_debug_x86_64.exe", "windows_release_x86_64.exe"]


@pytest.mark.parametrize("template", TEMPLATES)
def test_the_template_exemption_is_upstream_bytes_only(template: str):
    """MACPLAY-1 (Sol r3, P2): the exemption needs the template's own read-only data section (pinned by SHA-256 from the official
    Godot 4.7.2 templates) in a slot an exported template fills. Each mutation below keeps a parseable image of the right format and
    is a finding; the untouched template in each of its slots is not (the control). Expected values: the pinned upstream hashes and
    paths the test writes, never the audit's own output."""
    data = _official_template(template)
    prefix = audit_package.UPSTREAM_TEMPLATE_PATHS[1 if template.startswith("windows") else 0].encode()   # the whole upstream prefix
    slots = ["OpenBFME.exe", "OpenBFME.console.exe", "OpenBFMELauncher.exe"] if template.startswith("windows") else \
        ["OpenBFME.x86_64", "OpenBFMELauncher.x86_64"]
    assert set(slots) <= audit_package.UPSTREAM_TEMPLATE_SLOTS
    assert all(audit_package.is_template_slot(f"pkg.zip:top/{slot}") for slot in slots)
    audit = audit_package.Audit()
    audit.blob(f"pkg/{slots[0]}", data)
    assert [p for p in audit.problems if "path" in p] == [], audit.problems
    # the same untouched bytes in a project-built slot (the swap helper; the GDExtensions and the macOS executables are no template slot
    # either) and through the bare call without a slot
    audit = audit_package.Audit()
    audit.blob("pkg/openbfme-bundle-swap", data)
    assert "developer-machine path in pkg/openbfme-bundle-swap" in audit.problems, audit.problems
    assert not any(audit_package.is_template_slot(f"top/{slot}") for slot in [
        "openbfme.linux.template_debug.x86_64.so", "openbfme.windows.template_debug.x86_64.dll", "openbfme-bundle-swap",
        "openbfme.macos.template_debug.arm64.dylib", "openbfme.macos.template_debug.universal.dylib", "OpenBFME", "OpenBFME Launcher",
        "OpenBFME.pck", "README.txt", "OpenBFME.exe:scripts/game.gd"])
    assert audit_package.developer_path_problem(data) == "developer-machine path"
    off, size = _pinned_section(data)
    a, b = _upstream_path_at(data, prefix)
    private = (prefix + b"private-source.cpp").ljust(b - a, b"\0")[:b - a]
    mutations = {
        # a private path written over an upstream one, inside the upstream section (the section no longer hashes as pinned)
        "private path in the section": data[:a] + private + data[b:],
        # one byte of the section changed far from any path: the upstream paths themselves lose their exemption
        "section byte changed": data[:off] + bytes([data[off] ^ 1]) + data[off + 1:],
        # the private path appended after the image (an upstream path outside the pinned section)
        "path after the image": data + b"\0" + prefix + b"private-source.cpp\0",
    }
    for why, blob in mutations.items():
        assert audit_package.developer_path_problem(blob, template_slot=True) == "developer-machine path", why


# ---- PUBLISH-2 r3 (RELHOST-1: v0.4.7's package failed the allowlist): the pinned platform icons' two pack entries, by exact path. Merge
# queue q21: DEVPORT-6 pins the same six entries by their exported bytes (PINNED_GAME_ICONS, measured on Linux x64 and the M5), which
# PUBLISH-2's structural rules duplicated; the hash pins are kept and these cases run against them -------------------------------------
def test_the_pinned_icons_pass_and_nothing_else_does():
    icons = sorted(audit_package.pinned_icons())
    assert icons == ["linux.png", "macos.png", "windows.png"], icons
    # the paths Godot 4.7.2's export wrote into v0.4.7's pack (RELHOST-1's failing run listed exactly these six)
    paths = [p for n in icons for p in audit_package.icon_import_paths(n)]
    assert paths == [
        "assets/platform_icons/linux.png.import", ".godot/imported/linux.png-e3b3f6dbbb74c608680942a1b07c4593.ctex",
        "assets/platform_icons/macos.png.import", ".godot/imported/macos.png-40b991ba36498511f07038e54095534b.ctex",
        "assets/platform_icons/windows.png.import", ".godot/imported/windows.png-d254cfae9b851ca9c851d4089a8e9ea3.ctex"]
    assert sorted(paths) == sorted(audit_package.PINNED_GAME_ICONS)
    # the entries after an icon are named under their own pack (the icon rule once renamed the pack in later problem lines)
    tex = paths[1]
    assert audit_package.pck_release_problems("OpenBFME.pck", pck([(tex, b"GST2"), ("assets/music.bin", b"x")])) == [
        "OpenBFME.pck:.godot/imported/linux.png-e3b3f6dbbb74c608680942a1b07c4593.ctex: not the hash-pinned platform icon resource",
        "OpenBFME.pck:assets/music.bin: not a resource type this project exports"]
    other = ".godot/imported/other.png-" + hashlib.md5(b"res://assets/platform_icons/other.png").hexdigest() + ".ctex"
    for entries, why in [
        ([(other, b"GST2")], "not a resource type"),                                      # an unpinned texture
        ([("assets/platform_icons/other.png.import", b"[remap]\n")], "not a resource type"),  # an unpinned import file
        ([(tex.replace("e3b3", "e3b4"), b"GST2")], "not a resource type"),               # the pinned name under another hash
        ([("assets/linux.png.import", b"[remap]\n")], "not a resource type"),            # the pinned name in another folder
        ([(tex + ".bak", b"GST2")], "not a resource type"),
        ([(paths[0], b"[remap]\n")], "not the hash-pinned platform icon resource"),      # a pinned path with other bytes
    ]:
        probs = audit_package.pck_release_problems("p", pck(entries))
        assert any(why in p for p in probs), (entries[0][0], probs)


def test_the_real_release_package_passes_the_allowlist():
    """the package package.sh built on a CI host (OPENBFME_RELEASE_PACKAGE: its .tar.gz or .zip; the check runs where one is given)"""
    import os  # noqa: PLC0415
    path = os.environ.get("OPENBFME_RELEASE_PACKAGE")
    if not path:
        pytest.skip("SKIP LOUDLY: OPENBFME_RELEASE_PACKAGE is not set (a package.sh archive of this commit)")
    platform = "linux" if path.endswith(".tar.gz") else "windows"
    assert audit_package.check_release(platform, Path(path)) == []


# ---- community PR #36: the files of the AIO download (the optional BFME1 package included) never ship in our packages ----------------

def _aio_table(tmp_path: Path, monkeypatch, files: dict[str, bytes]) -> None:
    """a made-up aio_package_files.json pinning `files` (name -> bytes) in a package original-BFME1, for the audit"""
    import hashlib
    t = tmp_path / "aio_package_files.json"
    t.write_text(json.dumps({"original-BFME1": [[n, hashlib.md5(d).hexdigest(), len(d)] for n, d in files.items()]}))
    monkeypatch.setattr(audit_package, "AIO_FILES", t)


def test_the_real_aio_table_holds_every_pinned_file_of_the_three_packages():
    """the audit reads the launcher's own pins: the base RotWK / BFME2 download and the optional BFME1 package, e.g. lotrbfme.exe,
    worldbuilder.exe and the DSOAL dsound.dll (looked up by the pins, independently of the audit's table)"""
    table = json.loads((HERE / "aio_package_files.json").read_text())
    pinned = audit_package.aio_pinned(audit_package.AIO_FILES)
    assert set(table) == {"original-RotWK", "original-BFME2", "original-BFME1"} and len(table["original-BFME1"]) == 346
    for guid, rows in table.items():
        for name, md5, size in rows:
            if size > 0:
                assert (md5, size) in pinned, (guid, name)  # a file two packages share is named after one of them
    assert {"original-BFME1: lotrbfme.exe", "original-BFME1: worldbuilder.exe", "original-BFME1: dsound.dll"} <= set(pinned.values())


def test_a_pinned_aio_file_is_refused_under_any_name_and_in_any_container(tmp_path, monkeypatch):
    exe = b"MZ" + bytes(range(256)) * 40          # stands in for lotrbfme.exe: not retail-format bytes, an ordinary name
    _aio_table(tmp_path, monkeypatch, {"lotrbfme.exe": exe})
    zbuf = io.BytesIO()
    with zipfile.ZipFile(zbuf, "w") as z:
        z.writestr("pkg/tool.dll", exe)
    problems = audit(tmp_path, {"README_TESTERS.txt": b"hello", "helper.exe": exe, "b.zip": zbuf.getvalue(),
                                "OpenBFME.pck": pck([("res://a.bin", exe)])})
    for where in ("helper.exe", "b.zip:pkg/tool.dll", "OpenBFME.pck:res://a.bin"):
        assert any(p.endswith(where) and "original-BFME1: lotrbfme.exe" in p for p in problems), (where, problems)
    assert len(problems) == 3, problems
    # one byte different, or one byte longer: not that file (no false positives on our own files)
    assert audit(tmp_path / "other", {"helper.exe": exe[:-1] + b"\x01", "longer.exe": exe + b"\x00"}) == []
    # the release allowlist too: a pinned file under an allowed name, and the same file as an extra
    problems = release_problems(tmp_path / "rel", NOTICE=exe)
    assert any("NOTICE: a game file the AIO service distributes (original-BFME1: lotrbfme.exe)" in p for p in problems), problems
    problems = release_problems(tmp_path / "rel2", **{"lotrbfme.exe": exe})
    assert any("lotrbfme.exe: a game file the AIO service distributes" in p for p in problems), problems
    problems = audit_package.pck_release_problems("OpenBFME.pck", pck([("scripts/x.gd", exe)]))
    assert any("a game file the AIO service distributes" in p for p in problems), problems


@pytest.mark.parametrize("content", ["", "{}", '{"original-BFME1": []}', "not json"])
def test_the_aio_table_fails_closed(tmp_path, monkeypatch, content):
    t = tmp_path / "aio_package_files.json"
    t.write_text(content)
    monkeypatch.setattr(audit_package, "AIO_FILES", t)
    with pytest.raises(SystemExit, match="AUDIT"):
        audit_package.aio_file(b"x")
    monkeypatch.setattr(audit_package, "AIO_FILES", tmp_path / "missing.json")
    with pytest.raises(SystemExit, match="AUDIT"):
        audit_package.aio_file(b"x")
