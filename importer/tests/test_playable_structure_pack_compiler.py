import hashlib
import json
from copy import deepcopy

import pytest

from openbfme_importer.playable_structure_lifecycle_evidence import (
    compile_structure_lifecycle_evidence,
)
from openbfme_importer.playable_structure_pack_compiler import (
    PlayableStructurePackCompilerError,
    compile_structure_visual_recipe,
    compose_structure_runtime_document,
    validate_structure_visual_recipe,
)


_TARGET = "UniversalKeep"
_MODEL_INTACT = "art/w3d/fx/keep_skn.w3d"
_MODEL_CONSTRUCTION = "art/w3d/fx/keep_cons.w3d"
_MODEL_RUBBLE = "art/w3d/fx/keep_rubble.w3d"
_MODEL_BIB = "art/w3d/fx/keep_bib.w3d"
_ANIMATION_CONSTRUCTION = "art/w3d/fx/keep_consa.w3d"
_ANIMATION_IDLE = "art/w3d/fx/keep_idla.w3d"
_ANIMATION_COLLAPSE = "art/w3d/fx/keep_levera.w3d"
_ANIMATION_GHOST = "art/w3d/fx/keep_ghosta.w3d"
_HIERARCHY_MAIN = "art/w3d/fx/keep_skl.w3d"


def _leaf(
    identifier: str,
    kind: str,
    path: str,
    phases: list[str],
    conditions: list[str] | None = None,
    usage: str | None = None,
) -> dict[str, object]:
    return {
        "targetObject": _TARGET,
        "identifier": identifier,
        "kind": kind,
        "usage": usage if usage is not None else kind,
        "conditions": conditions or [],
        "lifecyclePhases": phases,
        "status": "resolved",
        "physicalVirtualPaths": [path],
        "evidence": ["fixture"],
        "provenance": {
            "definingObject": _TARGET,
            "inheritanceDistance": 0,
            "line": 1,
            "scopePath": ["W3DModelDraw ModuleTag_Draw", identifier],
            "virtualPath": "data/ini/object/fixture.ini",
        },
    }


def _scan(
    path: str,
    *,
    hierarchy_ids: list[str] | None = None,
    animation_ids: list[str] | None = None,
) -> dict[str, object]:
    return {
        "virtualPath": path,
        "byteLength": 1,
        "sha256": hashlib.sha256(path.encode()).hexdigest(),
        "headerIds": {
            "virtualPath": path,
            "modelIds": [],
            "hierarchyIds": hierarchy_ids or [],
            "animationIds": animation_ids or [],
        },
        "modelReferences": [],
        "warnings": [],
    }


def _closure(*, include_bib: bool = True) -> dict[str, object]:
    leaves = [
        _leaf("Keep_SKN", "model", _MODEL_INTACT, ["intact"]),
        _leaf("Keep_SKN", "model", _MODEL_INTACT, ["damaged"], ["DAMAGED"]),
        _leaf(
            "Keep_CONS",
            "model",
            _MODEL_CONSTRUCTION,
            ["construction"],
            ["ACTIVELY_BEING_CONSTRUCTED", "PARTIALLY_CONSTRUCTED"],
        ),
        _leaf("Keep_RUBBLE", "model", _MODEL_RUBBLE, ["rubble"], ["RUBBLE"]),
        _leaf(
            "Keep_CONSA",
            "animation",
            _ANIMATION_CONSTRUCTION,
            ["construction"],
            ["ACTIVELY_BEING_CONSTRUCTED", "PARTIALLY_CONSTRUCTED"],
        ),
        _leaf("Keep_IDLA", "animation", _ANIMATION_IDLE, ["intact"]),
        _leaf(
            "Keep_LEVERA",
            "animation",
            _ANIMATION_COLLAPSE,
            ["rubble"],
            ["RUBBLE"],
        ),
        _leaf("Keep_GHOSTA", "animation", _ANIMATION_GHOST, ["intact"]),
    ]
    if include_bib:
        leaves.append(
            _leaf(
                "Keep_BIB",
                "model",
                _MODEL_BIB,
                ["intact"],
                usage="floor-model",
            )
        )
    scanned = [
        _scan(_MODEL_INTACT),
        _scan(_MODEL_CONSTRUCTION),
        _scan(_MODEL_RUBBLE),
        _scan(_ANIMATION_CONSTRUCTION, animation_ids=["KEEP_SKL.KEEP_CONSA"]),
        _scan(_ANIMATION_IDLE, animation_ids=["KEEP_SKL.KEEP_IDLA"]),
        _scan(_ANIMATION_COLLAPSE, animation_ids=["KEEP_SKL.KEEP_LEVERA"]),
        _scan(_ANIMATION_GHOST, animation_ids=["KEEP_GHOST_SKL.KEEP_GHOSTA"]),
        _scan(_HIERARCHY_MAIN, hierarchy_ids=["KEEP_SKL"]),
    ]
    if include_bib:
        scanned.insert(3, _scan(_MODEL_BIB, hierarchy_ids=["KEEP_BIB_SKL"]))
    embedded = [
        {
            "identifier": "Fixture.tga",
            "sourceW3dVirtualPath": row["virtualPath"],
            "status": "resolved",
            "physicalVirtualPaths": ["art/compiledtextures/fx/fixture.dds"],
            "evidence": ["fixture"],
            "provenance": {"virtualPath": row["virtualPath"]},
        }
        for row in scanned
        if row["virtualPath"] != _HIERARCHY_MAIN
    ]
    dependency: dict[str, object] = {
        "readBoundary": {
            "policy": "fixture",
            "uniqueVirtualPaths": sorted(
                (row["virtualPath"] for row in scanned), key=str.casefold
            ),
            "uniqueReadCount": len(scanned),
            "byteLength": len(scanned),
        },
        "embeddedTextures": embedded,
        "summary": {
            "fileCount": len(scanned),
            "embeddedTextureReferenceCount": len(embedded),
            "resolvedEmbeddedTextureCount": len(embedded),
            "missingEmbeddedTextureCount": 0,
            "ambiguousEmbeddedTextureCount": 0,
            "invalidEmbeddedTextureCount": 0,
        },
    }
    dependency["aggregateSha256"] = hashlib.sha256(
        json.dumps(dependency, sort_keys=True, separators=(",", ":")).encode()
    ).hexdigest()
    closure: dict[str, object] = {
        "schema": "openbfme.retail-visual-closure",
        "schemaVersion": 1,
        "targets": [{"name": _TARGET, "status": "resolved"}],
        "exactLeaves": leaves,
        "semanticLeaves": [],
        "unresolved": {"graphDiagnostics": [], "references": []},
        "scannedW3d": scanned,
        "w3dDependencyClosure": dependency,
        "summary": {"ready": True},
    }
    closure["aggregateSha256"] = hashlib.sha256(
        json.dumps(closure, sort_keys=True, separators=(",", ":")).encode()
    ).hexdigest()
    return closure


def _rehash(closure: dict[str, object]) -> None:
    dependency = closure["w3dDependencyClosure"]
    scanned = closure["scannedW3d"]
    embedded = dependency["embeddedTextures"]
    dependency["readBoundary"]["uniqueVirtualPaths"] = sorted(
        (row["virtualPath"] for row in scanned), key=str.casefold
    )
    dependency["readBoundary"]["uniqueReadCount"] = len(scanned)
    dependency["readBoundary"]["byteLength"] = len(scanned)
    dependency["summary"]["fileCount"] = len(scanned)
    dependency["summary"]["embeddedTextureReferenceCount"] = len(embedded)
    statuses = [row["status"] for row in embedded]
    dependency["summary"]["resolvedEmbeddedTextureCount"] = statuses.count("resolved")
    dependency["summary"]["missingEmbeddedTextureCount"] = statuses.count("missing")
    dependency["summary"]["ambiguousEmbeddedTextureCount"] = statuses.count(
        "ambiguous"
    )
    dependency["summary"]["invalidEmbeddedTextureCount"] = statuses.count("invalid")
    unsigned_dependency = dict(dependency)
    unsigned_dependency.pop("aggregateSha256", None)
    dependency["aggregateSha256"] = hashlib.sha256(
        json.dumps(
            unsigned_dependency, sort_keys=True, separators=(",", ":")
        ).encode()
    ).hexdigest()
    unsigned = dict(closure)
    unsigned.pop("aggregateSha256", None)
    closure["aggregateSha256"] = hashlib.sha256(
        json.dumps(unsigned, sort_keys=True, separators=(",", ":")).encode()
    ).hexdigest()


def _models_by_source(recipe: dict[str, object]) -> dict[str, dict[str, object]]:
    result: dict[str, dict[str, object]] = {}
    for state in (*recipe["lifecycleStates"], *recipe["bibStates"]):
        resource = next(
            row
            for row in recipe["resources"]
            if row["id"] == state["resourceId"]
        )
        result[str(state["sourceW3d"])] = resource
    return result


def _keep_fixture() -> tuple[dict[str, object], dict[str, object], dict[str, object]]:
    from importer.tests.test_playable_structure_compiler import (
        _structure_documents,
    )
    from openbfme_importer.playable_structure_compiler import (
        compile_playable_structure_descriptor,
    )

    documents = _structure_documents()
    descriptor = compile_playable_structure_descriptor("TestKeep", documents)
    closure = _closure()
    for row in closure["exactLeaves"]:
        row["targetObject"] = "TestKeep"
    _rehash(closure)
    recipe = compile_structure_visual_recipe("TestKeep", closure)
    evidence = compile_structure_lifecycle_evidence("TestKeep", documents)
    return descriptor, recipe, evidence


def test_recipe_is_deterministic_and_validates() -> None:
    first = compile_structure_visual_recipe(_TARGET, _closure())
    second = compile_structure_visual_recipe(_TARGET, _closure())

    assert first == second
    validate_structure_visual_recipe(first)
    assert first["slug"] == "universalkeep"
    assert first["phaseCoverage"] == {
        "covered": ["construction", "intact", "damaged", "rubble"],
        "missing": ["really-damaged", "post-rubble"],
    }


def test_phase_grouping_and_converters() -> None:
    recipe = compile_structure_visual_recipe(_TARGET, _closure())

    models = _models_by_source(recipe)
    assert models[_MODEL_INTACT]["converter"] == "w3d-bundle"
    assert models[_MODEL_CONSTRUCTION]["converter"] == "w3d-bundle"
    assert models[_MODEL_RUBBLE]["converter"] == "w3d-bundle"
    assert models[_MODEL_BIB]["converter"] == "w3d-hierarchical"
    assert models[_MODEL_BIB]["options"]["provenRootRigidBake"] is True
    assert models[_MODEL_INTACT]["output"].endswith(
        "/intact-damaged-keep-skn.glb"
    )
    assert models[_MODEL_CONSTRUCTION]["output"].endswith(
        "/construction-keep-cons.glb"
    )
    assert models[_MODEL_RUBBLE]["output"].endswith("/rubble-keep-rubble.glb")
    assert models[_MODEL_BIB]["output"].endswith("/bib-keep-bib.glb")
    assert recipe["bibStates"][0]["sourceW3d"] == _MODEL_BIB


def test_lifecycle_states_record_conditions_and_clips() -> None:
    recipe = compile_structure_visual_recipe(_TARGET, _closure())

    by_source = {
        str(state["sourceW3d"]): state for state in recipe["lifecycleStates"]
    }
    assert by_source[_MODEL_INTACT]["sourceConditionSets"] == [[], ["DAMAGED"]]
    assert by_source[_MODEL_INTACT]["animationClipIds"] == ["keep_idla"]
    assert by_source[_MODEL_CONSTRUCTION]["animationClipIds"] == ["keep_consa"]
    assert by_source[_MODEL_RUBBLE]["animationClipIds"] == ["keep_levera"]


def test_animations_bind_by_hierarchy_not_by_phase_alone() -> None:
    recipe = compile_structure_visual_recipe(_TARGET, _closure())

    models = _models_by_source(recipe)
    assert models[_MODEL_INTACT]["options"]["animations"] == ["keep_idla.w3d"]
    assert models[_MODEL_CONSTRUCTION]["options"]["animations"] == [
        "keep_consa.w3d"
    ]
    assert "animations" not in models[_MODEL_BIB]["options"]
    assert _HIERARCHY_MAIN in models[_MODEL_INTACT]["patterns"]
    assert _HIERARCHY_MAIN in models[_MODEL_CONSTRUCTION]["patterns"]
    assert _HIERARCHY_MAIN not in models[_MODEL_BIB]["patterns"]

    reasons = {(row["reason"], row["sourceW3d"]) for row in recipe["exclusions"]}
    assert ("animation-hierarchy-unresolved", _ANIMATION_GHOST) in reasons


def test_editor_only_models_are_excluded() -> None:
    closure = _closure()
    closure["exactLeaves"].append(
        _leaf(
            "Keep_EDITOR",
            "model",
            "art/w3d/fx/keep_editor.w3d",
            ["intact"],
            ["WORLD_BUILDER"],
        )
    )
    closure["scannedW3d"].append(_scan("art/w3d/fx/keep_editor.w3d"))
    _rehash(closure)

    recipe = compile_structure_visual_recipe(_TARGET, closure)

    assert ("editor-only-model", "art/w3d/fx/keep_editor.w3d") in {
        (row["reason"], row["sourceW3d"]) for row in recipe["exclusions"]
    }
    assert all(
        "keep_editor" not in str(pattern)
        for row in recipe["resources"]
        for pattern in row["patterns"]
    )


def test_texture_closure_feeds_model_inputs() -> None:
    recipe = compile_structure_visual_recipe(_TARGET, _closure())

    texture_rows = [row for row in recipe["resources"] if row["kind"] == "texture"]
    assert len(texture_rows) == 1
    assert texture_rows[0]["converter"] == "hash-only"
    texture_id = texture_rows[0]["id"]
    for row in recipe["resources"]:
        if row["kind"] == "model":
            assert row["options"]["inputResourceIds"] == [texture_id]


def test_unknown_lifecycle_phase_fails_closed() -> None:
    closure = _closure()
    closure["exactLeaves"][0]["lifecyclePhases"] = ["intact", "melting"]
    _rehash(closure)

    with pytest.raises(
        PlayableStructurePackCompilerError, match="unknown lifecycle phase"
    ):
        compile_structure_visual_recipe(_TARGET, closure)


def test_structure_without_models_fails_closed() -> None:
    closure = _closure()

    with pytest.raises(
        PlayableStructurePackCompilerError, match="no resolved lifecycle model"
    ):
        compile_structure_visual_recipe("AbsentKeep", closure)


def test_unresolved_texture_fails_closed() -> None:
    closure = _closure()
    closure["w3dDependencyClosure"]["embeddedTextures"][0]["status"] = "missing"
    closure["w3dDependencyClosure"]["embeddedTextures"][0][
        "physicalVirtualPaths"
    ] = []
    _rehash(closure)

    with pytest.raises(PlayableStructurePackCompilerError):
        compile_structure_visual_recipe(_TARGET, closure)


def test_tampered_closure_digest_is_rejected() -> None:
    closure = _closure()
    closure["aggregateSha256"] = "0" * 64

    with pytest.raises(
        PlayableStructurePackCompilerError, match="closure digest"
    ):
        compile_structure_visual_recipe(_TARGET, closure)


def test_tampered_recipe_digest_is_rejected() -> None:
    recipe = compile_structure_visual_recipe(_TARGET, _closure())
    tampered = deepcopy(recipe)
    tampered["recipeSha256"] = "0" * 64

    with pytest.raises(
        PlayableStructurePackCompilerError, match="recipe digest"
    ):
        validate_structure_visual_recipe(tampered)


def test_runtime_document_composes_presenter_grade_lifecycle() -> None:
    descriptor, recipe, evidence = _keep_fixture()

    first = compose_structure_runtime_document(descriptor, recipe, evidence)
    second = compose_structure_runtime_document(descriptor, recipe, evidence)

    assert first == second
    assert first["schema"] == "openbfme.playable-structure-runtime"
    assert first["schemaVersion"] == 0
    assert first["descriptorSha256"] == descriptor["descriptorSha256"]
    assert first["recipeSha256"] == recipe["recipeSha256"]
    assert first["lifecycleEvidenceSha256"] == evidence["evidenceSha256"]
    assert len(first["runtimeSha256"]) == 64

    lifecycle = first["registration"]["presentation"]["buildingLifecycle"]
    assert lifecycle["schema"] == "openbfme.building-lifecycle-presentation"
    assert lifecycle["schemaVersion"] == 1
    assert lifecycle["evidenceProfile"] == "composed-structure-runtime"
    assert lifecycle["objectId"] == "bfme2.object.test-keep"
    assert lifecycle["initialPhase"] == "intact"
    assert lifecycle["rebuildHole"] is None

    phases = {row["phase"]: row for row in lifecycle["phases"]}
    assert [row["phase"] for row in lifecycle["phases"]] == [
        "construction",
        "intact",
        "damaged",
        "really-damaged",
        "collapsing",
        "rubble",
        "post-rubble",
        "post-collapse",
    ]
    assert phases["construction"]["nextPhase"] == "intact"
    assert phases["construction"]["animation"] == {
        "clip": "keep_consa",
        "mode": "manual-progress",
    }
    assert phases["construction"]["sourceConditionSets"] == [
        ["ACTIVELY_BEING_CONSTRUCTED", "PARTIALLY_CONSTRUCTED"]
    ]
    assert phases["intact"]["visual"]["mode"] == "glb"
    assert phases["intact"]["animation"] == {
        "clip": "keep_idla",
        "mode": "loop-random",
    }
    assert phases["damaged"]["visual"]["glb"] == phases["intact"]["visual"]["glb"]
    assert (
        phases["really-damaged"]["visual"]["visualFallback"]
        == "default-model-condition-state"
    )
    assert phases["collapsing"]["visual"]["glb"].endswith(
        "/rubble-keep-rubble.glb"
    )
    assert phases["collapsing"]["animation"] == {
        "clip": "keep_levera",
        "mode": "once",
    }
    assert phases["rubble"]["animation"] == {"clip": None, "mode": "none"}
    assert phases["post-rubble"]["visual"] == {
        "mode": "no-render",
        "sourceIdentifier": "None",
    }
    assert phases["post-collapse"]["nextPhase"] is None
    for row in lifecycle["phases"]:
        assert row["transitionAuthority"] == "deterministic-simulation"

    facts = lifecycle["simulationFacts"]
    assert facts["maximumHealth"] == 3000
    assert facts["damageStateRule"] == {
        "damagedThreshold": 2000,
        "reallyDamagedThreshold": 1000,
    }
    assert facts["construction"] == {
        "buildTimeSeconds": 45.0,
        "animationMode": "MANUAL",
        "animation": "keep_consa",
    }
    assert facts["collapse"]["module"] == "StructureCollapseUpdate"
    assert facts["collapse"]["destroyObjectWhenDone"] is True
    assert facts["collapse"]["fxLists"] == {
        "initial": "FX_StructureMediumCollapse",
        "almost-final": "FX_StructureAlmostCollapse",
    }
    assert facts["postRubble"] == {
        "terminalDuration": "destroy-object-when-collapse-done"
    }

    assert lifecycle["audioEvents"] == {"collapse": None, "construction": None}
    effects = lifecycle["effects"]
    assert effects["enteringStateFx"] == {
        "damaged": "FX_BuildingDamaged",
        "really-damaged": "FX_BuildingReallyDamaged",
        "collapsing": "FX_StructureMediumCollapse",
    }
    assert effects["collapseUpdateFx"] == {
        "initial": "FX_StructureMediumCollapse",
        "almost-final": "FX_StructureAlmostCollapse",
    }
    assert effects["particleAttachments"] == [
        {
            "bone": "FireSmall01",
            "options": [],
            "particleSystemId": "FireBuildingMedium",
            "sourceConditions": ["DAMAGED"],
            "sourceObject": "TestKeep",
        }
    ]

    bib = lifecycle["bib"]
    assert bib["duringConstruction"] is False
    assert bib["visual"]["mode"] == "glb"
    assert bib["visual"]["glb"].endswith("/bib-keep-bib.glb")
    assert bib["hideIfModelConditions"] == [
        "AWAITING_CONSTRUCTION",
        "PARTIALLY_CONSTRUCTED",
    ]


def test_runtime_document_without_floor_draw_has_null_bib() -> None:
    from importer.tests.test_playable_structure_compiler import (
        _structure_documents,
    )
    from openbfme_importer.playable_structure_compiler import (
        compile_playable_structure_descriptor,
    )

    documents = _structure_documents()
    objects_path = "data/ini/object/units/test_units.ini"
    source = documents[objects_path].decode("utf-8")
    start = source.index("  Draw = W3DFloorDraw ModuleTag_Bib")
    end = source.index("End", start) + len("End\n")
    documents[objects_path] = (source[:start] + source[end:]).encode("utf-8")

    descriptor = compile_playable_structure_descriptor("TestKeep", documents)
    closure = _closure(include_bib=False)
    for row in closure["exactLeaves"]:
        row["targetObject"] = "TestKeep"
    _rehash(closure)
    recipe = compile_structure_visual_recipe("TestKeep", closure)
    evidence = compile_structure_lifecycle_evidence("TestKeep", documents)

    document = compose_structure_runtime_document(descriptor, recipe, evidence)
    lifecycle = document["registration"]["presentation"]["buildingLifecycle"]
    assert lifecycle["bib"] is None


def test_runtime_document_requires_manual_construction_clip() -> None:
    from importer.tests.test_playable_structure_compiler import (
        _structure_documents,
    )
    from openbfme_importer.playable_structure_compiler import (
        compile_playable_structure_descriptor,
    )

    documents = _structure_documents()
    objects_path = "data/ini/object/units/test_units.ini"
    documents[objects_path] = (
        documents[objects_path]
        .decode("utf-8")
        .replace("        AnimationMode = MANUAL\n", "", 1)
        .encode("utf-8")
    )

    descriptor = compile_playable_structure_descriptor("TestKeep", documents)
    closure = _closure()
    for row in closure["exactLeaves"]:
        row["targetObject"] = "TestKeep"
    _rehash(closure)
    recipe = compile_structure_visual_recipe("TestKeep", closure)
    evidence = compile_structure_lifecycle_evidence("TestKeep", documents)

    with pytest.raises(
        PlayableStructurePackCompilerError, match="exactly one bundled MANUAL"
    ):
        compose_structure_runtime_document(descriptor, recipe, evidence)


def test_runtime_document_rejects_identity_mismatch() -> None:
    descriptor, _recipe, evidence = _keep_fixture()
    recipe = compile_structure_visual_recipe(_TARGET, _closure())

    with pytest.raises(
        PlayableStructurePackCompilerError, match="identities differ"
    ):
        compose_structure_runtime_document(descriptor, recipe, evidence)


def test_runtime_document_rejects_tampered_evidence() -> None:
    descriptor, recipe, evidence = _keep_fixture()
    tampered = deepcopy(evidence)
    tampered["evidenceSha256"] = "0" * 64

    with pytest.raises(ValueError, match="evidence digest"):
        compose_structure_runtime_document(descriptor, recipe, tampered)


def test_state_referencing_unknown_resource_is_rejected() -> None:
    recipe = compile_structure_visual_recipe(_TARGET, _closure())
    tampered = deepcopy(recipe)
    tampered["lifecycleStates"][0]["resourceId"] = "structure-ghost"
    unsigned = dict(tampered)
    unsigned.pop("recipeSha256", None)
    tampered["recipeSha256"] = hashlib.sha256(
        json.dumps(
            unsigned, sort_keys=True, separators=(",", ":"), ensure_ascii=False
        ).encode()
    ).hexdigest()

    with pytest.raises(
        PlayableStructurePackCompilerError, match="unknown resource"
    ):
        validate_structure_visual_recipe(tampered)
