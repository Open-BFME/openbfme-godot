"""Minimal Direct3D 9 shader bytecode disassembler for the retail compiled effects (Shaders.big shaders\\compiled\\*.fxo).

Lane RENDER-1. Used to read how the retail effects light objects (stop S-023): every shader blob inside an fx_2_0 effect
starts with a version token (vs 0xFFFExxxx / ps 0xFFFFxxxx) and ends with D3DSIO_END (0x0000FFFF); its CTAB comment names
the constant registers. Token layout per the public D3D9 shader bytecode documentation (d3d9types.h D3DSHADER_* masks).
Reads only the user's files; nothing retail is stored.

usage: python3 tools/render/d3d9_disasm.py <file.fxo> [blob index ...]
"""
from __future__ import annotations

import struct
import sys

OPS = {0: "nop", 1: "mov", 2: "add", 3: "sub", 4: "mad", 5: "mul", 6: "rcp", 7: "rsq", 8: "dp3", 9: "dp4", 10: "min", 11: "max",
       12: "slt", 13: "sge", 14: "exp", 15: "log", 16: "lit", 17: "dst", 18: "lrp", 19: "frc", 20: "m4x4", 21: "m4x3", 22: "m3x4",
       23: "m3x3", 24: "m3x2", 25: "call", 26: "callnz", 27: "loop", 28: "ret", 29: "endloop", 30: "label", 31: "dcl", 32: "pow",
       33: "crs", 34: "sgn", 35: "abs", 36: "nrm", 37: "sincos", 38: "rep", 39: "endrep", 40: "if", 41: "ifc", 42: "else",
       43: "endif", 44: "break", 45: "breakc", 46: "mova", 47: "defb", 48: "defi", 64: "texcoord", 65: "texkill", 66: "tex",
       67: "texbem", 68: "texbeml", 69: "texreg2ar", 70: "texreg2gb", 71: "texm3x2pad", 72: "texm3x2tex", 73: "texm3x3pad",
       74: "texm3x3tex", 76: "texm3x3spec", 77: "texm3x3vspec", 78: "expp", 79: "logp", 80: "cnd", 81: "def", 82: "texreg2rgb",
       83: "texdp3tex", 84: "texm3x2depth", 85: "texdp3", 86: "texm3x3", 87: "texdepth", 88: "cmp", 89: "bem", 90: "dp2add",
       91: "dsx", 92: "dsy", 93: "texldd", 94: "setp", 95: "texldl", 96: "breakp", 0xFFFD: "phase"}
SM1_ARGS = {0: 0, 1: 2, 2: 3, 3: 3, 4: 4, 5: 3, 6: 2, 7: 2, 8: 3, 9: 3, 10: 3, 11: 3, 12: 3, 13: 3, 14: 2, 15: 2, 16: 2, 17: 3,
            18: 4, 19: 2, 20: 3, 21: 3, 22: 3, 23: 3, 24: 3, 31: 2, 64: 1, 65: 1, 66: 1, 67: 2, 68: 2, 69: 2, 70: 2, 71: 2, 72: 2,
            73: 2, 74: 2, 76: 3, 77: 2, 78: 2, 79: 2, 80: 4, 81: 5, 82: 2, 83: 2, 84: 2, 85: 2, 86: 2, 87: 1, 88: 4, 89: 3,
            0xFFFD: 0}
REG = {0: "r", 1: "v", 2: "c", 3: "a", 4: "oPos", 5: "oD", 6: "oT", 7: "i", 8: "oC", 9: "oDepth", 10: "s", 14: "b", 15: "aL",
       17: "vMisc", 19: "p"}
USAGE = ["position", "blendweight", "blendindices", "normal", "psize", "texcoord", "tangent", "binormal", "tessfactor",
         "positiont", "color", "fog", "depth", "sample"]
SRCMOD = ["{}", "-{}", "{}_bias", "-{}_bias", "{}_bx2", "-{}_bx2", "1-{}", "{}_x2", "-{}_x2", "{}_dz", "{}_dw", "abs({})",
          "-abs({})", "!{}"]


def regname(tok: int, ps: bool, major: int) -> str:
    t = ((tok >> 28) & 7) | ((tok >> 8) & 0x18)
    n = tok & 0x7FF
    if t == 3 and ps:
        return f"t{n}"
    if t == 6 and major >= 3:
        return f"o{n}"
    if t == 4:
        return ["oPos", "oFog", "oPts"][n] if n < 3 else f"rast{n}"
    return f"{REG.get(t, f'?{t}_')}{n}"


def dst(tok: int, ps: bool, major: int) -> str:
    s = regname(tok, ps, major)
    mask = (tok >> 16) & 0xF
    if mask != 0xF:
        s += "." + "".join(c for i, c in enumerate("xyzw") if mask & (1 << i))
    mod = (tok >> 20) & 0xF
    return s, ("_sat" if mod & 1 else "") + ("_pp" if mod & 2 else "")


def src(tok: int, ps: bool, major: int, rel: str = "") -> str:
    s = regname(tok, ps, major) + rel
    sw = (tok >> 16) & 0xFF
    comps = "".join("xyzw"[(sw >> (2 * i)) & 3] for i in range(4))
    if comps != "xyzw":
        s += "." + (comps[0] if comps == comps[0] * 4 else comps)
    mod = (tok >> 24) & 0xF
    return SRCMOD[mod].format(s) if mod < len(SRCMOD) else s


def ctab(words: list[int]) -> dict:
    raw = struct.pack(f"<{len(words)}I", *words)
    if raw[:4] != b"CTAB":
        return {}
    b = raw[4:]
    n, info = struct.unpack_from("<II", b, 12)
    out = {}
    for i in range(n):
        name_off, rset, ridx, rcnt, _, _, _ = struct.unpack_from("<IHHHHII", b, info + 20 * i)
        name = b[name_off:b.index(b"\0", name_off)].decode("latin1")
        out[(rset, ridx)] = (name, rcnt)
    return out


PRES_OPS = {0x100: "mov", 0x101: "neg", 0x103: "rcp", 0x104: "frc", 0x105: "exp", 0x106: "log", 0x107: "rsq", 0x108: "sin",
            0x109: "cos", 0x10A: "asin", 0x10B: "acos", 0x10C: "atan", 0x200: "min", 0x201: "max", 0x202: "lt", 0x203: "ge",
            0x204: "add", 0x205: "mul", 0x206: "atan2", 0x208: "div", 0x300: "cmp", 0x301: "movc", 0x500: "dot", 0x502: "noise",
            0x700: "dotswiz6", 0x701: "dotswiz8"}


def preshader(words: list[int]) -> list[str]:
    """Decode an effect preshader (comment 'PRES': version 0x4658xxxx, then CTAB / CLIT / FXLC comments). Layout as read
    by Wine's d3dx9 preshader parser: FXLC = instruction count, then per instruction {opcode << 20 | scalar flag | ncomp,
    input count, (input count + 1) operands of {relative flag, table, offset}}; tables 1 literal (CLIT doubles),
    2 input constant (CTAB names), 4 output shader constant, 7 temporary; offsets count float components."""
    i = 2  # 'PRES', version 0x4658xxxx
    names = {}
    lits = []
    code = []
    while i < len(words):
        tok = words[i]
        if (tok & 0xFFFF) != 0xFFFE:
            break
        n = (tok >> 16) & 0x7FFF
        body = words[i + 1:i + 1 + n]
        raw = struct.pack(f"<{len(body)}I", *body)
        tag = raw[:4]
        if tag == b"CTAB":
            for (rset, ridx), (nm, cnt) in ctab(body).items():
                for k in range(cnt):
                    names[ridx + k] = nm + (f"[{k}]" if cnt > 1 else "")
        elif tag == b"CLIT":
            cnt = body[1]
            lits = list(struct.unpack_from(f"<{cnt}d", raw, 8))
        elif tag == b"FXLC":
            code = body[1:]
        i += 1 + n

    def operand(table: int, off: int, ncomp: int) -> str:
        comps = "".join("xyzw"[(off + k) & 3] for k in range(ncomp))
        if table == 1:
            return "(" + ", ".join(f"{lits[off + k]:.6g}" for k in range(ncomp)) + ")"
        if table == 2:
            return f"{names.get(off >> 2, f'in{off >> 2}')}.{comps}"
        if table == 4:
            return f"c{off >> 2}.{comps}"
        if table == 7:
            return f"t{off >> 2}.{comps}"
        return f"?{table}:{off}"

    out = []
    if not code:
        return out
    count = code[0]
    j = 1
    for _ in range(count):
        ins = code[j]
        nin = code[j + 1]
        j += 2
        op = (ins >> 20) & 0x7FF
        ncomp = ins & 0xFFFF
        scalar = (ins >> 31) & 1
        ops = []
        for k in range(nin + 1):
            rel, table, off = code[j], code[j + 1], code[j + 2]
            j += 3
            if rel:
                j += 2
            n = ncomp
            if scalar and k == 0 and nin > 1:
                n = 1
            if PRES_OPS.get(op) == "dot" and k < nin:
                n = ncomp
            ops.append(operand(table, off, n if k < nin else (1 if PRES_OPS.get(op) == "dot" else ncomp)))
        out.append(f"  pre {PRES_OPS.get(op, hex(op))} {ops[-1]}, " + ", ".join(ops[:-1]))
    return out


def disasm(words: list[int], start: int) -> tuple[list[str], int]:
    ver = words[start]
    ps = (ver >> 16) == 0xFFFF
    major = (ver >> 8) & 0xFF
    lines = [("ps" if ps else "vs") + f"_{major}_{ver & 0xFF}"]
    i = start + 1
    consts = {}
    while i < len(words):
        tok = words[i]
        op = tok & 0xFFFF
        if op == 0xFFFF:
            i += 1
            break
        if op == 0xFFFE:
            n = (tok >> 16) & 0x7FFF
            body = words[i + 1:i + 1 + n]
            consts.update(ctab(body))
            if body and body[0] == 0x53455250:  # 'PRES'
                lines.extend(preshader(body))
            i += 1 + n
            continue
        if major >= 2:
            n = (tok >> 24) & 0xF
        else:
            # SM1 has no length field: the parameter count of the opcode
            n = SM1_ARGS.get(op)
            if n is None:
                raise ValueError(f"unknown SM1 opcode {op} at word {i}")
        args = words[i + 1:i + 1 + n]
        name = OPS.get(op, f"op{op}")
        if op == 31:
            d, _ = dst(args[1], ps, major)
            t = ((args[1] >> 28) & 7) | ((args[1] >> 8) & 0x18)
            if t == 10:
                tt = {2: "2d", 3: "cube", 4: "volume"}.get((args[0] >> 27) & 0xF, "?")
                lines.append(f"dcl_{tt} {d}")
            else:
                u = args[0] & 0x1F
                lines.append(f"dcl_{USAGE[u] if u < len(USAGE) else u}{(args[0] >> 16) & 0xF} {d}")
        elif op == 81:
            d, _ = dst(args[0], ps, major)
            f = struct.unpack("<4f", struct.pack("<4I", *args[1:5]))
            lines.append(f"def {d}, " + ", ".join(f"{x:.6g}" for x in f))
        elif op in (47, 48):
            d, _ = dst(args[0], ps, major)
            lines.append(f"{name} {d}, " + ", ".join(str(struct.unpack('<i', struct.pack('<I', x))[0]) for x in args[1:]))
        else:
            parts = []
            j = 0
            first = True
            mods = ""
            has_dst = op not in (25, 26, 27, 28, 29, 30, 38, 39, 40, 41, 42, 43, 44, 45, 65, 96) or op == 65
            while j < len(args):
                a = args[j]
                rel = ""
                if (a >> 13) & 1 and major >= 2 and j + 1 < len(args):
                    rel = "[" + src(args[j + 1], ps, major) + "]"
                    j += 1
                if first and has_dst and op not in (40, 41, 26, 27, 38, 45):
                    d, mods = dst(a, ps, major)
                    parts.append(d + rel)
                else:
                    parts.append(src(a, ps, major, rel))
                first = False
                j += 1
            if op == 66 and major >= 2:
                name = "texld"
            lines.append(f"{name}{mods} " + ", ".join(parts))
        i += 1 + n
    names = []
    for (rset, ridx), (nm, cnt) in sorted(consts.items()):
        names.append(f"// {['b', 'i', 'c', 's'][rset]}{ridx}" + (f"..{ridx + cnt - 1}" if cnt > 1 else "") + f" = {nm}")
    return lines[:1] + names + lines[1:], i


def blobs(data: bytes) -> list[tuple[int, list[str]]]:
    words = list(struct.unpack(f"<{len(data) // 4}I", data[:len(data) // 4 * 4]))
    out = []
    i = 0
    while i < len(words):
        w = words[i]
        if (w >> 16) in (0xFFFE, 0xFFFF) and ((w >> 8) & 0xFF) in (1, 2, 3) and (w & 0xFF) <= 4 and i + 1 < len(words) \
                and (words[i + 1] & 0xFFFF) == 0xFFFE:
            lines, end = disasm(words, i)
            out.append((i * 4, lines))
            i = end
        else:
            i += 1
    return out


def main() -> int:
    data = open(sys.argv[1], "rb").read()
    want = {int(x) for x in sys.argv[2:]}
    for k, (off, lines) in enumerate(blobs(data)):
        if want and k not in want:
            continue
        print(f"==== blob {k} @0x{off:x}")
        print("\n".join(lines))
    return 0


if __name__ == "__main__":
    sys.exit(main())
