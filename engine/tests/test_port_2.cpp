// OpenBFME. GPL-3.0.
// PORT-2: ActiveBody's RecoveryTime (issue #54). RW 0x8C3FA3 ActiveBody::attemptDamage, branch 17 (RW 0x8C425D .. 0x8C4276): after the health change and before
// the last-damage record it reads the module data's +0x24 (the field row RW 0xC71DA8, parser RW 0x73A429 = ceil(ms * 0.005) frames), skips a zero value
// (RW 0x8C4265 `jbe`), adds the current logic frame (TheGameLogic + 0x40, RW 0x8C426D) and calls RW 0x9F3A3C with that deadline. RW 0x9F3A3C is `C2 04 00`
// (`ret 4`), the image's shared EMPTY function, so retail computes the deadline and throws it away: nothing is stored, so nothing is hashed or saved.
// Synthetic data (combattest::CombatWorld), no retail files. Evidence: docs/ennorath/port2/README.md.
#include "doctest.h"
#include "CombatTestUtil.h"

#include "GameLogic/Combat/CombatNames.h"

#include "Common/StateHash.h"

using namespace combattest;

namespace
{
DamageInfo hit(const Object *source, float amount, int type)
{
	DamageInfo d;
	d.m_input.m_sourceID = source ? source->getID() : 0;
	d.m_input.m_amount = amount;
	d.m_input.m_damageType = type;
	return d;
}

// a plain target: an ActiveBody with the field under test
const char kRecoveryObjects[] =
	"Object RecoveryDummy\n"
	"  KindOf = INFANTRY SELECTABLE SCORE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  ArmorSet\n"
	"    Conditions = None\n"
	"    Armor = NoArmor\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 500\n"
	"    RecoveryTime = 5000\n" // 5000 ms: RW 0x73A429 ceil(5000 * 0.005) = 25 frames
	"  End\n"
	"  Behavior = DestroyDie ModuleTag_Destroy\n"
	"  End\n"
	"End\n"
	"Object RecoveryDummyOff\n"
	"  KindOf = INFANTRY SELECTABLE SCORE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  ArmorSet\n"
	"    Conditions = None\n"
	"    Armor = NoArmor\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 500\n"
	"  End\n"
	"  Behavior = DestroyDie ModuleTag_Destroy\n"
	"  End\n"
	"End\n"
	// a HighlanderBody: its attemptDamage (RW 0x8C49C7) clamps and then calls the shared function, so it reads the same field (README section 3)
	"Object RecoveryHighlander\n"
	"  KindOf = INFANTRY SELECTABLE SCORE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  ArmorSet\n"
	"    Conditions = None\n"
	"    Armor = NoArmor\n"
	"  End\n"
	"  Body = HighlanderBody ModuleTag_Body\n"
	"    MaxHealth = 500\n"
	"    RecoveryTime = 10000\n" // the mod's other value: ceil(10000 * 0.005) = 50 frames
	"  End\n"
	"  Behavior = DestroyDie ModuleTag_Destroy\n"
	"  End\n"
	"End\n";

struct RecoveryCounts
{
	unsigned long long before;
	explicit RecoveryCounts() : before(ActiveBody::recoverySchedules()) {}
	unsigned long long delta() const { return ActiveBody::recoverySchedules() - before; }
};
} // namespace

TEST_CASE("PORT-2 recovery: a damaging hit hands the frame plus the parsed RecoveryTime to the empty hook")
{
	CombatWorld w(kRecoveryObjects);
	Object *target = w.unit("RecoveryDummy", 'B', 400, 300);
	Object *source = w.unit("Dummy", 'A', 300, 300); // no weapon, no AutoAcquireEnemiesWhenIdle: only the hits written below
	REQUIRE(target != nullptr);
	ActiveBody *body = dynamic_cast<ActiveBody *>(target->getBodyModule());
	REQUIRE(body != nullptr);

	// the parser: RW 0x73A429 = scanDuration then ceil(ms * 0.005), the port's INI::parseDurationUnsignedInt
	CHECK(body->data()->m_recoveryTime == 25u);

	const UnsignedInt frameAtHit = (UnsignedInt)w.logic->getFrame();
	const RecoveryCounts counts;
	DamageInfo d = hit(source, 100.0f, DAMAGE_SLASH);
	target->attemptDamage(d);

	// one call per damaging hit that reached the health change, with the deadline RW 0x8C4270 computes
	CHECK(counts.delta() == 1);
	CHECK(ActiveBody::lastRecoveryDeadline() == frameAtHit + 25u);
	// the hit itself still lands: the read sits between the health change and the record, it changes neither
	CHECK(w.health(target) == 400.0f);
	CHECK(d.m_output.m_actualDamageDealt == 100.0f);
	CHECK(target->getBodyModule()->getDamageState() == BODY_PRISTINE); // 500 max, thresholds not given: still pristine
}

TEST_CASE("PORT-2 recovery: a zero or absent RecoveryTime skips the call (RW 0x8C4265 jbe), the hit still lands")
{
	CombatWorld w(kRecoveryObjects);
	Object *quiet = w.unit("RecoveryDummyOff", 'B', 400, 300);
	Object *source = w.unit("Dummy", 'A', 300, 300); // no weapon, no AutoAcquireEnemiesWhenIdle: only the hits written below
	REQUIRE(quiet != nullptr);
	ActiveBody *body = dynamic_cast<ActiveBody *>(quiet->getBodyModule());
	REQUIRE(body != nullptr);
	CHECK(body->data()->m_recoveryTime == 0u);

	const RecoveryCounts counts;
	DamageInfo d = hit(source, 100.0f, DAMAGE_SLASH);
	quiet->attemptDamage(d);
	CHECK(counts.delta() == 0);                    // the deadline is not even computed
	CHECK(w.health(quiet) == 400.0f);              // the health change happened
	CHECK(d.m_output.m_actualDamageDealt == 100.0f);
}

TEST_CASE("PORT-2 recovery: only a hit that reaches the health change reads the field (healing and refused hits do not)")
{
	CombatWorld w(kRecoveryObjects);
	Object *target = w.unit("RecoveryDummy", 'B', 400, 300);
	Object *source = w.unit("Dummy", 'A', 300, 300); // no weapon, no AutoAcquireEnemiesWhenIdle: only the hits written below
	REQUIRE(target != nullptr);

	{
		// branch 7 (RW 0x8C406B): a HEALING hit returns before the health change and before branch 17
		const RecoveryCounts counts;
		DamageInfo heal = hit(source, 10.0f, DAMAGE_HEALING);
		target->attemptDamage(heal);
		CHECK(counts.delta() == 0);
	}
	{
		// branch 3 (RW 0x8C3FFB): UNATTACKABLE refuses a non-UNRESISTABLE hit before the health change
		target->setStatus((unsigned)CombatNames::status("UNATTACKABLE"), true);
		const RecoveryCounts counts;
		DamageInfo refused = hit(source, 100.0f, DAMAGE_SLASH);
		target->attemptDamage(refused);
		CHECK(counts.delta() == 0);
		CHECK(w.health(target) == 500.0f);
		target->setStatus((unsigned)CombatNames::status("UNATTACKABLE"), false);
	}
	{
		// the same object, once attackable again: the read is there for every damaging hit, not once
		const RecoveryCounts counts;
		DamageInfo first = hit(source, 50.0f, DAMAGE_SLASH);
		target->attemptDamage(first);
		DamageInfo second = hit(source, 50.0f, DAMAGE_SLASH);
		target->attemptDamage(second);
		CHECK(counts.delta() == 2);
		CHECK(w.health(target) == 400.0f);
	}
}

TEST_CASE("PORT-2 recovery: the deadline follows the frame it was hurt in and wraps as the 32-bit add of RW 0x8C4270")
{
	CombatWorld w(kRecoveryObjects);
	Object *target = w.unit("RecoveryDummy", 'B', 400, 300);
	Object *source = w.unit("Dummy", 'A', 300, 300); // no weapon, no AutoAcquireEnemiesWhenIdle: only the hits written below
	REQUIRE(target != nullptr);

	for (int i = 0; i < 3; ++i)
	{
		w.logic->runLogicFrame();
		const UnsignedInt frameAtHit = (UnsignedInt)w.logic->getFrame();
		const RecoveryCounts counts;
		DamageInfo d = hit(source, 10.0f, DAMAGE_SLASH);
		target->attemptDamage(d);
		CHECK(counts.delta() == 1);
		CHECK(ActiveBody::lastRecoveryDeadline() == frameAtHit + 25u);
	}
	CHECK(w.health(target) == 470.0f);
}

TEST_CASE("PORT-2 recovery: a body class that reaches the shared function reads the same field (HighlanderBody, RW 0x8C49C7 -> RW 0x8C3FA3)")
{
	CombatWorld w(kRecoveryObjects);
	Object *tough = w.unit("RecoveryHighlander", 'B', 400, 300);
	Object *source = w.unit("Dummy", 'A', 300, 300); // no weapon, no AutoAcquireEnemiesWhenIdle: only the hits written below
	REQUIRE(tough != nullptr);
	ActiveBody *body = dynamic_cast<ActiveBody *>(tough->getBodyModule());
	REQUIRE(body != nullptr);
	CHECK(body->data()->m_recoveryTime == 50u); // 10000 ms

	const UnsignedInt frameAtHit = (UnsignedInt)w.logic->getFrame();
	const RecoveryCounts counts;
	DamageInfo d = hit(source, 100.0f, DAMAGE_SLASH);
	tough->attemptDamage(d);
	CHECK(counts.delta() == 1);
	CHECK(ActiveBody::lastRecoveryDeadline() == frameAtHit + 50u);
	CHECK(w.health(tough) == 400.0f);
}

TEST_CASE("PORT-2 recovery: the read adds no state, so a body with RecoveryTime hashes like one without it")
{
	// retail stores nothing from the deadline (RW 0x8C4276 calls `ret 4`), so the body's own state after the same hit is the same whether the field is 25 or
	// 0: a stored deadline, a "recovery ends" frame or a counter kept in the body would show up here, in every save and in every replay
	CombatWorld w(kRecoveryObjects);
	Object *loud = w.unit("RecoveryDummy", 'B', 400, 300);
	Object *quiet = w.unit("RecoveryDummyOff", 'B', 400, 500);
	Object *source = w.unit("Dummy", 'A', 300, 300);
	REQUIRE(loud != nullptr);
	REQUIRE(quiet != nullptr);
	DamageInfo dl = hit(source, 100.0f, DAMAGE_SLASH);
	loud->attemptDamage(dl);
	DamageInfo dq = hit(source, 100.0f, DAMAGE_SLASH);
	quiet->attemptDamage(dq);

	StateHasher hl, hq;
	dynamic_cast<ActiveBody *>(loud->getBodyModule())->crc(hl);
	dynamic_cast<ActiveBody *>(quiet->getBodyModule())->crc(hq);
	CHECK(hl.value() == hq.value());
	// and the two hits are the same hit
	CHECK(w.health(loud) == w.health(quiet));
	CHECK(dl.m_output.m_actualDamageDealt == dq.m_output.m_actualDamageDealt);
	CHECK(dl.m_output.m_actualDamageClipped == dq.m_output.m_actualDamageClipped);
}
