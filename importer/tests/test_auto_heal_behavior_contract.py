"""AutoHealBehavior: closed self-heal cadence subset, everything else deferred.

Retail authors 88 AutoHealBehavior blocks in the measured effective-assets tree.
The dominant shape (StartsActive / HealingAmount / HealingDelay /
StartHealingDelay / HealOnlyIfNotInCombat) is a self-only regeneration timer the
sim can run exactly.  Radius heals, button bursts, containment heals and
upgrade-triggered activation stay row-deferred because no consumer runs them.
"""

from __future__ import annotations

import pytest

from openbfme_importer.module_contracts import (
    ModuleContractError,
    compile_all_module_contracts,
    compile_auto_heal_behaviors,
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


def _block(body: str) -> str:
    return f"""
Object FixtureObject
  Behavior = AutoHealBehavior ModuleTag_Healing
{body}
  End
End
"""


CLOSED_BODY = """    StartsActive = Yes
    HealingAmount = 30
    HealingDelay = 1000
    StartHealingDelay = 15000
    HealOnlyIfNotInCombat = Yes"""


def test_closed_self_heal_shape_is_executable_with_authored_magnitudes() -> None:
    rows = compile_auto_heal_behaviors(_lineage(_block(CLOSED_BODY)), "FixtureObject")
    assert len(rows) == 1
    row = rows[0]
    assert row["module"] == "AutoHealBehavior"
    assert row["extraction"] == "typed"
    assert row["runtimeStatus"] == "executable"
    assert row["tag"] == "ModuleTag_Healing"
    fields = row["fields"]
    assert fields["StartsActive"]["value"] is True
    assert fields["HealingAmount"]["value"] == 30
    assert fields["HealingDelay"]["milliseconds"] == 1000
    assert fields["StartHealingDelay"]["milliseconds"] == 15000
    assert fields["HealOnlyIfNotInCombat"]["value"] is True
    assert "unsupportedSemantics" not in fields


def test_trailing_slash_comment_on_starts_active_still_parses() -> None:
    body = CLOSED_BODY.replace(
        "StartsActive = Yes",
        "StartsActive = Yes\t// Active, as in no upgrade required",
    )
    rows = compile_auto_heal_behaviors(_lineage(_block(body)), "FixtureObject")
    assert rows[0]["fields"]["StartsActive"]["value"] is True
    assert rows[0]["runtimeStatus"] == "executable"


def test_missing_start_healing_delay_defaults_to_immediate_restart() -> None:
    body = "\n".join(
        line for line in CLOSED_BODY.splitlines() if "StartHealingDelay" not in line
    )
    rows = compile_auto_heal_behaviors(_lineage(_block(body)), "FixtureObject")
    assert rows[0]["runtimeStatus"] == "executable"
    assert "StartHealingDelay" not in rows[0]["fields"]


@pytest.mark.parametrize(
    ("body", "reason"),
    [
        (CLOSED_BODY.replace("StartsActive = Yes", "StartsActive = No"), "starts-inactive"),
        (CLOSED_BODY.replace("HealingAmount = 30", "HealingAmount = 0"), "non-positive-healing-amount"),
        (CLOSED_BODY.replace("HealingDelay = 1000", "HealingDelay = 0"), "non-positive-healing-delay"),
        (CLOSED_BODY + "\n    Radius = 200", "area-heal-without-runtime-oracle"),
        (CLOSED_BODY + "\n    TriggeredBy = Upgrade_ElvenGift", "upgrade-triggered-activation-without-runtime-oracle"),
        (CLOSED_BODY + "\n    ButtonTriggered = Yes", "button-triggered-burst-without-runtime-oracle"),
        (CLOSED_BODY + "\n    SingleBurst = Yes", "button-triggered-burst-without-runtime-oracle"),
        (CLOSED_BODY + "\n    AffectsContained = Yes", "contained-heal-without-runtime-oracle"),
        (CLOSED_BODY + "\n    HealOnlyOthers = Yes", "others-only-heal-without-runtime-oracle"),
        (CLOSED_BODY + "\n    KindOf = INFANTRY", "kind-filtered-heal-without-runtime-oracle"),
        (CLOSED_BODY + "\n    NonStackable = Yes", "stacking-policy-without-runtime-oracle"),
        (CLOSED_BODY + "\n    UnitHealPulseFX = FX_Pulse", "presentation-field-without-runtime-oracle"),
        (CLOSED_BODY + "\n    RespawnNearbyHordeMembers = Yes", "horde-respawn-heal-without-runtime-oracle"),
    ],
)
def test_shapes_outside_the_closed_subset_stay_deferred(body: str, reason: str) -> None:
    rows = compile_auto_heal_behaviors(_lineage(_block(body)), "FixtureObject")
    assert len(rows) == 1
    assert rows[0]["runtimeStatus"] == "deferred"
    reasons = [item["reason"] for item in rows[0]["fields"]["unsupportedSemantics"]]
    assert reason in reasons


HEARTH_HEAL_BODY = """    StartsActive = No
    HealOnlyIfNotUnderAttack = Yes
    HealOnlyIfNotInCombat = Yes
    TriggeredBy = Upgrade_MiniHordeLvl3
    HealingAmount = 30
    Radius = 100
    StartHealingDelay = 7500
    HealingDelay = 5000
    UnitHealPulseFX = FX_SpellHealUnitHealBuff
    NonStackable = Yes
    RespawnNearbyHordeMembers = Yes
    RespawnFXList = FX_BannerCarrierSpawnUnit
    RespawnMinimumDelay = 200"""


def test_hearth_heal_under_attack_gate_is_typed_not_a_hard_error() -> None:
    # Verbatim ModuleTag_HearthHeal body from GondorKnightsofDolHorde
    # (menhordes.ini:2816-2830); RotWK authors the same pair of gates on the
    # five other faction respawn hordes. The row stays deferred for the area
    # heal and the upgrade trigger, but the under-attack gate is not a reason
    # to waive the whole unit out of a faction pack.
    rows = compile_auto_heal_behaviors(_lineage(_block(HEARTH_HEAL_BODY)), "FixtureObject")
    assert len(rows) == 1
    fields = rows[0]["fields"]
    assert fields["HealOnlyIfNotUnderAttack"]["value"] is True
    assert fields["HealOnlyIfNotInCombat"]["value"] is True
    assert fields["StartHealingDelay"]["milliseconds"] == 7500
    assert rows[0]["runtimeStatus"] == "deferred"
    reasons = {item["reason"] for item in fields["unsupportedSemantics"]}
    assert "area-heal-without-runtime-oracle" in reasons
    assert not any("underattack" in str(item.get("name", "")).casefold() for item in fields["unsupportedSemantics"])


def test_under_attack_gate_alone_keeps_the_closed_cadence_executable() -> None:
    body = CLOSED_BODY.replace(
        "HealOnlyIfNotInCombat = Yes", "HealOnlyIfNotUnderAttack = Yes"
    )
    rows = compile_auto_heal_behaviors(_lineage(_block(body)), "FixtureObject")
    assert rows[0]["runtimeStatus"] == "executable"
    assert rows[0]["fields"]["HealOnlyIfNotUnderAttack"]["value"] is True
    assert "HealOnlyIfNotInCombat" not in rows[0]["fields"]


def test_named_defines_resolve_with_provenance() -> None:
    body = CLOSED_BODY.replace("HealingAmount = 30", "HealingAmount = HERO_HEAL_AMOUNT").replace(
        "StartHealingDelay = 15000", "StartHealingDelay = HERO_HEAL_DELAY"
    )
    rows = compile_auto_heal_behaviors(
        _lineage(_block(body)),
        "FixtureObject",
        numeric_defines={"hero_heal_amount": 30, "hero_heal_delay": 15000},
        numeric_define_provenance={
            "hero_heal_amount": {
                "defineId": "hero_heal_amount",
                "value": 30,
                "authoredValue": "30",
                "sourceIni": "data/ini/gamedata.ini",
                "line": 121,
            },
            "hero_heal_delay": {
                "defineId": "hero_heal_delay",
                "value": 15000,
                "authoredValue": "15000",
                "sourceIni": "data/ini/gamedata.ini",
                "line": 124,
            },
        },
    )
    row = rows[0]
    assert row["runtimeStatus"] == "executable"
    assert row["fields"]["HealingAmount"]["value"] == 30
    assert row["fields"]["HealingAmount"]["expression"] == "HERO_HEAL_AMOUNT"
    assert row["fields"]["HealingAmount"]["defineProvenance"]["line"] == 121
    assert row["fields"]["StartHealingDelay"]["milliseconds"] == 15000
    assert row["fields"]["StartHealingDelay"]["defineProvenance"]["line"] == 124


def test_unresolved_define_defers_instead_of_raising() -> None:
    body = CLOSED_BODY.replace("HealingAmount = 30", "HealingAmount = MONSTER_HEAL_AMOUNT")
    rows = compile_auto_heal_behaviors(_lineage(_block(body)), "FixtureObject")
    assert rows[0]["runtimeStatus"] == "deferred"
    reasons = [item["reason"] for item in rows[0]["fields"]["unsupportedSemantics"]]
    assert "unresolved-define-expression" in reasons
    assert rows[0]["fields"]["HealingAmount"]["expression"] == "MONSTER_HEAL_AMOUNT"
    assert "value" not in rows[0]["fields"]["HealingAmount"]


def test_unknown_field_fails_closed() -> None:
    body = CLOSED_BODY + "\n    HealingPolicyThatRetailNeverAuthored = Yes"
    with pytest.raises(ModuleContractError):
        compile_auto_heal_behaviors(_lineage(_block(body)), "FixtureObject")


def test_compile_all_module_contracts_emits_the_row() -> None:
    rows = compile_all_module_contracts(_lineage(_block(CLOSED_BODY)), "FixtureObject")
    healing = [row for row in rows if row["module"] == "AutoHealBehavior"]
    assert len(healing) == 1
    assert healing[0]["runtimeStatus"] == "executable"


def test_registry_names_the_runtime_consumer_and_runner() -> None:
    from pathlib import Path

    from openbfme_importer.module_contracts import ROW_EXECUTABLE_TYPED_MODULE_EVIDENCE

    consumer, runner = ROW_EXECUTABLE_TYPED_MODULE_EVIDENCE["AutoHealBehavior"]
    root = Path(__file__).resolve().parents[2]
    assert (root / consumer).is_file()
    assert (root / runner).is_file()
    assert "AutoHealBehavior" in (root / runner).read_text(encoding="utf-8")
