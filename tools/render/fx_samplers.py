"""List the sampler states of a retail compiled effect (fx_2_0, the .fxo files under shaders/compiled in Shaders.big).

Lane TERRAIN-SAMPLING. Same container layout as fx_params.py. A parameter of a sampler type (D3DXPT_SAMPLER 10 .. SAMPLERCUBE 14) holds a DWORD state
count, then per state four DWORDs: the state operation, its array index, the offset of the value's typedef and the offset of the value. A sampler
operation is 164 + the D3DSAMPLERSTATETYPE (165 ADDRESSU, 166 ADDRESSV, 167 ADDRESSW, 168 BORDERCOLOR, 169 MAGFILTER, 170 MINFILTER, 171 MIPFILTER,
172 MIPMAPLODBIAS, 173 MAXMIPLEVEL, 174 MAXANISOTROPY); operation 164 itself is the sampler's Texture (the value is an object index, not a state).
Values are the Direct3D enums: D3DTEXF_NONE 0, POINT 1, LINEAR 2, ANISOTROPIC 3; D3DTADDRESS_WRAP 1, MIRROR 2, CLAMP 3.

usage: python3 tools/render/fx_samplers.py <file.fxo>
"""
from __future__ import annotations

import struct
import sys

STATE_NAMES = {165: "ADDRESSU", 166: "ADDRESSV", 167: "ADDRESSW", 168: "BORDERCOLOR", 169: "MAGFILTER", 170: "MINFILTER", 171: "MIPFILTER",
               172: "MIPMAPLODBIAS", 173: "MAXMIPLEVEL", 174: "MAXANISOTROPY"}
D3DTEXF_NONE, D3DTEXF_POINT, D3DTEXF_LINEAR, D3DTEXF_ANISOTROPIC = 0, 1, 2, 3
D3DTADDRESS_WRAP, D3DTADDRESS_MIRROR, D3DTADDRESS_CLAMP = 1, 2, 3


def samplers(d: bytes) -> dict[str, dict[str, int]]:
    """{sampler parameter name: {state name: value}} of an fx_2_0 effect (the Texture assignment, operation 164, is left out)."""
    tag, off = struct.unpack_from("<II", d, 0)
    if tag != 0xFEFF0901:
        raise ValueError("not an fx_2_0 effect")
    base = 8

    def u32(o: int) -> int:
        return struct.unpack_from("<I", d, base + o)[0]

    def string(o: int) -> str:
        n = u32(o)
        return d[base + o + 4:base + o + 4 + n].split(b"\0")[0].decode("latin1")

    out: dict[str, dict[str, int]] = {}
    count = u32(off)
    p = off + 16
    for _ in range(count):
        tdef, val, _flags, nann = u32(p), u32(p + 4), u32(p + 8), u32(p + 12)
        p += 16 + 8 * nann
        ptype = u32(tdef)
        if 10 <= ptype <= 14:
            states = {}
            for i in range(u32(val)):
                op, _idx, _typedef, value = (u32(val + 4 + 16 * i + 4 * k) for k in range(4))
                if op != 164:
                    states[STATE_NAMES.get(op, str(op))] = u32(value)
            out[string(u32(tdef + 8))] = states
    return out


def main() -> int:
    for name, states in samplers(open(sys.argv[1], "rb").read()).items():
        print(name, ", ".join(f"{k}={v}" for k, v in states.items()))
    return 0


if __name__ == "__main__":
    sys.exit(main())
