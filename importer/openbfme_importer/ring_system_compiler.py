"""Compile RotWK's authored One Ring system without publishing retail bytes.

The output is a semantic runtime contract.  It deliberately contains no INI
text and no converted art; the latter is an exact conversion plan consumed by
the ordinary unit-model pack lane during a later, explicitly authorised cook.
"""

from __future__ import annotations

import hashlib
import json
import re
from collections.abc import Mapping, Sequence

from .playable_unit_compiler import (
    _ancestry,
    _effective_top_blocks,
    _effective_values,
    _resolved_expression,
    _tokens,
    prepare_playable_unit_compiler,
)
from .sage_cst import (
    SageBlock,
    SageObject,
    parse_sage_body_fragment,
    parse_sage_document,
)
from .sage_ini import IniBlock, parse_flat_named_blocks


SCHEMA = "openbfme.ring-system-descriptor"
SCHEMA_VERSION = 0
RUNTIME_SCHEMA = "openbfme.ring-system-runtime"
RUNTIME_SCHEMA_VERSION = 0

NEUTRAL_GOLLUM = "NeutralGollum"
RING_GOLLUM = "NeutralGollum_RingHero"
DROPPED_RING = "TheDroppedRing"
RING_UPGRADES = ("Upgrade_RingHero", "Upgrade_FortressRingHero")
RING_EVA_EVENTS = (
    "GollumSeen",
    "DiscoveredRing",
    "RingPickedUpLocal",
    "RingPickedUpEnemy",
    "LocalPlayerLosesRing",
    "LocalPlayerGainsRing",
    "AlliedPlayerGainsRing",
    "EnemyPlayerGainsRing",
)
FACTION_TRIGGER_UPGRADES = (
    "Upgrade_AngmarFaction",
    "Upgrade_IsengardFaction",
    "Upgrade_MordorFaction",
    "Upgrade_WildFaction",
    "Upgrade_MenFaction",
    "Upgrade_ElfFaction",
    "Upgrade_DwarfFaction",
)


class RingSystemCompilerError(ValueError):
    """The ring contract cannot be derived from the effective INI view."""


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


def _document(documents: Mapping[str, bytes], virtual_path: str) -> bytes:
    wanted = virtual_path.casefold()
    matches = [
        payload
        for path, payload in documents.items()
        if path.replace("\\", "/").casefold() == wanted
    ]
    if len(matches) != 1:
        raise RingSystemCompilerError(
            f"required effective source must resolve exactly once: {virtual_path}"
        )
    return matches[0]


def _named_blocks(source: bytes, kind: str) -> dict[str, IniBlock]:
    try:
        rows = parse_flat_named_blocks(source, kind)
    except ValueError as exc:
        raise RingSystemCompilerError(str(exc)) from exc
    result: dict[str, IniBlock] = {}
    for row in rows:
        key = row.name.casefold()
        if key in result:
            raise RingSystemCompilerError(f"duplicate {kind}: {row.name}")
        result[key] = row
    return result


def _required_object(objects: Mapping[str, SageObject], object_id: str) -> SageObject:
    row = objects.get(object_id.casefold())
    if row is None:
        raise RingSystemCompilerError(f"effective Object is missing: {object_id}")
    return row


def _lineage(objects: Mapping[str, SageObject], object_id: str) -> tuple[SageObject, ...]:
    try:
        return _ancestry(objects, _required_object(objects, object_id))
    except ValueError as exc:
        raise RingSystemCompilerError(str(exc)) from exc


def _modules(
    lineage: Sequence[SageObject], kind: str, *, header_key: str | None = None
) -> list[SageBlock]:
    folded = kind.casefold()
    header = header_key.casefold() if header_key else None
    return [
        row
        for row in _effective_top_blocks(lineage)
        if row.kind.casefold() == folded
        and (header is None or (row.header_key or "").casefold() == header)
    ]


def _one_module(
    lineage: Sequence[SageObject], kind: str, *, header_key: str | None = None
) -> SageBlock:
    rows = _modules(lineage, kind, header_key=header_key)
    if len(rows) != 1:
        raise RingSystemCompilerError(
            f"{lineage[-1].name} must author exactly one {kind} module"
        )
    return rows[0]


def _value(block: SageBlock, key: str) -> str:
    values = block.values(key)
    if len(values) != 1 or not values[0].strip():
        raise RingSystemCompilerError(
            f"{block.kind} must author exactly one nonempty {key}"
        )
    return values[0].strip()


def _object_value(lineage: Sequence[SageObject], key: str) -> str:
    values = _effective_values(lineage, key)
    if len(values) != 1 or not values[0].value.strip():
        raise RingSystemCompilerError(
            f"{lineage[-1].name} must author exactly one effective {key}"
        )
    return values[0].value.strip()


def _number(value: str, label: str) -> int | float:
    text = value.strip().rstrip("%")
    try:
        result = float(text)
    except ValueError as exc:
        raise RingSystemCompilerError(f"{label} is not numeric: {value}") from exc
    return int(result) if result.is_integer() else result


def _resolved_number(
    expression: str, constants: Mapping[str, int | float], label: str
) -> int | float:
    value = _resolved_expression(expression, constants)
    if value is None:
        raise RingSystemCompilerError(f"{label} is unresolved: {expression}")
    return value


def _nested_blocks(block: SageBlock, kind: str) -> list[SageBlock]:
    folded = kind.casefold()
    result: list[SageBlock] = []

    def visit(row: SageBlock) -> None:
        for child in row.blocks:
            if child.kind.casefold() == folded:
                result.append(child)
            visit(child)

    visit(block)
    return result


def _compile_gollum(
    objects: Mapping[str, SageObject], armor_source: bytes
) -> tuple[dict[str, object], dict[str, object]]:
    lineage = _lineage(objects, NEUTRAL_GOLLUM)
    body = _one_module(lineage, "ActiveBody", header_key="Body")
    invisibility = _one_module(lineage, "InvisibilityUpdate", header_key="Behavior")
    nuggets = _nested_blocks(invisibility, "InvisibilityNugget")
    if len(nuggets) != 1:
        raise RingSystemCompilerError("NeutralGollum must author one InvisibilityNugget")
    locomotors: dict[str, int | float] = {}
    for row in _modules(lineage, "LocomotorSet"):
        condition = _value(row, "Condition").casefold()
        key = {"set_normal": "normal", "set_wander": "wander"}.get(condition)
        if key is not None:
            locomotors[key] = _number(_value(row, "Speed"), f"Gollum {key} speed")
    if set(locomotors) != {"normal", "wander"}:
        raise RingSystemCompilerError("NeutralGollum locomotors are incomplete")

    armor_id = _value(_one_module(lineage, "ArmorSet"), "Armor")
    armors = _named_blocks(armor_source, "Armor")
    armor = armors.get(armor_id.casefold())
    if armor is None:
        raise RingSystemCompilerError(f"patched armor is missing: {armor_id}")
    damage: dict[str, int | float] = {}
    for expression in armor.values("Armor"):
        tokens = expression.split()
        if len(tokens) >= 2 and tokens[0].upper() in {"MAGIC", "HERO", "HERO_RANGED"}:
            damage[tokens[0].upper()] = _number(
                tokens[1], f"{armor_id} {tokens[0].upper()}"
            )
    expected_damage = {"MAGIC": 500, "HERO": 500, "HERO_RANGED": 500}
    if damage != expected_damage:
        missing = next(
            (key for key, value in expected_damage.items() if damage.get(key) != value),
            "damage table",
        )
        raise RingSystemCompilerError(f"{armor_id} {missing} must be 500%")

    kinds = [token.upper() for token in _tokens(_object_value(lineage, "KindOf"))]
    if "NEUTRALGOLLUM" not in kinds:
        raise RingSystemCompilerError("NeutralGollum KindOf lacks NEUTRALGOLLUM")
    gollum = {
        "schema": "openbfme.playable-unit-style-object",
        "schemaVersion": 0,
        "objectId": NEUTRAL_GOLLUM,
        "side": _object_value(lineage, "Side"),
        "kindOf": kinds,
        "body": {
            "kind": "ActiveBody",
            "maxHealth": _number(_value(body, "MaxHealth"), "Gollum MaxHealth"),
        },
        "armor": {"id": armor_id, "damagePercent": damage},
        "camouflage": {
            "type": _value(nuggets[0], "InvisibilityType"),
            "detectionRange": _number(
                _value(nuggets[0], "DetectionRange"), "Gollum DetectionRange"
            ),
        },
        "locomotors": locomotors,
        "ringMechanic": {"role": "gollum", "seenEvaEvent": "GollumSeen"},
    }

    child = _lineage(objects, RING_GOLLUM)
    animal = _one_module(child, "AnimalAIUpdate", header_key="Behavior")
    drop = _one_module(child, "CreateObjectDie", header_key="Behavior")
    ring_gollum = {
        "schema": "openbfme.playable-unit-style-object",
        "schemaVersion": 0,
        "objectId": RING_GOLLUM,
        "parentObjectId": NEUTRAL_GOLLUM,
        "side": "Neutral",
        "animalAI": {
            output: _number(_value(animal, source), f"Ring Gollum {source}")
            for source, output in (
                ("FleeRange", "fleeRange"),
                ("FleeDistance", "fleeDistance"),
                ("WanderPercentage", "wanderPercentage"),
                ("MaxWanderDistance", "maxWanderDistance"),
            )
        },
        "onDeath": {"createObjectList": _value(drop, "CreationList")},
        "ringMechanic": {"role": "ring-gollum", "dropsRingOnDeath": True},
    }
    if ring_gollum["onDeath"]["createObjectList"] != "OCL_TheOneRing":
        raise RingSystemCompilerError("NeutralGollum_RingHero must drop OCL_TheOneRing")
    return gollum, ring_gollum


def _compile_filter(expression: str) -> dict[str, object]:
    tokens = expression.split()
    modes: list[str] = []
    include: list[str] = []
    exclude: list[str] = []
    predicates: list[str] = []
    for token in tokens:
        if token.upper() in {"ANY", "NONE"}:
            modes.append(token.upper())
            continue
        if token.startswith("+"):
            include.append(token[1:])
        elif token.startswith("-"):
            exclude.append(token[1:])
        else:
            predicates.append(token)
    if "NEUTRALGOLLUM" not in {value.upper() for value in exclude}:
        raise RingSystemCompilerError("TheDroppedRing ObjectFilter must exclude -NEUTRALGOLLUM")
    if "NOT_FLYING_UNITS" not in {value.upper() for value in predicates}:
        raise RingSystemCompilerError("TheDroppedRing ObjectFilter lacks NOT_FLYING_UNITS")
    if len(modes) != 1:
        raise RingSystemCompilerError(
            "TheDroppedRing ObjectFilter must author exactly one ANY/NONE mode"
        )
    return {
        "mode": modes[0],
        "include": include,
        "exclude": exclude,
        "predicate": predicates,
    }


def _compile_ring(objects: Mapping[str, SageObject]) -> dict[str, object]:
    lineage = _lineage(objects, DROPPED_RING)
    attach = _one_module(lineage, "AttachUpdate", header_key="Behavior")
    remove = _one_module(lineage, "RemoveUpgradeUpgrade", header_key="Behavior")
    drop = _one_module(lineage, "CreateObjectDie", header_key="Behavior")
    kind_of = [token.upper() for token in _tokens(_object_value(lineage, "KindOf"))]
    if "REVEAL_TO_ALL" not in kind_of:
        raise RingSystemCompilerError("TheDroppedRing KindOf lacks REVEAL_TO_ALL")
    triggers = list(_tokens(_value(remove, "TriggeredBy")))
    if triggers != list(FACTION_TRIGGER_UPGRADES):
        raise RingSystemCompilerError("TheDroppedRing must author seven faction triggers")
    upgrade = _value(remove, "UpgradeToRemove")
    if upgrade != "Upgrade_RingHero":
        raise RingSystemCompilerError("TheDroppedRing must remove Upgrade_RingHero")
    return {
        "schema": "openbfme.ring-object",
        "schemaVersion": 0,
        "objectId": DROPPED_RING,
        "side": _object_value(lineage, "Side"),
        "kindOf": kind_of,
        "shroudClearingRange": _number(
            _object_value(lineage, "ShroudClearingRange"), "ring ShroudClearingRange"
        ),
        "ringMechanic": {
            "attach": {
                "filter": _compile_filter(_value(attach, "ObjectFilter")),
                "scanRange": _number(_value(attach, "ScanRange"), "ring ScanRange"),
                "parentStatus": _value(attach, "ParentStatus"),
            },
            "removeUpgrade": {
                "triggerFactionUpgrades": triggers,
                "upgradeId": upgrade,
            },
            "onDeath": {"createObjectList": _value(drop, "CreationList")},
        },
    }


def _scope_lines(source: bytes, family: str, name: str) -> list[str]:
    try:
        text = source.decode("cp1252")
    except UnicodeDecodeError as exc:
        raise RingSystemCompilerError(f"{family} source has invalid encoding") from exc
    lines = text.splitlines()
    header = re.compile(rf"^\s*{re.escape(family)}\s+{re.escape(name)}\s*$", re.I)
    start = next((index for index, line in enumerate(lines) if header.match(line)), None)
    if start is None:
        raise RingSystemCompilerError(f"missing {family}: {name}")
    depth = 1
    body: list[str] = []
    for raw in lines[start + 1 :]:
        clean = raw.split(";", 1)[0].split("//", 1)[0].strip()
        if not clean:
            continue
        if clean.casefold() == "end":
            depth -= 1
            if depth == 0:
                return body
        else:
            # OCL's only nested carrier is CreateObject.  Count a line without
            # '=' as an opening block; assignments never change nesting.
            if "=" not in clean:
                depth += 1
        body.append(clean)
    raise RingSystemCompilerError(f"unterminated {family}: {name}")


def _ocl_values(source: bytes, name: str) -> dict[str, str]:
    lines = _scope_lines(source, "ObjectCreationList", name)
    values: dict[str, str] = {}
    for line in lines:
        if "=" in line:
            key, value = (part.strip() for part in line.split("=", 1))
            values[key.casefold()] = value
    return values


def _compile_ocl(source: bytes, name: str) -> dict[str, object]:
    values = _ocl_values(source, name)
    if values.get("objectnames") != DROPPED_RING or values.get("count") != "1":
        raise RingSystemCompilerError(f"{name} must create one {DROPPED_RING}")
    result: dict[str, object] = {"objectId": DROPPED_RING, "count": 1}
    if name == "OCL_TheOneRingCD":
        match = re.fullmatch(
            r"X:([-+0-9.]+)\s+Y:([-+0-9.]+)\s+Z:([-+0-9.]+)",
            values.get("offset", ""),
            re.I,
        )
        if match is None:
            raise RingSystemCompilerError("OCL_TheOneRingCD must author an XYZ Offset")
        result["offset"] = {
            axis: _number(value, f"OCL_TheOneRingCD {axis}")
            for axis, value in zip(("x", "y", "z"), match.groups(), strict=True)
        }
    return result


def _compile_spawn_contract(source: bytes) -> tuple[dict[str, str], dict[str, str]]:
    carrier = _ocl_values(source, "OCL_TheRingCarrier")
    stealer = _ocl_values(source, "OCL_TheRingStealer")
    for name, values in (("OCL_TheRingCarrier", carrier), ("OCL_TheRingStealer", stealer)):
        if values.get("count") != "1":
            raise RingSystemCompilerError(f"{name} must create exactly one object")
        if not values.get("objectnames"):
            raise RingSystemCompilerError(f"{name} has no ObjectNames")
        if not values.get("destinationplayer") or not values.get("waypointspawnpoints"):
            raise RingSystemCompilerError(f"{name} has incomplete spawn routing")
    if carrier["destinationplayer"] != stealer["destinationplayer"]:
        raise RingSystemCompilerError("ring carrier and stealer destination players differ")
    if carrier["waypointspawnpoints"] != stealer["waypointspawnpoints"]:
        raise RingSystemCompilerError("ring carrier and stealer waypoint families differ")
    return (
        {
            "objectId": carrier["objectnames"],
            "waypointFamily": carrier["waypointspawnpoints"],
            "team": carrier["destinationplayer"],
        },
        {stealer["objectnames"]: "authored-stealer-spawn-excluded-from-runtime"},
    )


def _compile_upgrades(documents: Mapping[str, bytes]) -> dict[str, object]:
    blocks = _named_blocks(_document(documents, "data/ini/upgrade.ini"), "Upgrade")
    result: dict[str, object] = {}
    for upgrade_id, expected_type in zip(RING_UPGRADES, ("PLAYER", "OBJECT"), strict=True):
        row = blocks.get(upgrade_id.casefold())
        if row is None:
            raise RingSystemCompilerError(f"missing Upgrade: {upgrade_id}")
        types = row.values("Type")
        if types != (expected_type,):
            raise RingSystemCompilerError(f"{upgrade_id} Type must be {expected_type}")
        compiled: dict[str, object] = {"type": expected_type}
        if upgrade_id == "Upgrade_RingHero":
            compiled["gainEvaEvents"] = {
                output: values[0]
                for field, output in (
                    ("LocalPlayerGainsUpgradeEvaEvent", "local"),
                    ("AlliedPlayerGainsUpgradeEvaEvent", "ally"),
                    ("EnemyPlayerGainsUpgradeEvaEvent", "enemy"),
                )
                if (values := row.values(field))
            }
            if set(compiled["gainEvaEvents"]) != {"local", "ally", "enemy"}:
                raise RingSystemCompilerError("Upgrade_RingHero lacks three gain EVA events")
        result[upgrade_id] = compiled
    return result


def _compile_routes(documents: Mapping[str, bytes]) -> dict[str, object]:
    buttons = _named_blocks(
        _document(documents, "data/ini/commandbutton.ini"), "CommandButton"
    )
    button = buttons.get("command_ringheroreviveslot")
    if button is None:
        raise RingSystemCompilerError("missing Command_RingHeroReviveSlot")
    values = button.values("NeededUpgrade")
    prerequisites = list(_tokens(values[0])) if len(values) == 1 else []
    if prerequisites != list(RING_UPGRADES):
        raise RingSystemCompilerError(
            "Command_RingHeroReviveSlot NeededUpgrade must require both ring upgrades"
        )
    return {"commandButtonId": button.name, "prerequisites": prerequisites}


def _compile_player_templates(documents: Mapping[str, bytes]) -> dict[str, list[str]]:
    templates = _named_blocks(
        _document(documents, "data/ini/playertemplate.ini"), "PlayerTemplate"
    )
    result: dict[str, list[str]] = {}
    for template in templates.values():
        values = template.values("BuildableRingHeroesMP")
        if not values:
            continue
        if len(values) != 1:
            raise RingSystemCompilerError(
                f"PlayerTemplate {template.name} has ambiguous BuildableRingHeroesMP"
            )
        heroes = list(_tokens(values[0]))
        if not heroes:
            raise RingSystemCompilerError(
                f"PlayerTemplate {template.name} has empty BuildableRingHeroesMP"
            )
        # PlayerTemplate identity, not its Side scalar, is the faction key.
        # Retail's FactionTutorial and FactionMen intentionally share Side Men
        # but author independent ring rosters.
        faction = template.name.removeprefix("Faction")
        if faction in result:
            raise RingSystemCompilerError(f"duplicate ring-hero faction: {faction}")
        result[faction] = heroes
    if not result:
        raise RingSystemCompilerError("no PlayerTemplate BuildableRingHeroesMP routes")
    return dict(sorted(result.items(), key=lambda item: item[0].casefold()))


def _own_modules(obj: SageObject, kind: str) -> list[SageBlock]:
    return [
        row
        for row in obj.blocks
        if row.kind.casefold() == kind.casefold()
        and (row.header_key or "").casefold() in {"behavior", "draw"}
    ]


def _compile_ring_hero(
    objects: Mapping[str, SageObject],
    object_id: str,
    constants: Mapping[str, int | float],
) -> dict[str, object]:
    obj = _required_object(objects, object_id)
    # These are child-specific semantics.  Reading only obj.blocks prevents a
    # parent Terrible Fury or model form from being falsely attributed here.
    remove = _own_modules(obj, "RemoveUpgradeUpgrade")
    drops = _own_modules(obj, "CreateObjectDie")
    levels = _own_modules(obj, "ExperienceLevelCreate")
    if len(remove) != 1 or len(drops) != 1 or len(levels) != 1:
        raise RingSystemCompilerError(f"{object_id} ring lifecycle modules are incomplete")
    remove_upgrades = list(_tokens(_value(remove[0], "UpgradeToRemove")))
    triggered = list(_tokens(_value(remove[0], "TriggeredBy")))
    if set(remove_upgrades) != set(RING_UPGRADES) or not triggered:
        raise RingSystemCompilerError(f"{object_id} must remove both ring upgrades on create")
    remove_all = _value(remove[0], "RemoveFromAllPlayerObjects").casefold() == "yes"
    if not remove_all:
        raise RingSystemCompilerError(f"{object_id} RemoveFromAllPlayerObjects must be Yes")
    if _value(drops[0], "CreationList") != "OCL_TheOneRing":
        raise RingSystemCompilerError(f"{object_id} must drop OCL_TheOneRing")
    level = _number(_value(levels[0], "LevelToGrant"), f"{object_id} LevelToGrant")
    if level != 10:
        raise RingSystemCompilerError(f"{object_id} LevelToGrant must be 10")

    lineage = _lineage(objects, object_id)
    build_cost = tuple(row.value for row in _effective_values(lineage, "BuildCost"))
    build_time = tuple(row.value for row in _effective_values(lineage, "BuildTime"))
    if len(build_cost) != 1 or len(build_time) != 1:
        raise RingSystemCompilerError(f"{object_id} cost fields are incomplete")
    economy: dict[str, object] = {
        "buildCost": _resolved_number(build_cost[0], constants, f"{object_id} BuildCost"),
        "buildTime": _resolved_number(build_time[0], constants, f"{object_id} BuildTime"),
    }
    bounty = tuple(row.value for row in _effective_values(lineage, "BountyValue"))
    if bounty:
        economy["bountyValue"] = _resolved_number(
            bounty[-1], constants, f"{object_id} BountyValue"
        )
    result: dict[str, object] = {
        "schema": "openbfme.playable-unit-style-object",
        "schemaVersion": 0,
        "objectId": object_id,
        "parentObjectId": obj.parent,
        "economy": economy,
        "onCreate": {
            "triggerUpgrades": triggered,
            "removeUpgrades": list(RING_UPGRADES),
            "removeFromAllPlayerObjects": True,
        },
        "onDeath": {"createObjectList": "OCL_TheOneRing"},
        "experienceLevelOnCreate": 10,
    }
    fury = [
        row
        for row in obj.blocks
        if row.kind.casefold() in {"specialpowermodule", "specialabilityupdate"}
        and "terriblefury" in (row.instance_tag or "").casefold()
    ]
    if fury:
        kinds = [row.kind for row in fury]
        if {value.casefold() for value in kinds} != {
            "specialpowermodule", "specialabilityupdate"
        }:
            raise RingSystemCompilerError(
                f"{object_id} child Terrible Fury modules are incomplete"
            )
        result["terribleFuryModules"] = [
            {"kind": row.kind, "specialPowerTemplate": _value(row, "SpecialPowerTemplate")}
            for row in fury
        ]

    condition_rows = [
        row
        for row in _own_modules(obj, "ModelConditionUpgrade")
        if "USER_1" in {
            token.upper()
            for token in _tokens(" ".join(row.values("AddConditionFlags")))
        }
    ]
    draw_models = [
        value.strip()
        for source_object in lineage
        for draw in source_object.blocks
        if draw.kind.casefold() == "w3dscriptedmodeldraw"
        for state in _nested_blocks(draw, "ModelConditionState")
        if "USER_1" in {token.upper() for token in state.header_tokens}
        for value in state.values("Model")
    ]
    if condition_rows or draw_models:
        if len(condition_rows) != 1 or len(draw_models) != 1:
            raise RingSystemCompilerError(
                f"{object_id} USER_1 model-condition swap is incomplete"
            )
        dark = condition_rows[0]
        result["darkSkin"] = {
            "triggeredBy": _value(dark, "TriggeredBy"),
            "condition": "USER_1",
            "model": draw_models[0],
        }
    return result


def _module_contract(block: SageBlock) -> dict[str, object]:
    return {
        assignment.key: assignment.value.strip()
        for assignment in block.assignments
    }


def _compile_delivery(
    documents: Mapping[str, bytes],
    objects: Mapping[str, SageObject],
    ring_creation_lists: set[str],
) -> dict[str, object]:
    structures: list[dict[str, object]] = []
    module_rows: dict[str, list[SageBlock]] = {
        "lossAnnouncement": [],
        "ringDrop": [],
        "modelCondition": [],
    }
    draw_models: set[str] = set()
    for obj in objects.values():
        citadels = _own_modules(obj, "CitadelSlaughterHordeContain")
        if not citadels:
            continue
        if len(citadels) != 1:
            raise RingSystemCompilerError(f"{obj.name} has ambiguous ring-entry modules")
        module = citadels[0]
        destroy_expression = _value(module, "ObjectToDestroyForRingEntry")
        if DROPPED_RING.casefold() not in {
            token.lstrip("+-").casefold() for token in _tokens(destroy_expression)
        }:
            # The same module class is also used by the two Palantir-shard
            # sockets.  They are not ring delivery structures.
            continue
        row: dict[str, object] = {
            "objectId": obj.name,
            "statusForRingEntry": _value(module, "StatusForRingEntry"),
            "upgradeForRingEntry": list(_tokens(_value(module, "UpgradeForRingEntry"))),
            "objectToDestroyForRingEntry": destroy_expression,
            "fxForRingEntry": _value(module, "FXForRingEntry"),
            "enterSound": _value(module, "EnterSound"),
        }
        owns_ring_drop = any(
            creation_list.casefold() in ring_creation_lists
            for drop in _own_modules(obj, "CreateObjectDie")
            for creation_list in drop.values("CreationList")
        )
        includes_ring_func = any(
            ref.relative_virtual_path.casefold().endswith("fortressringfunc.inc")
            for ref in obj.includes
        )
        row["dropsRingOnDeath"] = owns_ring_drop or includes_ring_func
        if not row["dropsRingOnDeath"]:
            row["note"] = "retail-bug-accepts-ring-never-returns-it"
        structures.append(row)
        module_rows["lossAnnouncement"].extend(_own_modules(obj, "FXListDie"))
        module_rows["ringDrop"].extend(_own_modules(obj, "CreateObjectDie"))
        module_rows["modelCondition"].extend(_own_modules(obj, "ModelConditionUpgrade"))
        for draw in obj.blocks:
            if draw.kind.casefold() != "w3dscriptedmodeldraw":
                continue
            for state in _nested_blocks(draw, "ModelConditionState"):
                if "ONE_RING" in {token.upper() for token in state.header_tokens}:
                    draw_models.update(value.strip() for value in state.values("Model"))
    include_path = "data/ini/object/fortressringfunc.inc"
    include_source = next(
        (
            payload
            for path, payload in documents.items()
            if path.replace("\\", "/").casefold() == include_path
        ),
        None,
    )
    if include_source is not None:
        try:
            fragment = parse_sage_body_fragment(include_source, include_path)
        except ValueError as exc:
            raise RingSystemCompilerError(f"{include_path} is invalid: {exc}") from exc
        include_blocks = [item for item in fragment.items if isinstance(item, SageBlock)]
        module_rows = {
            "lossAnnouncement": [row for row in include_blocks if row.kind.casefold() == "fxlistdie"],
            "ringDrop": [row for row in include_blocks if row.kind.casefold() == "createobjectdie"],
            "modelCondition": [row for row in include_blocks if row.kind.casefold() == "modelconditionupgrade"],
        }
        draw_models = {
            value.strip()
            for draw in include_blocks
            if draw.kind.casefold() == "w3dscriptedmodeldraw"
            for state in _nested_blocks(draw, "ModelConditionState")
            if "ONE_RING" in {token.upper() for token in state.header_tokens}
            for value in state.values("Model")
        }
    structures.sort(key=lambda row: str(row["objectId"]).casefold())
    if len(structures) != 26:
        raise RingSystemCompilerError(
            f"ring delivery must compile 26 active structures, got {len(structures)}"
        )
    modules: dict[str, object] = {}
    for key in ("lossAnnouncement", "modelCondition"):
        if not module_rows[key]:
            raise RingSystemCompilerError(f"fortress ring module is missing: {key}")
        modules[key] = _module_contract(module_rows[key][0])
    drops = [
        row
        for row in module_rows["ringDrop"]
        if set(
            _tokens(
                " ".join(
                    [*row.values("UpgradeRequired"), *row.values("TriggeredBy")]
                )
            )
        ) == set(RING_UPGRADES)
        and row.values("CreationList") == ("OCL_TheOneRing",)
    ]
    if not drops:
        raise RingSystemCompilerError("fortress ring-drop module is not gated on both upgrades")
    modules["ringDrop"] = {
        "requiredUpgrades": list(RING_UPGRADES),
        "createObjectList": "OCL_TheOneRing",
    }
    if draw_models != {"EXOneRing", "EXOneRing_CR"}:
        raise RingSystemCompilerError("fortress ONE_RING draw must author EXOneRing and EXOneRing_CR")
    modules["drawModels"] = sorted(draw_models, key=str.casefold)
    return {"structures": structures, "fortressModules": modules}


def _art_plan(compiled_objects: Mapping[str, object]) -> dict[str, object]:
    gollum_animations = [
        f"art/w3d/cu/cugollum_{suffix}.w3d"
        for suffix in (
            "atkb", "chra", "diea", "dieb", "gtpa", "hita",
            "idla", "idlb", "idlc", "idld", "runa",
        )
    ]
    resources: list[dict[str, object]] = [
        {
            "id": "ring-gollum-textures",
            "kind": "texture",
            "converter": "hash-only",
            "patterns": ["art/compiledtextures/hc/hc_cugollum.tga"],
            "required": True,
            "limit": 1,
            "expected_count": 1,
        },
        {
            "id": "ring-gollum-model",
            "kind": "model",
            "converter": "w3d-bundle",
            "patterns": [
                "art/w3d/cu/cugollum_skn.w3d",
                "art/w3d/cu/cugollum_skl.w3d",
                *gollum_animations,
            ],
            "output": "assets/models/units/neutralgollum/00.glb",
            "options": {
                "model": "cugollum_skn.w3d",
                "animations": [path.rsplit("/", 1)[-1] for path in gollum_animations],
                "inputResourceIds": ["ring-gollum-textures"],
            },
            "required": True,
            "limit": 13,
            "expected_count": 13,
        },
        {
            "id": "ring-object-textures",
            "kind": "texture",
            "converter": "hash-only",
            "patterns": [
                "art/compiledtextures/th/thering.dds",
                "art/compiledtextures/th/thering_nrm.tga",
            ],
            "required": True,
            "limit": 2,
            "expected_count": 2,
        },
        {
            "id": "ring-object-model",
            "kind": "model",
            "converter": "w3d-bundle",
            "patterns": ["art/w3d/th/thering.w3d"],
            "output": "assets/models/ring/thering.glb",
            "options": {
                "model": "thering.w3d",
                "animations": ["thering.w3d"],
                "inputResourceIds": ["ring-object-textures"],
            },
            "required": True,
            "limit": 1,
            "expected_count": 1,
        },
        *[
            {
                "id": f"ring-fortress-model-{index}",
                "kind": "model",
                "converter": "w3d-static",
                "patterns": [path],
                "output": f"assets/models/ring/{path.rsplit('/', 1)[-1][:-4]}.glb",
                "options": {"model": path.rsplit("/", 1)[-1], "inputResourceIds": []},
                "required": True,
                "limit": 1,
                "expected_count": 1,
            }
            for index, path in enumerate(
                ("art/w3d/ex/exonering.w3d", "art/w3d/ex/exonering_cr.w3d")
            )
        ],
    ]
    return {
        "layout": "standard-unit-model-pack",
        "gollum": {
            "sourceGame": "bfme2",
            "models": [
                "art/w3d/cu/cugollum_skn.w3d",
                "art/w3d/cu/cugollum_skl.w3d",
            ],
            "animations": gollum_animations,
            "textures": ["art/compiledtextures/hc/hc_cugollum.tga"],
        },
        "ring": {
            "models": ["art/w3d/th/thering.w3d"],
            "textures": [
                "art/compiledtextures/th/thering.dds",
                "art/compiledtextures/th/thering_nrm.tga",
            ],
        },
        "fortressRingModels": [
            "art/w3d/ex/exonering.w3d",
            "art/w3d/ex/exonering_cr.w3d",
        ],
        "conditionalHeroModels": [
            {
                "objectId": object_id,
                "condition": dark_skin["condition"],
                "model": dark_skin["model"],
                "requiredIfUnshipped": True,
            }
            for object_id, compiled in compiled_objects.items()
            if isinstance(compiled, Mapping)
            and isinstance((dark_skin := compiled.get("darkSkin")), Mapping)
        ],
        "resources": resources,
    }


def _importer_evidence(documents: Mapping[str, bytes]) -> dict[str, object]:
    sources = [
        {
            "virtualPath": path.replace("\\", "/"),
            "sha256": hashlib.sha256(payload).hexdigest(),
        }
        for path, payload in sorted(documents.items(), key=lambda item: item[0].casefold())
        if path.replace("\\", "/").casefold().startswith("data/ini/")
        and path.casefold().endswith((".ini", ".inc"))
    ]
    return {
        "compiler": "openbfme_importer.ring_system_compiler",
        "sourceDocumentCount": len(sources),
        "sourceCorpusSha256": _digest(sources),
    }


def compile_ring_system_descriptor(
    documents: Mapping[str, bytes],
) -> dict[str, object]:
    """Compile the evidence-bearing ring descriptor from an effective INI view."""

    if not isinstance(documents, Mapping) or not documents:
        raise RingSystemCompilerError("effective INI document mapping is empty")
    try:
        prepared = prepare_playable_unit_compiler(documents)
    except ValueError as exc:
        raise RingSystemCompilerError(str(exc)) from exc
    objects = dict(prepared.objects)
    crate_path = "data/ini/crate.ini"
    try:
        crate_objects = parse_sage_document(
            _document(documents, crate_path), crate_path
        ).objects
    except ValueError as exc:
        raise RingSystemCompilerError(f"object parse failed for {crate_path}: {exc}") from exc
    for obj in crate_objects:
        key = obj.name.casefold()
        if key in objects:
            raise RingSystemCompilerError(f"duplicate effective Object: {obj.name}")
        objects[key] = obj

    gollum, ring_gollum = _compile_gollum(
        objects, _document(documents, "data/ini/armor.ini")
    )
    ring = _compile_ring(objects)
    player_templates = _compile_player_templates(documents)
    hero_ids: dict[str, str] = {}
    for heroes in player_templates.values():
        for hero_id in heroes:
            folded = hero_id.casefold()
            incumbent = hero_ids.get(folded)
            if incumbent is not None and incumbent != hero_id:
                raise RingSystemCompilerError(
                    f"ring hero id has ambiguous casing: {incumbent!r} / {hero_id!r}"
                )
            hero_ids[folded] = hero_id

    compiled_objects: dict[str, object] = {
        NEUTRAL_GOLLUM: gollum,
        RING_GOLLUM: ring_gollum,
        DROPPED_RING: ring,
    }
    for hero_id in sorted(hero_ids.values(), key=str.casefold):
        if hero_id.casefold() in {key.casefold() for key in compiled_objects}:
            raise RingSystemCompilerError(
                f"ring hero route collides with core ring object: {hero_id}"
            )
        compiled_objects[hero_id] = _compile_ring_hero(
            objects, hero_id, prepared.numeric_defines
        )
    ocl_source = _document(documents, "data/ini/objectcreationlist.ini")
    spawn, excluded_objects = _compile_spawn_contract(ocl_source)
    if spawn["objectId"] not in compiled_objects:
        raise RingSystemCompilerError(
            f"OCL_TheRingCarrier names uncompiled object: {spawn['objectId']}"
        )
    for excluded_id in excluded_objects:
        excluded = _required_object(objects, excluded_id)
        if (excluded.parent or "").casefold() != NEUTRAL_GOLLUM.casefold():
            raise RingSystemCompilerError(
                f"OCL_TheRingStealer object {excluded_id} is not a {NEUTRAL_GOLLUM} child"
            )
    object_creation_lists = {
        name: _compile_ocl(ocl_source, name)
        for name in ("OCL_TheOneRing", "OCL_TheOneRingCD")
    }
    result: dict[str, object] = {
        "schema": SCHEMA,
        "schemaVersion": SCHEMA_VERSION,
        "runtimeOutputPath": "data/ring/system.json",
        "objects": compiled_objects,
        "objectCreationLists": object_creation_lists,
        "upgrades": _compile_upgrades(documents),
        "delivery": _compile_delivery(
            documents,
            objects,
            {name.casefold() for name in object_creation_lists},
        ),
        "routes": _compile_routes(documents),
        "system": {
            "modeToken": "ringheroes",
            "spawn": spawn,
            "evaEvents": list(RING_EVA_EVENTS),
            "ringHeroesByFaction": player_templates,
        },
        "artConversionPlan": _art_plan(compiled_objects),
        "excludedObjects": excluded_objects,
        "importerEvidence": _importer_evidence(documents),
    }
    result["descriptorSha256"] = _digest(result)
    validate_ring_system_descriptor(result)
    return result


def _validate_registration(value: Mapping[str, object]) -> None:
    objects = value.get("objects")
    system = value.get("system")
    if not isinstance(objects, Mapping) or not isinstance(system, Mapping):
        raise RingSystemCompilerError("ring registration object/system tables are invalid")
    routes = system.get("ringHeroesByFaction")
    if not isinstance(routes, Mapping) or not routes:
        raise RingSystemCompilerError("ring registration has no faction hero routes")
    routed_heroes: set[str] = set()
    for faction, heroes in routes.items():
        if not isinstance(faction, str) or not isinstance(heroes, list) or not heroes:
            raise RingSystemCompilerError("ring faction hero route is invalid")
        if not all(isinstance(hero, str) and hero for hero in heroes):
            raise RingSystemCompilerError(f"ring faction {faction} has an invalid hero id")
        routed_heroes.update(heroes)
    expected_objects = {NEUTRAL_GOLLUM, RING_GOLLUM, DROPPED_RING, *routed_heroes}
    if set(objects) != expected_objects:
        raise RingSystemCompilerError("ring system object set does not match faction routes")

    ring = objects.get(DROPPED_RING)
    try:
        filter_row = ring["ringMechanic"]["attach"]["filter"]  # type: ignore[index]
    except (KeyError, TypeError):
        raise RingSystemCompilerError("ring attach filter is missing") from None
    if (
        not isinstance(filter_row, Mapping)
        or filter_row.get("mode") not in {"ANY", "NONE"}
        or not isinstance(filter_row.get("include"), list)
        or not isinstance(filter_row.get("exclude"), list)
        or not isinstance(filter_row.get("predicate"), list)
        or "NEUTRALGOLLUM" not in {
            str(item).upper() for item in filter_row.get("exclude", [])
        }
        or "NOT_FLYING_UNITS" not in {
            str(item).upper() for item in filter_row.get("predicate", [])
        }
    ):
        raise RingSystemCompilerError("ring attach filter contract is invalid")

    spawn = system.get("spawn")
    if (
        not isinstance(spawn, Mapping)
        or spawn.get("objectId") not in objects
        or not isinstance(spawn.get("waypointFamily"), str)
        or not spawn.get("waypointFamily")
        or not isinstance(spawn.get("team"), str)
        or not spawn.get("team")
    ):
        raise RingSystemCompilerError("ring spawn contract is invalid")
    if system.get("evaEvents") != list(RING_EVA_EVENTS):
        raise RingSystemCompilerError("ring system EVA contract is invalid")

    excluded = value.get("excludedObjects")
    if (
        not isinstance(excluded, Mapping)
        or not excluded
        or set(excluded).intersection(objects)
        or not all(isinstance(key, str) and key for key in excluded)
    ):
        raise RingSystemCompilerError("ring excluded-object contract is invalid")

    delivery = value.get("delivery")
    structures = delivery.get("structures") if isinstance(delivery, Mapping) else None
    if not isinstance(structures, list) or len(structures) != 26:
        raise RingSystemCompilerError("ring delivery structure set is invalid")
    if not all(
        isinstance(row, Mapping)
        and isinstance(row.get("dropsRingOnDeath"), bool)
        for row in structures
    ):
        raise RingSystemCompilerError("ring delivery drop flags are invalid")


def validate_ring_system_descriptor(value: Mapping[str, object]) -> None:
    """Validate the evidence-bearing descriptor and its content identity."""

    if value.get("schema") != SCHEMA or value.get("schemaVersion") != SCHEMA_VERSION:
        raise RingSystemCompilerError("ring system descriptor identity is invalid")
    unsigned = dict(value)
    digest = unsigned.pop("descriptorSha256", None)
    if not isinstance(digest, str) or digest != _digest(unsigned):
        raise RingSystemCompilerError("ring system descriptor digest is invalid")
    evidence = value.get("importerEvidence")
    if (
        not isinstance(evidence, Mapping)
        or not isinstance(evidence.get("sourceDocumentCount"), int)
        or evidence.get("sourceDocumentCount", 0) < 1
        or not isinstance(evidence.get("sourceCorpusSha256"), str)
    ):
        raise RingSystemCompilerError("ring system importer evidence is invalid")
    _validate_registration(value)


def build_ring_system_runtime(descriptor: Mapping[str, object]) -> dict[str, object]:
    """Build the Godot-facing runtime without descriptor-only importer evidence."""

    validate_ring_system_descriptor(descriptor)
    registration_keys = (
        "objects",
        "objectCreationLists",
        "upgrades",
        "delivery",
        "routes",
        "system",
        "excludedObjects",
    )
    result: dict[str, object] = {
        "schema": RUNTIME_SCHEMA,
        "schemaVersion": RUNTIME_SCHEMA_VERSION,
        "descriptorSha256": descriptor["descriptorSha256"],
        "registration": {key: descriptor[key] for key in registration_keys},
    }
    result["runtimeSha256"] = _digest(result)
    validate_ring_system_runtime(result)
    return result


def validate_ring_system_runtime(value: Mapping[str, object]) -> None:
    if (
        value.get("schema") != RUNTIME_SCHEMA
        or value.get("schemaVersion") != RUNTIME_SCHEMA_VERSION
    ):
        raise RingSystemCompilerError("ring system runtime identity is invalid")
    unsigned = dict(value)
    digest = unsigned.pop("runtimeSha256", None)
    if not isinstance(digest, str) or digest != _digest(unsigned):
        raise RingSystemCompilerError("ring system runtime digest is invalid")
    if not isinstance(value.get("descriptorSha256"), str):
        raise RingSystemCompilerError("ring runtime descriptor identity is invalid")
    registration = value.get("registration")
    if not isinstance(registration, Mapping):
        raise RingSystemCompilerError("ring runtime registration is invalid")
    _validate_registration(registration)


def compile_ring_system(documents: Mapping[str, bytes]) -> dict[str, object]:
    """Backward-compatible descriptor compiler; prefer the explicit name."""

    return compile_ring_system_descriptor(documents)


def validate_ring_system(value: Mapping[str, object]) -> None:
    """Backward-compatible descriptor validator; prefer the explicit name."""

    validate_ring_system_descriptor(value)
