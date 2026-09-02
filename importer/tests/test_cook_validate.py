from __future__ import annotations

import json
import os
from pathlib import Path
import subprocess
import sys

from openbfme_importer.cook._bundle import cook_layered_ini
from openbfme_importer.cook.validate import validate_layers


TEST_ROOT = Path(__file__).parent
FIXTURE_ROOT = TEST_ROOT / "fixtures" / "cook"
BASE = FIXTURE_ROOT / "validate" / "base" / "data" / "ini"
MOD = FIXTURE_ROOT / "mod"
BROKEN_MOD = FIXTURE_ROOT / "mod_broken"
IMPORTER_ROOT = TEST_ROOT.parent
CONTRACT = IMPORTER_ROOT.parent / "contracts" / "bundle-v1.schema.json"


def _run(*args: object) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, *map(str, args)],
        cwd=IMPORTER_ROOT,
        env=dict(os.environ, PYTHONPATH=str(IMPORTER_ROOT)),
        capture_output=True,
        text=True,
        timeout=120,
    )


def _named(rows: list[dict[str, object]], name: str) -> dict[str, object]:
    return next(row for row in rows if row["name"] == name)


def test_package_cli_cooks_every_schema_table_deterministically(tmp_path: Path) -> None:
    first = tmp_path / "first.json"
    second = tmp_path / "second.json"
    report = tmp_path / "report.json"
    first_run = _run(
        "-m",
        "openbfme_importer.cook",
        "--ini-root",
        BASE,
        "--out",
        first,
        "--report",
        report,
    )
    second_run = _run(
        "-m",
        "openbfme_importer.cook",
        "--ini-root",
        BASE,
        "--out",
        second,
    )

    assert first_run.returncode == 0, first_run.stdout + first_run.stderr
    assert second_run.returncode == 0, second_run.stdout + second_run.stderr
    assert first.read_bytes() == second.read_bytes()
    bundle = json.loads(first.read_text(encoding="utf-8"))
    schema = json.loads(CONTRACT.read_text(encoding="utf-8"))
    assert set(bundle) == set(schema["required"])
    assert json.loads(report.read_text(encoding="utf-8"))["parse_failures"] == []


def test_mod_overrides_file_adds_object_and_records_every_layer(tmp_path: Path) -> None:
    out = tmp_path / "mod-bundle.json"
    result = _run(
        "-m",
        "openbfme_importer.cook",
        "--ini-root",
        BASE,
        "--mod",
        MOD,
        "--out",
        out,
    )

    assert result.returncode == 0, result.stdout + result.stderr
    bundle = json.loads(out.read_text(encoding="utf-8"))
    assert _named(bundle["templates"], "CleanUnit")["build_cost"] == 222
    assert _named(bundle["weapons"], "CleanWeapon")["nuggets"][0]["fields"]["Damage"] == 25
    assert _named(bundle["templates"], "ModUnit")["name"] == "ModUnit"
    assert [row["path"] for row in bundle["source"]["paths"]] == [
        "layer-000-base/data/ini/definitions.ini",
        "layer-001-mod/data/ini/definitions.ini",
    ]


def test_later_mod_wins_same_virtual_path(tmp_path: Path) -> None:
    later = tmp_path / "later" / "data" / "ini"
    later.mkdir(parents=True)
    source = (MOD / "data" / "ini" / "definitions.ini").read_text(
        encoding="utf-8"
    )
    (later / "definitions.ini").write_text(
        source.replace("#define CLEAN_COST 222", "#define CLEAN_COST 333"),
        encoding="utf-8",
    )

    bundle = cook_layered_ini(BASE, [MOD, later.parents[1]]).bundle

    assert _named(bundle["templates"], "CleanUnit")["build_cost"] == 333
    assert len(bundle["source"]["paths"]) == 3


def test_clean_base_validates_clean() -> None:
    assert validate_layers(BASE) == []


def test_new_mod_module_is_a_gap_not_a_failure() -> None:
    issues = validate_layers(BASE, [MOD])

    assert issues == [
        {
            "kind": "gap",
            "severity": "gap",
            "file": "data/ini/definitions.ini",
            "line": 92,
            "template": "ModUnit",
            "field": "module",
            "target": "InventedModBehavior",
        }
    ]


def test_missing_weapon_and_parent_are_exactly_named_with_locations() -> None:
    issues = validate_layers(BASE, [BROKEN_MOD])

    assert [(row["kind"], row["target"]) for row in issues] == [
        ("missing_weapon", "MissingWeapon"),
        ("missing_parent", "MissingParent"),
    ]
    assert all(row["file"] == "data/ini/broken.ini" for row in issues)
    assert all(row["line"] > 0 for row in issues)


def test_every_supported_reference_family_and_bare_numeric_define_is_named(
    tmp_path: Path,
) -> None:
    source = tmp_path / "definitions.ini"
    source.write_text(
        """Object RefUnit
  BuildCost = UNDEFINED_COST
  CommandSet = MissingCommandSet
  WeaponSet
    Weapon = PRIMARY MissingWeapon
  End
  ArmorSet
    Armor = DEFAULT MissingArmor
  End
  LocomotorSet
    Locomotor = SET_NORMAL MissingLocomotor
  End
  Behavior = HordeContain ModuleTag_Horde
    RankInfo = RankNumber:1 UnitType:MissingMember Position:X:0 Y:0
  End
End
CommandSet RefCommandSet
  1 = MissingButton
End
CommandButton RefButton
  Object = MissingObject
  Upgrade = MissingUpgrade
  Science = MissingScience
  SpecialPower = MissingPower
End
Upgrade RefUpgrade
  Prerequisites = MissingPrerequisite
End
Science RefScience
  PrerequisiteSciences = MissingPrerequisiteScience
End
""",
        encoding="utf-8",
    )

    issues = validate_layers(tmp_path)

    assert {(row["kind"], row["target"]) for row in issues} == {
        ("missing_armor", "MissingArmor"),
        ("missing_command_button", "MissingButton"),
        ("missing_command_set", "MissingCommandSet"),
        ("missing_horde_unit", "MissingMember"),
        ("missing_locomotor", "MissingLocomotor"),
        ("missing_object", "MissingObject"),
        ("missing_prerequisite", "MissingPrerequisite"),
        ("missing_science", "MissingPrerequisiteScience"),
        ("missing_science", "MissingScience"),
        ("missing_specialpower", "MissingPower"),
        ("missing_upgrade", "MissingUpgrade"),
        ("missing_weapon", "MissingWeapon"),
        ("undefined_define", "UNDEFINED_COST"),
    }


def test_validator_cli_writes_list_and_uses_failure_exit_code(tmp_path: Path) -> None:
    output = tmp_path / "validation.json"
    result = _run(
        "-m",
        "openbfme_importer.cook.validate",
        "--ini-root",
        BASE,
        "--mod",
        BROKEN_MOD,
        "--json",
        output,
    )

    assert result.returncode == 1, result.stdout + result.stderr
    assert json.loads(result.stdout) == json.loads(output.read_text(encoding="utf-8"))
    assert len(json.loads(result.stdout)) == 2


def test_validator_usage_error_is_exit_two() -> None:
    result = _run("-m", "openbfme_importer.cook.validate", "--ini-root", BASE)
    assert result.returncode == 0
    usage = _run("-m", "openbfme_importer.cook.validate")
    assert usage.returncode == 2
