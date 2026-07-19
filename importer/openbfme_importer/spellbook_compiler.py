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
import re

from .playable_unit_compiler import (
    PlayableUnitCompilerInputs,
    _ancestry,
    _command_slots,
    _effective_top_blocks,
    _effective_values,
    _first,
    _kind_of,
    _named_blocks,
    _resolved_expression,
    _tokens,
    prepare_playable_unit_compiler,
)
from .playable_unit_import import FACTIONS
from .retail_men_damage_effects import parse_fx_lists
from .sage_cst import SageBlock, SageObject
from .sage_gameplay import _digest as _gameplay_digest
from .sage_ini import IniBlock
from .sage_particles import parse_particle_definitions, select_particle_definition


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

_SPELL_BOOK_KIND = "SPELL_BOOK"
_PURCHASE_COMMAND = "purchase_science"
_CAST_COMMAND = "spell_book"
_MAX_EFFECT_MODULES = 512
_MAX_NESTED_BLOCKS = 65_536

# Behavior-module fields bound to typed effect leaves.  Any other
# reference-shaped field on a spell-power module fails closed below.
_MODULE_OCL_FIELDS = frozenset({"ocl", "healocl", "elvenwoodocl"})
_MODULE_FX_FIELDS = frozenset({"triggerfx", "healfx", "elvenwoodfx"})
_MODULE_MODIFIER_FIELDS = frozenset({"attributemodifier"})
_MODULE_UPGRADE_FIELDS = frozenset({"upgradename"})
_MODULE_OBJECT_FIELDS = frozenset({"sunbeamobject", "elvengroveobject"})
_MODULE_WEAPON_FIELDS = frozenset({"weapon", "weaponname"})
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


def _merged_defines(
    documents: Mapping[str, bytes], prepared: PlayableUnitCompilerInputs
) -> dict[str, int | float]:
    constants = dict(prepared.numeric_defines)
    for path in (SCIENCE_PATH, SPECIAL_POWER_PATH):
        for key, value in _numeric_defines(
            _required_document(documents, path), path
        ).items():
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
    value = _resolved_expression(expression, constants)
    if value is None:
        raise SpellbookCompilerError(
            f"{label} has unresolved expression: {expression}"
        )
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
    if not isinstance(values, list) or any(not isinstance(item, str) for item in values):
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
        (spec[2] for spec in FACTIONS if spec[1].casefold() == template.casefold()),
        None,
    )
    if expected is None or faction != expected:
        raise SpellbookCompilerError(
            "faction graph playerTemplate/faction identity pair is invalid"
        )
    graph_identity = _sha256(
        graph.get("inputSetSha256"), "factionGraphInputSetSha256"
    )
    summary = graph.get("summary")
    if not isinstance(summary, Mapping):
        raise SpellbookCompilerError("faction graph summary is invalid")
    unresolved = summary.get("unresolvedCount")
    if unresolved != 0:
        raise SpellbookCompilerError(
            f"faction graph has {unresolved} unresolved census leaves"
        )
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
                raise SpellbookCompilerError("faction graph has multiple SpellBookMP roots")
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
        tuple(
            sorted(intrinsic.values(), key=lambda item: (item.casefold(), item))
        ),
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
        raise SpellbookCompilerError(f"effective PlayerTemplate is missing: {template_id}")
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
        raise SpellbookCompilerError(f"effective CommandButton is missing: {command_id}")
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
        raise SpellbookCompilerError(f"{path} has ambiguous {kind} blocks: {exc}") from exc


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


def _nested_named_blocks(source: bytes, kind: str, path: str) -> dict[str, dict[str, object]]:
    """Parse one flat-nested SAGE family such as ObjectCreationList or Weapon.

    Each named block carries ordered scalar assignments and ordered nested
    sections; sections carry their own assignments in source order.  The parser
    is deliberately lexical: it preserves authored payload without interpreting
    it and fails closed on unbalanced or duplicate blocks.
    """

    header = re.compile(r"^" + re.escape(kind) + r"\s+(\S+)\s*$", re.IGNORECASE)
    if len(source) > 16 * 1024 * 1024 or b"\0" in source:
        raise SpellbookCompilerError(f"{path} is unbounded")
    try:
        text = source.decode("cp1252")
    except UnicodeDecodeError as exc:
        raise SpellbookCompilerError(f"{path} has unsupported encoding") from exc
    lines = [
        line
        for raw in text.splitlines()
        if (line := re.sub(r"\s+", " ", raw.split(";", 1)[0].split("//", 1)[0]).strip())
    ]
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
            section: dict[str, object] = {"kind": tokens[0], "assignments": [], "sections": []}
            if stack:
                nested = stack[-1]["sections"]
                assert isinstance(nested, list)
                nested.append(section)
            else:
                sections.append(section)
            stack.append(section)
            cursor += 1
        if not closed:
            raise SpellbookCompilerError(f"{path} has an unterminated {kind} block: {name}")
        result[key] = {"id": name, "assignments": assignments, "sections": sections}
        index = cursor
    return result


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
        )
        self._weapons = _nested_named_blocks(
            _required_document(documents, WEAPON_PATH), "Weapon", WEAPON_PATH
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
        self.ocls: dict[str, dict[str, object]] = {}
        self.fx_lists: dict[str, dict[str, object]] = {}
        self.weapons: dict[str, dict[str, object]] = {}
        self.modifiers: dict[str, dict[str, object]] = {}
        self.upgrades: dict[str, dict[str, object]] = {}
        self.objects: dict[str, dict[str, object]] = {}
        self.particles: dict[str, dict[str, object]] = {}
        self.audio_ids: dict[str, str] = {}

    def object_reference(self, identifier: str, label: str) -> None:
        key = identifier.casefold()
        if key in self.objects:
            return
        target = self._prepared.objects.get(key)
        if target is None:
            raise SpellbookCompilerError(f"{label} references a missing Object: {identifier}")
        kinds = _kind_of(_ancestry(self._prepared.objects, target))
        self.objects[key] = {"id": target.name, "kindOf": list(kinds)}

    def particle_reference(self, identifier: str, label: str) -> None:
        key = identifier.casefold()
        if key in self.particles:
            return
        try:
            definition = select_particle_definition(self._particle_definitions, identifier)
        except ValueError as exc:
            raise SpellbookCompilerError(f"{label}: {exc}") from exc
        self.particles[key] = {
            "id": definition.name,
            "kind": definition.kind,
            "sourceSha256": definition.source.sha256,
        }

    def _fx_section(self, section: Mapping[str, object], label: str) -> dict[str, object]:
        kind = str(section.get("kind", ""))
        fields: list[dict[str, str]] = []
        for assignment in section.get("assignments", []):
            if not isinstance(assignment, Mapping):
                raise SpellbookCompilerError(f"{label} FXList section payload is invalid")
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
            self.particle_reference(identifier, f"{label} ParticleSystem")
            row["particleSystemId"] = self.particles[identifier.casefold()]["id"]
        elif folded == _FX_SOUND_SECTION:
            names = _fx_field_values(section, "Name")
            if len(names) != 1:
                raise SpellbookCompilerError(
                    f"{label} Sound nugget must have exactly one Name"
                )
            identifier = _first((names[0],))
            if identifier is None:
                raise SpellbookCompilerError(f"{label} Sound nugget has an invalid Name")
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
            raise SpellbookCompilerError(f"{label} references a missing FXList: {identifier}")
        sections = record.get("sections")
        if not isinstance(sections, list):
            raise SpellbookCompilerError(f"FXList {identifier} payload is invalid")
        nuggets = [self._fx_section(section, f"{label} FXList {identifier}") for section in sections]
        source = record.get("sourceSpan")
        if not isinstance(source, Mapping) or not isinstance(source.get("sha256"), str):
            raise SpellbookCompilerError(f"FXList {identifier} source evidence is invalid")
        self.fx_lists[key] = {
            "id": str(record.get("fxListId", identifier)),
            "sourceSha256": _sha256(source.get("sha256"), f"FXList {identifier} sourceSha256"),
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
                fields.append({"key": str(field), "value": str(value)})
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
                    self.particle_reference(token, f"ObjectCreationList {identifier} ParticleSystem")
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
            raise SpellbookCompilerError(f"{label} references a missing Upgrade: {identifier}")
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
            raise SpellbookCompilerError(f"{label} references a missing Weapon: {identifier}")
        fields: list[dict[str, str]] = []
        fire_fx: list[str] = []
        projectile: str | None = None
        for field, value in block["assignments"]:  # type: ignore[misc]
            fields.append({"key": str(field), "value": str(value)})
            folded = str(field).casefold()
            if folded == "firefx":
                token = _first((str(value),))
                if token is None:
                    raise SpellbookCompilerError(f"Weapon {identifier} has an invalid FireFX")
                fire_fx.append(self.fx_list(token, f"Weapon {identifier} FireFX"))
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
                self.object_reference(token, f"Weapon {identifier} ProjectileTemplateName")
                projectile = self.objects[token.casefold()]["id"]
            elif folded.endswith("ocl") or (folded.endswith("template") and folded != "projectiletemplatename"):
                raise SpellbookCompilerError(
                    f"Weapon {identifier} has an unsupported effect leaf field: {field}"
                )
        nuggets = [
            self._weapon_nugget(section, f"Weapon {identifier}")
            for section in block["sections"]  # type: ignore[misc]
        ]
        row: dict[str, object] = {"id": str(block["id"]), "fields": fields}
        if fire_fx:
            row["fireFx"] = fire_fx
        if projectile is not None:
            row["projectileTemplateId"] = projectile
        if nuggets:
            row["nuggets"] = nuggets
        self.weapons[key] = row
        return str(row["id"])

    def _weapon_nugget(self, section: Mapping[str, object], label: str) -> dict[str, object]:
        fields: list[dict[str, str]] = []
        row: dict[str, object] = {"kind": str(section.get("kind", "")), "fields": fields}
        fire_fx: list[str] = []
        for field, value in section.get("assignments", []):  # type: ignore[misc]
            fields.append({"key": str(field), "value": str(value)})
            folded = str(field).casefold()
            if folded == "firefx":
                token = _first((str(value),))
                if token is None:
                    raise SpellbookCompilerError(f"{label} has an invalid nugget FireFX")
                fire_fx.append(self.fx_list(token, f"{label} nugget FireFX"))
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
        if key in result:
            raise SpellbookCompilerError(
                f"spell-power {templates[0]} is bound by multiple modules"
            )
        result[key] = block
        if len(result) > _MAX_EFFECT_MODULES:
            raise SpellbookCompilerError("spell-power module count exceeds limit")
    return result


def _module_field_rows(block: SageBlock) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for assignment in block.assignments:
        rows.append({"key": assignment.key, "value": assignment.value.strip()})
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
            ocls.append(resolver.object_creation_list(token, f"{label} {assignment.key}"))
        elif folded in _MODULE_FX_FIELDS:
            if token is None:
                raise SpellbookCompilerError(f"{label} has an invalid {assignment.key}")
            fx_lists.append(resolver.fx_list(token, f"{label} {assignment.key}"))
        elif folded in _MODULE_MODIFIER_FIELDS:
            if token is None:
                raise SpellbookCompilerError(f"{label} has an invalid {assignment.key}")
            modifiers.append(resolver.attribute_modifier(token, f"{label} {assignment.key}"))
        elif folded in _MODULE_UPGRADE_FIELDS:
            if token is None:
                raise SpellbookCompilerError(f"{label} has an invalid {assignment.key}")
            upgrades.append(resolver.upgrade(token, f"{label} {assignment.key}"))
        elif folded in _MODULE_OBJECT_FIELDS:
            if token is None:
                raise SpellbookCompilerError(f"{label} has an invalid {assignment.key}")
            resolver.object_reference(token, f"{label} {assignment.key}")
            objects.append(
                {"field": assignment.key, "id": resolver.objects[token.casefold()]["id"]}
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
        raise SpellbookCompilerError(f"effective CommandSet is missing: {command_set_id}")
    store_set = prepared.command_sets.get(store_set_id.casefold())
    if store_set is None:
        raise SpellbookCompilerError(f"effective CommandSet is missing: {store_set_id}")

    resolver = _LeafResolver(documents, prepared, constants, census_upgrades)

    science_rows: list[dict[str, object]] = []
    tree_science_ids: dict[str, str] = {}
    referenced_science_ids: dict[str, str] = {}

    def _science_row(block: IniBlock, purchase: dict[str, object] | None) -> dict[str, object]:
        digest = _cross_check_definition(block, census_sciences, "Science")
        groups = _prerequisite_groups(block)
        flat = sorted(
            {token for group in groups for token in group},
            key=lambda item: (item.casefold(), item),
        )
        label = f"Science {block.name}"
        grantable = _one_value(block, "IsGrantable", label)
        if grantable is None or grantable.strip().casefold() not in {"yes", "no"}:
            raise SpellbookCompilerError(f"{label} has an invalid IsGrantable")
        row: dict[str, object] = {
            "id": block.name,
            "definitionSha256": digest,
            "isGrantable": grantable.strip().casefold() == "yes",
            "pointCost": _required_scalar(block, "SciencePurchasePointCost", constants, label),
            "prerequisiteGroups": [list(group) for group in groups],
            "prerequisites": flat,
        }
        mp_cost = _optional_scalar(block, "SciencePurchasePointCostMP", constants, label)
        if mp_cost is None:
            if purchase is not None:
                raise SpellbookCompilerError(
                    f"{label} is purchasable but has no SciencePurchasePointCostMP"
                )
        else:
            row["pointCostMP"] = mp_cost
        if purchase is not None:
            row["purchase"] = purchase
        return row

    for slot, command_id in _command_slots(store_set):
        button = _button(prepared, command_id)
        science_id = _unique_button_target(button, "Science", _PURCHASE_COMMAND, "spell store")
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

    if {item.casefold() for item in expected_sciences} != set(tree_science_ids):
        raise SpellbookCompilerError(
            "spell store science set disagrees with census spellbookSciences"
        )

    power_rows: list[dict[str, object]] = []
    tree_power_ids: dict[str, str] = {}
    effect_modules = _effect_modules(lineage)
    for slot, command_id in _command_slots(command_set):
        button = _button(prepared, command_id)
        power_id = _unique_button_target(button, "SpecialPower", _CAST_COMMAND, "spell book")
        key = power_id.casefold()
        if key in tree_power_ids:
            raise SpellbookCompilerError(
                f"spell book casts {power_id} from multiple slots"
            )
        block = power_blocks.get(key)
        if block is None:
            raise SpellbookCompilerError(f"effective SpecialPower is missing: {power_id}")
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
                raise SpellbookCompilerError(f"effective Science is missing: {science_id}")
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
            "fields": _module_field_rows(module),
            "references": _module_leaves(module, resolver, label),
        }
        row["effect"] = effect
        power_rows.append(row)
        tree_power_ids[key] = block.name

    if {item.casefold() for item in expected_powers} != set(tree_power_ids):
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
        key=str.casefold,
    )
    text_ids = sorted(
        {
            token
            for row in (*science_rows, *power_rows)
            for binding in (row.get("purchase"), row.get("cast"))
            if isinstance(binding, Mapping)
            for token in binding.get("textIds", [])
        },
        key=str.casefold,
    )
    audio_ids = sorted(resolver.audio_ids.values(), key=str.casefold)

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
            raise SpellbookCompilerError(f"spellbook source document is missing: {path}")
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
        },
        "sciences": science_rows,
        "powers": power_rows,
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
            "particles": [resolver.particles[key] for key in sorted(resolver.particles)],
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
                    (resolved_strings or {}).items(), key=lambda item: item[0].casefold()
                )
            },
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
            "Objects created by an ObjectCreationList are typed references "
            "only; their internal payloads (spell receptacle weapons, Draw, "
            "client audio, and deeper module templates such as FloodUpdate "
            "FloodMember chains) belong to the object conversion lanes and "
            "are not traversed.",
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
        raise SpellbookCompilerError("spellbook descriptor spell book evidence is invalid")
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
            raise SpellbookCompilerError("spellbook descriptor power payload is invalid")


__all__ = [
    "SCHEMA",
    "SCHEMA_VERSION",
    "SpellbookCompilerError",
    "compile_spellbook_descriptor",
    "validate_spellbook_descriptor",
]
