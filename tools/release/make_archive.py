#!/usr/bin/env python3
"""Lane RELEASE-1: the package archives, written byte-for-byte reproducibly.

    python3 tools/release/make_archive.py <folder> <out.tar.gz | out.zip> --mtime <unix time>
    python3 tools/release/make_archive.py --diff <a> <b>     (which layer / member of two archives differs; exit 1 when they differ)

The same folder content and --mtime (package.sh passes the commit's time, SOURCE_DATE_EPOCH) always give the same bytes:
  * members in sorted order, the top folder first; nothing but the folder and regular files (no links, no extra entries)
  * tar: GNU format, uid / gid 0, empty user / group names, every mtime = --mtime, mode 0755 for executables (the file's x bit) and the
    folder, 0644 otherwise; gzip: compression level 9, no file name, header mtime = --mtime
  * zip: no directory entries, no comments, no extra fields, deflate level 9, every date = --mtime (UTC), Unix permissions as above
The bytes then depend only on Python's zlib, which package.sh records next to the archives (see docs/RELEASE.md, "Reproducibility").
"""
from __future__ import annotations

import gzip
import io
import os
import stat
import sys
import tarfile
import time
import zipfile
from pathlib import Path


def members(folder: Path) -> list[Path]:
    out = []
    for p in sorted(folder.rglob("*")):
        if p.is_symlink() or not (p.is_file() or p.is_dir()):
            raise SystemExit(f"make_archive: {p} is neither a regular file nor a folder")
        out.append(p)
    return out


def mode_of(p: Path) -> int:
    return 0o755 if p.is_dir() or (p.stat().st_mode & stat.S_IXUSR) else 0o644


def tar_gz(folder: Path, out: Path, mtime: int) -> None:
    raw = io.BytesIO()
    with tarfile.open(fileobj=raw, mode="w", format=tarfile.GNU_FORMAT) as t:
        for p in [folder] + members(folder):
            arc = folder.name if p == folder else f"{folder.name}/{p.relative_to(folder).as_posix()}"
            info = tarfile.TarInfo(arc)
            info.mtime = mtime
            info.uid = info.gid = 0
            info.uname = info.gname = ""
            info.mode = mode_of(p)
            if p.is_dir():
                info.type = tarfile.DIRTYPE
                t.addfile(info)
            else:
                data = p.read_bytes()
                info.size = len(data)
                t.addfile(info, io.BytesIO(data))
    with open(out, "wb") as f, gzip.GzipFile(filename="", mode="wb", fileobj=f, compresslevel=9, mtime=mtime) as g:
        g.write(raw.getvalue())


def zip_file(folder: Path, out: Path, mtime: int) -> None:
    date = time.gmtime(max(mtime, 315532800))[:6]  # ZIP dates start in 1980
    with zipfile.ZipFile(out, "w") as z:
        for p in members(folder):
            if p.is_dir():
                raise SystemExit(f"make_archive: {p}: a package has no sub folders")
            info = zipfile.ZipInfo(f"{folder.name}/{p.relative_to(folder).as_posix()}", date_time=date)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.create_system = 3
            info.external_attr = (stat.S_IFREG | mode_of(p)) << 16
            z.writestr(info, p.read_bytes(), compress_type=zipfile.ZIP_DEFLATED, compresslevel=9)


def archive_members(path: Path) -> list[tuple[str, bytes]]:
    """(name + metadata, content) of every member, for --diff."""
    data = path.read_bytes()
    out = []
    if path.name.endswith(".zip"):
        with zipfile.ZipFile(io.BytesIO(data)) as z:
            for i in z.infolist():
                out.append((f"{i.filename} {i.date_time} {i.external_attr:o} extra={i.extra!r} comment={i.comment!r}", z.read(i)))
    else:
        with tarfile.open(fileobj=io.BytesIO(gzip.decompress(data)), mode="r:") as t:
            for m in t.getmembers():
                content = t.extractfile(m).read() if m.isfile() else b""
                out.append((f"{m.name} {m.type!r} {m.mode:o} {m.mtime} {m.uid}/{m.gid} {m.uname}/{m.gname} link={m.linkname!r}", content))
    return out


def diff(a: Path, b: Path) -> int:
    da, db = a.read_bytes(), b.read_bytes()
    if da == db:
        print(f"identical: {a.name}")
        return 0
    print(f"DIFFERENT: {a} and {b} ({len(da)} / {len(db)} bytes)")
    try:
        ma, mb = archive_members(a), archive_members(b)
    except Exception as e:  # a damaged archive: the byte difference is the finding
        print(f"  cannot list the members: {e}")
        return 1
    for (na, ca), (nb, cb) in zip(ma, mb):
        if na != nb:
            print(f"  member metadata: {na!r} vs {nb!r}")
        elif ca != cb:
            print(f"  member content: {na.split(' ')[0]} ({len(ca)} / {len(cb)} bytes)")
    if len(ma) != len(mb):
        print(f"  {len(ma)} / {len(mb)} members")
    if ma == mb:
        print("  same members: the difference is in the container (headers, padding, compression or bytes outside the members)")
    return 1


def main(argv: list[str]) -> int:
    if len(argv) == 3 and argv[0] == "--diff":
        return diff(Path(argv[1]), Path(argv[2]))
    if len(argv) != 4 or argv[2] != "--mtime":
        print(__doc__)
        return 2
    folder, out, mtime = Path(argv[0]), Path(argv[1]), int(argv[3])
    if out.name.endswith(".tar.gz"):
        tar_gz(folder, out, mtime)
    elif out.name.endswith(".zip"):
        zip_file(folder, out, mtime)
    else:
        print(f"make_archive: {out.name} is neither .tar.gz nor .zip")
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
