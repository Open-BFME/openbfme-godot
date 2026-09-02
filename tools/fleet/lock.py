#!/usr/bin/env python3
"""Per-file locks for hub files listed in hubs.txt.

    lock.py acquire <path>     take the lock (FLEET_AGENT names the holder)
    lock.py release <path>     drop it (only the holder, or --force)
    lock.py list               show every lock
    lock.py check <path>...    exit 1 if any path is a hub locked by someone else
                               or an unlocked hub while FLEET_AGENT is set

Locks live under workspace/locks/ (ignored) so they are machine-local. A
lock older than --stale-hours (default 12) is treated as expired.
"""
from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import sys
from pathlib import Path

import fleetlib as fl

LOCKS = fl.WORKSPACE / "locks"
HUBS = Path(__file__).resolve().parent / "hubs.txt"


def hubs() -> set[str]:
    out = set()
    for line in HUBS.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if line and not line.startswith("#"):
            out.add(line.replace("\\", "/"))
    return out


def _norm(path: str) -> str:
    p = Path(path)
    if p.is_absolute():
        try:
            p = p.resolve().relative_to(fl.REPO)
        except ValueError:
            pass
    return str(p).replace("\\", "/")


def _lock_path(rel: str) -> Path:
    return LOCKS / (hashlib.sha256(rel.encode("utf-8")).hexdigest()[:16] + ".json")


def _read(rel: str, stale_hours: float):
    path = _lock_path(rel)
    if not path.is_file():
        return None
    doc = fl.read_json(path)
    taken = dt.datetime.fromisoformat(doc["at"])
    if dt.datetime.now() - taken > dt.timedelta(hours=stale_hours):
        return None
    return doc


def cmd_acquire(args) -> int:
    rel = _norm(args.path)
    holder = _read(rel, args.stale_hours)
    me = fl.agent_name()
    if holder and holder["agent"] != me:
        print(f"LOCKED {rel} by {holder['agent']} since {holder['at']}")
        return 1
    fl.write_json(_lock_path(rel), {"path": rel, "agent": me, "at": dt.datetime.now().isoformat(timespec="seconds")})
    print(f"ACQUIRED {rel} for {me}")
    return 0


def cmd_release(args) -> int:
    rel = _norm(args.path)
    holder = _read(rel, args.stale_hours)
    if holder and holder["agent"] != fl.agent_name() and not args.force:
        print(f"HELD by {holder['agent']}; use --force to break it")
        return 1
    path = _lock_path(rel)
    if path.is_file():
        path.unlink()
    print(f"RELEASED {rel}")
    return 0


def cmd_list(args) -> int:
    if not LOCKS.is_dir():
        print("no locks")
        return 0
    for path in sorted(LOCKS.glob("*.json")):
        doc = fl.read_json(path)
        print(f"{doc['path']:60} {doc['agent']:20} {doc['at']}")
    return 0


def cmd_check(args) -> int:
    hub_set = hubs()
    me = fl.agent_name()
    enforced = bool(__import__("os").environ.get("FLEET_AGENT"))
    bad = 0
    for raw in args.paths:
        rel = _norm(raw)
        if rel not in hub_set:
            continue
        holder = _read(rel, args.stale_hours)
        if holder is None:
            if enforced:
                print(f"HUB {rel} is not locked; run: python tools/fleet/lock.py acquire {rel}")
                bad += 1
        elif holder["agent"] != me:
            print(f"HUB {rel} is locked by {holder['agent']}")
            bad += 1
    return 1 if bad else 0


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--stale-hours", type=float, default=12.0)
    sub = parser.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("acquire"); p.add_argument("path"); p.set_defaults(fn=cmd_acquire)
    p = sub.add_parser("release"); p.add_argument("path"); p.add_argument("--force", action="store_true"); p.set_defaults(fn=cmd_release)
    sub.add_parser("list").set_defaults(fn=cmd_list)
    p = sub.add_parser("check"); p.add_argument("paths", nargs="+"); p.set_defaults(fn=cmd_check)
    args = parser.parse_args(argv)
    return args.fn(args)


if __name__ == "__main__":
    sys.exit(main())
