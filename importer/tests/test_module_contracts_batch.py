"""Focused tests for the Class-B / death module contract batch."""

from __future__ import annotations

import pytest

from openbfme_importer.module_contracts import (
    ModuleContractError,
    compile_all_module_contracts,
    compile_attribute_modifier_upgrades,
    compile_create_object_die,
    compile_geometry_upgrades,
    compile_inactive_bodies,
    compile_keep_object_die,
    compile_spawn_point_production_exits,
    validate_module_contracts,
)
from openbfme_importer.sage_cst import parse_sage_document


def _lineage(text: str, name: str = "FixtureObject"):
    document = parse_sage_document(
        text.encode("utf-8"),
        virtual_path="data/ini/object/fixture.ini",
    )
    objects = [obj for obj in document.objects if obj.name.casefold() == name.casefold()]
    assert objects, f"missing object {name}"
    return objects


def test_attribute_modifier_upgrade_extracts_trigger_and_modifier() -> None:
    lineage = _lineage(
        """
Object FixtureObject
  Behavior = AttributeModifierUpgrade ModuleTag_Bonus
    TriggeredBy = Upgrade_EasyAISinglePlayer
    AttributeModifier = EasyAISinglePlayer_Bonus
  End
End
"""
    )
    rows = compile_attribute_modifier_upgrades(lineage, "FixtureObject")
    assert len(rows) == 1
    assert rows[0]["module"] == "AttributeModifierUpgrade"
    assert rows[0]["fields"]["TriggeredBy"]["value"] == ["Upgrade_EasyAISinglePlayer"]
    assert rows[0]["fields"]["AttributeModifier"]["value"] == "EasyAISinglePlayer_Bonus"
    assert rows[0]["runtimeStatus"] == "deferred"


def test_geometry_upgrade_extracts_show_hide() -> None:
    lineage = _lineage(
        """
Object FixtureObject
  Behavior = GeometryUpgrade Geom_ModuleTag_HideAll
    TriggeredBy = Upgrade_StructureLevel1
    ShowGeometry = Geom_Orig
    HideGeometry = Geom_V2
  End
End
"""
    )
    rows = compile_geometry_upgrades(lineage, "FixtureObject")
    assert rows[0]["fields"]["ShowGeometry"]["value"] == ["Geom_Orig"]
    assert rows[0]["fields"]["HideGeometry"]["value"] == ["Geom_V2"]


def test_inactive_body_is_presence_policy() -> None:
    lineage = _lineage(
        """
Object FixtureObject
  Body = InactiveBody ModuleTag_Body
  End
End
"""
    )
    rows = compile_inactive_bodies(lineage, "FixtureObject")
    assert rows[0]["module"] == "InactiveBody"
    assert rows[0]["fields"]["indestructible"] is True


def test_spawn_point_production_exit_requires_bone() -> None:
    lineage = _lineage(
        """
Object FixtureObject
  Behavior = SpawnPointProductionExitUpdate ModuleTag_ProductionExit
    SpawnPointBoneName = ARCHER
  End
End
"""
    )
    rows = compile_spawn_point_production_exits(lineage, "FixtureObject")
    assert rows[0]["fields"]["SpawnPointBoneName"]["value"] == "ARCHER"


def test_keep_object_die_defaults_all_and_does_not_destroy() -> None:
    lineage = _lineage(
        """
Object FixtureObject
  Behavior = KeepObjectDie ModuleTag_IWantRubble
  End
End
"""
    )
    rows = compile_keep_object_die(lineage, "FixtureObject")
    assert rows[0]["fields"]["deathTypes"] == "ALL"
    assert rows[0]["fields"]["destroyOnDeath"] is False
    assert rows[0]["runtimeStatus"] == "executable"
    assert rows[0]["extraction"] == "typed"


def test_create_object_die_requires_creation_list() -> None:
    lineage = _lineage(
        """
Object FixtureObject
  Behavior = CreateObjectDie ModuleTag_DropTheRing
    CreationList = OCL_TheOneRing
    DeathTypes = ALL
  End
End
"""
    )
    rows = compile_create_object_die(lineage, "FixtureObject")
    assert rows[0]["fields"]["CreationList"]["value"] == "OCL_TheOneRing"
    assert rows[0]["fields"]["deathTypes"] == "ALL"
    assert rows[0]["runtimeStatus"] == "executable"
    assert rows[0]["extraction"] == "typed"


def test_unknown_field_fails_closed() -> None:
    lineage = _lineage(
        """
Object FixtureObject
  Behavior = AttributeModifierUpgrade ModuleTag_Bonus
    TriggeredBy = Upgrade_X
    AttributeModifier = Bonus_X
    InventedField = Nope
  End
End
"""
    )
    with pytest.raises(ModuleContractError, match="unsupported fields"):
        compile_attribute_modifier_upgrades(lineage, "FixtureObject")


def test_batch_union_and_validator() -> None:
    lineage = _lineage(
        """
Object FixtureObject
  Body = InactiveBody ModuleTag_Body
  End
  Behavior = KeepObjectDie ModuleTag_Rubble
  End
  Behavior = AttributeModifierUpgrade ModuleTag_Bonus
    TriggeredBy = Upgrade_X
    AttributeModifier = Bonus_X
  End
End
"""
    )
    rows = compile_all_module_contracts(lineage, "FixtureObject")
    modules = {row["module"] for row in rows}
    assert modules == {"InactiveBody", "KeepObjectDie", "AttributeModifierUpgrade"}
    validate_module_contracts(rows, label="fixture")


def test_module_name_literals_are_census_visible() -> None:
    # AST-scan consumption requires the exact module-type string in pipeline files.
    from pathlib import Path

    from openbfme_importer.module_contracts import (
        OPAQUE_DEFERRED_MODULE_KINDS,
        TYPED_MODULE_KINDS,
    )

    text = Path("importer/openbfme_importer/module_contracts.py").read_text(
        encoding="utf-8"
    )
    for name in sorted(TYPED_MODULE_KINDS | OPAQUE_DEFERRED_MODULE_KINDS):
        assert f'"{name}"' in text


def test_opaque_deferred_preserves_all_fields_and_never_executes() -> None:
    from openbfme_importer.module_contracts import compile_opaque_deferred_module

    lineage = _lineage(
        """
Object FixtureObject
  Behavior = PhysicsBehavior ModuleTag_Physics
    Mass = 1.0
    Friction = 0.1
    InventedButPreserved = Hello
  End
  Draw = W3DTreeDraw ModuleTag_Draw
    Model = TBTree
  End
End
"""
    )
    physics = compile_opaque_deferred_module(lineage, "PhysicsBehavior", "FixtureObject")
    assert len(physics) == 1
    assert physics[0]["extraction"] == "opaque-authored"
    assert physics[0]["runtimeStatus"] == "deferred"
    assert set(physics[0]["fields"]) == {"Mass", "Friction", "InventedButPreserved"}
    tree = compile_opaque_deferred_module(lineage, "W3DTreeDraw", "FixtureObject")
    assert tree[0]["carrier"].casefold() == "draw"
    assert tree[0]["fields"]["Model"]["authored"].strip() == "TBTree"


def test_opaque_rejects_typed_kind_and_executable_claim() -> None:
    from openbfme_importer.module_contracts import (
        compile_opaque_deferred_module,
        validate_module_contracts,
    )

    lineage = _lineage(
        """
Object FixtureObject
  Body = InactiveBody ModuleTag_Body
  End
End
"""
    )
    with pytest.raises(ModuleContractError, match="typed extractor"):
        compile_opaque_deferred_module(lineage, "InactiveBody", "FixtureObject")
    with pytest.raises(ModuleContractError, match="must be deferred"):
        validate_module_contracts(
            [
                {
                    "module": "PhysicsBehavior",
                    "fields": {},
                    "runtimeStatus": "executable",
                    "extraction": "opaque-authored",
                    "sourceIni": "data/ini/object/fixture.ini",
                    "line": 1,
                    "tag": "",
                    "carrier": "Behavior",
                }
            ],
            label="fixture",
        )


def test_opaque_and_typed_sets_disjoint() -> None:
    from openbfme_importer.module_contracts import (
        OPAQUE_DEFERRED_MODULE_KINDS,
        TYPED_MODULE_KINDS,
    )

    assert not (OPAQUE_DEFERRED_MODULE_KINDS & TYPED_MODULE_KINDS)
    assert len(OPAQUE_DEFERRED_MODULE_KINDS) == 149
