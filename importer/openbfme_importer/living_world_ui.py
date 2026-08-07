"""Convert retail's War of the Ring UI SURFACE - the strategic screen's icons,
its buildable structures, its recruitable armies and the atlas crops behind all
three - into a bundle the Godot strategic screen can draw.

WHY THIS MODULE EXISTS
----------------------
The strategic screen draws armies as coloured DOTS. Retail draws them as BANNER
MARKERS CARRYING A PORTRAIT, and it draws a ring of building icons around a
selected build plot. A prior lane established, correctly, that
``data/ini/livingworldicons/*.ini`` carry NO portraits: they are 3D map-marker
definitions (43 W3D models, all present in the archives) and nothing else.

The portraits are real, and they are somewhere else. Every icon retail's
strategic UI shows is a ``MappedImage`` - an ATLAS NAME plus a pixel rectangle
into a shared texture - and the living-world data references them BY ID:

    LivingWorldBuilding LWB_MenFortress
        ConstructButtonImage = BGFortress
        BuildingNugget SpawnArmy NuggetTag_Spawner
            ArmyToSpawn
                PlayerArmy           = AragornPlayerArmy
                HeroTemplateName     = GondorAragornMP
                ConstructButtonImage = HIAragorn_wotr

    LivingWorldPlayerTemplate PlayerMen
        FactionIcon                    = "IconMenStrategic"
        GarrisonSelectionPortraitName  = "UPGondor_Army"
        BuildPlotSelectionPortraitName = "BPGFortress_BuildPlot"

So this module is, first and foremost, a MAPPED-IMAGE RESOLVER: it collects the
exact set of image ids the living world names, resolves each against retail's 37
``data/ini/mappedimages/**`` documents, resolves each resulting atlas to its
compiled leaf, and copies those atlas payloads out VERBATIM. The rectangles
travel with them, so the Godot side crops rather than guesses.

The parsing half is deliberately thin: ``openbfme_importer.mapped_image``
already implements the ``MappedImage`` grammar and the compiled-atlas path rule,
and ``openbfme_importer.livingworld`` already implements the Living World block
grammar with its ``#include`` expansion and its unbalanced-document quarantine.
Both are imported and used unchanged.

WHAT IT REFUSES TO DO
---------------------
* It NEVER substitutes a portrait. An image id with no ``MappedImage`` block is
  listed in ``gaps.missingImageIds``; an id defined twice is listed in
  ``gaps.ambiguousImageIds``; an atlas whose compiled leaf is not in the catalog
  is listed in ``gaps.unresolvedAtlases``. In all three cases the record keeps
  its retail id and carries NO crop, and the Godot side draws the faction colour
  and says which portrait is missing. Retail ships exactly one of these and
  marks it itself: ``ConstructButtonImage = CPYoungWizardAlpha  // TEMP``, whose
  texture ``CPYoungWizardAlpha.tga`` is in no archive.
* It NEVER derives an image id from a name. The only links it follows are
  retail's own authored fields - ``PlayerArmy``, ``HeroTemplateName``,
  ``GarrisonSelectionPortraitName`` - never a resemblance between two strings.
* It NEVER invents a building, a recruit or a cost. A field it does not model is
  recorded as a gap naming the block and the key.
* It reads through the CATALOGS, never through the effective-assets cache (which
  is BFME2 where RotWK should win). Winner for a name is the lowest
  ``(precedence, archive.casefold())``.

WHAT IT DOES NOT CARRY, AND WHY
-------------------------------
* No turn-phase list. ``data/ini/livingworldlogic.ini`` is 192 bytes of comment,
  there is no ``mprules.ini``, and retail's phase bar is hardcoded in the
  executable. There is nothing to convert and inventing one would be fiction.
* No 3D marker models. ``LivingWorldArmyIcon`` / ``LivingWorldBuildingIcon`` /
  ``LivingWorldBuildPlotIcon`` name W3D models, and this bundle records THE
  NAMES so the screen can say which models it is not standing up - it does not
  convert them. That is a separate, larger job.

BUNDLE LAYOUT
-------------
``living-world-ui.json``  the manifest: buildings, recruits, templates, crops
``ui-atlases/*``          verbatim retail atlas bytes (DDS/TGA), unaltered
"""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import re
from dataclasses import replace
from decimal import Decimal, InvalidOperation
from math import isfinite
from typing import Any, Iterable, Mapping

from .livingmap_bundle import CatalogReader
from .livingworld import (
    Gap,
    Node,
    expand_document,
    flatten_document,
    read_tree,
)
from .mapped_image import (
    MappedImageRecord,
    resolve_mapped_image_texture_paths_partial,
    resolve_mapped_images_partial,
)

SCHEMA = "openbfme.living-world-ui"
SCHEMA_VERSION = 2

MANIFEST_NAME = "living-world-ui.json"
ATLAS_DIRECTORY = "ui-atlases"

BUILDINGS_PATH = "data/ini/livingworldbuildings.ini"
PLAYERS_PATH = "data/ini/livingworldplayers.ini"
BUILD_PLOT_ICONS_PATH = "data/ini/livingworldbuildploticons.ini"
BUILDING_ICONS_PATH = "data/ini/livingworldbuildingicons.ini"

#: Where retail keeps every ``MappedImage`` document. All 37 are read, because
#: the living world draws from at least six of them (building radial buttons,
#: hero UI, unit portraits, strategic images, expansion icons, hand-created) and
#: guessing which is a way to produce a missing portrait that is this module's
#: fault rather than retail's.
MAPPED_IMAGE_PREFIX = "data/ini/mappedimages/"

#: Bounded, so a hostile or corrupt catalog cannot make this module allocate
#: without limit. Retail's real numbers are far below every one of these.
MAX_ATLASES = 512
MAX_ATLAS_BYTES = 16 * 1024 * 1024
MAX_IMAGE_IDS = 4096
MAX_NUGGETS = 32
MAX_NUGGET_BONUSES = 16
MAX_NUGGET_ARMIES = 16
MAX_UPGRADEABLE_UNITS = 64
MAX_NUGGET_STRING = 256
# Godot's JSON reader exposes numbers as IEEE-754 floats. Refuse integers whose
# exact value could not survive the converter/runtime boundary.
MAX_SAFE_JSON_INTEGER = (1 << 53) - 1
_NUGGET_HEADER_MARK = "|#|"

#: ``LivingWorldBuilding`` fields this module models, mapped to manifest keys.
_BUILDING_FIELDS = {
    "availableto": "availableTo",
    "battlethingtemplate": "battleThingTemplate",
    "buildingicon": "buildingIcon",
    "turnstobuild": "turnsToBuild",
    "strategicresourcecost": "strategicResourceCost",
    "constructbuttonimage": "constructButtonImage",
    "constructbuttontitle": "constructButtonTitle",
    "constructbuttonhelp": "constructButtonHelp",
    "displaynametag": "displayNameTag",
    "displaydescriptiontag": "displayDescriptionTag",
    "createunitduringautoresolve": "createUnitDuringAutoResolve",
    "candefendterritory": "canDefendTerritory",
    "type": "type",
}

#: ``ArmyToSpawn`` fields this module models. This is the block that carries a
#: hero's strategic portrait, and it is the ONLY place in the living-world data
#: that binds a ``PlayerArmy`` to an image.
_RECRUIT_FIELDS = {
    "playerarmy": "playerArmy",
    "herotemplatename": "heroTemplateName",
    "icon": "icon",
    "iconsize": "iconSize",
    "buildtime": "buildTime",
    "palantirmovie": "palantirMovie",
    "constructbuttonimage": "constructButtonImage",
    "constructbuttontitle": "constructButtonTitle",
    "constructbuttonhelp": "constructButtonHelp",
}

#: ``LivingWorldPlayerTemplate`` fields that name an image or a marker. The
#: command-point economy is already in the living-world document and is NOT
#: restated here; this bundle carries the UI surface only.
_TEMPLATE_FIELDS = {
    "faction": "faction",
    "factionicon": "factionIcon",
    "defaultarmyiconname": "defaultArmyIconName",
    "buildploticonname": "buildPlotIconName",
    "buildplotselectionportraitname": "buildPlotSelectionPortraitName",
    "garrisonselectionportraitname": "garrisonSelectionPortraitName",
    "garrisondisplaynametag": "garrisonDisplayNameTag",
}

#: Which of those fields is an image id rather than a marker or a string key.
_TEMPLATE_IMAGE_FIELDS = (
    "factionIcon",
    "buildPlotSelectionPortraitName",
    "garrisonSelectionPortraitName",
)

#: UI CHROME retail ships as MappedImages and the strategic screen can use
#: verbatim. Requested unconditionally, on top of whatever the living-world data
#: names, because they are the frame art rather than the content art.
#:
#: * ``RadialBorder`` is retail's own radial-menu ring (``radialborders.dds``).
#: * ``Banner_*`` are retail's seven faction standards from
#:   ``reinforcementbanners.ini``.
#:
#: An id here that does not resolve is reported exactly like any other gap and
#: the screen draws its own shape instead - it never substitutes another image.
CHROME_IMAGE_IDS = (
    "RadialBorder",
    "Banner_Angmar",
    "Banner_Dwarf",
    "Banner_Elf",
    "Banner_Isengard",
    "Banner_Men",
    "Banner_Mordor",
    "Banner_Wild",
)

#: The APT movie's own texture sheet for the War of the Ring shell. This is NOT
#: a MappedImage - it belongs to ``livingworldui.apt``, whose layout is built at
#: runtime by ActionScript and is therefore not converted - but the SHEET is a
#: plain TGA in the archives and it carries retail's strategic ring art: two
#: elvish-script rings (one gold, one red) and a set of gradient bars.
#:
#: The sub-rectangles are DERIVED, not authored: nothing in the shipped data
#: says where one ring ends and the next begins, so this module finds them by
#: labelling connected regions of non-transparent pixels. That is arithmetic
#: over retail's own alpha channel, it is reproducible, and it is recorded as
#: derived so the Godot side can say so rather than implying retail wrote the
#: numbers down.
CHROME_SHEET = "art/Textures/apt_LivingWorldUI_1.tga"
CHROME_SHEET_FILE = "apt_livingworldui_1.tga"
#: Alpha at or below this is transparent for component-finding. Retail's sheet
#: is fully transparent outside the art, so this only has to be above zero.
CHROME_ALPHA_FLOOR = 8
#: Components smaller than this in either axis are specks, not art.
CHROME_MIN_COMPONENT = 12
MAX_CHROME_COMPONENTS = 64

_SAFE_FILE = re.compile(r"^[A-Za-z0-9_.\-]{1,128}$")


class LivingWorldUiError(RuntimeError):
    """The bundle cannot be produced, with the exact reason."""


# --- reading the living-world dialect ------------------------------------------


def _unquote(value: str) -> str:
    text = value.strip()
    if len(text) >= 2 and text[0] == text[-1] and text[0] in "\"'":
        return text[1:-1].strip()
    return text


def _tree_of(reader: CatalogReader, virtual_path: str, gaps: list[Gap]):
    """Parse one Living World document through the shared block reader."""

    def read(path: str) -> bytes:
        entry = reader.resolve(path)
        if entry is None:
            raise LivingWorldUiError(f"{path} is not in the catalog")
        return reader.read(entry)

    document = expand_document(virtual_path, read)
    lines = flatten_document(
        document, openers=frozenset(), whitespace_pairs=False, gaps=gaps
    )
    protected = []
    for line in lines:
        tokens = line.text.split()
        if len(tokens) == 3 and "=" not in line.text and tokens[0].casefold() == "buildingnugget":
            protected.append(replace(line, text=tokens[0] + " " + tokens[1] + _NUGGET_HEADER_MARK + tokens[2]))
        else:
            protected.append(line)
    return read_tree(protected, openers=frozenset())


def _fields(
    node: Node, allowed: Mapping[str, str], scope: str, gaps: list[Gap]
) -> dict[str, str]:
    """Pull the modelled fields off one block; gap every key not modelled."""

    row: dict[str, str] = {name: "" for name in sorted(set(allowed.values()))}
    for key, value in node.fields:
        folded = key.casefold()
        if folded in allowed:
            row[allowed[folded]] = _unquote(value)
            continue
        gaps.append(
            Gap(
                virtual_path=node.virtual_path,
                line=node.line,
                scope=scope,
                reason="unknown-field",
                detail=key,
            )
        )
    return row


def _nugget_gap(node: Node, scope: str, reason: str, detail: str, gaps: list[Gap]) -> None:
    gaps.append(Gap(node.virtual_path, node.line, scope, reason, detail))


def _nugget_text(value: str) -> str:
    text = _unquote(value)
    if not text or len(text) > MAX_NUGGET_STRING:
        raise ValueError("string")
    return text


def _strict_int(value: str, *, positive: bool) -> int:
    text = _unquote(value)
    if re.fullmatch(r"[+-]?\d+", text) is None:
        raise ValueError("integer")
    result = int(text)
    if abs(result) > MAX_SAFE_JSON_INTEGER:
        raise ValueError("exact JSON integer range")
    if result <= 0 if positive else result < 0:
        raise ValueError("range")
    return result


def _scalar_fields(node: Node, allowed: set[str], required: set[str]) -> dict[str, str]:
    result: dict[str, str] = {}
    folded_allowed = {key.casefold(): key for key in allowed}
    for key, value in node.fields:
        folded = key.casefold()
        if folded not in folded_allowed:
            raise KeyError(key)
        canonical = folded_allowed[folded]
        if canonical in result:
            raise RuntimeError(key)
        result[canonical] = value
    missing = required - result.keys()
    if missing:
        raise LookupError(sorted(missing)[0])
    return result


def _bonus(value: str) -> dict[str, Any]:
    tokens = _unquote(value).split()
    if not tokens:
        raise ValueError("Bonus")
    threshold = _strict_int(tokens[0], positive=True)
    result: dict[str, Any] = {
        "threshold": threshold,
        "weaponPct": None, "weaponRaw": None,
        "armorPct": None, "armorRaw": None,
        "experiencePct": None, "experienceRaw": None,
    }
    dimensions = {
        "weapon": ("weaponPct", "weaponRaw"),
        "armor": ("armorPct", "armorRaw"),
        "experience": ("experiencePct", "experienceRaw"),
    }
    seen: set[str] = set()
    for token in tokens[1:]:
        if len(token) > MAX_NUGGET_STRING:
            raise OverflowError("Bonus string")
        if ":" not in token:
            raise ValueError(token)
        dimension, raw = token.split(":", 1)
        folded = dimension.casefold()
        if folded not in dimensions or folded in seen or not raw.endswith("%"):
            raise ValueError(token)
        # Strip exactly one percent sign and accept finite decimal literals only.
        number = raw[:-1]
        if re.fullmatch(r"[+-]?(?:\d+(?:\.\d*)?|\.\d+)", number) is None:
            raise ValueError(token)
        try:
            decimal = Decimal(number)
        except InvalidOperation as exc:
            raise ValueError(token) from exc
        numeric = float(decimal)
        if not decimal.is_finite() or not isfinite(numeric):
            raise ValueError(token)
        pct_key, raw_key = dimensions[folded]
        result[pct_key] = numeric
        result[raw_key] = token
        seen.add(folded)
    return result


def _typed_nugget(node: Node, scope: str) -> dict[str, Any]:
    name = str(node.name or "")
    if _NUGGET_HEADER_MARK not in name:
        raise TypeError(name or node.kind)
    authored_kind, tag = name.split(_NUGGET_HEADER_MARK, 1)
    tag = _nugget_text(tag)
    kind = authored_kind.casefold()
    if kind == "strengthenarmy":
        if node.children:
            raise ChildProcessError(node.children[0].kind)
        # Bonus is the sole repeatable field; all scalar fields are unique.
        scalar_node = replace(node, fields=tuple((k, v) for k, v in node.fields if k.casefold() != "bonus"))
        fields = _scalar_fields(scalar_node, {"StrengtheningRange", "BonusKey"}, {"StrengtheningRange", "BonusKey"})
        bonus_values = [v for k, v in node.fields if k.casefold() == "bonus"]
        if not bonus_values:
            raise LookupError("Bonus")
        if len(bonus_values) > MAX_NUGGET_BONUSES:
            raise OverflowError("Bonus")
        return {"kind": "strengthen_army", "tag": tag,
                "strengtheningRange": _nugget_text(fields["StrengtheningRange"]),
                "bonusKey": _nugget_text(fields["BonusKey"]),
                "bonuses": [_bonus(value) for value in bonus_values]}
    if kind == "increasetreasury":
        if node.children:
            raise ChildProcessError(node.children[0].kind)
        fields = _scalar_fields(node, {"TreasureAmount"}, {"TreasureAmount"})
        return {"kind": "increase_treasury", "tag": tag,
                "treasureAmount": _nugget_text(fields["TreasureAmount"])}
    if kind == "increasecommandpoints":
        if node.children:
            raise ChildProcessError(node.children[0].kind)
        fields = _scalar_fields(node, {"Type", "Amount"}, {"Type", "Amount"})
        raw_amount = _unquote(fields["Amount"])
        if re.fullmatch(r"[+-]?\d+", raw_amount) is None:
            raise ValueError("Amount")
        amount = int(raw_amount)
        if abs(amount) > MAX_SAFE_JSON_INTEGER:
            raise ValueError("Amount exact JSON integer range")
        return {"kind": "increase_command_points", "tag": tag,
                "type": _nugget_text(fields["Type"]), "amount": amount}
    if kind == "upgradetroops":
        if node.children:
            raise ChildProcessError(node.children[0].kind)
        fields = _scalar_fields(node, {"NumUpgradesPerTurn", "UpgradeableUnits"}, {"NumUpgradesPerTurn", "UpgradeableUnits"})
        units = _unquote(fields["UpgradeableUnits"]).split()
        if not units or len(units) > MAX_UPGRADEABLE_UNITS or any(len(v) > MAX_NUGGET_STRING for v in units):
            raise OverflowError("UpgradeableUnits") if len(units) > MAX_UPGRADEABLE_UNITS else ValueError("UpgradeableUnits")
        return {"kind": "upgrade_troops", "tag": tag,
                "numUpgradesPerTurn": _strict_int(fields["NumUpgradesPerTurn"], positive=True),
                "upgradeableUnits": units}
    if kind == "spawnarmy":
        fields = _scalar_fields(node, {"QueueSize"}, {"QueueSize"})
        if len(node.children) > MAX_NUGGET_ARMIES:
            raise OverflowError("ArmyToSpawn")
        armies: list[dict[str, Any]] = []
        for child in node.children:
            if child.kind.casefold() != "armytospawn" or child.name is not None or child.children:
                raise ChildProcessError(child.kind)
            # Full back-compatible recruit row, but SOURCE order here.
            seen: set[str] = set()
            for key, _ in child.fields:
                folded = key.casefold()
                if folded not in _RECRUIT_FIELDS:
                    raise KeyError(key)
                if folded in seen:
                    raise RuntimeError(key)
                seen.add(folded)
            army = _fields(child, _RECRUIT_FIELDS, scope + " / ArmyToSpawn", [])
            if any(len(str(value)) > MAX_NUGGET_STRING for value in army.values()):
                raise OverflowError("ArmyToSpawn string")
            armies.append(army)
        # Queue depth zero is valid: it explicitly disables queuing while still
        # retaining authored ArmyToSpawn choices.
        return {"kind": "spawn_army", "tag": tag,
                "queueSize": _strict_int(fields["QueueSize"], positive=False),
                "armies": armies}
    raise TypeError(authored_kind)


def _convert_nuggets(node: Node, scope: str, gaps: list[Gap]) -> tuple[str, list[dict[str, Any]]]:
    if len(node.children) > MAX_NUGGETS:
        _nugget_gap(node, scope, "nugget_cap_exceeded", "BuildingNugget", gaps)
        return "refused", []
    converted: list[dict[str, Any]] = []
    for child in node.children:
        child_scope = scope + " / " + str(child.name or child.kind)
        try:
            if child.kind.casefold() != "buildingnugget":
                raise ChildProcessError(child.kind)
            converted.append(_typed_nugget(child, child_scope))
        except OverflowError as exc:
            _nugget_gap(child, child_scope, "nugget_cap_exceeded", str(exc), gaps)
            return "refused", []
        except TypeError as exc:
            _nugget_gap(child, child_scope, "nugget_unknown_kind", str(exc), gaps)
            return "refused", []
        except KeyError as exc:
            _nugget_gap(child, child_scope, "nugget_unknown_field", str(exc).strip("'"), gaps)
            return "refused", []
        except ChildProcessError as exc:
            _nugget_gap(child, child_scope, "nugget_unknown_subblock", str(exc), gaps)
            return "refused", []
        except (ValueError, RuntimeError, LookupError) as exc:
            _nugget_gap(child, child_scope, "nugget_bad_value", str(exc), gaps)
            return "refused", []
    return "ok", converted


def _read_buildings(reader: CatalogReader, gaps: list[Gap]) -> list[dict[str, Any]]:
    """Convert buildings, retaining the legacy flattened recruit projection."""

    tree = _tree_of(reader, BUILDINGS_PATH, gaps)
    buildings: list[dict[str, Any]] = []
    for node in tree.roots:
        if node.kind.casefold() != "livingworldbuilding":
            gaps.append(Gap(node.virtual_path, node.line, "<root>", "unmodelled-block", node.kind))
            continue
        scope = f"LivingWorldBuilding {node.name}"
        row = _fields(node, _BUILDING_FIELDS, scope, gaps)
        row["id"] = str(node.name or "")
        recruits: list[dict[str, Any]] = []
        for nugget in node.children:
            for spawn in nugget.blocks("ArmyToSpawn"):
                recruits.append(_fields(spawn, _RECRUIT_FIELDS, f"{scope} / ArmyToSpawn", gaps))
        recruits.sort(key=lambda item: str(item["playerArmy"]).casefold())
        row["recruits"] = recruits
        status, nuggets = _convert_nuggets(node, scope, gaps)
        row["nuggetsStatus"] = status
        row["nuggets"] = nuggets
        buildings.append(row)
    buildings.sort(key=lambda item: str(item["id"]).casefold())
    return buildings


def _read_templates(reader: CatalogReader, gaps: list[Gap]) -> list[dict[str, Any]]:
    """The UI surface of each ``LivingWorldPlayerTemplate``.

    Unknown fields here are NOT gapped: this document's economy fields
    (``StartingWorldCP``, ``ScenarioStartResources`` and friends) are already
    modelled by ``openbfme_importer.livingworld`` and carried in the living-world
    document. Reporting them again as gaps would manufacture 60 false holes.
    """

    tree = _tree_of(reader, PLAYERS_PATH, gaps)
    templates: list[dict[str, Any]] = []
    for node in tree.roots:
        if node.kind.casefold() != "livingworldplayertemplate":
            continue
        row: dict[str, Any] = {
            name: "" for name in sorted(set(_TEMPLATE_FIELDS.values()))
        }
        for key, value in node.fields:
            folded = key.casefold()
            if folded in _TEMPLATE_FIELDS:
                row[_TEMPLATE_FIELDS[folded]] = _unquote(value)
        row["name"] = str(node.name or "")
        templates.append(row)
    templates.sort(key=lambda item: str(item["name"]).casefold())
    return templates


def _read_marker_families(
    reader: CatalogReader, virtual_path: str, kind: str, gaps: list[Gap]
) -> list[dict[str, Any]]:
    """The W3D models one marker family names, recorded but NOT converted.

    ``LivingWorldBuildPlotIcon BuildPlotIcon_MOW`` names ``LMGFoundation`` as its
    decal. This bundle carries the NAME so the screen can say which retail model
    it is standing in for; converting the model is a different job and claiming
    otherwise would be the exact kind of quiet overstatement this lane exists to
    avoid.
    """

    tree = _tree_of(reader, virtual_path, gaps)
    rows: list[dict[str, Any]] = []
    for node in tree.roots:
        if node.kind.casefold() != kind.casefold():
            continue
        objects: list[dict[str, str]] = []
        for child in node.children:
            objects.append(
                {
                    "slot": str(child.name or child.kind),
                    "model": _unquote(child.value("Model") or ""),
                    "subObjects": _unquote(child.value("SubObjects") or ""),
                }
            )
        objects.sort(key=lambda item: item["slot"].casefold())
        rows.append({"id": str(node.name or ""), "objects": objects})
    rows.sort(key=lambda item: str(item["id"]).casefold())
    return rows


# --- the mapped-image resolver -------------------------------------------------


def _mapped_image_documents(reader: CatalogReader) -> list[tuple[str, bytes]]:
    """Every ``MappedImage`` document in the catalog, by the winner rule."""

    names = sorted(
        entry.name
        for entry in reader._winners.values()  # noqa: SLF001 - read-only view
        if entry.name.casefold().startswith(MAPPED_IMAGE_PREFIX)
    )
    documents: list[tuple[str, bytes]] = []
    for name in names:
        entry = reader.resolve(name)
        if entry is None:  # pragma: no cover - names came from the winners map
            continue
        documents.append((name, reader.read(entry)))
    return documents


def _atlas_file_name(texture: str) -> str:
    """A safe, stable file name for one atlas payload."""

    stem = texture.rsplit("/", 1)[-1].rsplit("\\", 1)[-1]
    stem = stem.rsplit(".", 1)[0].casefold()
    if not _SAFE_FILE.fullmatch(stem):
        raise LivingWorldUiError(f"unsafe atlas texture name: {texture!r}")
    return stem


def resolve_images(
    reader: CatalogReader, image_ids: Iterable[str]
) -> tuple[dict[str, MappedImageRecord], list[str], list[str]]:
    """Resolve an exact id set to atlas crops, keeping the gaps.

    This is the reusable half of the module and the reason it exists: any lane
    that needs a retail UI icon needs exactly this - an id, an atlas name and a
    pixel rectangle - and it must fail by NAMING the id rather than by producing
    a plausible substitute.
    """

    requested = sorted({str(value).strip() for value in image_ids if str(value).strip()})
    if not requested:
        return {}, [], []
    if len(requested) > MAX_IMAGE_IDS:
        raise LivingWorldUiError(
            f"{len(requested)} image ids requested, over the {MAX_IMAGE_IDS} limit"
        )
    documents = _mapped_image_documents(reader)
    if not documents:
        raise LivingWorldUiError(
            f"the catalog carries no {MAPPED_IMAGE_PREFIX}** document, so no "
            "strategic icon can be resolved"
        )
    resolution = resolve_mapped_images_partial(
        (payload for _name, payload in documents), requested
    )
    by_id = {record.id.casefold(): record for record in resolution.records}
    return by_id, list(resolution.missing_ids), list(resolution.ambiguous_ids)


# --- the APT chrome sheet -------------------------------------------------------


def _tga_bgra(payload: bytes) -> tuple[int, int, bytes]:
    """The alpha channel of an uncompressed 32-bit TGA, and its size.

    Deliberately narrow: retail's strategic sheet is image type 2, 32 bits per
    pixel, no colour map, and anything else is REFUSED by name rather than
    decoded approximately. A wrong decode here would produce plausible
    rectangles over the wrong pixels, which is exactly the failure this module
    exists to make impossible.
    """

    if len(payload) < 18:
        raise LivingWorldUiError(f"{CHROME_SHEET} is {len(payload)} bytes, too short for a TGA")
    id_length = payload[0]
    colour_map_type = payload[1]
    image_type = payload[2]
    width = int.from_bytes(payload[12:14], "little")
    height = int.from_bytes(payload[14:16], "little")
    depth = payload[16]
    descriptor = payload[17]
    if colour_map_type != 0 or image_type != 2 or depth != 32:
        raise LivingWorldUiError(
            f"{CHROME_SHEET} is TGA type {image_type}, {depth}bpp, colour-map "
            f"{colour_map_type}; this module reads only uncompressed 32-bit "
            "true-colour"
        )
    if width <= 0 or height <= 0 or width > 8192 or height > 8192:
        raise LivingWorldUiError(f"{CHROME_SHEET} declares {width}x{height}")
    start = 18 + id_length
    needed = width * height * 4
    if len(payload) < start + needed:
        raise LivingWorldUiError(
            f"{CHROME_SHEET} declares {width}x{height} but carries "
            f"{len(payload) - start} pixel bytes, not {needed}"
        )
    pixels = payload[start : start + needed]
    # Bit 5 of the descriptor is the origin: 0 means the first row in the file is
    # the BOTTOM row. Rows are flipped here so the rectangles are in the same
    # top-left space Godot draws in.
    top_down = bool(descriptor & 0x20)
    if top_down:
        return width, height, bytes(pixels)
    stride = width * 4
    flipped = bytearray(len(pixels))
    for row in range(height):
        source = (height - 1 - row) * stride
        flipped[row * stride : row * stride + stride] = pixels[source : source + stride]
    return width, height, bytes(flipped)


def _alpha_components(
    width: int, height: int, bgra: bytes, *, block: int = 4
) -> list[dict[str, Any]]:
    """Bounding boxes of the sheet's separate opaque islands, largest first.

    Worked on a coarse grid of ``block``-pixel cells rather than per pixel: the
    result is a bounding box either way, the grid is 16x cheaper, and a
    rectangle rounded outward to a 4-pixel cell cannot clip the art it encloses.
    """

    columns = (width + block - 1) // block
    rows = (height + block - 1) // block
    occupied = bytearray(columns * rows)
    for y in range(height):
        row_base = y * width * 4
        cell_row = (y // block) * columns
        for x in range(width):
            if bgra[row_base + x * 4 + 3] > CHROME_ALPHA_FLOOR:
                occupied[cell_row + (x // block)] = 1
    seen = bytearray(len(occupied))
    boxes: list[dict[str, Any]] = []
    for index in range(len(occupied)):
        if not occupied[index] or seen[index]:
            continue
        stack = [index]
        seen[index] = 1
        min_c = max_c = index % columns
        min_r = max_r = index // columns
        while stack:
            current = stack.pop()
            c = current % columns
            r = current // columns
            min_c = min(min_c, c)
            max_c = max(max_c, c)
            min_r = min(min_r, r)
            max_r = max(max_r, r)
            for dc, dr in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                nc = c + dc
                nr = r + dr
                if nc < 0 or nr < 0 or nc >= columns or nr >= rows:
                    continue
                neighbour = nr * columns + nc
                if occupied[neighbour] and not seen[neighbour]:
                    seen[neighbour] = 1
                    stack.append(neighbour)
        left = min_c * block
        top = min_r * block
        right = min((max_c + 1) * block, width)
        bottom = min((max_r + 1) * block, height)
        if right - left < CHROME_MIN_COMPONENT or bottom - top < CHROME_MIN_COMPONENT:
            continue
        # THE COMPONENT'S OWN COLOUR AND HOW MUCH OF ITS BOX IT FILLS, both
        # averaged over its opaque pixels. These exist so the consumer can pick
        # "the gold ring" by a STATED RULE over retail's own pixels instead of
        # someone opening the sheet in an image editor and writing down an
        # index - which would be a number nobody could re-derive.
        total = 0
        sum_r = sum_g = sum_b = sum_a = 0
        for y in range(top, bottom):
            row_base = y * width * 4
            for x in range(left, right):
                offset = row_base + x * 4
                a = bgra[offset + 3]
                if a <= CHROME_ALPHA_FLOOR:
                    continue
                total += 1
                sum_b += bgra[offset]
                sum_g += bgra[offset + 1]
                sum_r += bgra[offset + 2]
                sum_a += a
        area = max((right - left) * (bottom - top), 1)
        boxes.append(
            {
                "left": left,
                "top": top,
                "right": right,
                "bottom": bottom,
                "meanColor": [
                    round(sum_r / total) if total else 0,
                    round(sum_g / total) if total else 0,
                    round(sum_b / total) if total else 0,
                ],
                "meanAlpha": round(sum_a / total) if total else 0,
                "coverage": round(total / area, 4),
            }
        )
    boxes.sort(
        key=lambda box: (
            -((box["right"] - box["left"]) * (box["bottom"] - box["top"])),
            box["top"],
            box["left"],
        )
    )
    return boxes[:MAX_CHROME_COMPONENTS]


def _faction_banners(
    templates: list[dict[str, Any]], images: Mapping[str, MappedImageRecord]
) -> tuple[dict[str, str], str]:
    """Bind each player template to retail's standard for its own faction.

    THIS LINK IS DERIVED, NOT AUTHORED, and it is emitted only when the
    derivation is a TOTAL BIJECTION - which is the whole reason it is allowed at
    all. No INI binds a ``LivingWorldPlayerTemplate`` to a ``Banner_*`` image;
    retail does it inside the APT. What retail's data does give is two sets:

      * the faction each template declares (``Faction = FactionMen``);
      * the standards ``reinforcementbanners.ini`` defines (``Banner_Men``).

    Strip ``Faction`` from the first, prefix ``Banner_`` to it, and the two sets
    correspond one-to-one with nothing left over on either side. That is an
    exact structural correspondence over retail's own enumeration, not a
    similarity judgement between two loose strings - and if it ever stops being
    a bijection this returns NOTHING and says why, rather than binding the ones
    that happen to line up.

    (Retail's own faction spellings do not all match the banner spellings -
    ``FactionElves`` against ``Banner_Elf``, ``FactionDwarves`` against
    ``Banner_Dwarf`` - so the plural is folded too. That fold is stated here and
    is part of what the bijection test has to survive.)
    """

    singulars = {"elves": "Elf", "dwarves": "Dwarf"}
    available = {
        key: record.id
        for key, record in images.items()
        if record.id.casefold().startswith("banner_")
    }
    bound: dict[str, str] = {}
    for template in templates:
        faction = str(template.get("faction", ""))
        if not faction.casefold().startswith("faction"):
            continue
        # RETAIL'S OWN TEST FOR A PLAYABLE FACTION, not this module's: a template
        # that names no `BuildPlotIconName` cannot build, hold territory or field
        # an army. RotWK ships exactly one - `PlayerObserver`, whose faction is
        # `FactionObserver` - and it has no standard because it never takes the
        # field. Excluding it is what makes the remaining correspondence total.
        if not str(template.get("buildPlotIconName", "")):
            continue
        stem = faction[len("Faction") :]
        stem = singulars.get(stem.casefold(), stem)
        candidate = f"Banner_{stem}".casefold()
        if candidate not in available:
            return {}, (
                f"no banner for {faction}: {candidate} is not among "
                f"{len(available)} Banner_* images, so the correspondence is not "
                "total and NONE is bound"
            )
        bound[str(template["name"])] = available[candidate]
    used = {value.casefold() for value in bound.values()}
    if len(used) != len(bound):
        return {}, "two templates would share one banner, so the map is not a bijection"
    leftover = sorted(available[key] for key in available if key not in used)
    if leftover:
        return {}, (
            "unbound banner(s) " + ", ".join(leftover) + "; the correspondence is "
            "not a bijection, so NONE is bound"
        )
    return dict(sorted(bound.items())), (
        f"derived: Faction<X> -> Banner_<X>, a total {len(bound)}-to-{len(bound)} "
        "bijection over retail's own faction enumeration"
    )


def _read_chrome_sheet(reader: CatalogReader, output: pathlib.Path) -> dict[str, Any]:
    """Copy retail's strategic sheet out verbatim and derive its islands."""

    entry = reader.resolve(CHROME_SHEET)
    if entry is None:
        return {
            "file": "",
            "virtualPath": CHROME_SHEET,
            "reason": "not in the catalog",
            "components": [],
        }
    payload = reader.read(entry)
    width, height, bgra = _tga_bgra(payload)
    (output / ATLAS_DIRECTORY).mkdir(parents=True, exist_ok=True)
    (output / ATLAS_DIRECTORY / CHROME_SHEET_FILE).write_bytes(payload)
    return {
        "file": CHROME_SHEET_FILE,
        "virtualPath": CHROME_SHEET,
        "archive": entry.archive,
        "bytes": len(payload),
        "sha256": hashlib.sha256(payload).hexdigest(),
        "width": width,
        "height": height,
        "reason": "",
        # DERIVED from retail's own alpha channel, not authored anywhere.
        "derivation": (
            "connected components of pixels with alpha > "
            f"{CHROME_ALPHA_FLOOR}, on a 4-pixel grid, largest first"
        ),
        "components": _alpha_components(width, height, bgra),
    }


# --- the bundle ----------------------------------------------------------------


def build_bundle(
    catalog_path: pathlib.Path | str,
    output_root: pathlib.Path | str,
) -> dict[str, Any]:
    """Write the living-world UI bundle and return its manifest."""

    reader = CatalogReader(catalog_path)
    output = pathlib.Path(output_root)
    output.mkdir(parents=True, exist_ok=True)

    gaps: list[Gap] = []
    buildings = _read_buildings(reader, gaps)
    templates = _read_templates(reader, gaps)
    plot_icons = _read_marker_families(
        reader, BUILD_PLOT_ICONS_PATH, "LivingWorldBuildPlotIcon", gaps
    )
    building_icons = _read_marker_families(
        reader, BUILDING_ICONS_PATH, "LivingWorldBuildingIcon", gaps
    )

    # THE EXACT ID SET the living world names, and nothing else. Asking for the
    # whole 3,145-entry corpus would resolve everything and prove nothing about
    # whether this screen's icons are present.
    wanted: set[str] = set()
    army_portraits: dict[str, str] = {}
    hero_portraits: dict[str, str] = {}
    for building in buildings:
        image = str(building["constructButtonImage"])
        if image:
            wanted.add(image)
        for recruit in building["recruits"]:
            recruit_image = str(recruit["constructButtonImage"])
            if not recruit_image:
                continue
            wanted.add(recruit_image)
            army = str(recruit["playerArmy"]).casefold()
            if army:
                army_portraits.setdefault(army, recruit_image)
            hero = str(recruit["heroTemplateName"]).casefold()
            if hero:
                hero_portraits.setdefault(hero, recruit_image)
    for template in templates:
        for field in _TEMPLATE_IMAGE_FIELDS:
            value = str(template[field])
            if value:
                wanted.add(value)
    wanted.update(CHROME_IMAGE_IDS)

    by_id, missing_ids, ambiguous_ids = resolve_images(reader, wanted)
    faction_banners, banner_note = _faction_banners(templates, by_id)

    # Atlases: resolve each distinct texture to its compiled leaf, then copy the
    # bytes out verbatim. An atlas that does not resolve takes its crops with it
    # - they are recorded without a file, never against a substitute image.
    catalog_names = sorted(
        entry.name for entry in reader._winners.values()  # noqa: SLF001
    )
    texture_paths, unresolved_textures = resolve_mapped_image_texture_paths_partial(
        by_id.values(), catalog_names
    )
    if len(texture_paths) > MAX_ATLASES:
        raise LivingWorldUiError(
            f"{len(texture_paths)} atlases requested, over the {MAX_ATLASES} limit"
        )

    atlas_directory = output / ATLAS_DIRECTORY
    atlas_directory.mkdir(parents=True, exist_ok=True)
    atlases: list[dict[str, Any]] = []
    atlas_file_by_texture: dict[str, str] = {}
    for texture_key in sorted(texture_paths):
        virtual_path = texture_paths[texture_key]
        entry = reader.resolve(virtual_path)
        if entry is None:  # pragma: no cover - the resolver checked the catalog
            unresolved_textures = (*unresolved_textures, texture_key)
            continue
        payload = reader.read(entry)
        if len(payload) > MAX_ATLAS_BYTES:
            raise LivingWorldUiError(
                f"{virtual_path} is {len(payload)} bytes, over the "
                f"{MAX_ATLAS_BYTES} limit"
            )
        extension = pathlib.Path(virtual_path).suffix.casefold()
        file_name = f"{_atlas_file_name(texture_key)}{extension}"
        (atlas_directory / file_name).write_bytes(payload)
        # Keyed CASEFOLDED. `resolve_mapped_image_texture_paths_partial` returns
        # the texture name exactly as the MappedImage block spelled it, and the
        # crops below look it up off `record.texture`; retail's own spellings
        # agree today, and keying on the fold means they never have to.
        atlas_file_by_texture[texture_key.casefold()] = file_name
        atlases.append(
            {
                "file": file_name,
                "texture": texture_key,
                "virtualPath": virtual_path,
                "archive": entry.archive,
                "bytes": len(payload),
                "sha256": hashlib.sha256(payload).hexdigest(),
            }
        )

    images: dict[str, Any] = {}
    crops_without_atlas: list[str] = []
    for key, record in sorted(by_id.items()):
        file_name = atlas_file_by_texture.get(record.texture.casefold(), "")
        if not file_name:
            crops_without_atlas.append(record.id)
        images[record.id] = {
            "atlas": file_name,
            "texture": record.texture,
            "textureWidth": record.texture_width,
            "textureHeight": record.texture_height,
            "left": record.left,
            "top": record.top,
            "right": record.right,
            "bottom": record.bottom,
        }

    chrome_sheet = _read_chrome_sheet(reader, output)

    manifest = {
        "schema": SCHEMA,
        "schemaVersion": SCHEMA_VERSION,
        "catalog": pathlib.Path(catalog_path).name,
        "atlasDirectory": ATLAS_DIRECTORY,
        "buildings": buildings,
        "playerTemplates": templates,
        "buildPlotIcons": plot_icons,
        "buildingIcons": building_icons,
        # RETAIL'S OWN LINKS, both of them authored fields rather than
        # resemblances: a strategic army's portrait is the one retail draws on
        # the button that recruits that same `PlayerArmy`, and failing that the
        # one it draws for that same `HeroTemplateName`.
        "armyPortraits": dict(sorted(army_portraits.items())),
        "heroPortraits": dict(sorted(hero_portraits.items())),
        # DERIVED, and kept in its own field with its own note so the Godot side
        # can say which of its art is bound by an authored link and which by a
        # structural correspondence this module worked out.
        "factionBanners": faction_banners,
        "factionBannerDerivation": banner_note,
        "images": images,
        "atlases": atlases,
        "chromeSheet": chrome_sheet,
        "gaps": {
            "missingImageIds": sorted(missing_ids),
            "ambiguousImageIds": sorted(ambiguous_ids),
            "unresolvedAtlases": sorted(unresolved_textures),
            "cropsWithoutAtlas": sorted(crops_without_atlas),
            "documents": [gap.public() for gap in gaps],
        },
        "totals": {
            "buildings": len(buildings),
            "recruits": sum(len(row["recruits"]) for row in buildings),
            "nuggets": sum(len(row["nuggets"]) for row in buildings),
            "nuggetsOk": sum(row["nuggetsStatus"] == "ok" for row in buildings),
            "nuggetsRefused": sum(row["nuggetsStatus"] == "refused" for row in buildings),
            "playerTemplates": len(templates),
            "buildPlotIcons": len(plot_icons),
            "buildingIcons": len(building_icons),
            "imageIdsRequested": len(wanted),
            "imageIdsResolved": len(images),
            "atlases": len(atlases),
            "atlasBytes": sum(int(row["bytes"]) for row in atlases),
            "factionBanners": len(faction_banners),
            "chromeComponents": len(chrome_sheet["components"]),
        },
    }
    (output / MANIFEST_NAME).write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    return manifest


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        prog="python -m openbfme_importer.living_world_ui",
        description=(
            "Convert retail's War of the Ring UI surface - buildings, recruits, "
            "player-template icons and the MappedImage atlas crops behind them - "
            "into a Godot bundle."
        ),
    )
    parser.add_argument("--catalog", required=True, type=pathlib.Path)
    parser.add_argument("--out", required=True, type=pathlib.Path)
    args = parser.parse_args(argv)

    manifest = build_bundle(args.catalog, args.out)
    totals = manifest["totals"]
    gaps = manifest["gaps"]
    print(
        f"wrote {args.out} - {totals['buildings']} buildings, "
        f"{totals['recruits']} recruit entries, {totals['playerTemplates']} player "
        f"templates, {totals['imageIdsResolved']}/{totals['imageIdsRequested']} "
        f"image ids resolved across {totals['atlases']} atlases "
        f"({totals['atlasBytes']} bytes)"
    )
    for label, key in (
        ("IMAGE IDS WITH NO MappedImage BLOCK", "missingImageIds"),
        ("IMAGE IDS DEFINED TWICE", "ambiguousImageIds"),
        ("ATLASES NOT IN THE CATALOG", "unresolvedAtlases"),
        ("CROPS WITH NO ATLAS BEHIND THEM", "cropsWithoutAtlas"),
    ):
        rows = gaps[key]
        if rows:
            print(f"  {label} ({len(rows)}): " + ", ".join(rows))
    if gaps["documents"]:
        print(f"  document gaps: {len(gaps['documents'])}")
    return 0


if __name__ == "__main__":  # pragma: no cover - CLI entry point
    raise SystemExit(main())
