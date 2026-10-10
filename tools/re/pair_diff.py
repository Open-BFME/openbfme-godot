"""Compare a RotWK function with its BFME2 counterpart instruction by instruction.

Addresses are printed as RVAs relative to each function; absolute operands are shown as
symbolic kinds (string literal text, or "abs") so only real code differences remain. Output
is the aligned diff (difflib over normalised instruction text) plus a one-line verdict:
counts of equal / changed / inserted / deleted instructions and what kind of operands differ.
Prints disassembly only (no raw bytes); keep it out of commits like any retail-derived text.

  python tools/re/pair_diff.py --rotwk <game.dat> --bfme2 <game.dat> --map rw_map.csv 0x6A1234 [...]   (RotWK VAs)
  python tools/re/pair_diff.py ... --sample A:30 --seed 1     (random pairs of one tier)
"""
import argparse
import csv
import difflib
import random
import re
import sys
from pathlib import Path

import capstone

sys.path.insert(0, str(Path(__file__).resolve().parent))
import relib  # noqa: E402

HEX = re.compile(r"0x[0-9a-f]+")


def norm_listing(img, rva, size):
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    lo, hi = img.base, img.base + img.size
    out = []

    def sub(m):
        v = int(m.group(0), 16)
        if lo <= v < hi:
            s = img.string_at(v - img.base)
            if s is not None:
                return repr(s[:24])
            if rva + img.base <= v < rva + img.base + size:
                return "L+0x%x" % (v - img.base - rva)
            return "abs"
        return m.group(0)

    for ins in md.disasm(bytes(img.mem[rva:rva + size]), rva + img.base):
        ops = ins.op_str
        if ins.mnemonic.startswith(("j", "call")) and ops.startswith("0x"):
            t = int(ops, 16) - img.base
            ops = ("L+0x%x" % (t - rva)) if rva <= t < rva + size else "ext"
        else:
            ops = HEX.sub(sub, ops)
        out.append(f"{ins.mnemonic} {ops}".strip())
    return out


def compare(rimg, bimg, row, show=True, context=2):
    r, rs = int(row["rotwk_rva"], 16), int(row["size"])
    b, bs = int(row["bfme2_rva"], 16), int(row["bfme2_size"])
    a, c = norm_listing(rimg, r, rs), norm_listing(bimg, b, bs)
    sm = difflib.SequenceMatcher(None, a, c, autojunk=False)
    eq = ch = ins = dele = 0
    kinds = set()
    lines = []
    for op, i1, i2, j1, j2 in sm.get_opcodes():
        if op == "equal":
            eq += i2 - i1
            continue
        if op == "replace":
            ch += max(i2 - i1, j2 - j1)
            for x, y in zip(a[i1:i2], c[j1:j2]):
                if x.split()[0] == y.split()[0]:
                    kinds.add("operand")
                else:
                    kinds.add("opcode")
        elif op == "delete":
            dele += i2 - i1
        else:
            ins += j2 - j1
        lines.append(f"  @{i1}: RW  {' | '.join(a[i1:i2][:6])}")
        lines.append(f"  @{j1}: B2  {' | '.join(c[j1:j2][:6])}")
    verdict = (f"{row['rotwk_rva']} <-> {row['bfme2_rva']} tier {row['tier']} score {row['score']} "
               f"len {len(a)}/{len(c)} equal {eq} changed {ch} rw-only {dele} b2-only {ins} "
               f"kinds {','.join(sorted(kinds)) or '-'} | {row.get('decomp_qualname') or row.get('rotwk_name')}")
    print(verdict)
    if show:
        for ln in lines[:2 * context * 4]:
            print(ln[:220])
    return eq, ch, ins, dele


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--rotwk", required=True)
    ap.add_argument("--bfme2", required=True)
    ap.add_argument("--map", required=True)
    ap.add_argument("--sample", help="TIER:N random pairs (functions >= 24 bytes)")
    ap.add_argument("--seed", type=int, default=20261009)
    ap.add_argument("--quiet", action="store_true", help="verdict lines only")
    ap.add_argument("rva", nargs="*")
    args = ap.parse_args()
    rows = list(csv.DictReader(open(args.map, newline="", encoding="utf-8")))
    by = {int(r["rotwk_rva"], 16): r for r in rows}
    rimg, bimg = relib.Image(args.rotwk), relib.Image(args.bfme2)
    picks = []
    if args.sample:
        tier, n = args.sample.split(":")
        pool = [r for r in rows if r["tier"] == tier and int(r["size"]) >= 24]
        picks = random.Random(args.seed).sample(pool, min(int(n), len(pool)))
    for v in args.rva:  # RotWK VAs as Ghidra and docs/STOPS.md cite them; "rva:0x..." for an RVA
        x = int(v[4:], 16) if v.startswith("rva:") else int(v, 16) - rimg.base
        if x not in by:
            raise SystemExit(f"{v}: not a function entry in the map")
        picks.append(by[x])
    for row in picks:
        if not row["bfme2_rva"]:
            print(f"{row['rotwk_rva']}: RotWK-only")
            continue
        compare(rimg, bimg, row, show=not args.quiet)


if __name__ == "__main__":
    main()
