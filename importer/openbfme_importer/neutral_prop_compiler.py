"""Compile the bounded retail neutral passive-prop family.

This lane is intentionally separate from playable units and structures.  The
objects are map/script/OCL placeable, never command-bar production targets.
"""

from __future__ import annotations

from collections import defaultdict
from collections.abc import Mapping, Sequence
import hashlib
import json
import re

from .module_contracts import (
    ModuleContractError,
    compile_all_module_contracts,
    validate_module_contracts,
)
from .playable_unit_compiler import (
    PlayableUnitCompilerError,
    PlayableUnitCompilerInputs,
    _ancestry,
    _effective_top_blocks,
    _geometry_contact_points,
    _geometry_contract,
    _nested_references,
    _object_semantic,
    _public_bone_contract,
    _runtime_module_evidence,
    playable_object_kind_of,
    playable_object_kind_of_provenance,
    prepare_playable_unit_compiler,
)


SCHEMA = "openbfme.neutral-prop-descriptor"
SCHEMA_VERSION = 0
SCENARIO_SURFACES = ["map-placement", "script-spawn", "object-creation-list"]
NEUTRAL_PROP_OBJECT_IDS = frozenset(
    {"RockBigTroll", *(f"SpiderWebs{index:02d}" for index in range(1, 12))}
)
_NEUTRAL_PROP_KEYS = frozenset(value.casefold() for value in NEUTRAL_PROP_OBJECT_IDS)
_UNIT_KINDS = frozenset(
    {
        "ARCHER",
        "CAVALRY",
        "CREEP",
        "GIANT",
        "HERO",
        "HORDE",
        "INFANTRY",
        "MACHINE",
        "MONSTER",
        "SHIP",
        "SIEGEENGINE",
        "TRANSPORT",
        "TROLL",
    }
)


class NeutralPropCompilerError(ValueError):
    """Raised when a passive prop cannot be admitted without invention."""


def _canonical_bytes(value: object) -> bytes:
    return json.dumps(
        value,
        sort_keys=True,
        separators=(",", ":"),
        ensure_ascii=False,
        allow_nan=False,
    ).encode("utf-8")


def _digest(value: object) -> str:
    return hashlib.sha256(_canonical_bytes(value)).hexdigest()


def _authored_kind_of(lineage: Sequence[object]) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for item in lineage:
        for assignment in item.assignments:
            if assignment.key.casefold() != "kindof":
                continue
            rows.append(
                {
                    "ownerObjectId": item.name,
                    "expression": assignment.value.strip(),
                    "sourceIni": assignment.source_virtual_path,
                    "line": assignment.line,
                }
            )
    return rows


def _draw_modules(lineage: Sequence[object]) -> list[dict[str, object]]:
    return [
        {
            "kind": block.kind,
            **({"instanceTag": block.instance_tag} if block.instance_tag else {}),
            "sourceIni": block.source_virtual_path,
            "line": block.line,
        }
        for block in _effective_top_blocks(lineage)
        if (block.header_key or "").casefold() == "draw"
    ]


def _runtime_capabilities(
    module_evidence: Sequence[Mapping[str, object]],
) -> list[dict[str, object]]:
    """Receipt conditional authored roles without making a passive prop active.

    RockBigTroll is a map-placeable/grabbable prop until an authored weapon
    launches it.  Its inherited Bezier behavior is therefore a capability,
    not permission to fabricate autonomous combat or production behavior.
    """

    return [
        {
            "kind": "projectile-capable",
            "activation": "authored-projectile-launch",
            "runtimeStatus": "deferred",
            "moduleEvidence": dict(row),
        }
        for row in module_evidence
        if str(row.get("kind", "")).casefold() == "bezierprojectilebehavior"
    ]


def compile_neutral_prop_descriptor(
    target_id: str,
    documents: Mapping[str, bytes],
    *,
    prepared: PlayableUnitCompilerInputs | None = None,
    game: str = "bfme2",
) -> dict[str, object]:
    """Compile one exact SpiderWeb/RockBigTroll passive prop descriptor."""

    if game not in {"bfme2", "rotwk"}:
        raise NeutralPropCompilerError(f"unsupported game {game!r}")
    if not isinstance(target_id, str) or target_id.casefold() not in _NEUTRAL_PROP_KEYS:
        raise NeutralPropCompilerError(f"unsupported neutral prop identity: {target_id!r}")
    if prepared is None:
        prepared = prepare_playable_unit_compiler(documents)
    elif prepared.documents is not documents:
        raise NeutralPropCompilerError(
            "prepared compiler inputs belong to a different document mapping"
        )
    target = prepared.objects.get(target_id.casefold())
    if target is None:
        raise NeutralPropCompilerError(f"effective Object is missing: {target_id}")
    if target.name.casefold() != target_id.casefold():
        raise NeutralPropCompilerError("neutral prop identity resolution is inconsistent")
    try:
        lineage = _ancestry(prepared.objects, target)
        effective_kind_of = playable_object_kind_of(prepared, target.name)
        geometry = _geometry_contract(lineage, prepared.numeric_defines)
        contact_points = _geometry_contact_points(lineage)
        public_bones = _public_bone_contract(lineage)
        references = _nested_references(lineage)
        module_contracts = compile_all_module_contracts(lineage, target.name)
        module_evidence = _runtime_module_evidence(
            lineage,
            lineage,
            frozenset(),
        )
    except (PlayableUnitCompilerError, ModuleContractError) as exc:
        raise NeutralPropCompilerError(
            f"neutral prop {target.name} has malformed authored evidence: {exc}"
        ) from exc
    kinds = set(effective_kind_of)
    if (
        "IMMOBILE" not in kinds
        or not kinds.intersection({"INERT", "OPTIMIZED_PROP"})
        or "STRUCTURE" in kinds
        or kinds.intersection(_UNIT_KINDS)
    ):
        raise NeutralPropCompilerError(
            f"neutral prop {target.name} does not resolve to a passive prop KindOf"
        )
    model_references = references.get("model", [])
    draw_modules = _draw_modules(lineage)
    if not model_references or not draw_modules:
        raise NeutralPropCompilerError(
            f"neutral prop {target.name} has no authored model presentation"
        )

    semantics_by_path: dict[str, list[dict[str, object]]] = defaultdict(list)
    inheritance: list[dict[str, object]] = []
    for item in lineage:
        semantic = _object_semantic(item)
        semantics_by_path[item.source_virtual_path].append(semantic)
        inheritance.append(
            {
                "objectId": item.name,
                "declarationKind": item.kind,
                "parentObjectId": item.parent,
                "sourceIni": item.source_virtual_path,
                "line": item.line,
                "semanticSha256": _digest(semantic),
            }
        )

    descriptor: dict[str, object] = {
        "schema": SCHEMA,
        "schemaVersion": SCHEMA_VERSION,
        "game": game,
        "requestedObjectId": target_id,
        "objectId": target.name,
        "declarationKind": target.kind,
        "parentObjectId": target.parent,
        "inheritance": inheritance,
        "kindOf": {
            "authored": _authored_kind_of(lineage),
            "effective": list(effective_kind_of),
            "defineProvenance": playable_object_kind_of_provenance(
                prepared, target.name
            ),
        },
        "moduleContracts": module_contracts,
        "runtimeModuleEvidence": module_evidence,
        "runtimeCapabilities": _runtime_capabilities(module_evidence),
        "geometry": geometry,
        "geometryContactPoints": contact_points,
        "publicBones": public_bones,
        "presentation": {
            "drawModules": draw_modules,
            "sourceReferences": references,
        },
        "production": [],
        "scenarioAdmission": {
            "kind": "authored-passive-prop",
            "surfaces": list(SCENARIO_SURFACES),
            "buildCommandExposed": False,
            "evidence": "bounded-retail-neutral-prop-family",
        },
        "sourceDocuments": [
            {"virtualPath": path, "semanticSha256": _digest(rows)}
            for path, rows in sorted(
                semantics_by_path.items(), key=lambda item: item[0].casefold()
            )
        ],
    }
    descriptor["descriptorSha256"] = _digest(descriptor)
    validate_neutral_prop_descriptor(descriptor)
    return descriptor


def validate_neutral_prop_descriptor(value: Mapping[str, object]) -> None:
    if value.get("schema") != SCHEMA or value.get("schemaVersion") != SCHEMA_VERSION:
        raise NeutralPropCompilerError("neutral prop descriptor schema is invalid")
    if value.get("game") not in {"bfme2", "rotwk"}:
        raise NeutralPropCompilerError("neutral prop descriptor game is invalid")
    object_id = value.get("objectId")
    if (
        not isinstance(object_id, str)
        or object_id.casefold() not in _NEUTRAL_PROP_KEYS
        or value.get("requestedObjectId") != object_id
        or value.get("declarationKind") not in {"Object", "ChildObject"}
    ):
        raise NeutralPropCompilerError("neutral prop descriptor identity is invalid")
    inheritance = value.get("inheritance")
    if not isinstance(inheritance, list) or not inheritance:
        raise NeutralPropCompilerError("neutral prop inheritance is invalid")
    if inheritance[-1].get("objectId") != object_id:
        raise NeutralPropCompilerError("neutral prop inheritance target is invalid")
    for row in inheritance:
        if (
            not isinstance(row, Mapping)
            or row.get("declarationKind") not in {"Object", "ChildObject"}
            or not isinstance(row.get("objectId"), str)
            or not row.get("objectId")
            or not isinstance(row.get("sourceIni"), str)
            or not row.get("sourceIni")
            or not isinstance(row.get("line"), int)
            or isinstance(row.get("line"), bool)
            or int(row.get("line", 0)) <= 0
            or re.fullmatch(r"[0-9a-f]{64}", str(row.get("semanticSha256", "")))
            is None
        ):
            raise NeutralPropCompilerError("neutral prop inheritance row is invalid")
    kind_of = value.get("kindOf")
    effective = kind_of.get("effective") if isinstance(kind_of, Mapping) else None
    if (
        not isinstance(kind_of, Mapping)
        or not isinstance(kind_of.get("authored"), list)
        or not kind_of.get("authored")
        or not isinstance(effective, list)
        or "IMMOBILE" not in effective
        or not set(effective).intersection({"INERT", "OPTIMIZED_PROP"})
        or set(effective).intersection(_UNIT_KINDS | {"STRUCTURE"})
        or not isinstance(kind_of.get("defineProvenance"), list)
    ):
        raise NeutralPropCompilerError("neutral prop KindOf contract is invalid")
    try:
        validate_module_contracts(value.get("moduleContracts"), label="neutral prop")
    except ModuleContractError as exc:
        raise NeutralPropCompilerError(str(exc)) from exc
    module_evidence = value.get("runtimeModuleEvidence")
    if not isinstance(module_evidence, list):
        raise NeutralPropCompilerError("neutral prop runtime module evidence is invalid")
    identities: set[tuple[str, int, str, str]] = set()
    for row in module_evidence:
        if (
            not isinstance(row, Mapping)
            or row.get("ownerRole") != "container"
            or not isinstance(row.get("kind"), str)
            or not row.get("kind")
            or not isinstance(row.get("instanceTag"), str)
            or not isinstance(row.get("sourceIni"), str)
            or not row.get("sourceIni")
            or not isinstance(row.get("line"), int)
            or isinstance(row.get("line"), bool)
            or int(row.get("line", 0)) <= 0
            or row.get("consumed") is not False
            or re.fullmatch(r"[0-9a-f]{64}", str(row.get("semanticSha256", "")))
            is None
        ):
            raise NeutralPropCompilerError(
                "neutral prop runtime module evidence must be exact and consumed=false"
            )
        identity = (
            str(row["sourceIni"]).casefold(),
            int(row["line"]),
            str(row["instanceTag"]).casefold(),
            str(row["kind"]).casefold(),
        )
        if identity in identities:
            raise NeutralPropCompilerError(
                "neutral prop runtime module evidence is duplicated"
            )
        identities.add(identity)
    expected_capabilities = _runtime_capabilities(module_evidence)
    if value.get("runtimeCapabilities") != expected_capabilities:
        raise NeutralPropCompilerError("neutral prop runtime capabilities drifted")
    if value.get("production") != []:
        raise NeutralPropCompilerError("neutral prop production must be empty")
    admission = value.get("scenarioAdmission")
    if admission != {
        "kind": "authored-passive-prop",
        "surfaces": SCENARIO_SURFACES,
        "buildCommandExposed": False,
        "evidence": "bounded-retail-neutral-prop-family",
    }:
        raise NeutralPropCompilerError("neutral prop scenario admission is invalid")
    presentation = value.get("presentation")
    references = (
        presentation.get("sourceReferences")
        if isinstance(presentation, Mapping)
        else None
    )
    if (
        not isinstance(presentation, Mapping)
        or not isinstance(presentation.get("drawModules"), list)
        or not presentation.get("drawModules")
        or not isinstance(references, Mapping)
        or not isinstance(references.get("model"), list)
        or not references.get("model")
    ):
        raise NeutralPropCompilerError("neutral prop presentation is invalid")
    for field in ("geometryContactPoints", "publicBones", "sourceDocuments"):
        if not isinstance(value.get(field), list):
            raise NeutralPropCompilerError(f"neutral prop {field} is invalid")
    if not value.get("sourceDocuments"):
        raise NeutralPropCompilerError("neutral prop source provenance is empty")
    unsigned = dict(value)
    digest = unsigned.pop("descriptorSha256", None)
    if (
        not isinstance(digest, str)
        or re.fullmatch(r"[0-9a-f]{64}", digest) is None
        or digest != _digest(unsigned)
    ):
        raise NeutralPropCompilerError("neutral prop descriptor digest is invalid")


__all__ = [
    "NEUTRAL_PROP_OBJECT_IDS",
    "NeutralPropCompilerError",
    "SCENARIO_SURFACES",
    "SCHEMA",
    "SCHEMA_VERSION",
    "compile_neutral_prop_descriptor",
    "validate_neutral_prop_descriptor",
]
