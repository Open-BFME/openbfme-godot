"""Deterministic private feature graph projected from the exhaustive INI scan."""

from __future__ import annotations

import hashlib
import json
from collections import Counter
from typing import Any, Mapping


SCHEMA = "openbfme.rotwk-202-feature-graph"
SCHEMA_VERSION = 1
EXPECTED_COUNTS = {
    "documents": 850,
    "definitions": 36_940,
    "assignments": 597_886,
    "objects": 5_494,
    "assetReferences": 188_543,
    "objectModuleSites": 24_230,
    "scriptCallSites": 4_384,
}


def _site_id(prefix: str, row: Mapping[str, Any]) -> str:
    identity = "|".join(
        str(row.get(key, ""))
        for key in ("sourceIni", "line", "rootKind", "rootName", "field", "command", "block")
    )
    return f"{prefix}-{hashlib.sha256(identity.encode('utf-8')).hexdigest()[:24]}"


def _ordered(rows: list[dict[str, Any]], *keys: str) -> list[dict[str, Any]]:
    return sorted(rows, key=lambda row: tuple(str(row.get(key, "")).casefold() for key in keys))


def build_feature_graph(
    scan: Any,
    *,
    baseline_id: str,
    policy_sha256: str,
    catalog_sha256: str,
    effective_tree_sha256: str,
    source_identity: Mapping[str, Any],
    root_queries: list[dict[str, Any]],
    assignment_classifications: Mapping[tuple[str, int, str], str],
    expected_counts: Mapping[str, int] = EXPECTED_COUNTS,
) -> dict[str, Any]:
    assignments: list[dict[str, Any]] = []
    for raw in scan.assignment_sites:
        row = dict(raw)
        key = (str(row["sourceIni"]).casefold(), int(row["line"]), str(row["field"]).casefold())
        classification = assignment_classifications.get(key)
        if classification not in {"scalar", "reference", "collection-reference", "opaque-unresolved"}:
            raise ValueError("assignment classification is missing or invalid")
        row["classification"] = classification
        row["assignmentId"] = _site_id("A", row)
        assignments.append(row)

    definitions = [dict(row, definitionId=_site_id("D", row)) for row in scan.definitions]
    scripts = [dict(row, scriptCallId=_site_id("S", row)) for row in scan.script_call_sites]
    nested = [dict(row, nestedSiteId=_site_id("N", row)) for row in scan.nested_sites]
    directives = [dict(row, directiveSiteId=_site_id("P", row)) for row in scan.directive_sites]
    unknown = [dict(row, unknownSiteId=_site_id("U", row)) for row in scan.unknown_lines]
    objects = _ordered(list(scan.object_rows), "objectId", "sourceIni")
    unique_objects = {str(row["objectId"]).casefold() for row in objects}
    object_modules: Counter[str] = Counter()
    for row in objects:
        object_modules.update({str(key).casefold(): int(value) for key, value in row.get("modules", {}).items()})
    counts = {
        "documents": len(scan.documents),
        "definitions": len(definitions),
        "assignments": len(assignments),
        "objects": len(unique_objects),
        "assetReferences": len(scan.asset_references),
        "objectModuleSites": sum(object_modules.values()),
        "scriptCallSites": len(scripts),
        "nestedSites": len(nested),
        "directiveSites": len(directives),
        "unknownSites": len(unknown),
        "opaqueUnresolvedAssignments": sum(row["classification"] == "opaque-unresolved" for row in assignments),
    }
    for key, value in expected_counts.items():
        if counts.get(key) != value:
            raise ValueError(f"feature-graph count differs for {key}: {counts.get(key)} != {value}")
    graph = {
        "schema": SCHEMA,
        "schemaVersion": SCHEMA_VERSION,
        "baselineId": baseline_id,
        "policySha256": policy_sha256,
        "catalogSha256": catalog_sha256,
        "effectiveTreeSha256": effective_tree_sha256,
        "sourceSchema": dict(source_identity),
        "rootQueries": root_queries,
        "counts": counts,
        "documents": _ordered(list(scan.documents), "path", "archive"),
        "definitions": _ordered(definitions, "sourceIni", "line", "kind", "name"),
        "assignments": _ordered(assignments, "sourceIni", "line", "field"),
        "objects": objects,
        "assetReferences": _ordered(list(scan.asset_references), "sourceIni", "line", "field", "token"),
        "objectModuleKinds": dict(sorted(object_modules.items())),
        "scriptCalls": _ordered(scripts, "sourceIni", "line", "command"),
        "nestedSites": _ordered(nested, "sourceIni", "line", "block"),
        "directiveSites": _ordered(directives, "sourceIni", "line", "directive"),
        "unknownSites": _ordered(unknown, "sourceIni", "line"),
    }
    validate_feature_graph(graph, expected_counts=expected_counts)
    return graph


def validate_feature_graph(graph: Mapping[str, Any], *, expected_counts: Mapping[str, int]) -> None:
    if graph.get("schema") != SCHEMA or graph.get("schemaVersion") != SCHEMA_VERSION:
        raise ValueError("feature-graph schema is invalid")
    counts = graph.get("counts")
    if not isinstance(counts, dict):
        raise ValueError("feature-graph counts are missing")
    for key, expected in expected_counts.items():
        if counts.get(key) != expected:
            raise ValueError(f"feature-graph receipt differs for {key}")
    assignments = graph.get("assignments")
    if not isinstance(assignments, list) or len(assignments) != counts["assignments"]:
        raise ValueError("feature-graph assignment rows do not close")
    ids = [row.get("assignmentId") for row in assignments]
    if len(set(ids)) != len(ids) or any(not isinstance(value, str) for value in ids):
        raise ValueError("feature-graph assignment identities are not unique")
    if any(row.get("classification") not in {"scalar", "reference", "collection-reference", "opaque-unresolved"} for row in assignments):
        raise ValueError("feature-graph assignment classification is invalid")


def render_feature_graph(graph: Mapping[str, Any]) -> bytes:
    return (json.dumps(graph, ensure_ascii=False, sort_keys=True, separators=(",", ":")) + "\n").encode("utf-8")
