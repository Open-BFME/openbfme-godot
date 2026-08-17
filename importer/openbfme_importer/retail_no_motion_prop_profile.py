"""Plan the exact Fords OrcMeatRack01 header-only W3D conversion.

This is intentionally not a general "ignore animation" escape hatch.  The
planner admits one retail object only after the sealed visual/static/
hierarchical/animated plans agree that it is still uncovered, the map and
unresolved census agree on its one placement, and fresh bytes from the private
effective-assets tree prove that its sole animation container has a header but
no motion-bearing child.  The generated ``w3d-hierarchical`` rule carries that
same exact proof contract into the conversion pipeline.

The returned plan is payload-free.  Retail bytes are read only below the
caller-provided ``workspace`` effective-assets root and are never written.
"""

from __future__ import annotations

from collections import Counter
from copy import deepcopy
import hashlib
import json
from pathlib import Path, PurePosixPath
import tempfile
from typing import Any, Mapping

from .paths import repo_root_from_module, safe_relative_parts
from .profile import ImportProfile
from .retail_animated_prop_profile import (
    ANIMATED_PROP_PLAN_SCHEMA,
    ANIMATED_PROP_PLAN_SCHEMA_VERSION,
)
from .retail_hierarchical_profile import (
    HIERARCHICAL_PROP_PLAN_SCHEMA,
    HIERARCHICAL_PROP_PLAN_SCHEMA_VERSION,
    _validate_placement_document,
)
from .retail_visual_profile import (
    STATIC_PROP_PLAN_SCHEMA,
    STATIC_PROP_PLAN_SCHEMA_VERSION,
    _canonical_sha256,
    _case_unique,
    _mapping,
    _source_record,
    _stable_slug,
    _text,
    _validate_declared_digest,
    _validate_effective_manifest,
    _validate_visual_closure,
)
from .util import write_json_atomic
from .w3d_metadata import W3DMetadata, scan_w3d_metadata
from .w3d_no_motion import (
    W3DNoMotionExpectation,
    strip_proven_header_only_animations,
)


NO_MOTION_PROP_PLAN_SCHEMA = "openbfme.retail-no-motion-prop-plan"
NO_MOTION_PROP_PLAN_SCHEMA_VERSION = 0
UNRESOLVED_CENSUS_SCHEMA = "openbfme.fords-unresolved-object-census"
UNRESOLVED_CENSUS_SCHEMA_VERSION = 1

TARGET_OBJECT = "OrcMeatRack01"
MODEL_VIRTUAL_PATH = "art/w3d/pm/pmmeatrack01.w3d"
TEXTURE_VIRTUAL_PATH = "art/compiledtextures/pm/pmmeatrack.dds"
OBJECT_DEFINITION_VIRTUAL_PATH = "data/ini/object/evilfaction/evilfactionprops.ini"
MODEL_IDENTIFIER = "PMMEATRACK01"
MESH_IDENTIFIER = "PMMEATRACK01.PMMEATRACK01"
TEXTURE_IDENTIFIER = "PMMeatrack.tga"
ANIMATION_FRAME_COUNT = 101
ANIMATION_FRAME_RATE = 30
EXPECTED_PLACEMENT_INDEX = 974
EXPECTED_PLACEMENT_UNIQUE_ID = "OrcMeatRack01 551"

_MAX_JSON_BYTES = 64 * 1024 * 1024
_MAX_W3D_BYTES = 512 * 1024 * 1024
_MAX_TEXTURE_BYTES = 256 * 1024 * 1024


def _list(value: object, label: str) -> list[Any]:
    if not isinstance(value, list):
        raise ValueError(f"{label} must be an array")
    return value


def _sha256_bytes(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def _document_digest(document: Mapping[str, Any], label: str) -> str:
    return _validate_declared_digest(document, "aggregateSha256", label)


def _exact_named_row(
    rows: object,
    *,
    field: str,
    name: str,
    label: str,
) -> Mapping[str, Any]:
    values = _list(rows, label)
    matches = [
        _mapping(value, f"{label} row {index}")
        for index, value in enumerate(values)
        if isinstance(value, Mapping) and value.get(field) == name
    ]
    if len(matches) != 1:
        raise ValueError(f"{label} must contain exactly one {name!r} row")
    return matches[0]


def _target_names(rows: object, field: str, label: str) -> set[str]:
    names = [
        _text(
            _mapping(value, f"{label} row {index}").get(field),
            f"{label} row {index} {field}",
        )
        for index, value in enumerate(_list(rows, label))
    ]
    _case_unique(names, label)
    return set(names)


def _require_source_evidence(
    raw: Mapping[str, Any],
    *,
    label: str,
    visual_digest: str,
    dependency_digest: str,
    manifest_evidence: Mapping[str, Any],
    map_evidence: Mapping[str, Any] | None,
    static_digest: str | None = None,
    hierarchical_digest: str | None = None,
) -> None:
    evidence = _mapping(raw.get("sourceEvidence"), f"{label} sourceEvidence")
    if evidence.get("visualClosureAggregateSha256") != visual_digest:
        raise ValueError(f"{label} visual-closure digest mismatch")
    if evidence.get("w3dDependencyAggregateSha256") != dependency_digest:
        raise ValueError(f"{label} W3D-dependency digest mismatch")
    if dict(_mapping(evidence.get("effectiveAssets"), "effectiveAssets")) != dict(
        manifest_evidence
    ):
        raise ValueError(f"{label} effective-assets evidence mismatch")
    if (
        static_digest is not None
        and evidence.get("staticPropPlanAggregateSha256") != static_digest
    ):
        raise ValueError(f"{label} static-plan digest mismatch")
    if (
        hierarchical_digest is not None
        and evidence.get("hierarchicalPropPlanAggregateSha256") != hierarchical_digest
    ):
        raise ValueError(f"{label} hierarchical-plan digest mismatch")
    if map_evidence is not None:
        declared_map = _mapping(evidence.get("mapObjects"), f"{label} mapObjects")
        if dict(declared_map) != dict(map_evidence):
            raise ValueError(f"{label} map-object evidence mismatch")


def _validate_upstream_plans(
    static_prop_plan: Mapping[str, Any],
    hierarchical_prop_plan: Mapping[str, Any],
    animated_prop_plan: Mapping[str, Any],
    *,
    visual_digest: str,
    dependency_digest: str,
    manifest_evidence: Mapping[str, Any],
    map_evidence: Mapping[str, Any],
) -> dict[str, str]:
    """Seal the target-specific handoff between the three prior planners."""

    static = _mapping(static_prop_plan, "retail static-prop plan")
    if (
        static.get("schema") != STATIC_PROP_PLAN_SCHEMA
        or static.get("schemaVersion") != STATIC_PROP_PLAN_SCHEMA_VERSION
    ):
        raise ValueError("unsupported retail static-prop plan")
    static_digest = _document_digest(static, "retail static-prop plan")
    _require_source_evidence(
        static,
        label="retail static-prop plan",
        visual_digest=visual_digest,
        dependency_digest=dependency_digest,
        manifest_evidence=manifest_evidence,
        map_evidence=None,
    )
    static_eligible = _target_names(
        static.get("eligibleTargets"), "targetObject", "static eligibleTargets"
    )
    if TARGET_OBJECT in static_eligible:
        raise ValueError(f"{TARGET_OBJECT} is already covered by the static plan")
    static_row = _exact_named_row(
        static.get("ineligibleTargets"),
        field="targetObject",
        name=TARGET_OBJECT,
        label="static ineligibleTargets",
    )
    if static_row.get("reasons") != [
        {"code": "model-w3d-contains-animation-headers", "headerCount": 2},
        {"code": "model-w3d-contains-hierarchy-headers", "headerCount": 1},
    ]:
        raise ValueError(f"{TARGET_OBJECT} static exclusion evidence changed")

    hierarchical = _mapping(hierarchical_prop_plan, "retail hierarchical-prop plan")
    if (
        hierarchical.get("schema") != HIERARCHICAL_PROP_PLAN_SCHEMA
        or hierarchical.get("schemaVersion") != HIERARCHICAL_PROP_PLAN_SCHEMA_VERSION
    ):
        raise ValueError("unsupported retail hierarchical-prop plan")
    hierarchical_digest = _document_digest(
        hierarchical, "retail hierarchical-prop plan"
    )
    _require_source_evidence(
        hierarchical,
        label="retail hierarchical-prop plan",
        visual_digest=visual_digest,
        dependency_digest=dependency_digest,
        manifest_evidence=manifest_evidence,
        map_evidence=map_evidence,
        static_digest=static_digest,
    )
    hierarchical_eligible = _target_names(
        hierarchical.get("eligibleTargets"),
        "targetObject",
        "hierarchical eligibleTargets",
    )
    if TARGET_OBJECT in hierarchical_eligible:
        raise ValueError(f"{TARGET_OBJECT} is already covered by the hierarchical plan")
    hierarchical_row = _exact_named_row(
        hierarchical.get("rejectedTargets"),
        field="targetObject",
        name=TARGET_OBJECT,
        label="hierarchical rejectedTargets",
    )
    if hierarchical_row.get("placementCount") != 1 or hierarchical_row.get(
        "reasons"
    ) != [{"code": "model-w3d-contains-animation-headers", "headerCount": 2}]:
        raise ValueError(f"{TARGET_OBJECT} hierarchical exclusion evidence changed")
    if hierarchical_row.get("staticPlanReasons") != static_row.get("reasons"):
        raise ValueError("hierarchical plan did not preserve the static exclusion")

    animated = _mapping(animated_prop_plan, "retail animated-prop plan")
    if (
        animated.get("schema") != ANIMATED_PROP_PLAN_SCHEMA
        or animated.get("schemaVersion") != ANIMATED_PROP_PLAN_SCHEMA_VERSION
    ):
        raise ValueError("unsupported retail animated-prop plan")
    animated_digest = _document_digest(animated, "retail animated-prop plan")
    _require_source_evidence(
        animated,
        label="retail animated-prop plan",
        visual_digest=visual_digest,
        dependency_digest=dependency_digest,
        manifest_evidence=manifest_evidence,
        map_evidence=map_evidence,
        static_digest=static_digest,
        hierarchical_digest=hierarchical_digest,
    )
    candidates = _list(animated.get("candidateTargets"), "animated candidateTargets")
    if candidates.count(TARGET_OBJECT) != 1:
        raise ValueError(f"{TARGET_OBJECT} is not one exact animated candidate")
    animated_eligible = _target_names(
        animated.get("eligibleTargets"),
        "targetObject",
        "animated eligibleTargets",
    )
    if TARGET_OBJECT in animated_eligible:
        raise ValueError(f"{TARGET_OBJECT} is already covered by the animated plan")
    animated_row = _exact_named_row(
        animated.get("rejectedTargets"),
        field="targetObject",
        name=TARGET_OBJECT,
        label="animated rejectedTargets",
    )
    if animated_row != {
        "targetObject": TARGET_OBJECT,
        "placementCount": 1,
        "reasons": [{"code": "embedded-animation-has-no-key-channel"}],
        "hierarchicalPlanReasons": hierarchical_row["reasons"],
    }:
        raise ValueError(f"{TARGET_OBJECT} animated rejection evidence changed")

    fragment = _mapping(animated.get("profileFragment"), "animated profileFragment")
    bindings = _mapping(fragment.get("objectBindings"), "animated objectBindings")
    bound_names = _target_names(
        bindings.get("models"), "typeName", "animated model bindings"
    )
    if TARGET_OBJECT in bound_names:
        raise ValueError(f"{TARGET_OBJECT} already has an animated model binding")
    for position, value in enumerate(
        _list(fragment.get("resources"), "animated resources")
    ):
        resource = _mapping(value, f"animated resource {position}")
        patterns = _list(
            resource.get("patterns"), f"animated resource {position} patterns"
        )
        if MODEL_VIRTUAL_PATH in patterns:
            raise ValueError("animated resources already own the no-motion model")

    return {
        "staticPropPlanAggregateSha256": static_digest,
        "hierarchicalPropPlanAggregateSha256": hierarchical_digest,
        "animatedPropPlanAggregateSha256": animated_digest,
    }


def _validate_visual_target(
    closure: Mapping[str, Any],
    sources: Mapping[str, Mapping[str, Any]],
) -> dict[str, Any]:
    if TARGET_OBJECT not in closure["targets"]:
        raise ValueError(f"visual closure omits {TARGET_OBJECT}")
    target = _mapping(closure["targetRecords"].get(TARGET_OBJECT), "target record")
    if target.get("status") != "resolved" or target.get("name") != TARGET_OBJECT:
        raise ValueError(f"{TARGET_OBJECT} target record is not exactly resolved")
    object_row = _mapping(closure["objects"].get(TARGET_OBJECT), "object summary")
    if (
        object_row.get("inheritanceComplete") is not True
        or object_row.get("ancestry") != [TARGET_OBJECT]
        or object_row.get("lifecycleCoverage") != ["intact"]
        or object_row.get("drawModuleCount") != 1
    ):
        raise ValueError(f"{TARGET_OBJECT} object semantics changed")
    if closure["diagnosticsByTarget"].get(TARGET_OBJECT) != []:
        raise ValueError(f"{TARGET_OBJECT} has object-graph diagnostics")

    leaves = closure["leavesByTarget"].get(TARGET_OBJECT)
    if not isinstance(leaves, list) or len(leaves) != 1:
        raise ValueError(f"{TARGET_OBJECT} must have one exact visual leaf")
    leaf = _mapping(leaves[0], f"{TARGET_OBJECT} visual leaf")
    if (
        leaf.get("kind") != "model"
        or leaf.get("identifier") != "PMMeatRack01"
        or leaf.get("status") != "resolved"
        or leaf.get("physicalVirtualPaths") != [MODEL_VIRTUAL_PATH]
        or leaf.get("conditions") != []
        or leaf.get("lifecyclePhases") != ["intact"]
    ):
        raise ValueError(f"{TARGET_OBJECT} visual leaf changed")

    scanned = _mapping(closure["scanned"].get(MODEL_VIRTUAL_PATH), "scanned W3D")
    model_source = _source_record(
        MODEL_VIRTUAL_PATH,
        sources,
        expected_sha256=str(scanned.get("sha256")),
        expected_size=int(scanned.get("byteLength", -1)),
    )
    if scanned.get("warnings") != []:
        raise ValueError("Orc meat-rack W3D scanner warnings are not allowed")
    if scanned.get("headerIds") != {
        "virtualPath": MODEL_VIRTUAL_PATH,
        "modelIds": [MESH_IDENTIFIER, MODEL_IDENTIFIER],
        "animationIds": [MODEL_IDENTIFIER, f"{MODEL_IDENTIFIER}.{MODEL_IDENTIFIER}"],
        "hierarchyIds": [MODEL_IDENTIFIER],
    }:
        raise ValueError("Orc meat-rack W3D header IDs changed")
    references = _list(scanned.get("modelReferences"), "scanned modelReferences")
    if len(references) != 1:
        raise ValueError("Orc meat-rack W3D must have one exact model reference")
    reference = _mapping(references[0], "scanned model reference")
    if {
        "identifier": reference.get("identifier"),
        "boneIndex": reference.get("boneIndex"),
        "role": reference.get("role"),
    } != {"identifier": MESH_IDENTIFIER, "boneIndex": 0, "role": "lod"}:
        raise ValueError("Orc meat-rack W3D root-rigid binding changed")

    embedded = closure["embeddedByW3d"].get(MODEL_VIRTUAL_PATH)
    if not isinstance(embedded, list) or len(embedded) != 1:
        raise ValueError("Orc meat-rack W3D must have one embedded texture")
    texture = _mapping(embedded[0], "embedded texture")
    if (
        texture.get("identifier") != TEXTURE_IDENTIFIER
        or texture.get("status") != "resolved"
        or texture.get("physicalVirtualPaths") != [TEXTURE_VIRTUAL_PATH]
        or texture.get("sourceW3dVirtualPath") != MODEL_VIRTUAL_PATH
    ):
        raise ValueError("Orc meat-rack embedded texture closure changed")
    texture_source = _source_record(TEXTURE_VIRTUAL_PATH, sources)
    return {
        "modelSource": model_source,
        "textureSource": texture_source,
        "embeddedTextureEvidence": deepcopy(dict(texture)),
    }


def _private_root(value: Path | str) -> Path:
    root = Path(value).expanduser().resolve()
    private = (repo_root_from_module() / "workspace").resolve()
    try:
        root.relative_to(private)
    except ValueError as exc:
        raise ValueError(
            "effective-assets root must remain below repository workspace"
        ) from exc
    if not root.is_dir():
        raise ValueError("effective-assets root is not a directory")
    return root


def _read_exact_source(
    root: Path,
    virtual_path: str,
    source: Mapping[str, Any],
    *,
    maximum: int,
) -> bytes:
    candidate = root.joinpath(*safe_relative_parts(virtual_path)).resolve()
    try:
        candidate.relative_to(root)
    except ValueError as exc:
        raise ValueError(
            f"source escapes effective-assets root: {virtual_path}"
        ) from exc
    if not candidate.is_file():
        raise ValueError(f"effective source is missing: {virtual_path}")
    expected_size = source.get("byteLength")
    if not isinstance(expected_size, int) or isinstance(expected_size, bool):
        raise ValueError(f"source byte length is invalid: {virtual_path}")
    if expected_size < 0 or expected_size > maximum:
        raise ValueError(f"source exceeds bounded read limit: {virtual_path}")
    if candidate.stat().st_size != expected_size:
        raise ValueError(f"effective source byte length mismatch: {virtual_path}")
    data = candidate.read_bytes()
    if len(data) != expected_size or _sha256_bytes(data) != source.get("sha256"):
        raise ValueError(f"effective source digest mismatch: {virtual_path}")
    return data


def _metadata_rows(metadata: W3DMetadata) -> dict[str, list[dict[str, object]]]:
    return {
        "meshHeaders": [item.neutral() for item in metadata.mesh_headers],
        "modelHeaders": [item.neutral() for item in metadata.model_headers],
        "hierarchyHeaders": [item.neutral() for item in metadata.hierarchy_headers],
        "hierarchyPivots": [item.neutral() for item in metadata.hierarchy_pivots],
        "animationHeaders": [item.neutral() for item in metadata.animation_headers],
        "modelReferences": [item.neutral() for item in metadata.model_references],
    }


def _chunk_kind_rows(metadata: W3DMetadata) -> list[dict[str, Any]]:
    counts: Counter[tuple[int, str | None, str]] = Counter(
        (item.chunk_id, item.chunk_name, item.classification)
        for item in metadata.chunks
    )
    result = []
    for (chunk_id, chunk_name, classification), count in sorted(counts.items()):
        row: dict[str, Any] = {
            "chunkId": chunk_id,
            "chunkIdHex": f"0x{chunk_id:08X}",
            "classification": classification,
            "count": count,
        }
        if chunk_name is not None:
            row["chunkName"] = chunk_name
        result.append(row)
    return result


def _census_file_core(value: Mapping[str, Any]) -> dict[str, Any]:
    return {
        "virtualPath": value.get("virtualPath"),
        "byteLength": value.get("byteLength"),
        "sha256": value.get("sha256"),
        "archive": value.get("archive"),
        "precedence": value.get("precedence"),
    }


def _manifest_file_core(source: Mapping[str, Any], path: str) -> dict[str, Any]:
    provenance = _mapping(source.get("source"), f"source provenance for {path}")
    return {
        "virtualPath": path,
        "byteLength": source.get("byteLength"),
        "sha256": source.get("sha256"),
        "archive": provenance.get("archive"),
        "precedence": provenance.get("precedence"),
    }


def _validate_census(
    unresolved_census: Mapping[str, Any],
    *,
    metadata: W3DMetadata,
    model_source: Mapping[str, Any],
    texture_source: Mapping[str, Any],
    manifest_evidence: Mapping[str, Any],
    map_document: Mapping[str, Any],
    map_evidence: Mapping[str, Any],
) -> dict[str, Any]:
    census = _mapping(unresolved_census, "Fords unresolved census")
    if (
        census.get("schema") != UNRESOLVED_CENSUS_SCHEMA
        or census.get("schemaVersion") != UNRESOLVED_CENSUS_SCHEMA_VERSION
    ):
        raise ValueError("unsupported Fords unresolved census")
    census_digest = _document_digest(census, "Fords unresolved census")
    source_evidence = _mapping(census.get("sourceEvidence"), "census sourceEvidence")
    census_assets = _mapping(
        source_evidence.get("effectiveAssetsManifest"),
        "census effectiveAssetsManifest",
    )
    for field in (
        "aggregateSha256",
        "catalogIdentitySha256",
        "fileCount",
        "byteLength",
    ):
        if census_assets.get(field) != manifest_evidence.get(field):
            raise ValueError(f"census effective-assets {field} mismatch")
    census_map = _mapping(source_evidence.get("mapObjects"), "census mapObjects")
    if (
        census_map.get("schema") != map_evidence.get("schema")
        or census_map.get("schemaVersion") != map_evidence.get("schemaVersion")
        or census_map.get("recordCount") != map_evidence.get("objectRecordCount")
        or census_map.get("sourceDocumentAggregateSha256")
        != map_evidence.get("documentAggregateSha256")
    ):
        raise ValueError("census map-object evidence mismatch")

    targets = _list(census.get("targets"), "census targets")
    names = [
        _text(
            _mapping(
                _mapping(value, f"census target {index}").get("objectDefinition"),
                f"census target {index} objectDefinition",
            ).get("name"),
            f"census target {index} name",
        )
        for index, value in enumerate(targets)
    ]
    _case_unique(names, "census target names")
    matches = [
        value
        for value, name in zip(targets, names, strict=True)
        if name == TARGET_OBJECT
    ]
    if len(matches) != 1:
        raise ValueError(f"census must contain exactly one {TARGET_OBJECT} target")
    target = _mapping(matches[0], f"census {TARGET_OBJECT}")

    definition = _mapping(target.get("objectDefinition"), "census objectDefinition")
    if (
        definition.get("name") != TARGET_OBJECT
        or definition.get("kind") != "Object"
        or definition.get("parent") is not None
        or definition.get("ancestry") != [TARGET_OBJECT]
        or definition.get("inheritanceComplete") is not True
        or definition.get("sourceVirtualPath") != OBJECT_DEFINITION_VIRTUAL_PATH
    ):
        raise ValueError("census object-definition evidence changed")

    placements = _mapping(target.get("mapPlacements"), "census mapPlacements")
    if placements != {
        "count": 1,
        "records": [
            {
                "recordIndex": EXPECTED_PLACEMENT_INDEX,
                "uniqueId": EXPECTED_PLACEMENT_UNIQUE_ID,
            }
        ],
    }:
        raise ValueError("census Orc meat-rack placement evidence changed")
    objects = _list(map_document.get("objects"), "SAGE map objects")
    if EXPECTED_PLACEMENT_INDEX >= len(objects):
        raise ValueError("census Orc meat-rack placement index is out of range")
    map_row = _mapping(objects[EXPECTED_PLACEMENT_INDEX], "Orc map placement")
    properties = _mapping(map_row.get("properties"), "Orc map placement properties")
    if (
        map_row.get("index") != EXPECTED_PLACEMENT_INDEX
        or map_row.get("typeName") != TARGET_OBJECT
        or map_row.get("roadType") != 0
        or properties.get("uniqueID") != EXPECTED_PLACEMENT_UNIQUE_ID
    ):
        raise ValueError("census placement does not match the cooked map object")

    visual = _mapping(target.get("visualReferences"), "census visualReferences")
    references = _list(visual.get("references"), "census visual references")
    if visual.get("count") != 1 or len(references) != 1:
        raise ValueError("census Orc meat-rack must have one visual reference")
    reference = _mapping(references[0], "census Orc visual reference")
    physical_files = _list(reference.get("physicalFiles"), "census visual files")
    if (
        reference.get("targetObject") != TARGET_OBJECT
        or reference.get("identifier") != "PMMeatRack01"
        or reference.get("kind") != "model"
        or reference.get("status") != "resolved"
        or reference.get("conditions") != []
        or reference.get("lifecyclePhases") != ["intact"]
        or len(physical_files) != 1
        or _census_file_core(_mapping(physical_files[0], "census visual file"))
        != _manifest_file_core(model_source, MODEL_VIRTUAL_PATH)
    ):
        raise ValueError("census Orc meat-rack visual source changed")

    closure = _mapping(target.get("physicalClosure"), "census physicalClosure")
    closure_files = [
        _mapping(value, f"census physical file {index}")
        for index, value in enumerate(
            _list(closure.get("files"), "census physicalClosure files")
        )
    ]
    model_files = [
        item
        for item in closure_files
        if MODEL_VIRTUAL_PATH == item.get("virtualPath")
        and "w3d-model" in _list(item.get("roles"), "census file roles")
    ]
    texture_files = [
        item
        for item in closure_files
        if TEXTURE_VIRTUAL_PATH == item.get("virtualPath")
        and "w3d-embedded-texture" in _list(item.get("roles"), "census file roles")
    ]
    if len(model_files) != 1 or _census_file_core(
        model_files[0]
    ) != _manifest_file_core(model_source, MODEL_VIRTUAL_PATH):
        raise ValueError("census physical W3D source mismatch")
    if len(texture_files) != 1 or _census_file_core(
        texture_files[0]
    ) != _manifest_file_core(texture_source, TEXTURE_VIRTUAL_PATH):
        raise ValueError("census physical texture source mismatch")

    w3d_closure = _mapping(target.get("w3dClosure"), "census w3dClosure")
    w3d_files = _list(w3d_closure.get("files"), "census w3dClosure files")
    if w3d_closure.get("fileCount") != 1 or len(w3d_files) != 1:
        raise ValueError("census must contain one exact Orc meat-rack W3D")
    w3d = _mapping(w3d_files[0], "census Orc W3D")
    if _census_file_core(
        _mapping(w3d.get("file"), "census W3D file")
    ) != _manifest_file_core(model_source, MODEL_VIRTUAL_PATH):
        raise ValueError("census W3D file identity mismatch")
    expected_headers = _metadata_rows(metadata)
    for field, expected in expected_headers.items():
        if w3d.get(field) != expected:
            raise ValueError(f"census W3D {field} disagrees with fresh metadata")
    if w3d.get("chunkKinds") != _chunk_kind_rows(metadata):
        raise ValueError("census W3D chunk kinds disagree with fresh metadata")
    if w3d.get("warnings") != [item.neutral() for item in metadata.warnings]:
        raise ValueError("census W3D warnings disagree with fresh metadata")
    header_ids = metadata.file_headers()
    if w3d.get("fileHeaders") != {
        "virtualPath": MODEL_VIRTUAL_PATH,
        "modelIds": list(header_ids.model_ids),
        "animationIds": list(header_ids.animation_ids),
        "hierarchyIds": list(header_ids.hierarchy_ids),
    }:
        raise ValueError("census W3D file headers disagree with fresh metadata")
    count_fields = {
        "meshHeaderCount": len(metadata.mesh_headers),
        "modelHeaderCount": len(metadata.model_headers),
        "hierarchyHeaderCount": len(metadata.hierarchy_headers),
        "animationHeaderCount": len(metadata.animation_headers),
    }
    for field, expected in count_fields.items():
        if w3d.get(field) != expected:
            raise ValueError(f"census W3D {field} disagrees with fresh metadata")
    if w3d.get("particleFamilyChunkCount") != 0:
        raise ValueError("Orc meat-rack unexpectedly contains particle-family chunks")
    if w3d_closure.get("requiredHierarchyIds") != [MODEL_IDENTIFIER]:
        raise ValueError("census required hierarchy identity changed")

    embedded = _list(w3d.get("embeddedTextures"), "census embeddedTextures")
    if len(embedded) != 1:
        raise ValueError("census W3D must contain one embedded texture")
    embedded_row = _mapping(embedded[0], "census embedded texture")
    embedded_files = _list(
        embedded_row.get("physicalFiles"), "census embedded texture files"
    )
    if (
        embedded_row.get("identifier") != TEXTURE_IDENTIFIER
        or embedded_row.get("status") != "resolved"
        or embedded_row.get("sourceW3dVirtualPath") != MODEL_VIRTUAL_PATH
        or len(embedded_files) != 1
        or _census_file_core(
            _mapping(embedded_files[0], "census embedded texture file")
        )
        != _manifest_file_core(texture_source, TEXTURE_VIRTUAL_PATH)
    ):
        raise ValueError("census embedded texture evidence changed")

    return {
        "aggregateSha256": census_digest,
        "visualClosureAggregateSha256": _mapping(
            source_evidence.get("visualClosure"), "census visualClosure"
        ).get("aggregateSha256"),
        "targetCount": len(targets),
        "placementRecordIndex": EXPECTED_PLACEMENT_INDEX,
    }


def _validate_generated_profile(resources: list[dict[str, Any]]) -> bool:
    payload = {
        "format": 1,
        "id": "no-motion-prop-fragment-validation",
        "pack": {"id": "no-motion-prop-fragment-validation-pack"},
        "resources": resources,
    }
    with tempfile.TemporaryDirectory(prefix="openbfme-no-motion-profile-") as raw:
        path = Path(raw) / "profile.json"
        path.write_text(json.dumps(payload), encoding="utf-8")
        parsed = ImportProfile.load(path)
    if len(parsed.resources) != len(resources):
        raise ValueError("generated ImportProfile changed no-motion resource count")
    return True


def build_retail_no_motion_prop_plan(
    visual_closure_report: Mapping[str, Any],
    static_prop_plan: Mapping[str, Any],
    hierarchical_prop_plan: Mapping[str, Any],
    animated_prop_plan: Mapping[str, Any],
    effective_assets_manifest: Mapping[str, Any],
    map_objects_document: Mapping[str, Any],
    unresolved_census: Mapping[str, Any],
    effective_assets_root: Path | str,
) -> dict[str, Any]:
    """Return the one exact, source-proven OrcMeatRack01 profile fragment."""

    manifest = _mapping(effective_assets_manifest, "effective-assets manifest")
    sources, manifest_evidence = _validate_effective_manifest(manifest)
    closure = _validate_visual_closure(
        _mapping(visual_closure_report, "retail visual closure"), sources
    )
    visual_target = _validate_visual_target(closure, sources)
    placements, map_evidence = _validate_placement_document(
        _mapping(map_objects_document, "SAGE map objects"), closure["targets"]
    )
    if placements.get(TARGET_OBJECT) != 1:
        raise ValueError(f"{TARGET_OBJECT} must have exactly one non-road placement")
    upstream = _validate_upstream_plans(
        static_prop_plan,
        hierarchical_prop_plan,
        animated_prop_plan,
        visual_digest=closure["reportDigest"],
        dependency_digest=closure["dependencyDigest"],
        manifest_evidence=manifest_evidence,
        map_evidence=map_evidence,
    )

    root = _private_root(effective_assets_root)
    model_bytes = _read_exact_source(
        root,
        MODEL_VIRTUAL_PATH,
        visual_target["modelSource"],
        maximum=_MAX_W3D_BYTES,
    )
    texture_bytes = _read_exact_source(
        root,
        TEXTURE_VIRTUAL_PATH,
        visual_target["textureSource"],
        maximum=_MAX_TEXTURE_BYTES,
    )
    metadata = scan_w3d_metadata(model_bytes, MODEL_VIRTUAL_PATH)
    if metadata.warnings:
        raise ValueError("fresh Orc meat-rack W3D metadata contains warnings")
    census_evidence = _validate_census(
        unresolved_census,
        metadata=metadata,
        model_source=visual_target["modelSource"],
        texture_source=visual_target["textureSource"],
        manifest_evidence=manifest_evidence,
        map_document=map_objects_document,
        map_evidence=map_evidence,
    )

    expectation = W3DNoMotionExpectation(
        identifier=MODEL_IDENTIFIER,
        hierarchy_identifier=MODEL_IDENTIFIER,
        frame_count=ANIMATION_FRAME_COUNT,
        frame_rate=ANIMATION_FRAME_RATE,
        compressed=False,
        model_identifier=MODEL_IDENTIFIER,
    )
    transformed = strip_proven_header_only_animations(
        model_bytes,
        virtual_path=MODEL_VIRTUAL_PATH,
        expectations=[expectation],
    )
    proof = transformed.proof.neutral()
    if proof["inputSha256"] != visual_target["modelSource"]["sha256"]:
        raise ValueError("no-motion proof input disagrees with the sealed source")
    if proof["removedContainerCount"] != 1 or proof["headers"] != [
        {
            **proof["headers"][0],
            "identifier": MODEL_IDENTIFIER,
            "hierarchyIdentifier": MODEL_IDENTIFIER,
            "frameCount": ANIMATION_FRAME_COUNT,
            "frameRate": ANIMATION_FRAME_RATE,
            "compressed": False,
            "modelIdentifier": MODEL_IDENTIFIER,
        }
    ]:
        raise ValueError("no-motion proof did not remove one exact expected header")
    # Reading the texture is part of the evidence gate; retain only its digest.
    if _sha256_bytes(texture_bytes) != visual_target["textureSource"]["sha256"]:
        raise ValueError("fresh embedded texture digest changed after validation")

    texture_id = _stable_slug("no-motion-prop-texture", TEXTURE_VIRTUAL_PATH)
    texture_stem = texture_id.removeprefix("no-motion-prop-texture-")
    model_id = _stable_slug("no-motion-prop-model", MODEL_VIRTUAL_PATH)
    model_stem = model_id.removeprefix("no-motion-prop-model-")
    texture_output = f"assets/textures/props-no-motion/{texture_stem}.png"
    model_output = f"assets/models/props-no-motion/{model_stem}.glb"
    declaration = {
        "identifier": MODEL_IDENTIFIER,
        "hierarchyIdentifier": MODEL_IDENTIFIER,
        "frameCount": ANIMATION_FRAME_COUNT,
        "frameRate": ANIMATION_FRAME_RATE,
        "compressed": False,
        "modelIdentifier": MODEL_IDENTIFIER,
    }
    resources = [
        {
            "id": texture_id,
            "kind": "texture",
            "patterns": [TEXTURE_VIRTUAL_PATH],
            "required": True,
            "converter": "texture",
            "output": texture_output,
            "limit": 1,
            "expected_count": 1,
        },
        {
            "id": model_id,
            "kind": "model",
            "patterns": [MODEL_VIRTUAL_PATH],
            "required": True,
            "converter": "w3d-hierarchical",
            "output": model_output,
            "limit": 1,
            "expected_count": 1,
            "options": {
                "model": PurePosixPath(MODEL_VIRTUAL_PATH).name,
                "animations": [],
                "required_equipment": [],
                "inputResourceIds": [texture_id],
                "provenRootRigidBake": True,
                "provenNoMotionAnimations": [declaration],
            },
        },
    ]
    _case_unique([str(item["id"]) for item in resources], "no-motion resources")
    _case_unique(
        [str(item["output"]) for item in resources], "no-motion resource outputs"
    )
    profile_validated = _validate_generated_profile(resources)
    binding = {
        "typeName": TARGET_OBJECT,
        "sourceVirtualModel": MODEL_VIRTUAL_PATH,
        "glb": model_output,
        "matchMethod": "exact-type-name",
    }
    plan: dict[str, Any] = {
        "schema": NO_MOTION_PROP_PLAN_SCHEMA,
        "schemaVersion": NO_MOTION_PROP_PLAN_SCHEMA_VERSION,
        "sourceEvidence": {
            "visualClosureAggregateSha256": closure["reportDigest"],
            "w3dDependencyAggregateSha256": closure["dependencyDigest"],
            **upstream,
            "effectiveAssets": manifest_evidence,
            "mapObjects": map_evidence,
            "unresolvedCensus": census_evidence,
            "privateReadBoundary": {
                "policy": "exact-manifest-source-below-repository-private-root",
                "sourceCount": 2,
                "byteLength": len(model_bytes) + len(texture_bytes),
                "sources": [
                    deepcopy(visual_target["modelSource"]),
                    deepcopy(visual_target["textureSource"]),
                ],
            },
        },
        "policy": {
            "selection": "one-exact-header-only-no-motion-retail-w3d",
            "targetObject": TARGET_OBJECT,
            "substitutesAllowed": False,
            "animationClipsEmitted": False,
            "rootRigidBakeRequiresSingleRootBoneZeroProof": True,
            "converter": "w3d-hierarchical",
            "profileFragmentValidatedByImportProfile": profile_validated,
        },
        "eligibleTargets": [
            {
                "targetObject": TARGET_OBJECT,
                "placementCount": 1,
                "modelVirtualPath": MODEL_VIRTUAL_PATH,
                "textureVirtualPaths": [TEXTURE_VIRTUAL_PATH],
                "provenRootRigidBake": True,
                "animations": [],
                "provenNoMotionAnimations": [declaration],
            }
        ],
        "conversionGroups": [
            {
                "modelResourceId": model_id,
                "targetObjects": [TARGET_OBJECT],
                "placementCount": 1,
                "outputGlb": model_output,
                "modelSource": deepcopy(visual_target["modelSource"]),
                "textureSources": [deepcopy(visual_target["textureSource"])],
                "freshMetadata": {
                    "modelHeaderIds": list(metadata.model_ids),
                    "hierarchyHeaderIds": list(metadata.hierarchy_ids),
                    "animationHeaderIds": list(metadata.animation_ids),
                    "modelReferences": [
                        item.neutral() for item in metadata.model_references
                    ],
                },
                "noMotionProof": proof,
            }
        ],
        "profileFragment": {
            "resources": resources,
            "objectBindings": {"models": [binding]},
        },
        "placementCoverage": {
            "noMotionBatchPlacementCount": 1,
            "byTarget": [
                {
                    "targetObject": TARGET_OBJECT,
                    "placementCount": 1,
                    "coverageStage": "no-motion",
                }
            ],
        },
        "summary": {
            "eligibleTargetTypeCount": 1,
            "conversionGroupCount": 1,
            "profileResourceCount": 2,
            "modelResourceCount": 1,
            "textureResourceCount": 1,
            "sourcePatternCount": 2,
            "objectBindingModelRowCount": 1,
            "placementCount": 1,
            "animationClipCount": 0,
            "removedHeaderOnlyAnimationCount": 1,
        },
    }
    plan["aggregateSha256"] = _canonical_sha256(plan)
    return plan


def generated_import_profile(
    plan: Mapping[str, Any],
    *,
    profile_id: str = "men-fords-v0-no-motion-prop-generated",
    pack_id: str = "bfme2-men-vslice-no-motion-prop-private",
) -> dict[str, Any]:
    """Return a standalone, non-publishing ImportProfile for this fragment."""

    document = _mapping(plan, "retail no-motion-prop plan")
    if (
        document.get("schema") != NO_MOTION_PROP_PLAN_SCHEMA
        or document.get("schemaVersion") != NO_MOTION_PROP_PLAN_SCHEMA_VERSION
    ):
        raise ValueError("unsupported retail no-motion-prop plan")
    _document_digest(document, "retail no-motion-prop plan")
    fragment = _mapping(document.get("profileFragment"), "profileFragment")
    resources = _list(fragment.get("resources"), "profileFragment resources")
    if len(resources) != 2:
        raise ValueError(
            "no-motion profile fragment must contain exactly two resources"
        )
    profile = {
        "format": 1,
        "id": profile_id,
        "title": "Private BFME II Fords exact no-motion prop batch",
        "pack": {
            "id": pack_id,
            "version": "1.06-plan-v0",
            "dataPolicy": {
                "externalPathsAllowed": False,
                "redistributable": False,
            },
        },
        "resources": deepcopy(resources),
    }
    with tempfile.TemporaryDirectory(prefix="openbfme-no-motion-generated-") as raw:
        path = Path(raw) / "profile.json"
        path.write_text(json.dumps(profile), encoding="utf-8")
        ImportProfile.load(path)
    return profile


def load_retail_no_motion_prop_plan_inputs(
    visual_closure_path: Path | str,
    static_prop_plan_path: Path | str,
    hierarchical_prop_plan_path: Path | str,
    animated_prop_plan_path: Path | str,
    effective_assets_manifest_path: Path | str,
    map_objects_path: Path | str,
    unresolved_census_path: Path | str,
) -> tuple[dict[str, Any], ...]:
    """Load bounded JSON inputs; semantic and byte checks occur in build."""

    def load(path: Path | str, label: str) -> dict[str, Any]:
        source = Path(path).expanduser().resolve()
        if not source.is_file() or source.stat().st_size > _MAX_JSON_BYTES:
            raise ValueError(f"{label} is missing or exceeds {_MAX_JSON_BYTES} bytes")
        try:
            value = json.loads(source.read_text(encoding="utf-8"))
        except (OSError, UnicodeDecodeError, json.JSONDecodeError) as exc:
            raise ValueError(f"invalid {label}: {exc}") from exc
        if not isinstance(value, dict):
            raise ValueError(f"{label} root must be an object")
        return value

    return (
        load(visual_closure_path, "retail visual closure"),
        load(static_prop_plan_path, "retail static-prop plan"),
        load(hierarchical_prop_plan_path, "retail hierarchical-prop plan"),
        load(animated_prop_plan_path, "retail animated-prop plan"),
        load(effective_assets_manifest_path, "effective-assets manifest"),
        load(map_objects_path, "SAGE map objects"),
        load(unresolved_census_path, "Fords unresolved census"),
    )


def write_retail_no_motion_prop_plan(path: Path | str, plan: Mapping[str, Any]) -> None:
    document = _mapping(plan, "retail no-motion-prop plan")
    if document.get("schema") != NO_MOTION_PROP_PLAN_SCHEMA:
        raise ValueError("cannot write unsupported no-motion-prop plan schema")
    _document_digest(document, "retail no-motion-prop plan")
    write_json_atomic(Path(path), deepcopy(dict(document)))


def write_generated_import_profile(
    path: Path | str, profile: Mapping[str, Any]
) -> None:
    document = _mapping(profile, "generated no-motion import profile")
    with tempfile.TemporaryDirectory(prefix="openbfme-no-motion-write-") as raw:
        check = Path(raw) / "profile.json"
        check.write_text(json.dumps(document), encoding="utf-8")
        ImportProfile.load(check)
    write_json_atomic(Path(path), deepcopy(dict(document)))


__all__ = [
    "NO_MOTION_PROP_PLAN_SCHEMA",
    "NO_MOTION_PROP_PLAN_SCHEMA_VERSION",
    "build_retail_no_motion_prop_plan",
    "generated_import_profile",
    "load_retail_no_motion_prop_plan_inputs",
    "write_generated_import_profile",
    "write_retail_no_motion_prop_plan",
]
