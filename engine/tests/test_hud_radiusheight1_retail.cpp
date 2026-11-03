// OpenBFME retail tests for lane RADIUSHEIGHT-1: radius damage honours DamageNugget's DamageMaxHeight (+0x160) and DamageMaxHeightAboveTerrain (+0x164), RW 0x90DEF0
// (BFME2 decomp DamageNugget.cpp DamageNugget::doEffectPosition, the m_140 / m_144 blocks). They share the HUD tests' retail world (see
// test_hud_combat_retail.cpp) and SKIP when ROTWK_INSTALL / BFME2_INSTALL are unset.

#include "HordeCompleteTestUtil.h"
#include "doctest.h"
#include "HudTestUtil.h"

#include "Common/PlayerList.h"
#include "Common/Team.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/Combat/CombatState.h"
#include "GameLogic/Combat/WeaponDelivery.h"
#include "GameLogic/Economy.h"
#include "GameLogic/EconomySettings.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/RetailObjectWorld.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/WeaponNugget.h"
#include "GameLogic/WeaponStores.h"

#include "PathfindTestUtil.h"
#include "RetailTestMount.h"

#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace
{
using hudtest::SharedWorld;
using hudtest::shared;

bool haveWorld()
{
	return hudtest::haveWorld("radiusheight1 retail");
}

// two enemy sides on a flat arena (ground height 0) with the retail economy and AI data (the DECOMP-1 arena)
struct Arena
{
	std::unique_ptr<RetailObjectWorld::ContextScope> context;
	TeamFactory teams;
	PlayerList players;
	GameLogic logic;
	AIWorldConfig cfg;
	pathtest::SyntheticTerrain terrain;
	std::unique_ptr<AIWorld> ai;

	Arena(SharedWorld &s, const char *factionA, const char *factionB)
		: context(s.world->enterContext())
		, players(s.world->nameKeys(), s.world->playerTemplates(), teams)
		, logic(s.world->things(), s.world->modules(), players, RandomAlgorithm::ZH_CarryChain)
		, terrain(120, 120)
	{
		SkirmishSetup setup;
		setup.startingMoney = 0;
		setup.players.push_back({ "A", factionA, true, 0, 0, 1 });
		setup.players.push_back({ "B", factionB, false, 1, 0, 2 });
		const std::vector<std::string> errors = players.setupSkirmish(setup);
		REQUIRE_MESSAGE(errors.empty(), (errors.empty() ? "" : errors[0]));
		std::string err;
		REQUIRE_MESSAGE(GameLogicSettingsLoader::load(*s.mount->fs, logic.settings(), &err), err);
		REQUIRE_MESSAGE(EconomySettings::load(*s.mount->fs, logic.economy().settings(), &err), err);
		logic.random().seedRandom(7);
		logic.economy().initAllCommandPoints();
		REQUIRE_MESSAGE(AIWorldConfigLoader::load(*s.mount->fs, cfg, &err), err);
		ai = std::make_unique<AIWorld>(logic, cfg, s.world->iniMacros());
		ai->attach();
		ai->newMap(terrain);
		logic.combat().setAutoAcquireEnabled(false);
	}
	~Arena()
	{
		logic.reset();
		ai.reset();
	}
	Player *player(int i) { return players.findPlayerWithName(i == 0 ? "A" : "B"); }
	Object *place(const char *templateName, int side, float x, float y)
	{
		const ThingTemplate *t = shared().world->things().findTemplate(templateName);
		REQUIRE_MESSAGE(t != nullptr, "retail template " << templateName);
		Object *o = logic.newObject(t, player(side)->getDefaultTeam(), ObjectStatusMaskType{});
		REQUIRE_MESSAGE(o != nullptr, templateName);
		Coord3D p{ x, y, 0.0f };
		o->setPosition(&p);
		testutil::completeHorde(o); // a horde makes its members where it stands (onBuildComplete, RW 0x8718ED)
		return o;
	}
	void frames(int n)
	{
		for (int i = 0; i < n; ++i)
		{
			logic.runLogicFrame();
		}
	}
};

const WeaponTemplate &weapon(const char *name)
{
	REQUIRE(TheWeaponStore);
	const WeaponTemplate *w = TheWeaponStore->findWeaponTemplate(name);
	REQUIRE_MESSAGE(w != nullptr, "retail weapon " << name);
	return *w;
}

// the retail weapon with only its DamageNuggets (no shockwave or FX nuggets that would move the victims); `keepCaps` false clears both height caps (-1, the
// parser's default), the weapon without the fields
WeaponTemplate damageOnly(const WeaponTemplate &w, bool keepCaps)
{
	WeaponTemplate copy = w;
	copy.m_nuggets.clear();
	for (const std::shared_ptr<WeaponNugget> &n : w.m_nuggets)
	{
		if (n->kind() != NUGGET_DAMAGE)
		{
			continue;
		}
		auto dn = std::make_shared<DamageNugget>(static_cast<const DamageNugget &>(*n));
		if (!keepCaps)
		{
			dn->m_damageMaxHeight = -1.0f;
			dn->m_damageMaxHeightAboveTerrain = -1.0f;
		}
		copy.m_nuggets.push_back(dn);
	}
	REQUIRE(!copy.m_nuggets.empty());
	return copy;
}

float health(const Object &o)
{
	return o.getBodyModule()->getHealth();
}

void lift(Object &o, float z)
{
	Coord3D p = *o.getPosition();
	p.z = z;
	o.setPosition(&p);
}
} // namespace

TEST_CASE("radiusheight1: the retail breath and Rain of Fire nuggets carry their height caps")
{
	if (!haveWorld())
	{
		return;
	}
	for (const char *name : { "MordorBalrogBreath", "RainOfFirePhaseOneWeapon" })
	{
		const WeaponTemplate &w = weapon(name);
		bool anyCap = false;
		for (const std::shared_ptr<WeaponNugget> &n : w.m_nuggets)
		{
			if (n->kind() == NUGGET_DAMAGE)
			{
				const DamageNugget &dn = static_cast<const DamageNugget &>(*n);
				std::printf("  info: %s DamageNugget Radius %.1f DamageMaxHeight %.1f DamageMaxHeightAboveTerrain %.1f\n", name, dn.m_radius, dn.m_damageMaxHeight,
					dn.m_damageMaxHeightAboveTerrain);
				anyCap = anyCap || dn.m_damageMaxHeight != -1.0f || dn.m_damageMaxHeightAboveTerrain != -1.0f;
			}
		}
		CHECK_MESSAGE(anyCap, name);
	}
}

// DamageMaxHeight 50 (both nuggets, DamageArc 90 at the Balrog's own position): the victim's z less the source's z (RW 0x90E1C8 .. 0x90E1DD); Gwaihir 100 above the Balrog is spared, a soldier on the ground and one 30 up are
// burnt; with the caps cleared Gwaihir burns too (the armour is not what spares it)
TEST_CASE("radiusheight1: the Balrog's breath spares an eagle flying above it and burns the soldiers beside it (DamageMaxHeight)")
{
	if (!haveWorld())
	{
		return;
	}
	for (const bool keepCaps : { true, false })
	{
		CAPTURE(keepCaps);
		Arena a(shared(), "FactionMordor", "FactionMen");
		const WeaponTemplate w = damageOnly(weapon("MordorBalrogBreath"), keepCaps);
		Object *balrog = a.place("MordorBalrog", 0, 600.0f, 600.0f);
		Object *eagle = a.place("GondorGwaihir", 1, 660.0f, 610.0f);
		Object *ground = a.place("GondorFighter", 1, 650.0f, 590.0f);
		Object *low = a.place("GondorFighter", 1, 640.0f, 600.0f);
		a.frames(3);
		lift(*balrog, 0.0f);
		lift(*eagle, 100.0f);
		lift(*ground, 0.0f);
		lift(*low, 30.0f);
		balrog->setOrientation(0.0f);
		const float eagleFull = health(*eagle), groundFull = health(*ground), lowFull = health(*low);
		const Coord3D center = *balrog->getPosition(); // DamageDealtAtSelfPosition: the 90 degree DamageArc cone opens along the Balrog's facing (+x)
		DeliverNuggets(a.logic, balrog->getID(), w, WeaponBonus{}, nullptr, &center, false, &a.logic.combat().counters().unportedNuggets);
		a.frames(15); // DelayTime 1000: the hits land 5 frames later (the victims were chosen at delivery)
		std::printf("  info: caps %d: Gwaihir %.1f -> %.9g, soldier %.1f -> %.1f, soldier 30 up %.1f -> %.1f\n", (int)keepCaps, eagleFull, health(*eagle), groundFull,
			health(*ground), lowFull, health(*low));
		CHECK(health(*ground) == 0.0f); // burnt to death
		CHECK(health(*low) == 0.0f); // 30 up is under the cap: burnt
		if (keepCaps)
		{
			CHECK(health(*eagle) == eagleFull);
		}
		else
		{
			CHECK(health(*eagle) == 1200.00012f); // 3000 less the two nuggets after Gwaihir's armour (float health)
		}
	}
}

// DamageMaxHeightAboveTerrain 1: Object::getHeightAboveTerrain (RW 0x70B8AE) above 1 is spared (RW 0x90E1F0 .. 0x90E20C): Gwaihir in flight and a soldier thrown 5 up
// take nothing, the soldier on the ground is hit; with the caps cleared all three are hit
TEST_CASE("radiusheight1: Rain of Fire spares a flyer and a thrown soldier and hits the soldier on the ground (DamageMaxHeightAboveTerrain)")
{
	if (!haveWorld())
	{
		return;
	}
	for (const bool keepCaps : { true, false })
	{
		CAPTURE(keepCaps);
		Arena a(shared(), "FactionMordor", "FactionMen");
		const WeaponTemplate w = damageOnly(weapon("RainOfFirePhaseOneWeapon"), keepCaps);
		Object *caster = a.place("MordorBalrog", 0, 400.0f, 400.0f);
		Object *eagle = a.place("GondorGwaihir", 1, 640.0f, 620.0f);
		Object *ground = a.place("GondorFighter", 1, 640.0f, 580.0f);
		Object *thrown = a.place("GondorFighter", 1, 660.0f, 600.0f);
		a.frames(3);
		lift(*caster, 0.0f);
		lift(*eagle, 100.0f);
		lift(*ground, 0.0f);
		lift(*thrown, 5.0f);
		const float eagleFull = health(*eagle), groundFull = health(*ground), thrownFull = health(*thrown);
		const Coord3D center{ 640.0f, 600.0f, 0.0f };
		DeliverNuggets(a.logic, caster->getID(), w, WeaponBonus{}, nullptr, &center, false, &a.logic.combat().counters().unportedNuggets);
		std::printf("  info: caps %d: Gwaihir %.1f -> %.1f, soldier %.1f -> %.1f, thrown soldier %.1f -> %.1f\n", (int)keepCaps, eagleFull, health(*eagle), groundFull,
			health(*ground), thrownFull, health(*thrown));
		CHECK(health(*ground) == 0.0f); // burnt to death
		if (keepCaps)
		{
			CHECK(health(*eagle) == eagleFull);
			CHECK(health(*thrown) == thrownFull);
		}
		else
		{
			CHECK(health(*eagle) == 0.0f);
			CHECK(health(*thrown) == 0.0f);
		}
	}
}

// the boundaries (Sol review): both tests are strict (ja): a victim exactly at the cap is hit, the next float above it is spared; DamageMaxHeight measures from the
// source object's z (+0x40), not the impact point's. Rain of Fire's nugget (no DamageArc) with DamageMaxHeight 50 set, the caster raised to 20, the impact at 0.
TEST_CASE("radiusheight1: both caps are strict and DamageMaxHeight measures from the source object, not the impact point")
{
	if (!haveWorld())
	{
		return;
	}
	for (const bool aboveTerrain : { false, true })
	{
		CAPTURE(aboveTerrain);
		Arena a(shared(), "FactionMordor", "FactionMen");
		WeaponTemplate w = damageOnly(weapon("RainOfFirePhaseOneWeapon"), false);
		for (std::shared_ptr<WeaponNugget> &n : w.m_nuggets)
		{
			auto dn = std::make_shared<DamageNugget>(static_cast<const DamageNugget &>(*n));
			if (aboveTerrain)
			{
				dn->m_damageMaxHeightAboveTerrain = 1.0f;
			}
			else
			{
				dn->m_damageMaxHeight = 50.0f;
			}
			n = dn;
		}
		Object *caster = a.place("MordorBalrog", 0, 400.0f, 400.0f);
		Object *atCap = a.place("GondorFighter", 1, 640.0f, 580.0f);
		Object *overCap = a.place("GondorFighter", 1, 660.0f, 600.0f);
		a.frames(3);
		const float cap = aboveTerrain ? 1.0f : 70.0f; // the caster stands 20 up: 70 is 50 above it
		lift(*caster, aboveTerrain ? 0.0f : 20.0f);
		lift(*atCap, cap);
		lift(*overCap, std::nextafter(cap, 1000.0f));
		const float full = health(*atCap);
		const Coord3D center{ 650.0f, 590.0f, 0.0f };
		DeliverNuggets(a.logic, caster->getID(), w, WeaponBonus{}, nullptr, &center, false, &a.logic.combat().counters().unportedNuggets);
		CHECK(health(*atCap) == 0.0f);
		CHECK(health(*overCap) == full);
	}
}
