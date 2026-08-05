"""Fast tests for the interface-art compiler lane (crop -> shipped PNG).

The lane must cut retail atlas pixels verbatim (rulebook S4: no resampling, no
restyling) and emit a machine-readable index the Godot ContentDB can consume.
"""

from __future__ import annotations

import json
from pathlib import Path

import pytest

from openbfme_importer.interface_art import (
    PACK_INDEX_RELATIVE,
    PACK_INDEX_SCHEMA,
    judge_interface_art,
    pack_image_filename,
)
from openbfme_importer.interface_art_lane import (
    InterfaceArtLaneError,
    emit_interface_art,
    plan_interface_art,
)

from importer.tests.test_interface_art import (  # noqa: F401  (fixture reuse)
    REFERENCED_IDS,
    oracle,
)


def test_plan_groups_crops_by_atlas_and_names_every_gap(oracle: Path) -> None:
    plan = plan_interface_art(oracle)
    assert len(plan.atlases) == 1
    atlas = plan.atlases[0]
    assert atlas.texture == "SCUserInterface_001.tga"
    assert [crop.image_id for crop in atlas.crops] == [
        "BGBarracks",
        "BPGBarracks",
        "Ruler-Right End",
        "UIGondorSoldier",
    ]
    # The fourth reference names an atlas retail never compiled: named, not lost.
    assert [(gap.image_id, gap.reason) for gap in plan.gaps] == [
        ("UPGondorSoldier", "retail-atlas-absent")
    ]
    assert plan.scope == "all"


def test_plan_crop_rectangles_come_straight_from_the_mapped_image(
    oracle: Path,
) -> None:
    plan = plan_interface_art(oracle)
    crops = {crop.image_id: crop for crop in plan.atlases[0].crops}
    assert crops["BGBarracks"].rectangle == (0, 0, 64, 64)
    assert crops["BPGBarracks"].rectangle == (64, 0, 64, 64)
    assert crops["UIGondorSoldier"].rectangle == (128, 0, 64, 64)


def test_emit_writes_exact_pixels_and_a_consumable_index(
    oracle: Path, tmp_path: Path
) -> None:
    from PIL import Image

    out = tmp_path / "scratch-pack"
    result = emit_interface_art(plan_interface_art(oracle), oracle, out)
    assert result.written_count == 4

    manifest = json.loads((out / PACK_INDEX_RELATIVE).read_text(encoding="utf-8"))
    assert manifest["schema"] == PACK_INDEX_SCHEMA
    assert sorted(manifest["images"]) == [
        "BGBarracks",
        "BPGBarracks",
        "Ruler-Right End",
        "UIGondorSoldier",
    ]
    assert [row["id"] for row in manifest["gaps"]] == ["UPGondorSoldier"]

    relative = manifest["images"]["BPGBarracks"]
    assert relative.startswith("assets/ui/interface-art/")
    assert relative.endswith(pack_image_filename("BPGBarracks"))

    source = oracle / "art/compiledtextures/sc/scuserinterface_001.dds"
    with Image.open(source) as atlas, Image.open(out / relative) as cropped:
        expected = atlas.convert("RGBA").crop((64, 0, 128, 64))
        assert cropped.size == (64, 64)
        # Pixel-for-pixel identity: the lane crops, it never resamples.
        assert list(cropped.convert("RGBA").getdata()) == list(expected.getdata())


def test_emit_is_idempotent_and_deterministic(oracle: Path, tmp_path: Path) -> None:
    plan = plan_interface_art(oracle)
    first = emit_interface_art(plan, oracle, tmp_path / "a")
    second = emit_interface_art(plan, oracle, tmp_path / "b")
    assert first.written_count == second.written_count
    for image_id, relative in first.index.items():
        assert (
            (tmp_path / "a" / relative).read_bytes()
            == (tmp_path / "b" / second.index[image_id]).read_bytes()
        )


def test_emitted_pack_satisfies_the_judge(oracle: Path, tmp_path: Path) -> None:
    out = tmp_path / "scratch-pack"
    emit_interface_art(plan_interface_art(oracle), oracle, out)
    report = judge_interface_art(
        oracle_root=oracle,
        selection_path=None,
        extra_pack_dirs=(out,),
        source_null_allowlist=("UPGondorSoldier",),
    )
    assert report.unresolved_count == 0
    assert report.exit_code == 0


def test_lane_refuses_a_crop_outside_the_atlas(oracle: Path, tmp_path: Path) -> None:
    from PIL import Image

    # Shrink the atlas so the authored crop no longer fits: the lane must fail
    # closed rather than emit a padded or clamped image.
    small = oracle / "art/compiledtextures/sc/scuserinterface_001.dds"
    Image.new("RGBA", (32, 32)).save(small, format="PNG")
    with pytest.raises(InterfaceArtLaneError, match="outside"):
        emit_interface_art(plan_interface_art(oracle), oracle, tmp_path / "out")
