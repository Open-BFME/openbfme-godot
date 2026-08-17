"""Compile the complete neutral-mob/lair family from effective retail INIs.

Faction command reachability is not a valid denominator for creeps: map-owned
lairs, summoned creatures, ambient wildlife, and scenario variants often have
no ``UNIT_BUILD`` producer.  This catalog accounts for the same closed family
as the coverage ledger and preserves every member as either a normal playable
unit descriptor or a hashed authored-template descriptor without inventing a
production route.
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
    PlayableUnitCompilerInputs,
    PlayableUnitCompilerError,
    _ancestry,
    _object_semantic,
    compile_playable_unit_descriptor,
    playable_object_kind_of,
    playable_object_kind_of_provenance,
    prepare_playable_unit_compiler,
    validate_playable_unit_descriptor,
)
from .playable_structure_compiler import (
    PlayableStructureCompilerError,
    compile_playable_structure_descriptor,
    validate_playable_structure_descriptor,
)
from .neutral_prop_compiler import (
    NeutralPropCompilerError,
    compile_neutral_prop_descriptor,
    validate_neutral_prop_descriptor,
)
from .retail_ini_coverage import is_neutral_mob_object


SCHEMA = "openbfme.neutral-mob-catalog"
SCHEMA_VERSION = 3
MAX_MAP_PLACEMENT_ROOTS = 4096
TEMPLATE_SCHEMA = "openbfme.authored-neutral-mob-template"
_NO_BUILD_ROUTE = "is not targeted by an authored UNIT_BUILD command"
_PASSIVE_READINESS_REASON = (
    "scenario noncombatant, SquishCollide, or SlowDeathBehavior runtime evidence is incomplete"
)


class NeutralMobCatalogError(ValueError):
    """Raised when an effective neutral-mob object cannot be receipted."""


def neutral_unit_passive_runtime_ready(
    descriptor: Mapping[str, object],
) -> bool:
    """Require closed simulation only for the passive Squish wildlife slice.

    Other scenario units may intentionally retain independently tracked combat
    gaps.  The fieldless ``SquishCollide`` marker plus passive effective
    ``KindOf`` is the exact authored signature of this map-live wildlife slice;
    it must never remain catalog-ready with a stale unresolved simulation.
    """

    gameplay = descriptor.get("gameplay")
    kind_of = descriptor.get("kindOf")
    if not isinstance(kind_of, Mapping) or not isinstance(gameplay, Mapping):
        return True
    simulation = gameplay.get("simulation")
    if not isinstance(simulation, Mapping):
        return True
    member_kinds = {
        str(token).upper()
        for token in kind_of.get("primaryMember", [])
        if isinstance(token, str)
    }
    resolved = simulation.get("resolved")
    contracts = (
        resolved.get("moduleContracts", []) if isinstance(resolved, Mapping) else []
    )
    squish_rows = [
        row
        for row in contracts
        if isinstance(row, Mapping) and row.get("module") == "SquishCollide"
    ]
    slow_death_rows = [
        row
        for row in contracts
        if isinstance(row, Mapping) and row.get("module") == "SlowDeathBehavior"
    ]
    passive = bool(member_kinds & {"INERT", "NOT_AUTOACQUIRABLE", "NO_COLLIDE"})
    hostile = bool(member_kinds & {"CAN_ATTACK", "CREEP", "MONSTER"})
    scenario_admission = descriptor.get("scenarioAdmission")
    if (
        not passive
        or hostile
        or descriptor.get("production") != []
        or not isinstance(scenario_admission, Mapping)
    ):
        return True
    combat = resolved.get("combat") if isinstance(resolved, Mapping) else None
    scenario_only = (
        resolved.get("scenarioOnly") if isinstance(resolved, Mapping) else None
    )
    return bool(
        simulation.get("status") == "ready"
        and simulation.get("missing") == []
        and isinstance(combat, Mapping)
        and combat.get("disposition") == "noncombatant"
        and isinstance(scenario_only, Mapping)
        and scenario_only.get("disposition") == "explicit-scenario-admission"
        and descriptor.get("production") == []
        and bool(squish_rows)
        and all(
            row.get("runtimeStatus") == "executable"
            and row.get("extraction") == "typed"
            and row.get("fields") == {}
            for row in squish_rows
        )
        and bool(slow_death_rows)
        and all(
            row.get("runtimeStatus") == "executable"
            and row.get("extraction") == "typed"
            for row in slow_death_rows
        )
    )


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


def _authored_tokens(target: Any, key: str) -> list[str]:
    values = [
        assignment.value
        for assignment in target.assignments
        if assignment.key.casefold() == key.casefold()
    ]
    return [
        token
        for value in values
        for token in re.findall(r"[A-Za-z0-9_+.-]+", value)
    ]


def _authored_side(target: Any) -> str | None:
    tokens = _authored_tokens(target, "Side")
    return tokens[-1] if tokens else None


def _role(target: Any, effective_kind_of: Sequence[str]) -> str:
    identity = f"{target.name} {target.parent or ''}"
    kinds = {item.upper() for item in effective_kind_of}
    if (
        "STRUCTURE" in kinds
        and re.search(r"(?:lair|hole)", identity, re.IGNORECASE)
    ):
        return "lair"
    if "HORDE" in kinds:
        return "horde"
    if "HERO" in kinds:
        return "summoned-hero"
    if kinds & {"CREEP", "MONSTER", "INFANTRY", "CAVALRY"}:
        return "creature"
    return "ambient-or-scenario"


def _runtime_domain(role: str, effective_kind_of: Sequence[str]) -> str:
    kinds = {item.upper() for item in effective_kind_of}
    if "STRUCTURE" in kinds:
        return "structure"
    if role == "ambient-or-scenario":
        return "prop"
    return "unit"


def _map_root_is_active_structure(effective_kind_of: Sequence[str]) -> bool:
    """Admit map-rooted gameplay structures from authored KindOf only.

    Map placement alone is not gameplay evidence: trees, rocks, ambient sound
    emitters, and optimized props already belong to the map binding lane.  An
    exact map root becomes a scenario structure only when retail marks it as a
    STRUCTURE and also authors an active/interactive structure capability.
    """

    kinds = {item.upper().lstrip("+-") for item in effective_kind_of}
    return "STRUCTURE" in kinds and bool(
        kinds
        & {
            "CAPTURABLE",
            "LINKED_TO_FLAG",
            "AUTO_RALLYPOINT",
            "FS_FACTORY",
            "ECONOMY_STRUCTURE",
        }
    )


def _normalized_map_placement_roots(values: Sequence[str]) -> set[str]:
    if isinstance(values, (str, bytes)) or len(values) > MAX_MAP_PLACEMENT_ROOTS:
        raise NeutralMobCatalogError("map placement roots are invalid")
    roots: dict[str, str] = {}
    for value in values:
        if not isinstance(value, str) or not value or len(value) > 256:
            raise NeutralMobCatalogError("map placement root identity is invalid")
        folded = value.casefold()
        prior = roots.get(folded)
        if prior is not None and prior != value:
            raise NeutralMobCatalogError("map placement root identities collide by case")
        roots[folded] = value
    return set(roots)


def _unit_admission(role: str) -> dict[str, object]:
    surfaces = {
        "creature": [
            "map-placement",
            "script-spawn",
            "object-creation-list",
            "lair-spawn",
        ],
        "horde": [
            "map-placement",
            "script-spawn",
            "object-creation-list",
            "horde-payload",
        ],
        "summoned-hero": ["script-spawn", "object-creation-list"],
    }
    try:
        return {"role": role, "surfaces": surfaces[role]}
    except KeyError as exc:
        raise NeutralMobCatalogError(
            f"unit-domain neutral role has no admission contract: {role}"
        ) from exc


def _structure_admission(role: str) -> dict[str, object]:
    surfaces = [
        "map-placement",
        "script-spawn",
        "object-creation-list",
    ]
    if role == "lair":
        surfaces.append("lair-spawn")
    return {
        "role": "lair" if role == "lair" else "neutral-structure",
        "surfaces": surfaces,
    }


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
        raise NeutralMobCatalogError(
            f"neutral-mob {target.name} has an invalid authored module contract: {exc}"
        ) from exc
    descriptor: dict[str, object] = {
        "schema": TEMPLATE_SCHEMA,
        "schemaVersion": 1,
        "objectId": target.name,
        "declarationKind": target.kind,
        "parentObjectId": target.parent,
        "role": role,
        "category": "neutral-mob",
        "effectiveKindOf": list(kind_of),
        "moduleContracts": module_contracts,
        "sourceDocuments": [
            {"virtualPath": path, "semanticSha256": _digest(rows)}
            for path, rows in sorted(
                semantics_by_path.items(), key=lambda item: item[0].casefold()
            )
        ],
    }
    descriptor["descriptorSha256"] = _digest(descriptor)
    return descriptor


def compile_neutral_mob_catalog(
    documents: Mapping[str, bytes],
    *,
    game: str = "bfme2",
    prepared: PlayableUnitCompilerInputs | None = None,
    map_placement_object_ids: Sequence[str] = (),
) -> dict[str, object]:
    """Account for every effective object in the shared neutral-mob family."""

    if game not in {"bfme2", "rotwk"}:
        raise NeutralMobCatalogError(f"unsupported game {game!r}")
    map_placement_roots = _normalized_map_placement_roots(
        map_placement_object_ids
    )
    if prepared is None:
        prepared = prepare_playable_unit_compiler(documents)
    elif prepared.documents is not documents:
        raise NeutralMobCatalogError(
            "prepared compiler inputs belong to a different document mapping"
        )
    targets: list[Any] = []
    neutral_family_ids: set[str] = set()
    for target in prepared.objects.values():
        authored_kind_of = _authored_tokens(target, "KindOf")
        is_neutral_family = is_neutral_mob_object(
            object_id=target.name,
            parent_id=target.parent,
            source_ini=target.source_virtual_path,
            side=_authored_side(target),
            kind_of=authored_kind_of,
        )
        try:
            effective_kind_of = playable_object_kind_of(prepared, target.name)
        except PlayableUnitCompilerError as exc:
            if target.name.casefold() in map_placement_roots:
                raise NeutralMobCatalogError(
                    f"map-rooted object {target.name} has invalid inheritance: {exc}"
                ) from exc
            effective_kind_of = ()
        is_map_rooted_active_structure = (
            target.name.casefold() in map_placement_roots
            and _map_root_is_active_structure(effective_kind_of)
        )
        if is_neutral_family:
            neutral_family_ids.add(target.name.casefold())
        if is_neutral_family or is_map_rooted_active_structure:
            targets.append(target)
    targets.sort(key=lambda item: (item.name.casefold(), item.name))

    rows: list[dict[str, object]] = []
    for target in targets:
        try:
            kind_of = playable_object_kind_of(prepared, target.name)
            lineage = _ancestry(prepared.objects, target)
        except PlayableUnitCompilerError as exc:
            raise NeutralMobCatalogError(
                f"neutral-mob {target.name} has invalid inheritance: {exc}"
            ) from exc
        role = _role(target, kind_of)
        runtime_domain = _runtime_domain(role, kind_of)
        kind_of_define_provenance = playable_object_kind_of_provenance(
            prepared, target.name
        )
        if runtime_domain == "prop":
            try:
                descriptor = compile_neutral_prop_descriptor(
                    target.name,
                    documents,
                    prepared=prepared,
                    game=game,
                )
                validate_neutral_prop_descriptor(descriptor)
            except NeutralPropCompilerError as exc:
                raise NeutralMobCatalogError(
                    f"neutral-mob {target.name} failed prop descriptor compilation: {exc}"
                ) from exc
            runtime_status = "descriptor-ready"
            deferred_reason = None
        elif runtime_domain == "structure":
            try:
                descriptor = compile_playable_structure_descriptor(
                    target.name,
                    documents,
                    prepared=prepared,
                    game=game,
                    scenario_admission=_structure_admission(role),
                )
                validate_playable_structure_descriptor(descriptor)
            except PlayableStructureCompilerError as exc:
                # A map root is a denominator, not permission to invent an
                # armor/body/lifecycle contract. Preserve the exact
                # effective Object and the concrete compiler blocker so a
                # partially coverable map does not make the whole neutral
                # catalog disappear. Existing neutral-family regressions
                # remain fatal exactly as before.
                if target.name.casefold() in neutral_family_ids:
                    raise NeutralMobCatalogError(
                        f"neutral-mob {target.name} failed structure descriptor compilation: {exc}"
                    ) from exc
                descriptor = _template_descriptor(
                    target, lineage, kind_of=kind_of, role=role
                )
                runtime_status = "deferred"
                deferred_reason = str(exc)
            else:
                runtime_status = "descriptor-ready"
                deferred_reason = None
        else:
            try:
                descriptor = compile_playable_unit_descriptor(
                    target.name,
                    documents,
                    prepared=prepared,
                    game=game,
                    scenario_admission=_unit_admission(role),
                )
            except PlayableUnitCompilerError as exc:
                raise NeutralMobCatalogError(
                    f"neutral-mob {target.name} failed descriptor compilation: {exc}"
                ) from exc
            validate_playable_unit_descriptor(descriptor)
            if descriptor.get("objectId") != target.name:
                raise NeutralMobCatalogError(
                    f"neutral-mob {target.name} scenario descriptor changed identity"
                )
            if neutral_unit_passive_runtime_ready(descriptor):
                runtime_status = "descriptor-ready"
                deferred_reason = None
            else:
                runtime_status = "deferred"
                deferred_reason = _PASSIVE_READINESS_REASON
        row: dict[str, object] = {
            "objectId": target.name,
            "side": _authored_side(target),
            "role": role,
            "runtimeDomain": runtime_domain,
            "kindOfDefineProvenance": kind_of_define_provenance,
            "runtimeStatus": runtime_status,
            "mapPlacementRoot": target.name.casefold() in map_placement_roots,
            "mapPlacementAdded": (
                target.name.casefold() in map_placement_roots
                and target.name.casefold() not in neutral_family_ids
            ),
            "descriptor": descriptor,
        }
        if deferred_reason is not None:
            row["deferredReason"] = deferred_reason
        rows.append(row)

    summary = {
        "neutralMobCount": len(rows),
        "descriptorReadyCount": sum(
            row["runtimeStatus"] == "descriptor-ready" for row in rows
        ),
        "runtimeDeferredCount": sum(
            row["runtimeStatus"] == "deferred" for row in rows
        ),
        "lairCount": sum(row["role"] == "lair" for row in rows),
        "hordeCount": sum(row["role"] == "horde" for row in rows),
        "unitDomainCount": sum(row["runtimeDomain"] == "unit" for row in rows),
        "structureDomainCount": sum(
            row["runtimeDomain"] == "structure" for row in rows
        ),
        "propDomainCount": sum(row["runtimeDomain"] == "prop" for row in rows),
        "mapPlacementRootCount": sum(row["mapPlacementRoot"] is True for row in rows),
        "mapPlacementAddedCount": sum(row["mapPlacementAdded"] is True for row in rows),
    }
    catalog: dict[str, object] = {
        "schema": SCHEMA,
        "schemaVersion": SCHEMA_VERSION,
        "game": game,
        "neutralMobs": rows,
        "summary": summary,
    }
    catalog["catalogSha256"] = _digest(catalog)
    validate_neutral_mob_catalog(catalog)
    return catalog


def validate_neutral_mob_catalog(value: Mapping[str, object]) -> None:
    if value.get("schema") != SCHEMA or value.get("schemaVersion") != SCHEMA_VERSION:
        raise NeutralMobCatalogError("neutral-mob catalog schema is invalid")
    if value.get("game") not in {"bfme2", "rotwk"}:
        raise NeutralMobCatalogError("neutral-mob catalog game is invalid")
    rows = value.get("neutralMobs")
    summary = value.get("summary")
    if not isinstance(rows, list) or not isinstance(summary, Mapping):
        raise NeutralMobCatalogError("neutral-mob catalog rows or summary are invalid")
    identities: set[str] = set()
    for row in rows:
        if not isinstance(row, Mapping):
            raise NeutralMobCatalogError("neutral-mob catalog row is invalid")
        object_id = row.get("objectId")
        if not isinstance(object_id, str) or not object_id:
            raise NeutralMobCatalogError("neutral-mob object identity is invalid")
        if object_id.casefold() in identities:
            raise NeutralMobCatalogError("neutral-mob identities are duplicated")
        identities.add(object_id.casefold())
        if row.get("role") not in {
            "lair",
            "horde",
            "summoned-hero",
            "creature",
            "ambient-or-scenario",
        }:
            raise NeutralMobCatalogError(f"neutral-mob {object_id} role is invalid")
        domain = row.get("runtimeDomain")
        if domain not in {"unit", "structure", "prop"}:
            raise NeutralMobCatalogError(
                f"neutral-mob {object_id} runtime domain is invalid"
            )
        map_placement_root = row.get("mapPlacementRoot")
        map_placement_added = row.get("mapPlacementAdded")
        if not isinstance(map_placement_root, bool) or not isinstance(
            map_placement_added, bool
        ):
            raise NeutralMobCatalogError(
                f"neutral-mob {object_id} map placement provenance is invalid"
            )
        if map_placement_added and not map_placement_root:
            raise NeutralMobCatalogError(
                f"neutral-mob {object_id} added map placement is not a root"
            )
        define_provenance = row.get("kindOfDefineProvenance")
        if not isinstance(define_provenance, list) or any(
            not isinstance(item, Mapping)
            or set(item) != {"defineId", "sourceIni", "line", "authoredValue", "tokens"}
            or not isinstance(item.get("defineId"), str)
            or not item.get("defineId")
            or not isinstance(item.get("sourceIni"), str)
            or not item.get("sourceIni")
            or not isinstance(item.get("line"), int)
            or isinstance(item.get("line"), bool)
            or int(item.get("line", 0)) <= 0
            or not isinstance(item.get("authoredValue"), str)
            or not isinstance(item.get("tokens"), list)
            or any(not isinstance(token, str) or not token for token in item.get("tokens", []))
            for item in define_provenance
        ):
            raise NeutralMobCatalogError(
                f"neutral-mob {object_id} KindOf define provenance is invalid"
            )
        status = row.get("runtimeStatus")
        descriptor = row.get("descriptor")
        if status not in {"descriptor-ready", "deferred"} or not isinstance(
            descriptor, Mapping
        ):
            raise NeutralMobCatalogError(f"neutral-mob {object_id} status is invalid")
        if descriptor.get("objectId") != object_id:
            raise NeutralMobCatalogError(
                f"neutral-mob {object_id} descriptor identity is invalid"
            )
        if status == "deferred":
            if descriptor.get("schema") == TEMPLATE_SCHEMA:
                pass
            elif (
                domain == "unit"
                and row.get("deferredReason") == _PASSIVE_READINESS_REASON
                and not neutral_unit_passive_runtime_ready(descriptor)
            ):
                pass
            else:
                raise NeutralMobCatalogError(
                    f"neutral-mob {object_id} deferred descriptor is invalid"
                )
        if status == "descriptor-ready":
            try:
                if domain == "unit":
                    validate_playable_unit_descriptor(descriptor)
                elif domain == "structure":
                    validate_playable_structure_descriptor(descriptor)
                elif domain == "prop":
                    validate_neutral_prop_descriptor(descriptor)
                else:
                    raise NeutralMobCatalogError(f"neutral-mob {object_id} domain is invalid")
            except (
                PlayableUnitCompilerError,
                PlayableStructureCompilerError,
                NeutralPropCompilerError,
            ) as exc:
                raise NeutralMobCatalogError(
                    f"neutral-mob {object_id} descriptor is invalid: {exc}"
                ) from exc
            if domain == "unit" and not neutral_unit_passive_runtime_ready(descriptor):
                raise NeutralMobCatalogError(
                    f"neutral-mob {object_id} {_PASSIVE_READINESS_REASON}"
                )
        if map_placement_root and status == "descriptor-ready":
            admission = descriptor.get("scenarioAdmission")
            if (
                not isinstance(admission, Mapping)
                or "map-placement" not in admission.get("surfaces", [])
                or (
                    map_placement_added
                    and (
                        domain != "structure"
                        or "STRUCTURE" not in descriptor.get("kindOf", [])
                    )
                )
            ):
                raise NeutralMobCatalogError(
                    f"neutral-mob {object_id} map placement admission is invalid"
                )
        elif map_placement_root:
            if (
                descriptor.get("schema") != TEMPLATE_SCHEMA
                or "STRUCTURE" not in descriptor.get("effectiveKindOf", [])
                or not isinstance(row.get("deferredReason"), str)
                or not row.get("deferredReason")
            ):
                raise NeutralMobCatalogError(
                    f"neutral-mob {object_id} deferred map placement evidence is invalid"
                )
    expected = {
        "neutralMobCount": len(rows),
        "descriptorReadyCount": sum(
            row.get("runtimeStatus") == "descriptor-ready" for row in rows
        ),
        "runtimeDeferredCount": sum(
            row.get("runtimeStatus") == "deferred" for row in rows
        ),
        "lairCount": sum(row.get("role") == "lair" for row in rows),
        "hordeCount": sum(row.get("role") == "horde" for row in rows),
        "unitDomainCount": sum(row.get("runtimeDomain") == "unit" for row in rows),
        "structureDomainCount": sum(
            row.get("runtimeDomain") == "structure" for row in rows
        ),
        "propDomainCount": sum(row.get("runtimeDomain") == "prop" for row in rows),
        "mapPlacementRootCount": sum(row.get("mapPlacementRoot") is True for row in rows),
        "mapPlacementAddedCount": sum(row.get("mapPlacementAdded") is True for row in rows),
    }
    if dict(summary) != expected:
        raise NeutralMobCatalogError("neutral-mob summary disagrees with rows")
    digest = value.get("catalogSha256")
    unsigned = dict(value)
    unsigned.pop("catalogSha256", None)
    if digest != _digest(unsigned):
        raise NeutralMobCatalogError("neutral-mob catalog digest is invalid")


__all__ = [
    "NeutralMobCatalogError",
    "SCHEMA",
    "SCHEMA_VERSION",
    "compile_neutral_mob_catalog",
    "validate_neutral_mob_catalog",
]
