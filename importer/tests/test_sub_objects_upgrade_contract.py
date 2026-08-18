"""Typed SubObjectsUpgrade compile: show/hide tokens stay executable; texture rows stay deferred."""

from __future__ import annotations

import json
from pathlib import Path

import pytest

from openbfme_importer.module_contracts import (
    ModuleContractError,
    compile_all_module_contracts,
    compile_sub_objects_upgrades,
)
from openbfme_importer.sage_cst import parse_sage_document

_REPO = Path(__file__).resolve().parents[2]
_NOLDOR_VIRTUAL = (
    "data/ini/object/goodfaction/units/elven/noldorwarrior.ini"
)
_NOLDOR_RETAIL = (
    _REPO
    / "workspace"
    / "retail-work"
    / "editions"
    / "rotwk"
    / "cache"
    / "effective-assets"
    / _NOLDOR_VIRTUAL
)

_TRIGGERED_FIXTURE = """
Object FixtureObject
  Behavior = SubObjectsUpgrade ModuleTag_Fire
    TriggeredBy = Upgrade_GondorFireStones
    ShowSubObjects = FirePlane
    HideSubObjects = Banner01
  End
End
"""

# Captured 2026-08-18 at HEAD bc206d0, before the untriggered-row change.
_TRIGGERED_ROW_ORACLE = {
    "carrier": "Behavior",
    "extraction": "typed",
    "fields": {
        "HideSubObjects": {
            "authored": "Banner01",
            "line": 6,
            "sourceIni": "data/ini/object/fixture.ini",
            "value": ["Banner01"],
        },
        "ShowSubObjects": {
            "authored": "FirePlane",
            "line": 5,
            "sourceIni": "data/ini/object/fixture.ini",
            "value": ["FirePlane"],
        },
        "TriggeredBy": {
            "authored": "Upgrade_GondorFireStones",
            "line": 4,
            "sourceIni": "data/ini/object/fixture.ini",
            "value": ["Upgrade_GondorFireStones"],
        },
    },
    "line": 3,
    "module": "SubObjectsUpgrade",
    "runtimeStatus": "executable",
    "sourceIni": "data/ini/object/fixture.ini",
    "tag": "ModuleTag_Fire",
}


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
        _lineage(_TRIGGERED_FIXTURE),
        "FixtureObject",
    )
    assert len(rows) == 1
    assert rows[0]["module"] == "SubObjectsUpgrade"
    assert rows[0]["runtimeStatus"] == "executable"
    assert rows[0]["fields"]["TriggeredBy"]["value"] == ["Upgrade_GondorFireStones"]
    assert rows[0]["fields"]["ShowSubObjects"]["value"] == ["FirePlane"]
    assert rows[0]["fields"]["HideSubObjects"]["value"] == ["Banner01"]


def test_sub_objects_upgrade_triggered_row_is_byte_identical_to_pre_q26() -> None:
    rows = compile_sub_objects_upgrades(
        _lineage(_TRIGGERED_FIXTURE),
        "FixtureObject",
    )
    assert json.dumps(rows, sort_keys=True, separators=(",", ":")) == json.dumps(
        [_TRIGGERED_ROW_ORACLE], sort_keys=True, separators=(",", ":")
    )


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


def _assert_untriggered_row(rows: list[dict[str, object]], *, show: list[str]) -> None:
    assert len(rows) == 1
    row = rows[0]
    assert row["module"] == "SubObjectsUpgrade"
    assert row["runtimeStatus"] == "deferred"
    fields = row["fields"]
    assert fields["untriggered"] is True
    assert fields["reason"] == "no-active-triggeredby-authored"
    assert "TriggeredBy" not in fields
    assert fields["ShowSubObjects"]["value"] == show
    assert row["sourceIni"]
    assert isinstance(row["line"], int) and row["line"] > 0


def test_sub_objects_upgrade_commented_triggeredby_is_untriggered() -> None:
    rows = compile_sub_objects_upgrades(
        _lineage(
            """
Object FixtureObject
  Behavior = SubObjectsUpgrade ForgedBlades_Upgrade
;    TriggeredBy = Upgrade_ElvenForgedBlades
    ShowSubObjects = Forged_Blade
  End
End
"""
        ),
        "FixtureObject",
    )
    _assert_untriggered_row(rows, show=["Forged_Blade"])
    assert rows[0]["tag"] == "ForgedBlades_Upgrade"


def test_sub_objects_upgrade_absent_triggeredby_is_untriggered() -> None:
    rows = compile_sub_objects_upgrades(
        _lineage(
            """
Object FixtureObject
  Behavior = SubObjectsUpgrade ForgedBlades_Upgrade
    ShowSubObjects = Forged_Blade
  End
End
"""
        ),
        "FixtureObject",
    )
    _assert_untriggered_row(rows, show=["Forged_Blade"])


def test_sub_objects_upgrade_unknown_field_still_raises() -> None:
    with pytest.raises(ModuleContractError, match="unsupported fields"):
        compile_sub_objects_upgrades(
            _lineage(
                """
Object FixtureObject
  Behavior = SubObjectsUpgrade ModuleTag_Fire
    ShowSubObjects = Forged_Blade
    NotARealField = 1
  End
End
"""
            ),
            "FixtureObject",
        )


def test_noldor_warrior_retail_untriggered_sub_objects_upgrade() -> None:
    if not _NOLDOR_RETAIL.is_file():
        pytest.skip("RotWK NoldorWarrior effective-assets oracle is unavailable")
    document = parse_sage_document(
        _NOLDOR_RETAIL.read_bytes(),
        virtual_path=_NOLDOR_VIRTUAL,
    )
    objects = [
        obj for obj in document.objects if obj.name.casefold() == "noldorwarrior"
    ]
    assert objects, "missing Object NoldorWarrior"
    rows = compile_sub_objects_upgrades(objects, "NoldorWarrior")
    _assert_untriggered_row(rows, show=["Forged_Blade"])
    assert rows[0]["tag"] == "ForgedBlades_Upgrade"
    assert rows[0]["sourceIni"] == _NOLDOR_VIRTUAL
    compile_all_module_contracts(objects, "NoldorWarrior")
