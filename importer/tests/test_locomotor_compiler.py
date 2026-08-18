"""Retail locomotion extraction and binding contract (Locomotion Phase A)."""

from __future__ import annotations

from pathlib import Path

import pytest

from openbfme_importer.locomotor_compiler import (
    compile_locomotor_templates,
    compile_object_locomotor_sets,
    referenced_locomotor_ids,
)
from openbfme_importer.playable_unit_compiler import _numeric_defines


REPO_ROOT = Path(__file__).resolve().parents[2]
RETAIL_ROOT = REPO_ROOT / "workspace" / "retail-extract"


@pytest.fixture(scope="module")
def retail_documents() -> dict[str, bytes]:
    root = RETAIL_ROOT / "data" / "ini"
    assert root.is_dir(), f"retail oracle missing: {root}"
    return {
        path.relative_to(RETAIL_ROOT).as_posix(): path.read_bytes()
        for path in root.rglob("*")
        if path.is_file() and path.suffix.casefold() in {".ini", ".inc"}
    }


@pytest.fixture(scope="module")
def compiled(retail_documents: dict[str, bytes]) -> tuple[dict[str, object], dict[str, object]]:
    constants = _numeric_defines(retail_documents)
    return (
        compile_locomotor_templates(retail_documents, constants),
        compile_object_locomotor_sets(retail_documents, constants),
    )


def _template(table: dict[str, object], name: str) -> dict[str, object]:
    return table["templates"][name]  # type: ignore[index]


def _values(row: dict[str, object]) -> dict[str, object]:
    return {
        key: field["value"]
        for key, field in row["fields"].items()  # type: ignore[union-attr]
    }


def test_four_oracle_templates_are_exact(compiled: tuple[dict[str, object], dict[str, object]]) -> None:
    table, _ = compiled
    human = _values(_template(table, "HumanLocomotor"))
    assert human == {
        "surfaces": ["GROUND", "RUBBLE"],
        "turnTime": 500,
        "turnTimeDamaged": 500,
        "fastTurnRadius": 3,
        "slowTurnRadius": 1,
        "acceleration": 510,
        "formationPriority": "MELEE1",
        "braking": 510,
        "minTurnSpeed": 0,
        "zAxisBehavior": "NO_Z_MOTIVE_FORCE",
        "appearance": "TWO_LEGS",
        "stickToGround": True,
        "canMoveBackwards": True,
        "backingUpSpeed": 0.33,
    }

    horse = _values(_template(table, "HorseLocomotor"))
    assert horse["turnTime"] == 1500
    assert horse["acceleration"] == 1500
    assert horse["braking"] == 2000
    assert horse["slowTurnRadius"] == 0
    assert horse["fastTurnRadius"] == 48
    assert horse["turnPivotOffset"] == 1
    assert horse["minTurnSpeed"] == 0.1
    assert horse["appearance"] == "FOUR_WHEELS"
    assert horse["closeEnoughDist"] == 2
    assert horse["canMoveBackwards"] is False

    catapult = _values(_template(table, "CatapultLocomotor"))
    assert catapult["slowTurnRadius"] == 24
    assert catapult["fastTurnRadius"] == 24
    assert catapult["turnTime"] == 1000
    assert catapult["acceleration"] == 1000
    assert catapult["braking"] == 1000
    assert catapult["minTurnSpeed"] == 0.66
    assert catapult["turnPivotOffset"] == -0.264

    fell_beast = _values(_template(table, "FellBeastLocomotor"))
    assert fell_beast["surfaces"] == ["AIR"]
    assert fell_beast["zAxisBehavior"] == "SURFACE_RELATIVE_HEIGHT"
    assert fell_beast["preferredHeight"] == 121
    assert fell_beast["preferredAttackHeight"] == 5
    assert fell_beast["lift"] == 1
    assert fell_beast["turnTime"] == 3500
    assert fell_beast["acceleration"] == 400
    assert fell_beast["braking"] == 1000
    assert fell_beast["minTurnSpeed"] == 0.3
    assert fell_beast["slideIntoPlaceTime"] == 900


def test_effective_retail_census(compiled: tuple[dict[str, object], dict[str, object]]) -> None:
    table, bindings = compiled
    assert len(table["templates"]) == 123  # type: ignore[arg-type]
    # Census verified 2026-08-18: authored per-object bindings
    unique_refs = len(referenced_locomotor_ids(bindings))
    total_refs = sum(len(rows) for rows in bindings.values())
    assert unique_refs >= 90, f"Expected at least 90 unique locomotor references, got {unique_refs}"
    assert total_refs >= 690, f"Expected at least 690 total references, got {total_refs}"


@pytest.mark.parametrize(
    ("object_name", "template_name"),
    [
        ("GondorFighter", "HumanLocomotor"),
        ("GondorTrebuchet", "CatapultLocomotor"),
    ],
)
def test_object_normal_binding_keeps_object_speed(
    compiled: tuple[dict[str, object], dict[str, object]],
    object_name: str,
    template_name: str,
) -> None:
    _, bindings = compiled
    normal = bindings[object_name]["SET_NORMAL"]
    assert normal["locomotorId"] == f"locomotors/{template_name}"
    assert float(normal["speed"]) >= 0
    assert normal["sourceIni"].startswith("data/ini/object/")
    assert int(normal["line"]) > 0


def test_unknown_template_and_binding_fields_are_preserved() -> None:
    documents = {
        "data/ini/locomotor.ini": (
            b"Locomotor TestLocomotor\n"
            b"  Acceleration = 10\n"
            b"  Braking = 20\n"
            b"  TurnTime = 1000\n"
            b"  FutureRetailField = OPAQUE 7\n"
            b"End\n"
        ),
        "data/ini/object/test.ini": (
            b"Object TestUnit\n"
            b"  LocomotorSet\n"
            b"    Condition = SET_NORMAL\n"
            b"    Locomotor = TestLocomotor\n"
            b"    Speed = 42\n"
            b"    FutureBindingField = KEEP_ME\n"
            b"  End\n"
            b"End\n"
        ),
    }
    table = compile_locomotor_templates(documents)
    template_unknown = table["templates"]["TestLocomotor"]["unsupported"]  # type: ignore[index]
    assert template_unknown == [{
        "key": "FutureRetailField",
        "raw": "OPAQUE 7",
        "sourceField": "FutureRetailField",
        "sourceIni": "data/ini/locomotor.ini",
        "line": 5,
    }]
    binding = compile_object_locomotor_sets(documents)["TestUnit"]["SET_NORMAL"]
    assert binding["unsupported"] == [{
        "key": "FutureBindingField",
        "raw": "KEEP_ME",
        "sourceIni": "data/ini/object/test.ini",
        "line": 6,
    }]
