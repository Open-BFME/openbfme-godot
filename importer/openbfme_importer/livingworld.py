"""Fail-closed profile of the War-of-the-Ring (Living World) strategic data.

``strategic_census`` proves *how many* strategic blocks a retail install ships.
This module reads the same family one level deeper and produces the actual
strategic **data layer**: the region graph and its connections, the territory
(``ConcurrentRegionBonus``) groupings, the city spawns, the per-faction starting
armies and hero armies, the strategic->tactical RTS handoff settings, and the
command-point economy carried by ``LivingWorldPlayerTemplate``.

Design rules, matching the rest of the importer:

* **No second parsing approach.**  Byte decoding and comment stripping come from
  :mod:`sage_ini`, and the one genuinely flat family here
  (``LivingWorldPlayerTemplate``) is read with ``parse_flat_named_blocks``.  The
  Living World documents are *nested* (``Region`` -> ``Connection`` ->
  ``DetourPoint``) and preprocessor-driven (``#include``), which no existing
  reader models, so this module adds exactly one bounded nested reader — used
  only where the flat reader provably cannot reach.
* **Fail-closed on structure.**  Unbalanced ``End``, an unresolvable or cyclic
  ``#include``, a block nested deeper than :data:`MAX_BLOCK_DEPTH`, or byte and
  document budgets being exceeded all raise.  Nothing is guessed.
* **Unknown content is recorded, never dropped.**  Every field or block outside
  the measured vocabulary lands in ``gaps`` with the document it came from, the
  authored spelling, and a reason.  A gap is evidence, not a silent fallback.
* **No retail payload leaves the private workspace.**  Reports carry authored
  identifiers (region ids, template names) and measured numbers only; document
  provenance is a virtual path plus a SHA-256, exactly like the other censuses.
* **Integers where the consumer needs determinism.**  The Godot strategic layer
  hashes this data, so scaled decimals (revive multipliers, effect timings, zoom)
  are emitted as exact ``*Milli`` integers rather than floats.

Deliberately out of scope, and therefore recorded as gaps rather than modelled:
the tutorial's scripted-campaign surface (``Phase`` / ``Session`` /
``TaskAfterAudio``), which needs the mission-script VM that does not exist yet.
"""

from __future__ import annotations

from dataclasses import dataclass
from decimal import Decimal, InvalidOperation
from pathlib import Path
from typing import Any, Callable, Iterable, Mapping
import json

from .catalog import CatalogEntry, InstallCatalog
from .faction_census import _read_document
from .game import retail_game
from .sage_ini import (
    MAX_INI_BYTES,
    _decode as _ini_decode,
    _strip_comments as _ini_strip_comments,
    parse_flat_named_blocks,
)


SCHEMA = "openbfme.living-world"
SCHEMA_VERSION = 1

# Where a content pack ships the strategic document.  The Godot strategic
# layer (`game/src/wotr/wotr_world.gd`) consumes this schema unchanged, so the
# path is a contract shared with every pack profile that declares the
# ``living-world`` converter.
LIVING_WORLD_PACK_PATH = "data/living-world.json"

# Entry points.  ``riskcampaign.ini`` is the document the engine actually reads
# for War of the Ring: it defines ``LivingWorldRegionCampaign DefaultCampaign``
# and pulls the shared region graph, army rosters and every WOTR scenario in by
# ``#include``.  ``livingworldregions.ini`` is the older Risk-style
# Good/Evil campaign map (``RegionCampain``) and is profiled alongside it so a
# missing WOTR graph can never be masked by the legacy one.
RISK_CAMPAIGN_PATH = "data/ini/campaigns/riskcampaign.ini"
LEGACY_REGIONS_PATH = "data/ini/livingworldregions.ini"
REGION_EFFECTS_PATH = "data/ini/livingworldregioneffects.ini"
PLAYER_TEMPLATE_PATH = "data/ini/livingworldplayers.ini"
CITIES_PATH = "data/ini/campaigns/common/livingworldcities.inc"
RTS_SETTINGS_PATH = "data/ini/campaigns/common/livingworlddefaultrtssettings.inc"

ENTRY_POINTS = (
    RISK_CAMPAIGN_PATH,
    LEGACY_REGIONS_PATH,
    REGION_EFFECTS_PATH,
    PLAYER_TEMPLATE_PATH,
    CITIES_PATH,
    RTS_SETTINGS_PATH,
)

# Budgets.  The measured BFME2 1.06 surface is ~55 documents / ~1.1 MB once
# every ``#include`` is expanded; these leave headroom for mods without letting
# a hostile install fan out unboundedly.
MAX_INCLUDE_DEPTH = 8
MAX_DOCUMENTS = 256
MAX_TOTAL_BYTES = 16 * 1024 * 1024
MAX_LINES = 400_000
MAX_BLOCK_DEPTH = 12
MAX_NODES = 100_000


# ---------------------------------------------------------------------------
# bounded nested reader
# ---------------------------------------------------------------------------


@dataclass(frozen=True, slots=True)
class _Line:
    text: str
    virtual_path: str
    number: int


@dataclass(frozen=True, slots=True)
class Node:
    """One authored block: its spelling, optional name, fields and children."""

    kind: str
    name: str | None
    virtual_path: str
    line: int
    fields: tuple[tuple[str, str], ...]
    children: tuple["Node", ...]

    def value(self, key: str) -> str | None:
        """Return the single value for ``key``; raise when authored twice."""

        found = self.values(key)
        if len(found) > 1:
            raise ValueError(
                f"{self.virtual_path}:{self.line}: {self.kind} repeats field {key}"
            )
        return found[0] if found else None

    def values(self, key: str) -> tuple[str, ...]:
        folded = key.casefold()
        return tuple(value for field, value in self.fields if field.casefold() == folded)

    def blocks(self, kind: str) -> tuple["Node", ...]:
        folded = kind.casefold()
        return tuple(child for child in self.children if child.kind.casefold() == folded)


@dataclass(frozen=True, slots=True)
class Gap:
    """One thing the profile saw and refused to guess at."""

    virtual_path: str
    line: int
    scope: str
    reason: str
    detail: str

    def public(self) -> dict[str, Any]:
        return {
            "virtualPath": self.virtual_path,
            "line": self.line,
            "scope": self.scope,
            "reason": self.reason,
            "detail": self.detail,
        }


def _sorted_gaps(gaps: Iterable[Gap]) -> list[dict[str, Any]]:
    rows = [gap.public() for gap in gaps]
    rows.sort(
        key=lambda row: (
            str(row["virtualPath"]).casefold(),
            int(row["line"]),
            str(row["scope"]).casefold(),
            str(row["reason"]),
            str(row["detail"]).casefold(),
        )
    )
    return rows


def _resolve_include(base_path: str, raw: str) -> str:
    """Resolve a ``#include`` target relative to the including document."""

    target = raw.strip().strip('"').replace("\\", "/").strip()
    if not target:
        raise ValueError(f"empty #include in {base_path}")
    if target.startswith("/") or ":" in target:
        raise ValueError(f"{base_path}: unsupported absolute #include {raw!r}")
    parts = base_path.split("/")[:-1]
    for piece in target.split("/"):
        if piece in {"", "."}:
            continue
        if piece == "..":
            if not parts:
                raise ValueError(f"{base_path}: #include escapes the tree: {raw!r}")
            parts.pop()
            continue
        parts.append(piece)
    return "/".join(parts).casefold()


@dataclass(frozen=True, slots=True)
class Document:
    """One source document: its own significant lines plus its includes."""

    virtual_path: str
    items: tuple[Any, ...]  # _Line | Document, in authored order


def expand_document(
    virtual_path: str,
    read: Callable[[str], bytes],
    *,
    _stack: tuple[str, ...] = (),
    _budget: dict[str, int] | None = None,
) -> Document:
    """Read a document and every ``#include`` it pulls in, preserving structure.

    ``read`` maps a virtual path to bytes and must raise when the document is
    absent — an unresolvable include is a structural failure, not a gap, because
    silently continuing would under-report the region graph.  The include tree
    is kept intact rather than flattened so :func:`flatten_document` can
    quarantine one malformed document without corrupting its neighbours.
    """

    budget = _budget if _budget is not None else {"documents": 0, "bytes": 0, "lines": 0}
    folded = virtual_path.casefold()
    if folded in _stack:
        raise ValueError(f"cyclic #include: {' -> '.join((*_stack, folded))}")
    if len(_stack) >= MAX_INCLUDE_DEPTH:
        raise ValueError(f"#include nested deeper than {MAX_INCLUDE_DEPTH}: {folded}")
    budget["documents"] += 1
    if budget["documents"] > MAX_DOCUMENTS:
        raise ValueError("living world document count exceeds limit")

    source = read(virtual_path)
    budget["bytes"] += len(source)
    if budget["bytes"] > MAX_TOTAL_BYTES:
        raise ValueError("living world document bytes exceed limit")

    items: list[Any] = []
    for number, raw in enumerate(_ini_decode(source).splitlines(), start=1):
        text = _ini_strip_comments(raw)
        if not text:
            continue
        if text.casefold().startswith("#include"):
            target = _resolve_include(folded, text[len("#include") :])
            items.append(
                expand_document(target, read, _stack=(*_stack, folded), _budget=budget)
            )
            continue
        budget["lines"] += 1
        if budget["lines"] > MAX_LINES:
            raise ValueError("living world line count exceeds limit")
        items.append(_Line(text=text, virtual_path=folded, number=number))
    return Document(virtual_path=folded, items=tuple(items))


# Line classifications shared by the balance check and the tree reader, so the
# two can never disagree about what opens a block.
_END = "end"
_PREPROCESSOR = "preprocessor"
_ASSIGNMENT = "assignment"
_PAIR = "pair"
_OPENER = "opener"


def _classify(text: str, openers: frozenset[str], whitespace_pairs: bool) -> str:
    if text.casefold() == "end":
        return _END
    if text.startswith("#"):
        return _PREPROCESSOR
    # Assignments are recognised before block headers: retail reuses block
    # spellings as field names (``Region = Arnor`` inside ``Connection``), and
    # only the ``=`` tells the two apart.
    if "=" in text:
        return _ASSIGNMENT
    tokens = text.split()
    if whitespace_pairs and len(tokens) == 2 and tokens[0].casefold() not in openers:
        return _PAIR
    return _OPENER


def flatten_document(
    document: Document,
    *,
    openers: frozenset[str],
    whitespace_pairs: bool,
    gaps: list[Gap],
) -> list[_Line]:
    """Flatten an include tree, quarantining any document that does not balance.

    Every Living World document is self-contained: an ``#include`` is spliced in
    at a point where the *including* document is mid-block, but the included
    document opens and closes every block it mentions.  A document whose own
    ``End`` count does not balance — RotWK 2.01 ships one, see the module tests —
    would otherwise silently reparent every later block in the whole include
    tree.  Such a document is dropped **as a whole subtree** and recorded as an
    ``unbalanced-document`` gap naming the file, so the loss is visible and
    bounded instead of turning into a plausible-looking wrong graph.
    """

    local: list[_Line] = []
    depth = 0
    lowest = 0
    for item in document.items:
        if isinstance(item, Document):
            local.extend(
                flatten_document(
                    item,
                    openers=openers,
                    whitespace_pairs=whitespace_pairs,
                    gaps=gaps,
                )
            )
            continue
        local.append(item)
        kind = _classify(item.text, openers, whitespace_pairs)
        if kind == _END:
            depth -= 1
            lowest = min(lowest, depth)
        elif kind == _OPENER:
            depth += 1
    if depth != 0 or lowest < 0:
        gaps.append(
            Gap(
                virtual_path=document.virtual_path,
                line=0,
                scope="<document>",
                reason="unbalanced-document",
                detail=f"net block depth {depth}, lowest {lowest}",
            )
        )
        return []
    return local


@dataclass(frozen=True, slots=True)
class Tree:
    """A parsed dialect: top-level blocks, bare top-level fields, and gaps."""

    roots: tuple[Node, ...]
    fields: tuple[tuple[str, str], ...]
    gaps: tuple[Gap, ...]


def read_tree(
    lines: Iterable[_Line],
    *,
    openers: frozenset[str],
    whitespace_pairs: bool = False,
) -> Tree:
    """Build the block tree for one Living World dialect.

    In this grammar every data line is an ``=`` assignment, so any other line
    opens a block that ``End`` closes.  The reader therefore tracks depth for
    blocks it has never heard of rather than skipping them — skipping would
    desynchronise ``End`` and silently truncate the region graph.  Deciding what
    an unrecognised block *means* is the caller's job: callers gap every child
    they do not model, so nothing is dropped.

    ``openers`` is the declared block vocabulary of the dialect.  It exists to
    disambiguate ``whitespace_pairs``, the ``LivingWorldRegionEffects`` dialect
    in which ``Geometry LMR_Fill`` is a key/value pair rather than a block.
    """

    roots: list[Node] = []
    root_fields: list[tuple[str, str]] = []
    # frame = (kind, name, virtual_path, line, fields, children)
    stack: list[tuple[str, str | None, str, int, list[tuple[str, str]], list[Node]]] = []
    gaps: list[Gap] = []
    nodes = 0

    def scope() -> str:
        return stack[-1][0] if stack else "<root>"

    for line in lines:
        text = line.text
        classification = _classify(text, openers, whitespace_pairs)
        if classification == _END:
            if not stack:
                raise ValueError(
                    f"{line.virtual_path}:{line.number}: End without an open block"
                )
            kind, name, path, opened, fields, children = stack.pop()
            node = Node(
                kind=kind,
                name=name,
                virtual_path=path,
                line=opened,
                fields=tuple(fields),
                children=tuple(children),
            )
            (stack[-1][5] if stack else roots).append(node)
            continue
        if classification == _PREPROCESSOR:
            gaps.append(
                Gap(
                    virtual_path=line.virtual_path,
                    line=line.number,
                    scope=scope(),
                    reason="unsupported-preprocessor",
                    detail=" ".join(text.split()[:2]),
                )
            )
            continue
        if classification == _ASSIGNMENT:
            key, _, value = text.partition("=")
            key = key.strip()
            value = value.strip()
            if not key:
                gaps.append(
                    Gap(
                        virtual_path=line.virtual_path,
                        line=line.number,
                        scope=scope(),
                        reason="unnamed-assignment",
                        detail=text,
                    )
                )
                continue
            # Retail authors bare top-level assignments in the ``.inc`` files a
            # campaign includes (the RTS-settings block), so these are returned
            # as the tree's own fields rather than treated as stray.
            (stack[-1][4] if stack else root_fields).append((key, value))
            continue
        tokens = text.split()
        if classification == _PAIR:
            (stack[-1][4] if stack else root_fields).append((tokens[0], tokens[1]))
            continue
        if len(tokens) > 2:
            gaps.append(
                Gap(
                    virtual_path=line.virtual_path,
                    line=line.number,
                    scope=scope(),
                    reason="unsupported-block-header",
                    detail=text,
                )
            )
        if len(stack) >= MAX_BLOCK_DEPTH:
            raise ValueError(
                f"{line.virtual_path}:{line.number}: block nesting exceeds limit"
            )
        nodes += 1
        if nodes > MAX_NODES:
            raise ValueError("living world block count exceeds limit")
        stack.append(
            (
                tokens[0],
                tokens[1] if len(tokens) == 2 else None,
                line.virtual_path,
                line.number,
                [],
                [],
            )
        )

    if stack:
        kind, _name, path, opened, _fields, _children = stack[-1]
        raise ValueError(f"{path}:{opened}: unterminated {kind} block")
    return Tree(roots=tuple(roots), fields=tuple(root_fields), gaps=tuple(gaps))


# ---------------------------------------------------------------------------
# value helpers
# ---------------------------------------------------------------------------


_TRUE = {"yes", "true"}
_FALSE = {"no", "false"}


def _unquote(value: str) -> str:
    if len(value) >= 2 and value[0] == '"' and value[-1] == '"':
        return value[1:-1]
    return value


def _as_int(value: str) -> int:
    text = value.strip()
    try:
        return int(text, 10)
    except ValueError as exc:
        raise ValueError(f"expected an integer, got {value!r}") from exc


def _as_milli(value: str) -> int:
    """Return a decimal scaled by 1000 as an exact integer.

    Living World authors a handful of decimals (revive multipliers, effect
    timings, camera zoom).  The Godot strategic layer hashes this data, so the
    profile refuses to emit binary floats: anything finer than a thousandth is a
    parse error rather than a rounded guess.
    """

    try:
        scaled = Decimal(value.strip()) * 1000
    except InvalidOperation as exc:
        raise ValueError(f"expected a decimal, got {value!r}") from exc
    if scaled != scaled.to_integral_value():
        raise ValueError(f"decimal {value!r} is finer than one thousandth")
    return int(scaled)


def _as_bool(value: str) -> bool:
    folded = value.strip().casefold()
    if folded in _TRUE:
        return True
    if folded in _FALSE:
        return False
    raise ValueError(f"expected Yes/No, got {value!r}")


def _as_axes(value: str, axes: str) -> dict[str, int]:
    """Parse retail's ``X:-505 Y:1860`` / ``R:245 G:245 B:245`` tuples."""

    tokens = value.split()
    if len(tokens) != len(axes):
        raise ValueError(f"expected {len(axes)} components, got {value!r}")
    parsed: dict[str, int] = {}
    for axis, token in zip(axes, tokens):
        prefix, separator, number = token.partition(":")
        if separator != ":" or prefix.casefold() != axis.casefold():
            raise ValueError(f"expected {axis}:<int>, got {token!r}")
        parsed[axis.casefold()] = _as_int(number)
    return parsed


def _point(value: str) -> dict[str, int]:
    return _as_axes(value, "XY")


def _color(value: str) -> dict[str, int]:
    return _as_axes(value, "RGB")


def _names(value: str) -> list[str]:
    return value.split()


# ---------------------------------------------------------------------------
# vocabularies (measured against BFME2 1.06 / RotWK 2.01)
# ---------------------------------------------------------------------------

_CAMPAIGN_OPENERS = frozenset(
    {
        "livingworldregioncampaign",
        "regioncampain",  # retail's own spelling in livingworldregions.ini
        "region",
        "connection",
        "restrictbuildings",
        "concurrentregionbonus",
        "livingworldcampaign",
        "scenario",
        "act",
        "ownershipset",
        "spawnarmies",
        "spawnarmy",
        "eyetowerpoints",
        "playerdefeatcondition",
        "teamdefeatcondition",
        "teamvictorycondition",
        "livingworldplayerarmy",
        "armyentry",
    }
)

_EFFECT_OPENERS = frozenset(
    {
        "livingworldregioneffects",
        "borderseffect",
        "filledownershipeffect",
        "mouseovereffectflareup",
        "mouseouteffectflareup",
        "homeregionhighlight",
        "regionselectioneffect",
        "unifiedeffect",
        "colorintensitycontrolpoint",
    }
)

_ARMY_OPENERS = frozenset({"livingworldplayerarmy", "armyentry", "spawnarmy"})

# Region and territory bonuses share one vocabulary: BFME2 ships the first six,
# RotWK 2.01 adds the rest (``DiscountedSeigeUnitsBonus`` is retail's spelling).
_REGION_BONUS_FIELDS = {
    "armybonus": "army",
    "resourcebonus": "resource",
    "legendarybonus": "legendary",
    "attackbonus": "attack",
    "defensebonus": "defense",
    "experiencebonus": "experience",
    "fertileterritorybonus": "fertileTerritory",
    "buildingdiscountbonus": "buildingDiscount",
    "extrastartresourcesbonus": "extraStartResources",
    "discountedherounitsbonus": "discountedHeroUnits",
    "discountedbarracksunitsbonus": "discountedBarracksUnits",
    "discountedseigeunitsbonus": "discountedSiegeUnits",
    "freebuilderbonus": "freeBuilder",
    "freeinnunitsbonus": "freeInnUnits",
}

_EMPTY_BONUSES = {name: 0 for name in sorted(set(_REGION_BONUS_FIELDS.values()))}


def _read_bonus(
    node: Node,
    key: str,
    value: str,
    bonuses: dict[str, int],
    macros: dict[str, str],
    gaps: list[Gap],
) -> None:
    """Record one bonus, keeping unexpanded ``#define`` macros as evidence.

    RotWK authors ``FertileTerritoryBonus = FERTILE_TERRITORY_BONUS``, a macro
    defined in ``gamedata.ini`` — outside this profile's include tree.  Rather
    than expanding a macro it never read (or dropping the field), the profile
    keeps the authored token in ``bonusMacros`` and records a gap.
    """

    name = _REGION_BONUS_FIELDS[key.casefold()]
    try:
        bonuses[name] = _as_int(value)
    except ValueError:
        macros[name] = value
        gaps.append(_field_gap(node, f"{key} = {value}", "unexpanded-macro"))

_RTS_SETTING_FIELDS = {
    "secondsperreinforcement": ("secondsPerReinforcement", _as_int),
    "startingcashrts": ("startingCashRts", _as_int),
    "startingcashrtswithfort": ("startingCashRtsWithFort", _as_int),
    "initialrevivalcostmultiplier": ("initialRevivalCostMilli", _as_milli),
    "initialrevivaltimemultiplier": ("initialRevivalTimeMilli", _as_milli),
}

_PLAYER_TEMPLATE_INT_FIELDS = {
    "startingworldcp": "startingWorldCp",
    "maxworldcp": "maxWorldCp",
    "startingherocp": "startingHeroCp",
    "maxherocp": "maxHeroCp",
    "scenariostartresources": "scenarioStartResources",
}

_PLAYER_TEMPLATE_TEXT_FIELDS = {
    "faction": "faction",
    "factionicon": "factionIcon",
    "defaultarmyiconname": "defaultArmyIconName",
    "buildploticonname": "buildPlotIconName",
    "buildplotselectionportraitname": "buildPlotSelectionPortraitName",
    "garrisonselectionportraitname": "garrisonSelectionPortraitName",
    "garrisondisplaynametag": "garrisonDisplayNameTag",
    "music": "music",
    "autoresolveloop": "autoResolveLoop",
    "factiondozertemplatename": "factionDozerTemplateName",
    "factioninnunittemplatename": "factionInnUnitTemplateName",
}


# ---------------------------------------------------------------------------
# block readers
# ---------------------------------------------------------------------------


def _field_gap(node: Node, key: str, reason: str = "unknown-field") -> Gap:
    return Gap(
        virtual_path=node.virtual_path,
        line=node.line,
        scope=node.kind,
        reason=reason,
        detail=key,
    )


def _block_gap(node: Node, child: Node, reason: str = "unmodelled-block") -> Gap:
    return Gap(
        virtual_path=child.virtual_path,
        line=child.line,
        scope=node.kind,
        reason=reason,
        detail=child.kind if child.name is None else f"{child.kind} {child.name}",
    )


def _read_region(node: Node, gaps: list[Gap]) -> dict[str, Any]:
    if not node.name:
        raise ValueError(f"{node.virtual_path}:{node.line}: Region has no id")
    region: dict[str, Any] = {
        "id": node.name,
        "displayName": "",
        "mapName": "",
        "subObject": "",
        "regionPortrait": "",
        "conqueredNotice": "",
        "skirmishStillImage": "",
        "skirmishMusicTrack": "",
        "skirmishVoiceTrack": "",
        "movieNameFirstTime": "",
        "movieNameRepeat": "",
        "bonuses": dict(_EMPTY_BONUSES),
        "bonusMacros": {},
        "cpLimit": -1,
        "allyCpLimit": -1,
        "createAutoFort": False,
        "customCenterPoint": False,
        "centerPoint": None,
        "heroArmySpots": [],
        "garrisonArmySpots": [],
        "buildingSpots": [],
        "fortress": None,
        "connections": [],
        "restrictBuildings": [],
    }
    text_fields = {
        "displayname": "displayName",
        "mapname": "mapName",
        "subobject": "subObject",
        "regionportrait": "regionPortrait",
        "conquerednotice": "conqueredNotice",
        "skirmishstillimage": "skirmishStillImage",
        "skirmishmusictrack": "skirmishMusicTrack",
        "skirmishvoicetrack": "skirmishVoiceTrack",
        "movienamefirsttime": "movieNameFirstTime",
        "movienamerepeat": "movieNameRepeat",
    }
    fortress = {"displayName": "", "displayDescription": "", "portrait": ""}
    fortress_fields = {
        "fortressdisplayname": "displayName",
        "fortressdisplaydescription": "displayDescription",
        "fortressportrait": "portrait",
    }
    spot_fields = {
        "heroarmyspot": "heroArmySpots",
        "garrisonarmyspot": "garrisonArmySpots",
        "buildingspot": "buildingSpots",
    }
    saw_fortress = False
    for key, value in node.fields:
        folded = key.casefold()
        if folded in text_fields:
            region[text_fields[folded]] = _unquote(value)
        elif folded in _REGION_BONUS_FIELDS:
            _read_bonus(node, key, value, region["bonuses"], region["bonusMacros"], gaps)
        elif folded in fortress_fields:
            saw_fortress = True
            fortress[fortress_fields[folded]] = _unquote(value)
        elif folded in spot_fields:
            region[spot_fields[folded]].append(_point(value))
        elif folded == "cplimit":
            region["cpLimit"] = _as_int(value)
        elif folded == "allycplimit":
            region["allyCpLimit"] = _as_int(value)
        elif folded == "createautofort":
            region["createAutoFort"] = _as_bool(value)
        elif folded == "customcenterpoint":
            region["customCenterPoint"] = _as_bool(value)
        elif folded == "centerpoint":
            region["centerPoint"] = _point(value)
        elif folded == "connectsto":
            # Retail authors a bare ``ConnectsTo =`` and then a run of
            # ``Connection`` blocks; the assignment itself carries no value.
            if value:
                gaps.append(_field_gap(node, key, "unsupported-connectsto-value"))
        else:
            gaps.append(_field_gap(node, key))
    if saw_fortress:
        region["fortress"] = fortress

    for child in node.children:
        folded = child.kind.casefold()
        if folded == "connection":
            target = child.value("Region")
            if not target:
                gaps.append(_block_gap(node, child, "connection-without-region"))
                continue
            detours = [_point(value) for value in child.values("DetourPoint")]
            for key, _value in child.fields:
                if key.casefold() not in {"region", "detourpoint"}:
                    gaps.append(_field_gap(child, key))
            region["connections"].append({"region": target, "detourPoints": detours})
        elif folded == "restrictbuildings":
            buildings: list[str] = []
            for value in child.values("Buildings"):
                buildings.extend(_names(value))
            allowed = child.value("NumberAllowed")
            for key, _value in child.fields:
                if key.casefold() not in {"buildings", "numberallowed"}:
                    gaps.append(_field_gap(child, key))
            region["restrictBuildings"].append(
                {
                    "buildings": sorted(buildings, key=str.casefold),
                    "numberAllowed": _as_int(allowed) if allowed else -1,
                }
            )
        else:
            gaps.append(_block_gap(node, child))

    region["connections"].sort(key=lambda row: str(row["region"]).casefold())
    region["restrictBuildings"].sort(key=lambda row: tuple(row["buildings"]))
    return region


def _read_territory_bonus(node: Node, gaps: list[Gap]) -> dict[str, Any]:
    bonus: dict[str, Any] = {
        "territory": "",
        "effectName": "",
        "regions": [],
        "bonuses": dict(_EMPTY_BONUSES),
        "bonusMacros": {},
        "unifiedEvaEvent": "",
        "lostEvaEvent": "",
        "lookAtCenter": None,
        "lookAtHeading": 0,
        "lookAtZoomMilli": 0,
    }
    for key, value in node.fields:
        folded = key.casefold()
        if folded == "territory":
            bonus["territory"] = _unquote(value)
        elif folded == "effectname":
            bonus["effectName"] = _unquote(value)
        elif folded == "regions":
            bonus["regions"].extend(_names(value))
        elif folded in _REGION_BONUS_FIELDS:
            _read_bonus(node, key, value, bonus["bonuses"], bonus["bonusMacros"], gaps)
        elif folded == "unifiedevaevent":
            bonus["unifiedEvaEvent"] = _unquote(value)
        elif folded == "lostevaevent":
            bonus["lostEvaEvent"] = _unquote(value)
        elif folded == "lookatcenter":
            bonus["lookAtCenter"] = _point(value)
        elif folded == "lookatheading":
            bonus["lookAtHeading"] = _as_milli(value)
        elif folded == "lookatzoom":
            bonus["lookAtZoomMilli"] = _as_milli(value)
        else:
            gaps.append(_field_gap(node, key))
    for child in node.children:
        gaps.append(_block_gap(node, child))
    bonus["regions"].sort(key=str.casefold)
    return bonus


def _read_spawn_army(node: Node, gaps: list[Gap]) -> dict[str, Any]:
    army: dict[str, Any] = {
        "scriptingName": "",
        "spawnForTemplates": [],
        "heroTemplateName": "",
        "playerArmy": "",
        "icon": "",
        "banner": "",
        "palantirMovie": "",
        "initialRegion": "",
        "moveSpeedMilli": -1,
        "spawnAtActStart": True,
        "isCity": False,
        "position": None,
    }
    for key, value in node.fields:
        folded = key.casefold()
        if folded == "scriptingname":
            army["scriptingName"] = _unquote(value)
        elif folded == "spawnfortemplates":
            army["spawnForTemplates"].extend(_names(value))
        elif folded == "herotemplatename":
            army["heroTemplateName"] = _unquote(value)
        elif folded == "playerarmy":
            army["playerArmy"] = _unquote(value)
        elif folded == "icon":
            army["icon"] = _unquote(value)
        elif folded == "banner":
            army["banner"] = _unquote(value)
        elif folded == "palantirmovie":
            army["palantirMovie"] = _unquote(value)
        elif folded == "initialregion":
            army["initialRegion"] = _unquote(value)
        elif folded == "movespeed":
            army["moveSpeedMilli"] = _as_milli(value)
        elif folded == "spawnatactstart":
            army["spawnAtActStart"] = _as_bool(value)
        elif folded == "iscity":
            army["isCity"] = _as_bool(value)
        elif folded == "position":
            army["position"] = _point(value)
        elif folded == "name":
            # Cities carry a commented-out ``Name``; an authored one is a real
            # field this profile does not model yet.
            gaps.append(_field_gap(node, key))
        else:
            gaps.append(_field_gap(node, key))
    for child in node.children:
        gaps.append(_block_gap(node, child))
    army["spawnForTemplates"].sort(key=str.casefold)
    return army


def _read_player_army(node: Node, gaps: list[Gap]) -> dict[str, Any]:
    army: dict[str, Any] = {"name": "", "displayNameTag": "", "entries": []}
    for key, value in node.fields:
        folded = key.casefold()
        if folded == "name":
            army["name"] = _unquote(value)
        elif folded == "displaynametag":
            army["displayNameTag"] = _unquote(value)
        else:
            gaps.append(_field_gap(node, key))
    for child in node.children:
        if child.kind.casefold() != "armyentry":
            gaps.append(_block_gap(node, child))
            continue
        template = child.value("ThingTemplate")
        quantity = child.value("Quantity")
        for key, _value in child.fields:
            if key.casefold() not in {"thingtemplate", "quantity"}:
                gaps.append(_field_gap(child, key))
        if not template:
            gaps.append(_block_gap(node, child, "army-entry-without-template"))
            continue
        army["entries"].append(
            {
                "thingTemplate": _unquote(template),
                "quantity": _as_int(quantity) if quantity else 1,
            }
        )
    if not army["name"]:
        raise ValueError(
            f"{node.virtual_path}:{node.line}: LivingWorldPlayerArmy has no Name"
        )
    return army


def _read_scenario(node: Node, gaps: list[Gap]) -> dict[str, Any]:
    """Read the strategic setup of one ``LivingWorldCampaign``.

    The narrative/scripted surface (``Phase`` / ``Session`` / ``TaskAfterAudio``)
    needs the mission-script VM and is recorded as a gap instead of modelled.
    """

    scenario: dict[str, Any] = {
        "name": node.name or "",
        "isEvilCampaign": False,
        "isHistoricalScenario": False,
        "regionCampaign": "",
        "minPlayers": 0,
        "maxPlayers": 0,
        "defaultStartSpots": [],
        "disallowStartInRegions": [],
        "disableRegions": [],
        "disabledFactions": [],
        "startingRestrictions": [],
        "victoryTypes": [],
        "displayTags": {},
        "playerDefeatConditions": [],
        "teamDefeatConditions": [],
        "teamVictoryConditions": [],
        "ownershipSets": [],
        "actArmies": [],
        "rtsSettings": {},
    }
    display_fields = {
        "displayname": "displayName",
        "displaydescription": "displayDescription",
        "displaygametype": "displayGameType",
        "displayobjectives": "displayObjectives",
        "displayfiction": "displayFiction",
        "displayvictorioustext": "displayVictoriousText",
        "displaydefeatedtext": "displayDefeatedText",
    }

    for key, value in node.fields:
        folded = key.casefold()
        if folded == "isevilcampaign":
            scenario["isEvilCampaign"] = _as_bool(value)
        elif folded in _RTS_SETTING_FIELDS:
            name, convert = _RTS_SETTING_FIELDS[folded]
            scenario["rtsSettings"][name] = convert(value)
        else:
            gaps.append(_field_gap(node, key))

    for child in node.children:
        folded = child.kind.casefold()
        if folded == "livingworldvictorytype":
            # Order is load-bearing: RotWK's MPRules indexes victory types by
            # position, so this list is never sorted.
            scenario["victoryTypes"].append(_read_victory_type(child, gaps))
        elif folded == "scenario":
            for key, value in child.fields:
                inner = key.casefold()
                if inner in display_fields:
                    scenario["displayTags"][display_fields[inner]] = _unquote(value)
                elif inner == "regioncampaign":
                    scenario["regionCampaign"] = _unquote(value)
                elif inner == "historicalscenario":
                    scenario["isHistoricalScenario"] = _as_bool(value)
                elif inner == "minplayers":
                    scenario["minPlayers"] = _as_int(value)
                elif inner == "disabledfactions":
                    scenario["disabledFactions"].extend(_names(value))
                elif inner == "maxplayers":
                    scenario["maxPlayers"] = _as_int(value)
                elif inner == "defaultstartspots":
                    scenario["defaultStartSpots"].extend(_names(value))
                elif inner == "disallowstartinregions":
                    scenario["disallowStartInRegions"].extend(_names(value))
                elif inner == "disableregions":
                    scenario["disableRegions"].extend(_names(value))
                else:
                    gaps.append(_field_gap(child, key))
            for inner_child in child.children:
                inner = inner_child.kind.casefold()
                if inner in {
                    "playerdefeatcondition",
                    "teamdefeatcondition",
                    "teamvictorycondition",
                }:
                    scenario[
                        {
                            "playerdefeatcondition": "playerDefeatConditions",
                            "teamdefeatcondition": "teamDefeatConditions",
                            "teamvictorycondition": "teamVictoryConditions",
                        }[inner]
                    ].append(_read_condition(inner_child, gaps))
                elif inner == "ownershipset":
                    scenario["ownershipSets"].append(
                        _read_ownership_set(inner_child, gaps)
                    )
                elif inner == "startingrestriction":
                    scenario["startingRestrictions"].append(
                        _read_starting_restriction(inner_child, gaps)
                    )
                else:
                    gaps.append(_block_gap(child, inner_child))
        elif folded == "act":
            for key, _value in child.fields:
                gaps.append(_field_gap(child, key))
            for inner_child in child.children:
                inner = inner_child.kind.casefold()
                if inner == "spawnarmy":
                    scenario["actArmies"].append(_read_spawn_army(inner_child, gaps))
                elif inner == "ownershipset":
                    scenario["ownershipSets"].append(
                        _read_ownership_set(inner_child, gaps)
                    )
                elif inner == "eyetowerpoints":
                    # Camera fluff for the strategic view: presentation only.
                    gaps.append(_block_gap(child, inner_child, "presentation-only"))
                else:
                    gaps.append(_block_gap(child, inner_child))
        else:
            gaps.append(_block_gap(node, child))

    scenario["defaultStartSpots"].sort(key=str.casefold)
    scenario["disallowStartInRegions"].sort(key=str.casefold)
    scenario["disableRegions"].sort(key=str.casefold)
    scenario["disabledFactions"].sort(key=str.casefold)
    return scenario


def _read_victory_type(node: Node, gaps: list[Gap]) -> dict[str, Any]:
    """Read one shared ``LivingWorldVictoryType`` (RotWK 2.01 and later)."""

    victory: dict[str, Any] = {
        "displayTags": {},
        "playerDefeatConditions": [],
        "teamDefeatConditions": [],
        "teamVictoryConditions": [],
    }
    display_fields = {
        "displaygametype": "displayGameType",
        "displayobjectives": "displayObjectives",
        "displayvictorioustext": "displayVictoriousText",
        "displaydefeatedtext": "displayDefeatedText",
    }
    for key, value in node.fields:
        folded = key.casefold()
        if folded in display_fields:
            victory["displayTags"][display_fields[folded]] = _unquote(value)
        else:
            gaps.append(_field_gap(node, key))
    for child in node.children:
        folded = child.kind.casefold()
        bucket = {
            "playerdefeatcondition": "playerDefeatConditions",
            "teamdefeatcondition": "teamDefeatConditions",
            "teamvictorycondition": "teamVictoryConditions",
        }.get(folded)
        if bucket is None:
            gaps.append(_block_gap(node, child))
            continue
        victory[bucket].append(_read_condition(child, gaps))
    return victory


def _read_starting_restriction(node: Node, gaps: list[Gap]) -> dict[str, Any]:
    """Read one ``StartingRestriction`` (RotWK historical-scenario seating)."""

    restriction: dict[str, Any] = {"factions": [], "regions": [], "teams": []}
    for key, value in node.fields:
        folded = key.casefold()
        if folded == "factions":
            restriction["factions"].extend(_names(value))
        elif folded == "regions":
            restriction["regions"].extend(_names(value))
        elif folded == "teams":
            restriction["teams"].extend(_as_int(token) for token in _names(value))
        else:
            gaps.append(_field_gap(node, key))
    for child in node.children:
        gaps.append(_block_gap(node, child))
    restriction["factions"].sort(key=str.casefold)
    restriction["regions"].sort(key=str.casefold)
    restriction["teams"].sort()
    return restriction


def _read_condition(node: Node, gaps: list[Gap]) -> dict[str, Any]:
    condition: dict[str, Any] = {
        "kind": node.kind,
        "teams": [],
        "loseIfCapitalLost": False,
        "numControlledRegionsLessOrEqualTo": -1,
        "numControlledRegionsGreaterOrEqualTo": -1,
        "controlledRegions": [],
    }
    for key, value in node.fields:
        folded = key.casefold()
        if folded == "teams":
            condition["teams"].extend(_as_int(token) for token in _names(value))
        elif folded == "loseifcapitallost":
            condition["loseIfCapitalLost"] = _as_bool(value)
        elif folded == "numcontrolledregionslessorequalto":
            condition["numControlledRegionsLessOrEqualTo"] = _as_int(value)
        elif folded == "numcontrolledregionsgreaterorequalto":
            condition["numControlledRegionsGreaterOrEqualTo"] = _as_int(value)
        elif folded == "controlledregions":
            condition["controlledRegions"].extend(_names(value))
        else:
            gaps.append(_field_gap(node, key))
    for child in node.children:
        gaps.append(_block_gap(node, child))
    condition["teams"].sort()
    condition["controlledRegions"].sort(key=str.casefold)
    return condition


def _read_ownership_set(node: Node, gaps: list[Gap]) -> dict[str, Any]:
    ownership: dict[str, Any] = {
        "regions": [],
        "startRegion": "",
        "spawnArmies": [],
        "spawnBuildings": [],
    }
    for key, value in node.fields:
        folded = key.casefold()
        if folded == "regions":
            ownership["regions"].extend(_names(value))
        elif folded == "startregion":
            ownership["startRegion"] = _unquote(value)
        else:
            gaps.append(_field_gap(node, key))
    for child in node.children:
        folded_kind = child.kind.casefold()
        if folded_kind not in {"spawnarmies", "spawnbuildings"}:
            gaps.append(_block_gap(node, child))
            continue
        # ``SpawnBuildings`` names buildings through ``#define`` macros
        # (``LW_FORT``) resolved in gamedata.ini, outside this include tree; the
        # authored tokens are kept verbatim and the macros stay a recorded gap.
        list_key = "armies" if folded_kind == "spawnarmies" else "buildings"
        items: list[str] = []
        region = ""
        for key, value in child.fields:
            folded = key.casefold()
            if folded == list_key:
                items.extend(_names(value))
            elif folded == "region":
                region = _unquote(value)
            else:
                gaps.append(_field_gap(child, key))
        for inner_child in child.children:
            gaps.append(_block_gap(child, inner_child))
        row = {list_key: sorted(items, key=str.casefold), "region": region}
        if folded_kind == "spawnarmies":
            ownership["spawnArmies"].append(row)
        else:
            ownership["spawnBuildings"].append(row)
    ownership["regions"].sort(key=str.casefold)
    ownership["spawnArmies"].sort(key=lambda row: str(row["region"]).casefold())
    ownership["spawnBuildings"].sort(key=lambda row: str(row["region"]).casefold())
    return ownership


def _read_region_campaign(node: Node, gaps: list[Gap]) -> dict[str, Any]:
    campaign: dict[str, Any] = {
        "name": node.name or "",
        "kind": node.kind,
        "regionObject": "",
        "regionConqueredSound": "",
        "regionEffectsManagerName": "",
        "regionBonusTags": {},
        "armyIconCommandPoints": {},
        "armyRetreatRounds": -1,
        "armyPlacementOffsets": [],
        "regions": [],
        "territoryBonuses": [],
    }
    bonus_tags = {
        "regionbonusarmy": "army",
        "regionbonusresource": "resource",
        "regionbonuslegendary": "legendary",
    }
    icon_caps = {
        "heroonlyarmycommandpoints": "heroOnly",
        "smallarmycommandpoints": "small",
        "mediumarmycommandpoints": "medium",
    }
    for key, value in node.fields:
        folded = key.casefold()
        if folded == "regionobject":
            campaign["regionObject"] = _unquote(value)
        elif folded == "regionconqueredsound":
            campaign["regionConqueredSound"] = _unquote(value)
        elif folded == "regioneffectsmanagername":
            campaign["regionEffectsManagerName"] = _unquote(value)
        elif folded in bonus_tags:
            campaign["regionBonusTags"][bonus_tags[folded]] = _unquote(value)
        elif folded in icon_caps:
            campaign["armyIconCommandPoints"][icon_caps[folded]] = _as_int(value)
        elif folded == "armyretreatrounds":
            campaign["armyRetreatRounds"] = _as_int(value)
        elif folded == "armyplacementpos":
            campaign["armyPlacementOffsets"].append(_point(value))
        else:
            gaps.append(_field_gap(node, key))
    for child in node.children:
        folded = child.kind.casefold()
        if folded == "region":
            campaign["regions"].append(_read_region(child, gaps))
        elif folded == "concurrentregionbonus":
            campaign["territoryBonuses"].append(_read_territory_bonus(child, gaps))
        else:
            gaps.append(_block_gap(node, child))
    campaign["regions"].sort(key=lambda row: str(row["id"]).casefold())
    campaign["territoryBonuses"].sort(key=lambda row: str(row["effectName"]).casefold())
    return campaign


def _read_region_effects(node: Node, gaps: list[Gap]) -> dict[str, Any]:
    """Read the presentation manifest for the strategic map.

    This document is entirely visual (geometry names, colours, flare-up
    timings).  It is profiled so the strategic layer can *reference* it, but no
    presentation behaviour is derived here.
    """

    effects: dict[str, Any] = {
        "name": node.name or "",
        "regionObject": "",
        "colors": {},
        "sections": [],
    }
    colors = {
        "neutralregioncolor": "neutralRegion",
        "regionbordercolor": "regionBorder",
        "shellstartpositioncolor": "shellStartPosition",
    }
    for key, value in node.fields:
        folded = key.casefold()
        if folded == "regionobject":
            effects["regionObject"] = _unquote(value)
        elif folded in colors:
            effects["colors"][colors[folded]] = _color(value)
        else:
            gaps.append(_field_gap(node, key))
    for child in node.children:
        section: dict[str, Any] = {
            "kind": child.kind,
            "geometry": [],
            "loadInShell": True,
            "controlPoints": [],
        }
        for key, value in child.fields:
            folded = key.casefold()
            if folded == "geometry":
                section["geometry"].append(_unquote(value))
            elif folded == "loadinshell":
                section["loadInShell"] = _as_bool(value)
            else:
                gaps.append(_field_gap(child, key))
        for point in child.children:
            if point.kind.casefold() != "colorintensitycontrolpoint":
                gaps.append(_block_gap(child, point))
                continue
            intensity = point.value("Intensity")
            time = point.value("Time")
            for key, _value in point.fields:
                if key.casefold() not in {"intensity", "time"}:
                    gaps.append(_field_gap(point, key))
            section["controlPoints"].append(
                {
                    "intensityMilli": _as_milli(intensity) if intensity else 0,
                    "timeMilli": _as_milli(time) if time else 0,
                }
            )
        effects["sections"].append(section)
    effects["sections"].sort(key=lambda row: str(row["kind"]).casefold())
    return effects


# ---------------------------------------------------------------------------
# profile
# ---------------------------------------------------------------------------


def _deduplicate(rows: list[dict[str, Any]]) -> list[dict[str, Any]]:
    """Drop byte-identical duplicates while preserving first-seen order.

    The WOTR scenarios all ``#include`` the same army and territory documents,
    so the same authored block legitimately arrives 44 times.  Deduplication is
    by full content, never by name: two blocks that share a name but differ in
    any field are both kept, so a mod that overrides one cannot be silently
    collapsed into the other.
    """

    seen: set[str] = set()
    unique: list[dict[str, Any]] = []
    for row in rows:
        key = json.dumps(row, sort_keys=True, separators=(",", ":"))
        if key in seen:
            continue
        seen.add(key)
        unique.append(row)
    return unique


def _reader(catalog: InstallCatalog, sources: dict[str, dict[str, Any]]):
    """Return a bounded catalog reader that records provenance per document."""

    def read(virtual_path: str) -> bytes:
        document = _read_document(catalog, virtual_path)
        if len(document.source) > MAX_INI_BYTES:
            raise ValueError(f"living world document exceeds byte limit: {virtual_path}")
        sources[document.virtual_path.casefold()] = document.public()
        return document.source

    return read


def profile_living_world(catalog: InstallCatalog, game: str) -> dict[str, Any]:
    """Profile the War-of-the-Ring strategic data surface, fail-closed.

    Returns a schema-versioned document carrying the region graph, territory
    bonuses, city spawns, army rosters, scenario setups, RTS handoff settings
    and the command-point economy — plus ``gaps``, which enumerates every field,
    block and directive the profile refused to guess at.
    """

    retail_game(game)
    sources: dict[str, dict[str, Any]] = {}
    read = _reader(catalog, sources)
    gaps: list[Gap] = []

    def lines_of(
        virtual_path: str,
        *,
        openers: frozenset[str],
        whitespace_pairs: bool = False,
    ) -> list[_Line]:
        """Expand one entry point, or record it as unresolved and continue.

        A *missing entry point* is a gap because a mod install may legitimately
        omit a surface; a missing ``#include`` inside a document that does exist
        stays a hard error, because that is a broken document, not an absent one.
        """

        if catalog.resolve_exact(virtual_path) is None:
            gaps.append(
                Gap(
                    virtual_path=virtual_path,
                    line=0,
                    scope="<entryPoint>",
                    reason="unresolved-file",
                    detail=virtual_path,
                )
            )
            return []
        return flatten_document(
            expand_document(virtual_path, read),
            openers=openers,
            whitespace_pairs=whitespace_pairs,
            gaps=gaps,
        )

    campaign_tree = read_tree(
        [
            *lines_of(RISK_CAMPAIGN_PATH, openers=_CAMPAIGN_OPENERS),
            *lines_of(LEGACY_REGIONS_PATH, openers=_CAMPAIGN_OPENERS),
        ],
        openers=_CAMPAIGN_OPENERS,
    )
    gaps.extend(campaign_tree.gaps)
    for key, _value in campaign_tree.fields:
        gaps.append(
            Gap(
                virtual_path=RISK_CAMPAIGN_PATH,
                line=0,
                scope="<root>",
                reason="unknown-field",
                detail=key,
            )
        )

    region_campaigns: list[dict[str, Any]] = []
    territory_bonuses: list[dict[str, Any]] = []
    scenarios: list[dict[str, Any]] = []
    player_armies: list[dict[str, Any]] = []
    default_armies: list[dict[str, Any]] = []
    for node in campaign_tree.roots:
        folded = node.kind.casefold()
        if folded in {"livingworldregioncampaign", "regioncampain"}:
            region_campaigns.append(_read_region_campaign(node, gaps))
        elif folded == "concurrentregionbonus":
            territory_bonuses.append(_read_territory_bonus(node, gaps))
        elif folded == "livingworldcampaign":
            scenarios.append(_read_scenario(node, gaps))
        elif folded == "livingworldplayerarmy":
            player_armies.append(_read_player_army(node, gaps))
        elif folded == "spawnarmy":
            default_armies.append(_read_spawn_army(node, gaps))
        else:
            gaps.append(
                Gap(
                    virtual_path=node.virtual_path,
                    line=node.line,
                    scope="<root>",
                    reason="unmodelled-block",
                    detail=node.kind if node.name is None else f"{node.kind} {node.name}",
                )
            )

    cities_tree = read_tree(
        lines_of(CITIES_PATH, openers=_ARMY_OPENERS), openers=_ARMY_OPENERS
    )
    gaps.extend(cities_tree.gaps)
    cities: list[dict[str, Any]] = []
    for node in cities_tree.roots:
        if node.kind.casefold() != "spawnarmy":
            gaps.append(
                Gap(
                    virtual_path=node.virtual_path,
                    line=node.line,
                    scope="<cities>",
                    reason="unmodelled-block",
                    detail=node.kind,
                )
            )
            continue
        cities.append(_read_spawn_army(node, gaps))

    # The RTS-settings include is bare assignments in BFME2; RotWK appends
    # ``#include "LivingWorldVictoryTypes.inc"`` to it, so it is read with the
    # tree reader and split into root assignments plus victory-type blocks.
    rts_settings: dict[str, Any] = {}
    victory_types: list[dict[str, Any]] = []
    rts_lines = lines_of(RTS_SETTINGS_PATH, openers=_CAMPAIGN_OPENERS)
    rts_tree = read_tree(rts_lines, openers=_CAMPAIGN_OPENERS)
    gaps.extend(rts_tree.gaps)
    for key, value in rts_tree.fields:
        folded = key.casefold()
        if folded not in _RTS_SETTING_FIELDS:
            gaps.append(
                Gap(
                    virtual_path=RTS_SETTINGS_PATH,
                    line=0,
                    scope="<rtsSettings>",
                    reason="unknown-field",
                    detail=key,
                )
            )
            continue
        name, convert = _RTS_SETTING_FIELDS[folded]
        rts_settings[name] = convert(value)
    for node in rts_tree.roots:
        if node.kind.casefold() == "livingworldvictorytype":
            victory_types.append(_read_victory_type(node, gaps))
        else:
            gaps.append(
                Gap(
                    virtual_path=node.virtual_path,
                    line=node.line,
                    scope="<rtsSettings>",
                    reason="unmodelled-block",
                    detail=node.kind,
                )
            )

    effects_tree = read_tree(
        lines_of(REGION_EFFECTS_PATH, openers=_EFFECT_OPENERS, whitespace_pairs=True),
        openers=_EFFECT_OPENERS,
        whitespace_pairs=True,
    )
    gaps.extend(effects_tree.gaps)
    region_effects = [_read_region_effects(node, gaps) for node in effects_tree.roots]

    player_templates: list[dict[str, Any]] = []
    if catalog.resolve_exact(PLAYER_TEMPLATE_PATH) is None:
        gaps.append(
            Gap(
                virtual_path=PLAYER_TEMPLATE_PATH,
                line=0,
                scope="<entryPoint>",
                reason="unresolved-file",
                detail=PLAYER_TEMPLATE_PATH,
            )
        )
    else:
        player_templates = _read_player_templates(read(PLAYER_TEMPLATE_PATH), gaps)

    graph_gaps, connection_count = _validate_graph(region_campaigns, scenarios)
    gaps.extend(graph_gaps)

    # Retail nests ``ConcurrentRegionBonus`` inside the region campaign and
    # authors ``SpawnArmy`` inside each scenario's ``Act`` (via a shared
    # ``#include``).  Both are surfaced at the top level as well, deduplicated,
    # so a consumer never has to know which document authored them.
    for campaign in region_campaigns:
        territory_bonuses.extend(campaign["territoryBonuses"])
    territory_bonuses = _deduplicate(territory_bonuses)
    for scenario in scenarios:
        default_armies.extend(scenario["actArmies"])
        victory_types.extend(scenario["victoryTypes"])
    default_armies = _deduplicate(default_armies)
    # Victory types are NOT sorted: RotWK's MPRules indexes them by load order.
    victory_types = _deduplicate(victory_types)

    region_campaigns.sort(key=lambda row: str(row["name"]).casefold())
    territory_bonuses.sort(
        key=lambda row: (str(row["territory"]).casefold(), str(row["effectName"]).casefold())
    )
    scenarios.sort(key=lambda row: str(row["name"]).casefold())
    player_armies.sort(key=lambda row: str(row["name"]).casefold())
    default_armies.sort(key=lambda row: str(row["scriptingName"]).casefold())
    cities.sort(key=lambda row: str(row["playerArmy"]).casefold())
    region_effects.sort(key=lambda row: str(row["name"]).casefold())

    return {
        "format": 1,
        "schema": SCHEMA,
        "schemaVersion": SCHEMA_VERSION,
        "game": game,
        "sources": [sources[key] for key in sorted(sources)],
        "regionCampaigns": region_campaigns,
        "regionCount": sum(len(row["regions"]) for row in region_campaigns),
        "connectionCount": connection_count,
        "territoryBonuses": territory_bonuses,
        "regionEffects": region_effects,
        "cities": cities,
        "defaultArmies": default_armies,
        "playerArmies": player_armies,
        "scenarios": scenarios,
        "scenarioCount": len(scenarios),
        "victoryTypes": victory_types,
        "rtsSettings": rts_settings,
        "playerTemplates": player_templates,
        "gaps": _sorted_gaps(gaps),
    }


# ---------------------------------------------------------------------------
# pack lane
# ---------------------------------------------------------------------------


class _FileCatalog:
    """Duck-typed ``InstallCatalog`` over already-extracted source documents.

    The pack pipeline hands the ``living-world`` converter deterministic
    extracted files rather than a live retail install; this adapter lets the
    exact same fail-closed profiling lane read them.  Construction validates
    every mapped path up front and raises naming the virtual path, so a broken
    extraction can never masquerade as an absent mod surface.  A virtual path
    that was never mapped resolves to nothing, which keeps the profiler's own
    distinction intact: a missing entry point is a recorded gap, a missing
    ``#include`` inside an existing document is a hard error.
    """

    def __init__(self, sources: Mapping[str, tuple[str, Path]]) -> None:
        self._by_key: dict[str, tuple[CatalogEntry, Path]] = {}
        for virtual_path, (archive, raw_path) in sorted(sources.items()):
            folded = virtual_path.replace("\\", "/").casefold()
            if folded in self._by_key:
                raise ValueError(
                    f"living world source mapped twice: {virtual_path}"
                )
            path = Path(raw_path)
            if not path.is_file():
                raise ValueError(
                    f"living world source is not a readable file: "
                    f"{virtual_path} -> {path}"
                )
            entry = CatalogEntry(
                archive=str(archive),
                name=virtual_path.replace("\\", "/"),
                offset=0,
                size=path.stat().st_size,
                precedence=0,
            )
            self._by_key[folded] = (entry, path)

    def resolve_exact(self, virtual_path: str) -> CatalogEntry | None:
        found = self._by_key.get(virtual_path.replace("\\", "/").casefold())
        return found[0] if found else None

    def open_archive_for(self, entry: CatalogEntry) -> "_FileCatalog":
        return self

    def as_entry(self, entry: CatalogEntry) -> CatalogEntry:
        return entry

    def read_entry(self, entry: CatalogEntry, max_bytes: int | None = None) -> bytes:
        found = self._by_key.get(entry.name.casefold())
        if found is None:
            raise ValueError(f"living world source vanished: {entry.name}")
        source = found[1].read_bytes()
        if max_bytes is not None and len(source) > max_bytes:
            raise ValueError(
                f"living world source exceeds byte limit: {entry.name}"
            )
        return source


def profile_living_world_from_files(
    sources: Mapping[str, tuple[str, Path]], game: str
) -> dict[str, Any]:
    """Profile the strategic surface from extracted files (the pack lane).

    ``sources`` maps virtual paths to ``(archive_label, extracted_path)``.
    The document produced is byte-for-byte the one :func:`profile_living_world`
    produces from a retail install whose catalog carries the same bytes under
    the same archive labels.
    """

    return profile_living_world(_FileCatalog(sources), game)


def require_shippable(document: Mapping[str, Any]) -> dict[str, Any]:
    """Fail closed unless the profiled surface can honestly ship in a pack.

    Two gates, both structural rather than statistical:

    * every entry point must have resolved — a mod install may legitimately
      omit a surface, but a pack that *declares* the living-world converter is
      promising the strategic layer real data, so an absent surface there is a
      broken pack, not a choice; and
    * at least one region campaign must carry connections — retail ships
      legacy Risk-style campaigns whose regions have no edges at all, and a
      strategic map without edges would load cleanly and then never let an
      army move.

    Raises ``ValueError`` naming exactly what is missing; returns the document
    unchanged when it passes.
    """

    unresolved = sorted(
        {
            str(gap.get("detail", ""))
            for gap in document.get("gaps", [])
            if gap.get("reason") == "unresolved-file"
        }
    )
    if unresolved:
        raise ValueError(
            "living-world document is not shippable: unresolved entry point(s) "
            + ", ".join(unresolved)
        )
    connected = max(
        (
            sum(len(region["connections"]) for region in campaign["regions"])
            for campaign in document.get("regionCampaigns", [])
        ),
        default=0,
    )
    if connected <= 0:
        raise ValueError(
            "living-world document is not shippable: no region campaign carries "
            "any connection, so no army could ever move on the strategic map"
        )
    return dict(document)


def _read_player_templates(source: bytes, gaps: list[Gap]) -> list[dict[str, Any]]:
    """Read the world/hero command-point economy per faction.

    ``LivingWorldPlayerTemplate`` is a flat named family, so it is read with the
    existing ``parse_flat_named_blocks`` rather than this module's nested reader.
    """

    templates: list[dict[str, Any]] = []
    for block in parse_flat_named_blocks(source, "LivingWorldPlayerTemplate"):
        template: dict[str, Any] = {
            "name": block.name,
            **{name: "" for name in sorted(set(_PLAYER_TEMPLATE_TEXT_FIELDS.values()))},
            **{name: -1 for name in sorted(set(_PLAYER_TEMPLATE_INT_FIELDS.values()))},
        }
        for key, value in block.assignments:
            folded = key.casefold()
            if folded in _PLAYER_TEMPLATE_TEXT_FIELDS:
                template[_PLAYER_TEMPLATE_TEXT_FIELDS[folded]] = _unquote(value)
            elif folded in _PLAYER_TEMPLATE_INT_FIELDS:
                template[_PLAYER_TEMPLATE_INT_FIELDS[folded]] = _as_int(value)
            else:
                gaps.append(
                    Gap(
                        virtual_path=PLAYER_TEMPLATE_PATH,
                        line=0,
                        scope=f"LivingWorldPlayerTemplate {block.name}",
                        reason="unknown-field",
                        detail=key,
                    )
                )
        templates.append(template)
    templates.sort(key=lambda row: str(row["name"]).casefold())
    return templates


def _validate_graph(
    region_campaigns: list[dict[str, Any]],
    scenarios: list[dict[str, Any]],
) -> tuple[list[Gap], int]:
    """Cross-check the region graph; every inconsistency becomes a gap."""

    gaps: list[Gap] = []
    connection_count = 0
    for campaign in region_campaigns:
        known = {str(region["id"]).casefold() for region in campaign["regions"]}
        if len(known) != len(campaign["regions"]):
            gaps.append(
                Gap(
                    virtual_path=RISK_CAMPAIGN_PATH,
                    line=0,
                    scope=str(campaign["name"]),
                    reason="duplicate-region-id",
                    detail=str(campaign["name"]),
                )
            )
        reciprocal: dict[str, set[str]] = {}
        for region in campaign["regions"]:
            source_id = str(region["id"])
            reciprocal[source_id.casefold()] = {
                str(link["region"]).casefold() for link in region["connections"]
            }
            connection_count += len(region["connections"])
            for link in region["connections"]:
                if str(link["region"]).casefold() not in known:
                    gaps.append(
                        Gap(
                            virtual_path=RISK_CAMPAIGN_PATH,
                            line=0,
                            scope=f"{campaign['name']}/{source_id}",
                            reason="dangling-connection",
                            detail=str(link["region"]),
                        )
                    )
        for source_id, targets in sorted(reciprocal.items()):
            for target in sorted(targets):
                if target in reciprocal and source_id not in reciprocal[target]:
                    gaps.append(
                        Gap(
                            virtual_path=RISK_CAMPAIGN_PATH,
                            line=0,
                            scope=f"{campaign['name']}/{source_id}",
                            reason="one-way-connection",
                            detail=target,
                        )
                    )

    campaigns_by_name = {
        str(campaign["name"]).casefold(): campaign for campaign in region_campaigns
    }
    for scenario in scenarios:
        campaign = campaigns_by_name.get(str(scenario["regionCampaign"]).casefold())
        if campaign is None:
            gaps.append(
                Gap(
                    virtual_path=RISK_CAMPAIGN_PATH,
                    line=0,
                    scope=str(scenario["name"]),
                    reason="unresolved-region-campaign",
                    detail=str(scenario["regionCampaign"]),
                )
            )
            continue
        known = {str(region["id"]).casefold() for region in campaign["regions"]}
        referenced: list[str] = [
            *scenario["defaultStartSpots"],
            *scenario["disallowStartInRegions"],
            *scenario["disableRegions"],
        ]
        for ownership in scenario["ownershipSets"]:
            referenced.extend(ownership["regions"])
            if ownership["startRegion"]:
                referenced.append(ownership["startRegion"])
            for spawn in ownership["spawnArmies"]:
                if spawn["region"]:
                    referenced.append(spawn["region"])
        for name in sorted(set(referenced), key=str.casefold):
            if name.casefold() not in known:
                gaps.append(
                    Gap(
                        virtual_path=RISK_CAMPAIGN_PATH,
                        line=0,
                        scope=str(scenario["name"]),
                        reason="unresolved-region-reference",
                        detail=name,
                    )
                )
    return gaps, connection_count
