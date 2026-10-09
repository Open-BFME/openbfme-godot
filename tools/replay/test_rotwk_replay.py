"""Tests for rotwk_replay.py. All replay bytes are synthesized here; set ROTWK_REPLAY to a
real .BfME2Replay to also run the end-to-end check (the file is read in place, never copied)."""
from __future__ import annotations

import contextlib
import io
import json
import os
import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import rotwk_replay as rr  # noqa: E402

OPTIONS = ("M=387maps/map test;MC=384A860D;MS=1234;SD=801010171;GSID=329D;GT=0;SI=-1;"
           "GR=1 0 1 100 1000 -1 -1 -1 -1 -1 ;S=HAlice,AA94501,8094,TT,1,8,3,0,0,1,0:"
           "CB,-1,-1,1,1,0,0:O:X:X:X:X:X:;")


def wz(text: str) -> bytes:
    return text.encode("utf-16-le") + b"\x00\x00"


def header(options: str = OPTIONS, heroes: dict | None = None, local: str = "0", frames: int = 500) -> bytes:
    out = rr.MAGIC + struct.pack("<IIIIi", 1785962368, 1785964000, frames, 100, -1)
    out += b"\x00" + bytes(8)
    out += wz("Last Replay") + struct.pack("<8H", 2026, 8, 3, 5, 22, 39, 28, 612)
    out += wz("2.01") + wz("2007-03-30 19:47:21")
    out += struct.pack("<IBi", 0x249EFCAC, 0, 0)
    out += options.encode() + b"\x00" + local.encode() + b"\x00"
    heroes = heroes or {}
    for slot in range(8):
        blob = heroes.get(slot)
        out += b"\x00" if blob is None else b"\x01" + struct.pack("<I", len(blob)) + blob
    return out + struct.pack("<iiii", 1, 1, 0, 0)


def msg(frame: int, mtype: int, player: int, groups: list[tuple[int, list[bytes]]]) -> bytes:
    out = struct.pack("<IIiB", frame, mtype, player, len(groups))
    out += b"".join(struct.pack("<BB", kind, len(vals)) for kind, vals in groups)
    return out + b"".join(b"".join(vals) for _, vals in groups)


def crc_msg(frame: int, player: int, crc: int, logic_frame: int) -> bytes:
    return msg(frame, rr.MSG_LOGIC_CRC, player, [
        (0, [struct.pack("<I", crc)]), (9, [struct.pack("<I", 0), struct.pack("<I", logic_frame)]),
        (2, [b"\x00", b"\x00"])])


def write(data: bytes) -> Path:
    fd, name = tempfile.mkstemp(suffix=".BfME2Replay")
    os.close(fd)
    Path(name).write_bytes(data)
    return Path(name)


class HeaderTests(unittest.TestCase):
    def test_fields(self):
        h, pos = rr.parse_header(header())
        self.assertEqual(pos, len(header()))
        self.assertEqual((h["net_crc_interval"], h["desync_frame"], h["frame_count"]), (100, -1, 500))
        self.assertEqual(h["replay_name"], "Last Replay")
        self.assertEqual(h["system_time"]["year"], 2026)
        self.assertEqual(h["ini_crc"], 0x249EFCAC)
        self.assertEqual((h["difficulty"], h["original_game_mode"]), (1, 1))
        self.assertEqual(h["create_a_hero"], [])

    def test_options(self):
        o = rr.parse_header(header())[0]["options"]
        self.assertEqual((o["map"], o["map_contents_mask"], o["map_crc"]), ("maps/map test", 0x387, 0x384A860D))
        self.assertEqual((o["seed"], o["gsid"], o["GT"], o["SI"]), (801010171, 0x329D, 0, -1))
        self.assertEqual(o["game_rules"], [1, 0, 1, 100, 1000, -1, -1, -1, -1, -1])
        human, ai, open_, closed = o["slots"][:4]
        self.assertEqual((human["name"], human["ip"], human["player_template"], human["start_pos"]),
                         ("Alice", "0AA94501", 8, 3))
        self.assertEqual((ai["ai_level"], ai["color"], ai["team"]), ("brutal", -1, 1))
        self.assertEqual((open_["kind"], closed["kind"]), ("open", "closed"))
        self.assertEqual(len(o["slots"]), 8)

    def test_create_a_hero_block(self):
        blob = b"heroString" + bytes(20)
        h, pos = rr.parse_header(header(heroes={2: blob}))
        self.assertEqual(h["create_a_hero"], [{"slot": 2, "size": len(blob), "head": blob[:16].hex()}])
        self.assertEqual(pos, len(header(heroes={2: blob})))

    def test_bad_magic(self):
        with self.assertRaises(rr.ReplayError):
            rr.parse_header(b"GENREP" + bytes(64))

    def test_truncated_header(self):
        with self.assertRaises(rr.ReplayError):
            rr.parse_header(header()[:-3])


class MessageTests(unittest.TestCase):
    def test_every_arg_type_width(self):
        groups = [(0, [struct.pack("<i", -7)]), (1, [struct.pack("<f", 1.5)]), (2, [b"\x01"]),
                  (3, [struct.pack("<I", 323)]), (4, [struct.pack("<I", 9)]), (5, [struct.pack("<i", 2)]),
                  (6, [struct.pack("<3f", 1.0, 2.0, 3.0)]), (7, [struct.pack("<2i", 4, 5)]),
                  (8, [struct.pack("<4i", 1, 2, 3, 4)]), (9, [struct.pack("<I", 77)]), (10, ["x".encode("utf-16-le")])]
        data = msg(5, 1001, 3, groups) + msg(6, rr.MSG_CLEAR_GAME_DATA, 3, [])
        msgs, left = rr.parse_messages(data, 0)
        self.assertEqual(left, 0)
        self.assertEqual([v for _, v in msgs[0]["args"]],
                         [-7, 1.5, 1, 323, 9, 2, [1.0, 2.0, 3.0], [4, 5], [1, 2, 3, 4], 77, "x"])
        self.assertEqual(msgs[1]["type"], rr.MSG_CLEAR_GAME_DATA)

    def test_unknown_arg_type_reads_nothing(self):
        data = msg(1, 1004, 3, [(11, [b"", b""])]) + msg(2, 1004, 3, [(2, [b"\x01"])])
        msgs, left = rr.parse_messages(data, 0)
        self.assertEqual((len(msgs), left), (2, 0))

    def test_partial_trailing_record_is_leftover(self):
        good = msg(1, 1004, 3, [(2, [b"\x01"])])
        msgs, left = rr.parse_messages(good + good[:7], 0)
        self.assertEqual((len(msgs), left), (1, 7))

    def test_names(self):
        self.assertEqual(rr.MSG_NAMES[1098], "MSG_LOGIC_CRC")
        self.assertEqual(rr.MSG_NAMES[1071], "MSG_DO_MOVETO")
        self.assertEqual(rr.MSG_NAMES[1001], "MSG_CREATE_SELECTED_GROUP")


class SummaryTests(unittest.TestCase):
    def test_crc_interval_and_agreement(self):
        body = b"".join(crc_msg(f + 1, 3, 0xAB00 + f, f) + crc_msg(f + 2, 4, 0xAB00 + f, f)
                        for f in (100, 200, 300))
        body += crc_msg(402, 4, 0xDEAD, 400) + crc_msg(401, 3, 0xBEEF, 400)
        body += msg(410, 1071, 3, [(6, [struct.pack("<3f", 1, 2, 3)])]) + msg(411, rr.MSG_CLEAR_GAME_DATA, 3, [])
        path = write(header(frames=411) + body)
        try:
            s = rr.summarize(path)
        finally:
            path.unlink()
        self.assertEqual(s["leftover_bytes"], 0)
        self.assertTrue(s["complete"], s["incomplete_reasons"])
        self.assertEqual(s["crc"]["count"], 8)
        self.assertEqual(s["crc"]["checkpoints"], 4)
        self.assertEqual(s["crc"]["intervals"], [100])
        self.assertEqual(s["crc"]["disagreements"], [400])
        self.assertEqual(s["crc"]["records"][0]["logic_frame"], 100)
        self.assertEqual(s["last_message_frame"], 411)

    def test_cli_json(self):
        path = write(header(frames=101) + crc_msg(101, 3, 1, 100) + msg(101, rr.MSG_CLEAR_GAME_DATA, 3, []))
        out = io.StringIO()
        try:
            with contextlib.redirect_stdout(out):
                self.assertEqual(rr.main([str(path), "--json", "--messages", "1"]), 0)
        finally:
            path.unlink()
        doc = json.loads(out.getvalue())
        self.assertEqual(doc["crc"]["records"][0]["crc"], 1)
        self.assertEqual(doc["messages"][0]["type"], rr.MSG_LOGIC_CRC)

def run_cli(data: bytes, *flags: str) -> tuple[int, str, str]:
    """Runs the CLI on `data` and returns (exit code, stdout, stderr)."""
    path = write(data)
    out, err = io.StringIO(), io.StringIO()
    try:
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = rr.main([str(path), *flags])
    finally:
        path.unlink()
    return code, out.getvalue(), err.getvalue()


def crc_with_args(groups: list[tuple[int, list[bytes]]]) -> bytes:
    return msg(101, rr.MSG_LOGIC_CRC, 3, groups) + msg(101, rr.MSG_CLEAR_GAME_DATA, 3, [])


class MalformedInputTests(unittest.TestCase):
    """Review finding 4: malformed fields must surface as ReplayError, never a bare TypeError/ValueError."""

    def summarize_bytes(self, data: bytes) -> dict:
        path = write(data)
        try:
            return rr.summarize(path)
        finally:
            path.unlink()

    def assert_replay_error(self, data: bytes):
        with self.assertRaises(rr.ReplayError) as ctx:
            self.summarize_bytes(data)
        self.assertIs(type(ctx.exception), rr.ReplayError)

    def test_crc_float_first_argument(self):
        self.assert_replay_error(header(frames=101) + crc_with_args([(1, [struct.pack("<f", 1.5)])]))

    def test_crc_unknown_first_argument(self):
        self.assert_replay_error(header(frames=101) + crc_with_args([(11, [b""])]))

    def test_crc_without_arguments(self):
        self.assert_replay_error(header(frames=101) + crc_with_args([]))

    def test_crc_aux_arguments_must_be_timestamps(self):
        self.assert_replay_error(header(frames=101) + crc_with_args(
            [(0, [struct.pack("<I", 5)]), (2, [b"\x00", b"\x00"])]))

    def test_non_numeric_local_slot(self):
        with self.assertRaises(rr.ReplayError) as ctx:
            rr.parse_header(header(local="abc"))
        self.assertIs(type(ctx.exception), rr.ReplayError)

    def test_malformed_option_fields(self):
        for bad in ("M=zzzmaps/x;", "MC=nothex;", "SD=notanumber;", "GR=1 x 2;", "S=HAlice,notahex,80,TT,1,8,3,0:;",
                    "S=HAlice:;", "S=Cx,a:;"):
            with self.subTest(options=bad), self.assertRaises(rr.ReplayError) as ctx:
                rr.parse_header(header(options=bad))
            self.assertIs(type(ctx.exception), rr.ReplayError)

    def test_cli_turns_malformed_input_into_structured_error(self):
        cases = {
            "bad local slot": header(local="abc"),
            "bad option field": header(options="MC=nothex;"),
            "float crc": header(frames=101) + crc_with_args([(1, [struct.pack("<f", 1.5)])]),
            "no-arg crc": header(frames=101) + crc_with_args([]),
            "bad magic": b"GENREP" + bytes(64),
        }
        for name, data in cases.items():
            with self.subTest(name):
                code, out, err = run_cli(data)
                self.assertNotEqual(code, 0)
                self.assertEqual(code, 2)
                self.assertTrue(err.startswith("error: "), err)
                self.assertEqual(out, "")

    def test_cli_missing_file(self):
        err = io.StringIO()
        with contextlib.redirect_stderr(err):
            code = rr.main(["definitely-not-here.BfME2Replay"])
        self.assertEqual(code, 2)
        self.assertTrue(err.getvalue().startswith("error: "))


HUMAN = "HAlice,AA94501,8094,TT,1,8,3,0,0,1,0"
AI = "CB,-1,-1,1,1,0,0"


def slots(*records: str) -> str:
    """OPTIONS with the S= field replaced; records are joined with ':' like the retail writer."""
    return OPTIONS[:OPTIONS.index(";S=") + 1] + "S=" + ":".join(records) + ":;"


class StrictDomainTests(unittest.TestCase):
    """Every decoded field is checked against its documented domain (replay-format.md, game options
    string); anything outside it is a ReplayError with CLI exit 2, never a default."""

    def assert_rejected(self, data: bytes, what: str):
        with self.subTest(what):
            code, out, err = run_cli(data)
            self.assertEqual(code, 2, out)
            self.assertTrue(err.startswith("error: "), err)

    def test_documented_slots_still_parse(self):
        o = rr.parse_header(header(options=slots(HUMAN, AI, "O", "X", "X", "X", "X", "X")))[0]["options"]
        self.assertEqual([s["kind"] for s in o["slots"]], ["human", "ai", "open", "closed", "closed", "closed", "closed", "closed"])
        self.assertEqual(o["slots"][0]["accepted"], True)
        self.assertEqual(o["slots"][0]["has_map"], True)
        self.assertEqual(rr.parse_header(header(options=OPTIONS.replace("TT", "FT", 1)))[0]["options"]["slots"][0]["accepted"], False)
        self.assertEqual(o["slots"][1]["ai_level"], "brutal")
        for level, name in (("E", "easy"), ("M", "medium"), ("H", "hard"), ("B", "brutal")):
            ai = rr.parse_header(header(options=slots(HUMAN, f"C{level},-1,-1,1,1,0,0", "O", "X", "X", "X", "X", "X")))
            self.assertEqual(ai[0]["options"]["slots"][1]["ai_level"], name)

    def test_slot_records(self):
        bad = {
            "unknown slot type": slots("Zfoo", AI, "O", "X", "X", "X", "X", "X"),
            "empty slot record": slots("", AI, "O", "X", "X", "X", "X", "X"),
            "open slot with payload": slots("Oxx", AI, "O", "X", "X", "X", "X", "X"),
            "human missing numeric fields": slots("HAlice,AA94501,8094,TT,1,8", AI, "O", "X", "X", "X", "X", "X"),
            "human missing everything": slots("HAlice", AI, "O", "X", "X", "X", "X", "X"),
            "human extra numeric field": slots(HUMAN + ",0", AI, "O", "X", "X", "X", "X", "X"),
            "human flags too short": slots("HAlice,AA94501,8094,T,1,8,3,0,0,1,0", AI, "O", "X", "X", "X", "X", "X"),
            "human flags not T/F": slots("HAlice,AA94501,8094,TX,1,8,3,0,0,1,0", AI, "O", "X", "X", "X", "X", "X"),
            "human flags lower case": slots("HAlice,AA94501,8094,tt,1,8,3,0,0,1,0", AI, "O", "X", "X", "X", "X", "X"),
            "human flags empty": slots("HAlice,AA94501,8094,,1,8,3,0,0,1,0", AI, "O", "X", "X", "X", "X", "X"),
            "human ip not hex": slots("HAlice,ZZ,8094,TT,1,8,3,0,0,1,0", AI, "O", "X", "X", "X", "X", "X"),
            "human ip too long": slots("HAlice,1AA945011,8094,TT,1,8,3,0,0,1,0", AI, "O", "X", "X", "X", "X", "X"),
            "human port not a number": slots("HAlice,AA94501,80x4,TT,1,8,3,0,0,1,0", AI, "O", "X", "X", "X", "X", "X"),
            "human port out of range": slots("HAlice,AA94501,70000,TT,1,8,3,0,0,1,0", AI, "O", "X", "X", "X", "X", "X"),
            "human numeric field not a number": slots("HAlice,AA94501,8094,TT,1,8,x,0,0,1,0", AI, "O", "X", "X", "X", "X", "X"),
            "human numeric field below -1": slots("HAlice,AA94501,8094,TT,-2,8,3,0,0,1,0", AI, "O", "X", "X", "X", "X", "X"),
            "ai invalid level": slots(HUMAN, "CZ,-1,-1,1,1,0,0", "O", "X", "X", "X", "X", "X"),
            "ai empty level": slots(HUMAN, "C,-1,-1,1,1,0,0", "O", "X", "X", "X", "X", "X"),
            "ai lower case level": slots(HUMAN, "Cb,-1,-1,1,1,0,0", "O", "X", "X", "X", "X", "X"),
            "ai missing numeric fields": slots(HUMAN, "CB,-1,-1", "O", "X", "X", "X", "X", "X"),
            "ai extra numeric field": slots(HUMAN, AI + ",0", "O", "X", "X", "X", "X", "X"),
            "too few slot records": slots(HUMAN, AI, "O", "X", "X", "X", "X"),
            "too many slot records": slots(HUMAN, AI, "O", "X", "X", "X", "X", "X", "X"),
        }
        for what, options in bad.items():
            self.assert_rejected(header(options=options), what)

    def test_option_fields(self):
        bad = {
            "GT not a number": OPTIONS.replace("GT=0", "GT=oops"),
            "SI not a number": OPTIONS.replace("SI=-1", "SI=x"),
            "MS not a number": OPTIONS.replace("MS=1234", "MS=big"),
            "SD not a number": OPTIONS.replace("SD=801010171", "SD=1.5"),
            "number with sign junk": OPTIONS.replace("GT=0", "GT=+-1"),
            "number with underscore": OPTIONS.replace("SD=801010171", "SD=801_010_171"),
            "MC not hex": OPTIONS.replace("MC=384A860D", "MC=nothex"),
            "GSID not hex": OPTIONS.replace("GSID=329D", "GSID=zz"),
            "M mask not hex": OPTIONS.replace("M=387", "M=3zz"),
            "M too short": "M=38;" + OPTIONS[OPTIONS.index(";") + 1:],
            "GR too few": OPTIONS.replace("GR=1 0 1 100 1000 -1 -1 -1 -1 -1 ", "GR=1 0 1 "),
            "GR too many": OPTIONS.replace("GR=1 0 1 100 1000 -1 -1 -1 -1 -1 ", "GR=1 0 1 100 1000 -1 -1 -1 -1 -1 7 "),
            "GR not numbers": OPTIONS.replace("GR=1 0 1 100", "GR=1 0 q 100"),
            "unknown key": OPTIONS.replace("GT=0;", "GT=0;ZZ=1;"),
            "duplicate key": OPTIONS.replace("GT=0;", "GT=0;GT=1;"),
            "missing key": OPTIONS.replace("GSID=329D;", ""),
            "part without =": OPTIONS.replace("GT=0;", "GT=0;junk;"),
        }
        for what, options in bad.items():
            self.assert_rejected(header(options=options), what)

    def test_integer_and_hex_widths(self):
        """%d fields are signed 32-bit, %X fields and the IP are unsigned 32-bit (at most 8 hex digits)."""
        bad = {
            "SD above int32": OPTIONS.replace("SD=801010171", "SD=4294967296"),
            "SD just above int32": OPTIONS.replace("SD=801010171", "SD=2147483648"),
            "GT above int32": OPTIONS.replace("GT=0", "GT=2147483648"),
            "SI below int32": OPTIONS.replace("SI=-1", "SI=-2147483649"),
            "MS above int32": OPTIONS.replace("MS=1234", "MS=2147483648"),
            "GR value above int32": OPTIONS.replace("GR=1 0", "GR=4294967296 0"),
            "GR value below int32": OPTIONS.replace("GR=1 0", "GR=-2147483649 0"),
            "human colour above int32": slots(HUMAN.replace(",1,8,3,", ",2147483648,8,3,"), AI, "O", "X", "X", "X", "X", "X"),
            "human extra field above int32": slots(HUMAN[:-1] + "2147483648", AI, "O", "X", "X", "X", "X", "X"),
            "AI team above int32": slots(HUMAN, "CB,-1,-1,1,4294967296,0,0", "O", "X", "X", "X", "X", "X"),
            "MC above 32 bits": OPTIONS.replace("MC=384A860D", "MC=1384A860D"),
            "GSID above 32 bits": OPTIONS.replace("GSID=329D", "GSID=100000000"),
            "IP above 32 bits": slots(HUMAN.replace("AA94501", "100000000"), AI, "O", "X", "X", "X", "X", "X"),
        }
        for what, options in bad.items():
            self.assert_rejected(header(options=options), what)
        edge = OPTIONS.replace("SD=801010171", "SD=-2147483648").replace("GT=0", "GT=2147483647") \
            .replace("MC=384A860D", "MC=FFFFFFFF").replace("GSID=329D", "GSID=0")
        o = rr.parse_header(header(options=edge))[0]["options"]
        self.assertEqual((o["seed"], o["GT"], o["map_crc"], o["gsid"]), (-2147483648, 2147483647, 0xFFFFFFFF, 0))

    def test_local_slot_domain(self):
        for local in ("-2", "8", "9", "1.5", " 1", "+1", ""):
            self.assert_rejected(header(local=local), f"local slot {local!r}")
        for local in ("-1", "0", "7"):
            self.assertEqual(rr.parse_header(header(local=local))[0]["local_slot"], int(local))


def crc_stream(groups: list[tuple[int, list[bytes]]]) -> bytes:
    return header(frames=101) + crc_with_args(groups)


T = struct.pack("<I", 7)
CRC_INT = (0, [struct.pack("<I", 0xAB)])
CRC_TS = (9, [T, T])


class CrcSignatureTests(unittest.TestCase):
    """MSG_LOGIC_CRC: int crc, timestamp, timestamp, bool[, bool] (replay-format.md, GameLogic::update 0x62e805)."""

    def assert_rejected(self, groups, what):
        with self.subTest(what):
            code, out, err = run_cli(crc_stream(groups))
            self.assertEqual(code, 2, out)
            self.assertTrue(err.startswith("error: "), err)

    def test_documented_arities_are_accepted(self):
        for bools in ([b"\x00"], [b"\x01", b"\x00"], [b"\x00", b"\x01"]):
            code, out, err = run_cli(crc_stream([CRC_INT, CRC_TS, (2, bools)]), "--json")
            self.assertEqual(code, 0, err)
            rec = json.loads(out)["crc"]["records"][0]
            self.assertEqual((rec["crc"], rec["logic_frame"], rec["flags"]), (0xAB, 7, [b[0] for b in bools]))

    def test_rejected_signatures(self):
        self.assert_rejected([CRC_INT], "integer-only crc")
        self.assert_rejected([CRC_INT, (9, [T])], "crc plus one timestamp")
        self.assert_rejected([CRC_INT, CRC_TS], "no flags")
        self.assert_rejected([CRC_INT, CRC_TS, (2, [b"\x00", b"\x00", b"\x00"])], "three bool flags")
        self.assert_rejected([CRC_INT, (9, [T]), (2, [b"\x00", b"\x00"])], "one timestamp then two bools")
        self.assert_rejected([CRC_INT, CRC_TS, (11, [b"", b""])], "unknown trailing flags (decode to None)")
        self.assert_rejected([CRC_INT, CRC_TS, (11, [b""])], "one unknown trailing flag")
        self.assert_rejected([CRC_INT, CRC_TS, (1, [struct.pack("<f", 1.0), struct.pack("<f", 0.0)])], "float flags")
        self.assert_rejected([CRC_INT, CRC_TS, (2, [b"\xff", b"\x00"])], "boolean byte 255")
        self.assert_rejected([CRC_INT, CRC_TS, (2, [b"\x00", b"\x02"])], "boolean byte 2")
        self.assert_rejected([CRC_INT, CRC_TS, (0, [struct.pack("<i", 0), struct.pack("<i", 0)])], "int flags")
        self.assert_rejected([CRC_INT, CRC_TS, (2, [b"\x00"]), (1, [struct.pack("<f", 0)])], "bool then float")
        self.assert_rejected([CRC_INT, (0, [T, T]), (2, [b"\x00"])], "int timestamps")
        self.assert_rejected([(9, [T]), CRC_TS, (2, [b"\x00"])], "timestamp crc")
        self.assert_rejected([(2, [b"\x01"]), CRC_TS, (2, [b"\x00"])], "bool crc")

    def test_non_crc_messages_reject_non_boolean_bytes(self):
        data = header(frames=101) + msg(5, 1001, 3, [(2, [b"\xff"])]) + msg(101, rr.MSG_CLEAR_GAME_DATA, 3, [])
        code, out, err = run_cli(data)
        self.assertEqual(code, 2)
        self.assertTrue(err.startswith("error: "), err)


class CompletenessTests(unittest.TestCase):
    """Review finding 5: a stream that ends early is incomplete, never success."""

    def test_header_only_500_frames(self):
        code, out, err = run_cli(header(frames=500))
        self.assertEqual(code, 1)
        self.assertIn("incomplete", out)
        code, out, err = run_cli(header(frames=500), "--json")
        doc = json.loads(out)
        self.assertEqual(code, 1)
        self.assertFalse(doc["complete"])
        self.assertTrue(any("MSG_CLEAR_GAME_DATA" in r for r in doc["incomplete_reasons"]), doc["incomplete_reasons"])
        self.assertEqual(doc["message_count"], 0)

    def test_cut_after_one_command(self):
        data = header(frames=500) + msg(7, 1071, 3, [(6, [struct.pack("<3f", 1, 2, 3)])])
        code, out, err = run_cli(data)
        self.assertEqual(code, 1)
        code, out, err = run_cli(data, "--json")
        doc = json.loads(out)
        self.assertEqual((code, doc["complete"], doc["leftover_bytes"], doc["message_count"]), (1, False, 0, 1))
        self.assertTrue(any("MSG_CLEAR_GAME_DATA" in r for r in doc["incomplete_reasons"]))

    def test_terminal_message_before_declared_frame_count(self):
        data = header(frames=500) + msg(7, rr.MSG_CLEAR_GAME_DATA, 3, [])
        code, out, err = run_cli(data, "--json")
        doc = json.loads(out)
        self.assertEqual((code, doc["complete"]), (1, False))
        self.assertTrue(any("500" in r for r in doc["incomplete_reasons"]), doc["incomplete_reasons"])

    def test_zero_header_frame_count_is_unknown_not_a_failure(self):
        # The only retail-format sample has frame_count 0 yet a full stream (replay-format.md).
        code, out, err = run_cli(header(frames=0) + msg(7, 1071, 3, []) + msg(900, rr.MSG_CLEAR_GAME_DATA, 3, []))
        self.assertEqual(code, 0, out)

    def test_complete_stream_passes(self):
        code, out, err = run_cli(header(frames=500) + msg(7, 1071, 3, []) + msg(500, rr.MSG_CLEAR_GAME_DATA, 3, []), "--json")
        doc = json.loads(out)
        self.assertEqual((code, doc["complete"], doc["incomplete_reasons"]), (0, True, []))

    def test_terminal_message_must_be_last(self):
        data = header(frames=0) + msg(7, rr.MSG_CLEAR_GAME_DATA, 3, []) + msg(8, 1071, 3, [])
        code, out, err = run_cli(data, "--json")
        self.assertEqual((code, json.loads(out)["complete"]), (1, False))

    def test_partial_trailing_record_is_incomplete(self):
        good = msg(7, 1071, 3, [])
        code, out, err = run_cli(header(frames=0) + good + msg(8, rr.MSG_CLEAR_GAME_DATA, 3, []) + good[:5], "--json")
        doc = json.loads(out)
        self.assertEqual((code, doc["complete"], doc["leftover_bytes"]), (1, False, 5))


@unittest.skipUnless(os.environ.get("ROTWK_REPLAY"), "set ROTWK_REPLAY to a real .BfME2Replay")
class RealReplayTest(unittest.TestCase):
    def test_parses_to_eof(self):
        s = rr.summarize(Path(os.environ["ROTWK_REPLAY"]))
        self.assertEqual(s["leftover_bytes"], 0)
        self.assertTrue(s["complete"], s["incomplete_reasons"])
        self.assertTrue(s["complete"], s["incomplete_reasons"])
        self.assertGreater(s["message_count"], 0)
        self.assertIn("seed", s["header"]["options"])


if __name__ == "__main__":
    unittest.main()
