"""Typed SubObjectsUpgrade compile: show/hide tokens stay executable; texture rows stay deferred."""

from __future__ import annotations

from openbfme_importer.module_contracts import compile_sub_objects_upgrades
from openbfme_importer.sage_cst import parse_sage_document


def _lineage(text: str, name: str = "FixtureObject"):
    document = parse_sage_document(
        text.encode("utf-8"),
        virtual_path="data/ini/object/fixture.ini",
    )
    objects = [obj for obj in document.objects if obj.name.casefold() == name.casefold()]
    assert objects, f"missing object {name}"
    return objects


def test_sub_objects_upgrade_show_hide_is_executable() -> None:
    rows = compile_sub_objects_upgrades(
        _lineage(
            """
Object FixtureObject
  Behavior = SubObjectsUpgrade ModuleTag_Fire
    TriggeredBy = Upgrade_GondorFireStones
    ShowSubObjects = FirePlane
    HideSubObjects = Banner01
  End
End
"""
        ),
        "FixtureObject",
    )
    assert len(rows) == 1
    assert rows[0]["module"] == "SubObjectsUpgrade"
    assert rows[0]["runtimeStatus"] == "executable"
    assert rows[0]["fields"]["TriggeredBy"]["value"] == ["Upgrade_GondorFireStones"]
    assert rows[0]["fields"]["ShowSubObjects"]["value"] == ["FirePlane"]
    assert rows[0]["fields"]["HideSubObjects"]["value"] == ["Banner01"]


def test_sub_objects_upgrade_texture_row_stays_deferred() -> None:
    rows = compile_sub_objects_upgrades(
        _lineage(
            """
Object FixtureObject
  Behavior = SubObjectsUpgrade ModuleTag_Body
    TriggeredBy = Upgrade_CHW02
    ShowSubObjects = HLMT_01
    UpgradeTexture = CH_Body CH_Body_Alt
  End
End
"""
        ),
        "FixtureObject",
    )
    assert rows[0]["runtimeStatus"] == "deferred"
    assert rows[0]["fields"]["deferredFields"][0]["name"] == "UpgradeTexture"
