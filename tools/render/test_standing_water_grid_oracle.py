"""tools/render/standing_water_grid_oracle.py reproduces the numbers engine/tests pins for the standing-water grid
(WaterGeometry::buildStandingGrid, RW 0x482A16 / 0x70E911). Run: python -m pytest tools/render/test_standing_water_grid_oracle.py -q
The Adorn River case reads the polygon from the install and is skipped unless ROTWK_INSTALL and BFME2_INSTALL are set."""
from __future__ import annotations

import os
import sys

import pytest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import standing_water_grid_oracle as oracle  # noqa: E402


def test_bridge_polygon_fills_both_lobes_and_not_the_bridge():
    # engine/tests/test_map_render.cpp "a polygon that touches itself fills as RW 0x482A16 fills it"
    for outline in (oracle.BRIDGE, oracle.BRIDGE[::-1]):
        g = oracle.grid(outline)
        assert g["spacing"] == 20
        assert len(g["cells"]) == 50
        assert g["vertices"] == 72
        assert g["indices"] == 300


def test_bridge_polygon_is_what_the_removed_ear_clipper_could_not_finish():
    ears, left, stuck = oracle.ear_clip_residue([(float(x), float(y)) for x, y in oracle.BRIDGE])
    assert (ears, left, stuck) == (8, 4, True)


def test_cell_centres_on_the_edges_follow_the_half_open_rule():
    # engine/tests/test_map_render.cpp "a cell centre on the polygon's edge follows RW 0x70E911's half-open crossing rule"
    g = oracle.grid(oracle.EDGE_SQUARE)
    assert g["cells"] == [(1, 1), (2, 1), (1, 2), (2, 2)]
    assert g["vertices"] == 9
    assert g["indices"] == 24
    assert g["corner_points"][0] == (20.0, 20.0) and g["corner_points"][-1] == (60.0, 60.0)


def test_large_polygon_doubles_the_spacing_and_negative_origins_truncate_toward_zero():
    # the pre-existing pins of test_map_render.cpp's PCA grid case
    big = oracle.grid([(0, 0), (4000, 0), (4000, 6000), (0, 6000)])
    assert big["spacing"] == 40
    assert big["vertices"] == 15251
    assert big["indices"] == 90000
    negative = oracle.grid([(-39, -39), (1, -39), (1, 1), (-39, 1)])
    assert negative["origin"] == (-20.0, -20.0)


def test_low_game_lod_selects_the_40_unit_grid():
    # RW 0x482A16: the LOD manager's field at +0x1784 <= 2 picks 40.0f spacing and a 30000 cell limit
    assert oracle.grid(oracle.BRIDGE, low_lod=True)["spacing"] == 40


@pytest.mark.skipif(not (os.environ.get("ROTWK_INSTALL") and os.environ.get("BFME2_INSTALL")),
                    reason="needs ROTWK_INSTALL and BFME2_INSTALL (the polygon is read from the install, never stored)")
def test_adorn_river_polygon_from_the_install():
    # engine/tests/test_map_corpus.cpp "Adorn River's one standing-water polygon ..."
    points = oracle.adorn_river_points()
    assert len(points) == 97
    assert len(oracle.revisited_vertices(points)) == 10
    assert oracle.ear_clip_residue(points) == (75, 22, True)
    g = oracle.grid(points)
    assert g["spacing"] == 20
    assert len(g["cells"]) == 3288
    assert g["vertices"] == 3758
    assert g["indices"] == 19728
