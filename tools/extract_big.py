#!/usr/bin/env python3
"""Extract EA SAGE BIGF/BIG4 archives (BFME2)."""
from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path


def read_cstring(data: bytes, offset: int) -> tuple[str, int]:
    end = data.index(0, offset)
    return data[offset:end].decode("latin-1", errors="replace"), end + 1


def parse_big(path: Path) -> list[tuple[str, int, int]]:
    data = path.read_bytes()
    if len(data) < 16:
        raise ValueError("file too small")
    magic = data[:4]
    if magic not in (b"BIGF", b"BIG4"):
        raise ValueError(f"unsupported magic {magic!r}")
    archive_size = struct.unpack_from("<I", data, 4)[0]
    file_count = struct.unpack_from(">I", data, 8)[0]
    header_size = struct.unpack_from(">I", data, 12)[0]
    entries: list[tuple[str, int, int]] = []
    off = 16
    for _ in range(file_count):
        if off + 8 > len(data):
            break
        entry_offset = struct.unpack_from(">I", data, off)[0]
        entry_size = struct.unpack_from(">I", data, off + 4)[0]
        off += 8
        name, off = read_cstring(data, off)
        name = name.replace("\\", "/")
        entries.append((name, entry_offset, entry_size))
    return entries


def extract(archive: Path, out_dir: Path, prefix: str | None = None) -> int:
    data = archive.read_bytes()
    entries = parse_big(archive)
    count = 0
    for name, offset, size in entries:
        if prefix and not name.lower().startswith(prefix.lower().replace("\\", "/")):
            continue
        target = out_dir / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data[offset : offset + size])
        count += 1
    return count


def main() -> int:
    ap = argparse.ArgumentParser(description="Extract BFME2 BIG archives")
    ap.add_argument("archive", type=Path)
    ap.add_argument("-o", "--out", type=Path, required=True)
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--prefix", default=None, help="Only extract paths with this prefix")
    ap.add_argument("--limit", type=int, default=0)
    args = ap.parse_args()
    entries = parse_big(args.archive)
    if args.list:
        n = 0
        for name, offset, size in entries:
            if args.prefix and not name.lower().startswith(args.prefix.lower().replace("\\", "/")):
                continue
            print(f"{size:8d}  {name}")
            n += 1
            if args.limit and n >= args.limit:
                break
        print(f"# {n} entries (total in archive: {len(entries)})", file=sys.stderr)
        return 0
    args.out.mkdir(parents=True, exist_ok=True)
    n = extract(args.archive, args.out, args.prefix)
    print(f"extracted {n} files -> {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
