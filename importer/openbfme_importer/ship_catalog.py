"""Compile the complete effective retail ``KindOf = SHIP`` family.

Faction conversion only sees command-reachable objects.  That is the correct
boundary for a playable faction pack, but it used to make the naval coverage
ledger report every authored ship interface as absent.  This compiler uses the
effective object corpus instead: buildable ships receive the normal playable
unit descriptor, while inheritance templates and scenario-only variants retain
an explicit, hashed authored-template descriptor.  The latter are deliberately
*not* called runtime-ready; no fake ``UNIT_BUILD`` route is invented for them.
"""

from __future__ import annotations

import hashlib
import json
import re
from collections import defaultdict
from collections.abc import Mapping, Sequence
from typing import Any

from .module_contracts import ModuleContractError, compile_all_module_contracts
from .playable_unit_compiler import (
    PlayableUnitCompilerError,
    _ancestry,
    _object_semantic,
    compile_playable_unit_descriptor,
    playable_object_kind_of,
    prepare_playable_unit_compiler,
    validate_playable_unit_descriptor,
)


SCHEMA = "openbfme.ship-catalog"
SCHEMA_VERSION = 1
TEMPLATE_SCHEMA = "openbfme.authored-ship-template"
_NO_BUILD_ROUTE = "is not targeted by an authored UNIT_BUILD command"


class ShipCatalogError(ValueError):
    """Raised when an authored ship cannot be accounted for losslessly."""


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


def _effective_side(lineage: Sequence[Any], target_id: str) -> str:
    side = ""
    for item in lineage:
        for assignment in item.assignments:
            if assignment.key.casefold() != "side":
                continue
            tokens = re.findall(r"[A-Za-z0-9_+.-]+", assignment.value)
            side = tokens[0] if tokens else ""
    if not side:
        raise ShipCatalogError(f"ship {target_id} has no effective Side")
    return side


def _template_descriptor(
    target: Any,
    lineage: Sequence[Any],
    *,
    kind_of: Sequence[str],
    role: str,
) -> dict[str, object]:
    semantics_by_path: dict[str, list[dict[str, object]]] = defaultdict(list)
    for item in lineage:
        semantics_by_path[item.source_virtual_path].append(_object_semantic(item))
    try:
        module_contracts = compile_all_module_contracts(lineage, target.name)
    except ModuleContractError as exc:
        raise ShipCatalogError(
            f"ship {target.name} has an invalid authored module contract: {exc}"
        ) from exc
    descriptor: dict[str, object] = {
        "schema": TEMPLATE_SCHEMA,
        "schemaVersion": 1,
        "objectId": target.name,
        "declarationKind": target.kind,
        "parentObjectId": target.parent,
        "role": role,
        "category": "naval",
        "effectiveKindOf": list(kind_of),
        "moduleContracts": module_contracts,
        "sourceDocuments": [
            {
                "virtualPath": path,
                "semanticSha256": _digest(rows),
            }
            for path, rows in sorted(
                semantics_by_path.items(), key=lambda item: item[0].casefold()
            )
        ],
    }
    descriptor["descriptorSha256"] = _digest(descriptor)
    return descriptor


def compile_ship_catalog(
    documents: Mapping[str, bytes], *, game: str = "bfme2"
) -> dict[str, object]:
    """Account for every effective retail ship, independent of faction reachability."""

    prepared = prepare_playable_unit_compiler(documents)
    # Start from declarations which author SHIP and follow the actual Object
    # inheritance graph.  Do not ask the unit compiler to resolve every retail
    # Object just to discover this family: the corpus contains unrelated,
    # intentionally malformed WorldBuilder-only parents such as ``(Rocks)``.
    # Scanning all objects made one bad prop able to hide the entire naval
    # catalog.
    candidate_ids: set[str] = set()
    for target in prepared.objects.values():
        authored_tokens = {
            token.removeprefix("+").upper()
            for assignment in target.assignments
            if assignment.key.casefold() == "kindof"
            for token in re.findall(r"[A-Za-z0-9_+.-]+", assignment.value)
            if not token.startswith("-")
        }
        if "SHIP" in authored_tokens:
            candidate_ids.add(target.name.casefold())
    changed = True
    while changed:
        changed = False
        for target in prepared.objects.values():
            if (
                target.parent
                and target.parent.casefold() in candidate_ids
                and target.name.casefold() not in candidate_ids
            ):
                candidate_ids.add(target.name.casefold())
                changed = True

    ship_targets: list[tuple[Any, tuple[str, ...]]] = []
    for target in prepared.objects.values():
        if target.name.casefold() not in candidate_ids:
            continue
        kind_of = playable_object_kind_of(prepared, target.name)
        if "SHIP" in kind_of:
            ship_targets.append((target, kind_of))
    ship_targets.sort(key=lambda item: (item[0].name.casefold(), item[0].name))

    child_counts: dict[str, int] = defaultdict(int)
    for target, _kind_of in ship_targets:
        if target.parent:
            child_counts[target.parent.casefold()] += 1

    rows: list[dict[str, object]] = []
    for target, kind_of in ship_targets:
        lineage = _ancestry(prepared.objects, target)
        side = _effective_side(lineage, target.name)
        try:
            descriptor = compile_playable_unit_descriptor(
                target.name,
                documents,
                prepared=prepared,
                game=game,
            )
        except PlayableUnitCompilerError as exc:
            if _NO_BUILD_ROUTE not in str(exc):
                raise ShipCatalogError(
                    f"ship {target.name} failed playable descriptor compilation: {exc}"
                ) from exc
            role = (
                "inheritance-template"
                if child_counts.get(target.name.casefold(), 0)
                else "scenario-only"
            )
            admitted = compile_playable_unit_descriptor(
                target.name,
                documents,
                prepared=prepared,
                game=game,
                scenario_admission_role=role,
            )
            validate_playable_unit_descriptor(admitted)
            rows.append(
                {
                    "objectId": target.name,
                    "side": side,
                    "role": role,
                    "runtimeStatus": "descriptor-ready",
                    "descriptor": admitted,
                }
            )
        else:
            validate_playable_unit_descriptor(descriptor)
            rows.append(
                {
                    "objectId": target.name,
                    "side": side,
                    "role": "buildable",
                    "runtimeStatus": "descriptor-ready",
                    "descriptor": descriptor,
                }
            )

    summary = {
        "shipCount": len(rows),
        "buildableDescriptorCount": sum(
            row["role"] == "buildable" for row in rows
        ),
        "inheritanceTemplateCount": sum(
            row["role"] == "inheritance-template" for row in rows
        ),
        "scenarioOnlyCount": sum(row["role"] == "scenario-only" for row in rows),
        "runtimeDeferredCount": sum(row["runtimeStatus"] == "deferred" for row in rows),
    }
    catalog: dict[str, object] = {
        "schema": SCHEMA,
        "schemaVersion": SCHEMA_VERSION,
        "game": game,
        "ships": rows,
        "summary": summary,
    }
    catalog["catalogSha256"] = _digest(catalog)
    validate_ship_catalog(catalog)
    return catalog


def validate_ship_catalog(value: Mapping[str, object]) -> None:
    if value.get("schema") != SCHEMA or value.get("schemaVersion") != SCHEMA_VERSION:
        raise ShipCatalogError("ship catalog schema is invalid")
    if value.get("game") not in {"bfme2", "rotwk"}:
        raise ShipCatalogError("ship catalog game is invalid")
    ships = value.get("ships")
    summary = value.get("summary")
    if not isinstance(ships, list) or not isinstance(summary, Mapping):
        raise ShipCatalogError("ship catalog rows or summary are invalid")
    identities: set[str] = set()
    for row in ships:
        if not isinstance(row, Mapping):
            raise ShipCatalogError("ship catalog row is invalid")
        object_id = row.get("objectId")
        role = row.get("role")
        status = row.get("runtimeStatus")
        descriptor = row.get("descriptor")
        if not isinstance(object_id, str) or not object_id:
            raise ShipCatalogError("ship catalog object identity is invalid")
        side = row.get("side")
        if not isinstance(side, str) or not side:
            raise ShipCatalogError(f"ship {object_id} Side is invalid")
        if object_id.casefold() in identities:
            raise ShipCatalogError("ship catalog object identities are duplicated")
        identities.add(object_id.casefold())
        if role not in {"buildable", "inheritance-template", "scenario-only"}:
            raise ShipCatalogError(f"ship {object_id} role is invalid")
        if not isinstance(descriptor, Mapping):
            raise ShipCatalogError(f"ship {object_id} descriptor is invalid")
        if role == "buildable":
            if status != "descriptor-ready":
                raise ShipCatalogError(f"buildable ship {object_id} is not ready")
            validate_playable_unit_descriptor(descriptor)
            if descriptor.get("objectId") != object_id:
                raise ShipCatalogError(
                    f"buildable ship {object_id} identity is inconsistent"
                )
        else:
            if status != "descriptor-ready":
                raise ShipCatalogError(
                    f"scenario/template ship {object_id} is not descriptor-ready"
                )
            validate_playable_unit_descriptor(descriptor)
            admission = descriptor.get("scenarioAdmission")
            if (
                descriptor.get("objectId") != object_id
                or descriptor.get("category") != "naval"
                or not isinstance(admission, Mapping)
                or admission.get("role") != role
                or admission.get("buildCommandExposed") is not False
                or descriptor.get("production") != []
            ):
                raise ShipCatalogError(
                    f"scenario/template ship {object_id} admission is inconsistent"
                )
    expected = {
        "shipCount": len(ships),
        "buildableDescriptorCount": sum(
            row.get("role") == "buildable" for row in ships
        ),
        "inheritanceTemplateCount": sum(
            row.get("role") == "inheritance-template" for row in ships
        ),
        "scenarioOnlyCount": sum(row.get("role") == "scenario-only" for row in ships),
        "runtimeDeferredCount": sum(
            row.get("runtimeStatus") == "deferred" for row in ships
        ),
    }
    if dict(summary) != expected:
        raise ShipCatalogError("ship catalog summary disagrees with its rows")
    digest = value.get("catalogSha256")
    unsigned = dict(value)
    unsigned.pop("catalogSha256", None)
    if digest != _digest(unsigned):
        raise ShipCatalogError("ship catalog digest is invalid")


__all__ = [
    "SCHEMA",
    "SCHEMA_VERSION",
    "ShipCatalogError",
    "compile_ship_catalog",
    "validate_ship_catalog",
]
