// OpenBFME unit tests. GPL-3.0.
// Lane RAI-DEF: the skirmish AI player info record's position sampler (record + 0x10, RW 0x8E4042 / 0x8E4456 / 0x8E4475 / 0x8E46A5, fed by the
// record's Register RW 0x8E4591 and filtered by RW 0x8E3EF8 against the army definition's OffensiveBuildings). The DEFENSIVE heuristic that reads
// it is the stacked PR's (RW 0x97D74F). A retail 1 v 1 on Tournament Udun: an idle human Men player at start 0, a Medium computer Men player at
// start 1, so the AI's ArmyDefinition is MenArmy (OffensiveBuildings: GondorBattleTower, GondorStatue, GondorWell).
//
// The expectations below follow the RW algorithm, recomputed here from the map's objects and DefenseTreeNodeRadius, never read back from the port.

#include "HordeCompleteTestUtil.h"
#include "doctest.h"

#include "PeImage.h"
#include "RetailTestMount.h"
#include "StartTestUtil.h"

#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/RandomValue.h"
#include "Common/StateHash.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "GameClient/GUI/Skirmish/IniSkirmishSetupSource.h"
#include "GameClient/LiveGame.h"
#include "GameLogic/Map/TerrainLogic.h"
#include "GameLogic/NewGame/NewGame.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SkirmishAI/AIStatCollector.h"
#include "GameLogic/SkirmishAI/AITacticalAI.h"
#include "GameLogic/SkirmishAI/SkirmishAIData.h"
#include "GameLogic/SkirmishAI/SkirmishAIManager.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace
{
// the target of the rel32 call at `va` (0 when it is no call)
std::uint32_t callTarget(const retailtest::PeImage &pe, std::uint32_t va)
{
	std::vector<std::uint8_t> d;
	if (!pe.read(va, 5, &d) || d[0] != 0xE8)
	{
		return 0;
	}
	return va + 5u + pe.u32At(va + 1);
}

int factionIndex(const PlayerTemplateStore &store, const std::string &name)
{
	for (int i = 0; i < store.getPlayerTemplateCount(); ++i)
	{
		if (store.getNthPlayerTemplate(i)->getName() == name)
		{
			return i;
		}
	}
	return -1;
}

struct RecordsGame
{
	std::unique_ptr<NewGameStart> start;
	std::unique_ptr<ArchiveW3DFileSource> source;
	std::unique_ptr<WW3DAssetManager> assets;
	std::unique_ptr<LiveGame> game;
	Player *human = nullptr;
	Player *computer = nullptr;
	AISkirmishPlayer *ai = nullptr;
};

void loadUdun(starttest::Shared &s, RecordsGame &g)
{
	static std::vector<MapCacheEntry> cache;
	std::string error;
	if (cache.empty())
	{
		REQUIRE_MESSAGE(IniSkirmishSetupSource::loadMapCache(*s.mount->fs, cache, &error), error);
	}
	NewGameMessage m;
	m.game.mapName = "maps/map mp tournament udun/map mp tournament udun.map";
	m.game.seed = 1234;
	m.game.startingCash = 1500;
	const char *factions[2] = { "FactionMordor", "FactionMen" };
	for (int i = 0; i < 2; ++i)
	{
		SkirmishGameSlot &slot = m.game.slots[i];
		slot.state = i == 0 ? SLOT_PLAYER : SLOT_MED_AI;
		slot.name = i == 0 ? u"Human" : u"Computer";
		slot.playerTemplate = factionIndex(s.world->playerTemplates(), factions[i]);
		REQUIRE(slot.playerTemplate >= 0); // tolerance: a precondition (the faction exists; its index is the store's)
		slot.startPos = i;
		slot.color = i;
		slot.teamNumber = i;
	}
	g.start = std::make_unique<NewGameStart>(RandomAlgorithm::ZH_CarryChain);
	REQUIRE_MESSAGE(NewGame::prepareNewGame(m, s.world->playerTemplates(), s.settings, cache, RandomAlgorithm::ZH_CarryChain, *g.start, &error), error);
	g.source = std::make_unique<ArchiveW3DFileSource>(*s.mount->fs);
	g.assets = std::make_unique<WW3DAssetManager>(*g.source);
	g.game = std::make_unique<LiveGame>(*s.world, *s.mount->fs, *g.assets, s.options);
	LiveGame::Options o;
	o.start = g.start.get();
	REQUIRE_MESSAGE(g.game->load(o, &error), error);
	GameLogic &logic = g.game->logic();
	// the skirmish AI's manager starts its updates at logic frame 10 (RW 0x6A96A0's cadence): advance past it so the live record's deferred
	// sampler registrations are flushed before the test reads them
	for (int i = 0; i < 12; ++i)
	{
		g.game->advance(0.2);
	}
	for (int i = 0; i < logic.players().getPlayerCount(); ++i)
	{
		Player *p = logic.players().getNthPlayer(i);
		if (!p || !p->getPlayerTemplate() || !p->getPlayerTemplate()->m_playableSide)
		{
			continue;
		}
		if (!g.human && p->getMultiplayerStartIndex() == 0)
		{
			g.human = p;
		}
		if (!g.computer && p->getMultiplayerStartIndex() == 1)
		{
			g.computer = p;
		}
	}
	REQUIRE(g.human != nullptr);
	REQUIRE(g.computer != nullptr);
	for (const auto &grp : logic.skirmishAI().groups())
	{
		auto it = grp->players.find(g.computer->getPlayerIndex());
		if (it != grp->players.end())
		{
			g.ai = it->second.get();
		}
	}
	REQUIRE(g.ai != nullptr);
	REQUIRE(g.ai->store != nullptr);
}

Object *makeBuilding(GameLogic &logic, const char *name, Player &owner, const Coord3D &at)
{
	const ThingTemplate *tt = logic.things().findTemplate(name);
	REQUIRE_MESSAGE(tt, name);
	Object *o = logic.newObject(tt, owner.getDefaultTeam(), ObjectStatusMaskType{});
	REQUIRE_MESSAGE(o, name);
	Coord3D pos{ at.x, at.y, logic.getGroundHeight(at.x, at.y) };
	o->setPosition(&pos);
	return o;
}

// the group holding `id` (nullptr when none)
const AIPositionSampler::Group *groupOf(const AIPositionSampler &s, ObjectID id)
{
	for (const AIPositionSampler::Group &g : s.groups())
	{
		for (ObjectID member : g.members)
		{
			if (member == id)
			{
				return &g;
			}
		}
	}
	return nullptr;
}
} // namespace

TEST_CASE("rai-def binary facts: the player info record's position sampler (RW 0x8E4042 / 0x8E42C8 / 0x8E4456 / 0x8E4475 / 0x8E3EF8)")
{
	const retailtest::PeImage *pe = retailtest::PeImage::fromEnvironment();
	if (!pe)
	{
		retailtest::printSkip("rai-def binary facts (RW_GAME_DAT unset)");
		return;
	}
	// Register RW 0x8E4591: the structure / economy adds, then RW 0x8E3EF8 and the sampler add RW 0x8E4456
	CHECK(callTarget(*pe, 0x8E45E3) == 0x8E3EF8u);
	CHECK(callTarget(*pe, 0x8E45F0) == 0x8E4456u);
	// UnRegister: the sampler's remove RW 0x8E46A5 and its rebuild RW 0x8E4475
	CHECK(callTarget(*pe, 0x8E46CE) == 0x8E4475u);
	CHECK(callTarget(*pe, 0x8E4490) == 0x8E3EE9u); // the group destructor
	// the sampler object: vtable RW 0xC78294, the padding at + 0x1C and the manager at + 0x20
	CHECK(pe->hex(0x8E404F, 6) == "c7069482c700");
	CHECK(pe->hex(0x8E4069, 5) == "f30f104508");   // movss xmm0, [ebp + 8] (the padding)
	CHECK(pe->hex(0x8E406E, 3) == "894620");       // the manager stored at + 0x20
	// insert RW 0x8E42C8: a new group of 0x28 bytes, its ctor RW 0x68223E, the membership test RW 0x8E41EB and the append RW 0x8E4082
	CHECK(callTarget(*pe, 0x8E42F8) == 0x8E41EBu);
	CHECK(callTarget(*pe, 0x8E4322) == 0x68223Eu);
	CHECK(callTarget(*pe, 0x8E433A) == 0x8E4082u);
	CHECK(callTarget(*pe, 0x8E4397) == 0x8E4082u);
	CHECK(pe->hex(0x8E4303, 2) == "6a28");
	// RW 0x8E3EF8: the army definition at AI + 0x160, its OffensiveBuildings vector + 0x98 / + 0x9C and the template's name key + 0x64
	CHECK(pe->hex(0x8E3F0E, 6) == "8b8760010000");
	CHECK(pe->hex(0x8E3F14, 6) == "8b889c000000");
	CHECK(pe->hex(0x8E3F1A, 5) == "0598000000");
	CHECK(pe->hex(0x8E3F2C, 3) == "8b4904");
	CHECK(pe->hex(0x8E3F31, 3) == "83c164");
	CHECK(callTarget(*pe, 0x8E3F38) == 0x4065AAu);
	// RW 0x8E3DD6's membership bound: 2 * padding^2 (RW 0xBD889C = 2.0f)
	CHECK(pe->hex(0x8E3E53, 8) == "f30f591d9c88bd00");
}

TEST_CASE("rai-def position sampler: two structures within sqrt(2) * DefenseTreeNodeRadius share a group; a farther one does not (RW 0x8E42C8)")
{
	OPENBFME_REQUIRE_START(s);
	RecordsGame g;
	loadUdun(*s, g);
	GameLogic &logic = g.game->logic();
	REQUIRE(g.ai->army != nullptr);
	AIStatCollector record(g.computer->getPlayerIndex());
	const float padding = g.ai->store->data().defenseTreeNodeRadius;
	REQUIRE(padding == 200.0f); // the MenArmy / SkirmishAIData default, exact here
	const Coord3D at{ 1800.0f, 1400.0f, logic.getGroundHeight(1800.0f, 1400.0f) };

	Object *a = makeBuilding(logic, "GondorBarracks", *g.computer, at);
	CHECK(record.registerObject(*a) == AIStatCollector::LISTEN_STRUCTURES);
	record.update(logic); // the sampler add is deferred until the object's position is final
	REQUIRE(record.positions().members().size() == 1u);
	CHECK(record.positions().members()[0] == a->getID());
	REQUIRE(record.positions().groups().size() == 1u);
	CHECK(record.positions().groups()[0].radius == padding);
	CHECK(record.positions().groups()[0].center.x == a->getPosition()->x);
	CHECK(record.positions().groups()[0].center.y == a->getPosition()->y);
	CHECK(record.positions().groups()[0].center.z == a->getPosition()->z);
	CHECK(record.positions().groups()[0].members == std::vector<ObjectID>{ a->getID() });

	// 100 to +x: inside sqrt(2) * 200 = 282.8, so it joins the group; the sphere becomes the midpoint with radius padding + 50
	const Coord3D bAt{ at.x + 100.0f, at.y, at.z };
	Object *b = makeBuilding(logic, "GondorFarm", *g.computer, bAt);
	CHECK(record.registerObject(*b) == AIStatCollector::LISTEN_ECONOMY);
	record.update(logic);
	REQUIRE(record.positions().groups().size() == 1u);
	const AIPositionSampler::Group &ab = record.positions().groups()[0];
	CHECK(ab.radius == 250.0f);
	CHECK(ab.center.x == at.x + 50.0f);
	CHECK(ab.center.y == at.y);
	CHECK(ab.center.z == at.z);
	CHECK(ab.members == (std::vector<ObjectID>{ a->getID(), b->getID() }));

	// 400 from a (300 from b): outside the membership bound, a second group of its own
	const Coord3D cAt{ at.x + 400.0f, at.y, at.z };
	Object *c = makeBuilding(logic, "GondorBarracks", *g.computer, cAt);
	CHECK(record.registerObject(*c) == AIStatCollector::LISTEN_STRUCTURES);
	record.update(logic);
	REQUIRE(record.positions().groups().size() == 2u);
	CHECK(record.positions().groups()[1].radius == padding);
	CHECK(record.positions().groups()[1].center.x == cAt.x);
	CHECK(record.positions().groups()[1].members == std::vector<ObjectID>{ c->getID() });

	// 100 from c: appends to c's group (the first selected group is 0 only when it matches; group 1 here)
	const Coord3D dAt{ cAt.x + 100.0f, at.y, at.z };
	Object *d = makeBuilding(logic, "GondorFarm", *g.computer, dAt);
	CHECK(record.registerObject(*d) == AIStatCollector::LISTEN_ECONOMY);
	record.update(logic);
	REQUIRE(record.positions().groups().size() == 2u);
	CHECK(record.positions().groups()[1].radius == 250.0f);
	CHECK(record.positions().groups()[1].center.x == cAt.x + 50.0f);
	CHECK(record.positions().groups()[1].members == (std::vector<ObjectID>{ c->getID(), d->getID() }));

	// RW 0x8E46A5: removing a also destroys and rebuilds every group from the survivors
	CHECK(record.unRegisterObject(*a) == AIStatCollector::LISTEN_STRUCTURES);
	REQUIRE(record.positions().groups().size() == 2u);
	CHECK(record.positions().groups()[0].members == std::vector<ObjectID>{ b->getID() });
	CHECK(record.positions().groups()[0].radius == padding);
	CHECK(record.positions().groups()[1].members == (std::vector<ObjectID>{ c->getID(), d->getID() }));
	CHECK(groupOf(record.positions(), a->getID()) == nullptr);
}

TEST_CASE("rai-def position sampler: an army OffensiveBuilding stays out of the sampler (RW 0x8E3EF8); the xfer keeps the groups (record version 3)")
{
	OPENBFME_REQUIRE_START(s);
	RecordsGame g;
	loadUdun(*s, g);
	GameLogic &logic = g.game->logic();
	AIStatCollector record(g.computer->getPlayerIndex());
	const float padding = g.ai->store->data().defenseTreeNodeRadius;
	const Coord3D at{ 2100.0f, 1600.0f, logic.getGroundHeight(2100.0f, 1600.0f) };
	Object *a = makeBuilding(logic, "GondorBarracks", *g.computer, at);
	CHECK(record.registerObject(*a) == AIStatCollector::LISTEN_STRUCTURES);
	record.update(logic); // the sampler add is deferred until the object's position is final
	REQUIRE(record.positions().members().size() == 1u);

	// GondorBattleTower is in MenArmy's OffensiveBuildings: the structure list takes it, the sampler does not
	REQUIRE_MESSAGE(std::find(g.ai->army->offensiveBuildings.begin(), g.ai->army->offensiveBuildings.end(), "GondorBattleTower") != g.ai->army->offensiveBuildings.end(),
		"MenArmy's OffensiveBuildings");
	Object *tower = makeBuilding(logic, "GondorBattleTower", *g.computer, Coord3D{ at.x + 50.0f, at.y, at.z });
	CHECK(record.registerObject(*tower) == AIStatCollector::LISTEN_STRUCTURES);
	record.update(logic);
	CHECK(record.positions().members().size() == 1u);
	CHECK(groupOf(record.positions(), tower->getID()) == nullptr);
	CHECK(record.positions().groups().size() == 1u);

	// the real record of the AI took the run's starting structures too (the manager's world hooks, RW 0x6A9D9C -> 0x8E4591)
	const AIStatCollector &live = AITacticalAI::recordOf(logic, g.computer->getPlayerIndex());
	CHECK_FALSE(live.positions().groups().empty());
	for (const AIPositionSampler::Group &grp : live.positions().groups())
	{
		CHECK_FALSE(grp.members.empty());
		// tolerance: a group's radius grows with its members; the padding is its floor (RW 0x8E4082)
		CHECK(grp.radius >= padding);
	}
	CHECK(groupOf(live.positions(), tower->getID()) == nullptr); // the real record skipped it too

	// Xfer version 3: the sampler round-trips (the groups, their centres, radii and members)
	XferSave save;
	record.xfer(save);
	AIStatCollector loaded(g.computer->getPlayerIndex());
	XferLoad load(save.data());
	loaded.xfer(load);
	REQUIRE(loaded.positions().groups().size() == record.positions().groups().size());
	for (size_t i = 0; i < record.positions().groups().size(); ++i)
	{
		const AIPositionSampler::Group &x = record.positions().groups()[i], &y = loaded.positions().groups()[i];
		CHECK(y.radius == x.radius);
		CHECK(y.center.x == x.center.x);
		CHECK(y.center.y == x.center.y);
		CHECK(y.center.z == x.center.z);
		CHECK(y.members == x.members);
	}
	CHECK(loaded.positions().members() == record.positions().members());
}
