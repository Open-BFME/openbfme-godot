from __future__ import annotations

import json
import os
from pathlib import Path
import subprocess
import sys

from openbfme_importer.cook._bundle import write_bundle
from openbfme_importer.cook.combat import cook_documents, cook_ini_root


FIXTURE_ROOT = Path(__file__).parent / "fixtures" / "cook"
IMPORTER_ROOT = Path(__file__).resolve().parents[1]
CONTRACT_FIXTURE = (
    Path(__file__).resolve().parents[2]
    / "contracts"
    / "fixtures"
    / "bundle-v1.json"
)


def _named(rows: list[dict[str, object]], name: str) -> dict[str, object]:
    return next(row for row in rows if row["name"] == name)


def test_combat_round_trips_every_assignment_and_nugget_in_authored_order() -> None:
    bundle = cook_ini_root(FIXTURE_ROOT).bundle
    weapon = _named(bundle["weapons"], "CookSword")

    assert weapon["fields"] == {
        "AttackRange": 300,
        "MinimumAttackRange": 10.5,
        "DelayBetweenShots": 500,
        "FireFX": ["CookFireOne", "CookFireTwo"],
    }
    assert [row["kind"] for row in weapon["nuggets"]] == [
        "DamageNugget",
        "MetaImpactNugget",
        "ProjectileNugget",
        "other",
    ]
    assert weapon["nuggets"][0]["fields"] == {
        "Damage": 55,
        "Radius": 5,
        "DamageType": "SLASH",
        "DamageFXType": "CookDamageFX",
        "DeathType": "NORMAL",
    }
    assert weapon["nuggets"][1]["fields"] == {
        "Amount": 10,
        "Radius": 12.5,
        "TaperOff": 0.5,
    }
    assert weapon["nuggets"][2]["fields"] == {
        "ProjectileTemplateName": "CookProjectile"
    }
    assert weapon["nuggets"][3]["fields"] == {"LockWeaponSlot": "PRIMARY"}

    armor = _named(bundle["armors"], "CookArmor")
    assert armor["entries"] == [
        {"damage_type": "DEFAULT", "percent": 100},
        {"damage_type": "SLASH", "percent": 35},
    ]
    assert armor["fields"] == {"DamageScalar": "90%"}
    damage_fx = _named(bundle["damage_fx"], "CookDamageFX")
    assert damage_fx["fields"] == {
        "AmountForMajorFX": 10,
        "ThrottleTime": 250,
        "Sound": ["CookImpactOne", "CookImpactTwo"],
    }
    assert bundle["defines"]["COOK_WEAPON_DAMAGE"] == 55
    assert bundle["defines"]["COOK_ARMOR_PERCENT"] == "35%"


def test_combat_output_is_byte_identical_twice_and_matches_golden(tmp_path: Path) -> None:
    first = tmp_path / "first.json"
    second = tmp_path / "second.json"
    write_bundle(cook_ini_root(FIXTURE_ROOT).bundle, first)
    write_bundle(cook_ini_root(FIXTURE_ROOT).bundle, second)

    assert first.read_bytes() == second.read_bytes()
    assert first.read_bytes() == CONTRACT_FIXTURE.read_bytes()


def test_combat_cli_entrypoint_writes_bundle_and_report(tmp_path: Path) -> None:
    out = tmp_path / "bundle.json"
    report = tmp_path / "report.json"
    env = dict(os.environ, PYTHONPATH=str(IMPORTER_ROOT))
    result = subprocess.run(
        [
            sys.executable,
            "-m",
            "openbfme_importer.cook.combat",
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
    assert len(json.loads(out.read_text(encoding="utf-8"))["weapons"]) == 1
    census = json.loads(report.read_text(encoding="utf-8"))
    assert census["weapons"] == 1
    assert census["armors"] == 1
    assert census["nuggets_by_kind"] == {
        "DamageNugget": 1,
        "MetaImpactNugget": 1,
        "ProjectileNugget": 1,
        "other": 1,
    }
    assert census["parse_failures"] == []


def test_malformed_weapon_is_named_and_valid_sibling_survives() -> None:
    result = cook_documents(
        [
            (
                "data/ini/broken.ini",
                b"Weapon BadWeapon\n  DamageNugget\n    Damage = 1\nEnd\n"
                b"Weapon GoodWeapon\n  AttackRange = 100\nEnd\n",
            )
        ]
    )

    assert [row["name"] for row in result.bundle["weapons"]] == ["GoodWeapon"]
    assert result.report["parse_failures"] == [
        {
            "file": "data/ini/broken.ini",
            "block": "BadWeapon",
            "message": "data/ini/broken.ini:1: unterminated Weapon BadWeapon",
        }
    ]
    assert result.bundle["diagnostics"] == [
        {
            "template": "BadWeapon",
            "message": "data/ini/broken.ini:1: unterminated Weapon BadWeapon",
        }
    ]
