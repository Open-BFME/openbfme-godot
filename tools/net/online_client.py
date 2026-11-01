#!/usr/bin/env python3
# OpenBFME online test client (lane ONLINE-2, docs/ONLINE.md phase O1): drives the lobby service (services/online/lobby) for headless peers, and writes
# the START a peer plays with (openbfme_peer --online-start FILE). Python 3 standard library only.
#   host    --lobby URL --name NAME --profile MD5 --map KEY --slot STATE[,faction,color,startPos,team[,name]] ... --players N --out START.json [--id-file F]
#           [--spectator-delay SECONDS] (lane ONLINE-3)
#           signs in as a guest, creates the game (the first "player" slot is the host's; other human slots are "open"), waits until N players are in and
#           ready, starts it and writes the host's START
#   join    --lobby URL --name NAME --profile MD5 --game ID --out START.json [--faction F --color C --start-pos P --team T]
#           signs in as a guest, joins, sets its slot options, says ready and waits for the START
#   fixture --key-hex HEX --game ID --relay A.B.C.D:PORT --profile MD5 --map KEY --slot ... --out-dir DIR [--seed N] [--run-ahead N] [--crc-interval N]
#           no lobby: writes start-<slot>.json for every human slot with tickets signed by HEX (key id 1): the soak's and the tests' START (an independent
#           implementation of the ticket the lobby and the relay share; tools/net/test_online_client.py pins it)
#   smoke   --lobby URL --profile MD5 --map KEY [--expect-no-relay]
#           (CFDEPLOY-1) checks a deployed lobby end to end with two guests: health, sign-in, create, list, join, slot, ready, chat, START (refused
#           with no_relay under --expect-no-relay, else a START with a ticket), leave, the host closing the game; one ok / FAIL line per check, exit 1
#           on any FAIL
#           lane ONLINE-3: [--rejoin SLOT:GENERATION] (repeated) also writes start-<slot>-rejoin<generation>.json with a version 2 rejoin ticket
#   lane ONLINE-3 (reconnect): host / join take [--token-file F] (the guest session is kept there: a restarted client signs in as the same player)
#   rejoin  --lobby URL --token-file F [--game ID] --out START.json
#           the player's running games (/v1/mine; without --game the only one), then a rejoin START for it (/v1/games/<id>/rejoin: a fresh ticket)
#   lane ONLINE-3 (spectators):
#   spectate --lobby URL --name NAME --game ID --out START.json [--wait SECONDS]
#           signs in as a guest and asks for a spectator START (/v1/games/<id>/spectate: a version 3 ticket, a fresh index each call); with --wait it
#           asks again while the game has not started
#   fixture  [--spectator INDEX:DELAY] (repeated) also writes spectate-<index>.json with a version 3 spectator ticket
#   lane ONLINE16-FIX: fixture [--auth] gives every START the lobby's per-slot "session_key" (OBFME-SESSION-1: HMAC-SHA256(key, "OBFME-SESSION-1" || the ticket)[0..16), base64url);
#           --mode 4 signs the tag-required bit (TICKET_MODE_AUTH) into the player tickets (a version-7 ticket);
#           host / join [--max-slots 8|16] send X-OpenBFME-Max-Slots (absent: an older client); host [--require-auth] asks the lobby for a game that
#           requires authenticated relay traffic (setup.requireAuth)
import argparse
import base64
import hashlib
import hmac
import json
import re
import struct
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path


def ticket(key: bytes, key_id: int, game_hex: str, slot: int, mask: int, expires: int) -> str:
    body = struct.pack("<BBQBBI", 1, key_id, int(game_hex, 16), slot, mask, expires)
    return base64.urlsafe_b64encode(body + hmac.new(key, body, hashlib.sha256).digest()[:16]).rstrip(b"=").decode()


def mode_ticket(key: bytes, key_id: int, game_hex: str, slot: int, mask: int, expires: int, mode: int, frame_ms: int) -> str:
    """ROUTER-1: a version-7 player START ticket (version 4's u16 slot mask, + u8 mode bits, + u16 frame period ms; RelayProtocol.cpp sign)."""
    body = struct.pack("<BBQBHBIH", 7, key_id, int(game_hex, 16), slot, mask, mode, expires, frame_ms)
    return base64.urlsafe_b64encode(body + hmac.new(key, body, hashlib.sha256).digest()[:16]).rstrip(b"=").decode()


def rejoin_ticket(key: bytes, key_id: int, game_hex: str, slot: int, mask: int, expires: int, generation: int) -> str:
    # lane ONLINE-3: RelayProtocol.h version 2: the same fields, u16 generation, HMAC over the 18 bytes
    body = struct.pack("<BBQBBIH", 2, key_id, int(game_hex, 16), slot, mask, expires, generation)
    return base64.urlsafe_b64encode(body + hmac.new(key, body, hashlib.sha256).digest()[:16]).rstrip(b"=").decode()


def spectator_ticket(key: bytes, key_id: int, game_hex: str, index: int, mask: int, expires: int, delay: int) -> str:
    # lane ONLINE-3: RelayProtocol.h version 3: the spectator index in the slot byte, u16 the spectator delay in seconds, HMAC over the 18 bytes
    body = struct.pack("<BBQBBIH", 3, key_id, int(game_hex, 16), index, mask, expires, delay)
    return base64.urlsafe_b64encode(body + hmac.new(key, body, hashlib.sha256).digest()[:16]).rstrip(b"=").decode()


def session_key(key: bytes, ticket_b64: str) -> str:
    """ONLINE16-FIX (one scheme with ANTICHEAT-1; RelayAuth::sessionKey): the START's session_key, base64url of the first 16 bytes of
    HMAC-SHA256(ticket key, "OBFME-SESSION-1" || the exact ticket bytes)."""
    raw = base64.urlsafe_b64decode(ticket_b64 + "=" * (-len(ticket_b64) % 4))
    return base64.urlsafe_b64encode(hmac.new(key, b"OBFME-SESSION-1" + raw, hashlib.sha256).digest()[:16]).rstrip(b"=").decode()


def parse_slot(text: str) -> dict:
    f = text.split(",")
    s = {"state": f[0]}
    if f[0] not in ("open", "closed"):
        nums = [int(x) for x in f[1:5]] + [-1] * (4 - len(f[1:5]))
        s.update(faction=nums[0], color=nums[1], startPos=nums[2], team=nums[3])
        if len(f) > 5:
            s["name"] = f[5]
    return s


class Lobby:
    def __init__(self, url: str, max_slots=None):
        self.url = url.rstrip("/")
        self.token = None
        self.max_slots = max_slots  # ONLINE16-FIX: X-OpenBFME-Max-Slots (None: not sent, an older client)

    def call(self, method: str, path: str, body=None, allow=()):
        data = None if body is None else json.dumps(body).encode()
        # INFRA-2: Cloudflare's browser integrity check answers Python's default User-Agent with 403 (error 1010) on play.openbfme.com
        headers = {"content-type": "application/json", "user-agent": "openbfme-online-client/1"}
        if self.token:
            headers["authorization"] = f"Bearer {self.token}"
        if self.max_slots is not None:
            headers["x-openbfme-max-slots"] = str(self.max_slots)
        req = urllib.request.Request(self.url + path, data=data, method=method, headers=headers)
        try:
            with urllib.request.urlopen(req, timeout=30) as r:
                return json.loads(r.read())
        except urllib.error.HTTPError as e:
            text = e.read()[:300].decode('utf-8', 'replace')
            if e.code == 409 and any(f'"{c}"' in text for c in allow):  # lane ONLINE-3: an error the caller waits out
                return None
            raise SystemExit(f"online_client: {method} {path}: HTTP {e.code} {text}")
        except urllib.error.URLError as e:
            raise SystemExit(f"online_client: {method} {path}: HTTP {e.code} {e.read()[:300].decode('utf-8', 'replace')}")

    def call_status(self, method: str, path: str, body=None):
        """(HTTP status, JSON body) without exiting on an error status"""
        try:
            return 200, self.call(method, path, body)
        except SystemExit as e:
            m = re.match(r"online_client: \S+ \S+: HTTP (\d+) (.*)$", str(e), re.S)
            if not m:
                raise
            try:
                return int(m.group(1)), json.loads(m.group(2))
            except ValueError:
                return int(m.group(1)), {"raw": m.group(2)}

    def guest(self, name: str, token_file=None):
        r = self.call("POST", "/v1/auth/guest", {"name": name})
        self.token = r["token"]
        if token_file:
            Path(token_file).write_text(self.token + "\n")  # lane ONLINE-3: the rejoin signs in as the same player
        return r["account"]


def write_start(path: str, start: dict):
    Path(path).write_text(json.dumps(start, indent=1) + "\n")
    who = f"spectator {start['spectator']}" if "spectator" in start else f"slot {start['slot']}"
    print(f"online_client: START for {who} of game {start['game']} -> {path}", flush=True)


def cmd_host(a) -> int:
    lb = Lobby(a.lobby, a.max_slots)
    lb.guest(a.name, a.token_file)
    slots = [parse_slot(s) for s in a.slot]
    setup = {"map": a.map, "startingCash": a.cash, "slots": slots, "spectatorDelay": a.spectator_delay}
    if a.require_auth:
        setup["requireAuth"] = True
    if a.map_start_count:
        setup["mapStartCount"] = a.map_start_count
    r = lb.call("POST", "/v1/games", {"name": a.game_name or f"{a.name}'s game", "profile": a.profile, "setup": setup})
    gid = r["id"]
    print(f"online_client: hosting game {gid}", flush=True)
    if a.id_file:
        Path(a.id_file).write_text(gid + "\n")
    deadline = time.time() + a.timeout
    while time.time() < deadline:
        g = lb.call("GET", f"/v1/games/{gid}")["game"]
        humans = [s for s in g["slots"] if s["state"] == "player"]
        if len(humans) >= a.players and all(s["ready"] or s["you"] for s in humans):
            write_start(a.out, lb.call("POST", f"/v1/games/{gid}/start")["start"])
            return 0
        time.sleep(0.5)
    print("online_client: the players did not join and get ready in time", file=sys.stderr)
    return 3


def cmd_join(a) -> int:
    lb = Lobby(a.lobby, a.max_slots)
    lb.guest(a.name, a.token_file)
    gid = a.game
    deadline = time.time() + a.timeout
    lb.call("POST", f"/v1/games/{gid}/join", {"profile": a.profile})
    opts = {k: v for k, v in (("faction", a.faction), ("color", a.color), ("startPos", a.start_pos), ("team", a.team)) if v is not None}
    if opts:
        lb.call("POST", f"/v1/games/{gid}/slot", opts)
    ready = False
    while time.time() < deadline:
        r = lb.call("GET", f"/v1/games/{gid}")
        if r["start"]:
            write_start(a.out, r["start"])
            return 0
        me = [s for s in r["game"]["slots"] if s["you"]][0]
        if not me["ready"] or not ready:
            lb.call("POST", f"/v1/games/{gid}/ready", {"ready": True})  # every options change unreadies everyone: ready again
            ready = True
        time.sleep(0.5)
    print("online_client: no START in time", file=sys.stderr)
    return 3


def cmd_rejoin(a) -> int:
    lb = Lobby(a.lobby)
    lb.token = Path(a.token_file).read_text().strip()
    games = lb.call("GET", "/v1/mine")["games"]
    for g in games:
        print(f"online_client: running game {g['id']} slot {g['slot']}: rejoin {g['rejoin']}", flush=True)
    gid = a.game
    if not gid:
        if len(games) != 1:
            print(f"online_client: {len(games)} running games: name one with --game", file=sys.stderr)
            return 3
        gid = games[0]["id"]
    write_start(a.out, lb.call("POST", f"/v1/games/{gid}/rejoin")["start"])
    return 0


def cmd_spectate(a) -> int:
    lb = Lobby(a.lobby)
    lb.guest(a.name)
    deadline = time.time() + a.wait
    while True:
        r = lb.call("POST", f"/v1/games/{a.game}/spectate", allow=("not_started",))
        if r:
            write_start(a.out, r["start"])
            return 0
        if time.time() >= deadline:
            print("online_client: the game did not start in time", file=sys.stderr)
            return 3
        time.sleep(0.5)


def cmd_fixture(a) -> int:
    key = bytes.fromhex(a.key_hex)
    slots = [parse_slot(s) for s in a.slot]
    for i, s in enumerate(slots):
        if s["state"] != "open" and s["state"] != "closed" and "name" not in s:
            s["name"] = f"Player{i}" if s["state"] == "player" else "Computer"
    mask = sum(1 << i for i, s in enumerate(slots) if s["state"] == "player")
    setup = {"map": a.map, "seed": a.seed, "startingCash": a.cash, "superweaponRestriction": 0, "rules": None, "legendaryHeroes": 1, "slots": slots}
    out = Path(a.out_dir)
    out.mkdir(parents=True, exist_ok=True)

    def start(**fields):  # ONLINE16-FIX: --auth adds the ticket's session_key, as the lobby does
        if a.auth:
            fields["session_key"] = session_key(key, fields["ticket"])
        return fields

    for i, s in enumerate(slots):
        if s["state"] == "player":
            write_start(str(out / f"start-{i}.json"), start(
                v=1, kind="openbfme-start", game=a.game, slot=i, relay=a.relay, ticket=(mode_ticket(key, 1, a.game, i, mask, a.expires, a.mode, a.frame_ms) if a.mode else ticket(key, 1, a.game, i, mask, a.expires)),
                runAhead=a.run_ahead, crcInterval=a.crc_interval, profile=a.profile, setup=setup))
    for r in a.rejoin or []:
        slot, generation = (int(x) for x in r.split(":"))
        write_start(str(out / f"start-{slot}-rejoin{generation}.json"), start(
            v=1, kind="openbfme-start", game=a.game, slot=slot, relay=a.relay,
            ticket=rejoin_ticket(key, 1, a.game, slot, mask, a.expires, generation), rejoin=generation,
            runAhead=a.run_ahead, crcInterval=a.crc_interval, profile=a.profile, setup=setup))
    for r in a.spectator or []:
        index, delay = (int(x) for x in r.split(":"))
        write_start(str(out / f"spectate-{index}.json"), start(
            v=1, kind="openbfme-spectate", game=a.game, spectator=index, relay=a.relay,
            ticket=spectator_ticket(key, 1, a.game, index, mask, a.expires, delay), delay=delay,
            runAhead=a.run_ahead, crcInterval=a.crc_interval, profile=a.profile, setup=setup))
    return 0


def cmd_smoke(a) -> int:
    results = []

    def check(cond: bool, what: str):
        results.append(bool(cond))
        print(("ok   " if cond else "FAIL ") + what, flush=True)

    host, guest = Lobby(a.lobby), Lobby(a.lobby)
    health = host.call("GET", "/v1/health")
    check(health.get("ok") is True and health.get("v") == 1, "health: " + json.dumps(health, sort_keys=True))
    if a.expect_no_relay:
        check(health.get("relays") == [] and health.get("relay") == "no relay configured", "health reports: no relay configured")
    host.guest("SmokeHost")
    guest.guest("SmokeGuest")
    check(bool(host.token) and bool(guest.token), "two guests signed in")
    setup = {"map": a.map, "startingCash": 1500, "slots": [{"state": "player", "faction": 0, "color": 0, "startPos": 0, "team": 0}, {"state": "open"}]}
    gid = host.call("POST", "/v1/games", {"name": "smoke test", "profile": a.profile, "setup": setup})["id"]
    check(bool(re.fullmatch(r"[0-9a-f]{16}", gid)), f"game {gid} created")
    listed = [g for g in host.call("GET", "/v1/games")["games"] if g["id"] == gid]
    check(len(listed) == 1 and listed[0]["host"] == "SmokeHost" and listed[0]["openSlots"] == 1, "the game is listed with one open slot")
    check(guest.call("POST", f"/v1/games/{gid}/join", {"profile": a.profile}).get("slot") == 1, "the second guest joined slot 1")
    guest.call("POST", f"/v1/games/{gid}/slot", {"faction": 1, "color": 1, "startPos": 1, "team": 1})
    guest.call("POST", f"/v1/games/{gid}/ready", {"ready": True})
    host.call("POST", f"/v1/games/{gid}/chat", {"text": "smoke chat"})
    seen = guest.call("GET", f"/v1/games/{gid}?since=0")
    check(any(c["from"] == "SmokeHost" and c["text"] == "smoke chat" for c in seen["chat"]), "the guest sees the host's chat line")
    status, body = host.call_status("POST", f"/v1/games/{gid}/start")
    if a.expect_no_relay:
        check(status == 503 and body.get("error") == "no_relay", f"START refused: HTTP {status} {body.get('error')}: {body.get('message')}")
        check(guest.call("GET", f"/v1/games/{gid}")["start"] is None, "no START reached the guest")
    else:
        check(status == 200 and bool(body.get("start", {}).get("ticket")), f"START: HTTP {status}")
    check(guest.call("POST", f"/v1/games/{gid}/leave").get("left") is True, "the guest left")
    closed = host.call("POST", f"/v1/games/{gid}/leave")
    if a.expect_no_relay:
        check(closed.get("closed") is True and not [g for g in host.call("GET", "/v1/games")["games"] if g["id"] == gid], "the host left: game closed and unlisted")
    print(f"smoke: {sum(results)} of {len(results)} checks passed")
    return 0 if all(results) else 1


def main() -> int:
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    for name in ("host", "join"):
        p = sub.add_parser(name)
        p.add_argument("--lobby", required=True)
        p.add_argument("--name", required=True)
        p.add_argument("--profile", required=True)
        p.add_argument("--out", required=True)
        p.add_argument("--timeout", type=float, default=300)
        p.add_argument("--token-file")
        p.add_argument("--max-slots", type=int, choices=(8, 16))  # ONLINE16-FIX: X-OpenBFME-Max-Slots (absent: an older client)
    h = sub.choices["host"]
    h.add_argument("--require-auth", action="store_true")  # ONLINE16-FIX: setup.requireAuth (the AUTH-1 cut-over, per game)
    h.add_argument("--map-start-count", type=int)  # ONLINE16-FIX: an extended map's starts (9 .. 16)
    h.add_argument("--map", required=True)
    h.add_argument("--slot", action="append", required=True)
    h.add_argument("--players", type=int, default=2)
    h.add_argument("--cash", type=int, default=1500)
    h.add_argument("--game-name")
    h.add_argument("--id-file")
    h.add_argument("--spectator-delay", type=int, default=120)  # seconds of game time the spectators watch behind the players
    j = sub.choices["join"]
    j.add_argument("--game", required=True)
    for opt in ("--faction", "--color", "--start-pos", "--team"):
        j.add_argument(opt, type=int)
    r = sub.add_parser("rejoin")
    r.add_argument("--lobby", required=True)
    r.add_argument("--token-file", required=True)
    r.add_argument("--game")
    r.add_argument("--out", required=True)
    sp = sub.add_parser("spectate")
    sp.add_argument("--lobby", required=True)
    sp.add_argument("--name", required=True)
    sp.add_argument("--game", required=True)
    sp.add_argument("--out", required=True)
    sp.add_argument("--wait", type=float, default=0)
    f = sub.add_parser("fixture")
    f.add_argument("--rejoin", action="append")
    f.add_argument("--spectator", action="append")
    f.add_argument("--key-hex", required=True)
    f.add_argument("--game", required=True)
    f.add_argument("--relay", required=True)
    f.add_argument("--profile", required=True)
    f.add_argument("--map", required=True)
    f.add_argument("--slot", action="append", required=True)
    f.add_argument("--out-dir", required=True)
    f.add_argument("--seed", type=int, default=1234)
    f.add_argument("--cash", type=int, default=1500)
    f.add_argument("--run-ahead", type=int, default=1)
    f.add_argument("--mode", type=int, default=0)        # ROUTER-1: TicketMode bits (1 = the relay keeps the beat); 0 = a legacy ticket
    f.add_argument("--frame-ms", type=int, default=0)    # ROUTER-1: the frame period in a mode ticket (0 = the 200 ms default)
    f.add_argument("--crc-interval", type=int, default=100)
    f.add_argument("--auth", action="store_true")  # ONLINE16-FIX: every START carries its ticket's session_key (authenticated datagrams)
    f.add_argument("--expires", type=int, default=int(time.time()) + 86400)
    sm = sub.add_parser("smoke")
    sm.add_argument("--lobby", required=True)
    sm.add_argument("--profile", required=True)
    sm.add_argument("--map", required=True)
    sm.add_argument("--expect-no-relay", action="store_true")
    a = ap.parse_args()
    return {"host": cmd_host, "join": cmd_join, "fixture": cmd_fixture, "smoke": cmd_smoke, "rejoin": cmd_rejoin, "spectate": cmd_spectate}[a.cmd](a)


if __name__ == "__main__":
    sys.exit(main())
