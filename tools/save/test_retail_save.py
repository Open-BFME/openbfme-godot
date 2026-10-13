"""Tests for the retail save reader on synthetic files built here (no retail bytes in git).

The writer below mirrors retail's XferSave (RW 0xA20DD0 Open, RW 0xA2111A beginBlock, RW 0xA206EF
endBlock, RW 0xA20FCE the block-name table, RW 0xA20B26 the DSCR label) and GameState::xferSaveData
(RW 0x6DDF2F): the reader must accept what that code writes and name every way a file can be wrong.
"""
import struct
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import retail_save as rs  # noqa: E402


class Writer:
    def __init__(self, tagged=False, kind=rs.KIND_2, version=1):
        self.b = bytearray()
        self.tagged = tagged
        self.stack = []
        self.name_ids = {}
        self.b += struct.pack("<II", rs.MAGIC, kind)
        if kind == rs.KIND_2:
            self.b += struct.pack("<I", version)
        self.b += struct.pack("<I", 1 if tagged else 0)

    def tag(self, t):
        if self.tagged:
            self.b += struct.pack("<I", rs._tag(t) if isinstance(t, str) else t)

    def label(self, text):
        """XferSave slot 0x2C: a DSCR record, tagged files only."""
        if self.tagged:
            self.b += struct.pack("<I", rs.DSCR)
            self.name(text)

    def name(self, text):
        if not text:
            self.b += b"\0"
        elif text in self.name_ids:
            self.b += b"\xff" + struct.pack("<I", self.name_ids[text])
        else:
            self.b += bytes([len(text)]) + text.encode("latin1")
            self.name_ids[text] = len(self.name_ids) + 1

    def string(self, s, wide=False):
        self.tag("ustr" if wide else "astr")
        n = len(s)
        if n >= 255:
            self.b += b"\xff" + struct.pack("<I", n)
        else:
            self.b += bytes([n])
        self.b += s.encode("utf-16-le" if wide else "latin1")

    def version(self, v):
        self.tag("ver")
        self.b += bytes([v])

    def u32(self, v, t="uint"):
        self.tag(t)
        self.b += struct.pack("<I", v)

    def u16(self, v):
        self.tag("usht")
        self.b += struct.pack("<H", v)

    def real(self, v):
        self.tag("real")
        self.b += struct.pack("<f", v)

    def coord3d(self, x, y, z):
        self.tag("c3d")
        self.b += struct.pack("<3f", x, y, z)

    def begin(self, name="Snapshot"):
        self.b += struct.pack("<I", rs.BLOK)
        if self.tagged:
            self.name(name)
        self.stack.append(len(self.b))
        self.b += b"\0\0\0\0"

    def end(self):
        if self.tagged:
            self.b += struct.pack("<I", rs.EBLK)
        at = self.stack.pop()
        struct.pack_into("<I", self.b, at, len(self.b))

    def block(self, token, body):
        self.string(token)
        self.begin()
        body(self)
        self.end()

    def eof(self):
        self.string("SG_EOF")
        return bytes(self.b)


def game_state(version=3, save_type=2, description="Helm's Deep", map_label="maps/map mp helms deep"):
    def body(w):
        w.version(1)              # GameState::xfer RW 0x6DCE1C
        w.version(version)        # SaveGameInfo RW 0x6DEFD0
        w.u32(save_type, "enu4")
        w.string("")
        for k, v in enumerate((2026, 10, 12, 1, 9, 30, 5, 250)):
            w.u16(v)
        w.string(description, wide=True)
        w.string(map_label)
        if version < 2:
            w.string("old")
            w.u32(7, "int")
        if version >= 3:
            w.u32(0, "enu4")
        w.string("Gandalf", wide=True)
        w.string("Alice, Bob", wide=True)
    return body


def logic_block(w):
    w.version(29)
    w.real(1.5)
    w.coord3d(1.0, 2.0, 3.0)
    w.begin("BehaviorModule")
    w.version(1)
    w.u32(42)
    w.end()


def test_untagged_save_lists_every_block_with_version_and_size():
    w = Writer()
    w.block("CHUNK_GameState", game_state())
    w.block("CHUNK_GameLogic", logic_block)
    w.block("CHUNK_SomethingNew", lambda w: w.b.extend(b"\x05abc"))
    data = w.eof()
    save = rs.parse(data)
    assert (save.kind, save.version, save.tagged) == ("2STR", 1, False)
    assert [b.token for b in save.blocks] == ["CHUNK_GameState", "CHUNK_GameLogic", "CHUNK_SomethingNew"]
    assert [b.version for b in save.blocks] == [1, 29, 5]
    # GameLogic: version 1 + real 4 + c3d 12 + nested BLOK(4) + end(4) + version 1 + u32 4
    assert save.blocks[1].size == 1 + 4 + 12 + 4 + 4 + 1 + 4
    assert save.blocks[2].size == 4
    assert save.blocks[0].end == save.blocks[1].offset
    assert save.trailing == 0
    assert data[save.eof_offset + 1:save.eof_offset + 7] == b"SG_EOF"


def test_the_save_game_info_header_is_decoded():
    w = Writer()
    w.block("CHUNK_GameState", game_state(save_type=4, description="Rohan holds"))
    save = rs.parse(w.eof())
    h = save.header
    assert h["version"] == 3      # the save game info's own version; GameState's is 1
    assert h["save_file_type"] == 4
    assert h["save_file_type_meaning"].startswith("War of the Ring strategic map, single player")
    assert h["description"] == "Rohan holds"
    assert h["map_label"] == "maps/map mp helms deep"
    assert h["date"] == [2026, 10, 12, 1, 9, 30, 5, 250]
    assert h["is_auto_save"] == 0
    assert h["player_names"] == "Alice, Bob"
    assert h["rest_bytes"] == 0


def test_save_game_info_version_1_has_the_old_text_and_int():
    w = Writer()
    w.block("CHUNK_GameState", game_state(version=1))
    h = rs.parse(w.eof()).header
    assert (h["v1_text"], h["v1_int"]) == ("old", 7)
    assert "is_auto_save" not in h


def test_1str_container_has_no_version_word():
    w = Writer(kind=rs.KIND_1)
    w.block("CHUNK_GameLogic", logic_block)
    save = rs.parse(w.eof())
    assert (save.kind, save.version) == ("1STR", None)
    assert [b.version for b in save.blocks] == [29]


def test_long_strings_use_the_255_escape():
    w = Writer()
    w.block("CHUNK_GameState", game_state(description="x" * 300))
    assert rs.parse(w.eof()).header["description"] == "x" * 300


def test_tagged_save_with_labels_name_table_and_nested_blocks():
    w = Writer(tagged=True)
    w.label("GameState")
    w.block("CHUNK_GameState", game_state())
    w.label("Logic")
    w.block("CHUNK_GameLogic", logic_block)
    data = w.eof()
    save = rs.parse(data)
    assert save.tagged
    # a repeated name is 0xFF + an id; the reader reports the id as is (retail's numbering of the
    # ids is not known: SAVES-RETAIL.md, checks that wait for a sample tagged save)
    assert [(b.token, b.name, b.version) for b in save.blocks] == [
        ("CHUNK_GameState", "Snapshot", 1), ("CHUNK_GameLogic", "#name2", 29)]
    assert save.header["description"] == "Helm's Deep"
    items = [item for _, _, item in rs.walk_tagged(data, save.blocks[1].payload, save.blocks[1].end - 4)]
    assert items[:3] == ["ver", "real", "c3d"]
    assert items[3].startswith("BLOK BehaviorModule end=")
    assert items[4:] == ["ver", "uint"]


def test_tagged_walk_stops_at_an_unknown_tag_and_says_so():
    w = Writer(tagged=True)
    w.block("CHUNK_X", lambda w: (w.version(1), w.b.extend(struct.pack("<I", rs._tag("zzzz")) + b"\0" * 8)))
    data = w.eof()
    blk = rs.parse(data).blocks[0]
    items = [item for _, _, item in rs.walk_tagged(data, blk.payload, blk.end - 4)]
    assert items == ["ver", "opaque 12 bytes (unknown tag 'zzzz')"]


@pytest.mark.parametrize("mutate,message", [
    (lambda d: b"XXXX" + d[4:], "not a save: magic"),
    (lambda d: d[:4] + struct.pack("<I", 0x52545333) + d[8:], "unknown container kind"),
    (lambda d: d[:8] + struct.pack("<I", 2) + d[12:], r"save version 2 is newer than retail reads \(at most 1\)"),
    (lambda d: d[:30], "file ends inside block token"),
    (lambda d: d[:40], r"end offset 0x46 outside 0x28\.\.0x28"),
])
def test_bad_containers_are_named(mutate, message):
    w = Writer()
    w.block("CHUNK_GameLogic", logic_block)
    with pytest.raises(rs.SaveFormatError, match=message):
        rs.parse(mutate(w.eof()))


def test_block_end_outside_the_file_is_refused():
    w = Writer()
    w.block("CHUNK_GameLogic", logic_block)
    data = bytearray(w.eof())
    blk_end_at = 16 + 1 + len("CHUNK_GameLogic") + 4
    struct.pack_into("<I", data, blk_end_at, len(data) + 10)
    with pytest.raises(rs.SaveFormatError, match="block 'CHUNK_GameLogic': end offset"):
        rs.parse(bytes(data))


def test_missing_blok_marker_is_refused():
    w = Writer()
    w.string("CHUNK_GameLogic")
    w.b += b"JUNK" + b"\0" * 8
    with pytest.raises(rs.SaveFormatError, match="BLOK expected, found"):
        rs.parse(w.eof())


def test_tagged_block_without_eblk_is_refused():
    w = Writer(tagged=True)
    w.block("CHUNK_GameLogic", logic_block)
    data = bytearray(w.eof())
    save = rs.parse(bytes(data))
    struct.pack_into("<I", data, save.blocks[0].end - 4, 0)
    with pytest.raises(rs.SaveFormatError, match="EBLK expected"):
        rs.parse(bytes(data))


def test_duplicate_block_is_refused():
    w = Writer()
    w.block("CHUNK_GameLogic", logic_block)
    w.block("CHUNK_GameLogic", logic_block)
    with pytest.raises(rs.SaveFormatError, match="appears twice"):
        rs.parse(w.eof())


def test_missing_eof_token_is_a_truncation():
    w = Writer()
    w.block("CHUNK_GameLogic", logic_block)
    with pytest.raises(rs.SaveFormatError, match="file ends inside block token"):
        rs.parse(bytes(w.b))


def test_save_game_info_version_outside_the_window_is_refused():
    w = Writer()
    w.block("CHUNK_GameState", game_state(version=4))
    with pytest.raises(rs.SaveFormatError, match=r"SaveGameInfo version 4 outside retail's window \(1, 3\)"):
        rs.parse(w.eof())


def test_cli_prints_the_block_table(tmp_path, capsys):
    w = Writer()
    w.block("CHUNK_GameState", game_state())
    w.block("CHUNK_GameLogic", logic_block)
    p = tmp_path / "x.BfME2Skirmish"
    p.write_bytes(w.eof())
    assert rs.main([str(p)]) == 0
    out = capsys.readouterr().out
    assert "save type 2 (skirmish (.BfME2Skirmish))" in out
    assert "CHUNK_GameLogic" in out


def test_cli_reports_a_bad_file_with_exit_2(tmp_path, capsys):
    p = tmp_path / "bad.BfME2Skirmish"
    p.write_bytes(b"ALAE")
    assert rs.main([str(p)]) == 2
    assert "file ends inside kind" in capsys.readouterr().err
