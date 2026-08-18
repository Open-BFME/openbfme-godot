"""Retail locomotion extraction and binding contract (Locomotion Phase A)."""

from __future__ import annotations

from collections import Counter
from pathlib import Path

import pytest

from openbfme_importer.locomotor_compiler import (
    compile_locomotor_templates,
    compile_object_locomotor_sets,
    referenced_locomotor_ids,
    resolve_locomotor_template,
    turn_rate_degrees_per_second,
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
    """Measured 2026-08-18 against workspace/retail-extract/data/ini.

    123 templates live in `locomotor.ini`; five more live in
    `object/cinematic/cinematiclocomotor.ini` (CINE_Egret/Crow x3/Cine_Dragon).
    A reader that opens only `locomotor.ini` loses `Cine_DragonLocomotor`, which
    `CINE_GrnDrgn_Flying` binds — so 128, not 123, is the whole table.
    """

    table, bindings = compiled
    templates = table["templates"]
    assert isinstance(templates, dict)
    by_file = Counter(str(row["sourceIni"]) for row in templates.values())
    assert by_file == {
        "data/ini/locomotor.ini": 123,
        "data/ini/object/cinematic/cinematiclocomotor.ini": 5,
    }
    assert len(templates) == 128
    unique_refs = len(referenced_locomotor_ids(bindings))
    total_refs = sum(len(rows) for rows in bindings.values())
    assert len(bindings) == 420
    assert unique_refs == 97
    assert total_refs == 711


def test_every_referenced_locomotor_resolves_to_a_compiled_template(
    compiled: tuple[dict[str, object], dict[str, object]],
) -> None:
    """No object may bind a template this compiler cannot produce.

    This is the failing-first guard for the `Cine_DragonLocomotor` class of
    bug: with a `locomotor.ini`-only reader, five references dangle and the
    bound objects silently lose acceleration/braking/turn rate.
    """

    table, bindings = compiled
    templates = table["templates"]
    assert isinstance(templates, dict)
    defined = {str(name).casefold() for name in templates}
    dangling = sorted(
        {
            str(row["locomotor"])
            for rows in bindings.values()
            for row in rows.values()
            if str(row["locomotor"]).casefold() not in defined
        }
    )
    assert dangling == []


def test_percent_fields_accept_both_sage_spellings() -> None:
    """SAGE's INI::parsePercentToReal treats '%' as a separator.

    `MinTurnSpeed = 0%` and `MinTurnSpeed = 0` are the same authored value;
    retail's locomotor.ini always writes the sign, map.ini overrides need not.
    """

    documents = {
        "data/ini/locomotor.ini": (
            b"Locomotor SignedLocomotor\n"
            b"  Acceleration = 10\n"
            b"  Braking = 20\n"
            b"  TurnTime = 1000\n"
            b"  MinTurnSpeed = 33%\n"
            b"  BackingUpSpeed = 0%\n"
            b"End\n"
            b"Locomotor BareLocomotor\n"
            b"  Acceleration = 10\n"
            b"  Braking = 20\n"
            b"  TurnTime = 1000\n"
            b"  MinTurnSpeed = 33\n"
            b"  BackingUpSpeed = 0\n"
            b"End\n"
        )
    }
    table = compile_locomotor_templates(documents)
    signed = table["templates"]["SignedLocomotor"]["fields"]  # type: ignore[index]
    bare = table["templates"]["BareLocomotor"]["fields"]  # type: ignore[index]
    assert signed["minTurnSpeed"]["value"] == 0.33
    assert bare["minTurnSpeed"]["value"] == 0.33
    assert signed["backingUpSpeed"]["value"] == 0
    assert bare["backingUpSpeed"]["value"] == 0
    assert signed["minTurnSpeed"]["percentSignAuthored"] is True
    assert bare["minTurnSpeed"]["percentSignAuthored"] is False


def test_turn_rate_is_derived_from_turn_time_only() -> None:
    """Retail authors no `TurnRate` field; `TurnTime` (ms) is the sole source."""

    documents = {
        "data/ini/locomotor.ini": (
            b"Locomotor CatapultLike\n"
            b"  Acceleration = 1000\n"
            b"  Braking = 1000\n"
            b"  TurnTime = 1000\n"
            b"End\n"
        )
    }
    table = compile_locomotor_templates(documents)
    row = resolve_locomotor_template(table, "catapultlike")
    assert row is not None
    turn = turn_rate_degrees_per_second(row)
    assert turn is not None
    assert turn["value"] == 360.0
    assert turn["authoredValueMilliseconds"] == 1000


def test_templates_outside_locomotor_ini_are_compiled() -> None:
    documents = {
        "data/ini/locomotor.ini": (
            b"Locomotor HumanLocomotor\n  Acceleration = 510\n  Braking = 510\n"
            b"  TurnTime = 500\nEnd\n"
        ),
        "data/ini/object/cinematic/cinematiclocomotor.ini": (
            b"Locomotor Cine_DragonLocomotor ;modified Fellbeast locomotor\n"
            b"  Acceleration = 400\n  Braking = 1000\n  TurnTime = 3500\nEnd\n"
        ),
        "data/ini/object/cinematic/cinematicobjects.ini": (
            b"Object CINE_GrnDrgn_Flying\n  LocomotorSet\n"
            b"    Locomotor = Cine_DragonLocomotor\n"
            b"    Condition = SET_NORMAL\n    Speed = 95\n  End\nEnd\n"
        ),
    }
    table = compile_locomotor_templates(documents)
    assert set(table["templates"]) == {"HumanLocomotor", "Cine_DragonLocomotor"}  # type: ignore[arg-type]
    bindings = compile_object_locomotor_sets(documents)
    assert bindings["CINE_GrnDrgn_Flying"]["SET_NORMAL"]["locomotor"] == (
        "Cine_DragonLocomotor"
    )


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


def test_locomotor_set_condition_defaults_to_set_normal() -> None:
    """`Condition` is optional in SAGE; its default is SET_NORMAL.

    Retail authors it on all 715 LocomotorSet blocks under data/ini/object,
    but refusing a block that omits it would fail conversion on any map.ini or
    mod override that relies on the engine default.
    """

    documents = {
        "data/ini/locomotor.ini": (
            b"Locomotor FixtureLocomotor\n"
            b"  Acceleration = 10\n"
            b"  Braking = 20\n"
            b"  TurnTime = 1000\n"
            b"End\n"
        ),
        "data/ini/object/test.ini": (
            b"Object Implicit\n"
            b"  LocomotorSet\n"
            b"    Locomotor = FixtureLocomotor\n"
            b"    Speed = 40\n"
            b"  End\n"
            b"End\n"
        ),
    }
    binding = compile_object_locomotor_sets(documents)["Implicit"]["SET_NORMAL"]
    assert binding["locomotor"] == "FixtureLocomotor"
    assert binding["conditionAuthored"] is False
    assert binding["speed"] == 40


def test_unresolvable_known_field_is_recorded_not_fatal() -> None:
    """RotWK 2.01's merged tree relocates some `;` comment markers.

    `locomotor.ini:2802` in `editions/rotwk/cache/effective-assets` reads
    `FastTurnRadius = 12.0 Can turn in a 10 foot radius circle when moving. ;,;`.
    The narrow readers this compiler replaces never looked at FastTurnRadius, so
    refusing the whole document over it would fail conversions that used to
    succeed. The field becomes ABSENT — which every consumer treats as a loud
    gap — and the authored text survives in `unsupported`. The fields the mover
    actually needs are unaffected.
    """

    documents = {
        "data/ini/locomotor.ini": (
            b"Locomotor PorterLocomotor\n"
            b"  FastTurnRadius = 12.0 Can turn in a 10 foot radius circle when moving.\n"
            b"  Acceleration = 500\n"
            b"  Braking = 10\n"
            b"  TurnTime = 850\n"
            b"End\n"
        )
    }
    table = compile_locomotor_templates(documents)
    row = resolve_locomotor_template(table, "PorterLocomotor")
    assert row is not None
    assert "fastTurnRadius" not in row["fields"]
    assert row["fields"]["acceleration"]["value"] == 500
    assert row["fields"]["braking"]["value"] == 10
    assert turn_rate_degrees_per_second(row)["value"] == 360000.0 / 850.0
    recorded = [entry for entry in row["unsupported"] if entry["key"] == "FastTurnRadius"]
    assert len(recorded) == 1
    assert recorded[0]["raw"] == "12.0 Can turn in a 10 foot radius circle when moving."
    assert "unresolved value" in recorded[0]["reason"]


def test_gollum_and_trebuchet_bind_the_templates_the_sim_needs() -> None:
    """The two objects whose movement the sim used to invent.

    NeutralGollum -> HumanLocomotor (510/510, TurnTime 500 -> 720 deg/s);
    GondorTrebuchet -> CatapultLocomotor (1000/1000, TurnTime 1000 -> 360).
    """

    documents = {
        "data/ini/locomotor.ini": (
            b"Locomotor HumanLocomotor\n"
            b"  Acceleration = 510\n"
            b"  Braking = 510\n"
            b"  TurnTime = 500\n"
            b"End\n"
            b"Locomotor CatapultLocomotor\n"
            b"  Acceleration = 1000\n"
            b"  Braking = 1000\n"
            b"  TurnTime = 1000\n"
            b"End\n"
        ),
    }
    table = compile_locomotor_templates(documents)
    human = resolve_locomotor_template(table, "HumanLocomotor")
    catapult = resolve_locomotor_template(table, "CatapultLocomotor")
    assert human is not None and catapult is not None
    assert turn_rate_degrees_per_second(human)["value"] == 720.0
    assert turn_rate_degrees_per_second(catapult)["value"] == 360.0
