"""Transactional source extraction, conversion, pack assembly and audit."""

from __future__ import annotations

from collections import defaultdict
from concurrent.futures import ThreadPoolExecutor, as_completed
import contextlib
from dataclasses import asdict
import hashlib
from io import BytesIO
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import struct
import sys
import tempfile
import threading
import time
from typing import Any, Mapping, Sequence
import uuid

from .big import COPY_CHUNK, sha256_file
from .catalog import CatalogEntry, InstallCatalog, KNOWN_SLICE_ARCHIVE_SHA256
from .game import retail_game, workspace_root
from .effective_assets_identity import verify_effective_assets
from .effective_assets_catalog import EffectiveAssetsCatalog
from .paths import ensure_external_to_repo, repo_root_from_module, safe_relative_parts
from .map_profile import (
    AI_LIBRARY_PLAYER_PLACEHOLDER,
    GOLLUM_SPAWN_LIBRARY_PATH,
    GOLLUM_SPAWN_LIBRARY_PLAYER,
    MULTIPLAYER_HUMAN_LIBRARY_PATH,
    MULTIPLAYER_START_TEAMS_LIBRARY_PATH,
)
from .profile import (
    ResolvedProfile,
    ResolvedResource,
    SLUG_PATTERN,
    W3D_EXCLUDED_OPTIONAL_MESHES_OPTION,
    W3D_INPUT_RESOURCE_IDS_OPTION,
    W3D_PROVEN_NO_MOTION_ANIMATIONS_OPTION,
    W3D_PROVEN_PIVOT_ONLY_MODEL_OPTION,
    W3D_PROVEN_ROOT_RIGID_BAKE_OPTION,
    W3D_RETAIL_ABSENT_TEXTURES_OPTION,
    W3D_TEXTURE_SUFFIXES,
    W3D_TEXTURE_OVERRIDES_OPTION,
    canonical_ai_library_map_runtime_slug,
    normalize_excluded_optional_meshes,
    normalize_retail_absent_textures,
    normalize_texture_atlas_crops,
    normalize_w3d_no_motion_animations,
    normalize_w3d_texture_overrides,
)
from .tools import (
    directory_tree_sha256,
    discover_executable,
    git_revision_at_exact_root,
    git_worktree_clean_at_exact_root,
    inspect_tool,
    run_checked,
)
from .util import read_json, write_json_atomic
from .incremental_rebuild import (
    rebuild_execution_provenance,
    reconvert_requested,
    validate_reconvert_matches,
    w3d_adapter_cache_identity,
)
from .version import __version__
from .w3d_metadata import scan_w3d_metadata
from .w3d_glb_validation import validate_w3d_glb_semantics
from .w3d_secondary_skin import (
    W3DSecondarySkinError,
    strip_proven_redundant_secondary_skin_streams,
)


RETAIL_PROVENANCE_CONTRACT = "openbfme.retail-import-provenance-v1"

# A shipped launcher carries its own source identity, stamped at build time by
# the release workflow beside the archived importer source. Development builds
# have no such stamp and derive identity from Git instead. The two are
# different mechanisms and the recipe says which one produced its commit, so a
# pack's provenance can be read back without guessing how it was made.
RELEASE_IDENTITY_RELATIVE = "release-identity.json"
RELEASE_IDENTITY_SCHEMA = "openbfme.bundled-source-identity"
RELEASE_IDENTITY_SCHEMA_VERSION = 1
PROVENANCE_SOURCE_RELEASE_IDENTITY = "release-identity"
PROVENANCE_SOURCE_GIT_EXACT_ROOT = "git-exact-root"
PROVENANCE_SOURCES = frozenset(
    {PROVENANCE_SOURCE_RELEASE_IDENTITY, PROVENANCE_SOURCE_GIT_EXACT_ROOT}
)
# Release bundles archive the release pin; a checkout also carries the wider
# development pin. Hash whichever are actually present rather than assuming a
# fixed name, and record the answer.
IMPORTER_REQUIREMENTS_NAMES = (
    "requirements-release-win.txt",
    "requirements-win.txt",
)

# The current base profile resolves 335 per-resource asset/data rows, plus the
# 56 documents the living-world strategic rule resolves from the BFME2 1.06
# catalog (the riskcampaign #include closure and its entry points), plus the
# exact map + four retail AI-library rows owned by the map-script composite.
# Provenance is per resource, not a unique-physical-source census: intentional
# W3D source variants therefore remain separate auditable rows.
MEN_FORDS_SOURCE_ENTRY_COUNT = 335 + 56 + 5
MAX_RENDERED_OUTPUT_PATH = 512
W3D_ADAPTER_REPORT_CONTRACT = "openbfme.w3d-adapter-report"
W3D_PRESENTATION_METADATA_CONTRACT = "openbfme.w3d-presentation-capabilities"
W3D_EQUIPMENT_ROLES = {"right-hand-weapon", "left-hand-shield"}
W3D_MESH_ROLES = {"character-mesh", *W3D_EQUIPMENT_ROLES}
W3D_ATTACHMENTS = {"scene", "skeletal", "right-hand", "left-hand"}
W3D_PROOF_METHODS = {
    "attachment-semantic",
    "custom-attachment",
    "dominant-weight-group",
    "material-semantic",
    "mesh-semantic",
    "parent-bone",
    "source-equipment-pivot",
    "weighted-hand-group",
    "rest-pose-proximity",
    "weighted-hand-dominance",
}
W3D_SHADER_BOOLEAN_PROPERTIES = (
    "AlphaBlendingEnable",
    "FogEnable",
)
W3D_SHADER_COMPATIBILITY_REPORT_KEYS = {
    "mapped_materials",
    "mapped_material_count",
    "mapped_property_count",
    "alpha_blending_enable_count",
    "fog_enable_count",
    "source_flags_preserved",
}
W3D_OPAQUE_NORMALIZATION_REPORT_KEYS = {
    "normalized_materials",
    "normalized_material_count",
    "removed_alpha_link_count",
    "source_blend_state_preserved",
}
W3D_TEXTURE_NAME_CHUNK = 0x00000032
W3D_SHADER_MATERIAL_PROPERTY_CHUNK = 0x00000053
W3D_SHADER_STRING_PROPERTY = 1
W3D_CHUNK_HAS_CHILDREN = 0x80000000
W3D_SECONDARY_SKIN_CHUNKS = frozenset({0x00000C00, 0x00000C01})
W3D_DDS_DECODE_MAX_RGB_DELTA = 2
GLB_JSON_CHUNK = 0x4E4F534A
GLB_BINARY_CHUNK = 0x004E4942
RESOURCE_BUNDLE_CONVERTERS = {
    "w3d-bundle",
    "w3d-hierarchical",
    "w3d-static",
    "sage-terrain-materials",
    "sage-apt-runtime",
    "sage-apt-shell-runtime",
    "sage-apt-screen-runtime",
    "retail-unit-rules",
    "living-world",
    "sage-script-composite",
    "texture-atlas-crops",
}
EFFECTIVE_ASSET_MANIFEST_SCHEMA = "openbfme.effective-assets-manifest"
EFFECTIVE_ASSET_MANIFEST_VERSION = 0
EFFECTIVE_ASSET_METADATA_DIRECTORY = ".openbfme"
EFFECTIVE_ASSET_MANIFEST_RELATIVE = (
    f"{EFFECTIVE_ASSET_METADATA_DIRECTORY}/manifest.json"
)
MAX_EFFECTIVE_ASSET_FILES = 50_000
MAX_EFFECTIVE_ASSET_BYTES = 8 * 1024 * 1024 * 1024


def _normalize_required_equipment(value: Any) -> list[str]:
    if not isinstance(value, list) or any(not isinstance(item, str) for item in value):
        raise ValueError(
            "w3d-bundle options.required_equipment must be an array of strings"
        )
    if len(value) != len(set(value)):
        raise ValueError("w3d-bundle options.required_equipment contains duplicates")
    unsupported = sorted(set(value) - W3D_EQUIPMENT_ROLES)
    if unsupported:
        raise ValueError(
            "unsupported required W3D equipment semantics: " + ", ".join(unsupported)
        )
    return sorted(value)


def _report_int(report: Mapping[str, Any], key: str, *, minimum: int = 0) -> int:
    value = report.get(key)
    if isinstance(value, bool) or not isinstance(value, int) or value < minimum:
        raise RuntimeError(f"W3D adapter report has invalid {key}")
    return value


def _report_sha256(report: Mapping[str, Any], key: str) -> str:
    value = report.get(key)
    if (
        not isinstance(value, str)
        or len(value) != 64
        or value != value.casefold()
        or any(character not in "0123456789abcdef" for character in value)
    ):
        raise RuntimeError(f"W3D adapter report has invalid {key}")
    return value


def _validated_shader_material_compatibility(
    report: Mapping[str, Any],
    *,
    material_count: int,
) -> dict[str, Any]:
    """Validate and canonicalize source shader booleans mapped by the adapter."""

    raw = report.get("shader_material_compatibility")
    if not isinstance(raw, Mapping) or set(raw) != W3D_SHADER_COMPATIBILITY_REPORT_KEYS:
        raise RuntimeError("W3D adapter shader compatibility report is invalid")

    source_flags_preserved = raw.get("source_flags_preserved")
    if type(source_flags_preserved) is not bool or not source_flags_preserved:
        raise RuntimeError(
            "W3D adapter shader compatibility preservation proof is invalid"
        )

    raw_materials = raw.get("mapped_materials")
    if not isinstance(raw_materials, list):
        raise RuntimeError(
            "W3D adapter shader compatibility material report is invalid"
        )

    mapped_materials: list[dict[str, Any]] = []
    seen_material_names: set[str] = set()
    row_keys = {"material", "properties"}
    supported_properties = set(W3D_SHADER_BOOLEAN_PROPERTIES)
    for raw_material in raw_materials:
        if not isinstance(raw_material, Mapping) or set(raw_material) != row_keys:
            raise RuntimeError(
                "W3D adapter shader compatibility material entry is invalid"
            )
        material_name = raw_material.get("material")
        if not isinstance(material_name, str) or not material_name:
            raise RuntimeError(
                "W3D adapter shader compatibility material name is invalid"
            )
        folded_name = material_name.casefold()
        if folded_name in seen_material_names:
            raise RuntimeError(
                "W3D adapter shader compatibility material names are ambiguous"
            )
        seen_material_names.add(folded_name)

        raw_properties = raw_material.get("properties")
        if (
            not isinstance(raw_properties, Mapping)
            or not raw_properties
            or not set(raw_properties).issubset(supported_properties)
            or any(type(value) is not bool for value in raw_properties.values())
        ):
            raise RuntimeError(
                "W3D adapter shader compatibility properties are invalid"
            )
        mapped_materials.append(
            {
                "material": material_name,
                "properties": {
                    property_name: raw_properties[property_name]
                    for property_name in W3D_SHADER_BOOLEAN_PROPERTIES
                    if property_name in raw_properties
                },
            }
        )

    canonical_names = sorted(
        (item["material"] for item in mapped_materials),
        key=lambda name: (name.casefold(), name),
    )
    if [item["material"] for item in mapped_materials] != canonical_names:
        raise RuntimeError(
            "W3D adapter shader compatibility material order is not canonical"
        )

    mapped_material_count = _report_int(raw, "mapped_material_count")
    mapped_property_count = _report_int(raw, "mapped_property_count")
    alpha_blending_enable_count = _report_int(raw, "alpha_blending_enable_count")
    fog_enable_count = _report_int(raw, "fog_enable_count")
    actual_alpha_count = sum(
        "AlphaBlendingEnable" in item["properties"] for item in mapped_materials
    )
    actual_fog_count = sum(
        "FogEnable" in item["properties"] for item in mapped_materials
    )
    if (
        mapped_material_count != len(mapped_materials)
        or mapped_material_count > material_count
        or alpha_blending_enable_count != actual_alpha_count
        or fog_enable_count != actual_fog_count
        or mapped_property_count != actual_alpha_count + actual_fog_count
    ):
        raise RuntimeError(
            "W3D adapter shader compatibility counts disagree with its materials"
        )

    return {
        "mappedMaterials": mapped_materials,
        "mappedMaterialCount": mapped_material_count,
        "mappedPropertyCount": mapped_property_count,
        "alphaBlendingEnableCount": alpha_blending_enable_count,
        "fogEnableCount": fog_enable_count,
        "sourceFlagsPreserved": source_flags_preserved,
    }


def _validated_opaque_material_normalization(
    report: Mapping[str, Any], *, material_count: int
) -> dict[str, Any]:
    raw = report.get("opaque_material_normalization")
    if raw is None:
        return {
            "normalizedMaterials": [],
            "normalizedMaterialCount": 0,
            "removedAlphaLinkCount": 0,
            "sourceBlendStatePreserved": False,
        }
    if not isinstance(raw, Mapping) or set(raw) != W3D_OPAQUE_NORMALIZATION_REPORT_KEYS:
        raise RuntimeError("W3D adapter opaque material normalization report is invalid")
    preserved = raw.get("source_blend_state_preserved")
    if type(preserved) is not bool or not preserved:
        raise RuntimeError("W3D adapter opaque material normalization proof is invalid")
    raw_rows = raw.get("normalized_materials")
    if not isinstance(raw_rows, list):
        raise RuntimeError("W3D adapter opaque material rows are invalid")
    rows: list[dict[str, Any]] = []
    names: set[str] = set()
    expected_keys = {
        "material",
        "source_blend",
        "destination_blend",
        "alpha_test",
        "removed_alpha_links",
    }
    for raw_row in raw_rows:
        if not isinstance(raw_row, Mapping) or set(raw_row) != expected_keys:
            raise RuntimeError("W3D adapter opaque material row is invalid")
        name = raw_row.get("material")
        if not isinstance(name, str) or not name or name.casefold() in names:
            raise RuntimeError("W3D adapter opaque material names are invalid")
        names.add(name.casefold())
        source = raw_row.get("source_blend")
        destination = raw_row.get("destination_blend")
        alpha_test = raw_row.get("alpha_test")
        removed = raw_row.get("removed_alpha_links")
        if (
            source != 1
            or destination != 0
            or alpha_test != 0
            or isinstance(removed, bool)
            or not isinstance(removed, int)
            or removed < 0
        ):
            raise RuntimeError("W3D adapter opaque material source state is invalid")
        rows.append(
            {
                "material": name,
                "sourceBlend": source,
                "destinationBlend": destination,
                "alphaTest": alpha_test,
                "removedAlphaLinks": removed,
            }
        )
    canonical_names = sorted(
        (row["material"] for row in rows), key=lambda value: (value.casefold(), value)
    )
    if [row["material"] for row in rows] != canonical_names:
        raise RuntimeError("W3D adapter opaque material order is not canonical")
    normalized_count = _report_int(raw, "normalized_material_count")
    removed_count = _report_int(raw, "removed_alpha_link_count")
    if (
        normalized_count != len(rows)
        or normalized_count > material_count
        or removed_count != sum(row["removedAlphaLinks"] for row in rows)
    ):
        raise RuntimeError("W3D adapter opaque material counts are inconsistent")
    return {
        "normalizedMaterials": rows,
        "normalizedMaterialCount": normalized_count,
        "removedAlphaLinkCount": removed_count,
        "sourceBlendStatePreserved": preserved,
    }


def _validated_w3d_metadata(
    report: Mapping[str, Any],
    required_equipment: list[str],
    *,
    expected_animation_count: int | None = None,
    asset_kind: str = "animated",
    expected_excluded_optional_meshes: list[str] | None = None,
    expected_proven_root_rigid_bake: bool = False,
    expected_embedded_model_animation: bool = False,
    expected_pivot_only_model: bool = False,
) -> dict[str, Any]:
    """Validate the private adapter report and return payload-free bundle facts."""

    if not isinstance(report, Mapping):
        raise RuntimeError("W3D adapter report root is not an object")
    report_version = report.get("report_version")
    if (
        report.get("report_schema") != W3D_ADAPTER_REPORT_CONTRACT
        or report_version not in {1, 2}
    ):
        raise RuntimeError("W3D adapter report contract is unsupported")
    typed_action_report = report_version == 2
    expected_required = _normalize_required_equipment(required_equipment)
    if asset_kind not in {"animated", "hierarchical", "static"}:
        raise ValueError(f"unsupported W3D asset kind: {asset_kind}")
    if not isinstance(expected_proven_root_rigid_bake, bool):
        raise ValueError("expected proven root-rigid bake must be a boolean")
    if not isinstance(expected_embedded_model_animation, bool):
        raise ValueError("expected embedded model animation must be a boolean")
    if not isinstance(expected_pivot_only_model, bool):
        raise ValueError("expected pivot-only model must be a boolean")
    if expected_proven_root_rigid_bake and asset_kind != "hierarchical":
        raise ValueError(
            "proven root-rigid bake is supported only for hierarchical W3D conversion"
        )
    if expected_embedded_model_animation and asset_kind != "animated":
        raise ValueError(
            "embedded model animation is supported only for animated W3D conversion"
        )
    if expected_pivot_only_model and asset_kind != "hierarchical":
        raise ValueError(
            "proven pivot-only model is supported only for hierarchical W3D conversion"
        )
    if expected_pivot_only_model and expected_proven_root_rigid_bake:
        raise ValueError(
            "proven pivot-only model cannot combine with proven root-rigid bake"
        )
    reported_asset_kind = report.get("asset_kind", "animated")
    if reported_asset_kind != asset_kind:
        raise RuntimeError(
            "W3D adapter asset kind does not match the conversion request"
        )
    if asset_kind in {"hierarchical", "static"} and expected_required:
        raise RuntimeError(
            f"{asset_kind} W3D conversion cannot require skeletal equipment"
        )
    reported_required = _normalize_required_equipment(report.get("required_equipment"))
    if reported_required != expected_required:
        raise RuntimeError(
            "W3D adapter did not enforce the requested equipment semantics"
        )

    mesh_count = _report_int(report, "meshes", minimum=0 if expected_pivot_only_model else 1)
    raw_root_rigid_bake = report.get("root_rigid_bake")
    root_rigid_keys = {
        "requested",
        "applied",
        "removed_carriers",
        "baked_meshes",
        "world_transforms_preserved",
        "deform_ambiguity_absent",
    }
    if (
        not isinstance(raw_root_rigid_bake, Mapping)
        or set(raw_root_rigid_bake) != root_rigid_keys
    ):
        raise RuntimeError("W3D adapter root-rigid bake report is invalid")
    root_rigid_requested = raw_root_rigid_bake.get("requested")
    root_rigid_applied = raw_root_rigid_bake.get("applied")
    world_transforms_preserved = raw_root_rigid_bake.get("world_transforms_preserved")
    deform_ambiguity_absent = raw_root_rigid_bake.get("deform_ambiguity_absent")
    if not all(
        isinstance(value, bool)
        for value in (
            root_rigid_requested,
            root_rigid_applied,
            world_transforms_preserved,
            deform_ambiguity_absent,
        )
    ):
        raise RuntimeError("W3D adapter root-rigid bake proof is invalid")
    removed_root_carriers = _report_int(raw_root_rigid_bake, "removed_carriers")
    root_rigid_baked_meshes = _report_int(raw_root_rigid_bake, "baked_meshes")
    if root_rigid_requested is not expected_proven_root_rigid_bake:
        raise RuntimeError(
            "W3D adapter root-rigid bake request does not match the profile"
        )
    if root_rigid_applied is not expected_proven_root_rigid_bake:
        raise RuntimeError(
            "W3D adapter root-rigid bake result does not match the profile"
        )
    if expected_proven_root_rigid_bake:
        if (
            removed_root_carriers != 1
            or root_rigid_baked_meshes != mesh_count
            or world_transforms_preserved is not True
            or deform_ambiguity_absent is not True
        ):
            raise RuntimeError("W3D adapter root-rigid bake proof is incomplete")
    elif any(
        (
            removed_root_carriers,
            root_rigid_baked_meshes,
            world_transforms_preserved,
            deform_ambiguity_absent,
        )
    ):
        raise RuntimeError("W3D adapter reported an unexpected root-rigid bake")
    animated = asset_kind == "animated"
    skeletal = asset_kind in {"animated", "hierarchical"} and not root_rigid_applied
    # The adapter proves which skeletal content the scene actually carried:
    # skinned meshes must survive as exported skins, and a model rig must have
    # skeleton-bound geometry. Proven rigid animated models (bone- or
    # armature-parented meshes, no weights) legitimately export no skins;
    # rigless animated composite carriers legitimately export neither.
    model_skinned_mesh_count = report.get("skinned_meshes")
    if (
        isinstance(model_skinned_mesh_count, bool)
        or not isinstance(model_skinned_mesh_count, int)
        or model_skinned_mesh_count < 0
    ):
        raise RuntimeError("W3D adapter report has invalid skinned_meshes")
    model_skeleton_count = report.get("skeletons")
    if model_skeleton_count is None:
        model_skeleton_count = 1 if animated else 0
    if (
        isinstance(model_skeleton_count, bool)
        or not isinstance(model_skeleton_count, int)
        or model_skeleton_count < 0
    ):
        raise RuntimeError("W3D adapter report has an invalid skeleton count")
    exported_skins_required = animated and model_skinned_mesh_count > 0
    exported_skeletal_meshes_required = animated and (
        model_skinned_mesh_count > 0 or model_skeleton_count > 0
    )
    animation_count = _report_int(report, "animations", minimum=1 if animated else 0)
    animation_curve_count = _report_int(
        report, "animation_curves", minimum=1 if animated else 0
    )
    animation_key_count = _report_int(
        report, "animation_keys", minimum=1 if animated else 0
    )
    if not animated and any(
        (animation_count, animation_curve_count, animation_key_count)
    ):
        raise RuntimeError(f"{asset_kind} W3D adapter report contains animation data")
    action_report: Mapping[str, Any] = report
    if not typed_action_report:
        legacy_shapes = []
        remaining_curves = animation_curve_count
        for index in range(animation_count):
            clips_left = animation_count - index
            curve_count = max(1, remaining_curves - (clips_left - 1))
            remaining_curves -= curve_count
            legacy_shapes.append(
                {
                    "name": f"legacy-clip-{index}",
                    "shape": "transform-only",
                    "action_count": 1,
                    "object_action_count": 1,
                    "armature_action_count": 0,
                    "transform_curve_count": curve_count,
                    "visibility_curve_count": 0,
                    "material_curve_count": 0,
                    "unsupported_curve_count": 0,
                }
            )
        action_report = {
            **report,
            "animation_action_shapes": legacy_shapes,
            "action_shape_animation_count": animation_count,
            "action_shape_action_count": animation_count,
            "action_shape_nla_track_count": animation_count,
            "action_shape_exported_animation_count": animation_count,
            "action_shape_exported_channel_count": animation_curve_count,
            "action_shape_exported_sampler_count": animation_curve_count,
            "action_shape_exported_skin_count": int(animated),
            "action_shape_exported_skeletal_mesh_count": int(animated),
            "duplicated_logical_animation_count": 0,
            "preserved_visibility_channel_count": 0,
            "preserved_visibility_key_count": 0,
            "visibility_only_sidecar_animation_count": 0,
        }
    raw_action_shapes = action_report.get("animation_action_shapes")
    if not isinstance(raw_action_shapes, list) or len(raw_action_shapes) != animation_count:
        raise RuntimeError("W3D adapter action-shape report is invalid")
    action_shape_keys = {
        "name",
        "shape",
        "action_count",
        "object_action_count",
        "armature_action_count",
        "transform_curve_count",
        "visibility_curve_count",
        "material_curve_count",
        "unsupported_curve_count",
    }
    action_shapes: list[dict[str, Any]] = []
    seen_action_names: set[str] = set()
    for raw_shape in raw_action_shapes:
        if not isinstance(raw_shape, Mapping) or set(raw_shape) != action_shape_keys:
            raise RuntimeError("W3D adapter action-shape entry is invalid")
        name = raw_shape.get("name")
        shape = raw_shape.get("shape")
        if (
            not isinstance(name, str)
            or not SLUG_PATTERN.fullmatch(name)
            or name in seen_action_names
            or shape
            not in {"transform-only", "transform-and-visibility", "visibility-only"}
        ):
            raise RuntimeError("W3D adapter action-shape identity is invalid")
        seen_action_names.add(name)
        counts = {
            key: _report_int(raw_shape, key)
            for key in action_shape_keys
            if key not in {"name", "shape"}
        }
        if (
            counts["action_count"] not in {1, 2}
            or counts["object_action_count"] not in {0, 1}
            or counts["armature_action_count"] not in {0, 1}
            or counts["action_count"]
            != counts["object_action_count"] + counts["armature_action_count"]
            or counts["unsupported_curve_count"] != 0
            or counts["material_curve_count"] != 0
        ):
            raise RuntimeError("W3D adapter action-shape owner proof is invalid")
        expected_shape = (
            "transform-and-visibility"
            if counts["transform_curve_count"]
            and counts["visibility_curve_count"]
            else "transform-only"
            if counts["transform_curve_count"]
            else "visibility-only"
            if counts["visibility_curve_count"]
            else None
        )
        if shape != expected_shape:
            raise RuntimeError("W3D adapter action-shape channel proof is invalid")
        action_shapes.append({"name": name, "shape": shape, **counts})

    action_shape_animation_count = _report_int(
        action_report, "action_shape_animation_count"
    )
    action_shape_action_count = _report_int(
        action_report, "action_shape_action_count"
    )
    action_shape_nla_track_count = _report_int(
        action_report, "action_shape_nla_track_count"
    )
    action_shape_exported_animation_count = _report_int(
        action_report, "action_shape_exported_animation_count"
    )
    action_shape_exported_channel_count = _report_int(
        action_report, "action_shape_exported_channel_count"
    )
    action_shape_exported_sampler_count = _report_int(
        action_report, "action_shape_exported_sampler_count"
    )
    action_shape_exported_skin_count = _report_int(
        action_report, "action_shape_exported_skin_count"
    )
    action_shape_exported_skeletal_mesh_count = _report_int(
        action_report, "action_shape_exported_skeletal_mesh_count"
    )
    duplicated_logical_animation_count = _report_int(
        action_report, "duplicated_logical_animation_count"
    )
    preserved_visibility_channel_count = _report_int(
        action_report, "preserved_visibility_channel_count"
    )
    preserved_visibility_key_count = _report_int(
        action_report, "preserved_visibility_key_count"
    )
    visibility_only_sidecar_animation_count = _report_int(
        action_report, "visibility_only_sidecar_animation_count"
    )
    expected_action_count = sum(item["action_count"] for item in action_shapes)
    expected_curve_count = sum(
        item["transform_curve_count"]
        + item["visibility_curve_count"]
        + item["material_curve_count"]
        + item["unsupported_curve_count"]
        for item in action_shapes
    )
    expected_visibility_channels = sum(
        item["visibility_curve_count"] for item in action_shapes
    )
    expected_transform_animation_count = sum(
        1 for item in action_shapes if item["transform_curve_count"] > 0
    )
    expected_visibility_only_count = sum(
        1 for item in action_shapes if item["shape"] == "visibility-only"
    )
    if (
        action_shape_animation_count != animation_count
        or action_shape_action_count != expected_action_count
        or action_shape_nla_track_count != expected_transform_animation_count
        or expected_curve_count != animation_curve_count
        or action_shape_exported_animation_count
        != expected_transform_animation_count
        or visibility_only_sidecar_animation_count != expected_visibility_only_count
        or action_shape_exported_animation_count
        + visibility_only_sidecar_animation_count
        != animation_count
        or (
            expected_transform_animation_count > 0
            and action_shape_exported_channel_count < 1
        )
        or (
            expected_transform_animation_count > 0
            and action_shape_exported_sampler_count < 1
        )
        or (
            expected_transform_animation_count == 0
            and action_shape_exported_channel_count != 0
        )
        or (
            expected_transform_animation_count == 0
            and action_shape_exported_sampler_count != 0
        )
        or (exported_skins_required and action_shape_exported_skin_count < 1)
        or (
            exported_skeletal_meshes_required
            and action_shape_exported_skeletal_mesh_count < 1
        )
        or duplicated_logical_animation_count
        >= max(1, expected_transform_animation_count)
        or preserved_visibility_channel_count != expected_visibility_channels
        or (
            preserved_visibility_channel_count > 0
            and preserved_visibility_key_count < preserved_visibility_channel_count
        )
        or (
            preserved_visibility_channel_count == 0
            and preserved_visibility_key_count != 0
        )
    ):
        raise RuntimeError("W3D adapter action-shape export proof is incomplete")
    reported_embedded_model_animation = report.get("embedded_model_animation", False)
    if type(reported_embedded_model_animation) is not bool:
        raise RuntimeError("W3D adapter embedded-animation proof is invalid")
    if reported_embedded_model_animation is not expected_embedded_model_animation:
        raise RuntimeError(
            "W3D adapter embedded-animation proof does not match the request"
        )

    embedded_counts: dict[str, int] = {}
    for report_key, canonical_key in (
        ("embedded_model_action_count", "actionCount"),
        ("embedded_exported_animation_count", "exportedAnimationCount"),
        ("embedded_exported_channel_count", "exportedChannelCount"),
        ("embedded_exported_sampler_count", "exportedSamplerCount"),
        ("embedded_exported_skin_count", "exportedSkinCount"),
        (
            "embedded_exported_skeletal_mesh_count",
            "exportedSkeletalMeshCount",
        ),
    ):
        raw_count = report.get(report_key, 0)
        if (
            isinstance(raw_count, bool)
            or not isinstance(raw_count, int)
            or raw_count < 0
        ):
            raise RuntimeError("W3D adapter embedded-animation counts are invalid")
        embedded_counts[canonical_key] = raw_count
    if expected_embedded_model_animation:
        embedded_transform_export_is_exact = (
            embedded_counts["exportedAnimationCount"]
            == expected_transform_animation_count
            and (
                (
                    expected_transform_animation_count > 0
                    and embedded_counts["exportedChannelCount"] >= 1
                    and embedded_counts["exportedSamplerCount"] >= 1
                )
                or (
                    expected_transform_animation_count == 0
                    and embedded_counts["exportedChannelCount"] == 0
                    and embedded_counts["exportedSamplerCount"] == 0
                )
            )
        )
        if (
            expected_animation_count != 1
            or animation_count != 1
            or embedded_counts["actionCount"] != action_shape_action_count
            or not embedded_transform_export_is_exact
            or (exported_skins_required and embedded_counts["exportedSkinCount"] < 1)
            or (
                exported_skeletal_meshes_required
                and embedded_counts["exportedSkeletalMeshCount"] < 1
            )
        ):
            raise RuntimeError("W3D adapter embedded-animation proof is incomplete")
    elif any(embedded_counts.values()):
        raise RuntimeError("W3D adapter reported an unexpected embedded animation")

    split_counts: dict[str, int] = {}
    for report_key, canonical_key in (
        ("split_action_animation_count", "animationCount"),
        ("split_action_count", "actionCount"),
        ("split_exported_animation_count", "exportedAnimationCount"),
        ("split_exported_channel_count", "exportedChannelCount"),
        ("split_exported_sampler_count", "exportedSamplerCount"),
        ("split_exported_skin_count", "exportedSkinCount"),
        (
            "split_exported_skeletal_mesh_count",
            "exportedSkeletalMeshCount",
        ),
    ):
        raw_count = report.get(report_key, 0)
        if (
            isinstance(raw_count, bool)
            or not isinstance(raw_count, int)
            or raw_count < 0
        ):
            raise RuntimeError("W3D adapter split-animation counts are invalid")
        split_counts[canonical_key] = raw_count
    split_animation_count = split_counts["animationCount"]
    expected_split_animation_count = sum(
        1
        for item in action_shapes
        if item["object_action_count"] == 1
        and item["armature_action_count"] == 1
    )
    if split_animation_count != expected_split_animation_count:
        raise RuntimeError("W3D adapter split-animation shape proof is inconsistent")
    if split_animation_count:
        split_transform_export_is_exact = (
            split_counts["exportedAnimationCount"]
            == expected_transform_animation_count
            and (
                (
                    expected_transform_animation_count > 0
                    and split_counts["exportedChannelCount"] >= 1
                    and split_counts["exportedSamplerCount"] >= 1
                )
                or (
                    expected_transform_animation_count == 0
                    and split_counts["exportedChannelCount"] == 0
                    and split_counts["exportedSamplerCount"] == 0
                )
            )
        )
        if (
            not animated
            or split_animation_count > animation_count
            or split_counts["actionCount"] != split_animation_count * 2
            or not split_transform_export_is_exact
            or (exported_skins_required and split_counts["exportedSkinCount"] < 1)
            or (
                exported_skeletal_meshes_required
                and split_counts["exportedSkeletalMeshCount"] < 1
            )
        ):
            raise RuntimeError("W3D adapter split-animation proof is incomplete")
    elif any(split_counts.values()):
        raise RuntimeError("W3D adapter reported inconsistent split-animation proof")
    bone_count = _report_int(
        report,
        "bones",
        minimum=1 if skeletal and model_skinned_mesh_count > 0 else 0,
    )
    raw_skeleton_count = report.get("skeletons")
    if raw_skeleton_count is None:
        if asset_kind == "hierarchical":
            raise RuntimeError("hierarchical W3D adapter report has no skeleton count")
        skeleton_count = 1 if animated else 0
    else:
        skeleton_count = _report_int(report, "skeletons")
    if asset_kind == "animated":
        # Rigless animated composite carriers are proven by the adapter when
        # every clip keys its own auxiliary rig; any other animated model
        # still carries exactly one model rig.
        allowed_skeleton_counts = {0, 1}
    elif skeletal:
        allowed_skeleton_counts = {1}
    else:
        allowed_skeleton_counts = {0}
    if skeleton_count not in allowed_skeleton_counts:
        raise RuntimeError("W3D adapter skeleton count does not match the asset kind")
    vertex_count = _report_int(
        report, "vertices", minimum=0 if expected_pivot_only_model else 1
    )
    triangle_count = _report_int(
        report, "triangles", minimum=0 if expected_pivot_only_model else 1
    )
    skinned_mesh_count = _report_int(report, "skinned_meshes")
    if not typed_action_report and animated and skinned_mesh_count < 1:
        raise RuntimeError("W3D adapter report has invalid skinned_meshes")
    material_count = _report_int(report, "materials")
    image_count = _report_int(report, "images")
    generated_image_count = _report_int(report, "generated_images")
    shader_material_compatibility = _validated_shader_material_compatibility(
        report,
        material_count=material_count,
    )
    opaque_material_normalization = _validated_opaque_material_normalization(
        report,
        material_count=material_count,
    )
    remaining_non_render = _report_int(report, "remaining_non_render_geometry")
    remaining_ambiguous_boxes = _report_int(report, "remaining_ambiguous_box_geometry")
    attachments_canonicalized_restored_and_revalidated = report.get(
        "equipment_attachments_canonicalized_restored_and_revalidated"
    )
    if not isinstance(attachments_canonicalized_restored_and_revalidated, bool):
        raise RuntimeError("W3D adapter attachment canonicalization proof is invalid")
    if expected_required and not attachments_canonicalized_restored_and_revalidated:
        raise RuntimeError(
            "W3D adapter did not canonicalize, restore, and revalidate requested equipment attachments"
        )
    fps = _report_int(report, "fps", minimum=1)
    if (
        expected_animation_count is not None
        and animation_count != expected_animation_count
    ):
        raise RuntimeError(
            "W3D adapter animation count does not match the conversion request"
        )
    if not animated and any(
        (animation_count, animation_curve_count, animation_key_count)
    ):
        raise RuntimeError(f"{asset_kind} W3D adapter report contains animation data")
    if asset_kind == "static" and any((bone_count, skeleton_count, skinned_mesh_count)):
        raise RuntimeError("static W3D adapter report contains skeletal data")
    if root_rigid_applied and any((bone_count, skeleton_count, skinned_mesh_count)):
        raise RuntimeError("root-rigid W3D adapter report contains skeletal data")
    if generated_image_count != 0:
        raise RuntimeError("W3D adapter report retains generated placeholder images")
    if remaining_non_render != 0:
        raise RuntimeError(
            "W3D adapter report retains renderable collision or helper geometry"
        )
    if remaining_ambiguous_boxes != 0:
        raise RuntimeError(
            "W3D adapter report retains ambiguous box-shaped render geometry"
        )

    filtered = report.get("filtered_non_render_geometry")
    if not isinstance(filtered, Mapping):
        raise RuntimeError("W3D adapter filtering report is missing")
    filtered_count = _report_int(filtered, "count")

    expected_exclusions = normalize_excluded_optional_meshes(
        expected_excluded_optional_meshes or []
    )
    raw_exclusions = report.get("excluded_optional_meshes", [])
    if not isinstance(raw_exclusions, list):
        raise RuntimeError("W3D adapter optional mesh exclusion report is not an array")
    if len(raw_exclusions) != len(expected_exclusions):
        raise RuntimeError(
            "W3D adapter optional mesh exclusions do not match the request"
        )
    optional_mesh_exclusions: list[dict[str, Any]] = []
    exclusion_keys = {
        "identifier",
        "geometry_sha256",
        "materials_sha256",
        "vertices",
        "triangles",
        "material_slots",
    }
    for expected_identifier, raw in zip(expected_exclusions, raw_exclusions):
        if not isinstance(raw, Mapping) or set(raw) != exclusion_keys:
            raise RuntimeError("W3D adapter optional mesh exclusion entry is invalid")
        if raw.get("identifier") != expected_identifier:
            raise RuntimeError(
                "W3D adapter optional mesh exclusions do not match the request"
            )
        optional_mesh_exclusions.append(
            {
                "identifier": expected_identifier,
                "geometrySha256": _report_sha256(raw, "geometry_sha256"),
                "materialsSha256": _report_sha256(raw, "materials_sha256"),
                "vertexCount": _report_int(raw, "vertices"),
                "triangleCount": _report_int(raw, "triangles"),
                "materialSlotCount": _report_int(raw, "material_slots"),
            }
        )

    raw_inventory = report.get("mesh_inventory")
    if not isinstance(raw_inventory, list) or len(raw_inventory) != mesh_count:
        raise RuntimeError("W3D adapter mesh inventory does not match its mesh count")
    inventory: list[dict[str, Any]] = []
    for expected_index, raw in enumerate(raw_inventory):
        if not isinstance(raw, Mapping):
            raise RuntimeError("W3D adapter mesh inventory entry is not an object")
        index = _report_int(raw, "index")
        if index != expected_index:
            raise RuntimeError("W3D adapter mesh inventory order is not canonical")
        role = raw.get("semantic_role")
        attachment = raw.get("attachment")
        if role not in W3D_MESH_ROLES or attachment not in W3D_ATTACHMENTS:
            raise RuntimeError("W3D adapter mesh inventory has unsupported semantics")
        proof_methods = raw.get("proof_methods")
        if (
            not isinstance(proof_methods, list)
            or any(not isinstance(item, str) for item in proof_methods)
            or proof_methods != sorted(set(proof_methods))
            or not set(proof_methods).issubset(W3D_PROOF_METHODS)
        ):
            raise RuntimeError(
                "W3D adapter mesh proof methods are invalid or non-canonical"
            )
        if role == "right-hand-weapon" and attachment != "right-hand":
            raise RuntimeError("W3D weapon mesh is not proven on the right hand")
        if role == "left-hand-shield" and attachment != "left-hand":
            raise RuntimeError("W3D shield mesh is not proven on the left hand")
        if role in W3D_EQUIPMENT_ROLES:
            if not set(proof_methods) & {
                "attachment-semantic",
                "material-semantic",
                "mesh-semantic",
            }:
                raise RuntimeError("W3D equipment mesh lacks semantic role evidence")
            if not set(proof_methods) & {
                "custom-attachment",
                "dominant-weight-group",
                "parent-bone",
                "source-equipment-pivot",
                "weighted-hand-group",
                "rest-pose-proximity",
                "weighted-hand-dominance",
            }:
                raise RuntimeError("W3D equipment mesh lacks attachment evidence")
        skinned = raw.get("skinned")
        if not isinstance(skinned, bool):
            raise RuntimeError("W3D adapter mesh inventory has an invalid skinned flag")
        if (asset_kind == "static" or root_rigid_applied) and (
            role != "character-mesh"
            or attachment != "scene"
            or proof_methods
            or skinned
        ):
            qualifier = "root-rigid" if root_rigid_applied else "static"
            raise RuntimeError(
                f"{qualifier} W3D mesh inventory contains skeletal semantics"
            )
        inventory.append(
            {
                "index": index,
                "semanticRole": role,
                "attachment": attachment,
                "proofMethods": list(proof_methods),
                "vertexCount": _report_int(raw, "vertices"),
                "triangleCount": _report_int(raw, "triangles"),
                "materialSlotCount": _report_int(raw, "material_slots"),
                "skinned": skinned,
            }
        )

    if sum(item["vertexCount"] for item in inventory) != vertex_count:
        raise RuntimeError(
            "W3D adapter vertex metrics disagree with the mesh inventory"
        )
    if sum(item["triangleCount"] for item in inventory) != triangle_count:
        raise RuntimeError(
            "W3D adapter triangle metrics disagree with the mesh inventory"
        )
    if sum(1 for item in inventory if item["skinned"]) != skinned_mesh_count:
        raise RuntimeError("W3D adapter skin metrics disagree with the mesh inventory")

    equipment: dict[str, dict[str, Any]] = {}
    adapter_equipment: dict[str, dict[str, Any]] = {}
    for role, attachment in (
        ("right-hand-weapon", "right-hand"),
        ("left-hand-shield", "left-hand"),
    ):
        members = [item for item in inventory if item["semanticRole"] == role]
        if role in expected_required and not members:
            raise RuntimeError(f"W3D adapter did not prove required equipment: {role}")
        if not members:
            continue
        summary = {
            "attachment": attachment,
            "meshIndices": [item["index"] for item in members],
            "meshCount": len(members),
            "proofMethods": sorted(
                {method for item in members for method in item["proofMethods"]}
            ),
        }
        equipment[role] = summary
        adapter_equipment[role] = {
            "attachment": attachment,
            "mesh_indices": summary["meshIndices"],
            "mesh_count": summary["meshCount"],
            "proof_methods": summary["proofMethods"],
        }
    if report.get("equipment") != adapter_equipment:
        raise RuntimeError(
            "W3D adapter equipment summary disagrees with its mesh inventory"
        )
    if asset_kind == "static" and equipment:
        raise RuntimeError(
            "static W3D adapter report contains skeletal equipment semantics"
        )
    if root_rigid_applied and equipment:
        raise RuntimeError(
            "root-rigid W3D adapter report contains skeletal equipment semantics"
        )

    return {
        "schema": W3D_PRESENTATION_METADATA_CONTRACT,
        "schemaVersion": 0,
        "capabilities": {
            "animated": animated,
            "skeletal": skeletal,
            "embeddedModelAnimationImportedOnce": expected_embedded_model_animation,
            "splitActionAnimationsMergedAndValidated": bool(split_animation_count),
            "sourceActionShapesTypedAndExported": typed_action_report and animated,
            "sourceVisibilityChannelsPreservedInGlbExtras": bool(
                preserved_visibility_channel_count
            ),
            "provenRootRigidBake": root_rigid_applied,
            "nonRenderGeometryExcluded": True,
            "ambiguousBoxGeometryExcluded": True,
            "declaredOptionalRenderSubobjectsExcluded": bool(optional_mesh_exclusions),
            "requiredEquipmentProven": all(
                role in equipment for role in expected_required
            ),
            "equipmentAttachmentsCanonicalizedRestoredAndRevalidated": attachments_canonicalized_restored_and_revalidated,
            "sourceShaderBooleanSemanticsPreserved": shader_material_compatibility[
                "sourceFlagsPreserved"
            ],
            "sourceOpaqueBlendSemanticsPreserved": opaque_material_normalization[
                "sourceBlendStatePreserved"
            ],
        },
        "requiredEquipment": expected_required,
        "equipment": equipment,
        "excludedOptionalMeshes": optional_mesh_exclusions,
        "rootRigidBake": {
            "applied": root_rigid_applied,
            "removedCarrierCount": removed_root_carriers,
            "bakedMeshCount": root_rigid_baked_meshes,
            "worldTransformsPreserved": world_transforms_preserved,
            "deformAmbiguityAbsent": deform_ambiguity_absent,
        },
        "embeddedModelAnimation": {
            "importedOnce": expected_embedded_model_animation,
            **embedded_counts,
        },
        "animationActionShapes": action_shapes if typed_action_report else [],
        "splitActionAnimations": split_counts,
        "shaderMaterialCompatibility": shader_material_compatibility,
        "opaqueMaterialNormalization": opaque_material_normalization,
        "meshInventory": inventory,
        "metrics": {
            "meshCount": mesh_count,
            "animationCount": animation_count,
            "animationCurveCount": animation_curve_count,
            "animationKeyCount": animation_key_count,
            "actionShapeAnimationCount": action_shape_animation_count,
            "actionShapeActionCount": action_shape_action_count,
            "actionShapeNlaTrackCount": action_shape_nla_track_count,
            "actionShapeExportedAnimationCount": action_shape_exported_animation_count,
            "actionShapeExportedChannelCount": action_shape_exported_channel_count,
            "actionShapeExportedSamplerCount": action_shape_exported_sampler_count,
            "actionShapeExportedSkinCount": action_shape_exported_skin_count,
            "actionShapeExportedSkeletalMeshCount": action_shape_exported_skeletal_mesh_count,
            "duplicatedLogicalAnimationCount": duplicated_logical_animation_count,
            "preservedVisibilityChannelCount": preserved_visibility_channel_count,
            "preservedVisibilityKeyCount": preserved_visibility_key_count,
            "visibilityOnlySidecarAnimationCount": visibility_only_sidecar_animation_count,
            "embeddedModelActionCount": embedded_counts["actionCount"],
            "embeddedExportedAnimationCount": embedded_counts["exportedAnimationCount"],
            "embeddedExportedChannelCount": embedded_counts["exportedChannelCount"],
            "embeddedExportedSamplerCount": embedded_counts["exportedSamplerCount"],
            "embeddedExportedSkinCount": embedded_counts["exportedSkinCount"],
            "embeddedExportedSkeletalMeshCount": embedded_counts[
                "exportedSkeletalMeshCount"
            ],
            "splitActionAnimationCount": split_animation_count,
            "splitActionCount": split_counts["actionCount"],
            "splitExportedAnimationCount": split_counts["exportedAnimationCount"],
            "splitExportedChannelCount": split_counts["exportedChannelCount"],
            "splitExportedSamplerCount": split_counts["exportedSamplerCount"],
            "splitExportedSkinCount": split_counts["exportedSkinCount"],
            "splitExportedSkeletalMeshCount": split_counts["exportedSkeletalMeshCount"],
            "boneCount": bone_count,
            "skeletonCount": skeleton_count,
            "vertexCount": vertex_count,
            "triangleCount": triangle_count,
            "skinnedMeshCount": skinned_mesh_count,
            "materialCount": material_count,
            "shaderCompatibilityMappedMaterialCount": shader_material_compatibility[
                "mappedMaterialCount"
            ],
            "shaderCompatibilityMappedPropertyCount": shader_material_compatibility[
                "mappedPropertyCount"
            ],
            "shaderCompatibilityAlphaBlendingEnableCount": shader_material_compatibility[
                "alphaBlendingEnableCount"
            ],
            "shaderCompatibilityFogEnableCount": shader_material_compatibility[
                "fogEnableCount"
            ],
            "opaqueMaterialNormalizedCount": opaque_material_normalization[
                "normalizedMaterialCount"
            ],
            "opaqueMaterialRemovedAlphaLinkCount": opaque_material_normalization[
                "removedAlphaLinkCount"
            ],
            "imageCount": image_count,
            "generatedImageCount": generated_image_count,
            "filteredNonRenderGeometryCount": filtered_count,
            "excludedOptionalMeshCount": len(optional_mesh_exclusions),
            "remainingNonRenderGeometryCount": remaining_non_render,
            "remainingAmbiguousBoxGeometryCount": remaining_ambiguous_boxes,
            "framesPerSecond": fps,
        },
    }


def _w3d_report_relative_path(asset_id: str) -> str:
    """Return the payload-free report location for a validated profile resource."""

    if not SLUG_PATTERN.fullmatch(asset_id):
        raise ValueError(f"W3D asset id must be a bounded lowercase slug: {asset_id!r}")
    return f"provenance/conversion/{asset_id}.json"


def _isolated_blender_environment(
    base_environment: Mapping[str, str],
    job_root: Path,
) -> dict[str, str]:
    """Allow Blender's embedded Python env without inheriting user Python hooks."""

    environment = {
        name: value
        for name, value in base_environment.items()
        if not name.upper().startswith("PYTHON")
        and not name.upper().startswith("BLENDER_")
    }
    blender_user = job_root / "blender-user"
    for name in ["config", "scripts", "datafiles", "temp"]:
        (blender_user / name).mkdir(parents=True, exist_ok=True)
    environment.update(
        {
            "BLENDER_USER_CONFIG": str(blender_user / "config"),
            "BLENDER_USER_SCRIPTS": str(blender_user / "scripts"),
            "BLENDER_USER_DATAFILES": str(blender_user / "datafiles"),
            "TEMP": str(blender_user / "temp"),
            "TMP": str(blender_user / "temp"),
            "PYTHONDONTWRITEBYTECODE": "1",
            "PYTHONNOUSERSITE": "1",
        }
    )
    return environment


def _w3d_conversion_cache_key(
    *,
    source_hashes: Mapping[str, str],
    adapter_sha256: str,
    plugin_attestation_sha256: str,
    blender_tree_sha256: str,
    argument_vector: list[str] | None = None,
    logical: Mapping[str, Any] | None = None,
) -> str:
    """Hash every byte-affecting W3D conversion input canonically.

    Prefer *logical* identity (asset kind, model name, options) so absolute
    output paths do not partition the shared DDC across factions/profiles.
    """

    if logical is not None:
        identity: dict[str, Any] = {
            "adapter_sha256": adapter_sha256,
            "blender_tree_sha256": blender_tree_sha256,
            "logical": dict(logical),
            "plugin_attestation_sha256": plugin_attestation_sha256,
            "source_hashes": {
                name: source_hashes[name]
                for name in sorted(
                    source_hashes, key=lambda value: (value.casefold(), value)
                )
            },
        }
    else:
        identity = {
            "adapter_sha256": adapter_sha256,
            "argument_vector": list(argument_vector or ()),
            "blender_tree_sha256": blender_tree_sha256,
            "plugin_attestation_sha256": plugin_attestation_sha256,
            "source_hashes": {
                name: source_hashes[name]
                for name in sorted(
                    source_hashes, key=lambda value: (value.casefold(), value)
                )
            },
        }
    return hashlib.sha256(_canonical_json_bytes(identity)).hexdigest()


def _w3d_plugin_attestation_sha256(attestation: Mapping[str, str]) -> str:
    return hashlib.sha256(_canonical_json_bytes(dict(attestation))).hexdigest()


# The multi-job adapter captures each job's real process output into per-job
# files and rides it on the success marker as ``output_log`` (bounded at the
# adapter by MAX_JOB_OUTPUT_CAPTURE_BYTES). The warning-text guards in
# _finalize_w3d_bundle_job must evaluate that real content; an unbounded or
# missing log fails the job closed so it can never reach the conversion cache.
_W3D_MULTI_JOB_MAX_OUTPUT_LOG_CHARS = 2 * 1024 * 1024


def _entry_cache_key(entry: CatalogEntry) -> str:
    value = f"{entry.archive.casefold()}\n{entry.name.casefold()}\n{entry.offset}\n{entry.size}"
    return hashlib.sha256(value.encode("utf-8")).hexdigest()[:20]


def _source_cache_key(entry: CatalogEntry, source_sha256: str) -> str:
    value = f"{entry.archive.casefold()}\n{entry.name.casefold()}\n{source_sha256}"
    return hashlib.sha256(value.encode("utf-8")).hexdigest()[:20]


def _media_conversion_cache_key(
    *,
    source_sha256: str,
    converter: str,
    options: Mapping[str, Any],
    tool_token: str,
    relative_output: str = "",
) -> str:
    """Content-addressed key for audio/texture outputs (shared across factions).

    Intentionally ignores pack-relative output path so the same source cooks
    once for every faction/profile that needs it.
    """

    suffix = Path(relative_output).suffix.casefold() if relative_output else ""
    payload = {
        "converter": converter,
        "options": dict(options),
        "output_suffix": suffix,
        "source_sha256": source_sha256.casefold(),
        "tool_token": tool_token,
    }
    return hashlib.sha256(_canonical_json_bytes(payload)).hexdigest()


def _w3d_staging_sources(
    resource: ResolvedResource,
    resources: tuple[ResolvedResource, ...],
    extracted: Mapping[tuple[str, str], Mapping[str, Any]],
) -> list[Path]:
    """Select the raw source closure for one W3D conversion job.

    Profiles predating explicit input declarations retain their all-profile
    closure. Once inputResourceIds is present, the boundary is the current
    resource plus exactly those declared resources.
    """

    by_id = {item.rule.id: item for item in resources}
    if W3D_INPUT_RESOURCE_IDS_OPTION in resource.rule.options:
        dependency_ids = resource.rule.options[W3D_INPUT_RESOURCE_IDS_OPTION]
        selected = [resource]
        for dependency_id in dependency_ids:
            dependency = by_id.get(dependency_id)
            if dependency is None:
                raise RuntimeError(
                    f"W3D input resource was not resolved: {dependency_id}"
                )
            selected.append(dependency)
    else:
        selected = list(resources)

    unique: dict[tuple[str, str], Path] = {}
    for selected_resource in selected:
        for entry in selected_resource.entries:
            key = (entry.archive.casefold(), entry.name.casefold())
            cached = extracted.get(key)
            if cached is None:
                raise RuntimeError(
                    f"W3D input resource was not extracted: {entry.name}"
                )
            unique[key] = Path(cached["source_path"])
    return sorted(unique.values(), key=lambda item: str(item).casefold())


def _stage_w3d_sources(
    sources: list[Path],
    input_root: Path,
    digests_out: dict[str, str] | None = None,
) -> dict[str, Path]:
    """Flatten a proven W3D input closure and reject ambiguous basenames.

    *digests_out*, when given, receives ``key -> sha256`` of every staged file
    AS STAGED. The copy already reads every byte, so hashing in the same pass
    is free; the caller can then skip a second full read of the job root when
    nothing rewrote it. The collision check gets the same treatment: it used to
    read both files end to end on every duplicate basename, and a W3D closure
    repeats shared animation clips constantly.
    """

    input_root.mkdir(parents=True)
    copied: dict[str, Path] = {}
    digests: dict[str, str] = {}
    for source in sorted(sources, key=lambda item: str(item).casefold()):
        key = source.name.casefold()
        if key in copied:
            if sha256_file(source) != digests[key]:
                raise RuntimeError(f"flat W3D staging collision: {source.name}")
            continue
        target = input_root / source.name.casefold()
        _size, digest = _copy_file_with_digest(source, target)
        copied[key] = target
        digests[key] = digest
    if digests_out is not None:
        digests_out.update(digests)
    return copied


def _prepare_w3d_secondary_skin_streams(
    copied: Mapping[str, Path], model: Path
) -> dict[str, Any] | None:
    """Prove and remove redundant dual-bone-local streams in one job root."""

    model_bytes = model.read_bytes()
    metadata = scan_w3d_metadata(model_bytes, model.name)
    secondary_chunks = [
        chunk.chunk_id
        for chunk in metadata.chunks
        if chunk.chunk_id in W3D_SECONDARY_SKIN_CHUNKS
    ]
    if not secondary_chunks:
        return None

    before_hashes = {
        basename: sha256_file(path) for basename, path in sorted(copied.items())
    }
    candidates: list[tuple[str, Any]] = []
    rejected: list[str] = []
    for basename, path in sorted(copied.items()):
        if path.suffix.casefold() != ".w3d":
            continue
        candidate_bytes = path.read_bytes()
        candidate_metadata = scan_w3d_metadata(candidate_bytes, path.name)
        if not candidate_metadata.hierarchy_ids:
            continue
        try:
            result = strip_proven_redundant_secondary_skin_streams(
                model_bytes,
                candidate_bytes,
            )
        except W3DSecondarySkinError as exc:
            rejected.append(f"{basename}: {exc}")
            continue
        candidates.append((basename, result))

    if len(candidates) > 1:
        detail = "; ".join(rejected[:8])
        if len(rejected) > 8:
            detail += f"; plus {len(rejected) - 8} more rejected candidates"
        raise RuntimeError(
            "W3D secondary-skin proof requires exactly one compatible hierarchy; "
            f"found {len(candidates)}" + (f" ({detail})" if detail else "")
        )

    if not candidates:
        # A bind-space coincidence rejection means redundancy could not be
        # proven for the staged hierarchy.  The pinned importer provably skips
        # secondary chunks at read time (it seeks past VERTICES_2/NORMALS_2
        # unconditionally), so the conversion output is identical to the
        # stripped outcome.  Retain the streams and record every rejected
        # candidate as exact evidence instead of inventing an equivalence
        # proof that does not exist.  Structural and identity failures (wrong
        # hierarchy, malformed chunks, ambiguous providers) still fail
        # closed, and when nothing was evaluated at all the job keeps failing.
        unproven = [
            reason
            for reason in rejected
            if "bind position delta" in reason or "bind normal delta" in reason
        ]
        if len(unproven) != len(rejected) or not rejected:
            detail = "; ".join(rejected[:8])
            if len(rejected) > 8:
                detail += f"; plus {len(rejected) - 8} more rejected candidates"
            raise RuntimeError(
                "W3D secondary-skin proof requires exactly one compatible hierarchy; "
                f"found 0 ({detail})"
            )
        after_hashes = {
            basename: sha256_file(path) for basename, path in sorted(copied.items())
        }
        if after_hashes != before_hashes:
            raise RuntimeError(
                "W3D secondary-skin retention changed staged files"
            )
        return {
            "schema": "openbfme.w3d-secondary-skin-retention",
            "schemaVersion": 0,
            "retained": True,
            "transformedMeshCount": 0,
            "removedByteCount": 0,
            "rejectedCandidates": rejected[:8],
            "rejectedCandidateCount": len(rejected),
            "stagedClosureBeforeSha256": _canonical_value_sha256(before_hashes),
            "stagedClosureAfterSha256": _canonical_value_sha256(after_hashes),
        }

    hierarchy_basename, result = candidates[0]
    transformed = result.model_bytes()
    temporary = model.with_name(f".{model.name}.secondary-skin.tmp")
    if temporary.exists() or _is_link_like(temporary):
        raise RuntimeError("W3D secondary-skin temporary path already exists")
    try:
        temporary.write_bytes(transformed)
        if sha256_file(temporary) != result.proof.output_model_sha256:
            raise RuntimeError("W3D secondary-skin staged output changed bytes")
        os.replace(temporary, model)
    finally:
        temporary.unlink(missing_ok=True)
    if sha256_file(model) != result.proof.output_model_sha256:
        raise RuntimeError("W3D secondary-skin model replacement changed bytes")

    after_hashes = {
        basename: sha256_file(path) for basename, path in sorted(copied.items())
    }
    changed = {
        basename
        for basename in before_hashes
        if before_hashes[basename] != after_hashes[basename]
    }
    if changed != {model.name.casefold()}:
        raise RuntimeError(
            "W3D secondary-skin preparation changed files outside the model"
        )
    return {
        **result.proof.neutral(),
        "hierarchyInputBasename": hierarchy_basename,
        "stagedClosureBeforeSha256": _canonical_value_sha256(before_hashes),
        "stagedClosureAfterSha256": _canonical_value_sha256(after_hashes),
    }


def _prepare_w3d_no_motion_animations(
    copied: Mapping[str, Path],
    model: Path,
    declarations: Any,
) -> dict[str, Any] | None:
    """Prove and remove exact header-only animation containers from one model."""

    if declarations is None:
        return None
    normalized = normalize_w3d_no_motion_animations(declarations)
    from .w3d_no_motion import (
        W3DNoMotionExpectation,
        strip_proven_header_only_animations,
    )

    expectations = tuple(
        W3DNoMotionExpectation(
            identifier=item["identifier"],
            hierarchy_identifier=item["hierarchyIdentifier"],
            frame_count=item["frameCount"],
            frame_rate=item["frameRate"],
            compressed=item["compressed"],
            model_identifier=item["modelIdentifier"],
            flavor=item.get("flavor"),
        )
        for item in normalized
    )
    before_hashes = {
        basename: sha256_file(path) for basename, path in sorted(copied.items())
    }
    result = strip_proven_header_only_animations(
        model.read_bytes(),
        virtual_path=model.name,
        expectations=expectations,
    )
    temporary = model.with_name(f".{model.name}.no-motion.tmp")
    if temporary.exists() or _is_link_like(temporary):
        raise RuntimeError("W3D no-motion temporary path already exists")
    try:
        temporary.write_bytes(result.output_bytes())
        if sha256_file(temporary) != result.proof.output_sha256:
            raise RuntimeError("W3D no-motion staged output changed bytes")
        os.replace(temporary, model)
    finally:
        temporary.unlink(missing_ok=True)
    if sha256_file(model) != result.proof.output_sha256:
        raise RuntimeError("W3D no-motion model replacement changed bytes")

    after_hashes = {
        basename: sha256_file(path) for basename, path in sorted(copied.items())
    }
    changed = {
        basename
        for basename in before_hashes
        if before_hashes[basename] != after_hashes[basename]
    }
    if changed != {model.name.casefold()}:
        raise RuntimeError("W3D no-motion preparation changed files outside the model")
    return {
        **result.proof.neutral(),
        "stagedClosureBeforeSha256": _canonical_value_sha256(before_hashes),
        "stagedClosureAfterSha256": _canonical_value_sha256(after_hashes),
    }


def _w3d_texture_references(model: Path) -> list[str]:
    """Read authored texture chunks and shader properties from a valid W3D."""

    payload = model.read_bytes()
    references: list[str] = []

    def read_declared_string(offset: int, end: int) -> tuple[str, int]:
        if end - offset < 4:
            raise RuntimeError("W3D shader string length is truncated")
        declared_length = struct.unpack_from("<I", payload, offset)[0]
        offset += 4
        value_end = offset + declared_length
        if declared_length < 1 or value_end > end:
            raise RuntimeError("W3D shader string escapes its property")
        encoded = payload[offset:value_end]
        if not encoded.endswith(b"\0") or b"\0" in encoded[:-1]:
            raise RuntimeError("W3D shader string is not one C string")
        try:
            value = encoded[:-1].decode("utf-8")
        except UnicodeDecodeError as exc:
            raise RuntimeError("W3D shader string is not UTF-8") from exc
        return value, value_end

    def visit(start: int, end: int, depth: int) -> None:
        if depth > 64:
            raise RuntimeError("W3D texture-reference chunk nesting is too deep")
        offset = start
        while offset < end:
            if end - offset < 8:
                raise RuntimeError("W3D texture-reference chunk header is truncated")
            chunk_type, raw_size = struct.unpack_from("<II", payload, offset)
            size = raw_size & 0x7FFFFFFF
            content_start = offset + 8
            content_end = content_start + size
            if content_end < content_start or content_end > end:
                raise RuntimeError("W3D texture-reference chunk escapes its parent")
            has_children = bool(raw_size & W3D_CHUNK_HAS_CHILDREN)
            if chunk_type == W3D_TEXTURE_NAME_CHUNK:
                if has_children:
                    raise RuntimeError("W3D texture-name chunk cannot contain children")
                encoded = payload[content_start:content_end]
                if not encoded.endswith(b"\0") or b"\0" in encoded[:-1]:
                    raise RuntimeError("W3D texture-name chunk is not one C string")
                try:
                    reference = encoded[:-1].decode("utf-8")
                except UnicodeDecodeError as exc:
                    raise RuntimeError("W3D texture-name chunk is not UTF-8") from exc
                if not reference:
                    raise RuntimeError("W3D texture-name chunk is empty")
                references.append(reference)
            elif chunk_type == W3D_SHADER_MATERIAL_PROPERTY_CHUNK:
                if has_children or content_end - content_start < 8:
                    raise RuntimeError("W3D shader material property is malformed")
                property_type = struct.unpack_from("<I", payload, content_start)[0]
                property_name, property_offset = read_declared_string(
                    content_start + 4, content_end
                )
                if not property_name:
                    raise RuntimeError("W3D shader material property name is empty")
                if property_type == W3D_SHADER_STRING_PROPERTY:
                    value, property_offset = read_declared_string(
                        property_offset, content_end
                    )
                    if property_offset != content_end:
                        raise RuntimeError(
                            "W3D shader string property has trailing payload"
                        )
                    basename = PurePosixPath(value.replace("\\", "/")).name
                    if Path(basename).suffix.casefold() in W3D_TEXTURE_SUFFIXES:
                        references.append(value)
            elif has_children:
                visit(content_start, content_end, depth + 1)
            offset = content_end
        if offset != end:
            raise RuntimeError("W3D texture-reference chunks do not close exactly")

    visit(0, len(payload), 0)
    return references


def _apply_w3d_texture_overrides(
    copied: Mapping[str, Path],
    model: Path,
    raw_overrides: Any,
) -> dict[str, Any] | None:
    """Apply declared aliases only to flattened files in one private job root."""

    if raw_overrides is None:
        return None
    overrides = normalize_w3d_texture_overrides(raw_overrides)
    model_root = model.resolve().parent
    references = _w3d_texture_references(model)
    before_hashes: dict[str, str] = {}
    for basename, path in sorted(copied.items()):
        resolved = path.resolve()
        if (
            resolved.parent != model_root
            or not resolved.is_file()
            or _is_link_like(path)
        ):
            raise RuntimeError("W3D override input is not an ordinary job-local file")
        before_hashes[basename] = sha256_file(resolved)

    prepared: list[dict[str, Any]] = []
    for override in overrides:
        target = copied.get(override["target"])
        source = copied.get(override["source"])
        if target is None or source is None:
            raise RuntimeError(
                "W3D texture override target and source must both be selected inputs"
            )
        if target.resolve() == source.resolve():
            raise RuntimeError(
                "W3D texture override target and source are not distinct"
            )

        target_stem = Path(override["target"]).stem
        same_stem_references = []
        for reference in references:
            basename = PurePosixPath(reference.replace("\\", "/")).name
            if Path(basename).stem.casefold() == target_stem:
                same_stem_references.append(reference)
        matching_references = [
            reference
            for reference in same_stem_references
            if reference.casefold() == override["authored"]
        ]
        if not matching_references or len(matching_references) != len(
            same_stem_references
        ):
            raise RuntimeError(
                "W3D model does not have only the exact authored texture reference "
                "for the override target"
            )

        original_target_sha256 = before_hashes[override["target"]]
        source_sha256 = before_hashes[override["source"]]
        if original_target_sha256 == source_sha256:
            raise RuntimeError(
                "W3D texture override source does not change target bytes"
            )
        prepared.append(
            {
                **override,
                "targetPath": target,
                "sourcePath": source,
                "referenceCount": len(matching_references),
                "originalTargetSha256": original_target_sha256,
                "sourceSha256": source_sha256,
            }
        )

    for index, item in enumerate(prepared):
        target = item["targetPath"]
        temporary = target.with_name(f".{target.name}.override-{index}.tmp")
        if temporary.exists() or _is_link_like(temporary):
            raise RuntimeError("W3D texture override temporary path already exists")
        shutil.copyfile(item["sourcePath"], temporary)
        if sha256_file(temporary) != item["sourceSha256"]:
            temporary.unlink(missing_ok=True)
            raise RuntimeError("W3D texture override temporary copy changed bytes")
        os.replace(temporary, target)
        if sha256_file(target) != item["sourceSha256"]:
            raise RuntimeError("W3D texture override staged target changed bytes")

    after_hashes = {
        basename: sha256_file(path) for basename, path in sorted(copied.items())
    }
    changed = {
        basename
        for basename in before_hashes
        if before_hashes[basename] != after_hashes[basename]
    }
    expected_changed = {item["target"] for item in prepared}
    if changed != expected_changed:
        raise RuntimeError("W3D texture override changed files outside its targets")

    entries = [
        {
            "authored": item["authored"],
            "target": item["target"],
            "source": item["source"],
            "authoredReferenceCount": item["referenceCount"],
            "originalTargetSha256": item["originalTargetSha256"],
            "sourceSha256": item["sourceSha256"],
            "stagedTargetSha256": after_hashes[item["target"]],
        }
        for item in prepared
    ]
    return {
        "schema": "openbfme.w3d-texture-overrides",
        "schemaVersion": 0,
        "modelSha256": sha256_file(model),
        "modelTextureReferenceSetSha256": _canonical_value_sha256(
            sorted(references, key=lambda value: (value.casefold(), value))
        ),
        "stagedInputCount": len(copied),
        "stagedClosureBeforeSha256": _canonical_value_sha256(before_hashes),
        "stagedClosureAfterSha256": _canonical_value_sha256(after_hashes),
        "entries": entries,
    }


def _load_glb_document(path: Path) -> tuple[dict[str, Any], bytes]:
    payload = path.read_bytes()
    if len(payload) < 20 or payload[:4] != b"glTF":
        raise RuntimeError("W3D texture override output is not a GLB")
    version, declared_length = struct.unpack_from("<II", payload, 4)
    if version != 2 or declared_length != len(payload):
        raise RuntimeError("W3D texture override GLB header is invalid")
    chunks: dict[int, bytes] = {}
    offset = 12
    while offset < len(payload):
        if len(payload) - offset < 8:
            raise RuntimeError("W3D texture override GLB chunk header is truncated")
        chunk_length, chunk_type = struct.unpack_from("<II", payload, offset)
        offset += 8
        end = offset + chunk_length
        if end > len(payload) or chunk_type in chunks:
            raise RuntimeError("W3D texture override GLB chunks are invalid")
        chunks[chunk_type] = payload[offset:end]
        offset = end
    if offset != len(payload) or set(chunks) != {GLB_JSON_CHUNK, GLB_BINARY_CHUNK}:
        raise RuntimeError("W3D texture override GLB chunk set is unsupported")
    try:
        document = json.loads(chunks[GLB_JSON_CHUNK].decode("utf-8").rstrip(" \x00"))
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise RuntimeError("W3D texture override GLB JSON is invalid") from exc
    if not isinstance(document, dict):
        raise RuntimeError("W3D texture override GLB JSON root is not an object")
    return document, chunks[GLB_BINARY_CHUNK]


def _glb_buffer_view_payload(
    document: Mapping[str, Any], binary: bytes, index: Any
) -> bytes:
    views = document.get("bufferViews")
    if (
        isinstance(index, bool)
        or not isinstance(index, int)
        or not isinstance(views, list)
        or not 0 <= index < len(views)
        or not isinstance(views[index], Mapping)
    ):
        raise RuntimeError("W3D texture override GLB image buffer view is invalid")
    view = views[index]
    if view.get("buffer", 0) != 0:
        raise RuntimeError("W3D texture override GLB image uses an external buffer")
    start = view.get("byteOffset", 0)
    length = view.get("byteLength")
    if (
        isinstance(start, bool)
        or not isinstance(start, int)
        or isinstance(length, bool)
        or not isinstance(length, int)
        or start < 0
        or length < 1
        or start + length > len(binary)
    ):
        raise RuntimeError("W3D texture override GLB image range is invalid")
    return binary[start : start + length]


def _validate_w3d_texture_override_glb(
    glb: Path,
    copied: Mapping[str, Path],
    proof: dict[str, Any] | None,
) -> dict[str, Any] | None:
    """Bind each exact staged alias to one embedded, consumed base-color image."""

    if proof is None:
        return None
    try:
        from PIL import Image
    except ImportError as exc:  # pragma: no cover - bootstrap enforces Pillow
        raise RuntimeError("Pillow is required for W3D texture override proof") from exc

    document, binary = _load_glb_document(glb)
    images = document.get("images")
    textures = document.get("textures")
    materials = document.get("materials")
    if not all(isinstance(value, list) for value in (images, textures, materials)):
        raise RuntimeError("W3D texture override GLB material tables are invalid")

    validated_entries = []
    for entry in proof["entries"]:
        image_name = Path(entry["authored"]).stem
        matching_images = [
            (index, image)
            for index, image in enumerate(images)
            if isinstance(image, Mapping)
            and str(image.get("name", "")).casefold() == image_name
        ]
        if len(matching_images) != 1:
            raise RuntimeError(
                "W3D texture override GLB does not contain one exact authored image"
            )
        image_index, image_record = matching_images[0]
        if image_record.get("mimeType") != "image/png" or "uri" in image_record:
            raise RuntimeError(
                "W3D texture override GLB authored image is not embedded PNG"
            )
        embedded = _glb_buffer_view_payload(
            document, binary, image_record.get("bufferView")
        )

        texture_indices = [
            index
            for index, texture in enumerate(textures)
            if isinstance(texture, Mapping)
            and type(texture.get("source")) is int
            and texture.get("source") == image_index
        ]
        material_indices = []
        for index, material in enumerate(materials):
            if not isinstance(material, Mapping):
                raise RuntimeError("W3D texture override GLB material is invalid")
            pbr = material.get("pbrMetallicRoughness", {})
            if not isinstance(pbr, Mapping):
                raise RuntimeError("W3D texture override GLB PBR material is invalid")
            base_color = pbr.get("baseColorTexture", {})
            if base_color and not isinstance(base_color, Mapping):
                raise RuntimeError(
                    "W3D texture override GLB base-color binding is invalid"
                )
            if (
                isinstance(base_color, Mapping)
                and type(base_color.get("index")) is int
                and base_color.get("index") in texture_indices
            ):
                material_indices.append(index)
        if not texture_indices or not material_indices:
            raise RuntimeError(
                "W3D texture override GLB image has no base-color consumer"
            )

        source_path = copied[entry["source"]]
        try:
            with Image.open(source_path) as source_image:
                source_image.load()
                source_rgba = source_image.convert("RGBA")
            with Image.open(BytesIO(embedded)) as embedded_image:
                embedded_image.load()
                embedded_rgba = embedded_image.convert("RGBA")
        except Exception as exc:
            raise RuntimeError(
                "W3D texture override image proof could not decode its inputs"
            ) from exc
        if source_rgba.size != embedded_rgba.size:
            raise RuntimeError("W3D texture override embedded image dimensions differ")

        source_pixels = source_rgba.tobytes()
        embedded_pixels = embedded_rgba.tobytes()
        alpha_exact = True
        max_rgb_delta = 0
        for offset in range(0, len(source_pixels), 4):
            alpha_exact = (
                alpha_exact and source_pixels[offset + 3] == embedded_pixels[offset + 3]
            )
            for channel in range(3):
                max_rgb_delta = max(
                    max_rgb_delta,
                    abs(
                        source_pixels[offset + channel]
                        - embedded_pixels[offset + channel]
                    ),
                )
        allowed_delta = (
            W3D_DDS_DECODE_MAX_RGB_DELTA
            if Path(entry["source"]).suffix == ".dds"
            else 0
        )
        if not alpha_exact or max_rgb_delta > allowed_delta:
            raise RuntimeError(
                "W3D texture override embedded image does not match its exact source"
            )

        validated_entries.append(
            {
                **entry,
                "embeddedImageName": image_record.get("name"),
                "embeddedImageIndex": image_index,
                "embeddedImageEncodedSha256": hashlib.sha256(embedded).hexdigest(),
                "sourceDecodedRgbaSha256": hashlib.sha256(source_pixels).hexdigest(),
                "embeddedDecodedRgbaSha256": hashlib.sha256(
                    embedded_pixels
                ).hexdigest(),
                "decodedRgbaExact": source_pixels == embedded_pixels,
                "decodedAlphaExact": alpha_exact,
                "maxRgbChannelDelta": max_rgb_delta,
                "allowedRgbChannelDelta": allowed_delta,
                "width": source_rgba.width,
                "height": source_rgba.height,
                "baseColorTextureIndices": texture_indices,
                "baseColorMaterialIndices": material_indices,
            }
        )

    return {**proof, "entries": validated_entries, "complete": True}


def _strip_windows_extended_prefix(value: Path) -> Path:
    """Normalize the \\\\?\\ and \\\\?\\UNC\\ forms Path.resolve may return."""
    text = str(value)
    if text.startswith("\\\\?\\UNC\\"):
        return Path("\\\\" + text[len("\\\\?\\UNC\\") :])
    if text.startswith("\\\\?\\"):
        return Path(text[len("\\\\?\\") :])
    return value


def _safe_output(root: Path, relative: str, *, root_is_resolved: bool = False) -> Path:
    """Join *relative* under *root*, refusing anything that escapes the root.

    *root_is_resolved* lets callers that already hold a resolved root (and
    call this once per pack file) skip re-resolving it every iteration; that
    resolve is a syscall and measured ~0.25 ms, i.e. ~4 s per 16k-file pack.
    """

    parts = safe_relative_parts(relative)
    resolved_root = root if root_is_resolved else root.resolve()
    target = (resolved_root / Path(*parts)).resolve()
    try:
        # Windows resolves a not-yet-created target through a different
        # syscall path than the existing root, which can race concurrent
        # directory creation and come back with a \\?\ extended prefix. The
        # containment check must compare the same spelling on both sides.
        contained = _strip_windows_extended_prefix(target).relative_to(
            _strip_windows_extended_prefix(resolved_root)
        )
    except ValueError as exc:
        raise ValueError(f"output path escaped pack root: {relative!r}") from exc
    return resolved_root / contained


def _render_output_template(template: str, *, index: int, stem: str, name: str) -> str:
    """Expand only the three documented tokens with a post-expansion bound."""

    rendered = template
    for token, value in (
        ("{index}", str(index)),
        ("{stem}", stem),
        ("{name}", name),
    ):
        rendered = rendered.replace(token, value)
    if "{" in rendered or "}" in rendered:
        raise ValueError("output path contains an unsupported format expression")
    if len(rendered) > MAX_RENDERED_OUTPUT_PATH:
        raise ValueError("rendered output path exceeds the safety limit")
    if any(character in rendered for character in '<>"|?*'):
        raise ValueError("rendered output path contains a Windows-unsafe character")
    return rendered


def _is_link_like(path: Path) -> bool:
    is_junction = getattr(path, "is_junction", None)
    return path.is_symlink() or bool(is_junction and is_junction())


def _canonical_json_bytes(value: Any) -> bytes:
    return (
        json.dumps(value, indent=2, sort_keys=True, ensure_ascii=False) + "\n"
    ).encode("utf-8")


def _canonical_value_sha256(value: Any) -> str:
    return hashlib.sha256(
        json.dumps(
            value,
            sort_keys=True,
            ensure_ascii=False,
            separators=(",", ":"),
        ).encode("utf-8")
    ).hexdigest()


def _effective_catalog_entries(catalog: InstallCatalog) -> tuple[CatalogEntry, ...]:
    """Return the catalog's case-insensitive winning virtual-path view."""

    representatives: dict[str, str] = {}
    for entry in catalog.entries:
        representatives.setdefault(entry.key, entry.name)
    winners: list[CatalogEntry] = []
    for key in sorted(representatives):
        winner = catalog.resolve_exact(representatives[key])
        if winner is None or winner.key != key:
            raise RuntimeError(f"catalog cannot resolve its effective entry: {key}")
        winners.append(winner)
    return tuple(winners)


def _catalog_identities(catalog: InstallCatalog) -> tuple[str, str]:
    archives = [
        asdict(item)
        for item in sorted(
            catalog.archives,
            key=lambda item: (item.relative_path.casefold(), item.relative_path),
        )
    ]
    install_identity = _canonical_value_sha256(
        {
            "archives": [
                {
                    "directory_sha256": item["directory_sha256"],
                    "entry_count": item["entry_count"],
                    "header_size": item["header_size"],
                    "magic": item["magic"],
                    "relative_path": item["relative_path"],
                    "size": item["size"],
                }
                for item in archives
            ]
        }
    )
    return catalog.identity_sha256(), install_identity


def _validate_effective_catalog(
    catalog: InstallCatalog,
    *,
    max_files: int,
    max_bytes: int,
) -> tuple[CatalogEntry, ...]:
    """Reject unsafe, stale, ambiguous, or over-limit full-catalog state."""

    if isinstance(max_files, bool) or not isinstance(max_files, int) or max_files < 1:
        raise ValueError("max_files must be a positive integer")
    if isinstance(max_bytes, bool) or not isinstance(max_bytes, int) or max_bytes < 1:
        raise ValueError("max_bytes must be a positive integer")

    archives: dict[str, Any] = {}
    for archive in catalog.archives:
        parts = safe_relative_parts(archive.relative_path)
        normalized = "/".join(parts)
        if normalized != archive.relative_path.replace("\\", "/"):
            raise ValueError(
                f"catalog archive path is not canonical: {archive.relative_path!r}"
            )
        key = normalized.casefold()
        if key in archives:
            raise ValueError(f"catalog has a case-colliding archive: {normalized}")
        if (
            isinstance(archive.size, bool)
            or not isinstance(archive.size, int)
            or archive.size < 16
            or isinstance(archive.header_size, bool)
            or not isinstance(archive.header_size, int)
            or archive.header_size < 16
            or isinstance(archive.entry_count, bool)
            or not isinstance(archive.entry_count, int)
            or archive.entry_count < 0
            or not isinstance(archive.directory_sha256, str)
            or len(archive.directory_sha256) != 64
            or any(
                character not in "0123456789abcdef"
                for character in archive.directory_sha256.casefold()
            )
        ):
            raise ValueError(f"catalog archive metadata is invalid: {normalized}")
        archives[key] = archive

    for entry in catalog.entries:
        archive_parts = safe_relative_parts(entry.archive)
        archive_name = "/".join(archive_parts)
        name_parts = safe_relative_parts(entry.name)
        virtual_path = "/".join(name_parts)
        if archive_name != entry.archive.replace("\\", "/"):
            raise ValueError(
                f"catalog entry archive path is not canonical: {entry.archive!r}"
            )
        if virtual_path != entry.name.replace("\\", "/"):
            raise ValueError(f"catalog virtual path is not canonical: {entry.name!r}")
        archive = archives.get(archive_name.casefold())
        if archive is None:
            raise ValueError(
                f"catalog entry refers to an unknown archive: {entry.archive}"
            )
        if (
            isinstance(entry.offset, bool)
            or not isinstance(entry.offset, int)
            or entry.offset < archive.header_size
            or isinstance(entry.size, bool)
            or not isinstance(entry.size, int)
            or entry.size < 0
            or entry.offset + entry.size > archive.size
            or isinstance(entry.precedence, bool)
            or not isinstance(entry.precedence, int)
            or entry.precedence < 0
        ):
            raise ValueError(
                f"catalog entry metadata is invalid: {entry.archive}:{entry.name}"
            )

    winners = _effective_catalog_entries(catalog)
    if len(winners) > max_files:
        raise RuntimeError(
            f"effective catalog selects {len(winners)} files; limit is {max_files}"
        )
    total_bytes = sum(entry.size for entry in winners)
    if total_bytes > max_bytes:
        raise RuntimeError(
            f"effective catalog selects {total_bytes} bytes; limit is {max_bytes}"
        )

    keys = [entry.key for entry in winners]
    reserved = EFFECTIVE_ASSET_METADATA_DIRECTORY.casefold()
    for key in keys:
        if key == reserved or key.startswith(reserved + "/"):
            raise RuntimeError(
                "effective catalog collides with the reserved extraction metadata "
                f"directory: {key}"
            )
    for previous, current in zip(keys, keys[1:]):
        if current.startswith(previous + "/"):
            raise RuntimeError(
                "effective catalog has a file/directory output collision: "
                f"{previous!r} and {current!r}"
            )

    stale = catalog.stale_reasons()
    if stale:
        raise RuntimeError("catalog is stale: " + "; ".join(stale))
    return winners


def _effective_asset_aggregate(files: list[dict[str, Any]]) -> str:
    digest = hashlib.sha256()
    for item in files:
        digest.update(item["path"].encode("utf-8"))
        digest.update(b"\0")
        digest.update(str(item["size"]).encode("ascii"))
        digest.update(b"\0")
        digest.update(item["sha256"].encode("ascii"))
        digest.update(b"\n")
    return digest.hexdigest()


def _effective_asset_manifest(
    catalog: InstallCatalog,
    entries: tuple[CatalogEntry, ...],
    hashes: Mapping[str, str],
) -> dict[str, Any]:
    catalog_identity, install_identity = _catalog_identities(catalog)
    files: list[dict[str, Any]] = []
    for entry in entries:
        sha256 = hashes.get(entry.key)
        if (
            not isinstance(sha256, str)
            or len(sha256) != 64
            or any(character not in "0123456789abcdef" for character in sha256)
        ):
            raise RuntimeError(f"missing extracted hash for {entry.name}")
        files.append(
            {
                "archive": entry.archive,
                "offset": entry.offset,
                "path": entry.name,
                "precedence": entry.precedence,
                "sha256": sha256,
                "size": entry.size,
            }
        )
    return {
        "schema": EFFECTIVE_ASSET_MANIFEST_SCHEMA,
        "schema_version": EFFECTIVE_ASSET_MANIFEST_VERSION,
        "catalog": {
            "archive_count": len(catalog.archives),
            "entry_count": len(catalog.entries),
            "format": InstallCatalog.FORMAT,
            "identity_sha256": catalog_identity,
        },
        "install": {
            "identity_sha256": install_identity,
            "root": str(catalog.install_root),
        },
        "totals": {
            "bytes": sum(entry.size for entry in entries),
            "files": len(entries),
        },
        "aggregate_sha256": _effective_asset_aggregate(files),
        "files": files,
    }


def _pack_hash_workers() -> int:
    """Thread count for pack-wide SHA-256 passes.

    Hashing 16k mixed-size files is I/O-latency bound long before it is CPU
    bound; measured on a 24-core box, 8 threads is the peak (1.66x over
    serial) and more threads regress. Override for constrained machines.
    """

    raw = os.environ.get("OPENBFME_HASH_WORKERS", "").strip()
    if raw:
        try:
            return max(1, int(raw))
        except ValueError:
            pass
    return max(1, min(8, os.cpu_count() or 1))


def _pack_files(root: Path) -> list[Path]:
    """List every regular file under *root*, sorted by pack-relative posix path.

    Uses os.scandir rather than Path.rglob: measured 0.11s vs 0.46s over a
    16,232-file pack. Ordering is identical to the previous rglob+sort.
    """

    found: list[Path] = []
    if not root.is_dir():
        return found
    stack = [root]
    while stack:
        current = stack.pop()
        with os.scandir(current) as entries:
            for entry in entries:
                path = Path(entry.path)
                if entry.is_dir(follow_symlinks=False):
                    stack.append(path)
                elif entry.is_file(follow_symlinks=False):
                    found.append(path)
                else:
                    # Symlink/junction/other: keep it visible so the callers'
                    # link guards can reject it instead of silently skipping.
                    found.append(path)
    found.sort(key=lambda item: item.relative_to(root).as_posix())
    return found


def _hash_files(paths: list[Path], *, workers: int | None = None) -> dict[Path, str]:
    """SHA-256 every path in parallel. Order-independent, so still deterministic."""

    if not paths:
        return {}
    count = workers or _pack_hash_workers()
    if count <= 1 or len(paths) == 1:
        return {path: sha256_file(path) for path in paths}
    with ThreadPoolExecutor(max_workers=min(count, len(paths))) as pool:
        digests = list(pool.map(sha256_file, paths))
    return dict(zip(paths, digests))


def _bundle_digest_with_files(pack_root: Path | str) -> tuple[str, dict[Path, str]]:
    """Bundle digest plus the per-file digests it was folded from.

    Verifying a published bundle needs BOTH its address and its audit, and
    both used to walk and re-hash the whole tree independently. One pass now
    serves both; the returned mapping is the evidence from THIS read, so
    handing it to :func:`audit_pack` removes a duplicate read without removing
    a check.
    """

    root = Path(pack_root).expanduser().resolve()
    paths = [path for path in _pack_files(root) if path.is_file()]
    digests = _hash_files(paths)
    return (
        _fold_bundle_digest(
            (path.relative_to(root).as_posix(), path.stat().st_size, digests[path])
            for path in paths
        ),
        digests,
    )


def bundle_digest(pack_root: Path | str) -> str:
    return _bundle_digest_with_files(pack_root)[0]


def _replace_directory_with_retry(
    source: Path, destination: Path, *, attempts: int = 6
) -> None:
    """os.replace a directory, retrying transient Windows sharing failures.

    Publishing a content-addressed cache entry renames a freshly written
    directory into place. On Windows that rename fails with ``WinError 5
    (Access is denied)`` whenever anything still holds a handle inside the
    source tree - most often the virus scanner or the search indexer picking
    up the file we just wrote. The condition clears in tens of milliseconds.

    Observed for real: a cold six-faction-class cook lost one audio and one
    W3D conversion to exactly this, which previously surfaced only as two
    silently missing outputs in the finished pack.

    A peer that populated the same key first is *not* handled here - the
    caller owns that check, because only it can compare output bytes.
    """

    delay = 0.05
    last: OSError | None = None
    for attempt in range(attempts):
        try:
            os.replace(source, destination)
            return
        except OSError as exc:
            last = exc
            if destination.is_dir():
                # Someone else won the race; let the caller verify bytes.
                raise
            if attempt == attempts - 1:
                break
            time.sleep(delay)
            delay = min(delay * 2, 1.0)
    assert last is not None
    raise OSError(
        last.errno,
        f"could not move {source} into place at {destination} after "
        f"{attempts} attempts: {last}. Another program is holding a handle "
        "inside the conversion cache - exclude the OpenBFME state directory "
        "from real-time virus scanning and search indexing, then re-run.",
    ) from last


def _copy_file_with_digest(source: Path, destination: Path) -> tuple[int, str]:
    """Copy one file, returning ``(bytes_written, sha256_of_bytes_written)``.

    One read serves both the copy and the verification. A conversion-cache hit
    used to read the same payload three times - hash the cache entry, copy it,
    hash the copy - which is why 2,511 warm media hits cost 86 s of a 188 s
    faction publish. Hashing what is actually written is the STRONGER of the
    two old checks: it proves the destination bytes, not merely that the source
    was intact before the copy started.
    """

    digest = hashlib.sha256()
    written = 0
    with source.open("rb") as reader, destination.open("wb") as writer:
        while chunk := reader.read(COPY_CHUNK):
            digest.update(chunk)
            writer.write(chunk)
            written += len(chunk)
    return written, digest.hexdigest()


def _copy_tree_with_digest(source: Path, destination: Path) -> str:
    """Copy *source* to *destination*, returning the copy's bundle digest.

    Each file is read once: the bytes are written to the destination and fed
    to SHA-256 in the same loop, so the returned digest describes what
    actually landed on disk. Comparing it to the source's bundle digest both
    verifies the copy and yields the published digest, replacing the previous
    copytree + bundle_digest + audit_pack sequence (three extra full passes).

    Auditing the copy separately is redundant once the digests match: the
    bundle digest covers every file's relative path, size and content, so a
    copy that matches an already-audited source is itself audited.
    """

    paths = _pack_files(source)
    for path in paths:
        if _is_link_like(path):
            raise RuntimeError(
                f"refusing to publish a pack containing a link: {path}"
            )
    # Same membership and ordering as bundle_digest, so the digest this
    # returns is directly comparable to the source's.
    relatives = [
        path.relative_to(source).as_posix() for path in paths if path.is_file()
    ]
    for parent in {(destination / relative).parent for relative in relatives}:
        parent.mkdir(parents=True, exist_ok=True)

    def _copy_one(relative: str) -> tuple[int, str]:
        digest = hashlib.sha256()
        written = 0
        with (source / relative).open("rb") as reader, (
            destination / relative
        ).open("wb") as writer:
            while chunk := reader.read(COPY_CHUNK):
                digest.update(chunk)
                writer.write(chunk)
                written += len(chunk)
        return written, digest.hexdigest()

    workers = min(_pack_hash_workers(), max(1, len(relatives)))
    with ThreadPoolExecutor(max_workers=workers) as pool:
        results = list(pool.map(_copy_one, relatives))
    return _fold_bundle_digest(
        (relative, size, digest)
        for relative, (size, digest) in zip(relatives, results)
    )


def _fold_bundle_digest(rows: Any) -> str:
    """Fold (relative, size, sha256) rows into the canonical bundle digest.

    Rows must arrive sorted by *relative*; the byte layout is unchanged from
    the original serial implementation, so digests stay comparable across
    versions and reproducibility checks keep working.
    """

    digest = hashlib.sha256()
    for relative, size, file_digest in rows:
        digest.update(relative.encode("utf-8"))
        digest.update(b"\0")
        digest.update(str(size).encode("ascii"))
        digest.update(b"\0")
        digest.update(file_digest.encode("ascii"))
        digest.update(b"\n")
    return digest.hexdigest()


def update_selection_entry(
    content_root: Path | str,
    pack_id: str,
    bundle_sha256: str,
) -> dict[str, Any]:
    """Atomically repoint every selection.json entry for ``pack_id`` to
    ``bundle_sha256``.

    Publication no longer touches selection.json by default, so a supplement
    (or active pack) that references a republished pack id is retargeted here
    in exactly one atomic rewrite: activePack stays active, supplement order
    is preserved, and no other entry changes.

    ROLE-PRESERVING BY DESIGN (RULE P2): this is the one writer that moves a
    single entry without restating the whole set, and that is exactly why it
    exists. What it lacked was exclusion - it read, modified and wrote a
    document a multi-root transaction could be mid-swap on, so the two writers
    could overwrite or erase each other. It now holds the same content-root
    lock for its whole validate -> read -> write lifetime.
    """

    if not pack_id or any(
        character not in "abcdefghijklmnopqrstuvwxyz0123456789._-"
        for character in pack_id
    ):
        raise ValueError(f"unsafe pack id: {pack_id!r}")
    digest = str(bundle_sha256).strip().lower()
    if len(digest) != 64 or any(
        character not in "0123456789abcdef" for character in digest
    ):
        raise ValueError(f"bundle sha256 is not a 64-hex digest: {bundle_sha256!r}")
    root = ensure_external_to_repo(Path(content_root), repo_root_from_module())
    with selection_transaction_lock(root):
        return _update_selection_entry_locked(root, pack_id, digest)


def _update_selection_entry_locked(
    root: Path, pack_id: str, digest: str
) -> dict[str, Any]:
    target_relative = f"{pack_id}/{digest}"
    target_dir = root / pack_id / digest
    if not target_dir.is_dir():
        raise FileNotFoundError(
            f"published bundle missing for selection update: {target_dir}"
        )
    target_pack = read_json(target_dir / "pack.json")
    if str(target_pack.get("id", "")) != pack_id:
        raise ValueError(
            f"published bundle at {target_dir} carries pack id "
            f"{target_pack.get('id')!r}, expected {pack_id!r}"
        )
    selection_path = root / "selection.json"
    if not selection_path.is_file():
        raise FileNotFoundError(
            f"selection.json does not exist yet: {selection_path}; "
            "publish with --select to create the first selection"
        )
    selection = read_json(selection_path)
    if not isinstance(selection, dict):
        raise ValueError(f"selection.json root is not an object: {selection_path}")

    def _references_pack(raw: Any) -> bool:
        entry = str(raw).strip().replace("\\", "/")
        return entry == pack_id or entry.startswith(f"{pack_id}/")

    updated: list[dict[str, str]] = []
    referenced = False
    active = selection.get("activePack")
    if active is not None and _references_pack(active):
        referenced = True
        if str(active).strip().replace("\\", "/") != target_relative:
            selection["activePack"] = target_relative
            updated.append({"role": "activePack", "previous": str(active)})
    supplements = selection.get("supplementalPacks")
    if isinstance(supplements, list):
        for index, raw in enumerate(supplements):
            if not _references_pack(raw):
                continue
            referenced = True
            if str(raw).strip().replace("\\", "/") == target_relative:
                continue
            supplements[index] = target_relative
            updated.append(
                {
                    "role": f"supplementalPacks[{index}]",
                    "previous": str(raw),
                }
            )
    if not referenced:
        raise ValueError(
            f"no selection entry references pack id {pack_id!r} in {selection_path}"
        )
    if updated:
        write_json_atomic(selection_path, selection)
    return {
        "selection": str(selection_path),
        "pack_relative": target_relative,
        "updated": updated,
        "changed": bool(updated),
    }


SELECTION_SCHEMA = "openbfme.pack-selection"
SELECTION_SCHEMA_VERSION = 0
#: Files whose presence means a cook is writing pack bundles right now. RULE P1
#: freezes compiler identity during a cook; changing the selection underneath one
#: is the same class of race, so the transaction refuses instead of interleaving.
#: TODO(agent): no pre-existing cook lock exists anywhere in the importer, so
#: this guard defines the marker itself. Whoever adds a real cook lock should
#: point COOK_ACTIVE_MARKERS at it rather than adding a second convention.
COOK_ACTIVE_MARKERS: tuple[str, ...] = (".cook-active", ".cooking")
#: Interprocess mutual exclusion for the whole validate -> stage -> commit ->
#: verify -> rollback lifetime. The marker files above are advisory breadcrumbs a
#: cook may forget to drop; this is an atomic O_CREAT|O_EXCL create, so two
#: processes cannot both believe they own the selection.
#: TODO(agent): the cook side lives outside this packet's allowed paths. Every
#: cook and publish path that writes pack bundles under a content root MUST take
#: this same lock (see :func:`selection_transaction_lock`) before it starts and
#: hold it until its bundles are complete; until it does, a cook racing this
#: transaction is only caught by the advisory markers.
SELECTION_TRANSACTION_LOCK = ".selection-transaction.lock"
_SELECTION_RESERVED_FIELDS = frozenset(
    {"schema", "schemaVersion", "activePack", "supplementalPacks"}
)


class SelectionTransactionError(RuntimeError):
    """A staged multi-pack selection change refused, failed, or rolled back.

    Every raise carries the full cause chain in its message: a rollback that
    itself failed names BOTH the original fault and the restore fault, because
    a half-restored pair of selection documents is exactly the hybrid state
    this transaction exists to make impossible (RULE P7).
    """


def _selection_payload_bytes(
    active_pack: str, supplemental_packs: Sequence[str], extra: Mapping[str, Any]
) -> bytes:
    document: dict[str, Any] = {
        "schema": SELECTION_SCHEMA,
        "schemaVersion": SELECTION_SCHEMA_VERSION,
        "activePack": active_pack,
        "supplementalPacks": list(supplemental_packs),
    }
    document.update(
        {key: value for key, value in extra.items() if key not in _SELECTION_RESERVED_FIELDS}
    )
    return (
        json.dumps(document, indent=2, sort_keys=True, ensure_ascii=False) + "\n"
    ).encode("utf-8")


def _normalized_selection_entry(raw: Any) -> str:
    entry = str(raw).strip().replace("\\", "/")
    try:
        parts = safe_relative_parts(entry)
    except ValueError as error:
        raise SelectionTransactionError(
            f"unsafe selection entry {entry!r}: {error}"
        ) from error
    if len(parts) != 2 or len(parts[1]) != 64 or any(
        character not in "0123456789abcdef" for character in parts[1].lower()
    ):
        raise SelectionTransactionError(
            "selection entry is not content-addressed (expected "
            f"<pack-id>/<64-hex bundle digest>): {entry}"
        )
    if any(
        character not in "abcdefghijklmnopqrstuvwxyz0123456789._-"
        for character in parts[0]
    ):
        raise SelectionTransactionError(f"unsafe pack id in selection entry: {entry}")
    return f"{parts[0]}/{parts[1].lower()}"


def _verify_selection_entry_in_root(role: str, root: Path, entry: str) -> None:
    pack_id, digest = entry.split("/", 1)
    bundle = root / pack_id / digest
    manifest = bundle / "pack.json"
    if not manifest.is_file():
        raise SelectionTransactionError(
            f"{role} root {root} has no published bundle for selection entry "
            f"{entry} (expected {manifest})"
        )
    try:
        document = read_json(manifest)
    except (OSError, json.JSONDecodeError) as error:
        raise SelectionTransactionError(
            f"{role} bundle {bundle} has an unreadable pack.json: {error}"
        ) from error
    if not isinstance(document, dict) or str(document.get("id", "")) != pack_id:
        found = document.get("id") if isinstance(document, dict) else None
        raise SelectionTransactionError(
            f"{role} bundle {bundle} carries pack id {found!r}, expected {pack_id!r}"
        )


def _refuse_active_cook(roots: Sequence[tuple[str, Path]]) -> None:
    if os.environ.get("OPENBFME_COOK_ACTIVE", "").strip():
        raise SelectionTransactionError(
            "refusing to change the selection while a cook is active "
            "(OPENBFME_COOK_ACTIVE is set)"
        )
    for role, root in roots:
        for marker in COOK_ACTIVE_MARKERS:
            candidate = root / marker
            if candidate.exists():
                raise SelectionTransactionError(
                    "refusing to change the selection while a cook is active: "
                    f"{role} root holds {candidate}"
                )


def canonical_lock_roots(roots: Sequence[Path | str]) -> list[Path]:
    """Total order over the roots a caller must lock, independent of input order.

    Two transactions that share a durable root must take their locks in the SAME
    sequence or they deadlock holding half of each other's set. Sorting by the
    casefolded resolved path gives every process the same sequence from any
    argument order, and folds a root named twice (or in two cases, which Windows
    treats as one directory) into one lock.
    """

    ordered: dict[str, Path] = {}
    for value in roots:
        ordered.setdefault(_lock_root_key(value), Path(value).expanduser().resolve())
    return [ordered[key] for key in sorted(ordered)]


def _lock_root_key(root: Path | str) -> str:
    """Filesystem-appropriate identity of a lock root.

    NOT a blanket ``casefold``: on a case-sensitive filesystem ``/srv/Content``
    and ``/srv/content`` are two different directories, and folding them
    together would drop one of them from the lock set entirely - the opposite of
    what this lock exists to guarantee. ``os.path.normcase`` folds case on
    Windows and is the identity on POSIX, which is exactly the distinction.
    """

    return os.path.normcase(str(Path(root).expanduser().resolve()))


@contextlib.contextmanager
def selection_transaction_locks(roots: Sequence[Path | str]):
    """Hold the exclusive lock of EVERY given root for one critical section."""

    with contextlib.ExitStack() as stack:
        for root in canonical_lock_roots(roots):
            stack.enter_context(selection_transaction_lock(root))
        yield


@contextlib.contextmanager
def selection_transaction_lock(content_root: Path | str):
    """Hold exclusive ownership of one content root's selection.

    ``os.open(..., O_CREAT | O_EXCL)`` is atomic on both Windows and POSIX, so
    the process that creates the file owns the selection until it removes it.
    A held lock is a REFUSAL, never a wait and never a break-in: a lock left
    behind by a killed process is indistinguishable from a live one, and
    guessing wrong means two writers on the same selection document.
    """

    root = Path(content_root)
    lock_path = root / SELECTION_TRANSACTION_LOCK
    # BOUNDED, OPT-IN QUEUEING - not a break-in and not an unbounded wait.
    #
    # A batch that publishes several faction packs at once has every child
    # reaching for this same lock for the few seconds each spends copying its
    # bundle in. They are not conflicting writers - each lands in its own
    # immutable digest directory - they merely arrive at the same door. With a
    # zero-second budget, six of seven honest publishes die on contention after
    # having already paid a full cook.
    #
    # The budget defaults to 0, which is EXACTLY the historical behaviour, and
    # the refusal at the end of the budget is the same refusal with the same
    # message. Nothing deletes, steals or ignores a held lock, so a lock left
    # behind by a killed process still fails the run - just this many seconds
    # later. Callers who want the old instant answer simply leave it unset.
    budget = 0.0
    raw_budget = os.environ.get("OPENBFME_SELECTION_LOCK_WAIT_SECONDS", "").strip()
    if raw_budget:
        try:
            budget = max(0.0, float(raw_budget))
        except ValueError:
            budget = 0.0
    deadline = time.monotonic() + budget
    delay = 0.1
    while True:
        try:
            handle = os.open(lock_path, os.O_CREAT | os.O_EXCL | os.O_RDWR)
            break
        except FileExistsError as error:
            if time.monotonic() >= deadline:
                holder = ""
                try:
                    holder = lock_path.read_text(
                        encoding="utf-8", errors="replace"
                    ).strip()
                except OSError:
                    holder = "<unreadable>"
                waited = (
                    f" Waited {budget:g}s for it to clear." if budget else ""
                )
                raise SelectionTransactionError(
                    f"another selection transaction or cook holds the lock at "
                    f"{lock_path} (holder: {holder or '<empty>'}). Refusing rather "
                    f"than waiting or breaking in.{waited} If you are certain no "
                    "cook or transaction is running, delete that lock file by hand."
                ) from error
            time.sleep(min(delay, max(0.0, deadline - time.monotonic())))
            delay = min(delay * 1.5, 1.0)
    try:
        os.write(
            handle,
            json.dumps(
                {
                    "pid": os.getpid(),
                    "acquiredUtc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
                    "purpose": "openbfme selection transaction",
                },
                sort_keys=True,
            ).encode("utf-8"),
        )
        os.fsync(handle)
        yield lock_path
    finally:
        try:
            os.close(handle)
        finally:
            lock_path.unlink(missing_ok=True)


# Q86 (owner-ratified 2026-08-25): the durable-mirror DETECTION machinery is
# gone. The game loader now fails closed when a workspace selection is broken
# instead of falling back to the durable cache, so a "durable mirror of a
# workspace" has no consumer to desynchronise. The durable cache is written
# ONLY through an explicit --durable-root on apply-selection-transaction (the
# launcher install path); publish never infers or touches it.


def _write_recovery_preimage(path: Path, prior: bytes, index: int) -> Path:
    """Persist a target's exact prior bytes BEFORE anything is committed.

    Rollback keeps the prior bytes in memory too, but memory dies with the
    process. If this transaction is killed mid-commit, or a restore fails, the
    operator still has the byte-exact document on disk instead of a description
    of it.

    CREATED EXCLUSIVELY, with a uuid component. A pid+index name is not unique:
    the same process running a second transaction reproduced it exactly, and
    opening it for write TRUNCATED the only surviving copy of an earlier failed
    rollback's prior document. O_CREAT|O_EXCL means this can only ever create.
    """

    for _ in range(8):
        candidate = path.parent / (
            f"{path.name}.{os.getpid()}-{index}-{uuid.uuid4().hex}.preimage"
        )
        try:
            handle = os.open(candidate, os.O_CREAT | os.O_EXCL | os.O_WRONLY)
        except FileExistsError:
            continue
        with os.fdopen(handle, "wb") as stream:
            stream.write(prior)
            stream.flush()
            os.fsync(stream.fileno())
        return candidate
    raise SelectionTransactionError(
        f"could not create a recovery preimage beside {path}: every candidate "
        "name already existed"
    )


def _unresolved_recovery_preimages(root: Path) -> list[Path]:
    """Preimages a previous transaction left behind because a restore failed."""

    return sorted(root.glob("*.preimage"))


def _stage_selection_file(path: Path, payload: bytes) -> str:
    path.parent.mkdir(parents=True, exist_ok=True)
    handle, temp_name = tempfile.mkstemp(
        prefix=path.name + ".", suffix=".staged", dir=path.parent
    )
    with os.fdopen(handle, "wb") as stream:
        stream.write(payload)
        stream.flush()
        os.fsync(stream.fileno())
    return temp_name


def _discard_recovery_preimages(targets: Sequence[Mapping[str, Any]]) -> None:
    """Drop the preimages once every target is provably at its intended bytes."""

    for target in targets:
        preimage = target.get("preimage")
        if preimage is not None:
            Path(preimage).unlink(missing_ok=True)


def _restore_selection_file(path: Path, prior: bytes | None) -> None:
    if prior is None:
        path.unlink(missing_ok=True)
        return
    handle, temp_name = tempfile.mkstemp(
        prefix=path.name + ".", suffix=".restore", dir=path.parent
    )
    try:
        with os.fdopen(handle, "wb") as stream:
            stream.write(prior)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temp_name, path)
    finally:
        Path(temp_name).unlink(missing_ok=True)


def apply_selection_transaction(
    content_root: Path | str,
    active_pack: str,
    supplemental_packs: Sequence[str] = (),
    *,
    durable_root: Path | str | None = None,
    _stage_hook: Any = None,
) -> dict[str, Any]:
    """Replace the COMPLETE selection in every target root in one swap each.

    The materializer this replaces rewrote selection.json once per faction
    entry, so a failure mid-way left a hybrid document — part new bundles, part
    old — with the durable mirror at yet a third state. Nothing downstream can
    tell such a tree from a deliberate selection.

    The recipe here is staged and all-or-nothing:

    1. validate EVERY entry (safe relative path, content-addressed
       ``<pack-id>/<64-hex>``, bundle present, pack.json id matching) in EVERY
       target root, before touching a single file;
    2. compose the whole new document once and stage it beside each target;
    3. commit with exactly ONE :func:`os.replace` per TARGET (never per entry);
    4. re-read every target and require byte equality with the staged payload
       and, across targets, with each other;
    5. on ANY failure restore the exact prior bytes of every target, and if the
       restore itself fails raise naming both faults.

    ``_stage_hook`` is a test seam only: it is called with the stage names
    ``validated``, ``staged``, ``after-first-commit``, ``committed``,
    ``verified`` and ``rollback-begin`` so a test can inject a failure at a
    real boundary rather than mocking the filesystem.

    RULE P2: this never runs publish, and never touches selection roles other
    than the ones it is given; ``update-selection-entry`` remains the
    role-preserving single-pack repoint.
    """

    repo = repo_root_from_module()
    workspace = ensure_external_to_repo(Path(content_root), repo)
    roots: list[tuple[str, Path]] = [("workspace", workspace)]
    durable: Path | None = None
    if durable_root is not None:
        durable = ensure_external_to_repo(Path(durable_root), repo)
        roots.append(("durable", durable))
    for role, root in roots:
        if not root.is_dir():
            raise SelectionTransactionError(f"{role} content root does not exist: {root}")

    # One writer per content root for the transaction's WHOLE lifetime, and that
    # means EVERY target root, not just the workspace: two transactions with
    # different workspaces share one durable mirror, so a workspace-only lock
    # excludes nothing where it matters most. Locks are taken in canonical order
    # so two such transactions cannot deadlock on each other.
    with selection_transaction_locks([root for _, root in roots]):
        return _apply_selection_transaction_locked(
            workspace, durable, roots, active_pack, supplemental_packs, _stage_hook
        )


def _apply_selection_transaction_locked(
    workspace: Path,
    durable: Path | None,
    roots: list[tuple[str, Path]],
    active_pack: str,
    supplemental_packs: Sequence[str],
    _stage_hook: Any,
) -> dict[str, Any]:
    _refuse_active_cook(roots)
    # An unresolved preimage means an earlier transaction failed to restore this
    # very document and its only byte-exact copy is sitting right there. Running
    # over it is how that copy dies.
    for role, root in roots:
        stranded = _unresolved_recovery_preimages(root)
        if stranded:
            raise SelectionTransactionError(
                f"refusing to start: the {role} root {root} holds "
                f"{len(stranded)} unresolved recovery preimage(s) from a failed "
                "rollback ("
                + ", ".join(item.name for item in stranded)
                + "). Restore the selection from one of them (or delete them "
                "once you have confirmed the document is correct) before "
                "changing the selection again."
            )

    active = _normalized_selection_entry(active_pack)
    supplements = [_normalized_selection_entry(entry) for entry in supplemental_packs]
    entries = [active, *supplements]
    seen: set[str] = set()
    for entry in entries:
        if entry in seen:
            raise SelectionTransactionError(
                f"duplicate selection entry would mount the same bundle twice: {entry}"
            )
        seen.add(entry)
    for role, root in roots:
        for entry in entries:
            _verify_selection_entry_in_root(role, root, entry)

    targets: list[dict[str, Any]] = []
    for role, root in roots:
        path = root / "selection.json"
        prior = path.read_bytes() if path.is_file() else None
        targets.append({"role": role, "path": path, "prior": prior})

    # Operator metadata on the live workspace selection (for example
    # `operatorNote`) survives the swap: this transaction owns pack identity,
    # not the owner's annotations.
    extra: dict[str, Any] = {}
    workspace_prior = targets[0]["prior"]
    if workspace_prior is not None:
        try:
            parsed = json.loads(workspace_prior.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError):
            parsed = None
        if isinstance(parsed, dict):
            extra = {
                key: value
                for key, value in parsed.items()
                if key not in _SELECTION_RESERVED_FIELDS
            }

    payload = _selection_payload_bytes(active, supplements, extra)
    payload_sha = hashlib.sha256(payload).hexdigest()

    def _hook(stage: str) -> None:
        if _stage_hook is not None:
            _stage_hook(stage)

    staged: list[str] = []
    swaps = 0
    committed = False
    try:
        # Inside the try on purpose: EVERY fault from here on leaves through the
        # one rollback path and one exception type, so no caller has to know
        # which stage failed to know the tree was restored.
        _hook("validated")
        for index, target in enumerate(targets):
            staged.append(_stage_selection_file(target["path"], payload))
            if target["prior"] is not None:
                target["preimage"] = _write_recovery_preimage(
                    target["path"], target["prior"], index
                )
        _hook("staged")
        for index, target in enumerate(targets):
            os.replace(staged[index], target["path"])
            staged[index] = ""
            swaps += 1
            if index == 0:
                _hook("after-first-commit")
        committed = True
        _hook("committed")
        observed: list[bytes] = []
        for target in targets:
            current = target["path"].read_bytes()
            observed.append(current)
            if current != payload:
                raise SelectionTransactionError(
                    "post-commit verification failed: "
                    f"{target['role']} selection at {target['path']} does not match "
                    f"the staged payload (sha256 {hashlib.sha256(current).hexdigest()} "
                    f"!= {payload_sha})"
                )
        if len(observed) > 1 and any(item != observed[0] for item in observed[1:]):
            raise SelectionTransactionError(
                "post-commit verification failed: the durable selection mirror is "
                "not byte-identical to the workspace selection"
            )
        _hook("verified")
    except BaseException as error:  # noqa: BLE001 - re-raised below, always
        cause = f"{type(error).__name__}: {error}"
        # EVERY target is restored INDEPENDENTLY and then re-read. The previous
        # shape stopped at the first restore exception, which is precisely how a
        # failed transaction could leave the workspace on the new document and
        # the durable mirror on the old one - the hybrid state this transaction
        # exists to make impossible.
        restore_failures: list[str] = []
        try:
            _hook("rollback-begin")
        except BaseException as begin_failure:  # noqa: BLE001
            restore_failures.append(
                f"rollback never started ({type(begin_failure).__name__}: "
                f"{begin_failure}); NO target was restored"
            )
        else:
            for target in targets:
                role = target["role"]
                path = target["path"]
                prior = target["prior"]
                try:
                    _hook(f"rollback-target:{role}")
                    _restore_selection_file(path, prior)
                    # Fires after the restore is written, before it is proven.
                    _hook(f"rollback-restored:{role}")
                    observed = path.read_bytes() if path.is_file() else None
                    if observed != prior:
                        raise SelectionTransactionError(
                            f"the restored bytes do not match the prior document "
                            f"(sha256 "
                            f"{hashlib.sha256(observed).hexdigest() if observed is not None else '<absent>'}"
                            f" != "
                            f"{hashlib.sha256(prior).hexdigest() if prior is not None else '<absent>'})"
                        )
                except BaseException as restore_failure:  # noqa: BLE001
                    preimage = target.get("preimage")
                    where = (
                        str(preimage)
                        if preimage is not None
                        else "<none: the file did not exist before this transaction>"
                    )
                    restore_failures.append(
                        f"{role} target {path} was NOT restored "
                        f"({type(restore_failure).__name__}: {restore_failure}); "
                        f"its exact prior bytes are preserved at {where}"
                    )
        for temp_name in staged:
            if temp_name:
                Path(temp_name).unlink(missing_ok=True)
        if restore_failures:
            # Preimages are deliberately LEFT on disk here: they are the only
            # byte-exact copy of what the operator has to put back.
            raise SelectionTransactionError(
                f"selection transaction failed ({cause}) and the rollback did not "
                f"fully restore every target. MANUAL RECOVERY REQUIRED: "
                + " | ".join(restore_failures)
                + ". Do not launch anything against these selections until they "
                "are restored by hand."
            ) from error
        _discard_recovery_preimages(targets)
        raise SelectionTransactionError(
            f"selection transaction failed and was rolled back: {cause}. "
            f"Committed {swaps} of {len(targets)} target(s) before the fault; "
            "every target has been restored to its exact prior bytes and each "
            "restore was verified independently."
        ) from error
    finally:
        for temp_name in staged:
            if temp_name:
                Path(temp_name).unlink(missing_ok=True)

    assert committed
    _discard_recovery_preimages(targets)
    return {
        "schema": "openbfme.selection-transaction",
        "schemaVersion": 1,
        "contentRoot": str(workspace),
        "durableRoot": str(durable) if durable is not None else None,
        "activePack": active,
        "supplementalPacks": supplements,
        "entries": entries,
        "payloadSha256": payload_sha,
        "swaps": swaps,
        "verified": True,
        "changed": any(target["prior"] != payload for target in targets),
        "preservedFields": sorted(extra),
        "targets": [
            {
                "role": target["role"],
                "path": str(target["path"]),
                "sha256": payload_sha,
                "previousSha256": (
                    hashlib.sha256(target["prior"]).hexdigest()
                    if target["prior"] is not None
                    else None
                ),
                "changed": target["prior"] != payload,
            }
            for target in targets
        ],
    }


#: Suffixes the cook itself writes while a file is still in flight. None of
#: them may ever survive into a finished pack: they are partial or duplicate
#: bytes, and a pack that carries one addresses to a digest that no clean run
#: of the same inputs reproduces.
PACK_IN_FLIGHT_SUFFIXES = (
    ".cache-copying",
    ".media-cache-copying",
    ".openbfme-part",
    ".tmp",
)


def _canonical_pack_inventory(
    pack_root: Path, known_digests: Mapping[str, str] | None = None
) -> list[dict[str, Any]]:
    root = pack_root.resolve()
    excluded = {"provenance/manifest.json", "provenance/audit.json"}
    selected: list[tuple[Path, str]] = []
    for path in _pack_files(root):
        if _is_link_like(path):
            raise RuntimeError(f"pack inventory refuses symbolic links: {path}")
        if not path.is_file():
            continue
        # FAIL CLOSED ON A LEFTOVER PARTIAL.
        #
        # A conversion-cache copy stages into the pack under one of these
        # suffixes and renames it into place. Every failure path is supposed to
        # remove it, and on 2026-08-20 one did not: a seven-way concurrent pack
        # proof shipped a Men bundle with one extra file and a different
        # address, while every converted output was byte-identical. The digest
        # is content-addressed, so nothing downstream could tell the difference
        # - it just silently was not the pack the same inputs produce.
        #
        # Refusing here makes that a build failure at the moment it happens
        # rather than an address that quietly drifts.
        if path.name.casefold().endswith(PACK_IN_FLIGHT_SUFFIXES):
            raise RuntimeError(
                "pack inventory refuses an in-flight temporary file left in the "
                f"pack: {path.relative_to(root).as_posix()}. A conversion-cache "
                "copy or atomic write failed part way and did not clean up; the "
                "cook must not address a bundle that contains it."
            )
        relative = path.relative_to(root).as_posix()
        safe_relative_parts(relative)
        if relative in excluded:
            continue
        selected.append((path, relative))
    # Reuse digests the caller just computed for this pack's converted
    # outputs instead of re-reading every file. This is not a trust
    # shortcut: build() runs a full audit_pack(light=False) over the same
    # staging tree immediately after, which re-hashes every file against
    # this inventory, so a wrong reused digest fails the build.
    reused = known_digests or {}
    to_hash = [path for path, relative in selected if relative not in reused]
    digests = _hash_files(to_hash)
    return [
        {
            "path": relative,
            "size": path.stat().st_size,
            "sha256": reused.get(relative) or digests[path],
        }
        for path, relative in selected
    ]


def _read_release_identity(path: Path) -> tuple[str, bool]:
    """Return the stamped ``(commit, clean)`` pair, or fail closed.

    The stamp is the authority for a shipped build, so anything short of a
    complete, current, clean identity is a refusal rather than a fallback:
    silently dropping to Git here is what let bundles inherit an unrelated
    checkout's commit in the first place.
    """

    def reject(reason: str) -> RuntimeError:
        return RuntimeError(
            f"bundled importer release identity is unusable ({reason}): {path}"
        )

    try:
        identity = read_json(path)
    except (OSError, ValueError) as exc:
        raise reject("unreadable") from exc
    if not isinstance(identity, dict):
        raise reject("not an object")
    if identity.get("schema") != RELEASE_IDENTITY_SCHEMA:
        raise reject("unexpected schema")
    if identity.get("schemaVersion") != RELEASE_IDENTITY_SCHEMA_VERSION:
        raise reject("unsupported schemaVersion")
    commit = identity.get("commit")
    if not _is_git_commit(commit):
        raise reject("commit is not a full git revision")
    if identity.get("sourceClean") is not True:
        raise reject("source was not clean when the build was stamped")
    return str(commit).casefold(), True


def _importer_recipe_report() -> dict[str, Any]:
    root = repo_root_from_module().resolve()
    identity_path = root / RELEASE_IDENTITY_RELATIVE
    if identity_path.is_file():
        commit, clean = _read_release_identity(identity_path)
        provenance_source = PROVENANCE_SOURCE_RELEASE_IDENTITY
    else:
        # No stamp: this must be a real checkout rooted exactly here. An
        # enclosing repository's HEAD is not this tree's identity.
        commit = git_revision_at_exact_root(root)
        clean = commit is not None and git_worktree_clean_at_exact_root(root)
        provenance_source = PROVENANCE_SOURCE_GIT_EXACT_ROOT
    requirements = [
        root / "importer" / name
        for name in IMPORTER_REQUIREMENTS_NAMES
        if (root / "importer" / name).is_file()
    ]
    candidates = [
        *sorted((root / "importer" / "openbfme_importer").rglob("*.py")),
        *sorted((root / "importer" / "blender").rglob("*.py")),
        *requirements,
        root / "tools" / "openbfme_import.py",
        root / "tools" / "bootstrap-importer-python.ps1",
        # The self-attestation is only worth anything inside the hash it
        # attests to.
        identity_path,
    ]
    files: list[dict[str, Any]] = []
    digest = hashlib.sha256()
    for path in sorted(
        {candidate.resolve() for candidate in candidates if candidate.is_file()},
        key=lambda candidate: candidate.relative_to(root).as_posix(),
    ):
        relative = path.relative_to(root).as_posix()
        size = path.stat().st_size
        file_sha256 = sha256_file(path)
        files.append({"path": relative, "size": size, "sha256": file_sha256})
        digest.update(relative.encode("utf-8"))
        digest.update(b"\0")
        digest.update(str(size).encode("ascii"))
        digest.update(b"\0")
        digest.update(file_sha256.encode("ascii"))
        digest.update(b"\n")
    return {
        "tree_sha256": digest.hexdigest(),
        "files": files,
        "git_commit": commit,
        "git_worktree_clean": clean,
        "provenance_source": provenance_source,
        "requirements_files": sorted(
            path.relative_to(root).as_posix() for path in requirements
        ),
    }


class ImportPipeline:
    def __init__(
        self,
        catalog: InstallCatalog,
        state_root: Path,
        *,
        game: str = "bfme2",
        conversion_cache_enabled: bool = True,
        conversion_jobs: int | None = None,
        source_override_root: Path | None = None,
        reconvert_only: Sequence[str] = (),
        single_build: bool = False,
    ) -> None:
        self.catalog = catalog
        self.state_root = ensure_external_to_repo(state_root, repo_root_from_module())
        self.game = retail_game(game)
        self.workspace_root = workspace_root(self.state_root, self.game.id)
        self._source_override_root: Path | None = None
        # True when this is a RotWK pipeline that must bind the canonical
        # oracle tree but could not at construction time because the tree does
        # not exist yet. `extract-all-assets` and `import-faction` BUILD that
        # tree, and both construct a pipeline first, so binding eagerly here is
        # a chicken-and-egg that fails those commands outright (it broke
        # test_game_editions). The requirement is therefore deferred to the
        # point of USE - `source_override_root` below - where a RotWK cook that
        # has no sealed oracle still fails closed rather than silently falling
        # back to the raw catalog.
        self._rotwk_autobind_pending = False
        if source_override_root is not None:
            explicit_root = Path(source_override_root).expanduser().resolve()
            verify_effective_assets(
                explicit_root,
                game=self.game.id,
                catalog=None if self.game.id == "rotwk" else catalog,
                consumer="import-pipeline-explicit-source-override",
            )
            self._source_override_root = explicit_root
        elif self.game.id == "rotwk":
            # CANONICAL TREE REBOUND 2026-08-04 (owner decision): PURE RETAIL
            # 2.01 = <workspace>/cache/effective-assets, which is what
            # `extract-all-assets --game rotwk` actually produces.
            #
            # This used to hard-require `cache/layered-effective-assets`, which
            # is NOT pure retail - its catalog carries `__patch202.big` and 530
            # of its INI files carry Unofficial-2.02 three-way merge markers
            # (`;,;` / `;;,;;`). That tree is quarantined: never an oracle,
            # never deleted. The layered bind also broke
            # test_game_editions.test_rotwk_catalog_and_effective_tree_do_not_touch_bfme2_paths,
            # which constructs an ImportPipeline straight after
            # extract-all-assets and therefore only ever has the pure tree.
            #
            # The sealed-manifest verification is unchanged: the tree's edition
            # and manifest identity are still checked here, and the catalog is
            # still retained only for retail reachability/archive attestation.
            effective_root = self.workspace_root / "cache" / "effective-assets"
            if not effective_root.is_dir():
                self._rotwk_autobind_pending = True
            else:
                verify_effective_assets(
                    effective_root,
                    game="rotwk",
                    catalog=None,
                    consumer="import-pipeline-source-override",
                )
                self._source_override_root = effective_root.resolve()
                self.catalog = EffectiveAssetsCatalog(
                    effective_root, base_catalog=self.catalog
                )
        self.sources_root = self.workspace_root / "cache" / "sources"
        self.packs_root = self.workspace_root / "packs"
        self.reports_root = self.workspace_root / "reports"
        self.jobs_root = self.workspace_root / "jobs"
        # Shared DDC root (cross-faction): OPENBFME_SHARED_CACHE or workspace cache.
        shared = os.environ.get("OPENBFME_SHARED_CACHE", "").strip()
        if shared:
            shared_root = Path(shared).expanduser().resolve()
        else:
            shared_root = self.workspace_root / "cache"
        self.converted_cache_root = shared_root / "converted"
        # Blender import/export is mostly single-threaded per process; more
        # workers amortize spawn cost better on 16–24 core boxes.
        default_jobs = max(1, min(16, (os.cpu_count() or 1) - 2))
        if conversion_jobs is not None and conversion_jobs < 1:
            raise ValueError("conversion_jobs must be at least 1")
        self.conversion_jobs = conversion_jobs or default_jobs
        self.conversion_cache_enabled = conversion_cache_enabled
        self.reconvert_only = tuple(str(pattern) for pattern in reconvert_only)
        if any(not pattern.strip() for pattern in self.reconvert_only):
            raise ValueError("reconvert-only patterns must not be empty")
        self.single_build = bool(single_build)
        # Set by build(): the pack root it finished, that pack's bundle digest
        # and the full audit it passed. Consumed by the CLI so a cook is not
        # audited and hashed twice more over the same unchanged bytes.
        self._last_build_verification: dict[str, Any] | None = None
        self._conversion_cache_stats = {
            "hits": 0,
            "misses": 0,
            "populated": 0,
            "forced": 0,
        }
        self._conversion_cache_lock = threading.Lock()
        self._conversion_key_locks: dict[str, threading.Lock] = {}
        self._w3d_batch_tools: dict[str, Any] | None = None
        self._w3d_final_attestation: dict[str, Any] | None = None
        self._blender_tree_verified = False
        self._blender_exe_fingerprint: tuple[int, int] | None = None
        self._blender_soft_tree_fingerprint_value: str | None = None
        self._python_runtime_report: dict[str, Any] = {}
        self._ffmpeg_attested_path: str | None = None
        self._pillow_choice_logged = False
        # (path, size, mtime_ns) -> sha256 of a retail source archive.
        self._archive_attest_cache: dict[tuple[str, int, int], str] = {}
        self.media_cache_root = shared_root / "converted-media"
        self.dev_mode = os.environ.get("OPENBFME_DEV", "").strip().casefold() in {
            "1",
            "true",
            "yes",
        }
        # Which roots this pipeline bound to is the first question any wrong-
        # output bug report has to answer, so it is recorded at construction
        # rather than inferred later from whatever the environment says then.
        from .diagnostics import active_run

        active_run().decision(
            "pipeline-roots",
            chosen=str(self.workspace_root),
            reason="ImportPipeline bound its state, source and pack roots",
            game=self.game.id,
            state_root=str(self.state_root),
            sources_root=str(self.sources_root),
            packs_root=str(self.packs_root),
            media_cache_root=str(self.media_cache_root),
            shared_cache=os.environ.get("OPENBFME_SHARED_CACHE") or None,
            source_override_root=(
                str(self._source_override_root)
                if self._source_override_root is not None
                else None
            ),
            rotwk_autobind_pending=bool(self._rotwk_autobind_pending),
            catalog_type=type(self.catalog).__name__,
            dev_mode=self.dev_mode,
            conversion_cache_enabled=self.conversion_cache_enabled,
            conversion_jobs=self.conversion_jobs,
            reconvert_only=list(self.reconvert_only),
            single_build=self.single_build,
        )

    def build_verification_for(self, pack_root: Path | str) -> dict[str, Any] | None:
        """Return this run's own full audit + bundle digest for *pack_root*.

        ``None`` unless :meth:`build` finished that exact root in this process,
        which is the only case where the answer is evidence rather than a
        cached claim: the audit was a full (non-light) re-hash of the pack's
        every file, the digest was folded from those verified hashes, and the
        pack has not been rewritten since. Callers that get ``None`` must do
        the work themselves. Set ``OPENBFME_FULL_REVERIFY=1`` to refuse the
        hand-over and force the second full pass everywhere.
        """

        if os.environ.get("OPENBFME_FULL_REVERIFY", "").strip().casefold() in {
            "1",
            "true",
            "yes",
        }:
            return None
        recorded = self._last_build_verification
        if not recorded:
            return None
        if recorded["pack_root"] != str(Path(pack_root).expanduser().resolve()):
            return None
        return recorded

    @property
    def source_override_root(self) -> Path | None:
        """The sealed oracle tree whose bytes win over raw catalog extraction.

        Deferred fail-closed bind. A RotWK pipeline constructed before its
        canonical PURE-RETAIL ``cache/effective-assets`` tree exists (which is
        exactly what ``extract-all-assets`` and ``import-faction`` do - they
        BUILD that tree) records the requirement instead of raising, and it is
        enforced here, the first time anything actually asks for the oracle.
        A cook therefore still refuses to run without a sealed tree; only the
        commands whose job is to produce it are allowed to construct without
        one. Never returns ``None`` for RotWK with the tree absent: that would
        be a silent fallback to the raw catalog, which is precisely the failure
        mode this bind exists to prevent.
        """

        if self._rotwk_autobind_pending:
            effective_root = self.workspace_root / "cache" / "effective-assets"
            if not effective_root.is_dir():
                raise FileNotFoundError(
                    "canonical RotWK pure-retail effective-assets tree is missing: "
                    f"{effective_root} (run extract-all-assets --game rotwk first)"
                )
            verify_effective_assets(
                effective_root,
                game="rotwk",
                catalog=None,
                consumer="import-pipeline-source-override",
            )
            self._source_override_root = effective_root.resolve()
            self.catalog = EffectiveAssetsCatalog(
                effective_root, base_catalog=self.catalog
            )
            self._rotwk_autobind_pending = False
            from .diagnostics import active_run

            active_run().decision(
                "source-oracle",
                chosen=str(self._source_override_root),
                reason="deferred RotWK bind to the verified effective-assets tree",
                game="rotwk",
                catalog_type=type(self.catalog).__name__,
            )
        return self._source_override_root

    @property
    def conversion_cache_stats(self) -> dict[str, Any]:
        with self._conversion_cache_lock:
            return {
                "enabled": self.conversion_cache_enabled,
                "jobs": self.conversion_jobs,
                **self._conversion_cache_stats,
            }

    def _media_cache_lock(self, key: str) -> threading.Lock:
        with self._conversion_cache_lock:
            return self._conversion_key_locks.setdefault(f"media:{key}", threading.Lock())

    def _copy_media_cache_hit(self, key: str, target: Path) -> bool:
        if not self.conversion_cache_enabled:
            return False
        entry = self.media_cache_root / key[:2] / key
        metadata_path = entry / "metadata.json"
        cached_output = entry / "output.bin"
        # The in-flight copy lives INSIDE the pack staging tree, so any path
        # that leaves it behind ships it: the pack inventory picks the stray
        # up, the audit happily verifies it, and the bundle silently addresses
        # to a different digest than the same cook without the cache miss.
        # Observed for real on 2026-08-20 - a seven-way concurrent proof
        # produced a men pack with 4795 files instead of 4794 and a different
        # address, with byte-identical CONVERTED OUTPUTS. Cleanup therefore
        # belongs in `finally`, not on the paths someone remembered.
        in_flight: Path | None = None
        try:
            metadata = read_json(metadata_path)
            if (
                metadata.get("format") != 1
                or metadata.get("key") != key
                or metadata.get("output_size") != cached_output.stat().st_size
            ):
                if entry.is_dir():
                    shutil.rmtree(entry)
                with self._conversion_cache_lock:
                    self._conversion_cache_stats["misses"] += 1
                return False
            target.parent.mkdir(parents=True, exist_ok=True)
            temporary = target.with_name(target.name + ".media-cache-copying")
            in_flight = temporary
            temporary.unlink(missing_ok=True)
            # ONE read verifies the cache entry AND the copy: the digest is
            # taken from the bytes written to *temporary*, so it can only match
            # when the cache entry was intact and the copy reproduced it.
            written, copied_sha256 = _copy_file_with_digest(cached_output, temporary)
            if (
                written != metadata["output_size"]
                or copied_sha256 != metadata["output_sha256"]
            ):
                temporary.unlink(missing_ok=True)
                # Only the rare failure pays a second read, to keep the two
                # diagnoses distinct: a rotten cache entry is self-healing
                # (discard, miss, convert cold), a bad copy is a hard error.
                if sha256_file(cached_output) != metadata["output_sha256"]:
                    if entry.is_dir():
                        shutil.rmtree(entry)
                    with self._conversion_cache_lock:
                        self._conversion_cache_stats["misses"] += 1
                    return False
                raise RuntimeError("media conversion cache copy failed byte verification")
            os.replace(temporary, target)
            in_flight = None
        except (FileNotFoundError, KeyError, OSError, TypeError, ValueError):
            if entry.is_dir():
                shutil.rmtree(entry, ignore_errors=True)
            with self._conversion_cache_lock:
                self._conversion_cache_stats["misses"] += 1
            return False
        finally:
            # An OSError anywhere in the copy above used to leave the partial
            # file sitting in the pack. Nothing else removes it.
            if in_flight is not None:
                in_flight.unlink(missing_ok=True)
        with self._conversion_cache_lock:
            self._conversion_cache_stats["hits"] += 1
        return True

    def _populate_media_cache(self, key: str, target: Path) -> None:
        if not self.conversion_cache_enabled:
            return
        destination = self.media_cache_root / key[:2] / key
        if destination.is_dir():
            existing = destination / "output.bin"
            if existing.is_file() and sha256_file(existing) == sha256_file(target):
                return
            raise RuntimeError(
                "media conversion cache key produced non-byte-identical output"
            )
        self.media_cache_root.mkdir(parents=True, exist_ok=True)
        (self.media_cache_root / key[:2]).mkdir(parents=True, exist_ok=True)
        temporary: Path | None = Path(
            tempfile.mkdtemp(prefix=f".{key[:12]}.", dir=self.media_cache_root / key[:2])
        )
        try:
            cached_output = temporary / "output.bin"
            shutil.copyfile(target, cached_output)
            output_sha256 = sha256_file(target)
            if sha256_file(cached_output) != output_sha256:
                raise RuntimeError("media conversion cache populate changed output bytes")
            write_json_atomic(
                temporary / "metadata.json",
                {
                    "format": 1,
                    "key": key,
                    "output_size": target.stat().st_size,
                    "output_sha256": output_sha256,
                },
            )
            try:
                _replace_directory_with_retry(temporary, destination)
            except OSError:
                if not destination.is_dir():
                    raise
                # A peer process populated the same key first: accept iff the
                # peer's bytes are identical to ours (same key, same output).
                existing = destination / "output.bin"
                if not existing.is_file() or sha256_file(existing) != output_sha256:
                    raise RuntimeError(
                        "media conversion cache key produced non-byte-identical output"
                    )
                return
            temporary = None
            with self._conversion_cache_lock:
                self._conversion_cache_stats["populated"] += 1
        finally:
            if temporary is not None and temporary.is_dir():
                shutil.rmtree(temporary)

    def _validate_source_catalog_binding(self, resolved: ResolvedProfile) -> str:
        profile = resolved.profile
        pack = profile.pack_metadata
        requires_binding = (
            profile.id == "men-fords-v1"
            or "m3Recipe" in pack
            or any(
                path.replace("\\", "/").casefold().startswith("data/m3/")
                for path in profile.runtime_data
            )
            or any(rule.id.startswith("m3-") for rule in profile.resources)
        )
        expected = pack.get("sourceCatalogIdentitySha256")
        if requires_binding and expected is None:
            raise ValueError("M3 profile is missing its source catalog identity")
        if expected is not None and (
            not isinstance(expected, str)
            or len(expected) != 64
            or any(character not in "0123456789abcdef" for character in expected)
        ):
            raise ValueError("profile source catalog identity is invalid")
        catalog = getattr(self, "catalog", None)
        if catalog is None:
            if expected is not None:
                raise ValueError(
                    "profile source catalog identity cannot be verified without a catalog"
                )
            return ""
        actual = catalog.identity_sha256()
        if expected is not None and expected != actual:
            raise ValueError(
                "profile source catalog identity does not match the current catalog"
            )
        return actual

    def plan_report(self, resolved: ResolvedProfile) -> dict[str, Any]:
        catalog_identity = self._validate_source_catalog_binding(resolved)
        recipe = _importer_recipe_report()
        return {
            "format": 1,
            "profile": resolved.profile.id,
            "profile_sha256": resolved.profile.source_sha256,
            "importer_recipe_sha256": recipe["tree_sha256"],
            "catalog_identity_sha256": catalog_identity,
            "pack": resolved.profile.pack_id,
            "install_root": str(self.catalog.install_root),
            "ready": not resolved.missing_required,
            "missing_required": list(resolved.missing_required),
            "resource_count": len(resolved.resources),
            "selected_file_count": len(resolved.selected_entries),
            "selected_bytes": sum(entry.size for entry in resolved.selected_entries),
            "resources": [
                {
                    "id": item.rule.id,
                    "kind": item.rule.kind,
                    "converter": item.rule.converter,
                    "required": item.rule.required,
                    "missing_patterns": list(item.missing_patterns),
                    "count_error": item.count_error,
                    "matches": [asdict(entry) for entry in item.entries],
                }
                for item in resolved.resources
            ],
        }

    def _effective_asset_paths(self) -> tuple[Path, Path, Path, Path]:
        """Return contained final, manifest, staging, and backup paths."""

        state_root = self.workspace_root.resolve()
        cache_root = state_root / "cache"
        asset_root = cache_root / "effective-assets"
        staging = cache_root / "effective-assets.building"
        backup = cache_root / "effective-assets.previous"
        for candidate in (cache_root, asset_root, staging, backup):
            resolved = candidate.resolve()
            try:
                resolved.relative_to(state_root)
            except ValueError as exc:
                raise RuntimeError(
                    f"effective asset cache path escaped the state root: {candidate}"
                ) from exc
            current = state_root
            for part in candidate.relative_to(state_root).parts:
                current = current / part
                if os.path.lexists(current) and _is_link_like(current):
                    raise RuntimeError(
                        f"effective asset cache refuses a symbolic link or junction: {current}"
                    )
        manifest_path = asset_root.joinpath(
            *PurePosixPath(EFFECTIVE_ASSET_MANIFEST_RELATIVE).parts
        )
        return asset_root, manifest_path, staging, backup

    @staticmethod
    def _scan_effective_asset_tree(
        root: Path,
        entries: tuple[CatalogEntry, ...],
    ) -> None:
        if not root.is_dir() or _is_link_like(root):
            raise RuntimeError(
                "effective asset cache root is missing, linked, or not a directory"
            )

        expected_file_names = [entry.name for entry in entries]
        expected_file_names.append(EFFECTIVE_ASSET_MANIFEST_RELATIVE)
        expected_files = {relative.casefold() for relative in expected_file_names}
        expected_directories = {EFFECTIVE_ASSET_METADATA_DIRECTORY.casefold()}
        for relative in expected_file_names:
            parts = PurePosixPath(relative).parts
            for index in range(1, len(parts)):
                expected_directories.add("/".join(parts[:index]).casefold())

        actual_files: dict[str, str] = {}
        actual_directories: set[str] = set()
        for path in sorted(
            root.rglob("*"), key=lambda item: item.relative_to(root).as_posix()
        ):
            relative = path.relative_to(root).as_posix()
            if _is_link_like(path):
                raise RuntimeError(
                    f"effective asset cache contains a symbolic link or junction: {relative}"
                )
            if path.is_file():
                key = relative.casefold()
                if key in actual_files:
                    raise RuntimeError(
                        "effective asset cache contains case-colliding files: "
                        f"{actual_files[key]!r} and {relative!r}"
                    )
                actual_files[key] = relative
            elif path.is_dir():
                actual_directories.add(relative.casefold())
            else:
                raise RuntimeError(
                    f"effective asset cache contains an unsupported filesystem entry: {relative}"
                )

        actual_file_keys = set(actual_files)
        missing_files = sorted(expected_files - actual_file_keys)
        unexpected_files = sorted(actual_file_keys - expected_files)
        missing_directories = sorted(expected_directories - actual_directories)
        unexpected_directories = sorted(actual_directories - expected_directories)
        if missing_files:
            raise RuntimeError(
                "effective asset cache is missing files: "
                + ", ".join(missing_files[:5])
            )
        if unexpected_files:
            raise RuntimeError(
                "effective asset cache has unexpected files: "
                + ", ".join(unexpected_files[:5])
            )
        if missing_directories:
            raise RuntimeError(
                "effective asset cache is missing directories: "
                + ", ".join(missing_directories[:5])
            )
        if unexpected_directories:
            raise RuntimeError(
                "effective asset cache has unexpected directories: "
                + ", ".join(unexpected_directories[:5])
            )

        for entry in entries:
            target = root.joinpath(*PurePosixPath(entry.name).parts)
            if target.stat().st_size != entry.size:
                raise RuntimeError(f"effective asset cache size mismatch: {entry.name}")

    @staticmethod
    def _refuse_link_descendants(path: Path, context: str) -> None:
        if _is_link_like(path):
            raise RuntimeError(f"{context} is a symbolic link or junction: {path}")
        if not path.is_dir():
            return
        for child in path.rglob("*"):
            if _is_link_like(child):
                raise RuntimeError(
                    f"{context} contains a symbolic link or junction: {child}"
                )

    def _stream_effective_entries(
        self,
        entries: tuple[CatalogEntry, ...],
        output_root: Path,
        *,
        max_files: int,
        max_bytes: int,
    ) -> dict[str, str]:
        """Extract or verify winners while opening only one archive at a time."""

        by_archive: dict[str, list[CatalogEntry]] = defaultdict(list)
        for entry in entries:
            by_archive[entry.archive].append(entry)

        hashes: dict[str, str] = {}
        for archive_name in sorted(
            by_archive, key=lambda value: (value.casefold(), value)
        ):
            selected = sorted(
                by_archive[archive_name],
                key=lambda item: (item.name.casefold(), item.name),
            )
            archive = self.catalog.open_archive_for(selected[0])
            extracted = archive.extract(
                [self.catalog.as_entry(item) for item in selected],
                output_root,
                max_files=max_files,
                max_bytes=max_bytes,
                overwrite=False,
            )
            selected_by_signature = {
                (item.name.casefold(), item.offset, item.size): item
                for item in selected
            }
            for item in extracted:
                selected_entry = selected_by_signature.get(
                    (item.entry.key, item.entry.offset, item.entry.size)
                )
                if selected_entry is None:
                    raise RuntimeError(
                        f"archive extraction returned an unselected entry: {item.entry.name}"
                    )
                if selected_entry.key in hashes:
                    raise RuntimeError(
                        f"effective extraction produced a duplicate path: {selected_entry.name}"
                    )
                hashes[selected_entry.key] = item.sha256
        if len(hashes) != len(entries):
            raise RuntimeError(
                f"effective extraction produced {len(hashes)} of {len(entries)} files"
            )
        return hashes

    def _load_effective_manifest(
        self,
        manifest_path: Path,
        entries: tuple[CatalogEntry, ...],
    ) -> dict[str, Any]:
        if not manifest_path.is_file() or _is_link_like(manifest_path):
            raise RuntimeError("effective asset manifest is missing or linked")
        if manifest_path.stat().st_size > 64 * 1024 * 1024:
            raise RuntimeError("effective asset manifest exceeds the safety limit")
        try:
            raw_bytes = manifest_path.read_bytes()
            manifest = json.loads(raw_bytes.decode("utf-8"))
        except (OSError, UnicodeDecodeError, json.JSONDecodeError) as exc:
            raise RuntimeError(f"effective asset manifest is invalid: {exc}") from exc
        if not isinstance(manifest, dict):
            raise RuntimeError("effective asset manifest root is not an object")
        files = manifest.get("files")
        if not isinstance(files, list) or len(files) != len(entries):
            raise RuntimeError("effective asset manifest file inventory is invalid")

        declared_hashes: dict[str, str] = {}
        expected_keys = {"archive", "offset", "path", "precedence", "sha256", "size"}
        for entry, item in zip(entries, files):
            if not isinstance(item, dict) or set(item) != expected_keys:
                raise RuntimeError("effective asset manifest file entry is invalid")
            expected_source = {
                "archive": entry.archive,
                "offset": entry.offset,
                "path": entry.name,
                "precedence": entry.precedence,
                "size": entry.size,
            }
            if any(item.get(key) != value for key, value in expected_source.items()):
                raise RuntimeError(
                    f"effective asset manifest source identity mismatch: {entry.name}"
                )
            sha256 = item.get("sha256")
            if (
                not isinstance(sha256, str)
                or len(sha256) != 64
                or any(character not in "0123456789abcdef" for character in sha256)
            ):
                raise RuntimeError(
                    f"effective asset manifest hash is invalid: {entry.name}"
                )
            declared_hashes[entry.key] = sha256

        expected_manifest = _effective_asset_manifest(
            self.catalog, entries, declared_hashes
        )
        if manifest != expected_manifest:
            raise RuntimeError(
                "effective asset manifest identity or totals do not match"
            )
        if raw_bytes != _canonical_json_bytes(expected_manifest):
            raise RuntimeError("effective asset manifest JSON is not canonical")
        return manifest

    @staticmethod
    def _effective_asset_report(
        root: Path,
        manifest_path: Path,
        manifest: Mapping[str, Any],
        *,
        reused: bool,
    ) -> dict[str, Any]:
        return {
            "ready": True,
            "verified": True,
            "reused": reused,
            "asset_root": str(root),
            "manifest": str(manifest_path),
            "manifest_sha256": sha256_file(manifest_path),
            "catalog_identity_sha256": manifest["catalog"]["identity_sha256"],
            "install_identity_sha256": manifest["install"]["identity_sha256"],
            "file_count": manifest["totals"]["files"],
            "total_bytes": manifest["totals"]["bytes"],
            "aggregate_sha256": manifest["aggregate_sha256"],
        }

    def extract_all_assets(
        self,
        *,
        force: bool = False,
        max_files: int = MAX_EFFECTIVE_ASSET_FILES,
        max_bytes: int = MAX_EFFECTIVE_ASSET_BYTES,
    ) -> dict[str, Any]:
        """Materialize the complete case-insensitive winning catalog view."""

        if self.catalog is None:
            raise ValueError("effective extraction requires an install catalog")
        entries = _validate_effective_catalog(
            self.catalog, max_files=max_files, max_bytes=max_bytes
        )
        root, manifest_path, staging, backup = self._effective_asset_paths()

        if not force:
            scratch = next(
                (path for path in (staging, backup) if os.path.lexists(path)), None
            )
            if scratch is not None:
                raise RuntimeError(
                    "effective extraction scratch state already exists; use --force: "
                    f"{scratch}"
                )

        if os.path.lexists(root) and not force:
            try:
                manifest = self._load_effective_manifest(manifest_path, entries)
                self._scan_effective_asset_tree(root, entries)
                actual_hashes = self._stream_effective_entries(
                    entries,
                    root,
                    max_files=max_files,
                    max_bytes=max_bytes,
                )
                verified_manifest = _effective_asset_manifest(
                    self.catalog, entries, actual_hashes
                )
                if manifest != verified_manifest:
                    raise RuntimeError(
                        "effective asset cache bytes do not match its manifest"
                    )
                return self._effective_asset_report(
                    root, manifest_path, manifest, reused=True
                )
            except (FileExistsError, OSError, ValueError, RuntimeError) as exc:
                raise RuntimeError(
                    "effective asset cache verification failed; use --force to rebuild: "
                    f"{exc}"
                ) from exc

        cache_root = root.parent
        cache_root.mkdir(parents=True, exist_ok=True)
        # Revalidate after creating the cache parent so a pre-existing link cannot
        # turn a contained lexical child into an external write target.
        root, manifest_path, staging, backup = self._effective_asset_paths()
        for scratch in (staging, backup):
            if not os.path.lexists(scratch):
                continue
            if _is_link_like(scratch):
                raise RuntimeError(
                    f"effective extraction refuses linked scratch state: {scratch}"
                )
            if not force:
                raise RuntimeError(
                    f"effective extraction scratch state already exists; use --force: {scratch}"
                )
            if scratch.is_dir():
                self._refuse_link_descendants(
                    scratch, "effective extraction scratch state"
                )
                shutil.rmtree(scratch)
            else:
                scratch.unlink()

        staging.mkdir(parents=False)
        try:
            hashes = self._stream_effective_entries(
                entries,
                staging,
                max_files=max_files,
                max_bytes=max_bytes,
            )
            manifest = _effective_asset_manifest(self.catalog, entries, hashes)
            staging_manifest = staging.joinpath(
                *PurePosixPath(EFFECTIVE_ASSET_MANIFEST_RELATIVE).parts
            )
            write_json_atomic(staging_manifest, manifest)
            self._scan_effective_asset_tree(staging, entries)

            had_previous = os.path.lexists(root)
            if had_previous:
                if _is_link_like(root):
                    raise RuntimeError(
                        f"effective extraction refuses a linked destination: {root}"
                    )
                self._refuse_link_descendants(root, "effective extraction destination")
                os.replace(root, backup)
            try:
                os.replace(staging, root)
            except BaseException:
                if (
                    had_previous
                    and os.path.lexists(backup)
                    and not os.path.lexists(root)
                ):
                    os.replace(backup, root)
                raise
            if had_previous:
                if backup.is_dir():
                    shutil.rmtree(backup)
                else:
                    backup.unlink()
        except BaseException:
            if os.path.lexists(staging) and not _is_link_like(staging):
                if staging.is_dir():
                    shutil.rmtree(staging)
                else:
                    staging.unlink()
            raise

        manifest_path = root.joinpath(
            *PurePosixPath(EFFECTIVE_ASSET_MANIFEST_RELATIVE).parts
        )
        return self._effective_asset_report(root, manifest_path, manifest, reused=False)

    def extract_sources(
        self,
        resolved: ResolvedProfile,
        *,
        force: bool = False,
        # Extraction bound, not a pack-content limit. 10_000 was sized when a
        # profile held a single faction; all six BFME2 factions select 10_626,
        # so the old ceiling made a cross-faction pack un-cookable by a margin
        # of 6%. The byte bound below is the one that actually protects the
        # disk and is left alone.
        max_files: int = 65_536,
        max_bytes: int = 4 * 1024 * 1024 * 1024,
    ) -> dict[tuple[str, str], dict[str, Any]]:
        self._validate_source_catalog_binding(resolved)
        entries = resolved.selected_entries
        if len(entries) > max_files:
            raise RuntimeError(
                f"profile selects {len(entries)} files; limit is {max_files}"
            )
        total = sum(entry.size for entry in entries)
        if total > max_bytes:
            raise RuntimeError(f"profile selects {total} bytes; limit is {max_bytes}")
        by_archive: dict[str, list[CatalogEntry]] = defaultdict(list)
        for entry in entries:
            by_archive[entry.archive].append(entry)

        result: dict[tuple[str, str], dict[str, Any]] = {}
        for archive_name in sorted(by_archive, key=str.casefold):
            archive_entries = by_archive[archive_name]
            override_by_key: dict[str, Path] = {}
            if self.source_override_root is not None:
                for catalog_entry in archive_entries:
                    override = self.source_override_root.joinpath(
                        *PurePosixPath(catalog_entry.name).parts
                    )
                    if override.is_file():
                        override_by_key[catalog_entry.name.casefold()] = override

            # A layered source is already a sealed, verified extraction.  Do not
            # copy it over the catalog extraction cache: doing so corrupts the
            # warm cache because catalog metadata still describes the archive
            # member's original size and hash.  Select it directly and extract
            # only entries which the layered oracle does not provide.
            for catalog_entry in archive_entries:
                override = override_by_key.get(catalog_entry.name.casefold())
                if override is None:
                    continue
                source_sha256 = sha256_file(override)
                result[(archive_name.casefold(), catalog_entry.name.casefold())] = {
                    "catalog": catalog_entry,
                    "source_path": override,
                    "source_sha256": source_sha256,
                    "cache_key": _source_cache_key(catalog_entry, source_sha256),
                }

            extraction_entries = [
                item
                for item in archive_entries
                if item.name.casefold() not in override_by_key
            ]
            if not extraction_entries:
                continue
            archive = self.catalog.open_archive_for(extraction_entries[0])
            archive_slug = hashlib.sha256(archive_name.casefold().encode()).hexdigest()[
                :12
            ]
            archive_output = self.sources_root / archive_slug
            wanted = [self.catalog.as_entry(item) for item in extraction_entries]
            extracted = archive.extract(
                wanted,
                archive_output,
                max_files=max_files,
                max_bytes=max_bytes,
                overwrite=force,
            )
            catalog_by_key = {
                item.name.casefold(): item for item in extraction_entries
            }
            for item in extracted:
                catalog_entry = catalog_by_key[item.entry.key]
                source_path = item.output
                source_sha256 = item.sha256
                result[(archive_name.casefold(), item.entry.key)] = {
                    "catalog": catalog_entry,
                    "source_path": source_path,
                    "source_sha256": source_sha256,
                    "cache_key": _source_cache_key(catalog_entry, source_sha256),
                }
        return result

    def build(
        self,
        resolved: ResolvedProfile,
        *,
        force: bool = False,
        allow_incomplete: bool = False,
    ) -> Path:
        from .diagnostics import active_run
        from .progress import emit as progress_emit

        run = active_run()
        run.event(
            "build.begin",
            profile=resolved.profile.id,
            pack_id=resolved.profile.pack_id,
            pack_version=resolved.profile.pack_version,
            game=getattr(getattr(self, "game", None), "id", None),
            resource_rules=len(resolved.resources),
            missing_required=len(resolved.missing_required),
            force=bool(force),
            allow_incomplete=bool(allow_incomplete),
        )
        self._validate_source_catalog_binding(resolved)
        if resolved.missing_required and not allow_incomplete:
            missing = ", ".join(resolved.missing_required)
            run.refusal(
                "build",
                reason="required profile resources did not resolve",
                profile=resolved.profile.id,
                missing_required=list(resolved.missing_required),
            )
            raise RuntimeError(f"required profile resources did not resolve: {missing}")

        progress_emit("extract", "attesting archives/tools and extracting sources")
        extract_phase = run.begin_phase("extract", profile=resolved.profile.id)
        try:
            source_archives = self._attest_source_archives(resolved)
            self._verify_required_tools(resolved)
            extracted = self.extract_sources(resolved, force=force)
        except BaseException as exc:
            run.end_phase(extract_phase, outcome="failed", exc=exc)
            raise
        run.end_phase(
            extract_phase,
            source_archives=len(source_archives),
            extracted_files=len(extracted),
        )
        pack_root = self.packs_root / resolved.profile.pack_id
        staging = self.packs_root / (resolved.profile.pack_id + ".building")
        if staging.exists():
            shutil.rmtree(staging)
        staging.mkdir(parents=True)
        progress_emit(
            "convert-assets",
            f"cooking pack resources ({len(resolved.resources)} rules)",
        )
        convert_phase = run.begin_phase(
            "convert-assets",
            profile=resolved.profile.id,
            resource_rules=len(resolved.resources),
            # getattr, not attribute access: tests drive build() on partially
            # constructed pipelines, and a logging field must never be the thing
            # that raises. Diagnostics are evidence, never a precondition.
            conversion_jobs=getattr(self, "conversion_jobs", None),
            conversion_cache_enabled=getattr(self, "conversion_cache_enabled", None),
            staging=str(staging),
        )
        provenance_entries: list[dict[str, Any]] = []
        incomplete: list[dict[str, str]] = []
        # Single classification pass: incomplete reasons + W3D jobs + media jobs.
        w3d_jobs: list[
            tuple[int, list[Path], str | None, dict[str, Any], Path, str, str, str]
        ] = []
        media_outputs: dict[tuple[int, int], list[Path]] = {}
        media_errors: dict[tuple[int, int], Exception] = {}
        media_jobs: list[
            tuple[int, int, Path, str, str | None, dict[str, Any], Path, str]
        ] = []
        w3d_kind = {
            "w3d-bundle": "animated",
            "w3d-hierarchical": "hierarchical",
            "w3d-static": "static",
        }
        for resource_index, resource in enumerate(resolved.resources):
            reasons: list[str] = []
            if resource.missing_patterns:
                reasons.append(
                    "missing patterns: " + ", ".join(resource.missing_patterns)
                )
            if resource.count_error:
                reasons.append(resource.count_error)
            if resource.rule.required and not resource.entries and not reasons:
                reasons.append("no entries resolved")
            if resource.rule.required and reasons:
                incomplete.append(
                    {"resource": resource.rule.id, "reason": "; ".join(reasons)}
                )

            converter = resource.rule.converter
            if converter in w3d_kind and resource.entries:
                w3d_jobs.append(
                    (
                        resource_index,
                        _w3d_staging_sources(resource, resolved.resources, extracted),
                        resource.rule.output,
                        resource.rule.options,
                        staging,
                        resolved.profile.id,
                        resource.rule.id,
                        w3d_kind[converter],
                    )
                )
            elif converter in {"audio", "texture", "texture-crop"}:
                for entry_index, entry in enumerate(resource.entries):
                    cached = extracted.get(
                        (entry.archive.casefold(), entry.name.casefold())
                    )
                    if cached is None:
                        media_errors[(resource_index, entry_index)] = RuntimeError(
                            f"media source was not extracted: {entry.name}"
                        )
                        continue
                    media_jobs.append(
                        (
                            resource_index,
                            entry_index,
                            Path(cached["source_path"]),
                            converter,
                            resource.rule.output,
                            resource.rule.options,
                            staging,
                            str(cached.get("source_sha256") or ""),
                        )
                    )

        validate_reconvert_matches(
            tuple(str(job[6]) for job in w3d_jobs),
            getattr(self, "reconvert_only", ()),
        )

        # Overlap W3D and media lanes when both have work (independent outputs).
        # Use one progress stage so concurrent workers do not clobber stage ETA.
        if w3d_jobs and media_jobs:
            from .progress import emit as progress_emit

            progress_emit(
                "convert-assets",
                f"w3d={len(w3d_jobs)} media={len(media_jobs)} (parallel)",
                total_units=len(w3d_jobs) + len(media_jobs),
            )
            with ThreadPoolExecutor(max_workers=2) as coordinator:
                w3d_future = coordinator.submit(
                    self._convert_w3d_resources,
                    w3d_jobs,
                    progress_stage="",
                )
                media_future = coordinator.submit(
                    self._convert_media_jobs,
                    media_jobs,
                    media_errors,
                    progress_stage="",
                )
                w3d_outputs, w3d_errors = w3d_future.result()
                media_outputs, media_errors = media_future.result()
        else:
            w3d_outputs, w3d_errors = self._convert_w3d_resources(w3d_jobs)
            if media_jobs:
                media_outputs, media_errors = self._convert_media_jobs(
                    media_jobs, media_errors
                )

        # This loop runs the remaining single-source and bundle converters
        # (APT runtimes, terrain, atlas crops, ...). It used to report nothing
        # at all, so a multi-minute stretch here was indistinguishable from a
        # hang on an end user's machine.
        progress_emit(
            "convert-assets",
            f"cooking {len(resolved.resources)} resource bundles",
            total_units=len(resolved.resources),
        )
        for resource_index, resource in enumerate(resolved.resources):
            progress_emit(
                "convert-assets",
                f"bundle {resource_index + 1}/{len(resolved.resources)}: "
                f"{resource.rule.id}",
                unit_delta=1,
            )
            bundle_outputs: list[Path] | None = None
            bundle_error: str | None = None
            if (
                resource.rule.converter
                in {
                    "w3d-bundle",
                    "w3d-hierarchical",
                    "w3d-static",
                }
                and resource.entries
            ):
                if resource_index in w3d_errors:
                    exc = w3d_errors[resource_index]
                    bundle_error = str(exc)
                    incomplete.append(
                        {"resource": resource.rule.id, "reason": bundle_error}
                    )
                    if resource.rule.required and not allow_incomplete:
                        raise exc
                    bundle_outputs = []
                elif resource_index not in w3d_outputs:
                    # Mirror the media lane: a required W3D job must never
                    # vanish between scheduling and collection without a
                    # recorded error (prior menofdale misclassification).
                    reason = "W3D conversion job was not scheduled"
                    incomplete.append(
                        {"resource": resource.rule.id, "reason": reason}
                    )
                    if resource.rule.required and not allow_incomplete:
                        raise RuntimeError(reason)
                    bundle_outputs = []
                else:
                    bundle_outputs = w3d_outputs[resource_index]
            elif (
                resource.rule.converter == "sage-terrain-materials" and resource.entries
            ):
                try:
                    bundle_outputs = self._convert_terrain_material_bundle(
                        resource,
                        extracted,
                        resource.rule.output,
                        resource.rule.options,
                        staging,
                    )
                except (FileNotFoundError, RuntimeError, ValueError) as exc:
                    bundle_error = str(exc)
                    incomplete.append(
                        {"resource": resource.rule.id, "reason": bundle_error}
                    )
                    if resource.rule.required and not allow_incomplete:
                        raise
                    bundle_outputs = []
            elif resource.rule.converter == "texture-atlas-crops" and resource.entries:
                try:
                    if resource.count_error is not None or len(resource.entries) != 1:
                        raise ValueError(
                            "texture-atlas-crops requires exactly one resolved source"
                        )
                    entry = resource.entries[0]
                    cached = extracted.get(
                        (entry.archive.casefold(), entry.name.casefold())
                    )
                    if cached is None:
                        raise RuntimeError(
                            f"texture atlas input resource was not extracted: {entry.name}"
                        )
                    bundle_outputs = self._convert_texture_atlas_crops(
                        Path(cached["source_path"]),
                        resource.rule.output,
                        resource.rule.options,
                        staging,
                    )
                except (FileNotFoundError, RuntimeError, ValueError) as exc:
                    bundle_error = str(exc)
                    incomplete.append(
                        {"resource": resource.rule.id, "reason": bundle_error}
                    )
                    if resource.rule.required and not allow_incomplete:
                        raise
                    bundle_outputs = []
            elif resource.rule.converter == "sage-apt-runtime" and resource.entries:
                try:
                    bundle_outputs = self._convert_hud_apt_runtime_bundle(
                        resource,
                        extracted,
                        resource.rule.output,
                        resource.rule.options,
                        staging,
                    )
                except (FileNotFoundError, RuntimeError, ValueError) as exc:
                    bundle_error = str(exc)
                    incomplete.append(
                        {"resource": resource.rule.id, "reason": bundle_error}
                    )
                    if resource.rule.required and not allow_incomplete:
                        raise
                    bundle_outputs = []
            elif (
                resource.rule.converter == "sage-apt-shell-runtime"
                and resource.entries
            ):
                try:
                    bundle_outputs = self._convert_shell_apt_runtime_bundle(
                        resource,
                        extracted,
                        resource.rule.output,
                        resource.rule.options,
                        staging,
                    )
                except (FileNotFoundError, RuntimeError, ValueError) as exc:
                    bundle_error = str(exc)
                    incomplete.append(
                        {"resource": resource.rule.id, "reason": bundle_error}
                    )
                    if resource.rule.required and not allow_incomplete:
                        raise
                    bundle_outputs = []
            elif (
                resource.rule.converter == "sage-apt-screen-runtime"
                and resource.entries
            ):
                try:
                    bundle_outputs = self._convert_screen_apt_runtime_bundle(
                        resource,
                        extracted,
                        resource.rule.output,
                        resource.rule.options,
                        staging,
                    )
                except (FileNotFoundError, RuntimeError, ValueError) as exc:
                    bundle_error = str(exc)
                    incomplete.append(
                        {"resource": resource.rule.id, "reason": bundle_error}
                    )
                    if resource.rule.required and not allow_incomplete:
                        raise
                    bundle_outputs = []
            elif resource.rule.converter == "retail-unit-rules" and resource.entries:
                try:
                    bundle_outputs = self._convert_retail_unit_rules_bundle(
                        resource,
                        extracted,
                        resource.rule.output,
                        resource.rule.options,
                        staging,
                    )
                except (FileNotFoundError, RuntimeError, ValueError) as exc:
                    bundle_error = str(exc)
                    incomplete.append(
                        {"resource": resource.rule.id, "reason": bundle_error}
                    )
                    if resource.rule.required and not allow_incomplete:
                        raise
                    bundle_outputs = []
            elif resource.rule.converter == "living-world" and resource.entries:
                try:
                    bundle_outputs = self._convert_living_world_bundle(
                        resource,
                        extracted,
                        resource.rule.output,
                        resource.rule.options,
                        staging,
                    )
                except (FileNotFoundError, RuntimeError, ValueError) as exc:
                    bundle_error = str(exc)
                    incomplete.append(
                        {"resource": resource.rule.id, "reason": bundle_error}
                    )
                    if resource.rule.required and not allow_incomplete:
                        raise
                    bundle_outputs = []
            elif (
                resource.rule.converter == "sage-script-composite"
                and resource.entries
            ):
                try:
                    bundle_outputs = self._convert_script_composite_bundle(
                        resource,
                        extracted,
                        resource.rule.output,
                        resource.rule.options,
                        staging,
                    )
                except (FileNotFoundError, RuntimeError, ValueError) as exc:
                    bundle_error = str(exc)
                    incomplete.append(
                        {"resource": resource.rule.id, "reason": bundle_error}
                    )
                    if resource.rule.required and not allow_incomplete:
                        raise
                    bundle_outputs = []

            for index, entry in enumerate(resource.entries):
                cache = extracted[(entry.archive.casefold(), entry.name.casefold())]
                source_path: Path = cache["source_path"]
                if resource.rule.converter in RESOURCE_BUNDLE_CONVERTERS:
                    # Bundle conversions consume an exact multi-source closure,
                    # but their cooked files are declared only once. Repeating
                    # outputs per source obscures collisions and inflates audits.
                    output_paths = (bundle_outputs or []) if index == 0 else []
                elif resource.rule.converter in {"audio", "texture", "texture-crop"}:
                    media_key = (resource_index, index)
                    if media_key in media_errors:
                        exc = media_errors[media_key]
                        incomplete.append(
                            {"resource": resource.rule.id, "reason": str(exc)}
                        )
                        if resource.rule.required and not allow_incomplete:
                            raise exc
                        output_paths = []
                    elif media_key not in media_outputs:
                        # Must not silently emit empty outputs for a missed job.
                        reason = "media conversion job was not scheduled"
                        incomplete.append(
                            {"resource": resource.rule.id, "reason": reason}
                        )
                        if resource.rule.required and not allow_incomplete:
                            raise RuntimeError(reason)
                        output_paths = []
                    else:
                        output_paths = media_outputs[media_key]
                else:
                    try:
                        output_paths = self._convert_resource(
                            source_path,
                            resource.rule.converter,
                            resource.rule.output,
                            resource.rule.options,
                            staging,
                            index=index,
                            source_sha256=str(cache.get("source_sha256") or "") or None,
                            source_virtual_path=entry.name,
                        )
                    except (FileNotFoundError, RuntimeError, ValueError) as exc:
                        incomplete.append(
                            {"resource": resource.rule.id, "reason": str(exc)}
                        )
                        if resource.rule.required and not allow_incomplete:
                            raise
                        output_paths = []
                # Digests are filled in by one parallel pass below: hashing
                # every converted output inline made this loop a long,
                # silent, single-threaded tail of the convert stage.
                provenance_entries.append(
                    {
                        "resource_id": resource.rule.id,
                        "kind": resource.rule.kind,
                        "converter": resource.rule.converter,
                        "source": {
                            "archive": entry.archive,
                            "virtual_path": entry.name,
                            "offset": entry.offset,
                            "size": entry.size,
                            "sha256": cache["source_sha256"],
                            "cache_key": cache["cache_key"],
                        },
                        "outputs": list(output_paths),
                    }
                )

        progress_emit(
            "convert-assets",
            f"hashing {sum(len(item['outputs']) for item in provenance_entries)} "
            "converted outputs",
        )
        output_digests = _hash_files(
            sorted({path for item in provenance_entries for path in item["outputs"]})
        )
        run.end_phase(
            convert_phase,
            converted_entries=len(provenance_entries),
            output_files=len(output_digests),
            incomplete_entries=len(incomplete),
        )
        cook_phase = run.begin_phase(
            "cook", pack_id=resolved.profile.pack_id, staging=str(staging)
        )
        for item in provenance_entries:
            item["outputs"] = [
                {
                    "path": path.relative_to(staging).as_posix(),
                    "size": path.stat().st_size,
                    "sha256": output_digests[path],
                }
                for path in item["outputs"]
            ]

        self._write_runtime_data(staging, resolved.profile.runtime_data)
        pack_manifest = {
            "id": resolved.profile.pack_id,
            "version": resolved.profile.pack_version,
            "priority": 100,
            "title": resolved.profile.title,
            "local_retail_import": True,
            "redistributable": False,
            "profile_build_complete": not incomplete,
            "vertical_slice_complete": False,
            "provenance_contract": RETAIL_PROVENANCE_CONTRACT,
        }
        pack_manifest.update(resolved.profile.pack_metadata)
        pack_manifest["id"] = resolved.profile.pack_id
        pack_manifest["version"] = resolved.profile.pack_version
        # Profiles may supply presentation metadata, but cannot downgrade the
        # local/private security boundary computed by the importer.
        pack_manifest["local_retail_import"] = True
        pack_manifest["redistributable"] = False
        pack_manifest["profile_build_complete"] = not incomplete
        pack_manifest["provenance_contract"] = RETAIL_PROVENANCE_CONTRACT
        pack_manifest["vertical_slice_complete"] = bool(
            resolved.profile.pack_metadata.get("vertical_slice_complete", False)
        )
        write_json_atomic(staging / "pack.json", pack_manifest)
        tools = self._canonical_tool_report()
        provenance = {
            "format": 1,
            "contract": RETAIL_PROVENANCE_CONTRACT,
            "importer_version": __version__,
            "profile": resolved.profile.id,
            "profile_sha256": resolved.profile.source_sha256,
            "importer_recipe": _importer_recipe_report(),
            "source_game": f"{self.game.id}-retail-user-owned",
            "source_archives": source_archives,
            "redistributable": False,
            "tools": tools,
            "incomplete": incomplete,
            "entries": provenance_entries,
            "incrementalRebuild": rebuild_execution_provenance(
                reconvert_only=getattr(self, "reconvert_only", ()),
                single_build=bool(getattr(self, "single_build", False)),
            ),
        }
        provenance["bundle_files"] = _canonical_pack_inventory(
            staging,
            {
                output["path"]: output["sha256"]
                for item in provenance_entries
                for output in item["outputs"]
            },
        )
        write_json_atomic(staging / "provenance" / "manifest.json", provenance)
        # On-disk audit.json is always full (canonical). Dev light audit is only
        # an optional outer CLI speed path and must not claim hash validity here.
        audit = audit_pack(staging, light=False)
        write_json_atomic(staging / "provenance" / "audit.json", audit)
        # The tool report is the record of which ffmpeg/Pillow actually cooked
        # these bytes. It goes into the run log verbatim so a bug report can be
        # answered without re-opening the pack.
        run.event(
            "tool-report",
            pack_id=resolved.profile.pack_id,
            tools=tools,
            incomplete_entries=len(incomplete),
        )
        if not audit["valid"]:
            run.end_phase(cook_phase, outcome="failed", audit_valid=False)
            run.refusal(
                "cook",
                reason="built pack failed its internal hash audit",
                pack_id=resolved.profile.pack_id,
                audit=audit.get("errors") or audit.get("issues"),
            )
            raise RuntimeError("built pack failed its internal hash audit")
        # The audit immediately above re-hashed every inventory file against
        # `provenance["bundle_files"]` and passed, so those digests describe
        # THESE bytes, verified in THIS run. Folding the pack address out of
        # them - plus the two provenance documents the inventory deliberately
        # excludes, hashed right here - is byte-identical to walking and
        # re-reading the whole tree a third time, which is what the CLI used to
        # do immediately after this returns. Nothing writes into `staging`
        # between `audit.json` and the rename below, and a rename does not
        # touch bytes, so the recorded digest names the finished pack.
        provenance_rows = [
            {
                "path": relative,
                "size": (staging / relative).stat().st_size,
                "sha256": sha256_file(staging / relative),
            }
            for relative in ("provenance/audit.json", "provenance/manifest.json")
        ]
        built_bundle_digest = _fold_bundle_digest(
            (row["path"], row["size"], row["sha256"])
            for row in sorted(
                [*(provenance.get("bundle_files") or []), *provenance_rows],
                key=lambda item: str(item["path"]),
            )
        )
        run.end_phase(
            cook_phase,
            audit_valid=True,
            bundle_files=len(provenance.get("bundle_files") or []),
            bundle_sha256=built_bundle_digest,
        )

        if pack_root.exists():
            previous = self.packs_root / (resolved.profile.pack_id + ".previous")
            if previous.exists():
                shutil.rmtree(previous)
            os.replace(pack_root, previous)
            try:
                os.replace(staging, pack_root)
            except BaseException:
                os.replace(previous, pack_root)
                raise
            shutil.rmtree(previous)
        else:
            os.replace(staging, pack_root)
        self._last_build_verification = {
            "pack_root": str(pack_root.resolve()),
            "bundle_sha256": built_bundle_digest,
            "audit": audit,
        }
        run.event(
            "build.end",
            pack_id=resolved.profile.pack_id,
            pack_root=str(pack_root),
            incomplete_entries=len(incomplete),
            bundle_sha256=built_bundle_digest,
        )
        return pack_root

    def _attest_source_archives(
        self, resolved: ResolvedProfile
    ) -> list[dict[str, Any]]:
        selected = sorted(
            {entry.archive for entry in resolved.selected_entries}, key=str.casefold
        )
        # Attestation reads whole retail archives (5 GB of BIGs for a full
        # install). Hash them concurrently - this is pure read bandwidth on
        # the user's install drive - and memoize per (path, size, mtime) so a
        # multi-faction cook in one process attests each archive once.
        paths_by_relative: dict[str, Path] = {}
        virtual_attestations: dict[str, tuple[int, str]] = {}
        archive_rows = {
            archive.relative_path.casefold(): archive
            for archive in self.catalog.archives
        }
        catalog_archive_sha256 = getattr(self.catalog, "archive_sha256", None)
        for relative in selected:
            path = self.catalog.install_root / Path(relative)
            if path.is_file():
                paths_by_relative[relative] = path
                continue
            archive_row = archive_rows.get(relative.casefold())
            if archive_row is None or not callable(catalog_archive_sha256):
                # Preserve the ordinary physical-archive error with its exact
                # missing path when no authenticated virtual archive exists.
                path.stat()
            digest = catalog_archive_sha256(relative)
            if not isinstance(digest, str) or len(digest) != 64:
                raise RuntimeError(
                    f"catalog virtual archive digest is invalid: {relative}"
                )
            virtual_attestations[relative.casefold()] = (archive_row.size, digest)

        paths = list(paths_by_relative.values())
        stats = {path: path.stat() for path in paths}
        pending = [
            path
            for path in paths
            if (str(path).casefold(), stats[path].st_size, stats[path].st_mtime_ns)
            not in self._archive_attest_cache
        ]
        for path, digest in _hash_files(pending).items():
            self._archive_attest_cache[
                (str(path).casefold(), stats[path].st_size, stats[path].st_mtime_ns)
            ] = digest

        reports: list[dict[str, Any]] = []
        for relative in selected:
            virtual = virtual_attestations.get(relative.casefold())
            if virtual is not None:
                archive_size, actual = virtual
            else:
                archive_path = paths_by_relative[relative]
                stat = stats[archive_path]
                archive_size = stat.st_size
                actual = self._archive_attest_cache[
                    (str(archive_path).casefold(), stat.st_size, stat.st_mtime_ns)
                ]
            expected = KNOWN_SLICE_ARCHIVE_SHA256.get(relative.casefold())
            if resolved.profile.id == "men-fords-v0":
                if expected is None:
                    raise RuntimeError(
                        f"retail slice archive has no trusted fingerprint: {relative}"
                    )
                if actual.casefold() != expected.casefold():
                    raise RuntimeError(
                        f"retail slice archive differs from the attested BFME II 1.06 input: {relative}"
                    )
            report: dict[str, Any] = {
                "relative_path": relative,
                "size": archive_size,
                "sha256": actual,
            }
            if expected is not None:
                report["expected_sha256"] = expected
                report["matches_reference"] = actual.casefold() == expected.casefold()
            reports.append(report)
        return reports

    def _verify_required_tools(self, resolved: ResolvedProfile) -> None:
        converters = {resource.rule.converter for resource in resolved.resources}
        required_checks = {"python", "python_runtime"}
        if converters & {
            "texture",
            "texture-crop",
            "texture-atlas-crops",
            "sage-terrain-materials",
        }:
            required_checks.update({"pillow", "pillow_tree"})
        if "audio" in converters:
            required_checks.update({"ffmpeg", "ffprobe"})
        w3d_required = bool(
            converters
            & {
            "w3d-model",
            "w3d-animation",
            "w3d-bundle",
            "w3d-hierarchical",
            "w3d-static",
            }
        )
        if w3d_required:
            required_checks.add("blender")
        if (
            required_checks == {"python", "python_runtime"}
            and resolved.profile.id != "men-fords-v0"
        ):
            return
        from .bootstrap import tool_status

        status = tool_status(
            self.state_root, skip_w3d_attestation=w3d_required
        )
        missing = sorted(
            name
            for name in required_checks
            if not status.get("checks", {}).get(name, False)
        )
        if missing:
            raise RuntimeError(
                "required pinned conversion tools are not ready: " + ", ".join(missing)
            )
        self._blender_tree_verified = bool(
            not w3d_required
            and status.get("checks", {}).get("blender_tree", False)
        )
        self._python_runtime_report = dict(status.get("python_runtime", {}))

    def publish_to_godot(
        self,
        pack_root: Path | str,
        content_root: Path | str,
        *,
        allow_incomplete: bool = False,
        select: bool = True,
        verified_digest: str | None = None,
    ) -> dict[str, str]:
        """Publish a built pack into the Godot content root.

        *verified_digest* is an optimisation for callers that have just run a
        full (non-light) ``audit_pack`` and ``bundle_digest`` over this exact
        pack in this process: pass the digest and publication skips repeating
        both passes, which on a 3.9 GB pack costs ~41 s of pure duplicate
        work. Anything else re-verifies from scratch. The digest is still
        re-derived from the bytes written to the destination, so a stale or
        wrong value fails the copy check rather than publishing bad content.

        EVERY publish holds the content root's selection-transaction lock for
        its whole write lifetime. This is the producer side the cook guard was
        missing: without it ``.cook-active`` was a breadcrumb nobody dropped,
        and a selection transaction could swap the document while bundles were
        still landing underneath it. Every publish route in this codebase
        (publish-faction-to-slice, publish-music-pack, playable-unit import)
        reaches the content root through here, so one lock covers them all.

        TODO(agent): ``playable_unit_import.py:787`` calls this with the default
        ``select=True`` and is OUTSIDE this packet's allowed paths. Hardening
        here makes that call safe rather than silent - it now writes the
        complete document through the locked transaction, or refuses loudly when
        a durable mirror exists - but the call site should be changed to
        ``select=False`` plus an explicit activation step by whoever owns it.
        """

        locked_root = ensure_external_to_repo(
            Path(content_root), repo_root_from_module()
        )
        locked_root.mkdir(parents=True, exist_ok=True)
        # Q86: publish locks ONLY its own content root. The durable cache is no
        # longer a mirror publish must keep in sync — the game loader fails
        # closed on a broken workspace instead of falling back to durable, so
        # workspace activation and the install cache are independent documents.
        lock_roots: list[Path] = [locked_root]
        with selection_transaction_locks(lock_roots):
            return self._publish_to_godot_locked(
                pack_root,
                locked_root,
                allow_incomplete=allow_incomplete,
                select=select,
                verified_digest=verified_digest,
            )

    def _publish_to_godot_locked(
        self,
        pack_root: Path | str,
        content_root: Path,
        *,
        allow_incomplete: bool = False,
        select: bool = True,
        verified_digest: str | None = None,
    ) -> dict[str, str]:
        from .diagnostics import active_run

        run = active_run()
        publish_phase = run.begin_phase(
            "publish",
            source_pack=str(pack_root),
            content_root=str(content_root),
            select=bool(select),
            allow_incomplete=bool(allow_incomplete),
            digest_pre_verified=verified_digest is not None,
        )
        try:
            result = self._publish_to_godot_body(
                pack_root,
                content_root,
                allow_incomplete=allow_incomplete,
                select=select,
                verified_digest=verified_digest,
                run=run,
            )
        except BaseException as exc:
            run.end_phase(publish_phase, outcome="failed", exc=exc)
            raise
        run.end_phase(
            publish_phase,
            pack_id=result.get("pack_id"),
            bundle_sha256=result.get("bundle_sha256"),
            active_pack=result.get("active_pack"),
            selection_written="selection" in result,
        )
        # Re-read identity from the root we just wrote so identity.json names
        # the selection this run produced, not the one it started with.
        run.record_identity(Path(content_root))
        return result

    def _publish_to_godot_body(
        self,
        pack_root: Path | str,
        content_root: Path,
        *,
        allow_incomplete: bool = False,
        select: bool = True,
        verified_digest: str | None = None,
        run: Any = None,
    ) -> dict[str, str]:
        if run is None:
            from .diagnostics import active_run

            run = active_run()
        source = Path(pack_root).expanduser().resolve()
        pack_data = read_json(source / "pack.json")
        pack_id = str(pack_data.get("id", ""))
        if not pack_id or any(
            character not in "abcdefghijklmnopqrstuvwxyz0123456789._-"
            for character in pack_id
        ):
            raise ValueError(f"built pack has an unsafe id: {pack_id!r}")
        if not bool(pack_data.get("profile_build_complete", False)):
            if not allow_incomplete:
                raise RuntimeError(
                    "incomplete retail packs cannot be published or selected"
                )
            # Dev/allow-incomplete path: still select so the vertical slice can
            # load converted playableUnit/Structure registries while residual
            # W3D gaps are fixed. Canonical release builds must stay complete.
        # Publication always full-hash audits regardless of OPENBFME_DEV /
        # light, unless the caller just did exactly that and handed us the
        # resulting digest.
        if verified_digest is None:
            # One read of the source serves the audit AND the address: the
            # digest map below is what the bundle digest was folded from.
            source_digest, source_file_digests = _bundle_digest_with_files(source)
            source_audit = audit_pack(
                source, light=False, known_digests=source_file_digests
            )
            if not source_audit["valid"]:
                raise RuntimeError(
                    "source pack failed canonical audit before publication: "
                    + "; ".join(source_audit["errors"][:5])
                )
        else:
            source_digest = verified_digest
        root = ensure_external_to_repo(Path(content_root), repo_root_from_module())
        digest = source_digest
        relative = Path(pack_id) / digest
        destination = (root / relative).resolve()
        try:
            destination.relative_to(root.resolve())
        except ValueError as exc:
            raise ValueError("published pack escaped the Godot content root") from exc
        if destination.exists() and not destination.is_dir():
            raise RuntimeError(
                f"published bundle path is not a directory: {destination}"
            )
        run.decision(
            "published-bundle",
            chosen=str(destination),
            reason=(
                "digest handed over by the caller's audit"
                if verified_digest
                else "digest recomputed from the source pack"
            ),
            pack_id=pack_id,
            bundle_sha256=digest,
            content_root=str(root),
            destination_exists=destination.is_dir(),
        )
        if destination.is_dir():
            # Reusing an address that already exists is only safe because the
            # bytes are re-hashed here. Record the reuse: an unverified reuse is
            # how a pack whose name is a lie stays loaded (AGENTS.md rule 1).
            observed_digest, observed_file_digests = _bundle_digest_with_files(
                destination
            )
            if (
                observed_digest != digest
                or not audit_pack(
                    destination, light=False, known_digests=observed_file_digests
                )["valid"]
            ):
                run.refusal(
                    "publish",
                    reason="pre-existing published bundle is corrupt or tampered",
                    destination=str(destination),
                    expected_sha256=digest,
                )
                raise RuntimeError(
                    f"pre-existing published bundle is corrupt or tampered: {destination}"
                )
            run.event(
                "publish.reuse",
                destination=str(destination),
                bundle_sha256=digest,
                reason="existing bundle re-hashed and matched; no copy needed",
            )
        else:
            staging = destination.with_name(destination.name + ".building")
            if staging.exists():
                shutil.rmtree(staging)
            staging.parent.mkdir(parents=True, exist_ok=True)
            # Copy and digest in a single read of every file. The digest is
            # computed from the bytes actually written to the destination, so
            # this is a strictly stronger check than the previous
            # copytree + re-walk + re-hash, at one pass instead of three.
            try:
                copied_digest = _copy_tree_with_digest(source, staging)
            except OSError as exc:
                shutil.rmtree(staging, ignore_errors=True)
                raise RuntimeError(
                    f"publishing the pack to {staging} failed while copying: {exc}. "
                    "Check free disk space and that no other process holds the "
                    "Godot content root open, then re-run."
                ) from exc
            if copied_digest != digest:
                shutil.rmtree(staging, ignore_errors=True)
                raise RuntimeError(
                    "published staging copy failed its bundle hash check "
                    f"(source {digest}, copy {copied_digest}); the copy did not "
                    "reproduce the source pack byte-for-byte"
                )
            os.replace(staging, destination)
        result: dict[str, str] = {
            "bundle_sha256": digest,
            "published_pack": str(destination),
            "pack_id": pack_id,
            "pack_relative": relative.as_posix(),
        }
        if not select:
            # Live playtests read selection.json while republished bundles
            # land; leaving it untouched keeps a running slice loadable.
            return result
        # ACTIVATION THROUGH THE ONE SELECTION WRITER. The complete document
        # goes through the same staged, verified, rollback-backed transaction
        # every other selection change uses.
        #
        # Q86: no durable-mirror probe any more. The game loader fails closed
        # on a broken workspace instead of substituting the durable cache, so
        # a workspace activation cannot strand an env-less launch on stale
        # bytes; the install cache moves only through an explicit
        # apply-selection-transaction --durable-root.
        supplements: list[str] = []
        # Preserve supplemental packs (map overlays, ranger contracts, etc.) so
        # a faction republish does not silently drop the rest of the slice stack.
        selection_path = root / "selection.json"
        if selection_path.is_file():
            try:
                prior = read_json(selection_path)
            except (OSError, ValueError, TypeError, KeyError) as exc:
                raise RuntimeError(
                    "--select refuses to overwrite malformed selection.json; "
                    "repair or remove it explicitly before publishing"
                ) from exc
            if (
                not isinstance(prior, dict)
                or prior.get("schema") != "openbfme.pack-selection"
                or prior.get("schemaVersion") != 0
                or not isinstance(prior.get("activePack"), str)
            ):
                raise RuntimeError(
                    "--select refuses to overwrite invalid selection.json schema"
                )
            prior_active = str(prior.get("activePack", "")).strip().replace("\\", "/")
            if prior_active and prior_active != relative.as_posix():
                raise RuntimeError(
                    "--select refuses to replace a different activePack; publish "
                    "without --select and use update-selection-entry for supplements"
                )
            prior_supplements = prior.get("supplementalPacks")
            if isinstance(prior_supplements, list):
                kept: list[str] = []
                seen: set[str] = set()
                for raw in prior_supplements:
                    entry = str(raw).strip().replace("\\", "/")
                    if not entry or entry in seen:
                        continue
                    # Never re-attach the pack we just published as a supplement.
                    if entry == relative.as_posix() or entry.startswith(f"{pack_id}/"):
                        continue
                    if not (root / entry).is_dir():
                        continue
                    seen.add(entry)
                    kept.append(entry)
                supplements = kept
        # Already inside publish_to_godot's lock on this root, so the locked
        # internals are called directly - re-acquiring would deadlock against
        # ourselves.
        transaction = _apply_selection_transaction_locked(
            root,
            None,
            [("workspace", root)],
            relative.as_posix(),
            supplements,
            None,
        )
        result["selection"] = str(selection_path)
        result["active_pack"] = relative.as_posix()
        result["selection_transaction"] = transaction
        return result

    def _log_pillow_choice_once(self, pil_module: Any) -> None:
        """Record which Pillow install is cooking textures, once per pipeline."""

        # Unsynchronised fast path: this runs once per converted texture, and
        # taking the conversion lock per file to answer an already-settled
        # question is a real cost on the default (diagnostics-off) path. The
        # race is benign — worst case two identical lines name the same Pillow.
        if getattr(self, "_pillow_choice_logged", False):
            return
        self._pillow_choice_logged = True
        from .diagnostics import active_run

        active_run().decision(
            "attested-tool",
            chosen=getattr(pil_module, "__file__", None),
            reason="Pillow import satisfied the pinned 12.2.0 requirement",
            tool="pillow",
            version=pil_module.__version__,
            interpreter=sys.executable,
        )

    def _canonical_tool_report(self) -> dict[str, Any]:
        report: dict[str, Any] = {}
        ffmpeg = discover_executable("ffmpeg", "OPENBFME_FFMPEG")
        if ffmpeg:
            inspected = inspect_tool("ffmpeg", "OPENBFME_FFMPEG")
            report["ffmpeg"] = {
                "version": inspected.version,
                "sha256": sha256_file(ffmpeg),
                "ffprobe_sha256": sha256_file(ffmpeg.with_name("ffprobe.exe")),
            }
        if not self._python_runtime_report:
            from .bootstrap import python_runtime_attestation

            self._python_runtime_report = python_runtime_attestation()
        report["python"] = dict(self._python_runtime_report)
        try:
            import PIL
            from .bootstrap import PILLOW_TREE_SHA256

            report["pillow"] = {
                "version": PIL.__version__,
                "tree_sha256": PILLOW_TREE_SHA256,
            }
        except ImportError:
            pass
        blender = (
            Path(
                os.environ.get(
                    "OPENBFME_BLENDER",
                    str(
                        self.state_root
                        / "tools"
                        / "blender-4.2.0-windows-x64"
                        / "blender.exe"
                    ),
                )
            )
            .expanduser()
            .resolve()
        )
        if blender.is_file():
            from .bootstrap import BLENDER_TREE_SHA256

            report["blender"] = {
                "version": "4.2.0",
                "sha256": sha256_file(blender),
                "tree_sha256": BLENDER_TREE_SHA256,
            }
        plugin = (
            Path(
                os.environ.get(
                    "OPENBFME_W3D_PLUGIN",
                    str(self.state_root / "tools" / "OpenSAGE.BlenderPlugin"),
                )
            )
            .expanduser()
            .resolve()
        )
        final_attestation = getattr(self, "_w3d_final_attestation", None)
        # Only tools that actually participated belong in provenance. A
        # media-only pack has no W3D final attestation; probing the available
        # plugin here both overstates the recipe and races unrelated Blender
        # work that may be materialising transient __pycache__ files. Actual
        # W3D batches still receive the strict begin/end attestation above.
        if final_attestation is not None:
            final_plugin = final_attestation.get("plugin", {})
            value = final_plugin.get("commit")
            submodule_value = final_plugin.get("submodule_commit")
        else:
            value = None
            submodule_value = None
        if value:
            report["opensage_w3d_plugin"] = {
                "commit": value,
                "submodule_commit": submodule_value,
                "worktree_clean": bool(final_attestation.get("plugin_worktree_clean", False)),
                "python_bytecode_free": True,
            }
        return report

    def _convert_resource(
        self,
        source: Path,
        converter: str,
        output: str | None,
        options: dict[str, Any],
        pack_root: Path,
        *,
        index: int,
        source_sha256: str | None = None,
        source_virtual_path: str | None = None,
    ) -> list[Path]:
        relative_output = output or f"source/{source.name}"
        relative_output = _render_output_template(
            relative_output,
            index=index,
            stem=source.stem.casefold(),
            name=source.name.casefold(),
        )
        target = _safe_output(pack_root, relative_output)
        target.parent.mkdir(parents=True, exist_ok=True)
        match converter:
            case "hash-only":
                return []
            case "sage-map":
                return self._convert_sage_map(
                    source,
                    target,
                    options,
                    source_sha256=source_sha256,
                    source_virtual_path=source_virtual_path,
                )
            case "sage-particle-definition":
                return self._convert_sage_particle_definition(source, target, options)
            case "sage-scripts":
                return self._convert_sage_scripts(source, target, options)
            case "copy" | "text" | "map":
                shutil.copyfile(source, target)
                return [target]
            case "texture" | "texture-crop" | "audio":
                return self._convert_cached_media(
                    source,
                    converter,
                    options,
                    target,
                    relative_output=relative_output,
                    source_sha256=source_sha256,
                )
            case "w3d-model" | "w3d-animation":
                executable = os.environ.get("OPENBFME_W3D_CONVERTER", "").strip()
                if not executable or not Path(executable).is_file():
                    raise FileNotFoundError(
                        "W3D converter unavailable; set OPENBFME_W3D_CONVERTER"
                    )
                mode = "model" if converter == "w3d-model" else "animation"
                run_checked(
                    [
                        executable,
                        "convert",
                        "--mode",
                        mode,
                        "--input",
                        str(source),
                        "--output",
                        str(target),
                    ]
                )
                if not target.is_file():
                    raise RuntimeError(f"W3D converter did not create {target}")
                return [target]
            case _:
                raise ValueError(f"unsupported converter: {converter}")

    def _png_compress_level(self) -> int:
        """PNG zlib level. Default 9 (shipping) or 6 in OPENBFME_DEV.

        Tool token includes the level, so media DDC never mixes level-6 and level-9.
        """

        default = "6" if getattr(self, "dev_mode", False) else "9"
        raw = os.environ.get("OPENBFME_PNG_LEVEL", default).strip()
        try:
            level = int(raw)
        except ValueError as exc:
            raise ValueError(
                f"OPENBFME_PNG_LEVEL must be an integer 0-9, got {raw!r}"
            ) from exc
        if level < 0 or level > 9:
            raise ValueError(f"OPENBFME_PNG_LEVEL must be 0-9, got {level}")
        return level

    def _convert_media_jobs(
        self,
        media_jobs: list[
            tuple[int, int, Path, str, str | None, dict[str, Any], Path, str]
        ],
        prior_errors: dict[tuple[int, int], Exception] | None = None,
        *,
        progress_stage: str | None = "media",
    ) -> tuple[dict[tuple[int, int], list[Path]], dict[tuple[int, int], Exception]]:
        """Convert audio/texture jobs in parallel; returns outputs + errors."""

        from .progress import emit as progress_emit

        outputs: dict[tuple[int, int], list[Path]] = {}
        errors: dict[tuple[int, int], Exception] = dict(prior_errors or {})
        if not media_jobs:
            return outputs, errors
        stage = "" if progress_stage is None else progress_stage
        if stage:
            progress_emit(
                stage,
                f"converting {len(media_jobs)} audio/texture files "
                f"({self.conversion_jobs} workers)",
                total_units=len(media_jobs),
            )

        def _run_media(
            job: tuple[int, int, Path, str, str | None, dict[str, Any], Path, str],
        ) -> list[Path]:
            (
                _resource_index,
                entry_index,
                source_path,
                converter,
                output,
                options,
                pack_root,
                source_sha,
            ) = job
            return self._convert_resource(
                source_path,
                converter,
                output,
                options,
                pack_root,
                index=entry_index,
                source_sha256=source_sha or None,
            )

        workers = min(self.conversion_jobs, len(media_jobs))
        with ThreadPoolExecutor(max_workers=workers) as pool:
            futures = {
                pool.submit(_run_media, job): (job[0], job[1]) for job in media_jobs
            }
            for future in as_completed(futures):
                key = futures[future]
                try:
                    outputs[key] = future.result()
                    progress_emit(
                        stage,
                        f"media done ({len(outputs) + len(errors)}/{len(media_jobs)})",
                        unit_delta=1,
                    )
                except (FileNotFoundError, RuntimeError, ValueError, OSError) as exc:
                    errors[key] = exc
                    progress_emit(
                        stage,
                        f"media failed ({len(outputs) + len(errors)}/{len(media_jobs)})",
                        unit_delta=1,
                    )
        return outputs, errors

    def _convert_cached_media(
        self,
        source: Path,
        converter: str,
        options: dict[str, Any],
        target: Path,
        *,
        relative_output: str,
        source_sha256: str | None = None,
    ) -> list[Path]:
        """Convert audio/texture with content-addressed cache + tool attest once."""

        png_level = self._png_compress_level()
        if converter == "audio":
            from .bootstrap import FFMPEG_EXE_SHA256

            ffmpeg = discover_executable("ffmpeg", "OPENBFME_FFMPEG")
            if not ffmpeg:
                raise FileNotFoundError(
                    "ffmpeg is required; set OPENBFME_FFMPEG to its executable"
                )
            resolved_ffmpeg = str(Path(ffmpeg).resolve())
            with self._conversion_cache_lock:
                if self._ffmpeg_attested_path != resolved_ffmpeg:
                    # Logged once per distinct executable, not once per file:
                    # this runs on every audio job and the run log has to stay
                    # readable. The pin failure below is the one this project
                    # actually hit, so it is recorded before it is raised.
                    from .diagnostics import active_run

                    actual = sha256_file(ffmpeg).casefold()
                    if actual != FFMPEG_EXE_SHA256:
                        active_run().refusal(
                            "audio-conversion",
                            reason="ffmpeg does not match the pinned 8.1.1 hash",
                            ffmpeg=resolved_ffmpeg,
                            expected_sha256=FFMPEG_EXE_SHA256,
                            actual_sha256=actual,
                        )
                        raise RuntimeError(
                            "FFmpeg executable does not match the pinned 8.1.1 hash"
                        )
                    active_run().decision(
                        "attested-tool",
                        chosen=resolved_ffmpeg,
                        reason="sha256 matches the pinned FFmpeg 8.1.1",
                        tool="ffmpeg",
                        sha256=actual,
                    )
                    self._ffmpeg_attested_path = resolved_ffmpeg
            tool_token = f"ffmpeg:{FFMPEG_EXE_SHA256}"
        else:
            try:
                import PIL
            except ImportError as exc:
                raise FileNotFoundError(
                    "Pillow is required for deterministic DDS/TGA conversion"
                ) from exc
            if PIL.__version__ != "12.2.0":
                from .diagnostics import active_run

                # `bare pytest picks up the wrong Pillow` is a recorded incident;
                # naming the module file makes the wrong-interpreter case obvious.
                active_run().refusal(
                    "texture-conversion",
                    reason="Pillow version is not the pinned 12.2.0",
                    found_version=PIL.__version__,
                    pillow_path=getattr(PIL, "__file__", None),
                    interpreter=sys.executable,
                )
                raise RuntimeError(
                    "Pillow 12.2.0 is required for deterministic texture output; "
                    f"found {PIL.__version__}"
                )
            self._log_pillow_choice_once(PIL)
            tool_token = f"pillow:{PIL.__version__}:png{png_level}"

        source_sha = source_sha256 or sha256_file(source)
        cache_key = _media_conversion_cache_key(
            source_sha256=source_sha,
            converter=converter,
            options=options,
            tool_token=tool_token,
            relative_output=relative_output,
        )
        with self._media_cache_lock(cache_key):
            if self._copy_media_cache_hit(cache_key, target):
                return [target]
            match converter:
                case "texture" | "texture-crop":
                    from PIL import Image

                    if target.suffix.casefold() != ".png":
                        raise ValueError(
                            "deterministic texture conversion currently emits PNG only"
                        )
                    with Image.open(source) as opened:
                        converted = opened.convert("RGBA")
                        if converter == "texture-crop":
                            crop = options.get("crop", [])
                            if not (
                                isinstance(crop, list)
                                and len(crop) == 4
                                and all(
                                    isinstance(value, int) and value >= 0
                                    for value in crop
                                )
                                and crop[2] > 0
                                and crop[3] > 0
                            ):
                                raise ValueError(
                                    "texture-crop requires options.crop="
                                    "[x,y,width,height]"
                                )
                            converted = converted.crop(
                                (
                                    crop[0],
                                    crop[1],
                                    crop[0] + crop[2],
                                    crop[1] + crop[3],
                                )
                            )
                        converted.save(
                            target,
                            format="PNG",
                            compress_level=png_level,
                            optimize=False,
                        )
                case "audio":
                    ffmpeg = discover_executable("ffmpeg", "OPENBFME_FFMPEG")
                    assert ffmpeg is not None
                    if source.suffix.casefold() == target.suffix.casefold() and not bool(
                        options.get("force_pcm", False)
                    ):
                        shutil.copyfile(source, target)
                    else:
                        if target.suffix.casefold() != ".wav":
                            raise ValueError(
                                "deterministic audio conversion only supports exact "
                                "copies or PCM WAV output"
                            )
                        command = [
                            str(ffmpeg),
                            "-nostdin",
                            "-hide_banner",
                            "-loglevel",
                            "error",
                            "-y",
                            "-i",
                            str(source),
                            "-fflags",
                            "+bitexact",
                            "-flags:a",
                            "+bitexact",
                            "-map_metadata",
                            "-1",
                            "-vn",
                            "-c:a",
                            "pcm_s16le",
                            str(target),
                        ]
                        run_checked(command)
                case _:
                    raise ValueError(f"unsupported media converter: {converter}")
            self._populate_media_cache(cache_key, target)
        return [target]

    def _convert_sage_particle_definition(
        self,
        source: Path,
        target: Path,
        options: dict[str, Any],
    ) -> list[Path]:
        if target.suffix.casefold() != ".json":
            raise ValueError("sage-particle-definition output must be a .json file")
        if set(options) != {"kind", "name"}:
            raise ValueError(
                "sage-particle-definition options must contain exactly kind and name"
            )
        from .sage_particles import (
            parse_particle_definition,
            particle_definition_document,
        )

        definition = parse_particle_definition(
            source.read_bytes(),
            options["name"],
            kind=options["kind"],
        )
        write_json_atomic(target, particle_definition_document(definition))
        return [target]

    def _convert_sage_scripts(
        self,
        source: Path,
        target: Path,
        options: dict[str, Any],
    ) -> list[Path]:
        if not target.name.casefold().endswith(".scripts.json"):
            raise ValueError("sage-scripts output must be a .scripts.json file")
        if options:
            raise ValueError(
                "sage-scripts accepts no options; got: "
                + ", ".join(sorted(options))
            )
        from .sage_scripts import convert_map_scripts

        return convert_map_scripts(source, target)

    def _convert_sage_map(
        self,
        source: Path,
        output_directory: Path,
        options: dict[str, Any],
        *,
        source_sha256: str | None = None,
        source_virtual_path: str | None = None,
    ) -> list[Path]:
        if output_directory.suffix:
            raise ValueError("sage-map output must be a pack-relative directory")
        from .sage_map import convert_sage_map

        unsupported = sorted(
            set(options) - {"metadata", "expected", "objectBindings", "fixtures", "aiBases", "profile"}
        )
        if unsupported:
            raise ValueError(
                "sage-map has unsupported option(s): " + ", ".join(unsupported)
            )
        metadata = options.get("metadata", {})
        if not isinstance(metadata, dict):
            raise ValueError("sage-map options.metadata must be an object")
        expected = options.get("expected", {})
        if not isinstance(expected, dict):
            raise ValueError("sage-map options.expected must be an object")
        object_bindings = options.get("objectBindings")
        if "objectBindings" in options and not isinstance(object_bindings, dict):
            raise ValueError("sage-map options.objectBindings must be an object")
        fixtures = options.get("fixtures")
        if "fixtures" in options and not isinstance(fixtures, dict):
            raise ValueError("sage-map options.fixtures must be an object")
        ai_bases = options.get("aiBases")
        if "aiBases" in options and not isinstance(ai_bases, dict):
            raise ValueError("sage-map options.aiBases must be an object")
        # Only lobby maps carry lobby start rules. Campaign, cinematic, tutorial
        # and shell maps ship with zero Player_N_Start waypoints by design, so
        # the resource declares the SAGE map profile its category needs.
        map_kind = options.get("profile", "multiplayer")
        if not isinstance(map_kind, str):
            raise ValueError("sage-map options.profile must be a string")
        outputs = convert_sage_map(
            source,
            output_directory,
            metadata,
            expected,
            object_bindings,
            fixtures,
            ai_bases,
            profile=map_kind,
        )
        map_path = output_directory / "map.json"
        map_document = json.loads(map_path.read_text(encoding="utf-8"))
        if not isinstance(map_document, dict):
            raise RuntimeError("sage-map produced a non-object map document")
        source_row = map_document.get("source")
        if not isinstance(source_row, dict):
            raise RuntimeError("sage-map produced no source identity")
        actual_sha256 = source_sha256 or sha256_file(source)
        if source_row.get("sha256") != actual_sha256:
            raise RuntimeError("sage-map source identity disagrees with extraction")
        if source_virtual_path is not None:
            canonical_virtual_path = "/".join(safe_relative_parts(source_virtual_path))
            if source_virtual_path != canonical_virtual_path:
                raise ValueError("sage-map source virtual path is not canonical")
            source_row["virtualPath"] = canonical_virtual_path.casefold()
            source_row["sourceBytes"] = source.stat().st_size
            write_json_atomic(map_path, map_document)
        return outputs

    def _convert_script_composite_bundle(
        self,
        resource: ResolvedResource,
        extracted: Mapping[tuple[str, str], Mapping[str, Any]],
        output: str | None,
        options: dict[str, Any],
        pack_root: Path,
    ) -> list[Path]:
        """Cook one exact map plus its qualified retail AI libraries.

        This remains a separate bundle converter rather than extending
        ``sage-map``: each source document owns an independent
        provenance row, while the composed artifact is declared only once.
        """

        from .sage_scripts import (
            compose_map_scripts_document,
            map_scripts_document,
        )

        if output is None:
            raise ValueError("sage-script-composite requires an output")

        expected_option_keys = {"mapVirtualPath", "libraryVirtualPaths"}
        if set(options) != expected_option_keys:
            unsupported = sorted(set(options) - expected_option_keys)
            missing = sorted(expected_option_keys - set(options))
            details: list[str] = []
            if unsupported:
                details.append("unsupported " + ", ".join(unsupported))
            if missing:
                details.append("missing " + ", ".join(missing))
            raise ValueError(
                "sage-script-composite options are invalid: " + "; ".join(details)
            )
        map_virtual_path = options.get("mapVirtualPath")
        library_virtual_paths = options.get("libraryVirtualPaths")
        legacy_libraries = [
            "libraries/ai_initialize/ai_initialize.map",
            "libraries/ai_mp_inherit_management/ai_mp_inherit_management.map",
        ]
        base_libraries = [
            MULTIPLAYER_START_TEAMS_LIBRARY_PATH,
            "libraries/ai_initialize/ai_initialize.map",
            "libraries/ai_mp_inherit_management/ai_mp_inherit_management.map",
            MULTIPLAYER_HUMAN_LIBRARY_PATH,
        ]
        runtime_slug = canonical_ai_library_map_runtime_slug(map_virtual_path)
        if runtime_slug is None:
            raise ValueError(
                "sage-script-composite mapVirtualPath must name one canonical "
                "multiplayer or admitted castle-skirmish map source"
            )
        expected_output = f"maps/{runtime_slug}/scripts.json"
        if output != expected_output:
            raise ValueError(
                "sage-script-composite output must be exactly "
                f"{expected_output!r} for mapVirtualPath"
            )
        target = _safe_output(pack_root, output)
        if target.exists():
            raise ValueError(
                f"sage-script-composite output collides with pack output: {output!r}"
            )
        # Archive lookup is case-insensitive, but emitted provenance has one
        # canonical spelling so runtime comparison cannot vary by catalog
        # casing or profile authoring.
        map_virtual_path = str(map_virtual_path).casefold()
        allowed_libraries = (
            legacy_libraries,
            [*legacy_libraries, GOLLUM_SPAWN_LIBRARY_PATH],
            base_libraries,
            [*base_libraries, GOLLUM_SPAWN_LIBRARY_PATH],
        )
        if library_virtual_paths not in allowed_libraries:
            raise ValueError(
                "sage-script-composite libraryVirtualPaths must be the ordered "
                "ai_initialize legacy closure or ordered multiplayer_start_teams, ai_initialize, "
                "ai_mp_inherit_management and multiplayer_human closure, optionally "
                "followed by the authored lib_gollumspawn dependency"
            )
        expected_libraries = list(library_virtual_paths)
        requested_paths = [map_virtual_path, *expected_libraries]
        if len({path.casefold() for path in requested_paths}) != len(requested_paths):
            raise ValueError(
                "sage-script-composite source virtual paths must be unique"
            )
        if len(resource.entries) != len(requested_paths):
            raise ValueError(
                "sage-script-composite requires exactly its declared resolved sources"
            )

        entries_by_path: dict[str, Any] = {}
        for entry in resource.entries:
            key = entry.name.casefold()
            if key in entries_by_path:
                raise ValueError(
                    "sage-script-composite resolved an ambiguous virtual path: "
                    f"{entry.name}"
                )
            entries_by_path[key] = entry
        if set(entries_by_path) != {path.casefold() for path in requested_paths}:
            raise ValueError(
                "sage-script-composite resolved sources do not match its exact "
                "map and AI-library closure"
            )

        def decoded_document(virtual_path: str) -> dict[str, Any]:
            entry = entries_by_path[virtual_path.casefold()]
            cached = extracted.get((entry.archive.casefold(), entry.name.casefold()))
            if cached is None:
                raise RuntimeError(
                    "sage-script-composite source was not extracted: "
                    f"{entry.name}"
                )
            source = Path(cached["source_path"])
            return map_scripts_document(source.read_bytes(), container="map")

        map_document = decoded_document(map_virtual_path)
        library_documents = [
            decoded_document(virtual_path)
            for virtual_path in expected_libraries
        ]
        document = compose_map_scripts_document(map_document, library_documents)
        # The composer knows only decoded documents, so it can only require
        # that a library declares ONE placeholder and that the map declares
        # it. This converter is the authority that resolved the archive paths,
        # so it is where each audited library is pinned to the player its own
        # retail bytes name: without this, a library whose source was swapped
        # (or a placeholder renamed) could bind its scripts to a concrete
        # skirmish player instead of the creeps player.
        expected_placeholders = [
            GOLLUM_SPAWN_LIBRARY_PLAYER
            if virtual_path == GOLLUM_SPAWN_LIBRARY_PATH
            else AI_LIBRARY_PLAYER_PLACEHOLDER
            for virtual_path in expected_libraries
        ]
        for index, expected_placeholder in enumerate(expected_placeholders):
            template = document["libraryTemplates"][index]
            if template["playerPlaceholder"] != expected_placeholder:
                raise ValueError(
                    "sage-script-composite library "
                    f"{expected_libraries[index]!r} must bind the player "
                    f"{expected_placeholder!r}, not "
                    f"{template['playerPlaceholder']!r}"
                )
        # This converter is the authority that resolved their
        # archive virtual paths, so bind those paths to the emitted
        # provenance rows here rather than trusting caller-supplied document
        # metadata.
        source_provenance = document["source"]
        source_provenance["game"] = self.game.id
        source_provenance["map"]["virtualPath"] = map_virtual_path
        for index, virtual_path in enumerate(expected_libraries):
            source_provenance["libraries"][index]["virtualPath"] = virtual_path
        target.parent.mkdir(parents=True, exist_ok=True)
        write_json_atomic(target, document)
        return [target]

    def _convert_shell_apt_runtime_bundle(
        self,
        resource: ResolvedResource,
        extracted: Mapping[tuple[str, str], Mapping[str, Any]],
        output: str | None,
        options: dict[str, Any],
        pack_root: Path,
    ) -> list[Path]:
        """Cook the retail main-menu shell APT closure into pack outputs.

        Mirrors :meth:`_convert_hud_apt_runtime_bundle`.  The shell closure is
        not size-pinned the way the palantir closure is, so the guard here is
        structural: exact output identity, no duplicate virtual paths, no
        output collisions, and a contract that never claims parity while the
        native View3D backdrop and timeline playback stay unbound.
        """

        from .retail_shell_apt_convert import (
            OUTPUT_SCHEMA,
            OUTPUT_SCHEMA_VERSION,
            RUNTIME_OUTPUT_PATH,
            SCENE_ID,
            convert_shell_apt_bundle,
        )

        if output != RUNTIME_OUTPUT_PATH:
            raise ValueError(
                f"sage-apt-shell-runtime output must be {RUNTIME_OUTPUT_PATH!r}"
            )
        allowed_options = {"expectedSourceAggregateSha256"}
        unsupported = sorted(set(options) - allowed_options)
        if unsupported:
            raise ValueError(
                "sage-apt-shell-runtime has unsupported option(s): "
                + ", ".join(unsupported)
            )
        expected_aggregate = options.get("expectedSourceAggregateSha256")
        if expected_aggregate is not None and not (
            isinstance(expected_aggregate, str)
            and len(expected_aggregate) == 64
            and all(value in "0123456789abcdef" for value in expected_aggregate)
        ):
            raise ValueError(
                "sage-apt-shell-runtime expectedSourceAggregateSha256 must be "
                "lowercase hex"
            )
        if resource.count_error is not None or not resource.entries:
            raise ValueError("sage-apt-shell-runtime requires resolved sources")

        sources: dict[str, Path] = {}
        seen_paths: set[str] = set()
        for entry in resource.entries:
            cached = extracted.get((entry.archive.casefold(), entry.name.casefold()))
            if cached is None:
                raise RuntimeError(
                    f"shell APT bundle input was not extracted: {entry.name}"
                )
            folded = entry.name.casefold()
            if folded in seen_paths:
                raise ValueError(
                    f"shell APT bundle has duplicate virtual path: {entry.name}"
                )
            seen_paths.add(folded)
            sources[entry.name] = Path(cached["source_path"])

        temporary = pack_root / f".shell-apt-runtime-{resource.rule.id}"
        if temporary.exists():
            raise RuntimeError("shell APT temporary output already exists")
        try:
            contract = convert_shell_apt_bundle(
                sources,
                temporary,
                expected_source_aggregate_sha256=expected_aggregate,
            )
            summary = contract.get("summary")
            source_proof = contract.get("source")
            policy = contract.get("renderPolicy")
            if (
                contract.get("schema") != OUTPUT_SCHEMA
                or contract.get("schemaVersion") != OUTPUT_SCHEMA_VERSION
                or contract.get("sceneId") != SCENE_ID
                or not isinstance(summary, dict)
                or not isinstance(policy, dict)
                or policy.get("actionScriptExecuted") is not False
                or policy.get("syntheticFallbackAllowed") is not False
                or summary.get("parityReady") is not False
                or summary.get("staticSubsetReady") is not True
                or int(summary.get("drawCount", 0)) <= 0
                or not isinstance(source_proof, dict)
                or source_proof.get("sourceCount") != len(sources)
            ):
                raise RuntimeError("shell APT runtime contract changed")

            temporary_files = sorted(
                (path for path in temporary.rglob("*") if path.is_file()),
                key=lambda path: path.relative_to(temporary).as_posix().casefold(),
            )
            if len(temporary_files) != int(summary.get("atlasCount", -1)) + 1:
                raise RuntimeError("shell APT runtime output count changed")
            output_pairs = [
                (
                    source_path,
                    _safe_output(
                        pack_root,
                        source_path.relative_to(temporary).as_posix(),
                    ),
                )
                for source_path in temporary_files
            ]
            collisions = [
                target.relative_to(pack_root).as_posix()
                for _, target in output_pairs
                if target.exists()
            ]
            if collisions:
                raise RuntimeError(
                    "shell APT runtime output collides with pack output: "
                    + ", ".join(collisions)
                )
            outputs: list[Path] = []
            for source_path, target in output_pairs:
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.move(str(source_path), str(target))
                outputs.append(target)
            if RUNTIME_OUTPUT_PATH not in {
                path.relative_to(pack_root).as_posix() for path in outputs
            }:
                raise RuntimeError("shell APT runtime contract output is missing")
        finally:
            shutil.rmtree(temporary, ignore_errors=True)
        return outputs

    def _convert_screen_apt_runtime_bundle(
        self,
        resource: ResolvedResource,
        extracted: Mapping[tuple[str, str], Mapping[str, Any]],
        output: str | None,
        options: dict[str, Any],
        pack_root: Path,
    ) -> list[Path]:
        """Cook ONE retail screen movie into the pack (queue Q117).

        The shell and palantir converters each serve a single pinned scene, so
        their output path is a constant.  There are 62 screens, so the movie is
        an OPTION and the declared output must be the path that movie derives -
        stating it twice is what stops a profile from cooking ScoreScreen into
        SpellStore's slot.

        Everything else keeps the shell converter's posture: exact output
        identity, no duplicate virtual paths, no collision with existing pack
        output, and a contract that never claims parity.
        """

        import re

        from .retail_screen_apt_convert import (
            OPEN_LABEL_PRIORITY,
            convert_screen_apt_bundle,
        )

        allowed_options = {"movie", "expectedSourceAggregateSha256"}
        unsupported = sorted(set(options) - allowed_options)
        if unsupported:
            raise ValueError(
                "sage-apt-screen-runtime has unsupported option(s): "
                + ", ".join(unsupported)
            )
        movie = options.get("movie")
        if not isinstance(movie, str) or not re.fullmatch(
            r"[A-Za-z0-9_]{1,64}", movie
        ):
            raise ValueError(
                "sage-apt-screen-runtime requires a bare movie identifier"
            )
        expected_output = f"data/ui/screens/{movie.casefold()}/scene-contract.json"
        if output != expected_output:
            raise ValueError(
                f"sage-apt-screen-runtime output must be {expected_output!r}"
            )
        expected_aggregate = options.get("expectedSourceAggregateSha256")
        if expected_aggregate is not None and not (
            isinstance(expected_aggregate, str)
            and len(expected_aggregate) == 64
            and all(value in "0123456789abcdef" for value in expected_aggregate)
        ):
            raise ValueError(
                "sage-apt-screen-runtime expectedSourceAggregateSha256 must be "
                "lowercase hex"
            )
        if resource.count_error is not None or not resource.entries:
            raise ValueError("sage-apt-screen-runtime requires resolved sources")

        sources: dict[str, Path] = {}
        seen_paths: set[str] = set()
        for entry in resource.entries:
            cached = extracted.get((entry.archive.casefold(), entry.name.casefold()))
            if cached is None:
                raise RuntimeError(
                    f"screen APT bundle input was not extracted: {entry.name}"
                )
            folded = entry.name.casefold()
            if folded in seen_paths:
                raise ValueError(
                    f"screen APT bundle has duplicate virtual path: {entry.name}"
                )
            seen_paths.add(folded)
            sources[entry.name] = Path(cached["source_path"])

        temporary = pack_root / f".screen-apt-runtime-{resource.rule.id}"
        if temporary.exists():
            raise RuntimeError("screen APT temporary output already exists")
        try:
            contract = convert_screen_apt_bundle(sources, movie, temporary)
            selection = contract.get("frameSelection")
            totals = contract.get("totals")
            if (
                contract.get("schema") != "openbfme.retail-screen-scene"
                or contract.get("schemaVersion") != 0
                or str(contract.get("movie", "")).casefold() != movie.casefold()
                or not isinstance(selection, dict)
                or not isinstance(totals, dict)
                or int(totals.get("draws", 0)) <= 0
            ):
                raise RuntimeError("screen APT runtime contract changed")
            # The frame a screen is shown at is a declared policy, and the
            # runtime refuses any rule it has not been told about.  Refusing an
            # unexpected rule HERE too means a policy change can never reach a
            # pack silently, only loudly.
            rule = str(selection.get("rule", ""))
            if rule not in {"authored-open-label", "no-authored-label-frame-zero"}:
                raise RuntimeError(
                    "screen APT frame was not selected by the declared policy: "
                    + rule
                )
            # Checking the rule NAME is not enough: reordering
            # `OPEN_LABEL_PRIORITY` changes which frame 40 screens bind while
            # the rule string stays "authored-open-label".  So re-derive the
            # choice here from the priority and the labels the contract itself
            # recorded, and refuse if it disagrees.  This is the lock; the rule
            # name is only the label on it.
            priority = selection.get("priority")
            labels = selection.get("availableLabels")
            if list(priority or []) != list(OPEN_LABEL_PRIORITY):
                raise RuntimeError(
                    "screen APT frame priority is not the declared policy: "
                    + repr(priority)
                )
            if not isinstance(labels, dict):
                raise RuntimeError("screen APT frame selection lists no labels")
            expected_label = next(
                (name for name in OPEN_LABEL_PRIORITY if name in labels), None
            )
            if selection.get("label") != expected_label:
                raise RuntimeError(
                    "screen APT bound "
                    f"{selection.get('label')!r} where the declared priority "
                    f"selects {expected_label!r}"
                )
            expected_frame = (
                0 if expected_label is None else int(labels[expected_label])
            )
            if int(selection.get("frame", -1)) != expected_frame:
                raise RuntimeError(
                    "screen APT frame does not match its authored label: "
                    f"{selection.get('frame')} vs {expected_frame}"
                )
            if expected_aggregate is not None and (
                contract.get("sourceAggregateSha256") != expected_aggregate
            ):
                raise RuntimeError("screen APT source aggregate changed")

            temporary_files = sorted(
                (path for path in temporary.rglob("*") if path.is_file()),
                key=lambda path: path.relative_to(temporary).as_posix().casefold(),
            )
            if len(temporary_files) != len(contract.get("atlases", [])) + 1:
                raise RuntimeError("screen APT runtime output count changed")
            output_pairs = [
                (
                    source_path,
                    _safe_output(
                        pack_root,
                        source_path.relative_to(temporary).as_posix(),
                    ),
                )
                for source_path in temporary_files
            ]
            collisions = [
                target.relative_to(pack_root).as_posix()
                for _, target in output_pairs
                if target.exists()
            ]
            if collisions:
                raise RuntimeError(
                    "screen APT runtime output collides with pack output: "
                    + ", ".join(collisions)
                )
            outputs: list[Path] = []
            for source_path, target in output_pairs:
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.move(str(source_path), str(target))
                outputs.append(target)
            if expected_output not in {
                path.relative_to(pack_root).as_posix() for path in outputs
            }:
                raise RuntimeError("screen APT runtime contract output is missing")
        finally:
            shutil.rmtree(temporary, ignore_errors=True)
        return outputs

    def _convert_hud_apt_runtime_bundle(
        self,
        resource: ResolvedResource,
        extracted: Mapping[tuple[str, str], Mapping[str, Any]],
        output: str | None,
        options: dict[str, Any],
        pack_root: Path,
    ) -> list[Path]:
        expected_output = "data/ui/palantir/scene-contract.json"
        if output != expected_output:
            raise ValueError(
                f"sage-apt-runtime output must be {expected_output!r}"
            )
        allowed_options = {
            "expectedSourceAggregateSha256",
            "externalFonts",
        }
        unsupported = sorted(set(options) - allowed_options)
        if unsupported:
            raise ValueError(
                "sage-apt-runtime has unsupported option(s): "
                + ", ".join(unsupported)
            )
        expected_aggregate = options.get("expectedSourceAggregateSha256")
        external_fonts = options.get("externalFonts")
        if not (
            isinstance(expected_aggregate, str)
            and len(expected_aggregate) == 64
            and all(value in "0123456789abcdef" for value in expected_aggregate)
        ):
            raise ValueError(
                "sage-apt-runtime requires lowercase expectedSourceAggregateSha256"
            )
        from .retail_hud_apt_convert import (
            PRODUCTION_ATLAS_COUNT,
            PRODUCTION_BLOCKER_COUNT,
            PRODUCTION_DRAW_COUNT,
            PRODUCTION_MEN_FORDS_RETAIL_INI_SHA256,
            PRODUCTION_OUTPUT_COUNT,
            PRODUCTION_SOURCE_COUNT,
            convert_hud_apt_bundle,
        )

        if (
            resource.count_error is not None
            or len(resource.entries) != PRODUCTION_SOURCE_COUNT
        ):
            raise ValueError(
                "sage-apt-runtime requires exactly "
                f"{PRODUCTION_SOURCE_COUNT} resolved sources"
            )

        sources: dict[str, Path] = {}
        seen_paths: set[str] = set()
        for entry in resource.entries:
            cached = extracted.get((entry.archive.casefold(), entry.name.casefold()))
            if cached is None:
                raise RuntimeError(
                    f"HUD APT bundle input was not extracted: {entry.name}"
                )
            folded = entry.name.casefold()
            if folded in seen_paths:
                raise ValueError(
                    f"HUD APT bundle has duplicate virtual path: {entry.name}"
                )
            seen_paths.add(folded)
            sources[entry.name] = Path(cached["source_path"])

        external_font_sources: dict[str, Path] = {}
        if not isinstance(external_fonts, list):
            raise ValueError("sage-apt-runtime externalFonts must be an array")
        for binding in external_fonts:
            if not isinstance(binding, dict):
                raise ValueError("sage-apt-runtime externalFonts entry must be an object")
            virtual = binding.get("sourceVirtualPath")
            expected_sha256 = binding.get("sourceSha256")
            if not isinstance(virtual, str) or not isinstance(expected_sha256, str):
                raise ValueError("sage-apt-runtime externalFonts identity is invalid")
            folded_virtual = virtual.replace("\\", "/").strip("/").casefold()
            candidates = [
                Path(cached["source_path"])
                for cached in extracted.values()
                if cached["catalog"].name.replace("\\", "/").strip("/").casefold()
                == folded_virtual
                and cached["source_sha256"] == expected_sha256
            ]
            if len(candidates) != 1:
                raise ValueError(
                    "sage-apt-runtime exact external font input could not be resolved"
                )
            external_font_sources[virtual] = candidates[0]

        retail_ini_sources: dict[str, Path] = {}
        for virtual, expected_sha256 in PRODUCTION_MEN_FORDS_RETAIL_INI_SHA256.items():
            folded_virtual = virtual.casefold()
            candidates = [
                Path(cached["source_path"])
                for cached in extracted.values()
                if cached["catalog"].name.replace("\\", "/").strip("/").casefold()
                == folded_virtual
                and cached["source_sha256"] == expected_sha256
            ]
            if len(candidates) != 1:
                raise ValueError(
                    "sage-apt-runtime exact Men/Fords INI input could not be resolved"
                )
            retail_ini_sources[virtual] = candidates[0]

        temporary = pack_root / f".hud-apt-runtime-{resource.rule.id}"
        if temporary.exists():
            raise RuntimeError("HUD APT temporary output already exists")
        try:
            contract = convert_hud_apt_bundle(
                sources,
                temporary,
                expected_source_aggregate_sha256=expected_aggregate,
                external_fonts=(
                    external_fonts if isinstance(external_fonts, list) else ()
                ),
                external_font_sources=external_font_sources,
                retail_ini_sources=retail_ini_sources,
            )
            summary = contract.get("summary")
            source_proof = contract.get("source")
            if (
                contract.get("schema") != "openbfme.retail-hud-apt-runtime"
                or contract.get("schemaVersion") != 0
                or contract.get("sceneId") != "bfme2.ui.palantir"
                or not isinstance(summary, dict)
                or summary.get("atlasCount") != PRODUCTION_ATLAS_COUNT
                or summary.get("drawCount") != PRODUCTION_DRAW_COUNT
                or summary.get("blockerCount") != PRODUCTION_BLOCKER_COUNT
                or summary.get("parityReady") is not False
                or not isinstance(source_proof, dict)
                or source_proof.get("sourceCount") != PRODUCTION_SOURCE_COUNT
                or source_proof.get("sourceAggregateSha256")
                != expected_aggregate
                or contract.get("runtimeAssetBindings")
                != {"externalFonts": external_fonts}
            ):
                raise RuntimeError("HUD APT runtime contract changed")

            temporary_files = sorted(
                (path for path in temporary.rglob("*") if path.is_file()),
                key=lambda path: path.relative_to(temporary).as_posix().casefold(),
            )
            if len(temporary_files) != PRODUCTION_OUTPUT_COUNT:
                raise RuntimeError("HUD APT runtime output count changed")
            output_pairs = [
                (
                    source_path,
                    _safe_output(
                        pack_root,
                        source_path.relative_to(temporary).as_posix(),
                    ),
                )
                for source_path in temporary_files
            ]
            collisions = [
                target.relative_to(pack_root).as_posix()
                for _, target in output_pairs
                if target.exists()
            ]
            if collisions:
                raise RuntimeError(
                    "HUD APT runtime output collides with pack output: "
                    + ", ".join(collisions)
                )
            outputs: list[Path] = []
            for source_path, target in output_pairs:
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.move(str(source_path), str(target))
                outputs.append(target)
            if expected_output not in {
                path.relative_to(pack_root).as_posix() for path in outputs
            }:
                raise RuntimeError("HUD APT runtime contract output is missing")
            return outputs
        finally:
            if temporary.exists():
                shutil.rmtree(temporary)

    def _convert_retail_unit_rules_bundle(
        self,
        resource: ResolvedResource,
        extracted: Mapping[tuple[str, str], Mapping[str, Any]],
        output: str | None,
        options: dict[str, Any],
        pack_root: Path,
    ) -> list[Path]:
        from .retail_unit_rules import OUTPUT_PATH, extract_retail_unit_rules

        if output != OUTPUT_PATH:
            raise ValueError(f"retail-unit-rules output must be {OUTPUT_PATH!r}")
        if options:
            raise ValueError(
                "retail-unit-rules has unsupported option(s): "
                + ", ".join(sorted(options))
            )
        if resource.count_error is not None:
            raise ValueError(resource.count_error)
        # The profile composer intentionally assigns the other three member INIs
        # to their existing faction resources. They are nevertheless present in
        # this deterministic extraction set, so assemble the exact retail rules
        # closure by virtual path rather than duplicating profile ownership.
        sources: dict[str, Path] = {}
        seen_paths: set[str] = set()
        for cached in extracted.values():
            virtual_path = str(cached["catalog"].name).replace("\\", "/")
            folded = virtual_path.casefold()
            if folded in seen_paths:
                continue
            seen_paths.add(folded)
            sources[virtual_path] = Path(cached["source_path"])
        target = _safe_output(pack_root, OUTPUT_PATH)
        write_json_atomic(target, extract_retail_unit_rules(sources))
        return [target]

    def _convert_living_world_bundle(
        self,
        resource: ResolvedResource,
        extracted: Mapping[tuple[str, str], Mapping[str, Any]],
        output: str | None,
        options: dict[str, Any],
        pack_root: Path,
    ) -> list[Path]:
        """Cook the War-of-the-Ring strategic document into the pack.

        The whole deterministic extraction set is offered to the profiler by
        virtual path (mirroring ``retail-unit-rules``): the profiler reads
        exactly the ``#include`` closure it needs, fails closed on a missing
        include, and :func:`require_shippable` refuses to ship a document
        whose entry points did not resolve or whose region graph has no edges.
        """

        from .livingworld import (
            LIVING_WORLD_PACK_PATH,
            profile_living_world_from_files,
            require_shippable,
        )

        if output != LIVING_WORLD_PACK_PATH:
            raise ValueError(
                f"living-world output must be {LIVING_WORLD_PACK_PATH!r}"
            )
        game = options.get("game")
        if set(options) != {"game"} or not isinstance(game, str):
            raise ValueError(
                "living-world requires exactly one option: game=<retail game id>"
            )
        if resource.count_error is not None:
            raise ValueError(resource.count_error)
        sources: dict[str, tuple[str, Path]] = {}
        seen_paths: set[str] = set()
        for cached in extracted.values():
            entry = cached["catalog"]
            virtual_path = str(entry.name).replace("\\", "/")
            folded = virtual_path.casefold()
            if folded in seen_paths:
                continue
            seen_paths.add(folded)
            sources[virtual_path] = (str(entry.archive), Path(cached["source_path"]))
        document = require_shippable(
            profile_living_world_from_files(sources, game)
        )
        target = _safe_output(pack_root, LIVING_WORLD_PACK_PATH)
        write_json_atomic(target, document)
        return [target]

    def _convert_terrain_material_bundle(
        self,
        resource: ResolvedResource,
        extracted: Mapping[tuple[str, str], Mapping[str, Any]],
        output: str | None,
        options: dict[str, Any],
        pack_root: Path,
    ) -> list[Path]:
        if not output:
            raise ValueError("sage-terrain-materials requires an output directory")
        if "{" in output or "}" in output:
            raise ValueError(
                "sage-terrain-materials output cannot contain format tokens"
            )
        unsupported = sorted(set(options) - {"symbols"})
        if unsupported:
            raise ValueError(
                "sage-terrain-materials has unsupported option(s): "
                + ", ".join(unsupported)
            )
        sources: list[tuple[str, Path]] = []
        for entry in resource.entries:
            key = (entry.archive.casefold(), entry.name.casefold())
            cached = extracted.get(key)
            if cached is None:
                raise RuntimeError(
                    f"terrain material input resource was not extracted: {entry.name}"
                )
            sources.append((entry.name, Path(cached["source_path"])))
        target = _safe_output(pack_root, output)
        from .terrain_materials import convert_terrain_materials

        return convert_terrain_materials(sources, target, options.get("symbols"))

    def _convert_texture_atlas_crops(
        self,
        source: Path,
        output: str | None,
        options: dict[str, Any],
        pack_root: Path,
    ) -> list[Path]:
        if not output:
            raise ValueError("texture-atlas-crops requires an output directory")
        if "{" in output or "}" in output:
            raise ValueError("texture-atlas-crops output cannot contain format tokens")
        unsupported = sorted(set(options) - {"crops"})
        if unsupported:
            raise ValueError(
                "texture-atlas-crops has unsupported option(s): "
                + ", ".join(unsupported)
            )
        crops = normalize_texture_atlas_crops(
            options.get("crops"),
            output,
        )
        try:
            from PIL import Image
            import PIL
        except ImportError as exc:
            raise FileNotFoundError(
                "Pillow is required for deterministic texture atlas conversion"
            ) from exc
        if PIL.__version__ != "12.2.0":
            raise RuntimeError(
                "Pillow 12.2.0 is required for deterministic texture output; "
                f"found {PIL.__version__}"
            )

        target_root = _safe_output(pack_root, output)
        outputs: list[Path] = []
        with Image.open(source) as opened:
            width, height = opened.size
            for crop_record in crops:
                x, y, crop_width, crop_height = crop_record["crop"]
                if x + crop_width > width or y + crop_height > height:
                    raise ValueError(
                        "texture-atlas-crops crop exceeds source image bounds: "
                        f"{crop_record['logicalName']!r} is outside {width}x{height}"
                    )

            converted = opened.convert("RGBA")
            for crop_record in crops:
                x, y, crop_width, crop_height = crop_record["crop"]
                target = _safe_output(target_root, crop_record["output"])
                target.parent.mkdir(parents=True, exist_ok=True)
                converted.crop((x, y, x + crop_width, y + crop_height)).save(
                    target, format="PNG", compress_level=9, optimize=False
                )
                outputs.append(target)
        return outputs

    def _blender_soft_tree_fingerprint(self, blender: Path) -> str:
        """Bounded soft identity for end-of-batch tool checks (dev / opt-in).

        Portable Blender 4.x puts conversion-relevant scripts under
        ``<version>/scripts/**`` (not always a top-level ``scripts/``). Soft
        mode samples:
        - blender.exe + top-level natives
        - every ``**/scripts/**`` tree (content hash for small files)
        - bounded native libs under versioned subdirs (``.dll``/``.pyd``)
        """

        root = blender.parent
        digest = hashlib.sha256()
        seen: set[str] = set()

        def _add(path: Path, *, content: bool) -> None:
            try:
                if not path.is_file() or _is_link_like(path):
                    return
                rel = path.relative_to(root).as_posix().casefold()
            except (OSError, ValueError):
                return
            if rel in seen:
                return
            seen.add(rel)
            try:
                st = path.stat()
                payload = path.read_bytes() if content and st.st_size <= 262_144 else None
            except OSError:
                return
            digest.update(rel.encode("utf-8"))
            digest.update(b"\0")
            if payload is not None:
                digest.update(hashlib.sha256(payload).digest())
            else:
                digest.update(str(int(st.st_mtime_ns)).encode("ascii"))
                digest.update(b"\0")
                digest.update(str(int(st.st_size)).encode("ascii"))
            digest.update(b"\n")

        _add(blender, content=False)
        try:
            for path in sorted(root.iterdir(), key=lambda p: p.name.casefold()):
                if path.is_file() and path.suffix.casefold() in {
                    ".dll",
                    ".pyd",
                    ".exe",
                }:
                    _add(path, content=False)
        except OSError:
            pass

        # Versioned portable layout: 4.2/scripts (not only top-level scripts/).
        # Bound discovery to root + one-level children to stay O(version dirs).
        script_roots: list[Path] = []
        try:
            if (root / "scripts").is_dir() and not _is_link_like(root / "scripts"):
                script_roots.append(root / "scripts")
            for child in sorted(root.iterdir(), key=lambda p: p.name.casefold()):
                if not child.is_dir() or _is_link_like(child):
                    continue
                candidate = child / "scripts"
                if candidate.is_dir() and not _is_link_like(candidate):
                    script_roots.append(candidate)
        except OSError:
            pass

        script_count = 0
        for scripts in script_roots:
            try:
                for path in sorted(scripts.rglob("*"), key=lambda p: str(p).casefold()):
                    if not path.is_file():
                        continue
                    # Prefer content for scripts/python sources.
                    content = path.suffix.casefold() in {
                        ".py",
                        ".pyw",
                        ".txt",
                        ".xml",
                        ".json",
                        ".osl",
                    }
                    _add(path, content=content)
                    script_count += 1
                    if script_count >= 800:
                        break
            except OSError:
                continue
            if script_count >= 800:
                break

        # Bounded natives under top-level and versioned children (e.g. 4.2/python).
        native_count = 0
        native_roots: list[Path] = [root]
        try:
            for child in sorted(root.iterdir(), key=lambda p: p.name.casefold()):
                if child.is_dir() and not _is_link_like(child):
                    native_roots.append(child)
        except OSError:
            pass
        for native_root in native_roots:
            try:
                for path in sorted(
                    native_root.rglob("*"), key=lambda p: str(p).casefold()
                ):
                    if not path.is_file():
                        continue
                    if path.suffix.casefold() not in {".dll", ".pyd"}:
                        continue
                    _add(path, content=False)
                    native_count += 1
                    if native_count >= 200:
                        break
            except OSError:
                continue
            if native_count >= 200:
                break
        return digest.hexdigest()

    def _prepare_w3d_execution_tools(
        self, blender: Path, plugin: Path
    ) -> tuple[str, dict[str, str]]:
        """Prepare both pinned execution inputs immediately before Blender runs."""

        from .bootstrap import (
            prepare_blender_portable_tree,
            prepare_opensage_plugin_checkout,
        )

        blender_tree_sha256 = prepare_blender_portable_tree(self.state_root, blender)
        plugin_attestation = prepare_opensage_plugin_checkout(self.state_root, plugin)
        self._blender_tree_verified = True
        try:
            st = blender.stat()
            # Keep legacy exe pair for diagnostics; soft end-attest uses the
            # broader scripts/native sample fingerprint.
            self._blender_exe_fingerprint = (int(st.st_mtime_ns), int(st.st_size))
            self._blender_soft_tree_fingerprint_value = (
                self._blender_soft_tree_fingerprint(blender)
            )
        except OSError:
            self._blender_exe_fingerprint = None
            self._blender_soft_tree_fingerprint_value = None
        return blender_tree_sha256, plugin_attestation

    def _w3d_execution_tool_paths(self) -> tuple[Path, Path]:
        blender = (
            Path(
                os.environ.get(
                    "OPENBFME_BLENDER",
                    str(
                        self.state_root
                        / "tools"
                        / "blender-4.2.0-windows-x64"
                        / "blender.exe"
                    ),
                )
            )
            .expanduser()
            .resolve()
        )
        plugin = (
            Path(
                os.environ.get(
                    "OPENBFME_W3D_PLUGIN",
                    str(self.state_root / "tools" / "OpenSAGE.BlenderPlugin"),
                )
            )
            .expanduser()
            .resolve()
        )
        if not blender.is_file():
            raise FileNotFoundError(
                f"pinned Blender 4.2.0 not found: {blender}; run importer bootstrap-tools"
            )
        if not (plugin / "io_mesh_w3d" / "__init__.py").is_file():
            raise FileNotFoundError(
                f"pinned OpenSAGE W3D plugin not found: {plugin}; run importer bootstrap-tools"
            )
        return blender, plugin

    def _begin_w3d_conversion_batch(self) -> None:
        if self._w3d_batch_tools is not None:
            raise RuntimeError("W3D conversion batch is already active")
        blender, plugin = self._w3d_execution_tool_paths()
        blender_tree_sha256, plugin_attestation = self._prepare_w3d_execution_tools(
            blender, plugin
        )
        self._w3d_batch_tools = {
            "blender": blender,
            "plugin": plugin,
            "blender_tree_sha256": blender_tree_sha256,
            "plugin_attestation_sha256": _w3d_plugin_attestation_sha256(
                plugin_attestation
            ),
        }

    def _end_w3d_conversion_batch(self) -> None:
        tools = self._w3d_batch_tools
        if tools is None:
            return
        self._w3d_batch_tools = None
        from .bootstrap import (
            BLENDER_TREE_SHA256,
            _attest_opensage_plugin_checkout,
            _reject_python_bytecode,
            _reject_tree_links,
        )

        blender = Path(tools["blender"])
        plugin = Path(tools["plugin"])
        _reject_tree_links(blender.parent, "Blender portable tree")
        _reject_python_bytecode(blender.parent, "Blender portable tree")
        plugin_attestation = _attest_opensage_plugin_checkout(plugin)
        # Shipping default: full tree re-hash at end (~5–7s). Soft end-attest
        # (bounded scripts/native sample) is only for OPENBFME_DEV or explicit
        # OPENBFME_SOFT_TOOL_ATTEST. OPENBFME_STRICT_TOOL_ATTEST forces full.
        force_strict = os.environ.get(
            "OPENBFME_STRICT_TOOL_ATTEST", ""
        ).strip().casefold() in {"1", "true", "yes"}
        allow_soft = (
            not force_strict
            and (
                self.dev_mode
                or os.environ.get("OPENBFME_SOFT_TOOL_ATTEST", "")
                .strip()
                .casefold()
                in {"1", "true", "yes"}
            )
        )
        tree_ok = False
        soft_fp = getattr(self, "_blender_soft_tree_fingerprint_value", None)
        if allow_soft and soft_fp is not None:
            try:
                tree_ok = soft_fp == self._blender_soft_tree_fingerprint(
                    blender
                ) and str(tools.get("blender_tree_sha256", "")).casefold() == (
                    BLENDER_TREE_SHA256.casefold()
                )
            except OSError:
                tree_ok = False
        if not tree_ok and directory_tree_sha256(blender.parent) != BLENDER_TREE_SHA256:
            raise RuntimeError("Blender portable tree changed during W3D conversion")
        # Exact-root only: a walking status check would report the cleanliness
        # of whatever checkout encloses a non-repository plugin directory.
        plugin_clean = git_worktree_clean_at_exact_root(plugin)
        if not plugin_clean:
            raise RuntimeError("OpenSAGE W3D plugin changed during W3D conversion")
        self._w3d_final_attestation = {
            "blender_tree_sha256": BLENDER_TREE_SHA256,
            "plugin": plugin_attestation,
            "plugin_worktree_clean": plugin_clean,
            "end_attest": "soft" if (allow_soft and tree_ok) else "full",
        }

    def _convert_w3d_resources(
        self,
        jobs: list[tuple[int, list[Path], str | None, dict[str, Any], Path, str, str, str]],
        *,
        progress_stage: str | None = "blender-w3d",
    ) -> tuple[dict[int, list[Path]], dict[int, Exception]]:
        outputs: dict[int, list[Path]] = {}
        errors: dict[int, Exception] = {}
        if not jobs:
            return outputs, errors
        job_roots: set[str] = set()
        output_paths: set[str] = set()
        for job in jobs:
            profile_id, asset_id = job[5], job[6]
            root_key = str(self.jobs_root / profile_id / "w3d" / asset_id).casefold()
            declared_outputs = (job[2], _w3d_report_relative_path(asset_id))
            if root_key in job_roots:
                raise RuntimeError(f"W3D conversion jobs share a job root: {asset_id}")
            job_roots.add(root_key)
            for declared in declared_outputs:
                if not declared:
                    continue
                validated_output = _safe_output(job[4], declared)
                output_key = str(validated_output).casefold()
                if output_key in output_paths:
                    raise RuntimeError(
                        f"W3D conversion jobs share an output path: {declared}"
                    )
                output_paths.add(output_key)
                # Resolve and create shared pack ancestors before workers start.
                # On Windows, concurrent directory creation can make strict
                # Path.resolve containment checks transiently inconsistent.
                validated_output.parent.mkdir(parents=True, exist_ok=True)
        self._begin_w3d_conversion_batch()
        try:
            from .progress import emit as progress_emit

            use_multi = os.environ.get("OPENBFME_W3D_MULTI", "1").strip().casefold() not in {
                "0",
                "false",
                "no",
            }
            try:
                batch_size = max(1, int(os.environ.get("OPENBFME_W3D_BATCH_SIZE", "8")))
            except ValueError:
                batch_size = 8
            stage = "" if progress_stage is None else progress_stage
            if stage:
                progress_emit(
                    stage,
                    f"converting {len(jobs)} W3D models "
                    f"(workers={self.conversion_jobs}, multi={use_multi}, batch={batch_size})",
                    total_units=len(jobs),
                )
            if use_multi and len(jobs) > 1 and batch_size > 1:
                chunks = [
                    jobs[offset : offset + batch_size]
                    for offset in range(0, len(jobs), batch_size)
                ]
                workers = min(self.conversion_jobs, len(chunks))
                with ThreadPoolExecutor(max_workers=workers) as pool:
                    futures = {
                        pool.submit(self._convert_w3d_chunk, chunk): chunk
                        for chunk in chunks
                    }
                    for future in as_completed(futures):
                        try:
                            chunk_outputs, chunk_errors = future.result()
                        except (FileNotFoundError, RuntimeError, ValueError, OSError) as exc:
                            # Whole chunk failed before per-job accounting.
                            for job in futures[future]:
                                errors[job[0]] = exc
                                progress_emit(
                                    stage,
                                    f"chunk failed ({len(outputs) + len(errors)}/{len(jobs)})",
                                    unit_delta=1,
                                )
                            continue
                        outputs.update(chunk_outputs)
                        errors.update(chunk_errors)
                        done = len(chunk_outputs) + len(chunk_errors)
                        progress_emit(
                            stage,
                            f"chunk done +{done} ({len(outputs) + len(errors)}/{len(jobs)})",
                            unit_delta=done,
                        )
            else:
                with ThreadPoolExecutor(
                    max_workers=min(self.conversion_jobs, len(jobs))
                ) as pool:
                    futures = {
                        pool.submit(self._convert_w3d_bundle, *job[1:]): job[0]
                        for job in jobs
                    }
                    for future in as_completed(futures):
                        index = futures[future]
                        try:
                            outputs[index] = future.result()
                            progress_emit(
                                stage,
                                f"model done ({len(outputs) + len(errors)}/{len(jobs)})",
                                unit_delta=1,
                            )
                        except (
                            FileNotFoundError,
                            RuntimeError,
                            ValueError,
                            OSError,
                        ) as exc:
                            errors[index] = exc
                            progress_emit(
                                stage,
                                f"model failed ({len(outputs) + len(errors)}/{len(jobs)})",
                                unit_delta=1,
                            )
        finally:
            self._end_w3d_conversion_batch()
        return outputs, errors

    def _convert_w3d_chunk(
        self,
        chunk: list[
            tuple[int, list[Path], str | None, dict[str, Any], Path, str, str, str]
        ],
    ) -> tuple[dict[int, list[Path]], dict[int, Exception]]:
        """Convert one batch of W3D jobs; one Blender process for cache misses."""

        outputs: dict[int, list[Path]] = {}
        errors: dict[int, Exception] = {}
        # Fall back to single-job path when batch is tiny or multi is unsafe.
        if len(chunk) == 1:
            index = chunk[0][0]
            try:
                outputs[index] = self._convert_w3d_bundle(*chunk[0][1:])
            except (FileNotFoundError, RuntimeError, ValueError, OSError) as exc:
                errors[index] = exc
            return outputs, errors

        prepared: list[dict[str, Any]] = []
        for job in chunk:
            index = job[0]
            try:
                prepared.append(
                    self._prepare_w3d_bundle_job(index, *job[1:])
                )
            except (FileNotFoundError, RuntimeError, ValueError, OSError) as exc:
                errors[index] = exc

        hits = [item for item in prepared if item["cache_hit"]]
        misses = [item for item in prepared if not item["cache_hit"]]
        for item in hits:
            try:
                outputs[item["index"]] = self._finalize_w3d_bundle_job(
                    item, item["combined_log"], cache_hit=True
                )
            except (FileNotFoundError, RuntimeError, ValueError, OSError) as exc:
                errors[item["index"]] = exc

        if not misses:
            return outputs, errors

        if self._w3d_batch_tools is None:
            raise RuntimeError("W3D conversion requires an active attested batch")
        blender = Path(self._w3d_batch_tools["blender"])
        plugin = Path(self._w3d_batch_tools["plugin"])
        multi_adapter = (
            repo_root_from_module() / "importer" / "blender" / "w3d_multi_to_glb.py"
        )
        batch_root = (
            self.jobs_root
            / misses[0]["profile_id"]
            / "w3d-multi"
            / hashlib.sha256(
                "|".join(item["asset_id"] for item in misses).encode("utf-8")
            ).hexdigest()[:16]
        )
        if batch_root.exists():
            shutil.rmtree(batch_root)
        batch_root.mkdir(parents=True)
        multi_jobs = []
        for item in misses:
            multi_jobs.append(
                {
                    "job_id": item["asset_id"],
                    "model": str(item["model"]),
                    "asset_kind": item["asset_kind"],
                    "animations": [str(path) for path in item["animations"]],
                    "required_equipment": list(item["required_equipment"]),
                    "excluded_optional_meshes": list(item["excluded_optional_meshes"]),
                    "proven_root_rigid_bake": item["proven_root_rigid_bake"],
                    "proven_pivot_only_model": item.get(
                        "proven_pivot_only_model", False
                    ),
                    "retail_absent_textures": list(
                        item.get("retail_absent_textures", [])
                    ),
                    "output": str(item["target"]),
                }
            )
        jobs_path = batch_root / "jobs.json"
        write_json_atomic(
            jobs_path,
            {"schema": "openbfme.w3d-multi-jobs", "jobs": multi_jobs},
        )
        command = [
            str(blender),
            "--factory-startup",
            "-noaudio",
            "--background",
            "--python-use-system-env",
            "--python-exit-code",
            "1",
            "--python",
            str(multi_adapter),
            "--",
            "--plugin-root",
            str(plugin),
            "--jobs",
            str(jobs_path),
        ]
        isolated = _isolated_blender_environment(os.environ, batch_root)
        try:
            result = run_checked(command, env=isolated)
            combined = result.stdout + "\n" + result.stderr
        except (FileNotFoundError, RuntimeError, ValueError, OSError) as exc:
            for item in misses:
                errors[item["index"]] = exc
            return outputs, errors

        ok_payloads: dict[str, dict[str, Any]] = {}
        fails: dict[str, str] = {}
        for line in combined.splitlines():
            if line.startswith("OPENBFME_W3D_JOB_OK "):
                payload = json.loads(line.split(" ", 1)[1])
                ok_payloads[str(payload["job_id"])] = payload
            elif line.startswith("OPENBFME_W3D_JOB_FAIL "):
                payload = json.loads(line.split(" ", 1)[1])
                detail = str(
                    payload.get("error") or payload.get("error_type") or "failed"
                )
                failure_phase = payload.get("failure_phase")
                failure_kind = payload.get("failure_kind")
                if (
                    type(failure_phase) is str
                    and failure_phase
                    and type(failure_kind) is str
                    and failure_kind
                ):
                    detail = f"{detail} [failure_phase={failure_phase} failure_kind={failure_kind}]"
                fails[str(payload["job_id"])] = detail

        for item in misses:
            asset_id = item["asset_id"]
            if asset_id in fails:
                errors[item["index"]] = RuntimeError(
                    f"W3D multi-job failed for {asset_id}: {fails[asset_id]}"
                )
                continue
            payload = ok_payloads.get(asset_id)
            if payload is None:
                errors[item["index"]] = RuntimeError(
                    f"W3D multi-job missing success marker for {asset_id}"
                )
                continue
            report = payload.get("report")
            output_log = payload.get("output_log")
            if (
                not isinstance(report, dict)
                or type(output_log) is not str
                or len(output_log) > _W3D_MULTI_JOB_MAX_OUTPUT_LOG_CHARS
            ):
                errors[item["index"]] = RuntimeError(
                    f"W3D multi-job emitted an invalid or unbounded output log "
                    f"for {asset_id}"
                )
                continue
            # Finalize against the job's REAL captured output (plus the
            # synthesized success marker), never the marker alone: the
            # warning-text guards in _finalize_w3d_bundle_job must see the
            # same content the single-job process log carries, and the
            # combined log is what the conversion cache stores for later
            # single-job cache hits.
            log = output_log + "\nOPENBFME_W3D_OK " + json.dumps(report, sort_keys=True)
            try:
                outputs[item["index"]] = self._finalize_w3d_bundle_job(
                    item, log, cache_hit=False
                )
            except (FileNotFoundError, RuntimeError, ValueError, OSError) as exc:
                errors[item["index"]] = exc
        return outputs, errors

    def _w3d_cache_lock(self, key: str) -> threading.Lock:
        with self._conversion_cache_lock:
            return self._conversion_key_locks.setdefault(key, threading.Lock())

    def _copy_w3d_cache_hit(self, key: str, target: Path) -> str | None:
        if not self.conversion_cache_enabled:
            return None
        def miss() -> None:
            with self._conversion_cache_lock:
                self._conversion_cache_stats["misses"] += 1

        def discard_invalid_entry(entry: Path) -> None:
            if entry.is_dir():
                shutil.rmtree(entry)

        entry = self.converted_cache_root / key
        metadata_path = entry / "metadata.json"
        cached_output = entry / "output.glb"
        # See _copy_media_cache_hit: this temporary lives inside the pack, so
        # leaving one behind changes the pack's address without changing a
        # single converted output.
        in_flight: Path | None = None
        try:
            metadata = read_json(metadata_path)
            if (
                metadata.get("format") != 1
                or metadata.get("key") != key
                or not isinstance(metadata.get("combined_log"), str)
                or len(metadata.get("combined_log", ""))
                > _W3D_MULTI_JOB_MAX_OUTPUT_LOG_CHARS
                or metadata.get("output_size") != cached_output.stat().st_size
            ):
                discard_invalid_entry(entry)
                miss()
                return None
            target.parent.mkdir(parents=True, exist_ok=True)
            temporary = target.with_name(target.name + ".cache-copying")
            in_flight = temporary
            temporary.unlink(missing_ok=True)
            # Same single-read verification as the media cache: the digest
            # describes the bytes actually written to *temporary*.
            written, copied_sha256 = _copy_file_with_digest(cached_output, temporary)
            if (
                written != metadata["output_size"]
                or copied_sha256 != metadata["output_sha256"]
            ):
                temporary.unlink(missing_ok=True)
                if sha256_file(cached_output) != metadata["output_sha256"]:
                    discard_invalid_entry(entry)
                    miss()
                    return None
                raise RuntimeError("converted W3D cache copy failed byte verification")
            os.replace(temporary, target)
            in_flight = None
        except (FileNotFoundError, KeyError, OSError, TypeError, ValueError):
            discard_invalid_entry(entry)
            miss()
            return None
        finally:
            if in_flight is not None:
                in_flight.unlink(missing_ok=True)
        with self._conversion_cache_lock:
            self._conversion_cache_stats["hits"] += 1
        return str(metadata["combined_log"])

    def _populate_w3d_cache(self, key: str, target: Path, combined_log: str) -> None:
        if not self.conversion_cache_enabled:
            return
        self.converted_cache_root.mkdir(parents=True, exist_ok=True)
        destination = self.converted_cache_root / key
        if destination.is_dir():
            existing = destination / "output.glb"
            if not existing.is_file() or sha256_file(existing) != sha256_file(target):
                raise RuntimeError(
                    "converted W3D cache key produced non-byte-identical output"
                )
            return
        temporary: Path | None = Path(
            tempfile.mkdtemp(prefix=f".{key}.", dir=self.converted_cache_root)
        )
        try:
            cached_output = temporary / "output.glb"
            shutil.copyfile(target, cached_output)
            output_sha256 = sha256_file(target)
            if sha256_file(cached_output) != output_sha256:
                raise RuntimeError("converted W3D cache populate changed output bytes")
            write_json_atomic(
                temporary / "metadata.json",
                {
                    "format": 1,
                    "key": key,
                    "output_sha256": output_sha256,
                    "output_size": target.stat().st_size,
                    "combined_log": combined_log,
                },
            )
            try:
                _replace_directory_with_retry(temporary, destination)
            except OSError:
                if not destination.is_dir():
                    raise
                existing = destination / "output.glb"
                if not existing.is_file() or sha256_file(existing) != output_sha256:
                    raise RuntimeError(
                        "converted W3D cache key produced non-byte-identical output"
                    )
                return
            temporary = None
            with self._conversion_cache_lock:
                self._conversion_cache_stats["populated"] += 1
        finally:
            if temporary is not None and temporary.is_dir():
                shutil.rmtree(temporary)

    def _prepare_w3d_bundle_job(
        self,
        index: int,
        staging_sources: list[Path],
        output: str | None,
        options: dict[str, Any],
        pack_root: Path,
        profile_id: str,
        asset_id: str,
        asset_kind: str,
    ) -> dict[str, Any]:
        """Stage sources and resolve cache for one W3D job (no Blender yet)."""

        if not output:
            raise ValueError(f"w3d-{asset_kind} requires an output path")
        if asset_kind not in {"animated", "hierarchical", "static"}:
            raise ValueError(f"unsupported W3D asset kind: {asset_kind}")
        report_relative_path = _w3d_report_relative_path(asset_id)
        model_name = str(options.get("model", "")).casefold()
        raw_animation_names = options.get("animations", [])
        if not isinstance(raw_animation_names, list) or any(
            not isinstance(value, str) for value in raw_animation_names
        ):
            raise ValueError("W3D options.animations must be an array of strings")
        animation_names = [value.casefold() for value in raw_animation_names]
        if not model_name:
            raise ValueError(f"w3d-{asset_kind} requires options.model")
        if asset_kind == "animated" and not animation_names:
            raise ValueError("w3d-bundle requires options.animations")
        if asset_kind != "animated" and animation_names:
            raise ValueError(f"w3d-{asset_kind} does not accept options.animations")
        required_equipment = _normalize_required_equipment(
            options.get("required_equipment", [])
        )
        excluded_optional_meshes = normalize_excluded_optional_meshes(
            options.get(W3D_EXCLUDED_OPTIONAL_MESHES_OPTION, [])
        )
        proven_root_rigid_bake = options.get(W3D_PROVEN_ROOT_RIGID_BAKE_OPTION, False)
        if not isinstance(proven_root_rigid_bake, bool):
            raise ValueError(
                f"W3D options.{W3D_PROVEN_ROOT_RIGID_BAKE_OPTION} must be a boolean"
            )
        if proven_root_rigid_bake and asset_kind != "hierarchical":
            raise ValueError(
                "proven root-rigid bake is supported only for hierarchical W3D conversion"
            )
        proven_pivot_only_model = options.get(
            W3D_PROVEN_PIVOT_ONLY_MODEL_OPTION, False
        )
        if not isinstance(proven_pivot_only_model, bool):
            raise ValueError(
                f"W3D options.{W3D_PROVEN_PIVOT_ONLY_MODEL_OPTION} must be a boolean"
            )
        if proven_pivot_only_model and asset_kind != "hierarchical":
            raise ValueError(
                "proven pivot-only model is supported only for hierarchical W3D conversion"
            )
        if proven_pivot_only_model and proven_root_rigid_bake:
            raise ValueError(
                "proven pivot-only model cannot combine with proven root-rigid bake"
            )
        retail_absent_textures = normalize_retail_absent_textures(
            options.get(W3D_RETAIL_ABSENT_TEXTURES_OPTION, [])
        )
        if asset_kind != "animated" and required_equipment:
            raise ValueError(
                f"w3d-{asset_kind} does not accept options.required_equipment"
            )
        if self._w3d_batch_tools is None:
            raise RuntimeError("W3D conversion requires an active attested batch")
        blender = Path(self._w3d_batch_tools["blender"])
        plugin = Path(self._w3d_batch_tools["plugin"])

        job_root = self.jobs_root / profile_id / "w3d" / asset_id
        if job_root.exists():
            shutil.rmtree(job_root)
        input_root = job_root / "input"
        staged_digests: dict[str, str] = {}
        copied = _stage_w3d_sources(staging_sources, input_root, staged_digests)
        model = copied.get(model_name)
        if not model:
            raise FileNotFoundError(
                f"W3D model was not selected by the profile: {model_name}"
            )
        no_motion_proof = _prepare_w3d_no_motion_animations(
            copied,
            model,
            options.get(W3D_PROVEN_NO_MOTION_ANIMATIONS_OPTION),
        )
        try:
            secondary_skin_proof = _prepare_w3d_secondary_skin_streams(copied, model)
        except RuntimeError as exc:
            raise RuntimeError(
                f"W3D secondary-skin preparation failed for asset '{asset_id}' "
                f"model '{model.name}': {exc}"
            ) from exc
        texture_override_proof = _apply_w3d_texture_overrides(
            copied,
            model,
            options.get(W3D_TEXTURE_OVERRIDES_OPTION),
        )
        animations: list[Path] = []
        effective_animation_names: list[str] = []
        empty_placeholder_animations: list[str] = []
        for name in animation_names:
            animation = copied.get(name)
            if not animation:
                raise FileNotFoundError(
                    f"W3D animation was not selected by the profile: {name}"
                )
            # Retail ships a handful of zero-byte W3D placeholders (e.g.
            # rugimli_idlg.w3d, guboromir_dieb.w3d). Importing them creates no
            # owned action and used to surface as a misleading owner-rig error.
            # Drop them from the conversion set with explicit evidence; do not
            # invent clips.
            if animation.stat().st_size == 0:
                empty_placeholder_animations.append(name)
                continue
            animations.append(animation)
            effective_animation_names.append(name)
        if asset_kind == "animated" and not animations:
            raise ValueError(
                "w3d-bundle requires at least one non-empty animation; "
                "all declared clips were zero-byte retail placeholders: "
                + ", ".join(empty_placeholder_animations)
            )
        # Cache / finalize must key on the clips actually converted.
        animation_names = effective_animation_names

        target = _safe_output(pack_root, output)
        target.parent.mkdir(parents=True, exist_ok=True)
        adapter = repo_root_from_module() / "importer" / "blender" / "w3d_to_glb.py"
        command = [
            str(blender),
            "--factory-startup",
            "-noaudio",
            "--background",
            "--python-use-system-env",
            "--python-exit-code",
            "1",
            "--python",
            str(adapter),
            "--",
            "--plugin-root",
            str(plugin),
            "--model",
            str(model),
            "--asset-kind",
            asset_kind,
            *(["--proven-root-rigid-bake"] if proven_root_rigid_bake else []),
            *(["--proven-pivot-only-model"] if proven_pivot_only_model else []),
            "--output",
            str(target),
            "--animations",
            *[str(path) for path in animations],
            "--required-equipment",
            *required_equipment,
            "--excluded-optional-meshes",
            *excluded_optional_meshes,
            "--retail-absent-textures",
            *retail_absent_textures,
        ]
        # The three preparation steps above are the only writers into the job
        # root after staging, and each one returns None WITHOUT writing when it
        # has nothing to do. So when all three declined, the staged bytes are
        # still exactly the bytes the copy hashed, and re-reading the whole job
        # root to learn that again is pure duplicate IO - which is most of what
        # a warm W3D cache-hit job was spending its time on. Any preparation at
        # all, and the job root is re-hashed from disk exactly as before: the
        # cache key must describe the bytes Blender will actually be handed.
        job_root_unmodified = (
            no_motion_proof is None
            and secondary_skin_proof is None
            and texture_override_proof is None
        )
        source_hashes = {
            name: (
                staged_digests[name]
                if job_root_unmodified and name in staged_digests
                else sha256_file(path)
            )
            for name, path in sorted(
                copied.items(), key=lambda item: (item[0].casefold(), item[0])
            )
        }
        force_reconvert = reconvert_requested(asset_id, self.reconvert_only)
        adapter_bundle_sha256 = (
            sha256_file(adapter)
            + sha256_file(
                repo_root_from_module()
                / "importer"
                / "blender"
                / "w3d_multi_to_glb.py"
            )
        )
        # An explicit scoped run is an operator-owned escape hatch: selected
        # assets retain exact adapter identity and are forced cold; siblings
        # use a stable scoped identity so an unrelated adapter edit does not
        # evict the full corpus. Provenance records that this was partial.
        adapter_cache_identity = w3d_adapter_cache_identity(
            adapter_bundle_sha256, asset_id, self.reconvert_only
        )
        cache_key = _w3d_conversion_cache_key(
            source_hashes=source_hashes,
            adapter_sha256=adapter_cache_identity,
            plugin_attestation_sha256=str(
                self._w3d_batch_tools["plugin_attestation_sha256"]
            ),
            blender_tree_sha256=str(self._w3d_batch_tools["blender_tree_sha256"]),
            logical={
                "asset_kind": asset_kind,
                "model_name": model_name,
                "animation_names": list(animation_names),
                "required_equipment": list(required_equipment),
                "excluded_optional_meshes": list(excluded_optional_meshes),
                "proven_root_rigid_bake": proven_root_rigid_bake,
                "options": json.loads(
                    json.dumps(options, sort_keys=True, separators=(",", ":"))
                ),
            },
        )
        combined_log: str | None
        if force_reconvert:
            with self._conversion_cache_lock:
                self._conversion_cache_stats["forced"] += 1
            combined_log = None
        else:
            with self._w3d_cache_lock(cache_key):
                combined_log = self._copy_w3d_cache_hit(cache_key, target)
        return {
            "index": index,
            "asset_id": asset_id,
            "profile_id": profile_id,
            "asset_kind": asset_kind,
            "model_name": model_name,
            "animation_names": animation_names,
            "empty_placeholder_animations": empty_placeholder_animations,
            "required_equipment": required_equipment,
            "excluded_optional_meshes": excluded_optional_meshes,
            "proven_root_rigid_bake": proven_root_rigid_bake,
            "proven_pivot_only_model": proven_pivot_only_model,
            "retail_absent_textures": retail_absent_textures,
            "model": model,
            "animations": animations,
            "target": target,
            "copied": copied,
            "options": options,
            "pack_root": pack_root,
            "report_relative_path": report_relative_path,
            "no_motion_proof": no_motion_proof,
            "secondary_skin_proof": secondary_skin_proof,
            "texture_override_proof": texture_override_proof,
            "cache_key": cache_key,
            "command": command,
            "job_root": job_root,
            "cache_hit": combined_log is not None,
            "force_reconvert": force_reconvert,
            "combined_log": combined_log or "",
        }

    def _finalize_w3d_bundle_job(
        self,
        prepared: Mapping[str, Any],
        combined_log: str,
        *,
        cache_hit: bool,
    ) -> list[Path]:
        """Validate GLB + adapter report and write metrics/cache."""

        target = Path(prepared["target"])
        pack_root = Path(prepared["pack_root"])
        unsupported = [
            line
            for line in combined_log.splitlines()
            if "not supported" in line.casefold()
        ]
        if unsupported:
            raise RuntimeError(
                f"W3D conversion emitted {len(unsupported)} unsupported-feature warning(s)"
            )
        missing_textures = [
            line
            for line in combined_log.splitlines()
            if "texture not found" in line.casefold()
        ]
        if missing_textures:
            raise RuntimeError(
                f"W3D conversion reported {len(missing_textures)} missing texture(s)"
            )
        if "OPENBFME_W3D_OK" not in combined_log:
            raise RuntimeError("W3D adapter did not emit its success marker")
        pivot_only_model = bool(prepared.get("proven_pivot_only_model", False))
        if not target.is_file() or (
            not pivot_only_model and target.stat().st_size < 1024
        ):
            raise RuntimeError(
                f"W3D adapter did not create a substantial GLB: {target}"
            )
        marker_lines = [
            line
            for line in combined_log.splitlines()
            if line.startswith("OPENBFME_W3D_OK ")
        ]
        if len(marker_lines) != 1:
            raise RuntimeError("W3D adapter emitted an ambiguous success report")
        report = json.loads(marker_lines[0].split(" ", 1)[1])
        metrics = _validated_w3d_metadata(
            report,
            prepared["required_equipment"],
            expected_animation_count=len(prepared["animation_names"]),
            asset_kind=prepared["asset_kind"],
            expected_excluded_optional_meshes=prepared["excluded_optional_meshes"],
            expected_proven_root_rigid_bake=prepared["proven_root_rigid_bake"],
            expected_pivot_only_model=prepared.get("proven_pivot_only_model", False),
            expected_embedded_model_animation=(
                prepared["asset_kind"] == "animated"
                and len(prepared["animation_names"]) == 1
                and prepared["animation_names"][0] == prepared["model_name"]
            ),
        )
        if pivot_only_model:
            # Pivot carriers intentionally export hierarchy/skin data without
            # a mesh and can be smaller than the ordinary-art size floor.  Do
            # not accept them on size alone: independently parse the GLB and
            # prove the adapter's exact zero-geometry and hierarchy counts.
            adapter_metrics = metrics["metrics"]
            validate_w3d_glb_semantics(
                target,
                {
                    "mesh_count": 0,
                    "vertex_count": 0,
                    "triangle_count": 0,
                    "skin_count": adapter_metrics["skeletonCount"],
                    "joint_count": adapter_metrics["boneCount"],
                    "animation_count": adapter_metrics["animationCount"],
                },
            )
        validated_texture_overrides = _validate_w3d_texture_override_glb(
            target,
            prepared["copied"],
            prepared["texture_override_proof"],
        )
        if validated_texture_overrides is not None:
            metrics["textureOverrides"] = validated_texture_overrides
            metrics["capabilities"]["declaredTextureOverridesAppliedAndValidated"] = (
                True
            )
            metrics["metrics"]["textureOverrideCount"] = len(
                validated_texture_overrides["entries"]
            )
        secondary_skin_proof = prepared["secondary_skin_proof"]
        if secondary_skin_proof is not None:
            metrics["secondarySkinStreams"] = secondary_skin_proof
            retained = secondary_skin_proof.get("retained") is True
            metrics["capabilities"][
                "secondarySkinStreamsProvenEquivalentAndRemoved"
            ] = not retained
            if retained:
                metrics["capabilities"][
                    "secondarySkinStreamsRetainedWithUnprovenRedundancy"
                ] = True
            metrics["metrics"]["secondarySkinTransformedMeshCount"] = (
                secondary_skin_proof["transformedMeshCount"]
            )
            metrics["metrics"]["secondarySkinRemovedByteCount"] = secondary_skin_proof[
                "removedByteCount"
            ]
        no_motion_proof = prepared["no_motion_proof"]
        if no_motion_proof is not None:
            metrics["noMotionAnimations"] = no_motion_proof
            metrics["capabilities"]["headerOnlyNoMotionAnimationsProvenAndRemoved"] = (
                True
            )
            metrics["metrics"]["noMotionAnimationCount"] = no_motion_proof[
                "removedContainerCount"
            ]
            metrics["metrics"]["noMotionRemovedByteCount"] = no_motion_proof[
                "removedByteCount"
            ]
        empty_placeholders = list(prepared.get("empty_placeholder_animations") or [])
        if empty_placeholders:
            metrics["emptyPlaceholderAnimations"] = empty_placeholders
            metrics["capabilities"]["zeroByteRetailAnimationPlaceholdersExcluded"] = True
            metrics["metrics"]["emptyPlaceholderAnimationCount"] = len(
                empty_placeholders
            )
        metrics_path = _safe_output(pack_root, prepared["report_relative_path"])
        write_json_atomic(metrics_path, metrics)
        if not cache_hit:
            with self._w3d_cache_lock(str(prepared["cache_key"])):
                self._populate_w3d_cache(
                    prepared["cache_key"], target, combined_log
                )
        return [target, metrics_path]

    def _convert_w3d_bundle(
        self,
        staging_sources: list[Path],
        output: str | None,
        options: dict[str, Any],
        pack_root: Path,
        profile_id: str,
        asset_id: str,
        asset_kind: str,
    ) -> list[Path]:
        prepared = self._prepare_w3d_bundle_job(
            -1,
            staging_sources,
            output,
            options,
            pack_root,
            profile_id,
            asset_id,
            asset_kind,
        )
        if prepared["cache_hit"]:
            return self._finalize_w3d_bundle_job(
                prepared, prepared["combined_log"], cache_hit=True
            )
        isolated_environment = _isolated_blender_environment(
            os.environ, prepared["job_root"]
        )
        with self._w3d_cache_lock(prepared["cache_key"]):
            # Re-check cache under lock in case a peer filled it.
            combined_log = (
                None
                if prepared.get("force_reconvert")
                else self._copy_w3d_cache_hit(
                    prepared["cache_key"], prepared["target"]
                )
            )
            if combined_log is None:
                result = run_checked(prepared["command"], env=isolated_environment)
                combined_log = result.stdout + "\n" + result.stderr
                cache_hit = False
            else:
                cache_hit = True
        return self._finalize_w3d_bundle_job(
            prepared, combined_log, cache_hit=cache_hit
        )

    def _write_runtime_data(
        self, pack_root: Path, runtime_data: dict[str, Any]
    ) -> None:
        for relative, value in sorted(runtime_data.items()):
            target = _safe_output(pack_root, relative)
            if target.suffix.casefold() != ".json":
                raise ValueError(f"runtime_data output must be JSON: {relative}")
            write_json_atomic(target, value)


def _is_sha256(value: Any) -> bool:
    return (
        isinstance(value, str)
        and len(value) == 64
        and all(character in "0123456789abcdef" for character in value.casefold())
    )


def _is_git_commit(value: Any) -> bool:
    return (
        isinstance(value, str)
        and len(value) == 40
        and all(character in "0123456789abcdef" for character in value.casefold())
    )


def _audit_recipe(recipe: Any, errors: list[str]) -> str:
    if not isinstance(recipe, dict):
        errors.append("retail provenance importer_recipe is not an object")
        return ""
    declared = recipe.get("tree_sha256")
    files = recipe.get("files")
    if not _is_sha256(declared):
        errors.append("retail provenance importer recipe has an invalid tree digest")
    if not isinstance(files, list) or not files:
        errors.append("retail provenance importer recipe has no file inventory")
        return str(declared or "")
    digest = hashlib.sha256()
    seen: set[str] = set()
    for item in files:
        if (
            not isinstance(item, dict)
            or not isinstance(item.get("path"), str)
            or not isinstance(item.get("size"), int)
            or item.get("size", -1) < 0
            or not _is_sha256(item.get("sha256"))
        ):
            errors.append("invalid importer recipe file entry")
            continue
        relative = item["path"]
        try:
            safe_relative_parts(relative)
        except ValueError as exc:
            errors.append(f"invalid importer recipe path: {exc}")
            continue
        folded = relative.casefold()
        if folded in seen:
            errors.append(f"duplicate importer recipe path: {relative}")
            continue
        seen.add(folded)
        digest.update(relative.encode("utf-8"))
        digest.update(b"\0")
        digest.update(str(item["size"]).encode("ascii"))
        digest.update(b"\0")
        digest.update(item["sha256"].encode("ascii"))
        digest.update(b"\n")
    if _is_sha256(declared) and digest.hexdigest() != declared:
        errors.append("importer recipe tree digest disagrees with its file inventory")
    if not _is_git_commit(recipe.get("git_commit")):
        errors.append("retail provenance importer recipe has no exact git commit")
    if not isinstance(recipe.get("git_worktree_clean"), bool):
        errors.append("retail provenance importer recipe lacks worktree state")
    provenance_source = recipe.get("provenance_source")
    if (
        not isinstance(provenance_source, str)
        or provenance_source not in PROVENANCE_SOURCES
    ):
        # Recipes written before provenance self-description landed reach here.
        # That is not a bug in the auditor: such a recipe may carry a commit
        # inherited from whatever checkout happened to enclose the bundle, which
        # is the exact defect these fields exist to rule out. There is no lax
        # mode for them on purpose - a second, weaker audit path is how the
        # guarantee quietly dies. Re-import is the migration.
        errors.append(
            "retail provenance importer recipe does not declare how its commit "
            "was established; a recipe predating provenance self-description "
            "cannot be verified - re-import this pack"
        )
    elif provenance_source == PROVENANCE_SOURCE_RELEASE_IDENTITY:
        # A stamp the inventory does not cover attests to nothing.
        if RELEASE_IDENTITY_RELATIVE.casefold() not in seen:
            errors.append(
                "retail provenance importer recipe claims a release identity "
                "it did not hash"
            )
    requirements = recipe.get("requirements_files")
    if (
        not isinstance(requirements, list)
        or not requirements
        or not all(isinstance(item, str) and item for item in requirements)
    ):
        errors.append(
            "retail provenance importer recipe does not name the requirements "
            "pin it hashed; a recipe predating provenance self-description "
            "cannot be verified - re-import this pack"
        )
    elif any(item.casefold() not in seen for item in requirements):
        errors.append(
            "retail provenance importer recipe names a requirements pin it did "
            "not hash"
        )
    return str(declared or "")


def _audit_tool_attestations(tools: Any, profile: str, errors: list[str]) -> int:
    if not isinstance(tools, dict):
        errors.append("retail provenance tools is not an object")
        return 0
    if profile != "men-fords-v0":
        return len(tools)
    from .bootstrap import (
        BLENDER_EXE_SHA256,
        BLENDER_TREE_SHA256,
        FFMPEG_EXE_SHA256,
        FFPROBE_EXE_SHA256,
        PILLOW_TREE_SHA256,
        PLUGIN_COMMIT,
        PLUGIN_SUBMODULE_COMMIT,
        PYTHON_BASE_DLL_SHA256,
        PYTHON_LAUNCHER_SHA256,
        PYTHON_RUNTIME_TREE_SHA256,
        PYTHON_VERSION,
    )

    required = {"blender", "ffmpeg", "opensage_w3d_plugin", "pillow", "python"}
    missing = sorted(required - set(tools))
    if missing:
        errors.append(
            "retail provenance is missing tool attestations: " + ", ".join(missing)
        )
        return len(tools)
    blender = tools.get("blender", {})
    if (
        not isinstance(blender, dict)
        or blender.get("sha256") != BLENDER_EXE_SHA256
        or blender.get("tree_sha256") != BLENDER_TREE_SHA256
    ):
        errors.append("retail provenance Blender attestation does not match the pin")
    ffmpeg = tools.get("ffmpeg", {})
    if (
        not isinstance(ffmpeg, dict)
        or ffmpeg.get("sha256") != FFMPEG_EXE_SHA256
        or ffmpeg.get("ffprobe_sha256") != FFPROBE_EXE_SHA256
    ):
        errors.append("retail provenance FFmpeg attestation does not match the pin")
    plugin = tools.get("opensage_w3d_plugin", {})
    if (
        not isinstance(plugin, dict)
        or plugin.get("commit") != PLUGIN_COMMIT
        or plugin.get("submodule_commit") != PLUGIN_SUBMODULE_COMMIT
        or plugin.get("worktree_clean") is not True
        or plugin.get("python_bytecode_free") is not True
    ):
        errors.append(
            "retail provenance OpenSAGE plugin attestation does not match the pin"
        )
    pillow = tools.get("pillow", {})
    if (
        not isinstance(pillow, dict)
        or pillow.get("version") != "12.2.0"
        or pillow.get("tree_sha256") != PILLOW_TREE_SHA256
    ):
        errors.append("retail provenance Pillow attestation does not match the pin")
    python = tools.get("python", {})
    if (
        not isinstance(python, dict)
        or python.get("version") != PYTHON_VERSION
        or python.get("launcher_sha256") != PYTHON_LAUNCHER_SHA256
        or python.get("base_dll_sha256") != PYTHON_BASE_DLL_SHA256
        or python.get("tree_sha256") != PYTHON_RUNTIME_TREE_SHA256
        or not isinstance(python.get("file_count"), int)
        or python.get("file_count", 0) <= 0
        or not isinstance(python.get("total_bytes"), int)
        or python.get("total_bytes", 0) <= 0
        or not isinstance(python.get("excludes"), list)
    ):
        errors.append(
            "retail provenance Python runtime attestation does not match the pin"
        )
    return len(tools)


def _audit_retail_provenance(
    manifest: dict[str, Any],
    pack_data: dict[str, Any],
    errors: list[str],
) -> dict[str, Any]:
    """Apply the semantic contract only to importer-generated local packs.

    Generic/mod packs remain eligible for the structural whole-tree audit, but
    ``local_retail_import=true`` is a non-downgradable promise that requires the
    source, recipe, and tool attestations below.
    """
    summary = {
        "semantic_provenance": False,
        "provenance_contract": str(manifest.get("contract", "")),
        "profile": str(manifest.get("profile", "")),
        "profile_sha256": str(manifest.get("profile_sha256", "")),
        "importer_recipe_sha256": "",
        "source_archive_count": 0,
        "provenance_entry_count": 0,
        "tool_attestation_count": 0,
    }
    if pack_data.get("local_retail_import") is not True:
        return summary

    if pack_data.get("provenance_contract") != RETAIL_PROVENANCE_CONTRACT:
        errors.append("local retail pack is missing its provenance contract marker")
    if manifest.get("contract") != RETAIL_PROVENANCE_CONTRACT:
        errors.append("retail provenance contract is missing or unsupported")
    if manifest.get("format") != 1:
        errors.append("retail provenance format is unsupported")
    profile = manifest.get("profile")
    if not isinstance(profile, str) or not profile:
        errors.append("retail provenance profile is missing")
        profile = ""
    if not _is_sha256(manifest.get("profile_sha256")):
        errors.append("retail provenance profile digest is invalid")
    if not isinstance(manifest.get("importer_version"), str) or not manifest.get(
        "importer_version"
    ):
        errors.append("retail provenance importer version is missing")
    if manifest.get("source_game") not in {
        "bfme2-retail-user-owned",
        "rotwk-retail-user-owned",
    }:
        errors.append("retail provenance source game is invalid")
    if manifest.get("redistributable") is not False:
        errors.append("retail provenance must declare redistributable=false")

    summary["importer_recipe_sha256"] = _audit_recipe(
        manifest.get("importer_recipe"), errors
    )
    summary["tool_attestation_count"] = _audit_tool_attestations(
        manifest.get("tools"), str(profile), errors
    )

    archives = manifest.get("source_archives")
    archive_names: set[str] = set()
    if not isinstance(archives, list):
        errors.append("retail provenance source_archives is not an array")
        archives = []
    for archive in archives:
        if (
            not isinstance(archive, dict)
            or not isinstance(archive.get("relative_path"), str)
            or not isinstance(archive.get("size"), int)
            or archive.get("size", -1) < 16
            or not _is_sha256(archive.get("sha256"))
        ):
            errors.append("invalid retail source archive attestation")
            continue
        relative = archive["relative_path"]
        try:
            safe_relative_parts(relative)
        except ValueError as exc:
            errors.append(f"invalid retail source archive path: {exc}")
            continue
        key = relative.casefold()
        if key in archive_names:
            errors.append(f"duplicate retail source archive attestation: {relative}")
            continue
        archive_names.add(key)
        if profile == "men-fords-v0":
            expected = KNOWN_SLICE_ARCHIVE_SHA256.get(key)
            if (
                expected is None
                or archive.get("expected_sha256") != expected
                or archive.get("sha256") != expected
                or archive.get("matches_reference") is not True
            ):
                errors.append(
                    f"retail source archive does not match the Fords pin: {relative}"
                )
    summary["source_archive_count"] = len(archive_names)
    if profile == "men-fords-v0" and archive_names != set(KNOWN_SLICE_ARCHIVE_SHA256):
        errors.append("Fords provenance source archive closure is incomplete")

    incomplete = manifest.get("incomplete")
    if not isinstance(incomplete, list):
        errors.append("retail provenance incomplete field is not an array")
    elif bool(pack_data.get("profile_build_complete", False)) == bool(incomplete):
        errors.append(
            "pack completion flag disagrees with provenance incomplete reasons"
        )

    entries = manifest.get("entries")
    if not isinstance(entries, list):
        errors.append("retail provenance entries is not an array")
        entries = []
    conversion_keys: set[tuple[str, str, str, str]] = set()
    for entry in entries:
        source = entry.get("source") if isinstance(entry, dict) else None
        if (
            not isinstance(entry, dict)
            or not isinstance(entry.get("resource_id"), str)
            or not entry.get("resource_id", "").strip()
            or not isinstance(entry.get("kind"), str)
            or not isinstance(entry.get("converter"), str)
            or not entry.get("converter", "").strip()
            or not isinstance(source, dict)
            or not isinstance(source.get("archive"), str)
            or not isinstance(source.get("virtual_path"), str)
            or not isinstance(source.get("offset"), int)
            or source.get("offset", -1) < 0
            or not isinstance(source.get("size"), int)
            or source.get("size", -1) < 0
            or not _is_sha256(source.get("sha256"))
            or not isinstance(source.get("cache_key"), str)
            or len(source.get("cache_key", "")) != 20
        ):
            errors.append("invalid retail provenance source entry")
            continue
        try:
            safe_relative_parts(source["archive"])
            safe_relative_parts(source["virtual_path"])
        except ValueError as exc:
            errors.append(f"invalid retail provenance source path: {exc}")
            continue
        key = (
            source["archive"].casefold(),
            source["virtual_path"].casefold(),
            entry["resource_id"].casefold(),
            entry["converter"].casefold(),
        )
        if key in conversion_keys:
            errors.append(
                "duplicate retail provenance conversion entry: "
                f"{source['archive']}:{source['virtual_path']} "
                f"resource={entry['resource_id']} converter={entry['converter']}"
            )
        conversion_keys.add(key)
        if source["archive"].casefold() not in archive_names:
            errors.append(
                f"retail provenance source uses an unattested archive: {source['archive']}"
            )
    summary["provenance_entry_count"] = len(entries)
    if profile == "men-fords-v0" and len(entries) != MEN_FORDS_SOURCE_ENTRY_COUNT:
        errors.append(
            "Fords provenance must contain exactly "
            f"{MEN_FORDS_SOURCE_ENTRY_COUNT} sources, found {len(entries)}"
        )

    summary["semantic_provenance"] = not errors
    return summary


def audit_pack(
    pack_root: Path | str,
    *,
    light: bool | None = None,
    known_digests: Mapping[Path, str] | None = None,
) -> dict[str, Any]:
    """Audit pack outputs against provenance.

    *light* (or OPENBFME_DEV / OPENBFME_DEV_AUDIT=light): verify path + size only,
    skip per-file SHA-256 rehash of the full pack (dev iteration speed).

    *known_digests* maps absolute file paths to SHA-256 values the CALLER read
    off this same tree, in this same process, moments ago - the publish path
    hashes the destination once to fold a bundle digest and hands the result
    here so the audit does not read every byte a second time. This is a
    de-duplication of one read, not a relaxation: any path absent from the
    mapping is still hashed here, and the values are still compared against the
    provenance inventory exactly as before. It is NOT a place to pass digests
    that came from a manifest, a cache, or a previous run.
    """

    root = Path(pack_root).expanduser().resolve()
    if light is None:
        light = os.environ.get("OPENBFME_DEV", "").strip().casefold() in {
            "1",
            "true",
            "yes",
        } or os.environ.get("OPENBFME_DEV_AUDIT", "").strip().casefold() in {
            "1",
            "true",
            "yes",
            "light",
        }
    manifest_path = root / "provenance" / "manifest.json"
    errors: list[str] = []
    checked = 0
    if not (root / "pack.json").is_file():
        errors.append("missing pack.json")
    if not manifest_path.is_file():
        errors.append("missing provenance/manifest.json")
        return {
            "valid": False,
            "checked_files": 0,
            "checked_outputs": 0,
            "errors": errors,
        }
    try:
        with manifest_path.open("r", encoding="utf-8") as stream:
            manifest = json.load(stream)
    except (OSError, json.JSONDecodeError) as exc:
        return {
            "valid": False,
            "checked_files": 0,
            "checked_outputs": 0,
            "errors": [f"invalid provenance manifest: {exc}"],
        }
    if not isinstance(manifest, dict):
        return {
            "valid": False,
            "checked_files": 0,
            "checked_outputs": 0,
            "errors": ["invalid provenance manifest: root is not an object"],
        }
    pack_data: dict[str, Any] = {}
    try:
        loaded_pack_data = read_json(root / "pack.json")
        if not isinstance(loaded_pack_data, dict):
            raise AttributeError("root is not an object")
        pack_data = loaded_pack_data
        if pack_data.get("redistributable", True) is not False:
            errors.append("retail pack must declare redistributable=false")
    except (OSError, json.JSONDecodeError, AttributeError) as exc:
        errors.append(f"invalid pack.json: {exc}")

    provenance_summary = _audit_retail_provenance(manifest, pack_data, errors)

    inventory = manifest.get("bundle_files", [])
    if not isinstance(inventory, list):
        inventory = []
        errors.append("provenance bundle_files is not an array")
    expected: dict[str, dict[str, Any]] = {}
    to_hash: list[tuple[Path, str, str]] = []
    for item in inventory:
        if (
            not isinstance(item, dict)
            or not isinstance(item.get("path"), str)
            or not isinstance(item.get("size"), int)
            or not isinstance(item.get("sha256"), str)
            or len(item["sha256"]) != 64
        ):
            errors.append("invalid bundle_files entry")
            continue
        relative = item["path"]
        if relative in expected:
            errors.append(f"duplicate bundle inventory path: {relative}")
            continue
        try:
            target = _safe_output(root, relative, root_is_resolved=True)
        except ValueError as exc:
            errors.append(str(exc))
            continue
        expected[relative] = item
        if not target.is_file() or _is_link_like(target):
            errors.append(f"missing or linked bundle file: {relative}")
            continue
        checked += 1
        if target.stat().st_size != item.get("size"):
            errors.append(f"size mismatch: {relative}")
        elif not light:
            to_hash.append((target, relative, str(item.get("sha256"))))

    # Hash the size-matching bundle files in one parallel pass. Errors are
    # collected into the same list and re-sorted below, so the reported set is
    # identical to the previous file-at-a-time ordering.
    if to_hash:
        supplied = dict(known_digests or {})
        pending = [target for target, _, _ in to_hash if target not in supplied]
        digests = _hash_files(pending)
        digests.update(
            {
                target: supplied[target]
                for target, _, _ in to_hash
                if target in supplied
            }
        )
        for target, relative, want in to_hash:
            if digests[target] != want:
                errors.append(f"hash mismatch: {relative}")

    excluded = {"provenance/manifest.json", "provenance/audit.json"}
    actual: set[str] = set()
    for path in _pack_files(root):
        if _is_link_like(path):
            errors.append(
                f"symbolic link or junction in pack: {path.relative_to(root).as_posix()}"
            )
        if path.is_file():
            relative = path.relative_to(root).as_posix()
            if relative not in excluded:
                actual.add(relative)
    for relative in sorted(actual - set(expected)):
        errors.append(f"unexpected bundle file: {relative}")
    for relative in sorted(set(expected) - actual):
        errors.append(f"missing bundle file: {relative}")

    declared_outputs: set[str] = set()
    entries = manifest.get("entries", [])
    if not isinstance(entries, list):
        entries = []
        errors.append("provenance entries is not an array")
    for entry in entries:
        if not isinstance(entry, dict) or not isinstance(
            entry.get("outputs", []), list
        ):
            errors.append("invalid provenance resource entry")
            continue
        for output in entry.get("outputs", []):
            if (
                not isinstance(output, dict)
                or not isinstance(output.get("path"), str)
                or not isinstance(output.get("size"), int)
                or not isinstance(output.get("sha256"), str)
            ):
                errors.append("invalid converted output entry")
                continue
            try:
                relative = output["path"]
                _safe_output(root, relative, root_is_resolved=True)
            except (KeyError, TypeError, ValueError) as exc:
                errors.append(str(exc))
                continue
            declared_outputs.add(relative)
            if relative not in expected:
                errors.append(
                    f"converted output absent from bundle inventory: {relative}"
                )
                continue
            inventory_item = expected[relative]
            if output["size"] != inventory_item.get("size"):
                errors.append(
                    f"converted output size disagrees with bundle inventory: {relative}"
                )
            if output["sha256"] != inventory_item.get("sha256"):
                errors.append(
                    f"converted output hash disagrees with bundle inventory: {relative}"
                )
    unique_errors = sorted(set(errors))
    return {
        "valid": not unique_errors,
        "light": bool(light),
        "checked_files": checked,
        "checked_outputs": len(declared_outputs),
        "errors": unique_errors,
        **provenance_summary,
    }
