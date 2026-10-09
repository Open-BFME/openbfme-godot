// OpenBFME unit tests. GPL-3.0.
// Lane BUILD-4 (the owner's FEEDBACK-1 F7: "the walls come up from the ground all at once; the original had the walls come up in sections at the same time
// but would 'grow' along the length of the wall"): a Men wall span built from a fortress pad, its per-tile construction timeline pinned against retail's rule,
// and the client side of each tile (the fade-in, the floor hidden while built). Retail tests: SKIP loudly when ROTWK_INSTALL / BFME2_INSTALL are unset.
//
// The rule (RotWK game.dat, S-001 caveat; BuildAssistant::buildWallSpan RW 0x795221, the tile loop RW 0x7952CF .. 0x79550E):
//   each tile is made complete, its health set to 1 (RW 0x795400), then its GettingBuiltBehavior runs its update now and sleeps StaggeredBuildFactor * index
//   frames (RW 0x795459 .. 0x795463: slot 0x44 = RW 0x8574F0; not for a WALL_HUB tile, template + 0x11B bit 4, nor when the plan's worst code is 10), and only
//   then the percent is set to 0 (RW 0x79546F), the status bits and PARTIALLY | ACTIVELY_BEING_CONSTRUCTED (bits 0x44 / 0x45) set, and the drawable told
//   fadeIn(0x8A) (RW 0x7954A8 -> 0x670AA2). So a waiting tile shows 0 % until its update wakes; awake, every frame heals max / buildFrames (RW 0x857FBD) and
//   the percent is health / max * 100 (RW 0x858074). StaggeredBuildFactor = STANDARD_WALL_STAGGERED_BUILD_FACTOR = 20 in the retail gamedata.ini.

#include "doctest.h"

#include "BuildTestUtil.h"

#include "Common/BuildAssistant.h"
#include "Common/Player.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameClient/Drawable.h"
#include "GameClient/DrawableManager.h"
#include "GameLogic/Construction.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/GameMessage.h"
#include "GameLogic/Module/ConstructionModules.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/WallSpan.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
struct Tile
{
	ObjectID id = INVALID_ID;
	std::string name;
	bool hub = false;
	float maxHealth = 0.0f;
	int buildFrames = 0;
	int buildFrameSeen = 0; // GettingBuiltBehavior's calcTimeToBuild once the self-build started (RW 0x856736)
	std::vector<float> percent; // after every logic frame from the span's frame on (index 0: the span's frame)
	std::vector<bool> floorHidden, fading;
	double fadeTarget = -1.0;
};

const DrawEntry *floorEntry(const Drawable &d)
{
	for (const DrawEntry &e : d.entries())
	{
		if (e.tag == "ModuleTag_DrawFloor")
		{
			return &e;
		}
	}
	return nullptr;
}

// the percent model: float health accumulating max / buildFrames from 1, percent = health / max * 100 (binary32, one rounding each: PC24)
float percentAfterHeals(float maxHealth, int buildFrames, int heals)
{
	volatile float amount = maxHealth / (float)(unsigned)buildFrames;
	volatile float h = 1.0f;
	for (int k = 0; k < heals; ++k)
	{
		h = h + amount;
		if (h > maxHealth)
		{
			h = maxHealth;
		}
	}
	volatile float fraction = h / maxHealth;
	return fraction * 100.0f;
}
} // namespace

TEST_CASE("build4 walls: a Men span's tiles wait at 0 % and rise StaggeredBuildFactor (20) frames apart along the wall; each fades in over 138 client frames; "
          "a segment's floor stays hidden until it stands")
{
	OPENBFME_REQUIRE_START(s);
	std::string error;
	buildtest::Game g;
	REQUIRE_MESSAGE(buildtest::startGame(*s, "FactionMen", "FactionMordor", 5150, g, &error), error);
	LiveGame &live = *g.live;
	GameLogic &logic = live.logic();
	Player *player = live.players().findPlayerWithName(live.report().startSlotPlayers[0]);
	REQUIRE(player != nullptr);
	for (int i = 0; i < 3; ++i)
	{
		logic.runLogicFrame();
	}
	Object *centre = nullptr, *plot = nullptr;
	for (const StartingBase::Placed &p : live.report().startingObjects)
	{
		Object *o = p.structure ? logic.findObjectByID(p.id) : nullptr;
		centre = (o && o->getControllingPlayer() == player) ? o : centre;
	}
	REQUIRE(centre != nullptr);
	float best = -1.0f;
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getControllingPlayer() == player && o->isKindOfName("BASE_FOUNDATION"))
		{
			const float dx = o->getPosition()->x - centre->getPosition()->x, dy = o->getPosition()->y - centre->getPosition()->y;
			if (dx * dx + dy * dy > best)
			{
				best = dx * dx + dy * dy;
				plot = o; // the outermost pad
			}
		}
	}
	REQUIRE(plot != nullptr);
	const ThingTemplate *hubTmpl = logic.things().findTemplate("MenWallHubSmallExpansion");
	const ThingTemplate *capTmpl = logic.things().findTemplate("MenWallHubSmall");
	REQUIRE(hubTmpl != nullptr);
	REQUIRE(capTmpl != nullptr);
	Object *hub = Construction::constructOnPlot(*plot, *hubTmpl->getFinalOverride(), *plot->getPosition(), 0.0f, *player, true);
	REQUIRE(hub != nullptr);
	const WallHubBehavior *wh = WallHubBehavior::find(*hub, 1u << 13);
	REQUIRE(wh != nullptr);
	CHECK(wh->staggeredBuildFactor() == 20); // STANDARD_WALL_STAGGERED_BUILD_FACTOR
	logic.runLogicFrame();
	live.refreshClient(200.0, 0.0);

	Coord3D start = *hub->getPosition(), end = start;
	const float dx = start.x - centre->getPosition()->x, dy = start.y - centre->getPosition()->y;
	const float len = std::sqrt(dx * dx + dy * dy);
	end.x += dx / len * 400.0f;
	end.y += dy / len * 400.0f;
	WallSpan::Plan plan;
	REQUIRE(WallSpan::plan(logic, *hub, start, end, 1u << 13, plan));
	GameMessage m(MSG_WALL_HUB_CONSTRUCT_SPAN, player->getPlayerIndex());
	m.appendIntegerArgument((int)capTmpl->getFinalOverride()->getTemplateID());
	m.appendLocationArgument(start);
	m.appendLocationArgument(end);
	m.appendIntegerArgument((int)(1u << 13));
	m.appendObjectIDArgument(hub->getID());
	live.commands().append(m);
	logic.runLogicFrame();

	// the tiles in plan order (the order they were made: object ids rise)
	std::vector<Tile> tiles;
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getProducerID() == hub->getID() && o->getControllingPlayer() == player && o->isKindOfName("STRUCTURE"))
		{
			Tile t;
			t.id = o->getID();
			t.name = o->getTemplate()->getName();
			t.hub = o->isKindOfName("WALL_HUB");
			t.maxHealth = o->getBodyModule()->getMaxHealth();
			t.buildFrames = (int)dynamic_cast<GettingBuiltBehavior *>(o->findModule("GettingBuiltBehavior"))->buildFrames();
			tiles.push_back(t);
		}
	}
	std::sort(tiles.begin(), tiles.end(), [](const Tile &a, const Tile &b) { return a.id < b.id; });
	REQUIRE(tiles.size() == plan.tiles.size());
	REQUIRE(tiles.size() >= 4);
	for (size_t i = 0; i < tiles.size(); ++i)
	{
		CHECK(tiles[i].name == plan.tiles[i].tmpl->getName());
	}

	auto sample = [&](bool client) {
		if (client)
		{
			live.refreshClient(200.0, 0.0);
		}
		bool all = true;
		for (Tile &t : tiles)
		{
			Object *o = logic.findObjectByID(t.id);
			REQUIRE(o != nullptr);
			t.percent.push_back(o->getConstructionPercent());
			if (t.buildFrameSeen == 0 && o->getConstructionPercent() > 0.0f)
			{
				t.buildFrameSeen = (int)dynamic_cast<GettingBuiltBehavior *>(o->findModule("GettingBuiltBehavior"))->buildFrames();
			}
			all = all && o->getConstructionPercent() == -1.0f;
			const Drawable *d = live.drawables().findByObject(t.id);
			const DrawEntry *fl = d ? floorEntry(*d) : nullptr;
			t.floorHidden.push_back(fl && fl->moduleHidden);
			t.fading.push_back(d && d->fade().mode == 1);
			if (d && t.fadeTarget < 0.0)
			{
				t.fadeTarget = d->fade().target;
			}
		}
		return all;
	};
	// the span's frame: the client applies the creation, the conditions and the fade request (no render time has passed yet)
	sample(true);
	for (const Tile &t : tiles)
	{
		INFO(t.name);
		CHECK(t.fading[0]);
		CHECK(t.fadeTarget == 138.0); // RW 0x7954A8: push 0x8A
	}
	bool done = false;
	for (int f = 0; f < 3000 && !done; ++f)
	{
		logic.runLogicFrame();
		done = sample(true);
	}
	REQUIRE(done);

	// tile 0 heals in the span's frame (its update runs now and wakes now + 0); a staggered segment i shows exactly 0 % until its first heal at frame 20 * i
	int firstSegmentStart = -1;
	for (size_t i = 0; i < tiles.size(); ++i)
	{
		const Tile &t = tiles[i];
		INFO("tile " << i << " " << t.name);
		int startFrame = -1;
		for (size_t f = 0; f < t.percent.size(); ++f)
		{
			if (t.percent[f] != 0.0f)
			{
				startFrame = (int)f;
				break;
			}
		}
		REQUIRE(startFrame >= 0);
		for (int f = 0; f < startFrame; ++f)
		{
			CHECK(t.percent[(size_t)f] == 0.0f); // RW 0x79546F after the stagger: not the 1-health percent
		}
		if (!t.hub)
		{
			CHECK(startFrame == 20 * (int)i);
			firstSegmentStart = firstSegmentStart < 0 ? startFrame : firstSegmentStart;
		}
		// awake, the percent follows health / max * 100 with health 1 + k * max / buildFrames until it completes (-1). A staggered segment heals in the frame it
		// wakes (its self-build started in the span's frame); a WALL_HUB tile's first update starts the self-build (the 1-health percent, RW 0x8566DF), then heals
		const int buildFrames = t.buildFrameSeen;
		REQUIRE(buildFrames > 0);
		// (the update that started it returned a one-second sleep, RW 0x8580AC: the 1-health percent holds until the next update)
		size_t f = (size_t)startFrame;
		int held = 0;
		while (f < t.percent.size() && t.percent[f] == percentAfterHeals(t.maxHealth, buildFrames, 0))
		{
			++f;
			++held;
		}
		CHECK((held > 0) == t.hub);
		int heals = 1;
		for (; f < t.percent.size() && t.percent[f] != -1.0f; ++f, ++heals)
		{
			const float want = percentAfterHeals(t.maxHealth, buildFrames, heals);
			if (t.percent[f] != want)
			{
				FAIL_CHECK("frame " << f << ": percent " << t.percent[f] << ", model " << want << " after " << heals << " heals");
				break;
			}
		}
		CHECK(f < t.percent.size());
		// the client: a segment's floor (BeginScript CurDrawableHideSubObject("ModuleTag_DrawFloor") of the construction states, RW 0x734EF4 -> 0x6789B4) is
		// hidden while it is built and shown by the idle state once it stands
		if (t.name == "MenWallSegmentSmall")
		{
			CHECK(t.floorHidden[1]);
			CHECK(t.floorHidden[(size_t)startFrame + 1]);
			CHECK(!t.floorHidden.back());
		}
		CHECK(!t.fading.back()); // the fade ended
	}
	CHECK(firstSegmentStart == 0);
	std::printf("  info: build4 Men span: %zu tiles; build frames %d (segment), %d (cap); starts", tiles.size(), tiles[0].buildFrameSeen, tiles.back().buildFrameSeen);
	for (const Tile &t : tiles)
	{
		size_t f = 0;
		while (f < t.percent.size() && t.percent[f] == 0.0f)
		{
			++f;
		}
		std::printf(" %s:%zu", t.hub ? "hub" : "seg", f);
	}
	std::printf("\n");
}

TEST_CASE("build4 walls: S-1520 is in DrawableManager::report().stops exactly once")
{
	OPENBFME_REQUIRE_START(s);
	std::string error;
	buildtest::Game g;
	REQUIRE_MESSAGE(buildtest::startGame(*s, "FactionMen", "FactionMordor", 5150, g, &error), error);
	g.live->logic().runLogicFrame();
	g.live->refreshClient(200.0, 0.0);
	int matches = 0;
	for (const std::string &line : g.live->drawables().report().stops)
	{
		if (line.rfind("[S-1520] ", 0) == 0)
		{
			++matches;
			CHECK(line.find("particle systems") != std::string::npos);
		}
	}
	CHECK(matches == 1);
}
