"""Lane WINCRASH-1: the PDB next to the Windows GDExtension (tools/release/pdb_tools.py), its place in the release allowlist, the testers'
README line on the log folder."""
from __future__ import annotations

import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
sys.path.insert(0, str(HERE))

import audit_package  # noqa: E402
import pdb_tools  # noqa: E402
import readme_testers  # noqa: E402

GUID = bytes(range(16))
HOME = b"/home" b"/alice/build-ci/llvm-mingw/lib/libc++.a"  # split: tools/precommit.py rejects a home path in a committed file


def msf(streams: list[bytes], block_size: int = 512) -> bytes:
    """An MSF 7.00 file with these streams, laid out as lld writes one: superblock, the two free page map blocks (the first active: every
    block in use, the bits past the last block free; the second all 0xFF), the stream blocks, the directory, its block map."""
    blocks: list[bytes] = [b"", b"", b"\xff" * block_size]
    lists = []
    for s in streams:
        n = (len(s) + block_size - 1) // block_size
        lists.append(list(range(len(blocks), len(blocks) + n)))
        blocks += [s[i * block_size:(i + 1) * block_size].ljust(block_size, b"\0") for i in range(n)]
    directory = struct.pack("<I", len(streams)) + b"".join(struct.pack("<I", len(s)) for s in streams)
    directory += b"".join(struct.pack(f"<{len(lst)}I", *lst) for lst in lists)
    n_dir = (len(directory) + block_size - 1) // block_size
    dir_blocks = list(range(len(blocks), len(blocks) + n_dir))
    blocks += [directory[i * block_size:(i + 1) * block_size].ljust(block_size, b"\0") for i in range(n_dir)]
    block_map = len(blocks)
    blocks.append(struct.pack(f"<{n_dir}I", *dir_blocks).ljust(block_size, b"\0"))
    count = len(blocks)
    fpm = bytearray(b"\xff" * block_size)
    for i in range(count):
        fpm[i // 8] &= ~(1 << (i % 8)) & 0xFF
    blocks[1] = bytes(fpm)
    blocks[0] = (pdb_tools.MSF_MAGIC + struct.pack("<6I", block_size, 1, count, len(directory), 0, block_map)).ljust(block_size, b"\0")
    return b"".join(blocks)


def string_table(strings: list[bytes]) -> bytes:
    """A /names stream: signature, version 1, the NUL-separated strings (offset 0 the empty string), a one-bucket hash, the count."""
    buffer = b"\0" + b"".join(s + b"\0" for s in strings)
    return struct.pack("<III", 0xEFFEEFFE, 1, len(buffer)) + buffer + struct.pack("<II", 1, 0) + struct.pack("<I", len(strings))


def pdb(guid: bytes = GUID, age: int = 1, extra: bytes = b"", named: bool = True, link_info: bytes = b"", stream0: bytes = b"",
        info_tail: bytes = b"") -> bytes:
    """A PDB shaped as lld writes one: the info stream (named streams /LinkInfo = stream 5, /names = stream 6; a 0 and the VC140 feature
    code after the map), an empty TPI / IPI, a DBI header naming no stream. `extra` is a string of /names (`named`) or a stream nothing
    names (an unknown payload); `link_info`, `stream0` and `info_tail` put bytes where Sol r2 hid a payload."""
    names = b"/LinkInfo\0/names\0"
    table = struct.pack("<II", 2, 4) + struct.pack("<II", 1, 0b0101) + struct.pack("<I", 0) + struct.pack("<IIII", 0, 5, 10, 6)
    info = struct.pack("<3I", 20000404, 0, age) + guid + struct.pack("<I", len(names)) + names + table + struct.pack("<II", 0, 20140508)
    dbi = struct.pack("<iIIHHHHHH", -1, 19990903, age, 0xFFFF, 0, 0xFFFF, 0, 0xFFFF, 0) + b"\0" * 40
    strings = [extra] if extra and named else []
    streams = [stream0, info + info_tail, b"", dbi, b"", link_info, string_table(strings)]
    return msf(streams + ([extra] if extra and not named else []))


def pe(guid: bytes = GUID, age: int = 1, name: bytes = b"openbfme.windows.template_debug.x86_64.pdb", extra: bytes = b"") -> bytes:
    """A PE32+ image with one section holding the debug directory and its CodeView (RSDS) record."""
    rsds = b"RSDS" + guid + struct.pack("<I", age) + name + b"\0"
    section = struct.pack("<IIHHIIII", 0, 0, 0, 0, 2, len(rsds), 0x1000 + 28, 0x200 + 28) + rsds + extra
    section = section.ljust(0x200, b"\0")
    dos = b"MZ".ljust(0x3C, b"\0") + struct.pack("<I", 0x40)
    coff = b"PE\0\0" + struct.pack("<HHIIIHH", 0x8664, 1, 0, 0, 0, 240, 0x2022)
    opt = bytearray(240)
    struct.pack_into("<H", opt, 0, 0x20B)
    struct.pack_into("<I", opt, 108, 16)  # NumberOfRvaAndSizes
    struct.pack_into("<II", opt, 112 + 6 * 8, 0x1000, 28)  # the debug directory
    sec = b".rdata\0\0" + struct.pack("<IIIIIIHHI", 0x200, 0x1000, 0x200, 0x200, 0, 0, 0, 0, 0x40000040)
    head = (dos + coff + bytes(opt) + sec).ljust(0x200, b"\0")
    return head + section


def test_a_dll_and_its_pdb_match_and_carry_no_home_folder():
    assert pdb_tools.pe_codeview(pe()) == (GUID, 1, "openbfme.windows.template_debug.x86_64.pdb")
    assert pdb_tools.pdb_identity(pdb()) == (GUID, 1)
    assert pdb_tools.check(pe(), pdb()) == []


def test_a_pdb_of_another_build_a_folder_in_the_dll_or_a_home_folder_is_a_problem():
    assert any("is not the DLL's" in p for p in pdb_tools.check(pe(), pdb(guid=bytes(16))))
    assert any("is not the DLL's" in p for p in pdb_tools.check(pe(), pdb(age=2)))
    assert any("with a folder" in p for p in pdb_tools.check(pe(name=b"C:/build/openbfme.pdb"), pdb()))
    assert any("PDB holds home folder paths" in p for p in pdb_tools.check(pe(), pdb(extra=HOME)))
    assert any("DLL holds home folder paths" in p for p in pdb_tools.check(pe(extra=HOME), pdb()))
    assert any("not an MSF" in p for p in pdb_tools.check(pe(), b"not a pdb"))


def test_scrub_blanks_the_whole_home_prefix_and_keeps_the_file_valid():
    raw = pdb(extra=HOME + b"\0" + b"C:\\Users\\Alice\\x.obj\0" + b"/home" b"/runner/work/llvm-mingw/libcxxabi/src/a.cpp\0")
    out = pdb_tools.scrub(raw)
    assert len(out) == len(raw)
    assert pdb_tools.pdb_identity(out) == (GUID, 1)
    assert pdb_tools.home_paths(out) == []
    assert b"___________/build-ci" in out and b"______________\\x.obj" in out  # neither the home prefix nor the user folder prefix is left
    assert b"/home/" + b"_" not in out and b"Users" not in out
    assert b"/runner/work/llvm-mingw/" in out  # the toolchain's own build path stays: nobody's home
    assert pdb_tools.check(pe(), out) == []
    assert pdb_tools.scrub(out) == out


def test_a_home_prefix_with_a_blanked_user_name_still_fails_the_check():
    """Sol r1: round 1's scrub left "/home/_____/..." (the absolute layout of the builder's home)."""
    blanked = b"/home" b"/_____/build-ci/llvm-mingw/lib/libc++.a"
    assert any("PDB holds home folder paths" in p for p in pdb_tools.check(pe(), pdb(extra=blanked)))


def test_scrub_dll_makes_the_toolchain_build_root_relative_and_the_dll_passes_the_package_audit():
    """Lane RELFIX-1: v0.4.7's package audit refused the DLL for the llvm-mingw libc++abi's own __FILE__ paths (in every release DLL)."""
    toolchain = b"/home" b"/runner/work/llvm-mingw/llvm-mingw/llvm-project/libcxxabi/src/cxa_personality.cpp\0"
    raw = pe(extra=b"\0" + toolchain + b"x\0" + toolchain)
    assert any("DLL holds home folder paths" in p for p in pdb_tools.check(raw, pdb()))  # the toolchain's path is no exemption in a DLL
    assert audit_package.developer_path_problem(raw) == "developer-machine path"
    out = pdb_tools.scrub_dll(raw)
    assert len(out) == len(raw) and out.count(b"./" * 9 + b"llvm-mingw/llvm-mingw/llvm-project/libcxxabi/src/cxa_personality.cpp\0") == 2
    assert out.replace(b"./" * 9 + b"llvm-mingw/", b"/home" b"/runner/work/llvm-mingw/") == raw  # nothing else changes
    assert pdb_tools.pe_codeview(out) == pdb_tools.pe_codeview(raw)
    assert pdb_tools.check(out, pdb()) == []
    assert audit_package.developer_path_problem(out) == ""
    assert pdb_tools.scrub_dll(out) == out
    # any other home folder in the DLL stays and fails both checks
    other = pdb_tools.scrub_dll(pe(extra=b"\0" + HOME + b"\0"))
    assert any("DLL holds home folder paths" in p for p in pdb_tools.check(other, pdb()))
    assert audit_package.developer_path_problem(other) == "developer-machine path"


def test_the_release_builds_audit_their_libraries():
    """Lane RELFIX-1: a build path in the GDExtension fails its build, not the release's packaging step."""
    windows = (HERE / "build_windows.sh").read_text()
    assert windows.index('pdb_tools.py" scrub-dll "$DLL"') < windows.index('pdb_tools.py" check "$DLL"') < windows.index('audit_package.py" "$DLL"')
    linux = (HERE / "build_linux.sh").read_text()
    assert linux.index('audit_package.py" "$SO"') < linux.index('echo "BUILT: $SO')


def test_debug_symbols_never_ship():
    """Stop S-1923: the PDB is private. The package audit refuses a .pdb by name and MSF bytes by content (also inside a ZIP, inside a pack
    entry, under another name); the commit gate does the same; the release allowlist has no .pdb; the gdextension exports none."""
    import io  # noqa: PLC0415
    import zipfile  # noqa: PLC0415
    sys.path.insert(0, str(REPO / "tools"))
    import precommit  # noqa: PLC0415
    raw = pdb()
    assert precommit.private_symbols(raw) and precommit.private_symbols(b"x" * 100 + raw)
    assert not precommit.private_symbols(b"Microsoft C/C++ MSF 7.00 in prose")
    assert ".pdb" in precommit.PRIVATE_SUFFIXES

    def audit(files: dict[str, bytes]) -> list[str]:
        a = audit_package.Audit()
        for name, data in files.items():
            a.blob(name, data)
        return a.problems

    assert any("debug symbols (PDB)" in p for p in audit({"openbfme.windows.template_debug.x86_64.pdb": b"not even a pdb"}))
    assert any("MSF / PDB bytes" in p for p in audit({"notes.txt": raw}))
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w") as z:
        z.writestr("bin/readme.bin", raw)
    assert any("MSF / PDB bytes" in p for p in audit({"extra.zip": buf.getvalue()}))
    assert audit_package.embedded_signatures(b"pack entry " + raw) == ["PDB / MSF debug symbols"]


def test_the_windows_package_does_not_ship_the_pdb_yet():
    """No in-game reader (Godot's handler symbolizes its executable only, stop S-1923): the PDB stays in the symbols archive."""
    assert "openbfme.windows.template_debug.x86_64.pdb" not in audit_package.RELEASE_FILES["windows"]
    assert "[dependencies]" not in (REPO / "godot/openbfme.gdextension").read_text()
    files = {"openbfme.windows.template_debug.x86_64.pdb": pdb()}
    assert any(".pdb is not a file of the release" in p for p in audit_package.release_file_problems("windows", files, "pkg"))


def test_the_readme_says_how_to_open_the_log_folder():
    lines, _ = readme_testers.known_issues(HERE / "known_issues.json", REPO / "docs/STOPS.md")
    assert "OpenBFME.exe --open-logs" in readme_testers.render("v9", "windows", lines)
    assert "./OpenBFME.x86_64 --open-logs" in readme_testers.render("v9", "linux", lines)
    # VERSION-1: a plain release's readme says "Alpha 0.4.5"; anything else keeps the tag
    first = readme_testers.render("v0.4.5", "linux", lines).splitlines()[:2]
    assert first == ["OpenBFME Alpha 0.4.5 for Linux x86-64", "=" * len("OpenBFME Alpha 0.4.5 for Linux x86-64")], first
    assert readme_testers.render("v0.4.0-alpha.4", "windows", lines).startswith("OpenBFME v0.4.0-alpha.4 - alpha build for Windows x86-64\n")
    assert readme_testers.render("v0.4.05", "linux", lines).startswith("OpenBFME v0.4.05 - alpha build for Linux x86-64\n")
    for platform in ("windows", "linux"):
        assert '"Logs" next to the version' in readme_testers.render("v9", platform, lines)


def test_the_session_log_names_stop_s1923_on_windows():
    """Stop S-1923 is reported at run time: release.gd prints it into every Windows session log, and the row exists."""
    release = (REPO / "godot/scripts/release/release.gd").read_text()
    at = release.index('if OS.get_name() == "Windows":')
    message = release[at:at + 1200]
    # DEVPORT-6: since win-crash-1 (WindowsCrashDump: a local minidump of a crash on any thread) the stop is the offset-only naming
    assert 'print("RELEASE [S-1923]' in message and "dll+<offset>" in message and "minidump" in message and "every thread" in message
    assert "| S-1923 | code |" in (REPO / "docs/STOPS.md").read_text()


def test_same_build_ignores_only_the_codeview_guid():
    """MACACT-1: the build host's and GitHub Actions' DLL of one commit differ in the CodeView GUID only (lld hashes the PDB, which holds
    the build folder's absolute path); any other byte, the size, the age or the PDB name is a different build"""
    a = pe(guid=b"\x01" * 16, extra=b"code")
    assert pdb_tools.same_build(a, pe(guid=b"\x02" * 16, extra=b"code")) == []
    assert pdb_tools.same_build(a, a) == []
    assert "differ outside the CodeView GUID" in pdb_tools.same_build(a, pe(guid=b"\x02" * 16, extra=b"cod3"))[0]
    assert "differ outside the CodeView GUID" in pdb_tools.same_build(a, pe(guid=b"\x01" * 16, age=2, extra=b"code"))[0]
    assert "the sizes differ" in pdb_tools.same_build(a, a + b"\0")[0]
    assert "not a PE image" in pdb_tools.same_build(a, b"MZ not a pe")[0]
