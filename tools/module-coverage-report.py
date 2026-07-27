#!/usr/bin/env python3
"""Usage-weighted module-type (behaviour) coverage report.

Reads the committed census at game/data/retail_module_census.json and reports
the share of retail module DECLARATION SITES the importer pipeline currently
consumes, per tree, broken out by the A-E classification.  Refused and
unhandled members are their own totals and are NEVER folded into progress -
counting a refusal as coverage is how a port convinces itself it is finished.

READ BEFORE QUOTING ANY NUMBER THIS PRINTS
==========================================
"consumed" means the importer pipeline names the module type and extracts from
it.  It does NOT mean the behaviour runs.  The importer flattens behaviours
into a static normalised description; the runtime adapter reads that
description and names almost no behaviours itself.  For class C members
(needs-runtime-system) a "consumed" status is at most half the work, and for
most of them the runtime half does not exist.  The honest headline is the
class C share: that work cannot be completed by importer changes at all.

The report asserts no threshold.  It fails only on incoherence: a census that
will not load, a member without a valid classification, or a status that
contradicts its own evidence - problems that would silently understate every
figure below them.

Usage:
    python tools/module-coverage-report.py [--census PATH] [--top N]
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_CENSUS = REPO_ROOT / "game" / "data" / "retail_module_census.json"

CLASS_ORDER = ("A", "B", "C", "D", "E")
STATUS_ORDER = ("consumed", "refused", "unhandled")


def fail(message: str) -> int:
    print(f"INCOHERENT: {message}", file=sys.stderr)
    return 1


def load_census(path: Path) -> dict:
    if not path.is_file():
        raise ValueError(f"census not found: {path}")
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError("census root is not an object")
    for key in ("trees", "members", "classes"):
        if key not in value:
            raise ValueError(f"census is missing its {key!r} block")
    return value


def validate_member(member: dict, trees: list[str]) -> None:
    name = member.get("name")
    if not isinstance(name, str) or not name:
        raise ValueError("census member without a name")
    letter = member.get("classification")
    if letter not in CLASS_ORDER:
        raise ValueError(f"{name}: invalid classification {letter!r}")
    if not member.get("classificationNote"):
        raise ValueError(f"{name}: empty classification note")
    status = member.get("status")
    if status not in STATUS_ORDER:
        raise ValueError(f"{name}: invalid status {status!r}")
    if status == "consumed" and not member.get("consumedBy"):
        raise ValueError(f"{name}: consumed without consumption evidence")
    if status == "refused" and not member.get("refusedBy"):
        raise ValueError(f"{name}: refused without refusal evidence")
    sites = member.get("declarationSites")
    if not isinstance(sites, dict) or set(sites) != set(trees):
        raise ValueError(f"{name}: declarationSites do not cover every tree")
    for tree, count in sites.items():
        if not isinstance(count, int) or count < 0:
            raise ValueError(f"{name}: invalid site count for {tree}")
    if sum(sites.values()) <= 0:
        raise ValueError(f"{name}: member with zero sites in every tree")


def pct(part: int, whole: int) -> str:
    return f"{(100.0 * part / whole):6.2f}%" if whole else "   n/a"


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--census", type=Path, default=DEFAULT_CENSUS)
    parser.add_argument("--top", type=int, default=30,
                        help="backlog rows per class (default 30)")
    args = parser.parse_args(argv)

    try:
        census = load_census(args.census)
        trees = sorted(census["trees"])
        members = census["members"]
        for member in members:
            validate_member(member, trees)
        for tree in trees:
            declared = census["trees"][tree].get("declarationSites")
            counted = sum(m["declarationSites"][tree] for m in members)
            if declared != counted:
                raise ValueError(
                    f"{tree}: trees block declares {declared} sites but "
                    f"members sum to {counted}"
                )
    except ValueError as exc:
        return fail(str(exc))

    print("RETAIL MODULE-TYPE COVERAGE - weighted by declaration sites")
    print(f"census: {args.census}")
    print(f"members: {len(members)} distinct module kinds")
    print()
    print("CAVEAT: 'consumed' = the importer names the type and extracts from it.")
    print("It does NOT mean the behaviour runs. Class C members need simulation")
    print("work no importer change can provide; their consumed share is at most")
    print("the flattened half of the work.")
    print()

    for tree in trees:
        total = census["trees"][tree]["declarationSites"]
        by_status = {status: 0 for status in STATUS_ORDER}
        for member in members:
            by_status[member["status"]] += member["declarationSites"][tree]
        print(f"{tree}: {total} declaration sites, "
              f"{census['trees'][tree]['distinctModuleKinds']} distinct kinds")
        print(f"  consumed   {by_status['consumed']:6d}  {pct(by_status['consumed'], total)}")
        print(f"  refused    {by_status['refused']:6d}  {pct(by_status['refused'], total)}  [deliberately unimplemented - NOT coverage]")
        print(f"  unhandled  {by_status['unhandled']:6d}  {pct(by_status['unhandled'], total)}")
        print()

    print("class breakdown (sites per tree; consumed share within class):")
    header = "  class  " + "".join(f"{tree:>24}" for tree in trees)
    print(header)
    for letter in CLASS_ORDER:
        row = [f"  {letter}      "]
        for tree in trees:
            class_total = sum(
                m["declarationSites"][tree]
                for m in members
                if m["classification"] == letter
            )
            class_consumed = sum(
                m["declarationSites"][tree]
                for m in members
                if m["classification"] == letter and m["status"] == "consumed"
            )
            row.append(f"{class_total:>10d} ({pct(class_consumed, class_total).strip():>7})")
        print("".join(row))
    print()
    print("  " + " / ".join(
        f"{letter}: {census['classes'][letter].split(':', 1)[0]}"
        for letter in CLASS_ORDER
    ))
    print()

    print(f"BACKLOG - not consumed, heaviest first, by class (top {args.top} per class)")
    print("(weight = max sites across trees; refused members listed with their reason)")
    for letter in CLASS_ORDER:
        backlog = [
            m for m in members
            if m["classification"] == letter and m["status"] != "consumed"
        ]
        backlog.sort(
            key=lambda m: (-max(m["declarationSites"].values()), m["name"].casefold())
        )
        class_note = census["classes"][letter].split(":", 1)[0]
        print()
        print(f"  class {letter} ({class_note}): {len(backlog)} members outstanding")
        for member in backlog[: args.top]:
            sites = "/".join(str(member["declarationSites"][tree]) for tree in trees)
            objects = "/".join(str(member["objectCount"][tree]) for tree in trees)
            line = (f"    {member['name']:<52} sites {sites:>11}  objects {objects:>9}"
                    f"  [{member['status']}]")
            print(line)
            if member["status"] == "refused" and member.get("refusalReasons"):
                print(f"      reason: {'; '.join(member['refusalReasons'])}")
        if len(backlog) > args.top:
            print(f"    ... and {len(backlog) - args.top} more")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
