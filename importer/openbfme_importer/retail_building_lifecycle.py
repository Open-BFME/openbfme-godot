"""Exact Men building lifecycle evidence for the private Fords slice.

This module is a reporting boundary, not a converter.  It composes the existing
source-proven visual closure with a bounded read of the selected SAGE Object
definitions.  The result keeps construction, intact, damage, rubble, floor,
upgrade, door, and fortress-unpack predicates explicit so a runtime cannot
silently bind one convenient W3D to every lifecycle state.

No retail payload bytes are emitted.  Source and bound-asset evidence consists
only of virtual paths, sizes, SHA-256 values, identifiers, and source locations.
Opaque ``BeginScript`` bodies are represented by a digest and statement count.
"""

from __future__ import annotations

import argparse
from collections import Counter
from collections.abc import Iterable, Mapping
import hashlib
import json
from pathlib import Path, PurePosixPath
import re

from .retail_visual_closure import build_retail_visual_closure
from .sage_map import SageMapError, parse_sage_base_template_bytes
from .sage_cst import (
    ResolvedSageCst,
    SageAssignment,
    SageBlock,
    SageIncludeRef,
    SageObject,
    SageScript,
    resolve_sage_documents,
)
from .util import write_json_atomic


SCHEMA = "openbfme.retail-men-building-lifecycle"
SCHEMA_VERSION = 0

MAX_SOURCE_COUNT = 64
MAX_SOURCE_BYTES = 16 * 1024 * 1024
MAX_BOUND_ASSET_COUNT = 4_096
MAX_BOUND_ASSET_BYTES = 512 * 1024 * 1024
MAX_BOUND_ASSET_TOTAL_BYTES = 2 * 1024 * 1024 * 1024
MAX_EFFECTIVE_MODULES = 2_048
MAX_MODULE_ASSIGNMENTS = 32_768
MAX_STATE_BLOCKS = 4_096
MAX_STATE_ASSIGNMENTS = 65_536
MAX_OBJECT_ANCESTRY = 32

_SYNTHETIC_ENTRY = "openbfme-men-building-lifecycle-entry.ini"
_PHASE_ORDER = (
    "intact",
    "construction",
    "damaged",
    "really-damaged",
    "rubble",
    "post-rubble",
)
_CONSTRUCTION_CONDITIONS = frozenset(
    {
        "AWAITING_CONSTRUCTION",
        "ACTIVELY_BEING_CONSTRUCTED",
        "PARTIALLY_CONSTRUCTED",
        "CONSTRUCTION_COMPLETE",
        "BUILD_PLACEMENT_CURSOR",
        "PHANTOM_STRUCTURE",
    }
)
_POST_RUBBLE_CONDITIONS = frozenset({"POST_RUBBLE", "POST_COLLAPSE"})
_STATE_KINDS = frozenset(
    {
        "defaultmodelconditionstate",
        "modelconditionstate",
        "conditionstate",
        "animationstate",
        "idleanimationstate",
        "transitionstate",
    }
)
_MODULE_CARRIERS = frozenset({"behavior", "body", "clientbehavior", "clientupdate"})
_PLACEMENT_KEYS = frozenset(
    {
        "buildcompletion",
        "buildcost",
        "buildtime",
        "editorsorting",
        "kindof",
        "placementviewangle",
        "radarpriority",
        "scale",
        "shadow",
        "shroudclearingrange",
        "side",
        "visionrange",
    }
)
_PROJECT_STRUCTURES: tuple[dict[str, object], ...] = (
    {
        "runtimeKind": "fortress",
        "projectRuntimeId": "bfme2.object.men-fortress",
        "sourceObjects": (
            {"name": "MenFortress", "role": "placement-and-unpack-controller"},
            {"name": "MenFortressCitadel", "role": "operational-citadel"},
        ),
    },
    {
        "runtimeKind": "farm",
        "projectRuntimeId": "bfme2.object.men-farm",
        "sourceObjects": ({"name": "GondorFarm", "role": "direct-object"},),
    },
    {
        "runtimeKind": "barracks",
        "projectRuntimeId": "bfme2.object.men-barracks",
        "sourceObjects": ({"name": "GondorBarracks", "role": "direct-object"},),
    },
    {
        "runtimeKind": "archery-range",
        "projectRuntimeId": "bfme2.object.men-archery-range",
        "sourceObjects": ({"name": "GondorArcherRange", "role": "direct-object"},),
    },
    {
        "runtimeKind": "stable",
        "projectRuntimeId": "bfme2.object.men-stable",
        "sourceObjects": ({"name": "GondorStable", "role": "direct-object"},),
    },
)
TARGET_OBJECTS = tuple(
    sorted(
        {
            str(source["name"])
            for structure in _PROJECT_STRUCTURES
            for source in structure["sourceObjects"]  # type: ignore[index]
        },
        key=lambda value: (value.casefold(), value),
    )
)


class RetailBuildingLifecycleError(ValueError):
    """The selected retail lifecycle evidence is unsafe or inconsistent."""


def _sort_text(value: str) -> tuple[str, str]:
    return value.casefold(), value


def _canonical_sha256(value: object) -> str:
    payload = json.dumps(
        value,
        ensure_ascii=False,
        allow_nan=False,
        sort_keys=True,
        separators=(",", ":"),
    ).encode("utf-8")
    return hashlib.sha256(payload).hexdigest()


def _verify_digest(value: Mapping[str, object], *, label: str) -> None:
    supplied = value.get("aggregateSha256")
    if not isinstance(supplied, str) or len(supplied) != 64:
        raise RetailBuildingLifecycleError(f"{label} has no valid aggregate SHA-256")
    unsigned = dict(value)
    del unsigned["aggregateSha256"]
    actual = _canonical_sha256(unsigned)
    if actual != supplied:
        raise RetailBuildingLifecycleError(
            f"{label} aggregate SHA-256 mismatch: expected {supplied}, got {actual}"
        )


def _safe_virtual_path(value: object, *, label: str) -> str:
    if not isinstance(value, str) or not value or len(value) > 1_024:
        raise RetailBuildingLifecycleError(f"unsafe {label}: {value!r}")
    if "\0" in value or "\\" in value:
        raise RetailBuildingLifecycleError(f"unsafe {label}: {value!r}")
    path = PurePosixPath(value)
    if (
        path.is_absolute()
        or not path.parts
        or any(part in {"", ".", ".."} or ":" in part for part in path.parts)
    ):
        raise RetailBuildingLifecycleError(f"unsafe {label}: {value!r}")
    return path.as_posix()


def _contained_path(root: Path, virtual_path: str) -> Path:
    path = root.joinpath(*PurePosixPath(virtual_path).parts)
    try:
        path.resolve().relative_to(root.resolve())
    except ValueError as exc:
        raise RetailBuildingLifecycleError(
            f"source escapes effective root: {virtual_path}"
        ) from exc
    return path


def _read_bounded(
    root: Path,
    virtual_path: str,
    *,
    maximum: int,
    label: str,
) -> bytes:
    path = _contained_path(root, virtual_path)
    if path.is_symlink() or not path.is_file():
        raise RetailBuildingLifecycleError(
            f"{label} is not a regular contained file: {virtual_path}"
        )
    before = path.stat()
    if before.st_size > maximum:
        raise RetailBuildingLifecycleError(
            f"{label} exceeds {maximum} byte limit: {virtual_path}"
        )
    source = path.read_bytes()
    after = path.stat()
    if (
        len(source) != before.st_size
        or before.st_size != after.st_size
        or before.st_mtime_ns != after.st_mtime_ns
    ):
        raise RetailBuildingLifecycleError(
            f"{label} changed during read: {virtual_path}"
        )
    return source


def _hash_file(root: Path, virtual_path: str) -> dict[str, object]:
    path = _contained_path(root, virtual_path)
    if path.is_symlink() or not path.is_file():
        raise RetailBuildingLifecycleError(
            f"bound asset is not a regular contained file: {virtual_path}"
        )
    before = path.stat()
    if before.st_size > MAX_BOUND_ASSET_BYTES:
        raise RetailBuildingLifecycleError(
            f"bound asset exceeds {MAX_BOUND_ASSET_BYTES} byte limit: {virtual_path}"
        )
    digest = hashlib.sha256()
    byte_count = 0
    with path.open("rb") as stream:
        while True:
            chunk = stream.read(1024 * 1024)
            if not chunk:
                break
            digest.update(chunk)
            byte_count += len(chunk)
    after = path.stat()
    if (
        byte_count != before.st_size
        or before.st_size != after.st_size
        or before.st_mtime_ns != after.st_mtime_ns
    ):
        raise RetailBuildingLifecycleError(
            f"bound asset changed during read: {virtual_path}"
        )
    return {
        "virtualPath": virtual_path,
        "byteCount": byte_count,
        "sha256": digest.hexdigest(),
    }


def _validated_visual_closure(value: object) -> dict[str, object]:
    if not isinstance(value, dict):
        raise RetailBuildingLifecycleError("visual closure must be a JSON object")
    if value.get("schema") != "openbfme.retail-visual-closure":
        raise RetailBuildingLifecycleError("unexpected visual closure schema")
    _verify_digest(value, label="visual closure")
    targets = value.get("targets")
    if not isinstance(targets, list):
        raise RetailBuildingLifecycleError("visual closure targets must be a list")
    resolved: list[str] = []
    for row in targets:
        if not isinstance(row, dict) or row.get("status") != "resolved":
            raise RetailBuildingLifecycleError("every lifecycle target must resolve")
        name = row.get("name")
        if not isinstance(name, str):
            raise RetailBuildingLifecycleError("resolved lifecycle target has no name")
        resolved.append(name)
    if sorted(resolved, key=_sort_text) != list(TARGET_OBJECTS):
        raise RetailBuildingLifecycleError(
            "visual closure target set does not match the five-building scope"
        )
    summary = value.get("summary")
    if not isinstance(summary, dict):
        raise RetailBuildingLifecycleError("visual closure summary must be an object")
    if summary.get("resolvedTargetCount") != len(TARGET_OBJECTS):
        raise RetailBuildingLifecycleError(
            "visual closure did not resolve every target"
        )
    return value


def _source_documents(
    root: Path, closure: Mapping[str, object]
) -> tuple[ResolvedSageCst, list[dict[str, object]]]:
    source_closure = closure.get("sourceClosure")
    if not isinstance(source_closure, dict):
        raise RetailBuildingLifecycleError("visual closure sourceClosure is missing")
    rows = source_closure.get("paths")
    if not isinstance(rows, list) or not 1 <= len(rows) <= MAX_SOURCE_COUNT:
        raise RetailBuildingLifecycleError(
            f"source closure count must be 1..{MAX_SOURCE_COUNT}"
        )
    documents: dict[str, bytes] = {}
    evidence: list[dict[str, object]] = []
    for row in rows:
        if not isinstance(row, dict):
            raise RetailBuildingLifecycleError("source closure row must be an object")
        virtual_path = _safe_virtual_path(
            row.get("virtualPath"), label="source virtual path"
        )
        source = _read_bounded(
            root,
            virtual_path,
            maximum=MAX_SOURCE_BYTES,
            label="SAGE source",
        )
        expected_size = row.get("byteLength")
        expected_sha = row.get("sha256")
        actual_sha = hashlib.sha256(source).hexdigest()
        if expected_size != len(source) or expected_sha != actual_sha:
            raise RetailBuildingLifecycleError(
                f"SAGE source evidence drift: {virtual_path}"
            )
        documents[virtual_path] = source
        roles = row.get("roles")
        if not isinstance(roles, list) or not all(
            isinstance(item, str) for item in roles
        ):
            raise RetailBuildingLifecycleError("source closure roles must be text")
        evidence.append(
            {
                "virtualPath": virtual_path,
                "roles": sorted(roles),
                "byteCount": len(source),
                "sha256": actual_sha,
            }
        )

    definition_rows = closure.get("definitionClosure")
    if not isinstance(definition_rows, list):
        raise RetailBuildingLifecycleError("definitionClosure must be a list")
    definition_paths: set[str] = set()
    for row in definition_rows:
        if not isinstance(row, dict):
            raise RetailBuildingLifecycleError(
                "definition closure row must be an object"
            )
        definition_paths.add(
            _safe_virtual_path(row.get("virtualPath"), label="definition virtual path")
        )
    if not definition_paths:
        raise RetailBuildingLifecycleError("definition closure is empty")
    missing = sorted(definition_paths.difference(documents), key=_sort_text)
    if missing:
        raise RetailBuildingLifecycleError(
            "definition sources are absent from source closure: " + ", ".join(missing)
        )
    if _SYNTHETIC_ENTRY.casefold() in {item.casefold() for item in documents}:
        raise RetailBuildingLifecycleError("reserved synthetic entry path collision")
    entry = "".join(
        f'#include "{path}"\n' for path in sorted(definition_paths, key=_sort_text)
    ).encode("cp1252")
    documents[_SYNTHETIC_ENTRY] = entry
    cst = resolve_sage_documents(_SYNTHETIC_ENTRY, documents)
    evidence.sort(key=lambda row: _sort_text(str(row["virtualPath"])))
    return cst, evidence


def _object_index(cst: ResolvedSageCst) -> dict[str, tuple[SageObject, ...]]:
    grouped: dict[str, list[SageObject]] = {}
    for item in cst.objects:
        grouped.setdefault(item.name.casefold(), []).append(item)
    return {
        key: tuple(
            sorted(
                values,
                key=lambda item: (
                    _sort_text(item.source_virtual_path),
                    item.line,
                    _sort_text(item.name),
                ),
            )
        )
        for key, values in grouped.items()
    }


def _single_object(
    index: Mapping[str, tuple[SageObject, ...]], name: str
) -> SageObject:
    candidates = index.get(name.casefold(), ())
    if len(candidates) != 1:
        raise RetailBuildingLifecycleError(
            f"expected one Object {name!r}, found {len(candidates)}"
        )
    return candidates[0]


def _ancestry(
    index: Mapping[str, tuple[SageObject, ...]], target: SageObject
) -> tuple[SageObject, ...]:
    child_to_root: list[SageObject] = []
    seen: set[str] = set()
    current = target
    while True:
        if len(child_to_root) >= MAX_OBJECT_ANCESTRY:
            raise RetailBuildingLifecycleError(
                f"Object ancestry exceeds {MAX_OBJECT_ANCESTRY}: {target.name}"
            )
        key = current.name.casefold()
        if key in seen:
            raise RetailBuildingLifecycleError(f"Object ancestry cycle: {target.name}")
        seen.add(key)
        child_to_root.append(current)
        if current.parent is None:
            break
        current = _single_object(index, current.parent)
    return tuple(reversed(child_to_root))


def _scope_name(block: SageBlock) -> str:
    name = block.kind
    if block.instance_tag is not None:
        name += " " + block.instance_tag
    elif block.header_tokens:
        name += " " + " ".join(block.header_tokens)
    return name


def _provenance(
    owner: SageObject,
    item: SageAssignment | SageBlock | SageScript,
    *,
    inheritance_distance: int,
    scope_path: Iterable[str],
) -> dict[str, object]:
    return {
        "definingObject": owner.name,
        "virtualPath": item.source_virtual_path,
        "line": item.line,
        "inheritanceDistance": inheritance_distance,
        "scopePath": list(scope_path),
    }


def _lifecycle_phases(conditions: Iterable[str]) -> list[str]:
    folded = {item.upper() for item in conditions}
    phases: set[str] = set()
    if folded & _CONSTRUCTION_CONDITIONS:
        phases.add("construction")
    if "DAMAGED" in folded:
        phases.add("damaged")
    if "REALLYDAMAGED" in folded:
        phases.add("really-damaged")
    if "RUBBLE" in folded:
        phases.add("rubble")
    if folded & _POST_RUBBLE_CONDITIONS:
        phases.add("post-rubble")
    if not phases:
        phases.add("intact")
    return [phase for phase in _PHASE_ORDER if phase in phases]


def _flatten_block_assignments(
    block: SageBlock,
    *,
    owner: SageObject,
    inheritance_distance: int,
    base_scope: tuple[str, ...],
    counter: list[int],
) -> tuple[list[dict[str, object]], list[dict[str, object]]]:
    assignments: list[dict[str, object]] = []
    scripts: list[dict[str, object]] = []

    def walk(items: tuple[object, ...], scope: tuple[str, ...]) -> None:
        for item in items:
            if isinstance(item, SageAssignment):
                counter[0] += 1
                if counter[0] > MAX_STATE_ASSIGNMENTS:
                    raise RetailBuildingLifecycleError(
                        f"state assignment count exceeds {MAX_STATE_ASSIGNMENTS}"
                    )
                assignments.append(
                    {
                        "key": item.key,
                        "rawValue": item.value,
                        "provenance": _provenance(
                            owner,
                            item,
                            inheritance_distance=inheritance_distance,
                            scope_path=scope,
                        ),
                    }
                )
            elif isinstance(item, SageScript):
                normalized = "\n".join(line.text for line in item.lines).encode(
                    "cp1252"
                )
                scripts.append(
                    {
                        "statementCount": len(item.lines),
                        "statementSha256": hashlib.sha256(normalized).hexdigest(),
                        "provenance": _provenance(
                            owner,
                            item,
                            inheritance_distance=inheritance_distance,
                            scope_path=scope,
                        ),
                    }
                )
            elif isinstance(item, SageBlock):
                walk(item.items, (*scope, _scope_name(item)))
            elif isinstance(item, SageIncludeRef):
                if item.resolved_virtual_path is None:
                    raise RetailBuildingLifecycleError(
                        f"unresolved include at {item.source_virtual_path}:{item.line}"
                    )
            else:
                raise TypeError(f"unexpected SAGE body item: {type(item).__name__}")

    walk(block.items, base_scope)
    return assignments, scripts


def _effective_tagged_blocks(
    ancestry: tuple[SageObject, ...],
    *,
    carrier_keys: frozenset[str] | None,
) -> list[tuple[SageBlock, SageObject, int]]:
    result: list[tuple[SageBlock, SageObject, int]] = []
    tag_indexes: dict[str, int] = {}
    total = len(ancestry)
    for object_index, owner in enumerate(ancestry):
        distance = total - object_index - 1
        for assignment in owner.assignments:
            if assignment.key.casefold() != "removemodule":
                continue
            tag = assignment.value.strip().casefold()
            index = tag_indexes.get(tag)
            if index is not None:
                result.pop(index)
                tag_indexes = {
                    block.instance_tag.casefold(): position
                    for position, (block, _, _) in enumerate(result)
                    if block.instance_tag is not None
                }
        local_tags: set[str] = set()
        for item in owner.blocks:
            header = item.header_key.casefold() if item.header_key else None
            if carrier_keys is None:
                if header != "draw":
                    continue
            elif header not in carrier_keys:
                continue
            if item.instance_tag is None:
                result.append((item, owner, distance))
                continue
            tag = item.instance_tag.casefold()
            if tag in local_tags:
                raise RetailBuildingLifecycleError(
                    f"duplicate module tag {item.instance_tag!r} in {owner.name}"
                )
            local_tags.add(tag)
            prior = tag_indexes.get(tag)
            if prior is None:
                tag_indexes[tag] = len(result)
                result.append((item, owner, distance))
            else:
                result[prior] = (item, owner, distance)
    if len(result) > MAX_EFFECTIVE_MODULES:
        raise RetailBuildingLifecycleError(
            f"effective module count exceeds {MAX_EFFECTIVE_MODULES}"
        )
    return result


def _state_rows(ancestry: tuple[SageObject, ...]) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    assignment_counter = [0]
    for draw, owner, distance in _effective_tagged_blocks(ancestry, carrier_keys=None):
        draw_scope = (_scope_name(draw),)

        def visit(block: SageBlock, scope: tuple[str, ...]) -> None:
            folded = block.kind.casefold()
            current_scope = (*scope, _scope_name(block))
            if folded in _STATE_KINDS:
                conditions = list(block.header_tokens)
                assignments, scripts = _flatten_block_assignments(
                    block,
                    owner=owner,
                    inheritance_distance=distance,
                    base_scope=current_scope,
                    counter=assignment_counter,
                )
                row: dict[str, object] = {
                    "family": block.kind,
                    "conditions": conditions,
                    "lifecyclePhases": _lifecycle_phases(conditions),
                    "drawModule": draw_scope[0],
                    "assignments": assignments,
                    "opaqueScripts": scripts,
                    "provenance": _provenance(
                        owner,
                        block,
                        inheritance_distance=distance,
                        scope_path=current_scope,
                    ),
                }
                rows.append(row)
                if len(rows) > MAX_STATE_BLOCKS:
                    raise RetailBuildingLifecycleError(
                        f"state block count exceeds {MAX_STATE_BLOCKS}"
                    )
                return
            for child in block.blocks:
                visit(child, current_scope)

        for child in draw.blocks:
            visit(child, draw_scope)
    return rows


def _module_rows(ancestry: tuple[SageObject, ...]) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    assignment_counter = [0]
    for block, owner, distance in _effective_tagged_blocks(
        ancestry, carrier_keys=_MODULE_CARRIERS
    ):
        assignments, scripts = _flatten_block_assignments(
            block,
            owner=owner,
            inheritance_distance=distance,
            base_scope=(_scope_name(block),),
            counter=assignment_counter,
        )
        if assignment_counter[0] > MAX_MODULE_ASSIGNMENTS:
            raise RetailBuildingLifecycleError(
                f"module assignment count exceeds {MAX_MODULE_ASSIGNMENTS}"
            )
        row: dict[str, object] = {
            "carrier": block.header_key,
            "moduleKind": block.kind,
            "assignments": assignments,
            "opaqueScripts": scripts,
            "inherited": distance > 0,
            "provenance": _provenance(
                owner,
                block,
                inheritance_distance=distance,
                scope_path=(_scope_name(block),),
            ),
        }
        if block.instance_tag is not None:
            row["instanceTag"] = block.instance_tag
        rows.append(row)
    return rows


def _placement_rows(ancestry: tuple[SageObject, ...]) -> list[dict[str, object]]:
    effective: dict[str, tuple[SageAssignment, SageObject, int]] = {}
    geometry: list[tuple[SageAssignment, SageObject, int]] = []
    total = len(ancestry)
    for object_index, owner in enumerate(ancestry):
        distance = total - object_index - 1
        for assignment in owner.assignments:
            key = assignment.key.casefold()
            if key.startswith("geometry") or key == "additionalgeometry":
                geometry.append((assignment, owner, distance))
            elif key in _PLACEMENT_KEYS:
                effective[key] = (assignment, owner, distance)
    selected = list(effective.values()) + geometry
    selected.sort(
        key=lambda row: (
            _sort_text(row[0].source_virtual_path),
            row[0].line,
            row[0].item_ordinal,
        )
    )
    return [
        {
            "key": assignment.key,
            "rawValue": assignment.value,
            "provenance": _provenance(
                owner,
                assignment,
                inheritance_distance=distance,
                scope_path=("Object",),
            ),
        }
        for assignment, owner, distance in selected
    ]


def _reference_group_key(row: Mapping[str, object]) -> tuple[object, ...]:
    provenance = row.get("provenance")
    if not isinstance(provenance, dict):
        raise RetailBuildingLifecycleError("visual reference has no provenance")
    scope = provenance.get("scopePath")
    conditions = row.get("conditions")
    phases = row.get("lifecyclePhases")
    if (
        not isinstance(scope, list)
        or not all(isinstance(item, str) for item in scope)
        or not isinstance(conditions, list)
        or not all(isinstance(item, str) for item in conditions)
        or not isinstance(phases, list)
        or not all(isinstance(item, str) for item in phases)
    ):
        raise RetailBuildingLifecycleError(
            "visual reference state fields are malformed"
        )
    return (
        row.get("targetObject"),
        tuple(scope),
        tuple(conditions),
        tuple(phases),
    )


def _slim_embedded_texture(row: Mapping[str, object]) -> dict[str, object]:
    return {
        key: row[key]
        for key in (
            "sourceW3dVirtualPath",
            "identifier",
            "status",
            "physicalVirtualPaths",
            "evidence",
            "provenance",
        )
        if key in row
    }


def _visual_bindings(
    closure: Mapping[str, object], source_names: set[str]
) -> list[dict[str, object]]:
    references: list[dict[str, object]] = []
    for field in ("exactLeaves", "semanticLeaves"):
        rows = closure.get(field)
        if not isinstance(rows, list):
            raise RetailBuildingLifecycleError(f"visual closure {field} must be a list")
        for row in rows:
            if not isinstance(row, dict):
                raise RetailBuildingLifecycleError("visual reference must be an object")
            if row.get("targetObject") in source_names:
                references.append(dict(row))
    unresolved = closure.get("unresolved")
    if not isinstance(unresolved, dict) or not isinstance(
        unresolved.get("references"), list
    ):
        raise RetailBuildingLifecycleError(
            "visual closure unresolved block is malformed"
        )
    for row in unresolved["references"]:
        if not isinstance(row, dict):
            raise RetailBuildingLifecycleError(
                "unresolved visual row must be an object"
            )
        if row.get("targetObject") in source_names:
            references.append(dict(row))

    dependency = closure.get("w3dDependencyClosure")
    if not isinstance(dependency, dict) or not isinstance(
        dependency.get("embeddedTextures"), list
    ):
        raise RetailBuildingLifecycleError("embedded texture closure is malformed")
    embedded_by_w3d: dict[str, list[dict[str, object]]] = {}
    for raw in dependency["embeddedTextures"]:
        if not isinstance(raw, dict):
            raise RetailBuildingLifecycleError("embedded texture row must be an object")
        source = _safe_virtual_path(
            raw.get("sourceW3dVirtualPath"), label="source W3D virtual path"
        )
        embedded_by_w3d.setdefault(source.casefold(), []).append(
            _slim_embedded_texture(raw)
        )

    groups: dict[tuple[object, ...], list[dict[str, object]]] = {}
    for row in references:
        groups.setdefault(_reference_group_key(row), []).append(row)
    result: list[dict[str, object]] = []
    for key, rows in groups.items():
        target, scope, conditions, phases = key
        rows.sort(
            key=lambda row: (
                str(row.get("status", "")),
                str(row.get("kind", "")),
                str(row.get("usage", "")),
                _sort_text(str(row.get("identifier", ""))),
                int(row.get("provenance", {}).get("line", 0)),  # type: ignore[union-attr]
            )
        )
        model_w3ds: set[str] = set()
        for row in rows:
            if row.get("kind") != "model" or row.get("status") != "resolved":
                continue
            paths = row.get("physicalVirtualPaths")
            if not isinstance(paths, list):
                raise RetailBuildingLifecycleError(
                    "resolved model reference has no physical paths"
                )
            for path in paths:
                safe = _safe_virtual_path(path, label="model W3D virtual path")
                if safe.casefold().endswith(".w3d"):
                    model_w3ds.add(safe)
        embedded = [
            item
            for path in sorted(model_w3ds, key=_sort_text)
            for item in embedded_by_w3d.get(path.casefold(), [])
        ]
        result.append(
            {
                "targetObject": target,
                "scopePath": list(scope),
                "conditions": list(conditions),
                "lifecyclePhases": list(phases),
                "references": rows,
                "embeddedTextureBindings": embedded,
            }
        )
    result.sort(
        key=lambda row: (
            _sort_text(str(row["targetObject"])),
            tuple(str(item) for item in row["scopePath"]),  # type: ignore[union-attr]
            tuple(str(item) for item in row["conditions"]),  # type: ignore[union-attr]
        )
    )
    return result


def _asset_paths(closure: Mapping[str, object]) -> tuple[str, ...]:
    paths: set[str] = set()
    for field in ("exactLeaves",):
        rows = closure.get(field)
        if not isinstance(rows, list):
            raise RetailBuildingLifecycleError(f"{field} must be a list")
        for row in rows:
            if not isinstance(row, dict):
                raise RetailBuildingLifecycleError("exact leaf must be an object")
            physical = row.get("physicalVirtualPaths")
            if not isinstance(physical, list):
                raise RetailBuildingLifecycleError(
                    "resolved exact leaf has no physical paths"
                )
            for path in physical:
                paths.add(_safe_virtual_path(path, label="bound asset virtual path"))
    dependency = closure.get("w3dDependencyClosure")
    if not isinstance(dependency, dict) or not isinstance(
        dependency.get("embeddedTextures"), list
    ):
        raise RetailBuildingLifecycleError("embedded texture closure is malformed")
    for row in dependency["embeddedTextures"]:
        if not isinstance(row, dict):
            raise RetailBuildingLifecycleError("embedded texture row must be an object")
        physical = row.get("physicalVirtualPaths", [])
        if not isinstance(physical, list):
            raise RetailBuildingLifecycleError(
                "embedded texture physical paths must be a list"
            )
        for path in physical:
            paths.add(_safe_virtual_path(path, label="bound texture virtual path"))
    result = tuple(sorted(paths, key=_sort_text))
    if len(result) > MAX_BOUND_ASSET_COUNT:
        raise RetailBuildingLifecycleError(
            f"bound asset count exceeds {MAX_BOUND_ASSET_COUNT}"
        )
    return result


def _asset_evidence(
    root: Path, closure: Mapping[str, object]
) -> list[dict[str, object]]:
    rows = [_hash_file(root, path) for path in _asset_paths(closure)]
    total = sum(int(row["byteCount"]) for row in rows)
    if total > MAX_BOUND_ASSET_TOTAL_BYTES:
        raise RetailBuildingLifecycleError(
            f"bound asset bytes exceed {MAX_BOUND_ASSET_TOTAL_BYTES}"
        )
    scanned = closure.get("scannedW3d")
    if not isinstance(scanned, list):
        raise RetailBuildingLifecycleError("scannedW3d must be a list")
    expected = {
        str(row["virtualPath"]).casefold(): row
        for row in scanned
        if isinstance(row, dict)
        and isinstance(row.get("virtualPath"), str)
        and isinstance(row.get("sha256"), str)
    }
    for row in rows:
        prior = expected.get(str(row["virtualPath"]).casefold())
        if prior is not None and (
            prior.get("sha256") != row["sha256"]
            or prior.get("byteLength") != row["byteCount"]
        ):
            raise RetailBuildingLifecycleError(
                f"scanned W3D evidence drift: {row['virtualPath']}"
            )
    return rows


def _fortress_handoff(
    root: Path,
    object_rows: Mapping[str, dict[str, object]],
) -> dict[str, object]:
    controller = object_rows["MenFortress"]
    castle_modules = [
        row
        for row in controller["runtimeModules"]  # type: ignore[index]
        if row.get("moduleKind", "").casefold() == "castlebehavior"
    ]
    assignments = [
        assignment
        for module in castle_modules
        for assignment in module["assignments"]
        if assignment.get("key", "").casefold()
        in {"castle tounpackforfaction", "castletounpackforfaction"}
    ]
    men = [
        row
        for row in assignments
        if str(row.get("rawValue", "")).split(maxsplit=1)[0].casefold() == "men"
    ]
    if len(men) != 1:
        raise RetailBuildingLifecycleError(
            "MenFortress must author one Men CastleToUnpackForFaction assignment"
        )
    tokens = str(men[0]["rawValue"]).split(maxsplit=1)
    if len(tokens) != 2:
        raise RetailBuildingLifecycleError(
            "Men CastleToUnpackForFaction must contain faction and template tokens"
        )
    template_token = tokens[1]
    if re.fullmatch(r"[A-Za-z0-9_-]+", template_token) is None:
        raise RetailBuildingLifecycleError(
            f"unsafe Men castle-template token: {template_token!r}"
        )
    template_slug = template_token.casefold()
    template_virtual_path = (
        f"bases/{template_slug}/{template_slug}.bse"
    )
    template_source = _read_bounded(
        root,
        template_virtual_path,
        maximum=MAX_BOUND_ASSET_BYTES,
        label="Men fortress base template",
    )
    try:
        template_evidence = parse_sage_base_template_bytes(template_source)
    except SageMapError as exc:
        raise RetailBuildingLifecycleError(
            f"Men fortress base template is unsupported: {exc}"
        ) from exc
    castle_templates = template_evidence["castleTemplates"]
    assert isinstance(castle_templates, dict)
    property_key = castle_templates["propertyKey"]
    if not isinstance(property_key, dict) or property_key != {
        "name": template_token,
        "wireType": "ascii-string",
        "wireTypeCode": 3,
    }:
        raise RetailBuildingLifecycleError(
            "Men fortress base-template property key does not match the "
            f"CastleBehavior token: {property_key!r}"
        )
    templates = castle_templates["templates"]
    if not isinstance(templates, list):
        raise RetailBuildingLifecycleError(
            "Men fortress CastleTemplates entries are malformed"
        )
    operational_templates = [
        row
        for row in templates
        if isinstance(row, dict)
        and row.get("templateName") == "MenFortressCitadel"
    ]
    if len(operational_templates) != 1:
        raise RetailBuildingLifecycleError(
            "Men fortress base template must contain exactly one "
            "MenFortressCitadel entry"
        )
    source_evidence = template_evidence["source"]
    assert isinstance(source_evidence, dict)
    return {
        "controllerObject": "MenFortress",
        "operationalObject": "MenFortressCitadel",
        "faction": tokens[0],
        "castleTemplateToken": template_token,
        "assignmentEvidence": men[0],
        "baseTemplateEvidence": {
            "virtualPath": template_virtual_path,
            "byteCount": source_evidence["byteCount"],
            "sha256": source_evidence["sha256"],
            "bodySha256": source_evidence["bodySha256"],
            "packaged": False,
            "castleTemplatesVersion": castle_templates["version"],
            "propertyKey": property_key,
            "templateCount": len(templates),
            "templates": templates,
            "objects": template_evidence["objects"],
        },
        "operationalTemplateEvidence": operational_templates[0],
        "status": "source-proven-template-to-operational-object-link",
    }


def _runtime_requirements(
    structures: list[dict[str, object]],
) -> list[dict[str, object]]:
    counters: Counter[str] = Counter()
    for structure in structures:
        for source in structure["sourceObjectEvidence"]:  # type: ignore[index]
            for state in source["visualStates"]:
                assignments = state["assignments"]
                keys = {str(item["key"]).casefold() for item in assignments}
                values = [str(item["rawValue"]) for item in assignments]
                counters["distinct-lifecycle-state-block"] += 1
                if "animationmode" in keys and any(
                    value.casefold() == "manual" for value in values
                ):
                    counters["manual-construction-animation-scrub"] += 1
                if "particlesysbone" in keys:
                    counters["particle-system-binding"] += 1
                if "enteringstatefx" in keys:
                    counters["entering-state-fx"] += 1
                if state["opaqueScripts"]:
                    counters["opaque-sage-draw-script"] += len(state["opaqueScripts"])
            for module in source["runtimeModules"]:
                if module["opaqueScripts"]:
                    counters["opaque-sage-module-script"] += len(
                        module["opaqueScripts"]
                    )
    return [
        {
            "feature": feature,
            "evidenceCount": counters[feature],
            "handoffStatus": "requires-runtime-implementation-and-render-proof",
        }
        for feature in sorted(counters, key=_sort_text)
    ]


def _report_summary(report: Mapping[str, object]) -> dict[str, object]:
    structures = report["structures"]
    assert isinstance(structures, list)
    source_objects = [
        source
        for structure in structures
        for source in structure["sourceObjectEvidence"]
    ]
    bindings = [
        binding for structure in structures for binding in structure["visualBindings"]
    ]
    references = [
        reference for binding in bindings for reference in binding["references"]
    ]
    embedded = [
        texture
        for binding in bindings
        for texture in binding["embeddedTextureBindings"]
    ]
    status_counts = Counter(str(item.get("status")) for item in references)
    return {
        "runtimeStructureCount": len(structures),
        "sourceObjectCount": len(source_objects),
        "visualStateCount": sum(len(item["visualStates"]) for item in source_objects),
        "runtimeModuleCount": sum(
            len(item["runtimeModules"]) for item in source_objects
        ),
        "placementAssignmentCount": sum(
            len(item["placementAssignments"]) for item in source_objects
        ),
        "visualBindingGroupCount": len(bindings),
        "visualReferenceCount": len(references),
        "resolvedVisualReferenceCount": status_counts["resolved"],
        "semanticVisualReferenceCount": status_counts["semantic"],
        "unresolvedVisualReferenceCount": sum(
            count
            for status, count in status_counts.items()
            if status not in {"resolved", "semantic"}
        ),
        "embeddedTextureBindingCount": len(embedded),
        "sourceEvidenceFileCount": len(report["sourceEvidence"]),
        "boundAssetCount": len(report["boundAssetEvidence"]),
        "boundAssetByteCount": sum(
            int(item["byteCount"]) for item in report["boundAssetEvidence"]
        ),
        "fortressHandoffUnresolvedCount": 1,
    }


def build_retail_building_lifecycle_report(
    effective_assets_root: Path | str,
) -> dict[str, object]:
    """Build deterministic payload-free evidence for the five Men structures."""

    root = Path(effective_assets_root)
    if root.is_symlink() or not root.is_dir():
        raise RetailBuildingLifecycleError(
            f"effective asset root is not a regular directory: {root}"
        )
    closure = _validated_visual_closure(
        build_retail_visual_closure(root, TARGET_OBJECTS)
    )
    cst, source_evidence = _source_documents(root, closure)
    index = _object_index(cst)
    object_rows: dict[str, dict[str, object]] = {}
    for name in TARGET_OBJECTS:
        target = _single_object(index, name)
        ancestry = _ancestry(index, target)
        object_rows[name] = {
            "name": name,
            "objectKind": target.kind,
            "source": {
                "virtualPath": target.source_virtual_path,
                "line": target.line,
            },
            "ancestry": [item.name for item in ancestry],
            "visualStates": _state_rows(ancestry),
            "runtimeModules": _module_rows(ancestry),
            "placementAssignments": _placement_rows(ancestry),
        }

    structures: list[dict[str, object]] = []
    for spec in _PROJECT_STRUCTURES:
        sources = spec["sourceObjects"]
        assert isinstance(sources, tuple)
        names = {str(item["name"]) for item in sources}
        structures.append(
            {
                "runtimeKind": spec["runtimeKind"],
                "projectRuntimeId": spec["projectRuntimeId"],
                "sourceObjectRoles": [dict(item) for item in sources],
                "sourceObjectEvidence": [
                    object_rows[str(item["name"])] for item in sources
                ],
                "visualBindings": _visual_bindings(closure, names),
            }
        )
    structures[0]["authoredHandoff"] = _fortress_handoff(root, object_rows)

    report: dict[str, object] = {
        "schema": SCHEMA,
        "schemaVersion": SCHEMA_VERSION,
        "scope": {
            "faction": "Men",
            "mapGate": "Fords of Isen II Men-versus-Men",
            "runtimeKinds": [str(item["runtimeKind"]) for item in _PROJECT_STRUCTURES],
            "targetObjects": list(TARGET_OBJECTS),
            "policy": (
                "retail identifiers and physical leaves only; no guessed state "
                "mapping or fallback art"
            ),
        },
        "visualClosureEvidence": {
            "schema": closure["schema"],
            "schemaVersion": closure["schemaVersion"],
            "aggregateSha256": closure["aggregateSha256"],
            "summary": closure["summary"],
        },
        "sourceEvidence": source_evidence,
        "boundAssetEvidence": _asset_evidence(root, closure),
        "structures": structures,
        "sourceProvenHandoffs": {
            "fortressObjectHandoff": structures[0]["authoredHandoff"],
        },
        "unresolvedOrUnsupported": {
            "visualReferences": closure["unresolved"],
            "opaqueScriptPolicy": (
                "BeginScript statements are source-located and SHA-256 sealed but "
                "not interpreted by this evidence pass."
            ),
        },
    }
    report["runtimeRequirements"] = _runtime_requirements(structures)
    report["summary"] = _report_summary(report)
    report["aggregateSha256"] = _canonical_sha256(report)
    return report


def verify_retail_building_lifecycle_report(report: object) -> dict[str, object]:
    """Validate schema, digest, and core count invariants before persistence."""

    if not isinstance(report, dict):
        raise RetailBuildingLifecycleError(
            "building lifecycle report must be an object"
        )
    if report.get("schema") != SCHEMA or report.get("schemaVersion") != SCHEMA_VERSION:
        raise RetailBuildingLifecycleError(
            "unexpected building lifecycle report schema"
        )
    _verify_digest(report, label="building lifecycle report")
    summary = report.get("summary")
    structures = report.get("structures")
    if not isinstance(summary, dict) or not isinstance(structures, list):
        raise RetailBuildingLifecycleError("report summary/structures are malformed")
    if summary.get("runtimeStructureCount") != len(_PROJECT_STRUCTURES):
        raise RetailBuildingLifecycleError("runtime structure count mismatch")
    if summary.get("sourceObjectCount") != len(TARGET_OBJECTS):
        raise RetailBuildingLifecycleError("source Object count mismatch")
    if len(structures) != len(_PROJECT_STRUCTURES):
        raise RetailBuildingLifecycleError("structure row count mismatch")
    return report


def write_retail_building_lifecycle_report(
    output_path: Path | str, report: object
) -> None:
    """Write a verified deterministic report without retail payload bytes."""

    write_json_atomic(
        Path(output_path), verify_retail_building_lifecycle_report(report)
    )


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Extract exact Men building lifecycle evidence"
    )
    parser.add_argument("effective_assets_root", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args(argv)
    report = build_retail_building_lifecycle_report(args.effective_assets_root)
    write_retail_building_lifecycle_report(args.output, report)
    summary = report["summary"]
    assert isinstance(summary, dict)
    print(
        "retail Men building lifecycle: "
        f"structures={summary['runtimeStructureCount']} "
        f"objects={summary['sourceObjectCount']} "
        f"references={summary['visualReferenceCount']} "
        f"unresolved={summary['unresolvedVisualReferenceCount']} "
        f"sha256={report['aggregateSha256']}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
