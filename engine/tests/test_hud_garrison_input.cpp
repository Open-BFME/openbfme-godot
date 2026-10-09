// OpenBFME. GARRISON-1: an input-driven HUD test on a retail map. A click selects a Gondor archer horde, the alternate setup's right click on a Gondor battle tower is
// the context command MSG_ENTER (ActionManager::canEnterObject); the order travels the command list to the logic and the archers garrison the tower. Needs the retail
// install.

#include "HudTestUtil.h"

#include "GameLogic/AI/GarrisonCommands.h"
#include "GameLogic/GameLogicDispatch.h"
#include "GameLogic/GameMessage.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Map/TerrainLogic.h"
#include "GameLogic/Object/Contain/GarrisonContainRuntime.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"

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
} // namespace

TEST_CASE("garrison hud: selecting an archer horde and clicking a Gondor battle tower is MSG_ENTER; the archers garrison it through the command list")
{
	if (!haveWorld("garrison hud"))
	{
		return;
	}
	SharedWorld &s = shared();
	Rig r(s);
	float mx = 0, my = 0;
	REQUIRE(r.logic().terrain() != nullptr);
	REQUIRE(r.logic().terrain()->getExtent(0, mx, my));
	const Coord3D c0 = r.freeSpot(mx * 0.5f, my * 0.5f, 300.0f);
	Object *tower = r.make("GondorKeep", c0.x, c0.y);
	Object *archers = r.make("GondorArcherHorde", c0.x - 160.0f, c0.y);
	r.lookAt(Coord3D{ c0.x - 80.0f, c0.y, 0.0f }, 420.0f, 520.0f);
	r.frame(3);
	REQUIRE(r.local != nullptr);
	const ICoord2D pa = r.screenOf({ c0.x - 160.0f, c0.y, 6.0f });
	r.leftClick(pa.x, pa.y);
	r.frame(1);
	REQUIRE(r.local->selection().size() == 1);
	CHECK(r.local->selection()[0] == archers->getID());
	r.input->commandTranslator().setUseAlternateMouse(true);
	const ICoord2D pt = r.screenOf({ c0.x, c0.y, 40.0f });
	r.rightClick(pt.x, pt.y);
	CHECK(logCountOf(*r.input, "MSG_ENTER") == 1);
	CHECK(logCountOf(*r.input, "MSG_DO_MOVETO") == 0);
	r.frame(1);
	CHECK(r.game->dispatch().unhandled().count(MSG_ENTER) == 0);
	size_t frames = 0;
	HordeContainInterface *h = archers->getContain()->getHordeContainInterface();
	while (frames < 600 && !(archers->getContainedBy() == tower && h->allMembersEntered()))
	{
		r.frame(1);
		++frames;
	}
	CHECK(archers->getContainedBy() == tower);
	CHECK(h->isGarrisoned());
	CHECK(tower->getContain()->getContainCount() == 1u);
	// with the W3D assets the tower has its ARROW_ garrison points (PassengerBonePrefix, RW 0x87C2B7)
	const GarrisonContain *g = dynamic_cast<const GarrisonContain *>(tower->getContain());
	REQUIRE(g != nullptr);
	CHECK(g->pointsLoaded());
	std::printf("  info: HUD garrison order: inside after %zu frames, garrison points %d / %d / %d\n", frames, g->pointCount(0), g->pointCount(1), g->pointCount(2));
}
