// OpenBFME. HORDE-2 hash mutation tests: every state field HORDE-2 added to the logic changes the world hash on its own (a field the hash misses would let two lockstep
// peers desync silently). One world per case, one field changed. Synthetic data, no retail files.
#include "doctest.h"
#include "CombatTestUtil.h"
#include "Horde2TestUtil.h"

#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/Module/BannerCarrierUpdate.h"
#include "GameLogic/Module/HordeAIUpdate.h"
#include "GameLogic/Module/HordeContain.h"
#include "GameLogic/SimMath.h"
#include "Common/RandomValue.h"

#include <memory>
#include <vector>

using namespace combattest;
using namespace horde2test;

// the friend of HordeAIUpdate, HordeContain and BannerCarrierUpdate (their private state, for the mutations only)
struct Horde2HashAccess
{
	static HordeAIUpdate &ai(Object *h) { return *dynamic_cast<HordeAIUpdate *>(h->getAIUpdateInterface()); }
	static HordeContain &hc(Object *h) { return *dynamic_cast<HordeContain *>(h->getContain()); }
	static void newTarget(Object *h) { ai(h).m_newTarget = !ai(h).m_newTarget; }
	static void swarmTicks(Object *h) { ++ai(h).m_swarmTicks; }
	static void contactRefreshes(Object *h) { ++ai(h).m_contactRefreshes; }
	static void recordDirty(Object *h) { REQUIRE_FALSE(ai(h).m_records.empty()); ai(h).m_records[0].dirty = !ai(h).m_records[0].dirty; }
	static void recordHistory(Object *h) { REQUIRE_FALSE(ai(h).m_records.empty()); ai(h).pushHistory(ai(h).m_records[0], 1234, 5678); }
	static void flankAngle(Object *h) { REQUIRE_FALSE(hc(h).m_flankAngles.empty()); hc(h).m_flankAngles[0] = SimMath::addf32(hc(h).m_flankAngles[0], 0.5f); }
	static void flankIndex(Object *h) { hc(h).m_flankIndex += 1; }
	static void flanker(Object *h) { hc(h).m_flankers[999] = 7; }
	static void canFlank(Object *h) { hc(h).m_canFlank = !hc(h).m_canFlank; }
	static void bannerCarrier(Object *h) { hc(h).m_bannerCarrier += 1; }
	static void bannerCountdown(Object *h) { hc(h).m_bannerCountdown += 3; }
	// the Amoeba idle cycle of a fresh record (active, timer 0) with DelayRandomActivateMin = Max = `draw`: returns {step, idle, timer}
	struct IdleResult
	{
		bool step;
		bool idle;
		unsigned timer;
	};
	static IdleResult idleCycle(Object *h, unsigned draw)
	{
		HordeAIUpdate &a = ai(h);
		MeleeBehaviorModuleData data(MeleeBehaviorModuleData::AMOEBA);
		data.m_delayRandomActivateMin = draw;
		data.m_delayRandomActivateMax = draw;
		data.m_delayUntilIdle = 10;
		HordeAIUpdate::MemberRecord rec;
		rec.idle = false;
		rec.timer = 0;
		const bool step = a.amoebaIdleCycle(rec, data, h->logic());
		return IdleResult{ step, rec.idle, rec.timer };
	}
	static void spawned(Object *banner) { ++dynamic_cast<BannerCarrierUpdate *>(banner->findModule("BannerCarrierUpdate"))->m_spawned; }
	// lane INTEG-1: the melee runtime a stance change (slot 0x260) rebuilds
	static const std::vector<HordeAIUpdate::MemberRecord> &records(Object *h) { return ai(h).m_records; }
	static bool newTargetOf(Object *h) { return ai(h).m_newTarget; }
	static unsigned cacheExpiry(Object *h) { return ai(h).m_cacheExpiry; }
};

namespace
{
struct Horde2World : CombatWorld
{
	Object *hordeA = nullptr, *hordeB = nullptr, *banner = nullptr, *bannerHorde = nullptr, *rider = nullptr;
	Horde2World()
		: CombatWorld((std::string(kCrushObjects) + kBannerObjects).c_str())
	{
		REQUIRE(w.load(kCrushWeapons, INI_LOAD_OVERWRITE, "weapon2.ini").empty());
		combat().setAutoAcquireEnabled(false);
		hordeA = unit("SwordHorde", 'A', 300, 300);
		hordeB = unit("SwordHorde", 'B', 420, 300);
		bannerHorde = unit("BannerHorde", 'A', 300, 700);
		rider = unit("Rider", 'A', 700, 700);
		frames(12);
		REQUIRE(hordeA->getAIUpdateInterface()->aiAttackObject(hordeB, CMD_FROM_PLAYER));
		runUntil([&] { return Horde2HashAccess::hc(hordeA).meleeEngaged(); }, 120);
		REQUIRE(Horde2HashAccess::hc(hordeA).meleeEngaged());
		ExperienceTrackerTestAccess::setRank(*bannerHorde->getExperienceTracker(), 1);
		frames(2);
		banner = byId(Horde2HashAccess::hc(bannerHorde).bannerCarrier());
		REQUIRE(banner != nullptr);
	}
};

#define MUTATE2(NAME, STATEMENT)                                                                                                                          \
	do                                                                                                                                                    \
	{                                                                                                                                                     \
		Horde2World WORLD;                                                                                                                                \
		const std::uint32_t before = WORLD.hash();                                                                                                        \
		{                                                                                                                                                 \
			Horde2World &w = WORLD;                                                                                                                       \
			(void)w;                                                                                                                                      \
			STATEMENT;                                                                                                                                    \
		}                                                                                                                                                 \
		CHECK_MESSAGE(WORLD.hash() != before, NAME);                                                                                                      \
	} while (0)
} // namespace

TEST_CASE("horde2 hash: two identical worlds hash the same")
{
	Horde2World a, b;
	CHECK(a.hash() == b.hash());
}

TEST_CASE("horde2 hash: the horde AI's melee fields - new target flag, swarm ticks, contact refreshes, a record's dirty flag and history")
{
	MUTATE2("new target", Horde2HashAccess::newTarget(w.hordeA));
	MUTATE2("swarm ticks", Horde2HashAccess::swarmTicks(w.hordeA));
	MUTATE2("contact refreshes", Horde2HashAccess::contactRefreshes(w.hordeA));
	MUTATE2("record dirty", Horde2HashAccess::recordDirty(w.hordeA));
	MUTATE2("record history", Horde2HashAccess::recordHistory(w.hordeA));
}

TEST_CASE("horde2 hash: the contain's flank and banner fields - history angle, ring index, flanker map, can-flank byte, carrier id, countdown (the rank is the tracker's, hashed by XP-1)")
{
	MUTATE2("flank angle", Horde2HashAccess::flankAngle(w.hordeA));
	MUTATE2("flank index", Horde2HashAccess::flankIndex(w.hordeA));
	MUTATE2("flanker map", Horde2HashAccess::flanker(w.hordeA));
	MUTATE2("can flank", Horde2HashAccess::canFlank(w.hordeA));
	MUTATE2("banner carrier", Horde2HashAccess::bannerCarrier(w.bannerHorde));
	MUTATE2("banner countdown", Horde2HashAccess::bannerCountdown(w.bannerHorde));
}

TEST_CASE("horde2 hash: the banner carrier's spawn count, the crush weapon's state and the HORDE-2 counters")
{
	MUTATE2("members spawned", Horde2HashAccess::spawned(w.banner));
	MUTATE2("crush weapon ammo", w.rider->getWeapons()->crushWeapon()->setAmmoInClip(w.rider->getWeapons()->crushWeapon()->ammoInClip() + 1));
	MUTATE2("crushes", ++w.combat().counters().crushes);
	MUTATE2("crush weapon shots", ++w.combat().counters().crushWeaponShots);
	MUTATE2("bumps", ++w.combat().counters().crushBumps);
	MUTATE2("ram hits", ++w.combat().counters().ramHits);
	MUTATE2("decelerations", ++w.combat().counters().crushDecelerations);
	MUTATE2("flank tests", ++w.combat().counters().flankTests);
	MUTATE2("flanks", ++w.combat().counters().flanks);
}

// review r2: RW runs the idle branch (RW 0x990561) in the update that drew the idle delay
TEST_CASE("horde2 amoeba: a member that goes idle counts its drawn delay down at once; a drawn 0 reactivates it at once")
{
	Horde2World w;
	const Horde2HashAccess::IdleResult three = Horde2HashAccess::idleCycle(w.hordeA, 3);
	CHECK(three.idle);
	CHECK(three.timer == 2u); // drew 3, decremented in the same update
	CHECK_FALSE(three.step);
	const Horde2HashAccess::IdleResult zero = Horde2HashAccess::idleCycle(w.hordeA, 0);
	CHECK_FALSE(zero.idle);       // drew 0: active again at once
	CHECK(zero.timer == 10u);     // with DelayUntilIdle
	CHECK(zero.step);             // and it steps in this update
}


// lane INTEG-1 (Sol review): RW 0x86C40D (HordeContain slot 0x260, a stance's MeleeBehavior) destroys the horde's melee behaviour object and makes a new one:
// the Amoeba constructor RW 0x98FE62 sets newTarget, runtime slot +4 RW 0x990B5B gives every member a fresh record (RW 0x98F5A5: no destination, no history,
// timer 0, not idle, dirty). The engaged target and the readiness cache stay; nothing is ordered and no logic random number is drawn. A fight that changes
// stance Battle -> Aggressive -> Battle mid-melee rebuilds the runtime each time, the same way in two runs.
TEST_CASE("horde2 melee: a stance's MeleeBehavior in the middle of a melee rebuilds the melee runtime (RW 0x86C40D), keeps the target and draws nothing")
{
	auto run = [](std::vector<std::uint32_t> &hashes) {
		Horde2World w;
		w.frames(15); // the Amoeba records age: timers, histories, idle flags
		HordeContain &hc = Horde2HashAccess::hc(w.hordeA);
		REQUIRE(hc.meleeEngaged());
		const auto &recs = Horde2HashAccess::records(w.hordeA);
		REQUIRE_FALSE(recs.empty());
		bool aged = false;
		for (const auto &r : recs)
		{
			aged = aged || r.historyCount > 0 || r.timer != 0 || r.hasDest || r.idle;
		}
		CHECK(aged); // the probe's state: a record with a timer / history
		const ObjectID engaged = Horde2HashAccess::ai(w.hordeA).engagedTarget();
		const ObjectID cache = Horde2HashAccess::ai(w.hordeA).meleeTargetId();
		const unsigned expiry = Horde2HashAccess::cacheExpiry(w.hordeA);
		auto aggressive = std::make_shared<MeleeBehaviorModuleData>(MeleeBehaviorModuleData::AMOEBA);
		for (int step = 0; step < 2; ++step)
		{
			const GameLogicRandom::Seed seed = w.logic->random().seedArray();
			hc.setMeleeBehavior(step == 0 ? aggressive : nullptr); // Aggressive's Amoeba, then Battle (the contain's own)
			CHECK(w.logic->random().seedArray() == seed);         // no logic random draw
			CHECK(Horde2HashAccess::newTargetOf(w.hordeA));         // RW 0x98FE62 (the contain's own MeleeBehavior is an Amoeba too)
			CHECK(recs.size() == hc.getContainCount());
			for (const auto &r : recs)
			{
				CHECK(r.id != 0);
				CHECK_FALSE(r.hasDest);
				CHECK(r.historyCount == 0);
				CHECK(r.timer == 0u);
				CHECK_FALSE(r.idle);
				CHECK(r.dirty);
			}
			CHECK(Horde2HashAccess::ai(w.hordeA).engagedTarget() == engaged);
			CHECK(Horde2HashAccess::ai(w.hordeA).meleeTargetId() == cache);
			CHECK(Horde2HashAccess::cacheExpiry(w.hordeA) == expiry);
			for (int f = 0; f < 10; ++f)
			{
				w.frames(1);
				hashes.push_back(w.hash());
			}
			CHECK_FALSE(Horde2HashAccess::newTargetOf(w.hordeA)); // the next Amoeba tick took the flag (RW 0x9902A1)
		}
	};
	std::vector<std::uint32_t> a, b;
	run(a);
	run(b);
	CHECK(a == b);
}

// lane INTEG-1 (Sol review): the stance's MeleeBehavior is hashed by its presence and every field of the selected configuration, so two Amoeba entries that
// differ only in DelayUntilIdle (StancesBehavior::applyMeleeBehavior can select one without a stance change) hash differently
TEST_CASE("horde2 hash: a same-kind MeleeBehavior configuration given through slot 0x260 changes the hash on its own")
{
	auto withDelay = [](unsigned delay, bool give) {
		Horde2World w;
		if (give)
		{
			auto d = std::make_shared<MeleeBehaviorModuleData>(MeleeBehaviorModuleData::AMOEBA);
			d->m_delayUntilIdle = delay;
			Horde2HashAccess::hc(w.hordeA).setMeleeBehavior(d);
		}
		return w.hash();
	};
	CHECK(withDelay(10, true) != withDelay(20, true));
	CHECK(withDelay(10, true) == withDelay(10, true));
	CHECK(withDelay(10, true) != withDelay(10, false)); // the override's presence
	// one field at a time, the others the defaults
	auto withField = [](int field) {
		Horde2World w;
		auto d = std::make_shared<MeleeBehaviorModuleData>(MeleeBehaviorModuleData::AMOEBA);
		switch (field)
		{
		case 1: d->m_facingBonus = 11.0f; break;
		case 2: d->m_angleLimitCos = -0.2f; break;
		case 3: d->m_innerRange = 61.0f; break;
		case 4: d->m_outerRange = 91.0f; break;
		case 5: d->m_outerRangeBuildings = 141.0f; break;
		case 6: d->m_idleModelConditions[0] ^= 1u; break;
		case 7: d->m_delayRandomActivateMin += 1; break;
		case 8: d->m_delayRandomActivateMax += 1; break;
		case 9: d->m_followLeader = true; break;
		case 10: d->m_distanceToActiveLeader = 41.0f; break;
		case 11: d->m_distanceToPassiveLeader = 16.0f; break;
		default: break;
		}
		Horde2HashAccess::hc(w.hordeA).setMeleeBehavior(d);
		return w.hash();
	};
	const std::uint32_t base = withField(0);
	for (int f = 1; f <= 11; ++f)
	{
		CHECK_MESSAGE(withField(f) != base, "field " << f);
	}
}
