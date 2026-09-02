#!/usr/bin/env python3
"""Print the product-bar scoreboard with deltas since the last run.

Every row is the whole corpus. A row with no denominator says so instead
of showing a flattering number.
"""
from __future__ import annotations

import datetime as dt
import sys

import fleetlib as fl

STATE = fl.WORKSPACE / "fleet" / "progress-last.json"

ROWS = [
    ("core", "kernel units landed in engine/"),
    ("core-modules", "SAGE module types implemented in engine/"),
    ("red", "headless slice checks passing"),
    ("bugs", "playtest issues closed (open shown as remaining)"),
    ("launcher", "launcher units landed"),
    ("maps", "maps cooked, booted, AI-finished"),
    ("assets", "corpus assets converted and verified"),
    ("screens", "APT/WND screens loading"),
    ("cook", "INI block types in the generic cook"),
    ("render", "presentation units landed"),
    ("missions", "campaign and tutorial missions completing"),
    ("ai", "AI planner behaviors"),
    ("net", "lockstep features"),
    ("mods", "community mods passing the validator"),
]


def main() -> int:
    previous = fl.read_json(STATE) if STATE.is_file() else {}
    current = {}
    print(f"OpenBFME product bar  {dt.date.today().isoformat()}  agent={fl.agent_name()}")
    print(f"{'queue':14} {'done':>6} {'total':>6} {'delta':>6}  what")
    for name, what in ROWS:
        units, total = fl.units_for(name)
        if total == 0:
            print(f"{name:14} {'-':>6} {'-':>6} {'':>6}  {what}  [no denominator: see tools/fleet/README.md]")
            continue
        done = total - len(units)
        delta = done - int(previous.get(name, done))
        sign = f"{delta:+d}" if delta else "0"
        print(f"{name:14} {done:6d} {total:6d} {sign:>6}  {what}")
        current[name] = done
    fl.write_json(STATE, current)
    return 0


if __name__ == "__main__":
    sys.exit(main())
