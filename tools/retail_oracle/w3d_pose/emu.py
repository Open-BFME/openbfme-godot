"""Scalar float32 emulator for the SSE / integer subset the BFME2 pose functions use (see README.md)."""
import re, struct
import numpy as np

REGS = ('EAX', 'EBX', 'ECX', 'EDX', 'ESI', 'EDI', 'EBP', 'ESP')


def parse(path):
    out = []
    for line in open(path):
        line = line.rstrip('\n')
        if not line:
            continue
        a, rest = line.split(' ', 1)
        mn, _, ops = rest.partition(' ')
        out.append((int(a, 16), mn, ops))
    return out


def f2b(f):
    return struct.unpack('<I', struct.pack('<f', float(f)))[0]


def b2f(b):
    return np.float32(struct.unpack('<f', struct.pack('<I', b & 0xFFFFFFFF))[0])


class Emu:
    def __init__(self, regs, mem):
        self.r = dict(regs)
        self.m = dict(mem)   # byte-aligned dword address -> uint32 bits
        self.x = {}
        self.unknown = set()

    def ea(self, op):
        m = re.match(r'(?:dword ptr |byte ptr )?\[(.*)\]$', op)
        inner = m.group(1).replace(' - ', ' + -')
        v = 0
        for t in [s.strip() for s in inner.split(' + ')]:
            if re.match(r'-?0x[0-9a-f]+$', t):
                v += int(t, 0)
            elif '*' in t:
                r, sc = t.split('*')
                v += self.r[r] * int(sc, 0)
            else:
                v += self.r[t]
        return v & 0xFFFFFFFF

    def ld(self, op):
        a = self.ea(op)
        return self.m.get(a, 0)

    def val(self, s):
        if s in REGS:
            return self.r[s]
        if s.startswith('XMM'):
            return None
        if re.match(r'-?0x[0-9a-f]+$', s):
            return int(s, 0) & 0xFFFFFFFF
        return self.ld(s)

    def xv(self, s):
        if s.startswith('XMM'):
            return self.x.get(s, np.float32(0))
        return b2f(self.ld(s))

    def run(self, instrs, lo, hi):
        for addr, mn, ops in instrs:
            if addr < lo or addr > hi:
                continue
            parts = [p.strip() for p in re.split(r',(?![^\[]*\])', ops)] if ops else []
            if mn in ('MOVSS', 'MOVAPS'):
                d, s = parts
                if d.startswith('XMM'):
                    self.x[d] = self.xv(s)
                else:
                    self.m[self.ea(d)] = f2b(self.x[s])
            elif mn == 'MULSS':
                self.x[parts[0]] = np.float32(self.x[parts[0]] * self.xv(parts[1]))
            elif mn == 'ADDSS':
                self.x[parts[0]] = np.float32(self.x[parts[0]] + self.xv(parts[1]))
            elif mn == 'SUBSS':
                self.x[parts[0]] = np.float32(self.x[parts[0]] - self.xv(parts[1]))
            elif mn == 'XORPS':
                if parts[0] == parts[1]:
                    self.x[parts[0]] = np.float32(0)
                else:
                    self.unknown.add('XORPS different')
            elif mn == 'MOV':
                d, s = parts
                if d in REGS:
                    self.r[d] = self.val(s)
                elif d.startswith('byte ptr'):
                    pass
                else:
                    self.m[self.ea(d)] = self.val(s)
            elif mn == 'LEA':
                self.r[parts[0]] = self.ea(parts[1])
            elif mn == 'ADD' and parts[0] in REGS:
                self.r[parts[0]] = (self.r[parts[0]] + self.val(parts[1])) & 0xFFFFFFFF
            elif mn == 'SUB' and parts[0] in REGS:
                self.r[parts[0]] = (self.r[parts[0]] - self.val(parts[1])) & 0xFFFFFFFF
            elif mn in ('CMP', 'TEST', 'NOP', 'PUSH', 'POP', 'JNZ', 'JZ', 'JMP', 'JLE', 'JGE', 'JL', 'JG'):
                pass
            else:
                self.unknown.add(mn)
        return self
