"""Typed ModelConditionUpgrade compile: add/remove flags stay executable; temp rows stay deferred."""

from __future__ import annotations

from openbfme_importer.module_contracts import compile_model_condition_upgrades
from openbfme_importer.sage_cst import parse_sage_document


def _lineage(text: str, name: str = "FixtureObject"):
    document = parse_sage_document(
        text.encode("utf-8"),
        virtual_path="data/ini/object/fixture.ini",
    )
    objects = [obj for obj in document.objects if obj.name.casefold() == name.casefold()]
    assert objects, f"missing object {name}"
    return objects


def test_model_condition_upgrade_add_flags_is_executable() -> None:
    rows = compile_model_condition_upgrades(
        _lineage(
            """
Object FixtureObject
  Behavior = ModelConditionUpgrade ModuleTag_Banner
    TriggeredBy = Upgrade_GondorForgedBlades
    AddConditionFlags = USER_1
    Permanent = Yes
  End
End
"""
        ),
        "FixtureObject",
    )
    assert len(rows) == 1
    assert rows[0]["module"] == "ModelConditionUpgrade"
    assert rows[0]["runtimeStatus"] == "executable"
    assert rows[0]["fields"]["TriggeredBy"]["value"] == ["Upgrade_GondorForgedBlades"]
    assert rows[0]["fields"]["AddConditionFlags"]["value"] == ["USER_1"]
    assert rows[0]["fields"]["Permanent"]["value"] is True


def test_model_condition_upgrade_temp_row_stays_deferred() -> None:
    rows = compile_model_condition_upgrades(
        _lineage(
            """
Object FixtureObject
  Behavior = ModelConditionUpgrade ModuleTag_Temp
    TriggeredBy = Upgrade_TempBanner
    AddConditionFlags = USER_4
    AddTempConditionFlag = USER_4
    TempConditionTime = 3000
  End
End
"""
        ),
        "FixtureObject",
    )
    assert rows[0]["runtimeStatus"] == "deferred"
    names = {entry["name"] for entry in rows[0]["fields"]["deferredFields"]}
    assert names == {"AddTempConditionFlag", "TempConditionTime"}
