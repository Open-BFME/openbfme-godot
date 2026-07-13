"""Blender-side adapter: OpenSAGE W3D model + clips -> one Godot-ready GLB.

Executed only by the pinned portable Blender process.  It intentionally has no
retail path defaults and writes only the single coordinator-provided output.
"""

from __future__ import annotations

import argparse
from collections import Counter
import copy as copy_module
import hashlib
import json
import math
from pathlib import Path
import re
import sys
from typing import Any, Iterable

import bpy


def parse_args() -> argparse.Namespace:
    argv = sys.argv[sys.argv.index("--") + 1 :] if "--" in sys.argv else []
    parser = argparse.ArgumentParser()
    parser.add_argument("--plugin-root", type=Path, required=True)
    parser.add_argument("--model", type=Path, required=True)
    parser.add_argument(
        "--asset-kind",
        choices=("animated", "hierarchical", "static"),
        default="animated",
    )
    parser.add_argument("--animations", type=Path, nargs="*", default=[])
    parser.add_argument("--required-equipment", nargs="*", default=[])
    parser.add_argument("--excluded-optional-meshes", nargs="*", default=[])
    parser.add_argument("--output", type=Path, required=True)
    return parser.parse_args(argv)


def clean_name(value: str) -> str:
    return re.sub(r"[^a-z0-9_]+", "_", value.casefold()).strip("_")


def normalize_optional_mesh_exclusions(value: Any) -> list[str]:
    if (
        not isinstance(value, list)
        or len(value) > MAX_OPTIONAL_MESH_EXCLUSIONS
        or any(not isinstance(identifier, str) for identifier in value)
    ):
        raise ValueError(
            f"excluded optional meshes must be an array of at most "
            f"{MAX_OPTIONAL_MESH_EXCLUSIONS} strings"
        )
    if len(value) != len(set(value)):
        raise ValueError("excluded optional meshes contain duplicates")
    for identifier in value:
        if not CLEAN_MESH_IDENTIFIER_PATTERN.fullmatch(identifier):
            raise ValueError(
                f"excluded optional mesh is not an exact clean identifier: {identifier!r}"
            )
    return sorted(value)


def validate_asset_kind_request(
    asset_kind: str, animations: list[Any], required_equipment: list[str]
) -> None:
    if asset_kind not in {"animated", "hierarchical", "static"}:
        raise ValueError(f"unsupported W3D asset kind: {asset_kind}")
    if asset_kind == "animated" and not animations:
        raise ValueError("animated W3D conversion requires at least one animation")
    if asset_kind != "animated" and animations:
        raise ValueError(f"{asset_kind} W3D conversion does not accept animations")
    if asset_kind != "animated" and required_equipment:
        raise ValueError(
            f"{asset_kind} W3D conversion does not accept required equipment"
        )


RENDERABLE_W3D_OBJECT_TYPE = "MESH"
SUPPORTED_EQUIPMENT_ROLES = {"right-hand-weapon", "left-hand-shield"}
ATTACHMENT_MATRIX_TOLERANCE = 1.0e-6
CANONICAL_BONE_SEPARATION_RATIO = 0.80
ADDITIVE_BLEND_ENUM = 1
ADDITIVE_ALPHA_EPSILON = 1.0e-8
ADDITIVE_PIXEL_ROUND_TRIP_TOLERANCE = (1.0 / 255.0) + 1.0e-6
MAX_OPTIONAL_MESH_EXCLUSIONS = 64
CLEAN_MESH_IDENTIFIER_PATTERN = re.compile(
    r"^[a-z0-9](?:[a-z0-9_]{0,126}[a-z0-9])?$"
)
HELPER_LABEL_MARKERS = (
    "aabox",
    "aggregate",
    "boundingbox",
    "collision",
    "collider",
    "helper",
    "hitbox",
    "obbox",
    "physicsproxy",
    "proxy",
    "shadowmesh",
    "shadowproxy",
    "triggervolume",
    "volume",
    "volumeproxy",
)
WEAPON_LABEL_MARKERS = ("blade", "sword", "weapon")
SHIELD_LABEL_MARKERS = ("buckler", "shield")
RIGHT_EXPLICIT_ATTACHMENT_MARKERS = (
    "bsword",
    "swordbone",
    "weaponr",
)
RIGHT_GENERIC_HAND_MARKERS = (
    "handr",
    "rhand",
    "righthand",
)
RIGHT_HAND_MARKERS = RIGHT_EXPLICIT_ATTACHMENT_MARKERS + RIGHT_GENERIC_HAND_MARKERS
LEFT_EXPLICIT_ATTACHMENT_MARKERS = (
    "bshield",
    "shieldbone",
    "shieldl",
)
LEFT_GENERIC_HAND_MARKERS = (
    "handl",
    "lefthand",
    "lhand",
)
LEFT_HAND_MARKERS = LEFT_EXPLICIT_ATTACHMENT_MARKERS + LEFT_GENERIC_HAND_MARKERS
ATTACHMENT_PROOF_METHODS = {
    "custom-attachment",
    "dominant-weight-group",
    "parent-bone",
    "rest-pose-proximity",
    "weighted-hand-dominance",
    "weighted-hand-group",
}


def _compact_label(value: Any) -> str:
    return re.sub(r"[^a-z0-9]+", "", str(value).casefold())


def _contains_marker(values: Iterable[Any], markers: Iterable[str]) -> bool:
    compact = [_compact_label(value) for value in values]
    return any(marker in value for value in compact for marker in markers)


def _custom_items(owner: Any) -> list[tuple[str, Any]]:
    try:
        return [(str(key), owner[key]) for key in sorted(owner.keys(), key=str.casefold)]
    except (AttributeError, KeyError, TypeError):
        return []


def _w3d_object_type(item: Any) -> str | None:
    for owner in (getattr(item, "data", None), item):
        if owner is None:
            continue
        value = getattr(owner, "object_type", None)
        if value in (None, ""):
            for key, candidate in _custom_items(owner):
                if clean_name(key) == "object_type":
                    value = candidate
                    break
        if value not in (None, ""):
            return re.sub(r"[^A-Z0-9_]+", "_", str(value).upper()).strip("_")
    return None


def _custom_value_is_enabled(value: Any) -> bool:
    if value is None or value is False or value == 0:
        return False
    if isinstance(value, str) and clean_name(value) in {"", "false", "mesh", "none", "off"}:
        return False
    return True


def _non_render_reasons(item: Any) -> list[str]:
    """Return safe reason enums when a Blender mesh is W3D helper geometry."""

    reasons: set[str] = set()
    object_type = _w3d_object_type(item)
    if object_type is not None and object_type != RENDERABLE_W3D_OBJECT_TYPE:
        reasons.add("non-render-object-type")

    labels = [getattr(item, "name", ""), getattr(getattr(item, "data", None), "name", "")]
    if _contains_marker(labels, HELPER_LABEL_MARKERS):
        reasons.add("helper-semantic")

    for owner in (item, getattr(item, "data", None)):
        if owner is None:
            continue
        for key, value in _custom_items(owner):
            if not _custom_value_is_enabled(value):
                continue
            if _contains_marker((key, value), HELPER_LABEL_MARKERS):
                reasons.add("custom-helper-semantic")
    return sorted(reasons)


def _dominant_weight_labels(item: Any) -> list[str]:
    groups = list(getattr(item, "vertex_groups", []) or [])
    names = {
        int(getattr(group, "index", index)): str(getattr(group, "name", ""))
        for index, group in enumerate(groups)
    }
    weights: Counter[int] = Counter()
    total = 0.0
    for vertex in getattr(getattr(item, "data", None), "vertices", []) or []:
        for assignment in getattr(vertex, "groups", []) or []:
            weight = float(getattr(assignment, "weight", 0.0))
            if weight <= 0.0:
                continue
            group_index = int(getattr(assignment, "group", -1))
            weights[group_index] += weight
            total += weight
    if total <= 0.0:
        return []
    # A rigid weapon/shield is overwhelmingly bound to its attachment bone.
    # Merely mentioning a hand among the many groups on a body skin is not proof.
    return sorted(
        (names[index] for index, weight in weights.items() if index in names and weight / total >= 0.75),
        key=str.casefold,
    )


def _weighted_hand_labels(item: Any) -> list[str]:
    """Return hand-labelled deform groups with material influence.

    Equipment meshes in the retail W3D can be softly skinned across hand,
    forearm, and accessory bones, so no single group reaches the rigid 75%
    threshold above. A hand-named group carrying at least 2% of the mesh's
    aggregate deform weight is still direct rig evidence, while a mere unused
    group name is not.
    """

    groups = list(getattr(item, "vertex_groups", []) or [])
    names = {
        int(getattr(group, "index", index)): str(getattr(group, "name", ""))
        for index, group in enumerate(groups)
    }
    weights: Counter[int] = Counter()
    total = 0.0
    for vertex in getattr(getattr(item, "data", None), "vertices", []) or []:
        for assignment in getattr(vertex, "groups", []) or []:
            weight = float(getattr(assignment, "weight", 0.0))
            if weight <= 0.0:
                continue
            group_index = int(getattr(assignment, "group", -1))
            weights[group_index] += weight
            total += weight
    if total <= 0.0:
        return []
    return sorted(
        (
            names[index]
            for index, weight in weights.items()
            if index in names
            and weight / total >= 0.02
            and _contains_marker((names[index],), RIGHT_HAND_MARKERS + LEFT_HAND_MARKERS)
        ),
        key=str.casefold,
    )


def _hand_weight_shares(item: Any) -> tuple[float, float]:
    groups = list(getattr(item, "vertex_groups", []) or [])
    names = {
        int(getattr(group, "index", index)): str(getattr(group, "name", ""))
        for index, group in enumerate(groups)
    }
    total = 0.0
    right = 0.0
    left = 0.0
    for vertex in getattr(getattr(item, "data", None), "vertices", []) or []:
        for assignment in getattr(vertex, "groups", []) or []:
            weight = float(getattr(assignment, "weight", 0.0))
            if weight <= 0.0:
                continue
            total += weight
            label = names.get(int(getattr(assignment, "group", -1)), "")
            if _contains_marker((label,), RIGHT_HAND_MARKERS):
                right += weight
            if _contains_marker((label,), LEFT_HAND_MARKERS):
                left += weight
    if total <= 0.0:
        return 0.0, 0.0
    return right / total, left / total


def _custom_attachment_labels(item: Any) -> list[str]:
    values: list[str] = []
    for owner in (item, getattr(item, "data", None)):
        if owner is None:
            continue
        for key, value in _custom_items(owner):
            compact_key = _compact_label(key)
            if any(marker in compact_key for marker in ("attach", "bone", "parent", "pivot", "socket")):
                if isinstance(value, (str, int)) and _custom_value_is_enabled(value):
                    values.append(str(value))
    return sorted(set(values), key=str.casefold)


def _is_skinned(item: Any) -> bool:
    return bool(getattr(item, "vertex_groups", [])) and any(
        getattr(modifier, "type", "") == "ARMATURE"
        for modifier in (getattr(item, "modifiers", []) or [])
    )


def _is_box_geometry(item: Any) -> bool:
    """Detect an axis-aligned box in object-local space without using its name."""

    coordinates: set[tuple[float, float, float]] = set()
    for vertex in getattr(getattr(item, "data", None), "vertices", []) or []:
        coordinate = getattr(vertex, "co", None)
        if coordinate is None:
            return False
        try:
            values = tuple(round(float(coordinate[index]), 6) for index in range(3))
        except (IndexError, TypeError, ValueError):
            return False
        coordinates.add(values)
    if len(coordinates) != 8:
        return False
    axes = [{coordinate[index] for coordinate in coordinates} for index in range(3)]
    if any(len(axis) != 2 for axis in axes):
        return False
    expected = {
        (x, y, z)
        for x in axes[0]
        for y in axes[1]
        for z in axes[2]
    }
    item.data.calc_loop_triangles()
    return coordinates == expected and len(item.data.loop_triangles) == 12


def _rest_pose_item_centroid(item: Any) -> Any:
    vertices = list(getattr(getattr(item, "data", None), "vertices", []) or [])
    if not vertices or not hasattr(item, "matrix_world"):
        return None
    center = None
    try:
        for vertex in vertices:
            world = item.matrix_world @ vertex.co
            center = world.copy() if center is None else center + world
        center /= float(len(vertices))
    except (AttributeError, ReferenceError, RuntimeError, TypeError, ValueError):
        return None
    try:
        if not all(math.isfinite(float(center[index])) for index in range(3)):
            return None
    except (IndexError, TypeError, ValueError):
        return None
    return center


def _rest_pose_bone_distance(center: Any, rig: Any, bone: Any) -> float | None:
    try:
        local_points = (
            bone.head_local,
            bone.tail_local,
            (bone.head_local + bone.tail_local) * 0.5,
        )
        distance = min((center - (rig.matrix_world @ point)).length for point in local_points)
        return float(distance) if math.isfinite(float(distance)) else None
    except (AttributeError, ReferenceError, RuntimeError, TypeError, ValueError):
        return None


def _rest_pose_hand_attachment(item: Any, rig: Any) -> str:
    """Infer a hand only when mesh geometry is materially closer in rest pose.

    Some OpenSAGE imports preserve an accessory as an unparented render mesh and
    therefore lose the W3D hierarchy label. The mesh transform and armature rest
    pose still share model space. This is accepted only when both hands exist and
    the accessory centroid is at least 20% closer to one hand than the other.
    """

    if rig is None:
        return ""
    center = _rest_pose_item_centroid(item)
    if center is None:
        return ""

    hands: dict[str, list[float]] = {"right-hand": [], "left-hand": []}
    for bone in getattr(getattr(rig, "data", None), "bones", []) or []:
        name = str(getattr(bone, "name", ""))
        attachment = ""
        if _contains_marker((name,), RIGHT_HAND_MARKERS):
            attachment = "right-hand"
        elif _contains_marker((name,), LEFT_HAND_MARKERS):
            attachment = "left-hand"
        if not attachment:
            continue
        distance = _rest_pose_bone_distance(center, rig, bone)
        if distance is not None:
            hands[attachment].append(distance)
    if not hands["right-hand"] or not hands["left-hand"]:
        return ""
    right_distance = min(hands["right-hand"])
    left_distance = min(hands["left-hand"])
    if right_distance < left_distance * 0.80:
        return "right-hand"
    if left_distance < right_distance * 0.80:
        return "left-hand"
    return ""


def _select_canonical_hand_bone(item: Any, rig: Any, attachment: str) -> Any:
    if attachment == "right-hand":
        explicit_markers = RIGHT_EXPLICIT_ATTACHMENT_MARKERS
        generic_markers = RIGHT_GENERIC_HAND_MARKERS
    else:
        explicit_markers = LEFT_EXPLICIT_ATTACHMENT_MARKERS
        generic_markers = LEFT_GENERIC_HAND_MARKERS
    center = _rest_pose_item_centroid(item)
    if center is None:
        raise RuntimeError("required rigid equipment has no finite rest-pose centroid")
    explicit: list[tuple[float, str, int, Any]] = []
    generic: list[tuple[float, str, int, Any]] = []
    for index, bone in enumerate(
        getattr(getattr(rig, "data", None), "bones", []) or []
    ):
        name = str(getattr(bone, "name", ""))
        target = None
        if _contains_marker((name,), explicit_markers):
            target = explicit
        elif _contains_marker((name,), generic_markers):
            target = generic
        if target is None:
            continue
        distance = _rest_pose_bone_distance(center, rig, bone)
        if distance is not None:
            target.append((distance, clean_name(name), index, bone))
    scored = explicit if explicit else generic
    if not scored:
        raise RuntimeError("required rigid equipment has no canonical hand-bone candidate")
    scored.sort(key=lambda value: (value[0], value[1], value[2]))
    if len(scored) > 1:
        nearest = scored[0][0]
        runner_up = scored[1][0]
        if not nearest < runner_up * CANONICAL_BONE_SEPARATION_RATIO:
            raise RuntimeError(
                "matching hand bones are not materially separated in rest pose"
            )
    return scored[0][3]


def _safe_attachment_diagnostics(item: Any, rig: Any) -> dict[str, Any]:
    """Payload-free facts suitable for a failed local conversion report."""

    parent_labels = []
    if getattr(item, "parent_type", "") == "BONE" and getattr(item, "parent_bone", ""):
        parent_labels.append(str(item.parent_bone))
    dominant_labels = _dominant_weight_labels(item)
    weighted_labels = _weighted_hand_labels(item)
    custom_labels = _custom_attachment_labels(item)
    bones = list(getattr(getattr(rig, "data", None), "bones", []) or []) if rig is not None else []
    bone_labels = [str(getattr(bone, "name", "")) for bone in bones]
    right_share, left_share = _hand_weight_shares(item)
    return {
        "skinned": _is_skinned(item),
        "vertex_group_count": len(list(getattr(item, "vertex_groups", []) or [])),
        "parent_right": _contains_marker(parent_labels, RIGHT_HAND_MARKERS),
        "parent_left": _contains_marker(parent_labels, LEFT_HAND_MARKERS),
        "dominant_right": _contains_marker(dominant_labels, RIGHT_HAND_MARKERS),
        "dominant_left": _contains_marker(dominant_labels, LEFT_HAND_MARKERS),
        "weighted_right": _contains_marker(weighted_labels, RIGHT_HAND_MARKERS),
        "weighted_left": _contains_marker(weighted_labels, LEFT_HAND_MARKERS),
        "right_hand_weight_share": round(right_share, 6),
        "left_hand_weight_share": round(left_share, 6),
        "custom_right": _contains_marker(custom_labels, RIGHT_HAND_MARKERS),
        "custom_left": _contains_marker(custom_labels, LEFT_HAND_MARKERS),
        "rig_right_candidate_count": sum(
            1 for label in bone_labels if _contains_marker((label,), RIGHT_HAND_MARKERS)
        ),
        "rig_left_candidate_count": sum(
            1 for label in bone_labels if _contains_marker((label,), LEFT_HAND_MARKERS)
        ),
        "rest_pose_attachment": _rest_pose_hand_attachment(item, rig) or "ambiguous",
    }


def _equipment_classification(item: Any, rig: Any = None) -> tuple[str, str, list[str]]:
    mesh_labels = [getattr(item, "name", ""), getattr(getattr(item, "data", None), "name", "")]
    material_labels = [
        getattr(material, "name", "")
        for material in (getattr(getattr(item, "data", None), "materials", []) or [])
        if material is not None
    ]
    parent_labels = []
    if getattr(item, "parent_type", "") == "BONE" and getattr(item, "parent_bone", ""):
        parent_labels.append(str(item.parent_bone))
    dominant_labels = _dominant_weight_labels(item)
    weighted_hand_labels = _weighted_hand_labels(item)
    custom_labels = _custom_attachment_labels(item)

    role_proofs: dict[str, set[str]] = {
        "right-hand-weapon": set(),
        "left-hand-shield": set(),
    }
    for labels, method in (
        (mesh_labels, "mesh-semantic"),
        (material_labels, "material-semantic"),
    ):
        if _contains_marker(labels, WEAPON_LABEL_MARKERS):
            role_proofs["right-hand-weapon"].add(method)
        if _contains_marker(labels, SHIELD_LABEL_MARKERS):
            role_proofs["left-hand-shield"].add(method)

    attachment_proofs: dict[str, set[str]] = {"right-hand": set(), "left-hand": set()}
    for labels, method in (
        (parent_labels, "parent-bone"),
        (dominant_labels, "dominant-weight-group"),
        (weighted_hand_labels, "weighted-hand-group"),
        (custom_labels, "custom-attachment"),
    ):
        if _contains_marker(labels, RIGHT_HAND_MARKERS):
            attachment_proofs["right-hand"].add(method)
        if _contains_marker(labels, LEFT_HAND_MARKERS):
            attachment_proofs["left-hand"].add(method)

    weapon_hint = bool(role_proofs["right-hand-weapon"])
    shield_hint = bool(role_proofs["left-hand-shield"])
    right_hint = bool(attachment_proofs["right-hand"])
    left_hint = bool(attachment_proofs["left-hand"])
    if not right_hint and not left_hint:
        rest_attachment = _rest_pose_hand_attachment(item, rig)
        if rest_attachment:
            attachment_proofs[rest_attachment].add("rest-pose-proximity")
            right_hint = bool(attachment_proofs["right-hand"])
            left_hint = bool(attachment_proofs["left-hand"])
    if weapon_hint and shield_hint:
        raise RuntimeError("render mesh has ambiguous weapon and shield semantics")
    if (weapon_hint or shield_hint) and right_hint and left_hint:
        right_share, left_share = _hand_weight_shares(item)
        if weapon_hint and right_share >= max(0.02, left_share * 1.5):
            attachment_proofs["right-hand"].add("weighted-hand-dominance")
            attachment_proofs["left-hand"].clear()
            left_hint = False
        elif shield_hint and left_share >= max(0.02, right_share * 1.5):
            attachment_proofs["left-hand"].add("weighted-hand-dominance")
            attachment_proofs["right-hand"].clear()
            right_hint = False
        else:
            raise RuntimeError(
                "render mesh has ambiguous left-hand and right-hand attachment semantics: "
                + json.dumps(
                    {
                        "weapon_hint": weapon_hint,
                        "shield_hint": shield_hint,
                        "right_proof_methods": sorted(attachment_proofs["right-hand"]),
                        "left_proof_methods": sorted(attachment_proofs["left-hand"]),
                        "attachment_facts": _safe_attachment_diagnostics(item, rig),
                    },
                    sort_keys=True,
                )
            )
    if weapon_hint:
        if not right_hint or left_hint:
            raise RuntimeError(
                "weapon-like render mesh has no proven right-hand attachment: "
                + json.dumps(_safe_attachment_diagnostics(item, rig), sort_keys=True)
            )
        proof = role_proofs["right-hand-weapon"] | attachment_proofs["right-hand"]
        return "right-hand-weapon", "right-hand", sorted(proof)
    if shield_hint:
        if not left_hint or right_hint:
            raise RuntimeError(
                "shield-like render mesh has no proven left-hand attachment: "
                + json.dumps(_safe_attachment_diagnostics(item, rig), sort_keys=True)
            )
        proof = role_proofs["left-hand-shield"] | attachment_proofs["left-hand"]
        return "left-hand-shield", "left-hand", sorted(proof)
    return "character-mesh", "skeletal" if _is_skinned(item) else "scene", []


def build_mesh_inventory(
    mesh_objects: list[Any], required_equipment: Iterable[str], rig: Any = None
) -> tuple[list[dict[str, Any]], dict[str, dict[str, Any]]]:
    required = sorted(set(str(value) for value in required_equipment))
    unsupported = sorted(set(required) - SUPPORTED_EQUIPMENT_ROLES)
    if unsupported:
        raise ValueError("unsupported required equipment semantics: " + ", ".join(unsupported))

    ordered = sorted(
        mesh_objects,
        key=lambda item: (
            clean_name(str(getattr(item, "name", ""))),
            clean_name(str(getattr(getattr(item, "data", None), "name", ""))),
        ),
    )
    inventory: list[dict[str, Any]] = []
    for index, item in enumerate(ordered):
        object_type = _w3d_object_type(item)
        if object_type != RENDERABLE_W3D_OBJECT_TYPE:
            raise RuntimeError("W3D plugin could not prove a remaining mesh is render geometry")
        if _non_render_reasons(item):
            raise RuntimeError("non-render W3D helper geometry remained after filtering")
        item.data.calc_loop_triangles()
        if _is_box_geometry(item):
            raise RuntimeError(
                "box-shaped render mesh is ambiguous with collision/helper geometry"
            )
        role, attachment, proof_methods = _equipment_classification(item, rig)
        inventory.append(
            {
                "index": index,
                "semantic_role": role,
                "attachment": attachment,
                "proof_methods": proof_methods,
                "vertices": len(item.data.vertices),
                "triangles": len(item.data.loop_triangles),
                "material_slots": len(item.data.materials),
                "skinned": _is_skinned(item),
            }
        )

    equipment: dict[str, dict[str, Any]] = {}
    for role, attachment in (
        ("right-hand-weapon", "right-hand"),
        ("left-hand-shield", "left-hand"),
    ):
        members = [item for item in inventory if item["semantic_role"] == role]
        if role in required and not members:
            raise RuntimeError(f"required equipment semantic was not proven: {role}")
        if members:
            equipment[role] = {
                "attachment": attachment,
                "mesh_indices": [item["index"] for item in members],
                "mesh_count": len(members),
                "proof_methods": sorted(
                    {method for item in members for method in item["proof_methods"]}
                ),
            }
    return inventory, equipment


def canonicalize_required_rigid_attachments(
    mesh_objects: list[Any], required_equipment: Iterable[str], rig: Any
) -> int:
    """Promote unique rest-pose-only rigid equipment to an explicit bone parent."""

    required = set(str(value) for value in required_equipment)
    canonicalized = 0
    for item in mesh_objects:
        role, attachment, proof_methods = _equipment_classification(item, rig)
        if role not in required:
            continue
        attachment_methods = set(proof_methods) & ATTACHMENT_PROOF_METHODS
        if attachment_methods != {"rest-pose-proximity"}:
            continue
        if _is_skinned(item):
            raise RuntimeError(
                "required skinned equipment cannot use rigid attachment promotion"
            )
        bone = _select_canonical_hand_bone(item, rig, attachment)
        if not hasattr(item, "matrix_world"):
            raise RuntimeError("required rigid equipment has no world transform")
        world_transform = _copy_private_transform(item.matrix_world)
        world_matrix = _finite_matrix_elements(world_transform)
        if world_matrix is None or world_matrix[0] != (4, 4):
            raise RuntimeError("required rigid equipment world transform is not finite")
        try:
            item.parent = rig
            item.parent_type = "BONE"
            item.parent_bone = str(getattr(bone, "name", ""))
            item.matrix_world = _copy_private_transform(world_transform)
        except (AttributeError, ReferenceError, RuntimeError, TypeError, ValueError) as exc:
            raise RuntimeError("could not canonicalize required rigid attachment") from exc
        restored_parent = getattr(item, "parent", None)
        if (
            restored_parent is None
            or _runtime_identity(restored_parent) != _runtime_identity(rig)
            or str(getattr(item, "parent_type", "")) != "BONE"
            or str(getattr(item, "parent_bone", "")) != str(getattr(bone, "name", ""))
            or not _private_transforms_close(item.matrix_world, world_transform)
        ):
            raise RuntimeError("canonical rigid attachment did not preserve its world transform")
        promoted_role, promoted_attachment, promoted_proofs = _equipment_classification(
            item, rig
        )
        if (
            promoted_role != role
            or promoted_attachment != attachment
            or "parent-bone" not in promoted_proofs
        ):
            raise RuntimeError("canonical rigid attachment semantic revalidation failed")
        canonicalized += 1
    return canonicalized


def _canonical_fingerprint_value(value: Any, *, depth: int = 0) -> Any:
    """Convert selected Blender values to stable JSON without emitting them."""

    if depth > 8:
        return {"type": type(value).__name__}
    if value is None or isinstance(value, (bool, int, str)):
        return value
    if isinstance(value, float):
        return {"float": value.hex()}
    if isinstance(value, bytes):
        return {"bytes_sha256": hashlib.sha256(value).hexdigest(), "size": len(value)}
    if isinstance(value, dict):
        return {
            str(key): _canonical_fingerprint_value(item, depth=depth + 1)
            for key, item in sorted(value.items(), key=lambda pair: str(pair[0]).casefold())
        }
    to_list = getattr(value, "to_list", None)
    if callable(to_list):
        return _canonical_fingerprint_value(to_list(), depth=depth + 1)
    if isinstance(value, (list, tuple)):
        return [_canonical_fingerprint_value(item, depth=depth + 1) for item in value]
    try:
        return [
            _canonical_fingerprint_value(item, depth=depth + 1)
            for item in list(value)
        ]
    except TypeError:
        return {"type": type(value).__name__}


def _custom_fingerprint(owner: Any) -> dict[str, Any]:
    return {
        key: _canonical_fingerprint_value(value)
        for key, value in _custom_items(owner)
    }


def _digest_fingerprint_payload(value: Any) -> str:
    encoded = json.dumps(value, ensure_ascii=True, separators=(",", ":"), sort_keys=True)
    return hashlib.sha256(encoded.encode("utf-8")).hexdigest()


def _runtime_identity(value: Any) -> tuple[str, int]:
    """Identify one live Blender datablock without putting identity in metadata."""

    as_pointer = getattr(value, "as_pointer", None)
    if callable(as_pointer):
        return "blender", int(as_pointer())
    return "python", id(value)


def _preserved_shader_enum(material: Any, property_name: str) -> int | None:
    """Read one exact preserved W3D shader enum without coercing heuristics."""

    shader = getattr(material, "shader", None)
    if shader is None or not hasattr(shader, property_name):
        return None
    value = getattr(shader, property_name)
    if isinstance(value, bool):
        raise RuntimeError("preserved W3D shader enum is not exact")
    if isinstance(value, int):
        return value
    # Blender EnumProperty exposes the plugin's source integer identifier as
    # a canonical decimal string. No other string or numeric coercion counts.
    if isinstance(value, str) and re.fullmatch(r"0|[1-9][0-9]*", value):
        return int(value)
    raise RuntimeError("preserved W3D shader enum is not exact")


def _material_has_proven_additive_blend(material: Any) -> bool:
    source = _preserved_shader_enum(material, "src_blend")
    destination = _preserved_shader_enum(material, "dest_blend")
    if source is None and destination is None:
        return False
    if source is None or destination is None:
        raise RuntimeError("preserved W3D shader blend proof is incomplete")
    return source == ADDITIVE_BLEND_ENUM and destination == ADDITIVE_BLEND_ENUM


def _socket_by_name(sockets: Any, name: str) -> Any:
    getter = getattr(sockets, "get", None)
    if callable(getter):
        socket = getter(name)
        if socket is not None:
            return socket
    try:
        return sockets[name]
    except (KeyError, TypeError):
        pass
    matches = [
        socket
        for socket in list(sockets or [])
        if str(getattr(socket, "name", "")) == name
    ]
    return matches[0] if len(matches) == 1 else None


def _additive_alpha_pixels(pixels: Iterable[Any]) -> tuple[list[float], dict[str, int]]:
    """Approximate additive RGB with normalized RGB and conventional alpha."""

    source = list(pixels)
    if not source or len(source) % 4 != 0:
        raise RuntimeError("additive material image has an invalid RGBA pixel buffer")
    converted: list[float] = []
    changed_alpha = 0
    transparent = 0
    visible = 0
    for offset in range(0, len(source), 4):
        channels = []
        for value in source[offset : offset + 4]:
            try:
                channel = float(value)
            except (TypeError, ValueError) as exc:
                raise RuntimeError("additive material image has invalid pixel data") from exc
            if not math.isfinite(channel):
                raise RuntimeError("additive material image has non-finite pixel data")
            channels.append(min(1.0, max(0.0, channel)))
        red, green, blue, source_alpha = channels
        intensity = max(red, green, blue)
        if intensity <= ADDITIVE_ALPHA_EPSILON:
            output_rgb = (0.0, 0.0, 0.0)
            output_alpha = 0.0
        else:
            output_rgb = (red / intensity, green / intensity, blue / intensity)
            output_alpha = min(1.0, max(0.0, source_alpha * intensity))
        converted.extend((*output_rgb, output_alpha))
        if abs(output_alpha - source_alpha) > ADDITIVE_ALPHA_EPSILON:
            changed_alpha += 1
        if output_alpha < 1.0 - ADDITIVE_ALPHA_EPSILON:
            transparent += 1
        if output_alpha > ADDITIVE_ALPHA_EPSILON:
            visible += 1
    if changed_alpha < 1:
        raise RuntimeError("additive material conversion did not change image alpha")
    if transparent < 1:
        raise RuntimeError("additive material conversion produced no transparent pixels")
    if visible < 1:
        raise RuntimeError("additive material conversion produced no visible pixels")
    return converted, {
        "changed_alpha_pixels": changed_alpha,
        "transparent_pixels": transparent,
        "visible_pixels": visible,
    }


def _verify_additive_pixel_round_trip(
    actual_pixels: Iterable[Any], expected_pixels: Iterable[Any]
) -> list[float]:
    actual = list(actual_pixels)
    expected = list(expected_pixels)
    if len(actual) != len(expected):
        raise RuntimeError("additive material image alpha did not round trip")
    verified: list[float] = []
    for actual_value, expected_value in zip(actual, expected):
        try:
            channel = float(actual_value)
            target = float(expected_value)
        except (TypeError, ValueError) as exc:
            raise RuntimeError("additive material image alpha did not round trip") from exc
        if (
            not math.isfinite(channel)
            or not math.isfinite(target)
            or channel < 0.0
            or channel > 1.0
            or abs(channel - target) > ADDITIVE_PIXEL_ROUND_TRIP_TOLERANCE
        ):
            raise RuntimeError("additive material image alpha did not round trip")
        verified.append(channel)
    return verified


def _convert_proven_additive_material(material: Any) -> dict[str, int]:
    if not bool(getattr(material, "use_nodes", False)):
        raise RuntimeError("proven additive material has no exportable node graph")
    node_tree = getattr(material, "node_tree", None)
    if node_tree is None:
        raise RuntimeError("proven additive material has no exportable node graph")
    nodes = list(getattr(node_tree, "nodes", []) or [])
    image_nodes = [
        node
        for node in nodes
        if str(getattr(node, "type", "")) == "TEX_IMAGE"
        and getattr(node, "image", None) is not None
    ]
    principled_nodes = [
        node for node in nodes if str(getattr(node, "type", "")) == "BSDF_PRINCIPLED"
    ]
    if len(principled_nodes) != 1:
        raise RuntimeError("proven additive material has an ambiguous surface shader")
    principled = principled_nodes[0]
    base_color = _socket_by_name(getattr(principled, "inputs", None), "Base Color")
    alpha_input = _socket_by_name(getattr(principled, "inputs", None), "Alpha")
    if base_color is None or alpha_input is None:
        raise RuntimeError("proven additive material lacks required shader inputs")
    links = getattr(node_tree, "links", None)
    if links is None:
        raise RuntimeError("proven additive material has no exportable node links")
    direct_color_nodes = {
        _runtime_identity(getattr(link, "from_node", None)): getattr(link, "from_node", None)
        for link in list(links)
        if getattr(link, "to_socket", None) is base_color
        and getattr(link, "from_node", None) in image_nodes
    }
    candidates = list(direct_color_nodes.values()) if direct_color_nodes else image_nodes
    if len(candidates) != 1:
        raise RuntimeError("proven additive material has an ambiguous color image")
    image_node = candidates[0]
    source_image = image_node.image
    try:
        source_pixels = list(source_image.pixels[:])
    except (AttributeError, ReferenceError, RuntimeError, TypeError) as exc:
        raise RuntimeError("proven additive material image pixels are unavailable") from exc
    converted_pixels, pixel_report = _additive_alpha_pixels(source_pixels)

    duplicated = int(int(getattr(source_image, "users", 0)) > 1)
    target_image = source_image
    if duplicated:
        try:
            target_image = source_image.copy()
        except (AttributeError, ReferenceError, RuntimeError, TypeError) as exc:
            raise RuntimeError("shared additive material image could not be duplicated") from exc
        for node in image_nodes:
            if getattr(node, "image", None) is source_image:
                node.image = target_image
    try:
        target_pixels = target_image.pixels
        writer = getattr(target_pixels, "foreach_set", None)
        if callable(writer):
            writer(converted_pixels)
        else:
            target_pixels[:] = converted_pixels
        target_image.update()
        round_trip_pixels = list(target_image.pixels[:])
    except (AttributeError, ReferenceError, RuntimeError, TypeError, ValueError) as exc:
        raise RuntimeError("additive material image alpha could not be written") from exc
    verified_pixels = _verify_additive_pixel_round_trip(
        round_trip_pixels, converted_pixels
    )
    if not any(
        verified_pixels[index] < 1.0 - ADDITIVE_ALPHA_EPSILON
        for index in range(3, len(verified_pixels), 4)
    ):
        raise RuntimeError("additive material image has no verified transparent pixels")
    if not any(
        verified_pixels[index] > ADDITIVE_ALPHA_EPSILON
        for index in range(3, len(verified_pixels), 4)
    ):
        raise RuntimeError("additive material image has no verified visible pixels")

    alpha_output = _socket_by_name(getattr(image_node, "outputs", None), "Alpha")
    if alpha_output is None:
        raise RuntimeError("proven additive material image has no alpha output")
    incoming_alpha = [
        link for link in list(links) if getattr(link, "to_socket", None) is alpha_input
    ]
    if incoming_alpha:
        if len(incoming_alpha) != 1 or (
            getattr(incoming_alpha[0], "from_node", None) is not image_node
            or getattr(incoming_alpha[0], "from_socket", None) is not alpha_output
        ):
            raise RuntimeError("proven additive material has an ambiguous alpha input")
    else:
        try:
            links.new(alpha_output, alpha_input)
        except (AttributeError, RuntimeError, TypeError) as exc:
            raise RuntimeError("additive material alpha could not be connected") from exc
    return {
        "converted_materials": 1,
        "duplicated_images": duplicated,
        **pixel_report,
    }


def convert_proven_additive_materials(materials: Iterable[Any]) -> dict[str, int]:
    report = {
        "converted_materials": 0,
        "duplicated_images": 0,
        "changed_alpha_pixels": 0,
        "transparent_pixels": 0,
        "visible_pixels": 0,
    }
    unique: dict[tuple[str, int], Any] = {}
    for material in materials:
        if material is not None:
            unique[_runtime_identity(material)] = material
    ordered = sorted(
        unique.values(),
        key=lambda item: clean_name(str(getattr(item, "name", ""))),
    )
    for material in ordered:
        if not _material_has_proven_additive_blend(material):
            continue
        converted = _convert_proven_additive_material(material)
        for key, value in converted.items():
            report[key] += value
    return report


def _material_payload(material: Any) -> Any:
    if material is None:
        return None
    payload: dict[str, Any] = {
        "name": str(getattr(material, "name", "")),
        "custom": _custom_fingerprint(material),
    }
    for attribute in (
        "alpha_threshold",
        "blend_method",
        "diffuse_color",
        "diffuse_intensity",
        "metallic",
        "roughness",
        "shadow_method",
        "specular_color",
        "specular_intensity",
        "surface_render_method",
        "use_nodes",
    ):
        if hasattr(material, attribute):
            payload[attribute] = _canonical_fingerprint_value(getattr(material, attribute))

    node_tree = getattr(material, "node_tree", None)
    if node_tree is not None:
        nodes = []
        for node in sorted(
            list(getattr(node_tree, "nodes", []) or []),
            key=lambda item: (str(getattr(item, "name", "")).casefold(), str(getattr(item, "type", ""))),
        ):
            inputs = []
            for socket in list(getattr(node, "inputs", []) or []):
                item = {"name": str(getattr(socket, "name", ""))}
                if hasattr(socket, "default_value"):
                    item["default"] = _canonical_fingerprint_value(socket.default_value)
                inputs.append(item)
            nodes.append(
                {
                    "name": str(getattr(node, "name", "")),
                    "type": str(getattr(node, "type", "")),
                    "label": str(getattr(node, "label", "")),
                    "mute": bool(getattr(node, "mute", False)),
                    "inputs": inputs,
                }
            )
        links = sorted(
            (
                str(getattr(getattr(link, "from_node", None), "name", "")),
                str(getattr(getattr(link, "from_socket", None), "name", "")),
                str(getattr(getattr(link, "to_node", None), "name", "")),
                str(getattr(getattr(link, "to_socket", None), "name", "")),
            )
            for link in (getattr(node_tree, "links", []) or [])
        )
        payload["nodes"] = nodes
        payload["links"] = links
    return payload


def _geometry_payload(item: Any) -> dict[str, Any]:
    data = item.data
    data.calc_loop_triangles()
    vertices = [
        {
            "co": _canonical_fingerprint_value(getattr(vertex, "co", None)),
            "normal": _canonical_fingerprint_value(getattr(vertex, "normal", None)),
        }
        for vertex in (getattr(data, "vertices", []) or [])
    ]
    edges = [
        {
            "vertices": _canonical_fingerprint_value(getattr(edge, "vertices", ())),
            "sharp": bool(getattr(edge, "use_edge_sharp", False)),
        }
        for edge in (getattr(data, "edges", []) or [])
    ]
    polygons = [
        {
            "vertices": _canonical_fingerprint_value(getattr(polygon, "vertices", ())),
            "material": int(getattr(polygon, "material_index", 0)),
            "smooth": bool(getattr(polygon, "use_smooth", False)),
        }
        for polygon in (getattr(data, "polygons", []) or [])
    ]
    triangles = [
        {
            "vertices": _canonical_fingerprint_value(getattr(triangle, "vertices", ())),
            "loops": _canonical_fingerprint_value(getattr(triangle, "loops", ())),
            "material": int(getattr(triangle, "material_index", 0)),
        }
        for triangle in (getattr(data, "loop_triangles", []) or [])
    ]
    uv_layers = []
    for layer in (getattr(data, "uv_layers", []) or []):
        uv_layers.append(
            {
                "name": str(getattr(layer, "name", "")),
                "active_render": bool(getattr(layer, "active_render", False)),
                "values": [
                    _canonical_fingerprint_value(getattr(entry, "uv", None))
                    for entry in (getattr(layer, "data", []) or [])
                ],
            }
        )
    return {
        "vertices": vertices,
        "edges": edges,
        "polygons": polygons,
        "triangles": triangles,
        "uv_layers": uv_layers,
    }


def _weight_payload(item: Any) -> dict[str, Any]:
    groups = sorted(
        (
            int(getattr(group, "index", index)),
            str(getattr(group, "name", "")),
            bool(getattr(group, "lock_weight", False)),
        )
        for index, group in enumerate(getattr(item, "vertex_groups", []) or [])
    )
    vertices = []
    for vertex in (getattr(item.data, "vertices", []) or []):
        vertices.append(
            sorted(
                (
                    int(getattr(assignment, "group", -1)),
                    _canonical_fingerprint_value(float(getattr(assignment, "weight", 0.0))),
                )
                for assignment in (getattr(vertex, "groups", []) or [])
            )
        )
    return {"groups": groups, "vertices": vertices}


def _object_data_payload(item: Any) -> dict[str, Any]:
    # Parent/parent_bone are intentionally absent. Some animation-only W3Ds
    # clear attachment parenting while leaving the validated render payload
    # untouched; a separate private proof restores and revalidates that state.
    return {
        "object_name": str(getattr(item, "name", "")),
        "object_type": str(getattr(item, "type", "")),
        "data_name": str(getattr(item.data, "name", "")),
        "w3d_object_type": _w3d_object_type(item),
        "object_custom": _custom_fingerprint(item),
        "data_custom": _custom_fingerprint(item.data),
        "modifiers": [
            {
                "name": str(getattr(modifier, "name", "")),
                "type": str(getattr(modifier, "type", "")),
                "show_render": bool(getattr(modifier, "show_render", True)),
            }
            for modifier in (getattr(item, "modifiers", []) or [])
        ],
    }


def capture_render_geometry_proof(mesh_objects: list[Any]) -> list[dict[str, Any]]:
    """Capture private, deterministic pre-animation proof for render meshes."""

    ordered = sorted(
        mesh_objects,
        key=lambda item: (
            clean_name(str(getattr(item, "name", ""))),
            clean_name(str(getattr(getattr(item, "data", None), "name", ""))),
        ),
    )
    proof: list[dict[str, Any]] = []
    for item in ordered:
        materials = tuple(getattr(item.data, "materials", []) or [])
        proof.append(
            {
                "object_ref": item,
                "object_identity": _runtime_identity(item),
                "data_ref": item.data,
                "data_identity": _runtime_identity(item.data),
                "material_refs": materials,
                "material_identities": tuple(_runtime_identity(value) for value in materials),
                "fingerprints": {
                    "object_data": _digest_fingerprint_payload(_object_data_payload(item)),
                    "geometry": _digest_fingerprint_payload(_geometry_payload(item)),
                    "materials": _digest_fingerprint_payload(
                        [_material_payload(material) for material in materials]
                    ),
                    "weights": _digest_fingerprint_payload(_weight_payload(item)),
                },
            }
        )
    return proof


def exclude_optional_render_meshes(
    mesh_objects: list[Any],
    excluded_identifiers: list[str],
    required_equipment: Iterable[str],
    rig: Any = None,
) -> list[dict[str, Any]]:
    """Remove an exact, predeclared optional render subobject closure.

    Identifiers refer only to ``clean_name(object.name)``. No source names or
    material payloads leave Blender; provenance contains declared identifiers,
    bounded counts, and digests of the removed geometry/material state.
    """

    requested = normalize_optional_mesh_exclusions(excluded_identifiers)
    if not requested:
        return []
    renderable = [
        item
        for item in mesh_objects
        if getattr(item, "type", "") == "MESH"
        and _w3d_object_type(item) == RENDERABLE_W3D_OBJECT_TYPE
        and not _non_render_reasons(item)
    ]
    by_identifier: dict[str, list[Any]] = {}
    for item in renderable:
        identifier = clean_name(str(getattr(item, "name", "")))
        if identifier:
            by_identifier.setdefault(identifier, []).append(item)

    targets: list[Any] = []
    for identifier in requested:
        matches = by_identifier.get(identifier, [])
        if len(matches) != 1:
            raise RuntimeError(
                f"excluded optional mesh {identifier!r} matched {len(matches)} "
                "initially renderable meshes"
            )
        targets.append(matches[0])

    roles: dict[tuple[str, int], str] = {}
    for item in renderable:
        if _is_box_geometry(item):
            raise RuntimeError(
                "box-shaped render mesh is ambiguous with collision/helper geometry"
            )
        role, _attachment, _proof_methods = _equipment_classification(item, rig)
        roles[_runtime_identity(item)] = role

    target_identities = {_runtime_identity(item) for item in targets}
    required = set(str(value) for value in required_equipment)
    for identifier, item in zip(requested, targets):
        role = roles[_runtime_identity(item)]
        if role in SUPPORTED_EQUIPMENT_ROLES:
            qualifier = "required" if role in required else "proven"
            raise RuntimeError(
                f"excluded optional mesh {identifier!r} is {qualifier} equipment"
            )
    character_count = sum(role == "character-mesh" for role in roles.values())
    removed_character_count = sum(
        roles[identity] == "character-mesh" for identity in target_identities
    )
    if character_count - removed_character_count < 1:
        raise RuntimeError("excluded optional meshes would remove the last character mesh")

    exclusions: list[dict[str, Any]] = []
    for identifier, item in zip(requested, targets):
        item.data.calc_loop_triangles()
        materials = list(getattr(item.data, "materials", []) or [])
        exclusions.append(
            {
                "identifier": identifier,
                "geometry_sha256": _digest_fingerprint_payload(_geometry_payload(item)),
                "materials_sha256": _digest_fingerprint_payload(
                    [_material_payload(material) for material in materials]
                ),
                "vertices": len(getattr(item.data, "vertices", []) or []),
                "triangles": len(getattr(item.data, "loop_triangles", []) or []),
                "material_slots": len(materials),
            }
        )

    for item in targets:
        bpy.data.objects.remove(item, do_unlink=True)
    for mesh in list(bpy.data.meshes):
        if mesh.users == 0:
            bpy.data.meshes.remove(mesh)
    for material in list(bpy.data.materials):
        if material.users == 0:
            bpy.data.materials.remove(material)
    return exclusions


def assert_render_geometry_unchanged(
    proof: list[dict[str, Any]], mesh_objects: list[Any]
) -> None:
    """Fail if animation imports changed any prevalidated render payload."""

    current_by_identity = {_runtime_identity(item): item for item in mesh_objects}
    if len(current_by_identity) != len(proof):
        raise RuntimeError("animation import added, removed, or replaced render geometry")
    for record in proof:
        current = current_by_identity.get(record["object_identity"])
        if current is None or _runtime_identity(current.data) != record["data_identity"]:
            raise RuntimeError("animation import added, removed, or replaced render geometry")
        current_materials = tuple(getattr(current.data, "materials", []) or [])
        current_material_identities = tuple(
            _runtime_identity(value) for value in current_materials
        )
        if current_material_identities != record["material_identities"]:
            raise RuntimeError("animation import replaced render material data")
        fingerprints = {
            "object_data": _digest_fingerprint_payload(_object_data_payload(current)),
            "geometry": _digest_fingerprint_payload(_geometry_payload(current)),
            "materials": _digest_fingerprint_payload(
                [_material_payload(material) for material in current_materials]
            ),
            "weights": _digest_fingerprint_payload(_weight_payload(current)),
        }
        changed = sorted(
            name
            for name, fingerprint in fingerprints.items()
            if fingerprint != record["fingerprints"].get(name)
        )
        if changed:
            raise RuntimeError(
                "animation import materially mutated prevalidated render geometry: "
                + ", ".join(changed)
            )


def _copy_private_transform(value: Any) -> Any:
    if value is None:
        return None
    copier = getattr(value, "copy", None)
    if callable(copier):
        return copier()
    return copy_module.deepcopy(value)


def _finite_matrix_elements(value: Any) -> tuple[tuple[int, ...], list[float]] | None:
    """Return matrix shape/elements only when every component is finite."""

    to_list = getattr(value, "to_list", None)
    if callable(to_list):
        value = to_list()

    def collect(item: Any) -> tuple[tuple[int, ...], list[float]] | None:
        if isinstance(item, bool):
            return None
        if isinstance(item, (int, float)):
            number = float(item)
            return ((), [number]) if math.isfinite(number) else None
        try:
            children = list(item)
        except TypeError:
            return None
        collected = [collect(child) for child in children]
        if any(child is None for child in collected):
            return None
        shapes = [child[0] for child in collected if child is not None]
        if shapes and any(shape != shapes[0] for shape in shapes):
            return None
        child_shape = shapes[0] if shapes else ()
        elements: list[float] = []
        for child in collected:
            if child is not None:
                elements.extend(child[1])
        return (len(children),) + child_shape, elements

    return collect(value)


def _private_transforms_close(
    actual: Any,
    expected: Any,
    tolerance: float = ATTACHMENT_MATRIX_TOLERANCE,
) -> bool:
    if not math.isfinite(tolerance) or tolerance < 0.0:
        return False
    actual_matrix = _finite_matrix_elements(actual)
    expected_matrix = _finite_matrix_elements(expected)
    if actual_matrix is None or expected_matrix is None:
        return False
    if actual_matrix[0] != expected_matrix[0] or len(actual_matrix[1]) != len(expected_matrix[1]):
        return False
    return all(
        abs(actual_value - expected_value) <= tolerance
        for actual_value, expected_value in zip(actual_matrix[1], expected_matrix[1])
    )


def capture_render_attachment_proof(mesh_objects: list[Any]) -> list[dict[str, Any]]:
    """Capture private attachment state that animation-only imports may clear."""

    proof: list[dict[str, Any]] = []
    for item in mesh_objects:
        parent = getattr(item, "parent", None)
        if not hasattr(item, "matrix_parent_inverse") or not hasattr(item, "matrix_basis"):
            raise RuntimeError("prevalidated attachment transform is unavailable")
        parent_inverse = _copy_private_transform(item.matrix_parent_inverse)
        local_transform = _copy_private_transform(item.matrix_basis)
        parent_matrix = _finite_matrix_elements(parent_inverse)
        local_matrix = _finite_matrix_elements(local_transform)
        if parent_matrix is None or parent_matrix[0] != (4, 4):
            raise RuntimeError("prevalidated parent-inverse matrix is not finite")
        if local_matrix is None or local_matrix[0] != (4, 4):
            raise RuntimeError("prevalidated local transform is not finite")
        proof.append(
            {
                "object_ref": item,
                "object_identity": _runtime_identity(item),
                "parent_ref": parent,
                "parent_identity": _runtime_identity(parent) if parent is not None else None,
                "parent_type": str(getattr(item, "parent_type", "")),
                "parent_bone": str(getattr(item, "parent_bone", "")),
                "parent_inverse": parent_inverse,
                "local_transform": local_transform,
            }
        )
    return proof


def restore_render_attachments(
    proof: list[dict[str, Any]], mesh_objects: list[Any], scene_objects: list[Any]
) -> None:
    """Restore exact prevalidated parenting without emitting private details."""

    current_by_identity = {_runtime_identity(item): item for item in mesh_objects}
    if len(current_by_identity) != len(proof):
        raise RuntimeError("animation import changed the render attachment inventory")
    available_by_identity = {_runtime_identity(item): item for item in scene_objects}
    for record in proof:
        current = current_by_identity.get(record["object_identity"])
        if current is None:
            raise RuntimeError("animation import changed the render attachment inventory")
        parent_identity = record["parent_identity"]
        parent = None
        if parent_identity is not None:
            parent = available_by_identity.get(parent_identity)
            if parent is None:
                raise RuntimeError(
                    "prevalidated attachment parent is unavailable after animation import"
                )
        try:
            current.parent = parent
            current.parent_type = record["parent_type"]
            current.parent_bone = record["parent_bone"]
            current.matrix_parent_inverse = _copy_private_transform(
                record["parent_inverse"]
            )
            current.matrix_basis = _copy_private_transform(record["local_transform"])
        except (AttributeError, ReferenceError, RuntimeError, TypeError, ValueError) as exc:
            raise RuntimeError("could not restore prevalidated render attachment") from exc

        restored_parent = getattr(current, "parent", None)
        restored_parent_identity = (
            _runtime_identity(restored_parent) if restored_parent is not None else None
        )
        if (
            restored_parent_identity != parent_identity
            or str(getattr(current, "parent_type", "")) != record["parent_type"]
            or str(getattr(current, "parent_bone", "")) != record["parent_bone"]
        ):
            raise RuntimeError(
                "restored attachment relationship does not match pre-animation proof"
            )
        if not _private_transforms_close(
            getattr(current, "matrix_parent_inverse", None),
            record["parent_inverse"],
        ):
            raise RuntimeError(
                "restored parent-inverse matrix does not match pre-animation proof"
            )
        if not _private_transforms_close(
            getattr(current, "matrix_basis", None),
            record["local_transform"],
        ):
            raise RuntimeError(
                "restored local transform does not match pre-animation proof"
            )


def revalidate_restored_inventory(
    mesh_objects: list[Any],
    required_equipment: Iterable[str],
    rig: Any,
    expected_inventory: list[dict[str, Any]],
    expected_equipment: dict[str, dict[str, Any]],
) -> None:
    try:
        inventory, equipment = build_mesh_inventory(mesh_objects, required_equipment, rig)
    except (RuntimeError, ValueError) as exc:
        raise RuntimeError("restored attachment semantic revalidation failed") from exc
    if inventory != expected_inventory or equipment != expected_equipment:
        raise RuntimeError("restored attachment semantics differ from pre-animation proof")


def find_single_rig() -> bpy.types.Object:
    rigs = [item for item in bpy.data.objects if item.type == "ARMATURE"]
    if len(rigs) != 1:
        raise RuntimeError(f"expected one armature after model import, found {len(rigs)}")
    return rigs[0]


def find_static_rig() -> Any:
    """Reject skeletal static imports instead of silently baking an arbitrary pose."""

    rigs = [item for item in bpy.data.objects if item.type == "ARMATURE"]
    if rigs:
        raise RuntimeError(
            f"static W3D import must be armature-free, found {len(rigs)} armature(s)"
        )
    return None


def assert_non_animated_scene_has_no_actions(asset_kind: str) -> None:
    if asset_kind == "animated":
        return
    actions = list(getattr(bpy.data, "actions", []) or [])
    active_actions = 0
    for item in list(getattr(bpy.data, "objects", []) or []):
        for owner in (item, getattr(item, "data", None)):
            animation_data = getattr(owner, "animation_data", None)
            if animation_data is not None and getattr(animation_data, "action", None) is not None:
                active_actions += 1
    if actions or active_actions:
        raise RuntimeError(
            f"{asset_kind} W3D import contains animation actions"
        )


def detach_actions(rig: bpy.types.Object) -> None:
    if rig.animation_data is not None:
        rig.animation_data.action = None
    if rig.data.animation_data is not None:
        rig.data.animation_data.action = None


def remove_non_render_geometry() -> dict[str, Any]:
    """Drop W3D collision/volume helpers before the GLB is exported.

    The OpenSAGE importer deliberately exposes collision boxes and other W3D
    helper geometry in Blender.  Those meshes are useful to an authoring tool,
    but glTF has no equivalent semantic and would otherwise render them as
    opaque purple boxes around every unit.
    """

    removed_count = 0
    removed_types: Counter[str] = Counter()
    removed_reasons: Counter[str] = Counter()
    for item in list(bpy.data.objects):
        if item.type != "MESH":
            continue
        reasons = _non_render_reasons(item)
        if not reasons:
            continue
        removed_count += 1
        removed_types[_w3d_object_type(item) or "UNDECLARED"] += 1
        for reason in reasons:
            removed_reasons[reason] += 1
        bpy.data.objects.remove(item, do_unlink=True)

    # Keep the exported inventory and metrics honest; Blender data blocks can
    # outlive their removed object until explicitly collected.
    for mesh in list(bpy.data.meshes):
        if mesh.users == 0:
            bpy.data.meshes.remove(mesh)
    for material in list(bpy.data.materials):
        if material.users == 0:
            bpy.data.materials.remove(material)
    return {
        "count": removed_count,
        "object_types": [
            {"type": name, "count": count} for name, count in sorted(removed_types.items())
        ],
        "reasons": [
            {"reason": name, "count": count} for name, count in sorted(removed_reasons.items())
        ],
    }


def main() -> None:
    args = parse_args()
    plugin_root = args.plugin_root.expanduser().resolve()
    model = args.model.expanduser().resolve()
    output = args.output.expanduser().resolve()
    if not model.is_file():
        raise FileNotFoundError(model)
    if not (plugin_root / "io_mesh_w3d" / "__init__.py").is_file():
        raise FileNotFoundError(plugin_root)

    sys.path.insert(0, str(plugin_root))
    import io_mesh_w3d  # type: ignore

    bpy.ops.wm.read_factory_settings(use_empty=True)
    io_mesh_w3d.register()
    result = bpy.ops.import_mesh.westwood_w3d(filepath=str(model))
    if result != {"FINISHED"}:
        raise RuntimeError(f"model import failed: {result}")
    validate_asset_kind_request(
        args.asset_kind, args.animations, args.required_equipment
    )
    rig = find_static_rig() if args.asset_kind == "static" else find_single_rig()
    if rig is not None and len(getattr(rig.data, "bones", []) or []) < 1:
        raise RuntimeError("skeletal W3D import has an empty hierarchy")
    assert_non_animated_scene_has_no_actions(args.asset_kind)
    filtered_geometry = remove_non_render_geometry()
    model_mesh_objects = [item for item in bpy.data.objects if item.type == "MESH"]
    if not model_mesh_objects:
        raise RuntimeError("W3D model import created no meshes")
    # Preserve the visual contribution of source-proven additive W3D textures
    # before the render payload is fingerprinted. Unproven materials are never
    # modified by this pass.
    convert_proven_additive_materials(list(bpy.data.materials))
    optional_mesh_exclusions = exclude_optional_render_meshes(
        model_mesh_objects,
        args.excluded_optional_meshes,
        args.required_equipment,
        rig,
    )
    model_mesh_objects = [item for item in bpy.data.objects if item.type == "MESH"]
    model_mesh_count = len(model_mesh_objects)
    if model_mesh_count < 1:
        raise RuntimeError("W3D model import retained no meshes")
    if rig is not None:
        canonicalize_required_rigid_attachments(
            model_mesh_objects, args.required_equipment, rig
        )
    # Prove equipment semantics against the model import itself. Animation-only
    # W3Ds may clear attachment parenting, so later work verifies this safe
    # render payload by private fingerprints instead of reclassifying it.
    mesh_inventory, equipment = build_mesh_inventory(
        model_mesh_objects, args.required_equipment, rig
    )
    render_geometry_proof = capture_render_geometry_proof(model_mesh_objects)
    render_attachment_proof = (
        capture_render_attachment_proof(model_mesh_objects) if rig is not None else []
    )

    imported_actions: list[bpy.types.Action] = []
    if rig is not None:
        detach_actions(rig)
    for animation in args.animations:
        source = animation.expanduser().resolve()
        if not source.is_file():
            raise FileNotFoundError(source)
        before = set(bpy.data.actions)
        result = bpy.ops.import_mesh.westwood_w3d(filepath=str(source))
        if result != {"FINISHED"}:
            raise RuntimeError(f"animation import failed for {source.name}: {result}")
        after = set(bpy.data.actions)
        created = sorted(after - before, key=lambda item: item.name.casefold())
        active = rig.animation_data.action if rig.animation_data else None
        candidates = created or ([active] if active is not None and active not in before else [])
        if len(candidates) != 1:
            if len(candidates) > 1:
                raise RuntimeError(
                    f"animation {source.name} created split object/data actions; visibility-channel merge is not supported yet"
                )
            raise RuntimeError(f"animation import created no action: {source.name}")
        action = candidates[0]
        action.name = clean_name(source.stem)
        action.use_fake_user = True
        if not action.fcurves or sum(len(curve.keyframe_points) for curve in action.fcurves) == 0:
            raise RuntimeError(f"animation action has no keyed curves: {source.name}")
        imported_actions.append(action)
        detach_actions(rig)

    if len(imported_actions) != len(args.animations):
        raise RuntimeError("requested and imported animation counts differ")
    assert_non_animated_scene_has_no_actions(args.asset_kind)

    mesh_objects = [item for item in bpy.data.objects if item.type == "MESH"]
    if rig is not None:
        restore_render_attachments(
            render_attachment_proof, mesh_objects, list(bpy.data.objects)
        )
    assert_render_geometry_unchanged(render_geometry_proof, mesh_objects)
    if rig is not None:
        revalidate_restored_inventory(
            mesh_objects,
            args.required_equipment,
            rig,
            mesh_inventory,
            equipment,
        )
    attachments_canonicalized_restored_and_revalidated = rig is not None
    mesh_count = model_mesh_count
    vertices = sum(item["vertices"] for item in mesh_inventory)
    triangles = sum(item["triangles"] for item in mesh_inventory)
    skinned_meshes = sum(1 for item in mesh_inventory if item["skinned"])
    generated_images = sorted(image.name for image in bpy.data.images if image.source == "GENERATED")
    if generated_images:
        raise RuntimeError(
            f"generated placeholder textures remain: {len(generated_images)} image(s)"
        )
    animation_curve_count = sum(len(action.fcurves) for action in imported_actions)
    animation_key_count = sum(
        len(curve.keyframe_points) for action in imported_actions for curve in action.fcurves
    )

    output.parent.mkdir(parents=True, exist_ok=True)
    export_result = bpy.ops.export_scene.gltf(
        filepath=str(output),
        export_format="GLB",
        export_animations=args.asset_kind == "animated",
        export_animation_mode="ACTIONS",
        export_skins=args.asset_kind in {"animated", "hierarchical"},
        export_morph=True,
        export_yup=True,
        export_apply=False,
        export_extras=True,
        export_cameras=False,
        export_lights=False,
    )
    if export_result != {"FINISHED"}:
        raise RuntimeError(f"glTF export failed: {export_result}")
    print(
        "OPENBFME_W3D_OK "
        + json.dumps(
            {
                "report_schema": "openbfme.w3d-adapter-report",
                "report_version": 1,
                "asset_kind": args.asset_kind,
                "meshes": mesh_count,
                "mesh_inventory": mesh_inventory,
                "required_equipment": sorted(set(args.required_equipment)),
                "equipment": equipment,
                "animations": len(imported_actions),
                "animation_curves": animation_curve_count,
                "animation_keys": animation_key_count,
                "bones": len(rig.data.bones) if rig is not None else 0,
                "skeletons": int(rig is not None),
                "vertices": vertices,
                "triangles": triangles,
                "skinned_meshes": skinned_meshes,
                "materials": len(bpy.data.materials),
                "images": len(bpy.data.images),
                "generated_images": len(generated_images),
                "filtered_non_render_geometry": filtered_geometry,
                "excluded_optional_meshes": optional_mesh_exclusions,
                "remaining_non_render_geometry": 0,
                "remaining_ambiguous_box_geometry": 0,
                "equipment_attachments_canonicalized_restored_and_revalidated": attachments_canonicalized_restored_and_revalidated,
                "fps": bpy.context.scene.render.fps,
            },
            sort_keys=True,
        )
    )


if __name__ == "__main__":
    main()
