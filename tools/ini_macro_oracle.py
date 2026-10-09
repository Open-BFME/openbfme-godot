#!/usr/bin/env python3
"""Independent oracle for the INI macro layer (spec ini-and-object-model.md 2.2-2.4).

Reads every *.ini / *.inc under a retail INI data root (the pure RotWK 2.01 + BFME2 1.06
`data` folder), extracts every column-0 `#define`, and writes two golden tables that the C++
tests compare against. It shares no code with the engine and works in a different form: macro expansion splices tokens
into a stream, and every real arithmetic step is executed on REAL x87 hardware at PC24 /
round-to-nearest followed by `fstp dword`, through the 32-bit helper in tools/x87_oracle
(plain float32 / numpy arithmetic is NOT equivalent: subnormal results double-round).

  --math-out   NAME<TAB>0xBITS     float32 bits of every math macro (value starts with '#'),
                                   evaluated as scanReal would (operands may be macros / groups)
  --mixed-out  TOKEN<TAB>MACRO<TAB>VALUE
                                   every distinct non-numeric token outside #define lines that
                                   differs from a macro name only by case (the retail names are
                                   resolved case-insensitively by full name, RW 0x42BC44)

Usage:
  python tools/ini_macro_oracle.py --data-root <dir> --math-out engine/tests/data/ini_math_macros.tsv \
      --mixed-out engine/tests/data/ini_mixed_case_refs.tsv
"""
import argparse
import os
import re
import struct
import subprocess
import sys
from collections import deque

NUM = re.compile(r'^\s*[+-]?(\d+\.?\d*([eE][+-]?\d+)?|\.\d+([eE][+-]?\d+)?)')


def strip_comment(line):
    for sep in (';', '//'):
        k = line.find(sep)
        if k >= 0:
            line = line[:k]
    return line


def collect(root):
    files = []
    for dp, _, fn in os.walk(root):
        for f in fn:
            if f.lower().endswith(('.ini', '.inc')):
                files.append(os.path.join(dp, f))
    files.sort()
    macros = {}   # lower name -> (name, value)
    lines = []    # non-define, comment-stripped lines
    for p in files:
        for raw in open(p, 'rb').read().decode('latin-1').replace('\r', '\n').split('\n'):
            code = strip_comment(raw)
            if code.startswith('#define'):
                t = code.split()
                if len(t) >= 3:
                    key = t[1].lower()
                    if key in macros:
                        sys.exit('duplicate macro name %s (%s)' % (t[1], p))
                    macros[key] = (t[1], ' '.join(t[2:]))
            elif code.strip():
                lines.append(code)
    return macros, lines


class X87:
    """The 32-bit hardware helper: one request per line, one answer per line."""

    def __init__(self, exe):
        self.p = subprocess.Popen([exe], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, bufsize=1)
        cw = self.ask('cw')
        if cw != '007f':
            sys.exit('x87 helper control word is %s, expected 007f (PC24, round to nearest)' % cw)

    def ask(self, line):
        self.p.stdin.write(line + '\n')
        self.p.stdin.flush()
        return self.p.stdout.readline().strip()

    def op(self, name, a, b):
        r = self.ask('f32 %s %08x %08x' % (name, a, b))
        if r == 'ERR' or not r:
            sys.exit('x87 helper rejected %s' % name)
        return int(r, 16)


def f32_bits(x):
    return struct.unpack('<I', struct.pack('<f', x))[0]


class Evaluator:
    """Values are float32 bit patterns; every real step is an x87 PC24 operation + fstp dword."""

    def __init__(self, macros, x87):
        self.macros = macros
        self.x87 = x87

    def number(self, text):
        m = NUM.match(text)
        if not m:
            raise ValueError('not a number: %r' % text)
        return f32_bits(float(m.group(0)))

    def scan(self, tok, stream):
        if not tok[0].isdigit() and tok.lower() in self.macros:
            value = self.macros[tok.lower()][1]
            if value.startswith('#'):
                stream.extendleft(reversed(value.split()))
                return self.expr(stream.popleft(), stream)
            return self.number(value)
        if tok.startswith('#'):
            return self.expr(tok, stream)
        return self.number(tok)

    def expr(self, op, stream):
        a = self.scan(stream.popleft(), stream)
        if op in ('#ADD(', '#MULTIPLY('):
            acc = a
            while True:
                t = stream.popleft()
                if t == ')':
                    return acc
                acc = self.x87.op('add' if op == '#ADD(' else 'mul', acc, self.scan(t, stream))
        if op in ('#SUBTRACT(', '#DIVIDE('):
            b = self.scan(stream.popleft(), stream)
            acc = self.x87.op('sub' if op == '#SUBTRACT(' else 'div', a, b)
            if stream.popleft() != ')':
                raise ValueError(op + ' takes only 2 operands')
            return acc
        raise ValueError('unknown operator ' + op)

    def macro(self, name):
        value = self.macros[name.lower()][1]
        stream = deque(value.split())
        return self.expr(stream.popleft(), stream)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--data-root', required=True)
    ap.add_argument('--x87', required=True, help='path of the built 32-bit helper (tools/x87_oracle/build.bat)')
    ap.add_argument('--math-out', required=True)
    ap.add_argument('--mixed-out', required=True)
    args = ap.parse_args()

    macros, lines = collect(args.data_root)
    ev = Evaluator(macros, X87(args.x87))

    rows = []
    for key, (name, value) in sorted(macros.items()):
        if value.startswith('#'):
            rows.append('%s\t0x%08X' % (name, ev.macro(name)))
    with open(args.math_out, 'w', newline='\n') as f:
        f.write('\n'.join(rows) + '\n')

    mixed = {}
    for code in lines:
        for tok in re.split(r'[ \t=]+', code.strip()):
            if tok and not tok[0].isdigit():
                m = macros.get(tok.lower())
                if m and m[0] != tok:
                    mixed[tok] = m
    with open(args.mixed_out, 'w', newline='\n') as f:
        for tok in sorted(mixed):
            f.write('%s\t%s\t%s\n' % (tok, mixed[tok][0], mixed[tok][1]))
    print('macros %d, math macros %d, mixed-case tokens %d' % (len(macros), len(rows), len(mixed)))


if __name__ == '__main__':
    main()
