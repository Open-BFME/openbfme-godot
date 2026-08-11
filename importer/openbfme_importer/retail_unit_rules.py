"""Extract the Men/Fords base locomotor, weapon, vision, and horde rules.

The converter consumes only the exact profile-selected BFME II 1.06 INI
sources supplied by :mod:`openbfme_importer.pipeline`.  It resolves authored
GameData constants but does not contain gameplay values of its own.
"""

from __future__ import annotations

from dataclasses import dataclass
from decimal import Decimal, InvalidOperation
import hashlib
import re
from pathlib import Path
from typing import Any, Mapping

from .sage_cst import SageAssignment, SageBlock, SageObject, parse_sage_document


SCHEMA = "openbfme.retail-unit-rules"
SCHEMA_VERSION = 0
OUTPUT_PATH = "data/retail_unit_rules.json"

GAMEDATA_PATH = "data/ini/gamedata.ini"
LOCOMOTOR_PATH = "data/ini/locomotor.ini"
WEAPON_PATH = "data/ini/weapon.ini"
HORDE_PATH = "data/ini/object/goodfaction/hordes/men/menhordes.ini"
ATTRIBUTE_MODIFIER_PATH = "data/ini/attributemodifier.ini"
COMMAND_BUTTON_PATH = "data/ini/commandbutton.ini"

_UNIT_SPECS = (
    (
        "bfme2.object.gondor-fighter",
        "bfme2.object.gondor-fighter-horde",
        "GondorFighter",
        "GondorFighterHorde",
        "data/ini/object/goodfaction/units/men/gondorfighter.ini",
    ),
    (
        "bfme2.object.gondor-archer",
        "bfme2.object.gondor-archer-horde",
        "GondorArcher",
        "GondorArcherHorde",
        "data/ini/object/goodfaction/units/men/gondorarcher.ini",
    ),
    (
        "bfme2.object.gondor-tower-guard",
        "bfme2.object.gondor-tower-guard-horde",
        "GondorTowerShieldGuard",
        "GondorTowerShieldGuardHorde",
        "data/ini/object/goodfaction/units/men/gondortowershieldguard.ini",
    ),
    (
        "bfme2.object.gondor-knight",
        "bfme2.object.gondor-knight-horde",
        "GondorCavalry",
        "GondorKnightHorde",
        "data/ini/object/goodfaction/units/men/gondorcavalry.ini",
    ),
)

RANGER_UNIT_SPEC = (
    "bfme2.object.gondor-ranger",
    "bfme2.object.gondor-ranger-horde",
    "GondorRanger",
    "GondorRangerHorde",
    "data/ini/object/goodfaction/units/men/gondorranger.ini",
)

def retail_unit_rule_source_paths(
    unit_specs: tuple[tuple[str, str, str, str, str], ...] = _UNIT_SPECS,
) -> frozenset[str]:
    return frozenset(
        {
            GAMEDATA_PATH,
            LOCOMOTOR_PATH,
            WEAPON_PATH,
            HORDE_PATH,
            ATTRIBUTE_MODIFIER_PATH,
            COMMAND_BUTTON_PATH,
        }
        | {item[4] for item in unit_specs}
    )
_DEFINE_RE = re.compile(r"^[ \t]*#define[ \t]+([A-Za-z_][A-Za-z0-9_]*)[ \t]+(.+?)\s*$")
_HEADER_RE = re.compile(
    r"^(Locomotor|Weapon|ModifierList|CommandButton)\s+(\S+)\s*$",
    re.IGNORECASE,
)
_ASSIGN_RE = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(.*?)\s*$")
_POSITION_RE = re.compile(
    r"Position\s*:\s*X\s*:\s*(-?(?:\d+(?:\.\d*)?|\.\d+))\s+Y\s*:\s*(-?(?:\d+(?:\.\d*)?|\.\d+))",
    re.IGNORECASE,
)
_RANK_RE = re.compile(r"RankNumber\s*:\s*(\d+)", re.IGNORECASE)
_UNIT_TYPE_RE = re.compile(r"UnitType\s*:\s*([A-Za-z0-9_]+)", re.IGNORECASE)
_FUNCTION_RE = re.compile(r"^#(SUBTRACT|MULTIPLY)\s*\(\s*(.+)\s*\)$", re.IGNORECASE)
_NESTED_WEAPON_BLOCKS = frozenset(
    {
        "damagenugget",
        "projectilenugget",
        "hordeattacknugget",
        "attributeModifierNugget".casefold(),
        "paralyzenugget",
        "specialmodelconditionnugget",
    }
)


@dataclass(frozen=True, slots=True)
class _Source:
    path: str
    payload: bytes
    text: str
    sha256: str


@dataclass(frozen=True, slots=True)
class _Define:
    name: str
    expression: str
    line: int


@dataclass(frozen=True, slots=True)
class _BlockAssignment:
    key: str
    value: str
    line: int
    depth: int
    parents: tuple[str, ...]


@dataclass(frozen=True, slots=True)
class _NamedBlock:
    kind: str
    name: str
    line: int
    end_line: int
    assignments: tuple[_BlockAssignment, ...]


def _strip_comment(raw: str) -> str:
    indexes = [index for marker in (";", "//") if (index := raw.find(marker)) >= 0]
    return (raw[: min(indexes)] if indexes else raw).strip()


def _load_sources(
    sources: Mapping[str, Path | str],
    unit_specs: tuple[tuple[str, str, str, str, str], ...],
) -> dict[str, _Source]:
    required_paths = retail_unit_rule_source_paths(unit_specs)
    normalized = {str(key).replace("\\", "/").casefold(): Path(value) for key, value in sources.items()}
    missing = sorted(path for path in required_paths if path.casefold() not in normalized)
    if missing:
        raise ValueError("retail unit-rule sources are missing: " + ", ".join(missing))
    result: dict[str, _Source] = {}
    for path in sorted(required_paths):
        source_path = normalized[path.casefold()]
        if not source_path.is_file():
            raise ValueError(f"retail unit-rule source is not a file: {path}")
        payload = source_path.read_bytes()
        if not payload:
            raise ValueError(f"retail unit-rule source is empty: {path}")
        text = payload.decode("cp1252")
        result[path] = _Source(path, payload, text, hashlib.sha256(payload).hexdigest())
    return result


def _parse_defines(source: _Source) -> dict[str, _Define]:
    result: dict[str, _Define] = {}
    for line, raw in enumerate(source.text.splitlines(), start=1):
        match = _DEFINE_RE.fullmatch(_strip_comment(raw))
        if match is None:
            continue
        name, expression = match.groups()
        folded = name.casefold()
        if folded in result:
            raise ValueError(f"duplicate GameData define {name!r}")
        result[folded] = _Define(name, expression.strip(), line)
    return result


def _split_function_arguments(value: str) -> tuple[str, str]:
    depth = 0
    for index, character in enumerate(value):
        if character == "(":
            depth += 1
        elif character == ")":
            depth -= 1
        elif character.isspace() and depth == 0:
            left = value[:index].strip()
            right = value[index:].strip()
            if left and right:
                return left, right
    raise ValueError(f"unsupported SAGE arithmetic expression: {value!r}")


def _resolve_decimal(
    expression: str,
    defines: Mapping[str, _Define],
    chain: list[_Define] | None = None,
) -> tuple[Decimal, list[_Define]]:
    value = expression.strip()
    try:
        return Decimal(value), list(chain or [])
    except InvalidOperation:
        pass
    function = _FUNCTION_RE.fullmatch(value)
    if function is not None:
        left_raw, right_raw = _split_function_arguments(function.group(2))
        left, left_chain = _resolve_decimal(left_raw, defines, chain)
        right, right_chain = _resolve_decimal(right_raw, defines, left_chain)
        if function.group(1).casefold() == "subtract":
            return left - right, right_chain
        return left * right, right_chain
    definition = defines.get(value.casefold())
    if definition is None:
        raise ValueError(f"unresolved retail numeric expression: {expression!r}")
    prior = list(chain or [])
    if any(item.name.casefold() == definition.name.casefold() for item in prior):
        raise ValueError(f"cyclic GameData define: {definition.name}")
    prior.append(definition)
    return _resolve_decimal(definition.expression, defines, prior)


def _json_number(value: Decimal) -> int | float:
    integral = value.to_integral_value()
    return int(integral) if value == integral else float(value)


def _source_record(source: _Source, scope_kind: str, scope_name: str, field: str, line: int) -> dict[str, Any]:
    return {
        "ini": source.path,
        "iniSha256": source.sha256,
        "scopeKind": scope_kind,
        "scopeName": scope_name,
        "field": field,
        "line": line,
    }


def _number(
    raw: str,
    source: _Source,
    scope_kind: str,
    scope_name: str,
    field: str,
    line: int,
    defines: Mapping[str, _Define],
) -> dict[str, Any]:
    value, chain = _resolve_decimal(raw, defines)
    return {
        "raw": raw,
        "value": _json_number(value),
        "source": _source_record(source, scope_kind, scope_name, field, line),
        "resolvedDefines": [
            {
                "name": item.name,
                "raw": item.expression,
                "value": _json_number(_resolve_decimal(item.expression, defines)[0]),
                "source": _source_record(
                    source=_SOURCE_CONTEXT[GAMEDATA_PATH],
                    scope_kind="Define",
                    scope_name=item.name,
                    field=item.name,
                    line=item.line,
                ),
            }
            for item in chain
        ],
    }


# Bound for the duration of one extraction so nested helper calls can attach
# exact GameData provenance without passing the same immutable mapping through
# every formatting layer.
_SOURCE_CONTEXT: dict[str, _Source] = {}


def _one_object(source: _Source, name: str) -> SageObject:
    document = parse_sage_document(source.payload, source.path)
    matches = [item for item in document.objects if item.name.casefold() == name.casefold()]
    if len(matches) != 1:
        raise ValueError(f"expected one Object {name}, found {len(matches)}")
    return matches[0]


def _assignment(items: tuple[Any, ...], key: str, *, required: bool = True) -> SageAssignment | None:
    matches = [item for item in items if isinstance(item, SageAssignment) and item.key.casefold() == key.casefold()]
    if len(matches) > 1 or (required and len(matches) != 1):
        raise ValueError(f"expected {'one' if required else 'at most one'} {key} assignment, found {len(matches)}")
    return matches[0] if matches else None


def _base_state_block(obj: SageObject, kind: str) -> SageBlock:
    candidates = [block for block in obj.blocks if block.kind.casefold() == kind.casefold()]
    for block in candidates:
        condition = _assignment(block.items, "Conditions", required=False)
        if condition is None or condition.value.strip().casefold() == "none":
            return block
    raise ValueError(f"{obj.name} has no base {kind} block")


def _locomotor_set(obj: SageObject, source: _Source, defines: Mapping[str, _Define]) -> dict[str, Any]:
    block = _base_state_block(obj, "LocomotorSet")
    locomotor = _assignment(block.items, "Locomotor")
    speed = _assignment(block.items, "Speed")
    assert locomotor is not None and speed is not None
    return {
        "template": locomotor.value.split()[0],
        "speed": _number(speed.value, source, "Object", obj.name, speed.key, speed.line, defines),
        "source": _source_record(source, "Object", obj.name, block.kind, block.line),
    }


def _vision(obj: SageObject, source: _Source, defines: Mapping[str, _Define]) -> dict[str, Any]:
    value = _assignment(obj.items, "VisionRange")
    assert value is not None
    return _number(value.value, source, "Object", obj.name, value.key, value.line, defines)


def _shroud_clearing(
    obj: SageObject, source: _Source, defines: Mapping[str, _Define]
) -> dict[str, Any] | None:
    """Return the object's authored ``ShroudClearingRange``, or None.

    This is a SEPARATE range from ``VisionRange`` and must not be derived from
    it. Retail's own macro table keeps two independent families
    (``gamedata.ini`` ``SHROUD_CLEAR_*`` versus ``VISION_*``) and the shipped
    objects disagree constantly: ``MenFortressCitadel`` is VisionRange 400 /
    ShroudClearingRange 800, ``GondorSentryTower`` is 600 / 500. Of the objects
    that author both, roughly half give them different values.

    It is optional because 352 shipped objects author ``VisionRange`` only. A
    missing value is reported as absent rather than defaulted, so the runtime
    can make its own fallback decision and say so.

    THE HORDE/MEMBER TRAP. ``SHROUD_CLEAR_STANDARD`` is 25 and is what the
    individual horde MEMBER authors, deliberately, so members do not each
    deshroud. The real radius lives on the horde PARENT (``GondorFighter`` 25
    versus ``GondorFighterHorde`` 400). Both are compiled here because both
    objects are compiled here; a consumer that reads the member value and
    ignores the horde will deshroud a 16x-too-small bubble.
    """
    value = _assignment(obj.items, "ShroudClearingRange")
    if value is None:
        return None
    return _number(value.value, source, "Object", obj.name, value.key, value.line, defines)


def _weapon_name(obj: SageObject) -> str:
    block = _base_state_block(obj, "WeaponSet")
    weapons = [
        item
        for item in block.assignments
        if item.key.casefold() == "weapon" and item.value.split()[0].casefold() == "primary"
    ]
    if len(weapons) != 1 or len(weapons[0].value.split()) < 2:
        raise ValueError(f"{obj.name} base WeaponSet does not have one primary weapon")
    return weapons[0].value.split()[1]


def _weapon_sets(
    obj: SageObject,
    source: _Source,
    weapon_source: _Source,
    defines: Mapping[str, _Define],
) -> list[dict[str, Any]]:
    """Return every authored member WeaponSet without collapsing conditions.

    The original v0 contract kept only ``Conditions=None``. Gondor Archers use
    a second ``CLOSE_RANGE CONTESTING_BUILDING`` set whose secondary weapon is
    the melee attack, so discarding the other sets silently changed gameplay.
    """

    result: list[dict[str, Any]] = []
    for block in obj.blocks:
        if block.kind.casefold() != "weaponset":
            continue
        condition_assignment = _assignment(block.items, "Conditions", required=False)
        conditions = (
            condition_assignment.value.split()
            if condition_assignment is not None
            else ["None"]
        )
        slots: dict[str, Any] = {}
        for assignment in block.assignments:
            if assignment.key.casefold() != "weapon":
                continue
            tokens = assignment.value.split()
            if len(tokens) < 2:
                raise ValueError(
                    f"{obj.name} WeaponSet has malformed Weapon assignment at line {assignment.line}"
                )
            slot = tokens[0].casefold()
            if slot not in {"primary", "secondary", "tertiary", "quinary"}:
                raise ValueError(f"{obj.name} WeaponSet has unsupported slot {tokens[0]!r}")
            if slot in slots:
                raise ValueError(f"{obj.name} WeaponSet repeats {tokens[0]} weapon")
            if slot == "quinary":
                # BFME2 uses Ranger's quinary slot only as the Long Shot
                # special-power range/timing carrier. It has no damage nugget
                # and must not be misrepresented as an ordinary attack mode.
                slots[slot] = {
                    "name": tokens[1],
                    "deferredSpecialAbility": True,
                    "source": _source_record(
                        source,
                        "Object",
                        obj.name,
                        assignment.key,
                        assignment.line,
                    ),
                }
            else:
                slots[slot] = _weapon_rules(
                    tokens[1],
                    weapon_source,
                    defines,
                    member_weapon=True,
                )
        if "primary" not in slots:
            raise ValueError(f"{obj.name} WeaponSet has no primary weapon")
        result.append(
            {
                "conditions": conditions,
                "slots": slots,
                "source": _source_record(
                    source,
                    "Object",
                    obj.name,
                    block.kind,
                    block.line,
                ),
            }
        )
    if not result:
        raise ValueError(f"{obj.name} has no WeaponSet blocks")
    return result


def _dual_weapon_switch_distance(
    obj: SageObject,
    source: _Source,
    defines: Mapping[str, _Define],
) -> dict[str, Any]:
    matches = [
        block
        for block in obj.blocks
        if block.kind.casefold() == "dualweaponbehavior"
    ]
    if not matches:
        return {
            "defined": False,
            "value": None,
            "source": _source_record(source, "Object", obj.name, "DualWeaponBehavior", obj.line),
        }
    if len(matches) != 1:
        raise ValueError(f"{obj.name} has {len(matches)} DualWeaponBehavior blocks")
    assignment = _assignment(matches[0].items, "SwitchWeaponOnCloseRangeDistance")
    assert assignment is not None
    return _number(
        assignment.value,
        source,
        "Object",
        obj.name,
        assignment.key,
        assignment.line,
        defines,
    )


def _named_blocks(source: _Source, kind: str, name: str) -> list[_NamedBlock]:
    lines = source.text.splitlines()
    result: list[_NamedBlock] = []
    target = (kind.casefold(), name.casefold())
    for start, raw in enumerate(lines, start=1):
        header = _HEADER_RE.fullmatch(_strip_comment(raw))
        if header is None or (header.group(1).casefold(), header.group(2).casefold()) != target:
            continue
        depth = 1
        parents: list[str] = []
        assignments: list[_BlockAssignment] = []
        for line_number in range(start + 1, len(lines) + 1):
            active = _strip_comment(lines[line_number - 1])
            if not active:
                continue
            if active.casefold() == "end":
                depth -= 1
                if parents:
                    parents.pop()
                if depth == 0:
                    result.append(_NamedBlock(header.group(1), header.group(2), start, line_number, tuple(assignments)))
                    break
                continue
            assignment = _ASSIGN_RE.fullmatch(active)
            if assignment is not None:
                assignments.append(
                    _BlockAssignment(
                        assignment.group(1),
                        assignment.group(2).strip(),
                        line_number,
                        depth,
                        tuple(parents),
                    )
                )
                continue
            token = active.split()[0].casefold()
            if kind.casefold() == "weapon" and token in _NESTED_WEAPON_BLOCKS:
                parents.append(token)
                depth += 1
        else:
            raise ValueError(f"unterminated {kind} {name}")
    return result


def _one_named_block(source: _Source, kind: str, name: str) -> _NamedBlock:
    matches = _named_blocks(source, kind, name)
    if len(matches) != 1:
        raise ValueError(f"expected one {kind} {name}, found {len(matches)}")
    return matches[0]


def _block_assignment(block: _NamedBlock, key: str, *, depth: int | None = None, required: bool = True) -> _BlockAssignment | None:
    matches = [
        item
        for item in block.assignments
        if item.key.casefold() == key.casefold() and (depth is None or item.depth == depth)
    ]
    if len(matches) > 1 or (required and len(matches) != 1):
        raise ValueError(f"{block.kind} {block.name} expected {'one' if required else 'at most one'} {key}, found {len(matches)}")
    return matches[0] if matches else None


def _template_locomotor(name: str, source: _Source, defines: Mapping[str, _Define]) -> dict[str, Any]:
    block = _one_named_block(source, "Locomotor", name)
    acceleration = _block_assignment(block, "Acceleration", depth=1)
    braking = _block_assignment(block, "Braking", depth=1)
    turn_rate = _block_assignment(block, "TurnRate", depth=1, required=False)
    turn_time = _block_assignment(block, "TurnTime", depth=1, required=turn_rate is None)
    assert acceleration is not None and braking is not None
    if turn_rate is not None:
        resolved_turn = _number(turn_rate.value, source, "Locomotor", name, turn_rate.key, turn_rate.line, defines)
    else:
        assert turn_time is not None
        authored = _number(turn_time.value, source, "Locomotor", name, turn_time.key, turn_time.line, defines)
        milliseconds = Decimal(str(authored["value"]))
        if milliseconds <= 0:
            raise ValueError(f"Locomotor {name} TurnTime must be positive")
        resolved_turn = {
            "raw": turn_time.value,
            "value": _json_number(Decimal(360000) / milliseconds),
            "authoredField": "TurnTime",
            "authoredValueMilliseconds": authored["value"],
            "semantic": "360 degrees divided by TurnTime seconds",
            "source": authored["source"],
            "resolvedDefines": authored["resolvedDefines"],
        }
    return {
        "name": name,
        "acceleration": _number(acceleration.value, source, "Locomotor", name, acceleration.key, acceleration.line, defines),
        "turnRateDegreesPerSecond": resolved_turn,
        "braking": _number(braking.value, source, "Locomotor", name, braking.key, braking.line, defines),
        "source": _source_record(source, "Locomotor", name, "Locomotor", block.line),
    }


def _weapon_rules(
    name: str,
    source: _Source,
    defines: Mapping[str, _Define],
    *,
    member_weapon: bool,
) -> dict[str, Any]:
    block = _one_named_block(source, "Weapon", name)
    result: dict[str, Any] = {"name": name, "source": _source_record(source, "Weapon", name, "Weapon", block.line)}
    fields = {
        "attackRange": "AttackRange",
        "minimumAttackRange": "MinimumAttackRange",
        "delayBetweenShotsMs": "DelayBetweenShots",
        "preAttackDelayMs": "PreAttackDelay",
        "firingDurationMs": "FiringDuration",
    }
    for output, field in fields.items():
        assignment = _block_assignment(
            block,
            field,
            depth=1,
            required=field == "AttackRange" or (member_weapon and field != "MinimumAttackRange"),
        )
        result[output] = (
            _number(assignment.value, source, "Weapon", name, assignment.key, assignment.line, defines)
            if assignment is not None
            else {"defined": False, "value": None, "source": result["source"]}
        )
    damages = [item for item in block.assignments if item.key.casefold() == "damage"]
    # A base weapon can contain later upgrade-gated DamageNuggets (Tower
    # Guard). The no-upgrade nugget is authored first in the retail block.
    damage = damages[0] if damages else None
    damage_scope = name
    if damage is None and member_weapon:
        # Projectile weapons may list a base projectile followed by upgrade-
        # gated alternatives. Source order plus the base Conditions=None
        # WeaponSet selects the first projectile for the no-upgrade slice.
        warheads = [
            item
            for item in block.assignments
            if item.key.casefold() == "warheadtemplatename"
        ]
        if not warheads:
            raise ValueError(f"Weapon {name} has no base damage or warhead")
        warhead = warheads[0]
        damage_scope = warhead.value.split()[0]
        damage_block = _one_named_block(source, "Weapon", damage_scope)
        damage = _block_assignment(damage_block, "Damage")
    result["damage"] = (
        _number(damage.value, source, "Weapon", damage_scope, damage.key, damage.line, defines)
        if damage is not None
        else {"defined": False, "value": None, "source": result["source"]}
    )
    return result


def _horde_formation(obj: SageObject, source: _Source, defines: Mapping[str, _Define]) -> dict[str, Any]:
    width = _assignment(obj.items, "FormationWidth")
    depth = _assignment(obj.items, "FormationDepth")
    assert width is not None and depth is not None
    contain = next((block for block in obj.blocks if block.kind.casefold() in {"hordecontain", "horsehordecontain"}), None)
    if contain is None:
        raise ValueError(f"{obj.name} has no HordeContain block")
    ranks: list[dict[str, Any]] = []
    for item in contain.assignments:
        if item.key.casefold() != "rankinfo":
            continue
        rank = _RANK_RE.search(item.value)
        unit_type = _UNIT_TYPE_RE.search(item.value)
        positions = _POSITION_RE.findall(item.value)
        if rank is None or unit_type is None or not positions:
            raise ValueError(f"unsupported RankInfo in {obj.name}:{item.line}")
        ranks.append(
            {
                "rankNumber": int(rank.group(1)),
                "unitType": unit_type.group(1),
                "positions": [
                    {
                        "x": _json_number(Decimal(x)),
                        "y": _json_number(Decimal(y)),
                    }
                    for x, y in positions
                ],
                "source": _source_record(source, "Object", obj.name, item.key, item.line),
            }
        )
    if not ranks:
        raise ValueError(f"{obj.name} has no authored RankInfo rows")
    return {
        "formationWidth": _number(width.value, source, "Object", obj.name, width.key, width.line, defines),
        "formationDepth": _number(depth.value, source, "Object", obj.name, depth.key, depth.line, defines),
        "memberCount": sum(len(rank["positions"]) for rank in ranks),
        "ranks": ranks,
        "source": _source_record(source, "Object", obj.name, contain.kind, contain.line),
    }


def _percentage(raw: str) -> float:
    value = raw.strip()
    if not value.endswith("%"):
        raise ValueError(f"expected percentage modifier, got {raw!r}")
    try:
        return float(Decimal(value[:-1].strip()))
    except InvalidOperation as exc:
        raise ValueError(f"invalid percentage modifier: {raw!r}") from exc


def _stance_modifier(
    template: str,
    stance: str,
    source: _Source,
) -> dict[str, Any]:
    if stance == "Battle":
        return {
            "name": "Battle",
            "damageMultiplier": 1.0,
            "incomingDamageMultiplier": 1.0,
            "visionMultiplier": 1.0,
            "speedMultiplier": 1.0,
            "modifiers": [],
            "source": {"status": "neutral-no-ModifierList"},
        }
    block_name = f"{template}Stance{stance}"
    block = _one_named_block(source, "ModifierList", block_name)
    result: dict[str, Any] = {
        "name": stance,
        "damageMultiplier": 1.0,
        "incomingDamageMultiplier": 1.0,
        "visionMultiplier": 1.0,
        "speedMultiplier": 1.0,
        "modifiers": [],
        "source": _source_record(
            source,
            "ModifierList",
            block_name,
            "ModifierList",
            block.line,
        ),
    }
    for assignment in block.assignments:
        if assignment.depth != 1 or assignment.key.casefold() != "modifier":
            continue
        tokens = assignment.value.split()
        if len(tokens) != 2:
            raise ValueError(f"unsupported stance modifier in {block_name}:{assignment.line}")
        field = tokens[0].upper()
        percent = _percentage(tokens[1])
        record = {
            "field": field,
            "raw": tokens[1],
            "percent": percent,
            "source": _source_record(
                source,
                "ModifierList",
                block_name,
                assignment.key,
                assignment.line,
            ),
        }
        result["modifiers"].append(record)
        if field == "DAMAGE_MULT":
            result["damageMultiplier"] = percent / 100.0
        elif field == "ARMOR":
            result["incomingDamageMultiplier"] = 1.0 - percent / 100.0
        elif field == "VISION":
            result["visionMultiplier"] = 1.0 + percent / 100.0
        elif field == "SPEED":
            result["speedMultiplier"] = percent / 100.0
        else:
            raise ValueError(f"unsupported stance field {field!r} in {block_name}")
    return result


def _stance_rules(
    obj: SageObject,
    source: _Source,
    modifier_source: _Source,
) -> dict[str, Any]:
    blocks = [
        block
        for block in obj.blocks
        if block.kind.casefold() == "stancesbehavior"
    ]
    if len(blocks) != 1:
        raise ValueError(f"{obj.name} expected one StancesBehavior, found {len(blocks)}")
    template_assignment = _assignment(blocks[0].items, "StanceTemplate")
    assert template_assignment is not None
    template = template_assignment.value.split()[0]
    order = ["HoldGround", "Battle", "Aggressive"]
    return {
        "template": template,
        "default": "Battle",
        "cycleOrder": order,
        "states": {
            stance: _stance_modifier(template, stance, modifier_source)
            for stance in order
        },
        "source": _source_record(
            source,
            "Object",
            obj.name,
            "StancesBehavior",
            blocks[0].line,
        ),
    }


def _toggle_stance_command(source: _Source) -> dict[str, Any]:
    block = _one_named_block(source, "CommandButton", "Command_ToggleStance")
    fields: dict[str, Any] = {}
    for key in (
        "Command",
        "Options",
        "TextLabel",
        "DescriptLabel",
        "ButtonImage",
        "Stances",
        "InPalantir",
        "UnitSpecificSound",
    ):
        assignment = _block_assignment(block, key, depth=1)
        assert assignment is not None
        fields[key] = {
            "tokens": assignment.value.split(),
            "source": _source_record(
                source,
                "CommandButton",
                block.name,
                assignment.key,
                assignment.line,
            ),
        }
    if fields["Command"]["tokens"] != ["TOGGLE_STANCE"]:
        raise ValueError("Command_ToggleStance command changed")
    if fields["Stances"]["tokens"] != ["HoldGround", "Battle", "Aggressive"]:
        raise ValueError("Command_ToggleStance order changed")
    return {
        "name": block.name,
        "fields": fields,
        "source": _source_record(
            source,
            "CommandButton",
            block.name,
            "CommandButton",
            block.line,
        ),
    }


def _object_rules(
    obj: SageObject,
    source: _Source,
    locomotor_source: _Source,
    weapon_source: _Source,
    defines: Mapping[str, _Define],
    modifier_source: _Source,
    *,
    include_formation: bool,
) -> dict[str, Any]:
    locomotor_set = _locomotor_set(obj, source, defines)
    base_weapon = _weapon_rules(
        _weapon_name(obj),
        weapon_source,
        defines,
        member_weapon=not include_formation,
    )
    result = {
        "sourceName": obj.name,
        "source": _source_record(source, "Object", obj.name, "Object", obj.line),
        "visionRange": _vision(obj, source, defines),
        "locomotorSet": locomotor_set,
        "locomotor": _template_locomotor(locomotor_set["template"], locomotor_source, defines),
        # Compatibility view for existing v0 consumers. New runtime code must
        # use weaponSets so conditions and secondary weapons are not lost.
        "weapon": base_weapon,
    }
    shroud_clearing = _shroud_clearing(obj, source, defines)
    if shroud_clearing is not None:
        # Absent stays ABSENT. Emitting a zero (or a copy of visionRange) for the
        # 352 objects that author no ShroudClearingRange would be indistinguishable
        # downstream from an object that authors 0 on purpose - and Carn Dum's
        # map.ini does exactly that for nine props.
        result["shroudClearingRange"] = shroud_clearing
    if include_formation:
        result["formation"] = _horde_formation(obj, source, defines)
        result["stances"] = _stance_rules(obj, source, modifier_source)
    else:
        body_blocks = [
            block
            for block in obj.blocks
            if (block.header_key or "").casefold() == "body"
        ]
        health_assignments = [
            assignment
            for block in body_blocks
            for assignment in block.assignments
            if assignment.key.casefold() == "maxhealth"
        ]
        if len(health_assignments) != 1:
            raise ValueError(
                f"Object {obj.name} must have exactly one authored Body MaxHealth"
            )
        health = health_assignments[0]
        result["health"] = _number(
            health.value,
            source,
            "Object",
            obj.name,
            health.key,
            health.line,
            defines,
        )
        result["weaponSets"] = _weapon_sets(
            obj,
            source,
            weapon_source,
            defines,
        )
        result["dualWeaponSwitchDistance"] = _dual_weapon_switch_distance(
            obj,
            source,
            defines,
        )
    return result


def extract_retail_unit_rules(
    sources: Mapping[str, Path | str],
    *,
    unit_specs: tuple[tuple[str, str, str, str, str], ...] = _UNIT_SPECS,
) -> dict[str, Any]:
    """Return source-backed runtime rules for the requested Men hordes."""

    if not unit_specs:
        raise ValueError("at least one retail unit spec is required")
    loaded = _load_sources(sources, unit_specs)
    _SOURCE_CONTEXT.clear()
    _SOURCE_CONTEXT.update(loaded)
    try:
        defines = _parse_defines(loaded[GAMEDATA_PATH])
        horde_source = loaded[HORDE_PATH]
        locomotor_source = loaded[LOCOMOTOR_PATH]
        weapon_source = loaded[WEAPON_PATH]
        modifier_source = loaded[ATTRIBUTE_MODIFIER_PATH]
        units: list[dict[str, Any]] = []
        for member_id, horde_id, member_name, horde_name, member_path in unit_specs:
            member_source = loaded[member_path]
            units.append(
                {
                    "id": member_id,
                    "hordeId": horde_id,
                    "member": _object_rules(
                        _one_object(member_source, member_name),
                        member_source,
                        locomotor_source,
                        weapon_source,
                        defines,
                        modifier_source,
                        include_formation=False,
                    ),
                    "horde": _object_rules(
                        _one_object(horde_source, horde_name),
                        horde_source,
                        locomotor_source,
                        weapon_source,
                        defines,
                        modifier_source,
                        include_formation=True,
                    ),
                    "runtimeSelection": {
                        "movement": "horde normal LocomotorSet",
                        "combat": "member WeaponSet conditions and authored slot selection",
                        "upgradeState": "base BFME II 1.06, no player upgrade",
                    },
                }
            )
        return {
            "schema": SCHEMA,
            "schemaVersion": SCHEMA_VERSION,
            "units": units,
            "stanceCommand": _toggle_stance_command(loaded[COMMAND_BUTTON_PATH]),
            "engineSemantics": {
                "distance": "SAGE source distances; runtime multiplies by RetailMapData.local_transform_scale",
                "weaponRange": "center-to-center target position distance",
                "weaponRangeOracle": ".private/scratch/opensage-source/src/OpenSage.Game/Logic/Object/Weapon/Weapon.cs:54",
                "locomotorUnitsOracle": ".private/scratch/opensage-source/src/OpenSage.Game/Data/Ini/IniParser.cs:472",
                "turnTimeOracle": ".private/scratch/opensage-source/src/OpenSage.Game/Logic/Object/LocomotorTemplate.cs:94",
            },
            "sources": [
                {"ini": source.path, "sha256": source.sha256, "byteCount": len(source.payload)}
                for source in sorted(loaded.values(), key=lambda item: item.path)
            ],
        }
    finally:
        _SOURCE_CONTEXT.clear()


__all__ = [
    "OUTPUT_PATH",
    "RANGER_UNIT_SPEC",
    "SCHEMA",
    "SCHEMA_VERSION",
    "extract_retail_unit_rules",
    "retail_unit_rule_source_paths",
]
