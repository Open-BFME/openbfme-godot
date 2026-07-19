"""Compile BFME2 retail references into one runtime playable-unit descriptor.

This module is deliberately object-name agnostic.  It follows authored SAGE
references and classifies the resulting unit by capabilities.  Conversion and
pack publication remain separate pipeline stages; callers provide converted
visual bindings and resolved audio/image leaves when those stages are ready.
"""

from __future__ import annotations

from collections import defaultdict
from collections.abc import Iterable, Mapping, Sequence
from copy import deepcopy
import hashlib
import json
import re

from .sage_cst import (
    SageAssignment,
    SageBlock,
    SageCstError,
    SageObject,
    parse_sage_document,
)
from .sage_ini import IniBlock, parse_flat_named_blocks


SCHEMA = "openbfme.playable-unit-descriptor"
SCHEMA_VERSION = 0
COMMAND_SET_PATH = "data/ini/commandset.ini"
COMMAND_BUTTON_PATH = "data/ini/commandbutton.ini"
PLAYER_TEMPLATE_PATH = "data/ini/playertemplate.ini"

_CATEGORIES = frozenset(
    {"infantry", "ranged-infantry", "cavalry", "hero", "siege", "monster", "naval"}
)


class PlayableUnitCompilerError(ValueError):
    """The requested descriptor cannot be derived without guessing."""


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


def _tokens(value: str) -> tuple[str, ...]:
    return tuple(re.findall(r"[A-Za-z0-9_+.-]+", value))


def _first(values: Sequence[str]) -> str | None:
    for value in values:
        tokens = _tokens(value)
        if tokens and tokens[0].casefold() not in {"none", "null"}:
            return tokens[0]
    return None


def _source_rows(
    documents: Mapping[str, bytes],
    used_paths: Iterable[str],
    semantic_scopes: Mapping[str, Sequence[Mapping[str, object]]],
) -> list[dict[str, object]]:
    wanted = {path.replace("\\", "/").casefold() for path in used_paths}
    return [
        {
            "virtualPath": path.replace("\\", "/"),
            "semanticSha256": _digest(
                list(semantic_scopes.get(path.replace("\\", "/").casefold(), ()))
            ),
        }
        for path, payload in sorted(
            documents.items(), key=lambda item: (item[0].casefold(), item[0])
        )
        if path.replace("\\", "/").casefold() in wanted
    ]


def _assignment_semantic(value: SageAssignment) -> dict[str, object]:
    return {"key": value.key, "value": value.value.strip()}


def _block_semantic(value: SageBlock) -> dict[str, object]:
    return {
        "kind": value.kind,
        "headerKey": value.header_key,
        "instanceTag": value.instance_tag,
        "headerTokens": list(value.header_tokens),
        "assignments": [_assignment_semantic(item) for item in value.assignments],
        "blocks": [_block_semantic(item) for item in value.blocks],
    }


def _object_semantic(value: SageObject) -> dict[str, object]:
    return {
        "kind": value.kind,
        "id": value.name,
        "parent": value.parent,
        "assignments": [_assignment_semantic(item) for item in value.assignments],
        "blocks": [_block_semantic(item) for item in value.blocks],
    }


def _ini_block_semantic(kind: str, value: IniBlock) -> dict[str, object]:
    return {
        "kind": kind,
        "id": value.name,
        "parent": value.parent,
        "assignments": [list(item) for item in value.assignments],
    }


def _object_index(documents: Mapping[str, bytes]) -> dict[str, SageObject]:
    result: dict[str, SageObject] = {}
    for path, source in sorted(
        documents.items(), key=lambda item: (item[0].casefold(), item[0])
    ):
        normalized = path.replace("\\", "/")
        if not normalized.casefold().startswith("data/ini/object/"):
            continue
        if not normalized.casefold().endswith((".ini", ".inc")):
            continue
        try:
            parsed = parse_sage_document(source, normalized).objects
        except SageCstError:
            # The effective tree contains cinematic/include-fragment dialects
            # outside the normal command-reachable object grammar.  They are
            # not part of this requested closure unless the target or a
            # producer resolves only there; that later fails as an unresolved
            # Object instead of making all unrelated fragments global inputs.
            continue
        for item in parsed:
            key = item.name.casefold()
            if key in result:
                raise PlayableUnitCompilerError(
                    f"ambiguous effective Object definition: {item.name}"
                )
            result[key] = item
    if not result:
        raise PlayableUnitCompilerError("no effective Object definitions were supplied")
    return result


def _ancestry(
    index: Mapping[str, SageObject], target: SageObject
) -> tuple[SageObject, ...]:
    result = [target]
    seen = {target.name.casefold()}
    current = target
    while current.parent:
        parent = index.get(current.parent.casefold())
        if parent is None:
            raise PlayableUnitCompilerError(
                f"Object {target.name} has unresolved parent {current.parent}"
            )
        key = parent.name.casefold()
        if key in seen or len(result) >= 64:
            raise PlayableUnitCompilerError(f"Object inheritance cycle: {target.name}")
        seen.add(key)
        result.append(parent)
        current = parent
    return tuple(reversed(result))


def _effective_values(
    ancestry: Sequence[SageObject], key: str
) -> tuple[SageAssignment, ...]:
    selected: tuple[SageAssignment, ...] = ()
    folded = key.casefold()
    for item in ancestry:
        values = tuple(row for row in item.assignments if row.key.casefold() == folded)
        if values:
            selected = values
    return selected


def _effective_top_blocks(ancestry: Sequence[SageObject]) -> tuple[SageBlock, ...]:
    """Apply SAGE module-tag replacement across an Object ancestry."""

    ordered: list[tuple[str, SageBlock]] = []
    positions: dict[str, int] = {}
    for item in ancestry:
        for block in item.blocks:
            conditions = "\0".join(
                assignment.value.strip().casefold()
                for assignment in block.assignments
                if assignment.key.casefold() in {"condition", "conditions"}
            )
            identity = "\0".join(
                (
                    (block.header_key or block.kind).casefold(),
                    (block.instance_tag or conditions or block.raw_header).casefold(),
                )
            )
            if identity in positions:
                ordered[positions[identity]] = (identity, block)
            else:
                positions[identity] = len(ordered)
                ordered.append((identity, block))
    return tuple(block for _, block in ordered)


def _effective_recursive_assignments(
    ancestry: Sequence[SageObject],
) -> Iterable[SageAssignment]:
    effective_scalar_keys = {
        assignment.key.casefold()
        for item in ancestry
        for assignment in item.assignments
    }
    for key in sorted(effective_scalar_keys):
        yield from _effective_values(ancestry, key)
    for block in _effective_top_blocks(ancestry):
        yield from block.assignments
        for nested in _walk_blocks(block.blocks):
            yield from nested.assignments


def _numeric_defines(documents: Mapping[str, bytes]) -> dict[str, int | float]:
    result: dict[str, int | float] = {}
    pattern = re.compile(
        rb"(?m)^[ \t]*#define[ \t]+([A-Za-z_][A-Za-z0-9_]*)[ \t]+(-?(?:[0-9]+(?:\.[0-9]*)?|\.[0-9]+))[ \t]*(?://|;|\r?$)"
    )
    for path, payload in documents.items():
        if path.replace("\\", "/").casefold() != "data/ini/gamedata.ini":
            continue
        for match in pattern.finditer(payload):
            key = match.group(1).decode("ascii").casefold()
            token = match.group(2).decode("ascii")
            value: int | float = float(token) if "." in token else int(token)
            if key in result and result[key] != value:
                raise PlayableUnitCompilerError(
                    f"ambiguous numeric GameData constant: {match.group(1).decode('ascii')}"
                )
            result[key] = value
    return result


def _resolved_expression(
    expression: str, constants: Mapping[str, int | float]
) -> int | float | None:
    token = expression.strip()
    if re.fullmatch(r"-?[0-9]+", token):
        return int(token)
    if re.fullmatch(r"-?(?:[0-9]+\.[0-9]*|\.[0-9]+)", token):
        return float(token)
    return constants.get(token.casefold())


def _resolved_scalar(
    fields: Mapping[str, Mapping[str, object]],
    name: str,
    constants: Mapping[str, int | float],
) -> dict[str, object] | None:
    row = fields.get(name)
    if not isinstance(row, Mapping):
        return None
    expression = str(row.get("expression", ""))
    value = _resolved_expression(expression, constants)
    if value is None:
        return None
    return {
        "value": value,
        "expression": expression,
        "sourceIni": str(row.get("sourceIni", "")),
        "line": int(row.get("line", 0)),
        "constantSourceIni": (
            "data/ini/gamedata.ini" if expression.casefold() in constants else None
        ),
    }


def _effective_body_health(
    ancestry: Sequence[SageObject], constants: Mapping[str, int | float]
) -> dict[str, object] | None:
    bodies = [
        block
        for block in _effective_top_blocks(ancestry)
        if (block.header_key or "").casefold() == "body"
    ]
    values = [
        assignment
        for block in bodies
        for assignment in block.assignments
        if assignment.key.casefold() == "maxhealth"
    ]
    if len(values) != 1:
        return None
    assignment = values[0]
    resolved = _resolved_expression(assignment.value, constants)
    if resolved is None:
        return None
    return {
        "value": resolved,
        "expression": assignment.value.strip(),
        "sourceIni": assignment.source_virtual_path,
        "line": assignment.line,
        "constantSourceIni": (
            "data/ini/gamedata.ini"
            if assignment.value.strip().casefold() in constants
            else None
        ),
    }


def _default_set_block(
    ancestry: Sequence[SageObject], block_name: str
) -> SageBlock | None:
    candidates: list[SageBlock] = []
    for block in _effective_top_blocks(ancestry):
        if (block.header_key or block.kind).casefold() != block_name.casefold():
            continue
        conditions = [
            row.value.strip().casefold()
            for row in block.assignments
            if row.key.casefold() in {"condition", "conditions"}
        ]
        if conditions and all(
            "set_normal" not in _tokens(value.casefold()) and value not in {"none", ""}
            for value in conditions
        ):
            continue
        candidates.append(block)
    return candidates[0] if len(candidates) == 1 else None


def _default_set_target(
    ancestry: Sequence[SageObject], block_name: str, assignment_name: str
) -> str | None:
    block = _default_set_block(ancestry, block_name)
    if block is None:
        return None
    candidates: list[str] = []
    primary_candidates: list[str] = []
    for assignment in block.assignments:
        if assignment.key.casefold() != assignment_name.casefold():
            continue
        tokens = _tokens(assignment.value)
        if tokens:
            candidates.append(tokens[-1])
            if any(token.casefold() == "primary" for token in tokens[:-1]):
                primary_candidates.append(tokens[-1])
    if primary_candidates:
        candidates = primary_candidates
    unique = {value.casefold(): value for value in candidates}
    return next(iter(unique.values())) if len(unique) == 1 else None


def _resolved_set_field(
    ancestry: Sequence[SageObject],
    block_name: str,
    field: str,
    constants: Mapping[str, int | float],
) -> dict[str, object] | None:
    block = _default_set_block(ancestry, block_name)
    if block is None:
        return None
    rows = [row for row in block.assignments if row.key.casefold() == field.casefold()]
    if len(rows) != 1:
        return None
    row = rows[0]
    value = _resolved_expression(row.value, constants)
    if value is None:
        return None
    return {
        "value": value,
        "expression": row.value.strip(),
        "sourceIni": row.source_virtual_path,
        "line": row.line,
        "constantSourceIni": (
            "data/ini/gamedata.ini"
            if row.value.strip().casefold() in constants
            else None
        ),
    }


def _named_definition_values(
    documents: Mapping[str, bytes], kind: str, identifier: str
) -> dict[str, list[dict[str, object]]] | None:
    header = re.compile(
        rf"^{re.escape(kind)}\s+{re.escape(identifier)}\s*$", re.IGNORECASE
    )
    matches: list[dict[str, list[dict[str, object]]]] = []
    for path, payload in sorted(documents.items(), key=lambda item: item[0].casefold()):
        try:
            lines = payload.decode("cp1252").splitlines()
        except UnicodeDecodeError:
            continue
        active = False
        values: dict[str, list[dict[str, object]]] = defaultdict(list)
        for line_number, raw in enumerate(lines, start=1):
            stripped = raw.strip()
            if not active:
                header_text = stripped.split(";", 1)[0].split("//", 1)[0].strip()
                if raw.lstrip() == raw and header.fullmatch(header_text):
                    active = True
                continue
            if raw.lstrip() == raw and stripped.casefold() == "end":
                matches.append(dict(values))
                active = False
                break
            clean = stripped.split(";", 1)[0].strip()
            if "=" not in clean:
                continue
            key, expression = (part.strip() for part in clean.split("=", 1))
            if key and expression:
                values[key.casefold()].append(
                    {
                        "expression": expression,
                        "sourceIni": path.replace("\\", "/"),
                        "line": line_number,
                    }
                )
    if not matches:
        return None
    semantic = {_digest(value): value for value in matches}
    return next(iter(semantic.values())) if len(semantic) == 1 else None


def _default_nested_target(
    documents: Mapping[str, bytes], kind: str, identifier: str, field: str
) -> str | None:
    candidates: dict[str, str] = {}
    for payload in documents.values():
        try:
            blocks = parse_flat_named_blocks(payload, kind)
        except (UnicodeDecodeError, ValueError):
            continue
        for block in blocks:
            if block.name.casefold() != identifier.casefold():
                continue
            values = [value for value in (_first((row,)) for row in block.values(field)) if value]
            if len(values) == 1:
                candidates[values[0].casefold()] = values[0]
    return next(iter(candidates.values())) if len(candidates) == 1 else None


def _resolved_definition_field(
    definition: Mapping[str, Sequence[Mapping[str, object]]] | None,
    field: str,
    constants: Mapping[str, int | float],
) -> dict[str, object] | None:
    if definition is None:
        return None
    rows = definition.get(field.casefold(), ())
    resolved: list[dict[str, object]] = []
    for row in rows:
        expression = str(row.get("expression", ""))
        value = _resolved_expression(expression, constants)
        if value is not None:
            resolved.append(
                {
                    "value": value,
                    "expression": expression,
                    "sourceIni": str(row.get("sourceIni", "")),
                    "line": int(row.get("line", 0)),
                    "constantSourceIni": (
                        "data/ini/gamedata.ini"
                        if expression.casefold() in constants
                        else None
                    ),
                }
            )
    by_value: dict[str, list[dict[str, object]]] = defaultdict(list)
    for row in resolved:
        by_value[_digest(row["value"])].append(row)
    if len(by_value) != 1:
        return None
    equivalent = next(iter(by_value.values()))
    result = dict(equivalent[0])
    if len(equivalent) > 1:
        result["equivalentSources"] = [
            {"sourceIni": row["sourceIni"], "line": row["line"]}
            for row in equivalent
        ]
    return result


def _simulation_contract(
    container_fields: Mapping[str, Mapping[str, object]],
    member_fields: Mapping[str, Mapping[str, object]],
    member_lineage: Sequence[SageObject],
    members: Sequence[Mapping[str, object]],
    constants: Mapping[str, int | float],
    documents: Mapping[str, bytes],
    container_lineage: Sequence[SageObject],
) -> dict[str, object]:
    resolved: dict[str, object] = {}
    required = {
        "buildCost": (container_fields, "BuildCost"),
        "buildTimeSeconds": (container_fields, "BuildTime"),
        "commandPoints": (container_fields, "CommandPoints"),
        "visionRange": (member_fields, "VisionRange"),
    }
    missing: list[str] = []
    for output_name, (owner, source_name) in required.items():
        row = _resolved_scalar(owner, source_name, constants)
        if row is None:
            missing.append(output_name)
        else:
            resolved[output_name] = row
    health = _effective_body_health(member_lineage, constants)
    if health is None:
        missing.append("memberHealth")
    else:
        resolved["memberHealth"] = health
    member_count = sum(int(row.get("count", 0)) for row in members)
    if member_count <= 0:
        missing.append("memberCount")
    else:
        resolved["memberCount"] = {
            "value": member_count,
            "source": "composition.members",
        }
    display = member_fields.get("DisplayName") or container_fields.get("DisplayName")
    if not isinstance(display, Mapping) or not str(display.get("expression", "")):
        missing.append("displayNameId")
    else:
        resolved["displayNameId"] = {
            "value": str(display["expression"]),
            "sourceIni": str(display.get("sourceIni", "")),
            "line": int(display.get("line", 0)),
        }
    locomotor_id = _default_set_target(member_lineage, "LocomotorSet", "Locomotor")
    speed = _resolved_set_field(member_lineage, "LocomotorSet", "Speed", constants)
    if speed is None:
        missing.append("speed")
    else:
        speed["definitionId"] = locomotor_id
        resolved["speed"] = speed
    locomotor = (
        _named_definition_values(documents, "Locomotor", locomotor_id)
        if locomotor_id
        else None
    )
    movement: dict[str, object] = {}
    if locomotor is not None:
        for output_name, source_name in (
            ("acceleration", "Acceleration"),
            ("braking", "Braking"),
        ):
            field = _resolved_definition_field(locomotor, source_name, constants)
            if field is not None:
                movement[output_name] = field
        turn_rate = _resolved_definition_field(locomotor, "TurnRate", constants)
        if turn_rate is None:
            turn_time = _resolved_definition_field(locomotor, "TurnTime", constants)
            if turn_time is not None and float(turn_time["value"]) > 0.0:
                turn_rate = dict(turn_time)
                turn_rate["value"] = 360000.0 / float(turn_time["value"])
                turn_rate["semantic"] = "360 degrees divided by TurnTime seconds"
        if turn_rate is not None:
            movement["turnRateDegreesPerSecond"] = turn_rate
    for field in ("acceleration", "braking", "turnRateDegreesPerSecond"):
        if field not in movement:
            missing.append(field)
    if movement:
        movement["locomotorId"] = locomotor_id
        resolved["movement"] = movement
    weapon_id = _default_set_target(member_lineage, "WeaponSet", "Weapon")
    weapon = (
        _named_definition_values(documents, "Weapon", weapon_id) if weapon_id else None
    )
    if weapon_id and weapon is not None:
        combat: dict[str, object] = {"weaponId": weapon_id}
        for output_name, source_name in (
            ("attackRange", "AttackRange"),
            ("minimumAttackRange", "MinimumAttackRange"),
            ("projectileSpeed", "WeaponSpeed"),
            ("delayBetweenShotsMs", "DelayBetweenShots"),
            ("preAttackDelayMs", "PreAttackDelay"),
            ("firingDurationMs", "FiringDuration"),
            ("damage", "Damage"),
        ):
            field = _resolved_definition_field(weapon, source_name, constants)
            if field is not None:
                combat[output_name] = field
        damage_owner = weapon
        warheads = {
            str(row.get("expression", "")).casefold(): str(row.get("expression", ""))
            for key in ("warheadtemplatename", "warhead")
            for row in weapon.get(key, ())
            if str(row.get("expression", ""))
        }
        warhead_id = (
            next(iter(warheads.values()))
            if len(warheads) == 1
            else _default_nested_target(
                documents, "Weapon", weapon_id, "WarheadTemplateName"
            )
        )
        if warhead_id:
            warhead = _named_definition_values(documents, "Weapon", warhead_id)
            if warhead is not None:
                damage_owner = warhead
                combat["warheadId"] = warhead_id
                damage = _resolved_definition_field(warhead, "Damage", constants)
                if damage is not None:
                    combat["damage"] = damage
        projectiles = {
            str(row.get("expression", "")).casefold(): str(row.get("expression", ""))
            for row in weapon.get("projectiletemplatename", ())
            if str(row.get("expression", ""))
        }
        projectile_id = (
            next(iter(projectiles.values()))
            if len(projectiles) == 1
            else _default_nested_target(
                documents, "Weapon", weapon_id, "ProjectileTemplateName"
            )
        )
        if projectile_id:
            combat["projectileObjectId"] = projectile_id
        damage_types = damage_owner.get("damagetype", ())
        unique_damage_types = {
            str(row.get("expression", "")).casefold(): str(row.get("expression", ""))
            for row in damage_types
            if str(row.get("expression", ""))
        }
        if len(unique_damage_types) == 1:
            combat["damageType"] = next(iter(unique_damage_types.values()))
        for output_name, source_name in (
            ("clipSize", "ClipSize"),
            ("clipReloadTimeMs", "ClipReloadTime"),
            ("continuousFireOne", "ContinuousFireOne"),
            ("continuousFireCoastMs", "ContinuousFireCoast"),
        ):
            field = _resolved_definition_field(weapon, source_name, constants)
            if field is not None:
                combat[output_name] = field
        resolved["combat"] = combat
    else:
        missing.append("combat.weapon")
    combat_value = resolved.get("combat", {})
    if isinstance(combat_value, Mapping):
        for field in ("attackRange", "delayBetweenShotsMs", "preAttackDelayMs", "firingDurationMs", "damage"):
            if field not in combat_value:
                missing.append(f"combat.{field}")
    formation = _formation_contract(container_lineage, members)
    if formation is None:
        missing.append("formation")
    else:
        resolved["formation"] = formation
    return {
        "status": "ready" if not missing else "unresolved",
        "resolved": resolved,
        "missing": sorted(set(missing), key=str.casefold),
    }


def _formation_contract(
    lineage: Sequence[SageObject], members: Sequence[Mapping[str, object]]
) -> dict[str, object] | None:
    member_count = sum(int(row.get("count", 0)) for row in members)
    if member_count == 1 and len(members) == 1:
        return {
            "memberCount": 1,
            "positions": [{"x": 0, "y": 0}],
            "source": "singleton-composition",
        }
    rank_rows: list[dict[str, object]] = []
    position_pattern = re.compile(
        r"Position\s*:\s*X\s*:\s*(-?(?:\d+(?:\.\d*)?|\.\d+))\s+Y\s*:\s*(-?(?:\d+(?:\.\d*)?|\.\d+))",
        re.I,
    )
    for block in _effective_top_blocks(lineage):
        if block.kind.casefold() not in {
            "hordecontain",
            "horsehordecontain",
        }:
            continue
        for assignment in block.assignments:
            if assignment.key.casefold() != "rankinfo":
                continue
            positions = [
                {"x": float(x), "y": float(y)}
                for x, y in position_pattern.findall(assignment.value)
            ]
            if not positions:
                return None
            rank_rows.append(
                {
                    "positions": positions,
                    "sourceIni": assignment.source_virtual_path,
                    "line": assignment.line,
                }
            )
    positions = [position for rank in rank_rows for position in rank["positions"]]
    if len(positions) != member_count:
        return None
    return {"memberCount": member_count, "positions": positions, "ranks": rank_rows}


def _provenance_paths(value: object) -> set[str]:
    result: set[str] = set()
    if isinstance(value, Mapping):
        for key, child in value.items():
            if (
                key in {"sourceIni", "constantSourceIni"}
                and isinstance(child, str)
                and child
            ):
                result.add(child)
            else:
                result.update(_provenance_paths(child))
    elif isinstance(value, Sequence) and not isinstance(value, (str, bytes)):
        for child in value:
            result.update(_provenance_paths(child))
    return result


def _walk_blocks(blocks: Iterable[SageBlock]) -> Iterable[SageBlock]:
    for block in blocks:
        yield block
        yield from _walk_blocks(block.blocks)


def _recursive_assignments(objects: Sequence[SageObject]) -> Iterable[SageAssignment]:
    for item in objects:
        yield from item.assignments
        for block in _walk_blocks(item.blocks):
            yield from block.assignments


def _named_blocks(source: bytes, kind: str) -> dict[str, IniBlock]:
    result: dict[str, IniBlock] = {}
    for block in parse_flat_named_blocks(source, kind):
        key = block.name.casefold()
        if key in result:
            if result[key].assignments == block.assignments:
                continue
            raise PlayableUnitCompilerError(f"ambiguous {kind}: {block.name}")
        result[key] = block
    return result


def _required_document(documents: Mapping[str, bytes], path: str) -> bytes:
    for candidate, payload in documents.items():
        if candidate.replace("\\", "/").casefold() == path.casefold():
            return payload
    raise PlayableUnitCompilerError(f"required effective source is missing: {path}")


def _player_template_context(
    documents: Mapping[str, bytes], faction_graph: Mapping[str, object]
) -> tuple[list[str], str, str]:
    target = faction_graph.get("target", {})
    if not isinstance(target, Mapping):
        raise PlayableUnitCompilerError("faction graph target is invalid")
    template_id = str(target.get("playerTemplate", ""))
    if not template_id:
        raise PlayableUnitCompilerError("faction graph has no playerTemplate identity")
    templates = _named_blocks(
        _required_document(documents, PLAYER_TEMPLATE_PATH), "PlayerTemplate"
    )
    template = templates.get(template_id.casefold())
    if template is None:
        raise PlayableUnitCompilerError(
            f"effective PlayerTemplate is missing: {template_id}"
        )
    roster_values = _block_values(template, "BuildableHeroesMP")
    if len(roster_values) != 1:
        raise PlayableUnitCompilerError(
            f"PlayerTemplate {template_id} must author one BuildableHeroesMP roster"
        )
    roster = list(_tokens(roster_values[0]))
    folded_roster: set[str] = set()
    for object_id in roster:
        folded = object_id.casefold()
        if folded in folded_roster:
            raise PlayableUnitCompilerError(
                f"PlayerTemplate {template_id} has duplicate BuildableHeroesMP hero: {object_id}"
            )
        folded_roster.add(folded)
    starting_values = _block_values(template, "StartingBuilding")
    starting_building = _first(starting_values) or ""
    return roster, starting_building, template_id


def _block_values(block: IniBlock, key: str) -> tuple[str, ...]:
    return tuple(block.values(key))


def _command_slots(block: IniBlock) -> tuple[tuple[int, str], ...]:
    result: list[tuple[int, str]] = []
    used: set[int] = set()
    for key, value in block.assignments:
        if re.fullmatch(r"[0-9]+", key) is None:
            continue
        slot = int(key)
        command = _first((value,))
        if slot < 1 or slot in used or not command:
            raise PlayableUnitCompilerError(
                f"CommandSet {block.name} has an invalid slot {key}"
            )
        used.add(slot)
        result.append((slot, command))
    return tuple(sorted(result))


def _producer_bindings(
    target_id: str,
    objects: Mapping[str, SageObject],
    command_sets: Mapping[str, IniBlock],
    command_buttons: Mapping[str, IniBlock],
    reachable_object_ids: frozenset[str] | None = None,
) -> tuple[dict[str, object], ...]:
    train_commands: dict[str, dict[str, object]] = {}
    for button in command_buttons.values():
        commands = {value.casefold() for value in _block_values(button, "Command")}
        targets = tuple(
            filter(
                None, (_first((value,)) for value in _block_values(button, "Object"))
            )
        )
        if commands in ({"unit_build"}, {"hero_build"}) and any(
            value.casefold() == target_id.casefold() for value in targets
        ):
            train_commands[button.name.casefold()] = {
                "id": button.name,
                "button": button,
            }
    if not train_commands:
        raise PlayableUnitCompilerError(
            f"Object {target_id} is not targeted by an authored UNIT_BUILD command"
        )

    set_bindings: list[tuple[IniBlock, int, dict[str, object]]] = []
    for command_set in command_sets.values():
        for slot, command_id in _command_slots(command_set):
            command = train_commands.get(command_id.casefold())
            if command is not None:
                set_bindings.append((command_set, slot, command))

    result: list[dict[str, object]] = []
    for producer in objects.values():
        if (
            reachable_object_ids is not None
            and producer.name.casefold() not in reachable_object_ids
        ):
            continue
        try:
            lineage = _ancestry(objects, producer)
        except PlayableUnitCompilerError:
            # An unrelated partial inheritance family is outside this target
            # closure.  If it is the only possible producer, the absence of a
            # resolved binding below still fails the requested unit.
            continue
        direct_sets = {
            value.casefold(): value
            for value in (
                _first((row.value,)) for row in _effective_values(lineage, "CommandSet")
            )
            if value
        }
        upgraded_sets: dict[str, list[dict[str, object]]] = defaultdict(list)
        for block in _walk_blocks(_effective_top_blocks(lineage)):
            set_id = _first(block.values("CommandSet"))
            if not set_id:
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
            upgraded_sets[set_id.casefold()].append(
                {
                    "sourceObject": producer.name,
                    "module": block.kind,
                    "triggeredBy": triggers,
                    "sourceIni": block.source_virtual_path,
                    "line": block.line,
                }
            )
        for command_set, slot, command in set_bindings:
            key = command_set.name.casefold()
            direct = key in direct_sets
            transitions = upgraded_sets.get(key, [])
            if not direct and not transitions:
                continue
            button = command["button"]
            assert isinstance(button, IniBlock)
            direct_requirements = sorted(
                {
                    token
                    for field in ("NeededUpgrade", "Upgrade", "Options")
                    for value in _block_values(button, field)
                    for token in _tokens(value)
                    if token.startswith(("Upgrade_", "SCIENCE_"))
                },
                key=str.casefold,
            )
            transition_requirements = sorted(
                {
                    value
                    for transition in transitions
                    for value in transition["triggeredBy"]
                },
                key=str.casefold,
            )
            result.append(
                {
                    "producerObjectId": producer.name,
                    "commandSetId": command_set.name,
                    "commandId": command["id"],
                    "surface": "command-socket",
                    "slot": slot,
                    "prerequisites": sorted(
                        set(direct_requirements + transition_requirements),
                        key=str.casefold,
                    ),
                    "commandSetTransition": transitions,
                    "source": {
                        "producerIni": producer.source_virtual_path,
                        "commandSetIni": COMMAND_SET_PATH,
                        "commandButtonIni": COMMAND_BUTTON_PATH,
                    },
                    "ui": {
                        key: list(_block_values(button, key))
                        for key in ("ButtonImage", "TextLabel", "DescriptLabel")
                        if _block_values(button, key)
                    },
                }
            )
    if not result:
        raise PlayableUnitCompilerError(
            f"no producer CommandSet reaches Object {target_id}"
        )
    return tuple(
        sorted(
            result,
            key=lambda row: (
                str(row["producerObjectId"]).casefold(),
                str(row["commandSetId"]).casefold(),
                int(row["slot"]),
            ),
        )
    )


def _member_rows(
    target: SageObject,
    ancestry: Sequence[SageObject],
    objects: Mapping[str, SageObject],
    constants: Mapping[str, int],
) -> tuple[
    tuple[dict[str, object], ...],
    SageObject,
    frozenset[tuple[str, int, str, str]],
]:
    payloads: list[dict[str, object]] = []
    consumed_modules: set[tuple[str, int, str, str]] = set()
    for block in _effective_top_blocks(ancestry):
        assignments = list(block.assignments)
        for nested in _walk_blocks(block.blocks):
            assignments.extend(nested.assignments)
        for assignment in assignments:
            if assignment.key.casefold() != "initialpayload":
                continue
            tokens = _tokens(assignment.value)
            if not tokens:
                continue
            count_expression = assignment.value.strip()[len(tokens[0]) :].strip()
            count = (
                1
                if not count_expression
                else _resolve_integer_expression(count_expression, constants)
            )
            if count < 1:
                raise PlayableUnitCompilerError(
                    f"Object {target.name} InitialPayload count is not positive"
                )
            member = objects.get(tokens[0].casefold())
            if member is None:
                raise PlayableUnitCompilerError(
                    f"Object {target.name} has unresolved InitialPayload {tokens[0]}"
                )
            payloads.append(
                {
                    "objectId": member.name,
                    "count": count,
                    "countExpression": count_expression or "1",
                    "sourceIni": assignment.source_virtual_path,
                    "line": assignment.line,
                }
            )
            consumed_modules.add(
                (
                    block.source_virtual_path.casefold(),
                    block.line,
                    (block.instance_tag or "").casefold(),
                    block.kind.casefold(),
                )
            )
    if not payloads:
        return (
            ({"objectId": target.name, "count": 1},),
            target,
            frozenset(),
        )
    primary = objects[str(payloads[0]["objectId"]).casefold()]
    return (tuple(payloads), primary, frozenset(consumed_modules))


def _resolve_integer_expression(expression: str, constants: Mapping[str, int]) -> int:
    token = expression.strip()
    if re.fullmatch(r"[0-9]+", token):
        return int(token)
    constant = constants.get(token.casefold())
    if constant is not None:
        return constant
    match = re.fullmatch(r"#(MULTIPLY|DIVIDE|ADD|SUBTRACT)\s*\((.*)\)", token, re.I)
    if match is None:
        raise PlayableUnitCompilerError(f"unresolved integer expression: {expression}")
    arguments = _tokens(match.group(2))
    if len(arguments) != 2:
        raise PlayableUnitCompilerError(
            f"integer expression requires two arguments: {expression}"
        )
    left = _resolve_integer_expression(arguments[0], constants)
    right = _resolve_integer_expression(arguments[1], constants)
    operation = match.group(1).upper()
    if operation == "MULTIPLY":
        return left * right
    if operation == "ADD":
        return left + right
    if operation == "SUBTRACT":
        return left - right
    if right == 0 or left % right != 0:
        raise PlayableUnitCompilerError(f"integer division is not exact: {expression}")
    return left // right


def _horde_containers(
    member_id: str, objects: Mapping[str, SageObject]
) -> tuple[SageObject, ...]:
    result: list[SageObject] = []
    for candidate in objects.values():
        for assignment in _recursive_assignments((candidate,)):
            if assignment.key.casefold() != "initialpayload":
                continue
            target = _first((assignment.value,))
            if target and target.casefold() == member_id.casefold():
                result.append(candidate)
                break
    return tuple(sorted(result, key=lambda item: item.name.casefold()))


def _kind_of(ancestry: Sequence[SageObject]) -> tuple[str, ...]:
    values = _effective_values(ancestry, "KindOf")
    return tuple(
        sorted({token.upper() for row in values for token in _tokens(row.value)})
    )


def _category(
    target_kinds: Sequence[str], member_kinds: Sequence[str], has_horde: bool
) -> str:
    kinds = set(target_kinds) | set(member_kinds)
    if kinds & {"SHIP", "NAVAL_UNIT", "TRANSPORT"}:
        return "naval"
    if "HERO" in kinds:
        return "hero"
    if kinds & {"SIEGEENGINE", "MACHINE", "SIEGE_WEAPON"}:
        return "siege"
    if kinds & {"MONSTER", "GIANT", "TROLL"}:
        return "monster"
    if "CAVALRY" in kinds:
        return "cavalry"
    if kinds & {"ARCHER", "RANGED"}:
        return "ranged-infantry"
    if has_horde or "INFANTRY" in kinds:
        return "infantry"
    raise PlayableUnitCompilerError(
        "unit category cannot be inferred from retail KindOf capabilities"
    )


def _capability_contract(
    category: str,
    kinds: Sequence[str],
    has_horde: bool,
    gameplay_fields: Mapping[str, object],
    references: Mapping[str, Sequence[Mapping[str, object]]],
    special_modules: Sequence[str],
) -> tuple[list[dict[str, object]], list[dict[str, object]], list[str]]:
    kind_set = set(kinds)
    capabilities: list[dict[str, object]] = []

    def add(identifier: str, evidence: str) -> None:
        capabilities.append({"id": identifier, "evidence": evidence})

    if "SELECTABLE" in kind_set:
        add("select", "KindOf:SELECTABLE")
    if "LocomotorSet" in gameplay_fields or references.get("locomotor"):
        add("move", "LocomotorSet/Locomotor reference")
    if references.get("weapon"):
        if kind_set & {"SIEGEENGINE", "SIEGE_WEAPON"}:
            add("siege-attack", "Weapon reference + siege KindOf")
        elif kind_set & {"ARCHER", "RANGED"}:
            add("ranged-attack", "Weapon reference + ranged KindOf")
        else:
            add("attack", "Weapon reference")
    if references.get("projectileobject"):
        add("projectile", "ProjectileObject reference")
    if has_horde:
        add("formation", "InitialPayload horde composition")
        add("member-death", "InitialPayload horde composition")
    else:
        add("death", "singleton composition")
    if "HERO" in kind_set and "ExperienceValue" in gameplay_fields:
        add("level", "KindOf:HERO + ExperienceValue")
    if kind_set & {"SHIP", "NAVAL_UNIT"} and (
        "LocomotorSet" in gameplay_fields or references.get("locomotor")
    ):
        add("water-locomotion", "naval KindOf + locomotor reference")
    if "TRANSPORT" in kind_set:
        add("transport", "KindOf:TRANSPORT")
    for module in special_modules:
        add(f"special-module:{module}", f"module:{module}")

    traits: list[str] = []
    if kind_set & {"ARCHER", "RANGED"}:
        traits.append("ranged")
    if "CAVALRY" in kind_set:
        traits.append("mounted")
    if kind_set & {"AIRCRAFT", "FLYING"}:
        traits.append("flying")
    if "TRANSPORT" in kind_set:
        traits.append("transport")
    traits.sort()
    unsupported = [
        {
            "id": f"module:{module}",
            "reason": "requires a category extension before runtime integration",
        }
        for module in special_modules
    ]
    return capabilities, unsupported, traits


def _scalar_fields(ancestry: Sequence[SageObject]) -> dict[str, dict[str, object]]:
    result: dict[str, dict[str, object]] = {}
    for field in (
        "BuildCost",
        "BuildTime",
        "CommandPoints",
        "VisionRange",
        "ShroudClearingRange",
        "DisplayName",
        "DescriptionStrategic",
        "SelectPortrait",
        "ButtonImage",
        "LocomotorSet",
        "ArmorSet",
        "ExperienceValue",
        "CrusherLevel",
        "CrushableLevel",
    ):
        values = _effective_values(ancestry, field)
        if not values:
            continue
        row = values[-1]
        result[field] = {
            "expression": row.value.strip(),
            "sourceIni": row.source_virtual_path,
            "line": row.line,
        }
    return result


def _nested_references(
    ancestry: Sequence[SageObject],
) -> dict[str, list[dict[str, object]]]:
    result: dict[str, list[dict[str, object]]] = defaultdict(list)
    for assignment in _effective_recursive_assignments(ancestry):
        folded = assignment.key.casefold()
        if folded in {
            "model",
            "skeleton",
            "projectileobject",
            "weapon",
            "locomotor",
            "armor",
        }:
            tokens = _tokens(assignment.value)
            if not tokens:
                continue
            target = tokens[-1] if folded == "weapon" and len(tokens) > 1 else tokens[0]
            result[folded].append(
                {
                    "id": target,
                    "expression": assignment.value.strip(),
                    "sourceIni": assignment.source_virtual_path,
                    "line": assignment.line,
                }
            )
    return {
        key: sorted(
            rows,
            key=lambda row: (
                str(row["id"]).casefold(),
                str(row["sourceIni"]).casefold(),
                int(row["line"]),
            ),
        )
        for key, rows in sorted(result.items())
    }


def _audio_routes(
    ancestry: Sequence[SageObject],
    authored_edges: frozenset[tuple[str, str]] | None = None,
) -> dict[str, list[dict[str, object]]]:
    result: dict[str, list[dict[str, object]]] = defaultdict(list)
    for assignment in _effective_recursive_assignments(ancestry):
        folded = assignment.key.casefold()
        if authored_edges is None:
            if not folded.startswith(("voice", "sound", "eva")):
                continue
            identifiers = [_first((assignment.value,))]
        else:
            tokens = {token.casefold() for token in _tokens(assignment.value)}
            identifiers = sorted(
                {
                    target
                    for field, target in authored_edges
                    if field == folded and target.casefold() in tokens
                },
                key=str.casefold,
            )
        for identifier in identifiers:
            if not identifier:
                continue
            result[assignment.key].append(
                {
                    "id": identifier,
                    "sourceIni": assignment.source_virtual_path,
                    "line": assignment.line,
                }
            )
    return {
        key: rows
        for key, rows in sorted(result.items(), key=lambda item: item[0].casefold())
    }


def _runtime_module_evidence(
    target_lineage: Sequence[SageObject],
    member_lineage: Sequence[SageObject],
    consumed_container_modules: frozenset[tuple[str, int, str, str]],
) -> list[dict[str, object]]:
    result: list[dict[str, object]] = []
    owners: list[tuple[str, Sequence[SageObject]]] = [("container", target_lineage)]
    if target_lineage[-1].name.casefold() != member_lineage[-1].name.casefold():
        owners.append(("primaryMember", member_lineage))
    for role, lineage in owners:
        for block in _walk_blocks(_effective_top_blocks(lineage)):
            if (block.header_key or "").casefold() != "behavior":
                continue
            semantic = _block_semantic(block)
            identity = (
                block.source_virtual_path.casefold(),
                block.line,
                (block.instance_tag or "").casefold(),
                block.kind.casefold(),
            )
            consumes_horde = (
                role == "container" and identity in consumed_container_modules
            )
            result.append(
                {
                    "ownerRole": role,
                    "kind": block.kind,
                    "instanceTag": block.instance_tag or "",
                    "sourceIni": block.source_virtual_path,
                    "line": block.line,
                    "semanticSha256": _digest(semantic),
                    "consumed": consumes_horde,
                }
            )
    return sorted(
        result,
        key=lambda row: (
            str(row["ownerRole"]),
            str(row["kind"]).casefold(),
            str(row["instanceTag"]).casefold(),
            str(row["sourceIni"]).casefold(),
            int(row["line"]),
        ),
    )


def _ui_binding(
    producers: Sequence[Mapping[str, object]],
    command_buttons: Mapping[str, IniBlock],
    target_lineage: Sequence[SageObject],
    member_lineage: Sequence[SageObject],
    command_audio: Mapping[str, Sequence[Mapping[str, object]]],
) -> dict[str, object]:
    portraits: list[str] = []
    for lineage in (target_lineage, member_lineage):
        for key in ("SelectPortrait", "ButtonImage"):
            portraits.extend(
                row.value.strip() for row in _effective_values(lineage, key)
            )
    return {
        "commands": [
            {
                "commandId": str(row["commandId"]),
                "fields": dict(row.get("ui", {})),
                "audioRoutes": [
                    {
                        "field": str(route["field"]),
                        "id": str(route["targetId"]),
                        "tokenOrdinal": int(route["tokenOrdinal"]),
                        "resolution": str(route["resolution"]),
                        "sourceIni": COMMAND_BUTTON_PATH,
                    }
                    for route in command_audio.get(str(row["commandId"]).casefold(), ())
                ],
            }
            for row in producers
        ],
        "portraitImageIds": sorted(set(portraits), key=str.casefold),
    }


def compile_playable_unit_descriptor(
    target_id: str,
    documents: Mapping[str, bytes],
    *,
    converted_visuals: Mapping[str, Mapping[str, object]] | None = None,
    resolved_images: Mapping[str, Mapping[str, object]] | None = None,
    resolved_audio: Mapping[str, Sequence[str]] | None = None,
    resolved_strings: Mapping[str, str] | None = None,
    faction_graph: Mapping[str, object] | None = None,
) -> dict[str, object]:
    """Compile one source-backed descriptor or fail on an unresolved core edge."""

    if not target_id or len(target_id) > 256:
        raise PlayableUnitCompilerError("target Object id is invalid")
    objects = _object_index(documents)
    requested_target = objects.get(target_id.casefold())
    if requested_target is None:
        raise PlayableUnitCompilerError(f"effective Object is missing: {target_id}")
    command_sets = _named_blocks(
        _required_document(documents, COMMAND_SET_PATH), "CommandSet"
    )
    command_buttons = _named_blocks(
        _required_document(documents, COMMAND_BUTTON_PATH), "CommandButton"
    )
    reachable_object_ids: frozenset[str] | None = None
    audio_edges_by_object: dict[str, frozenset[tuple[str, str]]] | None = None
    command_audio: dict[str, tuple[Mapping[str, object], ...]] = {}
    hero_roster: list[str] = []
    starting_building = ""
    player_template_id = ""
    if faction_graph is not None:
        definitions = faction_graph.get("definitions", {})
        if not isinstance(definitions, Mapping):
            raise PlayableUnitCompilerError("faction graph definitions are invalid")
        rows = definitions.get("objects", [])
        if not isinstance(rows, list):
            raise PlayableUnitCompilerError("faction graph object rows are invalid")
        reachable_object_ids = frozenset(
            str(row.get("id", "")).casefold()
            for row in rows
            if isinstance(row, Mapping) and row.get("id")
        )
        audio_edges_by_object = {}
        for row in rows:
            if not isinstance(row, Mapping) or not row.get("id"):
                continue
            raw_edges = row.get("edges", [])
            if not isinstance(raw_edges, list):
                raise PlayableUnitCompilerError(
                    "faction graph Object edges are invalid"
                )
            audio_edges_by_object[str(row["id"]).casefold()] = frozenset(
                (
                    str(edge.get("field", "")).casefold(),
                    str(edge.get("targetId", "")),
                )
                for edge in raw_edges
                if isinstance(edge, Mapping)
                and edge.get("targetKind") == "audio-definition"
                and edge.get("field")
                and edge.get("targetId")
            )
        raw_command_rows = definitions.get("commandButtons", [])
        if not isinstance(raw_command_rows, list):
            raise PlayableUnitCompilerError(
                "faction graph CommandButton rows are invalid"
            )
        for row in raw_command_rows:
            if not isinstance(row, Mapping) or not row.get("id"):
                continue
            routes = row.get("audioRoutes", [])
            if not isinstance(routes, list) or any(
                not isinstance(route, Mapping)
                or not isinstance(route.get("field"), str)
                or not route.get("field")
                or not isinstance(route.get("targetId"), str)
                or not route.get("targetId")
                or not isinstance(route.get("tokenOrdinal"), int)
                or isinstance(route.get("tokenOrdinal"), bool)
                or int(route["tokenOrdinal"]) < 0
                or route.get("resolution") not in {"resolved", "unresolved"}
                for route in routes
            ):
                raise PlayableUnitCompilerError(
                    "faction graph CommandButton audio routes are invalid"
                )
            command_audio[str(row["id"]).casefold()] = tuple(routes)
        hero_roster, starting_building, player_template_id = _player_template_context(
            documents, faction_graph
        )
    target = requested_target
    is_roster_hero = target.name.casefold() in {
        value.casefold() for value in hero_roster
    }
    direct_error: PlayableUnitCompilerError | None = None
    direct_producers: tuple[dict[str, object], ...] = ()
    try:
        direct_producers = _producer_bindings(
            target.name, objects, command_sets, command_buttons, reachable_object_ids
        )
    except PlayableUnitCompilerError as error:
        direct_error = error
    if is_roster_hero:
        if direct_producers:
            raise PlayableUnitCompilerError(
                f"hero {target.name} has conflicting hero-roster and command-socket routes"
            )
        if (
            not starting_building
            or starting_building.casefold() not in objects
            or reachable_object_ids is None
            or starting_building.casefold() not in reachable_object_ids
        ):
            raise PlayableUnitCompilerError(
                f"hero {target.name} has no reachable starting-fortress producer"
            )
        roster_ordinal = next(
            index + 1
            for index, value in enumerate(hero_roster)
            if value.casefold() == target.name.casefold()
        )
        producers = (
            {
                "producerObjectId": objects[starting_building.casefold()].name,
                "commandSetId": "__engine__/BuildableHeroesMP",
                "commandId": f"__engine__/HERO_BUILD/{target.name}",
                "surface": "hero-roster",
                "rosterOrdinal": roster_ordinal,
                "prerequisites": [],
                "commandSetTransition": [],
                "sourceField": "BuildableHeroesMP",
                "sourcePlayerTemplate": player_template_id,
                "ui": {},
            },
        )
    elif direct_producers:
        producers = direct_producers
    else:
        assert direct_error is not None
        containers = _horde_containers(target.name, objects)
        reachable: list[tuple[SageObject, tuple[dict[str, object], ...]]] = []
        for container in containers:
            try:
                reachable.append(
                    (
                        container,
                        _producer_bindings(
                            container.name,
                            objects,
                            command_sets,
                            command_buttons,
                            reachable_object_ids,
                        ),
                    )
                )
            except PlayableUnitCompilerError:
                continue
        if len(reachable) == 1:
            target, producers = reachable[0]
        else:
            raise direct_error
    target_lineage = _ancestry(objects, target)
    members, primary_member, consumed_container_modules = _member_rows(
        target, target_lineage, objects, _numeric_defines(documents)
    )
    member_lineage = _ancestry(objects, primary_member)
    container_audio_edges = (
        frozenset().union(
            *(
                audio_edges_by_object.get(item.name.casefold(), frozenset())
                for item in target_lineage
            )
        )
        if audio_edges_by_object is not None
        else None
    )
    member_audio_edges = (
        frozenset().union(
            *(
                audio_edges_by_object.get(item.name.casefold(), frozenset())
                for item in member_lineage
            )
        )
        if audio_edges_by_object is not None
        else None
    )
    target_kinds = _kind_of(target_lineage)
    member_kinds = _kind_of(member_lineage)
    category = _category(
        target_kinds, member_kinds, bool(members[0]["objectId"] != target.name)
    )
    visual_refs = _nested_references(member_lineage)
    if primary_member is not target:
        target_refs = _nested_references(target_lineage)
        for key, rows in target_refs.items():
            visual_refs.setdefault(key, []).extend(rows)
    visual_bindings = dict(converted_visuals or {})
    unresolved_visuals = sorted(
        {
            str(row["id"])
            for row in visual_refs.get("model", [])
            if str(row["id"]).casefold()
            not in {key.casefold() for key in visual_bindings}
        },
        key=str.casefold,
    )
    module_evidence = _runtime_module_evidence(
        target_lineage, member_lineage, consumed_container_modules
    )
    runtime_modules = sorted(
        {str(row["kind"]) for row in module_evidence}, key=str.casefold
    )
    unsupported_module_evidence = [
        row for row in module_evidence if row["consumed"] is not True
    ]
    unsupported_modules = sorted(
        {str(row["kind"]) for row in unsupported_module_evidence}, key=str.casefold
    )
    container_fields = _scalar_fields(target_lineage)
    member_fields = _scalar_fields(member_lineage)
    for producer in producers:
        if not str(producer.get("commandId", "")).startswith("__engine__/HERO_BUILD/"):
            continue
        button = container_fields.get("ButtonImage")
        label = container_fields.get("DisplayName")
        tooltip = container_fields.get("DescriptionStrategic")
        if button is None or label is None or tooltip is None:
            raise PlayableUnitCompilerError(
                f"hero {target.name} has unresolved required retail UI values"
            )
        producer["ui"] = {
            "ButtonImage": [str(button["expression"])],
            "TextLabel": [str(label["expression"])],
            "DescriptLabel": [str(tooltip["expression"])],
        }
    simulation = _simulation_contract(
        container_fields,
        member_fields,
        member_lineage,
        members,
        _numeric_defines(documents),
        documents,
        target_lineage,
    )
    combined_kinds = tuple(sorted(set(target_kinds) | set(member_kinds)))
    capabilities, unsupported_capabilities, traits = _capability_contract(
        category,
        combined_kinds,
        bool(members[0]["objectId"] != target.name),
        member_fields,
        visual_refs,
        (),
    )
    used_paths = {
        COMMAND_SET_PATH,
        COMMAND_BUTTON_PATH,
        *(item.source_virtual_path for item in target_lineage),
        *(item.source_virtual_path for item in member_lineage),
    }
    used_paths.update(_provenance_paths(simulation))
    for producer in producers:
        source = producer.get("source", {})
        if isinstance(source, Mapping):
            used_paths.update(str(value) for value in source.values())
        for transition in producer.get("commandSetTransition", []):
            if isinstance(transition, Mapping) and transition.get("sourceIni"):
                used_paths.add(str(transition["sourceIni"]))
    if player_template_id:
        used_paths.add(PLAYER_TEMPLATE_PATH)
    if any(
        str(member.get("countExpression", "1")) != str(member.get("count", 1))
        for member in members
    ):
        used_paths.add("data/ini/gamedata.ini")
    semantic_scopes: dict[str, list[Mapping[str, object]]] = defaultdict(list)
    for item in (*target_lineage, *member_lineage):
        semantic_scopes[item.source_virtual_path.casefold()].append(
            _object_semantic(item)
        )
    for path in _provenance_paths(simulation):
        semantic_scopes[path.casefold()].append(
            {"kind": "ResolvedPlayableUnitSimulation", "contract": simulation}
        )
    for producer in producers:
        producer_id = str(producer["producerObjectId"])
        producer_object = objects.get(producer_id.casefold())
        if producer_object is not None:
            for item in _ancestry(objects, producer_object):
                semantic_scopes[item.source_virtual_path.casefold()].append(
                    _object_semantic(item)
                )
        command_set = command_sets.get(str(producer["commandSetId"]).casefold())
        if command_set is not None:
            semantic_scopes[COMMAND_SET_PATH].append(
                _ini_block_semantic("CommandSet", command_set)
            )
        command_button = command_buttons.get(str(producer["commandId"]).casefold())
        if command_button is not None:
            semantic_scopes[COMMAND_BUTTON_PATH].append(
                _ini_block_semantic("CommandButton", command_button)
            )
    if player_template_id:
        template = _named_blocks(
            _required_document(documents, PLAYER_TEMPLATE_PATH), "PlayerTemplate"
        )[player_template_id.casefold()]
        semantic_scopes[PLAYER_TEMPLATE_PATH].append(
            _ini_block_semantic("PlayerTemplate", template)
        )
    if "data/ini/gamedata.ini" in used_paths:
        semantic_scopes["data/ini/gamedata.ini"].extend(
            {
                "kind": "InitialPayloadCount",
                "expression": str(member.get("countExpression", "1")),
                "resolved": int(member["count"]),
            }
            for member in members
        )
    for values in semantic_scopes.values():
        values.sort(key=lambda row: _canonical_bytes(row))
    descriptor: dict[str, object] = {
        "schema": SCHEMA,
        "schemaVersion": SCHEMA_VERSION,
        "requestedObjectId": requested_target.name,
        "objectId": target.name,
        "category": category,
        "traits": traits,
        "capabilities": capabilities,
        "production": list(producers),
        "composition": {
            "containerObjectId": target.name,
            "members": list(members),
            "primaryMemberObjectId": primary_member.name,
        },
        "kindOf": {
            "container": list(target_kinds),
            "primaryMember": list(member_kinds),
        },
        "gameplay": {
            "containerFields": container_fields,
            "memberFields": member_fields,
            "references": visual_refs,
            "simulation": simulation,
        },
        "presentation": {
            "visualRoots": visual_refs.get("model", []),
            "convertedVisuals": {
                key: dict(value)
                for key, value in sorted(
                    visual_bindings.items(), key=lambda item: item[0].casefold()
                )
            },
            "unresolvedVisualRoots": unresolved_visuals,
            "ui": _ui_binding(
                producers,
                command_buttons,
                target_lineage,
                member_lineage,
                command_audio,
            ),
            "resolvedImages": {
                key: deepcopy(value)
                for key, value in sorted(
                    (resolved_images or {}).items(), key=lambda item: item[0].casefold()
                )
            },
            "resolvedStrings": {
                key: value
                for key, value in sorted(
                    (resolved_strings or {}).items(), key=lambda item: item[0].casefold()
                )
            },
            "audioRoutes": {
                "container": _audio_routes(target_lineage, container_audio_edges),
                "primaryMember": _audio_routes(member_lineage, member_audio_edges),
            },
            "resolvedAudio": {
                key: list(value)
                for key, value in sorted(
                    (resolved_audio or {}).items(), key=lambda item: item[0].casefold()
                )
            },
        },
        "runtimeModules": runtime_modules,
        "runtimeModuleEvidence": module_evidence,
        "specialCapabilities": unsupported_modules,
        "unsupportedCapabilities": [
            {
                "id": "module:%s:%s:%s"
                % (row["ownerRole"], row["kind"], row["instanceTag"]),
                "reason": "authored Behavior is not consumed by the shared runtime adapter",
                "semanticSha256": row["semanticSha256"],
            }
            for row in unsupported_module_evidence
        ],
        "sourceDocuments": _source_rows(documents, used_paths, semantic_scopes),
    }
    descriptor["descriptorSha256"] = _digest(descriptor)
    return descriptor


def validate_playable_unit_descriptor(value: Mapping[str, object]) -> None:
    if value.get("schema") != SCHEMA or value.get("schemaVersion") != SCHEMA_VERSION:
        raise PlayableUnitCompilerError("playable-unit descriptor identity is invalid")
    category = value.get("category")
    if category not in _CATEGORIES:
        raise PlayableUnitCompilerError("playable-unit category is unsupported")
    for field in ("requestedObjectId", "objectId"):
        if not isinstance(value.get(field), str) or not value.get(field):
            raise PlayableUnitCompilerError(f"playable-unit {field} is invalid")
    expected = dict(value)
    digest = expected.pop("descriptorSha256", None)
    if not isinstance(digest, str) or re.fullmatch(r"[0-9a-f]{64}", digest) is None:
        raise PlayableUnitCompilerError(
            "playable-unit descriptor digest format is invalid"
        )
    if digest != _digest(expected):
        raise PlayableUnitCompilerError("playable-unit descriptor digest is invalid")
    traits = value.get("traits")
    if not isinstance(traits, list) or any(
        not isinstance(item, str) for item in traits
    ):
        raise PlayableUnitCompilerError("playable-unit traits are invalid")
    capabilities = value.get("capabilities")
    if not isinstance(capabilities, list):
        raise PlayableUnitCompilerError("playable-unit capabilities are invalid")
    capability_ids: set[str] = set()
    for row in capabilities:
        if (
            not isinstance(row, Mapping)
            or not isinstance(row.get("id"), str)
            or not row.get("id")
            or not isinstance(row.get("evidence"), str)
            or not row.get("evidence")
        ):
            raise PlayableUnitCompilerError("playable-unit capability row is invalid")
        if str(row["id"]) in capability_ids:
            raise PlayableUnitCompilerError(
                "playable-unit capability ids are duplicated"
            )
        capability_ids.add(str(row["id"]))
    production = value.get("production")
    if not isinstance(production, list) or not production:
        raise PlayableUnitCompilerError("playable-unit descriptor has no producer")
    for row in production:
        if not isinstance(row, Mapping):
            raise PlayableUnitCompilerError("playable-unit production row is invalid")
        for field in ("producerObjectId", "commandSetId", "commandId"):
            if not isinstance(row.get(field), str) or not row.get(field):
                raise PlayableUnitCompilerError(
                    f"playable-unit production {field} is invalid"
                )
        surface = row.get("surface")
        if surface not in {"command-socket", "hero-roster"}:
            raise PlayableUnitCompilerError(
                "playable-unit production surface is invalid"
            )
        slot = row.get("slot")
        roster_ordinal = row.get("rosterOrdinal")
        valid_slot = isinstance(slot, int) and not isinstance(slot, bool) and slot > 0
        valid_ordinal = (
            isinstance(roster_ordinal, int)
            and not isinstance(roster_ordinal, bool)
            and roster_ordinal > 0
        )
        if (surface == "command-socket" and (not valid_slot or valid_ordinal)) or (
            surface == "hero-roster" and (not valid_ordinal or valid_slot)
        ):
            raise PlayableUnitCompilerError(
                "playable-unit production route disagrees with its surface"
            )
        prerequisites = row.get("prerequisites")
        if not isinstance(prerequisites, list) or any(
            not isinstance(item, str) or not item for item in prerequisites
        ):
            raise PlayableUnitCompilerError(
                "playable-unit production prerequisites are invalid"
            )
        ui = row.get("ui")
        if not isinstance(ui, Mapping) or any(
            not isinstance(items, list)
            or any(not isinstance(item, str) or not item for item in items)
            for items in ui.values()
        ):
            raise PlayableUnitCompilerError("playable-unit production UI is invalid")
    composition = value.get("composition")
    if not isinstance(composition, Mapping):
        raise PlayableUnitCompilerError("playable-unit descriptor has no composition")
    for field in ("containerObjectId", "primaryMemberObjectId"):
        if not isinstance(composition.get(field), str) or not composition.get(field):
            raise PlayableUnitCompilerError(
                f"playable-unit composition {field} is invalid"
            )
    members = composition.get("members")
    if not isinstance(members, list) or not members:
        raise PlayableUnitCompilerError("playable-unit descriptor has no members")
    for member in members:
        if (
            not isinstance(member, Mapping)
            or not isinstance(member.get("objectId"), str)
            or not member.get("objectId")
            or not isinstance(member.get("count"), int)
            or isinstance(member.get("count"), bool)
            or int(member["count"]) < 1
        ):
            raise PlayableUnitCompilerError("playable-unit member row is invalid")
    kind_of = value.get("kindOf")
    if not isinstance(kind_of, Mapping):
        raise PlayableUnitCompilerError("playable-unit KindOf contract is invalid")
    for field in ("container", "primaryMember"):
        kinds = kind_of.get(field)
        if not isinstance(kinds, list) or any(
            not isinstance(item, str) or not item for item in kinds
        ):
            raise PlayableUnitCompilerError(
                f"playable-unit KindOf {field} values are invalid"
            )
    expected_category = _category(
        kind_of["container"],
        kind_of["primaryMember"],
        composition["containerObjectId"] != composition["primaryMemberObjectId"],
    )
    if category != expected_category:
        raise PlayableUnitCompilerError("playable-unit category disagrees with KindOf")
    expected_traits: set[str] = set()
    combined_kinds = set(kind_of["container"]) | set(kind_of["primaryMember"])
    if combined_kinds & {"ARCHER", "RANGED"}:
        expected_traits.add("ranged")
    if "CAVALRY" in combined_kinds:
        expected_traits.add("mounted")
    if combined_kinds & {"AIRCRAFT", "FLYING"}:
        expected_traits.add("flying")
    if "TRANSPORT" in combined_kinds:
        expected_traits.add("transport")
    if set(traits) != expected_traits:
        raise PlayableUnitCompilerError("playable-unit traits disagree with KindOf")
    gameplay = value.get("gameplay")
    if not isinstance(gameplay, Mapping):
        raise PlayableUnitCompilerError("playable-unit gameplay contract is invalid")
    for field in ("containerFields", "memberFields", "references"):
        if not isinstance(gameplay.get(field), Mapping):
            raise PlayableUnitCompilerError(
                f"playable-unit gameplay {field} is invalid"
            )
    for field in ("containerFields", "memberFields"):
        for name, row in gameplay[field].items():
            if (
                not isinstance(name, str)
                or not name
                or not isinstance(row, Mapping)
                or not isinstance(row.get("expression"), str)
                or not isinstance(row.get("sourceIni"), str)
                or not isinstance(row.get("line"), int)
            ):
                raise PlayableUnitCompilerError(
                    f"playable-unit gameplay {field} row is invalid"
                )
    for name, rows in gameplay["references"].items():
        if not isinstance(name, str) or not name or not isinstance(rows, list):
            raise PlayableUnitCompilerError(
                "playable-unit gameplay reference collection is invalid"
            )
        for row in rows:
            if (
                not isinstance(row, Mapping)
                or not isinstance(row.get("id"), str)
                or not row.get("id")
                or not isinstance(row.get("expression"), str)
                or not isinstance(row.get("sourceIni"), str)
                or not isinstance(row.get("line"), int)
            ):
                raise PlayableUnitCompilerError(
                    "playable-unit gameplay reference row is invalid"
                )
    presentation = value.get("presentation")
    if not isinstance(presentation, Mapping):
        raise PlayableUnitCompilerError("playable-unit presentation is invalid")
    ui = presentation.get("ui")
    audio_routes = presentation.get("audioRoutes")
    if not isinstance(ui, Mapping) or not isinstance(audio_routes, Mapping):
        raise PlayableUnitCompilerError(
            "playable-unit UI/audio presentation is invalid"
        )
    commands = ui.get("commands")
    portraits = ui.get("portraitImageIds")
    if (
        not isinstance(commands, list)
        or not isinstance(portraits, list)
        or any(not isinstance(item, str) or not item for item in portraits)
    ):
        raise PlayableUnitCompilerError("playable-unit UI bindings are invalid")
    production_command_ids = [str(row["commandId"]) for row in production]
    ui_command_ids: list[str] = []
    for command in commands:
        if (
            not isinstance(command, Mapping)
            or not isinstance(command.get("commandId"), str)
            or not command.get("commandId")
            or not isinstance(command.get("fields"), Mapping)
        ):
            raise PlayableUnitCompilerError("playable-unit UI command row is invalid")
        ui_command_ids.append(str(command["commandId"]))
        command_routes = command.get("audioRoutes")
        if not isinstance(command_routes, list) or any(
            not isinstance(route, Mapping)
            or not isinstance(route.get("field"), str)
            or not route.get("field")
            or not isinstance(route.get("id"), str)
            or not route.get("id")
            or not isinstance(route.get("tokenOrdinal"), int)
            or isinstance(route.get("tokenOrdinal"), bool)
            or int(route["tokenOrdinal"]) < 0
            or route.get("resolution") not in {"resolved", "unresolved"}
            or route.get("sourceIni") != COMMAND_BUTTON_PATH
            for route in command_routes
        ):
            raise PlayableUnitCompilerError(
                "playable-unit UI command audio routes are invalid"
            )
    if ui_command_ids != production_command_ids:
        raise PlayableUnitCompilerError(
            "playable-unit UI commands disagree with production routes"
        )
    if not portraits and not any(command.get("fields") for command in commands):
        raise PlayableUnitCompilerError(
            "playable-unit UI has no authored image/text binding"
        )
    for owner in ("container", "primaryMember"):
        routes = audio_routes.get(owner)
        if not isinstance(routes, Mapping):
            raise PlayableUnitCompilerError(
                f"playable-unit {owner} audio routes are invalid"
            )
        for rows in routes.values():
            if not isinstance(rows, list) or any(
                not isinstance(row, Mapping)
                or not isinstance(row.get("id"), str)
                or not row.get("id")
                or not isinstance(row.get("sourceIni"), str)
                or not isinstance(row.get("line"), int)
                for row in rows
            ):
                raise PlayableUnitCompilerError(
                    "playable-unit audio route row is invalid"
                )
    for field, expected_type in (
        ("visualRoots", list),
        ("convertedVisuals", Mapping),
        ("unresolvedVisualRoots", list),
        ("resolvedImages", Mapping),
        ("resolvedStrings", Mapping),
        ("resolvedAudio", Mapping),
    ):
        if not isinstance(presentation.get(field), expected_type):
            raise PlayableUnitCompilerError(
                f"playable-unit presentation {field} is invalid"
            )
    for row in presentation["visualRoots"]:
        if (
            not isinstance(row, Mapping)
            or not isinstance(row.get("id"), str)
            or not isinstance(row.get("expression"), str)
            or not isinstance(row.get("sourceIni"), str)
            or not isinstance(row.get("line"), int)
        ):
            raise PlayableUnitCompilerError("playable-unit visual-root row is invalid")
    for key, row in presentation["convertedVisuals"].items():
        if not isinstance(key, str) or not key or not isinstance(row, Mapping):
            raise PlayableUnitCompilerError(
                "playable-unit converted visual row is invalid"
            )
    if any(
        not isinstance(item, str) or not item
        for item in presentation["unresolvedVisualRoots"]
    ):
        raise PlayableUnitCompilerError(
            "playable-unit unresolved visual roots are invalid"
        )
    for key, image in presentation["resolvedImages"].items():
        if not isinstance(key, str) or not key or not isinstance(image, Mapping):
            raise PlayableUnitCompilerError("playable-unit resolved images are invalid")
        coords = image.get("coords")
        width = image.get("textureWidth")
        height = image.get("textureHeight")
        if (
            not isinstance(image.get("id"), str)
            or str(image["id"]).casefold() != key.casefold()
            or not isinstance(image.get("texture"), str)
            or not image.get("texture")
            or not isinstance(image.get("compiledTextureVirtualPath"), str)
            or not image.get("compiledTextureVirtualPath")
            or not isinstance(width, int)
            or isinstance(width, bool)
            or not isinstance(height, int)
            or isinstance(height, bool)
            or width <= 0
            or height <= 0
            or not isinstance(coords, Mapping)
        ):
            raise PlayableUnitCompilerError("playable-unit mapped image is invalid")
        values = [coords.get(name) for name in ("left", "top", "right", "bottom")]
        if (
            any(
                not isinstance(value, int) or isinstance(value, bool)
                for value in values
            )
            or values[0] < 0
            or values[1] < 0
            or values[2] <= values[0]
            or values[3] <= values[1]
            or values[2] > width
            or values[3] > height
        ):
            raise PlayableUnitCompilerError(
                "playable-unit mapped image crop is invalid"
            )
    for key, paths in presentation["resolvedAudio"].items():
        if (
            not isinstance(key, str)
            or not key
            or not isinstance(paths, list)
            or any(not isinstance(path, str) or not path for path in paths)
        ):
            raise PlayableUnitCompilerError("playable-unit resolved audio is invalid")
    for key, text in presentation["resolvedStrings"].items():
        if (
            not isinstance(key, str)
            or not key
            or not isinstance(text, str)
            or not text
        ):
            raise PlayableUnitCompilerError("playable-unit resolved strings are invalid")
    runtime_modules = value.get("runtimeModules")
    module_evidence = value.get("runtimeModuleEvidence")
    special = value.get("specialCapabilities")
    if (
        not isinstance(runtime_modules, list)
        or not isinstance(module_evidence, list)
        or not isinstance(special, list)
        or any(
            not isinstance(item, str) or not item for item in runtime_modules + special
        )
    ):
        raise PlayableUnitCompilerError(
            "playable-unit runtime module lists are invalid"
        )
    for row in module_evidence:
        if (
            not isinstance(row, Mapping)
            or row.get("ownerRole") not in {"container", "primaryMember"}
            or not isinstance(row.get("kind"), str)
            or not row.get("kind")
            or not isinstance(row.get("instanceTag"), str)
            or not isinstance(row.get("sourceIni"), str)
            or not isinstance(row.get("line"), int)
            or not isinstance(row.get("consumed"), bool)
            or re.fullmatch(r"[0-9a-f]{64}", str(row.get("semanticSha256", ""))) is None
        ):
            raise PlayableUnitCompilerError(
                "playable-unit runtime module evidence row is invalid"
            )
    expected_runtime_modules = sorted(
        {str(row["kind"]) for row in module_evidence}, key=str.casefold
    )
    if runtime_modules != expected_runtime_modules:
        raise PlayableUnitCompilerError(
            "playable-unit runtime modules disagree with module evidence"
        )
    unsupported = value.get("unsupportedCapabilities")
    if not isinstance(unsupported, list) or any(
        not isinstance(row, Mapping)
        or not isinstance(row.get("id"), str)
        or not isinstance(row.get("reason"), str)
        or re.fullmatch(r"[0-9a-f]{64}", str(row.get("semanticSha256", ""))) is None
        for row in unsupported
    ):
        raise PlayableUnitCompilerError(
            "playable-unit unsupported-capability rows are invalid"
        )
    unsupported_modules = {
        str(row["kind"]) for row in module_evidence if row["consumed"] is False
    }
    expected_unsupported = [
        {
            "id": "module:%s:%s:%s"
            % (row["ownerRole"], row["kind"], row["instanceTag"]),
            "reason": "authored Behavior is not consumed by the shared runtime adapter",
            "semanticSha256": row["semanticSha256"],
        }
        for row in module_evidence
        if row["consumed"] is False
    ]
    if unsupported_modules != set(special) or unsupported != expected_unsupported:
        raise PlayableUnitCompilerError(
            "playable-unit unsupported modules disagree with special capabilities"
        )
    sources = value.get("sourceDocuments")
    if not isinstance(sources, list) or not sources:
        raise PlayableUnitCompilerError("playable-unit source provenance is invalid")
    source_paths: set[str] = set()
    for source in sources:
        if (
            not isinstance(source, Mapping)
            or not isinstance(source.get("virtualPath"), str)
            or re.fullmatch(r"[0-9a-f]{64}", str(source.get("semanticSha256", "")))
            is None
        ):
            raise PlayableUnitCompilerError("playable-unit source row is invalid")
        path = str(source["virtualPath"])
        if path.casefold() in source_paths:
            raise PlayableUnitCompilerError("playable-unit source paths are duplicated")
        source_paths.add(path.casefold())


__all__ = [
    "COMMAND_BUTTON_PATH",
    "COMMAND_SET_PATH",
    "PlayableUnitCompilerError",
    "SCHEMA",
    "SCHEMA_VERSION",
    "compile_playable_unit_descriptor",
    "validate_playable_unit_descriptor",
]
