#!/usr/bin/env python3
"""Lane WINCRASH-1: the PDB of the Windows GDExtension. It is PRIVATE (stop S-1923): archive_symbols.sh keeps it in the builder's local
symbol store to map a crash report's DLL offsets to functions and lines; it is never packaged or committed (audit_package.py and
tools/precommit.py refuse a .pdb and any MSF bytes). So its content is not validated beyond these sanity checks: it is the DLL's own
(CodeView GUID + age) and holds no home folder path (the scrub below), so an archived file stays matched and free of the builder's name.

    python3 tools/release/pdb_tools.py scrub <file.pdb>          blank every home folder prefix in the PDB (in place)
    python3 tools/release/pdb_tools.py check <file.dll> <file.pdb>  the PDB is the DLL's (CodeView GUID + age), no home folder path

The compiler side keeps build paths out of the debug info (engine/CMakeLists.txt: prefix maps, no command lines), but lld still records
the toolchain's library archives, the linker's own path and its command line with their absolute paths. `scrub` replaces the whole home
folder prefix (/home/<user>, /Users/<user>, C:\\Users\\<user>) with as many underscores (Sol r1: a "/home/____" prefix left the absolute
layout): the strings keep their length, so every offset of the MSF file stays valid (DbgHelp maps addresses to functions and lines without
those strings). `check` refuses any home folder prefix, a blanked user name included.
"""
from __future__ import annotations

import re
import struct
import sys
from pathlib import Path

MSF_MAGIC = b"Microsoft C/C++ MSF 7.00\r\n\x1aDS\x00\x00\x00"
# a home folder and its user-name segment (group 2); the forms of tools/precommit.py MACHINE_PATH
HOME_PATHS = [re.compile(rb"(/home/)([^/\\\x00\s\"']+)"), re.compile(rb"(/Users/)([^/\\\x00\s\"']+)"),
              re.compile(rb"([A-Za-z]:[\\/]+Users[\\/]+)([^/\\\x00\s\"']+)", re.IGNORECASE)]
# the pinned llvm-mingw release's prebuilt libc++abi carries the __FILE__ paths of its GitHub Actions build (the runner's, nobody's home)
TOOLCHAIN_PATHS = (b"/home" b"/runner/work/llvm-mingw/",)  # split: tools/precommit.py rejects a home path in a committed file


def msf_streams(data: bytes) -> list[bytes]:
    """The streams of an MSF 7.00 (PDB) file. ValueError when it is not one or its directory is broken."""
    if not data.startswith(MSF_MAGIC):
        raise ValueError("not an MSF 7.00 (PDB) file")
    block_size, _fpm, num_blocks, dir_bytes, _unknown, block_map = struct.unpack_from("<6I", data, len(MSF_MAGIC))
    if block_size not in (512, 1024, 2048, 4096, 8192, 16384, 32768) or num_blocks * block_size != len(data):
        raise ValueError(f"MSF block size {block_size} x {num_blocks} blocks does not match the file size {len(data)}")

    def block(i: int) -> bytes:
        if i >= num_blocks:
            raise ValueError(f"MSF block {i} outside the file")
        return data[i * block_size:(i + 1) * block_size]

    n_dir_blocks = (dir_bytes + block_size - 1) // block_size
    map_block = block(block_map)
    dir_blocks = struct.unpack_from(f"<{n_dir_blocks}I", map_block, 0)
    directory = b"".join(block(b) for b in dir_blocks)[:dir_bytes]
    count, = struct.unpack_from("<I", directory, 0)
    sizes = struct.unpack_from(f"<{count}I", directory, 4)
    pos = 4 + 4 * count
    streams = []
    for size in sizes:
        if size == 0xFFFFFFFF:
            size = 0
        n = (size + block_size - 1) // block_size
        blocks = struct.unpack_from(f"<{n}I", directory, pos)
        pos += 4 * n
        streams.append(b"".join(block(b) for b in blocks)[:size])
    return streams


def pdb_identity(data: bytes) -> tuple[bytes, int]:
    """(GUID, age) of a PDB: the info stream (1) holds version, signature, age and GUID; DbgHelp matches the DBI stream's (3) age."""
    streams = msf_streams(data)
    if len(streams) < 4 or len(streams[1]) < 28 or len(streams[3]) < 12:
        raise ValueError("the PDB has no info / DBI stream")
    _version, _signature, _age = struct.unpack_from("<3I", streams[1], 0)
    guid = streams[1][12:28]
    dbi_age, = struct.unpack_from("<I", streams[3], 8)
    return guid, dbi_age


def pe_codeview(data: bytes) -> tuple[bytes, int, str]:
    """(GUID, age, PDB name) of a PE32+ image's CodeView (RSDS) debug record (read directly: no pefile on the build hosts). ValueError when
    it has none."""
    if data[:2] != b"MZ":
        raise ValueError("not a PE file")
    pe, = struct.unpack_from("<I", data, 0x3C)
    if data[pe:pe + 4] != b"PE\0\0":
        raise ValueError("not a PE file")
    n_sections, = struct.unpack_from("<H", data, pe + 6)
    opt_size, = struct.unpack_from("<H", data, pe + 20)
    opt = pe + 24
    if struct.unpack_from("<H", data, opt)[0] != 0x20B:
        raise ValueError("not a PE32+ image")
    debug_rva, debug_size = struct.unpack_from("<II", data, opt + 112 + 6 * 8)  # data directory 6: debug
    sections = [struct.unpack_from("<IIII", data, opt + opt_size + 40 * i + 8) for i in range(n_sections)]  # vsize, va, rawsize, rawptr

    def file_offset(rva: int) -> int:
        for vsize, va, rawsize, rawptr in sections:
            if va <= rva < va + max(vsize, rawsize):
                return rawptr + rva - va
        raise ValueError(f"RVA {rva:#x} is in no section")

    pos = file_offset(debug_rva) if debug_size else 0
    for i in range(debug_size // 28):
        _chars, _stamp, _major, _minor, typ, size, _rva, ptr = struct.unpack_from("<IIHHIIII", data, pos + 28 * i)
        raw = data[ptr:ptr + size]
        if typ != 2 or raw[:4] != b"RSDS":  # IMAGE_DEBUG_TYPE_CODEVIEW
            continue
        guid = raw[4:20]
        age, = struct.unpack_from("<I", raw, 20)
        name = raw[24:].split(b"\0", 1)[0].decode("utf-8", "replace")
        return guid, age, name
    raise ValueError("no CodeView (RSDS) debug record")


def home_paths(data: bytes) -> list[str]:
    """The distinct home folder prefixes in `data` (a blanked user name counts: the scrub blanks the whole prefix)."""
    found = set()
    for rx in HOME_PATHS:
        for m in rx.finditer(data):
            if not any(data.startswith(t, m.start()) for t in TOOLCHAIN_PATHS):
                found.add((m.group(1) + m.group(2)).decode("utf-8", "replace"))
    return sorted(found)


def scrub(data: bytes) -> bytes:
    def blank(m: re.Match) -> bytes:
        if any(m.string.startswith(t, m.start()) for t in TOOLCHAIN_PATHS):
            return m.group(0)
        return b"_" * len(m.group(0))
    for rx in HOME_PATHS:
        data = rx.sub(blank, data)
    return data


def check(dll: bytes, pdb: bytes) -> list[str]:
    """Problems of a DLL + PDB pair: the PDB must be an MSF file, be the DLL's (GUID and age), the DLL must record only the PDB's file
    name (no build folder), and neither may hold a home folder path."""
    problems: list[str] = []
    try:
        guid, age = pdb_identity(pdb)
        dguid, dage, name = pe_codeview(dll)
        if (guid, age) != (dguid, dage):
            problems.append(f"the PDB is not the DLL's (PDB GUID {guid.hex()} age {age}, DLL {dguid.hex()} age {dage})")
        if "/" in name or "\\" in name:
            problems.append(f"the DLL records the PDB with a folder ({name}): link with -pdbaltpath:%_PDB%")
    except (ValueError, struct.error) as e:
        problems.append(str(e))
    for what, data in (("PDB", pdb), ("DLL", dll)):
        paths = home_paths(data)
        if paths:
            problems.append(f"the {what} holds home folder paths: {', '.join(paths[:3])}")
    return problems


def main(argv: list[str]) -> int:
    if len(argv) == 3 and argv[1] == "scrub":
        p = Path(argv[2])
        data = p.read_bytes()
        msf_streams(data)  # an MSF file before and after
        out = scrub(data)
        msf_streams(out)
        p.write_bytes(out)
        print(f"PDB scrubbed: {p.name} ({len(home_paths(data))} home folder path(s) blanked)")
        return 0
    if len(argv) == 4 and argv[1] == "check":
        problems = check(Path(argv[2]).read_bytes(), Path(argv[3]).read_bytes())
        for problem in problems:
            print(f"PDB CHECK: {problem}")
        if not problems:
            print(f"PDB CHECK ok: {Path(argv[3]).name} is {Path(argv[2]).name}'s, no home folder path")
        return 1 if problems else 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
