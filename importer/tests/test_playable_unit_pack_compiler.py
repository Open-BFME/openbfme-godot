from __future__ import annotations

from copy import deepcopy
import hashlib
import json
from pathlib import PurePosixPath

import pytest

from importer.tests.test_playable_unit_compiler import _documents
from openbfme_importer.playable_unit_compiler import compile_playable_unit_descriptor
from openbfme_importer.playable_unit_pack_compiler import (
    PlayableUnitPackCompilerError,
    compile_playable_unit_pack_recipe,
    validate_playable_unit_pack_recipe,
)
from openbfme_importer.profile import ImportProfile


def _descriptor(target: str) -> dict[str, object]:
    first = compile_playable_unit_descriptor(target, _documents())
    presentation = first["presentation"]
    image_ids = set(presentation["ui"]["portraitImageIds"])
    for command in presentation["ui"]["commands"]:
        for field in ("ButtonImage",):
            image_ids.update(command["fields"].get(field, []))
    audio_ids = {
        row["id"]
        for owner in presentation["audioRoutes"].values()
        for rows in owner.values()
        for row in rows
    }
    string_ids = {
        value
        for command in presentation["ui"]["commands"]
        for field in ("TextLabel", "DescriptLabel")
        for value in command["fields"].get(field, [])
    }
    descriptor = compile_playable_unit_descriptor(
        target,
        _documents(),
        resolved_images={
            identifier: {
                "id": identifier,
                "texture": "FixtureAtlas.tga",
                "textureWidth": 256,
                "textureHeight": 256,
                "coords": {
                    "left": index * 16,
                    "top": 0,
                    "right": index * 16 + 16,
                    "bottom": 16,
                },
                "compiledTextureVirtualPath": "ui/fixture_atlas.dds",
            }
            for index, identifier in enumerate(sorted(image_ids, key=str.casefold))
        },
        resolved_audio={
            identifier: [f"audio/{identifier}.wav"] for identifier in audio_ids
        },
        resolved_strings={identifier: f"Resolved {identifier}" for identifier in string_ids},
    )
    capability_ids = {row["id"] for row in descriptor["capabilities"]}
    for identifier in ("move", "attack", "death"):
        if (
            identifier not in capability_ids
            and not (identifier == "attack" and "ranged-attack" in capability_ids)
            and not (identifier == "death" and "member-death" in capability_ids)
        ):
            descriptor["capabilities"].append(
                {"id": identifier, "evidence": "fixture runtime capability"}
            )
    descriptor["capabilities"].sort(key=lambda row: row["id"])
    unsigned = dict(descriptor)
    unsigned.pop("descriptorSha256")
    descriptor["descriptorSha256"] = hashlib.sha256(
        json.dumps(
            unsigned, sort_keys=True, separators=(",", ":"), ensure_ascii=False
        ).encode("utf-8")
    ).hexdigest()
    return descriptor


def _rehash_descriptor(descriptor: dict[str, object]) -> None:
    unsigned = dict(descriptor)
    unsigned.pop("descriptorSha256", None)
    descriptor["descriptorSha256"] = hashlib.sha256(
        json.dumps(unsigned, sort_keys=True, separators=(",", ":")).encode()
    ).hexdigest()


def _leaf(
    member: str,
    identifier: str,
    kind: str,
    path: str,
    conditions: list[str],
    scope: str,
    draw: str = "W3DModelDraw ModuleTag_Draw",
) -> dict[str, object]:
    return {
        "targetObject": member,
        "identifier": identifier,
        "kind": kind,
        "usage": kind,
        "conditions": conditions,
        "lifecyclePhases": ["intact"],
        "status": "resolved",
        "physicalVirtualPaths": [path],
        "evidence": ["fixture"],
        "provenance": {
            "definingObject": member,
            "inheritanceDistance": 0,
            "line": 1,
            "scopePath": [draw, scope],
            "virtualPath": "data/ini/object/fixture.ini",
        },
    }


def _closure(
    descriptor: dict[str, object], *, conditional_death: bool = False
) -> dict[str, object]:
    member = descriptor["composition"]["primaryMemberObjectId"]
    container = descriptor["composition"]["containerObjectId"]
    slug = str(member).casefold()
    model = f"art/w3d/fi/{slug}_skn.w3d"
    paths = {
        "idle": f"art/w3d/fi/{slug}_idla.w3d",
        "move": f"art/w3d/fi/{slug}_runa.w3d",
        "attack": f"art/w3d/fi/{slug}_ataka.w3d",
        "death": f"art/w3d/fi/{slug}_diea.w3d",
    }
    visual_ids = []
    for row in descriptor["presentation"]["visualRoots"]:
        identifier = row["id"]
        if identifier.casefold() != "none" and identifier.casefold() not in {
            value.casefold() for value in visual_ids
        }:
            visual_ids.append(identifier)
    primary_visual = visual_ids[0]
    leaves = [
        _leaf(member, primary_visual, "model", model, [], "DefaultModelConditionState"),
        _leaf(
            member,
            f"{member}_IDLA",
            "animation",
            paths["idle"],
            [],
            "IdleAnimationState",
        ),
        _leaf(
            member,
            f"{member}_RUNA",
            "animation",
            paths["move"],
            ["MOVING"],
            "AnimationState MOVING",
        ),
        _leaf(
            member,
            f"{member}_ATAKA",
            "animation",
            paths["attack"],
            ["FIRING_A"],
            "AnimationState FIRING_A",
        ),
        _leaf(
            member,
            f"{member}_DIEA",
            "animation",
            paths["death"],
            ["DYING", "DEATH_1"],
            "AnimationState DYING DEATH_1",
        ),
    ]
    for index, identifier in enumerate(visual_ids[1:], start=1):
        auxiliary_path = f"art/w3d/fi/{slug}_aux{index}.w3d"
        leaves.append(
            _leaf(
                container,
                identifier,
                "model",
                auxiliary_path,
                ["AUXILIARY"],
                "ModelConditionState AUXILIARY",
                f"W3DModelDraw ModuleTag_Aux{index}",
            )
        )
    if conditional_death:
        death_model_path = f"art/w3d/fi/{slug}_die_model.w3d"
        leaves.append(
            _leaf(
                member,
                f"{member}_DIE_MODEL",
                "model",
                death_model_path,
                ["DYING"],
                "ModelConditionState DYING",
            )
        )
    hierarchy = f"{member}_SKL".upper()
    scanned = [
        {
            "virtualPath": model,
            "byteLength": 1,
            "sha256": "0" * 64,
            "headerIds": {
                "virtualPath": model,
                "modelIds": [f"{member}_SKN"],
                "hierarchyIds": [hierarchy],
                "animationIds": [],
            },
            "modelReferences": [],
            "warnings": [],
        }
    ]
    for state, path in paths.items():
        scanned.append(
            {
                "virtualPath": path,
                "byteLength": 1,
                "sha256": hashlib.sha256(path.encode()).hexdigest(),
                "headerIds": {
                    "virtualPath": path,
                    "modelIds": [f"{member}_DIE_MODEL"]
                    if conditional_death and state == "death"
                    else [],
                    "hierarchyIds": [],
                    "animationIds": [f"{hierarchy}.{member}_{state.upper()}"],
                },
                "modelReferences": [],
                "warnings": [],
            }
        )
    selected = {
        path
        for row in leaves
        if row["kind"] in {"model", "animation"}
        for path in row["physicalVirtualPaths"]
    }
    scanned_paths = {row["virtualPath"] for row in scanned}
    for path in sorted(selected - scanned_paths):
        scanned.append(
            {
                "virtualPath": path,
                "byteLength": 1,
                "sha256": hashlib.sha256(path.encode()).hexdigest(),
                "headerIds": {
                    "virtualPath": path,
                    "modelIds": [PurePosixPath(path).stem],
                    "hierarchyIds": [],
                    "animationIds": [],
                },
                "modelReferences": [],
                "warnings": [],
            }
        )
    embedded = [
        {
            "identifier": "Fixture.tga",
            "sourceW3dVirtualPath": path,
            "status": "resolved",
            "physicalVirtualPaths": ["art/compiledtextures/fi/fixture.dds"],
            "evidence": ["fixture"],
            "provenance": {"virtualPath": path},
        }
        for path in sorted(selected)
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
        "targets": [
            {"name": identifier, "status": "resolved"}
            for identifier in sorted({member, container}, key=str.casefold)
        ],
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


def _rehash_closure(closure: dict[str, object]) -> None:
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
    dependency["summary"]["ambiguousEmbeddedTextureCount"] = statuses.count("ambiguous")
    dependency["summary"]["invalidEmbeddedTextureCount"] = statuses.count("invalid")
    dependency_unsigned = dict(dependency)
    dependency_unsigned.pop("aggregateSha256", None)
    dependency["aggregateSha256"] = hashlib.sha256(
        json.dumps(dependency_unsigned, sort_keys=True, separators=(",", ":")).encode()
    ).hexdigest()
    unsigned = dict(closure)
    unsigned.pop("aggregateSha256", None)
    closure["aggregateSha256"] = hashlib.sha256(
        json.dumps(unsigned, sort_keys=True, separators=(",", ":")).encode()
    ).hexdigest()


@pytest.mark.parametrize(
    ("target", "category"),
    [
        ("InfantryHorde", "infantry"),
        ("CavalryHorde", "cavalry"),
        ("HeroUnit", "hero"),
        ("SiegeUnit", "siege"),
        ("MonsterUnit", "monster"),
    ],
)
def test_same_compiler_emits_complete_category_recipes(
    target: str, category: str
) -> None:
    descriptor = _descriptor(target)
    recipe = compile_playable_unit_pack_recipe(descriptor, _closure(descriptor))
    validate_playable_unit_pack_recipe(recipe)
    assert recipe["category"] == category
    assert recipe["runtimeRegistration"]["stringBindings"] == descriptor["presentation"]["resolvedStrings"]
    assert set(recipe["runtimeRegistration"]["imageBindingMetadata"]) == set(recipe["runtimeRegistration"]["imageBindings"])
    assert all(
        metadata == {"width": 16, "height": 16}
        for metadata in recipe["runtimeRegistration"]["imageBindingMetadata"].values()
    )
    assert recipe["descriptorSha256"] == descriptor["descriptorSha256"]
    assert set(recipe["runtimeRegistration"]["visual"]["coreAnimations"]) == {
        "idle",
        "move",
        "attack",
        "death",
    }
    assert recipe["runtimeRegistration"]["production"] == descriptor["production"]
    assert recipe["runtimeRegistration"]["composition"] == descriptor["composition"]
    converters = {row["converter"] for row in recipe["resources"]}
    assert {"hash-only", "texture-atlas-crops", "audio", "w3d-bundle"}.issubset(
        converters
    )
    assert converters.issubset(
        {
            "copy",
            "hash-only",
            "texture",
            "texture-atlas-crops",
            "audio",
            "w3d-bundle",
            "w3d-hierarchical",
        }
    )


def test_recipe_is_deterministic_under_visual_leaf_reordering() -> None:
    descriptor = _descriptor("CavalryHorde")
    closure = _closure(descriptor)
    first = compile_playable_unit_pack_recipe(descriptor, closure)
    shuffled = deepcopy(closure)
    shuffled["exactLeaves"] = list(reversed(shuffled["exactLeaves"]))
    shuffled["scannedW3d"] = list(reversed(shuffled["scannedW3d"]))
    _rehash_closure(shuffled)
    second = compile_playable_unit_pack_recipe(descriptor, shuffled)
    assert first["resources"] == second["resources"]
    assert first["runtimeRegistration"] == second["runtimeRegistration"]


def test_conditional_embedded_death_model_is_not_attached_to_intact_bundle() -> None:
    descriptor = _descriptor("SiegeUnit")
    recipe = compile_playable_unit_pack_recipe(
        descriptor, _closure(descriptor, conditional_death=True)
    )
    components = recipe["runtimeRegistration"]["visual"]["components"]
    assert len(components) == 2
    death = next(row for row in components if not row["default"])
    binding = recipe["runtimeRegistration"]["visual"]["coreAnimations"]["death"][0]
    assert binding["modelSourceW3d"] == death["sourceW3d"]
    intact_resource = next(
        row for row in recipe["resources"] if row["id"] == components[0]["resourceId"]
    )
    assert binding["sourceW3d"] not in intact_resource["patterns"]


def test_missing_required_core_state_fails_closed() -> None:
    descriptor = _descriptor("HeroUnit")
    closure = _closure(descriptor)
    closure["exactLeaves"] = [
        row for row in closure["exactLeaves"] if "DYING" not in row["conditions"]
    ]
    _rehash_closure(closure)
    with pytest.raises(PlayableUnitPackCompilerError, match="death"):
        compile_playable_unit_pack_recipe(descriptor, closure)


def test_missing_conditional_core_animation_is_not_tolerated() -> None:
    descriptor = _descriptor("HeroUnit")
    closure = _closure(descriptor)
    resolved_move = next(
        row
        for row in closure["exactLeaves"]
        if row["kind"] == "animation" and "MOVING" in row["conditions"]
    )
    missing_move = deepcopy(resolved_move)
    missing_move["identifier"] = "HEROUNIT_SKL.HEROUNIT_RUN_MISSING"
    missing_move["status"] = "missing"
    missing_move.pop("physicalVirtualPaths")
    closure["unresolved"]["references"].append(missing_move)
    closure["summary"]["ready"] = False
    _rehash_closure(closure)

    with pytest.raises(PlayableUnitPackCompilerError, match="conversion-ready"):
        compile_playable_unit_pack_recipe(descriptor, closure)


def test_rehashed_non_core_gap_cannot_be_forged_into_core_state() -> None:
    descriptor = _descriptor("HeroUnit")
    closure = _closure(descriptor)
    missing = deepcopy(
        next(row for row in closure["exactLeaves"] if row["kind"] == "animation")
    )
    missing["identifier"] = "HEROUNIT_SKL.HEROUNIT_MORALE_MISSING"
    missing["conditions"] = ["EMOTION_MORALE_HIGH"]
    missing["status"] = "missing"
    missing.pop("physicalVirtualPaths")
    closure["unresolved"]["references"].append(missing)
    closure["summary"]["ready"] = False
    _rehash_closure(closure)
    recipe = compile_playable_unit_pack_recipe(descriptor, closure)
    row = recipe["runtimeRegistration"]["visual"]["unsupportedVisualReferences"][0]
    assert row["runtimeSupport"] == "excluded-non-core-animation-gap"
    row["conditions"] = ["MOVING"]
    unsigned = dict(recipe)
    unsigned.pop("recipeSha256")
    recipe["recipeSha256"] = hashlib.sha256(
        json.dumps(unsigned, sort_keys=True, separators=(",", ":")).encode()
    ).hexdigest()
    with pytest.raises(PlayableUnitPackCompilerError, match="unsupported visual"):
        validate_playable_unit_pack_recipe(recipe)


def test_unresolved_selected_texture_fails_closed() -> None:
    descriptor = _descriptor("MonsterUnit")
    closure = _closure(descriptor)
    closure["w3dDependencyClosure"]["embeddedTextures"][0]["status"] = "missing"
    closure["w3dDependencyClosure"]["embeddedTextures"][0]["physicalVirtualPaths"] = []
    _rehash_closure(closure)
    with pytest.raises(PlayableUnitPackCompilerError, match="unresolved texture"):
        compile_playable_unit_pack_recipe(descriptor, closure)


def test_missing_texture_on_conditional_model_is_explicitly_unsupported() -> None:
    descriptor = _descriptor("HeroUnit")
    closure = _closure(descriptor)
    member = descriptor["composition"]["primaryMemberObjectId"]
    conditional_id = "HeroMountedModel"
    conditional_path = "art/w3d/fi/heromountedmodel.w3d"
    conditional_root = deepcopy(descriptor["presentation"]["visualRoots"][0])
    conditional_root["id"] = conditional_id
    conditional_root["expression"] = conditional_id
    descriptor["presentation"]["visualRoots"].insert(0, conditional_root)
    _rehash_descriptor(descriptor)

    closure["exactLeaves"].append(
        _leaf(
            member,
            conditional_id,
            "model",
            conditional_path,
            ["MOUNTED"],
            "ModelConditionState MOUNTED",
        )
    )
    closure["scannedW3d"].append(
        {
            "virtualPath": conditional_path,
            "byteLength": 1,
            "sha256": "1" * 64,
            "headerIds": {
                "virtualPath": conditional_path,
                "modelIds": [conditional_id],
                "hierarchyIds": [],
                "animationIds": [],
            },
            "modelReferences": [],
            "warnings": [],
        }
    )
    closure["w3dDependencyClosure"]["embeddedTextures"].append(
        {
            "identifier": "MissingMounted.tga",
            "sourceW3dVirtualPath": conditional_path,
            "status": "missing",
            "physicalVirtualPaths": [],
            "evidence": ["fixture"],
            "provenance": {"virtualPath": conditional_path},
        }
    )
    closure["summary"]["ready"] = False
    _rehash_closure(closure)

    recipe = compile_playable_unit_pack_recipe(descriptor, closure)

    visual = recipe["runtimeRegistration"]["visual"]
    assert len(visual["components"]) == 1
    assert visual["unsupportedVisualReferences"][0]["identifier"] == "MissingMounted.tga"
    assert visual["unsupportedVisualReferences"][0]["runtimeSupport"] == (
        "excluded-missing-conditional-model-texture"
    )
    assert visual["unsupportedVisualReferences"][1]["identifier"] == conditional_id
    assert visual["unsupportedVisualReferences"][1]["runtimeSupport"] == (
        "excluded-hero-form"
    )


def test_world_builder_model_occurrence_does_not_claim_runtime_ownership() -> None:
    descriptor = _descriptor("HeroUnit")
    closure = _closure(descriptor)
    member = descriptor["composition"]["primaryMemberObjectId"]
    default_model = next(
        row for row in closure["exactLeaves"] if row["kind"] == "model"
    )
    editor_row = _leaf(
        member,
        default_model["identifier"],
        "model",
        default_model["physicalVirtualPaths"][0],
        ["WORLD_BUILDER"],
        "ModelConditionState WORLD_BUILDER",
        "W3DModelDraw ModuleTag_WorldBuilder",
    )
    closure["exactLeaves"].append(editor_row)
    _rehash_closure(closure)

    recipe = compile_playable_unit_pack_recipe(descriptor, closure)

    visual = recipe["runtimeRegistration"]["visual"]
    assert len(visual["components"]) == 1
    assert visual["unsupportedVisualReferences"][0]["identifier"] == editor_row["identifier"]
    assert visual["unsupportedVisualReferences"][0]["runtimeSupport"] == "excluded-editor-only"


def test_authored_silent_command_audio_has_an_explicit_empty_binding() -> None:
    descriptor = _descriptor("InfantryHorde")
    command = descriptor["presentation"]["ui"]["commands"][0]
    command["audioRoutes"] = [
        {
            "field": "UnitSpecificSound",
            "id": "SilentPurchaseEvent",
            "tokenOrdinal": 0,
            "resolution": "resolved",
            "sourceIni": "data/ini/commandbutton.ini",
        }
    ]
    descriptor["presentation"]["resolvedAudio"]["SilentPurchaseEvent"] = []
    _rehash_descriptor(descriptor)
    recipe = compile_playable_unit_pack_recipe(descriptor, _closure(descriptor))
    assert recipe["runtimeRegistration"]["audioBindings"]["SilentPurchaseEvent"] == []
    assert (
        recipe["runtimeRegistration"]["audioResolution"]["SilentPurchaseEvent"]
        == "authored-silent"
    )
    validate_playable_unit_pack_recipe(recipe)

    invented = deepcopy(recipe)
    invented["runtimeRegistration"]["audioBindings"]["InventedSilent"] = []
    invented["runtimeRegistration"]["audioResolution"]["InventedSilent"] = (
        "authored-silent"
    )
    unsigned = dict(invented)
    unsigned.pop("recipeSha256")
    invented["recipeSha256"] = hashlib.sha256(
        json.dumps(unsigned, sort_keys=True, separators=(",", ":")).encode()
    ).hexdigest()
    with pytest.raises(PlayableUnitPackCompilerError, match="audio binding"):
        validate_playable_unit_pack_recipe(invented)


def test_tampered_recipe_digest_is_rejected() -> None:
    descriptor = _descriptor("InfantryHorde")
    recipe = compile_playable_unit_pack_recipe(descriptor, _closure(descriptor))
    recipe["category"] = "monster"
    with pytest.raises(PlayableUnitPackCompilerError, match="digest"):
        validate_playable_unit_pack_recipe(recipe)


def test_siege_attack_capability_requires_attack_animation() -> None:
    descriptor = _descriptor("SiegeUnit")
    for row in descriptor["capabilities"]:
        if row["id"] == "attack":
            row["id"] = "siege-attack"
    _rehash_descriptor(descriptor)
    recipe = compile_playable_unit_pack_recipe(descriptor, _closure(descriptor))
    assert "attack" in recipe["runtimeRegistration"]["visual"]["coreAnimations"]


def test_missing_scanned_hierarchy_is_never_invented() -> None:
    descriptor = _descriptor("HeroUnit")
    closure = _closure(descriptor)
    for row in closure["scannedW3d"]:
        row["headerIds"]["hierarchyIds"] = []
    _rehash_closure(closure)
    with pytest.raises(
        PlayableUnitPackCompilerError, match="not backed by a scanned W3D"
    ):
        compile_playable_unit_pack_recipe(descriptor, closure)


def test_non_core_authored_animation_is_packaged_and_explicitly_unsupported() -> None:
    descriptor = _descriptor("HeroUnit")
    closure = _closure(descriptor)
    member = descriptor["composition"]["primaryMemberObjectId"]
    path = "art/w3d/fi/herounit_celebrate.w3d"
    closure["exactLeaves"].append(
        _leaf(
            member,
            "HeroUnit_CELEBRATE",
            "animation",
            path,
            ["EMOTION_CELEBRATING"],
            "AnimationState EMOTION_CELEBRATING",
        )
    )
    closure["scannedW3d"].append(
        {
            "virtualPath": path,
            "byteLength": 1,
            "sha256": hashlib.sha256(path.encode()).hexdigest(),
            "headerIds": {
                "virtualPath": path,
                "modelIds": [],
                "hierarchyIds": [],
                "animationIds": ["HEROUNIT_SKL.HEROUNIT_CELEBRATE"],
            },
            "modelReferences": [],
            "warnings": [],
        }
    )
    closure["w3dDependencyClosure"]["embeddedTextures"].append(
        {
            "identifier": "Fixture.tga",
            "sourceW3dVirtualPath": path,
            "status": "resolved",
            "physicalVirtualPaths": ["art/compiledtextures/fi/fixture.dds"],
            "evidence": ["fixture"],
            "provenance": {"virtualPath": path},
        }
    )
    _rehash_closure(closure)
    recipe = compile_playable_unit_pack_recipe(descriptor, closure)
    authored = recipe["runtimeRegistration"]["visual"]["authoredAnimationStates"]
    row = next(item for item in authored if item["sourceW3d"] == path)
    assert row["runtimeSupport"] == "packaged-unimplemented"
    assert any(path in resource["patterns"] for resource in recipe["resources"])


def test_mixed_member_horde_fails_until_per_member_presentation_exists() -> None:
    descriptor = _descriptor("InfantryHorde")
    descriptor["composition"]["members"].append(
        {"objectId": "SecondMember", "count": 1}
    )
    _rehash_descriptor(descriptor)
    with pytest.raises(
        PlayableUnitPackCompilerError, match="secondary member presentation"
    ):
        compile_playable_unit_pack_recipe(
            descriptor, _closure(_descriptor("InfantryHorde"))
        )


def test_rehashed_unsafe_or_dangling_recipe_is_rejected() -> None:
    descriptor = _descriptor("HeroUnit")
    recipe = compile_playable_unit_pack_recipe(descriptor, _closure(descriptor))
    model = next(row for row in recipe["resources"] if row["kind"] == "model")
    model["output"] = "../../escape.glb"
    unsigned = dict(recipe)
    unsigned.pop("recipeSha256")
    recipe["recipeSha256"] = hashlib.sha256(
        json.dumps(unsigned, sort_keys=True, separators=(",", ":")).encode()
    ).hexdigest()
    with pytest.raises(PlayableUnitPackCompilerError, match="unsafe"):
        validate_playable_unit_pack_recipe(recipe)


def test_unsupported_or_tampered_nested_closure_is_rejected() -> None:
    descriptor = _descriptor("HeroUnit")
    closure = _closure(descriptor)
    closure["schemaVersion"] = 99
    _rehash_closure(closure)
    with pytest.raises(PlayableUnitPackCompilerError, match="identity"):
        compile_playable_unit_pack_recipe(descriptor, closure)
    closure = _closure(descriptor)
    closure["w3dDependencyClosure"]["summary"]["fileCount"] += 1
    unsigned = dict(closure)
    unsigned.pop("aggregateSha256")
    closure["aggregateSha256"] = hashlib.sha256(
        json.dumps(unsigned, sort_keys=True, separators=(",", ":")).encode()
    ).hexdigest()
    with pytest.raises(PlayableUnitPackCompilerError, match="digest"):
        compile_playable_unit_pack_recipe(descriptor, closure)


def test_audio_outputs_are_executable_pcm_wav_rules() -> None:
    descriptor = _descriptor("HeroUnit")
    recipe = compile_playable_unit_pack_recipe(descriptor, _closure(descriptor))
    audio = [row for row in recipe["resources"] if row["converter"] == "audio"]
    assert audio
    assert all(row["output"].endswith(".wav") for row in audio)
    assert all(row["options"] == {"force_pcm": True} for row in audio)


def test_shared_audio_sample_is_converted_once_across_routes() -> None:
    descriptor = _descriptor("HeroUnit")
    resolved = descriptor["presentation"]["resolvedAudio"]
    identifiers = sorted(resolved, key=str.casefold)
    assert len(identifiers) >= 2
    resolved[identifiers[1]] = list(resolved[identifiers[0]])
    _rehash_descriptor(descriptor)
    recipe = compile_playable_unit_pack_recipe(descriptor, _closure(descriptor))
    audio = [row for row in recipe["resources"] if row["converter"] == "audio"]
    unique_sources = {
        source.casefold() for values in resolved.values() for source in values
    }
    assert len(audio) == len(unique_sources)
    bindings = recipe["runtimeRegistration"]["audioBindings"]
    assert bindings[identifiers[0]] == bindings[identifiers[1]]


def test_shared_ui_atlas_is_one_exact_crop_resource() -> None:
    descriptor = _descriptor("HeroUnit")
    recipe = compile_playable_unit_pack_recipe(descriptor, _closure(descriptor))
    atlases = [
        row for row in recipe["resources"] if row["converter"] == "texture-atlas-crops"
    ]
    assert len(atlases) == 1
    assert len(atlases[0]["options"]["crops"]) == len(
        descriptor["presentation"]["resolvedImages"]
    )
    assert set(recipe["runtimeRegistration"]["imageBindings"]) == set(
        descriptor["presentation"]["resolvedImages"]
    )


def test_ambiguous_hierarchy_owner_fails_closed() -> None:
    descriptor = _descriptor("HeroUnit")
    closure = _closure(descriptor)
    duplicate = deepcopy(closure["scannedW3d"][0])
    duplicate["virtualPath"] = "art/w3d/fi/duplicate_skl.w3d"
    duplicate["headerIds"] = deepcopy(duplicate["headerIds"])
    duplicate["headerIds"]["virtualPath"] = duplicate["virtualPath"]
    closure["scannedW3d"].append(duplicate)
    _rehash_closure(closure)
    with pytest.raises(PlayableUnitPackCompilerError, match="ownership is ambiguous"):
        compile_playable_unit_pack_recipe(descriptor, closure)


def test_reused_default_model_retains_unconditional_occurrence() -> None:
    descriptor = _descriptor("HeroUnit")
    closure = _closure(descriptor)
    default = closure["exactLeaves"][0]
    closure["exactLeaves"].append(
        _leaf(
            default["targetObject"],
            default["identifier"],
            "model",
            default["physicalVirtualPaths"][0],
            ["USER_1"],
            "ModelConditionState USER_1",
        )
    )
    _rehash_closure(closure)
    recipe = compile_playable_unit_pack_recipe(descriptor, closure)
    component = next(
        row
        for row in recipe["runtimeRegistration"]["visual"]["components"]
        if row["default"]
    )
    assert len(component["authoredOccurrences"]) == 2
    assert component["conditions"] == ["USER_1"]


def test_auxiliary_visual_leaf_is_converted_and_registered() -> None:
    descriptor = _descriptor("HeroUnit")
    closure = _closure(descriptor)
    member = descriptor["composition"]["primaryMemberObjectId"]
    source = "art/compiledtextures/fi/hero_upgrade.dds"
    closure["exactLeaves"].append(
        _leaf(
            member,
            "HeroUpgrade.tga",
            "texture",
            source,
            ["USER_1"],
            "ModelConditionState USER_1",
        )
    )
    _rehash_closure(closure)
    recipe = compile_playable_unit_pack_recipe(descriptor, closure)
    leaf = recipe["runtimeRegistration"]["visual"]["authoredVisualLeaves"][0]
    assert leaf["source"] == source
    assert leaf["runtimeSupport"] == "converted-unbound"
    resource = next(
        row for row in recipe["resources"] if row["id"] == leaf["resourceId"]
    )
    assert resource["converter"] == "texture"
    assert resource["patterns"] == [source]


def test_recipe_resources_load_through_canonical_import_profile(tmp_path) -> None:
    descriptor = _descriptor("HeroUnit")
    closure = _closure(descriptor)
    member = descriptor["composition"]["primaryMemberObjectId"]
    closure["exactLeaves"].append(
        _leaf(
            member,
            "AttachedSword",
            "attached-model",
            "art/w3d/fi/attached_sword.w3d",
            ["USER_1"],
            "ModelConditionState USER_1",
        )
    )
    _rehash_closure(closure)
    recipe = compile_playable_unit_pack_recipe(descriptor, closure)
    profile_path = tmp_path / "profile.json"
    profile_path.write_text(
        json.dumps(
            {
                "format": 1,
                "id": "playable-unit-fixture",
                "title": "Playable unit fixture",
                "pack": {"id": "playable-unit-fixture", "version": "0.1.0"},
                "resources": recipe["resources"],
                "runtime_data": {},
            }
        ),
        encoding="utf-8",
    )
    profile = ImportProfile.load(profile_path)
    assert len(profile.resources) == len(recipe["resources"])


@pytest.mark.parametrize(
    ("identifier", "reason", "effect"),
    [
        ("None", "sage-none-model", "hide-draw-module"),
        ("MODEL", "sage-model-skeleton", "use-active-model-skeleton"),
    ],
)
def test_semantic_visual_state_is_preserved_explicitly(
    identifier: str, reason: str, effect: str
) -> None:
    descriptor = _descriptor("HeroUnit")
    closure = _closure(descriptor)
    member = descriptor["composition"]["primaryMemberObjectId"]
    semantic = _leaf(
        member,
        identifier,
        "model" if reason == "sage-none-model" else "hierarchy",
        "unused",
        ["USER_2"],
        "ModelConditionState USER_2",
    )
    semantic.pop("physicalVirtualPaths")
    semantic["status"] = "semantic"
    semantic["reason"] = reason
    closure["semanticLeaves"].append(semantic)
    _rehash_closure(closure)
    recipe = compile_playable_unit_pack_recipe(descriptor, closure)
    rows = recipe["runtimeRegistration"]["visual"]["authoredVisualSemantics"]
    assert len(rows) == 1
    assert rows[0]["identifier"] == identifier
    assert rows[0]["conditions"] == ["USER_2"]
    assert rows[0]["effect"] == effect
    assert rows[0]["runtimeSupport"] == "required-unimplemented"


def test_rehashed_malformed_semantic_registration_is_rejected() -> None:
    descriptor = _descriptor("HeroUnit")
    closure = _closure(descriptor)
    member = descriptor["composition"]["primaryMemberObjectId"]
    semantic = _leaf(
        member,
        "None",
        "model",
        "unused",
        ["USER_2"],
        "ModelConditionState USER_2",
    )
    semantic.pop("physicalVirtualPaths")
    semantic["status"] = "semantic"
    semantic["reason"] = "sage-none-model"
    closure["semanticLeaves"].append(semantic)
    _rehash_closure(closure)
    original = compile_playable_unit_pack_recipe(descriptor, closure)
    for mutation in ("effect", "provenance", "kind-usage"):
        recipe = deepcopy(original)
        row = recipe["runtimeRegistration"]["visual"]["authoredVisualSemantics"][0]
        if mutation == "effect":
            row["effect"] = "use-active-model-skeleton"
        elif mutation == "provenance":
            row["provenance"] = {}
        else:
            row.pop("kind")
            row.pop("usage")
        unsigned = dict(recipe)
        unsigned.pop("recipeSha256")
        recipe["recipeSha256"] = hashlib.sha256(
            json.dumps(unsigned, sort_keys=True, separators=(",", ":")).encode()
        ).hexdigest()
        with pytest.raises(PlayableUnitPackCompilerError, match="semantic is invalid"):
            validate_playable_unit_pack_recipe(recipe)


def test_rehashed_malformed_unsupported_visual_reference_is_rejected() -> None:
    descriptor = _descriptor("HeroUnit")
    closure = _closure(descriptor)
    member = descriptor["composition"]["primaryMemberObjectId"]
    closure["exactLeaves"].append(
        _leaf(
            member,
            "HeroUnit_EDITOR",
            "model",
            "art/w3d/fi/herounit_editor.w3d",
            ["WORLD_BUILDER"],
            "ModelConditionState WORLD_BUILDER",
        )
    )
    _rehash_closure(closure)
    original = compile_playable_unit_pack_recipe(descriptor, closure)
    for mutation in ("condition", "provenance", "path", "marker"):
        recipe = deepcopy(original)
        row = recipe["runtimeRegistration"]["visual"]["unsupportedVisualReferences"][0]
        if mutation == "condition":
            row["conditions"] = [7]
        elif mutation == "provenance":
            row["provenance"] = {}
        elif mutation == "marker":
            row["runtimeSupport"] = "excluded-hero-form"
        else:
            row["physicalVirtualPaths"] = ["../escaped.w3d"]
        unsigned = dict(recipe)
        unsigned.pop("recipeSha256")
        recipe["recipeSha256"] = hashlib.sha256(
            json.dumps(unsigned, sort_keys=True, separators=(",", ":")).encode()
        ).hexdigest()
        with pytest.raises(
            (PlayableUnitPackCompilerError, ValueError),
            match="unsupported visual|unsafe|relative",
        ):
            validate_playable_unit_pack_recipe(recipe)
