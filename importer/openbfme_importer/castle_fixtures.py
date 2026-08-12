"""Map-referenced object corpus admission + ``fixtures.json`` (lane L2a).

``faction_import.build_faction_import_plan`` walks only command-reachable
objects, so castle map objects (``EreborGateDoors``, ``EBGarrisonableTower``,
``MinisWallA``, ``AngmarWallCarnDum`` …) have no compiled definition at all
(docs/castle-siege-design.md section 3.8).  This module is the admission path:

1. :func:`compile_map_object_descriptor` compiles one map-referenced type
   from the retail object corpus — KindOf, authored health, armor, geometry
   and module contracts — reusing the playable-structure/unit extractors.
   No production evidence is required (map objects are placed, never built),
   so the playable-structure compiler's fail-closed production gate does not
   apply; the contract here is compiled from the authored INI blocks directly.
2. :func:`build_map_fixtures` joins a map's placed object rows against the
   retail object index and emits the ``openbfme.sage-map-fixtures`` document
   (design 4.1): one fixture per admitted placement, carrying the type's
   contract plus the placement's authored properties.  ``capabilities`` is
   L1's derived requirement set plus the family-level
   ``skirmish-ai-libraries`` — exactly what the v2 ``castleSiege`` contracts
   pin, so the runtime can cross-check the two documents.
3. :func:`validate_map_fixtures` is the cook-time seal; the Godot loader
   (``retail_map_data.gd::_load_fixtures``) mirrors it and refuses malformed
   fixtures with named diagnostics.

Admission rules are retail-authored facts, never heuristics:

- a type is admissible when it resolves in the retail object index (the L1
  index: ``Object``/``ChildObject``/``ObjectReskin`` with inheritance
  resolved) and carries the ``STRUCTURE`` KindOf;
- role detection builds on L1's keys: ``gate`` = a ``GateOpenAndCloseBehavior``
  module (operable gates only); a ``BLOCKING_GATE``/``WALL_GATE`` KindOf
  without the module is retail's static gate prop and gets the explicit
  ``static-gate`` role; garrison = a
  ``HordeGarrisonContain``/``GarrisonContain`` module, never the ``GARRISON``
  KindOf and never a bare ``*Contain`` (citadel slaughter-houses stay
  ``structure``);
- fixtures require authored health (a Body ``MaxHealth``): retail authors
  indestructible ``STRUCTURE`` scenery with no Body (e.g. ``EBMineCartD``),
  and those types are recorded in the document's ``omitted`` list, never
  silently dropped;
- armor may be the recorded SAGE passthrough: ``null`` when no ``ArmorSet``
  is authored (e.g. ``CaptureFlag``), never an invented set name;
- everything else fail-closes: unresolvable types, gates without named
  geometry states, and garrisons without ``ContainMax`` are errors.
"""

from __future__ import annotations

import hashlib
import json
from dataclasses import replace
from pathlib import Path
from typing import Any, Iterable, Mapping, Sequence

from .armor_compiler import ArmorCompilerError, compile_armor_contract
from .castle_capabilities import (
    CastleCapabilityError,
    ObjectTypeInfo,
    build_retail_object_index,
    derive_map_capabilities,
    validate_capability_subset,
)
from .module_contracts import ModuleContractError, compile_all_module_contracts
from .playable_structure_compiler import (
    PlayableStructureCompilerError,
    _health_contract,
)
from .playable_unit_compiler import (
    PlayableUnitCompilerError,
    _ancestry,
    _effective_top_blocks,
    _geometry_contract,
    _kind_of,
    _numeric_defines,
    _object_index,
)


MAP_FIXTURES_SCHEMA = "openbfme.sage-map-fixtures"
MAP_FIXTURES_SCHEMA_VERSION = 0
MAP_OBJECT_DESCRIPTOR_SCHEMA = "openbfme.map-object-descriptor"

#: Fixture roles (a type gets exactly one; ``_role`` checks gate module, then
#: garrison module, then gate KindOf, then wall-mounted, then wall).
#: ``gate`` is strictly an OPERABLE gate: the ``GateOpenAndCloseBehavior``
#: module is authored and the gate block is required.  A
#: ``BLOCKING_GATE``/``WALL_GATE`` KindOf without the module is retail's
#: static gate prop (``MBMMGateC``, ``AngmarWallPosternGateCarnDum`` — their
#: command sets are commented out in the INI); it gets the explicit
#: ``static-gate`` role and never a gate block.  Garrison detection reuses
#: the L1 keys verbatim; ``wall-mounted`` is the ``WALL_UPGRADE``/``WALL_HUB``
#: set (wall catapults and hubs), and ``wall`` covers
#: walkable/scaleable/segment wall pieces.
FIXTURE_ROLES: tuple[str, ...] = (
    "gate",
    "garrison",
    "static-gate",
    "wall-mounted",
    "wall",
    "structure",
)

_GATE_KIND_OF = frozenset({"BLOCKING_GATE", "WALL_GATE"})
_GATE_BEHAVIOR = "gateopenandclosebehavior"
_GARRISON_MODULES = frozenset({"hordegarrisoncontain", "garrisoncontain"})
_WALL_MOUNTED_KIND_OF = frozenset({"WALL_UPGRADE", "WALL_HUB"})
_WALL_KIND_OF = frozenset({"WALL_SEGMENT", "WALK_ON_TOP_OF_WALL", "SCALEABLE_WALL"})
_KEEP_OBJECT_DIE = "keepobjectdie"

#: The family-level requirement (design 2.5) appended to the object-derived
#: set, exactly as ``map_profile._CASTLE_REQUIRED_CAPABILITIES`` pins it.
_FAMILY_CAPABILITY = "skirmish-ai-libraries"


class CastleFixturesError(ValueError):
    """A map object type or fixtures document is malformed or unresolvable."""


def _compile_lineage(
    type_name: str, raw: Mapping[str, Any]
) -> tuple[Any, Sequence[Any]]:
    item = raw.get(type_name.casefold())
    if item is None:
        raise CastleFixturesError(f"unknown map object type: {type_name}")
    target = item
    if (
        item.parent is not None
        and item.kind.casefold() == "object"
        and item.parent.casefold() not in raw
    ):
        # A plain ``Object`` header's second token that names no defined
        # object is a retail editor annotation, never a template (the L1
        # index applies the same rule; ``ChildObject``/``ObjectReskin``
        # parents still fail closed inside ``_ancestry``).
        target = replace(item, parent=None)
    try:
        ancestry = _ancestry(raw, target)
    except PlayableUnitCompilerError as exc:
        raise CastleFixturesError(str(exc)) from exc
    return item, ancestry


def compile_map_object_descriptor(
    type_name: str,
    documents: Mapping[str, bytes],
    *,
    raw: Mapping[str, Any] | None = None,
    defines: Mapping[str, int | float] | None = None,
    game: str = "bfme2",
) -> dict[str, Any]:
    """Compile one map-referenced object type into a descriptor document.

    The descriptor carries the authored health, armor, geometry and module
    contracts exactly as the playable compilers extract them; no production
    routes are required because map objects are placed, never constructed.
    """

    if raw is None:
        try:
            raw = _object_index(documents)
        except PlayableUnitCompilerError as exc:
            raise CastleFixturesError(str(exc)) from exc
    if defines is None:
        try:
            defines = _numeric_defines(documents)
        except PlayableUnitCompilerError as exc:
            raise CastleFixturesError(str(exc)) from exc
    item, ancestry = _compile_lineage(type_name, raw)
    try:
        health = _health_contract(ancestry, defines, item.name)
    except PlayableStructureCompilerError as exc:
        raise CastleFixturesError(str(exc)) from exc
    try:
        armor = compile_armor_contract(documents, ancestry, game=game)
    except ArmorCompilerError as exc:
        raise CastleFixturesError(str(exc)) from exc
    geometry = _geometry_contract(ancestry, defines)
    try:
        modules = compile_all_module_contracts(ancestry, item.name)
    except ModuleContractError as exc:
        raise CastleFixturesError(str(exc)) from exc
    descriptor: dict[str, Any] = {
        "schema": MAP_OBJECT_DESCRIPTOR_SCHEMA,
        "schemaVersion": 0,
        "objectId": item.name,
        "definition": item.kind,
        "kindOf": list(_kind_of(ancestry)),
        "health": health,
        "armor": armor,
        "geometry": geometry,
        "moduleContracts": modules,
        "sourceDocuments": sorted(
            {str(entry.source_virtual_path) for entry in ancestry}
        ),
    }
    canonical = json.dumps(
        descriptor, sort_keys=True, separators=(",", ":"), ensure_ascii=False
    )
    descriptor["descriptorSha256"] = hashlib.sha256(
        canonical.encode("utf-8")
    ).hexdigest()
    return descriptor


def _role(info: ObjectTypeInfo) -> str:
    behaviors = {behavior.casefold() for behavior in info.behaviors}
    kind_of = set(info.kind_of)
    if _GATE_BEHAVIOR in behaviors:
        return "gate"
    if _GARRISON_MODULES.intersection(behaviors):
        return "garrison"
    # Gate kinds outrank wall-mounted: Angmar's postern gates inherit BOTH
    # WALL_GATE and WALL_UPGRADE from their parents (they hang off wall hubs),
    # and they are gates that never open, never mounted defenses.
    if _GATE_KIND_OF.intersection(kind_of):
        return "static-gate"
    if _WALL_MOUNTED_KIND_OF.intersection(kind_of):
        return "wall-mounted"
    if _WALL_KIND_OF.intersection(kind_of):
        return "wall"
    return "structure"


def _module_assignments(ancestry: Sequence[Any], kind: str) -> dict[str, str]:
    """Case-folded assignment map of the first effective module of ``kind``."""

    for block in _effective_top_blocks(ancestry):
        if (block.header_key or "").casefold() != "behavior":
            continue
        if block.kind.casefold() != kind.casefold():
            continue
        return {assignment.key.casefold(): assignment.value for assignment in block.assignments}
    return {}


def _module_present(ancestry: Sequence[Any], kind: str) -> bool:
    return any(
        (block.header_key or "").casefold() == "behavior"
        and block.kind.casefold() == kind.casefold()
        for block in _effective_top_blocks(ancestry)
    )


def _required_text(fields: Mapping[str, str], key: str, label: str) -> str:
    value = fields.get(key.casefold())
    if value is None or not value.strip():
        raise CastleFixturesError(f"{label} omits required field {key}")
    return value.strip()


def _yes_no(fields: Mapping[str, str], key: str, label: str) -> bool:
    token = _required_text(fields, key, label).casefold()
    if token in {"yes", "true", "1"}:
        return True
    if token in {"no", "false", "0"}:
        return False
    raise CastleFixturesError(f"{label} {key} is not a yes/no value: {token!r}")


def _optional_yes_no(fields: Mapping[str, str], key: str, label: str) -> bool | None:
    if key.casefold() not in fields:
        return None
    return _yes_no(fields, key, label)


def _number(
    fields: Mapping[str, str],
    key: str,
    label: str,
    defines: Mapping[str, int | float],
) -> int | float:
    token = _required_text(fields, key, label)
    resolved = defines.get(token.casefold())
    if resolved is not None:
        return resolved
    text = token.rstrip("%")
    try:
        value = float(text) if ("." in text or "e" in text.casefold()) else int(text)
    except ValueError as exc:
        raise CastleFixturesError(
            f"{label} {key} is not numeric: {token!r}"
        ) from exc
    return value


def _geometry_number_value(row: object, label: str) -> float:
    if not isinstance(row, Mapping) or "value" not in row:
        raise CastleFixturesError(f"{label} has an unresolvable geometry scalar")
    return float(row["value"])  # type: ignore[index]


def _gate_block(
    ancestry: Sequence[Any], defines: Mapping[str, int | float], target_id: str
) -> dict[str, Any]:
    label = f"{target_id} GateOpenAndCloseBehavior"
    fields = _module_assignments(ancestry, "GateOpenAndCloseBehavior")
    if not fields:
        raise CastleFixturesError(f"{target_id} is a gate without the module")
    block: dict[str, Any] = {
        "openByDefault": _yes_no(fields, "OpenByDefault", label),
        "resetMilliseconds": _number(
            fields, "ResetTimeInMilliseconds", label, defines
        ),
        "percentOpenForPathing": _number(
            fields, "PercentOpenForPathing", label, defines
        ),
    }
    geometry = _geometry_contract(ancestry, defines)
    geometries: dict[str, Any] = {}
    for piece in (geometry or {}).get("pieces", []):
        name = piece.get("name")
        if not isinstance(name, str) or not name.strip():
            continue
        offset = piece.get("offset")
        geometries[name] = {
            "shape": str(piece.get("shape", "")),
            "majorRadius": _geometry_number_value(
                piece.get("majorRadius"), f"{target_id} geometry {name}"
            ),
            "minorRadius": _geometry_number_value(
                piece.get("minorRadius"), f"{target_id} geometry {name}"
            ),
            "height": _geometry_number_value(
                piece.get("height"), f"{target_id} geometry {name}"
            ),
            "offset": [
                float(offset["x"]),
                float(offset["y"]),
                float(offset["z"]),
            ]
            if isinstance(offset, Mapping)
            else [0.0, 0.0, 0.0],
        }
    if not geometries:
        raise CastleFixturesError(
            f"{target_id} is a gate without named geometry states"
        )
    block["geometries"] = geometries
    return block


def _garrison_block(
    ancestry: Sequence[Any], defines: Mapping[str, int | float], target_id: str
) -> dict[str, Any]:
    fields: dict[str, str] = {}
    kind = "HordeGarrisonContain"
    for candidate in ("HordeGarrisonContain", "GarrisonContain"):
        fields = _module_assignments(ancestry, candidate)
        if fields:
            kind = candidate
            break
    label = f"{target_id} {kind}"
    contain_max = _number(fields, "ContainMax", label, defines)
    if isinstance(contain_max, float) and not contain_max.is_integer():
        raise CastleFixturesError(f"{label} ContainMax is not an integer")
    block: dict[str, Any] = {"containMax": int(contain_max)}
    for key in (
        "AllowEnemiesInside",
        "AllowAlliesInside",
        "AllowNeutralInside",
        "AllowOwnPlayerInsideOverride",
        "KillPassengersOnDeath",
        "ShowPips",
    ):
        value = _optional_yes_no(fields, key, label)
        if value is not None:
            block[key[0].lower() + key[1:]] = value
    if "numberofexitpaths" in fields:
        exits = _number(fields, "NumberOfExitPaths", label, defines)
        block["numberOfExitPaths"] = int(exits)
    return block


def _health_value(descriptor: Mapping[str, Any], type_name: str) -> float:
    value = _health_value_or_none(descriptor)
    if value is None:
        raise CastleFixturesError(f"{type_name} authors no Body MaxHealth")
    return value


def _health_value_or_none(descriptor: Mapping[str, Any]) -> float | None:
    health = descriptor.get("health")
    if not isinstance(health, Mapping):
        return None
    primary = health.get("primary")
    if not isinstance(primary, Mapping):
        return None
    max_health = primary.get("maxHealth")
    if not isinstance(max_health, Mapping) or "value" not in max_health:
        return None
    value = float(max_health["value"])  # type: ignore[index]
    return value if value > 0 else None


def _armor_set_id(descriptor: Mapping[str, Any], type_name: str) -> str | None:
    armor = descriptor.get("armor")
    set_id = armor.get("setId") if isinstance(armor, Mapping) else None
    if set_id is None:
        # Not a gap: the armor contract records the SAGE engine passthrough
        # explicitly (no authored ArmorSet = unmodified damage).  CaptureFlag
        # is the standing retail example of an authored STRUCTURE with no set.
        return None
    if not isinstance(set_id, str) or not set_id.strip():
        raise CastleFixturesError(f"{type_name} authors an empty ArmorSet name")
    return set_id


def _fixture(
    info: ObjectTypeInfo,
    descriptor: Mapping[str, Any],
    placement: Mapping[str, Any],
    ancestry: Sequence[Any],
    defines: Mapping[str, int | float],
) -> dict[str, Any]:
    type_name = str(placement["typeName"])
    role = _role(info)
    properties = placement.get("properties")
    if not isinstance(properties, Mapping):
        properties = {}
    fixture: dict[str, Any] = {
        "typeName": type_name,
        "role": role,
        "index": int(placement["index"]),
        "position": [float(value) for value in placement["godotPosition"]],
        "angle": float(placement["godotYawRadians"]),
        "kindOf": list(info.kind_of),
        "maxHealth": _health_value(descriptor, type_name),
        "armor": _armor_set_id(descriptor, type_name),
    }
    owner = properties.get("originalOwner")
    if owner is not None:
        fixture["originalOwner"] = str(owner)
    for property_key, fixture_key in (
        ("objectInitialHealth", "initialHealth"),
        ("objectIndestructible", "indestructible"),
        ("objectEnabled", "enabled"),
        ("objectTargetable", "targetable"),
    ):
        if property_key in properties:
            fixture[fixture_key] = properties[property_key]
    if role == "gate":
        fixture["gate"] = _gate_block(ancestry, defines, info.name)
    elif role == "garrison":
        fixture["garrison"] = _garrison_block(ancestry, defines, info.name)
    if _module_present(ancestry, "KeepObjectDie"):
        fixture["deathRule"] = "keep-object"
    return fixture


def build_map_fixtures(
    documents: Mapping[str, bytes],
    placements: Iterable[Mapping[str, Any]],
    *,
    raw: Mapping[str, Any] | None = None,
    index: Mapping[str, ObjectTypeInfo] | None = None,
    defines: Mapping[str, int | float] | None = None,
    game: str = "bfme2",
) -> dict[str, Any]:
    """Build the ``openbfme.sage-map-fixtures`` document for one map.

    ``placements`` are the map's parsed object rows (``index``, ``typeName``,
    ``roadType``, ``godotPosition``, ``godotYawRadians``, ``properties``) —
    the same rows ``objects.json`` carries.  Non-``STRUCTURE`` types and the
    unresolved scenery residue (pinned by the L1 oracle tests) are not
    admitted; resolved ``STRUCTURE`` types without a Body ``MaxHealth`` are
    named in the document's ``omitted`` list, and a missing ``ArmorSet`` is
    emitted as the recorded ``null`` passthrough — nothing drops silently.
    """

    rows = list(placements)
    if raw is None:
        try:
            raw = _object_index(documents)
        except PlayableUnitCompilerError as exc:
            raise CastleFixturesError(str(exc)) from exc
    if index is None:
        index = build_retail_object_index(documents)
    if defines is None:
        try:
            defines = _numeric_defines(documents)
        except PlayableUnitCompilerError as exc:
            raise CastleFixturesError(str(exc)) from exc
    derivation = derive_map_capabilities(
        index, (str(row.get("typeName", "")) for row in rows)
    )
    capabilities = validate_capability_subset(
        tuple(derivation.required) + (_FAMILY_CAPABILITY,)
    )
    descriptors: dict[str, dict[str, Any]] = {}
    fixtures: list[dict[str, Any]] = []
    omitted: dict[str, str] = {}
    for row in rows:
        if int(row.get("roadType", 0)) != 0:
            continue
        type_name = str(row.get("typeName", ""))
        info = index.get(type_name.casefold())
        if info is None or "STRUCTURE" not in info.kind_of:
            continue
        folded = type_name.casefold()
        if folded not in descriptors:
            descriptors[folded] = compile_map_object_descriptor(
                type_name, documents, raw=raw, defines=defines, game=game
            )
        descriptor = descriptors[folded]
        if _health_value_or_none(descriptor) is None:
            # Retail authors indestructible STRUCTURE scenery with no Body
            # (e.g. Erebor's EBMineCartD).  It never becomes a sim fixture,
            # but the omission is named, never silent.
            omitted.setdefault(folded, type_name)
            continue
        _, ancestry = _compile_lineage(type_name, raw)
        fixtures.append(_fixture(info, descriptor, row, ancestry, defines))
    fixtures.sort(key=lambda fixture: int(fixture["index"]))
    return {
        "schema": MAP_FIXTURES_SCHEMA,
        "schemaVersion": MAP_FIXTURES_SCHEMA_VERSION,
        "capabilities": list(capabilities),
        "count": len(fixtures),
        "fixtures": fixtures,
        "omitted": [
            {"typeName": spelling, "reason": "no-authored-body-maxhealth"}
            for _, spelling in sorted(omitted.items(), key=lambda item: item[0])
        ],
    }


# --- cook-time validation (mirrored by retail_map_data.gd::_load_fixtures) --


def _is_number(value: object) -> bool:
    return isinstance(value, (int, float)) and not isinstance(value, bool)


def _validate_geometries(geometries: object, label: str) -> None:
    if not isinstance(geometries, Mapping) or not geometries:
        raise CastleFixturesError(f"{label} has invalid named geometries")
    for name, entry in geometries.items():
        if not isinstance(name, str) or not name.strip():
            raise CastleFixturesError(f"{label} has invalid named geometries")
        if not isinstance(entry, Mapping):
            raise CastleFixturesError(f"{label} has invalid named geometries")
        if not isinstance(entry.get("shape"), str) or not entry["shape"].strip():
            raise CastleFixturesError(f"{label} has invalid named geometries")
        for field in ("majorRadius", "minorRadius", "height"):
            value = entry.get(field)
            if not _is_number(value) or float(value) <= 0:  # type: ignore[arg-type]
                raise CastleFixturesError(f"{label} has invalid named geometries")
        offset = entry.get("offset")
        if (
            not isinstance(offset, Sequence)
            or isinstance(offset, (str, bytes))
            or len(offset) != 3
            or not all(_is_number(value) for value in offset)
        ):
            raise CastleFixturesError(f"{label} has invalid named geometries")


def _validate_gate_block(block: object, label: str) -> None:
    if not isinstance(block, Mapping):
        raise CastleFixturesError(f"{label} has an invalid gate module block")
    if not isinstance(block.get("openByDefault"), bool):
        raise CastleFixturesError(f"{label} has an invalid gate module block")
    reset = block.get("resetMilliseconds")
    if not _is_number(reset) or float(reset) <= 0:  # type: ignore[arg-type]
        raise CastleFixturesError(f"{label} has an invalid gate module block")
    percent = block.get("percentOpenForPathing")
    if not _is_number(percent) or float(percent) < 0:  # type: ignore[arg-type]
        raise CastleFixturesError(f"{label} has an invalid gate module block")
    _validate_geometries(block.get("geometries"), label)


def _validate_garrison_block(block: object, label: str) -> None:
    if not isinstance(block, Mapping):
        raise CastleFixturesError(f"{label} is missing its garrison module block")
    contain_max = block.get("containMax")
    if (
        not isinstance(contain_max, int)
        or isinstance(contain_max, bool)
        or contain_max <= 0
    ):
        raise CastleFixturesError(f"{label} has an invalid containMax")


def validate_map_fixtures(document: object) -> None:
    """Fail closed on any malformed fixtures document (cook-time seal)."""

    if not isinstance(document, Mapping):
        raise CastleFixturesError("map fixtures document must be an object")
    if (
        document.get("schema") != MAP_FIXTURES_SCHEMA
        or document.get("schemaVersion") != MAP_FIXTURES_SCHEMA_VERSION
    ):
        raise CastleFixturesError("map fixtures document has an unexpected schema")
    try:
        validate_capability_subset(
            document.get("capabilities")  # type: ignore[arg-type]
            if isinstance(document.get("capabilities"), Sequence)
            and not isinstance(document.get("capabilities"), (str, bytes))
            else []
        )
    except CastleCapabilityError as exc:
        raise CastleFixturesError(str(exc)) from exc
    fixtures = document.get("fixtures")
    if not isinstance(fixtures, Sequence) or isinstance(fixtures, (str, bytes)):
        raise CastleFixturesError("map fixtures document has no fixture rows")
    if not fixtures:
        raise CastleFixturesError("map fixtures document has no fixture rows")
    if document.get("count") != len(fixtures):
        raise CastleFixturesError("map fixtures count does not match its metadata")
    seen_indices: set[int] = set()
    for row in fixtures:
        if not isinstance(row, Mapping):
            raise CastleFixturesError("castle fixture record must be an object")
        type_name = row.get("typeName")
        if not isinstance(type_name, str) or not type_name.strip():
            raise CastleFixturesError("castle fixture has an invalid typeName")
        role = row.get("role")
        if role not in FIXTURE_ROLES:
            raise CastleFixturesError("castle fixture has an unknown role")
        index = row.get("index")
        if not isinstance(index, int) or isinstance(index, bool) or index < 0:
            raise CastleFixturesError("castle fixture has an invalid placement index")
        if index in seen_indices:
            raise CastleFixturesError("castle fixture placement index is duplicated")
        seen_indices.add(index)
        position = row.get("position")
        if (
            not isinstance(position, Sequence)
            or isinstance(position, (str, bytes))
            or len(position) != 3
            or not all(_is_number(value) for value in position)
        ):
            raise CastleFixturesError("castle fixture has an invalid placement")
        if not _is_number(row.get("angle")):
            raise CastleFixturesError("castle fixture has an invalid placement")
        owner = row.get("originalOwner")
        if not isinstance(owner, str) or not owner.strip():
            raise CastleFixturesError("castle fixture has an invalid originalOwner")
        kind_of = row.get("kindOf")
        if (
            not isinstance(kind_of, Sequence)
            or isinstance(kind_of, (str, bytes))
            or not kind_of
            or not all(isinstance(entry, str) for entry in kind_of)
        ):
            raise CastleFixturesError("castle fixture has an invalid kindOf")
        max_health = row.get("maxHealth")
        if not _is_number(max_health) or float(max_health) <= 0:  # type: ignore[arg-type]
            raise CastleFixturesError("castle fixture has an invalid maxHealth")
        armor = row.get("armor")
        if armor is not None and (not isinstance(armor, str) or not armor.strip()):
            # JSON null is the recorded SAGE passthrough (no authored
            # ArmorSet); anything else malformed fails closed.
            raise CastleFixturesError("castle fixture has an invalid armor")
        if "initialHealth" in row and not _is_number(row["initialHealth"]):
            raise CastleFixturesError("castle fixture has an invalid placement")
        for key in ("indestructible", "enabled", "targetable"):
            if key in row and not isinstance(row[key], bool):
                raise CastleFixturesError("castle fixture has an invalid placement")
        label = f"{role} fixture {type_name}"
        if role == "gate":
            _validate_gate_block(row.get("gate"), label)
            if "garrison" in row:
                raise CastleFixturesError(
                    "castle fixture role disagrees with its module block"
                )
        elif role == "garrison":
            _validate_garrison_block(row.get("garrison"), label)
            if "gate" in row:
                raise CastleFixturesError(
                    "castle fixture role disagrees with its module block"
                )
        elif "gate" in row or "garrison" in row:
            raise CastleFixturesError(
                "castle fixture role disagrees with its module block"
            )
    omitted = document.get("omitted", [])
    if not isinstance(omitted, Sequence) or isinstance(omitted, (str, bytes)):
        raise CastleFixturesError("map fixtures document has an invalid omitted list")
    for entry in omitted:
        if (
            not isinstance(entry, Mapping)
            or not isinstance(entry.get("typeName"), str)
            or not str(entry.get("typeName", "")).strip()
            or entry.get("reason") != "no-authored-body-maxhealth"
        ):
            raise CastleFixturesError(
                "map fixtures document has an invalid omitted entry"
            )


def make_effective_assets_fixtures_builder(
    effective_assets_root: str | Path,
    *,
    game: str = "bfme2",
) -> "Any":
    """Return a ``(target, parsed) -> fixtures document`` profile callable.

    The full effective INI view is loaded once (object corpus for the index,
    ``gamedata.ini``/includes for numeric defines, ``armor.ini`` for armor
    tables) and shared across maps.
    """

    root = Path(effective_assets_root)
    ini_root = root / "data" / "ini"
    documents: dict[str, bytes] = {}
    for path in sorted(ini_root.rglob("*")):
        if path.suffix.casefold() not in {".ini", ".inc"} or not path.is_file():
            continue
        virtual = path.relative_to(root).as_posix()
        documents[virtual] = path.read_bytes()
    raw = _object_index(documents)
    index = build_retail_object_index(documents)
    defines = _numeric_defines(documents)

    def build_fixtures(target: Any, parsed: Any) -> dict[str, Any]:
        return build_map_fixtures(
            documents, parsed.objects, raw=raw, index=index, defines=defines, game=game
        )

    return build_fixtures


__all__ = [
    "FIXTURE_ROLES",
    "MAP_FIXTURES_SCHEMA",
    "MAP_FIXTURES_SCHEMA_VERSION",
    "MAP_OBJECT_DESCRIPTOR_SCHEMA",
    "CastleFixturesError",
    "build_map_fixtures",
    "compile_map_object_descriptor",
    "make_effective_assets_fixtures_builder",
    "validate_map_fixtures",
]
