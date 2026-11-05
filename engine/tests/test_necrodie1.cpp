// OpenBFME retail tests (lane NECRODIE-1): a special power's UnitCost (RotWK's SpecialPowerTemplate + 0x80 / + 0x84) is paid by the caster's horde when the
// ability triggers (SpecialAbilityUpdate::update RW 0x85521F .. 0x855321, the member pick RW 0x8705A6). The Angmar Necromancer's Fell Strength costs 2
// acolytes and his Well of Souls 4, killed with UnitCostDeathType 3 (BURNED); the Necromancer himself (the banner carrier) and the horde live. They SKIP when
// ROTWK_INSTALL / BFME2_INSTALL are unset. GPL-3.0.

#include "doctest.h"
#include "HudTestUtil.h"
#include "StealthArena.h"
#include "FactcSweepUtil.h"

#include "Common/SpecialPower.h"
#include "GameLogic/ActionManager.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Damage.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/ActiveBody.h"
#include "GameLogic/Module/SpecialAbilityModules.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/Object/Object.h"

#include <memory>
#include <string>
#include <vector>

namespace
{
using hudtest::shared;
using stealthtest::Arena;
using stealthtest::membersOf;

constexpr unsigned kUngated = 0x40000; // the do* option that skips the pause / disabled gate (RW 0x8980A3)

SpecialPowerModule *powerOf(Object &o, const char *templateName)
{
	for (const std::unique_ptr<BehaviorModule> &m : o.modules())
	{
		SpecialPowerModule *sp = dynamic_cast<SpecialPowerModule *>(m.get());
		if (sp && sp->getSpecialPowerTemplate() && sp->getSpecialPowerTemplate()->getName() == templateName)
		{
			return sp;
		}
	}
	return nullptr;
}

const SpecialAbilityUpdate *updateOf(Object &o, const char *templateName)
{
	for (const std::unique_ptr<BehaviorModule> &m : o.modules())
	{
		const SpecialAbilityUpdate *u = dynamic_cast<const SpecialAbilityUpdate *>(m.get());
		if (u && u->abilityData()->m_specialPowerTemplateName == templateName)
		{
			return u;
		}
	}
	return nullptr;
}

struct Cast
{
	unsigned acolytesBefore = 0, acolytesAfter = 0;
	std::vector<ObjectID> paid; // the acolytes that left the horde
	bool necromancerAlive = false, hordeAlive = false;
	unsigned triggered = 0;
	bool endedUntriggered = false; // the ability ended without its trigger (RW 0x85527D: slot 0x34(1, 1))
};

// the horde alone on the field (nothing hurts it), `drop` acolytes killed first; the power cast at a point 120 in front of it. The horde's count is read on
// the trigger's frame (or when the ability ends without one), the deaths 2 frames later
Cast cast(const char *power, unsigned drop)
{
	Arena a(shared());
	Object *horde = a.place("AngmarNecromancerHorde", 0, 450.0f, 600.0f);
	a.run(10);
	HordeContain *h = dynamic_cast<HordeContain *>(horde->getContain());
	REQUIRE(h != nullptr);
	const ObjectID necromancer = h->bannerCarrier();
	REQUIRE(necromancer != INVALID_ID);
	for (Object *m : membersOf(*horde))
	{
		if (drop != 0 && m->getID() != necromancer)
		{
			m->kill(DAMAGE_UNRESISTABLE, 0);
			--drop;
		}
	}
	a.run(2);
	Cast r;
	std::vector<ObjectID> before;
	for (Object *m : membersOf(*horde))
	{
		before.push_back(m->getID());
	}
	r.acolytesBefore = h->payloadCount();
	SpecialPowerModule *sp = powerOf(*horde, power);
	REQUIRE(sp != nullptr);
	const SpecialAbilityUpdate *u = updateOf(*horde, power);
	REQUIRE(u != nullptr);
	Coord3D at = *horde->getPosition();
	at.x += 120.0f;
	sp->doSpecialPowerAtLocation(at, kUngated);
	// read on the trigger's frame: the dead acolytes go after their SlowDeath and the banner carrier's IdleSpawnRate brings new ones
	CHECK(u->isActive());
	for (int f = 0; f < 200 && u->abilitiesTriggered() == 0 && u->isActive(); ++f)
	{
		a.run(1);
	}
	r.triggered = u->abilitiesTriggered();
	r.endedUntriggered = !u->isActive() && r.triggered == 0;
	r.acolytesAfter = h->payloadCount();
	// the paid members carry UPDATING_AI when they are killed: Object::attemptDamage (RW 0x698E7D) holds the hit one frame
	a.run(2);
	Object *liveHorde = a.logic.findObjectByID(horde->getID());
	r.hordeAlive = liveHorde && !liveHorde->isEffectivelyDead();
	const Object *n = a.logic.findObjectByID(necromancer);
	r.necromancerAlive = n && !n->isEffectivelyDead();
	static const int kBurningDeath = CombatNames::modelCondition("BURNINGDEATH");
	static const unsigned kUnselectable = (unsigned)CombatNames::status("UNSELECTABLE");
	for (ObjectID id : before)
	{
		Object *m = a.logic.findObjectByID(id);
		if (m && m->isEffectivelyDead())
		{
			r.paid.push_back(id);
			// RW 0x8552D5 .. 0x855318: BURNINGDEATH, UNSELECTABLE, an UNRESISTABLE killing hit
			CHECK(m->testModelCondition(kBurningDeath));
			CHECK(m->testStatus(kUnselectable));
			int type = -1;
			const auto *body = dynamic_cast<const ActiveBody *>(m->getBodyModule());
			REQUIRE(body != nullptr);
			CHECK((body->lastDamageRecordType(type) && type == (int)DAMAGE_UNRESISTABLE));
			CHECK(m->getContainedBy() == nullptr);
		}
	}
	return r;
}
} // namespace

TEST_CASE("necrodie1 retail: the Necromancer's powers carry RotWK's UnitCost and UnitCostDeathType")
{
	if (!hudtest::haveWorld("necrodie1"))
	{
		return;
	}
	const auto ctx = shared().world->enterContext();
	const SpecialPowerTemplate *fell = TheSpecialPowerStore->findSpecialPowerTemplate("SpecialAbilityNecroFellStrength");
	const SpecialPowerTemplate *well = TheSpecialPowerStore->findSpecialPowerTemplate("SpecialAbilityNecroWellOfSouls");
	REQUIRE(fell != nullptr);
	REQUIRE(well != nullptr);
	CHECK(fell->getFinalOverride()->m_unitCost == 2);
	CHECK(well->getFinalOverride()->m_unitCost == 4);
	CHECK(fell->getFinalOverride()->m_unitCostDeathType == 3); // BURNED
	CHECK(well->getFinalOverride()->m_unitCostDeathType == 3);
}

TEST_CASE("necrodie1 retail: Fell Strength sacrifices 2 acolytes; the Necromancer and his horde live (RW 0x85521F)")
{
	if (!hudtest::haveWorld("necrodie1"))
	{
		return;
	}
	const Cast r = cast("SpecialAbilityNecroFellStrength", 0);
	MESSAGE("acolytes " << r.acolytesBefore << " -> " << r.acolytesAfter << ", triggered " << r.triggered);
	CHECK(r.triggered == 1);
	CHECK(r.acolytesBefore == 7);
	CHECK(r.acolytesAfter == 5);
	CHECK(r.paid.size() == 2);
	CHECK(r.necromancerAlive);
	CHECK(r.hordeAlive);
}

TEST_CASE("necrodie1 retail: Well of Souls sacrifices 4 acolytes; the Necromancer and his horde live (RW 0x85521F)")
{
	if (!hudtest::haveWorld("necrodie1"))
	{
		return;
	}
	const Cast r = cast("SpecialAbilityNecroWellOfSouls", 0);
	MESSAGE("acolytes " << r.acolytesBefore << " -> " << r.acolytesAfter << ", triggered " << r.triggered);
	CHECK(r.triggered == 1);
	CHECK(r.acolytesAfter == 3);
	CHECK(r.paid.size() == 4);
	CHECK(r.necromancerAlive);
	CHECK(r.hordeAlive);
}

TEST_CASE("necrodie1 retail: a horde with fewer acolytes than the cost does not trigger the power and loses nobody (RW 0x85527D)")
{
	if (!hudtest::haveWorld("necrodie1"))
	{
		return;
	}
	const Cast r = cast("SpecialAbilityNecroWellOfSouls", 4); // 3 acolytes left, the cost is 4
	MESSAGE("acolytes " << r.acolytesBefore << " -> " << r.acolytesAfter << ", triggered " << r.triggered);
	CHECK(r.acolytesBefore == 3);
	CHECK(r.triggered == 0);
	CHECK(r.endedUntriggered);
	CHECK(r.acolytesAfter == 3);
	CHECK(r.paid.empty());
	CHECK(r.necromancerAlive);
}

TEST_CASE("necrodie1 retail: the location test counts the acolytes without the Necromancer against the UnitCost (slot 0x188, RW 0x82D659)")
{
	if (!hudtest::haveWorld("necrodie1"))
	{
		return;
	}
	factc::HudRig h(shared(), "FactionAngmar");
	const auto ctx = h.hud->enterContext();
	const Coord3D spot = h.rig.freeSpot(2800, 1400, 400.0f);
	Object *horde = h.rig.make("AngmarNecromancerHorde", spot.x, spot.y);
	h.frames(5);
	HordeContain *hc = dynamic_cast<HordeContain *>(horde->getContain());
	REQUIRE(hc != nullptr);
	// Well of Souls' ObjectFilter wants an enemy at the location
	Object *enemy = h.rig.make("MordorMountainTroll", spot.x + 150.0f, spot.y, h.rig.game->players().findPlayerWithName("Player_2"));
	h.frames(1);
	const SpecialPowerTemplate *well = TheSpecialPowerStore->findSpecialPowerTemplate("SpecialAbilityNecroWellOfSouls");
	REQUIRE(well != nullptr);
	const Coord3D at = *enemy->getPosition();
	REQUIRE(ActionManager::canDoSpecialPowerAtLocation(*horde, at, well, kUngated));
	unsigned drop = 4; // 3 acolytes and the Necromancer stay: 4 contained, 3 count
	for (Object *m : membersOf(*horde))
	{
		if (drop != 0 && m->getID() != hc->bannerCarrier())
		{
			m->kill(DAMAGE_UNRESISTABLE, 0);
			--drop;
		}
	}
	h.frames(1);
	REQUIRE(hc->payloadCount() == 3);
	CHECK(horde->getContain()->getContainCount() == 4);
	CHECK_FALSE(ActionManager::canDoSpecialPowerAtLocation(*horde, at, well, kUngated));
}
