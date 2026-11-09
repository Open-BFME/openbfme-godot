"""Retail evidence for lane PATH-REBUILD: RotWK finishes a found path by rebuilding it from its optimised nodes and freeing the raw chain.

    python -m pytest tools/retail_oracle/test_path_rebuild_oracle.py -rs

RW 0x767B0E (Path::rebuild, BFME2 decomp Path::rva00365E98 in Pathfinder/rva00365E98Rva00365E98Farm.cpp): thiscall (Object *, const Coord3D *facing,
int surfaces, bool blocked), ret 0x10. The Path keeps its head at + 4, its tail at + 8 and its optimised flag at + 0xC; a PathNode (0x30 bytes) its
next at + 0, its previous at + 4, its next optimised node at + 8, its position at + 0xC, its layer at + 0x18, canOptimize at + 0x1C and its
waypoint id at + 0x20. The Object's AI is read at + 0x260 and the AI's current locomotor at + 0x1F0 (RW 0x68B31D): a zeroed stand-in has no
locomotor, so the rebuild takes its plain branch (turn 0, segment speed 5): the head copied (RW 0x766603), every corner between two plain nodes
rounded by an arc of radius 5 (RW 0x766718, the points appended by RW 0x665429), the tail copied, then every node of the original chain freed
(RW 0x6652A8). The node pool's allocation (RW 0x663365, the pool at RW 0xDE78DC that startup would build) is redirected to the host allocator
and the free to a log, so the test sees which nodes retail frees.

Reference: refs.path_rebuild_plain, the transcription of the decomp the engine's Path::rebuild follows (engine/tests/test_pathfind_path.cpp pins
the engine on the same chains). The arc points carry the trigonometry of each side (the CRT's here, SimMath's in the engine: stop S-167), so
they are compared with a tolerance; the copies and the counts are exact.
SKIPS loudly when retail_oracle.exe is not built or RotWK's game.dat is absent (ROTWK_INSTALL).
"""
from __future__ import annotations

import math
import struct
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent))

import helpers  # noqa: E402
import refs  # noqa: E402
from oracle import Oracle, find_host, install_dir  # noqa: E402

RW_DIR = install_dir("ROTWK_INSTALL", r"F:\RotWK")

if find_host() is None:
    pytest.skip("SKIPPED LOUDLY: tools/retail_oracle/build/retail_oracle.exe is not built (run tools/retail_oracle/build.bat)", allow_module_level=True)

helpers.require_fresh("retail_oracle")

needs_rw = pytest.mark.skipif(RW_DIR is None, reason="SKIPPED LOUDLY: RotWK game.dat not found (set ROTWK_INSTALL)")

RW_REBUILD = 0x767B0E
RW_ARC_SEGMENTS = 0x765E00  # cdecl (float angle, float radius) -> int: the arc's segment count
RW_ROUND_CORNER = 0x766718  # thiscall (PathNode *prev, PathNode *cur, PathNode *next, float radius): the corner's arc appended to the Path
RW_POOL_ALLOCATE = 0x663365  # thiscall on the node pool, no arguments: a 0x30-byte node
RW_FREE_POOLED_NODE = 0x6652A8  # cdecl (void *)
NODE_SIZE = 0x30
NO_WAYPOINT = 0x7FFFFFFF
MAX_FREED = 64


class Rebuild:
    """RW 0x767B0E over a hand-laid path, with the pool allocation on the host and the frees logged."""

    def __init__(self, o: Oracle):
        self.o = o
        self.obj = o.alloc(0x300)  # a zeroed Object: + 0x260 (its AI) null, so no locomotor entry
        self.allocs = o.alloc(4)
        self.freed = o.alloc(4 + 4 * MAX_FREED)
        alloc = o.hostfn("alloc")
        # push 0x30; mov eax, alloc; call eax; add esp, 4; inc dword [allocs]; ret
        shim = b"\x6A\x30" + b"\xB8" + struct.pack("<I", alloc) + b"\xFF\xD0" + b"\x83\xC4\x04" + b"\xFF\x05" + struct.pack("<I", self.allocs) + b"\xC3"
        self._patch(RW_POOL_ALLOCATE, o.code(shim))
        # mov eax, [esp + 4]; mov ecx, [freed]; mov [freed + 4 + ecx * 4], eax; inc dword [freed]; ret
        log = (b"\x8B\x44\x24\x04" + b"\x8B\x0D" + struct.pack("<I", self.freed) + b"\x89\x04\x8D" + struct.pack("<I", self.freed + 4) + b"\xFF\x05" +
               struct.pack("<I", self.freed) + b"\xC3")
        self._patch(RW_FREE_POOLED_NODE, o.code(log))

    def _patch(self, va: int, stub: int) -> None:
        self.o.poke(va, b"\xE9" + struct.pack("<I", (stub - (va + 5)) & 0xFFFFFFFF))

    def lay(self, cells, anchors):
        """A path of `cells` (raw chain, in order) whose optimised links join the `anchors` (indices into cells), as the search leaves it."""
        o = self.o
        base = o.alloc(NODE_SIZE * len(cells))
        nodes = [base + NODE_SIZE * k for k in range(len(cells))]
        for k, (x, y, z) in enumerate(cells):
            nxt = nodes[k + 1] if k + 1 < len(cells) else 0
            prv = nodes[k - 1] if k else 0
            opti = 0
            if k in anchors:
                i = anchors.index(k)
                opti = nodes[anchors[i + 1]] if i + 1 < len(anchors) else 0
            o.poke(nodes[k], struct.pack("<III", nxt, prv, opti) + struct.pack("<fff", x, y, z) + struct.pack("<IBxxxI", 1, 0, NO_WAYPOINT))
        path = o.alloc(0x28)
        o.poke(path, struct.pack("<IIIIII", 0, nodes[0], nodes[-1], 1, 0, 0))
        return path, nodes

    def run(self, path: int, facing=(1.0, 0.0, 0.0), surfaces=0xFFFF):
        o = self.o
        o.poke32(self.allocs, 0)
        o.poke32(self.freed, 0)
        dir_ = o.alloc(12)
        o.poke(dir_, struct.pack("<fff", *facing))
        r = o.call(RW_REBUILD, "thiscall", (path, self.obj, dir_, surfaces, 0))
        assert r.callee_pop == 0x10
        return r

    def chain(self, path: int):
        """The rebuilt chain: (position, layer, waypoint id, next == nextOptimized) per node, head to tail; the head and tail pointers checked."""
        o = self.o
        out = []
        node = o.peek32(path + 4)
        last = 0
        while node:
            raw = o.peek(node, NODE_SIZE)
            nxt, prv, opti = struct.unpack_from("<III", raw, 0)
            pos = struct.unpack_from("<fff", raw, 0xC)
            layer, way = struct.unpack_from("<I", raw, 0x18)[0], struct.unpack_from("<I", raw, 0x20)[0]
            assert prv == last
            out.append((pos, layer, way, nxt == opti))
            last, node = node, nxt
        assert o.peek32(path + 8) == last
        assert o.peek(path + 0xC, 1) == b"\x01"
        return out

    def freed_nodes(self):
        n = self.o.peek32(self.freed)
        return [self.o.peek32(self.freed + 4 + 4 * k) for k in range(n)]


@pytest.fixture(scope="module")
def rw():
    with Oracle("rw", RW_DIR / "game.dat") as o:
        yield Rebuild(o)


def near(got, want, tolerance):
    return all(abs(g - w) <= tolerance for g, w in zip(got, want))


@needs_rw
def test_a_corner_path_is_rebuilt_from_its_optimised_nodes_and_the_raw_chain_freed(rw):
    # 17 cells east then north, the anchors the optimiser leaves: the head, the corner, the tail
    cells = [(5.0 + 10.0 * k, 5.0, 0.0) for k in range(9)] + [(85.0, 15.0 + 10.0 * k, 0.0) for k in range(8)]
    path, nodes = rw.lay(cells, [0, 8, 16])
    rw.run(path)
    got = rw.chain(path)
    want = refs.path_rebuild_plain([(5.0, 5.0, 0.0), (85.0, 5.0, 0.0), (85.0, 85.0, 0.0)])
    assert len(got) == len(want) == 5
    assert got[0][0] == (5.0, 5.0, 0.0) and got[-1][0] == (85.0, 85.0, 0.0)  # the copies, bit for bit
    for (pos, layer, way, linked), w in zip(got, want):
        assert near(pos, w, 0.001), (pos, w)  # tolerance: the arc points' trigonometry (S-167)
        assert layer == 1 and way == NO_WAYPOINT and linked  # raw == optimised on every node
    # every original node, and nothing else, went back to the pool, head to tail; the new nodes came from the allocator
    assert rw.freed_nodes() == nodes
    assert rw.o.peek32(rw.allocs) == 5


@needs_rw
def test_two_corners_the_second_rounded_from_where_the_first_arc_moved_its_node(rw):
    anchors = [(85.0, 145.0, 300.0), (155.0, 145.0, 300.0), (155.0, 215.0, 300.0), (225.0, 215.0, 300.0)]
    path, nodes = rw.lay(anchors, [0, 1, 2, 3])  # the anchors alone as the raw chain too (a chain the engine's rebuild would also produce)
    rw.run(path)
    got = rw.chain(path)
    want = refs.path_rebuild_plain(anchors)
    assert len(got) == len(want) == 7
    for (pos, _layer, _way, linked), w in zip(got, want):
        assert near(pos, w, 0.001), (pos, w)  # tolerance: the arc points' trigonometry (S-167)
        assert linked
    assert rw.freed_nodes() == nodes


@needs_rw
def test_a_shallow_corner_and_a_waypoint_node_are_copied_as_they_are(rw):
    # 0.197 rad: the radius grows to 8.75 but the arc is one segment, so the node is copied
    path, nodes = rw.lay([(5.0, 5.0, 0.0), (105.0, 5.0, 0.0), (205.0, 25.0, 0.0)], [0, 1, 2])
    rw.run(path)
    got = rw.chain(path)
    assert [g[0] for g in got] == [(5.0, 5.0, 0.0), (105.0, 5.0, 0.0), (205.0, 25.0, 0.0)]
    assert rw.freed_nodes() == nodes
    # a right-angle corner that carries a waypoint id (a portal) is copied with its id and not rounded
    path, nodes = rw.lay([(5.0, 5.0, 0.0), (85.0, 5.0, 0.0), (85.0, 85.0, 0.0)], [0, 1, 2])
    rw.o.poke32(nodes[1] + 0x20, 7)
    rw.run(path)
    got = rw.chain(path)
    assert [g[0] for g in got] == [(5.0, 5.0, 0.0), (85.0, 5.0, 0.0), (85.0, 85.0, 0.0)]
    assert [g[2] for g in got] == [NO_WAYPOINT, 7, NO_WAYPOINT]
    assert rw.freed_nodes() == nodes


@needs_rw
@pytest.mark.parametrize("corner,radius", [
    ([(5.0, 5.0, 0.0), (85.0, 5.0, 0.0), (85.0, 85.0, 0.0)], 10.0),   # a left turn, 3 segments
    ([(5.0, 5.0, 0.0), (85.0, 5.0, 0.0), (85.0, 85.0, 0.0)], 20.0),   # a left turn, 6 segments
    ([(5.0, 85.0, 0.0), (85.0, 85.0, 0.0), (85.0, 5.0, 0.0)], 10.0),  # a right turn, 3 segments
    ([(5.0, 85.0, 0.0), (85.0, 85.0, 0.0), (85.0, 5.0, 0.0)], 20.0),  # a right turn, 6 segments
])
def test_the_corner_arc_matches_the_transcription_at_radii_with_interior_points(rw, corner, radius):
    # RW 0x766718 on three standalone nodes and an empty Path: the plain rebuild only ever draws radius 5 (two segments: a start, an end), so this is
    # where retail's rotation direction and the interior points of a longer arc (a locomotor's radius) are held to the transcription
    o = rw.o
    base = o.alloc(NODE_SIZE * 3)
    nodes = [base + NODE_SIZE * k for k in range(3)]
    for k, (x, y, z) in enumerate(corner):
        o.poke(nodes[k], struct.pack("<III", 0, 0, 0) + struct.pack("<fff", x, y, z) + struct.pack("<IBxxxI", 1, 0, NO_WAYPOINT))
    path = o.alloc(0x28)
    o.poke(path, struct.pack("<IIIIII", 0, 0, 0, 1, 0, 0))
    o.poke32(rw.allocs, 0)
    r = o.call(RW_ROUND_CORNER, "thiscall", (path, nodes[0], nodes[1], nodes[2], struct.unpack("<I", struct.pack("<f", radius))[0]))
    assert r.callee_pop == 0x10
    got = rw.chain(path)
    want = []
    end = refs._round_corner(want, corner[0], corner[1], corner[2], radius)
    assert end is not None
    count = refs.path_arc_segments(1.5707963705062866, radius)
    assert count >= 3  # an arc with interior points
    # a left turn appends the start, count - 1 interior points and the end; a right turn's last interior point is the end itself (RW 0x665429 drops
    # a node at the tail's x / y on an optimised path), so it has one node fewer: the transcription's count is the pin
    assert len(got) == len(want) >= count
    for (pos, layer, way, linked), w in zip(got, want):
        assert near(pos, w, 0.001)  # tolerance: the arc's trigonometry on each side (S-167); the count and the order are exact
        assert layer == 1 and way == NO_WAYPOINT and linked
    assert o.peek32(rw.allocs) == len(want)


@needs_rw
def test_the_arc_segment_count_is_the_x87_pc24_product(rw):
    # RW 0x765E00 multiplies at precision control 24 (each product a float): refs.path_arc_segments must agree on every angle and radius a corner or
    # a bend can ask for, including the ones a double accumulator gets wrong (a right angle at radius 15, 30, 60: RW 4, 8, 16, double 5, 9, 17)
    o = rw.o
    f = lambda v: struct.unpack("<I", struct.pack("<f", v))[0]
    angles = [math.pi / 16, math.pi / 8, math.pi / 4, 3 * math.pi / 8, math.pi / 2, 5 * math.pi / 8, 3 * math.pi / 4, 7 * math.pi / 8, math.pi, 1.7278761, 0.7225663304328919]
    radii = [5.0, 8.75, 10.0, 15.0, 20.0, 25.0, 30.0, 40.0, 60.0, 120.0]
    disagreements_of_a_double = 0
    for angle in angles:
        for radius in radii:
            r = o.call(RW_ARC_SEGMENTS, "cdecl", (f(angle), f(radius)))
            assert r.eax == refs.path_arc_segments(angle, radius), (angle, radius, r.eax)
            fa, fr = struct.unpack("<f", struct.pack("<f", angle))[0], radius
            dbl = max(1, min(128, int(math.ceil((fa * (fr * refs.PATH_ARC_FACTOR0)) * refs.PATH_ARC_FACTOR1))))
            disagreements_of_a_double += dbl != r.eax
    assert disagreements_of_a_double >= 10  # the double model is wrong often, not on a tie

