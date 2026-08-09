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
#: Where the POWERS screen is authored.  Retail puts the whole Create-a-Hero
#: power economy on the ``CommandButton`` -- not in the system file and not in
#: ``createaherospecialpowers.ini`` -- through four ``CreateAHeroUI*`` fields
#: that exist for no other purpose.  See :func:`_power_trees`.
COMMAND_BUTTON_PATH = "data/ini/commandbutton.ini"
#: The two halves of the 3D binding.  ``createaheromodels.inc`` carries the art
#: keyed by an opaque ``CREATE_A_HERO_NN`` model-condition flag;
#: ``createaheromodelconditionupgrades.inc`` is the only document that says
#: which class/subclass pair raises which flag.  Neither is usable alone --
#: see :func:`_model_bindings`.
MODELS_PATH = "data/ini/object/createahero/createaheromodels.inc"
MODEL_CONDITIONS_PATH = (
    "data/ini/object/createahero/createaheromodelconditionupgrades.inc"
)
#: The shared ExperienceLevel chain every created hero levels through.
EXPERIENCE_PATH = "data/ini/experiencelevels_createahero.inc"

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
    COMMAND_BUTTON_PATH,
    MODELS_PATH,
    MODEL_CONDITIONS_PATH,
    EXPERIENCE_PATH,
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
#: The ``BlingType`` that marks a cosmetic (helmet / shoulders / …) group.
APPEARANCE_BLING_TYPE = "APPEARANCE"
#: Special-power definitions for Create-a-Hero (levels, reload times).
SPECIAL_POWERS_PATH = "data/ini/createaherospecialpowers.ini"
#: Max powers a profile may store (retail's ability array is fixed-capacity 15).
MAX_POWER_SLOTS = 15

#: The ``CommandButton`` fields that make a button a Create-a-Hero power.  A
#: button carrying ``CreateAHeroUIMinimumLevel`` is on the POWERS screen; one
#: without it never is, which is the whole selection rule.
CAH_UI_LEVEL_FIELD = "CreateAHeroUIMinimumLevel"
CAH_UI_CLASSES_FIELD = "CreateAHeroUIAllowableUpgrades"
CAH_UI_PREREQUISITE_FIELD = "CreateAHeroUIPrerequisiteButtonName"
CAH_UI_COST_FIELD = "CreateAHeroUICostIfSelected"

#: The value ``CreateAHeroUIPrerequisiteButtonName`` uses to mean "this is the
#: first power in its chain".  Compared case-insensitively.
CAH_NO_PREREQUISITE = "none"

#: Every attribute group has exactly this many steps, in every group, in both
#: games.  A group that does not is a corpus this module does not understand.
ATTRIBUTE_STEP_COUNT = 20

#: Assignment keys that open an ``End``-terminated module block rather than
#: carrying a scalar.  See the comment in :func:`_parse_blocks`.
_MODULE_KEYS = frozenset(
    {
        "behavior",
        "behaviour",
        "body",
        "draw",
        "clientupdate",
        # `ModelConditionState = MOUNTED CREATE_A_HERO_00` opens a block whose
        # value is a flag list rather than a module class, but the grammar is
        # the same and the value is kept verbatim in `__module__`.
        "modelconditionstate",
    }
)

#: Block kinds whose header is two tokens (``ExperienceLevel CreateAHeroLevel1``)
#: rather than one.  An ALLOWLIST rather than a general "two bare words opens a
#: block" rule: that rule would swallow any unrecognised two-token line and turn
#: a corpus change into a silent misparse instead of the loud refusal below.
_NAMED_BLOCK_KINDS = frozenset({"experiencelevel"})

MAX_DOCUMENT_BYTES = 16 * 1024 * 1024
MAX_INCLUDE_DEPTH = 8
MAX_BLOCK_DEPTH = 16

_INCLUDE_PATTERN = re.compile(r'^#include\s+"([^"]+)"\s*$', re.IGNORECASE)
_DEFINE_PATTERN = re.compile(r"^#define\s+([A-Za-z_][A-Za-z0-9_]*)\s+(.*)$", re.IGNORECASE)
_ASSIGNMENT_PATTERN = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(.*)$")
_BLOCK_HEADER_PATTERN = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*)\s*$")
_NAMED_BLOCK_HEADER_PATTERN = re.compile(
    r"^([A-Za-z_][A-Za-z0-9_]*)\s+([A-Za-z_][A-Za-z0-9_]*)\s*$"
)
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

    __slots__ = ("kind", "name", "assignments", "blocks")

    def __init__(self, kind: str, name: str = "") -> None:
        self.kind = kind
        #: The second header token for ``Kind Name`` blocks, else "".
        self.name = name
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
        named = _NAMED_BLOCK_HEADER_PATTERN.match(line)
        if named is not None and named.group(1).casefold() in _NAMED_BLOCK_KINDS:
            if len(stack) > MAX_BLOCK_DEPTH:
                raise CahSystemCompilerError(
                    f"{label}: line {number}: block depth exceeds {MAX_BLOCK_DEPTH}"
                )
            block = _Block(named.group(1), named.group(2))
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
    return _bling_groups(system, ATTRIBUTE_BLING_TYPE, required=True)


def _appearance_groups(system: _Block) -> list[dict[str, Any]]:
    ## Cosmetic groups. Optional in synthetic fixtures that only declare
    ## ATTRIBUTE binders; real retail always ships them.
    return _bling_groups(system, APPEARANCE_BLING_TYPE, required=False)


def _bling_groups(
    system: _Block, bling_type: str, *, required: bool
) -> list[dict[str, Any]]:
    groups: list[dict[str, Any]] = []
    seen: set[str] = set()
    for binder in system.children("CreateAHeroBlingBinder"):
        declared = (binder.value("BlingType") or "").strip().upper()
        if declared != bling_type:
            continue
        group_name = (binder.value("GroupName") or "").strip()
        if not group_name:
            raise CahSystemCompilerError(
                f"{SYSTEM_PATH}: a CreateAHeroBlingBinder has no GroupName"
            )
        if group_name.casefold() in seen:
            raise CahSystemCompilerError(
                f"{SYSTEM_PATH}: {bling_type.lower()} group {group_name!r} is "
                f"declared twice"
            )
        seen.add(group_name.casefold())
        ui_slot = binder.value("UISlot")
        if ui_slot is None or not ui_slot.strip().isdigit():
            raise CahSystemCompilerError(
                f"{SYSTEM_PATH}: {bling_type.lower()} group {group_name!r} has "
                f"no numeric UISlot"
            )
        groups.append(
            {
                "groupName": group_name,
                "uiSlot": int(ui_slot.strip()),
                "labelStringId": (binder.value("LabelTag") or "").strip(),
                "descriptionStringId": (binder.value("DescriptionTag") or "").strip(),
                "blingType": bling_type,
            }
        )
    if required and not groups:
        raise CahSystemCompilerError(
            f"{SYSTEM_PATH}: no CreateAHeroBlingBinder declares "
            f"BlingType = {bling_type}"
        )
    groups.sort(key=lambda row: (row["uiSlot"], row["groupName"].casefold()))
    return groups


def _appearance_options(system: _Block) -> list[dict[str, Any]]:
    ## Every CreateAHeroBling part (helmet pieces, weapons, …) as a catalog row.
    options: list[dict[str, Any]] = []
    for bling in system.children("CreateAHeroBling"):
        upgrade = (bling.value("BlingUpgradeName") or "").strip()
        group = (bling.value("GroupName") or "").strip()
        if not upgrade or not group:
            continue
        options.append(
            {
                "upgradeName": upgrade,
                "groupName": group,
                "nameStringId": (bling.value("NameTag") or "").strip(),
                "descriptionStringId": (bling.value("DescriptionTag") or "").strip(),
            }
        )
    return options


def _special_power_index(documents: Mapping[str, bytes]) -> dict[str, dict[str, Any]]:
    """``SpecialPower`` name -> its authored enum and reload time.

    This file is the *effect* side of a power.  It carries no class binding, no
    hero level and no cost, so it cannot drive the POWERS screen on its own --
    it is joined onto the CommandButton rows in :func:`_power_trees`.
    """

    raw = _lookup(documents, SPECIAL_POWERS_PATH)
    if raw is None:
        return {}
    index: dict[str, dict[str, Any]] = {}
    for block in parse_flat_named_blocks(raw, "SpecialPower"):
        name = block.name.strip()
        if not name:
            continue
        index[name.casefold()] = {
            "enum": (block.values("Enum")[0].strip() if block.values("Enum") else ""),
            "reloadTimeMs": _optional_int(block.values("ReloadTime")),
        }
    return index


def _power_trees(
    documents: Mapping[str, bytes],
    defines: Mapping[str, float],
    class_upgrade_names: Sequence[str],
) -> list[dict[str, Any]]:
    """The POWERS screen, compiled from where retail actually authors it.

    WHERE THE POWER ECONOMY LIVES.  Not in ``createaherosystem.ini`` and not in
    ``createaherospecialpowers.ini``: every rule the CUSTOMIZE HERO POWERS
    screen enforces is on the ``CommandButton``, in four fields that exist for
    no other purpose in the corpus.

    * ``CreateAHeroUIMinimumLevel`` -- the required hero level.  Retail authors
      exactly four values (1, 3, 7, 10), which are the four columns of the
      screen's grid.  A button carrying this field IS a Create-a-Hero power; a
      button without it never is.
    * ``CreateAHeroUIAllowableUpgrades`` -- whitespace-separated
      ``Upgrade_CreateAHero_Class*`` names.  A power offered to more than one
      class lists them all, which is why this is a set and not a scalar.
    * ``CreateAHeroUIPrerequisiteButtonName`` -- the button that must already be
      selected, or ``None``.  These links are the arrows drawn between grid
      columns, and chaining them is what recovers the
      "Call Reinforcements -> Improved -> Great -> Superior" rows.
    * ``CreateAHeroUICostIfSelected`` -- what this power adds to the hero's
      build cost, as a ``#define`` name resolved through ``gamedata.ini``.  The
      screen's Build Cost is the base object cost plus the sum of these.

    A TREE IS A PREREQUISITE CHAIN, recovered rather than declared.  Retail
    names no families; it only links each power to its predecessor.  So the
    roots (``prerequisite = None``) are found first and each root's transitive
    closure becomes one tree -- one row of the grid -- ordered by required
    level.  The root's own label names the row, which is what the screen shows.

    FAIL-CLOSED like the rest of this module: a prerequisite that names no
    known button, a level retail never authored, a class upgrade no
    ``CreateAHeroClass`` declares, or a cost that resolves to neither a number
    nor a define, each raises rather than emitting a screen that would silently
    offer a power the game cannot honour.
    """

    raw = _lookup(documents, COMMAND_BUTTON_PATH)
    if raw is None:
        raise CahSystemCompilerError(f"{COMMAND_BUTTON_PATH}: document is missing")
    special_powers = _special_power_index(documents)
    known_classes = {name.casefold() for name in class_upgrade_names if name}

    rows: dict[str, dict[str, Any]] = {}
    order: list[str] = []
    for block in parse_flat_named_blocks(raw, "CommandButton"):
        levels = block.values(CAH_UI_LEVEL_FIELD)
        if not levels:
            continue
        button = block.name.strip()
        label = f"{COMMAND_BUTTON_PATH}: CommandButton {button}"
        level_text = levels[0].strip()
        if not level_text.isdigit() or int(level_text) < 1:
            raise CahSystemCompilerError(
                f"{label}: {CAH_UI_LEVEL_FIELD} is {level_text!r}, which is not a "
                f"positive hero level"
            )

        allowed: list[str] = []
        for text in block.values(CAH_UI_CLASSES_FIELD):
            for token in text.split():
                if token not in allowed:
                    allowed.append(token)
        if not allowed:
            raise CahSystemCompilerError(
                f"{label}: carries {CAH_UI_LEVEL_FIELD} but no "
                f"{CAH_UI_CLASSES_FIELD}, so no class could ever select it"
            )
        unknown = [name for name in allowed if name.casefold() not in known_classes]
        if unknown:
            raise CahSystemCompilerError(
                f"{label}: {CAH_UI_CLASSES_FIELD} names {', '.join(unknown)}, "
                f"which no CreateAHeroClass declares as its UpgradeName"
            )

        prerequisite = ""
        prerequisite_values = block.values(CAH_UI_PREREQUISITE_FIELD)
        if prerequisite_values:
            candidate = prerequisite_values[0].strip()
            if candidate and candidate.casefold() != CAH_NO_PREREQUISITE:
                prerequisite = candidate

        cost = 0
        cost_expression = ""
        cost_values = block.values(CAH_UI_COST_FIELD)
        if cost_values:
            cost_expression = cost_values[0].strip()
            cost = int(
                _resolved_scalar(cost_expression, defines, label, CAH_UI_COST_FIELD)
            )

        special_power_id = (
            block.values("SpecialPower")[0].strip()
            if block.values("SpecialPower")
            else ""
        )
        effect = special_powers.get(special_power_id.casefold(), {})
        options: list[str] = []
        for text in block.values("Options"):
            for token in text.split():
                if token not in options:
                    options.append(token)

        if button.casefold() in {name.casefold() for name in rows}:
            raise CahSystemCompilerError(
                f"{label}: a Create-a-Hero CommandButton of this name is "
                f"declared twice"
            )
        rows[button] = {
            "powerId": button,
            "specialPowerId": special_power_id,
            "specialPowerEnum": str(effect.get("enum", "")),
            "reloadTimeMs": int(effect.get("reloadTimeMs", 0)),
            "requiredHeroLevel": int(level_text),
            "prerequisitePowerId": prerequisite,
            "costIfSelected": cost,
            "costExpression": cost_expression,
            "allowedClassUpgrades": allowed,
            "commandType": (
                block.values("Command")[0].strip() if block.values("Command") else ""
            ),
            "nameStringId": (
                block.values("TextLabel")[0].strip() if block.values("TextLabel") else ""
            ),
            "descriptionStringId": (
                block.values("DescriptLabel")[0].strip()
                if block.values("DescriptLabel")
                else ""
            ),
            "buttonImageId": (
                block.values("ButtonImage")[0].strip()
                if block.values("ButtonImage")
                else ""
            ),
            "radiusCursorType": (
                block.values("RadiusCursorType")[0].strip()
                if block.values("RadiusCursorType")
                else ""
            ),
            "options": options,
        }
        order.append(button)

    if not rows:
        raise CahSystemCompilerError(
            f"{COMMAND_BUTTON_PATH}: no CommandButton carries "
            f"{CAH_UI_LEVEL_FIELD}, so the POWERS screen has nothing to offer"
        )

    by_folded = {name.casefold(): name for name in rows}
    children: dict[str, list[str]] = {name: [] for name in rows}
    roots: list[str] = []
    for name in order:
        prerequisite = str(rows[name]["prerequisitePowerId"])
        if not prerequisite:
            roots.append(name)
            continue
        resolved = by_folded.get(prerequisite.casefold())
        if resolved is None:
            raise CahSystemCompilerError(
                f"{COMMAND_BUTTON_PATH}: CommandButton {name}: "
                f"{CAH_UI_PREREQUISITE_FIELD} names {prerequisite}, which is not "
                f"a Create-a-Hero power button"
            )
        # Store the canonical spelling so a consumer can match without folding.
        rows[name]["prerequisitePowerId"] = resolved
        children[resolved].append(name)

    trees: list[dict[str, Any]] = []
    for root in roots:
        members: list[dict[str, Any]] = []
        stack = [root]
        guard = 0
        while stack:
            guard += 1
            if guard > len(rows):
                raise CahSystemCompilerError(
                    f"{COMMAND_BUTTON_PATH}: the {CAH_UI_PREREQUISITE_FIELD} "
                    f"chain rooted at {root} is cyclic"
                )
            current = stack.pop()
            members.append(rows[current])
            stack.extend(reversed(children[current]))
        members.sort(
            key=lambda row: (int(row["requiredHeroLevel"]), str(row["powerId"]))
        )
        for tier, row in enumerate(members, start=1):
            row["tier"] = tier
        allowed_any: list[str] = []
        for row in members:
            for name in row["allowedClassUpgrades"]:
                if name not in allowed_any:
                    allowed_any.append(name)
        trees.append(
            {
                "familyId": root,
                "rootPowerId": root,
                # The row label the screen shows is the first power's own label:
                # the "Call Reinforcements" row is named by its level-1 button.
                "labelStringId": str(rows[root]["nameStringId"]),
                "allowedClassUpgrades": allowed_any,
                "levels": members,
            }
        )
    trees.sort(key=lambda tree: str(tree["familyId"]).casefold())
    return trees


def _optional_int(values: Sequence[str]) -> int:
    if not values:
        return 0
    text = values[0].strip().split()[0]
    if text.isdigit():
        return int(text)
    return 0


#: The upgrade a subclass carries, e.g. ``Upgrade_CreateAHero_SubClass_0``.
_SUB_CLASS_UPGRADE_PATTERN = re.compile(
    r"^Upgrade_CreateAHero_SubClass_\d+$", re.IGNORECASE
)
#: The class upgrade, e.g. ``Upgrade_CreateAHero_ClassHeroOfTheWest``.
_CLASS_UPGRADE_PATTERN = re.compile(r"^Upgrade_CreateAHero_Class\w+$", re.IGNORECASE)
#: The opaque per-subclass model-condition flag, e.g. ``CREATE_A_HERO_07``.
_CAH_MODEL_FLAG_PATTERN = re.compile(r"^CREATE_A_HERO_\d+$")
#: The upgrade that means "the hero is posing in the Create-a-Hero screens
#: rather than standing on a battlefield".  It selects a different mesh.
MAP_MODE_UPGRADE = "Upgrade_CreateAHeroMapMode"

#: The art fields a ``ModelConditionState`` carries, as (INI field, emitted key).
_MODEL_FIELDS = (
    ("Model", "model"),
    ("Skeleton", "skeleton"),
    ("ModelAnimationPrefix", "animationPrefix"),
    ("PortraitImageName", "portraitImageId"),
    ("ButtonImageName", "buttonImageId"),
)


def _model_states(documents: Mapping[str, bytes]) -> dict[str, dict[str, Any]]:
    """``CREATE_A_HERO_NN`` flag -> the art that flag selects.

    A SAGE ``ModelConditionState`` matches on a SET of flags and the most
    specific matching set wins, so one Create-a-Hero flag can key several
    states.  Three cases, and telling them apart is the whole job:

    * ``CREATE_A_HERO_12`` alone -- the base on-foot mesh.
    * ``MOUNTED CREATE_A_HERO_12`` -- the mesh when the hero is on a horse.
      Both are real and neither replaces the other.
    * ``CREATE_A_HERO_12 INVISIBLE_STEALTH`` -- a conditional restatement (the
      Elf Archer authors one) that applies only while that other flag is up.

    The first two are the bindings a client needs to show the hero; the third is
    kept under ``conditionalStates`` rather than dropped, because dropping it
    would silently lose art, and rather than overwriting the base, because
    overwriting it would dress every Elf Archer in its stealth state.
    """

    root = _parse_blocks(_document_lines(documents, MODELS_PATH), MODELS_PATH)
    states: dict[str, dict[str, Any]] = {}
    for block in root.children("ModelConditionState"):
        conditions = (block.value("__module__") or "").split()
        flags = [token for token in conditions if _CAH_MODEL_FLAG_PATTERN.match(token)]
        if len(flags) != 1:
            # A state keyed by something other than exactly one CaH flag is not
            # a per-subclass binding (the shared damage and rubble states are
            # keyed by DAMAGED, RUBBLE and friends). Skipping is correct here;
            # the per-subclass completeness check is what fails closed.
            continue
        art: dict[str, Any] = {"conditionFlags": conditions}
        for field, key in _MODEL_FIELDS:
            art[key] = (block.value(field) or "").strip()
        art["weaponLaunchBones"] = [
            text.strip() for text in block.values("WeaponLaunchBone")
        ]
        if not art["model"]:
            raise CahSystemCompilerError(
                f"{MODELS_PATH}: the ModelConditionState for "
                f"{' '.join(conditions)} authors no Model"
            )

        entry = states.setdefault(flags[0], {})
        others = [token for token in conditions if token not in flags]
        if not others:
            variant = "onFoot"
        elif others == ["MOUNTED"]:
            variant = "mounted"
        else:
            entry.setdefault("conditionalStates", []).append(art)
            continue
        if variant in entry:
            raise CahSystemCompilerError(
                f"{MODELS_PATH}: {flags[0]} declares two {variant} "
                f"ModelConditionStates"
            )
        entry[variant] = art
    if not states:
        raise CahSystemCompilerError(
            f"{MODELS_PATH}: no ModelConditionState is keyed by a "
            f"CREATE_A_HERO_NN flag"
        )
    return states


def _model_bindings(documents: Mapping[str, bytes]) -> dict[tuple[str, str], dict[str, Any]]:
    """``(class upgrade, subclass upgrade)`` -> the meshes that pair wears.

    WHY THIS IS A JOIN AND NOT A LOOKUP.  ``createaheromodels.inc`` keys its art
    by ``CREATE_A_HERO_NN``, an opaque ordinal that appears nowhere in the
    system file and has no relationship to the class or subclass index.  The
    only document that connects the two is
    ``createaheromodelconditionupgrades.inc``, where each ``ModelConditionUpgrade``
    is ``TriggeredBy`` a class upgrade AND a subclass upgrade and raises exactly
    one flag.  So the ordinal is recovered from the trigger pair rather than
    assumed to track declaration order -- which it does not: the flags run
    0..65 across 16 subclasses because each subclass claims two (battlefield and
    creation screen), and the wizard and Olog-hai blocks interleave.

    ``Upgrade_CreateAHeroMapMode`` on the trigger list marks the creation-screen
    pose; its absence marks the battlefield one.
    """

    root = _parse_blocks(
        _document_lines(documents, MODEL_CONDITIONS_PATH), MODEL_CONDITIONS_PATH
    )
    states = _model_states(documents)
    bindings: dict[tuple[str, str], dict[str, Any]] = {}
    for block in root.children("Behavior"):
        module = (block.value("__module__") or "").split()
        if not module or module[0].casefold() != "modelconditionupgrade":
            continue
        flag = (block.value("AddConditionFlags") or "").strip()
        if not _CAH_MODEL_FLAG_PATTERN.match(flag):
            continue
        triggers: list[str] = []
        for text in block.values("TriggeredBy"):
            triggers.extend(text.split())
        class_upgrades = [t for t in triggers if _CLASS_UPGRADE_PATTERN.match(t)]
        sub_upgrades = [t for t in triggers if _SUB_CLASS_UPGRADE_PATTERN.match(t)]
        if len(class_upgrades) != 1 or len(sub_upgrades) != 1:
            raise CahSystemCompilerError(
                f"{MODEL_CONDITIONS_PATH}: the ModelConditionUpgrade raising "
                f"{flag} is triggered by {len(class_upgrades)} class and "
                f"{len(sub_upgrades)} subclass upgrade(s); exactly one of each "
                f"is what makes the flag attributable to a subclass"
            )
        entry_states = states.get(flag)
        if entry_states is None:
            raise CahSystemCompilerError(
                f"{MODEL_CONDITIONS_PATH}: {flag} is raised for "
                f"{class_upgrades[0]}/{sub_upgrades[0]} but {MODELS_PATH} "
                f"declares no ModelConditionState for it"
            )
        on_foot = entry_states.get("onFoot")
        if on_foot is None:
            raise CahSystemCompilerError(
                f"{MODELS_PATH}: {flag} declares only conditional or mounted "
                f"states; {class_upgrades[0]}/{sub_upgrades[0]} has no base "
                f"mesh to stand in"
            )
        # The base on-foot art is hoisted so a consumer reads `model` off the
        # binding directly; the situational states hang beneath it.
        art = dict(on_foot)
        if "mounted" in entry_states:
            art["mounted"] = entry_states["mounted"]
        if "conditionalStates" in entry_states:
            art["conditionalStates"] = entry_states["conditionalStates"]
        key = (class_upgrades[0].casefold(), sub_upgrades[0].casefold())
        surface = (
            "creationScreen"
            if any(t.casefold() == MAP_MODE_UPGRADE.casefold() for t in triggers)
            else "battlefield"
        )
        entry = bindings.setdefault(key, {})
        if surface in entry:
            raise CahSystemCompilerError(
                f"{MODEL_CONDITIONS_PATH}: {class_upgrades[0]}/"
                f"{sub_upgrades[0]} claims two {surface} model conditions "
                f"({entry[surface]['conditionFlag']} and {flag})"
            )
        entry[surface] = {"conditionFlag": flag, **art}
    return bindings


def _view_info(sub: _Block) -> dict[str, Any]:
    """The subclass's own camera framing for the Create-a-Hero preview.

    Retail authors four framings per subclass (far / near / close-up /
    portrait) so a Great Troll and a Wanderer are both framed head-to-toe in the
    same viewport. Emitted verbatim as floats; a screen that ignored these would
    put every hero at one distance and crop the tall ones.
    """

    blocks = sub.children("ViewInfo")
    if not blocks:
        return {}
    out: dict[str, Any] = {}
    for key, value in blocks[0].assignments:
        text = value.strip()
        try:
            out[key[0].lower() + key[1:]] = _number(float(text))
        except ValueError:
            out[key[0].lower() + key[1:]] = text
    return out


#: Modifier kinds the runtime's ExperienceLevel contract can actually apply.
#: A kind outside this set is carried as evidence under ``unsupportedModifiers``
#: rather than emitted as a modifier, because the consumer rejects the whole
#: ladder on an unknown kind -- and a rejected ladder is a hero that never
#: levels, which is a far worse failure than a named-but-unapplied bonus.
_SUPPORTED_LEVEL_MODIFIER_KINDS = frozenset(
    {"HEALTH", "DAMAGE_ADD", "DAMAGE_MULT", "SPELL_DAMAGE", "PRODUCTION"}
)


def _modifier_list_blocks(documents: Mapping[str, bytes]) -> dict[str, Any]:
    """Every ``ModifierList`` block by folded name, UNRESOLVED.

    Resolution is deliberately deferred to :func:`_resolved_modifier_list`.
    ``attributemodifier.ini`` holds well over a thousand lists for the whole
    game, authored in value forms this module has no reason to understand
    (percentages, durations, filters).  Resolving them all up front would make
    an unrelated list's spelling fail a Create-a-Hero compile -- which is
    exactly what happened to ``StandardDebuff``'s ``80%``.  Only the lists the
    level ladder actually names are ever resolved.
    """

    raw = _lookup(documents, MODIFIERS_PATH)
    if raw is None:
        raise CahSystemCompilerError(f"{MODIFIERS_PATH}: document is missing")
    return {block.name.casefold(): block for block in parse_flat_named_blocks(raw, "ModifierList")}


def _resolved_modifier_list(block: Any, defines: Mapping[str, float]) -> dict[str, Any]:
    """One named ``ModifierList``, resolved into the runtime's contract shape."""

    label = f"{MODIFIERS_PATH}: ModifierList {block.name}"
    modifiers: list[dict[str, Any]] = []
    unsupported: list[str] = []
    for text in block.values("Modifier"):
        parts = text.split(None, 1)
        if len(parts) != 2:
            continue
        kind, body = parts[0].strip().upper(), parts[1].strip()
        if kind not in _SUPPORTED_LEVEL_MODIFIER_KINDS:
            unsupported.append(f"{kind} {body}")
            continue
        match = _MULTIPLY_PATTERN.match(body)
        if match is not None:
            # A level list authored as a product resolves against the named
            # define rather than the attribute multiplier.
            value = _resolved_scalar(
                match.group(1), defines, label, "Modifier"
            ) * float(match.group(2))
        elif body.endswith("%"):
            # Retail writes multipliers as percentages in places. 80% is 0.8.
            head = body[:-1].strip()
            try:
                value = float(head) / 100.0
            except ValueError:
                unsupported.append(f"{kind} {body}")
                continue
        else:
            value = _resolved_scalar(body, defines, label, "Modifier")
        modifiers.append({"kind": kind, "value": _number(value)})
    return {
        "name": block.name,
        "category": (block.values("Category") or ("",))[0].strip().upper(),
        "modifiers": modifiers,
        "unsupportedModifiers": unsupported,
    }


def _experience_ladder(
    documents: Mapping[str, bytes], defines: Mapping[str, float]
) -> dict[str, Any]:
    """The shared level chain a created hero climbs, as authored.

    Every created hero uses the same ``CreateAHeroLevelN`` templates regardless
    of class, so this is compiled once rather than per subclass.  Thresholds are
    ``#define``s (``CREATE_A_HERO_LVL2_EXP_NEEDED`` and siblings) and are
    resolved here for the same reason build cost is: the name is not the number.
    """

    root = _parse_blocks(_document_lines(documents, EXPERIENCE_PATH), EXPERIENCE_PATH)
    modifier_lists = _modifier_list_blocks(documents)
    levels: list[dict[str, Any]] = []
    for block in root.children("ExperienceLevel"):
        label = f"{EXPERIENCE_PATH}: ExperienceLevel {block.name}"
        rank_text = (block.value("Rank") or "").strip()
        if not rank_text.isdigit():
            raise CahSystemCompilerError(f"{label}: Rank is missing or non-numeric")
        required = (block.value("RequiredExperience") or "").strip()
        if not required:
            raise CahSystemCompilerError(f"{label}: RequiredExperience is not authored")
        award = (block.value("ExperienceAward") or "").strip()
        upgrades: list[str] = []
        for text in block.values("Upgrades"):
            upgrades.extend(text.split())
        modifiers: list[dict[str, Any]] = []
        for text in block.values("AttributeModifiers"):
            for name in text.split():
                modifier_block = modifier_lists.get(name.casefold())
                if modifier_block is None:
                    raise CahSystemCompilerError(
                        f"{label}: AttributeModifiers names {name}, which has "
                        f"no ModifierList in {MODIFIERS_PATH}; the level would "
                        f"grant nothing"
                    )
                modifiers.append(_resolved_modifier_list(modifier_block, defines))
        levels.append(
            {
                "templateName": block.name,
                "rank": int(rank_text),
                "requiredExperience": _number(
                    _resolved_scalar(required, defines, label, "RequiredExperience")
                ),
                "requiredExperienceExpression": required,
                "experienceAward": (
                    _number(_resolved_scalar(award, defines, label, "ExperienceAward"))
                    if award
                    else 0
                ),
                "experienceAwardExpression": award,
                "upgrades": upgrades,
                "attributeModifiers": modifiers,
                "levelUpFx": (block.value("LevelUpFx") or "").strip(),
            }
        )
    if not levels:
        raise CahSystemCompilerError(
            f"{EXPERIENCE_PATH}: no ExperienceLevel is declared"
        )
    levels.sort(key=lambda row: int(row["rank"]))
    ranks = [int(row["rank"]) for row in levels]
    if ranks != list(range(1, len(ranks) + 1)):
        raise CahSystemCompilerError(
            f"{EXPERIENCE_PATH}: ranks are {ranks}, which is not a contiguous "
            f"1..N chain; a gap would make a level unreachable"
        )
    return {"maxLevel": ranks[-1], "initialRank": 1, "levels": levels}


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
    appearance_options: Sequence[Mapping[str, Any]],
    model_bindings: Mapping[tuple[str, str], Mapping[str, Any]],
    class_upgrade_name: str,
) -> list[dict[str, Any]]:
    group_names = [str(row["groupName"]) for row in groups]
    option_by_upgrade = {
        str(row["upgradeName"]).casefold(): row for row in appearance_options
    }
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

        awards: list[str] = []
        for text in sub.values("Awards"):
            awards.extend(part for part in text.split() if part and not part.startswith("//"))
        tracking_stats: list[str] = []
        for text in sub.values("Stats"):
            tracking_stats.extend(
                part for part in text.split() if part and not part.startswith("//")
            )

        appearance_by_group: dict[str, list[str]] = {}
        for text in sub.values("BlingUpgrades"):
            for token in text.replace("@", " ").split():
                upgrade = token.strip()
                if not upgrade or upgrade.startswith("//"):
                    continue
                option = option_by_upgrade.get(upgrade.casefold())
                group_name = (
                    str(option["groupName"]) if option is not None else "CreateAHero_Unknown"
                )
                appearance_by_group.setdefault(group_name, [])
                if upgrade not in appearance_by_group[group_name]:
                    appearance_by_group[group_name].append(upgrade)

        usable = tuple((sub.value("UsableFactions") or "").split())
        sub_upgrade = (sub.value("UpgradeName") or "").strip()
        if not sub_upgrade:
            raise CahSystemCompilerError(
                f"{label}: no UpgradeName, so no model condition can be "
                f"attributed to this subclass"
            )
        models = model_bindings.get(
            (class_upgrade_name.casefold(), sub_upgrade.casefold())
        )
        if not models:
            raise CahSystemCompilerError(
                f"{label}: no ModelConditionUpgrade in {MODEL_CONDITIONS_PATH} "
                f"is triggered by {class_upgrade_name} + {sub_upgrade}, so this "
                f"subclass has no mesh"
            )
        if "battlefield" not in models:
            raise CahSystemCompilerError(
                f"{label}: {sub_upgrade} has a creation-screen model but no "
                f"battlefield one; a hero that cannot be shown in a match is "
                f"not a hero this compiler will emit"
            )
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
                "awards": awards,
                "trackingStats": tracking_stats,
                "appearanceChoices": appearance_by_group,
                "defaultPrimaryColor": (sub.value("DefaultPrimaryColor") or "").strip(),
                "defaultSecondaryColor": (sub.value("DefaultSecondaryColor") or "").strip(),
                "defaultTertiaryColor": (sub.value("DefaultTertiaryColor") or "").strip(),
                "upgradeNameSubClass": sub_upgrade,
                "models": dict(models),
                "viewInfo": _view_info(sub),
            }
        )
    if not out:
        raise CahSystemCompilerError(
            f"{SYSTEM_PATH}: class {class_index} declares no SubClass"
        )
    return out


#: What a power carries when its effect was not compiled.  Shaped exactly like a
#: compiled one so the client never branches on presence -- an uncompiled power
#: is a power that does nothing, stated, rather than a missing key.
_UNCOMPILED_EFFECT: dict[str, Any] = {
    "effect": {"kind": "none"},
    "implementation": {
        "status": "unimplemented",
        "reason": "no SpecialPower behaviour module resolved for this button",
        "limitations": [],
    },
}


def _attach_ability_effects(
    power_catalog: list[dict[str, Any]],
    ability_effects: Mapping[str, Mapping[str, Any]],
) -> None:
    """Fold the compiled behaviour of each power onto its catalog row.

    THE EFFECT IS NOT ON THE BUTTON.  ``commandbutton.ini`` says which class may
    take a power, at what level and for what price; what the power DOES is in
    the ``SpecialPower`` behaviour modules of the ``CreateAHero`` Object.  Those
    are compiled by the same lane that compiles every retail hero's abilities
    (:func:`playable_unit_compiler.compile_create_a_hero_ability_effects`) and
    joined on here by button name.

    A power with no compiled effect keeps the neutral shape above rather than
    being dropped: it is still selectable, still priced and still gated, and the
    client reports it as not castable instead of silently pretending it fires.
    """

    for tree in power_catalog:
        for row in tree["levels"]:
            compiled = ability_effects.get(str(row["powerId"]))
            if compiled is None:
                row.update(_UNCOMPILED_EFFECT)
                continue
            row["effect"] = json.loads(json.dumps(compiled.get("effect", {"kind": "none"})))
            row["implementation"] = json.loads(
                json.dumps(compiled.get("implementation", _UNCOMPILED_EFFECT["implementation"]))
            )
            for key in ("targeting", "initiateSoundId", "unitSpecificSoundId"):
                if key in compiled:
                    row[key] = compiled[key]


def compile_cah_system_descriptor(
    documents: Mapping[str, bytes],
    *,
    ability_effects: Mapping[str, Mapping[str, Any]] | None = None,
) -> dict[str, Any]:
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
    appearance_groups = _appearance_groups(system)
    appearance_options = _appearance_options(system)
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

    model_bindings = _model_bindings(documents)

    classes: list[dict[str, Any]] = []
    for class_index, class_block in enumerate(system.children("CreateAHeroClass")):
        class_upgrade_name = (class_block.value("UpgradeName") or "").strip()
        if not class_upgrade_name:
            raise CahSystemCompilerError(
                f"{SYSTEM_PATH}: class {class_index} authors no UpgradeName"
            )
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
                "upgradeName": class_upgrade_name,
                "iconImageId": (class_block.value("IconImage") or "").strip(),
                "subClasses": _sub_classes(
                    class_block,
                    class_index,
                    groups,
                    upgrades,
                    appearance_options,
                    model_bindings,
                    class_upgrade_name,
                ),
            }
        )
    if not classes:
        raise CahSystemCompilerError(f"{SYSTEM_PATH}: no CreateAHeroClass is declared")

    # Compiled AFTER the classes, because a power's class binding is checked
    # against the UpgradeName each CreateAHeroClass actually declares rather
    # than against a list of class names this module carries.
    power_catalog = _power_trees(
        documents, defines, [str(row["upgradeName"]) for row in classes]
    )
    _attach_ability_effects(power_catalog, ability_effects or {})

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
        "appearanceGroups": appearance_groups,
        "appearanceOptions": appearance_options,
        "powerCatalog": power_catalog,
        "maxPowerSlots": MAX_POWER_SLOTS,
        "experience": _experience_ladder(documents, defines),
        "classes": classes,
        "sourceDocuments": _source_documents(documents),
        "limitations": [
            "Appearance bling is compiled as upgrade-name options per group; "
            "3D part meshes are not bound here (the client shows portrait + "
            "default hero mesh when available).",
            "Special powers are compiled from the CreateAHeroUI* fields on "
            "commandbutton.ini: class binding, required hero level, "
            "prerequisite chain and cost-if-selected all come from there, and "
            "the effect side (enum, reload time) is joined on from "
            "createaherospecialpowers.ini. A power whose SpecialPower has no "
            "block in that file still compiles, with a zero reload time.",
            "Hero colours (DefaultPrimaryColor and siblings) and ViewInfo camera "
            "framing are not compiled.",
            "Experience levels are not compiled; CreateAHero uses the shared "
            "ExperienceLevel CreateAHeroLevelN templates.",
            "Awards are listed as unlockable ids on each subclass; progress is "
            "tracked on the saved profile, not reconstructed from INI.",
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
            "appearanceGroups": descriptor.get("appearanceGroups", []),
            "appearanceOptions": descriptor.get("appearanceOptions", []),
            "powerCatalog": descriptor.get("powerCatalog", []),
            "maxPowerSlots": int(descriptor.get("maxPowerSlots", MAX_POWER_SLOTS)),
            "experience": descriptor.get("experience", {}),
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
