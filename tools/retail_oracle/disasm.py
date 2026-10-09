#!/usr/bin/env python3
"""Disassemble bytes of a retail PE image by virtual address (needs `pip install capstone`).

    python tools/retail_oracle/disasm.py <game.dat> <va hex> [byte count]

Reads the file from the user's install at runtime; nothing retail is stored in the repo.
"""
from __future__ import annotations

import struct
import sys


def sections(data: bytes):
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    nsec = struct.unpack_from("<H", data, pe + 6)[0]
    opt = pe + 24
    base = struct.unpack_from("<I", data, opt + 28)[0]
    first = opt + struct.unpack_from("<H", data, pe + 20)[0]
    return base, [struct.unpack_from("<8sIIII", data, first + i * 40) for i in range(nsec)]


def va_to_offset(data: bytes, va: int) -> int:
    base, secs = sections(data)
    rva = va - base
    for _name, vsize, sva, rsize, rptr in secs:
        if sva <= rva < sva + max(vsize, rsize):
            return rva - sva + rptr
    raise ValueError(f"VA {va:#x} is outside every section")


def read_va(data: bytes, va: int, count: int) -> bytes:
    base, secs = sections(data)
    rva = va - base
    for _name, vsize, sva, rsize, rptr in secs:
        if sva <= rva < sva + max(vsize, rsize):
            off = rva - sva + rptr
            return data[off:off + count]
    raise ValueError(f"VA {va:#x} is outside every section")


def main() -> int:
    import capstone

    path, va = sys.argv[1], int(sys.argv[2], 16)
    count = int(sys.argv[3], 0) if len(sys.argv) > 3 else 128
    data = open(path, "rb").read()
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    for i in md.disasm(read_va(data, va, count), va):
        print(f"{i.address:x}: {i.mnemonic} {i.op_str}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
