from __future__ import annotations

import json
import os
from pathlib import Path
import subprocess
import sys

from openbfme_importer.cook._bundle import write_bundle
from openbfme_importer.cook.movement import cook_documents, cook_ini_root


FIXTURE_ROOT = Path(__file__).parent / "fixtures" / "cook"
IMPORTER_ROOT = Path(__file__).resolve().parents[1]


def _named(rows: list[dict[str, object]], name: str) -> dict[str, object]:
    return next(row for row in rows if row["name"] == name)


def test_movement_round_trips_locomotor_sets_and_horde_world_units() -> None:
    bundle = cook_ini_root(FIXTURE_ROOT).bundle
    locomotor = _named(bundle["locomotors"], "CookHordeLocomotor")

    assert locomotor["fields"] == {
        "Speed": 60,
        "SpeedDamaged": 30,
        "TurnRate": 120,
        "Acceleration": 12.5,
        "Braking": 10,
        "MinTurnSpeed": "15%",
        "MaxTurnWithoutReform": 45,
        "Surface": ["GROUND", "RUBBLE"],
    }
    assert _named(bundle["locomotor_sets"], "CookHorde")["fields"] == {
        "Condition": "SET_NORMAL",
        "Locomotor": "CookHordeLocomotor",
        "Speed": 60,
    }

    horde = _named(bundle["hordes"], "CookHorde")
    assert horde["rank_info"] == [
        {
            "rank": 1,
            "unit_type": "CookMember",
            "position": [{"x": 20, "y": 0}, {"x": 20, "y": -20}],
        },
        {
            "rank": 2,
            "unit_type": "CookMember",
            "position": [{"x": 0, "y": 10}],
        },
    ]
    assert horde["fields"] == {
        "InitialPayload": ["CookMember 2", "CookMember 3"],
        "Slots": 5,
        "BannerCarrier": "CookBanner",
        "MaxTurnWithoutReform": 45,
    }
    assert bundle["defines"]["COOK_POSITION"] == 20


def test_movement_output_is_byte_identical_twice(tmp_path: Path) -> None:
    first = tmp_path / "first.json"
    second = tmp_path / "second.json"
    write_bundle(cook_ini_root(FIXTURE_ROOT).bundle, first)
    write_bundle(cook_ini_root(FIXTURE_ROOT).bundle, second)
    assert first.read_bytes() == second.read_bytes()


def test_movement_cli_entrypoint_writes_bundle_and_report(tmp_path: Path) -> None:
    out = tmp_path / "bundle.json"
    report = tmp_path / "report.json"
    env = dict(os.environ, PYTHONPATH=str(IMPORTER_ROOT))
    result = subprocess.run(
        [
            sys.executable,
            "-m",
            "openbfme_importer.cook.movement",
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
    assert len(bundle["locomotors"]) == 1
    census = json.loads(report.read_text(encoding="utf-8"))
    assert census["locomotors"] == 1
    assert census["hordes"] == 1
    assert census["parse_failures"] == []


def test_malformed_locomotor_is_named_and_valid_sibling_survives() -> None:
    result = cook_documents(
        [
            (
                "data/ini/broken.ini",
                b"Locomotor BadLocomotor\n  Speed = 1\n"
                b"Locomotor GoodLocomotor\n  Speed = 2\nEnd\n",
            )
        ]
    )

    assert [row["name"] for row in result.bundle["locomotors"]] == [
        "GoodLocomotor"
    ]
    assert result.report["parse_failures"] == [
        {
            "file": "data/ini/broken.ini",
            "block": "BadLocomotor",
            "message": "data/ini/broken.ini:1: unterminated Locomotor BadLocomotor",
        }
    ]
    assert result.bundle["diagnostics"][0]["template"] == "BadLocomotor"
