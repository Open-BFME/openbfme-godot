#!/usr/bin/env python3
# OpenBFME online relay: upload finished command logs to the lobby (lane ONLINE-2). Python 3 standard library only; run it from a systemd timer on the relay
# host (services/online/deploy/openbfme-relay-upload.timer). Each finished <game id>.obfmerlog in --log-dir is POSTed to <lobby>/v1/relay/logs/<game id>
# with the header X-OpenBFME-Signature: hex HMAC-SHA256(upload key, body); a stored log moves to <log-dir>/uploaded/. A refused upload stays for the next
# run and is reported (exit 1), never deleted.
#   upload_logs.py --log-dir DIR --lobby URL --upload-key-file FILE [--keep-days N] [--relay-id A.B.C.D:PORT --status-file FILE]
# INFRA-2 r2 (Sol r1: the monitor could not see whether the logs reach the lobby): with --relay-id, every run then posts its status to
# <lobby>/v1/relay/status, signed the same way: the logs still waiting (and the oldest one's age), the .part files of running games, this run's uploads
# and failures, and the relay's own logErrors counter from its --status file (null when it cannot be read: the monitor fails on that). A status the
# lobby refuses is exit 1. bfme-pve's monitor reads it back (services/online/deploy/monitor.py).
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


def post(url: str, body: bytes, key: bytes) -> str:
    req = urllib.request.Request(url, data=body, method="POST",
                                 # INFRA-2: Cloudflare answers Python's default User-Agent with 403 (error 1010): every upload to play.openbfme.com failed
                                 headers={"content-type": "application/octet-stream", "user-agent": "openbfme-relay-upload/1",
                                          "x-openbfme-signature": hmac.new(key, body, hashlib.sha256).hexdigest()})
    try:
        with urllib.request.urlopen(req, timeout=60) as r:
            return "" if r.status == 200 else f"HTTP {r.status}"
    except urllib.error.HTTPError as e:
        return f"HTTP {e.code}: {e.read()[:200].decode('utf-8', 'replace')}"
    except OSError as e:
        return str(e)


def upload(path: Path, lobby: str, key: bytes) -> str:
    return post(f"{lobby.rstrip('/')}/v1/relay/logs/{path.name.split('.')[0]}", path.read_bytes(), key)


def status_body(relay_id: str, logs: Path, status_file, uploaded: int, failed: int, now=None) -> bytes:
    now = time.time() if now is None else now
    pending = sorted(logs.glob("*.obfmerlog"))
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
    ap.add_argument("--keep-days", type=float, default=14.0, help="uploaded logs older than this are deleted from the relay host")
    ap.add_argument("--relay-id", help="this relay's public a.b.c.d:port: post the uploader's status to the lobby")
    ap.add_argument("--status-file", help="the relay's --status JSON (its logErrors counter)")
    a = ap.parse_args()
    key = bytes.fromhex(Path(a.upload_key_file).read_text().strip())
    logs = Path(a.log_dir)
    done = logs / "uploaded"
    done.mkdir(exist_ok=True)
    failed = uploaded = 0
    for path in sorted(logs.glob("*.obfmerlog")):
        problem = upload(path, a.lobby, key)
        if problem:
            failed += 1
            print(f"upload_logs: {path.name}: {problem}", file=sys.stderr)
        else:
            os.replace(path, done / path.name)
            uploaded += 1
            print(f"upload_logs: {path.name} stored")
    cutoff = time.time() - a.keep_days * 86400
    for old in done.glob("*.obfmerlog"):
        if old.stat().st_mtime < cutoff:
            old.unlink()
    if a.relay_id:
        problem = post(f"{a.lobby.rstrip('/')}/v1/relay/status", status_body(a.relay_id, logs, a.status_file, uploaded, failed), key)
        if problem:
            print(f"upload_logs: status: {problem}", file=sys.stderr)
            return 1
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
