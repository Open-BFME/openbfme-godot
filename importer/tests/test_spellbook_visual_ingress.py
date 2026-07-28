"""Effect-geometry ingress: the lane that gives summoned objects real models."""

from __future__ import annotations

import pytest

from openbfme_importer.playable_unit_pack_compiler import _digest
from openbfme_importer.spellbook_visual_ingress import (
    SpellbookVisualIngressError,
    spellbook_visual_recipe_parts,
    validate_spellbook_visual_bindings,
    visual_object_ids,
)


def _scanned(
    path: str,
    *,
    hierarchy_ids: list[str] | None = None,
    model_hierarchies: list[str] | None = None,
    animation_ids: list[str] | None = None,
    channels: int = 0,
) -> dict[str, object]:
    return {
        "virtualPath": path,
        "byteLength": 1024,
        "headerIds": {
            "hierarchyIds": list(hierarchy_ids or []),
            "animationIds": list(animation_ids or []),
        },
        "modelHierarchyIdentifiers": list(model_hierarchies or []),
        "embeddedAnimationChannelCount": channels,
    }


def _closure(
    object_id: str,
    *,
    model_identifier: str,
    model_path: str,
    scanned: list[dict[str, object]],
    textures: list[str],
    conditions: list[str] | None = None,
    status: str = "resolved",
    extra_models: list[tuple[str, str]] | None = None,
) -> dict[str, object]:
    exact: list[dict[str, object]] = [
        {
            "targetObject": object_id,
            "identifier": model_identifier,
            "kind": "model",
            "usage": "model",
            "status": status,
            "conditions": list(conditions or []),
            "physicalVirtualPaths": [model_path],
            "provenance": {
                "definingObject": object_id,
                "virtualPath": "data/ini/object/x.ini",
                "line": 1,
                "scopePath": ["W3DScriptedModelDraw ModuleTag_01"],
            },
        }
    ]
    for identifier, path in extra_models or []:
        exact.append(
            {
                "targetObject": object_id,
                "identifier": identifier,
                "kind": "model",
                "usage": "model",
                "status": "resolved",
                "conditions": [],
                "physicalVirtualPaths": [path],
                "provenance": {
                    "definingObject": object_id,
                    "virtualPath": "data/ini/object/x.ini",
                    "line": 2,
                    "scopePath": ["W3DScriptedModelDraw ModuleTag_02"],
                },
            }
        )
    body: dict[str, object] = {
        "schema": "openbfme.retail-visual-closure",
        "schemaVersion": 1,
        "targets": [{"name": object_id, "status": "resolved"}],
        "exactLeaves": exact,
        "semanticLeaves": [],
        "unresolved": {"graphDiagnostics": [], "references": []},
        "scannedW3d": scanned,
        "w3dDependencyClosure": {
            "embeddedTextures": [
                {
                    "sourceW3dVirtualPath": model_path,
                    "identifier": texture,
                    "status": "resolved",
                    "physicalVirtualPaths": [texture],
                }
                for texture in textures
            ]
        },
        "summary": {"ready": True},
    }
    body["aggregateSha256"] = _digest(body)
    return body


def _descriptor(*leaves: dict[str, object]) -> dict[str, object]:
    return {"leaves": {"objects": list(leaves)}}


def _model_leaf(object_id: str, *models: str) -> dict[str, object]:
    return {
        "id": object_id,
        "draw": [{"conditions": [], "drawModule": "W3DScriptedModelDraw", "models": list(models)}],
    }


def _invisible_leaf(object_id: str, system: str) -> dict[str, object]:
    return {
        "id": object_id,
        "draw": [
            {
                "conditions": [],
                "drawModule": "W3DScriptedModelDraw",
                "particleSysBones": [{"bone": "None", "particleSystem": system}],
            }
        ],
    }


def test_visual_object_ids_skips_hordes_and_modelless_leaves() -> None:
    descriptor = _descriptor(
        _model_leaf("RohanOathbreaker", "RUPsnt_1_SKN"),
        {
            **_model_leaf("RohanOathbreakerHordeSmall", "HordeMarkRUOat"),
            "horde": {"memberObject": "RohanOathbreaker", "memberCount": 10},
        },
        _invisible_leaf("ElvenGrove", "TaintHCPing"),
        {"id": "GondorArmyofTheDeadSmallEggs"},
    )
    assert visual_object_ids(descriptor) == ["RohanOathbreaker"]


def test_summoned_member_converts_and_horde_binds_to_it() -> None:
    descriptor = _descriptor(
        _model_leaf("RohanOathbreaker", "RUPsnt_1_SKN"),
        {
            **_model_leaf("RohanOathbreakerHordeSmall", "HordeMarkRUOat"),
            "horde": {"memberObject": "RohanOathbreaker", "memberCount": 10},
        },
        _invisible_leaf("CloudBreakSunbeam", "CloudBreakRays"),
    )
    closures = {
        "RohanOathbreaker": _closure(
            "RohanOathbreaker",
            model_identifier="RUPsnt_1_SKN",
            model_path="art/w3d/ru/rupsnt_1_skn.w3d",
            scanned=[
                _scanned(
                    "art/w3d/ru/rupsnt_1_skn.w3d",
                    model_hierarchies=["RUPSNT_1_SKL"],
                ),
                _scanned("art/w3d/ru/rupsnt_1_skl.w3d", hierarchy_ids=["RUPSNT_1_SKL"]),
            ],
            textures=["art/textures/rupsnt.tga"],
        )
    }
    resources, bindings = spellbook_visual_recipe_parts(
        descriptor, "menspellbook", closures
    )
    validate_spellbook_visual_bindings(bindings)

    model_resources = [row for row in resources if row["kind"] == "model"]
    assert len(model_resources) == 1
    resource = model_resources[0]
    assert resource["converter"] == "w3d-hierarchical"
    assert resource["output"] == (
        "assets/models/spellbook/menspellbook/rohanoathbreaker.glb"
    )
    # The external skeleton must ride the same resource or Blender cannot bind.
    assert resource["patterns"] == [
        "art/w3d/ru/rupsnt_1_skl.w3d",
        "art/w3d/ru/rupsnt_1_skn.w3d",
    ]
    assert resource["expected_count"] == len(resource["patterns"])

    textures = [row for row in resources if row["kind"] == "texture"]
    assert len(textures) == 1
    assert textures[0]["patterns"] == ["art/textures/rupsnt.tga"]
    assert resource["options"]["inputResourceIds"] == [textures[0]["id"]]

    objects = bindings["objects"]
    assert objects["RohanOathbreaker"]["status"] == "model"
    # A horde container's own marker W3D is never converted; the player sees
    # the authored MemberObject, so that is what the container binds to.
    horde = objects["RohanOathbreakerHordeSmall"]
    assert horde["status"] == "horde-member"
    assert horde["memberObjectId"] == "RohanOathbreaker"
    assert horde["model"] == objects["RohanOathbreaker"]["model"]
    # Retail authors Model = None here; it stays invisible.
    assert objects["CloudBreakSunbeam"]["status"] == "authored-invisible"
    assert bindings["summary"] == {
        "modelCount": 1,
        "hordeMemberCount": 1,
        "authoredInvisibleCount": 1,
        "unconvertedCount": 0,
    }


def test_embedded_animation_model_converts_as_self_animated_bundle() -> None:
    descriptor = _descriptor(_model_leaf("ElvenWoodTree", "PTElvnWood01"))
    closures = {
        "ElvenWoodTree": _closure(
            "ElvenWoodTree",
            model_identifier="PTElvnWood01",
            model_path="art/w3d/pt/ptelvnwood01.w3d",
            scanned=[
                _scanned(
                    "art/w3d/pt/ptelvnwood01.w3d",
                    hierarchy_ids=["PTELVNWOOD01"],
                    animation_ids=["PTELVNWOOD01.PTELVNWOOD01"],
                    channels=12,
                )
            ],
            textures=["art/textures/elvnwood.tga"],
        )
    }
    resources, bindings = spellbook_visual_recipe_parts(
        descriptor, "elvesspellbook", closures
    )
    model = next(row for row in resources if row["kind"] == "model")
    assert model["converter"] == "w3d-bundle"
    assert model["options"]["animations"] == ["ptelvnwood01.w3d"]
    assert bindings["objects"]["ElvenWoodTree"]["status"] == "model"


def test_static_placeholder_model_converts_without_a_rig() -> None:
    descriptor = _descriptor(_model_leaf("SpellBookEarthquakePiece", "Earthquake02"))
    closures = {
        "SpellBookEarthquakePiece": _closure(
            "SpellBookEarthquakePiece",
            model_identifier="Earthquake02",
            model_path="art/w3d/ex/earthquake02.w3d",
            scanned=[_scanned("art/w3d/ex/earthquake02.w3d")],
            textures=[],
        )
    }
    resources, _ = spellbook_visual_recipe_parts(descriptor, "menspellbook", closures)
    model = next(row for row in resources if row["kind"] == "model")
    assert model["converter"] == "w3d-static"
    assert "animations" not in model["options"]
    assert model["options"]["inputResourceIds"] == []


def test_conditional_only_model_is_recorded_not_guessed() -> None:
    descriptor = _descriptor(_model_leaf("BurnedThing", "PTStump03"))
    closures = {
        "BurnedThing": _closure(
            "BurnedThing",
            model_identifier="PTStump03",
            model_path="art/w3d/pt/ptstump03.w3d",
            scanned=[_scanned("art/w3d/pt/ptstump03.w3d")],
            textures=[],
            conditions=["BURNED"],
        )
    }
    resources, bindings = spellbook_visual_recipe_parts(
        descriptor, "menspellbook", closures
    )
    assert [row for row in resources if row["kind"] == "model"] == []
    assert bindings["objects"]["BurnedThing"]["status"] == "unconverted"


def test_two_draw_modules_with_different_default_models_stay_unconverted() -> None:
    descriptor = _descriptor(_model_leaf("GondorSentryTower_Independant", "GBBtlTwrM"))
    closures = {
        "GondorSentryTower_Independant": _closure(
            "GondorSentryTower_Independant",
            model_identifier="GBBtlTwrM",
            model_path="art/w3d/gb/gbbtltwrm.w3d",
            scanned=[
                _scanned("art/w3d/gb/gbbtltwrm.w3d"),
                _scanned("art/w3d/gb/gbhcbtltwrm.w3d"),
            ],
            textures=[],
            extra_models=[("GBHCBtlTwrM", "art/w3d/gb/gbhcbtltwrm.w3d")],
        )
    }
    _, bindings = spellbook_visual_recipe_parts(descriptor, "menspellbook", closures)
    row = bindings["objects"]["GondorSentryTower_Independant"]
    assert row["status"] == "unconverted"
    assert "ambiguous" in row["reason"]


def test_unresolved_texture_costs_only_that_object() -> None:
    descriptor = _descriptor(
        _model_leaf("Good", "GoodModel"), _model_leaf("Bad", "BadModel")
    )
    good = _closure(
        "Good",
        model_identifier="GoodModel",
        model_path="art/w3d/a/good.w3d",
        scanned=[_scanned("art/w3d/a/good.w3d")],
        textures=["art/textures/good.tga"],
    )
    bad = _closure(
        "Bad",
        model_identifier="BadModel",
        model_path="art/w3d/a/bad.w3d",
        scanned=[_scanned("art/w3d/a/bad.w3d")],
        textures=[],
    )
    bad.pop("aggregateSha256")
    bad["w3dDependencyClosure"] = {
        "embeddedTextures": [
            {
                "sourceW3dVirtualPath": "art/w3d/a/bad.w3d",
                "identifier": "missing.tga",
                "status": "missing",
            }
        ]
    }
    bad["aggregateSha256"] = _digest(bad)
    _, bindings = spellbook_visual_recipe_parts(
        descriptor, "menspellbook", {"Good": good, "Bad": bad}
    )
    assert bindings["objects"]["Good"]["status"] == "model"
    assert bindings["objects"]["Bad"]["status"] == "unconverted"


def test_closure_failures_ride_the_bindings_as_recorded_gaps() -> None:
    descriptor = _descriptor(_model_leaf("SpellBookArrowVolley", "EXArrowVolleyL"))
    resources, bindings = spellbook_visual_recipe_parts(
        descriptor,
        "menspellbook",
        {},
        {"SpellBookArrowVolley": "SageCstSyntaxError: stray End"},
    )
    # An empty closure map is the "no effective-assets root" shape and must stay
    # byte-identical to the pre-visual lane.
    assert resources == []
    assert bindings is None


def test_tampered_closure_digest_is_rejected() -> None:
    descriptor = _descriptor(_model_leaf("Thing", "ThingModel"))
    closure = _closure(
        "Thing",
        model_identifier="ThingModel",
        model_path="art/w3d/a/thing.w3d",
        scanned=[_scanned("art/w3d/a/thing.w3d")],
        textures=[],
    )
    closure["summary"] = {"ready": False}
    with pytest.raises(SpellbookVisualIngressError):
        spellbook_visual_recipe_parts(descriptor, "menspellbook", {"Thing": closure})


def test_validate_rejects_a_binding_without_a_glb() -> None:
    with pytest.raises(SpellbookVisualIngressError):
        validate_spellbook_visual_bindings(
            {"objects": {"Thing": {"status": "model", "model": "art/w3d/a.w3d"}}}
        )


def test_model_condition_state_none_is_the_default_state() -> None:
    """`ModelConditionState = NONE` is SAGE's other spelling of "no condition".

    The Balrog's body is authored that way; reading only
    `DefaultModelConditionState` left the single biggest summon in the game with
    no convertible geometry.
    """

    descriptor = _descriptor(_model_leaf("MordorBalrog", "MUBalrog_SKN"))
    closure = _closure(
        "MordorBalrog",
        model_identifier="MUBalrog_SKN",
        model_path="art/w3d/mu/mubalrog_skn.w3d",
        scanned=[
            _scanned("art/w3d/mu/mubalrog_skn.w3d", hierarchy_ids=["MUBALROG_SKL"])
        ],
        textures=[],
        conditions=["NONE"],
    )
    _, bindings = spellbook_visual_recipe_parts(
        descriptor, "mordorspellbook", {"MordorBalrog": closure}
    )
    assert bindings["objects"]["MordorBalrog"]["status"] == "model"


def test_world_builder_only_geometry_is_an_authored_in_game_absence() -> None:
    descriptor = _descriptor(_model_leaf("WyrmEgg", "CUWyrm_SKN"))
    closure = _closure(
        "WyrmEgg",
        model_identifier="CUWyrm_SKN",
        model_path="art/w3d/cu/cuwyrm_skn.w3d",
        scanned=[_scanned("art/w3d/cu/cuwyrm_skn.w3d")],
        textures=[],
        conditions=["WORLD_BUILDER"],
    )
    resources, bindings = spellbook_visual_recipe_parts(
        descriptor, "mordorspellbook", {"WyrmEgg": closure}
    )
    assert [row for row in resources if row["kind"] == "model"] == []
    row = bindings["objects"]["WyrmEgg"]
    assert row["status"] == "authored-invisible"
    assert "WORLD_BUILDER" in row["reason"]


def test_floor_draw_bib_does_not_make_the_body_look_ambiguous() -> None:
    descriptor = _descriptor(_model_leaf("MordorBarricade", "MBBarcade"))
    closure = _closure(
        "MordorBarricade",
        model_identifier="MBBarcade",
        model_path="art/w3d/mb/mbbarcade.w3d",
        scanned=[
            _scanned("art/w3d/mb/mbbarcade.w3d"),
            _scanned("art/w3d/mb/mbbarcade_bib.w3d"),
        ],
        textures=[],
    )
    bib = dict(closure["exactLeaves"][0])
    bib["identifier"] = "MBBarcade_bib"
    bib["physicalVirtualPaths"] = ["art/w3d/mb/mbbarcade_bib.w3d"]
    bib["provenance"] = {
        **dict(bib["provenance"]),
        "scopePath": ["W3DFloorDraw ModuleTag_DrawFloor"],
    }
    closure["exactLeaves"] = [closure["exactLeaves"][0], bib]
    closure.pop("aggregateSha256")
    closure["aggregateSha256"] = _digest(closure)
    _, bindings = spellbook_visual_recipe_parts(
        descriptor, "mordorspellbook", {"MordorBarricade": closure}
    )
    row = bindings["objects"]["MordorBarricade"]
    assert row["status"] == "model"
    assert row["sourceW3d"] == "art/w3d/mb/mbbarcade.w3d"
