"""Batch compiler contracts for remaining Class-B modules and death siblings.

Throughput path: extract measured retail field shapes into closed deferred
descriptor evidence without claiming live simulation. Census consumption is
earned by naming each module type while walking real assignments.
"""

from __future__ import annotations

from collections.abc import Mapping, Sequence
import re
from typing import TYPE_CHECKING

from .sage_cst import SageAssignment, SageBlock, SageObject

if TYPE_CHECKING:
    pass


def _walk_helpers() -> tuple[object, object, object]:
    # Late import avoids a unit_compiler <-> module_contracts cycle.
    from .playable_unit_compiler import (
        _effective_top_blocks,
        _tokens,
        _walk_blocks,
    )

    return _effective_top_blocks, _tokens, _walk_blocks


class ModuleContractError(ValueError):
    """A module contract cannot be compiled without guessing."""


def _assignment_map(block: SageBlock) -> dict[str, SageAssignment]:
    out: dict[str, SageAssignment] = {}
    for assignment in block.assignments:
        folded = assignment.key.casefold()
        # Last assignment wins (INI effective-last semantics).
        out[folded] = assignment
    return out


def _string_field(assignment: SageAssignment | None) -> dict[str, object] | None:
    if assignment is None:
        return None
    return {
        "authored": assignment.value,
        "value": assignment.value.strip(),
        "sourceIni": assignment.source_virtual_path,
        "line": assignment.line,
    }


def _token_list_field(assignment: SageAssignment | None) -> dict[str, object] | None:
    if assignment is None:
        return None
    _effective_top_blocks, _tokens, _walk_blocks = _walk_helpers()
    tokens = list(_tokens(assignment.value))
    return {
        "authored": assignment.value,
        "value": tokens,
        "sourceIni": assignment.source_virtual_path,
        "line": assignment.line,
    }


def _yes_no_field(assignment: SageAssignment | None, label: str) -> dict[str, object] | None:
    if assignment is None:
        return None
    folded = assignment.value.strip().casefold()
    if folded not in {"yes", "no"}:
        raise ModuleContractError(f"{label} must be Yes or No: {assignment.value!r}")
    return {
        "authored": assignment.value,
        "value": folded == "yes",
        "sourceIni": assignment.source_virtual_path,
        "line": assignment.line,
    }


def _coord_field(assignment: SageAssignment | None, label: str) -> dict[str, object] | None:
    if assignment is None:
        return None
    match = re.fullmatch(
        r"(?is)\s*X\s*:\s*([+-]?(?:\d+\.?\d*|\.\d+))\s+"
        r"Y\s*:\s*([+-]?(?:\d+\.?\d*|\.\d+))\s+"
        r"Z\s*:\s*([+-]?(?:\d+\.?\d*|\.\d+))\s*",
        assignment.value,
    )
    if match is None:
        raise ModuleContractError(
            f"{label} must be Coord3D X: Y: Z: {assignment.value!r}"
        )
    return {
        "authored": assignment.value,
        "value": {
            "x": float(match.group(1)),
            "y": float(match.group(2)),
            "z": float(match.group(3)),
        },
        "sourceIni": assignment.source_virtual_path,
        "line": assignment.line,
    }


def _die_mux_death_types(block: SageBlock, module: str) -> dict[str, object]:
    """DestroyDie-compatible DeathTypes subset used by KeepObjectDie/CreateObjectDie."""

    _effective_top_blocks, _tokens, _walk_blocks = _walk_helpers()
    rows = [
        assignment
        for assignment in block.assignments
        if assignment.key.casefold() == "deathtypes"
    ]
    if len(rows) > 1:
        raise ModuleContractError(f"{module} authors duplicate DeathTypes")
    if not rows:
        return {"deathTypes": "ALL", "excludedDeathTypes": []}
    tokens = tuple(token.upper() for token in _tokens(rows[0].value))
    if not tokens:
        raise ModuleContractError(f"{module} authors empty DeathTypes")
    # Forms:
    #   ALL [-EXCLUDED...]
    #   NONE [+INCLUDED...]   (common CreateObjectDie debris filter)
    head = tokens[0]
    included: list[str] = []
    excluded: list[str] = []
    if head == "ALL":
        for token in tokens[1:]:
            if not token.startswith("-") or len(token) < 2:
                raise ModuleContractError(
                    f"{module} authors unsupported DeathTypes: {' '.join(tokens)}"
                )
            excluded.append(token[1:])
        death_types = "ALL"
    elif head == "NONE":
        for token in tokens[1:]:
            if not token.startswith("+") or len(token) < 2:
                raise ModuleContractError(
                    f"{module} authors unsupported DeathTypes: {' '.join(tokens)}"
                )
            included.append(token[1:])
        death_types = "NONE"
    else:
        raise ModuleContractError(
            f"{module} authors unsupported DeathTypes: {' '.join(tokens)}"
        )
    return {
        "deathTypes": death_types,
        "excludedDeathTypes": excluded,
        "includedDeathTypes": included,
        "deathTypesAuthored": {
            "authored": rows[0].value,
            "sourceIni": rows[0].source_virtual_path,
            "line": rows[0].line,
        },
    }


def _row(
    module: str,
    block: SageBlock,
    fields: dict[str, object],
    *,
    runtime_status: str = "deferred",
    extraction: str = "typed",
    carrier: str = "",
) -> dict[str, object]:
    return {
        "module": module,
        "fields": fields,
        "runtimeStatus": runtime_status,
        # typed = known field schema; opaque-authored = every assignment stored
        # as authored text with no semantic interpretation (deferred Class C/D).
        "extraction": extraction,
        "carrier": carrier or (block.header_key or ""),
        "sourceIni": block.source_virtual_path,
        "line": block.line,
        "tag": block.instance_tag or "",
    }


def _behavior_blocks(lineage: Sequence[SageObject], kind: str) -> list[SageBlock]:
    _effective_top_blocks, _tokens, _walk_blocks = _walk_helpers()
    folded = kind.casefold()
    return [
        block
        for block in _walk_blocks(_effective_top_blocks(lineage))
        if (block.header_key or "").casefold() == "behavior"
        and block.kind.casefold() == folded
    ]


def _body_blocks(lineage: Sequence[SageObject], kind: str) -> list[SageBlock]:
    _effective_top_blocks, _tokens, _walk_blocks = _walk_helpers()
    folded = kind.casefold()
    return [
        block
        for block in _walk_blocks(_effective_top_blocks(lineage))
        if (block.header_key or "").casefold() == "body"
        and block.kind.casefold() == folded
    ]


# --- Class B ---------------------------------------------------------------

_ATTR_MOD_UPGRADE_FIELDS = frozenset(
    {
        "triggeredby",
        "attributemodifier",
        "conflictswith",
        "requiresalltriggers",
        "customanimandduration",
    }
)
_ATTR_MOD_UPGRADE_DEFERRED = frozenset({"customanimandduration"})


def compile_attribute_modifier_upgrades(
    lineage: Sequence[SageObject], target_id: str
) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for block in _behavior_blocks(lineage, "AttributeModifierUpgrade"):
        authored = {a.key.casefold() for a in block.assignments}
        unknown = authored - _ATTR_MOD_UPGRADE_FIELDS
        if unknown:
            raise ModuleContractError(
                f"{target_id} AttributeModifierUpgrade unsupported fields: "
                + ", ".join(sorted(unknown))
            )
        amap = _assignment_map(block)
        fields: dict[str, object] = {}
        for key, compiler in (
            ("TriggeredBy", _token_list_field),
            ("AttributeModifier", _string_field),
            ("ConflictsWith", _token_list_field),
        ):
            value = compiler(amap.get(key.casefold()))
            if value is not None:
                fields[key] = value
        requires = _yes_no_field(
            amap.get("requiresalltriggers"),
            f"{target_id} AttributeModifierUpgrade RequiresAllTriggers",
        )
        if requires is not None:
            fields["RequiresAllTriggers"] = requires
        deferred: list[dict[str, object]] = []
        for name in sorted(_ATTR_MOD_UPGRADE_DEFERRED):
            assignment = amap.get(name)
            if assignment is not None:
                deferred.append(
                    {
                        "name": assignment.key,
                        "authored": assignment.value,
                        "sourceIni": assignment.source_virtual_path,
                        "line": assignment.line,
                        "reason": "presentation-or-unmodeled-upgrade-side-effect",
                    }
                )
        if deferred:
            fields["deferredFields"] = deferred
        if "TriggeredBy" not in fields or "AttributeModifier" not in fields:
            raise ModuleContractError(
                f"{target_id} AttributeModifierUpgrade requires TriggeredBy "
                "and AttributeModifier"
            )
        # Deferred until attribute-modifier resolution + ConflictsWith /
        # RequiresAllTriggers mux have a runtime consumer (Codex REJECT on
        # ledger-only greening).
        rows.append(_row("AttributeModifierUpgrade", block, fields))
    rows.sort(key=lambda row: (str(row["sourceIni"]).casefold(), int(row["line"])))
    return rows


_GEOMETRY_UPGRADE_SUPPORTED = frozenset(
    {
        "triggeredby",
        "showgeometry",
        "hidegeometry",
        "conflictswith",
        "requiresalltriggers",
        "wallboundsmesh",
        "rampmesh1",
        "rampmesh2",
        "customanimandduration",
    }
)
_GEOMETRY_UPGRADE_DEFERRED = frozenset(
    {"customanimandduration", "wallboundsmesh", "rampmesh1", "rampmesh2"}
)


def compile_geometry_upgrades(
    lineage: Sequence[SageObject], target_id: str
) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for block in _behavior_blocks(lineage, "GeometryUpgrade"):
        authored = {a.key.casefold() for a in block.assignments}
        unknown = authored - _GEOMETRY_UPGRADE_SUPPORTED
        if unknown:
            raise ModuleContractError(
                f"{target_id} GeometryUpgrade unsupported fields: "
                + ", ".join(sorted(unknown))
            )
        amap = _assignment_map(block)
        fields: dict[str, object] = {}
        for key, compiler in (
            ("TriggeredBy", _token_list_field),
            ("ShowGeometry", _token_list_field),
            ("HideGeometry", _token_list_field),
            ("ConflictsWith", _token_list_field),
        ):
            value = compiler(amap.get(key.casefold()))
            if value is not None:
                fields[key] = value
        requires = _yes_no_field(
            amap.get("requiresalltriggers"),
            f"{target_id} GeometryUpgrade RequiresAllTriggers",
        )
        if requires is not None:
            fields["RequiresAllTriggers"] = requires
        deferred: list[dict[str, object]] = []
        for name in sorted(_GEOMETRY_UPGRADE_DEFERRED):
            assignment = amap.get(name)
            if assignment is not None:
                deferred.append(
                    {
                        "name": assignment.key,
                        "authored": assignment.value,
                        "sourceIni": assignment.source_virtual_path,
                        "line": assignment.line,
                        "reason": "mesh-or-presentation-field-without-runtime-oracle",
                    }
                )
        if deferred:
            fields["deferredFields"] = deferred
        if "TriggeredBy" not in fields:
            raise ModuleContractError(
                f"{target_id} GeometryUpgrade requires TriggeredBy"
            )
        # Deferred until ShowGeometry/HideGeometry mesh consumers exist
        # (Codex REJECT on ledger-only greening).
        rows.append(_row("GeometryUpgrade", block, fields))
    rows.sort(key=lambda row: (str(row["sourceIni"]).casefold(), int(row["line"])))
    return rows


def compile_inactive_bodies(
    lineage: Sequence[SageObject], target_id: str
) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for block in _body_blocks(lineage, "InactiveBody"):
        if block.assignments:
            raise ModuleContractError(
                f"{target_id} InactiveBody authors unexpected fields: "
                + ", ".join(sorted({a.key for a in block.assignments}, key=str.casefold))
            )
        # Indestructible body policy: presence is the entire contract.
        rows.append(
            _row(
                "InactiveBody",
                block,
                {"indestructible": True},
                runtime_status="deferred",
            )
        )
    rows.sort(key=lambda row: (str(row["sourceIni"]).casefold(), int(row["line"])))
    return rows


def compile_spawn_point_production_exits(
    lineage: Sequence[SageObject], target_id: str
) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for block in _behavior_blocks(lineage, "SpawnPointProductionExitUpdate"):
        amap = _assignment_map(block)
        unknown = set(amap) - {"spawnpointbonename"}
        if unknown:
            raise ModuleContractError(
                f"{target_id} SpawnPointProductionExitUpdate unsupported fields: "
                + ", ".join(sorted(unknown))
            )
        bone = _string_field(amap.get("spawnpointbonename"))
        if bone is None:
            raise ModuleContractError(
                f"{target_id} SpawnPointProductionExitUpdate requires "
                "SpawnPointBoneName"
            )
        rows.append(
            _row(
                "SpawnPointProductionExitUpdate",
                block,
                {"SpawnPointBoneName": bone},
            )
        )
    rows.sort(key=lambda row: (str(row["sourceIni"]).casefold(), int(row["line"])))
    return rows


def compile_supply_center_production_exits(
    lineage: Sequence[SageObject], target_id: str
) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for block in _behavior_blocks(lineage, "SupplyCenterProductionExitUpdate"):
        amap = _assignment_map(block)
        unknown = set(amap) - {"unitcreatepoint", "naturalrallypoint"}
        if unknown:
            raise ModuleContractError(
                f"{target_id} SupplyCenterProductionExitUpdate unsupported fields: "
                + ", ".join(sorted(unknown))
            )
        fields: dict[str, object] = {}
        create = _coord_field(
            amap.get("unitcreatepoint"),
            f"{target_id} SupplyCenterProductionExitUpdate UnitCreatePoint",
        )
        rally = _coord_field(
            amap.get("naturalrallypoint"),
            f"{target_id} SupplyCenterProductionExitUpdate NaturalRallyPoint",
        )
        if create is not None:
            fields["UnitCreatePoint"] = create
        if rally is not None:
            fields["NaturalRallyPoint"] = rally
        rows.append(_row("SupplyCenterProductionExitUpdate", block, fields))
    rows.sort(key=lambda row: (str(row["sourceIni"]).casefold(), int(row["line"])))
    return rows


def compile_supply_center_creates(
    lineage: Sequence[SageObject], target_id: str
) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for block in _behavior_blocks(lineage, "SupplyCenterCreate"):
        if block.assignments:
            raise ModuleContractError(
                f"{target_id} SupplyCenterCreate authors unexpected fields: "
                + ", ".join(sorted({a.key for a in block.assignments}, key=str.casefold))
            )
        rows.append(_row("SupplyCenterCreate", block, {}))
    rows.sort(key=lambda row: (str(row["sourceIni"]).casefold(), int(row["line"])))
    return rows


def compile_buildable_hero_list_upgrades(
    lineage: Sequence[SageObject], target_id: str
) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for block in _behavior_blocks(lineage, "BuildableHeroListUpgrade"):
        amap = _assignment_map(block)
        unknown = set(amap) - {"triggeredby"}
        if unknown:
            raise ModuleContractError(
                f"{target_id} BuildableHeroListUpgrade unsupported fields: "
                + ", ".join(sorted(unknown))
            )
        triggered = _token_list_field(amap.get("triggeredby"))
        if triggered is None:
            raise ModuleContractError(
                f"{target_id} BuildableHeroListUpgrade requires TriggeredBy"
            )
        rows.append(
            _row("BuildableHeroListUpgrade", block, {"TriggeredBy": triggered})
        )
    rows.sort(key=lambda row: (str(row["sourceIni"]).casefold(), int(row["line"])))
    return rows


def compile_allow_banner_spawn_upgrades(
    lineage: Sequence[SageObject], target_id: str
) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for block in _behavior_blocks(lineage, "AllowBannerSpawnUpgrade"):
        amap = _assignment_map(block)
        unknown = set(amap) - {"triggeredby"}
        if unknown:
            raise ModuleContractError(
                f"{target_id} AllowBannerSpawnUpgrade unsupported fields: "
                + ", ".join(sorted(unknown))
            )
        triggered = _token_list_field(amap.get("triggeredby"))
        if triggered is None:
            raise ModuleContractError(
                f"{target_id} AllowBannerSpawnUpgrade requires TriggeredBy"
            )
        rows.append(
            _row("AllowBannerSpawnUpgrade", block, {"TriggeredBy": triggered})
        )
    rows.sort(key=lambda row: (str(row["sourceIni"]).casefold(), int(row["line"])))
    return rows


_PERCENT_RE = re.compile(r"^\s*([+-]?\d+)\s*%\s*$")


def compile_spell_recharge_modifier_upgrades(
    lineage: Sequence[SageObject], target_id: str
) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for block in _behavior_blocks(lineage, "SpellRechargeModifierUpgrade"):
        percentages: list[dict[str, object]] = []
        label: dict[str, object] | None = None
        starts_active: dict[str, object] | None = None
        for assignment in block.assignments:
            folded = assignment.key.casefold()
            if folded == "percentage":
                match = _PERCENT_RE.fullmatch(assignment.value)
                if match is None:
                    raise ModuleContractError(
                        f"{target_id} SpellRechargeModifierUpgrade Percentage "
                        f"must be N%: {assignment.value!r}"
                    )
                percentages.append(
                    {
                        "authored": assignment.value,
                        "value": int(match.group(1)),
                        "sourceIni": assignment.source_virtual_path,
                        "line": assignment.line,
                    }
                )
            elif folded == "labelforpalantirstring":
                label = _string_field(assignment)
            elif folded == "startsactive":
                starts_active = _yes_no_field(
                    assignment,
                    f"{target_id} SpellRechargeModifierUpgrade StartsActive",
                )
            else:
                raise ModuleContractError(
                    f"{target_id} SpellRechargeModifierUpgrade unsupported field "
                    f"{assignment.key}"
                )
        if not percentages:
            raise ModuleContractError(
                f"{target_id} SpellRechargeModifierUpgrade requires Percentage"
            )
        fields: dict[str, object] = {"Percentage": percentages}
        if label is not None:
            fields["LabelForPalantirString"] = label
        if starts_active is not None:
            fields["StartsActive"] = starts_active
        rows.append(_row("SpellRechargeModifierUpgrade", block, fields))
    rows.sort(key=lambda row: (str(row["sourceIni"]).casefold(), int(row["line"])))
    return rows


# --- Death lifecycle batch -------------------------------------------------

_KEEP_OBJECT_DIE_FIELDS = frozenset({"deathtypes", "stayonradar", "collapsingtime"})
_KEEP_OBJECT_DIE_DEFERRED = frozenset({"stayonradar", "collapsingtime"})


def compile_keep_object_die(
    lineage: Sequence[SageObject], target_id: str
) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for block in _behavior_blocks(lineage, "KeepObjectDie"):
        authored = {a.key.casefold() for a in block.assignments}
        unknown = authored - _KEEP_OBJECT_DIE_FIELDS
        if unknown:
            raise ModuleContractError(
                f"{target_id} KeepObjectDie unsupported fields: "
                + ", ".join(sorted(unknown))
            )
        fields = _die_mux_death_types(block, "KeepObjectDie")
        deferred: list[dict[str, object]] = []
        amap = _assignment_map(block)
        for name in sorted(_KEEP_OBJECT_DIE_DEFERRED):
            assignment = amap.get(name)
            if assignment is not None:
                deferred.append(
                    {
                        "name": assignment.key,
                        "authored": assignment.value,
                        "sourceIni": assignment.source_virtual_path,
                        "line": assignment.line,
                        "reason": "structure-rubble-presentation-without-runtime-oracle",
                    }
                )
        if deferred:
            fields["deferredFields"] = deferred
        # KeepObjectDie intentionally does not destroy; presence is the policy.
        fields["destroyOnDeath"] = False
        # Runtime consumes destroyOnDeath=false at entity death (keep readable
        # corpse / refuse DestroyDie erase). Presentation-only deferredFields
        # (StayOnRadar, CollapsingTime) remain unexecuted evidence.
        rows.append(
            _row(
                "KeepObjectDie",
                block,
                fields,
                runtime_status="executable",
            )
        )
    rows.sort(key=lambda row: (str(row["sourceIni"]).casefold(), int(row["line"])))
    return rows


_CREATE_OBJECT_DIE_FIELDS = frozenset(
    {
        "creationlist",
        "deathtypes",
        "debrisportionofself",
        # Common BFME2 expansion CreateObjectDie filters: kept as deferred
        # authored evidence until a runtime status/upgrade mux is sourced.
        "exemptstatus",
        "requiredstatus",
        "upgraderequired",
    }
)
_CREATE_OBJECT_DIE_DEFERRED = frozenset(
    {
        "debrisportionofself",
        "exemptstatus",
        "requiredstatus",
        "upgraderequired",
    }
)


def compile_create_object_die(
    lineage: Sequence[SageObject], target_id: str
) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for block in _behavior_blocks(lineage, "CreateObjectDie"):
        authored = {a.key.casefold() for a in block.assignments}
        unknown = authored - _CREATE_OBJECT_DIE_FIELDS
        if unknown:
            raise ModuleContractError(
                f"{target_id} CreateObjectDie unsupported fields: "
                + ", ".join(sorted(unknown))
            )
        amap = _assignment_map(block)
        fields = _die_mux_death_types(block, "CreateObjectDie")
        creation = _string_field(amap.get("creationlist"))
        debris = _string_field(amap.get("debrisportionofself"))
        # Retail authors two CreateObjectDie shapes:
        #   1) CreationList egg/spawn on death (executable when no muxes)
        #   2) DebrisPortionOfSelf presentation debris only (trebuchets, etc.)
        # Requiring CreationList rejected debris-only modules that ship retail.
        if creation is None and debris is None:
            raise ModuleContractError(
                f"{target_id} CreateObjectDie requires CreationList or "
                "DebrisPortionOfSelf"
            )
        if creation is not None:
            fields["CreationList"] = creation
        if debris is not None:
            fields["DebrisPortionOfSelf"] = debris
        deferred: list[dict[str, object]] = []
        for name in sorted(_CREATE_OBJECT_DIE_DEFERRED):
            assignment = amap.get(name)
            if assignment is not None:
                # DebrisPortionOfSelf is now a first-class field above when it
                # is the only spawn path; still record deferred evidence for
                # presentation debris when a CreationList is also present.
                if (
                    name == "debrisportionofself"
                    and creation is None
                    and debris is not None
                ):
                    continue
                deferred.append(
                    {
                        "name": assignment.key,
                        "authored": assignment.value,
                        "sourceIni": assignment.source_virtual_path,
                        "line": assignment.line,
                        "reason": "debris-portion-without-local-runtime-oracle",
                    }
                )
        if deferred:
            fields["deferredFields"] = deferred
        # Behavioral status/upgrade muxes (ExemptStatus, RequiredStatus,
        # UpgradeRequired) change when death fires. Without a runtime mux,
        # claim deferred rather than execute under forbidden conditions.
        behavioral_deferred = {
            str(row.get("name", "")).casefold()
            for row in deferred
            if str(row.get("name", "")).casefold()
            in {"exemptstatus", "requiredstatus", "upgraderequired"}
        }
        # Debris-only dies have no CreationList to queue; keep them deferred
        # until a debris presentation consumer exists.
        if creation is None:
            status = "deferred"
        else:
            status = "deferred" if behavioral_deferred else "executable"
        rows.append(
            _row(
                "CreateObjectDie",
                block,
                fields,
                runtime_status=status,
            )
        )
    rows.sort(key=lambda row: (str(row["sourceIni"]).casefold(), int(row["line"])))
    return rows


# Remaining Class C/D/E kinds converted as opaque-authored deferred evidence.
# Typed extractors above own their kinds; this set must not overlap them.
# Generated from retail_module_census unhandled status at conversion time;
# empty residual unhandled is the acceptance criterion for this batch.
OPAQUE_DEFERRED_MODULE_KINDS: frozenset[str] = frozenset(
    {
        "AIGateUpdate",
        "AISpecialPowerUpdate",
        "AODCrushCollide",
        "AimWeaponBehavior",
        "AnimalAIUpdate",
        "AnnounceBirthAndDeathBehavior",
        "AttachUpdate",
        "AttributeModifierPoolUpdate",
        "AudioLoopUpgrade",
        "AutoAbilityBehavior",
        "AutoPickUpUpdate",
        "BannerCarrierUpdate",
        "BaseUpgrade",
        "BeaconClientUpdate",
        "BloodthirstyUpdate",
        "BoredUpdate",
        "BridgeBehavior",
        "BuildingBehavior",
        "CastleUpgrade",
        "CitadelSlaughterHordeContain",
        "CivilianSpawnCollide",
        "CivilianSpawnUpdate",
        "ClearanceTestingSlowDeathBehavior",
        "ClickReactionBehavior",
        "CloudBreakSpecialPower",
        "CommandButtonHuntUpdate",
        "CritterEmitterUpdate",
        "DamageFieldUpdate",
        "DamageFilteredCreateObjectDie",
        "DarknessSpecialPower",
        "DefaultDraw",
        "DelayedDeathBody",
        "DelayedLuaEventUpdate",
        "DestroyEnvironmentUpdate",
        "DetachableRiderBody",
        "DetachableRiderUpdate",
        "DevastateSpecialPower",
        "DoCommandUpgrade",
        "DynamicPortalBehaviour",
        "DynamicShroudClearingRangeUpdate",
        "ElvenWoodSpecialPower",
        "EvaAnnounceClientCreate",
        "EvacuateDamage",
        "FXListDie",
        "FadeAndDieOrnamentUpdate",
        "FakePathfindPortalBehaviour",
        "FellBeastSwoopPower",
        "FireSpreadUpdate",
        "FireWeaponWhenDeadBehavior",
        "FlammableUpdate",
        "FloodUpdate",
        "FoundationAIUpdate",
        "GarrisonContain",
        "GateOpenAndCloseBehavior",
        "GateProxyBehavior",
        "GettingBuiltBehavior",
        "GiveUpgradeUpdate",
        "HeroDie",
        "HitReactionBehavior",
        "HordeGarrisonContain",
        "HordeMemberCollide",
        "HordeNotifyTargetsOfImminentProbableCrushingUpdate",
        "HordeSiegeEngineContain",
        "HordeTransportContain",
        "HordeTransportContainDamage",
        "LargeGroupAudioUpdate",
        "LargeGroupBonusUpdate",
        "LaserUpdate",
        "ModelConditionSoundSelectorClientBehavior",
        "ModelConditionUpgrade",
        "MonitorConditionUpdate",
        "NotifyTargetsOfImminentProbableCrushingUpdate",
        "OCLUpdate",
        "OathbreakersFadeAwayBehavior",
        "ObjectCreationUpgrade",
        "OilSpillUpdate",
        "PartTheHeavensUpdate",
        "PassiveAreaEffectBehavior",
        "PhysicsBehavior",
        "PickupStuffUpdate",
        "PillageModule",
        "PlayerUpgradeSpecialPower",
        "PoisonedBehavior",
        "PorcupineFormationBodyModule",
        "ProductionQueueHordeContain",
        "ProductionSpeedBonus",
        "ProductionUpdate",
        "ProjectileStreamUpdate",
        "RadarMarkerClientUpdate",
        "RadiateFearUpdate",
        "RandomSoundSelectorClientBehavior",
        "RebuildHoleBehavior",
        "RebuildHoleExposeDie",
        "RemoveUpgradeUpgrade",
        "ReplaceSelfUpgrade",
        "RespawnBody",
        "RespawnUpdate",
        "RousingSpeechUpdate",
        "RubbleRiseUpdate",
        "RunOffMapBehavior",
        "SalvageCrateCollide",
        "ShareExperienceBehavior",
        "ShipSlowDeathBehavior",
        "SiegeDeployHordeSpecialPower",
        "SiegeDockingBehavior",
        "SiegeEngineContain",
        "SlaughterHordeContain",
        "SlaveWatcherBehavior",
        "SlavedUpdate",
        "SpawnBehavior",
        "SpawnUnitBehavior",
        "SquishCollide",
        "StealthDetectorUpdate",
        "StealthUpdate",
        "StrafeAreaUpdate",
        "StructureToppleUpdate",
        "SummonReplacementSpecialAbilityUpdate",
        "SupplyCenterDockUpdate",
        "SwayClientUpdate",
        "SymbioticStructuresBody",
        "TaintSpecialPower",
        "TemporarilyDefectUpdate",
        "TerrainResourceClientBehavior",
        "ThreatFinderUpdate",
        "TooltipUpgrade",
        "ToppleUpdate",
        "TransitionDamageFX",
        "TransportContain",
        "TunnelContain",
        "UpgradeDie",
        "UpgradeSoundSelectorClientBehavior",
        "VeterancyCrateCollide",
        "W3DBuffDraw",
        "W3DDebrisDraw",
        "W3DDefaultDraw",
        "W3DHordeModelDraw",
        "W3DLaserDraw",
        "W3DLightDraw",
        "W3DProjectileStreamDraw",
        "W3DPropDraw",
        "W3DQuadrupedDraw",
        "W3DSailModelDraw",
        "W3DStreakDraw",
        "W3DTornadoDraw",
        "W3DTreeDraw",
        "W3DTruckDraw",
        "WallHubBehavior",
        "WallUpgradeUpdate",
        "WeaponChangeSpecialPowerModule",
    }
)

# Typed extractors already own these names; never also emit opaque rows.
TYPED_MODULE_KINDS: frozenset[str] = frozenset(
    {
        "AttributeModifierUpgrade",
        "GeometryUpgrade",
        "InactiveBody",
        "SpawnPointProductionExitUpdate",
        "SupplyCenterProductionExitUpdate",
        "SupplyCenterCreate",
        "BuildableHeroListUpgrade",
        "AllowBannerSpawnUpgrade",
        "SpellRechargeModifierUpgrade",
        "KeepObjectDie",
        "CreateObjectDie",
    }
)


def _module_blocks(lineage: Sequence[SageObject], kind: str) -> list[SageBlock]:
    """Any carrier (Behavior/Body/Draw/ClientUpdate/ClientBehavior/Flasher)."""

    _effective_top_blocks, _tokens, _walk_blocks = _walk_helpers()
    folded = kind.casefold()
    return [
        block
        for block in _walk_blocks(_effective_top_blocks(lineage))
        if block.kind.casefold() == folded
    ]


def compile_opaque_deferred_module(
    lineage: Sequence[SageObject], kind: str, target_id: str
) -> list[dict[str, object]]:
    """Store every authored assignment without inventing runtime semantics.

    This is converter completion for Class C/D kinds that still need simulation
    or presentation systems. Opaque rows are never runtimeStatus=executable.
    """

    if kind in TYPED_MODULE_KINDS:
        raise ModuleContractError(
            f"{target_id}: {kind} has a typed extractor; do not opaque-compile it"
        )
    rows: list[dict[str, object]] = []
    for block in _module_blocks(lineage, kind):
        fields: dict[str, object] = {}
        for assignment in block.assignments:
            # Preserve authored key spelling; last assignment wins.
            fields[assignment.key] = {
                "authored": assignment.value,
                "sourceIni": assignment.source_virtual_path,
                "line": assignment.line,
            }
        rows.append(
            _row(
                kind,
                block,
                fields,
                runtime_status="deferred",
                extraction="opaque-authored",
                carrier=block.header_key or "",
            )
        )
    rows.sort(key=lambda row: (str(row["sourceIni"]).casefold(), int(row["line"])))
    return rows


def compile_all_opaque_deferred(
    lineage: Sequence[SageObject], target_id: str
) -> list[dict[str, object]]:
    """Single lineage walk: O(blocks) not O(kinds × blocks)."""

    _effective_top_blocks, _tokens, _walk_blocks = _walk_helpers()
    wanted = {name.casefold(): name for name in OPAQUE_DEFERRED_MODULE_KINDS}
    rows: list[dict[str, object]] = []
    for block in _walk_blocks(_effective_top_blocks(lineage)):
        canonical = wanted.get(block.kind.casefold())
        if canonical is None:
            continue
        fields: dict[str, object] = {}
        for assignment in block.assignments:
            fields[assignment.key] = {
                "authored": assignment.value,
                "sourceIni": assignment.source_virtual_path,
                "line": assignment.line,
            }
        rows.append(
            _row(
                canonical,
                block,
                fields,
                runtime_status="deferred",
                extraction="opaque-authored",
                carrier=block.header_key or "",
            )
        )
    rows.sort(
        key=lambda row: (
            str(row["module"]).casefold(),
            str(row["sourceIni"]).casefold(),
            int(row["line"]),
        )
    )
    return rows


def compile_all_module_contracts(
    lineage: Sequence[SageObject], target_id: str
) -> list[dict[str, object]]:
    """Union of typed + opaque deferred contracts, sorted deterministically."""

    overlap = OPAQUE_DEFERRED_MODULE_KINDS & TYPED_MODULE_KINDS
    if overlap:
        raise ModuleContractError(
            "opaque/typed module kind overlap: " + ", ".join(sorted(overlap))
        )

    rows: list[dict[str, object]] = []
    rows.extend(compile_attribute_modifier_upgrades(lineage, target_id))
    rows.extend(compile_geometry_upgrades(lineage, target_id))
    rows.extend(compile_inactive_bodies(lineage, target_id))
    rows.extend(compile_spawn_point_production_exits(lineage, target_id))
    rows.extend(compile_supply_center_production_exits(lineage, target_id))
    rows.extend(compile_supply_center_creates(lineage, target_id))
    rows.extend(compile_buildable_hero_list_upgrades(lineage, target_id))
    rows.extend(compile_allow_banner_spawn_upgrades(lineage, target_id))
    rows.extend(compile_spell_recharge_modifier_upgrades(lineage, target_id))
    rows.extend(compile_keep_object_die(lineage, target_id))
    rows.extend(compile_create_object_die(lineage, target_id))
    rows.extend(compile_all_opaque_deferred(lineage, target_id))
    rows.sort(
        key=lambda row: (
            str(row["module"]).casefold(),
            str(row["sourceIni"]).casefold(),
            int(row["line"]),
        )
    )
    return rows


def validate_module_contracts(rows: object, *, label: str) -> None:
    if not isinstance(rows, list):
        raise ModuleContractError(f"{label} moduleContracts must be a list")
    seen: set[tuple[str, int, str]] = set()
    for row in rows:
        if not isinstance(row, Mapping):
            raise ModuleContractError(f"{label} moduleContracts row is not an object")
        module = row.get("module")
        source = row.get("sourceIni")
        line = row.get("line")
        fields = row.get("fields")
        status = row.get("runtimeStatus")
        extraction = row.get("extraction", "typed")
        if (
            not isinstance(module, str)
            or not module
            or not isinstance(source, str)
            or not source
            or not isinstance(line, int)
            or isinstance(line, bool)
            or line <= 0
            or not isinstance(fields, Mapping)
            or status not in {"deferred", "executable"}
            or extraction not in {"typed", "opaque-authored"}
        ):
            raise ModuleContractError(f"{label} moduleContracts row schema invalid")
        # Opaque rows must never claim execution.
        if extraction == "opaque-authored" and status != "deferred":
            raise ModuleContractError(
                f"{label} opaque moduleContracts row for {module} must be deferred"
            )
        if status == "executable" and extraction != "typed":
            raise ModuleContractError(
                f"{label} executable moduleContracts row for {module} must be typed"
            )
        identity = (module.casefold(), line, source.casefold())
        if identity in seen:
            raise ModuleContractError(
                f"{label} moduleContracts duplicate {module}@{source}:{line}"
            )
        seen.add(identity)
