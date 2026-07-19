import hashlib
import json
from copy import deepcopy

import pytest

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
_ANIMATION_LEVER = "art/w3d/fx/keep_levera.w3d"
_ANIMATION_GHOST = "art/w3d/fx/keep_ghosta.w3d"
_HIERARCHY_MAIN = "art/w3d/fx/keep_skl.w3d"


def _leaf(
    identifier: str,
    kind: str,
    path: str,
    phases: list[str],
    conditions: list[str] | None = None,
) -> dict[str, object]:
    return {
        "targetObject": _TARGET,
        "identifier": identifier,
        "kind": kind,
        "usage": kind,
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


def _closure() -> dict[str, object]:
    leaves = [
        _leaf("Keep_SKN", "model", _MODEL_INTACT, ["intact", "damaged"]),
        _leaf("Keep_CONS", "model", _MODEL_CONSTRUCTION, ["construction"]),
        _leaf("Keep_RUBBLE", "model", _MODEL_RUBBLE, ["rubble", "post-rubble"]),
        _leaf("Keep_BIB", "model", _MODEL_BIB, ["construction", "intact"]),
        _leaf("Keep_CONSA", "animation", _ANIMATION_CONSTRUCTION, ["construction"]),
        _leaf("Keep_IDLA", "animation", _ANIMATION_IDLE, ["intact"]),
        _leaf("Keep_LEVERA", "animation", _ANIMATION_LEVER, ["really-damaged"]),
        _leaf("Keep_GHOSTA", "animation", _ANIMATION_GHOST, ["intact"]),
    ]
    scanned = [
        _scan(_MODEL_INTACT),
        _scan(_MODEL_CONSTRUCTION),
        _scan(_MODEL_RUBBLE),
        _scan(_MODEL_BIB, hierarchy_ids=["KEEP_BIB_SKL"]),
        _scan(_ANIMATION_CONSTRUCTION, animation_ids=["KEEP_SKL.KEEP_CONSA"]),
        _scan(_ANIMATION_IDLE, animation_ids=["KEEP_SKL.KEEP_IDLA"]),
        _scan(_ANIMATION_LEVER, animation_ids=["KEEP_SKL.KEEP_LEVERA"]),
        _scan(_ANIMATION_GHOST, animation_ids=["KEEP_GHOST_SKL.KEEP_GHOSTA"]),
        _scan(_HIERARCHY_MAIN, hierarchy_ids=["KEEP_SKL"]),
    ]
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
    for state in recipe["lifecycleStates"]:
        resource = next(
            row
            for row in recipe["resources"]
            if row["id"] == state["resourceId"]
        )
        result[str(state["sourceW3d"])] = resource
    return result


def test_recipe_is_deterministic_and_validates() -> None:
    first = compile_structure_visual_recipe(_TARGET, _closure())
    second = compile_structure_visual_recipe(_TARGET, _closure())

    assert first == second
    validate_structure_visual_recipe(first)
    assert first["slug"] == "universalkeep"
    assert first["phaseCoverage"] == {
        "covered": ["construction", "intact", "damaged", "rubble", "post-rubble"],
        "missing": ["really-damaged"],
    }


def test_phase_grouping_and_converters() -> None:
    recipe = compile_structure_visual_recipe(_TARGET, _closure())

    models = _models_by_source(recipe)
    assert models[_MODEL_INTACT]["converter"] == "w3d-bundle"
    assert models[_MODEL_CONSTRUCTION]["converter"] == "w3d-bundle"
    assert models[_MODEL_RUBBLE]["converter"] == "w3d-hierarchical"
    assert models[_MODEL_BIB]["converter"] == "w3d-hierarchical"
    assert models[_MODEL_INTACT]["output"].endswith(
        "/intact-damaged-keep-skn.glb"
    )
    assert models[_MODEL_CONSTRUCTION]["output"].endswith(
        "/construction-keep-cons.glb"
    )
    assert models[_MODEL_RUBBLE]["output"].endswith(
        "/rubble-post-rubble-keep-rubble.glb"
    )


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
    assert ("animation-unattached", _ANIMATION_LEVER) in reasons
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


def test_runtime_document_joins_descriptor_and_recipe() -> None:
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

    first = compose_structure_runtime_document(descriptor, recipe)
    second = compose_structure_runtime_document(descriptor, recipe)

    assert first == second
    assert first["schema"] == "openbfme.playable-structure-runtime"
    assert first["descriptorSha256"] == descriptor["descriptorSha256"]
    assert first["recipeSha256"] == recipe["recipeSha256"]
    lifecycle = first["registration"]["presentation"]["buildingLifecycle"]
    assert lifecycle["schemaVersion"] == 1
    assert lifecycle["simulationFacts"] == {
        "maxHealth": 3000,
        "damageStateRule": {
            "damagedThreshold": 2000,
            "reallyDamagedThreshold": 1000,
        },
    }
    chain = {row["phase"]: row.get("nextPhase") for row in lifecycle["phases"]}
    assert chain["construction"] == "intact"
    assert chain["intact"] == "damaged"
    assert chain["rubble"] == "post-rubble"
    assert chain["post-rubble"] is None
    assert len(first["runtimeSha256"]) == 64


def test_runtime_document_rejects_identity_mismatch() -> None:
    from importer.tests.test_playable_structure_compiler import (
        _structure_documents,
    )
    from openbfme_importer.playable_structure_compiler import (
        compile_playable_structure_descriptor,
    )

    documents = _structure_documents()
    descriptor = compile_playable_structure_descriptor("TestKeep", documents)
    recipe = compile_structure_visual_recipe(_TARGET, _closure())

    with pytest.raises(
        PlayableStructurePackCompilerError, match="identities differ"
    ):
        compose_structure_runtime_document(descriptor, recipe)


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
