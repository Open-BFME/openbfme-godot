"""Compile BFME2 retail references into one playable-structure descriptor.

Structures share the playable-unit corpus preparation but classify by the
STRUCTURE KindOf family, produce through authored construct or wall-upgrade
commands instead of UNIT_BUILD sockets, and carry lifecycle health facts
instead of unit core animation states.  This module is object-name agnostic;
engine-spawned fortress composites are admitted only through the caller-owned
implicit-root policy.
"""

from __future__ import annotations

from collections import defaultdict
from collections.abc import Iterable, Mapping, Sequence
import hashlib
import json
import re

from .playable_unit_compiler import (
    ATTRIBUTE_MODIFIER_PATH,
    EXPERIENCE_LEVELS_PATH,
    UPGRADE_PATH,
    PlayableUnitCompilerError,
    PlayableUnitCompilerInputs,
    _ability_list_defines,
    _ancestry,
    _audio_routes,
    _block_values,
    _command_slots,
    _effective_top_blocks,
    _effective_values,
    _experience_level_rows,
    _first,
    _kind_of,
    _level_modifier_leaf,
    _named_blocks,
    _optional_document,
    _scalar_fields,
    _select_experience_chain,
    _tokens,
    _walk_blocks,
    prepare_playable_unit_compiler,
)
from .sage_cst import SageBlock, SageObject
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
_HEALTH_FIELDS = ("MaxHealth", "MaxHealthDamaged", "MaxHealthReallyDamaged")


class PlayableStructureCompilerError(ValueError):
    """The requested structure descriptor cannot be derived without guessing."""


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
                    if token.startswith(("Upgrade_", "SCIENCE_"))
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
                    if token.startswith("Upgrade_")
                },
                key=str.casefold,
            )
            prerequisites = sorted(
                {
                    token
                    for field in ("NeededUpgrade", "Options")
                    for value in _block_values(button, field)
                    for token in _tokens(value)
                    if token.startswith(("Upgrade_", "SCIENCE_"))
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
    return {"primary": primary, "evidence": bodies}

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
            entry: dict[str, object] = {
                "upgradeId": upgrade_id,
                "commandId": command_id,
                "commandSetId": set_id,
                "slot": slot,
                "cancelable": "cancelable" in options,
                "neededUpgradeAny": any(
                    value.strip().casefold() in {"yes", "true", "1"}
                    for value in button.values("NeededUpgradeAny")
                ),
            }
            if needed:
                entry["neededUpgradeIds"] = needed
            for field, output_key in (
                ("TextLabel", "labelId"),
                ("DescriptLabel", "tooltipId"),
                ("ButtonImage", "buttonImageId"),
                ("LacksPrerequisiteLabel", "lacksPrerequisiteLabelId"),
            ):
                for value in button.values(field):
                    text = value.strip()
                    if text and text.casefold() not in {"none", "null"}:
                        entry[output_key] = text
                        break
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
    return {
        "upgrades": rows,
        "sourceIni": sorted(
            {UPGRADE_PATH, "data/ini/commandset.ini", "data/ini/commandbutton.ini"},
            key=str.casefold,
        ),
    }


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
            if not triggers or not applied or percent_raw is None:
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
        if level_cap is None or not re.fullmatch(r"[0-9]+", level_cap.strip()):
            raise PlayableStructureCompilerError(
                f"{label} LevelUpUpgrade has no valid LevelCap"
            )
        level_up_modules.append(
            (triggers, int(levels_to_gain.strip()), int(level_cap.strip()), block)
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

    def _purchase_button(set_id: str) -> tuple[str, str, int, object] | None:
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
            if any(
                upgrade_id.casefold() == trigger.casefold()
                for triggers, _gain, _cap, _block in level_up_modules
                for trigger in triggers
            ):
                return (command_id, upgrade_id, int(slot_row.get("slot", 0)), button)
        return None

    steps: list[dict[str, object]] = []
    consumed: set[str] = set()
    current_set = direct_sets[0]
    current_level = 1
    level_cap = max(cap for _t, _g, cap, _b in level_up_modules)
    while True:
        found = _purchase_button(current_set)
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
        to_command_set = _transition_for(folded_upgrade)
        if to_command_set is None:
            raise PlayableStructureCompilerError(
                f"{label} upgrade {upgrade_id} has no CommandSetUpgrade transition"
            )
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
        current_level = min(cap, current_level + gain)
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
    chain_upgrade_ids = {
        str(step["upgradeId"]).casefold() for step in steps
    } | {"upgrade_structurelevel1"}
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
    research = _research_surface(
        target.name,
        lineage,
        trained,
        documents,
        prepared.command_buttons,
        prepared.numeric_defines,
    )
    upgrade_effects = _upgrade_effects(
        target.name,
        lineage,
        documents,
        prepared.numeric_defines,
    )
    resource_behavior = _resource_behavior_radius(
        lineage, prepared.numeric_defines, target.name
    )
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
            {str(path) for path in research.get("sourceIni", [])}
            if research is not None
            else set()
        )
        | (
            {str(path) for path in upgrade_effects.get("sourceIni", [])}
            if upgrade_effects is not None
            else set()
        ),
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
                {"upgradeEffects": upgrade_effects}
                if upgrade_effects is not None
                else {}
            ),
            **(
                {"resourceBehavior": resource_behavior}
                if resource_behavior is not None
                else {}
            ),
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
    gameplay = value.get("gameplay")
    if not isinstance(gameplay, Mapping):
        raise PlayableStructureCompilerError(
            "structure descriptor gameplay is invalid"
        )
    if gameplay.get("health") is None and "BASE_FOUNDATION" not in {
        str(item) for item in kinds
    }:
        raise PlayableStructureCompilerError(
            "structure descriptor omits health without foundation evidence"
        )


__all__ = [
    "PlayableStructureCompilerError",
    "SCHEMA",
    "SCHEMA_VERSION",
    "STRUCTURE_KIND_TOKENS",
    "compile_playable_structure_descriptor",
    "validate_playable_structure_descriptor",
]
