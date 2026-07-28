"""The auto-resolve converter, exercised on documents that FAIL.

Retail's own nine documents leave nothing unresolved - 2,254 ``#define``s, zero
misses, zero gaps - which is a good thing about retail and a bad thing about a
test suite: the paths that name an unresolved macro would never run. These tests
therefore feed the readers synthetic documents in retail's own spelling and
assert that every failure is NAMED rather than smoothed over with a number.
"""

from __future__ import annotations

import json
import pathlib

import pytest

from openbfme_importer import living_world_autoresolve as autoresolve


class _Entry:
    def __init__(self, name: str, size: int) -> None:
        self.name = name
        self.archive = "synthetic.big"
        self.offset = 0
        self.size = size
        self.precedence = 0


class _Reader:
    """The two methods :func:`build_from_reader` uses, over an in-memory map."""

    def __init__(self, documents: dict[str, str]) -> None:
        self._documents = {key.casefold(): value.encode("cp1252") for key, value in documents.items()}

    def resolve(self, virtual_path: str):
        payload = self._documents.get(virtual_path.casefold())
        if payload is None:
            return None
        return _Entry(virtual_path.casefold(), len(payload))

    def read(self, entry) -> bytes:
        return self._documents[entry.name]


_EMPTY = {key: "" for _key, key in ((key, path) for key, path in autoresolve.DOCUMENTS)}


def _documents(**overrides: str) -> dict[str, str]:
    documents = dict(_EMPTY)
    for key, text in overrides.items():
        documents[dict(autoresolve.DOCUMENTS)[key]] = text
    return documents


def _build(**overrides: str) -> dict:
    return autoresolve.build_from_reader(_Reader(_documents(**overrides)))


# --- the preprocessor ---------------------------------------------------------


def test_add_multiply_and_divide_are_evaluated_and_percentness_travels() -> None:
    bundle = _build(
        armor="""
#define BASE 100%
#define SCALAR 2.0
#define SCALED #MULTIPLY( BASE SCALAR )
#define SHIFTED #ADD( SCALED 0 )
AutoResolveArmor AutoResolve_DefaultArmor
\tArmor = AutoResolveUnit_Hero SHIFTED
End
"""
    )
    row = bundle["armors"]["AutoResolve_DefaultArmor"]["vs"]["AutoResolveUnit_Hero"]
    assert row["value"] == 200
    assert row["percent"] is True
    # The percent survived the multiply, so the fraction is a multiplier and not
    # a hundred times one.
    assert row["fraction"] == 2
    assert row["raw"] == "SHIFTED"
    assert bundle["totals"]["definesUnresolved"] == 0


def test_divide_is_evaluated_inline_the_way_the_handicap_ladder_authors_it() -> None:
    bundle = _build(
        handicap="""
AutoResolveHandicapLevel
\tGUIDisplayedLevel = 5
\tWeaponMultiplier = 0.95
\tArmorMultiplier  = #DIVIDE( 1.0, 0.95 )
\tExperienceMultiplier = 1.0
End
"""
    )
    rung = bundle["handicapLevels"][0]
    assert rung["guiDisplayedLevel"] == 5
    assert rung["armorMultiplier"] == pytest.approx(1.0 / 0.95)


def test_an_absent_macro_is_named_and_never_replaced_by_a_number() -> None:
    bundle = _build(
        weapon="""
AutoResolveWeapon AutoResolve_DefaultWeapon
\tDamagePerRound = Damage:NO_SUCH_DAMAGE Against:AutoResolveUnit_Hero
End
"""
    )
    row = bundle["weapons"]["AutoResolve_DefaultWeapon"]["damagePerRound"]["AutoResolveUnit_Hero"]
    assert row["unresolved"] is True
    assert row["raw"] == "NO_SUCH_DAMAGE"
    assert "value" not in row
    references = bundle["unresolved"]["references"]
    assert [entry["name"] for entry in references] == ["NO_SUCH_DAMAGE"]
    assert references[0]["block"] == "AutoResolve_DefaultWeapon"
    assert references[0]["field"] == "DamagePerRound"
    assert "no #define declares this name" in references[0]["reason"]


def test_a_cyclic_define_is_named_rather_than_recursed_forever() -> None:
    bundle = _build(
        body="""
#define LOOP_A #ADD( LOOP_B 0 )
#define LOOP_B #ADD( LOOP_A 0 )
AutoResolveBody AutoResolve_DefaultBody
\tHitpointsAtLevel = Hitpoints:LOOP_A Level:1
End
"""
    )
    names = {entry["name"] for entry in bundle["unresolved"]["macros"]}
    assert {"LOOP_A", "LOOP_B"} <= names
    reasons = " ".join(entry["reason"] for entry in bundle["unresolved"]["macros"])
    assert "cyclic #define" in reasons


def test_an_operator_retail_does_not_author_is_named_rather_than_guessed() -> None:
    bundle = _build(
        weapon="""
#define ODD #EXPONENTIATE( 2 8 )
AutoResolveWeapon AutoResolve_DefaultWeapon
\tDamagePerRound = Damage:ODD Against:AutoResolveUnit_Hero
End
"""
    )
    unresolved = {entry["name"]: entry["reason"] for entry in bundle["unresolved"]["macros"]}
    assert "unsupported preprocessor operator #EXPONENTIATE" in unresolved["ODD"]


def test_divide_by_zero_is_named_rather_than_producing_an_infinity() -> None:
    bundle = _build(
        armor="""
#define BOOM #DIVIDE( 1.0 0.0 )
AutoResolveArmor AutoResolve_DefaultArmor
\tArmor = AutoResolveUnit_Hero BOOM
End
"""
    )
    unresolved = {entry["name"]: entry["reason"] for entry in bundle["unresolved"]["macros"]}
    assert unresolved["BOOM"] == "#DIVIDE by zero"


def test_defines_are_file_local_the_way_retail_authors_them() -> None:
    # Retail declares MEN_ARMOR_SCALAR separately in armor, weapon and body. A
    # shared table would let one document's define silently satisfy another's.
    bundle = _build(
        armor="#define SHARED 3\n",
        weapon="""
AutoResolveWeapon AutoResolve_DefaultWeapon
\tDamagePerRound = Damage:SHARED Against:AutoResolveUnit_Hero
End
""",
    )
    row = bundle["weapons"]["AutoResolve_DefaultWeapon"]["damagePerRound"]["AutoResolveUnit_Hero"]
    assert row["unresolved"] is True


# --- the readers --------------------------------------------------------------


def test_an_unmodelled_field_is_gapped_rather_than_dropped() -> None:
    bundle = _build(
        body="""
AutoResolveBody AutoResolve_DefaultBody
\tCanBeAttacked = Yes
\tSomeFutureField = 3
End
"""
    )
    details = [gap["detail"] for gap in bundle["gaps"]]
    assert "SomeFutureField" in details
    assert bundle["totals"]["gaps"] == 1


def test_an_unmodelled_block_is_gapped_rather_than_dropped() -> None:
    bundle = _build(
        leadership="""
AutoResolveLeadership AutoResolve_Test
\tAffects = AutoResolveUnit_Soldier
\tSomeFutureBlock
\t\tKey = 1
\tEnd
End
"""
    )
    reasons = {gap["reason"] for gap in bundle["gaps"]}
    assert "unmodelled-block" in reasons


def test_the_level_bonus_percentage_is_carried_verbatim_with_no_multiplier_derived() -> None:
    bundle = _build(
        weapon="""
AutoResolveWeapon AutoResolve_DefaultWeapon
\tLevelBonus = Level:2 Bonus:10%
End
"""
    )
    row = bundle["weapons"]["AutoResolve_DefaultWeapon"]["levelBonus"][0]
    assert row == {"level": 2, "raw": "10%", "bonusPercent": 10}
    # The ambiguity is stated in the manifest, by id, so a reader cannot miss it.
    assert "levelBonus.reading" in {entry["id"] for entry in bundle["semanticGaps"]}


def test_the_reinforcement_schedule_keeps_retails_signed_each_remaining() -> None:
    bundle = _build(
        reinforcement="""
AutoResolveReinforcementSchedule Defender
\t1 = 0
\t2 = 15
\tEachRemaining = +1
End
"""
    )
    schedule = bundle["reinforcementSchedules"]["Defender"]
    assert schedule["armyRound"] == {"1": 0, "2": 15}
    assert schedule["eachRemaining"] == 1
    assert schedule["eachRemainingRaw"] == "+1"


def test_leadership_percentages_carry_both_the_fraction_and_retails_own_percent() -> None:
    bundle = _build(
        leadership="""
AutoResolveLeadership AutoResolve_Test
\tAffects = AutoResolveUnit_Soldier
\tAffectsHigherLevelFirst = Yes
\tBonusForLevel
\t\tMinLevel = 4
\t\tWeaponMultiplier = 150%
\t\tArmorMultiplier = 50%
\t\tExperienceMultiplier = 200%
\t\tMaximumUnitsAffected = 2
\t\tPriority = 35
\tEnd
End
"""
    )
    bonus = bundle["leaderships"]["AutoResolve_Test"]["bonusForLevel"][0]
    assert bonus["weaponMultiplier"] == 1.5
    assert bonus["weaponMultiplierPercent"] == 150
    assert bonus["armorMultiplier"] == 0.5
    assert bonus["maximumUnitsAffected"] == 2
    assert bonus["priority"] == 35


def test_a_missing_document_is_a_refusal_and_not_an_empty_table() -> None:
    documents = _documents()
    del documents["data/ini/livingworldautoresolvearmor.ini"]
    with pytest.raises(autoresolve.AutoResolveError) as raised:
        autoresolve.build_from_reader(_Reader(documents))
    assert "livingworldautoresolvearmor.ini" in str(raised.value)


def test_the_bundle_writes_the_manifest_next_to_a_directory(tmp_path: pathlib.Path) -> None:
    # `build_bundle` needs a real catalog; the write half is exercised directly.
    bundle = _build()
    target = tmp_path / autoresolve.MANIFEST_NAME
    target.write_text(json.dumps(bundle, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    reloaded = json.loads(target.read_text(encoding="utf-8"))
    assert reloaded["schema"] == autoresolve.SCHEMA
    assert reloaded["schemaVersion"] == autoresolve.SCHEMA_VERSION
    assert reloaded["totals"]["documents"] == 9


def test_the_nine_documents_are_the_nine_retail_ships() -> None:
    # A prior costing named ELEVEN. The catalog carries nine; this asserts the
    # list this module reads is exactly those nine, by path.
    assert [path for _key, path in autoresolve.DOCUMENTS] == [
        "data/ini/livingworldautoresolvearmor.ini",
        "data/ini/livingworldautoresolveweapon.ini",
        "data/ini/livingworldautoresolvebody.ini",
        "data/ini/livingworldautoresolvecombatchain.ini",
        "data/ini/livingworldautoresolveleadership.ini",
        "data/ini/livingworldautoresolvehandicaps.ini",
        "data/ini/livingworldautoresolvereinforcementschedule.ini",
        "data/ini/livingworldautoresolveresourcebonus.ini",
        "data/ini/livingworldautoresolvesciencepurchasepointbonus.ini",
    ]
