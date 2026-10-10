"""Look up the BFME2 1.06 counterpart and the Open-BFME-2 decomp source of a RotWK 2.01 function.

  python tools/re/rw2decomp.py 0x66B3FD              RotWK VA (any address inside a function)
  python tools/re/rw2decomp.py rva:0x26B3FD          RotWK RVA
  python tools/re/rw2decomp.py privateMoveToPosition name: RotWK Ghidra name or decomp name
                                                     (substring; case-insensitive)
  python tools/re/rw2decomp.py --bfme2 0x66B487      reverse: BFME2 VA -> RotWK function(s)

Tiers (see tools/re/match_rotwk.py):
  A  identical to BFME2 after masking addresses: the BFME2 source describes the RotWK code
     exactly (callees, globals and string addresses differ; follow them through this tool too).
     "amb...-far" = identical code whose BFME2 instance is uncertain (tiny duplicated bodies).
  B  same function, changed: "same-shape" = only operand values differ (struct offsets, constants,
     frame size); "edited" = instructions added/removed. Read the source, then check the RotWK
     disassembly where they differ (tools/re/pair_diff.py shows exactly where).
  C  string-anchored only; D  diverged (similarity 0.6-0.9): starting points, not descriptions.
  RotWK-only: no BFME2 counterpart; disassembly is the only source.
Always cite the RotWK address; the decomp is a donor (BFME2), never the target.

Data: the map from match_rotwk.py (rw_map.csv) and an Open-BFME-2 clone. Defaults are
$OPENBFME_XMAP / $OPENBFME_DECOMP2, else <main checkout>/workspace/rebuild/xmap/rw_map.csv and
<main checkout>/workspace/reference/open-bfme-2 (the main checkout found through git, so it works
from any lane worktree).
"""
import argparse
import bisect
import csv
import os
import re
import subprocess
import sys
from pathlib import Path

BASE = 0x400000  # image base of both game.dat files

GUIDE = {
    "A": "identical code: the decomp source describes RotWK exactly (modulo addresses)",
    "B": "same function, changed in RotWK: read the source, verify the differences (pair_diff.py)",
    "C": "string-anchored counterpart only: a starting point, verify against the disassembly",
    "D": "diverged (similarity 0.6-0.9): the BFME2 source is a starting point only",
    "-": "RotWK-only: no BFME2 counterpart, read the disassembly",
}


def main_checkout():
    try:
        common = subprocess.run(["git", "rev-parse", "--path-format=absolute", "--git-common-dir"],
                                cwd=Path(__file__).resolve().parent, capture_output=True, text=True,
                                check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return Path(__file__).resolve().parents[2]
    return Path(common).parent


def load(path):
    if not path.is_file():
        raise SystemExit(f"map not found: {path}\n(build it with tools/re/match_rotwk.py, see "
                         f"tools/re/README.md, or set OPENBFME_XMAP)")
    with open(path, newline="", encoding="utf-8") as fh:
        rows = list(csv.DictReader(fh))
    for r in rows:
        r["_rva"] = int(r["rotwk_rva"], 16)
        r["_size"] = int(r["size"])
        r["_b2"] = int(r["bfme2_rva"], 16) if r["bfme2_rva"] else None
    rows.sort(key=lambda r: r["_rva"])
    return rows


def find_line(decomp, source, qualname):
    """Line of the definition of qualname in source (Class::method( or a free function name)."""
    if not decomp or not source or not qualname:
        return None
    p = decomp / source
    if not p.is_file():
        return None
    parts = qualname.split("::")
    leaf = parts[-1].replace("<>", "")
    if leaf in ("`scalar deleting destructor'", "`vector deleting destructor'", "`vftable'"):
        leaf = "~" + parts[-2] if len(parts) > 1 else leaf
    owner = parts[-2].replace("<>", "") if len(parts) > 1 else ""
    pat = re.compile((re.escape(owner) + r"\s*::\s*" if owner else r"\b") + re.escape(leaf) + r"\s*\(")
    try:
        lines = p.read_text(encoding="utf-8", errors="replace").splitlines()
    except OSError:
        return None
    for n, line in enumerate(lines, 1):
        if pat.search(line) and not line.rstrip().endswith(";"):
            return n
    return None


def show(r, decomp, query_note=""):
    va = r["_rva"] + BASE
    head = f"RW 0x{va:08X} (rva 0x{r['_rva']:X}, {r['_size']} B) {r['rotwk_name']}  [{r['subsystem']}]"
    print(head + query_note)
    tier = r["tier"]
    if tier == "-":
        print(f"  {GUIDE['-']}")
        return
    how = f", {r['how']}" if r["how"] else ""
    print(f"  tier {tier} (score {r['score']}{how}): BFME2 0x{r['_b2'] + BASE:08X} "
          f"(rva 0x{r['_b2']:X}, {r['bfme2_size']} B)")
    st = r["decomp_status"]
    if r["decomp_source"]:
        line = find_line(decomp, r["decomp_source"], r["decomp_qualname"])
        loc = r["decomp_source"] + (f":{line}" if line else "")
        name = r["decomp_qualname"] or r["decomp_name"]
        label = {"authored": "byte-matched C++", "vendored": "byte-matched vendored library source",
                 "generated": "byte-matched generated C++ (EH funclet / thunk)",
                 "attempt": "recovered C++, NOT byte-matching yet",
                 "tu-only": "no code yet; decomp assigns it to this file",
                 "library": "prebuilt library (no source)", "dump": "re-encoded bytes only (no source)"}.get(st, st)
        print(f"  decomp: {label}")
        print(f"    {loc}" + (f"  {name}" if name else ""))
    else:
        print(f"  decomp: nothing for this BFME2 function yet ({st or 'none'})")
    if r.get("near_source"):
        print(f"    probably in {r['near_source']} (the decomp rows on both sides are from it)")
    print(f"  -> {GUIDE[tier]}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("query", nargs="*", help="RotWK VA, rva:0x..., or a name substring")
    ap.add_argument("--bfme2", action="append", default=[], help="BFME2 VA (or rva:0x...) -> RotWK")
    ap.add_argument("--map", type=Path)
    ap.add_argument("--decomp", type=Path)
    ap.add_argument("--limit", type=int, default=20)
    args = ap.parse_args()
    root = main_checkout()
    map_path = args.map or Path(os.environ.get("OPENBFME_XMAP") or root / "workspace/rebuild/xmap/rw_map.csv")
    decomp = args.decomp or Path(os.environ.get("OPENBFME_DECOMP2") or root / "workspace/reference/open-bfme-2")
    if not decomp.is_dir():
        print(f"(decomp clone not found at {decomp}: no line numbers)", file=sys.stderr)
        decomp = None
    rows = load(map_path)
    starts = [r["_rva"] for r in rows]
    status = 0

    def parse_addr(text):
        if text.lower().startswith("rva:"):
            return int(text[4:], 16)
        v = int(text, 16)
        if v < BASE:
            raise SystemExit(f"{text}: below the image base; pass a VA or rva:0x...")
        return v - BASE

    for q in args.query:
        if re.fullmatch(r"(rva:)?(0x)?[0-9A-Fa-f]{5,8}", q, re.I):
            rva = parse_addr(q)
            k = bisect.bisect_right(starts, rva) - 1
            if k < 0 or rva >= rows[k]["_rva"] + rows[k]["_size"]:
                print(f"{q}: not inside a known RotWK function")
                status = 1
                continue
            note = "" if rva == rows[k]["_rva"] else f"   <- contains {q} (+0x{rva - rows[k]['_rva']:X})"
            show(rows[k], decomp, note)
        else:
            ql = q.lower()
            hits = [r for r in rows if ql in r["rotwk_name"].lower() or ql in r["decomp_qualname"].lower()
                    or ql in r["decomp_name"].lower()]
            if not hits:
                print(f"{q}: no RotWK function or decomp counterpart by that name")
                status = 1
            exact = [r for r in hits if r["rotwk_name"] == q or r["decomp_qualname"].split("::")[-1] == q
                     or r["decomp_qualname"] == q]
            hits = exact or hits
            for r in hits[:args.limit]:
                show(r, decomp)
            if len(hits) > args.limit:
                print(f"... {len(hits) - args.limit} more (--limit)")
    for q in args.bfme2:
        b2 = parse_addr(q)
        hits = [r for r in rows if r["_b2"] is not None and r["_b2"] <= b2 < r["_b2"] + int(r["bfme2_size"] or 0)]
        if not hits:
            print(f"BFME2 {q}: no RotWK counterpart (removed in RotWK, or changed beyond the D tier)")
            status = 1
        for r in hits:
            show(r, decomp)
    return status


if __name__ == "__main__":
    sys.exit(main())
