#!/usr/bin/env python3
"""Lane BUGS-1: the open-stop count the README quotes, generated from docs/STOPS.md.

An open stop is a row of docs/STOPS.md whose Kind is not `closed` (docs/ROADMAP.md, "Stop burn-down"): the `code`
rows (runtime-reported gaps) and the `project` rows (provenance facts). tools/test_stops_count.py fails when the
README's "**N open stops**" differs; `python3 tools/stops_count.py --write` rewrites it.
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
STOPS = REPO / "docs" / "STOPS.md"
README = REPO / "README.md"

ROW = re.compile(r"^\| (S-\d+) \| ([a-z]+) \|")
CLAIM = re.compile(r"\*\*([\d,]+) open stops\*\*")
KINDS = {"code", "project", "closed"}


def kinds(text: str) -> dict[str, str]:
    """ID -> Kind for every stop row; an unknown Kind or a repeated ID is an error, never skipped."""
    out: dict[str, str] = {}
    for n, line in enumerate(text.splitlines(), 1):
        if not line.startswith("| S-"):
            continue
        m = ROW.match(line)
        if not m or m.group(2) not in KINDS:
            raise ValueError(f"STOPS.md:{n}: a stop row without a known Kind ({sorted(KINDS)})")
        if m.group(1) in out:
            raise ValueError(f"STOPS.md:{n}: {m.group(1)} is listed twice")
        out[m.group(1)] = m.group(2)
    return out


def open_count(text: str) -> int:
    return sum(1 for k in kinds(text).values() if k != "closed")


def readme_claim(text: str) -> int:
    found = CLAIM.findall(text)
    if len(found) != 1:
        raise ValueError(f"README.md: expected one '**N open stops**', found {len(found)}")
    return int(found[0].replace(",", ""))


def main(argv: list[str]) -> int:
    n = open_count(STOPS.read_text(encoding="utf-8"))
    readme = README.read_text(encoding="utf-8")
    claim = readme_claim(readme)
    if "--write" in argv:
        if claim != n:
            README.write_bytes(CLAIM.sub(f"**{n} open stops**", readme).encode("utf-8"))
        print(f"README.md: {n} open stops")
        return 0
    print(f"docs/STOPS.md: {n} open stops; README.md says {claim}")
    return 0 if claim == n else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
