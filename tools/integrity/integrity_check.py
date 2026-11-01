#!/usr/bin/env python3
# OpenBFME automatic integrity check (lane ANTICHEAT-1, owner 2026-10-21: "run a program and it just checks ... automatic and quick and deterministic,
# the replay file for the admins to watch"). Python 3 standard library + openbfme_peer (+ ffmpeg for the evidence clip).
#
#   integrity_check.py game --peer PATH (--replay FILE | --relay-log LOG --online-start START.json) [--game-id ID] --out-dir DIR [--no-render]
#       one finished game: DIR/game.replay (the whole game, watchable: assembled from the relay log when given one), DIR/d1.json (orders and
#       selections on objects the player could not see), DIR/d2.json (the pointer resting over hidden enemies, from the client-reported views), DIR/d5.json (input rate: more than 100 commands in 5 s of game time), DIR/d6.json (commands an
#       unmodified client does not send), for a relay game DIR/d7.json (each slot's input at the relay: signed or plain, and the datagrams the
#       relay refused for it), DIR/summary.json + DIR/summary.txt, and for every player D1 flags DIR/evidence/d1-slot<N>.png / .mp4 (what
#       the player could see beside what the game held, around the first flagged order). One deterministic re-simulation for the three checks (plus
#       one more for the evidence frames when D1 found something).
#   integrity_check.py post DIR --url URL --key-file FILE [--detectors D1-orders-into-fog,...]
#       sends DIR's findings to the lobby's integrity intake (fog_check.py findings / post: signed X-OpenBFME-Admin, 5xx retried, 4xx final).
#   integrity_check.py watch (--peer PATH | --peers DIR) --root DIR (--incoming DIR | --lobby URL --read-key-file FILE) [--url URL --key-file FILE] [--once]
#       every finished game not checked yet: `game` into DIR/<game id>/, then `post` when --url is given; DIR/state.json remembers what was done.
#       --peers DIR: one checker per release, DIR/<profile>/openbfme_peer, picked by the game START's "profile" (a peer replays only the games of
#       its own build: the engine id hashes every engine source). A game whose profile has no checker is not a failure: state.json keeps it under
#       "unsupported" (with its profile), it is checked once a checker for its profile is installed, and given up after the lobby's 24 h roster
#       window ("expired"). A checker runs the detectors it has (a release's peer has its release's: the report flags in the executable).
#       --incoming: <game id>.obfmerlog + <game id>.start.json files (a relay host's log directory, or a copy of it); --lobby: the lobby's worker read
#       routes (GET /v1/internal/integrity/games?after=..., /games/<id>/log, /games/<id>/start; the Orthanc lobby's, signed like the intake).
#       Run it from a timer (tools/integrity/openbfme-integrity.timer) within the lobby's 24 h roster window.
#       Online batch r6: a version-2 relay log (a game a previous-release HELLO opened) keeps its INPUT records in a sidecar; from the lobby it is
#       fetched (/games/<id>/input) next to the log as <log>.input when the listing says has_input or the log is version 2; a sidecar the listing
#       names that does not fetch is retried, never checked without; a version-2 log listed without one is asked for it again on every run for an hour
#       (the relay uploads it after the log, and the listing never lists the game again: state.json "input_wait"), then checked without it: D7
#       abstains saying so.
#       --incoming reads a <log>.input beside the log as it is.
#
# Nothing here acts on a player. The wording is neutral; abstained games (the re-simulation does not reproduce the players' own hashes, a desynced or
# incomplete relay log, ...) give no findings.
from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import fog_check as fc  # noqa: E402

REPORTS = (("d1.json", fc.DETECTOR, "--fog-check"), ("d2.json", fc.VIEW_FOCUS, "--view-check"), ("d5.json", fc.INPUT_RATE, "--input-rate-check"),
           ("d6.json", fc.COMMAND_INTEGRITY, "--command-check"))
GAME_ID = re.compile(r"^[0-9a-f]{16}$")
ROSTER_SECONDS = 24 * 3600  # the lobby keeps a game's roster this long after its end: a finding posted later is refused (unknown_game)
INPUT_GRACE_SECONDS = 3600  # ADMINFIX-1 (Sol's follow-up): the relay uploads a version-2 log's INPUT sidecar AFTER its log, so a game can be
                            # listed before its sidecar exists, and the listing never lists it again: wait this long for it before D7 abstains
_FLAGS: dict[tuple[str, int, int], set[str]] = {}


def peer_flags(peer: str) -> set[str]:
    """The report flags this openbfme_peer build has: an older release's peer lacks the later detectors (its argument parser holds each flag as a
    literal, so the executable's bytes say which). D1's --fog-check is the minimum a checker needs."""
    st = os.stat(peer)
    key = (str(Path(peer).resolve()), st.st_mtime_ns, st.st_size)
    if key not in _FLAGS:
        data = Path(peer).read_bytes()
        _FLAGS[key] = {flag for flag in [f for _, _, f in REPORTS] + ["--packet-check"] if flag.encode() + b"\0" in data or flag.encode() + b'"' in data}
    return _FLAGS[key]


def checker_for(a: argparse.Namespace, start: Path) -> tuple[str | None, str | None]:
    """(the openbfme_peer to check a game with, the game's profile): --peer for every game, or --peers DIR/<the START's profile>/openbfme_peer; no
    peer when that profile has no checker (or the checker has no D1)."""
    try:
        profile = str(json.loads(start.read_text()).get("profile") or "")
    except (ValueError, OSError):
        profile = ""
    if a.peer:
        return a.peer, profile or None
    peer = Path(a.peers) / profile / "openbfme_peer" if re.fullmatch(r"[0-9a-f]{32}", profile) else None
    if peer is None or not peer.is_file() or "--fog-check" not in peer_flags(str(peer)):
        return None, profile or None
    return str(peer), profile


def peer_base(a: argparse.Namespace) -> list[str]:
    cmd = [a.peer]
    if getattr(a, "rotwk", None):
        cmd += ["--rotwk", a.rotwk]
    if getattr(a, "bfme2", None):
        cmd += ["--bfme2", a.bfme2]
    return cmd


def check_game(a: argparse.Namespace, out: Path, replay: str | None, relay_log: str | None, online_start: str | None, game_id: str | None,
               render: bool) -> dict:
    out.mkdir(parents=True, exist_ok=True)
    abstain = None
    flags = peer_flags(a.peer)
    if "--fog-check" not in flags:
        raise RuntimeError(f"{a.peer} has no D1 (--fog-check): not a checker")
    if relay_log:
        replay = str(out / "game.replay")
        (out / "d7.json").unlink(missing_ok=True)
        packet = ["--packet-check", str(out / "d7.json")] + (["--game-id", game_id] if game_id else []) if "--packet-check" in flags else []
        rc = subprocess.call(peer_base(a) + ["--relay-log", relay_log, "--online-start", online_start, "--record", replay, "--report",
                                             str(out / "assemble.txt")] + packet)
        if rc not in (0, *fc.ASSEMBLY_ABSTAIN) or not Path(replay).exists():
            raise RuntimeError(f"assembling the relay log failed (exit {rc})")
        abstain = fc.ASSEMBLY_ABSTAIN.get(rc)
    elif replay and Path(replay).resolve() != (out / "game.replay").resolve():
        (out / "game.replay").write_bytes(Path(replay).read_bytes())
    cmd = peer_base(a) + ["--replay", str(out / "game.replay"), "--report", str(out / "replay.txt")]
    for name, _, flag in REPORTS:
        if flag in flags:
            cmd += [flag, str(out / name)]
    if game_id:
        cmd += ["--game-id", game_id]
    started = time.monotonic()
    rc = subprocess.call(cmd)
    seconds = time.monotonic() - started
    for name, _, flag in REPORTS:
        if flag not in flags:
            (out / name).unlink(missing_ok=True)  # this release's peer has no such detector: no report, never an old one
            continue
        if not (out / name).exists():
            raise RuntimeError(f"openbfme_peer wrote no {name} (exit {rc})")
        if abstain:
            fc.mark_abstained(str(out / name), abstain)
    # Sol r3: each report on its own: one that does not read (a bug, or a client-reported value that slipped through) never hides the others
    reports, unreadable = {}, []
    for name, det in [(n, d) for n, d, _ in REPORTS] + [("d7.json", fc.PACKET_INTEGRITY)]:
        if not (out / name).exists():
            continue  # d7.json: a relay game only
        try:
            reports[det] = json.loads((out / name).read_text())
        except (ValueError, OSError) as e:
            unreadable.append(f"{name}: {e}")
    evidence = []
    if fc.DETECTOR not in reports:
        raise RuntimeError("the D1 report does not read: " + "; ".join(unreadable))
    d1 = reports[fc.DETECTOR]
    flagged = [p for p in d1["players"] if p["band"] in fc.BANDS and p["events"]] if d1["status"] == "ok" else []
    if render and flagged:
        evidence = render_evidence(a, out, flagged)
    summary = {"v": 1, "game_id": game_id, "peer": a.peer, "status": d1["status"], "abstain_reason": d1["abstain_reason"], "crc_parity": d1["crc_parity"],
               "frames": d1["frames"], "replay": "game.replay", "check_seconds": round(seconds, 1), "evidence": evidence, "unreadable": unreadable,
               "detectors": {det: [{"slot": p["slot"], "player_index": p["player_index"], "name": p["name"], "band": p["band"],
                                    "events": len(p["events"]), "summary": fc.summary(p, det) if p["band"] in fc.BANDS else None}
                                   for p in r["players"]] for det, r in reports.items()}}
    (out / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    (out / "summary.txt").write_text(summary_text(summary))
    return summary


def render_evidence(a: argparse.Namespace, out: Path, flagged: list[dict]) -> list[str]:
    """A second pass dumps each flagged player's view around their first flagged order; render_evidence.py draws it."""
    made = []
    render = Path(__file__).resolve().parent / "render_evidence.py"
    for p in flagged:
        first = p["events"][0]["frame"]
        with tempfile.TemporaryDirectory(prefix="integrity-dump-") as dump:
            rc = subprocess.call(peer_base(a) + ["--replay", str(out / "game.replay"), "--fog-check", str(Path(dump) / "d1.json"), "--fog-check-dump", dump,
                                                 "--fog-check-view", str(p["player_index"]), "--fog-check-around", str(first), "--fog-check-before", "100",
                                                 "--fog-check-after", "30", "--report", str(Path(dump) / "dump.txt")])
            if rc not in (0, 2):
                continue
            name = f"d1-slot{p['slot']}"
            rc = subprocess.call([sys.executable, str(render), "--dump", dump, "--report", str(Path(dump) / "d1.json"), "--player", str(p["player_index"]),
                                  "--out-dir", str(out / "evidence"), "--name", name])
            if rc == 0:
                made += [f"evidence/{name}.png", f"evidence/{name}.mp4"]
    return made


def summary_text(s: dict) -> str:
    lines = [f"game {s['game_id'] or '(no id)'}: {s['status']}" + (f" ({s['abstain_reason']})" if s["abstain_reason"] else "") +
             f", hashes {s['crc_parity']}, {s['frames']} frames ({s['frames'] / 5 / 60:.1f} min), checked in {s['check_seconds']} s",
             f"replay: {s['replay']} (open it in OpenBFME to watch the whole game)"]
    for det, players in s["detectors"].items():
        for p in players:
            if p["summary"]:
                lines.append(f"{det} slot {p['slot']} ({p['name']}): {p['band']}: {p['summary']}")
    if len(lines) == 2:
        lines.append("no findings")
    lines += [f"unreadable report: {u}" for u in s.get("unreadable", [])]
    lines += [f"evidence: {e}" for e in s["evidence"]]
    return "\n".join(lines) + "\n"


def post_dir(directory: Path, url: str, key: bytes, detectors: list[str]) -> int:
    failed = 0
    for name, det in [(n, d) for n, d, _ in REPORTS] + [("d7.json", fc.PACKET_INTEGRITY)]:
        if det not in detectors or not (directory / name).exists():
            continue
        try:
            rep = json.loads((directory / name).read_text())
            items = fc.findings(rep) + fc.abstentions(rep)
        except (ValueError, OSError) as e:
            print(f"integrity_check: {det}: {name} does not read: {e}", file=sys.stderr)
            failed += 1
            continue
        for item in items:
            problem = fc.post(url, fc.body_of(item), key)
            if problem:
                print(f"integrity_check: {det} slot {item.get('slot')}: post failed: {problem}", file=sys.stderr)
                failed += 1
    return failed


# ---- sources of finished games ------------------------------------------------------------------------------------------------------------------
def incoming_games(directory: Path) -> list[tuple[str, Path, Path]]:
    out = []
    for log in sorted(directory.glob("*.obfmerlog")):
        gid = log.name.split(".")[0]
        start = directory / f"{gid}.start.json"
        if GAME_ID.match(gid) and start.exists():
            out.append((gid, log, start))
    return out


def lobby_get(lobby: str, path: str, key: bytes) -> bytes:
    url = lobby.rstrip("/") + path
    req = urllib.request.Request(url, method="GET", headers={"user-agent": "openbfme-integrity/1",
                                                              "x-openbfme-admin": fc.admin_signature("GET", url, b"", key, int(time.time()))})
    with urllib.request.urlopen(req, timeout=120) as r:
        return r.read()


def lobby_games(lobby: str, key: bytes, after: str | None, has_input: set[str] | None = None) -> tuple[list[str], str | None]:
    """Every finished game with a stored relay log after `after` (the lobby's cursor "<stored_at ms>:<game id>"), oldest first, page by page; the
    cursor to resume from next run: the lobby's resume_cursor of the last non-empty page (ADMINFIX-1, f27dd93a3: "store it and resume from it"),
    else the last next_cursor. `has_input` collects the games whose listing says the lobby holds an INPUT sidecar (ADMINFIX-1, d1ac8fc63)."""
    ids: list[str] = []
    cursor, resume = after, after
    while True:
        # the lobby verifies the signature over its own serialization of the query (":" as %3A): send it in that form (a raw ":" gets a 401)
        q = "?limit=100" + ("" if not cursor else "&after=" + urllib.parse.quote(cursor, safe=""))
        page = json.loads(lobby_get(lobby, "/v1/internal/integrity/games" + q, key))
        items = [g for g in page.get("items", []) if g.get("has_start", True) and GAME_ID.match(str(g.get("game_id")))]
        ids += [g["game_id"] for g in items]
        if has_input is not None:
            has_input.update(g["game_id"] for g in items if g.get("has_input"))
        nxt = page.get("next_cursor")
        resume = page.get("resume_cursor") or nxt or resume
        if not nxt or nxt == cursor:
            return ids, resume
        cursor = nxt


def log_version(log: bytes) -> int | None:
    """The relay log's version (RelayCore.h "Log format"), None when it is not one."""
    return int.from_bytes(log[8:12], "little") if len(log) >= 12 and log[:8] == b"OBFMERLG" else None


def fetch_input_sidecar(lobby: str, gid: str, key: bytes, listed: bool) -> bytes | None:
    """Online batch r6: a version-2 log's INPUT sidecar (RelayCore.h "INPUT sidecar"), or None when the lobby holds none. A sidecar the listing
    names (has_input) that does not fetch is an error (the game is retried, never checked without it); without has_input a 404 is "none" (a game
    of a relay before the sidecar, or a lobby without the route)."""
    try:
        return lobby_get(lobby, f"/v1/internal/integrity/games/{gid}/input", key)
    except urllib.error.HTTPError as e:
        if e.code == 404 and not listed:
            return None
        raise OSError(f"the INPUT sidecar{' the listing names' if listed else ''}: HTTP {e.code}") from e


MAX_ATTEMPTS = 12  # a game whose check or delivery keeps failing is tried on 12 runs (an hour on the 5-minute timer), then left for a person


def watch_once(a: argparse.Namespace) -> int:
    root = Path(a.root)
    root.mkdir(parents=True, exist_ok=True)
    state_path = root / "state.json"
    state = json.loads(state_path.read_text()) if state_path.exists() else {}
    state.setdefault("done", [])
    state.setdefault("cursor", None)
    state.setdefault("pending", {})  # Sol r3: game id -> {"stage": "check" | "post", "attempts": n, "error": text}; retried, never skipped silently
    state.setdefault("given_up", [])
    state.setdefault("unsupported", {})  # game id -> {"profile": p, "since": unix s}: no checker for its build yet; not a failure
    state.setdefault("expired", [])      # unsupported past the lobby's roster window: never checked
    state.setdefault("input_wait", {})   # game id -> unix s first seen: a version-2 log listed without its INPUT sidecar (yet); not a failure
    done = set(state["done"]) | set(state["given_up"]) | set(state["expired"])
    now = time.time()
    for gid, u in list(state["unsupported"].items()):
        if now - u.get("since", now) > ROSTER_SECONDS:
            state["unsupported"].pop(gid)
            state["expired"].append(gid)
            done.add(gid)
            print(f"integrity_check: {gid}: never checked: no checker for its profile {u.get('profile')} within the lobby's 24 h", file=sys.stderr)
    post_key = bytes.fromhex(Path(a.key_file).read_text().strip()) if a.url else None
    detectors = a.detectors.split(",")

    def save():
        state["done"] = sorted(done - set(state["given_up"]) - set(state["expired"]))
        state_path.write_text(json.dumps(state, indent=2) + "\n")

    tmp = tempfile.TemporaryDirectory(prefix="integrity-fetch-")
    work: list[tuple[str, Path | None, Path | None]] = []
    fetch_failed: list[tuple[str, str]] = []
    if a.incoming:
        work = [w for w in incoming_games(Path(a.incoming)) if w[0] not in done]
    else:
        read_key = bytes.fromhex(Path(a.read_key_file).read_text().strip())
        listed_input: set[str] = set()
        ids, cursor = lobby_games(a.lobby, read_key, state.get("cursor"), listed_input)
        ids += [g for g in state["pending"] if g not in ids]
        ids += [g for g in state["input_wait"] if g not in ids]  # listed once, before their sidecar: asked again every run until the grace ends
        # an unsupported game is fetched again only once a checker for its profile is installed
        ids += [g for g, u in state["unsupported"].items() if g not in ids and a.peers and
                (Path(a.peers) / str(u.get("profile")) / "openbfme_peer").is_file()]
        for gid in ids:
            if gid in done:
                continue
            if state["pending"].get(gid, {}).get("stage") == "post":
                work.append((gid, None, None))  # checked: only the delivery is due
                continue
            log, start = Path(tmp.name) / f"{gid}.obfmerlog", Path(tmp.name) / f"{gid}.start.json"
            listed = gid in listed_input or state["pending"].get(gid, {}).get("has_input", False)
            try:
                log_bytes = lobby_get(a.lobby, f"/v1/internal/integrity/games/{gid}/log", read_key)
                log.write_bytes(log_bytes)
                start.write_bytes(lobby_get(a.lobby, f"/v1/internal/integrity/games/{gid}/start", read_key))
                version = log_version(log_bytes)
                if listed or (version is not None and version < 4):
                    # online batch r6: next to the log, as <log>.input: openbfme_peer --relay-log reads it (RelayLogFile::load) for D7
                    side = fetch_input_sidecar(a.lobby, gid, read_key, listed)
                    if side is not None:
                        log.with_name(log.name + ".input").write_bytes(side)
                    elif version == 2:  # the relay writes a sidecar for a version-2 log only (RelayCore.h "INPUT sidecar")
                        since = state["input_wait"].setdefault(gid, int(now))
                        if now - since < INPUT_GRACE_SECONDS:
                            if since == int(now):
                                print(f"integrity_check: {gid}: a version-2 log without its INPUT sidecar yet: asked again on later runs for "
                                      f"{INPUT_GRACE_SECONDS // 60} min before D7 abstains")
                            continue
                        print(f"integrity_check: {gid}: no INPUT sidecar after {INPUT_GRACE_SECONDS // 60} min: checked without it (D7 abstains)")
                    state["input_wait"].pop(gid, None)
            except (urllib.error.URLError, OSError) as e:
                fetch_failed.append((gid, f"fetch: {e}", listed))  # Sol r4: counted like any other failure (attempts, give-up, the exit code)
                continue
            work.append((gid, log, start))
        state["cursor"] = cursor or state.get("cursor")
    failures = 0

    def failed(gid: str, p: dict, why: str) -> None:
        print(f"integrity_check: {gid}: {why}", file=sys.stderr)
        p["attempts"] = p.get("attempts", 0) + 1
        p["error"] = why
        if p["attempts"] >= MAX_ATTEMPTS:
            state["pending"].pop(gid, None)
            state["given_up"].append(gid)
            print(f"integrity_check: {gid}: given up after {MAX_ATTEMPTS} attempts ({why})", file=sys.stderr)
        else:
            state["pending"][gid] = p

    for gid, why, listed in fetch_failed:
        failures += 1
        failed(gid, {**state["pending"].get(gid, {"stage": "check", "attempts": 0}), **({"has_input": True} if listed else {})}, why)
    for gid, log, start in work:
        out = root / gid
        p = state["pending"].get(gid, {"stage": "check", "attempts": 0})
        ga = a
        if p["stage"] == "check" and log is not None:
            peer, profile = checker_for(a, Path(start))
            if peer is None:
                if gid not in state["unsupported"]:
                    print(f"integrity_check: {gid}: no checker for its profile {profile} (install one as <--peers>/{profile}/openbfme_peer)")
                state["unsupported"].setdefault(gid, {"profile": profile, "since": int(now)})
                save()
                continue
            state["unsupported"].pop(gid, None)
            ga = argparse.Namespace(**{**vars(a), "peer": peer})
        try:
            if p["stage"] == "check" and log is not None:
                s = check_game(ga, out, None, str(log), str(start), gid, not a.no_render)
                print(f"integrity_check: {gid}: {s['status']}, {sum(1 for d in s['detectors'].values() for x in d if x['summary'])} finding(s)")
                p = {"stage": "post", "attempts": 0}
            if a.url and post_dir(out, a.url, post_key, detectors):
                raise RuntimeError("a finding was not delivered")
            state["pending"].pop(gid, None)
            done.add(gid)
        except (RuntimeError, OSError, ValueError) as e:
            failures += 1
            failed(gid, p, str(e))
        save()
    save()
    tmp.cleanup()
    return 1 if failures else 0


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description="OpenBFME automatic integrity check")
    sub = ap.add_subparsers(dest="cmd", required=True)

    def peer_args(p):
        p.add_argument("--peer", help="the openbfme_peer executable")
        p.add_argument("--rotwk", default=os.environ.get("ROTWK_INSTALL"))
        p.add_argument("--bfme2", default=os.environ.get("BFME2_INSTALL"))
        p.add_argument("--no-render", action="store_true", help="no evidence picture / clip")

    g = sub.add_parser("game")
    peer_args(g)
    g.add_argument("--replay")
    g.add_argument("--relay-log")
    g.add_argument("--online-start")
    g.add_argument("--game-id")
    g.add_argument("--out-dir", required=True)
    p = sub.add_parser("post")
    p.add_argument("dir")
    p.add_argument("--url", required=True)
    p.add_argument("--key-file", required=True)
    p.add_argument("--detectors", default=fc.DETECTOR, help="comma-separated detectors the intake takes (default: D1 only)")
    w = sub.add_parser("watch")
    peer_args(w)
    w.add_argument("--peers", help="DIR/<profile>/openbfme_peer: one checker per release, picked by the game START's profile")
    w.add_argument("--root", required=True)
    w.add_argument("--incoming")
    w.add_argument("--lobby")
    w.add_argument("--read-key-file")
    w.add_argument("--url")
    w.add_argument("--key-file")
    w.add_argument("--detectors", default=fc.DETECTOR)
    w.add_argument("--once", action="store_true")
    w.add_argument("--interval", type=int, default=300, help="seconds between polls without --once")
    a = ap.parse_args(argv)
    if a.cmd != "post" and bool(a.peer) == bool(getattr(a, "peers", None)):
        print("integrity_check: give --peer" + (" or --peers" if a.cmd == "watch" else ""), file=sys.stderr)
        return 1
    if a.cmd == "game":
        if not (a.replay or (a.relay_log and a.online_start)):
            print("integrity_check: give --replay or --relay-log with --online-start", file=sys.stderr)
            return 1
        s = check_game(a, Path(a.out_dir), a.replay, a.relay_log, a.online_start, a.game_id, not a.no_render)
        print(summary_text(s), end="")
        return 0
    if a.cmd == "post":
        return 1 if post_dir(Path(a.dir), a.url, bytes.fromhex(Path(a.key_file).read_text().strip()), a.detectors.split(",")) else 0
    if not a.incoming and not (a.lobby and a.read_key_file):
        print("integrity_check: watch needs --incoming DIR or --lobby URL --read-key-file FILE", file=sys.stderr)
        return 1
    if a.url and not a.key_file:
        print("integrity_check: --url needs --key-file", file=sys.stderr)
        return 1
    while True:
        rc = watch_once(a)
        if a.once:
            return rc
        time.sleep(a.interval)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
