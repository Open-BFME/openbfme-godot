#!/usr/bin/env python3
# The lobby's data backup (lane INFRA-2): pulls every stored blob (the relay's command logs, the replay START records and the player lists: what a
# replay download needs) through the lobby's signed admin API (lobby.mjs, "the admin backup") into dated snapshots, and pushes a snapshot back
# into a lobby (the restore). Python 3 standard library only.
#
#   pull    --lobby URL --admin-key-file FILE --dest DIR [--keep-days 14]
#           writes DIR/<UTC date>/ (blobs under their keys + manifest.json with each blob's size and SHA-256); a blob the previous snapshot already
#           holds is hard-linked from it (stored blobs never change: the lobby refuses to replace one), so a daily run fetches only the new games.
#           Then deletes the snapshots older than --keep-days. One line per run: "lobby_backup: pull <date> <n> blobs (<new> new) ..."
#   push    --lobby URL --admin-key-file FILE --snapshot DIR
#           checks the snapshot against its manifest, then imports every blob (POST /v1/admin/blobs/<key>): the lobby stores a blob only where none
#           is, answers "unchanged" for the same bytes and refuses different ones (409 blob_exists), so a push into a live lobby never overwrites
#   verify  --snapshot DIR   re-hashes a snapshot against its manifest
#
# The admin key is admin.key from make_keys.sh (hex); it is read from the file, never from the command line, and never printed. What is NOT in the
# backup, by design: the lobby's open games (they expire after 10 minutes idle, started ones after 3 hours) and the sign-in sessions (signed tokens,
# nothing stored). services/online/DEPLOY.md "Backups and disaster recovery".
import argparse
import datetime
import hashlib
import hmac
import json
import os
import re
import shutil
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

KEY_RE = re.compile(r"^(?:logs/[0-9a-f]{16}\.obfmerlog|(?:starts|players)/[0-9a-f]{16}\.json)$")  # lobby.mjs parseBlobKey
SNAPSHOT_RE = re.compile(r"^\d{4}-\d{2}-\d{2}$")
PAGE = 1000  # lobby.mjs ADMIN_PAGE
USER_AGENT = "openbfme-backup/1"


class BackupError(Exception):
    pass


def read_key(path):
    text = Path(path).read_text().strip()
    if not re.fullmatch(r"[0-9a-fA-F]{32,}", text) or len(text) % 2:
        raise BackupError(f"{path}: not a hex key of 16+ bytes (make_keys.sh's admin.key)")
    return bytes.fromhex(text)


class Admin:
    """Signed calls to <lobby>/v1/admin/...: X-OpenBFME-Admin: <unix seconds>.<hex HMAC-SHA256(key, "<METHOD> <path>?<query>\n<seconds>\n" + body)>.
    The query is signed in the form the lobby re-serialises (URLSearchParams: application/x-www-form-urlencoded, as urlencode writes it)."""

    def __init__(self, lobby, key, now=time.time):
        self.lobby = lobby.rstrip("/")
        self.key = key
        self.now = now

    def call(self, method, path, query=None, body=b""):
        q = urllib.parse.urlencode(query or {})
        ts = str(int(self.now()))
        mac = hmac.new(self.key, f"{method} {path}?{q}\n{ts}\n".encode() + body, hashlib.sha256).hexdigest()
        req = urllib.request.Request(f"{self.lobby}{path}" + (f"?{q}" if q else ""), data=body if method == "POST" else None, method=method,
                                     headers={"x-openbfme-admin": f"{ts}.{mac}", "user-agent": USER_AGENT, "content-type": "application/octet-stream"})
        try:
            with urllib.request.urlopen(req, timeout=120) as r:
                return r.read()
        except urllib.error.HTTPError as e:
            try:
                doc = json.loads(e.read())
                why = f"{doc.get('error')}: {doc.get('message')}"
            except ValueError:
                why = "(no JSON answer)"
            raise BackupError(f"{method} {path}: HTTP {e.code} {why}") from None
        except OSError as e:
            raise BackupError(f"{method} {path}: {e}") from None

    def list_all(self):
        out, after = [], ""
        while True:
            page = json.loads(self.call("GET", "/v1/admin/blobs", {"after": after, "limit": PAGE}))
            for b in page["blobs"]:
                if not KEY_RE.match(b["key"]) or (out and b["key"] <= out[-1]["key"]):
                    raise BackupError(f"the lobby listed a bad or out-of-order key {b['key']!r}")
                out.append(b)
            if not page.get("next"):
                return out
            after = page["next"]


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def snapshots(dest):
    return sorted(p for p in Path(dest).iterdir() if p.is_dir() and SNAPSHOT_RE.match(p.name)) if Path(dest).is_dir() else []


def load_manifest(snap):
    m = json.loads((Path(snap) / "manifest.json").read_text())
    return {b["key"]: b for b in m["blobs"]}


def verify(snap):
    """Every blob of the manifest is present with its size and SHA-256 and nothing else is there. Returns the blob count."""
    snap = Path(snap)
    blobs = load_manifest(snap)
    for key, b in blobs.items():
        data = (snap / key).read_bytes() if (snap / key).is_file() else None
        if data is None or len(data) != b["size"] or sha256(data) != b["sha256"]:
            raise BackupError(f"{snap.name}: {key} is missing or does not match its manifest")
    extra = [str(p.relative_to(snap)) for p in snap.rglob("*") if p.is_file() and p.name != "manifest.json" and str(p.relative_to(snap)) not in blobs]
    if extra:
        raise BackupError(f"{snap.name}: files not in the manifest: {', '.join(sorted(extra)[:5])}")
    return len(blobs)


def pull(admin, dest, keep_days, today=None, log=print):
    dest = Path(dest)
    dest.mkdir(parents=True, exist_ok=True)
    os.chmod(dest, 0o700)
    today = today or datetime.datetime.now(datetime.timezone.utc).date()
    name = today.isoformat()
    listed = admin.list_all()
    previous = [s for s in snapshots(dest) if s.name < name and (s / "manifest.json").is_file()]
    prev = previous[-1] if previous else None
    prev_blobs = load_manifest(prev) if prev else {}
    tmp = dest / f".{name}.tmp"
    if tmp.exists():
        shutil.rmtree(tmp)
    tmp.mkdir()
    entries, new = [], 0
    for b in listed:
        key = b["key"]
        target = tmp / key
        target.parent.mkdir(exist_ok=True)
        old = prev_blobs.get(key)
        if old and old["size"] == b["size"]:
            os.link(prev / key, target)
            digest = old["sha256"]
        else:
            data = admin.call("GET", f"/v1/admin/blobs/{key}")
            if len(data) != b["size"]:
                raise BackupError(f"{key}: listed as {b['size']} bytes, {len(data)} arrived")
            target.write_bytes(data)
            digest = sha256(data)
            new += 1
        entries.append({"key": key, "size": b["size"], "sha256": digest, "storedAt": b.get("storedAt")})
    (tmp / "manifest.json").write_text(json.dumps({"lobby": admin.lobby, "taken": datetime.datetime.now(datetime.timezone.utc).isoformat(timespec="seconds"),
                                                   "blobs": entries}, indent=1) + "\n")
    verify(tmp)
    final = dest / name
    if final.exists():
        shutil.rmtree(final)  # today's earlier run: replaced by this complete one
    os.replace(tmp, final)
    # INFRA-2 r2 (Sol r1): --keep-days 14 keeps 14 dates, today's included (the cut-off was one day late: 15 were kept)
    if keep_days < 1:
        raise BackupError("--keep-days must be at least 1")
    cutoff = (today - datetime.timedelta(days=keep_days - 1)).isoformat()
    pruned = [s.name for s in snapshots(dest) if s.name < cutoff]
    for s in pruned:
        shutil.rmtree(dest / s)
    total = sum(e["size"] for e in entries)
    log(f"lobby_backup: pull {name} {len(entries)} blobs ({new} new, {total} bytes) from {admin.lobby}; kept {len(snapshots(dest))} snapshots"
        + (f", pruned {', '.join(pruned)}" if pruned else ""))
    return entries


ORDER = {"starts": 0, "players": 1, "logs": 2}


def push(admin, snap, log=print):
    snap = Path(snap)
    verify(snap)
    stored = unchanged = 0
    for key in sorted(load_manifest(snap), key=lambda k: (ORDER[k.split("/")[0]], k)):
        r = json.loads(admin.call("POST", f"/v1/admin/blobs/{key}", body=(snap / key).read_bytes()))
        if r.get("unchanged"):
            unchanged += 1
        else:
            stored += 1
    log(f"lobby_backup: push {snap.name} into {admin.lobby}: {stored} stored, {unchanged} unchanged")
    return stored, unchanged


def main(argv=None):
    ap = argparse.ArgumentParser(description="Backs up / restores the OpenBFME lobby's stored blobs through its signed admin API")
    sub = ap.add_subparsers(dest="cmd", required=True)
    for name in ("pull", "push"):
        p = sub.add_parser(name)
        p.add_argument("--lobby", required=True)
        p.add_argument("--admin-key-file", required=True)
        if name == "pull":
            p.add_argument("--dest", required=True)
            p.add_argument("--keep-days", type=int, default=14)
        else:
            p.add_argument("--snapshot", required=True)
    p = sub.add_parser("verify")
    p.add_argument("--snapshot", required=True)
    a = ap.parse_args(argv)
    try:
        if a.cmd == "verify":
            print(f"lobby_backup: verify {Path(a.snapshot).name}: {verify(a.snapshot)} blobs match the manifest")
            return 0
        admin = Admin(a.lobby, read_key(a.admin_key_file))
        if a.cmd == "pull":
            pull(admin, a.dest, a.keep_days)
        else:
            push(admin, a.snapshot)
        return 0
    except (BackupError, OSError, ValueError, KeyError) as e:
        print(f"lobby_backup: {a.cmd} FAILED: {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
