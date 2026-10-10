// OpenBFME retail tests for lane DECOMP-1: stops closed with the Open-BFME-2 decompilation's progress, every fact confirmed in RotWK's binary. They share the
// HUD tests' retail world (the file name sorts with the test_hud_* files, see test_hud_combat_retail.cpp) and SKIP when ROTWK_INSTALL / BFME2_INSTALL are unset.

#include "doctest.h"
#include "HudTestUtil.h"

#include "Common/PlayerList.h"
#include "Common/Team.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/CombatState.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/Combat/WeaponDelivery.h"
#include "GameLogic/FXEvents.h"
#include "GameLogic/Damage.h"
#include "GameLogic/Economy.h"
#include "GameLogic/EconomySettings.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Module/HordeAIUpdate.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/AttributeModifierPool.h"
#include "GameLogic/Object/RetailObjectWorld.h"
#include "GameLogic/System/DOTManager.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/WeaponNugget.h"
#include "GameLogic/WeaponStores.h"

#include "PathfindTestUtil.h"
#include "RetailTestMount.h"

#include <algorithm>
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
	return hudtest::haveWorld("decomp1 retail");
}

// two enemy sides on a flat arena with the retail economy and AI data (the COMBAT-4 arena)
struct Arena
{
	std::unique_ptr<RetailObjectWorld::ContextScope> context;
	TeamFactory teams;
	PlayerList players;
	GameLogic logic;
	AIWorldConfig cfg;
	pathtest::SyntheticTerrain terrain;
	std::unique_ptr<AIWorld> ai;

	Arena(SharedWorld &s, const char *factionA, const char *factionB, unsigned seed = 7)
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
		logic.random().seedRandom(seed);
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
	Object *place(const char *templateName, int side, float x, float y, float angle)
	{
		const ThingTemplate *t = shared().world->things().findTemplate(templateName);
		REQUIRE_MESSAGE(t != nullptr, "retail template " << templateName);
		Object *o = logic.newObject(t, player(side)->getDefaultTeam(), ObjectStatusMaskType{});
		REQUIRE_MESSAGE(o != nullptr, templateName);
		Coord3D p{ x, y, 0.0f };
		o->setPosition(&p);
		o->setOrientation(angle);
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

const DOTNugget *dotOf(const WeaponTemplate &w)
{
	for (const std::shared_ptr<WeaponNugget> &n : w.m_nuggets)
	{
		if (n->kind() == NUGGET_DOT)
		{
			return static_cast<const DOTNugget *>(n.get());
		}
	}
	return nullptr;
}

float health(const Object &o)
{
	return o.getBodyModule()->getHealth();
}

DOTManager::Record record(std::uint32_t end, float amount)
{
	DOTManager::Record r;
	r.info.m_input.m_damageType = DAMAGE_POISON;
	r.info.m_input.m_amount = amount;
	r.endFrame = end;
	r.interval = 5;
	r.nextFrame = 5;
	return r;
}
} // namespace

// RW 0x8208D4 (x87): (end - now) * amount, the incoming record wins only when strictly greater; an end already passed wraps through the unsigned fild (+ 2^32)
TEST_CASE("decomp1 dot: the stronger record test of RW 0x8208D4")
{
	CHECK(DOTManager::stronger(record(110, 6.0f), record(160, 6.0f), 10));    // 600 < 900
	CHECK_FALSE(DOTManager::stronger(record(160, 6.0f), record(160, 6.0f), 10)); // equal: the current one stays
	CHECK_FALSE(DOTManager::stronger(record(110, 10.0f), record(160, 6.0f), 10)); // 1000 > 900
	CHECK(DOTManager::stronger(record(110, 10.0f), record(160, 7.0f), 10));    // 1000 < 1050
	// end below now: (end - now) as unsigned is about 2^32, far beyond any live record
	CHECK_FALSE(DOTManager::stronger(record(5, 1.0f), record(1000, 100.0f), 10));
}

// RotWK's GoblinFighterSwordPoisoned: DamageNugget then DOTNugget (Damage GOBLIN_FIGHTER_POISON_DAMAGE, POISON, DamageInterval 1000 ms, DamageDuration 15000 ms (the 2.01 data: "originally 30000"),
// SpecialObjectFilter AFFECTED_BY_POISON_OBJECTFILTER). One sword hit: the plain hit, the DOT nugget's own plain hit (RW 0x911485 -> 0x90E683) and a record that
// hurts the victim again every 5 frames (ceil(1000 * 0.005)) until frame + 75, a POISON hit through the victim's armour each time (RW 0x820EF0). Before
// lane DECOMP-1 the DOTNugget was counted as unported and the victim took the sword hit only.
TEST_CASE("decomp1 dot: a goblin's poisoned sword keeps hurting its victim every second for fifteen seconds (DOTNugget, DOTManager)")
{
	if (!haveWorld())
	{
		return;
	}
	Arena a(shared(), "FactionWild", "FactionMen");
	const WeaponTemplate &w = weapon("GoblinFighterSwordPoisoned");
	const DOTNugget *dot = dotOf(w);
	REQUIRE(dot);
	CHECK(dot->m_damageInterval == 5u);
	CHECK(dot->m_damageDuration == 75u);
	CHECK(dot->m_damageType == DAMAGE_POISON);
	Object *goblin = a.place("GoblinFighter", 0, 600.0f, 600.0f, 0.0f);
	Object *victim = a.place("GondorFighter", 1, 620.0f, 600.0f, 3.14159265f);
	a.frames(3);
	const float full = health(*victim);
	const unsigned long long unportedBefore = a.logic.combat().counters().unportedNuggets;
	const std::uint32_t hitFrame = a.logic.getFrame();
	DeliverNuggets(a.logic, goblin->getID(), w, WeaponBonus{}, victim, victim->getPosition(), false, &a.logic.combat().counters().unportedNuggets);
	CHECK(a.logic.combat().counters().unportedNuggets == unportedBefore);
	const float afterHit = health(*victim);
	REQUIRE(afterHit < full);
	REQUIRE(a.logic.dot().entries().count(victim->getID()) == 1);
	const DOTManager::Record &r = a.logic.dot().entries().at(victim->getID());
	CHECK(r.interval == 5u);
	CHECK(r.nextFrame == hitFrame + 5u);
	CHECK(r.endFrame == hitFrame + 75u);
	CHECK(r.info.m_input.m_damageType == DAMAGE_POISON);
	CHECK(r.info.m_input.m_sourceID == goblin->getID());
	// the frames: a tick exactly every 5 frames, the same armour-adjusted amount each time, nothing between
	std::vector<std::uint32_t> tickFrames;
	float tick = -1.0f;
	bool sameAmount = true;
	float prev = afterHit;
	while (a.logic.getFrame() < hitFrame + 85u && !victim->isEffectivelyDead())
	{
		a.frames(1);
		const float h = health(*victim);
		if (h < prev)
		{
			tickFrames.push_back(a.logic.getFrame());
			const float d = prev - h;
			if (tick < 0.0f)
			{
				tick = d;
			}
			else if (std::fabs(d - tick) > 0.001f) // the health is a float: the difference of two healths carries their rounding
			{
				sameAmount = false;
				std::printf("  info: tick at frame %u took %.4f (first %.4f)\n", a.logic.getFrame(), d, tick);
			}
		}
		prev = h;
	}
	std::printf("  info: GondorFighter health %.1f, after the sword %.1f, %zu poison ticks of %.2f, left %.1f\n", full, afterHit, tickFrames.size(), tick, prev);
	CHECK(tick > 0.0f);
	CHECK(sameAmount);
	if (!victim->isEffectivelyDead())
	{
		REQUIRE(tickFrames.size() == 15u);
		for (size_t i = 0; i < tickFrames.size(); ++i)
		{
			CHECK(tickFrames[i] == hitFrame + 5u * (std::uint32_t)(i + 1));
		}
		CHECK(a.logic.dot().entries().count(victim->getID()) == 0);
		CHECK(a.logic.dot().counters().expired == 1u);
	}
	else
	{
		CHECK(a.logic.dot().entries().count(victim->getID()) == 0); // a dead victim drops its record
	}
}

// AFFECTED_BY_POISON_OBJECTFILTER excludes STRUCTURE: shouldDeliver (slot 1, RW 0x90E855) refuses the DOT nugget, no record is made
TEST_CASE("decomp1 dot: a structure is not poisoned (SpecialObjectFilter) and a second hit within the record's life refreshes it")
{
	if (!haveWorld())
	{
		return;
	}
	Arena a(shared(), "FactionWild", "FactionMen");
	const WeaponTemplate &w = weapon("GoblinFighterSwordPoisoned");
	Object *goblin = a.place("GoblinFighter", 0, 600.0f, 600.0f, 0.0f);
	Object *barracks = a.place("GondorBarracks", 1, 700.0f, 700.0f, 0.0f);
	Object *victim = a.place("GondorFighter", 1, 620.0f, 600.0f, 3.14159265f);
	a.frames(3);
	DeliverNuggets(a.logic, goblin->getID(), w, WeaponBonus{}, barracks, barracks->getPosition(), false, nullptr);
	CHECK(a.logic.dot().entries().count(barracks->getID()) == 0);
	DeliverNuggets(a.logic, goblin->getID(), w, WeaponBonus{}, victim, victim->getPosition(), false, nullptr);
	const std::uint32_t first = a.logic.getFrame();
	a.frames(12);
	DeliverNuggets(a.logic, goblin->getID(), w, WeaponBonus{}, victim, victim->getPosition(), false, nullptr);
	const std::uint32_t second = a.logic.getFrame();
	REQUIRE(a.logic.dot().entries().count(victim->getID()) == 1);
	const DOTManager::Record &r = a.logic.dot().entries().at(victim->getID());
	CHECK(r.endFrame == second + 75u); // (75 - 12) * a < 75 * a: the new record replaces the old one
	CHECK(r.nextFrame == second + 5u);
	CHECK(a.logic.dot().counters().added == 1u);
	CHECK(a.logic.dot().counters().replaced == 1u);
	CHECK(second == first + 12u);
}

// the stop is reported at runtime
TEST_CASE("decomp1 dot: S-1959 is a line of the logic report")
{
	if (!haveWorld())
	{
		return;
	}
	Arena a(shared(), "FactionWild", "FactionMen");
	const std::vector<std::string> stops = a.logic.report().stops;
	CHECK(std::find(stops.begin(), stops.end(), std::string(DOTManager::stopLine())) != stops.end());
}

// RotWK's BarrowWightTouch: a radius DamageNugget and AttributeModifierNugget BarrowWightTouchDebuff (DEBUFF, ARMOR -25%, DAMAGE_MULT 75%, Duration 20000 ms),
// AntiCategories LEADERSHIP BUFF, SpecialObjectFilter GENERIC_BUFF_RECIPIENT_OBJECT_FILTER. The touch adds the list to the soldier (RW 0x90EAF9 -> RW 0x68F1A8) and
// disables its LEADERSHIP and BUFF categories until frame + 100 (RW 0x804FCC); a structure is refused by the filter. Before lane DECOMP-1 the nugget was counted
// as unported and nothing happened.
TEST_CASE("decomp1 attribute modifier nugget: a barrow wight's touch debuffs a soldier and blocks its leadership and buffs for the list's duration")
{
	if (!haveWorld())
	{
		return;
	}
	Arena a(shared(), "FactionAngmar", "FactionMen");
	const WeaponTemplate &w = weapon("BarrowWightTouch");
	Object *wight = a.place("BarrowWight", 0, 600.0f, 600.0f, 0.0f);
	Object *victim = a.place("GondorFighter", 1, 615.0f, 600.0f, 3.14159265f);
	Object *barracks = a.place("GondorBarracks", 1, 900.0f, 900.0f, 0.0f);
	a.frames(3);
	AttributeModifierPool *pool = static_cast<AttributeModifierPool *>(victim->findModule("AttributeModifierPoolUpdate"));
	REQUIRE(pool);
	CHECK_FALSE(pool->hasList("BarrowWightTouchDebuff"));
	const unsigned long long unportedBefore = a.logic.combat().counters().unportedNuggets;
	const std::uint32_t now = a.logic.getFrame();
	DeliverNuggets(a.logic, wight->getID(), w, WeaponBonus{}, victim, victim->getPosition(), false, &a.logic.combat().counters().unportedNuggets);
	CHECK(a.logic.combat().counters().unportedNuggets == unportedBefore);
	CHECK(pool->hasList("BarrowWightTouchDebuff"));
	CHECK(pool->categoryDisabledUntil(1) == now + 100u); // LEADERSHIP
	CHECK(pool->categoryDisabledUntil(7) == now + 100u); // BUFF
	CHECK(pool->categoryDisabledUntil(8) == 0u);         // DEBUFF is not an anti category of the nugget
	a.frames(1);
	float mult = 1.0f;
	CHECK(victim->attributeModifierProduct(ATTRIBUTE_MODIFIER_DAMAGE_MULT, nullptr, false, mult));
	CHECK(mult == doctest::Approx(0.75f));
	DeliverNuggets(a.logic, wight->getID(), w, WeaponBonus{}, barracks, barracks->getPosition(), false, nullptr);
	AttributeModifierPool *bp = static_cast<AttributeModifierPool *>(barracks->findModule("AttributeModifierPoolUpdate"));
	REQUIRE(bp);
	CHECK_FALSE(bp->hasList("BarrowWightTouchDebuff"));
	CHECK(bp->categoryDisabledUntil(1) == 0u);
}

// S-1581 closed: RW 0x748650(bit, 0) is RW 0x694569 on one status bit: the object's horde (RW 0x693A1A(0)) changes it on every member, then on itself; an object
// outside a horde changes only its own. The 0xE2 onExit (RW 0x74B716) clears RUNNING_DOWN_FROM_BEHIND through it before its own RW 0x62684D
TEST_CASE("decomp1 status: RW 0x694569 changes a status across the whole horde, members first, and only the object itself outside a horde")
{
	if (!haveWorld())
	{
		return;
	}
	Arena a(shared(), "FactionMen", "FactionMordor");
	Object *horde = a.place("GondorFighterHorde", 0, 600.0f, 600.0f, 0.0f);
	Object *lone = a.place("GondorFighter", 0, 900.0f, 600.0f, 0.0f);
	a.frames(3);
	REQUIRE(horde->getContain());
	const ContainModuleInterface::ContainedItemsList *items = horde->getContain()->getContainedItemsList();
	REQUIRE(items);
	REQUIRE(!items->empty());
	const unsigned bit = (unsigned)CombatNames::statuses().runningDownFromBehind;
	for (Object *m : *items)
	{
		m->setStatus(bit, true);
	}
	horde->setStatus(bit, true);
	lone->setStatus(bit, true);
	items->front()->setStatusAcrossHorde(bit, false); // asked of a member: its horde changes every member and itself
	for (Object *m : *items)
	{
		CHECK_FALSE(m->testStatus(bit));
	}
	CHECK_FALSE(horde->testStatus(bit));
	CHECK(lone->testStatus(bit));
	lone->setStatusAcrossHorde(bit, false);
	CHECK_FALSE(lone->testStatus(bit));
}

// S-586 closed: "in current melee" (RW 0x870180, tier A to the BFME2 decomp's Rva0046CE9DFinish.cpp) resolves both the asked object and the melee target to their
// hordes (RW 0x693A1A(0): null outside a horde) and answers true when they agree, two nulls included, or when the asked object is within an edge gap of 100 of
// the horde. The port compared the objects' containers (or the objects): a second lone enemy far away was "not in melee"; retail says it is
TEST_CASE("decomp1 melee contact: RW 0x870180 compares the hordes of the two objects (null outside a horde), not their containers")
{
	if (!haveWorld())
	{
		return;
	}
	Arena a(shared(), "FactionMen", "FactionMordor");
	Object *horde = a.place("GondorFighterHorde", 0, 600.0f, 600.0f, 0.0f);
	Object *target = a.place("MordorFighter", 1, 625.0f, 600.0f, 3.14159265f);
	Object *far = a.place("MordorFighter", 1, 1400.0f, 1400.0f, 0.0f);
	a.frames(3);
	HordeAIUpdate *h = dynamic_cast<HordeAIUpdate *>(horde->getAIUpdateInterface());
	REQUIRE(h);
	REQUIRE(h->isMeleeTargetReady(*target)); // a close target is ready: the cache takes it for 3 seconds
	REQUIRE(h->meleeTargetId() == target->getID());
	CHECK(h->isInCurrentMelee(target));
	CHECK(h->isInCurrentMelee(far)); // both outside a horde: null == null
	Object *otherHorde = a.place("GondorFighterHorde", 0, 2000.0f, 600.0f, 0.0f);
	a.frames(1);
	CHECK_FALSE(h->isInCurrentMelee(otherHorde)); // a horde (itself) against a lone target (null), far away
}

// RotWK's 12 DisabledTypes (RW 0xDAD904) and ParalyzeNugget (RW 0x90F0E8 / 0x90F403 / 0x90F177): Gloin's Shake Foundation (Duration 30000 ms, no FreezeAnimation) on an
// enemy barracks disables it as DISABLED_USER_PARALYZED for 150 frames with the model condition PARALYZED (RW 0x6907F1), cleared at the expiry by
// checkDisabledStatus (RW 0x690A42 -> 0x692443); IGNORE_PARALYZE_NUGGET makes an object immune. Before lane DECOMP-1 the nugget was counted as unported and the
// object model had 11 types (type 11, USER_FROZEN, threw)
TEST_CASE("decomp1 paralyze: Shake Foundation paralyzes a barracks for 150 frames (USER_PARALYZED, PARALYZED) and an IGNORE_PARALYZE_NUGGET object is immune")
{
	if (!haveWorld())
	{
		return;
	}
	Arena a(shared(), "FactionDwarves", "FactionMen");
	const WeaponTemplate &w = weapon("DwarvenGloinShakeFoundationWeapon");
	Object *gloin = a.place("DwarvenGloin", 0, 600.0f, 600.0f, 0.0f);
	Object *barracks = a.place("GondorBarracks", 1, 700.0f, 600.0f, 0.0f);
	Object *soldier = a.place("GondorFighter", 1, 600.0f, 700.0f, 0.0f);
	a.frames(3);
	const int paralyzed = CombatNames::modelCondition("PARALYZED");
	const std::uint32_t now = a.logic.getFrame();
	DeliverNuggets(a.logic, gloin->getID(), w, WeaponBonus{}, barracks, barracks->getPosition(), false, nullptr);
	CHECK(barracks->isDisabledByType(DISABLED_USER_PARALYZED));
	CHECK(barracks->testModelCondition(paralyzed));
	soldier->setStatus((unsigned)CombatNames::status("IGNORE_PARALYZE_NUGGET"), true);
	DeliverNuggets(a.logic, gloin->getID(), w, WeaponBonus{}, soldier, soldier->getPosition(), false, nullptr);
	CHECK_FALSE(soldier->isDisabledByType(DISABLED_USER_PARALYZED));
	soldier = nullptr; // not used after this frame
	while (a.logic.getFrame() < now + 149u)
	{
		a.frames(1);
	}
	CHECK(barracks->isDisabledByType(DISABLED_USER_PARALYZED));
	a.frames(2);
	CHECK_FALSE(barracks->isDisabledByType(DISABLED_USER_PARALYZED));
	CHECK_FALSE(barracks->testModelCondition(paralyzed));
	// type 11 exists: USER_FROZEN (a FreezeAnimation paralysis) sets PARALYZED too (a fresh soldier: the weapon's other nuggets may have killed the first)
	Object *frozen = a.place("GondorFighter", 1, 1500.0f, 1500.0f, 0.0f);
	frozen->setDisabled(DISABLED_USER_FROZEN, a.logic.getFrame() + 10u);
	CHECK(frozen->isDisabledByType(DISABLED_USER_FROZEN));
	CHECK(frozen->testModelCondition(paralyzed));
}

// RW 0x6CF530 / 0x6CF590 WeaponStore::createAndFireTempWeapon (tier A): a temporary PRIMARY weapon owned by the source fires through the full privateFireWeapon path:
// the fire FX block (FireFX at the shot), the nuggets (here the poisoned sword's hit and its damage over time at the victim), even from an object that has no
// weapons of its own (a barracks: the shot's host is a temporary one). Before lane DECOMP-1 the callers (FireWeaponWhenDead, the HealWeapon, the warhead of a
// HitStoredTarget projectile, SlowDeathBehavior's phase weapon, Devastate) delivered the damage nuggets only, or nothing
TEST_CASE("decomp1 temp weapon: createAndFireTempWeapon fires the full weapon (fire FX, nuggets), also from an object without weapons")
{
	if (!haveWorld())
	{
		return;
	}
	Arena a(shared(), "FactionWild", "FactionMen");
	const WeaponTemplate &w = weapon("GoblinFighterSwordPoisoned");
	Object *goblin = a.place("GoblinFighter", 0, 600.0f, 600.0f, 0.0f);
	Object *victim = a.place("GondorFighter", 1, 620.0f, 600.0f, 3.14159265f);
	Object *barracks = nullptr; // an object without an ObjectWeapons (retail templates with no WeaponSet)
	for (const char *name : { "GondorWell", "GondorWorker", "SuperweaponPing" })
	{
		if (shared().world->things().findTemplate(name))
		{
			Object *o = a.place(name, 0, 900.0f, 900.0f, 0.0f);
			if (!o->getWeapons())
			{
				barracks = o;
				break;
			}
			a.logic.destroyObject(o);
		}
	}
	REQUIRE(barracks);
	a.frames(3);
	auto fireFX = [&]() {
		int n = 0;
		for (const FXEvent &e : a.logic.fxEvents().frameEvents())
		{
			n += e.kind == FXEvent::WEAPON_FIRE_FX ? 1 : 0;
		}
		return n;
	};
	const float before = victim->getBodyModule()->getHealth();
	const int fxBefore = fireFX();
	ObjectWeapons::createAndFireTempWeaponAt(&w, goblin, *victim);
	CHECK(fireFX() == fxBefore + 1);
	CHECK(victim->getBodyModule()->getHealth() < before);
	CHECK(a.logic.dot().entries().count(victim->getID()) == 1);
	// from a structure without an ObjectWeapons, at a position
	ObjectWeapons::createAndFireTempWeapon(&w, barracks, *victim->getPosition());
	CHECK(fireFX() == fxBefore + 2);
}

// S-1582 (r2): DamageContainedNugget's isApplicable (RW 0x911086): only a garrisonable contain holding a living object of every KillKindof bit (the Fire Drake
// warhead's: INFANTRY). The base test alone (the port before r2) answered true for an empty keep
TEST_CASE("decomp1 nugget applicability: DamageContainedNugget applies to a garrison only while it holds a matching occupant (RW 0x911086)")
{
	if (!haveWorld())
	{
		return;
	}
	Arena a(shared(), "FactionMordor", "FactionMen");
	const WeaponTemplate &warhead = weapon("FireDrakeWarhead");
	WeaponTemplate only; // the warhead's DamageContainedNugget alone
	for (const std::shared_ptr<WeaponNugget> &n : warhead.m_nuggets)
	{
		if (n->kind() == NUGGET_DAMAGE_CONTAINED)
		{
			only.m_nuggets.push_back(n);
		}
	}
	REQUIRE(only.m_nuggets.size() == 1u);
	Object *drake = a.place("MordorFighter", 0, 500.0f, 600.0f, 0.0f);
	Object *keep = a.place("GondorKeep", 1, 600.0f, 600.0f, 0.0f);
	Object *archer = a.place("GondorFighter", 1, 800.0f, 800.0f, 0.0f);
	a.frames(3);
	REQUIRE(keep->getContain());
	REQUIRE(keep->getContain()->isGarrisonable());
	CHECK_FALSE(WeaponTemplateAnyNuggetApplicable(a.logic, only, drake->getID(), keep));
	REQUIRE(keep->getContain()->addToContain(archer));
	CHECK(WeaponTemplateAnyNuggetApplicable(a.logic, only, drake->getID(), keep));
}
