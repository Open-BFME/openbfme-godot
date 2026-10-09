#!/usr/bin/env python3
"""Parser for Rise of the Witch-king 2.01 replays (*.BfME2Replay, magic "BFME2RPL").

Layout established from the retail recorder (RecorderClass::startRecording,
readReplayHeader, appendNextCommand, readArgument) and cross-checked against
ZH Recorder.cpp and the BFME1 matching decompile. See
workspace/rebuild/reports/replay-format.md for offsets, evidence and confidence.

    python tools/replay/rotwk_replay.py <file> [--json] [--messages N]

Stdlib only. Never writes anything.
"""
from __future__ import annotations

import argparse
import collections
import json
import re
import struct
import sys
from pathlib import Path

MAGIC = b"BFME2RPL"
MAX_SLOTS = 8

# GameMessageArgumentDataType -> byte size (RotWK readArgument; same order as ZH).
ARG_TYPES = {
    0: ("int", 4), 1: ("real", 4), 2: ("bool", 1), 3: ("object_id", 4),
    4: ("drawable_id", 4), 5: ("team_id", 4), 6: ("location", 12),
    7: ("pixel", 8), 8: ("pixel_region", 16), 9: ("timestamp", 4), 10: ("widechar", 2),
}

ARG_INT, ARG_BOOL, ARG_TIMESTAMP = 0, 2, 9
CRC_SIGNATURES = ([ARG_INT, ARG_TIMESTAMP, ARG_TIMESTAMP, ARG_BOOL],
                  [ARG_INT, ARG_TIMESTAMP, ARG_TIMESTAMP, ARG_BOOL, ARG_BOOL])

MSG_CLEAR_GAME_DATA = 29
MSG_BEGIN_NETWORK_MESSAGES = 1000
MSG_LOGIC_CRC = 1098

# GameMessage::Type values 1001..1147, read out of the retail
# GameMessage::getCommandAsAsciiString switch (jump table for 1002..1147, 1001 special-cased).
_NETWORK_NAMES = """
CREATE_SELECTED_GROUP CREATE_SELECTED_GROUP_NO_SOUND CREATE_SELECTED_GROUP_IDLE_WORKER_VOICE
DESTROY_SELECTED_GROUP REMOVE_FROM_SELECTED_GROUP
CREATE_TEAM0 CREATE_TEAM1 CREATE_TEAM2 CREATE_TEAM3 CREATE_TEAM4 CREATE_TEAM5 CREATE_TEAM6
CREATE_TEAM7 CREATE_TEAM8 CREATE_TEAM9
SELECT_TEAM0 SELECT_TEAM1 SELECT_TEAM2 SELECT_TEAM3 SELECT_TEAM4 SELECT_TEAM5 SELECT_TEAM6
SELECT_TEAM7 SELECT_TEAM8 SELECT_TEAM9
ADD_TEAM0 ADD_TEAM1 ADD_TEAM2 ADD_TEAM3 ADD_TEAM4 ADD_TEAM5 ADD_TEAM6 ADD_TEAM7 ADD_TEAM8 ADD_TEAM9
DO_ATTACKSQUAD DO_WEAPON DO_WEAPON_AT_LOCATION DO_WEAPON_AT_OBJECT DO_SPECIAL_POWER
DO_SPECIAL_POWER_AT_LOCATION DO_SPECIAL_POWER_AT_OBJECT SET_RALLY_POINT PURCHASE_SCIENCE
QUEUE_UPGRADE CANCEL_UPGRADE QUEUE_UNIT_CREATE CANCEL_UNIT_CREATE FOUNDATION_CONSTRUCT
DOZER_CONSTRUCT DOZER_CANCEL_CONSTRUCT SELL EXIT EVACUATE EVACUATE_CONTESTERS SACRIFICE
COMBATDROP_AT_LOCATION COMBATDROP_AT_OBJECT COMBINE_HORDES_WITH_OBJECT AREA_SELECTION
DO_ATTACK_OBJECT DO_FORCE_ATTACK_OBJECT DO_FORCE_ATTACK_GROUND GET_REPAIRED GET_HEALED DO_REPAIR
RESUME_CONSTRUCTION ENTER DOCK HARVEST DO_MOVETO DO_ATTACKMOVETO DO_FORCEMOVETO ADD_WAYPOINT
DO_GUARD_POSITION DO_GUARD_OBJECT DO_STOP DO_SCATTER OPEN_GATE DO_CHEER CLOSE_GATE SWITCH_WEAPONS
CONVERT_TO_CARBOMB CAPTUREBUILDING CASTLE_UNPACK CASTLE_PACK CASTLE_UNPACK_EXPLICIT_OBJECT
SNIPE_VEHICLE DO_SPECIAL_POWER_OVERRIDE_DESTINATION DO_SALVAGE CLEAR_INGAME_POPUP_MESSAGE
PLACE_BEACON REMOVE_BEACON SET_BEACON_TEXT SET_REPLAY_CAMERA SELF_DESTRUCT CREATE_FORMATION
LOGIC_CRC SET_MINE_CLEARING_DETAIL DO_USER1 DO_USER2 DO_USER3 DO_USER4 MOVE_ARMY_TO_POSITION
AUTO_SAVE CHANGE_CAMERA_ARRIVED_AT_WAYPOINTID HORDE_TOGGLE_FORMATION ONE_RING CREW_EVACUATE
DO_SPELLBOOK_SPECIAL_POWER WEAPONSET_TOGGLE DO_AUTO_ABILITY DO_AUTO_ABILITY_WEAPON REVIVE
TOGGLE_NO_AUTO_ACQUIRE WAKE_AUTO_PICKUP START_SELF_REPAIR SUMMON_REINFORCEMENTS
CALL_IN_REINFORCEMENTS HORDE_SET_FORMATION CREATE_SELECT_ALL_GROUP ENABLE_RETALIATION_MODE
WALL_HUB_CONSTRUCT_SPAN DO_MOVETO_FORMATION DO_MOVE_AND_ORIENTATE_OBJECTTO GIVE_MONEY
DO_ROTATE_FIRINGARC CHANGE_STANCE CHANGE_ORDERMODE ACTIONQUEUE_EXECUTE_PLANNED
ACTIONQUEUE_CLEAR_ALL ACTIONQUEUE_CLEAR_PLANNED ACTIONQUEUE_CLEAR_LAST START_NEIGHBORHOOD_REPAIR
CANCEL_NEIGHBORHOOD ORDER_SYNCHRONIZE ADD_ALL_FACTION_UPGRADE
ADD_TO_TEAM0 ADD_TO_TEAM1 ADD_TO_TEAM2 ADD_TO_TEAM3 ADD_TO_TEAM4 ADD_TO_TEAM5 ADD_TO_TEAM6
ADD_TO_TEAM7 ADD_TO_TEAM8 ADD_TO_TEAM9
""".split()
MSG_NAMES = {1001 + i: "MSG_" + name for i, name in enumerate(_NETWORK_NAMES)}
MSG_NAMES[MSG_CLEAR_GAME_DATA] = "MSG_CLEAR_GAME_DATA"
MSG_NAMES[MSG_BEGIN_NETWORK_MESSAGES] = "MSG_BEGIN_NETWORK_MESSAGES"
assert MSG_NAMES[MSG_LOGIC_CRC] == "MSG_LOGIC_CRC" and MSG_NAMES[1147] == "MSG_ADD_TO_TEAM9"

AI_LEVELS = {"E": "easy", "M": "medium", "H": "hard", "B": "brutal"}


class ReplayError(ValueError):
    pass


class TruncatedError(ReplayError):
    """The data ended inside a record. parse_messages reports this as leftover bytes; every other
    ReplayError (a value outside its domain) is fatal."""


class Reader:
    def __init__(self, data: bytes, pos: int = 0):
        self.data, self.pos = data, pos

    def take(self, n: int) -> bytes:
        if self.pos + n > len(self.data):
            raise TruncatedError(f"truncated: need {n} bytes at offset {self.pos:#x}, file is {len(self.data):#x}")
        out = self.data[self.pos:self.pos + n]
        self.pos += n
        return out

    def unpack(self, fmt: str):
        vals = struct.unpack("<" + fmt, self.take(struct.calcsize("<" + fmt)))
        return vals[0] if len(vals) == 1 else vals

    def wstr(self) -> str:
        chars = []
        while (c := self.unpack("H")) != 0:
            chars.append(c)
        return struct.pack(f"<{len(chars)}H", *chars).decode("utf-16-le", "replace")

    def astr(self) -> str:
        end = self.data.find(b"\x00", self.pos)
        if end < 0:
            raise ReplayError(f"unterminated ascii string at {self.pos:#x}")
        out = self.data[self.pos:end].decode("latin-1")
        self.pos = end + 1
        return out


def parse_header(data: bytes) -> tuple[dict, int]:
    """Parses the replay header. Every malformed field is a ReplayError, never a bare ValueError."""
    try:
        return _parse_header(data)
    except ReplayError:
        raise
    except (ValueError, TypeError, IndexError, OverflowError, struct.error) as e:
        raise ReplayError(f"malformed header: {type(e).__name__}: {e}") from e


def _parse_header(data: bytes) -> tuple[dict, int]:
    r = Reader(data)
    magic = r.take(8)
    if magic != MAGIC:
        raise ReplayError(f"bad magic {magic!r}; expected {MAGIC!r}")
    h: dict = {"magic": magic.decode()}
    h["start_time"], h["end_time"], h["frame_count"] = r.unpack("III")
    h["net_crc_interval"], h["desync_frame"] = r.unpack("Ii")
    h["flag_0x1c"] = r.unpack("B")
    h["player_disconnected"] = list(r.take(MAX_SLOTS))
    h["replay_name"] = r.wstr()
    st = r.unpack("8H")
    h["system_time"] = dict(zip(("year", "month", "day_of_week", "day", "hour", "minute", "second", "ms"), st))
    h["version"] = r.wstr()
    h["build_time"] = r.wstr()
    h["ini_crc"] = r.unpack("I")
    h["fast_game_play"] = r.unpack("B")
    h["starting_money"] = r.unpack("i")
    h["game_options_offset"] = r.pos
    h["game_options"] = r.astr()
    h["local_slot"] = _dec(r.astr(), f"local slot (offset {r.pos:#x})", -1, MAX_SLOTS - 1)  # "%d", -1..7
    heroes = []
    for slot in range(MAX_SLOTS):
        if r.unpack("B"):
            size = r.unpack("I")
            blob = r.take(size)
            heroes.append({"slot": slot, "size": size, "head": blob[:16].hex()})
    h["create_a_hero"] = heroes
    h["difficulty"], h["original_game_mode"], h["rank_points"], h["max_fps"] = r.unpack("iiii")
    h["header_size"] = r.pos
    h["options"] = parse_options(h["game_options"])
    return h, r.pos


_DEC = re.compile(r"-?[0-9]+")
_HEX = re.compile(r"[0-9A-Fa-f]+")


INT32_MIN, INT32_MAX = -(1 << 31), (1 << 31) - 1


def _dec(text: str, what: str, minimum: int = INT32_MIN, maximum: int = INT32_MAX) -> int:
    """Strict decimal ("%d" of a 32-bit int): optional '-', ASCII digits only, within signed 32 bits
    (and any tighter bounds). Anything else is a ReplayError."""
    if not _DEC.fullmatch(text):
        raise ReplayError(f"{what}: {text!r} is not a decimal integer")
    val = int(text)
    if not max(minimum, INT32_MIN) <= val <= min(maximum, INT32_MAX):
        raise ReplayError(f"{what}: {val} is outside {max(minimum, INT32_MIN)}..{min(maximum, INT32_MAX)}")
    return val


def _hex(text: str, what: str, max_digits: int = 8) -> int:
    """Strict hex ("%X"): 1..max_digits hex digits, no sign, prefix or whitespace."""
    if not _HEX.fullmatch(text) or len(text) > max_digits:
        raise ReplayError(f"{what}: {text!r} is not 1..{max_digits} hex digits")
    return int(text, 16)


def _flag(ch: str, what: str) -> bool:
    if ch not in ("T", "F"):
        raise ReplayError(f"{what}: {ch!r} is not T or F")
    return ch == "T"


# Documented slot records (replay-format.md, "Game options string"):
#   human H<name>,<IP hex>,<port>,<accepted T/F><hasMap T/F>,color,template,startPos,team,x20,x40,b
#   AI    C<E|M|H|B>,color,template,startPos,team,x20,b          O = open, X = closed
# The numeric fields are decimal ints, -1 meaning random/unset. The AI level letter is the retail
# state 2/3/4/5 -> E/M/H/B mapping (RotWK has four AI levels).
SLOT_FIELDS = ("color", "player_template", "start_pos", "team")
HUMAN_NUMERIC_FIELDS = 7  # color, template, startPos, team, x20, x40, b
AI_NUMERIC_FIELDS = 6     # color, template, startPos, team, x20, b


def parse_slot(text: str) -> dict:
    if text == "X":
        return {"kind": "closed"}
    if text == "O":
        return {"kind": "open"}
    if text.startswith("H"):
        fields = text[1:].split(",")
        if len(fields) != 4 + HUMAN_NUMERIC_FIELDS:
            raise ReplayError(f"human slot {text!r}: expected name, ip, port, flags and {HUMAN_NUMERIC_FIELDS} "
                              f"numeric fields, got {len(fields)} fields")
        name, ip, port, flags, *rest = fields
        if len(flags) != 2:
            raise ReplayError(f"human slot {text!r}: flags {flags!r} must be two T/F characters")
        nums = [_dec(x, f"human slot {text!r} numeric field", minimum=-1) for x in rest]
        slot = {"kind": "human", "name": name, "ip": f"{_hex(ip, f'human slot {text!r} ip'):08X}",
                "port": _dec(port, f"human slot {text!r} port", 0, 65535),
                "accepted": _flag(flags[0], f"human slot {text!r} accepted"),
                "has_map": _flag(flags[1], f"human slot {text!r} hasMap")}
    elif text.startswith("C"):
        level, *rest = text[1:].split(",")
        if level not in AI_LEVELS:
            raise ReplayError(f"AI slot {text!r}: level {level!r} is not one of {'/'.join(AI_LEVELS)}")
        if len(rest) != AI_NUMERIC_FIELDS:
            raise ReplayError(f"AI slot {text!r}: expected {AI_NUMERIC_FIELDS} numeric fields, got {len(rest)}")
        nums = [_dec(x, f"AI slot {text!r} numeric field", minimum=-1) for x in rest]
        slot = {"kind": "ai", "ai_level": AI_LEVELS[level]}
    else:
        raise ReplayError(f"unknown slot record {text!r}")
    for key, val in zip(SLOT_FIELDS, nums):
        slot[key] = val
    slot["extra"] = nums[4:]
    return slot


OPTION_KEYS = ("M", "MC", "MS", "SD", "GSID", "GT", "SI", "GR", "S")
GAME_RULE_COUNT = 10


def parse_options(text: str) -> dict:
    """Game options string `M=%3.3x%s;MC=%X;MS=%d;SD=%d;GSID=%X;GT=%d;SI=%d;GR=<10 "%d ">;S=<8 slots>;`.

    Every key is required exactly once; unknown or duplicate keys, missing keys and out-of-domain
    values are errors.
    """
    out: dict = {}
    seen: list[str] = []
    parts = text.split(";")
    if parts[-1] == "":
        parts.pop()  # the string ends with ';'
    for part in parts:
        key, eq, val = part.partition("=")
        if not eq or key not in OPTION_KEYS:
            raise ReplayError(f"game options: unknown or malformed field {part!r}")
        if key in seen:
            raise ReplayError(f"game options: duplicate field {key}")
        seen.append(key)
        if key == "M":
            if len(val) <= 3:
                raise ReplayError(f"game options: M={val!r} lacks a map path after the 3-digit contents mask")
            out["map_contents_mask"] = _hex(val[:3], "game options M contents mask", 3)
            out["map"] = val[3:]
        elif key == "MC":
            out["map_crc"] = _hex(val, "game options MC")
        elif key == "GSID":
            out["gsid"] = _hex(val, "game options GSID")
        elif key == "MS":
            out["map_size"] = _dec(val, "game options MS", 0)
        elif key == "SD":
            out["seed"] = _dec(val, "game options SD")
        elif key == "GR":
            rules = val.split()
            if len(rules) != GAME_RULE_COUNT:
                raise ReplayError(f"game options: GR has {len(rules)} values, expected {GAME_RULE_COUNT}")
            out["game_rules"] = [_dec(x, "game options GR") for x in rules]
        elif key == "S":
            records = val.split(":")
            if records[-1] == "":
                records.pop()  # the string ends with ':'
            if len(records) != MAX_SLOTS:
                raise ReplayError(f"game options: S has {len(records)} slot records, expected {MAX_SLOTS}")
            out["slots"] = [parse_slot(s) for s in records]
        else:  # GT, SI
            out[key] = _dec(val, f"game options {key}")
    missing = [k for k in OPTION_KEYS if k not in seen]
    if missing:
        raise ReplayError(f"game options: missing field(s) {', '.join(missing)}")
    return out


def _decode_arg(kind: int, raw: bytes):
    if kind in (0, 5):
        return struct.unpack("<i", raw)[0]
    if kind in (3, 4, 9):
        return struct.unpack("<I", raw)[0]
    if kind == 1:
        return struct.unpack("<f", raw)[0]
    if kind == 2:
        if raw[0] > 1:
            raise ReplayError(f"boolean argument byte is {raw[0]}, expected 0 or 1")
        return raw[0]
    if kind == 6:
        return list(struct.unpack("<3f", raw))
    if kind == 7:
        return list(struct.unpack("<2i", raw))
    if kind == 8:
        return list(struct.unpack("<4i", raw))
    if kind == 10:
        return raw.decode("utf-16-le", "replace")
    return None


def parse_messages(data: bytes, pos: int) -> tuple[list[dict], int]:
    """Command stream: frame u32, type u32, player i32, n u8, n*(argtype u8, count u8), args.

    Mirrors RecorderClass::appendNextCommand: an unknown arg type reads zero bytes.
    Returns (messages, leftover_bytes). A partial trailing record is reported as leftover.
    """
    msgs = []
    r = Reader(data, pos)
    while r.pos < len(data):
        start = r.pos
        try:
            frame, mtype, player = r.unpack("IIi")
            ntypes = r.unpack("B")
            sig = [r.unpack("BB") for _ in range(ntypes)]
            args = []
            for kind, count in sig:
                size = ARG_TYPES.get(kind, ("unknown", 0))[1]
                for _ in range(count):
                    args.append((kind, _decode_arg(kind, r.take(size))))
        except TruncatedError:
            return msgs, len(data) - start
        msgs.append({"offset": start, "frame": frame, "type": mtype, "player": player,
                     "signature": sig, "args": args})
    return msgs, 0


def crc_records(msgs: list[dict]) -> list[dict]:
    """MSG_LOGIC_CRC records. Signature from GameLogic::update 0x62e805 (replay-format.md): int crc,
    timestamp (GameLogic+0x38), timestamp (logic frame), bool (in playback), and a final bool(0) that
    is skipped when -deepCRC/-liteCRC is set. So 4 or 5 arguments; anything else is an error."""
    out = []
    for m in msgs:
        if m["type"] != MSG_LOGIC_CRC:
            continue
        where = f"MSG_LOGIC_CRC at offset {m['offset']:#x}"
        kinds = [k for k, _ in m["args"]]
        if kinds not in CRC_SIGNATURES:
            names = [ARG_TYPES.get(k, ("unknown",))[0] for k in kinds]
            raise ReplayError(f"{where}: arguments {names} are not int, timestamp, timestamp, bool[, bool]")
        vals = [v for _, v in m["args"]]  # bools were already checked to be 0/1 by _decode_arg
        out.append({"frame": m["frame"], "player": m["player"], "crc": vals[0] & 0xFFFFFFFF,
                    "aux_timestamp": vals[1], "logic_frame": vals[2], "flags": vals[3:]})
    return out


def incomplete_reasons(header: dict, msgs: list[dict], leftover: int) -> list[str]:
    """Why a command stream cannot be a whole recording (empty list = complete).

    ZH Recorder::updateRecord writes MSG_CLEAR_GAME_DATA last and stopRecording -> logGameEnd stores
    the logic frame of that moment as the header frame count (Recorder.cpp:504, :219). So a whole
    recording ends with the terminal message at a frame >= the header count. Header count 0 means
    unknown (the retail-format sample holds 0), not "no frames".
    """
    reasons = []
    if leftover:
        reasons.append(f"{leftover} trailing bytes do not form a complete record")
    if not msgs or msgs[-1]["type"] != MSG_CLEAR_GAME_DATA:
        reasons.append("stream does not end with MSG_CLEAR_GAME_DATA")
    declared = header["frame_count"]
    last = msgs[-1]["frame"] if msgs else 0
    if declared and last < declared:
        reasons.append(f"header declares {declared} frames but the stream ends at frame {last}")
    return reasons


def summarize(path: Path) -> dict:
    data = path.read_bytes()
    header, pos = parse_header(data)
    msgs, leftover = parse_messages(data, pos)
    crcs = crc_records(msgs)
    reasons = incomplete_reasons(header, msgs, leftover)
    by_frame: dict = collections.OrderedDict()
    for c in crcs:
        by_frame.setdefault(c.get("logic_frame", c["frame"]), {})[c["player"]] = c["crc"]
    frames = list(by_frame)
    intervals = sorted({b - a for a, b in zip(frames, frames[1:])})
    hist = collections.Counter(m["type"] for m in msgs)
    return {
        "file": path.name, "size": len(data), "header": header,
        "message_count": len(msgs), "leftover_bytes": leftover,
        "complete": not reasons, "incomplete_reasons": reasons,
        "last_message_frame": msgs[-1]["frame"] if msgs else None,
        "players_in_stream": sorted({m["player"] for m in msgs if m["type"] != MSG_CLEAR_GAME_DATA}),
        "histogram": {f"{t} {MSG_NAMES.get(t, '?')}": n for t, n in sorted(hist.items())},
        "crc": {"count": len(crcs), "checkpoints": len(frames), "intervals": intervals,
                "disagreements": [f for f, v in by_frame.items() if len(set(v.values())) > 1],
                "records": crcs},
        "_messages": msgs,
    }


def _fmt_msg(m: dict) -> str:
    args = ", ".join(f"{ARG_TYPES.get(k, ('?',))[0]}={v}" for k, v in m["args"])
    return f"  f={m['frame']:>6} p={m['player']:>2} {m['type']:>4} {MSG_NAMES.get(m['type'], '?'):<40} {args}"


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("file", type=Path)
    ap.add_argument("--json", action="store_true", help="emit JSON")
    ap.add_argument("--messages", type=int, default=0, metavar="N", help="also list the first N messages")
    a = ap.parse_args(argv)
    try:
        s = summarize(a.file)
    except (ReplayError, OSError) as e:
        print(f"error: {e}", file=sys.stderr)
        return 2
    msgs = s.pop("_messages")
    if a.json:
        s["messages"] = msgs[:a.messages]
        print(json.dumps(s, indent=1))
        return 0 if s["complete"] else 1
    h, o = s["header"], s["header"]["options"]
    print(f"{s['file']}  {s['size']} bytes  header {h['header_size']} bytes")
    for k in ("start_time", "end_time", "frame_count", "net_crc_interval", "desync_frame", "flag_0x1c",
              "player_disconnected", "replay_name", "system_time", "version", "build_time", "fast_game_play",
              "starting_money", "local_slot", "difficulty", "original_game_mode", "rank_points", "max_fps"):
        print(f"  {k:<20} {h[k]}")
    print(f"  {'ini_crc':<20} {h['ini_crc']:#010x}")
    print(f"  {'create_a_hero':<20} {h['create_a_hero'] or 'none'}")
    print(f"  game_options         {h['game_options']}")
    print(f"map {o.get('map')}  contents={o.get('map_contents_mask', 0):#x} crc={o.get('map_crc', 0):#010x} "
          f"size={o.get('map_size')}  seed={o.get('seed')}  GSID={o.get('gsid', 0):#x} GT={o.get('GT')} "
          f"SI={o.get('SI')} GR={o.get('game_rules')}")
    for i, slot in enumerate(o.get("slots", [])):
        print(f"  slot {i}: {slot}")
    print(f"messages {s['message_count']}  leftover {s['leftover_bytes']}  last frame {s['last_message_frame']}  "
          f"players {s['players_in_stream']}")
    print("status: " + ("complete" if s["complete"] else "incomplete: " + "; ".join(s["incomplete_reasons"])))
    for name, n in s["histogram"].items():
        print(f"  {n:>6}  {name}")
    c = s["crc"]
    print(f"CRC messages {c['count']} at {c['checkpoints']} checkpoints, interval(s) {c['intervals']}, "
          f"cross-player disagreements {c['disagreements'] or 'none'}")
    for rec in c["records"]:
        print(f"  f={rec['frame']:>6} p={rec['player']:>2} crc={rec['crc']:08X} "
              f"logic_frame={rec.get('logic_frame')} flags={rec['flags']}")
    if a.messages:
        print(f"first {a.messages} messages:")
        for m in msgs[:a.messages]:
            print(_fmt_msg(m))
    return 0 if s["complete"] else 1


if __name__ == "__main__":
    sys.exit(main())
