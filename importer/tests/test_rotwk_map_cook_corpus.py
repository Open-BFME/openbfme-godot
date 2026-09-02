"""Offline tests for tools/rotwk_map_cook_corpus.py helpers."""

from __future__ import annotations

import importlib.util
import json
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
TOOL = ROOT / "tools" / "rotwk_map_cook_corpus.py"


def _load_tool():
    spec = importlib.util.spec_from_file_location("rotwk_map_cook_corpus", TOOL)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def test_map_slug_keeps_kind_tokens_to_avoid_collisions() -> None:
    mod = _load_tool()
    assert (
        mod._map_slug("maps/map mp fords of isen ii/map mp fords of isen ii.map")
        == "mp-fords-of-isen-ii"
    )
    assert mod._map_slug("maps/map wor rivendell/map wor rivendell.map") == "wor-rivendell"
    assert (
        mod._map_slug("maps/map mp harlindon/map mp harlindon.map") == "mp-harlindon"
    )
    assert (
        mod._map_slug("maps/map wor harlindon/map wor harlindon.map") == "wor-harlindon"
    )
    assert mod._map_slug("maps/map mp harlindon/map mp harlindon.map") != mod._map_slug(
        "maps/map wor harlindon/map wor harlindon.map"
    )


def test_starts_share_component_detects_connected_and_split(tmp_path: Path) -> None:
    mod = _load_tool()
    width, height = 8, 8
    # all walkable
    raw = bytearray((width + 7) // 8 * height)
    terrain = {
        "height": {"width": width, "height": height, "horizontalScale": 10.0},
        "passability": {
            "path": "impassability.bit",
            "rowStrideBytes": (width + 7) // 8,
        },
    }
    waypoints = {
        "playerStarts": {
            "Player_1_Start": {
                "playerIndex": 1,
                "sagePosition": [15.0, 15.0, 0.0],
            },
            "Player_2_Start": {
                "playerIndex": 2,
                "sagePosition": [55.0, 55.0, 0.0],
            },
        }
    }
    cooked = tmp_path / "cooked"
    cooked.mkdir()
    (cooked / "terrain.json").write_text(json.dumps(terrain), encoding="utf-8")
    (cooked / "waypoints.json").write_text(json.dumps(waypoints), encoding="utf-8")
    (cooked / "impassability.bit").write_bytes(bytes(raw))
    assert mod._starts_share_component(cooked) is True

    # Two rooms: x=0..2 walkable, x=5..7 walkable, wall x=3..4
    raw2 = bytearray(len(raw))
    stride = (width + 7) // 8
    for y in range(height):
        for x in range(width):
            if 3 <= x <= 4:
                raw2[y * stride + (x // 8)] |= 1 << (x % 8)
    (cooked / "impassability.bit").write_bytes(bytes(raw2))
    assert mod._starts_share_component(cooked) is False


def test_tool_module_exports_main() -> None:
    mod = _load_tool()
    assert callable(mod.main)


def test_constrain_output_rejects_public_paths(tmp_path: Path) -> None:
    mod = _load_tool()
    state = tmp_path / "retail-work"
    reports = state / "reports"
    reports.mkdir(parents=True)
    ok = mod._constrain_output(reports / "out.json", state)
    assert ok == (reports / "out.json").resolve()
    with pytest.raises(SystemExit):
        mod._constrain_output(tmp_path / "public.json", state)


def test_persistent_cook_cache_matches_source_and_replaces_atomically(tmp_path: Path) -> None:
    mod = _load_tool()
    staged = tmp_path / "staged"
    staged.mkdir()
    for name in ("terrain.json", "objects.json", "waypoints.json"):
        (staged / name).write_text("{}", encoding="utf-8")
    (staged / "map.json").write_text(
        json.dumps(
            {
                "source": {"sha256": "a" * 64},
                "terrain": "terrain.json",
                "objects": "objects.json",
                "waypoints": "waypoints.json",
            }
        ),
        encoding="utf-8",
    )
    destination = tmp_path / "cache" / "map-one"

    mod._publish_cached_cook(staged, destination)

    assert mod._cached_cook_matches(destination, "a" * 64)
    assert not mod._cached_cook_matches(destination, "b" * 64)
    assert not list(destination.parent.glob(".map-one.*"))


def test_report_contract_records_full_denominator() -> None:
    source = TOOL.read_text(encoding="utf-8")
    assert '"eligibleMapCount": eligible_count' in source
    assert '"requestedLimit": args.limit or None' in source


def test_fatal_and_nonfatal_verdict_sets_disjoint() -> None:
    mod = _load_tool()
    assert mod.FATAL_VERDICTS.isdisjoint(mod.NONFATAL_VERDICTS)
    assert "cook-error" in mod.FATAL_VERDICTS
    assert "cooked-and-connected" in mod.NONFATAL_VERDICTS
