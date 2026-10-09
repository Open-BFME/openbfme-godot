// OpenBFME retail tests (lane MODULES-2): the emotion system over the real RotWK 2.01 data (emotions.ini, the hordes' EmotionTrackerUpdate, the
// MordorAttackTroll's RadiateFearUpdate). They run only when ROTWK_INSTALL and BFME2_INSTALL are set (otherwise SKIP). The world is the HUD tests' shared
// one (one load per test process).

#include "doctest.h"

#include "Mod2TestUtil.h"

#include "Common/StateHash.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/Combat/CombatState.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/BitFlags.h"
#include "GameLogic/Module/EmotionModules.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/System/EmotionSystem.h"

#include <memory>
#include <string>
#include <vector>

namespace
{
int mcBit(const char *name)
{
	for (int i = 0; TheModelConditionNames[i]; ++i)
	{
		if (std::string(TheModelConditionNames[i]) == name)
		{
			return i;
		}
	}
	return -1;
}

bool hasMC(const EmotionNuggetTemplate::Flags &f, const char *name)
{
	const int b = mcBit(name);
	return b >= 0 && ((f[(size_t)b >> 5] >> (b & 31)) & 1u) != 0;
}

const EmotionTrackerUpdateModuleData *trackerData(const ThingTemplate *tt)
{
	for (const ThingTemplate::Nugget &n : tt->behaviorModules().nuggets())
	{
		if (const EmotionTrackerUpdateModuleData *d = dynamic_cast<const EmotionTrackerUpdateModuleData *>(n.data.get()))
		{
			return d;
		}
	}
	return nullptr;
}
} // namespace

TEST_CASE("emotions retail: emotions.ini gives TheEmotionSystem 17 nuggets in file order with the binary's field semantics (RW 0x8E0D59 / table RW 0xC77FD0)")
{
	if (!hudtest::haveWorld("emotions retail"))
	{
		return;
	}
	const EmotionSystem &es = hudtest::shared().world->emotions();
	std::vector<std::string> names;
	for (const auto &n : es.nuggets())
	{
		names.push_back(n->name);
	}
	const std::vector<std::string> expected = { "Taunt_Base", "Alert_Base", "CheerIdle_Base", "CheerBusy_Base", "HeroCheerIdle_Base", "HeroCheerBusy_Base",
		"CheerForAboutToCrush_Base", "FearIdle_Base", "FearBusy_Base", "UncontrollableFear_Base", "UncontrollableFear_Base_Evil", "Terror_Base", "Terror_Civilian",
		"Doom_Base", "BraceForBeingCrushed_Base", "Point_Base", "Quarrel_Base" };
	CHECK(names == expected);
	const EmotionNuggetTemplate *terror = es.find("Terror_Base");
	REQUIRE(terror);
	CHECK(terror->type == EMOTION_TERROR);
	CHECK(terror->aiState == EMOTION_AI_RUN_AWAY_PANIC);
	CHECK(terror->aiLockDuration == 75); // 15000 ms
	CHECK(terror->duration == 50);       // 10000 ms
	CHECK(terror->preventPlayerCommands);
	CHECK(hasMC(terror->modelConditions, "EMOTION_TERROR"));
	CHECK(hasMC(terror->modelConditions, "EMOTION_AFRAID"));
	CHECK(terror->modelConditionsClearOnExit == terror->modelConditions);
	const EmotionNuggetTemplate *unc = es.find("UncontrollableFear_Base");
	REQUIRE(unc);
	CHECK(unc->aiState == EMOTION_AI_IDLE);
	CHECK(hasMC(unc->modelConditionsClear, "MOVING"));
	CHECK(unc->modelConditionsSetOnExit == unc->modelConditionsClear);
	// `Type = TAUNT;FEAR`: the semicolon starts a comment
	CHECK(es.find("Terror_Civilian")->type == EMOTION_TAUNT);
	const EmotionNuggetTemplate *fearIdle = es.find("FearIdle_Base");
	CHECK(fearIdle->ignoreIfUnitBusy);
	CHECK(fearIdle->inactiveDurationSameObject == 50);
	CHECK(es.find("Quarrel_Base")->enemyThreatScale == 1);
	CHECK(es.find("Quarrel_Base")->enemyThreatOffset == -1);
	CHECK(es.find("CheerIdle_Base")->startFX == "FX_EmotionCheer");
}

TEST_CASE("emotions retail: GondorFighterHorde's EmotionTrackerUpdate data and nuggets (AddEmotion order, the OVERRIDE copy of Taunt_Base)")
{
	if (!hudtest::haveWorld("emotions retail"))
	{
		return;
	}
	hudtest::SharedWorld &sh = hudtest::shared();
	auto scope = sh.world->enterContext();
	const ThingTemplate *tt = sh.world->things().findTemplate("GondorFighterHorde");
	REQUIRE(tt);
	const EmotionTrackerUpdateModuleData *d = trackerData(tt);
	REQUIRE(d);
	CHECK(d->m_tauntAndPointDistance == doctest::Approx(100.0f)); // INFANTRY_TAUNT_POINT_RADIUS
	CHECK(d->m_tauntAndPointUpdateDelay == 5);
	CHECK(d->m_fearScanDistance == doctest::Approx(100.0f));       // INFANTRY_FEAR_SCAN_RADIUS
	CHECK(d->m_heroScanDistance == doctest::Approx(150.0f));
	CHECK(d->m_immuneToFearLevel == 5);
	REQUIRE(d->m_emotions.size() >= 6);
	const char *const head[] = { "Terror_Base", "Doom_Base", "BraceForBeingCrushed_Base", "UncontrollableFear_Base", "FearIdle_Base", "FearBusy_Base" };
	for (size_t i = 0; i < 6; ++i)
	{
		CHECK(d->m_emotions[i]->name == head[i]);
		CHECK_FALSE(d->m_emotions[i]->overridden); // the `//OVERRIDE` lines are comments
	}
	MESSAGE("GondorFighterHorde emotions: " << d->m_emotions.size());
	// GondorTowerShieldGuardHorde: `AddEmotion = OVERRIDE Taunt_Base` ... End makes a marked copy
	const EmotionTrackerUpdateModuleData *guard = trackerData(sh.world->things().findTemplate("GondorTowerShieldGuardHorde"));
	REQUIRE(guard);
	bool tauntOverridden = false;
	for (const auto &e : guard->m_emotions)
	{
		tauntOverridden = tauntOverridden || (e->name == "Taunt_Base" && e->overridden);
	}
	CHECK(tauntOverridden);
}

TEST_CASE("emotions retail: a TERROR request makes a Gondor horde's members show EMOTION_TERROR / EMOTION_AFRAID for Terror_Base's 50 frames; deterministic")
{
	if (!hudtest::haveWorld("emotions retail"))
	{
		return;
	}
	hudtest::SharedWorld &sh = hudtest::shared();
	auto scope = sh.world->enterContext();
	const int terrorBit = mcBit("EMOTION_TERROR"), afraidBit = mcBit("EMOTION_AFRAID");
	std::uint32_t hashes[2] = {};
	for (int run = 0; run < 2; ++run)
	{
		mod2test::RetailGame g(sh);
		Object *horde = g.make("GondorFighterHorde", "Men", 300.0f, 300.0f);
		Object *orc = g.make("MordorFighter", "Mordor", 700.0f, 700.0f);
		g.run(3);
		const std::vector<Object *> members = g.members(horde);
		REQUIRE_FALSE(members.empty());
		EmotionTrackerUpdate *t = EmotionTrackerUpdate::of(*horde);
		REQUIRE(t);
		CHECK_FALSE(horde->testModelCondition(terrorBit));
		// a member asks: the request reaches the horde's tracker (RW 0x68F383 walks the + 0x27C chain)
		EmotionTrackerUpdate::requestEmotion(*members.front(), EMOTION_TERROR, orc, 1);
		CHECK(t->requested(EMOTION_TERROR));
		g.run(1);
		REQUIRE(t->current());
		CHECK(t->current()->tmpl().name == "Terror_Base");
		CHECK(t->current()->sourceID() == orc->getID());
		CHECK(horde->testModelCondition(terrorBit));
		for (Object *m : g.members(horde))
		{
			CHECK(m->testModelCondition(terrorBit));
			CHECK(m->testModelCondition(afraidBit));
		}
		CHECK(horde->getAIUpdateInterface()->stateMachine().temporaryStateId() == 20u); // RUN_AWAY_PANIC: RW 0x662FC8 runs state 20 (lane MODULES-3)
		g.run(49);
		CHECK(t->current());
		g.run(1);
		CHECK_FALSE(t->current());
		for (Object *m : g.members(horde))
		{
			CHECK_FALSE(m->testModelCondition(terrorBit));
		}
		hashes[run] = g.logic.computeStateHash();
	}
	CHECK(hashes[0] == hashes[1]);
}

TEST_CASE("emotions retail: the MordorAttackTroll's fear aura (RadiateFearUpdate, InitiallyActive, 300) makes a nearby idle Gondor horde afraid (FearIdle_Base); deterministic")
{
	if (!hudtest::haveWorld("emotions retail"))
	{
		return;
	}
	hudtest::SharedWorld &sh = hudtest::shared();
	auto scope = sh.world->enterContext();
	const int afraidBit = mcBit("EMOTION_AFRAID");
	std::uint32_t hashes[2] = {};
	for (int run = 0; run < 2; ++run)
	{
		mod2test::RetailGame g(sh);
		Object *near = g.make("GondorFighterHorde", "Men", 300.0f, 300.0f);
		Object *far = g.make("GondorFighterHorde", "Men", 300.0f, 800.0f);
		Object *troll = g.make("MordorAttackTroll", "Mordor", 450.0f, 300.0f);
		RadiateFearUpdate *aura = nullptr;
		for (const auto &m : troll->modules())
		{
			aura = aura ? aura : dynamic_cast<RadiateFearUpdate *>(m.get());
		}
		REQUIRE(aura);
		CHECK(aura->isAlreadyUpgraded());
		// the pulse requests FEAR for EmotionPulseInterval (5 frames); FearIdle_Base has no Duration, so it runs while the request lasts, then the troll is
		// refused for InactiveDurationSameObject (50 frames) although the next pulses ask again (RW 0x8E12F5 / 0x8E168A)
		EmotionTrackerUpdate *tn = EmotionTrackerUpdate::of(*near);
		EmotionTrackerUpdate *tf = EmotionTrackerUpdate::of(*far);
		REQUIRE(tn);
		REQUIRE(tf);
		std::string timeline;
		std::vector<bool> running;
		bool afraidWhileRunning = true, farQuiet = true, requestedLater = false;
		for (int i = 0; i < 40; ++i)
		{
			g.run(1);
			const bool on = tn->current() != nullptr;
			running.push_back(on);
			timeline += on ? "F" : (tn->requested(EMOTION_FEAR) ? "r" : ".");
			if (on)
			{
				CHECK(tn->current()->tmpl().name == "FearIdle_Base");
				CHECK(tn->current()->sourceID() == troll->getID());
				for (Object *m : g.members(near))
				{
					afraidWhileRunning = afraidWhileRunning && m->testModelCondition(afraidBit);
				}
			}
			farQuiet = farQuiet && !tf->current();
		}
		MESSAGE("near horde, frames 1 .. 40 (F running, r requested): " << timeline);
		CHECK(aura->pulses() >= 4);
		size_t first = 0;
		while (first < running.size() && !running[first])
		{
			++first;
		}
		REQUIRE(first < running.size());
		for (size_t i = first; i < first + 5; ++i)
		{
			CHECK(running[i]);
		}
		for (size_t i = first + 5; i < running.size(); ++i)
		{
			CHECK_FALSE(running[i]);
			requestedLater = requestedLater || timeline[i] == 'r';
		}
		CHECK(requestedLater);
		CHECK(afraidWhileRunning);
		CHECK(farQuiet);
		for (Object *m : g.members(near))
		{
			CHECK_FALSE(m->testModelCondition(afraidBit));
		}
		hashes[run] = g.logic.computeStateHash();
	}
	CHECK(hashes[0] == hashes[1]);
}

TEST_CASE("emotions retail: each tracker draws the quarrel number once per frame (line 0x25E); horde members have none of their own running")
{
	if (!hudtest::haveWorld("emotions retail"))
	{
		return;
	}
	hudtest::SharedWorld &sh = hudtest::shared();
	auto scope = sh.world->enterContext();
	mod2test::RetailGame g(sh);
	g.make("GondorFighterHorde", "Men", 300.0f, 300.0f);
	g.make("GondorFighterHorde", "Men", 600.0f, 300.0f);
	g.run(3);
	size_t trackers = 0;
	for (Object *o = g.logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (EmotionTrackerUpdate::of(*o) && !o->getContainedBy())
		{
			++trackers;
		}
	}
	g.logic.random().enableCallLog(true);
	g.run(2);
	size_t draws = 0;
	for (const auto &c : g.logic.random().callLog())
	{
		draws += (c.file == "EmotionTrackerUpdate.cpp" && c.line == 0x25E) ? 1u : 0u;
	}
	MESSAGE("top-level trackers " << trackers);
	CHECK(trackers >= 2);
	CHECK(draws == 2 * trackers);
}
