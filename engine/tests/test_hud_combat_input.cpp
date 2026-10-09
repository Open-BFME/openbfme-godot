// OpenBFME. COMBAT-1: an input-driven HUD test. A click selects a Gondor horde, a click on the Mordor horde (the right button of the alternate setup) is the attack order the
// command translator turns into MSG_DO_ATTACK_OBJECT; the order travels the command list to the logic, the hordes fight and the Mordor horde dies. Needs the retail install.

#include "HudTestUtil.h"

#include "GameLogic/Combat/CombatState.h"
#include "GameLogic/GameLogicDispatch.h"
#include "GameLogic/GameMessage.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/Map/TerrainLogic.h"

using namespace hudtest;

namespace
{
size_t logCountOf(const HudInput &in, const std::string &prefix)
{
	size_t n = 0;
	for (const std::string &l : in.messageLog())
	{
		n += l.compare(0, prefix.size(), prefix) == 0 ? 1u : 0u;
	}
	return n;
}
// a spot `dist` from the object that its horde can walk to: each of 16 directions is tried by walking there (the map has cliffs and water), then the object goes back
bool reachableSpot(Rig &r, Object *walker, float dist, Coord3D &out)
{
	const Coord3D from = *walker->getPosition();
	for (int k = 0; k < 16; ++k)
	{
		const float a = 6.2831853f * (float)k / 16.0f;
		const Coord3D to{ from.x + std::cos(a) * dist, from.y + std::sin(a) * dist, 0.0f };
		walker->getAIUpdateInterface()->aiMoveToPosition(to, CMD_FROM_AI);
		for (int f = 0; f < 80; ++f)
		{
			r.game->advance(0.2);
		}
		const float d = std::hypot(walker->getPosition()->x - to.x, walker->getPosition()->y - to.y);
		walker->getAIUpdateInterface()->aiIdle(CMD_FROM_AI);
		Coord3D back = from;
		walker->setPosition(&back);
		for (int f = 0; f < 20; ++f)
		{
			r.game->advance(0.2);
		}
		if (d < 30.0f)
		{
			out = to;
			return true;
		}
	}
	return false;
}
size_t members(Object *horde)
{
	return horde && horde->getContain() ? horde->getContain()->getContainCount() : 0;
}
} // namespace

TEST_CASE("combat hud: selecting a horde and clicking the enemy horde orders the attack through the command list; the hordes fight and the enemy dies")
{
	if (!haveWorld("combat hud"))
	{
		return;
	}
	SharedWorld &s = shared();
	Rig r(s);
	float mx = 0, my = 0;
	REQUIRE(r.logic().terrain() != nullptr);
	REQUIRE(r.logic().terrain()->getExtent(0, mx, my));
	const Coord3D c0 = r.freeSpot(mx * 0.5f, my * 0.5f, 260.0f);
	Player *enemy = r.game->players().findPlayerWithName("Player_2");
	REQUIRE(enemy != nullptr);
	Object *ours = r.make("GondorFighterHorde", c0.x, c0.y);
	// far enough apart that the idle scan of neither horde sees the other before the order
	Coord3D theirsAt{};
	REQUIRE(reachableSpot(r, ours, 260.0f, theirsAt));
	Object *theirs = r.make("MordorFighterHorde", theirsAt.x, theirsAt.y, enemy);
	const ObjectID theirId = theirs->getID();
	const size_t theirMembers = members(theirs);
	REQUIRE(theirMembers == 20);
	r.lookAt(Coord3D{ (c0.x + theirsAt.x) * 0.5f, (c0.y + theirsAt.y) * 0.5f, 0.0f }, 520.0f, 640.0f);
	r.frame(3);
	REQUIRE(r.local != nullptr);
	CHECK_FALSE(r.logic().combat().counters().kills > 0);
	// 1. left click on our horde selects it
	const ICoord2D pu = r.screenOf({ c0.x, c0.y, 6.0f });
	r.leftClick(pu.x, pu.y);
	r.frame(1);
	REQUIRE(r.local->selection().size() == 1);
	CHECK(r.local->selection()[0] == ours->getID());
	// 2. the right click of the alternate setup on the enemy horde orders the attack
	r.input->commandTranslator().setUseAlternateMouse(true);
	const ICoord2D pe = r.screenOf({ theirsAt.x, theirsAt.y, 6.0f });
	r.rightClick(pe.x, pe.y);
	CHECK(logCountOf(*r.input, "MSG_DO_ATTACK_OBJECT") == 1);
	CHECK(logCountOf(*r.input, "MSG_DO_MOVETO") == 0);
	r.frame(1);
	CHECK(r.game->dispatch().unhandled().count(MSG_DO_ATTACK_OBJECT) == 0);
	CHECK(r.game->aiCommands().stats().attacks == 1);
	CHECK(r.game->aiCommands().stats().attackOrdersAccepted == 1);
	REQUIRE(ours->getAIUpdateInterface() != nullptr);
	CHECK(ours->getAIUpdateInterface()->isAttacking());
	// 3. the fight: the Gondor horde walks the 300 units, the Mordor horde acquires it, the orcs die one by one and their horde leaves the world
	size_t frames = 0;
	bool started = false;
	while (frames < 900 && r.logic().findObjectByID(theirId) != nullptr)
	{
		r.frame(1);
		++frames;
		started = started || r.logic().combat().counters().damageApplications > 0;
	}
	CHECK(started);
	CHECK(r.logic().findObjectByID(theirId) == nullptr);
	CHECK(r.logic().combat().counters().kills >= theirMembers);
	CHECK(members(ours) > 0);
	CHECK(r.logic().findObjectByID(ours->getID()) != nullptr);
	std::printf("  info: HUD attack order: the orcs were gone after %zu frames, %llu kills, %zu of 15 Gondor fighters left\n", frames, r.logic().combat().counters().kills, members(ours));
}
