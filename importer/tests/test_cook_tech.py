from __future__ import annotations

import json
import os
from pathlib import Path
import subprocess
import sys

from openbfme_importer.cook._bundle import write_bundle
from openbfme_importer.cook.tech import cook_documents, cook_ini_root


FIXTURE_ROOT = Path(__file__).parent / "fixtures" / "cook" / "tech"
ALL_FIXTURES = FIXTURE_ROOT.parent
IMPORTER_ROOT = Path(__file__).resolve().parents[1]
CONTRACT_ROOT = Path(__file__).resolve().parents[2] / "contracts"


def _named(rows: list[dict[str, object]], name: str) -> dict[str, object]:
    return next(row for row in rows if row["name"] == name)


def test_tech_round_trips_every_assignment_with_typed_repeated_values() -> None:
    bundle = cook_ini_root(FIXTURE_ROOT).bundle

    assert _named(bundle["upgrades"], "CookUpgrade")["fields"] == {
        "Type": "OBJECT",
        "BuildCost": 450,
        "BuildTime": 12.5,
        "Prerequisites": ["CookForge", "CookAcademy"],
        "Tooltip": "CONTROLBAR:CookUpgrade",
    }
    assert _named(bundle["sciences"], "SCIENCE_CookKnowledge")["fields"] == {
        "PrerequisiteSciences": ["SCIENCE_CookRoot", "SCIENCE_CookAlly"],
        "SciencePurchasePointCost": 3,
        "IsGrantable": True,
    }
    assert _named(bundle["special_powers"], "SpecialPowerCookBeacon")[
        "fields"
    ] == {
        "Enum": "SPECIAL_POWER_COOK_BEACON",
        "ReloadTime": 90000,
        "RequiredScience": ["SCIENCE_CookKnowledge", "SCIENCE_CookAlly"],
        "PublicTimer": False,
    }
    assert _named(bundle["command_buttons"], "Command_CookUpgrade")["fields"] == {
        "Command": "OBJECT_UPGRADE",
        "Object": "CookTarget",
        "Upgrade": "CookUpgrade",
        "Science": "SCIENCE_CookKnowledge",
        "SpecialPower": "SpecialPowerCookBeacon",
        "Options": ["NEED_UPGRADE", "CANCELABLE"],
        "TextLabel": "CONTROLBAR:CookUpgrade",
        "ButtonImage": "BICookUpgrade",
    }
    assert bundle["defines"]["COOK_TECH_COST"] == 450
    assert bundle["defines"]["COOK_BUTTON_COMMAND"] == "OBJECT_UPGRADE"


def test_command_set_preserves_authored_slot_order() -> None:
    command_set = _named(
        cook_ini_root(FIXTURE_ROOT).bundle["command_sets"], "CookCommandSet"
    )
    assert command_set["entries"] == [
        {"slot": 4, "button": "Command_CookUpgrade"},
        {"slot": 1, "button": "Command_CookFirst"},
        {"slot": 7, "button": "Command_CookLast"},
    ]
    assert command_set["fields"] == {"InitialVisible": 2}


def test_malformed_command_set_is_reported_and_retained() -> None:
    source = (FIXTURE_ROOT / "malformed-command-set.txt").read_bytes()
    result = cook_documents([("data/ini/malformed-command-set.ini", source)])

    assert result.bundle["command_sets"] == [
        {
            "name": "CookMalformedCommandSet",
            "entries": [{"slot": 2, "button": "Command_CookBroken"}],
            "fields": {},
        }
    ]
    assert result.report["parse_failures"] == [
        {
            "file": "data/ini/malformed-command-set.ini",
            "block": "CookMalformedCommandSet",
            "message": "data/ini/malformed-command-set.ini:1: unterminated CommandSet CookMalformedCommandSet",
        }
    ]
    assert result.bundle["diagnostics"] == [
        {
            "template": "CookMalformedCommandSet",
            "message": "data/ini/malformed-command-set.ini:1: unterminated CommandSet CookMalformedCommandSet",
        }
    ]


def test_tech_output_is_byte_identical_twice(tmp_path: Path) -> None:
    first = tmp_path / "first.json"
    second = tmp_path / "second.json"
    write_bundle(cook_ini_root(FIXTURE_ROOT).bundle, first)
    write_bundle(cook_ini_root(FIXTURE_ROOT).bundle, second)
    assert first.read_bytes() == second.read_bytes()


def test_complete_fixture_is_full_cook_output_and_schema_names_every_table(
    tmp_path: Path,
) -> None:
    actual = tmp_path / "bundle-v1.json"
    write_bundle(cook_ini_root(ALL_FIXTURES).bundle, actual)
    assert actual.read_bytes() == (
        CONTRACT_ROOT / "fixtures" / "bundle-v1.json"
    ).read_bytes()

    schema = json.loads(
        (CONTRACT_ROOT / "bundle-v1.schema.json").read_text(encoding="utf-8")
    )
    for table in (
        "upgrades",
        "sciences",
        "special_powers",
        "command_buttons",
        "command_sets",
    ):
        assert table in schema["required"]
        assert table in schema["properties"]


def test_tech_cli_entrypoint_writes_bundle_and_report(tmp_path: Path) -> None:
    out = tmp_path / "bundle.json"
    report = tmp_path / "report.json"
    env = dict(os.environ, PYTHONPATH=str(IMPORTER_ROOT))
    result = subprocess.run(
        [
            sys.executable,
            "-m",
            "openbfme_importer.cook.tech",
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
    bundle = json.loads(out.read_text(encoding="utf-8"))
    assert len(bundle["upgrades"]) == 1
    census = json.loads(report.read_text(encoding="utf-8"))
    assert census["upgrades"] == 1
    assert census["sciences"] == 1
    assert census["special_powers"] == 1
    assert census["command_buttons"] == 1
    assert census["command_sets"] == 1
    assert census["parse_failures"] == []
