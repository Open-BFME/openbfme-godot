"""Transactional source extraction, conversion, pack assembly and audit."""

from __future__ import annotations

from collections import defaultdict
from dataclasses import asdict
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
from typing import Any, Mapping

from .big import sha256_file
from .catalog import CatalogEntry, InstallCatalog, KNOWN_SLICE_ARCHIVE_SHA256
from .paths import ensure_external_to_repo, repo_root_from_module, safe_relative_parts
from .profile import (
    ResolvedProfile,
    ResolvedResource,
    SLUG_PATTERN,
    W3D_EXCLUDED_OPTIONAL_MESHES_OPTION,
    W3D_INPUT_RESOURCE_IDS_OPTION,
    normalize_excluded_optional_meshes,
    normalize_texture_atlas_crops,
)
from .tools import (
    directory_tree_sha256,
    discover_executable,
    git_revision,
    git_worktree_clean,
    inspect_tool,
    run_checked,
)
from .util import read_json, write_json_atomic
from .version import __version__


RETAIL_PROVENANCE_CONTRACT = "openbfme.retail-import-provenance-v1"
MEN_FORDS_SOURCE_ENTRY_COUNT = 264
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
    "weighted-hand-group",
    "rest-pose-proximity",
    "weighted-hand-dominance",
}
RESOURCE_BUNDLE_CONVERTERS = {
    "w3d-bundle",
    "w3d-hierarchical",
    "w3d-static",
    "sage-terrain-materials",
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
        raise ValueError("w3d-bundle options.required_equipment must be an array of strings")
    if len(value) != len(set(value)):
        raise ValueError("w3d-bundle options.required_equipment contains duplicates")
    unsupported = sorted(set(value) - W3D_EQUIPMENT_ROLES)
    if unsupported:
        raise ValueError("unsupported required W3D equipment semantics: " + ", ".join(unsupported))
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


def _validated_w3d_metadata(
    report: Mapping[str, Any],
    required_equipment: list[str],
    *,
    expected_animation_count: int | None = None,
    asset_kind: str = "animated",
    expected_excluded_optional_meshes: list[str] | None = None,
) -> dict[str, Any]:
    """Validate the private adapter report and return payload-free bundle facts."""

    if not isinstance(report, Mapping):
        raise RuntimeError("W3D adapter report root is not an object")
    if (
        report.get("report_schema") != W3D_ADAPTER_REPORT_CONTRACT
        or report.get("report_version") != 1
    ):
        raise RuntimeError("W3D adapter report contract is unsupported")
    expected_required = _normalize_required_equipment(required_equipment)
    if asset_kind not in {"animated", "hierarchical", "static"}:
        raise ValueError(f"unsupported W3D asset kind: {asset_kind}")
    reported_asset_kind = report.get("asset_kind", "animated")
    if reported_asset_kind != asset_kind:
        raise RuntimeError("W3D adapter asset kind does not match the conversion request")
    if asset_kind in {"hierarchical", "static"} and expected_required:
        raise RuntimeError(f"{asset_kind} W3D conversion cannot require skeletal equipment")
    reported_required = _normalize_required_equipment(report.get("required_equipment"))
    if reported_required != expected_required:
        raise RuntimeError("W3D adapter did not enforce the requested equipment semantics")

    mesh_count = _report_int(report, "meshes", minimum=1)
    animated = asset_kind == "animated"
    skeletal = asset_kind in {"animated", "hierarchical"}
    animation_count = _report_int(report, "animations", minimum=1 if animated else 0)
    animation_curve_count = _report_int(
        report, "animation_curves", minimum=1 if animated else 0
    )
    animation_key_count = _report_int(
        report, "animation_keys", minimum=1 if animated else 0
    )
    bone_count = _report_int(report, "bones", minimum=1 if skeletal else 0)
    raw_skeleton_count = report.get("skeletons")
    if raw_skeleton_count is None:
        if asset_kind == "hierarchical":
            raise RuntimeError("hierarchical W3D adapter report has no skeleton count")
        skeleton_count = 1 if animated else 0
    else:
        skeleton_count = _report_int(report, "skeletons")
    expected_skeleton_count = 1 if skeletal else 0
    if skeleton_count != expected_skeleton_count:
        raise RuntimeError("W3D adapter skeleton count does not match the asset kind")
    vertex_count = _report_int(report, "vertices", minimum=1)
    triangle_count = _report_int(report, "triangles", minimum=1)
    skinned_mesh_count = _report_int(
        report, "skinned_meshes", minimum=1 if animated else 0
    )
    material_count = _report_int(report, "materials")
    image_count = _report_int(report, "images")
    generated_image_count = _report_int(report, "generated_images")
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
    if expected_animation_count is not None and animation_count != expected_animation_count:
        raise RuntimeError("W3D adapter animation count does not match the conversion request")
    if not animated and any(
        (animation_count, animation_curve_count, animation_key_count)
    ):
        raise RuntimeError(f"{asset_kind} W3D adapter report contains animation data")
    if asset_kind == "static" and any(
        (bone_count, skeleton_count, skinned_mesh_count)
    ):
        raise RuntimeError("static W3D adapter report contains skeletal data")
    if generated_image_count != 0:
        raise RuntimeError("W3D adapter report retains generated placeholder images")
    if remaining_non_render != 0:
        raise RuntimeError("W3D adapter report retains renderable collision or helper geometry")
    if remaining_ambiguous_boxes != 0:
        raise RuntimeError("W3D adapter report retains ambiguous box-shaped render geometry")

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
        raise RuntimeError("W3D adapter optional mesh exclusions do not match the request")
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
            raise RuntimeError("W3D adapter optional mesh exclusions do not match the request")
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
            raise RuntimeError("W3D adapter mesh proof methods are invalid or non-canonical")
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
                "weighted-hand-group",
                "rest-pose-proximity",
                "weighted-hand-dominance",
            }:
                raise RuntimeError("W3D equipment mesh lacks attachment evidence")
        skinned = raw.get("skinned")
        if not isinstance(skinned, bool):
            raise RuntimeError("W3D adapter mesh inventory has an invalid skinned flag")
        if asset_kind == "static" and (
            role != "character-mesh"
            or attachment != "scene"
            or proof_methods
            or skinned
        ):
            raise RuntimeError("static W3D mesh inventory contains skeletal semantics")
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
        raise RuntimeError("W3D adapter vertex metrics disagree with the mesh inventory")
    if sum(item["triangleCount"] for item in inventory) != triangle_count:
        raise RuntimeError("W3D adapter triangle metrics disagree with the mesh inventory")
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
        raise RuntimeError("W3D adapter equipment summary disagrees with its mesh inventory")
    if asset_kind == "static" and equipment:
        raise RuntimeError("static W3D adapter report contains skeletal equipment semantics")

    return {
        "schema": W3D_PRESENTATION_METADATA_CONTRACT,
        "schemaVersion": 0,
        "capabilities": {
            "animated": animated,
            "skeletal": skeletal,
            "nonRenderGeometryExcluded": True,
            "ambiguousBoxGeometryExcluded": True,
            "declaredOptionalRenderSubobjectsExcluded": bool(
                optional_mesh_exclusions
            ),
            "requiredEquipmentProven": all(role in equipment for role in expected_required),
            "equipmentAttachmentsCanonicalizedRestoredAndRevalidated": attachments_canonicalized_restored_and_revalidated,
        },
        "requiredEquipment": expected_required,
        "equipment": equipment,
        "excludedOptionalMeshes": optional_mesh_exclusions,
        "meshInventory": inventory,
        "metrics": {
            "meshCount": mesh_count,
            "animationCount": animation_count,
            "animationCurveCount": animation_curve_count,
            "animationKeyCount": animation_key_count,
            "boneCount": bone_count,
            "skeletonCount": skeleton_count,
            "vertexCount": vertex_count,
            "triangleCount": triangle_count,
            "skinnedMeshCount": skinned_mesh_count,
            "materialCount": material_count,
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
        if not name.upper().startswith("PYTHON") and not name.upper().startswith("BLENDER_")
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


def _entry_cache_key(entry: CatalogEntry) -> str:
    value = f"{entry.archive.casefold()}\n{entry.name.casefold()}\n{entry.offset}\n{entry.size}"
    return hashlib.sha256(value.encode("utf-8")).hexdigest()[:20]


def _source_cache_key(entry: CatalogEntry, source_sha256: str) -> str:
    value = f"{entry.archive.casefold()}\n{entry.name.casefold()}\n{source_sha256}"
    return hashlib.sha256(value.encode("utf-8")).hexdigest()[:20]


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
                raise RuntimeError(f"W3D input resource was not extracted: {entry.name}")
            unique[key] = Path(cached["source_path"])
    return sorted(unique.values(), key=lambda item: str(item).casefold())


def _stage_w3d_sources(sources: list[Path], input_root: Path) -> dict[str, Path]:
    """Flatten a proven W3D input closure and reject ambiguous basenames."""

    input_root.mkdir(parents=True)
    copied: dict[str, Path] = {}
    for source in sorted(sources, key=lambda item: str(item).casefold()):
        key = source.name.casefold()
        if key in copied:
            if sha256_file(copied[key]) != sha256_file(source):
                raise RuntimeError(f"flat W3D staging collision: {source.name}")
            continue
        target = input_root / source.name.casefold()
        shutil.copyfile(source, target)
        copied[key] = target
    return copied


def _safe_output(root: Path, relative: str) -> Path:
    parts = safe_relative_parts(relative)
    target = (root / Path(*parts)).resolve()
    try:
        target.relative_to(root.resolve())
    except ValueError as exc:
        raise ValueError(f"output path escaped pack root: {relative!r}") from exc
    return target


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
    entries = [
        asdict(item)
        for item in sorted(
            catalog.entries,
            key=lambda item: (
                item.precedence,
                item.archive.casefold(),
                item.archive,
                item.name.casefold(),
                item.name,
                item.offset,
                item.size,
            ),
        )
    ]
    catalog_identity = _canonical_value_sha256(
        {
            "format": InstallCatalog.FORMAT,
            "install_root": str(catalog.install_root),
            "archives": archives,
            "entries": entries,
        }
    )
    return catalog_identity, install_identity


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
            raise ValueError(
                f"catalog virtual path is not canonical: {entry.name!r}"
            )
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


def bundle_digest(pack_root: Path | str) -> str:
    root = Path(pack_root).expanduser().resolve()
    digest = hashlib.sha256()
    for path in sorted((item for item in root.rglob("*") if item.is_file()), key=lambda item: item.relative_to(root).as_posix()):
        relative = path.relative_to(root).as_posix()
        digest.update(relative.encode("utf-8"))
        digest.update(b"\0")
        digest.update(str(path.stat().st_size).encode("ascii"))
        digest.update(b"\0")
        digest.update(sha256_file(path).encode("ascii"))
        digest.update(b"\n")
    return digest.hexdigest()


def _canonical_pack_inventory(pack_root: Path) -> list[dict[str, Any]]:
    root = pack_root.resolve()
    excluded = {"provenance/manifest.json", "provenance/audit.json"}
    inventory: list[dict[str, Any]] = []
    for path in sorted(
        (item for item in root.rglob("*") if item.is_file()),
        key=lambda item: item.relative_to(root).as_posix(),
    ):
        if _is_link_like(path):
            raise RuntimeError(f"pack inventory refuses symbolic links: {path}")
        relative = path.relative_to(root).as_posix()
        safe_relative_parts(relative)
        if relative in excluded:
            continue
        inventory.append(
            {
                "path": relative,
                "size": path.stat().st_size,
                "sha256": sha256_file(path),
            }
        )
    return inventory


def _importer_recipe_report() -> dict[str, Any]:
    root = repo_root_from_module()
    candidates = [
        *sorted((root / "importer" / "openbfme_importer").rglob("*.py")),
        *sorted((root / "importer" / "blender").rglob("*.py")),
        root / "importer" / "requirements-win.txt",
        root / "tools" / "openbfme_import.py",
        root / "tools" / "bootstrap-importer-python.ps1",
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
        "git_commit": git_revision(root),
        "git_worktree_clean": git_worktree_clean(root),
    }


class ImportPipeline:
    def __init__(self, catalog: InstallCatalog, state_root: Path) -> None:
        self.catalog = catalog
        self.state_root = ensure_external_to_repo(state_root, repo_root_from_module())
        self.sources_root = self.state_root / "cache" / "sources"
        self.packs_root = self.state_root / "packs"
        self.reports_root = self.state_root / "reports"
        self.jobs_root = self.state_root / "jobs"
        self._blender_tree_verified = False
        self._python_runtime_report: dict[str, Any] = {}

    def plan_report(self, resolved: ResolvedProfile) -> dict[str, Any]:
        recipe = _importer_recipe_report()
        return {
            "format": 1,
            "profile": resolved.profile.id,
            "profile_sha256": resolved.profile.source_sha256,
            "importer_recipe_sha256": recipe["tree_sha256"],
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

        state_root = self.state_root.resolve()
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
            raise RuntimeError("effective asset cache root is missing, linked, or not a directory")

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
                "effective asset cache is missing files: " + ", ".join(missing_files[:5])
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
                raise RuntimeError(
                f"effective asset cache size mismatch: {entry.name}"
                )

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
        for archive_name in sorted(by_archive, key=lambda value: (value.casefold(), value)):
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
                (item.name.casefold(), item.offset, item.size): item for item in selected
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
            raise RuntimeError("effective asset manifest identity or totals do not match")
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
                self._refuse_link_descendants(
                    root, "effective extraction destination"
                )
                os.replace(root, backup)
            try:
                os.replace(staging, root)
            except BaseException:
                if had_previous and os.path.lexists(backup) and not os.path.lexists(root):
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
        return self._effective_asset_report(
            root, manifest_path, manifest, reused=False
        )

    def extract_sources(
        self,
        resolved: ResolvedProfile,
        *,
        force: bool = False,
        max_files: int = 10_000,
        max_bytes: int = 4 * 1024 * 1024 * 1024,
    ) -> dict[tuple[str, str], dict[str, Any]]:
        entries = resolved.selected_entries
        if len(entries) > max_files:
            raise RuntimeError(f"profile selects {len(entries)} files; limit is {max_files}")
        total = sum(entry.size for entry in entries)
        if total > max_bytes:
            raise RuntimeError(f"profile selects {total} bytes; limit is {max_bytes}")
        by_archive: dict[str, list[CatalogEntry]] = defaultdict(list)
        for entry in entries:
            by_archive[entry.archive].append(entry)

        result: dict[tuple[str, str], dict[str, Any]] = {}
        for archive_name in sorted(by_archive, key=str.casefold):
            archive = self.catalog.open_archive_for(by_archive[archive_name][0])
            archive_slug = hashlib.sha256(archive_name.casefold().encode()).hexdigest()[:12]
            archive_output = self.sources_root / archive_slug
            wanted = [self.catalog.as_entry(item) for item in by_archive[archive_name]]
            extracted = archive.extract(
                wanted,
                archive_output,
                max_files=max_files,
                max_bytes=max_bytes,
                overwrite=force,
            )
            catalog_by_key = {item.name.casefold(): item for item in by_archive[archive_name]}
            for item in extracted:
                catalog_entry = catalog_by_key[item.entry.key]
                result[(archive_name.casefold(), item.entry.key)] = {
                    "catalog": catalog_entry,
                    "source_path": item.output,
                    "source_sha256": item.sha256,
                    "cache_key": _source_cache_key(catalog_entry, item.sha256),
                }
        return result

    def build(
        self,
        resolved: ResolvedProfile,
        *,
        force: bool = False,
        allow_incomplete: bool = False,
    ) -> Path:
        if resolved.missing_required and not allow_incomplete:
            missing = ", ".join(resolved.missing_required)
            raise RuntimeError(f"required profile resources did not resolve: {missing}")

        source_archives = self._attest_source_archives(resolved)
        self._verify_required_tools(resolved)

        extracted = self.extract_sources(resolved, force=force)
        pack_root = self.packs_root / resolved.profile.pack_id
        staging = self.packs_root / (resolved.profile.pack_id + ".building")
        if staging.exists():
            shutil.rmtree(staging)
        staging.mkdir(parents=True)
        provenance_entries: list[dict[str, Any]] = []
        incomplete: list[dict[str, str]] = []
        for resource in resolved.resources:
            reasons: list[str] = []
            if resource.missing_patterns:
                reasons.append("missing patterns: " + ", ".join(resource.missing_patterns))
            if resource.count_error:
                reasons.append(resource.count_error)
            if resource.rule.required and not resource.entries and not reasons:
                reasons.append("no entries resolved")
            if resource.rule.required and reasons:
                incomplete.append(
                    {"resource": resource.rule.id, "reason": "; ".join(reasons)}
                )

        for resource in resolved.resources:
            bundle_outputs: list[Path] | None = None
            bundle_error: str | None = None
            if resource.rule.converter in {
                "w3d-bundle",
                "w3d-hierarchical",
                "w3d-static",
            } and resource.entries:
                staging_sources = _w3d_staging_sources(
                    resource, resolved.resources, extracted
                )
                try:
                    bundle_outputs = self._convert_w3d_bundle(
                        staging_sources,
                        resource.rule.output,
                        resource.rule.options,
                        staging,
                        resolved.profile.id,
                        resource.rule.id,
                        {
                            "w3d-bundle": "animated",
                            "w3d-hierarchical": "hierarchical",
                            "w3d-static": "static",
                        }[resource.rule.converter],
                    )
                except (FileNotFoundError, RuntimeError, ValueError) as exc:
                    bundle_error = str(exc)
                    incomplete.append({"resource": resource.rule.id, "reason": bundle_error})
                    if resource.rule.required and not allow_incomplete:
                        raise
                    bundle_outputs = []
            elif resource.rule.converter == "sage-terrain-materials" and resource.entries:
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
                    incomplete.append({"resource": resource.rule.id, "reason": bundle_error})
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
                    cached = extracted.get((entry.archive.casefold(), entry.name.casefold()))
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
                    incomplete.append({"resource": resource.rule.id, "reason": bundle_error})
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
                else:
                    try:
                        output_paths = self._convert_resource(
                            source_path,
                            resource.rule.converter,
                            resource.rule.output,
                            resource.rule.options,
                            staging,
                            index=index,
                        )
                    except (FileNotFoundError, RuntimeError, ValueError) as exc:
                        incomplete.append({"resource": resource.rule.id, "reason": str(exc)})
                        if resource.rule.required and not allow_incomplete:
                            raise
                        output_paths = []
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
                        "outputs": [
                            {
                                "path": path.relative_to(staging).as_posix(),
                                "size": path.stat().st_size,
                                "sha256": sha256_file(path),
                            }
                            for path in output_paths
                        ],
                    }
                )

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
            "source_game": "bfme2-retail-user-owned",
            "source_archives": source_archives,
            "redistributable": False,
            "tools": tools,
            "incomplete": incomplete,
            "entries": provenance_entries,
        }
        provenance["bundle_files"] = _canonical_pack_inventory(staging)
        write_json_atomic(staging / "provenance" / "manifest.json", provenance)
        audit = audit_pack(staging)
        write_json_atomic(staging / "provenance" / "audit.json", audit)
        if not audit["valid"]:
            raise RuntimeError("built pack failed its internal hash audit")

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
        return pack_root

    def _attest_source_archives(self, resolved: ResolvedProfile) -> list[dict[str, Any]]:
        selected = sorted({entry.archive for entry in resolved.selected_entries}, key=str.casefold)
        reports: list[dict[str, Any]] = []
        for relative in selected:
            archive_path = self.catalog.install_root / Path(relative)
            actual = sha256_file(archive_path)
            expected = KNOWN_SLICE_ARCHIVE_SHA256.get(relative.casefold())
            if resolved.profile.id == "men-fords-v0":
                if expected is None:
                    raise RuntimeError(f"retail slice archive has no trusted fingerprint: {relative}")
                if actual.casefold() != expected.casefold():
                    raise RuntimeError(
                        f"retail slice archive differs from the attested BFME II 1.06 input: {relative}"
                    )
            report: dict[str, Any] = {
                "relative_path": relative,
                "size": archive_path.stat().st_size,
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
        if converters & {
            "w3d-model",
            "w3d-animation",
            "w3d-bundle",
            "w3d-hierarchical",
            "w3d-static",
        }:
            required_checks.update({"blender", "blender_tree", "opensage_w3d_plugin"})
        if required_checks == {"python", "python_runtime"} and resolved.profile.id != "men-fords-v0":
            return
        from .bootstrap import tool_status

        status = tool_status(self.state_root)
        missing = sorted(name for name in required_checks if not status.get("checks", {}).get(name, False))
        if missing:
            raise RuntimeError("required pinned conversion tools are not ready: " + ", ".join(missing))
        self._blender_tree_verified = bool(status.get("checks", {}).get("blender_tree", False))
        self._python_runtime_report = dict(status.get("python_runtime", {}))

    def publish_to_godot(self, pack_root: Path | str, content_root: Path | str) -> dict[str, str]:
        source = Path(pack_root).expanduser().resolve()
        pack_data = read_json(source / "pack.json")
        pack_id = str(pack_data.get("id", ""))
        if not pack_id or any(character not in "abcdefghijklmnopqrstuvwxyz0123456789._-" for character in pack_id):
            raise ValueError(f"built pack has an unsafe id: {pack_id!r}")
        if not bool(pack_data.get("profile_build_complete", False)):
            raise RuntimeError("incomplete retail packs cannot be published or selected")
        source_audit = audit_pack(source)
        if not source_audit["valid"]:
            raise RuntimeError("source pack failed canonical audit before publication")
        root = ensure_external_to_repo(Path(content_root), repo_root_from_module())
        digest = bundle_digest(source)
        relative = Path(pack_id) / digest
        destination = (root / relative).resolve()
        try:
            destination.relative_to(root.resolve())
        except ValueError as exc:
            raise ValueError("published pack escaped the Godot content root") from exc
        if destination.exists() and not destination.is_dir():
            raise RuntimeError(f"published bundle path is not a directory: {destination}")
        if destination.is_dir():
            if bundle_digest(destination) != digest or not audit_pack(destination)["valid"]:
                raise RuntimeError(
                    f"pre-existing published bundle is corrupt or tampered: {destination}"
                )
        else:
            staging = destination.with_name(destination.name + ".building")
            if staging.exists():
                shutil.rmtree(staging)
            staging.parent.mkdir(parents=True, exist_ok=True)
            shutil.copytree(source, staging)
            if bundle_digest(staging) != digest:
                shutil.rmtree(staging)
                raise RuntimeError("published staging copy failed its bundle hash check")
            if not audit_pack(staging)["valid"]:
                shutil.rmtree(staging)
                raise RuntimeError("published staging copy failed its canonical audit")
            os.replace(staging, destination)
        selection = {
            "schema": "openbfme.pack-selection",
            "schemaVersion": 0,
            "activePack": relative.as_posix(),
        }
        write_json_atomic(root / "selection.json", selection)
        return {
            "bundle_sha256": digest,
            "published_pack": str(destination),
            "selection": str(root / "selection.json"),
            "active_pack": relative.as_posix(),
        }

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
        blender = Path(
            os.environ.get(
                "OPENBFME_BLENDER",
                str(self.state_root / "tools" / "blender-4.2.0-windows-x64" / "blender.exe"),
            )
        ).expanduser().resolve()
        if blender.is_file():
            from .bootstrap import BLENDER_TREE_SHA256

            report["blender"] = {
                "version": "4.2.0",
                "sha256": sha256_file(blender),
                "tree_sha256": BLENDER_TREE_SHA256,
            }
        plugin = Path(
            os.environ.get(
                "OPENBFME_W3D_PLUGIN",
                str(self.state_root / "tools" / "OpenSAGE.BlenderPlugin"),
            )
        ).expanduser().resolve()
        value = git_revision(plugin)
        submodule_value = git_revision(plugin, "io_mesh_w3d/blender_addon_updater")
        if value:
            from .bootstrap import _reject_python_bytecode

            _reject_python_bytecode(plugin, "OpenSAGE W3D plugin")
            report["opensage_w3d_plugin"] = {
                "commit": value,
                "submodule_commit": submodule_value,
                "worktree_clean": git_worktree_clean(plugin),
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
        if converter == "hash-only":
            return []
        if converter == "sage-map":
            return self._convert_sage_map(source, target, options)
        if converter in {"copy", "text", "map"}:
            shutil.copyfile(source, target)
            return [target]
        if converter in {"texture", "texture-crop"}:
            try:
                from PIL import Image
                import PIL
            except ImportError as exc:
                raise FileNotFoundError(
                    "Pillow is required for deterministic DDS/TGA conversion"
                ) from exc
            if PIL.__version__ != "12.2.0":
                raise RuntimeError(
                    f"Pillow 12.2.0 is required for deterministic texture output; found {PIL.__version__}"
                )
            if target.suffix.casefold() != ".png":
                raise ValueError("deterministic texture conversion currently emits PNG only")
            with Image.open(source) as opened:
                converted = opened.convert("RGBA")
                if converter == "texture-crop":
                    crop = options.get("crop", [])
                    if not (
                        isinstance(crop, list)
                        and len(crop) == 4
                        and all(isinstance(value, int) and value >= 0 for value in crop)
                        and crop[2] > 0
                        and crop[3] > 0
                    ):
                        raise ValueError("texture-crop requires options.crop=[x,y,width,height]")
                    converted = converted.crop(
                        (crop[0], crop[1], crop[0] + crop[2], crop[1] + crop[3])
                    )
                converted.save(target, format="PNG", compress_level=9, optimize=False)
            return [target]
        if converter == "audio":
            ffmpeg = discover_executable("ffmpeg", "OPENBFME_FFMPEG")
            if not ffmpeg:
                raise FileNotFoundError(
                    "ffmpeg is required; set OPENBFME_FFMPEG to its executable"
                )
            from .bootstrap import FFMPEG_EXE_SHA256

            if sha256_file(ffmpeg).casefold() != FFMPEG_EXE_SHA256:
                raise RuntimeError("FFmpeg executable does not match the pinned 8.1.1 hash")
            command = [str(ffmpeg), "-nostdin", "-hide_banner", "-loglevel", "error", "-y", "-i", str(source)]
            if source.suffix.casefold() == target.suffix.casefold() and not bool(options.get("force_pcm", False)):
                shutil.copyfile(source, target)
                return [target]
            if target.suffix.casefold() != ".wav":
                raise ValueError(
                    "deterministic audio conversion only supports exact copies or PCM WAV output"
                )
            command.extend(
                [
                    "-fflags",
                    "+bitexact",
                    "-flags:a",
                    "+bitexact",
                    "-map_metadata",
                    "-1",
                    "-vn",
                    "-c:a",
                    "pcm_s16le",
                ]
            )
            command.append(str(target))
            run_checked(command)
            return [target]
        if converter in {"w3d-model", "w3d-animation"}:
            executable = os.environ.get("OPENBFME_W3D_CONVERTER", "").strip()
            if not executable or not Path(executable).is_file():
                raise FileNotFoundError(
                    "W3D converter unavailable; set OPENBFME_W3D_CONVERTER"
                )
            mode = "model" if converter == "w3d-model" else "animation"
            run_checked([executable, "convert", "--mode", mode, "--input", str(source), "--output", str(target)])
            if not target.is_file():
                raise RuntimeError(f"W3D converter did not create {target}")
            return [target]
        raise ValueError(f"unsupported converter: {converter}")

    def _convert_sage_map(
        self,
        source: Path,
        output_directory: Path,
        options: dict[str, Any],
    ) -> list[Path]:
        if output_directory.suffix:
            raise ValueError("sage-map output must be a pack-relative directory")
        from .sage_map import convert_sage_map

        unsupported = sorted(set(options) - {"metadata", "expected", "objectBindings"})
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
        return convert_sage_map(
            source,
            output_directory,
            metadata,
            expected,
            object_bindings,
        )

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
            raise ValueError("sage-terrain-materials output cannot contain format tokens")
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
                converted.crop(
                    (x, y, x + crop_width, y + crop_height)
                ).save(target, format="PNG", compress_level=9, optimize=False)
                outputs.append(target)
        return outputs

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
        if asset_kind != "animated" and required_equipment:
            raise ValueError(
                f"w3d-{asset_kind} does not accept options.required_equipment"
            )

        blender = Path(
            os.environ.get(
                "OPENBFME_BLENDER",
                str(self.state_root / "tools" / "blender-4.2.0-windows-x64" / "blender.exe"),
            )
        ).expanduser().resolve()
        plugin = Path(
            os.environ.get(
                "OPENBFME_W3D_PLUGIN",
                str(self.state_root / "tools" / "OpenSAGE.BlenderPlugin"),
            )
        ).expanduser().resolve()
        if not blender.is_file():
            raise FileNotFoundError(
                f"pinned Blender 4.2.0 not found: {blender}; run importer bootstrap-tools"
            )
        if not (plugin / "io_mesh_w3d" / "__init__.py").is_file():
            raise FileNotFoundError(
                f"pinned OpenSAGE W3D plugin not found: {plugin}; run importer bootstrap-tools"
            )
        from .bootstrap import (
            BLENDER_EXE_SHA256,
            BLENDER_TREE_SHA256,
            PLUGIN_COMMIT,
            PLUGIN_SUBMODULE_COMMIT,
            _reject_python_bytecode,
        )

        if sha256_file(blender).casefold() != BLENDER_EXE_SHA256:
            raise RuntimeError("Blender executable does not match the pinned 4.2.0 hash")
        if not self._blender_tree_verified:
            if directory_tree_sha256(blender.parent) != BLENDER_TREE_SHA256:
                raise RuntimeError("Blender portable tree does not match the pinned 4.2.0 distribution")
            self._blender_tree_verified = True
        plugin_commit = git_revision(plugin)
        submodule_commit = git_revision(plugin, "io_mesh_w3d/blender_addon_updater")
        if plugin_commit != PLUGIN_COMMIT or submodule_commit != PLUGIN_SUBMODULE_COMMIT:
            raise RuntimeError(
                "OpenSAGE W3D plugin or required updater submodule does not match the pinned commit"
            )
        if not git_worktree_clean(plugin):
            raise RuntimeError("OpenSAGE W3D plugin worktree is dirty; conversion is not reproducible")
        _reject_python_bytecode(plugin, "OpenSAGE W3D plugin")

        job_root = self.jobs_root / profile_id / "w3d" / asset_id
        if job_root.exists():
            shutil.rmtree(job_root)
        input_root = job_root / "input"
        copied = _stage_w3d_sources(staging_sources, input_root)

        model = copied.get(model_name)
        if not model:
            raise FileNotFoundError(f"W3D model was not selected by the profile: {model_name}")
        animations: list[Path] = []
        for name in animation_names:
            animation = copied.get(name)
            if not animation:
                raise FileNotFoundError(f"W3D animation was not selected by the profile: {name}")
            animations.append(animation)

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
            "--output",
            str(target),
            "--animations",
            *[str(path) for path in animations],
            "--required-equipment",
            *required_equipment,
            "--excluded-optional-meshes",
            *excluded_optional_meshes,
        ]
        isolated_environment = _isolated_blender_environment(os.environ, job_root)
        result = run_checked(command, env=isolated_environment)
        _reject_python_bytecode(blender.parent, "Blender portable tree")
        _reject_python_bytecode(plugin, "OpenSAGE W3D plugin")
        if directory_tree_sha256(blender.parent) != BLENDER_TREE_SHA256:
            raise RuntimeError("Blender portable tree changed during W3D conversion")
        if not git_worktree_clean(plugin):
            raise RuntimeError("OpenSAGE W3D plugin changed during W3D conversion")
        combined_log = result.stdout + "\n" + result.stderr
        unsupported = [line for line in combined_log.splitlines() if "not supported" in line.casefold()]
        if unsupported:
            raise RuntimeError(
                f"W3D conversion emitted {len(unsupported)} unsupported-feature warning(s)"
            )
        missing_textures = [line for line in combined_log.splitlines() if "texture not found" in line.casefold()]
        if missing_textures:
            raise RuntimeError(
                f"W3D conversion reported {len(missing_textures)} missing texture(s)"
            )
        if "OPENBFME_W3D_OK" not in combined_log:
            raise RuntimeError("W3D adapter did not emit its success marker")
        if not target.is_file() or target.stat().st_size < 1024:
            raise RuntimeError(f"W3D adapter did not create a substantial GLB: {target}")
        marker_lines = [line for line in combined_log.splitlines() if line.startswith("OPENBFME_W3D_OK ")]
        if len(marker_lines) != 1:
            raise RuntimeError("W3D adapter emitted an ambiguous success report")
        report = json.loads(marker_lines[0].split(" ", 1)[1])
        metrics = _validated_w3d_metadata(
            report,
            required_equipment,
            expected_animation_count=len(animation_names),
            asset_kind=asset_kind,
            expected_excluded_optional_meshes=excluded_optional_meshes,
        )
        metrics_path = _safe_output(pack_root, report_relative_path)
        write_json_atomic(metrics_path, metrics)
        return [target, metrics_path]

    def _write_runtime_data(self, pack_root: Path, runtime_data: dict[str, Any]) -> None:
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
        errors.append("retail provenance is missing tool attestations: " + ", ".join(missing))
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
        errors.append("retail provenance OpenSAGE plugin attestation does not match the pin")
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
        errors.append("retail provenance Python runtime attestation does not match the pin")
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
    if not isinstance(manifest.get("importer_version"), str) or not manifest.get("importer_version"):
        errors.append("retail provenance importer version is missing")
    if manifest.get("source_game") != "bfme2-retail-user-owned":
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
                errors.append(f"retail source archive does not match the Fords pin: {relative}")
    summary["source_archive_count"] = len(archive_names)
    if profile == "men-fords-v0" and archive_names != set(KNOWN_SLICE_ARCHIVE_SHA256):
        errors.append("Fords provenance source archive closure is incomplete")

    incomplete = manifest.get("incomplete")
    if not isinstance(incomplete, list):
        errors.append("retail provenance incomplete field is not an array")
    elif bool(pack_data.get("profile_build_complete", False)) == bool(incomplete):
        errors.append("pack completion flag disagrees with provenance incomplete reasons")

    entries = manifest.get("entries")
    if not isinstance(entries, list):
        errors.append("retail provenance entries is not an array")
        entries = []
    source_keys: set[tuple[str, str]] = set()
    for entry in entries:
        source = entry.get("source") if isinstance(entry, dict) else None
        if (
            not isinstance(entry, dict)
            or not isinstance(entry.get("resource_id"), str)
            or not isinstance(entry.get("kind"), str)
            or not isinstance(entry.get("converter"), str)
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
        key = (source["archive"].casefold(), source["virtual_path"].casefold())
        if key in source_keys:
            errors.append(
                "duplicate retail provenance source entry: "
                f"{source['archive']}:{source['virtual_path']}"
            )
        source_keys.add(key)
        if source["archive"].casefold() not in archive_names:
            errors.append(f"retail provenance source uses an unattested archive: {source['archive']}")
    summary["provenance_entry_count"] = len(entries)
    if profile == "men-fords-v0" and len(entries) != MEN_FORDS_SOURCE_ENTRY_COUNT:
        errors.append(
            "Fords provenance must contain exactly "
            f"{MEN_FORDS_SOURCE_ENTRY_COUNT} sources, found {len(entries)}"
        )

    summary["semantic_provenance"] = not errors
    return summary


def audit_pack(pack_root: Path | str) -> dict[str, Any]:
    root = Path(pack_root).expanduser().resolve()
    manifest_path = root / "provenance" / "manifest.json"
    errors: list[str] = []
    checked = 0
    if not (root / "pack.json").is_file():
        errors.append("missing pack.json")
    if not manifest_path.is_file():
        errors.append("missing provenance/manifest.json")
        return {"valid": False, "checked_files": 0, "checked_outputs": 0, "errors": errors}
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
            target = _safe_output(root, relative)
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
        if sha256_file(target) != item.get("sha256"):
            errors.append(f"hash mismatch: {relative}")

    excluded = {"provenance/manifest.json", "provenance/audit.json"}
    actual: set[str] = set()
    for path in root.rglob("*"):
        if _is_link_like(path):
            errors.append(f"symbolic link or junction in pack: {path.relative_to(root).as_posix()}")
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
        if not isinstance(entry, dict) or not isinstance(entry.get("outputs", []), list):
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
                _safe_output(root, relative)
            except (KeyError, TypeError, ValueError) as exc:
                errors.append(str(exc))
                continue
            declared_outputs.add(relative)
            if relative not in expected:
                errors.append(f"converted output absent from bundle inventory: {relative}")
                continue
            inventory_item = expected[relative]
            if output["size"] != inventory_item.get("size"):
                errors.append(f"converted output size disagrees with bundle inventory: {relative}")
            if output["sha256"] != inventory_item.get("sha256"):
                errors.append(f"converted output hash disagrees with bundle inventory: {relative}")
    unique_errors = sorted(set(errors))
    return {
        "valid": not unique_errors,
        "checked_files": checked,
        "checked_outputs": len(declared_outputs),
        "errors": unique_errors,
        **provenance_summary,
    }
