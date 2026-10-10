"""Export an Open-BFME ledger (reverse/functions.csv) with a provenance lane per row.

The lane is computed by the clone's own tools/progress.py (source_lane plus its
naked-asm detector), so this agrees with the project's published headline:

  authored   C++ written from the disassembly
  vendored   upstream library source compiled (zlib, STLport, CRT sources, GameSpy ...)
  generated  generator-written C++ (EH funclets, thunks: gen_small/)
  library    prebuilt .lib attached (CRT, d3dx9 ...)
  dump       __declspec(naked)/__emit or MASM re-encoded retail bytes

Read-only: runs `git grep`/`git ls-files` inside the clone and imports its modules.

  python tools/re/ledger_lanes.py --clone <open-bfme-2 clone> --out bfme2_ledger.csv
  python tools/re/ledger_lanes.py --clone <open-bfme-1 clone> --out bfme1_ledger.csv
"""
import argparse
import csv
import os
import sys
from pathlib import Path


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--clone", required=True, help="Open-BFME-1 or Open-BFME-2 checkout")
    ap.add_argument("--out", required=True)
    args = ap.parse_args()
    clone = Path(args.clone).resolve()
    out = Path(args.out).resolve()
    sys.path.insert(0, str(clone / "tools"))
    os.chdir(clone)
    import progress  # the clone's own module

    matched = progress.matched_at(None)
    notes = progress.notes_at(None)
    naked = progress.naked_cpp_rows_at(matched, None)
    with open(out, "w", newline="", encoding="utf-8") as fh:
        w = csv.writer(fh)
        w.writerow(["name", "target_rva", "target_size", "source", "lane"])
        for key, (size, source) in matched.items():
            lane = progress.source_lane(source, notes[key], key in naked)
            w.writerow([key[0], key[1], size, source, lane])
    print(f"{len(matched)} rows ({len(naked)} naked) -> {out}")


if __name__ == "__main__":
    main()
