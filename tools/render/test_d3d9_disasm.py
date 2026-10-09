"""Synthetic checks of tools/render/d3d9_disasm.py (lane RENDER-1): token decoding per the public D3D9 bytecode layout."""
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import d3d9_disasm  # noqa: E402


def words(*w):
    return struct.pack(f"<{len(w)}I", *w)


def test_vs2_mov_and_saturate_mad():
    blob = words(
        0xFFFE0200,             # vs_2_0
        0x0000FFFE,             # empty comment (the blob scanner wants one after the version)
        0x02000001, 0x800F0000, 0xA0E40001,                 # mov r0, c1
        0x04000004, 0x80170001, 0x80E40000, 0xA0000002, 0xA0550002,  # mad_sat r1.xyz, r0, c2.x, c2.y
        0x0000FFFF)
    found = d3d9_disasm.blobs(blob)
    assert len(found) == 1
    off, lines = found[0]
    assert off == 0
    assert lines[0] == "vs_2_0"
    assert "mov r0, c1" in lines
    assert "mad_sat r1.xyz, r0, c2.x, c2.y" in lines


def test_ps2_texld_and_def():
    blob = words(
        0xFFFF0200, 0x0000FFFE,
        0x05000051, 0xA00F0000, 0x3F800000, 0x00000000, 0x3F000000, 0x40000000,  # def c0, 1, 0, 0.5, 2
        0x03000042, 0x800F0000, 0xB0E40000, 0xA0E40800,                          # texld r0, t0, s0
        0x0000FFFF)
    _, lines = d3d9_disasm.blobs(blob)[0]
    assert lines[0] == "ps_2_0"
    assert "def c0, 1, 0, 0.5, 2" in lines
    assert "texld r0, t0, s0" in lines
