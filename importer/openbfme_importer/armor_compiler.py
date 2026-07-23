"""Compile BFME2 armor.ini Armor tables and object ArmorSet/upgrade contracts.

Retail 1.06 authors per-damage-type scalar tables in ``data/ini/armor.ini``
(``Armor <SetId>`` blocks with ``Armor = <TYPE> <percent>`` rows, an optional
``DamageScalar`` global incoming multiplier, and an optional ``FlankedPenalty``
evidence row).  Objects bind a set through their ``ArmorSet`` module blocks:
the default-conditions block is the base set, while upgrade-gated blocks
(``Conditions = PLAYER_UPGRADE``) pair with ``ArmorUpgrade`` behaviors via
``ArmorSetFlag``.  ``WeaponSetUpgrade`` behaviors swap either the weapon
(forged blades) or an upgrade-gated projectile warhead (fire arrows).

Every emitted number carries its INI line of provenance; anything referenced
but unresolvable fails closed instead of inventing a value.
"""

from __future__ import annotations

from collections import defaultdict
from collections.abc import Mapping, Sequence
import re
import threading

from .playable_unit_compiler import (
    _base_weapon_damage,
    _default_set_block,
    _digest,
    _effective_top_blocks,
    _named_definition_values,
    _resolved_definition_field,
    _resolved_multiplicative_expression,
    _tokens,
    _weapon_damage_nuggets,
)
from .sage_cst import SageObject


ARMOR_INI_PATH = "data/ini/armor.ini"

# Damage.h damage-type vocabulary (mirrored in the armor.ini header comment)
# plus the DEFAULT fallback row.  Anything outside this set in an Armor row
# means the parser drifted from the retail grammar, so it fails closed.
_ARMOR_ROW_TYPES = frozenset(
    {
        "DEFAULT",
        "FORCE",
        "CRUSH",
        "SLASH",
        "PIERCE",
        "SIEGE",
        "STRUCTURAL",
        "FLAME",
        "HEALING",
        "UNRESISTABLE",
        "WATER",
        "PENALTY",
        "FALLING",
        "TOPPLING",
        "REFLECTED",
        "PASSENGER",
        "MAGIC",
        "CHOP",
        "HERO",
        "SPECIALIST",
        "URUK",
        "HERO_RANGED",
        "FLY_INTO",
        "UNDEFINED",
        "LOGICAL_FIRE",
        "CAVALRY",
        "CAVALRY_RANGED",
        "POISON",
    }
)


class ArmorCompilerError(ValueError):
    """A referenced armor set or upgrade effect cannot be resolved."""


def _percent(token: str, *, context: str) -> float:
    text = token.strip()
    if not text.endswith("%"):
        raise ArmorCompilerError(f"{context} is not a percentage: {token!r}")
    try:
        value = float(text[:-1])
    except ValueError as exc:
        raise ArmorCompilerError(f"{context} is not a percentage: {token!r}") from exc
    if value < 0.0:
        raise ArmorCompilerError(f"{context} is a negative percentage: {token!r}")
    return value


def _row_provenance(row: Mapping[str, object]) -> dict[str, object]:
    return {
        "sourceIni": str(row.get("sourceIni", "")),
        "line": int(row.get("line", 0)),
    }


def compile_armor_table(
    documents: Mapping[str, bytes],
    set_id: str,
    *,
    named_definition_cache: dict | None = None,
    cache_lock: "threading.Lock | None" = None,
) -> dict[str, object]:
    """Resolve one armor.ini Armor definition into a scalar table or fail."""

    rows = _named_definition_values(
        documents,
        "Armor",
        set_id,
        cache=named_definition_cache,
        cache_lock=cache_lock,
    )
    if rows is None:
        raise ArmorCompilerError(
            f"armor set '{set_id}' has no unique authored definition in "
            f"{ARMOR_INI_PATH}"
        )
    scalars: dict[str, dict[str, object]] = {}
    default_row: dict[str, object] | None = None
    for row in rows.get("armor", ()):
        # armor.ini authors both `;` and `//` comments (FortressExpansionArmor
        # line 1868); _named_definition_values strips only `;`.
        parts = str(row.get("expression", "")).split("//", 1)[0].split()
        if len(parts) != 2:
            raise ArmorCompilerError(
                f"armor set '{set_id}' has a malformed Armor row at "
                f"{row.get('sourceIni')}:{row.get('line')}"
            )
        damage_type = parts[0].upper()
        if damage_type not in _ARMOR_ROW_TYPES:
            raise ArmorCompilerError(
                f"armor set '{set_id}' references unknown damage type "
                f"'{parts[0]}' at {row.get('sourceIni')}:{row.get('line')}"
            )
        percent = _percent(
            parts[1], context=f"armor set '{set_id}' row {damage_type}"
        )
        key = damage_type.casefold()
        entry = {
            "percent": percent,
            "damageType": damage_type,
            **_row_provenance(row),
        }
        if key == "default":
            if default_row is not None and default_row["percent"] != percent:
                raise ArmorCompilerError(
                    f"armor set '{set_id}' has conflicting DEFAULT rows"
                )
            default_row = entry
            continue
        existing = scalars.get(key)
        if existing is not None:
            if existing["percent"] != percent:
                raise ArmorCompilerError(
                    f"armor set '{set_id}' has conflicting {damage_type} rows"
                )
            continue
        scalars[key] = entry
    if default_row is None:
        raise ArmorCompilerError(
            f"armor set '{set_id}' has no DEFAULT row in {ARMOR_INI_PATH}"
        )
    damage_scalar_rows = rows.get("damagescalar", ())
    damage_scalar: dict[str, object] = {
        "percent": 100.0,
        "semantic": "DamageScalar is not authored; the SAGE default is 100%",
    }
    if damage_scalar_rows:
        unique = {
            _percent(
                str(row.get("expression", "")).split()[0],
                context=f"armor set '{set_id}' DamageScalar",
            ): row
            for row in damage_scalar_rows
        }
        if len(unique) != 1:
            raise ArmorCompilerError(
                f"armor set '{set_id}' has conflicting DamageScalar rows"
            )
        percent, row = next(iter(unique.items()))
        damage_scalar = {"percent": percent, **_row_provenance(row)}
    result: dict[str, object] = {
        "setId": set_id,
        "default": default_row,
        "scalars": {key: scalars[key] for key in sorted(scalars)},
        "damageScalar": damage_scalar,
        "sourceIni": ARMOR_INI_PATH,
    }
    flanked_rows = rows.get("flankedpenalty", ())
    if flanked_rows:
        unique = {
            _percent(
                str(row.get("expression", "")).split()[0],
                context=f"armor set '{set_id}' FlankedPenalty",
            ): row
            for row in flanked_rows
        }
        if len(unique) != 1:
            raise ArmorCompilerError(
                f"armor set '{set_id}' has conflicting FlankedPenalty rows"
            )
        percent, row = next(iter(unique.items()))
        result["flankedPenalty"] = {
            "percent": percent,
            "semantic": "evidence only; the slice has no flanking model",
            **_row_provenance(row),
        }
    return result


def _armor_set_blocks(lineage: Sequence[SageObject]):
    base = _default_set_block(lineage, "ArmorSet")
    upgraded: list = []
    for block in _effective_top_blocks(lineage):
        if (block.header_key or block.kind).casefold() != "armorset":
            continue
        conditions = [
            row.value.strip()
            for row in block.assignments
            if row.key.casefold() in {"condition", "conditions"}
        ]
        positive_tokens = {
            token.casefold()
            for condition in conditions
            for token in _tokens(condition.casefold())
            if not token.startswith("-") and token not in {"none", "set_normal"}
        }
        if positive_tokens:
            upgraded.append((block, positive_tokens))
    return base, upgraded


def _armor_row(block) -> object:
    rows = [row for row in block.assignments if row.key.casefold() == "armor"]
    if len(rows) != 1:
        return None
    tokens = _tokens(rows[0].value)
    return (rows[0], tokens[-1]) if tokens else None


def _behavior_blocks(lineage: Sequence[SageObject], module_kind: str):
    return [
        block
        for block in _effective_top_blocks(lineage)
        if (block.header_key or "").casefold() == "behavior"
        and block.kind.casefold() == module_kind.casefold()
    ]


def _behavior_assignment(block, key: str) -> object:
    rows = [row for row in block.assignments if row.key.casefold() == key.casefold()]
    return rows[0] if len(rows) == 1 else None


def compile_armor_contract(
    documents: Mapping[str, bytes],
    *lineages: Sequence[SageObject],
    named_definition_cache: dict | None = None,
    cache_lock: "threading.Lock | None" = None,
) -> dict[str, object]:
    """Compile the base armor set and ArmorUpgrade-gated sets for an object.

    The first lineage with an authored base ArmorSet wins (members before
    containers, mirroring how SAGE bodies bind the unit object's set).  When
    no lineage authors an ArmorSet the contract records the SAGE engine
    passthrough explicitly instead of inventing a set.
    """

    base = None
    base_lineage: Sequence[SageObject] = ()
    for lineage in lineages:
        candidate, _ = _armor_set_blocks(lineage)
        if candidate is not None:
            base = candidate
            base_lineage = lineage
            break
    if base is None:
        return {
            "setId": None,
            "semantic": (
                "no authored ArmorSet on the object ancestry; the SAGE engine "
                "applies unmodified damage (100% of every damage type)"
            ),
            "upgrades": [],
            "excludedUpgradeSets": [],
        }
    resolved_base = _armor_row(base)
    if resolved_base is None:
        raise ArmorCompilerError(
            f"base ArmorSet at {base.source_virtual_path}:{base.line} has no "
            "unique Armor row"
        )
    base_row, base_set_id = resolved_base
    contract: dict[str, object] = {
        "setId": base_set_id,
        "table": compile_armor_table(
            documents,
            base_set_id,
            named_definition_cache=named_definition_cache,
            cache_lock=cache_lock,
        ),
        **_row_provenance(
            {
                "sourceIni": base_row.source_virtual_path,
                "line": base_row.line,
            }
        ),
        "upgrades": [],
        "excludedUpgradeSets": [],
    }
    _, upgraded_sets = _armor_set_blocks(base_lineage)
    used_sets: set[int] = set()
    upgrades: list[dict[str, object]] = []
    for lineage in lineages:
        for behavior in _behavior_blocks(lineage, "ArmorUpgrade"):
            trigger = _behavior_assignment(behavior, "TriggeredBy")
            if trigger is None or not _tokens(trigger.value):
                raise ArmorCompilerError(
                    f"ArmorUpgrade at {behavior.source_virtual_path}:"
                    f"{behavior.line} has no unique TriggeredBy"
                )
            upgrade_id = _tokens(trigger.value)[-1]
            flag_row = _behavior_assignment(behavior, "ArmorSetFlag")
            flag = (
                _tokens(flag_row.value)[-1]
                if flag_row is not None and _tokens(flag_row.value)
                else "PLAYER_UPGRADE"
            )
            flag_semantic = (
                None
                if flag_row is not None
                else "ArmorSetFlag is not authored; the SAGE ArmorUpgrade "
                "default is PLAYER_UPGRADE"
            )
            match = None
            for index, (block, positive_tokens) in enumerate(upgraded_sets):
                if flag.casefold() in positive_tokens:
                    match = (index, block)
                    break
            if match is None:
                raise ArmorCompilerError(
                    f"ArmorUpgrade '{upgrade_id}' at "
                    f"{behavior.source_virtual_path}:{behavior.line} has no "
                    f"ArmorSet gated by '{flag}'"
                )
            index, block = match
            used_sets.add(index)
            resolved = _armor_row(block)
            if resolved is None:
                raise ArmorCompilerError(
                    f"upgraded ArmorSet at {block.source_virtual_path}:"
                    f"{block.line} has no unique Armor row"
                )
            row, set_id = resolved
            entry: dict[str, object] = {
                "upgradeId": upgrade_id,
                "armorSetFlag": flag,
                "setId": set_id,
                "table": compile_armor_table(
                    documents,
                    set_id,
                    named_definition_cache=named_definition_cache,
                    cache_lock=cache_lock,
                ),
                **_row_provenance(
                    {"sourceIni": row.source_virtual_path, "line": row.line}
                ),
                "behavior": {
                    "kind": "ArmorUpgrade",
                    "sourceIni": behavior.source_virtual_path,
                    "line": behavior.line,
                },
            }
            if flag_semantic is not None:
                entry["armorSetFlagSemantic"] = flag_semantic
            upgrades.append(entry)
    excluded = [
        {
            "setId": (
                resolved[1]
                if (resolved := _armor_row(block)) is not None
                else None
            ),
            "reason": "upgrade-gated ArmorSet has no matching ArmorUpgrade behavior",
            "conditions": sorted(positive_tokens),
            "sourceIni": block.source_virtual_path,
            "line": block.line,
        }
        for index, (block, positive_tokens) in enumerate(upgraded_sets)
        if index not in used_sets
    ]
    contract["upgrades"] = sorted(
        upgrades, key=lambda row: str(row["upgradeId"]).casefold()
    )
    contract["excludedUpgradeSets"] = sorted(
        excluded, key=lambda row: str(row.get("setId") or "").casefold()
    )
    return contract


def _weapon_projectile_nuggets(
    documents: Mapping[str, bytes],
    identifier: str,
) -> list[dict[str, object]] | None:
    """Authored ProjectileNugget sub-blocks of one named Weapon definition."""

    header = re.compile(rf"^Weapon\s+{re.escape(identifier)}\s*$", re.IGNORECASE)
    matches: list[list[dict[str, object]]] = []
    for path, payload in sorted(documents.items(), key=lambda item: item[0].casefold()):
        try:
            lines = payload.decode("cp1252").splitlines()
        except UnicodeDecodeError:
            continue
        active = False
        depth = 0
        nuggets: list[dict[str, object]] = []
        current: dict[str, object] | None = None
        for line_number, raw in enumerate(lines, start=1):
            stripped = raw.strip()
            clean = stripped.split(";", 1)[0].split("//", 1)[0].strip()
            if not active:
                if raw.lstrip() == raw and header.fullmatch(clean):
                    active = True
                continue
            if depth == 0 and raw.lstrip() == raw and clean.casefold() == "end":
                matches.append(nuggets)
                active = False
                break
            if not clean:
                continue
            if re.fullmatch(r"[A-Za-z]+Nugget", clean):
                depth += 1
                current = (
                    {"fields": defaultdict(list), "line": line_number}
                    if depth == 1 and clean.casefold() == "projectilenugget"
                    else None
                )
                if current is not None:
                    nuggets.append(current)
                continue
            if clean.casefold() == "end" and depth:
                depth -= 1
                current = None
                continue
            if depth == 1 and current is not None and "=" in clean:
                key, expression = (part.strip() for part in clean.split("=", 1))
                if key and expression:
                    fields = current["fields"]
                    fields[key.casefold()].append(
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


def _damage_scalars(fields: Mapping[str, object]) -> list[dict[str, object]]:
    scalars: list[dict[str, object]] = []
    for row in fields.get("damagescalar", ()):
        parts = str(row.get("expression", "")).split(None, 1)
        if not parts:
            continue
        percent = _percent(parts[0], context="DamageScalar")
        scalars.append(
            {
                "percent": percent,
                "filter": parts[1].strip() if len(parts) > 1 else "",
                **_row_provenance(row),
            }
        )
    return scalars


def _nugget_contract(
    nugget: Mapping[str, object],
    constants: Mapping[str, int | float],
) -> dict[str, object]:
    fields = nugget["fields"]
    damage = _resolved_definition_field(fields, "Damage", constants)
    if damage is None:
        damage = _resolved_definition_field(
            fields,
            "Damage",
            constants,
            resolve=_resolved_multiplicative_expression,
        )
    if damage is None:
        raise ArmorCompilerError(
            f"DamageNugget at line {nugget.get('line')} has unresolvable Damage"
        )
    damage_types = {
        str(row.get("expression", "")).casefold(): str(row.get("expression", ""))
        for row in fields.get("damagetype", ())
        if str(row.get("expression", ""))
    }
    contract: dict[str, object] = {
        "damage": damage,
        "line": int(nugget.get("line", 0)),
    }
    if len(damage_types) == 1:
        contract["damageType"] = next(iter(damage_types.values()))
    scalars = _damage_scalars(fields)
    if scalars:
        contract["damageScalars"] = scalars
    for gate_key in ("requiredupgradenames", "forbiddenupgradenames"):
        rows = fields.get(gate_key, ())
        if rows:
            contract[gate_key] = [
                {
                    "expression": str(row.get("expression", "")),
                    **_row_provenance(row),
                }
                for row in rows
            ]
    return contract


def _weapon_damage_contract(
    documents: Mapping[str, bytes],
    weapon_id: str,
    constants: Mapping[str, int | float],
    *,
    named_definition_cache: dict | None = None,
    cache_lock: "threading.Lock | None" = None,
) -> dict[str, object]:
    """Resolve one weapon's authored damage/type/nugget scalars or fail."""

    weapon = _named_definition_values(
        documents,
        "Weapon",
        weapon_id,
        cache=named_definition_cache,
        cache_lock=cache_lock,
    )
    if weapon is None:
        raise ArmorCompilerError(
            f"weapon '{weapon_id}' has no unique authored definition"
        )
    damage = _resolved_definition_field(weapon, "Damage", constants)
    damage_type = None
    if damage is None:
        nugget_total = _base_weapon_damage(
            documents,
            weapon_id,
            constants,
            cache=named_definition_cache,
            cache_lock=cache_lock,
        )
        if nugget_total is None:
            # Projectile weapons carry their damage on the warhead (retail
            # trebuchet rocks); exactly one un-gated warhead must carry
            # resolvable damage (shroud-revealer dummies carry none).
            warhead_hits: list[tuple[str, dict[str, object], str | None]] = []
            for warhead_id in _base_warhead_targets(documents, weapon_id):
                warhead = _named_definition_values(
                    documents,
                    "Weapon",
                    warhead_id,
                    cache=named_definition_cache,
                    cache_lock=cache_lock,
                )
                warhead_damage = _resolved_definition_field(
                    warhead, "Damage", constants
                )
                if warhead_damage is None:
                    warhead_damage = _base_weapon_damage(
                        documents,
                        warhead_id,
                        constants,
                        cache=named_definition_cache,
                        cache_lock=cache_lock,
                    )
                if warhead_damage is None:
                    continue
                warhead_types = {
                    str(row.get("expression", "")).casefold(): str(
                        row.get("expression", "")
                    )
                    for row in (warhead or {}).get("damagetype", ())
                    if str(row.get("expression", ""))
                }
                if not warhead_types:
                    warhead_types = {
                        str(component.get("damageType", "")).casefold(): str(
                            component.get("damageType", "")
                        )
                        for component in warhead_damage.get("components", ())
                        if str(component.get("damageType", ""))
                    }
                warhead_hits.append(
                    (
                        warhead_id,
                        warhead_damage,
                        next(iter(warhead_types.values()))
                        if len(warhead_types) == 1
                        else None,
                    )
                )
            if len(warhead_hits) == 1:
                _, damage, damage_type = warhead_hits[0]
        else:
            damage = nugget_total
            types = {
                str(component.get("damageType", "")).casefold(): str(
                    component.get("damageType", "")
                )
                for component in nugget_total.get("components", ())
                if str(component.get("damageType", ""))
            }
            if len(types) == 1:
                damage_type = next(iter(types.values()))
    else:
        types = {
            str(row.get("expression", "")).casefold(): str(row.get("expression", ""))
            for row in weapon.get("damagetype", ())
            if str(row.get("expression", ""))
        }
        if len(types) == 1:
            damage_type = next(iter(types.values()))
    if damage is None:
        raise ArmorCompilerError(
            f"weapon '{weapon_id}' has no resolvable authored damage"
        )
    contract: dict[str, object] = {
        "weaponId": weapon_id,
        "damage": damage,
    }
    if damage_type is not None:
        contract["damageType"] = damage_type
    nuggets = _weapon_damage_nuggets(
        documents, weapon_id, cache=named_definition_cache, cache_lock=cache_lock
    )
    scalars: list[dict[str, object]] = []
    for nugget in nuggets or ():
        fields = nugget["fields"]
        if fields.get("requiredupgradenames") or fields.get("forbiddenupgradenames"):
            continue
        scalars.extend(_damage_scalars(fields))
    if scalars:
        contract["damageScalars"] = scalars
    return contract


def _base_warhead_targets(
    documents: Mapping[str, bytes], weapon_id: str
) -> tuple[str, ...]:
    """The warheads of a projectile weapon's un-gated ProjectileNuggets."""

    warheads: dict[str, str] = {}
    for nugget in _weapon_projectile_nuggets(documents, weapon_id) or ():
        fields = nugget["fields"]
        if fields.get("requiredupgradenames"):
            continue
        rows = fields.get("warheadtemplatename", ())
        if not rows:
            continue
        tokens = _tokens(str(rows[0].get("expression", "")))
        if tokens:
            warheads[tokens[-1].casefold()] = tokens[-1]
    return tuple(warheads[key] for key in sorted(warheads))


def _player_upgrade_weapon_target(
    lineage: Sequence[SageObject],
) -> tuple[str | None, object | None]:
    """The PRIMARY weapon of the PLAYER_UPGRADE-gated WeaponSet block.

    Units with a second equipment axis author several upgrade sets (the
    goblin fighter's poison toggle plus forged blades); the plain upgrade set
    — the one carrying no additional positive weaponset condition — wins,
    while the combinations stay recorded alternates.
    """

    candidates: list[tuple[int, str, object]] = []
    for block in _effective_top_blocks(lineage):
        if (block.header_key or block.kind).casefold() != "weaponset":
            continue
        conditions = [
            row.value.strip().casefold()
            for row in block.assignments
            if row.key.casefold() in {"condition", "conditions"}
        ]
        positive = {
            token
            for condition in conditions
            for token in _tokens(condition)
            if not token.startswith("-")
        }
        if "player_upgrade" not in positive:
            continue
        extra = len(positive - {"player_upgrade"})
        for assignment in block.assignments:
            if assignment.key.casefold() != "weapon":
                continue
            tokens = _tokens(assignment.value)
            if tokens and any(token.casefold() == "primary" for token in tokens[:-1]):
                candidates.append((extra, tokens[-1], assignment))
    if not candidates:
        return None, None
    best_extra = min(extra for extra, _, _ in candidates)
    best = [candidate for candidate in candidates if candidate[0] == best_extra]
    targets = {target.casefold(): (target, row) for _, target, row in best}
    if len(targets) != 1:
        return None, None
    target, row = next(iter(targets.values()))
    return target, row


def base_weapon_targets(lineage: Sequence[SageObject]) -> tuple[str, ...]:
    """Every weapon id the lineage's non-upgrade WeaponSet blocks can wield."""

    weapons: dict[str, str] = {}
    for block in _effective_top_blocks(lineage):
        if (block.header_key or block.kind).casefold() != "weaponset":
            continue
        conditions = [
            row.value.strip().casefold()
            for row in block.assignments
            if row.key.casefold() in {"condition", "conditions"}
        ]
        positive = {
            token
            for condition in conditions
            for token in _tokens(condition)
            if not token.startswith("-")
        }
        if "player_upgrade" in positive:
            continue
        for assignment in block.assignments:
            if assignment.key.casefold() != "weapon":
                continue
            tokens = _tokens(assignment.value)
            if tokens:
                weapons.setdefault(tokens[-1].casefold(), tokens[-1])
    return tuple(weapons[key] for key in sorted(weapons))


def compile_weapon_upgrades(
    documents: Mapping[str, bytes],
    lineages: Sequence[Sequence[SageObject]],
    base_weapon_ids: Sequence[str],
    constants: Mapping[str, int | float],
    *,
    named_definition_cache: dict | None = None,
    cache_lock: "threading.Lock | None" = None,
) -> list[dict[str, object]]:
    """Compile each WeaponSetUpgrade behavior's authored damage effect.

    Three retail patterns resolve: a weapon swap (the PLAYER_UPGRADE WeaponSet
    names a different weapon, e.g. Gondor forged blades), an upgrade-gated
    projectile warhead on the unchanged weapon (e.g. fire arrows), and
    upgrade-gated DamageNuggets on the unchanged weapon or its warhead (e.g.
    tower-guard forged blades, whose swapped WeaponSet is commented out in the
    retail source).  ``base_weapon_ids`` carries every weapon the unit's
    non-upgrade sets can wield, combat (primary) weapon first; the combat
    weapon's effect drives when it resolves, otherwise exactly one alternate
    may resolve.  Anything else fails closed rather than guessing.
    """

    upgrades: list[dict[str, object]] = []
    by_upgrade_id: dict[str, dict[str, object]] = {}
    for lineage in lineages:
        for behavior in _behavior_blocks(lineage, "WeaponSetUpgrade"):
            trigger = _behavior_assignment(behavior, "TriggeredBy")
            if trigger is None or not _tokens(trigger.value):
                raise ArmorCompilerError(
                    f"WeaponSetUpgrade at {behavior.source_virtual_path}:"
                    f"{behavior.line} has no unique TriggeredBy"
                )
            upgrade_id = _tokens(trigger.value)[-1]
            behavior_row = {
                "kind": "WeaponSetUpgrade",
                "sourceIni": behavior.source_virtual_path,
                "line": behavior.line,
            }
            if upgrade_id.casefold() in by_upgrade_id:
                # Retail authors several WeaponSetUpgrade behaviors for one
                # upgrade (animation flags, legality); the damage effect is
                # shared, so the duplicate is recorded, not recompiled.
                existing = by_upgrade_id[upgrade_id.casefold()]
                existing.setdefault("additionalBehaviors", []).append(behavior_row)
                continue
            if not base_weapon_ids:
                raise ArmorCompilerError(
                    f"WeaponSetUpgrade '{upgrade_id}' has no base weapon to "
                    "compare against"
                )
            upgraded_weapon_id, weapon_row = _player_upgrade_weapon_target(lineage)
            entry: dict[str, object] = {
                "upgradeId": upgrade_id,
                "behavior": behavior_row,
            }
            if upgraded_weapon_id is not None and all(
                upgraded_weapon_id.casefold() != candidate.casefold()
                for candidate in base_weapon_ids
            ):
                contract = _weapon_damage_contract(
                    documents,
                    upgraded_weapon_id,
                    constants,
                    named_definition_cache=named_definition_cache,
                    cache_lock=cache_lock,
                )
                entry["kind"] = "weapon-swap"
                entry.update(contract)
                entry["sourceIni"] = weapon_row.source_virtual_path
                entry["line"] = weapon_row.line
            else:
                candidates = list(base_weapon_ids)
                if upgraded_weapon_id is not None:
                    candidates = [upgraded_weapon_id] + [
                        candidate
                        for candidate in candidates
                        if candidate.casefold() != upgraded_weapon_id.casefold()
                    ]
                resolved_effects: list[tuple[str, dict[str, object]]] = []
                failures: list[str] = []
                for candidate in candidates:
                    try:
                        resolved_effects.append(
                            (
                                candidate,
                                _gated_nugget_effect(
                                    documents,
                                    candidate,
                                    upgrade_id,
                                    constants,
                                    named_definition_cache=named_definition_cache,
                                    cache_lock=cache_lock,
                                ),
                            )
                        )
                    except ArmorCompilerError as exc:
                        failures.append(str(exc))
                chosen: dict[str, object] | None = None
                alternates: list[dict[str, object]] = []
                if resolved_effects and (
                    resolved_effects[0][0].casefold() == candidates[0].casefold()
                ):
                    # The combat weapon's effect drives; alternate weapons
                    # (bombard variants) are recorded exclusions.
                    chosen = resolved_effects[0][1]
                    alternates = [
                        {
                            "weaponId": candidate,
                            "effectId": effect.get("warheadId", effect.get("weaponId")),
                            "reason": (
                                "effect also resolved on a non-combat weapon; "
                                "the slice consumes the combat weapon's effect"
                            ),
                        }
                        for candidate, effect in resolved_effects[1:]
                    ]
                else:
                    unique_effects = {
                        _digest(effect): (candidate, effect)
                        for candidate, effect in resolved_effects
                    }
                    if len(unique_effects) == 1:
                        _, chosen = next(iter(unique_effects.values()))
                if chosen is None:
                    raise ArmorCompilerError(
                        f"WeaponSetUpgrade '{upgrade_id}' has no unique "
                        f"upgrade-gated effect across weapons "
                        f"{list(base_weapon_ids)}: {'; '.join(failures)}"
                    )
                entry.update(chosen)
                if alternates:
                    entry["excludedAlternates"] = alternates
            by_upgrade_id[upgrade_id.casefold()] = entry
            upgrades.append(entry)
    # Retail also sells weapon upgrades whose only authored behavior is a
    # StatusBitsUpgrade "dummy" while the damage rides upgrade-gated
    # DamageNuggets on the unchanged base weapon (RohanRohirrim forged
    # blades: "Just a dummy upgrade module to allow this unit to be
    # upgraded").  Those resolve through the same gated-nugget contract;
    # a StatusBitsUpgrade with no gated weapon effect is a production
    # legality marker and stays out — never an invented effect.
    for lineage in lineages:
        for behavior in _behavior_blocks(lineage, "StatusBitsUpgrade"):
            trigger = _behavior_assignment(behavior, "TriggeredBy")
            if trigger is None or not _tokens(trigger.value):
                continue
            upgrade_id = _tokens(trigger.value)[-1]
            if upgrade_id.casefold() in by_upgrade_id:
                continue
            resolved_effects: list[tuple[str, dict[str, object]]] = []
            for candidate in base_weapon_ids:
                try:
                    resolved_effects.append(
                        (
                            candidate,
                            _gated_nugget_effect(
                                documents,
                                candidate,
                                upgrade_id,
                                constants,
                                named_definition_cache=named_definition_cache,
                                cache_lock=cache_lock,
                            ),
                        )
                    )
                except ArmorCompilerError:
                    continue
            chosen: dict[str, object] | None = None
            if resolved_effects and base_weapon_ids and (
                resolved_effects[0][0].casefold() == base_weapon_ids[0].casefold()
            ):
                # The combat weapon's effect drives when it resolves.
                chosen = resolved_effects[0][1]
            else:
                unique_effects = {
                    _digest(effect): effect for _, effect in resolved_effects
                }
                if len(unique_effects) == 1:
                    chosen = next(iter(unique_effects.values()))
            if chosen is None:
                continue
            entry = {
                "upgradeId": upgrade_id,
                "behavior": {
                    "kind": "StatusBitsUpgrade",
                    "sourceIni": behavior.source_virtual_path,
                    "line": behavior.line,
                },
            }
            entry.update(chosen)
            by_upgrade_id[upgrade_id.casefold()] = entry
            upgrades.append(entry)
    return sorted(upgrades, key=lambda row: str(row["upgradeId"]).casefold())


def _gated_nugget_rows(
    nuggets: object,
    upgrade_id: str,
) -> list[Mapping[str, object]]:
    gated: list[Mapping[str, object]] = []
    for nugget in nuggets or ():
        fields = nugget["fields"]
        required = {
            token
            for row in fields.get("requiredupgradenames", ())
            for token in _tokens(str(row.get("expression", "")))
        }
        if upgrade_id in required:
            gated.append(nugget)
    return gated


def _gated_nugget_effect(
    documents: Mapping[str, bytes],
    base_weapon_id: str,
    upgrade_id: str,
    constants: Mapping[str, int | float],
    *,
    named_definition_cache: dict | None = None,
    cache_lock: "threading.Lock | None" = None,
) -> dict[str, object]:
    """Upgrade effect on an unchanged weapon: gated nuggets or warhead swap."""

    gated = _gated_nugget_rows(
        _weapon_damage_nuggets(
            documents,
            base_weapon_id,
            cache=named_definition_cache,
            cache_lock=cache_lock,
        ),
        upgrade_id,
    )
    if gated:
        return {
            "kind": "nugget-upgrade",
            "weaponId": base_weapon_id,
            "nuggets": [_nugget_contract(nugget, constants) for nugget in gated],
        }
    projectiles = _weapon_projectile_nuggets(documents, base_weapon_id)
    warhead_gated: list[tuple[Mapping[str, object], str | None]] = []
    base_warheads: dict[str, str] = {}
    for nugget in projectiles or ():
        fields = nugget["fields"]
        warhead_rows = fields.get("warheadtemplatename", ())
        warhead_id = (
            _tokens(str(warhead_rows[0].get("expression", "")))[-1]
            if warhead_rows and _tokens(str(warhead_rows[0].get("expression", "")))
            else None
        )
        required = {
            token
            for row in fields.get("requiredupgradenames", ())
            for token in _tokens(str(row.get("expression", "")))
        }
        if upgrade_id in required:
            warhead_gated.append((nugget, warhead_id))
        elif warhead_id and not required:
            base_warheads[warhead_id.casefold()] = warhead_id
    if len(warhead_gated) == 1 and warhead_gated[0][1] is not None:
        warhead_id = warhead_gated[0][1]
        nugget_rows = _weapon_damage_nuggets(
            documents,
            warhead_id,
            cache=named_definition_cache,
            cache_lock=cache_lock,
        )
        if not nugget_rows:
            raise ArmorCompilerError(
                f"warhead '{warhead_id}' has no authored DamageNuggets"
            )
        warhead_row = warhead_gated[0][0]["fields"]["warheadtemplatename"][0]
        entry: dict[str, object] = {
            "kind": "warhead-upgrade",
            "warheadId": warhead_id,
            "nuggets": [
                _nugget_contract(nugget, constants) for nugget in nugget_rows
            ],
            "sourceIni": warhead_row["sourceIni"],
            "line": warhead_row["line"],
        }
        if len(base_warheads) == 1:
            entry["replacesWarheadId"] = next(iter(base_warheads.values()))
        return entry
    # The base warhead itself can carry upgrade-gated DamageNuggets (the
    # dwarven axe-thrower pattern: same warhead, forged-blade nugget swap).
    if len(base_warheads) == 1:
        base_warhead_id = next(iter(base_warheads.values()))
        warhead_gated_nuggets = _gated_nugget_rows(
            _weapon_damage_nuggets(
                documents,
                base_warhead_id,
                cache=named_definition_cache,
                cache_lock=cache_lock,
            ),
            upgrade_id,
        )
        if warhead_gated_nuggets:
            return {
                "kind": "nugget-upgrade",
                "weaponId": base_weapon_id,
                "warheadId": base_warhead_id,
                "nuggets": [
                    _nugget_contract(nugget, constants)
                    for nugget in warhead_gated_nuggets
                ],
            }
    raise ArmorCompilerError(
        f"WeaponSetUpgrade '{upgrade_id}' on unchanged weapon "
        f"'{base_weapon_id}' has no unique upgrade-gated nugget or warhead"
    )


__all__ = [
    "ARMOR_INI_PATH",
    "ArmorCompilerError",
    "base_weapon_targets",
    "compile_armor_contract",
    "compile_armor_table",
    "compile_weapon_upgrades",
]
