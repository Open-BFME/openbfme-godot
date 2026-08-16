from __future__ import annotations

from copy import deepcopy
from pathlib import Path

import pytest

from openbfme_importer.catalog import InstallCatalog
from openbfme_importer.module_census import read_catalog_documents
from openbfme_importer.neutral_prop_compiler import (
    NEUTRAL_PROP_OBJECT_IDS,
    NeutralPropCompilerError,
    compile_neutral_prop_descriptor,
    validate_neutral_prop_descriptor,
)
from openbfme_importer.playable_unit_compiler import prepare_playable_unit_compiler
from openbfme_importer.playable_structure_pack_compiler import _digest


def _documents() -> dict[str, bytes]:
    return {
        "data/ini/object/nature/props.ini": b"""
Object RockBigTreeberd
 KindOf = IMMOBILE INERT ROCK
 Geometry = CYLINDER
 GeometryMajorRadius = 12
 GeometryHeight = 8
 GeometryContactPoint = X:1 Y:2 Z:3 GRAB
 Draw = W3DScriptedModelDraw ModuleTag_Draw
  ExtraPublicBone = GrabBone
  DefaultModelConditionState
   Model = MURockBase
  End
 End
 Behavior = BezierProjectileBehavior ModuleTag_03
  DetonateCallsKill = Yes
  FirstHeight = 8
  SecondHeight = 0
  FirstPercentIndent = 43%
  SecondPercentIndent = 86%
  PreLandingStateTime = 1000
  PreLandingEmotion = DOOM
  PreLandingEmotionRadius = 20.0
 End
End
ChildObject RockBigTroll RockBigTreeberd
 Draw = W3DScriptedModelDraw ModuleTag_Draw
  ExtraPublicBone = GrabBone
  DefaultModelConditionState
   Model = MURockTroll
  End
 End
End
Object SpiderWebs01
 KindOf = IMMOBILE OPTIMIZED_PROP
 Draw = W3DScriptedModelDraw ModuleTag_Draw
  DefaultModelConditionState
   Model = PMSpiderWebs01
  End
 End
End
""",
        "data/ini/commandset.ini": b"",
        "data/ini/commandbutton.ini": b"",
    }


def test_compiles_exact_passive_prop_with_inheritance_and_presentation() -> None:
    documents = _documents()
    descriptor = compile_neutral_prop_descriptor("RockBigTroll", documents)
    validate_neutral_prop_descriptor(descriptor)

    assert descriptor["objectId"] == "RockBigTroll"
    assert [row["objectId"] for row in descriptor["inheritance"]] == [
        "RockBigTreeberd",
        "RockBigTroll",
    ]
    assert descriptor["kindOf"]["effective"] == ["IMMOBILE", "INERT", "ROCK"]
    assert descriptor["geometry"]["shape"] == "CYLINDER"
    assert descriptor["geometryContactPoints"][0]["purpose"] == "GRAB"
    assert descriptor["publicBones"][0]["bone"] == "GrabBone"
    assert descriptor["presentation"]["sourceReferences"]["model"][0]["id"] == (
        "MURockTroll"
    )
    assert descriptor["production"] == []
    assert descriptor["scenarioAdmission"]["surfaces"] == [
        "map-placement",
        "script-spawn",
        "object-creation-list",
    ]
    assert len(descriptor["moduleContracts"]) == 1
    bezier = descriptor["moduleContracts"][0]
    assert bezier["module"] == "BezierProjectileBehavior"
    assert bezier["runtimeStatus"] == "deferred"
    assert bezier["extraction"] == "typed"
    assert bezier["effectGraph"]["trajectory"] == {
        "kind": "cubic-bezier-envelope",
        "runtimeStatus": "executable",
        "firstHeight": 8.0,
        "secondHeight": 0.0,
        "firstIndentRatio": 0.43,
        "secondIndentRatio": 0.86,
        "progressAuthority": "external-authored-projectile-flight",
    }
    assert descriptor["runtimeModuleEvidence"] == [
        {
            "ownerRole": "container",
            "kind": "BezierProjectileBehavior",
            "instanceTag": "ModuleTag_03",
            "sourceIni": "data/ini/object/nature/props.ini",
            "line": 14,
            "semanticSha256": descriptor["runtimeModuleEvidence"][0][
                "semanticSha256"
            ],
            "consumed": False,
        }
    ]
    assert descriptor["runtimeCapabilities"] == [
        {
            "kind": "projectile-capable",
            "activation": "authored-projectile-launch",
            "runtimeStatus": "deferred",
            "moduleEvidence": descriptor["runtimeModuleEvidence"][0],
        }
    ]


def test_compiles_spider_web_without_inventing_geometry() -> None:
    descriptor = compile_neutral_prop_descriptor("SpiderWebs01", _documents())

    assert descriptor["geometry"] is None
    assert descriptor["geometryContactPoints"] == []
    assert descriptor["publicBones"] == []
    assert descriptor["kindOf"]["effective"] == ["IMMOBILE", "OPTIMIZED_PROP"]


def test_rejects_unknown_active_and_presentationless_objects() -> None:
    with pytest.raises(NeutralPropCompilerError, match="unsupported neutral prop"):
        compile_neutral_prop_descriptor("UnboundedProp", _documents())

    active = _documents()
    active["data/ini/object/nature/props.ini"] = active[
        "data/ini/object/nature/props.ini"
    ].replace(
        b"KindOf = IMMOBILE OPTIMIZED_PROP",
        b"KindOf = IMMOBILE OPTIMIZED_PROP INFANTRY",
    )
    with pytest.raises(NeutralPropCompilerError, match="passive prop KindOf"):
        compile_neutral_prop_descriptor("SpiderWebs01", active)

    missing_model = _documents()
    missing_model["data/ini/object/nature/props.ini"] = missing_model[
        "data/ini/object/nature/props.ini"
    ].replace(b"   Model = PMSpiderWebs01\n", b"")
    with pytest.raises(NeutralPropCompilerError, match="model presentation"):
        compile_neutral_prop_descriptor("SpiderWebs01", missing_model)


def test_digest_and_scenario_admission_are_fail_closed() -> None:
    descriptor = compile_neutral_prop_descriptor("SpiderWebs01", _documents())
    broken = deepcopy(descriptor)
    broken["production"] = [{"invented": True}]
    with pytest.raises(NeutralPropCompilerError, match="production must be empty"):
        validate_neutral_prop_descriptor(broken)

    broken = deepcopy(descriptor)
    broken["descriptorSha256"] = "0" * 64
    with pytest.raises(NeutralPropCompilerError, match="digest"):
        validate_neutral_prop_descriptor(broken)

    rock = compile_neutral_prop_descriptor("RockBigTroll", _documents())
    broken = deepcopy(rock)
    broken["runtimeModuleEvidence"][0]["consumed"] = True
    with pytest.raises(NeutralPropCompilerError, match="consumed=false"):
        validate_neutral_prop_descriptor(broken)

    broken = deepcopy(rock)
    broken["runtimeCapabilities"][0]["activation"] = "map-placement"
    with pytest.raises(NeutralPropCompilerError, match="runtime capabilities"):
        validate_neutral_prop_descriptor(broken)

    broken = deepcopy(rock)
    broken["moduleContracts"][0]["effectGraph"]["trajectory"]["firstHeight"] = 9.0
    broken.pop("descriptorSha256", None)
    broken["descriptorSha256"] = _digest(broken)
    with pytest.raises(NeutralPropCompilerError, match="trajectory graph drifted"):
        validate_neutral_prop_descriptor(broken)


@pytest.mark.parametrize(
    ("catalog_name", "game"),
    (("bfme2.json", "bfme2"), ("rotwk-layered.json", "rotwk")),
)
def test_retail_passive_prop_family_is_exactly_twelve(
    catalog_name: str, game: str
) -> None:
    repo = Path(__file__).resolve().parents[2]
    catalog_path = repo / ".private" / "retail-work" / "catalog" / catalog_name
    if not catalog_path.is_file():
        pytest.skip("operator retail catalog is not available")
    documents = dict(read_catalog_documents(InstallCatalog.load(catalog_path)))
    prepared = prepare_playable_unit_compiler(documents)

    descriptors = [
        compile_neutral_prop_descriptor(
            object_id, documents, prepared=prepared, game=game
        )
        for object_id in sorted(NEUTRAL_PROP_OBJECT_IDS, key=str.casefold)
    ]

    assert len(descriptors) == 12
    assert {row["objectId"] for row in descriptors} == set(NEUTRAL_PROP_OBJECT_IDS)
    assert all(row["production"] == [] for row in descriptors)
    assert all(
        row["scenarioAdmission"]["surfaces"]
        == ["map-placement", "script-spawn", "object-creation-list"]
        for row in descriptors
    )
    evidence = [
        (row["objectId"], module)
        for row in descriptors
        for module in row["runtimeModuleEvidence"]
    ]
    assert len(evidence) == 3
    assert {object_id for object_id, _module in evidence} == {"RockBigTroll"}
    assert {module["kind"] for _object_id, module in evidence} == {
        "BezierProjectileBehavior",
        "DestroyDie",
        "FXListDie",
    }
    assert all(module["consumed"] is False for _object_id, module in evidence)
    rock = next(row for row in descriptors if row["objectId"] == "RockBigTroll")
    assert len(rock["runtimeCapabilities"]) == 1
    capability = rock["runtimeCapabilities"][0]
    assert capability["kind"] == "projectile-capable"
    assert capability["moduleEvidence"]["kind"] == "BezierProjectileBehavior"
    assert capability["moduleEvidence"]["line"] == 378
    assert capability["moduleEvidence"]["semanticSha256"] == (
        "e4473adfcc8cf882cb904b1516d0e119a17f852dd074902b5544b3444a460455"
    )
