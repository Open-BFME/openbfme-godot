"""Convert the UNIT-TO-BLOCK BINDING half of retail's War of the Ring
auto-resolve system - the ``AutoResolve*`` fields authored inside retail's 502
object documents - into one manifest.

WHY THIS MODULE EXISTS
----------------------
``living_world_autoresolve.py`` already converts retail's nine
``livingworldautoresolve*.ini`` documents. Those documents declare the NUMBERS:
131 armors, 150 weapons, 102 bodies, 9 combat chains, 13 leaderships. What they
do NOT say is WHICH UNIT USES WHICH. That binding is authored somewhere else
entirely - inside the object templates::

    ChildObject AngmarSoldierHorde GondorFighterHorde
        AutoResolveUnitType = AutoResolveUnit_Soldier
        AutoResolveCombatChain = AutoResolve_SoldierCombatChain
        AutoResolveBody = AutoResolve_GondorFighterHordeBody

        AutoResolveArmor
            RequiredUpgrades = Upgrade_AngmarDarkIronArmor
            Armor = AutoResolve_GondorSoldierHeavyArmor
        End
    End

So the armor and the weapon are NOT scalar fields. They are ``Armor =`` /
``Weapon =`` rows inside REPEATED ``AutoResolveArmor`` / ``AutoResolveWeapon``
blocks, each gated by ``RequiredUpgrades`` / ``ExcludedUpgrades`` (either, both
or neither, several tokens each). Retail evaluates those rows IN FILE ORDER, so
this module preserves file order and never sorts them.

THE ONE CORRECTNESS PROPERTY THAT MATTERS MOST
-----------------------------------------------
A COMMENTED-OUT LINE IS NOT A REFERENCE. Retail's object documents carry 27
commented-out auto-resolve lines in both of retail's comment spellings, whole
line and trailing::

    ;   AutoResolveArmor
    ;       Armor = AutoResolve_IsengardWargRiderHeavyArmor
    ;   End
    //  AutoResolveLeadership = AutoResolve_GandalfBonus

A prior analysis counted those as live and concluded that 15 leadership blocks
were DANGLING - referenced by a unit but never declared. They are not. With the
comments correctly excluded the dangling count is ZERO in every kind. Because
that miscount is easy to repeat, this manifest carries BOTH numbers:
``dangling`` (correct) and ``danglingIfCommentedCounted`` (the miscount), so the
difference is in the data and not only in a docstring.

THE FINDING
-----------
Angmar - the RotWK faction - IS bound. Nine of its object documents carry live
auto-resolve bindings. But they bind to GONDOR-, DWARVEN-, ISENGARD- and
MORDOR-authored blocks, not to the Angmar-NAMED blocks the auto-resolve
documents declare. Those Angmar-named blocks are therefore genuine orphans while
Angmar itself auto-resolves on real, live, authored numbers - NOT on the
documented fallback blocks. The evidence is emitted in ``findings`` by file and
by block name, as a measurement.

WHAT IT REFUSES TO DO
---------------------
* It never substitutes a default for a name it could not bind. An object the
  strategic layer needs but that carries no auto-resolve data is emitted BY NAME
  in ``coverage.unbound`` with the distinguishing reason.
* It never invents a parent. A ``ChildObject`` whose parent is not declared
  anywhere is a gap by name; an inheritance CYCLE is reported by name and the
  walk stops.
* It never guesses a block. An ``AutoResolveArmor`` block with no ``Armor =``
  row is a gap, not a dropped row.
* It reads through the CATALOG, never through the effective-assets cache. The
  winner for a name is the lowest ``(precedence, archive.casefold())``, which is
  what :class:`~.livingmap_bundle.CatalogReader` already implements.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import re
from dataclasses import dataclass, field
from typing import Any, Mapping, Sequence

from .livingmap_bundle import CatalogReader
from .util import write_json_atomic

SCHEMA = "openbfme.living-world-autoresolve-bindings"
SCHEMA_VERSION = 1

MANIFEST_NAME = "living-world-autoresolve-bindings.json"

#: Object documents are the catalog winners under this prefix with this suffix.
#: RotWK ships 502 of them.
OBJECT_INI_PREFIX = "data/ini/object/"
OBJECT_INI_SUFFIX = ".ini"

#: The five reference kinds, in the order they are reported. The value is the
#: key the auto-resolve bundle declares them under - the two happen to agree,
#: which is asserted rather than assumed when the bundle is loaded.
KINDS: tuple[str, ...] = ("armors", "weapons", "bodies", "combatChains", "leaderships")

#: Scalar object field -> (manifest key, census kind).
SCALAR_FIELDS: Mapping[str, tuple[str, str | None]] = {
    "autoresolveunittype": ("unitType", None),
    "autoresolvebody": ("body", "bodies"),
    "autoresolvecombatchain": ("combatChain", "combatChains"),
    "autoresolveleadership": ("leadership", "leaderships"),
}

#: Repeated block opener -> (manifest key, row field, census kind).
BLOCK_FIELDS: Mapping[str, tuple[str, str, str]] = {
    "autoresolvearmor": ("armorSet", "armor", "armors"),
    "autoresolveweapon": ("weaponSet", "weapon", "weapons"),
}

#: The keys an object carries that bind it to an auto-resolve BLOCK. A bare
#: ``AutoResolveUnitType`` is authored data but binds no block, so it is counted
#: separately rather than folded into the bound total.
BLOCK_BINDING_KEYS: tuple[str, ...] = ("body", "combatChain", "leadership", "armorSet", "weaponSet")

#: The four names retail's own comments mark as reached BY FALLBACK rather than
#: by name. They are orphans by construction, so they are split out of the
#: orphan lists instead of inflating them.
DOCUMENTED_FALLBACKS: Mapping[str, str] = {
    "armors": "AutoResolve_DefaultArmor",
    "weapons": "AutoResolve_DefaultWeapon",
    "bodies": "AutoResolve_DefaultBody",
    "combatChains": "AutoResolve_DefaultCombatChain",
}

#: Bounded so a hostile or corrupt catalog cannot make this module allocate
#: without limit. Retail's real numbers are far below every one of these.
MAX_OBJECT_INI_BYTES = 8 * 1024 * 1024
MAX_OBJECT_INIS = 5_000
MAX_OBJECTS = 100_000
MAX_ROWS_PER_OBJECT = 512
MAX_INHERITANCE_DEPTH = 64

#: ``Object Foo`` / ``ChildObject Foo Bar``. ``ObjectReskin`` is DELIBERATELY not
#: matched: a reskin restates art, never auto-resolve data, and admitting the
#: 759 reskins RotWK ships would silently change the object census. The name may
#: not contain ``=`` so that a stray ``Object = Foo`` assignment inside a module
#: cannot be mistaken for a template header.
_HEADER = re.compile(r"^(Object|ChildObject)\s+([^\s=]+)(?:\s+([^\s=]+))?\s*$", re.IGNORECASE)

_ASSIGNMENT = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(.+?)\s*$")


class AutoResolveBindingsError(RuntimeError):
    """The manifest cannot be built, with the exact reason."""


# ---------------------------------------------------------------------------
# lexing
# ---------------------------------------------------------------------------


def split_comment(raw: str) -> tuple[str, str]:
    """Split one authored line into its CODE half and its COMMENT half.

    Retail spells a comment two ways, ``;`` and ``//``, whole-line and trailing,
    and this module has to see both halves: the code half is what binds, the
    comment half is what a prior analysis miscounted as binding. Quoted text is
    respected so a ``"a;b"`` display string cannot truncate a line.
    """

    quoted = False
    index = 0
    while index < len(raw):
        character = raw[index]
        if character == '"':
            quoted = not quoted
        elif not quoted and character == ";":
            return raw[:index], raw[index + 1 :]
        elif not quoted and character == "/" and raw[index + 1 : index + 2] == "/":
            return raw[:index], raw[index + 2 :]
        index += 1
    return raw, ""


def _assignment(line: str) -> tuple[str, str] | None:
    match = _ASSIGNMENT.match(line.strip())
    if match is None:
        return None
    return match.group(1), match.group(2).strip()


def _first_token(value: str) -> str:
    tokens = value.split()
    return tokens[0] if tokens else ""


# ---------------------------------------------------------------------------
# the parsed shapes
# ---------------------------------------------------------------------------


@dataclass(slots=True)
class Row:
    """One ``AutoResolveArmor`` / ``AutoResolveWeapon`` block, in file order."""

    block: str
    required_upgrades: tuple[str, ...]
    excluded_upgrades: tuple[str, ...]
    line: int

    def public(self) -> dict[str, Any]:
        return {
            "block": self.block,
            "requiredUpgrades": list(self.required_upgrades),
            "excludedUpgrades": list(self.excluded_upgrades),
        }


@dataclass(slots=True)
class ObjectRecord:
    """One ``Object`` / ``ChildObject`` template and the fields IT ITSELF sets."""

    name: str
    kind: str
    parent: str | None
    virtual_path: str
    line: int
    unit_type: str | None = None
    body: str | None = None
    combat_chain: str | None = None
    leadership: str | None = None
    armor_set: list[Row] = field(default_factory=list)
    weapon_set: list[Row] = field(default_factory=list)

    def scalar(self, key: str) -> str | None:
        return {
            "unitType": self.unit_type,
            "body": self.body,
            "combatChain": self.combat_chain,
            "leadership": self.leadership,
        }[key]

    def sets(self, key: str) -> list[Row]:
        return self.armor_set if key == "armorSet" else self.weapon_set

    @property
    def declares_anything(self) -> bool:
        return bool(
            self.unit_type
            or self.body
            or self.combat_chain
            or self.leadership
            or self.armor_set
            or self.weapon_set
        )


@dataclass(slots=True)
class Reference:
    """One place an object named an auto-resolve block."""

    kind: str
    name: str
    virtual_path: str
    line: int
    owner: str

    def public(self) -> dict[str, Any]:
        return {
            "kind": self.kind,
            "name": self.name,
            "sourceFile": self.virtual_path,
            "line": self.line,
            "object": self.owner,
        }


@dataclass(slots=True)
class Gap:
    """Something the module could not understand, BY NAME, with a reason."""

    reason: str
    subject: str
    virtual_path: str
    line: int
    detail: str = ""

    def public(self) -> dict[str, Any]:
        record: dict[str, Any] = {
            "reason": self.reason,
            "subject": self.subject,
            "sourceFile": self.virtual_path,
            "line": self.line,
        }
        if self.detail:
            record["detail"] = self.detail
        return record


@dataclass(slots=True)
class Source:
    """Provenance for one object document, in the shape the sibling bundle uses."""

    virtual_path: str
    archive: str
    size: int
    sha256: str

    def public(self) -> dict[str, Any]:
        return {
            "virtualPath": self.virtual_path,
            "archive": self.archive,
            "bytes": self.size,
            "sha256": self.sha256,
        }


class _Parse:
    """Everything one sweep of the object documents produced."""

    def __init__(self) -> None:
        self.objects: dict[str, ObjectRecord] = {}
        self.order: list[str] = []
        #: ``name.casefold() -> name`` so a template can be looked up the way
        #: retail's own case-insensitive resolution would find it.
        self.folded: dict[str, str] = {}
        self.live: list[Reference] = []
        self.commented: list[Reference] = []
        self.gaps: list[Gap] = []
        self.sources: dict[str, Source] = {}
        self.duplicates: list[Gap] = []


# ---------------------------------------------------------------------------
# the object-document reader
# ---------------------------------------------------------------------------


class _BlockState:
    """A half-read ``AutoResolveArmor`` / ``AutoResolveWeapon`` block."""

    __slots__ = ("key", "opener", "row_field", "kind", "line", "block", "required", "excluded")

    def __init__(self, key: str, row_field: str, kind: str, line: int, opener: str) -> None:
        self.key = key
        #: Retail's own spelling of the block opener, so a gap quotes the file.
        self.opener = opener
        self.row_field = row_field
        self.kind = kind
        self.line = line
        self.block: str | None = None
        self.required: tuple[str, ...] = ()
        self.excluded: tuple[str, ...] = ()

    def feed(self, key: str, value: str) -> None:
        folded = key.casefold()
        if folded == self.row_field:
            self.block = _first_token(value)
        elif folded == "requiredupgrades":
            self.required = tuple(value.split())
        elif folded == "excludedupgrades":
            self.excluded = tuple(value.split())


def _parse_object_ini(virtual_path: str, text: str, parse: _Parse) -> None:
    """Read ONE object document into ``parse``.

    Two state machines run over the same lines: one over the CODE half, which
    produces bindings, and a shadow one over the COMMENT half, which produces
    nothing but the commented-reference census. The shadow machine exists so a
    commented ``Armor = AutoResolve_X`` is scoped to its commented
    ``AutoResolveArmor`` block exactly the way the live one is, rather than being
    guessed at by its name.
    """

    current: ObjectRecord | None = None
    pending: _BlockState | None = None
    shadow: _BlockState | None = None

    def close(state: _BlockState, owner: ObjectRecord, live: bool) -> None:
        if state.block is None:
            parse.gaps.append(
                Gap(
                    reason="autoresolve-block-without-a-row",
                    subject=owner.name,
                    virtual_path=virtual_path,
                    line=state.line,
                    detail=(
                        f"{state.opener} block declares no "
                        f"{state.row_field.capitalize()} = row"
                    ),
                )
            )
            return
        row = Row(state.block, state.required, state.excluded, state.line)
        reference = Reference(state.kind, state.block, virtual_path, state.line, owner.name)
        if live:
            target = owner.sets(state.key)
            if len(target) >= MAX_ROWS_PER_OBJECT:
                raise AutoResolveBindingsError(
                    f"{virtual_path}: {owner.name} has more than "
                    f"{MAX_ROWS_PER_OBJECT} {state.key} rows"
                )
            target.append(row)
            parse.live.append(reference)
        else:
            parse.commented.append(reference)

    for number, raw in enumerate(text.splitlines(), start=1):
        code, comment = split_comment(raw)

        # --- the shadow (commented) machine -------------------------------
        stripped_comment = comment.strip()
        if stripped_comment and current is not None:
            folded = stripped_comment.casefold()
            if shadow is not None:
                if folded == "end":
                    close(shadow, current, live=False)
                    shadow = None
                else:
                    item = _assignment(stripped_comment)
                    if item is not None:
                        shadow.feed(*item)
            elif folded in BLOCK_FIELDS:
                key, row_field, kind = BLOCK_FIELDS[folded]
                shadow = _BlockState(key, row_field, kind, number, stripped_comment)
            else:
                item = _assignment(stripped_comment)
                if item is not None:
                    mapped = SCALAR_FIELDS.get(item[0].casefold())
                    if mapped is not None and mapped[1] is not None:
                        parse.commented.append(
                            Reference(
                                mapped[1],
                                _first_token(item[1]),
                                virtual_path,
                                number,
                                current.name,
                            )
                        )

        # --- the live machine ---------------------------------------------
        line = code.strip()
        if not line:
            continue
        header = _HEADER.match(line)
        if header is not None:
            if pending is not None and current is not None:
                parse.gaps.append(
                    Gap(
                        reason="unterminated-autoresolve-block",
                        subject=current.name,
                        virtual_path=virtual_path,
                        line=pending.line,
                        detail=f"{pending.opener} was still open at {header.group(2)}",
                    )
                )
            pending = None
            shadow = None
            name = header.group(2)
            record = ObjectRecord(
                name=name,
                kind=header.group(1),
                parent=header.group(3),
                virtual_path=virtual_path,
                line=number,
            )
            existing = parse.objects.get(name)
            if existing is not None:
                parse.duplicates.append(
                    Gap(
                        reason="object-declared-twice",
                        subject=name,
                        virtual_path=virtual_path,
                        line=number,
                        detail=f"first declared at {existing.virtual_path}:{existing.line}",
                    )
                )
            if len(parse.objects) >= MAX_OBJECTS and existing is None:
                raise AutoResolveBindingsError(
                    f"the catalog declares more than {MAX_OBJECTS} object templates"
                )
            parse.objects[name] = record
            parse.folded[name.casefold()] = name
            parse.order.append(name)
            current = record
            continue
        if current is None:
            continue
        folded = line.casefold()
        if pending is not None:
            if folded == "end":
                close(pending, current, live=True)
                pending = None
                continue
            item = _assignment(line)
            if item is not None:
                pending.feed(*item)
            continue
        if folded in BLOCK_FIELDS:
            key, row_field, kind = BLOCK_FIELDS[folded]
            pending = _BlockState(key, row_field, kind, number, line)
            continue
        item = _assignment(line)
        if item is None:
            continue
        mapped = SCALAR_FIELDS.get(item[0].casefold())
        if mapped is None:
            continue
        manifest_key, kind = mapped
        value = _first_token(item[1])
        if not value:
            parse.gaps.append(
                Gap(
                    reason="autoresolve-field-without-a-value",
                    subject=current.name,
                    virtual_path=virtual_path,
                    line=number,
                    detail=item[0],
                )
            )
            continue
        if manifest_key == "unitType":
            current.unit_type = value
        elif manifest_key == "body":
            current.body = value
        elif manifest_key == "combatChain":
            current.combat_chain = value
        else:
            current.leadership = value
        if kind is not None:
            parse.live.append(Reference(kind, value, virtual_path, number, current.name))

    if pending is not None and current is not None:
        parse.gaps.append(
            Gap(
                reason="unterminated-autoresolve-block",
                subject=current.name,
                virtual_path=virtual_path,
                line=pending.line,
                detail=f"{pending.opener} was still open at end of file",
            )
        )


def object_ini_paths(reader: Any) -> list[str]:
    """The catalog winners that are object documents, sorted, deterministic."""

    winners = getattr(reader, "_winners", None)
    if winners is None:  # pragma: no cover - CatalogReader always has it
        raise AutoResolveBindingsError("the reader does not expose catalog winners")
    paths = sorted(
        name
        for name in winners
        if name.startswith(OBJECT_INI_PREFIX) and name.endswith(OBJECT_INI_SUFFIX)
    )
    if not paths:
        raise AutoResolveBindingsError(
            f"the catalog carries no {OBJECT_INI_PREFIX}*{OBJECT_INI_SUFFIX} winner"
        )
    if len(paths) > MAX_OBJECT_INIS:
        raise AutoResolveBindingsError(
            f"the catalog carries {len(paths)} object documents, over the "
            f"{MAX_OBJECT_INIS} ceiling"
        )
    return paths


def read_objects(reader: Any) -> _Parse:
    """Sweep every object document the catalog wins with."""

    parse = _Parse()
    for virtual_path in object_ini_paths(reader):
        entry = reader.resolve(virtual_path)
        if entry is None:  # pragma: no cover - the path came from the winner map
            raise AutoResolveBindingsError(f"{virtual_path} is not in the catalog")
        if entry.size > MAX_OBJECT_INI_BYTES:
            raise AutoResolveBindingsError(
                f"{virtual_path} is {entry.size} bytes, over the "
                f"{MAX_OBJECT_INI_BYTES} ceiling"
            )
        payload = reader.read(entry)
        # Retail's object documents are latin-1; decoding cannot fail, so a
        # stray high byte in an artist's comment can never abort a sweep.
        before = len(parse.live)
        _parse_object_ini(virtual_path, payload.decode("latin-1"), parse)
        if len(parse.live) > before:
            parse.sources[virtual_path] = Source(
                virtual_path=virtual_path,
                archive=entry.archive,
                size=len(payload),
                sha256=hashlib.sha256(payload).hexdigest(),
            )
    return parse


# ---------------------------------------------------------------------------
# inheritance
# ---------------------------------------------------------------------------


class _Inheritance:
    """Transitive ``ChildObject`` resolution, cycle-safe and memoised."""

    def __init__(self, parse: _Parse) -> None:
        self._parse = parse
        self._cache: dict[str, dict[str, Any]] = {}
        self.gaps: list[Gap] = []
        self._reported: set[str] = set()

    def resolve(self, name: str) -> dict[str, Any]:
        cached = self._cache.get(name)
        if cached is None:
            cached = self._walk(name, ())
            self._cache[name] = cached
        return cached

    def _walk(self, name: str, stack: tuple[str, ...]) -> dict[str, Any]:
        record = self._parse.objects.get(name)
        if record is None:
            return {}
        if name in stack:
            self._report(
                record,
                "childobject-inheritance-cycle",
                " -> ".join((*stack, name)),
            )
            return {}
        if len(stack) >= MAX_INHERITANCE_DEPTH:
            self._report(
                record,
                "childobject-inheritance-too-deep",
                f"deeper than {MAX_INHERITANCE_DEPTH}: {' -> '.join((*stack, name))}",
            )
            return {}
        resolved: dict[str, Any] = {}
        chain: list[str] = []
        parent = record.parent
        if parent:
            looked_up = self._parse.folded.get(parent.casefold())
            if looked_up is None:
                self._report(
                    record,
                    "childobject-parent-not-declared",
                    f"{name} inherits from {parent}, which no object document declares",
                )
            else:
                inherited = self._walk(looked_up, (*stack, name))
                if inherited:
                    resolved.update(
                        {key: value for key, value in inherited.items() if key != "inheritedFrom"}
                    )
                    chain.append(looked_up)
                    chain.extend(inherited.get("inheritedFrom", ()))
        for key in ("unitType", "body", "combatChain", "leadership"):
            own = record.scalar(key)
            if own:
                resolved[key] = own
        for key in ("armorSet", "weaponSet"):
            own_rows = record.sets(key)
            if own_rows:
                resolved[key] = list(own_rows)
        if not resolved:
            return {}
        # Only ancestors that actually CONTRIBUTED are named, so the chain reads
        # as evidence rather than as the whole family tree.
        contributing = [
            ancestor
            for ancestor in chain
            if self._parse.objects[ancestor].declares_anything
        ]
        if contributing:
            resolved["inheritedFrom"] = contributing
        return resolved

    def _report(self, record: ObjectRecord, reason: str, detail: str) -> None:
        key = f"{reason}:{record.name}"
        if key in self._reported:
            return
        self._reported.add(key)
        self.gaps.append(
            Gap(
                reason=reason,
                subject=record.name,
                virtual_path=record.virtual_path,
                line=record.line,
                detail=detail,
            )
        )


def _binds_a_block(resolved: Mapping[str, Any]) -> bool:
    return any(key in resolved for key in BLOCK_BINDING_KEYS)


# ---------------------------------------------------------------------------
# the auto-resolve bundle
# ---------------------------------------------------------------------------


def declared_blocks(bundle: Mapping[str, Any]) -> dict[str, dict[str, str]]:
    """``kind -> {name.casefold(): name}`` for every block the bundle declares."""

    schema = bundle.get("schema")
    if schema != "openbfme.living-world-autoresolve":
        raise AutoResolveBindingsError(
            f"the auto-resolve document declares schema {schema!r}, "
            "not 'openbfme.living-world-autoresolve'"
        )
    declared: dict[str, dict[str, str]] = {}
    for kind in KINDS:
        table = bundle.get(kind)
        if not isinstance(table, Mapping):
            raise AutoResolveBindingsError(
                f"the auto-resolve document carries no {kind} table"
            )
        declared[kind] = {name.casefold(): name for name in table}
    return declared


# ---------------------------------------------------------------------------
# coverage against the living-world document
# ---------------------------------------------------------------------------


def _thing_templates(document: Mapping[str, Any]) -> tuple[list[str], list[str]]:
    """Every distinct ``playerArmies[].entries[].thingTemplate``, sorted."""

    armies = document.get("playerArmies")
    if not isinstance(armies, Sequence):
        raise AutoResolveBindingsError(
            "the living-world document carries no playerArmies list"
        )
    templates: set[str] = set()
    problems: list[str] = []
    for army in armies:
        if not isinstance(army, Mapping):
            problems.append("a playerArmies row is not an object")
            continue
        entries = army.get("entries")
        if entries is None:
            continue
        if not isinstance(entries, Sequence):
            problems.append(f"{army.get('name', '<unnamed>')}: entries is not a list")
            continue
        for entry in entries:
            if not isinstance(entry, Mapping):
                continue
            template = entry.get("thingTemplate")
            if isinstance(template, str) and template:
                templates.add(template)
    return sorted(templates), problems


# ---------------------------------------------------------------------------
# the manifest
# ---------------------------------------------------------------------------


def build_from_reader(
    reader: Any,
    autoresolve: Mapping[str, Any],
    living_world: Mapping[str, Any] | None = None,
    *,
    living_world_absent_reason: str = "no --living-world document was given",
) -> dict[str, Any]:
    """The manifest, from anything with ``resolve``/``read``. No file is written.

    Split out from :func:`build_bundle` the way the sibling converter splits it,
    so the parser can be exercised against synthetic documents - the only way the
    cycle, empty-block and dangling paths can be tested at all, because retail's
    own 502 documents exercise none of them.
    """

    declared = declared_blocks(autoresolve)
    parse = read_objects(reader)
    inheritance = _Inheritance(parse)

    # --- objects -----------------------------------------------------------
    objects: dict[str, Any] = {}
    bound_names: list[str] = []
    unit_type_only: list[str] = []
    for name in parse.order:
        resolved = inheritance.resolve(name)
        if not resolved:
            continue
        record = parse.objects[name]
        public: dict[str, Any] = {"sourceFile": record.virtual_path}
        for key in ("unitType", "body", "combatChain", "leadership"):
            if key in resolved:
                public[key] = resolved[key]
        for key in ("armorSet", "weaponSet"):
            rows = resolved.get(key)
            if rows:
                public[key] = [row.public() for row in rows]
        if "inheritedFrom" in resolved:
            public["inheritedFrom"] = list(resolved["inheritedFrom"])
        objects[name] = public
        if _binds_a_block(resolved):
            bound_names.append(name)
        else:
            unit_type_only.append(name)

    direct_names = sorted(
        name for name, record in parse.objects.items() if record.declares_anything
    )
    direct_block_names = sorted(
        name
        for name, record in parse.objects.items()
        if record.body or record.combat_chain or record.leadership
        or record.armor_set or record.weapon_set
    )

    # --- census ------------------------------------------------------------
    census: dict[str, Any] = {}
    live_names: dict[str, set[str]] = {kind: set() for kind in KINDS}
    live_folded: dict[str, set[str]] = {kind: set() for kind in KINDS}
    commented_names: dict[str, set[str]] = {kind: set() for kind in KINDS}
    for kind in KINDS:
        live = [reference for reference in parse.live if reference.kind == kind]
        commented = [reference for reference in parse.commented if reference.kind == kind]
        live_names[kind] = {reference.name for reference in live}
        live_folded[kind] = {reference.name.casefold() for reference in live}
        commented_names[kind] = {reference.name for reference in commented}
        census[kind] = {
            "liveReferences": len(live),
            "liveDistinct": len(live_names[kind]),
            "liveFiles": len({reference.virtual_path for reference in live}),
            "commentedReferences": len(commented),
            "commentedDistinct": len(commented_names[kind]),
            "commentedNames": sorted(commented_names[kind]),
        }

    # --- dangling and orphans ---------------------------------------------
    dangling: dict[str, list[str]] = {}
    dangling_if_commented: dict[str, list[str]] = {}
    orphans: dict[str, list[str]] = {}
    orphans_excluding: dict[str, list[str]] = {}
    for kind in KINDS:
        known = declared[kind]
        dangling[kind] = sorted(
            name for name in live_names[kind] if name.casefold() not in known
        )
        dangling_if_commented[kind] = sorted(
            name
            for name in (live_names[kind] | commented_names[kind])
            if name.casefold() not in known
        )
        orphaned = sorted(
            known[folded] for folded in known if folded not in live_folded[kind]
        )
        orphans[kind] = orphaned
        fallback = DOCUMENTED_FALLBACKS.get(kind)
        orphans_excluding[kind] = [name for name in orphaned if name != fallback]

    unit_types = sorted(
        {record.unit_type for record in parse.objects.values() if record.unit_type}
    )

    # --- the Angmar finding -------------------------------------------------
    findings = [
        _angmar_finding(parse, declared, orphans),
        _commented_finding(census, dangling, dangling_if_commented),
    ]

    # --- coverage -----------------------------------------------------------
    coverage = _coverage(parse, inheritance, living_world, living_world_absent_reason)

    gaps = [gap.public() for gap in (*parse.gaps, *inheritance.gaps, *parse.duplicates)]
    gaps.sort(key=lambda row: (row["reason"], row["sourceFile"], row["line"], row["subject"]))

    unresolved = {
        "objectsCarryingAUnitTypeButNoBlock": sorted(unit_type_only),
        "childObjectsWithAnUndeclaredParent": sorted(
            gap["subject"]
            for gap in gaps
            if gap["reason"] == "childobject-parent-not-declared"
        ),
        "inheritanceCycles": sorted(
            gap["subject"] for gap in gaps if gap["reason"] == "childobject-inheritance-cycle"
        ),
        "autoResolveBlocksWithoutARow": sorted(
            f"{gap['subject']} ({gap['sourceFile']}:{gap['line']})"
            for gap in gaps
            if gap["reason"] == "autoresolve-block-without-a-row"
        ),
    }

    manifest: dict[str, Any] = {
        "schema": SCHEMA,
        "schemaVersion": SCHEMA_VERSION,
        "sources": [parse.sources[key].public() for key in sorted(parse.sources)],
        "objects": objects,
        "census": census,
        "dangling": dangling,
        "danglingIfCommentedCounted": dangling_if_commented,
        "orphans": orphans,
        "orphansExcludingDocumentedFallbacks": orphans_excluding,
        "documentedFallbacks": dict(DOCUMENTED_FALLBACKS),
        "unitTypes": unit_types,
        "coverage": coverage,
        "findings": findings,
        "unresolved": unresolved,
        "gaps": gaps,
        "totals": {
            "objectDocuments": len(object_ini_paths(reader)),
            "objectDocumentsWithABinding": len(parse.sources),
            "objectsParsed": len(parse.order),
            "objectsWithADirectAutoResolveField": len(direct_names),
            "objectsWithADirectBlockBinding": len(direct_block_names),
            "objectsBoundAfterInheritance": len(bound_names),
            "objectsWithAutoResolveDataAfterInheritance": len(objects),
            "objectsCarryingAUnitTypeButNoBlock": len(unit_type_only),
            "liveReferences": sum(census[kind]["liveReferences"] for kind in KINDS),
            "commentedReferences": sum(census[kind]["commentedReferences"] for kind in KINDS),
            "dangling": sum(len(dangling[kind]) for kind in KINDS),
            "danglingIfCommentedCounted": sum(
                len(dangling_if_commented[kind]) for kind in KINDS
            ),
            "orphans": sum(len(orphans[kind]) for kind in KINDS),
            "orphansExcludingDocumentedFallbacks": sum(
                len(orphans_excluding[kind]) for kind in KINDS
            ),
            "declaredBlocks": {kind: len(declared[kind]) for kind in KINDS},
            "unitTypes": len(unit_types),
            "gaps": len(gaps),
        },
    }
    return manifest


def _angmar_finding(
    parse: _Parse,
    declared: Mapping[str, Mapping[str, str]],
    orphans: Mapping[str, Sequence[str]],
) -> dict[str, Any]:
    """Angmar's bindings, measured - never asserted from this docstring."""

    angmar_files = sorted(
        {
            reference.virtual_path
            for reference in parse.live
            if "angmar" in reference.virtual_path.rsplit("/", 1)[-1].casefold()
        }
    )
    referenced = sorted(
        {
            reference.name
            for reference in parse.live
            if reference.virtual_path in angmar_files
        }
    )
    angmar_named_referenced = [
        name for name in referenced if "angmar" in name.casefold()
    ]
    foreign = [name for name in referenced if "angmar" not in name.casefold()]
    declared_angmar: dict[str, list[str]] = {}
    orphaned_angmar: dict[str, list[str]] = {}
    for kind in KINDS:
        declared_angmar[kind] = sorted(
            name for name in declared[kind].values() if "angmar" in name.casefold()
        )
        orphaned_angmar[kind] = sorted(
            name for name in orphans[kind] if "angmar" in name.casefold()
        )
    fallbacks = sorted(set(DOCUMENTED_FALLBACKS.values()) & set(referenced))
    return {
        "id": "angmar.binds-blocks-authored-for-other-factions",
        "summary": (
            f"{len(angmar_files)} Angmar object documents carry live auto-resolve "
            f"bindings naming {len(referenced)} distinct blocks. "
            f"{len(foreign)} of those blocks are NOT Angmar-named - they are the "
            "Gondor-, Dwarven-, Isengard- and Mordor-authored blocks. Meanwhile "
            f"{sum(len(names) for names in orphaned_angmar.values())} of the "
            f"{sum(len(names) for names in declared_angmar.values())} Angmar-NAMED "
            "blocks the auto-resolve documents declare are referenced by nothing "
            "live. So Angmar auto-resolves on real, live, authored numbers "
            f"({len(fallbacks)} of the documented fallback blocks appear in its "
            "bindings), and the Angmar-named blocks are genuine orphans."
        ),
        "angmarObjectDocumentsWithLiveBindings": angmar_files,
        "blocksReferencedByThoseDocuments": referenced,
        "blocksReferencedThatAreNotAngmarNamed": foreign,
        "blocksReferencedThatAreAngmarNamed": angmar_named_referenced,
        "documentedFallbacksReferenced": fallbacks,
        "angmarNamedBlocksDeclared": declared_angmar,
        "angmarNamedBlocksOrphaned": orphaned_angmar,
    }


def _commented_finding(
    census: Mapping[str, Any],
    dangling: Mapping[str, Sequence[str]],
    dangling_if_commented: Mapping[str, Sequence[str]],
) -> dict[str, Any]:
    """The miscount, stated as the arithmetic between the two dangling lists."""

    difference: dict[str, list[str]] = {}
    for kind in KINDS:
        extra = sorted(set(dangling_if_commented[kind]) - set(dangling[kind]))
        if extra:
            difference[kind] = extra
    return {
        "id": "commented.lines-are-not-references",
        "summary": (
            "Counting commented-out lines as references makes "
            f"{sum(len(names) for names in difference.values())} blocks look "
            "DANGLING that are not. With retail's ';' and '//' comments correctly "
            "excluded, "
            f"{sum(len(dangling[kind]) for kind in KINDS)} references dangle. "
            f"{sum(census[kind]['commentedReferences'] for kind in KINDS)} "
            "commented auto-resolve references were found and excluded."
        ),
        "falseDanglingIfCommentedCounted": difference,
    }


def _coverage(
    parse: _Parse,
    inheritance: _Inheritance,
    living_world: Mapping[str, Any] | None,
    absent_reason: str,
) -> dict[str, Any]:
    if living_world is None:
        return {"present": False, "reason": absent_reason}
    templates, problems = _thing_templates(living_world)
    bound: list[str] = []
    unbound: list[dict[str, str]] = []
    for template in templates:
        name = parse.folded.get(template.casefold())
        if name is None:
            unbound.append({"thingTemplate": template, "reason": "object not declared"})
            continue
        resolved = inheritance.resolve(name)
        if _binds_a_block(resolved):
            bound.append(template)
        else:
            unbound.append(
                {
                    "thingTemplate": template,
                    "reason": "object declared but carries no auto-resolve binding",
                    "object": name,
                    "sourceFile": parse.objects[name].virtual_path,
                }
            )
    return {
        "present": True,
        "thingTemplates": templates,
        "bound": bound,
        "unbound": unbound,
        "problems": problems,
        "totals": {
            "thingTemplates": len(templates),
            "bound": len(bound),
            "unbound": len(unbound),
        },
    }


def build_bundle(
    catalog_path: pathlib.Path | str,
    autoresolve_path: pathlib.Path | str,
    output_path: pathlib.Path | str,
    living_world_path: pathlib.Path | str | None = None,
) -> dict[str, Any]:
    """Read retail's object documents and write the manifest. Returns it."""

    autoresolve_file = pathlib.Path(autoresolve_path)
    if not autoresolve_file.is_file():
        raise AutoResolveBindingsError(
            f"{autoresolve_file} is not a file; build it with "
            "python -m openbfme_importer.living_world_autoresolve first"
        )
    autoresolve = json.loads(autoresolve_file.read_text(encoding="utf-8"))
    living_world: Mapping[str, Any] | None = None
    absent_reason = "no --living-world document was given"
    if living_world_path is not None:
        living_world_file = pathlib.Path(living_world_path)
        if not living_world_file.is_file():
            raise AutoResolveBindingsError(f"{living_world_file} is not a file")
        living_world = json.loads(living_world_file.read_text(encoding="utf-8"))
    manifest = build_from_reader(
        CatalogReader(catalog_path),
        autoresolve,
        living_world,
        living_world_absent_reason=absent_reason,
    )
    output = pathlib.Path(output_path)
    if output.suffix.casefold() != ".json":
        output = output / MANIFEST_NAME
    write_json_atomic(output, manifest)
    return manifest


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        prog="python -m openbfme_importer.living_world_autoresolve_bindings",
        description=(
            "Convert the unit-to-block bindings retail authors in its object "
            "documents for the War of the Ring auto-resolve system."
        ),
    )
    parser.add_argument("--catalog", required=True, type=pathlib.Path)
    parser.add_argument(
        "--autoresolve",
        required=True,
        type=pathlib.Path,
        help="living-world-autoresolve.json, from living_world_autoresolve",
    )
    parser.add_argument(
        "--out",
        required=True,
        type=pathlib.Path,
        help=f"a directory, or a path ending in .json ({MANIFEST_NAME} by default)",
    )
    parser.add_argument(
        "--living-world",
        type=pathlib.Path,
        default=None,
        help="a converted living-world document, for the strategic-coverage section",
    )
    parser.add_argument("--json", action="store_true", help="machine-readable output")
    args = parser.parse_args(argv)
    manifest = build_bundle(args.catalog, args.autoresolve, args.out, args.living_world)
    totals = manifest["totals"]
    if args.json:
        print(
            json.dumps(
                {
                    "out": str(args.out),
                    "schema": manifest["schema"],
                    "schemaVersion": manifest["schemaVersion"],
                    "totals": totals,
                    "coverage": manifest["coverage"].get("totals")
                    or {"present": False, "reason": manifest["coverage"].get("reason")},
                    "findings": [entry["id"] for entry in manifest["findings"]],
                },
                indent=2,
                sort_keys=True,
            )
        )
        return 0
    print(
        "wrote %s - %d object documents, %d objects parsed, %d bound after "
        "inheritance (%d bind directly)"
        % (
            args.out,
            totals["objectDocuments"],
            totals["objectsParsed"],
            totals["objectsBoundAfterInheritance"],
            totals["objectsWithADirectAutoResolveField"],
        )
    )
    for kind in KINDS:
        row = manifest["census"][kind]
        print(
            "  %-13s live %4d refs / %3d distinct / %2d files; commented %2d refs "
            "/ %2d distinct; dangling %d (%d if commented counted); orphans %d "
            "of %d (%d excluding the documented fallback)"
            % (
                kind,
                row["liveReferences"],
                row["liveDistinct"],
                row["liveFiles"],
                row["commentedReferences"],
                row["commentedDistinct"],
                len(manifest["dangling"][kind]),
                len(manifest["danglingIfCommentedCounted"][kind]),
                len(manifest["orphans"][kind]),
                totals["declaredBlocks"][kind],
                len(manifest["orphansExcludingDocumentedFallbacks"][kind]),
            )
        )
    for kind in KINDS:
        for name in manifest["dangling"][kind]:
            print("  DANGLING %s %s - no auto-resolve document declares it" % (kind, name))
    coverage = manifest["coverage"]
    if coverage.get("present"):
        print(
            "  coverage: %d thingTemplates, %d bound, %d unbound"
            % (
                coverage["totals"]["thingTemplates"],
                coverage["totals"]["bound"],
                coverage["totals"]["unbound"],
            )
        )
        for entry in coverage["unbound"]:
            print("  UNBOUND %s: %s" % (entry["thingTemplate"], entry["reason"]))
    else:
        print("  coverage: ABSENT - %s" % coverage.get("reason"))
    for entry in manifest["findings"]:
        print("  FINDING %s: %s" % (entry["id"], entry["summary"]))
    for reason, names in sorted(manifest["unresolved"].items()):
        for name in names:
            print("  UNRESOLVED %s: %s" % (reason, name))
    return 0


if __name__ == "__main__":  # pragma: no cover - CLI entry point
    raise SystemExit(main())
