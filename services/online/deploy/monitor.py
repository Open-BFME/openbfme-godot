#!/usr/bin/env python3
# The online service's monitor (lane INFRA-2): run every 10 minutes by a systemd --user timer on bfme-pve (pve_ops.sh installs it); appends ONE line per
# run to the status log the coordinator reads (~/openbfme-monitor/status.log):
#   <UTC time> OK|FAIL lobby <what> | relay <what> | backup <what>
# Checks (each failure is named in the line; the exit code is 1 when any failed):
#   lobby   GET <lobby>/v1/health over HTTPS: answers within 15 s, valid JSON, v 1, ok true, the log store, and the expected relay listed (a lobby
#           redeployed without --relays, or with another address, fails here)
#   relay   one UDP datagram to the relay: a DATA envelope from an address the relay never bound (RelayProtocol.h: u16 'BR', u8 version, u8 kind 3,
#           u8 target slot). A running relay answers NOT_BOUND (4 bytes) and creates no state; nothing within 3 tries of 2 s = down or blocked;
#           REFUSED (version) = the relay speaks another envelope version than this monitor (redeployed with a new protocol: update --envelope).
#   uploads (INFRA-2 r2, Sol r1) the relay's log uploader reports to the lobby after every run (upload_logs.py --relay-id, once a minute); read back
#           through the signed admin API (GET /v1/admin/relay-status, --admin-key-file): FAIL when the relay has never reported or not for 10 minutes
#           (the uploader stopped or cannot reach the lobby), a log waits more than 15 minutes, the last run had a failed upload, or the relay's logErrors
#           counter is not 0 or unreadable (a game it could not log: the fault the drill of 2026-10-13 found; it stays until the relay restarts)
#   backup  the newest snapshot of lobby_backup.py's --backups directory is at most 26 hours old and its manifest reads (a failing daily backup)
# A check that crashes is a FAIL naming the exception, never a missing line (INFRA-2 r2).
# `monitor.py stale --log FILE` (INFRA-2 r2) is for the READER: it says whether the newest OK line is older than 30 minutes (the monitor itself, its timer
# or bfme-pve stopped; or every run has failed since): one line, exit 1 when stale. deck_watch.sh runs it from the Deck every 10 minutes.
# Not covered (S-4102): gameplay itself (a desync, a game that cannot start: the players' reports), the AWS and Cloudflare bills and quotas.
# Python 3 standard library only; the admin key is read from its file and never printed.
import argparse
import datetime
import hashlib
import hmac
import json
import re
import socket
import sys
import time
import urllib.request
from pathlib import Path

ENVELOPE_VERSION = 3  # engine/src/GameNetwork/RelayProtocol.h kVersion on this branch (the live relay's; 3 since ONLINE-3, deployed by DEPLOY-2)
DATA, NOT_BOUND, REFUSED = 3, 5, 6


def check_lobby(url, relay, log_store, timeout=15, opener=None):
    opener = opener or (lambda u: urllib.request.urlopen(urllib.request.Request(u, headers={"user-agent": "openbfme-monitor/1"}), timeout=timeout))
    t0 = time.monotonic()
    try:
        with opener(url.rstrip("/") + "/v1/health") as r:
            h = json.loads(r.read())
    except Exception as e:  # noqa: BLE001  every failure is a line, never a crash
        return False, f"health unreachable: {type(e).__name__}: {str(e)[:120]}"
    ms = int((time.monotonic() - t0) * 1000)
    if not isinstance(h, dict) or h.get("v") != 1 or h.get("ok") is not True:
        return False, f"health not ok: {json.dumps(h)[:160]}"
    addresses = [r.get("address") for r in h.get("relays") or [] if isinstance(r, dict)]
    if relay not in addresses:
        return False, f"relay {relay} not listed (relays {addresses}, {h.get('relay', '')})"
    if h.get("logStore") != log_store:
        return False, f"log store {h.get('logStore')!r}, expected {log_store!r}"
    return True, f"ok games={h.get('games')} relays={','.join(addresses)} store={h.get('logStore')} ({ms} ms)"


def probe_relay(address, version=ENVELOPE_VERSION, tries=3, wait=2.0):
    host, port = address.rsplit(":", 1)
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        s.settimeout(wait)
        for _ in range(tries):
            t0 = time.monotonic()
            try:
                s.sendto(bytes([ord("B"), ord("R"), version, DATA, 0]), (host, int(port)))
                d, _ = s.recvfrom(64)
            except socket.timeout:
                continue
            except OSError as e:
                return False, f"{address} {type(e).__name__}: {e}"
            ms = int((time.monotonic() - t0) * 1000)
            if len(d) >= 4 and d[:2] == b"BR" and d[2] == version and d[3] == NOT_BOUND:
                return True, f"{address} NOT_BOUND ({ms} ms)"
            if len(d) >= 5 and d[:2] == b"BR" and d[3] == REFUSED:
                return False, f"{address} REFUSED reason {d[4]} (envelope version {d[2]}; this monitor speaks {version})"
            return False, f"{address} answered {d[:8].hex()}"
    return False, f"{address} no answer to {tries} probes ({wait:g} s each)"


def check_backup(directory, max_age_h=26, now=None):
    d = Path(directory)
    snaps = sorted(p for p in d.iterdir() if p.is_dir() and len(p.name) == 10 and p.name[4] == "-") if d.is_dir() else []
    if not snaps:
        return False, f"no snapshot in {d.name}"
    newest = snaps[-1]
    try:
        m = json.loads((newest / "manifest.json").read_text())
        taken = datetime.datetime.fromisoformat(m["taken"])
        if taken.tzinfo is None or not isinstance(m["blobs"], list):
            raise ValueError("taken without a time zone, or blobs not a list")
    except Exception as e:  # noqa: BLE001  INFRA-2 r2 (Sol r1): any malformed manifest is a FAIL, not a crash
        return False, f"{newest.name} has no readable manifest ({type(e).__name__})"
    age_h = ((now or datetime.datetime.now(datetime.timezone.utc)) - taken).total_seconds() / 3600
    if age_h > max_age_h:
        return False, f"newest snapshot {newest.name} is {age_h:.0f} h old"
    return True, f"{newest.name} {len(m['blobs'])} blobs ({age_h:.1f} h old, {len(snaps)} kept)"


def admin_get(lobby, key, path, timeout=15):
    """GET <lobby><path> signed as lobby_backup.py signs (lobby.mjs adminCheck); the query is empty."""
    ts = str(int(time.time()))
    mac = hmac.new(key, f"GET {path}?\n{ts}\n".encode(), hashlib.sha256).hexdigest()
    req = urllib.request.Request(lobby.rstrip("/") + path, headers={"x-openbfme-admin": f"{ts}.{mac}", "user-agent": "openbfme-monitor/1"})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return json.loads(r.read())


def check_uploads(doc, relay, max_silence_s=600, max_pending_s=900):
    """doc: GET /v1/admin/relay-status's answer."""
    st = next((r for r in doc.get("relays", []) if r.get("relay") == relay), None)
    if st is None:
        return False, f"no status from {relay}'s uploader (never reported)"
    silent = int(doc["now"]) - int(st["receivedAt"])
    problems = []
    if silent > max_silence_s:
        problems.append(f"uploader silent {silent // 60} min")
    if st["failed"]:
        problems.append(f"{st['failed']} upload(s) failed in its last run")
    if st["oldestPendingS"] > max_pending_s:
        problems.append(f"{st['pending']} log(s) waiting, the oldest {st['oldestPendingS'] // 60} min")
    if st["logErrors"] is None:
        problems.append("the relay's status file is unreadable (relay down?)")
    elif st["logErrors"]:
        problems.append(f"the relay could not write {st['logErrors']} game log(s) (logErrors; until its restart)")
    if problems:
        return False, "; ".join(problems)
    return True, f"ok pending {st['pending']} running {st['partial']} (reported {silent} s ago)"


def guarded(fn, *args):
    try:
        return fn(*args)
    except Exception as e:  # noqa: BLE001  a crashing check is a named FAIL in the line
        return False, f"check crashed: {type(e).__name__}: {str(e)[:120]}"


OK_LINE = re.compile(r"^(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z) OK ")


def stale(log, max_age_min=30, now=None):
    """(ok, line) for the reader: the newest OK line of the status log is at most max_age_min old."""
    now = now or datetime.datetime.now(datetime.timezone.utc)
    try:
        lines = Path(log).read_text().splitlines()
    except OSError as e:
        return False, f"MONITOR STALE: cannot read the status log ({type(e).__name__})"
    oks = [m.group(1) for m in map(OK_LINE.match, lines) if m]
    if not oks:
        return False, f"MONITOR STALE: no OK line in the status log ({len(lines)} lines)" + (f"; last: {lines[-1][:200]}" if lines else "")
    age = (now - datetime.datetime.strptime(oks[-1], "%Y-%m-%dT%H:%M:%SZ").replace(tzinfo=datetime.timezone.utc)).total_seconds() / 60
    if age > max_age_min:
        return False, f"MONITOR STALE: the last OK line ({oks[-1]}) is {age:.0f} min old; last line: {lines[-1][:200]}"
    return True, f"MONITOR OK: last OK line {oks[-1]} ({age:.0f} min old)"


def main(argv=None):
    if argv is None:
        argv = sys.argv[1:]
    if argv[:1] == ["stale"]:
        sp = argparse.ArgumentParser(prog="monitor.py stale")
        sp.add_argument("--log", required=True)
        sp.add_argument("--max-age-min", type=float, default=30)
        b = sp.parse_args(argv[1:])
        ok, line = stale(b.log, b.max_age_min)
        print(line)
        return 0 if ok else 1
    ap = argparse.ArgumentParser(description="Checks the OpenBFME lobby, relay, its log uploads and the lobby backups; appends one status line")
    ap.add_argument("--lobby", default="https://play.openbfme.com")
    ap.add_argument("--relay", default="3.13.126.208:7350")
    ap.add_argument("--log-store", default="durable-object")
    ap.add_argument("--envelope", type=int, default=ENVELOPE_VERSION)
    ap.add_argument("--admin-key-file", required=True, help="admin.key (make_keys.sh): reads the relay uploader's status")
    ap.add_argument("--backups", required=True)
    ap.add_argument("--log", required=True, help="the status log (one line appended)")
    a = ap.parse_args(argv)

    def uploads():
        key = bytes.fromhex(Path(a.admin_key_file).read_text().strip())
        return check_uploads(admin_get(a.lobby, key, "/v1/admin/relay-status"), a.relay)
    results = [("lobby", *guarded(check_lobby, a.lobby, a.relay, a.log_store)), ("relay", *guarded(probe_relay, a.relay, a.envelope)),
               ("uploads", *guarded(uploads)), ("backup", *guarded(check_backup, a.backups))]
    ok = all(r[1] for r in results)
    line = (datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ") + (" OK " if ok else " FAIL ")
            + " | ".join(f"{name} {'' if good else 'FAIL '}{what}" for name, good, what in results))
    Path(a.log).parent.mkdir(parents=True, exist_ok=True)
    with open(a.log, "a") as f:
        f.write(line + "\n")
    print(line)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
