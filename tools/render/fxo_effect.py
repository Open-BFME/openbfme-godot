"""Read the structure of a retail compiled effect (fx_2_0, Shaders.big shaders\\compiled\\*.fxo): parameters with their annotations, techniques with
theirs, array lengths, the pixel / vertex shader blobs, and the array-selector expressions (the FXLC preshaders that compute a shader array index from the
effect's parameters), evaluated for given parameter values.

Used by the binary-fact tests of the classic-material render lane (house colour combine, secondary texture modes). Layout as read by Wine's d3dx9 effect
parser, the same as fx_params.py: tag 0xFEFF0901, data block at byte 8, every offset relative to it; at the root offset: parameter count, technique count,
unused, object count, then the parameters {typedef offset, value offset, flags, annotation count, (typedef, value) per annotation} and the techniques
{name offset, annotation count, pass count, annotations, passes {name, annotation count, state count, annotations, states of 4 DWORDs}}. Reads only the
user's files; nothing retail is stored.
"""
from __future__ import annotations

import re
import struct
import sys
from dataclasses import dataclass, field
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import d3d9_disasm  # noqa: E402


@dataclass
class Parameter:
    name: str
    type: int
    cls: int
    elements: int
    annotations: dict = field(default_factory=dict)  # name -> int (an int annotation) or str
    value_offset: int = 0


@dataclass
class Technique:
    name: str
    annotations: dict = field(default_factory=dict)


class Effect:
    def __init__(self, data: bytes):
        tag, root = struct.unpack_from("<II", data, 0)
        if tag != 0xFEFF0901:
            raise ValueError("not an fx_2_0 effect")
        self.data = data
        self._blobs = None
        self.base = 8
        self.parameters: list[Parameter] = []
        self.techniques: list[Technique] = []
        count, tcount = self._u32(root), self._u32(root + 4)
        p = root + 16
        for _ in range(count):
            tdef, val, _flags, nann = (self._u32(p + 4 * k) for k in range(4))
            ptype, pclass, name, _sem, elems = (self._u32(tdef + 4 * k) for k in range(5))
            parameter = Parameter(self._string(name), ptype, pclass, elems, self._annotations(p + 16, nann), val)
            self.parameters.append(parameter)
            p += 16 + 8 * nann
        for _ in range(tcount):
            name, nann, npass = self._u32(p), self._u32(p + 4), self._u32(p + 8)
            self.techniques.append(Technique(self._string(name), self._annotations(p + 12, nann)))
            p += 12 + 8 * nann
            for _ in range(npass):
                pn, pa, ns = self._u32(p), self._u32(p + 4), self._u32(p + 8)
                p += 12 + 8 * pa + 16 * ns

    def _u32(self, off: int) -> int:
        return struct.unpack_from("<I", self.data, self.base + off)[0]

    def _string(self, off: int) -> str:
        if off == 0:
            return ""
        n = self._u32(off)
        return self.data[self.base + off + 4:self.base + off + 4 + n].split(b"\0")[0].decode("latin1")

    def _annotations(self, p: int, count: int) -> dict:
        out = {}
        for k in range(count):
            tdef, val = self._u32(p + 8 * k), self._u32(p + 8 * k + 4)
            ptype, _pclass, name = (self._u32(tdef + 4 * j) for j in range(3))
            out[self._string(name)] = self._string(val) if ptype == 4 else self._u32(val)
        return out

    def parameter(self, name: str) -> Parameter:
        return next(p for p in self.parameters if p.name == name)

    def technique(self, name: str) -> Technique:
        return next(t for t in self.techniques if t.name == name)

    def blobs(self) -> list[tuple[int, list[str]]]:
        """(file offset, disassembly lines) of every shader blob, in file order (decoded once)."""
        if self._blobs is None:
            self._blobs = d3d9_disasm.blobs(self.data)
        return self._blobs

    def pixel_blobs(self, version: str) -> list[int]:
        """Blob indices of the shaders of one version ('ps_2_0', 'ps_1_1', ...), in file order: an array's elements are consecutive among them."""
        return [i for i, (_o, lines) in enumerate(self.blobs()) if lines and lines[0] == version]

    def selector_expressions(self) -> dict[int, list[str]]:
        """Every array-selector preshader (a CTAB / CLIT / FXLC comment group with no PRES header, outside the shader blobs) by the file offset of its FXLC tag,
        as d3d9_disasm.preshader decodes it."""
        out = {}
        for m in re.finditer(b"FXLC", self.data):
            fx = m.start()
            ctab = self.data.rfind(b"CTAB", 0, fx)
            if ctab < 0 or fx - ctab > 0x800:
                continue
            tok = struct.unpack_from("<I", self.data, fx - 4)[0]
            if (tok & 0xFFFF) != 0xFFFE:
                continue
            end = fx - 4 + 4 + ((tok >> 16) & 0x7FFF) * 4
            raw = self.data[ctab - 4:end]
            words = [0x53455250, 0x46580000] + list(struct.unpack(f"<{len(raw) // 4}I", raw))
            lines = d3d9_disasm.preshader(words)
            if lines and lines[-1].split()[2].startswith("c0"):
                out[fx] = lines
        return out


_OPERAND = re.compile(r"\((-?[0-9.eE+-]+)\)|([A-Za-z_][A-Za-z_0-9]*)\.[xyzw]+")


def evaluate_selector(lines: list[str], values: dict[str, float]) -> int:
    """Evaluate a scalar selector preshader (the decoded lines of Effect.selector_expressions) for the given parameter values; returns the array index
    (the value of its output register c0.x)."""
    reg: dict[str, float] = {}

    def operand(text: str) -> float:
        m = _OPERAND.fullmatch(text.strip())
        if not m:
            raise ValueError(f"operand {text!r}")
        if m.group(1) is not None:
            return float(m.group(1))
        name = m.group(2)
        if re.fullmatch(r"[tc][0-9]+", name):
            return reg[name]
        return float(values[name])

    for line in lines:
        head, rest = line.strip().split(None, 2)[1:]
        dst, *srcs = [s.strip() for s in rest.split(", ")]
        a = [operand(s) for s in srcs]
        if head == "add":
            r = a[0] + a[1]
        elif head == "mul":
            r = a[0] * a[1]
        elif head == "neg":
            r = -a[0]
        elif head == "ge":
            r = 1.0 if a[0] >= a[1] else 0.0
        elif head == "lt":
            r = 1.0 if a[0] < a[1] else 0.0
        elif head == "min":
            r = min(a[0], a[1])
        elif head == "max":
            r = max(a[0], a[1])
        elif head == "mov":
            r = a[0]
        else:
            raise ValueError(f"preshader op {head!r}")
        reg[dst.split(".")[0]] = r
    return int(round(reg["c0"]))


def code(lines: list[str]) -> list[str]:
    """The instructions of a decoded blob without its version line, sampler declarations and CTAB comments."""
    return [x for x in lines[1:] if not x.startswith("dcl") and not x.startswith("//")]


def ps_array(effect: Effect) -> tuple[list[list[str]], list[int]]:
    """defaultw3d.fxo PS_Array (technique Default): its 288 ps_2_0 blobs, the last 288 ps_2_0 blobs of the file (four earlier ones serve the shadow map
    technique). Returns (decoded blobs, blob indices)."""
    blobs = effect.blobs()
    ids = effect.pixel_blobs("ps_2_0")[-288:]
    return [blobs[i][1] for i in ids], ids


def ps_l_array(effect: Effect) -> tuple[list[list[str]], list[int]]:
    """defaultw3d.fxo PS_L_Array (techniques Default_L / Default_M): its 144 ps_1_1 blobs."""
    blobs = effect.blobs()
    ids = effect.pixel_blobs("ps_1_1")
    return [blobs[i][1] for i in ids], ids


def expected_index(nt: int, stbm: int, hce: int, shroud: int, fog: int, blend: int, shadows: int, unshadowed: bool = False) -> int:
    """The array index the techniques' FXLC expressions compute (decoded with Effect.selector_expressions, pinned by the binary-fact tests): NumTextures
    NT' = NT + HCE [NT == 1], SecondaryTextureBlendMode STBM' = STBM + HCE (3 - STBM); NT' + 3 STBM' + 12 [Shroud == 2] + 24 [Fog] (1 + [Blend == 2]) +
    (72 HCE for the unshadowed technique Default_L / Default_M | 72 min(NumShadows, 1) + 144 HCE for technique Default)."""
    if nt == 1 and hce:
        nt += 1
    stbm += hce * (3 - stbm)
    index = nt + 3 * stbm + 12 * (shroud == 2) + 24 * (fog != 0) * (1 + (blend == 2))
    return index + (72 * hce if unshadowed else 72 * min(shadows, 1) + 144 * hce)


def run_pixel_shader(lines: list[str], v0, v1, t0, t1, cloud, shroud=None):
    """Execute a decoded ps_2_0 blob of defaultw3d.fxo on one pixel: v0 / v1 are the vertex colours oD0 / oD1 (r, g, b, a), t0 / t1 / cloud the texels the
    three samplers return (texld r, t0|t1|t2, s0|s1|s2 -> Sampler_0, Sampler_1, CloudSampler; a ShroudSampler-using variant is not supported). Supports the
    instructions the classic permutations without shroud, fog or shadows use (texld mov add mul mad lrp def). Returns oC0 as four floats; used by the
    binary-fact tests to compare the retail instructions with the formulas the generated shader implements."""
    reg: dict[str, list[float]] = {}
    consts: dict[str, list[float]] = {}
    samplers = {"s0": t0, "s1": t1, "s2": cloud}
    inputs = {"v0": list(v0), "v1": list(v1) + [0.0]}

    def read(token: str) -> list[float]:
        token = token.strip()
        neg = token.startswith("-")
        if neg:
            token = token[1:]
        name, _, swz = token.partition(".")
        if name in inputs:
            val = inputs[name]
        elif name in consts:
            val = consts[name]
        else:
            val = reg.get(name, [0.0, 0.0, 0.0, 0.0])
        if swz:
            idx = ["xyzw".index(c) for c in swz]
            if len(idx) == 1:
                idx = idx * 4
            val = [val[i] for i in idx] + [0.0] * (4 - len(idx))
        return [-x for x in val] if neg else list(val)

    def write(token: str, val: list[float]):
        """Store the components of `val` at the destination's masked positions (a write mask keeps the other components)."""
        name, _, mask = token.strip().partition(".")
        cur = reg.setdefault(name, [0.0, 0.0, 0.0, 0.0])
        for c in mask or "xyzw":
            cur["xyzw".index(c)] = val["xyzw".index(c)]

    for line in lines:
        line = line.strip()
        if not line or line.startswith(("//", "ps_", "dcl")):
            continue
        op, _, rest = line.partition(" ")
        args = [a.strip() for a in rest.split(", ")]
        if op == "def":
            consts[args[0]] = [float(x) for x in args[1:5]]
            continue
        if op == "texld":
            dst = args[0]
            sampler = args[2]
            write(dst, list(samplers[sampler]))
            continue
        srcs = [read(a) for a in args[1:]]
        if op == "mov":
            val = srcs[0]
        elif op == "add":
            val = [a + b for a, b in zip(*srcs)]
        elif op == "mul":
            val = [a * b for a, b in zip(*srcs)]
        elif op == "mad":
            val = [a * b + c for a, b, c in zip(*srcs)]
        elif op == "lrp":
            val = [a * b + (1 - a) * c for a, b, c in zip(*srcs)]
        else:
            raise ValueError(f"ps_2_0 op {op!r} not supported")
        write(args[0], val)
    return reg["oC0"]
