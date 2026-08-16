from __future__ import annotations

from pathlib import Path

import pytest

from openbfme_importer.catalog import InstallCatalog
from openbfme_importer.module_census import read_catalog_documents
from openbfme_importer.ship_catalog import compile_ship_catalog


EXPECTED_ROTWK_SHIPS = {
    "EGH_SlowTransport",
    "ElvenBattleShip",
    "ElvenFireShip",
    "ElvenShip_Interface",
    "ElvenShoreBombardShip",
    "ElvenTransportShip",
    "EvilFireShip",
    "EvilMenCorsairShip",
    "EvilMenTestCorsairShip",
    "EvilMenTransportShip",
    "EvilShip_Interface",
    "EvilShoreBombardShip",
    "TutorialElvenBattleShip",
}


def _fixture_documents() -> dict[str, bytes]:
    return {
        "data/ini/commandset.ini": b"",
        "data/ini/commandbutton.ini": b"",
        "data/ini/object/ships.ini": b"""
Object TestShip
  Side = Elves
  KindOf = PRELOAD SELECTABLE SHIP
  BuildCost = 100
  BuildTime = 5
  CommandPoints = 10
  VisionRange = 300
  VoiceEnterUnitTransportShip = VoiceEnterGeneric
  Draw = W3DScriptedModelDraw ModuleTag_Draw
    DefaultModelConditionState
      Model = TestShip
      ParticleSysBone = WakeFront WakeBack3 FollowBone:Yes
    End
    ModelConditionState = REALLYDAMAGED
      Model = TestShip_D2
      ParticleSysBone = FireBeam01 FireBoatBeam FollowBone:Yes
    End
  End
  Behavior = ShipSlowDeathBehavior ModuleTag_Death
    DeathTypes = ALL
    SinkDelay = 250
    SinkRate = 2.5
    DestructionDelay = 10000
  End
End
""",
    }


def _module(descriptor: dict, module_name: str) -> dict:
    rows = descriptor["gameplay"]["simulation"]["resolved"]["moduleContracts"]
    return next(row for row in rows if row["module"] == module_name)


def test_ship_descriptor_preserves_sink_voice_and_particle_sites() -> None:
    descriptor = compile_ship_catalog(_fixture_documents())["ships"][0]["descriptor"]

    assert _module(descriptor, "ShipSlowDeathBehavior")["fields"]["SinkDelay"][
        "milliseconds"
    ] == 250.0
    assert [
        row["id"]
        for row in descriptor["presentation"]["audioRoutes"]["container"][
            "VoiceEnterUnitTransportShip"
        ]
    ] == ["VoiceEnterGeneric"]
    assert descriptor["presentation"]["particleAttachments"] == [
        {
            "anchorBone": "WakeFront",
            "drawModuleKind": "W3DScriptedModelDraw",
            "drawModuleTag": "ModuleTag_Draw",
            "field": "ParticleSysBone",
            "followBone": True,
            "line": 13,
            "modelConditions": [],
            "options": ["FollowBone:Yes"],
            "particleSystemId": "WakeBack3",
            "sourceIni": "data/ini/object/ships.ini",
        },
        {
            "anchorBone": "FireBeam01",
            "drawModuleKind": "W3DScriptedModelDraw",
            "drawModuleTag": "ModuleTag_Draw",
            "field": "ParticleSysBone",
            "followBone": True,
            "line": 17,
            "modelConditions": ["REALLYDAMAGED"],
            "options": ["FollowBone:Yes"],
            "particleSystemId": "FireBoatBeam",
            "sourceIni": "data/ini/object/ships.ini",
        },
    ]


def test_retail_rotwk_catalog_emits_all_effective_ship_descriptors() -> None:
    repo = Path(__file__).resolve().parents[2]
    catalog_path = repo / ".private" / "retail-work" / "catalog" / "rotwk-layered.json"
    if not catalog_path.is_file():
        pytest.skip("operator RotWK catalog is unavailable")

    documents = dict(read_catalog_documents(InstallCatalog.load(catalog_path)))
    result = compile_ship_catalog(documents, game="rotwk")
    rows = {row["objectId"]: row for row in result["ships"]}

    assert set(rows) == EXPECTED_ROTWK_SHIPS
    assert all(row["runtimeStatus"] == "descriptor-ready" for row in rows.values())
    assert all(row["descriptor"]["category"] == "naval" for row in rows.values())
    assert all(
        row["descriptor"]["composition"]["primaryMemberObjectId"] == object_id
        for object_id, row in rows.items()
    )
    assert all(
        row["descriptor"]["gameplay"]["simulation"]["resolved"].get("movement")
        for row in rows.values()
    )
    sink_rows = {
        object_id
        for object_id, row in rows.items()
        if any(
            contract["module"] == "ShipSlowDeathBehavior"
            and "SinkDelay" in contract["fields"]
            for contract in row["descriptor"]["gameplay"]["simulation"]["resolved"][
                "moduleContracts"
            ]
        )
    }
    # EvilFireShip is the one effective ship that does not author or inherit
    # ShipSlowDeathBehavior. Preserve that retail exception instead of
    # fabricating a sink contract for uniformity.
    assert sink_rows == EXPECTED_ROTWK_SHIPS - {"EvilFireShip"}
    assert all(
        row["descriptor"]["presentation"]["particleAttachments"]
        for row in rows.values()
    )
