#!/usr/bin/env python3
"""Serve, list, complete, and bank work units.

    work.py next [queue]        one unit from the highest-priority non-empty queue
    work.py list <queue>        every open unit in a queue
    work.py queues              every queue with open/total counts
    work.py done <unit-id>      mark a manual unit done
    work.py bank <unit-id> --note "what is still wrong" [--file PATH]
"""
from __future__ import annotations

import argparse
import datetime as dt
import random
import shutil
import sys
from pathlib import Path

import fleetlib as fl


def cmd_queues(_args) -> int:
    for name in fl.QUEUE_ORDER:
        units, total = fl.units_for(name)
        kind = "derived" if name in fl.DERIVED else "manual"
        note = "" if total else "  (no denominator found; see tools/fleet/README.md)"
        print(f"{name:14} open={len(units):4d} total={total:5d}  {kind}{note}")
    return 0


def _print_unit(unit: dict) -> None:
    print(f"UNIT     {unit['id']}")
    print(f"TITLE    {unit['title']}")
    if unit.get("detail"):
        print(f"DETAIL   {unit['detail']}")
    if unit.get("path"):
        print(f"FILE     {unit['path']}")
    print(f"ORACLE   {unit['oracle']}")


def cmd_next(args) -> int:
    names = [args.queue] if args.queue else fl.QUEUE_ORDER
    for name in names:
        units, _ = fl.units_for(name)
        if not units:
            continue
        # Serve from the top band at random so parallel agents spread out.
        band = units[: max(1, min(len(units), args.band))]
        _print_unit(random.choice(band))
        return 0
    print("NO_WORK  every queue is empty or has no denominator; run `work.py queues`")
    return 1


def cmd_list(args) -> int:
    units, total = fl.units_for(args.queue)
    print(f"{args.queue}: {len(units)} open of {total}")
    for unit in units:
        print(f"  {unit['id']:60} {unit['title'][:70]}")
    return 0


def cmd_done(args) -> int:
    queue, _, stem = args.unit.partition("/")
    if queue in fl.DERIVED:
        print(f"{queue} is derived; it marks itself done when its oracle passes")
        return 1
    path = fl.QUEUES_DIR / queue / f"{stem}.json"
    if not path.is_file():
        print(f"no such unit file: {path}")
        return 1
    doc = fl.read_json(path)
    doc["status"] = "done"
    doc["done_by"] = fl.agent_name()
    doc["done_at"] = dt.date.today().isoformat()
    fl.write_json(path, doc)
    print(f"marked done: {path.relative_to(fl.REPO)}")
    return 0


def cmd_bank(args) -> int:
    stamp = dt.datetime.now().strftime("%Y%m%d-%H%M%S")
    safe = args.unit.replace("/", "__")
    folder = fl.WORKSPACE / "attempts" / f"{stamp}-{safe}"
    folder.mkdir(parents=True, exist_ok=True)
    (folder / "NOTE.txt").write_text(f"{args.unit}\n{fl.agent_name()}\n{args.note}\n", encoding="utf-8")
    for extra in args.file or []:
        src = Path(extra)
        if src.is_file():
            shutil.copy2(src, folder / src.name)
    lessons = fl.REPO / "docs" / "lessons.md"
    line = f"- {dt.date.today().isoformat()} {args.unit} ({fl.agent_name()}): {args.note.strip()}\n"
    text = lessons.read_text(encoding="utf-8")
    marker = "\n- "
    idx = text.find(marker)
    text = text[: idx + 1] + line + text[idx + 1 :] if idx >= 0 else text + line
    lessons.write_text(text, encoding="utf-8", newline="\n")
    print(f"banked to {folder}; docs/lessons.md updated (commit that line)")
    return 0


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="cmd", required=True)
    sub.add_parser("queues").set_defaults(fn=cmd_queues)
    p = sub.add_parser("next"); p.add_argument("queue", nargs="?"); p.add_argument("--band", type=int, default=8); p.set_defaults(fn=cmd_next)
    p = sub.add_parser("list"); p.add_argument("queue"); p.set_defaults(fn=cmd_list)
    p = sub.add_parser("done"); p.add_argument("unit"); p.set_defaults(fn=cmd_done)
    p = sub.add_parser("bank"); p.add_argument("unit"); p.add_argument("--note", required=True); p.add_argument("--file", action="append"); p.set_defaults(fn=cmd_bank)
    args = parser.parse_args(argv)
    return args.fn(args)


if __name__ == "__main__":
    sys.exit(main())
