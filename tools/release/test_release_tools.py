"""Lane RELEASE-1: the package audit, the testers' README and the privacy rule "no network calls besides LAN play"."""
from __future__ import annotations

import io
import re
import struct
import sys
import tarfile
import zipfile
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
sys.path.insert(0, str(HERE))

import audit_package  # noqa: E402
import readme_testers  # noqa: E402

BIG = b"BIGF" + b"\x00" * 60
DDS = b"DDS " + b"\x00" * 60
WAV = b"RIFF\x00\x00\x00\x00WAVEfmt " + b"\x00" * 40


def pck(entries: list[tuple[str, bytes]], fmt: int = 4) -> bytes:
    """A Godot 4 pack in the layout audit_package reads (header, files, directory at the end for format 3)."""
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
        directory += struct.pack("<I", len(p)) + p + struct.pack("<QQ", ofs, len(data)) + b"\0" * 16 + struct.pack("<I", 0)
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
        directory += struct.pack("<I", len(p)) + p + struct.pack("<QQ", base + ofs, len(data)) + b"\0" * 16 + struct.pack("<I", 0)
    head = b"GDPC" + struct.pack("<5I", 2, 4, 3, 0, 0) + struct.pack("<Q", 0) + b"\0" * 64
    return head + directory + blobs


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


def test_known_issue_naming_a_vanished_stop_fails(tmp_path):
    issues = tmp_path / "issues.json"
    issues.write_text('{"issues": [{"stops": ["S-9999999"], "text": "gone"}]}')
    _, errors = readme_testers.known_issues(issues, REPO / "docs/STOPS.md")
    assert errors and "S-9999999" in errors[0]


# ---- review r1: containers are found by their bytes, not their names ---------------------------------------------------------------------

def tar_bytes(members: dict[str, bytes], compression: str = "") -> bytes:
    buf = io.BytesIO()
    with tarfile.open(fileobj=buf, mode="w:" + compression) as t:
        for n, data in members.items():
            info = tarfile.TarInfo(n)
            info.size = len(data)
            t.addfile(info, io.BytesIO(data))
    return buf.getvalue()


def zip_bytes(members: dict[str, bytes]) -> bytes:
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED) as z:
        for n, data in members.items():
            z.writestr(n, data)
    return buf.getvalue()


@pytest.mark.parametrize("name,blob", [
    ("notes.txt", zip_bytes({"art/x.bin": DDS})),                        # a renamed ZIP
    ("data.bin", tar_bytes({"pkg/y.bin": BIG}, "gz")),                   # a renamed .tar.gz
    ("data.bin", tar_bytes({"pkg/y.bin": BIG}, "bz2")),                  # a renamed .tar.bz2
    ("data.bin", tar_bytes({"pkg/y.bin": BIG}, "xz")),                   # a renamed .tar.xz
    ("plain.dat2", tar_bytes({"y.bin": WAV})),                           # an uncompressed tar
    ("stream.res", __import__("gzip").compress(BIG)),                    # a gzip stream of a raw archive
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
    (GameNetwork/Transport.cpp), nothing else talks to the network."""
    found = {f: v for f, v in net_guard.scan().items() if f.endswith(".gd")}
    assert found == {}


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


def release_files(**override) -> dict:
    files = {"OpenBFME.x86_64": elf64(), "openbfme.linux.template_debug.x86_64.so": elf64(),
             "OpenBFME.pck": pck([("scripts/game.gd", b"extends Node\n"), ("project.binary", b"ECFG" + struct.pack("<I", 0))]),
             "README_TESTERS.txt": b"read me\n", "LICENSE": b"GPL\n", "NOTICE": b"notice\n", "VERSION": b"OpenBFME v1\n"}
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
    ({"OpenBFME.pck": pck([("scripts/x.gd", b"x" * (300 << 10))])}, "bytes of text"),
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
