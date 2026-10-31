#!/usr/bin/env python3
# OpenBFME integrity worker (lane ANTICHEAT-1): runs detector D1 ("orders on objects the player could not see", engine/src/GameNetwork/OrdersIntoFog.h)
# over a finished game and turns its report into review findings. Python 3 standard library only.
#
#   fog_check.py run --peer PATH (--replay FILE | --relay-log LOG --online-start START.json) [--game-id ID] --out REPORT.json
#       re-simulates the game headless (openbfme_peer --replay ... --fog-check; a relay log is first assembled into a replay with --relay-log --record)
#       and writes the detector's report; needs the player's RotWK / BFME2 files (ROTWK_INSTALL / BFME2_INSTALL or --rotwk / --bfme2)
#   fog_check.py findings REPORT.json [--post URL --key-file FILE]
#       prints one finding per player in the review or alert band (at most 50 events each, events_total counts all), or for an abstained game one
#       "abstained" record per player (no evidence: the lobby counts abstentions); a report without a game id gives nothing; with --post, sends them to the lobby's integrity intake, signed as the
#       lobby's private routes are (X-OpenBFME-Admin: <unix-seconds>.<hex>, admin_signature); 5xx are retried, 4xx are final. The intake route is the
#       Orthanc lobby's (not in this repository).
#
# Neutral wording: a finding says what the replay shows ("orders on objects this player could not see"), never "cheater". Nothing here acts on a player:
# "alert" asks the owner to review (the SuspectedCheater alert); "review" only lists the game in the Reports queue; "abstained" games are not findings.
from __future__ import annotations

import argparse
import hashlib
import hmac
import json
import os
import subprocess
import sys
import tempfile
import time
import urllib.parse
import urllib.error
import urllib.request
from pathlib import Path

DETECTOR = "D1-orders-into-fog"
INPUT_RATE = "D5-input-rate"            # engine/src/GameNetwork/CommandStreamChecks.h (owner 2026-10-21: informational)
COMMAND_INTEGRITY = "D6-command-integrity"
VIEW_FOCUS = "D2-view-focus"              # openbfme_peer --view-check: the pointer resting over a hidden enemy (client-reported views)
PACKET_INTEGRITY = "D7-packet-integrity"   # openbfme_peer --relay-log --packet-check: the relay log's INPUT records (signed input)
DETECTORS = (DETECTOR, VIEW_FOCUS, INPUT_RATE, COMMAND_INTEGRITY, PACKET_INTEGRITY)
BANDS = ("review", "alert")


def _plural(n: int, word: str) -> str:
    return f"{n} {word}{'' if n == 1 else 's'}"


def summary(player: dict, detector: str = DETECTOR) -> str:
    """The one neutral line a reviewer reads first."""
    events = player.get("events", [])
    moments = _plural(player["encounters"], "separate moment")
    if detector == INPUT_RATE:
        return (f"Automated check D5: more than 100 commands within 5 seconds of game time in {moments} of the game (peak "
                f"{player.get('peak_5s')} commands); macros are allowed, this is for information")
    if detector == VIEW_FOCUS:
        secs = sum(e.get("frames", 0) for e in events) / 5
        return (f"Automated check D2: the pointer stayed near enemies hidden from this player {_plural(len(events), 'time')} ({secs:.0f} s in all) "
                f"in {moments} of the game; {player.get('views')} views reported by the player's client (client-reported: supporting only)")
    if detector == PACKET_INTEGRITY:
        kinds = [("bad_tag", "with a wrong signature"), ("replayed", "sent again"), ("unsigned_refused", "unsigned after signing"),
                 ("slot_mismatch", "speaking for another slot"), ("malformed", "malformed"), ("malformed_records", "with a malformed record")]
        parts = ", ".join(f"{player.get(k, 0)} {w}" for k, w in kinds if player.get(k, 0))
        total = sum(player.get(k, 0) for k, _ in kinds)
        if player.get("anomaly"):  # online batch r4: authenticated players under a version-2 relay log (RelayLog::packetIntegrityReport)
            return (f"Automated check D7: {player['anomaly']}; the relay refused {_plural(total, 'datagram')} sent as this slot; its input was "
                    f"{player.get('input')} ({player.get('signed_datagrams')} signed, {player.get('plain_datagrams')} plain datagrams)")
        return (f"Automated check D7: the relay refused {_plural(total, 'datagram')} sent as this slot ({parts}); its input was "
                f"{player.get('input')} ({player.get('signed_datagrams')} signed, {player.get('plain_datagrams')} plain datagrams)")
    reasons: dict[str, int] = {}
    for e in events:
        reasons[e["reason"]] = reasons.get(e["reason"], 0) + 1
    parts = ", ".join(f"{n} {r.replace('_', ' ')}" for r, n in sorted(reasons.items()))
    if detector == COMMAND_INTEGRITY:
        return (f"Automated check D6: {_plural(len(events), 'command')} an unmodified client does not send ({parts}) in {moments} of the game; "
                f"{player.get('commands_total')} commands checked")
    return (f"Automated check D1: {_plural(len(events), 'order')} on objects this player could not see ({parts}) in {moments} of the game; "
            f"{player['orders_judged']} orders on enemy objects checked")


# the lobby intake's exact shapes (ADMINFIX-1, lane/adminfix1-lobby b8e395a05): the evidence counters it allows and each detector's event fields; the
# full detail stays in the local report and the kept replay
EVIDENCE_ALLOWED = ("player_index", "slot", "name", "band", "score", "encounters", "events", "strong_encounters", "orders_judged",
                    "orders_on_remembered", "orders_stale", "orders_abstained", "events_total", "commands_total", "commands_checked", "windows_checked")
EVENT_FIELDS = {
    INPUT_RATE: ("frame", "time_s", "command", "count", "encounter"),
    COMMAND_INTEGRITY: ("frame", "time_s", "command", "reason", "target_id", "encounter"),
}
MAX_EVENTS = 50  # the lobby intake's bounds (ADMINFIX-1, services/online/lobby/src/integrity.mjs on lane/adminfix1-lobby)
MAX_BODY_BYTES = 16 * 1024


def body_of(item: dict) -> bytes:
    return json.dumps(item, separators=(",", ":"), sort_keys=True).encode()


def _item(report: dict, p: dict, band: str, evidence: dict | None) -> dict:
    return {
        "v": 1,
        "detector": report.get("detector"),
        "detector_version": report.get("detector_version"),
        "game_id": report.get("game_id"),
        "status": report.get("status"),
        "abstain_reason": report.get("abstain_reason"),
        "slot": p.get("slot"),
        "player_index": p.get("player_index"),
        "name": p.get("name"),
        "band": band,
        "score": p.get("score") if evidence is not None else None,
        "summary": summary(p, report.get("detector")) if evidence is not None else None,
        "evidence": evidence,
    }


def findings(report: dict) -> list[dict]:
    """One finding per player in the review or alert band; none for an abstained game (its evidence does not count) or one without a game id."""
    if report.get("v") != 1 or report.get("detector") not in DETECTORS:
        raise ValueError(f"not a v1 report of {', '.join(DETECTORS)}")
    if report.get("status") != "ok" or report.get("crc_parity") != "match" or not report.get("game_id"):
        return []
    out = []
    for p in report.get("players", []):
        if p.get("band") not in BANDS:
            continue
        evidence = {k: v for k, v in p.items() if k in EVIDENCE_ALLOWED}
        events = p.get("events", [])
        keep = EVENT_FIELDS.get(report.get("detector"))
        if keep:
            events = [{k: e.get(k) for k in keep} for e in events]
        evidence["events_total"] = len(events)  # the summary counts them all
        item = _item(report, p, p["band"], evidence)
        n = min(MAX_EVENTS, len(events))
        evidence["events"] = events[:n]
        while n > 1 and len(body_of(item)) > MAX_BODY_BYTES:  # the lobby's JSON cap: the first events that fit
            n -= 1
            evidence["events"] = events[:n]
        out.append(item)
    return out


def abstentions(report: dict) -> list[dict]:
    """An abstained game: one record per judged player (the lobby counts them; no flag, no evidence)."""
    if report.get("v") != 1 or report.get("detector") not in DETECTORS:
        raise ValueError(f"not a v1 report of {', '.join(DETECTORS)}")
    if (report.get("status") == "ok" and report.get("crc_parity") == "match") or not report.get("game_id"):
        return []
    return [_item(dict(report, status="abstained", abstain_reason=report.get("abstain_reason") or f"crc_parity {report.get('crc_parity')}"),
                  p, "abstained", None) for p in report.get("players", [])]


def admin_signature(method: str, url: str, body: bytes, key: bytes, now: int) -> str:
    """The lobby's private-route form (as mod / metrics / enforcement-check): `<unix-seconds>.<hex>` with
    hex = HMAC-SHA256(key, "METHOD /path?query\n<unix-seconds>\n" + body); the `?` is literal also with an empty query."""
    u = urllib.parse.urlsplit(url)
    message = f"{method} {u.path}?{u.query}\n{now}\n".encode() + body
    return f"{now}.{hmac.new(key, message, hashlib.sha256).hexdigest()}"


def post(url: str, body: bytes, key: bytes, attempts: int = 4, sleep=time.sleep) -> str:
    """"" when the lobby took it (201, or 200 for an identical replay); else the problem. Only 5xx and transport errors are retried (with backoff,
    each attempt signed afresh: the lobby accepts +-300 s); 4xx are final (409 finding_conflict, 404 unknown_game, 422 not_a_player, 400 ...)."""
    problem = ""
    for attempt in range(attempts):
        req = urllib.request.Request(url, data=body, method="POST",
                                     headers={"content-type": "application/json", "user-agent": "openbfme-integrity/1",
                                              "x-openbfme-admin": admin_signature("POST", url, body, key, int(time.time()))})
        try:
            with urllib.request.urlopen(req, timeout=60) as r:
                return "" if r.status in (200, 201) else f"HTTP {r.status}"
        except urllib.error.HTTPError as e:
            problem = f"HTTP {e.code}: {e.read()[:200].decode('utf-8', 'replace')}"
            if e.code < 500:
                return problem
        except OSError as e:
            problem = str(e)
        if attempt + 1 < attempts:
            sleep(2 ** attempt)
    return problem


def peer_command(a: argparse.Namespace, *extra: str) -> list[str]:
    cmd = [a.peer]
    if a.rotwk:
        cmd += ["--rotwk", a.rotwk]
    if a.bfme2:
        cmd += ["--bfme2", a.bfme2]
    return cmd + list(extra)


# openbfme_peer --relay-log exit codes (engine/src/Main/openbfme_peer.cpp): 2 the players' CRCs disagree in the log (the game desynced), 4 the log is
# incomplete or the replay differs from the game (S-3710); in both the replay may still be written, but nothing judged from it is evidence
ASSEMBLY_ABSTAIN = {2: "the players' CRCs disagree in the relay log (the game desynced)",
                    4: "the relay log is incomplete or its replay differs from the game"}


def mark_abstained(path: str, reason: str) -> None:
    report = json.loads(Path(path).read_text(encoding="utf-8"))
    report["status"] = "abstained"
    report["abstain_reason"] = reason if not report.get("abstain_reason") else f"{reason}; {report['abstain_reason']}"
    for p in report.get("players", []):
        p["band"] = "abstained"
    Path(path).write_text(json.dumps(report, separators=(",", ":")) + "\n", encoding="utf-8")


def run(a: argparse.Namespace) -> int:
    with tempfile.TemporaryDirectory(prefix="fogcheck-") as tmp:
        replay = a.replay
        abstain = None
        if a.relay_log:
            if not a.online_start:
                print("fog_check: --relay-log needs --online-start", file=sys.stderr)
                return 1
            replay = str(Path(tmp) / "game.replay")
            rc = subprocess.call(peer_command(a, "--relay-log", a.relay_log, "--online-start", a.online_start, "--record", replay,
                                              "--report", str(Path(tmp) / "assemble.txt")))
            if rc not in (0, *ASSEMBLY_ABSTAIN) or not Path(replay).exists():
                print(f"fog_check: assembling the relay log failed (exit {rc})", file=sys.stderr)
                return 1
            abstain = ASSEMBLY_ABSTAIN.get(rc)
        if not replay:
            print("fog_check: give --replay or --relay-log", file=sys.stderr)
            return 1
        extra = ["--replay", replay, "--fog-check", a.out, "--report", str(Path(tmp) / "replay.txt")]
        if a.game_id:
            extra += ["--game-id", a.game_id]
        rc = subprocess.call(peer_command(a, *extra))
        if not Path(a.out).exists():
            print(f"fog_check: openbfme_peer wrote no report (exit {rc})", file=sys.stderr)
            return 1
        if abstain:
            mark_abstained(a.out, abstain)
        # exit 2 is a hash mismatch: the report exists and says abstained
        return 0 if rc in (0, 2) else 1


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    sub = ap.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("run")
    r.add_argument("--peer", required=True, help="the openbfme_peer executable")
    r.add_argument("--rotwk", default=os.environ.get("ROTWK_INSTALL"))
    r.add_argument("--bfme2", default=os.environ.get("BFME2_INSTALL"))
    r.add_argument("--replay")
    r.add_argument("--relay-log")
    r.add_argument("--online-start")
    r.add_argument("--game-id")
    r.add_argument("--out", required=True)
    f = sub.add_parser("findings")
    f.add_argument("report")
    f.add_argument("--post", help="the lobby's integrity intake URL")
    f.add_argument("--key-file", help="the lobby's INTEGRITY_KEY (64 hex) for the X-OpenBFME-Admin signature")
    a = ap.parse_args(argv)
    if a.cmd == "run":
        return run(a)
    rep = json.loads(Path(a.report).read_text(encoding="utf-8"))
    items = findings(rep) + abstentions(rep)
    failed = 0
    key = bytes.fromhex(Path(a.key_file).read_text().strip()) if a.post and a.key_file else None
    if a.post and key is None:
        print("fog_check: --post needs --key-file", file=sys.stderr)
        return 1
    for item in items:
        body = body_of(item)
        print(body.decode())
        if a.post:
            problem = post(a.post, body, key)
            if problem:
                print(f"fog_check: post failed: {problem}", file=sys.stderr)
                failed += 1
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
