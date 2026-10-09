// OpenBFME retail tests for GARRISON-3 (the owner's feedback G6: archers came out of a tower already in formation): a horde leaving a HordeGarrisonContain comes
// out through the contain's exit (GarrisonContain::exitObjectViaDoor RW 0x87D3CA -> RW 0x87CA2B: every member at the EntryPosition, then the horde object), the
// members keep their formation slots while they are on the way (HordeContain::onRemoving RW 0x86CF2A does not free them, the re-join RW 0x872D0A places only a
// member without a slot), the horde rejoins them and re-forms (slot 0x10, RW 0x8759FF) and they walk to their slots: no member moves faster than its locomotor
// on any frame of the exit. The positions over the exit frames are printed (MESSAGE) for the report. They share the HUD tests' retail world (the file name sorts
// with the test_hud_* files) and SKIP when ROTWK_INSTALL / BFME2_INSTALL are unset.

#include "doctest.h"
#include "HudTestUtil.h"

#include "Common/PlayerList.h"
#include "Common/Team.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/AI/AIGarrisonStates.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/AI/GarrisonCommands.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Economy.h"
#include "GameLogic/EconomySettings.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/GameLogicDispatch.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Object/Contain/GarrisonContainRuntime.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/RetailObjectWorld.h"

#include "PathfindTestUtil.h"
#include "RetailTestMount.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace
{
using hudtest::SharedWorld;
using hudtest::shared;

bool haveWorld()
{
	return hudtest::haveWorld("garrison exit");
}

struct Arena
{
	std::unique_ptr<RetailObjectWorld::ContextScope> context;
	TeamFactory teams;
	PlayerList players;
	GameLogic logic;
	AIWorldConfig cfg;
	pathtest::SyntheticTerrain terrain;
	std::unique_ptr<AIWorld> ai;
	GameLogicDispatch dispatch;

	explicit Arena(SharedWorld &s, unsigned seed = 7)
		: context(s.world->enterContext())
		, players(s.world->nameKeys(), s.world->playerTemplates(), teams)
		, logic(s.world->things(), s.world->modules(), players, RandomAlgorithm::ZH_CarryChain)
		, terrain(120, 120)
		, dispatch(logic)
	{
		SkirmishSetup setup;
		setup.startingMoney = 0;
		setup.players.push_back({ "A", "FactionMen", true, 0, 0, 1 });
		setup.players.push_back({ "B", "FactionMordor", false, 1, 0, 2 });
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
		GarrisonCommands::registerHandlers(dispatch);
	}
	~Arena()
	{
		logic.reset();
		ai.reset();
	}
	Player *player(int i) { return players.findPlayerWithName(i == 0 ? "A" : "B"); }
	Object *place(const char *templateName, int side, float x, float y, float angle = 0.0f)
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
	void run(int frames)
	{
		for (int i = 0; i < frames; ++i)
		{
			logic.runLogicFrame();
		}
	}
	void send(int side, int type, std::vector<ObjectID> selection, std::vector<ObjectID> args)
	{
		player(side)->selection() = selection;
		GameMessage m(type, player(side)->getPlayerIndex());
		for (ObjectID id : args)
		{
			m.appendObjectIDArgument(id);
		}
		dispatch.dispatch(m);
	}
};

HordeContainInterface *hordeOf(Object &o)
{
	return o.getContain() ? o.getContain()->getHordeContainInterface() : nullptr;
}

std::vector<Object *> membersOf(Object &horde)
{
	std::vector<Object *> out;
	if (ContainModuleInterface *c = horde.getContain())
	{
		if (const ContainModuleInterface::ContainedItemsList *items = c->getContainedItemsList())
		{
			out.assign(items->begin(), items->end());
		}
	}
	return out;
}

float dist2d(const Coord3D &a, const Coord3D &b)
{
	const double dx = (double)a.x - (double)b.x, dy = (double)a.y - (double)b.y;
	return (float)std::sqrt(dx * dx + dy * dy);
}

// the horde garrisons `tower`; true once the horde and every member are inside
bool garrison(Arena &a, Object *tower, Object *horde)
{
	a.send(0, MSG_ENTER, { horde->getID() }, { horde->getID(), tower->getID() });
	for (int f = 0; f < 600; ++f)
	{
		a.run(1);
		HordeContainInterface *h = hordeOf(*horde);
		if (horde->getContainedBy() == tower && h && h->allMembersEntered() && !membersOf(*horde).empty())
		{
			return true;
		}
	}
	return false;
}

struct ExitTrace
{
	// the members' ids and their positions per frame (frame 0 = the frame of the evacuate message)
	std::vector<ObjectID> ids;
	std::vector<std::map<ObjectID, Coord3D>> frames;
	std::vector<Coord3D> horde;
	std::map<ObjectID, int> slotBefore;          // the slot each member held inside the tower
	std::map<ObjectID, float> speed;             // the member's locomotor speed per frame
	int exitFrame = -1;                          // the first frame with the horde out of the tower
	Coord3D exitSpot{ 0.0f, 0.0f, 0.0f };        // the contain's EntryPosition in the world
	Coord3D exitOffset{ 0.0f, 0.0f, 0.0f };      // the contain's ExitOffset in the world
	float worstSlotError = 0.0f;                 // at the end: the largest distance of a member from its slot
	std::uint32_t hash = 0;
};

// garrisons `towerName` with a Gondor archer horde, evacuates it and records `frames` frames of the exit
ExitTrace garrisonAndExit(Arena &a, const char *towerName, float towerAngle, int frames)
{
	ExitTrace t;
	Object *tower = a.place(towerName, 0, 600.0f, 600.0f, towerAngle);
	Object *archers = a.place("GondorArcherHorde", 0, 450.0f, 600.0f);
	a.run(20); // the members settle on their slots
	REQUIRE_MESSAGE(garrison(a, tower, archers), "the archers never got into " << towerName);
	a.run(5);
	GarrisonContain *g = dynamic_cast<GarrisonContain *>(tower->getContain());
	REQUIRE(g);
	g->getEntryPosition(t.exitSpot);
	g->getExitOffset(t.exitOffset);
	HordeContainInterface *h = hordeOf(*archers);
	for (Object *m : membersOf(*archers))
	{
		t.ids.push_back(m->getID());
		t.slotBefore[m->getID()] = h->getMemberSlot(m);
		AIUpdateInterface *ai = m->getAIUpdateInterface();
		REQUIRE(ai);
		t.speed[m->getID()] = ai->curLocomotorSpeed();
	}
	a.send(0, MSG_EVACUATE, { tower->getID() }, {});
	for (int f = 0; f <= frames; ++f)
	{
		if (f > 0)
		{
			a.run(1);
		}
		std::map<ObjectID, Coord3D> pos;
		for (ObjectID id : t.ids)
		{
			if (Object *m = a.logic.findObjectByID(id))
			{
				pos[id] = *m->getPosition();
			}
		}
		t.frames.push_back(pos);
		t.horde.push_back(*archers->getPosition());
		if (t.exitFrame < 0 && !archers->getContainedBy())
		{
			t.exitFrame = f;
		}
	}
	if (HordeContain *hc = dynamic_cast<HordeContain *>(archers->getContain()))
	{
		t.worstSlotError = hc->worstMemberSlotError();
		MESSAGE("members rejoined unplaced " << hc->exitStats().membersRejoined << ", returns to formation " << hc->exitStats().returnsToFormation);
	}
	MESSAGE("exit paths " << g->exitStats().exitPaths << ", cell retries " << g->exitStats().exitCellRetries << ", horde regroups " << g->exitStats().hordeRegroups);
	t.hash = a.logic.computeStateHash();
	return t;
}

std::string describe(const ExitTrace &t, int frame)
{
	std::ostringstream s;
	s << "frame " << frame << ": horde (" << t.horde[(size_t)frame].x << ", " << t.horde[(size_t)frame].y << ")";
	for (ObjectID id : t.ids)
	{
		const auto it = t.frames[(size_t)frame].find(id);
		if (it != t.frames[(size_t)frame].end())
		{
			s << " [" << id << "] (" << it->second.x << ", " << it->second.y << ")";
		}
	}
	return s.str();
}
} // namespace

namespace
{
// the checks shared by the towers: the common exit point, no teleport, the spread and the walk away from the tower along its facing
void checkExit(const ExitTrace &t, const Coord3D &towerPos, float towerAngle, int frames)
{
	REQUIRE(t.exitFrame >= 0);
	REQUIRE_FALSE(t.ids.empty());
	for (int f : { 0, t.exitFrame, t.exitFrame + 1, t.exitFrame + 2, t.exitFrame + 4, t.exitFrame + 10, t.exitFrame + 30, frames })
	{
		if (f <= frames)
		{
			MESSAGE(describe(t, f));
		}
	}
	// the exit frame: every member stands on the contain's EntryPosition (the tower's base), the members are not on their slots yet
	const std::map<ObjectID, Coord3D> &out = t.frames[(size_t)t.exitFrame];
	for (ObjectID id : t.ids)
	{
		REQUIRE(out.count(id));
		CHECK_MESSAGE(dist2d(out.at(id), t.exitSpot) < 0.01f, "member " << id << " came out at (" << out.at(id).x << ", " << out.at(id).y << "), not at the exit");
	}
	// no teleport: from the exit frame on, no member moves farther in a frame than twice its locomotor speed
	float worst = 0.0f;
	for (int f = t.exitFrame + 1; f < (int)t.frames.size(); ++f)
	{
		for (ObjectID id : t.ids)
		{
			const auto a0 = t.frames[(size_t)f - 1].find(id), a1 = t.frames[(size_t)f].find(id);
			if (a0 == t.frames[(size_t)f - 1].end() || a1 == t.frames[(size_t)f].end())
			{
				continue;
			}
			const float step = dist2d(a0->second, a1->second);
			const float limit = 2.0f * t.speed.at(id) + 0.01f;
			worst = std::max(worst, step / limit);
			CHECK_MESSAGE(step <= limit, "member " << id << " jumped " << step << " on frame " << f << " (speed " << t.speed.at(id) << " per frame)");
		}
	}
	MESSAGE("the largest step over the limit ratio: " << worst);
	// the members spread out from the exit point; the horde ends in front of the tower (the ExitOffset adjusted for its locomotor, RW 0x6F3C87)
	float spread = 0.0f;
	for (ObjectID id : t.ids)
	{
		spread = std::max(spread, dist2d(t.frames.back().at(id), t.exitSpot));
	}
	CHECK(spread > 10.0f);
	const double fx = std::cos((double)towerAngle), fy = std::sin((double)towerAngle);
	const Coord3D &h = t.horde.back();
	CHECK(((double)h.x - towerPos.x) * fx + ((double)h.y - towerPos.y) * fy > 40.0);
	// every member ends near its slot (the formation formed outside)
	CHECK_MESSAGE(t.worstSlotError < 5.0f, "a member is " << t.worstSlotError << " from its slot");
}
} // namespace

TEST_CASE("garrison exit: archers leave a Gondor battle tower through its exit, one common point, then walk to their slots (G6, RW 0x87D3CA / 0x87CA2B / 0x8759FF)")
{
	if (!haveWorld())
	{
		return;
	}
	Arena a(shared());
	const ExitTrace t = garrisonAndExit(a, "GondorKeep", 0.0f, 60);
	checkExit(t, Coord3D{ 600.0f, 600.0f, 0.0f }, 0.0f, 60);
	CHECK(t.exitSpot.x == doctest::Approx(600.0f)); // GondorKeep: EntryPosition X:0 Y:0
	// the stop of the lane reaches the game's report
	const GameLogic::Report rep = a.logic.report();
	bool found = false;
	for (const std::string &line : rep.stops)
	{
		found = found || line.rfind("[S-1620]", 0) == 0;
	}
	CHECK(found);
}

TEST_CASE("garrison exit: the fortress's garrison tower expansion lets its archers out at its EntryPosition (X:35), turned with the building")
{
	if (!haveWorld())
	{
		return;
	}
	Arena a(shared());
	const float angle = 1.2f;
	const ExitTrace t = garrisonAndExit(a, "MenGarrisonTowerExpansion", angle, 60);
	checkExit(t, Coord3D{ 600.0f, 600.0f, 0.0f }, angle, 60);
	CHECK(dist2d(t.exitSpot, Coord3D{ 600.0f, 600.0f, 0.0f }) == doctest::Approx(35.0f).epsilon(0.001));
}

TEST_CASE("garrison exit: two exits give the same state hash")
{
	if (!haveWorld())
	{
		return;
	}
	std::uint32_t hashes[2] = { 0, 0 };
	for (int i = 0; i < 2; ++i)
	{
		Arena a(shared());
		hashes[i] = garrisonAndExit(a, "GondorKeep", 0.7f, 40).hash;
	}
	CHECK(hashes[0] != 0u);
	CHECK(hashes[0] == hashes[1]);
}

TEST_CASE("garrison exit: a horde leaving a transport (HordeTransportContain, RW 0x87A89E / 0x87A6D3) rejoins its members unplaced and they walk to their slots")
{
	if (!haveWorld())
	{
		return;
	}
	Arena a(shared());
	Object *boat = a.place("EGH_SlowTransport", 0, 600.0f, 600.0f);
	Object *horde = a.place("GondorFighterHorde", 0, 800.0f, 600.0f);
	a.run(20);
	REQUIRE(horde->getAIUpdateInterface()->aiEnter(boat, CMD_FROM_SCRIPT));
	for (int f = 0; f < 400 && !(horde->getContainedBy() == boat && hordeOf(*horde)->allMembersEntered()); ++f)
	{
		a.run(1);
	}
	REQUIRE(horde->getContainedBy() == boat);
	REQUIRE(hordeOf(*horde)->allMembersEntered());
	a.run(5);
	std::vector<ObjectID> ids;
	std::map<ObjectID, float> speed;
	for (Object *m : membersOf(*horde))
	{
		ids.push_back(m->getID());
		speed[m->getID()] = m->getAIUpdateInterface()->curLocomotorSpeed();
	}
	REQUIRE_FALSE(ids.empty());
	boat->getContain()->orderAllPassengersToExit((int)CMD_FROM_SCRIPT);
	std::map<ObjectID, Coord3D> last;
	bool out = false;
	float worst = 0.0f;
	for (int f = 0; f < 60; ++f)
	{
		a.run(1);
		for (ObjectID id : ids)
		{
			Object *m = a.logic.findObjectByID(id);
			REQUIRE(m);
			if (!m->isInWorld())
			{
				continue;
			}
			const Coord3D p = *m->getPosition();
			const auto it = last.find(id);
			if (it != last.end())
			{
				const float step = dist2d(it->second, p);
				const float limit = 2.0f * speed[id] + 0.01f;
				worst = std::max(worst, step / limit);
				CHECK_MESSAGE(step <= limit, "member " << id << " jumped " << step << " on frame " << f);
			}
			last[id] = p;
		}
		out = out || !horde->getContainedBy();
	}
	MESSAGE("transport exit: the largest step over the limit ratio " << worst);
	CHECK(out);
	CHECK(membersOf(*horde).size() == ids.size());
	HordeContain *hc = dynamic_cast<HordeContain *>(horde->getContain());
	REQUIRE(hc);
	CHECK(hc->exitStats().returnsToFormation >= 1u);
	CHECK(hc->exitStats().membersRejoined >= 2u * ids.size()); // in (RW 0x990DEA-like rejoin aboard) and out
	MESSAGE("members rejoined unplaced " << hc->exitStats().membersRejoined << ", worst slot error " << hc->worstMemberSlotError());
	CHECK(hc->worstMemberSlotError() < 5.0f);
}

TEST_CASE("garrison exit: no member is lost on the way into or out of a tower (the contain list plus the members on the way stay the whole horde)")
{
	if (!haveWorld())
	{
		return;
	}
	Arena a(shared());
	Object *tower = a.place("GondorKeep", 0, 600.0f, 600.0f);
	Object *archers = a.place("GondorArcherHorde", 0, 450.0f, 600.0f);
	a.run(20);
	const size_t whole = membersOf(*archers).size();
	REQUIRE(whole > 0u);
	HordeContainInterface *h = hordeOf(*archers);
	a.send(0, MSG_ENTER, { archers->getID() }, { archers->getID(), tower->getID() });
	int lost = 0;
	for (int f = 0; f < 300; ++f)
	{
		a.run(1);
		const size_t n = membersOf(*archers).size() + h->membersEntering().size();
		if (n != whole)
		{
			++lost;
			if (lost < 5)
			{
				MESSAGE("frame " << f << ": " << membersOf(*archers).size() << " listed + " << h->membersEntering().size() << " on the way != " << whole);
			}
		}
	}
	CHECK(lost == 0);
	a.send(0, MSG_EVACUATE, { tower->getID() }, {});
	for (int f = 0; f < 60; ++f)
	{
		a.run(1);
		CHECK(membersOf(*archers).size() + h->membersEntering().size() == whole);
	}
}
