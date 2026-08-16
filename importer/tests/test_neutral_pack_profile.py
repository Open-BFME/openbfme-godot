from __future__ import annotations

from copy import deepcopy
import json

import pytest

import openbfme_importer.neutral_pack_profile as subject
from importer.tests.test_neutral_prop_compiler import _documents as _prop_documents
from importer.tests.test_neutral_prop_pack_compiler import _prop_closure
from importer.tests.test_neutral_structure_pack_compiler import (
    _synthetic_closure as _structure_closure,
)
from importer.tests.test_playable_structure_compiler import _structure_documents
from importer.tests.test_playable_unit_pack_compiler import (
    _closure as _unit_closure,
    _descriptor as _unit_descriptor,
    _rehash_descriptor,
)
from openbfme_importer.neutral_pack_profile import (
    NeutralPackProfileError,
    compile_neutral_unit_pack_artifact,
    compose_neutral_pack_profile,
    validate_neutral_unit_pack_artifact,
)
from openbfme_importer.neutral_prop_pack_compiler import (
    compile_neutral_prop_pack_artifact,
)
from openbfme_importer.neutral_structure_pack_compiler import (
    compile_neutral_structure_pack_artifact,
)
from openbfme_importer.playable_structure_pack_compiler import _digest
from openbfme_importer.playable_unit_pack_compiler import (
    compile_playable_unit_pack_recipe,
)
from openbfme_importer.profile import ImportProfile


def _unit_artifact() -> dict[str, object]:
    descriptor = _unit_descriptor("InfantryHorde")
    descriptor["production"] = []
    descriptor["presentation"]["ui"]["commands"] = []
    descriptor["scenarioAdmission"] = {
        "kind": "authored-non-buildable",
        "role": "scenario-only",
        "surfaces": ["map-placement", "script-spawn", "object-creation-list"],
        "buildCommandExposed": False,
        "evidence": "no-authored-unit-build-route",
        "sourceIni": "data/ini/object/fixture.ini",
        "line": 1,
        "declarationKind": "Object",
    }
    _rehash_descriptor(descriptor)
    catalog_descriptor = deepcopy(descriptor)
    catalog_descriptor["presentation"]["convertedVisuals"] = {}
    catalog_descriptor["presentation"]["resolvedImages"] = {}
    catalog_descriptor["presentation"]["resolvedStrings"] = {}
    catalog_descriptor["presentation"]["resolvedAudio"] = {}
    _rehash_descriptor(catalog_descriptor)
    closure = _unit_closure(descriptor)
    recipe = compile_playable_unit_pack_recipe(descriptor, closure)
    return compile_neutral_unit_pack_artifact(
        descriptor,
        closure,
        recipe,
        game="bfme2",
        catalog_descriptor=catalog_descriptor,
    )


def _artifacts() -> list[dict[str, object]]:
    return [
        _unit_artifact(),
        compile_neutral_structure_pack_artifact(
            "TestCitadel",
            _structure_documents(),
            _structure_closure(),
            role="neutral-structure",
            surfaces=["map-placement", "script-spawn", "object-creation-list"],
        ),
        compile_neutral_prop_pack_artifact(
            "RockBigTroll", _prop_documents(), _prop_closure("RockBigTroll")
        ),
    ]


def _catalog(artifacts: list[dict[str, object]]) -> dict[str, object]:
    domains = {
        "InfantryHorde": ("unit", "scenario-only"),
        "TestCitadel": ("structure", "ambient-or-scenario"),
        "RockBigTroll": ("prop", "ambient-or-scenario"),
    }
    rows = []
    for artifact in artifacts:
        object_id = str(artifact["objectId"])
        domain, role = domains[object_id]
        rows.append(
            {
                "objectId": object_id,
                "runtimeDomain": domain,
                "role": role,
                "runtimeStatus": "descriptor-ready",
                "descriptor": deepcopy(
                    artifact["catalogDescriptor"]
                    if domain == "unit"
                    else artifact["descriptor"]
                ),
            }
        )
    value: dict[str, object] = {
        "schema": "openbfme.neutral-mob-catalog",
        "schemaVersion": 2,
        "game": "bfme2",
        "neutralMobs": rows,
        "summary": {},
    }
    value["catalogSha256"] = _digest(value)
    return value


def _bounded_fixture(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setitem(subject.EXPECTED_COUNTS, "bfme2", 3)
    monkeypatch.setattr(subject, "validate_neutral_mob_catalog", lambda value: None)
    monkeypatch.setattr(
        subject, "validate_neutral_dependency_pack_artifact", lambda value: None
    )


def _dependency_artifact(catalog: dict[str, object]) -> dict[str, object]:
    resource = {
        "id": "structure-treasurechest1-intact-pchesttreasure",
        "kind": "model",
        "converter": "w3d-static",
        "patterns": ["art/compiledtextures/treasure/pchesttreasure.w3d"],
        "output": "assets/models/structures/treasurechest1/intact-pchesttreasure.glb",
        "required": True,
        "limit": 1,
        "expected_count": 1,
    }
    recipe = {
        "objectId": "TreasureChest1",
        "recipeSha256": "3" * 64,
        "resources": [resource],
    }
    runtime = {
        "objectId": "TreasureChest1",
        "runtimeDomain": "active-pickup",
        "runtimeStatus": "executable",
        "runtimeSha256": "4" * 64,
    }
    pickup = {
        "objectId": "TreasureChest1",
        "runtimeDomain": "active-pickup",
        "runtimeStatus": "executable",
        "artifactSha256": "5" * 64,
        "descriptor": {"descriptorSha256": "2" * 64},
        "visualRecipe": recipe,
        "runtime": runtime,
        "resourceOwnership": subject._resource_ownership(
            recipe, "neutral pickup TreasureChest1"
        ),
    }
    return {
        "game": "bfme2",
        "catalogSha256": catalog["catalogSha256"],
        "artifactSha256": "6" * 64,
        "plan": {"schema": "fixture-neutral-dependency-plan"},
        "pickupArtifacts": [pickup],
        "summary": {"pickupObjectCount": 1},
        "runtimeSummary": {
            "contractCount": 1,
            "executableCount": 1,
            "deferredCount": 0,
            "ready": True,
        },
    }


def test_unit_tuple_is_sealed_with_exact_scenario_and_resource_ownership() -> None:
    artifact = _unit_artifact()
    validate_neutral_unit_pack_artifact(artifact)
    assert artifact["runtimeDomain"] == "unit"
    assert artifact["descriptor"]["production"] == []
    assert artifact["catalogDescriptorSha256"] != artifact[
        "integratedDescriptorSha256"
    ]
    assert artifact["catalogDescriptorSha256"] == artifact[
        "catalogDescriptor"
    ]["descriptorSha256"]
    assert artifact["integratedDescriptorSha256"] == artifact["descriptor"][
        "descriptorSha256"
    ]
    assert artifact["role"] == "scenario-only"
    assert artifact["resourceOwnership"]["resourceIds"]
    assert artifact["artifactSha256"] == _digest(
        {key: value for key, value in artifact.items() if key != "artifactSha256"}
    )


@pytest.mark.parametrize(
    ("field", "message"),
    (
        ("catalog-sha", "ownership drifted"),
        ("integrated-sha", "ownership drifted"),
        ("catalog-object", "catalog object identity drifted"),
        ("catalog-admission", "catalog/integrated admission drifted"),
    ),
)
def test_unit_artifact_rejects_tampered_dual_binding(
    field: str, message: str
) -> None:
    artifact = _unit_artifact()
    if field == "catalog-sha":
        artifact["catalogDescriptorSha256"] = "f" * 64
    elif field == "integrated-sha":
        artifact["integratedDescriptorSha256"] = "e" * 64
    elif field == "catalog-object":
        artifact["catalogDescriptor"]["objectId"] = "OtherNeutral"
        _rehash_descriptor(artifact["catalogDescriptor"])
        artifact["catalogDescriptorSha256"] = artifact["catalogDescriptor"][
            "descriptorSha256"
        ]
    else:
        artifact["catalogDescriptor"]["scenarioAdmission"]["role"] = "creature"
        _rehash_descriptor(artifact["catalogDescriptor"])
        artifact["catalogDescriptorSha256"] = artifact["catalogDescriptor"][
            "descriptorSha256"
        ]
    artifact["artifactSha256"] = _digest(
        {key: value for key, value in artifact.items() if key != "artifactSha256"}
    )
    with pytest.raises(NeutralPackProfileError, match=message):
        validate_neutral_unit_pack_artifact(artifact)


def test_profile_binds_catalog_and_integrated_descriptor_identities(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    _bounded_fixture(monkeypatch)
    artifacts = _artifacts()
    catalog = _catalog(artifacts)
    profile = compose_neutral_pack_profile(
        catalog,
        artifacts,
        dependency_artifact=_dependency_artifact(catalog),
        version="fixture",
    )
    receipt = profile["runtime_data"]["data/neutral/pack-profile-receipt.json"]
    unit = next(row for row in receipt["rows"] if row["runtimeDomain"] == "unit")
    assert unit["catalogDescriptorSha256"] != unit[
        "integratedDescriptorSha256"
    ]
    assert unit["descriptorSha256"] == unit["integratedDescriptorSha256"]

    catalog["neutralMobs"][0]["descriptor"] = deepcopy(artifacts[0]["descriptor"])
    catalog["catalogSha256"] = _digest(
        {key: value for key, value in catalog.items() if key != "catalogSha256"}
    )
    with pytest.raises(NeutralPackProfileError, match="catalog binding drifted"):
        compose_neutral_pack_profile(
            catalog,
            artifacts,
            dependency_artifact=_dependency_artifact(catalog),
            version="fixture",
        )


def test_complete_neutral_profile_is_deterministic_and_cook_loadable(
    tmp_path, monkeypatch: pytest.MonkeyPatch
) -> None:
    _bounded_fixture(monkeypatch)
    artifacts = _artifacts()
    catalog = _catalog(artifacts)
    first = compose_neutral_pack_profile(
        catalog,
        artifacts,
        dependency_artifact=_dependency_artifact(catalog),
        version="fixture",
    )
    second = compose_neutral_pack_profile(
        catalog,
        list(reversed(artifacts)),
        dependency_artifact=_dependency_artifact(catalog),
        version="fixture",
    )
    assert first == second
    assert first["pack"]["id"] == "bfme2-neutral-vslice"
    assert len(first["pack"]["files"]) == 6
    assert set(first["pack"]["files"]) == {
        "playableUnit.infantryhorde",
        "playableStructure.testcitadel",
        "neutralProp.rockbigtroll",
        "neutralPickup.treasurechest1",
        "neutralDependencyGraph",
        "neutralPackProfileReceipt",
    }
    receipt = first["runtime_data"]["data/neutral/pack-profile-receipt.json"]
    assert receipt["objectCount"] == 3
    assert receipt["dependencyObjectCount"] == 1
    assert receipt["dependencyRows"][0]["runtimeDomain"] == "active-pickup"
    prop_receipt = next(
        row for row in receipt["rows"] if row["objectId"] == "RockBigTroll"
    )
    prop_runtime = first["runtime_data"]["data/neutral-props/rockbigtroll.json"]
    assert prop_receipt["runtimeModuleEvidenceCount"] == 1
    assert prop_receipt["runtimeCapabilityCount"] == 1
    assert prop_receipt["runtimeModuleEvidenceSha256"] == _digest(
        prop_runtime["runtimeModuleEvidence"]
    )
    assert prop_receipt["runtimeCapabilitiesSha256"] == _digest(
        prop_runtime["runtimeCapabilities"]
    )
    assert prop_receipt["runtimeDescriptorSha256"] == prop_runtime[
        "descriptorSha256"
    ]
    assert not any(
        key.startswith(("faction", "hud", "commandSet"))
        for key in first["pack"]["files"]
    )
    path = tmp_path / "neutral-profile.json"
    path.write_text(json.dumps(first), encoding="utf-8")
    loaded = ImportProfile.load(path)
    assert loaded.pack_id == "bfme2-neutral-vslice"
    assert len(loaded.resources) == len(first["resources"])


def test_profile_packages_deferred_custom_animation_resources_and_receipt(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    _bounded_fixture(monkeypatch)
    artifacts = _artifacts()
    structure = next(row for row in artifacts if row["objectId"] == "TestCitadel")
    request = {
        "requestSha256": "a" * 64,
        "edgeIds": ["bfme2:TestCitadel:edge"],
        "particleClosure": {
            "resources": [
                {
                    "id": "fx-neutral-custom-animation-bfme2-tex-untamed",
                    "kind": "texture", "converter": "texture",
                    "patterns": ["art/compiledtextures/fx/untamed.dds"],
                    "output": "assets/textures/effects/neutral-custom-animation-bfme2/untamed.png",
                    "required": True, "limit": 1, "expected_count": 1,
                },
                {
                    "id": "fx-neutral-custom-animation-bfme2-def-untamed",
                    "kind": "data", "converter": "sage-particle-definition",
                    "patterns": ["data/ini/fxparticlesystem.ini"],
                    "output": "effects/particles/neutral-custom-animation-bfme2/untamed.json",
                    "required": True, "limit": 1, "expected_count": 1,
                    "options": {"kind": "FXParticleSystem", "name": "UntamedAllegiance"},
                },
            ]
        },
    }
    structure["customAnimationPresentation"] = request
    structure["runtime"]["registration"]["presentation"]["deferredCustomAnimationRequest"] = deepcopy(request)
    structure["runtime"].pop("runtimeSha256")
    structure["runtime"]["runtimeSha256"] = _digest(structure["runtime"])
    structure.pop("artifactSha256")
    structure["artifactSha256"] = _digest(structure)
    monkeypatch.setattr(subject, "validate_neutral_structure_pack_artifact", lambda value: None)
    monkeypatch.setattr(subject, "validate_neutral_custom_animation_presentation", lambda value: None)
    catalog = _catalog(artifacts)
    profile = compose_neutral_pack_profile(
        catalog, artifacts, dependency_artifact=_dependency_artifact(catalog), version="fixture"
    )
    assert {row["id"] for row in profile["resources"]}.issuperset(
        {resource["id"] for resource in request["particleClosure"]["resources"]}
    )
    receipt = profile["runtime_data"]["data/neutral/pack-profile-receipt.json"]
    row = next(value for value in receipt["rows"] if value["objectId"] == "TestCitadel")
    assert row["customAnimationPresentationSha256"] == "a" * 64
    assert row["customAnimationEdgeCount"] == 1


def test_profile_refuses_deferred_reachable_dependency(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    _bounded_fixture(monkeypatch)
    artifacts = _artifacts()
    catalog = _catalog(artifacts)
    dependency = _dependency_artifact(catalog)
    dependency["runtimeSummary"] = {
        "contractCount": 1,
        "executableCount": 0,
        "deferredCount": 1,
        "ready": False,
    }
    with pytest.raises(NeutralPackProfileError, match="gameplay contracts are deferred"):
        compose_neutral_pack_profile(
            catalog,
            artifacts,
            dependency_artifact=dependency,
            version="fixture",
        )


@pytest.mark.parametrize("stale_part", ["simulation", "squish"])
def test_profile_refuses_stale_passive_noncombatant_readiness(
    stale_part: str, monkeypatch: pytest.MonkeyPatch
) -> None:
    _bounded_fixture(monkeypatch)
    artifacts = _artifacts()
    catalog = _catalog(artifacts)
    descriptor = catalog["neutralMobs"][0]["descriptor"]
    descriptor["kindOf"]["primaryMember"].append(
        "NOT_AUTOACQUIRABLE"
    )
    simulation = descriptor["gameplay"]["simulation"]
    resolved = simulation["resolved"]
    resolved["scenarioOnly"] = {
        "value": True,
        "disposition": "explicit-scenario-admission",
    }
    resolved["combat"] = {
        "disposition": "noncombatant",
        "evidence": "no-effective-weapon-or-damage-route",
    }
    resolved["moduleContracts"] = [
        {
            "module": "SquishCollide",
            "fields": {},
            "runtimeStatus": "executable",
            "extraction": "typed",
        }
    ]
    if stale_part == "simulation":
        simulation["status"] = "unresolved"
        simulation["missing"] = ["combat.weapon"]
    else:
        resolved["moduleContracts"][0]["runtimeStatus"] = "deferred"
    with pytest.raises(NeutralPackProfileError, match="passive simulation"):
        compose_neutral_pack_profile(
            catalog,
            artifacts,
            dependency_artifact=_dependency_artifact(catalog),
            version="fixture",
        )


@pytest.mark.parametrize("failure", ["missing", "duplicate", "cross-edition", "deferred"])
def test_profile_fails_closed_on_incomplete_or_drifted_artifacts(
    failure: str, monkeypatch: pytest.MonkeyPatch
) -> None:
    _bounded_fixture(monkeypatch)
    artifacts = _artifacts()
    catalog = _catalog(artifacts)
    if failure == "missing":
        artifacts.pop()
        message = "not exact"
    elif failure == "duplicate":
        artifacts[-1] = deepcopy(artifacts[0])
        message = "duplicate"
    elif failure == "cross-edition":
        artifacts[0]["game"] = "rotwk"
        artifacts[0].pop("artifactSha256")
        artifacts[0]["artifactSha256"] = _digest(artifacts[0])
        message = "identity drifted"
    else:
        catalog["neutralMobs"][0]["runtimeStatus"] = "deferred"
        message = "deferred"
    with pytest.raises(NeutralPackProfileError, match=message):
        compose_neutral_pack_profile(
            catalog,
            artifacts,
            dependency_artifact=_dependency_artifact(catalog),
            version="fixture",
        )


def test_profile_rejects_cross_object_resource_ownership(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    _bounded_fixture(monkeypatch)
    artifacts = _artifacts()
    catalog = _catalog(artifacts)
    unit_resource = artifacts[0]["visualRecipe"]["resources"][0]
    structure = artifacts[1]
    structure["visualRecipe"]["resources"][0]["id"] = unit_resource["id"]
    # Underlying artifact seal catches the mutation before profile composition.
    with pytest.raises(NeutralPackProfileError, match="invalid"):
        compose_neutral_pack_profile(
            catalog,
            artifacts,
            dependency_artifact=_dependency_artifact(catalog),
            version="fixture",
        )


def test_profile_rejects_prop_runtime_module_evidence_drift(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    _bounded_fixture(monkeypatch)
    artifacts = _artifacts()
    catalog = _catalog(artifacts)
    prop = next(row for row in artifacts if row["objectId"] == "RockBigTroll")
    prop["runtime"]["runtimeModuleEvidence"][0]["semanticSha256"] = "0" * 64
    prop["runtime"]["runtimeCapabilities"][0]["moduleEvidence"][
        "semanticSha256"
    ] = "0" * 64
    prop["runtime"].pop("descriptorSha256", None)
    prop["runtime"]["descriptorSha256"] = _digest(prop["runtime"])
    prop["provenance"]["runtimeDescriptorSha256"] = prop["runtime"][
        "descriptorSha256"
    ]
    prop.pop("artifactSha256", None)
    prop["artifactSha256"] = _digest(prop)

    with pytest.raises(NeutralPackProfileError, match="runtimeModuleEvidence drifted"):
        compose_neutral_pack_profile(
            catalog,
            artifacts,
            dependency_artifact=_dependency_artifact(catalog),
            version="fixture",
        )
