"""Build a function inventory (rva,size,start_block_size,name CSV) from function entry points.

Both binaries get the same extent rule so masked hashes compare like with like: a function runs
from its entry to the next known entry in the same section, minus trailing int3 (0xCC) padding.
MSVC 7.1 places switch jump tables right after the code of their function, so the extent keeps
them (the masking in relib zeroes their absolute addresses); Ghidra's own body would drop them.

Entry sources (any number, unioned):
  --ghidra-url URL --program NAME   a GhidraMCP headless server (read-only /list_functions_enhanced)
  --ghidra-json PATH                a saved /list_functions_enhanced response
  --csv PATH[:rva_col[:name_col]]   any CSV with an RVA hex column (or a VA column named address), e.g. Open-BFME-2's
                                    reverse/ghidra_functions.csv (rva,name) or its ledger
                                    reverse/functions.csv (target_rva,name)

Names: the first source that names an entry wins (pass the most authoritative first).
No retail bytes are written: only addresses, sizes and names.

  python tools/re/inventory.py --bin <game.dat> --ghidra-url http://127.0.0.1:8089 \
      --program rotwk201_game.exe --out rotwk_functions.csv
"""
import argparse
import csv
import json
import sys
import urllib.parse
import urllib.request
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import relib  # noqa: E402


def ghidra_entries(data, base):
    out = []
    for f in data["functions"]:
        if f.get("isExternal"):
            continue
        out.append((int(f["address"], 16) - base, f["name"]))
    return out


def fetch_ghidra(url, program):
    q = urllib.parse.urlencode({"offset": 0, "limit": 1000000, "program": program})
    with urllib.request.urlopen(f"{url.rstrip('/')}/list_functions_enhanced?{q}", timeout=600) as r:
        return json.loads(r.read().decode("utf-8"))


def csv_entries(spec, base):
    parts = spec.split(":")
    path = parts[0]
    rva_col = parts[1] if len(parts) > 1 else None
    name_col = parts[2] if len(parts) > 2 else "name"
    out = []
    with open(path, newline="", encoding="utf-8") as fh:
        rd = csv.DictReader(fh)
        col = rva_col or next(c for c in ("rva", "target_rva", "address") if c in rd.fieldnames)
        for row in rd:
            v = row.get(col, "").strip()
            if not v:
                continue
            a = int(v, 16)
            if col == "address":  # VA column; rva / target_rva columns are already RVAs
                a -= base
            out.append((a, row.get(name_col, "") or ""))
    return out


def build(img, entries):
    """entries: [(rva, name)] -> sorted [(rva, size, blk, name)] for .text-resident entries."""
    names = {}
    for rva, name in entries:
        if rva not in names or (names[rva].startswith(("FUN_", "thunk_FUN_")) and name
                                and not name.startswith(("FUN_", "thunk_FUN_"))):
            names[rva] = name
    t0, t1 = img.text
    rvas = sorted(r for r in names if t0 <= r < t1)
    out = []
    for i, rva in enumerate(rvas):
        end = rvas[i + 1] if i + 1 < len(rvas) else t1
        while end > rva + 1 and img.mem[end - 1] == 0xCC:
            end -= 1
        size = end - rva
        out.append((rva, size, size, names[rva].replace(",", ";")))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--bin", required=True)
    ap.add_argument("--ghidra-url")
    ap.add_argument("--program")
    ap.add_argument("--ghidra-json")
    ap.add_argument("--save-json", help="also save the fetched Ghidra response here")
    ap.add_argument("--csv", action="append", default=[])
    ap.add_argument("--out", required=True)
    args = ap.parse_args()
    img = relib.Image(args.bin)
    entries = []
    if args.ghidra_url:
        data = fetch_ghidra(args.ghidra_url, args.program)
        if args.save_json:
            Path(args.save_json).write_text(json.dumps(data), encoding="utf-8")
        entries += ghidra_entries(data, img.base)
    if args.ghidra_json:
        entries += ghidra_entries(json.loads(Path(args.ghidra_json).read_text(encoding="utf-8")), img.base)
    for spec in args.csv:
        entries += csv_entries(spec, img.base)
    if not entries:
        raise SystemExit("no entry points given")
    inv = build(img, entries)
    with open(args.out, "w", newline="", encoding="utf-8") as fh:
        w = csv.writer(fh)
        w.writerow(["rva", "size", "start_block_size", "name"])
        for rva, size, blk, name in inv:
            w.writerow(["0x%X" % rva, size, blk, name])
    print(f"{len(inv)} functions ({sum(f[1] for f in inv)} bytes of .text) -> {args.out}")


if __name__ == "__main__":
    main()
