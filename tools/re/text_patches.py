"""Find .text instructions that reach into sections added after link (protection/patch sections).

Disassembles every inventory function body (instruction-accurate, not a raw byte scan: a raw
E8/E9 scan of RotWK produces hundreds of false hits inside other instructions) and reports
each call/jump target, memory operand or immediate that lands in one of --sections. Also
summarises how much of each added section is non-zero and which of its bytes are referenced.

  python tools/re/text_patches.py --bin <game.dat> --inv <functions.csv> \
      --sections stxt774,stxt371,.mackt,.danetta --out <hits.tsv>
"""
import argparse
import collections
import struct
import sys
from pathlib import Path

import capstone

sys.path.insert(0, str(Path(__file__).resolve().parent))
import relib  # noqa: E402


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--bin", required=True)
    ap.add_argument("--inv", required=True)
    ap.add_argument("--sections", default="stxt774,stxt371,.mackt,.danetta")
    ap.add_argument("--out", required=True)
    args = ap.parse_args()
    img = relib.Image(args.bin)
    wanted = args.sections.split(",")
    added = [s for s in img.sections if s[0] in wanted]
    t0, t1 = img.text
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    md.skipdata = True
    base = img.base

    def which(rva):
        for name, lo, hi, _ in added:
            if lo <= rva < hi:
                return name
        return None

    hits = []
    iat_sections = collections.Counter()
    for rva, size, blk, name in relib.load_inventory(args.inv):
        if not (t0 <= rva < t1):
            continue
        code = img.mem[rva:rva + (blk or size)]
        for ins in md.disasm(code, rva):
            if ins.id == 0:
                continue
            targets = []
            if 7 in ins.groups and ins.operands and ins.operands[0].type == 2:
                targets.append(("branch", ins.operands[0].imm & 0xFFFFFFFF))
            else:
                off = ins.address - rva
                for fo, fs, kind in ((ins.disp_offset, ins.disp_size, "mem"), (ins.imm_offset, ins.imm_size, "imm")):
                    if fs == 4 and off + fo + 4 <= len(code):
                        v = struct.unpack_from("<I", code, off + fo)[0]
                        if base <= v < base + img.size:
                            targets.append((kind, v - base))
                if ins.mnemonic in ("call", "jmp") and ins.operands and ins.operands[0].type == 3 \
                        and ins.operands[0].mem.base == 0 and ins.operands[0].mem.index == 0:
                    iat_sections[img.section_of((ins.operands[0].mem.disp & 0xFFFFFFFF) - base)] += 1
            for kind, tgt in targets:
                sec = which(tgt)
                if sec:
                    hits.append((ins.address, rva, name, kind, tgt, sec,
                                 f"{ins.mnemonic} {ins.op_str}", ins.bytes.hex()))
    with open(args.out, "w", encoding="utf-8") as fh:
        fh.write("site_rva\tfunc_rva\tfunc_name\tkind\ttarget_rva\tsection\tinstruction\tbytes\n")
        for h in hits:
            fh.write("0x%X\t0x%X\t%s\t%s\t0x%X\t%s\t%s\t%s\n" % h)
    print(f"{len(hits)} references from .text into {wanted}")
    by = collections.Counter((h[5], h[3]) for h in hits)
    for k, v in sorted(by.items()):
        print(f"  {k[0]:10} {k[1]:6} {v}")
    print("indirect call/jmp [abs] targets by section:", dict(iat_sections))
    for name, lo, hi, ch in added:
        data = img.mem[lo:hi]
        nz = sum(1 for b in data if b)
        print(f"section {name}: rva 0x{lo:X}-0x{hi:X} ({hi - lo} bytes), non-zero {nz}, characteristics 0x{ch:08X}")


if __name__ == "__main__":
    main()
