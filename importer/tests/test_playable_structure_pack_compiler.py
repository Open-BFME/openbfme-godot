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
    model_hierarchy_identifiers: list[str] | None = None,
    embedded_animation_channel_count: int | None = None,
    skinned_mesh_count: int | None = None,
    mesh_count: int | None = None,
) -> dict[str, object]:
    row: dict[str, object] = {
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
    if model_hierarchy_identifiers is not None:
        row["modelHierarchyIdentifiers"] = model_hierarchy_identifiers
    if embedded_animation_channel_count is not None:
        row["embeddedAnimationChannelCount"] = embedded_animation_channel_count
    if skinned_mesh_count is not None:
        row["skinnedMeshCount"] = skinned_mesh_count
    if mesh_count is not None:
        row["meshCount"] = mesh_count
    return row


def _closure(
    *, include_bib: bool = True, include_construction: bool = True
) -> dict[str, object]:
    leaves = [
        _leaf("Keep_SKN", "model", _MODEL_INTACT, ["intact"]),
        _leaf("Keep_SKN", "model", _MODEL_INTACT, ["damaged"], ["DAMAGED"]),
        _leaf("Keep_RUBBLE", "model", _MODEL_RUBBLE, ["rubble"], ["RUBBLE"]),
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
    if include_construction:
        leaves.insert(
            2,
            _leaf(
                "Keep_CONS",
                "model",
                _MODEL_CONSTRUCTION,
                ["construction"],
                ["ACTIVELY_BEING_CONSTRUCTED", "PARTIALLY_CONSTRUCTED"],
            ),
        )
        leaves.insert(
            4,
            _leaf(
                "Keep_CONSA",
                "animation",
                _ANIMATION_CONSTRUCTION,
                ["construction"],
                ["ACTIVELY_BEING_CONSTRUCTED", "PARTIALLY_CONSTRUCTED"],
            ),
        )
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


def test_world_builder_only_intact_model_is_rescued_and_converted() -> None:
    closure = _closure()
    closure["exactLeaves"][0] = _leaf(
        "Keep_SKN", "model", _MODEL_INTACT, ["intact"], ["WORLD_BUILDER"]
    )
    _rehash(closure)

    recipe = compile_structure_visual_recipe(_TARGET, closure)

    assert ("editor-only-model", _MODEL_INTACT) not in {
        (row["reason"], row["sourceW3d"]) for row in recipe["exclusions"]
    }
    intact = next(
        state
        for state in recipe["lifecycleStates"]
        if "intact" in state["phases"]
    )
    assert intact["sourceW3d"] == _MODEL_INTACT
    assert ["WORLD_BUILDER"] in intact["sourceConditionSets"]
    assert _MODEL_INTACT in {
        pattern for row in recipe["resources"] for pattern in row["patterns"]
    }
    validate_structure_visual_recipe(recipe)


def _world_builder_keep_fixture() -> tuple[
    dict[str, object], dict[str, object], dict[str, object]
]:
    from importer.tests.test_playable_structure_compiler import (
        _structure_documents,
    )
    from openbfme_importer.playable_structure_compiler import (
        compile_playable_structure_descriptor,
    )

    documents = _structure_documents()
    descriptor = compile_playable_structure_descriptor("TestKeep", documents)
    closure = _closure()
    closure["exactLeaves"][0] = _leaf(
        "Keep_SKN", "model", _MODEL_INTACT, ["intact"], ["WORLD_BUILDER"]
    )
    for row in closure["exactLeaves"]:
        row["targetObject"] = "TestKeep"
    _rehash(closure)
    recipe = compile_structure_visual_recipe("TestKeep", closure)
    evidence = compile_structure_lifecycle_evidence("TestKeep", documents)
    return descriptor, recipe, evidence


def test_runtime_document_presents_world_builder_gated_intact() -> None:
    descriptor, recipe, evidence = _world_builder_keep_fixture()

    document = compose_structure_runtime_document(descriptor, recipe, evidence)
    lifecycle = document["registration"]["presentation"]["buildingLifecycle"]
    phases = {row["phase"]: row for row in lifecycle["phases"]}
    assert phases["intact"]["visual"]["glb"].endswith("/intact-damaged-keep-skn.glb")
    notes = lifecycle["compositionExclusions"]
    assert any(
        note.get("reason") == "world-builder-gated-default-visual"
        and note.get("sourceW3d") == _MODEL_INTACT
        for note in notes
    )


def test_runtime_document_rejects_competing_world_builder_intact_models() -> None:
    descriptor, recipe, evidence = _world_builder_keep_fixture()
    closure = _closure()
    closure["exactLeaves"][0] = _leaf(
        "Keep_SKN", "model", _MODEL_INTACT, ["intact"], ["WORLD_BUILDER"]
    )
    closure["exactLeaves"].append(
        _leaf(
            "Keep_ALT",
            "model",
            "art/w3d/fx/keep_alt.w3d",
            ["intact"],
            ["WORLD_BUILDER"],
        )
    )
    closure["scannedW3d"].append(_scan("art/w3d/fx/keep_alt.w3d"))
    for row in closure["exactLeaves"]:
        row["targetObject"] = "TestKeep"
    _rehash(closure)
    recipe = compile_structure_visual_recipe("TestKeep", closure)

    with pytest.raises(
        PlayableStructurePackCompilerError,
        match="no canonical default-state intact visual",
    ):
        compose_structure_runtime_document(descriptor, recipe, evidence)


def test_start_hidden_bib_variant_does_not_compete_with_the_visible_bib() -> None:
    from importer.tests.test_playable_structure_compiler import (
        _structure_documents,
    )
    from openbfme_importer.playable_structure_compiler import (
        compile_playable_structure_descriptor,
    )

    documents = _structure_documents()
    objects_path = "data/ini/object/units/test_units.ini"
    source = documents[objects_path].decode("utf-8")
    hidden_draw = (
        "  Draw = W3DFloorDraw DrawFloor_V1\n"
        "    ModelName = Keep_V1\n"
        "    StartHidden = Yes\n"
        "    HideIfModelConditions = AWAITING_CONSTRUCTION PARTIALLY_CONSTRUCTED\n"
        "  End\n"
    )
    anchor = "  Draw = W3DFloorDraw ModuleTag_Bib"
    source = source.replace(anchor, hidden_draw + anchor, 1)
    documents[objects_path] = source.encode("utf-8")

    descriptor = compile_playable_structure_descriptor("TestKeep", documents)
    closure = _closure()
    closure["exactLeaves"].append(
        _leaf(
            "Keep_V1",
            "model",
            "art/w3d/fx/keep_v1.w3d",
            ["intact"],
            usage="floor-model",
        )
    )
    closure["scannedW3d"].append(_scan("art/w3d/fx/keep_v1.w3d"))
    for row in closure["exactLeaves"]:
        row["targetObject"] = "TestKeep"
    _rehash(closure)
    recipe = compile_structure_visual_recipe("TestKeep", closure)
    evidence = compile_structure_lifecycle_evidence("TestKeep", documents)

    document = compose_structure_runtime_document(descriptor, recipe, evidence)
    lifecycle = document["registration"]["presentation"]["buildingLifecycle"]
    bib = lifecycle["bib"]
    assert bib["visual"]["glb"].endswith("/bib-keep-bib.glb")
    assert bib["startHiddenAuthored"] is False
    notes = lifecycle["compositionExclusions"]
    assert any(
        note.get("reason") == "start-hidden-authored-bib-not-presented"
        and note.get("sourceW3d") == "art/w3d/fx/keep_v1.w3d"
        for note in notes
    )


def test_runtime_document_rejects_an_all_hidden_bib_set() -> None:
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
        .replace(
            "    ModelName = Keep_BIB\n",
            "    ModelName = Keep_BIB\n    StartHidden = Yes\n",
            1,
        )
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
        PlayableStructurePackCompilerError,
        match="bib visual is absent or ambiguous",
    ):
        compose_structure_runtime_document(descriptor, recipe, evidence)


def test_construction_manual_clip_prefers_the_selected_models_draw_module() -> None:
    from importer.tests.test_playable_structure_compiler import (
        _structure_documents,
    )
    from openbfme_importer.playable_structure_compiler import (
        compile_playable_structure_descriptor,
    )

    documents = _structure_documents()
    objects_path = "data/ini/object/units/test_units.ini"
    source = documents[objects_path].decode("utf-8")
    door_module = (
        "  Draw = W3DScriptedModelDraw ModuleTag_Door\n"
        "    AnimationState = ACTIVELY_BEING_CONSTRUCTED PARTIALLY_CONSTRUCTED\n"
        "      Animation = DoorBuild\n"
        "        AnimationName = KEEP_DOOR_SKL.KEEP_DOOR_BLD\n"
        "        AnimationMode = MANUAL\n"
        "      End\n"
        "    End\n"
        "  End\n"
    )
    anchor = "  Draw = W3DFloorDraw ModuleTag_Bib"
    source = source.replace(anchor, door_module + anchor, 1)
    documents[objects_path] = source.encode("utf-8")

    descriptor = compile_playable_structure_descriptor("TestKeep", documents)
    closure = _closure()
    # The closure scopes each leaf to the draw module that authored it.
    for row in closure["exactLeaves"]:
        row["targetObject"] = "TestKeep"
        row["provenance"]["scopePath"][0] = "W3DScriptedModelDraw ModuleTag_Draw"
    door_leaf = _leaf(
        "KEEP_DOOR_SKL.KEEP_DOOR_BLD",
        "animation",
        "art/w3d/fx/keep_door_bld.w3d",
        ["construction"],
        ["ACTIVELY_BEING_CONSTRUCTED", "PARTIALLY_CONSTRUCTED"],
    )
    door_leaf["targetObject"] = "TestKeep"
    door_leaf["provenance"]["scopePath"][0] = "W3DScriptedModelDraw ModuleTag_Door"
    closure["exactLeaves"].append(door_leaf)
    closure["scannedW3d"].append(
        _scan("art/w3d/fx/keep_door_bld.w3d", animation_ids=["KEEP_DOOR_SKL.KEEP_DOOR_BLD"])
    )
    closure["scannedW3d"].append(
        _scan("art/w3d/fx/keep_door_skl.w3d", hierarchy_ids=["KEEP_DOOR_SKL"])
    )
    _rehash(closure)
    recipe = compile_structure_visual_recipe("TestKeep", closure)
    evidence = compile_structure_lifecycle_evidence("TestKeep", documents)

    document = compose_structure_runtime_document(descriptor, recipe, evidence)
    lifecycle = document["registration"]["presentation"]["buildingLifecycle"]
    phases = {row["phase"]: row for row in lifecycle["phases"]}
    assert phases["construction"]["animation"] == {
        "clip": "keep_consa",
        "mode": "manual-progress",
    }
    notes = lifecycle["compositionExclusions"]
    assert any(
        note.get("reason") == "manual-construction-clip-primary-draw-module"
        and note.get("clip") == "keep_consa"
        and note.get("competingClips") == ["keep_consa", "keep_door_bld"]
        for note in notes
    )


def test_construction_manual_clip_stays_fail_closed_without_module_match() -> None:
    from importer.tests.test_playable_structure_compiler import (
        _structure_documents,
    )
    from openbfme_importer.playable_structure_compiler import (
        compile_playable_structure_descriptor,
    )

    documents = _structure_documents()
    objects_path = "data/ini/object/units/test_units.ini"
    source = documents[objects_path].decode("utf-8")
    door_module = (
        "  Draw = W3DScriptedModelDraw ModuleTag_Door\n"
        "    AnimationState = ACTIVELY_BEING_CONSTRUCTED PARTIALLY_CONSTRUCTED\n"
        "      Animation = DoorBuild\n"
        "        AnimationName = KEEP_DOOR_SKL.KEEP_DOOR_BLD\n"
        "        AnimationMode = MANUAL\n"
        "      End\n"
        "    End\n"
        "  End\n"
    )
    anchor = "  Draw = W3DFloorDraw ModuleTag_Bib"
    source = source.replace(anchor, door_module + anchor, 1)
    documents[objects_path] = source.encode("utf-8")

    descriptor = compile_playable_structure_descriptor("TestKeep", documents)
    closure = _closure()
    # Every leaf claims an unrelated module, so no clip is provably primary.
    for row in closure["exactLeaves"]:
        row["targetObject"] = "TestKeep"
        row["provenance"]["scopePath"][0] = "W3DScriptedModelDraw ModuleTag_Other"
    door_leaf = _leaf(
        "KEEP_DOOR_SKL.KEEP_DOOR_BLD",
        "animation",
        "art/w3d/fx/keep_door_bld.w3d",
        ["construction"],
        ["ACTIVELY_BEING_CONSTRUCTED", "PARTIALLY_CONSTRUCTED"],
    )
    door_leaf["targetObject"] = "TestKeep"
    door_leaf["provenance"]["scopePath"][0] = "W3DScriptedModelDraw ModuleTag_Door"
    closure["exactLeaves"].append(door_leaf)
    closure["scannedW3d"].append(
        _scan("art/w3d/fx/keep_door_bld.w3d", animation_ids=["KEEP_DOOR_SKL.KEEP_DOOR_BLD"])
    )
    closure["scannedW3d"].append(
        _scan("art/w3d/fx/keep_door_skl.w3d", hierarchy_ids=["KEEP_DOOR_SKL"])
    )
    _rehash(closure)
    recipe = compile_structure_visual_recipe("TestKeep", closure)
    evidence = compile_structure_lifecycle_evidence("TestKeep", documents)

    with pytest.raises(
        PlayableStructurePackCompilerError, match="exactly one bundled MANUAL"
    ):
        compose_structure_runtime_document(descriptor, recipe, evidence)


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


def test_ambiguous_texture_fails_closed() -> None:
    closure = _closure()
    closure["w3dDependencyClosure"]["embeddedTextures"][0]["status"] = "ambiguous"
    closure["w3dDependencyClosure"]["embeddedTextures"][0][
        "physicalVirtualPaths"
    ] = []
    _rehash(closure)

    with pytest.raises(PlayableStructurePackCompilerError):
        compile_structure_visual_recipe(_TARGET, closure)


def test_retail_absent_texture_is_an_explicit_exclusion() -> None:
    closure = _closure()
    closure["w3dDependencyClosure"]["embeddedTextures"][0]["status"] = "missing"
    closure["w3dDependencyClosure"]["embeddedTextures"][0][
        "physicalVirtualPaths"
    ] = []
    _rehash(closure)

    recipe = compile_structure_visual_recipe(_TARGET, closure)

    exclusions = [
        row for row in recipe["exclusions"] if row["reason"] == "retail-absent-texture"
    ]
    assert exclusions == [
        {
            "kind": "texture",
            "identifier": "Fixture.tga",
            "sourceW3d": _MODEL_INTACT,
            "reason": "retail-absent-texture",
        }
    ]
    models = _models_by_source(recipe)
    assert models[_MODEL_INTACT]["options"]["retailAbsentTextures"] == [
        "Fixture.tga"
    ]
    validate_structure_visual_recipe(recipe)


def test_retail_none_texture_sentinel_is_not_collected_as_absent() -> None:
    closure = _closure()
    real_missing = closure["w3dDependencyClosure"]["embeddedTextures"][0]
    real_missing["status"] = "missing"
    real_missing["physicalVirtualPaths"] = []
    none_sentinel = dict(real_missing)
    none_sentinel["identifier"] = "None"
    closure["w3dDependencyClosure"]["embeddedTextures"].append(none_sentinel)
    _rehash(closure)

    recipe = compile_structure_visual_recipe(_TARGET, closure)

    models = _models_by_source(recipe)
    assert models[_MODEL_INTACT]["options"]["retailAbsentTextures"] == [
        "Fixture.tga"
    ]
    exclusions = [
        row for row in recipe["exclusions"] if row["reason"] == "retail-absent-texture"
    ]
    assert [row["identifier"] for row in exclusions] == ["Fixture.tga"]


def test_all_sentinel_model_emits_consistent_resources_and_references() -> None:
    # A model whose only texture reference is the retail "None" sentinel gets
    # no texture resource. Its model resource must then reference none either:
    # the profile loader refuses any inputResourceIds entry the profile does
    # not declare.
    closure = _closure()
    rows = closure["w3dDependencyClosure"]["embeddedTextures"]
    for row in rows:
        row["status"] = "missing"
        row["physicalVirtualPaths"] = []
        row["identifier"] = "None"
    _rehash(closure)

    recipe = compile_structure_visual_recipe(_TARGET, closure)

    declared = {str(row["id"]) for row in recipe["resources"]}
    referenced = {
        str(value)
        for row in recipe["resources"]
        for value in (row.get("options") or {}).get("inputResourceIds", [])
    }
    assert referenced <= declared
    assert not [row for row in recipe["resources"] if row["kind"] == "texture"]
    validate_structure_visual_recipe(recipe)


def test_mixed_sentinel_model_still_emits_its_texture_resource() -> None:
    closure = _closure()
    rows = closure["w3dDependencyClosure"]["embeddedTextures"]
    sentinel = dict(rows[0])
    sentinel["status"] = "missing"
    sentinel["physicalVirtualPaths"] = []
    sentinel["identifier"] = "None"
    rows.append(sentinel)
    _rehash(closure)

    recipe = compile_structure_visual_recipe(_TARGET, closure)

    textures = [row for row in recipe["resources"] if row["kind"] == "texture"]
    assert textures
    declared = {str(row["id"]) for row in recipe["resources"]}
    referenced = {
        str(value)
        for row in recipe["resources"]
        for value in (row.get("options") or {}).get("inputResourceIds", [])
    }
    assert referenced <= declared
    validate_structure_visual_recipe(recipe)


_MODEL_D1 = "art/w3d/fx/keep_d1.w3d"
_HIERARCHY_D1 = "art/w3d/fx/keep_d1skl.w3d"
_MODEL_DRC = "art/w3d/fx/keep_drc.w3d"
_MODEL_RING = "art/w3d/fx/keep_ring.w3d"
_MODEL_SKINNED = "art/w3d/fx/keep_skin.w3d"
_MODEL_FIRE = "art/w3d/fx/keep_fire.w3d"
_MODEL_VOID = "art/w3d/fx/keep_void.w3d"


def test_external_hierarchy_model_stages_provider_and_bakes() -> None:
    closure = _closure()
    closure["exactLeaves"].append(
        _leaf("Keep_D1", "model", _MODEL_D1, ["damaged"], ["DAMAGED"])
    )
    closure["scannedW3d"].append(
        _scan(_MODEL_D1, model_hierarchy_identifiers=["KEEP_D1_SKL"])
    )
    closure["scannedW3d"].append(_scan(_HIERARCHY_D1, hierarchy_ids=["KEEP_D1_SKL"]))
    _rehash(closure)

    recipe = compile_structure_visual_recipe(_TARGET, closure)

    models = _models_by_source(recipe)
    assert models[_MODEL_D1]["converter"] == "w3d-hierarchical"
    assert models[_MODEL_D1]["options"]["provenRootRigidBake"] is True
    assert models[_MODEL_D1]["patterns"] == sorted(
        [_MODEL_D1, _HIERARCHY_D1], key=str.casefold
    )
    assert models[_MODEL_D1]["limit"] == 2
    validate_structure_visual_recipe(recipe)


def test_external_hierarchy_model_requires_a_unique_provider() -> None:
    closure = _closure()
    closure["exactLeaves"].append(
        _leaf("Keep_D1", "model", _MODEL_D1, ["damaged"], ["DAMAGED"])
    )
    closure["scannedW3d"].append(
        _scan(_MODEL_D1, model_hierarchy_identifiers=["KEEP_D1_SKL"])
    )
    _rehash(closure)

    with pytest.raises(
        PlayableStructurePackCompilerError, match="hierarchy provider is not unique"
    ):
        compile_structure_visual_recipe(_TARGET, closure)

    closure["scannedW3d"].append(_scan(_HIERARCHY_D1, hierarchy_ids=["KEEP_D1_SKL"]))
    closure["scannedW3d"].append(
        _scan("art/w3d/fx/keep_d1skl_copy.w3d", hierarchy_ids=["KEEP_D1_SKL"])
    )
    _rehash(closure)

    with pytest.raises(
        PlayableStructurePackCompilerError, match="hierarchy provider is not unique"
    ):
        compile_structure_visual_recipe(_TARGET, closure)


def test_embedded_animation_model_is_an_embedded_bundle() -> None:
    closure = _closure()
    closure["exactLeaves"].append(_leaf("Keep_DRC", "model", _MODEL_DRC, ["intact"]))
    closure["scannedW3d"].append(
        _scan(
            _MODEL_DRC,
            hierarchy_ids=["KEEP_DRC"],
            animation_ids=["KEEP_DRC"],
            embedded_animation_channel_count=3,
        )
    )
    _rehash(closure)

    recipe = compile_structure_visual_recipe(_TARGET, closure)

    models = _models_by_source(recipe)
    assert models[_MODEL_DRC]["converter"] == "w3d-bundle"
    assert models[_MODEL_DRC]["options"]["animations"] == ["keep_drc.w3d"]
    assert "provenRootRigidBake" not in models[_MODEL_DRC]["options"]
    validate_structure_visual_recipe(recipe)


def test_pivot_only_model_keeps_hierarchy_with_pivot_option() -> None:
    closure = _closure()
    closure["exactLeaves"].append(_leaf("Keep_FIRE", "model", _MODEL_FIRE, ["intact"]))
    closure["scannedW3d"].append(
        _scan(
            _MODEL_FIRE,
            hierarchy_ids=["KEEP_FIRE"],
            mesh_count=0,
        )
    )
    _rehash(closure)

    recipe = compile_structure_visual_recipe(_TARGET, closure)

    models = _models_by_source(recipe)
    assert models[_MODEL_FIRE]["converter"] == "w3d-hierarchical"
    assert models[_MODEL_FIRE]["options"]["provenPivotOnlyModel"] is True
    assert "provenRootRigidBake" not in models[_MODEL_FIRE]["options"]
    validate_structure_visual_recipe(recipe)


def test_meshless_model_without_hierarchy_fails_closed() -> None:
    closure = _closure()
    closure["exactLeaves"].append(_leaf("Keep_VOID", "model", _MODEL_VOID, ["intact"]))
    closure["scannedW3d"].append(_scan(_MODEL_VOID, mesh_count=0))
    _rehash(closure)

    with pytest.raises(
        PlayableStructurePackCompilerError, match="no meshes and no hierarchy"
    ):
        compile_structure_visual_recipe(_TARGET, closure)


def test_skinned_hierarchy_model_keeps_hierarchy_without_bake() -> None:
    closure = _closure()
    closure["exactLeaves"].append(
        _leaf("Keep_SKIN", "model", _MODEL_SKINNED, ["intact"])
    )
    closure["scannedW3d"].append(
        _scan(
            _MODEL_SKINNED,
            hierarchy_ids=["KEEP_SKIN_SKL"],
            skinned_mesh_count=1,
        )
    )
    _rehash(closure)

    recipe = compile_structure_visual_recipe(_TARGET, closure)

    models = _models_by_source(recipe)
    assert models[_MODEL_SKINNED]["converter"] == "w3d-hierarchical"
    assert "provenRootRigidBake" not in models[_MODEL_SKINNED]["options"]
    validate_structure_visual_recipe(recipe)


def test_vacuous_embedded_animation_chunk_keeps_rigid_bake() -> None:
    closure = _closure()
    closure["exactLeaves"].append(_leaf("Keep_RING", "model", _MODEL_RING, ["intact"]))
    closure["scannedW3d"].append(
        _scan(
            _MODEL_RING,
            hierarchy_ids=["KEEP_RING"],
            animation_ids=["KEEP_RING"],
            embedded_animation_channel_count=0,
        )
    )
    _rehash(closure)

    recipe = compile_structure_visual_recipe(_TARGET, closure)

    models = _models_by_source(recipe)
    assert models[_MODEL_RING]["converter"] == "w3d-hierarchical"
    assert models[_MODEL_RING]["options"]["provenRootRigidBake"] is True
    assert "animations" not in models[_MODEL_RING]["options"]
    validate_structure_visual_recipe(recipe)


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


def _self_referential_construction_fixture(
    *,
    embedded_channels: int = 0,
    keep_split_animation: bool = False,
) -> tuple[dict[str, object], dict[str, object], dict[str, object]]:
    """Build retail's embedded-build-up shape.

    Retail authors a structure's construction animation two ways. The split
    shape names a separate asset (``GBBarracks_ASKL.GBBarracks_ABLD``). The
    embedded shape names the construction model as its own clip
    (``GBWell_A.GBWell_A``) and keys the motion inside that model's own W3D
    as compressed animation channels, with no sibling animation file. The
    fixture rewrites the AnimationName to the self-referential spelling and
    drops the separate animation asset; ``embedded_channels`` sets how many
    keyed channels the construction model itself carries.
    """

    from importer.tests.test_playable_structure_compiler import (
        _structure_documents,
    )
    from openbfme_importer.playable_structure_compiler import (
        compile_playable_structure_descriptor,
    )

    documents = _structure_documents()
    objects_path = "data/ini/object/units/test_units.ini"
    source = documents[objects_path].decode("utf-8")
    source = source.replace(
        "        AnimationName = Keep_SKL.Keep_CONSA\n",
        "        AnimationName = Keep_CONS.Keep_CONS\n",
        1,
    )
    documents[objects_path] = source.encode("utf-8")

    descriptor = compile_playable_structure_descriptor("TestKeep", documents)
    closure = _closure()
    if not keep_split_animation:
        # The embedded shape ships no sibling animation file at all.
        closure["exactLeaves"] = [
            row
            for row in closure["exactLeaves"]
            if row["identifier"] != "Keep_CONSA"
        ]
        closure["scannedW3d"] = [
            row
            for row in closure["scannedW3d"]
            if row["virtualPath"] != _ANIMATION_CONSTRUCTION
        ]
        dependency = closure["w3dDependencyClosure"]
        dependency["embeddedTextures"] = [
            row
            for row in dependency["embeddedTextures"]
            if row["sourceW3dVirtualPath"] != _ANIMATION_CONSTRUCTION
        ]
    for row in closure["exactLeaves"]:
        row["targetObject"] = "TestKeep"
    if embedded_channels:
        for row in closure["scannedW3d"]:
            if row["virtualPath"] == _MODEL_CONSTRUCTION:
                row["headerIds"]["animationIds"] = ["KEEP_CONS.KEEP_CONS"]
                row["embeddedAnimationChannelCount"] = embedded_channels
    _rehash(closure)
    recipe = compile_structure_visual_recipe("TestKeep", closure)
    evidence = compile_structure_lifecycle_evidence("TestKeep", documents)
    return descriptor, recipe, evidence


def test_runtime_document_binds_embedded_self_referential_construction_clip() -> None:
    # Retail's embedded build-up: gbwell_a.w3d ships eight keyed compressed
    # animation channels named GBWELL_A on hierarchy GBWELL_A, and the state
    # names it self-referentially. The conversion declares the model as its
    # own animation source, so the clip it captures must resolve as the
    # phase's manual-progress animation.
    descriptor, recipe, evidence = _self_referential_construction_fixture(
        embedded_channels=8
    )

    resources = _models_by_source(recipe)
    construction = resources[_MODEL_CONSTRUCTION]
    assert construction["converter"] == "w3d-bundle"
    assert construction["options"]["animations"] == ["keep_cons.w3d"]
    state = next(
        row
        for row in recipe["lifecycleStates"]
        if row["sourceW3d"] == _MODEL_CONSTRUCTION
    )
    assert state["animationClipIds"] == ["keep_cons"]

    document = compose_structure_runtime_document(descriptor, recipe, evidence)
    lifecycle = document["registration"]["presentation"]["buildingLifecycle"]
    phases = {row["phase"]: row for row in lifecycle["phases"]}
    assert phases["construction"]["animation"] == {
        "clip": "keep_cons",
        "mode": "manual-progress",
    }
    assert lifecycle["simulationFacts"]["construction"] == {
        "buildTimeSeconds": 45.0,
        "animationMode": "MANUAL",
        "animation": "keep_cons",
    }


def test_self_referential_construction_clip_without_channels_fails_closed() -> None:
    # A self-referential name whose model keys nothing is the genuinely
    # vacuous case: there is no motion anywhere, so it stays a hard failure.
    descriptor, recipe, evidence = _self_referential_construction_fixture()

    with pytest.raises(
        PlayableStructurePackCompilerError,
        match="embeds no keyed animation channels",
    ):
        compose_structure_runtime_document(descriptor, recipe, evidence)


def test_self_referential_construction_clip_fails_closed_when_displaced() -> None:
    # The construction model keys motion, but a separately authored clip also
    # binds to it, so the conversion drops the embedded action and which clip
    # drives the build becomes ambiguous. Fail closed rather than guess.
    descriptor, recipe, evidence = _self_referential_construction_fixture(
        embedded_channels=8, keep_split_animation=True
    )

    with pytest.raises(
        PlayableStructurePackCompilerError,
        match="displaced by externally bound clips",
    ):
        compose_structure_runtime_document(descriptor, recipe, evidence)


def test_runtime_document_reduces_damage_phases_without_thresholds() -> None:
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
        .replace("    MaxHealthDamaged = KEEP_HEALTH_DAMAGED\n", "")
        .replace("    MaxHealthReallyDamaged = KEEP_HEALTH_REALLY_DAMAGED\n", "")
    ).encode("utf-8")

    descriptor = compile_playable_structure_descriptor("TestKeep", documents)
    closure = _closure()
    for row in closure["exactLeaves"]:
        row["targetObject"] = "TestKeep"
    _rehash(closure)
    recipe = compile_structure_visual_recipe("TestKeep", closure)
    evidence = compile_structure_lifecycle_evidence("TestKeep", documents)

    document = compose_structure_runtime_document(descriptor, recipe, evidence)
    lifecycle = document["registration"]["presentation"]["buildingLifecycle"]
    assert [row["phase"] for row in lifecycle["phases"]] == [
        "construction",
        "intact",
        "collapsing",
        "rubble",
        "post-rubble",
        "post-collapse",
    ]
    intact = next(
        row for row in lifecycle["phases"] if row["phase"] == "intact"
    )
    assert intact["nextPhase"] == "collapsing"
    facts = lifecycle["simulationFacts"]
    assert "damageStateRule" not in facts
    assert facts["damageStateRuleStatus"] == "no-authored-damage-thresholds"
    assert {
        "kind": "phase-chain",
        "phase": "damaged/really-damaged",
        "reason": "no-authored-damage-thresholds",
    } in lifecycle["compositionExclusions"]


def test_runtime_document_rejects_half_authored_thresholds() -> None:
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
        .replace("    MaxHealthDamaged = KEEP_HEALTH_DAMAGED\n", "")
    ).encode("utf-8")

    descriptor = compile_playable_structure_descriptor("TestKeep", documents)
    closure = _closure()
    for row in closure["exactLeaves"]:
        row["targetObject"] = "TestKeep"
    _rehash(closure)
    recipe = compile_structure_visual_recipe("TestKeep", closure)
    evidence = compile_structure_lifecycle_evidence("TestKeep", documents)

    with pytest.raises(
        PlayableStructurePackCompilerError, match="only one damage threshold"
    ):
        compose_structure_runtime_document(descriptor, recipe, evidence)


def test_runtime_document_omits_unauthored_construction_phase() -> None:
    from importer.tests.test_playable_structure_compiler import (
        _structure_documents,
    )
    from openbfme_importer.playable_structure_compiler import (
        compile_playable_structure_descriptor,
    )

    documents = _structure_documents()
    objects_path = "data/ini/object/units/test_units.ini"
    source = documents[objects_path].decode("utf-8")
    source = source.replace(
        """    ModelConditionState = ACTIVELY_BEING_CONSTRUCTED PARTIALLY_CONSTRUCTED
      Model = Keep_CONS
    End
    AnimationState = ACTIVELY_BEING_CONSTRUCTED PARTIALLY_CONSTRUCTED
      Animation = Build
        AnimationName = Keep_SKL.Keep_CONSA
        AnimationMode = MANUAL
      End
    End
""",
        "",
    )
    documents[objects_path] = source.encode("utf-8")

    descriptor = compile_playable_structure_descriptor("TestKeep", documents)
    closure = _closure(include_construction=False)
    for row in closure["exactLeaves"]:
        row["targetObject"] = "TestKeep"
    _rehash(closure)
    recipe = compile_structure_visual_recipe("TestKeep", closure)
    evidence = compile_structure_lifecycle_evidence("TestKeep", documents)

    document = compose_structure_runtime_document(descriptor, recipe, evidence)
    lifecycle = document["registration"]["presentation"]["buildingLifecycle"]
    assert [row["phase"] for row in lifecycle["phases"]][0] == "intact"
    assert lifecycle["initialPhase"] == "intact"
    facts = lifecycle["simulationFacts"]
    assert facts["construction"] == {
        "status": "no-authored-construction-states"
    }
    assert {
        "kind": "phase-chain",
        "phase": "construction",
        "reason": "no-authored-construction-states",
    } in lifecycle["compositionExclusions"]


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


def _mapped_image_row(
    identifier: str,
    *,
    texture: str = "art/compiledtextures/bi/bibuttons.tga",
    right: int = 64,
    bottom: int = 64,
) -> dict[str, object]:
    return {
        "id": identifier,
        "texture": texture.rsplit("/", 1)[-1],
        "textureWidth": 256,
        "textureHeight": 256,
        "coords": {"left": 0, "top": 0, "right": right, "bottom": bottom},
        "compiledTextureVirtualPath": texture,
    }


def test_ui_image_bindings_compile_into_recipe_and_runtime() -> None:
    descriptor, _recipe, evidence = _keep_fixture()
    closure = _closure()
    for row in closure["exactLeaves"]:
        row["targetObject"] = "TestKeep"
    _rehash(closure)
    resolved = {
        "BITestKeep": _mapped_image_row("BITestKeep"),
        "UPTestKeep": _mapped_image_row(
            "UPTestKeep",
            texture="art/compiledtextures/up/upportraits.tga",
            right=191,
            bottom=191,
        ),
    }
    gaps = [
        {
            "usage": "select-portrait",
            "imageId": "UPMissing",
            "reason": "unresolved-mapped-image",
        }
    ]

    recipe = compile_structure_visual_recipe(
        "TestKeep", closure, resolved_images=resolved, image_binding_gaps=gaps
    )

    validate_structure_visual_recipe(recipe)
    assert recipe["imageBindings"]["BITestKeep"].startswith(
        "assets/ui/structures/testkeep/"
    )
    assert recipe["imageBindings"]["BITestKeep"].endswith(".png")
    assert recipe["imageBindingMetadata"]["BITestKeep"] == {
        "width": 64,
        "height": 64,
    }
    assert recipe["imageBindingMetadata"]["UPTestKeep"] == {
        "width": 191,
        "height": 191,
    }
    assert recipe["imageBindingGaps"] == gaps
    ui_resources = [
        row
        for row in recipe["resources"]
        if row["converter"] == "texture-atlas-crops"
    ]
    assert len(ui_resources) == 2
    patterns = sorted(
        pattern for row in ui_resources for pattern in row["patterns"]
    )
    assert patterns == [
        "art/compiledtextures/bi/bibuttons.tga",
        "art/compiledtextures/up/upportraits.tga",
    ]
    for row in ui_resources:
        assert row["kind"] == "ui"
        assert row["required"] is True

    document = compose_structure_runtime_document(descriptor, recipe, evidence)
    presentation = document["registration"]["presentation"]
    assert presentation["imageBindings"] == recipe["imageBindings"]
    assert presentation["imageBindingMetadata"] == recipe["imageBindingMetadata"]
    assert presentation["imageBindingGaps"] == gaps


def test_ui_image_gap_only_recipe_records_explicit_rows() -> None:
    descriptor, _recipe, evidence = _keep_fixture()
    closure = _closure()
    for row in closure["exactLeaves"]:
        row["targetObject"] = "TestKeep"
    _rehash(closure)
    gaps = [
        {
            "usage": "construct-button",
            "imageId": "BITestKeep",
            "reason": "unresolved-mapped-image-texture",
        },
        {
            "usage": "select-portrait",
            "imageId": "",
            "reason": "no-authored-select-portrait",
        },
    ]

    recipe = compile_structure_visual_recipe(
        "TestKeep", closure, resolved_images={}, image_binding_gaps=gaps
    )

    validate_structure_visual_recipe(recipe)
    assert recipe["imageBindings"] == {}
    assert recipe["imageBindingGaps"] == sorted(
        gaps, key=lambda row: (row["usage"], row["imageId"], row["reason"])
    )
    assert not any(
        row["converter"] == "texture-atlas-crops" for row in recipe["resources"]
    )

    document = compose_structure_runtime_document(descriptor, recipe, evidence)
    presentation = document["registration"]["presentation"]
    assert presentation["imageBindings"] == {}
    assert len(presentation["imageBindingGaps"]) == 2


def test_recipe_without_ui_binding_request_stays_legacy_shaped() -> None:
    recipe = compile_structure_visual_recipe(_TARGET, _closure())

    assert "imageBindings" not in recipe
    assert "imageBindingMetadata" not in recipe
    assert "imageBindingGaps" not in recipe

def test_resource_ids_distinguish_bib_and_body_when_stems_match() -> None:
    """Bib and body may share a retail W3D stem; resource ids must still be unique.

    Map prop lifecycle fallback previously rejected trees whose bib and intact
    model shared a stem because both resources were keyed only on that stem.
    """

    recipe = compile_structure_visual_recipe(_TARGET, _closure())
    identifiers = [str(row["id"]) for row in recipe["resources"]]
    assert len({item.casefold() for item in identifiers}) == len(identifiers)
    model_ids = {
        str(row["id"])
        for row in recipe["resources"]
        if row.get("kind") == "model"
    }
    bib_ids = {str(row["resourceId"]) for row in recipe["bibStates"]}
    body_ids = {str(row["resourceId"]) for row in recipe["lifecycleStates"]}
    assert bib_ids
    assert body_ids
    assert bib_ids.isdisjoint(body_ids)
    assert model_ids == bib_ids | body_ids
    for bib_id in bib_ids:
        assert "-bib-" in bib_id
