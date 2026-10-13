#!/usr/bin/env python3
"""Read-only reader for Rise of the Witch-king 2.01 save games (*.BfME2Skirmish, *.BfME2Campaign,
*.BfME2WotR, *.BfME2WotRMP).

    python3 tools/save/retail_save.py <save file> [--json] [--walk]

Lane SAVE-A0. Never writes anything. Layout (docs/SAVES-RETAIL.md has the evidence):

  u32 0x45414C41 "ALAE", u32 0x52545332 "2STR" (or 0x52545331 "1STR": no version word follows)
  u32 version           XferSave::Open RW 0xA20DD0 writes 1; the loaders (RW 0x6DEA25, the save
                        list reader RW 0x6DEEC3) refuse a value above 1
  u32 tagged            XferLoad::Open RW 0xA200A3: nonzero = every transfer carries a type tag
  then per block:
    string token        "CHUNK_..." (GameState::xferSaveData RW 0x6DDF2F)
    u32 0x424C4F4B BLOK [tagged: name record]  u32 END  payload  [tagged: u32 0x45424C4B EBLK]
  string "SG_EOF"

END is the absolute file offset just past the block (XferSave::endBlock RW 0xA206EF patches the
stream position over the placeholder; Xfer::skipBlock RW 0xA203D6 seeks to it), not a size.
A string is u8 length (255: a u32 length follows) and the bytes (UTF-16LE for wide strings).
Tagged mode only: each typed transfer is preceded by its u32 type tag ('ver', 'astr', 'int', ...),
a name record is u8 length + bytes, 0 = none, 0xFF + u32 = a repeat of an earlier name, and
'DSCR' (u32 0x44534352) + a name record may precede any tag or BLOK (a field label, XferSave
slot 0x2C RW 0xA20B26; the loaders skip it). Retail's own saves are untagged (the flag byte
RW 0xDE87BB that saveGame passes to XferSave::Open is zero and nothing reachable sets it).

Errors are SaveFormatError with the file offset; the reader never guesses past a bad byte.
"""
from __future__ import annotations

import argparse
import json
import struct
import sys
from dataclasses import asdict, dataclass, field
from pathlib import Path

MAGIC = 0x45414C41          # "ALAE"
KIND_2 = 0x52545332         # "2STR": a version word follows
KIND_1 = 0x52545331         # "1STR": no version word
BLOK = 0x424C4F4B
EBLK = 0x45424C4B
DSCR = 0x44534352
EOF_TOKEN = "SG_EOF"
MAX_VERSION = 1             # loaders refuse a version above 1 (RW 0x6DEA25: version < 2)

# type tags (Xfer.cpp in the Open-BFME-2 decomp, tier A for RotWK; multi-char constants as u32)
def _tag(s):
    v = 0
    for ch in s:
        v = (v << 8) | ord(ch)
    return v


TAG_SIZES = {   # tag -> payload bytes after the tag (fixed-size transfers)
    _tag("byte"): 1, _tag("ubyt"): 1, _tag("bool"): 1, _tag("int"): 4, _tag("i64"): 8,
    _tag("uint"): 4, _tag("shrt"): 2, _tag("usht"): 2, _tag("real"): 4, _tag("ver"): 1,
    _tag("enu1"): 1, _tag("enu2"): 2, _tag("enu3"): 3, _tag("enu4"): 4,
    # composites announce themselves with a zero-length tag, then move their bytes untagged
    _tag("c3d"): 12, _tag("ic3d"): 12, _tag("c2d"): 8, _tag("ic2d"): 8, _tag("rnge"): 8,
    _tag("rgb"): 12, _tag("rgbr"): 16, _tag("rgbi"): 16, _tag("r3d"): 24, _tag("ir3d"): 24,
    _tag("r2d"): 16, _tag("ir2d"): 16,
}
TAG_ASTR = _tag("astr")
TAG_USTR = _tag("ustr")
TAG_RAW = _tag("raw")


def tag_name(v):
    s = "".join(chr((v >> s) & 0xFF) for s in (24, 16, 8, 0) if (v >> s) & 0xFF)
    return s if s.isprintable() else f"0x{v:08X}"


class SaveFormatError(Exception):
    def __init__(self, offset, message):
        super().__init__(f"at 0x{offset:X}: {message}")
        self.offset = offset


class Reader:
    def __init__(self, data, tagged=False):
        self.data = data
        self.pos = 0
        self.tagged = tagged
        self.names = []

    def need(self, n, what):
        if self.pos + n > len(self.data):
            raise SaveFormatError(self.pos, f"file ends inside {what} ({len(self.data) - self.pos} of {n} bytes)")

    def u8(self, what="u8"):
        self.need(1, what)
        v = self.data[self.pos]
        self.pos += 1
        return v

    def u16(self, what="u16"):
        self.need(2, what)
        v = struct.unpack_from("<H", self.data, self.pos)[0]
        self.pos += 2
        return v

    def u32(self, what="u32"):
        self.need(4, what)
        v = struct.unpack_from("<I", self.data, self.pos)[0]
        self.pos += 4
        return v

    def i32(self, what="i32"):
        return struct.unpack("<i", struct.pack("<I", self.u32(what)))[0]

    def raw(self, n, what):
        self.need(n, what)
        v = self.data[self.pos:self.pos + n]
        self.pos += n
        return v

    # tagged mode ------------------------------------------------------------------------------
    def name_record(self, what):
        """u8 length + bytes; 0 = no name; 0xFF + u32 = a repeat (XferSave name table, RW 0xA20FCE)."""
        n = self.u8(what)
        if n == 0:
            return ""
        if n == 0xFF:
            ref = self.u32(what)
            return f"#name{ref}"
        s = self.raw(n, what).decode("latin1")
        self.names.append(s)
        return s

    def next_marker(self, what):
        """The next u32 marker, skipping DSCR label records (XferLoad::XferImpl RW 0xA2048D)."""
        while True:
            v = self.u32(what)
            if v != DSCR:
                return v
            self.name_record("DSCR label")

    def expect_tag(self, tag, what):
        if not self.tagged:
            return
        at = self.pos
        v = self.next_marker(what)
        if v != tag:
            raise SaveFormatError(at, f"{what}: expected tag '{tag_name(tag)}' but found '{tag_name(v)}'")

    # transfers ------------------------------------------------------------------------------
    def string(self, wide=False, what="string"):
        """Xfer::operator==(AsciiString&) RW 0xA1FB84 / (UnicodeString&) RW 0xA1FC7E."""
        self.expect_tag(TAG_USTR if wide else TAG_ASTR, what)
        n = self.u8(what + " length")
        if n == 255:
            n = self.u32(what + " long length")
        if wide:
            return self.raw(2 * n, what).decode("utf-16-le", errors="replace")
        return self.raw(n, what).decode("latin1")

    def version(self, what="version"):
        self.expect_tag(_tag("ver"), what)
        return self.u8(what)

    def typed_u32(self, tag, what):
        self.expect_tag(tag, what)
        return self.u32(what)

    def typed_u16(self, what):
        self.expect_tag(_tag("usht"), what)
        return self.u16(what)


@dataclass
class Block:
    token: str
    offset: int          # file offset of the token
    blok_offset: int     # file offset of the BLOK marker
    payload: int         # first payload byte
    end: int             # absolute end (past EBLK in tagged files)
    name: str = ""       # tagged files: the block name ("Snapshot")
    version: int | None = None   # first payload byte: the snapshot's own version (untagged; tagged via 'ver')

    @property
    def size(self):
        return self.end - self.payload


@dataclass
class SaveFile:
    kind: str
    version: int | None
    tagged: bool
    blocks: list = field(default_factory=list)
    eof_offset: int = 0
    trailing: int = 0
    header: dict | None = None


def parse(data: bytes) -> SaveFile:
    """Parse the container and the top-level blocks of a retail save. Raises SaveFormatError."""
    r = Reader(data)
    magic = r.u32("magic")
    if magic != MAGIC:
        raise SaveFormatError(0, f"not a save: magic 0x{magic:08X}, expected 0x{MAGIC:08X} 'ALAE'")
    kind = r.u32("kind")
    if kind not in (KIND_1, KIND_2):
        raise SaveFormatError(4, f"unknown container kind 0x{kind:08X} (expected '1STR' or '2STR')")
    version = None
    if kind == KIND_2:
        version = r.u32("version")
        if version > MAX_VERSION:
            raise SaveFormatError(8, f"save version {version} is newer than retail reads (at most {MAX_VERSION})")
    r.tagged = r.u32("tagged flag") != 0
    save = SaveFile(kind="2STR" if kind == KIND_2 else "1STR", version=version, tagged=r.tagged)
    seen = set()
    while True:
        tok_at = r.pos
        token = r.string(what="block token")
        if token.upper() == EOF_TOKEN:
            save.eof_offset = tok_at
            break
        if token.upper() in seen:
            raise SaveFormatError(tok_at, f"block {token} appears twice")
        seen.add(token.upper())
        blok_at = r.pos
        marker = r.next_marker("BLOK") if r.tagged else r.u32("BLOK")
        if r.tagged is False and marker == DSCR:
            r.name_record("DSCR label")
            marker = r.u32("BLOK")
        if marker != BLOK:
            raise SaveFormatError(blok_at, f"block '{token}': BLOK expected, found 0x{marker:08X}")
        name = r.name_record("block name") if r.tagged else ""
        end_at = r.pos
        end = r.u32("block end")
        payload = r.pos
        if end < payload or end > len(data):
            raise SaveFormatError(end_at, f"block '{token}': end offset 0x{end:X} outside 0x{payload:X}..0x{len(data):X}")
        if r.tagged:
            if end - payload < 4 or struct.unpack_from("<I", data, end - 4)[0] != EBLK:
                raise SaveFormatError(end - 4, f"block '{token}': EBLK expected before its end")
        blk = Block(token=token, offset=tok_at, blok_offset=blok_at, payload=payload, end=end, name=name)
        if r.tagged:
            sub = Reader(data[:end - 4], tagged=True)
            sub.pos = payload
            try:
                blk.version = sub.version(f"{token} version")
            except SaveFormatError:
                blk.version = None
        elif end > payload:
            blk.version = data[payload]
        save.blocks.append(blk)
        r.pos = end
    save.trailing = len(data) - r.pos
    for blk in save.blocks:
        if blk.token.upper() == "CHUNK_GAMESTATE":
            save.header = read_game_state(data, blk, r.tagged)
    return save


# CHUNK_GameState: GameState::xfer RW 0x6DCE1C (the Snapshot vtable RW 0xC1A3B4 at GameState+0xC;
# rw_map.csv mislabels it AIGuardReturnState::xfer) transfers its own version, window (1, 1), then
# xferSnapshot(the save game info at GameState+0x24): RW 0x6DEFD0, tier B of the decomp's
# BfmeSubobject0022CE19::xfer (SaveGameInfoCopyBFME2.cpp), window (1, 3).
SAVE_FILE_TYPES = {
    0: "campaign mission (.BfME2Campaign)",
    1: "campaign, between missions (.BfME2Campaign)",
    2: "skirmish (.BfME2Skirmish)",
    3: "War of the Ring battle, single player (.BfME2WotR)",
    4: "War of the Ring strategic map, single player (.BfME2WotR)",
    5: "War of the Ring battle, multiplayer (.BfME2Campaign extension)",
    6: "War of the Ring strategic map, multiplayer (.BfME2WotRMP)",
}


def read_game_state(data, blk, tagged):
    """The fields every Load screen entry shows; stops before the War of the Ring preview data."""
    r = Reader(data[:blk.end - (4 if tagged else 0)], tagged=tagged)
    r.pos = blk.payload
    out = {}
    gs = r.version("GameState version")
    if gs != 1:
        raise SaveFormatError(blk.payload, f"GameState version {gs} outside retail's window (1, 1)")
    v = r.version("SaveGameInfo version")
    if not 1 <= v <= 3:
        raise SaveFormatError(blk.payload + 1, f"SaveGameInfo version {v} outside retail's window (1, 3)")
    out["version"] = v
    out["save_file_type"] = r.typed_u32(_tag("enu4"), "SaveFileType")     # XferEnum('SaveFileType', 4)
    out["save_file_type_meaning"] = SAVE_FILE_TYPES.get(out["save_file_type"], "unknown")
    out["text_2c"] = r.string(what="SaveGameInfo +0x2C")
    # SYSTEMTIME as saveGame RW 0x6DE498 copies it: year, month, day, day of week, hour, minute, second, ms
    out["date"] = [r.typed_u16(f"date[{k}]") for k in range(8)]
    out["description"] = r.string(wide=True, what="description")
    out["map_label"] = r.string(what="map label")
    if v < 2:
        out["v1_text"] = r.string(what="v1 text")
        out["v1_int"] = struct.unpack("<i", struct.pack("<I", r.typed_u32(_tag("int"), "v1 int")))[0]
    if v >= 3:
        out["is_auto_save"] = r.typed_u32(_tag("enu4"), "IsAutoSaveOrNot")
    out["text_30"] = r.string(wide=True, what="SaveGameInfo +0x30")
    out["player_names"] = r.string(wide=True, what="player names")
    out["rest_bytes"] = blk.end - r.pos
    return out


# ------------------------------------------------------------------------------------ tagged walk

def walk_tagged(data, start, end, depth=0, limit=100000):
    """Self-describing walk of a tagged payload: (offset, depth, item) tuples. Stops (item 'opaque')
    at the first untagged run it cannot size; nested blocks end where their END offset says."""
    r = Reader(data[:end], tagged=True)
    r.pos = start
    items = []
    while r.pos < end and len(items) < limit:
        at = r.pos
        if end - r.pos < 4:
            items.append((at, depth, f"opaque {end - at} bytes"))
            break
        tag = r.next_marker("tag")
        if tag == BLOK:
            name = r.name_record("block name")
            bend = r.u32("block end")
            if bend < r.pos or bend > end:
                raise SaveFormatError(at, f"nested block '{name}': end 0x{bend:X} outside its parent")
            items.append((at, depth, f"BLOK {name or '-'} end=0x{bend:X}"))
            items.extend(walk_tagged(data, r.pos, bend - 4, depth + 1, limit))
            if struct.unpack_from("<I", data, bend - 4)[0] != EBLK:
                raise SaveFormatError(bend - 4, f"nested block '{name}': EBLK expected")
            r.pos = bend
            continue
        if tag in TAG_SIZES:
            r.raw(TAG_SIZES[tag], tag_name(tag))
            items.append((at, depth, tag_name(tag)))
        elif tag in (TAG_ASTR, TAG_USTR):
            n = r.u8("length")
            if n == 255:
                n = r.u32("long length")
            r.raw(n * (2 if tag == TAG_USTR else 1), "string")
            items.append((at, depth, tag_name(tag)))
        elif tag == TAG_RAW:
            n = r.u32("raw size")
            r.raw(n, "raw bytes")
            items.append((at, depth, f"raw {n}"))
        else:
            items.append((at, depth, f"opaque {end - at} bytes (unknown tag '{tag_name(tag)}')"))
            break
    return items


def describe(save: SaveFile):
    lines = [f"container {save.kind} version {save.version} tagged {save.tagged}"]
    if save.header:
        h = save.header
        lines.append(f"save type {h['save_file_type']} ({h['save_file_type_meaning']}), "
                     f"description {h['description']!r}, map {h['map_label']!r}, date {h['date']}")
    lines.append(f"{'token':40} {'offset':>10} {'payload':>10} {'size':>10} version")
    for b in save.blocks:
        lines.append(f"{b.token:40} {b.offset:>#10x} {b.payload:>#10x} {b.size:>10} {b.version}")
    lines.append(f"SG_EOF at 0x{save.eof_offset:X}; {save.trailing} trailing bytes")
    return "\n".join(lines)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("save")
    ap.add_argument("--json", action="store_true")
    ap.add_argument("--walk", action="store_true", help="tagged files: list every transfer")
    args = ap.parse_args(argv)
    data = Path(args.save).read_bytes()
    try:
        save = parse(data)
    except SaveFormatError as e:
        print(f"{args.save}: {e}", file=sys.stderr)
        return 2
    if args.json:
        d = asdict(save)
        for b, bd in zip(save.blocks, d["blocks"]):
            bd["size"] = b.size
        print(json.dumps(d, indent=1))
    else:
        print(describe(save))
    if args.walk and save.tagged:
        for b in save.blocks:
            print(f"== {b.token}")
            for at, depth, item in walk_tagged(data, b.payload, b.end - 4):
                print(f"  {at:#8x} {'  ' * depth}{item}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
