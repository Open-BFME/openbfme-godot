#!/usr/bin/env python3
"""Inventory of every place RotWK 2.01 shows a text on hover (lane HOVER-1), read from the player's own game files.

Reads the pure retail mount (tools/fx/bigfs.py: ROTWK_INSTALL then BFME2_INSTALL, the pinned archive order) and writes, to --out:
  places.csv / places.md     one row per kind of hover text: where retail shows it, the RotWK code that builds it, the label family its text comes
                             from, the port's state before and after lane HOVER-1 and the stop that names what is still missing;
  buttons.csv                every (command set, slot, command button) of data\\ini\\commandset.ini with its Command, TextLabel / DescriptLabel (and
                             whether the string file has them), the RW 0x807A81 branch that fills the help's cost line and the field the number comes from;
  factions.csv               per playable faction (PlayerTemplate PlayableSide): the builder, the main structures (the builder's DOZER_CONSTRUCT buttons
                             and the fortress), the heroes (BuildableHeroesMP), the spell book set (SpellBookMp) and their button counts;
  menus.csv                  every "TOOLTIP:<movie>/<path>" label of the string file (the menus' tool tips, stops S-173 / S-4610) classed against the Apt
                             movies' hover areas: a static area (directly or through AptButtonTooltipMap.ini), a button of that name only in an export
                             (attached at run time), or no button of any movie; the areas come from --apt-areas, the CSV the engine's Apt walk writes
                             (engine/tests/test_hover1_menus.cpp "hover1 inventory", OPENBFME_HOVER_INVENTORY_OUT=<dir>: GameClient/GUI/AptHoverInventory
                             walks every timeline of all 86 movies through their imports and exports);
  labels.csv                 the fixed labels the help code fetches (cost lines, unit help, warnings) and whether the string file has each;
  readouts.csv               the control bar's readout and menu buttons (every NonCommand_* CommandButton: resources, resource multiplier, command
                             points, options, ...) with the title and description lotr.str gives them and whether the port hovers them;
  sources.csv                every other tooltip source, named: the native gadgets' TOOLTIPTEXT / TOOLTIPCALLBACK of every .wnd, the INI fields that
                             carry a tooltip (Upgrade Tooltip, TooltipName, OverrideTooltip, BackButtonTooltip), and every tooltip label the retail
                             code names (the TOOLTIP:* / STRATEGICHUD:* / ... literals of game.dat; the lobby's TOOLTIP:StartPosition[N], the quit menu,
                             the Create-a-Hero builder, the in-game unit tips, the War of the Ring HUD), each with its family, the string it resolves to in
                             lotr.str (a row that resolves to nothing without a named reason is refused), the stop that names what is not ported
                             and the engine files that fetch it. The tool REFUSES (exit 2, nothing written) an empty or incomplete
                             input: an areas file without its movie list, a movie of the install missing from it, a tooltip source the tables do not
                             name, a table entry the install no longer has.
Nothing retail is stored in git: the tool reads the installs at run time; its output belongs under workspace/ (git-ignored).

  OPENBFME_HOVER_INVENTORY_OUT=<dir> openbfme_tests -tc="hover1 inventory*"     (writes <dir>/apt_hover_areas.csv)
  python3 tools/hover/hover_inventory.py --apt-areas <dir>/apt_hover_areas.csv --out workspace/rebuild/proof/HOVER-1
"""
from __future__ import annotations

import argparse
import csv
import os
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent.parent

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "fx"))
import bigfs  # noqa: E402

# the label families the help code fetches (CommandButtonHelp.cpp RW 0x807A81, UnitHelpSource.cpp RW 0x8EBC61, InGameCommandButtonHelp RW 0x974EEB)
FIXED_LABELS = [
    "TOOLTIP:Cost", "TOOLTIP:CostFree", "TOOLTIP:CommandPoints", "TOOLTIP:ScienceCost", "TOOLTIP:UnitCost", "TOOLTIP:Shortcut",
    "TOOLTIP:BuildDisabled", "TOOLTIP:ScienceDisabled", "TOOLTIP:TooltipNotEnoughMoneyToBuild", "TOOLTIP:TooltipCannotPurchaseBecauseQueueFull",
    "TOOLTIP:AlreadyUpgradedDefault", "TOOLTIP:HasConflictingUpgradeDefault", "TOOLTIP:LacksPrerequisiteUpgradeDefault", "CONTROLBAR:Requirements",
    "CONTROLBAR:GenericReviveHero", "CONTROLBAR:GenericRecruitHero", "CONTROLBAR:ToolTipGenericReviveHero", "CONTROLBAR:ToolTipGenericRecruitHero",
    "APT:RankLabel", "TOOLTIP:Rank", "TOOLTIP:MaxHealth", "TOOLTIP:MeleeDamage", "TOOLTIP:RangedDamage", "TOOLTIP:BattlesFought",
    "TOOLTIP:VerteranHordeMemberName", "APT:SpellHelpText", "APT:SpellDescription",
]

# the kinds of hover text (the port's facts; the counts of each come from the data below). state: ok / stop (S-...) ; before -> after
PLACES = [
    # (place, retail code, text source, before, after, stop)
    ("Help box: command buttons of the Palantir arc, the side bar and the radial bubbles", "RW 0x807848 -> 0x807A81, InGameCommandButtonHelp RW 0x974EEB",
     "CommandButton TextLabel / DescriptLabel, TOOLTIP:Cost / CostFree / CommandPoints / Shortcut", "ok (name, description, build / upgrade cost)",
     "ok", ""),
    ("Help box: hero ability (SPECIAL_POWER) cost line", "RW 0x808675 (RotWK only)", "TOOLTIP:UnitCost with SpecialPower UnitCost", "missing", "ok", ""),
    ("Help box: revive / recruit a hero (REVIVE)", "RW 0x8083EF", "CONTROLBAR:GenericReviveHero / GenericRecruitHero, ReviveText / RecruitText, APT:RankLabel, TOOLTIP:Cost",
     "missing", "ok (Create-a-Hero name: stop)", "S-4612"),
    ("Help box: castle unpack cost (CASTLE_UNPACK)", "RW 0x80863C", "TOOLTIP:Cost with the castle's unpack cost", "missing", "ok", ""),
    ("Help box: repair cost (START_SELF_REPAIR / START_NEIGHBORHOOD_REPAIR)", "RW 0x807BFD", "TOOLTIP:Cost with the repair interface's cost", "missing",
     "stop", "S-4612"),
    ("Help box: science pick and cost (PURCHASE_SCIENCE, Science = several)", "RW 0x807B29", "TOOLTIP:ScienceCost", "single science only", "ok", ""),
    ("Help box: upgrade conflicting / lacks prerequisite / already upgraded", "RW 0x808144", "Button + 0x70 / 0x74 / 0x78 labels or the TOOLTIP defaults",
     "already-upgraded only", "ok", ""),
    ("Help box: the spell book's powers (InGameSpellBook)", "RW 0x930DC4 -> 0x807A00", "the power's CommandButton labels, TOOLTIP:ScienceDisabled", "none", "ok", ""),
    ("Help box: the hero bar's hero portraits", "RW 0x92BF34 -> UnitHelpSource RW 0x8EBC61, InGameSimpleHelp RW 0x9402E7",
     "DisplayName, TOOLTIP:Rank / MaxHealth / MeleeDamage / RangedDamage, upgrade Tooltip, Description", "none",
     "ok (battles fought, script captions, disguise, Create-a-Hero name: stop)", "S-4612"),
    ("Help box: the hero bar's builder button", "RW 0x92BF34 -> NonCommand_SelectNearestBuilder", "its CommandButton labels", "none", "ok", ""),
    ("Help box: the hero bar's select-all button", "RW 0x92C265 -> NonCommand_SelectAllHeroes", "its CommandButton labels", "none", "ok", ""),
    ("Spell store (SpellStore.apt) power help", "RW 0x822E43 (APT:SpellHelpText / APT:SpellDescription)", "the store button's TextLabel / DescriptLabel",
     "ok (lane SPELL-2)", "ok", ""),
    ("Menus' tool tips: main menu, Options, Skirmish, the online screens, the quit menu (front end and in game)",
     "RW 0x624C68 -> RW 0x6235E2 -> Mouse RW 0x5EE1A6 / 0x5EE7A8 -> GuiFX ShowToolTip RW 0x782DD9",
     "TOOLTIP:<movie>/<path> through AptButtonTooltipMap.ini (shown only when the string file has it)", "label computed (the parent clip's path), never drawn",
     "ok (26 labels on screen); My Heroes / Quit: stop (the menu leaves their clips on frame 0)", "S-4610"),
    ("Help box: the Palantir's portrait of the selected unit", "PalantirCommandUI over-button \"CommandUI/PortraitBackground\" RW 0x93099D -> UnitHelpSource",
     "as the hero bar's portraits", "none", "ok", ""),
]


class InventoryError(Exception):
    """An input the inventory cannot be complete from: nothing is written."""


# the engine's Apt walk writes these two files (engine/tests/test_hover1_menus.cpp "hover1 inventory")
AREAS_HEADER = ["movie", "path", "from export", "placed at frames", "label", "in string file"]
MOVIES_HEADER = ["movie", "areas"]


def _read_csv(path: Path, header: list[str], what: str) -> list[dict[str, str]]:
    if not path.is_file():
        raise InventoryError(f"{what} {path} does not exist")
    with open(path, newline="") as f:
        reader = csv.DictReader(f)
        if reader.fieldnames != header:
            raise InventoryError(f"{what} {path} has the columns {reader.fieldnames}, expected {header}")
        rows = list(reader)
    if not rows:
        raise InventoryError(f"{what} {path} has no rows")
    return rows


def movie_key(name: str) -> str:
    return name.lower().removesuffix(".apt")


def load_areas(areas_csv: Path, install_movies: list[str]) -> list[dict[str, str]]:
    """The Apt walk's hover areas, refused unless the walk covered every movie of the install: the movie list file next to the areas lists exactly the
    install's movies and each movie's area count matches the rows."""
    rows = _read_csv(areas_csv, AREAS_HEADER, "the hover areas file")
    movies = _read_csv(areas_csv.with_name("apt_movies.csv"), MOVIES_HEADER, "the walked movies file")
    expected = {movie_key(m) for m in install_movies}
    if not expected:
        raise InventoryError("the install lists no Apt movie")
    walked = {movie_key(r["movie"]): int(r["areas"]) for r in movies}
    if len(walked) != len(movies):
        raise InventoryError("the walked movies file names a movie twice")
    if set(walked) != expected:
        missing = sorted(expected - set(walked))
        extra = sorted(set(walked) - expected)
        raise InventoryError(f"the walk does not cover the install's movies: missing {missing[:5]}, not in the install {extra[:5]}")
    counts: dict[str, int] = {}
    for r in rows:
        counts[movie_key(r["movie"])] = counts.get(movie_key(r["movie"]), 0) + 1
    for m, n in walked.items():
        if counts.get(m, 0) != n:
            raise InventoryError(f"movie {m}: the walked file says {n} areas, the areas file has {counts.get(m, 0)}")
    if not any(r["from export"] == "no" and r["label"] for r in rows):
        raise InventoryError("the areas file has no labelled static area")
    return rows


# the engine's tooltip fields of the INI files (data\ini\**): field -> (family, stop). A field of that spelling the install has and this table lacks is refused.
INI_FIELDS = {
    "Tooltip": ("Upgrade Tooltip text of a unit's help (UnitHelpSource)", ""),
    "TooltipName": ("multiplayer.ini colour tool tip of the lobby's colour combo", "S-4610"),
    "OverrideTooltip": ("object name shown by the tool tip (ReferenceDisplayName)", "S-4612"),
    "BackButtonTooltip": ("War of the Ring HUD back button", "S-4612"),
}
# Mouse.ini / Language.ini presentation of the tool tip box itself (CursorTooltipSettings)
INI_SETTINGS = {"TooltipFontName", "TooltipFontSize", "TooltipFontIsBold", "TooltipAnimateBackground", "TooltipFillTime", "TooltipDelayTime",
                "TooltipTextColor", "TooltipHighlightColor", "TooltipShadowColor", "TooltipBorderColor", "TooltipBackgroundColor", "TooltipWidth",
                "UseTooltipAltTextColor", "UseTooltipAltBackColor", "AdjustTooltipAltColor"}

# the retail code's own names of the mechanism (INI field / function / window field names, not a text): game.dat strings with "tooltip" in them
CODE_MECHANISM = {
    "AdjustTooltipAltColor", "AptButtonTooltipMap", "HideToolTip", "MoveToolTip", "OverrideTooltip", "ShowToolTip", "ShowTooltips", "TOOLTIP",
    "TOOLTIP:", "TOOLTIPCALLBACK", "TOOLTIPDELAY", "TOOLTIPTEXT", "ToolTipCachedStats", "ToolTipText", "Tooltip", "TooltipAnimateBackground",
    "TooltipBackgroundColor", "TooltipBorderColor", "TooltipDelayTime", "TooltipFillTime", "TooltipFontIsBold", "TooltipFontName", "TooltipFontSize",
    "TooltipHighlightColor", "TooltipName", "TooltipShadowColor", "TooltipStringTag", "TooltipTextColor", "TooltipUpgrade", "TooltipWidth",
    "UseMapListTooltips", "UseTooltipAltBackColor", "UseTooltipAltTextColor",
}

# every tooltip text the retail code names: family -> (stop of what is not fetched by the port, [labels]). A label of game.dat that is in no family is
# refused, and so is a listed one game.dat no longer has.
HELP_BOX = ["TOOLTIP:Cost", "TOOLTIP:CostFree", "TOOLTIP:CommandPoints", "TOOLTIP:ScienceCost", "TOOLTIP:UnitCost", "TOOLTIP:Shortcut", "TOOLTIP:BuildDisabled",
            "TOOLTIP:ScienceDisabled", "TOOLTIP:TooltipNotEnoughMoneyToBuild", "TOOLTIP:TooltipCannotPurchaseBecauseQueueFull", "TOOLTIP:AlreadyUpgradedDefault",
            "TOOLTIP:HasConflictingUpgradeDefault", "TOOLTIP:LacksPrerequisiteUpgradeDefault", "CONTROLBAR:ToolTipGenericRecruitHero",
            "CONTROLBAR:ToolTipGenericReviveHero", "TOOLTIP:Rank", "TOOLTIP:MaxHealth", "TOOLTIP:MeleeDamage", "TOOLTIP:RangedDamage", "TOOLTIP:BattlesFought",
            "TOOLTIP:VerteranHordeMemberName"]
CODE_FAMILIES = {
    "help box (command button and unit help lines)": ("S-4612", HELP_BOX),
    "in-game unit tips (disband, rename, supply warehouse, special ability)": ("S-4612", [
        "TOOLTIP:AddSpecialAbilityNoPower", "TOOLTIP:ClickToRenameThisUnit", "TOOLTIP:DisbandUnit", "TOOLTIP:DisbandUnitDisabled",
        "TOOLTIP:DisbandUnitDisabledHero", "TOOLTIP:DisbandUnitDisabledLeader", "TOOLTIP:SupplyWarehouse", "TOOLTIP:YouMayNotRenameAHero"]),
    "lobby, staging and map list tips (start positions, players, map success, advanced settings)": ("S-4610", [
        "TOOLTIP:StartPosition", "TOOLTIP:StartPositionN", "TOOLTIP:GameStarted", "TOOLTIP:BuddyPlayer", "TOOLTIP:ChatPlayerInfo",
        "TOOLTIP:StagingPlayerInfo", "TOOLTIP:GenericPlayer", "TOOLTIP:LocalPlayer", "TOOLTIP:ProfiledPlayer", "TOOLTIP:LadderRankUnavailable",
        "TOOLTIP:PasswordIcon", "TOOLTIP:MpSaveGame", "TOOLTIP:AdvSetting", "TOOLTIP:MapNoSuccess", "TOOLTIP:MapEasySuccess", "TOOLTIP:MapMediumSuccess",
        "TOOLTIP:MapHardSuccess", "TOOLTIP:MapHardMaxSuccess", "TOOLTIP:UserMapIcon", "Skirmish/tooltipPlayerLevelIcon"]),
    "quit menu tips": ("S-4610", ["TOOLTIP:QuitMenu/Forfeit/WOTRForfeit", "TOOLTIP:QuitMenu/Restart/TheButton", "TOOLTIP:QuitMenu/Surrender/WOTRSurrender"]),
    "Create-a-Hero builder tips (power states, matrix, palantir)": ("S-4610", [
        "TOOLTIP:CAH_CURRENT_POWER", "TOOLTIP:CAH_IN_PALANTIR", "TOOLTIP:CAH_IS_AVAILABLE", "TOOLTIP:CAH_IS_SELECTED", "TOOLTIP:CAH_LEVEL_TOO_LOW",
        "TOOLTIP:CAH_NEEDS_PREREQ", "TOOLTIP:CAH_NO_CURRENT_POWER", "TOOLTIP:CAH_PALANTIR_EMPTY_SLOT", "TOOLTIP:CAH_PALANTIR_FULL",
        "TOOLTIP:CAH_POWER_BOOK_FULL", "AptCreateAHero::MatrixToolTip", "AptCreateAHero::MyPowerToolTip", "AptCreateAHero::PalantirToolTip"]),
    "War of the Ring HUD tips (strategic hud, battle prompt)": ("S-4612", [
        "APT:StrategicBattlePromptSwapButton1Tooltip", "APT:StrategicBattlePromptSwapButton2Tooltip", "STRATEGICHUD:ArcherUnitArchetypeTooltip",
        "STRATEGICHUD:ArmoryStructureArchetypeTooltip", "STRATEGICHUD:ArmyCurrentOverMaxCommandPointsTooltip", "STRATEGICHUD:BarracksStructureArchetypeTooltip",
        "STRATEGICHUD:CavalryUnitArchetypeTooltip", "STRATEGICHUD:ConstructionTurnsRemainingTooltip", "STRATEGICHUD:FarmStructureArchetypeTooltip",
        "STRATEGICHUD:FortressStructureArchetypeTooltip", "STRATEGICHUD:HeroUnitArchetypeTooltip", "STRATEGICHUD:InfantryUnitArchetypeTooltip",
        "STRATEGICHUD:PikemanUnitArchetypeTooltip", "STRATEGICHUD:RegionAttackBonusTooltip", "STRATEGICHUD:RegionCommandPointBonusTooltip",
        "STRATEGICHUD:RegionDefenseBonusTooltip", "STRATEGICHUD:RegionExperienceBonusTooltip", "STRATEGICHUD:RegionPowerPointBonusTooltip",
        "STRATEGICHUD:RegionResourceBonusTooltip", "STRATEGICHUD:RegionTreasuryIncomeBonusTooltip", "STRATEGICHUD:SiegeUnitArchetypeTooltip",
        "STRATEGICHUD:SupportUnitArchetypeTooltip", "BackButtonTooltip"]),
}
# the labels of the help box the port fetches and shows (tests: test_hover1.cpp), the rest of the family names a stop
STOP_LABELS = {"TOOLTIP:BattlesFought", "TOOLTIP:VerteranHordeMemberName"}


def strip_comment(line: str) -> str:
    for mark in (";", "//"):
        i = line.find(mark)
        if i >= 0:
            line = line[:i]
    return line


def blocks(text: str, keyword: str):
    """Yield (name, {field: [values...]}) of the top-level `keyword <name>` blocks (fields keep every occurrence)."""
    name, fields, depth = None, {}, 0
    for raw in text.splitlines():
        line = strip_comment(raw).strip()
        if not line:
            continue
        tok = line.split()
        if name is None:
            if tok[0] == keyword:  # a block without a name (AptButtonTooltipMap) is ""
                name, fields, depth = (tok[1] if len(tok) > 1 else ""), {}, 0
            continue
        if tok[0].lower() == "end":
            if depth == 0:
                yield name, fields
                name = None
            else:
                depth -= 1
            continue
        if "=" in line:
            if depth == 0:  # a nested block's fields are not the block's
                k, v = line.split("=", 1)
                fields.setdefault(k.strip(), []).append(v.strip())
        else:
            depth += 1  # a nested block


def string_table(raw: bytes) -> dict[str, str]:
    """data\\lotr.str: LABEL, then one quoted line (or several), then END."""
    out: dict[str, str] = {}
    label = None
    text = None
    for line in raw.decode("latin-1").splitlines():
        s = line.strip()
        if label is None:
            if s and not s.startswith("//"):
                label, text = s, None
            continue
        if s.upper() == "END":
            out[label.lower()] = text or ""  # labels compare case-insensitively (GameTextTable::lookup, ZH GameText's compareLUT)
            label = None
            continue
        if s.startswith('"'):
            text = (text or "") + s.strip('"')
    return out


def wnd_tooltips(files: dict[str, str]) -> list[dict[str, str]]:
    """The native gadgets of the .wnd files that carry a tooltip: a TOOLTIPTEXT, or a TOOLTIPCALLBACK other than the no-op ones. `files` maps a file name to
    its text. Refused when the files hold no gadget at all (a parse that found nothing is not an inventory)."""
    rows = []
    gadgets = 0
    for path, text in sorted(files.items()):
        name = ""
        for line in text.splitlines():
            code = strip_comment(line).strip()
            m = re.match(r'NAME\s*=\s*"([^"]*)"', code)
            if m:
                name = m.group(1)
                gadgets += 1
                continue
            m = re.match(r'(?i)(TOOLTIPTEXT|TOOLTIPCALLBACK)\s*=\s*"([^"]*)"', code)
            if not m:
                continue
            kind, value = m.group(1).upper(), m.group(2)
            if kind == "TOOLTIPCALLBACK" and value in ("[None]", "Apt:None"):
                continue
            rows.append({"window": path, "gadget": name, "field": kind, "value": value})
    if not gadgets:
        raise InventoryError("the .wnd files hold no gadget (NAME = ...)")
    return rows


def ini_tooltip_fields(files: dict[str, str]) -> dict[str, list[tuple[str, str]]]:
    """INI field -> [(file, value)] for every field spelled like a tooltip; refused when `files` is empty. A field in neither INI_FIELDS nor
    INI_SETTINGS is refused: an unnamed tooltip source."""
    if not files:
        raise InventoryError("no INI file to read the tooltip fields from")
    found: dict[str, list[tuple[str, str]]] = {}
    for path, text in sorted(files.items()):
        for line in text.splitlines():
            m = re.match(r"^\s*([A-Za-z_]*[Tt]ool\s?[Tt]ip[A-Za-z_]*)\s*=\s*(.*)$", strip_comment(line))
            if m:
                found.setdefault(m.group(1), []).append((path, m.group(2).strip()))
    for field in found:
        if field not in INI_FIELDS and field not in INI_SETTINGS:
            raise InventoryError(f"the INI field {field} ({found[field][0][0]}) is a tooltip source this tool does not name")
    return found


def dat_noncommand_names(dat: bytes) -> set[str]:
    """The NonCommand_* button names the retail executable holds (the control bar's readouts and menu buttons)."""
    if not dat:
        raise InventoryError("the executable is empty")
    return {m.group(0).decode("latin-1") for m in re.finditer(rb"NonCommand_[A-Za-z0-9_]*", dat)}


def code_tooltip_strings(dat: bytes) -> set[str]:
    """The strings of the retail executable that name a tooltip (printable runs with "tooltip" in them) apart from the mechanism's own names."""
    if not dat:
        raise InventoryError("the executable is empty")
    out = {m.group(0).decode("latin-1") for m in re.finditer(rb"[ -~]{6,}", dat) if re.search(rb"(?i)tool ?tip", m.group(0))}
    return out - CODE_MECHANISM


def classify_code_strings(found: set[str]) -> dict[str, str]:
    """label -> family; refused when the executable names a tooltip label no family lists, or a family lists one the executable does not have."""
    by_label = {label: fam for fam, (_, labels) in CODE_FAMILIES.items() for label in labels}
    if len(by_label) != sum(len(v[1]) for v in CODE_FAMILIES.values()):
        raise InventoryError("a tooltip label is listed in two families")
    unnamed = sorted(found - set(by_label))
    if unnamed:
        raise InventoryError(f"{len(unnamed)} tooltip strings of the executable are in no family: {unnamed[:5]}")
    # the labels the executable builds from a prefix (a format) are not whole strings of it; the lists name them
    gone = sorted(l for l in by_label if l not in found and l not in CODE_DERIVED)
    if gone:
        raise InventoryError(f"{len(gone)} listed tooltip labels are not in the executable: {gone[:5]}")
    return by_label


# labels of the families the executable does not hold as a whole string (a format of its own prefix); none at this install, kept so a table can say so
CODE_DERIVED: set[str] = set()


def port_refs(repo: Path, label: str) -> list[str]:
    """The engine / script files that name `label` in a line that is not a comment (the files that fetch it)."""
    needle = label.lower()
    out = []
    for root in ("engine/src", "godot/scripts"):
        for path in sorted((repo / root).rglob("*")):
            if path.suffix not in (".cpp", ".h", ".gd") or not path.is_file():
                continue
            for line in path.read_text(errors="replace").splitlines():
                code = line.strip()
                if code.startswith("//") or code.startswith("#") or code.startswith("*"):
                    continue
                if needle in code.lower():
                    out.append(str(path.relative_to(repo)).replace("\\", "/"))
                    break
    return out


# the labels the data names that the string file lacks: retail shows its missing-label marker for them (the pinned set: a different set is refused)
EXPECTED_MISSING = {
    "color:gray", "color:white", "tooltip:arnorflamingarrows", "tooltip:arnorforgedblades", "tooltip:arnorheavyarmor", "tooltip:arnorknightshields",
    "tooltip:arnorfirestones", "tooltip:loadprogress",
}
# an INI value that is a keyword, not a label
INI_KEYWORDS = {("OverrideTooltip", "ReferenceDisplayName"): "the object's own DisplayName text"}
# rows whose text the data does not hold: a window callback that shows the text the code set, an Apt path whose label a window manager callback returns
CODE_SET = {"GameWinDefaultTooltip", "Skirmish/tooltipPlayerLevelIcon"}
# a code name that is not a label: the texts it shows are these rows' (an Apt command of the Create-a-Hero builder, an INI field name)
RESOLVES_VIA = {
    "AptCreateAHero::MatrixToolTip": "TOOLTIP:CAH_", "AptCreateAHero::MyPowerToolTip": "TOOLTIP:CAH_", "AptCreateAHero::PalantirToolTip": "TOOLTIP:CAH_",
    "BackButtonTooltip": "STRATEGICHUD:",
}
PLACEHOLDERS = {"apt:none", "apt:null"}
SOURCE_HEADER = ["kind", "family", "where", "label / field", "resolved from", "text", "status", "port", "stop"]


def lookup(strings: dict[str, str], key: str) -> tuple[str, str] | None:
    """(label that resolved, its text): the key as a label, else as the TOOLTIP: path label of an Apt hover area."""
    for k in (key, "TOOLTIP:" + key):
        if k.lower() in strings:
            return k, strings[k.lower()]
    return None


def source_rows(wnd: list[dict[str, str]], ini: dict[str, list[tuple[str, str]]], code: dict[str, str], strings: dict[str, str], repo: Path) -> list[list[str]]:
    """sources.csv, SOURCE_HEADER: every source with the string it resolves to in lotr.str. A row that resolves to nothing is refused unless the data
    says why (a placeholder, a text set by code, a label the string file lacks: the pinned EXPECTED_MISSING)."""
    rows: list[list[str]] = []
    missing: set[str] = set()
    unresolved: list[str] = []

    def resolved(key: str) -> tuple[str, str, str]:
        hit = lookup(strings, key)
        if key.lower() in PLACEHOLDERS:
            if hit and hit[1].strip():
                unresolved.append(key)  # a placeholder label with a text is not a placeholder
            return "", "", "placeholder: no tool tip (its label is empty in the string file)"
        if hit and not hit[1].strip():
            unresolved.append(key)
            return hit[0], "", "UNRESOLVED"
        if hit:
            return hit[0], hit[1], "text"
        if key in CODE_SET:
            return "", "", "text set by the code, none in the data"
        if ":" in key and "::" not in key:
            missing.add(key.lower())
            return "", "", "label not in the string file (retail shows its missing-label marker)"
        unresolved.append(key)
        return "", "", "UNRESOLVED"

    for g in wnd:
        key = g["value"]
        if g["field"] == "TOOLTIPCALLBACK":
            label, text, status = ("", "", "text set by the code, none in the data") if key in CODE_SET else ("", "", "UNRESOLVED")
            if key not in CODE_SET:
                unresolved.append(key)
        else:
            label, text, status = resolved(key)
        rows.append(["native gadget", g["field"], f"{g['window']}:{g['gadget']}", key, label, text, status, "", "S-4610" if status == "text" else ""])
    for field in sorted(ini):
        family, stop = INI_FIELDS.get(field, ("Mouse.ini / Language.ini presentation of the tool tip box", "S-4610"))
        refs = port_refs(repo, field)
        port = ("read by " + ", ".join(refs)) if refs else "not read"
        for path, value in ini[field]:
            if field in INI_SETTINGS:
                label, text, status = "", value, "the setting's value"
            elif (field, value) in INI_KEYWORDS:
                label, text, status = "", INI_KEYWORDS[(field, value)], "keyword"
            else:
                label, text, status = resolved(value)
            rows.append(["INI field", family, path, f"{field} = {value}", label, text, status, port, stop if not refs else ""])
    for label in sorted(code):
        family = code[label]
        stop = CODE_FAMILIES[family][0]
        refs = port_refs(repo, label)
        if label in RESOLVES_VIA:
            texts = [v for k, v in sorted(strings.items()) if k.startswith(RESOLVES_VIA[label].lower())] if RESOLVES_VIA[label] != "STRATEGICHUD:" else \
                [strings[v.lower()] for f, vs in ini.items() if f == label for _, v in vs if v.lower() in strings]
            if not texts:
                unresolved.append(label)
            res, text, status = f"{RESOLVES_VIA[label]}* / the INI values", " | ".join(texts), f"resolves via {len(texts)} texts"
        else:
            res, text, status = resolved(label)
        if label in STOP_LABELS:
            state, row_stop = "stop", stop
        elif refs:
            state, row_stop = "fetched by " + ", ".join(refs), ""
        else:
            state, row_stop = "not fetched", stop
        rows.append(["retail code label", family, "game.dat", label, res, text, status, state, row_stop])
    if unresolved:
        raise InventoryError(f"{len(unresolved)} tooltip sources resolve to no text and no named reason: {sorted(set(unresolved))[:5]}")
    if missing != EXPECTED_MISSING:
        raise InventoryError(f"the labels the string file lacks changed: new {sorted(missing - EXPECTED_MISSING)}, gone {sorted(EXPECTED_MISSING - missing)}")
    return rows


# the control bar's own readouts and menu buttons: the NonCommand_* CommandButtons (named in no CommandSet; the control bar's windows show them)
NO_BUTTON_NAMED_BY_CODE = {"NonCommand_GeneralsExperience", "NonCommand_"}  # game.dat names these; no CommandButton of the install has them (the prefix itself)
PORT_HOVERED_READOUTS = {"NonCommand_SelectAllHeroes", "NonCommand_SelectNearestBuilder"}  # the hero bar's two (RW 0x92C265 / 0x92BF34; test_hover1.cpp)
READOUT_HEADER = ["button", "TextLabel", "title", "DescriptLabel", "description", "named by game.dat", "port", "stop"]


def readout_rows(buttons: dict[str, dict[str, list[str]]], strings: dict[str, str], dat_names: set[str], repo: Path) -> list[list[str]]:
    """readouts.csv: every NonCommand_* CommandButton of commandbutton.ini with the title and description lotr.str gives it. Refused: no such button,
    a label the string file lacks (outside EXPECTED_MISSING), a name game.dat holds that has no button (outside NO_BUTTON_NAMED_BY_CODE)."""
    names = sorted(n for n in buttons if n.startswith("NonCommand_"))
    if not names:
        raise InventoryError("commandbutton.ini has no NonCommand_ button (the control bar's readouts)")
    gone = sorted(dat_names - set(names) - NO_BUTTON_NAMED_BY_CODE)
    if gone:
        raise InventoryError(f"game.dat names NonCommand buttons the install lacks: {gone}")
    rows = []
    for n in names:
        b = buttons[n]
        text = (b.get("TextLabel") or [""])[0]
        desc = (b.get("DescriptLabel") or [""])[0]
        for label in (text, desc):
            if not label or (label.lower() not in strings and label.lower() not in EXPECTED_MISSING):
                raise InventoryError(f"{n}: the label {label!r} resolves to nothing")
        refs = port_refs(repo, n)
        hovered = n in PORT_HOVERED_READOUTS and bool(refs)
        rows.append([n, text, strings.get(text.lower(), ""), desc, strings.get(desc.lower(), ""), "yes" if n in dat_names else "no",
                     "hovered (hero bar): " + ", ".join(refs) if hovered else "not hovered", "" if hovered else "S-4612"])
    return rows


def object_command_sets(mount: bigfs.Mount) -> dict[str, str]:
    """Object / ChildObject name -> its CommandSet (the first CommandSet line of the block; a ChildObject without one keeps its parent's)."""
    out: dict[str, str] = {}
    parents: dict[str, str] = {}
    head = re.compile(r"^\s*(Object|ChildObject)\s+(\S+)(?:\s+(\S+))?", re.I)
    cs = re.compile(r"^\s*CommandSet\s*=\s*(\S+)", re.I)
    for path in mount.list("data\\ini\\object\\", ".ini"):
        cur = None
        for raw in mount.read(path).decode("latin-1").splitlines():
            line = strip_comment(raw)
            m = head.match(line)
            if m:
                cur = m.group(2)
                if m.group(1).lower() == "childobject" and m.group(3):
                    parents[cur] = m.group(3)
                continue
            m = cs.match(line)
            if m and cur and cur not in out:
                out[cur] = m.group(1)
    for child, parent in parents.items():
        if child not in out and parent in out:
            out[child] = out[parent]
    return out


def button_map(mount: bigfs.Mount) -> dict[str, str]:
    """data\\ini\\aptbuttontooltipmap.ini's "ButtonMap = <path> <name>" rows (the last one of a path wins)."""
    out: dict[str, str] = {}
    for _, fields in blocks(mount.read("data\\ini\\aptbuttontooltipmap.ini").decode("latin-1"), "AptButtonTooltipMap"):
        for v in fields.get("ButtonMap", []):
            parts = v.split()
            if len(parts) == 2:
                out[parts[0]] = parts[1]
    return out


def cost_branch(button: dict[str, list[str]]) -> tuple[str, str]:
    """The branch of RW 0x807A81 that fills the help's cost line and the field its number comes from."""
    cmd = (button.get("Command") or [""])[0].split()[0] if button.get("Command") else ""
    sciences = " ".join(button.get("Science", [])).split()
    if button.get("Object") and cmd not in ("PURCHASE_SCIENCE", "HORDE_SET_FORMATION") and not (
            button.get("Upgrade") and cmd in ("PLAYER_UPGRADE", "OBJECT_UPGRADE", "CASTLE_UPGRADE")):
        return "build cost", "Object BuildCost / CommandPoints"
    if button.get("Upgrade"):
        return "upgrade cost", "Upgrade BuildCost"
    if len(sciences) == 1 or (len(sciences) > 1 and cmd == "PURCHASE_SCIENCE"):
        return "science cost", "Science SciencePurchasePointCost(MP)"
    if cmd == "REVIVE":
        return "revive cost", "the hero record's cost"
    if cmd == "CASTLE_UNPACK":
        return "castle unpack cost", "CastleBehavior CastleToUnpackForFaction cost"
    if cmd == "SPECIAL_POWER":
        return "unit cost", "SpecialPower UnitCost (shown when not 0)"
    if cmd in ("START_SELF_REPAIR", "START_NEIGHBORHOOD_REPAIR"):
        return "repair cost (stop S-4612)", "the repair interface"
    return "none", ""


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", required=True, type=Path)
    ap.add_argument("--apt-areas", required=True, type=Path, help="apt_hover_areas.csv of the engine's Apt walk")
    args = ap.parse_args()
    try:
        return build(args)
    except (InventoryError, bigfs.MountError) as e:
        print(f"REFUSED: {e}", file=sys.stderr)
        return 2


def game_dat() -> bytes:
    env = os.environ.get("RW_GAME_DAT") or os.path.join(os.environ.get("ROTWK_INSTALL", ""), "game.dat")
    if not os.path.isfile(env):
        raise InventoryError(f"the retail executable {env!r} does not exist (RW_GAME_DAT or ROTWK_INSTALL/game.dat)")
    return Path(env).read_bytes()


def source_places(srows: list[list[str]], readouts: list[list[str]]) -> list[tuple]:
    """One PLACES row per family of retail code labels and for the native gadgets, its state from the engine files that fetch the labels."""
    out = []
    families: dict[str, list[list[str]]] = {}
    for r in srows:
        if r[0] == "retail code label":
            families.setdefault(r[1], []).append(r)
    for family, rows in families.items():
        if family.startswith("help box"):
            continue  # the command button / unit help lines: the first rows of PLACES, labels.csv
        fetched = sum(1 for r in rows if r[7].startswith("fetched"))
        stops = sorted({r[8] for r in rows if r[8]})
        if fetched == len(rows):
            after = f"labels fetched by the port ({fetched} of {len(rows)})"
        elif fetched:
            after = f"partial: {fetched} of {len(rows)} labels fetched by the port"
        else:
            after = "not ported"
        out.append((f"Tool tips: {family}", "game.dat strings (sources.csv)", f"{len(rows)} retail labels", "not inventoried", after, " ".join(stops)))
    gadgets = [r for r in srows if r[0] == "native gadget"]
    out.append(("Native gadgets with a tool tip (.wnd TOOLTIPTEXT / TOOLTIPCALLBACK: the experience screen, the load screen, the production queue, the "
                "minimap input window, the replay control)", "window\\*.wnd", f"{len(gadgets)} gadgets (sources.csv)", "not inventoried",
                "not ported (the .wnd gadgets are not built)", "S-4610"))
    hovered = sum(1 for r in readouts if r[6].startswith("hovered"))
    out.append(("Control bar readouts and menu buttons (the NonCommand_* CommandButtons: resources, multiplier, command points, options, idle worker, "
                "beacon, messenger, ...)", "commandbutton.ini NonCommand_*, named by game.dat", f"{len(readouts)} buttons (readouts.csv)", "not inventoried",
                f"partial: {hovered} of {len(readouts)} hovered (the hero bar's two)", "S-4612"))
    return out


def build(args) -> int:
    mount = bigfs.Mount()
    out: Path = args.out
    strings = string_table(mount.read("data\\lotr.str"))
    # everything is read and validated before anything is written: an empty or incomplete input is refused
    area_rows = load_areas(args.apt_areas, [Path(p).name for p in mount.list("", ".apt")])
    wnd = wnd_tooltips({p: mount.read(p).decode("latin-1") for p in mount.list("", ".wnd")})
    ini = ini_tooltip_fields({p: mount.read(p).decode("latin-1") for p in mount.list("", ".ini")})
    code = classify_code_strings(code_tooltip_strings(game_dat()))
    srows = source_rows(wnd, ini, code, strings, REPO)
    buttons = dict(blocks(mount.read("data\\ini\\commandbutton.ini").decode("latin-1"), "CommandButton"))
    readouts = readout_rows(buttons, strings, dat_noncommand_names(game_dat()), REPO)
    places = PLACES + source_places(srows, readouts)
    out.mkdir(parents=True, exist_ok=True)
    sets = dict(blocks(mount.read("data\\ini\\commandset.ini").decode("latin-1"), "CommandSet"))
    templates = dict(blocks(mount.read("data\\ini\\playertemplate.ini").decode("latin-1"), "PlayerTemplate"))
    obj_sets = object_command_sets(mount)

    def has(labels: str) -> str:
        """every label of a TextLabel / DescriptLabel line (a toggle button lists several)"""
        names = labels.split()
        if not names:
            return ""
        return "yes" if all(n.lower() in strings for n in names) else "MISSING " + " ".join(n for n in names if n.lower() not in strings)

    # buttons.csv
    rows = []
    for set_name, fields in sets.items():
        for slot in range(1, 34):
            for name in fields.get(str(slot), []):
                b = buttons.get(name)
                if b is None:
                    rows.append([set_name, slot, name, "UNKNOWN BUTTON", "", "", "", "", "", ""])
                    continue
                text = (b.get("TextLabel") or [""])[0]
                desc = (b.get("DescriptLabel") or [""])[0]
                branch, field = cost_branch(b)
                rows.append([set_name, slot, name, (b.get("Command") or [""])[0], text, has(text), desc, has(desc), branch, field])
    with open(out / "buttons.csv", "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["command set", "slot", "button", "Command", "TextLabel", "in string file", "DescriptLabel", "in string file", "cost line", "number from"])
        w.writerows(rows)

    # factions.csv
    frows = []
    for name, f in templates.items():
        if (f.get("PlayableSide") or ["No"])[0].lower() != "yes" or not f.get("StartingUnit0") or not f.get("StartingBuilding"):
            continue
        builder = f["StartingUnit0"][0].split()[0]
        structures = [f["StartingBuilding"][0].split()[0]]
        for slot in range(1, 34):
            for bname in sets.get(obj_sets.get(builder, ""), {}).get(str(slot), []):
                b = buttons.get(bname, {})
                if (b.get("Command") or [""])[0].split()[:1] == ["DOZER_CONSTRUCT"] and b.get("Object"):
                    obj = b["Object"][0].split()[0]
                    if obj not in structures:
                        structures.append(obj)
        heroes = " ".join(f.get("BuildableHeroesMP", [])).split()

        def count(objs):
            n = 0
            for o in objs:
                for slot in range(1, 34):
                    n += len(sets.get(obj_sets.get(o, ""), {}).get(str(slot), []))
            return n
        book = (f.get("SpellBookMp") or [""])[0]
        book_set = obj_sets.get(book, book)
        book_buttons = sum(len(sets.get(book_set, {}).get(str(s), [])) for s in range(1, 34))
        frows.append([name, builder, count([builder]), " ".join(structures), count(structures), " ".join(heroes), count(heroes), book_set, book_buttons])
    with open(out / "factions.csv", "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["faction", "builder", "builder buttons", "main structures", "structure buttons", "heroes (BuildableHeroesMP)", "hero buttons",
                    "spell book set", "spell book buttons"])
        w.writerows(frows)

    # menus.csv: each TOOLTIP path label against the Apt walk's areas (paths compare without case, as the labels do)
    static_paths: set[str] = set()
    all_leaves: set[str] = set()
    aliases = {k.lower(): v.lower() for k, v in button_map(mount).items()}
    alias_targets = {v: k for k, v in aliases.items()}
    for row in area_rows:
        path = row["path"].lower()
        all_leaves.add(path.rstrip("/").split("/")[-1] if not path.endswith("/") else "")
        if row["from export"] == "no":
            static_paths.add(path)
    raw_labels = [line.strip() for line in mount.read("data\\lotr.str").decode("latin-1").splitlines()
                  if line.strip().lower().startswith("tooltip:") and "/" in line and " " not in line.strip()]
    mrows = []
    for label in sorted(set(raw_labels)):
        path = label[len("TOOLTIP:"):].lower()
        if path in static_paths or (path in alias_targets and alias_targets[path] in static_paths):
            kind = "static hover area"
        elif path.rstrip("/").split("/")[-1] in all_leaves:
            kind = "a button of that name exists only elsewhere (an export attached at run time)"
        else:
            kind = "no button of any movie"
        mrows.append([label, label[len("TOOLTIP:"):].split("/")[0], kind, strings.get(label.lower(), "")])
    with open(out / "menus.csv", "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["label", "movie", "hover area", "text"])
        w.writerows(mrows)

    # labels.csv
    with open(out / "labels.csv", "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["label", "in string file", "text"])
        for label in FIXED_LABELS:
            w.writerow([label, "yes" if label.lower() in strings else "MISSING", strings.get(label.lower(), "")])

    # places
    with open(out / "places.csv", "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["place", "retail code", "text from", "before HOVER-1", "after HOVER-1", "stop"])
        w.writerows(places)
    with open(out / "sources.csv", "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(SOURCE_HEADER)
        w.writerows(srows)
    with open(out / "readouts.csv", "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(READOUT_HEADER)
        w.writerows(readouts)
    before = sum(1 for p in places if p[3].startswith("ok"))
    after = sum(1 for p in places if p[4].startswith("ok"))
    stopped = [p[5] for p in places if p[5]]
    missing_labels = sum(1 for r in rows if str(r[5]).startswith("MISSING") or str(r[7]).startswith("MISSING"))
    with open(out / "places.md", "w") as f:
        f.write("# Hover texts of RotWK 2.01 (lane HOVER-1 inventory, generated by tools/hover/hover_inventory.py)\n\n")
        f.write(f"Places: {len(places)}; working before: {before}; working after: {after}; with a stop: {len(stopped)} ({', '.join(sorted(set(stopped)))})\n\n")
        f.write(f"Command set slots: {len(rows)} ({missing_labels} name a label the string file lacks); menu tooltip labels: {len(mrows)} "
                f"({sum(1 for r in mrows if r[2] == 'static hover area')} on a static hover area, "
                f"{sum(1 for r in mrows if r[2].startswith('a button'))} by a name only, {sum(1 for r in mrows if r[2].startswith('no'))} on no button); "
                f"factions: {len(frows)}\n\n")
        f.write("| place | retail code | text from | before | after | stop |\n|---|---|---|---|---|---|\n")
        for p in places:
            f.write("| " + " | ".join(p) + " |\n")
        f.write("\n| faction | builder buttons | structures | structure buttons | heroes | hero buttons | spell book | spell book buttons |\n"
                "|---|---|---|---|---|---|---|---|\n")
        for r in frows:
            f.write(f"| {r[0]} | {r[2]} | {len(r[3].split())} | {r[4]} | {len(r[5].split())} | {r[6]} | {r[7]} | {r[8]} |\n")
    print(f"places {len(places)} before {before} after {after} stopped {len(stopped)}; slots {len(rows)} missing labels {missing_labels}; "
          f"menu labels {len(mrows)}; factions {len(frows)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
