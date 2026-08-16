from __future__ import annotations

from copy import deepcopy
from pathlib import Path

import pytest

from openbfme_importer.catalog import InstallCatalog
from openbfme_importer.module_census import read_catalog_documents
from openbfme_importer.ship_catalog import (
    ShipCatalogError,
    compile_ship_catalog,
    validate_ship_catalog,
)


def _documents() -> dict[str, bytes]:
    return {
        "data/ini/object/ships.ini": b"""
Object TestShipyard
  CommandSet = TestShipyardCommandSet
  KindOf = PRELOAD SELECTABLE STRUCTURE
End

; An unrelated WorldBuilder-only malformed parent must not hide the naval
; family. Retail contains the same shape (MoriaDebrisPileA -> (Rocks)).
Object BrokenProp (Rocks)
  KindOf = IMMOBILE INERT
End

Object TestShipInterface
  Side = Elves
  KindOf = PRELOAD SELECTABLE CAN_ATTACK SHIP
  BuildCost = 500
  BuildTime = 30
  CommandPoints = 25
  VisionRange = 300
  SelectPortrait = UPTestShip
  VoiceSelect = TestShipVoiceSelect
  VoicePriority = 40
  VoiceMove = TestShipVoiceMove
  VoiceAttack = TestShipVoiceAttack
  Draw = W3DScriptedModelDraw ModuleTag_Draw
    DefaultModelConditionState
      Model = TestShipModel
    End
  End
  Behavior = ShipSlowDeathBehavior ModuleTag_Death
    DeathTypes = ALL
    SinkDelay = 0
    SinkRate = 2.5
    DestructionDelay = 10000
  End
End

ChildObject TestBuildableShip TestShipInterface
End

ChildObject TestScenarioShip TestShipInterface
End
""",
        "data/ini/commandset.ini": b"""
CommandSet TestShipyardCommandSet
  1 = Command_BuildTestShip
End
""",
        "data/ini/commandbutton.ini": b"""
CommandButton Command_BuildTestShip
  Command = UNIT_BUILD
  Object = TestBuildableShip
  ButtonImage = BITestShip
  TextLabel = CONTROLBAR:TestBuildableShip
  DescriptLabel = CONTROLBAR:ToolTipTestBuildableShip
End
""",
    }


def test_catalog_accounts_for_buildable_template_and_scenario_ships() -> None:
    documents = _documents()
    result = compile_ship_catalog(documents)
    repeated = compile_ship_catalog(dict(reversed(documents.items())))

    assert result == repeated
    assert result["summary"] == {
        "shipCount": 3,
        "buildableDescriptorCount": 1,
        "inheritanceTemplateCount": 1,
        "scenarioOnlyCount": 1,
        "runtimeDeferredCount": 0,
    }
    rows = {row["objectId"]: row for row in result["ships"]}
    assert rows["TestBuildableShip"]["runtimeStatus"] == "descriptor-ready"
    assert rows["TestBuildableShip"]["descriptor"]["category"] == "naval"
    assert rows["TestShipInterface"]["role"] == "inheritance-template"
    assert rows["TestScenarioShip"]["role"] == "scenario-only"
    for object_id in ("TestShipInterface", "TestScenarioShip"):
        row = rows[object_id]
        assert row["runtimeStatus"] == "descriptor-ready"
        assert row["descriptor"]["production"] == []
        admission = row["descriptor"]["scenarioAdmission"]
        assert admission["kind"] == "authored-non-buildable"
        assert admission["role"] == row["role"]
        assert admission["surfaces"] == [
            "map-placement",
            "script-spawn",
            "tutorial-script",
        ]
        assert admission["buildCommandExposed"] is False
        assert admission["evidence"] == "no-authored-unit-build-route"
        assert admission["sourceIni"] == "data/ini/object/ships.ini"
        assert admission["line"] > 0


def test_catalog_validation_rejects_a_ship_lost_from_the_summary() -> None:
    result = compile_ship_catalog(_documents())
    broken = deepcopy(result)
    broken["ships"].pop()

    with pytest.raises(ShipCatalogError, match="summary disagrees"):
        validate_ship_catalog(broken)


@pytest.mark.parametrize(
    ("catalog_name", "game"),
    (("bfme2.json", "bfme2"), ("rotwk-layered.json", "rotwk")),
)
def test_retail_ship_family_is_completely_accounted_for(
    catalog_name: str, game: str
) -> None:
    repo = Path(__file__).resolve().parents[2]
    catalog_path = repo / ".private" / "retail-work" / "catalog" / catalog_name
    if not catalog_path.is_file():
        pytest.skip("operator retail catalog is not available")
    catalog = InstallCatalog.load(catalog_path)
    documents = dict(read_catalog_documents(catalog))

    result = compile_ship_catalog(documents, game=game)
    rows = {row["objectId"]: row for row in result["ships"]}

    assert result["summary"] == {
        "shipCount": 13,
        "buildableDescriptorCount": 8,
        "inheritanceTemplateCount": 2,
        "scenarioOnlyCount": 3,
        "runtimeDeferredCount": 0,
    }
    assert rows["ElvenShip_Interface"]["role"] == "inheritance-template"
    assert rows["EvilShip_Interface"]["role"] == "inheritance-template"
    assert rows["ElvenTransportShip"]["runtimeStatus"] == "descriptor-ready"
    assert rows["EvilMenTransportShip"]["runtimeStatus"] == "descriptor-ready"
    assert all(row["runtimeStatus"] == "descriptor-ready" for row in result["ships"])
    assert all(
        row["descriptor"].get("scenarioAdmission", {}).get("buildCommandExposed")
        is False
        and row["descriptor"]["production"] == []
        for row in result["ships"]
        if row["role"] != "buildable"
    )
    assert all(
        "SHIP"
        in row["descriptor"].get(
            "effectiveKindOf",
            row["descriptor"].get("kindOf", {}).get("container", []),
        )
        for row in result["ships"]
    )
