"""Compile BFME2 retail references into one playable-structure descriptor.

Structures share the playable-unit corpus preparation but classify by the
STRUCTURE KindOf family, produce through authored construct or wall-upgrade
commands instead of UNIT_BUILD sockets, and carry lifecycle health facts
instead of unit core animation states.  This module is object-name agnostic;
engine-spawned fortress composites are admitted only through the caller-owned
implicit-root policy.
"""

from __future__ import annotations

import re
from functools import lru_cache

from collections import defaultdict
from collections.abc import Iterable, Mapping, Sequence
import hashlib
import json
import re

from .module_contracts import (
    ModuleContractError,
    compile_all_module_contracts,
    validate_module_contracts,
)
from .castle_behavior import (
    CastleBehaviorCompilerError,
    harvest_castle_upgrade_behaviors,
)
from .playable_unit_compiler import (
    ATTRIBUTE_MODIFIER_PATH,
    COMMAND_BUTTON_PATH,
    COMMAND_SET_PATH,
    EXPERIENCE_LEVELS_PATH,
    UPGRADE_PATH,
    PlayableUnitCompilerError,
    PlayableUnitCompilerInputs,
    _ability_list_defines,
    _ancestry,
    _audio_routes,
    _block_values,
    _command_slots,
    _base_weapon_damage,
    _default_nested_target,
    _default_set_target,
    _default_weapon_slot,
    _effective_top_blocks,
    _effective_values,
    _experience_level_rows,
    _first,
    _geometry_contract,
    _kind_of,
    _level_modifier_leaf,
    _named_blocks,
    _named_definition_values,
    _optional_document,
    _scalar_fields,
    _resolved_definition_field,
    _apply_nugget_damage_types,
    _select_experience_chain,
    _tokens,
    _walk_blocks,
    prepare_playable_unit_compiler,
)
from .sage_cst import SageAssignment, SageBlock, SageObject
from .sage_ini import IniBlock


SCHEMA = "openbfme.playable-structure-descriptor"
SCHEMA_VERSION = 0
STRUCTURE_KIND_TOKENS = frozenset(
    {"STRUCTURE", "BASE_FOUNDATION", "FS_BASE_DEFENSE"}
)
_CONSTRUCT_COMMANDS = (
    {"dozer_construct"},
    {"porter_construct"},
    {"foundation_construct"},
)
_WALL_UPGRADE_COMMAND = "object_upgrade"
# Retail authors ONE fortress-menu button with this command type instead:
# Command_PurchaseUpgradeMenFortressHouseOfHealing (commandbutton.ini:13283).
# It sells exactly like the OBJECT_UPGRADE improvements around it.
_CASTLE_UPGRADE_COMMAND = "castle_upgrade"
# The fortress command set is ONE set the engine reveals in ranges: a
# PUSH_VISIBLE_COMMAND_RANGE selector opens the improvements/heroes page and
# POP_VISIBLE_COMMAND_RANGE (`Command_RadialBack`) closes it.  These buttons
# carry retail's own TextLabel/DescriptLabel, and the packs never named them,
# so the strings lane -- which publishes exactly the rows a runtime document
# references -- had nothing to resolve and the buttons drew fallback text.
_RADIAL_PAGE_COMMANDS = frozenset(
    {"push_visible_command_range", "pop_visible_command_range"}
)
_RADIAL_PAGE_RANGE_FIELDS = (
    ("CommandRangeStart", "commandRangeStart"),
    ("CommandRangeCount", "commandRangeCount"),
)
_HEALTH_FIELDS = ("MaxHealth", "MaxHealthDamaged", "MaxHealthReallyDamaged")


class PlayableStructureCompilerError(ValueError):
    """The requested structure descriptor cannot be derived without guessing."""


@lru_cache(maxsize=16)
def _decoded_ini_lines(payload: bytes) -> tuple[str, ...]:
    return tuple(payload.decode("cp1252", errors="strict").splitlines())


def _weapon_has_authored_slave_attack_nugget(
    documents: Mapping[str, bytes], weapon_id: str, weapon: Mapping[str, object]
) -> bool:
    """Return whether the named Weapon directly authors SlaveAttackNugget.

    The acquisition-shell -> ThingToSpawn join is specific to this SAGE
    nugget.  Weapon incompleteness alone is not evidence: ordinary tower bows
    author interval-valued delays that the scalar compiler intentionally does
    not flatten, and must remain valid lifecycle-only structure descriptors.
    """

    header = re.compile(
        rf"^Weapon\s+{re.escape(weapon_id)}(?:\s+\S+)?\s*$", re.IGNORECASE
    )
    source_paths = {
        str(row.get("sourceIni", ""))
        for value in weapon.values()
        for row in (
            [value]
            if isinstance(value, Mapping)
            else value
            if isinstance(value, Sequence) and not isinstance(value, (str, bytes))
            else []
        )
        if isinstance(row, Mapping) and str(row.get("sourceIni", ""))
    }
    payloads = (
        [documents[path] for path in source_paths if path in documents]
        if source_paths
        else list(documents.values())
    )
    for payload in payloads:
        inside = False
        for raw_line in _decoded_ini_lines(payload):
            if not inside:
                inside = header.fullmatch(raw_line.strip()) is not None
                continue
            active = raw_line.split(";", 1)[0].split("//", 1)[0].strip()
            if (
                raw_line
                and not raw_line[0].isspace()
                and active
                and active.casefold() != "end"
            ):
                break
            if active.casefold().startswith("slaveattacknugget"):
                return True
    return False


def _structure_combat_contract(
    lineage: Sequence[SageObject],
    documents: Mapping[str, bytes],
    prepared: PlayableUnitCompilerInputs,
) -> dict[str, object] | None:
    """Compile an authored default structure WeaponSet without approximations.

    Fortress artillery expansions are ordinary armed SAGE objects.  Treating
    structure descriptors as lifecycle-only discarded their WeaponSet join,
    so the runtime had neither acquisition numbers nor a projectile identity.
    """

    weapon_id = _default_set_target(lineage, "WeaponSet", "Weapon")
    if weapon_id is None:
        return None
    weapon = _named_definition_values(
        documents,
        "Weapon",
        weapon_id,
        cache=prepared.named_definition_cache,
        cache_lock=prepared.cache_lock,
    )
    if weapon is None:
        raise PlayableStructureCompilerError(
            f"structure default weapon is unresolvable: {weapon_id}"
        )
    combat: dict[str, object] = {"weaponId": weapon_id}
    weapon_slot = _default_weapon_slot(lineage, weapon_id)
    if weapon_slot is not None:
        combat["weaponSlot"] = weapon_slot
    for output_name, source_name in (
        ("attackRange", "AttackRange"),
        ("minimumAttackRange", "MinimumAttackRange"),
        ("projectileSpeed", "WeaponSpeed"),
        ("delayBetweenShotsMs", "DelayBetweenShots"),
        ("preAttackDelayMs", "PreAttackDelay"),
        ("firingDurationMs", "FiringDuration"),
        ("damage", "Damage"),
    ):
        field = _resolved_definition_field(
            weapon, source_name, prepared.numeric_defines
        )
        if field is not None:
            combat[output_name] = field

    warhead_id = _default_nested_target(
        documents,
        "Weapon",
        weapon_id,
        "WarheadTemplateName",
        flat_kind_cache=prepared.flat_kind_cache,
        cache_lock=prepared.cache_lock,
    )
    damage_owner = weapon
    if warhead_id:
        warhead = _named_definition_values(
            documents,
            "Weapon",
            warhead_id,
            cache=prepared.named_definition_cache,
            cache_lock=prepared.cache_lock,
        )
        if warhead is None:
            raise PlayableStructureCompilerError(
                f"structure weapon warhead is unresolvable: {warhead_id}"
            )
        combat["warheadId"] = warhead_id
        damage_owner = warhead
        damage = _resolved_definition_field(
            warhead, "Damage", prepared.numeric_defines
        )
        if damage is not None:
            combat["damage"] = damage
    if "damage" not in combat:
        damage = _base_weapon_damage(
            documents,
            warhead_id or weapon_id,
            prepared.numeric_defines,
            cache=prepared.named_definition_cache,
            cache_lock=prepared.cache_lock,
        )
        if damage is not None:
            combat["damage"] = damage

    projectile_id = _default_nested_target(
        documents,
        "Weapon",
        weapon_id,
        "ProjectileTemplateName",
        flat_kind_cache=prepared.flat_kind_cache,
        cache_lock=prepared.cache_lock,
    )
    if projectile_id:
        combat["projectileObjectId"] = projectile_id
    damage_types = {
        str(row.get("expression", "")).casefold(): str(row.get("expression", ""))
        for row in damage_owner.get("damagetype", ())
        if str(row.get("expression", ""))
    }
    if len(damage_types) == 1:
        combat["damageType"] = next(iter(damage_types.values()))
    elif not damage_types:
        _apply_nugget_damage_types(combat)

    required = {
        "attackRange",
        "delayBetweenShotsMs",
        "preAttackDelayMs",
        "firingDurationMs",
        "damage",
    }
    if required - combat.keys():
        # Fortress artillery expansions author a fake acquisition weapon with
        # SlaveAttackNugget; an ObjectCreationUpgrade spawns the real turret,
        # whose WeaponSet owns projectile speed, warhead and damage. Follow
        # that authored join rather than inventing combat on the shell.
        if not _weapon_has_authored_slave_attack_nugget(
            documents, weapon_id, weapon
        ):
            return None
        spawned_ids = {
            tokens[0].casefold(): tokens[0]
            for block in _effective_top_blocks(lineage)
            if (block.header_key or "").casefold() == "behavior"
            and block.kind.casefold() == "objectcreationupgrade"
            for row in block.assignments
            if row.key.casefold() == "thingtospawn"
            for tokens in [_tokens(row.value)]
            if len(tokens) == 1
        }
        if len(spawned_ids) == 1:
            spawned_id = next(iter(spawned_ids.values()))
            spawned = prepared.objects.get(spawned_id.casefold())
            if spawned is None:
                raise PlayableStructureCompilerError(
                    f"structure artillery slave object is missing: {spawned_id}"
                )
            delivery = _structure_combat_contract(
                _ancestry(prepared.objects, spawned), documents, prepared
            )
            if delivery is not None:
                for field in (
                    "attackRange",
                    "minimumAttackRange",
                    "delayBetweenShotsMs",
                    "preAttackDelayMs",
                    "firingDurationMs",
                ):
                    if field in combat:
                        delivery[field] = combat[field]
                delivery["targetAcquisitionWeaponId"] = weapon_id
                delivery["spawnedObjectId"] = spawned_id
                return delivery
        raise PlayableStructureCompilerError(
            f"structure weapon contract is incomplete: {weapon_id}"
        )
    return combat




def _is_upgrade_or_science_token(token: str) -> bool:
    """Does this CommandButton token name an Upgrade or a Science?

    NOT `startswith("Upgrade_")`. Pure RotWK 2.01 authors four upgrades with no
    underscore after "Upgrade", and all four are the Angmar structure-level
    upgrades that gate Angmar's tier-2/tier-3 units:

        upgrade.ini:  Upgrade UpgradeAngmarBarracksLevel2
                      Upgrade UpgradeAngmarBarracksLevel3
                      Upgrade UpgradeAngmarDenLevel2
                      Upgrade UpgradeAngmarDenLevel3

    (501 other ids DO use the `Upgrade_` form, which is why the underscore
    looked safe to require.) Requiring it discarded those tokens silently, so
    the affected buttons compiled with an EMPTY prerequisite set and the units
    shipped buildable with nothing owned - a gameplay defect with no diagnostic.

    Widening to the `Upgrade` prefix is safe against the fields these call sites
    read: the non-upgrade tokens that appear in `Options` are flags like
    `NEED_UPGRADE`, `CANCELABLE` and `NOT_QUEUEABLE`, none of which start with
    "Upgrade".
    """

    return token.startswith("Upgrade") or token.startswith("SCIENCE_")

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


def _numeric_value(
    token: str, defines: Mapping[str, int | float], label: str
) -> int | float:
    text = token.strip().rstrip("%")
    try:
        return float(text) if "." in text else int(text)
    except ValueError:
        resolved = defines.get(text.casefold())
        if resolved is None:
            raise PlayableStructureCompilerError(
                f"{label} references an unresolved GameData constant: {token}"
            )
        return resolved


def _construct_button_image(button: object) -> str | None:
    """First authored ButtonImage identifier of a commandbutton, or ``None``.

    Retail authors one MappedImage id per construct/wall-upgrade button; an
    absent or explicit ``None`` value stays ``None`` so callers record the
    gap instead of inventing art.
    """

    values = tuple(
        filter(
            None,
            (_first((value,)) for value in _block_values(button, "ButtonImage")),
        )
    )
    if not values or values[0].casefold() == "none":
        return None
    return values[0]


def _construct_routes(
    target_id: str,
    objects: Mapping[str, SageObject],
    command_sets: Mapping[str, object],
    command_buttons: Mapping[str, object],
) -> list[dict[str, object]]:
    construct_commands: dict[str, dict[str, object]] = {}
    for button in command_buttons.values():
        commands = {value.casefold() for value in _block_values(button, "Command")}
        if commands not in _CONSTRUCT_COMMANDS:
            continue
        targets = tuple(
            filter(
                None, (_first((value,)) for value in _block_values(button, "Object"))
            )
        )
        if any(value.casefold() == target_id.casefold() for value in targets):
            construct_commands[button.name.casefold()] = {
                "id": button.name,
                "command": next(iter(commands)),
                "button": button,
            }
    if not construct_commands:
        return []

    set_bindings: list[tuple[object, int, dict[str, object]]] = []
    for command_set in command_sets.values():
        for slot, command_id in _command_slots(command_set):
            command = construct_commands.get(command_id.casefold())
            if command is not None:
                set_bindings.append((command_set, slot, command))

    routes: list[dict[str, object]] = []
    for builder in objects.values():
        try:
            lineage = _ancestry(objects, builder)
        except ValueError:
            continue
        direct_sets = {
            value.casefold()
            for value in (
                _first((row.value,))
                for row in _effective_values(lineage, "CommandSet")
            )
            if value
        }
        for command_set, slot, command in set_bindings:
            if command_set.name.casefold() not in direct_sets:
                continue
            button = command["button"]
            prerequisites = sorted(
                {
                    token
                    for field in ("NeededUpgrade", "Upgrade", "Options")
                    for value in _block_values(button, field)
                    for token in _tokens(value)
                    if _is_upgrade_or_science_token(token)
                },
                key=str.casefold,
            )
            route: dict[str, object] = {
                "surface": "construct",
                "commandId": str(command["id"]),
                "commandKind": str(command["command"]),
                "builderObjectId": builder.name,
                "commandSetId": command_set.name,
                "slot": slot,
                "prerequisites": prerequisites,
            }
            # The construct button's own MappedImage id (BEElvenBarracks,
            # BDDwarvenHall, ...): the HUD's build-strip icon evidence. A
            # button without one keeps the key absent so downstream binding
            # records an explicit gap instead of guessing.
            button_image = _construct_button_image(button)
            if button_image is not None:
                route["buttonImageId"] = button_image
            routes.append(route)
    routes.sort(
        key=lambda row: (
            str(row["builderObjectId"]).casefold(),
            str(row["commandSetId"]).casefold(),
            int(row["slot"]),
            str(row["commandId"]).casefold(),
        )
    )
    return routes


def _wall_upgrade_routes(
    target_id: str,
    objects: Mapping[str, SageObject],
    command_sets: Mapping[str, object],
    command_buttons: Mapping[str, object],
) -> list[dict[str, object]]:
    """Bind OBJECT_UPGRADE buttons which morph a wall hub into the target.

    Retail wall gates, towers, trebuchets, and postern gates are not porter
    constructs: an authored ``OBJECT_UPGRADE`` command on the wall hub's
    CommandSet replaces the hub segment with the target Object.  The target
    evidence is the button's ``Object`` field plus the hub which authors the
    CommandSet holding the button.
    """

    upgrade_commands: dict[str, dict[str, object]] = {}
    for button in command_buttons.values():
        commands = {value.casefold() for value in _block_values(button, "Command")}
        if commands != {_WALL_UPGRADE_COMMAND}:
            continue
        targets = tuple(
            filter(
                None, (_first((value,)) for value in _block_values(button, "Object"))
            )
        )
        if any(value.casefold() == target_id.casefold() for value in targets):
            upgrade_commands[button.name.casefold()] = {
                "id": button.name,
                "button": button,
            }
    if not upgrade_commands:
        return []

    set_bindings: list[tuple[object, int, dict[str, object]]] = []
    for command_set in command_sets.values():
        for slot, command_id in _command_slots(command_set):
            command = upgrade_commands.get(command_id.casefold())
            if command is not None:
                set_bindings.append((command_set, slot, command))

    routes: list[dict[str, object]] = []
    for hub in objects.values():
        try:
            lineage = _ancestry(objects, hub)
        except ValueError:
            continue
        direct_sets = {
            value.casefold()
            for value in (
                _first((row.value,))
                for row in _effective_values(lineage, "CommandSet")
            )
            if value
        }
        for command_set, slot, command in set_bindings:
            if command_set.name.casefold() not in direct_sets:
                continue
            button = command["button"]
            upgrades = sorted(
                {
                    token
                    for value in _block_values(button, "Upgrade")
                    for token in _tokens(value)
                    if _is_upgrade_or_science_token(token)
                },
                key=str.casefold,
            )
            prerequisites = sorted(
                {
                    token
                    for field in ("NeededUpgrade", "Options")
                    for value in _block_values(button, field)
                    for token in _tokens(value)
                    if _is_upgrade_or_science_token(token)
                },
                key=str.casefold,
            )
            route: dict[str, object] = {
                "surface": "wall-upgrade",
                "commandId": str(command["id"]),
                "commandKind": _WALL_UPGRADE_COMMAND,
                "builderObjectId": hub.name,
                "commandSetId": command_set.name,
                "slot": slot,
                "upgrade": upgrades,
                "prerequisites": prerequisites,
            }
            button_image = _construct_button_image(button)
            if button_image is not None:
                route["buttonImageId"] = button_image
            routes.append(route)
    routes.sort(
        key=lambda row: (
            str(row["builderObjectId"]).casefold(),
            str(row["commandSetId"]).casefold(),
            int(row["slot"]),
            str(row["commandId"]).casefold(),
        )
    )
    return routes


def _resolved_scalar_fields(
    scalars: Mapping[str, Mapping[str, object]],
    keys: frozenset[str],
    defines: Mapping[str, int | float],
) -> dict[str, dict[str, object]]:
    """Copy selected scalar rows, resolving numeric GameData constants.

    The authored expression stays untouched; ``value`` is added only when the
    expression is a literal number or a resolvable ``#define``.  Unresolvable
    symbols keep expression-only rows so downstream consumers fail closed on
    the exact field they need instead of on descriptor compilation.
    """

    result: dict[str, dict[str, object]] = {}
    for key, row in sorted(scalars.items()):
        if key not in keys:
            continue
        entry: dict[str, object] = dict(row)
        expression = str(row.get("expression", "")).strip().rstrip("%")
        try:
            entry["value"] = (
                float(expression) if "." in expression else int(expression)
            )
        except ValueError:
            resolved = defines.get(expression.casefold())
            if resolved is not None:
                entry["value"] = resolved
        result[key] = entry
    return result


def _resource_behavior_radius(
    lineage: Sequence[SageObject],
    defines: Mapping[str, int | float],
    target_id: str,
) -> dict[str, object] | None:
    """TerrainResourceBehavior Radius: the resource structure's effectiveness
    ring shown while placing it (farm/mallorn build radius, REF-29/30).
    Structures without the behavior have no ring (fail closed downstream)."""
    for block in _walk_blocks(_effective_top_blocks(lineage)):
        words = block.kind.casefold().split()
        if not words or words[0] != "terrainresourcebehavior":
            continue
        token = _first(block.values("Radius"))
        if token is None:
            continue
        return {
            "radius": {
                "authored": token,
                "value": _numeric_value(
                    token, defines, f"{target_id} {block.kind} Radius"
                ),
            },
            "module": block.kind,
            "sourceIni": block.source_virtual_path,
            "line": block.line,
        }
    return None


def _health_contract(
    lineage: Sequence[SageObject],
    defines: Mapping[str, int | float],
    target_id: str,
) -> dict[str, object] | None:
    bodies: list[dict[str, object]] = []
    for block in _walk_blocks(_effective_top_blocks(lineage)):
        words = block.kind.casefold().split()
        if not words or "body" not in words[0]:
            continue
        fields: dict[str, object] = {}
        for field in _HEALTH_FIELDS:
            token = _first(block.values(field))
            if token is None:
                continue
            fields[field[0].lower() + field[1:]] = {
                "authored": token,
                "value": _numeric_value(
                    token, defines, f"{target_id} {block.kind} {field}"
                ),
            }
        if fields:
            bodies.append(
                {
                    "module": block.kind,
                    "sourceIni": block.source_virtual_path,
                    "line": block.line,
                    **fields,
                }
            )
    if not bodies:
        return None
    primary = bodies[0]
    if "maxHealth" not in primary:
        raise PlayableStructureCompilerError(
            f"structure body does not author MaxHealth: {target_id}"
        )
    contract: dict[str, object] = {"primary": primary, "evidence": bodies}
    if str(primary.get("module", "")).casefold() == "highlanderbody":
        contract["highlanderBody"] = {
            "value": True,
            "sourceIni": str(primary.get("sourceIni", "")),
            "line": int(primary.get("line", 0)),
        }
    return contract


def _grant_upgrade_create_contract(
    lineage: Sequence[SageObject],
    documents: Mapping[str, bytes],
    target_id: str,
) -> list[dict[str, object]]:
    """Flatten supported GrantUpgradeCreate rows into exact lifecycle grants.

    BFME's foundation objects explicitly use ``GiveOnBuildComplete = Yes``.
    The earlier EA implementation also supports the
    ``ExemptStatus = UNDER_CONSTRUCTION`` create-time path.  Other status
    masks and false/implicit build-complete policy remain unsupported rather
    than being assigned guessed timing.
    """

    modules = [
        block
        for block in _walk_blocks(_effective_top_blocks(lineage))
        if block.kind.casefold() == "grantupgradecreate"
    ]
    if not modules:
        return []
    upgrade_source = _optional_document(documents, UPGRADE_PATH)
    if upgrade_source is None:
        raise PlayableStructureCompilerError(
            f"{target_id} authors GrantUpgradeCreate but {UPGRADE_PATH} is "
            "not in the effective INI view"
        )
    upgrade_blocks = _named_blocks(upgrade_source, "Upgrade")
    rows: list[dict[str, object]] = []
    seen: set[tuple[str, bool, bool]] = set()
    for block in modules:
        upgrade_id = _first(block.values("UpgradeToGrant"))
        if upgrade_id is None or not upgrade_id.strip():
            raise PlayableStructureCompilerError(
                f"{target_id} {block.kind} omits UpgradeToGrant"
            )
        upgrade_id = upgrade_id.strip()
        upgrade_block = upgrade_blocks.get(upgrade_id.casefold())
        if upgrade_block is None:
            raise PlayableStructureCompilerError(
                f"{target_id} {block.kind} references missing upgrade "
                f"{upgrade_id}"
            )
        upgrade_type = _first(upgrade_block.values("Type"))
        if upgrade_type is None or upgrade_type.strip().casefold() not in {
            "object",
            "player",
        }:
            raise PlayableStructureCompilerError(
                f"{target_id} {block.kind} upgrade {upgrade_id} has unsupported "
                "or missing Type"
            )
        exempt_tokens = {
            token.casefold()
            for value in block.values("ExemptStatus")
            for token in _tokens(value)
            if token.casefold() not in {"none", "null"}
        }
        if exempt_tokens - {"under_construction"}:
            raise PlayableStructureCompilerError(
                f"{target_id} {block.kind} has unsupported ExemptStatus "
                f"{sorted(exempt_tokens)}"
            )
        give_values = [
            value.strip().casefold()
            for value in block.values("GiveOnBuildComplete")
            if value.strip()
        ]
        if len(set(give_values)) > 1 or any(
            value not in {"yes", "true", "1", "no", "false", "0"}
            for value in give_values
        ):
            raise PlayableStructureCompilerError(
                f"{target_id} {block.kind} has invalid GiveOnBuildComplete"
            )
        on_build_complete = bool(give_values) and give_values[-1] in {
            "yes",
            "true",
            "1",
        }
        on_create_when_complete = "under_construction" in exempt_tokens
        if not on_build_complete and not on_create_when_complete:
            raise PlayableStructureCompilerError(
                f"{target_id} {block.kind} has no source-backed grant timing"
            )
        identity = (
            upgrade_id.casefold(),
            on_create_when_complete,
            on_build_complete,
        )
        if identity in seen:
            continue
        seen.add(identity)
        rows.append(
            {
                "upgradeId": upgrade_id,
                "upgradeType": upgrade_type.strip().upper(),
                "onCreateWhenComplete": on_create_when_complete,
                "onBuildComplete": on_build_complete,
                "module": "GrantUpgradeCreate",
                "sourceIni": block.source_virtual_path,
                "line": block.line,
            }
        )
    rows.sort(
        key=lambda row: (
            str(row["upgradeId"]).casefold(),
            str(row["sourceIni"]).casefold(),
            int(row["line"]),
        )
    )
    return rows


def _inherit_upgrade_create_contract(
    lineage: Sequence[SageObject],
    documents: Mapping[str, bytes],
    defines: Mapping[str, int | float],
    target_id: str,
) -> list[dict[str, object]]:
    """Compile retail's creation-time nearby-source upgrade inheritance.

    Every BFME2/RotWK declaration in the retail census authors the same closed
    filter shape, ``ANY +ObjectType``.  Preserve that exact positive source
    identity instead of widening it to KindOf or substring matching.
    """

    modules = [
        block
        for block in _walk_blocks(_effective_top_blocks(lineage))
        if block.kind.casefold() == "inheritupgradecreate"
    ]
    if not modules:
        return []
    upgrade_source = _optional_document(documents, UPGRADE_PATH)
    if upgrade_source is None:
        raise PlayableStructureCompilerError(
            f"{target_id} authors InheritUpgradeCreate but {UPGRADE_PATH} is "
            "not in the effective INI view"
        )
    upgrade_blocks = _named_blocks(upgrade_source, "Upgrade")
    rows: list[dict[str, object]] = []
    seen: set[tuple[str, float, str]] = set()
    for block in modules:
        authored_keys = [item.key.casefold() for item in block.assignments]
        expected_keys = {"radius", "upgrade", "objectfilter"}
        if (
            set(authored_keys) != expected_keys
            or any(authored_keys.count(key) != 1 for key in expected_keys)
        ):
            raise PlayableStructureCompilerError(
                f"{target_id} {block.kind} must author exactly one Radius, "
                "Upgrade, and ObjectFilter assignment"
            )
        radius_token = _first(block.values("Radius"))
        upgrade_id = _first(block.values("Upgrade"))
        filter_value = _first_raw(block, "ObjectFilter")
        if radius_token is None:
            raise PlayableStructureCompilerError(
                f"{target_id} {block.kind} omits Radius"
            )
        radius = _numeric_value(
            radius_token, defines, f"{target_id} {block.kind} Radius"
        )
        if radius <= 0.0:
            raise PlayableStructureCompilerError(
                f"{target_id} {block.kind} has non-positive Radius"
            )
        if upgrade_id is None or upgrade_id.casefold() not in upgrade_blocks:
            raise PlayableStructureCompilerError(
                f"{target_id} {block.kind} references missing Upgrade "
                f"{upgrade_id or ''}"
            )
        upgrade_type = _first(
            upgrade_blocks[upgrade_id.casefold()].values("Type")
        )
        if upgrade_type is None or upgrade_type.casefold() != "object":
            raise PlayableStructureCompilerError(
                f"{target_id} {block.kind} upgrade {upgrade_id} is not "
                "Type = OBJECT"
            )
        filter_tokens = _tokens(filter_value or "")
        if (
            len(filter_tokens) != 2
            or filter_tokens[0].casefold() != "any"
            or not filter_tokens[1].startswith("+")
            or len(filter_tokens[1]) == 1
        ):
            raise PlayableStructureCompilerError(
                f"{target_id} {block.kind} has unsupported ObjectFilter "
                f"{filter_value or ''!r}"
            )
        source_object_id = filter_tokens[1][1:]
        identity = (upgrade_id.casefold(), float(radius), source_object_id.casefold())
        if identity in seen:
            continue
        seen.add(identity)
        rows.append(
            {
                "radius": {"authored": radius_token, "value": radius},
                "upgradeId": upgrade_id,
                "upgradeType": "OBJECT",
                "objectFilter": filter_value,
                "sourceObjectId": source_object_id,
                "module": "InheritUpgradeCreate",
                "sourceIni": block.source_virtual_path,
                "line": block.line,
            }
        )
    rows.sort(
        key=lambda row: (
            str(row["upgradeId"]).casefold(),
            str(row["sourceObjectId"]).casefold(),
            float(row["radius"]["value"]),
            str(row["sourceIni"]).casefold(),
            int(row["line"]),
        )
    )
    return rows


_QUEUE_EXIT_SUPPORTED_FIELDS = frozenset(
    {
        "unitcreatepoint",
        "naturalrallypoint",
        "exitdelay",
        "allowairbornecreation",
        "initialburst",
    }
)
_QUEUE_EXIT_DEFERRED_FIELDS = frozenset(
    {
        # BFME accepts these inside QueueProductionExitUpdate, but the local
        # Generals module-data oracle does not define their behavior.
        "placementviewangle",
        "usereturntoformation",
        "noexitpath",
    }
)


def _effective_block_assignment(
    block: SageBlock, key: str
) -> SageAssignment | None:
    """Return the last assignment for one case-insensitive module field.

    SAGE's INI field parser writes each occurrence into the same module-data
    slot in source order.  Repeated scalar fields therefore use the final
    assignment; rejecting duplicates or taking the first silently changes the
    effective module data.
    """

    folded = key.casefold()
    rows = tuple(
        row for row in block.assignments if row.key.casefold() == folded
    )
    return rows[-1] if rows else None


_QUEUE_EXIT_COORD_PATTERN = re.compile(
    r"(?i)^\s*X\s*:\s*"
    r"([+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?)\s+"
    r"Y\s*:\s*"
    r"([+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?)\s+"
    r"Z\s*:\s*"
    r"([+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?)\s*$"
)


def _queue_exit_coord_values(
    authored: str,
    label: str,
) -> dict[str, float]:
    match = _QUEUE_EXIT_COORD_PATTERN.fullmatch(authored)
    if match is None:
        # Known RotWK typo: AngmarKennelExpansion authors ``X:70.0.0`` (extra
        # ``.0``) instead of ``X:70.0``. Accept a single extra trailing ``.0``
        # on any axis so the structure still compiles from retail bytes.
        repaired = re.sub(
            r"(?i)(X|Y|Z)\s*:\s*([+-]?(?:\d+\.\d+|\d+|\.\d+))\.0(?=\s|$)",
            r"\1:\2",
            authored.strip(),
        )
        match = _QUEUE_EXIT_COORD_PATTERN.fullmatch(repaired)
    if match is None:
        raise PlayableStructureCompilerError(
            f"{label} is not an exact X/Y/Z Coord3D: {authored!r}"
        )
    x_token, y_token, z_token = match.groups()
    return {
        "x": float(x_token),
        "y": float(y_token),
        "z": float(z_token),
    }


def _queue_exit_coord(
    assignment: SageAssignment | None,
    label: str,
) -> dict[str, object]:
    if assignment is None:
        return {
            "authored": "",
            "value": {"x": 0.0, "y": 0.0, "z": 0.0},
            "defaulted": True,
        }
    values = _queue_exit_coord_values(assignment.value, label)
    return {
        "authored": assignment.value,
        "value": values,
        "sourceIni": assignment.source_virtual_path,
        "line": assignment.line,
    }


def _queue_exit_number(
    assignment: SageAssignment | None,
    defines: Mapping[str, int | float],
    label: str,
    *,
    integral: bool,
) -> dict[str, object]:
    if assignment is None:
        return {"authored": "0", "value": 0, "defaulted": True}
    authored = assignment.value.strip()
    resolved_define: dict[str, object] | None = None
    if re.fullmatch(r"[0-9]+", authored):
        value: int | float = int(authored)
    elif re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", authored):
        resolved = defines.get(authored.casefold())
        if resolved is None:
            raise PlayableStructureCompilerError(
                f"{label} references an unresolved GameData constant: "
                f"{assignment.value}"
            )
        value = resolved
        resolved_define = {"name": authored, "value": resolved}
    else:
        raise PlayableStructureCompilerError(
            f"{label} must be an exact unsigned decimal or resolved "
            f"GameData constant: {assignment.value!r}"
        )
    if (
        float(value) < 0.0
        or (integral and float(value) != int(value))
        or (integral and int(value) > 4_294_967_295)
    ):
        raise PlayableStructureCompilerError(
            f"{label} must be an UnsignedInt in range 0..4294967295"
        )
    effective: int | float = int(value) if integral else value
    result: dict[str, object] = {
        "authored": assignment.value,
        "value": effective,
        "sourceIni": assignment.source_virtual_path,
        "line": assignment.line,
    }
    if resolved_define is not None:
        resolved_define["value"] = effective
        result["resolvedDefine"] = resolved_define
    return result


def _queue_exit_bool(
    assignment: SageAssignment | None,
    label: str,
) -> dict[str, object]:
    if assignment is None:
        return {"authored": "No", "value": False, "defaulted": True}
    folded = assignment.value.strip().casefold()
    if folded not in {"yes", "no"}:
        raise PlayableStructureCompilerError(
            f"{label} must be Yes or No: {assignment.value!r}"
        )
    return {
        "authored": assignment.value,
        "value": folded == "yes",
        "sourceIni": assignment.source_virtual_path,
        "line": assignment.line,
    }


def _queue_production_exit_contract(
    lineage: Sequence[SageObject],
    defines: Mapping[str, int | float],
    target_id: str,
) -> list[dict[str, object]]:
    """Compile QueueProductionExitUpdate without claiming live simulation.

    The local Generals GPL module-data and implementation source the five
    supported fields.  BFME-only fields are retained as explicit deferred
    assignments; an unknown field fails closed.
    """

    modules = [
        block
        for block in _walk_blocks(_effective_top_blocks(lineage))
        if block.kind.casefold() == "queueproductionexitupdate"
    ]
    rows: list[dict[str, object]] = []
    for block in modules:
        authored_keys = {row.key.casefold() for row in block.assignments}
        unknown = authored_keys - (
            _QUEUE_EXIT_SUPPORTED_FIELDS | _QUEUE_EXIT_DEFERRED_FIELDS
        )
        if unknown:
            raise PlayableStructureCompilerError(
                f"{target_id} {block.kind} has unsupported fields: "
                + ", ".join(sorted(unknown))
            )
        deferred_fields: list[dict[str, object]] = []
        for field_name in sorted(_QUEUE_EXIT_DEFERRED_FIELDS):
            assignment = _effective_block_assignment(block, field_name)
            if assignment is None:
                continue
            deferred_fields.append(
                {
                    "name": assignment.key,
                    "authored": assignment.value,
                    "sourceIni": assignment.source_virtual_path,
                    "line": assignment.line,
                    "reason": "bfme-field-without-local-runtime-oracle",
                }
            )
        rows.append(
            {
                "module": "QueueProductionExitUpdate",
                "unitCreatePoint": _queue_exit_coord(
                    _effective_block_assignment(block, "UnitCreatePoint"),
                    f"{target_id} {block.kind} UnitCreatePoint",
                ),
                "naturalRallyPoint": _queue_exit_coord(
                    _effective_block_assignment(block, "NaturalRallyPoint"),
                    f"{target_id} {block.kind} NaturalRallyPoint",
                ),
                "exitDelay": {
                    **_queue_exit_number(
                        _effective_block_assignment(block, "ExitDelay"),
                        defines,
                        f"{target_id} {block.kind} ExitDelay",
                        integral=True,
                    ),
                    # INI::parseDurationUnsignedInt parses the authored duration;
                    # preserve that millisecond unit. A frame conversion needs a
                    # BFME tick oracle and is intentionally not invented here.
                    "unit": "milliseconds",
                },
                "allowAirborneCreation": _queue_exit_bool(
                    _effective_block_assignment(
                        block, "AllowAirborneCreation"
                    ),
                    f"{target_id} {block.kind} AllowAirborneCreation",
                ),
                "initialBurst": _queue_exit_number(
                    _effective_block_assignment(block, "InitialBurst"),
                    defines,
                    f"{target_id} {block.kind} InitialBurst",
                    integral=True,
                ),
                "deferredFields": deferred_fields,
                "runtimeStatus": "deferred",
                "sourceIni": block.source_virtual_path,
                "line": block.line,
            }
        )
    rows.sort(
        key=lambda row: (
            str(row["sourceIni"]).casefold(),
            int(row["line"]),
        )
    )
    return rows


_AUTO_DEPOSIT_SUPPORTED_FIELDS = frozenset(
    {
        "deposittiming",
        "depositamount",
        "initialcapturebonus",
        "actualmoney",
        "upgradedboost",
    }
)
_AUTO_DEPOSIT_DEFERRED_FIELDS = frozenset(
    {
        # BFME2 additions not present in the Generals module-data/implementation
        # oracle. Both change eligibility/score semantics, so they cannot be
        # silently ignored by the runtime.
        "givenoxp",
        "onlywhengarrisoned",
    }
)
_AUTO_DEPOSIT_UPGRADE_PAIR_PATTERN = re.compile(
    r"(?i)^\s*UpgradeType\s*:\s*([A-Za-z_][A-Za-z0-9_]*)\s+"
    r"Boost\s*:\s*([+-]?[0-9]+)\s*$"
)


def _auto_deposit_integer(
    assignment: SageAssignment | None,
    defines: Mapping[str, int | float],
    label: str,
    *,
    default: int,
    unsigned: bool = False,
) -> dict[str, object]:
    if assignment is None:
        return {"authored": str(default), "value": default, "defaulted": True}
    authored = assignment.value.strip()
    resolved_define: dict[str, object] | None = None
    if re.fullmatch(r"[+-]?[0-9]+", authored):
        value = int(authored)
    elif re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", authored):
        resolved = defines.get(authored.casefold())
        if (
            resolved is None
            or isinstance(resolved, bool)
            or float(resolved) != int(resolved)
        ):
            raise PlayableStructureCompilerError(
                f"{label} references a missing or non-integral GameData "
                f"constant: {assignment.value}"
            )
        value = int(resolved)
        resolved_define = {"name": authored, "value": value}
    else:
        raise PlayableStructureCompilerError(
            f"{label} must be an exact integer or resolved GameData constant: "
            f"{assignment.value!r}"
        )
    if unsigned and not 0 <= value <= 4_294_967_295:
        raise PlayableStructureCompilerError(
            f"{label} must be an UnsignedInt in range 0..4294967295"
        )
    if not unsigned and not -2_147_483_648 <= value <= 2_147_483_647:
        raise PlayableStructureCompilerError(
            f"{label} must be an Int in range -2147483648..2147483647"
        )
    result: dict[str, object] = {
        "authored": assignment.value,
        "value": value,
        "sourceIni": assignment.source_virtual_path,
        "line": assignment.line,
    }
    if resolved_define is not None:
        result["resolvedDefine"] = resolved_define
    return result


def _auto_deposit_contract(
    lineage: Sequence[SageObject],
    documents: Mapping[str, bytes],
    defines: Mapping[str, int | float],
    target_id: str,
) -> list[dict[str, object]]:
    """Compile the source-backed AutoDepositUpdate module-data contract.

    ``parseDurationUnsignedInt`` consumes milliseconds and rounds upward to
    engine frames. The local authoritative simulation advances at 100 ms per
    tick, so its deterministic duration projection is ceil(ms / 100).
    """

    modules = [
        block
        for block in _walk_blocks(_effective_top_blocks(lineage))
        if block.kind.casefold() == "autodepositupdate"
    ]
    if not modules:
        return []
    upgrade_blocks: Mapping[str, object] = {}
    if any(
        assignment.key.casefold() == "upgradedboost"
        for block in modules
        for assignment in block.assignments
    ):
        upgrade_source = _optional_document(documents, UPGRADE_PATH)
        if upgrade_source is None:
            raise PlayableStructureCompilerError(
                f"{target_id} authors AutoDepositUpdate UpgradedBoost but "
                f"{UPGRADE_PATH} is not in the effective INI view"
            )
        upgrade_blocks = _named_blocks(upgrade_source, "Upgrade")
    rows: list[dict[str, object]] = []
    for block in modules:
        authored_keys = {assignment.key.casefold() for assignment in block.assignments}
        unknown = authored_keys - (
            _AUTO_DEPOSIT_SUPPORTED_FIELDS | _AUTO_DEPOSIT_DEFERRED_FIELDS
        )
        if unknown:
            raise PlayableStructureCompilerError(
                f"{target_id} {block.kind} has unsupported fields: "
                + ", ".join(sorted(unknown))
            )
        timing = _auto_deposit_integer(
            _effective_block_assignment(block, "DepositTiming"),
            defines,
            f"{target_id} {block.kind} DepositTiming",
            default=0,
            unsigned=True,
        )
        timing["unit"] = "milliseconds"
        timing["simulationTicks"] = (
            (int(timing["value"]) + 99) // 100
            if int(timing["value"]) > 0
            else 0
        )
        actual_money = _queue_exit_bool(
            _effective_block_assignment(block, "ActualMoney"),
            f"{target_id} {block.kind} ActualMoney",
        )
        if actual_money.get("defaulted") is True:
            actual_money = {
                "authored": "Yes",
                "value": True,
                "defaulted": True,
            }
        upgrade_boosts: list[dict[str, object]] = []
        for assignment in block.assignments:
            if assignment.key.casefold() != "upgradedboost":
                continue
            match = _AUTO_DEPOSIT_UPGRADE_PAIR_PATTERN.fullmatch(assignment.value)
            if match is None:
                raise PlayableStructureCompilerError(
                    f"{target_id} {block.kind} UpgradedBoost is not an exact "
                    f"UpgradeType/Boost pair: {assignment.value!r}"
                )
            upgrade_id, boost_token = match.groups()
            if upgrade_id.casefold() not in upgrade_blocks:
                raise PlayableStructureCompilerError(
                    f"{target_id} {block.kind} UpgradedBoost references missing "
                    f"Upgrade {upgrade_id}"
                )
            upgrade_type = _first(
                upgrade_blocks[upgrade_id.casefold()].values("Type")
            )
            if (
                upgrade_type is None
                or upgrade_type.strip().casefold() != "player"
            ):
                raise PlayableStructureCompilerError(
                    f"{target_id} {block.kind} UpgradedBoost upgrade "
                    f"{upgrade_id} must have source-attested Type PLAYER"
                )
            upgrade_boosts.append(
                {
                    "upgradeId": upgrade_id,
                    # AutoDepositUpdate queries the controlling player's
                    # completed-upgrade set. OBJECT upgrades are not a
                    # compatible authority.
                    "upgradeType": "PLAYER",
                    "upgradeAttestation": {
                        "upgradeId": upgrade_id,
                        # Parsed from the named block in upgrade.ini and bound
                        # to the pack source-document receipt below.
                        "upgradeType": "PLAYER",
                        "sourceIni": UPGRADE_PATH,
                        "sourceSha256": hashlib.sha256(upgrade_source).hexdigest(),
                    },
                    "boost": int(boost_token),
                    "authored": assignment.value,
                    "sourceIni": assignment.source_virtual_path,
                    "line": assignment.line,
                }
            )
        deferred_fields: list[dict[str, object]] = []
        for field_name in sorted(_AUTO_DEPOSIT_DEFERRED_FIELDS):
            assignment = _effective_block_assignment(block, field_name)
            if assignment is not None:
                deferred_fields.append(
                    {
                        "name": assignment.key,
                        "authored": assignment.value,
                        "sourceIni": assignment.source_virtual_path,
                        "line": assignment.line,
                        "reason": "bfme-field-without-local-runtime-oracle",
                    }
                )
        rows.append(
            {
                "module": "AutoDepositUpdate",
                "depositTiming": timing,
                "depositAmount": _auto_deposit_integer(
                    _effective_block_assignment(block, "DepositAmount"),
                    defines,
                    f"{target_id} {block.kind} DepositAmount",
                    default=0,
                ),
                "initialCaptureBonus": _auto_deposit_integer(
                    _effective_block_assignment(block, "InitialCaptureBonus"),
                    defines,
                    f"{target_id} {block.kind} InitialCaptureBonus",
                    default=0,
                ),
                "actualMoney": actual_money,
                "upgradedBoosts": upgrade_boosts,
                "deferredFields": deferred_fields,
                "runtimeStatus": (
                    "executable" if not deferred_fields else "deferred"
                ),
                "sourceIni": block.source_virtual_path,
                "line": block.line,
            }
        )
    rows.sort(key=lambda row: (str(row["sourceIni"]).casefold(), int(row["line"])))
    return rows


def _trained_command_sets(
    lineage: Sequence[SageObject],
    command_sets: Mapping[str, object],
    target_id: str,
    source_null_command_sets: frozenset[str] = frozenset(),
) -> tuple[list[dict[str, object]], list[str]]:
    result: list[dict[str, object]] = []
    source_null: set[str] = set()
    seen: set[tuple[str, str]] = set()
    direct = [
        value
        for value in (
            _first((row.value,)) for row in _effective_values(lineage, "CommandSet")
        )
        if value
    ]
    upgraded: list[tuple[str, list[str]]] = []
    for block in _walk_blocks(_effective_top_blocks(lineage)):
        set_id = _first(block.values("CommandSet"))
        if not set_id or "commandsetupgrade" not in block.kind.casefold():
            continue
        triggers = sorted(
            {
                token
                for value in block.values("TriggeredBy")
                for token in _tokens(value)
                if token.casefold() not in {"none", "null"}
            },
            key=str.casefold,
        )
        upgraded.append((set_id, triggers))
    for kind, entries in (
        ("direct", [(value, []) for value in direct]),
        ("upgraded", upgraded),
    ):
        for set_id, triggers in entries:
            key = (kind, set_id.casefold())
            if key in seen:
                continue
            seen.add(key)
            command_set = command_sets.get(set_id.casefold())
            if command_set is None:
                if set_id.casefold() in source_null_command_sets:
                    # Caller-declared retail source-null reference (for
                    # example the Isengard side-pad CommandSet): recorded
                    # explicitly, never silently dropped or invented.
                    source_null.add(set_id)
                    continue
                raise PlayableStructureCompilerError(
                    f"structure references a missing CommandSet: {set_id}"
                )
            row: dict[str, object] = {
                "id": command_set.name,
                "kind": kind,
                "slots": [
                    {"slot": slot, "commandId": command_id}
                    for slot, command_id in _command_slots(command_set)
                ],
            }
            if triggers:
                row["triggeredBy"] = triggers
            result.append(row)
    result.sort(key=lambda row: (str(row["kind"]), str(row["id"]).casefold()))
    return result, sorted(source_null, key=str.casefold)


def _module_evidence(
    lineage: Sequence[SageObject],
) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for block in _walk_blocks(_effective_top_blocks(lineage)):
        rows.append(
            {
                "module": block.kind,
                "sourceIni": block.source_virtual_path,
                "line": block.line,
            }
        )
    rows.sort(
        key=lambda row: (
            str(row["sourceIni"]).casefold(),
            int(row["line"]),
            str(row["module"]).casefold(),
        )
    )
    return rows


# ---------------------------------------------------------------------------
# Purchased structure level chain.
#
# Retail levels production structures through authored OBJECT_UPGRADE
# purchases: a LevelUpUpgrade Behavior binds each Upgrade_* trigger to the
# levels it grants, a CommandSetUpgrade Behavior swaps the building's command
# set, the upgrade.ini block authors cost and build time, the purchase
# CommandButton occupies a slot of the command set that is active before the
# purchase, and the structure's ExperienceLevel chain authors the per-level
# stat effects (health additions, PRODUCTION build-speed factors).  The chain
# is walked deterministically: the direct command set sells the first step,
# each step's CommandSetUpgrade target sells the next.  Any break in that
# linked evidence fails closed — a malformed chain is never approximated.
# ---------------------------------------------------------------------------

_OBJECT_UPGRADE_COMMAND = "object_upgrade"


def _level_module_rows(
    lineage: Sequence[SageObject],
) -> tuple[
    list[tuple[tuple[str, ...], str, bool]],
    list[tuple[tuple[str, ...], tuple[str, ...], tuple[str, ...], SageBlock]],
]:
    """Harvest CommandSetUpgrade transitions and SubObjectsUpgrade rows.

    Command-set transitions resolve lazily at chain-walk time: retail also
    authors multi-trigger (RequiresAllTriggers) and ConflictsWith pairs on
    wall/turret objects, which never serve a level chain and must not fail
    it. A step requires exactly one single-trigger transition target.
    SubObjectsUpgrade modules author the per-level model variants: which
    named sub-objects of the building's model show or hide at each level.
    They bind to the same upgrade ids the level chain walks plus the
    engine-granted StructureLevel<N> base states.
    """

    transition_rows: list[tuple[tuple[str, ...], str, bool]] = []
    subobject_rows: list[tuple[tuple[str, ...], tuple[str, ...], tuple[str, ...], SageBlock]] = []
    for block in _walk_blocks(_effective_top_blocks(lineage)):
        block_kind = block.kind.casefold()
        if block_kind == "commandsetupgrade":
            set_id = _first(block.values("CommandSet"))
            triggers = tuple(
                token
                for value in block.values("TriggeredBy")
                for token in _tokens(value)
                if token.casefold() not in {"none", "null"}
            )
            if not set_id or not triggers:
                continue
            requires_all = any(
                value.strip().casefold() in {"yes", "true", "1"}
                for value in block.values("RequiresAllTriggers")
            )
            transition_rows.append((triggers, set_id, requires_all))
        elif block_kind == "subobjectsupgrade":
            triggers = tuple(
                token
                for value in block.values("TriggeredBy")
                for token in _tokens(value)
                if token.casefold() not in {"none", "null"}
            )
            # Sub-object tokens keep their authored form verbatim, including
            # the SAGE prefix wildcard (V1_PIECE*) the model matcher honors.
            shows = tuple(
                token
                for value in block.values("ShowSubObjects")
                for token in value.split()
                if token.casefold() not in {"none", "null"}
            )
            hides = tuple(
                token
                for value in block.values("HideSubObjects")
                for token in value.split()
                if token.casefold() not in {"none", "null"}
            )
            if triggers:
                subobject_rows.append((triggers, shows, hides, block))
    return transition_rows, subobject_rows


_STRUCTURE_LEVEL_TRIGGER = re.compile(r"upgrade_structurelevel([0-9]+)\Z")


def _structure_level_presentation(
    target_id: str,
    lineage: Sequence[SageObject],
) -> dict[str, object] | None:
    """Compile presentation-only staged levels for structures without a
    purchase chain.

    Economy structures (the Gondor farm through FarmInterface, and every
    faction's auto-leveling resource building) author SubObjectsUpgrade rows
    bound to the engine-granted Upgrade_StructureLevel<N> ids instead of a
    purchasable LevelUpUpgrade chain. Retail grants those levels itself; the
    rows still author exactly which sub-objects show or hide per level, so
    they compile here as presentation data — never as purchase contracts and
    never as invented visibility. Visibility resolves cumulatively per level
    in ascending StructureLevel order, mirroring the chain compiler.
    """

    _transition_rows, subobject_rows = _level_module_rows(lineage)
    staged: dict[int, list[tuple[tuple[str, ...], tuple[str, ...], SageBlock]]] = {}
    for triggers, shows, hides, block in subobject_rows:
        levels = sorted(
            {
                int(match.group(1))
                for token in triggers
                if (match := _STRUCTURE_LEVEL_TRIGGER.fullmatch(token.casefold()))
                is not None
            }
        )
        for level in levels:
            if level < 1:
                continue
            staged.setdefault(level, []).append((shows, hides, block))
    if not staged:
        return None
    visibility: dict[str, bool] = {}
    levels: dict[str, object] = {}
    trigger_upgrades: list[str] = []
    source_blocks: list[SageBlock] = []
    for level in sorted(staged):
        rows = staged[level]
        trigger_upgrades.append(f"Upgrade_StructureLevel{level}")
        for shows, hides, block in rows:
            source_blocks.append(block)
            for token in shows:
                visibility[token] = True
            for token in hides:
                visibility[token] = False
        levels[str(level)] = {
            "visibleSubObjects": sorted(
                (token for token, shown in visibility.items() if shown),
                key=str.casefold,
            ),
            "hiddenSubObjects": sorted(
                (token for token, shown in visibility.items() if not shown),
                key=str.casefold,
            ),
            "subObjects": [
                {
                    "show": list(shows),
                    "hide": list(hides),
                    "sourceIni": block.source_virtual_path,
                    "line": int(block.line),
                }
                for shows, hides, block in rows
            ],
        }
    return {
        "levels": levels,
        "triggerUpgrades": trigger_upgrades,
        "sourceIni": sorted(
            {block.source_virtual_path for block in source_blocks},
            key=str.casefold,
        ),
    }


# ---------------------------------------------------------------------------
# Player-upgrade research surface and per-structure upgrade effects.
#
# Retail sells PLAYER technologies on structure command sets: the marketplace
# offers GrandHarvest/Defiance/IronOre, the forge offers the Gondor weapon
# technologies, the barracks offers Basic Training, the archery range offers
# Fire Arrows.  Each sale is a PLAYER_UPGRADE command button bound to a
# PLAYER Upgrade.ini block (cost/build time), optionally gated by a
# NeededUpgrade row.  Separately, structures author effect modules bound to
# those technologies (the marketplace's CostModifierUpgrade discount, the
# RefundDie rows, the farm's TerrainResourceBehavior income bonus).  Both
# compile here verbatim; effect module kinds the runtime cannot apply are
# recorded as declared unsupported capabilities, never dropped silently.
# ---------------------------------------------------------------------------

_PLAYER_UPGRADE_COMMAND = "player_upgrade"
_SUPPORTED_UPGRADE_EFFECT_KINDS = {
    "costmodifierupgrade",
    "refunddie",
    "terrainresourcebehavior",
}


def _first_raw(block: SageBlock, key: str) -> str | None:
    """First non-null raw assignment text, percent suffixes intact."""

    for value in block.values(key):
        text = value.strip()
        if text and text.casefold() not in {"none", "null"}:
            return text
    return None


def _percent_value(
    raw: str,
    defines: Mapping[str, int | float],
    context: str,
) -> float:
    """Resolve an authored percent row ('-25%', '50%', 'DEFINE_NAME %')."""

    match = re.fullmatch(r"(.+?)\s*%\s*", raw.strip())
    if match is None:
        raise PlayableStructureCompilerError(f"{context} is not a percent value: {raw!r}")
    return float(_numeric_value(match.group(1), defines, context))


def _purchase_button_fields(
    button: IniBlock,
    *,
    include_needed_upgrade_any: bool,
) -> dict[str, object]:
    """Project the shared authored purchase-button presentation fields."""

    needed = [
        token
        for value in button.values("NeededUpgrade")
        for token in _tokens(value)
        if token.casefold() not in {"none", "null"}
    ]
    options = {
        token.casefold()
        for value in button.values("Options")
        for token in _tokens(value)
    }
    fields: dict[str, object] = {
        "cancelable": "cancelable" in options,
    }
    if needed:
        fields["neededUpgradeIds"] = needed
    needed_any_values = tuple(button.values("NeededUpgradeAny"))
    if include_needed_upgrade_any:
        fields["neededUpgradeAny"] = any(
            value.strip().casefold() in {"yes", "true", "1"}
            for value in needed_any_values
        )
    elif needed_any_values:
        fields["neededUpgradeAny"] = any(
            value.strip().casefold() in {"yes", "true", "1"}
            for value in needed_any_values
        )
    for field, output_key in (
        ("TextLabel", "labelId"),
        ("DescriptLabel", "tooltipId"),
        ("ButtonImage", "buttonImageId"),
        ("LacksPrerequisiteLabel", "lacksPrerequisiteLabelId"),
    ):
        for value in button.values(field):
            text = value.strip()
            if text and text.casefold() not in {"none", "null"}:
                fields[output_key] = text
                break
    return fields


def _authored_presentation(button: IniBlock) -> dict[str, object]:
    """The button's authored label/tooltip/icon ids, and nothing else."""

    fields: dict[str, object] = {}
    for field, output_key in (
        ("TextLabel", "labelId"),
        ("DescriptLabel", "tooltipId"),
        ("ButtonImage", "buttonImageId"),
    ):
        for value in button.values(field):
            text = value.strip()
            if text and text.casefold() not in {"none", "null"}:
                fields[output_key] = text
                break
    return fields


def _castle_upgrade_surface(
    target_id: str,
    lineage: Sequence[SageObject],
    trained: Sequence[Mapping[str, object]],
    documents: Mapping[str, bytes],
    command_buttons: Mapping[str, object],
    defines: Mapping[str, int | float],
) -> dict[str, object] | None:
    """Compile the fortress trigger-purchase surface from authored evidence."""

    label = f"structure {target_id}"
    try:
        behavior_rows = harvest_castle_upgrade_behaviors(lineage)
    except CastleBehaviorCompilerError as error:
        raise PlayableStructureCompilerError(str(error)) from error
    if not behavior_rows:
        return None

    behavior_by_trigger = {
        trigger.casefold(): (trigger, grant, radius)
        for trigger, (grant, radius) in behavior_rows.items()
    }
    rows: list[dict[str, object]] = []
    non_purchasable: list[dict[str, object]] = []
    sold_triggers: set[str] = set()
    command_surface_seen = False
    page_selectors: dict[str, dict[str, object]] = {}
    for trained_row in trained:
        set_id = str(trained_row.get("id", ""))
        for slot_row in trained_row.get("slots", []):
            if not isinstance(slot_row, Mapping):
                continue
            command_id = str(slot_row.get("commandId", ""))
            button = command_buttons.get(command_id.casefold())
            if button is None:
                raise PlayableStructureCompilerError(
                    f"{label} command set {set_id} references the missing "
                    f"CommandButton {command_id}"
                )
            commands = {value.strip().casefold() for value in button.values("Command")}
            if commands & _RADIAL_PAGE_COMMANDS:
                # One selector legitimately rides several slots and several
                # command-set variants: `Command_RadialBack` closes BOTH pages,
                # so it sits at the last slot of each (DwarvenFortressCommandSet
                # 14 and 24). Collect the slots rather than picking a winner.
                selector = page_selectors.setdefault(
                    command_id.casefold(),
                    {"commandId": command_id, "slots": []},
                )
                slot = int(slot_row.get("slot", 0))
                slots = selector["slots"]
                assert isinstance(slots, list)
                if slot not in slots:
                    slots.append(slot)
                if "command" not in selector:
                    selector["command"] = sorted(commands)[0].upper()
                    selector.update(_authored_presentation(button))
                    for field, output_key in _RADIAL_PAGE_RANGE_FIELDS:
                        raw = _first(button.values(field))
                        if raw is None or not raw.strip():
                            continue
                        selector[output_key] = int(
                            _numeric_value(
                                raw,
                                defines,
                                f"{label} CommandButton {command_id} {field}",
                            )
                        )
                continue
            if commands not in ({_WALL_UPGRADE_COMMAND}, {_CASTLE_UPGRADE_COMMAND}):
                continue
            command_surface_seen = True
            upgrades = [
                token
                for value in button.values("Upgrade")
                for token in _tokens(value)
                if token.casefold() not in {"none", "null"}
            ]
            if len(upgrades) != 1:
                # A multi-upgrade object button is an authored surface for a
                # different mechanic; it has no single castle trigger to bind.
                continue
            upgrade_id = upgrades[0]
            folded = upgrade_id.casefold()
            if folded in sold_triggers:
                # The same improvement rides every per-level / _ForMP variant of
                # the fortress command set, so one trigger is legitimately
                # reachable from several sets. Emit it once - the runtime
                # validator rejects the whole surface on a duplicate
                # `upgradeId` - but only when the button and slot agree
                # everywhere, mirroring the conflict check `_research_surface`
                # already makes. A genuine disagreement is a data question, not
                # something to silently pick a winner for.
                previous = next(
                    row for row in rows if str(row["upgradeId"]).casefold() == folded
                )
                if previous["commandId"] != command_id or previous["slot"] != int(
                    slot_row.get("slot", 0)
                ):
                    raise PlayableStructureCompilerError(
                        f"{label} castle upgrade {upgrade_id} authors conflicting "
                        "buttons or slots across command sets"
                    )
                continue
            # A fortress improvement comes in TWO authored shapes and both are
            # sales. The trigger shape buys a `*Trigger` upgrade that a
            # `CastleUpgrade` module converts and hands to the whole castle
            # (dwarvenfortress.ini:1096). The plain shape buys an upgrade that
            # simply applies to the fortress itself — Banners, Siege Kegs, Oil
            # Casks and Mighty Catapult on `DwarvenFortressCommandSet`
            # (commandset.ini:4107 slots 8/9/11/13) are all of that shape, four
            # of the six buttons on retail's upgrades page. Compiling only the
            # trigger shape is what left that page two-thirds empty in game.
            behavior = behavior_by_trigger.get(folded)
            trigger_id = upgrade_id
            granted_id = ""
            if behavior is not None:
                trigger_id, granted_id, _wall_upgrade_radius = behavior

            def _record_non_purchasable(reason: str) -> None:
                marker: dict[str, object] = {
                    "upgradeId": upgrade_id,
                    "commandId": command_id,
                    "slot": int(slot_row.get("slot", 0)),
                    "reason": reason,
                }
                marker.update(
                    _purchase_button_fields(button, include_needed_upgrade_any=False)
                )
                non_purchasable.append(marker)

            # A trigger NAMED BY A MODULE with no authored price is a broken
            # pack and fails closed. A plain button may legitimately point at a
            # feature-toggle upgrade that is granted elsewhere and never sold,
            # so that case is recorded instead of aborting the whole faction.
            upgrade_source = _optional_document(documents, UPGRADE_PATH)
            if upgrade_source is None:
                if behavior is None:
                    _record_non_purchasable(
                        f"{UPGRADE_PATH} is not in the effective INI view"
                    )
                    continue
                raise PlayableStructureCompilerError(
                    f"{label} authors castle upgrades but {UPGRADE_PATH} is not in "
                    "the effective INI view"
                )
            upgrade_blocks = _named_blocks(upgrade_source, "Upgrade")
            upgrade_block = upgrade_blocks.get(folded)
            if upgrade_block is None:
                if behavior is None:
                    _record_non_purchasable(
                        f"OBJECT_UPGRADE button has no {UPGRADE_PATH} block"
                    )
                    continue
                raise PlayableStructureCompilerError(
                    f"{label} castle upgrade {trigger_id} has no "
                    f"{UPGRADE_PATH} block"
                )
            upgrade_type = _first(upgrade_block.values("Type"))
            if upgrade_type is None or upgrade_type.strip().casefold() != "object":
                if behavior is None:
                    _record_non_purchasable(
                        "OBJECT_UPGRADE button sells a non-OBJECT upgrade"
                    )
                    continue
                raise PlayableStructureCompilerError(
                    f"{label} castle upgrade {trigger_id} is not an OBJECT upgrade"
                )
            cost_expression = _first(upgrade_block.values("BuildCost"))
            time_expression = _first(upgrade_block.values("BuildTime"))
            if cost_expression is None or time_expression is None:
                if behavior is None:
                    _record_non_purchasable(
                        "OBJECT_UPGRADE button sells an upgrade with no authored "
                        "BuildCost/BuildTime"
                    )
                    continue
                raise PlayableStructureCompilerError(
                    f"{label} castle upgrade {trigger_id} lacks authored "
                    "BuildCost/BuildTime"
                )
            entry: dict[str, object] = {
                "upgradeId": trigger_id,
                "grantsUpgradeId": granted_id,
                "cost": _numeric_value(
                    cost_expression, defines, f"{label} castle upgrade {trigger_id}"
                ),
                "buildTimeSeconds": _numeric_value(
                    time_expression, defines, f"{label} castle upgrade {trigger_id}"
                ),
                "slot": int(slot_row.get("slot", 0)),
                "commandId": command_id,
            }
            entry.update(
                _purchase_button_fields(
                    button, include_needed_upgrade_any=False
                )
            )
            rows.append(entry)
            sold_triggers.add(folded)

    for folded in sorted(behavior_by_trigger):
        if folded in sold_triggers:
            continue
        trigger_id, granted_id, _wall_upgrade_radius = behavior_by_trigger[folded]
        non_purchasable.append(
            {
                "upgradeId": trigger_id,
                "grantsUpgradeId": granted_id,
                "reason": (
                    "CastleUpgrade trigger has no selling OBJECT_UPGRADE button"
                ),
            }
        )

    rows.sort(key=lambda row: str(row["upgradeId"]).casefold())
    non_purchasable.sort(
        key=lambda row: (
            str(row["upgradeId"]).casefold(),
            str(row.get("commandId", "")).casefold(),
            int(row.get("slot", 0)),
        )
    )
    selector_rows = [
        page_selectors[key] for key in sorted(page_selectors)
    ]
    for selector in selector_rows:
        slots = selector["slots"]
        assert isinstance(slots, list)
        slots.sort()
    source_paths = {
        item.source_virtual_path
        for item in lineage
    }
    if rows:
        source_paths.update({UPGRADE_PATH, COMMAND_SET_PATH, COMMAND_BUTTON_PATH})
    elif command_surface_seen or selector_rows:
        source_paths.update({COMMAND_SET_PATH, COMMAND_BUTTON_PATH})
    result: dict[str, object] = {
        "sourceIni": sorted(source_paths, key=str.casefold),
    }
    if rows:
        result["upgrades"] = rows
    if selector_rows:
        result["pageSelectors"] = selector_rows
    if non_purchasable:
        result["nonPurchasable"] = non_purchasable
    return result


def _research_surface(
    target_id: str,
    lineage: Sequence[SageObject],
    trained: Sequence[Mapping[str, object]],
    documents: Mapping[str, bytes],
    command_buttons: Mapping[str, object],
    defines: Mapping[str, int | float],
) -> dict[str, object] | None:
    """Compile the structure's authored PLAYER_UPGRADE research sales."""

    label = f"structure {target_id}"
    entries: dict[str, dict[str, object]] = {}
    for row in trained:
        set_id = str(row.get("id", ""))
        for slot_row in row.get("slots", []):
            if not isinstance(slot_row, Mapping):
                continue
            command_id = str(slot_row.get("commandId", ""))
            button = command_buttons.get(command_id.casefold())
            if button is None:
                raise PlayableStructureCompilerError(
                    f"{label} command set {set_id} references the missing "
                    f"CommandButton {command_id}"
                )
            commands = {value.strip().casefold() for value in button.values("Command")}
            if commands != {_PLAYER_UPGRADE_COMMAND}:
                continue
            upgrades = [
                token
                for value in button.values("Upgrade")
                for token in _tokens(value)
                if token.casefold() not in {"none", "null"}
            ]
            if len(upgrades) != 1:
                raise PlayableStructureCompilerError(
                    f"{label} CommandButton {command_id} names an ambiguous "
                    "research upgrade"
                )
            upgrade_id = upgrades[0]
            slot = int(slot_row.get("slot", 0))
            existing = entries.get(upgrade_id.casefold())
            if existing is not None:
                # The same research rides every per-level command set; the
                # button and slot must agree everywhere it is offered.
                if existing["commandId"] != command_id or existing["slot"] != slot:
                    raise PlayableStructureCompilerError(
                        f"{label} research {upgrade_id} authors conflicting "
                        "buttons across command sets"
                    )
                continue
            entry: dict[str, object] = {
                "upgradeId": upgrade_id,
                "commandId": command_id,
                "commandSetId": set_id,
                "slot": slot,
            }
            entry.update(
                _purchase_button_fields(
                    button, include_needed_upgrade_any=True
                )
            )
            entries[upgrade_id.casefold()] = entry
    if not entries:
        return None
    upgrade_source = _optional_document(documents, UPGRADE_PATH)
    if upgrade_source is None:
        raise PlayableStructureCompilerError(
            f"{label} authors a research surface but {UPGRADE_PATH} is not in "
            "the effective INI view"
        )
    upgrade_blocks = _named_blocks(upgrade_source, "Upgrade")
    rows: list[dict[str, object]] = []
    non_purchasable: list[dict[str, object]] = []
    for folded in sorted(entries):
        entry = entries[folded]
        upgrade_id = str(entry["upgradeId"])
        upgrade_block = upgrade_blocks.get(folded)
        if upgrade_block is None:
            raise PlayableStructureCompilerError(
                f"{label} research {upgrade_id} has no {UPGRADE_PATH} block"
            )
        upgrade_type = _first(upgrade_block.values("Type"))
        if upgrade_type is None or upgrade_type.strip().casefold() != "player":
            raise PlayableStructureCompilerError(
                f"{label} research {upgrade_id} is not a PLAYER upgrade"
            )
        cost_expression = _first(upgrade_block.values("BuildCost"))
        time_expression = _first(upgrade_block.values("BuildTime"))
        if cost_expression is None and time_expression is None:
            # RotWK authors its Collector's-Edition graphics switches
            # (Upgrade_ActivateCEGraphicsA/B) as PLAYER upgrades with no
            # BuildCost/BuildTime at all: a free player-side feature toggle,
            # not research the HUD sells. Record the marker row excluded from
            # the purchasable surface. A row authoring exactly one of the two
            # fields is still a malformed purchase and fails closed below.
            marker = dict(entry)
            marker["reason"] = (
                "cost-less PLAYER feature toggle (no authored "
                "BuildCost/BuildTime)"
            )
            non_purchasable.append(marker)
            continue
        if cost_expression is None or time_expression is None:
            raise PlayableStructureCompilerError(
                f"{label} research {upgrade_id} lacks authored BuildCost/BuildTime"
            )
        entry["cost"] = _numeric_value(cost_expression, defines, f"{label} research {upgrade_id}")
        entry["buildTimeSeconds"] = _numeric_value(
            time_expression, defines, f"{label} research {upgrade_id}"
        )
        rows.append(entry)
    rows.sort(key=lambda row: (int(row["slot"]), str(row["upgradeId"]).casefold()))
    non_purchasable.sort(
        key=lambda row: (int(row["slot"]), str(row["upgradeId"]).casefold())
    )
    result: dict[str, object] = {
        "sourceIni": sorted(
            {UPGRADE_PATH, "data/ini/commandset.ini", "data/ini/commandbutton.ini"},
            key=str.casefold,
        ),
    }
    if rows:
        result["upgrades"] = rows
    if non_purchasable:
        result["nonPurchasable"] = non_purchasable
    return result


def _upgrade_effects(
    target_id: str,
    lineage: Sequence[SageObject],
    documents: Mapping[str, bytes],
    defines: Mapping[str, int | float],
) -> dict[str, object] | None:
    """Compile this structure's own effect modules bound to PLAYER upgrades.

    The marketplace authors the IronOre purchase discount and a Defiance
    refund; farms author the GrandHarvest income bonus and the same Defiance
    refund.  Each effect binds the technology id its module names.  Modules
    bound to PLAYER upgrades whose kind the runtime cannot apply are recorded
    as declared unsupported capabilities with their authored evidence.
    """

    label = f"structure {target_id}"
    upgrade_source = _optional_document(documents, UPGRADE_PATH)
    player_upgrades: frozenset[str] = frozenset()
    if upgrade_source is not None:
        player_upgrades = frozenset(
            name
            for name, block in _named_blocks(upgrade_source, "Upgrade").items()
            if any(value.strip().casefold() == "player" for value in block.values("Type"))
        )
    effects: list[dict[str, object]] = []
    unsupported: list[dict[str, object]] = []
    for block in _walk_blocks(_effective_top_blocks(lineage)):
        kind = block.kind.casefold()
        if kind not in _SUPPORTED_UPGRADE_EFFECT_KINDS:
            bound = [
                token
                for field in ("TriggeredBy", "UpgradeRequired")
                for value in block.values(field)
                for token in _tokens(value)
                if token.casefold() in player_upgrades
            ]
            for token in sorted(set(bound), key=str.casefold):
                unsupported.append(
                    {
                        "upgradeId": token,
                        "module": block.kind,
                        "sourceIni": block.source_virtual_path,
                        "line": int(block.line),
                        "reason": (
                            "authored module kind is not a supported "
                            "structure upgrade effect"
                        ),
                    }
                )
            continue
        if kind == "costmodifierupgrade":
            triggers = [
                token
                for value in block.values("TriggeredBy")
                for token in _tokens(value)
                if token.casefold() not in {"none", "null"}
            ]
            applied = [
                token
                for value in block.values("ApplyToTheseUpgrades")
                for token in _tokens(value)
                if token.casefold() not in {"none", "null"}
            ]
            percent_raw = _first_raw(block, "Percentage")
            starts_active = any(
                value.strip().casefold() in {"yes", "true", "1"}
                for value in block.values("StartsActive")
            )
            if not applied:
                # Retail also authors CostModifierUpgrade variants this
                # runtime does not apply as an upgrade-purchase discount:
                # ObjectFilter unit/structure cost discounts (the
                # IsengardFortress Excavations block, hero statues, the
                # GondorStoneMaker) and StartsActive/Slaughter value
                # modifiers (MenGarrisonTowerExpansion). Record the
                # PLAYER-upgrade-bound ones as declared unsupported
                # capabilities with their authored evidence — mirroring the
                # unsupported-module path above — instead of failing the
                # whole structure closed.
                for token in sorted(
                    {t for t in triggers if t.casefold() in player_upgrades},
                    key=str.casefold,
                ):
                    unsupported.append(
                        {
                            "upgradeId": token,
                            "module": block.kind,
                            "sourceIni": block.source_virtual_path,
                            "line": int(block.line),
                            "reason": (
                                "authored CostModifierUpgrade variant is not "
                                "a supported structure upgrade effect"
                            ),
                        }
                    )
                continue
            if not triggers and starts_active and percent_raw is not None:
                # Some retail factories author an always-active purchase
                # discount over an explicit upgrade list (MordorTavern).
                # It is not triggered by one PLAYER upgrade, so the current
                # per-upgrade effect model cannot apply it faithfully. Keep
                # the exact binding as declared unsupported instead of
                # rejecting the whole producer structure.
                for token in sorted(
                    {t for t in applied if t.casefold() in player_upgrades},
                    key=str.casefold,
                ):
                    unsupported.append(
                        {
                            "upgradeId": token,
                            "module": block.kind,
                            "sourceIni": block.source_virtual_path,
                            "line": int(block.line),
                            "reason": (
                                "authored always-active CostModifierUpgrade "
                                "is not a supported per-upgrade effect"
                            ),
                        }
                    )
                continue
            if not triggers or percent_raw is None:
                raise PlayableStructureCompilerError(
                    f"{label} CostModifierUpgrade lacks TriggeredBy/"
                    "ApplyToTheseUpgrades/Percentage"
                )
            for upgrade_id in triggers:
                if upgrade_id.casefold() not in player_upgrades:
                    continue
                effect = {
                    "upgradeId": upgrade_id,
                    "kind": "upgrade-discount",
                    "applyToUpgradeIds": applied,
                    "percent": _percent_value(
                        percent_raw, defines, f"{label} {block.kind} Percentage"
                    ),
                    "upgradeDiscount": any(
                        value.strip().casefold() in {"yes", "true", "1"}
                        for value in block.values("UpgradeDiscount")
                    ),
                    "sourceIni": block.source_virtual_path,
                    "line": int(block.line),
                }
                label_row = _first_raw(block, "LabelForPalantirString")
                if label_row is not None:
                    effect["labelId"] = label_row
                effects.append(effect)
        elif kind == "refunddie":
            required = [
                token
                for value in block.values("UpgradeRequired")
                for token in _tokens(value)
                if token.casefold() not in {"none", "null"}
            ]
            percent_raw = _first_raw(block, "RefundPercent")
            if not required or percent_raw is None:
                raise PlayableStructureCompilerError(
                    f"{label} RefundDie lacks UpgradeRequired/RefundPercent"
                )
            for upgrade_id in required:
                if upgrade_id.casefold() not in player_upgrades:
                    continue
                effect = {
                    "upgradeId": upgrade_id,
                    "kind": "refund-on-death",
                    "refundPercent": _percent_value(
                        percent_raw, defines, f"{label} {block.kind} RefundPercent"
                    ),
                    "sourceIni": block.source_virtual_path,
                    "line": int(block.line),
                }
                building_required = _first_raw(block, "BuildingRequired")
                if building_required is not None:
                    effect["buildingRequired"] = building_required
                effects.append(effect)
        else:
            upgrade_row = _first(block.values("Upgrade"))
            if upgrade_row is None:
                continue
            upgrade_ids = [
                token
                for token in _tokens(upgrade_row)
                if token.casefold() not in {"none", "null"}
            ]
            bonus_raw = _first_raw(block, "UpgradeBonusPercent")
            for upgrade_id in upgrade_ids:
                if upgrade_id.casefold() not in player_upgrades:
                    continue
                if bonus_raw is None:
                    raise PlayableStructureCompilerError(
                        f"{label} TerrainResourceBehavior names {upgrade_id} "
                        "without UpgradeBonusPercent"
                    )
                effect = {
                    "upgradeId": upgrade_id,
                    "kind": "income-bonus",
                    "bonusPercent": _percent_value(
                        bonus_raw,
                        defines,
                        f"{label} {block.kind} UpgradeBonusPercent",
                    ),
                    "sourceIni": block.source_virtual_path,
                    "line": int(block.line),
                }
                must_be_present = _first_raw(block, "UpgradeMustBePresent")
                if must_be_present is not None:
                    effect["upgradeMustBePresent"] = must_be_present
                effects.append(effect)
    if not effects and not unsupported:
        return None
    effects.sort(key=lambda row: (str(row["upgradeId"]).casefold(), str(row["kind"])))
    unsupported.sort(key=lambda row: (str(row["upgradeId"]).casefold(), str(row["module"])))
    return {
        "effects": effects,
        "unsupportedEffects": unsupported,
        "sourceIni": sorted(
            {
                str(row["sourceIni"])
                for row in [*effects, *unsupported]
            },
            key=str.casefold,
        ),
    }


def _upgrade_chain(
    target_id: str,
    lineage: Sequence[SageObject],
    trained: Sequence[Mapping[str, object]],
    documents: Mapping[str, bytes],
    command_buttons: Mapping[str, object],
    defines: Mapping[str, int | float],
) -> dict[str, object] | None:
    """Compile the authored purchased-level chain of one structure."""

    label = f"structure {target_id}"
    level_up_modules: list[tuple[tuple[str, ...], int, int, SageBlock]] = []
    for block in _walk_blocks(_effective_top_blocks(lineage)):
        kind = block.kind.casefold()
        if kind != "levelupupgrade":
            continue
        triggers = tuple(
            token
            for value in block.values("TriggeredBy")
            for token in _tokens(value)
            if token.casefold() not in {"none", "null"}
        )
        if not triggers:
            raise PlayableStructureCompilerError(
                f"{label} LevelUpUpgrade has no TriggeredBy upgrade"
            )
        levels_to_gain = _first(block.values("LevelsToGain"))
        level_cap = _first(block.values("LevelCap"))
        if (
            levels_to_gain is None
            or not re.fullmatch(r"[0-9]+", levels_to_gain.strip())
            or int(levels_to_gain.strip()) < 1
        ):
            raise PlayableStructureCompilerError(
                f"{label} LevelUpUpgrade has no valid LevelsToGain"
            )
        # SAGE's INI integer scanner is atoi-style: leading digits parse and
        # trailing junk is ignored. Retail relies on that exactly once — the
        # GoblinFissure Level3 module authors "LevelCap = 3w"
        # (data/ini/object/evilfaction/structures/wild/fissure.ini) and the
        # retail engine still caps the fissure at level 3. Match the engine:
        # accept a leading-integer LevelCap; fail closed when no digit leads.
        cap_match = (
            re.match(r"[0-9]+", level_cap.strip()) if level_cap is not None else None
        )
        if cap_match is None:
            raise PlayableStructureCompilerError(
                f"{label} LevelUpUpgrade has no valid LevelCap"
            )
        level_up_modules.append(
            (triggers, int(levels_to_gain.strip()), int(cap_match.group(0)), block)
        )
    if not level_up_modules:
        return None
    transition_rows, subobject_rows = _level_module_rows(lineage)

    def _subobject_modules_for(upgrade_id: str) -> list[tuple[tuple[str, ...], tuple[str, ...], SageBlock]]:
        return [
            (shows, hides, block)
            for triggers, shows, hides, block in subobject_rows
            if any(token.casefold() == upgrade_id.casefold() for token in triggers)
        ]

    def _transition_for(upgrade_id: str) -> str | None:
        matches = sorted(
            {
                set_id
                for triggers, set_id, requires_all in transition_rows
                if not requires_all
                and any(token.casefold() == upgrade_id.casefold() for token in triggers)
            },
            key=str.casefold,
        )
        if len(matches) != 1:
            return None
        return matches[0]

    upgrade_source = _optional_document(documents, UPGRADE_PATH)
    if upgrade_source is None:
        raise PlayableStructureCompilerError(
            f"{label} authors LevelUpUpgrade modules but {UPGRADE_PATH} is not "
            "in the effective INI view"
        )
    upgrade_blocks = _named_blocks(upgrade_source, "Upgrade")
    experience_source = _optional_document(documents, EXPERIENCE_LEVELS_PATH)
    if experience_source is None:
        raise PlayableStructureCompilerError(
            f"{label} authors LevelUpUpgrade modules but "
            f"{EXPERIENCE_LEVELS_PATH} is not in the effective INI view"
        )
    modifier_source = _optional_document(documents, ATTRIBUTE_MODIFIER_PATH)
    modifier_blocks: dict[str, IniBlock] = {}
    if modifier_source is not None:
        modifier_blocks = _named_blocks(modifier_source, "ModifierList")
    structure_names = frozenset(item.name.casefold() for item in lineage)
    try:
        chain = _select_experience_chain(
            _experience_level_rows(experience_source),
            _ability_list_defines(experience_source),
            structure_names,
            label,
        )
    except PlayableUnitCompilerError as exc:
        raise PlayableStructureCompilerError(str(exc)) from exc
    if not chain:
        raise PlayableStructureCompilerError(
            f"{label} authors LevelUpUpgrade modules but no ExperienceLevel "
            "chain targets it"
        )
    def _level_modifier_leaf_wrapped(
        modifier_id: str, row: Mapping[str, object]
    ) -> dict[str, object]:
        try:
            return _level_modifier_leaf(
                modifier_blocks,
                str(modifier_id),
                defines,
                f"{label} ExperienceLevel {row.get('id')}",
            )
        except PlayableUnitCompilerError as exc:
            raise PlayableStructureCompilerError(str(exc)) from exc

    effects_by_rank: dict[int, list[dict[str, object]]] = {}
    for row, _targets in chain:
        rank = int(row["rank"])
        modifier_ids = [
            token
            for token in row.get("attributeModifiers", ())
            if str(token).casefold() not in {"", "none", "null"}
        ]
        if not modifier_ids:
            continue
        if modifier_source is None:
            raise PlayableStructureCompilerError(
                f"{label} ExperienceLevel {row.get('id')} authors "
                f"AttributeModifiers but {ATTRIBUTE_MODIFIER_PATH} is not in "
                "the effective INI view"
            )
        effects_by_rank[rank] = [
            _level_modifier_leaf_wrapped(modifier_id, row)
            for modifier_id in modifier_ids
        ]

    set_slots: dict[str, list[dict[str, object]]] = {}
    direct_sets: list[str] = []
    for row in trained:
        set_id = str(row.get("id", ""))
        set_slots[set_id.casefold()] = list(row.get("slots", []))
        if str(row.get("kind", "")) == "direct":
            direct_sets.append(set_id)
    if len(direct_sets) != 1:
        raise PlayableStructureCompilerError(
            f"{label} authors LevelUpUpgrade modules but has "
            f"{len(direct_sets)} direct command sets"
        )

    def _purchase_button(
        set_id: str, already_consumed: set[str]
    ) -> tuple[str, str, int, object] | None:
        for slot_row in set_slots.get(set_id.casefold(), []):
            command_id = str(slot_row.get("commandId", ""))
            button = command_buttons.get(command_id.casefold())
            if button is None:
                continue
            commands = {value.strip().casefold() for value in button.values("Command")}
            if commands != {_OBJECT_UPGRADE_COMMAND}:
                continue
            upgrades = [
                token
                for value in button.values("Upgrade")
                for token in _tokens(value)
                if token.casefold() not in {"none", "null"}
            ]
            if len(upgrades) != 1:
                raise PlayableStructureCompilerError(
                    f"{label} CommandButton {command_id} names an ambiguous "
                    "Upgrade chain"
                )
            upgrade_id = upgrades[0]
            if upgrade_id.casefold() in already_consumed:
                # An identity transition keeps the command set in place, so
                # the already-purchased step's button is still on the panel;
                # it is spent, not a loop.
                continue
            if any(
                upgrade_id.casefold() == trigger.casefold()
                for triggers, _gain, _cap, _block in level_up_modules
                for trigger in triggers
            ):
                return (command_id, upgrade_id, int(slot_row.get("slot", 0)), button)
        return None

    steps: list[dict[str, object]] = []
    consumed: set[str] = set()
    consumed_engine_triggers: set[str] = set()
    current_set = direct_sets[0]
    current_level = 1
    level_cap = max(cap for _t, _g, cap, _b in level_up_modules)
    while True:
        found = _purchase_button(current_set, consumed)
        if found is None:
            break
        command_id, upgrade_id, slot, button = found
        folded_upgrade = upgrade_id.casefold()
        if folded_upgrade in consumed:
            raise PlayableStructureCompilerError(
                f"{label} upgrade {upgrade_id} loops in the command-set chain"
            )
        module_matches = [
            (triggers, gain, cap)
            for triggers, gain, cap, _block in level_up_modules
            if any(token.casefold() == folded_upgrade for token in triggers)
        ]
        if len(module_matches) != 1:
            raise PlayableStructureCompilerError(
                f"{label} upgrade {upgrade_id} binds "
                f"{len(module_matches)} LevelUpUpgrade modules"
            )
        _triggers, gain, cap = module_matches[0]
        target_level = min(cap, current_level + gain)
        # Transition resolution mirrors retail's two authoring styles, then
        # falls through to an explicit identity — never an invented swap:
        # 1. "authored": CommandSetUpgrade keyed by the purchased upgrade id
        #    (the GondorBarracks family; 24 of 29 BFME2 level-up structures).
        # 2. "structure-level": CommandSetUpgrade keyed by the engine-granted
        #    Upgrade_StructureLevel<N> that fires when this step's level
        #    lands (IsengardSiegeWorks, GoblinCave, GoblinFissure,
        #    WildSpiderPit, DwarvenArcheryRange).
        # 3. "identity": no CommandSetUpgrade serves the step at all — the
        #    command set legitimately stays put, compiled as an explicit
        #    fromCommandSet == toCommandSet pair with a marker so downstream
        #    stays honest. Malformed steps (missing cost/time/upgrade block)
        #    still fail closed below.
        engine_trigger = f"upgrade_structurelevel{target_level}"
        transition_kind = "authored"
        to_command_set = _transition_for(folded_upgrade)
        if to_command_set is None:
            to_command_set = _transition_for(engine_trigger)
            if to_command_set is not None:
                transition_kind = "structure-level"
                consumed_engine_triggers.add(engine_trigger)
            else:
                transition_kind = "identity"
                to_command_set = current_set
        if to_command_set.casefold() not in set_slots:
            raise PlayableStructureCompilerError(
                f"{label} upgrade {upgrade_id} transitions to the missing "
                f"command set {to_command_set}"
            )
        upgrade_block = upgrade_blocks.get(folded_upgrade)
        if upgrade_block is None:
            raise PlayableStructureCompilerError(
                f"{label} upgrade {upgrade_id} has no {UPGRADE_PATH} block"
            )
        upgrade_type = _first(upgrade_block.values("Type"))
        if upgrade_type is None or upgrade_type.strip().casefold() != "object":
            raise PlayableStructureCompilerError(
                f"{label} upgrade {upgrade_id} is not an OBJECT upgrade"
            )
        cost_expression = _first(upgrade_block.values("BuildCost"))
        time_expression = _first(upgrade_block.values("BuildTime"))
        if cost_expression is None or time_expression is None:
            raise PlayableStructureCompilerError(
                f"{label} upgrade {upgrade_id} lacks authored BuildCost/BuildTime"
            )
        cost = _numeric_value(cost_expression, defines, f"{label} upgrade {upgrade_id}")
        build_time = _numeric_value(time_expression, defines, f"{label} upgrade {upgrade_id}")
        current_level = target_level
        options = {
            token.casefold()
            for value in button.values("Options")
            for token in _tokens(value)
        }
        step: dict[str, object] = {
            "upgradeId": upgrade_id,
            "commandId": command_id,
            "slot": slot,
            "toLevel": current_level,
            "levelsToGain": gain,
            "levelCap": cap,
            "cost": cost,
            "buildTimeSeconds": build_time,
            "cancelable": "cancelable" in options,
            "fromCommandSet": current_set,
            "toCommandSet": to_command_set,
        }
        if transition_kind != "authored":
            # Absent marker == authored purchased-trigger transition; the
            # authored family's descriptors stay byte-identical.
            step["commandSetTransition"] = transition_kind
        if steps:
            step["requiresUpgradeId"] = steps[-1]["upgradeId"]
        labels: list[str] = []
        named_labels: dict[str, str] = {}
        for field, output_key in (
            ("TextLabel", "labelId"),
            ("DescriptLabel", "tooltipId"),
            ("ButtonImage", "buttonImageId"),
        ):
            for value in button.values(field):
                text = value.strip()
                if text and text.casefold() not in {"none", "null"}:
                    # UI ids carry a NAMESPACE:Name colon; keep the raw leaf.
                    labels.append(text)
                    named_labels[output_key] = text
                    break
        if labels:
            step["buttonLabels"] = labels
        step.update(named_labels)
        step_subobjects = _subobject_modules_for(folded_upgrade)
        if transition_kind != "authored" and not step_subobjects:
            # The aliased authoring style binds the per-level model variants
            # to the same engine-granted Upgrade_StructureLevel<N> trigger as
            # the command-set swap; the engine fires that upgrade whenever
            # the level lands, so the rows belong to this step.
            step_subobjects = _subobject_modules_for(engine_trigger)
            if step_subobjects:
                consumed_engine_triggers.add(engine_trigger)
        if step_subobjects:
            step["presentation"] = {
                "subObjects": [
                    {
                        "show": list(shows),
                        "hide": list(hides),
                        "sourceIni": block.source_virtual_path,
                        "line": int(block.line),
                    }
                    for shows, hides, block in step_subobjects
                ],
            }
        effects = effects_by_rank.get(current_level)
        if effects:
            step["effects"] = effects
        steps.append(step)
        consumed.add(folded_upgrade)
        current_set = to_command_set
        if len(steps) > len(level_up_modules):
            raise PlayableStructureCompilerError(
                f"{label} upgrade chain exceeds its LevelUpUpgrade module count"
            )
    unconsumed = sorted(
        {
            trigger
            for triggers, _gain, _cap, _block in level_up_modules
            for trigger in triggers
            if trigger.casefold() not in consumed
        },
        key=str.casefold,
    )
    if unconsumed:
        raise PlayableStructureCompilerError(
            f"{label} LevelUpUpgrade upgrades are not purchasable through the "
            f"command-set chain: {', '.join(unconsumed)}"
        )
    if not steps:
        return None
    # Per-level model variants: the engine-granted StructureLevel1 base state
    # (retail's HideAll/Show module), then every chain step's SubObjectsUpgrade
    # applied in authored order. Visibility resolves cumulatively per level —
    # never a guess, only the authored show/hide tokens.
    level_one_rows = [
        (shows, hides, block)
        for triggers, shows, hides, block in subobject_rows
        if any(token.casefold() == "upgrade_structurelevel1" for token in triggers)
    ]
    visibility: dict[str, bool] = {}
    for shows, hides, _block in level_one_rows:
        for token in shows:
            visibility[token] = True
        for token in hides:
            visibility[token] = False
    chain: dict[str, object] = {
        "levelCap": level_cap,
        "steps": steps,
        "sourceIni": sorted(
            {
                UPGRADE_PATH,
                EXPERIENCE_LEVELS_PATH,
                *(
                    [ATTRIBUTE_MODIFIER_PATH]
                    if effects_by_rank
                    else []
                ),
            },
            key=str.casefold,
        ),
    }
    if level_one_rows:
        chain["levelOne"] = {
            "subObjects": [
                {
                    "show": list(shows),
                    "hide": list(hides),
                    "sourceIni": block.source_virtual_path,
                    "line": int(block.line),
                }
                for shows, hides, block in level_one_rows
            ],
            "visibleSubObjects": sorted(
                (token for token, shown in visibility.items() if shown),
                key=str.casefold,
            ),
            "hiddenSubObjects": sorted(
                (token for token, shown in visibility.items() if not shown),
                key=str.casefold,
            ),
        }
    for step in steps:
        presentation = step.get("presentation")
        if not isinstance(presentation, dict):
            continue
        for row in presentation["subObjects"]:
            for token in row["show"]:
                visibility[token] = True
            for token in row["hide"]:
                visibility[token] = False
        presentation["visibleSubObjects"] = sorted(
            (token for token, shown in visibility.items() if shown),
            key=str.casefold,
        )
        presentation["hiddenSubObjects"] = sorted(
            (token for token, shown in visibility.items() if not shown),
            key=str.casefold,
        )
    chain_upgrade_ids = (
        {str(step["upgradeId"]).casefold() for step in steps}
        | {"upgrade_structurelevel1"}
        | consumed_engine_triggers
    )
    unconsumed_subobjects = sorted(
        {
            token
            for triggers, _shows, _hides, _block in subobject_rows
            for token in triggers
            if token.casefold() not in chain_upgrade_ids
        },
        key=str.casefold,
    )
    if unconsumed_subobjects:
        # SubObjectsUpgrade rows bound to other mechanics (wall/turret grants)
        # are recorded, never silently dropped or invented into the chain.
        chain["unconsumedSubObjectTriggers"] = unconsumed_subobjects
    return chain


def compile_playable_structure_descriptor(
    target_id: str,
    documents: Mapping[str, bytes],
    *,
    prepared: PlayableUnitCompilerInputs | None = None,
    engine_spawned_roots: Iterable[str] = (),
    engine_spawned_roles: Mapping[str, str] | None = None,
    wall_template_roots: Iterable[str] = (),
    source_null_command_sets: Iterable[str] = (),
    game: str = "bfme2",
) -> dict[str, object]:
    """Compile one source-backed structure descriptor or fail closed."""

    if not target_id or len(target_id) > 256:
        raise PlayableStructureCompilerError("target Object id is invalid")
    if prepared is None:
        prepared = prepare_playable_unit_compiler(documents)
    elif prepared.documents is not documents:
        raise PlayableStructureCompilerError(
            "prepared compiler inputs belong to a different document mapping"
        )
    target = prepared.objects.get(target_id.casefold())
    if target is None:
        raise PlayableStructureCompilerError(
            f"effective Object is missing: {target_id}"
        )
    lineage = _ancestry(prepared.objects, target)
    # SAGE selection/collision volume. Retail hit-tests a click against this
    # footprint; without it a runtime has nothing but a guessed radius.
    geometry_contract = _geometry_contract(lineage, prepared.numeric_defines)
    kinds = _kind_of(lineage)
    if not STRUCTURE_KIND_TOKENS & set(kinds):
        raise PlayableStructureCompilerError(
            f"Object {target_id} has no structure KindOf capability"
        )

    production = _construct_routes(
        target_id, prepared.objects, prepared.command_sets, prepared.command_buttons
    )
    upgrade_routes = _wall_upgrade_routes(
        target_id, prepared.objects, prepared.command_sets, prepared.command_buttons
    )
    spawned_keys = {value.casefold() for value in engine_spawned_roots}
    spawned_roles = {
        str(key).casefold(): str(value)
        for key, value in (engine_spawned_roles or {}).items()
    }
    if set(spawned_roles) - spawned_keys:
        raise PlayableStructureCompilerError(
            "engine-spawned composite roles name undeclared roots"
        )
    wall_keys = {value.casefold() for value in wall_template_roots}
    if production:
        production_evidence = "authored-construct-command"
        production = [*production, *upgrade_routes]
    elif upgrade_routes:
        production_evidence = "authored-wall-upgrade-command"
        production = upgrade_routes
    elif target.name.casefold() in spawned_keys:
        production_evidence = "engine-spawned-composite"
    elif target.name.casefold() in wall_keys:
        production_evidence = "wall-template"
    else:
        raise PlayableStructureCompilerError(
            f"Object {target_id} is not targeted by an authored construct "
            "command or wall-upgrade command and is not a declared "
            "engine-spawned or wall-template composite"
        )

    scalars = _scalar_fields(lineage)
    health = _health_contract(lineage, prepared.numeric_defines, target_id)
    if health is None and "BASE_FOUNDATION" not in kinds:
        raise PlayableStructureCompilerError(
            f"structure has no authored body health: {target_id}"
        )
    # armor.ini ArmorSet table (armor section): a referenced but unresolvable
    # set fails the descriptor closed; structures without an authored ArmorSet
    # record the SAGE engine passthrough explicitly.
    from .armor_compiler import ArmorCompilerError, compile_armor_contract

    try:
        armor = compile_armor_contract(documents, lineage, game=game)
    except ArmorCompilerError as exc:
        raise PlayableStructureCompilerError(
            f"structure armor contract is unresolvable: {exc}"
        ) from exc
    trained, source_null_sets = _trained_command_sets(
        lineage,
        prepared.command_sets,
        target_id,
        frozenset(value.casefold() for value in source_null_command_sets),
    )
    upgrade_chain = _upgrade_chain(
        target.name,
        lineage,
        trained,
        documents,
        prepared.command_buttons,
        prepared.numeric_defines,
    )
    structure_level_presentation = (
        None
        if upgrade_chain is not None
        else _structure_level_presentation(target.name, lineage)
    )
    research_surface = _research_surface(
        target.name,
        lineage,
        trained,
        documents,
        prepared.command_buttons,
        prepared.numeric_defines,
    )
    castle_upgrade_surface = _castle_upgrade_surface(
        target.name,
        lineage,
        trained,
        documents,
        prepared.command_buttons,
        prepared.numeric_defines,
    )
    # The purchasable surface ("research") and the recorded non-purchasable
    # feature-toggle markers ride separate keys: downstream registration
    # validates "research" as a sales surface (non-empty purchasable rows),
    # while the markers stay evidence-only.
    research: dict[str, object] | None = None
    non_purchasable_research: dict[str, object] | None = None
    if research_surface is not None:
        marker_rows = research_surface.pop("nonPurchasable", None)
        if "upgrades" in research_surface:
            research = research_surface
        if marker_rows:
            non_purchasable_research = {
                "upgrades": marker_rows,
                "sourceIni": research_surface["sourceIni"],
            }
    castle_upgrades: dict[str, object] | None = None
    non_purchasable_castle_upgrades: dict[str, object] | None = None
    if castle_upgrade_surface is not None:
        marker_rows = castle_upgrade_surface.pop("nonPurchasable", None)
        if "upgrades" in castle_upgrade_surface:
            castle_upgrades = castle_upgrade_surface
        if marker_rows:
            non_purchasable_castle_upgrades = {
                "upgrades": marker_rows,
                "sourceIni": castle_upgrade_surface["sourceIni"],
            }
    upgrade_effects = _upgrade_effects(
        target.name,
        lineage,
        documents,
        prepared.numeric_defines,
    )
    resource_behavior = _resource_behavior_radius(
        lineage, prepared.numeric_defines, target.name
    )
    create_grants = _grant_upgrade_create_contract(
        lineage, documents, target.name
    )
    inherit_upgrades = _inherit_upgrade_create_contract(
        lineage, documents, prepared.numeric_defines, target.name
    )
    production_exit_updates = _queue_production_exit_contract(
        lineage, prepared.numeric_defines, target.name
    )
    auto_deposit_updates = _auto_deposit_contract(
        lineage,
        documents,
        prepared.numeric_defines,
        target.name,
    )
    combat = _structure_combat_contract(lineage, documents, prepared)
    try:
        module_contracts = compile_all_module_contracts(lineage, target.name)
    except ModuleContractError as error:
        raise PlayableStructureCompilerError(str(error)) from error
    audio = {
        key: value
        for key, value in sorted(_audio_routes(lineage).items())
    }
    sources = sorted(
        {
            row["sourceIni"]
            for row in _module_evidence(lineage)
            if isinstance(row.get("sourceIni"), str)
        }
        | {item.source_virtual_path for item in lineage}
        | ({"data/ini/armor.ini"} if armor.get("setId") is not None else set())
        | (
            {str(path) for path in upgrade_chain.get("sourceIni", [])}
            if upgrade_chain is not None
            else set()
        )
        | (
            {str(path) for path in structure_level_presentation.get("sourceIni", [])}
            if structure_level_presentation is not None
            else set()
        )
        | (
            {str(path) for path in research_surface.get("sourceIni", [])}
            if research_surface is not None
            else set()
        )
        | (
            {str(path) for path in castle_upgrade_surface.get("sourceIni", [])}
            if castle_upgrade_surface is not None
            else set()
        )
        | (
            {str(path) for path in upgrade_effects.get("sourceIni", [])}
            if upgrade_effects is not None
            else set()
        )
        | {
            str(row["sourceIni"])
            for row in create_grants
        }
        | {
            str(row["sourceIni"])
            for row in inherit_upgrades
        }
        | (
            {UPGRADE_PATH}
            if (
                create_grants
                or inherit_upgrades
                or any(
                    row["upgradedBoosts"]
                    for row in auto_deposit_updates
                )
            )
            else set()
        )
        | {
            str(row["sourceIni"])
            for value in (combat or {}).values()
            for row in ([value] if isinstance(value, Mapping) else [])
            if isinstance(row.get("sourceIni"), str) and row.get("sourceIni")
        },
        key=lambda value: (value.casefold(), value),
    )
    source_documents = []
    for path in sources:
        payload = documents.get(path)
        if payload is None:
            normalized = next(
                (
                    key
                    for key in documents
                    if key.replace("\\", "/").casefold()
                    == path.replace("\\", "/").casefold()
                ),
                None,
            )
            payload = documents.get(normalized) if normalized else None
        if payload is None:
            raise PlayableStructureCompilerError(
                f"structure source document is missing: {path}"
            )
        source_documents.append(
            {"virtualPath": path, "sha256": hashlib.sha256(payload).hexdigest()}
        )

    descriptor: dict[str, object] = {
        "schema": SCHEMA,
        "schemaVersion": SCHEMA_VERSION,
        "objectId": target.name,
        "category": "structure",
        "kindOf": list(kinds),
        "production": {
            "evidence": production_evidence,
            "routes": production,
        },
        "gameplay": {
            "health": health,
            "armor": armor,
            "trainedCommandSets": trained,
            "sourceNullCommandSets": source_null_sets,
            **(
                {"upgradeChain": upgrade_chain}
                if upgrade_chain is not None
                else {}
            ),
            **(
                {"structureLevelPresentation": structure_level_presentation}
                if structure_level_presentation is not None
                else {}
            ),
            **({"research": research} if research is not None else {}),
            **(
                {"nonPurchasableResearch": non_purchasable_research}
                if non_purchasable_research is not None
                else {}
            ),
            **(
                {"castleUpgrades": castle_upgrades}
                if castle_upgrades is not None
                else {}
            ),
            **(
                {
                    "nonPurchasableCastleUpgrades": (
                        non_purchasable_castle_upgrades
                    )
                }
                if non_purchasable_castle_upgrades is not None
                else {}
            ),
            **(
                {"upgradeEffects": upgrade_effects}
                if upgrade_effects is not None
                else {}
            ),
            **(
                {"resourceBehavior": resource_behavior}
                if resource_behavior is not None
                else {}
            ),
            **({"createGrants": create_grants} if create_grants else {}),
            **(
                {"inheritUpgradesOnCreate": inherit_upgrades}
                if inherit_upgrades
                else {}
            ),
            **(
                {"productionExitUpdates": production_exit_updates}
                if production_exit_updates
                else {}
            ),
            **(
                {"autoDepositUpdates": auto_deposit_updates}
                if auto_deposit_updates
                else {}
            ),
            **(
                {"moduleContracts": module_contracts}
                if module_contracts
                else {}
            ),
            **(
                {"geometry": geometry_contract}
                if geometry_contract is not None
                else {}
            ),
            **({"combat": combat} if combat is not None else {}),
            "scalarFields": _resolved_scalar_fields(
                scalars,
                frozenset(
                    {
                        "BuildCost",
                        "BuildTime",
                        "VisionRange",
                        "ShroudClearingRange",
                        "CommandPoints",
                    }
                ),
                prepared.numeric_defines,
            ),
        },
        "presentation": {
            "ui": {
                key: value
                for key, value in sorted(scalars.items())
                if key in {"DisplayName", "SelectPortrait", "ButtonImage"}
            },
            "audioRoutes": audio,
        },
        "runtimeModules": [],
        "runtimeModuleEvidence": _module_evidence(lineage),
        "sourceDocuments": source_documents,
    }
    if production_evidence == "engine-spawned-composite":
        role = spawned_roles.get(target.name.casefold())
        if role is not None:
            if re.fullmatch(r"[a-z0-9][a-z0-9-]{0,127}", role) is None:
                raise PlayableStructureCompilerError(
                    f"engine-spawned composite role is invalid: {role!r}"
                )
            descriptor["compositeRole"] = role
    descriptor["descriptorSha256"] = _digest(descriptor)
    return descriptor


def validate_playable_structure_descriptor(value: Mapping[str, object]) -> None:
    """Reject any structure descriptor that drifted from its evidence."""

    if value.get("schema") != SCHEMA or value.get("schemaVersion") != SCHEMA_VERSION:
        raise PlayableStructureCompilerError(
            "structure descriptor identity is invalid"
        )
    unsigned = dict(value)
    digest = unsigned.pop("descriptorSha256", None)
    if not isinstance(digest, str) or digest != _digest(unsigned):
        raise PlayableStructureCompilerError(
            "structure descriptor digest is invalid"
        )
    if value.get("category") != "structure":
        raise PlayableStructureCompilerError(
            "structure descriptor category is invalid"
        )
    kinds = value.get("kindOf")
    if not isinstance(kinds, list) or not STRUCTURE_KIND_TOKENS & {
        str(item) for item in kinds
    }:
        raise PlayableStructureCompilerError(
            "structure descriptor KindOf evidence is invalid"
        )
    production = value.get("production")
    if not isinstance(production, Mapping):
        raise PlayableStructureCompilerError(
            "structure descriptor production is invalid"
        )
    routes = production.get("routes")
    evidence = production.get("evidence")
    if not isinstance(routes, list) or evidence not in {
        "authored-construct-command",
        "authored-wall-upgrade-command",
        "engine-spawned-composite",
        "wall-template",
    }:
        raise PlayableStructureCompilerError(
            "structure descriptor production evidence is invalid"
        )
    authored = evidence in {
        "authored-construct-command",
        "authored-wall-upgrade-command",
    }
    if authored and not routes:
        raise PlayableStructureCompilerError(
            "structure descriptor claims authored production evidence without routes"
        )
    if not authored and routes:
        raise PlayableStructureCompilerError(
            "structure descriptor claims non-authored evidence with routes"
        )
    role = value.get("compositeRole")
    if role is not None and (
        evidence != "engine-spawned-composite"
        or not isinstance(role, str)
        or re.fullmatch(r"[a-z0-9][a-z0-9-]{0,127}", role) is None
    ):
        raise PlayableStructureCompilerError(
            "structure descriptor composite role is invalid"
        )
    gameplay = value.get("gameplay")
    if not isinstance(gameplay, Mapping):
        raise PlayableStructureCompilerError(
            "structure descriptor gameplay is invalid"
        )
    combat = gameplay.get("combat")
    if combat is not None:
        if not isinstance(combat, Mapping) or not all(
            isinstance(combat.get(field), Mapping)
            and isinstance(combat[field].get("value"), (int, float))
            and not isinstance(combat[field].get("value"), bool)
            for field in (
                "attackRange",
                "delayBetweenShotsMs",
                "preAttackDelayMs",
                "firingDurationMs",
                "damage",
            )
        ):
            raise PlayableStructureCompilerError(
                "structure descriptor combat contract is invalid"
            )
        for identity in (
            "weaponId",
            "weaponSlot",
            "warheadId",
            "projectileObjectId",
            "damageType",
            "targetAcquisitionWeaponId",
            "spawnedObjectId",
        ):
            if identity in combat and (
                not isinstance(combat[identity], str) or not combat[identity]
            ):
                raise PlayableStructureCompilerError(
                    "structure descriptor combat identity is invalid"
                )
    health = gameplay.get("health")
    if health is None and "BASE_FOUNDATION" not in {
        str(item) for item in kinds
    }:
        raise PlayableStructureCompilerError(
            "structure descriptor omits health without foundation evidence"
        )
    if health is not None:
        if not isinstance(health, Mapping):
            raise PlayableStructureCompilerError(
                "structure descriptor health is invalid"
            )
        highlander = health.get("highlanderBody")
        if highlander is not None and (
            not isinstance(highlander, Mapping)
            or highlander.get("value") is not True
            or not isinstance(highlander.get("sourceIni"), str)
            or not highlander.get("sourceIni")
            or not isinstance(highlander.get("line"), int)
            or isinstance(highlander.get("line"), bool)
            or int(highlander["line"]) <= 0
            or str((health.get("primary", {}) or {}).get("module", "")).casefold()
            != "highlanderbody"
        ):
            raise PlayableStructureCompilerError(
                "structure descriptor HighlanderBody policy is invalid"
            )
    create_grants = gameplay.get("createGrants", [])
    if not isinstance(create_grants, list):
        raise PlayableStructureCompilerError(
            "structure descriptor create grants are invalid"
        )
    for row in create_grants:
        if (
            not isinstance(row, Mapping)
            or not isinstance(row.get("upgradeId"), str)
            or not row.get("upgradeId")
            or row.get("upgradeType") not in {"OBJECT", "PLAYER"}
            or not isinstance(row.get("onCreateWhenComplete"), bool)
            or not isinstance(row.get("onBuildComplete"), bool)
            or not (
                row.get("onCreateWhenComplete")
                or row.get("onBuildComplete")
            )
            or row.get("module") != "GrantUpgradeCreate"
            or not isinstance(row.get("sourceIni"), str)
            or not isinstance(row.get("line"), int)
        ):
            raise PlayableStructureCompilerError(
                "structure descriptor create grant row is invalid"
            )
    production_exit_updates = gameplay.get("productionExitUpdates", [])
    if not isinstance(production_exit_updates, list):
        raise PlayableStructureCompilerError(
            "structure descriptor production exit updates are invalid"
        )
    production_exit_source_paths: set[str] = set()
    if production_exit_updates:
        source_documents = value.get("sourceDocuments")
        if not isinstance(source_documents, list):
            raise PlayableStructureCompilerError(
                "structure descriptor production exit source evidence is missing"
            )
        production_exit_source_hashes: dict[str, str] = {}
        for source in source_documents:
            if (
                not isinstance(source, Mapping)
                or set(source) != {"virtualPath", "sha256"}
                or not isinstance(source.get("virtualPath"), str)
                or not source.get("virtualPath")
                or not isinstance(source.get("sha256"), str)
                or re.fullmatch(r"[0-9a-f]{64}", str(source.get("sha256")))
                is None
            ):
                raise PlayableStructureCompilerError(
                    "structure descriptor production exit source document "
                    "schema is invalid"
                )
            normalized_path = (
                str(source["virtualPath"]).replace("\\", "/").casefold()
            )
            if normalized_path in production_exit_source_hashes:
                if (
                    production_exit_source_hashes[normalized_path]
                    != source["sha256"]
                ):
                    raise PlayableStructureCompilerError(
                        "structure descriptor production exit source document "
                        "hashes contradict"
                    )
                raise PlayableStructureCompilerError(
                    "structure descriptor production exit source document "
                    "path is duplicated"
                )
            production_exit_source_hashes[normalized_path] = str(
                source["sha256"]
            )
        production_exit_source_paths = set(production_exit_source_hashes)

    def require_production_exit_source(source_ini: object) -> bool:
        return (
            isinstance(source_ini, str)
            and bool(source_ini)
            and source_ini.replace("\\", "/").casefold()
            in production_exit_source_paths
        )

    for row in production_exit_updates:
        if (
            not isinstance(row, Mapping)
            or set(row)
            != {
                "module",
                "unitCreatePoint",
                "naturalRallyPoint",
                "exitDelay",
                "allowAirborneCreation",
                "initialBurst",
                "deferredFields",
                "runtimeStatus",
                "sourceIni",
                "line",
            }
        ):
            raise PlayableStructureCompilerError(
                "structure descriptor production exit update row is invalid"
            )
        for field_name in ("unitCreatePoint", "naturalRallyPoint"):
            field = row.get(field_name)
            coordinates = (
                field.get("value") if isinstance(field, Mapping) else None
            )
            authored_coordinates: Mapping[str, float] | None = None
            if (
                isinstance(field, Mapping)
                and isinstance(field.get("authored"), str)
                and field.get("authored")
            ):
                try:
                    authored_coordinates = _queue_exit_coord_values(
                        str(field["authored"]),
                        f"structure descriptor {field_name}",
                    )
                except PlayableStructureCompilerError:
                    authored_coordinates = None
            if (
                not isinstance(field, Mapping)
                or (
                    set(field)
                    not in (
                        {"authored", "value", "defaulted"},
                        {"authored", "value", "sourceIni", "line"},
                    )
                )
                or not isinstance(field.get("authored"), str)
                or not isinstance(coordinates, Mapping)
                or set(coordinates) != {"x", "y", "z"}
                or any(
                    not isinstance(coordinates[axis], float)
                    for axis in ("x", "y", "z")
                )
                or (
                    "defaulted" in field
                    and field.get("defaulted") is not True
                )
                or (
                    field.get("defaulted") is True
                    and (
                        field.get("authored") != ""
                        or dict(coordinates)
                        != {"x": 0.0, "y": 0.0, "z": 0.0}
                    )
                )
                or (
                    "defaulted" not in field
                    and (
                        not field.get("authored")
                        or authored_coordinates is None
                        or any(
                            float(coordinates[axis])
                            != float(authored_coordinates[axis])
                            for axis in ("x", "y", "z")
                        )
                        or not require_production_exit_source(
                            field.get("sourceIni")
                        )
                        or not isinstance(field.get("line"), int)
                        or isinstance(field.get("line"), bool)
                        or int(field["line"]) <= 0
                    )
                )
            ):
                raise PlayableStructureCompilerError(
                    "structure descriptor production exit coordinate is invalid"
                )
        for field_name, value_type in (
            ("exitDelay", int),
            ("initialBurst", int),
            ("allowAirborneCreation", bool),
        ):
            field = row.get(field_name)
            authored_unsigned_matches = True
            if isinstance(field, Mapping) and value_type is int:
                authored = field.get("authored")
                stored = field.get("value")
                resolved_define = field.get("resolvedDefine")
                if (
                    isinstance(authored, str)
                    and re.fullmatch(r"[0-9]+", authored)
                ):
                    authored_unsigned_matches = (
                        resolved_define is None
                        and not isinstance(stored, bool)
                        and isinstance(stored, int)
                        and int(authored) == stored
                    )
                elif (
                    isinstance(authored, str)
                    and re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", authored)
                    and isinstance(resolved_define, Mapping)
                ):
                    authored_unsigned_matches = (
                        set(resolved_define) == {"name", "value"}
                        and resolved_define.get("name") == authored
                        and not isinstance(resolved_define.get("value"), bool)
                        and isinstance(resolved_define.get("value"), int)
                        and resolved_define.get("value") == stored
                    )
                else:
                    authored_unsigned_matches = False
            if (
                not isinstance(field, Mapping)
                or (
                    set(field)
                    not in (
                        (
                            {"authored", "value", "defaulted", "unit"}
                            if field_name == "exitDelay"
                            else {"authored", "value", "defaulted"}
                        ),
                        (
                            {"authored", "value", "sourceIni", "line", "unit"}
                            if field_name == "exitDelay"
                            else {"authored", "value", "sourceIni", "line"}
                        ),
                        (
                            {
                                "authored",
                                "value",
                                "sourceIni",
                                "line",
                                "unit",
                                "resolvedDefine",
                            }
                            if field_name == "exitDelay"
                            else {
                                "authored",
                                "value",
                                "sourceIni",
                                "line",
                                "resolvedDefine",
                            }
                        ),
                    )
                )
                or not isinstance(field.get("authored"), str)
                or (
                    value_type is int
                    and (
                        not isinstance(field.get("value"), int)
                        or isinstance(field.get("value"), bool)
                    )
                )
                or (
                    value_type is bool
                    and not isinstance(field.get("value"), bool)
                )
                or (
                    "defaulted" in field
                    and field.get("defaulted") is not True
                )
                or (value_type is bool and "resolvedDefine" in field)
                or not authored_unsigned_matches
                or (
                    value_type is int
                    and (
                        isinstance(field.get("value"), bool)
                        or int(field["value"]) < 0
                        or int(field["value"]) > 4_294_967_295
                    )
                )
                or (
                    field_name == "exitDelay"
                    and field.get("unit") != "milliseconds"
                )
                or (
                    field_name != "exitDelay"
                    and "unit" in field
                )
                or (
                    field.get("defaulted") is True
                    and (
                        (
                            field_name == "allowAirborneCreation"
                            and (
                                field.get("authored") != "No"
                                or field.get("value") is not False
                            )
                        )
                        or (
                            field_name != "allowAirborneCreation"
                            and (
                                field.get("authored") != "0"
                                or int(field.get("value", -1)) != 0
                            )
                        )
                    )
                )
                or (
                    "defaulted" not in field
                    and (
                        not field.get("authored")
                        or (
                            field_name == "allowAirborneCreation"
                            and (
                                str(field.get("authored", ""))
                                .strip()
                                .casefold()
                                not in {"yes", "no"}
                                or bool(field.get("value"))
                                != (
                                    str(field.get("authored", ""))
                                    .strip()
                                    .casefold()
                                    == "yes"
                                )
                            )
                        )
                        or not require_production_exit_source(
                            field.get("sourceIni")
                        )
                        or not isinstance(field.get("line"), int)
                        or isinstance(field.get("line"), bool)
                        or int(field["line"]) <= 0
                    )
                )
            ):
                raise PlayableStructureCompilerError(
                    "structure descriptor production exit scalar is invalid"
                )
        deferred_fields = row.get("deferredFields")
        if not isinstance(deferred_fields, list):
            raise PlayableStructureCompilerError(
                "structure descriptor production exit deferred fields are invalid"
            )
        seen_deferred: set[str] = set()
        for deferred in deferred_fields:
            name = (
                str(deferred.get("name", "")).casefold()
                if isinstance(deferred, Mapping)
                else ""
            )
            if (
                not isinstance(deferred, Mapping)
                or set(deferred)
                != {"name", "authored", "sourceIni", "line", "reason"}
                or name not in _QUEUE_EXIT_DEFERRED_FIELDS
                or name in seen_deferred
                or not isinstance(deferred.get("authored"), str)
                or not deferred.get("authored")
                or not require_production_exit_source(
                    deferred.get("sourceIni")
                )
                or not isinstance(deferred.get("line"), int)
                or isinstance(deferred.get("line"), bool)
                or int(deferred["line"]) <= 0
                or deferred.get("reason")
                != "bfme-field-without-local-runtime-oracle"
            ):
                raise PlayableStructureCompilerError(
                    "structure descriptor production exit deferred field is invalid"
                )
            seen_deferred.add(name)
        if (
            row.get("module") != "QueueProductionExitUpdate"
            or row.get("runtimeStatus") != "deferred"
            or not require_production_exit_source(row.get("sourceIni"))
            or not isinstance(row.get("line"), int)
            or isinstance(row.get("line"), bool)
            or int(row["line"]) <= 0
        ):
            raise PlayableStructureCompilerError(
                "structure descriptor production exit update row is invalid"
            )
    module_contracts = gameplay.get("moduleContracts", [])
    try:
        validate_module_contracts(
            module_contracts, label="structure descriptor"
        )
    except ModuleContractError as error:
        raise PlayableStructureCompilerError(str(error)) from error
    auto_deposit_updates = gameplay.get("autoDepositUpdates", [])
    if not isinstance(auto_deposit_updates, list):
        raise PlayableStructureCompilerError(
            "structure descriptor auto-deposit updates are invalid"
        )
    auto_deposit_source_paths: set[str] = set()
    if auto_deposit_updates:
        source_documents = value.get("sourceDocuments")
        if not isinstance(source_documents, list):
            raise PlayableStructureCompilerError(
                "structure descriptor auto-deposit source evidence is missing"
            )
        auto_deposit_source_paths = {
            str(source.get("virtualPath", "")).replace("\\", "/").casefold()
            for source in source_documents
            if isinstance(source, Mapping)
            and isinstance(source.get("sha256"), str)
            and re.fullmatch(r"[0-9a-f]{64}", str(source.get("sha256")))
        }

    def require_auto_deposit_source(source_ini: object) -> bool:
        return (
            isinstance(source_ini, str)
            and bool(source_ini)
            and source_ini.replace("\\", "/").casefold()
            in auto_deposit_source_paths
        )

    def valid_auto_deposit_integer(
        field: object, *, unsigned: bool = False
    ) -> bool:
        if not isinstance(field, Mapping):
            return False
        allowed = (
            {"authored", "value", "defaulted"},
            {"authored", "value", "sourceIni", "line"},
            {
                "authored",
                "value",
                "sourceIni",
                "line",
                "resolvedDefine",
            },
        )
        if set(field) not in allowed:
            return False
        value_field = field.get("value")
        if not isinstance(value_field, int) or isinstance(value_field, bool):
            return False
        if unsigned:
            if not 0 <= value_field <= 4_294_967_295:
                return False
        elif not -2_147_483_648 <= value_field <= 2_147_483_647:
            return False
        if field.get("defaulted") is True:
            return (
                set(field) == {"authored", "value", "defaulted"}
                and field.get("authored") == str(value_field)
            )
        if (
            not isinstance(field.get("authored"), str)
            or not field.get("authored")
            or not require_auto_deposit_source(field.get("sourceIni"))
            or not isinstance(field.get("line"), int)
            or isinstance(field.get("line"), bool)
            or int(field["line"]) <= 0
        ):
            return False
        authored = str(field["authored"]).strip()
        if re.fullmatch(r"[+-]?[0-9]+", authored):
            return "resolvedDefine" not in field and int(authored) == value_field
        resolved = field.get("resolvedDefine")
        return (
            re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", authored) is not None
            and isinstance(resolved, Mapping)
            and set(resolved) == {"name", "value"}
            and resolved.get("name") == authored
            and resolved.get("value") == value_field
        )

    for row in auto_deposit_updates:
        if (
            not isinstance(row, Mapping)
            or set(row)
            != {
                "module",
                "depositTiming",
                "depositAmount",
                "initialCaptureBonus",
                "actualMoney",
                "upgradedBoosts",
                "deferredFields",
                "runtimeStatus",
                "sourceIni",
                "line",
            }
            or row.get("module") != "AutoDepositUpdate"
            or row.get("runtimeStatus") not in {"executable", "deferred"}
            or not require_auto_deposit_source(row.get("sourceIni"))
            or not isinstance(row.get("line"), int)
            or isinstance(row.get("line"), bool)
            or int(row["line"]) <= 0
        ):
            raise PlayableStructureCompilerError(
                "structure descriptor auto-deposit update row is invalid"
            )
        timing = row.get("depositTiming")
        if (
            not isinstance(timing, Mapping)
            or set(timing)
            not in (
                {
                    "authored",
                    "value",
                    "defaulted",
                    "unit",
                    "simulationTicks",
                },
                {
                    "authored",
                    "value",
                    "sourceIni",
                    "line",
                    "unit",
                    "simulationTicks",
                },
                {
                    "authored",
                    "value",
                    "sourceIni",
                    "line",
                    "resolvedDefine",
                    "unit",
                    "simulationTicks",
                },
            )
            or timing.get("unit") != "milliseconds"
        ):
            raise PlayableStructureCompilerError(
                "structure descriptor auto-deposit timing is invalid"
            )
        timing_integer = dict(timing)
        timing_integer.pop("unit")
        simulation_ticks = timing_integer.pop("simulationTicks", None)
        if (
            not valid_auto_deposit_integer(timing_integer, unsigned=True)
            or not isinstance(simulation_ticks, int)
            or isinstance(simulation_ticks, bool)
            or simulation_ticks
            != (
                (int(timing["value"]) + 99) // 100
                if int(timing["value"]) > 0
                else 0
            )
        ):
            raise PlayableStructureCompilerError(
                "structure descriptor auto-deposit timing is invalid"
            )
        if not valid_auto_deposit_integer(row.get("depositAmount")) or not valid_auto_deposit_integer(
            row.get("initialCaptureBonus")
        ):
            raise PlayableStructureCompilerError(
                "structure descriptor auto-deposit amount is invalid"
            )
        for default_field, expected_value in (
            (timing, 0),
            (row.get("depositAmount"), 0),
            (row.get("initialCaptureBonus"), 0),
        ):
            if (
                isinstance(default_field, Mapping)
                and default_field.get("defaulted") is True
                and (
                    default_field.get("value") != expected_value
                    or default_field.get("authored") != str(expected_value)
                )
            ):
                raise PlayableStructureCompilerError(
                    "structure descriptor auto-deposit default is invalid"
                )
        actual_money = row.get("actualMoney")
        if not isinstance(actual_money, Mapping) or set(actual_money) not in (
            {"authored", "value", "defaulted"},
            {"authored", "value", "sourceIni", "line"},
        ):
            raise PlayableStructureCompilerError(
                "structure descriptor auto-deposit ActualMoney is invalid"
            )
        if (
            not isinstance(actual_money.get("value"), bool)
            or str(actual_money.get("authored", "")).strip().casefold()
            not in {"yes", "no"}
            or bool(actual_money["value"])
            != (str(actual_money["authored"]).strip().casefold() == "yes")
            or (
                actual_money.get("defaulted") is True
                and (
                    set(actual_money) != {"authored", "value", "defaulted"}
                    or actual_money.get("authored") != "Yes"
                    or actual_money.get("value") is not True
                )
            )
            or (
                "defaulted" not in actual_money
                and (
                    not require_auto_deposit_source(
                        actual_money.get("sourceIni")
                    )
                    or not isinstance(actual_money.get("line"), int)
                    or isinstance(actual_money.get("line"), bool)
                    or int(actual_money["line"]) <= 0
                )
            )
        ):
            raise PlayableStructureCompilerError(
                "structure descriptor auto-deposit ActualMoney is invalid"
            )
        boosts = row.get("upgradedBoosts")
        if not isinstance(boosts, list):
            raise PlayableStructureCompilerError(
                "structure descriptor auto-deposit boosts are invalid"
            )
        for boost in boosts:
            if (
                not isinstance(boost, Mapping)
                or set(boost)
                != {
                    "upgradeId",
                    "upgradeType",
                    "upgradeAttestation",
                    "boost",
                    "authored",
                    "sourceIni",
                    "line",
                }
                or not isinstance(boost.get("upgradeId"), str)
                or not boost.get("upgradeId")
                or boost.get("upgradeType") != "PLAYER"
                or not isinstance(boost.get("upgradeAttestation"), Mapping)
                or not isinstance(boost.get("boost"), int)
                or isinstance(boost.get("boost"), bool)
                or _AUTO_DEPOSIT_UPGRADE_PAIR_PATTERN.fullmatch(
                    str(boost.get("authored", ""))
                )
                is None
                or not require_auto_deposit_source(boost.get("sourceIni"))
                or not isinstance(boost.get("line"), int)
                or isinstance(boost.get("line"), bool)
                or int(boost["line"]) <= 0
                or UPGRADE_PATH.casefold() not in auto_deposit_source_paths
            ):
                raise PlayableStructureCompilerError(
                    "structure descriptor auto-deposit boost is invalid"
                )
            authored_match = _AUTO_DEPOSIT_UPGRADE_PAIR_PATTERN.fullmatch(
                str(boost["authored"])
            )
            attestation = boost["upgradeAttestation"]
            attested_source_hashes = {
                str(source.get("sha256", ""))
                for source in value.get("sourceDocuments", [])
                if isinstance(source, Mapping)
                and str(source.get("virtualPath", ""))
                .replace("\\", "/")
                .casefold()
                == UPGRADE_PATH.casefold()
            }
            if (
                authored_match is None
                or authored_match.group(1) != boost["upgradeId"]
                or int(authored_match.group(2)) != boost["boost"]
                or set(attestation)
                != {
                    "upgradeId",
                    "upgradeType",
                    "sourceIni",
                    "sourceSha256",
                }
                or attestation.get("upgradeId") != boost["upgradeId"]
                or attestation.get("upgradeType") != "PLAYER"
                or str(attestation.get("sourceIni", "")).casefold()
                != UPGRADE_PATH.casefold()
                or attestation.get("sourceSha256") not in attested_source_hashes
            ):
                raise PlayableStructureCompilerError(
                    "structure descriptor auto-deposit boost projection is invalid"
                )
        deferred = row.get("deferredFields")
        if not isinstance(deferred, list):
            raise PlayableStructureCompilerError(
                "structure descriptor auto-deposit deferred fields are invalid"
            )
        for field in deferred:
            if (
                not isinstance(field, Mapping)
                or set(field)
                != {"name", "authored", "sourceIni", "line", "reason"}
                or str(field.get("name", "")).casefold()
                not in _AUTO_DEPOSIT_DEFERRED_FIELDS
                or not isinstance(field.get("authored"), str)
                or not field.get("authored")
                or not require_auto_deposit_source(field.get("sourceIni"))
                or not isinstance(field.get("line"), int)
                or isinstance(field.get("line"), bool)
                or int(field["line"]) <= 0
                or field.get("reason")
                != "bfme-field-without-local-runtime-oracle"
            ):
                raise PlayableStructureCompilerError(
                    "structure descriptor auto-deposit deferred field is invalid"
                )
        if (row.get("runtimeStatus") == "executable") != (not deferred):
            raise PlayableStructureCompilerError(
                "structure descriptor auto-deposit runtime status is invalid"
            )
    inherit_upgrades = gameplay.get("inheritUpgradesOnCreate", [])
    if not isinstance(inherit_upgrades, list):
        raise PlayableStructureCompilerError(
            "structure descriptor inherited upgrades are invalid"
        )
    for row in inherit_upgrades:
        radius = row.get("radius") if isinstance(row, Mapping) else None
        if (
            not isinstance(row, Mapping)
            or not isinstance(radius, Mapping)
            or not isinstance(radius.get("authored"), str)
            or not radius.get("authored")
            or not isinstance(radius.get("value"), (int, float))
            or isinstance(radius.get("value"), bool)
            or float(radius["value"]) <= 0.0
            or not isinstance(row.get("upgradeId"), str)
            or not row.get("upgradeId")
            or row.get("upgradeType") != "OBJECT"
            or not isinstance(row.get("sourceObjectId"), str)
            or not row.get("sourceObjectId")
            or row.get("objectFilter")
            != "ANY +%s" % row.get("sourceObjectId")
            or row.get("module") != "InheritUpgradeCreate"
            or not isinstance(row.get("sourceIni"), str)
            or not row.get("sourceIni")
            or not isinstance(row.get("line"), int)
            or isinstance(row.get("line"), bool)
            or int(row["line"]) <= 0
        ):
            raise PlayableStructureCompilerError(
                "structure descriptor inherited upgrade row is invalid"
            )
    if inherit_upgrades:
        source_documents = value.get("sourceDocuments")
        if not isinstance(source_documents, list):
            raise PlayableStructureCompilerError(
                "structure descriptor inherited upgrade sources are invalid"
            )
        source_paths = {
            str(row.get("virtualPath", "")).replace("\\", "/").casefold()
            for row in source_documents
            if isinstance(row, Mapping)
            and isinstance(row.get("sha256"), str)
            and re.fullmatch(r"[0-9a-f]{64}", str(row.get("sha256")))
        }
        if UPGRADE_PATH.casefold() not in source_paths or any(
            str(row["sourceIni"]).replace("\\", "/").casefold()
            not in source_paths
            for row in inherit_upgrades
        ):
            raise PlayableStructureCompilerError(
                "structure descriptor inherited upgrade source evidence is missing"
            )


__all__ = [
    "PlayableStructureCompilerError",
    "SCHEMA",
    "SCHEMA_VERSION",
    "STRUCTURE_KIND_TOKENS",
    "compile_playable_structure_descriptor",
    "validate_playable_structure_descriptor",
]
