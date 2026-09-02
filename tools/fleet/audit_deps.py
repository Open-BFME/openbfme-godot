#!/usr/bin/env python3
"""Before deleting a file, find every tracked file that names it.

    audit_deps.py <path>...            report referrers for each candidate
    audit_deps.py --list FILE          candidates one per line (# comments ok)
    audit_deps.py ... --check          exit 1 if any candidate has referrers

A reference is the candidate's base name appearing in another tracked text
file. Markdown under docs/ and the candidate itself are ignored, because
prose pointing at a deleted file is not a broken build. Binary files are
skipped.
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

import fleetlib as fl

TEXT_SUFFIXES = {
    ".py", ".gd", ".cs", ".ps1", ".bat", ".yml", ".yaml", ".json", ".toml",
    ".txt", ".md", ".tscn", ".tres", ".cfg", ".godot", ".csproj", ".sln",
    ".xaml", ".mjs", ".js", ".ts", ".ini", ".inc", ".xml", ".sh",
}


def _ignored(rel: str, candidate: str) -> bool:
    if rel == candidate:
        return True
    if rel.startswith("docs/") and rel.endswith(".md"):
        return True
    return False


def audit(candidates: list[str]) -> dict[str, list[str]]:
    tracked = fl.tracked_files()
    texts: dict[str, str] = {}
    for rel in tracked:
        if Path(rel).suffix.lower() not in TEXT_SUFFIXES:
            continue
        try:
            texts[rel] = (fl.REPO / rel).read_text(encoding="utf-8", errors="ignore")
        except OSError:
            continue
    report: dict[str, list[str]] = {}
    for cand in candidates:
        cand = cand.replace("\\", "/").strip()
        if not cand:
            continue
        name = Path(cand).name
        hits = [rel for rel, text in texts.items() if name in text and not _ignored(rel, cand)]
        report[cand] = sorted(hits)
    return report


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("paths", nargs="*")
    parser.add_argument("--list")
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args(argv)
    candidates = list(args.paths)
    if args.list:
        for line in Path(args.list).read_text(encoding="utf-8").splitlines():
            line = line.strip()
            if line and not line.startswith("#"):
                candidates.append(line)
    if not candidates:
        parser.error("give paths or --list")
    report = audit(candidates)
    blocked = 0
    for cand, hits in report.items():
        if hits:
            blocked += 1
            print(f"REFERENCED {cand}")
            for hit in hits:
                print(f"    {hit}")
        else:
            print(f"FREE       {cand}")
    if args.check and blocked:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
