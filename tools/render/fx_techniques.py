"""List the techniques of a retail compiled effect (fx_2_0, Shaders.big shaders\\compiled\\*.fxo) and the shader blobs each one runs.

Lane WATER-LOD (stop S-035). Layout as read by Wine's d3dx9 effect parser, on top of tools/render/fx_params.py (the data block starts at byte 8;
after the parameters come the techniques: per technique {name, annotation count, pass count}, per pass {name, annotation count, state count},
per state {operation, index, typedef offset, value offset}; operation 146 / 147 is VertexShader / PixelShader whose value is an object id).
After the techniques: DWORD string count, DWORD resource count, then
  * string objects {object id, size, data}: the shaders an array parameter holds (PSBumpSpecular_Array = [no shadow, shadow]) among the
    names and values; a shader is recognised by its version token;
  * resources {technique, pass, array element (0xFFFFFFFF = none), state (0 vertex shader, 1 pixel shader), flag, size, data}: a technique's
    vertex shader, and its pixel shader: a shader of its own, or (flag 2) the preshader that selects an element of a pixel shader array by
    NumShadows, whose data starts with the array parameter's name.
A blob's number is its position in tools/render/d3d9_disasm.py's list (shaders found by scanning for version tokens); the ps_1_1 pixel shaders of
the fixed-function techniques carry no constant table and are not in that list (None here).

usage: python3 tools/render/fx_techniques.py <file.fxo>
"""
from __future__ import annotations

import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import d3d9_disasm  # noqa: E402

VERTEX_SHADER = 146
PIXEL_SHADER = 147
NO_ELEMENT = 0xFFFFFFFF


class Effect:
    def __init__(self, data: bytes):
        tag, off = struct.unpack_from("<II", data, 0)
        if tag != 0xFEFF0901:
            raise ValueError("not an fx_2_0 effect")
        self.data = data
        self.base = 8
        self.root = off
        self.blob_starts = [start for start, _ in d3d9_disasm.blobs(data)]
        self._parse()

    def u32(self, o: int) -> int:
        return struct.unpack_from("<I", self.data, self.base + o)[0]

    def string(self, o: int) -> str:
        if o == 0:
            return ""
        n = self.u32(o)
        return self.data[self.base + o + 4:self.base + o + 4 + n].split(b"\0")[0].decode("latin1")

    def _parse(self) -> None:
        pc, tc = self.u32(self.root), self.u32(self.root + 4)
        p = self.root + 16
        self.shader_arrays: dict[str, list[int]] = {}  # parameter name -> object ids
        for _ in range(pc):
            tdef, val, _flags, nann = (self.u32(p + 4 * k) for k in range(4))
            p += 16 + 8 * nann
            ptype, _pclass, name, _sem, elems = (self.u32(tdef + 4 * k) for k in range(5))
            if ptype == 15 and elems:  # an array of pixel shaders
                self.shader_arrays[self.string(name)] = [self.u32(val + 4 * k) for k in range(elems)]
        self.techniques: list[str] = []
        for _ in range(tc):
            name, nann, npass = self.u32(p), self.u32(p + 4), self.u32(p + 8)
            p += 12 + 8 * nann
            self.techniques.append(self.string(name))
            for _q in range(npass):
                _pname, pnann, nstates = self.u32(p), self.u32(p + 4), self.u32(p + 8)
                p += 12 + 8 * pnann + 16 * nstates
        nstrings, nresources = self.u32(p), self.u32(p + 4)
        p += 8
        self.objects: dict[int, int] = {}  # object id -> file offset of its data
        for _ in range(nstrings):
            oid, size = self.u32(p), self.u32(p + 4)
            self.objects[oid] = self.base + p + 8
            p += 8 + ((size + 3) & ~3)
        self.resources: dict[tuple[int, int], tuple[int, int, int]] = {}  # (technique, state) -> (flag, file offset, size)
        for _ in range(nresources):
            tech, _pass, element, state, flag, size = (self.u32(p + 4 * k) for k in range(6))
            if tech != 0xFFFFFFFF and element == NO_ELEMENT:
                self.resources[(tech, state)] = (flag, self.base + p + 24, size)
            p += 24 + ((size + 3) & ~3)

    def blob_index(self, file_offset: int) -> int | None:
        """The shader at a file offset as a d3d9_disasm blob number (the scan finds a blob at its comment token, at most 16 bytes in)."""
        for k, start in enumerate(self.blob_starts):
            if 0 <= start - file_offset <= 16:
                return k
        return None

    def shaders(self, technique: str) -> dict:
        """{'vs': blob, 'ps': [blobs] (one, or the array's [no shadow, shadow]), 'selector': array parameter name or None}."""
        tech = self.techniques.index(technique)
        out: dict = {"vs": None, "ps": [], "selector": None}
        if (tech, 0) in self.resources:
            out["vs"] = self.blob_index(self.resources[(tech, 0)][1])
        flag, off, size = self.resources.get((tech, 1), (0, 0, 0))
        if flag == 2:  # an array selector: the preshader starts with the array parameter's name
            n = struct.unpack_from("<I", self.data, off)[0]
            name = self.data[off + 4:off + 4 + n].split(b"\0")[0].decode("latin1")
            out["selector"] = name
            out["ps"] = [self.blob_index(self.objects[i]) for i in self.shader_arrays[name]]
        elif size:
            out["ps"] = [self.blob_index(off)]
        return out


def main() -> int:
    fx = Effect(open(sys.argv[1], "rb").read())
    for name in fx.techniques:
        print(name, fx.shaders(name))
    return 0


if __name__ == "__main__":
    sys.exit(main())
