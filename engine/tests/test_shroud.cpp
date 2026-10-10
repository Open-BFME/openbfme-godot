// OpenBFME unit tests. GPL-3.0.
// Lane VIS-1: TheShroudManager (GameLogic/System/ShroudManager.h). The expected cells come from the RW routines named in the header: the integer circle of RW
// 0xB50100 (hand-traced below), the looker counts of RW 0xB52E10 / 0xB52EC0, the unlook queue of RW 0xB526A0 / 0xB51690 and the retail data (PartitionCellSize
// 40, UnlookPersistDuration 1 frame, UseShroud No), never this engine's own output.

#include "CreepTestUtil.h"
#include "doctest.h"

#include "LogicTestUtil.h"
#include "StartTestUtil.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/StateHash.h"
#include "Common/Thing/ThingFactory.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/Object/ObjectGeometry.h"
#include "GameClient/GUI/Skirmish/IniSkirmishSetupSource.h"
#include "GameClient/LiveGame.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"
#include "GameLogic/AI/AICommandSink.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/Map/TerrainLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/NewGame/NewGame.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/System/ShroudManager.h"
#include "GameLogic/System/VisionSettings.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace logictest;

namespace
{
const char kObjects[] =
	"Object Scout\n"
	"  KindOf = INFANTRY SELECTABLE\n"
	"  ShroudClearingRange = 100\n"
	"  VisionRange = 100\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 5\n"
	"  GeometryHeight = 10\n"
	"End\n"
	"Object Egg\n"
	"  KindOf = INFANTRY\n"
	"  ShroudClearingRange = 200\n"
	"  VisionSide = 50%\n"
	"  VisionRear = 25%\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 5\n"
	"End\n"
	"Object Blind\n"
	"  KindOf = INFANTRY\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 5\n"
	"End\n"
	"Object Hiller\n"
	"  KindOf = INFANTRY\n"
	"  ShroudClearingRange = 150\n"
	"  VisionBonusPercentPerFoot = 1%\n"
	"  VisionBonusTestRadius = 30\n"
	"  VisionBonusTestSegments = 8\n"
	"  MaxVisionBonusPercent = 50%\n"
	"  MinVisionBonusPercent = -20%\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 5\n"
	"End\n"
	"Object BigScout\n"
	"  KindOf = INFANTRY\n"
	"  ShroudClearingRange = 100\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 90\n"
	"End\n"
	"Object Tower\n"
	"  KindOf = STRUCTURE IMMOBILE\n"
	"  ShroudClearingRange = 40\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 5\n"
	"End\n";

struct Fx : LogicWorld
{
	ShroudManager shroud;
	Fx()
		: shroud(*logic)
	{
		REQUIRE_MESSAGE(loadError.empty(), loadError);
		const std::string err = w.load(kObjects);
		REQUIRE_MESSAGE(err.empty(), err);
		shroud.init(40.0f, 1);
		shroud.setExtent(0.0f, 0.0f, 1000.0f, 800.0f);
		shroud.attach();
	}
	int idx(const char *name) { return players.findPlayerWithName(name)->getPlayerIndex(); }
	Object *place(const char *tmpl, const char *owner, float x, float y, float angle = 0.0f)
	{
		Object *o = make(tmpl, teamOf(owner));
		Coord3D p{ x, y, 0.0f };
		o->setPosition(&p);
		o->setOrientation(angle);
		return o;
	}
	std::set<std::pair<int, int>> clearCells(int player)
	{
		std::set<std::pair<int, int>> s;
		for (int y = 0; y < shroud.cellCountY(); ++y)
		{
			for (int x = 0; x < shroud.cellCountX(); ++x)
			{
				if (shroud.getCellStatus(player, x, y) == CELLSHROUD_CLEAR)
				{
					s.insert({ x, y });
				}
			}
		}
		return s;
	}
};

// RW 0xB50100 traced by hand for r = 2 at (10, 10): err = -2, dy = 2: err + dy = 0 is not > 0; err < x -> x = 1, columns 9..11, err = 1; err + dy = 3: rows 12
// and 8 get 9..11, dy = 1, err = 0; err < x -> x = 2, columns 8..12, err = 5; rows 11 and 9 get 8..12, dy = 0, err = 6; row 10 gets 8..12 and stops
const std::set<std::pair<int, int>> kCircle2 = { { 9, 12 }, { 10, 12 }, { 11, 12 }, { 9, 8 }, { 10, 8 }, { 11, 8 }, { 8, 11 }, { 9, 11 }, { 10, 11 }, { 11, 11 }, { 12, 11 },
	{ 8, 9 }, { 9, 9 }, { 10, 9 }, { 11, 9 }, { 12, 9 }, { 8, 10 }, { 9, 10 }, { 10, 10 }, { 11, 10 }, { 12, 10 } };
// r = 1: rows 11 / 9 get x 10; then dy = 0 and r == 1 widens to 9..11 on row 10 (a plus)
const std::set<std::pair<int, int>> kCircle1 = { { 10, 11 }, { 10, 9 }, { 9, 10 }, { 10, 10 }, { 11, 10 } };
} // namespace

TEST_CASE("shroud cells: the grid is ceil(extent / PartitionCellSize), every cell starts never seen, the looker counts follow RW 0xB52E10 / 0xB52EC0")
{
	Fx f;
	CHECK(f.shroud.cellCountX() == 25);
	CHECK(f.shroud.cellCountY() == 20);
	CHECK(f.shroud.getCellStatus(0, 3, 3) == CELLSHROUD_SHROUDED);
	CHECK(f.shroud.getCellStatus(0, -1, 3) == CELLSHROUD_SHROUDED);   // outside the grid
	CHECK(f.shroud.getCellStatus(25, 3, 3) == CELLSHROUD_SHROUDED);   // outside the 20 players
	ShroudManager::Radii r;
	r.forward = r.side = r.rear = 0;
	f.shroud.lookAt(3, 3, r, 0.0f, 1u << 0);
	CHECK(f.shroud.lookerCount(0, 3, 3) == 1);                        // -1 + 1 = 0 becomes 1
	f.shroud.lookAt(3, 3, r, 0.0f, 1u << 0);
	CHECK(f.shroud.lookerCount(0, 3, 3) == 2);
	f.shroud.unlookAt(3, 3, r, 0.0f, 1u << 0);
	f.shroud.unlookAt(3, 3, r, 0.0f, 1u << 0);
	CHECK(f.shroud.lookerCount(0, 3, 3) == 0);
	CHECK(f.shroud.getCellStatus(0, 3, 3) == CELLSHROUD_FOGGED);      // seen once, nobody looks
	CHECK(f.shroud.getCellStatus(1, 3, 3) == CELLSHROUD_SHROUDED);    // the other player never saw it
	// a narrower extent is widened to 1, the counts to at least one cell
	ShroudManager tiny(*f.logic);
	tiny.init(40.0f, 1);
	tiny.setExtent(0.0f, 0.0f, 0.5f, 0.0f);
	CHECK(tiny.cellCountX() == 1);
	CHECK(tiny.cellCountY() == 1);
}

TEST_CASE("shroud cells: the round look is RW 0xB50100's integer circle (hand-traced r = 1 and r = 2), clipped at the grid edge")
{
	Fx f;
	ShroudManager::Radii r;
	r.forward = r.side = r.rear = 2;
	f.shroud.lookAt(10, 10, r, 0.0f, 1u << 1);
	CHECK(f.clearCells(1) == kCircle2);
	f.shroud.unlookAt(10, 10, r, 0.0f, 1u << 1);
	CHECK(f.clearCells(1).empty());
	r.forward = r.side = r.rear = 1;
	f.shroud.lookAt(10, 10, r, 0.0f, 1u << 1);
	CHECK(f.clearCells(1) == kCircle1);
	f.shroud.unlookAt(10, 10, r, 0.0f, 1u << 1);
	// the corner: only the cells inside the grid
	r.forward = r.side = r.rear = 2;
	f.shroud.lookAt(0, 0, r, 0.0f, 1u << 1);
	const std::set<std::pair<int, int>> corner = { { 0, 2 }, { 1, 2 }, { 0, 1 }, { 1, 1 }, { 2, 1 }, { 0, 0 }, { 1, 0 }, { 2, 0 } };
	CHECK(f.clearCells(1) == corner);
}

TEST_CASE("shroud cells: an egg look (VisionSide / VisionRear) reaches the forward radius ahead of the facing and the rear radius behind it")
{
	Fx f;
	ShroudManager::Radii r;
	r.forward = 6;
	r.side = 3;
	r.rear = 2;
	f.shroud.lookAt(12, 10, r, 0.0f, 1u << 0); // facing +x
	const auto east = f.clearCells(0);
	CHECK(east.count({ 18, 10 }) == 1);  // forward radius ahead
	CHECK(east.count({ 19, 10 }) == 0);
	CHECK(east.count({ 10, 10 }) == 1);  // rear radius behind
	CHECK(east.count({ 9, 10 }) == 0);
	// the side radius across: the scan conversion is half-open (RW 0xB501A0 stops before drawing the row of the lowest vertex): the vertex row above is drawn,
	// the one below is not
	CHECK(east.count({ 12, 7 }) == 1);
	CHECK(east.count({ 12, 6 }) == 0);
	CHECK(east.count({ 12, 12 }) == 1);
	CHECK(east.count({ 12, 13 }) == 0);
	f.shroud.unlookAt(12, 10, r, 0.0f, 1u << 0);
	CHECK(f.clearCells(0).empty());      // the unlook removes exactly what the look added
	f.shroud.lookAt(12, 10, r, 1.5707964f, 1u << 0); // facing +y
	const auto north = f.clearCells(0);
	CHECK(north.count({ 12, 15 }) == 1); // the forward vertex is the lowest row (16): not drawn
	CHECK(north.count({ 12, 16 }) == 0);
	CHECK(north.count({ 12, 8 }) == 1);  // the rear vertex is the top row: drawn
	CHECK(north.count({ 12, 7 }) == 0);
}

TEST_CASE("shroud map: revealMap fogs every cell, the permanent reveal keeps it clear until undone, shroudMap returns fog to never seen (RW 0xB4F570 .. 0xB51A00)")
{
	Fx f;
	f.shroud.revealMapForPlayer(0);
	CHECK(f.shroud.getCellStatus(0, 24, 19) == CELLSHROUD_FOGGED);
	CHECK(f.shroud.getCellStatus(1, 24, 19) == CELLSHROUD_SHROUDED);
	f.shroud.revealMapForPlayerPermanently(1);
	CHECK(f.shroud.getCellStatus(1, 5, 5) == CELLSHROUD_CLEAR);
	f.shroud.undoRevealMapForPlayerPermanently(1);
	CHECK(f.shroud.getCellStatus(1, 5, 5) == CELLSHROUD_FOGGED);
	f.shroud.shroudMapForPlayer(1);
	CHECK(f.shroud.getCellStatus(1, 5, 5) == CELLSHROUD_SHROUDED);
	// UseShroud No: every slot player starts fogged; Yes: never seen
	ShroudManager a(*f.logic), b(*f.logic);
	a.init(40.0f, 1);
	b.init(40.0f, 1);
	a.setExtent(0.0f, 0.0f, 400.0f, 400.0f);
	b.setExtent(0.0f, 0.0f, 400.0f, 400.0f);
	a.applyNewGameShroud(false, { 0, 1 }, {});
	b.applyNewGameShroud(true, { 0, 1 }, { 2 });
	CHECK(a.getCellStatus(1, 9, 9) == CELLSHROUD_FOGGED);
	CHECK(b.getCellStatus(1, 9, 9) == CELLSHROUD_SHROUDED);
	CHECK(b.getCellStatus(2, 9, 9) == CELLSHROUD_CLEAR); // the observer
}

TEST_CASE("shroud objects: a unit looks for its owner (ceil(ShroudClearingRange / 40) cells), not for the enemy; its old look persists until the unlook queue is due")
{
	Fx f;
	const int alice = f.idx("Alice"), bob = f.idx("Bob");
	Object *scout = f.place("Scout", "Alice", 420.0f, 420.0f);
	f.shroud.update();
	REQUIRE(f.shroud.isRegistered(*scout));
	// 100 / 40 -> 3 cells around cell (10, 10)
	CHECK(f.shroud.getCellStatus(alice, 10, 10) == CELLSHROUD_CLEAR);
	CHECK(f.shroud.getCellStatus(alice, 13, 10) == CELLSHROUD_CLEAR);
	CHECK(f.shroud.getCellStatus(alice, 14, 10) == CELLSHROUD_SHROUDED);
	CHECK(f.shroud.getCellStatus(alice, 10, 7) == CELLSHROUD_CLEAR);
	CHECK(f.shroud.getCellStatus(bob, 10, 10) == CELLSHROUD_SHROUDED);
	// move 200 east: the new cells clear now; the old look is queued with due = counter + 1 and runs once the counter passes it (two updates later)
	Coord3D p{ 620.0f, 420.0f, 0.0f };
	scout->setPosition(&p);
	f.shroud.update();
	CHECK(f.shroud.getCellStatus(alice, 18, 10) == CELLSHROUD_CLEAR);
	CHECK(f.shroud.getCellStatus(alice, 8, 10) == CELLSHROUD_CLEAR); // still the old look
	CHECK(f.shroud.pendingUnlooks() == 1);
	f.shroud.update();
	CHECK(f.shroud.getCellStatus(alice, 8, 10) == CELLSHROUD_CLEAR);
	f.shroud.update();
	CHECK(f.shroud.getCellStatus(alice, 8, 10) == CELLSHROUD_FOGGED);
	CHECK(f.shroud.pendingUnlooks() == 0);
	// an object with no ShroudClearingRange does not look
	Object *blind = f.place("Blind", "Bob", 100.0f, 100.0f);
	f.shroud.update();
	CHECK(f.shroud.getCellStatus(bob, 2, 2) == CELLSHROUD_SHROUDED);
	(void)blind;
}

TEST_CASE("shroud objects: the status of an object for a player (RW 0xB4E890) and the fog rule of RW 0x68EDD0: a mobile enemy in fog is hidden, a seen building is not")
{
	Fx f;
	const int alice = f.idx("Alice");
	Object *scout = f.place("Scout", "Alice", 420.0f, 420.0f);
	Object *enemy = f.place("Blind", "Bob", 500.0f, 420.0f);  // cell (12, 10): inside the scout's look
	Object *tower = f.place("Tower", "Bob", 540.0f, 420.0f);  // cell (13, 10)
	f.shroud.update();
	CHECK(f.shroud.getObjectStatus(*enemy, alice) == OBJECTSHROUD_CLEAR);
	CHECK(f.shroud.getObjectStatus(*tower, alice) == OBJECTSHROUD_CLEAR);
	CHECK(f.shroud.hasSeen(*tower, alice));
	Coord3D away{ 100.0f, 100.0f, 0.0f };
	scout->setPosition(&away);
	for (int i = 0; i < 3; ++i)
	{
		f.shroud.update();
	}
	CHECK(f.shroud.getCellStatus(alice, 12, 10) == CELLSHROUD_FOGGED);
	CHECK(f.shroud.getObjectStatus(*enemy, alice) == OBJECTSHROUD_SHROUDED); // mobile: hidden in fog
	CHECK(f.shroud.getObjectStatus(*tower, alice) == OBJECTSHROUD_FOGGED);   // immobile and seen: drawn in fog
	// a unit leaving the world takes its look with it (queued), and its record
	f.logic->destroyObject(scout);
	f.logic->processDestroyList();
	CHECK_FALSE(f.shroud.isRegistered(*enemy) == false);
	f.shroud.update();
	f.shroud.update();
	CHECK(f.shroud.getCellStatus(alice, 2, 2) == CELLSHROUD_FOGGED);
}

TEST_CASE("shroud hash: two identical runs agree, one extra look moves the hash, the logic's state hash includes the shroud")
{
	auto run = [](bool extra) {
		Fx f;
		Object *s = f.place("Egg", "Alice", 300.0f, 300.0f, 0.5f);
		std::vector<std::uint32_t> hashes;
		for (int i = 0; i < 10; ++i)
		{
			Coord3D p{ 300.0f + 37.0f * (float)i, 300.0f + 11.0f * (float)i, 0.0f };
			s->setPosition(&p);
			s->setOrientation(0.5f + 0.3f * (float)i);
			if (extra && i == 5)
			{
				ShroudManager::Radii r;
				r.forward = r.side = r.rear = 1;
				f.shroud.lookAt(3, 3, r, 0.0f, 1u);
			}
			f.logic->runLogicFrame();
			hashes.push_back(f.logic->computeStateHash());
		}
		return hashes;
	};
	const auto a = run(false), b = run(false), c = run(true);
	CHECK(a == b);
	CHECK(a[4] == c[4]);
	CHECK(a[5] != c[5]);
}

TEST_CASE("shroud targeting: a human player's unit cannot attack an enemy that is fogged for it (ZH isObjectShroudedForAction, S-565)")
{
	Fx f;
	const int alice = f.idx("Alice");
	Object *enemy = f.place("Blind", "Bob", 500.0f, 420.0f);
	Object *scout = f.place("Scout", "Alice", 900.0f, 700.0f);
	f.shroud.update();
	CHECK(f.shroud.getObjectStatus(*enemy, alice) == OBJECTSHROUD_SHROUDED);
	CHECK(ShroudManager::isShroudedForAction(*scout, *enemy));
	Coord3D near{ 460.0f, 420.0f, 0.0f };
	scout->setPosition(&near);
	f.shroud.update();
	CHECK_FALSE(ShroudManager::isShroudedForAction(*scout, *enemy));
	// Bob is not human: his units are never blocked by the rule
	CHECK_FALSE(ShroudManager::isShroudedForAction(*enemy, *scout));
}

TEST_CASE("vision settings: the GameData and MultiplayerSettings keys parse with the binary's parsers; a missing key is an error")
{
	VisionSettings v;
	std::string error;
	REQUIRE_MESSAGE(VisionSettings::scan("GameData\n  PartitionCellSize = 40.0\n  UnlookPersistDuration = 1\n  ShroudColor = R:255 G:255 B:255\n  FogAlpha = 127\n"
										 "  ShroudAlpha = 0\n  StealthFriendlyOpacity = 50%\n  SomethingElse = 3\nEnd\n",
					   "MultiplayerSettings\n  UseShroud = No\n  StartCountdownTimer = 5\nEnd\n", v, &error),
		error);
	CHECK(v.partitionCellSize == 40.0f);
	CHECK(v.unlookPersistFrames == 1u); // ceil(1 * 0.005)
	CHECK(v.fogAlpha == 127u);
	CHECK(v.shroudAlpha == 0u);
	CHECK(v.clearAlpha == 255u);        // the constructor's value (RW 0x6432CC)
	CHECK(v.stealthFriendlyOpacity == doctest::Approx(0.5f));
	CHECK_FALSE(v.useShroud);
	CHECK_FALSE(VisionSettings::scan("GameData\n  PartitionCellSize = 40.0\nEnd\n", "MultiplayerSettings\n  UseShroud = No\nEnd\n", v, &error));
	CHECK(error.find("UnlookPersistDuration") != std::string::npos);
}

TEST_CASE("vision settings: the install's GameData and multiplayer.ini")
{
	retailtest::Mount *m = retailtest::pureMount();
	if (!m)
	{
		retailtest::printSkip("vision settings retail");
		return;
	}
	REQUIRE_MESSAGE(m->fs != nullptr, m->error);
	VisionSettings v;
	std::string error;
	REQUIRE_MESSAGE(VisionSettings::load(*m->fs, v, &error), error);
	CHECK(v.partitionCellSize == 40.0f);
	CHECK(v.unlookPersistFrames == 1u);
	CHECK(v.fogAlpha == 127u);
	CHECK(v.shroudAlpha == 0u);
	CHECK_FALSE(v.useShroud);
}

namespace
{
int templateIndex(const PlayerTemplateStore &store, const std::string &name)
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

NewGameMessage evendim(const starttest::Shared &s)
{
	NewGameMessage m;
	m.game.mapName = "maps/map mp evendim/map mp evendim.map";
	m.game.seed = 77;
	m.game.startingCash = 1500;
	SkirmishGameSlot &h = m.game.slots[0];
	h.state = SLOT_PLAYER;
	h.name = u"Human";
	h.playerTemplate = templateIndex(s.world->playerTemplates(), "FactionMen");
	h.startPos = 0;
	h.color = 0;
	h.teamNumber = 0;
	SkirmishGameSlot &c = m.game.slots[1];
	c.state = SLOT_EASY_AI;
	c.name = u"Computer";
	c.playerTemplate = templateIndex(s.world->playerTemplates(), "FactionMordor");
	c.startPos = 1;
	c.color = 1;
	c.teamNumber = 1;
	return m;
}

struct RetailRun
{
	std::vector<std::uint32_t> hashes;
	bool startFogged = false, startOwnBaseClear = false, enemyBaseShrouded = false;
	bool walkedAxisClear = false, walkedBeyondNotClear = false, oldCellsFogged = false;
	bool targetHiddenBlocked = false, targetVisibleAllowed = false;
	int radiusCells = -1;
};

void runRetail(starttest::Shared &s, RetailRun &out, bool checks)
{
	static std::vector<MapCacheEntry> cache;
	std::string error;
	if (cache.empty())
	{
		REQUIRE_MESSAGE(IniSkirmishSetupSource::loadMapCache(*s.mount->fs, cache, &error), error);
	}
	NewGameStart start(RandomAlgorithm::ZH_CarryChain);
	REQUIRE_MESSAGE(NewGame::prepareNewGame(evendim(s), s.world->playerTemplates(), s.settings, cache, RandomAlgorithm::ZH_CarryChain, start, &error), error);
	ArchiveW3DFileSource source(*s.mount->fs);
	WW3DAssetManager assets(source);
	LiveGame game(*s.world, *s.mount->fs, assets, s.options);
	LiveGame::Options o;
	o.start = &start;
	REQUIRE_MESSAGE(game.load(o, &error), error);
	GameLogic &logic = game.logic();
	ShroudManager &sm = game.shroud();
	CHECK(sm.cellSize() == 40.0f);
	const Player *human = logic.players().getLocalPlayer();
	REQUIRE(human != nullptr);
	const int me = human->getPlayerIndex();
	const Player *enemy = nullptr;
	for (int i = 0; i < logic.players().getPlayerCount(); ++i)
	{
		const Player *p = logic.players().getNthPlayer(i);
		if (p && p != human && p->getMultiplayerStartIndex() == 1)
		{
			enemy = p;
		}
	}
	REQUIRE(enemy != nullptr);
	const Coord3D home = logic.terrain()->findWaypointByName("Player_1_Start")->location;
	const Coord3D away = logic.terrain()->findWaypointByName("Player_2_Start")->location;
	int hx, hy, ax, ay;
	REQUIRE(sm.worldToCell(home.x, home.y, hx, hy));
	REQUIRE(sm.worldToCell(away.x, away.y, ax, ay));
	// UseShroud No: the map is explored (fog) from the start, the own base is clear
	out.startFogged = sm.getCellStatus(me, 0, 0) == CELLSHROUD_FOGGED;
	out.startOwnBaseClear = sm.getCellStatus(me, hx, hy) == CELLSHROUD_CLEAR;
	out.enemyBaseShrouded = sm.getCellStatus(me, ax, ay) != CELLSHROUD_CLEAR;
	// a mobile unit of the human with a positive ShroudClearingRange walks toward the map centre
	Object *walker = nullptr;
	for (Object *x = logic.getFirstObject(); x; x = x->getNextObject())
	{
		if (x->getControllingPlayer() == human && x->getAIUpdateInterface() && !x->isKindOfName("STRUCTURE") && !x->isKindOfName("HORDE"))
		{
			walker = x;
			break;
		}
	}
	REQUIRE(walker != nullptr);
	Object *target = nullptr;
	for (Object *x = logic.getFirstObject(); x; x = x->getNextObject())
	{
		if (x->getControllingPlayer() == enemy && !x->isKindOfName("STRUCTURE") && x->getAIUpdateInterface())
		{
			target = x;
			break;
		}
	}
	float maxX = 0.0f, maxY = 0.0f;
	logic.terrain()->getExtent(0, maxX, maxY);
	Coord3D goal{ maxX * 0.5f, maxY * 0.5f, 0.0f };
	// lane MOD-4: the map's creep lairs spawn and guard their creeps now (SpawnBehavior / SlavedUpdate); Evendim's Barrow Wights guard the centre this walk goes to:
	// the lairs stop spawning and their creeps are removed so the walk measures the shroud only
	creeptest::removeCreeps(logic);
	walker->getAIUpdateInterface()->aiMoveToPosition(goal, CMD_FROM_PLAYER);
	const ObjectID walkerId = walker->getID();
	for (int i = 0; i < 300; ++i)
	{
		game.advance(0.2);
		out.hashes.push_back(logic.computeStateHash());
	}
	walker = logic.findObjectByID(walkerId);
	REQUIRE_MESSAGE(walker != nullptr, "the walker is gone");
	if (!checks)
	{
		return;
	}
	int wx, wy;
	REQUIRE(sm.worldToCell(walker->getPosition()->x, walker->getPosition()->y, wx, wy));
	ShroudManager::LookInputs in;
	ShroudManager::retailObjectSource().look(*walker, ShroudManager::templateVision(*walker->getTemplate()->getFinalOverride()), in);
	REQUIRE(in.forward > 0.0f);
	out.radiusCells = (int)std::ceil(in.forward / 40.0f);
	const int r = out.radiusCells;
	out.walkedAxisClear = sm.getCellStatus(me, wx + r, wy) == CELLSHROUD_CLEAR && sm.getCellStatus(me, wx - r, wy) == CELLSHROUD_CLEAR &&
		sm.getCellStatus(me, wx, wy + r) == CELLSHROUD_CLEAR && sm.getCellStatus(me, wx, wy - r) == CELLSHROUD_CLEAR;
	// beyond the radius only other lookers could clear a cell: far from the base it is fogged
	const int fx = wx + r + 2;
	out.walkedBeyondNotClear = (std::abs(fx - hx) <= 12 && std::abs(wy - hy) <= 12) || sm.getCellStatus(me, fx, wy) == CELLSHROUD_FOGGED;
	// a Gondor soldier of the human next to the walker, a Mordor soldier of the enemy in the fog far from both bases
	const ThingTemplate *ft = s.world->things().findTemplate("GondorFighter");
	const ThingTemplate *et = s.world->things().findTemplate("MordorFighter");
	REQUIRE(ft != nullptr);
	REQUIRE(et != nullptr);
	Object *fighter = logic.newObject(ft, human->getDefaultTeam(), ObjectStatusMaskType{});
	fighter->setPosition(walker->getPosition());
	target = logic.newObject(et, enemy->getDefaultTeam(), ObjectStatusMaskType{});
	Coord3D fog{ walker->getPosition()->x, walker->getPosition()->y, 0.0f };
	fog.y = fog.y > maxY * 0.5f ? fog.y - 900.0f : fog.y + 900.0f;
	target->setPosition(&fog);
	game.advance(0.2);
	walker = fighter;
	if (target)
	{
		ObjectWeapons *w = walker->getWeapons();
		REQUIRE(w != nullptr);
		{
			const ObjectShroudStatus st = sm.getObjectStatus(*target, me);
			MESSAGE("fogged target status: " << (int)st);
			if (st >= OBJECTSHROUD_FOGGED)
			{
				out.targetHiddenBlocked = !w->canAttackObject(*target, CMD_FROM_PLAYER, false) && ShroudManager::isShroudedForAction(*walker, *target);
			}
			// bring the target next to the walker: visible, the order is allowed
			Coord3D p = *walker->getPosition();
			p.x += 30.0f;
			target->setPosition(&p);
			game.advance(0.2);
			sm.update();
			out.targetVisibleAllowed = sm.getObjectStatus(*target, me) <= OBJECTSHROUD_PARTIAL_CLEAR && !ShroudManager::isShroudedForAction(*walker, *target) &&
				w->canAttackObject(*target, CMD_FROM_PLAYER, false);
		}
	}
}
} // namespace

TEST_CASE("shroud retail: an Evendim skirmish starts explored (UseShroud No), a walking unit clears ceil(range / 40) cells, a fogged enemy is no order target")
{
	OPENBFME_REQUIRE_START(s);
	RetailRun run;
	runRetail(*s, run, true);
	CHECK(run.startFogged);
	CHECK(run.startOwnBaseClear);
	CHECK(run.enemyBaseShrouded);
	CHECK(run.radiusCells > 0);
	CHECK(run.walkedAxisClear);
	CHECK(run.walkedBeyondNotClear);
	CHECK(run.targetHiddenBlocked);
	CHECK(run.targetVisibleAllowed);
	MESSAGE("walker look radius (cells): " << run.radiusCells);
}

TEST_CASE("shroud retail: two runs of the same skirmish agree on every frame hash")
{
	OPENBFME_REQUIRE_START(s);
	RetailRun a, b;
	runRetail(*s, a, false);
	runRetail(*s, b, false);
	REQUIRE(a.hashes.size() == b.hashes.size());
	CHECK(a.hashes == b.hashes);
}

namespace
{
void perfRun(bool eightPlayers)
{
	Fx f;
	if (eightPlayers)
	{
		// the same looks shared by 8 players (an 8-player alliance): every look and unlook touches 8 player columns
		ShroudManager::ObjectSource s = ShroudManager::retailObjectSource();
		auto look = s.look;
		s.look = [look](const Object &o, const ShroudManager::TemplateVision &tv, ShroudManager::LookInputs &in) {
			look(o, tv, in);
			in.mask = 0xFFu;
		};
		f.shroud.setObjectSource(s);
	}
	f.shroud.setExtent(0.0f, 0.0f, 4000.0f, 4000.0f);
	std::vector<Object *> units;
	for (int i = 0; i < 5000; ++i)
	{
		units.push_back(f.place(i % 2 ? "Scout" : "Hiller", "Alice", 100.0f + (float)(i % 70) * 54.0f, 100.0f + (float)(i / 70) * 54.0f, 0.3f * (float)i));
	}
	f.shroud.update();
	double total = 0.0;
	const int frames = 20;
	for (int k = 0; k < frames; ++k)
	{
		for (size_t i = 0; i < units.size(); ++i)
		{
			Coord3D p = *units[i]->getPosition();
			p.x += (k % 2) ? -7.0f : 7.0f;
			units[i]->setPosition(&p);
		}
		const auto t0 = std::chrono::steady_clock::now();
		f.shroud.update();
		total += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
	}
	MESSAGE("shroud update, 5000 moving units, " << (eightPlayers ? 8 : 1) << " player(s) per look: " << total / frames << " ms per update");
}
} // namespace

TEST_CASE("shroud perf: 5000 moving units, one shroud update per frame, for 1 and 8 players per look (timing only, run with -tc and --no-skip)" * doctest::skip())
{
	perfRun(false);
	perfRun(true);
}

namespace
{
std::uint32_t hashOf(const ShroudManager &s)
{
	StateHasher h;
	s.hash(h);
	return h.value();
}
} // namespace

TEST_CASE("shroud hash: the grid origin, the unlook duration, the record facing, the dirty flags and their order, the coverage each move the hash")
{
	// the dirty order: the same two records marked in the two orders
	{
		Fx a, b;
		Object *a1 = a.place("Scout", "Alice", 200.0f, 200.0f), *a2 = a.place("Scout", "Alice", 600.0f, 600.0f);
		Object *b1 = b.place("Scout", "Alice", 200.0f, 200.0f), *b2 = b.place("Scout", "Alice", 600.0f, 600.0f);
		a.shroud.update();
		b.shroud.update();
		CHECK(hashOf(a.shroud) == hashOf(b.shroud));
		a.shroud.markDirty(*a1, false);
		a.shroud.markDirty(*a2, false);
		b.shroud.markDirty(*b2, false);
		b.shroud.markDirty(*b1, false);
		CHECK(hashOf(a.shroud) != hashOf(b.shroud));
		// a dirty flag alone (no other change)
		Fx c, d;
		Object *c1 = c.place("Scout", "Alice", 200.0f, 200.0f);
		d.place("Scout", "Alice", 200.0f, 200.0f);
		c.shroud.update();
		d.shroud.update();
		CHECK(hashOf(c.shroud) == hashOf(d.shroud));
		c.shroud.markDirty(*c1, false);
		CHECK(hashOf(c.shroud) != hashOf(d.shroud));
	}
	// the grid origin with the same cell counts; the unlook duration
	{
		Fx a, b, c;
		b.shroud.setExtent(10.0f, 0.0f, 1010.0f, 800.0f);
		CHECK(a.shroud.cellCountX() == b.shroud.cellCountX());
		CHECK(hashOf(a.shroud) != hashOf(b.shroud));
		c.shroud.init(40.0f, 2);
		c.shroud.setExtent(0.0f, 0.0f, 1000.0f, 800.0f);
		a.shroud.init(40.0f, 1);
		a.shroud.setExtent(0.0f, 0.0f, 1000.0f, 800.0f);
		CHECK(hashOf(a.shroud) != hashOf(c.shroud));
	}
	// the stored facing of a round look (the cells are the same)
	{
		Fx a, b;
		a.place("Scout", "Alice", 200.0f, 200.0f, 0.0f);
		b.place("Scout", "Alice", 200.0f, 200.0f, 1.0f);
		a.shroud.update();
		b.shroud.update();
		CHECK(a.clearCells(a.idx("Alice")) == b.clearCells(b.idx("Alice")));
		CHECK(hashOf(a.shroud) != hashOf(b.shroud));
	}
	// the covered cells (same look, a wider geometry)
	{
		Fx a, b;
		a.place("Scout", "Alice", 220.0f, 220.0f);
		b.place("BigScout", "Alice", 220.0f, 220.0f);
		a.shroud.update();
		b.shroud.update();
		CHECK(a.clearCells(a.idx("Alice")) == b.clearCells(b.idx("Alice")));
		CHECK(hashOf(a.shroud) != hashOf(b.shroud));
	}
}

TEST_CASE("shroud objects: a geometry row that does not parse is an error naming the template, never a default coverage (PLAN rule 10)")
{
	Fx f;
	const std::string err = f.w.load("Object Bogus\n  KindOf = INFANTRY\n  ShroudClearingRange = 50\n  Geometry = BOGUS\nEnd\n");
	REQUIRE_MESSAGE(err.empty(), err);
	f.place("Bogus", "Alice", 300.0f, 300.0f);
	CHECK_THROWS_WITH_AS(f.shroud.update(), doctest::Contains("shroud: template Bogus"), std::logic_error);
}

TEST_CASE("shroud stops: S-560 .. S-567 are the exact stop lines and reach GameLogic::report().stops once the manager is attached")
{
	const std::vector<std::string> expected = {
		"[S-560] shroud channels: the three per-cell channels of RW 0xB51030 (BuildCost / ThreatValue / CampnessValue maps the AI reads, RW 0x68EC3B) are not "
		"added; no AI consumer is ported",
		"[S-561] ghost objects: a destroyed immobile object's record is not kept for the players that saw it fogged (RW 0xB4E190, the ghost object of RW "
		"0x691602 / TheGhostObjectManager); its remembered building disappears when it dies",
		"[S-562] shroud inference: the object statuses are evaluated eagerly in update() (retail on demand); the covered cells use the template's last geometry "
		"shape; TheTerrainLogic::getExtent is boundary 0 (BFME2 donor, present-unmatched)",
		"[S-563] shroud numerics: the look polygon's x87 extended-precision vertices are computed in binary64 with SimMath's sin / cos (RW fsin / fcos); "
		"a horizontal first left edge (uninitialised slope in RW 0xB501A0) starts at the top vertex with slope 0",
		"[S-564] object look: the SHROUD_CLEARING modifier (RW 0x804F39 type 0x14) and the object's own range (Object + 0x1B4) are read since lane DECOMP-1; the spied mask (player + 0x3C8), object + 0x30 / + 0x3F4 (radii "
		"0.1), DynamicShroudClearingRangeUpdate and the HordeContain VisionSide / VisionRearOverride are not read; object + 0x480 (hide when fogged) is taken as set",
		"[S-565] fogged targets: a human player's units cannot attack an object FOGGED or SHROUDED for that player unless the order comes from a script (RW "
		"0x82C167, the action helper: human and not script, as ZH ActionManager isObjectShroudedForAction); its other callers and the other action types were not read",
		"[S-566] stealth and detection: RotWK's invisibility (InvisibilityUpdate, StealthDetectorUpdate, the InvisibilityManager, the INVISIBLE_STEALTH / CAMOUFLAGE "
		"conditions and the INVISIBLE_DETECTED statuses) is lane STEALTH-1's (S-1040 .. S-1042); the shroud does not change for a stealthed object",
		"[S-567] shroud drawing: one texel per cell with bilinear filtering multiplies the terrain by ShroudColor * level / 255 (ClearAlpha / FogAlpha / "
		"ShroudAlpha); objects the local player cannot see are not drawn, fogged ones are not darkened; water, rivers and roads are not shrouded; the radar "
		"picture is multiplied per pixel; W3DShroud / W3DRadar's own textures were not read",
	};
	CHECK(ShroudManager::stopLines() == expected);
	Fx f;
	const std::vector<std::string> stops = f.logic->report().stops;
	for (const std::string &s : expected)
	{
		CHECK_MESSAGE(std::find(stops.begin(), stops.end(), s) != stops.end(), s);
	}
}

TEST_CASE("shroud objects: a GeometryOffset with a blank after the colon parses like retail's colon sub-tokens (RotWK mumakilpen.ini `X: -10.0`)")
{
	Fx f;
	const std::string err = f.w.load("Object Pen\n  KindOf = STRUCTURE IMMOBILE\n  ShroudClearingRange = 50\n  Geometry = BOX\n  GeometryMajorRadius = 46.0\n"
									 "  GeometryMinorRadius = 40.0\n  GeometryOffset = X: -10.0 Y: 2.0 Z: 0.0\nEnd\n");
	REQUIRE_MESSAGE(err.empty(), err);
	const std::vector<ObjectGeometry::Shape> shapes = ObjectGeometry::shapesOf(*f.w.get("Pen"));
	REQUIRE(shapes.size() == 1);
	CHECK(shapes[0].offsetX == -10.0f);
	CHECK(shapes[0].offsetY == 2.0f);
	f.place("Pen", "Alice", 300.0f, 300.0f);
	CHECK_NOTHROW(f.shroud.update());
}

TEST_CASE("shroud retail: every template of the retail object world gives the shroud its vision values and geometry (no row is rejected)")
{
	OPENBFME_REQUIRE_START(s);
	size_t n = 0;
	for (const ThingTemplate *t : s->world->things().templates())
	{
		const ThingTemplate &tt = *t->getFinalOverride();
		CHECK_NOTHROW(ShroudManager::templateVision(tt));
		++n;
	}
	CHECK(n > 1000);
}

TEST_CASE("shroud hash: peers with different local players hash the same after the local refresh and after a grid resize")
{
	Fx a, b;
	for (Fx *f : { &a, &b })
	{
		f->place("Scout", "Alice", 300.0f, 300.0f);
		f->place("Scout", "Bob", 700.0f, 500.0f);
		f->place("Tower", "Bob", 500.0f, 300.0f);
	}
	a.shroud.setLocalPlayer(a.idx("Alice"), [](int, int, CellShroudStatus) {});
	b.shroud.setLocalPlayer(b.idx("Bob"), [](int, int, CellShroudStatus) {});
	a.shroud.update();
	b.shroud.update();
	CHECK(hashOf(a.shroud) == hashOf(b.shroud));
	a.shroud.refreshLocalPlayer();
	b.shroud.refreshLocalPlayer();
	CHECK(hashOf(a.shroud) == hashOf(b.shroud));
	a.shroud.update();
	b.shroud.update();
	CHECK(hashOf(a.shroud) == hashOf(b.shroud));
	// the grid resize (RW 0xB51D90 ends with the local refresh)
	a.shroud.setExtent(0.0f, 0.0f, 1200.0f, 900.0f);
	b.shroud.setExtent(0.0f, 0.0f, 1200.0f, 900.0f);
	CHECK(hashOf(a.shroud) == hashOf(b.shroud));
	a.shroud.update();
	b.shroud.update();
	CHECK(hashOf(a.shroud) == hashOf(b.shroud));
	CHECK(a.logic->computeStateHash() == b.logic->computeStateHash());
}

TEST_CASE("shroud objects: GeometryOffset reads X, Y, Z in order with scanned values (RW 0xAD1770 -> 0x42F247); malformed or reordered axes are errors")
{
	Fx f;
	auto offset = [&f](const std::string &name, const std::string &value) {
		const std::string err = f.w.load("Object " + name + "\n  Geometry = BOX\n  GeometryMajorRadius = 10\n  GeometryMinorRadius = 10\n  GeometryOffset = " + value + "\nEnd\n");
		REQUIRE_MESSAGE(err.empty(), err);
		return ObjectGeometry::shapesOf(*f.w.get(name));
	};
	const auto ok = offset("Ok1", "x:1.5 Y: -2 z :3");
	REQUIRE(ok.size() == 1);
	CHECK(ok[0].offsetX == 1.5f);
	CHECK(ok[0].offsetY == -2.0f);
	CHECK(ok[0].offsetZ == 3.0f);
	CHECK_THROWS_AS(offset("Bad1", "X: nope Y: 2 Z: 0"), std::logic_error);
	CHECK_THROWS_AS(offset("Bad2", "X: Y:2 Z:0"), std::logic_error);
	CHECK_THROWS_AS(offset("Bad3", "Y:2 X:1 Z:0"), std::logic_error);
	CHECK_THROWS_AS(offset("Bad4", "X:1 Z:0 Y:2"), std::logic_error);
	CHECK_THROWS_AS(offset("Bad5", "X:1 Y:2"), std::logic_error);
	CHECK_THROWS_AS(offset("Bad6", "X:1 Y:2 Z:"), std::logic_error);
	// a macro value is scanned like retail's numeric scan (sscanf "%f": the leading number, the rest of the token ignored)
	const std::string mac = f.w.load("#define NUM 1 junk\nObject Mac\n  Geometry = BOX\n  GeometryMajorRadius = 10\n  GeometryMinorRadius = 10\n  GeometryOffset = X: NUM Y:2 Z:3\nEnd\n");
	REQUIRE_MESSAGE(mac.empty(), mac);
	std::vector<ObjectGeometry::Shape> m;
	CHECK_NOTHROW(m = ObjectGeometry::shapesOf(*f.w.get("Mac")));
	REQUIRE(m.size() == 1);
	CHECK(m[0].offsetX == 1.0f);
	CHECK(m[0].offsetY == 2.0f);
	CHECK(m[0].offsetZ == 3.0f);
}

TEST_CASE("shroud objects: the template values are read at every use: an in-place INI overwrite of the range and a later bad geometry row are seen, whatever was read before")
{
	// the range: 100 (3 cells), then the same template overwritten to 500 (13 cells)
	Fx f;
	const int alice = f.idx("Alice");
	Object *s = f.place("Scout", "Alice", 500.0f, 400.0f); // cell (12, 10)
	f.shroud.update();
	CHECK(f.shroud.getCellStatus(alice, 15, 10) == CELLSHROUD_CLEAR);
	CHECK(f.shroud.getCellStatus(alice, 20, 10) == CELLSHROUD_SHROUDED);
	const ThingTemplate *before = f.w.get("Scout");
	std::string err = f.w.load("Object Scout\n  KindOf = INFANTRY SELECTABLE\n  ShroudClearingRange = 500\n  Geometry = CYLINDER\n  GeometryMajorRadius = 5\nEnd\n");
	REQUIRE_MESSAGE(err.empty(), err);
	CHECK(f.w.get("Scout") == before); // the overwrite changes the same template in place
	REQUIRE(s->getTemplate()->getFinalOverride()->findField("ShroudClearingRange") != nullptr);
	CHECK(ShroudManager::templateVision(*s->getTemplate()->getFinalOverride()).shroudClearingRange == 500.0f);
	f.shroud.markDirty(*s, true);
	f.shroud.update();
	CHECK(f.shroud.getCellStatus(alice, 24, 10) == CELLSHROUD_CLEAR);
	// a bad geometry row added after the template was read is an error at the next use
	err = f.w.load("Object Scout\n  KindOf = INFANTRY SELECTABLE\n  ShroudClearingRange = 500\n  Geometry = BOGUS\nEnd\n");
	REQUIRE_MESSAGE(err.empty(), err);
	f.shroud.markDirty(*s, true);
	CHECK_THROWS_WITH_AS(f.shroud.update(), doctest::Contains("shroud: template Scout"), std::logic_error);
}

TEST_CASE("shroud hash: what was read from a template before does not change later results (no read history)")
{
	Fx a, b;
	Object *sa = a.place("Scout", "Alice", 500.0f, 400.0f);
	Object *sb = b.place("Scout", "Alice", 500.0f, 400.0f);
	a.shroud.update(); // a reads the template at 100 first
	b.shroud.update();
	for (Fx *f : { &a, &b })
	{
		const std::string err = f->w.load("Object Scout\n  KindOf = INFANTRY SELECTABLE\n  ShroudClearingRange = 300\n  Geometry = CYLINDER\n  GeometryMajorRadius = 5\nEnd\n");
		REQUIRE_MESSAGE(err.empty(), err);
	}
	(void)ShroudManager::templateVision(*sa->getTemplate()->getFinalOverride());
	CHECK(hashOf(a.shroud) == hashOf(b.shroud));
	a.shroud.markDirty(*sa, true);
	b.shroud.markDirty(*sb, true);
	a.shroud.update();
	b.shroud.update();
	CHECK(hashOf(a.shroud) == hashOf(b.shroud));
}

TEST_CASE("shroud refresh: an unseen structure in fog becomes SHROUDED after its viewer's relationship turns from neutral to enemy, the same on every peer")
{
	Fx a, b, c;
	std::vector<std::uint32_t> hashes;
	int n = 0;
	for (Fx *f : { &a, &b, &c })
	{
		Player *alice = f->players.findPlayerWithName("Alice");
		Player *bob = f->players.findPlayerWithName("Bob");
		alice->setPlayerRelationship(bob, NEUTRAL);
		f->shroud.revealMapForPlayer(alice->getPlayerIndex()); // explored: fog everywhere, never clear
		Object *tower = f->place("Tower", "Bob", 700.0f, 500.0f);
		// different local players: Alice, Bob, and none (an invalid index)
		const int local = n == 0 ? alice->getPlayerIndex() : (n == 1 ? bob->getPlayerIndex() : -1);
		f->shroud.setLocalPlayer(local, [](int, int, CellShroudStatus) {});
		f->shroud.update();
		CHECK(f->shroud.peekObjectStatus(*tower, alice->getPlayerIndex()) == OBJECTSHROUD_FOGGED); // neutral and immobile: drawn in fog (RW 0x68EDD0)
		alice->setPlayerRelationship(bob, ENEMIES);
		f->shroud.refreshLocalPlayer();
		CHECK(f->shroud.peekObjectStatus(*tower, alice->getPlayerIndex()) == OBJECTSHROUD_SHROUDED); // an enemy structure never seen: hidden
		hashes.push_back(hashOf(f->shroud));
		f->shroud.update();
		CHECK(f->shroud.peekObjectStatus(*tower, alice->getPlayerIndex()) == OBJECTSHROUD_SHROUDED);
		hashes.push_back(hashOf(f->shroud));
		++n;
	}
	CHECK(hashes[0] == hashes[2]);
	CHECK(hashes[0] == hashes[4]);
	CHECK(hashes[1] == hashes[3]);
	CHECK(hashes[1] == hashes[5]);
}

// ---- lane DECOMP-1: the object's own clearing range (RW 0x68C234), the forced look (RW 0x68C7E9) and the SHROUD_CLEARING modifier (RW 0x68E500) ----
#include "GameLogic/AttributeModifiers.h"
#include "GameLogic/Object/AttributeModifierPool.h"

namespace
{
const char kDecomp1Objects[] =
	"ModifierList TestFarSight\n"
	"  Category = BUFF\n"
	"  Duration = 0\n"
	"  Modifier = SHROUD_CLEARING 100%\n"
	"End\n"
	"Object PooledScout\n"
	"  KindOf = INFANTRY\n"
	"  ShroudClearingRange = 100\n"
	"  Behavior = AttributeModifierPoolUpdate ModuleTag_Pool\n"
	"  End\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 5\n"
	"End\n";

struct Decomp1Fx : Fx
{
	AttributeModifierStore modifiers;
	AttributeModifierStore *saved = TheAttributeModifierStore;
	Decomp1Fx()
	{
		modifiers.registerBlock(w.fx.env.blocks);
		TheAttributeModifierStore = &modifiers;
		AttributeModifierPool::registerClass(w.modules); // the real pool (the base fixture binds no runtime class to the name)
		const std::string err = w.load(kDecomp1Objects);
		REQUIRE_MESSAGE(err.empty(), err);
	}
	~Decomp1Fx() { TheAttributeModifierStore = saved; }
};
} // namespace

// RW 0x68C234: + 0x1B4 = range when it differs, the record marked dirty (forced); RW 0x68C7E9 -> 0xB4F410: the record leaves the dirty list and looks now, before
// any shroud update (the SpecialPowerViewObject of RW 0x896FD9 reveals at once). Before lane DECOMP-1 the object kept the template's range
TEST_CASE("shroud objects (DECOMP-1): setShroudClearingRange gives an object its own range and updateShroudNow looks at once")
{
	Fx f;
	const int bob = f.idx("Bob");
	Object *blind = f.place("Blind", "Bob", 420.0f, 420.0f);
	f.shroud.update();
	CHECK(f.shroud.getCellStatus(bob, 10, 10) == CELLSHROUD_SHROUDED); // no ShroudClearingRange: no look
	blind->setShroudClearingRange(100.0f);
	CHECK(blind->hasShroudClearingRange());
	blind->updateShroudNow();
	CHECK(f.shroud.getCellStatus(bob, 10, 10) == CELLSHROUD_CLEAR);
	CHECK(f.shroud.getCellStatus(bob, 13, 10) == CELLSHROUD_CLEAR); // ceil(100 / 40) = 3 cells
	CHECK(f.shroud.getCellStatus(bob, 14, 10) == CELLSHROUD_SHROUDED);
	CHECK(f.shroud.dirtyCount() == 0u);
}

// RW 0x68E500 .. 0x68E52E: the pool's SHROUD_CLEARING sum scales the range by (1 + sum): 100% doubles 100 to 200, ceil(200 / 40) = 5 cells
TEST_CASE("shroud objects (DECOMP-1): a SHROUD_CLEARING modifier widens the look by its percentage")
{
	Decomp1Fx f;
	const int alice = f.idx("Alice");
	Object *scout = f.place("PooledScout", "Alice", 420.0f, 420.0f);
	f.shroud.update();
	CHECK(f.shroud.getCellStatus(alice, 13, 10) == CELLSHROUD_CLEAR);
	CHECK(f.shroud.getCellStatus(alice, 15, 10) == CELLSHROUD_SHROUDED);
	REQUIRE(scout->addAttributeModifier("TestFarSight", -1));
	f.logic->runLogicFrame(); // the pool answers from the next frame (an entry is suppressed at its own frame 0)
	// no manual markDirty: the add itself marked the stationary scout's record dirty (RW 0x805E59 -> 0x68C213, lane DECOMP-1 r3)
	f.shroud.update();
	CHECK(f.shroud.getCellStatus(alice, 15, 10) == CELLSHROUD_CLEAR);
	CHECK(f.shroud.getCellStatus(alice, 16, 10) == CELLSHROUD_SHROUDED);
}
