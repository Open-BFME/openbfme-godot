# Lane REPLAYEXPORT-1: the training export never lets an identity through. Fuzzed start records and relay logs carry names, account ids, install
# ids, chat lines, network addresses, tickets and Create-a-Hero names in every field the lobby or a client can fill; none of them may appear in any
# exported byte (UTF-8, UTF-16LE, the game id's text or its little-endian bytes), whatever the seed. Games before the notice are never read; an
# unknown record or start kind drops the whole game.
import json
import random
import struct
import sys
from datetime import date
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import replay_export as rx  # noqa: E402

KEY = bytes(range(32))
NOTICE = date(2026, 11, 10)
NOTICE_S = 1794268800  # 2026-11-10T00:00:00Z


def u16le(s):
    return s.encode("utf-16-le")


def secrets_for(rng):
    """identity strings planted in a game (long enough that an accidental match in honest bytes is impossible)"""
    name = "Plyr" + "".join(rng.choice("ABCDEFGHJKLMNPQRSTUVWXYZ") for _ in range(10))
    return {
        "name": name,
        "account": "guest:" + "".join(rng.choice("0123456789abcdef") for _ in range(16)),
        "install": "".join(rng.choice("0123456789abcdef") for _ in range(32)),
        "chat": "chatline " + "".join(rng.choice("abcdefghijklmnop") for _ in range(16)),
        "ip": "203.0.113.%d:%d" % (rng.randrange(1, 255), rng.randrange(1024, 65535)),
        "ticket": "TKT" + "".join(rng.choice("ABCDEFabcdef0123456789-_") for _ in range(40)),
        "hero": "HeroName" + "".join(rng.choice("QWXZ") for _ in range(12)),
        "archive": "".join(rng.choice("0123456789abcdef") for _ in range(64)),
    }


def start_record(game_id, sec, rng, players=3):
    slots = []
    for i in range(players):
        slots.append({"state": "player", "name": sec["name"] + str(i), "faction": rng.randrange(-1, 8), "color": i, "startPos": i, "team": -1,
                      "handicap": -5 * rng.randrange(0, 4), "cah": __import__("base64").b64encode(sec["hero"].encode()).decode(),
                      "account": sec["account"], "install": sec["install"], "platform": "linux", "pid": sec["install"]})
    slots.append({"state": "hard", "name": "Computer " + sec["name"], "faction": 2, "color": 5, "startPos": 5, "team": 1})
    slots.append({"state": "open"})
    return {
        "v": 1, "kind": "openbfme-replay-start", "game": game_id, "runAhead": 3, "crcInterval": 100, "profile": "0123456789abcdef0123456789abcdef",
        "relay": sec["ip"], "ticket": sec["ticket"], "session_key": sec["ticket"], "platforms": ["linux", None], "chatMuted": [False, True],
        "archives": {"0": sec["archive"], "1": sec["archive"]}, "players": [sec["account"]], "unknownTopLevel": sec["name"],
        "setup": {"map": "maps/map mp evendim/map mp evendim.map", "seed": rng.randrange(0, 2**32), "startingCash": 1500,
                  "superweaponRestriction": 0, "legendaryHeroes": 1, "rules": [rng.randrange(-5, 5) for _ in range(10)], "cheatMode": 0,
                  "slots": slots, "hostName": sec["name"], "relay": sec["ip"], "chat": [sec["chat"]]},
    }


def command(sender, ms, cmd):
    return bytes([1, sender]) + struct.pack("<II", ms, len(cmd)) + cmd


def game_message(rng, args=None):
    args = args if args is not None else [(0, struct.pack("<i", rng.randrange(-1000, 1000))), (6, struct.pack("<fff", 1.0, 2.0, 0.0)),
                                          (2, b"\x01"), (3, struct.pack("<I", rng.randrange(1, 9999)))]
    body = struct.pack("<HBI", 1100 + rng.randrange(0, 50), 0, len(args)) + b"".join(bytes([t]) + v for t, v in args)
    return bytes([0x04, 0]) + struct.pack("<I", rng.randrange(0, 5000)) + body


def relay_log(game_id, sec, rng, start=NOTICE_S + 3600, version=4, extra_records=b""):
    mask = 0b111
    head = b"OBFMERLG" + struct.pack("<I", version) + struct.pack("<Q", int(game_id, 16)) + bytes([mask])
    head += (b"\x00" if version == 4 else b"") + struct.pack("<I", start)
    recs = bytearray()
    recs += bytes([2, 0, 1]) + struct.pack("<I", 5)  # EVENT bound
    for i in range(30):
        recs += command(i % 3, 100 * i, game_message(rng))
        recs += command(i % 3, 100 * i + 1, bytes([0x03, i % 3]) + struct.pack("<IH", i, 1))  # FRAMEINFO
    # every carrier of text a client can send
    recs += command(0, 4000, bytes([14, 0]) + struct.pack("<I", 0) + struct.pack("<I", 0b110) + struct.pack("<I", len(sec["chat"])) + u16le(sec["chat"]))
    recs += command(1, 4001, bytes([13, 1]) + struct.pack("<I", 0) + struct.pack("<I", len(sec["name"])) + u16le(sec["name"]))
    recs += command(2, 4002, bytes([20, 2]) + struct.pack("<I", 0) + sec["hero"].encode() + u16le(sec["hero"]))  # HERO
    recs += command(0, 4003, bytes([19, 0]) + struct.pack("<I", 0) + sec["ip"].encode())  # FILE
    recs += command(1, 4004, bytes([18, 1]) + struct.pack("<I", 0) + sec["account"].encode())  # WRAPPER
    recs += command(2, 4005, game_message(rng) + sec["name"].encode())  # a GAMECOMMAND with text appended: must be dropped
    recs += command(0, 4006, game_message(rng, args=[(10, u16le(sec["name"]))]))  # a WIDECHAR argument: no encoding exists: dropped
    recs += command(1, 4007, bytes([0x03, 1]) + struct.pack("<IH", 1, 1) + sec["install"].encode())  # FRAMEINFO of the wrong size
    recs += command(2, 4008, bytes([0x80, 2]) + struct.pack("<I", 0) + sec["ticket"].encode())  # DESYNCREPORT
    recs += bytes([3, 0, 1]) + struct.pack("<I", 4100) + b"\x01"  # GAP
    if version >= 2:
        recs += bytes([5, 1]) + struct.pack("<HI", 1, 4200)  # REJOINED
    if version == 4:
        recs += bytes([7, 0, 1]) + struct.pack("<7Q", *range(7))  # INPUT: dropped
    recs += extra_records
    recs += bytes([4]) + struct.pack("<I", 5000) + b"\x01" + b"\x00" * (8 * 24)  # END
    return head + bytes(recs)


def write_snapshot(root, games):
    snap = root / "2026-11-20"
    (snap / "logs").mkdir(parents=True)
    (snap / "starts").mkdir()
    (snap / "players").mkdir()
    for game_id, start, log, players in games:
        (snap / "logs" / f"{game_id}.obfmerlog").write_bytes(log)
        if start is not None:
            (snap / "starts" / f"{game_id}.json").write_text(json.dumps(start), encoding="utf-8")
        (snap / "players" / f"{game_id}.json").write_text(json.dumps(players), encoding="utf-8")
    return snap


def exported_bytes(out):
    return [(p, p.read_bytes()) for p in sorted(Path(out).rglob("*")) if p.is_file()]


def forbidden(sec, game_id):
    out = []
    for v in sec.values():
        out += [v.encode(), u16le(v)]
    out += [game_id.encode(), struct.pack("<Q", int(game_id, 16)), "Computer".encode()]
    return out


@pytest.mark.parametrize("seed", range(40))
def test_no_identity_leaves_a_fuzzed_game(tmp_path, seed):
    rng = random.Random(seed)
    sec = secrets_for(rng)
    game_id = "%016x" % rng.randrange(1, 2**64)
    version = rng.choice([2, 4])
    snap = write_snapshot(tmp_path / "backup", [(game_id, start_record(game_id, sec, rng), relay_log(game_id, sec, rng, version=version), [sec["account"]])])
    lines = []
    done, skipped, old = rx.export(snap, tmp_path / "out", NOTICE, KEY, log=lines.append)
    assert (done, skipped, old) == (1, 0, 0), lines
    files = exported_bytes(tmp_path / "out")
    assert [p.name for p, _ in files] == ["log.obfmerlog", "start.json"]
    for path, data in files:
        for bad in forbidden(sec, game_id):
            assert bad not in data, f"{path.name} leaks {bad!r} (seed {seed})"
    # what is kept: the orders and frames, the settings; nothing else
    start = json.loads((tmp_path / "out" / rx.pseudonym(KEY, game_id) / "start.json").read_text())
    assert start["kind"] == "openbfme-training-start" and start["game"] == rx.pseudonym(KEY, game_id)
    assert set(start) <= {"v", "kind", "game", "setup", "runAhead", "crcInterval", "profile"}
    assert set(start["setup"]) <= {"map", "mapStartCount", "seed", "startingCash", "superweaponRestriction", "rules", "legendaryHeroes", "cheatMode", "slots"}
    for s in start["setup"]["slots"]:
        assert set(s) <= {"state", "faction", "color", "startPos", "team", "handicap"}
    assert any("60 commands kept, 9 dropped" in line for line in lines), lines  # 30 orders + 30 frames kept; every text carrier dropped


def test_games_before_the_notice_are_never_read_and_unknown_kinds_drop_the_game(tmp_path):
    rng = random.Random(7)
    sec = secrets_for(rng)
    before, unknown, canonical, war, nostart = ("%016x" % (i + 1) for i in range(5))
    war_start = start_record(war, sec, rng)
    war_start["setup"]["gameMode"] = 1
    v2_start = dict(start_record(unknown, sec, rng), v=2)
    snap = write_snapshot(tmp_path / "backup", [
        (before, start_record(before, sec, rng), relay_log(before, sec, rng, start=NOTICE_S - 1), []),
        (unknown, v2_start, relay_log(unknown, sec, rng), []),
        (canonical, start_record(canonical, sec, rng), relay_log(canonical, sec, rng, extra_records=bytes([6]) + struct.pack("<I", 2) + b"xx"), []),
        (war, war_start, relay_log(war, sec, rng), []),
        (nostart, None, relay_log(nostart, sec, rng), []),
    ])
    lines = []
    assert rx.export(snap, tmp_path / "out", NOTICE, KEY, log=lines.append) == (0, 4, 1)
    assert exported_bytes(tmp_path / "out") == []
    text = "\n".join(lines)
    assert before not in text and rx.pseudonym(KEY, before) not in text  # a game before the notice is not even named
    for game_id in (unknown, canonical, war, nostart):
        assert game_id not in text  # the report names pseudonyms only


def test_an_unknown_record_kind_or_a_cut_log_drops_the_game(tmp_path):
    rng = random.Random(9)
    sec = secrets_for(rng)
    a, b = "%016x" % 11, "%016x" % 12
    good = relay_log(b, sec, rng)
    snap = write_snapshot(tmp_path / "backup", [
        (a, start_record(a, sec, rng), relay_log(a, sec, rng, extra_records=bytes([9, 1, 2, 3])), []),
        (b, start_record(b, sec, rng), good[:-10], []),  # cut: no END
    ])
    assert rx.export(snap, tmp_path / "out", NOTICE, KEY, log=lambda _: None) == (0, 2, 0)
    assert exported_bytes(tmp_path / "out") == []


def test_the_pseudonym_is_keyed_and_stable(tmp_path):
    assert rx.pseudonym(KEY, "0123456789abcdef") == rx.pseudonym(KEY, "0123456789abcdef")
    assert rx.pseudonym(KEY, "0123456789abcdef") != rx.pseudonym(bytes(32), "0123456789abcdef")
    keyfile = tmp_path / "k"
    keyfile.write_text("ab" * 16)
    with pytest.raises(SystemExit):
        rx.read_key(keyfile)  # 16 bytes: too short
    keyfile.write_text("ab" * 32)
    assert len(rx.read_key(keyfile)) == 32


def test_the_notice_date_is_required():
    with pytest.raises(SystemExit):
        rx.main(["--backup", "x", "--out", "y", "--key-file", "z"])


class _Resp:
    status = 200

    def __enter__(self):
        return self

    def __exit__(self, *a):
        return False


def test_the_upload_sends_each_game_once_with_a_signed_request(tmp_path):
    rng = random.Random(3)
    sec = secrets_for(rng)
    game_id = "%016x" % 99
    snap = write_snapshot(tmp_path / "backup", [(game_id, start_record(game_id, sec, rng), relay_log(game_id, sec, rng), [])])
    rx.export(snap, tmp_path / "out", NOTICE, KEY, log=lambda _: None)
    sent = []

    def opener(req, timeout):
        sent.append((req.get_method(), req.full_url, dict(req.header_items()), req.data))
        return _Resp()

    endpoint = "https://acct.r2.cloudflarestorage.com"
    assert rx.upload(tmp_path / "out", endpoint, "openbfme-training", "games", "AKID", "SECRET", opener=opener, log=lambda _: None) == 1
    pseudo = rx.pseudonym(KEY, game_id)
    assert [(m, u) for m, u, _, _ in sent] == [("PUT", f"{endpoint}/openbfme-training/games/{pseudo}/start.json"),
                                              ("PUT", f"{endpoint}/openbfme-training/games/{pseudo}/log.obfmerlog")]
    for _, _, headers, body in sent:
        auth = headers["Authorization"]
        assert auth.startswith("AWS4-HMAC-SHA256 Credential=AKID/") and "/auto/s3/aws4_request" in auth
        assert "SignedHeaders=host;x-amz-content-sha256;x-amz-date" in auth
        assert headers["X-amz-content-sha256"] == __import__("hashlib").sha256(body).hexdigest()
        assert "SECRET" not in auth
    # once uploaded, a game is not sent again
    assert rx.upload(tmp_path / "out", endpoint, "openbfme-training", "games", "AKID", "SECRET", opener=opener, log=lambda _: None) == 0
    with pytest.raises(SystemExit):
        rx.upload(tmp_path / "out", "http://insecure", "openbfme-training", "games", "AKID", "SECRET", opener=opener, log=lambda _: None)


# Sol r1 (High): a map path is the client's string; only the original game's official multiplayer maps keep their name
@pytest.mark.parametrize("planted", ["maps/{name}/{name}.map", "\\\\{ip}\\maps\\arena.map", "maps/map mp {name}/map mp {name}.map", "maps/{account}.map"])
def test_a_custom_map_path_becomes_a_keyed_pseudonym(tmp_path, planted):
    rng = random.Random(21)
    sec = secrets_for(rng)
    game_id = "%016x" % 21
    start = start_record(game_id, sec, rng)
    start["setup"]["map"] = planted.format(name=sec["name"], ip=sec["ip"].split(":")[0], account=sec["account"])
    snap = write_snapshot(tmp_path / "backup", [(game_id, start, relay_log(game_id, sec, rng), [])])
    assert rx.export(snap, tmp_path / "out", NOTICE, KEY, log=lambda _: None) == (1, 0, 0)
    out = json.loads((tmp_path / "out" / rx.pseudonym(KEY, game_id) / "start.json").read_text())
    assert out["setup"]["map"].startswith("custom-") and len(out["setup"]["map"]) == len("custom-") + 16
    for _, data in exported_bytes(tmp_path / "out"):
        for bad in forbidden(sec, game_id) + [sec["ip"].split(":")[0].encode()]:
            assert bad not in data


def test_an_official_map_keeps_its_name():
    official = "maps/map mp evendim/map mp evendim.map"
    assert official in rx.OFFICIAL_MAPS
    assert rx.map_name("Maps/Map MP Evendim/Map MP Evendim.map", KEY) == official
    assert len(rx.OFFICIAL_MAPS) == 72


def test_a_cut_version4_header_skips_that_game_only(tmp_path):
    rng = random.Random(5)
    sec = secrets_for(rng)
    cut, good = "%016x" % 31, "%016x" % 32
    snap = write_snapshot(tmp_path / "backup", [
        (cut, start_record(cut, sec, rng), relay_log(cut, sec, rng)[:25], []),
        (good, start_record(good, sec, rng), relay_log(good, sec, rng), []),
    ])
    assert rx.export(snap, tmp_path / "out", NOTICE, KEY, log=lambda _: None) == (1, 1, 0)


def test_a_large_selection_order_is_kept(tmp_path):
    rng = random.Random(6)
    big = game_message(rng, args=[(2, b"\x01")] + [(3, struct.pack("<I", i + 1)) for i in range(64)])
    assert rx.command_ok(big)
    assert not rx.command_ok(big + b"x")
