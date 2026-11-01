// OpenBFME unit tests. GPL-3.0.
// Lane MODCOMPAT-MOD1: RebuildHoleExposeDie (RW 0x889AAF) and RebuildHoleBehavior (RW 0x8868C2 / 0x886831 / 0x8865F7 / 0x886878). Binary facts read from the
// RotWK binary (RW_GAME_DAT), the retail data (ROTWK_INSTALL) and live objects of a started skirmish. The expectations are retail's rules (each check names
// its address), never this engine's frame-exact output.

#include "doctest.h"

#include "BuildTestUtil.h"
#include "PeImage.h"
#include "StartTestUtil.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/StateHash.h"
#include "Common/Thing/ThingFactory.h"
#include "Common/Thing/ThingTemplate.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "GameLogic/Combat/CombatQueries.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/ConstructionModules.h"
#include "GameLogic/Module/RebuildHoleModules.h"
#include "GameLogic/Module/SpawnBehavior.h"
#include "GameLogic/Module/WorkerAIUpdate.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/ObjectGeometry.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/ObjectTemplateInfo.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace
{
float floatAt(const retailtest::PeImage &pe, std::uint32_t va)
{
	const std::uint32_t u = pe.u32At(va);
	float f;
	std::memcpy(&f, &u, 4);
	return f;
}

// the lobby's result: slot 0 a human (`faction0`), slot 1 a computer of `aiState` (`faction1`) on Evendim, start positions 0 and 4 (BUILD-1's game)
bool startHoleGame(starttest::Shared &s, const std::string &faction0, const std::string &faction1, std::uint32_t seed, buildtest::Game &out, std::string *error)
{
	static std::vector<MapCacheEntry> cache;
	if (cache.empty() && !IniSkirmishSetupSource::loadMapCache(*s.mount->fs, cache, error))
	{
		return false;
	}
	startgolden::Spec a{ 0 }, b{ 1 };
	a.tmpl = buildtest::templateIndex(s, faction0);
	b.tmpl = buildtest::templateIndex(s, faction1);
	a.color = 0;
	b.color = 1;
	a.start = 0;
	b.start = 4;
	NewGameMessage message;
	message.game = startgolden::makeInfo({ a, b });
	message.game.slots[1].state = SLOT_EASY_AI;
	message.game.mapName = "maps/map mp evendim/map mp evendim.map";
	message.game.seed = seed;
	message.game.startingCash = 5000;
	if (!NewGame::prepareNewGame(message, s.world->playerTemplates(), s.settings, cache, RandomAlgorithm::ZH_CarryChain, out.start, error))
	{
		return false;
	}
	out.source = std::make_unique<ArchiveW3DFileSource>(*s.mount->fs);
	out.assets = std::make_unique<WW3DAssetManager>(*out.source);
	out.live = std::make_unique<LiveGame>(*s.world, *s.mount->fs, *out.assets, s.options);
	LiveGame::Options o;
	o.start = &out.start;
	return out.live->load(o, error);
}

const Player *playerAtStart(GameLogic &logic, int start)
{
	for (int i = 0; i < logic.players().getPlayerCount(); ++i)
	{
		const Player *p = logic.players().getNthPlayer(i);
		if (p && p->getPlayerTemplate() && p->getPlayerTemplate()->m_playableSide && p->getMultiplayerStartIndex() == start)
		{
			return p;
		}
	}
	return nullptr;
}

Object *firstOf(GameLogic &logic, const Player *owner, const char *kindOfName)
{
	for (Object *x = logic.getFirstObject(); x; x = x->getNextObject())
	{
		if (x->getControllingPlayer() == owner && x->isKindOfName(kindOfName) && !x->isEffectivelyDead())
		{
			return x;
		}
	}
	return nullptr;
}

// the live (not effectively dead, not destroyed) objects of a template, newest last
std::vector<Object *> liveOf(GameLogic &logic, const std::string &name)
{
	std::vector<Object *> out;
	for (Object *x = logic.getFirstObject(); x; x = x->getNextObject())
	{
		if (x->getTemplate()->getName() == name && !x->isEffectivelyDead() && !x->isDestroyed())
		{
			out.push_back(x);
		}
	}
	return out;
}

template <class T>
const T *dataOf(const ThingTemplate *tt)
{
	for (const ThingTemplate::Nugget &n : tt->getFinalOverride()->behaviorModules().nuggets())
	{
		if (const T *d = dynamic_cast<const T *>(n.data.get()))
		{
			return d;
		}
	}
	return nullptr;
}

bool status(const Object &o, const char *name)
{
	return o.testStatus((unsigned)ObjectTemplateInfoBuilder::objectStatusIndex(name));
}

// the module's snapshot round-trips (the state of `before` comes back after more frames changed it) and a truncated one is refused
void checkRoundTrip(RebuildHoleBehavior &rh, buildtest::Game &g, int frames)
{
	StateHasher before;
	rh.crc(before);
	XferSave saved;
	rh.xfer(saved);
	const UnsignedInt counter = rh.workerWaitCounter();
	for (int i = 0; i < frames; ++i)
	{
		g.live->advance(0.2);
	}
	CHECK(rh.workerWaitCounter() != counter);
	{
		XferLoad load(saved.data());
		rh.xfer(load);
		CHECK(load.atEnd());
	}
	StateHasher after;
	rh.crc(after);
	CHECK(after.value() == before.value());
	CHECK(rh.workerWaitCounter() == counter);
	std::vector<std::uint8_t> truncated = saved.data();
	truncated.pop_back();
	XferLoad lt(truncated);
	CHECK_THROWS_AS(rh.xfer(lt), XferException);
	{
		XferLoad load(saved.data()); // back to the saved state
		rh.xfer(load);
	}
}
} // namespace

// ---- binary facts ------------------------------------------------------------------------------------------------------------------------------------------

TEST_CASE("rebuild hole: the field tables RW 0xC616B8 / 0xC60230, the defaults (RW 0x8899D0 / 0x8864BB) and the frame constants")
{
	const retailtest::PeImage *pe = retailtest::PeImage::fromEnvironment();
	if (!pe)
	{
		retailtest::printSkip("rebuild hole binary facts (set RW_GAME_DAT)");
		return;
	}
	struct Row
	{
		const char *name;
		std::uint32_t parser;
		std::uint32_t offset;
	};
	const Row expose[] = { { "HoleName", 0x42EE5E, 0x38 }, { "HoleMaxHealth", 0x42ED00, 0x3C }, { "TransferAttackers", 0x42E558, 0x44 },
		{ "FadeInTimeSeconds", 0x42ED00, 0x40 } };
	const Row hole[] = { { "WorkerObjectName", 0x42EE5E, 0x10 }, { "WorkerRespawnDelay", 0x73A403, 0x08 }, { "HoleHealthRegen%PerSecond", 0x42EEFA, 0x0C } };
	auto check = [&](std::uint32_t table, const Row *rows, size_t n) {
		for (size_t i = 0; i < n; ++i)
		{
			const std::uint32_t at = table + 16u * (std::uint32_t)i;
			CHECK(pe->cstring(pe->u32At(at)) == rows[i].name);
			CHECK(pe->u32At(at + 4u) == rows[i].parser);
			CHECK(pe->u32At(at + 12u) == rows[i].offset);
		}
		CHECK(pe->u32At(table + 16u * (std::uint32_t)n) == 0u);
	};
	check(0xC616B8, expose, 4);
	check(0xC60230, hole, 3);
	CHECK(floatAt(*pe, 0xBD83D4) == 0.1f);   // the HoleHealthRegen default (RW 0x8864C5)
	CHECK(pe->u32At(0xD9F608) == 5u);       // LOGICFRAMES_PER_SECOND (the heal, RW 0x886BA8)
	CHECK(pe->u32At(0xD9F60C) == 30u);      // the client frames of the fade (RW 0x889C3D)
	CHECK(floatAt(*pe, 0xD9F610) == 0.005f); // parseDurationReal's scale (RW 0x73A403)
	// the rebuild interface (RW 0xC60270): startRebuildProcess, getSpawnerID, getReconstructedBuildingID, getRebuildTemplate, setSpawnerWasActive
	const std::uint32_t slots[] = { 0x886831, 0x5F2D61, 0x9F9F16, 0x9F9F54, 0x8AA101 };
	for (int i = 0; i < 5; ++i)
	{
		CHECK(pe->u32At(0xC60270 + 4u * (std::uint32_t)i) == slots[i]);
	}
	CHECK(pe->u32At(0xC60288) == 0x8868C2u); // the update interface's slot 0
	CHECK(pe->u32At(0xC60284) == 0x886878u); // the die interface's slot 0
	CHECK(pe->u32At(0xC6ACA8 + 0x1F8) == 0x8AD6A4u); // WorkerAIUpdate's construct (vslot 0x1F8)
}

// ---- the retail data -----------------------------------------------------------------------------------------------------------------------------------

TEST_CASE("rebuild hole retail: the creep lairs leave their hole (500 health, no worker, 120 s); the Wild holes name MordorWorkerNoSelect")
{
	OPENBFME_REQUIRE_START(s);
	RetailObjectWorld &w = *s->world;
	const ThingTemplate *lair = w.things().findTemplate("CaveTrollLair");
	REQUIRE(lair);
	const RebuildHoleExposeDieModuleData *ed = dataOf<RebuildHoleExposeDieModuleData>(lair);
	REQUIRE(ed);
	CHECK(ed->m_holeName == "CaveTrollLairHole");
	CHECK(ed->m_holeMaxHealth == 500.0f);
	CHECK(ed->m_fadeInTimeSeconds == 2.0f);
	CHECK_FALSE(ed->m_transferAttackers);
	CHECK(MaskTest(ed->m_dieMux.m_exemptStatus, (unsigned)ObjectTemplateInfoBuilder::objectStatusIndex("SOLD")));
	const ThingTemplate *hole = w.things().findTemplate("CaveTrollLairHole");
	REQUIRE(hole);
	const RebuildHoleBehaviorModuleData *hd = dataOf<RebuildHoleBehaviorModuleData>(hole);
	REQUIRE(hd);
	CHECK(hd->m_workerObjectName.empty());
	CHECK(hd->m_workerRespawnDelay == 600.0f); // 120000 ms * 0.005
	CHECK(hd->m_holeHealthRegen == 0.0f);
	const ThingTemplate *cave = w.things().findTemplate("GoblinCave");
	REQUIRE(cave);
	const RebuildHoleExposeDieModuleData *cd = dataOf<RebuildHoleExposeDieModuleData>(cave);
	REQUIRE(cd);
	CHECK(cd->m_holeName == "WildCaveHole");
	CHECK(cd->m_transferAttackers); // the constructor's default (RW 0x8899EF)
	const RebuildHoleBehaviorModuleData *wd = dataOf<RebuildHoleBehaviorModuleData>(w.things().findTemplate("WildCaveHole"));
	REQUIRE(wd);
	CHECK(wd->m_workerObjectName == "MordorWorkerNoSelect");
	CHECK(wd->m_workerRespawnDelay == 600.0f);
	CHECK(wd->m_holeHealthRegen == doctest::Approx(0.005f)); // tolerance: 0.5 % through parsePercentToReal's float product (0.5 * 0.01) is not the literal 0.005f
	// every template with a RebuildHoleExposeDie names a hole template that has a RebuildHoleBehavior
	int dies = 0;
	for (const ThingTemplate *tt : w.things().templates())
	{
		if (const RebuildHoleExposeDieModuleData *d = dataOf<RebuildHoleExposeDieModuleData>(tt))
		{
			++dies;
			const ThingTemplate *h = w.things().findTemplate(d->m_holeName);
			CHECK_MESSAGE(h, tt->getName());
			if (h)
			{
				CHECK_MESSAGE(dataOf<RebuildHoleBehaviorModuleData>(h), d->m_holeName);
			}
		}
	}
	CHECK(dies >= 18); // tolerance: the census names 18 templates; the loaded world's ChildObject copies (the Snow lairs' children) inherit the module too
	const std::vector<std::string> unported = w.modules().unportedModuleClassNames();
	CHECK(std::find(unported.begin(), unported.end(), "RebuildHoleExposeDie") == unported.end());
	CHECK(std::find(unported.begin(), unported.end(), "RebuildHoleBehavior") == unported.end());
	int s1427 = 0;
	for (const std::string &l : w.acceptanceStops())
	{
		s1427 += l.rfind("[S-1427] RebuildHoleExposeDie / RebuildHoleBehavior (lane MODCOMPAT-MOD1, ported", 0) == 0 ? 1 : 0;
	}
	CHECK(s1427 == 1);
}

// ---- a live game -----------------------------------------------------------------------------------------------------------------------------------------

TEST_CASE("rebuild hole live: a dead Cave Troll Lair leaves its hole, which rebuilds the lair by itself after 600 frames (RW 0x889AAF / 0x8868C2)")
{
	OPENBFME_REQUIRE_START(s);
	buildtest::Game g;
	std::string err;
	REQUIRE_MESSAGE(startHoleGame(*s, "FactionMen", "FactionMordor", 4411, g, &err), err);
	GameLogic &logic = g.live->logic();
	const Player *human = playerAtStart(logic, 0);
	REQUIRE(human);
	Object *fortress = firstOf(logic, human, "COMMANDCENTER");
	REQUIRE(fortress);
	const Coord3D at{ fortress->getPosition()->x + 600.0f, fortress->getPosition()->y, fortress->getPosition()->z };
	Object *lair = g.live->createObject("CaveTrollLair", human->getPlayerIndex(), at, 0.5f, &err);
	REQUIRE_MESSAGE(lair, err);
	for (int i = 0; i < 20; ++i)
	{
		g.live->advance(0.2);
	}
	REQUIRE_FALSE(lair->isUnderConstruction());
	auto *spawner = dynamic_cast<SpawnBehavior *>(lair->findModule("SpawnBehavior"));
	REQUIRE(spawner);
	spawner->stopSpawning(); // the hole keeps the spawner's flag (RW 0x889BDA) and stops the rebuilt lair's spawner (RW 0x886B4A)
	lair->setBuildCostPaid(123.0f);
	lair->setName("TestLair");
	const ObjectID lairID = lair->getID();
	const Coord3D lairPos = *lair->getPosition();
	lair->kill(DAMAGE_UNRESISTABLE, 0);
	g.live->advance(0.2);
	std::vector<Object *> holes = liveOf(logic, "CaveTrollLairHole");
	REQUIRE(holes.size() == 1);
	Object *hole = holes.front();
	CHECK(hole->getPosition()->x == lairPos.x);
	CHECK(hole->getPosition()->y == lairPos.y);
	CHECK(hole->getOrientation() == 0.5f);
	CHECK(hole->getTeam() == lair->getTeam());
	CHECK(hole->getName() == "TestLair"); // RW 0x759467
	// RW 0x699349: the hole carries the dead lair's GeometryInfo (its own template is a 25-radius cylinder)
	CHECK(&hole->geometryTemplate() == lair->getTemplate());
	REQUIRE(hole->geometryShapes().size() == lair->geometryShapes().size());
	for (size_t i = 0; i < hole->geometryShapes().size(); ++i)
	{
		CHECK(hole->geometryShapes()[i].majorRadius == lair->geometryShapes()[i].majorRadius);
		CHECK(hole->geometryShapes()[i].minorRadius == lair->geometryShapes()[i].minorRadius);
		CHECK(hole->geometryShapes()[i].height == lair->geometryShapes()[i].height);
	}
	CHECK(hole->geometryShapes()[0].majorRadius != ObjectGeometry::shapesOf(*static_cast<const ThingTemplate *>(hole->getTemplate()))[0].majorRadius);
	// Sol review of MODCOMPAT-MOD1: the lair's flags were never changed, so the hole has no flags of its own; the partition still reads the lair's GeometryInfo
	// (its bounding circle / sphere, RW Object + 0xB8 / + 0xBC), as combat does, not the hole template's cylinder
	REQUIRE(hole->geometryActive().empty());
	REQUIRE(lair->geometryActive().empty());
	CHECK(logic.partition().geometryOf(*hole).circle == logic.partition().geometryOf(*lair).circle);
	CHECK(logic.partition().geometryOf(*hole).sphere == logic.partition().geometryOf(*lair).sphere);
	CHECK(logic.partition().geometryOf(*hole).circle == CombatQueries::boundingCircleRadius(*hole));
	CHECK(logic.partition().geometryOf(*hole).sphere == CombatQueries::boundingSphereRadius(*hole));
	CHECK(logic.partition().geometryOf(*hole).circle != 25.0f); // the hole template's own 25-radius cylinder
	{
		// the object's snapshot names the template whose geometry it carries: a save / load round trip keeps the lair's
		XferSave saved;
		hole->xfer(saved);
		XferLoad load(saved.data());
		hole->xfer(load);
		CHECK(load.atEnd());
		CHECK(&hole->geometryTemplate() == lair->getTemplate());
		CHECK(hole->geometryShapes()[0].majorRadius == lair->geometryShapes()[0].majorRadius);
	}
	CHECK(hole->getBodyModule()->getMaxHealth() == 500.0f);
	CHECK(hole->getBodyModule()->getHealth() == 500.0f);
	RebuildHoleBehavior *rh = RebuildHoleBehavior::of(*hole);
	REQUIRE(rh);
	CHECK(rh->getRebuildTemplate() == lair->getTemplate());
	CHECK(rh->getSpawnerID() == lairID);
	CHECK(rh->pricePaid() == 123.0f);
	CHECK_FALSE(rh->spawnerWasActive());
	CHECK(rh->getWorkerID() == INVALID_ID);
	CHECK(rh->workerWaitCounter() == 599u); // ftol(600.0) at the death, one update since
	CHECK_FALSE(hole->isMasked());
	CHECK_FALSE(status(*hole, "UNSELECTABLE"));

	checkRoundTrip(*rh, g, 3);

	// the counter runs out: the lair is built at once through the build-now slot, the hole is masked and dies the same frame (the lair is not under construction)
	const unsigned diedAt = logic.getFrame();
	int frames = 0;
	while (!hole->isEffectivelyDead() && frames < 700)
	{
		g.live->advance(0.2);
		++frames;
	}
	MESSAGE("the hole rebuilt its lair " << logic.getFrame() - diedAt << " frames after the lair died");
	CHECK(hole->isEffectivelyDead());
	CHECK(frames == 599); // the saved counter (the round trip restored it)
	CHECK(rh->rebuildsStarted() == 1u);
	CHECK(hole->isMasked());
	CHECK(status(*hole, "UNSELECTABLE"));
	CHECK(status(*hole, "UNATTACKABLE"));
	CHECK_FALSE(hole->isScriptSelectable());
	Object *rebuilt = logic.findObjectByID(rh->getReconstructedBuildingID());
	REQUIRE(rebuilt);
	CHECK(rebuilt->getTemplate()->getName() == "CaveTrollLair");
	CHECK(rebuilt->getID() != lairID);
	CHECK(rebuilt->getProducerID() == hole->getID());
	CHECK(rebuilt->getBuildCostPaid() == 123.0f);
	CHECK(rebuilt->getName() == "TestLair");
	CHECK_FALSE(rebuilt->isUnderConstruction());
	CHECK(rebuilt->getPosition()->x == lairPos.x);
	CHECK(rebuilt->getPosition()->y == lairPos.y);
	// it rises from 1 % of its health (body vslot 0x58) and builds itself up (GettingBuiltBehavior slot 4)
	const float max = rebuilt->getBodyModule()->getMaxHealth();
	const float first = rebuilt->getBodyModule()->getHealth();
	CHECK(first < 0.05f * max); // tolerance: 1 % of the health (vslot 0x58), and the lair's own GettingBuiltBehavior may heal it later in the same frame
	auto *gb = dynamic_cast<GettingBuiltBehavior *>(rebuilt->findModule("GettingBuiltBehavior"));
	REQUIRE(gb);
	auto *spawner2 = dynamic_cast<SpawnBehavior *>(rebuilt->findModule("SpawnBehavior"));
	REQUIRE(spawner2);
	CHECK_FALSE(spawner2->isSpawnerActive()); // RW 0x886B4A: the dead lair's spawner was stopped
	for (int i = 0; i < 50; ++i)
	{
		g.live->advance(0.2);
	}
	CHECK(rebuilt->getBodyModule()->getHealth() > first); // tolerance: the claim is that it builds itself up, not its rate (GettingBuiltBehavior's, tested elsewhere)
}

TEST_CASE("rebuild hole live: a dead Goblin cave's Wild hole makes a worker that rebuilds the cave as RECONSTRUCTING, then the hole dies (RW 0x8AD6A4)")
{
	OPENBFME_REQUIRE_START(s);
	buildtest::Game g;
	std::string err;
	REQUIRE_MESSAGE(startHoleGame(*s, "FactionWild", "FactionMordor", 4412, g, &err), err);
	GameLogic &logic = g.live->logic();
	const Player *human = playerAtStart(logic, 0);
	REQUIRE(human);
	Object *fortress = firstOf(logic, human, "COMMANDCENTER");
	REQUIRE(fortress);
	const Coord3D at{ fortress->getPosition()->x + 500.0f, fortress->getPosition()->y + 100.0f, fortress->getPosition()->z };
	Object *cave = g.live->createObject("GoblinCave", human->getPlayerIndex(), at, 0.0f, &err);
	REQUIRE_MESSAGE(cave, err);
	for (int i = 0; i < 10; ++i)
	{
		g.live->advance(0.2);
	}
	REQUIRE_FALSE(cave->isUnderConstruction());
	const Coord3D cavePos = *cave->getPosition();
	Player *owner = cave->getControllingPlayer();
	REQUIRE(owner);
	const std::uint32_t moneyBefore = owner->getMoney()->countMoney();
	cave->kill(DAMAGE_UNRESISTABLE, 0);
	g.live->advance(0.2);
	std::vector<Object *> holes = liveOf(logic, "WildCaveHole");
	REQUIRE(holes.size() == 1);
	Object *hole = holes.front();
	RebuildHoleBehavior *rh = RebuildHoleBehavior::of(*hole);
	REQUIRE(rh);
	CHECK(rh->getRebuildTemplate()->getName() == "GoblinCave");
	checkRoundTrip(*rh, g, 2);
	int frames = 0;
	while (rh->getWorkerID() == INVALID_ID && frames < 700)
	{
		g.live->advance(0.2);
		++frames;
	}
	REQUIRE(rh->getWorkerID() != INVALID_ID);
	Object *worker = logic.findObjectByID(rh->getWorkerID());
	REQUIRE(worker);
	CHECK(worker->getTemplate()->getName() == "MordorWorkerNoSelect");
	CHECK(status(*worker, "UNSELECTABLE"));
	auto *wa = dynamic_cast<WorkerAIUpdate *>(worker->getAIUpdateInterface());
	REQUIRE(wa);
	CHECK(wa->isRebuildConstructWorker());
	Object *site = logic.findObjectByID(rh->getReconstructedBuildingID());
	REQUIRE(site);
	CHECK(site->getTemplate()->getName() == "GoblinCave");
	CHECK(site->isUnderConstruction());
	CHECK(status(*site, "RECONSTRUCTING"));
	CHECK(site->getBuilderID() == worker->getID());
	CHECK(site->getProducerID() == hole->getID());
	CHECK(site->getPosition()->x == cavePos.x);
	CHECK(site->getPosition()->y == cavePos.y);
	CHECK(owner->getMoney()->countMoney() >= moneyBefore); // tolerance: the player's income keeps coming in meanwhile; the rebuild itself pays nothing (RW 0x8AD77E)
	CHECK(hole->isMasked());
	// the worker builds it; when it is done the hole dies and the worker goes. The ids are taken now: a dead object can be deleted within the loop's last
	// frame and its pointer then dangles (merge queue q34: with #161's block search the worker is; reading it after the loop was a use after free)
	const ObjectID workerID = worker->getID();
	const ObjectID holeID = hole->getID();
	const auto holeDead = [&] {
		const Object *h = logic.findObjectByID(holeID);
		return h == nullptr || h->isEffectivelyDead();
	};
	frames = 0;
	while (!holeDead() && frames < 3000)
	{
		g.live->advance(0.2);
		++frames;
	}
	MESSAGE("the worker rebuilt the cave in " << frames << " frames");
	CHECK(holeDead());
	CHECK_FALSE(site->isUnderConstruction());
	const Object *workerNow = logic.findObjectByID(workerID);
	CHECK((workerNow == nullptr || workerNow->isDestroyed())); // RW 0x886C35 (then the hole's onDie, RW 0x886878)
	g.live->advance(0.2);
	CHECK(logic.findObjectByID(workerID) == nullptr);
}
