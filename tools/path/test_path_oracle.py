"""Self-tests of the PATH-1 grid oracle on hand-built maps (no install needed). Every expected value is derived by hand from the rules
in path_oracle.classify, not computed by it: run  python -m pytest tools/path -q"""
import os
import sys

os.environ.setdefault("ROTWK_INSTALL", "unused")
os.environ.setdefault("BFME2_INSTALL", "unused")
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import path_oracle as po  # noqa: E402

WADE, DEEP = 5.0, 6.0


def flat_map(cells=10, extent=12):
    m = po.MapData()
    m.width = m.height = extent
    m.border = 0
    m.boundaries = [(cells, cells)]
    m.heights = tuple([0] * (extent * extent))
    m.cliff_stride = (extent + 7) // 8
    m.cliff = bytearray(m.cliff_stride * extent)
    return m


def set_cliff(m, x, y):
    m.cliff[y * m.cliff_stride + (x >> 3)] |= 1 << (x & 7)


def square(cx, cy, r=1.0):
    return [(cx - r, cy - r), (cx + r, cy - r), (cx + r, cy + r), (cx - r, cy + r)]


def run(m):
    types, pinched, b18, b21 = po.classify(m, WADE, DEEP)
    return types, pinched, po.counts(types, pinched, b18, b21)


def test_flat_map_is_all_clear():
    types, pinched, c = run(flat_map())
    assert c["clear"] == 100 and c["cliff"] == 0 and c["pinched"] == 0
    assert (c["width"], c["height"]) == (10, 10)


def test_a_cliff_cell_turns_its_eight_clear_neighbours_into_pinched_cliff():
    m = flat_map()
    set_cliff(m, 5, 5)
    types, pinched, c = run(m)
    # the 3 x 3 block around (5, 5) is cliff afterwards; the eight neighbours were clear when the pass ran, so they are the pinched ones
    assert c["cliff"] == 9 and c["clear"] == 91 and c["pinched"] == 8
    assert types[5][5] == po.CLIFF and types[4][4] == po.CLIFF and types[6][6] == po.CLIFF
    assert types[3][5] == po.CLEAR and types[7][5] == po.CLEAR


def test_the_pinch_pass_is_a_single_dilation_not_a_flood():
    m = flat_map()
    set_cliff(m, 5, 5)
    types, _pinched, _c = run(m)
    # (3, 5) is two cells away: the new cliff cells do not pinch their own neighbours
    assert types[3][5] == po.CLEAR and types[5][7] == po.CLEAR


def test_a_cliff_in_the_corner_pinches_three_cells():
    m = flat_map()
    set_cliff(m, 0, 0)
    _types, _pinched, c = run(m)
    assert c["cliff"] == 4 and c["pinched"] == 3 and c["clear"] == 96


def test_water_depth_thresholds_are_strict():
    for z, expect in ((5.0, po.CLEAR), (5.5, po.WATER), (6.0, po.WATER), (6.5, po.DEEP_WATER)):
        m = flat_map()
        m.standing = [([(-100.0, -100.0), (1000.0, -100.0), (1000.0, 1000.0), (-100.0, 1000.0)], z)]
        types, _pinched, c = run(m)
        assert all(types[i][j] == expect for i in range(10) for j in range(10)), (z, expect)
        assert c["width"] == 10


def test_the_last_corner_that_passes_a_test_wins():
    m = flat_map()
    # cell (2, 2) has the corners (20, 20), (20, 30), (30, 30), (30, 20) in that order: deep water at the first, shallow at the third
    m.standing = [(square(20.0, 20.0), 6.5), (square(30.0, 30.0), 5.5)]
    types, _pinched, _c = run(m)
    assert types[2][2] == po.WATER      # the shallow corner is tested last and overwrites the deep one
    assert types[1][1] == po.DEEP_WATER  # (20, 20) is the (x1, y1) corner of cell (1, 1) and only the deep polygon touches it
    assert types[3][3] == po.WATER       # (30, 30) is the TL corner of cell (3, 3)


def test_a_cliff_cell_is_not_tested_for_water():
    m = flat_map()
    set_cliff(m, 5, 5)
    m.standing = [([(-100.0, -100.0), (1000.0, -100.0), (1000.0, 1000.0), (-100.0, 1000.0)], 6.5)]
    types, _pinched, _c = run(m)
    assert types[5][5] == po.CLIFF


def test_plane_index_truncates_clamps_and_adds_the_border():
    m = flat_map(extent=12)
    assert po.plane_index(m, 25.0, 39.9) == (2, 3)
    assert po.plane_index(m, 10000.0, 10000.0) == (10, 10)   # clamped to extent - 2
    assert po.plane_index(m, -50.0, -50.0) == (0, 0)
    m.border = 2
    assert po.plane_index(m, 25.0, 39.9) == (4, 5)


def test_ground_height_follows_the_sw_ne_split_in_float32():
    m = flat_map(extent=12)
    hs = list(m.heights)
    for iy in range(12):
        for ix in range(12):
            hs[ix + iy * 12] = 256 * ix   # 10 units per cell in x
    m.heights = tuple(hs)
    assert po.ground_height(m, 25.0, 25.0) == 25.0
    assert po.ground_height(m, 30.0, 55.0) == 30.0
    # the edge rows take the clamped sample with no interpolation: x = 5 is below ix 1 (index 0, height 0)
    assert po.ground_height(m, 5.0, 25.0) == 0.0


def test_the_extra_planes_are_read_by_the_same_index():
    m = flat_map()
    stride = (m.width + 7) // 8
    m.planes["a"] = bytearray(stride * m.height)
    m.planes["extra"] = bytearray(stride * m.height)
    m.planes["a"][3 * stride + 0] |= 1 << 2      # cell (2, 3)
    m.planes["extra"][7 * stride + 1] |= 1 << 1  # cell (9, 7)
    types, pinched, b18, b21 = po.classify(m, WADE, DEEP)
    c = po.counts(types, pinched, b18, b21)
    assert c["impassable_to_players"] == 1 and b18[2][3]
    assert c["extra_pass"] == 1 and b21[9][7]


def test_the_retail_point_test_counts_the_top_edge_and_the_right_border_but_not_the_bottom():
    """RW 0x70E911 (lane PATH-2): an edge counts for ay < y <= by and a point ON the edge (lhs == rhs) is inside; the even-odd rule the first port used
    took the opposite half-open interval and a strict x test."""
    sq = [(0.0, 0.0), (10.0, 0.0), (10.0, 10.0), (0.0, 10.0)]
    assert po.point_in_polygon(sq, 5.0, 5.0)
    # the top edge y = 10 is inside, the bottom edge y = 0 is not
    assert po.point_in_polygon(sq, 5.0, 10.0)
    assert not po.point_in_polygon(sq, 5.0, 0.0)
    # the right border x = 10 is inside (the left edge lies left of the point and is skipped, the right edge counts: lhs == rhs); the left border x = 0 is
    # outside (both vertical edges count there: two toggles)
    assert po.point_in_polygon(sq, 10.0, 5.0)
    assert not po.point_in_polygon(sq, 0.0, 5.0)
    # outside the box: rejected before the edges
    assert not po.point_in_polygon(sq, 10.5, 5.0)
    assert not po.point_in_polygon(sq, 5.0, 10.5)
    # a two-point "polygon" counts its one edge twice: never inside
    assert not po.point_in_polygon([(0.0, 0.0), (10.0, 10.0)], 0.0, 5.0)
    assert not po.point_in_polygon([], 0.0, 0.0)
