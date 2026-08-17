"""Retail-exact Karsh Blink leaves without overclaiming its admission gate."""

from __future__ import annotations

import re
from pathlib import Path

import pytest

from openbfme_importer.playable_unit_compiler import (
    _teleport_destination_weapon_fields,
    compile_playable_unit_descriptor,
    prepare_playable_unit_compiler,
    validate_playable_unit_descriptor,
)


REPO = Path(__file__).resolve().parents[2]
ROOT = REPO / ".private/retail-work/editions/rotwk/cache/effective-assets"
KARSH = "AngmarKarsh"
COMMAND = "Command_KarshBlink"


def _documents() -> dict[str, bytes]:
    if not ROOT.is_dir():
        pytest.skip("private RotWK effective-assets oracle is not present")
    return {
        path.relative_to(ROOT).as_posix().lower(): path.read_bytes()
        for path in ROOT.rglob("*")
        if path.is_file() and path.suffix.lower() in {".ini", ".inc"}
    }


def _ability(documents: dict[str, bytes]) -> dict[str, object]:
    descriptor = compile_playable_unit_descriptor(
        KARSH,
        documents,
        prepared=prepare_playable_unit_compiler(documents),
        game="rotwk",
        scenario_admission={"role": "scenario-only", "surfaces": ["script-spawn"]},
    )
    validate_playable_unit_descriptor(descriptor)
    return next(row for row in descriptor["abilities"] if row["id"] == COMMAND)


def _without_unproven_admission_branches(documents: dict[str, bytes]) -> dict[str, bytes]:
    result = dict(documents)
    path = "data/ini/specialpower.ini"
    payload = result[path]
    pattern = re.compile(
        rb"(?ms)^SpecialPower SpecialAbilityKarshBlink\s*$.*?^End\s*$"
    )
    match = pattern.search(payload)
    assert match is not None
    block = re.sub(
        rb"(?m)^\s*(?:Flags|ForbiddenObjectFilter|ForbiddenObjectRange)\s*=.*(?:\r?\n|$)",
        b"",
        match.group(0),
    )
    block = block.replace(b"SPECIAL_BALROG_WINGS", b"SPECIAL_GENERAL_TARGETLESS")
    result[path] = payload[: match.start()] + block + payload[match.end() :]
    return result


def test_retail_karsh_destination_weapon_is_exact_zero_damage_enemy_metaimpact() -> None:
    documents = _documents()
    contract = _teleport_destination_weapon_fields(
        documents,
        "CreateaHeroBlinkDestination",
        {},
        "Karsh Blink",
        named_definition_cache={},
        cache_lock=None,
    )

    assert contract["weaponId"] == "CreateaHeroBlinkDestination"
    assert contract["damage"] == 0
    assert contract["affects"] == "ENEMIES"
    assert contract["knockbackStrength"] == 50.0
    assert contract["knockbackRadius"] == 55.0
    assert contract["knockbackTaperOff"] == 0.75
    assert contract["knockbackZMult"] == 1.2
    assert contract["fireFxId"] == "FX_Blink"


def test_retail_karsh_stays_unavailable_until_enum_admission_is_proven() -> None:
    row = _ability(_documents())

    assert row["implementation"]["status"] == "unimplemented"
    assert "SPECIAL_BALROG_WINGS flip/cliff/layer admission is not represented" in row[
        "implementation"
    ]["reason"]
    assert row["effect"] == {"kind": "none"}


def test_karsh_proven_teleport_leaves_compile_when_unproven_gate_is_removed() -> None:
    row = _ability(_without_unproven_admission_branches(_documents()))

    assert row["implementation"]["status"] == "implemented"
    assert row["cooldownMs"] == 180000
    assert row["levelGate"]["requiredLevel"] == 7
    assert row["targeting"] == "point"
    effect = row["effect"]
    assert effect["kind"] == "teleport"
    assert "maxDistance" not in effect
    assert effect["busyForDurationMs"] == 1800
    assert effect["destinationWeapon"]["damage"] == 0
    assert effect["destinationWeapon"]["affects"] == "ENEMIES"
    assert effect["destinationWeapon"]["knockbackStrength"] == 50.0
    assert effect["destinationWeapon"]["knockbackRadius"] == 55.0
    assert effect["destinationWeapon"]["knockbackTaperOff"] == 0.75
    assert effect["destinationWeapon"]["knockbackZMult"] == 1.2
