"""Plan exact static-prop conversions from a retail visual closure.

This module deliberately stops before conversion or publication.  It accepts
the neutral ``openbfme.retail-visual-closure`` report plus the verified
effective-assets manifest, selects only statically provable target Objects,
and emits a deterministic profile fragment and exact map binding rows.

No filename neighbour, unresolved candidate, or substitute is promoted.  A
target that cannot satisfy the complete static contract remains in the plan
with explicit reasons.
"""

from __future__ import annotations

from collections import Counter
from copy import deepcopy
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
from typing import Any, Mapping

from .paths import safe_relative_parts
from .util import write_json_atomic


VISUAL_CLOSURE_SCHEMA = "openbfme.retail-visual-closure"
VISUAL_CLOSURE_SCHEMA_VERSION = 1
EFFECTIVE_ASSETS_SCHEMA = "openbfme.effective-assets-manifest"
EFFECTIVE_ASSETS_SCHEMA_VERSION = 0
STATIC_PROP_PLAN_SCHEMA = "openbfme.retail-static-prop-plan"
STATIC_PROP_PLAN_SCHEMA_VERSION = 0

_HEX_SHA256 = re.compile(r"^[0-9a-f]{64}$")
_OBJECT_ID = re.compile(r"^[A-Za-z0-9_][A-Za-z0-9_.-]{0,254}$")
_FORBIDDEN_TARGET_LEAF_KINDS = frozenset(
    {"animation", "hierarchy", "particle", "attached-model"}
)
_TEXTURE_LEAF_KINDS = frozenset({"texture", "shadow"})
_TEXTURE_SUFFIXES = frozenset({".dds", ".tga", ".jpg", ".png"})
_SUPPORTED_W3D_MODEL_REFERENCE_ROLES = frozenset({"lod"})
_W3D_READ_POLICY = "resolved-target-w3d-leaves-plus-exact-unresolved-candidates"
_MAX_PROFILE_RESOURCES = 256


def _canonical_sha256(value: object) -> str:
    try:
        encoded = json.dumps(
            value,
            sort_keys=True,
            ensure_ascii=False,
            separators=(",", ":"),
            allow_nan=False,
        ).encode("utf-8")
    except (TypeError, ValueError) as exc:
        raise ValueError(f"document is not canonical JSON data: {exc}") from exc
    return hashlib.sha256(encoded).hexdigest()


def _is_int(value: object) -> bool:
    return isinstance(value, int) and not isinstance(value, bool)


def _mapping(value: object, label: str) -> Mapping[str, Any]:
    if not isinstance(value, Mapping):
        raise ValueError(f"{label} must be an object")
    return value


def _list(value: object, label: str) -> list[Any]:
    if not isinstance(value, list):
        raise ValueError(f"{label} must be an array")
    return value


def _text(value: object, label: str) -> str:
    if not isinstance(value, str) or not value:
        raise ValueError(f"{label} must be a non-empty string")
    return value


def _sha256(value: object, label: str) -> str:
    if not isinstance(value, str) or _HEX_SHA256.fullmatch(value) is None:
        raise ValueError(f"{label} must be a lowercase SHA-256")
    return value


def _nonnegative_int(value: object, label: str) -> int:
    if not _is_int(value) or int(value) < 0:
        raise ValueError(f"{label} must be a nonnegative integer")
    return int(value)


def _safe_virtual_path(value: object, label: str) -> str:
    path = _text(value, label)
    if "\\" in path:
        raise ValueError(f"{label} is not a canonical POSIX path: {path!r}")
    try:
        canonical = "/".join(safe_relative_parts(path))
    except ValueError as exc:
        raise ValueError(f"unsafe {label}: {path!r}") from exc
    if canonical != path:
        raise ValueError(f"{label} is not canonical: {path!r}")
    return path


def _safe_object_id(value: object, label: str) -> str:
    identifier = _text(value, label)
    if _OBJECT_ID.fullmatch(identifier) is None or identifier in {".", ".."}:
        raise ValueError(f"unsafe {label}: {identifier!r}")
    return identifier


def _case_unique(values: list[str] | tuple[str, ...], label: str) -> dict[str, str]:
    result: dict[str, str] = {}
    for value in values:
        key = value.casefold()
        previous = result.get(key)
        if previous is not None:
            if previous == value:
                raise ValueError(f"duplicate {label}: {value!r}")
            raise ValueError(f"case-ambiguous {label}: {previous!r}, {value!r}")
        result[key] = value
    return result


def _validate_declared_digest(
    document: Mapping[str, Any], field: str, label: str
) -> str:
    declared = _sha256(document.get(field), f"{label}.{field}")
    payload = dict(document)
    payload.pop(field, None)
    actual = _canonical_sha256(payload)
    if actual != declared:
        raise ValueError(
            f"{label} digest mismatch: declared {declared}, calculated {actual}"
        )
    return declared


def _effective_asset_aggregate(files: list[Mapping[str, Any]]) -> str:
    digest = hashlib.sha256()
    for item in files:
        digest.update(str(item["path"]).encode("utf-8"))
        digest.update(b"\0")
        digest.update(str(item["size"]).encode("ascii"))
        digest.update(b"\0")
        digest.update(str(item["sha256"]).encode("ascii"))
        digest.update(b"\n")
    return digest.hexdigest()


def _validate_effective_manifest(
    raw: Mapping[str, Any],
) -> tuple[dict[str, Mapping[str, Any]], dict[str, Any]]:
    if raw.get("schema") != EFFECTIVE_ASSETS_SCHEMA:
        raise ValueError("unsupported effective-assets manifest schema")
    if raw.get("schema_version") != EFFECTIVE_ASSETS_SCHEMA_VERSION:
        raise ValueError("unsupported effective-assets manifest schema version")

    files = _list(raw.get("files"), "effective-assets manifest files")
    expected_fields = {
        "archive",
        "offset",
        "path",
        "precedence",
        "sha256",
        "size",
    }
    validated: list[Mapping[str, Any]] = []
    paths: list[str] = []
    for position, value in enumerate(files):
        item = _mapping(value, f"effective-assets file {position}")
        if set(item) != expected_fields:
            raise ValueError(f"effective-assets file {position} has unsupported fields")
        archive = _safe_virtual_path(
            item.get("archive"), f"effective-assets file {position} archive"
        )
        path = _safe_virtual_path(
            item.get("path"), f"effective-assets file {position} path"
        )
        offset = _nonnegative_int(
            item.get("offset"), f"effective-assets file {position} offset"
        )
        precedence = _nonnegative_int(
            item.get("precedence"),
            f"effective-assets file {position} precedence",
        )
        size = _nonnegative_int(
            item.get("size"), f"effective-assets file {position} size"
        )
        digest = _sha256(item.get("sha256"), f"effective-assets file {position} sha256")
        paths.append(path)
        validated.append(
            {
                "archive": archive,
                "offset": offset,
                "path": path,
                "precedence": precedence,
                "sha256": digest,
                "size": size,
            }
        )
    path_cases = _case_unique(paths, "effective-assets virtual path")
    declared_aggregate = _sha256(
        raw.get("aggregate_sha256"),
        "effective-assets manifest aggregate_sha256",
    )
    calculated_aggregate = _effective_asset_aggregate(validated)
    if declared_aggregate != calculated_aggregate:
        raise ValueError("effective-assets manifest aggregate digest mismatch")

    totals = _mapping(raw.get("totals"), "effective-assets manifest totals")
    if totals.get("files") != len(validated) or totals.get("bytes") != sum(
        int(item["size"]) for item in validated
    ):
        raise ValueError("effective-assets manifest totals mismatch")

    catalog = _mapping(raw.get("catalog"), "effective-assets manifest catalog")
    install = _mapping(raw.get("install"), "effective-assets manifest install")
    catalog_identity = _sha256(
        catalog.get("identity_sha256"), "catalog identity_sha256"
    )
    install_identity = _sha256(
        install.get("identity_sha256"), "install identity_sha256"
    )
    by_path = {path_cases[item["path"].casefold()]: item for item in validated}
    return by_path, {
        "aggregateSha256": declared_aggregate,
        "catalogIdentitySha256": catalog_identity,
        "installIdentitySha256": install_identity,
        "fileCount": len(validated),
        "byteLength": sum(int(item["size"]) for item in validated),
    }


def _source_record(
    virtual_path: str,
    sources: Mapping[str, Mapping[str, Any]],
    *,
    expected_sha256: str | None = None,
    expected_size: int | None = None,
) -> dict[str, Any]:
    """Resolve a closure path against the effective-assets inventory.

    Layered RotWK extracts can mix path casings across overlay layers
    (``art/...`` vs ``Art/...``). The inventory key is the authority: when the
    only difference is case, rewrite to the manifest path rather than failing
    the whole map binding. True absence still fails closed.
    """
    source = sources.get(virtual_path)
    resolved_path = virtual_path
    if source is None:
        folded_matches = [
            path for path in sources if path.casefold() == virtual_path.casefold()
        ]
        if len(folded_matches) == 1:
            resolved_path = folded_matches[0]
            source = sources[resolved_path]
        elif len(folded_matches) > 1:
            raise ValueError(
                "closure path matches multiple case-colliding effective-assets "
                f"entries: {virtual_path!r} -> {folded_matches!r}"
            )
        else:
            raise ValueError(
                f"closure source is absent from effective-assets manifest: {virtual_path}"
            )
    if expected_sha256 is not None and source["sha256"] != expected_sha256:
        raise ValueError(f"closure source SHA-256 mismatch: {resolved_path}")
    if expected_size is not None and source["size"] != expected_size:
        raise ValueError(f"closure source byte length mismatch: {resolved_path}")
    return {
        "virtualPath": resolved_path,
        "byteLength": source["size"],
        "sha256": source["sha256"],
        "source": {
            "archive": source["archive"],
            "offset": source["offset"],
            "precedence": source["precedence"],
        },
    }


def _validate_id_array(value: object, label: str) -> list[str]:
    items = _list(value, label)
    identifiers = [_text(item, f"{label} entry") for item in items]
    _case_unique(identifiers, label)
    return identifiers


def _validate_visual_closure(
    raw: Mapping[str, Any], sources: Mapping[str, Mapping[str, Any]]
) -> dict[str, Any]:
    if raw.get("schema") != VISUAL_CLOSURE_SCHEMA:
        raise ValueError("unsupported retail visual closure schema")
    if raw.get("schemaVersion") != VISUAL_CLOSURE_SCHEMA_VERSION:
        raise ValueError("unsupported retail visual closure schema version")
    report_digest = _validate_declared_digest(
        raw, "aggregateSha256", "retail visual closure"
    )

    catalog = _mapping(raw.get("catalog"), "retail visual closure catalog")
    if catalog.get("w3dCatalogMode") != "path-only-plus-targeted-headers":
        raise ValueError("unsupported retail visual closure W3D catalog mode")
    missing_definitions = _list(
        raw.get("missingDefinitions"), "retail visual closure missingDefinitions"
    )

    dependency = _mapping(raw.get("w3dDependencyClosure"), "w3dDependencyClosure")
    dependency_digest = _validate_declared_digest(
        dependency, "aggregateSha256", "w3dDependencyClosure"
    )

    path_case_registry: dict[str, str] = {}

    def register_path(value: object, label: str) -> str:
        path = _safe_virtual_path(value, label)
        key = path.casefold()
        previous = path_case_registry.get(key)
        if previous is not None and previous != path:
            raise ValueError(f"case-ambiguous closure paths: {previous!r}, {path!r}")
        path_case_registry[key] = path
        return path

    target_rows = _list(raw.get("targets"), "retail visual closure targets")
    targets: list[str] = []
    target_records: dict[str, Mapping[str, Any]] = {}
    for position, value in enumerate(target_rows):
        item = _mapping(value, f"target {position}")
        name = _safe_object_id(item.get("requestedName"), "target Object id")
        status = item.get("status")
        if status not in {"resolved", "missing-definition"}:
            raise ValueError(f"target {name!r} has invalid status")
        targets.append(name)
        target_records[name] = item
    _case_unique(targets, "target Object id")

    object_rows = _list(raw.get("objects"), "retail visual closure objects")
    objects: dict[str, Mapping[str, Any]] = {}
    object_names: list[str] = []
    for position, value in enumerate(object_rows):
        item = _mapping(value, f"Object summary {position}")
        name = _safe_object_id(item.get("name"), "Object summary name")
        if name not in target_records:
            folded = [
                target for target in targets if target.casefold() == name.casefold()
            ]
            if folded:
                raise ValueError(f"Object summary case does not match target: {name!r}")
            raise ValueError(f"Object summary is not a requested target: {name!r}")
        if not isinstance(item.get("inheritanceComplete"), bool):
            raise ValueError(f"Object summary inheritanceComplete is invalid: {name!r}")
        object_names.append(name)
        objects[name] = item
    _case_unique(object_names, "Object summary name")
    resolved_targets = {
        name
        for name, item in target_records.items()
        if item.get("status") == "resolved"
    }
    if set(objects) != resolved_targets:
        raise ValueError("resolved target and Object summary sets do not match")

    exact_rows = _list(raw.get("exactLeaves"), "exactLeaves")
    semantic_rows = _list(raw.get("semanticLeaves"), "semanticLeaves")
    unresolved_root = _mapping(raw.get("unresolved"), "unresolved")
    unresolved_rows = _list(unresolved_root.get("references"), "unresolved.references")
    leaves_by_target: dict[str, list[Mapping[str, Any]]] = {
        name: [] for name in targets
    }

    def validate_leaf(
        value: object, position: int, expected_statuses: frozenset[str]
    ) -> Mapping[str, Any]:
        item = _mapping(value, f"visual leaf {position}")
        target = _safe_object_id(item.get("targetObject"), "leaf targetObject")
        if target not in target_records:
            folded = [name for name in targets if name.casefold() == target.casefold()]
            if folded:
                raise ValueError(f"leaf target case does not match: {target!r}")
            raise ValueError(f"leaf references an unknown target: {target!r}")
        _text(item.get("kind"), "leaf kind")
        _text(item.get("usage"), "leaf usage")
        _text(item.get("identifier"), "leaf identifier")
        status = item.get("status")
        if status not in expected_statuses:
            raise ValueError(f"leaf for {target!r} has invalid status")
        if status == "resolved":
            paths = _list(item.get("physicalVirtualPaths"), "leaf physical paths")
            if not paths:
                raise ValueError(f"resolved leaf for {target!r} has no physical path")
            exact_paths = [register_path(path, "leaf physical path") for path in paths]
            _case_unique(exact_paths, "leaf physical path")
        elif status == "semantic":
            if item.get("physicalVirtualPaths") is not None:
                if item.get("physicalVirtualPaths") != []:
                    raise ValueError("semantic leaf cannot contain a physical path")
        else:
            candidates = item.get("candidates", [])
            if candidates is not None:
                candidate_paths = [
                    register_path(path, "unresolved candidate path")
                    for path in _list(candidates, "unresolved leaf candidates")
                ]
                _case_unique(candidate_paths, "unresolved leaf candidate path")
        leaves_by_target[target].append(item)
        return item

    for index, value in enumerate(exact_rows):
        validate_leaf(value, index, frozenset({"resolved"}))
    for index, value in enumerate(semantic_rows):
        validate_leaf(value, index, frozenset({"semantic"}))
    for index, value in enumerate(unresolved_rows):
        validate_leaf(value, index, frozenset({"missing", "ambiguous", "invalid"}))

    scanned_rows = _list(raw.get("scannedW3d"), "scannedW3d")
    scanned: dict[str, Mapping[str, Any]] = {}
    scanned_paths: list[str] = []
    for position, value in enumerate(scanned_rows):
        item = _mapping(value, f"scannedW3d record {position}")
        path = register_path(item.get("virtualPath"), "scanned W3D path")
        if PurePosixPath(path).suffix.casefold() != ".w3d":
            raise ValueError(f"scanned W3D path is not a W3D: {path}")
        byte_length = _nonnegative_int(
            item.get("byteLength"), f"scanned W3D {path} byteLength"
        )
        digest = _sha256(item.get("sha256"), f"scanned W3D {path} sha256")
        header = _mapping(item.get("headerIds"), f"scanned W3D {path} headerIds")
        if header.get("virtualPath") != path:
            raise ValueError(f"scanned W3D header path mismatch: {path}")
        _validate_id_array(header.get("modelIds"), f"{path} modelIds")
        _validate_id_array(header.get("hierarchyIds"), f"{path} hierarchyIds")
        _validate_id_array(header.get("animationIds"), f"{path} animationIds")
        if "modelHierarchyIdentifiers" in item:
            _validate_id_array(
                item.get("modelHierarchyIdentifiers"),
                f"{path} modelHierarchyIdentifiers",
            )
        if "embeddedAnimationChannelCount" in item:
            _nonnegative_int(
                item.get("embeddedAnimationChannelCount"),
                f"{path} embeddedAnimationChannelCount",
            )
        if "skinnedMeshCount" in item:
            _nonnegative_int(
                item.get("skinnedMeshCount"),
                f"{path} skinnedMeshCount",
            )
        if "meshCount" in item:
            _nonnegative_int(item.get("meshCount"), f"{path} meshCount")
        if "hiddenMeshCount" in item:
            _nonnegative_int(item.get("hiddenMeshCount"), f"{path} hiddenMeshCount")
        _list(item.get("warnings"), f"scanned W3D {path} warnings")
        model_references = _list(
            item.get("modelReferences"), f"scanned W3D {path} modelReferences"
        )
        for reference in model_references:
            record = _mapping(reference, f"scanned W3D {path} model reference")
            _text(record.get("identifier"), "W3D model-reference identifier")
            _text(record.get("role"), "W3D model-reference role")
        _source_record(
            path,
            sources,
            expected_sha256=digest,
            expected_size=byte_length,
        )
        scanned_paths.append(path)
        scanned[path] = item
    _case_unique(scanned_paths, "scanned W3D path")

    read_boundary = _mapping(dependency.get("readBoundary"), "W3D readBoundary")
    if read_boundary.get("policy") != _W3D_READ_POLICY:
        raise ValueError("unsupported W3D read-boundary policy")
    boundary_paths = [
        register_path(path, "W3D read-boundary path")
        for path in _list(
            read_boundary.get("uniqueVirtualPaths"),
            "W3D readBoundary uniqueVirtualPaths",
        )
    ]
    _case_unique(boundary_paths, "W3D read-boundary path")
    if boundary_paths != scanned_paths:
        raise ValueError("W3D read boundary does not match scannedW3d")
    if read_boundary.get("uniqueReadCount") != len(scanned_rows):
        raise ValueError("W3D read-boundary count mismatch")
    if read_boundary.get("byteLength") != sum(
        int(item["byteLength"]) for item in scanned_rows
    ):
        raise ValueError("W3D read-boundary byte length mismatch")

    embedded_rows = _list(
        dependency.get("embeddedTextures"), "embedded texture dependencies"
    )
    embedded_by_w3d: dict[str, list[Mapping[str, Any]]] = {
        path: [] for path in scanned_paths
    }
    embedded_keys: set[tuple[str, int, str]] = set()
    for position, value in enumerate(embedded_rows):
        item = _mapping(value, f"embedded texture {position}")
        source_path = register_path(
            item.get("sourceW3dVirtualPath"), "embedded texture source W3D"
        )
        if source_path not in scanned:
            raise ValueError(
                f"embedded texture references an unscanned W3D: {source_path}"
            )
        identifier = _text(item.get("identifier"), "embedded texture identifier")
        provenance = _mapping(item.get("provenance"), "embedded texture provenance")
        value_offset = _nonnegative_int(
            provenance.get("valueOffset"), "embedded texture valueOffset"
        )
        unique_key = (source_path.casefold(), value_offset, identifier.casefold())
        if unique_key in embedded_keys:
            raise ValueError("duplicate embedded texture dependency record")
        embedded_keys.add(unique_key)
        status = item.get("status")
        if status not in {"resolved", "missing", "ambiguous", "invalid"}:
            raise ValueError("embedded texture has invalid status")
        if status == "resolved":
            paths = [
                register_path(path, "embedded texture physical path")
                for path in _list(
                    item.get("physicalVirtualPaths"),
                    "embedded texture physicalVirtualPaths",
                )
            ]
            if not paths:
                raise ValueError("resolved embedded texture has no physical path")
            _case_unique(paths, "embedded texture physical path")
            for path in paths:
                _source_record(path, sources)
        else:
            candidates = item.get("candidates", [])
            if candidates is not None:
                candidate_paths = [
                    register_path(path, "embedded texture candidate")
                    for path in _list(candidates, "embedded texture candidates")
                ]
                _case_unique(candidate_paths, "embedded texture candidate")
        embedded_by_w3d[source_path].append(item)

    graph_diagnostics = _list(
        unresolved_root.get("graphDiagnostics"), "unresolved.graphDiagnostics"
    )
    diagnostics_by_target: dict[str, list[Mapping[str, Any]]] = {
        name: [] for name in targets
    }
    for value in graph_diagnostics:
        item = _mapping(value, "graph diagnostic")
        name = _safe_object_id(item.get("objectName"), "diagnostic objectName")
        if name not in target_records:
            raise ValueError(f"graph diagnostic references unknown target: {name!r}")
        diagnostics_by_target[name].append(item)

    dep_summary = _mapping(dependency.get("summary"), "W3D dependency summary")
    embedded_counts = Counter(str(item.get("status")) for item in embedded_rows)
    expected_dependency_summary = {
        "fileCount": len(scanned_rows),
        "embeddedTextureReferenceCount": len(embedded_rows),
        "resolvedEmbeddedTextureCount": embedded_counts.get("resolved", 0),
        "missingEmbeddedTextureCount": embedded_counts.get("missing", 0),
        "ambiguousEmbeddedTextureCount": embedded_counts.get("ambiguous", 0),
        "invalidEmbeddedTextureCount": embedded_counts.get("invalid", 0),
    }
    if any(
        dep_summary.get(key) != value
        for key, value in expected_dependency_summary.items()
    ):
        raise ValueError("W3D dependency summary mismatch")

    summary = _mapping(raw.get("summary"), "retail visual closure summary")
    summary_checks = {
        "targetCount": len(targets),
        "resolvedTargetCount": len(objects),
        "exactLeafCount": len(exact_rows),
        "semanticLeafCount": len(semantic_rows),
        "unresolvedReferenceCount": len(unresolved_rows),
        "graphDiagnosticCount": len(graph_diagnostics),
        "scannedW3dCount": len(scanned_rows),
        "scannedW3dByteLength": sum(int(item["byteLength"]) for item in scanned_rows),
        "embeddedTextureReferenceCount": len(embedded_rows),
        "resolvedEmbeddedTextureCount": embedded_counts.get("resolved", 0),
        "unresolvedEmbeddedTextureCount": sum(
            count for status, count in embedded_counts.items() if status != "resolved"
        ),
    }
    if any(summary.get(key) != value for key, value in summary_checks.items()):
        raise ValueError("retail visual closure summary mismatch")
    expected_ready = bool(
        not missing_definitions
        and not graph_diagnostics
        and not unresolved_rows
        and summary_checks["unresolvedEmbeddedTextureCount"] == 0
        and len(objects) == len(targets)
    )
    if (
        not isinstance(summary.get("ready"), bool)
        or summary.get("ready") != expected_ready
    ):
        raise ValueError("retail visual closure ready status mismatch")

    return {
        "reportDigest": report_digest,
        "dependencyDigest": dependency_digest,
        "targets": targets,
        "targetRecords": target_records,
        "objects": objects,
        "leavesByTarget": leaves_by_target,
        "scanned": scanned,
        "embeddedByW3d": embedded_by_w3d,
        "diagnosticsByTarget": diagnostics_by_target,
    }


def _stable_slug(prefix: str, virtual_path: str) -> str:
    stem = re.sub(
        r"[^a-z0-9]+", "-", PurePosixPath(virtual_path).stem.casefold()
    ).strip("-")
    if not stem:
        stem = "asset"
    suffix = hashlib.sha256(virtual_path.casefold().encode("utf-8")).hexdigest()[:12]
    available = 63 - len(prefix) - len(suffix) - 2
    return f"{prefix}-{stem[:available].rstrip('-')}-{suffix}"


def _reason(code: str, **evidence: Any) -> dict[str, Any]:
    return {"code": code, **evidence}


def _unique_reasons(reasons: list[dict[str, Any]]) -> list[dict[str, Any]]:
    selected: dict[str, dict[str, Any]] = {}
    for reason in reasons:
        encoded = json.dumps(reason, sort_keys=True, separators=(",", ":"))
        selected.setdefault(encoded, reason)
    return sorted(
        selected.values(),
        key=lambda item: (str(item["code"]), _canonical_sha256(item)),
    )


def build_retail_static_prop_plan(
    visual_closure_report: Mapping[str, Any],
    effective_assets_manifest: Mapping[str, Any],
) -> dict[str, Any]:
    """Return a deterministic, payload-free static-prop conversion plan.

    The report may be globally non-ready: each target is evaluated
    independently so unrelated animal animation gaps cannot block proven
    rocks, trees, and grass.  Source hashes always come from the validated
    effective-assets manifest and every scanned W3D hash must agree with it.
    """

    report = _mapping(visual_closure_report, "retail visual closure")
    manifest = _mapping(effective_assets_manifest, "effective-assets manifest")
    sources, manifest_evidence = _validate_effective_manifest(manifest)
    closure = _validate_visual_closure(report, sources)

    eligible: list[dict[str, Any]] = []
    ineligible: list[dict[str, Any]] = []
    per_target_textures: dict[str, tuple[str, ...]] = {}

    for target in sorted(
        closure["targets"], key=lambda value: (value.casefold(), value)
    ):
        target_record = closure["targetRecords"][target]
        reasons: list[dict[str, Any]] = []
        model_paths: set[str] = set()
        texture_paths: set[str] = set()

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
                        statuses=sorted({str(item.get("status")) for item in matching}),
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
                and kind != "model"
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
                # Dual-bone skin streams no longer surface as scanner warnings
                # (the scanner decodes them); this lane still has no secondary-
                # skin normalization step, so their presence stays a rejection.
                secondary_streams = int(
                    scanned.get("secondaryGeometryStreamCount", 0) or 0
                )
                if secondary_streams:
                    reasons.append(
                        _reason(
                            "model-w3d-secondary-geometry-streams",
                            streamCount=secondary_streams,
                        )
                    )
                header = scanned["headerIds"]
                if not header.get("modelIds"):
                    reasons.append(_reason("model-w3d-has-no-model-header"))
                if header.get("animationIds"):
                    reasons.append(
                        _reason(
                            "model-w3d-contains-animation-headers",
                            headerCount=len(header["animationIds"]),
                        )
                    )
                if header.get("hierarchyIds"):
                    reasons.append(
                        _reason(
                            "model-w3d-contains-hierarchy-headers",
                            headerCount=len(header["hierarchyIds"]),
                        )
                    )
                unsupported_roles = sorted(
                    {
                        str(item.get("role"))
                        for item in scanned.get("modelReferences", [])
                        if item.get("role") not in _SUPPORTED_W3D_MODEL_REFERENCE_ROLES
                    }
                )
                if unsupported_roles:
                    reasons.append(
                        _reason(
                            "model-w3d-unsupported-reference-role",
                            roles=unsupported_roles,
                        )
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
            ineligible.append({"targetObject": target, "reasons": reasons})
            continue
        assert model_path is not None
        per_target_textures[target] = tuple(
            sorted(texture_paths, key=lambda value: (value.casefold(), value))
        )
        eligible.append(
            {
                "targetObject": target,
                "modelVirtualPath": model_path,
                "textureVirtualPaths": list(per_target_textures[target]),
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
        resource_id = _stable_slug("static-prop-texture", path)
        output_stem = resource_id.removeprefix("static-prop-texture-")
        texture_resources[path] = {
            "id": resource_id,
            "kind": "texture",
            "patterns": [path],
            "required": True,
            "converter": "texture",
            "output": f"assets/textures/props/{output_stem}.png",
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
        targets = sorted(
            grouped_targets[model_path], key=lambda value: (value.casefold(), value)
        )
        group_texture_paths = sorted(
            {path for target in targets for path in per_target_textures[target]},
            key=lambda value: (value.casefold(), value),
        )
        resource_id = _stable_slug("static-prop-model", model_path)
        output_stem = resource_id.removeprefix("static-prop-model-")
        output = f"assets/models/props/{output_stem}.glb"
        input_ids = [texture_resources[path]["id"] for path in group_texture_paths]
        model_resource = {
            "id": resource_id,
            "kind": "model",
            "patterns": [model_path],
            "required": True,
            "converter": "w3d-static",
            "output": output,
            "limit": 1,
            "expected_count": 1,
            "options": {
                "model": PurePosixPath(model_path).name,
                "inputResourceIds": input_ids,
            },
        }
        model_resources.append(model_resource)
        scanned = closure["scanned"][model_path]
        model_source = _source_record(
            model_path,
            sources,
            expected_sha256=str(scanned["sha256"]),
            expected_size=int(scanned["byteLength"]),
        )
        conversion_groups.append(
            {
                "modelResourceId": resource_id,
                "targetObjects": targets,
                "outputGlb": output,
                "modelSource": model_source,
                "textureSources": [
                    texture_records[path] for path in group_texture_paths
                ],
            }
        )
        for target in targets:
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
            "static-prop profile fragment exceeds the 256-resource profile limit"
        )
    resource_ids = [str(item["id"]) for item in resources]
    _case_unique(resource_ids, "generated profile resource id")
    output_paths = [str(item["output"]) for item in resources]
    _case_unique(output_paths, "generated profile output path")

    reason_counts = Counter(
        str(reason["code"]) for target in ineligible for reason in target["reasons"]
    )
    plan: dict[str, Any] = {
        "schema": STATIC_PROP_PLAN_SCHEMA,
        "schemaVersion": STATIC_PROP_PLAN_SCHEMA_VERSION,
        "sourceEvidence": {
            "visualClosureAggregateSha256": closure["reportDigest"],
            "w3dDependencyAggregateSha256": closure["dependencyDigest"],
            "effectiveAssets": manifest_evidence,
        },
        "policy": {
            "selection": "exact-static-target-types-only",
            "placementDataConsumed": False,
            "substitutesAllowed": False,
            "grouping": "one-conversion-per-exact-physical-w3d",
        },
        "eligibleTargets": eligible,
        "ineligibleTargets": ineligible,
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
        "summary": {
            "targetTypeCount": len(closure["targets"]),
            "eligibleTargetTypeCount": len(eligible),
            "ineligibleTargetTypeCount": len(ineligible),
            "conversionGroupCount": len(conversion_groups),
            "uniqueModelSourceCount": len(conversion_groups),
            "uniqueTextureSourceCount": len(all_texture_paths),
            "profileResourceCount": len(resources),
            "objectBindingModelRowCount": len(binding_rows),
            "placementIndependent": True,
            "ineligibleReasonCounts": [
                {"code": code, "targetCount": reason_counts[code]}
                for code in sorted(reason_counts)
            ],
        },
    }
    plan["aggregateSha256"] = _canonical_sha256(plan)
    return plan


def load_retail_static_prop_plan_inputs(
    visual_closure_path: Path | str,
    effective_assets_manifest_path: Path | str,
) -> tuple[dict[str, Any], dict[str, Any]]:
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
        load(
            effective_assets_manifest_path,
            "effective-assets manifest",
            64 * 1024 * 1024,
        ),
    )


def write_retail_static_prop_plan(path: Path | str, plan: Mapping[str, Any]) -> None:
    """Atomically write a previously built plan (private paths are expected)."""

    document = _mapping(plan, "retail static-prop plan")
    if document.get("schema") != STATIC_PROP_PLAN_SCHEMA:
        raise ValueError("cannot write an unsupported static-prop plan schema")
    _validate_declared_digest(document, "aggregateSha256", "static-prop plan")
    write_json_atomic(Path(path), deepcopy(dict(document)))


__all__ = [
    "STATIC_PROP_PLAN_SCHEMA",
    "STATIC_PROP_PLAN_SCHEMA_VERSION",
    "build_retail_static_prop_plan",
    "load_retail_static_prop_plan_inputs",
    "write_retail_static_prop_plan",
]
