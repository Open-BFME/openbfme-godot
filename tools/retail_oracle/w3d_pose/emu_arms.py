"""Rows for test_w3d_pose.cpp: the two generic Anim_Update arms (0x562BB0) of BFME2 1.06 run from their instructions (README.md)."""
import os, random, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from emu import *

instrs = parse(os.environ.get('ARM_ASM', 'arm_asm.txt'))
THIS, PIV, ESP = 0x10000, 0x20000, 0x30000


def f32(x):
    return float(np.float32(x))


def run_arm(arm, scale, base, anim, rot):
    """arm 0 = motion-channel arm (562BB0 lines 84-222), 1 = classic arm. base = [(q4,t3)]*2, anim = [(t3,q4)]*2, rot = [bool]*2.
    Returns the world (q4, t3) bits of pivots 1 and 2."""
    mem = {THIS + 0x10: 3, THIS + 0x14: PIV, THIS + 0x18: f2b(scale), THIS + 0x1C: 0, THIS + 0x20: 0}
    for k, v in enumerate([0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0]):
        mem[PIV + 0x30 + 4 * k] = f2b(v)
    for i in (1, 2):
        q, t = base[i - 1]
        b = PIV + 0x58 * i
        mem[b + 0x10] = PIV + 0x58 * (i - 1)
        for k in range(4): mem[b + 0x14 + 4 * k] = f2b(q[k])
        for k in range(3): mem[b + 0x24 + 4 * k] = f2b(t[k])
    out = []
    xs = {}
    for i in (1, 2):
        at, aq = anim[i - 1]
        mem[ESP + 0x14] = 0x58 * i
        regs = {'EBP': THIS, 'ESP': ESP, 'EBX': 1, 'EAX': 0, 'ECX': 1, 'EDX': 0, 'ESI': 0, 'EDI': 0}
        e = Emu(regs, mem)
        segs = []
        if arm == 0:
            segs.append((0x562c90, 0x562ed1))
            mem_extra = {ESP + 0x18: f2b(scale), ESP + 0x5c: f2b(at[0])}
            e.run(instrs, 0x562c90, 0x562ed1)
            e.m.update(mem_extra)
            e.x['XMM3'] = np.float32(at[1])
            e.x['XMM5'] = np.float32(at[2])
            e.run(instrs, 0x562f6b, 0x562f8f)
            if rot[i - 1]:
                e.m[ESP + 0x4c], e.m[ESP + 0x50], e.m[ESP + 0x54], e.m[ESP + 0x58] = [f2b(v) for v in aq]
                e.run(instrs, 0x562fb2, 0x5631ea)
            else:
                e.run(instrs, 0x5631f4, 0x563316)
        else:
            e.run(instrs, 0x563441, 0x563678)
            e.m[ESP + 0x40], e.m[ESP + 0x44], e.m[ESP + 0x48] = [f2b(v) for v in at]
            if scale != 1.0:
                e.x['XMM0'] = np.float32(scale)
                e.run(instrs, 0x5636a7, 0x5636d0)
            if rot[i - 1]:
                e.m[ESP + 0x30], e.m[ESP + 0x34], e.m[ESP + 0x38], e.m[ESP + 0x3c] = [f2b(v) for v in aq]
                e.run(instrs, 0x5636ee, 0x56392d)
            else:
                e.run(instrs, 0x5636ee, 0x56372a)
                e.run(instrs, 0x563937, 0x563a17)
        assert not e.unknown, e.unknown
        mem = e.m
        out.append([mem[PIV + 0x58 * i + 0x30 + 4 * k] for k in range(7)])
    return out


def row(arm, scale, rotmask, base, anim):
    rot = [bool(rotmask & 1), bool(rotmask & 2)]
    w = run_arm(arm, scale, base, anim, rot)
    def h(v): return '0x%08Xu' % f2b(v)
    def hs(vs): return ', '.join(h(v) for v in vs)
    return '{ %d, %s, %d, { %s }, { %s }, { %s }, { %s }, { %s }, { %s }, { %s }, { %s }, { { %s }, { %s } } },' % (
        arm, h(scale), rotmask, hs(base[0][0]), hs(base[0][1]), hs(base[1][0]), hs(base[1][1]),
        hs(anim[0][0]), hs(anim[0][1]), hs(anim[1][0]), hs(anim[1][1]),
        ', '.join('0x%08Xu' % x for x in w[0]), ', '.join('0x%08Xu' % x for x in w[1]))


rng = random.Random(99)
rows = []
golden_base = [([0.1, 0.2, 0.3, 0.9], [1, 2, 3]), ([0.5, -0.5, 0.5, 0.5], [0.4, -1.5, 2.2])]
golden_anim = [([0.3, 0.2, -0.1], [0.2, 0.1, -0.3, 0.8]), ([-0.7, 0.9, 0.25], [0.0, 0.6, 0.0, 0.8])]
for arm in (0, 1):
    for mask in (3, 0):
        rows.append(row(arm, 1.0, mask, [([f32(v) for v in q], [f32(v) for v in t]) for q, t in golden_base],
                        [([f32(v) for v in t], [f32(v) for v in q]) for t, q in golden_anim]))
for arm in (0, 1):
    for mask in (3, 0, 1, 2):
        for scale in (1.0, 0.5, 2.25):
            for rep in range(2):
                base = [([f32(rng.uniform(-1, 1)) for _ in range(4)], [f32(rng.uniform(-5, 5)) for _ in range(3)]) for _ in range(2)]
                anim = [([f32(rng.uniform(-3, 3)) for _ in range(3)], [f32(rng.uniform(-1, 1)) for _ in range(4)]) for _ in range(2)]
                rows.append(row(arm, scale, mask, base, anim))
print('\n'.join(rows))
