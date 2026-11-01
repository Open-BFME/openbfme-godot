// OpenBFME unit tests (lane MODCOMPAT-MOD1): CritterEmitterUpdate (data RW 0x8CD313 / table RW 0xC75640, onCollide RW 0x8CD297, update RW 0x8CD1F6,
// xfer RW 0x8CD256). The synthetic cases run on the move tests' world (TheAI and its contact pass); the retail case loads RotWK's BirdEmitter and SKIPs
// when ROTWK_INSTALL / BFME2_INSTALL are unset. GPL-3.0.

#include "doctest.h"

#include "MoveTestUtil.h"
#include "PathfindTestUtil.h"
#include "RetailTestMount.h"

#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"

#include "Common/PlayerList.h"
#include "Common/StateHash.h"
#include "Common/Team.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "GameClient/FXList.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/Economy.h"
#include "GameLogic/EconomySettings.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/CritterEmitterUpdate.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/Object/RetailObjectWorld.h"
#include "GameLogic/ObjectCreationList.h"

#include <memory>
#include <string>
#include <vector>

namespace
{
const char kLists[] = "FXList FX_TestBirdsA\nEnd\n"
					  "FXList FX_TestBirdsB\nEnd\n"
					  "ObjectCreationList OCL_TestRabbit\n  CreateObject\n    ObjectNames = Walker\n    Count = 1\n  End\nEnd\n";

// RotWK's BirdEmitter (natureunits.ini) with a list of four FX (one "None", one unknown: both NULL entries) and a spawn list
std::string emitter(const char *name, const char *fields)
{
	return std::string("Object ") + name +
		"\n  KindOf = PRELOAD CRITTER_EMITTER\n"
		"  Body = ImmortalBody ModuleTag_ImmortalBody\n    MaxHealth = 1\n  End\n"
		"  Behavior = AIUpdateInterface ModuleTag_03\n  End\n"
		"  Behavior = CritterEmitterUpdate ModuleTag_CrittersEverywhere\n" +
		fields +
		"  End\n"
		"  Geometry = CYLINDER\n  GeometryMajorRadius = 30\n  GeometryHeight = 0.8\n  GeometryIsSmall = No\nEnd\n";
}

struct CritterWorld : movetest::MoveWorld
{
	FXListStore fxLists;
	FXListStore *savedFx = TheFXListStore;
	ObjectCreationListStore ocls{ w.keys };
	ObjectCreationListStore *savedOcl = TheObjectCreationListStore;
	explicit CritterWorld(const std::string &objects)
	{
		TheFXListStore = &fxLists;
		TheObjectCreationListStore = &ocls;
		w.fx.env.blocks.registerBlock("FXList", [](INI *ini) { ParseFXListDefinitionGlobal(ini); });
		w.fx.env.blocks.registerBlock("ObjectCreationList", [](INI *ini) { ObjectCreationListStore::parseObjectCreationListDefinitionGlobal(ini); });
		std::string err = w.load(kLists, INI_LOAD_OVERWRITE, "lists.ini");
		REQUIRE_MESSAGE(err.empty(), err);
		err = w.load(objects, INI_LOAD_OVERWRITE, "critters.ini");
		REQUIRE_MESSAGE(err.empty(), err);
	}
	~CritterWorld()
	{
		logic->reset();
		TheFXListStore = savedFx;
		TheObjectCreationListStore = savedOcl;
	}
	unsigned long long fxAt(const char *site) const
	{
		const auto &sites = logic->fxEvents().perSite();
		const auto it = sites.find(site);
		return it == sites.end() ? 0 : it->second;
	}
};

CritterEmitterUpdate *critterOf(Object *o)
{
	for (const auto &m : o->modules())
	{
		if (CritterEmitterUpdate *x = dynamic_cast<CritterEmitterUpdate *>(m.get()))
		{
			return x;
		}
	}
	return nullptr;
}

const CritterEmitterUpdateModuleData *critterData(const ThingTemplate *t)
{
	for (const ThingTemplate::Nugget &n : t->getFinalOverride()->behaviorModules().nuggets())
	{
		if (const auto *d = dynamic_cast<const CritterEmitterUpdateModuleData *>(n.data.get()))
		{
			return d;
		}
	}
	return nullptr;
}

size_t countTemplate(GameLogic &logic, const char *name)
{
	size_t n = 0;
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		n += !o->isDestroyed() && o->getTemplate()->getName() == name;
	}
	return n;
}
} // namespace

// RW 0x8CD313 / RW 0xC75640: FX (RW 0x73B549: every token, NULL entries kept), ReloadTime (RW 0x73A429: ms -> frames), SpawnObject (RW 0x8CD3A8: every
// token onto + 0x14)
TEST_CASE("critter emitter: the data is RW 0xC75640 (FX and SpawnObject lists keep their NULL entries, ReloadTime in frames)")
{
	CritterWorld f(emitter("TestEmitterDefaults", "") +
		emitter("TestEmitterParsed", "    FX = FX_TestBirdsA None FX_NoSuchList\n    FX = FX_TestBirdsB\n    SpawnObject = OCL_TestRabbit OCL_NoSuchList\n"
									 "    ReloadTime = 1000\n"));
	const CritterEmitterUpdateModuleData *d0 = critterData(f.w.get("TestEmitterDefaults"));
	REQUIRE(d0);
	CHECK(d0->m_fxLists.empty());
	CHECK(d0->m_spawnLists.empty());
	CHECK(d0->m_reloadTime == 0u);
	const CritterEmitterUpdateModuleData *d1 = critterData(f.w.get("TestEmitterParsed"));
	REQUIRE(d1);
	CHECK(d1->m_fxLists == std::vector<std::string>{ "FX_TestBirdsA", "", "", "FX_TestBirdsB" }); // a second FX line appends (RW pushes)
	CHECK(d1->m_spawnLists == std::vector<std::string>{ "OCL_TestRabbit", "OCL_NoSuchList" });
	CHECK(d1->m_reloadTime == 5u); // 1000 ms at 5 logic frames a second
}

// RW 0x8CD297 / RW 0x8CD1F6: a unit touching the emitter makes it play one FX and run one OCL (logic random picks, lines 127 / 138), once; the next update
// takes the emitter out of the collision manager for ReloadTime (no contact pairs), then it comes back and emits again
TEST_CASE("critter emitter: a contact emits once (logic random FX and OCL picks); the emitter leaves the collision manager for ReloadTime")
{
	CritterWorld f(emitter("TestEmitter", "    FX = FX_TestBirdsA FX_TestBirdsB None\n    SpawnObject = OCL_TestRabbit\n    ReloadTime = 1000\n"));
	f.buildMap();
	Object *e = f.spawn("TestEmitter", 505.0f, 505.0f);
	CritterEmitterUpdate *c = critterOf(e);
	REQUIRE(c);
	CHECK(c->collided() == false);
	CHECK(c->inManagerSinceCreation());
	f.frames(3);
	CHECK(c->emissions() == 0); // alone: no contact
	CHECK_FALSE(e->isOutOfCollisionManager());

	f.logic->random().clearCallLog();
	f.logic->random().enableCallLog(true);
	Object *walker = f.spawn("Walker", 525.0f, 505.0f); // 20 apart, radii 30 + 8: they overlap
	REQUIRE(walker);
	int framesToEmit = 0;
	while (c->emissions() == 0 && framesToEmit < 5)
	{
		f.frames(1);
		++framesToEmit;
	}
	f.logic->random().enableCallLog(false);
	REQUIRE(c->emissions() == 1);
	std::vector<int> lines;
	for (const auto &call : f.logic->random().callLog())
	{
		if (call.file == "CritterEmitter.cpp")
		{
			lines.push_back(call.line);
			if (call.line == 127)
			{
				CHECK(call.lo == 0);
				CHECK(call.hi == 2); // three FX entries
			}
			else
			{
				CHECK(call.hi == 0); // one OCL: GetGameLogicRandomValue(0, 0) (the generator still steps, RW 0x6D328E)
			}
		}
	}
	CHECK(lines == std::vector<int>{ 127, 138 });
	CHECK(f.fxAt("CritterEmitterUpdate") <= 1); // tolerance: 0 when the logic random picked the NULL ("None") entry, else 1
	CHECK(countTemplate(*f.logic, "Walker") == 2); // OCL_TestRabbit made one Walker

	// the update after the contact: out of the manager, both flags clear, asleep for ReloadTime
	f.frames(2);
	CHECK(e->isOutOfCollisionManager());
	CHECK_FALSE(c->collided());
	CHECK_FALSE(c->inManagerSinceCreation());
	f.ai->processCollisions();
	CHECK(f.ai->collisionPairsLastFrame() <= 1); // tolerance: the two Walkers may touch each other (the OCL places its one); never the emitter
	f.frames(1);
	CHECK(c->emissions() == 1); // the walkers still stand on it: nothing while it is out (ReloadTime 5 frames)
	CHECK(e->isOutOfCollisionManager());

	// the snapshot: retail's two bools, the manager state and the counter round-trip
	StateHasher before;
	c->crc(before);
	XferSave saved;
	c->xfer(saved);
	// past ReloadTime (5 frames from the update that took it out, 2 or 3 of them run): the update puts it back (and a walker's contact takes it out again)
	int back = 0;
	for (int i = 1; i <= 20 && back == 0; ++i)
	{
		f.frames(1);
		back = (!e->isOutOfCollisionManager() || c->emissions() > 1) ? i : 0;
	}
	CHECK(back >= 1); // tolerance: the frame the sleeping update wakes depends on where in the frame the contact fell
	CHECK(back <= 4); // tolerance: as above (ReloadTime 5 frames)
	{
		XferLoad load(saved.data());
		c->xfer(load);
	}
	StateHasher after;
	c->crc(after);
	CHECK(after.value() == before.value());
	CHECK(e->isOutOfCollisionManager());
	CHECK(c->emissions() == 1);
	std::vector<std::uint8_t> truncated = saved.data();
	truncated.pop_back();
	XferLoad lt(truncated);
	CHECK_THROWS_AS(c->xfer(lt), XferException);
}

// a lone emitter (no contact) leaves the state as before: nothing drawn, no FX, no object, the manager untouched; two equal games hash equal
TEST_CASE("critter emitter: without a contact the emitter draws nothing; equal games hash equal")
{
	std::uint32_t hashes[2] = {};
	for (int run = 0; run < 2; ++run)
	{
		CritterWorld f(emitter("TestEmitter", "    FX = FX_TestBirdsA FX_TestBirdsB\n    SpawnObject = OCL_TestRabbit\n    ReloadTime = 1000\n"));
		f.buildMap();
		Object *e = f.spawn("TestEmitter", 505.0f, 505.0f);
		f.spawn("Walker", 305.0f, 505.0f); // 200 away
		f.logic->random().clearCallLog();
		f.logic->random().enableCallLog(true);
		f.frames(30);
		f.logic->random().enableCallLog(false);
		for (const auto &call : f.logic->random().callLog())
		{
			CHECK(call.file != "CritterEmitter.cpp");
		}
		CHECK(critterOf(e)->emissions() == 0);
		CHECK_FALSE(e->isOutOfCollisionManager());
		CHECK(f.fxAt("CritterEmitterUpdate") == 0);
		hashes[run] = f.logic->computeStateHash();
	}
	CHECK(hashes[0] == hashes[1]);
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

// RotWK 2.01: BirdEmitter (natureunits.ini: FX = FX_Birds, ReloadTime = 20000) is the one template with the module. A retail soldier walking onto it plays
// FX_Birds once (one FX: GetGameLogicRandomValue(0, 0), the generator steps), takes it out of the collision manager for 100 frames; equal games hash equal
TEST_CASE("critter emitter retail: BirdEmitter plays FX_Birds once on a contact and rests 100 frames out of the collision manager")
{
	RetailShared &sh = retailShared();
	if (!sh.mount || !sh.mount->fs)
	{
		MESSAGE("SKIP: ROTWK_INSTALL / BFME2_INSTALL not set (retail CritterEmitterUpdate test)");
		return;
	}
	REQUIRE_MESSAGE(sh.world, sh.error);
	auto contextScope = sh.world->enterContext();
	std::vector<std::string> users;
	for (const ThingTemplate *t : sh.world->things().templates())
	{
		for (const auto &m : t->moduleList())
		{
			if (m.first == "CritterEmitterUpdate")
			{
				users.push_back(t->getName());
			}
		}
	}
	CHECK(users == std::vector<std::string>{ "BirdEmitter" });
	const CritterEmitterUpdateModuleData *d = critterData(sh.world->things().findTemplate("BirdEmitter"));
	REQUIRE(d);
	CHECK(d->m_fxLists == std::vector<std::string>{ "FX_Birds" });
	CHECK(d->m_spawnLists.empty()); // ";SpawnObject = OCL_CritterRabbit" is a comment
	CHECK(d->m_reloadTime == 100u); // 20000 ms at 5 logic frames a second

	std::uint32_t hashes[2] = {};
	for (int run = 0; run < 2; ++run)
	{
		TeamFactory teams;
		PlayerList players(sh.world->nameKeys(), sh.world->playerTemplates(), teams);
		SkirmishSetup setup;
		setup.players.push_back({ "Tester", "FactionMen", true, 0, 0, 0 });
		setup.players.push_back({ "Enemy", "FactionMordor", false, 1, 0, 1 });
		REQUIRE(players.setupSkirmish(setup).empty());
		GameLogic logic(sh.world->things(), sh.world->modules(), players, RandomAlgorithm::ZH_CarryChain);
		logic.random().seedRandom(1);
		std::string err;
		REQUIRE_MESSAGE(GameLogicSettingsLoader::load(*sh.mount->fs, logic.settings(), &err), err);
		logic.setUpgradeTypes(&sh.world->upgradeTypes());
		REQUIRE_MESSAGE(EconomySettings::load(*sh.mount->fs, logic.economy().settings(), &err), err);
		AIWorldConfig cfg;
		REQUIRE_MESSAGE(AIWorldConfigLoader::load(*sh.mount->fs, cfg, &err), err);
		pathtest::SyntheticTerrain terrain(100, 100);
		std::unique_ptr<AIWorld> ai = std::make_unique<AIWorld>(logic, cfg, sh.world->iniMacros());
		ai->attach();
		ai->newMap(terrain);
		logic.partition().setRegion(0.0f, 0.0f, 1000.0f, 1000.0f);
		Player *tester = players.findPlayerWithName("Tester");
		REQUIRE(tester);
		auto make = [&](const char *name, float x) {
			Object *o = logic.newObject(sh.world->things().findTemplate(name), tester->getDefaultTeam(), ObjectStatusMaskType{});
			REQUIRE(o);
			const Coord3D pos{ x, 300.0f, 0.0f };
			o->setPosition(&pos);
			return o;
		};
		Object *bird = make("BirdEmitter", 300.0f);
		CritterEmitterUpdate *c = critterOf(bird);
		REQUIRE(c);
		logic.runLogicFrame();
		logic.random().clearCallLog();
		logic.random().enableCallLog(true);
		make("GondorFighter", 310.0f);
		for (int i = 0; i < 5; ++i)
		{
			logic.runLogicFrame();
		}
		logic.random().enableCallLog(false);
		CHECK(c->emissions() == 1);
		CHECK(bird->isOutOfCollisionManager());
		const auto &sites = logic.fxEvents().perSite();
		const auto it = sites.find("CritterEmitterUpdate");
		CHECK((it == sites.end() ? 0ull : it->second) == 1ull); // FX_Birds
		for (const auto &call : logic.random().callLog())
		{
			if (call.file == "CritterEmitter.cpp")
			{
				CHECK(call.line == 127);
				CHECK(call.hi == 0); // one FX: (0, 0)
			}
		}
		for (int i = 0; i < 90; ++i)
		{
			logic.runLogicFrame();
		}
		CHECK(c->emissions() == 1); // still resting (100 frames from the update after the contact)
		for (int i = 0; i < 15; ++i)
		{
			logic.runLogicFrame();
		}
		CHECK(c->emissions() == 2); // back in the manager: the soldier still stands on it
		hashes[run] = logic.computeStateHash();
		logic.reset();
		ai.reset();
	}
	CHECK(hashes[0] == hashes[1]);
}
