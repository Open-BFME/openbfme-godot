#!/usr/bin/env python3
"""Compare gcovr --json-summary reports without counting unbuilt files as zeroes.

Only engine/src is counted; vendor/thirdparty paths are never included. Simulation
classification comes from the configured sim_manifest.json, not directory guesses.
"""
from __future__ import annotations

import argparse
from collections import defaultdict
import json
from pathlib import Path


KEYS = ("line_covered", "line_total", "branch_covered", "branch_total")


def read_summary(path):
    files = {}
    for entry in json.loads(Path(path).read_text())["files"]:
        name = entry["filename"].replace("\\", "/")
        if not name.startswith("engine/src/") or {"vendor", "thirdparty"}.intersection(name.split("/")):
            continue
        files[name] = {key: entry[key] for key in KEYS}
    return files


def total(files):
    return {key: sum(entry[key] for entry in files.values()) for key in KEYS}


def metric(entry, kind):
    covered, count = entry[kind + "_covered"], entry[kind + "_total"]
    return f"{covered}/{count} ({100 * covered / count:.2f}%)" if count else "— (0/0)"


def table(before, after, names):
    rows = ["| Path | Lines before | Lines after | Branches before | Branches after |",
            "|---|---:|---:|---:|---:|"]
    for name in names:
        cells = []
        for kind in ("line", "branch"):
            cells.extend(metric(data[name], kind) if name in data else "not measured" for data in (before, after))
        rows.append("| `" + name + "` | " + " | ".join(cells) + " |")
    return "\n".join(rows)


def directories(files):
    groups = defaultdict(dict)
    for name, entry in files.items():
        groups[str(Path(name).parent)][name] = entry
    return {name: total(group) for name, group in groups.items()}


def render(before, after, simulation, selected, retail_only, sources=()):
    for name in selected:
        if name in simulation or name.startswith("engine/src/GameLogic/"):
            raise ValueError("selected simulation source: " + name)
        if name not in before or name not in after:
            raise ValueError("selected source was not measured: " + name)
    for name in retail_only:
        if any(name not in data or data[name]["line_covered"] for data in (before, after)):
            raise ValueError("retail-only exclusions must be measured at zero: " + name)
    b = {n: e for n, e in before.items() if n not in retail_only}
    a = {n: e for n, e in after.items() if n not in retail_only}
    eligible = sorted((n for n in b if n.endswith((".cpp", ".cc", ".c"))
                       and n not in simulation and not n.startswith("engine/src/GameLogic/")),
                      key=lambda n: (-(b[n]["line_total"] - b[n]["line_covered"]), n))
    sim_gaps = sorted((n for n in b if n in simulation),
                      key=lambda n: (-(b[n]["line_total"] - b[n]["line_covered"]), n))
    out = ["# No-game engine coverage", "", "## Overall (instrumented engine/src)", "",
           "`Overall` excludes the explicitly listed retail-only files; `All measured` retains them for auditability.", "",
           table({"Overall": total(b), "All measured": total(before)},
                 {"Overall": total(a), "All measured": total(after)}, ["Overall", "All measured"]), "", "## Selected files", "",
           table(before, after, selected), "", "## Non-simulation candidate ranking (baseline uncovered lines)", "",
           "Ranking is an inventory, not a claim that all missing lines are reachable without retail data.", "",
           "| Source | Uncovered lines |", "|---|---:|"]
    out.extend(f"| `{n}` | {b[n]['line_total'] - b[n]['line_covered']} |" for n in eligible)
    out += ["", "## Simulation gaps (findings only; no simulation changes)", "",
            "| Source | Baseline uncovered lines |", "|---|---:|"]
    out.extend(f"| `{n}` | {b[n]['line_total'] - b[n]['line_covered']} |" for n in sim_gaps if b[n]['line_total'] > b[n]['line_covered'])
    out += ["", "## Retail-only zero-coverage exclusions", "", "| Source | Reason |", "|---|---|"]
    out.extend(f"| `{n}` | {reason} |" for n, reason in sorted(retail_only.items()))
    if not retail_only:
        out.append("| None verified | A skipped retail test alone does not prove a source cannot be tested synthetically. |")
    bd, ad = directories(b), directories(a)
    out += ["", "## Per-directory (direct members, no double counting; excluding listed retail-only files)", "", table(bd, ad, sorted(bd.keys() | ad.keys())),
            "", "## Per-file (all measured sources and headers)", "", table(before, after, sorted(before.keys() | after.keys()))]
    unmeasured = sorted(set(sources) - (before.keys() | after.keys()))
    if unmeasured:
        out += ["", "## Unmeasured sources/headers (not compiled, or no executable code)", "",
                "Not counted as zero coverage or classified as retail-only.", "",
                "| Path | Status |", "|---|---|"]
        out.extend(f"| `{name}` | not measured |" for name in unmeasured)
    return "\n".join(out) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--before", required=True)
    parser.add_argument("--after", required=True)
    parser.add_argument("--manifest", required=True)
    parser.add_argument("--selected", required=True, help="JSON array of engine/src paths")
    parser.add_argument("--retail-only", help="JSON object mapping verified zero-coverage files to reasons")
    parser.add_argument("--source-root", help="engine/src tree to inventory unmeasured sources and headers")
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    simulation = {"engine/" + name for name in json.loads(Path(args.manifest).read_text())["sources"]}
    selected = json.loads(Path(args.selected).read_text())
    retail = json.loads(Path(args.retail_only).read_text()) if args.retail_only else {}
    sources = []
    if args.source_root:
        root = Path(args.source_root)
        sources = ["engine/src/" + path.relative_to(root).as_posix() for path in root.rglob("*")
                   if path.suffix in {".c", ".cc", ".cpp", ".h", ".hpp"}
                   and not {"vendor", "thirdparty"}.intersection(path.relative_to(root).parts)]
    Path(args.output).write_text(render(read_summary(args.before), read_summary(args.after), simulation, selected, retail, sources))


if __name__ == "__main__":
    main()
