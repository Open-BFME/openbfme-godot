"""Plan exact zero-clip hierarchical prop conversions from retail evidence.

The static-prop planner intentionally rejects every W3D that contains a
hierarchy.  This module consumes that planner's sealed output and selects the
strictly smaller follow-on class that the ``w3d-hierarchical`` converter can
prove safe: one exact model W3D, at least one hierarchy header, no animation
headers or authored animation/effect dependencies, no scanner warnings, and a
complete exact texture closure.

The result is a payload-free profile fragment and exact map binding plan.  It
does not convert, publish, or modify a content pack.
"""

from __future__ import annotations

from collections import Counter
from copy import deepcopy
import json
from pathlib import Path, PurePosixPath
import tempfile
from typing import Any, Mapping

from .profile import ImportProfile
from .retail_visual_profile import (
    STATIC_PROP_PLAN_SCHEMA,
    STATIC_PROP_PLAN_SCHEMA_VERSION,
    _canonical_sha256,
    _case_unique,
    _is_int,
    _mapping,
    _reason,
    _safe_object_id,
    _source_record,
    _stable_slug,
    _text,
    _unique_reasons,
    _validate_declared_digest,
    _validate_effective_manifest,
    _validate_visual_closure,
    build_retail_static_prop_plan,
)
from .util import write_json_atomic


HIERARCHICAL_PROP_PLAN_SCHEMA = "openbfme.retail-hierarchical-prop-plan"
HIERARCHICAL_PROP_PLAN_SCHEMA_VERSION = 0
MAP_OBJECTS_SCHEMA = "openbfme.sage-map-objects"
MAP_OBJECTS_SCHEMA_VERSION = 0

_FORBIDDEN_TARGET_LEAF_KINDS = frozenset(
    {"animation", "particle", "attached-model"}
)
_TEXTURE_LEAF_KINDS = frozenset({"texture", "shadow"})
_TEXTURE_SUFFIXES = frozenset({".dds", ".tga", ".jpg", ".png"})
_SUPPORTED_W3D_MODEL_REFERENCE_ROLES = frozenset({"lod"})
_MAX_PROFILE_RESOURCES = 256


def _validate_static_plan(
    raw: Mapping[str, Any],
    visual_closure_report: Mapping[str, Any],
    effective_assets_manifest: Mapping[str, Any],
) -> tuple[str, set[str], dict[str, list[Mapping[str, Any]]]]:
    """Require the input plan to be the exact plan implied by the other inputs."""

    plan = _mapping(raw, "retail static-prop plan")
    if plan.get("schema") != STATIC_PROP_PLAN_SCHEMA:
        raise ValueError("unsupported retail static-prop plan schema")
    if plan.get("schemaVersion") != STATIC_PROP_PLAN_SCHEMA_VERSION:
        raise ValueError("unsupported retail static-prop plan schema version")
    digest = _validate_declared_digest(
        plan, "aggregateSha256", "retail static-prop plan"
    )

    expected = build_retail_static_prop_plan(
        visual_closure_report, effective_assets_manifest
    )
    if dict(plan) != expected:
        raise ValueError(
            "retail static-prop plan does not exactly match the validated visual "
            "closure and effective-assets manifest"
        )

    eligible_rows = plan.get("eligibleTargets")
    ineligible_rows = plan.get("ineligibleTargets")
    if not isinstance(eligible_rows, list) or not isinstance(ineligible_rows, list):
        raise ValueError("retail static-prop plan target partitions are invalid")
    eligible: set[str] = set()
    ineligible: dict[str, list[Mapping[str, Any]]] = {}
    for position, value in enumerate(eligible_rows):
        item = _mapping(value, f"static eligible target {position}")
        eligible.add(_safe_object_id(item.get("targetObject"), "static target id"))
    for position, value in enumerate(ineligible_rows):
        item = _mapping(value, f"static ineligible target {position}")
        target = _safe_object_id(item.get("targetObject"), "static target id")
        reasons = item.get("reasons")
        if not isinstance(reasons, list) or not reasons:
            raise ValueError(f"static target {target!r} has no exclusion reasons")
        ineligible[target] = [
            _mapping(reason, f"static target {target!r} reason")
            for reason in reasons
        ]
    _case_unique(list(eligible) + list(ineligible), "static target id")
    return digest, eligible, ineligible


def _validate_placement_document(
    raw: Mapping[str, Any], targets: list[str]
) -> tuple[dict[str, int], dict[str, Any]]:
    """Validate cooked map object rows and count exact non-road target matches."""

    document = _mapping(raw, "SAGE map objects document")
    if document.get("schema") != MAP_OBJECTS_SCHEMA:
        raise ValueError("unsupported SAGE map objects schema")
    if document.get("schemaVersion") != MAP_OBJECTS_SCHEMA_VERSION:
        raise ValueError("unsupported SAGE map objects schema version")
    rows = document.get("objects")
    if not isinstance(rows, list):
        raise ValueError("SAGE map objects must be an array")
    count = document.get("count")
    if not _is_int(count) or int(count) != len(rows):
        raise ValueError("SAGE map objects count does not match its rows")

    target_cases = _case_unique(targets, "visual-closure target id")
    placement_counts = {target: 0 for target in targets}
    seen_indices: set[int] = set()
    non_road_count = 0
    road_count = 0
    for position, value in enumerate(rows):
        item = _mapping(value, f"SAGE map object {position}")
        index = item.get("index")
        if not _is_int(index) or int(index) < 0:
            raise ValueError(f"SAGE map object {position} has an invalid index")
        index = int(index)
        if index in seen_indices:
            raise ValueError(f"duplicate SAGE map object index: {index}")
        seen_indices.add(index)
        if index != position:
            raise ValueError("SAGE map object rows are not in exact index order")

        type_name = _text(item.get("typeName"), f"SAGE map object {position} typeName")
        if len(type_name) > 512:
            raise ValueError(f"SAGE map object {position} typeName is too long")
        road_type = item.get("roadType")
        if not _is_int(road_type) or int(road_type) < 0:
            raise ValueError(f"SAGE map object {position} has an invalid roadType")
        if int(road_type) != 0:
            road_count += 1
            continue
        non_road_count += 1
        exact_target = target_cases.get(type_name.casefold())
        if exact_target is not None:
            if exact_target != type_name:
                raise ValueError(
                    "SAGE map object target case does not match visual closure: "
                    f"{type_name!r} != {exact_target!r}"
                )
            placement_counts[exact_target] += 1

    evidence = {
        "schema": MAP_OBJECTS_SCHEMA,
        "schemaVersion": MAP_OBJECTS_SCHEMA_VERSION,
        "documentAggregateSha256": _canonical_sha256(document),
        "objectRecordCount": len(rows),
        "nonRoadObjectRecordCount": non_road_count,
        "roadControlPointRecordCount": road_count,
    }
    return placement_counts, evidence


def _validate_generated_profile_fragment(resources: list[dict[str, Any]]) -> bool:
    """Run the actual ImportProfile parser over a non-empty generated fragment."""

    if not resources:
        return False
    payload = {
        "format": 1,
        "id": "hierarchical-prop-fragment-validation",
        "pack": {"id": "hierarchical-prop-fragment-validation-pack"},
        "resources": resources,
    }
    with tempfile.TemporaryDirectory(prefix="openbfme-hier-profile-") as raw:
        path = Path(raw) / "profile.json"
        path.write_text(
            json.dumps(
                payload,
                sort_keys=True,
                ensure_ascii=False,
                separators=(",", ":"),
                allow_nan=False,
            ),
            encoding="utf-8",
        )
        parsed = ImportProfile.load(path)
    if len(parsed.resources) != len(resources):
        raise ValueError("generated ImportProfile fragment changed resource count")
    return True


def build_retail_hierarchical_prop_plan(
    visual_closure_report: Mapping[str, Any],
    static_prop_plan: Mapping[str, Any],
    effective_assets_manifest: Mapping[str, Any],
    map_objects_document: Mapping[str, Any],
) -> dict[str, Any]:
    """Return a deterministic conversion plan for exact zero-clip hierarchies."""

    report = _mapping(visual_closure_report, "retail visual closure")
    manifest = _mapping(effective_assets_manifest, "effective-assets manifest")
    sources, manifest_evidence = _validate_effective_manifest(manifest)
    closure = _validate_visual_closure(report, sources)
    static_digest, static_eligible, static_ineligible = _validate_static_plan(
        _mapping(static_prop_plan, "retail static-prop plan"), report, manifest
    )
    targets = sorted(
        closure["targets"], key=lambda value: (value.casefold(), value)
    )
    if static_eligible | set(static_ineligible) != set(targets):
        raise ValueError("static-prop target partition does not match visual closure")
    if static_eligible & set(static_ineligible):
        raise ValueError("static-prop target partitions overlap")
    placement_counts, placement_source = _validate_placement_document(
        _mapping(map_objects_document, "SAGE map objects document"), targets
    )

    eligible: list[dict[str, Any]] = []
    rejected: list[dict[str, Any]] = []
    per_target_textures: dict[str, tuple[str, ...]] = {}
    per_target_root_rigid_bake: dict[str, bool] = {}

    for target in targets:
        if target in static_eligible:
            rejected.append(
                {
                    "targetObject": target,
                    "placementCount": placement_counts[target],
                    "reasons": [_reason("already-covered-by-static-prop-plan")],
                }
            )
            continue

        reasons: list[dict[str, Any]] = []
        target_record = closure["targetRecords"][target]
        if target_record.get("status") != "resolved":
            reasons.append(_reason("missing-object-definition"))
        object_summary = closure["objects"].get(target)
        if object_summary is None:
            reasons.append(_reason("missing-object-summary"))
        elif not object_summary.get("inheritanceComplete"):
            reasons.append(_reason("incomplete-object-inheritance"))
        for diagnostic in closure["diagnosticsByTarget"][target]:
            reasons.append(
                _reason(
                    "object-graph-diagnostic",
                    diagnosticCode=str(diagnostic.get("code", "unknown")),
                )
            )

        leaves = closure["leavesByTarget"][target]
        for kind in sorted(_FORBIDDEN_TARGET_LEAF_KINDS):
            matching = [leaf for leaf in leaves if leaf.get("kind") == kind]
            if matching:
                reasons.append(
                    _reason(
                        f"requires-{kind}",
                        referenceCount=len(matching),
                        statuses=sorted(
                            {str(item.get("status")) for item in matching}
                        ),
                    )
                )
        unresolved = [
            leaf
            for leaf in leaves
            if leaf.get("status") in {"missing", "ambiguous", "invalid"}
        ]
        if unresolved:
            reasons.append(
                _reason(
                    "unresolved-visual-reference",
                    referenceCount=len(unresolved),
                    statuses=sorted({str(item.get("status")) for item in unresolved}),
                )
            )

        model_paths: set[str] = set()
        hierarchy_paths: set[str] = set()
        texture_paths: set[str] = set()
        for leaf in leaves:
            kind = str(leaf.get("kind"))
            status = leaf.get("status")
            if kind == "model" and status == "resolved":
                paths = list(leaf.get("physicalVirtualPaths", []))
                w3d_paths = [
                    path
                    for path in paths
                    if PurePosixPath(path).suffix.casefold() == ".w3d"
                ]
                if len(paths) != 1 or len(w3d_paths) != 1:
                    reasons.append(
                        _reason(
                            "model-reference-not-one-exact-w3d",
                            identifier=str(leaf.get("identifier")),
                            physicalPathCount=len(paths),
                        )
                    )
                else:
                    model_paths.add(w3d_paths[0])
            elif kind == "hierarchy" and status == "resolved":
                paths = list(leaf.get("physicalVirtualPaths", []))
                if (
                    len(paths) != 1
                    or PurePosixPath(paths[0]).suffix.casefold() != ".w3d"
                ):
                    reasons.append(
                        _reason(
                            "hierarchy-reference-not-one-exact-w3d",
                            identifier=str(leaf.get("identifier")),
                            physicalPathCount=len(paths),
                        )
                    )
                else:
                    hierarchy_paths.add(paths[0])
            elif kind in _TEXTURE_LEAF_KINDS and status == "resolved":
                paths = list(leaf.get("physicalVirtualPaths", []))
                if (
                    len(paths) != 1
                    or PurePosixPath(paths[0]).suffix.casefold()
                    not in _TEXTURE_SUFFIXES
                ):
                    reasons.append(
                        _reason(
                            "texture-reference-not-one-exact-image",
                            identifier=str(leaf.get("identifier")),
                            physicalPathCount=len(paths),
                        )
                    )
                else:
                    texture_paths.add(paths[0])
            elif status == "semantic":
                if not (
                    kind == "model" and str(leaf.get("identifier")).casefold() == "none"
                ):
                    reasons.append(
                        _reason(
                            "unsupported-semantic-visual-reference",
                            kind=kind,
                            identifier=str(leaf.get("identifier")),
                        )
                    )
            elif (
                status == "resolved"
                and kind not in _FORBIDDEN_TARGET_LEAF_KINDS
                and kind not in _TEXTURE_LEAF_KINDS
                and kind not in {"model", "hierarchy"}
            ):
                reasons.append(_reason("unsupported-visual-leaf-kind", kind=kind))

        if not model_paths:
            reasons.append(_reason("no-physical-model-w3d"))
        elif len(model_paths) > 1:
            reasons.append(
                _reason(
                    "multiple-physical-model-w3ds",
                    physicalVirtualPaths=sorted(
                        model_paths, key=lambda value: (value.casefold(), value)
                    ),
                )
            )
        model_path = next(iter(model_paths)) if len(model_paths) == 1 else None

        if model_path is not None and hierarchy_paths - {model_path}:
            reasons.append(
                _reason(
                    "external-hierarchy-w3d-not-supported",
                    physicalVirtualPaths=sorted(
                        hierarchy_paths - {model_path},
                        key=lambda value: (value.casefold(), value),
                    ),
                )
            )

        hierarchy_ids: list[str] = []
        model_reference_proof: list[dict[str, Any]] = []
        root_rigid_bake_proven = False
        if model_path is not None:
            scanned = closure["scanned"].get(model_path)
            if scanned is None:
                reasons.append(_reason("model-w3d-not-scanned"))
            else:
                warnings = list(scanned.get("warnings", []))
                if warnings:
                    reasons.append(
                        _reason(
                            "model-w3d-scanner-warnings", warningCount=len(warnings)
                        )
                    )
                header = scanned["headerIds"]
                if not header.get("modelIds"):
                    reasons.append(_reason("model-w3d-has-no-model-header"))
                hierarchy_ids = list(header.get("hierarchyIds", []))
                if not hierarchy_ids:
                    reasons.append(_reason("model-w3d-has-no-hierarchy-header"))
                animation_ids = list(header.get("animationIds", []))
                if animation_ids:
                    reasons.append(
                        _reason(
                            "model-w3d-contains-animation-headers",
                            headerCount=len(animation_ids),
                        )
                    )
                raw_model_references = scanned.get("modelReferences")
                malformed_reference_count = 0
                if not isinstance(raw_model_references, list):
                    raw_model_references = []
                    malformed_reference_count = 1
                for raw_reference in raw_model_references:
                    if not isinstance(raw_reference, Mapping):
                        malformed_reference_count += 1
                        continue
                    identifier = raw_reference.get("identifier")
                    bone_index = raw_reference.get("boneIndex")
                    role = raw_reference.get("role")
                    if (
                        not isinstance(identifier, str)
                        or not identifier
                        or not _is_int(bone_index)
                        or int(bone_index) < 0
                        or not isinstance(role, str)
                        or not role
                    ):
                        malformed_reference_count += 1
                        continue
                    model_reference_proof.append(
                        {
                            "identifier": identifier,
                            "boneIndex": int(bone_index),
                            "role": role,
                        }
                    )
                if malformed_reference_count:
                    reasons.append(
                        _reason(
                            "model-w3d-malformed-model-reference",
                            referenceCount=malformed_reference_count,
                        )
                    )
                unsupported_roles = sorted(
                    {
                        str(item["role"])
                        for item in model_reference_proof
                        if item["role"]
                        not in _SUPPORTED_W3D_MODEL_REFERENCE_ROLES
                    }
                )
                if unsupported_roles:
                    reasons.append(
                        _reason(
                            "model-w3d-unsupported-reference-role",
                            roles=unsupported_roles,
                        )
                    )
                model_ids = set(header.get("modelIds", []))
                supported_render_references = [
                    item
                    for item in model_reference_proof
                    if item["role"] in _SUPPORTED_W3D_MODEL_REFERENCE_ROLES
                    and item["identifier"] in model_ids
                ]
                if not supported_render_references:
                    reasons.append(
                        _reason("model-w3d-has-no-supported-render-subobject")
                    )
                missing_render_subobjects = sorted(
                    {
                        str(item["identifier"])
                        for item in model_reference_proof
                        if item["role"] in _SUPPORTED_W3D_MODEL_REFERENCE_ROLES
                        and item["identifier"] not in model_ids
                    },
                    key=lambda value: (value.casefold(), value),
                )
                if missing_render_subobjects:
                    reasons.append(
                        _reason(
                            "model-w3d-reference-has-no-render-subobject",
                            identifiers=missing_render_subobjects,
                        )
                    )
                root_rigid_bake_proven = bool(model_reference_proof) and all(
                    item["role"] in _SUPPORTED_W3D_MODEL_REFERENCE_ROLES
                    and item["boneIndex"] == 0
                    and item["identifier"] in model_ids
                    for item in model_reference_proof
                )

            for dependency in closure["embeddedByW3d"].get(model_path, []):
                if dependency.get("status") != "resolved":
                    reasons.append(
                        _reason(
                            "embedded-texture-unresolved",
                            identifier=str(dependency.get("identifier")),
                            status=str(dependency.get("status")),
                        )
                    )
                    continue
                paths = list(dependency.get("physicalVirtualPaths", []))
                if (
                    len(paths) != 1
                    or PurePosixPath(paths[0]).suffix.casefold()
                    not in _TEXTURE_SUFFIXES
                ):
                    reasons.append(
                        _reason(
                            "embedded-texture-not-one-exact-image",
                            identifier=str(dependency.get("identifier")),
                            physicalPathCount=len(paths),
                        )
                    )
                else:
                    texture_paths.add(paths[0])

        reasons = _unique_reasons(reasons)
        if reasons:
            rejected.append(
                {
                    "targetObject": target,
                    "placementCount": placement_counts[target],
                    "reasons": reasons,
                    "staticPlanReasons": deepcopy(static_ineligible[target]),
                }
            )
            continue
        assert model_path is not None
        textures = tuple(
            sorted(texture_paths, key=lambda value: (value.casefold(), value))
        )
        per_target_textures[target] = textures
        per_target_root_rigid_bake[target] = root_rigid_bake_proven
        eligible.append(
            {
                "targetObject": target,
                "modelVirtualPath": model_path,
                "hierarchyIds": sorted(
                    hierarchy_ids, key=lambda value: (value.casefold(), value)
                ),
                "textureVirtualPaths": list(textures),
                "placementCount": placement_counts[target],
                "provenRootRigidBake": root_rigid_bake_proven,
            }
        )

    grouped_targets: dict[str, list[str]] = {}
    for item in eligible:
        grouped_targets.setdefault(str(item["modelVirtualPath"]), []).append(
            str(item["targetObject"])
        )

    all_texture_paths = sorted(
        {
            path
            for target in per_target_textures
            for path in per_target_textures[target]
        },
        key=lambda value: (value.casefold(), value),
    )
    texture_resources: dict[str, dict[str, Any]] = {}
    texture_records: dict[str, dict[str, Any]] = {}
    for path in all_texture_paths:
        resource_id = _stable_slug("hier-prop-texture", path)
        output_stem = resource_id.removeprefix("hier-prop-texture-")
        texture_resources[path] = {
            "id": resource_id,
            "kind": "texture",
            "patterns": [path],
            "required": True,
            "converter": "texture",
            "output": f"assets/textures/props-hierarchical/{output_stem}.png",
            "limit": 1,
            "expected_count": 1,
        }
        texture_records[path] = _source_record(path, sources)

    conversion_groups: list[dict[str, Any]] = []
    model_resources: list[dict[str, Any]] = []
    binding_rows: list[dict[str, Any]] = []
    for model_path in sorted(
        grouped_targets, key=lambda value: (value.casefold(), value)
    ):
        group_targets = sorted(
            grouped_targets[model_path], key=lambda value: (value.casefold(), value)
        )
        group_texture_paths = sorted(
            {
                path
                for target in group_targets
                for path in per_target_textures[target]
            },
            key=lambda value: (value.casefold(), value),
        )
        resource_id = _stable_slug("hier-prop-model", model_path)
        output_stem = resource_id.removeprefix("hier-prop-model-")
        output = f"assets/models/props-hierarchical/{output_stem}.glb"
        input_ids = [texture_resources[path]["id"] for path in group_texture_paths]
        root_rigid_values = {
            per_target_root_rigid_bake[target] for target in group_targets
        }
        if len(root_rigid_values) != 1:
            raise ValueError(
                "one physical hierarchical W3D has inconsistent root-rigid proof"
            )
        root_rigid_bake = root_rigid_values.pop()
        model_resource = {
            "id": resource_id,
            "kind": "model",
            "patterns": [model_path],
            "required": True,
            "converter": "w3d-hierarchical",
            "output": output,
            "limit": 1,
            "expected_count": 1,
            "options": {
                "model": PurePosixPath(model_path).name,
                "animations": [],
                "required_equipment": [],
                "inputResourceIds": input_ids,
                "provenRootRigidBake": root_rigid_bake,
            },
        }
        model_resources.append(model_resource)
        scanned = closure["scanned"][model_path]
        group_placements = sum(placement_counts[target] for target in group_targets)
        conversion_groups.append(
            {
                "modelResourceId": resource_id,
                "targetObjects": group_targets,
                "placementCount": group_placements,
                "outputGlb": output,
                "modelSource": _source_record(
                    model_path,
                    sources,
                    expected_sha256=str(scanned["sha256"]),
                    expected_size=int(scanned["byteLength"]),
                ),
                "modelHeaderIds": list(scanned["headerIds"]["modelIds"]),
                "hierarchyHeaderIds": list(scanned["headerIds"]["hierarchyIds"]),
                "animationHeaderIds": [],
                "provenRootRigidBake": root_rigid_bake,
                "modelReferences": [
                    {
                        "identifier": str(item["identifier"]),
                        "boneIndex": int(item["boneIndex"]),
                        "role": str(item["role"]),
                    }
                    for item in scanned["modelReferences"]
                ],
                "textureSources": [
                    texture_records[path] for path in group_texture_paths
                ],
            }
        )
        for target in group_targets:
            binding_rows.append(
                {
                    "typeName": target,
                    "sourceVirtualModel": model_path,
                    "glb": output,
                    "matchMethod": "exact-type-name",
                }
            )

    resources = [
        *[texture_resources[path] for path in all_texture_paths],
        *model_resources,
    ]
    if len(resources) > _MAX_PROFILE_RESOURCES:
        raise ValueError(
            "hierarchical-prop profile fragment exceeds the 256-resource limit"
        )
    _case_unique(
        [str(item["id"]) for item in resources], "generated profile resource id"
    )
    _case_unique(
        [str(item["output"]) for item in resources],
        "generated profile output path",
    )
    profile_validated = _validate_generated_profile_fragment(resources)

    eligible_names = {str(item["targetObject"]) for item in eligible}
    static_placements = sum(placement_counts[target] for target in static_eligible)
    hierarchical_placements = sum(
        placement_counts[target] for target in eligible_names
    )
    target_placements = sum(placement_counts.values())
    cumulative_names = static_eligible | eligible_names
    cumulative_placements = sum(
        placement_counts[target] for target in cumulative_names
    )
    placement_by_target = [
        {
            "targetObject": target,
            "placementCount": placement_counts[target],
            "coverageStage": (
                "static"
                if target in static_eligible
                else "hierarchical"
                if target in eligible_names
                else "uncovered"
            ),
        }
        for target in targets
    ]
    reason_counts = Counter(
        str(reason["code"])
        for target in rejected
        for reason in target["reasons"]
    )

    plan: dict[str, Any] = {
        "schema": HIERARCHICAL_PROP_PLAN_SCHEMA,
        "schemaVersion": HIERARCHICAL_PROP_PLAN_SCHEMA_VERSION,
        "sourceEvidence": {
            "visualClosureAggregateSha256": closure["reportDigest"],
            "w3dDependencyAggregateSha256": closure["dependencyDigest"],
            "staticPropPlanAggregateSha256": static_digest,
            "effectiveAssets": manifest_evidence,
            "mapObjects": placement_source,
        },
        "policy": {
            "selection": "exact-zero-clip-hierarchical-target-types-only",
            "substitutesAllowed": False,
            "grouping": "one-conversion-per-exact-physical-w3d",
            "roadsCountedAsPlacements": False,
            "profileFragmentValidatedByImportProfile": profile_validated,
        },
        "eligibleTargets": eligible,
        "rejectedTargets": rejected,
        "conversionGroups": conversion_groups,
        "profileFragment": {
            "resources": resources,
            "objectBindings": {
                "models": sorted(
                    binding_rows,
                    key=lambda item: (
                        str(item["typeName"]).casefold(),
                        str(item["typeName"]),
                    ),
                )
            },
        },
        "placementCoverage": {
            "targetPlacementCount": target_placements,
            "staticPlanEligiblePlacementCount": static_placements,
            "hierarchicalBatchPlacementCount": hierarchical_placements,
            "cumulativePlannedPlacementCount": cumulative_placements,
            "uncoveredTargetPlacementCount": target_placements
            - cumulative_placements,
            "byTarget": placement_by_target,
        },
        "summary": {
            "targetTypeCount": len(targets),
            "eligibleTargetTypeCount": len(eligible),
            "rejectedTargetTypeCount": len(rejected),
            "conversionGroupCount": len(conversion_groups),
            "uniqueModelSourceCount": len(conversion_groups),
            "uniqueTextureSourceCount": len(all_texture_paths),
            "provenRootRigidConversionGroupCount": sum(
                1
                for group in conversion_groups
                if group["provenRootRigidBake"]
            ),
            "profileResourceCount": len(resources),
            "objectBindingModelRowCount": len(binding_rows),
            "hierarchicalBatchPlacementCount": hierarchical_placements,
            "cumulativePlannedTargetTypeCount": len(cumulative_names),
            "cumulativePlannedPlacementCount": cumulative_placements,
            "rejectionReasonCounts": [
                {"code": code, "targetCount": reason_counts[code]}
                for code in sorted(reason_counts)
            ],
        },
    }
    plan["aggregateSha256"] = _canonical_sha256(plan)
    return plan


def load_retail_hierarchical_prop_plan_inputs(
    visual_closure_path: Path | str,
    static_prop_plan_path: Path | str,
    effective_assets_manifest_path: Path | str,
    map_objects_path: Path | str,
) -> tuple[dict[str, Any], dict[str, Any], dict[str, Any], dict[str, Any]]:
    """Load bounded JSON inputs; semantic validation occurs in ``build``."""

    def load(path: Path | str, label: str, max_bytes: int) -> dict[str, Any]:
        source = Path(path).expanduser().resolve()
        if not source.is_file() or source.stat().st_size > max_bytes:
            raise ValueError(f"{label} is missing or exceeds {max_bytes} bytes")
        try:
            value = json.loads(source.read_text(encoding="utf-8"))
        except (OSError, UnicodeDecodeError, json.JSONDecodeError) as exc:
            raise ValueError(f"invalid {label}: {exc}") from exc
        if not isinstance(value, dict):
            raise ValueError(f"{label} root must be an object")
        return value

    return (
        load(visual_closure_path, "retail visual closure", 64 * 1024 * 1024),
        load(static_prop_plan_path, "retail static-prop plan", 64 * 1024 * 1024),
        load(
            effective_assets_manifest_path,
            "effective-assets manifest",
            64 * 1024 * 1024,
        ),
        load(map_objects_path, "SAGE map objects", 64 * 1024 * 1024),
    )


def write_retail_hierarchical_prop_plan(
    path: Path | str, plan: Mapping[str, Any]
) -> None:
    """Atomically write a previously built payload-free plan."""

    document = _mapping(plan, "retail hierarchical-prop plan")
    if document.get("schema") != HIERARCHICAL_PROP_PLAN_SCHEMA:
        raise ValueError("cannot write an unsupported hierarchical-prop plan schema")
    _validate_declared_digest(
        document, "aggregateSha256", "retail hierarchical-prop plan"
    )
    write_json_atomic(Path(path), deepcopy(dict(document)))


__all__ = [
    "HIERARCHICAL_PROP_PLAN_SCHEMA",
    "HIERARCHICAL_PROP_PLAN_SCHEMA_VERSION",
    "build_retail_hierarchical_prop_plan",
    "load_retail_hierarchical_prop_plan_inputs",
    "write_retail_hierarchical_prop_plan",
]
