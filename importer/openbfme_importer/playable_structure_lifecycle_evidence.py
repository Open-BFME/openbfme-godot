"""Generic per-structure lifecycle evidence extraction.

The presenter-grade building-lifecycle contract needs facts the visual closure
does not carry: per-state ``AnimationMode``/``AnimationName`` pairs,
``EnteringStateFX`` and ``ParticleSysBone`` bindings, ``StructureCollapseUpdate``
numbers, authored audio behaviors, and ``W3DFloorDraw`` bib visibility rules.
This module reads exactly that evidence for one structure Object from the same
resolved SAGE corpus the descriptor compiler uses, reusing the sealed Men
lifecycle report's row shapes so the composition parsers stay shared.

No retail payload bytes are emitted: rows carry identifiers, raw assignment
values, and source locations only.
"""

from __future__ import annotations

from collections.abc import Mapping

from .playable_structure_pack_compiler import _digest
from .playable_unit_compiler import (
    PlayableUnitCompilerInputs,
    _ancestry,
    prepare_playable_unit_compiler,
)
from .retail_building_lifecycle import (
    _effective_tagged_blocks,
    _flatten_block_assignments,
    _module_rows,
    _scope_name,
    _state_rows,
)


SCHEMA = "openbfme.playable-structure-lifecycle-evidence"
SCHEMA_VERSION = 0

_FLOOR_DRAW_PREFIX = "w3dfloordraw"


class PlayableStructureLifecycleEvidenceError(ValueError):
    """The structure's lifecycle evidence cannot be read without guessing."""


def _with_source_objects(rows: list[dict[str, object]]) -> list[dict[str, object]]:
    for row in rows:
        provenance = row.get("provenance")
        if not isinstance(provenance, Mapping) or not isinstance(
            provenance.get("definingObject"), str
        ):
            raise PlayableStructureLifecycleEvidenceError(
                "lifecycle evidence row has no defining object provenance"
            )
        row["sourceObject"] = provenance["definingObject"]
    return rows


def _floor_draw_rows(ancestry: tuple[object, ...]) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    counter = [0]
    for block, owner, distance in _effective_tagged_blocks(
        ancestry, carrier_keys=None
    ):
        if not block.kind.casefold().startswith(_FLOOR_DRAW_PREFIX):
            continue
        assignments, scripts = _flatten_block_assignments(
            block,
            owner=owner,
            inheritance_distance=distance,
            base_scope=(_scope_name(block),),
            counter=counter,
        )
        rows.append(
            {
                "moduleKind": block.kind,
                "assignments": assignments,
                "opaqueScripts": scripts,
                "sourceObject": owner.name,
                "provenance": {
                    "definingObject": owner.name,
                    "virtualPath": block.source_virtual_path,
                    "line": block.line,
                    "inheritanceDistance": distance,
                    "scopePath": [_scope_name(block)],
                },
            }
        )
    return rows


def compile_structure_lifecycle_evidence(
    target_id: str,
    documents: Mapping[str, bytes],
    *,
    prepared: PlayableUnitCompilerInputs | None = None,
) -> dict[str, object]:
    """Extract one structure's lifecycle evidence document or fail closed."""

    if not target_id or len(target_id) > 256:
        raise PlayableStructureLifecycleEvidenceError("target Object id is invalid")
    if prepared is None:
        prepared = prepare_playable_unit_compiler(documents)
    elif prepared.documents is not documents:
        raise PlayableStructureLifecycleEvidenceError(
            "prepared compiler inputs belong to a different document mapping"
        )
    target = prepared.objects.get(target_id.casefold())
    if target is None:
        raise PlayableStructureLifecycleEvidenceError(
            f"effective Object is missing: {target_id}"
        )
    lineage = _ancestry(prepared.objects, target)
    document: dict[str, object] = {
        "schema": SCHEMA,
        "schemaVersion": SCHEMA_VERSION,
        "objectId": target.name,
        "visualStates": _with_source_objects(_state_rows(lineage)),
        "runtimeModules": _with_source_objects(_module_rows(lineage)),
        "floorDraws": _floor_draw_rows(lineage),
    }
    document["evidenceSha256"] = _digest(document)
    return document


def validate_structure_lifecycle_evidence(value: Mapping[str, object]) -> None:
    """Reject a lifecycle evidence document that drifted from its digest."""

    if value.get("schema") != SCHEMA or value.get("schemaVersion") != SCHEMA_VERSION:
        raise PlayableStructureLifecycleEvidenceError(
            "lifecycle evidence identity is invalid"
        )
    unsigned = dict(value)
    digest = unsigned.pop("evidenceSha256", None)
    if not isinstance(digest, str) or digest != _digest(unsigned):
        raise PlayableStructureLifecycleEvidenceError(
            "lifecycle evidence digest is invalid"
        )
    if not isinstance(value.get("objectId"), str) or not value["objectId"]:
        raise PlayableStructureLifecycleEvidenceError(
            "lifecycle evidence objectId is invalid"
        )
    for field in ("visualStates", "runtimeModules", "floorDraws"):
        if not isinstance(value.get(field), list):
            raise PlayableStructureLifecycleEvidenceError(
                f"lifecycle evidence {field} is invalid"
            )


__all__ = [
    "PlayableStructureLifecycleEvidenceError",
    "SCHEMA",
    "SCHEMA_VERSION",
    "compile_structure_lifecycle_evidence",
    "validate_structure_lifecycle_evidence",
]
