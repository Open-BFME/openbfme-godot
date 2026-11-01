"""Retail evidence for stop S-1833: RotWK's path follower keeps the earlier leg of a path that doubles back along one line.

    python -m pytest tools/retail_oracle/test_path_follower_oracle.py -rs

RW 0x765598 (Path::updateClosestSegment, BFME2 decomp AI/PathClosestPoint.cpp): thiscall (const Coord3D *pos, bool opt), ret 8. The Path keeps
its head at + 4, its tail at + 8, the selected node at + 0x10 and the fraction at + 0x14; a PathNode its next at + 0, its next optimised node at
+ 8 and its position at + 0xC. On success it hands the position to TheAI's pathfinder (TheAI RW 0xDE4B40, + 0x10; the setter RW 0x6E8926 copies
12 bytes to + 0x4C), which a zeroed stand-in takes.

The legs and positions are the Rivendell campaign's starting horde as the engine captured them (MAP GOOD Rivendell, bfme2camp1 tests, with the
path search's target rule on and the S-1833 rule switched off): a patch spliced into its path runs down x = 695 from y 1135 to y 1125 and back up
to y 1145, and the horde flipped between the two positions below every frame. Retail picks the same leg and fraction as the engine's Faithful
follower: on this path retail's follower keeps the earlier leg; the Uber rule takes the later one (engine/tests/test_pathfind_path.cpp pins both
on the same legs, the fractions bit for bit). Whether retail builds this path at all is a separate question (RW 0x767B0E frees a finished
path's raw node chain, which the port keeps: docs/UBER.md, UBER-PATH).
SKIPS loudly when retail_oracle.exe is not built or RotWK's game.dat is absent (ROTWK_INSTALL).
"""
from __future__ import annotations

import struct
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent))

import helpers  # noqa: E402
from oracle import Oracle, find_host, install_dir  # noqa: E402

RW_DIR = install_dir("ROTWK_INSTALL", r"F:\RotWK")

if find_host() is None:
    pytest.skip("SKIPPED LOUDLY: tools/retail_oracle/build/retail_oracle.exe is not built (run tools/retail_oracle/build.bat)", allow_module_level=True)

helpers.require_fresh("retail_oracle")

needs_rw = pytest.mark.skipif(RW_DIR is None, reason="SKIPPED LOUDLY: RotWK game.dat not found (set ROTWK_INSTALL)")

RW_CLOSEST_SEGMENT = 0x765598
RW_THE_AI = 0xDE4B40
NODE_SIZE = 0x30

# the captured optimised chain around the turn (z 300 throughout)
LEGS = [(685.0, 1145.0), (695.0, 1135.0), (695.0, 1125.0), (695.0, 1145.0), (705.0, 1155.0)]
DOWN_LEG, UP_LEG = 1, 2  # (695, 1135) -> (695, 1125), then (695, 1125) -> (695, 1145)


def f32_bits(v: float) -> int:
    return struct.unpack("<I", struct.pack("<f", v))[0]


@pytest.fixture(scope="module")
def rw():
    with Oracle("rw", RW_DIR / "game.dat") as o:
        pathfinder = o.alloc(0x100)
        the_ai = o.alloc(0x40)
        o.poke32(the_ai + 0x10, pathfinder)
        o.poke32(RW_THE_AI, the_ai)
        yield o


def closest_segment(o: Oracle, x: float, y: float) -> tuple[int, int]:
    """Retail's choice for a fresh path over LEGS: (index of the selected node, the fraction's bits)."""
    base = o.alloc(NODE_SIZE * len(LEGS))
    nodes = [base + NODE_SIZE * k for k in range(len(LEGS))]
    for k, (nx, ny) in enumerate(LEGS):
        nxt = nodes[k + 1] if k + 1 < len(LEGS) else 0
        o.poke(nodes[k], struct.pack("<III", nxt, nodes[k - 1] if k else 0, nxt) + struct.pack("<fff", nx, ny, 300.0))
    path = o.alloc(0x20)
    o.poke(path, struct.pack("<IIIIII", 0, nodes[0], nodes[-1], 1, 0, 0))
    pos = o.alloc(12)
    o.poke(pos, struct.pack("<fff", x, y, 300.0))
    r = o.call(RW_CLOSEST_SEGMENT, "thiscall", (path, pos, 1))
    assert r.eax & 0xFF == 1 and r.callee_pop == 8
    return nodes.index(o.peek32(path + 0x10)), o.peek32(path + 0x14)


@needs_rw
def test_a_point_on_one_leg_only_selects_that_leg(rw):
    # (700, 1150) lies on the last leg (695, 1145) -> (705, 1155) at its middle and on no other: the harness reads the layout as retail does
    assert closest_segment(rw, 700.0, 1150.0) == (3, f32_bits(0.5))


@needs_rw
@pytest.mark.parametrize("y, fraction_bits", [(1131.218994140625, 0x3EC19666), (1129.781005859375, 0x3F059B33)])
def test_on_the_doubled_back_line_retail_keeps_the_earlier_leg(rw, y, fraction_bits):
    # both legs are at distance 0 from (695, y); retail keeps the down leg (a later leg must be strictly closer), whose fraction the engine
    # without the S-1833 rule computed bit for bit (0.3781 and 0.5219: (1135 - y) / 10)
    assert closest_segment(rw, 695.0, y) == (DOWN_LEG, fraction_bits)
