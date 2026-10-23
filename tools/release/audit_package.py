#!/usr/bin/env python3
"""Lane RELEASE-1: a test package must never carry game files.

    python3 tools/release/audit_package.py <dir | .tar.gz | .zip | .pck> ...

Every file is judged the way the commit gate judges a staged file (tools/precommit.py): by suffix (RETAIL_SUFFIXES) and by content
(retail_content: RIFF/WAVE, ID3, MPEG Layer III, BIG, DDS, under any name). Containers are recognised by their bytes, whatever their names:
Godot packs (also appended to an executable), ZIP, gzip, bzip2, xz and tar are opened and audited member by member, recursively; other
container kinds (7-Zip, Zstandard, RAR, CAB, LZ4, LZO), encrypted packs and encrypted ZIP members are findings, so nothing hides inside. Text files are also checked for
developer-machine paths (precommit MACHINE_PATH). Every file, member and pack entry (and every file of a --release package) is also
compared with the files the launcher's AIO download pins (tools/release/aio_package_files.json, by MD5 and size: the RotWK / BFME2 base
download and the optional BFME1 package of community PR #36, lotrbfme.exe, worldbuilder.exe, the DSOAL dsound.dll ... among them): those
come only from the third-party service, never from our packages. Exit 1 with one line per problem; 0 prints "package audit ok (<n> files)".
"""
from __future__ import annotations

import bz2
import functools
import json
import gzip
import hashlib
import io
import lzma
import re
import stat
import struct
import zlib
import sys
import tarfile
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from precommit import MACHINE_PATH, MSF_MAGICS, PRIVATE_SUFFIXES, RETAIL_SUFFIXES, private_symbols, retail_content  # noqa: E402

PCK_MAGIC = b"GDPC"
LOCAL_PATH = re.compile(
    r"(?:[A-Za-z]:\\Users\\[^\\/\x00\s]+\\|/Users/[^/\x00\s]+/|/home/[^/\x00\s]+/|"
    r"/root/|/(?:private/)?tmp/[^/\x00\s]+(?:/|$)|/(?:private/)?var/tmp/[^/\x00\s]+(?:/|$)|"
    r"/(?:private/)?var/folders/[^/\x00\s]+/)"
)
MAX_DEPTH = 16                 # containers inside containers
MAX_UNPACKED = 4 << 30         # bytes one compressed stream may expand to (a bomb is a finding, not a hang)
# containers recognised by their bytes that this audit cannot open: a finding, never a pass (fail closed)
UNOPENABLE = {b"7z\xbc\xaf\x27\x1c": "7-Zip", b"\x28\xb5\x2f\xfd": "Zstandard", b"Rar!\x1a\x07": "RAR", b"MSCF": "CAB",
              b"\x04\x22\x4d\x18": "LZ4", b"\x89LZO": "LZO"}


AIO_FILES = Path(__file__).resolve().parent / "aio_package_files.json"


@functools.lru_cache(maxsize=None)
def aio_pinned(path: Path = AIO_FILES) -> dict[tuple[str, int], str]:
    """(md5, size) -> "<package>: <name>" of every non-empty file the AIO packages pin. Fails closed: a missing or unreadable table, or a
    package without files, is an error, never an empty set."""
    try:
        table = json.loads(path.read_text())
    except (OSError, ValueError) as e:
        raise SystemExit(f"AUDIT: cannot read the AIO pins {path.name}: {e}")
    if not isinstance(table, dict) or not table or any(not isinstance(rows, list) or not rows for rows in table.values()):
        raise SystemExit(f"AUDIT: the AIO pins {path.name} name no package or a package without files")
    return {(str(r[1]), int(r[2])): f"{guid}: {r[0]}" for guid, rows in table.items() for r in rows if int(r[2]) > 0}


@functools.lru_cache(maxsize=None)
def _aio_sizes(path: Path = AIO_FILES) -> frozenset:
    return frozenset(size for _, size in aio_pinned(path))


def aio_file(data: bytes) -> str | None:
    """the AIO pin `data` is (by size, then MD5), or None"""
    if len(data) not in _aio_sizes(AIO_FILES):
        return None
    return aio_pinned(AIO_FILES).get((hashlib.md5(data).hexdigest(), len(data)))


STREAM_MAGIC = {"gzip": b"\x1f\x8b", "bzip2": b"BZh", "xz": b"\xfd7zXZ\x00"}


# DEVPORT-6: the build-host paths the OFFICIAL Godot 4.7.2 export templates carry (SHA-pinned upstream binaries; every exported game and
# launcher executable is one): Godot's GitHub Actions runner's (its home folder "runner") Rust crate sources (AccessKit) in the Linux and Windows templates, and the
# C++/WinRT headers of its Windows build container. Measured on a Linux CI host 2026-10-17: linux_{debug,release}.x86_64 105 matches each, all under
# the first prefix; windows_{debug,release}_x86_64.exe 33 each, under the two. With LOCAL_PATH applied to binaries (7c731b971) every
# Linux / Windows launcher and game package failed the content audit. Exactly these prefixes are upstream's; any other path still fails.
UPSTREAM_TEMPLATE_PATHS = (
    "/" + "home/runner/.cargo/registry/src/index.crates.io-1949cf8c6b5b557f/",   # split: the commit gate's path rule reads this file
    "/root/winrt/winrt/",
)


# MACPLAY-1 (Sol r1 on DEVPORT-6, P2): the exemption was a text replacement anywhere in any blob, before every check: a path that
# only STARTED with an upstream prefix and climbed out of it (/root/winrt/winrt/../../private/x.cpp) passed, inside a game pack too, and a
# forbidden build prefix below an upstream one was erased before the forbidden-prefix check. Now: the forbidden prefixes are checked on
# the original text; the exemption applies only to executables (what the official templates are: a pack appended to one is audited
# member by member as a blob of its own, without the exemption); and only to a whole path that lies below an upstream prefix with no "."
# or ".." component and no backslash (the path runs to the first NUL, space, quote or other character no build path holds).
# MACPLAY-1 (Sol r2): "an executable" was two magic bytes: a README-MAC.txt starting "MZ" got the exemption. r3 then required an
# executable slot and a whole parsed image, but structure is not provenance: a synthetic Mach-O segment in the project-built
# openbfme-bundle-swap carrying /root/winrt/winrt/private-source.cpp passed both audits (Sol r3, P2).
# MACPLAY-1 (Sol r3): the exemption now needs verified upstream bytes. An upstream path is exempt only inside a section of the file
# whose (format, name, size, SHA-256) is one of UPSTREAM_TEMPLATE_SECTIONS below: the read-only data section of an official Godot
# 4.7.2 x86-64 Linux / Windows template, where all of their upstream paths lie (105 per Linux template in .rodata, 33 per Windows
# template in .rdata). Godot's export leaves that section byte for byte (it rewrites the "pck" section header / the .rsrc resources and
# appends the pack): measured 2026-10-18 on exported v0.4.5 / v0.4.6 game and launcher executables of both platforms. And only in the
# slots that hold an exported Godot template (UPSTREAM_TEMPLATE_SLOTS), never in the project-built GDExtension libraries
# (openbfme.*.so / .dll / .dylib) or the bundle swap helper (tools/release/macos_bundle_swap.c), whatever bytes they hold. The macOS
# templates carry no upstream path of these prefixes (their /Users/runner/ paths are replaced when the Mac package is made:
# package_macos_preview.sanitize_template_slice), so no Mac slot is exempt.
# Pinned from Godot_v4.7.2-stable_export_templates.tpz (SHA-512 ca4d71c4...4079 as Godot's Godot_v4.7.2-SHA512-SUMS.txt lists it);
# templates/linux_debug.x86_64 sha256 1a291d3d..., linux_release.x86_64 d9f79ab8..., windows_debug_x86_64.exe 51498b72...,
# windows_release_x86_64.exe d34d36f3... (the console wrappers windows_*_console.exe carry no such path).
UPSTREAM_TEMPLATE_SECTIONS = {
    ("elf", ".rodata", 10337072): "2def8bddcd97034a73b244a9f904304642a59446966326e60e617a13c21d0d44",   # linux_debug.x86_64
    ("elf", ".rodata", 10148304): "5a61a1899d9645c939135f03ac2c7be2e7beedec1eb51766e5272b82bd97597f",   # linux_release.x86_64
    ("pe", ".rdata", 20961792): "fe821b19a4d508603f794c07335f013da73a1702de568cabc15911948bbfe57d",     # windows_debug_x86_64.exe
    ("pe", ".rdata", 20376064): "e2cae493f1bc84d760c20f2accd25886b1d4835deb8c163f3ad4fd3306075870",     # windows_release_x86_64.exe
}
# the release slots an exported Linux / Windows Godot template fills (RELEASE_FILES: the game's executable, its console wrapper and the
# launcher's executable with its embedded pack); nothing else gets the exemption
UPSTREAM_TEMPLATE_SLOTS = frozenset({"OpenBFME.x86_64", "OpenBFMELauncher.x86_64", "OpenBFME.exe", "OpenBFME.console.exe",
                                     "OpenBFMELauncher.exe"})


def is_template_slot(name: str) -> bool:
    """`name` (a package path, members of containers after ":") is a slot an exported Linux / Windows Godot template fills"""
    return re.split(r"[/:]", name)[-1] in UPSTREAM_TEMPLATE_SLOTS


_PATH_END = re.compile(r"[\x00-\x20\"'<>|*?\x7f]")


def _upstream_spans(text: str) -> list[tuple[int, int]]:
    """(start, end) of every whole upstream-template path in `text` that stays below its prefix"""
    spans = []
    for upstream in UPSTREAM_TEMPLATE_PATHS:
        at = text.find(upstream)
        while at >= 0:
            end_match = _PATH_END.search(text, at)
            end = end_match.start() if end_match else len(text)
            rest = text[at + len(upstream):end]
            if "\\" not in rest and not any(part in (".", "..") for part in rest.split("/")):
                spans.append((at, end))
            at = text.find(upstream, at + 1)
    return spans


def _sections(data: bytes) -> list[tuple[str, str, int, int]]:
    """(format, name, file offset, size) of every section with file bytes of a 64-bit little-endian ELF or a PE file; [] for anything
    else or anything whose headers do not parse (a section running past the file is left out)"""
    out = []
    try:
        if data[:4] == b"\x7fELF" and data[4:6] == b"\x02\x01":
            shoff, = struct.unpack_from("<Q", data, 40)
            shentsize, shnum, shstrndx = struct.unpack_from("<HHH", data, 58)
            if shentsize != 64 or shstrndx >= shnum:
                return []
            strings, = struct.unpack_from("<Q", data, shoff + shstrndx * shentsize + 24)
            for i in range(shnum):
                o = shoff + i * shentsize
                name_at, typ = struct.unpack_from("<II", data, o)
                off, size = struct.unpack_from("<QQ", data, o + 24)
                end = data.index(b"\0", strings + name_at)
                if typ != 8 and off + size <= len(data):  # not NOBITS
                    out.append(("elf", data[strings + name_at:end].decode("latin-1"), off, size))
        elif data[:2] == b"MZ":
            import pefile  # noqa: PLC0415
            pe = pefile.PE(data=data, fast_load=True)
            for s in pe.sections:
                if s.PointerToRawData + s.SizeOfRawData <= len(data):
                    out.append(("pe", s.Name.rstrip(b"\0").decode("latin-1"), s.PointerToRawData, s.SizeOfRawData))
    except Exception:  # noqa: BLE001 (struct.error, ValueError, pefile's PEFormatError: no section table)
        return []
    return out


def upstream_template_ranges(data: bytes) -> list[tuple[int, int]]:
    """(start, end) of every section of `data` that is byte for byte a pinned section of an official Godot template
    (UPSTREAM_TEMPLATE_SECTIONS)"""
    ranges = []
    for fmt, name, off, size in _sections(data):
        want = UPSTREAM_TEMPLATE_SECTIONS.get((fmt, name, size))
        if want is not None and hashlib.sha256(data[off:off + size]).hexdigest() == want:
            ranges.append((off, off + size))
    return ranges


def _blank_upstream_paths(data: bytes, ranges: list[tuple[int, int]]) -> bytes:
    """`data` with every whole upstream-template path that lies inside one of `ranges` (byte offsets: latin-1 and UTF-16 decodings map
    one to one onto bytes) replaced by NUL bytes of the same length; nothing else changes"""
    def inside(a: int, b: int) -> bool:
        return any(lo <= a and b <= hi for lo, hi in ranges)
    out = bytearray(data)
    for a, b in _upstream_spans(data.decode("latin-1")):
        if inside(a, b):
            out[a:b] = bytes(b - a)
    even = data[:len(data) // 2 * 2]
    for encoding in ("utf-16le", "utf-16be"):
        for a, b in _upstream_spans(even.decode(encoding, "surrogatepass")):
            if inside(2 * a, 2 * b):
                out[2 * a:2 * b] = bytes(2 * (b - a))
    return bytes(out)


def _texts(data: bytes) -> list[str]:
    texts = [data.decode("utf-8", "ignore")]
    if b"\x00" in data:
        texts.extend((data.decode("utf-16le", "ignore"), data.decode("utf-16be", "ignore")))
    return texts


def developer_path_problem(data: bytes, forbidden_prefixes: tuple[str, ...] = (), template_slot: bool = False) -> str:
    """"" or why `data` leaks a build path. `template_slot`: the blob fills a release slot of an exported Godot template
    (UPSTREAM_TEMPLATE_SLOTS); only then are the upstream paths inside its pinned upstream sections exempt."""
    prefixes = tuple(prefix.rstrip("/\\") for prefix in forbidden_prefixes if prefix.rstrip("/\\"))
    if any(prefix in text for text in _texts(data) for prefix in prefixes):
        return "exact checkout/build/stage path"
    ranges = upstream_template_ranges(data) if template_slot else []
    scanned = _blank_upstream_paths(data, ranges) if ranges else data
    if any(LOCAL_PATH.search(text) for text in _texts(scanned)):
        return "developer-machine path"
    return ""


def stream_kind(data: bytes) -> str | None:
    for kind, magic in STREAM_MAGIC.items():
        if data.startswith(magic):
            return kind
    return None


def _decompressor(kind: str):
    if kind == "gzip":
        return zlib.decompressobj(16 + zlib.MAX_WBITS)
    if kind == "bzip2":
        return bz2.BZ2Decompressor()
    return lzma.LZMADecompressor(format=lzma.FORMAT_XZ)


def decompress_members(kind: str, data: bytes) -> list[bytes]:
    """Every member stream of a gzip / bzip2 / xz blob, each decoded to its end (review r2: only the first stream was read, so a harmless
    stream followed by a compressed BIG passed, and a truncated stream passed too). ValueError for a stream that ends before its end
    marker, for bytes after the last stream that are not another stream of the same kind (xz: or its zero stream padding), and for more
    than MAX_UNPACKED bytes in total (a bomb)."""
    members: list[bytes] = []
    rest = data
    total = 0
    while True:
        d = _decompressor(kind)
        out = d.decompress(rest, MAX_UNPACKED + 1 - total)
        total += len(out)
        if total > MAX_UNPACKED:
            raise ValueError(f"{kind} data expands beyond {MAX_UNPACKED} bytes")
        if not d.eof:
            more = d.unconsumed_tail if kind == "gzip" else (b"" if d.needs_input else b"x")
            raise ValueError(f"{kind} data expands beyond {MAX_UNPACKED} bytes" if more else f"truncated {kind} stream {len(members) + 1}")
        members.append(out)
        rest = d.unused_data
        if kind == "xz":
            # xz stream padding: zero bytes in multiples of four between and after streams
            stripped = rest.lstrip(b"\0")
            pad = len(rest) - len(stripped)
            if pad % 4:
                raise ValueError(f"xz stream padding of {pad} bytes (not a multiple of 4)")
            rest = stripped
        if not rest:
            return members
        if not rest.startswith(STREAM_MAGIC[kind]):
            raise ValueError(f"{len(rest)} trailing bytes after {kind} stream {len(members)}")


def is_tar(data: bytes) -> bool:
    """A tar header by its checksum (POSIX ustar, GNU and the old V7 format alike; review r2: V7 archives have no "ustar" magic)."""
    if len(data) < 512 or data[0] == 0:
        return False
    field = data[148:156].replace(b"\0", b" ").strip()
    if not field or any(c not in b"01234567" for c in field):
        return False
    stored = int(field, 8)
    unsigned = sum(data[:148]) + 8 * 32 + sum(data[156:512])
    signed = sum((b - 256 if b > 127 else b) for b in data[:148]) + 8 * 32 + sum((b - 256 if b > 127 else b) for b in data[156:512])
    return stored in (unsigned, signed)


def pck_entries(data: bytes, base: int = 0, end: int | None = None) -> list[tuple[str, bytes]]:
    """The files of a Godot 4 pack starting at `base` in `data` and ending at `end` (format 2, Godot 4.0-4.3: the directory after the
    header; formats 3 and 4, Godot 4.4+ (4.7.2 writes 4, checked on a real export): the same header plus the directory's offset, the
    directory at the end). Every byte of the pack must be the header, the directory, an entry or zero padding (review r2: nothing may sit
    in the pack outside its entries); ValueError otherwise."""
    end = len(data) if end is None else end
    if data[base:base + 4] != PCK_MAGIC:
        raise ValueError("not a Godot pack")
    fmt, major, minor, patch, flags = struct.unpack_from("<5I", data, base + 4)
    if fmt not in (2, 3, 4):
        raise ValueError("unknown pack format (not 2, 3 or 4; its value is not printed: it may be text)")
    if flags & 1:
        raise ValueError("encrypted pack directory: cannot be audited")
    # PUBLISH-2 r4 (Sol r3 P2: a hostname in the reserved header words passed every audit): every header and directory byte is a checked
    # value, so nothing but the entry names (checked as names) and contents (audited) can carry text: the Godot version numbers below 256,
    # no flag but PACK_REL_FILEBASE, the 16 reserved words zero, the files right after the header (alignment padding only), each entry's
    # name UTF-8 with zero padding, its MD5 its content's (Godot 4.7.2 writes it: v0.4.6's packs) and its flags zero
    if max(major, minor, patch) > 255:
        raise ValueError("a pack header version number above 255")
    if flags & ~2:
        raise ValueError("unknown pack flags (their value is not printed: it may be text)")
    rel_base = flags & 2  # PACK_REL_FILEBASE: offsets relative to the pack start
    file_base, = struct.unpack_from("<Q", data, base + 24)
    if fmt >= 3:
        dir_offset, = struct.unpack_from("<Q", data, base + 32)
        header_end = base + 40 + 16 * 4
        pos = base + dir_offset
    else:
        header_end = base + 32 + 16 * 4
        pos = header_end
    if any(data[header_end - 64:header_end]):
        raise ValueError("non-zero reserved words in the pack header")
    files_at = base + file_base if rel_base or fmt >= 3 else file_base
    if not (header_end <= files_at <= header_end + 64 if fmt >= 3 else files_at <= end):
        raise ValueError("the pack's files do not start right after its header (the offset is not printed: it may be text)")
    dir_start = pos
    count, = struct.unpack_from("<I", data, pos)
    pos += 4
    out = []
    used = [(base, header_end)]
    for _ in range(count):
        n, = struct.unpack_from("<I", data, pos)
        pos += 4
        raw = data[pos:pos + n].rstrip(b"\0")
        if b"\0" in raw:
            raise ValueError("a pack entry name with bytes after its end")
        try:
            path = raw.decode("utf-8")
        except UnicodeDecodeError:
            raise ValueError("a pack entry name that is not UTF-8") from None
        pos += n
        ofs, size = struct.unpack_from("<QQ", data, pos)
        md5 = data[pos + 16:pos + 32]
        pos += 16 + 16  # offset, size, md5
        eflags, = struct.unpack_from("<I", data, pos)
        pos += 4
        if eflags & 1:
            raise ValueError(f"encrypted pack entry {path}: cannot be audited")
        if eflags:
            raise ValueError(f"pack entry {path}: unknown flags (their value is not printed: it may be text)")
        start = files_at + ofs
        if start < base or start + size > end:
            raise ValueError(f"pack entry {path} lies outside the pack")
        if hashlib.md5(data[start:start + size]).digest() != md5:
            raise ValueError(f"pack entry {path}: its MD5 is not its content's")
        out.append((path, data[start:start + size]))
        used.append((start, start + size))
    used.append((dir_start, pos))
    if pos > end:
        raise ValueError("the pack directory runs past the pack")
    used.sort()
    cur = base
    for s0, e0 in used:
        if s0 < cur:
            raise ValueError(f"pack regions overlap at {s0 - base}")
        if any(data[cur:s0]):
            raise ValueError(f"{s0 - cur} non-zero bytes at pack offset {cur - base} belong to no entry")
        cur = e0
    if any(data[cur:end]):
        raise ValueError(f"{end - cur} non-zero bytes after the last pack region")
    return out


def embedded_pck_base(data: bytes) -> int | None:
    """An executable with an embedded pack ends with <u64 pack size> "GDPC"."""
    if len(data) >= 12 and data[-4:] == PCK_MAGIC:
        size, = struct.unpack_from("<Q", data, len(data) - 12)
        base = len(data) - 12 - size
        if 0 <= base and data[base:base + 4] == PCK_MAGIC:
            return base
    return None


def zip_member_exact(data: bytes, info) -> tuple[bytes, str]:
    """A ZIP member read from its own local entry: stored or deflated, the compressed region decoded to its end with no unused bytes
    (review r3: zipfile ignores a compressed tail), its size and CRC as listed. (content, "") or (b"", why)."""
    off = info.header_offset
    if data[off:off + 4] != b"PK\x03\x04":
        return b"", "no local header at its offset"
    n, m = struct.unpack_from("<HH", data, off + 26)
    region = data[off + 30 + n + m:off + 30 + n + m + info.compress_size]
    if len(region) != info.compress_size:
        return b"", "compressed data runs past the archive"
    if info.compress_type == zipfile.ZIP_STORED:
        content = region
    elif info.compress_type == zipfile.ZIP_DEFLATED:
        d = zlib.decompressobj(-zlib.MAX_WBITS)
        try:
            content = d.decompress(region, MAX_UNPACKED + 1)
        except zlib.error as e:
            return b"", f"broken deflate data ({e})"
        if not d.eof:
            return b"", "the deflate stream does not end"
        if d.unused_data or d.unconsumed_tail:
            return b"", f"{len(d.unused_data) + len(d.unconsumed_tail)} bytes after the deflate stream in the member's compressed data"
    else:
        return b"", f"compression method {info.compress_type} (only stored and deflate are read)"
    if len(content) != info.file_size:
        return b"", f"{len(content)} bytes, the directory lists {info.file_size}"
    if zlib.crc32(content) != info.CRC:
        return b"", "CRC mismatch"
    return content, ""


def zip_unexplained(data: bytes, infos, start_dir: int) -> list[str]:
    """What in a ZIP is neither a listed local entry nor the central directory and its end records."""
    regions = []
    for info in infos:
        off = info.header_offset
        if data[off:off + 4] != b"PK\x03\x04":
            return [f"{info.filename}: no local header at its offset"]
        n, m = struct.unpack_from("<HH", data, off + 26)
        end = off + 30 + n + m + info.compress_size
        if info.flag_bits & 8:  # a data descriptor follows: optional signature, crc, sizes (4 or 8 bytes each)
            if data[end:end + 4] == b"PK\x07\x08":
                end += 4
            end += 4 + (16 if info.compress_size >= 0xFFFFFFFF or info.file_size >= 0xFFFFFFFF else 8)
        regions.append((off, end))
    eocd = data.rfind(b"PK\x05\x06")
    if eocd < 0:
        return ["no end of central directory record"]
    cd_size, = struct.unpack_from("<I", data, eocd + 12)
    comment, = struct.unpack_from("<H", data, eocd + 20)
    problems = []
    if eocd + 22 + comment != len(data):
        problems.append(f"{len(data) - (eocd + 22 + comment)} bytes after the end of central directory record")
    cd_end = start_dir + cd_size if cd_size != 0xFFFFFFFF else eocd
    between = data[cd_end:eocd]
    if between and not between.startswith(b"PK\x06\x06"):  # only the ZIP64 end records may sit there
        problems.append(f"{len(between)} unexplained bytes between the central directory and its end record")
    regions.sort()
    cur = regions[0][0] if regions else start_dir
    for s0, e0 in regions:
        if s0 != cur:
            problems.append(f"{abs(s0 - cur)} bytes at offset {min(s0, cur)} belong to no listed entry" if s0 > cur else f"entries overlap at {s0}")
        cur = max(cur, e0)
    if cur != start_dir:
        problems.append(f"{abs(start_dir - cur)} bytes between the last entry and the central directory are not explained")
    return problems


class Audit:
    # DEVPORT-6: which developer-path rule applies. A release package (binaries, packs) gets LOCAL_PATH on every blob, containers included
    # (any /Users/<name>/, /home/<name>/ or /tmp/<dir>/ in a shipped file is a leaked build path). The public SOURCE tree
    # (autorelease.py's SyncAudit) keeps the commit gate's own rule (tools/precommit.py MACHINE_PATH on text leaves, as before the Mac
    # packaging): LOCAL_PATH there refused the tree's own tests and docs (/tmp/... examples, test fixtures), so every sync failed.
    commit_gate_paths = False

    def __init__(self, forbidden_prefixes: tuple[str, ...] = ()) -> None:
        self.problems: list[str] = []
        self.files = 0
        self.forbidden_prefixes = forbidden_prefixes

    def blob(self, name: str, data: bytes, depth: int = 0) -> None:
        """Containers are recognised by their bytes, never by their names (review r1: a renamed .zip or .tar.gz passed with BIG/DDS
        inside): Godot packs (also appended to an executable), ZIP (also appended), gzip / bzip2 / xz streams and tar archives are opened
        and every member audited the same way, at any depth up to MAX_DEPTH; a container kind that cannot be opened is a finding."""
        self.files += 1
        path_problem = "" if self.commit_gate_paths else developer_path_problem(
            data, self.forbidden_prefixes, template_slot=is_template_slot(name))
        if path_problem:
            self.problems.append(f"{path_problem} in {name}")
        if depth > MAX_DEPTH:
            self.problems.append(f"containers nested deeper than {MAX_DEPTH}: {name}")
            return
        pinned = aio_file(data)
        if pinned:
            self.problems.append(f"a game file the AIO service distributes ({pinned}): {name}")
            return
        low = name.lower()
        if Path(low.rsplit(":", 1)[-1]).suffix in RETAIL_SUFFIXES:
            self.problems.append(f"retail-format file name: {name}")
            return
        # lane WINCRASH-1 (stop S-1923): debug symbols never ship; the PDB stays in the builder's symbol store
        if Path(low.rsplit(":", 1)[-1]).suffix in PRIVATE_SUFFIXES:
            self.problems.append(f"debug symbols (PDB) in the package: {name}")
            return
        if private_symbols(data):
            self.problems.append(f"debug symbols (MSF / PDB bytes) in the package: {name}")
            return
        kind = retail_content(data)
        if kind:
            self.problems.append(f"retail-format bytes ({kind}): {name}")
            return
        for magic, what in UNOPENABLE.items():
            if data.startswith(magic):
                self.problems.append(f"{what} container the audit cannot open: {name}")
                return
        if data[:4] == PCK_MAGIC or low.endswith(".pck"):
            self.pack(name, data, 0, depth)
            return
        kind = stream_kind(data)
        if kind:
            try:
                members = decompress_members(kind, data)
            except (ValueError, OSError, EOFError, zlib.error, lzma.LZMAError) as e:
                self.problems.append(f"{name}: unreadable {kind} data ({e})")
                return
            # each member stream on its own (a BIG in the second member starts that member's bytes) and the joined data (what gunzip
            # writes, e.g. a tar split across members)
            if len(members) > 1:
                for i, m in enumerate(members, 1):
                    self.blob(f"{name}[{kind} #{i}]", m, depth + 1)
            self.blob(f"{name}[{kind}]", b"".join(members), depth + 1)
            return
        if is_tar(data):
            self.tar(name, data, depth)
            return
        if data[:4] in (b"PK\x03\x04", b"PK\x05\x06", b"PK\x07\x08") and not zipfile.is_zipfile(io.BytesIO(data)):
            self.problems.append(f"{name}: malformed ZIP archive (ZIP signature, no readable central directory)")
            return
        base = embedded_pck_base(data)
        if base is not None:
            self.pack(name + "[embedded pack]", data, base, depth, end=len(data) - 12)
        if zipfile.is_zipfile(io.BytesIO(data)):
            self.zip(name, data, depth)
            return
        if self.commit_gate_paths and b"\x00" not in data[:4096] and MACHINE_PATH.search(data[:1 << 20].decode("utf-8", "replace")):
            self.problems.append(f"developer-machine path in {name}")

    def pack(self, name: str, data: bytes, base: int, depth: int = 0, end: int | None = None) -> None:
        try:
            entries = pck_entries(data, base, end)
        except (ValueError, struct.error) as e:
            self.problems.append(f"{name}: {e}")
            return
        for path, content in entries:
            self.blob(f"{name}:{path}", content, depth + 1)

    def tar(self, name: str, data: bytes, depth: int = 0) -> None:
        """Every member, then nothing but zero blocks to the end (a second archive or data after the end-of-archive blocks is a finding)."""
        try:
            with tarfile.open(fileobj=io.BytesIO(data), mode="r:") as t:
                members = t.getmembers()
                for m in members:
                    if m.isfile():
                        self.blob(f"{name}:{m.name}", t.extractfile(m).read(), depth + 1)
                    padded = (m.size + 511) // 512 * 512 if m.isfile() or m.size else 0
                    if any(data[m.offset_data + m.size:m.offset_data + padded]):  # review r3: data hidden in a member's padding
                        self.problems.append(f"{name}:{m.name}: non-zero bytes in the member's padding")
                    elif m.issparse():
                        self.problems.append(f"{name}:{m.name}: sparse tar member cannot be audited")
                if not members:
                    self.problems.append(f"{name}: tar header without members")
                last = members[-1] if members else None
                end = (last.offset_data + ((last.size + 511) // 512) * 512) if last else 0
        except (tarfile.TarError, OSError) as e:
            self.problems.append(f"{name}: unreadable tar archive ({e})")
            return
        if any(data[end:]):
            self.problems.append(f"{name}: {len(data) - end} bytes after the tar archive's last member are not zero blocks")

    def zip(self, name: str, data: bytes, depth: int = 0) -> None:
        """Every member, and every byte of the archive explained: the local entries, the central directory and its end records must cover
        the file without gaps (review r2: data outside the listed entries, e.g. an unlisted local entry, was never looked at); bytes before the
        first entry (a self-extracting stub) are a finding too."""
        try:
            with zipfile.ZipFile(io.BytesIO(data)) as z:
                infos = z.infolist()
                for info in infos:
                    if info.flag_bits & 1:
                        self.problems.append(f"{name}:{info.filename}: encrypted ZIP member cannot be audited")
                        continue
                    content, why = zip_member_exact(data, info)
                    if why:
                        self.problems.append(f"{name}:{info.filename}: {why}")
                        continue
                    if info.is_dir():
                        if content:  # review r3: a directory entry with a payload
                            self.problems.append(f"{name}:{info.filename}: a directory entry with {len(content)} bytes of data")
                        continue
                    self.blob(f"{name}:{info.filename}", content, depth + 1)
                start_dir = z.start_dir
        except (zipfile.BadZipFile, zipfile.LargeZipFile, NotImplementedError, OSError, RuntimeError, ValueError, zlib.error) as e:
            self.problems.append(f"{name}: unreadable ZIP archive ({e})")
            return
        try:
            gaps = zip_unexplained(data, infos, start_dir)
        except (ValueError, struct.error) as e:
            gaps = [str(e)]
        for g in gaps:
            self.problems.append(f"{name}: {g}")
        first = min([i.header_offset for i in infos], default=start_dir)
        if first > 0:
            # a self-extracting stub or anything else before the first entry cannot be audited like an archive member: a finding
            self.problems.append(f"{name}: {first} bytes before the first ZIP entry (a ZIP prefix) are not explained")

    def path(self, p: Path) -> None:
        if p.is_dir():
            for f in sorted(p.rglob("*")):
                if f.is_file():
                    self.blob(str(f.relative_to(p.parent)), f.read_bytes())
        else:
            self.blob(p.name, p.read_bytes())


# ---- the release allowlist (review r3: a package is checked against what the release defines, not searched for what it must not hold) ----

# the files of a test package, by platform: name -> kind. Nothing else may be in it, and none of these may be missing.
_GAME_FILES = {
    "linux": {"OpenBFME.x86_64": "elf", "openbfme.linux.template_debug.x86_64.so": "elf", "OpenBFME.pck": "pck",
              "README_TESTERS.txt": "text", "LICENSE": "text", "NOTICE": "text", "VERSION": "text"},
    "windows": {"OpenBFME.exe": "pe", "OpenBFME.console.exe": "pe", "openbfme.windows.template_debug.x86_64.dll": "pe", "OpenBFME.pck": "pck",
                "README_TESTERS.txt": "text", "LICENSE": "text", "NOTICE": "text", "VERSION": "text"},
}
_MAC_GAME_FILES = {
    "OpenBFME.app/Contents/MacOS/OpenBFME": "macho",
    "OpenBFME.app/Contents/Resources/OpenBFME.pck": "pck",
    "OpenBFME.app/Contents/Resources/icon.icns": "binary",
    "OpenBFME.app/Contents/Resources/PrivacyInfo.xcprivacy": "text",
    "OpenBFME.app/Contents/Info.plist": "text",
    "OpenBFME.app/Contents/PkgInfo": "binary",
    "OpenBFME.app/Contents/_CodeSignature/CodeResources": "binary",
    "OpenBFME.app/Contents/Frameworks/openbfme.macos.template_debug.universal.dylib": "macho",
    "README_TESTERS.txt": "text", "LICENSE": "text", "NOTICE": "text", "VERSION": "text",
}
_MAC_LAUNCHER_APP_FILES = {
    "OpenBFMELauncher.app/Contents/MacOS/OpenBFME Launcher": "macho",
    "OpenBFMELauncher.app/Contents/Resources/OpenBFME Launcher.pck": "pck",
    "OpenBFMELauncher.app/Contents/Resources/icon.icns": "binary",
    "OpenBFMELauncher.app/Contents/Resources/PrivacyInfo.xcprivacy": "text",
    "OpenBFMELauncher.app/Contents/Info.plist": "text",
    "OpenBFMELauncher.app/Contents/PkgInfo": "binary",
    "OpenBFMELauncher.app/Contents/_CodeSignature/CodeResources": "binary",
}
_MAC_LAUNCHER_FILES = {
    **_MAC_LAUNCHER_APP_FILES,
    "README.txt": "text", "LICENSE": "text", "NOTICE": "text", "VERSION": "text",
}
RELEASE_FILES = {
    # lane ONEZIP-1: the release's package of a platform is the combined one: the game's files and the OpenBFME Launcher's executable
    # (its pack embedded, checked like the launcher package's) with the launcher's README
    "linux": {**_GAME_FILES["linux"], "OpenBFMELauncher.x86_64": "elf+pck", "README_LAUNCHER.txt": "text"},
    "windows": {**_GAME_FILES["windows"], "OpenBFMELauncher.exe": "pe+pck", "README_LAUNCHER.txt": "text"},
    "macos-universal": {**_MAC_GAME_FILES, **_MAC_LAUNCHER_APP_FILES, "README_LAUNCHER.txt": "text"},
    # lane ONEZIP-1 r2: the signed payload inventory, added by seal_package.py on the machine with the release key (the archives package.sh
    # writes have none yet: OPTIONAL_FILES)
    # Older game-only audit platforms and the explicit combined Apple Silicon preview.
    "game-linux": _GAME_FILES["linux"],
    "game-windows": _GAME_FILES["windows"],
    "game-macos-universal": _MAC_GAME_FILES,
    "game-macos-arm64-unstable-preview": {
        "OpenBFME.app/Contents/MacOS/OpenBFME": "macho-arm64",
        "OpenBFME.app/Contents/Resources/OpenBFME.pck": "pck",
        "OpenBFME.app/Contents/Resources/icon.icns": "binary",
        "OpenBFME.app/Contents/Resources/PrivacyInfo.xcprivacy": "text",
        "OpenBFME.app/Contents/Info.plist": "text",
        "OpenBFME.app/Contents/PkgInfo": "binary",
        "OpenBFME.app/Contents/_CodeSignature/CodeResources": "binary",
        "OpenBFME.app/Contents/Frameworks/openbfme.macos.template_debug.arm64.dylib": "macho-arm64",
        "OpenBFME Launcher.app/Contents/MacOS/OpenBFME Launcher": "macho-arm64",
        "OpenBFME Launcher.app/Contents/Resources/OpenBFME Launcher.pck": "pck",
        "OpenBFME Launcher.app/Contents/Resources/icon.icns": "binary",
        "OpenBFME Launcher.app/Contents/Resources/PrivacyInfo.xcprivacy": "text",
        "OpenBFME Launcher.app/Contents/Info.plist": "text",
        "OpenBFME Launcher.app/Contents/PkgInfo": "binary",
        "OpenBFME Launcher.app/Contents/_CodeSignature/CodeResources": "binary",
        "README-MAC.txt": "text",
        "LICENSE": "text",
        "NOTICE": "text",
        "VERSION": "text",
    },
    # lane LAUNCH-1: the OpenBFME Launcher packages (tools/release/build_launcher.sh)
    # the launcher's pack is embedded in its executable (one file: a self-update replaces it with one rename, Sol r1)
    "launcher-linux": {"OpenBFMELauncher.x86_64": "elf+pck", "README.txt": "text", "LICENSE": "text", "NOTICE": "text", "VERSION": "text"},
    "launcher-windows": {"OpenBFMELauncher.exe": "pe+pck", "README.txt": "text", "LICENSE": "text", "NOTICE": "text", "VERSION": "text"},
    "launcher-macos-universal": _MAC_LAUNCHER_FILES,
}
RELEASE_FILES["macos-arm64"] = {
    **RELEASE_FILES["game-macos-arm64-unstable-preview"],
    "OpenBFME Launcher.app/Contents/MacOS/openbfme-bundle-swap": "macho-arm64",
}
# MACPLAY-1 (Sol r3): every template slot is a file of the releases (a renamed release file cannot keep an exemption it lost)
assert UPSTREAM_TEMPLATE_SLOTS <= {n.split("/")[-1] for files in RELEASE_FILES.values() for n in files}
# MACPLAY-1: the Apple Silicon package is sealed too (seal_package.py), after the Mac build host returned it unsealed
OPTIONAL_FILES = {p: {"payload.json": "text", "payload.json.sig": "sig"} for p in ("linux", "windows", "macos-universal", "macos-arm64")}
TOP_DIR = re.compile(
    r"^openbfme-(?!launcher-)[0-9A-Za-z][0-9A-Za-z.+_-]*-(linux|windows)-x64$|"
    r"^openbfme-(?!launcher-)[0-9A-Za-z][0-9A-Za-z.+_-]*-macos-universal$|"
    r"^openbfme-(?!launcher-)[0-9A-Za-z][0-9A-Za-z.+_-]*-macos-arm64$"
)
PREVIEW_TOP_DIR = "openbfme-v0.4.6-macos-arm64-unstable-preview"
LAUNCHER_TOP_DIR = re.compile(r"^openbfme-launcher-[0-9A-Za-z][0-9A-Za-z.+_-]*-(linux|windows)-x64$|^openbfme-launcher-[0-9A-Za-z][0-9A-Za-z.+_-]*-macos-universal$")

# Owner-approved OpenBFME seal, exported by Godot 4.7.2 from PNG SHA-256
# 32180a4153977ed292bd00626a8232380b929e2dee4a9ee66d4a13b75ab1a48d (POWEREDBY-1).
PINNED_GAME_BRANDING = {
    ".godot/imported/poweredby_openbfme.png-8e49593c1d11f096cae531ecedee93ce.ctex":
        "07492d4d17f1fa16504a97bcf92c71155696e673539e410fed4b5b23d84ccb8e",
    "assets/brand/online_branding.tres":
        "ac35cf8ac2e5ef425f10e15a549ce5a3ff709446bdf07d1ee347b1ed99f43ac9",
    "assets/brand/poweredby_openbfme.png.import":
        "54674bc24f279dc10093a40b14992eb68beee5d6188c3b37027b2eb6e8e4c5b3",
}

# DEVPORT-6: PLATFORMICON-1's lobby icons (godot/assets/platform_icons, our own glyphs from tools/platformicon/make_icons.py, pinned in
# public_binaries.txt) as Godot 4.7.2 exports them: the same bytes from the Linux x64 and the macOS Apple Silicon presets (measured on
# a Linux x64 CI host and the Apple Silicon Mac, 2026-10-16). Without them every game pack since PLATFORMICON-1 failed this allowlist (the archive's next
# Windows / Linux release and the first Mac package build).
PINNED_GAME_ICONS = {
    ".godot/imported/linux.png-e3b3f6dbbb74c608680942a1b07c4593.ctex":
        "2810e4eb3887d6c0e5f1184ea254176c36d9b737e7a3c0f56dffe558cdc2c3dc",
    ".godot/imported/macos.png-40b991ba36498511f07038e54095534b.ctex":
        "4f74bba0de07d67bef95977890d2274e1103dbe4598e371199690c59d66237d2",
    ".godot/imported/windows.png-d254cfae9b851ca9c851d4089a8e9ea3.ctex":
        "e26a38cfaeeb5d1214725310b3014ba579fa2b1ac28d331af9954b2b8c1c1791",
    "assets/platform_icons/linux.png.import":
        "a0fc160c61169b175b3bff1e9d9c89e8e3f8d31a9cb6e9ac71b7ac277871ca44",
    "assets/platform_icons/macos.png.import":
        "1bbc06e574486fa9a1bd7dc8af98c9f92aab0d01e37d635a968b533173632190",
    "assets/platform_icons/windows.png.import":
        "8a9c65f2799985c024f5d632b1c450420b202096bb6a5d32ca9d674bded99462",
}

# the Godot resource types of this project a pack may hold (godot/export_presets.cfg exports scripts as text), each with its format check
PCK_RULES = [
    *((re.compile(re.escape(path) + r"\Z"), "brand") for path in PINNED_GAME_BRANDING),
    *((re.compile(re.escape(path) + r"\Z"), "icon") for path in PINNED_GAME_ICONS),
    (re.compile(r"^\.godot/exported/\d+/export-[0-9a-f]{32}-[\w.-]+\.scn$"), "rsrc"),
    (re.compile(r"^(scripts|tests)/[\w/.-]+\.gd$"), "text"),
    (re.compile(r"^(scenes|tests)/[\w/.-]+\.tscn$"), "scene"),  # scenes ship as their committed text (project.godot: no binary conversion); tests/ holds a test scene beside the test scripts above
    (re.compile(r"^[\w/.-]+\.(tscn|tres)\.remap$"), "remap"),
    (re.compile(r"^shaders/[\w/.-]+\.(gdshader|gdshaderinc)$"), "text"),
    (re.compile(r"^\.godot/(extension_list|global_script_class_cache)\.cfg$"), "text"),
    (re.compile(r"^\.godot/uid_cache\.bin$"), "uidcache"),
    (re.compile(r"^project\.binary$"), "ecfg"),
    (re.compile(r"^openbfme\.gdextension$"), "text"),
    (re.compile(r"^tests/[\w.-]+\.txt$"), "text"),
]
# the launcher's pack (launcher/export_presets.cfg): its scripts and scene as text, the project settings, the UID and class caches and the
# build information build_launcher.sh writes; no tests, no GDExtension
LAUNCHER_PCK_RULES = [
    (re.compile(r"^scripts/[\w/.-]+\.gd$"), "text"),
    (re.compile(r"^scenes/[\w/.-]+\.tscn$"), "scene"),
    (re.compile(r"^[\w/.-]+\.(tscn|tres)\.remap$"), "remap"),
    (re.compile(r"^\.godot/(extension_list|global_script_class_cache)\.cfg$"), "text"),
    (re.compile(r"^\.godot/uid_cache\.bin$"), "uidcache"),
    (re.compile(r"^project\.binary$"), "ecfg"),
    (re.compile(r"^build_info\.json$"), "text"),
    (re.compile(r"^README_LAUNCHER_MACOS\.txt$"), "text"),
    # lane UI-5: the launcher's OFL fonts, exported as their own bytes (importer "keep"), each exactly a file public_binaries.txt pins
    # under launcher/fonts/, and their licences
    (re.compile(r"^fonts/[\w-]+\.ttf$"), "font"),
    (re.compile(r"^fonts/OFL-[\w-]+\.txt$"), "text"),
]
PUBLIC_BINARIES = Path(__file__).resolve().parent / "public_binaries.txt"
ICON_DIR = "assets/platform_icons"   # godot/assets/platform_icons in the repository (PLATFORMICON-1)


def pinned_icons() -> dict[str, str]:
    """{file name: sha256} of PLATFORMICON-1's icons that tools/release/public_binaries.txt lists (godot/assets/platform_icons/<name>.png)"""
    out = {}
    for line in PUBLIC_BINARIES.read_text(encoding="utf-8").splitlines():
        m = re.fullmatch(rf"([0-9a-f]{{64}})  godot/{ICON_DIR}/([\w-]+\.png)  \S.*", line)
        if m:
            out[m.group(2)] = m.group(1)
    return out


def icon_import_paths(name: str) -> tuple[str, str]:
    """(the .import file, the imported texture) Godot 4.7's export writes for res://assets/platform_icons/<name>: the texture importer's
    <file>-<md5 of its res:// path>.ctex under .godot/imported (checked on the v0.4.7 package)"""
    res = f"res://{ICON_DIR}/{name}"
    return f"{ICON_DIR}/{name}.import", f".godot/imported/{name}-{hashlib.md5(res.encode()).hexdigest()}.ctex"


def pinned_fonts() -> dict[str, str]:
    """{file name: sha256} of the launcher's fonts that tools/release/public_binaries.txt lists (launcher/fonts/<name>.ttf)"""
    out = {}
    for line in PUBLIC_BINARIES.read_text(encoding="utf-8").splitlines():
        m = re.fullmatch(r"([0-9a-f]{64})  launcher/fonts/([\w-]+\.ttf)  \S.*", line)
        if m:
            out[m.group(2)] = m.group(1)
    return out
# signatures that may not appear ANYWHERE inside a pack entry or a text file: retail formats and containers / compressed streams of any kind
EMBEDDED_SIGNATURES = {b"BIGF": "BIG archive", b"BIG4": "BIG archive", b"DDS |": "DDS texture", b"WAVEfmt": "RIFF/WAVE audio",
                       b"ID3\x03": "ID3 audio", b"ID3\x04": "ID3 audio", b"PK\x03\x04": "ZIP entry", b"PK\x05\x06": "ZIP directory",
                       b"\x1f\x8b\x08": "gzip stream", b"BZh91AY&SY": "bzip2 stream", b"\xfd7zXZ\x00": "xz stream",
                       b"7z\xbc\xaf\x27\x1c": "7-Zip archive", b"\x28\xb5\x2f\xfd": "Zstandard stream", b"Rar!\x1a\x07": "RAR archive",
                       b"MSCF": "CAB archive", b"\x04\x22\x4d\x18": "LZ4 stream", b"GDPC": "Godot pack", b"RSCC": "compressed Godot resource",
                       b"ustar": "tar archive", **{m: "PDB / MSF debug symbols" for m in MSF_MAGICS}}


def embedded_signatures(data: bytes, text: bool = False) -> list[str]:
    """The signatures found anywhere in `data`. In a text file (already checked by text_problem: UTF-8, no control characters) a signature
    made of printable characters only ("BIG4" in prose) cannot start binary content, so only the ones with control bytes are looked for."""
    return sorted({what for sig, what in EMBEDDED_SIGNATURES.items()
                   if (not text or any(b < 0x20 or b >= 0x7F for b in sig)) and sig in data})


MAX_TEXT = 512 << 10  # a text file of the package (README, licence, a script) is small; a large one is a finding (256 KiB until 2026-10-15: godot/scripts/game.gd passed it at 259 KiB)


def text_problem(data: bytes) -> str:
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError as e:
        return f"not UTF-8 text ({e})"
    bad = [c for c in text if ord(c) < 32 and c not in "\t\r\n"]
    return f"control characters in text ({len(bad)})" if bad else ""


def elf_extent(b: bytes) -> int:
    """The bytes an ELF64 little-endian file's headers account for: program segments, sections (not NOBITS) and the section header table."""
    if b[:4] != b"\x7fELF" or b[4] != 2 or b[5] != 1:
        raise ValueError("not a 64-bit little-endian ELF file")
    phoff, shoff = struct.unpack_from("<QQ", b, 32)
    phentsize, phnum, shentsize, shnum = struct.unpack_from("<HHHH", b, 54)
    end = max(64, phoff + phnum * phentsize, shoff + shnum * shentsize)
    for i in range(phnum):
        off, = struct.unpack_from("<Q", b, phoff + i * phentsize + 8)
        filesz, = struct.unpack_from("<Q", b, phoff + i * phentsize + 32)
        end = max(end, off + filesz)
    for i in range(shnum):
        o = shoff + i * shentsize
        typ, = struct.unpack_from("<I", b, o + 4)
        off, size = struct.unpack_from("<QQ", b, o + 24)
        if typ != 8:
            end = max(end, off + size)
    return end


def binary_problems(name: str, kind: str, b: bytes) -> list[str]:
    """An executable or library: the right format, nothing appended after what its headers describe (no embedded pack, archive or payload),
    and the retail-content check on its first bytes."""
    problems = []
    kind_found = retail_content(b)
    if kind_found:
        problems.append(f"{name}: retail-format bytes ({kind_found})")
    try:
        if kind == "macho-arm64":
            end = macho_arm64_extent(b)
            if end != len(b):
                problems.append(f"{name}: {len(b) - end} bytes after the Mach-O image" if end < len(b) else f"{name}: truncated Mach-O")
            return problems
        if kind in ("macho", "macho+pck"):
            end = macho_extent(b)
            if end != len(b):
                problems.append(f"{name}: {len(b) - end} bytes after the Mach-O image" if end < len(b) else f"{name}: truncated Mach-O")
            return problems
        if kind == "elf":
            end = elf_extent(b)
            if end != len(b):
                problems.append(f"{name}: {len(b) - end} bytes after the ELF image (appended data)" if end < len(b) else f"{name}: truncated ELF")
        else:
            import pefile  # noqa: PLC0415 (only for Windows packages)
            pe = pefile.PE(data=b, fast_load=True)
            overlay = pe.get_overlay_data_start_offset()
            if overlay is not None:
                problems.append(f"{name}: {len(b) - overlay} bytes after the PE image (an overlay)")
    except (ValueError, struct.error, Exception) as e:  # pefile raises its own PEFormatError
        problems.append(f"{name}: not a valid {kind.upper()} file ({e})")
    if embedded_pck_base(b) is not None:
        problems.append(f"{name}: a Godot pack is embedded")
    return problems


def macho_extent(b: bytes) -> int:
    """Check the universal Mach-O header, slice bounds, and load-command bounds."""
    if len(b) < 8 or b[:4] != b"\xca\xfe\xba\xbe":
        raise ValueError("not a fat Mach-O")
    n, = struct.unpack_from(">I", b, 4)
    if n != 2 or len(b) < 8 + n * 20:
        raise ValueError("fat Mach-O must contain two architecture records")
    wanted = {0x01000007, 0x0100000c}
    got = set()
    end = 8 + n * 20
    for i in range(n):
        cpu, _sub, off, size, align = struct.unpack_from(">IIIII", b, 8 + i * 20)
        if cpu not in wanted or off < end or size == 0 or off + size > len(b) or align > 31:
            raise ValueError("fat Mach-O architecture bounds are invalid")
        if off % (1 << align):
            raise ValueError("fat Mach-O slice is not aligned as recorded")
        if any(b[end:off]):
            raise ValueError("fat Mach-O slice padding is not zero")
        got.add(cpu)
        end = max(end, off + size)
        thin = b[off:off + size]
        if thin[:4] not in (b"\xcf\xfa\xed\xfe", b"\xfe\xed\xfa\xcf"):
            raise ValueError("fat Mach-O contains a non-64-bit slice")
        if len(thin) < 32:
            raise ValueError("fat Mach-O slice header is truncated")
        endian = "<" if thin[:4] == b"\xcf\xfa\xed\xfe" else ">"
        thin_cpu, = struct.unpack_from(endian + "I", thin, 4)
        if thin_cpu != cpu:
            raise ValueError("fat Mach-O CPU type does not match its thin slice")
        ncmds, sizeofcmds = struct.unpack_from(endian + "II", thin, 16)
        pos = 32
        command_limit = 32 + sizeofcmds
        if command_limit > len(thin):
            raise ValueError("Mach-O load commands exceed slice bounds")
        image_end = command_limit
        for _ in range(ncmds):
            if pos + 8 > command_limit:
                raise ValueError("Mach-O load command is truncated")
            cmd, cmdsize = struct.unpack_from(endian + "II", thin, pos)
            if cmdsize < 8 or cmdsize % 8 or pos + cmdsize > command_limit:
                raise ValueError("Mach-O load command bounds are invalid")
            if cmd == 0x19:  # LC_SEGMENT_64
                if cmdsize < 72:
                    raise ValueError("Mach-O LC_SEGMENT_64 command is truncated")
                fileoff, filesize = struct.unpack_from(endian + "QQ", thin, pos + 40)
                if fileoff + filesize > len(thin):
                    raise ValueError("Mach-O segment exceeds its slice")
                image_end = max(image_end, fileoff + filesize)
            pos += cmdsize
        if pos != command_limit:
            raise ValueError("Mach-O load command sizes do not sum to sizeofcmds")
        if image_end != len(thin):
            raise ValueError("Mach-O load commands exceed slice")
    if got != wanted:
        raise ValueError("fat Mach-O must contain arm64 and x86_64")
    return end


def macho_arm64_extent(b: bytes) -> int:
    """Check a thin 64-bit arm64 Mach-O header, load commands, segments and image extent."""
    if len(b) < 32 or b[:4] != b"\xcf\xfa\xed\xfe":
        raise ValueError("not a thin 64-bit little-endian Mach-O")
    cpu, = struct.unpack_from("<I", b, 4)
    if cpu != 0x0100000c:
        raise ValueError("Mach-O CPU type is not arm64")
    ncmds, sizeofcmds = struct.unpack_from("<II", b, 16)
    pos = 32
    command_limit = 32 + sizeofcmds
    if sizeofcmds % 8 or command_limit > len(b):
        raise ValueError("Mach-O load commands exceed file bounds")
    image_end = command_limit
    for _ in range(ncmds):
        if pos + 8 > command_limit:
            raise ValueError("Mach-O load command is truncated")
        cmd, cmdsize = struct.unpack_from("<II", b, pos)
        if cmdsize < 8 or cmdsize % 8 or pos + cmdsize > command_limit:
            raise ValueError("Mach-O load command bounds are invalid")
        if cmd == 0x19:
            if cmdsize < 72:
                raise ValueError("Mach-O LC_SEGMENT_64 command is truncated")
            fileoff, filesize = struct.unpack_from("<QQ", b, pos + 40)
            if fileoff + filesize > len(b):
                raise ValueError("Mach-O segment exceeds file bounds")
            image_end = max(image_end, fileoff + filesize)
        pos += cmdsize
    if pos != command_limit:
        raise ValueError("Mach-O load command sizes do not sum to sizeofcmds")
    return image_end


def embedded_pack_problems(
    name: str,
    kind: str,
    b: bytes,
    forbidden_prefixes: tuple[str, ...] = (),
) -> list[str]:
    """The launcher's executable with its pack embedded the way Godot 4.7's export does it: the pack is the executable's last section
    (ELF: covered by the section table; PE: a section named "pck", no overlay), followed by the <u64 pack size> "GDPC" trailer inside it;
    every byte of the file is the image or the pack, and the pack holds only the launcher's resource types (LAUNCHER_PCK_RULES)."""
    problems = []
    found = retail_content(b)
    if found:
        problems.append(f"{name}: retail-format bytes ({found})")
    base = embedded_pck_base(b)
    if base is None:
        return problems + [f"{name}: no embedded Godot pack"]
    try:
        if kind == "elf+pck":
            if elf_extent(b) != len(b):
                problems.append(f"{name}: bytes outside what the ELF headers describe")
        elif kind == "macho+pck":
            if macho_extent(b[:base]) != base:
                problems.append(f"{name}: bytes outside what the Mach-O headers describe before the pack")
        else:
            import pefile  # noqa: PLC0415
            pe = pefile.PE(data=b, fast_load=True)
            if pe.get_overlay_data_start_offset() is not None:
                problems.append(f"{name}: bytes after the PE image (an overlay)")
            last = pe.sections[-1]
            if last.Name.rstrip(b"\0") != b"pck" or last.PointerToRawData != base or last.PointerToRawData + last.SizeOfRawData != len(b):
                problems.append(f"{name}: the pack is not exactly the last PE section")
    except (ValueError, struct.error, Exception) as e:
        problems.append(f"{name}: not a valid {kind.split('+')[0].upper()} file ({e})")
    problems += pck_release_problems(
        f"{name}:pck", b[base:len(b) - 12], LAUNCHER_PCK_RULES, forbidden_prefixes,
    )
    if embedded_pck_base(b[:base]) is not None:
        problems.append(f"{name}: a second pack is embedded")
    return problems


def uid_cache_problem(b: bytes) -> str:
    """ResourceUID's cache: u32 count, then per entry i64 id, u32 length, the path; exactly to the end."""
    try:
        count, = struct.unpack_from("<I", b, 0)
        pos = 4
        for _ in range(count):
            _uid, n = struct.unpack_from("<qI", b, pos)
            path = b[pos + 12:pos + 12 + n]
            if len(path) != n or not path.startswith(b"res://"):
                return "a malformed entry"
            pos += 12 + n
        return "" if pos == len(b) else f"{len(b) - pos} bytes after the last entry"
    except struct.error:
        return "truncated"


def ecfg_problem(b: bytes) -> str:
    """project.binary: "ECFG", u32 count, then per setting a length-prefixed key and a length-prefixed value; exactly to the end."""
    if b[:4] != b"ECFG":
        return "no ECFG signature"
    try:
        count, = struct.unpack_from("<I", b, 4)
        pos = 8
        for _ in range(count):
            n, = struct.unpack_from("<I", b, pos)
            pos += 4 + n
            m, = struct.unpack_from("<I", b, pos)
            pos += 4 + m
        return "" if pos == len(b) else f"{len(b) - pos} bytes after the last setting"
    except struct.error:
        return "truncated"


def pck_release_problems(
    name: str,
    data: bytes,
    rules: list | None = None,
    forbidden_prefixes: tuple[str, ...] = (),
) -> list[str]:
    rules = PCK_RULES if rules is None else rules
    try:
        entries = pck_entries(data)
    except (ValueError, struct.error) as e:
        return [f"{name}: {e}"]
    problems = []
    for path, content in entries:
        where = f"{name}:{path}"
        path_problem = developer_path_problem(content, forbidden_prefixes)
        if path_problem:
            problems.append(f"{where}: {path_problem}")
        rule = next((kind for rx, kind in rules if rx.match(path)), None)
        if rule is None:
            problems.append(f"{where}: not a resource type this project exports")
            continue
        if rule == "brand" and hashlib.sha256(content).hexdigest() != PINNED_GAME_BRANDING[path]:
            problems.append(f"{where}: not the hash-pinned owner artwork resource")
        if rule == "icon" and hashlib.sha256(content).hexdigest() != PINNED_GAME_ICONS[path]:
            problems.append(f"{where}: not the hash-pinned platform icon resource")
        kind = retail_content(content)
        if kind:
            problems.append(f"{where}: retail-format bytes ({kind})")
        if aio_file(content):
            problems.append(f"{where}: a game file the AIO service distributes ({aio_file(content)})")
        for what in embedded_signatures(content, text=rule in ("text", "remap", "scene")):
            problems.append(f"{where}: contains a {what} signature")
        if rule in ("text", "remap", "scene") and len(content) > MAX_TEXT:
            problems.append(f"{where}: {len(content)} bytes of text (more than {MAX_TEXT})")
        if rule == "rsrc" and not content.startswith(b"RSRC"):
            problems.append(f"{where}: not a binary Godot resource (RSRC)")
        elif rule == "rsrc" and not content.endswith(b"RSRC"):
            # ResourceFormatSaverBinary ends every file with the "RSRC" footer: bytes after it are no part of the resource (review r4)
            tail = len(content) - content.rfind(b"RSRC") - 4 if content.rfind(b"RSRC") > 0 else len(content)
            problems.append(f"{where}: {tail} bytes after the resource's RSRC footer")
        elif rule in ("text", "remap", "scene"):
            t = text_problem(content)
            if t:
                problems.append(f"{where}: {t}")
            elif rule == "remap" and not content.startswith(b"[remap]"):
                problems.append(f"{where}: not a remap file")
            elif rule == "scene" and not content.startswith(b"[gd_scene "):
                problems.append(f"{where}: not a text scene ([gd_scene ...] header)")
        elif rule == "font":
            # lane UI-5: an OpenType/TrueType font (sfnt 1.0), byte for byte the pinned file of that name
            want = pinned_fonts().get(path.split("/")[-1])
            if not content.startswith(b"\x00\x01\x00\x00"):
                problems.append(f"{where}: not a TrueType font")
            elif want is None or hashlib.sha256(content).hexdigest() != want:
                problems.append(f"{where}: not a font tools/release/public_binaries.txt pins")
        elif rule == "uidcache":
            t = uid_cache_problem(content)
            if t:
                problems.append(f"{where}: {t}")
        elif rule == "ecfg":
            t = ecfg_problem(content)
            if t:
                problems.append(f"{where}: {t}")
    return problems


def release_file_problems(
    platform: str,
    files: dict[str, bytes],
    where: str,
    forbidden_prefixes: tuple[str, ...] = (),
) -> list[str]:
    """The files of one package (names relative to its top folder) against the allowlist."""
    allowed = dict(RELEASE_FILES[platform])
    optional = OPTIONAL_FILES.get(platform, {})
    allowed.update(optional)
    problems = [f"{where}: {n} is not a file of the release" for n in sorted(set(files) - set(allowed))]
    problems += [f"{where}/{n}: a game file the AIO service distributes ({aio_file(files[n])})" for n in sorted(files) if aio_file(files[n])]
    problems += [f"{where}: {n} is missing" for n in sorted(set(allowed) - set(files) - set(optional))]
    if (set(optional) & set(files)) and set(optional) - set(files):
        problems.append(f"{where}: {' and '.join(sorted(optional))} come together")
    for n in sorted(set(files) & set(allowed)):
        kind, data = allowed[n], files[n]
        is_mac_arm64 = platform in ("macos-arm64", "game-macos-arm64-unstable-preview")
        if is_mac_arm64:
            path_problem = developer_path_problem(data, forbidden_prefixes, template_slot=n in UPSTREAM_TEMPLATE_SLOTS)
            if path_problem:
                problems.append(f"{where}/{n}: {path_problem}")
        if kind == "sig":
            if len(data) != 64:
                problems.append(f"{where}/{n}: {len(data)} bytes (an Ed25519 signature is 64)")
            continue
        if kind in ("elf", "pe", "macho", "macho+pck", "macho-arm64"):
            if kind == "macho+pck":
                problems += embedded_pack_problems(
                    f"{where}/{n}", kind, data, forbidden_prefixes if is_mac_arm64 else (),
                )
                continue
            problems += binary_problems(f"{where}/{n}", kind, data)
        elif kind in ("elf+pck", "pe+pck"):
            problems += embedded_pack_problems(
                f"{where}/{n}", kind, data, forbidden_prefixes if is_mac_arm64 else (),
            )
        elif kind == "pck":
            launcher_pack = platform.startswith("launcher-") or (
                platform == "macos-universal" and n.startswith("OpenBFMELauncher.app/")
            ) or (
                platform in ("game-macos-arm64-unstable-preview", "macos-arm64")
                and n.startswith("OpenBFME Launcher.app/")
            )
            problems += pck_release_problems(
                f"{where}/{n}", data, LAUNCHER_PCK_RULES if launcher_pack else PCK_RULES,
                forbidden_prefixes if is_mac_arm64 else (),
            )
        elif kind == "binary":
            t = retail_content(data)
            if t:
                problems.append(f"{where}/{n}: retail-format bytes ({t})")
            for what in embedded_signatures(data):
                problems.append(f"{where}/{n}: contains a {what} signature")
        else:
            t = text_problem(data) or retail_content(data)
            if t:
                problems.append(f"{where}/{n}: {t}")
            for what in embedded_signatures(data, text=True):
                problems.append(f"{where}/{n}: contains a {what} signature")
            if len(data) > MAX_TEXT:
                problems.append(f"{where}/{n}: {len(data)} bytes of text (more than {MAX_TEXT})")
            if not is_mac_arm64 and MACHINE_PATH.search(data.decode("utf-8", "replace")):
                problems.append(f"{where}/{n}: developer-machine path")
    return problems


TAR_FIELDS = [(0, 100, "name"), (100, 108, "mode"), (108, 116, "uid"), (116, 124, "gid"), (124, 136, "size"), (136, 148, "mtime"),
              (148, 156, "checksum"), (156, 157, "type"), (157, 257, "linkname"), (257, 265, "magic"), (265, 297, "uname"),
              (297, 329, "gname"), (329, 345, "device"), (345, 512, "prefix / reserved")]


def expected_tar_header(h: bytes, name: str, typ: bytes, size: int) -> bytes:
    """The header tools/release/make_archive.py writes for this member (name, type, size, mode and mtime as found; everything else fixed):
    a byte in any other field (linkname, user names, device numbers, padding: review r4) makes the header differ."""
    info = tarfile.TarInfo(name)
    info.size = size if typ != b"5" else 0
    info.type = tarfile.DIRTYPE if typ == b"5" else tarfile.REGTYPE
    info.mode = int(h[100:108].replace(b"\0", b" ").strip() or b"0", 8) & 0o7777
    info.mtime = int(h[136:148].replace(b"\0", b" ").strip() or b"0", 8)
    info.uid = info.gid = 0
    info.uname = info.gname = ""
    return info.tobuf(format=tarfile.GNU_FORMAT, encoding="utf-8", errors="surrogateescape")[:512]


def read_release_tar_gz(data: bytes, where: str) -> tuple[str | None, dict[str, bytes], list[str]]:
    """A package .tar.gz, strictly: exactly one gzip member with nothing after it, then a tar of the top folder and regular files only,
    every header valid, every member's padding zero, and nothing but zero blocks after the last member."""
    problems: list[str] = []
    try:
        members = decompress_members("gzip", data)
    except (ValueError, zlib.error) as e:
        return None, {}, [f"{where}: {e}"]
    if len(members) != 1:
        return None, {}, [f"{where}: {len(members)} gzip members (one is written)"]
    tar = members[0]
    files: dict[str, bytes] = {}
    top = None
    pos = 0
    zero = bytes(512)
    while True:
        if pos + 512 > len(tar):
            problems.append(f"{where}: the tar ends without its end-of-archive blocks")
            break
        h = tar[pos:pos + 512]
        if h == zero:
            rest = tar[pos:]
            if any(rest) or len(rest) < 1024:
                problems.append(f"{where}: data after the tar's end-of-archive block" if any(rest) else f"{where}: one end-of-archive block")
            break
        if not is_tar(h) or h[257:263] not in (b"ustar\x00", b"ustar "):
            problems.append(f"{where}: a malformed tar header at {pos}")
            break
        name = h[0:100].split(b"\0", 1)[0].decode("utf-8", "replace")
        if h[257:263] == b"ustar\x00" and h[345]:
            name = h[345:500].split(b"\0", 1)[0].decode("utf-8", "replace") + "/" + name
        typ = h[156:157]
        size = int(h[124:136].replace(b"\0", b" ").strip() or b"0", 8)
        expect = expected_tar_header(h, name, typ, size)
        if h != expect:
            changed = [label for (a, b, label) in TAR_FIELDS if h[a:b] != expect[a:b]]
            problems.append(f"{where}:{name}: tar header fields {', '.join(changed) or 'padding'} are not what the packager writes")
        body = tar[pos + 512:pos + 512 + size]
        padded = (size + 511) // 512 * 512
        if any(tar[pos + 512 + size:pos + 512 + padded]):
            problems.append(f"{where}:{name}: non-zero bytes in the member's padding")
        parts = name.rstrip("/").split("/")
        if typ == b"5":
            if len(parts) != 1 or top is not None or size:
                problems.append(f"{where}: unexpected directory {name}")
            top = parts[0]
        elif typ in (b"0", b"\0"):
            if len(parts) != 2 or (top is not None and parts[0] != top):
                problems.append(f"{where}: {name} is not a file of the package folder")
            else:
                top = top or parts[0]
                if parts[1] in files:
                    problems.append(f"{where}: {name} appears twice")
                files[parts[1]] = body
        else:
            problems.append(f"{where}: tar member {name} of type {typ!r} (only the folder and regular files are written)")
        pos += 512 + padded
    return top, files, problems


def read_release_zip(data: bytes, where: str, nested: bool = False) -> tuple[str | None, dict[str, bytes], list[str]]:
    """A package .zip, strictly: no prefix, no comment, no directory entries, stored or deflated members whose compressed data decodes
    exactly to the member (no unused tail), and nothing outside the entries and the central directory."""
    try:
        z = zipfile.ZipFile(io.BytesIO(data))
        infos = z.infolist()
        start_dir = z.start_dir
    except (zipfile.BadZipFile, zipfile.LargeZipFile, OSError, ValueError) as e:
        return None, {}, [f"{where}: unreadable ZIP archive ({e})"]
    problems = [f"{where}: {g}" for g in zip_unexplained(data, infos, start_dir)]
    if z.comment:
        problems.append(f"{where}: an archive comment")
    if min([i.header_offset for i in infos], default=start_dir) != 0:
        problems.append(f"{where}: bytes before the first entry")
    files: dict[str, bytes] = {}
    top = None
    for info in infos:
        name = info.filename[:-1] if info.filename.endswith("/") else info.filename
        parts = name.split("/")
        if name.startswith("/") or "\\" in name or any(part in ("", ".", "..") for part in parts):
            problems.append(f"{where}:{info.filename}: unsafe member path")
            continue
        if info.filename.endswith("/") or info.is_dir():
            _, why = zip_member_exact(data, info)
            if why:
                problems.append(f"{where}:{info.filename}: {why}")
            problems.append(f"{where}: directory entry {info.filename} (none are written)")
            continue
        mode = (info.external_attr >> 16) & 0xFFFF
        if info.create_system == 3 and stat.S_IFMT(mode) not in (0, stat.S_IFREG):
            problems.append(f"{where}:{info.filename}: a link or another special file")
            continue
        if info.flag_bits & 1:
            problems.append(f"{where}:{info.filename}: an encrypted member")
            continue
        content, why = zip_member_exact(data, info)
        if why:
            problems.append(f"{where}:{info.filename}: {why}")
            continue
        n, m = struct.unpack_from("<HH", data, info.header_offset + 26)
        if info.extra or m or info.comment:  # make_archive.py writes none (review r4: payloads in extra fields and entry comments)
            problems.append(f"{where}:{info.filename}: extra fields or an entry comment (none are written)")
        if (len(parts) < 2 if nested else len(parts) != 2) or (top is not None and parts[0] != top):
            problems.append(f"{where}: {info.filename} is not a file of the package folder")
            continue
        top = top or parts[0]
        rel = "/".join(parts[1:])
        if rel in files:
            problems.append(f"{where}: {info.filename} appears twice")
        files[rel] = content
    return top, files, problems


def check_release(platform: str, path: Path, forbidden_prefixes: tuple[str, ...] = ()) -> list[str]:
    """A package folder, a package archive, or a SHA256SUMS file (with its archives next to it). `platform`: linux, windows,
    launcher-linux or launcher-windows."""
    where = path.name
    base = platform.removeprefix("launcher-").removeprefix("game-")
    if path.is_dir():
        files, problems = {}, []
        for f in sorted(path.rglob("*")):
            rel = f.relative_to(path)
            if f.is_symlink() or not (f.is_file() or f.is_dir()):
                problems.append(f"{where}: {rel} is not a regular file")
            elif f.is_dir():
                if base not in ("macos-universal", "macos-arm64-unstable-preview", "macos-arm64"):
                    problems.append(f"{where}: {rel} is not a file of the release")
            else:
                if len(rel.parts) != 1 and base not in ("macos-universal", "macos-arm64-unstable-preview", "macos-arm64"):
                    problems.append(f"{where}: {rel} is not a file of the release")
                else:
                    files["/".join(rel.parts)] = f.read_bytes()
        top = path.name
    elif path.name.startswith("SHA256SUMS"):
        return sums_problems(platform, path)
    else:
        data = path.read_bytes()
        if path.name.endswith(".tar.gz") and base == "linux":
            top, files, problems = read_release_tar_gz(data, where)
        elif path.name.endswith(".zip") and base in ("windows", "macos-universal", "macos-arm64-unstable-preview", "macos-arm64"):
            top, files, problems = read_release_zip(
                data, where, nested=base in ("macos-universal", "macos-arm64-unstable-preview", "macos-arm64"),
            )
        else:
            return [f"{where}: not a {platform} package archive"]
        if top and path.name != top + (".tar.gz" if base == "linux" else ".zip"):
            problems.append(f"{where}: the folder inside is {top}")
    top_rx = LAUNCHER_TOP_DIR if platform.startswith("launcher-") else TOP_DIR
    if base == "macos-arm64-unstable-preview":
        top_valid = top == PREVIEW_TOP_DIR
    else:
        suffix = f"-{base}-x64" if base in ("linux", "windows") else f"-{base}"
        top_valid = bool(top and top_rx.match(top) and top.endswith(suffix))
    if not top or not top_valid:
        kind = "openbfme-launcher" if platform.startswith("launcher-") else "openbfme"
        problems.append(f"{where}: the package folder {top!r} is not {kind}-<version>-{base}-x64")
    return problems + release_file_problems(platform, files, where, forbidden_prefixes)


def sums_problems(platform: str, path: Path, expected: list[str] | None = None) -> list[str]:
    """SHA256SUMS-<version>.txt: only "<64 hex>  <archive>" lines, each archive present next to it with that digest, and every package
    archive of its folder (or `expected`) listed exactly once (review r4: an empty checksum file passed)."""
    import hashlib  # noqa: PLC0415
    problems = []
    raw = path.read_bytes()
    t = text_problem(raw)
    if t:
        return [f"{path.name}: {t}"]
    listed: list[str] = []
    for line in raw.decode("utf-8").splitlines():
        # MACREL-1: the release's Mac package (openbfme-<v>-macos-arm64.zip) is listed with the others
        m = re.match(r"^([0-9a-f]{64})  (openbfme-[\w.+-]+-(linux|windows)-x64\.(tar\.gz|zip)|openbfme-[\w.+-]+-macos-(universal|arm64)\.zip)$", line)
        if not m:
            problems.append(f"{path.name}: unexpected line {line[:80]!r}")
            continue
        listed.append(m.group(2))
        archive = path.parent / m.group(2)
        if not archive.is_file():
            problems.append(f"{path.name}: {m.group(2)} is missing")
        elif hashlib.sha256(archive.read_bytes()).hexdigest() != m.group(1):
            problems.append(f"{path.name}: the checksum of {m.group(2)} does not match")
    if not raw.endswith(b"\n") and raw:
        problems.append(f"{path.name}: the last line has no newline")
    if expected is None:
        expected = sorted(p.name for p in path.parent.iterdir()
                          if re.match(r"^openbfme-[\w.+-]+-(linux|windows)-x64\.(tar\.gz|zip)$|^openbfme-[\w.+-]+-macos-(universal|arm64)\.zip$", p.name))
    for a in sorted(set(listed)):
        if listed.count(a) > 1:
            problems.append(f"{path.name}: {a} is listed {listed.count(a)} times")
    for a in sorted(set(expected) - set(listed)):
        problems.append(f"{path.name}: {a} is not listed")
    for a in sorted(set(listed) - set(expected)):
        problems.append(f"{path.name}: {a} is listed but not an archive of this release")
    if not listed:
        problems.append(f"{path.name}: lists no archive")
    return problems


def main(argv: list[str]) -> int:
    forbidden_prefixes = []
    while len(argv) >= 2 and argv[0] == "--forbid-prefix":
        forbidden_prefixes.append(argv[1])
        argv = argv[2:]
    if not argv:
        print(__doc__)
        return 2
    if argv[0] == "--sums":
        if len(argv) < 3:
            print("usage: audit_package.py --sums <SHA256SUMS file> <archive name> ...")
            return 2
        problems = sums_problems("", Path(argv[1]), expected=argv[2:])
        for p in problems:
            print(" -", p)
        print("checksum file ok" if not problems else "CHECKSUM FILE FAILED")
        return 1 if problems else 0
    if argv[0] == "--release":
        if len(argv) < 3 or argv[1] not in RELEASE_FILES:
            print("usage: audit_package.py --release linux|windows|macos-universal|game-linux|game-windows|game-macos-universal|game-macos-arm64-unstable-preview|macos-arm64|launcher-linux|launcher-windows|launcher-macos-universal <package folder | archive | SHA256SUMS file> ...")
            return 2
        problems = []
        for a in argv[2:]:
            problems += check_release(argv[1], Path(a), tuple(forbidden_prefixes))
        if problems:
            print("RELEASE ALLOWLIST FAILED")
            for p in problems:
                print(" -", p)
            return 1
        print(f"release allowlist ok ({len(argv) - 2} item(s), {argv[1]})")
        return 0
    audit = Audit(tuple(forbidden_prefixes))
    for a in argv:
        audit.path(Path(a))
    if audit.problems:
        print("PACKAGE AUDIT FAILED")
        for p in audit.problems:
            print(" -", p)
        return 1
    print(f"package audit ok ({audit.files} files)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
