"""Compile BFME2 retail references into one faction spellbook descriptor.

A BFME2 faction spellbook is a PlayerTemplate-bound purchase tree: the spell
store CommandSet sells sciences, the spell book CommandSet casts special
powers, and spell-power Behavior modules on the SpellBook object bind each
power to its effect leaves (ObjectCreationList, Weapon, FXList/particles,
attribute modifiers, upgrades, audio, and button art).  This module resolves
that tree from the faction census graph plus the effective INI view and fails
closed on any missing or unsupported leaf; it never substitutes generic
placeholders.  Conversion and pack publication remain separate stages.
"""

from __future__ import annotations

from collections.abc import Mapping, Sequence
import hashlib
import json
from pathlib import PurePosixPath
import re

from .playable_unit_compiler import (
    PlayableUnitCompilerError,
    PlayableUnitCompilerInputs,
    EXPERIENCE_LEVELS_PATH,
    _ancestry,
    _command_slots,
    _default_set_target,
    _effective_body_health,
    _effective_top_blocks,
    _effective_values,
    _experience_contract,
    _experience_level_create,
    _first,
    _kind_of,
    _named_blocks,
    _named_definition_values,
    _numeric_defines as _playable_numeric_defines,
    _resolved_multiplicative_expression,
    _default_weapon_slot,
    _permanent_weapon_locks,
    _resolved_expression,
    _resolved_multiplicative_expression,
    _resolved_set_field,
    _tokens,
    prepare_playable_unit_compiler,
)
from .playable_unit_import import FACTIONS, ROTWK_FACTIONS
from .retail_men_damage_effects import parse_fx_lists
from .sage_cst import SageBlock, SageObject
from .sage_gameplay import _digest as _gameplay_digest
from .sage_ini import IniBlock, parse_flat_named_blocks
from .sage_particles import parse_particle_definitions, select_particle_definition
from .locomotor_compiler import (
    compile_locomotor_templates,
    resolve_locomotor_template,
    turn_rate_degrees_per_second,
)


SCHEMA = "openbfme.spellbook-descriptor"
SCHEMA_VERSION = 0
SCIENCE_PATH = "data/ini/science.ini"
SPECIAL_POWER_PATH = "data/ini/specialpower.ini"
OBJECT_CREATION_LIST_PATH = "data/ini/objectcreationlist.ini"
FX_LIST_PATH = "data/ini/fxlist.ini"
ATTRIBUTE_MODIFIER_PATH = "data/ini/attributemodifier.ini"
WEAPON_PATH = "data/ini/weapon.ini"
UPGRADE_PATH = "data/ini/upgrade.ini"
FX_PARTICLE_PATH = "data/ini/fxparticlesystem.ini"
PLAYER_TEMPLATE_PATH = "data/ini/playertemplate.ini"
LOCOMOTOR_PATH = "data/ini/locomotor.ini"
RANK_PATH = "data/ini/rank.ini"

_SPELL_BOOK_KIND = "SPELL_BOOK"
_PURCHASE_COMMAND = "purchase_science"
_CAST_COMMAND = "spell_book"
_MAX_EFFECT_MODULES = 512
_MAX_NESTED_BLOCKS = 65_536

# The compiler deliberately admits spell-power module kinds dynamically so a
# mod can add a new Behavior without changing an importer allow-list.  The
# module census cannot infer that dynamic consumption from ``block.kind``, so
# name the retail kinds whose typed Godot runtime seams are independently
# covered.  This is evidence for the census, never an admission gate.
_SPELLBOOK_RUNTIME_EFFECT_MODULE_KINDS = frozenset(
    {
        "FreezingRainSpecialPower",
        "ScavengerSpecialPower",
        "UntamedAllegianceSpecialPower",
    }
)

# Behavior-module fields bound to typed effect leaves.  Any other
# reference-shaped field on a spell-power module fails closed below.
_MODULE_OCL_FIELDS = frozenset({"ocl", "healocl", "elvenwoodocl", "taintocl"})
_MODULE_FX_FIELDS = frozenset({"triggerfx", "healfx", "elvenwoodfx", "taintfx", "fx"})
_MODULE_MODIFIER_FIELDS = frozenset({"attributemodifier"})
_MODULE_UPGRADE_FIELDS = frozenset({"upgradename"})
_MODULE_OBJECT_FIELDS = frozenset({"sunbeamobject", "elvengroveobject", "taintobject"})
_MODULE_WEAPON_FIELDS = frozenset({"weapon", "weaponname", "fireweapon"})
_MODIFIER_FX_FIELDS = frozenset({"fx", "fx2", "fx3"})
_FX_PARTICLE_SECTION = "particlesystem"
_FX_SOUND_SECTION = "sound"
_NULL_TOKENS = frozenset({"none", "null", "0"})


class SpellbookCompilerError(ValueError):
    """The requested spellbook descriptor cannot be derived without guessing."""


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


def _sha256(value: object, field: str) -> str:
    if (
        not isinstance(value, str)
        or len(value) != 64
        or any(character not in "0123456789abcdef" for character in value)
    ):
        raise SpellbookCompilerError(f"{field} must be a lowercase SHA-256 identity")
    return value


def _required_document(documents: Mapping[str, bytes], path: str) -> bytes:
    for candidate, payload in documents.items():
        if candidate.replace("\\", "/").casefold() == path.casefold():
            return payload
    raise SpellbookCompilerError(f"required effective source is missing: {path}")


def _numeric_defines(source: bytes, label: str) -> dict[str, int | float]:
    result: dict[str, int | float] = {}
    pattern = re.compile(
        rb"(?m)^[ \t]*#define[ \t]+([A-Za-z_][A-Za-z0-9_]*)[ \t]+(-?(?:[0-9]+(?:\.[0-9]*)?|\.[0-9]+))[ \t]*(?://|;|\r?$)"
    )
    for match in pattern.finditer(source):
        key = match.group(1).decode("ascii").casefold()
        token = match.group(2).decode("ascii")
        value: int | float = float(token) if "." in token else int(token)
        if key in result and result[key] != value:
            raise SpellbookCompilerError(f"ambiguous numeric {label} constant")
        result[key] = value
    return result


def _text_defines(source: bytes) -> dict[str, str]:
    """Single-line #define constants with free-text values (object filters).

    Values keep their authored token order; the numeric-only parser above
    deliberately skips these.
    """

    result: dict[str, str] = {}
    pattern = re.compile(
        rb"(?m)^[ \t]*#define[ \t]+([A-Za-z_][A-Za-z0-9_]*)[ \t]+([^\r\n;]+?)[ \t]*(?://|;|\r?$)"
    )
    for match in pattern.finditer(source):
        key = match.group(1).decode("ascii").casefold()
        value = match.group(2).decode("ascii").strip()
        if not value or value.replace(".", "").replace("-", "").isdigit():
            continue
        result.setdefault(key, value)
    return result


def _merged_defines(
    documents: Mapping[str, bytes], prepared: PlayableUnitCompilerInputs
) -> dict[str, int | float]:
    constants = dict(prepared.numeric_defines)
    for path, source in sorted(
        documents.items(), key=lambda item: (item[0].casefold(), item[0])
    ):
        normalized = path.replace("\\", "/").casefold()
        if not normalized.startswith("data/ini/") or not normalized.endswith(
            (".ini", ".inc")
        ):
            continue
        for key, value in _numeric_defines(source, path).items():
            previous = constants.setdefault(key, value)
            if previous != value:
                raise SpellbookCompilerError(
                    f"conflicting GameData constant across spellbook sources: {key}"
                )
    return constants


def _resolved_field(
    expression: str,
    constants: Mapping[str, int | float],
    label: str,
) -> dict[str, object]:
    stripped = expression.strip()
    value = _resolved_expression(stripped, constants)
    if value is None:
        # RotWK authors a few dual-token radii as ``DEFINE literal`` after the
        # BFME2 ``DEFINE  ; ;literal`` comment form lost its comment markers
        # (e.g. ``SPAWN_UNDERMINE_DECAL_RADIUS 95.0``). Prefer the define when
        # it resolves; otherwise accept a trailing numeric literal only when
        # the leading token is a known constant name that failed evaluation.
        parts = stripped.split()
        if len(parts) >= 2:
            head = _resolved_expression(parts[0], constants)
            if head is not None:
                value = head
            else:
                tail = _resolved_expression(parts[-1], constants)
                if tail is not None and parts[0].casefold() in constants:
                    value = tail
    if value is None:
        raise SpellbookCompilerError(f"{label} has unresolved expression: {expression}")
    return {"value": value, "expression": expression}


def _optional_scalar(
    block: IniBlock, field: str, constants: Mapping[str, int | float], label: str
) -> dict[str, object] | None:
    values = block.values(field)
    if not values:
        return None
    if len(values) != 1:
        raise SpellbookCompilerError(f"{label} has ambiguous {field}")
    return _resolved_field(values[0].strip(), constants, f"{label} {field}")


def _required_scalar(
    block: IniBlock, field: str, constants: Mapping[str, int | float], label: str
) -> dict[str, object]:
    result = _optional_scalar(block, field, constants, label)
    if result is None:
        raise SpellbookCompilerError(f"{label} has no authored {field}")
    return result


def _one_value(block: IniBlock, field: str, label: str) -> str | None:
    values = block.values(field)
    if len(values) > 1:
        raise SpellbookCompilerError(f"{label} has ambiguous {field}")
    return values[0] if values else None


def _document(documents: Mapping[str, bytes], path: str) -> bytes | None:
    for candidate, payload in documents.items():
        if candidate.replace("\\", "/").casefold() == path.casefold():
            return payload
    return None


def _flat_assignment_line(
    source: bytes, kind: str, name: str, field: str
) -> int:
    header = re.compile(
        rf"^\s*{re.escape(kind)}\s+{re.escape(name)}\s*(?:;.*)?$",
        re.IGNORECASE,
    )
    assignment = re.compile(rf"^\s*{re.escape(field)}\s*=", re.IGNORECASE)
    active = False
    found = 0
    for line_number, raw in enumerate(source.decode("cp1252").splitlines(), 1):
        if not active:
            active = header.match(raw) is not None
            continue
        if raw.strip().casefold() == "end":
            break
        if assignment.match(raw):
            found = line_number
    if found <= 0:
        raise SpellbookCompilerError(
            f"{kind} {name} has no line receipt for {field}"
        )
    return found


def _field_contract(
    source: bytes,
    block: IniBlock,
    field: str,
    value: object,
    source_ini: str,
) -> dict[str, object] | None:
    authored_values = block.values(field)
    if not authored_values:
        return None
    return {
        "authored": " ".join(item.strip() for item in authored_values),
        "value": value,
        "sourceIni": source_ini,
        "line": _flat_assignment_line(source, block.kind, block.name, field),
    }


def compile_rank_science_grants(
    documents: Mapping[str, bytes],
) -> list[dict[str, object]]:
    """Compile the Rank.ini player ladder with exact authored receipts.

    Two authored fields per ``Rank`` make the ladder executable:
    ``SkillPointsNeededDefault`` is the threshold that promotes the player and
    ``SciencePurchasePointsGranted`` is the spell-point award that promotion
    pays.  Thresholds must ascend with rank, or the ladder has no single
    crossing point and the grant cannot be attributed to one rank.

    ``SkillPointsNeededCampaign`` is deliberately NOT compiled: it is the
    single-player campaign ladder and no runtime consumes a campaign player
    rank, so emitting it would ship an unconsumed contract.
    """

    source = _document(documents, RANK_PATH)
    if source is None:
        raise SpellbookCompilerError(f"spellbook source document is missing: {RANK_PATH}")
    constants = _playable_numeric_defines(documents)
    rows: list[dict[str, object]] = []
    seen: set[int] = set()
    for block in parse_flat_named_blocks(source, "Rank"):
        if not block.name.isdigit() or int(block.name) < 1:
            raise SpellbookCompilerError(f"Rank has invalid identity: {block.name}")
        rank = int(block.name)
        if rank in seen:
            raise SpellbookCompilerError(f"Rank is ambiguous: {rank}")
        seen.add(rank)
        authored = _one_value(
            block, "SciencePurchasePointsGranted", f"Rank {rank}"
        )
        if authored is None:
            raise SpellbookCompilerError(
                f"Rank {rank} has no authored SciencePurchasePointsGranted"
            )
        resolved = _resolved_expression(authored.strip(), constants)
        if (
            resolved is None
            or isinstance(resolved, bool)
            or float(resolved) < 0
            or not float(resolved).is_integer()
        ):
            raise SpellbookCompilerError(
                f"Rank {rank} has unresolved SciencePurchasePointsGranted: {authored}"
            )
        threshold_authored = _one_value(
            block, "SkillPointsNeededDefault", f"Rank {rank}"
        )
        if threshold_authored is None:
            raise SpellbookCompilerError(
                f"Rank {rank} has no authored SkillPointsNeededDefault"
            )
        threshold = _resolved_multiplicative_expression(
            threshold_authored.strip(), constants
        )
        if (
            threshold is None
            or isinstance(threshold, bool)
            or float(threshold) < 0
            or not float(threshold).is_integer()
        ):
            raise SpellbookCompilerError(
                f"Rank {rank} has unresolved SkillPointsNeededDefault: "
                f"{threshold_authored}"
            )
        rows.append(
            {
                "rank": rank,
                "sciencePurchasePointsGranted": {
                    "authored": authored.strip(),
                    "value": int(resolved),
                    "sourceIni": RANK_PATH,
                    "line": _flat_assignment_line(
                        source, "Rank", block.name, "SciencePurchasePointsGranted"
                    ),
                },
                "skillPointsNeededDefault": {
                    "authored": threshold_authored.strip(),
                    "value": int(threshold),
                    "sourceIni": RANK_PATH,
                    "line": _flat_assignment_line(
                        source, "Rank", block.name, "SkillPointsNeededDefault"
                    ),
                },
            }
        )
    rows.sort(key=lambda row: int(row["rank"]))
    if not rows:
        raise SpellbookCompilerError(f"{RANK_PATH} has no Rank definitions")
    previous = -1
    for row in rows:
        threshold_value = int(row["skillPointsNeededDefault"]["value"])
        if threshold_value <= previous:
            raise SpellbookCompilerError(
                f"Rank {row['rank']} SkillPointsNeededDefault does not ascend: "
                f"{threshold_value} follows {previous}"
            )
        previous = threshold_value
    return rows


def _definition_rows(
    graph: Mapping[str, object], family: str
) -> dict[str, Mapping[str, object]]:
    definitions = graph.get("definitions")
    if not isinstance(definitions, Mapping):
        raise SpellbookCompilerError("faction graph definitions are invalid")
    rows = definitions.get(family)
    if not isinstance(rows, list):
        raise SpellbookCompilerError(f"faction graph {family} definitions are invalid")
    result: dict[str, Mapping[str, object]] = {}
    for row in rows:
        if not isinstance(row, Mapping) or not isinstance(row.get("id"), str):
            raise SpellbookCompilerError(f"faction graph {family} row is invalid")
        key = row["id"].casefold()
        if key in result:
            raise SpellbookCompilerError(f"faction graph has duplicate {family} row")
        _sha256(
            row.get("definitionSha256"),
            f"faction graph {family} definitionSha256",
        )
        result[key] = row
    return result


def _dependency_ids(graph: Mapping[str, object], family: str) -> tuple[str, ...]:
    dependencies = graph.get("dependencies")
    if not isinstance(dependencies, Mapping):
        raise SpellbookCompilerError("faction graph dependencies are invalid")
    values = dependencies.get(family)
    if not isinstance(values, list) or any(
        not isinstance(item, str) for item in values
    ):
        raise SpellbookCompilerError(f"faction graph {family} dependencies are invalid")
    if len({item.casefold() for item in values}) != len(values):
        raise SpellbookCompilerError(f"faction graph has duplicate {family} ids")
    return tuple(values)


def _graph_context(
    graph: Mapping[str, object],
) -> tuple[str, str, str, str, str, tuple[str, ...]]:
    target = graph.get("target")
    if not isinstance(target, Mapping):
        raise SpellbookCompilerError("faction graph target is invalid")
    template = target.get("playerTemplate")
    faction = target.get("faction")
    if not isinstance(template, str) or not template:
        raise SpellbookCompilerError("faction graph playerTemplate is invalid")
    if not isinstance(faction, str) or not faction:
        raise SpellbookCompilerError("faction graph faction is invalid")
    expected = next(
        (
            spec[2]
            for spec in (*FACTIONS, *ROTWK_FACTIONS)
            if spec[1].casefold() == template.casefold()
        ),
        None,
    )
    if expected is None or faction != expected:
        raise SpellbookCompilerError(
            "faction graph playerTemplate/faction identity pair is invalid"
        )
    graph_identity = _sha256(graph.get("inputSetSha256"), "factionGraphInputSetSha256")
    summary = graph.get("summary")
    if not isinstance(summary, Mapping):
        raise SpellbookCompilerError("faction graph summary is invalid")
    unresolved = summary.get("unresolvedCount")
    if not isinstance(unresolved, int) or isinstance(unresolved, bool) or unresolved < 0:
        raise SpellbookCompilerError("faction graph unresolvedCount is invalid")
    if unresolved != 0:
        # Expansion installs (RotWK 2.01) mount the base game's archives at
        # runtime, so an expansion-only catalog legitimately leaves base-game
        # payload leaves unresolved graph-wide: audio sample files, mapped
        # image textures, and base-authored objects. The spellbook never
        # consumes those families through the graph-wide count — its own
        # requirements (mappedImages incl. compiledTextureVirtualPath, audio
        # sample closure, strings) resolve individually fail-closed in the
        # media/strings lane. Every leaf family the spellbook does bind
        # through the graph must still be fully resolved here; anything
        # unaccounted for keeps the historical failure.
        failure = SpellbookCompilerError(
            f"faction graph has {unresolved} unresolved census leaves"
        )
        diagnostics = graph.get("unresolved")
        if not isinstance(diagnostics, Mapping):
            # No per-family diagnostics: nothing can be proven tolerable.
            raise failure
        tolerated = {"missingobjects", "missingmappedimagetextures", "missingaudiosamples"}
        blocking = 0
        accounted = 0
        for family, items in diagnostics.items():
            if not isinstance(family, str) or not isinstance(items, list):
                raise failure
            accounted += len(items)
            if family.casefold() not in tolerated:
                blocking += len(items)
        if blocking != 0 or accounted != unresolved:
            raise failure
    roots = graph.get("roots")
    if not isinstance(roots, list):
        raise SpellbookCompilerError("faction graph roots are invalid")

    spellbook_id: str | None = None
    store_set_id: str | None = None
    intrinsic: dict[str, str] = {}
    for row in roots:
        if not isinstance(row, Mapping):
            raise SpellbookCompilerError("faction graph root row is invalid")
        field = str(row.get("sourceField", ""))
        identifier = row.get("id")
        if not isinstance(identifier, str) or not identifier:
            raise SpellbookCompilerError("faction graph root id is invalid")
        folded = field.casefold()
        if folded == "spellbookmp":
            if spellbook_id is not None:
                raise SpellbookCompilerError(
                    "faction graph has multiple SpellBookMP roots"
                )
            spellbook_id = identifier
        elif folded == "purchasesciencecommandsetmp":
            if store_set_id is not None:
                raise SpellbookCompilerError(
                    "faction graph has multiple PurchaseScienceCommandSetMP roots"
                )
            store_set_id = identifier
        elif folded == "intrinsicsciencesmp":
            if str(row.get("edgeKind", "")) != "science":
                raise SpellbookCompilerError("intrinsic science root kind is invalid")
            intrinsic.setdefault(identifier.casefold(), identifier)
    if spellbook_id is None or store_set_id is None:
        raise SpellbookCompilerError(
            "faction graph has no SpellBookMP/PurchaseScienceCommandSetMP roots"
        )
    return (
        template,
        faction,
        graph_identity,
        spellbook_id,
        store_set_id,
        tuple(sorted(intrinsic.values(), key=lambda item: (item.casefold(), item))),
    )


def _player_template_check(
    prepared: PlayableUnitCompilerInputs,
    template_id: str,
    spellbook_id: str,
    store_set_id: str,
) -> None:
    if not prepared.player_templates:
        raise SpellbookCompilerError(
            f"required effective source is missing: {PLAYER_TEMPLATE_PATH}"
        )
    template = prepared.player_templates.get(template_id.casefold())
    if template is None:
        raise SpellbookCompilerError(
            f"effective PlayerTemplate is missing: {template_id}"
        )
    book = _first(template.values("SpellBookMP"))
    store = _first(template.values("PurchaseScienceCommandSetMP"))
    if book is None or book.casefold() != spellbook_id.casefold():
        raise SpellbookCompilerError(
            f"PlayerTemplate {template_id} SpellBookMP disagrees with the census"
        )
    if store is None or store.casefold() != store_set_id.casefold():
        raise SpellbookCompilerError(
            f"PlayerTemplate {template_id} PurchaseScienceCommandSetMP disagrees "
            "with the census"
        )


def _button(prepared: PlayableUnitCompilerInputs, command_id: str) -> IniBlock:
    block = prepared.command_buttons.get(command_id.casefold())
    if block is None:
        raise SpellbookCompilerError(
            f"effective CommandButton is missing: {command_id}"
        )
    return block


def _first_token(value: str) -> str | None:
    """Return the first whitespace-separated token, dropping SAGE null markers."""

    tokens = value.split()
    if not tokens:
        return None
    token = tokens[0]
    if token.casefold() in _NULL_TOKENS or token.startswith("$"):
        return None
    return token


def _button_leaf_fields(button: IniBlock) -> dict[str, object]:
    options = sorted(
        {
            token
            for value in button.values("Options")
            for token in _tokens(value)
            if token.casefold() not in _NULL_TOKENS
        },
        key=str.casefold,
    )
    row: dict[str, object] = {
        "commandId": button.name,
        "iconIds": [
            token
            for value in button.values("ButtonImage")
            if (token := _first_token(value)) is not None
        ],
        "textIds": sorted(
            {
                token
                for field in ("TextLabel", "DescriptLabel")
                for value in button.values(field)
                if (token := _first_token(value)) is not None
            },
            key=str.casefold,
        ),
    }
    if options:
        row["options"] = options
    cursor = _first(button.values("RadiusCursorType"))
    if cursor is not None:
        row["radiusCursorType"] = cursor
    return row


def _unique_button_target(
    button: IniBlock, field: str, command_kind: str, label: str
) -> str:
    commands = {value.strip().casefold() for value in button.values("Command")}
    if commands != {command_kind}:
        raise SpellbookCompilerError(
            f"{label} command {button.name} must be {command_kind.upper()}"
        )
    targets = [
        token
        for value in button.values(field)
        for token in _tokens(value)
        if token.casefold() not in _NULL_TOKENS
    ]
    if len(targets) != 1:
        raise SpellbookCompilerError(
            f"{label} command {button.name} must name exactly one {field}"
        )
    return targets[0]


def _unique_blocks(source: bytes, kind: str, path: str) -> dict[str, IniBlock]:
    try:
        return _named_blocks(source, kind)
    except ValueError as exc:
        raise SpellbookCompilerError(
            f"{path} has ambiguous {kind} blocks: {exc}"
        ) from exc


def _cross_check_definition(
    block: IniBlock,
    census_rows: Mapping[str, Mapping[str, object]],
    kind: str,
) -> str:
    row = census_rows.get(block.name.casefold())
    if row is None:
        raise SpellbookCompilerError(
            f"{kind} {block.name} is absent from the faction census"
        )
    digest = _gameplay_digest(block)
    if digest != str(row["definitionSha256"]):
        raise SpellbookCompilerError(
            f"{kind} {block.name} no longer matches its census definition digest"
        )
    return digest


def _prerequisite_groups(block: IniBlock) -> tuple[tuple[str, ...], ...]:
    tokens = [
        token
        for value in block.values("PrerequisiteSciences")
        for token in value.split()
    ]
    groups: list[list[str]] = [[]]
    authored = False
    for token in tokens:
        if token.casefold() in _NULL_TOKENS:
            continue
        if token.casefold() == "or":
            authored = True
            if not groups[-1]:
                raise SpellbookCompilerError(
                    f"Science {block.name} has an empty PrerequisiteSciences group"
                )
            groups.append([])
            continue
        if not token.startswith("SCIENCE_"):
            raise SpellbookCompilerError(
                f"Science {block.name} has a non-science prerequisite: {token}"
            )
        authored = True
        groups[-1].append(token)
    if not authored:
        return ()
    if not groups[-1]:
        raise SpellbookCompilerError(
            f"Science {block.name} ends with an empty PrerequisiteSciences group"
        )
    return tuple(tuple(group) for group in groups)


_INCLUDE_DIRECTIVE = re.compile(r'^#include\s+"([^"]+)"$', re.IGNORECASE)
_MAX_INCLUDE_DEPTH = 4


def _document_lines(
    source: bytes,
    path: str,
    documents: Mapping[str, bytes] | None,
    depth: int = 0,
) -> list[str]:
    """Comment-stripped significant lines with SAGE ``#include`` expansion.

    RotWK 2.01 authors ``#include`` directives inside definition bodies
    (weapon.ini's Weapon MordorBalrogBreath pulls balrogdotbreath.inc), so the
    engine's inline-at-directive semantics must be mirrored here. Includes
    resolve relative to the including document's directory against the
    supplied document view and fail closed when absent, oversized, or nested
    beyond a bounded depth — never skipped silently.
    """

    if len(source) > 16 * 1024 * 1024 or b"\0" in source:
        raise SpellbookCompilerError(f"{path} is unbounded")
    try:
        text = source.decode("cp1252")
    except UnicodeDecodeError as exc:
        raise SpellbookCompilerError(f"{path} has unsupported encoding") from exc
    lines: list[str] = []
    for raw in text.splitlines():
        line = re.sub(r"\s+", " ", raw.split(";", 1)[0].split("//", 1)[0]).strip()
        if not line:
            continue
        include = _INCLUDE_DIRECTIVE.fullmatch(line)
        if include is None or documents is None:
            lines.append(line)
            continue
        if depth >= _MAX_INCLUDE_DEPTH:
            raise SpellbookCompilerError(
                f"{path} exceeds the bounded #include depth"
            )
        relative = PurePosixPath(include.group(1).replace("\\", "/"))
        resolved_parts: list[str] = list(PurePosixPath(path).parent.parts)
        for part in relative.parts:
            if part == "..":
                if not resolved_parts:
                    raise SpellbookCompilerError(
                        f"{path} has an escaping #include: {include.group(1)!r}"
                    )
                resolved_parts.pop()
            elif part not in {"", "."}:
                resolved_parts.append(part)
        include_path = "/".join(resolved_parts)
        payload = documents.get(include_path)
        if payload is None:
            folded = include_path.casefold()
            payload = next(
                (
                    value
                    for key, value in documents.items()
                    if key.replace("\\", "/").casefold() == folded
                ),
                None,
            )
        if payload is None:
            raise SpellbookCompilerError(
                f"{path} #include target is missing: {include.group(1)!r}"
            )
        lines.extend(
            _document_lines(payload, include_path, documents, depth + 1)
        )
    return lines


def _nested_named_blocks(
    source: bytes,
    kind: str,
    path: str,
    documents: Mapping[str, bytes] | None = None,
) -> dict[str, dict[str, object]]:
    """Parse one flat-nested SAGE family such as ObjectCreationList or Weapon.

    Each named block carries ordered scalar assignments and ordered nested
    sections; sections carry their own assignments in source order.  The parser
    is deliberately lexical: it preserves authored payload without interpreting
    it and fails closed on unbalanced or duplicate blocks.  When ``documents``
    is supplied, authored ``#include`` directives inline their target first
    (SAGE engine semantics); without it the historical lexical view applies.
    """

    # RotWK 2.01 retail authors trailing junk after block names ("Weapon
    # AvalancheCrush  ."); SAGE's token scanner reads the first two tokens
    # and ignores the rest, so the header match must too — a stricter match
    # would drop the whole block and leave its Ends stray.
    header = re.compile(r"^" + re.escape(kind) + r"\s+(\S+)(?:\s.*)?$", re.IGNORECASE)
    lines = _document_lines(source, path, documents)
    result: dict[str, dict[str, object]] = {}
    index = 0
    block_count = 0
    while index < len(lines):
        match = header.fullmatch(lines[index])
        if match is None:
            if lines[index].casefold() == "end":
                raise SpellbookCompilerError(f"{path} has a stray top-level End")
            index += 1
            continue
        name = match.group(1)
        key = name.casefold()
        if key in result:
            raise SpellbookCompilerError(f"{path} has a duplicate {kind}: {name}")
        block_count += 1
        if block_count > _MAX_NESTED_BLOCKS:
            raise SpellbookCompilerError(f"{path} exceeds the {kind} block limit")
        assignments: list[tuple[str, str]] = []
        sections: list[dict[str, object]] = []
        stack: list[dict[str, object]] = []
        cursor = index + 1
        closed = False
        while cursor < len(lines):
            line = lines[cursor]
            if line.casefold() == "end":
                if not stack:
                    closed = True
                    cursor += 1
                    break
                stack.pop()
                cursor += 1
                continue
            if "=" in line:
                field, value = (part.strip() for part in line.split("=", 1))
                if not field or not value:
                    raise SpellbookCompilerError(
                        f"{path} {kind} {name} has a malformed assignment"
                    )
                if stack:
                    target = stack[-1]["assignments"]
                    assert isinstance(target, list)
                    target.append((field, value))
                else:
                    assignments.append((field, value))
                cursor += 1
                continue
            if header.fullmatch(line):
                raise SpellbookCompilerError(
                    f"{path} has an unterminated {kind} block: {name}"
                )
            tokens = line.split()
            if len(tokens) != 1:
                raise SpellbookCompilerError(
                    f"{path} {kind} {name} has an unsupported statement: {line!r}"
                )
            section: dict[str, object] = {
                "kind": tokens[0],
                "assignments": [],
                "sections": [],
            }
            if stack:
                nested = stack[-1]["sections"]
                assert isinstance(nested, list)
                nested.append(section)
            else:
                sections.append(section)
            stack.append(section)
            cursor += 1
        if not closed:
            raise SpellbookCompilerError(
                f"{path} has an unterminated {kind} block: {name}"
            )
        result[key] = {"id": name, "assignments": assignments, "sections": sections}
        index = cursor
    return result


_PARTICLE_SYS_BONE_FIELD = re.compile(r'"([^"]*)"|(\S+)')


def _particle_sys_bone_fields(value: str) -> tuple[str, str] | None:
    """Split one ``ParticleSysBone`` value into its bone and system fields.

    The authored grammar is ``<bone> <system> [Key:Value ...]``, and the bone
    field may be a DOUBLE-QUOTED string containing spaces — retail RotWK
    authors ``ParticleSysBone = "Bip L Finger2" SoWolf_Ambient_fog
    FollowBone:YES`` on AngmarShadeWolf (neutralunits.ini:5969).  Splitting
    that on whitespace makes the second field ``L``, i.e. a bone-name fragment
    read as a particle-system name, so the quoting has to be honoured here
    rather than by the generic identifier tokenizer.

    Returns ``None`` when the value does not carry both fields.
    """

    fields: list[str] = []
    for match in _PARTICLE_SYS_BONE_FIELD.finditer(value):
        quoted, bare = match.group(1), match.group(2)
        fields.append(quoted if quoted is not None else bare)
        if len(fields) == 2:
            break
    if len(fields) < 2 or not fields[1]:
        return None
    return fields[0], fields[1]


def _fx_field_values(section: Mapping[str, object], field: str) -> list[str]:
    values: list[str] = []
    for assignment in section.get("assignments", []):
        if not isinstance(assignment, Mapping):
            raise SpellbookCompilerError("FXList section payload is invalid")
        key = str(assignment.get("field", ""))
        if key.casefold() == field.casefold():
            values.append(str(assignment.get("value", "")))
    return values


class _LeafResolver:
    """Bounded resolution of the effect-leaf families behind one spellbook."""

    def __init__(
        self,
        documents: Mapping[str, bytes],
        prepared: PlayableUnitCompilerInputs,
        constants: Mapping[str, int | float],
        census_upgrades: Mapping[str, Mapping[str, object]],
    ) -> None:
        self._prepared = prepared
        self._constants = constants
        self._ocls = _nested_named_blocks(
            _required_document(documents, OBJECT_CREATION_LIST_PATH),
            "ObjectCreationList",
            OBJECT_CREATION_LIST_PATH,
            documents=documents,
        )
        self._weapons = _nested_named_blocks(
            _required_document(documents, WEAPON_PATH),
            "Weapon",
            WEAPON_PATH,
            documents=documents,
        )
        self._fx_lists = parse_fx_lists(_required_document(documents, FX_LIST_PATH))
        self._modifiers = _unique_blocks(
            _required_document(documents, ATTRIBUTE_MODIFIER_PATH),
            "ModifierList",
            ATTRIBUTE_MODIFIER_PATH,
        )
        self._upgrades = _unique_blocks(
            _required_document(documents, UPGRADE_PATH), "Upgrade", UPGRADE_PATH
        )
        self._census_upgrades = census_upgrades
        particle_source = _required_document(documents, FX_PARTICLE_PATH)
        self._particle_definitions = list(parse_particle_definitions(particle_source))
        # Lazily filled by _particle_family; classification evidence only.
        self._legacy_particle_names: set[str] | None = None
        self._documents = documents
        self._text_defines = _text_defines(
            _required_document(documents, "data/ini/gamedata.ini")
        )
        self.ocls: dict[str, dict[str, object]] = {}
        self.fx_lists: dict[str, dict[str, object]] = {}
        self.weapons: dict[str, dict[str, object]] = {}
        self.modifiers: dict[str, dict[str, object]] = {}
        self.upgrades: dict[str, dict[str, object]] = {}
        self.objects: dict[str, dict[str, object]] = {}
        self.particles: dict[str, dict[str, object]] = {}
        self.audio_ids: dict[str, str] = {}
        self.used_locomotor = False

    def object_reference(self, identifier: str, label: str) -> None:
        key = identifier.casefold()
        if key in self.objects:
            return
        target = self._prepared.objects.get(key)
        if target is None:
            raise SpellbookCompilerError(
                f"{label} references a missing Object: {identifier}"
            )
        # Reserve a placeholder before projecting so BuildVariations (and similar
        # recursive object graphs common in RotWK) cannot re-enter this object
        # and recurse until Python's stack dies.
        self.objects[key] = {
            "id": target.name,
            "projectionStatus": "in-progress",
            "sourceIni": target.source_virtual_path,
            "line": target.line,
        }
        try:
            projected = self._project_effect_object(target, label)
            self.objects[key] = projected
        except Exception:
            self.objects.pop(key, None)
            raise

    def _resolve_numeric(self, expression: str) -> int | float | None:
        return _resolved_multiplicative_expression(expression, self._constants)

    def _project_effect_object(
        self, target: SageObject, label: str
    ) -> dict[str, object]:
        """Bounded structured projection of one effect-referenced Object.

        The effect families the runtime consumes are converted with full
        inheritance applied (Body/health, summon lifetimes, hatch OCLs, horde
        payloads, weapon sets, fire-weapon nuggets, attribute-modifier auras,
        locomotors).  Any other Behavior module on the object is recorded by
        name in ``unconvertedBehaviors`` so the runtime gate can hold the
        power locked with the exact gap — evidence is never silently dropped.
        """

        lineage = _ancestry(self._prepared.objects, target)
        leaf: dict[str, object] = {
            "id": target.name,
            "kindOf": list(_kind_of(lineage)),
            "sourceIni": target.source_virtual_path,
            "line": target.line,
        }
        for source_name, output_name in (
            ("EquivalentTo", "equivalentTo"),
            ("Scale", "scale"),
            ("VisionRange", "visionRange"),
            ("CommandPoints", "commandPoints"),
        ):
            values = list(_effective_values(lineage, source_name))
            if len(values) == 1:
                resolved = self._resolve_numeric(values[0].value.strip())
                leaf[output_name] = (
                    resolved if resolved is not None else values[0].value.strip()
                )
        health = _effective_body_health(lineage, self._constants)
        body_kinds = {
            block.kind
            for block in _effective_top_blocks(lineage)
            if (block.header_key or "").casefold() == "body"
        }
        if body_kinds:
            leaf["bodyKinds"] = sorted(body_kinds, key=str.casefold)
        if health is not None:
            leaf["maxHealth"] = health["value"]
        if any(kind.casefold() == "immortalbody" for kind in body_kinds):
            leaf["immortal"] = True
        weapon_name = _default_set_target(lineage, "WeaponSet", "Weapon")
        if weapon_name is not None:
            leaf["weaponId"] = self.weapon(weapon_name, f"{label} WeaponSet")
            weapon_slot = _default_weapon_slot(lineage, weapon_name)
            if weapon_slot is not None:
                leaf["weaponSlot"] = weapon_slot
        try:
            permanent_weapon_locks = _permanent_weapon_locks(lineage, weapon_name)
        except PlayableUnitCompilerError as error:
            raise SpellbookCompilerError(f"{label}: {error}") from error
        if permanent_weapon_locks:
            leaf["permanentWeaponLocks"] = permanent_weapon_locks
        try:
            experience_level_create = _experience_level_create(lineage)
        except PlayableUnitCompilerError as error:
            raise SpellbookCompilerError(f"{label}: {error}") from error
        if experience_level_create is not None:
            leaf["experienceLevelCreate"] = experience_level_create
            try:
                leaf["experience"] = _experience_contract(
                    lineage,
                    lineage,
                    (),
                    self._documents,
                    self._constants,
                    experience_level_create,
                )
            except PlayableUnitCompilerError as error:
                raise SpellbookCompilerError(f"{label}: {error}") from error
        locomotor_name = _default_set_target(lineage, "LocomotorSet", "Locomotor")
        if locomotor_name is not None:
            self._project_locomotor(leaf, lineage, locomotor_name, label)
        variations = list(_effective_values(lineage, "BuildVariations"))
        if len(variations) == 1:
            variation_ids: list[str] = []
            for token in _tokens(variations[0].value):
                self.object_reference(token, f"{label} BuildVariations")
                variation_ids.append(self.objects[token.casefold()]["id"])
            if variation_ids:
                leaf["buildVariations"] = variation_ids
        draw = self._project_draw(lineage)
        if draw:
            leaf["draw"] = draw
        unconverted: set[str] = set()
        for block in _effective_top_blocks(lineage):
            if (block.header_key or "").casefold() != "behavior":
                continue
            kind = block.kind.casefold()
            if kind == "lifetimeupdate":
                if not self._project_lifetime(leaf, block):
                    unconverted.add(block.kind)
            elif kind == "deletionupdate":
                self._project_deletion(leaf, block)
            elif kind == "slowdeathbehavior":
                # Every SlowDeath module records its destruction-delay
                # evidence row (the authored FADED fade window used to be
                # dropped silently -- the named compiler gap pinned in
                # goal_spellbook_matrix_runner.gd). Hatch-bearing modules
                # additionally keep the hatch contract below.
                if not self._project_slow_death(leaf, block):
                    unconverted.add(block.kind)
                if block.values("OCL") and not self._project_hatch(
                    leaf, block, label
                ):
                    slow_death_ocls = leaf.setdefault(
                        "unconvertedSlowDeathOcls", []
                    )
                    assert isinstance(slow_death_ocls, list)
                    for value in block.values("OCL"):
                        tokens = _tokens(value)
                        if tokens:
                            slow_death_ocls.append(tokens[-1])
                    unconverted.add(block.kind)
            elif kind in ("hordecontain", "horsehordecontain", "aodhordecontain"):
                self._project_horde(leaf, block, label)
            elif kind == "fireweaponupdate":
                self._project_fire_weapons(leaf, block, label)
            elif kind == "attributemodifierauraupdate":
                if not self._project_aura(leaf, block, label):
                    unconverted.add(block.kind)
            elif kind in ("destroydie", "keepobjectdie"):
                if not self._project_death_policy(leaf, block):
                    unconverted.add(block.kind)
            elif kind == "lockweaponcreate":
                # Projected above with the default WeaponSet so the lock and
                # the slot it protects are validated as one atomic contract.
                pass
            elif kind == "experiencelevelcreate":
                # Projected above as authoritative creation-rank evidence.
                pass
            elif kind == "invisibilityupdate":
                if not self._project_invisibility(leaf, block):
                    unconverted.add(block.kind)
            elif kind == "stealthdetectorupdate":
                if not self._project_stealth_detector(leaf, block):
                    unconverted.add(block.kind)
            else:
                unconverted.add(block.kind)
        if unconverted:
            leaf["unconvertedBehaviors"] = sorted(unconverted, key=str.casefold)
        return leaf

    def _particle_family(self, identifier: str) -> str:
        """Classify a particle system this lane could not bind.

        Retail ships TWO particle families and this lane indexes only
        ``FXParticleSystem``.  Some authored ParticleSysBone systems
        (``RainOfFireProjectileSmoke``, ``InfantryDustTrails``) are defined
        only in the legacy ``data/ini/particlesystem.ini``; others
        (``BalrogSword``, ``GoblinKingTaint``) are in neither file and are
        simply absent from the corpus.  Both are recorded, but they are
        different gaps and only the first is fixable by widening the lookup —
        so the evidence has to say which.  Classification only: this never
        binds a definition.
        """

        legacy = self._documents.get("data/ini/particlesystem.ini")
        if legacy is None:
            # The spellbook document view does not carry the legacy family at
            # all, so this lane cannot tell "absent from retail" from "defined
            # in the family we never load". Say exactly that instead of
            # asserting an absence that was never checked.
            return "unknown-legacy-family-not-in-view"
        if self._legacy_particle_names is None:
            try:
                self._legacy_particle_names = {
                    definition.name.casefold()
                    for definition in parse_particle_definitions(legacy)
                }
            except ValueError:
                self._legacy_particle_names = set()
        if identifier.casefold() in self._legacy_particle_names:
            return "ParticleSystem"
        return "none"

    def _project_draw(self, lineage: Sequence[SageObject]) -> list[dict[str, object]]:
        """Record the authored Draw evidence of one effect-referenced Object.

        Spellbook leaves used to carry gameplay only, so every summoned unit,
        grove, tree, and sunbeam reached the runtime with no art binding at all
        and presented as synthetic kit geometry (or as nothing).  Retail
        authors that art in the object's Draw modules, and it comes in two
        distinct channels that must both be recorded:

        * ``Model`` in a (Default)ModelConditionState — real geometry, e.g.
          goodfactionsubobjects.ini ElvenWoodTree ``Model = PTElvnWood01`` and
          neutralunits.ini RohanOathbreaker ``Model = RUPsnt_1_SKN``.
        * ``ParticleSysBone`` — the ONLY visual some objects have.  Retail
          authors ``Model = None`` for CloudBreakSunbeam
          (goodfactionprops.ini) and for ElvenGrove (structures/elven/
          grove.ini); their entire appearance is the bone-attached particle
          system (``CloudBreakRays`` / ``TaintHCPing``).

        ``Model = None`` is recorded as the authored absence it is (``models``
        stays empty for that state) — retail authoring is never replaced by an
        invented stand-in.  Each referenced particle system is resolved through
        the same particle leaf family the FXList lane uses, so a converted
        definition and its textures ride the pack.
        """

        states: list[dict[str, object]] = []
        for block in _effective_top_blocks(lineage):
            if (block.header_key or "").casefold() != "draw":
                continue
            for state in block.blocks:
                kind = state.kind.casefold()
                if kind not in {
                    "defaultmodelconditionstate",
                    "modelconditionstate",
                }:
                    continue
                models: list[str] = []
                particles: list[dict[str, str]] = []
                unresolved_particles: list[dict[str, object]] = []
                for assignment in state.assignments:
                    key = assignment.key.casefold()
                    if key == "model":
                        token = _first(_tokens(assignment.value))
                        if token is not None and token.casefold() not in _NULL_TOKENS:
                            models.append(token)
                    elif key == "particlesysbone":
                        fields = _particle_sys_bone_fields(assignment.value)
                        if fields is None:
                            continue
                        bone, system = fields
                        if system.casefold() in _NULL_TOKENS:
                            continue
                        try:
                            self.particle_reference(
                                system, f"{block.kind} ParticleSysBone"
                            )
                        except SpellbookCompilerError as exc:
                            # Retail ships ParticleSysBone lines whose system
                            # name does not exist — neutralunits.ini:6016
                            # authors `SoWolf_Ambient_snowFollowBone:YES`, one
                            # missing space away from the real
                            # `SoWolf_Ambient_snow`.  SAGE looks that name up,
                            # misses, and draws nothing.  Record the authored
                            # reference verbatim with its source line (same
                            # source-null precedent as the census' missing
                            # audio samples) rather than either inventing a
                            # definition or costing the whole faction its
                            # spellbook over one retail typo.
                            unresolved_particles.append(
                                {
                                    "bone": bone,
                                    "authoredValue": assignment.value.strip(),
                                    "particleSystem": system,
                                    "authoredFamily": self._particle_family(system),
                                    "reason": str(exc),
                                    "sourceIni": assignment.source_virtual_path,
                                    "line": assignment.line,
                                }
                            )
                            continue
                        particles.append(
                            {
                                "bone": bone,
                                "particleSystem": self.particles[system.casefold()][
                                    "id"
                                ],
                            }
                        )
                if not models and not particles and not unresolved_particles:
                    continue
                row: dict[str, object] = {
                    "drawModule": block.kind,
                    "conditions": [
                        token
                        for token in state.model_condition_tokens
                        or state.header_tokens
                    ],
                }
                if models:
                    row["models"] = models
                if particles:
                    row["particleSysBones"] = particles
                if unresolved_particles:
                    row["unresolvedParticleSysBones"] = sorted(
                        unresolved_particles,
                        key=lambda item: (
                            str(item["sourceIni"]).casefold(),
                            int(item["line"]),  # type: ignore[arg-type]
                        ),
                    )
                states.append(row)
        states.sort(
            key=lambda row: (
                str(row["drawModule"]).casefold(),
                tuple(str(item).casefold() for item in row["conditions"]),
                tuple(str(item).casefold() for item in row.get("models", [])),
            )
        )
        return states

    def _block_numeric(
        self, block: SageBlock, field: str
    ) -> tuple[int | float | None, str | None]:
        values = block.values(field)
        if len(values) != 1:
            return None, None
        return self._resolve_numeric(values[0].strip()), values[0].strip()

    def _project_lifetime(self, leaf: dict[str, object], block: SageBlock) -> bool:
        minimum, _ = self._block_numeric(block, "MinLifetime")
        maximum, _ = self._block_numeric(block, "MaxLifetime")
        # LifetimeUpdate is atomic. Mixed resolved/unresolved bounds must stay
        # named as unconverted instead of emitting a misleading partial row.
        if minimum is None or maximum is None:
            return False
        row: dict[str, object] = {}
        if minimum is not None:
            row["minMs"] = minimum
        if maximum is not None:
            row["maxMs"] = maximum
        death_type = next(iter(block.values("DeathType")), None)
        if death_type is not None:
            row["deathType"] = death_type.strip()
        leaf["lifetime"] = row
        return True

    def _project_deletion(self, leaf: dict[str, object], block: SageBlock) -> None:
        minimum, _ = self._block_numeric(block, "MinLifetime")
        maximum, _ = self._block_numeric(block, "MaxLifetime")
        if minimum is None and maximum is None:
            return
        row: dict[str, object] = {}
        if minimum is not None:
            row["minMs"] = minimum
        if maximum is not None:
            row["maxMs"] = maximum
        leaf["deletion"] = row

    def _project_hatch(
        self, leaf: dict[str, object], block: SageBlock, label: str
    ) -> bool:
        ocl_values = block.values("OCL")
        token_rows = [_tokens(value) for value in ocl_values]
        if any(not tokens for tokens in token_rows):
            raise SpellbookCompilerError(
                f"{label} SlowDeathBehavior has an invalid OCL"
            )
        if len(token_rows) == 1:
            tokens = token_rows[0]
        else:
            midpoint_rows = [
                tokens
                for tokens in token_rows
                if tokens[0].casefold() == "midpoint"
            ]
            if len(midpoint_rows) != 1:
                return False
            tokens = midpoint_rows[0]
        if not tokens:
            return False
        hatch_ocl = tokens[-1]
        delay, _ = self._block_numeric(block, "DestructionDelay")
        row: dict[str, object] = {
            "trigger": tokens[0] if len(tokens) > 1 else "FINAL",
            "ocl": self.object_creation_list(hatch_ocl, f"{label} SlowDeathBehavior"),
        }
        if delay is not None:
            row["destructionDelayMs"] = delay
        leaf["hatch"] = row
        return True

    def _project_slow_death(
        self, leaf: dict[str, object], block: SageBlock
    ) -> bool:
        """Record one SlowDeathBehavior destruction-delay evidence row.

        Retail authors the FADED fade window as ``DestructionDelay`` on a
        ``DeathTypes = NONE +FADED`` module (pure retail examples:
        object/goodfaction/generic/tombombadil.ini:541-548 = 1000 ms,
        object/goodfaction/units/elven/gwaihir.ini:453-460 = 2500 ms,
        object/goodfaction/units/ents/entsinfantry.ini:387-394 = 10000 ms).
        The row keeps the authored milliseconds verbatim; a module with no
        authored delay is recorded as ``destructionDelayAuthored: False``
        rather than defaulted to 0.  Returns False (named unconverted) only
        when an authored delay cannot be resolved.
        """

        row: dict[str, object] = {
            "module": block.kind,
            "moduleTag": block.instance_tag or "",
        }
        death_type_values = list(block.values("DeathTypes"))
        if death_type_values:
            row["deathTypes"] = [
                token for value in death_type_values for token in _tokens(value)
            ]
        delay_values = block.values("DestructionDelay")
        if delay_values:
            delay, _ = self._block_numeric(block, "DestructionDelay")
            if delay is None:
                # Authored but unresolvable (macro outside the supported
                # grammar, or duplicate rows): keep the module named instead
                # of shipping a wrong or invented window.
                return False
            row["destructionDelayAuthored"] = True
            row["destructionDelayMs"] = delay
        else:
            row["destructionDelayAuthored"] = False
        row["sourceIni"] = block.source_virtual_path
        row["line"] = block.line
        rows = leaf.setdefault("slowDeaths", [])
        assert isinstance(rows, list)
        rows.append(row)
        return True

    # InvisibilityUpdate fields this projector converts. Everything else a
    # module authors is preserved by NAME in ``unconvertedFields`` -- evidence
    # is never silently dropped, and a consumer can gate on the list being
    # empty (the Enshrouding Mist broadcast dummy at
    # object/system/system.ini:2018-2028 authors exactly this subset).
    _INVISIBILITY_MODULE_FIELDS = frozenset(
        {"updateperiod", "broadcast", "broadcastrange",
         "broadcastobjectfilter", "startsactive"}
    )
    _INVISIBILITY_NUGGET_FIELDS = frozenset(
        {"invisibilitytype", "detectionrange"}
    )

    def _project_invisibility(
        self, leaf: dict[str, object], block: SageBlock
    ) -> bool:
        """Convert one InvisibilityUpdate camouflage/broadcast module.

        Emits nugget ``InvisibilityType``/``DetectionRange`` plus the module
        ``UpdatePeriod``/``Broadcast``/``BroadcastRange``/
        ``BroadcastObjectFilter``/``StartsActive`` contract, resolving
        gamedata defines (ELVEN_MIST_CAMOUFLAGE_DETECTION_RANGE = 100.0 at
        gamedata.ini:144, ENSHROUDING_MIST_EFFECT_RADIUS = 150 at
        gamedata.ini:22, ELVEN_MIST_OBJECT_FILTER at gamedata.ini:145).
        Atomic per module: a missing/ambiguous nugget or an authored numeric
        field that cannot resolve keeps the module named in
        ``unconvertedBehaviors``.
        """

        nuggets = [
            nested
            for nested in block.blocks
            if nested.kind.casefold() == "invisibilitynugget"
        ]
        if len(nuggets) != 1:
            return False
        nugget = nuggets[0]
        invisibility_type = next(iter(nugget.values("InvisibilityType")), None)
        if invisibility_type is None:
            return False
        row: dict[str, object] = {
            "invisibilityType": invisibility_type.strip(),
        }
        if nugget.values("DetectionRange"):
            detection, _ = self._block_numeric(nugget, "DetectionRange")
            if detection is None:
                return False
            row["detectionRange"] = detection
        for source_name, output_name in (
            ("UpdatePeriod", "updatePeriodMs"),
            ("BroadcastRange", "broadcastRange"),
        ):
            if block.values(source_name):
                value, _ = self._block_numeric(block, source_name)
                if value is None:
                    return False
                row[output_name] = value
        for source_name, output_name in (
            ("Broadcast", "broadcast"),
            ("StartsActive", "startsActive"),
        ):
            value = next(iter(block.values(source_name)), None)
            if value is not None:
                row[output_name] = value.strip()
        object_filter = next(
            iter(block.values("BroadcastObjectFilter")), None
        )
        if object_filter is not None:
            row["broadcastObjectFilter"] = self._text_define(
                object_filter.strip()
            )
        unconverted_fields = sorted(
            {
                assignment.key
                for assignment in block.assignments
                if assignment.key.casefold()
                not in self._INVISIBILITY_MODULE_FIELDS
            }
            | {
                assignment.key
                for assignment in nugget.assignments
                if assignment.key.casefold()
                not in self._INVISIBILITY_NUGGET_FIELDS
            },
            key=str.casefold,
        )
        if unconverted_fields:
            row["unconvertedFields"] = unconverted_fields
        row["sourceIni"] = block.source_virtual_path
        row["line"] = block.line
        rows = leaf.setdefault("invisibilityUpdates", [])
        assert isinstance(rows, list)
        rows.append(row)
        return True

    _STEALTH_DETECTOR_FIELDS = frozenset(
        {
            "detectionrate",
            "detectionrange",
            "startsactive",
            "initiallydisabled",
            "canceloneringeffect",
            "candetectwhilegarrisoned",
            "candetectwhilecontained",
            "requiredupgrade",
        }
    )

    def _project_stealth_detector(
        self, leaf: dict[str, object], block: SageBlock
    ) -> bool:
        """Convert one StealthDetectorUpdate module (DetectionRate et al)."""

        row: dict[str, object] = {}
        for source_name, output_name in (
            ("DetectionRate", "detectionRateMs"),
            ("DetectionRange", "detectionRange"),
        ):
            if block.values(source_name):
                value, _ = self._block_numeric(block, source_name)
                if value is None:
                    return False
                row[output_name] = value
        for source_name, output_name in (
            ("StartsActive", "startsActive"),
            ("InitiallyDisabled", "initiallyDisabled"),
            ("CancelOneRingEffect", "cancelOneRingEffect"),
            ("CanDetectWhileGarrisoned", "canDetectWhileGarrisoned"),
            ("CanDetectWhileContained", "canDetectWhileContained"),
            ("RequiredUpgrade", "requiredUpgrade"),
        ):
            value = next(iter(block.values(source_name)), None)
            if value is not None:
                row[output_name] = value.strip()
        unconverted_fields = sorted(
            {
                assignment.key
                for assignment in block.assignments
                if assignment.key.casefold() not in self._STEALTH_DETECTOR_FIELDS
            },
            key=str.casefold,
        )
        if unconverted_fields:
            row["unconvertedFields"] = unconverted_fields
        row["sourceIni"] = block.source_virtual_path
        row["line"] = block.line
        rows = leaf.setdefault("stealthDetection", [])
        assert isinstance(rows, list)
        rows.append(row)
        return True

    def _project_horde(
        self, leaf: dict[str, object], block: SageBlock, label: str
    ) -> None:
        row: dict[str, object] = {}
        payload_values = block.values("InitialPayload")
        if len(payload_values) == 1:
            tokens = _tokens(payload_values[0])
            if len(tokens) >= 2:
                self.object_reference(tokens[0], f"{label} InitialPayload")
                count = self._resolve_numeric(tokens[1])
                if count is None:
                    raise SpellbookCompilerError(
                        f"{label} InitialPayload has an unresolved count: {tokens[1]}"
                    )
                row["memberObject"] = self.objects[tokens[0].casefold()]["id"]
                row["memberCount"] = count
        slots, _ = self._block_numeric(block, "Slots")
        if slots is not None:
            row["slots"] = slots
        ranks: list[dict[str, object]] = []
        for value in block.values("RankInfo"):
            rank_match = re.search(r"RankNumber\s*:\s*(\d+)", value)
            positions = [
                [float(x), float(y)]
                for x, y in re.findall(
                    r"Position\s*:\s*X\s*:\s*(-?(?:[0-9]+(?:\.[0-9]*)?|\.[0-9]+))\s+Y\s*:\s*(-?(?:[0-9]+(?:\.[0-9]*)?|\.[0-9]+))",
                    value,
                )
            ]
            if rank_match is None or not positions:
                raise SpellbookCompilerError(f"{label} has a malformed RankInfo row")
            ranks.append({"rank": int(rank_match.group(1)), "positions": positions})
        if ranks:
            row["ranks"] = ranks
        if row:
            leaf["horde"] = row

    def _project_fire_weapons(
        self, leaf: dict[str, object], block: SageBlock, label: str
    ) -> None:
        nuggets: list[dict[str, object]] = []
        for nugget in block.blocks:
            if nugget.kind.casefold() != "fireweaponnugget":
                raise SpellbookCompilerError(
                    f"{label} FireWeaponUpdate has an unsupported {nugget.kind} section"
                )
            names = nugget.values("WeaponName")
            if len(names) != 1:
                raise SpellbookCompilerError(
                    f"{label} FireWeaponNugget must have exactly one WeaponName"
                )
            row: dict[str, object] = {
                "weapon": self.weapon(names[0].strip(), f"{label} FireWeaponNugget")
            }
            delay, _ = self._block_numeric(nugget, "FireDelay")
            if delay is not None:
                row["fireDelayMs"] = delay
            one_shot = next(iter(nugget.values("OneShot")), None)
            if one_shot is not None:
                row["oneShot"] = one_shot.strip()
            nuggets.append(row)
        if nuggets:
            leaf["fireWeapons"] = nuggets

    def _project_aura(
        self, leaf: dict[str, object], block: SageBlock, label: str
    ) -> bool:
        names = block.values("BonusName")
        if len(names) != 1:
            raise SpellbookCompilerError(
                f"{label} AttributeModifierAuraUpdate must have exactly one BonusName"
            )
        row: dict[str, object] = {
            "modifier": self.attribute_modifier(names[0].strip(), f"{label} BonusName")
        }
        refresh, _ = self._block_numeric(block, "RefreshDelay")
        if refresh is not None:
            row["refreshDelayMs"] = refresh
        aura_range, _ = self._block_numeric(block, "Range")
        if aura_range is not None:
            row["range"] = aura_range
        object_filter = next(iter(block.values("ObjectFilter")), None)
        if object_filter is not None:
            row["objectFilter"] = self._text_define(object_filter.strip())
        for field, output in (
            ("TargetEnemy", "targetEnemy"),
            ("TargetAllies", "targetAllies"),
        ):
            value = next(iter(block.values(field)), None)
            if value is not None:
                row[output] = value.strip()
        required = next(iter(block.values("RequiredConditions")), None)
        if required is not None:
            row["requiredConditions"] = required.strip()
        starts_active = next(iter(block.values("StartsActive")), None)
        triggered_by = list(block.values("TriggeredBy"))
        if starts_active is not None:
            row["startsActive"] = starts_active.strip()
        if triggered_by:
            row["triggeredBy"] = [
                token for value in triggered_by for token in _tokens(value)
            ]
        # A disabled aura with no authored activation edge cannot safely be
        # treated as always-on. Keep the module named in unconvertedBehaviors.
        if (
            starts_active is not None
            and starts_active.strip().casefold() == "no"
            and not row.get("triggeredBy")
        ):
            return False
        auras = leaf.setdefault("auras", [])
        assert isinstance(auras, list)
        auras.append(row)
        # Keep the historical singular shape only for genuinely single-aura
        # leaves. It is removed as soon as a second module is projected so no
        # consumer can silently discard the first one.
        if len(auras) == 1:
            leaf["aura"] = row
        else:
            leaf.pop("aura", None)
        return True

    @staticmethod
    def _project_death_policy(leaf: dict[str, object], block: SageBlock) -> bool:
        values = list(block.values("DeathTypes"))
        tokens = _tokens(values[-1]) if values else ["ALL"]
        if not tokens or tokens[0].upper() not in {"ALL", "NONE"}:
            return False
        row = {
            "deathTypes": tokens[0].upper(),
            "excludedDeathTypes": sorted(
                (token[1:].upper() for token in tokens[1:] if token.startswith("-")),
                key=str.casefold,
            ),
            "includedDeathTypes": sorted(
                (token[1:].upper() for token in tokens[1:] if token.startswith("+")),
                key=str.casefold,
            ),
        }
        key = "destroyDie" if block.kind.casefold() == "destroydie" else "keepObjectDie"
        rows = leaf.setdefault(key, [])
        assert isinstance(rows, list)
        rows.append(row)
        return True

    def _text_define(self, expression: str) -> str:
        return str(self._text_defines.get(expression.casefold(), expression))

    def _project_locomotor(
        self,
        leaf: dict[str, object],
        lineage: Sequence[SageObject],
        locomotor_name: str,
        label: str,
    ) -> None:
        # ONE canonical locomotor reader (locomotor_compiler).
        template = resolve_locomotor_template(
            compile_locomotor_templates(self._documents, self._constants),
            locomotor_name,
        )
        if template is None:
            raise SpellbookCompilerError(
                f"{label} references a missing Locomotor: {locomotor_name}"
            )
        self.used_locomotor = True
        row: dict[str, object] = {"id": locomotor_name}
        # Per-object Speed is authored on the Object's LocomotorSet block;
        # response fields live in the shared locomotor definition.
        speed = _resolved_set_field(lineage, "LocomotorSet", "Speed", self._constants)
        if speed is not None:
            row["speed"] = speed["value"]
        fields = template["fields"]
        assert isinstance(fields, Mapping)
        for key in ("acceleration", "braking"):
            field = fields.get(key)
            if isinstance(field, Mapping):
                row[key] = field["value"]
        turn_rate = turn_rate_degrees_per_second(template)
        if turn_rate is not None:
            row["turnRateDegreesPerSecond"] = turn_rate["value"]
        leaf["locomotor"] = row

    def particle_reference(self, identifier: str, label: str) -> None:
        key = identifier.casefold()
        if key in self.particles:
            return
        try:
            definition = select_particle_definition(
                self._particle_definitions, identifier
            )
        except ValueError as exc:
            raise SpellbookCompilerError(f"{label}: {exc}") from exc
        self.particles[key] = {
            "id": definition.name,
            "kind": definition.kind,
            "sourceSha256": definition.source.sha256,
        }

    def _fx_section(
        self, section: Mapping[str, object], label: str
    ) -> dict[str, object]:
        kind = str(section.get("kind", ""))
        fields: list[dict[str, str]] = []
        for assignment in section.get("assignments", []):
            if not isinstance(assignment, Mapping):
                raise SpellbookCompilerError(
                    f"{label} FXList section payload is invalid"
                )
            fields.append(
                {
                    "key": str(assignment.get("field", "")),
                    "value": str(assignment.get("value", "")),
                }
            )
        row: dict[str, object] = {"kind": kind, "fields": fields}
        folded = kind.casefold()
        if folded == _FX_PARTICLE_SECTION:
            names = _fx_field_values(section, "Name")
            if len(names) != 1:
                raise SpellbookCompilerError(
                    f"{label} ParticleSystem nugget must have exactly one Name"
                )
            identifier = _first((names[0],))
            if identifier is None:
                raise SpellbookCompilerError(
                    f"{label} ParticleSystem nugget has an invalid Name"
                )
            try:
                self.particle_reference(identifier, f"{label} ParticleSystem")
            except SpellbookCompilerError:
                # Retail FXLists can name presentation-only particles absent
                # from both shipped particle families (RotWK's
                # AODsummonLightShafts). Preserve that authored reference and
                # its checked family boundary instead of rejecting the whole
                # gameplay spellbook leaf.
                row["unresolvedParticleSystem"] = {
                    "id": identifier,
                    "definitionFamily": self._particle_family(identifier),
                    "reason": "authored FXList particle has no shipped definition",
                }
            else:
                row["particleSystemId"] = self.particles[identifier.casefold()]["id"]
        elif folded == _FX_SOUND_SECTION:
            names = _fx_field_values(section, "Name")
            if len(names) != 1:
                raise SpellbookCompilerError(
                    f"{label} Sound nugget must have exactly one Name"
                )
            identifier = _first((names[0],))
            if identifier is None:
                raise SpellbookCompilerError(
                    f"{label} Sound nugget has an invalid Name"
                )
            self.audio_ids.setdefault(identifier.casefold(), identifier)
            row["soundId"] = identifier
        nested = [
            self._fx_section(child, label)
            for child in section.get("sections", [])  # type: ignore[misc]
        ]
        if nested:
            row["nuggets"] = nested
        return row

    def fx_list(self, identifier: str, label: str) -> str:
        key = identifier.casefold()
        if key in self.fx_lists:
            return str(self.fx_lists[key]["id"])
        record = self._fx_lists.get(key)
        if record is None:
            raise SpellbookCompilerError(
                f"{label} references a missing FXList: {identifier}"
            )
        sections = record.get("sections")
        if not isinstance(sections, list):
            raise SpellbookCompilerError(f"FXList {identifier} payload is invalid")
        nuggets = [
            self._fx_section(section, f"{label} FXList {identifier}")
            for section in sections
        ]
        source = record.get("sourceSpan")
        if not isinstance(source, Mapping) or not isinstance(source.get("sha256"), str):
            raise SpellbookCompilerError(
                f"FXList {identifier} source evidence is invalid"
            )
        self.fx_lists[key] = {
            "id": str(record.get("fxListId", identifier)),
            "sourceSha256": _sha256(
                source.get("sha256"), f"FXList {identifier} sourceSha256"
            ),
            "nuggets": nuggets,
        }
        return str(self.fx_lists[key]["id"])

    def object_creation_list(self, identifier: str, label: str) -> str:
        key = identifier.casefold()
        if key in self.ocls:
            return str(self.ocls[key]["id"])
        block = self._ocls.get(key)
        if block is None:
            raise SpellbookCompilerError(
                f"{label} references a missing ObjectCreationList: {identifier}"
            )
        top_assignments = block["assignments"]
        if top_assignments:
            raise SpellbookCompilerError(
                f"ObjectCreationList {identifier} has unsupported top-level assignments"
            )
        sections = block["sections"]
        if not sections:
            raise SpellbookCompilerError(
                f"ObjectCreationList {identifier} has no CreateObject entries"
            )
        entries: list[dict[str, object]] = []
        for section in sections:  # type: ignore[assignment]
            kind = str(section.get("kind", ""))
            if kind.casefold() != "createobject":
                raise SpellbookCompilerError(
                    f"ObjectCreationList {identifier} has an unsupported {kind} section"
                )
            fields: list[dict[str, str]] = []
            object_ids: list[str] = []
            particle_ids: list[str] = []
            for field, value in section.get("assignments", []):  # type: ignore[misc]
                row: dict[str, object] = {"key": str(field), "value": str(value)}
                resolved = self._resolve_numeric(str(value))
                if resolved is not None:
                    row["resolved"] = resolved
                fields.append(row)  # type: ignore[arg-type]
                folded = str(field).casefold()
                if folded == "objectnames":
                    for token in _tokens(str(value)):
                        if token.casefold() in _NULL_TOKENS:
                            continue
                        self.object_reference(token, f"ObjectCreationList {identifier}")
                        object_ids.append(self.objects[token.casefold()]["id"])
                elif folded == "particlesystem":
                    token = _first((str(value),))
                    if token is None:
                        raise SpellbookCompilerError(
                            f"ObjectCreationList {identifier} has an invalid ParticleSystem"
                        )
                    self.particle_reference(
                        token, f"ObjectCreationList {identifier} ParticleSystem"
                    )
                    particle_ids.append(self.particles[token.casefold()]["id"])
            if not object_ids:
                raise SpellbookCompilerError(
                    f"ObjectCreationList {identifier} CreateObject has no ObjectNames"
                )
            entry: dict[str, object] = {"fields": fields, "objects": object_ids}
            if particle_ids:
                entry["particleSystems"] = particle_ids
            entries.append(entry)
        self.ocls[key] = {"id": str(block["id"]), "createObjects": entries}
        return str(self.ocls[key]["id"])

    def attribute_modifier(self, identifier: str, label: str) -> str:
        key = identifier.casefold()
        if key in self.modifiers:
            return str(self.modifiers[key]["id"])
        block = self._modifiers.get(key)
        if block is None:
            raise SpellbookCompilerError(
                f"{label} references a missing ModifierList: {identifier}"
            )
        fields: list[dict[str, str]] = []
        fx_ids: list[str] = []
        for field, value in block.assignments:
            fields.append({"key": field, "value": value})
            folded = field.casefold()
            if folded in _MODIFIER_FX_FIELDS:
                token = _first((value,))
                if token is None:
                    continue
                fx_ids.append(self.fx_list(token, f"ModifierList {identifier} {field}"))
        row: dict[str, object] = {
            "id": block.name,
            "fields": fields,
            "definitionSha256": _gameplay_digest(
                IniBlock("ModifierList", block.name, None, block.assignments)
            ),
        }
        if fx_ids:
            row["fxLists"] = fx_ids
        self.modifiers[key] = row
        return block.name

    def upgrade(self, identifier: str, label: str) -> str:
        key = identifier.casefold()
        if key in self.upgrades:
            return str(self.upgrades[key]["id"])
        block = self._upgrades.get(key)
        if block is None:
            raise SpellbookCompilerError(
                f"{label} references a missing Upgrade: {identifier}"
            )
        digest = _gameplay_digest(block)
        census_row = self._census_upgrades.get(key)
        if census_row is not None and str(census_row["definitionSha256"]) != digest:
            raise SpellbookCompilerError(
                f"Upgrade {identifier} no longer matches its census definition digest"
            )
        upgrade_type = _one_value(block, "Type", f"Upgrade {identifier}")
        row: dict[str, object] = {"id": block.name, "definitionSha256": digest}
        if upgrade_type is not None:
            row["type"] = upgrade_type.strip()
        self.upgrades[key] = row
        return block.name

    def weapon(self, identifier: str, label: str) -> str:
        key = identifier.casefold()
        if key in self.weapons:
            return str(self.weapons[key]["id"])
        block = self._weapons.get(key)
        if block is None:
            raise SpellbookCompilerError(
                f"{label} references a missing Weapon: {identifier}"
            )
        fields: list[dict[str, str]] = []
        fire_fx: list[str] = []
        projectile: str | None = None
        for field, value in block["assignments"]:  # type: ignore[misc]
            row: dict[str, object] = {"key": str(field), "value": str(value)}
            min_max = re.fullmatch(
                r"\s*Min\s*:\s*(\S+)\s+Max\s*:\s*(\S+)\s*", str(value)
            )
            if min_max is not None:
                minimum = self._resolve_numeric(min_max.group(1))
                maximum = self._resolve_numeric(min_max.group(2))
                if minimum is not None and maximum is not None:
                    row["resolvedMin"] = minimum
                    row["resolvedMax"] = maximum
            else:
                resolved = self._resolve_numeric(str(value))
                if resolved is not None:
                    row["resolved"] = resolved
            fields.append(row)  # type: ignore[arg-type]
            folded = str(field).casefold()
            if folded == "firefx":
                token = _first((str(value),))
                if token is None:
                    raise SpellbookCompilerError(
                        f"Weapon {identifier} has an invalid FireFX"
                    )
                # FireFX ids are validated but not traversed: weapon fire
                # presentation belongs to the FX lane, and deep traversal
                # drags unpackaged audio chains into this lane's packaging.
                if token.casefold() not in self._fx_lists:
                    raise SpellbookCompilerError(
                        f"Weapon {identifier} references a missing FXList: {token}"
                    )
                fire_fx.append(token)
            elif folded == "projectiletemplatename":
                token = _first((str(value),))
                if token is None:
                    raise SpellbookCompilerError(
                        f"Weapon {identifier} has an invalid ProjectileTemplateName"
                    )
                if projectile is not None and projectile.casefold() != token.casefold():
                    raise SpellbookCompilerError(
                        f"Weapon {identifier} has ambiguous ProjectileTemplateName"
                    )
                self.object_reference(
                    token, f"Weapon {identifier} ProjectileTemplateName"
                )
                projectile = self.objects[token.casefold()]["id"]
            elif folded.endswith("ocl") or (
                folded.endswith("template") and folded != "projectiletemplatename"
            ):
                raise SpellbookCompilerError(
                    f"Weapon {identifier} has an unsupported effect leaf field: {field}"
                )
        nuggets = [
            self._weapon_nugget(section, f"Weapon {identifier}")
            for section in block["sections"]  # type: ignore[misc]
        ]
        row: dict[str, object] = {"id": str(block["id"]), "fields": fields}
        radius_affects = [
            str(value).strip()
            for field, value in block["assignments"]  # type: ignore[misc]
            if str(field).casefold() == "radiusdamageaffects"
        ]
        if len(radius_affects) == 1:
            row["radiusDamageAffects"] = radius_affects[0]
        damage_nuggets: list[dict[str, object]] = []
        for section in block["sections"]:  # type: ignore[misc]
            if str(section.get("kind", "")).casefold() != "damagenugget":
                continue
            entry: dict[str, object] = {}
            for field, value in section.get("assignments", []):  # type: ignore[misc]
                folded = str(field).casefold()
                if folded in (
                    "damage",
                    "radius",
                    "delaytime",
                    "damagemaxheightaboveterrain",
                ):
                    resolved = self._resolve_numeric(str(value))
                    if resolved is None:
                        raise SpellbookCompilerError(
                            f"Weapon {identifier} DamageNugget has an unresolved {field}: {value}"
                        )
                    entry[folded] = resolved
                elif folded in ("damagetype", "deathtype", "damagescalar", "damagefx"):
                    entry[folded] = str(value).strip()
            damage_nuggets.append(entry)
        if damage_nuggets:
            row["damageNuggets"] = damage_nuggets
        if fire_fx:
            row["fireFx"] = fire_fx
        if projectile is not None:
            row["projectileTemplateId"] = projectile
        if nuggets:
            row["nuggets"] = nuggets
        self.weapons[key] = row
        return str(row["id"])

    def _weapon_nugget(
        self, section: Mapping[str, object], label: str
    ) -> dict[str, object]:
        fields: list[dict[str, str]] = []
        row: dict[str, object] = {
            "kind": str(section.get("kind", "")),
            "fields": fields,
        }
        fire_fx: list[str] = []
        for field, value in section.get("assignments", []):  # type: ignore[misc]
            fields.append({"key": str(field), "value": str(value)})
            folded = str(field).casefold()
            if folded == "firefx":
                token = _first((str(value),))
                if token is None:
                    raise SpellbookCompilerError(
                        f"{label} has an invalid nugget FireFX"
                    )
                fire_fx.append(token)
            elif folded == "warheadtemplatename":
                token = _first((str(value),))
                if token is None:
                    raise SpellbookCompilerError(
                        f"{label} has an invalid WarheadTemplateName"
                    )
                # Bow/lob weapons carry their damage on the warhead weapon.
                row["warheadId"] = self.weapon(token, f"{label} WarheadTemplateName")
            elif folded == "projectiletemplatename":
                token = _first((str(value),))
                if token is None:
                    raise SpellbookCompilerError(
                        f"{label} has an invalid ProjectileTemplateName"
                    )
                self.object_reference(token, f"{label} ProjectileTemplateName")
                row["projectileTemplateId"] = self.objects[token.casefold()]["id"]
            elif folded.endswith("ocl") or folded.endswith("template"):
                raise SpellbookCompilerError(
                    f"{label} has an unsupported nugget effect leaf field: {field}"
                )
        if fire_fx:
            row["fireFx"] = fire_fx
        nested = [
            self._weapon_nugget(child, label)
            for child in section.get("sections", [])  # type: ignore[misc]
        ]
        if nested:
            row["nuggets"] = nested
        return row


def _effect_modules(
    lineage: Sequence[SageObject],
) -> dict[str, SageBlock]:
    result: dict[str, SageBlock] = {}
    for block in _effective_top_blocks(lineage):
        if (block.header_key or "").casefold() != "behavior":
            continue
        templates = [
            token
            for value in block.values("SpecialPowerTemplate")
            for token in _tokens(value)
        ]
        if not templates:
            continue
        if len(templates) != 1:
            raise SpellbookCompilerError(
                f"spell-power module {block.kind} has ambiguous SpecialPowerTemplate"
            )
        key = templates[0].casefold()
        previous = result.get(key)
        if previous is not None:
            identity = (
                block.kind.casefold(),
                tuple(
                    (assignment.key.casefold(), assignment.value.strip())
                    for assignment in block.assignments
                ),
            )
            previous_identity = (
                previous.kind.casefold(),
                tuple(
                    (assignment.key.casefold(), assignment.value.strip())
                    for assignment in previous.assignments
                ),
            )
            if identity == previous_identity:
                # Retail 1.06 authors a byte-identical duplicate binding for
                # one evil spell power (RainOfFire/RainOfFire02).  It is one
                # effective module, not an ambiguity; conflicting duplicates
                # still fail closed below.
                continue
            raise SpellbookCompilerError(
                f"spell-power {templates[0]} is bound by multiple modules"
            )
        result[key] = block
        if len(result) > _MAX_EFFECT_MODULES:
            raise SpellbookCompilerError("spell-power module count exceeds limit")
    return result


def _command_points_upgrade(
    lineage: Sequence[SageObject],
) -> dict[str, object] | None:
    """Compile the exact retail spellbook ``CommandPointsUpgrade`` shape."""

    modules = [
        block
        for block in _effective_top_blocks(lineage)
        if (block.header_key or "").casefold() == "behavior"
        and block.kind.casefold() == "commandpointsupgrade"
    ]
    if not modules:
        return None
    if len(modules) != 1:
        raise SpellbookCompilerError(
            "spell book has multiple effective CommandPointsUpgrade modules"
        )
    block = modules[0]
    fields = {}
    for row in block.assignments:
        folded = row.key.casefold()
        if (
            folded not in {"triggeredby", "commandpoints", "requiredobject"}
            or folded in fields
        ):
            raise SpellbookCompilerError(
                "CommandPointsUpgrade must author exactly TriggeredBy, "
                "CommandPoints, and RequiredObject"
            )
        fields[folded] = row
    if set(fields) != {"triggeredby", "commandpoints", "requiredobject"}:
        raise SpellbookCompilerError(
            "CommandPointsUpgrade must author exactly TriggeredBy, "
            "CommandPoints, and RequiredObject"
        )
    trigger_tokens = _tokens(fields["triggeredby"].value)
    required_tokens = _tokens(fields["requiredobject"].value)
    points_token = fields["commandpoints"].value.strip()
    if (
        len(trigger_tokens) != 1
        or trigger_tokens[0].casefold()
        != "upgrade_marketplaceupgradegrandharvest"
    ):
        raise SpellbookCompilerError(
            "CommandPointsUpgrade TriggeredBy is outside the retail corpus"
        )
    if (
        re.fullmatch(r"[1-9][0-9]*", points_token) is None
        or int(points_token) != 100
    ):
        raise SpellbookCompilerError(
            "CommandPointsUpgrade CommandPoints is outside the retail corpus"
        )
    if [token.casefold() for token in required_tokens] != [
        "none",
        "+gondormarketplace",
    ]:
        raise SpellbookCompilerError(
            "CommandPointsUpgrade RequiredObject is outside the retail corpus"
        )
    return {
        "triggeredBy": trigger_tokens[0],
        "commandPoints": int(points_token),
        "requiredObject": " ".join(required_tokens),
        "module": "CommandPointsUpgrade",
        "sourceIni": block.source_virtual_path,
        "line": block.line,
    }


def _module_field_rows(
    block: SageBlock,
    constants: Mapping[str, int | float],
    text_defines: Mapping[str, str],
) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for assignment in block.assignments:
        row: dict[str, object] = {
            "key": assignment.key,
            "value": assignment.value.strip(),
        }
        resolved = _resolved_multiplicative_expression(
            assignment.value.strip(), constants
        )
        if resolved is not None:
            row["resolved"] = resolved
        resolved_text = text_defines.get(assignment.value.strip().casefold())
        if resolved_text is not None:
            row["resolvedText"] = resolved_text
        rows.append(row)
    return rows


def _module_leaves(
    block: SageBlock, resolver: _LeafResolver, label: str
) -> dict[str, object]:
    ocls: list[str] = []
    fx_lists: list[str] = []
    modifiers: list[str] = []
    upgrades: list[str] = []
    objects: list[dict[str, str]] = []
    weapons: list[str] = []
    for assignment in block.assignments:
        folded = assignment.key.casefold()
        if folded == "specialpowertemplate":
            continue
        token = _first((assignment.value,))
        if folded in _MODULE_OCL_FIELDS:
            if token is None:
                raise SpellbookCompilerError(f"{label} has an invalid {assignment.key}")
            ocls.append(
                resolver.object_creation_list(token, f"{label} {assignment.key}")
            )
        elif folded in _MODULE_FX_FIELDS:
            if token is None:
                raise SpellbookCompilerError(f"{label} has an invalid {assignment.key}")
            fx_lists.append(resolver.fx_list(token, f"{label} {assignment.key}"))
        elif folded in _MODULE_MODIFIER_FIELDS:
            if token is None:
                raise SpellbookCompilerError(f"{label} has an invalid {assignment.key}")
            modifiers.append(
                resolver.attribute_modifier(token, f"{label} {assignment.key}")
            )
        elif folded in _MODULE_UPGRADE_FIELDS:
            if token is None:
                raise SpellbookCompilerError(f"{label} has an invalid {assignment.key}")
            upgrades.append(resolver.upgrade(token, f"{label} {assignment.key}"))
        elif folded in _MODULE_OBJECT_FIELDS:
            if token is None:
                raise SpellbookCompilerError(f"{label} has an invalid {assignment.key}")
            resolver.object_reference(token, f"{label} {assignment.key}")
            objects.append(
                {
                    "field": assignment.key,
                    "id": resolver.objects[token.casefold()]["id"],
                }
            )
        elif folded in _MODULE_WEAPON_FIELDS:
            if token is None:
                raise SpellbookCompilerError(f"{label} has an invalid {assignment.key}")
            weapons.append(resolver.weapon(token, f"{label} {assignment.key}"))
        elif folded.endswith("ocl") or folded.endswith("template"):
            raise SpellbookCompilerError(
                f"{label} has an unsupported effect leaf field: {assignment.key}"
            )
        elif folded.endswith("fx") and len(folded) > 2:
            raise SpellbookCompilerError(
                f"{label} has an unsupported effect leaf field: {assignment.key}"
            )
    references: dict[str, object] = {}
    if ocls:
        references["objectCreationLists"] = ocls
    if fx_lists:
        references["fxLists"] = fx_lists
    if modifiers:
        references["attributeModifiers"] = modifiers
    if upgrades:
        references["upgrades"] = upgrades
    if objects:
        references["objects"] = objects
    if weapons:
        references["weapons"] = weapons
    return references


def compile_spellbook_descriptor(
    faction_graph: Mapping[str, object],
    documents: Mapping[str, bytes],
    *,
    resolved_images: Mapping[str, Mapping[str, object]] | None = None,
    resolved_audio: Mapping[str, Sequence[str]] | None = None,
    resolved_strings: Mapping[str, str] | None = None,
    prepared: PlayableUnitCompilerInputs | None = None,
) -> dict[str, object]:
    """Compile one source-backed faction spellbook descriptor or fail closed."""

    if prepared is None:
        prepared = prepare_playable_unit_compiler(documents)
    elif prepared.documents is not documents:
        raise SpellbookCompilerError(
            "prepared compiler inputs belong to a different document mapping"
        )
    (
        template_id,
        faction,
        graph_identity,
        spellbook_id,
        store_set_id,
        intrinsic_sciences,
    ) = _graph_context(faction_graph)
    _player_template_check(prepared, template_id, spellbook_id, store_set_id)
    census_sciences = _definition_rows(faction_graph, "sciences")
    census_powers = _definition_rows(faction_graph, "specialPowers")
    census_upgrades = _definition_rows(faction_graph, "upgrades")
    expected_sciences = _dependency_ids(faction_graph, "spellbookSciences")
    expected_powers = _dependency_ids(faction_graph, "spellbookSpecialPowers")

    constants = _merged_defines(documents, prepared)
    science_blocks = _unique_blocks(
        _required_document(documents, SCIENCE_PATH), "Science", SCIENCE_PATH
    )
    power_blocks = _unique_blocks(
        _required_document(documents, SPECIAL_POWER_PATH),
        "SpecialPower",
        SPECIAL_POWER_PATH,
    )

    spellbook = prepared.objects.get(spellbook_id.casefold())
    if spellbook is None:
        raise SpellbookCompilerError(f"effective Object is missing: {spellbook_id}")
    lineage = _ancestry(prepared.objects, spellbook)
    kinds = _kind_of(lineage)
    if _SPELL_BOOK_KIND not in kinds:
        raise SpellbookCompilerError(
            f"Object {spellbook_id} has no {_SPELL_BOOK_KIND} KindOf capability"
        )
    command_points_upgrade = _command_points_upgrade(lineage)
    command_values = [
        value
        for row in _effective_values(lineage, "CommandSet")
        if (value := _first((row.value,))) is not None
    ]
    if len(command_values) != 1:
        raise SpellbookCompilerError(
            f"Object {spellbook_id} has no single effective CommandSet"
        )
    command_set_id = command_values[0]
    command_set = prepared.command_sets.get(command_set_id.casefold())
    if command_set is None:
        raise SpellbookCompilerError(
            f"effective CommandSet is missing: {command_set_id}"
        )
    store_set = prepared.command_sets.get(store_set_id.casefold())
    if store_set is None:
        raise SpellbookCompilerError(f"effective CommandSet is missing: {store_set_id}")

    resolver = _LeafResolver(documents, prepared, constants, census_upgrades)

    science_rows: list[dict[str, object]] = []
    tree_science_ids: dict[str, str] = {}
    referenced_science_ids: dict[str, str] = {}

    def _science_row(
        block: IniBlock, purchase: dict[str, object] | None
    ) -> dict[str, object]:
        digest = _cross_check_definition(block, census_sciences, "Science")
        groups = _prerequisite_groups(block)
        flat = sorted(
            {token for group in groups for token in group},
            key=lambda item: (item.casefold(), item),
        )
        label = f"Science {block.name}"
        grantable = _one_value(block, "IsGrantable", label)
        grantable_tokens = _tokens(grantable or "")
        if (
            not grantable_tokens
            or grantable_tokens[0].casefold() not in {"yes", "no"}
            or any(
                not token.casefold().startswith("science_")
                for token in grantable_tokens[1:]
            )
        ):
            raise SpellbookCompilerError(f"{label} has an invalid IsGrantable")
        science_source = _document(documents, SCIENCE_PATH)
        assert science_source is not None
        point_cost = _required_scalar(
            block, "SciencePurchasePointCost", constants, label
        )
        row: dict[str, object] = {
            "id": block.name,
            "definitionSha256": digest,
            "isGrantable": grantable_tokens[0].casefold() == "yes",
            "pointCost": point_cost,
            "prerequisiteGroups": [list(group) for group in groups],
            "prerequisites": flat,
        }
        if len(grantable_tokens) > 1:
            row["isGrantableQualifierSciences"] = list(grantable_tokens[1:])
        mp_cost = _optional_scalar(
            block, "SciencePurchasePointCostMP", constants, label
        )
        if mp_cost is None:
            if purchase is not None:
                raise SpellbookCompilerError(
                    f"{label} is purchasable but has no SciencePurchasePointCostMP"
                )
        else:
            row["pointCostMP"] = mp_cost
        if purchase is not None:
            row["purchase"] = purchase
        contracts = {
            "IsGrantable": _field_contract(
                science_source, block, "IsGrantable", row["isGrantable"], SCIENCE_PATH
            ),
            "PrerequisiteSciences": _field_contract(
                science_source, block, "PrerequisiteSciences", row["prerequisiteGroups"], SCIENCE_PATH
            ),
            "SciencePurchasePointCost": _field_contract(
                science_source, block, "SciencePurchasePointCost", point_cost["value"], SCIENCE_PATH
            ),
            "SciencePurchasePointCostMP": _field_contract(
                science_source, block, "SciencePurchasePointCostMP", None if mp_cost is None else mp_cost["value"], SCIENCE_PATH
            ),
        }
        row["fieldContracts"] = {
            key: value for key, value in contracts.items() if value is not None
        }
        return row

    for slot, command_id in _command_slots(store_set):
        button = _button(prepared, command_id)
        science_id = _unique_button_target(
            button, "Science", _PURCHASE_COMMAND, "spell store"
        )
        key = science_id.casefold()
        if key in tree_science_ids:
            raise SpellbookCompilerError(
                f"spell store sells {science_id} from multiple slots"
            )
        block = science_blocks.get(key)
        if block is None:
            raise SpellbookCompilerError(f"effective Science is missing: {science_id}")
        purchase = {"slot": slot, **_button_leaf_fields(button)}
        science_rows.append(_science_row(block, purchase))
        tree_science_ids[key] = block.name

    pending = [
        token
        for row in science_rows
        for group in row["prerequisiteGroups"]
        for token in group
    ]
    while pending:
        token = pending.pop()
        key = token.casefold()
        if key in tree_science_ids or key in referenced_science_ids:
            continue
        block = science_blocks.get(key)
        if block is None:
            raise SpellbookCompilerError(f"effective Science is missing: {token}")
        row = _science_row(block, None)
        science_rows.append(row)
        referenced_science_ids[key] = block.name
        pending.extend(token for group in row["prerequisiteGroups"] for token in group)

    layered_authority = (
        faction_graph.get("spellbookDefinitionAuthority")
        == "layered-effective-assets"
    )
    expected_science_keys = {item.casefold() for item in expected_sciences}
    actual_science_keys = set(tree_science_ids)
    if not layered_authority and expected_science_keys != actual_science_keys:
        raise SpellbookCompilerError(
            "spell store science set disagrees with census spellbookSciences: "
            f"missing={sorted(expected_science_keys - actual_science_keys)} "
            f"store_only={sorted(actual_science_keys - expected_science_keys)}"
        )

    power_rows: list[dict[str, object]] = []
    tree_power_ids: dict[str, str] = {}
    effect_modules = _effect_modules(lineage)
    for slot, command_id in _command_slots(command_set):
        button = _button(prepared, command_id)
        power_id = _unique_button_target(
            button, "SpecialPower", _CAST_COMMAND, "spell book"
        )
        key = power_id.casefold()
        if key in tree_power_ids:
            raise SpellbookCompilerError(
                f"spell book casts {power_id} from multiple slots"
            )
        block = power_blocks.get(key)
        if block is None:
            raise SpellbookCompilerError(
                f"effective SpecialPower is missing: {power_id}"
            )
        label = f"SpecialPower {power_id}"
        digest = _cross_check_definition(block, census_powers, "SpecialPower")
        enum = _one_value(block, "Enum", label)
        if enum is None:
            raise SpellbookCompilerError(f"{label} has no authored Enum")
        required = sorted(
            {
                token
                for value in block.values("RequiredSciences")
                for token in _tokens(value)
                if token.startswith("SCIENCE_")
            },
            key=lambda item: (item.casefold(), item),
        )
        for science_id in required:
            science_key = science_id.casefold()
            if science_key in tree_science_ids or science_key in referenced_science_ids:
                continue
            science_block = science_blocks.get(science_key)
            if science_block is None:
                raise SpellbookCompilerError(
                    f"effective Science is missing: {science_id}"
                )
            science_rows.append(_science_row(science_block, None))
            referenced_science_ids[science_key] = science_block.name
        flags = sorted(
            {
                token
                for value in block.values("Flags")
                for token in _tokens(value)
                if token.casefold() not in _NULL_TOKENS
            },
            key=str.casefold,
        )
        sound = _first(block.values("InitiateAtLocationSound"))
        if sound is not None:
            resolver.audio_ids.setdefault(sound.casefold(), sound)
        row: dict[str, object] = {
            "id": block.name,
            "definitionSha256": digest,
            "enum": enum.strip(),
            "reloadTimeMs": _required_scalar(block, "ReloadTime", constants, label),
            "requiredSciences": required,
            "cast": {"slot": slot, **_button_leaf_fields(button)},
        }
        special_source = _document(documents, SPECIAL_POWER_PATH)
        assert special_source is not None
        object_filter = _one_value(block, "ObjectFilter", label)
        forbidden_filter = _one_value(block, "ForbiddenObjectFilter", label)
        forbidden_range = _optional_scalar(
            block, "ForbiddenObjectRange", constants, label
        )
        if object_filter is not None:
            row["objectFilter"] = list(_tokens(resolver._text_define(object_filter.strip())))
        if forbidden_filter is not None:
            row["forbiddenObjectFilter"] = list(_tokens(resolver._text_define(forbidden_filter.strip())))
        if forbidden_range is not None:
            row["forbiddenObjectRange"] = forbidden_range
        special_contracts = {
            "ReloadTime": _field_contract(special_source, block, "ReloadTime", row["reloadTimeMs"]["value"], SPECIAL_POWER_PATH),
            "RequiredSciences": _field_contract(special_source, block, "RequiredSciences", required, SPECIAL_POWER_PATH),
            "ObjectFilter": _field_contract(special_source, block, "ObjectFilter", row.get("objectFilter", []), SPECIAL_POWER_PATH),
            "ForbiddenObjectFilter": _field_contract(special_source, block, "ForbiddenObjectFilter", row.get("forbiddenObjectFilter", []), SPECIAL_POWER_PATH),
            "ForbiddenObjectRange": _field_contract(special_source, block, "ForbiddenObjectRange", None if forbidden_range is None else forbidden_range["value"], SPECIAL_POWER_PATH),
        }
        row["fieldContracts"] = {
            key: value for key, value in special_contracts.items() if value is not None
        }
        if flags:
            row["flags"] = flags
        if sound is not None:
            row["initiateSoundId"] = sound
        radius = _optional_scalar(block, "RadiusCursorRadius", constants, label)
        if radius is not None:
            row["radiusCursorRadius"] = radius
        duration = _optional_scalar(block, "ViewObjectDuration", constants, label)
        if duration is not None:
            row["viewObjectDurationMs"] = duration
        view_range = _optional_scalar(block, "ViewObjectRange", constants, label)
        if view_range is not None:
            row["viewObjectRange"] = view_range
        module = effect_modules.get(key)
        if module is None:
            raise SpellbookCompilerError(
                f"{label} has no spell-power module on {spellbook_id}"
            )
        effect: dict[str, object] = {
            "module": module.kind,
            "moduleTag": module.instance_tag or "",
            "sourceIni": module.source_virtual_path,
            "line": module.line,
            "fields": _module_field_rows(module, constants, resolver._text_defines),
            "references": _module_leaves(module, resolver, label),
        }
        row["effect"] = effect
        power_rows.append(row)
        tree_power_ids[key] = block.name

    expected_power_keys = {item.casefold() for item in expected_powers}
    actual_power_keys = set(tree_power_ids)
    if not layered_authority and expected_power_keys != actual_power_keys:
        raise SpellbookCompilerError(
            "spell book power set disagrees with census spellbookSpecialPowers"
        )

    image_ids = sorted(
        {
            token
            for row in (*science_rows, *power_rows)
            for binding in (row.get("purchase"), row.get("cast"))
            if isinstance(binding, Mapping)
            for token in binding.get("iconIds", [])
        },
        key=lambda item: (item.casefold(), item),
    )
    text_ids = sorted(
        {
            token
            for row in (*science_rows, *power_rows)
            for binding in (row.get("purchase"), row.get("cast"))
            if isinstance(binding, Mapping)
            for token in binding.get("textIds", [])
        },
        key=lambda item: (item.casefold(), item),
    )
    audio_ids = sorted(
        resolver.audio_ids.values(), key=lambda item: (item.casefold(), item)
    )

    used_paths = [
        PLAYER_TEMPLATE_PATH,
        "data/ini/commandset.ini",
        "data/ini/commandbutton.ini",
        "data/ini/gamedata.ini",
        SCIENCE_PATH,
        SPECIAL_POWER_PATH,
        OBJECT_CREATION_LIST_PATH,
        FX_LIST_PATH,
        ATTRIBUTE_MODIFIER_PATH,
        WEAPON_PATH,
        UPGRADE_PATH,
        FX_PARTICLE_PATH,
    ]
    if any("experience" in leaf for leaf in resolver.objects.values()):
        used_paths.append(EXPERIENCE_LEVELS_PATH)
    if resolver.used_locomotor:
        used_paths.append(LOCOMOTOR_PATH)
    rank_source = _document(documents, RANK_PATH)
    rank_science_grants = (
        compile_rank_science_grants(documents) if rank_source is not None else []
    )
    if rank_source is not None:
        used_paths.append(RANK_PATH)
    used_paths.extend(
        sorted(
            {item.source_virtual_path for item in lineage},
            key=lambda item: (item.casefold(), item),
        )
    )
    source_documents = []
    for path in sorted(set(used_paths), key=lambda item: (item.casefold(), item)):
        payload = next(
            (
                payload
                for candidate, payload in documents.items()
                if candidate.replace("\\", "/").casefold() == path.casefold()
            ),
            None,
        )
        if payload is None:
            raise SpellbookCompilerError(
                f"spellbook source document is missing: {path}"
            )
        source_documents.append(
            {"virtualPath": path, "sha256": hashlib.sha256(payload).hexdigest()}
        )

    descriptor: dict[str, object] = {
        "schema": SCHEMA,
        "schemaVersion": SCHEMA_VERSION,
        "target": {"playerTemplate": template_id, "faction": faction},
        "inputs": {"factionGraphInputSetSha256": graph_identity},
        "spellBook": {
            "objectId": spellbook.name,
            "kindOf": list(kinds),
            "commandSetId": command_set.name,
            "spellStoreCommandSetId": store_set.name,
            "intrinsicSciences": list(intrinsic_sciences),
            **(
                {"commandPointsUpgrade": command_points_upgrade}
                if command_points_upgrade is not None
                else {}
            ),
        },
        "sciences": science_rows,
        "powers": power_rows,
        **(
            {"rankScienceGrants": rank_science_grants}
            if rank_science_grants
            else {}
        ),
        "leaves": {
            "objectCreationLists": [
                resolver.ocls[key] for key in sorted(resolver.ocls)
            ],
            "fxLists": [resolver.fx_lists[key] for key in sorted(resolver.fx_lists)],
            "weapons": [resolver.weapons[key] for key in sorted(resolver.weapons)],
            "attributeModifiers": [
                resolver.modifiers[key] for key in sorted(resolver.modifiers)
            ],
            "upgrades": [resolver.upgrades[key] for key in sorted(resolver.upgrades)],
            "objects": [resolver.objects[key] for key in sorted(resolver.objects)],
            "particles": [
                resolver.particles[key] for key in sorted(resolver.particles)
            ],
        },
        "requirements": {
            "mappedImages": image_ids,
            "audio": audio_ids,
            "strings": text_ids,
        },
        "presentation": {
            "resolvedImages": {
                key: dict(value)
                for key, value in sorted(
                    (resolved_images or {}).items(), key=lambda item: item[0].casefold()
                )
            },
            "resolvedStrings": {
                key: value
                for key, value in sorted(
                    (resolved_strings or {}).items(),
                    key=lambda item: item[0].casefold(),
                )
            },
            "sourceNullStringIds": sorted(
                {
                    str(value).strip()
                    for value in faction_graph.get("layeredSourceNullTextIds", [])
                    if isinstance(value, str)
                    and str(value).strip() in text_ids
                },
                key=str.casefold,
            )
            if faction_graph.get("layeredDocumentAuthority")
            == "layered-effective-assets"
            else [],
            "resolvedAudio": {
                key: list(value)
                for key, value in sorted(
                    (resolved_audio or {}).items(), key=lambda item: item[0].casefold()
                )
            },
        },
        "sourceDocuments": source_documents,
        "limitations": [
            "Effect payloads cover spell-power modules and their direct leaves "
            "(ObjectCreationList, Weapon, FXList/particles, attribute "
            "modifiers, upgrades, audio, button art).",
            "Objects created by an ObjectCreationList carry a bounded "
            "effect-family projection (health, lifetimes, hatch OCLs, horde "
            "payloads, weapon sets, fire-weapon nuggets, attribute-modifier "
            "auras, locomotors) with full Object inheritance applied; "
            "presentation modules (Draw, client audio) and deeper module "
            "templates (e.g. FloodUpdate FloodMember chains) stay out of "
            "scope and any Behavior outside the projected families is "
            "recorded by name in unconvertedBehaviors.",
            "AI heuristic (AISpecialPowerUpdate) and non-power spellbook "
            "modules are outside this descriptor's power-tree scope.",
        ],
    }
    descriptor["descriptorSha256"] = _digest(descriptor)
    return descriptor


def validate_spellbook_descriptor(value: Mapping[str, object]) -> None:
    """Reject any spellbook descriptor that drifted from its evidence."""

    if value.get("schema") != SCHEMA or value.get("schemaVersion") != SCHEMA_VERSION:
        raise SpellbookCompilerError("spellbook descriptor identity is invalid")
    unsigned = dict(value)
    digest = unsigned.pop("descriptorSha256", None)
    if not isinstance(digest, str) or digest != _digest(unsigned):
        raise SpellbookCompilerError("spellbook descriptor digest is invalid")
    spellbook = value.get("spellBook")
    if not isinstance(spellbook, Mapping) or _SPELL_BOOK_KIND not in {
        str(item) for item in spellbook.get("kindOf", [])
    }:
        raise SpellbookCompilerError(
            "spellbook descriptor spell book evidence is invalid"
        )
    command_points_upgrade = spellbook.get("commandPointsUpgrade")
    if command_points_upgrade is not None and (
        not isinstance(command_points_upgrade, Mapping)
        or set(command_points_upgrade)
        != {
            "triggeredBy",
            "commandPoints",
            "requiredObject",
            "module",
            "sourceIni",
            "line",
        }
        or command_points_upgrade.get("triggeredBy")
        != "Upgrade_MarketplaceUpgradeGrandHarvest"
        or command_points_upgrade.get("commandPoints") != 100
        or command_points_upgrade.get("requiredObject")
        != "NONE +GondorMarketPlace"
        or command_points_upgrade.get("module") != "CommandPointsUpgrade"
        or not isinstance(command_points_upgrade.get("sourceIni"), str)
        or not command_points_upgrade.get("sourceIni")
        or not isinstance(command_points_upgrade.get("line"), int)
        or isinstance(command_points_upgrade.get("line"), bool)
        or int(command_points_upgrade["line"]) <= 0
    ):
        raise SpellbookCompilerError(
            "spellbook CommandPointsUpgrade evidence is invalid"
        )
    sciences = value.get("sciences")
    powers = value.get("powers")
    if not isinstance(sciences, list) or not isinstance(powers, list) or not powers:
        raise SpellbookCompilerError("spellbook descriptor tree rows are invalid")
    for row in powers:
        if not isinstance(row, Mapping):
            raise SpellbookCompilerError("spellbook descriptor power row is invalid")
        if not isinstance(row.get("cast"), Mapping) or not isinstance(
            row.get("effect"), Mapping
        ):
            raise SpellbookCompilerError(
                "spellbook descriptor power payload is invalid"
            )
    leaves = value.get("leaves")
    objects = leaves.get("objects") if isinstance(leaves, Mapping) else None
    if not isinstance(objects, list):
        raise SpellbookCompilerError(
            "spellbook descriptor object leaves are invalid"
        )
    for leaf in objects:
        if not isinstance(leaf, Mapping):
            raise SpellbookCompilerError(
                "spellbook descriptor object leaf is invalid"
            )
        creation_grant = leaf.get("experienceLevelCreate")
        if creation_grant is not None and (
            not isinstance(creation_grant, Mapping)
            or creation_grant.get("module") != "ExperienceLevelCreate"
            or creation_grant.get("mpOnly") is not False
            or not isinstance(creation_grant.get("rank"), int)
            or isinstance(creation_grant.get("rank"), bool)
            or int(creation_grant["rank"]) < 1
            or not isinstance(creation_grant.get("sourceIni"), str)
            or not creation_grant.get("sourceIni")
            or not isinstance(creation_grant.get("line"), int)
            or isinstance(creation_grant.get("line"), bool)
            or int(creation_grant["line"]) <= 0
        ):
            raise SpellbookCompilerError(
                "spellbook ExperienceLevelCreate leaf evidence is invalid"
            )
        experience = leaf.get("experience")
        if (creation_grant is None) != (experience is None):
            raise SpellbookCompilerError(
                "spellbook creation experience contract is incomplete"
            )
        if creation_grant is not None:
            levels = (
                experience.get("levels")
                if isinstance(experience, Mapping)
                else None
            )
            granted_rank = int(creation_grant["rank"])
            if (
                not isinstance(experience, Mapping)
                or experience.get("status") != "compiled"
                or experience.get("initialRank") != granted_rank
                or not isinstance(experience.get("maxLevel"), int)
                or isinstance(experience.get("maxLevel"), bool)
                or int(experience["maxLevel"]) < granted_rank
                or not isinstance(experience.get("sourceIni"), str)
                or not experience.get("sourceIni")
                or not isinstance(levels, list)
                or not levels
                or sum(
                    1
                    for level_row in levels
                    if isinstance(level_row, Mapping)
                    and level_row.get("rank") == granted_rank
                )
                != 1
            ):
                raise SpellbookCompilerError(
                    "spellbook creation experience contract is invalid"
                )
        locks = leaf.get("permanentWeaponLocks")
        if locks is None:
            continue
        valid_lock = (
            isinstance(locks, list)
            and len(locks) == 1
            and isinstance(locks[0], Mapping)
        )
        lock = locks[0] if valid_lock else {}
        if (
            not valid_lock
            or leaf.get("weaponSlot") != "PRIMARY"
            or lock.get("slot") != "PRIMARY"
            or lock.get("state") != "LOCKED_PERMANENTLY"
            or lock.get("module") != "LockWeaponCreate"
            or not isinstance(lock.get("sourceIni"), str)
            or not lock.get("sourceIni")
            or not isinstance(lock.get("line"), int)
            or isinstance(lock.get("line"), bool)
            or int(lock["line"]) <= 0
        ):
            raise SpellbookCompilerError(
                "spellbook LockWeaponCreate leaf evidence is invalid"
            )


__all__ = [
    "SCHEMA",
    "SCHEMA_VERSION",
    "SpellbookCompilerError",
    "compile_spellbook_descriptor",
    "compile_rank_science_grants",
    "validate_spellbook_descriptor",
]
