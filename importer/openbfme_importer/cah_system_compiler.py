"""Compile retail's Create-a-Hero class system into a descriptor and a runtime.

WHAT THIS IS FOR
----------------
``sage_cah.py`` already reads a saved hero (``.cah``).  A saved hero is *only*
names and small integers: a class/subclass pair, one ``GroupOrder`` per
customisation group, and a list of purchased ``CommandButton`` names.  Not one
stat is in the file.  Every number a Create-a-Hero has on the battlefield comes
from resolving those names against INI at load time.

This module is the other half: it reads the INI side, so a saved hero can be
turned into something the game can actually spawn.

WHAT RETAIL AUTHORS
-------------------
``data/ini/createaherosystem.ini`` declares, through six ``#include``s:

* ``CreateAHeroBlingBinder`` -- the customisation groups.  Five of them carry
  ``BlingType = ATTRIBUTE``; those are the attribute sliders and the only ones
  this module compiles.  The other seven are APPEARANCE (helmet, shoulders,
  body, gauntlets, weapon, shield, boots) and are out of scope, named in
  ``limitations``.
* ``CreateAHeroClass`` -- seven classes, each with two or three ``SubClass``
  blocks.  A subclass authors ``SpendableAttributePoints`` and, per attribute
  group, an ``Attribute`` block giving ``MinValueUpgrade`` /
  ``MaxValueUpgrade`` / ``DefaultValueUpgrade``.

Each of those upgrade names resolves twice:

1. to an ``Upgrade`` in ``createaheroupgrades.inc`` carrying ``GroupName`` and
   ``GroupOrder`` -- the 0-based index a ``.cah`` stores; and
2. to a ``ModifierList`` of the same bare name in ``attributemodifier.ini``
   carrying the literal multipliers.

THE POINT-BUDGET RULE, WHICH IS NOWHERE STATED AND IS ENFORCED HERE
-------------------------------------------------------------------
No comment in the retail corpus says what an attribute point buys.  It is
recoverable by arithmetic: **one point per step above the class minimum**, and
the authored default loadout spends the budget exactly.

    sum(defaultStep - minStep) over the five groups == SpendableAttributePoints

That holds for all sixteen live subclasses with no exceptions -- sixteen
independent confirmations.  :func:`compile_cah_system_descriptor` raises if a
subclass ever violates it rather than emitting a budget nobody can trust, and
:func:`attribute_spend` is the one place the rule is implemented.

Independent confirmation from the other side of the system: the EA-shipped
``data/systemheroes/myhero_d24a5d30798e43d4986d3ec.cah`` has
``appearance = (0, 0)`` -- class 0 (Men of the West), subclass 0 (Captain of
Gondor) -- and its five attribute ``GroupOrder`` values are
``(15, 11, 9, 5, 7)``, which are exactly Captain of Gondor's four
``DefaultValueUpgrade`` steps ``(16, 12, 10, 6, 8)`` minus one.  So a shipped
binary agrees with the INI on both the default table and the 0-based
``GroupOrder`` convention.

FAIL-CLOSED
-----------
Every cross-reference is checked and every failure raises
:class:`CahSystemCompilerError` naming what was missing.  Nothing is defaulted
and nothing is guessed.  A descriptor this module returns is one where every
class, every subclass, every attribute step and every multiplier resolved.
"""

from __future__ import annotations

import hashlib
import json
import re
from typing import Any, Iterable, Mapping, Sequence

from .sage_ini import _lines, parse_flat_named_blocks

SCHEMA = "openbfme.cah-system-descriptor"
SCHEMA_VERSION = 0

RUNTIME_SCHEMA = "openbfme.cah-system-runtime"
RUNTIME_SCHEMA_VERSION = 0

SYSTEM_PATH = "data/ini/createaherosystem.ini"
UPGRADES_PATH = "data/ini/createaheroupgrades.inc"
MODIFIERS_PATH = "data/ini/attributemodifier.ini"
GAME_DATA_PATH = "data/ini/createaherogamedata.inc"
GLOBAL_GAME_DATA_PATH = "data/ini/gamedata.ini"
DESIGN_PATH = "data/ini/object/createahero/createaherodesign.inc"
RESPAWN_PATH = "data/ini/object/createahero/createaherorespawn.inc"

#: Documents that must be present.  The ``#include``s reached from
#: ``createaherosystem.ini`` are resolved from the same mapping and are also
#: required, but their names come from the file rather than from here.
REQUIRED_DOCUMENTS = (
    SYSTEM_PATH,
    UPGRADES_PATH,
    MODIFIERS_PATH,
    GAME_DATA_PATH,
    GLOBAL_GAME_DATA_PATH,
    DESIGN_PATH,
    RESPAWN_PATH,
)

#: Where ``#define``s are read from, in precedence order (later wins).  BOTH are
#: needed and neither is optional: pure retail's Create-a-Hero object reaches
#: for ``CAH_BUILDCOST`` and ``FARAMIR_HEALTH``, which live in the global
#: ``gamedata.ini``, while the tuning include owns ``CREATE_A_HERO_*``.
DEFINE_PATHS = (GLOBAL_GAME_DATA_PATH, GAME_DATA_PATH)

#: The base-object fields this module compiles, as ``(block field, emitted key)``.
#: Read from the design include BY REFERENCE and then resolved through the
#: defines, rather than by reaching for a define name this module picked.  That
#: distinction is load-bearing: pure retail authors ``BuildCost = CAH_BUILDCOST``
#: (500) while a patched corpus rewrites the same line to
#: ``BuildCost = CREATE_A_HERO_BUILDCOST`` (a different number).  Following the
#: reference reports whichever the corpus in hand actually uses; guessing the
#: define name would silently report a value the object never reads.
DESIGN_FIELDS = (
    ("BuildCost", "buildCost"),
    ("BuildTime", "buildTimeSeconds"),
    ("VisionRange", "visionRange"),
    ("ShroudClearingRange", "shroudClearingRange"),
    ("CommandPoints", "commandPoints"),
    ("BountyValue", "bountyValue"),
)

#: The ``BlingType`` that marks a customisation group as an attribute slider.
ATTRIBUTE_BLING_TYPE = "ATTRIBUTE"

#: Every attribute group has exactly this many steps, in every group, in both
#: games.  A group that does not is a corpus this module does not understand.
ATTRIBUTE_STEP_COUNT = 20

#: Assignment keys that open an ``End``-terminated module block rather than
#: carrying a scalar.  See the comment in :func:`_parse_blocks`.
_MODULE_KEYS = frozenset({"behavior", "behaviour", "body", "draw", "clientupdate"})

MAX_DOCUMENT_BYTES = 16 * 1024 * 1024
MAX_INCLUDE_DEPTH = 8
MAX_BLOCK_DEPTH = 16

_INCLUDE_PATTERN = re.compile(r'^#include\s+"([^"]+)"\s*$', re.IGNORECASE)
_DEFINE_PATTERN = re.compile(r"^#define\s+([A-Za-z_][A-Za-z0-9_]*)\s+(.*)$", re.IGNORECASE)
_ASSIGNMENT_PATTERN = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(.*)$")
_BLOCK_HEADER_PATTERN = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*)\s*$")
#: ``#MULTIPLY( NAME 0.62 )`` -- the only expression form the attribute ladders
#: use.  Deliberately narrow: a ladder that grows a second form should fail
#: loudly here rather than silently resolve to half of it.
_MULTIPLY_PATTERN = re.compile(
    r"^#MULTIPLY\(\s*([A-Za-z_][A-Za-z0-9_]*)\s+([-+]?[0-9]*\.?[0-9]+)\s*\)$",
    re.IGNORECASE,
)
#: An attribute upgrade name, e.g. ``Upgrade_ArmorAttribute05``.  The bare tail
#: (``ArmorAttribute05``) is the ``ModifierList`` name.
_ATTRIBUTE_UPGRADE_PATTERN = re.compile(r"^Upgrade_([A-Za-z]+Attribute)(\d\d)$")


class CahSystemCompilerError(ValueError):
    """The Create-a-Hero INI surface could not be compiled, and why."""


class _Block:
    """One ``Kind ... End`` block: ordered assignments plus child blocks."""

    __slots__ = ("kind", "assignments", "blocks")

    def __init__(self, kind: str) -> None:
        self.kind = kind
        self.assignments: list[tuple[str, str]] = []
        self.blocks: list["_Block"] = []

    def values(self, key: str) -> tuple[str, ...]:
        folded = key.casefold()
        return tuple(value for name, value in self.assignments if name.casefold() == folded)

    def value(self, key: str) -> str | None:
        found = self.values(key)
        return found[0] if found else None

    def children(self, kind: str) -> tuple["_Block", ...]:
        folded = kind.casefold()
        return tuple(block for block in self.blocks if block.kind.casefold() == folded)


def _document_lines(
    documents: Mapping[str, bytes],
    virtual_path: str,
    *,
    depth: int = 0,
    seen: tuple[str, ...] = (),
) -> list[str]:
    """Comment-stripped lines of one document with its ``#include``s spliced in.

    Includes resolve ONLY against the supplied mapping -- never the host
    filesystem -- which is the same discipline ``sage_cst.resolve_sage_documents``
    keeps, and is what lets a test drive this with a handful of literal byte
    strings.  Lookup is relative to the including document's directory first,
    then against the mapping root, both case-insensitively.
    """

    if depth > MAX_INCLUDE_DEPTH:
        raise CahSystemCompilerError(
            f"{virtual_path}: include depth exceeds {MAX_INCLUDE_DEPTH}"
        )
    if virtual_path.casefold() in seen:
        raise CahSystemCompilerError(f"{virtual_path}: include cycle")

    raw = _lookup(documents, virtual_path)
    if raw is None:
        raise CahSystemCompilerError(f"{virtual_path}: document is missing")
    if len(raw) > MAX_DOCUMENT_BYTES:
        raise CahSystemCompilerError(
            f"{virtual_path}: {len(raw)} bytes exceeds the {MAX_DOCUMENT_BYTES} limit"
        )

    directory = virtual_path.rsplit("/", 1)[0] if "/" in virtual_path else ""
    out: list[str] = []
    for stripped in _lines(raw):
        include = _INCLUDE_PATTERN.match(stripped)
        if include is None:
            out.append(stripped)
            continue
        target = include.group(1).replace("\\", "/").strip()
        candidates = [f"{directory}/{target}" if directory else target, target]
        resolved = next(
            (c for c in candidates if _lookup(documents, c) is not None), None
        )
        if resolved is None:
            raise CahSystemCompilerError(
                f"{virtual_path}: #include \"{target}\" resolves to nothing "
                f"(tried {', '.join(candidates)})"
            )
        out.extend(
            _document_lines(
                documents,
                resolved,
                depth=depth + 1,
                seen=seen + (virtual_path.casefold(),),
            )
        )
    return out


def _lookup(documents: Mapping[str, bytes], virtual_path: str) -> bytes | None:
    if virtual_path in documents:
        return documents[virtual_path]
    folded = virtual_path.replace("\\", "/").casefold()
    for key, value in documents.items():
        if key.replace("\\", "/").casefold() == folded:
            return value
    return None


def _parse_blocks(lines: Sequence[str], label: str) -> _Block:
    """Fold comment-stripped lines into a nested block tree.

    A line holding ``=`` is an assignment; a bare identifier opens a block;
    ``End`` closes one.  That is the whole grammar the Create-a-Hero surface
    uses, and anything else is a line this reader refuses rather than skips.
    """

    root = _Block("")
    stack = [root]
    for number, line in enumerate(lines, start=1):
        if line.casefold() == "end":
            if len(stack) == 1:
                raise CahSystemCompilerError(f"{label}: line {number}: unmatched End")
            stack.pop()
            continue
        assignment = _ASSIGNMENT_PATTERN.match(line)
        if assignment is not None:
            key, value = assignment.group(1), assignment.group(2).strip()
            # SAGE HAS TWO BLOCK FORMS.  ``SubClass`` opens one with a bare
            # header; ``Body = RespawnBody ModuleTag`` opens one with an
            # assignment whose value names a module class and a tag.  Both are
            # closed by ``End``.  Treating the second as a plain assignment
            # makes its body leak into the parent and leaves a stray ``End``,
            # so the module-opening keys are named explicitly.  Kept to the
            # keys this surface actually uses rather than guessing from the
            # value's shape, which would misread any two-token scalar.
            if key.casefold() in _MODULE_KEYS:
                if len(stack) > MAX_BLOCK_DEPTH:
                    raise CahSystemCompilerError(
                        f"{label}: line {number}: block depth exceeds {MAX_BLOCK_DEPTH}"
                    )
                block = _Block(key)
                block.assignments.append(("__module__", value))
                stack[-1].blocks.append(block)
                stack.append(block)
                continue
            stack[-1].assignments.append((key, value))
            continue
        header = _BLOCK_HEADER_PATTERN.match(line)
        if header is not None:
            if len(stack) > MAX_BLOCK_DEPTH:
                raise CahSystemCompilerError(
                    f"{label}: line {number}: block depth exceeds {MAX_BLOCK_DEPTH}"
                )
            block = _Block(header.group(1))
            stack[-1].blocks.append(block)
            stack.append(block)
            continue
        if line.startswith("#"):
            # A preprocessor directive other than #include (the includes were
            # spliced away upstream).  Retail's Create-a-Hero surface has none;
            # tolerating them silently would hide a corpus change.
            raise CahSystemCompilerError(
                f"{label}: line {number}: unsupported directive {line!r}"
            )
        raise CahSystemCompilerError(f"{label}: line {number}: unparsable line {line!r}")
    if len(stack) != 1:
        raise CahSystemCompilerError(
            f"{label}: {len(stack) - 1} block(s) left unterminated at end of input"
        )
    return root


def _numeric_defines(documents: Mapping[str, bytes]) -> dict[str, float]:
    """``#define NAME <number>`` pairs from every define-bearing document.

    Non-numeric defines are skipped rather than rejected: the game-data files
    carry thousands of them (object filters, armour clauses, animation names)
    and none of them is something this module reads.
    """

    defines: dict[str, float] = {}
    for virtual_path in DEFINE_PATHS:
        raw = _lookup(documents, virtual_path)
        if raw is None:
            raise CahSystemCompilerError(f"{virtual_path}: document is missing")
        for line in _lines(raw):
            match = _DEFINE_PATTERN.match(line)
            if match is None:
                continue
            try:
                defines[match.group(1)] = float(match.group(2).strip())
            except ValueError:
                continue
    return defines


def _resolved_scalar(
    token: str, defines: Mapping[str, float], label: str, field: str
) -> float:
    """A literal number, or a ``#define``d name, or a named failure."""

    token = token.strip()
    if not token:
        raise CahSystemCompilerError(f"{label}: {field} is empty")
    try:
        return float(token)
    except ValueError:
        pass
    if token in defines:
        return defines[token]
    raise CahSystemCompilerError(
        f"{label}: {field} references {token!r}, which is neither a number nor "
        f"a numeric #define in {' or '.join(DEFINE_PATHS)}"
    )


def _required_define(defines: Mapping[str, float], name: str) -> float:
    if name not in defines:
        raise CahSystemCompilerError(f"#define {name} is missing")
    return defines[name]


def _number(value: float) -> int | float:
    """Emit whole numbers as ints so the JSON reads like the INI does."""

    return int(value) if float(value).is_integer() else float(value)


def attribute_spend(attributes: Iterable[Mapping[str, Any]], key: str = "defaultStep") -> int:
    """Points a loadout spends: one per step above the class minimum.

    THE ONE PLACE THE POINT-COST RULE LIVES.  Every caller -- the compiler's own
    default-budget check, a validator, the runtime -- goes through here, so the
    rule cannot drift into two implementations that disagree.
    """

    total = 0
    for row in attributes:
        chosen = int(row[key])
        minimum = int(row["minStep"])
        if chosen < minimum:
            raise CahSystemCompilerError(
                f"{row.get('groupName', '?')}: step {chosen} is below the "
                f"authored minimum {minimum}"
            )
        total += chosen - minimum
    return total


def _attribute_groups(system: _Block) -> list[dict[str, Any]]:
    groups: list[dict[str, Any]] = []
    seen: set[str] = set()
    for binder in system.children("CreateAHeroBlingBinder"):
        bling_type = (binder.value("BlingType") or "").strip().upper()
        if bling_type != ATTRIBUTE_BLING_TYPE:
            continue
        group_name = (binder.value("GroupName") or "").strip()
        if not group_name:
            raise CahSystemCompilerError(
                f"{SYSTEM_PATH}: a CreateAHeroBlingBinder has no GroupName"
            )
        if group_name.casefold() in seen:
            raise CahSystemCompilerError(
                f"{SYSTEM_PATH}: attribute group {group_name!r} is declared twice"
            )
        seen.add(group_name.casefold())
        ui_slot = binder.value("UISlot")
        if ui_slot is None or not ui_slot.strip().isdigit():
            raise CahSystemCompilerError(
                f"{SYSTEM_PATH}: attribute group {group_name!r} has no numeric UISlot"
            )
        groups.append(
            {
                "groupName": group_name,
                "uiSlot": int(ui_slot.strip()),
                "labelStringId": (binder.value("LabelTag") or "").strip(),
                "descriptionStringId": (binder.value("DescriptionTag") or "").strip(),
            }
        )
    if not groups:
        raise CahSystemCompilerError(
            f"{SYSTEM_PATH}: no CreateAHeroBlingBinder declares "
            f"BlingType = {ATTRIBUTE_BLING_TYPE}"
        )
    groups.sort(key=lambda row: (row["uiSlot"], row["groupName"].casefold()))
    return groups


def _upgrade_index(documents: Mapping[str, bytes]) -> dict[str, tuple[str, int]]:
    """``upgradeName -> (groupName, groupOrder)`` for every grouped upgrade."""

    raw = _lookup(documents, UPGRADES_PATH)
    if raw is None:
        raise CahSystemCompilerError(f"{UPGRADES_PATH}: document is missing")
    index: dict[str, tuple[str, int]] = {}
    for block in parse_flat_named_blocks(raw, "Upgrade"):
        names = block.values("GroupName")
        order = block.values("GroupOrder")
        if not names or not order:
            continue
        group = names[0]
        text = order[0].strip()
        if not text.isdigit():
            raise CahSystemCompilerError(
                f"{UPGRADES_PATH}: Upgrade {block.name} has non-numeric "
                f"GroupOrder {text!r}"
            )
        index[block.name.casefold()] = (group.strip(), int(text))
    return index


def _modifier_index(
    documents: Mapping[str, bytes], attribute_multiplier: float
) -> dict[str, dict[str, Any]]:
    """``modifierListName -> {category, modifiers[]}`` for attribute ladders only."""

    raw = _lookup(documents, MODIFIERS_PATH)
    if raw is None:
        raise CahSystemCompilerError(f"{MODIFIERS_PATH}: document is missing")
    index: dict[str, dict[str, Any]] = {}
    for block in parse_flat_named_blocks(raw, "ModifierList"):
        if not re.fullmatch(r"[A-Za-z]+Attribute\d\d", block.name):
            continue
        modifiers: list[dict[str, Any]] = []
        for text in block.values("Modifier"):
            parts = text.split(None, 1)
            if len(parts) != 2:
                raise CahSystemCompilerError(
                    f"{MODIFIERS_PATH}: ModifierList {block.name} has an "
                    f"unreadable Modifier {text!r}"
                )
            kind, body = parts[0].strip().upper(), parts[1].strip()
            match = _MULTIPLY_PATTERN.match(body)
            if match is not None:
                value = float(match.group(2)) * attribute_multiplier
            else:
                try:
                    value = float(body)
                except ValueError as error:
                    raise CahSystemCompilerError(
                        f"{MODIFIERS_PATH}: ModifierList {block.name} modifier "
                        f"{kind} has an unresolvable value {body!r}"
                    ) from error
            modifiers.append({"kind": kind, "value": _number(value)})
        if not modifiers:
            raise CahSystemCompilerError(
                f"{MODIFIERS_PATH}: ModifierList {block.name} declares no Modifier"
            )
        category = (block.values("Category") or ("",))[0].strip().upper()
        index[block.name.casefold()] = {
            "category": category,
            "modifiers": modifiers,
        }
    return index


def _attribute_step(
    upgrade_name: str,
    group_name: str,
    upgrades: Mapping[str, tuple[str, int]],
    label: str,
) -> int:
    """Resolve an upgrade name to its 1-based step, checking every claim."""

    match = _ATTRIBUTE_UPGRADE_PATTERN.match(upgrade_name)
    if match is None:
        raise CahSystemCompilerError(
            f"{label}: {upgrade_name!r} is not an attribute upgrade name"
        )
    step = int(match.group(2))
    row = upgrades.get(upgrade_name.casefold())
    if row is None:
        raise CahSystemCompilerError(
            f"{label}: {upgrade_name} has no Upgrade in {UPGRADES_PATH}"
        )
    declared_group, group_order = row
    if declared_group.casefold() != group_name.casefold():
        raise CahSystemCompilerError(
            f"{label}: {upgrade_name} is in group {declared_group!r} but was "
            f"used for {group_name!r}"
        )
    # THE 0-BASED CONVENTION, ENFORCED RATHER THAN ASSUMED.  A `.cah` stores
    # GroupOrder; everything downstream converts with +1.  If retail ever
    # authored an upgrade whose numeric suffix and GroupOrder disagreed, that
    # conversion would be silently wrong, so it is checked here instead.
    if group_order != step - 1:
        raise CahSystemCompilerError(
            f"{label}: {upgrade_name} declares GroupOrder {group_order}; its "
            f"name says step {step}, which requires GroupOrder {step - 1}"
        )
    return step


def _sub_classes(
    class_block: _Block,
    class_index: int,
    groups: Sequence[Mapping[str, Any]],
    upgrades: Mapping[str, tuple[str, int]],
) -> list[dict[str, Any]]:
    group_names = [str(row["groupName"]) for row in groups]
    out: list[dict[str, Any]] = []
    for sub_index, sub in enumerate(class_block.children("SubClass")):
        name_tag = (sub.value("NameTag") or "").strip()
        label = f"{SYSTEM_PATH}: class {class_index} subclass {sub_index} ({name_tag or '?'})"
        budget_text = (sub.value("SpendableAttributePoints") or "").strip()
        if not budget_text.isdigit():
            raise CahSystemCompilerError(
                f"{label}: SpendableAttributePoints is missing or non-numeric"
            )
        budget = int(budget_text)

        attributes: list[dict[str, Any]] = []
        for attribute in sub.children("Attribute"):
            group_name = (attribute.value("GroupName") or "").strip()
            if group_name not in group_names:
                raise CahSystemCompilerError(
                    f"{label}: Attribute group {group_name!r} is not an "
                    f"ATTRIBUTE CreateAHeroBlingBinder"
                )
            steps = {}
            for field, key in (
                ("MinValueUpgrade", "minStep"),
                ("MaxValueUpgrade", "maxStep"),
                ("DefaultValueUpgrade", "defaultStep"),
            ):
                upgrade_name = (attribute.value(field) or "").strip()
                if not upgrade_name:
                    raise CahSystemCompilerError(
                        f"{label}: Attribute {group_name} has no {field}"
                    )
                steps[key] = _attribute_step(upgrade_name, group_name, upgrades, label)
                steps[key + "Upgrade"] = upgrade_name
            if not steps["minStep"] <= steps["defaultStep"] <= steps["maxStep"]:
                raise CahSystemCompilerError(
                    f"{label}: Attribute {group_name} authors "
                    f"min={steps['minStep']} default={steps['defaultStep']} "
                    f"max={steps['maxStep']}, which is not ordered"
                )
            attributes.append({"groupName": group_name, **steps})

        declared = {row["groupName"] for row in attributes}
        missing = [name for name in group_names if name not in declared]
        if missing:
            raise CahSystemCompilerError(
                f"{label}: no Attribute block for {', '.join(missing)}"
            )
        attributes.sort(key=lambda row: group_names.index(str(row["groupName"])))

        # THE POINT-BUDGET RULE.  See the module docstring: the authored default
        # loadout spends the whole budget, in all sixteen live subclasses.  A
        # violation means either the corpus changed or the rule was never the
        # rule, and either way emitting the budget anyway would be a lie.
        spend = attribute_spend(attributes)
        if spend != budget:
            raise CahSystemCompilerError(
                f"{label}: the authored default loadout spends {spend} points "
                f"but SpendableAttributePoints is {budget}; the one-point-per-"
                f"step-above-minimum rule does not hold for this subclass"
            )

        usable = tuple((sub.value("UsableFactions") or "").split())
        out.append(
            {
                "subClassIndex": sub_index,
                "nameStringId": name_tag,
                "descriptionStringId": (sub.value("DescriptionTag") or "").strip(),
                "upgradeName": (sub.value("UpgradeName") or "").strip(),
                "iconImageId": (sub.value("IconImage") or "").strip(),
                "buttonImageId": (sub.value("ButtonImage") or "").strip(),
                "defaultFaction": (sub.value("DefaultFaction") or "").strip(),
                "usableFactions": list(usable),
                "spendableAttributePoints": budget,
                "defaultAttributeSpend": spend,
                "attributes": attributes,
            }
        )
    if not out:
        raise CahSystemCompilerError(
            f"{SYSTEM_PATH}: class {class_index} declares no SubClass"
        )
    return out


def compile_cah_system_descriptor(documents: Mapping[str, bytes]) -> dict[str, Any]:
    """Compile the Create-a-Hero class system, or raise saying what was missing.

    ``documents`` maps posix virtual paths (relative to an effective-assets
    root) to bytes.  Only :data:`REQUIRED_DOCUMENTS` and the ``#include``s
    reached from the system file are read.
    """

    missing = [path for path in REQUIRED_DOCUMENTS if _lookup(documents, path) is None]
    if missing:
        raise CahSystemCompilerError(
            f"missing required document(s): {', '.join(sorted(missing))}"
        )

    defines = _numeric_defines(documents)
    attribute_multiplier = _required_define(defines, "CREATE_A_HERO_ATTRIBUTE_MULTIPLIER")

    root = _parse_blocks(
        _document_lines(documents, SYSTEM_PATH), SYSTEM_PATH
    )
    system_blocks = root.children("CreateAHeroSystem")
    if len(system_blocks) != 1:
        raise CahSystemCompilerError(
            f"{SYSTEM_PATH}: expected exactly one CreateAHeroSystem block, "
            f"found {len(system_blocks)}"
        )
    system = system_blocks[0]

    groups = _attribute_groups(system)
    upgrades = _upgrade_index(documents)
    modifiers = _modifier_index(documents, attribute_multiplier)

    for group in groups:
        family = str(group["groupName"]).split("_", 1)[-1]
        steps: list[dict[str, Any]] = []
        for step in range(1, ATTRIBUTE_STEP_COUNT + 1):
            upgrade_name = f"Upgrade_{family}{step:02d}"
            _attribute_step(upgrade_name, str(group["groupName"]), upgrades, SYSTEM_PATH)
            modifier = modifiers.get(f"{family}{step:02d}".casefold())
            if modifier is None:
                raise CahSystemCompilerError(
                    f"{MODIFIERS_PATH}: no ModifierList {family}{step:02d} for "
                    f"{upgrade_name}"
                )
            steps.append(
                {
                    "step": step,
                    "groupOrder": step - 1,
                    "upgradeName": upgrade_name,
                    "category": modifier["category"],
                    "modifiers": modifier["modifiers"],
                }
            )
        group["stepCount"] = ATTRIBUTE_STEP_COUNT
        group["steps"] = steps

    classes: list[dict[str, Any]] = []
    for class_index, class_block in enumerate(system.children("CreateAHeroClass")):
        classes.append(
            {
                # THE INDEX A `.cah` STORES.  `appearance[0]` in a saved hero is
                # this ordinal -- declaration order inside createaherosystem.ini,
                # i.e. the order of its #includes -- and `appearance[1]` is the
                # subclass ordinal.  Confirmed against the eight EA-shipped
                # heroes in data/systemheroes/, whose class-scoped ability
                # prefixes (HotW_, SoS_, CM_) agree with the ordinal every time.
                "classIndex": class_index,
                "nameStringId": (class_block.value("NameTag") or "").strip(),
                "descriptionStringId": (class_block.value("DescriptionTag") or "").strip(),
                "powersDescriptionStringId": (class_block.value("PowersDescTag") or "").strip(),
                "upgradeName": (class_block.value("UpgradeName") or "").strip(),
                "iconImageId": (class_block.value("IconImage") or "").strip(),
                "subClasses": _sub_classes(class_block, class_index, groups, upgrades),
            }
        )
    if not classes:
        raise CahSystemCompilerError(f"{SYSTEM_PATH}: no CreateAHeroClass is declared")

    design = _parse_blocks(_document_lines(documents, DESIGN_PATH), DESIGN_PATH)
    base_stats: dict[str, Any] = {}
    for field, key in DESIGN_FIELDS:
        token = design.value(field)
        if token is None:
            raise CahSystemCompilerError(f"{DESIGN_PATH}: {field} is not authored")
        base_stats[key] = _number(
            _resolved_scalar(token, defines, DESIGN_PATH, field)
        )
        base_stats[key + "Expression"] = token.strip()

    respawn = _parse_blocks(_document_lines(documents, RESPAWN_PATH), RESPAWN_PATH)
    bodies = [
        block
        for block in respawn.children("Body")
        if (block.value("__module__") or "").split()[:1] == ["RespawnBody"]
    ]
    if len(bodies) != 1:
        raise CahSystemCompilerError(
            f"{RESPAWN_PATH}: expected exactly one RespawnBody, found {len(bodies)}"
        )
    max_health_token = bodies[0].value("MaxHealth")
    if max_health_token is None:
        raise CahSystemCompilerError(f"{RESPAWN_PATH}: RespawnBody has no MaxHealth")
    base_stats["maxHealth"] = _number(
        _resolved_scalar(max_health_token, defines, RESPAWN_PATH, "MaxHealth")
    )
    base_stats["maxHealthExpression"] = max_health_token.strip()

    revive_costs = sorted(
        {
            int(part.split(":", 1)[1])
            for block in respawn.children("Behavior")
            for text in block.values("RespawnRules") + block.values("RespawnEntry")
            for part in text.split()
            if part.casefold().startswith("cost:") and part.split(":", 1)[1].isdigit()
        }
    )
    if len(revive_costs) != 1:
        raise CahSystemCompilerError(
            f"{RESPAWN_PATH}: expected one revive cost across every RespawnEntry, "
            f"found {revive_costs or 'none'}"
        )

    body = {
        "schema": SCHEMA,
        "schemaVersion": SCHEMA_VERSION,
        "system": {
            "objectId": "CreateAHero",
            "attributeMultiplier": _number(attribute_multiplier),
            "reviveCost": revive_costs[0],
            "commandSet": (design.value("CommandSet") or "").strip(),
            "displayNameStringId": (design.value("DisplayName") or "").strip(),
            "weaponGroupName": (system.value("WeaponGroupName") or "").strip(),
            "commandSetTemplate": (system.value("CommandSetTemplate") or "").strip(),
            **base_stats,
        },
        "attributeGroups": groups,
        "classes": classes,
        "sourceDocuments": _source_documents(documents),
        "limitations": [
            "Attribute groups only. The seven APPEARANCE CreateAHeroBlingBinder "
            "groups (helmet, shoulders, body, gauntlets, weapon, shield, boots) "
            "and their per-part upgrades are not compiled.",
            "Per-class special powers are not compiled. The purchase cost of a "
            "power is not authored anywhere in the INI corpus - createaherosystem.ini "
            "authors only SpecialPowerDiscountPerLevel - so a power-buying budget "
            "cannot be reconstructed from INI alone.",
            "Hero colours (DefaultPrimaryColor and siblings) and ViewInfo camera "
            "framing are not compiled.",
            "Experience levels are not compiled; CreateAHero uses the shared "
            "ExperienceLevel CreateAHeroLevelN templates.",
        ],
    }
    body["descriptorSha256"] = _digest(body)
    return body


def _source_documents(documents: Mapping[str, bytes]) -> list[dict[str, str]]:
    rows = []
    for path in sorted(REQUIRED_DOCUMENTS, key=str.casefold):
        raw = _lookup(documents, path)
        if raw is None:  # pragma: no cover - guarded by the caller
            continue
        rows.append({"virtualPath": path, "sha256": hashlib.sha256(raw).hexdigest()})
    return rows


def validate_cah_system_descriptor(value: Mapping[str, Any]) -> None:
    """Raise unless ``value`` is a descriptor this module produced intact."""

    if value.get("schema") != SCHEMA or value.get("schemaVersion") != SCHEMA_VERSION:
        raise CahSystemCompilerError("cah system descriptor identity is invalid")
    unsigned = dict(value)
    digest = unsigned.pop("descriptorSha256", None)
    if not isinstance(digest, str) or digest != _digest(unsigned):
        raise CahSystemCompilerError("cah system descriptor digest is invalid")
    classes = value.get("classes")
    if not isinstance(classes, list) or not classes:
        raise CahSystemCompilerError("cah system descriptor declares no classes")
    for class_row in classes:
        for sub in class_row["subClasses"]:
            if attribute_spend(sub["attributes"]) != sub["spendableAttributePoints"]:
                raise CahSystemCompilerError(
                    f"{sub['nameStringId']}: default loadout no longer spends "
                    f"the authored budget"
                )


def build_cah_system_runtime(descriptor: Mapping[str, Any]) -> dict[str, Any]:
    """The Godot-facing document: the same table, under a runtime identity.

    Kept as a separate schema from the descriptor because the descriptor carries
    importer evidence (source digests, limitations prose) that the game has no
    use for, and because Godot pins schema identity by exact equality -- one
    document serving both roles would tie a runtime bump to an evidence bump.
    """

    validate_cah_system_descriptor(descriptor)
    body = {
        "schema": RUNTIME_SCHEMA,
        "schemaVersion": RUNTIME_SCHEMA_VERSION,
        "descriptorSha256": descriptor["descriptorSha256"],
        "registration": {
            "system": descriptor["system"],
            "attributeGroups": descriptor["attributeGroups"],
            "classes": descriptor["classes"],
        },
    }
    body["runtimeSha256"] = _digest(body)
    return body


def validate_cah_system_runtime(value: Mapping[str, Any]) -> None:
    if value.get("schema") != RUNTIME_SCHEMA or value.get("schemaVersion") != RUNTIME_SCHEMA_VERSION:
        raise CahSystemCompilerError("cah system runtime identity is invalid")
    unsigned = dict(value)
    digest = unsigned.pop("runtimeSha256", None)
    if not isinstance(digest, str) or digest != _digest(unsigned):
        raise CahSystemCompilerError("cah system runtime digest is invalid")


def _canonical_bytes(value: Any) -> bytes:
    return json.dumps(
        value, sort_keys=True, separators=(",", ":"), ensure_ascii=False
    ).encode("utf-8")


def _digest(value: Any) -> str:
    return hashlib.sha256(_canonical_bytes(value)).hexdigest()


__all__ = [
    "SCHEMA",
    "SCHEMA_VERSION",
    "RUNTIME_SCHEMA",
    "RUNTIME_SCHEMA_VERSION",
    "REQUIRED_DOCUMENTS",
    "CahSystemCompilerError",
    "attribute_spend",
    "compile_cah_system_descriptor",
    "validate_cah_system_descriptor",
    "build_cah_system_runtime",
    "validate_cah_system_runtime",
]
