from __future__ import annotations

import copy

import pytest

from openbfme_importer.m3_pack_expansion import BUILDINGS, RUNTIME_PATHS, build_icon_census, validate_tooltip_closure
from importer.tests.test_m3_pack_expansion import fixture_report


def test_icon_census_covers_every_building_command_and_portrait() -> None:
    census = build_icon_census(fixture_report())
    assert census["complete"] is True
    assert census["summary"] == {"buildingCount": len(BUILDINGS), "buildingButtonCount": len(BUILDINGS), "commandCount": len(BUILDINGS), "dynamicIconCount": 0, "missingCount": 0}
    assert [row["id"] for row in census["buildings"]] == list(BUILDINGS)
    assert all(row["portraits"][0]["iconPresent"] for row in census["buildings"])
    assert all(row["constructionCommands"][0]["iconPresent"] for row in census["buildings"])
    assert all(row["commands"][0]["iconPresent"] for row in census["buildings"])

    report = fixture_report()
    report["definitions"]["commandButtons"][0]["fields"] = {"Command": ["REVIVE"]}
    dynamic_census = build_icon_census(report)
    dynamic = dynamic_census["buildings"][0]["commands"][0]
    assert dynamic_census["complete"] is False
    assert dynamic["iconPresent"] is False
    assert dynamic["iconSource"] == "unresolved-runtime-hero-slot"
    assert dynamic_census["missing"][0]["reason"] == "unresolved-dynamic-hero-slot"
    assert dynamic_census["missing"][0]["id"].endswith(".ButtonImage")
    assert all(row.get("id") and row.get("reason") for row in dynamic_census["missing"])


def test_icon_census_reports_missing_authored_icon_and_fails_closed_on_missing_command() -> None:
    report = fixture_report()
    report["resolvedLeaves"]["mappedImages"] = [
        row for row in report["resolvedLeaves"]["mappedImages"] if row["id"] != "Image_11"
    ]
    census = build_icon_census(report)
    assert census["complete"] is False
    assert census["summary"]["missingCount"] == 3

    malformed = fixture_report()
    missing_id = malformed["definitions"]["commandSets"][-1]["buttons"][0]
    malformed["definitions"]["commandButtons"] = [
        row
        for row in malformed["definitions"]["commandButtons"]
        if row["id"] != missing_id
    ]
    with pytest.raises(ValueError, match="missing command button"):
        build_icon_census(malformed)


def test_construction_tooltips_participate_in_localized_string_closure() -> None:
    report = fixture_report()
    report["definitions"]["commandButtons"][1]["fields"].update(
        {"TextLabel": ["CONTROLBAR:BuildFixture"], "DescriptLabel": ["CONTROLBAR:BuildFixtureDesc"]}
    )
    icons = build_icon_census(report)
    runtime = {
        RUNTIME_PATHS["icons"]: icons,
        RUNTIME_PATHS["upgrades"]: {"upgrades": []},
        RUNTIME_PATHS["spellbook"]: {"sciences": [], "powers": []},
    }
    base = {"runtime_data": {"data/strings.json": {"strings": {}}}}
    with pytest.raises(ValueError, match="BuildFixture"):
        validate_tooltip_closure(runtime, base)
