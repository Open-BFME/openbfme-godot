// OpenBFME unit tests (lane MODCOMPAT-MOD1): ClickReactionBehavior (data RW 0x85C655 / table RW 0xC57B28, onClick RW 0x85C75E, clearReaction RW 0x85C8C9,
// update RW 0x85C704, xfer RW 0x85C5DE) reached by the player's selection message (RW 0x77A0FA -> selectObject RW 0x625685 -> RW 0x68C409's interface).
// Synthetic templates on the move tests' world (its lockstep command list and dispatcher); the retail case checks the 2.01 trolls' data and SKIPs when
// ROTWK_INSTALL / BFME2_INSTALL are unset. GPL-3.0.

#include "doctest.h"

#include "MoveTestUtil.h"
#include "RetailTestMount.h"

#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"

#include "Common/StateHash.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "GameLogic/Module/ClickReactionBehavior.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/RetailObjectWorld.h"

#include <memory>
#include <string>
#include <vector>

namespace
{
// the 2.01 mountain troll's numbers with a short meter (ClickReactionTimer 10: a click adds 10, the meter loses 1 a frame, at most 50)
const char kTroll[] =
	"Object TestTroll\n"
	"  KindOf = INFANTRY SELECTABLE\n"
	"  Body = ActiveBody ModuleTag_Body\n    MaxHealth = 100\n  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n  End\n"
	"  Behavior = ClickReactionBehavior ClickBehaviorModuleTag\n"
	"    ClickReactionTimer = 10\n    ReactionFrames1 = 15\n    ReactionFrames2 = 7\n    ReactionFrames3 = 7\n    ReactionFrames4 = 15\n    ReactionFrames5 = 18\n"
	"  End\n"
	"  Behavior = ClickReactionBehavior ClickDefaultsTag\n  End\n"
	"  Geometry = CYLINDER\n  GeometryMajorRadius = 8\n  GeometryHeight = 20\n"
	"  LocomotorSet\n    Locomotor = WalkerLoco\n    Condition = SET_NORMAL\n    Speed = 30\n  End\n"
	"End\n";

int condition(const char *name)
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

int shownReaction(const Object &o)
{
	static const char *const kReact[] = { "REACT_1", "REACT_2", "REACT_3", "REACT_4", "REACT_5", "REACT_6" };
	int shown = 0;
	for (int i = 0; i < 6; ++i)
	{
		if (o.testModelCondition(condition(kReact[i])))
		{
			shown = shown == 0 ? i + 1 : -1; // -1: more than one
		}
	}
	return shown;
}

std::vector<ClickReactionBehavior *> clicksOf(Object &o)
{
	std::vector<ClickReactionBehavior *> out;
	for (const auto &m : o.modules())
	{
		if (auto *c = dynamic_cast<ClickReactionBehavior *>(m.get()))
		{
			out.push_back(c);
		}
	}
	return out;
}

std::vector<int> clickDraws(GameLogic &logic)
{
	std::vector<int> out;
	for (const auto &c : logic.random().callLog())
	{
		if (c.file == "ClickReactionBehavior.cpp")
		{
			CHECK(c.line == 116);
			CHECK(c.lo == 1);
			CHECK(c.hi == 3);
			out.push_back(c.result);
		}
	}
	return out;
}
} // namespace

// RW 0x85C655 / RW 0xC57B28: ClickReactionTimer 600, ReactionFrames 0 by default; plain ints (RW 0x42EC5E), no ms conversion
TEST_CASE("click reaction: the data is RW 0xC57B28 (ClickReactionTimer 600 by default, ReactionFrames as plain frames)")
{
	movetest::MoveWorld mw(100, 100, kTroll);
	mw.buildMap();
	Object *t = mw.spawn("TestTroll", 505.0f, 505.0f);
	const std::vector<ClickReactionBehavior *> c = clicksOf(*t);
	REQUIRE(c.size() == 2);
	CHECK(ClickReactionBehavior::find(*t) == c[0]); // RW 0x68C409: the first module answering
	CHECK(c[0]->clickMeter() == 0);
	CHECK(c[0]->countdown() == 0);
	// the defaults module: a click adds 600 and the countdown takes ReactionFrames 0
	mw.frames(2);
	c[1]->onClick();
	CHECK(c[1]->clickMeter() == 600);
	CHECK(c[1]->countdown() == 0);
}

// RW 0x77A0FA -> 0x625685 -> RW 0x85C75E: the owner's selection message clicks; the reaction runs ReactionFrames[n - 1] frames, then RW 0x85C704 clears it
TEST_CASE("click reaction: the owner's selection makes an idle troll react (logic random 1 .. 3); repeated fast clicks reach REACT_4 / REACT_5")
{
	movetest::MoveWorld mw(100, 100, kTroll);
	mw.buildMap();
	Object *t = mw.spawn("TestTroll", 505.0f, 505.0f);
	ClickReactionBehavior *c = ClickReactionBehavior::find(*t);
	REQUIRE(c);
	mw.frames(3);
	REQUIRE(t->getAIUpdateInterface()->isIdle());

	// an enemy selecting the troll: no click (RW 0x625685: the selecting player must control it)
	mw.select(mw.playerIndex("Bob"), { t });
	mw.frames(1);
	CHECK(c->clickMeter() == 0);
	CHECK(shownReaction(*t) == 0);

	mw.logic->random().clearCallLog();
	mw.logic->random().enableCallLog(true);
	mw.select(mw.playerIndex("Alice"), { t });
	mw.frames(1);
	std::vector<int> draws = clickDraws(*mw.logic);
	REQUIRE(draws.size() == 1);
	const int first = draws[0];
	CHECK(shownReaction(*t) == first);
	const int frames[5] = { 15, 7, 7, 15, 18 };
	// tolerance (the next four): the module's update of the click's frame may run after the command list (one frame off both counters)
	CHECK(c->countdown() <= frames[first - 1]); // tolerance: see above
	CHECK(c->countdown() >= frames[first - 1] - 1); // tolerance: see above
	CHECK(c->clickMeter() >= 9); // tolerance: see above
	CHECK(c->clickMeter() <= 10); // tolerance: see above

	// clicks while it reacts fill the meter (capped at 5 * 10) but start nothing
	for (int i = 0; i < 6; ++i)
	{
		mw.select(mw.playerIndex("Alice"), { t });
		mw.frames(1);
	}
	CHECK(c->clickMeter() <= 50); // tolerance: the cap is 5 * ClickReactionTimer; the frame's own decrement may already have run
	CHECK(shownReaction(*t) == (c->countdown() == 0 ? 0 : first));
	// the reaction ends (RW 0x85C704: countdown 0 -> clearReaction(true))
	int guard = 0;
	while (c->countdown() > 0 && guard++ < 30)
	{
		mw.frames(1);
	}
	CHECK(shownReaction(*t) == 0);
	// a fast click now: meter / timer is 4 or 5, no draw
	mw.logic->random().clearCallLog();
	mw.select(mw.playerIndex("Alice"), { t });
	mw.frames(1);
	draws = clickDraws(*mw.logic);
	mw.logic->random().enableCallLog(false);
	CHECK(draws.empty());
	const int angry = shownReaction(*t);
	CHECK((angry == 4 || angry == 5));

	// the snapshot: retail's two ints round-trip
	StateHasher before;
	c->crc(before);
	XferSave saved;
	c->xfer(saved);
	mw.frames(5);
	{
		XferLoad load(saved.data());
		c->xfer(load);
	}
	StateHasher after;
	c->crc(after);
	CHECK(after.value() == before.value());
	std::vector<std::uint8_t> truncated = saved.data();
	truncated.pop_back();
	XferLoad lt(truncated);
	CHECK_THROWS_AS(c->xfer(lt), XferException);
}

// RW 0x85C75E's gates: HIT_REACTION (172) refuses; HitReactionBehavior's slot 2 (0) call ends a reaction; a moving troll does not react and its reaction ends
TEST_CASE("click reaction: HIT_REACTION refuses a click, clearReaction ends one, a busy AI neither reacts nor keeps its reaction")
{
	movetest::MoveWorld mw(100, 100, kTroll);
	mw.buildMap();
	Object *t = mw.spawn("TestTroll", 505.0f, 505.0f);
	ClickReactionBehavior *c = ClickReactionBehavior::find(*t);
	REQUIRE(c);
	mw.frames(3);
	t->setModelConditionState(condition("HIT_REACTION"), true);
	c->onClick();
	CHECK(c->clickMeter() == 0);
	t->setModelConditionState(condition("HIT_REACTION"), false);
	c->onClick();
	CHECK(shownReaction(*t) >= 1); // tolerance: the reaction (1 .. 3) is the logic random's
	CHECK(c->countdown() > 0); // tolerance: that reaction's ReactionFrames
	c->clearReaction(false); // HitReactionBehavior RW 0x85C55A
	CHECK(shownReaction(*t) == 0);
	CHECK(c->countdown() == 0);
	c->onClick();
	REQUIRE(shownReaction(*t) >= 1); // tolerance: the reaction is the logic random's
	// a move order: the AI is busy, the update ends the reaction and a click does nothing
	mw.select(mw.playerIndex("Alice"), { t });
	mw.moveTo(mw.playerIndex("Alice"), 805.0f, 505.0f);
	mw.frames(3);
	CHECK_FALSE(t->getAIUpdateInterface()->isIdle());
	CHECK(shownReaction(*t) == 0);
	const int meter = c->clickMeter();
	c->onClick();
	CHECK(c->clickMeter() == meter);
}

// ---- retail data ---------------------------------------------------------------------------------------------------------------------------------------
namespace
{
struct RetailShared
{
	retailtest::Mount *mount = nullptr;
	std::unique_ptr<RetailObjectWorld> world;
	std::string error;
};

RetailShared &retailShared()
{
	static RetailShared s;
	static bool built = false;
	if (!built)
	{
		built = true;
		s.mount = retailtest::pureMount();
		if (s.mount && s.mount->fs)
		{
			s.world = std::make_unique<RetailObjectWorld>(*s.mount->fs);
			if (!s.world->load(&s.error))
			{
				s.world.reset();
			}
		}
	}
	return s;
}
} // namespace

// RotWK 2.01: the mountain troll, the attack troll and the cave troll (mountaintroll.ini, attacktroll.ini, cavetroll.ini): ClickReactionTimer 600,
// ReactionFrames 15 / 7 / 7 / 15 / 18
TEST_CASE("click reaction retail: the 2.01 trolls carry the module with ClickReactionTimer 600 and ReactionFrames 15 / 7 / 7 / 15 / 18")
{
	RetailShared &sh = retailShared();
	if (!sh.mount || !sh.mount->fs)
	{
		MESSAGE("SKIP: ROTWK_INSTALL / BFME2_INSTALL not set (retail ClickReactionBehavior test)");
		return;
	}
	REQUIRE_MESSAGE(sh.world, sh.error);
	auto contextScope = sh.world->enterContext();
	for (const char *name : { "MordorMountainTroll", "MordorAttackTroll", "GoblinCaveTroll" })
	{
		const ThingTemplate *t = sh.world->things().findTemplate(name);
		REQUIRE_MESSAGE(t, name);
		const ClickReactionBehaviorModuleData *d = nullptr;
		for (const ThingTemplate::Nugget &n : t->getFinalOverride()->behaviorModules().nuggets())
		{
			d = d ? d : dynamic_cast<const ClickReactionBehaviorModuleData *>(n.data.get());
		}
		REQUIRE_MESSAGE(d, name);
		CHECK(d->m_clickReactionTimer == 600);
		CHECK(d->m_reactionFrames[0] == 15);
		CHECK(d->m_reactionFrames[1] == 7);
		CHECK(d->m_reactionFrames[2] == 7);
		CHECK(d->m_reactionFrames[3] == 15);
		CHECK(d->m_reactionFrames[4] == 18);
	}
}
