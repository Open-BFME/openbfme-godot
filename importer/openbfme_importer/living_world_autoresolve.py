"""Convert retail's WAR OF THE RING AUTO-RESOLVE system - the nine
``livingworldautoresolve*.ini`` documents that decide a strategic battle without
loading the RTS map - into one bundle.

WHY THIS MODULE EXISTS
----------------------
The strategic setup screen draws its RULES rows ``Battle Type`` and ``Battle
Type Priority`` LOCKED, and says on screen that it locks them because no
auto-resolve path exists to choose between. Retail's own screens read
``Auto Resolve and RTS`` and ``Auto Resolve``, and retail's model is not a stub:
it is ~590 KB of authored tables that ship in ``ini.big``.

    AutoResolveWeapon AutoResolve_GondorArcherWeapon
        DamagePerRound = Damage:GONDORARCHERHORDE_DEFAULT_DAMAGE Against:AutoResolveUnit_Cavalry
        LevelBonus = Level:2 Bonus:10%
        ReduceAttackWhenHurt = Yes
    End

This module reads those documents and emits the numbers behind every one of
those symbols, so the strategic layer can eventually say what retail's model
says BECAUSE IT READ IT.

WHAT IT CONVERTS, AND WHAT RETAIL ACTUALLY SHIPS
------------------------------------------------
Nine documents, not eleven::

    livingworldautoresolvearmor.ini                   242,718 B   131 armors
    livingworldautoresolveweapon.ini                  204,745 B   150 weapons
    livingworldautoresolvebody.ini                    123,896 B   102 bodies
    livingworldautoresolveleadership.ini                8,626 B    13 leaderships
    livingworldautoresolvehandicaps.ini                 6,599 B    21 handicap levels
    livingworldautoresolvecombatchain.ini               3,405 B     9 combat chains
    livingworldautoresolvereinforcementschedule.ini     2,102 B     2 schedules
    livingworldautoresolvesciencepurchasepointbonus.ini 1,430 B     1 rule
    livingworldautoresolveresourcebonus.ini             1,401 B     1 rule

The catalog carries no other ``livingworldautoresolve*`` document. The two extra
files a prior costing named do not exist under RotWK; that is reported rather
than papered over with two empty tables.

THE PREPROCESSOR
----------------
These documents are the reason ``#DIVIDE()`` came up. They use three expression
forms and no others - ``#ADD``, ``#MULTIPLY`` (in the ``#define`` tables of
armor, weapon and body) and ``#DIVIDE`` (inline, in the handicap ladder). All
1,846 authored expressions are BINARY and NONE is nested. The evaluator here is
recursive anyway, so a nested one would evaluate rather than fail; what it will
NOT do is invent a value. A macro that does not resolve is carried in
``unresolved.macros`` BY NAME with the reason, and every reference to it is
carried in ``unresolved.references`` with the block and field that wanted it.
No number is ever substituted for a name.

Percent-ness travels with the value: retail writes ``100%`` for armor and
``150%`` for leadership, and ``#MULTIPLY( ARCHER_BASIC_VS_ARCHER_DEFAULT_ARMOR
MEN_ARMOR_SCALAR )`` has to stay a percent through the multiply. Both the
fraction and retail's own written percent are emitted, so nothing downstream has
to remember which notation a field used.

WHAT IT REFUSES TO DECIDE
-------------------------
``LevelBonus = Level:2 Bonus:10%`` is AMBIGUOUS IN RETAIL'S OWN WORDS. The file
header calls it "a multiplier for all damage based on the level of unit"; read
literally that makes a level-2 unit do a tenth of its damage, which contradicts
the ladder rising to 50% at level 10. Retail states nothing that settles it.
This module therefore emits ``bonusPercent`` VERBATIM and no derived multiplier,
and records the ambiguity in ``semanticGaps`` by name. The GDScript model takes
the reading as an explicit argument and labels it as this project's, never
retail's.

It reads through the CATALOGS, never through the effective-assets cache.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import re
from typing import Any, Callable, Mapping

from .livingmap_bundle import CatalogReader
from .livingworld import Gap, Node, expand_document, flatten_document, read_tree

SCHEMA = "openbfme.living-world-autoresolve"
SCHEMA_VERSION = 1

MANIFEST_NAME = "living-world-autoresolve.json"

#: Retail's nine documents, keyed by the section name this bundle uses. The
#: order is the order they are read and reported in.
DOCUMENTS: tuple[tuple[str, str], ...] = (
    ("armor", "data/ini/livingworldautoresolvearmor.ini"),
    ("weapon", "data/ini/livingworldautoresolveweapon.ini"),
    ("body", "data/ini/livingworldautoresolvebody.ini"),
    ("combatChain", "data/ini/livingworldautoresolvecombatchain.ini"),
    ("leadership", "data/ini/livingworldautoresolveleadership.ini"),
    ("handicap", "data/ini/livingworldautoresolvehandicaps.ini"),
    ("reinforcement", "data/ini/livingworldautoresolvereinforcementschedule.ini"),
    ("resourceBonus", "data/ini/livingworldautoresolveresourcebonus.ini"),
    (
        "sciencePurchasePointBonus",
        "data/ini/livingworldautoresolvesciencepurchasepointbonus.ini",
    ),
)

#: The unit types, quoted from ``livingworldautoresolvearmor.ini``'s own header,
#: which says they "are defined in code; ask an engineer if you need to add more
#: unit types". ``AutoResolveUnit_INVALID`` is not in that list but IS authored
#: in every weapon block, with retail's own comment "For now, since not everyone
#: has a type yet", so it is carried as a tenth key and marked.
UNIT_TYPES: tuple[str, ...] = (
    "AutoResolveUnit_Soldier",
    "AutoResolveUnit_Archer",
    "AutoResolveUnit_Pikemen",
    "AutoResolveUnit_Cavalry",
    "AutoResolveUnit_Monster",
    "AutoResolveUnit_Hero",
    "AutoResolveUnit_Fortress",
    "AutoResolveUnit_Siege",
    "AutoResolveUnit_Support",
)
UNIT_TYPE_INVALID = "AutoResolveUnit_INVALID"

#: The four names retail's own comments mark as hard-coded fallbacks. Each
#: comment is quoted at the point it is used.
DEFAULT_ARMOR = "AutoResolve_DefaultArmor"
DEFAULT_WEAPON = "AutoResolve_DefaultWeapon"
DEFAULT_BODY = "AutoResolve_DefaultBody"
DEFAULT_COMBAT_CHAIN = "AutoResolve_DefaultCombatChain"

#: Bounded so a hostile or corrupt catalog cannot make this module allocate
#: without limit. Retail's real numbers are far below every one of these.
MAX_DOCUMENT_BYTES = 8 * 1024 * 1024
MAX_DEFINES = 20_000
MAX_BLOCKS = 4_000
MAX_MACRO_DEPTH = 32


class AutoResolveError(RuntimeError):
    """The bundle cannot be built, with the exact reason."""


# ---------------------------------------------------------------------------
# values and the preprocessor
# ---------------------------------------------------------------------------

_DEFINE = re.compile(r"^#define\s+([A-Za-z_][A-Za-z0-9_]*)\s+(.+?)\s*$", re.IGNORECASE)
_NUMBER = re.compile(r"^([+-]?\d+(?:\.\d+)?)\s*(%?)$")
_EXPRESSION = re.compile(r"^#([A-Za-z]+)\s*\(\s*(.*?)\s*\)\s*$")
_NAME = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")

#: The three operators retail authors in these documents, and nothing else. An
#: operator outside this table is a gap naming the spelling, never a guess at
#: what it might mean.
_OPERATORS: Mapping[str, Callable[[float, float], float]] = {
    "ADD": lambda a, b: a + b,
    "MULTIPLY": lambda a, b: a * b,
    "DIVIDE": lambda a, b: a / b,
    "SUBTRACT": lambda a, b: a - b,
}


class Value:
    """One resolved number, with retail's own notation preserved.

    ``percent`` records that retail WROTE it as a percentage. ``fraction`` is
    the number a multiplier would use: ``100%`` is ``1.0`` with
    ``percent=True``, and a bare ``1.0`` is ``1.0`` with ``percent=False``. Both
    are emitted so nothing downstream has to remember the notation.
    """

    __slots__ = ("number", "percent")

    def __init__(self, number: float, percent: bool) -> None:
        self.number = float(number)
        self.percent = bool(percent)

    @property
    def fraction(self) -> float:
        return self.number / 100.0 if self.percent else self.number

    def public(self) -> dict[str, Any]:
        record: dict[str, Any] = {"value": _tidy(self.number), "percent": self.percent}
        if self.percent:
            record["fraction"] = _tidy(self.fraction)
        return record

    def __repr__(self) -> str:  # pragma: no cover - debugging aid
        return f"Value({self.number}{'%' if self.percent else ''})"


def _tidy(number: float) -> float | int:
    """Emit an integral float as an int so the manifest reads like retail's."""

    if number == int(number) and abs(number) < 1e15:
        return int(number)
    return round(number, 10)


class MacroTable:
    """The ``#define`` table of ONE document, with lazy, cycle-safe evaluation.

    Retail's defines are file-local: ``MEN_ARMOR_SCALAR`` is declared separately
    in the armor, weapon and body documents. Keeping one table per document is
    therefore the faithful reading, not a convenience.
    """

    def __init__(self, virtual_path: str) -> None:
        self.virtual_path = virtual_path
        self.raw: dict[str, tuple[str, int]] = {}
        self._cache: dict[str, Value | None] = {}
        #: ``NAME -> reason``, every define that could not be evaluated.
        self.unresolved: dict[str, str] = {}

    def add(self, name: str, body: str, line: int) -> None:
        if len(self.raw) >= MAX_DEFINES:
            raise AutoResolveError(
                f"{self.virtual_path}: more than {MAX_DEFINES} #defines"
            )
        self.raw[name] = (body, line)

    def resolve(self, name: str, _stack: tuple[str, ...] = ()) -> Value | None:
        """The value of ``name``, or ``None`` with a stated reason recorded."""

        if name in self._cache and not _stack:
            return self._cache[name]
        if name in _stack:
            self._fail(name, f"cyclic #define: {' -> '.join((*_stack, name))}")
            return None
        if len(_stack) >= MAX_MACRO_DEPTH:
            self._fail(name, f"#define nested deeper than {MAX_MACRO_DEPTH}")
            return None
        entry = self.raw.get(name)
        if entry is None:
            self._fail(name, "no #define declares this name")
            return None
        value = self._evaluate(entry[0], name, (*_stack, name))
        if not _stack:
            self._cache[name] = value
        return value

    def _evaluate(self, body: str, owner: str, stack: tuple[str, ...]) -> Value | None:
        literal = _NUMBER.match(body)
        if literal is not None:
            return Value(float(literal.group(1)), bool(literal.group(2)))
        expression = _EXPRESSION.match(body)
        if expression is None:
            if _NAME.match(body):
                return self.resolve(body, stack)
            self._fail(owner, f"body is not a number, a name or an expression: {body!r}")
            return None
        operator = expression.group(1).upper()
        function = _OPERATORS.get(operator)
        if function is None:
            self._fail(owner, f"unsupported preprocessor operator #{expression.group(1)}")
            return None
        # Retail separates the two operands with whitespace in these documents
        # and with a comma in the handicap ladder; both are accepted because
        # both are retail's own spelling of the same thing.
        arguments = [token for token in expression.group(2).replace(",", " ").split() if token]
        if len(arguments) != 2:
            self._fail(owner, f"#{operator} takes 2 operands, got {len(arguments)}")
            return None
        operands: list[Value] = []
        for token in arguments:
            resolved = self.evaluate_token(token, stack)
            if resolved is None:
                self._fail(owner, f"operand {token} did not resolve")
                return None
            operands.append(resolved)
        left, right = operands
        if operator == "DIVIDE" and right.number == 0:
            self._fail(owner, "#DIVIDE by zero")
            return None
        return Value(function(left.number, right.number), left.percent or right.percent)

    def evaluate_token(self, token: str, stack: tuple[str, ...] = ()) -> Value | None:
        """A literal, a macro name, or an inline expression - whichever it is."""

        literal = _NUMBER.match(token)
        if literal is not None:
            return Value(float(literal.group(1)), bool(literal.group(2)))
        if token.startswith("#"):
            return self._evaluate(token, token, stack)
        if _NAME.match(token):
            return self.resolve(token, stack)
        return None

    def _fail(self, name: str, reason: str) -> None:
        self.unresolved.setdefault(name, reason)

    def resolve_all(self) -> None:
        """Force every define, so the manifest's unresolved list is complete."""

        for name in sorted(self.raw):
            self.resolve(name)


# ---------------------------------------------------------------------------
# reading
# ---------------------------------------------------------------------------


class _Reference:
    """One place a document named a macro that did not resolve."""

    __slots__ = ("document", "block", "field", "name", "reason")

    def __init__(self, document: str, block: str, field: str, name: str, reason: str) -> None:
        self.document = document
        self.block = block
        self.field = field
        self.name = name
        self.reason = reason

    def public(self) -> dict[str, str]:
        return {
            "document": self.document,
            "block": self.block,
            "field": self.field,
            "name": self.name,
            "reason": self.reason,
        }


class _Section:
    """One document, split into its ``#define`` table and its block tree."""

    def __init__(self, key: str, virtual_path: str, payload: bytes, archive: str) -> None:
        self.key = key
        self.virtual_path = virtual_path
        self.archive = archive
        self.payload = payload
        self.macros = MacroTable(virtual_path)
        self.gaps: list[Gap] = []
        self.roots: tuple[Node, ...] = ()
        self.references: list[_Reference] = []

    def source(self) -> dict[str, Any]:
        return {
            "virtualPath": self.virtual_path,
            "archive": self.archive,
            "bytes": len(self.payload),
            "sha256": hashlib.sha256(self.payload).hexdigest(),
        }

    def note(self, block: str, field: str, name: str, reason: str) -> None:
        self.references.append(_Reference(self.key, block, field, name, reason))

    def number(self, node: Node, field: str, token: str) -> Value | None:
        """Resolve one authored token, recording the miss by NAME when it fails."""

        value = self.macros.evaluate_token(token)
        if value is None:
            reason = self.macros.unresolved.get(token, "not a number, a name or an expression")
            self.note(node.name or node.kind, field, token, reason)
        return value


def _read_section(reader: CatalogReader, key: str, virtual_path: str) -> _Section:
    entry = reader.resolve(virtual_path)
    if entry is None:
        raise AutoResolveError(f"{virtual_path} is not in the catalog")
    if entry.size > MAX_DOCUMENT_BYTES:
        raise AutoResolveError(
            f"{virtual_path} is {entry.size} bytes, over the {MAX_DOCUMENT_BYTES} ceiling"
        )
    payload = reader.read(entry)
    section = _Section(key, virtual_path, payload, entry.archive)

    # Reuse the living-world reader rather than writing a second one: it already
    # decodes retail's codepage, strips both comment spellings, splices
    # ``#include`` and quarantines a document whose blocks do not balance.
    document = expand_document(virtual_path, lambda _path: payload)
    lines = flatten_document(
        document, openers=frozenset(), whitespace_pairs=False, gaps=section.gaps
    )
    #: ``#define`` lines are the one preprocessor form this dialect authors at
    #: top level; they are pulled out here so the block reader does not have to
    #: gap 2,254 of them, and every other preprocessor line still gaps.
    body: list[Any] = []
    for line in lines:
        match = _DEFINE.match(line.text)
        if match is not None:
            section.macros.add(match.group(1), match.group(2).strip(), line.number)
            continue
        body.append(line)
    tree = read_tree(body, openers=frozenset(), whitespace_pairs=False)
    section.gaps.extend(tree.gaps)
    if len(tree.roots) > MAX_BLOCKS:
        raise AutoResolveError(f"{virtual_path}: more than {MAX_BLOCKS} blocks")
    section.roots = tree.roots
    for key_, value in tree.fields:
        section.gaps.append(
            Gap(
                virtual_path=virtual_path,
                line=0,
                scope="<root>",
                reason="unmodelled-root-field",
                detail=f"{key_} = {value}",
            )
        )
    section.macros.resolve_all()
    return section


def _field_gap(section: _Section, node: Node, key: str, reason: str = "unknown-field") -> None:
    section.gaps.append(
        Gap(
            virtual_path=node.virtual_path,
            line=node.line,
            scope=f"{node.kind} {node.name or ''}".strip(),
            reason=reason,
            detail=key,
        )
    )


def _pairs(value: str) -> dict[str, str]:
    """``Damage:50 Against:AutoResolveUnit_Archer`` -> ``{damage: ..., against: ...}``."""

    found: dict[str, str] = {}
    for token in value.split():
        head, sep, tail = token.partition(":")
        if not sep:
            continue
        found[head.casefold()] = tail
    return found


def _bool(value: str) -> bool | None:
    folded = value.strip().casefold()
    if folded in ("yes", "true"):
        return True
    if folded in ("no", "false"):
        return False
    return None


# ---------------------------------------------------------------------------
# the nine readers
# ---------------------------------------------------------------------------


def _read_armors(section: _Section) -> dict[str, Any]:
    armors: dict[str, Any] = {}
    for node in section.roots:
        if node.kind.casefold() != "autoresolvearmor":
            _field_gap(section, node, node.kind, "unmodelled-block")
            continue
        name = node.name or ""
        rows: dict[str, Any] = {}
        for key, value in node.fields:
            if key.casefold() != "armor":
                _field_gap(section, node, key)
                continue
            tokens = value.split()
            if len(tokens) != 2:
                _field_gap(section, node, f"Armor = {value}", "unsupported-armor-row")
                continue
            resolved = section.number(node, "Armor", tokens[1])
            rows[tokens[0]] = (
                {"raw": tokens[1], **resolved.public()}
                if resolved is not None
                else {"raw": tokens[1], "unresolved": True}
            )
        for child in node.children:
            _field_gap(section, node, child.kind, "unmodelled-block")
        armors[name] = {"vs": rows, "line": node.line}
    return armors


def _read_weapons(section: _Section) -> dict[str, Any]:
    weapons: dict[str, Any] = {}
    for node in section.roots:
        if node.kind.casefold() != "autoresolveweapon":
            _field_gap(section, node, node.kind, "unmodelled-block")
            continue
        damage: dict[str, Any] = {}
        level_bonus: list[dict[str, Any]] = []
        record: dict[str, Any] = {"line": node.line}
        for key, value in node.fields:
            folded = key.casefold()
            if folded == "damageperround":
                parts = _pairs(value)
                against = parts.get("against")
                token = parts.get("damage")
                if against is None or token is None:
                    _field_gap(section, node, f"{key} = {value}", "unsupported-damage-row")
                    continue
                resolved = section.number(node, "DamagePerRound", token)
                damage[against] = (
                    {"raw": token, **resolved.public()}
                    if resolved is not None
                    else {"raw": token, "unresolved": True}
                )
            elif folded == "levelbonus":
                parts = _pairs(value)
                level = parts.get("level")
                bonus = parts.get("bonus")
                if level is None or bonus is None:
                    _field_gap(section, node, f"{key} = {value}", "unsupported-level-bonus")
                    continue
                resolved = section.number(node, "LevelBonus", bonus)
                row: dict[str, Any] = {"level": int(level), "raw": bonus}
                # DELIBERATELY NOT a multiplier. See the module docstring: retail
                # does not state whether Bonus:10% means x0.10 or x1.10, so only
                # the authored percentage travels.
                if resolved is not None:
                    row["bonusPercent"] = _tidy(resolved.number)
                else:
                    row["unresolved"] = True
                level_bonus.append(row)
            elif folded == "reduceattackwhenhurt":
                flag = _bool(value)
                if flag is None:
                    _field_gap(section, node, f"{key} = {value}", "unsupported-boolean")
                else:
                    record["reduceAttackWhenHurt"] = flag
            elif folded == "misspercentchance":
                resolved = section.number(node, "MissPercentChance", value.strip())
                if resolved is not None:
                    record["missPercentChance"] = _tidy(resolved.number)
            else:
                _field_gap(section, node, key)
        for child in node.children:
            _field_gap(section, node, child.kind, "unmodelled-block")
        level_bonus.sort(key=lambda row: row["level"])
        record["damagePerRound"] = damage
        record["levelBonus"] = level_bonus
        weapons[node.name or ""] = record
    return weapons


def _read_bodies(section: _Section) -> dict[str, Any]:
    bodies: dict[str, Any] = {}
    for node in section.roots:
        if node.kind.casefold() != "autoresolvebody":
            _field_gap(section, node, node.kind, "unmodelled-block")
            continue
        hitpoints: list[dict[str, Any]] = []
        regenerate: list[dict[str, Any]] = []
        record: dict[str, Any] = {"line": node.line}
        for key, value in node.fields:
            folded = key.casefold()
            if folded in ("hitpointsatlevel", "regenerateatlevel"):
                parts = _pairs(value)
                level = parts.get("level")
                amount = parts.get("hitpoints" if folded == "hitpointsatlevel" else "regenerate")
                if level is None or amount is None:
                    _field_gap(section, node, f"{key} = {value}", "unsupported-level-row")
                    continue
                resolved = section.number(node, key, amount)
                row: dict[str, Any] = {"level": int(level), "raw": amount}
                if resolved is not None:
                    row["amount"] = _tidy(resolved.number)
                else:
                    row["unresolved"] = True
                (hitpoints if folded == "hitpointsatlevel" else regenerate).append(row)
            elif folded == "canbeattacked":
                flag = _bool(value)
                if flag is None:
                    _field_gap(section, node, f"{key} = {value}", "unsupported-boolean")
                else:
                    record["canBeAttacked"] = flag
            elif folded == "leaveinarmysummary":
                flag = _bool(value)
                if flag is None:
                    _field_gap(section, node, f"{key} = {value}", "unsupported-boolean")
                else:
                    record["leaveInArmySummary"] = flag
            else:
                _field_gap(section, node, key)
        for child in node.children:
            _field_gap(section, node, child.kind, "unmodelled-block")
        hitpoints.sort(key=lambda row: row["level"])
        regenerate.sort(key=lambda row: row["level"])
        record["hitpointsAtLevel"] = hitpoints
        record["regenerateAtLevel"] = regenerate
        bodies[node.name or ""] = record
    return bodies


def _read_combat_chains(section: _Section) -> dict[str, Any]:
    chains: dict[str, Any] = {}
    for node in section.roots:
        if node.kind.casefold() != "autoresolvecombatchain":
            _field_gap(section, node, node.kind, "unmodelled-block")
            continue
        targets: dict[str, Any] = {}
        for key, value in node.fields:
            if key.casefold() != "target":
                _field_gap(section, node, key)
                continue
            parts = _pairs(value)
            target = parts.get("target")
            priority = parts.get("priority")
            if target is None or priority is None:
                _field_gap(section, node, f"{key} = {value}", "unsupported-target-row")
                continue
            resolved = section.number(node, "Target", priority)
            if resolved is None:
                targets[target] = {"raw": priority, "unresolved": True}
            else:
                targets[target] = _tidy(resolved.number)
        for child in node.children:
            _field_gap(section, node, child.kind, "unmodelled-block")
        chains[node.name or ""] = {"targets": targets, "line": node.line}
    return chains


def _read_leaderships(section: _Section) -> dict[str, Any]:
    leaderships: dict[str, Any] = {}
    for node in section.roots:
        if node.kind.casefold() != "autoresolveleadership":
            _field_gap(section, node, node.kind, "unmodelled-block")
            continue
        record: dict[str, Any] = {"line": node.line, "affects": [], "bonusForLevel": []}
        for key, value in node.fields:
            folded = key.casefold()
            if folded == "affects":
                record["affects"] = value.split()
            elif folded == "affectshigherlevelfirst":
                flag = _bool(value)
                if flag is None:
                    _field_gap(section, node, f"{key} = {value}", "unsupported-boolean")
                else:
                    record["affectsHigherLevelFirst"] = flag
            else:
                _field_gap(section, node, key)
        for child in node.children:
            if child.kind.casefold() != "bonusforlevel":
                _field_gap(section, node, child.kind, "unmodelled-block")
                continue
            bonus: dict[str, Any] = {"line": child.line}
            for key, value in child.fields:
                folded = key.casefold()
                mapped = {
                    "minlevel": "minLevel",
                    "weaponmultiplier": "weaponMultiplier",
                    "armormultiplier": "armorMultiplier",
                    "experiencemultiplier": "experienceMultiplier",
                    "maximumunitsaffected": "maximumUnitsAffected",
                    "priority": "priority",
                }.get(folded)
                if mapped is None:
                    _field_gap(section, child, key)
                    continue
                resolved = section.number(child, key, value.strip())
                if resolved is None:
                    bonus[mapped] = {"raw": value.strip(), "unresolved": True}
                elif mapped in ("weaponMultiplier", "armorMultiplier", "experienceMultiplier"):
                    bonus[mapped] = _tidy(resolved.fraction)
                    bonus[mapped + "Percent"] = _tidy(resolved.number)
                else:
                    bonus[mapped] = _tidy(resolved.number)
            record["bonusForLevel"].append(bonus)
        record["bonusForLevel"].sort(key=lambda row: row.get("minLevel", 0))
        leaderships[node.name or ""] = record
    return leaderships


def _read_handicaps(section: _Section) -> list[dict[str, Any]]:
    levels: list[dict[str, Any]] = []
    for node in section.roots:
        if node.kind.casefold() != "autoresolvehandicaplevel":
            _field_gap(section, node, node.kind, "unmodelled-block")
            continue
        record: dict[str, Any] = {"line": node.line}
        for key, value in node.fields:
            mapped = {
                "guidisplayedlevel": "guiDisplayedLevel",
                "weaponmultiplier": "weaponMultiplier",
                "armormultiplier": "armorMultiplier",
                "experiencemultiplier": "experienceMultiplier",
            }.get(key.casefold())
            if mapped is None:
                _field_gap(section, node, key)
                continue
            resolved = section.number(node, key, value.strip())
            if resolved is None:
                record[mapped] = {"raw": value.strip(), "unresolved": True}
            else:
                record[mapped] = _tidy(resolved.fraction)
        for child in node.children:
            _field_gap(section, node, child.kind, "unmodelled-block")
        levels.append(record)
    levels.sort(key=lambda row: row.get("guiDisplayedLevel", 0))
    return levels


def _read_reinforcement(section: _Section) -> dict[str, Any]:
    schedules: dict[str, Any] = {}
    for node in section.roots:
        if node.kind.casefold() != "autoresolvereinforcementschedule":
            _field_gap(section, node, node.kind, "unmodelled-block")
            continue
        explicit: dict[str, int] = {}
        record: dict[str, Any] = {"line": node.line}
        for key, value in node.fields:
            stripped = key.strip()
            if stripped.casefold() == "eachremaining":
                token = value.strip()
                # Retail writes ``+1``; the sign is part of the authored value
                # and means "this many rounds AFTER the previous army".
                resolved = section.number(node, key, token)
                if resolved is None:
                    record["eachRemaining"] = {"raw": token, "unresolved": True}
                else:
                    record["eachRemaining"] = _tidy(resolved.number)
                    record["eachRemainingRaw"] = token
                continue
            if not stripped.isdigit():
                _field_gap(section, node, key)
                continue
            resolved = section.number(node, key, value.strip())
            if resolved is None:
                continue
            explicit[stripped] = int(resolved.number)
        for child in node.children:
            _field_gap(section, node, child.kind, "unmodelled-block")
        record["armyRound"] = dict(sorted(explicit.items(), key=lambda item: int(item[0])))
        schedules[node.name or ""] = record
    return schedules


def _read_threshold_bonus(section: _Section, kind: str, threshold_key: str) -> list[dict[str, Any]]:
    rules: list[dict[str, Any]] = []
    for node in section.roots:
        if node.kind.casefold() != kind:
            _field_gap(section, node, node.kind, "unmodelled-block")
            continue
        record: dict[str, Any] = {"line": node.line, "sides": [], "bonuses": []}
        for key, value in node.fields:
            if key.casefold() == "sides":
                record["sides"] = value.split()
            else:
                _field_gap(section, node, key)
        for child in node.children:
            if child.kind.casefold() != "bonus":
                _field_gap(section, node, child.kind, "unmodelled-block")
                continue
            bonus: dict[str, Any] = {"line": child.line}
            for key, value in child.fields:
                folded = key.casefold()
                mapped = {
                    threshold_key: "minimum",
                    "weaponmultiplier": "weaponMultiplier",
                    "armormultiplier": "armorMultiplier",
                }.get(folded)
                if mapped is None:
                    _field_gap(section, child, key)
                    continue
                resolved = section.number(child, key, value.strip())
                if resolved is None:
                    bonus[mapped] = {"raw": value.strip(), "unresolved": True}
                else:
                    bonus[mapped] = _tidy(resolved.fraction)
            record["bonuses"].append(bonus)
        record["bonuses"].sort(key=lambda row: row.get("minimum", 0))
        rules.append(record)
    return rules


# ---------------------------------------------------------------------------
# the bundle
# ---------------------------------------------------------------------------

#: Places where retail's documents do not state what a number MEANS. Every one
#: is emitted so the model cannot quietly pick a reading and the screen cannot
#: quietly show a number retail never wrote. The wording is the finding.
SEMANTIC_GAPS: tuple[dict[str, str], ...] = (
    {
        "id": "levelBonus.reading",
        "field": "AutoResolveWeapon.LevelBonus Bonus:<percent>",
        "detail": (
            "livingworldautoresolveweapon.ini calls LevelBonus 'a multiplier for "
            "all damage based on the level of unit', but authors Bonus:10% at "
            "level 2 rising to Bonus:50% at level 10. Read as a literal "
            "multiplier a level-10 unit would do HALF the damage of a level-1 "
            "unit, which contradicts 'bonus' and the rising ladder. Retail "
            "states nothing that settles x0.10 versus x1.10, so only the "
            "authored percentage is emitted."
        ),
    },
    {
        "id": "factor.composition",
        "field": "<every multiplier>",
        "detail": (
            "Retail states what each multiplier does on its own - armor 'multiply "
            "all damage done TO affected unit', handicap 'a penalty', resource "
            "bonus '5% bonus to attack' - but never states the ORDER or the "
            "combining rule when several apply at once. The model composes them "
            "multiplicatively and labels that as this project's arrangement."
        ),
    },
    {
        "id": "round.order",
        "field": "<the auto-resolve round>",
        "detail": (
            "Retail names the parts of a round (damage per round, regeneration "
            "'at the end of each auto-resolve round', reinforcement 'at the "
            "beginning of round N') but never states the full within-round "
            "sequence, nor whether both sides strike simultaneously. The model "
            "resolves simultaneously and says so."
        ),
    },
    {
        "id": "miss.roll",
        "field": "AutoResolveWeapon.MissPercentChance",
        "detail": (
            "Retail's default weapon carries MissPercentChance = 50 with the "
            "comment 'default = all weapons miss 50% of the time'. That is a "
            "RANDOM ROLL; retail states no seed and no stream. The model "
            "computes the EXPECTATION (damage x (1 - miss/100)) rather than "
            "rolling, so it is deterministic, and never claims to reproduce a "
            "particular retail battle."
        ),
    },
)


def build_bundle(
    catalog_path: pathlib.Path | str, output_path: pathlib.Path | str
) -> dict[str, Any]:
    """Read retail's nine documents and write the bundle. Returns the manifest."""

    bundle = build_from_reader(CatalogReader(catalog_path))
    output = pathlib.Path(output_path)
    if output.suffix.casefold() != ".json":
        output = output / MANIFEST_NAME
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(bundle, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return bundle


def build_from_reader(reader: Any) -> dict[str, Any]:
    """The manifest, from anything with ``resolve``/``read``. No file is written.

    Split out from :func:`build_bundle` so the readers can be exercised against
    synthetic documents without a retail install, which is the only way the
    unresolved-macro paths can be tested at all: retail's own nine documents
    leave NOTHING unresolved.
    """

    sections: dict[str, _Section] = {}
    for key, virtual_path in DOCUMENTS:
        sections[key] = _read_section(reader, key, virtual_path)

    armors = _read_armors(sections["armor"])
    weapons = _read_weapons(sections["weapon"])
    bodies = _read_bodies(sections["body"])
    chains = _read_combat_chains(sections["combatChain"])
    leaderships = _read_leaderships(sections["leadership"])
    handicaps = _read_handicaps(sections["handicap"])
    reinforcement = _read_reinforcement(sections["reinforcement"])
    resource = _read_threshold_bonus(
        sections["resourceBonus"], "livingworldautoresolveresourcebonus", "minresourcebonus"
    )
    science = _read_threshold_bonus(
        sections["sciencePurchasePointBonus"],
        "livingworldautoresolvesciencepurchasepointbonus",
        "minsciencepurchasepointsforbonus",
    )

    unresolved_macros: list[dict[str, str]] = []
    references: list[dict[str, str]] = []
    gaps: list[dict[str, Any]] = []
    define_total = 0
    for key, _path in DOCUMENTS:
        section = sections[key]
        define_total += len(section.macros.raw)
        for name in sorted(section.macros.unresolved):
            unresolved_macros.append(
                {
                    "document": key,
                    "name": name,
                    "reason": section.macros.unresolved[name],
                }
            )
        references.extend(reference.public() for reference in section.references)
        gaps.extend(gap.public() for gap in section.gaps)
    references.sort(key=lambda row: (row["document"], row["block"], row["field"], row["name"]))

    armor_rows = sum(len(record["vs"]) for record in armors.values())
    damage_rows = sum(len(record["damagePerRound"]) for record in weapons.values())
    level_rows = sum(len(record["levelBonus"]) for record in weapons.values())
    hitpoint_rows = sum(len(record["hitpointsAtLevel"]) for record in bodies.values())
    regenerate_rows = sum(len(record["regenerateAtLevel"]) for record in bodies.values())
    target_rows = sum(len(record["targets"]) for record in chains.values())

    bundle: dict[str, Any] = {
        "schema": SCHEMA,
        "schemaVersion": SCHEMA_VERSION,
        "sources": [sections[key].source() for key, _path in DOCUMENTS],
        "unitTypes": list(UNIT_TYPES),
        "unitTypeInvalid": UNIT_TYPE_INVALID,
        "defaults": {
            "armor": DEFAULT_ARMOR,
            "weapon": DEFAULT_WEAPON,
            "body": DEFAULT_BODY,
            "combatChain": DEFAULT_COMBAT_CHAIN,
        },
        "totals": {
            "documents": len(DOCUMENTS),
            "armors": len(armors),
            "weapons": len(weapons),
            "bodies": len(bodies),
            "combatChains": len(chains),
            "leaderships": len(leaderships),
            "handicapLevels": len(handicaps),
            "reinforcementSchedules": len(reinforcement),
            "resourceBonusRules": len(resource),
            "sciencePurchasePointBonusRules": len(science),
            "defines": define_total,
            "definesUnresolved": len(unresolved_macros),
            "armorRows": armor_rows,
            "damageRows": damage_rows,
            "levelBonusRows": level_rows,
            "hitpointRows": hitpoint_rows,
            "regenerateRows": regenerate_rows,
            "targetPriorityRows": target_rows,
            "unresolvedReferences": len(references),
            "gaps": len(gaps),
        },
        "armors": armors,
        "weapons": weapons,
        "bodies": bodies,
        "combatChains": chains,
        "leaderships": leaderships,
        "handicapLevels": handicaps,
        "reinforcementSchedules": reinforcement,
        "resourceBonus": resource,
        "sciencePurchasePointBonus": science,
        "unresolved": {"macros": unresolved_macros, "references": references},
        "semanticGaps": [dict(entry) for entry in SEMANTIC_GAPS],
        "gaps": gaps,
    }
    return bundle


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        prog="python -m openbfme_importer.living_world_autoresolve",
        description="Convert retail's War of the Ring auto-resolve documents.",
    )
    parser.add_argument("--catalog", required=True, type=pathlib.Path)
    parser.add_argument(
        "--out",
        required=True,
        type=pathlib.Path,
        help=f"a directory, or a path ending in .json ({MANIFEST_NAME} by default)",
    )
    args = parser.parse_args(argv)
    bundle = build_bundle(args.catalog, args.out)
    totals = bundle["totals"]
    print(
        "wrote %s - %d armors, %d weapons, %d bodies, %d combat chains, "
        "%d leaderships, %d handicap levels, %d reinforcement schedules"
        % (
            args.out,
            totals["armors"],
            totals["weapons"],
            totals["bodies"],
            totals["combatChains"],
            totals["leaderships"],
            totals["handicapLevels"],
            totals["reinforcementSchedules"],
        )
    )
    print(
        "  %d #defines, %d unresolved; %d unresolved references; %d gaps"
        % (
            totals["defines"],
            totals["definesUnresolved"],
            totals["unresolvedReferences"],
            totals["gaps"],
        )
    )
    for entry in bundle["unresolved"]["macros"]:
        print("  UNRESOLVED MACRO %s (%s): %s" % (entry["name"], entry["document"], entry["reason"]))
    for entry in bundle["unresolved"]["references"]:
        print(
            "  UNRESOLVED REFERENCE %s.%s wanted %s: %s"
            % (entry["block"], entry["field"], entry["name"], entry["reason"])
        )
    for entry in bundle["semanticGaps"]:
        print("  SEMANTIC GAP %s: %s" % (entry["id"], entry["field"]))
    return 0


if __name__ == "__main__":  # pragma: no cover - CLI entry point
    raise SystemExit(main())
