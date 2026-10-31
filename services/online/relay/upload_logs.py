#!/usr/bin/env python3
# OpenBFME online relay: upload finished command logs to the lobby (lane ONLINE-2). Python 3 standard library only; run it from a systemd timer on the relay
# host (services/online/deploy/openbfme-relay-upload.timer). Each finished <game id>.obfmerlog in --log-dir is POSTed to <lobby>/v1/relay/logs/<game id>
# with the header X-OpenBFME-Signature: hex HMAC-SHA256(upload key, body); a stored log moves to <log-dir>/uploaded/. A refused upload stays for the next
# run and is reported (exit 1), never deleted.
#   upload_logs.py --log-dir DIR --lobby URL --upload-key-file FILE [--keep-days N] [--relay-id A.B.C.D:PORT --status-file FILE]
# INFRA-2 r2 (Sol r1: the monitor could not see whether the logs reach the lobby): with --relay-id, every run then posts its status to
# <lobby>/v1/relay/status, signed the same way: the logs still waiting (and the oldest one's age), the .part files of running games, this run's uploads
# and failures, and the relay's own logErrors counter from its --status file (null when it cannot be read: the monitor fails on that). A status the
# lobby refuses is exit 1. The operator's monitor reads it back.
# Online batch r5 (ADMINFIX-1's contract): a version-2 log's INPUT sidecar (<log>.input, RelayCore.h "INPUT sidecar") is POSTed, after its log was
# stored, to <lobby>/v1/relay/logs/<game id>/input, signed the same way. Both files move to uploaded/ only once both are stored; otherwise both stay
# and the next run sends them again (the lobby answers a log it already holds 200 unchanged). 409 log_missing: the lobby does not have the log yet,
# retried next run; 409 blob_exists (another sidecar is stored for that game) and any other refusal: an error, counted as failed. The status's
# pending and failed counts include the sidecars. Online batch r6 (Sol): a sidecar whose log is already in uploaded/ (a run interrupted between the
# two moves) is sent on its own and moved; one with no log beside it (a failed log finalization) is deleted once older than --keep-days.
import argparse
import json
import hashlib
import hmac
import os
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path


def send(url: str, body: bytes, key: bytes) -> tuple[str, str]:
    """POSTs a signed body: ("", "") when stored (200), else (the problem, the lobby's error code when it gave one)."""
    req = urllib.request.Request(url, data=body, method="POST",
                                 # INFRA-2: Cloudflare answers Python's default User-Agent with 403 (error 1010): every upload to play.openbfme.com failed
                                 headers={"content-type": "application/octet-stream", "user-agent": "openbfme-relay-upload/1",
                                          "x-openbfme-signature": hmac.new(key, body, hashlib.sha256).hexdigest()})
    try:
        with urllib.request.urlopen(req, timeout=60) as r:
            return ("", "") if r.status == 200 else (f"HTTP {r.status}", "")
    except urllib.error.HTTPError as e:
        text = e.read()[:200].decode("utf-8", "replace")
        try:
            code = str(json.loads(text).get("error") or "")
        except (ValueError, AttributeError):
            code = ""
        return f"HTTP {e.code}: {text}", code
    except OSError as e:
        return str(e), ""


def post(url: str, body: bytes, key: bytes) -> str:
    return send(url, body, key)[0]


def upload(path: Path, lobby: str, key: bytes) -> str:
    return post(f"{lobby.rstrip('/')}/v1/relay/logs/{path.name.split('.')[0]}", path.read_bytes(), key)


def sidecar_of(log: Path) -> Path:
    return log.with_name(log.name + ".input")


def upload_sidecar(side: Path, lobby: str, key: bytes) -> tuple[str, str]:
    return send(f"{lobby.rstrip('/')}/v1/relay/logs/{side.name.split('.')[0]}/input", side.read_bytes(), key)


def upload_all(logs: Path, lobby: str, key: bytes) -> tuple[int, int]:
    """One run: every finished log, then its sidecar; returns (files stored and moved to uploaded/, files that failed)."""
    done = logs / "uploaded"
    done.mkdir(exist_ok=True)
    failed = uploaded = 0
    for path in sorted(logs.glob("*.obfmerlog")):
        side = sidecar_of(path)
        problem = upload(path, lobby, key)
        if problem:
            failed += 1 + side.exists()  # the sidecar waits for its log
            print(f"upload_logs: {path.name}: {problem}", file=sys.stderr)
            continue
        if side.exists():
            problem, code = upload_sidecar(side, lobby, key)
            if code == "log_missing":
                print(f"upload_logs: {side.name}: the lobby has no log of the game yet: both stay for the next run", file=sys.stderr)
                continue
            if problem:
                failed += 1
                print(f"upload_logs: {side.name}: {problem}" + (" (another sidecar is stored for this game)" if code == "blob_exists" else ""),
                      file=sys.stderr)
                continue
            os.replace(side, done / side.name)
            uploaded += 1
            print(f"upload_logs: {side.name} stored")
        os.replace(path, done / path.name)
        uploaded += 1
        print(f"upload_logs: {path.name} stored")
    # online batch r6: a sidecar whose log was stored and moved without it
    for side in sorted(logs.glob("*.obfmerlog.input")):
        log_name = side.name[:-len(".input")]
        if (logs / log_name).exists() or not (done / log_name).exists():
            continue  # waits for its log, or an orphan (prune)
        problem, code = upload_sidecar(side, lobby, key)
        if problem:
            failed += code != "log_missing"
            print(f"upload_logs: {side.name}: {problem}", file=sys.stderr)
            continue
        os.replace(side, done / side.name)
        uploaded += 1
        print(f"upload_logs: {side.name} stored")
    return uploaded, failed


def prune(logs: Path, keep_days: float, now: float | None = None) -> None:
    """Deletes what is older than keep_days: uploaded logs and sidecars, and (online batch r6) a sidecar in the log directory with no log beside it."""
    cutoff = (time.time() if now is None else now) - keep_days * 86400
    done = logs / "uploaded"
    old = [*done.glob("*.obfmerlog"), *done.glob("*.obfmerlog.input")]
    old += [side for side in logs.glob("*.obfmerlog.input") if not (logs / side.name[:-len(".input")]).exists()]
    for path in old:
        if path.stat().st_mtime < cutoff:
            path.unlink()
            print(f"upload_logs: {path.name} older than {keep_days:g} days: deleted")


def status_body(relay_id: str, logs: Path, status_file, uploaded: int, failed: int, now=None) -> bytes:
    now = time.time() if now is None else now
    pending = sorted([*logs.glob("*.obfmerlog"), *logs.glob("*.obfmerlog.input")])
    try:
        log_errors = int(json.loads(Path(status_file).read_text())["logErrors"]) if status_file else None
    except (OSError, ValueError, KeyError, TypeError):
        log_errors = None
    return json.dumps({"v": 1, "relay": relay_id, "at": int(now), "pending": len(pending), "partial": len(list(logs.glob("*.part"))),
                       "oldestPendingS": int(max([now - p.stat().st_mtime for p in pending], default=0)), "uploaded": uploaded, "failed": failed,
                       "logErrors": log_errors}).encode()


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--log-dir", required=True)
    ap.add_argument("--lobby", required=True)
    ap.add_argument("--upload-key-file", required=True)
    ap.add_argument("--keep-days", type=float, default=14.0, help="uploaded logs and sidecars, and orphaned sidecars, older than this are deleted from the relay host")
    ap.add_argument("--relay-id", help="this relay's public a.b.c.d:port: post the uploader's status to the lobby")
    ap.add_argument("--status-file", help="the relay's --status JSON (its logErrors counter)")
    a = ap.parse_args()
    key = bytes.fromhex(Path(a.upload_key_file).read_text().strip())
    logs = Path(a.log_dir)
    uploaded, failed = upload_all(logs, a.lobby, key)
    prune(logs, a.keep_days)
    if a.relay_id:
        problem = post(f"{a.lobby.rstrip('/')}/v1/relay/status", status_body(a.relay_id, logs, a.status_file, uploaded, failed), key)
        if problem:
            print(f"upload_logs: status: {problem}", file=sys.stderr)
            return 1
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
