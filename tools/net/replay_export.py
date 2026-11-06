#!/usr/bin/env python3
# Lane REPLAYEXPORT-1 (owner-approved 2026-11-04): anonymised online games for training the computer opponent (an AI developer's data set).
# Reads the lobby backup's newest snapshot (services/online/deploy/lobby_backup.py: DIR/<UTC date>/{logs,starts}/<game id>.<ext>) and writes, for
# every game that started on or after the privacy notice date, an anonymised copy:
#   OUT/<pseudonym>/start.json      the game's settings: an ALLOW-LIST of fields (no name, account, install, archive list, Create-a-Hero, chat)
#   OUT/<pseudonym>/log.obfmerlog   the relay log with only the game's orders and frame bookkeeping (COMMAND records of an allow-listed kind),
#                                   its game id replaced by the pseudonym and its start time cut to the day
# Owner's conditions: we run it (no lobby access for the data's user); no player names, account ids, install ids, chat or network addresses.
# Owner 2026-11-04: no privacy notice; --notice-date (required) is the day the export was first enabled: no game that started before that day is
# exported (a relay log's header is read to learn its start time; nothing else of an earlier game is read or written).
# Fails closed: a record kind, command kind, start-record kind or log version it does not know drops the whole game (reported, never copied raw).
# A canonical-mode game (CANONICAL records hold whole frame bundles) and a War of the Ring game are skipped in this version.
#
#   replay_export.py --backup DIR --out DIR --notice-date YYYY-MM-DD --key-file FILE   (Python 3 standard library only)
# The key (>= 32 bytes, hex or raw) keys the pseudonyms: HMAC-SHA256(key, game id)[:8], so the same game always gets the same name and nobody without
# the key can link a pseudonym back to the lobby's game id. Keep it with the ops secrets; never give it to the data's user.
import argparse
import datetime
import hashlib
import hmac
import json
import os
import re
import struct
import sys
import tempfile
from pathlib import Path

LOG_MAGIC = b"OBFMERLG"
SNAPSHOT_RE = re.compile(r"^\d{4}-\d{2}-\d{2}$")
GAME_ID_RE = re.compile(r"^[0-9a-f]{16}$")
# COMMAND kinds kept (GameNetwork/NetPacket.h NetCommandType: the first byte of an encoded command). Game orders (GAMECOMMAND) carry only integer,
# real, boolean, object-id and location arguments: NetPacket::writeGameMessage refuses any other argument type, so no text can ride in them.
# Dropped by not being listed: CHAT 14, DISCONNECTCHAT 13 (text), HERO 20 (a Create-a-Hero's name), FILE 19, WRAPPER 18, DESYNCREPORT, VIEW, keep-alives,
# load progress and every disconnect-screen command.
KEEP_COMMANDS = {
    0x03: "FRAMEINFO",
    0x04: "GAMECOMMAND",
    0x0A: "PLAYERLEAVE",
    0x0B: "DESTROYPLAYER",
    0x82: "RUNAHEAD",
    0x83: "REJOIN",
    0x85: "PAUSE",
    0x86: "RESUME",
}
# each kept kind's exact size (NetPacket::encodeCommand: u8 type, u8 slot, u32 execution frame, then the kind's fields); GAMECOMMAND is parsed
FIXED_SIZE = {0x03: 8, 0x0A: 6, 0x0B: 7, 0x82: 10, 0x83: 7, 0x85: 10, 0x86: 10}
# GAMECOMMAND (NetPacket::writeGameMessage): u16 message type, u8 player, u32 argument count, then per argument u8 type + its value:
# INTEGER 0 (i32), REAL 1 (f32), BOOLEAN 2 (u8 0/1), OBJECTID 3 (u32), LOCATION 6 (3 x f32): no other type has an encoding
ARGUMENT_SIZE = {0: 4, 1: 4, 2: 1, 3: 4, 6: 12}
MAX_ARGUMENTS = 1 << 20  # GameNetwork/NetPacket.h kMaxGameMessageArguments (the parse is bounded by the command's length anyway)


def command_ok(cmd):
    """an allow-listed command whose bytes are exactly that kind's numeric layout (a client's own bytes are never trusted to hold no text)"""
    kind = cmd[0]
    if kind in FIXED_SIZE:
        return len(cmd) == FIXED_SIZE[kind]
    if kind != 0x04 or len(cmd) < 13:
        return False
    argc = struct.unpack_from("<I", cmd, 9)[0]
    if argc > MAX_ARGUMENTS:
        return False
    pos = 13
    for _ in range(argc):
        if pos >= len(cmd) or cmd[pos] not in ARGUMENT_SIZE:
            return False
        if cmd[pos] == 2 and (pos + 1 >= len(cmd) or cmd[pos + 1] > 1):
            return False
        pos += 1 + ARGUMENT_SIZE[cmd[pos]]
    return pos == len(cmd)


# the start record's allow-list (the lobby's "openbfme-replay-start": services/online/lobby/src/lobby.mjs signAndStart); everything else is dropped
SETUP_FIELDS = ("map", "mapStartCount", "seed", "startingCash", "superweaponRestriction", "rules", "legendaryHeroes")
SLOT_FIELDS = ("state", "faction", "color", "startPos", "team", "handicap")
SLOT_STATES = {"open", "closed", "player", "easy", "medium", "hard", "brutal"}
# Sol r1 (High): a map path is the client's string and a custom or mod map's path may carry a name or an address: only the original game's official
# multiplayer maps (replay_export_maps.json, from the retail map cache) are exported by name; any other map is "custom-" + a keyed pseudonym
OFFICIAL_MAPS = frozenset(json.loads((Path(__file__).resolve().parent / "replay_export_maps.json").read_text(encoding="utf-8"))["maps"])


class Skip(Exception):
    """this game is not exported (the reason is reported)"""


def pseudonym(key, game_id):
    return hmac.new(key, game_id.encode("ascii"), hashlib.sha256).hexdigest()[:16]


def _int(value, lo, hi, what):
    if isinstance(value, bool) or not isinstance(value, int) or not lo <= value <= hi:
        raise Skip(f"start record: {what} is not an integer in {lo} .. {hi}")
    return value


def map_name(path, key):
    if isinstance(path, str) and path.lower() in OFFICIAL_MAPS:
        return path.lower()
    if not isinstance(path, str) or not path:
        raise Skip("start record: no map")
    return "custom-" + hmac.new(key, b"map:" + path.encode("utf-8", "replace"), hashlib.sha256).hexdigest()[:16]


def clean_start(doc, pseudo, key):
    """the start record's allow-listed settings; anything unexpected skips the game"""
    if not isinstance(doc, dict) or doc.get("kind") != "openbfme-replay-start" or doc.get("v") not in (1,):
        raise Skip("start record: not a version-1 openbfme-replay-start (a War of the Ring or another kind is not exported)")
    setup = doc.get("setup")
    if not isinstance(setup, dict):
        raise Skip("start record: no setup")
    if setup.get("gameMode", 0) != 0:
        raise Skip("start record: a War of the Ring game")
    out_setup = {}
    for k in SETUP_FIELDS:
        if k not in setup:
            continue
        v = setup[k]
        if k == "map":
            out_setup[k] = map_name(v, key)
        elif k == "rules":
            if v is None:
                out_setup[k] = None
            elif isinstance(v, list) and len(v) == 10:
                out_setup[k] = [_int(x, -2**31, 2**31 - 1, "rules[]") for x in v]
            else:
                raise Skip("start record: rules are not 10 integers")
        else:
            out_setup[k] = _int(v, -2**31, 2**32 - 1, k)
    if "cheatMode" in setup:
        out_setup["cheatMode"] = True  # the cheat seat's slot is the host's: only whether the game had Cheat Mode is kept
    slots = setup.get("slots")
    if not isinstance(slots, list) or not 1 <= len(slots) <= 16:
        raise Skip("start record: slots are not a list of 1 .. 16")
    out_slots = []
    for s in slots:
        if not isinstance(s, dict) or s.get("state") not in SLOT_STATES:
            raise Skip("start record: a slot of an unknown state")
        out = {"state": s["state"]}
        for k in SLOT_FIELDS[1:]:
            if k in s:
                out[k] = _int(s[k], -95, 63, k)
        out_slots.append(out)
    out_setup["slots"] = out_slots
    clean = {"v": 1, "kind": "openbfme-training-start", "game": pseudo, "setup": out_setup}
    for k in ("runAhead", "crcInterval"):
        if k in doc:
            clean[k] = _int(doc[k], 0, 1000, k)
    if "profile" in doc:
        # the game data's identity (an MD5 of the archives every player of that version has): needed to replay; not a player's
        if not isinstance(doc["profile"], str) or not re.fullmatch(r"[0-9a-f]{32}", doc["profile"]):
            raise Skip("start record: a profile that is not 32 hex digits")
        clean["profile"] = doc["profile"]
    return clean


def clean_log(data, pseudo, game_id):
    """the relay log with only allow-listed COMMAND records (and the record kinds that hold no player data); raises Skip otherwise"""
    if len(data) < 25 or data[:8] != LOG_MAGIC:
        raise Skip("log: not a relay log")
    version = struct.unpack_from("<I", data, 8)[0]
    if version == 4 and len(data) < 26:
        raise Skip("log: a cut header")
    if version not in (1, 2, 4):
        raise Skip(f"log: version {version}")
    if "%016x" % struct.unpack_from("<Q", data, 12)[0] != game_id:
        raise Skip("log: its header names another game")
    pos = 20
    mask = data[pos]
    if version == 4:
        mask |= data[pos + 1] << 8
        pos += 2
    else:
        pos += 1
    start = struct.unpack_from("<I", data, pos)[0]
    pos += 4
    wide = 16 if mask > 0xFF else 8
    out = bytearray(LOG_MAGIC)
    out += struct.pack("<I", version)
    out += struct.pack("<Q", int(pseudo, 16))
    out += bytes([mask & 0xFF]) + (bytes([mask >> 8]) if version == 4 else b"")
    out += struct.pack("<I", start - start % 86400)  # the day only
    kept = dropped = 0
    ended = False
    while pos < len(data):
        if ended:
            raise Skip("log: bytes after the END record")
        kind = data[pos]
        if kind == 1:  # COMMAND: u8 sender, u32 ms, u32 length, the command
            if pos + 10 > len(data):
                raise Skip("log: a cut COMMAND record")
            length = struct.unpack_from("<I", data, pos + 6)[0]
            end = pos + 10 + length
            if length < 1 or end > len(data):
                raise Skip("log: a COMMAND record's length")
            if data[pos + 10] in KEEP_COMMANDS and command_ok(data[pos + 10:end]):
                out += data[pos:end]
                kept += 1
            else:
                dropped += 1
            pos = end
        elif kind in (2, 3, 5):  # EVENT, GAP, REJOINED: slots, times, reasons only
            size = {2: 7, 3: 8, 5: 8}[kind]
            if kind == 5 and version < 2:
                raise Skip("log: a REJOINED record in a version-1 log")
            if pos + size > len(data):
                raise Skip("log: a cut record")
            out += data[pos:pos + size]
            pos += size
        elif kind == 4:  # END: u32 ms, u8 reason, then per slot 3 x u64 (counts)
            size = 6 + wide * 24
            if pos + size > len(data):
                raise Skip("log: a cut END record")
            out += data[pos:pos + size]
            pos += size
            ended = True
        elif kind == 7 and version >= 4:  # INPUT: the anti-cheat counters, not exported
            pos += 3 + 7 * 8
            if pos > len(data):
                raise Skip("log: a cut INPUT record")
        elif kind == 6:
            raise Skip("log: a canonical-mode game (CANONICAL records are not exported in this version)")
        else:
            raise Skip(f"log: an unknown record kind {kind}")
    if not ended:
        raise Skip("log: no END record (the game's log is not complete)")
    return bytes(out), kept, dropped


def newest_snapshot(backup):
    snaps = sorted(p for p in Path(backup).iterdir() if p.is_dir() and SNAPSHOT_RE.match(p.name)) if Path(backup).is_dir() else []
    if not snaps:
        raise SystemExit(f"replay_export: no snapshot under {backup}")
    return snaps[-1]


def read_key(path):
    raw = Path(path).read_bytes().strip()
    key = bytes.fromhex(raw.decode("ascii")) if re.fullmatch(rb"[0-9a-fA-F]+", raw) else raw
    if len(key) < 32:
        raise SystemExit("replay_export: the pseudonym key must be at least 32 bytes")
    return key


def export(snapshot, out, notice_date, key, log=print):
    notice = int(datetime.datetime.combine(notice_date, datetime.time(), datetime.timezone.utc).timestamp())
    out = Path(out)
    out.mkdir(parents=True, exist_ok=True)
    done = skipped = old = 0
    for log_path in sorted((Path(snapshot) / "logs").glob("*.obfmerlog")):
        game_id = log_path.name[:-len(".obfmerlog")]
        if not GAME_ID_RE.match(game_id):
            continue
        with open(log_path, "rb") as f:
            data = f.read(26)  # the header only: an earlier game's log is never read further
        if len(data) < 25 or data[:8] != LOG_MAGIC:
            skipped += 1
            log(f"replay_export: skip {pseudonym(key, game_id)}: not a relay log")
            continue
        version = struct.unpack_from("<I", data, 8)[0]
        if version == 4 and len(data) < 26:
            skipped += 1
            log(f"replay_export: skip {pseudonym(key, game_id)}: log: a cut header")
            continue
        start = struct.unpack_from("<I", data, 21 if version != 4 else 22)[0]
        if start < notice:
            old += 1  # before the export was enabled: not exported (only the header was read)
            continue
        pseudo = pseudonym(key, game_id)
        target = out / pseudo
        data = log_path.read_bytes()  # an eligible game: its whole log
        if (target / "log.obfmerlog").is_file():
            continue  # exported already (the same game always gets the same pseudonym)
        try:
            start_path = Path(snapshot) / "starts" / f"{game_id}.json"
            if not start_path.is_file():
                raise Skip("no start record")
            try:
                doc = json.loads(start_path.read_text(encoding="utf-8"))
            except (ValueError, UnicodeDecodeError):
                raise Skip("start record: not JSON")
            clean = clean_start(doc, pseudo, key)
            clean_bytes, kept, dropped = clean_log(data, pseudo, game_id)
        except Skip as why:
            skipped += 1
            log(f"replay_export: skip {pseudo}: {why}")
            continue
        tmp = Path(tempfile.mkdtemp(prefix=f".{pseudo}.", dir=out))
        (tmp / "start.json").write_text(json.dumps(clean, sort_keys=True, separators=(",", ":")) + "\n", encoding="utf-8")
        (tmp / "log.obfmerlog").write_bytes(clean_bytes)
        os.replace(tmp, target)
        done += 1
        log(f"replay_export: {pseudo}: {kept} commands kept, {dropped} dropped")
    log(f"replay_export: {done} exported, {skipped} skipped, {old} before the notice ({snapshot.name if hasattr(snapshot, 'name') else snapshot})")
    return done, skipped, old


# ---- delivery: an R2 bucket (S3 API, Signature Version 4, standard library only) ---------------------------------------------------------------------
# The job holds a WRITE token for the training bucket only; the data's user gets a separate READ-only token for that bucket (Cloudflare R2 API tokens
# are scoped per bucket). --r2-key-file holds two lines: the access key id, then the secret. Only games not uploaded yet are sent (a marker file).
def _sign(key, msg):
    return hmac.new(key, msg.encode("utf-8"), hashlib.sha256).digest()


def sigv4_headers(method, url, body, access_key, secret_key, now=None, region="auto", service="s3"):
    """the Authorization, x-amz-date and x-amz-content-sha256 headers of a path-style S3 request"""
    from urllib.parse import urlsplit, quote
    now = now or datetime.datetime.now(datetime.timezone.utc)
    amz_date = now.strftime("%Y%m%dT%H%M%SZ")
    day = now.strftime("%Y%m%d")
    parts = urlsplit(url)
    payload = hashlib.sha256(body).hexdigest()
    canonical_uri = quote(parts.path or "/", safe="/-_.~")
    headers = {"host": parts.netloc, "x-amz-content-sha256": payload, "x-amz-date": amz_date}
    signed = ";".join(sorted(headers))
    canonical = "\n".join([method, canonical_uri, parts.query, "".join(f"{k}:{headers[k]}\n" for k in sorted(headers)), signed, payload])
    scope = f"{day}/{region}/{service}/aws4_request"
    to_sign = "\n".join(["AWS4-HMAC-SHA256", amz_date, scope, hashlib.sha256(canonical.encode("utf-8")).hexdigest()])
    k = _sign(_sign(_sign(_sign(("AWS4" + secret_key).encode("utf-8"), day), region), service), "aws4_request")
    signature = hmac.new(k, to_sign.encode("utf-8"), hashlib.sha256).hexdigest()
    return {"Authorization": f"AWS4-HMAC-SHA256 Credential={access_key}/{scope}, SignedHeaders={signed}, Signature={signature}",
            "x-amz-date": amz_date, "x-amz-content-sha256": payload}


def upload(out, endpoint, bucket, prefix, access_key, secret_key, opener=None, log=print):
    """PUT every exported game not uploaded yet to <endpoint>/<bucket>/<prefix>/<pseudonym>/<file>; a game is marked uploaded once both files are in"""
    import urllib.request
    opener = opener or urllib.request.urlopen
    if not re.fullmatch(r"https://[A-Za-z0-9.-]+", endpoint) or not re.fullmatch(r"[a-z0-9][a-z0-9-]{1,61}[a-z0-9]", bucket) or not re.fullmatch(r"[a-z0-9-]{1,64}", prefix):
        raise SystemExit("replay_export: --r2-endpoint must be https://<host>, the bucket and prefix plain names")
    sent = 0
    for game in sorted(p for p in Path(out).iterdir() if p.is_dir() and re.fullmatch(r"[0-9a-f]{16}", p.name)):
        if (game / ".uploaded").exists():
            continue
        for name in ("start.json", "log.obfmerlog"):
            body = (game / name).read_bytes()
            url = f"{endpoint}/{bucket}/{prefix}/{game.name}/{name}"
            req = urllib.request.Request(url, data=body, method="PUT", headers=sigv4_headers("PUT", url, body, access_key, secret_key))
            with opener(req, timeout=60) as resp:
                if resp.status not in (200, 201):
                    raise SystemExit(f"replay_export: upload of {game.name}/{name} answered HTTP {resp.status}")
        (game / ".uploaded").write_text("")
        sent += 1
    log(f"replay_export: {sent} games uploaded to {bucket}/{prefix}/")
    return sent


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--backup", required=True, help="the lobby backup's directory (its newest dated snapshot is read)")
    ap.add_argument("--out", required=True)
    ap.add_argument("--notice-date", required=True, help="the day the export was first enabled (UTC, YYYY-MM-DD): earlier games are never exported")
    ap.add_argument("--key-file", required=True)
    ap.add_argument("--r2-endpoint", help="https://<account>.r2.cloudflarestorage.com (with --r2-bucket, --r2-prefix, --r2-key-file: upload after the export)")
    ap.add_argument("--r2-bucket")
    ap.add_argument("--r2-prefix", default="games")
    ap.add_argument("--r2-key-file", help="two lines: the write token's access key id, then its secret")
    a = ap.parse_args(argv)
    try:
        notice = datetime.date.fromisoformat(a.notice_date)
    except ValueError:
        raise SystemExit("replay_export: --notice-date must be YYYY-MM-DD")
    export(newest_snapshot(a.backup), a.out, notice, read_key(a.key_file))
    if a.r2_endpoint or a.r2_bucket or a.r2_key_file:
        if not (a.r2_endpoint and a.r2_bucket and a.r2_key_file):
            raise SystemExit("replay_export: an upload needs --r2-endpoint, --r2-bucket and --r2-key-file")
        lines = Path(a.r2_key_file).read_text(encoding="ascii").split()
        if len(lines) != 2:
            raise SystemExit("replay_export: --r2-key-file must hold the access key id and the secret, one per line")
        upload(a.out, a.r2_endpoint, a.r2_bucket, a.r2_prefix, lines[0], lines[1])
    return 0


if __name__ == "__main__":
    sys.exit(main())
