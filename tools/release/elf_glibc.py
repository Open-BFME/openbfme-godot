#!/usr/bin/env python3
"""Lane GLIBC-1: the GLIBC version a Linux ELF file needs.

    python3 tools/release/elf_glibc.py <ELF file> ...

Prints the highest GLIBC_<x.y[.z]> symbol version the files require ("2.35"; "none" when they require none); exit 1 for a file it cannot
read. package.sh writes it into the Linux package's VERSION file ("requires GLIBC 2.35"), and the launcher compares it with the system's
GLIBC before Play (launcher/scripts/core/glibc_check.gd), so a too old system gets a message instead of a library that fails to load.

Read the way the dynamic loader reads it (glibc elf/dl-version.c, elf.h): the PT_DYNAMIC segment's DT_VERNEED / DT_VERNEEDNUM / DT_STRTAB,
mapped to file offsets through the PT_LOAD segments, then every Elf64_Verneed / Elf64_Vernaux entry. Works on stripped files.
"""
from __future__ import annotations

import re
import struct
import sys
from pathlib import Path

PT_LOAD, PT_DYNAMIC = 1, 2
DT_NULL, DT_STRTAB, DT_VERNEED, DT_VERNEEDNUM = 0, 5, 0x6FFFFFFE, 0x6FFFFFFF
GLIBC_VERSION = re.compile(r"^GLIBC_(\d+)\.(\d+)(?:\.(\d+))?$")


def required_versions(data: bytes) -> list[str]:
    """Every version name the file's Verneed table requires. ValueError for a file that is not a 64-bit little-endian ELF or whose
    dynamic tables point outside it."""
    if data[:4] != b"\x7fELF" or len(data) < 64 or data[4] != 2 or data[5] != 1:
        raise ValueError("not a 64-bit little-endian ELF file")
    try:
        phoff, = struct.unpack_from("<Q", data, 32)
        phentsize, phnum = struct.unpack_from("<HH", data, 54)
        loads, dyn = [], None
        for i in range(phnum):
            p_type, _flags, p_offset, p_vaddr, _paddr, p_filesz = struct.unpack_from("<IIQQQQ", data, phoff + i * phentsize)
            if p_type == PT_LOAD:
                loads.append((p_vaddr, p_offset, p_filesz))
            elif p_type == PT_DYNAMIC:
                dyn = (p_offset, p_filesz)
        if dyn is None:
            return []  # statically linked

        def offset(vaddr: int) -> int:
            for v, o, n in loads:
                if v <= vaddr < v + n:
                    return o + vaddr - v
            raise ValueError(f"address {vaddr:#x} is in no loaded segment")

        tags: dict[int, int] = {}
        for o in range(dyn[0], dyn[0] + dyn[1], 16):
            tag, val = struct.unpack_from("<qQ", data, o)
            if tag == DT_NULL:
                break
            tags.setdefault(tag, val)
        if DT_VERNEED not in tags:
            return []
        strtab = offset(tags[DT_STRTAB])
        out: list[str] = []
        vn = offset(tags[DT_VERNEED])
        for _ in range(tags[DT_VERNEEDNUM]):
            _ver, cnt, _file, aux, nxt = struct.unpack_from("<HHIII", data, vn)
            a = vn + aux
            for _ in range(cnt):
                _hash, _flags, _other, name, anext = struct.unpack_from("<IHHII", data, a)
                out.append(data[strtab + name:data.index(b"\0", strtab + name)].decode("ascii", "replace"))
                if anext == 0:
                    break
                a += anext
            if nxt == 0:
                break
            vn += nxt
        return out
    except (struct.error, IndexError, KeyError) as e:
        raise ValueError(f"truncated ELF dynamic tables ({e})") from e


def glibc_requirement(data: bytes) -> tuple[int, ...] | None:
    """The highest GLIBC_<x.y[.z]> version the file requires (None: none)."""
    found = [tuple(int(g) for g in m.groups() if g is not None) for n in required_versions(data) if (m := GLIBC_VERSION.match(n))]
    return max(found) if found else None


def main(argv: list[str]) -> int:
    if not argv or argv[0].startswith("-"):
        print(__doc__.split("\n\n")[1])
        return 2
    best = None
    for f in argv:
        try:
            v = glibc_requirement(Path(f).read_bytes())
        except ValueError as e:
            print(f"elf_glibc: {f}: {e}", file=sys.stderr)
            return 1
        if v is not None and (best is None or v > best):
            best = v
    print("none" if best is None else ".".join(map(str, best)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
