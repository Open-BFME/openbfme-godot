from __future__ import annotations

import json
import os
from pathlib import Path
import subprocess
import sys

from openbfme_importer.cook.objects import (
    cook_documents,
    cook_files,
    cook_ini_root,
    write_bundle,
)


FIXTURE_ROOT = Path(__file__).parent / "fixtures" / "cook" / "objects"
IMPORTER_ROOT = Path(__file__).resolve().parents[1]
CONTRACT_FIXTURE = Path(__file__).resolve().parents[2] / "contracts" / "fixtures" / "bundle-v1.json"


def _by_name(bundle: dict[str, object]) -> dict[str, dict[str, object]]:
    return {row["name"]: row for row in bundle["templates"]}


def _module(template: dict[str, object], module_type: str) -> dict[str, object]:
    return next(row for row in template["modules"] if row["type"] == module_type)


def test_cook_round_trips_authored_object_module_and_weapon_set_assignments() -> None:
    result = cook_ini_root(FIXTURE_ROOT)
    bundle = result.bundle
    templates = _by_name(bundle)
    base = templates["CookBase"]

    assert bundle["schema"] == "openbfme.bundle.v1"
    assert [row["name"] for row in bundle["templates"]] == [
        "CookBase",
        "CookChild",
        "CookReskin",
        "CookMystery",
        "CookMalformed",
    ]
    assert base["fields"]["VoiceSelect"] == ["CookVoiceOne", "CookVoiceTwo"]
    assert base["fields"] == {
        "Side": "CookFaction",
        "BuildCost": 725,
        "BuildTime": 15.5,
        "CommandPoints": 25,
        "KindOf": "PRELOAD SELECTABLE CAN_ATTACK",
        "Geometry": "BOX",
        "GeometryMajorRadius": 12,
        "GeometryMinorRadius": 7.5,
        "GeometryHeight": 20,
        "VoiceSelect": ["CookVoiceOne", "CookVoiceTwo"],
    }
    assert base["side"] == "CookFaction"
    assert base["kindof"] == ["PRELOAD", "SELECTABLE", "CAN_ATTACK"]
    assert base["geometry"] == {
        "shape": "BOX",
        "major_radius": 12,
        "minor_radius": 7.5,
        "height": 20,
    }
    assert base["build_cost"] == 725
    assert base["build_time"] == 15.5
    assert base["command_points"] == 25
    assert base["health"] == 1200

    production = _module(base, "ProductionUpdate")
    assert production["carrier"] == "Behavior"
    assert production["tag"] == "ModuleTag_Production"
    assert production["fields"] == {
        "GiveNoXP": True,
        "QueueMax": 3,
        "Bonus": ["FIRST", "SECOND"],
    }
    assert _module(base, "ActiveBody")["fields"] == {
        "MaxHealth": 1200,
        "MaxHealthDamaged": 600,
    }
    draw = _module(base, "W3DScriptedModelDraw")
    assert draw["blocks"][0]["fields"]["Model"] == "COOK_MODEL"

    weapon_set = next(row for row in base["blocks"] if row["type"] == "WeaponSet")
    assert weapon_set["fields"] == {
        "Conditions": "None",
        "Weapon": ["PRIMARY CookSword", "SECONDARY CookBow"],
        "Damage": 42,
    }


def test_cook_records_inheritance_without_flattening() -> None:
    templates = _by_name(cook_ini_root(FIXTURE_ROOT).bundle)
    child = templates["CookChild"]
    reskin = templates["CookReskin"]

    assert child["kind"] == "child"
    assert child["parent"] == "CookBase"
    assert child["build_cost"] == 800
    assert "side" not in child
    assert _module(child, "ProductionUpdate")["fields"] == {"QueueMax": 5}
    assert reskin["kind"] == "reskin"
    assert reskin["parent"] == "CookBase"
    assert "health" not in reskin


def test_well_formed_unknown_module_is_typed_without_a_runtime_contract() -> None:
    mystery = _by_name(cook_ini_root(FIXTURE_ROOT).bundle)["CookMystery"]
    unknown = _module(mystery, "MadeUpBehavior")

    assert unknown["gap"] is False
    assert unknown["fields"] == {
        "RawText": "keep this text verbatim",
        "Repeated": ["alpha", "beta"],
        "Count": 7,
    }


def test_malformed_module_and_unknown_carrier_are_verbatim_gaps() -> None:
    malformed = _by_name(cook_ini_root(FIXTURE_ROOT).bundle)["CookMalformed"]
    assert malformed["blocks"] == []

    missing_type = _module(malformed, "ModuleTag_Malformed")
    assert missing_type["carrier"] == "Behavior"
    assert missing_type["gap"] is True
    assert missing_type["fields"] == {"Count": "8", "Enabled": "Yes"}

    unknown_carrier = _module(malformed, "AlienBehavior")
    assert unknown_carrier["carrier"] == "other"
    assert unknown_carrier["tag"] == "ModuleTag_Alien"
    assert unknown_carrier["gap"] is True
    assert unknown_carrier["fields"] == {"Count": "9"}


def test_doubled_module_delimiter_never_becomes_equals_type() -> None:
    result = cook_ini_root(FIXTURE_ROOT)
    base = _by_name(result.bundle)["CookBase"]

    draw = _module(base, "W3DScriptedModelDraw")
    assert draw["carrier"] == "Draw"
    assert draw["tag"] == "ModuleTag_Draw"
    assert all(
        module["type"] != "="
        for template in result.bundle["templates"]
        for module in template["modules"]
    )


def test_defines_are_resolved_and_source_identity_is_relative() -> None:
    bundle = cook_ini_root(FIXTURE_ROOT).bundle

    assert bundle["defines"] == {
        "COOK_COST": 725,
        "COOK_DAMAGE": 42,
        "COOK_HEALTH": 1200,
        "COOK_KINDOF": "PRELOAD SELECTABLE CAN_ATTACK",
        "COOK_TIME": 15.5,
    }
    assert [row["path"] for row in bundle["source"]["paths"]] == [
        "data/ini/base-behavior.inc",
        "data/ini/constants.inc",
        "data/ini/entry.ini",
        "data/ini/objects.inc",
    ]


def test_output_is_byte_identical_for_root_and_explicit_file_runs(tmp_path: Path) -> None:
    first = tmp_path / "first.json"
    second = tmp_path / "second.json"
    root_result = cook_ini_root(FIXTURE_ROOT)
    file_result = cook_files(sorted(FIXTURE_ROOT.iterdir()))

    write_bundle(root_result.bundle, first)
    write_bundle(file_result.bundle, second)
    assert first.read_bytes() == second.read_bytes()


def test_committed_bundle_fixture_is_the_cook_output(tmp_path: Path) -> None:
    actual = tmp_path / "bundle-v1.json"
    write_bundle(cook_ini_root(FIXTURE_ROOT).bundle, actual)
    assert actual.read_bytes() == CONTRACT_FIXTURE.read_bytes()


def test_module_cli_entrypoint_writes_bundle_and_report(tmp_path: Path) -> None:
    out = tmp_path / "bundle.json"
    report = tmp_path / "report.json"
    env = dict(os.environ, PYTHONPATH=str(IMPORTER_ROOT))
    result = subprocess.run(
        [
            sys.executable,
            "-m",
            "openbfme_importer.cook.objects",
            "--ini-root",
            str(FIXTURE_ROOT),
            "--out",
            str(out),
            "--report",
            str(report),
        ],
        cwd=IMPORTER_ROOT,
        env=env,
        capture_output=True,
        text=True,
        timeout=120,
    )

    assert result.returncode == 0, result.stdout + result.stderr
    assert json.loads(out.read_text(encoding="utf-8"))["schema"] == "openbfme.bundle.v1"
    census = json.loads(report.read_text(encoding="utf-8"))
    assert census["templates_by_kind"] == {"child": 1, "object": 3, "reskin": 1}
    assert census["parse_failures"] == []


def test_parse_failure_names_template_and_keeps_valid_sibling() -> None:
    result = cook_documents(
        [
            (
                "data/ini/broken.ini",
                b"Object GoodObject\n  BuildCost = 1\nEnd\n"
                b"Object BadObject\n  UnitSpecificSounds\nEnd\n",
            )
        ]
    )

    assert [row["name"] for row in result.bundle["templates"]] == ["GoodObject"]
    assert len(result.report["parse_failures"]) == 1
    assert result.report["parse_failures"][0]["file"] == "data/ini/broken.ini"
    assert result.report["parse_failures"][0]["template"] == "BadObject"
