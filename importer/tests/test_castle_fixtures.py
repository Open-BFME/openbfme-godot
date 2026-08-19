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
    "data/ini/gamedata.ini": (
        b"#define TEST_GARRISON_HEALTH 1500\n"
        b"#define GENERIC_FACTION_GARRISONABLE ANY +INFANTRY +BANNER "
        b"-CAVALRY -SUMMONED -WildSpiderling -WildSpiderlingHorde "
        b"-COMBO_HORDE -IsengardSharku -AngmarThrallMaster\n"
    ),
    "data/ini/commandset.ini": (
        b"CommandSet CastleGateCommandSet\n"
        b"  1 = Command_ToggleGate\n"
        b"  2 = Command_StartSelfRepair\n"
        b"  6 = Command_Sell\n"
        b"End\n"
    ),
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
        # CastleGateCommandSet carries Command_ToggleGate in retail
        # commandset.ini:6845-6852.
        b"  CommandSet = CastleGateCommandSet\n"
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
        # Field-for-field the Erebor gate (ereborbuildings.ini:6406); all 32
        # GateOpenAndCloseBehavior blocks in the RotWK effective-assets tree
        # author this whole set, which is why the contract requires it.
        b"  Behavior = GateOpenAndCloseBehavior ModuleTag_GATE\n"
        b"    ResetTimeInMilliseconds = 5000\n"
        b"    OpenByDefault = Yes\n"
        b"    PercentOpenForPathing = 50\n"
        b"    SoundOpeningGateLoop = GateOpenStart\n"
        b"    SoundClosingGateLoop = GateCloseStart\n"
        b"    SoundFinishedOpeningGate = GateOpenEnd\n"
        b"    SoundFinishedClosingGate = GateCloseEnd\n"
        b"    TimeBeforePlayingOpenSound = 9500\n"
        b"    TimeBeforePlayingClosedSound = 9500\n"
        b"  End\n"
        # Retail Helm's Deep gates author both optional policies
        # (helmsdeepbuildings.ini:6286-6293).
        b"  Behavior = FakePathfindPortalBehaviour ModuleTag_FAKEPATHFIND\n"
        b"    AllowEnemies = No\n"
        b"    AllowNonSkirmishAIUnits = No\n"
        b"  End\n"
        b"  Behavior = AIGateUpdate ModuleTag_AIGateUpdate\n"
        b"    TriggerWidthX = 300.0\n"
        b"    TriggerWidthY = 150.0\n"
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
        # Field-for-field the Erebor garrisonable tower
        # (ereborbuildings.ini:5279), the retail shape the L1 oracle pins.
        b"  Behavior = HordeGarrisonContain ModuleTag_hordeGarrison\n"
        b"    ObjectStatusOfContained = UNSELECTABLE CAN_ATTACK ENCLOSED\n"
        b"    ContainMax = 3\n"
        b"    DamagePercentToUnits = 0%\n"
        b"    PassengerFilter = GENERIC_FACTION_GARRISONABLE\n"
        b"    AllowEnemiesInside = No\n"
        b"    AllowAlliesInside = No\n"
        b"    AllowNeutralInside = Yes\n"
        b"    AllowOwnPlayerInsideOverride = Yes\n"
        b"    NumberOfExitPaths = 1\n"
        b"    PassengerBonePrefix = PassengerBone:ARROW_ KindOf:INFANTRY\n"
        b"    EntryOffset = X:50.0 Y:0.0 Z:0.0\n"
        b"    EntryPosition = X:20.0 Y:0.0 Z:0.0\n"
        b"    ExitOffset = X:50.0 Y:0.0 Z:0.0\n"
        b"    EnterSound = RuinedTowerEnterSound\n"
        b"    KillPassengersOnDeath = No\n"
        b"    ShowPips = No\n"
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
        # Field-for-field the Erebor throne (ereborbuildings.ini:5594) apart
        # from the capacity; every castle-map keep authors this whole block.
        b"  Behavior = CitadelSlaughterHordeContain ModuleTag_Slaughter\n"
        b"    PassengerFilter = GENERIC_FACTION_SLAUGHTERABLE\n"
        b"    ObjectStatusOfContained = UNSELECTABLE ENCLOSED\n"
        b"    CashBackPercent = 200%\n"
        b"    ContainMax = 20\n"
        b"    AllowEnemiesInside = No\n"
        b"    AllowAlliesInside = No\n"
        b"    AllowNeutralInside = No\n"
        b"    AllowOwnPlayerInsideOverride = Yes\n"
        b"    EnterSound = MordorSlaughterhouseEnterSound\n"
        b"    EntryOffset = X:-117.0 Y:-150.0 Z:0.0\n"
        b"    EntryPosition = X:-117.0 Y:-30.0 Z:0.0\n"
        b"    ExitOffset = X:-117.0 Y:-150.0 Z:0.0\n"
        b"    StatusForRingEntry = HOLDING_THE_RING\n"
        b"    UpgradeForRingEntry = Upgrade_RingHero Upgrade_FortressRingHero\n"
        b"    ObjectToDestroyForRingEntry = NONE +TheDroppedRing\n"
        b"    FXForRingEntry = FX_OneRingFlare\n"
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
    assert gate["fields"]["TimeBeforePlayingOpenSound"]["milliseconds"] == 9500
    assert gate["fields"]["TimeBeforePlayingClosedSound"]["milliseconds"] == 9500
    assert gate["fields"]["SoundFinishedOpeningGate"]["value"] == "GateOpenEnd"
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
    assert block["commandSet"] == "CastleGateCommandSet"
    assert block["commandSetRows"] == [
        {"slot": 1, "commandId": "Command_ToggleGate"},
        {"slot": 2, "commandId": "Command_StartSelfRepair"},
        {"slot": 6, "commandId": "Command_Sell"},
    ]
    assert block["aiGateUpdate"] == {
        "triggerWidthX": 300.0,
        "triggerWidthY": 150.0,
    }
    assert block["fakePathfindPortal"] == {
        "allowEnemies": False,
        "allowNonSkirmishAIUnits": False,
    }
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
        assert block["objectStatusOfContained"] == [
            "UNSELECTABLE", "CAN_ATTACK", "ENCLOSED"
        ]
        assert block["passengerFilter"] == [
            "ANY", "+INFANTRY", "+BANNER", "-CAVALRY", "-SUMMONED",
            "-WildSpiderling", "-WildSpiderlingHorde", "-COMBO_HORDE",
            "-IsengardSharku", "-AngmarThrallMaster",
        ]
        assert block["damagePercentToUnits"] == 0.0
        assert block["entryOffset"] == [50.0, 0.0, 0.0]
        assert block["exitOffset"] == [50.0, 0.0, 0.0]


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
            lambda doc: doc["fixtures"][0]["gate"].update(commandSet=""),
            "gate command set",
        ),
        (
            lambda doc: doc["fixtures"][0]["gate"].update(commandSetRows={}),
            "command set rows",
        ),
        (
            lambda doc: doc["fixtures"][0]["gate"].update(commandSetRows=[]),
            "command set rows",
        ),
        (
            lambda doc: doc["fixtures"][0]["gate"]["commandSetRows"][0].pop("slot"),
            "command set rows",
        ),
        (
            lambda doc: doc["fixtures"][0]["gate"]["commandSetRows"][0].pop("commandId"),
            "command set rows",
        ),
        (
            lambda doc: doc["fixtures"][0]["gate"]["aiGateUpdate"].pop(
                "triggerWidthY"
            ),
            "AI gate update",
        ),
        (
            lambda doc: doc["fixtures"][0]["gate"]["aiGateUpdate"].update(
                triggerWidthX=-300.0
            ),
            "AI gate update",
        ),
        (
            lambda doc: doc["fixtures"][0]["gate"]["fakePathfindPortal"].pop(
                "allowNonSkirmishAIUnits"
            ),
            "fake pathfind portal",
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


# --- lane L2b: lifecycle-structure reclassification ---------------------------

from openbfme_importer.castle_fixtures import (  # noqa: E402
    castle_fixture_seed_disposition,
    map_fixture_object_id,
    rebind_castle_fixture_structures,
)


def _fixture_row(
    index: int, type_name: str, kind_of: list[str], role: str = "structure"
) -> dict[str, object]:
    return {
        "typeName": type_name,
        "role": role,
        "index": index,
        "position": [0.0, 300.0, 0.0],
        "angle": 0.0,
        "kindOf": kind_of,
        "maxHealth": 1000.0,
        "armor": "TestWallArmor",
        "originalOwner": "Player_1/teamPlayer_1",
    }


def _model_row(type_name: str) -> dict[str, object]:
    slug = type_name.casefold()
    return {
        "typeName": type_name,
        "sourceVirtualModel": f"art/w3d/nb/{slug}.w3d",
        "glb": f"assets/models/props/{slug}.glb",
        "matchMethod": "exact-type-name",
    }


# --- seed disposition (the sim-seed filter, shared with the loader's mirror) --


def test_seed_disposition_seeds_combat_structures() -> None:
    row = _fixture_row(0, "TestMapGate", ["STRUCTURE", "IMMOBILE", "WALL_GATE"], "gate")
    assert castle_fixture_seed_disposition(row) == "seed"


def test_seed_disposition_defers_creep_lairs_to_the_creep_lane() -> None:
    row = _fixture_row(0, "FireDrakeLair", ["STRUCTURE", "IMMOBILE"])
    assert castle_fixture_seed_disposition(row) == "creep-lair-owned"
    # case-insensitive: the cooked typeName case is not guaranteed
    row["typeName"] = "warglair"
    assert castle_fixture_seed_disposition(row) == "creep-lair-owned"


def test_seed_disposition_skips_inert_scenery() -> None:
    row = _fixture_row(0, "RockHighPass03", ["STRUCTURE", "INERT", "ROCK_VENDOR"])
    assert castle_fixture_seed_disposition(row) == "inert-scenery"


def test_seed_disposition_skips_capturable_flags() -> None:
    row = _fixture_row(0, "CaptureFlag", ["STRUCTURE", "CAPTURABLE", "CAPTUREFLAG"])
    assert castle_fixture_seed_disposition(row) == "capturable-flag"


# --- map-fixture object ids ----------------------------------------------------


def test_map_fixture_object_id_is_loader_safe() -> None:
    assert (
        map_fixture_object_id("EreborGateDoors")
        == "bfme2.object.map-fixture.ereborgatedoors"
    )
    # SAGE type names may carry underscores; the loader's id alphabet cannot.
    assert (
        map_fixture_object_id("WOR_EreborThrone")
        == "bfme2.object.map-fixture.wor-ereborthrone"
    )


def test_map_fixture_object_id_refuses_unsafe_names() -> None:
    for bad in ("", "Foo Bar", "Foo/Bar", "Foo..Bar", "Foo-Bar-", "Foo.", "Éowyn"):
        with pytest.raises(CastleFixturesError):
            map_fixture_object_id(bad)


# --- rebind --------------------------------------------------------------------


def _rebind_bindings() -> dict[str, object]:
    return {
        "logical": [{"typeName": "TestLogical", "classification": "logical"}],
        "models": [
            _model_row("TestMapGate"),
            _model_row("TestGarrisonTower"),
            _model_row("TestTree"),
        ],
    }


def test_rebind_moves_seeded_fixture_types_to_structures() -> None:
    fixtures = {
        "fixtures": [
            _fixture_row(0, "TestMapGate", ["STRUCTURE", "WALL_GATE"], "gate"),
            _fixture_row(
                1, "TestGarrisonTower", ["STRUCTURE", "GARRISON"], "garrison"
            ),
        ]
    }
    bindings, evidence = rebind_castle_fixture_structures(
        _rebind_bindings(), fixtures
    )
    models = bindings["models"]
    structures = bindings["structures"]
    assert [row["typeName"] for row in models] == ["TestTree"]
    moved = {row["typeName"]: row for row in structures}
    assert set(moved) == {"TestMapGate", "TestGarrisonTower"}
    gate = moved["TestMapGate"]
    assert gate["objectId"] == "bfme2.object.map-fixture.testmapgate"
    # The renderable binding's art is reused verbatim — no second conversion.
    assert gate["glb"] == "assets/models/props/testmapgate.glb"
    assert gate["sourceVirtualModel"] == "art/w3d/nb/testmapgate.w3d"
    assert gate["matchMethod"] == "exact-type-name"
    assert evidence["movedTypeNames"] == ["TestGarrisonTower", "TestMapGate"]
    assert evidence["unboundFixtureTypeNames"] == []
    assert evidence["deferredPlacements"] == {}
    # The input document is never mutated.
    assert fixtures["fixtures"][0]["typeName"] == "TestMapGate"


def test_rebind_leaves_deferred_fixture_types_renderable() -> None:
    fixtures = {
        "fixtures": [
            _fixture_row(0, "FireDrakeLair", ["STRUCTURE"]),
            _fixture_row(1, "RockHighPass03", ["STRUCTURE", "INERT"]),
            _fixture_row(2, "RockHighPass03", ["STRUCTURE", "INERT"]),
            _fixture_row(3, "CaptureFlag", ["STRUCTURE", "CAPTURABLE"]),
            _fixture_row(4, "TestMapGate", ["STRUCTURE", "WALL_GATE"], "gate"),
        ]
    }
    bindings = {
        "logical": [],
        "models": [
            _model_row("FireDrakeLair"),
            _model_row("RockHighPass03"),
            _model_row("CaptureFlag"),
            _model_row("TestMapGate"),
        ],
    }
    bindings, evidence = rebind_castle_fixture_structures(bindings, fixtures)
    assert [row["typeName"] for row in bindings["models"]] == [
        "CaptureFlag",
        "FireDrakeLair",
        "RockHighPass03",
    ]
    assert [row["typeName"] for row in bindings["structures"]] == ["TestMapGate"]
    assert evidence["deferredPlacements"] == {
        "capturable-flag": 1,
        "creep-lair-owned": 1,
        "inert-scenery": 2,
    }


def test_rebind_records_fixture_types_without_a_visual_binding() -> None:
    fixtures = {
        "fixtures": [
            _fixture_row(0, "TestMapGate", ["STRUCTURE", "WALL_GATE"], "gate"),
            _fixture_row(1, "TestGhostWall", ["STRUCTURE"], "wall"),
        ]
    }
    bindings = {"logical": [], "models": [_model_row("TestMapGate")]}
    bindings, evidence = rebind_castle_fixture_structures(bindings, fixtures)
    assert [row["typeName"] for row in bindings["structures"]] == ["TestMapGate"]
    # Nothing is invented: an unbound fixture type is named, not silently
    # left renderable and not given a fabricated GLB.
    assert evidence["unboundFixtureTypeNames"] == ["TestGhostWall"]


def test_rebind_refuses_a_fixture_type_declared_logical() -> None:
    fixtures = {
        "fixtures": [
            _fixture_row(0, "TestMapGate", ["STRUCTURE", "WALL_GATE"], "gate"),
        ]
    }
    bindings = {
        "logical": [{"typeName": "testmapgate", "classification": "logical"}],
        "models": [],
    }
    with pytest.raises(CastleFixturesError, match="logical"):
        rebind_castle_fixture_structures(bindings, fixtures)


def test_rebind_refuses_object_id_casefold_collisions() -> None:
    fixtures = {
        "fixtures": [
            _fixture_row(0, "Foo_Bar", ["STRUCTURE"]),
            _fixture_row(1, "Foo-Bar", ["STRUCTURE"]),
        ]
    }
    bindings = {"logical": [], "models": [_model_row("Foo_Bar"), _model_row("Foo-Bar")]}
    with pytest.raises(CastleFixturesError, match="collid"):
        rebind_castle_fixture_structures(bindings, fixtures)


def test_rebind_refuses_malformed_binding_rows() -> None:
    fixtures = {
        "fixtures": [
            _fixture_row(0, "TestMapGate", ["STRUCTURE", "WALL_GATE"], "gate"),
        ]
    }
    broken = _model_row("TestMapGate")
    broken["glb"] = ""
    with pytest.raises(CastleFixturesError, match="glb"):
        rebind_castle_fixture_structures(
            {"logical": [], "models": [broken]}, fixtures
        )
