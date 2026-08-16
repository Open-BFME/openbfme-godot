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


# Closed allow-list: a typed importer contract is executable only after a
# concrete Godot consumer and focused runtime runner both exist.  Keep these
# receipts exact so adding a parser cannot silently overstate simulation parity.
_SIM_CONSUMER = "game/src/retail_slice/retail_slice_sim.gd"
EXECUTABLE_TYPED_MODULE_EVIDENCE: Mapping[str, tuple[str, str]] = {
    "ActivateModuleSpecialPower": (_SIM_CONSUMER, "game/tests/activate_module_special_power_runtime_runner.gd"),
    "AISpecialPowerUpdate": (_SIM_CONSUMER, "game/tests/ai_special_power_runtime_runner.gd"),
    "DominateEnemySpecialPower": (_SIM_CONSUMER, "game/tests/dominate_enemy_special_power_runtime_runner.gd"),
    "WeaponModeSpecialPowerUpdate": (_SIM_CONSUMER, "game/tests/weapon_mode_special_power_runtime_runner.gd"),
    "AIUpdateInterface": (_SIM_CONSUMER, "game/tests/ai_update_interface_runtime_runner.gd"),
    "AIGateUpdate": (_SIM_CONSUMER, "game/tests/upgrade_gate_stealth_runtime_runner.gd"),
    "AnimalAIUpdate": (_SIM_CONSUMER, "game/tests/hit_animal_threat_runtime_runner.gd"),
    "AttributeModifierAuraUpdate": (_SIM_CONSUMER, "game/tests/attribute_aura_lifetime_runtime_runner.gd"),
    "AttributeModifierUpgrade": (_SIM_CONSUMER, "game/tests/attribute_modifier_upgrade_runtime_runner.gd"),
    "AutoAbilityBehavior": (_SIM_CONSUMER, "game/tests/auto_ability_runtime_runner.gd"),
    "BannerCarrierUpdate": (_SIM_CONSUMER, "game/tests/queue_banner_respawn_body_runtime_runner.gd"),
    "BuildingBehavior": (_SIM_CONSUMER, "game/tests/production_getting_built_runtime_runner.gd"),
    "CastleUpgrade": (_SIM_CONSUMER, "game/tests/slaved_castle_upgrade_runtime_runner.gd"),
    "CreateObjectDie": (_SIM_CONSUMER, "game/tests/module_contracts_create_object_die_runner.gd"),
    "DamageFieldUpdate": (_SIM_CONSUMER, "game/tests/fear_poison_damage_spawn_runtime_runner.gd"),
    "DeletionUpdate": (_SIM_CONSUMER, "game/tests/fire_weapon_deletion_runtime_runner.gd"),
    "FakePathfindPortalBehaviour": (_SIM_CONSUMER, "game/tests/upgrade_gate_stealth_runtime_runner.gd"),
    "FireSpreadUpdate": (_SIM_CONSUMER, "game/tests/fire_spread_runtime_runner.gd"),
    "FireWeaponUpdate": (_SIM_CONSUMER, "game/tests/fire_weapon_deletion_runtime_runner.gd"),
    "FireWeaponWhenDeadBehavior": (_SIM_CONSUMER, "game/tests/fire_weapon_when_dead_runner.gd"),
    "GarrisonContain": (_SIM_CONSUMER, "game/tests/container_family_runtime_runner.gd"),
    "GateOpenAndCloseBehavior": (_SIM_CONSUMER, "game/tests/upgrade_gate_stealth_runtime_runner.gd"),
    "GettingBuiltBehavior": (_SIM_CONSUMER, "game/tests/production_getting_built_runtime_runner.gd"),
    "GiveUpgradeUpdate": (_SIM_CONSUMER, "game/tests/upgrade_gate_stealth_runtime_runner.gd"),
    "HitReactionBehavior": (_SIM_CONSUMER, "game/tests/hit_animal_threat_runtime_runner.gd"),
    "HordeAIUpdate": (_SIM_CONSUMER, "game/tests/horde_ai_update_runtime_runner.gd"),
    "HordeContain": (_SIM_CONSUMER, "game/tests/horde_contain_runtime_runner.gd"),
    "HordeGarrisonContain": (_SIM_CONSUMER, "game/tests/container_family_runtime_runner.gd"),
    "HordeTransportContain": (_SIM_CONSUMER, "game/tests/ship_transport_runtime_runner.gd"),
    "KeepObjectDie": (_SIM_CONSUMER, "game/tests/module_contracts_keep_object_die_runner.gd"),
    "LargeGroupAudioUpdate": (_SIM_CONSUMER, "game/tests/typed_audio_selector_runtime_runner.gd"),
    "LargeGroupBonusUpdate": (_SIM_CONSUMER, "game/tests/large_group_siege_queue_runtime_runner.gd"),
    "LifetimeUpdate": (_SIM_CONSUMER, "game/tests/attribute_aura_lifetime_runtime_runner.gd"),
    "ModelConditionSoundSelectorClientBehavior": (_SIM_CONSUMER, "game/tests/typed_audio_selector_runtime_runner.gd"),
    "ObjectCreationUpgrade": (_SIM_CONSUMER, "game/tests/object_factory_runtime_runner.gd"),
    "OCLUpdate": (_SIM_CONSUMER, "game/tests/object_factory_runtime_runner.gd"),
    "PhysicsBehavior": (_SIM_CONSUMER, "game/tests/physics_behavior_runtime_runner.gd"),
    "PickupStuffUpdate": (_SIM_CONSUMER, "game/tests/pickup_stuff_update_runtime_runner.gd"),
    "PoisonedBehavior": (_SIM_CONSUMER, "game/tests/fear_poison_damage_spawn_runtime_runner.gd"),
    "ProductionQueueHordeContain": (_SIM_CONSUMER, "game/tests/large_group_siege_queue_runtime_runner.gd"),
    "ProductionUpdate": (_SIM_CONSUMER, "game/tests/production_getting_built_runtime_runner.gd"),
    "RadiateFearUpdate": (_SIM_CONSUMER, "game/tests/fear_poison_damage_spawn_runtime_runner.gd"),
    "RandomSoundSelectorClientBehavior": (_SIM_CONSUMER, "game/tests/typed_audio_selector_runtime_runner.gd"),
    "ReplaceSelfUpgrade": (_SIM_CONSUMER, "game/tests/replace_self_upgrade_runtime_runner.gd"),
    "RespawnBody": (_SIM_CONSUMER, "game/tests/queue_banner_respawn_body_runtime_runner.gd"),
    "RespawnUpdate": (_SIM_CONSUMER, "game/tests/respawn_update_runtime_runner.gd"),
    "ShipSlowDeathBehavior": (_SIM_CONSUMER, "game/tests/ship_transport_runtime_runner.gd"),
    "SiegeEngineContain": (_SIM_CONSUMER, "game/tests/large_group_siege_queue_runtime_runner.gd"),
    "SlavedUpdate": (_SIM_CONSUMER, "game/tests/slaved_castle_upgrade_runtime_runner.gd"),
    "SquishCollide": (_SIM_CONSUMER, "game/tests/squish_collide_runtime_runner.gd"),
    "RebuildHoleExposeDie": (_SIM_CONSUMER, "game/tests/rebuild_hole_runtime_runner.gd"),
    "RebuildHoleBehavior": (_SIM_CONSUMER, "game/tests/rebuild_hole_runtime_runner.gd"),
    "SalvageCrateCollide": (_SIM_CONSUMER, "game/tests/salvage_crate_binary_runtime_runner.gd"),
    "SpawnUnitBehavior": (_SIM_CONSUMER, "game/tests/fear_poison_damage_spawn_runtime_runner.gd"),
    "SpecialEnemySenseUpdate": (_SIM_CONSUMER, "game/tests/stop_unleash_enemy_sense_runtime_runner.gd"),
    "StancesBehavior": (_SIM_CONSUMER, "game/tests/stances_behavior_runtime_runner.gd"),
    "StealthDetectorUpdate": (_SIM_CONSUMER, "game/tests/upgrade_gate_stealth_runtime_runner.gd"),
    "StealthUpdate": (_SIM_CONSUMER, "game/tests/spawn_stealth_runtime_runner.gd"),
    "ThreatFinderUpdate": (_SIM_CONSUMER, "game/tests/hit_animal_threat_runtime_runner.gd"),
    "TransportContain": (_SIM_CONSUMER, "game/tests/container_family_runtime_runner.gd"),
    "TunnelContain": (_SIM_CONSUMER, "game/tests/container_family_runtime_runner.gd"),
    "StopSpecialPower": (_SIM_CONSUMER, "game/tests/stop_unleash_enemy_sense_runtime_runner.gd"),
    "DeployStyleAIUpdate": (_SIM_CONSUMER, "game/tests/toggle_deploy_special_ability_runtime_runner.gd"),
    "ToggleDeploySpecialAbilityUpdate": (_SIM_CONSUMER, "game/tests/toggle_deploy_special_ability_runtime_runner.gd"),
    "UnleashSpecialPower": (_SIM_CONSUMER, "game/tests/stop_unleash_enemy_sense_runtime_runner.gd"),
}
EXECUTABLE_TYPED_MODULE_KINDS: frozenset[str] = frozenset(
    EXECUTABLE_TYPED_MODULE_EVIDENCE
)

# Some module kinds have a closed executable subset while other authored field
# shapes remain explicitly deferred.  They must not enter the kind-level set.
ROW_EXECUTABLE_TYPED_MODULE_EVIDENCE: Mapping[str, tuple[str, str]] = {
    "AutoHealBehavior": (
        _SIM_CONSUMER,
        "game/tests/auto_heal_timer_runtime_runner.gd",
    ),
    "BezierProjectileBehavior": (
        _SIM_CONSUMER,
        "game/tests/bezier_projectile_runtime_runner.gd",
    ),
    "GeometryUpgrade": (
        _SIM_CONSUMER,
        "game/tests/geometry_upgrade_runtime_runner.gd",
    ),
    "QueueProductionExitUpdate": (
        _SIM_CONSUMER,
        "game/tests/queue_production_exit_exact_runtime_runner.gd",
    ),
    "SpawnBehavior": (
        _SIM_CONSUMER,
        "game/tests/spawn_reclaim_binary_runtime_runner.gd",
    ),
    "SlowDeathBehavior": (
        _SIM_CONSUMER,
        "game/tests/slow_death_runtime_runner.gd",
    ),
    "SpecialDisguiseUpdate": (
        _SIM_CONSUMER,
        "game/tests/special_disguise_runtime_runner.gd",
    ),
    "SubObjectsUpgrade": (
        "game/src/retail_slice/retail_battalion.gd",
        "game/tests/sub_objects_upgrade_runtime_runner.gd",
    ),
    "AnimationSoundClientBehavior": (
        "game/src/retail_slice/retail_slice_audio.gd",
        "game/tests/animation_sound_client_behavior_runtime_runner.gd",
    ),
    "DamageCreationList": (
        "game/src/retail_slice/damage_creation.gd",
        "game/tests/test_damage_creation_list.gd",
    ),
    "TransitionDamageFX": (
        "game/src/retail_slice/transition_damage_fx.gd",
        "game/tests/transition_damage_fx_runtime_runner.gd",
    ),
    "FxTiming": (
        "game/src/retail_slice/fx_timing.gd",
        "game/tests/test_fx_timing_delays.gd",
    ),
}


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


_AUTHORED_NUMBER_RE = re.compile(
    r"^\s*([+-]?(?:\d+\.?\d*|\.\d+))(?:\s*//.*)?\s*$"
)


def _number_field(
    assignment: SageAssignment | None, label: str
) -> dict[str, object] | None:
    """Parse a literal SAGE scalar while retaining its exact authored form."""

    if assignment is None:
        return None
    match = _AUTHORED_NUMBER_RE.fullmatch(assignment.value)
    if match is None:
        raise ModuleContractError(
            f"{label} must be a numeric literal: {assignment.value!r}"
        )
    token = match.group(1)
    value: int | float
    if re.fullmatch(r"[+-]?\d+", token):
        value = int(token)
    else:
        value = float(token)
    return {
        "authored": assignment.value,
        "value": value,
        "sourceIni": assignment.source_virtual_path,
        "line": assignment.line,
    }


def _milliseconds_field(
    assignment: SageAssignment | None, label: str
) -> dict[str, object] | None:
    """Parse the non-negative integer millisecond shape authored by retail."""

    if assignment is None:
        return None
    match = re.fullmatch(r"\s*(\d+)(?:\s*//.*)?\s*", assignment.value)
    if match is None:
        raise ModuleContractError(
            f"{label} must be non-negative integer milliseconds: {assignment.value!r}"
        )
    milliseconds = int(match.group(1))
    if milliseconds < 0:
        raise ModuleContractError(
            f"{label} must be non-negative integer milliseconds: {assignment.value!r}"
        )
    return {
        "authored": assignment.value,
        "milliseconds": milliseconds,
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
    runtime_status: str | None = None,
    extraction: str = "typed",
    carrier: str = "",
) -> dict[str, object]:
    status = runtime_status
    if status is None:
        status = (
            "executable"
            if extraction == "typed" and module in EXECUTABLE_TYPED_MODULE_KINDS
            else "deferred"
        )
    return {
        "module": module,
        "fields": fields,
        "runtimeStatus": status,
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


def _geometry_upgrade_row_has_closed_runtime(fields: Mapping[str, object]) -> bool:
    """True for the visibility-only shape covered by the Godot consumer.

    At least one visible-state operation is required. Mesh replacement and
    authored custom-animation fields deliberately keep the row deferred.
    """

    triggers = fields.get("TriggeredBy")
    if not isinstance(triggers, Mapping) or not isinstance(triggers.get("value"), list):
        return False
    if not triggers["value"] or "deferredFields" in fields:
        return False
    return any(
        isinstance(fields.get(key), Mapping)
        and isinstance(fields[key].get("value"), list)
        and bool(fields[key]["value"])
        for key in ("ShowGeometry", "HideGeometry")
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
        # The common retail shape is fully consumed by the authoritative
        # upgrade ledger and the structure visibility presenter. Wall/ramp
        # meshes and CustomAnimAndDuration remain row-deferred because their
        # collision/animation semantics are not covered by that consumer.
        rows.append(
            _row(
                "GeometryUpgrade",
                block,
                fields,
                runtime_status=(
                    "executable"
                    if _geometry_upgrade_row_has_closed_runtime(fields)
                    else "deferred"
                ),
            )
        )
    rows.sort(key=lambda row: (str(row["sourceIni"]).casefold(), int(row["line"])))
    return rows


_SUB_OBJECTS_UPGRADE_SUPPORTED = frozenset(
    {
        "triggeredby",
        "showsubobjects",
        "hidesubobjects",
        "conflictswith",
        "requiresalltriggers",
        "upgradetexture",
        "recolorhouse",
        "excludesubobjects",
        "skipfadeoncreate",
        "hidesubobjectsonremove",
        "fadetimeinseconds",
        "customanimandduration",
    }
)
_SUB_OBJECTS_UPGRADE_DEFERRED = frozenset(
    {
        "upgradetexture",
        "recolorhouse",
        "excludesubobjects",
        "skipfadeoncreate",
        "hidesubobjectsonremove",
        "fadetimeinseconds",
        "customanimandduration",
    }
)


def _sub_objects_upgrade_row_has_closed_runtime(fields: Mapping[str, object]) -> bool:
    """True for TriggeredBy + Show/Hide tokens covered by the battalion consumer.

    Texture swaps, house recolor, fade, and HideSubObjectsOnRemove stay deferred.
    """

    triggers = fields.get("TriggeredBy")
    if not isinstance(triggers, Mapping) or not isinstance(triggers.get("value"), list):
        return False
    if not triggers["value"] or "deferredFields" in fields:
        return False
    return any(
        isinstance(fields.get(key), Mapping)
        and isinstance(fields[key].get("value"), list)
        and bool(fields[key]["value"])
        for key in ("ShowSubObjects", "HideSubObjects")
    )


def compile_sub_objects_upgrades(
    lineage: Sequence[SageObject], target_id: str
) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for block in _behavior_blocks(lineage, "SubObjectsUpgrade"):
        authored = {a.key.casefold() for a in block.assignments}
        unknown = authored - _SUB_OBJECTS_UPGRADE_SUPPORTED
        if unknown:
            raise ModuleContractError(
                f"{target_id} SubObjectsUpgrade unsupported fields: "
                + ", ".join(sorted(unknown))
            )
        amap = _assignment_map(block)
        fields: dict[str, object] = {}
        for key, compiler in (
            ("TriggeredBy", _token_list_field),
            ("ShowSubObjects", _token_list_field),
            ("HideSubObjects", _token_list_field),
            ("ConflictsWith", _token_list_field),
        ):
            value = compiler(amap.get(key.casefold()))
            if value is not None:
                fields[key] = value
        requires = _yes_no_field(
            amap.get("requiresalltriggers"),
            f"{target_id} SubObjectsUpgrade RequiresAllTriggers",
        )
        if requires is not None:
            fields["RequiresAllTriggers"] = requires
        deferred: list[dict[str, object]] = []
        for name in sorted(_SUB_OBJECTS_UPGRADE_DEFERRED):
            assignment = amap.get(name)
            if assignment is not None:
                deferred.append(
                    {
                        "name": assignment.key,
                        "authored": assignment.value,
                        "sourceIni": assignment.source_virtual_path,
                        "line": assignment.line,
                        "reason": "texture-swap-fade-or-hide-on-remove-without-runtime-oracle",
                    }
                )
        if deferred:
            fields["deferredFields"] = deferred
        if "TriggeredBy" not in fields:
            raise ModuleContractError(
                f"{target_id} SubObjectsUpgrade requires TriggeredBy"
            )
        rows.append(
            _row(
                "SubObjectsUpgrade",
                block,
                fields,
                runtime_status=(
                    "executable"
                    if _sub_objects_upgrade_row_has_closed_runtime(fields)
                    else "deferred"
                ),
            )
        )
    rows.sort(key=lambda row: (str(row["sourceIni"]).casefold(), int(row["line"])))
    return rows


_TRANSITION_DAMAGE_FX_KEY_RE = re.compile(
    r"^(?P<stage>Damaged|ReallyDamaged|Rubble)"
    r"(?P<kind>FXList|ParticleSystem|OCL)(?P<index>\d+)$",
    re.IGNORECASE,
)
_TRANSITION_DAMAGE_FX_LIST_RE = re.compile(
    r"Loc:\s*X:([+-]?(?:\d+\.?\d*|\.\d+))\s+Y:([+-]?(?:\d+\.?\d*|\.\d+))"
    r"\s+Z:([+-]?(?:\d+\.?\d*|\.\d+))\s+FXList:(\S+)",
    re.IGNORECASE,
)
_TRANSITION_DAMAGE_FX_PSYS_RE = re.compile(
    r"Bone:(\S+)\s+RandomBone:(Yes|No)\s+PSys:(\S+)",
    re.IGNORECASE,
)


def _strip_ini_line_comment(raw: str) -> str:
    text = raw.strip()
    for marker in ("//", ";,;", ";"):
        index = text.find(marker)
        if index >= 0:
            text = text[:index].strip()
    return text


def _transition_damage_fx_row_has_closed_runtime(fields: Mapping[str, object]) -> bool:
    effects = fields.get("effects")
    if not isinstance(effects, list):
        return False
    return any(
        isinstance(row, Mapping) and row.get("kind") in {"FXList", "ParticleSystem"}
        for row in effects
    )


def compile_transition_damage_fx(
    lineage: Sequence[SageObject], target_id: str
) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for block in _behavior_blocks(lineage, "TransitionDamageFX"):
        effects: list[dict[str, object]] = []
        deferred: list[dict[str, object]] = []
        for assignment in block.assignments:
            key_match = _TRANSITION_DAMAGE_FX_KEY_RE.fullmatch(assignment.key)
            if key_match is None:
                raise ModuleContractError(
                    f"{target_id} TransitionDamageFX unsupported field: {assignment.key}"
                )
            stage = {
                "damaged": "Damaged",
                "reallydamaged": "ReallyDamaged",
                "rubble": "Rubble",
            }[key_match.group("stage").casefold()]
            kind = {
                "fxlist": "FXList",
                "particlesystem": "ParticleSystem",
                "ocl": "OCL",
            }[key_match.group("kind").casefold()]
            raw = _strip_ini_line_comment(assignment.value)
            if kind == "OCL":
                deferred.append(
                    {
                        "name": assignment.key,
                        "authored": assignment.value,
                        "sourceIni": assignment.source_virtual_path,
                        "line": assignment.line,
                        "reason": "ocl-debris-spawn-without-runtime-oracle",
                    }
                )
                continue
            if kind == "FXList":
                parsed = _TRANSITION_DAMAGE_FX_LIST_RE.fullmatch(raw)
                if parsed is None:
                    deferred.append(
                        {
                            "name": assignment.key,
                            "authored": assignment.value,
                            "sourceIni": assignment.source_virtual_path,
                            "line": assignment.line,
                            "reason": "unparsed-transition-fxlist",
                        }
                    )
                    continue
                effects.append(
                    {
                        "stage": stage,
                        "kind": "FXList",
                        "index": int(key_match.group("index")),
                        "fxList": parsed.group(4),
                        "loc": {
                            "x": float(parsed.group(1)),
                            "y": float(parsed.group(2)),
                            "z": float(parsed.group(3)),
                        },
                        "authored": assignment.value,
                        "sourceIni": assignment.source_virtual_path,
                        "line": assignment.line,
                    }
                )
                continue
            parsed = _TRANSITION_DAMAGE_FX_PSYS_RE.fullmatch(raw)
            if parsed is None:
                deferred.append(
                    {
                        "name": assignment.key,
                        "authored": assignment.value,
                        "sourceIni": assignment.source_virtual_path,
                        "line": assignment.line,
                        "reason": "unparsed-transition-particle-system",
                    }
                )
                continue
            effects.append(
                {
                    "stage": stage,
                    "kind": "ParticleSystem",
                    "index": int(key_match.group("index")),
                    "particleSystem": parsed.group(3),
                    "bone": parsed.group(1),
                    "randomBone": parsed.group(2).casefold() == "yes",
                    "authored": assignment.value,
                    "sourceIni": assignment.source_virtual_path,
                    "line": assignment.line,
                }
            )
        fields: dict[str, object] = {"effects": effects}
        if deferred:
            fields["deferredFields"] = deferred
        rows.append(
            _row(
                "TransitionDamageFX",
                block,
                fields,
                runtime_status=(
                    "executable"
                    if _transition_damage_fx_row_has_closed_runtime(fields)
                    else "deferred"
                ),
            )
        )
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


# --- Class C typed data contracts -----------------------------------------

_PHYSICS_BEHAVIOR_FIELDS = frozenset(
    {
        "gravitymult",
        "allowbouncing",
        "orienttoflightpath",
        "killwhenrestingonground",
        "shockstunnedtimelow",
        "shockstunnedtimehigh",
        "shockstandingtime",
        "firstheight",
        "secondheight",
    }
)


def compile_physics_behaviors(
    lineage: Sequence[SageObject], target_id: str
) -> list[dict[str, object]]:
    """Compile the complete measured retail ``PhysicsBehavior`` field shape.

    This closes opaque importer handling only.  The shock timers and motion
    policy remain deferred until the deterministic simulation consumes them.
    """

    rows: list[dict[str, object]] = []
    for block in _behavior_blocks(lineage, "PhysicsBehavior"):
        amap = _assignment_map(block)
        unknown = set(amap) - _PHYSICS_BEHAVIOR_FIELDS
        if unknown:
            raise ModuleContractError(
                f"{target_id} PhysicsBehavior unsupported fields: "
                + ", ".join(sorted(unknown))
            )
        fields: dict[str, object] = {}
        for key in ("GravityMult", "FirstHeight", "SecondHeight"):
            value = _number_field(
                amap.get(key.casefold()), f"{target_id} PhysicsBehavior {key}"
            )
            if value is not None:
                fields[key] = value
        for key in (
            "AllowBouncing",
            "OrientToFlightPath",
            "KillWhenRestingOnGround",
        ):
            value = _yes_no_field(
                amap.get(key.casefold()), f"{target_id} PhysicsBehavior {key}"
            )
            if value is not None:
                fields[key] = value
        for key in (
            "ShockStunnedTimeLow",
            "ShockStunnedTimeHigh",
            "ShockStandingTime",
        ):
            value = _milliseconds_field(
                amap.get(key.casefold()), f"{target_id} PhysicsBehavior {key}"
            )
            if value is not None:
                fields[key] = value
        rows.append(_row("PhysicsBehavior", block, fields))
    rows.sort(key=lambda row: (str(row["sourceIni"]).casefold(), int(row["line"])))
    return rows


_BEZIER_PROJECTILE_FIELDS = frozenset(
    {
        "firstheight",
        "secondheight",
        "firstpercentindent",
        "secondpercentindent",
        "tumblerandomly",
        "crushstyle",
        "dieonimpact",
        "bouncecount",
        "bouncedistance",
        "bouncefirstheight",
        "bouncesecondheight",
        "bouncefirstpercentindent",
        "bouncesecondpercentindent",
        "groundhitfx",
        "groundhitweapon",
        "groundbouncefx",
        "groundbounceweapon",
        "detonatecallskill",
        "flightpathadjustdistpersecond",
        "curveflattenmindist",
        "invisibleframes",
        "prelandingstatetime",
        "prelandingemotion",
        "prelandingemotionradius",
        "fadeintime",
        "ignoreterrainheight",
        "firstpercentheight",
        "secondpercentheight",
        "finalstucktime",
        "orienttoflightpath",
        "garrisonhitkillrequiredkindof",
        "garrisonhitkillforbiddenkindof",
        "garrisonhitkillcount",
        "garrisonhitkillfx",
        "prelandingemotionaffectsallies",
    }
)
_BEZIER_REQUIRED_TRAJECTORY_FIELDS = (
    "FirstHeight",
    "SecondHeight",
    "FirstPercentIndent",
    "SecondPercentIndent",
)
_BEZIER_NUMBER_FIELDS = (
    "FirstHeight",
    "SecondHeight",
    "BounceDistance",
    "BounceFirstHeight",
    "BounceSecondHeight",
    "FlightPathAdjustDistPerSecond",
    "CurveFlattenMinDist",
    "PreLandingEmotionRadius",
)
_BEZIER_PERCENT_FIELDS = (
    "FirstPercentIndent",
    "SecondPercentIndent",
    "BounceFirstPercentIndent",
    "BounceSecondPercentIndent",
    "FirstPercentHeight",
    "SecondPercentHeight",
)
_BEZIER_BOOL_FIELDS = (
    "TumbleRandomly",
    "CrushStyle",
    "DieOnImpact",
    "DetonateCallsKill",
    "IgnoreTerrainHeight",
    "OrientToFlightPath",
    "PreLandingEmotionAffectsAllies",
)
_BEZIER_INTEGER_FIELDS = (
    "BounceCount",
    "InvisibleFrames",
    "PreLandingStateTime",
    "FadeInTime",
    "FinalStuckTime",
    "GarrisonHitKillCount",
)
_BEZIER_IDENTIFIER_FIELDS = (
    "GroundHitFX",
    "GroundHitWeapon",
    "GroundBounceFX",
    "GroundBounceWeapon",
    "PreLandingEmotion",
    "GarrisonHitKillFX",
)
_BEZIER_TOKEN_FIELDS = (
    "GarrisonHitKillRequiredKindOf",
    "GarrisonHitKillForbiddenKindOf",
)

_BEZIER_COMMON_LANDING_FIELDS = frozenset(
    {
        "BounceCount", "BounceDistance", "BounceFirstHeight",
        "BounceFirstPercentIndent", "BounceSecondHeight",
        "BounceSecondPercentIndent", "CrushStyle", "DieOnImpact",
        "FirstHeight", "FirstPercentIndent", "GroundBounceFX", "GroundHitFX",
        "SecondHeight", "SecondPercentIndent", "TumbleRandomly",
    }
)


def _bezier_common_landing_shape(fields: Mapping[str, object]) -> bool:
    return set(fields) == _BEZIER_COMMON_LANDING_FIELDS


def _bezier_execution_blockers(fields: Mapping[str, object]) -> list[str]:
    if _bezier_common_landing_shape(fields):
        return []
    blockers = {"impact"}  # Even an omitted impact policy has unproven SAGE defaults.
    if any(key not in fields for key in _BEZIER_REQUIRED_TRAJECTORY_FIELDS):
        blockers.add("trajectory")
    if any(key.startswith("Bounce") or key.startswith("GroundBounce") for key in fields):
        blockers.add("bounce")
    if any(key in fields for key in ("CrushStyle", "DieOnImpact")):
        blockers.add("impact")
    if any(key in fields for key in ("DieOnImpact", "DetonateCallsKill")):
        blockers.add("kill")
    if any(key.endswith("Weapon") for key in fields):
        blockers.add("weapon")
    if any(key.endswith("FX") for key in fields):
        blockers.add("fx")
    if any(key.startswith("PreLanding") for key in fields):
        blockers.add("prelanding")
    if "IgnoreTerrainHeight" in fields:
        blockers.add("terrain")
    if any(
        key in fields
        for key in (
            "TumbleRandomly",
            "InvisibleFrames",
            "FadeInTime",
            "FirstPercentHeight",
            "SecondPercentHeight",
            "FinalStuckTime",
            "OrientToFlightPath",
            "FlightPathAdjustDistPerSecond",
            "CurveFlattenMinDist",
        )
    ):
        blockers.add("presentation")
    if any(key.startswith("GarrisonHit") for key in fields):
        blockers.add("garrison")
    return sorted(blockers)


def _bezier_effect_graph(fields: Mapping[str, object]) -> dict[str, object]:
    if all(key in fields for key in _BEZIER_REQUIRED_TRAJECTORY_FIELDS):
        trajectory: dict[str, object] = {
            "kind": "cubic-bezier-envelope",
            "runtimeStatus": "executable",
            "firstHeight": float(fields["FirstHeight"]["value"]),
            "secondHeight": float(fields["SecondHeight"]["value"]),
            "firstIndentRatio": float(fields["FirstPercentIndent"]["ratio"]),
            "secondIndentRatio": float(fields["SecondPercentIndent"]["ratio"]),
            "progressAuthority": "external-authored-projectile-flight",
        }
    else:
        trajectory = {
            "kind": "cubic-bezier-envelope",
            "runtimeStatus": "deferred",
            "authoredControlFields": [
                key for key in _BEZIER_REQUIRED_TRAJECTORY_FIELDS if key in fields
            ],
            "deferredReason": "incomplete-authored-cubic-controls",
        }
    common_landing = _bezier_common_landing_shape(fields)
    graph: dict[str, object] = {
        "kind": "bezier-projectile",
        "trajectory": trajectory,
        "executionEligibility": {
            "runtimeStatus": "executable" if common_landing else "deferred",
            "blockers": _bezier_execution_blockers(fields),
        },
    }
    if common_landing:
        graph["arrival"] = {
            "kind": "authored-ground-impact-bounce",
            "runtimeStatus": "executable",
            "crushStyle": bool(fields["CrushStyle"]["value"]),
            "dieOnImpact": bool(fields["DieOnImpact"]["value"]),
            "tumbleRandomly": bool(fields["TumbleRandomly"]["value"]),
            "bounceCount": int(fields["BounceCount"]["value"]),
            "bounceDistance": float(fields["BounceDistance"]["value"]),
            "bounceFirstHeight": float(fields["BounceFirstHeight"]["value"]),
            "bounceSecondHeight": float(fields["BounceSecondHeight"]["value"]),
            "bounceFirstIndentRatio": float(fields["BounceFirstPercentIndent"]["ratio"]),
            "bounceSecondIndentRatio": float(fields["BounceSecondPercentIndent"]["ratio"]),
            "groundHitFxId": str(fields["GroundHitFX"]["value"]),
            "groundBounceFxId": str(fields["GroundBounceFX"]["value"]),
            "terminalPolicy": (
                "remove-on-final-impact"
                if bool(fields["DieOnImpact"]["value"])
                else "land-and-clear-projectile-state"
            ),
        }
    return graph


def compile_bezier_projectile_behaviors(
    lineage: Sequence[SageObject], target_id: str
) -> list[dict[str, object]]:
    """Type the complete retail grammar while executing only the proven arc.

    Flight timing and all arrival effects remain owned by an external authored
    projectile launch.  This contract therefore never promotes the containing
    module row to executable merely because its cubic envelope is available.
    """

    rows: list[dict[str, object]] = []
    for block in _behavior_blocks(lineage, "BezierProjectileBehavior"):
        if block.blocks:
            raise ModuleContractError(
                f"{target_id} BezierProjectileBehavior has nested blocks"
            )
        grouped: dict[str, list[SageAssignment]] = {}
        for assignment in block.assignments:
            grouped.setdefault(assignment.key.casefold(), []).append(assignment)
        unknown = set(grouped) - _BEZIER_PROJECTILE_FIELDS
        if unknown:
            raise ModuleContractError(
                f"{target_id} BezierProjectileBehavior unsupported fields: "
                + ", ".join(sorted(unknown))
            )
        duplicates = sorted(key for key, values in grouped.items() if len(values) != 1)
        if duplicates:
            raise ModuleContractError(
                f"{target_id} BezierProjectileBehavior duplicate fields: "
                + ", ".join(duplicates)
            )
        amap = {key: values[0] for key, values in grouped.items()}
        fields: dict[str, object] = {}
        for key in _BEZIER_NUMBER_FIELDS:
            value = _number_field(amap.get(key.casefold()), f"{target_id} BezierProjectileBehavior {key}")
            if value is not None:
                fields[key] = value
        for key in _BEZIER_PERCENT_FIELDS:
            value = _percent_assignment_field(amap.get(key.casefold()), f"{target_id} BezierProjectileBehavior {key}")
            if value is not None:
                fields[key] = value
        for key in _BEZIER_BOOL_FIELDS:
            value = _yes_no_field(amap.get(key.casefold()), f"{target_id} BezierProjectileBehavior {key}")
            if value is not None:
                fields[key] = value
        for key in _BEZIER_INTEGER_FIELDS:
            value = _integer_assignment_field(amap.get(key.casefold()), f"{target_id} BezierProjectileBehavior {key}", minimum=0)
            if value is not None:
                fields[key] = value
        for key in _BEZIER_IDENTIFIER_FIELDS:
            assignment = amap.get(key.casefold())
            if assignment is not None:
                fields[key] = _required_identifier_field(
                    assignment, f"{target_id} BezierProjectileBehavior {key}"
                )
        for key in _BEZIER_TOKEN_FIELDS:
            value = _token_list_field(amap.get(key.casefold()))
            if value is not None:
                if not value["value"]:
                    raise ModuleContractError(
                        f"{target_id} BezierProjectileBehavior {key} requires tokens"
                    )
                fields[key] = value
        row = _row(
            "BezierProjectileBehavior",
            block,
            fields,
            runtime_status=(
                "executable" if _bezier_common_landing_shape(fields) else "deferred"
            ),
        )
        row["effectGraph"] = _bezier_effect_graph(fields)
        rows.append(row)
    rows.sort(key=lambda row: (str(row["sourceIni"]).casefold(), int(row["line"])))
    return rows


_FIRE_WEAPON_WHEN_DEAD_FIELDS = frozenset(
    {
        "deathtypes",
        "requiredstatus",
        "exemptstatus",
        "startsactive",
        "activeduringconstruction",
        "delaytime",
        "deathweapon",
        "weaponoffset",
    }
)


def compile_fire_weapon_when_dead_behaviors(
    lineage: Sequence[SageObject], target_id: str
) -> list[dict[str, object]]:
    """Compile the complete measured delayed death-weapon contract."""

    rows: list[dict[str, object]] = []
    for block in _behavior_blocks(lineage, "FireWeaponWhenDeadBehavior"):
        amap = _assignment_map(block)
        unknown = set(amap) - _FIRE_WEAPON_WHEN_DEAD_FIELDS
        if unknown:
            raise ModuleContractError(
                f"{target_id} FireWeaponWhenDeadBehavior unsupported fields: "
                + ", ".join(sorted(unknown))
            )
        fields = _die_mux_death_types(block, "FireWeaponWhenDeadBehavior")
        for key in ("RequiredStatus", "ExemptStatus"):
            value = _token_list_field(amap.get(key.casefold()))
            if value is not None:
                fields[key] = value
        for key in ("StartsActive", "ActiveDuringConstruction"):
            value = _yes_no_field(
                amap.get(key.casefold()),
                f"{target_id} FireWeaponWhenDeadBehavior {key}",
            )
            if value is not None:
                fields[key] = value
        delay = _milliseconds_field(
            amap.get("delaytime"),
            f"{target_id} FireWeaponWhenDeadBehavior DelayTime",
        )
        if delay is not None:
            fields["DelayTime"] = delay
        weapon = _string_field(amap.get("deathweapon"))
        if weapon is None:
            raise ModuleContractError(
                f"{target_id} FireWeaponWhenDeadBehavior requires DeathWeapon"
            )
        fields["DeathWeapon"] = weapon
        offset = _coord_field(
            amap.get("weaponoffset"),
            f"{target_id} FireWeaponWhenDeadBehavior WeaponOffset",
        )
        if offset is not None:
            fields["WeaponOffset"] = offset
        rows.append(_row("FireWeaponWhenDeadBehavior", block, fields))
    rows.sort(key=lambda row: (str(row["sourceIni"]).casefold(), int(row["line"])))
    return rows


_SHIP_SLOW_DEATH_FIELDS = frozenset(
    {"deathtypes", "sinkdelay", "sinkrate", "destructiondelay", "sound"}
)


def compile_ship_slow_death_behaviors(
    lineage: Sequence[SageObject], target_id: str
) -> list[dict[str, object]]:
    """Compile every measured ship sinking timer and presentation field."""

    rows: list[dict[str, object]] = []
    for block in _behavior_blocks(lineage, "ShipSlowDeathBehavior"):
        amap = _assignment_map(block)
        unknown = set(amap) - _SHIP_SLOW_DEATH_FIELDS
        if unknown:
            raise ModuleContractError(
                f"{target_id} ShipSlowDeathBehavior unsupported fields: "
                + ", ".join(sorted(unknown))
            )
        fields = _die_mux_death_types(block, "ShipSlowDeathBehavior")
        for key in ("SinkDelay", "DestructionDelay"):
            value = _milliseconds_field(
                amap.get(key.casefold()),
                f"{target_id} ShipSlowDeathBehavior {key}",
            )
            if value is None:
                raise ModuleContractError(
                    f"{target_id} ShipSlowDeathBehavior requires {key}"
                )
            fields[key] = value
        sink_rate = _number_field(
            amap.get("sinkrate"), f"{target_id} ShipSlowDeathBehavior SinkRate"
        )
        if sink_rate is None or float(sink_rate["value"]) < 0:
            raise ModuleContractError(
                f"{target_id} ShipSlowDeathBehavior requires non-negative SinkRate"
            )
        fields["SinkRate"] = sink_rate
        sound = amap.get("sound")
        if sound is not None:
            tokens = sound.value.split()
            if len(tokens) != 2 or tokens[0].upper() != "INITIAL":
                raise ModuleContractError(
                    f"{target_id} ShipSlowDeathBehavior Sound must be "
                    "INITIAL <event>"
                )
            fields["Sound"] = {
                "phase": "INITIAL",
                "event": tokens[1],
                "authored": sound.value,
                "sourceIni": sound.source_virtual_path,
                "line": sound.line,
            }
        rows.append(_row("ShipSlowDeathBehavior", block, fields))
    rows.sort(key=lambda row: (str(row["sourceIni"]).casefold(), int(row["line"])))
    return rows


_SLOW_DEATH_FIELDS = frozenset(
    {
        "deathtypes",
        "deathflags",
        "decaybegintime",
        "destructiondelay",
        "donotrandomizemidpoint",
        "fadedelay",
        "fadetime",
        "fx",
        "ocl",
        "probabilitymodifier",
        "shadowwhendead",
        "sinkdelay",
        "sinkrate",
        "sound",
        "weapon",
    }
)
_SLOW_DEATH_REPEATED_FIELDS = frozenset({"fx", "ocl"})
_SLOW_DEATH_PHASES = frozenset({"INITIAL", "MIDPOINT", "FINAL", "HIT_GROUND"})
_SLOW_DEATH_PRESENTATION_FIELDS = frozenset(
    {"DeathFlags", "DecayBeginTime", "FadeDelay", "FadeTime", "FX", "ShadowWhenDead", "Sound"}
)


def _slow_death_types(block: SageBlock) -> dict[str, object]:
    """Parse the retail DieMux bit-mask expression without normalizing it away."""

    _effective_top_blocks, tokens_fn, _walk_blocks = _walk_helpers()
    assignments = [
        assignment
        for assignment in block.assignments
        if assignment.key.casefold() == "deathtypes"
    ]
    if len(assignments) > 1:
        raise ModuleContractError("SlowDeathBehavior authors duplicate DeathTypes")
    if not assignments:
        return {
            "deathTypes": "ALL",
            "includedDeathTypes": [],
            "excludedDeathTypes": [],
        }
    tokens = [token.upper() for token in tokens_fn(assignments[0].value)]
    if not tokens or tokens[0] not in {"ALL", "NONE"}:
        raise ModuleContractError(
            "SlowDeathBehavior authors unsupported DeathTypes: " + " ".join(tokens)
        )
    included: list[str] = []
    excluded: list[str] = []
    for token in tokens[1:]:
        if (
            len(token) < 2
            or token[0] not in {"+", "-"}
            or re.fullmatch(r"[A-Z_][A-Z0-9_]*", token[1:]) is None
        ):
            raise ModuleContractError(
                "SlowDeathBehavior authors unsupported DeathTypes: " + " ".join(tokens)
            )
        (included if token[0] == "+" else excluded).append(token[1:])
    assignment = assignments[0]
    return {
        "deathTypes": tokens[0],
        "includedDeathTypes": included,
        "excludedDeathTypes": excluded,
        "deathTypesAuthored": {
            "authored": assignment.value,
            "sourceIni": assignment.source_virtual_path,
            "line": assignment.line,
        },
    }


def _slow_death_phase_rows(
    assignments: Sequence[SageAssignment],
    *,
    target_id: str,
    field: str,
) -> list[dict[str, object]]:
    """Preserve each authored phase row and its reference order exactly."""

    _effective_top_blocks, tokens_fn, _walk_blocks = _walk_helpers()
    rows: list[dict[str, object]] = []
    for assignment in assignments:
        tokens = list(tokens_fn(assignment.value))
        if len(tokens) < 2 or tokens[0].upper() not in _SLOW_DEATH_PHASES:
            raise ModuleContractError(
                f"{target_id} SlowDeathBehavior {field} must be PHASE plus reference(s)"
            )
        references = tokens[1:]
        if any(
            re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", reference) is None
            for reference in references
        ):
            raise ModuleContractError(
                f"{target_id} SlowDeathBehavior {field} contains malformed reference"
            )
        receipt: dict[str, object] = {
            "phase": tokens[0].upper(),
            "references": references,
            "authored": assignment.value,
            "sourceIni": assignment.source_virtual_path,
            "line": assignment.line,
            "runtimeStatus": "deferred",
        }
        if field in {"FX", "Sound"}:
            receipt["deferredReason"] = "presentation-system-not-bound"
        elif field == "OCL":
            receipt["deferredReason"] = "object-creation-side-effect-not-bound"
        else:
            receipt["deferredReason"] = "weapon-side-effect-not-bound"
        rows.append(receipt)
    return rows


def _slow_death_milliseconds_field(
    assignment: SageAssignment | None,
    *,
    label: str,
    numeric_defines: Mapping[str, int | float] | None,
    numeric_define_provenance: Mapping[str, Mapping[str, object]] | None,
) -> dict[str, object] | None:
    if assignment is None:
        return None
    expression = assignment.value.strip()
    if re.fullmatch(r"\d+", expression):
        return _milliseconds_field(assignment, label)
    if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", expression) is None:
        raise ModuleContractError(
            f"{label} must be non-negative integer milliseconds or a resolved define"
        )
    key = expression.casefold()
    value = None if numeric_defines is None else numeric_defines.get(key)
    provenance = (
        None
        if numeric_define_provenance is None
        else numeric_define_provenance.get(key)
    )
    if value is None or provenance is None:
        raise ModuleContractError(f"{label} define is unresolved: {expression}")
    numeric = float(value)
    if numeric < 0 or not numeric.is_integer() or numeric > 4_294_967_295:
        raise ModuleContractError(
            f"{label} define must resolve to unsigned integer milliseconds: {expression}"
        )
    provenance_value = provenance.get("value")
    if (
        str(provenance.get("defineId", "")).casefold() != key
        or not isinstance(provenance.get("sourceIni"), str)
        or not provenance.get("sourceIni")
        or isinstance(provenance.get("line"), bool)
        or not isinstance(provenance.get("line"), int)
        or int(provenance["line"]) <= 0
        or not isinstance(provenance.get("authoredValue"), str)
        or not provenance.get("authoredValue")
        or isinstance(provenance_value, bool)
        or not isinstance(provenance_value, (int, float))
        or float(provenance_value) != numeric
    ):
        raise ModuleContractError(f"{label} define provenance is invalid: {expression}")
    return {
        "authored": assignment.value,
        "expression": expression,
        "milliseconds": int(numeric),
        "defineProvenance": dict(provenance),
        "sourceIni": assignment.source_virtual_path,
        "line": assignment.line,
    }


def compile_slow_death_behaviors(
    lineage: Sequence[SageObject],
    target_id: str,
    *,
    numeric_defines: Mapping[str, int | float] | None = None,
    numeric_define_provenance: Mapping[str, Mapping[str, object]] | None = None,
) -> list[dict[str, object]]:
    """Compile the complete canonical BFME2/RotWK SlowDeathBehavior grammar.

    This is a typed importer/effect-graph contract, not a runtime-parity claim.
    EA source closes selection, phase order, timing, sinking, FX/OCL/Weapon
    vector choice, and destruction.  BFME2 additions whose exact runtime hook
    is presentation-only or still binary-ambiguous remain explicit receipts.
    """

    rows: list[dict[str, object]] = []
    for block in _behavior_blocks(lineage, "SlowDeathBehavior"):
        if block.blocks:
            raise ModuleContractError(
                f"{target_id} SlowDeathBehavior authors unsupported nested blocks"
            )
        grouped: dict[str, list[SageAssignment]] = {}
        for assignment in block.assignments:
            grouped.setdefault(assignment.key.casefold(), []).append(assignment)
        unknown = set(grouped) - _SLOW_DEATH_FIELDS
        if unknown:
            raise ModuleContractError(
                f"{target_id} SlowDeathBehavior unsupported fields: "
                + ", ".join(sorted(unknown))
            )
        duplicates = sorted(
            key
            for key, assignments in grouped.items()
            if len(assignments) > 1 and key not in _SLOW_DEATH_REPEATED_FIELDS
        )
        if duplicates:
            raise ModuleContractError(
                f"{target_id} SlowDeathBehavior duplicate scalar fields: "
                + ", ".join(duplicates)
            )
        amap = {key: assignments[0] for key, assignments in grouped.items()}
        fields = _slow_death_types(block)

        for key in (
            "SinkDelay",
            "DestructionDelay",
            "FadeDelay",
            "FadeTime",
            "DecayBeginTime",
        ):
            assignment = amap.get(key.casefold())
            value = _slow_death_milliseconds_field(
                assignment,
                label=f"{target_id} SlowDeathBehavior {key}",
                numeric_defines=numeric_defines,
                numeric_define_provenance=numeric_define_provenance,
            )
            if value is not None:
                if key in _SLOW_DEATH_PRESENTATION_FIELDS:
                    value["runtimeStatus"] = "deferred"
                    value["deferredReason"] = "presentation-system-not-bound"
                fields[key] = value

        sink_rate = _number_field(
            amap.get("sinkrate"), f"{target_id} SlowDeathBehavior SinkRate"
        )
        if sink_rate is not None:
            fields["SinkRate"] = sink_rate
        probability = _integer_assignment_field(
            amap.get("probabilitymodifier"),
            f"{target_id} SlowDeathBehavior ProbabilityModifier",
            minimum=0,
        )
        if probability is not None:
            fields["ProbabilityModifier"] = probability

        death_flags = _token_list_field(amap.get("deathflags"))
        if death_flags is not None:
            if not death_flags["value"] or any(
                re.fullmatch(r"DEATH_[1-4]", str(flag).upper()) is None
                for flag in death_flags["value"]
            ):
                raise ModuleContractError(
                    f"{target_id} SlowDeathBehavior DeathFlags malformed"
                )
            death_flags["value"] = [str(flag).upper() for flag in death_flags["value"]]
            death_flags["runtimeStatus"] = "deferred"
            death_flags["deferredReason"] = "model-condition-presentation-not-bound"
            fields["DeathFlags"] = death_flags

        for key in ("ShadowWhenDead", "DoNotRandomizeMidpoint"):
            value = _yes_no_field(
                amap.get(key.casefold()), f"{target_id} SlowDeathBehavior {key}"
            )
            if value is not None:
                value["runtimeStatus"] = "deferred"
                value["deferredReason"] = (
                    "presentation-system-not-bound"
                    if key == "ShadowWhenDead"
                    else "binary-midpoint-semantics-ambiguous"
                )
                fields[key] = value

        for key in ("FX", "Sound", "OCL", "Weapon"):
            phase_rows = _slow_death_phase_rows(
                grouped.get(key.casefold(), []), target_id=target_id, field=key
            )
            if phase_rows:
                fields[key] = phase_rows

        blockers: list[str] = []
        if "OCL" in fields:
            blockers.append("OCL")
        if "Weapon" in fields:
            blockers.append("Weapon")
        if "DoNotRandomizeMidpoint" in fields:
            blockers.append("DoNotRandomizeMidpoint")
        if any(
            phase_row["phase"] == "HIT_GROUND"
            for key in ("FX", "Sound", "OCL", "Weapon")
            for phase_row in fields.get(key, [])
        ):
            blockers.append("HIT_GROUND")

        runtime_status = "executable" if not blockers else "deferred"
        row = _row(
            "SlowDeathBehavior", block, fields, runtime_status=runtime_status
        )
        execution_eligibility: dict[str, object] = {
            "status": "evidence-closed-core" if not blockers else "deferred",
            "blockers": blockers,
            "runtimeStatus": runtime_status,
        }
        if blockers:
            execution_eligibility["deferredReason"] = (
                "slow-death-gameplay-variant-not-runtime-bound"
            )
        row["effectGraph"] = {
            "kind": "slow-death",
            "deathTypes": fields["deathTypes"],
            "includedDeathTypes": list(fields.get("includedDeathTypes", [])),
            "excludedDeathTypes": list(fields["excludedDeathTypes"]),
            "probabilityWeight": int(
                fields.get("ProbabilityModifier", {"value": 10})["value"]
            ),
            "sinkDelayMs": int(fields.get("SinkDelay", {"milliseconds": 0})["milliseconds"]),
            "sinkRatePerSecond": float(fields.get("SinkRate", {"value": 0.0})["value"]),
            "destructionDelayMs": int(
                fields.get("DestructionDelay", {"milliseconds": 0})["milliseconds"]
            ),
            "phaseOrder": ["INITIAL", "MIDPOINT", "FINAL"],
            # EA source closes this gameplay order.  Sound is retained as a
            # presentation receipt because its relative dispatch is not proven.
            "phaseEffectOrder": ["FX", "OCL", "Weapon"],
            "executionEligibility": execution_eligibility,
            "sourceIni": block.source_virtual_path,
            "line": block.line,
        }
        rows.append(row)
    rows.sort(key=lambda row: (str(row["sourceIni"]).casefold(), int(row["line"])))
    return rows


def _slow_death_row_has_closed_runtime(row: Mapping[str, object]) -> bool:
    """Accept only the exact SlowDeath gameplay subset proven by its runner.

    Presentation receipts do not block the core. Gameplay side effects and the
    binary-ambiguous midpoint/HIT_GROUND variants do. Recompute the boundary
    from typed fields as well as checking the effect-graph claim so a tampered
    descriptor cannot promote itself by changing only one projection.
    """

    fields = row.get("fields")
    graph = row.get("effectGraph")
    if not isinstance(fields, Mapping) or not isinstance(graph, Mapping):
        return False
    eligibility = graph.get("executionEligibility")
    if not isinstance(eligibility, Mapping) or eligibility != {
        "status": "evidence-closed-core",
        "blockers": [],
        "runtimeStatus": "executable",
    }:
        return False
    if any(key in fields for key in ("OCL", "Weapon", "DoNotRandomizeMidpoint")):
        return False
    for key in ("FX", "Sound"):
        phase_rows = fields.get(key, [])
        if not isinstance(phase_rows, list):
            return False
        for phase_row in phase_rows:
            if not isinstance(phase_row, Mapping):
                return False
            if str(phase_row.get("phase", "")).upper() == "HIT_GROUND":
                return False
    return True


_HORDE_TRANSPORT_FIELDS = frozenset(
    {
        "objectstatusofcontained",
        "slots",
        "entersound",
        "exitsound",
        "damagepercenttounits",
        "passengerfilter",
        "allowownplayerinsideoverride",
        "allowalliesinside",
        "allowenemiesinside",
        "allowneutralinside",
        "exitdelay",
        "numberofexitpaths",
        "forceorientationcontainer",
        "passengerboneprefix",
        "showpips",
        "killpassengersondeath",
        "ejectpassengersondeath",
        "fadefilter",
        "fadepassengeronenter",
        "enterfadetime",
        "fadepassengeronexit",
        "exitfadetime",
        "initialpayload",
    }
)
_PASSENGER_BONE_RE = re.compile(
    r"^\s*PassengerBone\s*:\s*(\S+)\s+KindOf\s*:\s*(\S+)\s*$",
    re.IGNORECASE,
)


def _integer_assignment_field(
    assignment: SageAssignment | None,
    label: str,
    *,
    minimum: int,
) -> dict[str, object] | None:
    if assignment is None:
        return None
    token = assignment.value.strip()
    if re.fullmatch(r"\d+", token) is None or int(token) < minimum:
        raise ModuleContractError(f"{label} must be an integer >= {minimum}")
    return {
        "authored": assignment.value,
        "value": int(token),
        "sourceIni": assignment.source_virtual_path,
        "line": assignment.line,
    }


def _percent_assignment_field(
    assignment: SageAssignment | None, label: str
) -> dict[str, object] | None:
    if assignment is None:
        return None
    match = re.fullmatch(r"\s*([+-]?(?:\d+\.?\d*|\.\d+))\s*%\s*", assignment.value)
    if match is None:
        raise ModuleContractError(f"{label} must be a percentage literal")
    percent = float(match.group(1))
    return {
        "authored": assignment.value,
        "percent": percent,
        "ratio": percent / 100.0,
        "sourceIni": assignment.source_virtual_path,
        "line": assignment.line,
    }


def compile_horde_transport_contains(
    lineage: Sequence[SageObject], target_id: str
) -> list[dict[str, object]]:
    """Compile measured horde/ship capacity, admission, and exit contracts."""

    rows: list[dict[str, object]] = []
    for block in _behavior_blocks(lineage, "HordeTransportContain"):
        amap = _assignment_map(block)
        unknown = set(amap) - _HORDE_TRANSPORT_FIELDS
        if unknown:
            raise ModuleContractError(
                f"{target_id} HordeTransportContain unsupported fields: "
                + ", ".join(sorted(unknown))
            )
        fields: dict[str, object] = {}
        for key in ("ObjectStatusOfContained", "PassengerFilter", "FadeFilter"):
            value = _token_list_field(amap.get(key.casefold()))
            if value is not None:
                fields[key] = value
        for required in ("ObjectStatusOfContained", "PassengerFilter"):
            if required not in fields:
                raise ModuleContractError(
                    f"{target_id} HordeTransportContain requires {required}"
                )
        slots = _integer_assignment_field(
            amap.get("slots"), f"{target_id} HordeTransportContain Slots", minimum=1
        )
        if slots is None:
            raise ModuleContractError(
                f"{target_id} HordeTransportContain requires Slots"
            )
        fields["Slots"] = slots
        exits = _integer_assignment_field(
            amap.get("numberofexitpaths"),
            f"{target_id} HordeTransportContain NumberOfExitPaths",
            minimum=0,
        )
        if exits is not None:
            fields["NumberOfExitPaths"] = exits
        damage = _percent_assignment_field(
            amap.get("damagepercenttounits"),
            f"{target_id} HordeTransportContain DamagePercentToUnits",
        )
        if damage is None:
            raise ModuleContractError(
                f"{target_id} HordeTransportContain requires DamagePercentToUnits"
            )
        fields["DamagePercentToUnits"] = damage
        for key in ("EnterSound", "ExitSound"):
            value = _string_field(amap.get(key.casefold()))
            if value is not None:
                fields[key] = value
        for key in (
            "AllowOwnPlayerInsideOverride",
            "AllowAlliesInside",
            "AllowEnemiesInside",
            "AllowNeutralInside",
            "ForceOrientationContainer",
            "ShowPips",
            "KillPassengersOnDeath",
            "EjectPassengersOnDeath",
            "FadePassengerOnEnter",
            "FadePassengerOnExit",
        ):
            value = _yes_no_field(
                amap.get(key.casefold()), f"{target_id} HordeTransportContain {key}"
            )
            if value is not None:
                fields[key] = value
        for key in ("ExitDelay", "EnterFadeTime", "ExitFadeTime"):
            value = _milliseconds_field(
                amap.get(key.casefold()), f"{target_id} HordeTransportContain {key}"
            )
            if value is not None:
                fields[key] = value
        bones: list[dict[str, object]] = []
        for assignment in block.assignments:
            if assignment.key.casefold() != "passengerboneprefix":
                continue
            match = _PASSENGER_BONE_RE.fullmatch(assignment.value)
            if match is None:
                raise ModuleContractError(
                    f"{target_id} HordeTransportContain PassengerBonePrefix malformed"
                )
            bones.append(
                {
                    "passengerBone": match.group(1),
                    "kindOf": match.group(2).upper(),
                    "authored": assignment.value,
                    "sourceIni": assignment.source_virtual_path,
                    "line": assignment.line,
                }
            )
        if not bones:
            raise ModuleContractError(
                f"{target_id} HordeTransportContain requires PassengerBonePrefix"
            )
        fields["PassengerBonePrefix"] = bones
        payload = amap.get("initialpayload")
        if payload is not None:
            tokens = payload.value.split()
            if len(tokens) != 2 or re.fullmatch(r"\d+", tokens[1]) is None or int(tokens[1]) < 1:
                raise ModuleContractError(
                    f"{target_id} HordeTransportContain InitialPayload malformed"
                )
            fields["InitialPayload"] = {
                "objectId": tokens[0],
                "count": int(tokens[1]),
                "authored": payload.value,
                "sourceIni": payload.source_virtual_path,
                "line": payload.line,
            }
        rows.append(_row("HordeTransportContain", block, fields))
    rows.sort(key=lambda row: (str(row["sourceIni"]).casefold(), int(row["line"])))
    return rows


_ATTRIBUTE_MODIFIER_AURA_FIELDS = frozenset(
    {
        "startsactive",
        "bonusname",
        "triggeredby",
        "conflictswith",
        "refreshdelay",
        "range",
        "objectfilter",
        "targetenemy",
        "maxactiverank",
        "anticategory",
        "allowself",
        "runwhiledead",
        "requiredconditions",
        "affectcontainedonly",
    }
)
_NON_NEGATIVE_EXPRESSION_RE = re.compile(
    r"^(?:\d+(?:\.\d*)?|\.\d+|[A-Za-z_][A-Za-z0-9_]*)$"
)


def _non_negative_expression_field(
    assignment: SageAssignment | None,
    label: str,
    *,
    integer_literal: bool = False,
) -> dict[str, object] | None:
    """Preserve a literal/define expression, resolving safe literals only."""

    if assignment is None:
        return None
    expression = assignment.value.strip()
    if _NON_NEGATIVE_EXPRESSION_RE.fullmatch(expression) is None:
        raise ModuleContractError(
            f"{label} must be a non-negative numeric literal or define"
        )
    result: dict[str, object] = {
        "expression": expression,
        "sourceIni": assignment.source_virtual_path,
        "line": assignment.line,
    }
    if re.fullmatch(r"(?:\d+(?:\.\d*)?|\.\d+)", expression):
        numeric = float(expression)
        if integer_literal and not numeric.is_integer():
            raise ModuleContractError(f"{label} must resolve to an integer")
        result["value"] = int(numeric) if numeric.is_integer() else numeric
    return result


def compile_attribute_modifier_aura_updates(
    lineage: Sequence[SageObject], target_id: str
) -> list[dict[str, object]]:
    """Compile every effective retail aura assignment into typed evidence."""

    rows: list[dict[str, object]] = []
    for block in _behavior_blocks(lineage, "AttributeModifierAuraUpdate"):
        field_counts: dict[str, int] = {}
        for assignment in block.assignments:
            folded = assignment.key.casefold()
            field_counts[folded] = field_counts.get(folded, 0) + 1
        duplicated = sorted(key for key, count in field_counts.items() if count > 1)
        if duplicated:
            raise ModuleContractError(
                f"{target_id} AttributeModifierAuraUpdate duplicate fields: "
                + ", ".join(duplicated)
            )
        amap = _assignment_map(block)
        unknown = set(amap) - _ATTRIBUTE_MODIFIER_AURA_FIELDS
        if unknown:
            raise ModuleContractError(
                f"{target_id} AttributeModifierAuraUpdate unsupported fields: "
                + ", ".join(sorted(unknown))
            )
        fields: dict[str, object] = {}
        for key in (
            "TriggeredBy",
            "ConflictsWith",
            "ObjectFilter",
            "AntiCategory",
            "RequiredConditions",
        ):
            value = _token_list_field(amap.get(key.casefold()))
            if value is not None:
                if not value["value"]:
                    raise ModuleContractError(
                        f"{target_id} AttributeModifierAuraUpdate {key} is empty"
                    )
                fields[key] = value
        for key in (
            "StartsActive",
            "TargetEnemy",
            "AllowSelf",
            "RunWhileDead",
            "AffectContainedOnly",
        ):
            value = _yes_no_field(
                amap.get(key.casefold()),
                f"{target_id} AttributeModifierAuraUpdate {key}",
            )
            if value is not None:
                fields[key] = value
        for key in ("BonusName",):
            value = _string_field(amap.get(key.casefold()))
            if value is not None:
                if len(str(value["value"]).split()) != 1:
                    raise ModuleContractError(
                        f"{target_id} AttributeModifierAuraUpdate {key} malformed"
                    )
                fields[key] = value
        refresh = _milliseconds_field(
            amap.get("refreshdelay"),
            f"{target_id} AttributeModifierAuraUpdate RefreshDelay",
        )
        if refresh is not None:
            fields["RefreshDelay"] = refresh
        aura_range = _non_negative_expression_field(
            amap.get("range"), f"{target_id} AttributeModifierAuraUpdate Range"
        )
        if aura_range is not None:
            fields["Range"] = aura_range
        max_rank = _integer_assignment_field(
            amap.get("maxactiverank"),
            f"{target_id} AttributeModifierAuraUpdate MaxActiveRank",
            minimum=1,
        )
        if max_rank is not None:
            fields["MaxActiveRank"] = max_rank
        if "BonusName" not in fields:
            raise ModuleContractError(
                f"{target_id} AttributeModifierAuraUpdate requires BonusName"
            )
        if "StartsActive" not in fields:
            # RotWK deliberately omits StartsActive on upgrade-gated auras:
            # 52 distinct effective declarations (68 inherited occurrences),
            # every one with TriggeredBy. AttributeModifierAuraUpdate's module
            # data stores this as an ordinary zero-initialized bool, so the
            # omitted value is false. Keep that engine default distinguishable
            # from authored `StartsActive = No`; an ungated omission remains a
            # hard error instead of silently disabling an aura.
            if "TriggeredBy" not in fields:
                raise ModuleContractError(
                    f"{target_id} AttributeModifierAuraUpdate omitted "
                    "StartsActive requires an authored TriggeredBy gate"
                )
            fields["StartsActive"] = {
                "authored": None,
                "value": False,
                "defaulted": True,
                "defaultSource": (
                    "AttributeModifierAuraUpdate-module-data-bool-zero"
                ),
                "sourceIni": block.source_virtual_path,
                "line": block.line,
            }
        rows.append(_row("AttributeModifierAuraUpdate", block, fields))
    rows.sort(key=lambda row: (str(row["sourceIni"]).casefold(), int(row["line"])))
    return rows


_AUTO_HEAL_SUPPORTED = frozenset(
    {
        "startsactive",
        "healingamount",
        "healingdelay",
        "starthealingdelay",
        "healonlyifnotincombat",
        "triggeredby",
        "radius",
        "buttontriggered",
        "singleburst",
        "affectscontained",
        "healonlyothers",
        "kindof",
        "nonstackable",
        "unithealpulsefx",
        "respawnnearbyhordemembers",
        "respawnfxlist",
        "respawnminimumdelay",
    }
)
# Authoring any of these leaves the closed self-heal subset: the heal stops
# being "this object, on its own timer" and needs a system nobody runs yet.
_AUTO_HEAL_DEFERRING_FIELDS: Mapping[str, str] = {
    "triggeredby": "upgrade-triggered-activation-without-runtime-oracle",
    "radius": "area-heal-without-runtime-oracle",
    "buttontriggered": "button-triggered-burst-without-runtime-oracle",
    "singleburst": "button-triggered-burst-without-runtime-oracle",
    "affectscontained": "contained-heal-without-runtime-oracle",
    "healonlyothers": "others-only-heal-without-runtime-oracle",
    "kindof": "kind-filtered-heal-without-runtime-oracle",
    "nonstackable": "stacking-policy-without-runtime-oracle",
    "unithealpulsefx": "presentation-field-without-runtime-oracle",
    "respawnnearbyhordemembers": "horde-respawn-heal-without-runtime-oracle",
    "respawnfxlist": "horde-respawn-heal-without-runtime-oracle",
    "respawnminimumdelay": "horde-respawn-heal-without-runtime-oracle",
}
_AUTO_HEAL_TRAILING_COMMENT = re.compile(r"(?://|;).*$")


def _auto_heal_flag(
    assignment: SageAssignment | None, label: str
) -> dict[str, object] | None:
    """Yes/No tolerant of the trailing `// ...` retail authors on StartsActive."""

    if assignment is None:
        return None
    folded = _AUTO_HEAL_TRAILING_COMMENT.sub("", assignment.value).strip().casefold()
    if folded not in {"yes", "no"}:
        raise ModuleContractError(f"{label} must be Yes or No: {assignment.value!r}")
    return {
        "authored": assignment.value,
        "value": folded == "yes",
        "sourceIni": assignment.source_virtual_path,
        "line": assignment.line,
    }


def _auto_heal_scalar(
    assignment: SageAssignment | None,
    label: str,
    *,
    numeric_defines: Mapping[str, int | float] | None,
    numeric_define_provenance: Mapping[str, Mapping[str, object]] | None,
) -> dict[str, object] | None:
    """A literal number, or a GameData define resolved with its own receipt.

    An unresolvable expression keeps the authored text and omits ``value`` so a
    caller cannot mistake "we do not know this magnitude" for a magnitude.
    """

    if assignment is None:
        return None
    authored = _AUTO_HEAL_TRAILING_COMMENT.sub("", assignment.value).strip()
    field: dict[str, object] = {
        "authored": assignment.value,
        "sourceIni": assignment.source_virtual_path,
        "line": assignment.line,
    }
    literal = re.fullmatch(r"[+-]?(?:\d+\.?\d*|\.\d+)", authored)
    if literal is not None:
        text = literal.group(0)
        field["value"] = int(text) if re.fullmatch(r"[+-]?\d+", text) else float(text)
        return field
    if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", authored) is None:
        raise ModuleContractError(f"{label} is neither a number nor a define: {authored!r}")
    field["expression"] = authored
    from .playable_unit_compiler import _evaluated_define_body

    bodies = {
        str(name).casefold(): str(value)
        for name, value in (numeric_defines or {}).items()
        if not isinstance(value, bool) and isinstance(value, (int, float))
    }
    resolved = _evaluated_define_body(authored, bodies)
    if resolved is None or isinstance(resolved, bool):
        return field
    provenance = (
        None
        if numeric_define_provenance is None
        else numeric_define_provenance.get(authored.casefold())
    )
    if provenance is None:
        return field
    field["value"] = int(resolved) if float(resolved).is_integer() else float(resolved)
    field["defineProvenance"] = dict(provenance)
    return field


def compile_auto_heal_behaviors(
    lineage: Sequence[SageObject],
    target_id: str,
    *,
    numeric_defines: Mapping[str, int | float] | None = None,
    numeric_define_provenance: Mapping[str, Mapping[str, object]] | None = None,
) -> list[dict[str, object]]:
    """Compile the self-only regeneration timer retail authors on heroes.

    Executable subset: ``StartsActive = Yes`` with a positive integral
    ``HealingAmount`` and a positive ``HealingDelay``, and nothing that widens
    the heal past the object itself.  ``StartHealingDelay`` is the
    damage-anchored restart delay; ``HealOnlyIfNotInCombat`` says whether damage
    restarts it at all.  Every other authored shape is a deferred row that names
    why.
    """

    rows: list[dict[str, object]] = []
    for block in _behavior_blocks(lineage, "AutoHealBehavior"):
        counts: dict[str, int] = {}
        for assignment in block.assignments:
            folded = assignment.key.casefold()
            counts[folded] = counts.get(folded, 0) + 1
        duplicated = sorted(key for key, count in counts.items() if count > 1)
        if duplicated:
            raise ModuleContractError(
                f"{target_id} AutoHealBehavior duplicate fields: " + ", ".join(duplicated)
            )
        amap = _assignment_map(block)
        unknown = set(amap) - _AUTO_HEAL_SUPPORTED
        if unknown:
            raise ModuleContractError(
                f"{target_id} AutoHealBehavior unsupported fields: "
                + ", ".join(sorted(unknown))
            )
        fields: dict[str, object] = {}
        unsupported: list[dict[str, object]] = []
        for key, label in (
            ("StartsActive", "StartsActive"),
            ("HealOnlyIfNotInCombat", "HealOnlyIfNotInCombat"),
        ):
            flag = _auto_heal_flag(
                amap.get(key.casefold()), f"{target_id} AutoHealBehavior {label}"
            )
            if flag is not None:
                fields[key] = flag
        amount = _auto_heal_scalar(
            amap.get("healingamount"),
            f"{target_id} AutoHealBehavior HealingAmount",
            numeric_defines=numeric_defines,
            numeric_define_provenance=numeric_define_provenance,
        )
        if amount is not None:
            fields["HealingAmount"] = amount
        for key in ("HealingDelay", "StartHealingDelay"):
            duration = _auto_heal_scalar(
                amap.get(key.casefold()),
                f"{target_id} AutoHealBehavior {key}",
                numeric_defines=numeric_defines,
                numeric_define_provenance=numeric_define_provenance,
            )
            if duration is None:
                continue
            if "value" in duration:
                numeric = float(duration.pop("value"))
                if not numeric.is_integer() or numeric < 0:
                    raise ModuleContractError(
                        f"{target_id} AutoHealBehavior {key} must be non-negative "
                        f"integer milliseconds: {duration['authored']!r}"
                    )
                duration["milliseconds"] = int(numeric)
            fields[key] = duration
        for folded, reason in sorted(_AUTO_HEAL_DEFERRING_FIELDS.items()):
            assignment = amap.get(folded)
            if assignment is None:
                continue
            unsupported.append(
                {
                    "name": assignment.key,
                    "authored": assignment.value,
                    "sourceIni": assignment.source_virtual_path,
                    "line": assignment.line,
                    "reason": reason,
                }
            )
        if amount is None or "HealingDelay" not in fields:
            unsupported.append(
                {
                    "name": "HealingAmount" if amount is None else "HealingDelay",
                    "reason": "incomplete-heal-cadence",
                    "sourceIni": block.source_virtual_path,
                    "line": block.line,
                }
            )
        for key in ("HealingAmount", "HealingDelay", "StartHealingDelay"):
            field = fields.get(key)
            if isinstance(field, dict) and "expression" in field and "value" not in field and "milliseconds" not in field:
                unsupported.append(
                    {
                        "name": key,
                        "authored": field["authored"],
                        "sourceIni": field["sourceIni"],
                        "line": field["line"],
                        "reason": "unresolved-define-expression",
                    }
                )
        starts_active = fields.get("StartsActive")
        if not isinstance(starts_active, dict) or not bool(starts_active.get("value")):
            unsupported.append(
                {
                    "name": "StartsActive",
                    "reason": "starts-inactive",
                    "sourceIni": block.source_virtual_path,
                    "line": block.line,
                }
            )
        if isinstance(amount, dict) and "value" in amount:
            numeric_amount = float(amount["value"])
            if numeric_amount <= 0.0:
                unsupported.append(
                    {
                        "name": "HealingAmount",
                        "authored": amount["authored"],
                        "sourceIni": amount["sourceIni"],
                        "line": amount["line"],
                        "reason": "non-positive-healing-amount",
                    }
                )
            elif not numeric_amount.is_integer():
                unsupported.append(
                    {
                        "name": "HealingAmount",
                        "authored": amount["authored"],
                        "sourceIni": amount["sourceIni"],
                        "line": amount["line"],
                        "reason": "fractional-healing-amount",
                    }
                )
        delay = fields.get("HealingDelay")
        if isinstance(delay, dict) and int(delay.get("milliseconds", 0)) <= 0 and "milliseconds" in delay:
            unsupported.append(
                {
                    "name": "HealingDelay",
                    "authored": delay["authored"],
                    "sourceIni": delay["sourceIni"],
                    "line": delay["line"],
                    "reason": "non-positive-healing-delay",
                }
            )
        if unsupported:
            fields["unsupportedSemantics"] = unsupported
        rows.append(
            _row(
                "AutoHealBehavior",
                block,
                fields,
                runtime_status="deferred" if unsupported else "executable",
            )
        )
    rows.sort(key=lambda row: (str(row["sourceIni"]).casefold(), int(row["line"])))
    return rows


_LIFETIME_UPDATE_FIELDS = frozenset(
    {"minlifetime", "maxlifetime", "deathtype", "waitforwakeup"}
)


def _lifetime_expression_field(
    assignment: SageAssignment | None, label: str
) -> dict[str, object] | None:
    value = _non_negative_expression_field(assignment, label)
    if value is not None and "value" in value:
        numeric = float(value.pop("value"))
        if not numeric.is_integer():
            raise ModuleContractError(
                f"{label} must resolve to integer milliseconds"
            )
        value["milliseconds"] = int(numeric)
    return value


def compile_lifetime_updates(
    lineage: Sequence[SageObject], target_id: str
) -> list[dict[str, object]]:
    """Compile authored lifetime bounds or the wake-up-gated retail shape."""

    rows: list[dict[str, object]] = []
    for block in _behavior_blocks(lineage, "LifetimeUpdate"):
        field_counts: dict[str, int] = {}
        for assignment in block.assignments:
            folded = assignment.key.casefold()
            field_counts[folded] = field_counts.get(folded, 0) + 1
        duplicated = sorted(key for key, count in field_counts.items() if count > 1)
        if duplicated:
            raise ModuleContractError(
                f"{target_id} LifetimeUpdate duplicate fields: "
                + ", ".join(duplicated)
            )
        amap = _assignment_map(block)
        unknown = set(amap) - _LIFETIME_UPDATE_FIELDS
        if unknown:
            raise ModuleContractError(
                f"{target_id} LifetimeUpdate unsupported fields: "
                + ", ".join(sorted(unknown))
            )
        fields: dict[str, object] = {}
        minimum = _lifetime_expression_field(
            amap.get("minlifetime"), f"{target_id} LifetimeUpdate MinLifetime"
        )
        maximum = _lifetime_expression_field(
            amap.get("maxlifetime"), f"{target_id} LifetimeUpdate MaxLifetime"
        )
        wake = _yes_no_field(
            amap.get("waitforwakeup"), f"{target_id} LifetimeUpdate WaitForWakeUp"
        )
        if (minimum is None) != (maximum is None):
            raise ModuleContractError(
                f"{target_id} LifetimeUpdate requires both MinLifetime and MaxLifetime"
            )
        if minimum is None and wake is None:
            raise ModuleContractError(
                f"{target_id} LifetimeUpdate requires lifetime bounds or WaitForWakeUp"
            )
        if minimum is not None:
            fields["MinLifetime"] = minimum
            fields["MaxLifetime"] = maximum
        if wake is not None:
            fields["WaitForWakeUp"] = wake
        death_type = _string_field(amap.get("deathtype"))
        if death_type is not None:
            token = str(death_type["value"]).upper()
            if len(token.split()) != 1:
                raise ModuleContractError(
                    f"{target_id} LifetimeUpdate DeathType malformed"
                )
            death_type["value"] = token
            fields["DeathType"] = death_type
        rows.append(_row("LifetimeUpdate", block, fields))
    rows.sort(key=lambda row: (str(row["sourceIni"]).casefold(), int(row["line"])))
    return rows


_AI_UPDATE_FIELDS = frozenset(
    {
        "autoacquireenemieswhenidle",
        "canattackwhilecontained",
        "ailuaeventslist",
        "attackpriority",
        "fadeonportals",
        "moodattackcheckrate",
        "holdgroundcloserangedistance",
        "mincowertime",
        "maxcowertime",
        "rampagetime",
        "timetoejectpassengersonrampage",
        "stopchasedistance",
        "burningdeathtime",
        "rampagerequiresaflame",
        "specialcontactpoints",
    }
)
_AI_AUTO_ACQUIRE_FLAGS = frozenset({"ATTACK_BUILDINGS", "STEALTHED"})
_AI_TURRET_FIELDS = frozenset({"turretturnrate", "controlledweaponslots"})


def compile_ai_update_interfaces(
    lineage: Sequence[SageObject], target_id: str
) -> list[dict[str, object]]:
    """Compile the complete effective-retail AIUpdateInterface grammar."""

    rows: list[dict[str, object]] = []
    for block in _behavior_blocks(lineage, "AIUpdateInterface"):
        counts: dict[str, int] = {}
        for assignment in block.assignments:
            folded = assignment.key.casefold()
            counts[folded] = counts.get(folded, 0) + 1
        duplicated = sorted(key for key, count in counts.items() if count > 1)
        if duplicated:
            raise ModuleContractError(
                f"{target_id} AIUpdateInterface duplicate fields: "
                + ", ".join(duplicated)
            )
        amap = _assignment_map(block)
        unknown = set(amap) - _AI_UPDATE_FIELDS
        if unknown:
            raise ModuleContractError(
                f"{target_id} AIUpdateInterface unsupported fields: "
                + ", ".join(sorted(unknown))
            )
        fields: dict[str, object] = {}
        acquire = amap.get("autoacquireenemieswhenidle")
        if acquire is not None:
            tokens = acquire.value.split()
            if not tokens or tokens[0].casefold() not in {"yes", "no"}:
                raise ModuleContractError(
                    f"{target_id} AIUpdateInterface AutoAcquireEnemiesWhenIdle "
                    "must start with Yes or No"
                )
            flags = [token.upper() for token in tokens[1:]]
            if tokens[0].casefold() == "no" and flags:
                raise ModuleContractError(
                    f"{target_id} AIUpdateInterface disabled auto-acquire has flags"
                )
            unknown_flags = set(flags) - _AI_AUTO_ACQUIRE_FLAGS
            if unknown_flags or len(flags) != len(set(flags)):
                raise ModuleContractError(
                    f"{target_id} AIUpdateInterface AutoAcquireEnemiesWhenIdle "
                    "has unsupported or duplicate flags"
                )
            fields["AutoAcquireEnemiesWhenIdle"] = {
                "enabled": tokens[0].casefold() == "yes",
                "flags": flags,
                "authored": acquire.value,
                "sourceIni": acquire.source_virtual_path,
                "line": acquire.line,
            }
        for key in (
            "CanAttackWhileContained",
            "FadeOnPortals",
            "RampageRequiresAflame",
        ):
            value = _yes_no_field(
                amap.get(key.casefold()), f"{target_id} AIUpdateInterface {key}"
            )
            if value is not None:
                fields[key] = value
        for key in ("AILuaEventsList", "AttackPriority", "SpecialContactPoints"):
            value = _string_field(amap.get(key.casefold()))
            if value is not None:
                if len(str(value["value"]).split()) != 1:
                    raise ModuleContractError(
                        f"{target_id} AIUpdateInterface {key} malformed"
                    )
                fields[key] = value
        for key in (
            "MoodAttackCheckRate",
            "MinCowerTime",
            "MaxCowerTime",
            "RampageTime",
            "TimeToEjectPassengersOnRampage",
        ):
            value = _milliseconds_field(
                amap.get(key.casefold()), f"{target_id} AIUpdateInterface {key}"
            )
            if value is not None:
                fields[key] = value
        for key in ("HoldGroundCloseRangeDistance", "StopChaseDistance"):
            value = _number_field(
                amap.get(key.casefold()), f"{target_id} AIUpdateInterface {key}"
            )
            if value is not None:
                if float(value["value"]) < 0:
                    raise ModuleContractError(
                        f"{target_id} AIUpdateInterface {key} must be non-negative"
                    )
                fields[key] = value
        burning = _lifetime_expression_field(
            amap.get("burningdeathtime"),
            f"{target_id} AIUpdateInterface BurningDeathTime",
        )
        if burning is not None:
            fields["BurningDeathTime"] = burning

        turrets: list[dict[str, object]] = []
        unknown_nested = [
            child.kind for child in block.blocks if child.kind.casefold() != "turret"
        ]
        if unknown_nested:
            raise ModuleContractError(
                f"{target_id} AIUpdateInterface unsupported nested blocks: "
                + ", ".join(sorted(unknown_nested, key=str.casefold))
            )
        for turret in block.blocks:
            turret_map = _assignment_map(turret)
            unknown = set(turret_map) - _AI_TURRET_FIELDS
            if unknown:
                raise ModuleContractError(
                    f"{target_id} AIUpdateInterface Turret unsupported fields: "
                    + ", ".join(sorted(unknown))
                )
            if len(turret_map) != len(turret.assignments):
                raise ModuleContractError(
                    f"{target_id} AIUpdateInterface Turret duplicate fields"
                )
            turn = _number_field(
                turret_map.get("turretturnrate"),
                f"{target_id} AIUpdateInterface TurretTurnRate",
            )
            slots = _token_list_field(turret_map.get("controlledweaponslots"))
            if turn is None or float(turn["value"]) < 0 or slots is None or not slots["value"]:
                raise ModuleContractError(
                    f"{target_id} AIUpdateInterface Turret requires non-negative "
                    "TurretTurnRate and ControlledWeaponSlots"
                )
            turrets.append(
                {
                    "TurretTurnRate": turn,
                    "ControlledWeaponSlots": slots,
                    "sourceIni": turret.source_virtual_path,
                    "line": turret.line,
                }
            )
        if turrets:
            fields["Turrets"] = turrets
        rows.append(_row("AIUpdateInterface", block, fields))
    rows.sort(key=lambda row: (str(row["sourceIni"]).casefold(), int(row["line"])))
    return rows


def compile_stances_behaviors(
    lineage: Sequence[SageObject], target_id: str
) -> list[dict[str, object]]:
    """Compile the complete effective-retail StancesBehavior grammar."""

    rows: list[dict[str, object]] = []
    for block in _behavior_blocks(lineage, "StancesBehavior"):
        if block.blocks:
            raise ModuleContractError(
                f"{target_id} StancesBehavior authors unsupported nested blocks"
            )
        authored = [
            assignment
            for assignment in block.assignments
            if assignment.key.casefold() == "stancetemplate"
        ]
        unknown = sorted(
            {
                assignment.key
                for assignment in block.assignments
                if assignment.key.casefold() != "stancetemplate"
            },
            key=str.casefold,
        )
        if unknown:
            raise ModuleContractError(
                f"{target_id} StancesBehavior unsupported fields: "
                + ", ".join(unknown)
            )
        if len(authored) != 1:
            raise ModuleContractError(
                f"{target_id} StancesBehavior requires exactly one StanceTemplate"
            )
        field = _string_field(authored[0])
        assert field is not None
        if len(str(field["value"]).split()) != 1:
            raise ModuleContractError(
                f"{target_id} StancesBehavior StanceTemplate malformed"
            )
        rows.append(_row("StancesBehavior", block, {"StanceTemplate": field}))
    rows.sort(key=lambda row: (str(row["sourceIni"]).casefold(), int(row["line"])))
    return rows


_HORDE_CONTAIN_FIELDS = frozenset(
    {
        "frontangle", "flankeddelay", "objectstatusofcontained", "initialpayload",
        "slots", "passengerfilter", "showpips", "thisformationisthemainformation",
        "randomoffset", "rankstoreleasewhenattacking", "rankstojustfreewhenattacking",
        "attributemodifiers", "isporcupineformation", "minimumhordesize",
        "alternateformation", "visionrearoverride", "visionsideoverride",
        "notcomboformation", "bannercarriersallowed", "bannercarrierposition",
        "rankinfo", "meleebehavior", "meleeattackleashdistance", "backupmindelaytime",
        "backupmaxdelaytime", "backupmindistance", "backupmaxdistance",
        "backuppercentage", "ranksplit", "splithordenumber", "splithorde",
        "useslowhordemovement", "facingbonus", "anglelimitcos", "innerrange",
        "outerrange", "outerrangebuildings", "bannercarrierminlevel",
        "bannercarrierdestroyhordeondeath", "bannercarrierhordedeathtype",
        "livingworldoverloadtemplate",
    }
)
_HORDE_REPEATED_FIELDS = frozenset(
    {"initialpayload", "randomoffset", "rankinfo", "splithorde"}
)
_HORDE_BOOL_FIELDS = (
    "ShowPips", "ThisFormationIsTheMainFormation", "IsPorcupineFormation",
    "NotComboFormation", "RankSplit", "UseSlowHordeMovement",
    "BannerCarrierDestroyHordeOnDeath",
)
_HORDE_INT_FIELDS = (
    ("Slots", 0), ("MinimumHordeSize", 0), ("SplitHordeNumber", 0),
    ("BannerCarrierMinLevel", 0),
)
_HORDE_NUMBER_FIELDS = (
    "FrontAngle", "BackUpMinDistance", "BackUpMaxDistance",
    "MeleeAttackLeashDistance", "FacingBonus", "AngleLimitCos", "InnerRange",
    "OuterRange", "OuterRangeBuildings",
)
_HORDE_TOKEN_LIST_FIELDS = (
    "ObjectStatusOfContained", "PassengerFilter", "RanksToReleaseWhenAttacking",
    "RanksToJustFreeWhenAttacking", "AttributeModifiers",
    "BannerCarrierHordeDeathType",
)
_HORDE_IDENTIFIER_FIELDS = (
    "AlternateFormation", "BannerCarriersAllowed", "MeleeBehavior",
    "LivingWorldOverloadTemplate",
)
_HORDE_STRUCTURED_RE = re.compile(r"(\S+?):(\S+)")


def _authored_row(assignment: SageAssignment) -> dict[str, object]:
    return {
        "authored": assignment.value,
        "sourceIni": assignment.source_virtual_path,
        "line": assignment.line,
    }


def _horde_structured_field(
    assignment: SageAssignment, target_id: str, field: str
) -> dict[str, object]:
    tokens = assignment.value.split()
    clauses: list[dict[str, str]] = []
    for token in tokens:
        match = _HORDE_STRUCTURED_RE.fullmatch(token)
        if match is None:
            clauses.append({"key": "argument", "value": token})
        else:
            clauses.append({"key": match.group(1), "value": match.group(2)})
    if not clauses:
        raise ModuleContractError(f"{target_id} HordeContain {field} is empty")
    authored_keys = {row["key"].casefold() for row in clauses}
    allowed = {
        "rankinfo": {
            "argument", "ranknumber", "unittype", "position", "y",
            "grantedweaponcondition", "revokedweaponcondition",
        },
        "splithorde": {"splitresult", "unittype", "ranknumber"},
        "bannercarrierposition": {"unittype", "pos", "y"},
    }[field.casefold()]
    unknown = authored_keys - allowed
    if unknown:
        raise ModuleContractError(
            f"{target_id} HordeContain {field} has unsupported clauses: "
            + ", ".join(sorted(unknown))
        )
    arguments = [row["value"] for row in clauses if row["key"] == "argument"]
    if arguments and (
        field.casefold() != "rankinfo"
        or any(value.casefold() != "leader" and not value.isdigit() for value in arguments)
    ):
        raise ModuleContractError(
            f"{target_id} HordeContain {field} has malformed arguments"
        )
    required = {
        "rankinfo": {"ranknumber", "unittype", "position"},
        "splithorde": {"splitresult", "unittype", "ranknumber"},
        "bannercarrierposition": {"unittype", "pos"},
    }[field.casefold()]
    if not required <= authored_keys:
        raise ModuleContractError(
            f"{target_id} HordeContain {field} lacks required clauses"
        )
    return {**_authored_row(assignment), "clauses": clauses}


def _horde_payload_count(
    expression: str | None, target_id: str
) -> dict[str, object] | None:
    if expression is None:
        return None
    if expression.isdigit() and int(expression) > 0:
        return {"kind": "literal", "value": int(expression)}
    if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", expression):
        return {"kind": "define", "name": expression}
    match = re.fullmatch(
        r"#MULTIPLY\(\s*([A-Za-z_][A-Za-z0-9_]*)\s+([1-9]\d*)\s*\)",
        expression,
        re.IGNORECASE,
    )
    if match is not None:
        return {
            "kind": "multiply", "name": match.group(1),
            "factor": int(match.group(2)),
        }
    raise ModuleContractError(
        f"{target_id} HordeContain InitialPayload count malformed"
    )


def compile_horde_contains(
    lineage: Sequence[SageObject],
    target_id: str,
) -> list[dict[str, object]]:
    """Compile every measured HordeContain scalar and repeated row."""

    rows: list[dict[str, object]] = []
    for block in _behavior_blocks(lineage, "HordeContain"):
        if any(child.kind.casefold() != "meleebehavior" for child in block.blocks):
            raise ModuleContractError(
                f"{target_id} HordeContain authors unsupported nested blocks"
            )
        grouped: dict[str, list[SageAssignment]] = {}
        for assignment in block.assignments:
            grouped.setdefault(assignment.key.casefold(), []).append(assignment)
        unknown = set(grouped) - _HORDE_CONTAIN_FIELDS
        if unknown:
            raise ModuleContractError(
                f"{target_id} HordeContain unsupported fields: "
                + ", ".join(sorted(unknown))
            )
        duplicated = sorted(
            key for key, values in grouped.items()
            if len(values) > 1 and key not in _HORDE_REPEATED_FIELDS
        )
        if duplicated:
            raise ModuleContractError(
                f"{target_id} HordeContain duplicate scalar fields: "
                + ", ".join(duplicated)
            )
        amap = {key: values[-1] for key, values in grouped.items()}
        fields: dict[str, object] = {}
        melee_children = [
            child for child in block.blocks if child.kind.casefold() == "meleebehavior"
        ]
        if melee_children:
            if len(melee_children) != 1 or melee_children[0].blocks:
                raise ModuleContractError(
                    f"{target_id} HordeContain MeleeBehavior shape malformed"
                )
            child = melee_children[0]
            match = re.fullmatch(
                r"\s*MeleeBehavior\s*=\s*(\S+)\s*", child.raw_header,
                re.IGNORECASE,
            )
            if match is None:
                raise ModuleContractError(
                    f"{target_id} HordeContain MeleeBehavior header malformed"
                )
            fields["MeleeBehavior"] = {
                "authored": match.group(1), "value": match.group(1),
                "sourceIni": child.source_virtual_path, "line": child.line,
            }
            nested_map = _assignment_map(child)
            nested_unknown = set(nested_map) - {
                name.casefold()
                for name in (
                    "FacingBonus", "AngleLimitCos", "InnerRange", "OuterRange",
                    "OuterRangeBuildings",
                )
            }
            if nested_unknown or len(nested_map) != len(child.assignments):
                raise ModuleContractError(
                    f"{target_id} HordeContain MeleeBehavior nested fields malformed"
                )
            for key in (
                "FacingBonus", "AngleLimitCos", "InnerRange", "OuterRange",
                "OuterRangeBuildings",
            ):
                value = _number_field(
                    nested_map.get(key.casefold()), f"{target_id} HordeContain {key}"
                )
                if value is not None:
                    fields[key] = value
        for key in _HORDE_BOOL_FIELDS:
            value = _yes_no_field(amap.get(key.casefold()), f"{target_id} HordeContain {key}")
            if value is not None:
                fields[key] = value
        for key, minimum in _HORDE_INT_FIELDS:
            value = _integer_assignment_field(
                amap.get(key.casefold()), f"{target_id} HordeContain {key}", minimum=minimum
            )
            if value is not None:
                fields[key] = value
        for key in _HORDE_NUMBER_FIELDS:
            value = _number_field(amap.get(key.casefold()), f"{target_id} HordeContain {key}")
            if value is not None:
                fields[key] = value
        for key in ("FlankedDelay", "BackUpMinDelayTime", "BackUpMaxDelayTime"):
            value = _milliseconds_field(
                amap.get(key.casefold()),
                f"{target_id} HordeContain {key}",
            )
            if value is not None:
                fields[key] = value
        for key in ("BackupPercentage", "VisionRearOverride", "VisionSideOverride"):
            value = _percent_assignment_field(amap.get(key.casefold()), f"{target_id} HordeContain {key}")
            if value is not None:
                fields[key] = value
        for key in _HORDE_TOKEN_LIST_FIELDS:
            assignment = amap.get(key.casefold())
            if assignment is not None:
                value = _token_list_field(assignment)
                assert value is not None
                fields[key] = value
        for key in _HORDE_IDENTIFIER_FIELDS:
            assignment = amap.get(key.casefold())
            if assignment is not None:
                value = _string_field(assignment)
                assert value is not None
                if len(str(value["value"]).split()) != 1:
                    raise ModuleContractError(f"{target_id} HordeContain {key} malformed")
                fields[key] = value
        offsets: list[dict[str, object]] = []
        for offset in grouped.get("randomoffset", []):
            match = re.fullmatch(
                r"\s*X\s*:\s*([+-]?(?:\d+\.?\d*|\.\d+))\s+Y\s*:\s*([+-]?(?:\d+\.?\d*|\.\d+))\s*",
                offset.value, re.IGNORECASE,
            )
            if match is None:
                raise ModuleContractError(f"{target_id} HordeContain RandomOffset malformed")
            offsets.append({
                **_authored_row(offset),
                "value": {"x": float(match.group(1)), "y": float(match.group(2))},
            })
        if offsets:
            fields["RandomOffset"] = offsets
        payloads: list[dict[str, object]] = []
        for assignment in grouped.get("initialpayload", []):
            tokens = assignment.value.split(None, 1)
            if not tokens or re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", tokens[0]) is None:
                raise ModuleContractError(f"{target_id} HordeContain InitialPayload empty")
            count_expression = tokens[1].strip() if len(tokens) == 2 else None
            payloads.append({
                **_authored_row(assignment), "objectId": tokens[0],
                "countExpression": count_expression,
                "count": _horde_payload_count(count_expression, target_id),
            })
        if payloads:
            fields["InitialPayload"] = payloads
        for field in ("RankInfo", "SplitHorde"):
            authored = grouped.get(field.casefold(), [])
            if authored:
                fields[field] = [
                    _horde_structured_field(item, target_id, field) for item in authored
                ]
        banner = amap.get("bannercarrierposition")
        if banner is not None:
            fields["BannerCarrierPosition"] = _horde_structured_field(
                banner, target_id, "BannerCarrierPosition"
            )
        rows.append(_row("HordeContain", block, fields))
    rows.sort(key=lambda row: (str(row["sourceIni"]).casefold(), int(row["line"])))
    return rows


_HORDE_AI_UPDATE_FIELDS = frozenset(
    {
        "autoacquireenemieswhenidle", "moodattackcheckrate", "ailuaeventslist",
        "maxcowertime", "mincowertime", "attackpriority",
        "canattackwhilecontained",
    }
)


def compile_horde_ai_updates(
    lineage: Sequence[SageObject], target_id: str
) -> list[dict[str, object]]:
    """Compile the complete effective-retail ``HordeAIUpdate`` grammar."""

    rows: list[dict[str, object]] = []
    for block in _behavior_blocks(lineage, "HordeAIUpdate"):
        if block.blocks:
            raise ModuleContractError(
                f"{target_id} HordeAIUpdate authors unsupported nested blocks"
            )
        grouped: dict[str, list[SageAssignment]] = {}
        for assignment in block.assignments:
            grouped.setdefault(assignment.key.casefold(), []).append(assignment)
        unknown = set(grouped) - _HORDE_AI_UPDATE_FIELDS
        if unknown:
            raise ModuleContractError(
                f"{target_id} HordeAIUpdate unsupported fields: "
                + ", ".join(sorted(unknown))
            )
        duplicated = sorted(
            key for key, values in grouped.items()
            if len(values) > 1 and key != "ailuaeventslist"
        )
        if duplicated:
            raise ModuleContractError(
                f"{target_id} HordeAIUpdate duplicate scalar fields: "
                + ", ".join(duplicated)
            )
        amap = {key: values[-1] for key, values in grouped.items()}
        fields: dict[str, object] = {}
        acquire = amap.get("autoacquireenemieswhenidle")
        if acquire is not None:
            tokens = acquire.value.split()
            if not tokens or tokens[0].casefold() not in {"yes", "no"}:
                raise ModuleContractError(
                    f"{target_id} HordeAIUpdate AutoAcquireEnemiesWhenIdle malformed"
                )
            flags = [token.upper() for token in tokens[1:]]
            if (
                (tokens[0].casefold() == "no" and flags)
                or set(flags) - _AI_AUTO_ACQUIRE_FLAGS
                or len(flags) != len(set(flags))
            ):
                raise ModuleContractError(
                    f"{target_id} HordeAIUpdate AutoAcquireEnemiesWhenIdle "
                    "has unsupported or duplicate flags"
                )
            fields["AutoAcquireEnemiesWhenIdle"] = {
                **_authored_row(acquire),
                "enabled": tokens[0].casefold() == "yes",
                "flags": flags,
            }
        for key in ("MoodAttackCheckRate", "MinCowerTime", "MaxCowerTime"):
            value = _milliseconds_field(
                amap.get(key.casefold()), f"{target_id} HordeAIUpdate {key}"
            )
            if value is not None:
                fields[key] = value
        contained = _yes_no_field(
            amap.get("canattackwhilecontained"),
            f"{target_id} HordeAIUpdate CanAttackWhileContained",
        )
        if contained is not None:
            fields["CanAttackWhileContained"] = contained
        priority = amap.get("attackpriority")
        if priority is not None:
            value = _string_field(priority)
            assert value is not None
            if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", str(value["value"])) is None:
                raise ModuleContractError(
                    f"{target_id} HordeAIUpdate AttackPriority malformed"
                )
            fields["AttackPriority"] = value
        lua_rows: list[dict[str, object]] = []
        for assignment in grouped.get("ailuaeventslist", []):
            value = _string_field(assignment)
            assert value is not None
            if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", str(value["value"])) is None:
                raise ModuleContractError(
                    f"{target_id} HordeAIUpdate AILuaEventsList malformed"
                )
            lua_rows.append(value)
        if lua_rows:
            fields["AILuaEventsList"] = lua_rows
        rows.append(_row("HordeAIUpdate", block, fields))
    rows.sort(key=lambda row: (str(row["sourceIni"]).casefold(), int(row["line"])))
    return rows


_PICKUP_STUFF_FIELDS = frozenset(
    {"skirmishaionly", "stufftopickup", "scanrange", "scanintervalseconds"}
)


def compile_pickup_stuff_updates(
    lineage: Sequence[SageObject], target_id: str
) -> list[dict[str, object]]:
    """Compile the complete effective-retail ``PickupStuffUpdate`` grammar."""

    rows: list[dict[str, object]] = []
    for block in _behavior_blocks(lineage, "PickupStuffUpdate"):
        if block.blocks:
            raise ModuleContractError(
                f"{target_id} PickupStuffUpdate authors unsupported nested blocks"
            )
        amap = _assignment_map(block)
        if len(amap) != len(block.assignments):
            raise ModuleContractError(
                f"{target_id} PickupStuffUpdate duplicate fields"
            )
        unknown = set(amap) - _PICKUP_STUFF_FIELDS
        if unknown:
            raise ModuleContractError(
                f"{target_id} PickupStuffUpdate unsupported fields: "
                + ", ".join(sorted(unknown))
            )
        fields: dict[str, object] = {}
        skirmish = _yes_no_field(
            amap.get("skirmishaionly"),
            f"{target_id} PickupStuffUpdate SkirmishAIOnly",
        )
        stuff = _token_list_field(amap.get("stufftopickup"))
        scan_range = _number_field(
            amap.get("scanrange"), f"{target_id} PickupStuffUpdate ScanRange"
        )
        interval = _number_field(
            amap.get("scanintervalseconds"),
            f"{target_id} PickupStuffUpdate ScanIntervalSeconds",
        )
        if (
            skirmish is None or stuff is None or not stuff["value"]
            or scan_range is None or float(scan_range["value"]) < 0
            or interval is None or float(interval["value"]) < 0
        ):
            raise ModuleContractError(
                f"{target_id} PickupStuffUpdate requires its four non-negative fields"
            )
        interval["seconds"] = float(interval["value"])
        interval["milliseconds"] = int(round(float(interval["value"]) * 1000.0))
        fields["SkirmishAIOnly"] = skirmish
        fields["StuffToPickUp"] = stuff
        fields["ScanRange"] = scan_range
        fields["ScanIntervalSeconds"] = interval
        rows.append(_row("PickupStuffUpdate", block, fields))
    rows.sort(key=lambda row: (str(row["sourceIni"]).casefold(), int(row["line"])))
    return rows


_AUTO_ABILITY_FIELDS = frozenset(
    {
        "specialability", "startsactive", "basemaxrangefromstartpos",
        "adjustattackmeleeposition", "maxscanrange", "minscanrange",
        "allowself", "idletimeseconds", "query", "forbiddenstatus",
    }
)
_AUTO_ABILITY_RANGE_RE = re.compile(
    r"#SUBTRACT\(\s*([A-Za-z_][A-Za-z0-9_]*)\s+([0-9]+(?:\.[0-9]*)?|\.[0-9]+)\s*\)",
    re.IGNORECASE,
)


def _auto_ability_range_field(
    assignment: SageAssignment | None, target_id: str, key: str
) -> dict[str, object] | None:
    if assignment is None:
        return None
    expression = assignment.value.strip()
    result: dict[str, object] = {**_authored_row(assignment), "expression": expression}
    if re.fullmatch(r"[0-9]+(?:\.[0-9]*)?|\.[0-9]+", expression):
        numeric = float(expression)
        result.update({"kind": "literal", "value": int(numeric) if numeric.is_integer() else numeric})
        return result
    if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", expression):
        result.update({"kind": "define", "name": expression})
        return result
    match = _AUTO_ABILITY_RANGE_RE.fullmatch(expression)
    if match is not None:
        amount = float(match.group(2))
        result.update({
            "kind": "subtract", "name": match.group(1),
            "amount": int(amount) if amount.is_integer() else amount,
        })
        return result
    raise ModuleContractError(f"{target_id} AutoAbilityBehavior {key} malformed")


def compile_auto_ability_behaviors(
    lineage: Sequence[SageObject], target_id: str
) -> list[dict[str, object]]:
    """Compile the complete effective-retail ``AutoAbilityBehavior`` grammar."""

    rows: list[dict[str, object]] = []
    for block in _behavior_blocks(lineage, "AutoAbilityBehavior"):
        if block.blocks:
            raise ModuleContractError(
                f"{target_id} AutoAbilityBehavior authors unsupported nested blocks"
            )
        grouped: dict[str, list[SageAssignment]] = {}
        for assignment in block.assignments:
            grouped.setdefault(assignment.key.casefold(), []).append(assignment)
        unknown = set(grouped) - _AUTO_ABILITY_FIELDS
        if unknown:
            raise ModuleContractError(
                f"{target_id} AutoAbilityBehavior unsupported fields: "
                + ", ".join(sorted(unknown))
            )
        duplicated = sorted(
            key for key, values in grouped.items()
            if len(values) > 1 and key != "query"
        )
        if duplicated:
            raise ModuleContractError(
                f"{target_id} AutoAbilityBehavior duplicate scalar fields: "
                + ", ".join(duplicated)
            )
        amap = {key: values[-1] for key, values in grouped.items()}
        fields: dict[str, object] = {}
        special = amap.get("specialability")
        if special is not None:
            value = _string_field(special)
            assert value is not None
            if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", str(value["value"])) is None:
                raise ModuleContractError(
                    f"{target_id} AutoAbilityBehavior SpecialAbility malformed"
                )
            fields["SpecialAbility"] = value
        for key in (
            "StartsActive", "BaseMaxRangeFromStartPos",
            "AdjustAttackMeleePosition", "AllowSelf",
        ):
            value = _yes_no_field(
                amap.get(key.casefold()), f"{target_id} AutoAbilityBehavior {key}"
            )
            if value is not None:
                fields[key] = value
        for key in ("MaxScanRange", "MinScanRange"):
            value = _auto_ability_range_field(amap.get(key.casefold()), target_id, key)
            if value is not None:
                fields[key] = value
        idle = _number_field(
            amap.get("idletimeseconds"),
            f"{target_id} AutoAbilityBehavior IdleTimeSeconds",
        )
        if idle is not None:
            if float(idle["value"]) < 0:
                raise ModuleContractError(
                    f"{target_id} AutoAbilityBehavior IdleTimeSeconds must be non-negative"
                )
            idle["seconds"] = float(idle["value"])
            idle["milliseconds"] = int(round(float(idle["value"]) * 1000.0))
            fields["IdleTimeSeconds"] = idle
        forbidden = _token_list_field(amap.get("forbiddenstatus"))
        if forbidden is not None:
            if not forbidden["value"]:
                raise ModuleContractError(
                    f"{target_id} AutoAbilityBehavior ForbiddenStatus is empty"
                )
            fields["ForbiddenStatus"] = forbidden
        queries: list[dict[str, object]] = []
        for assignment in grouped.get("query", []):
            tokens = assignment.value.split()
            if (
                len(tokens) < 2 or not tokens[0].isdigit() or int(tokens[0]) < 1
                or any(re.fullmatch(r"[+-]?[A-Za-z_][A-Za-z0-9_]*", token) is None for token in tokens[1:])
            ):
                raise ModuleContractError(
                    f"{target_id} AutoAbilityBehavior Query malformed"
                )
            queries.append({
                **_authored_row(assignment), "minimumMatches": int(tokens[0]),
                "filterTokens": tokens[1:],
            })
        if queries:
            fields["Query"] = queries
        rows.append(_row("AutoAbilityBehavior", block, fields))
    rows.sort(key=lambda row: (str(row["sourceIni"]).casefold(), int(row["line"])))
    return rows


_RESPAWN_UPDATE_FIELDS = frozenset(
    {
        "deathanim", "deathfx", "deathanimationtime", "initialspawnfx",
        "respawnanim", "respawnfx", "respawnanimationtime",
        "autorespawnatobjectfilter", "buttonimage", "respawnrules",
        "respawnentry", "respawnastemplate",
    }
)


def _respawn_clauses(
    assignment: SageAssignment,
    target_id: str,
    field: str,
    *,
    numeric_defines: Mapping[str, int | float] | None,
    numeric_define_provenance: Mapping[str, Mapping[str, object]] | None,
) -> dict[str, object]:
    clauses: dict[str, str] = {}
    clause_matches = list(
        re.finditer(
            r"(?<!\S)(AutoSpawn|Cost|Time|Health|Level):",
            assignment.value,
            re.IGNORECASE,
        )
    )
    if not clause_matches or assignment.value[: clause_matches[0].start()].strip():
        raise ModuleContractError(f"{target_id} RespawnUpdate {field} malformed")
    for ordinal, match in enumerate(clause_matches):
        key = match.group(1)
        stop = (
            clause_matches[ordinal + 1].start()
            if ordinal + 1 < len(clause_matches)
            else len(assignment.value)
        )
        value = assignment.value[match.end() : stop].strip()
        folded = key.casefold()
        if not value or folded in clauses:
            raise ModuleContractError(f"{target_id} RespawnUpdate {field} malformed")
        clauses[folded] = value
    required = (
        {"autospawn", "cost", "time", "health"}
        if field.casefold() == "respawnrules"
        else {"level", "cost", "time"}
    )
    if set(clauses) != required:
        raise ModuleContractError(
            f"{target_id} RespawnUpdate {field} requires exactly "
            + ", ".join(sorted(required))
        )
    typed: dict[str, object] = {}
    if "autospawn" in clauses:
        if clauses["autospawn"].casefold() not in {"yes", "no"}:
            raise ModuleContractError(
                f"{target_id} RespawnUpdate {field} AutoSpawn malformed"
            )
        typed["autoSpawn"] = clauses["autospawn"].casefold() == "yes"
    for key in ("level", "cost", "time"):
        if key not in clauses:
            continue
        expression = clauses[key]
        if key == "level" and not expression.isdigit():
            raise ModuleContractError(
                f"{target_id} RespawnUpdate {field} {key} malformed"
            )
        if expression.isdigit():
            numeric = int(expression)
            define_receipts: list[dict[str, object]] = []
        else:
            # Use the same bounded SAGE numeric-expression evaluator that
            # resolves GameData defines for playable descriptors. Supplying
            # the already-resolved define table as expression bodies retains
            # its nested/binary/fail-closed grammar without a second evaluator.
            from .playable_unit_compiler import _evaluated_define_body

            bodies = {
                str(name).casefold(): str(value)
                for name, value in (numeric_defines or {}).items()
                if not isinstance(value, bool) and isinstance(value, (int, float))
            }
            resolved = _evaluated_define_body(expression, bodies)
            if (
                resolved is None
                or isinstance(resolved, bool)
                or not float(resolved).is_integer()
                or float(resolved) < 0
            ):
                raise ModuleContractError(
                    f"{target_id} RespawnUpdate {field} {key} expression unresolved"
                )
            numeric = int(resolved)
            define_receipts = []
            seen: set[str] = set()
            for identifier in re.findall(r"[A-Za-z_][A-Za-z0-9_]*", expression):
                folded_identifier = identifier.casefold()
                if folded_identifier in {"add", "subtract", "multiply", "divide"} or folded_identifier in seen:
                    continue
                seen.add(folded_identifier)
                expected = bodies.get(folded_identifier)
                provenance = (
                    None
                    if numeric_define_provenance is None
                    else numeric_define_provenance.get(folded_identifier)
                )
                if expected is None or provenance is None:
                    raise ModuleContractError(
                        f"{target_id} RespawnUpdate {field} {key} define provenance unresolved: {identifier}"
                    )
                provenance_value = provenance.get("value")
                if (
                    str(provenance.get("defineId", "")).casefold() != folded_identifier
                    or not isinstance(provenance.get("sourceIni"), str)
                    or not provenance.get("sourceIni")
                    or isinstance(provenance.get("line"), bool)
                    or not isinstance(provenance.get("line"), int)
                    or int(provenance["line"]) <= 0
                    or not isinstance(provenance.get("authoredValue"), str)
                    or not provenance.get("authoredValue")
                    or isinstance(provenance_value, bool)
                    or not isinstance(provenance_value, (int, float))
                    or float(provenance_value) != float(expected)
                ):
                    raise ModuleContractError(
                        f"{target_id} RespawnUpdate {field} {key} define provenance invalid: {identifier}"
                    )
                define_receipts.append(dict(provenance))
        if key == "level" and numeric < 1:
            raise ModuleContractError(
                f"{target_id} RespawnUpdate {field} level must be positive"
            )
        typed[{"level": "level", "cost": "cost", "time": "timeMilliseconds"}[key]] = numeric
        if define_receipts:
            stem = "time" if key == "time" else key
            typed[f"{stem}Expression"] = expression
            typed[f"{stem}DefineProvenance"] = define_receipts
    if "health" in clauses:
        match = re.fullmatch(r"(\d+(?:\.\d*)?|\.\d+)%", clauses["health"])
        if match is None:
            raise ModuleContractError(
                f"{target_id} RespawnUpdate {field} Health malformed"
            )
        typed["healthPercent"] = float(match.group(1))
    return {**_authored_row(assignment), **typed}


def compile_respawn_updates(
    lineage: Sequence[SageObject],
    target_id: str,
    *,
    numeric_defines: Mapping[str, int | float] | None = None,
    numeric_define_provenance: Mapping[str, Mapping[str, object]] | None = None,
) -> list[dict[str, object]]:
    """Compile hero respawn rules, level entries, timings, FX and templates."""

    rows: list[dict[str, object]] = []
    for block in _behavior_blocks(lineage, "RespawnUpdate"):
        if block.blocks:
            raise ModuleContractError(
                f"{target_id} RespawnUpdate authors unsupported nested blocks"
            )
        grouped: dict[str, list[SageAssignment]] = {}
        for assignment in block.assignments:
            grouped.setdefault(assignment.key.casefold(), []).append(assignment)
        unknown = set(grouped) - _RESPAWN_UPDATE_FIELDS
        if unknown:
            raise ModuleContractError(
                f"{target_id} RespawnUpdate unsupported fields: "
                + ", ".join(sorted(unknown))
            )
        duplicated = sorted(
            key for key, values in grouped.items()
            if len(values) > 1 and key != "respawnentry"
        )
        if duplicated:
            raise ModuleContractError(
                f"{target_id} RespawnUpdate duplicate scalar fields: "
                + ", ".join(duplicated)
            )
        amap = {key: values[-1] for key, values in grouped.items()}
        fields: dict[str, object] = {}
        for key in (
            "DeathAnim", "DeathFX", "InitialSpawnFX", "RespawnAnim", "RespawnFX",
            "ButtonImage", "RespawnAsTemplate",
        ):
            assignment = amap.get(key.casefold())
            if assignment is not None:
                value = _string_field(assignment)
                assert value is not None
                if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", str(value["value"])) is None:
                    raise ModuleContractError(f"{target_id} RespawnUpdate {key} malformed")
                fields[key] = value
        for key in ("DeathAnimationTime", "RespawnAnimationTime"):
            value = _milliseconds_field(
                amap.get(key.casefold()), f"{target_id} RespawnUpdate {key}"
            )
            if value is not None:
                fields[key] = value
        auto_filter = _token_list_field(amap.get("autorespawnatobjectfilter"))
        if auto_filter is not None:
            if not auto_filter["value"]:
                raise ModuleContractError(
                    f"{target_id} RespawnUpdate AutoRespawnAtObjectFilter empty"
                )
            fields["AutoRespawnAtObjectFilter"] = auto_filter
        rules = amap.get("respawnrules")
        if rules is not None:
            fields["RespawnRules"] = _respawn_clauses(
                rules,
                target_id,
                "RespawnRules",
                numeric_defines=numeric_defines,
                numeric_define_provenance=numeric_define_provenance,
            )
        entries = [
            _respawn_clauses(
                item,
                target_id,
                "RespawnEntry",
                numeric_defines=numeric_defines,
                numeric_define_provenance=numeric_define_provenance,
            )
            for item in grouped.get("respawnentry", [])
        ]
        if entries:
            levels = [int(item["level"]) for item in entries]
            if len(levels) != len(set(levels)):
                raise ModuleContractError(
                    f"{target_id} RespawnUpdate duplicate RespawnEntry levels"
                )
            fields["RespawnEntry"] = entries
        required = {"DeathAnim", "AutoRespawnAtObjectFilter", "ButtonImage", "RespawnRules"}
        if not required <= set(fields):
            raise ModuleContractError(
                f"{target_id} RespawnUpdate lacks required core fields"
            )
        rows.append(_row("RespawnUpdate", block, fields))
    rows.sort(key=lambda row: (str(row["sourceIni"]).casefold(), int(row["line"])))
    return rows


def compile_dual_weapon_behaviors(
    lineage: Sequence[SageObject], target_id: str
) -> list[dict[str, object]]:
    """Compile the measured close-range weapon-switch threshold."""

    rows: list[dict[str, object]] = []
    for block in _behavior_blocks(lineage, "DualWeaponBehavior"):
        if block.blocks or len(block.assignments) != 1:
            raise ModuleContractError(f"{target_id} DualWeaponBehavior malformed shape")
        assignment = block.assignments[0]
        if assignment.key.casefold() != "switchweapononcloserangedistance":
            raise ModuleContractError(
                f"{target_id} DualWeaponBehavior unsupported field: {assignment.key}"
            )
        distance = _non_negative_expression_field(
            assignment,
            f"{target_id} DualWeaponBehavior SwitchWeaponOnCloseRangeDistance",
        )
        assert distance is not None
        rows.append(_row("DualWeaponBehavior", block, {
            "SwitchWeaponOnCloseRangeDistance": distance
        }))
    rows.sort(key=lambda row: (str(row["sourceIni"]).casefold(), int(row["line"])))
    return rows


_CASTLE_MEMBER_FIELDS = frozenset(
    {
        "countsforevacastlebreached", "storeupgradeprice", "beingbuiltsound",
        "campdestroyedownerevaevent", "campdestroyedallyevaevent",
        "campdestroyedattackerevaevent",
    }
)


def compile_castle_member_behaviors(
    lineage: Sequence[SageObject], target_id: str
) -> list[dict[str, object]]:
    """Compile all effective CastleMemberBehavior membership/presentation fields."""

    rows: list[dict[str, object]] = []
    for block in _behavior_blocks(lineage, "CastleMemberBehavior"):
        if block.blocks:
            raise ModuleContractError(
                f"{target_id} CastleMemberBehavior authors unsupported nested blocks"
            )
        amap = _assignment_map(block)
        if len(amap) != len(block.assignments):
            raise ModuleContractError(f"{target_id} CastleMemberBehavior duplicate fields")
        unknown = set(amap) - _CASTLE_MEMBER_FIELDS
        if unknown:
            raise ModuleContractError(
                f"{target_id} CastleMemberBehavior unsupported fields: "
                + ", ".join(sorted(unknown))
            )
        fields: dict[str, object] = {}
        for key in ("CountsForEvaCastleBreached", "StoreUpgradePrice"):
            value = _yes_no_field(
                amap.get(key.casefold()), f"{target_id} CastleMemberBehavior {key}"
            )
            if value is not None:
                fields[key] = value
        for key in (
            "BeingBuiltSound", "CampDestroyedOwnerEvaEvent",
            "CampDestroyedAllyEvaEvent", "CampDestroyedAttackerEvaEvent",
        ):
            assignment = amap.get(key.casefold())
            if assignment is not None:
                value = _string_field(assignment)
                assert value is not None
                if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", str(value["value"])) is None:
                    raise ModuleContractError(
                        f"{target_id} CastleMemberBehavior {key} malformed"
                    )
                fields[key] = value
        rows.append(_row("CastleMemberBehavior", block, fields))
    rows.sort(key=lambda row: (str(row["sourceIni"]).casefold(), int(row["line"])))
    return rows


_EMOTION_TRACKER_FIELDS = frozenset(
    {
        "afraidof", "alwaysafraidof", "fearscandistance", "addemotion",
        "tauntandpointdistance", "tauntandpointupdatedelay",
        "tauntandpointexcluded", "pointat", "heroscandistance",
        "quarrelprobability", "immunetofearlevel", "ignoreveterancy",
    }
)


def compile_emotion_tracker_updates(
    lineage: Sequence[SageObject], target_id: str
) -> list[dict[str, object]]:
    """Compile all emotion filters, ranges, timers and repeated emotion rows."""

    rows: list[dict[str, object]] = []
    for block in _behavior_blocks(lineage, "EmotionTrackerUpdate"):
        grouped: dict[str, list[SageAssignment]] = {}
        for assignment in block.assignments:
            grouped.setdefault(assignment.key.casefold(), []).append(assignment)
        unknown = set(grouped) - _EMOTION_TRACKER_FIELDS
        if unknown:
            raise ModuleContractError(
                f"{target_id} EmotionTrackerUpdate unsupported fields: "
                + ", ".join(sorted(unknown))
            )
        duplicated = sorted(
            key for key, values in grouped.items()
            if len(values) > 1 and key != "addemotion"
        )
        if duplicated:
            raise ModuleContractError(
                f"{target_id} EmotionTrackerUpdate duplicate scalar fields: "
                + ", ".join(duplicated)
            )
        unknown_children = [
            child.kind for child in block.blocks
            if child.kind.casefold() != "addemotion"
        ]
        if unknown_children:
            raise ModuleContractError(
                f"{target_id} EmotionTrackerUpdate unsupported nested blocks: "
                + ", ".join(sorted(unknown_children, key=str.casefold))
            )
        amap = {key: values[-1] for key, values in grouped.items()}
        fields: dict[str, object] = {}
        for key in ("AfraidOf", "AlwaysAfraidOf", "TauntAndPointExcluded", "PointAt"):
            value = _token_list_field(amap.get(key.casefold()))
            if value is not None:
                if not value["value"]:
                    raise ModuleContractError(
                        f"{target_id} EmotionTrackerUpdate {key} is empty"
                    )
                fields[key] = value
        for key in ("FearScanDistance", "TauntAndPointDistance", "HeroScanDistance"):
            value = _non_negative_expression_field(
                amap.get(key.casefold()), f"{target_id} EmotionTrackerUpdate {key}"
            )
            if value is not None:
                fields[key] = value
        delay = _milliseconds_field(
            amap.get("tauntandpointupdatedelay"),
            f"{target_id} EmotionTrackerUpdate TauntAndPointUpdateDelay",
        )
        if delay is not None:
            fields["TauntAndPointUpdateDelay"] = delay
        probability = amap.get("quarrelprobability")
        if probability is not None:
            match = re.fullmatch(r"(\d+(?:\.\d*)?|\.\d+)%", probability.value.strip())
            if match is None:
                raise ModuleContractError(
                    f"{target_id} EmotionTrackerUpdate QuarrelProbability malformed"
                )
            percent = float(match.group(1))
            if percent > 100:
                raise ModuleContractError(
                    f"{target_id} EmotionTrackerUpdate QuarrelProbability exceeds 100%"
                )
            fields["QuarrelProbability"] = {
                **_authored_row(probability), "percent": percent,
                "fraction": percent / 100.0,
            }
        immune = _integer_assignment_field(
            amap.get("immunetofearlevel"),
            f"{target_id} EmotionTrackerUpdate ImmuneToFearLevel", minimum=0,
        )
        if immune is not None:
            fields["ImmuneToFearLevel"] = immune
        ignore = _yes_no_field(
            amap.get("ignoreveterancy"),
            f"{target_id} EmotionTrackerUpdate IgnoreVeterancy",
        )
        if ignore is not None:
            fields["IgnoreVeterancy"] = ignore
        emotions: list[dict[str, object]] = []
        for assignment in grouped.get("addemotion", []):
            name = assignment.value.strip()
            if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", name) is None:
                raise ModuleContractError(
                    f"{target_id} EmotionTrackerUpdate AddEmotion malformed"
                )
            emotions.append({**_authored_row(assignment), "name": name, "override": False})
        for child in block.blocks:
            match = re.fullmatch(
                r"\s*AddEmotion\s*=\s*OVERRIDE\s+([A-Za-z_][A-Za-z0-9_]*)\s*",
                child.raw_header, re.IGNORECASE,
            )
            if match is None or child.blocks:
                raise ModuleContractError(
                    f"{target_id} EmotionTrackerUpdate AddEmotion override malformed"
                )
            child_map = _assignment_map(child)
            if set(child_map) - {"duration"} or len(child_map) != len(child.assignments):
                raise ModuleContractError(
                    f"{target_id} EmotionTrackerUpdate AddEmotion override fields malformed"
                )
            emotion: dict[str, object] = {
                "authored": child.raw_header.split("=", 1)[1].strip(),
                "name": match.group(1), "override": True,
                "sourceIni": child.source_virtual_path, "line": child.line,
            }
            duration = _milliseconds_field(
                child_map.get("duration"),
                f"{target_id} EmotionTrackerUpdate AddEmotion Duration",
            )
            if duration is not None:
                emotion["Duration"] = duration
            emotions.append(emotion)
        if emotions:
            emotions.sort(key=lambda item: (str(item["sourceIni"]).casefold(), int(item["line"])))
            fields["AddEmotion"] = emotions
        rows.append(_row("EmotionTrackerUpdate", block, fields))
    rows.sort(key=lambda row: (str(row["sourceIni"]).casefold(), int(row["line"])))
    return rows


def compile_siege_docking_behaviors(
    lineage: Sequence[SageObject], target_id: str
) -> list[dict[str, object]]:
    """Preserve the fieldless authored siege-docking marker."""

    rows: list[dict[str, object]] = []
    for block in _behavior_blocks(lineage, "SiegeDockingBehavior"):
        if block.assignments or block.blocks:
            raise ModuleContractError(f"{target_id} SiegeDockingBehavior must be empty")
        rows.append(_row("SiegeDockingBehavior", block, {}))
    rows.sort(key=lambda row: (str(row["sourceIni"]).casefold(), int(row["line"])))
    return rows


def compile_deletion_updates(
    lineage: Sequence[SageObject], target_id: str
) -> list[dict[str, object]]:
    """Compile the complete authored deletion lifetime bounds."""

    rows: list[dict[str, object]] = []
    for block in _behavior_blocks(lineage, "DeletionUpdate"):
        if block.blocks:
            raise ModuleContractError(f"{target_id} DeletionUpdate has nested blocks")
        amap = _assignment_map(block)
        if len(amap) != len(block.assignments) or set(amap) != {"minlifetime", "maxlifetime"}:
            raise ModuleContractError(
                f"{target_id} DeletionUpdate requires exactly MinLifetime and MaxLifetime"
            )
        def deletion_bound(assignment: SageAssignment, key: str) -> dict[str, object]:
            if assignment.value.strip() == "-1":
                return {**_authored_row(assignment), "indefinite": True}
            value = _lifetime_expression_field(
                assignment, f"{target_id} DeletionUpdate {key}"
            )
            assert value is not None
            value["indefinite"] = False
            return value

        minimum = deletion_bound(amap["minlifetime"], "MinLifetime")
        maximum = deletion_bound(amap["maxlifetime"], "MaxLifetime")
        assert minimum is not None and maximum is not None
        if "milliseconds" in minimum and "milliseconds" in maximum and minimum["milliseconds"] > maximum["milliseconds"]:
            raise ModuleContractError(f"{target_id} DeletionUpdate lifetime bounds inverted")
        rows.append(_row("DeletionUpdate", block, {
            "MinLifetime": minimum, "MaxLifetime": maximum
        }))
    rows.sort(key=lambda row: (str(row["sourceIni"]).casefold(), int(row["line"])))
    return rows


def compile_refund_die_behaviors(
    lineage: Sequence[SageObject], target_id: str
) -> list[dict[str, object]]:
    """Compile refund percentage and optional upgrade/building prerequisites."""

    allowed = {"upgraderequired", "buildingrequired", "refundpercent"}
    rows: list[dict[str, object]] = []
    for block in _behavior_blocks(lineage, "RefundDie"):
        if block.blocks:
            raise ModuleContractError(f"{target_id} RefundDie has nested blocks")
        amap = _assignment_map(block)
        if len(amap) != len(block.assignments) or set(amap) - allowed:
            raise ModuleContractError(f"{target_id} RefundDie duplicate/unsupported fields")
        refund = amap.get("refundpercent")
        if refund is None:
            raise ModuleContractError(f"{target_id} RefundDie requires RefundPercent")
        match = re.fullmatch(r"(\d+(?:\.\d*)?|\.\d+)%", refund.value.strip())
        if match is None or float(match.group(1)) > 100:
            raise ModuleContractError(f"{target_id} RefundDie RefundPercent malformed")
        percent = float(match.group(1))
        fields: dict[str, object] = {
            "RefundPercent": {**_authored_row(refund), "percent": percent, "fraction": percent / 100.0}
        }
        upgrade = amap.get("upgraderequired")
        if upgrade is not None:
            value = _string_field(upgrade)
            assert value is not None
            if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", str(value["value"])) is None:
                raise ModuleContractError(f"{target_id} RefundDie UpgradeRequired malformed")
            fields["UpgradeRequired"] = value
        building = _token_list_field(amap.get("buildingrequired"))
        if building is not None:
            if not building["value"]:
                raise ModuleContractError(f"{target_id} RefundDie BuildingRequired empty")
            fields["BuildingRequired"] = building
        rows.append(_row("RefundDie", block, fields))
    rows.sort(key=lambda row: (str(row["sourceIni"]).casefold(), int(row["line"])))
    return rows


_FIRE_WEAPON_UPDATE_FIELDS = frozenset(
    {"chargingmodetrigger", "aliveonly", "heromodetrigger"}
)


def compile_fire_weapon_updates(
    lineage: Sequence[SageObject], target_id: str
) -> list[dict[str, object]]:
    """Compile repeated weapon firing nuggets and activation policy."""

    rows: list[dict[str, object]] = []
    for block in _behavior_blocks(lineage, "FireWeaponUpdate"):
        amap = _assignment_map(block)
        if len(amap) != len(block.assignments) or set(amap) - _FIRE_WEAPON_UPDATE_FIELDS:
            raise ModuleContractError(f"{target_id} FireWeaponUpdate duplicate/unsupported fields")
        fields: dict[str, object] = {}
        for key in ("ChargingModeTrigger", "AliveOnly", "HeroModeTrigger"):
            value = _yes_no_field(
                amap.get(key.casefold()), f"{target_id} FireWeaponUpdate {key}"
            )
            if value is not None:
                fields[key] = value
        nuggets: list[dict[str, object]] = []
        for child in block.blocks:
            if child.kind.casefold() != "fireweaponnugget" or child.blocks:
                raise ModuleContractError(
                    f"{target_id} FireWeaponUpdate unsupported nested block"
                )
            child_map = _assignment_map(child)
            allowed = {"weaponname", "firedelay", "oneshot", "offset"}
            if len(child_map) != len(child.assignments) or set(child_map) - allowed:
                raise ModuleContractError(
                    f"{target_id} FireWeaponUpdate FireWeaponNugget malformed fields"
                )
            weapon = _string_field(child_map.get("weaponname"))
            one_shot = _yes_no_field(
                child_map.get("oneshot"), f"{target_id} FireWeaponUpdate OneShot"
            )
            if weapon is None or one_shot is None or re.fullmatch(
                r"[A-Za-z_][A-Za-z0-9_]*", str(weapon["value"])
            ) is None:
                raise ModuleContractError(
                    f"{target_id} FireWeaponUpdate nugget requires WeaponName and OneShot"
                )
            nugget: dict[str, object] = {
                "WeaponName": weapon, "OneShot": one_shot,
                "sourceIni": child.source_virtual_path, "line": child.line,
            }
            delay = _milliseconds_field(
                child_map.get("firedelay"), f"{target_id} FireWeaponUpdate FireDelay"
            )
            if delay is not None:
                nugget["FireDelay"] = delay
            offset = _coord_field(
                child_map.get("offset"), f"{target_id} FireWeaponUpdate Offset"
            )
            if offset is not None:
                nugget["Offset"] = offset
            nuggets.append(nugget)
        if not nuggets:
            raise ModuleContractError(f"{target_id} FireWeaponUpdate requires nuggets")
        fields["FireWeaponNugget"] = nuggets
        rows.append(_row("FireWeaponUpdate", block, fields))
    rows.sort(key=lambda row: (str(row["sourceIni"]).casefold(), int(row["line"])))
    return rows


def compile_fire_spread_updates(
    lineage: Sequence[SageObject], target_id: str
) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for block in _behavior_blocks(lineage, "FireSpreadUpdate"):
        if block.blocks:
            raise ModuleContractError(f"{target_id} FireSpreadUpdate has nested blocks")
        amap = _assignment_map(block)
        required = {"minspreaddelay", "maxspreaddelay", "spreadtryrange"}
        if len(amap) != len(block.assignments) or set(amap) != required:
            raise ModuleContractError(f"{target_id} FireSpreadUpdate malformed fields")
        minimum = _milliseconds_field(amap["minspreaddelay"], f"{target_id} FireSpreadUpdate MinSpreadDelay")
        maximum = _milliseconds_field(amap["maxspreaddelay"], f"{target_id} FireSpreadUpdate MaxSpreadDelay")
        spread_range = _number_field(amap["spreadtryrange"], f"{target_id} FireSpreadUpdate SpreadTryRange")
        assert minimum is not None and maximum is not None and spread_range is not None
        if minimum["milliseconds"] > maximum["milliseconds"] or float(spread_range["value"]) < 0:
            raise ModuleContractError(f"{target_id} FireSpreadUpdate invalid bounds")
        rows.append(_row("FireSpreadUpdate", block, {
            "MinSpreadDelay": minimum, "MaxSpreadDelay": maximum,
            "SpreadTryRange": spread_range,
        }))
    rows.sort(key=lambda row: (str(row["sourceIni"]).casefold(), int(row["line"])))
    return rows


_ATTACH_UPDATE_FIELDS = frozenset(
    {"objectfilter", "scanrange", "parentstatus", "alwaysteleport", "anchortotopofgeometry", "parentownerattachmentevaevent", "parentenemyattachmentevaevent", "parentownerdiedevaevent"}
)


def compile_attach_updates(lineage: Sequence[SageObject], target_id: str) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for block in _behavior_blocks(lineage, "AttachUpdate"):
        if block.blocks:
            raise ModuleContractError(f"{target_id} AttachUpdate has nested blocks")
        amap = _assignment_map(block)
        if len(amap) != len(block.assignments) or set(amap) - _ATTACH_UPDATE_FIELDS:
            raise ModuleContractError(f"{target_id} AttachUpdate duplicate/unsupported fields")
        fields: dict[str, object] = {}
        object_filter = _token_list_field(amap.get("objectfilter"))
        if object_filter is None or not object_filter["value"]:
            raise ModuleContractError(f"{target_id} AttachUpdate requires ObjectFilter")
        fields["ObjectFilter"] = object_filter
        scan_range = _number_field(amap.get("scanrange"), f"{target_id} AttachUpdate ScanRange")
        if scan_range is not None:
            if float(scan_range["value"]) < 0: raise ModuleContractError(f"{target_id} AttachUpdate ScanRange negative")
            fields["ScanRange"] = scan_range
        for key in ("AlwaysTeleport", "AnchorToTopOfGeometry"):
            value = _yes_no_field(amap.get(key.casefold()), f"{target_id} AttachUpdate {key}")
            if value is not None: fields[key] = value
        parent = _token_list_field(amap.get("parentstatus"))
        if parent is not None:
            if not parent["value"]: raise ModuleContractError(f"{target_id} AttachUpdate ParentStatus empty")
            fields["ParentStatus"] = parent
        for key in ("ParentOwnerAttachmentEvaEvent", "ParentEnemyAttachmentEvaEvent", "ParentOwnerDiedEvaEvent"):
            assignment = amap.get(key.casefold())
            if assignment is not None:
                value = _string_field(assignment); assert value is not None
                if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", str(value["value"])) is None: raise ModuleContractError(f"{target_id} AttachUpdate {key} malformed")
                fields[key] = value
        rows.append(_row("AttachUpdate", block, fields))
    rows.sort(key=lambda row: (str(row["sourceIni"]).casefold(), int(row["line"])))
    return rows


_INVISIBILITY_FIELDS = frozenset({"startsactive", "updateperiod", "requiredupgrades", "forbiddenupgrades", "unitspecificsoundnametouseasvoicemovetostealthyarea", "unitspecificsoundnametouseasvoiceenterstatemovetostealthyarea", "broadcast", "broadcastrange", "broadcastobjectfilter"})
_INVISIBILITY_NUGGET_FIELDS = frozenset({"invisibilitytype", "forbiddenconditions", "becomestealthedfx", "exitstealthfx", "options", "detectionrange", "forbiddenweaponconditions", "hintdetectableconditions"})


def compile_invisibility_updates(lineage: Sequence[SageObject], target_id: str) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for block in _behavior_blocks(lineage, "InvisibilityUpdate"):
        amap = _assignment_map(block)
        if len(amap) != len(block.assignments) or set(amap) - _INVISIBILITY_FIELDS:
            raise ModuleContractError(f"{target_id} InvisibilityUpdate duplicate/unsupported fields")
        fields: dict[str, object] = {}
        starts = _yes_no_field(amap.get("startsactive"), f"{target_id} InvisibilityUpdate StartsActive")
        period = _milliseconds_field(amap.get("updateperiod"), f"{target_id} InvisibilityUpdate UpdatePeriod")
        if starts is None or period is None: raise ModuleContractError(f"{target_id} InvisibilityUpdate requires StartsActive and UpdatePeriod")
        fields.update({"StartsActive": starts, "UpdatePeriod": period})
        for key in ("RequiredUpgrades", "ForbiddenUpgrades", "BroadcastObjectFilter"):
            value = _token_list_field(amap.get(key.casefold()))
            if value is not None:
                if not value["value"]: raise ModuleContractError(f"{target_id} InvisibilityUpdate {key} empty")
                fields[key] = value
        broadcast = _yes_no_field(amap.get("broadcast"), f"{target_id} InvisibilityUpdate Broadcast")
        if broadcast is not None: fields["Broadcast"] = broadcast
        brange = _non_negative_expression_field(amap.get("broadcastrange"), f"{target_id} InvisibilityUpdate BroadcastRange")
        if brange is not None: fields["BroadcastRange"] = brange
        for key in ("UnitSpecificSoundNameToUseAsVoiceMoveToStealthyArea", "UnitSpecificSoundNameToUseAsVoiceEnterStateMoveToStealthyArea"):
            value = _string_field(amap.get(key.casefold()))
            if value is not None:
                if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", str(value["value"])) is None: raise ModuleContractError(f"{target_id} InvisibilityUpdate {key} malformed")
                fields[key] = value
        nuggets: list[dict[str, object]] = []
        for child in block.blocks:
            if child.kind.casefold() != "invisibilitynugget" or child.blocks: raise ModuleContractError(f"{target_id} InvisibilityUpdate nested block malformed")
            cmap = _assignment_map(child)
            if len(cmap) != len(child.assignments) or set(cmap) - _INVISIBILITY_NUGGET_FIELDS: raise ModuleContractError(f"{target_id} InvisibilityUpdate nugget fields malformed")
            invis_type = _string_field(cmap.get("invisibilitytype"))
            if invis_type is None or str(invis_type["value"]).upper() not in {"CAMOUFLAGE", "STEALTH"}: raise ModuleContractError(f"{target_id} InvisibilityUpdate InvisibilityType malformed")
            nugget: dict[str, object] = {"InvisibilityType": invis_type, "sourceIni": child.source_virtual_path, "line": child.line}
            for key in ("ForbiddenConditions", "Options", "ForbiddenWeaponConditions", "HintDetectableConditions"):
                value = _token_list_field(cmap.get(key.casefold()))
                if value is not None:
                    if not value["value"]: raise ModuleContractError(f"{target_id} InvisibilityUpdate {key} empty")
                    nugget[key] = value
            detection = _non_negative_expression_field(cmap.get("detectionrange"), f"{target_id} InvisibilityUpdate DetectionRange")
            if detection is not None: nugget["DetectionRange"] = detection
            for key in ("BecomeStealthedFX", "ExitStealthFX"):
                value = _string_field(cmap.get(key.casefold()))
                if value is not None: nugget[key] = value
            nuggets.append(nugget)
        if len(nuggets) != 1: raise ModuleContractError(f"{target_id} InvisibilityUpdate requires exactly one nugget")
        fields["InvisibilityNugget"] = nuggets
        rows.append(_row("InvisibilityUpdate", block, fields))
    rows.sort(key=lambda row: (str(row["sourceIni"]).casefold(), int(row["line"])))
    return rows


_CLEARANCE_SLOW_DEATH_FIELDS = frozenset({"deathtypes", "sinkdelay", "sinkrate", "destructiondelay", "deathflags", "decaybegintime", "minkillerangle", "maxkillerangle", "probabilitymodifier", "clearancegeometry", "clearancegeometrymajorradius", "clearancegeometryminorradius", "clearancegeometryheight", "clearancegeometryissmall", "clearancegeometryoffset", "clearancemaxheight", "clearancemaxheightfraction", "clearanceminheight", "clearanceminheightfraction", "fx", "shadowwhendead", "damageamountrequired"})
_CLEARANCE_REPEATED = frozenset({"deathflags", "probabilitymodifier", "clearanceminheightfraction"})


def compile_clearance_testing_slow_death_behaviors(lineage: Sequence[SageObject], target_id: str) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for block in _behavior_blocks(lineage, "ClearanceTestingSlowDeathBehavior"):
        if block.blocks: raise ModuleContractError(f"{target_id} ClearanceTestingSlowDeathBehavior nested blocks")
        grouped: dict[str, list[SageAssignment]] = {}
        for assignment in block.assignments: grouped.setdefault(assignment.key.casefold(), []).append(assignment)
        if set(grouped) - _CLEARANCE_SLOW_DEATH_FIELDS: raise ModuleContractError(f"{target_id} ClearanceTestingSlowDeathBehavior unsupported fields")
        if any(len(v) > 1 and k not in _CLEARANCE_REPEATED for k, v in grouped.items()): raise ModuleContractError(f"{target_id} ClearanceTestingSlowDeathBehavior duplicate scalar")
        amap = {k: v[-1] for k, v in grouped.items()}
        fields = _die_mux_death_types(block, "ClearanceTestingSlowDeathBehavior")
        for key in ("SinkDelay", "DestructionDelay", "DecayBeginTime"):
            value = _milliseconds_field(amap.get(key.casefold()), f"{target_id} ClearanceTestingSlowDeathBehavior {key}")
            if value is not None: fields[key] = value
        for key in ("SinkRate", "MinKillerAngle", "MaxKillerAngle", "ClearanceGeometryMajorRadius", "ClearanceGeometryMinorRadius", "ClearanceGeometryHeight", "ClearanceMaxHeight", "ClearanceMinHeight", "DamageAmountRequired"):
            value = _number_field(amap.get(key.casefold()), f"{target_id} ClearanceTestingSlowDeathBehavior {key}")
            if value is not None: fields[key] = value
        for key in ("ClearanceGeometryIsSmall", "ShadowWhenDead"):
            value = _yes_no_field(amap.get(key.casefold()), f"{target_id} ClearanceTestingSlowDeathBehavior {key}")
            if value is not None: fields[key] = value
        geometry = _string_field(amap.get("clearancegeometry"))
        if geometry is not None:
            if str(geometry["value"]).casefold() != "box": raise ModuleContractError(f"{target_id} ClearanceTestingSlowDeathBehavior geometry malformed")
            fields["ClearanceGeometry"] = geometry
        offset = _coord_field(amap.get("clearancegeometryoffset"), f"{target_id} ClearanceTestingSlowDeathBehavior ClearanceGeometryOffset")
        if offset is not None: fields["ClearanceGeometryOffset"] = offset
        for field in ("DeathFlags", "ProbabilityModifier", "ClearanceMinHeightFraction"):
            authored = grouped.get(field.casefold(), [])
            if authored:
                if field == "DeathFlags":
                    values = [_string_field(item) for item in authored]
                    if any(value is None or len(str(value["value"]).split()) != 1 for value in values): raise ModuleContractError(f"{target_id} ClearanceTestingSlowDeathBehavior DeathFlags malformed")
                else:
                    values = [_number_field(item, f"{target_id} ClearanceTestingSlowDeathBehavior {field}") for item in authored]
                fields[field] = values
        max_fraction = _number_field(amap.get("clearancemaxheightfraction"), f"{target_id} ClearanceTestingSlowDeathBehavior ClearanceMaxHeightFraction")
        if max_fraction is not None: fields["ClearanceMaxHeightFraction"] = max_fraction
        fx = amap.get("fx")
        if fx is not None:
            tokens = fx.value.split()
            if len(tokens) != 2 or tokens[0].upper() != "INITIAL": raise ModuleContractError(f"{target_id} ClearanceTestingSlowDeathBehavior FX malformed")
            fields["FX"] = {**_authored_row(fx), "phase": "INITIAL", "event": tokens[1]}
        required = {"SinkDelay", "SinkRate", "DestructionDelay", "ClearanceGeometry", "ClearanceGeometryMajorRadius", "ClearanceGeometryMinorRadius", "ClearanceGeometryHeight", "ClearanceGeometryIsSmall", "ClearanceGeometryOffset", "ClearanceMaxHeight"}
        if not required <= set(fields): raise ModuleContractError(f"{target_id} ClearanceTestingSlowDeathBehavior lacks required fields")
        rows.append(_row("ClearanceTestingSlowDeathBehavior", block, fields))
    rows.sort(key=lambda row: (str(row["sourceIni"]).casefold(), int(row["line"])))
    return rows


def compile_squish_collides(lineage: Sequence[SageObject], target_id: str) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for block in _behavior_blocks(lineage, "SquishCollide"):
        if block.assignments or block.blocks: raise ModuleContractError(f"{target_id} SquishCollide must be empty")
        rows.append(_row("SquishCollide", block, {}))
    rows.sort(key=lambda row: (str(row["sourceIni"]).casefold(), int(row["line"])))
    return rows


_PRODUCTION_UPDATE_FIELDS = frozenset({"givenoxp", "numdooranimations", "dooropeningtime", "doorwaitopentime", "doorclosetime", "constructioncompleteduration", "unitinvulnerabletime", "veteranunitsfromveteranfactory", "setbonusmodelconditiononspeedbonus", "bonusfortype", "speedbonusaudioloop", "maxqueueentries"})
_PRODUCTION_MODIFIER_FIELDS = frozenset({"requiredupgrade", "costmultiplier", "modifierfilter", "timemultiplier", "heropurchase", "herorevive"})


def compile_production_updates(lineage: Sequence[SageObject], target_id: str) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for block in _behavior_blocks(lineage, "ProductionUpdate"):
        amap = _assignment_map(block)
        if len(amap) != len(block.assignments) or set(amap) - _PRODUCTION_UPDATE_FIELDS: raise ModuleContractError(f"{target_id} ProductionUpdate duplicate/unsupported fields")
        fields: dict[str, object] = {}
        for key in ("GiveNoXP", "VeteranUnitsFromVeteranFactory", "SetBonusModelConditionOnSpeedBonus"):
            value = _yes_no_field(amap.get(key.casefold()), f"{target_id} ProductionUpdate {key}")
            if value is not None: fields[key] = value
        for key in ("NumDoorAnimations", "MaxQueueEntries"):
            value = _integer_assignment_field(amap.get(key.casefold()), f"{target_id} ProductionUpdate {key}", minimum=0)
            if value is not None: fields[key] = value
        for key in ("DoorOpeningTime", "DoorWaitOpenTime", "DoorCloseTime", "ConstructionCompleteDuration", "UnitInvulnerableTime"):
            value = _milliseconds_field(amap.get(key.casefold()), f"{target_id} ProductionUpdate {key}")
            if value is not None: fields[key] = value
        for key in ("BonusForType", "SpeedBonusAudioLoop"):
            value = _string_field(amap.get(key.casefold()))
            if value is not None:
                if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", str(value["value"])) is None: raise ModuleContractError(f"{target_id} ProductionUpdate {key} malformed")
                fields[key] = value
        modifiers: list[dict[str, object]] = []
        for child in block.blocks:
            if child.kind.casefold() != "productionmodifier" or child.blocks: raise ModuleContractError(f"{target_id} ProductionUpdate nested block malformed")
            cmap = _assignment_map(child)
            if len(cmap) != len(child.assignments) or set(cmap) - _PRODUCTION_MODIFIER_FIELDS: raise ModuleContractError(f"{target_id} ProductionModifier fields malformed")
            modifier: dict[str, object] = {"sourceIni": child.source_virtual_path, "line": child.line}
            required = _string_field(cmap.get("requiredupgrade"))
            if required is None: raise ModuleContractError(f"{target_id} ProductionModifier requires RequiredUpgrade")
            modifier["RequiredUpgrade"] = required
            for key in ("CostMultiplier", "TimeMultiplier"):
                value = _number_field(cmap.get(key.casefold()), f"{target_id} ProductionModifier {key}")
                if value is not None:
                    if float(value["value"]) < 0: raise ModuleContractError(f"{target_id} ProductionModifier {key} negative")
                    modifier[key] = value
            filter_value = _token_list_field(cmap.get("modifierfilter"))
            if filter_value is not None: modifier["ModifierFilter"] = filter_value
            for key in ("HeroPurchase", "HeroRevive"):
                value = _yes_no_field(cmap.get(key.casefold()), f"{target_id} ProductionModifier {key}")
                if value is not None: modifier[key] = value
            modifiers.append(modifier)
        if modifiers: fields["ProductionModifier"] = modifiers
        rows.append(_row("ProductionUpdate", block, fields))
    rows.sort(key=lambda row: (str(row["sourceIni"]).casefold(), int(row["line"])))
    return rows


_GETTING_BUILT_FIELDS = frozenset({"workername", "spawntimer", "selfbuildingloop", "selfrepairfromdamageloop", "selfrepairfromrubbleloop", "rebuildtimeseconds", "evilworkername", "testfaction", "rebuildwhendead", "usespawntimerwithoutworker", "disallowrebuildrange", "disallowrebuildfilter"})


def _seconds_expression_field(assignment: SageAssignment | None, target_id: str, key: str, *, allow_negative_one: bool = False) -> dict[str, object] | None:
    if assignment is None: return None
    expression = assignment.value.strip()
    result: dict[str, object] = {**_authored_row(assignment), "expression": expression}
    if allow_negative_one and expression in {"-1", "-1.0"}:
        result["disabled"] = True
        return result
    if re.fullmatch(r"[0-9]+(?:\.[0-9]*)?|\.[0-9]+", expression):
        seconds = float(expression); result.update({"seconds": seconds, "milliseconds": int(round(seconds * 1000.0)), "disabled": False}); return result
    if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", expression):
        result.update({"define": expression, "disabled": False}); return result
    raise ModuleContractError(f"{target_id} GettingBuiltBehavior {key} malformed")


def compile_getting_built_behaviors(lineage: Sequence[SageObject], target_id: str) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for block in _behavior_blocks(lineage, "GettingBuiltBehavior"):
        if block.blocks: raise ModuleContractError(f"{target_id} GettingBuiltBehavior nested blocks")
        amap = _assignment_map(block)
        if len(amap) != len(block.assignments) or set(amap) - _GETTING_BUILT_FIELDS: raise ModuleContractError(f"{target_id} GettingBuiltBehavior duplicate/unsupported fields")
        fields: dict[str, object] = {}
        for key in ("WorkerName", "EvilWorkerName", "SelfBuildingLoop", "SelfRepairFromDamageLoop", "SelfRepairFromRubbleLoop"):
            value = _string_field(amap.get(key.casefold()))
            if value is not None:
                if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", str(value["value"])) is None: raise ModuleContractError(f"{target_id} GettingBuiltBehavior {key} malformed")
                fields[key] = value
        spawn = _seconds_expression_field(amap.get("spawntimer"), target_id, "SpawnTimer", allow_negative_one=True)
        if spawn is not None: fields["SpawnTimer"] = spawn
        rebuild = _seconds_expression_field(amap.get("rebuildtimeseconds"), target_id, "RebuildTimeSeconds")
        if rebuild is not None: fields["RebuildTimeSeconds"] = rebuild
        for key in ("TestFaction", "RebuildWhenDead", "UseSpawnTimerWithoutWorker"):
            value = _yes_no_field(amap.get(key.casefold()), f"{target_id} GettingBuiltBehavior {key}")
            if value is not None: fields[key] = value
        disallow_range = _number_field(amap.get("disallowrebuildrange"), f"{target_id} GettingBuiltBehavior DisallowRebuildRange")
        if disallow_range is not None:
            if float(disallow_range["value"]) < 0: raise ModuleContractError(f"{target_id} GettingBuiltBehavior DisallowRebuildRange negative")
            fields["DisallowRebuildRange"] = disallow_range
        disallow_filter = _token_list_field(amap.get("disallowrebuildfilter"))
        if disallow_filter is not None:
            if not disallow_filter["value"]: raise ModuleContractError(f"{target_id} GettingBuiltBehavior DisallowRebuildFilter empty")
            fields["DisallowRebuildFilter"] = disallow_filter
        rows.append(_row("GettingBuiltBehavior", block, fields))
    rows.sort(key=lambda row: (str(row["sourceIni"]).casefold(), int(row["line"])))
    return rows


_AI_SPECIAL_POWER_FIELDS = frozenset({"commandbuttonname", "specialpoweraitype", "specialpowerradius", "specialpowerrange", "spellmakesastructure", "randomizetargetlocation"})


def compile_ai_special_power_updates(lineage: Sequence[SageObject], target_id: str) -> list[dict[str, object]]:
    """Compile the complete effective-retail AI special-power routing grammar."""

    rows: list[dict[str, object]] = []
    for block in _behavior_blocks(lineage, "AISpecialPowerUpdate"):
        if block.blocks:
            raise ModuleContractError(f"{target_id} AISpecialPowerUpdate has nested blocks")
        grouped: dict[str, list[SageAssignment]] = {}
        for assignment in block.assignments:
            grouped.setdefault(assignment.key.casefold(), []).append(assignment)
        if set(grouped) - _AI_SPECIAL_POWER_FIELDS:
            raise ModuleContractError(f"{target_id} AISpecialPowerUpdate unsupported fields")
        if any(len(assignments) > 1 for assignments in grouped.values()):
            raise ModuleContractError(
                f"{target_id} AISpecialPowerUpdate duplicate scalar fields"
            )
        amap = {key: assignments[0] for key, assignments in grouped.items()}
        fields: dict[str, object] = {}
        command = _string_field(amap.get("commandbuttonname"))
        if command is None or re.fullmatch(
            r"[A-Za-z_][A-Za-z0-9_]*", str(command["value"])
        ) is None:
            raise ModuleContractError(
                f"{target_id} AISpecialPowerUpdate requires valid CommandButtonName"
            )
        fields["CommandButtonName"] = command
        ai_type = _string_field(amap.get("specialpoweraitype"))
        if ai_type is None or re.fullmatch(
            r"[A-Za-z_][A-Za-z0-9_]*", str(ai_type["value"])
        ) is None:
            raise ModuleContractError(
                f"{target_id} AISpecialPowerUpdate requires valid SpecialPowerAIType"
            )
        fields["SpecialPowerAIType"] = ai_type
        for key in ("SpecialPowerRadius", "SpecialPowerRange"):
            value = _non_negative_expression_field(
                amap.get(key.casefold()), f"{target_id} AISpecialPowerUpdate {key}"
            )
            if value is not None:
                fields[key] = value
        for key in ("SpellMakesAStructure", "RandomizeTargetLocation"):
            value = _yes_no_field(
                amap.get(key.casefold()), f"{target_id} AISpecialPowerUpdate {key}"
            )
            if value is not None:
                fields[key] = value
        row = _row("AISpecialPowerUpdate", block, fields)
        row["effectGraph"] = {
            "kind": "ai-special-power-routing",
            "commandButtonId": str(command["value"]),
            "specialPowerAIType": str(ai_type["value"]),
            "sourceIni": block.source_virtual_path,
            "line": block.line,
        }
        rows.append(row)
    rows.sort(key=lambda row: (str(row["sourceIni"]).casefold(), int(row["line"])))
    return rows


_BUILDING_BEHAVIOR_FIELDS = frozenset(
    {"nightwindowname", "firewindowname", "glowwindowname", "firename"}
)


def compile_building_behaviors(lineage: Sequence[SageObject], target_id: str) -> list[dict[str, object]]:
    """Compile building window/model subobject presentation bindings."""

    rows: list[dict[str, object]] = []
    for block in _behavior_blocks(lineage, "BuildingBehavior"):
        if block.blocks:
            raise ModuleContractError(f"{target_id} BuildingBehavior has nested blocks")
        grouped: dict[str, list[SageAssignment]] = {}
        for assignment in block.assignments:
            grouped.setdefault(assignment.key.casefold(), []).append(assignment)
        if set(grouped) - _BUILDING_BEHAVIOR_FIELDS:
            raise ModuleContractError(f"{target_id} BuildingBehavior unsupported fields")
        if any(len(values) > 1 and key != "firename" for key, values in grouped.items()):
            raise ModuleContractError(f"{target_id} BuildingBehavior duplicate scalar fields")
        fields: dict[str, object] = {}
        for key in ("NightWindowName", "FireWindowName", "GlowWindowName"):
            authored = grouped.get(key.casefold(), [])
            if authored:
                value = _token_list_field(authored[0])
                assert value is not None
                if not value["value"]:
                    raise ModuleContractError(f"{target_id} BuildingBehavior {key} empty")
                fields[key] = value
        fires: list[dict[str, object]] = []
        for assignment in grouped.get("firename", []):
            value = _string_field(assignment)
            assert value is not None
            if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", str(value["value"])) is None:
                raise ModuleContractError(f"{target_id} BuildingBehavior FireName malformed")
            fires.append(value)
        if fires:
            fields["FireName"] = fires
        rows.append(_row("BuildingBehavior", block, fields))
    rows.sort(key=lambda row: (str(row["sourceIni"]).casefold(), int(row["line"])))
    return rows


_QUEUE_EXIT_FIELDS = frozenset({"unitcreatepoint", "placementviewangle", "naturalrallypoint", "exitdelay", "initialburst", "allowairbornecreation", "usereturntoformation", "noexitpath", "canrallytoslaughter"})
_QUEUE_EXECUTABLE_FIELDS = frozenset(
    {"UnitCreatePoint", "NaturalRallyPoint", "ExitDelay", "PlacementViewAngle", "NoExitPath"}
)


def _queue_exit_row_has_closed_runtime(fields: Mapping[str, object]) -> bool:
    if not set(fields) <= _QUEUE_EXECUTABLE_FIELDS:
        return False
    points = fields.get("UnitCreatePoint")
    if not isinstance(points, list) or not points:
        return False
    for key in ("UnitCreatePoint", "NaturalRallyPoint"):
        values = fields.get(key, [])
        if not isinstance(values, list):
            return False
        for value in values:
            if (
                not isinstance(value, Mapping)
                or value.get("validNumeric") is not True
                or not isinstance(value.get("value"), Mapping)
            ):
                return False
    return True


def _queue_coord_field(assignment: SageAssignment, target_id: str, key: str) -> dict[str, object]:
    """Parse Coord3D while explicitly retaining retail's one malformed X token."""

    match = re.fullmatch(r"\s*X\s*:\s*(\S+)\s+Y\s*:\s*(\S+)\s+Z\s*:\s*(\S+)\s*", assignment.value, re.IGNORECASE)
    if match is None:
        raise ModuleContractError(f"{target_id} QueueProductionExitUpdate {key} malformed")
    tokens = match.groups(); result: dict[str, object] = {**_authored_row(assignment), "components": {"x": tokens[0], "y": tokens[1], "z": tokens[2]}}
    try:
        result["value"] = {"x": float(tokens[0]), "y": float(tokens[1]), "z": float(tokens[2])}
        result["validNumeric"] = True
    except ValueError:
        # AngmarKennelExpansion authors X:70.0.0. Preserve and surface it;
        # consumers must not silently invent a coordinate.
        if tokens != ("70.0.0", "0.0", "0.0"):
            raise ModuleContractError(f"{target_id} QueueProductionExitUpdate {key} invalid coordinate")
        result["value"] = None; result["validNumeric"] = False
    return result


def _queue_exit_delay_field(
    assignment: SageAssignment,
    target_id: str,
    numeric_defines: Mapping[str, int | float] | None,
    numeric_define_provenance: Mapping[str, Mapping[str, object]] | None,
) -> dict[str, object]:
    label = f"{target_id} QueueProductionExitUpdate ExitDelay"
    result = _lifetime_expression_field(assignment, label)
    assert result is not None
    if "milliseconds" in result:
        if re.fullmatch(r"\s*\d+\s*", assignment.value) is None:
            raise ModuleContractError(f"{label} must be an exact unsigned decimal")
        if int(result["milliseconds"]) > 4_294_967_295:
            raise ModuleContractError(f"{label} exceeds UnsignedInt range 0..4294967295")
        return result
    expression = str(result.get("expression", "")).strip()
    if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", expression) is None:
        raise ModuleContractError(f"{label} is not a resolvable millisecond expression")
    key = expression.casefold()
    value = None if numeric_defines is None else numeric_defines.get(key)
    provenance = None if numeric_define_provenance is None else numeric_define_provenance.get(key)
    if value is None or provenance is None:
        raise ModuleContractError(f"{label} define is unresolved: {expression}")
    numeric = float(value)
    if numeric < 0 or not numeric.is_integer() or numeric > 4_294_967_295:
        raise ModuleContractError(f"{label} define must resolve to non-negative integer milliseconds: {expression}")
    provenance_value = provenance.get("value")
    if (
        str(provenance.get("defineId", "")).casefold() != key
        or not isinstance(provenance.get("sourceIni"), str)
        or not str(provenance.get("sourceIni"))
        or isinstance(provenance.get("line"), bool)
        or not isinstance(provenance.get("line"), int)
        or int(provenance["line"]) <= 0
        or not isinstance(provenance.get("authoredValue"), str)
        or not str(provenance.get("authoredValue"))
        or isinstance(provenance_value, bool)
        or not isinstance(provenance_value, (int, float))
        or float(provenance_value) != numeric
    ):
        raise ModuleContractError(f"{label} define provenance is invalid: {expression}")
    result["milliseconds"] = int(numeric)
    result["defineProvenance"] = dict(provenance)
    return result


def compile_queue_production_exit_updates(
    lineage: Sequence[SageObject],
    target_id: str,
    *,
    numeric_defines: Mapping[str, int | float] | None = None,
    numeric_define_provenance: Mapping[str, Mapping[str, object]] | None = None,
) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for block in _behavior_blocks(lineage, "QueueProductionExitUpdate"):
        if block.blocks: raise ModuleContractError(f"{target_id} QueueProductionExitUpdate nested blocks")
        grouped: dict[str, list[SageAssignment]] = {}
        for assignment in block.assignments: grouped.setdefault(assignment.key.casefold(), []).append(assignment)
        repeated = {"unitcreatepoint", "naturalrallypoint", "exitdelay", "placementviewangle"}
        if set(grouped) - _QUEUE_EXIT_FIELDS or any(len(v) > 1 and k not in repeated for k, v in grouped.items()): raise ModuleContractError(f"{target_id} QueueProductionExitUpdate duplicate/unsupported fields")
        fields: dict[str, object] = {}
        for key in ("UnitCreatePoint", "NaturalRallyPoint"):
            authored = grouped.get(key.casefold(), [])
            if authored:
                fields[key] = [_queue_coord_field(item, target_id, key) for item in authored]
        if "UnitCreatePoint" not in fields:
            raise ModuleContractError(
                f"{target_id} QueueProductionExitUpdate requires UnitCreatePoint"
            )
        angles = [_number_field(item, f"{target_id} QueueProductionExitUpdate PlacementViewAngle") for item in grouped.get("placementviewangle", [])]
        if angles: fields["PlacementViewAngle"] = angles
        delays = [
            _queue_exit_delay_field(item, target_id, numeric_defines, numeric_define_provenance)
            for item in grouped.get("exitdelay", [])
        ]
        if delays: fields["ExitDelay"] = delays
        burst = _integer_assignment_field(grouped.get("initialburst", [None])[-1], f"{target_id} QueueProductionExitUpdate InitialBurst", minimum=0)
        if burst is not None:
            if int(burst["value"]) > 4_294_967_295:
                raise ModuleContractError(
                    f"{target_id} QueueProductionExitUpdate InitialBurst exceeds UnsignedInt range 0..4294967295"
                )
            fields["InitialBurst"] = burst
        for key in ("AllowAirborneCreation", "UseReturnToFormation", "NoExitPath", "CanRallyToSlaughter"):
            value = _yes_no_field(grouped.get(key.casefold(), [None])[-1], f"{target_id} QueueProductionExitUpdate {key}")
            if value is not None: fields[key] = value
        has_invalid_coordinate = any(
            not bool(coordinate.get("validNumeric"))
            for key in ("UnitCreatePoint", "NaturalRallyPoint")
            for coordinate in fields.get(key, [])
        )
        has_closed_runtime = _queue_exit_row_has_closed_runtime(fields)
        rows.append(_row(
            "QueueProductionExitUpdate",
            block,
            fields,
            runtime_status=(
                "executable"
                if has_closed_runtime and not has_invalid_coordinate
                else "deferred"
            ),
        ))
    rows.sort(key=lambda row: (str(row["sourceIni"]).casefold(), int(row["line"])))
    return rows


_REBUILD_HOLE_EXPOSE_FIELDS = frozenset(
    {"exemptstatus", "holename", "holemaxhealth", "fadeintimeseconds", "transferattackers"}
)


def _required_identifier_field(
    assignment: SageAssignment | None, label: str
) -> dict[str, object]:
    value = _string_field(assignment)
    if value is None or re.fullmatch(
        r"[A-Za-z_][A-Za-z0-9_]*", str(value["value"])
    ) is None:
        raise ModuleContractError(f"{label} must be an identifier")
    return value


def compile_rebuild_hole_expose_dies(
    lineage: Sequence[SageObject], target_id: str
) -> list[dict[str, object]]:
    """Compile lair-death to rebuild-hole lifecycle edges."""

    rows: list[dict[str, object]] = []
    for block in _behavior_blocks(lineage, "RebuildHoleExposeDie"):
        if block.blocks:
            raise ModuleContractError(
                f"{target_id} RebuildHoleExposeDie has nested blocks"
            )
        amap = _assignment_map(block)
        if len(amap) != len(block.assignments) or set(amap) - _REBUILD_HOLE_EXPOSE_FIELDS:
            raise ModuleContractError(
                f"{target_id} RebuildHoleExposeDie duplicate or unsupported fields"
            )
        exempt = _token_list_field(amap.get("exemptstatus"))
        if exempt is None or not exempt["value"] or any(
            re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", str(token)) is None
            for token in exempt["value"]
        ):
            raise ModuleContractError(
                f"{target_id} RebuildHoleExposeDie requires ExemptStatus tokens"
            )
        hole = _required_identifier_field(
            amap.get("holename"), f"{target_id} RebuildHoleExposeDie HoleName"
        )
        health = _number_field(
            amap.get("holemaxhealth"),
            f"{target_id} RebuildHoleExposeDie HoleMaxHealth",
        )
        fade = _number_field(
            amap.get("fadeintimeseconds"),
            f"{target_id} RebuildHoleExposeDie FadeInTimeSeconds",
        )
        if health is None or float(health["value"]) <= 0:
            raise ModuleContractError(
                f"{target_id} RebuildHoleExposeDie HoleMaxHealth must be positive"
            )
        if fade is None or float(fade["value"]) < 0:
            raise ModuleContractError(
                f"{target_id} RebuildHoleExposeDie FadeInTimeSeconds must be non-negative"
            )
        transfer = _yes_no_field(
            amap.get("transferattackers"),
            f"{target_id} RebuildHoleExposeDie TransferAttackers",
        )
        fields: dict[str, object] = {
            "ExemptStatus": exempt,
            "HoleName": hole,
            "HoleMaxHealth": health,
            "FadeInTimeSeconds": fade,
        }
        if transfer is not None:
            fields["TransferAttackers"] = transfer
        row = _row(
            "RebuildHoleExposeDie",
            block,
            fields,
            runtime_status=(
                "deferred"
                if transfer is not None and transfer["value"] is True
                else None
            ),
        )
        graph: dict[str, object] = {
            "kind": "rebuild-hole-exposure",
            "exemptStatuses": list(exempt["value"]),
            "holeObjectId": hole["value"],
            "holeMaxHealth": health["value"],
            "fadeInTimeSeconds": fade["value"],
            "sourceIni": block.source_virtual_path,
            "line": block.line,
        }
        if transfer is not None:
            graph["transferAttackers"] = transfer["value"]
        row["lifecycleGraph"] = graph
        rows.append(row)
    rows.sort(key=lambda row: (str(row["sourceIni"]).casefold(), int(row["line"])))
    return rows


_REBUILD_HOLE_BEHAVIOR_FIELDS = frozenset(
    {"workerobjectname", "workerrespawndelay", "holehealthregen%persecond"}
)


def _hole_regen_percent_field(
    assignment: SageAssignment | None, label: str
) -> dict[str, object]:
    if assignment is None:
        raise ModuleContractError(f"{label} is required")
    match = re.fullmatch(
        r"\s*(\d+(?:\.\d*)?|\.\d+)\s*(%)?\s*", assignment.value
    )
    if match is None or (match.group(2) is None and float(match.group(1)) != 0.0):
        raise ModuleContractError(
            f"{label} must be a non-negative percentage or the literal zero"
        )
    percent = float(match.group(1))
    return {
        "authored": assignment.value,
        "percent": percent,
        "ratio": percent / 100.0,
        "sourceIni": assignment.source_virtual_path,
        "line": assignment.line,
    }


def compile_rebuild_hole_behaviors(
    lineage: Sequence[SageObject], target_id: str
) -> list[dict[str, object]]:
    """Compile rebuild-hole worker timing and health regeneration policy."""

    rows: list[dict[str, object]] = []
    for block in _behavior_blocks(lineage, "RebuildHoleBehavior"):
        if block.blocks:
            raise ModuleContractError(
                f"{target_id} RebuildHoleBehavior has nested blocks"
            )
        amap = _assignment_map(block)
        if len(amap) != len(block.assignments) or set(amap) - _REBUILD_HOLE_BEHAVIOR_FIELDS:
            raise ModuleContractError(
                f"{target_id} RebuildHoleBehavior duplicate or unsupported fields"
            )
        delay = _milliseconds_field(
            amap.get("workerrespawndelay"),
            f"{target_id} RebuildHoleBehavior WorkerRespawnDelay",
        )
        if delay is None:
            raise ModuleContractError(
                f"{target_id} RebuildHoleBehavior requires WorkerRespawnDelay"
            )
        regen = _hole_regen_percent_field(
            amap.get("holehealthregen%persecond"),
            f"{target_id} RebuildHoleBehavior HoleHealthRegen%PerSecond",
        )
        fields: dict[str, object] = {
            "WorkerRespawnDelay": delay,
            "HoleHealthRegen%PerSecond": regen,
        }
        worker_assignment = amap.get("workerobjectname")
        worker = None
        if worker_assignment is not None:
            worker = _required_identifier_field(
                worker_assignment,
                f"{target_id} RebuildHoleBehavior WorkerObjectName",
            )
            fields["WorkerObjectName"] = worker
        row = _row("RebuildHoleBehavior", block, fields)
        graph: dict[str, object] = {
            "kind": "rebuild-hole-regrowth",
            "workerRespawnDelayMs": delay["milliseconds"],
            "holeHealthRegenRatioPerSecond": regen["ratio"],
            "sourceIni": block.source_virtual_path,
            "line": block.line,
        }
        if worker is not None:
            graph["workerObjectId"] = worker["value"]
        row["lifecycleGraph"] = graph
        rows.append(row)
    rows.sort(key=lambda row: (str(row["sourceIni"]).casefold(), int(row["line"])))
    return rows


_SALVAGE_CRATE_FIELDS = frozenset(
    {
        "forbiddenkindof",
        "executefx",
        "porterchance",
        "bannerchance",
        "levelupchance",
        "levelupradius",
        "resourcechance",
        "minresource",
        "maxresource",
        "allowaipickup",
        "upgrade",
    }
)


def compile_salvage_crate_collides(
    lineage: Sequence[SageObject], target_id: str
) -> list[dict[str, object]]:
    """Compile the exact canonical salvage-crate reward lottery."""

    rows: list[dict[str, object]] = []
    for block in _behavior_blocks(lineage, "SalvageCrateCollide"):
        if block.blocks:
            raise ModuleContractError(
                f"{target_id} SalvageCrateCollide has nested blocks"
            )
        amap = _assignment_map(block)
        if len(amap) != len(block.assignments) or set(amap) - _SALVAGE_CRATE_FIELDS:
            raise ModuleContractError(
                f"{target_id} SalvageCrateCollide has duplicate or unsupported fields"
            )
        forbidden = _token_list_field(amap.get("forbiddenkindof"))
        if forbidden is None or not forbidden["value"] or any(
            re.fullmatch(r"[+-]?[A-Za-z_][A-Za-z0-9_]*", str(token)) is None
            for token in forbidden["value"]
        ):
            raise ModuleContractError(
                f"{target_id} SalvageCrateCollide ForbiddenKindOf is invalid"
            )
        fields: dict[str, object] = {"ForbiddenKindOf": forbidden}
        execute_fx_assignment = amap.get("executefx")
        if execute_fx_assignment is not None:
            fields["ExecuteFX"] = _required_identifier_field(
                execute_fx_assignment,
                f"{target_id} SalvageCrateCollide ExecuteFX",
            )
        upgrade_assignment = amap.get("upgrade")
        if upgrade_assignment is not None:
            fields["Upgrade"] = _required_identifier_field(
                upgrade_assignment,
                f"{target_id} SalvageCrateCollide Upgrade",
            )
        for key in ("PorterChance", "BannerChance", "LevelUpChance", "ResourceChance"):
            assignment = amap.get(key.casefold())
            if assignment is None:
                continue
            value = _percent_assignment_field(
                assignment, f"{target_id} SalvageCrateCollide {key}"
            )
            if value is None or not 0 <= float(value["percent"]) <= 100:
                raise ModuleContractError(
                    f"{target_id} SalvageCrateCollide {key} must be between 0% and 100%"
                )
            fields[key] = value
        if "LevelUpChance" not in fields:
            raise ModuleContractError(
                f"{target_id} SalvageCrateCollide requires LevelUpChance"
            )
        radius = _number_field(
            amap.get("levelupradius"),
            f"{target_id} SalvageCrateCollide LevelUpRadius",
        )
        if radius is None or float(radius["value"]) < 0:
            raise ModuleContractError(
                f"{target_id} SalvageCrateCollide LevelUpRadius must be non-negative"
            )
        fields["LevelUpRadius"] = radius
        resource_keys = {"resourcechance", "minresource", "maxresource"}
        present_resource_keys = resource_keys & set(amap)
        minimum = maximum = None
        if present_resource_keys and present_resource_keys != resource_keys:
            raise ModuleContractError(
                f"{target_id} SalvageCrateCollide resource reward is half-authored"
            )
        if present_resource_keys:
            minimum = _integer_assignment_field(
                amap.get("minresource"),
                f"{target_id} SalvageCrateCollide MinResource",
                minimum=0,
            )
            maximum = _integer_assignment_field(
                amap.get("maxresource"),
                f"{target_id} SalvageCrateCollide MaxResource",
                minimum=0,
            )
            if minimum is None or maximum is None or minimum["value"] > maximum["value"]:
                raise ModuleContractError(
                    f"{target_id} SalvageCrateCollide resource bounds are invalid"
                )
            fields["MinResource"] = minimum
            fields["MaxResource"] = maximum
        allow_ai = _yes_no_field(
            amap.get("allowaipickup"),
            f"{target_id} SalvageCrateCollide AllowAIPickup",
        )
        if allow_ai is None:
            raise ModuleContractError(
                f"{target_id} SalvageCrateCollide requires AllowAIPickup"
            )
        fields["AllowAIPickup"] = allow_ai
        row = _row("SalvageCrateCollide", block, fields)
        reward_graph: dict[str, object] = {
            "kind": "salvage-crate-reward",
            "forbiddenKindOf": list(forbidden["value"]),
            "levelUpChanceRatio": fields["LevelUpChance"]["ratio"],
            "levelUpRadius": radius["value"],
            "allowAIPickup": allow_ai["value"],
            "sourceIni": block.source_virtual_path,
            "line": block.line,
        }
        for field_name, graph_name in (
            ("PorterChance", "porterChanceRatio"),
            ("BannerChance", "bannerChanceRatio"),
            ("ResourceChance", "resourceChanceRatio"),
        ):
            if field_name in fields:
                reward_graph[graph_name] = fields[field_name]["ratio"]
        if minimum is not None and maximum is not None:
            reward_graph["minResource"] = minimum["value"]
            reward_graph["maxResource"] = maximum["value"]
        if "ExecuteFX" in fields:
            reward_graph["executeFxId"] = fields["ExecuteFX"]["value"]
        if "Upgrade" in fields:
            reward_graph["upgradeId"] = fields["Upgrade"]["value"]
        row["rewardGraph"] = reward_graph
        rows.append(row)
    rows.sort(key=lambda row: (str(row["sourceIni"]).casefold(), int(row["line"])))
    return rows


def compile_horde_member_collides(lineage: Sequence[SageObject], target_id: str) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for block in _behavior_blocks(lineage, "HordeMemberCollide"):
        if block.assignments or block.blocks: raise ModuleContractError(f"{target_id} HordeMemberCollide must be empty")
        rows.append(_row("HordeMemberCollide", block, {}))
    rows.sort(key=lambda row: (str(row["sourceIni"]).casefold(), int(row["line"])))
    return rows


_BANNER_UPDATE_FIELDS = frozenset({"idlespawnrate", "meleefreeunitspawntime", "diedrespawntime", "meleefreebannerrespawntime", "bannermorphfx", "unitspawnfx", "morphcondition", "upgraderequired", "replenishnearbyhorde", "scanhordedistance", "replenishallnearbyhordes"})


def compile_banner_carrier_updates(lineage: Sequence[SageObject], target_id: str) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for block in _behavior_blocks(lineage, "BannerCarrierUpdate"):
        if block.blocks: raise ModuleContractError(f"{target_id} BannerCarrierUpdate nested blocks")
        grouped: dict[str, list[SageAssignment]] = {}
        for assignment in block.assignments: grouped.setdefault(assignment.key.casefold(), []).append(assignment)
        if set(grouped) - _BANNER_UPDATE_FIELDS or any(len(v) > 1 and k != "morphcondition" for k, v in grouped.items()): raise ModuleContractError(f"{target_id} BannerCarrierUpdate duplicate/unsupported fields")
        amap = {k: v[-1] for k, v in grouped.items()}; fields: dict[str, object] = {}
        for key in ("IdleSpawnRate", "MeleeFreeUnitSpawnTime", "DiedRespawnTime", "MeleeFreeBannerRespawnTime"):
            value = _milliseconds_field(amap.get(key.casefold()), f"{target_id} BannerCarrierUpdate {key}")
            if value is not None: fields[key] = value
        if "IdleSpawnRate" not in fields: raise ModuleContractError(f"{target_id} BannerCarrierUpdate requires IdleSpawnRate")
        for key in ("BannerMorphFX", "UnitSpawnFX", "UpgradeRequired"):
            value = _string_field(amap.get(key.casefold()))
            if value is not None:
                if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", str(value["value"])) is None: raise ModuleContractError(f"{target_id} BannerCarrierUpdate {key} malformed")
                fields[key] = value
        for key in ("ReplenishNearbyHorde", "ReplenishAllNearbyHordes"):
            value = _yes_no_field(amap.get(key.casefold()), f"{target_id} BannerCarrierUpdate {key}")
            if value is not None: fields[key] = value
        scan = _non_negative_expression_field(amap.get("scanhordedistance"), f"{target_id} BannerCarrierUpdate ScanHordeDistance")
        if scan is not None: fields["ScanHordeDistance"] = scan
        morphs: list[dict[str, object]] = []
        for assignment in grouped.get("morphcondition", []):
            match = re.fullmatch(r'\s*UnitType:(\S+)(?:\s+Locomotor:(\S+))?\s+ModelState:"([^"]+)"\s*', assignment.value)
            if match is None: raise ModuleContractError(f"{target_id} BannerCarrierUpdate MorphCondition malformed")
            morphs.append({**_authored_row(assignment), "unitType": match.group(1), "locomotor": match.group(2), "modelStates": match.group(3).split()})
        if morphs: fields["MorphCondition"] = morphs
        rows.append(_row("BannerCarrierUpdate", block, fields))
    rows.sort(key=lambda row: (str(row["sourceIni"]).casefold(), int(row["line"])))
    return rows


def compile_notify_crushing_updates(lineage: Sequence[SageObject], target_id: str) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for block in _behavior_blocks(lineage, "NotifyTargetsOfImminentProbableCrushingUpdate"):
        if block.assignments or block.blocks: raise ModuleContractError(f"{target_id} NotifyTargetsOfImminentProbableCrushingUpdate must be empty")
        rows.append(_row("NotifyTargetsOfImminentProbableCrushingUpdate", block, {}))
    rows.sort(key=lambda row: (str(row["sourceIni"]).casefold(), int(row["line"])))
    return rows


def compile_foundation_ai_updates(lineage: Sequence[SageObject], target_id: str) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for block in _behavior_blocks(lineage, "FoundationAIUpdate"):
        if block.blocks: raise ModuleContractError(f"{target_id} FoundationAIUpdate nested blocks")
        amap = _assignment_map(block)
        if len(amap) != len(block.assignments) or set(amap) - {"buildvariation"}: raise ModuleContractError(f"{target_id} FoundationAIUpdate malformed fields")
        fields: dict[str, object] = {}
        variation = _integer_assignment_field(amap.get("buildvariation"), f"{target_id} FoundationAIUpdate BuildVariation", minimum=1)
        if variation is not None: fields["BuildVariation"] = variation
        rows.append(_row("FoundationAIUpdate", block, fields))
    rows.sort(key=lambda row: (str(row["sourceIni"]).casefold(), int(row["line"])))
    return rows


_MONITOR_FIELDS = frozenset({"weaponsetflags", "weapontogglecommandset", "modelconditionflags", "modelconditioncommandset"})


def compile_monitor_condition_updates(lineage: Sequence[SageObject], target_id: str) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for block in _behavior_blocks(lineage, "MonitorConditionUpdate"):
        if block.blocks:
            raise ModuleContractError(f"{target_id} MonitorConditionUpdate nested blocks")
        amap = _assignment_map(block)
        if len(amap) != len(block.assignments) or set(amap) - _MONITOR_FIELDS:
            raise ModuleContractError(
                f"{target_id} MonitorConditionUpdate malformed fields"
            )
        fields: dict[str, object] = {}
        model_flags = _token_list_field(amap.get("modelconditionflags"))
        model_command = _string_field(amap.get("modelconditioncommandset"))
        weapon_flags = _token_list_field(amap.get("weaponsetflags"))
        weapon_command = _string_field(amap.get("weapontogglecommandset"))

        for label, flags in (
            ("ModelConditionFlags", model_flags),
            ("WeaponSetFlags", weapon_flags),
        ):
            if flags is not None and not flags["value"]:
                raise ModuleContractError(
                    f"{target_id} MonitorConditionUpdate malformed {label}"
                )
        for label, command in (
            ("ModelConditionCommandSet", model_command),
            ("WeaponToggleCommandSet", weapon_command),
        ):
            if command is not None and re.fullmatch(
                r"[A-Za-z_][A-Za-z0-9_]*", str(command["value"])
            ) is None:
                raise ModuleContractError(
                    f"{target_id} MonitorConditionUpdate malformed {label}"
                )

        model_pair = (model_flags is not None, model_command is not None)
        weapon_pair = (weapon_flags is not None, weapon_command is not None)
        if model_pair[0] != model_pair[1] or weapon_pair[0] != weapon_pair[1]:
            raise ModuleContractError(
                f"{target_id} MonitorConditionUpdate has an incomplete or "
                "unsupported predicate/target shape"
            )
        if model_flags is not None and model_command is not None:
            fields["ModelConditionRoute"] = {
                "flags": model_flags,
                "commandSet": model_command,
            }
        if weapon_flags is not None and weapon_command is not None:
            fields["WeaponSetRoute"] = {
                "flags": weapon_flags,
                "commandSet": weapon_command,
            }
        if not fields:
            raise ModuleContractError(
                f"{target_id} MonitorConditionUpdate has an incomplete or "
                "unsupported predicate/target shape"
            )
        rows.append(_row("MonitorConditionUpdate", block, fields))
    rows.sort(key=lambda row: (str(row["sourceIni"]).casefold(), int(row["line"])))
    return rows


_RESPAWN_BODY_FIELDS = frozenset({"cheerradius", "maxhealth", "permanentlykilledbyfilter", "dodgepercent", "maxhealthdamaged", "recoverytime", "burningdeathbehavior", "burningdeathfx", "healingbufffx", "canrespawn"})


def _respawn_body_expression(assignment: SageAssignment | None, target_id: str, key: str, *, percent: bool = False) -> dict[str, object] | None:
    if assignment is None: return None
    expression = assignment.value.strip(); result = {**_authored_row(assignment), "expression": expression}
    match = re.fullmatch(r"(\d+(?:\.\d*)?|\.\d+)%?", expression)
    if match is not None:
        value = float(match.group(1)); result["value"] = int(value) if value.is_integer() else value
        if percent: result["percent"] = value
        return result
    if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", expression): result["define"] = expression; return result
    raise ModuleContractError(f"{target_id} RespawnBody {key} malformed")


def compile_respawn_bodies(lineage: Sequence[SageObject], target_id: str) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for block in _module_blocks(lineage, "RespawnBody"):
        if block.blocks: raise ModuleContractError(f"{target_id} RespawnBody nested blocks")
        grouped: dict[str, list[SageAssignment]] = {}
        for assignment in block.assignments: grouped.setdefault(assignment.key.casefold(), []).append(assignment)
        if set(grouped) - _RESPAWN_BODY_FIELDS or any(len(v) > 1 and k != "cheerradius" for k, v in grouped.items()): raise ModuleContractError(f"{target_id} RespawnBody malformed fields")
        amap = {k: v[-1] for k, v in grouped.items()}
        fields: dict[str, object] = {}
        cheers = [_respawn_body_expression(item, target_id, "CheerRadius") for item in grouped.get("cheerradius", [])]
        if cheers: fields["CheerRadius"] = cheers
        for key in ("MaxHealth", "MaxHealthDamaged", "RecoveryTime"):
            value = _respawn_body_expression(amap.get(key.casefold()), target_id, key)
            if value is not None: fields[key] = value
        if "MaxHealth" not in fields: raise ModuleContractError(f"{target_id} RespawnBody requires MaxHealth")
        dodge = _respawn_body_expression(amap.get("dodgepercent"), target_id, "DodgePercent", percent=True)
        if dodge is not None: fields["DodgePercent"] = dodge
        killed = _token_list_field(amap.get("permanentlykilledbyfilter"))
        if killed is not None: fields["PermanentlyKilledByFilter"] = killed
        for key in ("BurningDeathBehavior", "CanRespawn"):
            value = _yes_no_field(amap.get(key.casefold()), f"{target_id} RespawnBody {key}")
            if value is not None: fields[key] = value
        for key in ("BurningDeathFX", "HealingBuffFX"):
            value = _string_field(amap.get(key.casefold()))
            if value is not None: fields[key] = value
        rows.append(_row("RespawnBody", block, fields))
    rows.sort(key=lambda row: (str(row["sourceIni"]).casefold(), int(row["line"])))
    return rows


_GIVE_UPGRADE_FIELDS = frozenset({"specialpowertemplate", "startabilityrange", "unpacktime", "preparationtime", "persistentpreptime", "packtime", "approachrequireslos", "spawnoutfx", "deliverupgrade", "fadeoutspeed"})


def compile_give_upgrade_updates(lineage: Sequence[SageObject], target_id: str) -> list[dict[str, object]]:
    rows=[]
    for block in _behavior_blocks(lineage, "GiveUpgradeUpdate"):
        if block.blocks: raise ModuleContractError(f"{target_id} GiveUpgradeUpdate nested blocks")
        amap=_assignment_map(block)
        if len(amap)!=len(block.assignments) or set(amap)-_GIVE_UPGRADE_FIELDS: raise ModuleContractError(f"{target_id} GiveUpgradeUpdate malformed fields")
        fields={}
        for key in ("SpecialPowerTemplate", "SpawnOutFX"):
            value=_string_field(amap.get(key.casefold()))
            if value is None or re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*",str(value["value"])) is None: raise ModuleContractError(f"{target_id} GiveUpgradeUpdate requires {key}")
            fields[key]=value
        range_value=_number_field(amap.get("startabilityrange"),f"{target_id} GiveUpgradeUpdate StartAbilityRange")
        if range_value is None or float(range_value["value"])<0: raise ModuleContractError(f"{target_id} GiveUpgradeUpdate requires StartAbilityRange")
        fields["StartAbilityRange"]=range_value
        for key in ("UnpackTime","PreparationTime","PersistentPrepTime","PackTime"):
            value=_lifetime_expression_field(amap.get(key.casefold()),f"{target_id} GiveUpgradeUpdate {key}")
            if value is None: raise ModuleContractError(f"{target_id} GiveUpgradeUpdate requires {key}")
            fields[key]=value
        for key in ("ApproachRequiresLOS","DeliverUpgrade"):
            value=_yes_no_field(amap.get(key.casefold()),f"{target_id} GiveUpgradeUpdate {key}")
            if value is not None: fields[key]=value
        fade=_number_field(amap.get("fadeoutspeed"),f"{target_id} GiveUpgradeUpdate FadeOutSpeed")
        if fade is not None:
            if float(fade["value"])<0: raise ModuleContractError(f"{target_id} GiveUpgradeUpdate FadeOutSpeed negative")
            fields["FadeOutSpeed"]=fade
        rows.append(_row("GiveUpgradeUpdate",block,fields))
    rows.sort(key=lambda r:(str(r["sourceIni"]).casefold(),int(r["line"])));return rows


_GATE_FIELDS=frozenset({"resettimeinmilliseconds","openbydefault","percentopenforpathing","soundopeninggateloop","soundclosinggateloop","soundfinishedopeninggate","soundfinishedclosinggate","timebeforeplayingopensound","timebeforeplayingclosedsound","repelcollidingunits"})


def compile_gate_open_close_behaviors(lineage: Sequence[SageObject], target_id: str) -> list[dict[str, object]]:
    rows=[]
    for block in _behavior_blocks(lineage,"GateOpenAndCloseBehavior"):
        if block.blocks: raise ModuleContractError(f"{target_id} GateOpenAndCloseBehavior nested blocks")
        amap=_assignment_map(block)
        if len(amap)!=len(block.assignments) or set(amap)-_GATE_FIELDS: raise ModuleContractError(f"{target_id} GateOpenAndCloseBehavior malformed fields")
        fields={}
        for key in ("ResetTimeInMilliseconds","TimeBeforePlayingOpenSound","TimeBeforePlayingClosedSound"):
            value=_milliseconds_field(amap.get(key.casefold()),f"{target_id} GateOpenAndCloseBehavior {key}")
            if value is None: raise ModuleContractError(f"{target_id} GateOpenAndCloseBehavior requires {key}")
            fields[key]=value
        for key in ("OpenByDefault","RepelCollidingUnits"):
            value=_yes_no_field(amap.get(key.casefold()),f"{target_id} GateOpenAndCloseBehavior {key}")
            if value is not None:fields[key]=value
        percent=_number_field(amap.get("percentopenforpathing"),f"{target_id} GateOpenAndCloseBehavior PercentOpenForPathing")
        if percent is None or not 0<=float(percent["value"])<=100:raise ModuleContractError(f"{target_id} GateOpenAndCloseBehavior invalid pathing percent")
        fields["PercentOpenForPathing"]=percent
        for key in ("SoundOpeningGateLoop","SoundClosingGateLoop","SoundFinishedOpeningGate","SoundFinishedClosingGate"):
            value=_string_field(amap.get(key.casefold()))
            if value is None:raise ModuleContractError(f"{target_id} GateOpenAndCloseBehavior requires {key}")
            fields[key]=value
        rows.append(_row("GateOpenAndCloseBehavior",block,fields))
    rows.sort(key=lambda r:(str(r["sourceIni"]).casefold(),int(r["line"])));return rows


def compile_ai_gate_updates(lineage: Sequence[SageObject], target_id: str) -> list[dict[str, object]]:
    rows=[]
    for block in _behavior_blocks(lineage,"AIGateUpdate"):
        amap=_assignment_map(block)
        if block.blocks or len(amap)!=2 or set(amap)!={"triggerwidthx","triggerwidthy"}:raise ModuleContractError(f"{target_id} AIGateUpdate malformed")
        fields={}
        for key in ("TriggerWidthX","TriggerWidthY"):
            value=_number_field(amap[key.casefold()],f"{target_id} AIGateUpdate {key}")
            assert value is not None
            if float(value["value"])<=0:raise ModuleContractError(f"{target_id} AIGateUpdate {key} non-positive")
            fields[key]=value
        rows.append(_row("AIGateUpdate",block,fields))
    rows.sort(key=lambda r:(str(r["sourceIni"]).casefold(),int(r["line"])));return rows


def compile_fake_pathfind_portals(lineage: Sequence[SageObject], target_id: str) -> list[dict[str, object]]:
    rows=[]
    for block in _behavior_blocks(lineage,"FakePathfindPortalBehaviour"):
        amap=_assignment_map(block)
        if block.blocks or len(amap)!=len(block.assignments) or set(amap)!={"allowenemies","allownonskirmishaiunits"}:raise ModuleContractError(f"{target_id} FakePathfindPortalBehaviour malformed")
        fields={}
        for key in ("AllowEnemies","AllowNonSkirmishAIUnits"):
            value=_yes_no_field(amap[key.casefold()],f"{target_id} FakePathfindPortalBehaviour {key}");assert value is not None;fields[key]=value
        rows.append(_row("FakePathfindPortalBehaviour",block,fields))
    rows.sort(key=lambda r:(str(r["sourceIni"]).casefold(),int(r["line"])));return rows


_STEALTH_DETECT_FIELDS=frozenset({"detectionrate","detectionrange","canceloneringeffect","requiredupgrade","candetectwhilegarrisoned","candetectwhilecontained"})


def compile_stealth_detector_updates(lineage: Sequence[SageObject], target_id: str) -> list[dict[str, object]]:
    rows=[]
    for block in _behavior_blocks(lineage,"StealthDetectorUpdate"):
        amap=_assignment_map(block)
        if block.blocks or len(amap)!=len(block.assignments) or set(amap)-_STEALTH_DETECT_FIELDS:raise ModuleContractError(f"{target_id} StealthDetectorUpdate malformed fields")
        fields={}
        rate=_lifetime_expression_field(amap.get("detectionrate"),f"{target_id} StealthDetectorUpdate DetectionRate")
        if rate is None:raise ModuleContractError(f"{target_id} StealthDetectorUpdate requires DetectionRate")
        fields["DetectionRate"]=rate
        detection_range=_non_negative_expression_field(amap.get("detectionrange"),f"{target_id} StealthDetectorUpdate DetectionRange")
        if detection_range is not None:fields["DetectionRange"]=detection_range
        for key in ("CancelOneRingEffect","CanDetectWhileGarrisoned","CanDetectWhileContained"):
            value=_yes_no_field(amap.get(key.casefold()),f"{target_id} StealthDetectorUpdate {key}")
            if value is not None:fields[key]=value
        upgrade=_string_field(amap.get("requiredupgrade"))
        if upgrade is not None:fields["RequiredUpgrade"]=upgrade
        rows.append(_row("StealthDetectorUpdate",block,fields))
    rows.sort(key=lambda r:(str(r["sourceIni"]).casefold(),int(r["line"])));return rows


_SLAVED_FIELDS=frozenset({"leashrange","guardmaxrange","guardwanderrange","attackrange","useslaverascontrolforevaobjectsightedevents","dieonmastersdeath","markunselectable","guardpositionoffset","fadeoutrange","fadetime"})

_DELAYED_DEATH_BODY_FIELDS = frozenset({
    "maxhealth", "delayeddeathtime", "canrespawn", "maxhealthdamaged",
    "recoverytime", "dohealthcheck", "cheerradius", "immortaluntildeathtime",
    "burningdeathbehavior", "burningdeathfx", "maxhealthreallydamaged",
    "dodgepercent",
})

_DYNAMIC_PORTAL_FIELDS = frozenset({
    "activationdelayseconds", "generatenow", "objectfilter", "boneprefix",
    "numberofbones", "waypoint", "link", "allowenemies", "triggeredby",
    "conflictswith", "customanimandduration", "abovewall", "topattackpos",
    "topattackradius",
})

_FLAMMABLE_FIELDS = frozenset({
    "aflameduration", "aflamedamageamount", "aflamedamagedelay",
    "flamedamagelimit", "burncontained", "firefxlist",
    "flamedamageexpiration", "setburnedstatus", "damagetype", "burneddelay",
    "burningsoundname",
})

_SPAWN_BEHAVIOR_FIELDS = frozenset({
    "spawnnumber", "spawnreplacedelay", "spawntemplatename", "oneshot",
    "canreclaimorphans", "respectcommandlimit", "initialburst",
    "killspawnsbasedonmodelconditionstate", "spawnedrequirespawner",
    "shareupgrades", "fadeintime", "spawninsidebuilding", "triggeredby",
})

# Binary-accepted BFME2 1.06 / RotWK 2.01 reclaim rows all share this closed
# shape: the initial population is full, orphan adoption is enabled, and none
# of the still-unresolved optional SpawnBehavior branches are authored.  Keep
# row promotion deliberately narrower than the typed parser.
_SPAWN_RECLAIM_EXECUTABLE_FIELDS = frozenset({
    "SpawnNumber", "SpawnReplaceDelay", "SpawnTemplateName", "InitialBurst",
    "CanReclaimOrphans",
})


def _spawn_reclaim_row_has_closed_runtime(fields: Mapping[str, object]) -> bool:
    if set(fields) != _SPAWN_RECLAIM_EXECUTABLE_FIELDS:
        return False
    number = fields.get("SpawnNumber")
    initial = fields.get("InitialBurst")
    templates = fields.get("SpawnTemplateName")
    reclaim = fields.get("CanReclaimOrphans")
    delay = fields.get("SpawnReplaceDelay")
    return (
        isinstance(number, Mapping)
        and isinstance(number.get("value"), int)
        and not isinstance(number.get("value"), bool)
        and int(number["value"]) > 0
        and isinstance(initial, Mapping)
        and initial.get("value") == number.get("value")
        and isinstance(templates, Mapping)
        and isinstance(templates.get("value"), list)
        and bool(templates["value"])
        and isinstance(delay, Mapping)
        and isinstance(delay.get("milliseconds"), int)
        and not isinstance(delay.get("milliseconds"), bool)
        and int(delay["milliseconds"]) >= 0
        and isinstance(reclaim, Mapping)
        and reclaim.get("value") is True
        and reclaim.get("runtimeStatus") == "executable"
    )


def compile_spawn_behaviors(
    lineage: Sequence[SageObject], target_id: str
) -> list[dict[str, object]]:
    """Compile spawn population, replacement timing, ownership, and upgrades."""

    rows: list[dict[str, object]] = []
    for block in _module_blocks(lineage, "SpawnBehavior"):
        amap = _assignment_map(block)
        if (
            block.blocks
            or len(amap) != len(block.assignments)
            or set(amap) - _SPAWN_BEHAVIOR_FIELDS
        ):
            raise ModuleContractError(f"{target_id} SpawnBehavior malformed fields")
        fields: dict[str, object] = {}
        number = _integer_assignment_field(
            amap.get("spawnnumber"), f"{target_id} SpawnBehavior SpawnNumber", minimum=1
        )
        delay = _milliseconds_field(
            amap.get("spawnreplacedelay"), f"{target_id} SpawnBehavior SpawnReplaceDelay"
        )
        templates = _token_list_field(amap.get("spawntemplatename"))
        if number is None or delay is None or templates is None or not templates["value"]:
            raise ModuleContractError(
                f"{target_id} SpawnBehavior requires SpawnNumber, SpawnReplaceDelay, and SpawnTemplateName"
            )
        fields.update({
            "SpawnNumber": number, "SpawnReplaceDelay": delay,
            "SpawnTemplateName": templates,
        })
        initial = _integer_assignment_field(
            amap.get("initialburst"), f"{target_id} SpawnBehavior InitialBurst", minimum=0
        )
        if initial is not None:
            if int(initial["value"]) > int(number["value"]):
                raise ModuleContractError(f"{target_id} SpawnBehavior InitialBurst exceeds SpawnNumber")
            fields["InitialBurst"] = initial
        fade = _milliseconds_field(
            amap.get("fadeintime"), f"{target_id} SpawnBehavior FadeInTime"
        )
        if fade is not None:
            fields["FadeInTime"] = fade
        for key in (
            "OneShot", "CanReclaimOrphans", "RespectCommandLimit",
            "KillSpawnsBasedOnModelConditionState", "SpawnedRequireSpawner",
            "ShareUpgrades", "SpawnInsideBuilding",
        ):
            value = _yes_no_field(
                amap.get(key.casefold()), f"{target_id} SpawnBehavior {key}"
            )
            if value is not None:
                if key == "CanReclaimOrphans":
                    # BFME2 1.06 and RotWK 2.01 binary bodies now prove the
                    # enabled and disabled flag branches. Row execution below
                    # remains limited to the exact closed canonical shape.
                    value["runtimeStatus"] = "executable"
                fields[key] = value
        trigger = _string_field(amap.get("triggeredby"))
        if trigger is not None:
            if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", str(trigger["value"])) is None:
                raise ModuleContractError(f"{target_id} SpawnBehavior TriggeredBy malformed")
            fields["TriggeredBy"] = trigger
        rows.append(_row(
            "SpawnBehavior",
            block,
            fields,
            runtime_status=(
                "executable"
                if _spawn_reclaim_row_has_closed_runtime(fields)
                else "deferred"
            ),
        ))
    rows.sort(key=lambda r: (str(r["sourceIni"]).casefold(), int(r["line"])))
    return rows


_STEALTH_UPDATE_FIELDS = frozenset({
    "stealthdelay", "friendlyopacitymin", "friendlyopacitymax", "pulsefrequency",
    "innatestealth", "orderidleenemiestoattackmeuponreveal",
    "stealthforbiddenconditions", "hintdetectableconditions",
    "detectedbyanyonerange", "removeterrainrestrictiononupgrade",
    "revealweaponsets", "startsactive", "disguisesasteam",
    "revealdistancefromtarget", "disguisetransitiontime",
    "disguiserevealtransitiontime", "requiredupgradenames",
})


def _stealth_opacity(
    assignment: SageAssignment | None, target_id: str, key: str
) -> dict[str, object] | None:
    if assignment is None:
        return None
    expression = assignment.value.strip()
    result: dict[str, object] = {**_authored_row(assignment), "expression": expression}
    match = re.fullmatch(r"(\d+(?:\.\d*)?|\.\d+)\s*%", expression)
    if match is not None:
        percent = float(match.group(1))
        if percent > 100:
            raise ModuleContractError(f"{target_id} StealthUpdate {key} out of range")
        result.update({"percent": percent, "ratio": percent / 100.0})
        return result
    if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", expression):
        result["define"] = expression
        return result
    raise ModuleContractError(f"{target_id} StealthUpdate {key} malformed")


def compile_stealth_updates(
    lineage: Sequence[SageObject], target_id: str
) -> list[dict[str, object]]:
    """Compile stealth timing, opacity, reveal conditions, and disguise policy."""

    rows: list[dict[str, object]] = []
    for block in _module_blocks(lineage, "StealthUpdate"):
        amap = _assignment_map(block)
        if (
            block.blocks
            or len(amap) != len(block.assignments)
            or set(amap) - _STEALTH_UPDATE_FIELDS
        ):
            raise ModuleContractError(f"{target_id} StealthUpdate malformed fields")
        fields: dict[str, object] = {}
        for key in (
            "StealthDelay", "PulseFrequency", "DisguiseTransitionTime",
            "DisguiseRevealTransitionTime",
        ):
            value = _milliseconds_field(
                amap.get(key.casefold()), f"{target_id} StealthUpdate {key}"
            )
            if value is not None:
                fields[key] = value
        minimum = _stealth_opacity(amap.get("friendlyopacitymin"), target_id, "FriendlyOpacityMin")
        maximum = _stealth_opacity(amap.get("friendlyopacitymax"), target_id, "FriendlyOpacityMax")
        if (minimum is None) != (maximum is None):
            raise ModuleContractError(f"{target_id} StealthUpdate opacity bounds incomplete")
        if minimum is not None and maximum is not None:
            fields["FriendlyOpacityMin"] = minimum
            fields["FriendlyOpacityMax"] = maximum
        for key in (
            "InnateStealth", "OrderIdleEnemiesToAttackMeUponReveal", "StartsActive",
            "DisguisesAsTeam",
        ):
            value = _yes_no_field(
                amap.get(key.casefold()), f"{target_id} StealthUpdate {key}"
            )
            if value is not None:
                fields[key] = value
        for key in (
            "StealthForbiddenConditions", "HintDetectableConditions", "RevealWeaponSets",
            "RequiredUpgradeNames",
        ):
            value = _token_list_field(amap.get(key.casefold()))
            if value is not None:
                if not value["value"]:
                    raise ModuleContractError(f"{target_id} StealthUpdate {key} empty")
                fields[key] = value
        for key in ("DetectedByAnyoneRange",):
            value = _number_field(amap.get(key.casefold()), f"{target_id} StealthUpdate {key}")
            if value is not None:
                if float(value["value"]) < 0:
                    raise ModuleContractError(f"{target_id} StealthUpdate {key} negative")
                fields[key] = value
        reveal = amap.get("revealdistancefromtarget")
        if reveal is not None:
            match = re.fullmatch(r"\s*(\d+(?:\.\d*)?|\.\d+)f?\s*", reveal.value, re.IGNORECASE)
            if match is None:
                raise ModuleContractError(f"{target_id} StealthUpdate RevealDistanceFromTarget malformed")
            fields["RevealDistanceFromTarget"] = {
                **_authored_row(reveal), "value": float(match.group(1)),
            }
        upgrade = _string_field(amap.get("removeterrainrestrictiononupgrade"))
        if upgrade is not None:
            if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", str(upgrade["value"])) is None:
                raise ModuleContractError(
                    f"{target_id} StealthUpdate RemoveTerrainRestrictionOnUpgrade malformed"
                )
            fields["RemoveTerrainRestrictionOnUpgrade"] = upgrade
        if ("DisguiseTransitionTime" in fields) != ("DisguiseRevealTransitionTime" in fields):
            raise ModuleContractError(f"{target_id} StealthUpdate disguise timers incomplete")
        rows.append(_row("StealthUpdate", block, fields))
    rows.sort(key=lambda r: (str(r["sourceIni"]).casefold(), int(r["line"])))
    return rows


_OBJECT_CREATION_UPGRADE_FIELDS = frozenset({
    "triggeredby", "delay", "grantupgrade", "destroywhensold",
    "deathanimandduration", "requiresalltriggers", "thingtospawn", "offset",
    "fadeintime", "upgradeobject", "removeupgrade", "conflictswith",
    "usebuildingproduction",
})


def _object_creation_delay(
    assignment: SageAssignment | None, target_id: str
) -> dict[str, object] | None:
    if assignment is None:
        return None
    expression = assignment.value.strip()
    result: dict[str, object] = {**_authored_row(assignment), "expression": expression}
    if re.fullmatch(r"\d+(?:\.\d*)?|\.\d+", expression):
        numeric = float(expression)
        if not numeric.is_integer():
            raise ModuleContractError(
                f"{target_id} ObjectCreationUpgrade Delay must resolve to integer milliseconds"
            )
        result["milliseconds"] = int(numeric)
        return result
    if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", expression):
        result["define"] = expression
        return result
    raise ModuleContractError(f"{target_id} ObjectCreationUpgrade Delay malformed")


def compile_object_creation_upgrades(
    lineage: Sequence[SageObject], target_id: str
) -> list[dict[str, object]]:
    """Compile upgrade-triggered object/effect creation and lifecycle policy."""

    rows: list[dict[str, object]] = []
    for block in _module_blocks(lineage, "ObjectCreationUpgrade"):
        amap = _assignment_map(block)
        if (
            block.blocks
            or len(amap) != len(block.assignments)
            or set(amap) - _OBJECT_CREATION_UPGRADE_FIELDS
        ):
            raise ModuleContractError(f"{target_id} ObjectCreationUpgrade malformed fields")
        fields: dict[str, object] = {}
        triggers = _token_list_field(amap.get("triggeredby"))
        if triggers is None or not triggers["value"]:
            raise ModuleContractError(f"{target_id} ObjectCreationUpgrade requires TriggeredBy")
        fields["TriggeredBy"] = triggers
        delay = _object_creation_delay(amap.get("delay"), target_id)
        if delay is not None:
            fields["Delay"] = delay
        fade = _milliseconds_field(
            amap.get("fadeintime"), f"{target_id} ObjectCreationUpgrade FadeInTime"
        )
        if fade is not None:
            fields["FadeInTime"] = fade
        offset = _coord_field(
            amap.get("offset"), f"{target_id} ObjectCreationUpgrade Offset"
        )
        if offset is not None:
            fields["Offset"] = offset
        for key in ("RequiresAllTriggers", "DestroyWhenSold", "UseBuildingProduction"):
            value = _yes_no_field(
                amap.get(key.casefold()), f"{target_id} ObjectCreationUpgrade {key}"
            )
            if value is not None:
                fields[key] = value
        conflicts = _token_list_field(amap.get("conflictswith"))
        if conflicts is not None:
            if not conflicts["value"]:
                raise ModuleContractError(f"{target_id} ObjectCreationUpgrade ConflictsWith empty")
            fields["ConflictsWith"] = conflicts
        for key in ("ThingToSpawn", "GrantUpgrade", "RemoveUpgrade", "UpgradeObject"):
            value = _string_field(amap.get(key.casefold()))
            if value is not None:
                if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", str(value["value"])) is None:
                    raise ModuleContractError(f"{target_id} ObjectCreationUpgrade {key} malformed")
                fields[key] = value
        death = amap.get("deathanimandduration")
        if death is not None:
            match = re.fullmatch(
                r"\s*AnimState\s*:\s*([A-Za-z_][A-Za-z0-9_]*)\s+"
                r"AnimTime\s*:\s*(\d+)\s*", death.value, re.IGNORECASE,
            )
            if match is None:
                raise ModuleContractError(
                    f"{target_id} ObjectCreationUpgrade DeathAnimAndDuration malformed"
                )
            fields["DeathAnimAndDuration"] = {
                **_authored_row(death), "animState": match.group(1),
                "animTimeMilliseconds": int(match.group(2)),
            }
        if not any(
            key in fields
            for key in ("ThingToSpawn", "GrantUpgrade", "RemoveUpgrade", "UpgradeObject")
        ):
            raise ModuleContractError(f"{target_id} ObjectCreationUpgrade requires an effect")
        rows.append(_row("ObjectCreationUpgrade", block, fields))
    rows.sort(key=lambda r: (str(r["sourceIni"]).casefold(), int(r["line"])))
    return rows


def compile_ocl_updates(
    lineage: Sequence[SageObject], target_id: str
) -> list[dict[str, object]]:
    """Compile periodic ObjectCreationList emission bounds and amount."""

    rows: list[dict[str, object]] = []
    for block in _module_blocks(lineage, "OCLUpdate"):
        amap = _assignment_map(block)
        if (
            block.blocks
            or len(amap) != len(block.assignments)
            or set(amap) - {"ocl", "mindelay", "maxdelay", "amount"}
        ):
            raise ModuleContractError(f"{target_id} OCLUpdate malformed fields")
        ocl = _string_field(amap.get("ocl"))
        minimum = _milliseconds_field(
            amap.get("mindelay"), f"{target_id} OCLUpdate MinDelay"
        )
        maximum = _milliseconds_field(
            amap.get("maxdelay"), f"{target_id} OCLUpdate MaxDelay"
        )
        amount = _integer_assignment_field(
            amap.get("amount"), f"{target_id} OCLUpdate Amount", minimum=1
        )
        if ocl is None or minimum is None or maximum is None or amount is None:
            raise ModuleContractError(
                f"{target_id} OCLUpdate requires OCL, MinDelay, MaxDelay, and Amount"
            )
        if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", str(ocl["value"])) is None:
            raise ModuleContractError(f"{target_id} OCLUpdate OCL malformed")
        if int(minimum["milliseconds"]) > int(maximum["milliseconds"]):
            raise ModuleContractError(f"{target_id} OCLUpdate MinDelay exceeds MaxDelay")
        rows.append(_row("OCLUpdate", block, {
            "OCL": ocl, "MinDelay": minimum, "MaxDelay": maximum, "Amount": amount,
        }))
    rows.sort(key=lambda r: (str(r["sourceIni"]).casefold(), int(r["line"])))
    return rows


def _contain_passenger_bone(
    assignment: SageAssignment, target_id: str, module: str
) -> dict[str, object]:
    match = re.fullmatch(
        r"\s*PassengerBone\s*:\s*([A-Za-z_][A-Za-z0-9_]*)\s+"
        r"KindOf\s*:\s*([A-Za-z_][A-Za-z0-9_]*)\s*",
        assignment.value, re.IGNORECASE,
    )
    if match is None:
        raise ModuleContractError(f"{target_id} {module} PassengerBonePrefix malformed")
    return {
        **_authored_row(assignment), "passengerBone": match.group(1),
        "kindOf": match.group(2),
    }


def _contain_common_fields(
    grouped: Mapping[str, list[SageAssignment]], target_id: str, module: str,
    *, capacity_key: str, capacity_minimum: int = 1,
) -> dict[str, object]:
    amap = {key: values[-1] for key, values in grouped.items()}
    fields: dict[str, object] = {}
    for key in ("ObjectStatusOfContained", "PassengerFilter"):
        value = _token_list_field(amap.get(key.casefold()))
        if value is not None:
            if not value["value"]:
                raise ModuleContractError(f"{target_id} {module} {key} empty")
            fields[key] = value
    capacity = _integer_assignment_field(
        amap.get(capacity_key.casefold()), f"{target_id} {module} {capacity_key}",
        minimum=capacity_minimum,
    )
    if capacity is not None:
        fields[capacity_key] = capacity
    damage = _percent_assignment_field(
        amap.get("damagepercenttounits"), f"{target_id} {module} DamagePercentToUnits"
    )
    if damage is not None:
        if float(damage["percent"]) < 0 or float(damage["percent"]) > 100:
            raise ModuleContractError(f"{target_id} {module} DamagePercentToUnits out of range")
        fields["DamagePercentToUnits"] = damage
    for key in (
        "ShowPips", "AllowEnemiesInside", "AllowNeutralInside", "AllowAlliesInside",
        "AllowOwnPlayerInsideOverride", "EjectPassengersOnDeath",
        "KillPassengersOnDeath", "DestroyRidersWhoAreNotFreeToExit",
        "ForceOrientationContainer", "CollidePickup", "FireGrabWeaponOnVictim",
        "CanGrabStructure", "GoAggressiveOnExit",
    ):
        value = _yes_no_field(amap.get(key.casefold()), f"{target_id} {module} {key}")
        if value is not None:
            fields[key] = value
    paths = _integer_assignment_field(
        amap.get("numberofexitpaths"), f"{target_id} {module} NumberOfExitPaths",
        minimum=0,
    )
    if paths is not None:
        fields["NumberOfExitPaths"] = paths
    delay = _milliseconds_field(
        amap.get("exitdelay"), f"{target_id} {module} ExitDelay"
    )
    if delay is not None:
        fields["ExitDelay"] = delay
    for key in ("EntryPosition", "EntryOffset", "ExitOffset"):
        value = _coord_field(amap.get(key.casefold()), f"{target_id} {module} {key}")
        if value is not None:
            fields[key] = value
    sound = _string_field(amap.get("entersound"))
    if sound is not None:
        if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", str(sound["value"])) is None:
            raise ModuleContractError(f"{target_id} {module} EnterSound malformed")
        fields["EnterSound"] = sound
    bones = [
        _contain_passenger_bone(item, target_id, module)
        for item in grouped.get("passengerboneprefix", [])
    ]
    if bones:
        fields["PassengerBonePrefix"] = bones
    return fields


_TRANSPORT_CONTAIN_FIELDS = frozenset({
    "objectstatusofcontained", "passengerfilter", "slots", "showpips",
    "allowenemiesinside", "allowneutralinside", "allowalliesinside",
    "damagepercenttounits", "typeoneforweaponset", "typeoneforweaponstate",
    "typetwoforweaponstate", "passengerboneprefix", "ejectpassengersondeath",
    "killpassengersondeath", "numberofexitpaths",
    "destroyriderswhoarenotfreetoexit", "forceorientationcontainer",
    "collidepickup", "grabweapon", "firegrabweapononvictim",
    "releasesnappyness", "manualpickupfilter", "typetwoforweaponset",
    "allowownplayerinsideoverride", "exitdelay", "bonespecificconditionstate",
    "fadefilter", "upgradecreationtrigger", "cangrabstructure",
})


def compile_transport_contains(
    lineage: Sequence[SageObject], target_id: str
) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    repeated = {
        "passengerboneprefix", "typeoneforweaponset", "bonespecificconditionstate",
        "upgradecreationtrigger",
    }
    for block in _module_blocks(lineage, "TransportContain"):
        if block.blocks:
            raise ModuleContractError(f"{target_id} TransportContain nested blocks")
        grouped: dict[str, list[SageAssignment]] = {}
        for assignment in block.assignments:
            grouped.setdefault(assignment.key.casefold(), []).append(assignment)
        if set(grouped) - _TRANSPORT_CONTAIN_FIELDS or any(
            len(values) > 1 and key not in repeated for key, values in grouped.items()
        ):
            raise ModuleContractError(f"{target_id} TransportContain malformed fields")
        fields = _contain_common_fields(
            grouped, target_id, "TransportContain", capacity_key="Slots"
        )
        for required in (
            "ObjectStatusOfContained", "PassengerFilter", "Slots", "ShowPips",
            "AllowEnemiesInside", "AllowNeutralInside", "AllowAlliesInside",
        ):
            if required not in fields:
                raise ModuleContractError(f"{target_id} TransportContain requires {required}")
        amap = {key: values[-1] for key, values in grouped.items()}
        for key in ("ManualPickUpFilter", "FadeFilter"):
            value = _token_list_field(amap.get(key.casefold()))
            if value is not None:
                if not value["value"]:
                    raise ModuleContractError(f"{target_id} TransportContain {key} empty")
                fields[key] = value
        grab = _string_field(amap.get("grabweapon"))
        if grab is not None:
            if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", str(grab["value"])) is None:
                raise ModuleContractError(f"{target_id} TransportContain GrabWeapon malformed")
            fields["GrabWeapon"] = grab
        release = _number_field(
            amap.get("releasesnappyness"), f"{target_id} TransportContain ReleaseSnappyness"
        )
        if release is not None:
            if float(release["value"]) < 0:
                raise ModuleContractError(f"{target_id} TransportContain ReleaseSnappyness negative")
            fields["ReleaseSnappyness"] = release
        for key in (
            "TypeOneForWeaponSet", "TypeOneForWeaponState", "TypeTwoForWeaponSet",
            "TypeTwoForWeaponState",
        ):
            values: list[dict[str, object]] = []
            for assignment in grouped.get(key.casefold(), []):
                value = _string_field(assignment)
                assert value is not None
                if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", str(value["value"])) is None:
                    raise ModuleContractError(f"{target_id} TransportContain {key} malformed")
                values.append(value)
            if values:
                fields[key] = values
        states: list[dict[str, object]] = []
        for assignment in grouped.get("bonespecificconditionstate", []):
            token_row = _token_list_field(assignment)
            assert token_row is not None
            tokens = token_row["value"]
            if len(tokens) != 2 or not tokens[0].isdigit() or int(tokens[0]) < 1:
                raise ModuleContractError(
                    f"{target_id} TransportContain BoneSpecificConditionState malformed"
                )
            states.append({
                **_authored_row(assignment), "boneIndex": int(tokens[0]),
                "conditionState": tokens[1],
            })
        if states:
            fields["BoneSpecificConditionState"] = states
        triggers: list[dict[str, object]] = []
        for assignment in grouped.get("upgradecreationtrigger", []):
            token_row = _token_list_field(assignment)
            assert token_row is not None
            tokens = token_row["value"]
            if len(tokens) != 3 or not tokens[2].isdigit() or int(tokens[2]) < 1:
                raise ModuleContractError(
                    f"{target_id} TransportContain UpgradeCreationTrigger malformed"
                )
            triggers.append({
                **_authored_row(assignment), "upgrade": tokens[0],
                "object": tokens[1], "count": int(tokens[2]),
            })
        if triggers:
            fields["UpgradeCreationTrigger"] = triggers
        rows.append(_row("TransportContain", block, fields))
    rows.sort(key=lambda r: (str(r["sourceIni"]).casefold(), int(r["line"])))
    return rows


_TUNNEL_CONTAIN_FIELDS = frozenset({
    "objectstatusofcontained", "containmax", "damagepercenttounits",
    "passengerfilter", "allowenemiesinside", "allowneutralinside",
    "numberofexitpaths", "passengerboneprefix", "entryposition", "entryoffset",
    "exitoffset", "entersound", "killpassengersondeath", "showpips",
    "allowalliesinside", "allowownplayerinsideoverride", "exitdelay",
})
_HORDE_GARRISON_FIELDS = _TUNNEL_CONTAIN_FIELDS
_GARRISON_FIELDS = frozenset({
    "objectstatusofcontained", "containmax", "passengerfilter",
    "allowalliesinside", "allowenemiesinside",
})


def _compile_garrison_like(
    lineage: Sequence[SageObject], target_id: str, module: str,
    allowed: frozenset[str], required: tuple[str, ...],
) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for block in _module_blocks(lineage, module):
        if block.blocks:
            raise ModuleContractError(f"{target_id} {module} nested blocks")
        grouped: dict[str, list[SageAssignment]] = {}
        for assignment in block.assignments:
            grouped.setdefault(assignment.key.casefold(), []).append(assignment)
        if set(grouped) - allowed or any(len(values) > 1 for values in grouped.values()):
            raise ModuleContractError(f"{target_id} {module} malformed fields")
        fields = _contain_common_fields(
            grouped, target_id, module, capacity_key="ContainMax"
        )
        for key in required:
            if key not in fields:
                raise ModuleContractError(f"{target_id} {module} requires {key}")
        rows.append(_row(module, block, fields))
    rows.sort(key=lambda r: (str(r["sourceIni"]).casefold(), int(r["line"])))
    return rows


def compile_tunnel_contains(lineage: Sequence[SageObject], target_id: str) -> list[dict[str, object]]:
    return _compile_garrison_like(
        lineage, target_id, "TunnelContain", _TUNNEL_CONTAIN_FIELDS,
        (
            "ObjectStatusOfContained", "ContainMax", "DamagePercentToUnits",
            "PassengerFilter", "AllowEnemiesInside", "AllowNeutralInside",
            "NumberOfExitPaths", "PassengerBonePrefix", "EntryPosition",
            "EntryOffset", "ExitOffset", "EnterSound", "KillPassengersOnDeath",
            "ShowPips",
        ),
    )


def compile_garrison_contains(lineage: Sequence[SageObject], target_id: str) -> list[dict[str, object]]:
    return _compile_garrison_like(
        lineage, target_id, "GarrisonContain", _GARRISON_FIELDS,
        (
            "ObjectStatusOfContained", "ContainMax", "PassengerFilter",
            "AllowAlliesInside", "AllowEnemiesInside",
        ),
    )


def compile_horde_garrison_contains(
    lineage: Sequence[SageObject], target_id: str
) -> list[dict[str, object]]:
    return _compile_garrison_like(
        lineage, target_id, "HordeGarrisonContain", _HORDE_GARRISON_FIELDS,
        (
            "ObjectStatusOfContained", "ContainMax", "DamagePercentToUnits",
            "PassengerFilter", "AllowEnemiesInside", "EntryPosition",
            "EntryOffset", "ExitOffset",
        ),
    )


def compile_large_group_bonus_updates(
    lineage: Sequence[SageObject], target_id: str
) -> list[dict[str, object]]:
    allowed = {
        "updaterate", "hordememberfilter", "count", "radius", "ruboffradius",
        "alliesonly", "attributemodifier",
    }
    rows: list[dict[str, object]] = []
    for block in _module_blocks(lineage, "LargeGroupBonusUpdate"):
        amap = _assignment_map(block)
        if block.blocks or len(amap) != len(block.assignments) or set(amap) != allowed:
            raise ModuleContractError(f"{target_id} LargeGroupBonusUpdate malformed fields")
        update = _milliseconds_field(
            amap.get("updaterate"), f"{target_id} LargeGroupBonusUpdate UpdateRate"
        )
        member_filter = _token_list_field(amap.get("hordememberfilter"))
        count = _integer_assignment_field(
            amap.get("count"), f"{target_id} LargeGroupBonusUpdate Count", minimum=1
        )
        radius = _number_field(
            amap.get("radius"), f"{target_id} LargeGroupBonusUpdate Radius"
        )
        ruboff = _number_field(
            amap.get("ruboffradius"), f"{target_id} LargeGroupBonusUpdate RubOffRadius"
        )
        allies = _yes_no_field(
            amap.get("alliesonly"), f"{target_id} LargeGroupBonusUpdate AlliesOnly"
        )
        modifier = _string_field(amap.get("attributemodifier"))
        if (
            update is None or member_filter is None or not member_filter["value"]
            or count is None or radius is None or ruboff is None or allies is None
            or modifier is None
        ):
            raise ModuleContractError(f"{target_id} LargeGroupBonusUpdate incomplete")
        if float(radius["value"]) < 0 or float(ruboff["value"]) < float(radius["value"]):
            raise ModuleContractError(f"{target_id} LargeGroupBonusUpdate invalid radii")
        if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", str(modifier["value"])) is None:
            raise ModuleContractError(f"{target_id} LargeGroupBonusUpdate AttributeModifier malformed")
        rows.append(_row("LargeGroupBonusUpdate", block, {
            "UpdateRate": update, "HordeMemberFilter": member_filter,
            "Count": count, "Radius": radius, "RubOffRadius": ruboff,
            "AlliesOnly": allies, "AttributeModifier": modifier,
        }))
    rows.sort(key=lambda r: (str(r["sourceIni"]).casefold(), int(r["line"])))
    return rows


_PRODUCTION_QUEUE_HORDE_FIELDS = frozenset({
    "objectstatusofcontained", "containmax", "damagepercenttounits",
    "passengerfilter", "allowenemiesinside", "allowneutralinside",
    "allowalliesinside", "numberofexitpaths", "entryposition", "entryoffset",
    "exitoffset", "entersound",
})


def compile_production_queue_horde_contains(
    lineage: Sequence[SageObject], target_id: str
) -> list[dict[str, object]]:
    required = (
        "ObjectStatusOfContained", "ContainMax", "DamagePercentToUnits",
        "PassengerFilter", "AllowEnemiesInside", "AllowNeutralInside",
        "AllowAlliesInside", "NumberOfExitPaths", "EntryPosition", "EntryOffset",
        "ExitOffset", "EnterSound",
    )
    return _compile_garrison_like(
        lineage, target_id, "ProductionQueueHordeContain",
        _PRODUCTION_QUEUE_HORDE_FIELDS, required,
    )


_SIEGE_ENGINE_CONTAIN_FIELDS = frozenset({
    "objectstatusofcrew", "objectstatusofcontained", "slots",
    "damagepercenttounits", "passengerfilter", "killpassengersondeath",
    "allowalliesinside", "allowenemiesinside", "allowneutralinside",
    "crewfilter", "crewmax", "initialcrew", "exitdelay", "numberofexitpaths",
    "goaggressiveonexit", "typeoneforweaponset", "ejectpassengersondeath",
    "showpips", "passengerboneprefix", "bonespecificconditionstate",
    "speedpercentpercrew",
})


def compile_siege_engine_contains(
    lineage: Sequence[SageObject], target_id: str
) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for block in _module_blocks(lineage, "SiegeEngineContain"):
        if block.blocks:
            raise ModuleContractError(f"{target_id} SiegeEngineContain nested blocks")
        grouped: dict[str, list[SageAssignment]] = {}
        for assignment in block.assignments:
            grouped.setdefault(assignment.key.casefold(), []).append(assignment)
        if set(grouped) - _SIEGE_ENGINE_CONTAIN_FIELDS or any(
            len(values) > 1 and key != "bonespecificconditionstate"
            for key, values in grouped.items()
        ):
            raise ModuleContractError(f"{target_id} SiegeEngineContain malformed fields")
        fields = _contain_common_fields(
            grouped, target_id, "SiegeEngineContain", capacity_key="Slots",
            capacity_minimum=0,
        )
        for required in (
            "Slots", "DamagePercentToUnits", "PassengerFilter", "AllowAlliesInside",
            "AllowEnemiesInside", "AllowNeutralInside", "ExitDelay",
            "NumberOfExitPaths", "GoAggressiveOnExit",
        ):
            if required not in fields:
                raise ModuleContractError(f"{target_id} SiegeEngineContain requires {required}")
        amap = {key: values[-1] for key, values in grouped.items()}
        crew_status = _token_list_field(amap.get("objectstatusofcrew"))
        if crew_status is not None:
            fields["ObjectStatusOfCrew"] = crew_status
        if ("ObjectStatusOfCrew" in fields) == ("ObjectStatusOfContained" in fields):
            raise ModuleContractError(
                f"{target_id} SiegeEngineContain requires exactly one contained status field"
            )
        crew_filter = _token_list_field(amap.get("crewfilter"))
        crew_max = _integer_assignment_field(
            amap.get("crewmax"), f"{target_id} SiegeEngineContain CrewMax", minimum=1
        )
        initial = amap.get("initialcrew")
        crew_fields = (crew_filter, crew_max, initial)
        has_crew_status = "ObjectStatusOfCrew" in fields
        has_any_crew_field = any(item is not None for item in crew_fields)
        if has_crew_status != has_any_crew_field:
            raise ModuleContractError(
                f"{target_id} SiegeEngineContain crew status/fields mismatch"
            )
        if any(item is not None for item in crew_fields):
            if any(item is None for item in crew_fields):
                raise ModuleContractError(f"{target_id} SiegeEngineContain incomplete crew fields")
            assert crew_filter is not None and crew_max is not None and initial is not None
            if not crew_filter["value"]:
                raise ModuleContractError(f"{target_id} SiegeEngineContain CrewFilter empty")
            initial_tokens = _token_list_field(initial)
            assert initial_tokens is not None
            values = initial_tokens["value"]
            if len(values) != 2 or not values[1].isdigit() or int(values[1]) < 1:
                raise ModuleContractError(f"{target_id} SiegeEngineContain InitialCrew malformed")
            if int(values[1]) > int(crew_max["value"]):
                raise ModuleContractError(f"{target_id} SiegeEngineContain InitialCrew exceeds CrewMax")
            fields["CrewFilter"] = crew_filter
            fields["CrewMax"] = crew_max
            fields["InitialCrew"] = {
                **_authored_row(initial), "object": values[0], "count": int(values[1]),
            }
        weapon_set = _string_field(amap.get("typeoneforweaponset"))
        if weapon_set is not None:
            if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", str(weapon_set["value"])) is None:
                raise ModuleContractError(f"{target_id} SiegeEngineContain TypeOneForWeaponSet malformed")
            fields["TypeOneForWeaponSet"] = weapon_set
        speed = _percent_assignment_field(
            amap.get("speedpercentpercrew"), f"{target_id} SiegeEngineContain SpeedPercentPerCrew"
        )
        if speed is not None:
            if float(speed["percent"]) < 0 or float(speed["percent"]) > 100:
                raise ModuleContractError(f"{target_id} SiegeEngineContain SpeedPercentPerCrew out of range")
            fields["SpeedPercentPerCrew"] = speed
        states: list[dict[str, object]] = []
        for assignment in grouped.get("bonespecificconditionstate", []):
            token_row = _token_list_field(assignment)
            assert token_row is not None
            values = token_row["value"]
            if len(values) != 2 or not values[0].isdigit() or int(values[0]) < 1:
                raise ModuleContractError(
                    f"{target_id} SiegeEngineContain BoneSpecificConditionState malformed"
                )
            states.append({
                **_authored_row(assignment), "boneIndex": int(values[0]),
                "conditionState": values[1],
            })
        if states:
            fields["BoneSpecificConditionState"] = states
        rows.append(_row("SiegeEngineContain", block, fields))
    rows.sort(key=lambda r: (str(r["sourceIni"]).casefold(), int(r["line"])))
    return rows


def compile_large_group_audio_updates(
    lineage: Sequence[SageObject], target_id: str
) -> list[dict[str, object]]:
    """Compile retail large-group audio category membership and weight."""

    rows: list[dict[str, object]] = []
    for block in _module_blocks(lineage, "LargeGroupAudioUpdate"):
        if block.blocks or len(_assignment_map(block)) != len(block.assignments):
            raise ModuleContractError(f"{target_id} LargeGroupAudioUpdate malformed fields")
        amap = _assignment_map(block)
        if set(amap) - {"key", "unitweight"}:
            raise ModuleContractError(f"{target_id} LargeGroupAudioUpdate unsupported fields")
        key = _token_list_field(amap.get("key"))
        if key is None or not key["value"]:
            raise ModuleContractError(f"{target_id} LargeGroupAudioUpdate requires Key")
        fields: dict[str, object] = {"Key": key}
        weight = _integer_assignment_field(
            amap.get("unitweight"), f"{target_id} LargeGroupAudioUpdate UnitWeight",
            minimum=1,
        )
        if weight is not None:
            fields["UnitWeight"] = weight
        rows.append(_row("LargeGroupAudioUpdate", block, fields))
    rows.sort(key=lambda r: (str(r["sourceIni"]).casefold(), int(r["line"])))
    return rows


_HIT_REACTION_FIELDS = frozenset({
    "hitreactionlifetimer1", "hitreactionlifetimer2", "hitreactionlifetimer3",
    "hitreactionthreshold1", "hitreactionthreshold2", "hitreactionthreshold3",
    "fasthitsresetreaction",
})
_FAST_HITS_COMMENT = (
    "If set -- when hits occur faster than the reaction animations, it will "
    "reset the animation. (like getting riddled with machine gun bullets)"
)


def _fast_hits_bool_field(
    assignment: SageAssignment | None, label: str
) -> dict[str, object] | None:
    if assignment is None:
        return None
    parts = assignment.value.strip().split(None, 1)
    if not parts or parts[0].casefold() not in {"yes", "no"}:
        raise ModuleContractError(f"{label} must be Yes or No")
    if len(parts) == 2 and parts[1].strip() != _FAST_HITS_COMMENT:
        raise ModuleContractError(f"{label} has unsupported trailing text")
    row: dict[str, object] = {
        **_authored_row(assignment), "value": parts[0].casefold() == "yes",
    }
    if len(parts) == 2:
        row["annotation"] = parts[1].strip()
    return row


def compile_hit_reaction_behaviors(
    lineage: Sequence[SageObject], target_id: str
) -> list[dict[str, object]]:
    """Compile the one-tier or three-tier retail hit-reaction thresholds."""

    rows: list[dict[str, object]] = []
    for block in _module_blocks(lineage, "HitReactionBehavior"):
        amap = _assignment_map(block)
        if (
            block.blocks or len(amap) != len(block.assignments)
            or set(amap) - _HIT_REACTION_FIELDS
        ):
            raise ModuleContractError(f"{target_id} HitReactionBehavior malformed fields")
        fields: dict[str, object] = {}
        for index in range(1, 4):
            timer_key = f"HitReactionLifeTimer{index}"
            threshold_key = f"HitReactionThreshold{index}"
            timer = _integer_assignment_field(
                amap.get(timer_key.casefold()),
                f"{target_id} HitReactionBehavior {timer_key}", minimum=0,
            )
            threshold = _number_field(
                amap.get(threshold_key.casefold()),
                f"{target_id} HitReactionBehavior {threshold_key}",
            )
            if (timer is None) != (threshold is None):
                raise ModuleContractError(
                    f"{target_id} HitReactionBehavior incomplete tier {index}"
                )
            if threshold is not None and float(threshold["value"]) < 0:
                raise ModuleContractError(
                    f"{target_id} HitReactionBehavior negative threshold"
                )
            if timer is not None:
                fields[timer_key] = timer
                fields[threshold_key] = threshold
        if "HitReactionLifeTimer1" not in fields:
            raise ModuleContractError(f"{target_id} HitReactionBehavior requires tier 1")
        if ("HitReactionLifeTimer2" in fields) != ("HitReactionLifeTimer3" in fields):
            raise ModuleContractError(
                f"{target_id} HitReactionBehavior requires either one or three tiers"
            )
        reset = _fast_hits_bool_field(
            amap.get("fasthitsresetreaction"),
            f"{target_id} HitReactionBehavior FastHitsResetReaction",
        )
        if reset is not None:
            fields["FastHitsResetReaction"] = reset
        rows.append(_row("HitReactionBehavior", block, fields))
    rows.sort(key=lambda r: (str(r["sourceIni"]).casefold(), int(r["line"])))
    return rows


_ANIMAL_AI_FIELDS = frozenset({
    "fleerange", "fleedistance", "wanderpercentage", "maxwanderdistance",
    "maxwanderradius", "updatetimer",
})


def compile_animal_ai_updates(
    lineage: Sequence[SageObject], target_id: str
) -> list[dict[str, object]]:
    """Compile animal flee/wander bounds and polling timer."""

    rows: list[dict[str, object]] = []
    for block in _module_blocks(lineage, "AnimalAIUpdate"):
        amap = _assignment_map(block)
        if (
            block.blocks or len(amap) != len(block.assignments)
            or set(amap) - _ANIMAL_AI_FIELDS
        ):
            raise ModuleContractError(f"{target_id} AnimalAIUpdate malformed fields")
        fields: dict[str, object] = {}
        for key in ("FleeRange", "FleeDistance", "WanderPercentage", "MaxWanderDistance", "MaxWanderRadius"):
            value = _number_field(
                amap.get(key.casefold()), f"{target_id} AnimalAIUpdate {key}"
            )
            if value is not None:
                if float(value["value"]) < 0:
                    raise ModuleContractError(f"{target_id} AnimalAIUpdate negative {key}")
                fields[key] = value
        for required in ("FleeRange", "WanderPercentage", "MaxWanderDistance", "MaxWanderRadius"):
            if required not in fields:
                raise ModuleContractError(f"{target_id} AnimalAIUpdate requires {required}")
        if float(fields["WanderPercentage"]["value"]) > 100:
            raise ModuleContractError(f"{target_id} AnimalAIUpdate WanderPercentage out of range")
        timer = _milliseconds_field(
            amap.get("updatetimer"), f"{target_id} AnimalAIUpdate UpdateTimer"
        )
        if timer is not None:
            fields["UpdateTimer"] = timer
        rows.append(_row("AnimalAIUpdate", block, fields))
    rows.sort(key=lambda r: (str(r["sourceIni"]).casefold(), int(r["line"])))
    return rows


def compile_threat_finder_updates(
    lineage: Sequence[SageObject], target_id: str
) -> list[dict[str, object]]:
    """Compile the retail threat-search radius, including C-style float suffix."""

    rows: list[dict[str, object]] = []
    for block in _module_blocks(lineage, "ThreatFinderUpdate"):
        amap = _assignment_map(block)
        if block.blocks or len(amap) != 1 or set(amap) != {"defaultradius"}:
            raise ModuleContractError(f"{target_id} ThreatFinderUpdate malformed fields")
        assignment = amap["defaultradius"]
        match = re.fullmatch(r"\s*(\d+(?:\.\d*)?|\.\d+)([fF]?)\s*", assignment.value)
        if match is None:
            raise ModuleContractError(f"{target_id} ThreatFinderUpdate DefaultRadius malformed")
        fields = {"DefaultRadius": {
            **_authored_row(assignment), "value": float(match.group(1)),
            "suffix": match.group(2),
        }}
        rows.append(_row("ThreatFinderUpdate", block, fields))
    rows.sort(key=lambda r: (str(r["sourceIni"]).casefold(), int(r["line"])))
    return rows


_MODEL_SOUND_FIELDS = frozenset({
    "voiceattack", "voiceattackcharge", "voiceattackmachine",
    "voiceattackstructure", "voicefear", "voicemove", "voicemovetocamp",
    "voicemovewhileattacking", "voiceretreattocastle", "voiceselect",
    "voiceselectbattle", "voiceguard", "voiceenterstatemove",
    "voiceenterstatemovetocamp", "voiceenterstatemovewhileattacking",
    "soundimpact", "soundmoveloop", "voicepriority",
})


def compile_model_condition_sound_selectors(
    lineage: Sequence[SageObject], target_id: str
) -> list[dict[str, object]]:
    """Compile model-condition keyed sound overrides and priority."""

    rows: list[dict[str, object]] = []
    for block in _module_blocks(lineage, "ModelConditionSoundSelectorClientBehavior"):
        if block.assignments or not block.blocks:
            raise ModuleContractError(
                f"{target_id} ModelConditionSoundSelectorClientBehavior malformed body"
            )
        states: list[dict[str, object]] = []
        for state in block.blocks:
            if (
                (state.header_key or "").casefold() != "soundstate"
                or len(state.header_tokens) != 1 or len(state.blocks) > 1
            ):
                raise ModuleContractError(f"{target_id} malformed SoundState")
            amap = _assignment_map(state)
            if (
                not amap or len(amap) != len(state.assignments)
                or set(amap) - _MODEL_SOUND_FIELDS
            ):
                raise ModuleContractError(f"{target_id} unsupported SoundState fields")
            sounds: dict[str, object] = {}
            for assignment in state.assignments:
                if assignment.key.casefold() == "voicepriority":
                    value = _integer_assignment_field(
                        assignment, f"{target_id} SoundState VoicePriority", minimum=0
                    )
                else:
                    value = _string_field(assignment)
                    assert value is not None
                    if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", str(value["value"])) is None:
                        raise ModuleContractError(
                            f"{target_id} SoundState {assignment.key} malformed"
                        )
                assert value is not None
                sounds[assignment.key] = value
            state_row: dict[str, object] = {
                "conditions": list(state.header_tokens), "sounds": sounds,
                "sourceIni": state.source_virtual_path, "line": state.line,
            }
            if state.blocks:
                specific = state.blocks[0]
                smap = _assignment_map(specific)
                if (
                    specific.kind.casefold() != "unitspecificsounds" or specific.blocks
                    or len(smap) != len(specific.assignments) or not smap
                    or set(smap) - {"voicegarrison", "voicemovetotrees"}
                ):
                    raise ModuleContractError(f"{target_id} malformed UnitSpecificSounds")
                specific_sounds: dict[str, object] = {}
                for assignment in specific.assignments:
                    value = _string_field(assignment)
                    assert value is not None
                    if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", str(value["value"])) is None:
                        raise ModuleContractError(
                            f"{target_id} UnitSpecificSounds {assignment.key} malformed"
                        )
                    specific_sounds[assignment.key] = value
                state_row["unitSpecificSounds"] = {
                    "sounds": specific_sounds, "sourceIni": specific.source_virtual_path,
                    "line": specific.line,
                }
            states.append(state_row)
        rows.append(_row(
            "ModelConditionSoundSelectorClientBehavior", block,
            {"SoundState": states},
        ))
    rows.sort(key=lambda r: (str(r["sourceIni"]).casefold(), int(r["line"])))
    return rows


def compile_random_sound_selectors(
    lineage: Sequence[SageObject], target_id: str
) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    required = {"chance", "rerolloneveryframe", "voicepriority"}
    for block in _module_blocks(lineage, "RandomSoundSelectorClientBehavior"):
        amap = _assignment_map(block)
        if block.blocks or len(amap) != len(block.assignments) or set(amap) != required:
            raise ModuleContractError(f"{target_id} RandomSoundSelectorClientBehavior malformed")
        chance = _percent_assignment_field(
            amap["chance"], f"{target_id} RandomSoundSelector Chance"
        )
        assert chance is not None
        if float(chance["percent"]) < 0 or float(chance["percent"]) > 100:
            raise ModuleContractError(f"{target_id} RandomSoundSelector Chance out of range")
        reroll = _yes_no_field(
            amap["rerolloneveryframe"], f"{target_id} RandomSoundSelector RerollOnEveryFrame"
        )
        priority = _integer_assignment_field(
            amap["voicepriority"], f"{target_id} RandomSoundSelector VoicePriority",
            minimum=0,
        )
        rows.append(_row("RandomSoundSelectorClientBehavior", block, {
            "Chance": chance, "RerollOnEveryFrame": reroll, "VoicePriority": priority,
        }))
    rows.sort(key=lambda r: (str(r["sourceIni"]).casefold(), int(r["line"])))
    return rows


_ANIMATION_SOUND_LINE_RE = re.compile(
    r"^Sound:\s*(?P<event>\S+)\s+(?P<groups>Animation:.+)$"
)
_ANIMATION_SOUND_GROUP_RE = re.compile(
    r"Animation:\s*(?P<animation>\S+)\s+Frames:\s*(?P<frames>[0-9]+(?:\s+[0-9]+)*)"
)
_ANIMATION_SOUND_SUPPORTED = frozenset({"animationsound", "maxupdaterangecap"})


def _strip_animation_sound_comments(raw: str) -> str:
    text = raw.strip()
    for marker in ("//", ";,;", ";"):
        index = text.find(marker)
        if index >= 0:
            text = text[:index].strip()
    return text


def _animation_sound_row_has_closed_runtime(fields: Mapping[str, object]) -> bool:
    sounds = fields.get("AnimationSound")
    return isinstance(sounds, list) and bool(sounds)


def compile_animation_sound_client_behaviors(
    lineage: Sequence[SageObject], target_id: str
) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for block in _module_blocks(lineage, "AnimationSoundClientBehavior"):
        authored = {a.key.casefold() for a in block.assignments}
        unknown = authored - _ANIMATION_SOUND_SUPPORTED
        if unknown or block.blocks:
            raise ModuleContractError(
                f"{target_id} AnimationSoundClientBehavior unsupported fields: "
                + ", ".join(sorted(unknown))
            )
        sounds: list[dict[str, object]] = []
        deferred: list[dict[str, object]] = []
        for assignment in block.assignments:
            if assignment.key.casefold() != "animationsound":
                continue
            raw = _strip_animation_sound_comments(assignment.value)
            if "requiredmc:" in raw.casefold():
                deferred.append(
                    {
                        "name": assignment.key,
                        "authored": assignment.value,
                        "sourceIni": assignment.source_virtual_path,
                        "line": assignment.line,
                        "reason": "required-model-condition-gate-without-runtime-oracle",
                    }
                )
                continue
            match = _ANIMATION_SOUND_LINE_RE.fullmatch(raw)
            groups = (
                list(_ANIMATION_SOUND_GROUP_RE.finditer(match.group("groups")))
                if match is not None
                else []
            )
            remainder = (
                _ANIMATION_SOUND_GROUP_RE.sub("", match.group("groups")).strip()
                if match is not None
                else raw
            )
            if match is None or not groups or remainder:
                deferred.append(
                    {
                        "name": assignment.key,
                        "authored": assignment.value,
                        "sourceIni": assignment.source_virtual_path,
                        "line": assignment.line,
                        "reason": "unparsed-animation-sound-line",
                    }
                )
                continue
            for group in groups:
                sounds.append(
                    {
                        "eventId": match.group("event"),
                        "animation": group.group("animation"),
                        "frames": [int(value) for value in group.group("frames").split()],
                        "authored": assignment.value,
                        "sourceIni": assignment.source_virtual_path,
                        "line": assignment.line,
                    }
                )
        fields: dict[str, object] = {"AnimationSound": sounds}
        cap = _number_field(
            _assignment_map(block).get("maxupdaterangecap"),
            f"{target_id} AnimationSoundClientBehavior MaxUpdateRangeCap",
        )
        if cap is not None:
            fields["MaxUpdateRangeCap"] = cap
        if deferred:
            fields["deferredAnimationSound"] = deferred
        rows.append(
            _row(
                "AnimationSoundClientBehavior",
                block,
                fields,
                runtime_status=(
                    "executable"
                    if _animation_sound_row_has_closed_runtime(fields)
                    else "deferred"
                ),
            )
        )
    rows.sort(key=lambda row: (str(row["sourceIni"]).casefold(), int(row["line"])))
    return rows


_RADIATE_FEAR_FIELDS = frozenset({
    "initiallyactive", "triggeredby", "whichspecialpower", "generateterror",
    "generatefear", "generateuncontrollablefear", "emotionpulseradius",
    "emotionpulseinterval", "victimfilter",
})


def _number_or_define_field(
    assignment: SageAssignment | None, label: str
) -> dict[str, object] | None:
    if assignment is None:
        return None
    try:
        return _number_field(assignment, label)
    except ModuleContractError:
        token = assignment.value.strip()
        if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", token) is None:
            raise
        return {**_authored_row(assignment), "define": token}


def compile_radiate_fear_updates(
    lineage: Sequence[SageObject], target_id: str
) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for block in _module_blocks(lineage, "RadiateFearUpdate"):
        amap = _assignment_map(block)
        if (
            block.blocks or len(amap) != len(block.assignments)
            or set(amap) - _RADIATE_FEAR_FIELDS
        ):
            raise ModuleContractError(f"{target_id} RadiateFearUpdate malformed fields")
        fields: dict[str, object] = {}
        for key in (
            "InitiallyActive", "GenerateTerror", "GenerateFear",
            "GenerateUncontrollableFear",
        ):
            value = _yes_no_field(amap.get(key.casefold()), f"{target_id} RadiateFearUpdate {key}")
            if value is not None:
                fields[key] = value
        if "InitiallyActive" not in fields:
            raise ModuleContractError(f"{target_id} RadiateFearUpdate requires InitiallyActive")
        generators = (
            fields.get("GenerateTerror"), fields.get("GenerateFear"),
            fields.get("GenerateUncontrollableFear"),
        )
        if not any(item is not None and item["value"] for item in generators):
            raise ModuleContractError(f"{target_id} RadiateFearUpdate requires an active fear type")
        triggered = _string_field(amap.get("triggeredby"))
        power = _integer_assignment_field(
            amap.get("whichspecialpower"), f"{target_id} RadiateFearUpdate WhichSpecialPower",
            minimum=0,
        )
        if (triggered is None) != (power is None):
            raise ModuleContractError(f"{target_id} RadiateFearUpdate incomplete trigger fields")
        if triggered is not None:
            if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", str(triggered["value"])) is None:
                raise ModuleContractError(f"{target_id} RadiateFearUpdate TriggeredBy malformed")
            fields["TriggeredBy"] = triggered
            fields["WhichSpecialPower"] = power
        radius = _number_or_define_field(
            amap.get("emotionpulseradius"), f"{target_id} RadiateFearUpdate EmotionPulseRadius"
        )
        interval = _milliseconds_field(
            amap.get("emotionpulseinterval"), f"{target_id} RadiateFearUpdate EmotionPulseInterval"
        )
        if radius is None or interval is None:
            raise ModuleContractError(f"{target_id} RadiateFearUpdate requires pulse radius/interval")
        if "value" in radius and float(radius["value"]) < 0:
            raise ModuleContractError(f"{target_id} RadiateFearUpdate negative radius")
        fields["EmotionPulseRadius"] = radius
        fields["EmotionPulseInterval"] = interval
        victim = _token_list_field(amap.get("victimfilter"))
        if victim is not None:
            if not victim["value"]:
                raise ModuleContractError(f"{target_id} RadiateFearUpdate empty VictimFilter")
            fields["VictimFilter"] = victim
        rows.append(_row("RadiateFearUpdate", block, fields))
    rows.sort(key=lambda r: (str(r["sourceIni"]).casefold(), int(r["line"])))
    return rows


def compile_poisoned_behaviors(
    lineage: Sequence[SageObject], target_id: str
) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for block in _module_blocks(lineage, "PoisonedBehavior"):
        amap = _assignment_map(block)
        if (
            block.blocks or len(amap) != len(block.assignments)
            or set(amap) != {"poisondamageinterval", "poisonduration"}
        ):
            raise ModuleContractError(f"{target_id} PoisonedBehavior malformed fields")
        interval = _milliseconds_field(
            amap["poisondamageinterval"], f"{target_id} PoisonedBehavior PoisonDamageInterval"
        )
        duration = _milliseconds_field(
            amap["poisonduration"], f"{target_id} PoisonedBehavior PoisonDuration"
        )
        assert interval is not None and duration is not None
        if interval["milliseconds"] <= 0 or duration["milliseconds"] < interval["milliseconds"]:
            raise ModuleContractError(f"{target_id} PoisonedBehavior invalid duration")
        rows.append(_row("PoisonedBehavior", block, {
            "PoisonDamageInterval": interval, "PoisonDuration": duration,
        }))
    rows.sort(key=lambda r: (str(r["sourceIni"]).casefold(), int(r["line"])))
    return rows


def compile_damage_field_updates(
    lineage: Sequence[SageObject], target_id: str
) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for block in _module_blocks(lineage, "DamageFieldUpdate"):
        amap = _assignment_map(block)
        if (
            len(amap) != len(block.assignments)
            or set(amap) != {"radius", "objectfilter", "requiredupgrade"}
            or len(block.blocks) != 1
        ):
            raise ModuleContractError(f"{target_id} DamageFieldUpdate malformed body")
        nugget = block.blocks[0]
        nmap = _assignment_map(nugget)
        if (
            nugget.kind.casefold() != "fireweaponnugget" or nugget.blocks
            or len(nmap) != len(nugget.assignments)
            or set(nmap) != {"weaponname", "firedelay", "oneshot"}
        ):
            raise ModuleContractError(f"{target_id} DamageFieldUpdate malformed FireWeaponNugget")
        radius = _number_field(amap["radius"], f"{target_id} DamageFieldUpdate Radius")
        object_filter = _token_list_field(amap["objectfilter"])
        required_upgrade = _string_field(amap["requiredupgrade"])
        weapon = _string_field(nmap["weaponname"])
        assert radius is not None and object_filter is not None
        assert required_upgrade is not None and weapon is not None
        if float(radius["value"]) < 0 or not object_filter["value"]:
            raise ModuleContractError(f"{target_id} DamageFieldUpdate invalid radius/filter")
        for label, value in (("RequiredUpgrade", required_upgrade), ("WeaponName", weapon)):
            if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", str(value["value"])) is None:
                raise ModuleContractError(f"{target_id} DamageFieldUpdate malformed {label}")
        fire_delay = _milliseconds_field(
            nmap["firedelay"], f"{target_id} DamageFieldUpdate FireDelay"
        )
        one_shot = _yes_no_field(nmap["oneshot"], f"{target_id} DamageFieldUpdate OneShot")
        rows.append(_row("DamageFieldUpdate", block, {
            "Radius": radius, "ObjectFilter": object_filter,
            "RequiredUpgrade": required_upgrade,
            "FireWeaponNugget": {
                "WeaponName": weapon, "FireDelay": fire_delay, "OneShot": one_shot,
                "sourceIni": nugget.source_virtual_path, "line": nugget.line,
            },
        }))
    rows.sort(key=lambda r: (str(r["sourceIni"]).casefold(), int(r["line"])))
    return rows


def compile_spawn_unit_behaviors(
    lineage: Sequence[SageObject], target_id: str
) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for block in _module_blocks(lineage, "SpawnUnitBehavior"):
        amap = _assignment_map(block)
        if (
            block.blocks or len(amap) != len(block.assignments)
            or set(amap) - {"unitname", "unitcommand", "spawnonce"}
        ):
            raise ModuleContractError(f"{target_id} SpawnUnitBehavior malformed fields")
        unit = _string_field(amap.get("unitname"))
        command = _string_field(amap.get("unitcommand"))
        once = _yes_no_field(amap.get("spawnonce"), f"{target_id} SpawnUnitBehavior SpawnOnce")
        if unit is None or (command is None) != (once is None):
            raise ModuleContractError(f"{target_id} SpawnUnitBehavior incomplete fields")
        fields: dict[str, object] = {"UnitName": unit}
        for label, value in (("UnitName", unit), ("UnitCommand", command)):
            if value is not None:
                if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", str(value["value"])) is None:
                    raise ModuleContractError(f"{target_id} SpawnUnitBehavior malformed {label}")
                fields[label] = value
        if once is not None:
            fields["SpawnOnce"] = once
        rows.append(_row("SpawnUnitBehavior", block, fields))
    rows.sort(key=lambda r: (str(r["sourceIni"]).casefold(), int(r["line"])))
    return rows


def compile_replace_self_upgrades(
    lineage: Sequence[SageObject], target_id: str
) -> list[dict[str, object]]:
    """Compile retail wall-segment replacement and mutually-exclusive upgrades."""

    rows: list[dict[str, object]] = []
    allowed = {"replacewith", "andthenadda", "triggeredby", "conflictswith"}
    identifier = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
    for block in _module_blocks(lineage, "ReplaceSelfUpgrade"):
        if block.blocks:
            raise ModuleContractError(f"{target_id} ReplaceSelfUpgrade nested blocks")
        grouped: dict[str, list[SageAssignment]] = {}
        for assignment in block.assignments:
            grouped.setdefault(assignment.key.casefold(), []).append(assignment)
        if set(grouped) - allowed or any(
            len(values) != 1 for key, values in grouped.items() if key != "andthenadda"
        ):
            raise ModuleContractError(f"{target_id} ReplaceSelfUpgrade malformed fields")
        if set(grouped) - {"andthenadda"} != {
            "replacewith", "triggeredby", "conflictswith",
        }:
            raise ModuleContractError(f"{target_id} ReplaceSelfUpgrade incomplete core fields")
        additions = grouped.get("andthenadda", [])
        if additions and len(additions) != 2:
            raise ModuleContractError(
                f"{target_id} ReplaceSelfUpgrade requires exactly two AndThenAddA rows"
            )
        replace = _string_field(grouped["replacewith"][0])
        trigger = _string_field(grouped["triggeredby"][0])
        conflicts = _token_list_field(grouped["conflictswith"][0])
        assert replace is not None and trigger is not None and conflicts is not None
        if (
            identifier.fullmatch(str(replace["value"])) is None
            or identifier.fullmatch(str(trigger["value"])) is None
            or not conflicts["value"]
            or any(identifier.fullmatch(token) is None for token in conflicts["value"])
        ):
            raise ModuleContractError(f"{target_id} ReplaceSelfUpgrade malformed identifiers")
        fields: dict[str, object] = {
            "ReplaceWith": replace, "TriggeredBy": trigger,
            "ConflictsWith": conflicts,
        }
        if additions:
            compiled_additions: list[dict[str, object]] = []
            for assignment in additions:
                value = _string_field(assignment)
                assert value is not None
                if identifier.fullmatch(str(value["value"])) is None:
                    raise ModuleContractError(
                        f"{target_id} ReplaceSelfUpgrade malformed AndThenAddA"
                    )
                compiled_additions.append(value)
            fields["AndThenAddA"] = compiled_additions
        rows.append(_row("ReplaceSelfUpgrade", block, fields))
    rows.sort(key=lambda r: (str(r["sourceIni"]).casefold(), int(r["line"])))
    return rows


_CITADEL_SLAUGHTER_FIELDS = frozenset({
    "passengerfilter", "objectstatusofcontained", "cashbackpercent", "containmax",
    "allowenemiesinside", "allowalliesinside", "allowneutralinside",
    "allowownplayerinsideoverride", "entersound", "entryoffset", "entryposition",
    "exitoffset", "statusforringentry", "upgradeforringentry",
    "objecttodestroyforringentry", "fxforringentry",
})


def compile_citadel_slaughter_horde_contains(
    lineage: Sequence[SageObject], target_id: str
) -> list[dict[str, object]]:
    """Compile citadel slaughter cashback, admission, and ring-return behavior."""

    rows: list[dict[str, object]] = []
    required = {
        "passengerfilter", "objectstatusofcontained", "cashbackpercent", "containmax",
        "allowenemiesinside", "allowneutralinside", "entersound", "entryoffset",
        "entryposition", "exitoffset", "statusforringentry",
        "objecttodestroyforringentry",
    }
    identifier = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
    for block in _module_blocks(lineage, "CitadelSlaughterHordeContain"):
        amap = _assignment_map(block)
        if (
            block.blocks or len(amap) != len(block.assignments)
            or set(amap) - _CITADEL_SLAUGHTER_FIELDS
            or not required.issubset(amap)
        ):
            raise ModuleContractError(
                f"{target_id} CitadelSlaughterHordeContain malformed fields"
            )
        optional_admission = ("allowalliesinside", "allowownplayerinsideoverride")
        if sum(key in amap for key in optional_admission) not in {0, 2}:
            raise ModuleContractError(
                f"{target_id} CitadelSlaughterHordeContain incomplete own-player admission"
            )
        optional_ring = ("upgradeforringentry", "fxforringentry")
        if sum(key in amap for key in optional_ring) not in {0, 2}:
            raise ModuleContractError(
                f"{target_id} CitadelSlaughterHordeContain incomplete ring effects"
            )
        passenger_filter = _token_list_field(amap["passengerfilter"])
        status = _token_list_field(amap["objectstatusofcontained"])
        cashback = _percent_assignment_field(
            amap["cashbackpercent"],
            f"{target_id} CitadelSlaughterHordeContain CashBackPercent",
        )
        capacity = _integer_assignment_field(
            amap["containmax"], f"{target_id} CitadelSlaughterHordeContain ContainMax",
            minimum=1,
        )
        assert passenger_filter is not None and status is not None
        assert cashback is not None and capacity is not None
        if not passenger_filter["value"] or not status["value"] or float(cashback["percent"]) < 0:
            raise ModuleContractError(
                f"{target_id} CitadelSlaughterHordeContain invalid filter/status/cashback"
            )
        fields: dict[str, object] = {
            "PassengerFilter": passenger_filter,
            "ObjectStatusOfContained": status,
            "CashBackPercent": cashback,
            "ContainMax": capacity,
        }
        for key in (
            "AllowEnemiesInside", "AllowAlliesInside", "AllowNeutralInside",
            "AllowOwnPlayerInsideOverride",
        ):
            value = _yes_no_field(
                amap.get(key.casefold()), f"{target_id} CitadelSlaughterHordeContain {key}"
            )
            if value is not None:
                fields[key] = value
        for key in ("EntryOffset", "EntryPosition", "ExitOffset"):
            value = _coord_field(
                amap[key.casefold()], f"{target_id} CitadelSlaughterHordeContain {key}"
            )
            assert value is not None
            fields[key] = value
        for key in ("EnterSound", "StatusForRingEntry", "FXForRingEntry"):
            value = _string_field(amap.get(key.casefold()))
            if value is not None:
                if identifier.fullmatch(str(value["value"])) is None:
                    raise ModuleContractError(
                        f"{target_id} CitadelSlaughterHordeContain malformed {key}"
                    )
                fields[key] = value
        for key in ("UpgradeForRingEntry", "ObjectToDestroyForRingEntry"):
            value = _token_list_field(amap.get(key.casefold()))
            if value is not None:
                if not value["value"] or any(
                    token != "NONE" and identifier.fullmatch(token.lstrip("+-")) is None
                    for token in value["value"]
                ):
                    raise ModuleContractError(
                        f"{target_id} CitadelSlaughterHordeContain malformed {key}"
                    )
                fields[key] = value
        rows.append(_row("CitadelSlaughterHordeContain", block, fields))
    rows.sort(key=lambda r: (str(r["sourceIni"]).casefold(), int(r["line"])))
    return rows


_WALL_HUB_FIELDS = frozenset({
    "options", "maxbuildoutdistance", "staggeredbuildfactor",
    "segmenttemplatename", "builderradius", "hubcaptemplatename",
    "defaultsegmenttemplatename", "cliffcaptemplatename",
})


def compile_wall_hub_behaviors(
    lineage: Sequence[SageObject], target_id: str
) -> list[dict[str, object]]:
    """Compile wall-builder option geometry and ordered segment templates."""

    rows: list[dict[str, object]] = []
    identifier = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
    for block in _module_blocks(lineage, "WallHubBehavior"):
        if block.blocks:
            raise ModuleContractError(f"{target_id} WallHubBehavior nested blocks")
        grouped: dict[str, list[SageAssignment]] = {}
        for assignment in block.assignments:
            grouped.setdefault(assignment.key.casefold(), []).append(assignment)
        if set(grouped) - _WALL_HUB_FIELDS or any(
            len(values) != 1
            for key, values in grouped.items()
            if key not in {"segmenttemplatename", "maxbuildoutdistance"}
        ):
            raise ModuleContractError(f"{target_id} WallHubBehavior malformed fields")
        required = {
            "options", "maxbuildoutdistance", "segmenttemplatename",
            "hubcaptemplatename", "defaultsegmenttemplatename",
        }
        if not required.issubset(grouped) or len(grouped["maxbuildoutdistance"]) not in {1, 2}:
            raise ModuleContractError(f"{target_id} WallHubBehavior incomplete core fields")
        option = _string_field(grouped["options"][0])
        assert option is not None
        if option["value"] not in {"OPTION_ONE", "OPTION_TWO", "OPTION_THREE"}:
            raise ModuleContractError(f"{target_id} WallHubBehavior invalid Options")
        distances: list[dict[str, object]] = []
        for assignment in grouped["maxbuildoutdistance"]:
            value = _number_or_define_field(
                assignment, f"{target_id} WallHubBehavior MaxBuildoutDistance"
            )
            assert value is not None
            if "value" in value and float(value["value"]) < 0:
                raise ModuleContractError(f"{target_id} WallHubBehavior negative distance")
            distances.append(value)
        segments: list[dict[str, object]] = []
        for assignment in grouped["segmenttemplatename"]:
            value = _string_field(assignment)
            assert value is not None
            if identifier.fullmatch(str(value["value"])) is None:
                raise ModuleContractError(f"{target_id} WallHubBehavior malformed segment")
            segments.append(value)
        fields: dict[str, object] = {
            "Options": option,
            "MaxBuildoutDistance": distances,
            "EffectiveMaxBuildoutDistance": distances[-1],
            "SegmentTemplateName": segments,
        }
        stagger = _string_field(grouped.get("staggeredbuildfactor", [None])[0])
        if stagger is not None:
            if identifier.fullmatch(str(stagger["value"])) is None:
                raise ModuleContractError(f"{target_id} WallHubBehavior malformed stagger factor")
            fields["StaggeredBuildFactor"] = stagger
        builder = _number_field(
            grouped.get("builderradius", [None])[0], f"{target_id} WallHubBehavior BuilderRadius"
        )
        if builder is not None:
            if float(builder["value"]) < 0:
                raise ModuleContractError(f"{target_id} WallHubBehavior negative BuilderRadius")
            fields["BuilderRadius"] = builder
        for key in (
            "HubCapTemplateName", "DefaultSegmentTemplateName", "CliffCapTemplateName",
        ):
            value = _string_field(grouped.get(key.casefold(), [None])[0])
            if value is not None:
                if identifier.fullmatch(str(value["value"])) is None:
                    raise ModuleContractError(f"{target_id} WallHubBehavior malformed {key}")
                fields[key] = value
        rows.append(_row("WallHubBehavior", block, fields))
    rows.sort(key=lambda r: (str(r["sourceIni"]).casefold(), int(r["line"])))
    return rows


_ACTIVATE_MODULE_SPECIAL_POWER_FIELDS = frozenset({
    "specialpowertemplate", "startabilityrange", "effectrange",
    "triggerspecialpower", "mustfinishability", "startdelay",
    "preparationtime", "persistentpreptime", "unpacktime", "packtime",
    "specialpowerduration", "unpackingvariation",
})


def _milliseconds_or_define_field(
    assignment: SageAssignment | None, label: str
) -> dict[str, object] | None:
    if assignment is None:
        return None
    try:
        return _milliseconds_field(assignment, label)
    except ModuleContractError:
        token = assignment.value.strip()
        if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", token) is None:
            raise
        return {**_authored_row(assignment), "define": token}


def compile_activate_module_special_powers(
    lineage: Sequence[SageObject], target_id: str
) -> list[dict[str, object]]:
    """Compile ordered activation routes and resolve every target ModuleTag."""

    _effective_top_blocks, _tokens, _walk_blocks = _walk_helpers()
    effective_blocks = list(_walk_blocks(_effective_top_blocks(lineage)))
    tags: dict[str, SageBlock] = {}
    duplicate_tags: set[str] = set()
    for candidate in effective_blocks:
        tag = candidate.instance_tag
        if not tag:
            continue
        folded = tag.casefold()
        if folded in tags:
            duplicate_tags.add(folded)
        else:
            tags[folded] = candidate
    supported_targets = {
        "specialpowermodule", "oclspecialpower", "playerhealspecialpower",
    }
    rows: list[dict[str, object]] = []
    for block in _module_blocks(lineage, "ActivateModuleSpecialPower"):
        if block.blocks:
            raise ModuleContractError(f"{target_id} ActivateModuleSpecialPower nested blocks")
        grouped: dict[str, list[SageAssignment]] = {}
        for assignment in block.assignments:
            grouped.setdefault(assignment.key.casefold(), []).append(assignment)
        if set(grouped) - _ACTIVATE_MODULE_SPECIAL_POWER_FIELDS or any(
            len(values) != 1
            for key, values in grouped.items()
            if key != "triggerspecialpower"
        ):
            raise ModuleContractError(
                f"{target_id} ActivateModuleSpecialPower malformed fields"
            )
        required = {"specialpowertemplate", "startabilityrange", "triggerspecialpower"}
        if not required.issubset(grouped) or not grouped["triggerspecialpower"]:
            raise ModuleContractError(
                f"{target_id} ActivateModuleSpecialPower incomplete core fields"
            )
        template = _string_field(grouped["specialpowertemplate"][0])
        start_range = _number_or_define_field(
            grouped["startabilityrange"][0],
            f"{target_id} ActivateModuleSpecialPower StartAbilityRange",
        )
        assert template is not None and start_range is not None
        identifier = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
        if identifier.fullmatch(str(template["value"])) is None:
            raise ModuleContractError(
                f"{target_id} ActivateModuleSpecialPower malformed SpecialPowerTemplate"
            )
        if "value" in start_range and float(start_range["value"]) < 0:
            raise ModuleContractError(
                f"{target_id} ActivateModuleSpecialPower negative StartAbilityRange"
            )
        fields: dict[str, object] = {
            "SpecialPowerTemplate": template, "StartAbilityRange": start_range,
        }
        effect_range = _number_or_define_field(
            grouped.get("effectrange", [None])[0],
            f"{target_id} ActivateModuleSpecialPower EffectRange",
        )
        if effect_range is not None:
            if "value" in effect_range and float(effect_range["value"]) < 0:
                raise ModuleContractError(
                    f"{target_id} ActivateModuleSpecialPower negative EffectRange"
                )
            fields["EffectRange"] = effect_range
        must_finish = _yes_no_field(
            grouped.get("mustfinishability", [None])[0],
            f"{target_id} ActivateModuleSpecialPower MustFinishAbility",
        )
        if must_finish is not None:
            fields["MustFinishAbility"] = must_finish
        for key in (
            "StartDelay", "PreparationTime", "PersistentPrepTime", "UnpackTime",
            "PackTime", "SpecialPowerDuration",
        ):
            value = _milliseconds_or_define_field(
                grouped.get(key.casefold(), [None])[0],
                f"{target_id} ActivateModuleSpecialPower {key}",
            )
            if value is not None:
                fields[key] = value
        variation = _integer_assignment_field(
            grouped.get("unpackingvariation", [None])[0],
            f"{target_id} ActivateModuleSpecialPower UnpackingVariation", minimum=0,
        )
        if variation is not None:
            fields["UnpackingVariation"] = variation
        triggers: list[dict[str, object]] = []
        mode_map = {"TARGETPOS": "LOCATION", "OBJECTPOS": "CURRENT_TARGET", "SELF": "SELF"}
        for assignment in grouped["triggerspecialpower"]:
            tokens = list(_tokens(assignment.value))
            if len(tokens) != 2 or identifier.fullmatch(tokens[0]) is None:
                raise ModuleContractError(
                    f"{target_id} ActivateModuleSpecialPower TriggerSpecialPower malformed"
                )
            authored_mode = tokens[1].upper()
            if authored_mode not in mode_map:
                raise ModuleContractError(
                    f"{target_id} ActivateModuleSpecialPower unsupported target mode {tokens[1]}"
                )
            folded_tag = tokens[0].casefold()
            if folded_tag in duplicate_tags or folded_tag not in tags:
                raise ModuleContractError(
                    f"{target_id} ActivateModuleSpecialPower unresolved target tag {tokens[0]}"
                )
            target = tags[folded_tag]
            if target.kind.casefold() not in supported_targets:
                raise ModuleContractError(
                    f"{target_id} ActivateModuleSpecialPower target {tokens[0]} has unsupported kind {target.kind}"
                )
            target_template = _string_field(_assignment_map(target).get("specialpowertemplate"))
            if target_template is None:
                raise ModuleContractError(
                    f"{target_id} ActivateModuleSpecialPower target {tokens[0]} lacks SpecialPowerTemplate"
                )
            triggers.append({
                **_authored_row(assignment), "tag": tokens[0],
                "moduleTag": tokens[0], "authoredTargetMode": authored_mode,
                "targetMode": mode_map[authored_mode], "targetModuleKind": target.kind,
                "targetSpecialPowerTemplate": target_template,
                "targetSourceIni": target.source_virtual_path, "targetLine": target.line,
            })
        fields["TriggerSpecialPower"] = triggers
        rows.append(_row("ActivateModuleSpecialPower", block, fields))
    rows.sort(key=lambda r: (str(r["sourceIni"]).casefold(), int(r["line"])))
    return rows


_WEAPON_MODE_SPECIAL_POWER_UPDATE_FIELDS = frozenset({
    "specialpowertemplate", "duration", "attributemodifier",
    "weaponsetflags", "lockweaponslot", "startspaused",
})


def compile_weapon_mode_special_power_updates(
    lineage: Sequence[SageObject], target_id: str
) -> list[dict[str, object]]:
    """Compile the timed weapon-mode envelope without guessing its runtime state."""

    _effective_top_blocks, tokens, _walk_blocks = _walk_helpers()
    identifier = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
    rows: list[dict[str, object]] = []
    for block in _module_blocks(lineage, "WeaponModeSpecialPowerUpdate"):
        if block.blocks:
            raise ModuleContractError(f"{target_id} WeaponModeSpecialPowerUpdate nested blocks")
        grouped: dict[str, list[SageAssignment]] = {}
        for assignment in block.assignments:
            grouped.setdefault(assignment.key.casefold(), []).append(assignment)
        if set(grouped) - _WEAPON_MODE_SPECIAL_POWER_UPDATE_FIELDS or any(
            len(values) != 1 for values in grouped.values()
        ):
            raise ModuleContractError(
                f"{target_id} WeaponModeSpecialPowerUpdate malformed fields"
            )
        required = {"specialpowertemplate", "duration", "startspaused"}
        if not required.issubset(grouped):
            raise ModuleContractError(
                f"{target_id} WeaponModeSpecialPowerUpdate incomplete core fields"
            )
        template = _string_field(grouped["specialpowertemplate"][0])
        duration = _milliseconds_or_define_field(
            grouped["duration"][0], f"{target_id} WeaponModeSpecialPowerUpdate Duration"
        )
        starts_paused = _yes_no_field(
            grouped["startspaused"][0],
            f"{target_id} WeaponModeSpecialPowerUpdate StartsPaused",
        )
        assert template is not None and duration is not None and starts_paused is not None
        if identifier.fullmatch(str(template["value"])) is None:
            raise ModuleContractError(
                f"{target_id} WeaponModeSpecialPowerUpdate malformed SpecialPowerTemplate"
            )
        if "milliseconds" in duration and int(duration["milliseconds"]) < 0:
            raise ModuleContractError(
                f"{target_id} WeaponModeSpecialPowerUpdate negative Duration"
            )
        fields: dict[str, object] = {
            "SpecialPowerTemplate": template,
            "Duration": duration,
            "StartsPaused": starts_paused,
        }
        modifier = _string_field(grouped.get("attributemodifier", [None])[0])
        if modifier is not None:
            if identifier.fullmatch(str(modifier["value"])) is None:
                raise ModuleContractError(
                    f"{target_id} WeaponModeSpecialPowerUpdate malformed AttributeModifier"
                )
            fields["AttributeModifier"] = modifier
        flag_assignment = grouped.get("weaponsetflags", [None])[0]
        if flag_assignment is not None:
            flag_values = list(tokens(flag_assignment.value))
            if not flag_values or any(
                re.fullmatch(r"WEAPONSET_[A-Z0-9_]+", value) is None
                for value in flag_values
            ):
                raise ModuleContractError(
                    f"{target_id} WeaponModeSpecialPowerUpdate malformed WeaponSetFlags"
                )
            fields["WeaponSetFlags"] = {
                **_authored_row(flag_assignment), "value": flag_values,
            }
        slot = _string_field(grouped.get("lockweaponslot", [None])[0])
        if slot is not None:
            if str(slot["value"]).upper() != "SECONDARY":
                raise ModuleContractError(
                    f"{target_id} WeaponModeSpecialPowerUpdate unsupported LockWeaponSlot"
                )
            slot["value"] = "SECONDARY"
            fields["LockWeaponSlot"] = slot
        if "WeaponSetFlags" not in fields and "LockWeaponSlot" not in fields and modifier is None:
            raise ModuleContractError(
                f"{target_id} WeaponModeSpecialPowerUpdate has no mode effect"
            )
        rows.append(_row("WeaponModeSpecialPowerUpdate", block, fields))
    rows.sort(key=lambda row: (str(row["sourceIni"]).casefold(), int(row["line"])))
    return rows


_DOMINATE_ENEMY_SPECIAL_POWER_FIELDS = frozenset({
    "specialpowertemplate", "unpackingvariation", "startabilityrange",
    "attributemodifieraffects", "dominateradius", "dominatedfx", "triggerfx",
    "permanentlyconvert", "triggersound", "triggermodelcondition",
    "triggermodelconditionduration", "unpacktime", "preparationtime",
    "freezeaftertriggerduration",
})


def compile_dominate_enemy_special_powers(
    lineage: Sequence[SageObject], target_id: str
) -> list[dict[str, object]]:
    """Compile authored allegiance-conversion targeting and channel timing."""

    _effective_top_blocks, tokens, _walk_blocks = _walk_helpers()
    identifier = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
    rows: list[dict[str, object]] = []
    for block in _module_blocks(lineage, "DominateEnemySpecialPower"):
        if block.blocks:
            raise ModuleContractError(f"{target_id} DominateEnemySpecialPower nested blocks")
        grouped: dict[str, list[SageAssignment]] = {}
        for assignment in block.assignments:
            grouped.setdefault(assignment.key.casefold(), []).append(assignment)
        if set(grouped) - _DOMINATE_ENEMY_SPECIAL_POWER_FIELDS or any(
            len(values) != 1 for values in grouped.values()
        ):
            raise ModuleContractError(f"{target_id} DominateEnemySpecialPower malformed fields")
        required = {"specialpowertemplate", "startabilityrange", "attributemodifieraffects"}
        if not required.issubset(grouped):
            raise ModuleContractError(f"{target_id} DominateEnemySpecialPower incomplete core fields")
        template = _string_field(grouped["specialpowertemplate"][0])
        start_range = _number_or_define_field(
            grouped["startabilityrange"][0], f"{target_id} DominateEnemySpecialPower StartAbilityRange"
        )
        assert template is not None and start_range is not None
        if identifier.fullmatch(str(template["value"])) is None:
            raise ModuleContractError(f"{target_id} DominateEnemySpecialPower malformed template")
        if "value" in start_range and float(start_range["value"]) < 0:
            raise ModuleContractError(f"{target_id} DominateEnemySpecialPower negative range")
        filter_assignment = grouped["attributemodifieraffects"][0]
        filter_values = list(tokens(filter_assignment.value))
        if not filter_values or any(re.fullmatch(r"[+-]?[A-Za-z_][A-Za-z0-9_]*", token) is None for token in filter_values):
            raise ModuleContractError(f"{target_id} DominateEnemySpecialPower malformed filter")
        fields: dict[str, object] = {
            "SpecialPowerTemplate": template,
            "StartAbilityRange": start_range,
            "AttributeModifierAffects": {**_authored_row(filter_assignment), "value": filter_values},
        }
        radius = _number_or_define_field(
            grouped.get("dominateradius", [None])[0], f"{target_id} DominateEnemySpecialPower DominateRadius"
        )
        if radius is not None:
            if "value" in radius and float(radius["value"]) < 0:
                raise ModuleContractError(f"{target_id} DominateEnemySpecialPower negative radius")
            fields["DominateRadius"] = radius
        for key in ("DominatedFX", "TriggerFX", "TriggerSound"):
            value = _string_field(grouped.get(key.casefold(), [None])[0])
            if value is not None:
                if identifier.fullmatch(str(value["value"])) is None:
                    raise ModuleContractError(f"{target_id} DominateEnemySpecialPower malformed {key}")
                fields[key] = value
        permanent = _yes_no_field(
            grouped.get("permanentlyconvert", [None])[0],
            f"{target_id} DominateEnemySpecialPower PermanentlyConvert",
        )
        if permanent is not None:
            fields["PermanentlyConvert"] = permanent
        variation = _integer_assignment_field(
            grouped.get("unpackingvariation", [None])[0],
            f"{target_id} DominateEnemySpecialPower UnpackingVariation", minimum=0,
        )
        if variation is not None:
            fields["UnpackingVariation"] = variation
        for key in ("UnpackTime", "PreparationTime", "FreezeAfterTriggerDuration", "TriggerModelConditionDuration"):
            value = _milliseconds_or_define_field(
                grouped.get(key.casefold(), [None])[0], f"{target_id} DominateEnemySpecialPower {key}"
            )
            if value is not None:
                fields[key] = value
        condition_assignment = grouped.get("triggermodelcondition", [None])[0]
        if condition_assignment is not None:
            condition = condition_assignment.value.strip()
            if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*:[A-Za-z_][A-Za-z0-9_]*", condition) is None:
                raise ModuleContractError(f"{target_id} DominateEnemySpecialPower malformed TriggerModelCondition")
            namespace, value = condition.split(":", 1)
            fields["TriggerModelCondition"] = {
                **_authored_row(condition_assignment), "namespace": namespace, "value": value,
            }
        rows.append(_row("DominateEnemySpecialPower", block, fields))
    rows.sort(key=lambda row: (str(row["sourceIni"]).casefold(), int(row["line"])))
    return rows


_GRAB_PASSENGER_SPECIAL_POWER_FIELDS = frozenset({
    "specialpowertemplate", "updatemodulestartsattack", "allowtree", "initiatefx",
})


def compile_grab_passenger_special_powers(
    lineage: Sequence[SageObject], target_id: str
) -> list[dict[str, object]]:
    """Compile the authored passenger/tree grab initiation contract."""

    identifier = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
    rows: list[dict[str, object]] = []
    for block in _module_blocks(lineage, "GrabPassengerSpecialPower"):
        if block.blocks:
            raise ModuleContractError(f"{target_id} GrabPassengerSpecialPower nested blocks")
        grouped: dict[str, list[SageAssignment]] = {}
        for assignment in block.assignments:
            grouped.setdefault(assignment.key.casefold(), []).append(assignment)
        if set(grouped) - _GRAB_PASSENGER_SPECIAL_POWER_FIELDS or any(
            len(values) != 1 for values in grouped.values()
        ):
            raise ModuleContractError(f"{target_id} GrabPassengerSpecialPower malformed fields")
        required = {"specialpowertemplate", "updatemodulestartsattack"}
        if not required.issubset(grouped):
            raise ModuleContractError(f"{target_id} GrabPassengerSpecialPower incomplete core fields")
        template = _string_field(grouped["specialpowertemplate"][0])
        starts_attack = _yes_no_field(
            grouped["updatemodulestartsattack"][0],
            f"{target_id} GrabPassengerSpecialPower UpdateModuleStartsAttack",
        )
        assert template is not None and starts_attack is not None
        if identifier.fullmatch(str(template["value"])) is None:
            raise ModuleContractError(f"{target_id} GrabPassengerSpecialPower malformed template")
        fields: dict[str, object] = {
            "SpecialPowerTemplate": template,
            "UpdateModuleStartsAttack": starts_attack,
        }
        allow_tree = _yes_no_field(
            grouped.get("allowtree", [None])[0],
            f"{target_id} GrabPassengerSpecialPower AllowTree",
        )
        if allow_tree is not None:
            fields["AllowTree"] = allow_tree
        initiate_fx = _string_field(grouped.get("initiatefx", [None])[0])
        if initiate_fx is not None:
            if identifier.fullmatch(str(initiate_fx["value"])) is None:
                raise ModuleContractError(f"{target_id} GrabPassengerSpecialPower malformed InitiateFX")
            fields["InitiateFX"] = initiate_fx
        rows.append(_row("GrabPassengerSpecialPower", block, fields))
    rows.sort(key=lambda row: (str(row["sourceIni"]).casefold(), int(row["line"])))
    return rows


_FLING_PASSENGER_SPECIAL_ABILITY_UPDATE_FIELDS = frozenset({
    "specialpowertemplate", "unpacktime", "packtime", "flingpassengervelocity",
    "flingpassengerlandingwarhead", "customanimandduration", "mustfinishability",
})


def compile_fling_passenger_special_ability_updates(
    lineage: Sequence[SageObject], target_id: str
) -> list[dict[str, object]]:
    """Compile passenger/tree release timing, trajectory, and landing payload."""

    identifier = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
    rows: list[dict[str, object]] = []
    for block in _module_blocks(lineage, "FlingPassengerSpecialAbilityUpdate"):
        if block.blocks:
            raise ModuleContractError(
                f"{target_id} FlingPassengerSpecialAbilityUpdate nested blocks"
            )
        grouped: dict[str, list[SageAssignment]] = {}
        for assignment in block.assignments:
            grouped.setdefault(assignment.key.casefold(), []).append(assignment)
        if set(grouped) - _FLING_PASSENGER_SPECIAL_ABILITY_UPDATE_FIELDS or any(
            len(values) != 1 for values in grouped.values()
        ):
            raise ModuleContractError(
                f"{target_id} FlingPassengerSpecialAbilityUpdate malformed fields"
            )
        required = {"specialpowertemplate", "unpacktime"}
        if not required.issubset(grouped):
            raise ModuleContractError(
                f"{target_id} FlingPassengerSpecialAbilityUpdate incomplete core fields"
            )
        template = _string_field(grouped["specialpowertemplate"][0])
        unpack = _milliseconds_or_define_field(
            grouped["unpacktime"][0],
            f"{target_id} FlingPassengerSpecialAbilityUpdate UnpackTime",
        )
        assert template is not None and unpack is not None
        if identifier.fullmatch(str(template["value"])) is None:
            raise ModuleContractError(
                f"{target_id} FlingPassengerSpecialAbilityUpdate malformed template"
            )
        fields: dict[str, object] = {
            "SpecialPowerTemplate": template, "UnpackTime": unpack,
        }
        pack = _milliseconds_or_define_field(
            grouped.get("packtime", [None])[0],
            f"{target_id} FlingPassengerSpecialAbilityUpdate PackTime",
        )
        if pack is not None:
            fields["PackTime"] = pack
        velocity = _coord_field(
            grouped.get("flingpassengervelocity", [None])[0],
            f"{target_id} FlingPassengerSpecialAbilityUpdate FlingPassengerVelocity",
        )
        if velocity is not None:
            fields["FlingPassengerVelocity"] = velocity
        warhead = _string_field(grouped.get("flingpassengerlandingwarhead", [None])[0])
        if warhead is not None:
            if identifier.fullmatch(str(warhead["value"])) is None:
                raise ModuleContractError(
                    f"{target_id} FlingPassengerSpecialAbilityUpdate malformed warhead"
                )
            fields["FlingPassengerLandingWarhead"] = warhead
        if (velocity is None) != (warhead is None):
            raise ModuleContractError(
                f"{target_id} FlingPassengerSpecialAbilityUpdate trajectory/warhead incomplete"
            )
        custom = grouped.get("customanimandduration", [None])[0]
        if custom is not None:
            match = re.fullmatch(
                r"\s*AnimState\s*:\s*([A-Za-z_][A-Za-z0-9_]*)\s+"
                r"AnimTime\s*:\s*(\d+)\s*", custom.value, re.IGNORECASE,
            )
            if match is None:
                raise ModuleContractError(
                    f"{target_id} FlingPassengerSpecialAbilityUpdate CustomAnimAndDuration malformed"
                )
            fields["CustomAnimAndDuration"] = {
                **_authored_row(custom), "animState": match.group(1),
                "animTimeMilliseconds": int(match.group(2)),
            }
        must_finish = _yes_no_field(
            grouped.get("mustfinishability", [None])[0],
            f"{target_id} FlingPassengerSpecialAbilityUpdate MustFinishAbility",
        )
        if must_finish is not None:
            fields["MustFinishAbility"] = must_finish
        if velocity is None and custom is None:
            raise ModuleContractError(
                f"{target_id} FlingPassengerSpecialAbilityUpdate has no release payload"
            )
        rows.append(_row("FlingPassengerSpecialAbilityUpdate", block, fields))
    rows.sort(key=lambda row: (str(row["sourceIni"]).casefold(), int(row["line"])))
    return rows


def compile_temporarily_defect_update_default(
    source: bytes, *, source_ini: str = "data/ini/default/object.ini"
) -> dict[str, object]:
    """Compile the sole inherited default TemporarilyDefectUpdate envelope."""

    lines = source.decode("cp1252").splitlines()
    starts = [
        index for index, line in enumerate(lines)
        if re.match(r"^\s*Behavior\s*=\s*TemporarilyDefectUpdate\s+", line, re.IGNORECASE)
    ]
    if len(starts) != 1:
        raise ModuleContractError(
            "default Object must author exactly one TemporarilyDefectUpdate"
        )
    start = starts[0]
    header = re.fullmatch(
        r"\s*Behavior\s*=\s*TemporarilyDefectUpdate\s+([A-Za-z_][A-Za-z0-9_]*)\s*",
        lines[start], re.IGNORECASE,
    )
    if header is None:
        raise ModuleContractError("TemporarilyDefectUpdate header malformed")
    assignments: list[tuple[str, str, int]] = []
    end_line = 0
    for index in range(start + 1, len(lines)):
        text = lines[index].split(";", 1)[0].strip()
        if not text:
            continue
        if text.casefold() == "end":
            end_line = index + 1
            break
        if "=" not in text:
            raise ModuleContractError("TemporarilyDefectUpdate nested/bare field")
        key, value = (part.strip() for part in text.split("=", 1))
        assignments.append((key, value, index + 1))
    if not end_line or len(assignments) != 1 or assignments[0][0].casefold() != "defectduration":
        raise ModuleContractError("TemporarilyDefectUpdate requires only DefectDuration")
    key, value, line = assignments[0]
    match = re.fullmatch(r"\d+", value)
    if match is None:
        if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", value) is None:
            raise ModuleContractError("TemporarilyDefectUpdate DefectDuration malformed")
        duration: dict[str, object] = {
            "authored": value, "define": value, "sourceIni": source_ini, "line": line,
        }
    else:
        duration = {
            "authored": value, "milliseconds": int(value),
            "sourceIni": source_ini, "line": line,
        }
    return {
        "module": "TemporarilyDefectUpdate", "carrier": "behavior",
        "tag": header.group(1), "fields": {"DefectDuration": duration},
        "runtimeStatus": "deferred", "extraction": "typed",
        "sourceIni": source_ini, "line": start + 1,
    }


def compile_repair_special_powers(
    lineage: Sequence[SageObject], target_id: str
) -> list[dict[str, object]]:
    """Compile the retail repair command's sole authored module binding."""

    identifier = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
    rows: list[dict[str, object]] = []
    for block in _module_blocks(lineage, "RepairSpecialPower"):
        if block.blocks or len(block.assignments) != 1:
            raise ModuleContractError(f"{target_id} RepairSpecialPower malformed fields")
        assignment = block.assignments[0]
        if assignment.key.casefold() != "specialpowertemplate":
            raise ModuleContractError(f"{target_id} RepairSpecialPower unsupported field")
        template = _string_field(assignment)
        assert template is not None
        if identifier.fullmatch(str(template["value"])) is None:
            raise ModuleContractError(f"{target_id} RepairSpecialPower malformed template")
        rows.append(_row(
            "RepairSpecialPower", block, {"SpecialPowerTemplate": template}
        ))
    rows.sort(key=lambda row: (str(row["sourceIni"]).casefold(), int(row["line"])))
    return rows


def compile_horde_dispatch_special_powers(
    lineage: Sequence[SageObject], target_id: str
) -> list[dict[str, object]]:
    """Compile horde-to-member special-power dispatch state."""

    identifier = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
    rows: list[dict[str, object]] = []
    for block in _module_blocks(lineage, "HordeDispatchSpecialPower"):
        if block.blocks or len(_assignment_map(block)) != len(block.assignments):
            raise ModuleContractError(f"{target_id} HordeDispatchSpecialPower malformed fields")
        amap = _assignment_map(block)
        if set(amap) - {"specialpowertemplate", "updatemodulestartsattack", "startspaused"}:
            raise ModuleContractError(f"{target_id} HordeDispatchSpecialPower unsupported fields")
        template = _string_field(amap.get("specialpowertemplate"))
        paused = _yes_no_field(
            amap.get("startspaused"), f"{target_id} HordeDispatchSpecialPower StartsPaused"
        )
        if template is None or paused is None or identifier.fullmatch(str(template["value"])) is None:
            raise ModuleContractError(f"{target_id} HordeDispatchSpecialPower incomplete core fields")
        fields: dict[str, object] = {
            "SpecialPowerTemplate": template, "StartsPaused": paused,
        }
        starts_attack = _yes_no_field(
            amap.get("updatemodulestartsattack"),
            f"{target_id} HordeDispatchSpecialPower UpdateModuleStartsAttack",
        )
        if starts_attack is not None:
            fields["UpdateModuleStartsAttack"] = starts_attack
        rows.append(_row("HordeDispatchSpecialPower", block, fields))
    rows.sort(key=lambda row: (str(row["sourceIni"]).casefold(), int(row["line"])))
    return rows


def _required_identifier_field(
    assignment: SageAssignment | None, label: str
) -> dict[str, object]:
    field = _string_field(assignment)
    if field is None or re.fullmatch(
        r"[A-Za-z_][A-Za-z0-9_]*", str(field["value"])
    ) is None:
        raise ModuleContractError(f"{label} must be an identifier")
    return field


def compile_stop_special_powers(
    lineage: Sequence[SageObject], target_id: str
) -> list[dict[str, object]]:
    """Compile exact StopSpecialPower template-to-template cancellation routes."""

    rows: list[dict[str, object]] = []
    for block in _module_blocks(lineage, "StopSpecialPower"):
        if block.blocks or len(_assignment_map(block)) != len(block.assignments):
            raise ModuleContractError(f"{target_id} StopSpecialPower malformed fields")
        amap = _assignment_map(block)
        if set(amap) != {"specialpowertemplate", "stoppowertemplate"}:
            raise ModuleContractError(f"{target_id} StopSpecialPower incomplete or unsupported fields")
        fields = {
            "SpecialPowerTemplate": _required_identifier_field(
                amap.get("specialpowertemplate"), f"{target_id} StopSpecialPower SpecialPowerTemplate"
            ),
            "StopPowerTemplate": _required_identifier_field(
                amap.get("stoppowertemplate"), f"{target_id} StopSpecialPower StopPowerTemplate"
            ),
        }
        stop_template = str(fields["StopPowerTemplate"]["value"])
        effective_top_blocks, _tokens, _walk_blocks = _walk_helpers()
        linked = [
            candidate for candidate in effective_top_blocks(lineage)
            if (candidate.header_key or "").casefold() == "behavior"
            and candidate.kind.casefold() != "stopspecialpower"
            and any(
                value.strip().casefold() == stop_template.casefold()
                for value in candidate.values("SpecialPowerTemplate")
            )
        ]
        if len(linked) != 1:
            raise ModuleContractError(
                f"{target_id} StopSpecialPower target template is not uniquely bound"
            )
        target = linked[0]
        row = _row("StopSpecialPower", block, fields)
        row["effectGraph"] = {
            "kind": "stop-special-power",
            "specialPowerTemplateId": str(fields["SpecialPowerTemplate"]["value"]),
            "stopPowerTemplateId": stop_template,
            "targetMode": "SELF",
            "interruptsCurrentOrder": True,
            "linkedModule": {
                "kind": target.kind,
                "tag": target.instance_tag or "",
                "sourceIni": target.source_virtual_path,
                "line": target.line,
            },
            "sourceIni": block.source_virtual_path,
            "line": block.line,
        }
        rows.append(row)
    rows.sort(key=lambda row: (str(row["sourceIni"]).casefold(), int(row["line"])))
    return rows


def compile_siege_deploy_special_powers(
    lineage: Sequence[SageObject], target_id: str
) -> list[dict[str, object]]:
    """Compile the complete retail SiegeDeploySpecialPower field grammar."""

    allowed = {
        "specialpowertemplate", "lowerdelay", "raisedelay",
        "evacuatepassengersondeploy", "skipadjustposition",
        "initiatesound", "extrawalldistance",
    }
    rows: list[dict[str, object]] = []
    for block in _module_blocks(lineage, "SiegeDeploySpecialPower"):
        if block.blocks or len(_assignment_map(block)) != len(block.assignments):
            raise ModuleContractError(f"{target_id} SiegeDeploySpecialPower malformed fields")
        amap = _assignment_map(block)
        if set(amap) - allowed:
            raise ModuleContractError(f"{target_id} SiegeDeploySpecialPower unsupported fields")
        if not {
            "specialpowertemplate", "lowerdelay", "raisedelay",
            "evacuatepassengersondeploy", "skipadjustposition", "initiatesound",
        }.issubset(amap):
            raise ModuleContractError(f"{target_id} SiegeDeploySpecialPower incomplete core fields")
        fields: dict[str, object] = {
            "SpecialPowerTemplate": _required_identifier_field(
                amap.get("specialpowertemplate"),
                f"{target_id} SiegeDeploySpecialPower SpecialPowerTemplate",
            ),
            "LowerDelay": _milliseconds_field(
                amap.get("lowerdelay"), f"{target_id} SiegeDeploySpecialPower LowerDelay"
            ),
            "RaiseDelay": _milliseconds_field(
                amap.get("raisedelay"), f"{target_id} SiegeDeploySpecialPower RaiseDelay"
            ),
            "EvacuatePassengersOnDeploy": _yes_no_field(
                amap.get("evacuatepassengersondeploy"),
                f"{target_id} SiegeDeploySpecialPower EvacuatePassengersOnDeploy",
            ),
            "SkipAdjustPosition": _yes_no_field(
                amap.get("skipadjustposition"),
                f"{target_id} SiegeDeploySpecialPower SkipAdjustPosition",
            ),
            "InitiateSound": _required_identifier_field(
                amap.get("initiatesound"),
                f"{target_id} SiegeDeploySpecialPower InitiateSound",
            ),
        }
        extra = _number_field(
            amap.get("extrawalldistance"),
            f"{target_id} SiegeDeploySpecialPower ExtraWallDistance",
        )
        if extra is not None:
            if float(extra["value"]) < 0.0:
                raise ModuleContractError(
                    f"{target_id} SiegeDeploySpecialPower ExtraWallDistance must be non-negative"
                )
            fields["ExtraWallDistance"] = extra
        template = str(fields["SpecialPowerTemplate"]["value"])
        graph: dict[str, object] = {
            "kind": "siege-deploy",
            "specialPowerTemplateId": template,
            "targetMode": "TARGET_STRUCTURE",
            "lowerDelayMs": int(fields["LowerDelay"]["milliseconds"]),
            "raiseDelayMs": int(fields["RaiseDelay"]["milliseconds"]),
            "evacuatePassengersOnDeploy": bool(fields["EvacuatePassengersOnDeploy"]["value"]),
            "skipAdjustPosition": bool(fields["SkipAdjustPosition"]["value"]),
            "initiateSoundId": str(fields["InitiateSound"]["value"]),
            "modelReceipts": [],
            "sourceIni": block.source_virtual_path,
            "line": block.line,
        }
        if extra is not None:
            graph["extraWallDistanceSource"] = float(extra["value"])
            graph["modelReceipts"] = [
                "wall-contact-offset:ExtraWallDistance requires retail docking geometry"
            ]
        row = _row("SiegeDeploySpecialPower", block, fields)
        row["effectGraph"] = graph
        rows.append(row)
    rows.sort(key=lambda row: (str(row["sourceIni"]).casefold(), int(row["line"])))
    return rows


_NONSHIPPING_SPECIAL_POWER_OWNERS: dict[str, tuple[str, str, str]] = {
    "DeflectSpecialPower": (
        "MordorHaradrimObsolete",
        "data/ini/object/obsolete/evilmenharadrim.ini",
        "obsolete-non-shipping",
    ),
    "SplitHordeSpecialPower": (
        "LAElvenWarriorDoubleHorde",
        "data/ini/object/cinematic/lastallianceunits.ini",
        "cinematic-non-shipping",
    ),
}


def _compile_nonshipping_special_powers(
    lineage: Sequence[SageObject], target_id: str, module: str
) -> list[dict[str, object]]:
    """Preserve a fieldless subclass whose only leaf is the inherited template.

    The only effective retail owners are an obsolete object and a cinematic
    combo horde.  Their gameplay systems are deliberately not invented here;
    a mod may preserve the same grammar, but receives an unadmitted deferred
    receipt rather than becoming executable by naming the class.
    """

    expected_owner, expected_source, admitted_disposition = (
        _NONSHIPPING_SPECIAL_POWER_OWNERS[module]
    )
    rows: list[dict[str, object]] = []
    for block in _module_blocks(lineage, module):
        if block.blocks or len(_assignment_map(block)) != len(block.assignments):
            raise ModuleContractError(f"{target_id} {module} malformed fields")
        amap = _assignment_map(block)
        if set(amap) != {"specialpowertemplate"}:
            raise ModuleContractError(
                f"{target_id} {module} must author only SpecialPowerTemplate"
            )
        template = _required_identifier_field(
            amap.get("specialpowertemplate"),
            f"{target_id} {module} SpecialPowerTemplate",
        )
        source = block.source_virtual_path.replace("\\", "/")
        retail_owner_match = (
            target_id.casefold() == expected_owner.casefold()
            and source.casefold() == expected_source.casefold()
        )
        row = _row(module, block, {"SpecialPowerTemplate": template})
        row["effectGraph"] = {
            "kind": "non-shipping-special-power",
            "authoredModuleKind": module,
            "specialPowerTemplateId": str(template["value"]),
            # The subclass parse table has no authored leaves beyond the
            # inherited SpecialPowerTemplate above.
            "subclassFields": [],
            "executionEligibility": {
                "runtimeStatus": "deferred",
                "shippingAdmission": False,
                "retailOwnerMatch": retail_owner_match,
                "disposition": (
                    admitted_disposition
                    if retail_owner_match
                    else "unadmitted-owner"
                ),
            },
            "sourceIni": block.source_virtual_path,
            "line": block.line,
        }
        rows.append(row)
    rows.sort(key=lambda row: (str(row["sourceIni"]).casefold(), int(row["line"])))
    return rows


def compile_deflect_special_powers(
    lineage: Sequence[SageObject], target_id: str
) -> list[dict[str, object]]:
    return _compile_nonshipping_special_powers(
        lineage, target_id, "DeflectSpecialPower"
    )


def compile_split_horde_special_powers(
    lineage: Sequence[SageObject], target_id: str
) -> list[dict[str, object]]:
    return _compile_nonshipping_special_powers(
        lineage, target_id, "SplitHordeSpecialPower"
    )


def compile_deploy_style_ai_updates(
    lineage: Sequence[SageObject], target_id: str
) -> list[dict[str, object]]:
    """Compile the one effective BFME2/RotWK DeployStyleAIUpdate grammar.

    The retail binaries expose additional optional turret-control leaves, but
    neither retail tree authors them.  They are deliberately not defaulted
    into the Demolisher contract.
    """

    expected = {
        "autoacquireenemieswhenidle",
        "moodattackcheckrate",
        "mustdeploytoattack",
        "unpacktime",
        "packtime",
        "deployedattributemodifier",
    }
    rows: list[dict[str, object]] = []
    for block in _module_blocks(lineage, "DeployStyleAIUpdate"):
        if block.blocks or len(_assignment_map(block)) != len(block.assignments):
            raise ModuleContractError(
                f"{target_id} DeployStyleAIUpdate malformed fields"
            )
        amap = _assignment_map(block)
        if set(amap) != expected:
            raise ModuleContractError(
                f"{target_id} DeployStyleAIUpdate incomplete or unsupported fields"
            )
        auto_acquire = _token_list_field(amap.get("autoacquireenemieswhenidle"))
        assert auto_acquire is not None
        auto_tokens = list(auto_acquire["value"])
        if not auto_tokens or auto_tokens[0].casefold() not in {"yes", "no"}:
            raise ModuleContractError(
                f"{target_id} DeployStyleAIUpdate AutoAcquireEnemiesWhenIdle "
                "must begin with Yes or No"
            )
        mood = _milliseconds_field(
            amap.get("moodattackcheckrate"),
            f"{target_id} DeployStyleAIUpdate MoodAttackCheckRate",
        )
        unpack = _milliseconds_field(
            amap.get("unpacktime"), f"{target_id} DeployStyleAIUpdate UnpackTime"
        )
        pack = _milliseconds_field(
            amap.get("packtime"), f"{target_id} DeployStyleAIUpdate PackTime"
        )
        assert mood is not None and unpack is not None and pack is not None
        fields: dict[str, object] = {
            "AutoAcquireEnemiesWhenIdle": auto_acquire,
            "MoodAttackCheckRate": mood,
            "MustDeployToAttack": _yes_no_field(
                amap.get("mustdeploytoattack"),
                f"{target_id} DeployStyleAIUpdate MustDeployToAttack",
            ),
            "UnpackTime": unpack,
            "PackTime": pack,
            "DeployedAttributeModifier": _required_identifier_field(
                amap.get("deployedattributemodifier"),
                f"{target_id} DeployStyleAIUpdate DeployedAttributeModifier",
            ),
        }
        row = _row("DeployStyleAIUpdate", block, fields)
        row["effectGraph"] = {
            "kind": "deploy-style",
            "autoAcquireEnabled": auto_tokens[0].casefold() == "yes",
            "autoAcquireModes": auto_tokens[1:],
            "moodAttackCheckRateMs": int(mood["milliseconds"]),
            "mustDeployToAttack": bool(fields["MustDeployToAttack"]["value"]),
            "unpackTimeMs": int(unpack["milliseconds"]),
            "packTimeMs": int(pack["milliseconds"]),
            "deployedAttributeModifierId": str(
                fields["DeployedAttributeModifier"]["value"]
            ),
            "sourceIni": block.source_virtual_path,
            "line": block.line,
        }
        rows.append(row)
    rows.sort(key=lambda row: (str(row["sourceIni"]).casefold(), int(row["line"])))
    return rows


def compile_toggle_deploy_special_ability_updates(
    lineage: Sequence[SageObject], target_id: str
) -> list[dict[str, object]]:
    """Bind ToggleDeploySpecialAbilityUpdate to its DeployStyle owner state."""

    toggle_blocks = _module_blocks(lineage, "ToggleDeploySpecialAbilityUpdate")
    if not toggle_blocks:
        return []
    style_rows = compile_deploy_style_ai_updates(lineage, target_id)
    if len(style_rows) != 1:
        raise ModuleContractError(
            f"{target_id} ToggleDeploySpecialAbilityUpdate needs exactly one "
            "DeployStyleAIUpdate"
        )
    style_graph = style_rows[0].get("effectGraph")
    if not isinstance(style_graph, Mapping):
        raise ModuleContractError(f"{target_id} DeployStyleAIUpdate graph is missing")
    expected = {
        "specialpowertemplate",
        "ignorefacingcheck",
        "sounddeploy",
        "soundundeploy",
    }
    rows: list[dict[str, object]] = []
    for block in toggle_blocks:
        if block.blocks or len(_assignment_map(block)) != len(block.assignments):
            raise ModuleContractError(
                f"{target_id} ToggleDeploySpecialAbilityUpdate malformed fields"
            )
        amap = _assignment_map(block)
        if set(amap) != expected:
            raise ModuleContractError(
                f"{target_id} ToggleDeploySpecialAbilityUpdate incomplete or "
                "unsupported fields"
            )
        fields: dict[str, object] = {
            "SpecialPowerTemplate": _required_identifier_field(
                amap.get("specialpowertemplate"),
                f"{target_id} ToggleDeploySpecialAbilityUpdate SpecialPowerTemplate",
            ),
            "IgnoreFacingCheck": _yes_no_field(
                amap.get("ignorefacingcheck"),
                f"{target_id} ToggleDeploySpecialAbilityUpdate IgnoreFacingCheck",
            ),
            "SoundDeploy": _required_identifier_field(
                amap.get("sounddeploy"),
                f"{target_id} ToggleDeploySpecialAbilityUpdate SoundDeploy",
            ),
            "SoundUndeploy": _required_identifier_field(
                amap.get("soundundeploy"),
                f"{target_id} ToggleDeploySpecialAbilityUpdate SoundUndeploy",
            ),
        }
        graph = dict(style_graph)
        graph.update(
            {
                "kind": "toggle-deploy",
                "specialPowerTemplateId": str(
                    fields["SpecialPowerTemplate"]["value"]
                ),
                "targetMode": "SELF",
                "ignoreFacingCheck": bool(fields["IgnoreFacingCheck"]["value"]),
                "soundDeployId": str(fields["SoundDeploy"]["value"]),
                "soundUndeployId": str(fields["SoundUndeploy"]["value"]),
                "deployStyle": {
                    "tag": style_rows[0]["tag"],
                    "sourceIni": style_rows[0]["sourceIni"],
                    "line": style_rows[0]["line"],
                },
                "sourceIni": block.source_virtual_path,
                "line": block.line,
            }
        )
        row = _row("ToggleDeploySpecialAbilityUpdate", block, fields)
        row["effectGraph"] = graph
        rows.append(row)
    rows.sort(key=lambda row: (str(row["sourceIni"]).casefold(), int(row["line"])))
    return rows


def compile_special_disguise_updates(
    lineage: Sequence[SageObject], target_id: str
) -> list[dict[str, object]]:
    """Compile the binary-closed SpecialDisguiseUpdate subset.

    The executable body is presentation-only object swapping: the authoritative
    Object identity never changes. RotWK's generic SpecialAbilityUpdate base can
    parse TriggerAttributeModifier/AttributeModifierDuration, but the exact 2.01
    disguise callgraph has no proven application read, so those authored rows
    remain typed and explicitly deferred.
    """

    required = {
        "specialpowertemplate", "unpacktime", "preparationtime",
        "persistentpreptime", "packtime", "opacitytarget",
        "disguiseastemplate", "disguisedastemplate_enemyperspective",
        "disguisefx", "forcemountedwhendisguising",
    }
    optional = {
        "awardxpfortriggering", "triggerinstantlyoncreate",
        "triggerattributemodifier", "attributemodifierduration",
    }
    rows: list[dict[str, object]] = []
    for block in _module_blocks(lineage, "SpecialDisguiseUpdate"):
        if block.blocks or len(_assignment_map(block)) != len(block.assignments):
            raise ModuleContractError(
                f"{target_id} SpecialDisguiseUpdate malformed fields"
            )
        amap = _assignment_map(block)
        if not required <= set(amap) or not set(amap) <= required | optional:
            raise ModuleContractError(
                f"{target_id} SpecialDisguiseUpdate incomplete or unsupported fields"
            )
        modifier = amap.get("triggerattributemodifier")
        modifier_duration = amap.get("attributemodifierduration")
        if (modifier is None) != (modifier_duration is None):
            raise ModuleContractError(
                f"{target_id} SpecialDisguiseUpdate modifier fields are unpaired"
            )
        unpack = _milliseconds_field(amap.get("unpacktime"), f"{target_id} SpecialDisguiseUpdate UnpackTime")
        preparation = _milliseconds_field(amap.get("preparationtime"), f"{target_id} SpecialDisguiseUpdate PreparationTime")
        persistent = _milliseconds_field(amap.get("persistentpreptime"), f"{target_id} SpecialDisguiseUpdate PersistentPrepTime")
        pack = _milliseconds_field(amap.get("packtime"), f"{target_id} SpecialDisguiseUpdate PackTime")
        opacity = _number_field(amap.get("opacitytarget"), f"{target_id} SpecialDisguiseUpdate OpacityTarget")
        assert unpack is not None and preparation is not None and persistent is not None and pack is not None and opacity is not None
        if any(int(value["milliseconds"]) <= 0 for value in (unpack, preparation, persistent, pack)):
            raise ModuleContractError(
                f"{target_id} SpecialDisguiseUpdate transition time must be positive"
            )
        if not 0.0 <= float(opacity["value"]) <= 1.0:
            raise ModuleContractError(
                f"{target_id} SpecialDisguiseUpdate OpacityTarget is out of range"
            )
        force_mounted = _yes_no_field(
            amap.get("forcemountedwhendisguising"),
            f"{target_id} SpecialDisguiseUpdate ForceMountedWhenDisguising",
        )
        fields: dict[str, object] = {
            "SpecialPowerTemplate": _required_identifier_field(amap.get("specialpowertemplate"), f"{target_id} SpecialDisguiseUpdate SpecialPowerTemplate"),
            "UnpackTime": unpack,
            "PreparationTime": preparation,
            "PersistentPrepTime": persistent,
            "PackTime": pack,
            "OpacityTarget": opacity,
            "DisguiseAsTemplate": _required_identifier_field(amap.get("disguiseastemplate"), f"{target_id} SpecialDisguiseUpdate DisguiseAsTemplate"),
            "DisguisedAsTemplate_EnemyPerspective": _required_identifier_field(amap.get("disguisedastemplate_enemyperspective"), f"{target_id} SpecialDisguiseUpdate DisguisedAsTemplate_EnemyPerspective"),
            "DisguiseFX": _required_identifier_field(amap.get("disguisefx"), f"{target_id} SpecialDisguiseUpdate DisguiseFX"),
            "ForceMountedWhenDisguising": force_mounted,
        }
        if amap.get("awardxpfortriggering") is not None:
            fields["AwardXPForTriggering"] = _number_field(
                amap.get("awardxpfortriggering"),
                f"{target_id} SpecialDisguiseUpdate AwardXPForTriggering",
            )
        if amap.get("triggerinstantlyoncreate") is not None:
            fields["TriggerInstantlyOnCreate"] = _yes_no_field(
                amap.get("triggerinstantlyoncreate"),
                f"{target_id} SpecialDisguiseUpdate TriggerInstantlyOnCreate",
            )
        if modifier is not None:
            fields["TriggerAttributeModifier"] = _required_identifier_field(
                modifier, f"{target_id} SpecialDisguiseUpdate TriggerAttributeModifier"
            )
            duration = _milliseconds_field(
                modifier_duration,
                f"{target_id} SpecialDisguiseUpdate AttributeModifierDuration",
            )
            assert duration is not None
            fields["AttributeModifierDuration"] = duration
        closed = (
            bool(force_mounted["value"])
            and "AwardXPForTriggering" not in fields
            and "TriggerInstantlyOnCreate" not in fields
            and "TriggerAttributeModifier" not in fields
        )
        row = _row(
            "SpecialDisguiseUpdate", block, fields,
            runtime_status="executable" if closed else "deferred",
        )
        graph: dict[str, object] = {
            "kind": "special-disguise",
            "specialPowerTemplateId": str(fields["SpecialPowerTemplate"]["value"]),
            "targetMode": "SELF",
            "unpackTimeMs": int(unpack["milliseconds"]),
            "preparationTimeMs": int(preparation["milliseconds"]),
            "persistentPrepTimeMs": int(persistent["milliseconds"]),
            "packTimeMs": int(pack["milliseconds"]),
            "opacityTarget": float(opacity["value"]),
            "ownerObjectId": target_id,
            "ownerDisguiseTemplateId": str(fields["DisguiseAsTemplate"]["value"]),
            "hostileDisguiseTemplateId": str(fields["DisguisedAsTemplate_EnemyPerspective"]["value"]),
            "disguiseFxId": str(fields["DisguiseFX"]["value"]),
            "forceMountedWhenDisguising": bool(force_mounted["value"]),
            "deferredBoundaries": [
                "critical-hit-ordering", "death-reset-ordering",
                "user1-stealth-ordering", "viewer-perspective",
            ],
            "sourceIni": block.source_virtual_path,
            "line": block.line,
        }
        if modifier is not None:
            graph["triggerAttributeModifierId"] = str(fields["TriggerAttributeModifier"]["value"])
            graph["attributeModifierDurationMs"] = int(fields["AttributeModifierDuration"]["milliseconds"])
            graph["executionEligibility"] = {
                "runtimeStatus": "deferred",
                "reason": "binary-unresolved:TriggerAttributeModifier-application",
            }
        row["effectGraph"] = graph
        rows.append(row)
    rows.sort(key=lambda row: (str(row["sourceIni"]).casefold(), int(row["line"])))
    return rows


def compile_scavenger_special_powers(
    lineage: Sequence[SageObject], target_id: str
) -> list[dict[str, object]]:
    """Compile the complete retail Scavenger kill-bounty multiplier contract."""

    rows: list[dict[str, object]] = []
    expected = {
        "specialpowertemplate",
        "bountypercent",
        "availableatstart",
        "requirementsfiltermpskirmish",
        "requirementsfilterstrategic",
    }
    for block in _module_blocks(lineage, "ScavengerSpecialPower"):
        if block.blocks or len(_assignment_map(block)) != len(block.assignments):
            raise ModuleContractError(
                f"{target_id} ScavengerSpecialPower malformed fields"
            )
        amap = _assignment_map(block)
        if set(amap) != expected:
            raise ModuleContractError(
                f"{target_id} ScavengerSpecialPower incomplete or unsupported fields"
            )
        bounty = _number_field(
            amap.get("bountypercent"),
            f"{target_id} ScavengerSpecialPower BountyPercent",
        )
        assert bounty is not None
        if float(bounty["value"]) < 0.0:
            raise ModuleContractError(
                f"{target_id} ScavengerSpecialPower BountyPercent is negative"
            )
        fields = {
            "SpecialPowerTemplate": _required_identifier_field(
                amap.get("specialpowertemplate"),
                f"{target_id} ScavengerSpecialPower SpecialPowerTemplate",
            ),
            "BountyPercent": bounty,
            "AvailableAtStart": _yes_no_field(
                amap.get("availableatstart"),
                f"{target_id} ScavengerSpecialPower AvailableAtStart",
            ),
            "RequirementsFilterMPSkirmish": _required_identifier_field(
                amap.get("requirementsfiltermpskirmish"),
                f"{target_id} ScavengerSpecialPower RequirementsFilterMPSkirmish",
            ),
            "RequirementsFilterStrategic": _required_identifier_field(
                amap.get("requirementsfilterstrategic"),
                f"{target_id} ScavengerSpecialPower RequirementsFilterStrategic",
            ),
        }
        rows.append(_row("ScavengerSpecialPower", block, fields))
    rows.sort(key=lambda row: (str(row["sourceIni"]).casefold(), int(row["line"])))
    return rows


def compile_unleash_special_powers(
    lineage: Sequence[SageObject], target_id: str
) -> list[dict[str, object]]:
    """Compile the BFME2 warg-sentry unleash timing and instant route."""

    rows: list[dict[str, object]] = []
    for block in _module_blocks(lineage, "UnleashSpecialPower"):
        if block.blocks or len(_assignment_map(block)) != len(block.assignments):
            raise ModuleContractError(f"{target_id} UnleashSpecialPower malformed fields")
        amap = _assignment_map(block)
        allowed = {"specialpowertemplate", "unpacktime", "awardxpfortriggering", "instant"}
        if set(amap) != allowed:
            raise ModuleContractError(f"{target_id} UnleashSpecialPower incomplete or unsupported fields")
        unpack = _milliseconds_field(amap.get("unpacktime"), f"{target_id} UnleashSpecialPower UnpackTime")
        award = _number_field(amap.get("awardxpfortriggering"), f"{target_id} UnleashSpecialPower AwardXPForTriggering")
        instant = _yes_no_field(amap.get("instant"), f"{target_id} UnleashSpecialPower Instant")
        if unpack is None or award is None or float(award["value"]) < 0 or instant is None:
            raise ModuleContractError(f"{target_id} UnleashSpecialPower invalid core fields")
        fields = {
            "SpecialPowerTemplate": _required_identifier_field(
                amap.get("specialpowertemplate"), f"{target_id} UnleashSpecialPower SpecialPowerTemplate"
            ),
            "UnpackTime": unpack,
            "AwardXPForTriggering": award,
            "Instant": instant,
        }
        effective_top_blocks, tokens, _walk_blocks = _walk_helpers()
        all_behaviors = [
            candidate for candidate in effective_top_blocks(lineage)
            if (candidate.header_key or "").casefold() == "behavior"
        ]
        creators = [
            candidate for candidate in all_behaviors
            if candidate.kind.casefold() == "objectcreationupgrade"
            and candidate.values("ThingToSpawn")
        ]
        watchers = [
            candidate for candidate in all_behaviors
            if candidate.kind.casefold() == "slavewatcherbehavior"
        ]
        if len(creators) != 1 or len(watchers) != 1:
            raise ModuleContractError(
                f"{target_id} UnleashSpecialPower release binding is not unique"
            )
        creator, watcher = creators[0], watchers[0]
        spawned = [
            token for value in creator.values("ThingToSpawn") for token in tokens(value)
        ]
        triggered = [
            token for value in creator.values("TriggeredBy") for token in tokens(value)
        ]
        removed = [
            token for value in watcher.values("RemoveUpgrade") for token in tokens(value)
        ]
        granted = [
            token for value in watcher.values("GrantUpgrade") for token in tokens(value)
        ]
        if len(spawned) != 1 or not triggered or len(removed) != 1 or len(granted) != 1:
            raise ModuleContractError(
                f"{target_id} UnleashSpecialPower release binding is incomplete"
            )
        row = _row("UnleashSpecialPower", block, fields)
        row["effectGraph"] = {
            "kind": "unleash-special-power",
            "specialPowerTemplateId": str(fields["SpecialPowerTemplate"]["value"]),
            "timingMs": {"UnpackTime": int(unpack["milliseconds"])},
            "awardXpForTriggering": award["value"],
            "instant": bool(instant["value"]),
            "targetMode": "SELF_OWNED_SLAVE",
            "spawnedObjectId": spawned[0],
            "creationGateUpgradeIds": triggered,
            "slaveWatcher": {
                "removeUpgradeId": removed[0],
                "grantUpgradeId": granted[0],
                "sourceIni": watcher.source_virtual_path,
                "line": watcher.line,
            },
            "sourceIni": block.source_virtual_path,
            "line": block.line,
        }
        rows.append(row)
    rows.sort(key=lambda row: (str(row["sourceIni"]).casefold(), int(row["line"])))
    return rows


def compile_special_enemy_sense_updates(
    lineage: Sequence[SageObject],
    target_id: str,
    *,
    numeric_defines: Mapping[str, int | float] | None = None,
    numeric_define_provenance: Mapping[str, Mapping[str, object]] | None = None,
) -> list[dict[str, object]]:
    """Compile periodic hostile-filter sensing without inventing detection state."""

    rows: list[dict[str, object]] = []
    for block in _module_blocks(lineage, "SpecialEnemySenseUpdate"):
        if block.blocks or len(_assignment_map(block)) != len(block.assignments):
            raise ModuleContractError(f"{target_id} SpecialEnemySenseUpdate malformed fields")
        amap = _assignment_map(block)
        if set(amap) != {"specialenemyfilter", "scanrange", "scaninterval"}:
            raise ModuleContractError(f"{target_id} SpecialEnemySenseUpdate incomplete or unsupported fields")
        object_filter = _token_list_field(amap.get("specialenemyfilter"))
        scan_assignment = amap.get("scanrange")
        scan_range = None
        if scan_assignment is not None:
            expression = scan_assignment.value.strip()
            if re.fullmatch(r"-?(?:\d+(?:\.\d*)?|\.\d+)", expression):
                scan_range = _number_field(
                    scan_assignment,
                    f"{target_id} SpecialEnemySenseUpdate ScanRange",
                )
            elif re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", expression):
                key = expression.casefold()
                value = None if numeric_defines is None else numeric_defines.get(key)
                provenance = (
                    None
                    if numeric_define_provenance is None
                    else numeric_define_provenance.get(key)
                )
                if value is None or provenance is None:
                    raise ModuleContractError(
                        f"{target_id} SpecialEnemySenseUpdate ScanRange define provenance is unresolved: {expression}"
                    )
                numeric = float(value)
                provenance_value = provenance.get("value")
                if (
                    numeric <= 0
                    or str(provenance.get("defineId", "")).casefold() != key
                    or not isinstance(provenance.get("sourceIni"), str)
                    or not provenance.get("sourceIni")
                    or isinstance(provenance.get("line"), bool)
                    or not isinstance(provenance.get("line"), int)
                    or int(provenance["line"]) <= 0
                    or not isinstance(provenance.get("authoredValue"), str)
                    or not provenance.get("authoredValue")
                    or isinstance(provenance_value, bool)
                    or not isinstance(provenance_value, (int, float))
                    or float(provenance_value) != numeric
                ):
                    raise ModuleContractError(
                        f"{target_id} SpecialEnemySenseUpdate ScanRange define provenance is invalid: {expression}"
                    )
                resolved: int | float = int(numeric) if numeric.is_integer() else numeric
                scan_range = {
                    **_authored_row(scan_assignment),
                    "expression": expression,
                    "value": resolved,
                    "defineProvenance": dict(provenance),
                }
            else:
                raise ModuleContractError(
                    f"{target_id} SpecialEnemySenseUpdate ScanRange expression is unsupported"
                )
        interval = _milliseconds_field(amap.get("scaninterval"), f"{target_id} SpecialEnemySenseUpdate ScanInterval")
        if (
            object_filter is None or not object_filter["value"]
            or scan_range is None or float(scan_range["value"]) <= 0
            or interval is None or int(interval["milliseconds"]) <= 0
        ):
            raise ModuleContractError(f"{target_id} SpecialEnemySenseUpdate invalid core fields")
        fields = {
            "SpecialEnemyFilter": object_filter,
            "ScanRange": scan_range,
            "ScanInterval": interval,
        }
        row = _row("SpecialEnemySenseUpdate", block, fields)
        row["effectGraph"] = {
            "kind": "special-enemy-sense",
            "specialEnemyFilter": list(object_filter["value"]),
            "scanRange": scan_range["value"],
            "scanIntervalMs": interval["milliseconds"],
            "targetMode": "PERIODIC_ENEMY_RADIUS_SCAN",
            "sourceIni": block.source_virtual_path,
            "line": block.line,
        }
        rows.append(row)
    rows.sort(key=lambda row: (str(row["sourceIni"]).casefold(), int(row["line"])))
    return rows


def _flammable_expression(
    assignment: SageAssignment | None, target_id: str, key: str, *,
    timer: bool = False, allow_multiply: bool = False,
) -> dict[str, object] | None:
    if assignment is None:
        return None
    expression = assignment.value.strip()
    result: dict[str, object] = {**_authored_row(assignment), "expression": expression}
    if re.fullmatch(r"\d+(?:\.\d*)?|\.\d+", expression):
        numeric = float(expression)
        value: int | float = int(numeric) if numeric.is_integer() else numeric
        result["milliseconds" if timer else "value"] = value
        return result
    if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", expression):
        result["define"] = expression
        return result
    match = re.fullmatch(
        r"#MULTIPLY\(\s*([A-Za-z_][A-Za-z0-9_]*)\s+"
        r"([A-Za-z_][A-Za-z0-9_]*)\s*\)", expression, re.IGNORECASE,
    )
    if allow_multiply and match is not None:
        result.update({"operation": "multiply", "operands": [match.group(1), match.group(2)]})
        return result
    raise ModuleContractError(f"{target_id} FlammableUpdate {key} malformed")


def _flammable_fire_fx(
    assignment: SageAssignment, target_id: str
) -> dict[str, object]:
    match = re.fullmatch(
        r"\s*FX\s*:\s*([A-Za-z_][A-Za-z0-9_]*)"
        r"(?:\s+BONE\s*:\s*([A-Za-z_][A-Za-z0-9_]*))?\s*",
        assignment.value, re.IGNORECASE,
    )
    if match is None:
        raise ModuleContractError(f"{target_id} FlammableUpdate FireFXList malformed")
    return {**_authored_row(assignment), "fx": match.group(1), "bone": match.group(2)}


def compile_flammable_updates(
    lineage: Sequence[SageObject], target_id: str
) -> list[dict[str, object]]:
    """Compile ignition damage, timers, thresholds, status, FX, and audio."""

    rows: list[dict[str, object]] = []
    for block in _module_blocks(lineage, "FlammableUpdate"):
        if block.blocks:
            raise ModuleContractError(f"{target_id} FlammableUpdate nested blocks")
        grouped: dict[str, list[SageAssignment]] = {}
        for assignment in block.assignments:
            grouped.setdefault(assignment.key.casefold(), []).append(assignment)
        if set(grouped) - _FLAMMABLE_FIELDS or any(
            len(values) > 1 and key != "firefxlist"
            for key, values in grouped.items()
        ):
            raise ModuleContractError(f"{target_id} FlammableUpdate malformed fields")
        amap = {key: values[-1] for key, values in grouped.items()}
        fields: dict[str, object] = {}
        for key in ("AflameDuration", "AflameDamageDelay", "FlameDamageExpiration", "BurnedDelay"):
            value = _flammable_expression(
                amap.get(key.casefold()), target_id, key, timer=True
            )
            if value is not None:
                fields[key] = value
        for key in ("AflameDamageAmount", "FlameDamageLimit"):
            value = _flammable_expression(
                amap.get(key.casefold()), target_id, key,
                allow_multiply=key == "FlameDamageLimit",
            )
            if value is not None:
                fields[key] = value
        for key in ("BurnContained", "SetBurnedStatus"):
            value = _yes_no_field(
                amap.get(key.casefold()), f"{target_id} FlammableUpdate {key}"
            )
            if value is not None:
                fields[key] = value
        for key in ("DamageType", "BurningSoundName"):
            value = _string_field(amap.get(key.casefold()))
            if value is not None:
                if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", str(value["value"])) is None:
                    raise ModuleContractError(f"{target_id} FlammableUpdate {key} malformed")
                fields[key] = value
        fx_rows = [
            _flammable_fire_fx(item, target_id)
            for item in grouped.get("firefxlist", [])
        ]
        if fx_rows:
            fields["FireFXList"] = fx_rows
        rows.append(_row("FlammableUpdate", block, fields))
    rows.sort(key=lambda r: (str(r["sourceIni"]).casefold(), int(r["line"])))
    return rows


def _dynamic_portal_waypoint(
    assignment: SageAssignment, target_id: str
) -> dict[str, object]:
    match = re.fullmatch(
        r"\s*Index\s*:\s*(\d+)\s+Type\s*:\s*([A-Za-z_][A-Za-z0-9_]*)\s*",
        assignment.value, re.IGNORECASE,
    )
    if match is None:
        raise ModuleContractError(f"{target_id} DynamicPortalBehaviour WayPoint malformed")
    return {
        **_authored_row(assignment), "index": int(match.group(1)),
        "type": match.group(2),
    }


def _dynamic_portal_link(
    assignment: SageAssignment, target_id: str
) -> dict[str, object]:
    clauses = re.findall(r"([A-Za-z]+)\s*:\s*(\d+)", assignment.value)
    if not clauses or re.sub(r"[A-Za-z]+\s*:\s*\d+", "", assignment.value).strip():
        raise ModuleContractError(f"{target_id} DynamicPortalBehaviour Link malformed")
    names = [name.casefold() for name, _ in clauses]
    if names[0] != "from" or names[-1] != "to" or any(
        name != "via" for name in names[1:-1]
    ):
        raise ModuleContractError(f"{target_id} DynamicPortalBehaviour Link malformed")
    return {
        **_authored_row(assignment), "from": int(clauses[0][1]),
        "via": [int(value) for _, value in clauses[1:-1]],
        "to": int(clauses[-1][1]),
    }


def compile_dynamic_portal_behaviours(
    lineage: Sequence[SageObject], target_id: str
) -> list[dict[str, object]]:
    """Compile portal graph nodes, links, admission, upgrades, and attack geometry."""

    rows: list[dict[str, object]] = []
    for block in _module_blocks(lineage, "DynamicPortalBehaviour"):
        if block.blocks:
            raise ModuleContractError(f"{target_id} DynamicPortalBehaviour nested blocks")
        grouped: dict[str, list[SageAssignment]] = {}
        for assignment in block.assignments:
            grouped.setdefault(assignment.key.casefold(), []).append(assignment)
        if set(grouped) - _DYNAMIC_PORTAL_FIELDS or any(
            len(values) > 1 and key not in {"waypoint", "link"}
            for key, values in grouped.items()
        ):
            raise ModuleContractError(f"{target_id} DynamicPortalBehaviour malformed fields")
        amap = {key: values[-1] for key, values in grouped.items()}
        fields: dict[str, object] = {}
        activation = _seconds_expression_field(
            amap.get("activationdelayseconds"), target_id, "ActivationDelaySeconds"
        )
        if activation is not None:
            fields["ActivationDelaySeconds"] = activation
        for key in ("GenerateNow", "AllowEnemies"):
            value = _yes_no_field(
                amap.get(key.casefold()), f"{target_id} DynamicPortalBehaviour {key}"
            )
            if value is not None:
                fields[key] = value
        for key in ("ObjectFilter", "ConflictsWith"):
            value = _token_list_field(amap.get(key.casefold()))
            if value is not None:
                if not value["value"]:
                    raise ModuleContractError(f"{target_id} DynamicPortalBehaviour {key} empty")
                fields[key] = value
        for key in ("BonePrefix", "TriggeredBy"):
            value = _string_field(amap.get(key.casefold()))
            if value is not None:
                if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", str(value["value"])) is None:
                    raise ModuleContractError(f"{target_id} DynamicPortalBehaviour {key} malformed")
                fields[key] = value
        for key in ("NumberOfBones", "AboveWall"):
            value = _integer_assignment_field(
                amap.get(key.casefold()), f"{target_id} DynamicPortalBehaviour {key}",
                minimum=1,
            )
            if value is not None:
                fields[key] = value
        radius = _number_field(
            amap.get("topattackradius"), f"{target_id} DynamicPortalBehaviour TopAttackRadius"
        )
        if radius is not None:
            if float(radius["value"]) < 0:
                raise ModuleContractError(f"{target_id} DynamicPortalBehaviour TopAttackRadius negative")
            fields["TopAttackRadius"] = radius
        position = _coord_field(
            amap.get("topattackpos"), f"{target_id} DynamicPortalBehaviour TopAttackPos"
        )
        if position is not None:
            fields["TopAttackPos"] = position
        waypoints = [
            _dynamic_portal_waypoint(item, target_id)
            for item in grouped.get("waypoint", [])
        ]
        links = [
            _dynamic_portal_link(item, target_id) for item in grouped.get("link", [])
        ]
        if waypoints:
            fields["WayPoint"] = waypoints
        if links:
            fields["Link"] = links
        custom = amap.get("customanimandduration")
        if custom is not None:
            match = re.fullmatch(
                r"\s*AnimState\s*:\s*([A-Za-z_][A-Za-z0-9_]*)\s+"
                r"AnimTime\s*:\s*(\d+)\s*", custom.value, re.IGNORECASE,
            )
            if match is None:
                raise ModuleContractError(
                    f"{target_id} DynamicPortalBehaviour CustomAnimAndDuration malformed"
                )
            fields["CustomAnimAndDuration"] = {
                **_authored_row(custom), "animState": match.group(1),
                "animTimeMilliseconds": int(match.group(2)),
            }
        for required in ("ObjectFilter", "BonePrefix", "NumberOfBones", "WayPoint", "Link"):
            if required not in fields:
                raise ModuleContractError(f"{target_id} DynamicPortalBehaviour requires {required}")
        rows.append(_row("DynamicPortalBehaviour", block, fields))
    rows.sort(key=lambda r: (str(r["sourceIni"]).casefold(), int(r["line"])))
    return rows


def compile_delayed_death_bodies(
    lineage: Sequence[SageObject], target_id: str
) -> list[dict[str, object]]:
    """Compile delayed-body health thresholds and death timers as typed evidence."""

    rows: list[dict[str, object]] = []
    for block in _module_blocks(lineage, "DelayedDeathBody"):
        amap = _assignment_map(block)
        if (
            block.blocks
            or len(amap) != len(block.assignments)
            or set(amap) - _DELAYED_DEATH_BODY_FIELDS
        ):
            raise ModuleContractError(f"{target_id} DelayedDeathBody malformed")
        fields: dict[str, object] = {}
        for key in (
            "MaxHealth", "MaxHealthDamaged", "MaxHealthReallyDamaged", "CheerRadius",
        ):
            value = _non_negative_expression_field(
                amap.get(key.casefold()), f"{target_id} DelayedDeathBody {key}"
            )
            if value is not None:
                fields[key] = value
        for key in ("DelayedDeathTime", "RecoveryTime"):
            value = _milliseconds_field(
                amap.get(key.casefold()), f"{target_id} DelayedDeathBody {key}"
            )
            if value is not None:
                fields[key] = value
        for key in (
            "CanRespawn", "DoHealthCheck", "ImmortalUntilDeathTime",
            "BurningDeathBehavior",
        ):
            value = _yes_no_field(
                amap.get(key.casefold()), f"{target_id} DelayedDeathBody {key}"
            )
            if value is not None:
                fields[key] = value
        fx = _string_field(amap.get("burningdeathfx"))
        if fx is not None:
            if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", str(fx["value"])) is None:
                raise ModuleContractError(f"{target_id} DelayedDeathBody BurningDeathFX malformed")
            fields["BurningDeathFX"] = fx
        dodge = _number_field(
            amap.get("dodgepercent"), f"{target_id} DelayedDeathBody DodgePercent"
        )
        if dodge is not None:
            percent = float(dodge["value"])
            if percent < 0 or percent > 100:
                raise ModuleContractError(f"{target_id} DelayedDeathBody DodgePercent out of range")
            dodge["percent"] = int(percent) if percent.is_integer() else percent
            dodge["ratio"] = percent / 100.0
            fields["DodgePercent"] = dodge
        for required in ("MaxHealth", "DelayedDeathTime", "CanRespawn"):
            if required not in fields:
                raise ModuleContractError(f"{target_id} DelayedDeathBody requires {required}")
        rows.append(_row("DelayedDeathBody", block, fields))
    rows.sort(key=lambda r: (str(r["sourceIni"]).casefold(), int(r["line"])))
    return rows


def compile_slaved_updates(lineage: Sequence[SageObject], target_id: str) -> list[dict[str, object]]:
    rows=[]
    for block in _behavior_blocks(lineage,"SlavedUpdate"):
        amap=_assignment_map(block)
        if block.blocks or len(amap)!=len(block.assignments) or set(amap)-_SLAVED_FIELDS:raise ModuleContractError(f"{target_id} SlavedUpdate malformed")
        fields={}
        for key in ("LeashRange","GuardMaxRange","GuardWanderRange","AttackRange","FadeOutRange"):
            value=_number_field(amap.get(key.casefold()),f"{target_id} SlavedUpdate {key}")
            if value is not None:
                if float(value["value"])<0:raise ModuleContractError(f"{target_id} SlavedUpdate {key} negative")
                fields[key]=value
        for key in ("UseSlaverAsControlForEvaObjectSightedEvents","DieOnMastersDeath","MarkUnselectable"):
            value=_yes_no_field(amap.get(key.casefold()),f"{target_id} SlavedUpdate {key}")
            if value is not None:fields[key]=value
        offset=_coord_field(amap.get("guardpositionoffset"),f"{target_id} SlavedUpdate GuardPositionOffset")
        if offset is not None:fields["GuardPositionOffset"]=offset
        fade=_milliseconds_field(amap.get("fadetime"),f"{target_id} SlavedUpdate FadeTime")
        if fade is not None:fields["FadeTime"]=fade
        rows.append(_row("SlavedUpdate",block,fields))
    rows.sort(key=lambda r:(str(r["sourceIni"]).casefold(),int(r["line"])));return rows


def compile_castle_upgrades(lineage: Sequence[SageObject], target_id: str) -> list[dict[str, object]]:
    rows=[]
    for block in _behavior_blocks(lineage,"CastleUpgrade"):
        amap=_assignment_map(block)
        if block.blocks or len(amap)!=len(block.assignments) or set(amap)-{"triggeredby","upgrade","wallupgraderadius"}:raise ModuleContractError(f"{target_id} CastleUpgrade malformed")
        fields={}
        for key in ("TriggeredBy","Upgrade"):
            value=_string_field(amap.get(key.casefold()))
            if value is None:raise ModuleContractError(f"{target_id} CastleUpgrade requires {key}")
            fields[key]=value
        radius=_non_negative_expression_field(amap.get("wallupgraderadius"),f"{target_id} CastleUpgrade WallUpgradeRadius")
        if radius is not None:fields["WallUpgradeRadius"]=radius
        rows.append(_row("CastleUpgrade",block,fields))
    rows.sort(key=lambda r:(str(r["sourceIni"]).casefold(),int(r["line"])));return rows


# Remaining Class C/D/E kinds converted as opaque-authored deferred evidence.
# Typed extractors above own their kinds; this set must not overlap them.
# Generated from retail_module_census unhandled status at conversion time;
# empty residual unhandled is the acceptance criterion for this batch.
OPAQUE_DEFERRED_MODULE_KINDS: frozenset[str] = frozenset(
    {
        "AODCrushCollide",
        "AimWeaponBehavior",
        "AnnounceBirthAndDeathBehavior",
        "AttributeModifierPoolUpdate",
        "AudioLoopUpgrade",
        "AutoPickUpUpdate",
        "BaseUpgrade",
        "BeaconClientUpdate",
        "BloodthirstyUpdate",
        "BoredUpdate",
        "BridgeBehavior",
        "CivilianSpawnCollide",
        "CivilianSpawnUpdate",
        "ClickReactionBehavior",
        "CloudBreakSpecialPower",
        "CommandButtonHuntUpdate",
        "CritterEmitterUpdate",
        "DamageFilteredCreateObjectDie",
        "DarknessSpecialPower",
        "DefaultDraw",
        "DelayedLuaEventUpdate",
        "DestroyEnvironmentUpdate",
        "DetachableRiderBody",
        "DetachableRiderUpdate",
        "DevastateSpecialPower",
        "DoCommandUpgrade",
        "DynamicShroudClearingRangeUpdate",
        "ElvenWoodSpecialPower",
        "EvaAnnounceClientCreate",
        "EvacuateDamage",
        "FXListDie",
        "FadeAndDieOrnamentUpdate",
        "FellBeastSwoopPower",
        "FloodUpdate",
        "GateProxyBehavior",
        "HeroDie",
        "HordeNotifyTargetsOfImminentProbableCrushingUpdate",
        "HordeSiegeEngineContain",
        "HordeTransportContainDamage",
        "LaserUpdate",
        "ModelConditionUpgrade",
        "OathbreakersFadeAwayBehavior",
        "OilSpillUpdate",
        "PartTheHeavensUpdate",
        "PassiveAreaEffectBehavior",
        "PillageModule",
        "PlayerUpgradeSpecialPower",
        "PorcupineFormationBodyModule",
        "ProductionSpeedBonus",
        "ProjectileStreamUpdate",
        "RadarMarkerClientUpdate",
        "RemoveUpgradeUpgrade",
        "RousingSpeechUpdate",
        "RubbleRiseUpdate",
        "RunOffMapBehavior",
        "ShareExperienceBehavior",
        "SiegeDeployHordeSpecialPower",
        "SlaughterHordeContain",
        "SlaveWatcherBehavior",
        "StrafeAreaUpdate",
        "StructureToppleUpdate",
        "SummonReplacementSpecialAbilityUpdate",
        "SupplyCenterDockUpdate",
        "SwayClientUpdate",
        "SymbioticStructuresBody",
        "TaintSpecialPower",
        "TerrainResourceClientBehavior",
        "TooltipUpgrade",
        "ToppleUpdate",
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
        "WallUpgradeUpdate",
        "WeaponChangeSpecialPowerModule",
    }
)

# Typed extractors already own these names; never also emit opaque rows.
TYPED_MODULE_KINDS: frozenset[str] = frozenset(
    {
        "AttributeModifierUpgrade",
        "AttributeModifierAuraUpdate",
        "AutoHealBehavior",
        "AIUpdateInterface",
        "StancesBehavior",
        "HordeContain",
        "HordeAIUpdate",
        "PickupStuffUpdate",
        "AutoAbilityBehavior",
        "RespawnUpdate",
        "DualWeaponBehavior",
        "CastleMemberBehavior",
        "EmotionTrackerUpdate",
        "FireWeaponUpdate",
        "DeletionUpdate",
        "SiegeDockingBehavior",
        "RefundDie",
        "FireSpreadUpdate",
        "InvisibilityUpdate",
        "AttachUpdate",
        "ClearanceTestingSlowDeathBehavior",
        "ProductionUpdate",
        "SquishCollide",
        "GettingBuiltBehavior",
        "AISpecialPowerUpdate",
        "BuildingBehavior",
        "QueueProductionExitUpdate",
        "RebuildHoleExposeDie",
        "RebuildHoleBehavior",
        "SalvageCrateCollide",
        "HordeMemberCollide",
        "BannerCarrierUpdate",
        "RespawnBody",
        "NotifyTargetsOfImminentProbableCrushingUpdate",
        "FoundationAIUpdate",
        "MonitorConditionUpdate",
        "GiveUpgradeUpdate",
        "GateOpenAndCloseBehavior",
        "AIGateUpdate",
        "FakePathfindPortalBehaviour",
        "StealthDetectorUpdate",
        "SlavedUpdate",
        "CastleUpgrade",
        "DelayedDeathBody",
        "DynamicPortalBehaviour",
        "FlammableUpdate",
        "SpawnBehavior",
        "StealthUpdate",
        "ObjectCreationUpgrade",
        "OCLUpdate",
        "TransportContain",
        "TunnelContain",
        "GarrisonContain",
        "HordeGarrisonContain",
        "LargeGroupBonusUpdate",
        "ProductionQueueHordeContain",
        "SiegeEngineContain",
        "LargeGroupAudioUpdate",
        "HitReactionBehavior",
        "AnimalAIUpdate",
        "ThreatFinderUpdate",
        "ModelConditionSoundSelectorClientBehavior",
        "RandomSoundSelectorClientBehavior",
        "AnimationSoundClientBehavior",
        "RadiateFearUpdate",
        "PoisonedBehavior",
        "DamageFieldUpdate",
        "SpawnUnitBehavior",
        "ReplaceSelfUpgrade",
        "CitadelSlaughterHordeContain",
        "WallHubBehavior",
        "ActivateModuleSpecialPower",
        "WeaponModeSpecialPowerUpdate",
        "DominateEnemySpecialPower",
        "GrabPassengerSpecialPower",
        "FlingPassengerSpecialAbilityUpdate",
        "TemporarilyDefectUpdate",
        "RepairSpecialPower",
        "HordeDispatchSpecialPower",
        "StopSpecialPower",
        "SiegeDeploySpecialPower",
        "DeflectSpecialPower",
        "SplitHordeSpecialPower",
        "DeployStyleAIUpdate",
        "ToggleDeploySpecialAbilityUpdate",
        "SpecialDisguiseUpdate",
        "UnleashSpecialPower",
        "SpecialEnemySenseUpdate",
        "ScavengerSpecialPower",
        "GeometryUpgrade",
        "SubObjectsUpgrade",
        "TransitionDamageFX",
        "InactiveBody",
        "SpawnPointProductionExitUpdate",
        "SupplyCenterProductionExitUpdate",
        "SupplyCenterCreate",
        "BuildableHeroListUpgrade",
        "AllowBannerSpawnUpgrade",
        "SpellRechargeModifierUpgrade",
        "KeepObjectDie",
        "CreateObjectDie",
        "PhysicsBehavior",
        "BezierProjectileBehavior",
        "FireWeaponWhenDeadBehavior",
        "ShipSlowDeathBehavior",
        "SlowDeathBehavior",
        "HordeTransportContain",
        "LifetimeUpdate",
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
    lineage: Sequence[SageObject],
    target_id: str,
    *,
    numeric_defines: Mapping[str, int | float] | None = None,
    numeric_define_provenance: Mapping[str, Mapping[str, object]] | None = None,
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
    rows.extend(compile_sub_objects_upgrades(lineage, target_id))
    rows.extend(compile_transition_damage_fx(lineage, target_id))
    rows.extend(compile_inactive_bodies(lineage, target_id))
    rows.extend(compile_spawn_point_production_exits(lineage, target_id))
    rows.extend(compile_supply_center_production_exits(lineage, target_id))
    rows.extend(compile_supply_center_creates(lineage, target_id))
    rows.extend(compile_buildable_hero_list_upgrades(lineage, target_id))
    rows.extend(compile_allow_banner_spawn_upgrades(lineage, target_id))
    rows.extend(compile_spell_recharge_modifier_upgrades(lineage, target_id))
    rows.extend(compile_keep_object_die(lineage, target_id))
    rows.extend(compile_create_object_die(lineage, target_id))
    rows.extend(compile_physics_behaviors(lineage, target_id))
    rows.extend(compile_bezier_projectile_behaviors(lineage, target_id))
    rows.extend(compile_fire_weapon_when_dead_behaviors(lineage, target_id))
    rows.extend(compile_ship_slow_death_behaviors(lineage, target_id))
    rows.extend(compile_slow_death_behaviors(
        lineage,
        target_id,
        numeric_defines=numeric_defines,
        numeric_define_provenance=numeric_define_provenance,
    ))
    rows.extend(compile_horde_transport_contains(lineage, target_id))
    rows.extend(compile_attribute_modifier_aura_updates(lineage, target_id))
    rows.extend(compile_auto_heal_behaviors(
        lineage,
        target_id,
        numeric_defines=numeric_defines,
        numeric_define_provenance=numeric_define_provenance,
    ))
    rows.extend(compile_lifetime_updates(lineage, target_id))
    rows.extend(compile_ai_update_interfaces(lineage, target_id))
    rows.extend(compile_stances_behaviors(lineage, target_id))
    rows.extend(compile_horde_contains(lineage, target_id))
    rows.extend(compile_horde_ai_updates(lineage, target_id))
    rows.extend(compile_pickup_stuff_updates(lineage, target_id))
    rows.extend(compile_auto_ability_behaviors(lineage, target_id))
    rows.extend(compile_respawn_updates(
        lineage,
        target_id,
        numeric_defines=numeric_defines,
        numeric_define_provenance=numeric_define_provenance,
    ))
    rows.extend(compile_dual_weapon_behaviors(lineage, target_id))
    rows.extend(compile_castle_member_behaviors(lineage, target_id))
    rows.extend(compile_emotion_tracker_updates(lineage, target_id))
    rows.extend(compile_fire_weapon_updates(lineage, target_id))
    rows.extend(compile_deletion_updates(lineage, target_id))
    rows.extend(compile_siege_docking_behaviors(lineage, target_id))
    rows.extend(compile_refund_die_behaviors(lineage, target_id))
    rows.extend(compile_fire_spread_updates(lineage, target_id))
    rows.extend(compile_invisibility_updates(lineage, target_id))
    rows.extend(compile_attach_updates(lineage, target_id))
    rows.extend(compile_clearance_testing_slow_death_behaviors(lineage, target_id))
    rows.extend(compile_production_updates(lineage, target_id))
    rows.extend(compile_squish_collides(lineage, target_id))
    rows.extend(compile_getting_built_behaviors(lineage, target_id))
    rows.extend(compile_ai_special_power_updates(lineage, target_id))
    rows.extend(compile_building_behaviors(lineage, target_id))
    rows.extend(compile_queue_production_exit_updates(
        lineage,
        target_id,
        numeric_defines=numeric_defines,
        numeric_define_provenance=numeric_define_provenance,
    ))
    rows.extend(compile_rebuild_hole_expose_dies(lineage, target_id))
    rows.extend(compile_rebuild_hole_behaviors(lineage, target_id))
    rows.extend(compile_salvage_crate_collides(lineage, target_id))
    rows.extend(compile_horde_member_collides(lineage, target_id))
    rows.extend(compile_banner_carrier_updates(lineage, target_id))
    rows.extend(compile_respawn_bodies(lineage, target_id))
    rows.extend(compile_notify_crushing_updates(lineage, target_id))
    rows.extend(compile_foundation_ai_updates(lineage, target_id))
    rows.extend(compile_monitor_condition_updates(lineage, target_id))
    rows.extend(compile_give_upgrade_updates(lineage, target_id))
    rows.extend(compile_gate_open_close_behaviors(lineage, target_id))
    rows.extend(compile_ai_gate_updates(lineage, target_id))
    rows.extend(compile_fake_pathfind_portals(lineage, target_id))
    rows.extend(compile_stealth_detector_updates(lineage, target_id))
    rows.extend(compile_slaved_updates(lineage, target_id))
    rows.extend(compile_castle_upgrades(lineage, target_id))
    rows.extend(compile_delayed_death_bodies(lineage, target_id))
    rows.extend(compile_dynamic_portal_behaviours(lineage, target_id))
    rows.extend(compile_flammable_updates(lineage, target_id))
    rows.extend(compile_spawn_behaviors(lineage, target_id))
    rows.extend(compile_stealth_updates(lineage, target_id))
    rows.extend(compile_object_creation_upgrades(lineage, target_id))
    rows.extend(compile_ocl_updates(lineage, target_id))
    rows.extend(compile_transport_contains(lineage, target_id))
    rows.extend(compile_tunnel_contains(lineage, target_id))
    rows.extend(compile_garrison_contains(lineage, target_id))
    rows.extend(compile_horde_garrison_contains(lineage, target_id))
    rows.extend(compile_large_group_bonus_updates(lineage, target_id))
    rows.extend(compile_production_queue_horde_contains(lineage, target_id))
    rows.extend(compile_siege_engine_contains(lineage, target_id))
    rows.extend(compile_large_group_audio_updates(lineage, target_id))
    rows.extend(compile_hit_reaction_behaviors(lineage, target_id))
    rows.extend(compile_animal_ai_updates(lineage, target_id))
    rows.extend(compile_threat_finder_updates(lineage, target_id))
    rows.extend(compile_model_condition_sound_selectors(lineage, target_id))
    rows.extend(compile_random_sound_selectors(lineage, target_id))
    rows.extend(compile_animation_sound_client_behaviors(lineage, target_id))
    rows.extend(compile_radiate_fear_updates(lineage, target_id))
    rows.extend(compile_poisoned_behaviors(lineage, target_id))
    rows.extend(compile_damage_field_updates(lineage, target_id))
    rows.extend(compile_spawn_unit_behaviors(lineage, target_id))
    rows.extend(compile_replace_self_upgrades(lineage, target_id))
    rows.extend(compile_citadel_slaughter_horde_contains(lineage, target_id))
    rows.extend(compile_wall_hub_behaviors(lineage, target_id))
    rows.extend(compile_activate_module_special_powers(lineage, target_id))
    rows.extend(compile_weapon_mode_special_power_updates(lineage, target_id))
    rows.extend(compile_dominate_enemy_special_powers(lineage, target_id))
    rows.extend(compile_grab_passenger_special_powers(lineage, target_id))
    rows.extend(compile_fling_passenger_special_ability_updates(lineage, target_id))
    rows.extend(compile_repair_special_powers(lineage, target_id))
    rows.extend(compile_horde_dispatch_special_powers(lineage, target_id))
    rows.extend(compile_stop_special_powers(lineage, target_id))
    rows.extend(compile_siege_deploy_special_powers(lineage, target_id))
    rows.extend(compile_deflect_special_powers(lineage, target_id))
    rows.extend(compile_split_horde_special_powers(lineage, target_id))
    rows.extend(compile_deploy_style_ai_updates(lineage, target_id))
    rows.extend(compile_toggle_deploy_special_ability_updates(lineage, target_id))
    rows.extend(compile_special_disguise_updates(lineage, target_id))
    rows.extend(compile_unleash_special_powers(lineage, target_id))
    rows.extend(compile_special_enemy_sense_updates(
        lineage,
        target_id,
        numeric_defines=numeric_defines,
        numeric_define_provenance=numeric_define_provenance,
    ))
    rows.extend(compile_scavenger_special_powers(lineage, target_id))
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
        row_evidence = module in ROW_EXECUTABLE_TYPED_MODULE_EVIDENCE and (
            (module == "BezierProjectileBehavior" and _bezier_common_landing_shape(fields))
            or (module == "GeometryUpgrade" and _geometry_upgrade_row_has_closed_runtime(fields))
            or (module == "SubObjectsUpgrade" and _sub_objects_upgrade_row_has_closed_runtime(fields))
            or (module == "TransitionDamageFX" and _transition_damage_fx_row_has_closed_runtime(fields))
            or (module == "AnimationSoundClientBehavior" and _animation_sound_row_has_closed_runtime(fields))
            or (module == "QueueProductionExitUpdate" and _queue_exit_row_has_closed_runtime(fields))
            or (module == "SpawnBehavior" and _spawn_reclaim_row_has_closed_runtime(fields))
            or (module == "SlowDeathBehavior" and _slow_death_row_has_closed_runtime(row))
            or (
                module == "SpecialDisguiseUpdate"
                and "TriggerAttributeModifier" not in fields
                and "TriggerInstantlyOnCreate" not in fields
                and "AwardXPForTriggering" not in fields
                and isinstance(fields.get("ForceMountedWhenDisguising"), Mapping)
                and fields["ForceMountedWhenDisguising"].get("value") is True
            )
        )
        if (
            status == "executable"
            and module not in EXECUTABLE_TYPED_MODULE_KINDS
            and not row_evidence
        ):
            raise ModuleContractError(
                f"{label} executable moduleContracts row for {module} lacks closed runtime evidence"
            )
        if module == "BezierProjectileBehavior" and extraction == "typed":
            expected_status = (
                "executable" if _bezier_common_landing_shape(fields) else "deferred"
            )
            if status != expected_status or row.get("effectGraph") != _bezier_effect_graph(fields):
                raise ModuleContractError(
                    f"{label} BezierProjectileBehavior trajectory graph drifted"
                )
        identity = (module.casefold(), line, source.casefold())
        if identity in seen:
            raise ModuleContractError(
                f"{label} moduleContracts duplicate {module}@{source}:{line}"
            )
        seen.add(identity)
