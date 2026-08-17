"""Real BFME2 1.06 regressions for scenario-only visual recipe edge forms.

These are retail-authored Objects, not synthetic approximations. The test is
skipped outside the maintainer's private oracle; no retail bytes are published.
"""

from __future__ import annotations

from pathlib import Path

import pytest

from openbfme_importer.catalog import InstallCatalog
from openbfme_importer.neutral_pack_profile import (
    compile_neutral_unit_pack_artifact,
    validate_neutral_unit_pack_artifact,
)
from openbfme_importer.playable_unit_compiler import prepare_playable_unit_compiler
from openbfme_importer.playable_unit_import import (
    _source_documents,
    compile_scenario_unit_recipe,
)
from openbfme_importer.retail_visual_closure import build_retail_visual_closure


REPO = Path(__file__).resolve().parents[2]
ASSETS = REPO / "workspace/retail-work/cache/effective-assets"
CATALOG = REPO / "workspace/retail-work/catalog/bfme2.json"

pytestmark = pytest.mark.skipif(
    not ASSETS.is_dir() or not CATALOG.is_file(),
    reason="private BFME2 1.06 effective-assets oracle is not present",
)


@pytest.fixture(scope="module")
def oracle():
    catalog = InstallCatalog.load(CATALOG)
    documents = _source_documents(ASSETS)
    prepared = prepare_playable_unit_compiler(documents)
    return catalog, documents, prepared


def _compile_real_scenario_unit(oracle, object_id: str):
    catalog, documents, prepared = oracle
    closure = build_retail_visual_closure(ASSETS, [object_id], catalog=catalog)
    descriptor, compiled_closure, recipe = compile_scenario_unit_recipe(
        catalog,
        ASSETS,
        object_id,
        game="bfme2",
        scenario_admission={
            "role": "scenario-only",
            "surfaces": [
                "map-placement",
                "script-spawn",
                "object-creation-list",
                "lair-spawn",
            ],
        },
        prebuilt_visual_closure=closure,
        prepared=prepared,
        source_documents=documents,
    )
    return descriptor, compiled_closure, recipe


EXPECTED = {
    "AragornOathbreaker1": {
        "presentations": {"death": "slow-death"},
        "default": "art/w3d/ru/ruoath3a_skn.w3d",
        "missing": {"RUOath3A_DIEA"},
    },
    "CINE_GrnDrgn_Flying": {
        "presentations": {
            "idle": "static-model",
            "move": "transform-locomotion",
            "death": "object-removal",
        },
        "default": "art/w3d/su/sumndrag_skn.w3d",
        "missing": {"WUDrogoth_FLYS"},
    },
    "IsengardMurderOfCrows": {
        "presentations": {"idle": "static-model", "death": "slow-death"},
        "default": "art/w3d/cr/crebain_skn.w3d",
        "missing": {"Crebain_SKL.Crebain_IDLA"},
    },
    "RohanOathbreaker1": {
        "presentations": {"death": "slow-death"},
        "default": "art/w3d/ru/ruoath3a_skn.w3d",
        "missing": {"RUOath3A_DIEA"},
    },
    "WildBatCloud": {
        "presentations": {"move": "transform-locomotion"},
        "default": "art/w3d/ba/bats_skn.w3d",
        "missing": {"Bats_SKLBats_SMNABats_MOV"},
    },
    "RohanOathbreakersCavalry": {
        "presentations": {"attack": "weapon-effect"},
        "default": "art/w3d/ru/ruothhrse_skn.w3d",
        "missing": set(),
    },
    "CreateAHeroFamiliar_Base": {
        "presentations": {"death": "slow-death"},
        "default": "art/w3d/cr/crebain_skn.w3d",
        "missing": set(),
    },
    "MordorBalrog": {
        "presentations": {},
        "default": "art/w3d/mu/mubalrog_skn.w3d",
        "missing": {
            "MUBalrog_SKL.MUBalrog_BRNA",
            "MUBalrog_SKL.MUBalrog_GRBA",
            "MUBalrog_SKL.MUBalrog_GRBC",
        },
    },
}


@pytest.mark.parametrize("object_id", EXPECTED)
def test_real_neutral_visual_recipe_closes_exact_retail_form(
    oracle, object_id: str
) -> None:
    _, compiled_closure, recipe = _compile_real_scenario_unit(oracle, object_id)
    expected = EXPECTED[object_id]
    visual = recipe["runtimeRegistration"]["visual"]
    presentations = {
        state: row["binding"]
        for state, row in visual.get("corePresentations", {}).items()
    }
    defaults = [
        row["sourceW3d"] for row in visual["components"] if row["default"] is True
    ]
    unresolved = {
        row["identifier"]
        for row in compiled_closure["unresolved"]["references"]
        if row.get("status") == "missing"
        and row.get("kind") == "animation"
    }

    assert presentations == expected["presentations"]
    assert defaults == [expected["default"]]
    assert unresolved == expected["missing"]
    # A presentation fallback is a receipt over authored model/movement/death
    # evidence, never a fabricated animation binding.
    assert set(presentations).isdisjoint(visual["coreAnimations"])
    assert recipe["runtimeRegistration"]["production"] == []
    assert recipe["runtimeRegistration"]["scenarioAdmission"]["role"] == "scenario-only"

    for state, binding in presentations.items():
        row = visual["corePresentations"][state]
        assert row["modelSourceW3d"] == expected["default"]
        if binding == "slow-death":
            assert row["contracts"]
            assert all(contract["module"] == "SlowDeathBehavior" for contract in row["contracts"])
            assert all(contract["sourceIni"] and contract["line"] > 0 for contract in row["contracts"])

    if expected["missing"]:
        excluded = {
            row["identifier"]
            for row in visual["unsupportedVisualReferences"]
            if row.get("runtimeSupport")
            in {
                "excluded-retail-absent-animation-gap",
                "excluded-non-core-animation-gap",
            }
        }
        assert expected["missing"].issubset(excluded)

    if object_id == "MordorBalrog":
        default = next(row for row in visual["components"] if row["default"] is True)
        whip = next(
            row
            for row in visual["components"]
            if row["sourceW3d"] == "art/w3d/mu/mubalswhip_skn.w3d"
        )
        assert default["conditions"] == ["NONE"]
        assert default["drawModule"].endswith("moduletag_bodydraw")
        assert whip["default"] is False
        assert whip["drawModule"].endswith("moduletag_whipdraw")


@pytest.mark.parametrize(
    ("object_id", "identifier", "source_ini", "line"),
    [
        (
            "CINE_GrnDrgn_Flying",
            "WUDrogoth_FLYS",
            "data/ini/object/cinematic/cinematicobjects.ini",
            28711,
        ),
        (
            "IsengardMurderOfCrows",
            "Crebain_SKL.Crebain_IDLA",
            "data/ini/object/neutral/neutralunits.ini",
            3051,
        ),
    ],
)
def test_real_static_idle_gap_survives_neutral_artifact_envelope(
    oracle,
    object_id: str,
    identifier: str,
    source_ini: str,
    line: int,
) -> None:
    descriptor, closure, recipe = _compile_real_scenario_unit(oracle, object_id)

    artifact = compile_neutral_unit_pack_artifact(
        descriptor,
        closure,
        recipe,
        game="bfme2",
        catalog_descriptor=descriptor,
    )
    validate_neutral_unit_pack_artifact(artifact)

    visual = artifact["visualRecipe"]["runtimeRegistration"]["visual"]
    idle_gap = next(
        row
        for row in visual["unsupportedVisualReferences"]
        if row.get("identifier") == identifier
        and row.get("semanticState") == "idle"
        and row.get("conditions") == []
    )
    assert idle_gap["runtimeSupport"] == "excluded-retail-absent-animation-gap"
    assert idle_gap["runtimeExclusionReason"] == (
        "retail-absent-animation-state-covered"
    )
    assert idle_gap["provenance"]["virtualPath"] == source_ini
    assert idle_gap["provenance"]["line"] == line
    assert idle_gap["provenance"]["scopePath"][1] == "IdleAnimationState"
    assert visual["corePresentations"]["idle"]["binding"] == "static-model"
    assert "idle" not in visual["coreAnimations"]
    assert artifact["visualRecipe"]["runtimeRegistration"]["production"] == []

    if object_id == "IsengardMurderOfCrows":
        selected_gap = next(
            row
            for row in visual["unsupportedVisualReferences"]
            if row.get("identifier") == identifier
            and row.get("semanticState") == "selected"
        )
        assert selected_gap["conditions"] == ["SELECTED"]
        assert selected_gap["provenance"]["virtualPath"] == source_ini
        assert selected_gap["provenance"]["line"] == 3070
        assert selected_gap["provenance"]["scopePath"][1] == (
            "AnimationState SELECTED"
        )
