from __future__ import annotations

import json
import struct
from pathlib import Path

import pytest

from openbfme_importer import living_world_ui as ui_module
from openbfme_importer.living_world_ui import (
    BUILDINGS_PATH, EXPERIENCE_LEVELS_PATH, GAMEDATA_PATH, SCHEMA_VERSION,
    LivingWorldUiError, _bonus, _read_buildings, _read_level_up_upgrades,
    _read_upgrade_experience_levels, _validate_level_up_experience_coverage,
)
from openbfme_importer.livingmap_bundle import CatalogReader


def _big(path: Path, members: dict[str, bytes]) -> None:
    names = [(name.encode("latin-1") + b"\0", body) for name, body in members.items()]
    header = 16 + sum(8 + len(name) for name, _ in names)
    offset = header
    records = bytearray()
    bodies = bytearray()
    for name, body in names:
        records += struct.pack(">II", offset, len(body)) + name
        bodies += body
        offset += len(body)
    path.write_bytes(b"BIG4" + struct.pack("<I", offset) + struct.pack(">II", len(names), header) + records + bodies)


def _reader(tmp_path: Path, text: str) -> CatalogReader:
    tmp_path.mkdir(parents=True, exist_ok=True)
    archive = tmp_path / "fixture.big"
    payload = text.encode("latin-1")
    _big(archive, {BUILDINGS_PATH: payload})
    catalog = {"install_root": str(tmp_path), "entries": [{"name": BUILDINGS_PATH, "archive": archive.name, "offset": 16 + 8 + len(BUILDINGS_PATH.encode()) + 1, "size": len(payload), "precedence": 0}]}
    path = tmp_path / "catalog.json"
    path.write_text(json.dumps(catalog))
    return CatalogReader(path)


def _catalog(tmp_path: Path, body: str):
    gaps = []
    rows = _read_buildings(_reader(tmp_path, "LivingWorldBuilding Fixture\n Type = Resource\n" + body + "\nEnd\n"), gaps)
    return rows[0], [gap.public() for gap in gaps]


def test_a1_schema_v4():
    assert SCHEMA_VERSION == 4


def test_a2_all_five_and_source_order(tmp_path: Path):
    row, gaps = _catalog(tmp_path, """
 BuildingNugget StrengthenArmy S
  StrengtheningRange = TypoRange
  BonusKey = Key
  Bonus = 1 Armor:10%
 End
 BuildingNugget IncreaseTreasury T
  TreasureAmount = MACRO_RAW
 End
 BuildingNugget SpawnArmy P
  QueueSize = 0
  ArmyToSpawn
   PlayerArmy = Second
  End
  ArmyToSpawn
   PlayerArmy = First
  End
 End
 BuildingNugget UpgradeTroops U
  NumUpgradesPerTurn = 1
  UpgradeableUnits = One Two One
 End
 BuildingNugget IncreaseCommandPoints C
  Type = WORLD
  Amount = -7
 End""")
    assert not gaps
    assert [n["kind"] for n in row["nuggets"]] == ["strengthen_army", "increase_treasury", "spawn_army", "upgrade_troops", "increase_command_points"]
    assert [a["playerArmy"] for a in row["nuggets"][2]["armies"]] == ["Second", "First"]
    assert [a["playerArmy"] for a in row["recruits"]] == ["First", "Second"]
    assert row["nuggets"][3]["upgradeableUnits"] == ["One", "Two", "One"]


def test_a3_raw_range_typo_preserved(tmp_path: Path):
    row, _ = _catalog(tmp_path, "BuildingNugget StrengthenArmy X\n StrengtheningRange = tYpO\n BonusKey = K\n Bonus = 1 Armor:2%\n End")
    assert row["nuggets"][0]["strengtheningRange"] == "tYpO"


def test_a4_bonus_raw_numeric_and_null():
    value = _bonus("2 Weapon:1.25% Armor:-3%")
    assert value["weaponRaw"] == "Weapon:1.25%" and value["weaponPct"] == 1.25
    assert value["experienceRaw"] is None and value["experiencePct"] is None


@pytest.mark.parametrize("bad", ["0 Armor:1%", "1 Armor:1% Armor:2%", "1 Speed:2%", "1 Armor:2%%", "1 Armor:NaN%"])
def test_a5_bad_bonus(bad: str):
    with pytest.raises(ValueError): _bonus(bad)


def test_a6_raw_treasury_and_signed_cp(tmp_path: Path):
    row, _ = _catalog(tmp_path, "BuildingNugget IncreaseTreasury T\n TreasureAmount = RAW_MACRO\n End\nBuildingNugget IncreaseCommandPoints C\n Type = WORLD\n Amount = +12\n End")
    assert row["nuggets"][0]["treasureAmount"] == "RAW_MACRO"
    assert row["nuggets"][1]["amount"] == 12
    refused, gaps = _catalog(tmp_path / "unsafe", "BuildingNugget IncreaseCommandPoints C\n Type = WORLD\n Amount = 9007199254740992\n End")
    assert refused["nuggetsStatus"] == "refused"
    assert gaps[-1]["reason"] == "nugget_bad_value"


def test_a7_unknown_kind_refuses_building(tmp_path: Path):
    row, gaps = _catalog(tmp_path, "BuildingNugget Mystery X\n Value = 1\n End")
    assert row["nuggetsStatus"] == "refused" and row["nuggets"] == []
    assert gaps[-1]["reason"] == "nugget_unknown_kind"


def test_a7_unknown_scalar_duplicate_and_empty_required_refuse(tmp_path: Path):
    cases = [
        ("BuildingNugget IncreaseTreasury X\n TreasureAmount = M\n Extra = X\n End", "nugget_unknown_field"),
        ("BuildingNugget IncreaseTreasury X\n TreasureAmount = M\n TreasureAmount = N\n End", "nugget_bad_value"),
        ("BuildingNugget IncreaseTreasury X\n TreasureAmount = \"\"\n End", "nugget_bad_value"),
    ]
    for index, (body, reason) in enumerate(cases):
        row, gaps = _catalog(tmp_path / str(index), body)
        assert row["nuggetsStatus"] == "refused" and gaps[-1]["reason"] == reason


def test_a8_unknown_army_field_refuses(tmp_path: Path):
    row, gaps = _catalog(tmp_path, "BuildingNugget SpawnArmy X\n QueueSize = 1\n ArmyToSpawn\n Mystery = X\n End\n End")
    assert row["nuggetsStatus"] == "refused"
    assert gaps[-1]["reason"] == "nugget_unknown_field"


def test_a8_unknown_subblock_refuses(tmp_path: Path):
    row, gaps = _catalog(tmp_path, "BuildingNugget SpawnArmy X\n QueueSize = 1\n MysteryBlock\n End\n End")
    assert row["nuggetsStatus"] == "refused"
    assert gaps[-1]["reason"] == "nugget_unknown_subblock"


def test_a9_caps_refuse(tmp_path: Path):
    body = "\n".join("BuildingNugget IncreaseTreasury T%d\n TreasureAmount = M\n End" % n for n in range(33))
    row, gaps = _catalog(tmp_path, body)
    assert row["nuggetsStatus"] == "refused"
    assert gaps[-1]["reason"] == "nugget_cap_exceeded"


def test_a9_nested_caps_refuse(tmp_path: Path):
    bonus = "\n".join(" Bonus = %d Armor:1%%" % (n + 1) for n in range(17))
    armies = "\n".join(" ArmyToSpawn\n  PlayerArmy = A%d\n End" % n for n in range(17))
    units = " ".join("U%d" % n for n in range(65))
    cases = [
        "BuildingNugget StrengthenArmy X\n StrengtheningRange = R\n BonusKey = K\n%s\n End" % bonus,
        "BuildingNugget SpawnArmy X\n QueueSize = 1\n%s\n End" % armies,
        "BuildingNugget UpgradeTroops X\n NumUpgradesPerTurn = 1\n UpgradeableUnits = %s\n End" % units,
        "BuildingNugget SpawnArmy X\n QueueSize = 1\n ArmyToSpawn\n  PlayerArmy = %s\n End\n End" % ("A" * 257),
        "BuildingNugget StrengthenArmy X\n StrengtheningRange = R\n BonusKey = K\n Bonus = 1 Armor:%s.5%%\n End" % ("0" * 300),
    ]
    for index, body in enumerate(cases):
        row, gaps = _catalog(tmp_path / str(index), body)
        assert row["nuggetsStatus"] == "refused"
        assert gaps[-1]["reason"] == "nugget_cap_exceeded"


def test_a10_deterministic(tmp_path: Path):
    body = "BuildingNugget IncreaseTreasury T\n TreasureAmount = MACRO\n End"
    first, first_gaps = _catalog(tmp_path / "one", body)
    second, second_gaps = _catalog(tmp_path / "two", body)
    assert json.dumps(first, sort_keys=True) == json.dumps(second, sort_keys=True)
    assert first_gaps == second_gaps == []


def _multi_reader(tmp_path: Path, members: dict[str, bytes]) -> CatalogReader:
    tmp_path.mkdir(parents=True, exist_ok=True)
    archive = tmp_path / "fixture.big"
    _big(archive, members)
    header = 16 + sum(8 + len(name.encode("latin-1")) + 1 for name in members)
    offset = header
    entries = []
    for name, payload in members.items():
        entries.append({"name": name, "archive": archive.name, "offset": offset,
                        "size": len(payload), "precedence": 0})
        offset += len(payload)
    catalog = {"install_root": str(tmp_path), "entries": entries}
    path = tmp_path / "catalog.json"
    path.write_text(json.dumps(catalog))
    return CatalogReader(path)


def _experience_reader(tmp_path: Path, experience: str,
                       *, gamedata: str = "#define BASE 1\n",
                       extra: dict[str, bytes] | None = None) -> CatalogReader:
    members = {
        GAMEDATA_PATH: gamedata.encode("latin-1"),
        EXPERIENCE_LEVELS_PATH: experience.encode("latin-1"),
    }
    members.update(extra or {})
    return _multi_reader(tmp_path, members)


def _active(*units: str) -> list[dict]:
    return [{"nuggetsStatus": "ok", "nuggets": [{
        "kind": "upgrade_troops", "upgradeableUnits": list(units)}]}]


def test_b1_experience_macros_include_position_filter_order_duplicates_and_exact_ints(tmp_path: Path):
    include_path = "fixture-experience.inc"
    reader = _experience_reader(tmp_path, f"""
ExperienceLevel First
 TargetNames = Ignored
 RequiredExperience = 1
 ExperienceAward = 0
 Rank = 1
End
#include "{include_path}"
ExperienceLevel Third
 TargetNames = UnitA
 RequiredExperience = 9
 ExperienceAward = 4
 Rank = 3
 Upgrades = U3
End
""", gamedata="#define BASE 2\n#define AWARD ZERO\n#define ZERO 0\n", extra={"data/ini/" + include_path: b"""
#define TARGETS UnitA Ignored UnitA
ExperienceLevel Second
 TargetNames = TARGETS
 RequiredExperience = BASE
 ExperienceAward = AWARD
 Rank = 1
 Upgrades = U1 U1
 SelectionDecal
  Texture = RetailVisualOnly
 End
End
ExperienceLevel SecondRank
 TargetNames = UnitA
 RequiredExperience = 5
 ExperienceAward = 3
 Rank = 2
 Upgrades = U2
End
"""})
    rows = _read_upgrade_experience_levels(reader, _active("UnitA"))
    assert [row["name"] for row in rows] == ["Second", "SecondRank", "Third"]
    assert rows[0] == {"name": "Second", "targetNames": ["UnitA", "UnitA"],
                       "requiredExperience": 2, "experienceAward": 0, "rank": 1,
                       "upgrades": ["U1", "U1"]}
    assert all(type(row[key]) is int for row in rows
               for key in ("requiredExperience", "experienceAward", "rank"))


def test_b2_experience_missing_coverage_cycle_duplicates_and_fractional_fail(tmp_path: Path):
    valid = """ExperienceLevel One
 TargetNames = UnitA
 RequiredExperience = VALUE
 ExperienceAward = 0
 Rank = 1
End
"""
    cases = [
        ("missing", valid.replace("UnitA", "Other"), "#define VALUE 1\n"),
        ("cycle", valid, "#define VALUE AGAIN\n#define AGAIN VALUE\n"),
        ("fractional", valid, "#define VALUE 1.5\n"),
        ("duplicate-define", valid, "#define VALUE 1\n#define VALUE 2\n"),
        ("duplicate-name", valid + valid, "#define VALUE 1\n"),
    ]
    for name, document, gamedata in cases:
        with pytest.raises(LivingWorldUiError):
            _read_upgrade_experience_levels(
                _experience_reader(tmp_path / name, document, gamedata=gamedata),
                _active("UnitA"))


def test_b3_experience_caps_progression_and_determinism(tmp_path: Path, monkeypatch):
    document = """ExperienceLevel One
 TargetNames = UnitA
 RequiredExperience = 1
 ExperienceAward = 0
 Rank = 1
 Upgrades = U1 U2
End
"""
    reader = _experience_reader(tmp_path / "deterministic", document)
    first = _read_upgrade_experience_levels(reader, _active("UnitA"))
    second = _read_upgrade_experience_levels(reader, _active("UnitA"))
    assert json.dumps(first, sort_keys=True) == json.dumps(second, sort_keys=True)
    monkeypatch.setattr(ui_module, "MAX_EXPERIENCE_TOKENS", 1)
    with pytest.raises(LivingWorldUiError, match="cap"):
        _read_upgrade_experience_levels(reader, _active("UnitA"))

    backwards = document + """ExperienceLevel Two
 TargetNames = UnitA
 RequiredExperience = 1
 ExperienceAward = 0
 Rank = 2
End
"""
    monkeypatch.setattr(ui_module, "MAX_EXPERIENCE_TOKENS", 65536)
    with pytest.raises(LivingWorldUiError, match="non-increasing"):
        _read_upgrade_experience_levels(
            _experience_reader(tmp_path / "backwards", backwards), _active("UnitA"))


def _level_up_reader(tmp_path: Path, objects: dict[str, str]) -> CatalogReader:
    return _multi_reader(tmp_path, {
        "data/ini/object/" + name: payload.encode("latin-1")
        for name, payload in objects.items()
    })


def test_c1_level_up_rows_are_active_exact_flat_and_source_ordered(tmp_path: Path):
    reader = _level_up_reader(tmp_path, {
        "two.ini": """Object UnitB
 Behavior = LevelUpUpgrade ModuleTag_Level
  TriggeredBy = Upgrade_One Upgrade_Two
  TriggeredBy = Upgrade_Three
  LevelsToGain = 7
  LevelCap = 3
 End
End
""",
        "one.ini": """Object UnitA
 Behavior = LevelUpUpgrade ModuleTag_Level
  TriggeredBy = Upgrade_A
  LevelsToGain = 1
  LevelCap = 2
 End
End
Object Inactive
 Behavior = LevelUpUpgrade ModuleTag_Other
  TriggeredBy = NONE
  LevelsToGain = nope
  LevelCap = nope
 End
End
""",
    })
    rows = _read_level_up_upgrades(reader, _active("UnitB", "UnitA"))
    assert rows == [
        {"template": "UnitA", "triggeredBy": ["Upgrade_A"],
         "levelsToGain": 1, "levelCap": 2},
        {"template": "UnitB", "triggeredBy": ["Upgrade_One", "Upgrade_Two", "Upgrade_Three"],
         "levelsToGain": 7, "levelCap": 3},
    ]


@pytest.mark.parametrize("body, match", [
    ("", "missing"),
    ("ChildObject UnitA Parent\nEnd\n", "direct Object"),
    ("Object UnitA Parent\nEnd\n", "direct Object"),
    ("Object UnitA\nEnd\n", "exactly one"),
    ("Object UnitA\n Behavior = LevelUpUpgrade Tag\n  TriggeredBy = U\n  LevelsToGain = 1\n  LevelCap = 2\n End\n Behavior = LevelUpUpgrade Tag2\n  TriggeredBy = V\n  LevelsToGain = 1\n  LevelCap = 2\n End\nEnd\n", "exactly one"),
    ("Object UnitA\n Behavior = LevelUpUpgrade Tag\n  TriggeredBy = U u\n  LevelsToGain = 1\n  LevelCap = 2\n End\nEnd\n", "duplicate TriggeredBy"),
    ("Object UnitA\n Behavior = LevelUpUpgrade Tag\n  TriggeredBy = NULL\n  LevelsToGain = 1\n  LevelCap = 2\n End\nEnd\n", "TriggeredBy"),
    ("Object UnitA\n Behavior = LevelUpUpgrade Tag\n  TriggeredBy = U ; NONE\n  LevelsToGain = +1 // rejected sign\n  LevelCap = 2\n End\nEnd\n", "positive digit"),
    ("Object UnitA\n RemoveModule Tag\n Behavior = LevelUpUpgrade Tag\n  TriggeredBy = U\n  LevelsToGain = 1\n  LevelCap = 2\n End\nEnd\n", "modifies"),
    ("Object UnitA\n AddModule Tag\n Behavior = LevelUpUpgrade Tag\n  TriggeredBy = U\n  LevelsToGain = 1\n  LevelCap = 2\n End\nEnd\n", "modifies"),
    ("Object UnitA\n OverrideableByLikeKind Tag\n Behavior = LevelUpUpgrade Tag\n  TriggeredBy = U\n  LevelsToGain = 1\n  LevelCap = 2\n End\nEnd\n", "modifies"),
    ("Object UnitA\n Behavior = LevelUpUpgrade Tag\n  TriggeredBy = U\n  LevelsToGain = 1\n  LevelCap = 2\n  Nested\n   Value = 1\n  End\n End\nEnd\n", "nested"),
])
def test_c2_level_up_fail_closed(tmp_path: Path, body: str, match: str):
    reader = _level_up_reader(tmp_path, {"unit.ini": body})
    with pytest.raises(LivingWorldUiError, match=match):
        _read_level_up_upgrades(reader, _active("UnitA"))


def test_c2_level_up_numeric_and_trigger_boundaries(tmp_path: Path):
    cases = [
        ("missing-gain", "TriggeredBy = U\n  LevelCap = 2", "exactly one"),
        ("missing-cap", "TriggeredBy = U\n  LevelsToGain = 1", "exactly one"),
        ("zero", "TriggeredBy = U\n  LevelsToGain = 0\n  LevelCap = 2", "positive"),
        ("negative", "TriggeredBy = U\n  LevelsToGain = -1\n  LevelCap = 2", "positive digit"),
        ("non-digit", "TriggeredBy = U\n  LevelsToGain = many\n  LevelCap = 2", "positive digit"),
        ("over-safe", "TriggeredBy = U\n  LevelsToGain = 1\n  LevelCap = 9007199254740992", "exact positive integer range"),
        ("empty-trigger", "TriggeredBy = \n  LevelsToGain = 1\n  LevelCap = 2", "no TriggeredBy"),
    ]
    for name, fields, match in cases:
        reader = _level_up_reader(tmp_path / name, {"unit.ini": (
            "Object UnitA\n Behavior = LevelUpUpgrade Tag\n  "
            + fields + "\n End\nEnd\n")})
        with pytest.raises(LivingWorldUiError, match=match):
            _read_level_up_upgrades(reader, _active("UnitA"))


def test_c3_level_up_duplicate_objects_casefold_and_bounds(tmp_path: Path, monkeypatch):
    duplicate = _level_up_reader(tmp_path / "duplicate", {
        "a.ini": "Object UnitA\nEnd\n", "b.ini": "Object unita\nEnd\n"})
    with pytest.raises(LivingWorldUiError, match="duplicated"):
        _read_level_up_upgrades(duplicate, _active("UnitA"))
    with pytest.raises(LivingWorldUiError, match="duplicate active"):
        _read_level_up_upgrades(_level_up_reader(tmp_path / "active", {}), _active("UnitA", "unita"))
    monkeypatch.setattr(ui_module, "MAX_LEVEL_UP_ROWS", 0)
    with pytest.raises(LivingWorldUiError, match="row cap"):
        _read_level_up_upgrades(_level_up_reader(tmp_path / "row-cap", {}), _active("UnitA"))
    monkeypatch.setattr(ui_module, "MAX_LEVEL_UP_ROWS", 4096)
    monkeypatch.setattr(ui_module, "MAX_LEVEL_UP_OBJECT_INIS", 0)
    with pytest.raises(LivingWorldUiError, match="catalog cap"):
        _read_level_up_upgrades(_level_up_reader(tmp_path / "cap", {"a.ini": "Object X\nEnd\n"}), [])


def test_c4_level_up_experience_cross_invariant():
    upgrades = [{"template": "UnitA", "triggeredBy": ["U"], "levelsToGain": 9, "levelCap": 4}]
    valid = [
        {"targetNames": ["UnitA"], "rank": 2},
        {"targetNames": ["UnitA"], "rank": 3},
        {"targetNames": ["UnitA"], "rank": 4},
    ]
    _validate_level_up_experience_coverage(upgrades, valid)
    with pytest.raises(LivingWorldUiError, match="rank 3"):
        _validate_level_up_experience_coverage(upgrades, [valid[0], valid[2]])
