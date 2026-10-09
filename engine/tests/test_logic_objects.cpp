// OpenBFME unit tests: object creation order, helpers, teams and ids (lane LOGIC-1).
//
// Expectations come from the RotWK Object constructor RW 0x69990F and newObject RW 0x6D165E (read from the disassembly, caveat S-001;
// the creation order is documented in GameLogic/Object/Object.h) and ZH Object.cpp / ThingFactory.cpp where cited.

#include "GameLogic/ScriptEngine/LuaScriptEngine.h"
#include "doctest.h"
#include "LogicTestUtil.h"

#include "Common/Player.h"
#include "GameClient/DrawableManager.h"
#include "GameClient/RenderInterpolation.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DDrawModules.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"
#include "GameLogic/MapObjectLoop.h"

#include <fstream>
#include <set>
#include <sstream>

using namespace logictest;

namespace
{
const char kObjects[] =
	"Object BaseVar\n"
	"  BuildVariations = VarB VarC\n"
	"  Behavior = LifetimeUpdate ModuleTag_L\n"
	"  End\n"
	"End\n"
	"Object VarB\n"
	"  Behavior = LifetimeUpdate ModuleTag_L\n"
	"  End\n"
	"  Behavior = AutoHealBehavior ModuleTag_H\n"
	"  End\n"
	"End\n"
	"Object VarC\n"
	"  Behavior = FlammableUpdate ModuleTag_F\n"
	"  End\n"
	"End\n"
	"Object Plain\n"
	"End\n"
	"Object One\n"
	"  Behavior = LifetimeUpdate ModuleTag_L\n"
	"  End\n"
	"End\n"
	"Object Repulsable\n"
	"  KindOf = CAN_BE_REPULSED\n"
	"  WeaponSet\n"
	"    Conditions = None\n"
	"    Weapon = PRIMARY SomeSword\n"
	"  End\n"
	"End\n"
	"Object Shrub\n"
	"  KindOf = SHRUBBERY\n"
	"End\n"
	"Object Rock\n"
	"  KindOf = ROCK\n"
	"End\n"
	"Object Vendor\n"
	"  KindOf = ROCK_VENDOR\n"
	"End\n"
	"Object UnarmedSet\n"
	"  WeaponSet\n"
	"    Conditions = None\n"
	"    Weapon = PRIMARY None\n"
	"  End\n"
	"End\n"
	"Object BadKind\n"
	"  KindOf = NOT_A_KIND\n"
	"End\n";

std::vector<std::string> helperNames(const Object *o)
{
	std::vector<std::string> out;
	for (size_t i = 0; i < o->helperCount(); ++i)
	{
		out.push_back(o->modules()[i]->getModuleClassName());
	}
	return out;
}

struct Fx : LogicWorld
{
	Fx()
	{
		REQUIRE_MESSAGE(loadError.empty(), loadError);
		const std::string err = w.load(kObjects);
		REQUIRE_MESSAGE(err.empty(), err);
	}
};
} // namespace

TEST_CASE("logic objects: the creation order is build variation, ctor (modules in list order, onObjectCreated, registerObject), onCreate, initObject, hook (spec 5.3; RW 0x6D165E)")
{
	Fx f;
	auto l = f.bind("LifetimeUpdate");
	l->hasCreate = true;
	auto h = f.bind("AutoHealBehavior");
	auto fl = f.bind("FlammableUpdate");
	(void)h;
	(void)fl;
	f.logic->setClientHooks(&f.hooks);
	// LUA-1's function itself (no script state: the draw and the drawable still happen), with a log line after it standing for the dispatch
	f.logic->setObjectCreatedProc([&](Object &o, const std::function<void()> &bindDrawable) {
		const int seed = LuaScriptEngine::sendObjectCreated(f.logic->random(), nullptr, nullptr, bindDrawable);
		f.log.add("hook#" + std::to_string(o.getID()));
		return seed;
	});
	f.logic->random().enableCallLog(true);
	// the variation the same generator would pick (ZH carry chain seeded 1, GameLogicRandomValue(0, 1))
	GameLogicRandom ref(RandomAlgorithm::ZH_CarryChain);
	ref.seedRandom(1);
	const int pick = ref.getValue(0, 1, "ThingFactory.cpp", 0x20B);
	Object *o = f.make("BaseVar");
	// two draws, in this order: the build variation (newObject, step 1) and sendObjectCreated's unconditional one (initObject, RW 0x628892)
	REQUIRE(f.logic->random().callLog().size() == 2);
	CHECK(f.logic->random().callLog()[0].lo == 0);
	CHECK(f.logic->random().callLog()[0].hi == 1);
	CHECK(f.logic->random().callLog()[0].file == "ThingFactory.cpp");
	CHECK(f.logic->random().callLog()[0].line == 0x20B);
	CHECK(f.logic->random().callLog()[0].result == pick);
	CHECK(f.logic->random().callLog()[1].lo == 1);
	CHECK(f.logic->random().callLog()[1].hi == 999);
	CHECK(f.logic->random().callLog()[1].file == "GameLogic.cpp");
	CHECK(f.logic->random().callLog()[1].line == 0x19A7);
	CHECK(o->getCreationSeed() == f.logic->random().callLog()[1].result);
	CHECK(o->getCreationSeed() >= 1);
	CHECK(o->getCreationSeed() <= 999);
	CHECK(o->getTemplate()->getName() == (pick == 0 ? "VarB" : "VarC"));
	if (pick == 0)
	{
		// VarB: LifetimeUpdate then AutoHealBehavior; every module exists when the first onObjectCreated runs, none is registered yet
		const std::vector<std::string> expect = {
			"ctor:LifetimeUpdate", "ctor:AutoHealBehavior",
			"onObjectCreated:LifetimeUpdate modules=6 notInList", "onObjectCreated:AutoHealBehavior modules=6 notInList",
			"onCreate:LifetimeUpdate", "client:created#1", "hook#1" };
		CHECK(f.log.events == expect);
	}
	else
	{
		const std::vector<std::string> expect = { "ctor:FlammableUpdate", "onObjectCreated:FlammableUpdate modules=5 notInList", "client:created#1", "hook#1" };
		CHECK(f.log.events == expect);
	}
	CHECK(f.logic->getObjectCount() == 1);
}

TEST_CASE("logic objects: a template without BuildVariations draws only sendObjectCreated's unconditional (1, 999) from the RNG (RW 0x628892)")
{
	Fx f;
	f.logic->random().enableCallLog(true);
	f.make("Plain");
	REQUIRE(f.logic->random().callLog().size() == 1);
	CHECK(f.logic->random().callLog()[0].lo == 1);
	CHECK(f.logic->random().callLog()[0].hi == 999);
	CHECK(f.logic->random().callLog()[0].line == 0x19A7);
}

TEST_CASE("logic objects: an installed ObjectCreatedProc (LUA-1's sendObjectCreated) owns the creation order and gets the drawable bind as a closure")
{
	Fx f;
	f.logic->setClientHooks(&f.hooks);
	int calls = 0;
	f.logic->setObjectCreatedProc([&](Object &o, const std::function<void()> &bindDrawable) {
		++calls;
		f.log.add("proc:begin#" + std::to_string(o.getID()));
		bindDrawable(); // the client hook: makes the drawable
		f.log.add("proc:end#" + std::to_string(o.getID()));
		return 321;
	});
	f.logic->random().enableCallLog(true);
	Object *o = f.make("Plain");
	CHECK(calls == 1);
	CHECK(o->getCreationSeed() == 321);
	CHECK(f.logic->random().callLog().empty()); // the proc draws, GameLogic does not draw a second time
	CHECK(f.log.events == std::vector<std::string>{ "proc:begin#1", "client:created#1", "proc:end#1" });
	// with a proc installed no dispatch is missing: no S-147 line
	for (const std::string &l : f.logic->report().stops)
	{
		CHECK(l.rfind("[S-147]", 0) != 0);
	}
	CHECK(f.logic->creationsWithoutDispatch() == 0);
}

TEST_CASE("logic objects: the helper modules are made in the retail order under the retail conditions (RW 0x69A0A9 .. 0x69A327)")
{
	Fx f;
	const std::vector<std::string> all = { "SMCHelper", "RecoveryHelper", "RepulsorHelper", "DefectionHelper", "GuardingHelper", "WeaponStatusHelper", "FiringTrackerHelper" };
	CHECK(helperNames(f.make("Repulsable")) == all);
	CHECK(helperNames(f.make("Plain")) == std::vector<std::string>{ "SMCHelper", "RecoveryHelper", "DefectionHelper", "GuardingHelper" });
	// no defection helper for SHRUBBERY, ROCK or ROCK_VENDOR
	const std::vector<std::string> noDefection = { "SMCHelper", "RecoveryHelper", "GuardingHelper" };
	CHECK(helperNames(f.make("Shrub")) == noDefection);
	CHECK(helperNames(f.make("Rock")) == noDefection);
	CHECK(helperNames(f.make("Vendor")) == noDefection);
	// a weapon set whose only weapon is None cannot have a weapon
	CHECK(helperNames(f.make("UnarmedSet")) == std::vector<std::string>{ "SMCHelper", "RecoveryHelper", "DefectionHelper", "GuardingHelper" });
	// AIData EnableRepulsors off: no repulsor helper
	f.logic->settings().enableRepulsors = false;
	CHECK(helperNames(f.make("Repulsable")) == std::vector<std::string>{ "SMCHelper", "RecoveryHelper", "DefectionHelper", "GuardingHelper", "WeaponStatusHelper", "FiringTrackerHelper" });
	// every helper starts asleep: the scheduler holds it in `sleeping`
	for (const UpdateModule *u : f.logic->sleepingVector())
	{
		CHECK(u->friend_getNextCallFrame() == (UnsignedInt)UPDATE_SLEEP_FOREVER);
	}
}

TEST_CASE("logic objects: the team is the one given, else the neutral player's default team; members are kept in creation order")
{
	Fx f;
	Team *alice = f.teamOf("Alice");
	Object *a = f.make("Plain", alice);
	Object *b = f.make("Plain", alice);
	Object *n = f.make("Plain");
	CHECK(a->getTeam() == alice);
	CHECK(a->getControllingPlayer()->getPlayerName() == "Alice");
	CHECK(alice->getMemberCount() == 2);
	CHECK(alice->getFirstMember() == a);
	CHECK(a->friend_teamNext() == b);
	CHECK(n->getTeam() == f.players.getNeutralPlayer()->getDefaultTeam());
	b->setTeam(f.teamOf("Bob"));
	CHECK(alice->getMemberCount() == 1);
	CHECK(f.teamOf("Bob")->getFirstMember() == b);
	// destroying removes the member
	f.logic->destroyObject(a);
	f.logic->processDestroyList();
	CHECK(alice->getMemberCount() == 0);
	CHECK(alice->getFirstMember() == nullptr);
}

TEST_CASE("logic objects: an explicit id is used and later ids stay above it; a duplicate id is an error")
{
	Fx f;
	Object *a = f.make("Plain", nullptr, 77);
	CHECK(a->getID() == 77);
	CHECK(f.make("Plain")->getID() == 78);
	CHECK_THROWS_AS(f.make("Plain", nullptr, 77), std::logic_error);
}

TEST_CASE("logic objects: a template's KindOf is decoded by the binary's names and a bad name is reported, never dropped")
{
	Fx f;
	Object *r = f.make("Repulsable");
	CHECK(r->isKindOfName("CAN_BE_REPULSED"));
	CHECK_FALSE(r->isKindOfName("SHRUBBERY"));
	f.make("BadKind");
	const GameLogic::Report rep = f.logic->report();
	bool found = false;
	for (const std::string &e : rep.errors)
	{
		found = found || e.find("BadKind") != std::string::npos && e.find("NOT_A_KIND") != std::string::npos;
	}
	CHECK(found);
}

TEST_CASE("logic objects: a missing neutral default team is an error, not a silent null (PLAN rule 10)")
{
	LogicWorld w0;
	REQUIRE(w0.loadError.empty());
	REQUIRE(w0.w.load("Object Plain\nEnd\n").empty());
	TeamFactory teams2;
	PlayerTemplateStore store2(w0.w.keys);
	PlayerList bare(w0.w.keys, store2, teams2); // init(): the neutral player has no default team
	GameLogic logic2(w0.w.things, w0.w.modules, bare, RandomAlgorithm::ZH_CarryChain);
	CHECK_THROWS_AS(logic2.newObject(w0.w.get("Plain"), nullptr, ObjectStatusMaskType{}), std::logic_error);
}

TEST_CASE("logic report: unported module classes are counted per class and per object (stop S-140); helpers per name (S-141)")
{
	Fx f;
	f.bind("LifetimeUpdate");
	Object *a = f.make("VarB"); // LifetimeUpdate (scripted) + AutoHealBehavior (unported: AutoHealBehavior is bound here? no)
	(void)a;
	f.make("VarB");
	f.make("VarC"); // FlammableUpdate unported
	const GameLogic::Report r = f.logic->report();
	REQUIRE(r.unportedModules.count("AutoHealBehavior"));
	CHECK(r.unportedModules.at("AutoHealBehavior").modules == 2);
	CHECK(r.unportedModules.at("AutoHealBehavior").objects == 2);
	CHECK(r.unportedModules.at("FlammableUpdate").modules == 1);
	CHECK_FALSE(r.unportedModules.count("LifetimeUpdate"));
	CHECK(r.unportedCreated.at("AutoHealBehavior") == 2);
	CHECK(r.helperShells.at("SMCHelper") == 3);
	CHECK(r.helperShells.at("GuardingHelper") == 3);
	CHECK(r.unportedUpdateCalls == 0);
	// the stop lines are present
	bool s140 = false, s141 = false, s142 = false, s143 = false, s147 = false;
	for (const std::string &l : r.stops)
	{
		s140 = s140 || l.rfind("[S-140]", 0) == 0;
		s141 = s141 || l.rfind("[S-141]", 0) == 0;
		s142 = s142 || l.rfind("[S-142]", 0) == 0;
		s143 = s143 || l.rfind("[S-143]", 0) == 0;
		s147 = s147 || l.rfind("[S-147]", 0) == 0;
	}
	CHECK((s140 && s141 && s142 && s143 && s147));
	// an unported update module sleeps forever and a woken one counts its calls
	f.logic->runLogicFrame();
	CHECK(f.logic->report().unportedUpdateCalls == 0);
	CHECK(r.creationsWithoutDispatch == 3); // no proc installed: counted
}

TEST_CASE("logic objects: onDrawableBoundToObject reaches every module when a drawable is bound")
{
	Fx f;
	f.bind("LifetimeUpdate");
	Object *a = f.make("VarB");
	f.log.events.clear();
	a->friend_bindToClient(nullptr);
	CHECK(f.log.has("onDrawableBound:LifetimeUpdate"));
	CHECK(a->clientHooks() == nullptr);
}

TEST_CASE("logic stops: every stop line of the lane is registered in docs/STOPS.md, S-140 .. S-152 are all carried by a report")
{
	Fx f;
	f.make("Plain");
	f.logic->settings().unappliedFields.insert("GameData.SomeField"); // what the settings loader leaves unread (S-152)
	std::vector<std::string> lines = f.logic->report().stops;
	for (const std::string &l : MapObjectLoop::stopLines())
	{
		lines.push_back(l);
	}
	lines.push_back(DrawableManager::interpolationStopLine());
	for (const std::string &l : RenderInterpolation::stopLines())
	{
		lines.push_back(l); // SMOOTH-1
	}
	for (const std::string &l : f.templates.unverified())
	{
		lines.push_back(l);
	}
	std::ifstream in(std::string(OPENBFME_DOCS_DIR) + "/STOPS.md");
	REQUIRE_MESSAGE(static_cast<bool>(in), "cannot read docs/STOPS.md (OPENBFME_DOCS_DIR = " << OPENBFME_DOCS_DIR << ")");
	std::stringstream ss;
	ss << in.rdbuf();
	const std::string doc = ss.str();
	std::set<std::string> ids;
	for (const std::string &line : lines)
	{
		REQUIRE(line.size() > 7);
		REQUIRE(line.compare(0, 3, "[S-") == 0);
		size_t end = 3;
		while (end < line.size() && line[end] >= '0' && line[end] <= '9')
		{
			++end;
		}
		const std::string id = line.substr(1, end - 1); // "S-151", "S-1140" (lane SMOOTH-2: four-digit ids), "S-320" of "[S-320..S-328 counters]"
		ids.insert(id);
		CHECK_MESSAGE(doc.find("| " + id + " |") != std::string::npos, "docs/STOPS.md has no row for " << id);
	}
	// S-148 (DisabledType names) is a code comment + row with no runtime line: an opaque mask has nothing to report
	for (int n = 140; n <= 152; ++n)
	{
		if (n == 148)
		{
			continue;
		}
		CHECK_MESSAGE(ids.count("S-" + std::to_string(n)) == 1, "no report carries S-" << n);
	}
	CHECK(doc.find("| S-148 |") != std::string::npos);
}

TEST_CASE("logic settings: a missing key or block is an error naming it, never a default (PLAN rule 10)")
{
	GameLogicSettings g;
	std::string err;
	const std::string good = "#define X 1\nGameData\n  ForceModelsToFollowTimeOfDay = Yes\n  ForceModelsToFollowWeather = No ; comment\n  UnitDamagedThreshold = 0.5f\n  UnitReallyDamagedThreshold = 0.25\n  DefaultStartingCash = 700\nEnd\n";
	REQUIRE(GameLogicSettingsLoader::scanGameData(good, g, &err));
	CHECK(g.forceModelsToFollowTimeOfDay);
	CHECK_FALSE(g.forceModelsToFollowWeather);
	CHECK(g.unitDamagedThreshold == 0.5f);
	CHECK(g.unitReallyDamagedThreshold == 0.25f);
	CHECK(g.defaultStartingCash == 700);
	CHECK(g.bodyThresholdsLoaded);
	GameLogicSettings h;
	CHECK_FALSE(GameLogicSettingsLoader::scanGameData("GameData\n  ForceModelsToFollowTimeOfDay = Yes\nEnd\n", h, &err));
	CHECK(err.find("ForceModelsToFollowWeather") != std::string::npos);
	CHECK_FALSE(GameLogicSettingsLoader::scanGameData("AIData\nEnd\n", h, &err)); // a block of another name is not the GameData block
	CHECK(err.find("no GameData block") != std::string::npos);
	CHECK_FALSE(GameLogicSettingsLoader::scanGameData(good.substr(0, good.find("DefaultStartingCash")) + "End\n", h, &err));
	CHECK(err.find("DefaultStartingCash") != std::string::npos);
	CHECK_FALSE(GameLogicSettingsLoader::scanAIData("AIData\nEnd\n", h, &err));
	CHECK(err.find("EnableRepulsors") != std::string::npos);
	REQUIRE(GameLogicSettingsLoader::scanAIData("AIData\n  EnableRepulsors = No\n  RepulsedDistance = 120.0\nEnd\n", h, &err));
	CHECK_FALSE(h.enableRepulsors);
	CHECK_FALSE(GameLogicSettingsLoader::scanMultiplayer("MultiplayerSettings\n  InitialCreditsLow = 1\nEnd\n", h, &err));
	CHECK(err.find("InitialCredits") != std::string::npos);
	REQUIRE(GameLogicSettingsLoader::scanMultiplayer("MultiplayerSettings\n  InitialCreditsVeryLow = 1\n  InitialCreditsLow = 2\n  InitialCreditsMedium = 3\n  InitialCreditsHigh = 4\n  InitialCreditsVeryHigh = 5\nEnd\n", h, &err));
	CHECK(h.initialCredits[4] == 5);
	CHECK(h.startingCashLoaded);
}

TEST_CASE("logic drawables: the ClientUpdate and ClientBehavior modules of a template are made, in list order, as explicit unported modules and reported (S-140)")
{
	LogicWorld lw;
	REQUIRE(lw.loadError.empty());
	W3DDrawModules::registerTypedDrawModuleData(lw.w.modules);
	REQUIRE(lw.w.load(
		"Object Swayer\n"
		"  Draw = W3DDefaultDraw ModuleTag_Draw\n  End\n"
		"  ClientUpdate = SwayClientUpdate ModuleTag_Sway\n  End\n"
		"  ClientUpdate = BeaconClientUpdate ModuleTag_Beacon\n    RadarPulseFrequency = 5\n  End\n"
		"  ClientBehavior = TerrainResourceClientBehavior ModuleTag_CB\n  End\n"
		"End\n").empty());
	ArchiveW3DFileSource source(lw.w.fx.fsys);
	WW3DAssetManager assets(source);
	DrawableManager dm(assets, *lw.logic);
	ClientEventRecorder rec(*lw.logic); // SMOOTH-1: the logic's calls are events the render side applies
	lw.logic->setClientHooks(&rec);
	Object *o = lw.make("Swayer");
	dm.applyEvents(rec.take());
	const Drawable *d = dm.findByObject(o->getID());
	REQUIRE(d);
	REQUIRE(d->clientModules().size() == 3);
	CHECK(d->clientModules()[0]->getModuleClassName() == "SwayClientUpdate");
	CHECK(d->clientModules()[1]->getModuleClassName() == "BeaconClientUpdate");
	CHECK(d->clientModules()[2]->getModuleClassName() == "TerrainResourceClientBehavior");
	for (const auto &m : d->clientModules())
	{
		CHECK(m->isUnported());
		CHECK(m->getDrawable() == d);
		CHECK(m->getModuleTagNameKey() != NAMEKEY_INVALID);
	}
	// the draw module list: W3DDefaultDraw draws nothing in a release build (S-113 family), and says so
	REQUIRE(d->entries().size() == 1);
	CHECK(d->entries()[0].kind == W3D_DRAWKIND_NOTHING);
	CHECK(d->entries()[0].notDrawnReason.find("draws nothing") != std::string::npos);
	const DrawableManager::Report r = dm.report();
	CHECK(r.unportedClientModules.at("SwayClientUpdate") == 1);
	CHECK(r.unportedClientModules.at("BeaconClientUpdate") == 1);
	CHECK(r.unportedClientModules.at("TerrainResourceClientBehavior") == 1);
	CHECK(r.notDrawnEntries == 1);
	bool s140 = false;
	for (const std::string &l : r.stops)
	{
		s140 = s140 || l.rfind("[S-140] client modules: 3", 0) == 0;
	}
	CHECK(s140);
	CHECK(lw.w.modules.unportedCreated().at("SwayClientUpdate") == 1);
	lw.logic->reset();
	lw.logic->setClientHooks(nullptr);
}

TEST_CASE("logic objects: an object is findable by id from its constructor on, before registerObject (RW 0x68BC01 setID adds it to the lookup)")
{
	Fx f;
	auto s = f.bind("LifetimeUpdate");
	Object *seenInCreated = nullptr;
	bool inList = true;
	s->onObjectCreatedHook = [&](ScriptModule &m) {
		seenInCreated = f.logic->findObjectByID(m.getObject()->getID());
		inList = f.logic->getFirstObject() == m.getObject();
	};
	Object *o = f.make("One");
	CHECK(seenInCreated == o);   // findable while its modules are told the object exists ...
	CHECK_FALSE(inList);         // ... though it is not in the object list yet
	CHECK(f.logic->findObjectByID(o->getID()) == o);
	// a constructor that throws leaves nothing behind
	CHECK_THROWS_AS(f.make("Plain", nullptr, o->getID()), std::logic_error);
	CHECK(f.logic->findObjectByID(o->getID()) == o);
}

TEST_CASE("logic objects: the world entry and exit seams run at the end of the constructor and the start of the destructor (RW 0x69A6C6, 0x69A89D)")
{
	Fx f;
	std::vector<std::string> log;
	f.logic->setWorldHooks([&](Object &o) { log.push_back("enter#" + std::to_string(o.getID()) + (f.logic->findObjectByID(o.getID()) == &o ? " found" : " missing") + (f.logic->getLastObject() == &o ? " listed" : " unlisted")); },
		[&](Object &o) { log.push_back("leave#" + std::to_string(o.getID())); });
	Object *o = f.make("Plain");
	f.logic->destroyObject(o);
	f.logic->processDestroyList();
	CHECK(log == std::vector<std::string>{ "enter#1 found listed", "leave#1" });
}
