"""Fast tests for publishing the interface-art lane through the pack pipeline.

The lane could always plan and cut retail icons; nothing ever published them,
so no mounted pack carried ``data/interface-art/index.json`` and the Godot
consumer had nothing to load.  These tests pin the wiring: every planned crop
becomes a pipeline resource, the index it ships addresses exactly those
outputs, and anything the cook catalog cannot resolve is named as a gap rather
than dropped.
"""

from __future__ import annotations

import json
from pathlib import Path

import pytest

from openbfme_importer.interface_art import PACK_INDEX_RELATIVE, PACK_INDEX_SCHEMA
from openbfme_importer.interface_art_lane import (
    AtlasPlan,
    CropPlan,
    LaneGap,
    LanePlan,
    plan_interface_art,
)
from openbfme_importer.interface_art_pack import (
    CATALOG_ABSENT_REASON,
    INTERFACE_ART_PACK_KEY,
    INTERFACE_ART_RUNTIME_PATH,
    MAX_CROPS_PER_RESOURCE,
    compile_interface_art_pack,
)
from openbfme_importer.profile import ImportProfile

from importer.tests.test_interface_art import (  # noqa: F401  (fixture reuse)
    oracle,
)


def test_runtime_path_is_the_path_the_godot_consumer_reads() -> None:
    assert INTERFACE_ART_RUNTIME_PATH == PACK_INDEX_RELATIVE == (
        "data/interface-art/index.json"
    )
    assert INTERFACE_ART_PACK_KEY == "interfaceArt"


def test_every_planned_crop_becomes_a_pipeline_resource(oracle: Path) -> None:
    plan = plan_interface_art(oracle)
    compiled = compile_interface_art_pack(plan)
    assert len(compiled.resources) == 1
    resource = compiled.resources[0]
    assert resource["converter"] == "texture-atlas-crops"
    assert resource["patterns"] == ["art/compiledtextures/sc/scuserinterface_001.dds"]
    assert resource["output"] == plan.atlases[0].output_directory
    assert [crop["crop"] for crop in resource["options"]["crops"]] == [
        [0, 0, 64, 64],
        [64, 0, 64, 64],
        [192, 0, 64, 64],
        [128, 0, 64, 64],
    ]


def test_index_addresses_exactly_the_outputs_the_converter_writes(
    oracle: Path,
) -> None:
    plan = plan_interface_art(oracle)
    compiled = compile_interface_art_pack(plan)
    written = {
        f"{compiled.resources[0]['output']}/{crop['output']}"
        for crop in compiled.resources[0]["options"]["crops"]
    }
    assert set(compiled.document["images"].values()) == written
    assert compiled.document["schema"] == PACK_INDEX_SCHEMA
    assert compiled.document["scope"] == "all"
    # Retail's own source gap survives into the shipped index, by name.
    assert {
        row["id"]: row["reason"] for row in compiled.document["gaps"]
    } == {"UPGondorSoldier": "retail-atlas-absent"}


def test_space_bearing_retail_ids_get_a_schema_safe_logical_name(
    oracle: Path,
) -> None:
    # MappedImage "Ruler-Right End" is real; profile.TEXTURE_ATLAS_LOGICAL_NAME
    # forbids the space, so the lane's slug filename is the logical name too.
    plan = plan_interface_art(oracle)
    compiled = compile_interface_art_pack(plan)
    logical = {crop["logicalName"] for crop in compiled.resources[0]["options"]["crops"]}
    assert "ruler-right-end-2b74d3f5" in logical
    assert "Ruler-Right End" in compiled.document["images"]


def test_atlas_absent_from_the_cook_catalog_becomes_a_named_gap(
    oracle: Path,
) -> None:
    plan = plan_interface_art(oracle)
    compiled = compile_interface_art_pack(plan, catalog_has=lambda path: False)
    assert compiled.resources == ()
    assert compiled.document["images"] == {}
    reasons = {row["id"]: row["reason"] for row in compiled.document["gaps"]}
    assert reasons["BGBarracks"] == CATALOG_ABSENT_REASON
    assert reasons["UPGondorSoldier"] == "retail-atlas-absent"
    assert compiled.receipt["catalogAbsentAtlases"] == [
        "art/compiledtextures/sc/scuserinterface_001.dds"
    ]


def test_dense_atlas_is_split_to_respect_the_profile_crop_bound() -> None:
    crops = tuple(
        CropPlan(
            image_id=f"Image{index:03d}",
            output_name=f"image{index:03d}-0000000{index % 10}.png",
            left=index,
            top=0,
            width=8,
            height=8,
        )
        for index in range(MAX_CROPS_PER_RESOURCE + 5)
    )
    plan = LanePlan(
        scope="all",
        oracle_root="oracle",
        atlases=(
            AtlasPlan(
                texture="Dense.tga",
                source_relative="art/compiledtextures/de/dense.dds",
                digest="abcdef012345",
                output_directory="assets/ui/interface-art/abcdef012345",
                crops=crops,
            ),
        ),
        gaps=(LaneGap("Nothing", "no-mapped-image", ()),),
    )
    compiled = compile_interface_art_pack(plan)
    assert len(compiled.resources) == 2
    assert [len(row["options"]["crops"]) for row in compiled.resources] == [
        MAX_CROPS_PER_RESOURCE,
        5,
    ]
    assert len(compiled.document["images"]) == len(crops)


def test_emitted_resources_are_valid_to_the_import_profile_schema(
    oracle: Path, tmp_path: Path
) -> None:
    plan = plan_interface_art(oracle)
    compiled = compile_interface_art_pack(plan)
    profile_path = tmp_path / "profile.json"
    profile_path.write_text(
        json.dumps(
            {
                "format": 1,
                "id": "interface-art-test",
                "title": "interface-art-test",
                "pack": {
                    "id": "test-vslice",
                    "version": "test",
                    "files": {INTERFACE_ART_PACK_KEY: INTERFACE_ART_RUNTIME_PATH},
                },
                "resources": list(compiled.resources),
                "runtime_data": {INTERFACE_ART_RUNTIME_PATH: compiled.document},
            }
        ),
        encoding="utf-8",
    )
    loaded = ImportProfile.load(profile_path)
    assert INTERFACE_ART_RUNTIME_PATH in loaded.runtime_data


def test_create_a_hero_model_portraits_are_collected(tmp_path: Path) -> None:
    # CPUruk and its siblings are authored as PortraitImageName /
    # ButtonImageName inside CreateAHeroModels.inc -- an .inc body fragment
    # under data/ini/object/ that the object sweep cannot parse and whose
    # field spellings no other collector knows. The compiled CaH table
    # publishes them, so the index has to answer them.
    from openbfme_importer.interface_art import collect_create_a_hero_images

    root = tmp_path / "effective-assets"
    models = root / "data/ini/object/createahero/createaheromodels.inc"
    models.parent.mkdir(parents=True, exist_ok=True)
    models.write_text(
        "DefaultModelConditionState\n"
        "\tModel                = CHSS_UK_U_SKN\n"
        "\tPortraitImageName    = CPUruk\n"
        "\tButtonImageName      = HICAHUrukHai\n"
        "End\n",
        encoding="cp1252",
    )
    (root / "data/ini").mkdir(parents=True, exist_ok=True)
    references = collect_create_a_hero_images(root)
    assert {reference.image_id for reference in references} == {
        "CPUruk",
        "HICAHUrukHai",
    }
    assert {reference.field for reference in references} == {
        "PortraitImageName",
        "ButtonImageName",
    }


def test_create_a_hero_award_medals_are_collected(tmp_path: Path) -> None:
    from openbfme_importer.interface_art import collect_create_a_hero_images

    root = tmp_path / "effective-assets"
    awards = root / "data/ini/awardsystem.ini"
    awards.parent.mkdir(parents=True, exist_ok=True)
    awards.write_text(
        "AwardSystem\nObjectAward\nAwardName = Vanquisher\n"
        "ImageName = CahAward_Vanquisher\nEnd\nEnd\n",
        encoding="cp1252",
    )
    references = collect_create_a_hero_images(root)
    assert [(row.field, row.image_id) for row in references] == [
        ("ImageName", "CahAward_Vanquisher")
    ]
