#!/usr/bin/env python3
"""Write engine/tests/data/bfme1/ini_fields.txt: the field names of BFME1's INI tables.

engine/tests/test_bfme1_fields.cpp loads each one through BFME1's parsers. The names come from
a checkout of the public Open-BFME-1 decomp, not from any game file:
  docs/ini_schema.md                     top-level INI blocks, read from lotrbfme.exe
  targets/game/reverse/field_names.csv   module field tables, by table address

The csv has no reliable module name, so a table goes to the RotWK module whose own table
(engine/data/rotwk-201) shares the most of its names. A table with no clear match is left out
and counted, so this list can miss a module but should not blame the wrong one.
"""
import argparse
import collections
import csv
import json
import os
import re
import subprocess
import sys

MODULE_KEYWORD = {0: "Behavior", 1: "Draw", 2: "ClientUpdate", 3: "ClientBehavior"}
# a table joins a module only if it shares at least this many names and this share of its own
MIN_SHARED = 2
MIN_SHARE = 0.5


def read_blocks(path):
    """Block name -> its top-level field names. Sub-block tables sit in <details> and are left out."""
    blocks = {}
    block = None
    nested = False
    with open(path, encoding="utf-8") as f:
        for line in f:
            heading = re.match(r"### `([^`]+)`", line)
            if heading:
                block = heading.group(1)
                blocks[block] = []
                nested = False
            elif line.startswith("<details>"):
                nested = True
            elif line.startswith("</details>"):
                nested = False
            elif block and not nested:
                row = re.match(r"\| `([^`]+)` \|", line)
                if row and row.group(1) not in blocks[block]:
                    blocks[block].append(row.group(1))
    return blocks


def read_tables(path):
    """Table address -> (field names, the csv's most common class name, or empty)."""
    rows = collections.defaultdict(list)
    with open(path, encoding="utf-8", newline="") as f:
        for row in csv.DictReader(f):
            rows[row["table_rva"]].append(row)
    tables = {}
    for address, entries in rows.items():
        names = list(dict.fromkeys(e["ini_key"] for e in entries))
        votes = collections.Counter(e["upstream_class"] for e in entries if e["upstream_class"])
        tables[address] = (names, votes.most_common(1)[0][0] if votes else "")
    return tables


def read_modules(data_dir):
    """Module class -> (keyword, names per table, all names)."""
    with open(os.path.join(data_dir, "module-registry.json"), encoding="utf-8") as f:
        registry = json.load(f)
    with open(os.path.join(data_dir, "field-tables.json"), encoding="utf-8") as f:
        tables = json.load(f)["tables"]
    modules = {}
    for cls in registry["classes"]:
        per_table = [{row["name"] for row in tables[t["table"]]["rows"]} for t in cls["tables"]]
        modules[cls["name"]] = (MODULE_KEYWORD[cls["type"]], per_table, set().union(*per_table) if per_table else set())
    return modules


def match_module(names, upstream, modules):
    """The one module this table belongs to, or None when the names don't settle it."""
    wanted = set(names)
    hinted = re.sub(r"ModuleData$", "", upstream)
    scored = []
    for name, (_, per_table, _) in modules.items():
        best = max((len(wanted & t) for t in per_table), default=0)
        if best >= MIN_SHARED and best / len(wanted) >= MIN_SHARE:
            # a base-class table is shared by many modules; any of them can host the probe
            exact = max((len(wanted & t) / len(wanted | t) for t in per_table), default=0)
            scored.append((best, exact, name == hinted, name))
    if not scored:
        return None
    scored.sort(key=lambda s: (-s[0], -s[1], not s[2], s[3]))
    top = scored[0]
    rivals = [s for s in scored if s[:2] == top[:2]]
    if len(rivals) > 1 and not top[2]:
        # the rivals must all hold the same table, or the match is a guess
        own = lambda s: frozenset(max(modules[s[3]][1], key=lambda t: len(wanted & t)))
        if len({own(s) for s in rivals}) > 1:
            return None
    return top[3]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--schema", required=True, help="Open-BFME-1 docs/ini_schema.md")
    parser.add_argument("--fields", required=True, help="Open-BFME-1 targets/game/reverse/field_names.csv")
    parser.add_argument("--commit", required=True, help="the Open-BFME-1 commit they came from")
    args = parser.parse_args()
    root = subprocess.check_output(["git", "rev-parse", "--show-toplevel"], text=True).strip()

    blocks = read_blocks(args.schema)
    modules = read_modules(os.path.join(root, "engine", "data", "rotwk-201"))
    per_module = collections.defaultdict(dict)
    unmatched = 0
    for names, upstream in read_tables(args.fields).values():
        module = match_module(names, upstream, modules)
        if module is None:
            unmatched += 1
            continue
        per_module[module].update(dict.fromkeys(names))

    out = os.path.join(root, "engine", "tests", "data", "bfme1", "ini_fields.txt")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write(f"# BFME1 INI field names. Written by tools/bfme1_fields.py from Open-BFME-1 {args.commit}.\n")
        f.write(f"# {unmatched} module tables had no clear match and are left out; sub-block fields are left out.\n")
        for block in sorted(blocks):
            if blocks[block]:
                f.write(f"block {block} {' '.join(blocks[block])}\n")
        for module in sorted(per_module):
            keyword = "Body" if module.endswith(("Body", "BodyModule")) else modules[module][0]
            f.write(f"module {keyword} {module} {' '.join(per_module[module])}\n")
    print(f"{len(blocks)} blocks, {len(per_module)} modules, {unmatched} tables unmatched: {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
