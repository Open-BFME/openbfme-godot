"""Compile BFME2 retail references into one runtime playable-unit descriptor.

This module is deliberately object-name agnostic.  It follows authored SAGE
references and classifies the resulting unit by capabilities.  Conversion and
pack publication remain separate pipeline stages; callers provide converted
visual bindings and resolved audio/image leaves when those stages are ready.
"""

from __future__ import annotations

from collections import defaultdict
from collections.abc import Callable, Iterable, Mapping, Sequence
from copy import deepcopy
from dataclasses import dataclass, field
import hashlib
import json
import math
import re
import threading

from .module_contracts import (
    ModuleContractError,
    compile_all_module_contracts,
    validate_module_contracts,
)
from .retail_men_damage_effects import parse_fx_lists
from .sage_cst import (
    SageAssignment,
    SageBlock,
    SageCstError,
    SageObject,
    parse_sage_body_fragment,
    parse_sage_document,
)
from .sage_ini import IniBlock, parse_flat_named_blocks
from .sage_audio import normalize_faction_voice_event


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


@dataclass(frozen=True, slots=True)
class PlayableUnitCompilerInputs:
    """Parsed effective inputs shared by a bounded batch compilation."""

    documents: Mapping[str, bytes]
    objects: Mapping[str, SageObject]
    command_sets: Mapping[str, IniBlock]
    command_buttons: Mapping[str, IniBlock]
    player_templates: Mapping[str, IniBlock]
    numeric_defines: Mapping[str, int | float]
    object_parse_errors: Mapping[str, str]
    # Lazy per-kind indexes filled during a faction batch (not part of identity).
    flat_kind_cache: dict[str, tuple[IniBlock, ...]] = field(
        default_factory=dict, hash=False, compare=False, repr=False
    )
    named_definition_cache: dict[tuple[str, str], dict[str, list[dict[str, object]]] | None] = field(
        default_factory=dict, hash=False, compare=False, repr=False
    )
    cache_lock: threading.Lock = field(
        default_factory=threading.Lock, hash=False, compare=False, repr=False
    )




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


def _object_index(
    documents: Mapping[str, bytes], parse_errors: dict[str, str] | None = None
) -> dict[str, SageObject]:
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
        except SageCstError as document_error:
            if parse_errors is not None:
                parse_errors[normalized.casefold()] = str(document_error)
            # A malformed unrelated retail Object must not erase every valid
            # sibling in a large document. Parse bounded top-level slices and
            # retain only slices which are independently well formed.
            lines = source.splitlines(keepends=True)
            starts = [
                index
                for index, line in enumerate(lines)
                if re.match(rb"^(?:Object|ChildObject)\s+", line.lstrip())
            ]
            recovered: list[SageObject] = []
            for ordinal, start in enumerate(starts):
                stop = starts[ordinal + 1] if ordinal + 1 < len(starts) else len(lines)
                try:
                    fragment = parse_sage_document(
                        (b"\n" * start) + b"".join(lines[start:stop]),
                        normalized,
                    ).objects
                except SageCstError:
                    continue
                if len(fragment) == 1:
                    recovered.append(fragment[0])
            parsed = tuple(recovered)
        for item in parsed:
            key = item.name.casefold()
            # SAGE retail semantic for a re-declared Object/ChildObject of the
            # same name is last-declaration-wins.  In the retail engine
            # (ThingFactory::parseObjectDefinition) a duplicate ChildObject
            # re-enters the reskin path: copyFrom(parent) resets the template to
            # its stated parent (a full ``*this = *that`` overwrite, discarding
            # the earlier body) before the new body is applied; the DEBUG_CRASH
            # guard is compiled out of retail release builds.  RotWK ships this
            # incremental-override pattern (e.g. UAFireDrakeLairHole is declared
            # twice in data/ini/object/neutral/holes.ini).  BFME2 1.06 has no
            # duplicate object names, so this branch is never taken there and
            # its resolved output is unchanged.  Keep the final declaration.
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


# ---------------------------------------------------------------------------
# GameData define-expression evaluator.
#
# Measured over both effective gamedata corpora (BFME2 1.06 and RotWK 2.01):
# every non-literal numeric define is built from exactly three SAGE macro
# functions — #ADD, #MULTIPLY, #DIVIDE — always binary, nested at most two
# deep, chained through at most two intermediate defines, with no cycles and
# no percent literals inside expressions.  #SUBTRACT is the fourth member of
# the same SAGE macro family (unobserved in either corpus) and shares the
# binary shape.  Anything outside that grammar fails closed: unknown
# identifiers, unknown functions, non-binary arities, division by zero, and
# define cycles all resolve to None so the consuming contract raises its own
# descriptive error instead of guessing.
# ---------------------------------------------------------------------------

_DEFINE_LINE_PATTERN = re.compile(
    rb"(?m)^[ \t]*#define[ \t]+([A-Za-z_][A-Za-z0-9_]*)[ \t]+([^\r\n]*?)[ \t]*\r?$"
)
_NUMERIC_LITERAL_PATTERN = re.compile(r"-?(?:[0-9]+(?:\.[0-9]*)?|\.[0-9]+)\Z")
_PERCENT_LITERAL_PATTERN = re.compile(r"(-?(?:[0-9]+(?:\.[0-9]*)?|\.[0-9]+))%\Z")
_IDENTIFIER_PATTERN = re.compile(r"[A-Za-z_][A-Za-z0-9_]*\Z")
_EXPRESSION_TOKEN_PATTERN = re.compile(r"#[A-Za-z_]+\(|\)|[^()\s]+")
_EXPRESSION_FUNCTIONS = frozenset({"add", "subtract", "multiply", "divide"})
_MAX_DEFINE_EXPRESSION_TOKENS = 64

# Percent-valued defines (``#define LEVEL_MULT_BONUS_DMG_2 110%``) carry
# percent semantics only: SAGE substitutes the authored ``NNN%`` text at the
# use site and the field parser scales it, so they must never resolve in a
# plain numeric context.  They are admitted under this reserved suffix —
# identifiers can never contain ``%`` — and only the percent-aware
# ModifierList magnitude path consults that namespace.
_PERCENT_KEY_SUFFIX = "%"


def _stripped_define_body(raw: str) -> str:
    for marker in ("//", ";"):
        index = raw.find(marker)
        if index >= 0:
            raw = raw[:index]
    return raw.strip()


def _evaluated_expression_node(
    tokens: Sequence[str],
    position: int,
    bodies: Mapping[str, str],
    stack: tuple[str, ...],
) -> tuple[float | None, int]:
    """Evaluate one expression node; (None, len(tokens)) poisons the parse."""

    token = tokens[position]
    if token.startswith("#") and token.endswith("("):
        function = token[1:-1].casefold()
        if function not in _EXPRESSION_FUNCTIONS:
            return None, len(tokens)
        position += 1
        operands: list[float] = []
        while position < len(tokens) and tokens[position] != ")":
            value, position = _evaluated_expression_node(
                tokens, position, bodies, stack
            )
            if value is None:
                return None, len(tokens)
            operands.append(value)
        if position >= len(tokens) or len(operands) != 2:
            return None, len(tokens)
        left, right = operands
        if function == "add":
            result = left + right
        elif function == "subtract":
            result = left - right
        elif function == "multiply":
            result = left * right
        else:
            if right == 0.0:
                return None, len(tokens)
            result = left / right
        return result, position + 1
    if _NUMERIC_LITERAL_PATTERN.fullmatch(token):
        return float(token), position + 1
    if _IDENTIFIER_PATTERN.fullmatch(token):
        key = token.casefold()
        if key in stack:
            return None, len(tokens)
        referenced = bodies.get(key)
        if referenced is None:
            return None, len(tokens)
        value = _evaluated_define_body(referenced, bodies, stack + (key,))
        if value is None:
            return None, len(tokens)
        return float(value), position + 1
    return None, len(tokens)


def _evaluated_define_body(
    body: str,
    bodies: Mapping[str, str],
    stack: tuple[str, ...] = (),
) -> int | float | None:
    """Resolve one define body to a number, fail-closed on everything else.

    SAGE's define scanner consumes exactly one value unit — a single token,
    or one balanced ``#FUNCTION( ... )`` group — and ignores trailing prose
    (retail evidence: ``#define AWARD_BASE_RING_HERO 120  (with 110% award
    increase)`` feeds MordorSauron's award chain).  The evaluator mirrors
    that: only the first value unit is read.
    """

    text = body.strip()
    if not text.startswith("#"):
        parts = text.split(None, 1)
        if not parts:
            return None
        token = parts[0]
        if _NUMERIC_LITERAL_PATTERN.fullmatch(token):
            return float(token) if "." in token else int(token)
        if _IDENTIFIER_PATTERN.fullmatch(token):
            key = token.casefold()
            if key in stack:
                return None
            referenced = bodies.get(key)
            if referenced is None:
                return None
            return _evaluated_define_body(referenced, bodies, stack + (key,))
        return None
    tokens = _EXPRESSION_TOKEN_PATTERN.findall(text)
    if not tokens or len(tokens) > _MAX_DEFINE_EXPRESSION_TOKENS:
        return None
    value, position = _evaluated_expression_node(tokens, 0, bodies, stack)
    if value is None or position > len(tokens):
        return None
    return int(value) if float(value).is_integer() else float(value)


def _numeric_defines(documents: Mapping[str, bytes]) -> dict[str, int | float]:
    """Numeric constants from the effective INI/include document set.

    Retail object modules consume constants declared outside ``gamedata.ini``
    (notably ``createaherogamedata.inc``), so the complete effective define set
    is the conversion authority.
    """

    bodies: dict[str, str] = {}
    occurrences: list[tuple[str, str, str]] = []
    for path, payload in documents.items():
        normalized = path.replace("\\", "/").casefold()
        if not normalized.startswith("data/ini/") or not normalized.endswith(
            (".ini", ".inc")
        ):
            continue
        for match in _DEFINE_LINE_PATTERN.finditer(payload):
            name = match.group(1).decode("ascii")
            body = _stripped_define_body(match.group(2).decode("latin-1"))
            key = name.casefold()
            bodies.setdefault(key, body)
            occurrences.append((name, key, body))
    result: dict[str, int | float] = {}
    for name, key, body in occurrences:
        parts = body.split(None, 1)
        percent_match = (
            _PERCENT_LITERAL_PATTERN.fullmatch(parts[0]) if parts else None
        )
        if percent_match:
            scaled = float(percent_match.group(1)) / 100.0
            percent_key = key + _PERCENT_KEY_SUFFIX
            if percent_key in result and result[percent_key] != scaled:
                raise PlayableUnitCompilerError(
                    f"ambiguous percent GameData constant: {name}"
                )
            result[percent_key] = scaled
            continue
        value = _evaluated_define_body(body, bodies)
        if value is None:
            continue
        if key in result and result[key] != value:
            raise PlayableUnitCompilerError(
                f"ambiguous numeric GameData constant: {name}"
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


_MULTIPLICATIVE_PATTERN = re.compile(
    r"#MULTIPLY\(\s*([^()\s]+)\s+(-?(?:[0-9]+(?:\.[0-9]*)?|\.[0-9]+))\s*\)",
    re.IGNORECASE,
)


def _resolved_multiplicative_expression(
    expression: str, constants: Mapping[str, int | float]
) -> int | float | None:
    """Resolve one numeric expression, including authored #MULTIPLY factors."""

    match = _MULTIPLICATIVE_PATTERN.fullmatch(expression.strip())
    if match is None:
        return _resolved_expression(expression, constants)
    base = _resolved_expression(match.group(1), constants)
    if base is None:
        return None
    value = float(base) * float(match.group(2))
    return int(value) if value.is_integer() else value


def _resolved_percent_expression(
    expression: str, constants: Mapping[str, int | float]
) -> int | float | None:
    """Resolve one percent-suffixed expression (``100.0%``) to its number."""

    token = expression.strip()
    if token.endswith("%"):
        token = token[:-1].strip()
    return _resolved_expression(token, constants)


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


def _effective_primary_body(
    ancestry: Sequence[SageObject],
) -> tuple[SageBlock, object] | None:
    bodies = [
        block
        for block in _effective_top_blocks(ancestry)
        if (block.header_key or "").casefold() == "body"
    ]
    values = [
        (block, assignment)
        for block in bodies
        for assignment in block.assignments
        if assignment.key.casefold() == "maxhealth"
    ]
    if len(values) != 1:
        return None
    return values[0]


def _effective_body_health(
    ancestry: Sequence[SageObject], constants: Mapping[str, int | float]
) -> dict[str, object] | None:
    primary = _effective_primary_body(ancestry)
    if primary is None:
        return None
    _block, assignment = primary
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
        if conditions and not all(
            _is_default_set_condition(value) for value in conditions
        ):
            continue
        candidates.append(block)
    return candidates[0] if len(candidates) == 1 else None


def _is_default_set_condition(value: str) -> bool:
    """SAGE set-condition semantics: a positive token requires that flag to be
    set (an alternate stance), while a ``-`` prefix requires it to be *unset*.
    A set whose conditions carry no positive requirement beyond ``None`` /
    ``SET_NORMAL`` is therefore the base stance (Rohirrim author their default
    spear set as ``Conditions = -WEAPONSET_TOGGLE_1``)."""
    positives = [
        token for token in _tokens(value.casefold()) if not token.startswith("-")
    ]
    return all(token in {"none", "set_normal"} for token in positives)


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
            target = tokens[-1]
            # SAGE uses ``None`` as an explicit empty weapon slot. It is not a
            # Weapon definition and must not become a converter dependency.
            if target.casefold() in {"none", "null"}:
                continue
            candidates.append(target)
            if any(token.casefold() == "primary" for token in tokens[:-1]):
                primary_candidates.append(target)
    if primary_candidates:
        candidates = primary_candidates
    unique = {value.casefold(): value for value in candidates}
    return next(iter(unique.values())) if len(unique) == 1 else None


_WEAPON_SLOT_NAMES = frozenset({"primary", "secondary", "tertiary"})


def _weapon_slot_for_target(block: SageBlock, weapon_id: str) -> str | None:
    """Return the unique authored slot carrying ``weapon_id`` in a WeaponSet."""

    slots: dict[str, str] = {}
    for assignment in block.assignments:
        if assignment.key.casefold() != "weapon":
            continue
        tokens = _tokens(assignment.value)
        if not tokens or tokens[-1].casefold() != weapon_id.casefold():
            continue
        authored = [
            token
            for token in tokens[:-1]
            if token.casefold() in _WEAPON_SLOT_NAMES
        ]
        if len(authored) != 1:
            return None
        slots[authored[0].casefold()] = authored[0].upper()
    return next(iter(slots.values())) if len(slots) == 1 else None


def _default_weapon_slot(
    ancestry: Sequence[SageObject], weapon_id: str
) -> str | None:
    block = _default_set_block(ancestry, "WeaponSet")
    return _weapon_slot_for_target(block, weapon_id) if block is not None else None


def _permanent_weapon_locks(
    ancestry: Sequence[SageObject], default_weapon_id: str | None
) -> list[dict[str, object]]:
    """Compile the complete retail ``LockWeaponCreate`` corpus.

    BFME2 and RotWK author this module only with ``SlotToLock = PRIMARY``.
    Missing/ambiguous slots and any wider slot vocabulary fail closed rather
    than silently claiming support the current oracle/corpus does not provide.
    """

    modules = [
        block
        for block in _effective_top_blocks(ancestry)
        if (block.header_key or "").casefold() == "behavior"
        and block.kind.casefold() == "lockweaponcreate"
    ]
    if not modules:
        return []
    if len(modules) != 1:
        raise PlayableUnitCompilerError(
            "Object has multiple effective LockWeaponCreate modules"
        )
    block = modules[0]
    rows = [
        row
        for row in block.assignments
        if row.key.casefold() == "slottolock"
    ]
    if len(rows) != 1 or len(_tokens(rows[0].value)) != 1:
        raise PlayableUnitCompilerError(
            "LockWeaponCreate must author exactly one SlotToLock"
        )
    slot = _tokens(rows[0].value)[0].upper()
    if slot != "PRIMARY":
        raise PlayableUnitCompilerError(
            f"LockWeaponCreate slot is outside the retail corpus: {slot}"
        )
    if (
        default_weapon_id is None
        or _default_weapon_slot(ancestry, default_weapon_id) != slot
    ):
        raise PlayableUnitCompilerError(
            "LockWeaponCreate PRIMARY has no unique default PRIMARY weapon"
        )
    return [
        {
            "slot": slot,
            "state": "LOCKED_PERMANENTLY",
            "module": "LockWeaponCreate",
            "sourceIni": rows[0].source_virtual_path,
            "line": rows[0].line,
        }
    ]


def _experience_level_create(
    ancestry: Sequence[SageObject],
) -> dict[str, object] | None:
    """Compile the exact retail ``ExperienceLevelCreate`` shape."""

    modules = [
        block
        for block in _effective_top_blocks(ancestry)
        if (block.header_key or "").casefold() == "behavior"
        and block.kind.casefold() == "experiencelevelcreate"
    ]
    if not modules:
        return None
    if len(modules) != 1:
        raise PlayableUnitCompilerError(
            "Object has multiple effective ExperienceLevelCreate modules"
        )
    block = modules[0]
    fields: dict[str, SageAssignment] = {}
    for row in block.assignments:
        folded = row.key.casefold()
        if folded not in {"leveltogrant", "mponly"} or folded in fields:
            raise PlayableUnitCompilerError(
                "ExperienceLevelCreate must author exactly LevelToGrant and MPOnly"
            )
        fields[folded] = row
    if set(fields) != {"leveltogrant", "mponly"}:
        raise PlayableUnitCompilerError(
            "ExperienceLevelCreate must author exactly LevelToGrant and MPOnly"
        )
    level_token = fields["leveltogrant"].value.strip()
    if re.fullmatch(r"[1-9][0-9]*", level_token) is None:
        raise PlayableUnitCompilerError(
            "ExperienceLevelCreate LevelToGrant must be a positive integer"
        )
    mp_tokens = _tokens(fields["mponly"].value)
    if len(mp_tokens) != 1 or mp_tokens[0].casefold() != "no":
        raise PlayableUnitCompilerError(
            "ExperienceLevelCreate MPOnly is outside the retail MPOnly = No corpus"
        )
    return {
        "rank": int(level_token),
        "mpOnly": False,
        "module": "ExperienceLevelCreate",
        "sourceIni": block.source_virtual_path,
        "line": block.line,
    }


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


def _flat_blocks_for_kind(
    documents: Mapping[str, bytes],
    kind: str,
    cache: dict[str, tuple[IniBlock, ...]] | None = None,
    *,
    cache_lock: threading.Lock | None = None,
) -> tuple[IniBlock, ...]:
    """Parse every document for one flat INI kind once per prepared batch."""

    key = kind.casefold()
    if cache is not None:
        lock = cache_lock or threading.Lock()
        with lock:
            if key in cache:
                return cache[key]
    blocks: list[IniBlock] = []
    for payload in documents.values():
        try:
            blocks.extend(parse_flat_named_blocks(payload, kind))
        except (UnicodeDecodeError, ValueError):
            continue
    packed = tuple(blocks)
    if cache is not None:
        lock = cache_lock or threading.Lock()
        with lock:
            cache.setdefault(key, packed)
            return cache[key]
    return packed


def _named_definition_values(
    documents: Mapping[str, bytes],
    kind: str,
    identifier: str,
    *,
    cache: dict[tuple[str, str], dict[str, list[dict[str, object]]] | None] | None = None,
    cache_lock: threading.Lock | None = None,
) -> dict[str, list[dict[str, object]]] | None:
    cache_key = (kind.casefold(), identifier.casefold())
    if cache is not None:
        lock = cache_lock or threading.Lock()
        with lock:
            if cache_key in cache:
                return cache[cache_key]
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
        result = None
    else:
        semantic = {_digest(value): value for value in matches}
        result = next(iter(semantic.values())) if len(semantic) == 1 else None
    if cache is not None:
        lock = cache_lock or threading.Lock()
        with lock:
            cache.setdefault(cache_key, result)
            return cache[cache_key]
    return result


def _default_nested_target(
    documents: Mapping[str, bytes],
    kind: str,
    identifier: str,
    field: str,
    *,
    flat_kind_cache: dict[str, tuple[IniBlock, ...]] | None = None,
    cache_lock: threading.Lock | None = None,
) -> str | None:
    candidates: dict[str, str] = {}
    blocks = _flat_blocks_for_kind(
        documents, kind, flat_kind_cache, cache_lock=cache_lock
    )
    for block in blocks:
        if block.name.casefold() != identifier.casefold():
            continue
        values = [
            value
            for value in (_first((row,)) for row in block.values(field))
            if value
        ]
        if len(values) == 1:
            candidates[values[0].casefold()] = values[0]
    return next(iter(candidates.values())) if len(candidates) == 1 else None


def _weapon_damage_nuggets(
    documents: Mapping[str, bytes],
    identifier: str,
    *,
    nugget_kind: str = "damagenugget",
    cache: dict[tuple[str, str], dict[str, list[dict[str, object]]] | None] | None = None,
    cache_lock: threading.Lock | None = None,
) -> list[Mapping[str, object]] | None:
    """Authored nugget sub-blocks of one kind on one named Weapon definition."""

    cache_key = (f"weapon-nugget:{nugget_kind}", identifier.casefold())
    if cache is not None:
        lock = cache_lock or threading.Lock()
        with lock:
            if cache_key in cache:
                return cache[cache_key]
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
                    if depth == 1 and clean.casefold() == nugget_kind
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
                    assert isinstance(fields, defaultdict)
                    fields[key.casefold()].append(
                        {
                            "expression": expression,
                            "sourceIni": path.replace("\\", "/"),
                            "line": line_number,
                        }
                    )
    if not matches:
        result = None
    else:
        semantic = {_digest(value): value for value in matches}
        result = next(iter(semantic.values())) if len(semantic) == 1 else None
    if cache is not None:
        lock = cache_lock or threading.Lock()
        with lock:
            cache.setdefault(cache_key, result)
            return cache[cache_key]
    return result


def _base_weapon_damage(
    documents: Mapping[str, bytes],
    weapon_id: str,
    constants: Mapping[str, int | float],
    *,
    cache: dict[tuple[str, str], dict[str, list[dict[str, object]]] | None] | None = None,
    cache_lock: threading.Lock | None = None,
) -> dict[str, object] | None:
    """Aggregate a weapon's base authored DamageNugget damage.

    Retail hero melee weapons and upgrade-gated horde weapons carry damage in
    DamageNugget sub-blocks rather than one flat Damage row.  The base
    (level-1) total sums every nugget that is not locked behind an upgrade or
    restricted to a target filter; upgrade and filtered nuggets are recorded
    as exclusions instead of guessed at.
    """

    nuggets = _weapon_damage_nuggets(
        documents, weapon_id, cache=cache, cache_lock=cache_lock
    )
    if not nuggets:
        return None
    components: list[dict[str, object]] = []
    excluded: list[dict[str, object]] = []
    for nugget in nuggets:
        fields = nugget["fields"]
        assert isinstance(fields, Mapping)
        exclusion = (
            "required-upgrade"
            if fields.get("requiredupgradenames")
            else "special-object-filter"
            if fields.get("specialobjectfilter")
            else ""
        )
        damage = _resolved_definition_field(
            fields,
            "Damage",
            constants,
            resolve=_resolved_multiplicative_expression,
        )
        if exclusion:
            row: dict[str, object] = {"reason": exclusion, "line": nugget["line"]}
            if damage is not None:
                row["damage"] = damage
            excluded.append(row)
            continue
        if damage is None:
            return None
        component = dict(damage)
        damage_types = {
            str(value.get("expression", "")).casefold(): str(
                value.get("expression", "")
            )
            for value in fields.get("damagetype", ())
            if str(value.get("expression", ""))
        }
        if len(damage_types) == 1:
            component["damageType"] = next(iter(damage_types.values()))
        components.append(component)
    if not components:
        return None
    result: dict[str, object] = {
        "value": sum(component["value"] for component in components),
        "semantic": (
            "base authored DamageNugget damage total "
            "(level-1 unupgraded, unfiltered components)"
        ),
        "components": components,
    }
    if excluded:
        result["excludedNuggets"] = excluded
    return result


def _typed_damage_components(damage: object) -> list[dict[str, object]] | None:
    """Per-nugget (damageType, value) rows of an aggregated damage block.

    None when the block is not a nugget aggregate or a component carries no
    resolvable value -- callers then leave the damage untyped rather than
    guessing.  A nugget with no authored DamageType keeps an empty type: the
    runtime resolves that component against the victim's DEFAULT armor column,
    which is what an untyped retail nugget does.
    """

    if not isinstance(damage, Mapping):
        return None
    components = damage.get("components")
    if not isinstance(components, Sequence) or isinstance(components, (str, bytes)):
        return None
    rows: list[dict[str, object]] = []
    for component in components:
        if not isinstance(component, Mapping):
            return None
        value = component.get("value")
        if not isinstance(value, (int, float)) or isinstance(value, bool):
            return None
        rows.append(
            {"damageType": str(component.get("damageType", "")), "value": value}
        )
    return rows or None


def _apply_nugget_damage_types(combat: dict[str, object]) -> None:
    """Type a weapon whose DamageType rides its DamageNuggets, not a flat row.

    Retail multi-nugget weapons (ArwenSword: HERO ARWEN_DAMAGE plus SLASH 20)
    author no Weapon-level DamageType at all.  Summing those nuggets into one
    untyped lump made the whole hit resolve against the victim's DEFAULT armor
    column -- for Arwen into RivendellLancerArmor that is 200 damage where
    retail intends 180*HERO + 20*SLASH.

    One authored type across every component wins outright.  A genuine mix is
    published as ``damageComponents`` so the runtime can weight each component
    against its own armor column; no single ``damageType`` is claimed for it,
    because none is authored.  Nothing is invented either way.
    """

    rows = _typed_damage_components(combat.get("damage"))
    if rows is None:
        return
    authored = {str(row["damageType"]) for row in rows if row["damageType"]}
    if not authored:
        return
    if len(authored) == 1 and all(row["damageType"] for row in rows):
        combat["damageType"] = next(iter(authored))
        combat["damageTypeSemantic"] = (
            "the weapon authors no flat DamageType; every base DamageNugget "
            "authors the same type"
        )
        return
    combat["damageComponents"] = rows
    combat["damageComponentsSemantic"] = (
        "the weapon authors no flat DamageType and its base DamageNuggets "
        "author different types; each component resolves against its own "
        "armor column (an untyped component falls to DEFAULT)"
    )


def _hero_secondary_weapon_target(ancestry: Sequence[SageObject]) -> str | None:
    """Hero standard weapon when the default set reserves PRIMARY for powers."""

    block = _default_set_block(ancestry, "WeaponSet")
    if block is None:
        return None
    restricted_slots = {
        tokens[0].casefold()
        for row in block.assignments
        if row.key.casefold() == "onlyagainst"
        for tokens in [_tokens(row.value)]
        if tokens
    }
    candidates: list[str] = []
    for row in block.assignments:
        if row.key.casefold() != "weapon":
            continue
        tokens = _tokens(row.value)
        if len(tokens) < 2:
            continue
        slots = {token.casefold() for token in tokens[:-1]}
        if slots & restricted_slots:
            continue
        candidates.append(tokens[-1])
    unique = {value.casefold(): value for value in candidates}
    return next(iter(unique.values())) if len(unique) == 1 else None


def _resolved_definition_field(
    definition: Mapping[str, Sequence[Mapping[str, object]]] | None,
    field: str,
    constants: Mapping[str, int | float],
    *,
    resolve: Callable[[str, Mapping[str, int | float]], int | float | None] = (
        _resolved_expression
    ),
) -> dict[str, object] | None:
    if definition is None:
        return None
    rows = definition.get(field.casefold(), ())
    resolved: list[dict[str, object]] = []
    for row in rows:
        expression = str(row.get("expression", ""))
        value = resolve(expression, constants)
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
    *,
    flat_kind_cache: dict[str, tuple[IniBlock, ...]] | None = None,
    named_definition_cache: dict[
        tuple[str, str], dict[str, list[dict[str, object]]] | None
    ]
    | None = None,
    cache_lock: threading.Lock | None = None,
    hero: bool = False,
    game: str = "bfme2",
    destroy_die_policies: Sequence[Mapping[str, object]] = (),
    module_contracts: Sequence[Mapping[str, object]] = (),
    slow_death_fades: Sequence[Mapping[str, object]] = (),
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
        primary_body = _effective_primary_body(member_lineage)
        if (
            primary_body is not None
            and primary_body[0].kind.casefold() == "highlanderbody"
        ):
            resolved["highlanderBody"] = {
                "value": True,
                "module": primary_body[0].kind,
                "sourceIni": primary_body[0].source_virtual_path,
                "line": primary_body[0].line,
            }
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
        _named_definition_values(
            documents,
            "Locomotor",
            locomotor_id,
            cache=named_definition_cache,
            cache_lock=cache_lock,
        )
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
    if weapon_id is None and hero:
        # Hero sets that reserve PRIMARY for special powers author the standard
        # attack on an unrestricted secondary slot (retail Drogoth).
        weapon_id = _hero_secondary_weapon_target(member_lineage)
    weapon = (
        _named_definition_values(
            documents,
            "Weapon",
            weapon_id,
            cache=named_definition_cache,
            cache_lock=cache_lock,
        )
        if weapon_id
        else None
    )
    if weapon_id and weapon is not None:
        combat: dict[str, object] = {"weaponId": weapon_id}
        weapon_slot = _default_weapon_slot(member_lineage, weapon_id)
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
            field = _resolved_definition_field(weapon, source_name, constants)
            if field is None and hero:
                field = _resolved_definition_field(
                    weapon,
                    source_name,
                    constants,
                    resolve=_resolved_multiplicative_expression,
                )
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
                documents,
                "Weapon",
                weapon_id,
                "WarheadTemplateName",
                flat_kind_cache=flat_kind_cache,
                cache_lock=cache_lock,
            )
        )
        if warhead_id:
            warhead = _named_definition_values(
                documents,
                "Weapon",
                warhead_id,
                cache=named_definition_cache,
                cache_lock=cache_lock,
            )
            if warhead is not None:
                damage_owner = warhead
                combat["warheadId"] = warhead_id
                damage = _resolved_definition_field(warhead, "Damage", constants)
                if damage is not None:
                    combat["damage"] = damage
        if "damage" not in combat:
            # Retail hero melee weapons and upgrade-gated horde weapons author
            # damage as DamageNugget sub-blocks; aggregate the base components
            # instead of failing as an unexplained gap.  Projectile weapons
            # carry those nuggets on the warhead, which takes precedence just
            # like a flat warhead Damage row.
            nugget_damage = (
                _base_weapon_damage(
                    documents,
                    warhead_id,
                    constants,
                    cache=named_definition_cache,
                    cache_lock=cache_lock,
                )
                if warhead_id
                else None
            )
            if nugget_damage is None:
                nugget_damage = _base_weapon_damage(
                    documents,
                    weapon_id,
                    constants,
                    cache=named_definition_cache,
                    cache_lock=cache_lock,
                )
            if nugget_damage is not None:
                combat["damage"] = nugget_damage
        projectiles = {
            str(row.get("expression", "")).casefold(): str(row.get("expression", ""))
            for row in weapon.get("projectiletemplatename", ())
            if str(row.get("expression", ""))
        }
        projectile_id = (
            next(iter(projectiles.values()))
            if len(projectiles) == 1
            else _default_nested_target(
                documents,
                "Weapon",
                weapon_id,
                "ProjectileTemplateName",
                flat_kind_cache=flat_kind_cache,
                cache_lock=cache_lock,
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
        elif not unique_damage_types:
            # No flat Weapon-level DamageType row: the type rides the
            # DamageNuggets the damage total was aggregated from.
            _apply_nugget_damage_types(combat)
        for output_name, source_name in (
            ("clipSize", "ClipSize"),
            ("clipReloadTimeMs", "ClipReloadTime"),
            ("continuousFireOne", "ContinuousFireOne"),
            ("continuousFireCoastMs", "ContinuousFireCoast"),
        ):
            field = _resolved_definition_field(weapon, source_name, constants)
            if field is None and hero:
                field = _resolved_definition_field(
                    weapon,
                    source_name,
                    constants,
                    resolve=_resolved_multiplicative_expression,
                )
            if field is not None:
                combat[output_name] = field
        if "delayBetweenShotsMs" not in combat and not weapon.get("delaybetweenshots"):
            # SAGE defaults an unauthored DelayBetweenShots to 0 ms (retail
            # MordorLanceThrown comments it out and carries cadence on the clip
            # reload instead); record that explicitly rather than inventing an
            # authored source.
            combat["delayBetweenShotsMs"] = {
                "value": 0,
                "semantic": (
                    "DelayBetweenShots is not authored; the SAGE engine default is 0 ms"
                ),
            }
        if hero and "preAttackDelayMs" not in combat and not weapon.get("preattackdelay"):
            # SAGE defaults an unauthored PreAttackDelay to 0 ms; record that
            # explicitly rather than inventing an authored source.
            combat["preAttackDelayMs"] = {
                "value": 0,
                "semantic": (
                    "PreAttackDelay is not authored; the SAGE engine default is 0 ms"
                ),
            }
        resolved["combat"] = combat
    else:
        missing.append("combat.weapon")
    permanent_weapon_locks = _permanent_weapon_locks(member_lineage, weapon_id)
    if permanent_weapon_locks:
        resolved["permanentWeaponLocks"] = permanent_weapon_locks
    if destroy_die_policies:
        resolved["destroyDie"] = [dict(row) for row in destroy_die_policies]
    if slow_death_fades:
        resolved["slowDeaths"] = [dict(row) for row in slow_death_fades]
    if module_contracts:
        resolved["moduleContracts"] = [dict(row) for row in module_contracts]
    # Alternate weapon-mode profiles (WEAPONSET_TOGGLE_* / MOUNTED): the
    # runtime unit rule carries every fully-resolved conditioned WeaponSet so
    # toggles and mounts swap live combat stats; unresolvable sets are
    # recorded gaps, never partial swaps.
    weapon_modes, weapon_mode_gaps = _conditional_weapon_modes(
        member_lineage,
        documents,
        constants,
        named_definition_cache=named_definition_cache,
        cache_lock=cache_lock,
    )
    if weapon_modes:
        resolved["weaponModes"] = weapon_modes
    if weapon_mode_gaps:
        resolved["weaponModeGaps"] = weapon_mode_gaps
    combat_value = resolved.get("combat", {})
    if isinstance(combat_value, Mapping):
        for field in ("attackRange", "delayBetweenShotsMs", "preAttackDelayMs", "firingDurationMs", "damage"):
            if field not in combat_value:
                missing.append(f"combat.{field}")
    # Armor/forge-upgrade section (armor.ini ArmorSet tables + WeaponSetUpgrade
    # effects). A referenced set or effect that cannot be resolved fails the
    # descriptor closed; an object with no authored ArmorSet records the SAGE
    # engine passthrough explicitly.
    # Imported lazily: armor_compiler reuses this module's resolution helpers.
    from .armor_compiler import (
        ArmorCompilerError,
        base_weapon_targets,
        compile_armor_contract,
        compile_weapon_upgrades,
    )

    try:
        # Singleton units share one ancestry between container and member;
        # scanning it twice would duplicate every upgrade behavior.
        armor_lineages = [member_lineage]
        if (
            container_lineage
            and member_lineage
            and container_lineage[-1].name.casefold()
            != member_lineage[-1].name.casefold()
        ):
            armor_lineages.append(container_lineage)
        resolved["armor"] = compile_armor_contract(
            documents,
            *armor_lineages,
            named_definition_cache=named_definition_cache,
            cache_lock=cache_lock,
            game=game,
        )
        if isinstance(combat_value, Mapping) and combat_value.get("weaponId"):
            combat_weapon = str(combat_value["weaponId"])
            weapon_candidates = [combat_weapon] + [
                candidate
                for candidate in base_weapon_targets(member_lineage)
                if candidate.casefold() != combat_weapon.casefold()
            ]
            weapon_upgrades = compile_weapon_upgrades(
                documents,
                armor_lineages,
                weapon_candidates,
                constants,
                named_definition_cache=named_definition_cache,
                cache_lock=cache_lock,
            )
            if weapon_upgrades:
                combat_value["upgrades"] = weapon_upgrades
    except ArmorCompilerError as exc:
        raise PlayableUnitCompilerError(
            f"armor/upgrade contract is unresolvable: {exc}"
        ) from exc
    formation = _formation_contract(container_lineage, members)
    if formation is None:
        missing.append("formation")
    else:
        resolved["formation"] = formation
    fear_resistance = _fear_resistance_contract(
        container_lineage, member_lineage, documents
    )
    if fear_resistance is not None:
        resolved["fearResistant"] = fear_resistance
    # The runtime simulates one aggregate object. A produced horde's own
    # HordeAIUpdate owns that aggregate policy; its payload member's
    # AIUpdateInterface governs individual SAGE members that are not separate
    # authoritative entities here. Singletons use their sole lineage,
    # including concrete subclasses such as DozerAIUpdate.
    auto_acquire_lineage = (
        container_lineage
        if container_lineage
        and member_lineage
        and container_lineage[-1].name.casefold()
        != member_lineage[-1].name.casefold()
        else member_lineage
    )
    auto_acquire = _auto_acquire_enemies_contract(auto_acquire_lineage)
    if auto_acquire is not None:
        resolved["autoAcquireEnemiesWhenIdle"] = auto_acquire
    mood_attack_check_rate = _mood_attack_check_rate_contract(auto_acquire_lineage)
    if mood_attack_check_rate is not None:
        if auto_acquire is None:
            raise PlayableUnitCompilerError(
                "MoodAttackCheckRate is authored without "
                "AutoAcquireEnemiesWhenIdle on the effective AI update owner"
            )
        resolved["moodAttackCheckRate"] = mood_attack_check_rate
    return {
        "status": "ready" if not missing else "unresolved",
        "resolved": resolved,
        "missing": sorted(set(missing), key=str.casefold),
    }


def _auto_acquire_enemies_contract(
    owner_lineage: Sequence[SageObject],
) -> dict[str, object] | None:
    """Compile AIUpdateInterface.AutoAcquireEnemiesWhenIdle exactly.

    Missing authoring emits no contract, preserving the runtime's established
    behavior. The first token is the Yes/No switch; ATTACK_BUILDINGS and
    STEALTHED are independent authored bits (retail sometimes retains them
    after No, where they are inert). STEALTHED describes the source unit
    firing while cloaked, not detection of cloaked targets.
    """

    authored: list[tuple[SageBlock, tuple[str, ...]]] = []
    for block in _effective_top_blocks(owner_lineage):
        if (block.header_key or "").casefold() != "behavior":
            continue
        if block.kind.casefold() not in {
            "aiupdateinterface",
            "dozeraiupdate",
            "deploystyleaiupdate",
            "giantbirdaiupdate",
            "hordeaiupdate",
            "hordeworkeraiupdate",
            "siegeaiupdate",
            "workeraiupdate",
        }:
            continue
        values = block.values("AutoAcquireEnemiesWhenIdle")
        if not values:
            continue
        # SAGE INI's bit-list scanner tokenizes only on whitespace and '='.
        # Do not use the compiler's broad identifier extractor here: it would
        # incorrectly turn malformed `Yes,ATTACK_BUILDINGS` into two valid
        # tokens instead of preserving the comma for an unknown-token refusal.
        tokens = tuple(
            token
            for token in re.split(r"[ \n\r\t=]+", values[-1].strip())
            if token
        )
        authored.append((block, tokens))
    if not authored:
        return None

    contracts: list[tuple[bool, bool, bool]] = []
    for block, tokens in authored:
        if not tokens or tokens[0].casefold() not in {"yes", "no"}:
            raise PlayableUnitCompilerError(
                f"{block.kind} AutoAcquireEnemiesWhenIdle must start with Yes or No"
            )
        enabled = tokens[0].casefold() == "yes"
        modifiers = [token.casefold() for token in tokens[1:]]
        if len(modifiers) != len(set(modifiers)):
            raise PlayableUnitCompilerError(
                f"{block.kind} AutoAcquireEnemiesWhenIdle repeats a modifier"
            )
        unknown = sorted(
            token
            for token in modifiers
            if token not in {"attack_buildings", "stealthed"}
        )
        if unknown:
            raise PlayableUnitCompilerError(
                f"{block.kind} AutoAcquireEnemiesWhenIdle has unknown modifier(s): "
                + ", ".join(unknown)
            )
        contracts.append(
            (enabled, "attack_buildings" in modifiers, "stealthed" in modifiers)
        )
    if any(contract != contracts[0] for contract in contracts[1:]):
        raise PlayableUnitCompilerError(
            "effective AIUpdateInterface modules disagree on "
            "AutoAcquireEnemiesWhenIdle"
        )
    enabled, attack_buildings, while_stealthed = contracts[0]
    block = authored[0][0]
    return {
        "enabled": {"value": enabled},
        "attackBuildings": {"value": attack_buildings},
        "whileStealthed": {"value": while_stealthed},
        "sourceIni": block.source_virtual_path,
        "line": block.line,
        "semantic": (
            "AIUpdateInterface.AutoAcquireEnemiesWhenIdle "
            + ("Yes" if enabled else "No")
        ),
    }


def _mood_attack_check_rate_contract(
    owner_lineage: Sequence[SageObject],
) -> dict[str, object] | None:
    """Compile AIUpdateInterface.MoodAttackCheckRate as authored milliseconds."""

    authored: list[tuple[SageBlock, int]] = []
    for block in _effective_top_blocks(owner_lineage):
        if (block.header_key or "").casefold() != "behavior":
            continue
        if block.kind.casefold() not in {
            "aiupdateinterface",
            "dozeraiupdate",
            "deploystyleaiupdate",
            "giantbirdaiupdate",
            "hordeaiupdate",
            "hordeworkeraiupdate",
            "siegeaiupdate",
            "workeraiupdate",
        }:
            continue
        values = block.values("MoodAttackCheckRate")
        if not values:
            continue
        # INI::parseDurationUnsignedInt consumes one concrete unsigned duration.
        # Retail authors decimal milliseconds only; accepting signs, floats,
        # defines or trailing tokens here would invent parser semantics.
        token = values[-1].strip()
        if re.fullmatch(r"[0-9]+", token) is None or int(token) <= 0:
            raise PlayableUnitCompilerError(
                f"{block.kind} MoodAttackCheckRate must be one positive "
                "base-10 integer millisecond duration"
            )
        authored.append((block, int(token)))
    if not authored:
        return None
    if any(milliseconds != authored[0][1] for _, milliseconds in authored[1:]):
        raise PlayableUnitCompilerError(
            "effective AIUpdateInterface modules disagree on MoodAttackCheckRate"
        )
    block, milliseconds = authored[0]
    return {
        "milliseconds": {"value": milliseconds},
        "sourceIni": block.source_virtual_path,
        "line": block.line,
        "semantic": "AIUpdateInterface.MoodAttackCheckRate",
    }


def _fear_resistance_contract(
    container_lineage: Sequence[SageObject],
    member_lineage: Sequence[SageObject],
    documents: Mapping[str, bytes],
) -> dict[str, object] | None:
    """Authored fear reaction of one unit (EmotionTrackerUpdate x emotions.ini).

    SAGE units flee terror because their EmotionTrackerUpdate adds a
    FEAR/TERROR/UNCONTROLLABLE_FEAR EmotionNugget; retail heroes author those
    nuggets out.  A unit whose effective trackers (container + member) add no
    such nugget is fear resistant.  Fail-closed: no tracker, no emotions.ini,
    or an unresolvable nugget reference emits nothing (the runtime default is
    "not resistant").
    """

    trackers: dict[tuple[str, int], SageBlock] = {}
    lineages: list[Sequence[SageObject]] = [member_lineage]
    if (
        container_lineage
        and member_lineage
        and container_lineage[-1].name.casefold() != member_lineage[-1].name.casefold()
    ):
        lineages.append(container_lineage)
    for lineage in lineages:
        for block in _effective_top_blocks(lineage):
            if (block.header_key or "").casefold() != "behavior":
                continue
            if block.kind.casefold() != "emotiontrackerupdate":
                continue
            trackers[(block.source_virtual_path, block.line)] = block
    if not trackers:
        return None
    emotions_source = _optional_document(documents, EMOTIONS_PATH)
    if emotions_source is None:
        return None
    nuggets = _named_blocks(emotions_source, "EmotionNugget")
    fearful = False
    added: list[str] = []
    for block in trackers.values():
        for value in block.values("AddEmotion"):
            tokens = [
                token
                for token in _tokens(value)
                if token.casefold() not in {"override", "none", "null"}
            ]
            if tokens:
                added.append(tokens[-1])
        # Retail also authors ``AddEmotion = OVERRIDE <Name> ... End`` blocks;
        # the CST parses those as nested blocks whose header carries the name.
        for nested in block.blocks:
            if nested.kind.casefold() != "addemotion":
                continue
            tokens = [
                token
                for token in nested.header_tokens
                if token.casefold() not in {"override", "none", "null"}
            ]
            if tokens:
                added.append(tokens[-1])
    for name in added:
        nugget = nuggets.get(name.casefold())
        if nugget is None:
            # Dangling emotion reference: fail closed, emit nothing.
            return None
        if (_first(nugget.values("Type")) or "").casefold() in _FEAR_EMOTION_TYPES:
            fearful = True
    anchor = sorted(trackers.values(), key=lambda row: (row.source_virtual_path, row.line))[0]
    return {
        "value": not fearful,
        "semantic": (
            "authored EmotionTrackerUpdate adds no FEAR/TERROR/"
            "UNCONTROLLABLE_FEAR EmotionNugget"
            if not fearful
            else "authored EmotionTrackerUpdate adds a fear-reaction EmotionNugget"
        ),
        "sourceIni": anchor.source_virtual_path,
        "line": anchor.line,
        "emotionSource": {"sourceIni": EMOTIONS_PATH},
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


def prepare_playable_unit_compiler(
    documents: Mapping[str, bytes],
) -> PlayableUnitCompilerInputs:
    """Parse the large shared corpus once for deterministic faction batches."""

    try:
        player_template_source = _required_document(documents, PLAYER_TEMPLATE_PATH)
    except PlayableUnitCompilerError:
        # PlayerTemplate data is only required when a faction graph asks the
        # compiler to validate hero-roster and starting-building semantics.
        player_templates: Mapping[str, IniBlock] = {}
    else:
        player_templates = _named_blocks(player_template_source, "PlayerTemplate")

    object_parse_errors: dict[str, str] = {}
    objects = _object_index(documents, object_parse_errors)
    return PlayableUnitCompilerInputs(
        documents=documents,
        objects=objects,
        command_sets=_named_blocks(
            _required_document(documents, COMMAND_SET_PATH), "CommandSet"
        ),
        command_buttons=_named_blocks(
            _required_document(documents, COMMAND_BUTTON_PATH), "CommandButton"
        ),
        player_templates=player_templates,
        numeric_defines=_numeric_defines(documents),
        object_parse_errors=object_parse_errors,
    )


def _player_template_context(
    documents: Mapping[str, bytes],
    faction_graph: Mapping[str, object],
    templates: Mapping[str, IniBlock] | None = None,
) -> tuple[list[str], list[str], str, str]:
    target = faction_graph.get("target", {})
    if not isinstance(target, Mapping):
        raise PlayableUnitCompilerError("faction graph target is invalid")
    template_id = str(target.get("playerTemplate", ""))
    if not template_id:
        raise PlayableUnitCompilerError("faction graph has no playerTemplate identity")
    if templates is None:
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
    ring_roster_values = _block_values(template, "BuildableRingHeroesMP")
    if len(ring_roster_values) > 1:
        raise PlayableUnitCompilerError(
            f"PlayerTemplate {template_id} must author at most one "
            "BuildableRingHeroesMP roster"
        )
    ring_roster = list(_tokens(ring_roster_values[0])) if ring_roster_values else []
    starting_values = _block_values(template, "StartingBuilding")
    starting_building = _first(starting_values) or ""
    return roster, ring_roster, starting_building, template_id


def _block_values(block: IniBlock, key: str) -> tuple[str, ...]:
    return tuple(block.values(key))


def _command_slots(block: IniBlock) -> tuple[tuple[int, str], ...]:
    # A slot index may legitimately repeat within a single CommandSet: RotWK
    # packs several radial submenu pages (main / upgrades / hero menus) into one
    # block and restarts slot numbering per page. BFME2's flat single-page sets
    # never repeat a slot, so tolerating repeats here changes no BFME2 output
    # while admitting RotWK's multi-page layout. Genuine malformation (slot < 1
    # or an empty command binding) still fails closed. Exact (slot, command)
    # duplicates are collapsed so a copy-pasted line cannot double-count.
    result: list[tuple[int, str]] = []
    seen: set[tuple[int, str]] = set()
    for key, value in block.assignments:
        if re.fullmatch(r"[0-9]+", key) is None:
            continue
        slot = int(key)
        command = _first((value,))
        if slot < 1 or not command:
            raise PlayableUnitCompilerError(
                f"CommandSet {block.name} has an invalid slot {key}"
            )
        pair = (slot, command)
        if pair in seen:
            continue
        seen.add(pair)
        result.append(pair)
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
            # The ANY/ALL split is NOT uniform across one production row.
            #
            # CITATIONS REBASED 2026-08-04 to the PURE RotWK 2.01 tree
            # (.../editions/rotwk/cache/effective-assets/data/ini). The earlier
            # line numbers here (7513-7519 / 7488 / 8336) and the "TWO
            # NeededUpgrade tokens" claim came from the fan-patched (Unofficial
            # 2.02) layered tree and are NOT retail.
            #
            # Pure retail authors `NeededUpgradeAny` on exactly NINE buttons in
            # commandbutton.ini -- the layered tree has 44:
            #   :6327 Command_ConstructGondorRangerHorde
            #   :6765 Command_ConstructArnorRangerHorde
            #   :7138 Command_ConstructRohanRohirrimHorde
            #   :11571 Command_PurchaseTechnologyGondorFireArrows
            #   :11724 Command_PurchaseTechnologyArnorFireArrows
            #   :15137 Command_ConstructAngmarDarkDunedainHorde
            #   :15153 Command_ConstructAngmarDarkRangerHorde
            #   :15182 Command_ConstructAngmarSnowTrollHorde
            #   :15197 Command_ConstructAngmarHillTrollHorde
            # Every one of those nine names exactly ONE `NeededUpgrade` token,
            # so on pure retail the emitted group is a single-member set and the
            # ANY-of gate is behaviourally identical to the ALL-of gate. The
            # multi-token ANY-of groups only exist in the fan patch. This code
            # stays because it is data-driven and correct either way; do not
            # "verify" it against a multi-token retail example, because there
            # is none.
            #
            # The commandSetTransition `TriggeredBy` requirements are a
            # different authority (the producer must sit on the upgraded
            # CommandSet at all) and stay ALL-of regardless.
            # `prerequisites` therefore keeps exactly the ALL-of set and the
            # ANY-of group rides beside it in `prerequisiteAnyOf`; a row that
            # never authors the flag emits no group at all, so every consumer
            # that ignores the new key keeps the historical ALL-of behavior.
            needed_upgrade_any = any(
                value.strip().casefold() in {"yes", "true", "1"}
                for value in _block_values(button, "NeededUpgradeAny")
            )
            needed_requirements = sorted(
                {
                    token
                    for value in _block_values(button, "NeededUpgrade")
                    for token in _tokens(value)
                    if _is_upgrade_or_science_token(token)
                },
                key=str.casefold,
            )
            other_requirements = sorted(
                {
                    token
                    for field in ("Upgrade", "Options")
                    for value in _block_values(button, field)
                    for token in _tokens(value)
                    if _is_upgrade_or_science_token(token)
                },
                key=str.casefold,
            )
            any_of_requirements: list[str] = []
            if needed_upgrade_any and needed_requirements:
                any_of_requirements = needed_requirements
                direct_requirements = other_requirements
            else:
                direct_requirements = sorted(
                    set(needed_requirements + other_requirements),
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
                    **(
                        {"prerequisiteAnyOf": any_of_requirements}
                        if any_of_requirements
                        else {}
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
        try:
            lineage = _ancestry(objects, candidate)
        except PlayableUnitCompilerError:
            # An unrelated broken inheritance family must not hide the intact
            # containers of the requested member.
            continue
        for block in _effective_top_blocks(lineage):
            assignments = list(block.assignments)
            for nested in _walk_blocks(block.blocks):
                assignments.extend(nested.assignments)
            if any(
                assignment.key.casefold() == "initialpayload"
                and (target := _first((assignment.value,)))
                and target.casefold() == member_id.casefold()
                for assignment in assignments
            ):
                result.append(candidate)
                break
    return tuple(sorted(result, key=lambda item: item.name.casefold()))


def _kind_of(ancestry: Sequence[SageObject]) -> tuple[str, ...]:
    kinds: set[str] = set()
    for item in ancestry:
        for row in item.assignments:
            if row.key.casefold() != "kindof":
                continue
            tokens = tuple(token.upper() for token in _tokens(row.value))
            if any(token.startswith(("+", "-")) for token in tokens):
                for token in tokens:
                    if token.startswith("+") and len(token) > 1:
                        kinds.add(token[1:])
                    elif token.startswith("-") and len(token) > 1:
                        kinds.discard(token[1:])
                    else:
                        kinds.add(token)
            else:
                kinds = set(tokens)
    return tuple(sorted(kinds))


def playable_object_kind_of(
    prepared: PlayableUnitCompilerInputs, object_id: str
) -> tuple[str, ...]:
    """Return the effective authored KindOf tokens for one prepared Object."""

    target = prepared.objects.get(object_id.casefold())
    if target is None:
        raise PlayableUnitCompilerError(f"effective Object is missing: {object_id}")
    return _kind_of(_ancestry(prepared.objects, target))


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


_GEOMETRY_PIECE_FIELDS = {
    "geometrymajorradius": "majorRadius",
    "geometryminorradius": "minorRadius",
    "geometryheight": "height",
}


def _geometry_offset(token: str) -> dict[str, float] | None:
    """Parse a SAGE ``GeometryOffset = X:-22 Y:-30 Z:0`` triple.

    SAGE geometry is authored in the object's own source frame with Z up, so X
    and Y are the ground-plane axes the selection footprint is measured on.
    """

    offset: dict[str, float] = {}
    for part in token.replace(",", " ").split():
        axis, _, raw = part.partition(":")
        folded = axis.strip().casefold()
        if folded not in {"x", "y", "z"} or not raw.strip():
            continue
        try:
            offset[folded] = float(raw.strip())
        except ValueError:
            return None
    if not offset:
        return None
    return {axis: offset.get(axis, 0.0) for axis in ("x", "y", "z")}


def _geometry_number(
    token: str, defines: Mapping[str, int | float]
) -> dict[str, object]:
    """Authored geometry scalar with its resolved numeric value when known.

    Unresolvable symbols keep an expression-only row: the footprint union below
    then ignores that piece rather than inventing a size for it.
    """

    text = token.strip().rstrip("%")
    row: dict[str, object] = {"authored": text}
    try:
        row["value"] = float(text) if "." in text else int(text)
        return row
    except ValueError:
        resolved = defines.get(text.casefold())
        if resolved is not None:
            row["value"] = resolved
    return row


def _geometry_contract(
    ancestry: Sequence[SageObject], defines: Mapping[str, int | float]
) -> dict[str, object] | None:
    """Project an Object's authored SAGE Geometry block.

    SAGE uses this volume for collision AND for mouse picking: the click is
    hit-tested against the footprint, never against a flat world-unit radius.
    The primary ``Geometry`` plus every ``AdditionalGeometry`` piece (each with
    its own ``GeometryOffset``) are retained verbatim, and ``footprint`` carries
    their union so a runtime can pick without re-deriving the geometry algebra.

    Geometry is not inherited piecemeal in SAGE: the most-derived ancestor that
    authors ``Geometry`` replaces the block wholesale, which is what the owner
    scan below reproduces.
    """

    owner: SageObject | None = None
    for item in ancestry:
        if any(row.key.casefold() == "geometry" for row in item.assignments):
            owner = item
    if owner is None:
        return None
    pieces: list[dict[str, object]] = []
    current: dict[str, object] | None = None
    is_small: bool | None = None
    for row in owner.assignments:
        key = row.key.casefold()
        if key in {"geometry", "additionalgeometry"}:
            if key == "geometry":
                # A second primary block restarts the volume.
                pieces = []
            shape = row.value.strip().split()[0].upper() if row.value.strip() else ""
            current = {
                "role": "primary" if key == "geometry" else "additional",
                "shape": shape,
                "line": row.line,
                "sourceIni": row.source_virtual_path,
            }
            pieces.append(current)
            continue
        if key == "geometryissmall":
            is_small = row.value.strip().casefold() in {"yes", "true", "1"}
            continue
        if current is None:
            continue
        if key == "geometryname":
            current["name"] = row.value.strip()
        elif key in _GEOMETRY_PIECE_FIELDS:
            current[_GEOMETRY_PIECE_FIELDS[key]] = _geometry_number(row.value, defines)
        elif key == "geometryoffset":
            offset = _geometry_offset(row.value)
            if offset is not None:
                current["offset"] = offset
    if not pieces:
        return None
    contract: dict[str, object] = {
        "objectId": owner.name,
        "sourceIni": owner.source_virtual_path,
        "pieces": pieces,
    }
    if is_small is not None:
        contract["isSmall"] = is_small
    primary = pieces[0]
    for field in ("shape", "majorRadius", "minorRadius", "height"):
        if field in primary:
            contract[field] = primary[field]
    footprint = _geometry_footprint(pieces)
    if footprint is not None:
        contract["footprint"] = footprint
    return contract


def _geometry_footprint(
    pieces: Sequence[Mapping[str, object]],
) -> dict[str, float] | None:
    """Union ground-plane half-extents of every geometry piece, source units.

    ``radius`` is the larger half-extent rather than the half-diagonal: a
    selection circle sized to the half-diagonal bulges well past the silhouette
    corners, which is the over-picking this whole projection exists to end.
    """

    half_x = 0.0
    half_y = 0.0
    measured = False
    for piece in pieces:
        major = _geometry_piece_value(piece, "majorRadius")
        minor = _geometry_piece_value(piece, "minorRadius")
        if major is None and minor is None:
            continue
        span_x = abs(major if major is not None else minor or 0.0)
        span_y = abs(minor if minor is not None else major or 0.0)
        offset = piece.get("offset")
        offset_x = float(offset["x"]) if isinstance(offset, Mapping) else 0.0
        offset_y = float(offset["y"]) if isinstance(offset, Mapping) else 0.0
        half_x = max(half_x, abs(offset_x) + span_x)
        half_y = max(half_y, abs(offset_y) + span_y)
        measured = True
    if not measured:
        return None
    return {
        "majorRadius": half_x,
        "minorRadius": half_y,
        "radius": max(half_x, half_y),
    }


def _geometry_piece_value(
    piece: Mapping[str, object], field: str
) -> float | None:
    row = piece.get(field)
    if not isinstance(row, Mapping) or "value" not in row:
        return None
    return float(row["value"])


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
        authored_identifier = _first((assignment.value,))
        if authored_edges is None:
            if not folded.startswith(("voice", "sound", "eva")):
                continue
            normalized_identifier = normalize_faction_voice_event(
                ancestry[-1].name, assignment.key, authored_identifier
            )
            identifiers = [normalized_identifier]
        else:
            tokens = {
                normalize_faction_voice_event(
                    ancestry[-1].name, assignment.key, token
                ).casefold()
                for token in _tokens(assignment.value)
            }
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
            row = {
                "id": identifier,
                "sourceIni": assignment.source_virtual_path,
                "line": assignment.line,
            }
            result[assignment.key].append(row)
    return {
        key: rows
        for key, rows in sorted(result.items(), key=lambda item: item[0].casefold())
    }


# ---------------------------------------------------------------------------
# Weapon audio chain.
#
# Retail does NOT author a melee swing / weapon-fire sound on the Object: it
# authors it on the WEAPON, as ``Weapon BoromirSword / FireFX =
# FX_GondorSwordHit`` (pure retail weapon.ini:5616-5624) resolved through
# ``FXList FX_GondorSwordHit / Sound / Name = ImpactSword01``
# (fxlist.ini:7584-7586).  This section joins the object's WeaponSet weapons
# to that chain and emits the result as the ``weapon`` owner in
# ``presentation.audioRoutes``, keeping the weapon hop AND the fxlist hop
# provenance per row.  Every unresolvable link is RECORDED in
# ``presentation.weaponAudioGaps`` with its reason instead of being dropped.
# ---------------------------------------------------------------------------

FX_LIST_PATH = "data/ini/fxlist.ini"

_WEAPON_AUDIO_FX_FIELDS = (
    ("FireFX", "firefx"),
    ("ProjectileDetonationFX", "projectiledetonationfx"),
)
_AUDIO_NULL_SENTINELS = frozenset({"none", "null"})
_FX_INDEX_ERROR_KEY = "__parse_error__"


def _fx_list_sound_index(
    documents: Mapping[str, bytes],
    *,
    cache: dict[tuple[str, str], dict[str, list[dict[str, object]]] | None]
    | None = None,
    cache_lock: threading.Lock | None = None,
) -> tuple[dict[str, list[dict[str, object]]] | None, str | None]:
    """Index fxlist.ini Sound names: fx key -> rows ``{id, fxListId, line}``.

    Returns ``(index, error)``.  ``index`` is None when fxlist.ini is absent
    from this compilation view or unparseable; ``error`` carries the parse
    failure text so the caller can record the exact reason.
    """

    payload = documents.get(FX_LIST_PATH)
    if payload is None:
        return None, None
    cache_key = ("fxlist-sound-index", FX_LIST_PATH)
    lock = cache_lock or threading.Lock()
    if cache is not None:
        with lock:
            if cache_key in cache:
                cached = cache[cache_key]
                if cached is not None and _FX_INDEX_ERROR_KEY in cached:
                    return None, str(cached[_FX_INDEX_ERROR_KEY][0]["reason"])
                return cached, None
    try:
        records = parse_fx_lists(payload)
    except ValueError as exc:
        failure: dict[str, list[dict[str, object]]] = {
            _FX_INDEX_ERROR_KEY: [{"reason": str(exc)}]
        }
        if cache is not None:
            with lock:
                cache.setdefault(cache_key, failure)
        return None, str(exc)
    index: dict[str, list[dict[str, object]]] = {}
    for key, record in records.items():
        rows: list[dict[str, object]] = []
        for section in record["sections"]:
            if str(section["kind"]).casefold() != "sound":
                continue
            for item in section["assignments"]:
                if str(item["field"]).casefold() != "name":
                    continue
                tokens = _tokens(str(item["value"]))
                if not tokens or tokens[0].casefold() in _AUDIO_NULL_SENTINELS:
                    continue
                rows.append(
                    {
                        "id": tokens[0],
                        "fxListId": str(record["fxListId"]),
                        "line": int(item["sourceSpan"]["startLine"]),
                    }
                )
        index[key] = rows
    if cache is not None:
        with lock:
            cache.setdefault(cache_key, index)
            cached = cache[cache_key]
            if cached is not None and _FX_INDEX_ERROR_KEY in cached:
                return None, str(cached[_FX_INDEX_ERROR_KEY][0]["reason"])
            return cached, None
    return index, None


def _weapon_audio_routes(
    target_lineage: Sequence[SageObject],
    member_lineage: Sequence[SageObject],
    documents: Mapping[str, bytes],
    *,
    named_definition_cache: dict[
        tuple[str, str], dict[str, list[dict[str, object]]] | None
    ]
    | None = None,
    cache_lock: threading.Lock | None = None,
) -> tuple[dict[str, list[dict[str, object]]], list[dict[str, object]]]:
    """Resolve the weapon -> FireFX/ProjectileDetonationFX -> Sound chain.

    Walks every effective WeaponSet block on the container and primary-member
    lineages, resolves each authored weapon slot through weapon.ini and
    fxlist.ini, and returns ``(routes, gaps)``: ``routes`` maps the weapon FX
    field (``FireFX`` / ``ProjectileDetonationFX``) to fully-resolved sound
    rows carrying both hops' provenance; ``gaps`` records every reference the
    chain could not resolve (missing weapon definition, fxlist.ini absent
    from the view, missing FXList, or an FXList that authors no Sound) with
    its reason — fail closed by recording, never by inventing a leaf.
    """

    fx_index, fx_error = _fx_list_sound_index(
        documents, cache=named_definition_cache, cache_lock=cache_lock
    )
    same_object = (
        target_lineage[-1].name.casefold() == member_lineage[-1].name.casefold()
    )
    owners: list[tuple[str, Sequence[SageObject]]] = [
        ("object" if same_object else "container", target_lineage)
    ]
    if not same_object:
        owners.append(("primaryMember", member_lineage))
    routes: dict[str, list[dict[str, object]]] = {}
    gaps: list[dict[str, object]] = []
    seen: set[str] = set()

    def _record(bucket: list[dict[str, object]], row: dict[str, object]) -> None:
        key = _digest(row)
        if key not in seen:
            seen.add(key)
            bucket.append(row)

    for role, lineage in owners:
        for block in _effective_top_blocks(lineage):
            if (block.header_key or block.kind).casefold() != "weaponset":
                continue
            condition_values = [
                assignment.value.strip()
                for assignment in block.assignments
                if assignment.key.casefold() in {"condition", "conditions"}
            ]
            default_set = all(
                _is_default_set_condition(value) for value in condition_values
            )
            condition_tokens = sorted(
                {
                    token
                    for value in condition_values
                    for token in _tokens(value)
                },
                key=str.casefold,
            )
            for assignment in block.assignments:
                if assignment.key.casefold() != "weapon":
                    continue
                tokens = _tokens(assignment.value)
                if not tokens:
                    continue
                weapon_id = tokens[-1]
                if weapon_id.casefold() in _AUDIO_NULL_SENTINELS:
                    continue
                slot_tokens = {
                    token.upper()
                    for token in tokens[:-1]
                    if token.casefold() in _WEAPON_SLOT_NAMES
                }
                slot = (
                    next(iter(slot_tokens)) if len(slot_tokens) == 1 else None
                )
                base: dict[str, object] = {
                    "ownerRole": role,
                    "weaponId": weapon_id,
                    **({"weaponSlot": slot} if slot is not None else {}),
                    "defaultSet": default_set,
                    **(
                        {"weaponSetConditions": condition_tokens}
                        if not default_set
                        else {}
                    ),
                }
                definition = _named_definition_values(
                    documents,
                    "Weapon",
                    weapon_id,
                    cache=named_definition_cache,
                    cache_lock=cache_lock,
                )
                if definition is None:
                    _record(
                        gaps,
                        {
                            **base,
                            "reason": "weapon-definition-missing-or-ambiguous",
                            "sourceIni": assignment.source_virtual_path,
                            "line": assignment.line,
                        },
                    )
                    continue
                for field_name, folded in _WEAPON_AUDIO_FX_FIELDS:
                    for fx_row in definition.get(folded, ()):
                        fx_tokens = _tokens(str(fx_row.get("expression", "")))
                        if (
                            not fx_tokens
                            or fx_tokens[0].casefold() in _AUDIO_NULL_SENTINELS
                        ):
                            continue
                        fx_id = fx_tokens[0]
                        hop: dict[str, object] = {
                            "field": field_name,
                            "fxListId": fx_id,
                            "sourceIni": str(fx_row.get("sourceIni", "")),
                            "line": int(fx_row.get("line", 0)),
                        }
                        if fx_index is None:
                            reason = (
                                "fxlist-unparseable"
                                if fx_error
                                else "fxlist-document-not-in-view"
                            )
                            gap = {**base, **hop, "reason": reason}
                            if fx_error:
                                gap["detail"] = fx_error
                            _record(gaps, gap)
                            continue
                        sound_rows = fx_index.get(fx_id.casefold())
                        if sound_rows is None:
                            _record(
                                gaps,
                                {
                                    **base,
                                    **hop,
                                    "reason": "fxlist-definition-missing",
                                },
                            )
                            continue
                        if not sound_rows:
                            _record(
                                gaps,
                                {
                                    **base,
                                    **hop,
                                    "reason": "fxlist-authors-no-sound",
                                },
                            )
                            continue
                        for sound in sound_rows:
                            _record(
                                routes.setdefault(field_name, []),
                                {
                                    "id": str(sound["id"]),
                                    **base,
                                    "fxListId": str(sound["fxListId"]),
                                    "sourceIni": hop["sourceIni"],
                                    "line": hop["line"],
                                    "fxSourceIni": FX_LIST_PATH,
                                    "fxLine": int(sound["line"]),
                                },
                            )
    for rows in routes.values():
        rows.sort(
            key=lambda row: (
                str(row["id"]).casefold(),
                str(row["weaponId"]).casefold(),
                str(row["ownerRole"]),
                str(row.get("weaponSlot", "")),
                int(row["line"]),
                int(row["fxLine"]),
            )
        )
    gaps.sort(
        key=lambda row: (
            str(row["reason"]),
            str(row["weaponId"]).casefold(),
            str(row.get("field", "")),
            str(row.get("fxListId", "")).casefold(),
            str(row["ownerRole"]),
            int(row.get("line", 0)),
        )
    )
    return (
        {key: routes[key] for key in sorted(routes, key=str.casefold)},
        gaps,
    )


def _runtime_module_evidence(
    target_lineage: Sequence[SageObject],
    member_lineage: Sequence[SageObject],
    consumed_container_modules: frozenset[tuple[str, int, str, str]],
    consumed_member_modules: frozenset[tuple[str, int, str, str]] = frozenset(),
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
            consumed = (
                identity in consumed_container_modules
                if role == "container"
                else identity in consumed_member_modules
            )
            result.append(
                {
                    "ownerRole": role,
                    "kind": block.kind,
                    "instanceTag": block.instance_tag or "",
                    "sourceIni": block.source_virtual_path,
                    "line": block.line,
                    "semanticSha256": _digest(semantic),
                    "consumed": consumed,
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


def _behavior_module_identities(
    lineage: Sequence[SageObject], kind: str
) -> frozenset[tuple[str, int, str, str]]:
    folded = kind.casefold()
    return frozenset(
        (
            block.source_virtual_path.casefold(),
            block.line,
            (block.instance_tag or "").casefold(),
            block.kind.casefold(),
        )
        for block in _effective_top_blocks(lineage)
        if (block.header_key or "").casefold() == "behavior"
        and block.kind.casefold() == folded
    )


def _slow_death_fade_rows(
    lineage: Sequence[SageObject],
    owner_role: str,
    constants: Mapping[str, int | float],
) -> list[dict[str, object]]:
    """Record every SlowDeathBehavior destruction-delay evidence row.

    Retail authors the per-object FADED fade window as ``DestructionDelay``
    on a ``DeathTypes = NONE +FADED`` SlowDeathBehavior (pure retail range
    1000..10000 ms; e.g. object/goodfaction/units/elven/gwaihir.ini:453-460
    authors 2500).  That value used to be dropped silently, handing the
    simulation an effective 0 instead of the authored fade.  Rows are
    EVIDENCE, not consumption: the module stays unconsumed in the runtime
    module evidence, and the authored milliseconds are carried verbatim.  A
    module with no authored delay is recorded as
    ``destructionDelayAuthored: False`` rather than defaulted to 0; an
    authored delay outside the supported define grammar is recorded with its
    raw expression instead of a guessed number.
    """

    rows: list[dict[str, object]] = []
    for block in _effective_top_blocks(lineage):
        if (
            (block.header_key or "").casefold() != "behavior"
            or not block.kind.casefold().endswith("slowdeathbehavior")
        ):
            continue
        row: dict[str, object] = {
            "ownerRole": owner_role,
            "module": block.kind,
            "moduleTag": block.instance_tag or "",
        }
        death_type_values = list(block.values("DeathTypes"))
        if death_type_values:
            row["deathTypes"] = [
                token for value in death_type_values for token in _tokens(value)
            ]
        delay_values = list(block.values("DestructionDelay"))
        if not delay_values:
            row["destructionDelayAuthored"] = False
        else:
            row["destructionDelayAuthored"] = True
            resolved = (
                _resolved_expression(delay_values[0], constants)
                if len(delay_values) == 1
                else None
            )
            if resolved is None and len(delay_values) == 1:
                resolved = _resolved_multiplicative_expression(
                    delay_values[0], constants
                )
            if resolved is not None:
                row["destructionDelayMs"] = resolved
            else:
                row["destructionDelayUnresolvedExpression"] = " ".join(
                    value.strip() for value in delay_values
                )
        row["sourceIni"] = block.source_virtual_path
        row["line"] = block.line
        rows.append(row)
    return rows


def _destroy_die_policies(
    lineage: Sequence[SageObject], owner_role: str
) -> tuple[
    list[dict[str, object]],
    frozenset[tuple[str, int, str, str]],
]:
    """Compile the measured retail DestroyDie/DieMuxData subset.

    DestroyDie has no module-local payload: after DieMuxData accepts a death
    event it destroys the object immediately.  The effective BFME2/RotWK
    carrier census authors only the default ALL mask, explicit ALL, and
    ALL -TOPPLED.  Refuse every other field/mask rather than pretending the
    runtime implements unmeasured veterancy or status filtering.
    """

    policies: list[dict[str, object]] = []
    consumed: set[tuple[str, int, str, str]] = set()
    for block in _effective_top_blocks(lineage):
        if (
            (block.header_key or "").casefold() != "behavior"
            or block.kind.casefold() != "destroydie"
        ):
            continue
        unsupported = sorted(
            {
                assignment.key
                for assignment in block.assignments
                if assignment.key.casefold() != "deathtypes"
            },
            key=str.casefold,
        )
        death_type_rows = [
            assignment
            for assignment in block.assignments
            if assignment.key.casefold() == "deathtypes"
        ]
        if unsupported or len(death_type_rows) > 1:
            detail = ", ".join(unsupported) if unsupported else "duplicate DeathTypes"
            raise PlayableUnitCompilerError(
                f"DestroyDie {block.instance_tag or '<untagged>'} authors "
                f"unsupported DieMuxData fields: {detail}"
            )
        tokens = (
            tuple(token.upper() for token in _tokens(death_type_rows[0].value))
            if death_type_rows
            else ("ALL",)
        )
        if tokens == ("ALL",):
            excluded: list[str] = []
        elif tokens == ("ALL", "-TOPPLED"):
            excluded = ["TOPPLED"]
        else:
            raise PlayableUnitCompilerError(
                f"DestroyDie {block.instance_tag or '<untagged>'} authors "
                f"unsupported DeathTypes filter: {' '.join(tokens)}"
            )
        policies.append(
            {
                "ownerRole": owner_role,
                "module": block.kind,
                "deathTypes": "ALL",
                "excludedDeathTypes": excluded,
                "sourceIni": block.source_virtual_path,
                "line": block.line,
            }
        )
        consumed.add(
            (
                block.source_virtual_path.casefold(),
                block.line,
                (block.instance_tag or "").casefold(),
                block.kind.casefold(),
            )
        )
    return policies, frozenset(consumed)


# Retail SelectPortraits are authored 191x191 (units) or 192x192 (heroes and
# every RotWK "KU*Portrait"); button icons are 63/64px.  The portrait socket
# must only ever bind portrait-class art — a 64-class icon in that slot is a
# conversion bug, not a fallback.
_PORTRAIT_CLASS_SIZES = frozenset({191, 192})


def _mapped_image_size_index(
    faction_graph: Mapping[str, object] | None,
) -> dict[str, tuple[int, int]] | None:
    """Casefolded MappedImage id -> (width, height) from the faction census.

    Returns ``None`` when the graph carries no mapped-image closure so the
    caller can distinguish "no size oracle" from "measured empty".
    """

    if not isinstance(faction_graph, Mapping):
        return None
    leaves = faction_graph.get("resolvedLeaves")
    if not isinstance(leaves, Mapping):
        return None
    rows = leaves.get("mappedImages")
    if not isinstance(rows, list):
        return None
    result: dict[str, tuple[int, int]] = {}
    for row in rows:
        if not isinstance(row, Mapping):
            continue
        identifier = row.get("id")
        coords = row.get("coords")
        if not isinstance(identifier, str) or not isinstance(coords, Mapping):
            continue
        values = [coords.get(name) for name in ("left", "top", "right", "bottom")]
        if any(not isinstance(value, int) or isinstance(value, bool) for value in values):
            continue
        result[identifier.casefold()] = (values[2] - values[0], values[3] - values[1])
    return result


def _portrait_image_ids(
    target_lineage: Sequence[SageObject],
    member_lineage: Sequence[SageObject],
    portrait_sizes: Mapping[str, tuple[int, int]] | None,
) -> list[str]:
    """The authored SelectPortrait bindings, fail-closed to an empty socket.

    Only the effective authored ``SelectPortrait`` values qualify — button
    icons (``ButtonImage``) never spill into the portrait slot.  When the
    faction census provides mapped-image sizes, any candidate that is not
    measurably portrait-class (191/192 square) is dropped: retail authoring
    a missing or 64-class SelectPortrait yields an honest empty portrait
    binding, never borrowed button art.
    """

    candidates: list[str] = []
    for lineage in (target_lineage, member_lineage):
        candidates.extend(
            row.value.strip() for row in _effective_values(lineage, "SelectPortrait")
        )
    candidates = [
        value for value in candidates if value and value.casefold() != "none"
    ]
    if portrait_sizes is not None:
        candidates = [
            value
            for value in candidates
            if (size := portrait_sizes.get(value.casefold())) is not None
            and size[0] == size[1]
            and size[0] in _PORTRAIT_CLASS_SIZES
        ]
    return sorted(set(candidates), key=str.casefold)


def _ui_binding(
    producers: Sequence[Mapping[str, object]],
    command_buttons: Mapping[str, IniBlock],
    target_lineage: Sequence[SageObject],
    member_lineage: Sequence[SageObject],
    command_audio: Mapping[str, Sequence[Mapping[str, object]]],
    portrait_sizes: Mapping[str, tuple[int, int]] | None = None,
) -> dict[str, object]:
    portraits = _portrait_image_ids(target_lineage, member_lineage, portrait_sizes)
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
        "portraitImageIds": portraits,
    }


# ---------------------------------------------------------------------------
# Hero ability contract.
#
# A retail hero carries active abilities as SPECIAL_POWER commands on its own
# authored CommandSet.  Each button names a SpecialPower template; Behavior
# modules on the hero Object bind that template to its effect leaves (weapon
# blasts, ObjectCreationList summons, attribute modifiers, heals) and an
# UnpauseSpecialPowerUpgrade gate chains the ability to the hero's authored
# ExperienceLevel grants.  This section converts that graph into the
# descriptor ``abilities`` array.  Every resolution fails closed per ability:
# an ability whose leaves or systems cannot be converted is recorded as
# ``unimplemented`` with its reason, never approximated; heroes without
# SPECIAL_POWER commands simply emit an empty array.
# ---------------------------------------------------------------------------

SPECIAL_POWER_PATH = "data/ini/specialpower.ini"
EXPERIENCE_LEVELS_PATH = "data/ini/experiencelevels.ini"
OBJECT_CREATION_LIST_PATH = "data/ini/objectcreationlist.ini"
ATTRIBUTE_MODIFIER_PATH = "data/ini/attributemodifier.ini"
WEAPON_PATH = "data/ini/weapon.ini"

_MAX_ABILITY_MODULES = 256
_MAX_EXPERIENCE_LEVELS = 8192
_MAX_OCL_BLOCKS = 16_384
_MAX_MODIFIER_ROWS = 64

# Module kinds that participate in binding/timing/gating but carry no effect
# leaf of their own.
_ABILITY_SUPPORT_MODULE_KINDS = frozenset(
    {
        "specialpowermodule",
        "specialabilityupdate",
        "modelconditionspecialabilityupdate",
        "specialpowertimerrefreshspecialpower",
        "unpausespecialpowerupgrade",
    }
)
# Module kinds whose runtime systems do not exist yet; the ability is recorded
# as unimplemented instead of faking the behavior.
_ABILITY_UNIMPLEMENTED_MODULE_KINDS = {
    "specialdisguiseupdate": "disguise needs the disguise system",
    "weaponmodespecialpowerupdate": "weapon-mode toggle needs the weapon-set system",
    "teleporttocasterspecialpower": "teleport-to-caster needs the caster-anchor system",
    "dominateenemyspecialpower": "domination needs the allegiance system",
    "untamedallegiancespecialpower": "allegiance change needs the allegiance system",
    "grabpassengerspecialpower": "grab needs the passenger system",
    "flingpassengerspecialabilityupdate": "fling needs the passenger system",
    "storeobjectsspecialpower": "object storage needs the garrison system",
    "stopspecialpower": "stop needs the order-interrupt system",
    "specialenemysenseupdate": "enemy sense needs the detection system",
    "siegedeployspecialpower": "siege deploy needs the deploy system",
    "repairspecialpower": "repair needs the structure-repair system",
    "hordedispatchspecialpower": "horde dispatch needs the horde-spawn system",
    "unleashspecialpower": "unleash needs the weapon-set system",
    "toggledeployspecialabilityupdate": "deploy toggle needs the deploy system",
    "splithordespecialpower": "horde split needs the horde system",
    "scavengerspecialpower": "scavenger needs the loot system",
    "manthewallsspecialpower": "man-the-walls needs the garrison system",
    "freezingrainspecialpower": "freezing rain needs the weather system",
    "deflectspecialpower": "deflect needs the status-effect system",
    "activatemodulespecialpower": "module activation needs the module system",
}
# Attribute-modifier kinds the runtime can apply; anything else is recorded as
# a limitation instead of being silently dropped.  The timed-modifier core in
# the sim compounds DAMAGE_MULT/SPEED/VISION/EXPERIENCE/CRUSH/HEALTH, sums
# ARMOR, and reads INVULNERABLE/RESIST_FEAR as flags at value >= 1.
_SUPPORTED_MODIFIER_KINDS = frozenset(
    {
        "ARMOR",
        "DAMAGE_MULT",
        "SPEED",
        "INVULNERABLE",
        "VISION",
        "HEALTH",
        "RESIST_FEAR",
        "CRUSH",
        "EXPERIENCE",
    }
)
EMOTIONS_PATH = "data/ini/emotions.ini"
GAME_DATA_PATH = "data/ini/gamedata.ini"
# EmotionNugget types that make a unit run a fear reaction (emotions.ini);
# a unit whose authored EmotionTrackerUpdate adds none of them never flees.
_FEAR_EMOTION_TYPES = frozenset({"fear", "terror", "uncontrollable_fear"})


def _ability_button_leaf_fields(button: IniBlock) -> dict[str, object]:
    """Icon/label/tooltip/options leaves of one SPECIAL_POWER CommandButton."""

    def first_token(value: str) -> str | None:
        tokens = value.split()
        if not tokens:
            return None
        token = tokens[0]
        if token.casefold() in {"none", "null", "0"} or token.startswith("$"):
            return None
        return token

    options = sorted(
        {
            token
            for value in button.values("Options")
            for token in _tokens(value)
            if token.casefold() not in {"none", "null"}
        },
        key=str.casefold,
    )
    row: dict[str, object] = {
        "commandId": button.name,
        "iconIds": [
            token
            for value in button.values("ButtonImage")
            if (token := first_token(value)) is not None
        ],
        "labelIds": [
            token
            for value in button.values("TextLabel")
            if (token := first_token(value)) is not None
        ],
        "tooltipIds": [
            token
            for value in button.values("DescriptLabel")
            if (token := first_token(value)) is not None
        ],
    }
    if options:
        row["options"] = options
    cursor = _first(button.values("RadiusCursorType"))
    if cursor is not None:
        row["radiusCursorType"] = cursor
    return row


def _invisibility_nugget(block: SageBlock) -> SageBlock | None:
    """The first nested InvisibilityNugget of one Behavior module, if any."""

    for child in block.blocks:
        if (child.header_key or child.kind).casefold() == "invisibilitynugget":
            return child
    return None


def _module_tokens(block: SageBlock, field: str) -> tuple[str, ...]:
    return tuple(
        token
        for value in block.values(field)
        for token in _tokens(value)
        if token.casefold() not in {"none", "null"}
    )


def _optional_document(documents: Mapping[str, bytes], path: str) -> bytes | None:
    for candidate, payload in documents.items():
        if candidate.replace("\\", "/").casefold() == path.casefold():
            return payload
    return None


def _ability_list_defines(source: bytes) -> dict[str, tuple[str, ...]]:
    """Parse ``#define NAME token ...`` list constants (ExperienceLevel targets)."""

    result: dict[str, tuple[str, ...]] = {}
    pattern = re.compile(
        rb"(?m)^[ \t]*#define[ \t]+([A-Za-z_][A-Za-z0-9_]*)[ \t]+(\S[^;/]*?)[ \t]*(?://|;|\r?$)"
    )
    for match in pattern.finditer(source):
        key = match.group(1).decode("ascii").casefold()
        tokens = tuple(
            token
            for token in _tokens(match.group(2).decode("cp1252", errors="replace"))
            if token.casefold() not in {"none", "null"}
        )
        if key in result and result[key] != tokens:
            raise PlayableUnitCompilerError(f"ambiguous ExperienceLevel define: {key}")
        result[key] = tokens
    return result


def _experience_level_rows(source: bytes) -> tuple[dict[str, object], ...]:
    """Bounded depth-aware scan of ExperienceLevel blocks.

    ExperienceLevel nests a SelectionDecal section, so the flat named-block
    parser would close each block early.  The gating fields (TargetNames,
    Rank, Upgrades) plus the full per-level economy leaves
    (RequiredExperience, ExperienceAward, AttributeModifiers, LevelUpFx and
    the rank SelectionDecal texture) are captured; every other assignment is
    ignored by design.
    """

    if len(source) > 8 * 1024 * 1024 or b"\0" in source:
        raise PlayableUnitCompilerError(f"{EXPERIENCE_LEVELS_PATH} is unbounded")
    try:
        text = source.decode("cp1252")
    except UnicodeDecodeError as exc:
        raise PlayableUnitCompilerError(
            f"{EXPERIENCE_LEVELS_PATH} has unsupported encoding"
        ) from exc
    header = re.compile(r"^ExperienceLevel\s+(\S+)\s*$", re.IGNORECASE)
    rows: list[dict[str, object]] = []
    active: str | None = None
    start_line = 0
    depth = 0
    target_names = ""
    upgrades: tuple[str, ...] = ()
    rank: int | None = None
    required_experience = ""
    experience_award = ""
    attribute_modifiers: tuple[str, ...] = ()
    level_up_fx = ""
    decal_texture = ""
    for line_number, raw in enumerate(text.splitlines(), start=1):
        line = re.sub(r"\s+", " ", raw.split(";", 1)[0].split("//", 1)[0]).strip()
        if not line:
            continue
        if active is None:
            match = header.fullmatch(line)
            if match is not None:
                if len(rows) >= _MAX_EXPERIENCE_LEVELS:
                    raise PlayableUnitCompilerError(
                        f"{EXPERIENCE_LEVELS_PATH} exceeds the ExperienceLevel limit"
                    )
                active = match.group(1)
                start_line = line_number
                depth = 0
                target_names = ""
                upgrades = ()
                rank = None
                required_experience = ""
                experience_award = ""
                attribute_modifiers = ()
                level_up_fx = ""
                decal_texture = ""
            continue
        if depth == 0 and header.fullmatch(line):
            raise PlayableUnitCompilerError(
                f"{EXPERIENCE_LEVELS_PATH} has an unterminated ExperienceLevel: {active}"
            )
        if line.casefold() == "end":
            if depth == 0:
                rows.append(
                    {
                        "id": active,
                        "line": start_line,
                        "targetNames": target_names,
                        "rank": rank,
                        "upgrades": upgrades,
                        "requiredExperience": required_experience,
                        "experienceAward": experience_award,
                        "attributeModifiers": attribute_modifiers,
                        "levelUpFx": level_up_fx,
                        "selectionDecalTexture": decal_texture,
                    }
                )
                active = None
            else:
                depth -= 1
            continue
        if "=" in line:
            key, value = (part.strip() for part in line.split("=", 1))
            folded = key.casefold()
            if depth == 1 and folded == "texture" and not decal_texture:
                # The rank chevron leaf of the nested SelectionDecal section.
                decal_texture = value.split()[0] if value.split() else ""
            elif folded == "targetnames":
                target_names = value
            elif folded == "upgrades":
                upgrades = _tokens(value)
            elif folded == "rank" and re.fullmatch(r"[0-9]+", value):
                rank = int(value)
            elif folded == "requiredexperience":
                required_experience = value
            elif folded == "experienceaward":
                experience_award = value
            elif folded in {"attributemodifiers", "attributemodifier"}:
                attribute_modifiers = _tokens(value)
            elif folded == "levelupfx":
                level_up_fx = value
            continue
        depth += 1
    if active is not None:
        raise PlayableUnitCompilerError(
            f"{EXPERIENCE_LEVELS_PATH} has an unterminated ExperienceLevel: {active}"
        )
    return tuple(rows)


def _hero_level_grants(
    rows: Sequence[Mapping[str, object]],
    defines: Mapping[str, tuple[str, ...]],
    hero_names: frozenset[str],
) -> dict[str, int]:
    """Resolve each upgrade granted to this hero to its minimum authored rank."""

    grants: dict[str, int] = {}
    for row in rows:
        rank = row.get("rank")
        if not isinstance(rank, int):
            continue
        targets: set[str] = set()
        for token in _tokens(str(row.get("targetNames", ""))):
            expanded = defines.get(token.casefold())
            if expanded:
                targets.update(value.casefold() for value in expanded)
            else:
                targets.add(token.casefold())
        if not targets & hero_names:
            continue
        for upgrade in row.get("upgrades", ()):
            key = str(upgrade).casefold()
            if key in {"", "none", "null"}:
                continue
            if key not in grants or rank < grants[key]:
                grants[key] = rank
    return grants


# ---------------------------------------------------------------------------
# Experience economy contract.
#
# Every playable unit gains and pays experience through the authored
# ExperienceLevel chain that targets it: per-level cumulative XP thresholds
# (RequiredExperience), the XP a killer collects when this unit dies at that
# level (ExperienceAward), and the level's permanent AttributeModifiers
# (SAGE applies them cumulatively at level-up; additive kinds sum into the
# base stat, multiplicative kinds product).  Chains are selected by target
# specificity: the smallest authored TargetNames set covering the unit wins,
# and an equal-size tie fails closed instead of guessing.  Every level of
# the selected chain must resolve its threshold and award through authored
# literals or GameData constants — a level retail leaves unresolved fails the
# descriptor; a unit with no authored chain at all is recorded as
# ``unauthored`` instead of inventing a default chain.
# ---------------------------------------------------------------------------

UPGRADE_PATH = "data/ini/upgrade.ini"

# Modifier kinds the runtime level-up path can apply faithfully.  HEALTH and
# DAMAGE_ADD are flat per-member additions; the other supported kinds are
# authored multiplicative factors. Anything else stays recorded, never applied.
_LEVEL_MODIFIER_ADDITIVE_KINDS = frozenset({"health", "damage_add"})
_LEVEL_MODIFIER_MULTIPLICATIVE_KINDS = frozenset(
    {"production", "damage_mult", "spell_damage"}
)


def _experience_target_set(
    row: Mapping[str, object],
    defines: Mapping[str, tuple[str, ...]],
) -> frozenset[str]:
    """Resolve one row's TargetNames through the authored list defines."""

    targets: set[str] = set()
    for token in _tokens(str(row.get("targetNames", ""))):
        expanded = defines.get(token.casefold())
        if expanded:
            targets.update(value.casefold() for value in expanded)
        else:
            targets.add(token.casefold())
    targets.discard("")
    targets.discard("none")
    targets.discard("null")
    return frozenset(targets)


def _select_experience_chain(
    rows: Sequence[Mapping[str, object]],
    defines: Mapping[str, tuple[str, ...]],
    unit_names: frozenset[str],
    label: str,
    container_names: frozenset[str] = frozenset(),
    primary_name: str = "",
) -> tuple[tuple[Mapping[str, object], frozenset[str]], ...]:
    """Pick the most specific authored chain targeting one of the unit names.

    Returns the chain's rows sorted by rank, each paired with the resolved
    target set it was matched through.  A unit covered by two different
    chains of equal specificity is ambiguous and fails closed, unless the tie
    resolves through one of two authored facts.

    First, the object's own name (``primary_name``) beats an inherited
    ancestor's.  ``unit_names`` spans the whole inheritance lineage, so a
    ``ChildObject`` inherits every chain that targets its parent: retail
    authors ``ChildObject MordorBatteringRam IsengardBatteringRam`` alongside
    both ``IsengardBatteringRamLevel1`` (TargetNames = IsengardBatteringRam)
    and ``MordorBatteringRamLevel1`` (TargetNames = MordorBatteringRam), and
    both match this unit with one name each.  The engine grants experience per
    object name, so the chain naming the fielded object is the authored truth
    and the parent's chain belongs to the parent.

    Second, and only when the first does not decide it, exactly one of the
    tied chains targeting the fielded container object (``container_names``)
    wins, for the case where retail splits a horde and its members across
    different target lists (RotWK AngmarNecromancerHorde vs its
    AngmarNecroAcolyte members).
    """

    candidates: dict[frozenset[str], list[Mapping[str, object]]] = {}
    for row in rows:
        targets = _experience_target_set(row, defines)
        if not targets or not targets & unit_names:
            continue
        bucket = candidates.setdefault(targets, [])
        if len(bucket) >= _MAX_EXPERIENCE_LEVELS:
            raise PlayableUnitCompilerError(
                f"{label} exceeds the ExperienceLevel chain limit"
            )
        bucket.append(row)
    if not candidates:
        return ()
    ordered = sorted(
        candidates.items(),
        key=lambda item: (len(item[0]), sorted(item[0])),
    )
    own_name = primary_name.strip().casefold()

    def _tie_break(tied: list[tuple[frozenset[str], list[Mapping[str, object]]]]):
        """Resolve equal-size target sets: own name first, then container."""
        own_matched = (
            [item for item in tied if own_name in item[0]] if own_name else []
        )
        if len(own_matched) == 1:
            return own_matched[0]
        container_matched = [item for item in tied if item[0] & container_names]
        if len(container_matched) == 1:
            return container_matched[0]
        return None

    # The engine grants experience per object name and applies EVERY authored
    # ExperienceLevel whose TargetNames covers the object, so specificity only
    # decides ties AT THE SAME RANK — it must not discard the other ranks.
    # Retail relies on this: `EvilLevel1` (experiencelevels.ini:9444) comments
    # out its `EVIL_TROOPS` macro and repeats it as a literal 40-name list,
    # while EvilLevel2..5 keep the 42-name macro. Picking one bucket by size
    # made the 40-name literal win as a one-row chain and silently threw away
    # ranks 2..5 for every EVIL_TROOPS horde — Isengard/Mordor/Wild infantry
    # could never level past rank 1.
    by_rank: dict[int, list[tuple[frozenset[str], Mapping[str, object]]]] = {}
    for targets, bucket_rows in ordered:
        seen_in_bucket: set[int] = set()
        for row in bucket_rows:
            rank = row.get("rank")
            if not isinstance(rank, int) or rank < 1 or rank in seen_in_bucket:
                raise PlayableUnitCompilerError(
                    f"{label} ExperienceLevel {row.get('id')} has a duplicate or "
                    f"invalid Rank: {row.get('rank')}"
                )
            seen_in_bucket.add(rank)
            by_rank.setdefault(rank, []).append((targets, row))
    if len(by_rank) > _MAX_EXPERIENCE_LEVELS:
        raise PlayableUnitCompilerError(
            f"{label} exceeds the ExperienceLevel chain limit"
        )

    ranked: list[Mapping[str, object]] = []
    row_targets: dict[int, frozenset[str]] = {}
    for rank in sorted(by_rank):
        supplying = by_rank[rank]
        own_suppliers = (
            [item for item in supplying if own_name in item[0]]
            if own_name
            else []
        )
        if own_suppliers:
            supplying = own_suppliers
        best_size = min(len(targets) for targets, _ in supplying)
        tied = [item for item in supplying if len(item[0]) == best_size]
        if len(tied) == 1:
            chosen_targets, chosen_row = tied[0]
        else:
            resolved = _tie_break([(t, [r]) for t, r in tied])
            if resolved is None:
                raise PlayableUnitCompilerError(
                    f"{label} matches two ExperienceLevel chains of equal specificity: "
                    f"{sorted(tied[0][0])} vs {sorted(tied[1][0])}"
                )
            chosen_targets = resolved[0]
            chosen_row = resolved[1][0]
        row_targets[rank] = chosen_targets
        ranked.append(chosen_row)
    # Retail chains are rank-ascending but not always 1..N: ring heroes and
    # Treebeard author a single rank-10 row (they enter at the top rank).
    # The contract keeps the authored ranks verbatim instead of renumbering.
    return tuple((row, row_targets[int(row["rank"])]) for row in ranked)


def _level_modifier_leaf(
    modifiers: Mapping[str, IniBlock],
    modifier_id: str,
    constants: Mapping[str, int | float],
    label: str,
) -> dict[str, object]:
    """Resolve one per-level ModifierList to its runtime-applicable rows."""

    block = modifiers.get(modifier_id.casefold())
    if block is None:
        raise PlayableUnitCompilerError(
            f"{label} references a missing ModifierList: {modifier_id}"
        )
    rows: list[dict[str, object]] = []
    unsupported: list[str] = []
    for value in block.values("Modifier"):
        parts = value.split()
        if len(parts) < 2:
            raise PlayableUnitCompilerError(
                f"ModifierList {modifier_id} has a malformed Modifier row: {value!r}"
            )
        kind = parts[0]
        magnitude = _modifier_value(parts[1], constants)
        if magnitude is None:
            raise PlayableUnitCompilerError(
                f"ModifierList {modifier_id} has an unresolvable Modifier value: {value!r}"
            )
        if len(rows) >= _MAX_MODIFIER_ROWS:
            raise PlayableUnitCompilerError(
                f"ModifierList {modifier_id} exceeds the Modifier row limit"
            )
        folded = kind.casefold()
        if folded in _LEVEL_MODIFIER_ADDITIVE_KINDS:
            canonical = "HEALTH" if folded == "health" else "DAMAGE_ADD"
            rows.append(
                {"kind": canonical, "value": magnitude, "application": "additive"}
            )
        elif folded in _LEVEL_MODIFIER_MULTIPLICATIVE_KINDS:
            canonical = {
                "production": "PRODUCTION",
                "damage_mult": "DAMAGE_MULT",
                "spell_damage": "SPELL_DAMAGE",
            }[folded]
            rows.append(
                {"kind": canonical, "value": magnitude, "application": "multiplicative"}
            )
        else:
            unsupported.append(kind)
    leaf: dict[str, object] = {
        "id": block.name,
        "modifiers": rows,
        "sourceIni": ATTRIBUTE_MODIFIER_PATH,
    }
    category = _first(block.values("Category"))
    if category is not None:
        leaf["category"] = category
    if unsupported:
        leaf["unsupportedModifiers"] = sorted(set(unsupported), key=str.casefold)
    return leaf


def _experience_contract(
    target_lineage: Sequence[SageObject],
    member_lineage: Sequence[SageObject],
    members: Sequence[Mapping[str, object]],
    documents: Mapping[str, bytes],
    constants: Mapping[str, int | float],
    creation_grant: Mapping[str, object] | None = None,
) -> dict[str, object]:
    """Compile the authored ExperienceLevel chain of one playable unit."""

    label = f"unit {target_lineage[-1].name}"
    source = _optional_document(documents, EXPERIENCE_LEVELS_PATH)
    if source is None:
        if creation_grant is not None:
            raise PlayableUnitCompilerError(
                f"{label} ExperienceLevelCreate has no authored ExperienceLevel source"
            )
        return {
            "status": "unavailable",
            "note": "experience level source is not in the effective INI view",
        }
    unit_names = frozenset(
        {item.name.casefold() for item in target_lineage}
        | {item.name.casefold() for item in member_lineage}
        | {str(row.get("objectId", "")).casefold() for row in members}
    )
    rows = _experience_level_rows(source)
    defines = _ability_list_defines(source)
    chain = _select_experience_chain(
        rows,
        defines,
        unit_names,
        label,
        container_names=frozenset(
            item.name.casefold() for item in target_lineage
        ),
        # Lineage runs ancestor -> descendant, so the last entry is the
        # fielded object itself.
        primary_name=target_lineage[-1].name if target_lineage else "",
    )
    if not chain:
        if creation_grant is not None:
            raise PlayableUnitCompilerError(
                f"{label} ExperienceLevelCreate has no authored ExperienceLevel chain"
            )
        return {
            "status": "unauthored",
            "note": "retail authors no ExperienceLevel chain targeting this unit",
            "sourceIni": EXPERIENCE_LEVELS_PATH,
        }
    modifier_source = _optional_document(documents, ATTRIBUTE_MODIFIER_PATH)
    modifier_blocks: dict[str, IniBlock] = {}
    if modifier_source is not None:
        modifier_blocks = _named_blocks(modifier_source, "ModifierList")
    levels: list[dict[str, object]] = []
    for row, targets in chain:
        rank = int(row["rank"])
        level_label = f"{label} ExperienceLevel {row.get('id')}"
        required_expression = str(row.get("requiredExperience", "")).strip()
        award_expression = str(row.get("experienceAward", "")).strip()
        required = (
            _resolved_expression(required_expression, constants)
            if required_expression
            else None
        )
        award = (
            _resolved_expression(award_expression, constants)
            if award_expression
            else None
        )
        if required is None:
            raise PlayableUnitCompilerError(
                f"{level_label} has no resolvable RequiredExperience"
            )
        award_authored = award is not None
        if award is None:
            # Several retail creation-granted chains do not author a kill
            # award at or below the granted entry rank. Those lower ranks are
            # unreachable on creation, but remain explicit unknown evidence.
            # A non-empty unresolved expression is always malformed.
            if (
                award_expression
                or creation_grant is None
                or rank > int(creation_grant["rank"])
            ):
                raise PlayableUnitCompilerError(
                    f"{level_label} has no resolvable ExperienceAward"
                )
        # SAGE parses both fields through its INI integer scanner, which
        # truncates fractional macro results (RotWK authors awards like
        # #DIVIDE( ADVANCED_EXP_AWARD_TARGET ANGMAR_HILL_TROLL_HORDE_SIZE )
        # = 7.5; every award/threshold context in both corpora is integral
        # or positive-fractional, where truncation and floor coincide).
        # BFME2 authors only integral values here, so this is a no-op there.
        if isinstance(required, float):
            required = math.floor(required)
        if isinstance(award, float):
            award = math.floor(award)
        level: dict[str, object] = {
            "experienceId": str(row["id"]),
            "rank": rank,
            "requiredExperience": required,
            "line": int(row["line"]),
        }
        if award_authored:
            level["experienceAward"] = award
        else:
            level["experienceAwardStatus"] = "unauthored"
        if any(
            expression
            and _resolved_expression(expression, {}) is None
            for expression in (required_expression, award_expression)
        ):
            # A non-literal resolved only through the GameData constants.
            level["constantSourceIni"] = "data/ini/gamedata.ini"
        modifier_ids = [
            token
            for token in row.get("attributeModifiers", ())
            if str(token).casefold() not in {"", "none", "null"}
        ]
        if modifier_ids:
            if modifier_source is None:
                raise PlayableUnitCompilerError(
                    f"{level_label} authors AttributeModifiers but "
                    f"{ATTRIBUTE_MODIFIER_PATH} is not in the effective INI view"
                )
            level["attributeModifiers"] = [
                _level_modifier_leaf(modifier_blocks, str(modifier_id), constants, level_label)
                for modifier_id in modifier_ids
            ]
        upgrades = [
            str(token)
            for token in row.get("upgrades", ())
            if str(token).casefold() not in {"", "none", "null"}
        ]
        if upgrades:
            level["upgrades"] = upgrades
        decal_texture = str(row.get("selectionDecalTexture", "")).strip()
        if decal_texture:
            level["selectionDecalTextureId"] = decal_texture
        level_up_fx = str(row.get("levelUpFx", "")).strip()
        if level_up_fx:
            level["levelUpFxId"] = level_up_fx
        levels.append(level)
    # An ExperienceLevel row describes progression at that rank; it does not
    # itself grant the rank at object creation. Ordinary objects start at rank
    # one even when their first authored row is a higher summon-only rank.
    initial_rank = 1
    if creation_grant is not None:
        granted_rank = int(creation_grant["rank"])
        matching_grant_rows = [
            row for row, _targets in chain if int(row["rank"]) == granted_rank
        ]
        if len(matching_grant_rows) != 1:
            raise PlayableUnitCompilerError(
                f"{label} ExperienceLevelCreate rank {granted_rank} does not "
                "match exactly one authored experience-chain rank"
            )
        initial_rank = granted_rank
    return {
        "status": "compiled",
        "sourceIni": EXPERIENCE_LEVELS_PATH,
        # ExperienceLevelCreate is the only creation-rank authority. All exact
        # corpus carriers enter at the same rank as their chain's first row, so
        # a disagreement is rejected above rather than guessed.
        "initialRank": initial_rank,
        **(
            {"experienceLevelCreate": dict(creation_grant)}
            if creation_grant is not None
            else {}
        ),
        "maxLevel": int(chain[-1][0]["rank"]),
        "targetCount": len(chain[0][1]),
        # SAGE applies level modifiers permanently at level-up; the runtime
        # recomputes effective stats from the base plus every earned level.
        "modifierApplication": "cumulative-per-level",
        "levels": levels,
    }


_SUMMON_LEAF_DOCUMENTS = (
    "data/ini/objectcreationlist.ini",
    "data/ini/weapon.ini",
    "data/ini/fxlist.ini",
    "data/ini/attributemodifier.ini",
    "data/ini/upgrade.ini",
    "data/ini/fxparticlesystem.ini",
    "data/ini/gamedata.ini",
)


def _summon_leaf_closure(
    ocl_id: str,
    objects: Mapping[str, SageObject],
    documents: Mapping[str, bytes],
    constants: Mapping[str, int | float],
) -> dict[str, object] | None:
    """Resolve one ability ObjectCreationList into spellbook-shaped leaves.

    Retail summons an army through an egg: the ability's ObjectCreationList
    creates a short-lived, deliberately model-less egg whose
    ``SlowDeathBehavior`` OCL hatches the real battalions
    (goodfactionsubobjects.ini AragornArmyofTheDeadSmallEgg -> MIDPOINT
    SUPERWEAPON_SpawnAragornOathbreakers).  The summon effect on its own names
    only the egg, which no playable-unit document ever describes, so the
    runtime cannot spawn anything from it.  This resolves the SAME leaf
    families the spellbook lane already publishes (objects, object-creation
    lists, weapons) over the ability's OCL, so the runtime can walk the egg ->
    hatch -> horde -> member chain with the proven spellbook machinery instead
    of a second summon mechanism.

    Returns ``None`` when the effective INI view this compilation was handed
    does not carry every document the shared leaf resolver requires (the
    single-unit ``import-unit`` lane), leaving the effect byte-identical to the
    pre-closure shape.
    """

    if any(path not in documents for path in _SUMMON_LEAF_DOCUMENTS):
        return None
    # Deferred import: spellbook_compiler imports this module at load time.
    from types import SimpleNamespace

    from .spellbook_compiler import SpellbookCompilerError, _LeafResolver

    try:
        resolver = _LeafResolver(
            documents,
            SimpleNamespace(objects=objects),  # type: ignore[arg-type]
            constants,
            {},
        )
        resolver.object_creation_list(ocl_id, f"summon ability {ocl_id}")
    except (SpellbookCompilerError, ValueError, KeyError):
        # Evidence is never invented: an unresolvable leaf chain leaves the
        # effect exactly as authored so the runtime gate reports the gap.
        return None
    return {
        "objectCreationLists": sorted(
            (deepcopy(row) for row in resolver.ocls.values()),
            key=lambda row: str(row["id"]).casefold(),
        ),
        "objects": sorted(
            (deepcopy(row) for row in resolver.objects.values()),
            key=lambda row: str(row["id"]).casefold(),
        ),
        "weapons": sorted(
            (deepcopy(row) for row in resolver.weapons.values()),
            key=lambda row: str(row["id"]).casefold(),
        ),
    }


def _ocl_create_object_entries(
    source: bytes, ocl_id: str
) -> tuple[dict[str, object], ...] | None:
    """Return the CreateObject entries of one authored ObjectCreationList."""

    if len(source) > 8 * 1024 * 1024 or b"\0" in source:
        raise PlayableUnitCompilerError(f"{OBJECT_CREATION_LIST_PATH} is unbounded")
    try:
        text = source.decode("cp1252")
    except UnicodeDecodeError as exc:
        raise PlayableUnitCompilerError(
            f"{OBJECT_CREATION_LIST_PATH} has unsupported encoding"
        ) from exc
    header = re.compile(
        r"^ObjectCreationList\s+" + re.escape(ocl_id) + r"\s*$", re.IGNORECASE
    )
    any_header = re.compile(r"^ObjectCreationList\s+\S+\s*$", re.IGNORECASE)
    entries: list[dict[str, object]] = []
    active = False
    depth = 0
    block_count = 0
    current: dict[str, object] | None = None
    for line_number, raw in enumerate(text.splitlines(), start=1):
        line = re.sub(r"\s+", " ", raw.split(";", 1)[0].split("//", 1)[0]).strip()
        if not line:
            continue
        if not active:
            if header.fullmatch(line):
                active = True
                depth = 0
            continue
        if depth == 0 and any_header.fullmatch(line):
            raise PlayableUnitCompilerError(
                f"ObjectCreationList {ocl_id} is unterminated"
            )
        if line.casefold() == "end":
            if depth == 0:
                return tuple(entries)
            if current is not None:
                entries.append(current)
                current = None
            depth -= 1
            continue
        if "=" in line and current is not None:
            key, value = (part.strip() for part in line.split("=", 1))
            fields = current["fields"]
            assert isinstance(fields, list)
            fields.append({"key": key, "value": value, "line": line_number})
            continue
        if "=" not in line:
            if depth == 0 and line.casefold() == "createobject":
                block_count += 1
                if block_count > _MAX_OCL_BLOCKS:
                    raise PlayableUnitCompilerError(
                        f"ObjectCreationList {ocl_id} exceeds the CreateObject limit"
                    )
                current = {"kind": line, "fields": [], "line": line_number}
            elif depth >= 1:
                # Nested sections under CreateObject are outside the summon
                # contract; fail closed instead of silently skipping them.
                raise PlayableUnitCompilerError(
                    f"ObjectCreationList {ocl_id} has an unsupported nested section: {line}"
                )
            depth += 1
    raise PlayableUnitCompilerError(f"ObjectCreationList {ocl_id} is unterminated")


def _modifier_value(token: str, constants: Mapping[str, int | float]) -> float | None:
    """Resolve one authored Modifier magnitude (percent literal, scalar, define)."""

    text = token.strip()
    if text.endswith("%"):
        numeric = text[:-1]
        try:
            return float(numeric) / 100.0
        except ValueError:
            return None
    resolved = _resolved_expression(text, constants)
    if resolved is None:
        # Percent-valued defines live in the reserved percent namespace and
        # resolve only in this percent-aware context, exactly as SAGE scales
        # the substituted ``NNN%`` text at the use site.
        percent = constants.get(text.casefold() + _PERCENT_KEY_SUFFIX)
        if isinstance(percent, float):
            return percent
        return None
    return float(resolved)


def _ability_modifier_leaf(
    modifiers: Mapping[str, IniBlock],
    modifier_id: str,
    constants: Mapping[str, int | float],
    label: str,
) -> dict[str, object]:
    """Resolve one ModifierList leaf to its runtime-applicable rows."""

    block = modifiers.get(modifier_id.casefold())
    if block is None:
        raise PlayableUnitCompilerError(
            f"{label} references a missing ModifierList: {modifier_id}"
        )
    rows: list[dict[str, object]] = []
    unsupported: list[str] = []
    for value in block.values("Modifier"):
        parts = value.split()
        if len(parts) < 2:
            raise PlayableUnitCompilerError(
                f"ModifierList {modifier_id} has a malformed Modifier row: {value!r}"
            )
        kind = parts[0]
        magnitude = _modifier_value(parts[1], constants)
        if magnitude is None:
            raise PlayableUnitCompilerError(
                f"ModifierList {modifier_id} has an unresolvable Modifier value: {value!r}"
            )
        if len(rows) >= _MAX_MODIFIER_ROWS:
            raise PlayableUnitCompilerError(
                f"ModifierList {modifier_id} exceeds the Modifier row limit"
            )
        if kind.casefold() in {item.casefold() for item in _SUPPORTED_MODIFIER_KINDS}:
            rows.append(
                {
                    "kind": kind,
                    "value": magnitude,
                    "application": (
                        "additive" if kind.casefold() == "armor" else "multiplicative"
                    ),
                }
            )
        else:
            unsupported.append(kind)
    duration: int | float | None = None
    duration_values = block.values("Duration")
    if duration_values:
        duration = _resolved_expression(duration_values[-1], constants)
        if duration is None:
            raise PlayableUnitCompilerError(
                f"ModifierList {modifier_id} has an unresolvable Duration"
            )
    leaf: dict[str, object] = {
        "id": block.name,
        "modifiers": rows,
        "sourceIni": ATTRIBUTE_MODIFIER_PATH,
    }
    category = _first(block.values("Category"))
    if category is not None:
        leaf["category"] = category
    if duration is not None:
        leaf["durationMs"] = duration
    fx_ids = [
        token
        for field in ("FX", "FX2", "FX3")
        for value in block.values(field)
        if (token := _first((value,))) is not None
    ]
    if fx_ids:
        leaf["fxIds"] = fx_ids
    if unsupported:
        leaf["unsupportedModifiers"] = sorted(set(unsupported), key=str.casefold)
    return leaf


def _projected_object_filter(
    tokens: Sequence[str],
    filter_defines: Mapping[str, tuple[str, ...]],
) -> str:
    """Project one authored ObjectFilter into the runtime filter grammar.

    Named gamedata #define lists expand in place.  ``ALL`` normalizes to the
    runtime's ``ANY`` superset token.  ``HORDE`` terms drop: retail filters
    exclude the horde *container* because buffs apply per member, while the
    runtime battalion entity proxies those members directly.
    """

    expanded: list[str] = []
    for token in tokens:
        rows = filter_defines.get(token.casefold())
        expanded.extend(rows if rows else (token,))
    projected: list[str] = []
    for token in expanded:
        bare = token.lstrip("+-").casefold()
        if bare == "horde":
            continue
        if bare == "all" and not token.startswith(("+", "-")):
            projected.append("ANY")
            continue
        projected.append(token)
    return " ".join(projected)


def _weapon_warhead_target(
    documents: Mapping[str, bytes],
    identifier: str,
) -> str | None:
    """Unique authored WarheadTemplateName behind a weapon's ProjectileNuggets.

    Some ability weapons (Legolas HawkStrike, Thranduil Thorn of Vengeance)
    carry no DamageNugget of their own: they launch a projectile whose warhead
    holds the damage.  The authored chain is one hop; ambiguity fails closed.
    """

    summary = _weapon_nugget_summary(documents, identifier)
    warheads = summary["warheads"]
    if summary["found"] and len(warheads) == 1:
        return next(iter(warheads.values()))
    return None


# Damage-payload nugget kinds the runtime has no system for yet, annotated
# with the system each one needs so a recorded gap names its blocker.
_UNSUPPORTED_DAMAGE_NUGGET_SYSTEMS = {
    "firelogicnugget": "needs fire ignition/burn-rate logic",
    "weaponoclnugget": "needs weapon-spawned object (OCL) payloads",
    "dotnugget": "needs damage-over-time",
    "paralyzenugget": "needs paralysis status",
    "grabnugget": "needs grab/passenger carry",
    "specialmodelconditionnugget": "needs scripted model-condition status",
    "attributemodifiernugget": "needs weapon-applied attribute modifiers",
    "emotionweaponnugget": "needs weapon-applied emotion push",
    "metaimpactnugget": "knockback-only payload",
    "projectilenugget": "projectile warhead chain",
}


def _annotated_nugget_kind(kind: str) -> str:
    annotation = _UNSUPPORTED_DAMAGE_NUGGET_SYSTEMS.get(kind.casefold())
    return f"{kind} ({annotation})" if annotation else kind


def _weapon_nugget_summary(
    documents: Mapping[str, bytes],
    identifier: str,
) -> dict[str, object]:
    """Bounded scan of one weapon's nugget sections (kinds + warhead targets)."""

    header = re.compile(rf"^Weapon\s+{re.escape(identifier)}\s*$", re.IGNORECASE)
    nugget_header = re.compile(r"^([A-Za-z]+Nugget)\s*$", re.IGNORECASE)
    kinds: dict[str, str] = {}
    warheads: dict[str, str] = {}
    found = False
    for path, payload in sorted(documents.items(), key=lambda item: item[0].casefold()):
        try:
            lines = payload.decode("cp1252").splitlines()
        except UnicodeDecodeError:
            continue
        active = False
        depth = 0
        for raw in lines:
            stripped = raw.strip()
            clean = stripped.split(";", 1)[0].split("//", 1)[0].strip()
            if not active:
                if raw.lstrip() == raw and header.fullmatch(clean):
                    active = True
                    found = True
                continue
            if depth == 0 and raw.lstrip() == raw and clean.casefold() == "end":
                break
            if not clean:
                continue
            if clean.casefold() == "end" and depth:
                depth -= 1
                continue
            if "=" in clean and depth >= 1:
                key, expression = (part.strip() for part in clean.split("=", 1))
                if key.casefold() == "warheadtemplatename" and expression:
                    token = expression.split()[0]
                    warheads.setdefault(token.casefold(), token)
                continue
            if "=" not in clean:
                if depth == 0:
                    nugget = nugget_header.fullmatch(clean)
                    if nugget is not None:
                        kinds.setdefault(nugget.group(1).casefold(), nugget.group(1))
                depth += 1
    return {"found": found, "kinds": kinds, "warheads": warheads}


def _weapon_ocl_nugget_names(
    documents: Mapping[str, bytes],
    weapon_id: str,
    *,
    cache: dict[tuple[str, str], dict[str, list[dict[str, object]]] | None] | None,
    cache_lock: threading.Lock | None,
) -> list[str] | None:
    """The ObjectCreationList ids one weapon's WeaponOCLNuggets name.

    ``None`` when the weapon authors no WeaponOCLNugget at all, or when any
    nugget's ``WeaponOCLName`` is missing/ambiguous (fail-closed: the caller
    then treats the weapon as a plain damage weapon).
    """

    nuggets = _weapon_damage_nuggets(
        documents,
        weapon_id,
        nugget_kind="weaponoclnugget",
        cache=cache,
        cache_lock=cache_lock,
    )
    if not nuggets:
        return None
    names: list[str] = []
    for nugget in nuggets:
        fields = nugget["fields"]
        assert isinstance(fields, Mapping)
        rows = fields.get("weaponoclname") or ()
        if len(rows) != 1:
            return None
        tokens = _tokens(str(rows[0]["expression"]))
        if len(tokens) != 1 or tokens[0].casefold() in {"none", "null"}:
            return None
        names.append(tokens[0])
    return names


def _weapon_knockback_fields(
    documents: Mapping[str, bytes],
    weapon_ids: Sequence[str],
    constants: Mapping[str, int | float],
    label: str,
    limitations: list[str],
    *,
    named_definition_cache: dict[tuple[str, str], dict[str, list[dict[str, object]]] | None] | None,
    cache_lock: threading.Lock | None,
) -> dict[str, object]:
    """Authored MetaImpactNugget shockwave of one ability weapon chain.

    ShockWaveAmount/ShockWaveRadius map to knockbackStrength/knockbackRadius
    in source units (the runtime scales them exactly like attack ranges).
    The first weapon in the chain that authors a MetaImpactNugget wins; an
    ambiguous or unresolvable nugget records a limitation and emits nothing
    (fail-closed: the blast keeps dealing damage without a shockwave).
    """

    for weapon_id in weapon_ids:
        nuggets = _weapon_damage_nuggets(
            documents,
            weapon_id,
            nugget_kind="metaimpactnugget",
            cache=named_definition_cache,
            cache_lock=cache_lock,
        )
        if not nuggets:
            continue
        if len(nuggets) > 1:
            limitations.append(
                f"weapon {weapon_id} authors multiple MetaImpactNuggets "
                "(ambiguous shockwave, not applied)"
            )
            return {}
        fields = nuggets[0]["fields"]
        assert isinstance(fields, Mapping)
        strength = _resolved_definition_field(fields, "ShockWaveAmount", constants)
        radius = _resolved_definition_field(fields, "ShockWaveRadius", constants)
        if (
            strength is None
            or radius is None
            or float(strength["value"]) <= 0.0
            or float(radius["value"]) <= 0.0
        ):
            limitations.append(
                f"weapon {weapon_id} MetaImpactNugget has no resolvable "
                "ShockWaveAmount/ShockWaveRadius (shockwave not applied)"
            )
            return {}
        return {
            "knockbackStrength": strength["value"],
            "knockbackRadius": radius["value"],
            "knockbackWeaponId": weapon_id,
        }
    return {}


def _ability_weapon_leaf(
    documents: Mapping[str, bytes],
    weapon_id: str,
    constants: Mapping[str, int | float],
    label: str,
    *,
    named_definition_cache: dict[tuple[str, str], dict[str, list[dict[str, object]]] | None] | None,
    cache_lock: threading.Lock | None,
) -> dict[str, object]:
    """Resolve one ability Weapon to its base damage, radius, and range."""

    definition = _named_definition_values(
        documents,
        "Weapon",
        weapon_id,
        cache=named_definition_cache,
        cache_lock=cache_lock,
    )
    if definition is None:
        raise PlayableUnitCompilerError(
            f"{label} references a missing or ambiguous Weapon: {weapon_id}"
        )
    base = _base_weapon_damage(
        documents,
        weapon_id,
        constants,
        cache=named_definition_cache,
        cache_lock=cache_lock,
    )
    damage_weapon_ids = [weapon_id]
    if base is None:
        # ProjectileNugget warhead chain: the launcher authors no damage, its
        # authored warheads do.  Retail fires every ProjectileNugget per
        # shot, so every distinct warhead must resolve and their base
        # damages combine.  One bounded hop, never recursive.
        summary = _weapon_nugget_summary(documents, weapon_id)
        warhead_ids = sorted(summary["warheads"].values(), key=str.casefold)
        if not warhead_ids:
            unsupported_kinds = sorted(
                (
                    kind
                    for key, kind in summary["kinds"].items()
                    if key != "damagenugget"
                ),
                key=str.casefold,
            )
            detail = (
                f"; its damage payload uses unsupported nugget kinds: "
                + ", ".join(
                    _annotated_nugget_kind(kind) for kind in unsupported_kinds
                )
                if unsupported_kinds
                else ""
            )
            raise PlayableUnitCompilerError(
                f"{label} weapon {weapon_id} has no resolvable base DamageNugget "
                f"damage and no authored warhead{detail}"
            )
        warhead_bases: list[dict[str, object]] = []
        for warhead_id in warhead_ids:
            warhead_base = _base_weapon_damage(
                documents,
                warhead_id,
                constants,
                cache=named_definition_cache,
                cache_lock=cache_lock,
            )
            if warhead_base is None:
                warhead_summary = _weapon_nugget_summary(documents, warhead_id)
                unsupported_kinds = sorted(
                    (
                        kind
                        for key, kind in warhead_summary["kinds"].items()
                        if key != "damagenugget"
                    ),
                    key=str.casefold,
                )
                detail = (
                    "; its damage payload uses unsupported nugget kinds: "
                    + ", ".join(
                        _annotated_nugget_kind(kind) for kind in unsupported_kinds
                    )
                    if unsupported_kinds
                    else ""
                )
                raise PlayableUnitCompilerError(
                    f"{label} weapon {weapon_id} warhead {warhead_id} has no "
                    f"resolvable base DamageNugget damage{detail}"
                )
            warhead_bases.append(warhead_base)
        combined_components: list[dict[str, object]] = []
        combined_excluded: list[dict[str, object]] = []
        for warhead_base in warhead_bases:
            combined_components.extend(warhead_base["components"])  # type: ignore[arg-type]
            combined_excluded.extend(warhead_base.get("excludedNuggets", ()))  # type: ignore[arg-type]
        base = {
            "value": sum(
                warhead_base["value"] for warhead_base in warhead_bases  # type: ignore[misc]
            ),
            "components": combined_components,
        }
        if combined_excluded:
            base["excludedNuggets"] = combined_excluded
        damage_weapon_ids = list(warhead_ids)
    radius = 0.0
    damage_scalar_authored = False
    for damage_weapon_id in damage_weapon_ids:
        nuggets = _weapon_damage_nuggets(
            documents,
            damage_weapon_id,
            cache=named_definition_cache,
            cache_lock=cache_lock,
        ) or []
        for nugget in nuggets:
            fields = nugget["fields"]
            assert isinstance(fields, Mapping)
            if fields.get("requiredupgradenames") or fields.get("specialobjectfilter"):
                continue
            if fields.get("damagescalar"):
                damage_scalar_authored = True
            resolved = _resolved_definition_field(fields, "Radius", constants)
            if resolved is not None:
                radius = max(radius, float(resolved["value"]))
    damage_types = sorted(
        {
            str(component["damageType"])
            for component in base["components"]
            if component.get("damageType")
        },
        key=str.casefold,
    )
    leaf: dict[str, object] = {
        "id": weapon_id,
        "damage": base["value"],
        "damageRadius": radius,
        "components": list(base["components"]),
        "sourceIni": WEAPON_PATH,
    }
    if damage_weapon_ids != [weapon_id]:
        if len(damage_weapon_ids) == 1:
            leaf["warheadId"] = damage_weapon_ids[0]
        else:
            leaf["warheadIds"] = list(damage_weapon_ids)
    if damage_scalar_authored:
        leaf["damageScalarAuthored"] = True
    if len(damage_types) == 1:
        leaf["damageType"] = damage_types[0]
    if base.get("excludedNuggets"):
        leaf["excludedNuggets"] = list(base["excludedNuggets"])
    attack_range = _resolved_definition_field(definition, "AttackRange", constants)
    if attack_range is not None:
        leaf["attackRange"] = attack_range["value"]
    fire_fx = [
        row
        for row in definition.get("firefx", [])
        if str(row.get("expression", "")).strip()
    ]
    if fire_fx:
        leaf["fireFxId"] = str(fire_fx[0]["expression"]).strip()
    return leaf


def _weapon_mode_profile(
    documents: Mapping[str, bytes],
    weapon_id: str,
    constants: Mapping[str, int | float],
    label: str,
    *,
    named_definition_cache: dict[tuple[str, str], dict[str, list[dict[str, object]]] | None] | None,
    cache_lock: threading.Lock | None,
) -> dict[str, object]:
    """Resolve one conditioned Weapon into a full runtime combat profile.

    The runtime weapon-mode table needs the same fields the default combat
    contract carries (range, cadence, damage, clip behavior) so a toggled or
    mounted mode can swap the battalion's live combat stats faithfully.
    Raises PlayableUnitCompilerError when the authored weapon cannot resolve
    into a complete profile — a mode is either fully swappable or a recorded
    gap, never a partial stat swap.
    """

    definition = _named_definition_values(
        documents,
        "Weapon",
        weapon_id,
        cache=named_definition_cache,
        cache_lock=cache_lock,
    )
    if definition is None:
        raise PlayableUnitCompilerError(
            f"{label} references a missing or ambiguous Weapon: {weapon_id}"
        )
    profile: dict[str, object] = {"weaponId": weapon_id}
    for output_name, source_name in (
        ("attackRange", "AttackRange"),
        ("minimumAttackRange", "MinimumAttackRange"),
        ("delayBetweenShotsMs", "DelayBetweenShots"),
        ("preAttackDelayMs", "PreAttackDelay"),
        ("firingDurationMs", "FiringDuration"),
        ("damage", "Damage"),
        ("clipSize", "ClipSize"),
        ("clipReloadTimeMs", "ClipReloadTime"),
        ("continuousFireOne", "ContinuousFireOne"),
        ("continuousFireCoastMs", "ContinuousFireCoast"),
    ):
        field = _resolved_definition_field(definition, source_name, constants)
        if field is None:
            # Hero weapons author macro products (FARAMIR_DAMAGE * 0.4).
            field = _resolved_definition_field(
                definition,
                source_name,
                constants,
                resolve=_resolved_multiplicative_expression,
            )
        if field is not None:
            profile[output_name] = field
    if "damage" not in profile:
        nugget_damage = _base_weapon_damage(
            documents,
            weapon_id,
            constants,
            cache=named_definition_cache,
            cache_lock=cache_lock,
        )
        if nugget_damage is None:
            raise PlayableUnitCompilerError(
                f"{label} weapon {weapon_id} has no resolvable Damage"
            )
        profile["damage"] = nugget_damage
    if "attackRange" not in profile:
        raise PlayableUnitCompilerError(
            f"{label} weapon {weapon_id} has no resolvable AttackRange"
        )
    for output_name, source_name in (
        ("delayBetweenShotsMs", "DelayBetweenShots"),
        ("preAttackDelayMs", "PreAttackDelay"),
        ("firingDurationMs", "FiringDuration"),
    ):
        if output_name in profile:
            continue
        if definition.get(source_name.casefold()):
            raise PlayableUnitCompilerError(
                f"{label} weapon {weapon_id} has an unresolvable {source_name}"
            )
        # SAGE defaults unauthored cadence fields to 0 ms; record explicitly.
        profile[output_name] = {
            "value": 0,
            "semantic": (
                f"{source_name} is not authored; the SAGE engine default is 0 ms"
            ),
        }
    damage_types = {
        str(row.get("expression", "")).casefold(): str(row.get("expression", ""))
        for row in definition.get("damagetype", ())
        if str(row.get("expression", ""))
    }
    if len(damage_types) == 1:
        profile["damageType"] = next(iter(damage_types.values()))
    profile["sourceIni"] = WEAPON_PATH
    return profile


def _conditioned_weapon_set_target(block: SageBlock, label: str) -> str:
    """The single authored weapon of one conditioned WeaponSet block."""

    weapon_ids = {
        tokens[-1].casefold(): tokens[-1]
        for assignment in block.assignments
        if assignment.key.casefold() == "weapon"
        for tokens in [_tokens(assignment.value)]
        if tokens
    }
    if len(weapon_ids) != 1:
        raise PlayableUnitCompilerError(
            f"{label} conditioned WeaponSet names an ambiguous weapon set"
        )
    return next(iter(weapon_ids.values()))


def _conditional_weapon_modes(
    member_lineage: Sequence[SageObject],
    documents: Mapping[str, bytes],
    constants: Mapping[str, int | float],
    *,
    named_definition_cache: dict[tuple[str, str], dict[str, list[dict[str, object]]] | None] | None,
    cache_lock: threading.Lock | None,
) -> tuple[dict[str, object], list[dict[str, str]]]:
    """Compile every single-condition alternate WeaponSet into a mode profile.

    Retail alternate stances author WeaponSet blocks conditioned on exactly one
    positive flag (WEAPONSET_TOGGLE_1 for weapon toggles, MOUNTED for mounted
    heroes).  Each such set compiles into a keyed runtime weapon-mode profile;
    an unresolvable or ambiguous set is a recorded gap, never a partial swap.
    """

    modes: dict[str, object] = {}
    gaps: list[dict[str, str]] = []
    dropped: set[str] = set()
    for block in _effective_top_blocks(member_lineage):
        if (block.header_key or block.kind).casefold() != "weaponset":
            continue
        positives = sorted(
            {
                token.casefold()
                for assignment in block.assignments
                if assignment.key.casefold() in {"condition", "conditions"}
                for token in _tokens(assignment.value)
                if not token.startswith("-")
                and token.casefold() not in {"none", "set_normal"}
            }
        )
        if not positives:
            continue  # the base stance rides the combat contract
        if len(positives) != 1:
            gaps.append(
                {
                    "mode": " ".join(positives),
                    "reason": "WeaponSet authors multiple positive conditions",
                }
            )
            continue
        mode_key = positives[0]
        label = f"weapon mode {mode_key}"
        if mode_key in modes or mode_key in dropped:
            if mode_key in modes:
                del modes[mode_key]
            if mode_key not in dropped:
                dropped.add(mode_key)
                gaps.append(
                    {
                        "mode": mode_key,
                        "reason": "multiple WeaponSet blocks author this condition",
                    }
                )
            continue
        try:
            weapon_id = _conditioned_weapon_set_target(block, label)
            profile = _weapon_mode_profile(
                documents,
                weapon_id,
                constants,
                label,
                named_definition_cache=named_definition_cache,
                cache_lock=cache_lock,
            )
            weapon_slot = _weapon_slot_for_target(block, weapon_id)
            if weapon_slot is not None:
                profile["weaponSlot"] = weapon_slot
            modes[mode_key] = profile
        except PlayableUnitCompilerError as error:
            gaps.append({"mode": mode_key, "reason": str(error)})
    return modes, gaps


def _leadership_aura_effect(
    label: str,
    behavior_modules: Sequence[SageBlock],
    gate_upgrades: Sequence[str],
    gate_resolved: bool,
    modifier_blocks: Mapping[str, IniBlock],
    constants: Mapping[str, int | float],
    filter_defines: Mapping[str, tuple[str, ...]],
    limitations: list[str],
) -> dict[str, object] | None:
    """Bind one passive leadership button to its AttributeModifierAuraUpdate.

    Retail aura modules carry no SpecialPowerTemplate; the authored join is
    the shared TriggeredBy upgrade between the button's
    UnpauseSpecialPowerUpgrade gate and the aura module.  StartsActive=No
    auras whose TriggeredBy upgrades resolve through the hero's authored
    ExperienceLevel grants ride the row's level gate; an unresolvable gate
    keeps the aura off (startsActive false).  Returns None when no aura
    module binds; raises on ambiguous or unconvertible authoring.
    """

    candidates = [
        block
        for block in behavior_modules
        if block.kind.casefold() == "attributemodifierauraupdate"
    ]
    gates = {upgrade.casefold() for upgrade in gate_upgrades}
    if gates:
        matched = [
            block
            for block in candidates
            if gates
            & {token.casefold() for token in _module_tokens(block, "TriggeredBy")}
        ]
    else:
        matched = [
            block
            for block in candidates
            if not _module_tokens(block, "TriggeredBy")
            and (_first(block.values("StartsActive")) or "yes").casefold() == "yes"
        ]
    if not matched:
        return None
    if len(matched) > 1:
        raise PlayableUnitCompilerError(
            f"{label} matches multiple AttributeModifierAuraUpdate modules"
        )
    block = matched[0]
    bonus_tokens = _module_tokens(block, "BonusName")
    if len(bonus_tokens) != 1:
        raise PlayableUnitCompilerError(
            f"{label} aura must name exactly one BonusName"
        )
    bonus_name = bonus_tokens[0]
    leaf = _ability_modifier_leaf(modifier_blocks, bonus_name, constants, label)
    supported = list(leaf.get("modifiers", []))
    if not supported:
        unsupported = leaf.get("unsupportedModifiers", [])
        raise PlayableUnitCompilerError(
            f"{label} aura {bonus_name} has no runtime-supported Modifier rows"
            + (
                " (unsupported: %s)" % ", ".join(unsupported)  # type: ignore[arg-type]
                if unsupported
                else ""
            )
        )
    aura_range = _resolved_expression((block.values("Range") or ("",))[-1], constants)
    if aura_range is None or float(aura_range) <= 0.0:
        raise PlayableUnitCompilerError(f"{label} aura has no resolvable Range")
    starts_active_authored = (
        _first(block.values("StartsActive")) or "yes"
    ).casefold() == "yes"
    # StartsActive=No means the aura waits for its TriggeredBy upgrade; when
    # that upgrade is an authored hero level grant, the row's level gate is
    # the faithful runtime activation and the aura radiates once the hero
    # reaches the authored rank.  An unresolvable gate keeps the aura off.
    starts_active = starts_active_authored or (bool(gates) and gate_resolved)
    if not starts_active:
        limitations.append(
            "aura upgrade gate does not resolve to an authored hero level "
            "grant; the aura stays off"
        )
    effect: dict[str, object] = {
        "kind": "leadership-aura",
        "bonusName": bonus_name,
        "range": aura_range,
        "modifiers": supported,
        "affectsSelf": (_first(block.values("AllowSelf")) or "no").casefold() == "yes",
        "startsActive": starts_active,
        "sourceIni": block.source_virtual_path,
        "line": block.line,
        "modifierSourceIni": ATTRIBUTE_MODIFIER_PATH,
    }
    filter_tokens = _module_tokens(block, "ObjectFilter")
    if filter_tokens:
        effect["affects"] = _projected_object_filter(filter_tokens, filter_defines)
    if leaf.get("category"):
        effect["category"] = leaf["category"]
    if leaf.get("fxIds"):
        effect["fxIds"] = leaf["fxIds"]
    if leaf.get("unsupportedModifiers"):
        limitations.append(
            "modifier kinds not applied by the runtime: "
            + ", ".join(leaf["unsupportedModifiers"])  # type: ignore[arg-type]
        )
    for anti in _module_tokens(block, "AntiCategory"):
        limitations.append(f"anti-category strip ({anti}) is not applied")
    return effect


def _weapon_toggle_row(
    slot: int,
    button: IniBlock,
    member_lineage: Sequence[SageObject],
    documents: Mapping[str, bytes],
    constants: Mapping[str, int | float],
    *,
    named_definition_cache: dict[tuple[str, str], dict[str, list[dict[str, object]]] | None] | None,
    cache_lock: threading.Lock | None,
) -> dict[str, object]:
    """Convert one TOGGLE_WEAPONSET command into an explicit ability row.

    The authored toggle (FlagsUsedForToggle + the WeaponSet conditioned on
    that flag) compiles into a castable weapon-toggle effect when the toggled
    weapon resolves into a complete runtime profile — the same profile the
    simulation contract publishes under ``weaponModes`` for the runtime unit
    rule to swap live combat stats.  An unresolvable toggle stays a recorded
    gap instead of a castable toggle that could never engage — fail-closed,
    never faked.
    """

    label = f"ability {button.name}"
    limitations: list[str] = []
    toggle_profile: dict[str, object] | None = None
    row: dict[str, object] = {
        "id": button.name,
        "slot": slot,
        "command": "TOGGLE_WEAPONSET",
        "specialPowerId": "",
        "button": _ability_button_leaf_fields(button),
        "targeting": "self",
        "modules": [],
        "sourceIni": COMMAND_BUTTON_PATH,
    }
    reason = (
        "the authored toggle contract did not resolve into a complete "
        "runtime weapon-mode profile"
    )
    evidence: dict[str, object] = {}
    flags = _module_tokens(button, "FlagsUsedForToggle")
    if len(flags) != 1:
        reason = f"{label} must author exactly one FlagsUsedForToggle flag"
    else:
        flag = flags[0]
        evidence["toggleFlag"] = flag
        evidence["toggleMode"] = flag.casefold()
        toggled_sets = []
        exact_sets = []
        for block in _effective_top_blocks(member_lineage):
            if (block.header_key or block.kind).casefold() != "weaponset":
                continue
            positive_tokens = {
                token.casefold()
                for assignment in block.assignments
                if assignment.key.casefold() in {"condition", "conditions"}
                for token in _tokens(assignment.value)
                if not token.startswith("-")
                and token.casefold() not in {"none", "set_normal"}
            }
            if flag.casefold() in positive_tokens:
                toggled_sets.append(block)
                # Retail best-match selection: engaging the toggle sets only
                # the toggle flag, so the WeaponSet conditioned on exactly
                # that flag is the toggled base state; sets that add further
                # conditions (CONTAINED, CLOSE_RANGE, HERO_MODE) are other
                # states layered on top.
                if positive_tokens == {flag.casefold()}:
                    exact_sets.append(block)
        if len(exact_sets) == 1:
            toggled_sets = exact_sets
        if len(toggled_sets) != 1:
            reason = (
                f"{label} matches {len(toggled_sets)} WeaponSet blocks for "
                f"{flag} and none is conditioned on exactly that flag "
                "(need one base toggled state)"
                if len(exact_sets) != 1
                else f"{label} matches {len(toggled_sets)} WeaponSet blocks for "
                f"{flag} (need exactly one)"
            )
        else:
            toggled = toggled_sets[0]
            weapon_ids = {
                tokens[-1].casefold(): tokens[-1]
                for assignment in toggled.assignments
                if assignment.key.casefold() == "weapon"
                for tokens in [_tokens(assignment.value)]
                if tokens
            }
            default_weapon = _default_set_target(
                member_lineage, "WeaponSet", "Weapon"
            )
            if default_weapon is not None:
                evidence["defaultWeaponId"] = default_weapon
            if len(weapon_ids) != 1:
                reason = (
                    f"{label} toggled WeaponSet names an ambiguous weapon set"
                )
            else:
                toggled_weapon = next(iter(weapon_ids.values()))
                evidence["toggledWeaponId"] = toggled_weapon
                try:
                    evidence["toggledWeapon"] = _ability_weapon_leaf(
                        documents,
                        toggled_weapon,
                        constants,
                        label,
                        named_definition_cache=named_definition_cache,
                        cache_lock=cache_lock,
                    )
                except PlayableUnitCompilerError as error:
                    limitations.append(str(error))
                try:
                    toggle_profile = _weapon_mode_profile(
                        documents,
                        toggled_weapon,
                        constants,
                        label,
                        named_definition_cache=named_definition_cache,
                        cache_lock=cache_lock,
                    )
                except PlayableUnitCompilerError as error:
                    reason = str(error)
    if evidence:
        row["weaponToggle"] = evidence
    if toggle_profile is not None:
        # Castable toggle: the runtime pins combat to the compiled weapon-mode
        # profile keyed by the authored flag (published via the simulation
        # contract's weaponModes) until the player toggles back.  Retail
        # weapon toggles author no SpecialPower, so no reload gates the flip.
        row["effect"] = {
            "kind": "weapon-toggle",
            "toggleMode": str(evidence["toggleMode"]),
            "toggledWeaponId": str(evidence["toggledWeaponId"]),
            "sourceIni": COMMAND_BUTTON_PATH,
        }
        row["cooldownMs"] = 0
        row["implementation"] = {
            "status": "implemented",
            "reason": "",
            "limitations": limitations,
        }
        return row
    row["effect"] = {"kind": "none"}
    row["implementation"] = {
        "status": "unimplemented",
        "reason": reason,
        "limitations": limitations,
    }
    return row


def _hero_abilities(
    target: SageObject,
    target_lineage: Sequence[SageObject],
    member_lineage: Sequence[SageObject],
    command_sets: Mapping[str, IniBlock],
    command_buttons: Mapping[str, IniBlock],
    objects: Mapping[str, SageObject],
    documents: Mapping[str, bytes],
    constants: Mapping[str, int | float],
    *,
    named_definition_cache: dict[tuple[str, str], dict[str, list[dict[str, object]]] | None] | None,
    cache_lock: threading.Lock | None,
    button_overrides: Sequence[str] | None = None,
    extra_power_documents: Sequence[str] = (),
) -> tuple[list[dict[str, object]], dict[str, IniBlock]]:
    """Emit the converted SPECIAL_POWER ability rows for one hero.

    Returns the ability rows plus the authored SpecialPower blocks they
    consumed (for descriptor provenance).

    ``button_overrides`` names the CommandButtons to compile instead of reading
    them off the object's CommandSet.  Create-a-Hero needs this and nothing else
    does: its object's CommandSet holds only Attack-Move and Stop, because the
    player's chosen powers are inserted into ``CreateAHeroCommandSetTemplate``
    at runtime rather than authored into a set.  The powers are real and their
    modules are on the object; only the *list* lives somewhere else.

    ``extra_power_documents`` names further ``SpecialPower`` documents to merge
    over ``specialpower.ini`` -- Create-a-Hero keeps its templates in
    ``createaherospecialpowers.ini``.
    """

    command_set: IniBlock | None = None
    if button_overrides is None:
        command_values = [
            value
            for row in _effective_values(target_lineage, "CommandSet")
            if (value := _first((row.value,))) is not None
        ]
        if not command_values:
            return [], {}
        command_set = command_sets.get(command_values[0].casefold())
        if command_set is None:
            return [], {}

    # Gather the hero's Behavior modules once; SPECIAL_POWER buttons then bind
    # the modules that reference their SpecialPower template.
    lineages: list[Sequence[SageObject]] = [target_lineage]
    if member_lineage[-1].name.casefold() != target_lineage[-1].name.casefold():
        lineages.append(member_lineage)
    behavior_modules: list[SageBlock] = []
    for lineage in lineages:
        behavior_modules.extend(
            block
            for block in _effective_top_blocks(lineage)
            if (block.header_key or "").casefold() == "behavior"
        )
    # Object-body #include directives (retail's shared CaptureBuilding.inc)
    # author Behavior modules the plain object parse leaves as unexpanded
    # refs.  Expand each authored include one level deep against the supplied
    # document view so those modules bind like any other authored Behavior;
    # a missing or unparsable include contributes nothing (fail-closed: the
    # dependent ability then stays a recorded gap).
    included_paths: set[str] = set()
    for lineage in lineages:
        for item in lineage:
            for ref in item.includes:
                include_path = str(ref.relative_virtual_path)
                folded = include_path.casefold()
                if folded in included_paths:
                    continue
                included_paths.add(folded)
                payload = _optional_document(documents, include_path)
                if payload is None:
                    continue
                try:
                    fragment = parse_sage_body_fragment(payload, include_path)
                except SageCstError:
                    continue
                behavior_modules.extend(
                    block
                    for block in fragment.items
                    if isinstance(block, SageBlock)
                    and (block.header_key or "").casefold() == "behavior"
                )
    hero_names = frozenset(
        item.name.casefold() for lineage in lineages for item in lineage
    )
    burst_heal_modules = [
        block
        for block in behavior_modules
        if block.kind.casefold() == "autohealbehavior"
        and (_first(block.values("ButtonTriggered")) or "").casefold() == "yes"
        and (_first(block.values("SingleBurst")) or "").casefold() == "yes"
    ]

    special_power_source = _optional_document(documents, SPECIAL_POWER_PATH)
    power_blocks: dict[str, IniBlock] = {}
    if special_power_source is not None:
        power_blocks = _named_blocks(special_power_source, "SpecialPower")
    for extra_path in extra_power_documents:
        extra_source = _optional_document(documents, extra_path)
        if extra_source is not None:
            power_blocks.update(_named_blocks(extra_source, "SpecialPower"))
    modifier_source = _optional_document(documents, ATTRIBUTE_MODIFIER_PATH)
    modifier_blocks: dict[str, IniBlock] = {}
    if modifier_source is not None:
        modifier_blocks = _named_blocks(modifier_source, "ModifierList")
    experience_source = _optional_document(documents, EXPERIENCE_LEVELS_PATH)
    level_grants: dict[str, int] = {}
    if experience_source is not None:
        level_grants = _hero_level_grants(
            _experience_level_rows(experience_source),
            _ability_list_defines(experience_source),
            hero_names,
        )
    ocl_source = _optional_document(documents, OBJECT_CREATION_LIST_PATH)
    gamedata_source = _optional_document(documents, GAME_DATA_PATH)
    filter_defines: Mapping[str, tuple[str, ...]] = (
        _ability_list_defines(gamedata_source) if gamedata_source is not None else {}
    )

    abilities: list[dict[str, object]] = []
    used_power_blocks: dict[str, IniBlock] = {}
    if button_overrides is None:
        slot_bindings = list(_command_slots(command_set))
        binding_source = f"CommandSet {command_set.name}"
    else:
        # Slots are 1-based and follow the given order, which is the numbering
        # the Create-a-Hero Current Powers list shows.
        slot_bindings = list(enumerate(button_overrides, start=1))
        binding_source = "the Create-a-Hero power list"
    for slot, command_id in slot_bindings:
        button = command_buttons.get(command_id.casefold())
        if button is None:
            raise PlayableUnitCompilerError(
                f"{binding_source} references a missing CommandButton: {command_id}"
            )
        command_kinds = {
            value.strip().casefold() for value in button.values("Command")
        }
        if command_kinds == {"toggle_weaponset"}:
            # Weapon-mode toggles are ability surface too: record the authored
            # toggle contract explicitly instead of silently skipping it.
            abilities.append(
                _weapon_toggle_row(
                    slot,
                    button,
                    member_lineage,
                    documents,
                    constants,
                    named_definition_cache=named_definition_cache,
                    cache_lock=cache_lock,
                )
            )
            continue
        if command_kinds != {"special_power"}:
            continue
        ability = _hero_ability_row(
            slot,
            button,
            command_buttons,
            power_blocks,
            modifier_blocks,
            ocl_source,
            level_grants,
            experience_source is not None,
            behavior_modules,
            burst_heal_modules,
            objects,
            documents,
            constants,
            filter_defines,
            member_lineage,
            named_definition_cache=named_definition_cache,
            cache_lock=cache_lock,
        )
        power_block = power_blocks.get(str(ability["specialPowerId"]).casefold())
        if power_block is not None:
            used_power_blocks[power_block.name.casefold()] = power_block
        abilities.append(ability)
    return abilities, used_power_blocks


#: The Create-a-Hero Object, and where its SpecialPower templates live.
CREATE_A_HERO_OBJECT = "CreateAHero"
CREATE_A_HERO_SPECIAL_POWER_PATH = "data/ini/createaherospecialpowers.ini"


def compile_create_a_hero_ability_effects(
    documents: Mapping[str, bytes],
    button_names: Sequence[str],
    *,
    prepared: "PlayableUnitCompilerInputs | None" = None,
) -> dict[str, dict[str, object]]:
    """``CommandButton name -> its compiled ability row`` for Create-a-Hero.

    WHAT THIS BUYS. Without it a created hero's powers are a list of names with
    icons and level gates and no behaviour, because the *effect* of a power is
    not on the button -- it is in the ``SpecialPower`` behaviour modules of the
    ``CreateAHero`` Object, which ``createaheropowers.inc`` contributes through
    an ``#include``.  That is the same place every retail hero's effects live,
    so this reuses :func:`_hero_abilities` rather than growing a second effect
    compiler that could disagree with the first.

    Two things make Create-a-Hero different, and both are passed through rather
    than special-cased downstream:

    * its Object's ``CommandSet`` holds only Attack-Move and Stop, because the
      player's chosen powers are inserted into ``CreateAHeroCommandSetTemplate``
      at runtime.  So the button list is supplied instead of read off a set.
    * its ``SpecialPower`` templates are in ``createaherospecialpowers.ini``
      rather than ``specialpower.ini``.

    Returns only the buttons that compiled; a button this lane cannot resolve is
    omitted, and the caller reports the power as effect-less rather than the
    whole catalog failing.
    """

    if prepared is None:
        prepared = prepare_playable_unit_compiler(documents)
    target = prepared.objects.get(CREATE_A_HERO_OBJECT.casefold())
    if target is None:
        raise PlayableUnitCompilerError(
            f"effective Object is missing: {CREATE_A_HERO_OBJECT}"
        )
    lineage = _ancestry(prepared.objects, target)
    known = [
        name
        for name in button_names
        if prepared.command_buttons.get(name.casefold()) is not None
    ]
    rows, _ = _hero_abilities(
        target,
        lineage,
        lineage,
        prepared.command_sets,
        prepared.command_buttons,
        prepared.objects,
        documents,
        prepared.numeric_defines,
        named_definition_cache=prepared.named_definition_cache,
        cache_lock=prepared.cache_lock,
        button_overrides=known,
        extra_power_documents=(CREATE_A_HERO_SPECIAL_POWER_PATH,),
    )
    return {str(row["id"]): row for row in rows if row.get("id")}


def _hero_ability_row(
    slot: int,
    button: IniBlock,
    command_buttons: Mapping[str, IniBlock],
    power_blocks: Mapping[str, IniBlock],
    modifier_blocks: Mapping[str, IniBlock],
    ocl_source: bytes | None,
    level_grants: Mapping[str, int],
    experience_levels_available: bool,
    behavior_modules: Sequence[SageBlock],
    burst_heal_modules: Sequence[SageBlock],
    objects: Mapping[str, SageObject],
    documents: Mapping[str, bytes],
    constants: Mapping[str, int | float],
    filter_defines: Mapping[str, tuple[str, ...]],
    member_lineage: Sequence[SageObject],
    *,
    named_definition_cache: dict[tuple[str, str], dict[str, list[dict[str, object]]] | None] | None,
    cache_lock: threading.Lock | None,
) -> dict[str, object]:
    label = f"ability {button.name}"
    power_tokens = _module_tokens(button, "SpecialPower")
    if len(power_tokens) != 1:
        raise PlayableUnitCompilerError(
            f"{label} must name exactly one SpecialPower template"
        )
    power_id = power_tokens[0]
    templates = {power_id.casefold()}
    trigger_tokens = _module_tokens(button, "CommandTrigger")
    if len(trigger_tokens) > 1:
        raise PlayableUnitCompilerError(
            f"{label} has an ambiguous CommandTrigger chain"
        )
    if trigger_tokens:
        trigger_button = command_buttons.get(trigger_tokens[0].casefold())
        if trigger_button is None:
            raise PlayableUnitCompilerError(
                f"{label} CommandTrigger references a missing CommandButton: "
                f"{trigger_tokens[0]}"
            )
        trigger_kinds = {
            value.strip().casefold() for value in trigger_button.values("Command")
        }
        trigger_powers = _module_tokens(trigger_button, "SpecialPower")
        if trigger_kinds != {"special_power"} or len(trigger_powers) != 1:
            raise PlayableUnitCompilerError(
                f"{label} CommandTrigger {trigger_button.name} must name exactly "
                "one SPECIAL_POWER template"
            )
        templates.add(trigger_powers[0].casefold())

    options = {
        token
        for value in button.values("Options")
        for token in _tokens(value)
        if token.casefold() not in {"none", "null"}
    }
    bound = [
        block
        for block in behavior_modules
        if templates
        & {token.casefold() for token in _module_tokens(block, "SpecialPowerTemplate")}
    ]
    if len(bound) > _MAX_ABILITY_MODULES:
        raise PlayableUnitCompilerError(f"{label} exceeds the ability module limit")

    limitations: list[str] = []
    effect: dict[str, object] = {"kind": "none"}
    status = "implemented"
    reason = ""

    # Level gate: every authored UnpauseSpecialPowerUpgrade trigger must chain
    # to an authored ExperienceLevel grant covering this hero.
    gate_upgrades = sorted(
        {
            token
            for block in bound
            if block.kind.casefold() == "unpausespecialpowerupgrade"
            for token in _module_tokens(block, "TriggeredBy")
        },
        key=str.casefold,
    )
    level_gate: dict[str, object] | None = None
    if gate_upgrades:
        missing = [upgrade for upgrade in gate_upgrades if upgrade.casefold() not in level_grants]
        if missing:
            detail = (
                "no authored ExperienceLevel grants %s to this hero"
                % ", ".join(missing)
                if experience_levels_available
                else "experience level source is not in the effective INI view"
            )
            level_gate = {
                "upgradeIds": list(gate_upgrades),
                "requiredLevel": None,
                "limitation": detail,
                "sourceIni": EXPERIENCE_LEVELS_PATH,
            }
            limitations.append(f"level gate unresolved: {detail}")
        else:
            level_gate = {
                "upgradeIds": list(gate_upgrades),
                "requiredLevel": max(
                    level_grants[upgrade.casefold()] for upgrade in gate_upgrades
                ),
                "sourceIni": EXPERIENCE_LEVELS_PATH,
            }

    row: dict[str, object] = {
        "id": button.name,
        "slot": slot,
        "specialPowerId": power_id,
        "button": _ability_button_leaf_fields(button),
        "targeting": (
            "point"
            if "NEED_TARGET_POS" in options
            else "enemy-object"
            if "NEED_TARGET_ENEMY_OBJECT" in options
            else "self"
        ),
        "modules": [
            {
                "kind": block.kind,
                "instanceTag": block.instance_tag or "",
                "sourceIni": block.source_virtual_path,
                "line": block.line,
            }
            for block in bound
        ],
        "sourceIni": COMMAND_BUTTON_PATH,
    }
    unit_sound = _first(button.values("UnitSpecificSound"))
    if unit_sound is not None:
        row["unitSpecificSoundId"] = unit_sound

    power_block = power_blocks.get(power_id.casefold())
    if power_block is None:
        status = "unimplemented"
        reason = f"effective SpecialPower is missing: {power_id}"
    if status == "implemented" and power_block is not None:
        enum = _first(power_block.values("Enum"))
        if enum is not None:
            row["enum"] = enum
        reload_values = power_block.values("ReloadTime")
        if reload_values:
            cooldown = _resolved_expression(reload_values[-1], constants)
        else:
            # Retail may omit ReloadTime entirely (Dain's Stubborn Pride):
            # the engine default is zero milliseconds — no recharge gate.
            # Only an authored expression that cannot resolve fails closed.
            cooldown = 0
        if cooldown is None:
            status = "unimplemented"
            reason = f"SpecialPower {power_id} has an unresolvable ReloadTime"
        else:
            row["cooldownMs"] = cooldown
        initiate_sound = _first(power_block.values("InitiateAtLocationSound")) or _first(
            power_block.values("InitiateSound")
        )
        if initiate_sound is not None:
            row["initiateSoundId"] = initiate_sound
        radius = power_block.values("RadiusCursorRadius")
        if radius:
            resolved_radius = _resolved_expression(radius[-1], constants)
            if resolved_radius is not None:
                row["radiusCursorRadius"] = resolved_radius

    if status == "implemented":
        if "NONPRESSABLE" in options:
            # Passive display buttons (leadership auras, cloak) never cast.
            status = "passive"
            reason = "button is authored NONPRESSABLE (passive display)"
        else:
            unsupported_kinds = sorted(
                {
                    block.kind
                    for block in bound
                    if block.kind.casefold() in _ABILITY_UNIMPLEMENTED_MODULE_KINDS
                    and _ABILITY_UNIMPLEMENTED_MODULE_KINDS[block.kind.casefold()]
                },
                key=str.casefold,
            )
            unknown_kinds = sorted(
                {
                    block.kind
                    for block in bound
                    if block.kind.casefold()
                    not in _ABILITY_SUPPORT_MODULE_KINDS
                    and block.kind.casefold() not in _ABILITY_UNIMPLEMENTED_MODULE_KINDS
                    and block.kind.casefold()
                    not in {
                        "weaponfirespecialabilityupdate",
                        "oclspecialpower",
                        "heromodespecialabilityupdate",
                        "playerhealspecialpower",
                        "togglemountedspecialabilityupdate",
                        "levelgrantspecialpower",
                        "arrowstormupdate",
                        "togglehiddenspecialabilityupdate",
                        "invisibilityspecialpower",
                        "teleportspecialabilityupdate",
                        "cursespecialpower",
                    }
                },
                key=str.casefold,
            )
            if unsupported_kinds:
                status = "unimplemented"
                reason = "; ".join(
                    _ABILITY_UNIMPLEMENTED_MODULE_KINDS[kind.casefold()]
                    for kind in unsupported_kinds
                )
            elif unknown_kinds:
                status = "unimplemented"
                reason = "unsupported ability module kind: " + ", ".join(unknown_kinds)

    if status == "implemented":
        if not gate_upgrades and any(
            (_first(block.values("StartsPaused")) or "").casefold() == "yes"
            for block in bound
        ):
            status = "unimplemented"
            reason = "ability starts paused without an authored unpause gate"

    if status == "implemented":
        try:
            effect = _hero_ability_effect(
                label,
                bound,
                burst_heal_modules,
                gate_upgrades,
                modifier_blocks,
                ocl_source,
                objects,
                documents,
                constants,
                filter_defines,
                limitations,
                power_enum=str(row.get("enum", "")),
                member_lineage=member_lineage,
                behavior_modules=behavior_modules,
                radius_cursor_radius=row.get("radiusCursorRadius"),
                named_definition_cache=named_definition_cache,
                cache_lock=cache_lock,
            )
        except PlayableUnitCompilerError as error:
            status = "unimplemented"
            reason = str(error)
            effect = {"kind": "none"}
        if status == "implemented" and effect.get("kind") == "none":
            status = "unimplemented"
            reason = reason or "no convertible effect leaf is bound to this ability"

    if status == "passive":
        # Passive leadership buttons bind their AttributeModifierAuraUpdate
        # through the shared TriggeredBy upgrade; a bound aura compiles into
        # a real leadership-aura effect the runtime radiates.  Everything
        # else keeps the placeholder with the recorded gap.
        gate_is_resolved = level_gate is not None and (
            level_gate.get("requiredLevel") is not None
        )
        try:
            aura_effect = _leadership_aura_effect(
                label,
                behavior_modules,
                gate_upgrades,
                gate_is_resolved,
                modifier_blocks,
                constants,
                filter_defines,
                limitations,
            )
        except PlayableUnitCompilerError as error:
            aura_effect = None
            limitations.append(f"leadership aura not converted: {error}")
        if aura_effect is not None:
            effect = aura_effect

    if level_gate is not None:
        row["levelGate"] = level_gate
    row["effect"] = effect
    row["implementation"] = {
        "status": status,
        "reason": reason,
        "limitations": limitations,
    }
    return row


def _hero_ability_effect(
    label: str,
    bound: Sequence[SageBlock],
    burst_heal_modules: Sequence[SageBlock],
    gate_upgrades: Sequence[str],
    modifier_blocks: Mapping[str, IniBlock],
    ocl_source: bytes | None,
    objects: Mapping[str, SageObject],
    documents: Mapping[str, bytes],
    constants: Mapping[str, int | float],
    filter_defines: Mapping[str, tuple[str, ...]],
    limitations: list[str],
    *,
    power_enum: str = "",
    member_lineage: Sequence[SageObject] = (),
    behavior_modules: Sequence[SageBlock] = (),
    radius_cursor_radius: int | float | None = None,
    named_definition_cache: dict[tuple[str, str], dict[str, list[dict[str, object]]] | None] | None,
    cache_lock: threading.Lock | None,
) -> dict[str, object]:
    """Bind the ability's effect leaves; raise PlayableUnitCompilerError on gaps."""

    def modules_of(kind: str) -> list[SageBlock]:
        return [block for block in bound if block.kind.casefold() == kind]

    heal_modules = modules_of("playerhealspecialpower")
    ocl_modules = modules_of("oclspecialpower")
    weapon_modules = modules_of("weaponfirespecialabilityupdate")
    hero_mode_modules = modules_of("heromodespecialabilityupdate")
    mount_modules = modules_of("togglemountedspecialabilityupdate")

    if power_enum.casefold() == "special_infantry_capture_building":
        # Capture building (the ubiquitous CaptureBuilding.inc): the bound
        # SpecialAbilityUpdate authors the channel envelope (StartAbilityRange
        # gate, UnpackTime + PreparationTime + PackTime channel).  The runtime
        # channels for the authored duration on a capturable structure, then
        # transfers ownership.
        capture_modules = [
            block
            for block in modules_of("specialabilityupdate")
            if block.values("StartAbilityRange")
        ]
        if len(capture_modules) != 1:
            raise PlayableUnitCompilerError(
                f"{label} needs exactly one capture SpecialAbilityUpdate with "
                f"a StartAbilityRange; found {len(capture_modules)}"
            )
        block = capture_modules[0]
        start_range = _resolved_expression(
            (block.values("StartAbilityRange") or ("",))[-1], constants
        )
        if start_range is None or float(start_range) <= 0.0:
            raise PlayableUnitCompilerError(
                f"{label} capture module has no resolvable StartAbilityRange"
            )
        effect: dict[str, object] = {
            "kind": "capture-building",
            "startAbilityRange": start_range,
            "sourceIni": block.source_virtual_path,
            "line": block.line,
        }
        for output_name, source_name in (
            ("unpackMs", "UnpackTime"),
            ("preparationMs", "PreparationTime"),
            ("packMs", "PackTime"),
        ):
            resolved = _resolved_expression(
                (block.values(source_name) or ("",))[-1], constants
            )
            effect[output_name] = resolved if resolved is not None else 0
        if (_first(block.values("DoCaptureFX")) or "").casefold() == "yes":
            effect["doCaptureFx"] = True
        limitations.append(
            "capture is tier-1: only neutral/unowned structures flagged "
            "capturable by the runtime transfer ownership"
        )
        return effect

    if mount_modules:
        # Mount/dismount (SpecialAbilityToggleMounted): the mounted state
        # lives on the same object as authored condition sets — LocomotorSet
        # Condition=SET_MOUNTED carries the mounted speed, an optional
        # WeaponSet Conditions=MOUNTED carries the mounted weapon (published
        # as the "mounted" runtime weapon mode by the simulation contract).
        if len(mount_modules) > 1:
            raise PlayableUnitCompilerError(
                f"{label} matches multiple ToggleMountedSpecialAbilityUpdate modules"
            )
        block = mount_modules[0]
        mounted_locomotors = [
            candidate
            for candidate in _effective_top_blocks(member_lineage)
            if (candidate.header_key or candidate.kind).casefold() == "locomotorset"
            and any(
                token.casefold() == "set_mounted"
                for assignment in candidate.assignments
                if assignment.key.casefold() in {"condition", "conditions"}
                for token in _tokens(assignment.value)
            )
        ]
        if len(mounted_locomotors) != 1:
            raise PlayableUnitCompilerError(
                f"{label} needs exactly one SET_MOUNTED LocomotorSet; "
                f"found {len(mounted_locomotors)}"
            )
        locomotor_block = mounted_locomotors[0]
        speed_rows = [
            assignment
            for assignment in locomotor_block.assignments
            if assignment.key.casefold() == "speed"
        ]
        mounted_speed = (
            _resolved_expression(speed_rows[-1].value, constants)
            if speed_rows
            else None
        )
        if mounted_speed is None or float(mounted_speed) <= 0.0:
            raise PlayableUnitCompilerError(
                f"{label} SET_MOUNTED LocomotorSet has no resolvable Speed"
            )
        effect = {
            "kind": "mount-toggle",
            "mountedSpeed": mounted_speed,
            "sourceIni": block.source_virtual_path,
            "line": block.line,
        }
        locomotor_ids = [
            _tokens(assignment.value)[-1]
            for assignment in locomotor_block.assignments
            if assignment.key.casefold() == "locomotor" and _tokens(assignment.value)
        ]
        if len({token.casefold() for token in locomotor_ids}) == 1:
            effect["mountedLocomotorId"] = locomotor_ids[0]
        for output_name, source_name in (
            ("unpackMs", "UnpackTime"),
            ("packMs", "PackTime"),
        ):
            resolved = _resolved_expression(
                (block.values(source_name) or ("",))[-1], constants
            )
            effect[output_name] = resolved if resolved is not None else 0
        mounted_weapon_sets = [
            candidate
            for candidate in _effective_top_blocks(member_lineage)
            if (candidate.header_key or candidate.kind).casefold() == "weaponset"
            and {
                token.casefold()
                for assignment in candidate.assignments
                if assignment.key.casefold() in {"condition", "conditions"}
                for token in _tokens(assignment.value)
                if not token.startswith("-")
                and token.casefold() not in {"none", "set_normal"}
            }
            == {"mounted"}
        ]
        if len(mounted_weapon_sets) > 1:
            raise PlayableUnitCompilerError(
                f"{label} matches multiple MOUNTED WeaponSet blocks"
            )
        if mounted_weapon_sets:
            mounted_weapon = _conditioned_weapon_set_target(
                mounted_weapon_sets[0], label
            )
            # The profile must resolve or the mount fails closed: a mounted
            # state that silently kept foot combat stats despite an authored
            # mounted weapon would be a partial swap.
            _weapon_mode_profile(
                documents,
                mounted_weapon,
                constants,
                label,
                named_definition_cache=named_definition_cache,
                cache_lock=cache_lock,
            )
            effect["mountedWeaponModeKey"] = "mounted"
            effect["mountedWeaponId"] = mounted_weapon
        else:
            limitations.append(
                "no authored MOUNTED WeaponSet; the mounted state keeps the "
                "foot weapon"
            )
        mounted_armor_sets = [
            candidate
            for candidate in _effective_top_blocks(member_lineage)
            if (candidate.header_key or candidate.kind).casefold() == "armorset"
            and any(
                token.casefold() == "mounted"
                for assignment in candidate.assignments
                if assignment.key.casefold() in {"condition", "conditions"}
                for token in _tokens(assignment.value)
            )
        ]
        if mounted_armor_sets:
            limitations.append(
                "authored MOUNTED ArmorSet is not applied (armor swap is a "
                "follow-up)"
            )
        return effect

    if heal_modules:
        block = heal_modules[0]
        amount = _resolved_expression(
            (block.values("HealAmount") or ("",))[-1], constants
        )
        radius = _resolved_expression(
            (block.values("HealRadius") or ("",))[-1], constants
        )
        if amount is None or radius is None:
            raise PlayableUnitCompilerError(
                f"{label} heal module has an unresolvable HealAmount/HealRadius"
            )
        effect: dict[str, object] = {
            "kind": "heal",
            "module": block.kind,
            "amountKind": "fraction",
            "amount": amount,
            "radius": radius,
            "onlyOthers": False,
            "sourceIni": block.source_virtual_path,
            "line": block.line,
        }
        heal_fx = _first(block.values("HealFX"))
        if heal_fx is not None:
            effect["healFxId"] = heal_fx
        affects = _first(block.values("HealAffects"))
        if affects is not None:
            effect["affects"] = affects
        return effect

    def summon_effect(ocl_id: str) -> dict[str, object]:
        """Compile one ability ObjectCreationList into a summon effect.

        Shared by the OCLSpecialPower module lane and the OCL-only
        SpecialWeapon lane below, so both produce the identical shape and both
        walk the SAME leaf closure (`_summon_leaf_closure`).
        """

        if ocl_source is None:
            raise PlayableUnitCompilerError(
                f"{label} requires {OBJECT_CREATION_LIST_PATH} in the effective INI view"
            )
        entries = _ocl_create_object_entries(ocl_source, ocl_id)
        if entries is None:
            raise PlayableUnitCompilerError(
                f"{label} references a missing ObjectCreationList: {ocl_id}"
            )
        summon_objects: list[dict[str, object]] = []
        for entry in entries:
            names: list[str] = []
            count: int | float = 1
            for field in entry["fields"]:
                key = str(field["key"]).casefold()
                if key == "objectnames":
                    names.extend(
                        token
                        for token in _tokens(str(field["value"]))
                        if token.casefold() not in {"none", "null"}
                    )
                elif key == "count":
                    resolved = _resolved_expression(str(field["value"]), constants)
                    if resolved is None or int(resolved) < 1:
                        raise PlayableUnitCompilerError(
                            f"{label} ObjectCreationList {ocl_id} has an "
                            "unresolvable Count"
                        )
                    count = int(resolved)
            if not names:
                raise PlayableUnitCompilerError(
                    f"{label} ObjectCreationList {ocl_id} has no ObjectNames"
                )
            for name in names:
                target = objects.get(name.casefold())
                if target is None:
                    raise PlayableUnitCompilerError(
                        f"{label} ObjectCreationList {ocl_id} references a "
                        f"missing Object: {name}"
                    )
                summon_objects.append(
                    {
                        "id": target.name,
                        "count": count,
                        "sourceIni": OBJECT_CREATION_LIST_PATH,
                        "line": int(entry["line"]),
                    }
                )
        resolved_effect: dict[str, object] = {
            "kind": "summon",
            "oclId": ocl_id,
            "objects": summon_objects,
            "sourceIni": OBJECT_CREATION_LIST_PATH,
        }
        summon_leaves = _summon_leaf_closure(ocl_id, objects, documents, constants)
        if summon_leaves is not None:
            resolved_effect["leaves"] = summon_leaves
        return resolved_effect

    if ocl_modules:
        block = ocl_modules[0]
        ocl_tokens = _module_tokens(block, "OCL")
        if len(ocl_tokens) != 1:
            raise PlayableUnitCompilerError(f"{label} has an ambiguous OCL reference")
        effect = summon_effect(ocl_tokens[0])
        create_location = _first(block.values("CreateLocation"))
        if create_location is not None:
            effect["createLocation"] = create_location
        return effect

    if weapon_modules:
        block = weapon_modules[0]
        weapon_tokens = _module_tokens(block, "SpecialWeapon")
        if len(weapon_tokens) != 1:
            raise PlayableUnitCompilerError(
                f"{label} has an ambiguous SpecialWeapon reference"
            )
        # Aragorn's Army of the Dead: aragorn.ini:854-866 fires the ability
        # through WeaponFireSpecialAbilityUpdate, and weapon.ini:7806-7810
        # gives that weapon a single WeaponOCLNugget and nothing else.  Such a
        # weapon has no damage payload at all -- it IS a summon, and it names
        # the same egg OCL the OCLSpecialPower lane already resolves.  The rule
        # is deliberately narrow: a weapon that also damages, or one whose OCL
        # does not resolve, falls through to the damage lane below and keeps
        # failing exactly as it did.
        ocl_names = _weapon_ocl_nugget_names(
            documents,
            weapon_tokens[0],
            cache=named_definition_cache,
            cache_lock=cache_lock,
        )
        if ocl_names is not None:
            summary = _weapon_nugget_summary(documents, weapon_tokens[0])
            kinds = summary["kinds"]
            assert isinstance(kinds, Mapping)
            if set(kinds) == {"weaponoclnugget"} and len(ocl_names) == 1:
                effect = summon_effect(ocl_names[0])
                effect["weaponId"] = weapon_tokens[0]
                start_range = _resolved_expression(
                    (block.values("StartAbilityRange") or ("",))[-1], constants
                )
                if start_range is not None:
                    effect["startAbilityRange"] = start_range
                return effect
        leaf = _ability_weapon_leaf(
            documents,
            weapon_tokens[0],
            constants,
            label,
            named_definition_cache=named_definition_cache,
            cache_lock=cache_lock,
        )
        effect = {
            "kind": "weapon-blast",
            "weaponId": leaf["id"],
            "damage": leaf["damage"],
            "damageRadius": leaf["damageRadius"],
            "components": leaf["components"],
            "sourceIni": WEAPON_PATH,
        }
        for key in (
            "damageType",
            "attackRange",
            "fireFxId",
            "excludedNuggets",
            "warheadId",
            "warheadIds",
        ):
            if key in leaf:
                effect[key] = leaf[key]
        # MetaImpactNugget shockwave (Gandalf blasts): source-unit knockback
        # magnitudes ride the effect; the runtime scales and applies them.
        knockback_chain: list[str] = [weapon_tokens[0]]
        if "warheadId" in leaf:
            knockback_chain.append(str(leaf["warheadId"]))
        for warhead_id in leaf.get("warheadIds", ()):  # type: ignore[union-attr]
            knockback_chain.append(str(warhead_id))
        effect.update(
            _weapon_knockback_fields(
                documents,
                knockback_chain,
                constants,
                label,
                limitations,
                named_definition_cache=named_definition_cache,
                cache_lock=cache_lock,
            )
        )
        if leaf.get("damageScalarAuthored"):
            limitations.append(
                "weapon authors DamageScalar target-type scaling (not applied)"
            )
        slot_tokens = _module_tokens(block, "WhichSpecialWeapon")
        if slot_tokens:
            resolved = _resolved_expression(slot_tokens[0], constants)
            if resolved is not None:
                effect["specialWeaponSlot"] = int(resolved)
        start_range = _resolved_expression(
            (block.values("StartAbilityRange") or ("",))[-1], constants
        )
        if start_range is not None:
            effect["startAbilityRange"] = start_range
        if "attackRange" not in effect and "startAbilityRange" not in effect:
            limitations.append("ability weapon authors no AttackRange/StartAbilityRange")
        return effect

    grant_modules = modules_of("levelgrantspecialpower")
    if grant_modules:
        # LevelGrantSpecialPower (King's Favor / Train Allies): the module
        # authors the grant circle and the exact Experience amount; the
        # runtime pays it through the normal experience pipeline.  Every
        # consumed magnitude must resolve or the ability stays a recorded gap.
        if len(grant_modules) > 1:
            raise PlayableUnitCompilerError(
                f"{label} matches multiple LevelGrantSpecialPower modules"
            )
        block = grant_modules[0]
        experience = _resolved_expression(
            (block.values("Experience") or ("",))[-1], constants
        )
        if experience is None or float(experience) <= 0.0:
            raise PlayableUnitCompilerError(
                f"{label} LevelGrantSpecialPower has no resolvable Experience"
            )
        radius_effect = _resolved_expression(
            (block.values("RadiusEffect") or ("",))[-1], constants
        )
        if radius_effect is None or float(radius_effect) <= 0.0:
            raise PlayableUnitCompilerError(
                f"{label} LevelGrantSpecialPower has no resolvable RadiusEffect"
            )
        effect = {
            "kind": "experience-grant",
            "experience": experience,
            "radiusEffect": radius_effect,
            "sourceIni": block.source_virtual_path,
            "line": block.line,
        }
        start_range = _resolved_expression(
            (block.values("StartAbilityRange") or ("",))[-1], constants
        )
        if start_range is not None and float(start_range) > 0.0:
            effect["startAbilityRange"] = start_range
        filter_tokens = _module_tokens(block, "AcceptanceFilter")
        if filter_tokens:
            effect["affects"] = _projected_object_filter(filter_tokens, filter_defines)
        level_fx = _first(block.values("LevelFX"))
        if level_fx is not None:
            effect["levelFxId"] = level_fx
        return effect

    storm_modules = modules_of("arrowstormupdate")
    if storm_modules:
        # ArrowStormUpdate barrage (Arrow Storm / Lightning Sword): the
        # module authors the volley envelope; its named WeaponTemplate
        # resolves per-shot damage through the shared ability-weapon leaf
        # (ProjectileNugget warhead chain included).  Fail-closed: the
        # damage, TargetRadius and MaxShots must all resolve.
        if len(storm_modules) > 1:
            raise PlayableUnitCompilerError(
                f"{label} matches multiple ArrowStormUpdate modules"
            )
        block = storm_modules[0]
        weapon_tokens = _module_tokens(block, "WeaponTemplate")
        if len(weapon_tokens) != 1:
            raise PlayableUnitCompilerError(
                f"{label} ArrowStormUpdate must name exactly one WeaponTemplate"
            )
        leaf = _ability_weapon_leaf(
            documents,
            weapon_tokens[0],
            constants,
            label,
            named_definition_cache=named_definition_cache,
            cache_lock=cache_lock,
        )
        if float(leaf["damage"]) <= 0.0:  # type: ignore[arg-type]
            raise PlayableUnitCompilerError(
                f"{label} arrow-storm weapon {weapon_tokens[0]} resolves no "
                "positive per-shot damage"
            )
        target_radius = _resolved_expression(
            (block.values("TargetRadius") or ("",))[-1], constants
        )
        if target_radius is None or float(target_radius) <= 0.0:
            raise PlayableUnitCompilerError(
                f"{label} ArrowStormUpdate has no resolvable TargetRadius"
            )
        max_shots = _resolved_expression(
            (block.values("MaxShots") or ("",))[-1], constants
        )
        if max_shots is None or int(max_shots) < 1:
            raise PlayableUnitCompilerError(
                f"{label} ArrowStormUpdate has no resolvable MaxShots"
            )
        shots_per_burst = _resolved_expression(
            (block.values("ShotsPerBurst") or ("",))[-1], constants
        )
        persistent_prep = _resolved_expression(
            (block.values("PersistentPrepTime") or ("",))[-1], constants
        )
        effect = {
            "kind": "arrow-storm",
            "weaponId": leaf["id"],
            "weaponDamage": leaf["damage"],
            "components": leaf["components"],
            "targetRadius": target_radius,
            "maxShots": int(max_shots),
            "shotsPerBurst": int(shots_per_burst) if shots_per_burst is not None else 1,
            "persistentPrepMs": persistent_prep if persistent_prep is not None else 0,
            "canShootEmptyGround": (
                (_first(block.values("CanShootEmptyGround")) or "").casefold() == "yes"
            ),
            "sourceIni": block.source_virtual_path,
            "line": block.line,
        }
        for key in ("damageType", "fireFxId", "warheadId", "excludedNuggets"):
            if key in leaf:
                effect[key] = leaf[key]
        start_range = _resolved_expression(
            (block.values("StartAbilityRange") or ("",))[-1], constants
        )
        if start_range is not None and float(start_range) > 0.0:
            effect["startAbilityRange"] = start_range
        # `UnpackingVariation = N` selects the authored PACKING_TYPE_<N>
        # AnimationState envelope for this ability's caster pose
        # (gandalf.ini:1330 -> the PACKING_TYPE_1 UNPACKING/PREPARING/PACKING
        # LightningSword clips at gandalf.ini:343-370). Carrying it lets the
        # runtime request the ability's own animation instead of a generic
        # attack swing.
        unpacking_variation = _resolved_expression(
            (block.values("UnpackingVariation") or ("",))[-1], constants
        )
        if unpacking_variation is not None and int(unpacking_variation) > 0:
            effect["unpackingVariation"] = int(unpacking_variation)
        if leaf.get("damageScalarAuthored"):
            limitations.append(
                "weapon authors DamageScalar target-type scaling (not applied)"
            )
        return effect

    hidden_modules = modules_of("togglehiddenspecialabilityupdate")
    invisibility_modules = modules_of("invisibilityspecialpower")
    if hidden_modules or invisibility_modules:
        # Stealth (ToggleHiddenSpecialAbilityUpdate / InvisibilitySpecialPower):
        # the cast cloaks the hero (and, for a broadcast module, allies in
        # the authored radius) for the authored duration; the paired
        # InvisibilityNugget's ForbiddenConditions break the cloak early.
        if len(hidden_modules) + len(invisibility_modules) > 1:
            raise PlayableUnitCompilerError(
                f"{label} matches multiple stealth modules"
            )
        if hidden_modules:
            block = hidden_modules[0]
            duration = _resolved_expression(
                (block.values("EffectDuration") or ("",))[-1], constants
            )
            if duration is None or float(duration) <= 0.0:
                raise PlayableUnitCompilerError(
                    f"{label} ToggleHiddenSpecialAbilityUpdate has no "
                    "resolvable EffectDuration"
                )
            # Retail binds the toggle to the object's FIRST InvisibilityUpdate
            # (the authored ordering contract in thranduil.ini); its nugget
            # carries the cloak's ForbiddenConditions.
            invisibility_updates = [
                module
                for module in behavior_modules
                if module.kind.casefold() == "invisibilityupdate"
            ]
            if not invisibility_updates:
                raise PlayableUnitCompilerError(
                    f"{label} toggle-hidden has no InvisibilityUpdate module "
                    "to convert"
                )
            nugget = _invisibility_nugget(invisibility_updates[0])
            if nugget is None:
                raise PlayableUnitCompilerError(
                    f"{label} first InvisibilityUpdate authors no "
                    "InvisibilityNugget"
                )
            forbidden = [
                token
                for value in nugget.values("ForbiddenConditions")
                for token in _tokens(value)
            ]
            effect = {
                "kind": "stealth-toggle",
                "effectDurationMs": duration,
                "forbiddenConditions": forbidden,
                "sourceIni": block.source_virtual_path,
                "line": block.line,
            }
        else:
            block = invisibility_modules[0]
            duration = _resolved_expression(
                (block.values("Duration") or ("",))[-1], constants
            )
            if duration is None or float(duration) <= 0.0:
                raise PlayableUnitCompilerError(
                    f"{label} InvisibilitySpecialPower has no resolvable Duration"
                )
            nugget = _invisibility_nugget(block)
            forbidden = (
                [
                    token
                    for value in nugget.values("ForbiddenConditions")
                    for token in _tokens(value)
                ]
                if nugget is not None
                else []
            )
            effect = {
                "kind": "stealth-toggle",
                "effectDurationMs": duration,
                "forbiddenConditions": forbidden,
                "sourceIni": block.source_virtual_path,
                "line": block.line,
            }
            broadcast = _resolved_expression(
                (block.values("BroadcastRadius") or ("",))[-1], constants
            )
            if broadcast is not None and float(broadcast) > 0.0:
                effect["broadcastRadius"] = broadcast
            filter_tokens = _module_tokens(block, "ObjectFilter")
            if filter_tokens:
                effect["affects"] = _projected_object_filter(
                    filter_tokens, filter_defines
                )
        unsupported_conditions = sorted(
            {
                token
                for token in effect["forbiddenConditions"]  # type: ignore[union-attr]
                if token.casefold()
                not in {"taking_damage", "firing_any", "using_ability"}
            },
            key=str.casefold,
        )
        if unsupported_conditions:
            limitations.append(
                "forbidden stealth conditions not enforced by the runtime: "
                + ", ".join(unsupported_conditions)
            )
        return effect

    teleport_modules = modules_of("teleportspecialabilityupdate")
    if teleport_modules:
        # TeleportSpecialAbilityUpdate (Shelob Tunnel): MaxDistance gates the
        # cast like a range; BusyForDuration holds the hero after arrival.
        if len(teleport_modules) > 1:
            raise PlayableUnitCompilerError(
                f"{label} matches multiple TeleportSpecialAbilityUpdate modules"
            )
        block = teleport_modules[0]
        max_distance = _resolved_expression(
            (block.values("MaxDistance") or ("",))[-1], constants
        )
        if max_distance is None or float(max_distance) <= 0.0:
            raise PlayableUnitCompilerError(
                f"{label} TeleportSpecialAbilityUpdate has no resolvable "
                "MaxDistance"
            )
        busy = _resolved_expression(
            (block.values("BusyForDuration") or ("",))[-1], constants
        )
        effect = {
            "kind": "teleport",
            "maxDistance": max_distance,
            "busyForDurationMs": busy if busy is not None else 0,
            "sourceIni": block.source_virtual_path,
            "line": block.line,
        }
        destination_weapon = _first(block.values("DestinationWeaponName"))
        if destination_weapon is not None:
            limitations.append(
                f"authored DestinationWeaponName ({destination_weapon}) is "
                "not fired at the arrival point"
            )
        return effect

    curse_modules = modules_of("cursespecialpower")
    if curse_modules:
        # CurseSpecialPower (Hour of the Witch-King): the authored
        # CursePercentage restarts the victim hero's recharges; the power's
        # RadiusCursorRadius bounds target selection.
        if len(curse_modules) > 1:
            raise PlayableUnitCompilerError(
                f"{label} matches multiple CurseSpecialPower modules"
            )
        block = curse_modules[0]
        percentage = _resolved_percent_expression(
            (block.values("CursePercentage") or ("",))[-1], constants
        )
        if percentage is None or float(percentage) <= 0.0:
            raise PlayableUnitCompilerError(
                f"{label} CurseSpecialPower has no resolvable CursePercentage"
            )
        if radius_cursor_radius is None or float(radius_cursor_radius) <= 0.0:
            raise PlayableUnitCompilerError(
                f"{label} curse power authors no RadiusCursorRadius target circle"
            )
        effect = {
            "kind": "curse",
            "cursePercentage": percentage,
            "radiusCursorRadius": radius_cursor_radius,
            "sourceIni": block.source_virtual_path,
            "line": block.line,
        }
        start_range = _resolved_expression(
            (block.values("StartAbilityRange") or ("",))[-1], constants
        )
        if start_range is not None and float(start_range) > 0.0:
            effect["startAbilityRange"] = start_range
        cursed_fx = _first(block.values("CursedFX"))
        if cursed_fx is not None:
            effect["cursedFxId"] = cursed_fx
        return effect

    # Terror pulse (Elendil, Screech-class): a bound ability update that
    # authors GenerateTerror pushes the TERROR emotion onto every enemy in
    # EmotionPulseRadius; victims run the authored TERROR EmotionNugget
    # (emotions.ini) — its Duration is the authored fear window and its
    # PreventPlayerCommands/RUN_AWAY_PANIC lock projects as a no-fight
    # penalty for that window.  Ambiguity fails closed.
    terror_modules = [
        block
        for block in bound
        if block.kind.casefold()
        in {"specialabilityupdate", "modelconditionspecialabilityupdate"}
        and (_first(block.values("GenerateTerror")) or "").casefold() == "yes"
    ]
    if terror_modules:
        block = terror_modules[0]
        radius = _resolved_expression(
            (block.values("EmotionPulseRadius") or ("",))[-1], constants
        )
        if radius is None or float(radius) <= 0.0:
            raise PlayableUnitCompilerError(
                f"{label} terror module has no resolvable EmotionPulseRadius"
            )
        emotions_source = _optional_document(documents, EMOTIONS_PATH)
        if emotions_source is None:
            raise PlayableUnitCompilerError(
                f"{label} requires {EMOTIONS_PATH} in the effective INI view"
            )
        terror_nuggets = [
            nugget
            for nugget in _named_blocks(emotions_source, "EmotionNugget").values()
            if (_first(nugget.values("Type")) or "").casefold() == "terror"
        ]
        resolved_nuggets: dict[str, tuple[IniBlock, int | float]] = {}
        for nugget in terror_nuggets:
            nugget_duration = _resolved_expression(
                (nugget.values("Duration") or ("",))[-1], constants
            )
            if nugget_duration is not None and float(nugget_duration) > 0.0:
                resolved_nuggets[nugget.name.casefold()] = (nugget, nugget_duration)
        if len(resolved_nuggets) != 1:
            raise PlayableUnitCompilerError(
                f"{label} needs exactly one authored TERROR EmotionNugget with "
                f"a Duration; found {len(resolved_nuggets)}"
            )
        nugget, terror_duration = next(iter(resolved_nuggets.values()))
        if (_first(nugget.values("PreventPlayerCommands")) or "").casefold() != "yes":
            raise PlayableUnitCompilerError(
                f"{label} TERROR EmotionNugget {nugget.name} authors no "
                "PreventPlayerCommands lock to convert"
            )
        effect = {
            "kind": "terror",
            "radius": radius,
            "durationMs": terror_duration,
            # The authored flee reaction (AIState RUN_AWAY_PANIC +
            # PreventPlayerCommands) projects as a no-fight penalty for the
            # authored Duration; no stat magnitudes are invented.
            "modifiers": [
                {
                    "kind": "DAMAGE_MULT",
                    "value": 0.0,
                    "application": "multiplicative",
                    "semantic": (
                        "TERROR victims cannot fight while the authored flee "
                        "reaction (RUN_AWAY_PANIC / PreventPlayerCommands) holds"
                    ),
                }
            ],
            "emotionNuggetId": nugget.name,
            "emotionSource": {"sourceIni": EMOTIONS_PATH},
            "sourceIni": block.source_virtual_path,
            "line": block.line,
        }
        filter_tokens = _module_tokens(block, "ObjectFilter")
        if filter_tokens:
            effect["affects"] = _projected_object_filter(filter_tokens, filter_defines)
        limitations.append(
            "authored flee AI (RUN_AWAY_PANIC) is projected as a no-fight "
            "debuff; retail authors no scatter displacement"
        )
        anti_categories = sorted(
            {
                token
                for starter in bound
                for token in _module_tokens(starter, "AntiCategory")
            },
            key=str.casefold,
        )
        for anti in anti_categories:
            limitations.append(f"anti-category strip ({anti}) is not applied")
        return effect

    modifier_id: str | None = None
    duration: int | float | None = None
    range_value: int | float | None = None
    affects_self = True
    affects_filter: str | None = None
    anti_category: str | None = None
    modifier_module: SageBlock | None = None
    if hero_mode_modules:
        modifier_module = hero_mode_modules[0]
        tokens = _module_tokens(modifier_module, "HeroAttributeModifier")
        if len(tokens) > 1:
            raise PlayableUnitCompilerError(
                f"{label} has an ambiguous HeroAttributeModifier"
            )
        modifier_id = tokens[0] if tokens else None
        duration = _resolved_expression(
            (modifier_module.values("HeroEffectDuration") or ("",))[-1], constants
        )
    else:
        timing = [
            block
            for block in modules_of("specialabilityupdate")
            if _module_tokens(block, "TriggerAttributeModifier")
        ]
        starters = [
            block
            for block in modules_of("specialpowermodule")
            if _module_tokens(block, "AttributeModifier")
        ]
        if timing:
            modifier_module = timing[0]
            modifier_id = _module_tokens(modifier_module, "TriggerAttributeModifier")[0]
            duration = _resolved_expression(
                (modifier_module.values("AttributeModifierDuration") or ("",))[-1],
                constants,
            )
        elif starters:
            modifier_module = starters[0]
            modifier_id = _module_tokens(modifier_module, "AttributeModifier")[0]
    if modifier_module is not None and modifier_id is not None:
        anti_tokens = _module_tokens(modifier_module, "AntiCategory")
        if anti_tokens:
            anti_category = anti_tokens[0]
        range_expression = _resolved_expression(
            (modifier_module.values("AttributeModifierRange") or ("",))[-1], constants
        )
        if range_expression is not None:
            range_value = range_expression
        affects_self_value = _first(modifier_module.values("AttributeModifierAffectsSelf"))
        affects_self = (affects_self_value or "yes").casefold() == "yes"
        affects_tokens = _module_tokens(modifier_module, "AttributeModifierAffects")
        if affects_tokens:
            affects_filter = " ".join(affects_tokens)
        leaf = _ability_modifier_leaf(modifier_blocks, modifier_id, constants, label)
        if duration is None:
            duration = leaf.get("durationMs")  # type: ignore[assignment]
        supported = list(leaf.get("modifiers", []))
        if anti_category is not None and not supported:
            # Pure anti-category strip (Horn of Gondor): the ModifierList
            # authors only the suppression Duration; the module authors the
            # strip radius.  LEADERSHIP strips convert (the runtime drops
            # aura grants and suppresses new ones); any other category or a
            # missing magnitude stays a recorded gap.
            if duration is None:
                duration = leaf.get("durationMs")  # type: ignore[assignment]
            if (
                anti_category.casefold() == "leadership"
                and duration is not None
                and float(duration) > 0.0
                and range_value is not None
                and float(range_value) > 0.0
            ):
                effect = {
                    "kind": "leadership-strip",
                    "antiCategory": anti_category,
                    "modifierId": modifier_id,
                    "attributeModifierRange": range_value,
                    "antiCategoryDurationMs": duration,
                    "sourceIni": modifier_module.source_virtual_path,
                    "line": modifier_module.line,
                    "modifierSourceIni": ATTRIBUTE_MODIFIER_PATH,
                }
                if affects_filter is not None:
                    effect["affectsFilter"] = affects_filter
                return effect
            raise PlayableUnitCompilerError(
                f"{label} strips {anti_category} buffs with no supported "
                "modifier rows; only a LEADERSHIP strip with an authored "
                "AttributeModifierRange and ModifierList Duration converts"
            )
        if not supported:
            raise PlayableUnitCompilerError(
                f"{label} modifier {modifier_id} has no runtime-supported Modifier rows"
            )
        if duration is None:
            raise PlayableUnitCompilerError(
                f"{label} modifier {modifier_id} has no authored effect duration"
            )
        effect = {
            "kind": "attribute-modifier",
            "modifierId": modifier_id,
            "durationMs": duration,
            "affectsSelf": affects_self,
            "modifiers": supported,
            "sourceIni": ATTRIBUTE_MODIFIER_PATH,
        }
        if range_value is not None and range_value > 0:
            effect["range"] = range_value
        if affects_filter is not None:
            effect["affectsFilter"] = affects_filter
        if leaf.get("category"):
            effect["category"] = leaf["category"]
        if leaf.get("fxIds"):
            effect["fxIds"] = leaf["fxIds"]
        if leaf.get("unsupportedModifiers"):
            limitations.append(
                "modifier kinds not applied by the runtime: "
                + ", ".join(leaf["unsupportedModifiers"])  # type: ignore[arg-type]
            )
        if anti_category is not None:
            limitations.append(f"anti-category strip ({anti_category}) is not applied")
        return effect

    # Burst heals bind through authored cross-references: the heal module's
    # TriggeredBy matches the ability gate, or its UnitHealPulseFX matches the
    # ability's authored TriggerFX.  Ambiguity fails closed (no binding).
    trigger_fxes = {
        token.casefold()
        for block in bound
        for token in _module_tokens(block, "TriggerFX")
    }
    heal_candidates: list[SageBlock] = []
    for block in burst_heal_modules:
        triggers = {token.casefold() for token in _module_tokens(block, "TriggeredBy")}
        if triggers and triggers & {upgrade.casefold() for upgrade in gate_upgrades}:
            heal_candidates.append(block)
            continue
        if not triggers:
            pulse = (_first(block.values("UnitHealPulseFX")) or "").casefold()
            if pulse and pulse in trigger_fxes:
                heal_candidates.append(block)
    if len(heal_candidates) > 1:
        raise PlayableUnitCompilerError(
            f"{label} has an ambiguous burst-heal binding"
        )
    if heal_candidates:
        block = heal_candidates[0]
        amount = _resolved_expression(
            (block.values("HealingAmount") or ("",))[-1], constants
        )
        radius = _resolved_expression((block.values("Radius") or ("",))[-1], constants)
        if amount is None or radius is None:
            raise PlayableUnitCompilerError(
                f"{label} burst-heal module has an unresolvable HealingAmount/Radius"
            )
        effect = {
            "kind": "heal",
            "module": block.kind,
            "amountKind": "flat",
            "amount": amount,
            "radius": radius,
            "onlyOthers": (_first(block.values("HealOnlyOthers")) or "").casefold()
            == "yes",
            "sourceIni": block.source_virtual_path,
            "line": block.line,
        }
        pulse_fx = _first(block.values("UnitHealPulseFX"))
        if pulse_fx is not None:
            effect["healFxId"] = pulse_fx
        kind_filter = _first(block.values("KindOf"))
        if kind_filter is not None:
            effect["affects"] = kind_filter
        return effect

    if power_enum.casefold() == "special_screech":
        # Screech / Terrible Fury / Instill Terror: the fear push is
        # hardcoded in the retail engine behind Enum SPECIAL_SCREECH — the
        # bound SpecialAbilityUpdate authors only an EffectRange plus the
        # animation envelope (no GenerateTerror, weapon, or modifier leaf),
        # so there is no authored magnitude to convert without inventing the
        # emotion push.
        ranges = [
            resolved
            for module in modules_of("specialabilityupdate")
            if (
                resolved := _resolved_expression(
                    (module.values("EffectRange") or ("",))[-1], constants
                )
            )
            is not None
        ]
        range_text = (
            f"EffectRange {ranges[0]}" if ranges else "no EffectRange resolves"
        )
        raise PlayableUnitCompilerError(
            f"{label} SPECIAL_SCREECH fear push is engine-hardcoded: the "
            f"bound SpecialAbilityUpdate authors only the cast envelope "
            f"({range_text}), no convertible effect leaf — needs the screech "
            "emotion system"
        )

    return {"kind": "none"}


# ---------------------------------------------------------------------------
# Per-battalion upgrade purchase surface.
#
# Retail sells OBJECT upgrades on the horde's own command set (forged blades,
# heavy armor, fire arrows, Basic Training): an OBJECT_UPGRADE command button
# gated by its NeededUpgrade PLAYER technology, priced by the OBJECT block in
# upgrade.ini.  The horde's LevelUpUpgrade modules (the Basic Training banner
# carrier across every faction) author the level effect such a purchase
# applies.  Both compile here verbatim; nothing is inferred from unit class.
# ---------------------------------------------------------------------------

_OBJECT_UPGRADE_COMMAND = "object_upgrade"


def _upgrade_purchase_commands(
    target: SageObject,
    target_lineage: Sequence[SageObject],
    command_sets: Mapping[str, IniBlock],
    command_buttons: Mapping[str, IniBlock],
    documents: Mapping[str, bytes],
    constants: Mapping[str, int | float],
) -> list[dict[str, object]]:
    """Compile the unit's authored OBJECT_UPGRADE purchase buttons."""

    label = f"unit {target.name}"
    command_values = [
        value
        for row in _effective_values(target_lineage, "CommandSet")
        if (value := _first((row.value,))) is not None
    ]
    if not command_values:
        return []
    command_set = command_sets.get(command_values[0].casefold())
    if command_set is None:
        return []
    rows: list[dict[str, object]] = []
    for slot, command_id in _command_slots(command_set):
        button = command_buttons.get(command_id.casefold())
        if button is None:
            raise PlayableUnitCompilerError(
                f"CommandSet {command_set.name} references a missing "
                f"CommandButton: {command_id}"
            )
        command_kinds = {value.strip().casefold() for value in button.values("Command")}
        if command_kinds != {_OBJECT_UPGRADE_COMMAND}:
            continue
        upgrades = [
            token
            for value in button.values("Upgrade")
            for token in _tokens(value)
            if token.casefold() not in {"none", "null"}
        ]
        if len(upgrades) != 1:
            raise PlayableUnitCompilerError(
                f"{label} CommandButton {command_id} names an ambiguous "
                "upgrade purchase"
            )
        upgrade_id = upgrades[0]
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
        row: dict[str, object] = {
            "upgradeId": upgrade_id,
            "commandId": command_id,
            "commandSetId": command_set.name,
            "slot": slot,
            "cancelable": "cancelable" in options,
            "multiSelect": "ok_for_multi_select" in options,
            "neededUpgradeAny": any(
                value.strip().casefold() in {"yes", "true", "1"}
                for value in button.values("NeededUpgradeAny")
            ),
        }
        if needed:
            row["neededUpgradeIds"] = needed
        for field, output_key in (
            ("TextLabel", "labelId"),
            ("DescriptLabel", "tooltipId"),
            ("ButtonImage", "buttonImageId"),
            ("LacksPrerequisiteLabel", "lacksPrerequisiteLabelId"),
        ):
            for value in button.values(field):
                text = value.strip()
                if text and text.casefold() not in {"none", "null"}:
                    row[output_key] = text
                    break
        rows.append(row)
    if not rows:
        return []
    upgrade_source = _optional_document(documents, UPGRADE_PATH)
    if upgrade_source is None:
        raise PlayableUnitCompilerError(
            f"{label} authors upgrade purchases but {UPGRADE_PATH} is not in "
            "the effective INI view"
        )
    upgrade_blocks = _named_blocks(upgrade_source, "Upgrade")
    for row in rows:
        upgrade_id = str(row["upgradeId"])
        upgrade_block = upgrade_blocks.get(upgrade_id.casefold())
        if upgrade_block is None:
            raise PlayableUnitCompilerError(
                f"{label} purchase {upgrade_id} has no {UPGRADE_PATH} block"
            )
        upgrade_type = _first(upgrade_block.values("Type"))
        if upgrade_type is None or upgrade_type.strip().casefold() != "object":
            raise PlayableUnitCompilerError(
                f"{label} purchase {upgrade_id} is not an OBJECT upgrade"
            )
        cost_expression = _first(upgrade_block.values("BuildCost"))
        time_expression = _first(upgrade_block.values("BuildTime"))
        if cost_expression is None or time_expression is None:
            raise PlayableUnitCompilerError(
                f"{label} purchase {upgrade_id} lacks authored BuildCost/BuildTime"
            )
        cost = _resolved_expression(cost_expression, constants)
        build_time = _resolved_expression(time_expression, constants)
        if cost is None or build_time is None:
            raise PlayableUnitCompilerError(
                f"{label} purchase {upgrade_id} has an unresolvable "
                "BuildCost/BuildTime"
            )
        row["cost"] = cost
        row["buildTimeSeconds"] = build_time
    rows.sort(key=lambda row: (int(row["slot"]), str(row["upgradeId"]).casefold()))
    return rows


_BANNER_POSITION_RE = re.compile(
    r"UnitType\s*:\s*(?P<unit>[A-Za-z0-9_+'.-]+)\s*"
    r"Pos\s*:\s*X\s*:\s*(?P<x>[+-]?(?:\d+(?:\.\d*)?|\.\d+))\s*"
    r"Y\s*:\s*(?P<y>[+-]?(?:\d+(?:\.\d*)?|\.\d+))",
    re.IGNORECASE,
)


_HORDE_CONTAIN_KINDS = frozenset(
    {
        "hordecontain",
        "horsehordecontain",
        "transportcontain",  # defensive; banner fields rarely appear here
    }
)


def _banner_field_assignments(
    target_lineage: Sequence[SageObject], key: str
) -> tuple[SageAssignment, ...]:
    """Collect banner fields from Object assignments and HordeContain modules.

    Retail authors ``BannerCarriersAllowed`` / ``BannerCarrierPosition`` on the
    HordeContain Behavior block (not as free Object key/values). Object-level
    rows still win when present so ObjectReskin overrides stay source-backed.
    """

    object_rows = _effective_values(target_lineage, key)
    if object_rows:
        return object_rows
    folded = key.casefold()
    selected: tuple[SageAssignment, ...] = ()
    for block in _effective_top_blocks(target_lineage):
        if (block.header_key or "").casefold() != "behavior":
            continue
        if block.kind.casefold() not in _HORDE_CONTAIN_KINDS:
            continue
        values = tuple(
            row for row in block.assignments if row.key.casefold() == folded
        )
        if values:
            selected = values
    return selected


def _banner_carrier_contract(
    target: SageObject,
    target_lineage: Sequence[SageObject],
) -> dict[str, object] | None:
    """Compile retail horde banner-carrier fields for RotWK/BFME2 sim.

    Retail hordes author ``BannerCarriersAllowed`` (banner object ids) and
    optional ``BannerCarrierPosition`` / ``BannerCarrierMinLevel`` on the
    HordeContain module. When ``BannerCarrierMinLevel`` is omitted, standard
    skirmish hordes unlock the carrier at level 2 (the common RotWK/BFME2
    rank-up rule); thrall-style authors set min level to 0 explicitly.
    """

    label = f"unit {target.name}"
    allowed: list[str] = []
    for row in _banner_field_assignments(target_lineage, "BannerCarriersAllowed"):
        for token in _tokens(row.value):
            if token.casefold() in {"none", "null", ""}:
                continue
            if token not in allowed:
                allowed.append(token)
    if not allowed:
        return None

    positions: list[dict[str, object]] = []
    for row in _banner_field_assignments(target_lineage, "BannerCarrierPosition"):
        match = _BANNER_POSITION_RE.search(row.value)
        if match is None:
            raise PlayableUnitCompilerError(
                f"{label} BannerCarrierPosition is not parseable: {row.value!r}"
            )
        positions.append(
            {
                "unitType": match.group("unit"),
                "x": float(match.group("x")),
                "y": float(match.group("y")),
                "sourceIni": row.source_virtual_path,
                "line": int(row.line),
            }
        )

    min_level = 2
    min_level_authored = False
    for row in _banner_field_assignments(target_lineage, "BannerCarrierMinLevel"):
        raw = row.value.strip()
        if not re.fullmatch(r"[0-9]+", raw):
            raise PlayableUnitCompilerError(
                f"{label} BannerCarrierMinLevel is invalid: {row.value!r}"
            )
        min_level = int(raw)
        min_level_authored = True

    destroy_on_death = False
    for row in _banner_field_assignments(
        target_lineage, "BannerCarrierDestroyHordeOnDeath"
    ):
        token = (row.value or "").strip().casefold()
        if token in {"yes", "true", "1"}:
            destroy_on_death = True
        elif token in {"no", "false", "0"}:
            destroy_on_death = False
        else:
            raise PlayableUnitCompilerError(
                f"{label} BannerCarrierDestroyHordeOnDeath is invalid: {row.value!r}"
            )

    return {
        "allowedObjectIds": allowed,
        "positions": positions,
        "minLevel": min_level,
        "minLevelDefaulted": not min_level_authored,
        "destroyHordeOnBannerDeath": destroy_on_death,
    }


def _banner_carrier_update_contract(
    target: SageObject,
    target_lineage: Sequence[SageObject],
) -> dict[str, object] | None:
    """Compile only the retail-authored banner replacement timers.

    A banner may be replaced by its horde only when its own
    ``BannerCarrierUpdate`` authors ``DiedRespawnTime``.  The companion melee
    timer is retained as a second lower bound.  Missing fields are not given
    engine defaults: absence means no executable respawn contract.
    """

    selected: SageBlock | None = None
    for block in _effective_top_blocks(target_lineage):
        if (
            (block.header_key or "").casefold() == "behavior"
            and block.kind.casefold() == "bannercarrierupdate"
        ):
            selected = block
    if selected is None:
        return None

    fields: dict[str, dict[str, object]] = {}
    for assignment in selected.assignments:
        folded = assignment.key.casefold()
        if folded not in {"diedrespawntime", "meleefreebannerrespawntime"}:
            continue
        raw = assignment.value.strip()
        if re.fullmatch(r"[0-9]+", raw) is None:
            raise PlayableUnitCompilerError(
                f"unit {target.name} {assignment.key} is invalid: "
                f"{assignment.value!r}"
            )
        fields[folded] = {
            "milliseconds": int(raw),
            "sourceIni": assignment.source_virtual_path,
            "line": int(assignment.line),
        }

    died = fields.get("diedrespawntime")
    if died is None:
        return None
    result: dict[str, object] = {"diedRespawnTime": died}
    melee = fields.get("meleefreebannerrespawntime")
    if melee is not None:
        result["meleeFreeBannerRespawnTime"] = melee
    return result


def _level_up_upgrades(
    target: SageObject,
    target_lineage: Sequence[SageObject],
) -> tuple[list[dict[str, object]], frozenset[tuple[str, int, str, str]]]:
    """Compile the container's LevelUpUpgrade modules (Basic Training).

    Returns the upgrade rows plus the consumed module identities so the
    modules leave the unsupported-capability record they used to occupy.
    """

    label = f"unit {target.name}"
    rows: list[dict[str, object]] = []
    consumed: set[tuple[str, int, str, str]] = set()
    for block in _effective_top_blocks(target_lineage):
        if (block.header_key or "").casefold() != "behavior":
            continue
        if block.kind.casefold() != "levelupupgrade":
            continue
        triggers = tuple(
            token
            for value in block.values("TriggeredBy")
            for token in _tokens(value)
            if token.casefold() not in {"none", "null"}
        )
        if not triggers:
            raise PlayableUnitCompilerError(
                f"{label} LevelUpUpgrade has no TriggeredBy upgrade"
            )
        gains_raw = _first(block.values("LevelsToGain"))
        cap_raw = _first(block.values("LevelCap"))
        if (
            gains_raw is None
            or not re.fullmatch(r"[0-9]+", gains_raw.strip())
            or int(gains_raw.strip()) < 1
        ):
            raise PlayableUnitCompilerError(
                f"{label} LevelUpUpgrade has no valid LevelsToGain"
            )
        if cap_raw is None or not re.fullmatch(r"[0-9]+", cap_raw.strip()):
            raise PlayableUnitCompilerError(
                f"{label} LevelUpUpgrade has no valid LevelCap"
            )
        for upgrade_id in triggers:
            rows.append(
                {
                    "upgradeId": upgrade_id,
                    "levelsToGain": int(gains_raw.strip()),
                    "levelCap": int(cap_raw.strip()),
                    "sourceIni": block.source_virtual_path,
                    "line": int(block.line),
                }
            )
        consumed.add(
            (
                block.source_virtual_path.casefold(),
                block.line,
                (block.instance_tag or "").casefold(),
                block.kind.casefold(),
            )
        )
    rows.sort(key=lambda row: str(row["upgradeId"]).casefold())
    return rows, frozenset(consumed)


def compile_playable_unit_descriptor(
    target_id: str,
    documents: Mapping[str, bytes],
    *,
    converted_visuals: Mapping[str, Mapping[str, object]] | None = None,
    resolved_images: Mapping[str, Mapping[str, object]] | None = None,
    resolved_audio: Mapping[str, Sequence[str]] | None = None,
    resolved_strings: Mapping[str, str] | None = None,
    faction_graph: Mapping[str, object] | None = None,
    prepared: PlayableUnitCompilerInputs | None = None,
    game: str = "bfme2",
    engine_spawned_banner_carrier: bool = False,
) -> dict[str, object]:
    """Compile one source-backed descriptor or fail on an unresolved core edge."""

    if not target_id or len(target_id) > 256:
        raise PlayableUnitCompilerError("target Object id is invalid")
    if prepared is None:
        prepared = prepare_playable_unit_compiler(documents)
    elif prepared.documents is not documents:
        raise PlayableUnitCompilerError(
            "prepared compiler inputs belong to a different document mapping"
        )
    objects = prepared.objects
    requested_target = objects.get(target_id.casefold())
    if requested_target is None:
        raise PlayableUnitCompilerError(f"effective Object is missing: {target_id}")
    command_sets = prepared.command_sets
    command_buttons = prepared.command_buttons
    reachable_object_ids: frozenset[str] | None = None
    audio_edges_by_object: dict[str, frozenset[tuple[str, str]]] | None = None
    command_audio: dict[str, tuple[Mapping[str, object], ...]] = {}
    hero_roster: list[str] = []
    ring_roster: list[str] = []
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
        hero_roster, ring_roster, starting_building, player_template_id = (
            _player_template_context(
                documents, faction_graph, prepared.player_templates
            )
        )
    target = requested_target
    is_roster_hero = target.name.casefold() in {
        value.casefold() for value in hero_roster
    }
    ring_matches = [
        index + 1
        for index, value in enumerate(ring_roster)
        if value.casefold() == target.name.casefold()
    ]
    is_ring_hero = bool(ring_matches) and not is_roster_hero
    direct_error: PlayableUnitCompilerError | None = None
    direct_producers: tuple[dict[str, object], ...] = ()
    try:
        direct_producers = _producer_bindings(
            target.name, objects, command_sets, command_buttons, reachable_object_ids
        )
    except PlayableUnitCompilerError as error:
        # Fortress citadels that castle-unpack creates (e.g. MenFortressCitadel)
        # author UNIT_BUILD for porters/builders but often sit outside the
        # player-template reachability set. When the filtered search fails,
        # retry without the reachability gate and keep same-Side producers.
        if reachable_object_ids is not None and "no producer CommandSet reaches" in str(
            error
        ):
            try:
                unrestricted = _producer_bindings(
                    target.name, objects, command_sets, command_buttons, None
                )
                target_side = {
                    value.casefold()
                    for row in _effective_values([target], "Side")
                    for value in (row.value,)
                    if value
                }
                same_side: list[dict[str, object]] = []
                for route in unrestricted:
                    producer = objects.get(str(route["producerObjectId"]).casefold())
                    if producer is None:
                        continue
                    producer_side = {
                        value.casefold()
                        for row in _effective_values(
                            _ancestry(objects, producer), "Side"
                        )
                        for value in (row.value,)
                        if value
                    }
                    if target_side and producer_side and target_side.isdisjoint(
                        producer_side
                    ):
                        continue
                    same_side.append(route)
                if same_side:
                    direct_producers = tuple(same_side)
                else:
                    direct_error = error
            except PlayableUnitCompilerError:
                direct_error = error
        else:
            direct_error = error
    if is_roster_hero and is_ring_hero:
        raise PlayableUnitCompilerError(
            f"hero {target.name} has conflicting BuildableHeroesMP and "
            "BuildableRingHeroesMP roster routes"
        )
    if is_roster_hero or is_ring_hero:
        if is_ring_hero and len(ring_matches) != 1:
            raise PlayableUnitCompilerError(
                f"PlayerTemplate {player_template_id} has duplicate "
                f"BuildableRingHeroesMP hero: {target.name}"
            )
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
        ring_prerequisites: list[str] = []
        if is_ring_hero:
            slot_button = command_buttons.get("command_ringheroreviveslot")
            if slot_button is None:
                raise PlayableUnitCompilerError(
                    "ring hero route has no authored Command_RingHeroReviveSlot"
                )
            needed_values = _block_values(slot_button, "NeededUpgrade")
            ring_prerequisites = (
                list(_tokens(needed_values[0])) if len(needed_values) == 1 else []
            )
            if ring_prerequisites != [
                "Upgrade_RingHero",
                "Upgrade_FortressRingHero",
            ]:
                raise PlayableUnitCompilerError(
                    "Command_RingHeroReviveSlot NeededUpgrade must require "
                    "Upgrade_RingHero and Upgrade_FortressRingHero"
                )
            # The ring hero revives at the fortress after the regular hero
            # roster slots, so its engine ordinal continues that sequence.
            roster_ordinals = (len(hero_roster) + ring_matches[0],)
            command_set_id = "__engine__/BuildableRingHeroesMP"
            command_id = f"__engine__/RING_HERO_BUILD/{target.name}"
            source_field = "BuildableRingHeroesMP"
        else:
            # Retail authors duplicate roster slots for heroes recruitable in
            # several independent fortress slots (for example three Nazgul);
            # each authored slot remains its own producer route.
            roster_ordinals = tuple(
                index + 1
                for index, value in enumerate(hero_roster)
                if value.casefold() == target.name.casefold()
            )
            command_set_id = "__engine__/BuildableHeroesMP"
            command_id = f"__engine__/HERO_BUILD/{target.name}"
            source_field = "BuildableHeroesMP"
        producers = tuple(
            {
                "producerObjectId": objects[starting_building.casefold()].name,
                "commandSetId": command_set_id,
                "commandId": command_id,
                "surface": "hero-roster",
                "rosterOrdinal": roster_ordinal,
                # This shared emission also serves ordinary fortress heroes.
                # Only the authored ring-revive path inherits the slot's
                # ALL-of NeededUpgrade pair.
                "prerequisites": list(ring_prerequisites),
                "commandSetTransition": [],
                "sourceField": source_field,
                "sourcePlayerTemplate": player_template_id,
                "ui": {},
            }
            for roster_ordinal in roster_ordinals
        )
    elif direct_producers:
        producers = direct_producers
    elif "BANNER" in {
        kind.upper()
        for kind in _kind_of(_ancestry(objects, target))
    } or engine_spawned_banner_carrier:
        # Banner carriers are not UNIT_BUILD targets. Hordes spawn them via
        # BannerCarriersAllowed when the battalion reaches min level. Keep a
        # single engine surface so descriptors remain production-accounted.
        producers = (
            {
                "producerObjectId": target.name,
                "commandSetId": "__engine__/BannerCarriersAllowed",
                "commandId": f"__engine__/BANNER_CARRIER/{target.name}",
                "surface": "banner-carrier",
                "prerequisites": [],
                "commandSetTransition": [],
                "sourceField": "BannerCarriersAllowed",
                "evidence": (
                    "kindof-banner"
                    if "BANNER" in {kind.upper() for kind in _kind_of(_ancestry(objects, target))}
                    else "banner-carriers-allowed-edge"
                ),
                "ui": {},
            },
        )
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
                continue
            except PlayableUnitCompilerError:
                pass
            # A horde which is itself only fielded as another produced horde's
            # payload still brings its own payload into play through that
            # ancestor's authored production route.  Bind the direct container
            # to the ancestor's routes; ambiguity stays fail-closed below.
            for ancestor in _horde_containers(container.name, objects):
                try:
                    reachable.append(
                        (
                            container,
                            _producer_bindings(
                                ancestor.name,
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
        target, target_lineage, objects, prepared.numeric_defines
    )
    level_upgrades, consumed_level_up_modules = _level_up_upgrades(
        target, target_lineage
    )
    consumed_container_modules = consumed_container_modules | consumed_level_up_modules
    banner_carrier = _banner_carrier_contract(target, target_lineage)
    banner_carrier_update = _banner_carrier_update_contract(target, target_lineage)
    upgrade_commands = _upgrade_purchase_commands(
        target,
        target_lineage,
        command_sets,
        command_buttons,
        documents,
        prepared.numeric_defines,
    )
    member_lineage = _ancestry(objects, primary_member)
    # SAGE picks a horde by hit-testing one member's authored Geometry, so the
    # member's block is the footprint a runtime needs (the container Object has
    # none of its own).
    member_geometry = _geometry_contract(member_lineage, prepared.numeric_defines)
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
    weapon_audio_routes, weapon_audio_gaps = _weapon_audio_routes(
        target_lineage,
        member_lineage,
        documents,
        named_definition_cache=prepared.named_definition_cache,
        cache_lock=prepared.cache_lock,
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
    consumed_lock_modules = _behavior_module_identities(
        member_lineage, "LockWeaponCreate"
    )
    experience_level_create = _experience_level_create(target_lineage)
    if experience_level_create is not None:
        consumed_container_modules = (
            consumed_container_modules
            | _behavior_module_identities(
                target_lineage, "ExperienceLevelCreate"
            )
        )
    destroy_die_policies: list[dict[str, object]] = []
    slow_death_fades: list[dict[str, object]] = []
    if target_lineage[-1].name.casefold() == member_lineage[-1].name.casefold():
        object_policies, consumed_destroy_die = _destroy_die_policies(
            target_lineage, "object"
        )
        destroy_die_policies.extend(object_policies)
        consumed_container_modules = (
            consumed_container_modules | consumed_destroy_die
        )
        slow_death_fades.extend(
            _slow_death_fade_rows(
                target_lineage, "object", prepared.numeric_defines
            )
        )
    else:
        container_policies, consumed_container_destroy_die = (
            _destroy_die_policies(target_lineage, "container")
        )
        member_policies, consumed_member_destroy_die = _destroy_die_policies(
            member_lineage, "primaryMember"
        )
        destroy_die_policies.extend(container_policies)
        destroy_die_policies.extend(member_policies)
        consumed_container_modules = (
            consumed_container_modules | consumed_container_destroy_die
        )
        slow_death_fades.extend(
            _slow_death_fade_rows(
                target_lineage, "container", prepared.numeric_defines
            )
        )
        slow_death_fades.extend(
            _slow_death_fade_rows(
                member_lineage, "primaryMember", prepared.numeric_defines
            )
        )
    try:
        module_contracts = compile_all_module_contracts(
            target_lineage, target_lineage[-1].name
        )
        if target_lineage[-1].name.casefold() != member_lineage[-1].name.casefold():
            member_contracts = compile_all_module_contracts(
                member_lineage, member_lineage[-1].name
            )
            # Prefer container contracts first, then primary-member contracts.
            module_contracts = module_contracts + member_contracts
    except ModuleContractError as error:
        raise PlayableUnitCompilerError(str(error)) from error
    consumed_member_modules: frozenset[tuple[str, int, str, str]] = frozenset()
    if target_lineage[-1].name.casefold() == member_lineage[-1].name.casefold():
        consumed_container_modules = (
            consumed_container_modules | consumed_lock_modules
        )
    else:
        consumed_member_modules = (
            consumed_lock_modules | consumed_member_destroy_die
        )
    module_evidence = _runtime_module_evidence(
        target_lineage,
        member_lineage,
        consumed_container_modules,
        consumed_member_modules,
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
        command_id = str(producer.get("commandId", ""))
        if not command_id.startswith(
            ("__engine__/HERO_BUILD/", "__engine__/RING_HERO_BUILD/")
        ):
            continue
        button = container_fields.get("ButtonImage")
        label = container_fields.get("DisplayName")
        tooltip = container_fields.get("DescriptionStrategic")
        if button is None or label is None:
            raise PlayableUnitCompilerError(
                f"hero {target.name} has unresolved required retail UI values"
            )
        if command_id.startswith("__engine__/RING_HERO_BUILD/"):
            # Ring heroes recruit through the fortress generic revive slot, so
            # retail authors no per-hero tooltip for them; the slot's authored
            # generic labels are the only source-backed UI text.
            slot_button = command_buttons.get("command_ringheroreviveslot")
            if slot_button is None:
                raise PlayableUnitCompilerError(
                    "ring hero route has no authored Command_RingHeroReviveSlot"
                )
            slot_labels = _block_values(slot_button, "TextLabel")
            slot_tooltips = _block_values(slot_button, "DescriptLabel")
            if not slot_labels or not slot_tooltips:
                raise PlayableUnitCompilerError(
                    "Command_RingHeroReviveSlot has unresolved retail UI values"
                )
            producer["ui"] = {
                "ButtonImage": [str(button["expression"])],
                "TextLabel": list(slot_labels),
                "DescriptLabel": list(slot_tooltips),
            }
            continue
        if tooltip is None:
            # RotWK's expansion heroes (AngmarKarsh) author no
            # DescriptionStrategic; retail's recruit-button tooltip for them
            # is the object's own authored RecruitText, so that label is the
            # source-backed fallback. Kept out of _scalar_fields so existing
            # descriptor identities stay byte-stable.
            recruit_rows = _effective_values(target_lineage, "RecruitText")
            if recruit_rows:
                recruit = recruit_rows[-1]
                tooltip = {
                    "expression": recruit.value.strip(),
                    "sourceIni": recruit.source_virtual_path,
                    "line": recruit.line,
                }
        if tooltip is None:
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
        prepared.numeric_defines,
        documents,
        target_lineage,
        flat_kind_cache=prepared.flat_kind_cache,
        named_definition_cache=prepared.named_definition_cache,
        cache_lock=prepared.cache_lock,
        hero=category == "hero",
        game=game,
        destroy_die_policies=destroy_die_policies,
        module_contracts=module_contracts,
        slow_death_fades=slow_death_fades,
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
    abilities: list[dict[str, object]] = []
    ability_power_blocks: dict[str, IniBlock] = {}
    if category == "hero":
        abilities, ability_power_blocks = _hero_abilities(
            target,
            target_lineage,
            member_lineage,
            command_sets,
            command_buttons,
            objects,
            documents,
            prepared.numeric_defines,
            named_definition_cache=prepared.named_definition_cache,
            cache_lock=prepared.cache_lock,
        )
    experience = _experience_contract(
        target_lineage,
        member_lineage,
        members,
        documents,
        prepared.numeric_defines,
        experience_level_create,
    )
    used_paths = {
        COMMAND_SET_PATH,
        COMMAND_BUTTON_PATH,
        *(item.source_virtual_path for item in target_lineage),
        *(item.source_virtual_path for item in member_lineage),
    }
    used_paths.update(_provenance_paths(simulation))
    used_paths.update(_provenance_paths(abilities))
    used_paths.update(_provenance_paths(experience))
    if upgrade_commands or level_upgrades:
        used_paths.add(UPGRADE_PATH)
    if ability_power_blocks:
        used_paths.add(SPECIAL_POWER_PATH)
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
    for path in _provenance_paths(abilities):
        semantic_scopes[path.casefold()].append(
            {"kind": "ResolvedHeroAbilities", "abilities": abilities}
        )
    for path in _provenance_paths(experience):
        semantic_scopes[path.casefold()].append(
            {"kind": "ResolvedExperienceLevels", "experience": experience}
        )
    for power_block in ability_power_blocks.values():
        semantic_scopes[SPECIAL_POWER_PATH].append(
            _ini_block_semantic("SpecialPower", power_block)
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
        template = prepared.player_templates[player_template_id.casefold()]
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
    ui_binding = _ui_binding(
        producers,
        command_buttons,
        target_lineage,
        member_lineage,
        command_audio,
        _mapped_image_size_index(faction_graph),
    )
    # Localization ids this unit's own command buttons reference. Retail
    # authors some of these with no record in data/lotr.str at all (the RotWK
    # patch added CONTROLBAR:ConstructBlackRiderHorde to commandbutton.ini and
    # never added the string), so a resolved-strings table is legitimately
    # narrower than the required set. Record the difference the same way
    # spellbook_compiler.py:2400 does instead of leaving a hole that reads as a
    # broken pack: ContentDB used to reject the whole unit document for it,
    # which is how MordorBlackRiderHorde vanished from the mordor roster.
    _required_ui_string_ids = {
        str(value)
        for command in ui_binding.get("commands", [])
        if isinstance(command, Mapping)
        for field in ("TextLabel", "DescriptLabel")
        for value in command.get("fields", {}).get(field, [])
        if value
    }
    _source_null_string_ids = sorted(
        {
            str(value).strip()
            for value in (faction_graph or {}).get("layeredSourceNullTextIds", [])
            if isinstance(value, str) and str(value).strip() in _required_ui_string_ids
        },
        key=str.casefold,
    )
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
            **(
                {"geometry": member_geometry}
                if member_geometry is not None
                else {}
            ),
            **(
                {"upgradeCommands": upgrade_commands}
                if upgrade_commands
                else {}
            ),
            **(
                {"levelUpgrades": level_upgrades}
                if level_upgrades
                else {}
            ),
            **(
                {"bannerCarrier": banner_carrier}
                if banner_carrier is not None
                else {}
            ),
            **(
                {"bannerCarrierUpdate": banner_carrier_update}
                if banner_carrier_update is not None
                else {}
            ),
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
            "ui": ui_binding,
            "sourceNullStringIds": _source_null_string_ids,
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
                "weapon": weapon_audio_routes,
            },
            **(
                {"weaponAudioGaps": weapon_audio_gaps}
                if weapon_audio_gaps
                else {}
            ),
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
    if category == "hero":
        # Hero-only contract: SPECIAL_POWER command abilities. Non-hero
        # descriptors stay byte-stable (no key emitted).
        descriptor["abilities"] = abilities
    # Every playable unit carries its experience economy contract (compiled
    # chain, or the recorded reason there is none).
    descriptor["experience"] = experience
    # Widen the retail-unlocalized record to ability buttons. Hero mount and
    # special-power buttons carry their label/tooltip ids under
    # abilities[].button, not under presentation.ui.commands -- and that is
    # exactly where the two ids that blocked every Men match live
    # (CONTROLBAR:ToolTipFaramirMount on GondorFaramir,
    # CONTROLBAR:SpecialAbilityShieldBubble on GondorGandalf).
    for ability_row in descriptor.get("abilities", []) or []:
        if not isinstance(ability_row, Mapping):
            continue
        button = ability_row.get("button")
        if not isinstance(button, Mapping):
            continue
        for field in ("labelIds", "tooltipIds"):
            for value in button.get(field, []) or []:
                if isinstance(value, str) and value:
                    _required_ui_string_ids.add(value)
    descriptor["presentation"]["sourceNullStringIds"] = sorted(
        {
            str(value).strip()
            for value in (faction_graph or {}).get("layeredSourceNullTextIds", [])
            if isinstance(value, str) and str(value).strip() in _required_ui_string_ids
        }
        - set(descriptor["presentation"]["resolvedStrings"]),
        key=str.casefold,
    )
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
        if surface not in {"command-socket", "hero-roster", "banner-carrier"}:
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
        if (
            (surface == "command-socket" and (not valid_slot or valid_ordinal))
            or (surface == "hero-roster" and (not valid_ordinal or valid_slot))
            or (surface == "banner-carrier" and (valid_slot or valid_ordinal))
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
        if "prerequisiteAnyOf" in row:
            # Optional ANY-of gate (commandbutton.ini:7513-7519
            # NeededUpgradeAny): when present it must be a non-empty list of
            # non-empty upgrade ids.
            #
            # It may OVERLAP `prerequisites`, and an earlier disjointness rule
            # here was wrong. The overlap is the normal retail shape:
            # commandbutton.ini:7517 lists
            # `Upgrade_GondorArcheryRangeLevel2 Upgrade_CustomGenericUpgrade1`
            # under NeededUpgradeAny, while
            # object/goodfaction/structures/men/archerrange.ini:418 makes that
            # same Level2 upgrade the CommandSetUpgrade trigger — so the token
            # is simultaneously an ALL-of requirement (the producer must sit on
            # the upgraded CommandSet at all) and a member of the ANY-of group.
            # That is not two semantics for one token; the ALL-of requirement
            # simply subsumes that member of the group. Demanding disjointness
            # rejected GondorRanger(Horde), GondorTowerShieldGuard(Horde) and
            # RohanRohirrim(Horde) and silently shrank the published pack.
            any_of = row.get("prerequisiteAnyOf")
            if (
                not isinstance(any_of, list)
                or not any_of
                or any(not isinstance(item, str) or not item for item in any_of)
            ):
                raise PlayableUnitCompilerError(
                    "playable-unit production any-of prerequisites are invalid"
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
    banner_carrier = gameplay.get("bannerCarrier")
    if banner_carrier is not None:
        allowed = (
            banner_carrier.get("allowedObjectIds")
            if isinstance(banner_carrier, Mapping)
            else None
        )
        positions = (
            banner_carrier.get("positions")
            if isinstance(banner_carrier, Mapping)
            else None
        )
        min_level = (
            banner_carrier.get("minLevel")
            if isinstance(banner_carrier, Mapping)
            else None
        )
        if (
            not isinstance(banner_carrier, Mapping)
            or not isinstance(allowed, list)
            or not allowed
            or any(not isinstance(item, str) or not item for item in allowed)
            or len({item.casefold() for item in allowed}) != len(allowed)
            or not isinstance(positions, list)
            or not isinstance(min_level, int)
            or isinstance(min_level, bool)
            or min_level < 0
            or not isinstance(banner_carrier.get("minLevelDefaulted"), bool)
            or not isinstance(
                banner_carrier.get("destroyHordeOnBannerDeath"), bool
            )
        ):
            raise PlayableUnitCompilerError(
                "playable-unit banner carrier contract is invalid"
            )
        for position in positions:
            if (
                not isinstance(position, Mapping)
                or not isinstance(position.get("unitType"), str)
                or not position.get("unitType")
                or not isinstance(position.get("x"), (int, float))
                or isinstance(position.get("x"), bool)
                or not math.isfinite(float(position["x"]))
                or not isinstance(position.get("y"), (int, float))
                or isinstance(position.get("y"), bool)
                or not math.isfinite(float(position["y"]))
                or not isinstance(position.get("sourceIni"), str)
                or not position.get("sourceIni")
                or not isinstance(position.get("line"), int)
                or isinstance(position.get("line"), bool)
                or int(position["line"]) <= 0
            ):
                raise PlayableUnitCompilerError(
                    "playable-unit banner carrier position is invalid"
                )
    banner_update = gameplay.get("bannerCarrierUpdate")
    if banner_update is not None:
        if not isinstance(banner_update, Mapping):
            raise PlayableUnitCompilerError(
                "playable-unit banner carrier update contract is invalid"
            )
        allowed_fields = {
            "diedRespawnTime",
            "meleeFreeBannerRespawnTime",
        }
        if (
            set(banner_update) - allowed_fields
            or "diedRespawnTime" not in banner_update
        ):
            raise PlayableUnitCompilerError(
                "playable-unit banner carrier update contract is invalid"
            )
        for field, timer in banner_update.items():
            if (
                not isinstance(timer, Mapping)
                or not isinstance(timer.get("milliseconds"), int)
                or isinstance(timer.get("milliseconds"), bool)
                or int(timer["milliseconds"]) < 0
                or not isinstance(timer.get("sourceIni"), str)
                or not timer.get("sourceIni")
                or not isinstance(timer.get("line"), int)
                or isinstance(timer.get("line"), bool)
                or int(timer["line"]) <= 0
            ):
                raise PlayableUnitCompilerError(
                    f"playable-unit banner carrier update {field} is invalid"
                )
    simulation = gameplay.get("simulation")
    resolved_simulation = (
        simulation.get("resolved") if isinstance(simulation, Mapping) else None
    )
    highlander_body = (
        resolved_simulation.get("highlanderBody")
        if isinstance(resolved_simulation, Mapping)
        else None
    )
    if highlander_body is not None and (
        not isinstance(highlander_body, Mapping)
        or highlander_body.get("value") is not True
        or highlander_body.get("module") != "HighlanderBody"
        or not isinstance(highlander_body.get("sourceIni"), str)
        or not highlander_body.get("sourceIni")
        or not isinstance(highlander_body.get("line"), int)
        or isinstance(highlander_body.get("line"), bool)
        or int(highlander_body["line"]) <= 0
    ):
        raise PlayableUnitCompilerError(
            "playable-unit HighlanderBody policy evidence is invalid"
        )
    permanent_weapon_locks = (
        resolved_simulation.get("permanentWeaponLocks")
        if isinstance(resolved_simulation, Mapping)
        else None
    )
    if permanent_weapon_locks is not None:
        combat = resolved_simulation.get("combat")
        valid_lock = (
            isinstance(permanent_weapon_locks, list)
            and len(permanent_weapon_locks) == 1
            and isinstance(permanent_weapon_locks[0], Mapping)
        )
        lock = permanent_weapon_locks[0] if valid_lock else {}
        if (
            not valid_lock
            or lock.get("slot") != "PRIMARY"
            or lock.get("state") != "LOCKED_PERMANENTLY"
            or lock.get("module") != "LockWeaponCreate"
            or not isinstance(lock.get("sourceIni"), str)
            or not lock.get("sourceIni")
            or not isinstance(lock.get("line"), int)
            or isinstance(lock.get("line"), bool)
            or int(lock["line"]) <= 0
            or not isinstance(combat, Mapping)
            or combat.get("weaponSlot") != "PRIMARY"
        ):
            raise PlayableUnitCompilerError(
                "playable-unit LockWeaponCreate policy evidence is invalid"
            )
    module_contracts_value = (
        resolved_simulation.get("moduleContracts")
        if isinstance(resolved_simulation, Mapping)
        else None
    )
    if module_contracts_value is not None:
        try:
            validate_module_contracts(
                module_contracts_value, label="playable-unit"
            )
        except ModuleContractError as error:
            raise PlayableUnitCompilerError(str(error)) from error
    destroy_die = (
        resolved_simulation.get("destroyDie")
        if isinstance(resolved_simulation, Mapping)
        else None
    )
    if destroy_die is not None:
        if not isinstance(destroy_die, list) or not destroy_die:
            raise PlayableUnitCompilerError(
                "playable-unit DestroyDie policy evidence is invalid"
            )
        for policy in destroy_die:
            if (
                not isinstance(policy, Mapping)
                or policy.get("ownerRole")
                not in {"object", "container", "primaryMember"}
                or policy.get("module") != "DestroyDie"
                or policy.get("deathTypes") != "ALL"
                or policy.get("excludedDeathTypes") not in ([], ["TOPPLED"])
                or not isinstance(policy.get("sourceIni"), str)
                or not policy.get("sourceIni")
                or not isinstance(policy.get("line"), int)
                or isinstance(policy.get("line"), bool)
                or int(policy["line"]) <= 0
            ):
                raise PlayableUnitCompilerError(
                    "playable-unit DestroyDie policy evidence is invalid"
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
    engine_spawned_banner = all(
        row.get("surface") == "banner-carrier" for row in production
    )
    if (
        not engine_spawned_banner
        and not portraits
        and not any(command.get("fields") for command in commands)
    ):
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
    weapon_routes = audio_routes.get("weapon")
    if not isinstance(weapon_routes, Mapping):
        raise PlayableUnitCompilerError(
            "playable-unit weapon audio routes are invalid"
        )
    for rows in weapon_routes.values():
        if not isinstance(rows, list) or any(
            not isinstance(row, Mapping)
            or not isinstance(row.get("id"), str)
            or not row.get("id")
            or not isinstance(row.get("weaponId"), str)
            or not row.get("weaponId")
            or not isinstance(row.get("fxListId"), str)
            or not row.get("fxListId")
            or not isinstance(row.get("sourceIni"), str)
            or not isinstance(row.get("line"), int)
            or not isinstance(row.get("fxSourceIni"), str)
            or not isinstance(row.get("fxLine"), int)
            for row in rows
        ):
            raise PlayableUnitCompilerError(
                "playable-unit weapon audio route row is invalid"
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
    source_null_string_ids = presentation.get("sourceNullStringIds")
    if (
        not isinstance(source_null_string_ids, list)
        or any(
            not isinstance(item, str) or not item for item in source_null_string_ids
        )
        or len({item.casefold() for item in source_null_string_ids})
        != len(source_null_string_ids)
        or any(item in presentation["resolvedStrings"] for item in source_null_string_ids)
    ):
        raise PlayableUnitCompilerError(
            "playable-unit source-null strings are invalid"
        )
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
    abilities = value.get("abilities")
    if category == "hero":
        if not isinstance(abilities, list):
            raise PlayableUnitCompilerError("playable-unit hero abilities are invalid")
    elif abilities is not None:
        raise PlayableUnitCompilerError(
            "playable-unit non-hero descriptor must not carry abilities"
        )
    for row in abilities or []:
        slot = row.get("slot") if isinstance(row, Mapping) else None
        if (
            not isinstance(row, Mapping)
            or not isinstance(row.get("id"), str)
            or not row.get("id")
            or not isinstance(slot, int)
            or isinstance(slot, bool)
            or slot < 1
            or not isinstance(row.get("specialPowerId"), str)
            or (
                not row.get("specialPowerId")
                # TOGGLE_WEAPONSET commands author no SpecialPower template.
                and row.get("command") != "TOGGLE_WEAPONSET"
            )
            or row.get("targeting") not in {"self", "point", "enemy-object"}
            or not isinstance(row.get("sourceIni"), str)
            or not row.get("sourceIni")
        ):
            raise PlayableUnitCompilerError("playable-unit ability row is invalid")
        cooldown = row.get("cooldownMs")
        if cooldown is not None and (
            not isinstance(cooldown, (int, float))
            or isinstance(cooldown, bool)
            or cooldown < 0
        ):
            raise PlayableUnitCompilerError("playable-unit ability cooldown is invalid")
        button = row.get("button")
        if not isinstance(button, Mapping):
            raise PlayableUnitCompilerError("playable-unit ability button is invalid")
        for field in ("iconIds", "labelIds", "tooltipIds"):
            values = button.get(field)
            if not isinstance(values, list) or any(
                not isinstance(item, str) or not item for item in values
            ):
                raise PlayableUnitCompilerError(
                    "playable-unit ability button leaves are invalid"
                )
        options = button.get("options")
        if options is not None and (
            not isinstance(options, list)
            or any(not isinstance(item, str) or not item for item in options)
        ):
            raise PlayableUnitCompilerError(
                "playable-unit ability button options are invalid"
            )
        gate = row.get("levelGate")
        if gate is not None:
            required = gate.get("requiredLevel") if isinstance(gate, Mapping) else None
            if (
                not isinstance(gate, Mapping)
                or not isinstance(gate.get("upgradeIds"), list)
                or not gate["upgradeIds"]
                or any(
                    not isinstance(item, str) or not item
                    for item in gate["upgradeIds"]
                )
                or (
                    required is not None
                    and (
                        not isinstance(required, int)
                        or isinstance(required, bool)
                        or required < 1
                    )
                )
            ):
                raise PlayableUnitCompilerError(
                    "playable-unit ability level gate is invalid"
                )
        effect = row.get("effect")
        if not isinstance(effect, Mapping) or effect.get("kind") not in {
            "none",
            "weapon-blast",
            "heal",
            "summon",
            "attribute-modifier",
            "leadership-aura",
            "weapon-toggle",
            "terror",
            "mount-toggle",
            "capture-building",
            "experience-grant",
            "arrow-storm",
            "stealth-toggle",
            "teleport",
            "curse",
            "leadership-strip",
        }:
            raise PlayableUnitCompilerError("playable-unit ability effect is invalid")
        implementation = row.get("implementation")
        if (
            not isinstance(implementation, Mapping)
            or implementation.get("status")
            not in {"implemented", "unimplemented", "passive"}
            or not isinstance(implementation.get("reason"), str)
            or not isinstance(implementation.get("limitations"), list)
            or any(
                not isinstance(item, str)
                for item in implementation["limitations"]
            )
        ):
            raise PlayableUnitCompilerError(
                "playable-unit ability implementation is invalid"
            )
        if implementation["status"] == "implemented" and (
            effect["kind"] == "none" or "cooldownMs" not in row
        ):
            raise PlayableUnitCompilerError(
                "playable-unit implemented ability lacks its effect or cooldown"
            )
        if implementation["status"] == "unimplemented" and effect["kind"] != "none":
            raise PlayableUnitCompilerError(
                "playable-unit unavailable ability must not carry an effect"
            )
        if implementation["status"] == "passive" and effect["kind"] not in {
            "none",
            # Passive leadership buttons carry the aura the runtime radiates.
            "leadership-aura",
        }:
            raise PlayableUnitCompilerError(
                "playable-unit passive ability carries a non-passive effect"
            )
        modules = row.get("modules")
        if not isinstance(modules, list) or any(
            not isinstance(module, Mapping)
            or not isinstance(module.get("kind"), str)
            or not module.get("kind")
            or not isinstance(module.get("instanceTag"), str)
            or not isinstance(module.get("sourceIni"), str)
            or not isinstance(module.get("line"), int)
            for module in modules
        ):
            raise PlayableUnitCompilerError(
                "playable-unit ability module evidence is invalid"
            )
    experience = value.get("experience")
    if not isinstance(experience, Mapping):
        raise PlayableUnitCompilerError("playable-unit experience contract is invalid")
    status = experience.get("status")
    if status not in {"compiled", "unauthored", "unavailable"}:
        raise PlayableUnitCompilerError("playable-unit experience status is invalid")
    if status == "compiled":
        max_level = experience.get("maxLevel")
        initial_rank = experience.get("initialRank")
        levels = experience.get("levels")
        if (
            not isinstance(max_level, int)
            or isinstance(max_level, bool)
            or max_level < 1
            or not isinstance(initial_rank, int)
            or isinstance(initial_rank, bool)
            or initial_rank < 1
            or initial_rank > max_level
            or not isinstance(levels, list)
            or not levels
        ):
            raise PlayableUnitCompilerError(
                "playable-unit experience level table is invalid"
            )
        creation_grant = experience.get("experienceLevelCreate")
        if creation_grant is not None and (
            not isinstance(creation_grant, Mapping)
            or creation_grant.get("module") != "ExperienceLevelCreate"
            or creation_grant.get("mpOnly") is not False
            or creation_grant.get("rank") != initial_rank
            or not isinstance(creation_grant.get("sourceIni"), str)
            or not creation_grant.get("sourceIni")
            or not isinstance(creation_grant.get("line"), int)
            or isinstance(creation_grant.get("line"), bool)
            or int(creation_grant["line"]) <= 0
        ):
            raise PlayableUnitCompilerError(
                "playable-unit ExperienceLevelCreate evidence is invalid"
            )
        expected_previous = 0
        for level_row in levels:
            if not isinstance(level_row, Mapping):
                raise PlayableUnitCompilerError(
                    "playable-unit experience level row is invalid"
                )
            rank = level_row.get("rank")
            required_xp = level_row.get("requiredExperience")
            award = level_row.get("experienceAward")
            award_unknown = (
                award is None
                and level_row.get("experienceAwardStatus") == "unauthored"
                and creation_grant is not None
                and rank <= initial_rank
            )
            if (
                not isinstance(rank, int)
                or isinstance(rank, bool)
                or rank <= expected_previous
                or not isinstance(required_xp, (int, float))
                or isinstance(required_xp, bool)
                or required_xp < 0
                or (
                    not award_unknown
                    and (
                        not isinstance(award, (int, float))
                        or isinstance(award, bool)
                        or award < 0
                    )
                )
                or (
                    award is not None
                    and "experienceAwardStatus" in level_row
                )
            ):
                raise PlayableUnitCompilerError(
                    "playable-unit experience level row is invalid"
                )
            expected_previous = rank
            for leaf in level_row.get("attributeModifiers", []):
                if not isinstance(leaf, Mapping):
                    raise PlayableUnitCompilerError(
                        "playable-unit experience modifier leaf is invalid"
                    )
                for modifier in leaf.get("modifiers", []):
                    application = (
                        modifier.get("application")
                        if isinstance(modifier, Mapping)
                        else None
                    )
                    if (
                        not isinstance(modifier, Mapping)
                        or modifier.get("kind")
                        not in {
                            "HEALTH",
                            "DAMAGE_ADD",
                            "PRODUCTION",
                            "DAMAGE_MULT",
                            "SPELL_DAMAGE",
                        }
                        or not isinstance(modifier.get("value"), (int, float))
                        or isinstance(modifier.get("value"), bool)
                        or application
                        not in {"additive", "multiplicative"}
                    ):
                        raise PlayableUnitCompilerError(
                            "playable-unit experience modifier row is invalid"
                        )
        if (
            expected_previous != max_level
            or (
                creation_grant is None
                and initial_rank != 1
            )
            or (
                creation_grant is not None
                and sum(
                    1
                    for level_row in levels
                    if int(level_row["rank"]) == initial_rank
                )
                != 1
            )
        ):
            raise PlayableUnitCompilerError(
                "playable-unit experience level table is invalid"
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
    "PlayableUnitCompilerInputs",
    "PlayableUnitCompilerError",
    "SCHEMA",
    "SCHEMA_VERSION",
    "compile_playable_unit_descriptor",
    "prepare_playable_unit_compiler",
    "playable_object_kind_of",
    "validate_playable_unit_descriptor",
]
