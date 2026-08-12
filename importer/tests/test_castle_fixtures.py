"""Lane L2a synthetic tests: map-object corpus admission + fixtures document.

The admission path compiles retail object definitions for map-referenced
types (gates, garrison towers, walls) that never enter the command-reachable
faction corpus, and ``build_map_fixtures`` joins those definitions against a
map's placed object rows into the ``openbfme.sage-map-fixtures`` document.
All expectations here come from the synthetic INI corpus below; the retail
pins live in ``test_castle_fixtures_oracle.py``.
"""

from __future__ import annotations

import json

import pytest

from openbfme_importer.castle_fixtures import (
    MAP_FIXTURES_SCHEMA,
    CastleFixturesError,
    build_map_fixtures,
    compile_map_object_descriptor,
    validate_map_fixtures,
)
from openbfme_importer.sage_map import SageMapError, convert_sage_map
from importer.tests.test_sage_map import _synthetic_map


_DOCUMENTS = {
    "data/ini/gamedata.ini": b"#define TEST_GARRISON_HEALTH 1500\n",
    "data/ini/armor.ini": (
        b"Armor TestWallArmor\n"
        b"  Armor = DEFAULT 10%\n"
        b"  Armor = SIEGE 100%\n"
        b"  Armor = PIERCE 1%\n"
        b"End\n"
        b"\n"
        b"Armor TestStructureArmor\n"
        b"  Armor = DEFAULT 60%\n"
        b"  Armor = CRUSH 1%\n"
        b"  Armor = SLASH 40%\n"
        b"End\n"
    ),
    "data/ini/object/test/castle.ini": (
        b"Object TestMapGate\n"
        b"  KindOf = STRUCTURE IMMOBILE SELECTABLE BLOCKING_GATE WALL_GATE\n"
        b"  Body = ActiveBody ModuleTag_02\n"
        b"    MaxHealth = 20000.0\n"
        b"  End\n"
        b"  ArmorSet\n"
        b"    Conditions = None\n"
        b"    Armor = TestWallArmor\n"
        b"  End\n"
        b"  Behavior = KeepObjectDie ModuleTag_Rubble\n"
        b"    CollapsingTime = 10000\n"
        b"  End\n"
        b"  Behavior = GateOpenAndCloseBehavior ModuleTag_GATE\n"
        b"    ResetTimeInMilliseconds = 5000\n"
        b"    OpenByDefault = Yes\n"
        b"    PercentOpenForPathing = 50\n"
        b"  End\n"
        b"  Geometry = BOX\n"
        b"  GeometryMajorRadius = 130.0\n"
        b"  GeometryMinorRadius = 7.5\n"
        b"  GeometryHeight = 140\n"
        b"  GeometryName = Closed\n"
        b"  AdditionalGeometry = BOX\n"
        b"  GeometryMajorRadius = 7.5\n"
        b"  GeometryMinorRadius = 60\n"
        b"  GeometryHeight = 140\n"
        b"  GeometryOffset = X:-115 Y:68 Z:0\n"
        b"  GeometryName = OpenLeft\n"
        b"  AdditionalGeometry = BOX\n"
        b"  GeometryMajorRadius = 7.5\n"
        b"  GeometryMinorRadius = 60\n"
        b"  GeometryHeight = 140\n"
        b"  GeometryOffset = X:115 Y:68 Z:0\n"
        b"  GeometryName = OpenRight\n"
        b"End\n"
        b"\n"
        b"Object TestGarrisonTower\n"
        b"  KindOf = PRELOAD STRUCTURE SELECTABLE IMMOBILE GARRISON "
        b"GARRISONABLE_UNTIL_DESTROYED\n"
        b"  Body = StructureBody ModuleTag_05\n"
        b"    MaxHealth = TEST_GARRISON_HEALTH\n"
        b"    MaxHealthDamaged = 1000\n"
        b"    MaxHealthReallyDamaged = 500\n"
        b"  End\n"
        b"  ArmorSet\n"
        b"    Conditions = None\n"
        b"    Armor = TestStructureArmor\n"
        b"  End\n"
        b"  Behavior = HordeGarrisonContain ModuleTag_hordeGarrison\n"
        b"    ContainMax = 3\n"
        b"    AllowEnemiesInside = No\n"
        b"    AllowAlliesInside = No\n"
        b"    AllowNeutralInside = Yes\n"
        b"    AllowOwnPlayerInsideOverride = Yes\n"
        b"    NumberOfExitPaths = 1\n"
        b"    KillPassengersOnDeath = No\n"
        b"  End\n"
        b"  Geometry = BOX\n"
        b"  GeometryMajorRadius = 14.0\n"
        b"  GeometryMinorRadius = 14.0\n"
        b"  GeometryHeight = 105.0\n"
        b"End\n"
        b"\n"
        b"Object TestWalkableWall\n"
        b"  KindOf = STRUCTURE IMMOBILE WALK_ON_TOP_OF_WALL SELECTABLE\n"
        b"  Body = StructureBody ModuleTag_05\n"
        b"    MaxHealth = 110000.0\n"
        b"  End\n"
        b"  ArmorSet\n"
        b"    Conditions = None\n"
        b"    Armor = TestWallArmor\n"
        b"  End\n"
        b"  Geometry = BOX\n"
        b"  GeometryMajorRadius = 40.0\n"
        b"  GeometryMinorRadius = 114.4\n"
        b"  GeometryHeight = 54.0\n"
        b"End\n"
        b"\n"
        b"ChildObject TestChildWall TestWalkableWall\n"
        b"  KindOf = PRELOAD STRUCTURE SELECTABLE IMMOBILE WALL_SEGMENT "
        b"DEFENSIVE_WALL\n"
        b"  Body = StructureBody ModuleTag_05\n"
        b"    MaxHealth = 8000\n"
        b"  End\n"
        b"  ArmorSet\n"
        b"    Conditions = None\n"
        b"    Armor = TestStructureArmor\n"
        b"  End\n"
        b"  Geometry = BOX\n"
        b"  GeometryMajorRadius = 12.0\n"
        b"  GeometryMinorRadius = 30.0\n"
        b"  GeometryHeight = 60.0\n"
        b"End\n"
        b"\n"
        b"Object TestWallCatapult\n"
        b"  KindOf = STRUCTURE IMMOBILE WALL_UPGRADE SELECTABLE\n"
        b"  Body = ActiveBody ModuleTag_02\n"
        b"    MaxHealth = 3000.0\n"
        b"  End\n"
        b"  ArmorSet\n"
        b"    Conditions = None\n"
        b"    Armor = TestStructureArmor\n"
        b"  End\n"
        b"End\n"
        b"\n"
        b"Object TestStaticGate\n"
        b"  KindOf = STRUCTURE IMMOBILE WALL_GATE SELECTABLE\n"
        b"  Body = ActiveBody ModuleTag_05\n"
        b"    MaxHealth = 1000.0\n"
        b"  End\n"
        b"  ArmorSet\n"
        b"    Conditions = None\n"
        b"    Armor = TestWallArmor\n"
        b"  End\n"
        b"End\n"
        b"\n"
        b"Object TestKeep\n"
        b"  KindOf = STRUCTURE IMMOBILE SELECTABLE\n"
        b"  Body = ActiveBody ModuleTag_02\n"
        b"    MaxHealth = 5000.0\n"
        b"  End\n"
        b"  ArmorSet\n"
        b"    Conditions = None\n"
        b"    Armor = TestStructureArmor\n"
        b"  End\n"
        b"  Behavior = CitadelSlaughterHordeContain ModuleTag_Slaughter\n"
        b"    ContainMax = 20\n"
        b"  End\n"
        b"End\n"
        b"\n"
        b"Object TestTree\n"
        b"  KindOf = SELECTABLE\n"
        b"  Body = ActiveBody ModuleTag_02\n"
        b"    MaxHealth = 100.0\n"
        b"  End\n"
        b"End\n"
        b"\n"
        b"Object TestBrokenCart\n"
        b"  KindOf = STRUCTURE IMMOBILE\n"
        b"End\n"
    ),
}


def _placement(index: int, type_name: str, **properties: object) -> dict[str, object]:
    bag: dict[str, object] = {
        "originalOwner": "Player_1/teamPlayer_1",
        "objectInitialHealth": 100,
        "objectIndestructible": False,
        "objectEnabled": True,
        "objectTargetable": False,
    }
    bag.update(properties)
    return {
        "index": index,
        "typeName": type_name,
        "roadType": 0,
        "godotPosition": [float(index) * 10.0, 300.0, -100.0],
        "godotYawRadians": 1.5,
        "properties": bag,
    }


def _placements() -> list[dict[str, object]]:
    return [
        _placement(0, "TestMapGate"),
        _placement(1, "TestGarrisonTower", originalOwner="PlyrCivilian/teamPlyrCivilian"),
        _placement(2, "TestGarrisonTower", originalOwner="PlyrCivilian/teamPlyrCivilian"),
        _placement(3, "TestWalkableWall"),
        _placement(4, "TestChildWall"),
        _placement(5, "TestWallCatapult"),
        _placement(6, "TestKeep"),
        _placement(7, "TestTree"),
        _placement(8, "DirtRoad2"),
        _placement(9, "TestBrokenCart"),
        _placement(10, "TestStaticGate"),
    ]


def _fixtures() -> dict[str, object]:
    return build_map_fixtures(_DOCUMENTS, _placements())


def _module_row(descriptor: dict[str, object], module: str) -> dict[str, object]:
    rows = [
        row
        for row in descriptor["moduleContracts"]
        if isinstance(row, dict) and row.get("module") == module
    ]
    assert len(rows) == 1, f"expected exactly one {module} row"
    return rows[0]


# --- corpus admission -------------------------------------------------------


def test_admission_compiles_gate_health_armor_geometry_and_modules() -> None:
    descriptor = compile_map_object_descriptor("TestMapGate", _DOCUMENTS)
    assert descriptor["schema"] == "openbfme.map-object-descriptor"
    assert descriptor["objectId"] == "TestMapGate"
    assert "BLOCKING_GATE" in descriptor["kindOf"]
    health = descriptor["health"]
    assert health["primary"]["maxHealth"]["value"] == 20000.0
    assert descriptor["armor"]["setId"] == "TestWallArmor"
    pieces = {
        piece.get("name"): piece for piece in descriptor["geometry"]["pieces"]
    }
    assert set(pieces) == {"Closed", "OpenLeft", "OpenRight"}
    assert pieces["Closed"]["majorRadius"]["value"] == 130.0
    assert pieces["OpenLeft"]["offset"] == {"x": -115.0, "y": 68.0, "z": 0.0}
    gate = _module_row(descriptor, "GateOpenAndCloseBehavior")
    assert gate["fields"]["ResetTimeInMilliseconds"]["authored"].strip() == "5000"
    assert gate["fields"]["OpenByDefault"]["authored"].strip().casefold() == "yes"
    keep = _module_row(descriptor, "KeepObjectDie")
    assert keep["module"] == "KeepObjectDie"


def test_admission_resolves_health_macros_and_child_objects() -> None:
    tower = compile_map_object_descriptor("TestGarrisonTower", _DOCUMENTS)
    assert tower["health"]["primary"]["maxHealth"]["value"] == 1500
    assert tower["armor"]["setId"] == "TestStructureArmor"
    child = compile_map_object_descriptor("TestChildWall", _DOCUMENTS)
    assert child["definition"] == "ChildObject"
    assert "WALL_SEGMENT" in child["kindOf"]
    assert child["health"]["primary"]["maxHealth"]["value"] == 8000
    # Wholesale geometry inheritance: the child re-authors its own box.
    assert child["geometry"]["pieces"][0]["majorRadius"]["value"] == 12.0


def test_admission_fails_closed_on_unknown_type() -> None:
    with pytest.raises(CastleFixturesError, match="unknown"):
        compile_map_object_descriptor("TestNotAThing", _DOCUMENTS)


# --- fixtures builder -------------------------------------------------------


def test_fixtures_roles_and_exclusions() -> None:
    document = _fixtures()
    assert document["schema"] == MAP_FIXTURES_SCHEMA
    by_type: dict[str, list[dict[str, object]]] = {}
    for fixture in document["fixtures"]:
        by_type.setdefault(str(fixture["typeName"]), []).append(fixture)
    roles = {
        type_name: {str(row["role"]) for row in rows}
        for type_name, rows in by_type.items()
    }
    assert roles == {
        "TestMapGate": {"gate"},
        "TestGarrisonTower": {"garrison"},
        "TestWalkableWall": {"wall"},
        "TestChildWall": {"wall"},
        "TestWallCatapult": {"wall-mounted"},
        # The citadel slaughter-house contain is never a garrison (L1 trap).
        "TestKeep": {"structure"},
        # WALL_GATE KindOf without the module is retail's static gate prop.
        "TestStaticGate": {"static-gate"},
    }
    # Non-STRUCTURE types and unresolved scenery names are not admitted.
    assert "TestTree" not in by_type
    assert "DirtRoad2" not in by_type
    # STRUCTURE scenery with no authored Body is named in the omitted list,
    # never silently dropped and never a fixture.
    assert "TestBrokenCart" not in by_type
    assert document["omitted"] == [
        {"typeName": "TestBrokenCart", "reason": "no-authored-body-maxhealth"}
    ]


def test_fixtures_capabilities_are_the_v2_requirement_set() -> None:
    document = _fixtures()
    assert document["capabilities"] == [
        "walkable-walls",
        "defendable-gates",
        "wall-garrisons",
        "wall-mounted-defenses",
        "skirmish-ai-libraries",
    ]


def test_gate_fixture_carries_module_block_and_named_geometries() -> None:
    document = _fixtures()
    gate = next(
        row for row in document["fixtures"] if row["typeName"] == "TestMapGate"
    )
    assert gate["deathRule"] == "keep-object"
    block = gate["gate"]
    assert block["openByDefault"] is True
    assert block["resetMilliseconds"] == 5000
    assert block["percentOpenForPathing"] == 50
    geometries = block["geometries"]
    assert set(geometries) == {"Closed", "OpenLeft", "OpenRight"}
    closed = geometries["Closed"]
    assert closed["shape"] == "BOX"
    assert closed["majorRadius"] == 130.0
    assert closed["minorRadius"] == 7.5
    assert closed["height"] == 140
    assert closed["offset"] == [0.0, 0.0, 0.0]
    assert geometries["OpenLeft"]["offset"] == [-115.0, 68.0, 0.0]
    assert geometries["OpenRight"]["offset"] == [115.0, 68.0, 0.0]


def test_garrison_fixture_carries_contain_block() -> None:
    document = _fixtures()
    towers = [
        row for row in document["fixtures"] if row["typeName"] == "TestGarrisonTower"
    ]
    assert len(towers) == 2
    for tower in towers:
        block = tower["garrison"]
        assert block["containMax"] == 3
        assert block["allowEnemiesInside"] is False
        assert block["allowNeutralInside"] is True
        assert block["allowOwnPlayerInsideOverride"] is True
        assert block["numberOfExitPaths"] == 1
        assert block["killPassengersOnDeath"] is False


def test_fixtures_carry_type_contracts_and_placement_properties() -> None:
    document = _fixtures()
    gate = next(
        row for row in document["fixtures"] if row["typeName"] == "TestMapGate"
    )
    assert gate["maxHealth"] == 20000.0
    assert gate["armor"] == "TestWallArmor"
    assert "BLOCKING_GATE" in gate["kindOf"]
    assert gate["index"] == 0
    assert gate["position"] == [0.0, 300.0, -100.0]
    assert gate["angle"] == 1.5
    assert gate["originalOwner"] == "Player_1/teamPlayer_1"
    assert gate["initialHealth"] == 100
    assert gate["indestructible"] is False
    assert gate["enabled"] is True
    assert gate["targetable"] is False
    tower = next(
        row for row in document["fixtures"] if row["typeName"] == "TestGarrisonTower"
    )
    assert tower["maxHealth"] == 1500
    assert tower["originalOwner"] == "PlyrCivilian/teamPlyrCivilian"
    assert document["count"] == len(document["fixtures"])


# --- validation -------------------------------------------------------------


def test_validate_accepts_builder_output() -> None:
    validate_map_fixtures(_fixtures())


@pytest.mark.parametrize(
    "mutate, match",
    [
        (lambda doc: doc.update(schema="openbfme.bogus"), "schema"),
        (lambda doc: doc.update(schemaVersion=9), "schema"),
        (lambda doc: doc.update(capabilities=[]), "capabilit"),
        (
            lambda doc: doc.update(capabilities=["moonbeams"]),
            "unknown castle capability",
        ),
        (
            lambda doc: doc.update(
                capabilities=["defendable-gates", "walkable-walls"]
            ),
            "canonical order",
        ),
        (lambda doc: doc.update(count=999), "count"),
        (lambda doc: doc["fixtures"][0].update(role="moonbeam"), "role"),
        (
            lambda doc: doc["fixtures"][0].update(typeName=""),
            "typeName",
        ),
        (
            lambda doc: doc["fixtures"][1].update(index=doc["fixtures"][0]["index"]),
            "duplicate",
        ),
        (
            lambda doc: doc["fixtures"][0].update(originalOwner=""),
            "originalOwner",
        ),
        (
            lambda doc: doc["fixtures"][0].update(maxHealth=0),
            "maxHealth",
        ),
        (lambda doc: doc["fixtures"][0].update(armor=""), "armor"),
        (
            lambda doc: doc["fixtures"][0]["gate"].update(geometries={}),
            "geometries",
        ),
        (
            lambda doc: doc["fixtures"][1]["garrison"].update(containMax=0),
            "containMax",
        ),
        (
            lambda doc: doc["fixtures"][1].pop("garrison"),
            "garrison",
        ),
    ],
)
def test_validate_refuses_malformed_documents(mutate, match) -> None:
    document = json.loads(json.dumps(_fixtures()))
    mutate(document)
    with pytest.raises(CastleFixturesError, match=match):
        validate_map_fixtures(document)


# --- convert_sage_map emission ----------------------------------------------


def test_convert_sage_map_emits_fixtures_document(tmp_path) -> None:
    source, _ = _synthetic_map()
    source_path = tmp_path / "castle.map"
    source_path.write_bytes(source)
    fixtures = _fixtures()

    outputs = convert_sage_map(
        source_path, tmp_path / "output", fixtures=fixtures
    )

    fixtures_path = tmp_path / "output" / "fixtures.json"
    assert fixtures_path in outputs
    written = json.loads(fixtures_path.read_text(encoding="utf-8"))
    assert written["schema"] == MAP_FIXTURES_SCHEMA
    assert written["fixtures"] == fixtures["fixtures"]
    map_document = json.loads(
        (tmp_path / "output" / "map.json").read_text(encoding="utf-8")
    )
    assert map_document["fixtures"] == "fixtures.json"


def test_convert_sage_map_without_fixtures_stays_silent(tmp_path) -> None:
    source, _ = _synthetic_map()
    source_path = tmp_path / "plain.map"
    source_path.write_bytes(source)

    convert_sage_map(source_path, tmp_path / "output")

    assert not (tmp_path / "output" / "fixtures.json").exists()
    map_document = json.loads(
        (tmp_path / "output" / "map.json").read_text(encoding="utf-8")
    )
    assert "fixtures" not in map_document


def test_convert_sage_map_refuses_malformed_fixtures(tmp_path) -> None:
    source, _ = _synthetic_map()
    source_path = tmp_path / "castle.map"
    source_path.write_bytes(source)
    fixtures = json.loads(json.dumps(_fixtures()))
    fixtures["capabilities"] = ["moonbeams"]

    with pytest.raises(SageMapError, match="fixtures"):
        convert_sage_map(source_path, tmp_path / "output", fixtures=fixtures)
