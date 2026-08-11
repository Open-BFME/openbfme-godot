from __future__ import annotations

from copy import deepcopy
import hashlib
import json
from pathlib import PurePosixPath

import pytest

from importer.tests.test_playable_unit_compiler import (
    _documents,
    _weapon_audio_documents,
)
from openbfme_importer.playable_unit_compiler import compile_playable_unit_descriptor
from openbfme_importer.playable_unit_pack_compiler import (
    PlayableUnitPackCompilerError,
    _ability_animation_key,
    _ability_animations,
    _state,
    compile_playable_unit_pack_recipe,
    validate_playable_unit_pack_recipe,
)
from openbfme_importer.profile import ImportProfile


def test_user_form_only_animation_state_is_idle() -> None:
    row = {
        "conditions": ["USER_3"],
        "provenance": {
            "scopePath": [
                "W3DScriptedModelDraw ModuleTag_01",
                "AnimationState USER_3",
                "Animation IdleB",
            ]
        },
    }

    assert _state(row) == "idle"
    assert _state({**row, "conditions": ["USER_3", "SPECIAL_WEAPON_ONE"]}) != "idle"


def test_high_speed_turn_animation_is_not_core_move_state() -> None:
    row = {
        "conditions": ["TURN_RIGHT_HIGH_SPEED"],
        "provenance": {
            "scopePath": [
                "W3DHordeModelDraw ModuleTag_01",
                "AnimationState TURN_RIGHT_HIGH_SPEED",
                "Animation MTurnRight",
            ]
        },
    }
    assert _state(row) is None


def test_moving_turn_animation_keeps_move_classification() -> None:
    row = {
        "conditions": ["MOVING", "TURN_LEFT"],
        "provenance": {
            "scopePath": [
                "W3DHordeModelDraw ModuleTag_01",
                "AnimationState MOVING TURN_LEFT",
                "Animation MMoveTurnLeft",
            ]
        },
    }
    assert _state(row) == "move"


def test_turn_clip_cannot_replace_missing_walk_clip() -> None:
    descriptor = _descriptor("HeroUnit")
    closure = _closure(descriptor)
    closure["exactLeaves"] = [
        row
        for row in closure["exactLeaves"]
        if not (
            row.get("kind") == "animation"
            and "MOVING" in row.get("conditions", [])
        )
    ]
    turn = deepcopy(next(row for row in closure["exactLeaves"] if row.get("kind") == "animation"))
    turn["conditions"] = ["TURN_RIGHT_HIGH_SPEED"]
    closure["exactLeaves"].append(turn)
    _rehash_closure(closure)
    with pytest.raises(PlayableUnitPackCompilerError, match="move"):
        compile_playable_unit_pack_recipe(descriptor, closure)


def _descriptor(
    target: str, documents: dict[str, bytes] | None = None
) -> dict[str, object]:
    if documents is None:
        documents = _documents()
    first = compile_playable_unit_descriptor(target, documents)
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
        documents,
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


def _missing_animation_reference(
    leaf: dict[str, object],
    identifier: str,
    conditions: list[str],
    scope: str,
) -> dict[str, object]:
    """A closure-diagnosed retail-absent animation reference for one slot."""

    row = deepcopy(leaf)
    row["identifier"] = identifier
    row["conditions"] = conditions
    row["status"] = "missing"
    row["reason"] = "missing W3D animation reference"
    row.pop("physicalVirtualPaths", None)
    row.pop("evidence", None)
    provenance = dict(row["provenance"])
    provenance["scopePath"] = [provenance["scopePath"][0], scope]
    row["provenance"] = provenance
    return row


def _static_shell_closure(descriptor: dict[str, object]) -> dict[str, object]:
    """Drop every authored AnimationState, like a retail randomizer shell."""

    closure = _closure(descriptor)
    animation_paths = {
        path
        for row in closure["exactLeaves"]
        if row["kind"] == "animation"
        for path in row["physicalVirtualPaths"]
    }
    closure["exactLeaves"] = [
        row for row in closure["exactLeaves"] if row["kind"] != "animation"
    ]
    closure["scannedW3d"] = [
        row
        for row in closure["scannedW3d"]
        if row["virtualPath"] not in animation_paths
    ]
    embedded = closure["w3dDependencyClosure"]["embeddedTextures"]
    closure["w3dDependencyClosure"]["embeddedTextures"] = [
        row
        for row in embedded
        if row["sourceW3dVirtualPath"] not in animation_paths
    ]
    _rehash_closure(closure)
    return closure


def _mounted_closure(descriptor: dict[str, object]) -> dict[str, object]:
    """A mount + hidden-member closure: the container authors every visual."""

    composition = descriptor["composition"]
    member = str(composition["primaryMemberObjectId"])
    container = str(composition["containerObjectId"])
    slug = container.casefold()
    model = f"art/w3d/fi/{slug}_skn.w3d"
    paths = {
        "idle": f"art/w3d/fi/{slug}_idla.w3d",
        "move": f"art/w3d/fi/{slug}_runa.w3d",
        "attack": f"art/w3d/fi/{slug}_ataka.w3d",
        "death": f"art/w3d/fi/{slug}_diea.w3d",
    }
    leaves = [
        _leaf(
            container, "MountModel", "model", model, [], "DefaultModelConditionState"
        ),
        _leaf(
            container,
            f"{container}_IDLA",
            "animation",
            paths["idle"],
            [],
            "IdleAnimationState",
        ),
        _leaf(
            container,
            f"{container}_RUNA",
            "animation",
            paths["move"],
            ["MOVING"],
            "AnimationState MOVING",
        ),
        _leaf(
            container,
            f"{container}_ATAKA",
            "animation",
            paths["attack"],
            ["FIRING_A"],
            "AnimationState FIRING_A",
        ),
        _leaf(
            container,
            f"{container}_DIEA",
            "animation",
            paths["death"],
            ["DYING", "DEATH_1"],
            "AnimationState DYING DEATH_1",
        ),
        _leaf(
            member,
            "HordeMarkFixture",
            "model",
            f"art/w3d/fi/{slug}_mark.w3d",
            ["WORLD_BUILDER"],
            "ModelConditionState WORLD_BUILDER",
        ),
    ]
    hidden = _leaf(member, "None", "model", "unused", [], "DefaultModelConditionState")
    hidden.pop("physicalVirtualPaths")
    hidden["status"] = "semantic"
    hidden["reason"] = "sage-none-model"
    hierarchy = f"{container}_SKL".upper()
    scanned = [
        {
            "virtualPath": model,
            "byteLength": 1,
            "sha256": "0" * 64,
            "headerIds": {
                "virtualPath": model,
                "modelIds": [f"{container}_SKN"],
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
                    "modelIds": [],
                    "hierarchyIds": [],
                    "animationIds": [f"{hierarchy}.{container}_{state.upper()}"],
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
        "semanticLeaves": [hidden],
        "unresolved": {"graphDiagnostics": [], "references": []},
        "scannedW3d": scanned,
        "w3dDependencyClosure": dependency,
        "summary": {"ready": True},
    }
    closure["aggregateSha256"] = hashlib.sha256(
        json.dumps(closure, sort_keys=True, separators=(",", ":")).encode()
    ).hexdigest()
    return closure


def _mounted_descriptor() -> dict[str, object]:
    """A horde descriptor whose member authors no default model of its own."""

    descriptor = _descriptor("InfantryHorde")
    descriptor["presentation"]["visualRoots"] = [
        {
            "expression": identifier,
            "id": identifier,
            "line": line,
            "sourceIni": "data/ini/object/units/test_units.ini",
        }
        for line, identifier in enumerate(
            ("None", "HordeMarkFixture", "MountModel"), start=1
        )
    ]
    _rehash_descriptor(descriptor)
    return descriptor


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
            "w3d-static",
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


def test_zero_byte_retail_animation_placeholder_is_excluded_from_conversion() -> None:
    """Retail zero-byte W3Ds (rugimli_idlg) must not enter w3d-bundle animations."""

    descriptor = _descriptor("HeroUnit")
    closure = _closure(descriptor)
    idle = next(
        row
        for row in closure["exactLeaves"]
        if row["kind"] == "animation" and not row["conditions"]
    )
    placeholder_path = "art/w3d/fi/herounit_idlg.w3d"
    placeholder_leaf = deepcopy(idle)
    placeholder_leaf["identifier"] = "HEROUNIT_SKL.HEROUNIT_IDLG"
    placeholder_leaf["physicalVirtualPaths"] = [placeholder_path]
    closure["exactLeaves"].append(placeholder_leaf)
    closure["scannedW3d"].append(
        {
            "virtualPath": placeholder_path,
            "byteLength": 0,
            "sha256": "0" * 64,
            "headerIds": {
                "virtualPath": placeholder_path,
                "modelIds": [],
                "hierarchyIds": [],
                "animationIds": [],
            },
            "modelReferences": [],
            "warnings": [],
        }
    )
    _rehash_closure(closure)

    recipe = compile_playable_unit_pack_recipe(descriptor, closure)
    visual = next(
        row
        for row in recipe["resources"]
        if row.get("converter") == "w3d-bundle"
        and str(row.get("id", "")).endswith("visual-00")
    )
    animations = list(visual["options"]["animations"])
    assert "herounit_idlg.w3d" not in {name.casefold() for name in animations}
    assert "herounit_idla.w3d" in {name.casefold() for name in animations}
    authored = recipe["runtimeRegistration"]["visual"]["authoredAnimationStates"]
    excluded = [
        row
        for row in authored
        if str(row.get("sourceW3d", "")).casefold() == placeholder_path.casefold()
    ]
    assert len(excluded) == 1
    assert excluded[0]["runtimeSupport"] == "excluded-zero-byte-placeholder"
    assert (
        excluded[0]["runtimeExclusionReason"] == "zero-byte-retail-w3d-placeholder"
    )


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


def test_galadriel_ring_hero_user_1_dark_skin_is_converted_not_excluded() -> None:
    descriptor = _descriptor("HeroUnit")
    closure = _closure(descriptor)
    member = descriptor["composition"]["primaryMemberObjectId"]
    path = "art/w3d/eu/eugaldrl_skn.w3d"
    closure["exactLeaves"].append(
        _leaf(
            member,
            "EUGaldrl_SKN",
            "model",
            path,
            ["USER_1"],
            "ModelConditionState USER_1",
        )
    )
    closure["scannedW3d"].append(
        {
            "virtualPath": path,
            "byteLength": 1,
            "sha256": "4" * 64,
            "headerIds": {
                "virtualPath": path,
                "modelIds": ["EUGaldrl_SKN"],
                "hierarchyIds": [],
                "animationIds": [],
            },
            "modelReferences": [],
            "warnings": [],
        }
    )
    _rehash_closure(closure)

    recipe = compile_playable_unit_pack_recipe(descriptor, closure)

    visual = recipe["runtimeRegistration"]["visual"]
    assert not any(
        row.get("identifier") == "EUGaldrl_SKN"
        and row.get("runtimeSupport") == "excluded-hero-form"
        for row in visual["unsupportedVisualReferences"]
    )
    resource = next(
        row
        for row in recipe["resources"]
        if row["kind"] == "model"
        and path in row["patterns"]
    )
    component = next(
        row for row in visual["components"] if row["sourceW3d"] == path
    )
    assert component["conditions"] == ["USER_1"]


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


def test_mounted_container_payload_converts_explicitly() -> None:
    descriptor = _mounted_descriptor()
    composition = descriptor["composition"]
    member = str(composition["primaryMemberObjectId"])
    container = str(composition["containerObjectId"])
    recipe = compile_playable_unit_pack_recipe(
        descriptor, _mounted_closure(descriptor)
    )
    validate_playable_unit_pack_recipe(recipe)
    visual = recipe["runtimeRegistration"]["visual"]
    assert visual["presentationComposition"] == {
        "form": "mounted-container-payload",
        "visualPrimaryObjectId": container,
        "hiddenMemberObjectId": member,
    }
    defaults = [row for row in visual["components"] if row["default"]]
    assert len(defaults) == 1
    assert defaults[0]["ownerObjectId"] == container
    assert defaults[0]["role"] == "primary-container"
    assert set(visual["coreAnimations"]) == {"idle", "move", "attack", "death"}
    for rows in visual["coreAnimations"].values():
        assert all(row["ownerObjectId"] == container for row in rows)
    semantics = visual["authoredVisualSemantics"]
    assert [
        (row["targetObject"], row["reason"], row["conditions"], row["effect"])
        for row in semantics
    ] == [(member, "sage-none-model", [], "hide-draw-module")]
    editor_only = visual["unsupportedVisualReferences"]
    assert [
        (row["identifier"], row["runtimeSupport"], row["runtimeExclusionReason"])
        for row in editor_only
    ] == [("HordeMarkFixture", "excluded-editor-only", "world-builder-only")]


def test_mounted_presentation_requires_authored_hidden_member() -> None:
    descriptor = _mounted_descriptor()
    closure = _mounted_closure(descriptor)
    closure["semanticLeaves"] = []
    _rehash_closure(closure)
    with pytest.raises(
        PlayableUnitPackCompilerError, match="does not identify one default model"
    ):
        compile_playable_unit_pack_recipe(descriptor, closure)


def test_mounted_presentation_requires_one_mount_default_model() -> None:
    descriptor = _mounted_descriptor()
    composition = descriptor["composition"]
    container = str(composition["containerObjectId"])
    closure = _mounted_closure(descriptor)
    closure["exactLeaves"].append(
        _leaf(
            container,
            "SecondMountModel",
            "model",
            "art/w3d/fi/second_mount_skn.w3d",
            [],
            "DefaultModelConditionState",
            "W3DModelDraw ModuleTag_Aux",
        )
    )
    _rehash_closure(closure)
    with pytest.raises(
        PlayableUnitPackCompilerError, match="does not identify one default model"
    ):
        compile_playable_unit_pack_recipe(descriptor, closure)


def test_rehashed_mounted_presentation_composition_is_rejected() -> None:
    descriptor = _mounted_descriptor()
    original = compile_playable_unit_pack_recipe(
        descriptor, _mounted_closure(descriptor)
    )
    for mutation in ("marker-absent", "marker-member", "marker-role"):
        recipe = deepcopy(original)
        visual = recipe["runtimeRegistration"]["visual"]
        if mutation == "marker-absent":
            visual.pop("presentationComposition")
        elif mutation == "marker-member":
            visual["presentationComposition"]["hiddenMemberObjectId"] = "ForgedMember"
        else:
            default = next(row for row in visual["components"] if row["default"])
            default["role"] = "primary-member"
        unsigned = dict(recipe)
        unsigned.pop("recipeSha256")
        recipe["recipeSha256"] = hashlib.sha256(
            json.dumps(unsigned, sort_keys=True, separators=(",", ":")).encode()
        ).hexdigest()
        with pytest.raises(
            PlayableUnitPackCompilerError, match="presentation composition"
        ):
            validate_playable_unit_pack_recipe(recipe)


def test_forged_mount_marker_on_member_owned_recipe_is_rejected() -> None:
    descriptor = _descriptor("HeroUnit")
    recipe = compile_playable_unit_pack_recipe(descriptor, _closure(descriptor))
    composition = descriptor["composition"]
    recipe["runtimeRegistration"]["visual"]["presentationComposition"] = {
        "form": "mounted-container-payload",
        "visualPrimaryObjectId": composition["containerObjectId"],
        "hiddenMemberObjectId": composition["primaryMemberObjectId"],
    }
    unsigned = dict(recipe)
    unsigned.pop("recipeSha256")
    recipe["recipeSha256"] = hashlib.sha256(
        json.dumps(unsigned, sort_keys=True, separators=(",", ":")).encode()
    ).hexdigest()
    with pytest.raises(
        PlayableUnitPackCompilerError, match="presentation composition"
    ):
        validate_playable_unit_pack_recipe(recipe)


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


def test_embedded_animation_auxiliary_model_is_an_embedded_bundle() -> None:
    descriptor = _descriptor("InfantryHorde")
    closure = _closure(descriptor)
    for row in closure["scannedW3d"]:
        if row["virtualPath"].endswith("_aux1.w3d"):
            row["headerIds"]["hierarchyIds"] = ["FIXTURE_AUX_SKL"]
            row["headerIds"]["animationIds"] = ["FIXTURE_AUX_SKL"]
            row["embeddedAnimationChannelCount"] = 4
    _rehash_closure(closure)

    recipe = compile_playable_unit_pack_recipe(descriptor, closure)

    aux = next(
        row
        for row in recipe["resources"]
        if row["id"] == "unit-infantryhorde-visual-01"
    )
    assert aux["converter"] == "w3d-bundle"
    assert aux["options"]["animations"] == ["infantrymember_aux1.w3d"]
    validate_playable_unit_pack_recipe(recipe)


def test_hierarchy_less_auxiliary_model_is_a_static_conversion() -> None:
    descriptor = _descriptor("InfantryHorde")
    recipe = compile_playable_unit_pack_recipe(descriptor, _closure(descriptor))
    aux = next(
        row
        for row in recipe["resources"]
        if row["id"] == "unit-infantryhorde-visual-01"
    )
    assert aux["converter"] == "w3d-static"
    assert "animations" not in aux["options"]
    assert aux["options"]["model"] == "infantrymember_aux1.w3d"
    validate_playable_unit_pack_recipe(recipe)


def test_external_model_hierarchy_is_staged_with_the_model() -> None:
    descriptor = _descriptor("HeroUnit")
    closure = _closure(descriptor)
    member = descriptor["composition"]["primaryMemberObjectId"]
    slug = str(member).casefold()
    model_path = f"art/w3d/fi/{slug}_skn.w3d"
    hierarchy_path = f"art/w3d/fi/{slug}_skl.w3d"
    hierarchy = f"{member}_SKL".upper()
    for row in closure["scannedW3d"]:
        if row["virtualPath"] == model_path:
            row["headerIds"]["hierarchyIds"] = []
            row["modelHierarchyIdentifiers"] = [hierarchy]
    closure["scannedW3d"].append(
        {
            "virtualPath": hierarchy_path,
            "byteLength": 1,
            "sha256": hashlib.sha256(hierarchy_path.encode()).hexdigest(),
            "headerIds": {
                "virtualPath": hierarchy_path,
                "modelIds": [],
                "hierarchyIds": [hierarchy],
                "animationIds": [],
            },
            "modelReferences": [],
            "warnings": [],
        }
    )
    _rehash_closure(closure)

    recipe = compile_playable_unit_pack_recipe(descriptor, closure)

    visual_models = [
        row
        for row in recipe["resources"]
        if row["kind"] == "model" and row["options"].get("model") == PurePosixPath(model_path).name
    ]
    assert len(visual_models) == 1
    assert hierarchy_path in visual_models[0]["patterns"]
    assert visual_models[0]["patterns"].count(hierarchy_path) == 1
    validate_playable_unit_pack_recipe(recipe)


def test_external_model_hierarchy_requires_a_unique_provider() -> None:
    descriptor = _descriptor("HeroUnit")
    closure = _closure(descriptor)
    member = descriptor["composition"]["primaryMemberObjectId"]
    slug = str(member).casefold()
    model_path = f"art/w3d/fi/{slug}_skn.w3d"
    for row in closure["scannedW3d"]:
        if row["virtualPath"] == model_path:
            row["modelHierarchyIdentifiers"] = ["OTHER_SKL"]
    _rehash_closure(closure)
    with pytest.raises(
        PlayableUnitPackCompilerError, match="hierarchy ownership is not unique"
    ):
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


@pytest.mark.parametrize(
    ("object_id", "condition"),
    [
        ("DwarvenBanner", "USER_4"),
        ("DwarvenPhalanxBanner", "USER_6"),
        ("MenofDaleBanner", "USER_3"),
    ],
)
def test_dwarven_morph_banner_uses_authored_carrier_form_as_default(
    object_id: str, condition: str
) -> None:
    descriptor = _descriptor("InfantryMember")
    descriptor["composition"]["primaryMemberObjectId"] = object_id
    descriptor["composition"]["containerObjectId"] = object_id
    for member in descriptor["composition"]["members"]:
        member["objectId"] = object_id
    _rehash_descriptor(descriptor)
    closure = _closure(descriptor)
    model = next(row for row in closure["exactLeaves"] if row["kind"] == "model")
    model["conditions"] = [condition]
    hidden = _leaf(
        object_id,
        "None",
        "model",
        "unused",
        [],
        "DefaultModelConditionState",
    )
    hidden.pop("physicalVirtualPaths")
    hidden["status"] = "semantic"
    hidden["reason"] = "sage-none-model"
    closure["semanticLeaves"].append(hidden)
    _rehash_closure(closure)

    recipe = compile_playable_unit_pack_recipe(descriptor, closure)
    defaults = [
        row
        for row in recipe["runtimeRegistration"]["visual"]["components"]
        if row["default"]
    ]
    assert len(defaults) == 1
    assert defaults[0]["conditions"] == [condition]


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


def test_editor_only_visual_root_is_an_explicit_exclusion() -> None:
    descriptor = _descriptor("InfantryHorde")
    closure = _closure(descriptor)
    container = descriptor["composition"]["containerObjectId"]
    root = deepcopy(descriptor["presentation"]["visualRoots"][0])
    root["id"] = "HordeMarkFIX"
    root["expression"] = "HordeMarkFIX"
    descriptor["presentation"]["visualRoots"].append(root)
    _rehash_descriptor(descriptor)
    closure["exactLeaves"].append(
        _leaf(
            container,
            "HordeMarkFIX",
            "model",
            "art/w3d/ho/hordemarkfix.w3d",
            ["WORLD_BUILDER"],
            "ModelConditionState WORLD_BUILDER",
            "W3DModelDraw ModuleTag_WorldBuilder",
        )
    )
    _rehash_closure(closure)

    recipe = compile_playable_unit_pack_recipe(descriptor, closure)

    visual = recipe["runtimeRegistration"]["visual"]
    excluded = next(
        row
        for row in visual["unsupportedVisualReferences"]
        if row["identifier"] == "HordeMarkFIX"
    )
    assert excluded["runtimeSupport"] == "excluded-editor-only"
    assert excluded["runtimeExclusionReason"] == "world-builder-only"
    validate_playable_unit_pack_recipe(recipe)


def test_unauthored_visual_root_still_fails_closed() -> None:
    descriptor = _descriptor("InfantryHorde")
    closure = _closure(descriptor)
    root = deepcopy(descriptor["presentation"]["visualRoots"][0])
    root["id"] = "HordeMarkFIX"
    root["expression"] = "HordeMarkFIX"
    descriptor["presentation"]["visualRoots"].append(root)
    _rehash_descriptor(descriptor)
    with pytest.raises(PlayableUnitPackCompilerError, match="missing authored roots"):
        compile_playable_unit_pack_recipe(descriptor, closure)


def test_retail_absent_animation_variant_is_an_explicit_exclusion() -> None:
    descriptor = _descriptor("HeroUnit")
    closure = _closure(descriptor)
    death_leaf = next(
        row
        for row in closure["exactLeaves"]
        if row["kind"] == "animation" and "DYING" in row["conditions"]
    )
    closure["unresolved"]["references"].append(
        _missing_animation_reference(
            death_leaf,
            "HEROUNIT_IDLG",
            ["DYING", "DEATH_2"],
            "AnimationState DYING DEATH_2",
        )
    )
    closure["summary"]["ready"] = False
    _rehash_closure(closure)

    recipe = compile_playable_unit_pack_recipe(descriptor, closure)

    visual = recipe["runtimeRegistration"]["visual"]
    row = next(
        row
        for row in visual["unsupportedVisualReferences"]
        if row["identifier"] == "HEROUNIT_IDLG"
    )
    assert row["runtimeSupport"] == "excluded-retail-absent-animation-gap"
    assert row["runtimeExclusionReason"] == "retail-absent-animation-state-covered"
    assert row["semanticState"] == "death"
    validate_playable_unit_pack_recipe(recipe)


def test_retail_absent_default_extra_mesh_is_explicit_when_primary_is_resolved() -> None:
    descriptor = _descriptor("HeroUnit")
    closure = _closure(descriptor)
    primary = next(row for row in closure["exactLeaves"] if row["kind"] == "model")
    missing = deepcopy(primary)
    missing.update(
        {
            "identifier": "HEROUNIT_MISSING_EXTRA",
            "kind": "extra-mesh",
            "usage": "extra-mesh",
            "status": "missing",
            "reason": "missing W3D extra-mesh reference",
        }
    )
    missing.pop("physicalVirtualPaths", None)
    missing.pop("evidence", None)
    closure["unresolved"]["references"].append(missing)
    closure["summary"]["ready"] = False
    _rehash_closure(closure)

    recipe = compile_playable_unit_pack_recipe(descriptor, closure)

    row = next(
        row
        for row in recipe["runtimeRegistration"]["visual"]["unsupportedVisualReferences"]
        if row["identifier"] == "HEROUNIT_MISSING_EXTRA"
    )
    assert row["runtimeSupport"] == "excluded-retail-absent-extra-mesh"
    assert row["runtimeExclusionReason"] == (
        "retail-absent-extra-mesh-default-covered"
    )
    validate_playable_unit_pack_recipe(recipe)


def test_retail_absent_extra_mesh_without_resolved_draw_fails_closed() -> None:
    descriptor = _descriptor("HeroUnit")
    closure = _closure(descriptor)
    primary = next(row for row in closure["exactLeaves"] if row["kind"] == "model")
    closure["exactLeaves"].remove(primary)
    missing = deepcopy(primary)
    missing.update(
        {
            "kind": "extra-mesh",
            "usage": "extra-mesh",
            "status": "missing",
            "reason": "missing W3D extra-mesh reference",
        }
    )
    missing.pop("physicalVirtualPaths", None)
    missing.pop("evidence", None)
    closure["unresolved"]["references"].append(missing)
    closure["summary"]["ready"] = False
    _rehash_closure(closure)

    with pytest.raises(PlayableUnitPackCompilerError, match="not conversion-ready"):
        compile_playable_unit_pack_recipe(descriptor, closure)


def test_retail_absent_random_texture_is_explicit_when_default_is_resolved() -> None:
    descriptor = _descriptor("HeroUnit")
    closure = _closure(descriptor)
    primary = next(row for row in closure["exactLeaves"] if row["kind"] == "model")
    missing = deepcopy(primary)
    missing.update(
        {
            "identifier": "IUWargSntryA.tga",
            "kind": "texture",
            "usage": "random-texture",
            "status": "missing",
            "reason": "no exact catalog candidate",
        }
    )
    missing.pop("physicalVirtualPaths", None)
    missing.pop("evidence", None)
    closure["unresolved"]["references"].append(missing)
    closure["summary"]["ready"] = False
    _rehash_closure(closure)

    recipe = compile_playable_unit_pack_recipe(descriptor, closure)

    row = next(
        row
        for row in recipe["runtimeRegistration"]["visual"]["unsupportedVisualReferences"]
        if row["identifier"] == "IUWargSntryA.tga"
    )
    assert row["runtimeSupport"] == "excluded-retail-absent-random-texture"
    assert row["runtimeExclusionReason"] == (
        "retail-absent-random-texture-default-covered"
    )
    validate_playable_unit_pack_recipe(recipe)


def test_retail_absent_random_texture_without_same_draw_default_fails() -> None:
    descriptor = _descriptor("HeroUnit")
    closure = _closure(descriptor)
    primary = next(row for row in closure["exactLeaves"] if row["kind"] == "model")
    missing = deepcopy(primary)
    missing.update(
        {
            "identifier": "UnrelatedVariant.tga",
            "kind": "texture",
            "usage": "random-texture",
            "status": "missing",
            "reason": "no exact catalog candidate",
            "targetObject": "UnrelatedObject",
        }
    )
    missing.pop("physicalVirtualPaths", None)
    missing.pop("evidence", None)
    closure["unresolved"]["references"].append(missing)
    closure["summary"]["ready"] = False
    _rehash_closure(closure)

    with pytest.raises(PlayableUnitPackCompilerError, match="not conversion-ready"):
        compile_playable_unit_pack_recipe(descriptor, closure)


def test_retail_absent_animation_gap_combines_with_non_core_gaps() -> None:
    descriptor = _descriptor("HeroUnit")
    closure = _closure(descriptor)
    animation_leaf = next(
        row for row in closure["exactLeaves"] if row["kind"] == "animation"
    )
    closure["unresolved"]["references"].extend(
        [
            _missing_animation_reference(
                animation_leaf,
                "HEROUNIT_TNTB",
                ["EMOTION_ALERT", "EMOTION_MORALE_HIGH"],
                "AnimationState EMOTION_ALERT EMOTION_MORALE_HIGH",
            ),
            _missing_animation_reference(
                animation_leaf,
                "HEROUNIT_BAKA",
                ["MOUNTED", "MOVING", "BACKING_UP"],
                "AnimationState MOUNTED MOVING BACKING_UP",
            ),
        ]
    )
    closure["summary"]["ready"] = False
    _rehash_closure(closure)

    recipe = compile_playable_unit_pack_recipe(descriptor, closure)

    visual = recipe["runtimeRegistration"]["visual"]
    support = {
        row["identifier"]: row["runtimeSupport"]
        for row in visual["unsupportedVisualReferences"]
    }
    assert support["HEROUNIT_TNTB"] == "excluded-non-core-animation-gap"
    assert support["HEROUNIT_BAKA"] == "excluded-retail-absent-animation-gap"
    validate_playable_unit_pack_recipe(recipe)


def test_retail_absent_animation_without_state_coverage_fails_closed() -> None:
    descriptor = _descriptor("HeroUnit")
    closure = _closure(descriptor)
    death_leaf = next(
        row
        for row in closure["exactLeaves"]
        if row["kind"] == "animation" and "DYING" in row["conditions"]
    )
    missing = _missing_animation_reference(
        death_leaf,
        "HEROUNIT_IDLG",
        ["DYING", "DEATH_2"],
        "AnimationState DYING DEATH_2",
    )
    closure["exactLeaves"] = [
        row
        for row in closure["exactLeaves"]
        if row["kind"] != "animation" or "DYING" not in row["conditions"]
    ]
    closure["unresolved"]["references"].append(missing)
    closure["summary"]["ready"] = False
    _rehash_closure(closure)

    with pytest.raises(PlayableUnitPackCompilerError, match="conversion-ready"):
        compile_playable_unit_pack_recipe(descriptor, closure)


def test_rehashed_retail_absent_exclusion_is_rejected() -> None:
    descriptor = _descriptor("HeroUnit")
    closure = _closure(descriptor)
    death_leaf = next(
        row
        for row in closure["exactLeaves"]
        if row["kind"] == "animation" and "DYING" in row["conditions"]
    )
    closure["unresolved"]["references"].append(
        _missing_animation_reference(
            death_leaf,
            "HEROUNIT_IDLG",
            ["DYING", "DEATH_2"],
            "AnimationState DYING DEATH_2",
        )
    )
    closure["summary"]["ready"] = False
    _rehash_closure(closure)
    original = compile_playable_unit_pack_recipe(descriptor, closure)
    for mutation in ("state", "reason", "diagnosis"):
        recipe = deepcopy(original)
        row = next(
            row
            for row in recipe["runtimeRegistration"]["visual"][
                "unsupportedVisualReferences"
            ]
            if row["identifier"] == "HEROUNIT_IDLG"
        )
        if mutation == "state":
            row["semanticState"] = "move"
        elif mutation == "reason":
            row["runtimeExclusionReason"] = "unmapped-runtime-state"
        else:
            row.pop("reason")
        unsigned = dict(recipe)
        unsigned.pop("recipeSha256")
        recipe["recipeSha256"] = hashlib.sha256(
            json.dumps(unsigned, sort_keys=True, separators=(",", ":")).encode()
        ).hexdigest()
        with pytest.raises(PlayableUnitPackCompilerError, match="unsupported visual"):
            validate_playable_unit_pack_recipe(recipe)


def test_randomizer_shell_records_explicit_core_animation_exclusions() -> None:
    descriptor = _descriptor("MonsterUnit")
    recipe = compile_playable_unit_pack_recipe(
        descriptor, _static_shell_closure(descriptor)
    )

    visual = recipe["runtimeRegistration"]["visual"]
    assert visual["coreAnimations"] == {}
    assert visual["coreAnimationExclusions"] == [
        {
            "state": state,
            "runtimeSupport": "excluded-randomizer-shell",
            "runtimeExclusionReason": "zero-authored-animation-states",
        }
        for state in ("idle", "move", "attack", "death")
    ]
    assert visual["authoredAnimationStates"] == []
    assert sum(row["default"] is True for row in visual["components"]) == 1
    validate_playable_unit_pack_recipe(recipe)


def test_shell_with_authored_animation_is_not_a_randomizer_shell() -> None:
    descriptor = _descriptor("MonsterUnit")
    closure = _static_shell_closure(descriptor)
    member = descriptor["composition"]["primaryMemberObjectId"]
    path = "art/w3d/fi/monsterunit_celebrate.w3d"
    closure["exactLeaves"].append(
        _leaf(
            member,
            "MonsterUnit_CELEBRATE",
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
                "animationIds": ["MONSTERUNIT_SKL.MONSTERUNIT_CELEBRATE"],
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

    with pytest.raises(
        PlayableUnitPackCompilerError, match="no resolved core animations"
    ):
        compile_playable_unit_pack_recipe(descriptor, closure)


def test_unready_shell_closure_is_not_a_randomizer_shell() -> None:
    descriptor = _descriptor("MonsterUnit")
    closure = _static_shell_closure(descriptor)
    closure["summary"]["ready"] = False
    _rehash_closure(closure)

    with pytest.raises(
        PlayableUnitPackCompilerError, match="no resolved core animations"
    ):
        compile_playable_unit_pack_recipe(descriptor, closure)


def test_rehashed_randomizer_shell_exclusions_are_rejected() -> None:
    descriptor = _descriptor("MonsterUnit")
    original = compile_playable_unit_pack_recipe(
        descriptor, _static_shell_closure(descriptor)
    )
    for mutation in ("dropped-state", "support", "authored-state"):
        recipe = deepcopy(original)
        visual = recipe["runtimeRegistration"]["visual"]
        if mutation == "dropped-state":
            visual["coreAnimationExclusions"] = [
                row
                for row in visual["coreAnimationExclusions"]
                if row["state"] != "death"
            ]
        elif mutation == "support":
            visual["coreAnimationExclusions"][0]["runtimeSupport"] = "supported"
        else:
            visual["authoredAnimationStates"] = [
                {
                    "sourceW3d": "art/w3d/fi/monsterunit_idla.w3d",
                    "identifier": "MonsterUnit_IDLA",
                    "conditions": [],
                    "modelSourceW3d": "art/w3d/fi/monsterunit_skn.w3d",
                    "ownerObjectId": "MonsterUnit",
                    "drawModule": "ModuleTag_Draw",
                    "semanticState": "idle",
                    "runtimeSupport": "generic-core",
                }
            ]
        unsigned = dict(recipe)
        unsigned.pop("recipeSha256")
        recipe["recipeSha256"] = hashlib.sha256(
            json.dumps(unsigned, sort_keys=True, separators=(",", ":")).encode()
        ).hexdigest()
        with pytest.raises(
            PlayableUnitPackCompilerError, match="core animation exclusions"
        ):
            validate_playable_unit_pack_recipe(recipe)


def test_ability_animation_key_names_special_weapon_states() -> None:
    # gandalf.ini:305 authors `AnimationState = SPECIAL_WEAPON_TWO` for
    # GUGandalfG_SKL.GUGandalfG_SPCL, the staff-lowering wizard blast pose.
    assert _ability_animation_key(["SPECIAL_WEAPON_TWO"]) == ("specialWeaponTwo", "cast")
    assert _ability_animation_key(["SPECIAL_WEAPON_ONE"]) == ("specialWeaponOne", "cast")
    assert _ability_animation_key(["SPECIAL_WEAPON_THREE"]) == (
        "specialWeaponThree",
        "cast",
    )
    # theoden.ini:135 is the Men set's only literal USING_SPECIAL_ABILITY.
    assert _ability_animation_key(["MOUNTED", "MOVING", "USING_SPECIAL_ABILITY"]) == (
        "usingSpecialAbility",
        "cast",
    )


def test_ability_animation_key_names_the_four_phase_envelope() -> None:
    # boromir.ini:127-178 authors UNPACKING / PREPARING / PACKING / bare for
    # SPECIAL_POWER_1 (HRNA, HRNB, HRNC, HRNB).
    assert _ability_animation_key(["UNPACKING", "SPECIAL_POWER_1"]) == (
        "specialPower1",
        "unpack",
    )
    assert _ability_animation_key(["PREPARING", "SPECIAL_POWER_1"]) == (
        "specialPower1",
        "prepare",
    )
    assert _ability_animation_key(["PACKING", "SPECIAL_POWER_1"]) == (
        "specialPower1",
        "pack",
    )
    assert _ability_animation_key(["SPECIAL_POWER_1"]) == ("specialPower1", "cast")
    # gandalf.ini:343-370 authors the lightning-sword envelope against
    # PACKING_TYPE_1, which ArrowStormUpdate selects via UnpackingVariation = 1.
    assert _ability_animation_key(["PACKING_TYPE_1", "UNPACKING"]) == (
        "packingType1",
        "unpack",
    )


def test_ability_animation_key_refuses_ambiguous_rows() -> None:
    assert _ability_animation_key([]) is None
    assert _ability_animation_key(["MOVING", "ATTACKING"]) is None
    # Two ability tokens, or two envelope phases, cannot be addressed.
    assert _ability_animation_key(["SPECIAL_WEAPON_ONE", "SPECIAL_POWER_2"]) is None
    assert _ability_animation_key(["UNPACKING", "PACKING", "SPECIAL_POWER_1"]) is None


def test_ability_animations_index_authored_rows_and_skip_exclusions() -> None:
    rows = [
        {
            "identifier": "GUGandalfG_SKL.GUGandalfG_SPCL",
            "conditions": ["SPECIAL_WEAPON_TWO"],
            "sourceW3d": "art/w3d/gu/gugandalfg_spcl.w3d",
            "modelSourceW3d": "art/w3d/gu/gugandalfg_skn.w3d",
            "ownerObjectId": "GondorGandalf",
            "runtimeSupport": "generic-core",
        },
        {
            "identifier": "GUBoromir_SKL.GUBoromir_HRNA",
            "conditions": ["UNPACKING", "SPECIAL_POWER_1"],
            "sourceW3d": "art/w3d/gu/guboromir_hrna.w3d",
            "modelSourceW3d": "art/w3d/gu/guboromir_skn.w3d",
            "ownerObjectId": "GondorBoromir",
            "runtimeSupport": "packaged-unimplemented",
        },
        {
            "identifier": "GUGandalfG_SKL.GUGandalfG_SPCK",
            "conditions": ["SPECIAL_WEAPON_ONE"],
            "sourceW3d": "art/w3d/gu/gugandalfg_spck.w3d",
            "modelSourceW3d": "art/w3d/gu/gugandalfg_skn.w3d",
            "ownerObjectId": "GondorGandalf",
            "runtimeSupport": "excluded-zero-byte-placeholder",
        },
        {
            "identifier": "GUManMocap_ATKA",
            "conditions": ["ATTACKING"],
            "sourceW3d": "art/w3d/gu/gumanmocap_atka.w3d",
            "modelSourceW3d": "art/w3d/gu/gumanmocap_skn.w3d",
            "ownerObjectId": "GondorFighter",
            "runtimeSupport": "generic-core",
        },
    ]
    result = _ability_animations(rows)
    assert sorted(result) == ["specialPower1", "specialWeaponTwo"]
    assert list(result["specialPower1"]) == ["unpack"]
    assert result["specialWeaponTwo"]["cast"][0]["identifier"] == (
        "GUGandalfG_SKL.GUGandalfG_SPCL"
    )
    # An excluded row never becomes addressable, and a generic combat pose is
    # not an ability.
    assert "specialWeaponOne" not in result


def test_ability_animations_are_phase_ordered_and_deterministic() -> None:
    rows = [
        {
            "identifier": "GUBoromir_SKL.GUBoromir_HRN%s" % suffix,
            "conditions": conditions + ["SPECIAL_POWER_1"],
            "sourceW3d": "art/w3d/gu/guboromir_hrn%s.w3d" % suffix.lower(),
            "modelSourceW3d": "art/w3d/gu/guboromir_skn.w3d",
            "ownerObjectId": "GondorBoromir",
            "runtimeSupport": "packaged-unimplemented",
        }
        for suffix, conditions in (
            ("C", ["PACKING"]),
            ("B", []),
            ("A", ["UNPACKING"]),
            ("B", ["PREPARING"]),
        )
    ]
    result = _ability_animations(rows)
    assert list(result["specialPower1"]) == ["unpack", "prepare", "cast", "pack"]
    assert _ability_animations(list(reversed(rows))) == result


def test_donor_source_retention_never_lands_under_assets() -> None:
    # A pack must not ship raw retail `.w3d` at a runtime path. An auxiliary
    # visual leaf whose converter is unimplemented keeps its donor payload for
    # provenance, but under the source-only `sources/` root the pack audit
    # excludes — never under `assets/`, which is runtime content.
    descriptor = _descriptor("HeroUnit")
    closure = _closure(descriptor)
    member = descriptor["composition"]["primaryMemberObjectId"]
    source = "art/w3d/fi/hero_banner.w3d"
    closure["exactLeaves"].append(
        _leaf(
            member,
            "HeroBanner",
            "attachment",
            source,
            ["USER_1"],
            "ModelConditionState USER_1",
        )
    )
    _rehash_closure(closure)
    recipe = compile_playable_unit_pack_recipe(descriptor, closure)
    leaf = next(
        row
        for row in recipe["runtimeRegistration"]["visual"]["authoredVisualLeaves"]
        if row["source"] == source
    )
    assert leaf["runtimeSupport"] == "source-retained-unimplemented"
    assert leaf["output"].startswith("sources/units/")
    assert not leaf["output"].startswith("assets/")
    resource = next(
        row for row in recipe["resources"] if row["id"] == leaf["resourceId"]
    )
    assert resource["converter"] == "copy"
    assert resource["output"] == leaf["output"]
    for row in recipe["resources"]:
        output = str(row.get("output", ""))
        if PurePosixPath(output).suffix.casefold() == ".w3d":
            assert not output.startswith("assets/"), output


def test_retail_unlocalized_command_string_is_recorded_not_a_broken_pack() -> None:
    # Oracle: RotWK layered data/ini/commandbutton.ini authors
    # Command_ConstructMordorBlackRiderHorde with
    # DescriptLabel = CONTROLBAR:ConstructBlackRiderHorde, and no retail .str
    # file defines that id. The resolved-strings table is therefore legitimately
    # narrower than the command-referenced set. Before this contract existed the
    # runtime read the hole as a broken document and dropped
    # MordorBlackRiderHorde from the roster entirely, leaving a dead fortress
    # button.
    descriptor = _descriptor("InfantryHorde")
    unlocalized = "CONTROLBAR:ToolTipInfantryHorde"
    descriptor["presentation"]["resolvedStrings"].pop(unlocalized)
    descriptor["presentation"]["sourceNullStringIds"] = [unlocalized]
    _rehash_descriptor(descriptor)

    recipe = compile_playable_unit_pack_recipe(descriptor, _closure(descriptor))
    validate_playable_unit_pack_recipe(recipe)
    runtime = recipe["runtimeRegistration"]
    assert runtime["sourceNullStringIds"] == [unlocalized]
    assert unlocalized not in runtime["stringBindings"]
    assert "CONTROLBAR:InfantryHorde" in runtime["stringBindings"]


def test_command_string_that_is_neither_bound_nor_retail_null_fails_closed() -> None:
    # The exemption above must not become a hole: an id that is simply absent,
    # with no retail-unlocalized evidence, is still a broken pack.
    descriptor = _descriptor("InfantryHorde")
    descriptor["presentation"]["resolvedStrings"].pop("CONTROLBAR:ToolTipInfantryHorde")
    _rehash_descriptor(descriptor)

    recipe = compile_playable_unit_pack_recipe(descriptor, _closure(descriptor))
    with pytest.raises(PlayableUnitPackCompilerError, match="localized string bindings"):
        validate_playable_unit_pack_recipe(recipe)


def test_retail_null_string_may_not_also_be_bound() -> None:
    # Claiming an id is both resolved AND retail-unlocalized is a contradiction;
    # the descriptor gate rejects it before a recipe can be built from it.
    from openbfme_importer.playable_unit_compiler import PlayableUnitCompilerError

    descriptor = _descriptor("InfantryHorde")
    descriptor["presentation"]["sourceNullStringIds"] = ["CONTROLBAR:InfantryHorde"]
    _rehash_descriptor(descriptor)

    with pytest.raises(PlayableUnitCompilerError, match="source-null strings"):
        compile_playable_unit_pack_recipe(descriptor, _closure(descriptor))


def test_zero_string_unit_still_needs_full_command_string_coverage() -> None:
    # Hole: the union check was guarded by `bool(string_bindings)`, so a unit
    # whose every command string failed to resolve -- the WORST case, a fully
    # text-dead button set -- skipped the gate entirely and shipped.
    descriptor = _descriptor("InfantryHorde")
    descriptor["presentation"]["resolvedStrings"].clear()
    _rehash_descriptor(descriptor)

    recipe = compile_playable_unit_pack_recipe(descriptor, _closure(descriptor))
    assert recipe["runtimeRegistration"]["stringBindings"] == {}
    with pytest.raises(
        PlayableUnitPackCompilerError, match="localized string bindings"
    ):
        validate_playable_unit_pack_recipe(recipe)


def test_ability_button_string_must_be_bound_or_retail_null() -> None:
    # Hole: ability button label/tooltip ids were admitted as legal members of
    # sourceNullStringIds but were never themselves required to be covered by
    # the bound-or-retail-null union, so a hero ability could ship with a blank
    # tooltip and a green pack.
    from openbfme_importer.playable_unit_pack_compiler import _digest

    descriptor = _descriptor("InfantryHorde")
    recipe = compile_playable_unit_pack_recipe(descriptor, _closure(descriptor))
    recipe["runtimeRegistration"]["abilities"] = [
        {
            "button": {
                "labelIds": ["CONTROLBAR:UnboundAbilityLabel"],
                "tooltipIds": [],
            }
        }
    ]
    unsigned = dict(recipe)
    unsigned.pop("recipeSha256")
    recipe["recipeSha256"] = _digest(unsigned)

    with pytest.raises(
        PlayableUnitPackCompilerError, match="localized string bindings"
    ):
        validate_playable_unit_pack_recipe(recipe)


def test_weapon_audio_routes_ship_sample_bindings() -> None:
    ## The `weapon` owner (Weapon -> FireFX/ProjectileDetonationFX -> FXList
    ## -> Sound) must ship: its AudioEvents become required audio bindings so
    ## the runtime can play the authored swing/impact instead of the class
    ## fallback.
    descriptor = _descriptor("Swordsman", _weapon_audio_documents())
    recipe = compile_playable_unit_pack_recipe(descriptor, _closure(descriptor))
    validate_playable_unit_pack_recipe(recipe)
    runtime = recipe["runtimeRegistration"]
    weapon_routes = runtime["audioRoutes"]["weapon"]
    assert {row["id"] for row in weapon_routes["FireFX"]} == {"ImpactSwordFx"}
    assert {row["id"] for row in weapon_routes["ProjectileDetonationFX"]} == {
        "ImpactArrowFx"
    }
    for event_id in ("ImpactSwordFx", "ImpactArrowFx"):
        assert runtime["audioBindings"][event_id]
        assert runtime["audioResolution"][event_id] == "samples"
