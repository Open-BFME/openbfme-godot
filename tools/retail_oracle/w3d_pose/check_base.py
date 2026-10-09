"""Validates emu.py against the real Base_Update: runs the loop body 0x562930-0x562B7A of BFME2's Base_Update on the rows that
oracle_base.py printed (saved to a file) and compares every pivot's world (q, t) bit for bit.

    python dump_asm.py <port> 0x5628A0 base_asm.txt
    python oracle_base.py > base_vectors.txt
    python check_base.py base_asm.txt base_vectors.txt
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from emu import Emu, parse  # noqa: E402

instrs = parse(sys.argv[1])
rows = []
for line in open(sys.argv[2]):
    nums = [int(x, 16) for x in re.findall(r'0x([0-9A-F]{8})u', line)]
    if len(nums) == 12 + 4 + 3 + 4 + 3 + 21:
        rows.append(nums)
print(len(rows), 'rows')
THIS, PIV = 0x10000, 0x20000
bad = 0
for ri, n in enumerate(rows):
    aq, at, bq, bt, world = n[12:16], n[16:19], n[19:23], n[23:26], n[26:]
    mem = {THIS + 0x10: 3, THIS + 0x14: PIV, THIS + 0x1C: 0, THIS + 0x20: 0}
    for k in range(7):
        mem[PIV + 0x30 + 4 * k] = world[k]  # pivot 0 world: the root matrix conversion (FUN_00b17b40) is not under test here
    for i, (q, t) in ((1, (aq, at)), (2, (bq, bt))):
        base = PIV + 0x58 * i
        mem[base + 0x10] = PIV + 0x58 * (i - 1)
        for k in range(4):
            mem[base + 0x14 + 4 * k] = q[k]
        for k in range(3):
            mem[base + 0x24 + 4 * k] = t[k]
    for i in (1, 2):
        e = Emu({'ESI': THIS, 'EBP': 0x58 * i, 'EBX': 1, 'EAX': 0xFFFFFFFF, 'EDI': i}, mem)
        e.run(instrs, 0x562930, 0x562b7a)
        mem = e.m
        assert not e.unknown, e.unknown
        got = [mem[PIV + 0x58 * i + 0x30 + 4 * k] for k in range(7)]
        want = world[7 * i:7 * i + 7]
        if got != want:
            bad += 1
            print('row', ri, 'pivot', i, 'MISMATCH', [hex(x) for x in got], [hex(x) for x in want])
print('mismatches:', bad)
