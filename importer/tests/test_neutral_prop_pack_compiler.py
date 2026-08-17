from __future__ import annotations

from copy import deepcopy
import json
from pathlib import Path
import tempfile

import pytest

from importer.tests.test_neutral_prop_compiler import _documents
from importer.tests.test_playable_structure_pack_compiler import _closure, _rehash
from openbfme_importer.catalog import InstallCatalog
from openbfme_importer.module_census import read_catalog_documents
from openbfme_importer.neutral_prop_compiler import NEUTRAL_PROP_OBJECT_IDS
from openbfme_importer.neutral_prop_pack_compiler import (
    NeutralPropPackCompilerError,
    compile_neutral_prop_pack_artifact,
    validate_neutral_prop_pack_artifact,
)
from openbfme_importer.playable_structure_pack_compiler import _digest
from openbfme_importer.playable_unit_compiler import prepare_playable_unit_compiler
from openbfme_importer.profile import (
    ImportProfile,
    assert_input_resource_references_resolve,
)
from openbfme_importer.retail_visual_closure import build_retail_visual_closure
from openbfme_importer.retail_ability_fx_ingress import build_texture_index
from openbfme_importer.neutral_prop_death_fx import build_audio_sample_index


def _effect_documents(root: Path) -> dict[str, bytes]:
    return {
        relative: (root / relative).read_bytes()
        for relative in (
            "data/ini/fxlist.ini",
            "data/ini/particlesystem.ini",
            "data/ini/fxparticlesystem.ini",
            "data/ini/soundeffects.ini",
        )
    }


def _prop_closure(object_id: str = "SpiderWebs01") -> dict[str, object]:
    closure = _closure(include_bib=False, include_construction=False)
    closure["targets"] = [{"name": object_id, "status": "resolved"}]
    closure["exactLeaves"] = [
        row
        for row in closure["exactLeaves"]
        if row["kind"] == "model" and row["lifecyclePhases"] == ["intact"]
    ]
    for row in closure["exactLeaves"]:
        row["targetObject"] = object_id
        row["provenance"]["definingObject"] = object_id
    _rehash(closure)
    return closure


def _resign(artifact: dict[str, object]) -> None:
    artifact.pop("artifactSha256", None)
    artifact["artifactSha256"] = _digest(artifact)


def test_artifact_owns_converted_glb_and_contentdb_loadable_runtime() -> None:
    artifact = compile_neutral_prop_pack_artifact(
        "SpiderWebs01", _documents(), _prop_closure()
    )

    validate_neutral_prop_pack_artifact(artifact)
    runtime = artifact["runtime"]
    assert runtime["schema"] == "openbfme.neutral-prop-descriptor"
    assert runtime["scenarioAdmission"]["surfaces"] == [
        "map-placement",
        "script-spawn",
        "object-creation-list",
    ]
    assert runtime["production"] == []
    visual = runtime["presentation"]["convertedVisual"]
    assert visual["mode"] == "glb"
    assert visual["glb"].startswith("assets/models/neutral-props/")
    assert visual["glb"].endswith(".glb")
    assert visual["modelResourceId"] == artifact["resourceOwnership"][
        "modelResourceId"
    ]
    resources = artifact["resourceOwnership"]["resources"]
    assert {row["kind"] for row in resources} == {"model", "texture"}
    assert all(row["id"].startswith("neutral-prop-") for row in resources)
    assert artifact["provenance"]["recipeSha256"] == artifact["visualRecipe"][
        "recipeSha256"
    ]


def test_artifact_preserves_full_lineage_module_evidence_without_execution() -> None:
    artifact = compile_neutral_prop_pack_artifact(
        "RockBigTroll", _documents(), _prop_closure("RockBigTroll")
    )

    validate_neutral_prop_pack_artifact(artifact)
    descriptor = artifact["descriptor"]
    runtime = artifact["runtime"]
    assert descriptor["moduleContracts"] == runtime["moduleContracts"]
    assert len(runtime["moduleContracts"]) == 1
    assert runtime["moduleContracts"][0]["module"] == "BezierProjectileBehavior"
    assert runtime["moduleContracts"][0]["runtimeStatus"] == "deferred"
    assert runtime["moduleContracts"][0]["effectGraph"]["trajectory"]["runtimeStatus"] == "executable"
    assert runtime["runtimeModuleEvidence"] == descriptor["runtimeModuleEvidence"]
    assert runtime["runtimeCapabilities"] == descriptor["runtimeCapabilities"]
    assert runtime["runtimeModuleEvidence"][0]["kind"] == (
        "BezierProjectileBehavior"
    )
    assert runtime["runtimeModuleEvidence"][0]["consumed"] is False
    assert runtime["runtimeCapabilities"][0]["runtimeStatus"] == "deferred"

    broken = deepcopy(artifact)
    broken["runtime"]["runtimeModuleEvidence"][0]["consumed"] = True
    broken["runtime"].pop("descriptorSha256", None)
    broken["runtime"]["descriptorSha256"] = _digest(broken["runtime"])
    _resign(broken)
    with pytest.raises(NeutralPropPackCompilerError, match="consumed=false"):
        validate_neutral_prop_pack_artifact(broken)

    broken = deepcopy(artifact)
    broken["runtime"]["runtimeModuleEvidence"][0]["semanticSha256"] = "0" * 64
    broken["runtime"]["runtimeCapabilities"][0]["moduleEvidence"][
        "semanticSha256"
    ] = "0" * 64
    broken["runtime"].pop("descriptorSha256", None)
    broken["runtime"]["descriptorSha256"] = _digest(broken["runtime"])
    _resign(broken)
    with pytest.raises(
        NeutralPropPackCompilerError, match="runtime runtimeModuleEvidence drifted"
    ):
        validate_neutral_prop_pack_artifact(broken)

    broken = deepcopy(artifact)
    broken["runtime"]["moduleContracts"][0]["effectGraph"]["trajectory"]["firstHeight"] = 9.0
    broken["runtime"].pop("descriptorSha256", None)
    broken["runtime"]["descriptorSha256"] = _digest(broken["runtime"])
    _resign(broken)
    with pytest.raises(NeutralPropPackCompilerError, match="trajectory graph drifted"):
        validate_neutral_prop_pack_artifact(broken)


def test_neutralized_recipe_references_resolve_in_import_profile() -> None:
    artifact = compile_neutral_prop_pack_artifact(
        "SpiderWebs01", _documents(), _prop_closure()
    )
    resources = artifact["visualRecipe"]["resources"]
    declared = {row["id"] for row in resources}
    model = next(row for row in resources if row["kind"] == "model")
    dependencies = model["options"]["inputResourceIds"]

    assert dependencies
    assert all(value.startswith("neutral-prop-") for value in dependencies)
    assert set(dependencies) <= declared
    assert_input_resource_references_resolve(resources, label="test neutral prop")

    payload = {
        "format": 1,
        "id": "neutral-prop-reference-fixture",
        "pack": {"id": "neutral-prop-reference-pack"},
        "resources": resources,
        "runtime_data": {
            "data/neutral-props/spiderwebs01.json": artifact["runtime"]
        },
    }
    with tempfile.TemporaryDirectory() as raw:
        path = Path(raw) / "profile.json"
        path.write_text(json.dumps(payload), encoding="utf-8")
        profile = ImportProfile.load(path)

    loaded_model = next(row for row in profile.resources if row.kind == "model")
    assert loaded_model.options["inputResourceIds"] == dependencies


def test_missing_or_ambiguous_model_and_texture_fail_closed() -> None:
    missing_model = _prop_closure()
    missing_model["exactLeaves"][0]["status"] = "missing"
    missing_model["exactLeaves"][0]["physicalVirtualPaths"] = []
    _rehash(missing_model)
    with pytest.raises(NeutralPropPackCompilerError, match="pack-ready"):
        compile_neutral_prop_pack_artifact(
            "SpiderWebs01", _documents(), missing_model
        )

    ambiguous_model = _prop_closure()
    source = ambiguous_model["exactLeaves"][0]["physicalVirtualPaths"][0]
    ambiguous_model["exactLeaves"][0]["status"] = "ambiguous"
    ambiguous_model["exactLeaves"][0]["physicalVirtualPaths"] = [
        source,
        "art/w3d/fx/other.w3d",
    ]
    _rehash(ambiguous_model)
    with pytest.raises(NeutralPropPackCompilerError, match="pack-ready"):
        compile_neutral_prop_pack_artifact(
            "SpiderWebs01", _documents(), ambiguous_model
        )

    ambiguous_texture = _prop_closure()
    embedded = ambiguous_texture["w3dDependencyClosure"]["embeddedTextures"]
    selected = ambiguous_texture["exactLeaves"][0]["physicalVirtualPaths"][0]
    row = next(item for item in embedded if item["sourceW3dVirtualPath"] == selected)
    row["status"] = "ambiguous"
    row["physicalVirtualPaths"] = [
        "art/compiledtextures/fx/fixture.dds",
        "art/compiledtextures/fx/fixture.tga",
    ]
    _rehash(ambiguous_texture)
    with pytest.raises(NeutralPropPackCompilerError, match="unresolved texture"):
        compile_neutral_prop_pack_artifact(
            "SpiderWebs01", _documents(), ambiguous_texture
        )

    missing_texture = _prop_closure()
    embedded = missing_texture["w3dDependencyClosure"]["embeddedTextures"]
    selected = missing_texture["exactLeaves"][0]["physicalVirtualPaths"][0]
    row = next(item for item in embedded if item["sourceW3dVirtualPath"] == selected)
    row["status"] = "missing"
    row["physicalVirtualPaths"] = []
    _rehash(missing_texture)
    with pytest.raises(NeutralPropPackCompilerError, match="missing or excluded"):
        compile_neutral_prop_pack_artifact(
            "SpiderWebs01", _documents(), missing_texture
        )


def test_resource_ownership_and_digest_tampering_fail_closed() -> None:
    artifact = compile_neutral_prop_pack_artifact(
        "SpiderWebs01", _documents(), _prop_closure()
    )
    broken = deepcopy(artifact)
    broken["resourceOwnership"] = deepcopy(broken["resourceOwnership"])
    broken["resourceOwnership"]["resourceIds"] = []
    _resign(broken)
    with pytest.raises(NeutralPropPackCompilerError, match="ownership drifted"):
        validate_neutral_prop_pack_artifact(broken)

    broken = deepcopy(artifact)
    broken["artifactSha256"] = "0" * 64
    with pytest.raises(NeutralPropPackCompilerError, match="artifact digest"):
        validate_neutral_prop_pack_artifact(broken)


@pytest.mark.parametrize(
    ("catalog_name", "game", "effective_relative"),
    (
        ("bfme2.json", "bfme2", "cache/effective-assets"),
        (
            "rotwk-layered.json",
            "rotwk",
            "editions/rotwk/cache/layered-effective-assets",
        ),
    ),
)
def test_exact_retail_neutral_prop_family_is_pack_ready(
    catalog_name: str, game: str, effective_relative: str
) -> None:
    repo = Path(__file__).resolve().parents[2]
    retail = repo / "workspace" / "retail-work"
    catalog_path = retail / "catalog" / catalog_name
    effective_root = retail / effective_relative
    if not catalog_path.is_file() or not (
        effective_root / ".openbfme" / "manifest.json"
    ).is_file():
        pytest.skip("operator retail catalog/effective assets are not available")
    documents = dict(read_catalog_documents(InstallCatalog.load(catalog_path)))
    prepared = prepare_playable_unit_compiler(documents)
    object_ids = sorted(NEUTRAL_PROP_OBJECT_IDS, key=str.casefold)
    closure = build_retail_visual_closure(effective_root, object_ids)
    effect_documents = _effect_documents(effective_root)
    fx_texture_index = build_texture_index(effective_root)
    audio_sample_index = build_audio_sample_index(effective_root)

    artifacts = [
        compile_neutral_prop_pack_artifact(
            object_id,
            documents,
            closure,
            prepared=prepared,
            game=game,
            effect_documents=effect_documents,
            fx_texture_index=fx_texture_index,
            audio_sample_index=audio_sample_index,
        )
        for object_id in object_ids
    ]

    assert len(artifacts) == 12
    assert {row["objectId"] for row in artifacts} == set(object_ids)
    assert len({row["artifactSha256"] for row in artifacts}) == 12
    assert all(row["runtime"]["production"] == [] for row in artifacts)
    assert all(
        row["runtime"]["presentation"]["convertedVisual"]["glb"].endswith(
            ".glb"
        )
        for row in artifacts
    )
    assert all(row["resourceOwnership"]["resourceIds"] for row in artifacts)

    rock = next(row for row in artifacts if row["objectId"] == "RockBigTroll")
    binding = rock["runtime"]["presentation"]["deathFxBinding"]
    assert binding["schema"] == "openbfme.neutral-prop-death-fx-binding"
    assert binding["fxListId"] == "FX_RockImpactHit"
    nuggets = binding["authoredNuggets"]
    assert [row["kind"] for row in nuggets].count("ParticleSystem") in {5, 6}
    assert [row["kind"] for row in nuggets].count("ViewShake") == 1
    assert [row["kind"] for row in nuggets].count("Sound") == 1
    names = [
        assignment["value"]
        for row in nuggets if row["kind"] == "ParticleSystem"
        for assignment in row["assignments"] if assignment["field"] == "Name"
    ]
    assert {"TrebuchetImpactDebris", "TrebuchetImpactDust", "ProjectileSplash01a", "ProjectileSplash02a"} <= set(names)
    assert binding["particleBindings"]["unresolved"] == []
    assert binding["particleBindings"]["presentableFxListIds"] == ["FX_RockImpactHit"]
    assert rock["provenance"]["deathFxClosureSha256"] == rock["deathFxClosure"]["aggregateSha256"]
    assert len(rock["deathFxClosure"]["resources"]) >= 15
    assert len(binding["audioClosure"]["sampleIds"]) == 5
    assert binding["audioBindings"]["ImpactEntRock"]
