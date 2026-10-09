#!/usr/bin/env python3
"""Lane RELEASE-1: a test package must never carry game files.

    python3 tools/release/audit_package.py <dir | .tar.gz | .zip | .pck> ...

Every file is judged the way the commit gate judges a staged file (tools/precommit.py): by suffix (RETAIL_SUFFIXES) and by content
(retail_content: RIFF/WAVE, ID3, MPEG Layer III, BIG, DDS, under any name). Containers are recognised by their bytes, whatever their names:
Godot packs (also appended to an executable), ZIP, gzip, bzip2, xz and tar are opened and audited member by member, recursively; other
container kinds (7-Zip, Zstandard, RAR, CAB, LZ4, LZO), encrypted packs and encrypted ZIP members are findings, so nothing hides inside. Text files are also checked for
developer-machine paths (precommit MACHINE_PATH). Exit 1 with one line per problem; 0 prints "package audit ok (<n> files)".
"""
from __future__ import annotations

import bz2
import gzip
import io
import lzma
import re
import struct
import zlib
import sys
import tarfile
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from precommit import MACHINE_PATH, RETAIL_SUFFIXES, retail_content  # noqa: E402

PCK_MAGIC = b"GDPC"
MAX_DEPTH = 16                 # containers inside containers
MAX_UNPACKED = 4 << 30         # bytes one compressed stream may expand to (a bomb is a finding, not a hang)
# containers recognised by their bytes that this audit cannot open: a finding, never a pass (fail closed)
UNOPENABLE = {b"7z\xbc\xaf\x27\x1c": "7-Zip", b"\x28\xb5\x2f\xfd": "Zstandard", b"Rar!\x1a\x07": "RAR", b"MSCF": "CAB",
              b"\x04\x22\x4d\x18": "LZ4", b"\x89LZO": "LZO"}


STREAM_MAGIC = {"gzip": b"\x1f\x8b", "bzip2": b"BZh", "xz": b"\xfd7zXZ\x00"}


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
    fmt, _major, _minor, _patch, flags = struct.unpack_from("<5I", data, base + 4)
    if fmt not in (2, 3, 4):
        raise ValueError(f"unknown pack format {fmt}")
    if flags & 1:
        raise ValueError("encrypted pack directory: cannot be audited")
    rel_base = flags & 2  # PACK_REL_FILEBASE: offsets relative to the pack start
    file_base, = struct.unpack_from("<Q", data, base + 24)
    if fmt >= 3:
        dir_offset, = struct.unpack_from("<Q", data, base + 32)
        header_end = base + 40 + 16 * 4
        pos = base + dir_offset
    else:
        header_end = base + 32 + 16 * 4
        pos = header_end
    dir_start = pos
    count, = struct.unpack_from("<I", data, pos)
    pos += 4
    out = []
    used = [(base, header_end)]
    for _ in range(count):
        n, = struct.unpack_from("<I", data, pos)
        pos += 4
        path = data[pos:pos + n].rstrip(b"\0").decode("utf-8", "replace")
        pos += n
        ofs, size = struct.unpack_from("<QQ", data, pos)
        pos += 16 + 16  # offset, size, md5
        eflags, = struct.unpack_from("<I", data, pos)
        pos += 4
        if eflags & 1:
            raise ValueError(f"encrypted pack entry {path}: cannot be audited")
        start = (base + file_base if rel_base or fmt >= 3 else file_base) + ofs
        if start < base or start + size > end:
            raise ValueError(f"pack entry {path} lies outside the pack")
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
    def __init__(self) -> None:
        self.problems: list[str] = []
        self.files = 0

    def blob(self, name: str, data: bytes, depth: int = 0) -> None:
        """Containers are recognised by their bytes, never by their names (review r1: a renamed .zip or .tar.gz passed with BIG/DDS
        inside): Godot packs (also appended to an executable), ZIP (also appended), gzip / bzip2 / xz streams and tar archives are opened
        and every member audited the same way, at any depth up to MAX_DEPTH; a container kind that cannot be opened is a finding."""
        self.files += 1
        if depth > MAX_DEPTH:
            self.problems.append(f"containers nested deeper than {MAX_DEPTH}: {name}")
            return
        low = name.lower()
        if Path(low.rsplit(":", 1)[-1]).suffix in RETAIL_SUFFIXES:
            self.problems.append(f"retail-format file name: {name}")
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
        if b"\x00" not in data[:4096] and MACHINE_PATH.search(data[:1 << 20].decode("utf-8", "replace")):
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
RELEASE_FILES = {
    "linux": {"OpenBFME.x86_64": "elf", "openbfme.linux.template_debug.x86_64.so": "elf", "OpenBFME.pck": "pck",
              "README_TESTERS.txt": "text", "LICENSE": "text", "NOTICE": "text", "VERSION": "text"},
    "windows": {"OpenBFME.exe": "pe", "OpenBFME.console.exe": "pe", "openbfme.windows.template_debug.x86_64.dll": "pe", "OpenBFME.pck": "pck",
                "README_TESTERS.txt": "text", "LICENSE": "text", "NOTICE": "text", "VERSION": "text"},
    # lane LAUNCH-1: the OpenBFME Launcher packages (tools/release/build_launcher.sh)
    # the launcher's pack is embedded in its executable (one file: a self-update replaces it with one rename, Sol r1)
    "launcher-linux": {"OpenBFMELauncher.x86_64": "elf+pck", "README.txt": "text", "LICENSE": "text", "NOTICE": "text", "VERSION": "text"},
    "launcher-windows": {"OpenBFMELauncher.exe": "pe+pck", "README.txt": "text", "LICENSE": "text", "NOTICE": "text", "VERSION": "text"},
}
TOP_DIR = re.compile(r"^openbfme-(?!launcher-)[0-9A-Za-z][0-9A-Za-z.+_-]*-(linux|windows)-x64$")
LAUNCHER_TOP_DIR = re.compile(r"^openbfme-launcher-[0-9A-Za-z][0-9A-Za-z.+_-]*-(linux|windows)-x64$")

# the Godot resource types of this project a pack may hold (godot/export_presets.cfg exports scripts as text), each with its format check
PCK_RULES = [
    (re.compile(r"^\.godot/exported/\d+/export-[0-9a-f]{32}-[\w.-]+\.scn$"), "rsrc"),
    (re.compile(r"^(scripts|tests)/[\w/.-]+\.gd$"), "text"),
    (re.compile(r"^scenes/[\w/.-]+\.tscn$"), "scene"),  # scenes ship as their committed text (project.godot: no binary conversion)
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
]
# signatures that may not appear ANYWHERE inside a pack entry or a text file: retail formats and containers / compressed streams of any kind
EMBEDDED_SIGNATURES = {b"BIGF": "BIG archive", b"BIG4": "BIG archive", b"DDS |": "DDS texture", b"WAVEfmt": "RIFF/WAVE audio",
                       b"ID3\x03": "ID3 audio", b"ID3\x04": "ID3 audio", b"PK\x03\x04": "ZIP entry", b"PK\x05\x06": "ZIP directory",
                       b"\x1f\x8b\x08": "gzip stream", b"BZh91AY&SY": "bzip2 stream", b"\xfd7zXZ\x00": "xz stream",
                       b"7z\xbc\xaf\x27\x1c": "7-Zip archive", b"\x28\xb5\x2f\xfd": "Zstandard stream", b"Rar!\x1a\x07": "RAR archive",
                       b"MSCF": "CAB archive", b"\x04\x22\x4d\x18": "LZ4 stream", b"GDPC": "Godot pack", b"RSCC": "compressed Godot resource",
                       b"ustar": "tar archive"}


def embedded_signatures(data: bytes, text: bool = False) -> list[str]:
    """The signatures found anywhere in `data`. In a text file (already checked by text_problem: UTF-8, no control characters) a signature
    made of printable characters only ("BIG4" in prose) cannot start binary content, so only the ones with control bytes are looked for."""
    return sorted({what for sig, what in EMBEDDED_SIGNATURES.items()
                   if (not text or any(b < 0x20 or b >= 0x7F for b in sig)) and sig in data})


MAX_TEXT = 256 << 10  # a text file of the package (README, licence, a script) is small; a large one is a finding


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


def embedded_pack_problems(name: str, kind: str, b: bytes) -> list[str]:
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
    problems += pck_release_problems(f"{name}:pck", b[base:len(b) - 12], LAUNCHER_PCK_RULES)
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


def pck_release_problems(name: str, data: bytes, rules: list | None = None) -> list[str]:
    rules = PCK_RULES if rules is None else rules
    try:
        entries = pck_entries(data)
    except (ValueError, struct.error) as e:
        return [f"{name}: {e}"]
    problems = []
    for path, content in entries:
        where = f"{name}:{path}"
        rule = next((kind for rx, kind in rules if rx.match(path)), None)
        if rule is None:
            problems.append(f"{where}: not a resource type this project exports")
            continue
        kind = retail_content(content)
        if kind:
            problems.append(f"{where}: retail-format bytes ({kind})")
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
        elif rule == "uidcache":
            t = uid_cache_problem(content)
            if t:
                problems.append(f"{where}: {t}")
        elif rule == "ecfg":
            t = ecfg_problem(content)
            if t:
                problems.append(f"{where}: {t}")
    return problems


def release_file_problems(platform: str, files: dict[str, bytes], where: str) -> list[str]:
    """The files of one package (names relative to its top folder) against the allowlist."""
    allowed = RELEASE_FILES[platform]
    problems = [f"{where}: {n} is not a file of the release" for n in sorted(set(files) - set(allowed))]
    problems += [f"{where}: {n} is missing" for n in sorted(set(allowed) - set(files))]
    for n in sorted(set(files) & set(allowed)):
        kind, data = allowed[n], files[n]
        if kind in ("elf", "pe"):
            problems += binary_problems(f"{where}/{n}", kind, data)
        elif kind in ("elf+pck", "pe+pck"):
            problems += embedded_pack_problems(f"{where}/{n}", kind, data)
        elif kind == "pck":
            problems += pck_release_problems(f"{where}/{n}", data, LAUNCHER_PCK_RULES if platform.startswith("launcher-") else PCK_RULES)
        else:
            t = text_problem(data) or retail_content(data)
            if t:
                problems.append(f"{where}/{n}: {t}")
            for what in embedded_signatures(data, text=True):
                problems.append(f"{where}/{n}: contains a {what} signature")
            if len(data) > MAX_TEXT:
                problems.append(f"{where}/{n}: {len(data)} bytes of text (more than {MAX_TEXT})")
            if MACHINE_PATH.search(data.decode("utf-8", "replace")):
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


def read_release_zip(data: bytes, where: str) -> tuple[str | None, dict[str, bytes], list[str]]:
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
        parts = info.filename.split("/")
        if info.filename.endswith("/") or info.is_dir():
            _, why = zip_member_exact(data, info)
            if why:
                problems.append(f"{where}:{info.filename}: {why}")
            problems.append(f"{where}: directory entry {info.filename} (none are written)")
            continue
        content, why = zip_member_exact(data, info)
        if why:
            problems.append(f"{where}:{info.filename}: {why}")
            continue
        n, m = struct.unpack_from("<HH", data, info.header_offset + 26)
        if info.extra or m or info.comment:  # make_archive.py writes none (review r4: payloads in extra fields and entry comments)
            problems.append(f"{where}:{info.filename}: extra fields or an entry comment (none are written)")
        if len(parts) != 2 or (top is not None and parts[0] != top):
            problems.append(f"{where}: {info.filename} is not a file of the package folder")
            continue
        top = top or parts[0]
        if parts[1] in files:
            problems.append(f"{where}: {info.filename} appears twice")
        files[parts[1]] = content
    return top, files, problems


def check_release(platform: str, path: Path) -> list[str]:
    """A package folder, a package archive, or a SHA256SUMS file (with its archives next to it). `platform`: linux, windows,
    launcher-linux or launcher-windows."""
    where = path.name
    base = platform.removeprefix("launcher-")
    if path.is_dir():
        files, problems = {}, []
        for f in sorted(path.rglob("*")):
            rel = f.relative_to(path)
            if f.is_dir() or len(rel.parts) != 1:
                problems.append(f"{where}: {rel} is not a file of the release")
            elif f.is_symlink() or not f.is_file():
                problems.append(f"{where}: {rel} is not a regular file")
            else:
                files[rel.name] = f.read_bytes()
        top = path.name
    elif path.name.startswith("SHA256SUMS"):
        return sums_problems(platform, path)
    else:
        data = path.read_bytes()
        if path.name.endswith(".tar.gz") and base == "linux":
            top, files, problems = read_release_tar_gz(data, where)
        elif path.name.endswith(".zip") and base == "windows":
            top, files, problems = read_release_zip(data, where)
        else:
            return [f"{where}: not a {platform} package archive"]
        if top and path.name != top + (".tar.gz" if base == "linux" else ".zip"):
            problems.append(f"{where}: the folder inside is {top}")
    top_rx = LAUNCHER_TOP_DIR if platform.startswith("launcher-") else TOP_DIR
    if not top or not top_rx.match(top) or not top.endswith(f"-{base}-x64"):
        kind = "openbfme-launcher" if platform.startswith("launcher-") else "openbfme"
        problems.append(f"{where}: the package folder {top!r} is not {kind}-<version>-{base}-x64")
    return problems + release_file_problems(platform, files, where)


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
        m = re.match(r"^([0-9a-f]{64})  (openbfme-[\w.+-]+-(linux|windows)-x64\.(tar\.gz|zip))$", line)
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
                          if re.match(r"^openbfme-[\w.+-]+-(linux|windows)-x64\.(tar\.gz|zip)$", p.name))
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
            print("usage: audit_package.py --release linux|windows|launcher-linux|launcher-windows <package folder | archive | SHA256SUMS file> ...")
            return 2
        problems = []
        for a in argv[2:]:
            problems += check_release(argv[1], Path(a))
        if problems:
            print("RELEASE ALLOWLIST FAILED")
            for p in problems:
                print(" -", p)
            return 1
        print(f"release allowlist ok ({len(argv) - 2} item(s), {argv[1]})")
        return 0
    audit = Audit()
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
