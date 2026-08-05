from __future__ import annotations

import pytest

from openbfme_importer.armor_compiler import (
    ArmorCompilerError,
    base_weapon_targets,
    compile_armor_contract,
    compile_armor_table,
    compile_weapon_upgrades,
)
from openbfme_importer.playable_unit_compiler import (
    _ancestry,
    prepare_playable_unit_compiler,
)


ARMOR_INI = b"""
Armor TestArmor
  Armor = DEFAULT        100%
  Armor = SLASH          50%
  Armor = PIERCE         25%
  Armor = SPECIALIST     200%
  Armor = CAVALRY        150%
  Armor = SIEGE          100%
  Armor = FLAME          33%
  Armor = LOGICAL_FIRE   0%
  Armor = LOGICAL_FIRE   0%
  FlankedPenalty = 50%
End

Armor TestHeavyArmor
  Armor = DEFAULT        50%
  Armor = SLASH          25%
  Armor = PIERCE         20%
  DamageScalar = 120%
End

Armor TestConflictArmor
  Armor = DEFAULT        100%
  Armor = SLASH          50%
  Armor = SLASH          75%
End

Armor TestNoDefaultArmor
  Armor = SLASH          50%
End

Armor TestUnknownTypeArmor
  Armor = DEFAULT        100%
  Armor = INVERTED       50%
End

Armor TestFrostArmor
  Armor = DEFAULT        100%
  Armor = FROST          75%
End

Armor TestBarePercentArmor
  Armor = DEFAULT        100
  Armor = SIEGE          50
End
"""

WEAPON_INI = b"""
Weapon TestSword
  AttackRange = 10
  Damage = 40
  DamageType = SLASH
End

Weapon TestSwordUpgraded
  AttackRange = 10
  DamageNugget
    Damage = TEST_UPGRADED_SWORD_DAMAGE
    DamageType = SLASH
    DamageScalar = 200% ANY +INFANTRY -HERO
    DamageScalar = 150% ANY +HERO
  End
End

Weapon TestBow
  AttackRange = 300
  ProjectileNugget
    ProjectileTemplateName = TestArrow
    WarheadTemplateName = TestBowWarhead
    ForbiddenUpgradeNames = Upgrade_TestFireArrows
  End
  ProjectileNugget
    ProjectileTemplateName = TestFireArrow
    WarheadTemplateName = TestBowFireWarhead
    RequiredUpgradeNames = Upgrade_TestFireArrows
  End
End

Weapon TestBowWarhead
  DamageNugget
    Damage = 25
    DamageType = PIERCE
  End
End

Weapon TestBowFireWarhead
  DamageNugget
    Damage = 1
    DamageType = FLAME
    DamageScalar = 50000% NONE +MINE
  End
  DamageNugget
    Damage = TEST_FIRE_BONUS_DAMAGE
    DamageType = FLAME
    DamageScalar = 25% ALL -STRUCTURE
  End
  DamageNugget
    Damage = 25
    DamageType = PIERCE
  End
End

Weapon TestPike
  AttackRange = 10
  DamageNugget
    Damage = 60
    DamageType = SPECIALIST
    ForbiddenUpgradeNames = Upgrade_TestForgedBlades
  End
  DamageNugget
    Damage = TEST_PIKE_UPGRADED_DAMAGE
    DamageType = SPECIALIST
    DamageScalar = 200% ANY +INFANTRY -HERO
    RequiredUpgradeNames = Upgrade_TestForgedBlades
  End
End

Weapon TestBrokenUpgradedWeapon
  AttackRange = 10
End
"""

GAMEDATA_INI = b"""
#define TEST_UPGRADED_SWORD_DAMAGE 90
#define TEST_FIRE_BONUS_DAMAGE 32
#define TEST_PIKE_UPGRADED_DAMAGE 115
"""


def _object_document(body: str) -> bytes:
    return ("Object TestUnit\n" + body + "End\n").encode("cp1252")


def _documents(object_payload: bytes) -> dict[str, bytes]:
    return {
        "data/ini/object/test.ini": object_payload,
        "data/ini/armor.ini": ARMOR_INI,
        "data/ini/weapon.ini": WEAPON_INI,
        "data/ini/gamedata.ini": GAMEDATA_INI,
        "data/ini/commandset.ini": b"",
        "data/ini/commandbutton.ini": b"",
        "data/ini/playertemplate.ini": b"",
    }


def _lineage(documents: dict[str, bytes], name: str = "TestUnit"):
    prepared = prepare_playable_unit_compiler(documents)
    obj = prepared.objects[name.casefold()]
    return _ancestry(prepared.objects, obj), prepared


def test_armor_table_resolves_scalars_and_provenance() -> None:
    table = compile_armor_table(_documents(b""), "TestArmor")
    assert table["setId"] == "TestArmor"
    assert table["default"]["percent"] == 100.0
    assert table["scalars"]["slash"]["percent"] == 50.0
    assert table["scalars"]["pierce"]["percent"] == 25.0
    assert table["scalars"]["specialist"]["percent"] == 200.0
    # Duplicate rows with identical values are accepted (retail FortressArmor
    # authors LOGICAL_FIRE twice); one provenance line is kept.
    assert table["scalars"]["logical_fire"]["percent"] == 0.0
    for entry in (table["default"], *table["scalars"].values()):
        assert entry["sourceIni"] == "data/ini/armor.ini"
        assert entry["line"] > 0
    assert table["damageScalar"]["percent"] == 100.0
    assert table["flankedPenalty"]["percent"] == 50.0
    assert "no flanking model" in table["flankedPenalty"]["semantic"]


def test_armor_table_resolves_authored_damage_scalar() -> None:
    table = compile_armor_table(_documents(b""), "TestHeavyArmor")
    assert table["damageScalar"]["percent"] == 120.0
    assert table["damageScalar"]["line"] > 0
    assert "flankedPenalty" not in table


def test_armor_table_accepts_retail_bare_percent_magnitudes() -> None:
    table = compile_armor_table(_documents(b""), "TestBarePercentArmor")
    assert table["default"]["percent"] == 100.0
    assert table["scalars"]["siege"]["percent"] == 50.0


def test_armor_table_fails_closed_on_unknown_set() -> None:
    with pytest.raises(ArmorCompilerError, match="no unique authored definition"):
        compile_armor_table(_documents(b""), "MissingArmor")


def test_armor_table_fails_closed_on_conflicting_rows() -> None:
    with pytest.raises(ArmorCompilerError, match="conflicting SLASH rows"):
        compile_armor_table(_documents(b""), "TestConflictArmor")


def test_armor_table_fails_closed_on_missing_default() -> None:
    with pytest.raises(ArmorCompilerError, match="no DEFAULT row"):
        compile_armor_table(_documents(b""), "TestNoDefaultArmor")


def test_armor_table_fails_closed_on_unknown_damage_type() -> None:
    with pytest.raises(ArmorCompilerError, match="unknown damage type"):
        compile_armor_table(_documents(b""), "TestUnknownTypeArmor")


def test_frost_accepted_under_rotwk() -> None:
    # RotWK 2.01 extends the Damage vocabulary with FROST (measured: 162
    # armor rows in _patch201ini.big!data/ini/armor.ini, first at line 59).
    table = compile_armor_table(_documents(b""), "TestFrostArmor", game="rotwk")
    assert table["scalars"]["frost"]["percent"] == 75.0
    assert table["scalars"]["frost"]["damageType"] == "FROST"


def test_frost_rejected_under_bfme2() -> None:
    # BFME2 1.06 Damage.h has no FROST; the base-game vocabulary must stay
    # byte-identical, so a FROST row fails closed under the default game.
    with pytest.raises(ArmorCompilerError, match="unknown damage type 'FROST'"):
        compile_armor_table(_documents(b""), "TestFrostArmor")
    with pytest.raises(ArmorCompilerError, match="unknown damage type 'FROST'"):
        compile_armor_table(_documents(b""), "TestFrostArmor", game="bfme2")


def test_unknown_damage_type_rejected_under_both_games() -> None:
    for game in ("bfme2", "rotwk"):
        with pytest.raises(ArmorCompilerError, match="unknown damage type"):
            compile_armor_table(_documents(b""), "TestUnknownTypeArmor", game=game)


def test_unknown_game_fails_closed() -> None:
    with pytest.raises(ArmorCompilerError, match="does not support game"):
        compile_armor_table(_documents(b""), "TestArmor", game="bfme1")


def test_bfme2_vocabulary_accepted_under_rotwk() -> None:
    # The RotWK vocabulary is a strict superset: every BFME2 table still
    # resolves identically under game=rotwk.
    base = compile_armor_table(_documents(b""), "TestArmor")
    rotwk = compile_armor_table(_documents(b""), "TestArmor", game="rotwk")
    assert base == rotwk


def test_armor_contract_without_armor_set_records_engine_passthrough() -> None:
    lineage, _ = _lineage(_documents(_object_document("  KindOf = INFANTRY\n")))
    contract = compile_armor_contract(_documents(b""), lineage)
    assert contract["setId"] is None
    assert "SAGE engine" in contract["semantic"]
    assert contract["upgrades"] == []


def test_armor_contract_resolves_base_and_upgrade_sets() -> None:
    payload = _object_document(
        "  KindOf = INFANTRY\n"
        "  ArmorSet\n"
        "    Conditions = None\n"
        "    Armor = TestArmor\n"
        "  End\n"
        "  ArmorSet\n"
        "    Conditions = PLAYER_UPGRADE\n"
        "    Armor = TestHeavyArmor\n"
        "  End\n"
        "  Behavior = ArmorUpgrade ArmorUpgradeModuleTag\n"
        "    TriggeredBy = Upgrade_TestHeavyArmor\n"
        "    ArmorSetFlag = PLAYER_UPGRADE\n"
        "  End\n"
    )
    documents = _documents(payload)
    lineage, _ = _lineage(documents)
    contract = compile_armor_contract(documents, lineage)
    assert contract["setId"] == "TestArmor"
    assert contract["table"]["scalars"]["slash"]["percent"] == 50.0
    assert contract["sourceIni"] == "data/ini/object/test.ini"
    assert contract["line"] > 0
    assert len(contract["upgrades"]) == 1
    upgrade = contract["upgrades"][0]
    assert upgrade["upgradeId"] == "Upgrade_TestHeavyArmor"
    assert upgrade["armorSetFlag"] == "PLAYER_UPGRADE"
    assert upgrade["setId"] == "TestHeavyArmor"
    assert upgrade["table"]["damageScalar"]["percent"] == 120.0
    assert upgrade["behavior"]["kind"] == "ArmorUpgrade"
    assert contract["excludedUpgradeSets"] == []


def test_armor_contract_defaults_armor_set_flag() -> None:
    payload = _object_document(
        "  KindOf = INFANTRY\n"
        "  ArmorSet\n"
        "    Conditions = None\n"
        "    Armor = TestArmor\n"
        "  End\n"
        "  ArmorSet\n"
        "    Conditions = PLAYER_UPGRADE\n"
        "    Armor = TestHeavyArmor\n"
        "  End\n"
        "  Behavior = ArmorUpgrade ArmorUpgradeModuleTag\n"
        "    TriggeredBy = Upgrade_TestHeavyArmor\n"
        "  End\n"
    )
    documents = _documents(payload)
    lineage, _ = _lineage(documents)
    contract = compile_armor_contract(documents, lineage)
    upgrade = contract["upgrades"][0]
    assert upgrade["armorSetFlag"] == "PLAYER_UPGRADE"
    assert "default is PLAYER_UPGRADE" in upgrade["armorSetFlagSemantic"]


def test_armor_contract_fails_closed_on_missing_referenced_set() -> None:
    payload = _object_document(
        "  KindOf = INFANTRY\n"
        "  ArmorSet\n"
        "    Conditions = None\n"
        "    Armor = MissingArmor\n"
        "  End\n"
    )
    documents = _documents(payload)
    lineage, _ = _lineage(documents)
    with pytest.raises(ArmorCompilerError, match="MissingArmor"):
        compile_armor_contract(documents, lineage)


def test_armor_contract_records_a_dangling_upgrade_as_an_engine_no_op() -> None:
    """RE-PINNED 2026-08-04. A dangling ArmorUpgrade must NOT fail the object.

    This test used to assert the opposite (fail closed). That was over-strict,
    and it cost two real units: pure RotWK 2.01 authors, on the SAME object,

        object/goodfaction/units/elven/elvenrivendelllancerbanner.ini:421-424
            ArmorSet
                Conditions      = None
                Armor           = NoArmor
        object/goodfaction/units/elven/elvenrivendelllancerbanner.ini:690-693
            Behavior = ArmorUpgrade ArmorUpgradeModuleTag
                TriggeredBy   = Upgrade_ElvenHeavyArmor
                ArmorSetFlag  = PLAYER_UPGRADE

    - an upgrade whose flag no ArmorSet declares. Failing closed made
    ElvenRivendellArcherBanner and ElvenRivendellLancerBanner converter gaps,
    which left the elves pack shipping a `bannerCarrier` contract naming a unit
    it did not contain, which aborted the whole faction at runtime.

    The engine treats it as a NO-OP, not a contradiction. `ArmorUpgrade` only
    sets a bit (`ActiveBody.SetArmorSetFlag`), and armor selection then runs
    `BitArrayMatchFinder.FindBest` over the declared sets
    (`ActiveBody.ValidateArmorAndDamageFX`): with no set declaring the flag the
    base `Conditions = None` set keeps winning, so the armor never changes.
    (Semantic confirmed against the OpenSAGE shared core; no code copied.)

    So it is RECORDED, not raised - loudly enough to stay visible.
    """

    payload = _object_document(
        "  KindOf = INFANTRY\n"
        "  ArmorSet\n"
        "    Conditions = None\n"
        "    Armor = TestArmor\n"
        "  End\n"
        "  Behavior = ArmorUpgrade ArmorUpgradeModuleTag\n"
        "    TriggeredBy = Upgrade_TestHeavyArmor\n"
        "  End\n"
    )
    documents = _documents(payload)
    lineage, _ = _lineage(documents)

    contract = compile_armor_contract(documents, lineage)

    assert contract["setId"] == "TestArmor"
    assert [row["upgradeId"] for row in contract["upgrades"]] == []
    recorded = contract["danglingUpgrades"]
    assert [row["upgradeId"] for row in recorded] == ["Upgrade_TestHeavyArmor"]
    assert recorded[0]["armorSetFlag"] == "PLAYER_UPGRADE"
    assert "no ArmorSet" in recorded[0]["reason"]


def test_armor_contract_records_dangling_upgrade_with_flag_authored_explicitly() -> (
    None
):
    """ADVERSARIAL, added 2026-08-04 (round 13). The RETAIL shape, exactly.

    The test above omits `ArmorSetFlag` entirely, so it drives the DEFAULT
    branch in `armor_compiler._armor_contract` (`flag_row is None` ->
    "PLAYER_UPGRADE" plus an `armorSetFlagSemantic` note). But the real object
    it cites as its justification does NOT omit the row - pure RotWK 2.01
    authors it explicitly:

        object/goodfaction/units/elven/elvenrivendelllancerbanner.ini:690-693
            Behavior = ArmorUpgrade ArmorUpgradeModuleTag
                TriggeredBy   = Upgrade_ElvenHeavyArmor
                ArmorSetFlag  = PLAYER_UPGRADE

    So the retail shape took a DIFFERENT code path than the one under test, and
    nothing pinned it. This case closes that hole: an explicitly authored flag
    that no ArmorSet declares must still be RECORDED as an engine no-op, must
    still leave `upgrades` empty, and must NOT carry the defaulted-flag semantic
    note (nothing was defaulted).
    """

    payload = _object_document(
        "  KindOf = INFANTRY\n"
        "  ArmorSet\n"
        "    Conditions = None\n"
        "    Armor = TestArmor\n"
        "  End\n"
        "  Behavior = ArmorUpgrade ArmorUpgradeModuleTag\n"
        "    TriggeredBy = Upgrade_TestHeavyArmor\n"
        "    ArmorSetFlag = PLAYER_UPGRADE\n"
        "  End\n"
    )
    documents = _documents(payload)
    lineage, _ = _lineage(documents)

    contract = compile_armor_contract(documents, lineage)

    assert contract["setId"] == "TestArmor"
    assert contract["upgrades"] == []
    recorded = contract["danglingUpgrades"]
    assert [row["upgradeId"] for row in recorded] == ["Upgrade_TestHeavyArmor"]
    assert recorded[0]["armorSetFlag"] == "PLAYER_UPGRADE"
    assert "no ArmorSet" in recorded[0]["reason"]
    # The flag was authored, so the "we defaulted it" note must be absent. A
    # dangling row never carries it at all, which is why this is asserted on the
    # matched case below as well.
    assert "armorSetFlagSemantic" not in recorded[0]


def test_armor_contract_treats_an_empty_armor_set_flag_as_the_default() -> None:
    """ADVERSARIAL, added 2026-08-04 (round 13). Malformed input records, never raises.

    `ArmorSetFlag =` with no token on the right-hand side is malformed INI. The
    compiler's flag resolution
    (`armor_compiler._armor_contract`, the `flag = ... if flag_row is not None
    and _tokens(flag_row.value) else "PLAYER_UPGRADE"` expression) folds it into
    the SAME "PLAYER_UPGRADE" default as an absent row - deliberately, not by
    accident:

    * Raising here is the failure mode this module already paid for once. The
      dangling-upgrade test above documents how failing closed on an inert
      authored row turned two Rivendell banner units into converter gaps and
      aborted the entire elves faction at runtime. An empty token is strictly
      less harmful than a dangling one: SAGE's own tokeniser yields no flag, so
      the engine sets no bit and the base set keeps winning - the identical
      no-op.
    * The result is therefore RECORDED and stays visible, exactly like the
      dangling case, rather than being dropped or raised.

    One asymmetry is pinned here on purpose so it cannot drift silently:
    `armorSetFlagSemantic` is attached only when the row is ABSENT
    (`flag_row is None`), so an empty-but-present row resolves to
    "PLAYER_UPGRADE" WITHOUT that note. If the compiler ever starts annotating
    malformed rows too, this assertion is the thing that says so.
    """

    matched_payload = _object_document(
        "  KindOf = INFANTRY\n"
        "  ArmorSet\n"
        "    Conditions = None\n"
        "    Armor = TestArmor\n"
        "  End\n"
        "  ArmorSet\n"
        "    Conditions = PLAYER_UPGRADE\n"
        "    Armor = TestHeavyArmor\n"
        "  End\n"
        "  Behavior = ArmorUpgrade ArmorUpgradeModuleTag\n"
        "    TriggeredBy = Upgrade_TestHeavyArmor\n"
        "    ArmorSetFlag =\n"
        "  End\n"
    )
    documents = _documents(matched_payload)
    lineage, _ = _lineage(documents)

    contract = compile_armor_contract(documents, lineage)

    assert contract["danglingUpgrades"] == []
    upgrade = contract["upgrades"][0]
    assert upgrade["armorSetFlag"] == "PLAYER_UPGRADE"
    assert upgrade["setId"] == "TestHeavyArmor"
    # Present-but-empty is NOT the same provenance as absent: no semantic note.
    assert "armorSetFlagSemantic" not in upgrade

    # And with no set declaring the defaulted flag it degrades to the recorded
    # no-op rather than raising.
    dangling_payload = _object_document(
        "  KindOf = INFANTRY\n"
        "  ArmorSet\n"
        "    Conditions = None\n"
        "    Armor = TestArmor\n"
        "  End\n"
        "  Behavior = ArmorUpgrade ArmorUpgradeModuleTag\n"
        "    TriggeredBy = Upgrade_TestHeavyArmor\n"
        "    ArmorSetFlag =\n"
        "  End\n"
    )
    documents = _documents(dangling_payload)
    lineage, _ = _lineage(documents)

    contract = compile_armor_contract(documents, lineage)

    assert contract["upgrades"] == []
    recorded = contract["danglingUpgrades"]
    assert [row["upgradeId"] for row in recorded] == ["Upgrade_TestHeavyArmor"]
    assert recorded[0]["armorSetFlag"] == "PLAYER_UPGRADE"


def test_armor_contract_records_unmatched_upgrade_sets() -> None:
    payload = _object_document(
        "  KindOf = INFANTRY\n"
        "  ArmorSet\n"
        "    Conditions = None\n"
        "    Armor = TestArmor\n"
        "  End\n"
        "  ArmorSet\n"
        "    Conditions = PLAYER_UPGRADE\n"
        "    Armor = TestHeavyArmor\n"
        "  End\n"
    )
    documents = _documents(payload)
    lineage, _ = _lineage(documents)
    contract = compile_armor_contract(documents, lineage)
    assert contract["upgrades"] == []
    assert len(contract["excludedUpgradeSets"]) == 1
    assert contract["excludedUpgradeSets"][0]["setId"] == "TestHeavyArmor"


def test_armor_contract_keeps_mount_state_sets_out_of_the_upgrade_gate() -> None:
    # Oracle: RotWK layered
    # data/ini/object/goodfaction/units/men/theoden.ini:687-696 authors a base
    # ArmorSet (Conditions = None -> HeroArmor) and a mount-state ArmorSet
    # (Conditions = MOUNTED -> HeroArmorMounted) with NO ArmorUpgrade behavior.
    # MOUNTED is an engine mount state, not an upgrade flag, so the mounted set
    # must be carried as a conditional set -- not dropped as "upgrade-gated
    # ArmorSet has no matching ArmorUpgrade behavior", which left mounted
    # Theoden wearing foot armor.
    payload = _object_document(
        "  KindOf = INFANTRY\n"
        "  ArmorSet\n"
        "    Conditions = None\n"
        "    Armor = TestArmor\n"
        "  End\n"
        "  ArmorSet\n"
        "    Conditions = MOUNTED\n"
        "    Armor = TestHeavyArmor\n"
        "  End\n"
    )
    documents = _documents(payload)
    lineage, _ = _lineage(documents)
    contract = compile_armor_contract(documents, lineage)
    assert contract["setId"] == "TestArmor"
    assert contract["upgrades"] == []
    assert contract["excludedUpgradeSets"] == []
    assert len(contract["conditionalSets"]) == 1
    conditional = contract["conditionalSets"][0]
    assert conditional["setId"] == "TestHeavyArmor"
    assert conditional["conditions"] == ["mounted"]
    assert conditional["table"]["damageScalar"]["percent"] == 120.0
    assert conditional["sourceIni"] == "data/ini/object/test.ini"
    assert conditional["line"] > 0


def test_armor_contract_mount_state_set_still_matches_its_own_upgrade() -> None:
    # Oracle: RotWK layered
    # data/ini/object/goodfaction/units/men/rohanbanner.ini:686-700 authors
    # ArmorSets conditioned on MOUNTED *and* PLAYER_UPGRADE together. Those are
    # genuinely upgrade-gated, so the ArmorUpgrade behavior must still bind
    # them and they must not also appear as unconditioned conditional sets.
    payload = _object_document(
        "  KindOf = INFANTRY\n"
        "  ArmorSet\n"
        "    Conditions = None\n"
        "    Armor = TestArmor\n"
        "  End\n"
        "  ArmorSet\n"
        "    Conditions = MOUNTED PLAYER_UPGRADE\n"
        "    Armor = TestHeavyArmor\n"
        "  End\n"
        "  Behavior = ArmorUpgrade ArmorUpgradeModuleTag\n"
        "    TriggeredBy = Upgrade_TestHeavyArmor\n"
        "    ArmorSetFlag = PLAYER_UPGRADE\n"
        "  End\n"
    )
    documents = _documents(payload)
    lineage, _ = _lineage(documents)
    contract = compile_armor_contract(documents, lineage)
    assert len(contract["upgrades"]) == 1
    assert contract["upgrades"][0]["setId"] == "TestHeavyArmor"
    assert contract["excludedUpgradeSets"] == []
    assert contract["conditionalSets"] == []


def test_armor_contract_mixed_mount_upgrade_set_is_conditional_without_armor_upgrade() -> None:
    # Oracle: RotWK layered
    # data/ini/object/goodfaction/units/men/rohanbanner.ini authors a base set
    # (:665 Conditions = None), a foot upgrade set (:671-675 PLAYER_UPGRADE ->
    # ArcherEliteHeavyArmor), a mount-state set (:682-686 MOUNTED) and three
    # MIXED sets (:687 MOUNTED PLAYER_UPGRADE, :692 MOUNTED PLAYER_UPGRADE_2,
    # :697 MOUNTED PLAYER_UPGRADE_2 PLAYER_UPGRADE) -- and NO ArmorUpgrade
    # behavior anywhere in the file. So a mixed MOUNTED+PLAYER_UPGRADE set does
    # NOT "stay upgrade-gated": with nothing to bind it, it takes the state
    # branch and becomes a conditional set that keeps its upgrade tokens in
    # `conditions` while `stateConditions` names only the state. Only the plain
    # PLAYER_UPGRADE foot set is excluded. This pins the branch the
    # _STATE_CONDITIONS comment used to misdescribe.
    payload = _object_document(
        "  KindOf = INFANTRY\n"
        "  ArmorSet\n"
        "    Conditions = None\n"
        "    Armor = TestArmor\n"
        "  End\n"
        "  ArmorSet\n"
        "    Conditions = PLAYER_UPGRADE\n"
        "    Armor = TestHeavyArmor\n"
        "  End\n"
        "  ArmorSet\n"
        "    Conditions = MOUNTED\n"
        "    Armor = TestFrostArmor\n"
        "  End\n"
        "  ArmorSet\n"
        "    Conditions = MOUNTED PLAYER_UPGRADE\n"
        "    Armor = TestBarePercentArmor\n"
        "  End\n"
    )
    documents = _documents(payload)
    lineage, _ = _lineage(documents)
    contract = compile_armor_contract(documents, lineage, game="rotwk")
    assert contract["setId"] == "TestArmor"
    # No ArmorUpgrade behavior exists, so nothing is upgrade-gated at all.
    assert contract["upgrades"] == []
    assert [row["setId"] for row in contract["excludedUpgradeSets"]] == [
        "TestHeavyArmor"
    ]
    conditional = {row["setId"]: row for row in contract["conditionalSets"]}
    assert set(conditional) == {"TestFrostArmor", "TestBarePercentArmor"}
    mixed = conditional["TestBarePercentArmor"]
    assert mixed["conditions"] == ["mounted", "player_upgrade"]
    assert mixed["stateConditions"] == ["mounted"]


def _weapon_object(weapon_block: str, behavior: str) -> bytes:
    return _object_document(
        "  KindOf = INFANTRY\n"
        "  ArmorSet\n"
        "    Conditions = None\n"
        "    Armor = TestArmor\n"
        "  End\n"
        f"{weapon_block}"
        f"{behavior}"
    )


def test_weapon_upgrade_compiles_weapon_swap() -> None:
    payload = _weapon_object(
        "  WeaponSet\n"
        "    Conditions = None\n"
        "    Weapon = PRIMARY TestSword\n"
        "  End\n"
        "  WeaponSet\n"
        "    Conditions = PLAYER_UPGRADE\n"
        "    Weapon = PRIMARY TestSwordUpgraded\n"
        "  End\n",
        "  Behavior = WeaponSetUpgrade WeaponSetUpgradeModuleTag\n"
        "    TriggeredBy = Upgrade_TestForgedBlades\n"
        "  End\n",
    )
    documents = _documents(payload)
    lineage, prepared = _lineage(documents)
    upgrades = compile_weapon_upgrades(
        documents,
        [lineage],
        base_weapon_targets(lineage),
        prepared.numeric_defines,
    )
    assert len(upgrades) == 1
    upgrade = upgrades[0]
    assert upgrade["kind"] == "weapon-swap"
    assert upgrade["upgradeId"] == "Upgrade_TestForgedBlades"
    assert upgrade["weaponId"] == "TestSwordUpgraded"
    assert upgrade["damage"]["value"] == 90
    assert upgrade["damage"]["expression"] == "TEST_UPGRADED_SWORD_DAMAGE"
    assert upgrade["damage"]["constantSourceIni"] == "data/ini/gamedata.ini"
    assert upgrade["damageType"] == "SLASH"
    assert [(row["percent"], row["filter"]) for row in upgrade["damageScalars"]] == [
        (200.0, "ANY +INFANTRY -HERO"),
        (150.0, "ANY +HERO"),
    ]
    assert all(row["line"] > 0 for row in upgrade["damageScalars"])


def test_weapon_upgrade_compiles_warhead_upgrade() -> None:
    payload = _weapon_object(
        "  WeaponSet\n    Conditions = None\n    Weapon = PRIMARY TestBow\n  End\n",
        "  Behavior = WeaponSetUpgrade ModuleTag_FireArrows\n"
        "    TriggeredBy = Upgrade_TestFireArrows\n"
        "  End\n",
    )
    documents = _documents(payload)
    lineage, prepared = _lineage(documents)
    upgrades = compile_weapon_upgrades(
        documents,
        [lineage],
        base_weapon_targets(lineage),
        prepared.numeric_defines,
    )
    assert len(upgrades) == 1
    upgrade = upgrades[0]
    assert upgrade["kind"] == "warhead-upgrade"
    assert upgrade["warheadId"] == "TestBowFireWarhead"
    assert upgrade["replacesWarheadId"] == "TestBowWarhead"
    nuggets = [
        (row["damage"]["value"], row.get("damageType")) for row in upgrade["nuggets"]
    ]
    assert nuggets == [(1, "FLAME"), (32, "FLAME"), (25, "PIERCE")]
    bonus = upgrade["nuggets"][1]
    assert bonus["damage"]["expression"] == "TEST_FIRE_BONUS_DAMAGE"
    assert [(row["percent"], row["filter"]) for row in bonus["damageScalars"]] == [
        (25.0, "ALL -STRUCTURE")
    ]


def test_weapon_upgrade_compiles_nugget_upgrade() -> None:
    payload = _weapon_object(
        "  WeaponSet\n    Conditions = None\n    Weapon = PRIMARY TestPike\n  End\n",
        "  Behavior = WeaponSetUpgrade WeaponSetUpgradeModuleTag\n"
        "    TriggeredBy = Upgrade_TestForgedBlades\n"
        "  End\n",
    )
    documents = _documents(payload)
    lineage, prepared = _lineage(documents)
    upgrades = compile_weapon_upgrades(
        documents,
        [lineage],
        base_weapon_targets(lineage),
        prepared.numeric_defines,
    )
    assert len(upgrades) == 1
    upgrade = upgrades[0]
    assert upgrade["kind"] == "nugget-upgrade"
    assert upgrade["weaponId"] == "TestPike"
    assert len(upgrade["nuggets"]) == 1
    nugget = upgrade["nuggets"][0]
    assert nugget["damage"]["value"] == 115
    assert nugget["damageType"] == "SPECIALIST"
    assert [(row["percent"], row["filter"]) for row in nugget["damageScalars"]] == [
        (200.0, "ANY +INFANTRY -HERO")
    ]


def test_weapon_upgrade_deduplicates_repeated_behaviors() -> None:
    payload = _weapon_object(
        "  WeaponSet\n    Conditions = None\n    Weapon = PRIMARY TestPike\n  End\n",
        "  Behavior = WeaponSetUpgrade ModuleTag_One\n"
        "    TriggeredBy = Upgrade_TestForgedBlades\n"
        "  End\n"
        "  Behavior = WeaponSetUpgrade ModuleTag_Two\n"
        "    TriggeredBy = Upgrade_TestForgedBlades\n"
        "  End\n",
    )
    documents = _documents(payload)
    lineage, prepared = _lineage(documents)
    upgrades = compile_weapon_upgrades(
        documents,
        [lineage],
        base_weapon_targets(lineage),
        prepared.numeric_defines,
    )
    assert len(upgrades) == 1
    assert len(upgrades[0]["additionalBehaviors"]) == 1


def test_weapon_upgrade_fails_closed_on_unresolvable_swap() -> None:
    payload = _weapon_object(
        "  WeaponSet\n"
        "    Conditions = None\n"
        "    Weapon = PRIMARY TestSword\n"
        "  End\n"
        "  WeaponSet\n"
        "    Conditions = PLAYER_UPGRADE\n"
        "    Weapon = PRIMARY TestBrokenUpgradedWeapon\n"
        "  End\n",
        "  Behavior = WeaponSetUpgrade WeaponSetUpgradeModuleTag\n"
        "    TriggeredBy = Upgrade_TestForgedBlades\n"
        "  End\n",
    )
    documents = _documents(payload)
    lineage, prepared = _lineage(documents)
    with pytest.raises(ArmorCompilerError, match="no resolvable authored damage"):
        compile_weapon_upgrades(
            documents,
            [lineage],
            base_weapon_targets(lineage),
            prepared.numeric_defines,
        )


def test_weapon_upgrade_fails_closed_without_any_effect() -> None:
    payload = _weapon_object(
        "  WeaponSet\n    Conditions = None\n    Weapon = PRIMARY TestSword\n  End\n",
        "  Behavior = WeaponSetUpgrade WeaponSetUpgradeModuleTag\n"
        "    TriggeredBy = Upgrade_TestFireArrows\n"
        "  End\n",
    )
    documents = _documents(payload)
    lineage, prepared = _lineage(documents)
    with pytest.raises(ArmorCompilerError, match="no unique upgrade-gated"):
        compile_weapon_upgrades(
            documents,
            [lineage],
            base_weapon_targets(lineage),
            prepared.numeric_defines,
        )


def test_weapon_upgrade_emits_legality_only_with_duplicate_player_weapon() -> None:
    payload = _weapon_object(
        "  WeaponSet\n"
        "    Conditions = None\n"
        "    Weapon = PRIMARY TestSword\n"
        "  End\n"
        "  WeaponSet\n"
        "    Conditions = PLAYER_UPGRADE\n"
        "    Weapon = PRIMARY TestSword\n"
        "  End\n",
        "  Behavior = WeaponSetUpgrade WeaponSetUpgradeModuleTag\n"
        "    TriggeredBy = Upgrade_TestProductionLegality\n"
        "  End\n",
    )
    documents = _documents(payload)
    lineage, prepared = _lineage(documents)
    upgrades = compile_weapon_upgrades(
        documents,
        [lineage],
        base_weapon_targets(lineage),
        prepared.numeric_defines,
    )

    assert len(upgrades) == 1
    assert upgrades[0]["kind"] == "production-legality"
    assert upgrades[0]["upgradeId"] == "Upgrade_TestProductionLegality"


def test_noldor_default_silverthorn_shell_is_production_legality_only() -> None:
    payload = _weapon_object(
        "  WeaponSet\n"
        "    Conditions = None\n"
        "    Weapon = PRIMARY NoldorWarriorSilverthornBow\n"
        "    Weapon = TERTIARY NoldorWarriorSilverthornBowBombard\n"
        "  End\n"
        "  WeaponSet\n"
        "    Conditions = WEAPONSET_TOGGLE_1\n"
        "    Weapon = PRIMARY NoldorWarriorSword\n"
        "  End\n",
        "  Behavior = WeaponSetUpgrade ModuleTag_Silverthorn\n"
        "    TriggeredBy = Upgrade_ElvenSilverthornArrows\n"
        "  End\n",
    )
    documents = _documents(payload)
    lineage, prepared = _lineage(documents)
    upgrades = compile_weapon_upgrades(
        documents,
        [lineage],
        base_weapon_targets(lineage),
        prepared.numeric_defines,
    )

    assert len(upgrades) == 1
    assert upgrades[0]["kind"] == "production-legality"
    assert upgrades[0]["upgradeId"] == "Upgrade_ElvenSilverthornArrows"


def test_named_silverthorn_upgrade_without_default_silverthorn_still_fails() -> None:
    payload = _weapon_object(
        "  WeaponSet\n    Conditions = None\n    Weapon = PRIMARY TestBow\n  End\n",
        "  Behavior = WeaponSetUpgrade ModuleTag_Silverthorn\n"
        "    TriggeredBy = Upgrade_ElvenSilverthornArrows\n"
        "  End\n",
    )
    documents = _documents(payload)
    lineage, prepared = _lineage(documents)
    with pytest.raises(ArmorCompilerError, match="no unique upgrade-gated"):
        compile_weapon_upgrades(
            documents,
            [lineage],
            base_weapon_targets(lineage),
            prepared.numeric_defines,
        )


def test_horde_coordination_weapon_swap_preserves_range_without_damage() -> None:
    payload = _weapon_object(
        "  WeaponSet\n"
        "    Conditions = None\n"
        "    Weapon = PRIMARY TestHordeRangeFinder\n"
        "  End\n"
        "  WeaponSet\n"
        "    Conditions = PLAYER_UPGRADE\n"
        "    Weapon = PRIMARY TestHordeRangeFinderUpgraded\n"
        "  End\n",
        "  Behavior = WeaponSetUpgrade WeaponSetUpgradeModuleTag\n"
        "    TriggeredBy = Upgrade_TestRange\n"
        "  End\n",
    )
    documents = _documents(payload)
    documents["data/ini/weapon.ini"] += b"""
Weapon TestHordeRangeFinder
  AttackRange = 300
  HordeAttackNugget
  End
End
Weapon TestHordeRangeFinderUpgraded
  AttackRange = 400
  HordeAttackNugget
  End
End
"""
    lineage, prepared = _lineage(documents)
    upgrades = compile_weapon_upgrades(
        documents,
        [lineage],
        base_weapon_targets(lineage),
        prepared.numeric_defines,
    )

    assert len(upgrades) == 1
    assert upgrades[0]["kind"] == "production-legality"
    assert upgrades[0]["weaponId"] == "TestHordeRangeFinderUpgraded"
    assert upgrades[0]["coordinationAttackRange"]["value"] == 400
    assert "member weapons remain authoritative" in upgrades[0]["semantic"]


# --- Descriptor integration -------------------------------------------------


def _integration_documents() -> dict[str, bytes]:
    from importer.tests.test_playable_unit_compiler import _documents

    documents = _documents()
    objects_path = "data/ini/object/units/test_units.ini"
    objects = documents[objects_path].decode("utf-8")
    objects = objects.replace(
        "Object InfantryMember\n  KindOf = PRELOAD SELECTABLE INFANTRY\n",
        "Object InfantryMember\n"
        "  KindOf = PRELOAD SELECTABLE INFANTRY\n"
        "  ArmorSet\n"
        "    Conditions = None\n"
        "    Armor = TestArmor\n"
        "  End\n"
        "  ArmorSet\n"
        "    Conditions = PLAYER_UPGRADE\n"
        "    Armor = TestHeavyArmor\n"
        "  End\n"
        "  Behavior = ArmorUpgrade ArmorUpgradeModuleTag\n"
        "    TriggeredBy = Upgrade_TestHeavyArmor\n"
        "  End\n"
        "  WeaponSet\n"
        "    Conditions = None\n"
        "    Weapon = PRIMARY TestSword\n"
        "  End\n"
        "  WeaponSet\n"
        "    Conditions = PLAYER_UPGRADE\n"
        "    Weapon = PRIMARY TestSwordUpgraded\n"
        "  End\n"
        "  Behavior = WeaponSetUpgrade WeaponSetUpgradeModuleTag\n"
        "    TriggeredBy = Upgrade_TestForgedBlades\n"
        "  End\n",
        1,
    )
    documents[objects_path] = objects.encode("utf-8")
    documents["data/ini/armor.ini"] = ARMOR_INI
    documents["data/ini/weapon.ini"] = WEAPON_INI
    documents["data/ini/gamedata.ini"] = (
        documents["data/ini/gamedata.ini"] + GAMEDATA_INI
    )
    return documents


def test_unit_descriptor_carries_compiled_armor_and_forge_upgrades() -> None:
    from openbfme_importer.playable_unit_compiler import (
        compile_playable_unit_descriptor,
        validate_playable_unit_descriptor,
    )

    descriptor = compile_playable_unit_descriptor(
        "InfantryHorde", _integration_documents()
    )
    validate_playable_unit_descriptor(descriptor)
    simulation = descriptor["gameplay"]["simulation"]
    # The shared fixture leaves unrelated movement/display gaps unresolved;
    # the armor section asserts only its own contract.
    assert "armor" not in simulation["missing"]
    armor = simulation["resolved"]["armor"]
    assert armor["setId"] == "TestArmor"
    assert armor["table"]["scalars"]["pierce"]["percent"] == 25.0
    assert armor["table"]["sourceIni"] == "data/ini/armor.ini"
    assert armor["upgrades"][0]["upgradeId"] == "Upgrade_TestHeavyArmor"
    assert armor["upgrades"][0]["table"]["damageScalar"]["percent"] == 120.0
    upgrades = simulation["resolved"]["combat"]["upgrades"]
    assert upgrades[0]["kind"] == "weapon-swap"
    assert upgrades[0]["upgradeId"] == "Upgrade_TestForgedBlades"
    assert upgrades[0]["damage"]["value"] == 90
    provenance_paths = {row["virtualPath"] for row in descriptor["sourceDocuments"]}
    assert "data/ini/armor.ini" in provenance_paths


def test_structure_descriptor_carries_compiled_armor() -> None:
    from importer.tests.test_playable_structure_compiler import (
        _structure_documents,
    )
    from openbfme_importer.playable_structure_compiler import (
        compile_playable_structure_descriptor,
        validate_playable_structure_descriptor,
    )

    documents = _structure_documents()
    objects_path = "data/ini/object/units/test_units.ini"
    objects = documents[objects_path].decode("utf-8")
    objects = objects.replace(
        "Object TestKeep\n  CommandSet = TestKeepCommandSet\n",
        "Object TestKeep\n"
        "  CommandSet = TestKeepCommandSet\n"
        "  ArmorSet\n"
        "    Conditions = None\n"
        "    Armor = TestArmor\n"
        "  End\n",
        1,
    )
    documents[objects_path] = objects.encode("utf-8")
    documents["data/ini/armor.ini"] = ARMOR_INI

    descriptor = compile_playable_structure_descriptor("TestKeep", documents)
    validate_playable_structure_descriptor(descriptor)
    armor = descriptor["gameplay"]["armor"]
    assert armor["setId"] == "TestArmor"
    assert armor["table"]["scalars"]["slash"]["percent"] == 50.0
    provenance_paths = {row["virtualPath"] for row in descriptor["sourceDocuments"]}
    assert "data/ini/armor.ini" in provenance_paths


def test_unit_descriptor_fails_closed_on_unresolvable_armor_set() -> None:
    from openbfme_importer.playable_unit_compiler import (
        PlayableUnitCompilerError,
        compile_playable_unit_descriptor,
    )

    documents = _integration_documents()
    objects_path = "data/ini/object/units/test_units.ini"
    objects = documents[objects_path].decode("utf-8")
    documents[objects_path] = objects.replace(
        "Armor = TestArmor", "Armor = MissingArmor", 1
    ).encode("utf-8")

    with pytest.raises(PlayableUnitCompilerError, match="MissingArmor"):
        compile_playable_unit_descriptor("InfantryHorde", documents)


def test_status_bits_dummy_upgrade_resolves_gated_nugget_effect() -> None:
    # RohanRohirrim forged blades: the member authors only a StatusBitsUpgrade
    # "dummy" while the damage rides an upgrade-gated DamageNugget on the
    # unchanged base weapon.
    payload = _weapon_object(
        "  WeaponSet\n    Conditions = None\n    Weapon = PRIMARY TestPike\n  End\n",
        "  Behavior = StatusBitsUpgrade ModuleTag_ForgedBlades\n"
        "    TriggeredBy = Upgrade_TestForgedBlades\n"
        "  End\n",
    )
    documents = _documents(payload)
    lineage, prepared = _lineage(documents)
    upgrades = compile_weapon_upgrades(
        documents,
        [lineage],
        base_weapon_targets(lineage),
        prepared.numeric_defines,
    )
    assert len(upgrades) == 1
    upgrade = upgrades[0]
    assert upgrade["upgradeId"] == "Upgrade_TestForgedBlades"
    assert upgrade["behavior"]["kind"] == "StatusBitsUpgrade"
    assert upgrade["kind"] == "nugget-upgrade"
    assert upgrade["weaponId"] == "TestPike"
    assert upgrade["nuggets"][0]["damage"]["value"] == 115


def test_status_bits_dummy_upgrade_resolves_player_upgrade_weapon_swap() -> None:
    # MordorBlackOrc/WildMarauderSword pattern: the purchase is represented by
    # a dummy status behavior while PLAYER_UPGRADE selects a distinct weapon.
    payload = _weapon_object(
        "  WeaponSet\n"
        "    Conditions = None\n"
        "    Weapon = PRIMARY TestSword\n"
        "  End\n"
        "  WeaponSet\n"
        "    Conditions = PLAYER_UPGRADE\n"
        "    Weapon = PRIMARY TestSwordUpgraded\n"
        "  End\n",
        "  Behavior = StatusBitsUpgrade ModuleTag_ForgedBlades\n"
        "    TriggeredBy = Upgrade_TestForgedBlades\n"
        "  End\n",
    )
    documents = _documents(payload)
    lineage, prepared = _lineage(documents)
    upgrades = compile_weapon_upgrades(
        documents,
        [lineage],
        base_weapon_targets(lineage),
        prepared.numeric_defines,
    )

    assert len(upgrades) == 1
    upgrade = upgrades[0]
    assert upgrade["upgradeId"] == "Upgrade_TestForgedBlades"
    assert upgrade["behavior"]["kind"] == "StatusBitsUpgrade"
    assert upgrade["kind"] == "weapon-swap"
    assert upgrade["weaponId"] == "TestSwordUpgraded"
    assert upgrade["damage"]["value"] == 90
    assert upgrade["damage"]["expression"] == "TEST_UPGRADED_SWORD_DAMAGE"
    assert upgrade["damageType"] == "SLASH"
    assert [(row["percent"], row["filter"]) for row in upgrade["damageScalars"]] == [
        (200.0, "ANY +INFANTRY -HERO"),
        (150.0, "ANY +HERO"),
    ]
    assert upgrade["sourceIni"] == "data/ini/object/test.ini"
    assert upgrade["line"] > 0


def test_status_bits_horde_trigger_joins_member_weapon_swap_only() -> None:
    # Retail Mordor Black Orc pattern: horde owns the purchase StatusBitsUpgrade
    # while the member authors SubObjectsUpgrade for the same upgrade id plus
    # the PLAYER_UPGRADE weapon set.
    payload = (
        "Object TestMember\n"
        "  WeaponSet\n"
        "    Conditions = None\n"
        "    Weapon = PRIMARY TestSword\n"
        "  End\n"
        "  WeaponSet\n"
        "    Conditions = PLAYER_UPGRADE\n"
        "    Weapon = PRIMARY TestSwordUpgraded\n"
        "  End\n"
        "  Behavior = ArmorUpgrade ModuleTag_HeavyArmor\n"
        "    TriggeredBy = Upgrade_TestHeavyArmor\n"
        "  End\n"
        "  Behavior = SubObjectsUpgrade ModuleTag_ForgedBladesSubObjects\n"
        "    TriggeredBy = Upgrade_TestForgedBlades\n"
        "  End\n"
        "End\n"
        "Object TestHorde\n"
        "  Behavior = StatusBitsUpgrade ModuleTag_HeavyArmorLegality\n"
        "    TriggeredBy = Upgrade_TestHeavyArmor\n"
        "  End\n"
        "  Behavior = StatusBitsUpgrade ModuleTag_ForgedBladesLegality\n"
        "    TriggeredBy = Upgrade_TestForgedBlades\n"
        "  End\n"
        "End\n"
    ).encode("cp1252")
    documents = _documents(payload)
    member_lineage, prepared = _lineage(documents, "TestMember")
    horde_lineage, _ = _lineage(documents, "TestHorde")
    upgrades = compile_weapon_upgrades(
        documents,
        [member_lineage, horde_lineage],
        base_weapon_targets(member_lineage),
        prepared.numeric_defines,
    )

    assert len(upgrades) == 1
    assert upgrades[0]["upgradeId"] == "Upgrade_TestForgedBlades"
    assert upgrades[0]["kind"] == "weapon-swap"
    assert upgrades[0]["weaponId"] == "TestSwordUpgraded"
    assert upgrades[0]["damage"]["value"] == 90


def test_status_bits_horde_trigger_joins_member_coordination_weapon() -> None:
    payload = (
        "Object TestMember\n"
        "  WeaponSet\n"
        "    Conditions = None\n"
        "    Weapon = PRIMARY TestSword\n"
        "  End\n"
        "  WeaponSet\n"
        "    Conditions = PLAYER_UPGRADE\n"
        "    Weapon = PRIMARY TestHordeRangeFinderUpgraded\n"
        "  End\n"
        "  Behavior = SubObjectsUpgrade ModuleTag_RangeSubObjects\n"
        "    TriggeredBy = Upgrade_TestRange\n"
        "  End\n"
        "End\n"
        "Object TestHorde\n"
        "  Behavior = StatusBitsUpgrade ModuleTag_RangeLegality\n"
        "    TriggeredBy = Upgrade_TestRange\n"
        "  End\n"
        "End\n"
    ).encode("cp1252")
    documents = _documents(payload)
    documents["data/ini/weapon.ini"] += b"""
Weapon TestHordeRangeFinderUpgraded
  AttackRange = 400
  HordeAttackNugget
  End
End
"""
    member_lineage, prepared = _lineage(documents, "TestMember")
    horde_lineage, _ = _lineage(documents, "TestHorde")
    upgrades = compile_weapon_upgrades(
        documents,
        [member_lineage, horde_lineage],
        base_weapon_targets(member_lineage),
        prepared.numeric_defines,
    )

    assert len(upgrades) == 1
    assert upgrades[0]["upgradeId"] == "Upgrade_TestRange"
    assert upgrades[0]["kind"] == "production-legality"
    assert upgrades[0]["weaponId"] == "TestHordeRangeFinderUpgraded"
    assert upgrades[0]["coordinationAttackRange"]["value"] == 400


def test_status_bits_unrelated_horde_trigger_does_not_join_member_weapon() -> None:
    # Adversarial: uniqueness of a PLAYER_UPGRADE weapon is not identity of an
    # upgrade relationship. An unrelated StatusBitsUpgrade must stay out.
    payload = (
        "Object TestMember\n"
        "  WeaponSet\n"
        "    Conditions = None\n"
        "    Weapon = PRIMARY TestSword\n"
        "  End\n"
        "  WeaponSet\n"
        "    Conditions = PLAYER_UPGRADE\n"
        "    Weapon = PRIMARY TestSwordUpgraded\n"
        "  End\n"
        "  Behavior = SubObjectsUpgrade ModuleTag_ForgedBladesSubObjects\n"
        "    TriggeredBy = Upgrade_TestForgedBlades\n"
        "  End\n"
        "End\n"
        "Object TestHorde\n"
        "  Behavior = StatusBitsUpgrade ModuleTag_Unrelated\n"
        "    TriggeredBy = Upgrade_CompletelyUnrelated\n"
        "  End\n"
        "End\n"
    ).encode("cp1252")
    documents = _documents(payload)
    member_lineage, prepared = _lineage(documents, "TestMember")
    horde_lineage, _ = _lineage(documents, "TestHorde")
    upgrades = compile_weapon_upgrades(
        documents,
        [member_lineage, horde_lineage],
        base_weapon_targets(member_lineage),
        prepared.numeric_defines,
    )

    assert upgrades == []


def test_status_bits_legality_marker_stays_out_of_weapon_upgrades() -> None:
    # A StatusBitsUpgrade whose upgrade gates no weapon nugget is a production
    # legality marker, never an invented effect.
    payload = _weapon_object(
        "  WeaponSet\n    Conditions = None\n    Weapon = PRIMARY TestSword\n  End\n",
        "  Behavior = StatusBitsUpgrade ModuleTag_Legality\n"
        "    TriggeredBy = Upgrade_TestProductionLegality\n"
        "  End\n",
    )
    documents = _documents(payload)
    lineage, prepared = _lineage(documents)
    upgrades = compile_weapon_upgrades(
        documents,
        [lineage],
        base_weapon_targets(lineage),
        prepared.numeric_defines,
    )
    assert upgrades == []


def test_status_bits_same_player_upgrade_weapon_is_not_a_swap() -> None:
    # A PLAYER_UPGRADE condition alone does not prove a damage effect: the
    # selected primary must differ from every authored base weapon.
    payload = _weapon_object(
        "  WeaponSet\n"
        "    Conditions = None\n"
        "    Weapon = PRIMARY TestSword\n"
        "  End\n"
        "  WeaponSet\n"
        "    Conditions = PLAYER_UPGRADE\n"
        "    Weapon = PRIMARY TestSword\n"
        "  End\n",
        "  Behavior = StatusBitsUpgrade ModuleTag_Legality\n"
        "    TriggeredBy = Upgrade_TestProductionLegality\n"
        "  End\n",
    )
    documents = _documents(payload)
    lineage, prepared = _lineage(documents)
    upgrades = compile_weapon_upgrades(
        documents,
        [lineage],
        base_weapon_targets(lineage),
        prepared.numeric_defines,
    )

    assert upgrades == []


def test_status_bits_weapon_swap_fails_closed_without_authored_damage() -> None:
    payload = _weapon_object(
        "  WeaponSet\n"
        "    Conditions = None\n"
        "    Weapon = PRIMARY TestSword\n"
        "  End\n"
        "  WeaponSet\n"
        "    Conditions = PLAYER_UPGRADE\n"
        "    Weapon = PRIMARY TestBrokenUpgradedWeapon\n"
        "  End\n",
        "  Behavior = StatusBitsUpgrade ModuleTag_ForgedBlades\n"
        "    TriggeredBy = Upgrade_TestForgedBlades\n"
        "  End\n",
    )
    documents = _documents(payload)
    lineage, prepared = _lineage(documents)

    with pytest.raises(ArmorCompilerError, match="no resolvable authored damage"):
        compile_weapon_upgrades(
            documents,
            [lineage],
            base_weapon_targets(lineage),
            prepared.numeric_defines,
        )
