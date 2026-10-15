"""Lane CONTRIB-1 (GitHub issue #21): lists a BIG archive's entries as the archive policy's `entryChecked` versions
(engine/data/retail-archives/*.json): name (lower case, backslashes), size and MD5 of every entry, each distinct version once.
Only names, sizes and hashes are printed: no game bytes. A player whose unmodified Data1.big is refused runs

    py -3 tools\\release\\big_entries.py "C:\\...\\Data1.big"        (Windows)
    python3 tools/release/big_entries.py /path/to/Data1.big          (Linux)

and posts the output, so that version can be pinned. Every count and range is checked before it is used (a file from a player's disk).
"""
from __future__ import annotations

import hashlib
import json
import os
import struct
import sys

MAX_ENTRIES = 4096
MAX_FILE = 64 << 20  # far larger than any known Data1.big (under 1 MB): refused by size before it is read


def entries(data: bytes) -> list[dict]:
    if len(data) < 16 or data[:4] not in (b"BIGF", b"BIG4"):
        raise ValueError("not a BIG archive")
    count = struct.unpack(">I", data[8:12])[0]
    if count > MAX_ENTRIES or count * 9 > len(data) - 16:
        raise ValueError(f"declares {count} entries, more than it can hold")
    pos, out, seen, hashed = 16, [], set(), 0
    for i in range(count):
        if pos + 8 > len(data):
            raise ValueError(f"the directory is cut short at entry {i}")
        offset, size = struct.unpack(">II", data[pos:pos + 8])
        pos += 8
        end = data.find(b"\0", pos, min(len(data), pos + 261))
        if end < 0:
            raise ValueError(f"the name of entry {i} is cut short or too long")
        name = data[pos:end].decode("latin-1").lower().replace("/", "\\")
        pos = end + 1
        if offset + size > len(data):
            raise ValueError(f"entry '{name}' runs past the end of the archive")
        # an archive whose entries do not overlap hashes at most its own size: more is refused (overlapping ranges would be hashed again and again)
        hashed += size
        if hashed > len(data):
            raise ValueError(f"its entries overlap (entry '{name}' makes the bytes to check exceed the archive's own {len(data)} bytes)")
        md5 = hashlib.md5(data[offset:offset + size]).hexdigest()
        if (name, md5) not in seen:
            seen.add((name, md5))
            out.append({"name": name, "size": size, "md5": md5})
    return out


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    try:
        with open(argv[1], "rb") as f:
            size = os.fstat(f.fileno()).st_size
            if size > MAX_FILE:
                print(f"{argv[1]}: {size} bytes, far larger than any known Data1.big (limit {MAX_FILE}); nothing was read", file=sys.stderr)
                return 1
            data = f.read(MAX_FILE + 1)
    except OSError as e:
        print(f"{argv[1]}: cannot read it: {e}", file=sys.stderr)
        return 1
    try:
        listed = entries(data)
    except ValueError as e:
        print(f"{argv[1]}: {e}", file=sys.stderr)
        return 1
    print(json.dumps({"file": {"size": len(data), "md5": hashlib.md5(data).hexdigest()}, "entries": listed}, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
