// OpenBFME. GPL-3.0. BFME1 only; mounted 1.03 scripts are the strategy oracle.
#include "doctest.h"
#include "RetailTestMount.h"
#include "Common/ArchiveFile.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/XferSave.h"
#include "Common/XferLoad.h"
#include "Common/SpecialPower.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"
#include "GameLogic/Object/RetailObjectWorld.h"
#include "GameClient/MapObjectDrawables.h"
#include "GameClient/GUI/Skirmish/IniSkirmishSetupSource.h"
#include "GameClient/LiveGame.h"
#include "GameClient/DrawableManager.h"
#include "GameClient/MapScriptSetup.h"
#include "GameLogic/NewGame/NewGame.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Combat/CombatState.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/Module/CastleModules.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/StructureModules.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/ScriptEngine/ScriptEngine.h"
#include "GameLogic/ScriptEngine/ScriptConditions.h"
#include "GameLogic/SkirmishAI/SkirmishAIManager.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <set>
#include <sstream>
#include <cstring>

namespace
{
struct Fixture
{
	Win32BIGFileSystem fs;
	std::unique_ptr<RetailObjectWorld> world;
	MapObjectOptions options;
	GameLogicSettings settings;
	std::vector<MapCacheEntry> maps;
	Fixture(const char *install)
	{
		std::filesystem::path root = std::filesystem::u8path(install);
		std::vector<std::string> names;
		for (const auto &entry : std::filesystem::recursive_directory_iterator(root))
		{
			if (!entry.is_regular_file() || entry.path().filename().u8string().rfind("!!!", 0) == 0) continue;
			std::string extension = entry.path().extension().u8string();
			std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return std::tolower(c); });
			if (extension == ".big") names.push_back(entry.path().lexically_relative(root).u8string());
		}
		std::string error;
		for (const auto &name : BFMEArchiveLoadOrder(names))
		{
			auto archive = fs.openArchiveFile((root / std::filesystem::u8path(name)).u8string(), &error);
			REQUIRE_MESSAGE(archive, error);
			fs.mountArchive(std::move(archive), name, false);
		}
		REQUIRE(isBfme1Data(fs));
		world = std::make_unique<RetailObjectWorld>(fs);
		REQUIRE_MESSAGE(world->load(&error), error);
		REQUIRE(world->report().errors.empty());
		REQUIRE_MESSAGE(MapObjectGameData::load(fs, options, &error), error);
		REQUIRE_MESSAGE(MapObjectGameData::loadPlayerTemplates(fs, options, &error), error);
		REQUIRE_MESSAGE(MapCreationHooks::load(fs, options.creationScripts, &error), error);
		REQUIRE_MESSAGE(GameLogicSettingsLoader::load(fs, settings, &error), error);
		REQUIRE_MESSAGE(IniSkirmishSetupSource::loadMapCache(fs, maps, &error), error);
	}
	int faction(const std::string &side)
	{
		for (int i = 0; i < world->playerTemplates().getPlayerTemplateCount(); ++i)
			if (world->playerTemplates().getNthPlayerTemplate(i)->m_side == side) return i;
		return -1;
	}
};
}

TEST_CASE("bfme1 ai retail: original faction libraries construct, train and attack in a real skirmish")
{
	const char *install = std::getenv("BFME1_INSTALL");
	if (!install || !*install) { retailtest::printSkip("BFME1 skirmish AI (BFME1_INSTALL unset)"); return; }
	Fixture fx(install);
	for (const std::string side : { "Gondor", "Rohan", "Mordor", "Isengard" })
	{
		INFO(side);
		NewGameMessage message;
		message.game.mapName = "maps/map mp fords of isen/map mp fords of isen.map";
		message.game.seed = 9001;
		message.game.startingCash = 2000;
		for (int i = 0; i < 2; ++i)
		{
			auto &slot = message.game.slots[i];
			slot.state = i == 0 ? SLOT_PLAYER : SLOT_MED_AI;
			slot.name = i == 0 ? u"Human" : u"Computer";
			slot.playerTemplate = fx.faction(i == 0 ? "Rohan" : side);
			REQUIRE(slot.playerTemplate != -1);
			slot.startPos = i; slot.color = i; slot.teamNumber = i;
		}
		std::string error;
		NewGameStart start(RandomAlgorithm::ZH_CarryChain);
		REQUIRE_MESSAGE(NewGame::prepareNewGame(message, fx.world->playerTemplates(), fx.settings, fx.maps, RandomAlgorithm::ZH_CarryChain, start, &error), error);
		ArchiveW3DFileSource source(fx.fs);
		WW3DAssetManager assets(source);
		LiveGame game(*fx.world, fx.fs, assets, fx.options);
		LiveGame::Options opt; opt.start = &start;
		REQUIRE_MESSAGE(game.load(opt, &error), error);
		GameLogic &logic = game.logic();
		Player *computer = logic.players().findPlayerWithName(game.report().startSlotPlayers[1]);
		Player *human = logic.players().findPlayerWithName(game.report().startSlotPlayers[0]);
		REQUIRE(computer); REQUIRE(human);
		REQUIRE(logic.skirmishAI().bfme1());
		int attacked = 0;
		logic.combat().setDamageTrace([&](const Object &victim, unsigned sourceId, float dealt, float) {
			const Object *attacker = logic.findObjectByID(sourceId);
			if (dealt > 0 && victim.getControllingPlayer() == human && attacker && attacker->getControllingPlayer() == computer) ++attacked;
		});
		bool baseNamed = false, built = false, trained = false;
		for (int frame = 0; frame < 4500; ++frame)
		{
			game.advance(0.2);
			for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
			{
				if (o->getControllingPlayer() != computer) continue;
				if (o->getName() == "BASE_FLAG_2") baseNamed = dynamic_cast<CastleBehavior *>(o->findModule("CastleBehavior")) != nullptr;
				Object *producer = logic.findObjectByID(o->getProducerID());
				if (!producer || o->isUnderConstruction()) continue;
				if (o->isKindOfName("STRUCTURE") && producer->isKindOfName("BASE_FOUNDATION")) built = true;
				if (o->isKindOfName("HORDE") && producer->getProductionUpdate()) trained = true;
			}
			if (built && trained && attacked) break;
		}
		MESSAGE(side << " built=" << built << " trained=" << trained << " hits=" << attacked << " money=" << computer->getMoney()->countMoney());
		if (!built || !trained || !attacked)
			for (const auto &line : logic.scriptEngine().report()) MESSAGE(line);
		if (!attacked) for (const auto &line : logic.skirmishAI().report()) MESSAGE(line);
		CHECK(baseNamed);
		CHECK(built);
		CHECK(trained);
		CHECK(attacked > 0); // tolerance: outcome gate; scripts choose team size and targets, not a fixed hit count.
		// The pending teams, orders, production-condition timers and threat caches
		// survive a native AI-block round trip without altering the state hash.
		XferSave save;
		logic.skirmishAI().xfer(save);
		const auto before = logic.computeStateHash();
		XferLoad load(save.data());
		logic.skirmishAI().xfer(load);
		CHECK(logic.computeStateHash() == before);
		XferSave again; logic.skirmishAI().xfer(again);
		CHECK(again.data() == save.data());
	}
}

TEST_CASE("bfme1 ai retail isolation: BFME2 and RotWK load the shared settings and start with their existing AI")
{
	for (retailtest::Mount *mount : { retailtest::bfme2Mount(), retailtest::pureMount() })
	{
		if (!mount) { retailtest::printSkip("BFME1 AI isolation (BFME2_INSTALL / ROTWK_INSTALL unset)"); continue; }
		REQUIRE_MESSAGE(mount->error.empty(), mount->error);
		REQUIRE(mount->fs);
		auto &fs = *mount->fs;
		CHECK_FALSE(isBfme1Data(fs));
		RetailObjectWorld world(fs);
		std::string error;
		REQUIRE_MESSAGE(world.load(&error), error);
		for (const auto &problem : world.report().errors)
		{
			// Existing BFME2-only Living World gap, before AI/settings startup;
			// it is reported and does not prevent ordinary skirmish startup.
			CHECK(isBfme2Data(fs));
			CHECK(problem.message.find("[S-1321] Living World field 'AddPlayer' is not ported") != std::string::npos);
			CHECK(problem.message.find("WOTRTutorial.inc") != std::string::npos);
			MESSAGE(problem.message);
		}
		MapObjectOptions options;
		GameLogicSettings settings;
		std::vector<MapCacheEntry> maps;
		REQUIRE_MESSAGE(MapObjectGameData::load(fs, options, &error), error);
		REQUIRE_MESSAGE(MapObjectGameData::loadPlayerTemplates(fs, options, &error), error);
		REQUIRE_MESSAGE(MapCreationHooks::load(fs, options.creationScripts, &error), error);
		REQUIRE_MESSAGE(GameLogicSettingsLoader::load(fs, settings, &error), error);
		REQUIRE_MESSAGE(IniSkirmishSetupSource::loadMapCache(fs, maps, &error), error);
		const auto map = std::find_if(maps.begin(), maps.end(), [](const MapCacheEntry &m) { return m.isMultiplayer && m.numPlayers == 2; });
		REQUIRE(map != maps.end());
		int faction = -1;
		for (int i = 0; i < world.playerTemplates().getPlayerTemplateCount(); ++i)
			if (world.playerTemplates().getNthPlayerTemplate(i)->m_playableSide) { faction = i; break; }
		REQUIRE(faction != -1);
		NewGameMessage message;
		message.game.mapName = map->name;
		message.game.seed = 9001;
		message.game.startingCash = 2000;
		for (int i = 0; i < 2; ++i)
		{
			auto &slot = message.game.slots[i];
			slot.state = i == 0 ? SLOT_PLAYER : SLOT_MED_AI;
			slot.name = i == 0 ? u"Human" : u"Computer";
			slot.playerTemplate = faction;
			slot.startPos = i; slot.color = i; slot.teamNumber = i;
		}
		NewGameStart start(RandomAlgorithm::ZH_CarryChain);
		REQUIRE_MESSAGE(NewGame::prepareNewGame(message, world.playerTemplates(), settings, maps, RandomAlgorithm::ZH_CarryChain, start, &error), error);
		ArchiveW3DFileSource source(fs);
		WW3DAssetManager assets(source);
		LiveGame game(world, fs, assets, options);
		LiveGame::Options opt; opt.start = &start;
		REQUIRE_MESSAGE(game.load(opt, &error), error);
		CHECK_FALSE(game.logic().skirmishAI().bfme1());
		CHECK(game.logic().skirmishAI().groups().size() == 1);
		for (int i = 0; i < 12; ++i) game.advance(0.2);
		CHECK(game.logic().skirmishAI().groups().front()->active);
	}
}

#include "ProdTestUtil.h"
#include "ScriptTestUtil.h"

TEST_CASE("bfme1 ai: importing libraries shadow named scripts across groups before difficulty filtering")
{
	using namespace scripttest;
	ScriptList faction, dependency;
	auto hard = script("Initialize", {{cond("CONDITION_TRUE", {})}}, {act("SET_COUNTER", {sp(4, "Launch"), ip(0, 650)})});
	hard->easy = hard->normal = false;
	std::vector<ScriptItem> specific;
	specific.push_back(item(std::move(hard)));
	specific.push_back(item(script("Initialize Medium", {{cond("CONDITION_TRUE", {})}}, {act("SET_COUNTER", {sp(4, "Launch"), ip(0, 400)})})));
	faction.items.push_back(group("Faction", true, false, std::move(specific)));
	std::vector<ScriptItem> defaults;
	defaults.push_back(item(script("Initialize", {{cond("CONDITION_TRUE", {})}}, {act("SET_COUNTER", {sp(4, "Launch"), ip(0, 600)})})));
	dependency.items.push_back(group("Defaults", true, false, std::move(defaults)));
	ScriptGame game;
	game.lists[1] = MapScriptSetup::mergeBfme1Scripts({ &faction, &dependency });
	game.start();
	game.frame();
	REQUIRE(game.engine().findCounter("Alice/Launch"));
	CHECK(game.engine().findCounter("Alice/Launch")->value == 400);
}

TEST_CASE("bfme1 ai: a shadowed group stops automatic traversal but retains its named subroutines")
{
	using namespace scripttest;
	ScriptList faction, dependency;
	faction.items.push_back(group("Shared", true, false, {}));
	std::vector<ScriptItem> defaults;
	auto retained = script("Retained", {{cond("CONDITION_TRUE", {})}}, {act("INCREMENT_COUNTER", {ip(0, 1), sp(4, "Calls")})});
	retained->isSubroutine = true;
	defaults.push_back(item(std::move(retained)));
	dependency.items.push_back(group("Shared", true, false, std::move(defaults)));
	ScriptGame game;
	game.lists[1] = MapScriptSetup::mergeBfme1Scripts({ &faction, &dependency });
	game.start();
	game.frame();
	CHECK_FALSE(game.engine().findCounter("Alice/Calls"));
	game.engine().executeAction(act("CALL_SUBROUTINE", {sp(13, "Alice/Retained")}));
	REQUIRE(game.engine().findCounter("Alice/Calls"));
	CHECK(game.engine().findCounter("Alice/Calls")->value == 1);
}

TEST_CASE("bfme1 ai: legacy production settings retain retail defaults and real-valued TeamSeconds")
{
	GameLogicSettings s;
	std::string error;
	REQUIRE(GameLogicSettingsLoader::scanAIData("AIData\nEnd\n", s, &error));
	CHECK(s.legacyAI.teamSeconds == 0.0f);
	CHECK(s.legacyAI.wealthyRate == 0.0f);
	CHECK(s.legacyAI.poorRate == 0.0f);
	CHECK(s.legacyAI.resourcesToStart == 0.0f);
	REQUIRE(GameLogicSettingsLoader::scanAIData("AIData\n TeamSeconds = 2.5\n Wealthy = 7000\n Poor = 2000\n TeamsWealthyRate = 2.0\n TeamsPoorRate = 0.6\n TeamResourcesToStart = 0.1\n MaxRecruitRadius = 400\nEnd\n", s, &error));
	CHECK(s.legacyAI.teamSeconds == 2.5f);
	CHECK(s.legacyAI.wealthy == 7000);
	CHECK(s.legacyAI.poor == 2000);
	CHECK(s.legacyAI.wealthyRate == 2.0f);
	CHECK(s.legacyAI.poorRate == 0.6f);
	CHECK(s.legacyAI.resourcesToStart == 0.1f);
	CHECK(s.legacyAI.maxRecruitRadius == 400.0f);
}

TEST_CASE("bfme1 ai: library production conditions train through a real factory and activate the completed team")
{
	using namespace scripttest;
	prodtest::ProdWorld f;
	std::string objects = prodtest::kBarracksObjects;
	const auto kind = objects.find("KindOf = STRUCTURE SELECTABLE");
	REQUIRE(kind != std::string::npos);
	objects.replace(kind, std::string("KindOf = STRUCTURE SELECTABLE").size(), "KindOf = STRUCTURE SELECTABLE FS_FACTORY");
	f.load(objects);
	f.load(prodtest::kBarracksCommands);
	Player *owner = f.alice();
	owner->setPlayerType(PLAYER_COMPUTER, true);
	f.players.findPlayerWithName("Bob")->setPlayerType(PLAYER_HUMAN, false);
	owner->setSkirmishDifficulty(1);
	f.logic->settings().legacyAI.teamSeconds = 10.0f;
	f.logic->settings().legacyAI.wealthy = 7000;
	f.logic->settings().legacyAI.wealthyRate = 2.0f;
	f.logic->settings().legacyAI.poorRate = 0.6f;
	Object *factory = f.make("Barracks", f.teamOf("Alice"));
	REQUIRE(factory);
	Dict dict;
	dict.setAsciiString("teamProductionCondition", "Produce");
	dict.setInt("teamProductionPriority", 50);
	dict.setInt("teamMaxInstances", 1);
	dict.setAsciiString("teamUnitType1", "Soldier");
	dict.setInt("teamUnitMinCount1", 1);
	dict.setInt("teamUnitMaxCount1", 2); // native requests the optional member too; there is no random size draw.
	int expectedMembers = 2;
	bool cancel = false;
	SUBCASE("the full optional complement is produced") {}
	SUBCASE("cancelled production is retried") { cancel = true; }
	SUBCASE("an expired build waits for its in-flight minimum and omits optional units")
	{
		dict.setInt("teamInitialIdleSeconds", 10);
		expectedMembers = 1;
	}
	SUBCASE("an expired build with an unqueued required member cancels and retires the team")
	{
		dict.setInt("teamInitialIdleSeconds", 10);
		dict.setInt("teamUnitMinCount1", 2);
		expectedMembers = 0;
	}
	TeamPrototype *proto = f.teams.initTeam("Wave", owner, false, &dict);
	ScriptList list;
	list.items.push_back(item(script("Produce", {{cond("FLAG", {sp(5, "Allowed"), ip(8, 1)})}}, {}, {}, false, false)));
	std::vector<ScriptEngine::SideScripts> sides(3);
	for (int i = 0; i < 3; ++i) sides[i].sideName = f.players.getNthPlayer(i)->getPlayerName();
	sides[owner->getPlayerIndex()].lists.push_back(&list);
	f.logic->scriptEngine().newGame(sides, nullptr, nullptr, false);
	f.logic->scriptEngine().flag("Alice/Allowed") = true;
	f.logic->skirmishAI().useBfme1();
	CHECK(f.logic->skirmishAI().newGame(true) == 1);
	CHECK(f.logic->skirmishAI().report().back() == "[S-5018] BFME1 skirmish AI: shared horde/transport, hero revival, combat, movement and script fidelity gaps remain S-5017 / S-1186 / S-1190");
	const unsigned money = owner->getMoney()->countMoney();
	f.frames(35); // the 50-frame Soldier is queued and unfinished.
	INFO("teams=" << proto->teams().size() << " frame=" << f.logic->getFrame() << " dead=" << factory->isEffectivelyDead() << " factory=" << factory->isKindOfName("FS_FACTORY") << " disabled=" << factory->getDisabledMask());
	CHECK(f.logic->scriptEngine().evaluateProductionScript("Produce", *owner));
	CHECK(BuildAssistant::isInProducersCommandSet(*factory, f.w.get("Soldier"), -1));
	CHECK(BuildAssistant::playerCanBuild(*owner, f.w.get("Soldier"), *f.logic));
	CHECK(BuildAssistant::commandPointsAvailable(*owner, *f.w.get("Soldier"), *f.logic));
	REQUIRE(f.pu(factory)->firstProduction());
	const auto pendingHash = f.logic->computeStateHash();
	XferSave pending;
	f.logic->skirmishAI().xfer(pending);
	XferLoad restore(pending.data());
	f.logic->skirmishAI().xfer(restore);
	CHECK(f.logic->computeStateHash() == pendingHash);
	auto obsolete = pending.data();
	obsolete[0] = 7; // pre-reinforcement blocks cannot silently replace a version-8 production queue.
	XferLoad old(obsolete);
	CHECK_THROWS_AS(f.logic->skirmishAI().xfer(old), XferException);
	CHECK(f.logic->computeStateHash() == pendingHash);
	auto truncated = pending.data();
	REQUIRE_FALSE(truncated.empty());
	truncated.pop_back();
	XferLoad bad(truncated);
	CHECK_THROWS_AS(f.logic->skirmishAI().xfer(bad), XferException);
	CHECK(f.logic->computeStateHash() == pendingHash);
	f.logic->scriptEngine().flag("Alice/Allowed") = false; // no second build after the timeout disbands the first.
	if (cancel) f.pu(factory)->cancelUnitCreate(f.pu(factory)->firstProduction()->productionID);
	f.frames(180);
	REQUIRE(proto->teams().size() == 1);
	Team *team = proto->getFirstTeam();
	int members = 0;
	for (Object *o = team->getFirstMember(); o; o = o->friend_teamNext()) ++members;
	CHECK(members == expectedMembers);
	if (expectedMembers)
	{
		REQUIRE(team->getFirstMember());
		CHECK(team->getFirstMember()->getTemplate()->getName() == "Soldier");
		CHECK(team->getFirstMember()->getProducerID() == factory->getID());
		CHECK(team->isActive());
	}
	else CHECK_FALSE(team->isActive());
	CHECK_FALSE(team->scriptState().beingBuilt);
	CHECK(team->scriptState().wasBuilt);
	CHECK(owner->getMoney()->countMoney() == money - (unsigned)expectedMembers * 100);
}

TEST_CASE("bfme1 ai: threat is raw ThreatLevel and keeps BFME1's 25-frame scan cache")
{
	using namespace scripttest;
	logictest::LogicWorld f(RandomAlgorithm::ZH_CarryChain, nullptr, true);
	LogicModules::registerAll(f.w.modules);
	REQUIRE(f.w.load("Object Scout\n KindOf = SELECTABLE\n ThreatLevel = 50\nEnd\nObject Enemy\n KindOf = SELECTABLE\n ThreatLevel = 7\nEnd\n").empty());
	const auto frames = [&](int n) { for (int i = 0; i < n; ++i) f.logic->runLogicFrame(); };
	f.logic->partition().setRegion(0, 0, 1000, 1000);
	Object *scout = f.make("Scout", f.teamOf("Alice"));
	Object *enemy = f.make("Enemy", f.teamOf("Bob"));
	REQUIRE(scout); REQUIRE(enemy);
	Coord3D at{100, 100, 0}, near{150, 100, 0}, far{800, 800, 0};
	scout->setPosition(&at); enemy->setPosition(&near);
	std::vector<ScriptEngine::SideScripts> sides(3);
	for (int i = 0; i < 3; ++i) sides[i].sideName = f.players.getNthPlayer(i)->getPlayerName();
	f.logic->scriptEngine().newGame(sides, nullptr, nullptr, false);
	scout->setName("Scout");
	f.logic->skirmishAI().useBfme1();
	// BFME1 0x1D2DA8 initializes value/deadline to zero; 0x1D2580 uses >=.
	// The first query at frame zero returns zero even with an enemy nearby.
	REQUIRE(f.logic->getFrame() == 0);
	auto check = cond("UNIT_THREAT_LEVEL", {sp(14, "Scout"), ip(6, 2), rp(0), rp(100)});
	CHECK(f.logic->scriptEngine().evaluateCondition(check));
	frames(1);
	CHECK(f.logic->scriptEngine().getUnitNamed("Scout") == scout);
	CHECK(scout->getRelationship(*enemy) == ENEMIES);
	INFO("threat field kind=" << enemy->getTemplate()->findField("ThreatLevel")->index() << " frame=" << f.logic->getFrame() << " dead=" << enemy->isEffectivelyDead());
	check.params[2] = rp(7);
	CHECK(f.logic->scriptEngine().evaluateCondition(check));
	enemy->setPosition(&far);
	frames(25);
	CHECK(f.logic->scriptEngine().evaluateCondition(check)); // frame 26 == cached deadline; refresh uses a strict comparison.
	frames(1);
	check.params[2] = rp(0);
	CHECK(f.logic->scriptEngine().evaluateCondition(check));
}

namespace
{
struct NativeAIFixture : prodtest::ProdWorld
{
	NativeAIFixture()
	{
		std::string objects = prodtest::kBarracksObjects;
		const std::string kind = "KindOf = STRUCTURE SELECTABLE";
		objects.replace(objects.find(kind), kind.size(), kind + " FS_FACTORY");
		load(objects);
		load(prodtest::kBarracksCommands);
		alice()->setPlayerType(PLAYER_COMPUTER, true);
		players.findPlayerWithName("Bob")->setPlayerType(PLAYER_HUMAN, false);
		alice()->setSkirmishDifficulty(1);
		logic->settings().legacyAI.teamSeconds = 10.0f;
		logic->settings().legacyAI.resourcesToStart = 0.1f;
		logic->settings().legacyAI.maxRecruitRadius = 400.0f;
		logic->settings().legacyAI.wealthy = 7000;
		logic->settings().legacyAI.wealthyRate = 2.0f;
		logic->settings().legacyAI.poorRate = 0.6f;
	}
	void start(const ScriptList *list = nullptr)
	{
		std::vector<ScriptEngine::SideScripts> sides(3);
		for (int i = 0; i < 3; ++i) sides[i].sideName = players.getNthPlayer(i)->getPlayerName();
		if (list) sides[alice()->getPlayerIndex()].lists.push_back(list);
		logic->scriptEngine().newGame(sides, nullptr, nullptr, false);
		logic->skirmishAI().useBfme1();
		REQUIRE(logic->skirmishAI().newGame(true) == 1);
	}
	Dict wave(const char *type, int minimum, int maximum, int priority = 50)
	{
		Dict d;
		d.setAsciiString("teamProductionCondition", "Produce");
		d.setInt("teamProductionPriority", priority); d.setInt("teamMaxInstances", 1);
		d.setAsciiString("teamUnitType1", type); d.setInt("teamUnitMinCount1", minimum); d.setInt("teamUnitMaxCount1", maximum);
		return d;
	}
	void roundTrip()
	{
		const auto before = logic->computeStateHash();
		XferSave save; logic->skirmishAI().xfer(save);
		XferLoad load(save.data()); logic->skirmishAI().xfer(load);
		CHECK(logic->computeStateHash() == before);
		XferSave again; logic->skirmishAI().xfer(again);
		CHECK(again.data() == save.data());
	}
};
}

TEST_CASE("bfme1 ai: special-power readiness caches each condition and resumes at the native frame boundary")
{
	using namespace scripttest;
	NativeAIFixture f;
	SpecialPowerStore powers;
	struct StoreScope
	{
		SpecialPowerStore *saved = TheSpecialPowerStore;
		~StoreScope() { TheSpecialPowerStore = saved; }
	} scope;
	TheSpecialPowerStore = &powers;
	f.w.fx.env.blocks.registerBlock("SpecialPower", [](INI *ini) { SpecialPowerStore::parseSpecialPowerDefinitionGlobal(ini); });
	f.load("SpecialPower Power\n ReloadTime = 0\nEnd\nObject Caster\n KindOf = SPELL_BOOK\n Behavior = SpecialPowerModule Ability\n SpecialPowerTemplate = Power\n End\nEnd\n");
	ScriptList list;
	for (const std::string name : { "First", "Second", "Missing" })
	{
		auto s = script(name, {{cond("SKIRMISH_SPECIAL_POWER_READY", {sp(11, "Alice"), sp(25, name == "Missing" ? "Unknown" : "Power")})}},
			{act("INCREMENT_COUNTER", {ip(0, 1), sp(4, name)})});
		s->isSubroutine = true; list.items.push_back(item(std::move(s)));
	}
	f.start(&list);
	auto call = [&](const std::string &name) { f.logic->scriptEngine().executeAction(act("CALL_SUBROUTINE", {sp(13, "Alice/" + name)})); };
	auto count = [&](const std::string &name) { const auto *c = f.logic->scriptEngine().findCounter("Alice/" + name); return c ? c->value : 0; };
	call("First"); // no caster: cache frame 50
	call("Missing"); // unknown template: cache -1
	Object *caster = f.make("Caster", f.alice()->getDefaultTeam()); REQUIRE(caster);
	auto *module = SpecialPowerModules::findModule(*caster, powers.findSpecialPowerTemplate("Power")); REQUIRE(module);
	call("First"); CHECK(count("First") == 0);
	call("Second"); CHECK(count("Second") == 1); // independent parameter cache
	f.roundTrip();
	f.frames(49); call("First"); CHECK(count("First") == 0);
	f.frames(1); call("First"); CHECK(count("First") == 1);
	module->setReadyFrame(57);
	call("Second"); CHECK(count("Second") == 1); // next check min(now+50, readyFrame)
	f.frames(6); call("Second"); CHECK(count("Second") == 1);
	f.frames(1); call("Second"); CHECK(count("Second") == 2);
	f.load("SpecialPower Unknown\n ReloadTime = 0\nEnd\nObject OtherCaster\n KindOf = SPELL_BOOK\n Behavior = SpecialPowerModule Ability\n SpecialPowerTemplate = Unknown\n End\nEnd\n");
	REQUIRE(f.make("OtherCaster", f.alice()->getDefaultTeam()));
	call("Missing"); CHECK(count("Missing") == 0); // -1 survives newly available template
	f.roundTrip();
}

TEST_CASE("bfme1 ai: queued teams recruit before buying and default-team recruitment can exceed the radius")
{
	using namespace scripttest;
	NativeAIFixture f;
	Dict d = f.wave("Soldier", 1, 1);
	TeamPrototype *proto = f.teams.initTeam("Wave", f.alice(), false, &d);
	Object *unit = f.make("Soldier", f.alice()->getDefaultTeam());
	REQUIRE(unit);
	Coord3D far{900000, 900000, 0}; unit->setPosition(&far);
	ScriptList list;
	list.items.push_back(item(script("Produce", {{cond("CONDITION_TRUE", {})}}, {}, {}, false, false)));
	f.start(&list);
	const auto money = f.alice()->getMoney()->countMoney();
	f.frames(60);
	REQUIRE(proto->getFirstTeam());
	CHECK(unit->getTeam() == proto->getFirstTeam());
	CHECK(proto->getFirstTeam()->isActive());
	CHECK(f.alice()->getMoney()->countMoney() == money);
	CHECK(unit->getProducerID() == INVALID_ID);
	f.roundTrip();
}

TEST_CASE("bfme1 ai: direct team builds require factories but can queue before money is available")
{
	using namespace scripttest;
	NativeAIFixture f;
	Dict d = f.wave("Soldier", 1, 1);
	TeamPrototype *proto = f.teams.initTeam("Wave", f.alice(), false, &d);
	bool producer = false;
	SUBCASE("missing factory") {}
	SUBCASE("factory exists but no cash") { producer = true; REQUIRE(f.make("Barracks", f.alice()->getDefaultTeam())); }
	f.alice()->getMoney()->withdraw(f.alice()->getMoney()->countMoney());
	f.start();
	f.logic->scriptEngine().executeAction(act("BUILD_TEAM", {sp(3, "Alice/Wave")}));
	CHECK((proto->getFirstTeam() != nullptr) == producer);
	if (producer)
	{
		CHECK(proto->getFirstTeam()->scriptState().beingBuilt);
		CHECK_FALSE(proto->getFirstTeam()->isActive());
	}
}

TEST_CASE("bfme1 ai: explicit team recruitment overrides are saved and override the default team")
{
	using namespace scripttest;
	NativeAIFixture f;
	Dict d = f.wave("Soldier", 1, 1);
	TeamPrototype *proto = f.teams.initTeam("Wave", f.alice(), false, &d);
	Object *unit = f.make("Soldier", f.alice()->getDefaultTeam()); REQUIRE(unit);
	f.start();
	const auto src = "Alice/" + f.alice()->getDefaultTeam()->getPrototype()->getName();
	f.logic->scriptEngine().executeAction(act("TEAM_AVAILABLE_FOR_RECRUITMENT", {sp(3, src), ip(8, 0)}));
	f.roundTrip();
	f.logic->scriptEngine().executeAction(act("TEAM_RECRUIT_UNITS", {sp(3, "Alice/Wave"), ip(0, 1), sp(61, "Soldier")}));
	REQUIRE(proto->getFirstTeam());
	CHECK(unit->getTeam() == f.alice()->getDefaultTeam());
	f.logic->scriptEngine().executeAction(act("TEAM_AVAILABLE_FOR_RECRUITMENT", {sp(3, src), ip(8, 1)}));
	f.logic->scriptEngine().executeAction(act("TEAM_RECRUIT_UNITS", {sp(3, "Alice/Wave"), ip(0, 1), sp(61, "Soldier")}));
	CHECK(unit->getTeam() == proto->getFirstTeam());
}

TEST_CASE("bfme1 ai: automatic reinforcement replenishes the existing team without another instance")
{
	NativeAIFixture f;
	Object *factory = f.make("Barracks", f.teamOf("Alice")); REQUIRE(factory);
	Dict d = f.wave("Soldier", 1, 2);
	d.setBool("teamAutoReinforce", true);
	TeamPrototype *proto = f.teams.initTeam("Wave", f.alice(), false, &d);
	Team *team = f.teams.createTeam(proto, true);
	Object *veteran = f.make("Soldier", team); REQUIRE(veteran);
	Object *reserve = nullptr;
	bool recruited = false;
	SUBCASE("recruit an existing unit") { recruited = true; reserve = f.make("Soldier", f.alice()->getDefaultTeam()); REQUIRE(reserve); }
	SUBCASE("train a replacement") {}
	f.start();
	const auto money = f.alice()->getMoney()->countMoney();
	f.frames(3);
	f.roundTrip();
	f.frames(160);
	CHECK(proto->teams().size() == 1);
	int count = 0;
	for (Object *o = team->getFirstMember(); o; o = o->friend_teamNext()) ++count;
	CHECK(count == 2);
	CHECK(veteran->getTeam() == team);
	CHECK(team->isActive());
	CHECK_FALSE(team->scriptState().beingBuilt);
	if (recruited) { CHECK(reserve->getTeam() == team); CHECK(f.alice()->getMoney()->countMoney() == money); }
	else CHECK(f.alice()->getMoney()->countMoney() == money - 100);
	f.roundTrip();
}

TEST_CASE("bfme1 ai: a revival record selects a REVIVE build index instead of the purchase button")
{
	using namespace scripttest;
	NativeAIFixture f;
	f.load("Object Hero\n BuildCost = 1200\n BuildTime = 30\n KindOf = SELECTABLE INFANTRY HERO\nEnd\n"
	       "CommandButton ReviveHero\n Command = REVIVE\nEnd\n"
	       "CommandButton BuyHero\n Command = UNIT_BUILD\n Object = Hero\nEnd\n"
	       "CommandSet HeroSet\n 1 = BuyHero\n 2 = ReviveHero\nEnd\n");
	f.load("ChildObject HeroFactory Barracks\n CommandSet = HeroSet\nEnd\n");
	Object *factory = f.make("HeroFactory", f.teamOf("Alice")); REQUIRE(factory);
	HeroRecord record = PlayerHeroList::recordFromTemplate(*f.w.get("Hero"));
	record.dead = true; record.cost = 300; record.seconds = 10;
	f.alice()->heroes().addRecord(record);
	bool blocked = false;
	SUBCASE("the first record is available") {}
	SUBCASE("the first matching record is already in production and a second is available")
	{
		blocked = true;
		REQUIRE(f.alice()->heroes().startProduction(0, 900, f.logic->getFrame()));
		f.alice()->heroes().addRecord(record);
	}
	Dict d = f.wave("Hero", 1, 1);
	TeamPrototype *proto = f.teams.initTeam("Heroes", f.alice(), false, &d);
	ScriptList list;
	list.items.push_back(item(script("Produce", {{cond("CONDITION_TRUE", {})}}, {}, {}, false, false)));
	f.start(&list);
	const auto money = f.alice()->getMoney()->countMoney();
	f.frames(35);
	if (blocked)
	{
		CHECK(f.pu(factory)->firstProduction() == nullptr);
		CHECK(proto->getFirstTeam() == nullptr);
		CHECK(f.alice()->getMoney()->countMoney() == money);
		f.roundTrip();
		return;
	}
	const auto *entry = f.pu(factory)->firstProduction(); REQUIRE(entry);
	CHECK(entry->type == PRODUCTION_BUILD_INDEX);
	CHECK(f.alice()->heroes().at(0)->productionID == entry->productionID);
	CHECK(entry->cost == 300);
	CHECK(f.alice()->getMoney()->countMoney() == money - 300);
	CHECK_FALSE(f.alice()->heroes().isAvailable(0));
	f.roundTrip();
	f.frames(170);
	REQUIRE(proto->getFirstTeam());
	REQUIRE(proto->getFirstTeam()->getFirstMember());
	CHECK(proto->getFirstTeam()->getFirstMember()->getTemplate()->getName() == "Hero");
	CHECK(proto->getFirstTeam()->isActive());
}

TEST_CASE("bfme1 ai: mixed auto-build production is an idle factory unless auto-build is disabled")
{
	using namespace scripttest;
	NativeAIFixture f;
	f.load("Object AutoUnit\n BuildCost = 0\n BuildTime = 1000\n IsAutoBuilt = Yes\n KindOf = INFANTRY\nEnd\n"
	       "CommandButton AutoUnitButton\n Command = UNIT_BUILD\n Object = AutoUnit\nEnd\n"
	       "CommandSet AutoSet\n 1 = Command_BuildSoldier\n 2 = AutoUnitButton\nEnd\n");
	f.load("ChildObject AutoFactory Barracks\n CommandSet = AutoSet\nEnd\n");
	Object *factory = f.make("AutoFactory", f.teamOf("Alice")); REQUIRE(factory);
	Dict d = f.wave("Soldier", 1, 1);
	f.teams.initTeam("Wave", f.alice(), false, &d);
	ScriptList list;
	list.items.push_back(item(script("Produce", {{cond("CONDITION_TRUE", {})}}, {}, {}, false, false)));
	f.start(&list);
	bool enabled = true;
	SUBCASE("default auto-build enabled") {}
	SUBCASE("explicitly disabled") { enabled = false; f.logic->scriptEngine().executeAction(act("TOGGLE_AUTO_BUILD", {sp(11, "Alice"), ip(8, 0)})); }
	REQUIRE(f.pu(factory)->queueCreateUnit(f.w.get("AutoUnit"), -1, f.pu(factory)->requestUniqueUnitID(), -1, false, "", false));
	f.frames(35);
	CHECK(f.pu(factory)->getProductionCount() == (enabled ? 2 : 1));
	f.roundTrip();
}

#include "MoveTestUtil.h"

TEST_CASE("bfme1 ai: per-unit AI recruitment overrides exclude units and survive a snapshot")
{
	using namespace scripttest;
	movetest::MoveWorld f(100, 100, "", true);
	Dict d; d.setInt("teamProductionPriority", 50);
	TeamPrototype *proto = f.teams.initTeam("Wave", f.players.findPlayerWithName("Alice"), false, &d);
	Object *unit = f.spawn("Walker", 100, 100, 0, f.players.findPlayerWithName("Alice")->getDefaultTeam()); REQUIRE(unit);
	unit->setName("Reserve");
	f.buildMap();
	std::vector<ScriptEngine::SideScripts> sides(3);
	for (int i = 0; i < 3; ++i) sides[i].sideName = f.players.getNthPlayer(i)->getPlayerName();
	f.logic->scriptEngine().newGame(sides, nullptr, nullptr, false);
	f.logic->skirmishAI().useBfme1();
	f.frames(1);
	REQUIRE(unit->getAIUpdateInterface());
	f.logic->scriptEngine().executeAction(act("UNIT_AFFECT_OBJECT_PANEL_FLAGS", {sp(14, "Reserve"), sp(46, "AI_Recruitable"), ip(8, 0)}));
	XferSave save; f.logic->skirmishAI().xfer(save);
	const auto before = f.logic->computeStateHash();
	XferLoad load(save.data()); f.logic->skirmishAI().xfer(load);
	CHECK(f.logic->computeStateHash() == before);
	f.logic->scriptEngine().executeAction(act("TEAM_RECRUIT_UNITS", {sp(3, "Alice/Wave"), ip(0, 1), sp(61, "Walker")}));
	REQUIRE(proto->getFirstTeam());
	CHECK(unit->getTeam() != proto->getFirstTeam());
	f.logic->scriptEngine().executeAction(act("UNIT_AFFECT_OBJECT_PANEL_FLAGS", {sp(14, "Reserve"), sp(46, "AI_Recruitable"), ip(8, 1)}));
	f.logic->scriptEngine().executeAction(act("TEAM_RECRUIT_UNITS", {sp(3, "Alice/Wave"), ip(0, 1), sp(61, "Walker")}));
	CHECK(unit->getTeam() == proto->getFirstTeam());
}

TEST_CASE("bfme1 ai: a specifically recruited team waits until idle or strictly more than sixty seconds")
{
	using namespace scripttest;
	movetest::MoveWorld f(100, 100, "", true);
	Player *owner = f.players.findPlayerWithName("Alice");
	Dict d; d.setInt("teamProductionPriority", 50); d.setAsciiString("teamUnitType1", "Walker"); d.setInt("teamUnitMaxCount1", 1);
	TeamPrototype *proto = f.teams.initTeam("Recruits", owner, false, &d);
	Object *unit = f.spawn("Walker", 100, 100, 0, owner->getDefaultTeam()); REQUIRE(unit);
	f.buildMap();
	std::vector<ScriptEngine::SideScripts> sides(3);
	for (int i = 0; i < 3; ++i) sides[i].sideName = f.players.getNthPlayer(i)->getPlayerName();
	f.logic->scriptEngine().newGame(sides, nullptr, nullptr, false);
	f.logic->skirmishAI().useBfme1();
	f.logic->scriptEngine().executeAction(act("RECRUIT_TEAM", {sp(3, "Alice/Recruits"), rp(0)}));
	Team *team = proto->getFirstTeam(); REQUIRE(team);
	CHECK(unit->getTeam() == team);
	CHECK_FALSE(team->isActive());
	unit->getAIUpdateInterface()->stateMachine().setState(AI_BUSY);
	XferSave save; f.logic->skirmishAI().xfer(save);
	const auto before = f.logic->computeStateHash();
	XferLoad load(save.data()); f.logic->skirmishAI().xfer(load);
	CHECK(f.logic->computeStateHash() == before);
	f.frames(300);
	CHECK_FALSE(team->isActive());
	f.frames(1);
	CHECK(team->isActive());
}

TEST_CASE("bfme1 ai: template variations and equivalence aliases retain every original token")
{
	using namespace scripttest;
	logictest::LogicWorld f(RandomAlgorithm::ZH_CarryChain, nullptr, true);
	REQUIRE(f.w.load("Object First\n EquivalentTo = Common Other\n BuildVariations = VariantA VariantB\nEnd\nObject Second\n EquivalentTo = Common\nEnd\n").empty());
	const auto *list = std::get_if<std::vector<std::string>>(f.w.get("First")->findField("BuildVariations")); REQUIRE(list);
	CHECK(*list == std::vector<std::string>{"VariantA", "VariantB"});
	const auto *aliases = std::get_if<std::vector<std::string>>(f.w.get("First")->findField("EquivalentTo")); REQUIRE(aliases);
	CHECK(*aliases == std::vector<std::string>{"Common", "Other"});
	Dict d; d.setInt("teamProductionPriority", 50);
	TeamPrototype *proto = f.teams.initTeam("Recruits", f.players.findPlayerWithName("Alice"), false, &d);
	Object *unit = f.make("Second", f.players.findPlayerWithName("Alice")->getDefaultTeam()); REQUIRE(unit);
	std::vector<ScriptEngine::SideScripts> sides(3);
	for (int i = 0; i < 3; ++i) sides[i].sideName = f.players.getNthPlayer(i)->getPlayerName();
	f.logic->scriptEngine().newGame(sides, nullptr, nullptr, false);
	f.logic->skirmishAI().useBfme1();
	f.logic->scriptEngine().executeAction(act("TEAM_RECRUIT_UNITS", {sp(3, "Alice/Recruits"), ip(0, 1), sp(61, "First")}));
	CHECK(unit->getTeam() == proto->getFirstTeam());
}

TEST_CASE("bfme1 ai: team references precede prototype lookup and copies keep the original team ID")
{
	using namespace scripttest;
	NativeAIFixture f;
	Team *first = f.teams.createTeam(f.teams.initTeam("First", f.alice(), false), true);
	Team *second = f.teams.createTeam(f.teams.initTeam("Second", f.alice(), false), true);
	Team *shadow = f.teams.createTeam(f.teams.initTeam("Alias", f.alice(), false), true);
	REQUIRE(first); REQUIRE(second); REQUIRE(shadow);
	ScriptList list;
	auto bind = script("Bind", {{cond("CONDITION_TRUE", {})}}, {
		act("SET_TEAM_REFERENCE", {sp(55, "Alias"), sp(3, "First")}),
		act("SET_TEAM_REFERENCE_TO_REFERNECE", {sp(55, "Copy"), sp(55, "Alias")})});
	bind->isSubroutine = true; list.items.push_back(item(std::move(bind)));
	auto rebind = script("Rebind", {{cond("CONDITION_TRUE", {})}}, {act("SET_TEAM_REFERENCE", {sp(55, "Alias"), sp(3, "Second")})});
	rebind->isSubroutine = true; list.items.push_back(item(std::move(rebind)));
	f.start(&list);
	CHECK(ScriptConditions::team(f.logic->scriptEngine(), "Alice/Alias") == shadow);
	f.logic->scriptEngine().executeAction(act("CALL_SUBROUTINE", {sp(13, "Alice/Bind")}));
	CHECK(ScriptConditions::team(f.logic->scriptEngine(), "Alice/Alias") == first);
	CHECK(ScriptConditions::team(f.logic->scriptEngine(), "Alice/Copy") == first);
	f.roundTrip();
	f.logic->scriptEngine().executeAction(act("CALL_SUBROUTINE", {sp(13, "Alice/Rebind")}));
	CHECK(ScriptConditions::team(f.logic->scriptEngine(), "Alice/Alias") == second);
	CHECK(ScriptConditions::team(f.logic->scriptEngine(), "Alice/Copy") == first);
}

TEST_CASE("bfme1 ai: built hordes wait for their first member rather than their full complement")
{
	movetest::MoveWorld f(100, 100, "", true);
	Team *team = f.teams.createTeam(f.teams.initTeam("HordeWave", f.players.findPlayerWithName("Alice"), false), true);
	Object *horde = f.make("Horde", team, INVALID_ID, false); REQUIRE(horde);
	auto *contain = horde->getContain(); REQUIRE(contain);
	CHECK(contain->getContainCount() == 0);
	f.logic->skirmishAI().useBfme1();
	auto *ai = f.logic->skirmishAI().bfme1(); REQUIRE(ai);
	CHECK(ai->teamReady(*team)); // map-created teams do not have the sticky AI-build flag.
	team->scriptState().wasBuilt = true;
	CHECK_FALSE(ai->teamReady(*team));
	Object *member = f.make("MemberOfHorde", team); REQUIRE(member);
	REQUIRE(contain->addToContain(member));
	CHECK(contain->getContainCount() == 1);
	CHECK(ai->teamReady(*team)); // BFME1 checks nonempty, not a full five-member formation.
}

TEST_CASE("bfme1 ai: horde workers retain the native horde attack runtime and initial wake delay")
{
	for (bool bfme1 : { false, true })
	{
		movetest::MoveWorld f(100, 100,
			"Object WorkerHorde\n KindOf = HORDE\n Behavior = HordeWorkerAIUpdate WorkerAI\n End\nEnd\n", bfme1);
		Object *worker = f.make("WorkerHorde", f.players.findPlayerWithName("Alice")->getDefaultTeam()); REQUIRE(worker);
		auto *ai = worker->getAIUpdateInterface(); REQUIRE(ai);
		CHECK((dynamic_cast<HordeAIUpdate *>(ai) != nullptr) == bfme1);
		const auto report = f.logic->report();
		CHECK(std::count(report.stops.begin(), report.stops.end(),
			"[S-5017] BFME1 HordeWorkerAIUpdate: inherited horde combat runs; worker repair/task overrides remain unported") == (bfme1 ? 1 : 0));
		if (bfme1)
		{
			CHECK(ai->attackMachineKind() == ATTACK_MACHINE_HORDE);
			CHECK(ai->friend_getNextCallFrame() == f.logic->getFrame() + 5);
		}
	}
}

TEST_CASE("bfme1 ai: merging retires an empty source so another wave can be recruited or trained")
{
	using namespace scripttest;
	NativeAIFixture f;
	Object *factory = f.make("Barracks", f.teamOf("Alice")); REQUIRE(factory);
	Dict d = f.wave("Soldier", 1, 1);
	TeamPrototype *proto = f.teams.initTeam("Source", f.alice(), false, &d);
	Team *from = f.teams.createTeam(proto, true);
	Team *to = f.teams.createTeam(f.teams.initTeam("Army", f.alice(), false), true);
	Object *unit = f.make("Soldier", from); REQUIRE(unit);
	ScriptList list;
	list.items.push_back(item(script("Produce", {{cond("CONDITION_TRUE", {})}}, {}, {}, false, false)));
	f.start(&list);
	f.logic->scriptEngine().executeAction(act("TEAM_MERGE_INTO_TEAM", {sp(3, "Alice/Source"), sp(3, "Alice/Army")}));
	CHECK(unit->getTeam() == to);
	CHECK(from->getFirstMember() == nullptr);
	CHECK(from->isActive()); // native deleteTeam(false) defers the team cleanup to the prototype update.
	f.roundTrip();
	f.frames(1);
	CHECK_FALSE(from->isActive());
	CHECK(ScriptConditions::team(f.logic->scriptEngine(), "Alice/Source") == nullptr);
	f.frames(34);
	CHECK(proto->teams().size() == 2);
	CHECK(f.pu(factory)->getProductionCount() == 1);
}

#include "GameEngineDevice/W3DDevice/GameClient/WorldHeightMap.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Module/HordeContain.h"
#include "GameLogic/Object/Contain/TransportContainRuntime.h"
#include "GameLogic/Object/Contain/ContainEditionRules.h"
#include "GameLogic/Object/ExperienceTracker.h"
#include "GameLogic/ExperienceWorld.h"

namespace
{
std::string combinationObjects()
{
	std::string out;
	for (const std::string name : { "PartA", "PartB", "Combined" })
	{
		const bool combined = name == "Combined";
		out += "Object " + name + "\n KindOf = HORDE SELECTABLE" + (combined ? std::string(" COMBO_HORDE") : "") +
			"\n Geometry = CYLINDER\n GeometryMajorRadius = 10\n GeometryHeight = 10\n Behavior = HordeAIUpdate AI\n End\n Behavior = HordeContain Contain\n Slots = " +
			(combined ? "4" : "2") + "\n RankInfo = RankNumber:1 UnitType:MemberOfHorde Position:X:0 Y:0 Position:X:10 Y:0" +
			(combined ? " Position:X:20 Y:0 Position:X:30 Y:0" : "") + "\n InitialPayload = MemberOfHorde " + (combined ? "4" : "2") + "\n";
		if (combined) out += " SplitHorde = SplitResult:PartA UnitType:MemberOfHorde\n SplitHorde = SplitResult:PartB UnitType:MemberOfHorde\n";
		else out += " ComboHorde = Target:" + std::string(name == "PartA" ? "PartB" : "PartA") + " Result:Combined\n";
		out += " End\n LocomotorSet\n Locomotor = HordeLoco\n Condition = SET_NORMAL\n Speed = 50\n End\nEnd\n";
	}
	return out;
}
void startBfme1Scripts(movetest::MoveWorld &f)
{
	// Recruitment can issue move orders immediately, before the first frame.
	// Match LiveGame startup: initialize the pathfinder before running scripts.
	if (!f.mapBuilt) f.buildMap();
	std::vector<ScriptEngine::SideScripts> sides(3);
	for (int i = 0; i < 3; ++i) sides[i].sideName = f.players.getNthPlayer(i)->getPlayerName();
	f.logic->skirmishAI().useBfme1();
	f.logic->scriptEngine().newGame(sides, nullptr, nullptr, false);
}

struct ReinforcementHost : ScriptEngineHost
{
	GameLogic &logic;
	explicit ReinforcementHost(GameLogic &l) : logic(l) {}
	Object *createObject(const ThingTemplate &tt, Team &team, const Coord3D &position, float angle) override
	{
		Object *o = logic.newObject(&tt, &team, ObjectStatusMaskType{});
		if (o) { o->setPosition(&position); o->setOrientation(angle); }
		return o;
	}
};
const char transportObjects[] =
	"Object Rider\n KindOf = INFANTRY SELECTABLE\n TransportSlotCount = 1\n Geometry = CYLINDER\n GeometryMajorRadius = 2\n GeometryHeight = 4\n"
	" Behavior = AIUpdateInterface AI\n End\n LocomotorSet\n Locomotor = WalkerLoco\n Condition = SET_NORMAL\n Speed = 50\n End\nEnd\n"
	"Object Carrier\n KindOf = TRANSPORT SELECTABLE VEHICLE\n Geometry = CYLINDER\n GeometryMajorRadius = 10\n GeometryHeight = 20\n"
	" Behavior = AIUpdateInterface AI\n End\n Behavior = TransportContain Carry\n Slots = 2\n PassengerFilter = ANY +INFANTRY\n ObjectStatusOfContained = UNSELECTABLE\n End\n"
	" LocomotorSet\n Locomotor = WalkerLoco\n Condition = SET_NORMAL\n Speed = 80\n End\nEnd\n";
struct ReinforcementWorld
{
	movetest::MoveWorld f{100, 100, transportObjects, true};
	WorldHeightMap map;
	MapChunks chunks;
	TerrainLogic terrain;
	ReinforcementHost host{*f.logic};
	ReinforcementWorld()
	{
		map.m_width = map.m_height = 101; map.m_dataSize = 10201; map.m_data.assign(10201, 0); map.m_boundaries = {{100, 100}};
		MapObject origin; origin.m_objectName = "*Waypoints/Waypoint"; origin.m_location = {100, 100, 0}; origin.m_isWaypoint = true;
		origin.m_properties.setInt("waypointID", 1); origin.m_properties.setAsciiString("waypointName", "Origin"); chunks.objects.push_back(origin);
		std::vector<std::string> problems; terrain.init(map, chunks, &problems); REQUIRE(problems.empty());
		f.logic->setTerrain(&terrain); f.logic->scriptEngine().setHost(&host); startBfme1Scripts(f);
	}
	~ReinforcementWorld() { f.logic->scriptEngine().setHost(nullptr); f.logic->setTerrain(nullptr); }
	TeamPrototype *wave(bool explicitCarrier, bool startsFull, bool exits, int riders = 3)
	{
		Dict d; d.setAsciiString("teamUnitType1", "Rider"); d.setInt("teamUnitMaxCount1", riders);
		if (explicitCarrier) d.setAsciiString("teamTransport", "Carrier");
		else { d.setAsciiString("teamUnitType2", "Carrier"); d.setInt("teamUnitMaxCount2", 1); }
		d.setBool("teamStartsFull", startsFull); d.setBool("teamTransportsExit", exits); d.setAsciiString("teamReinforcementOrigin", "Origin");
		return f.teams.initTeam("Delivery", f.players.findPlayerWithName("Alice"), false, &d);
	}
	void create()
	{
		using namespace scripttest;
		Object *at = f.make("Walker", f.players.findPlayerWithName("Alice")->getDefaultTeam()); REQUIRE(at);
		Coord3D pos{600, 300, 0}; at->setPosition(&pos); at->setName("Destination");
		f.logic->scriptEngine().executeAction(act("CREATE_REINFORCEMENT_TEAM_AT_UNIT_POSITION", {sp(3, "Alice/Delivery"), sp(14, "Destination")}));
	}
};
}

TEST_CASE("bfme1 ai: combo recruitment preserves soldiers and consumes both component hordes")
{
	using namespace scripttest;
	const std::string objects = combinationObjects();
	movetest::MoveWorld f(100, 100, objects.c_str(), true);
	startBfme1Scripts(f);
	Player *p = f.players.findPlayerWithName("Alice");
	Object *a = f.make("PartA", p->getDefaultTeam()), *b = f.make("PartB", p->getDefaultTeam()); REQUIRE(a); REQUIRE(b);
	a->setBfme1IndicatorColorIndex(7); b->setBfme1IndicatorColorIndex(3);
	std::set<ObjectID> members;
	for (Object *h : {a, b}) for (Object *m : *h->getContain()->getContainedItemsList()) members.insert(m->getID());
	REQUIRE(members.size() == 4);
	Dict d; d.setInt("teamProductionPriority", 100); d.setAsciiString("teamUnitType1", "Combined"); d.setInt("teamUnitMaxCount1", 1);
	auto *proto = f.teams.initTeam("Combo", p, false, &d);
	f.logic->scriptEngine().executeAction(act("RECRUIT_TEAM", {sp(3, "Alice/Combo"), rp(0)}));
	REQUIRE(proto->getFirstTeam()); Object *combo = proto->getFirstTeam()->getFirstMember(); REQUIRE(combo);
	CHECK(combo->getTemplate()->getName() == "Combined");
	CHECK(combo->bfme1IndicatorColorIndex() == 7);
	CHECK(a->isDestroyed()); CHECK(b->isDestroyed());
	CHECK(combo->getContain()->getContainCount() == 4);
	std::set<ObjectID> after;
	for (Object *m : *combo->getContain()->getContainedItemsList()) { after.insert(m->getID()); CHECK(m->getTeam() == combo->getTeam()); CHECK(m->getContainedBy() == combo); }
	CHECK(after == members);
	f.frames(3);
	CHECK(combo->getContain()->getContainCount() == 4); // no replacement InitialPayload was spawned.
	const auto report = f.logic->scriptEngine().report();
	CHECK(std::none_of(report.begin(), report.end(), [](const std::string &s) { return s.find("[S-5017] BFME1 combined horde: Object +0x370 value notification remains unverified") != std::string::npos; }));
}

TEST_CASE("bfme1 ai: named combo recruitment spans instances and honors native merge refusals")
{
	using namespace scripttest;
	const std::string objects = combinationObjects(); movetest::MoveWorld f(100, 100, objects.c_str(), true); startBfme1Scripts(f);
	Player *p = f.players.findPlayerWithName("Alice");
	auto *src = f.teams.initTeam("Source", p, false); Team *first = f.teams.createTeam(src, true), *second = f.teams.createTeam(src, true);
	Object *a = f.make("PartA", first), *b = f.make("PartB", second); REQUIRE(a); REQUIRE(b);
	Team *target = f.teams.createTeam(f.teams.initTeam("Target", p, false), true);
	bool blocked = false;
	SUBCASE("two eligible components") {}
	SUBCASE("unselectable component") { blocked = true; b->setStatus((unsigned)CombatNames::status("UNSELECTABLE"), true); }
	SUBCASE("just-built component") { blocked = true; b->setModelConditionState(CombatNames::modelCondition("JUST_BUILT"), true); }
	f.logic->scriptEngine().executeAction(act("TEAM_RECRUIT_UNITS_FROM_TEAM", {sp(3, "Alice/Target"), ip(0, 1), sp(61, "Combined"), sp(3, "Alice/Source")}));
	CHECK((target->getFirstMember() != nullptr) == !blocked);
	if (blocked) { CHECK(a->getTeam() == p->getDefaultTeam()); CHECK(b->getTeam() == p->getDefaultTeam()); }
	else CHECK(target->getFirstMember()->getTemplate()->getName() == "Combined");
}

TEST_CASE("bfme1 ai: scripted transports load overflow carriers and activate their team on delivery")
{
	ReinforcementWorld w;
	bool exits = false;
	SUBCASE("carriers remain after evacuation") {}
	SUBCASE("carriers return to origin and disappear after evacuation") { exits = true; }
	auto *proto = w.wave(true, false, exits); w.create();
	Team *team = proto->getFirstTeam(); REQUIRE(team); CHECK_FALSE(team->isActive());
	std::vector<ObjectID> carriers, passengers;
	for (Object *o = team->getFirstMember(); o; o = o->friend_teamNext())
	{
		if (o->isKindOfName("TRANSPORT")) carriers.push_back(o->getID());
		else { passengers.push_back(o->getID()); CHECK(o->getContainedBy() != nullptr); }
	}
	CHECK(carriers.size() == 2); CHECK(passengers.size() == 3);
	for (ObjectID id : carriers)
	{
		auto *ai = w.f.logic->findObjectByID(id)->getAIUpdateInterface(); REQUIRE(ai);
		CHECK(ai->stateMachine().isLocked()); CHECK(ai->currentStateId() == (exits ? 29 : 28));
		XferSave save; ai->xferAIState(save); const auto hash = w.f.logic->computeStateHash();
		XferLoad load(save.data()); ai->xferAIState(load); CHECK(w.f.logic->computeStateHash() == hash);
	}
	bool savedDeparture = false;
	for (int i = 0; i < 300; ++i)
	{
		w.f.frames(1);
		if (exits && !savedDeparture) for (ObjectID id : carriers)
			if (Object *o = w.f.logic->findObjectByID(id))
			{
				auto *ai = o->getAIUpdateInterface();
				if (ai->currentStateId() != 30 || !ai->curLocomotor() || !(ai->curLocomotor()->flags() & 2u)) continue;
				XferSave save; ai->xferAIState(save); const auto hash = w.f.logic->computeStateHash();
				XferLoad load(save.data()); ai->xferAIState(load); CHECK(w.f.logic->computeStateHash() == hash);
				savedDeparture = true; break;
			}
	}
	CHECK(savedDeparture == exits);
	CHECK(team->isActive());
	for (ObjectID id : passengers) { Object *o = w.f.logic->findObjectByID(id); REQUIRE(o); CHECK(o->getContainedBy() == nullptr); CHECK(o->getTeam() == team); }
	for (ObjectID id : carriers)
	{
		Object *o = w.f.logic->findObjectByID(id);
		if (exits) CHECK(o == nullptr);
		else { REQUIRE(o); CHECK(o->getContain()->getContainCount() == 0); }
	}
}

TEST_CASE("bfme1 ai: starts-full partitions team passengers into existing carriers without creating extra transports")
{
	ReinforcementWorld w; auto *proto = w.wave(false, true, false); w.create();
	Team *team = proto->getFirstTeam(); REQUIRE(team); CHECK(team->isActive());
	int carriers = 0, contained = 0, unassigned = 0;
	for (Object *o = team->getFirstMember(); o; o = o->friend_teamNext())
	{
		if (o->isKindOfName("TRANSPORT")) { ++carriers; CHECK(o->getContain()->getContainCount() == 2); }
		else if (o->getContainedBy()) ++contained;
		else ++unassigned;
	}
	CHECK(carriers == 1); CHECK(contained == 2); CHECK(unassigned == 1);
}

TEST_CASE("bfme1 ai retail: mounted combined hordes preserve their soldiers and a scripted mumak delivers its passengers")
{
	using namespace scripttest;
	const char *install = std::getenv("BFME1_INSTALL");
	if (!install || !*install) { retailtest::printSkip("BFME1 combined hordes / transports (BFME1_INSTALL unset)"); return; }
	Fixture fx(install);
	NewGameMessage message;
	message.game.mapName = "maps/map mp fords of isen/map mp fords of isen.map";
	message.game.seed = 9001; message.game.startingCash = 2000;
	for (int i = 0; i < 2; ++i)
	{
		auto &slot = message.game.slots[i]; slot.state = SLOT_PLAYER;
		slot.name = i ? u"Other" : u"Human"; slot.playerTemplate = fx.faction("Rohan");
		slot.startPos = i; slot.color = i; slot.teamNumber = i;
	}
	std::string error;
	NewGameStart start(RandomAlgorithm::ZH_CarryChain);
	REQUIRE_MESSAGE(NewGame::prepareNewGame(message, fx.world->playerTemplates(), fx.settings, fx.maps, RandomAlgorithm::ZH_CarryChain, start, &error), error);
	ArchiveW3DFileSource source(fx.fs); WW3DAssetManager assets(source);
	LiveGame game(*fx.world, fx.fs, assets, fx.options); LiveGame::Options opt; opt.start = &start;
	REQUIRE_MESSAGE(game.load(opt, &error), error);
	auto &logic = game.logic(); auto &engine = logic.scriptEngine();
	Player *owner = logic.players().findPlayerWithName(game.report().startSlotPlayers[0]); REQUIRE(owner);
	auto &teams = logic.players().teams();
	const Waypoint *origin = logic.terrain()->findWaypointByName("Player_1_Start"); REQUIRE(origin);
	Coord3D position = origin->location;
	position.x = SimMath::addf32(position.x, 300.0f);
	position.z = logic.getGroundHeight(position.x, position.y);
	auto create = [&](const char *type, Team *team) {
		const auto *tt = logic.things().findTemplate(type); REQUIRE_MESSAGE(tt, type);
		Object *o = engine.host()->createObject(*tt, *team, position, 0.0f); REQUIRE(o);
		o->friend_onBuildComplete(); return o;
	};
	int index = 0;
	for (const auto &row : std::vector<std::vector<const char *>>{
		{"RohanPeasantHorde", "RohanArcherHorde", "RohanPeasantArcherComboHorde", "2"},
		{"GondorFighterHorde", "GondorArcherHorde", "GondorFighterArcherComboHorde", "1"},
		{"RohanElvenWarriorHorde", "RohanElvenWarriorHorde", "RohanElvenWarriorDoubleHorde", "2"}})
	{
		INFO(std::string(row[2]));
		const std::string suffix = std::to_string(++index);
		Team *from = teams.createTeam(teams.initTeam("Parts" + suffix, owner, false), true);
		Team *to = teams.createTeam(teams.initTeam("Combo" + suffix, owner, false), true);
		Object *a = create(row[0], from), *b = create(row[1], from);
		REQUIRE(a->getExperienceTracker()); REQUIRE(b->getExperienceTracker());
		REQUIRE(a->getExperienceTracker()->gainExpForLevel(1, false));
		REQUIRE(b->getExperienceTracker()->gainExpForLevel(2, false));
		CHECK(a->getExperienceTracker()->getRank() == 2); CHECK(b->getExperienceTracker()->getRank() == 3);
		a->setBfme1IndicatorColorIndex(1); b->setBfme1IndicatorColorIndex(2);
		std::set<ObjectID> soldiers;
		for (Object *o : {a, b})
		{
			auto *h = dynamic_cast<HordeContain *>(o->getContain()); REQUIRE(h);
			for (Object *member : *h->getContainedItemsList()) soldiers.insert(member->getID());
			// This check exercises the recruitment path after production's
			// JUST_BUILT gate, not the shared spawn animation timer.
			o->setModelConditionState(CombatNames::modelCondition("JUST_BUILT"), false);
		}
		CHECK(soldiers.size() == 10);
		const auto levelUps = logic.experience().counters().unitLevelUps;
		const bool scoring = logic.economy().context().scoring;
		engine.executeAction(act("TEAM_RECRUIT_UNITS_FROM_TEAM", {sp(3, owner->getPlayerName() + "/Combo" + suffix), ip(0, 1), sp(5, row[2]), sp(3, owner->getPlayerName() + "/Parts" + suffix)}));
		Object *combo = to->getFirstMember(); REQUIRE(combo);
		CHECK(combo->getTemplate()->getName() == row[2]);
		CHECK(combo->getExperienceTracker()->getRank() == 3);
		// First SplitResult receives the merge; identical types take the
		// newest source-team member first. The expected palette indices
		// above pin that native order, independently of the spawn labels.
		CHECK(combo->bfme1IndicatorColorIndex() == std::stoi(row[3]));
		CHECK(logic.experience().counters().unitLevelUps == levelUps);
		CHECK(logic.economy().context().scoring == scoring);
		auto *h = dynamic_cast<HordeContain *>(combo->getContain()); REQUIRE(h);
		std::set<ObjectID> actual; for (Object *member : *h->getContainedItemsList()) { actual.insert(member->getID()); CHECK(member->getExperienceTracker()->getRank() == 3); }
		CHECK(actual == soldiers); CHECK(a->isDestroyed()); CHECK(b->isDestroyed());
	}
	Dict d; d.setAsciiString("teamUnitType1", "MordorHaradrimHorde"); d.setInt("teamUnitMaxCount1", 1);
	d.setAsciiString("teamTransport", "MordorMumakil"); d.setAsciiString("teamReinforcementOrigin", origin->name);
	auto *proto = teams.initTeam("MumakDelivery", owner, false, &d);
	Object *destination = create("RohanPeasant", owner->getDefaultTeam()); destination->setName("DeliverySpot");
	engine.executeAction(act("CREATE_REINFORCEMENT_TEAM_AT_UNIT_POSITION", {sp(3, owner->getPlayerName() + "/MumakDelivery"), sp(14, "DeliverySpot")}));
	Team *delivery = proto->getFirstTeam(); REQUIRE(delivery); CHECK_FALSE(delivery->isActive());
	ObjectID carrier = INVALID_ID, passenger = INVALID_ID;
	for (Object *o = delivery->getFirstMember(); o; o = o->friend_teamNext())
	{
		if (o->getTemplate()->getName() == "MordorMumakil") carrier = o->getID();
		if (o->getTemplate()->getName() == "MordorHaradrimHorde") passenger = o->getID();
	}
	REQUIRE(carrier != INVALID_ID); REQUIRE(passenger != INVALID_ID);
	CHECK(logic.findObjectByID(passenger)->getContainedBy() == logic.findObjectByID(carrier));
	game.advance(0.2);
	auto shot = LogicSnapshot::build(logic, nullptr, false, 0);
	auto &drawables = game.drawables();
	Drawable *parentDraw = drawables.findByObject(carrier); REQUIRE(parentDraw);
	Object *passengerObject = logic.findObjectByID(passenger); REQUIRE(passengerObject);
	const auto *members = passengerObject->getContain()->getContainedItemsList(); REQUIRE(members);
	CHECK(members->size() == 5);
	for (Object *member : *members)
	{
		const auto *record = shot->find(member->getID()); REQUIRE(record);
		CHECK(record->containerBoneParent == carrier); CHECK_FALSE(record->containerBone.empty());
	}
	// Both sync schedules must attach after the parent's animation and pose.
	std::vector<Coord3D> serial;
	drawables.setParallelSync(false); drawables.syncTransforms(*shot, 0.5, true);
	for (Object *member : *members)
	{
		Drawable *draw = drawables.findByObject(member->getID()); REQUIRE(draw);
		const auto *record = shot->find(member->getID());
		Matrix3D bone; REQUIRE(parentDraw->currentContainerBone(record->containerBone, bone));
		CHECK(draw->getPosition()->x == bone.Row[0][3]); CHECK(draw->getPosition()->y == bone.Row[1][3]); CHECK(draw->getPosition()->z == bone.Row[2][3]);
		serial.push_back(*draw->getPosition());
	}
	drawables.setParallelSync(true); drawables.setParallelSyncMinimum(0); drawables.syncTransforms(*shot, 0.5, true);
	size_t memberIndex = 0;
	for (Object *member : *members)
	{
		const Coord3D &actual = *drawables.findByObject(member->getID())->getPosition();
		const Coord3D &expected = serial[memberIndex++];
		CHECK(actual.x == expected.x); CHECK(actual.y == expected.y); CHECK(actual.z == expected.z);
	}
	// A fresh client rider starts its aiming latch at zero, independently
	// of the carrier's bone facing. Repeated syncs consume no extra tick.
	Object *aimObject=create(members->front()->getTemplate()->getName().c_str(),owner->getDefaultTeam());
	game.advance(0.2);
	Drawable *aimed=drawables.findByObject(aimObject->getID()); REQUIRE(aimed);
	ObjectSnapshot aim=*shot->find(members->front()->getID());
	CHECK(aim.containerBoneCanAim);
	aim.position={0,0,0};aim.containerBoneHasTarget=true;aim.containerBoneTarget={0,100,0};
	Matrix3D aimingBone;REQUIRE(parentDraw->currentContainerBone(aim.containerBone,aimingBone));
	const auto aimHash=logic.computeStateHash();
	auto expectAim=[&](float angle) {
		float boneAngle=std::atan2(aimingBone.Row[1][0],aimingBone.Row[0][0]);
		float d=angle-boneAngle,c=std::cos(d),sn=std::sin(d);
		for(int r=0;r<3;++r)
		{
			CHECK(aimed->getBasis()[r*3]==aimingBone.Row[r][0]*c+aimingBone.Row[r][1]*sn);
			CHECK(aimed->getBasis()[r*3+1]==aimingBone.Row[r][1]*c-aimingBone.Row[r][0]*sn);
		}
	};
	aimed->syncContainerBone(parentDraw,aim.containerBone,&aim);expectAim(0.1f);
	aimed->syncContainerBone(parentDraw,aim.containerBone,&aim);expectAim(0.1f);
	aimed->updateFade(1000.0/30.0);aimed->syncContainerBone(parentDraw,aim.containerBone,&aim);expectAim(0.2f);
	aim.containerBoneHasTarget=false;
	const float boneFacing=std::atan2(aimingBone.Row[1][0],aimingBone.Row[0][0]);
	float returning=0.2f;
	if(returning>boneFacing){if(returning-boneFacing>3.14159274f)returning+=0.1f;else if(returning-boneFacing>0.1f)returning-=0.1f;else returning=boneFacing;}
	else if(returning<boneFacing){if(boneFacing-returning>3.14159274f)returning-=0.1f;else if(boneFacing-returning>0.1f)returning+=0.1f;else returning=boneFacing;}
	if(returning>3.14159274f)returning-=6.28318548f;if(returning< -3.14159274f)returning+=6.28318548f;
	aimed->updateFade(1000.0/30.0);aimed->syncContainerBone(parentDraw,aim.containerBone,&aim);expectAim(returning);
	aim.containerBoneHasTarget=true;aim.containerBoneTarget={-100,1,0};
	aimed->updateFade(2000);aimed->syncContainerBone(parentDraw,aim.containerBone,&aim);
	expectAim(std::atan2(1.0f,-100.0f));
	aim.containerBoneTarget={-100,-1,0};aimed->updateFade(1000.0/30.0);aimed->syncContainerBone(parentDraw,aim.containerBone,&aim);
	// Native 0x0075B550 crosses the wrap boundary by a full 0.1,
	// even when the remaining angular distance is smaller than that.
	expectAim((std::atan2(1.0f,-100.0f)+0.1f)-6.28318548f);
	CHECK(logic.computeStateHash()==aimHash);

	// Native 0x007629F0 release: the saved bone at blend zero,
	// then 1/30 toward the ordinary pose per client tick. Client only.
	Drawable *released = drawables.findByObject(members->front()->getID()); REQUIRE(released);
	const Coord3D attached = *released->getPosition();
	const Coord3D ordinary{SimMath::addf32(attached.x, 120.0f), SimMath::addf32(attached.y, 30.0f), SimMath::addf32(attached.z, 60.0f)};
	const auto simulationHash = logic.computeStateHash();
	released->releaseContainerBone(0.0f); released->setPosition(&ordinary); released->syncContainerBone(nullptr, "");
	CHECK(released->getPosition()->x == attached.x); CHECK(released->getPosition()->y == attached.y); CHECK(released->getPosition()->z == attached.z);
	released->updateFade(1000.0 / 30.0); released->setPosition(&ordinary); released->syncContainerBone(nullptr, "");
	const float fraction = SimMath::divf32(1.0f, 30.0f);
	CHECK(released->getPosition()->x == SimMath::addf32(SimMath::mulf32(SimMath::subf32(ordinary.x, attached.x), fraction), attached.x));
	CHECK(released->getPosition()->y == SimMath::addf32(SimMath::mulf32(SimMath::subf32(ordinary.y, attached.y), fraction), attached.y));
	CHECK(released->getPosition()->z == SimMath::addf32(SimMath::mulf32(SimMath::subf32(ordinary.z, attached.z), fraction), attached.z));
	released->updateFade(1000.0); released->setPosition(&ordinary); released->syncContainerBone(nullptr, "");
	CHECK(released->getPosition()->x == ordinary.x); CHECK(released->getPosition()->y == ordinary.y); CHECK(released->getPosition()->z == ordinary.z);
	CHECK(logic.computeStateHash() == simulationHash);
	for (int i = 0; i < 600 && !delivery->isActive(); ++i) game.advance(0.2);
	for (int i = 0; i < 100 && logic.findObjectByID(passenger)->getContainedBy(); ++i) game.advance(0.2);
	CHECK(delivery->isActive());
	CHECK(logic.findObjectByID(passenger)->getContainedBy() == nullptr);
	CHECK(logic.findObjectByID(carrier)->getContain()->getContainCount() == 0);

	Object *house = create("SlaughterHouse", owner->getDefaultTeam());
	Object *offering = create("MordorFighterHorde", owner->getDefaultTeam());
	REQUIRE(house->getContain()); REQUIRE(offering->getContain());
	std::vector<ObjectID> offered;
	for (Object *member : *offering->getContain()->getContainedItemsList()) offered.push_back(member->getID());
	REQUIRE_FALSE(offered.empty());
	auto *offeringHorde = offering->getContain()->getHordeContainInterface(); REQUIRE(offeringHorde);
	offeringHorde->enterContainer(house, CMD_FROM_AI);
	REQUIRE(offering->getContainedBy() == house);
	for (int frame = 0; frame < 200; ++frame)
	{
		bool alive = false;
		for (ObjectID id : offered) if (Object *member = logic.findObjectByID(id); member && !member->isDestroyed()) alive = true;
		if (!alive) break;
		game.advance(0.2);
	}
	for (ObjectID id : offered)
	{
		Object *member = logic.findObjectByID(id);
		CHECK_MESSAGE((member == nullptr || member->isDestroyed()), "id=", id,
			" state=", member && member->getAIUpdateInterface() ? member->getAIUpdateInterface()->currentStateId() : 999,
			" contained=", member && member->getContainedBy() ? member->getContainedBy()->getID() : 0,
			" x=", member ? member->getPosition()->x : 0, " y=", member ? member->getPosition()->y : 0,
			" target=", house->getPosition()->x, ",", house->getPosition()->y);
	}

	for(const char *type:{"RohanOathbreakerHorde","RohanOathbreakerHordeSmall"})
	{
		Object *army=create(type,owner->getDefaultTeam());REQUIRE(army);
		auto *aod=dynamic_cast<HordeContain *>(army->getContain());REQUIRE(aod);
		CHECK_FALSE(army->findModule("AODHordeContain")->isUnported());
		REQUIRE_FALSE(aod->getContainedItemsList()->empty());
		Object *ghost=aod->getContainedItemsList()->front();REQUIRE(ghost->findModule("AODCrushCollide"));
		CHECK_FALSE(ghost->findModule("AODCrushCollide")->isUnported());
		ghost->setModelConditionState(CombatNames::modelCondition("JUST_BUILT"),false);ghost->getAIUpdateInterface()->aiBusy(CMD_FROM_AI);
		army->setModelConditionState(CombatNames::modelCondition("MOVING"),true);aod->update();
		Coord3D flowed=aod->getMemberFormationPosition(ghost);CHECK(std::isfinite(flowed.z));
		Object *large=create("MordorMumakil",owner->getDefaultTeam());large->setPosition(&flowed);
		REQUIRE_FALSE(large->isEffectivelyDead());
		const auto *largeValue=large->getTemplate()->findField("DeadCollideSize");REQUIRE(largeValue);
		const auto *largeSize=std::get_if<long long>(largeValue);REQUIRE(largeSize);CHECK(*largeSize==2);
		aod->trackLargeUnit(large);
		CHECK(aod->getMemberFormationPosition(ghost).z>flowed.z); // tolerance: native LARGE-unit hump raises a rider at the contact centre.
		XferSave saved;aod->xfer(saved);StateHasher original;aod->crc(original);XferLoad restored(saved.data());aod->xfer(restored);StateHasher roundTrip;aod->crc(roundTrip);CHECK(roundTrip.value()==original.value());
	}

	Object *rampage = create("CINE_MordorMumakil", owner->getDefaultTeam());
	auto *falling = dynamic_cast<HordeTransportContain *>(rampage->getContain()); REQUIRE(falling);
	rampage->setStatus((unsigned)CombatNames::status("AFLAME"), true);
	unsigned ejectFrames = 0, durationFrames = 0;
	const auto *data = dynamic_cast<const RawModuleData *>(rampage->getAIUpdateInterface()->getModuleData()); REQUIRE(data);
	for (const auto &line : data->lines())
	{
		std::istringstream input(line.text); std::string field, equal, value; input >> field >> equal >> value;
		if (field == "TimeToEjectPassengersOnRampage") ejectFrames = NumericState::ceilScaled((unsigned)std::stoul(value), 0.005f);
		if (field == "RampageTime") durationFrames = NumericState::ceilScaled((unsigned)std::stoul(value), 0.005f);
	}
	CHECK(ejectFrames == 12); CHECK(durationFrames == 40); // native ceil(ms * 0.005f), 2300 / 7960 ms.
	auto *delayedBody = dynamic_cast<DelayedDeathBody *>(rampage->getBodyModule()); REQUIRE(delayedBody);
	CHECK_FALSE(delayedBody->checked());
	REQUIRE(Bfme1SkirmishAI::enterRampage(*rampage));
	CHECK(delayedBody->checked());
	CHECK_FALSE(falling->ridersFalling());
	for (unsigned i = 0; i < durationFrames - 1 && !falling->ridersFalling(); ++i) game.advance(0.2);
	CHECK(falling->ridersFalling());
}

TEST_CASE("bfme1 ai: queue combination keeps a nearer existing horde and favors component ties")
{
	using namespace scripttest;
	movetest::MoveWorld f(100, 100, combinationObjects().c_str(), true); startBfme1Scripts(f);
	Player *p = f.players.findPlayerWithName("Alice");
	Object *a = f.make("PartA", p->getDefaultTeam()), *b = f.make("PartB", p->getDefaultTeam());
	Object *existing = f.make("Combined", p->getDefaultTeam());
	Coord3D pa{100, 0, 0}, pb{200, 0, 0}, pc{150, 0, 0};
	bool merges = false;
	SUBCASE("an existing combination nearer than either component wins") {}
	SUBCASE("a tie with the farther component favors combining") { pc.x = 200; merges = true; }
	a->setPosition(&pa); b->setPosition(&pb); existing->setPosition(&pc);
	Dict d; d.setInt("teamProductionPriority", 100); d.setAsciiString("teamUnitType1", "Combined"); d.setInt("teamUnitMaxCount1", 1);
	auto *proto = f.teams.initTeam("Combo", p, false, &d);
	f.logic->scriptEngine().executeAction(act("RECRUIT_TEAM", {sp(3, "Alice/Combo"), rp(0)}));
	REQUIRE(proto->getFirstTeam()); Object *result = proto->getFirstTeam()->getFirstMember(); REQUIRE(result);
	CHECK((result == existing) == !merges); CHECK(a->isDestroyed() == merges); CHECK(b->isDestroyed() == merges);
	CHECK(result->getPosition()->x == (merges ? 100.0f : 150.0f));
}

TEST_CASE("bfme1 ai: repeated split types consume candidates in the native single-scan order")
{
	using namespace scripttest;
	std::string objects = combinationObjects();
	auto replace = [&](const std::string &from, const std::string &to) { const auto at = objects.find(from); REQUIRE(at != std::string::npos); objects.replace(at, from.size(), to); };
	replace("SplitResult:PartB", "SplitResult:PartA"); replace("Target:PartB", "Target:PartA");
	movetest::MoveWorld f(100, 100, objects.c_str(), true); startBfme1Scripts(f);
	Player *p = f.players.findPlayerWithName("Alice");
	Object *a = f.make("PartA", p->getDefaultTeam()), *b = f.make("PartA", p->getDefaultTeam());
	Object *first = nullptr;
	for (Object *o = f.logic->getFirstObject(); o; o = o->getNextObject()) if (o == a || o == b) { first = o; break; }
	Object *second = first == a ? b : a;
	Coord3D near{100, 0, 0}, far{200, 0, 0}; first->setPosition(&near); second->setPosition(&far);
	Dict d; d.setInt("teamProductionPriority", 100); d.setAsciiString("teamUnitType1", "Combined"); d.setInt("teamUnitMaxCount1", 1);
	auto *proto = f.teams.initTeam("Combo", p, false, &d);
	f.logic->scriptEngine().executeAction(act("RECRUIT_TEAM", {sp(3, "Alice/Combo"), rp(0)}));
	REQUIRE(proto->getFirstTeam()); Object *result = proto->getFirstTeam()->getFirstMember(); REQUIRE(result);
	CHECK(result->getTemplate()->getName() == "Combined"); CHECK(result->getContain()->getContainCount() == 4);
	CHECK(a->isDestroyed()); CHECK(b->isDestroyed()); CHECK(result->getPosition()->x == 100.0f);
}

TEST_CASE("bfme1 ai: delivery states are registered only for BFME1 objects")
{
	for (bool bfme1 : {false, true})
	{
		movetest::MoveWorld f(100, 100, transportObjects, bfme1);
		Object *carrier = f.make("Carrier", f.players.findPlayerWithName("Alice")->getDefaultTeam()); REQUIRE(carrier);
		auto *ai = carrier->getAIUpdateInterface(); REQUIRE(ai);
		for (unsigned id : {28u, 29u, 30u}) CHECK((ai->stateMachine().findState(id) != nullptr) == bfme1);
		const auto stops = f.logic->report().stops;
		CHECK(std::count(stops.begin(), stops.end(), "[S-5017] BFME1 containers: general pathfinder equivalence, mesh-backed Bridge heights/damage, contested-building mood callbacks, contact coordinates and rampage completion remain unverified") == (bfme1 ? 1 : 0));
	}
}

namespace
{
class IndicatorObserver : public BehaviorModule
{
	std::vector<std::string> &m_calls;
public:
	IndicatorObserver(Thing *t, const ModuleData *d, std::vector<std::string> &calls) : BehaviorModule(t, d), m_calls(calls) {}
	void onBfme1IndicatorColorIndexChanged(int index) override
	{
		CHECK(getObject()->bfme1IndicatorColorIndex() == index); // store precedes broadcast.
		m_calls.push_back(getModuleTagName() + ":" + std::to_string(index));
	}
};
}
TEST_CASE("bfme1 ai: combination color callback stores then notifies every behavior even for repeated values")
{
	movetest::MoveWorld f(100, 100, "", true); std::vector<std::string> calls;
	f.w.modules.bindModuleProc("AutoHealBehavior", MODULETYPE_BEHAVIOR, [&calls](Thing *t, const ModuleData *d, const ModuleFactory::ModuleTemplate &) {
		return std::make_unique<IndicatorObserver>(t, d, calls);
	});
	REQUIRE(f.w.load("Object Observer\n Behavior = AutoHealBehavior One\n End\n Behavior = AutoHealBehavior Two\n End\nEnd\n").empty());
	Object *o = f.make("Observer", f.teamOf("Alice")); REQUIRE(o); CHECK(o->bfme1IndicatorColorIndex() == -1);
	o->setBfme1IndicatorColorIndex(9); o->setBfme1IndicatorColorIndex(9);
	CHECK(calls == std::vector<std::string>{"One:9", "Two:9", "One:9", "Two:9"});
	Object *walker = f.make("Walker", f.teamOf("Alice")); REQUIRE(walker); walker->setBfme1IndicatorColorIndex(4);
	XferSave save; walker->xfer(save); StateHasher before; walker->crc(before);
	walker->setBfme1IndicatorColorIndex(2); StateHasher changed; walker->crc(changed); CHECK_FALSE(changed.value() == before.value());
	XferLoad load(save.data()); walker->xfer(load); CHECK(walker->bfme1IndicatorColorIndex() == 4);
	StateHasher after; walker->crc(after); CHECK(after.value() == before.value());
}

TEST_CASE("bfme1 ai: transport healing follows the original x87 operation order without changing later editions")
{
	const char objects[] = "Object HealingRider\n KindOf = INFANTRY\n TransportSlotCount = 1\n Body = ActiveBody Body\n MaxHealth = 1000.3\n InitialHealth = 1\n End\nEnd\n"
		"Object Healer\n Behavior = TransportContain Carry\n Slots = 1\n PassengerFilter = ANY +INFANTRY\n HealthRegen%PerSec = 13.7\n End\nEnd\n";
	for (bool bfme1 : {false, true})
	{
		movetest::MoveWorld f(100, 100, objects, bfme1);
		Object *rider = f.make("HealingRider", f.teamOf("Alice")), *carrier = f.make("Healer", f.teamOf("Alice")); REQUIRE(rider); REQUIRE(carrier);
		auto *contain = dynamic_cast<TransportContain *>(carrier->getContain()); REQUIRE(contain); REQUIRE(contain->addToContain(rider));
		contain->update();
		// Native instruction trace 0x0022D6E8..0x0022D70B gives
		// 0x41DB4409; the established shared double path gives 0x41DB4408.
		CHECK(rider->getBodyModule()->getHealth() == (bfme1 ? 28.408220291137695f : 28.408218383789062f));
	}
}

TEST_CASE("bfme1 ai: returning scripted transports follow the appended path beyond the map edge")
{
	movetest::MoveWorld f(100, 100, transportObjects, true); f.buildMap();
	Object *carrier = f.spawn("Carrier", 100, 100); auto *ai = carrier->getAIUpdateInterface(); REQUIRE(ai);
	ai->stateMachine().clear(); ai->stateMachine().setGoalPosition(Coord3D{-100, 100, 0}); ai->stateMachine().setState(30); ai->wakeUpNow();
	const ObjectID id = carrier->getID(); bool crossedEdge = false;
	for (int i = 0; i < 300; ++i)
	{
		f.frames(1); Object *live = f.logic->findObjectByID(id); if (!live) break;
		if (live->getPosition()->x < 0.0f) crossedEdge = true; // tolerance: proves travel beyond the boundary before deletion, not a chosen stopping cell.
	}
	CHECK(crossedEdge); CHECK(f.logic->findObjectByID(id) == nullptr);
}

namespace
{
class SlotWrapper : public BehaviorModule, public ContainModuleInterface
{
	ContainedItemsList m_items;
public:
	SlotWrapper(Thing *t, const ModuleData *d) : BehaviorModule(t, d) {}
	ContainModuleInterface *getContain() override { return this; }
	const ContainedItemsList *getContainedItemsList() const override { return &m_items; }
	unsigned getContainCount() const override { return (unsigned)m_items.size(); }
	int getContainMax() const override { return 2; }
	bool isSpecialZeroSlotContainer() const override { return true; }
	bool addToContain(Object *obj) override { m_items.push_back(obj); return true; }
	void removeFromContain(Object *obj) override { m_items.remove(obj); }
};
class ExitRefusingAI : public AIUpdateInterface
{
public:
	ExitRefusingAI(Thing *t, const ModuleData *d) : AIUpdateInterface(t, d) {}
	bool isPassengerFreeToExit(const Object &) const override { return false; }
};
}
TEST_CASE("bfme1 ai: transparent wrappers use payload identity and recursive slot cost with virtual capacity")
{
	movetest::MoveWorld f(100, 100, transportObjects, true);
	f.w.modules.bindModuleProc("AutoHealBehavior", MODULETYPE_BEHAVIOR, [](Thing *t, const ModuleData *d, const ModuleFactory::ModuleTemplate &) {
		return std::make_unique<SlotWrapper>(t, d);
	});
	REQUIRE(f.w.load("Object Wrapper\n KindOf = TRANSPORT\n TransportSlotCount = 99\n Behavior = AutoHealBehavior Fake\n End\nEnd\n").empty());
	Object *wrapper = f.make("Wrapper", f.teamOf("Alice")), *outer = f.make("Wrapper", f.teamOf("Alice")); REQUIRE(wrapper); REQUIRE(outer);
	Object *carrier = f.make("Carrier", f.teamOf("Alice")); REQUIRE(carrier);
	CHECK(wrapper->getContain()->getContainMax() == 2); CHECK(TransportContain::transportSlotCount(*wrapper) == 0);
	Object *a = f.make("Rider", f.teamOf("Alice")), *b = f.make("Rider", f.teamOf("Alice")); REQUIRE(a); REQUIRE(b);
	wrapper->getContain()->addToContain(a); wrapper->getContain()->addToContain(b); outer->getContain()->addToContain(wrapper);
	CHECK(TransportContain::transportSlotCount(*wrapper) == 2); CHECK(TransportContain::transportSlotCount(*outer) == 2);
	CHECK(carrier->getContain()->isValidContainerFor(*wrapper, true, false));
	// BFME1 0x22DC25/0x22DC76 validates the first rider's cost, whereas
	// 0x22DD72 accounts for the whole wrapper. Native can overfill this way.
	Object *already = f.make("Rider", f.teamOf("Alice")); REQUIRE(already);
	REQUIRE(carrier->getContain()->addToContain(already));
	CHECK(carrier->getContain()->getContainMax() == 2);
	CHECK(carrier->getContain()->isValidContainerFor(*wrapper, true, false));
	REQUIRE(carrier->getContain()->addToContain(wrapper));
	CHECK(carrier->getContain()->getContainCount() == 2);
	Object *next = f.make("Rider", f.teamOf("Alice")); REQUIRE(next);
	CHECK_FALSE(carrier->getContain()->isValidContainerFor(*next, true, false));
}
TEST_CASE("bfme1 ai: transport exit obeys the native AI permission virtual")
{
	for (bool bfme1 : {false, true})
	{
		movetest::MoveWorld f(100, 100, transportObjects, bfme1); f.buildMap();
		f.w.modules.bindModuleProc("AIUpdateInterface", MODULETYPE_BEHAVIOR, [](Thing *t, const ModuleData *d, const ModuleFactory::ModuleTemplate &) {
			return std::make_unique<ExitRefusingAI>(t, d);
		});
		Object *carrier = f.spawn("Carrier", 100, 100), *rider = f.spawn("Rider", 100, 100); REQUIRE(carrier); REQUIRE(rider);
		auto *contain = dynamic_cast<TransportContain *>(carrier->getContain()); REQUIRE(contain);
		CHECK(contain->isSpecificRiderFreeToExit(*rider) == !bfme1);
	}
}
TEST_CASE("bfme1 ai: an indestructible transport gives newly accepted payload the native protection")
{
	const char objects[] = "Object ProtectedRider\n KindOf = INFANTRY\n TransportSlotCount = 1\n Body = ActiveBody Body\n MaxHealth = 100\n End\nEnd\n"
		"Object ProtectedCarrier\n Body = ActiveBody Body\n MaxHealth = 100\n End\n Behavior = TransportContain Carry\n Slots = 2\n PassengerFilter = ANY +INFANTRY\n InitialPayload = ProtectedRider 1\n End\nEnd\n";
	for (bool bfme1 : {false, true})
	{
		movetest::MoveWorld f(100, 100, objects, bfme1); Object *carrier = f.make("ProtectedCarrier", f.teamOf("Alice")); REQUIRE(carrier);
		auto *contain = dynamic_cast<TransportContain *>(carrier->getContain()); REQUIRE(contain); CHECK(contain->getContainCount() == (bfme1 ? 0 : 1));
		carrier->getBodyModule()->setIndestructible(true); carrier->friend_onBuildComplete(); REQUIRE(contain->getContainCount() == 1);
		CHECK(contain->getContainedItemsList()->back()->getBodyModule()->isIndestructible() == bfme1);
		carrier->friend_onBuildComplete(); CHECK(contain->getContainCount() == 1);
	}
}

TEST_CASE("bfme1 ai: container entry keeps BFME1 relationship and mounted-unit rules")
{
	const char objects[] = "Object EntryRider\n KindOf = INFANTRY\n TransportSlotCount = 1\nEnd\n"
		"Object EntryCarrier\n Behavior = TransportContain Carry\n Slots = 2\n PassengerFilter = ANY +INFANTRY -CAVALRY\n AllowEnemiesInside = No\n AllowNeutralInside = No\n End\nEnd\n";
	for (bool bfme1 : {false, true})
	{
		movetest::MoveWorld f(100, 100, objects, bfme1);
		Object *carrier = f.make("EntryCarrier", f.teamOf("Alice")); REQUIRE(carrier);
		Object *enemy = f.make("EntryRider", f.teamOf("Bob")); REQUIRE(enemy);
		enemy->setStatus((unsigned)CombatNames::status("CAN_ENTER_ANYTHING"), true);
		CHECK(carrier->getContain()->isValidContainerFor(*enemy, false, false) == !bfme1);
		Object *own = f.make("EntryRider", f.teamOf("Alice")); REQUIRE(own);
		own->setStatus((unsigned)CombatNames::status("HOLDING_THE_RING"), true);
		CHECK(carrier->getContain()->isValidContainerFor(*own, false, false) == bfme1);
		own->setStatus((unsigned)CombatNames::status("HOLDING_THE_RING"), false);
		own->setModelConditionState(CombatNames::modelCondition("MOUNTED"), true);
		CHECK(carrier->getContain()->isValidContainerFor(*own, false, false) == bfme1);
	}
}

#include "GameLogic/Weapon.h"
#include "GameLogic/WeaponStores.h"
#include "GameLogic/ContainParseHooks.h"
TEST_CASE("bfme1 ai: grab weapon fires around the carrier and protects its passenger unless configured otherwise")
{
	bool fireOnVictim = false;
	SUBCASE("passenger is protected during the area shot") {}
	SUBCASE("FireGrabWeaponOnVictim permits passenger damage") { fireOnVictim = true; }
	WeaponStores stores; stores.install();
	std::string objects = "Object GrabRider\n KindOf = INFANTRY SELECTABLE\n TransportSlotCount = 1\n Geometry = CYLINDER\n GeometryMajorRadius = 2\n Body = ActiveBody Body\n MaxHealth = 100\n End\nEnd\n"
		"Object GrabCarrier\n Behavior = TransportContain Carry\n Slots = 1\n PassengerFilter = ANY +INFANTRY\n AllowEnemiesInside = Yes\n GrabWeapon = GrabBlast\n FireGrabWeaponOnVictim = ";
	objects += fireOnVictim ? "Yes\n End\nEnd\n" : "No\n End\nEnd\n";
	movetest::MoveWorld f(100, 100, "", true);
	struct HookGuard { ContainParseHooks saved = TheContainParseHooks(); ~HookGuard() { TheContainParseHooks() = saved; } } hooks;
	TheContainParseHooks().weaponTemplateExists = [](const std::string &name) { return TheWeaponStore && TheWeaponStore->findWeaponTemplate(name); };
	f.w.fx.env.blocks.registerBlock("Weapon", [](INI *ini) { WeaponStore::parseWeaponTemplateDefinitionGlobal(ini); });
	REQUIRE(f.w.load("Weapon GrabBlast\n AttackRange = 100\n DamageNugget\n Damage = 10\n Radius = 30\n DamageType = FORCE\n DeathType = NORMAL\n End\nEnd\n").empty());
	REQUIRE(f.w.load(objects).empty());
	Object *carrier = f.make("GrabCarrier", f.teamOf("Alice")), *rider = f.make("GrabRider", f.teamOf("Bob")); REQUIRE(carrier); REQUIRE(rider);
	f.logic->partition().setRegion(0, 0, 1000, 1000); f.buildMap();
	Coord3D at{100, 100, 0}; carrier->setPosition(&at); rider->setPosition(&at);
	auto *contain = dynamic_cast<TransportContain *>(carrier->getContain()); REQUIRE(contain); REQUIRE(contain->addToContain(rider));
	CHECK_FALSE(rider->testStatus((unsigned)CombatNames::status("UNATTACKABLE")));
	CHECK(rider->getBodyModule()->getHealth() == (fireOnVictim ? 90.0f : 100.0f));
	CHECK(contain->stats().unported == 0);
}

TEST_CASE("bfme1 ai: unloading idles the passenger and resets its mood check using BFME1 rules")
{
	std::string objects = transportObjects;
	objects.replace(objects.find("Slots = 2"), 9, "Slots = 2\n ResetMoodCheckTimeOnExit = Yes");
	for (bool bfme1 : {false, true})
	{
		movetest::MoveWorld f(100, 100, objects.c_str(), bfme1); f.buildMap();
		Object *carrier = f.spawn("Carrier", 100, 100), *rider = f.spawn("Rider", 100, 100); REQUIRE(carrier); REQUIRE(rider);
		auto *ai = rider->getAIUpdateInterface(); REQUIRE(ai);
		ai->aiMoveToPosition(Coord3D{400, 100, 0}, CMD_FROM_SCRIPT); const unsigned before = ai->currentStateId();
		REQUIRE(before != (unsigned)AI_IDLE);
		ai->setNextMoodCheckTime(99);
		REQUIRE(carrier->getContain()->addToContain(rider)); carrier->getContain()->removeFromContain(rider);
		CHECK(ai->currentStateId() == (bfme1 ? (unsigned)AI_IDLE : before));
		CHECK(ai->nextMoodCheckTime() == (bfme1 ? f.logic->getFrame() : 99)); CHECK(ai->randomlyOffsetMoodCheck() == bfme1);
	}
}

#include "GameClient/ClientEvents.h"
#include "GameClient/DrawableManager.h"
TEST_CASE("bfme1 ai: passenger fades honor reverse, filters, horde members and edition isolation")
{
	for (bool bfme1 : {false, true}) for (bool reverse : {false, true})
	{
		std::string objects = transportObjects + combinationObjects();
		const auto at = objects.find("Slots = 2\n PassengerFilter = ANY +INFANTRY");
		objects.replace(at, std::string("Slots = 2\n PassengerFilter = ANY +INFANTRY").size(),
			"Slots = 10\n PassengerFilter = ANY +INFANTRY +HORDE\n FadeFilter = ANY +INFANTRY +HORDE -HERO\n FadePassengerOnEnter = Yes\n FadePassengerOnExit = Yes\n EnterFadeTime = 1000\n ExitFadeTime = 2000\n FadeReverse = " + std::string(reverse ? "Yes" : "No"));
		objects += "Object Unfaded\n KindOf = INFANTRY HERO\nEnd\n";
		movetest::MoveWorld f(100, 100, objects.c_str(), bfme1); f.buildMap();
		ClientEventRecorder events(*f.logic); f.logic->setClientHooks(&events);
		Object *carrier = f.spawn("Carrier", 100, 100), *rider = f.spawn("Rider", 100, 100); REQUIRE(carrier); REQUIRE(rider);
		events.take(); REQUIRE(carrier->getContain()->addToContain(rider));
		auto enter = events.take();
		std::vector<ClientEvent> fades;
		for (const auto &e : enter) if (e.kind == ClientEvent::FADE_IN || e.kind == ClientEvent::FADE_OUT) fades.push_back(e);
		CHECK(fades.size() == (bfme1 ? 1 : 0));
		if (bfme1) { CHECK(fades[0].object == rider->getID()); CHECK(fades[0].fadeFrames == 30); CHECK(fades[0].kind == (reverse ? ClientEvent::FADE_OUT : ClientEvent::FADE_IN)); }
		carrier->getContain()->removeFromContain(rider);
		auto exit = events.take(); fades.clear();
		for (const auto &e : exit) if (e.kind == ClientEvent::FADE_IN) fades.push_back(e);
		CHECK(fades.size() == (bfme1 ? 1 : 0));
		if (bfme1) { CHECK(fades[0].object == rider->getID()); CHECK(fades[0].fadeFrames == 60); }
		Object *horde = f.spawn("PartA", 100, 100); REQUIRE(horde); horde->friend_onBuildComplete(); events.take();
		REQUIRE(carrier->getContain()->addToContain(horde));
		enter = events.take(); fades.clear();
		for (const auto &e : enter) if (e.kind == ClientEvent::FADE_IN || e.kind == ClientEvent::FADE_OUT) fades.push_back(e);
		CHECK(fades.size() == (bfme1 ? 2 : 0));
		for (const auto &e : fades) CHECK(e.object != horde->getID());
		carrier->getContain()->removeFromContain(horde); events.take();
		Object *unfaded = f.spawn("Unfaded", 100, 100); REQUIRE(unfaded); events.take();
		REQUIRE(carrier->getContain()->addToContain(unfaded));
		for (const auto &e : events.take()) CHECK_FALSE((e.kind == ClientEvent::FADE_IN || e.kind == ClientEvent::FADE_OUT));
		f.logic->setClientHooks(nullptr);
	}
}

TEST_CASE("bfme1 ai: slaughter uses bounty value and original percent rounding")
{
	const char objects[] =
		"Object Offering\n KindOf = INFANTRY SELECTABLE\n BuildCost = 999\n BountyValue = 101\n Body = ActiveBody Body\n MaxHealth = 100\n End\nEnd\n"
		"Object Slaughter\n KindOf = STRUCTURE SELECTABLE FS_CASH_PRODUCER\n Body = ActiveBody Body\n MaxHealth = 1000\n End\n"
		" Behavior = SlaughterHordeContain Contain\n MaxHordeCapacity = 3\n PassengerFilter = ANY +INFANTRY +HORDE\n CashBackPercent = 50%\n End\nEnd\n";
	for (bool bfme1 : {false, true})
	{
		std::string config = objects;
		std::string hordes = combinationObjects();
		for (size_t pos = 0; (pos = hordes.find("MemberOfHorde", pos)) != std::string::npos; pos += 8) hordes.replace(pos, 13, "Offering");
		config += hordes;
		if (!bfme1) config.replace(config.find("MaxHordeCapacity"), 16, "ContainMax");
		movetest::MoveWorld f(100, 100, "", bfme1); f.buildMap();
		if (bfme1)
		{
			initest::Fixture parse({{"Data\\INI\\Default\\SubsystemLegend.ini", "; BFME1"}});
			parse.env.blocks = f.w.fx.env.blocks; parse.env.macros = f.w.fx.env.macros;
			REQUIRE_MESSAGE(initest::loadError(parse.env, "slaughter.ini", config).empty(), config);
		}
		else REQUIRE(f.w.load(config).empty());
		Object *house = f.make("Slaughter", f.teamOf("Alice")); REQUIRE(house);
		if (!bfme1) { CHECK(house->findModule("SlaughterHordeContain")->isUnported()); continue; }
		Object *offering = f.make("Offering", f.teamOf("Alice")); REQUIRE(offering); REQUIRE(house->getContain());
		Player *owner = offering->getControllingPlayer(); REQUIRE(owner);
		const auto money = owner->getMoney()->countMoney();
		REQUIRE(house->getContain()->addToContain(offering));
		CHECK(offering->isDestroyed()); CHECK(owner->getMoney()->countMoney() == money + 50);
		CHECK(house->getContain()->getContainMax() == 3);
		Object *wrapper = f.spawn("PartA", 100, 100); REQUIRE(wrapper);
		auto *horde = wrapper->getContain()->getHordeContainInterface(); REQUIRE(horde);
		const auto *items = wrapper->getContain()->getContainedItemsList(); REQUIRE(items); REQUIRE(items->size() == 2);
		std::vector<Object *> members(items->begin(), items->end());
		horde->releaseMembersToward(house, CMD_FROM_AI);
		REQUIRE(house->getContain()->addToContain(wrapper));
		REQUIRE(house->getContain()->addToContain(members[0]));
		CHECK_FALSE(members[0]->isDestroyed()); CHECK(members[0]->getContainedBy() == wrapper);
		CHECK_FALSE(wrapper->isDestroyed()); CHECK_FALSE(horde->allMembersEntered());
		CHECK(owner->getMoney()->countMoney() == money + 100);
		REQUIRE(house->getContain()->addToContain(members[1]));
		CHECK(wrapper->isDestroyed()); CHECK(members[0]->isDestroyed()); CHECK(members[1]->isDestroyed());
		CHECK(owner->getMoney()->countMoney() == money + 150);
	}
}

TEST_CASE("bfme1 ai: scripted rampage arms falling riders at the native deadline")
{
	for (bool requiresAflame : {false, true}) for (bool aflame : {false, true}) for (int period : {0, 1000})
	{
		std::string objects = transportObjects;
		objects.replace(objects.find("Behavior = TransportContain Carry"), 33, "Behavior = HordeTransportContain Carry");
		const auto begin = objects.find("Object Carrier");
		const auto ai = objects.find("Behavior = AIUpdateInterface AI\n", begin);
		objects.insert(ai + std::string("Behavior = AIUpdateInterface AI\n").size(),
			" RampageTime = 10000\n TimeToEjectPassengersOnRampage = " + std::to_string(period) +
			"\n RampageRequiresAflame = " + (requiresAflame ? "Yes" : "No") + "\n");
		objects.insert(begin + std::string("Object Carrier\n").size(), " Body = ActiveBody Body\n MaxHealth = 100\n End\n");
		movetest::MoveWorld f(100, 100, objects.c_str(), true); f.buildMap();
		Object *carrier = f.spawn("Carrier", 100, 100); REQUIRE(carrier);
		auto *contain = dynamic_cast<HordeTransportContain *>(carrier->getContain()); REQUIRE(contain);
		carrier->setStatus((unsigned)CombatNames::status("AFLAME"), aflame);
		REQUIRE(Bfme1SkirmishAI::enterRampage(*carrier));
		auto *brain = carrier->getAIUpdateInterface(); REQUIRE(brain);
		CHECK(brain->currentStateId() == 45);
		CHECK_FALSE(contain->ridersFalling());
		CHECK_FALSE(brain->allowedToRespondToCommand(CMD_FROM_PLAYER, 0));
		CHECK_FALSE(brain->allowedToRespondToCommand(CMD_FROM_SCRIPT, 0));
		CHECK(brain->allowedToRespondToCommand(CMD_FROM_AI, 0));
		if (requiresAflame && !aflame)
		{
			brain->stateMachine().updateStateMachine();
			CHECK(contain->ridersFalling() == (period == 1000));
			continue;
		}
		f.frames(4);
		CHECK_FALSE(contain->ridersFalling());
		XferSave saved; brain->xferAIState(saved); const auto hash = f.logic->computeStateHash();
		XferLoad loaded(saved.data()); brain->xferAIState(loaded); CHECK(f.logic->computeStateHash() == hash);
		CHECK(brain->currentStateId() == 45);
		f.frames(1);
		CHECK(contain->ridersFalling() == (period == 1000));
		// Repeating the Lua request does not restart a running rampage.
		REQUIRE(Bfme1SkirmishAI::enterRampage(*carrier));
		CHECK(contain->ridersFalling() == (period == 1000));
	}
	movetest::MoveWorld later(100, 100, transportObjects, false); later.buildMap();
	Object *carrier = later.spawn("Carrier", 100, 100); REQUIRE(carrier);
	CHECK_FALSE(Bfme1SkirmishAI::enterRampage(*carrier));
	CHECK(carrier->getAIUpdateInterface()->stateMachine().findState(45) == nullptr);
}

TEST_CASE("bfme1 ai: zero-frame fades keep the BFME1 start state until the client step")
{
	for (bool bfme1 : {false, true})
	{
		movetest::MoveWorld f(100, 100, transportObjects, bfme1); f.buildMap();
		ArchiveW3DFileSource source(f.w.fx.fsys); WW3DAssetManager assets(source);
		DrawableManager drawables(assets, *f.logic);
		ClientEventRecorder events(*f.logic); f.logic->setClientHooks(&events);
		Object *rider = f.spawn("Rider", 100, 100); REQUIRE(rider);
		drawables.applyEvents(events.take()); Drawable *draw = drawables.findByObject(rider->getID()); REQUIRE(draw);
		draw->fadeIn(0);
		CHECK(draw->fade().mode == (bfme1 ? 1 : 0)); CHECK(draw->fade().opacity == (bfme1 ? 0.0f : 1.0f));
		draw->updateFade(0); CHECK(draw->fade().mode == 0); CHECK(draw->fade().opacity == 1.0f);
		draw->fadeOut(0);
		CHECK(draw->fade().mode == (bfme1 ? 2 : 0)); CHECK(draw->fade().opacity == (bfme1 ? 1.0f : 0.0f));
		draw->updateFade(0); CHECK(draw->fade().mode == 0); CHECK(draw->fade().opacity == 0.0f);
		f.logic->setClientHooks(nullptr);
	}
}

#include "GameLogic/Module/UpgradeModule.h"
#include "GameLogic/Module/PhysicsBehavior.h"
#include "GameLogic/Module/CollideModule.h"
#include "Common/Upgrade.h"

TEST_CASE("bfme1 ai: slaughter replaces upgrades by exact team and flushes saved deferred scores once")
{
	std::string objects=combinationObjects()+
		"Upgrade Upgrade_OfferingA\n Type = OBJECT\nEnd\nUpgrade Upgrade_OfferingB\n Type = OBJECT\nEnd\n"
		"Object Offering\n KindOf = INFANTRY SELECTABLE\n BountyValue = 100\n Body = ActiveBody Body\n MaxHealth = 100\n End\nEnd\n"
		"Object House\n KindOf = STRUCTURE\n Body = ActiveBody Body\n MaxHealth = 1000\n End\n"
		" Behavior = SlaughterHordeContain Contain\n MaxHordeCapacity = 3\n CashBackPercent = 50%\n PassengerFilter = ANY +INFANTRY +HORDE\n End\n"
		" Behavior = ArmorUpgrade PermanentArmor\n TriggeredBy = Upgrade_OfferingA\n Permanent = Yes\n ArmorSetFlag = PLAYER_UPGRADE\n End\nEnd\n";
	movetest::MoveWorld f(100,100,"",true);f.buildMap();
	initest::Fixture parse({{"Data\\INI\\Default\\SubsystemLegend.ini", "; BFME1"}});
	parse.env.blocks=f.w.fx.env.blocks;parse.env.macros=f.w.fx.env.macros;
	REQUIRE_MESSAGE(initest::loadError(parse.env,"slaughter-transfer.ini",objects).empty(),objects);
	Team *first=f.teamOf("Alice");Player *alice=first->getControllingPlayer();REQUIRE(alice);
	Team *second=f.teams.createTeam(f.teams.initTeam("OtherAliceTeam",alice,false),true);
	Object *house=f.make("House",first);REQUIRE(house);
	auto *mux=house->findModule("ArmorUpgrade")->getUpgrade();REQUIRE(mux);
	auto feed=[&](Team *team,const char *upgrade){Object *o=f.make("Offering",team);REQUIRE(o);o->giveUpgrade(upgrade);REQUIRE(house->getContain()->addToContain(o));};
	feed(first,"Upgrade_OfferingA");CHECK(house->hasUpgrade("Upgrade_OfferingA"));CHECK(mux->isAlreadyUpgraded());
	feed(first,"Upgrade_OfferingB");CHECK(house->hasUpgrade("Upgrade_OfferingA"));CHECK(house->hasUpgrade("Upgrade_OfferingB"));
	feed(second,"Upgrade_OfferingB");CHECK_FALSE(house->hasUpgrade("Upgrade_OfferingA"));CHECK(house->hasUpgrade("Upgrade_OfferingB"));CHECK_FALSE(mux->isAlreadyUpgraded());
	CHECK_FALSE((house->armorSetFlags() & (1u << CombatNames::armorSetBit("PLAYER_UPGRADE"))));
	// The last arrival records the whole horde count. Next controller's
	// arrival performs the correction even with normal score collection off.
	Object *wrapper=f.spawn("PartA",100,100,0,first);REQUIRE(wrapper);
	auto *horde=wrapper->getContain()->getHordeContainInterface();REQUIRE(horde);
	std::vector<Object *> members(wrapper->getContain()->getContainedItemsList()->begin(),wrapper->getContain()->getContainedItemsList()->end());
	horde->releaseMembersToward(house,CMD_FROM_AI);REQUIRE(house->getContain()->addToContain(wrapper));
	for(Object *m:members)REQUIRE(house->getContain()->addToContain(m));
	CHECK(wrapper->isDestroyed());
	StateHasher before;house->findModule("SlaughterHordeContain")->crc(before);
	XferSave save;house->findModule("SlaughterHordeContain")->xfer(save);
	XferLoad load(save.data());house->findModule("SlaughterHordeContain")->xfer(load);
	StateHasher after;house->findModule("SlaughterHordeContain")->crc(after);CHECK(after.value()==before.value());
	Player *bob=f.teamOf("Bob")->getControllingPlayer();REQUIRE(bob);
	auto &old=alice->getScoreKeeper();auto &next=bob->getScoreKeeper();old.setEnabled(false);next.setEnabled(false);next.setCounting(false);
	const int lost=old.unitsLost(),destroyed=next.unitsDestroyed(alice->getPlayerIndex());
	feed(f.teamOf("Bob"),"Upgrade_OfferingB");CHECK(old.unitsLost()==lost+2);CHECK(next.unitsDestroyed(alice->getPlayerIndex())==destroyed+2);
	CHECK(old.lostByTemplate().at("MemberOfHorde")>=2); // tolerance: ordinary deaths may already have scored these same soldiers.
	CHECK(next.destroyedByTemplate(alice->getPlayerIndex()).at("MemberOfHorde")==2);
	feed(f.teamOf("Bob"),"Upgrade_OfferingB");CHECK(old.unitsLost()==lost+2);CHECK(next.unitsDestroyed(alice->getPlayerIndex())==destroyed+2);
}

TEST_CASE("bfme1 ai: AOD uses flowing rows waves large-unit expiry and a saved scatter distance")
{
	std::string config=combinationObjects();
	auto begin=config.find("Behavior = HordeContain");config.replace(begin,std::string("Behavior = HordeContain").size(),"Behavior = AODHordeContain");
	config.insert(config.find(" Slots =",begin)," DeathTypes = ALL\n ExemptStatus = NONE\n RequiredStatus = NONE\n DamageAmountRequired = 12.5\n MinKillerAngle = -45\n MaxKillerAngle = 45\n");
	config.insert(config.find(" Slots =",begin)," FrequencyScale = 0\n FrequencyRandomness = 0\n AmplitudeScaleZ = 10\n StillAmplitudeZ = 1\n LargeUnitTimeout = 800\n LargeUnitMinHeight = 100\n LargeUnitMaxHeight = 100\n");
	config+="Object Large\n DeadCollideSize = LARGE\n Geometry = CYLINDER\n GeometryHeight = 200\n Body = ActiveBody Body\n MaxHealth = 1000\n End\nEnd\n";
	movetest::MoveWorld f(100,100,"",true);f.buildMap();
	initest::Fixture parse({{"Data\\INI\\Default\\SubsystemLegend.ini", "; BFME1"}});
	parse.env.blocks=f.w.fx.env.blocks;parse.env.macros=f.w.fx.env.macros;
	REQUIRE_MESSAGE(initest::loadError(parse.env,"aod.ini",config).empty(),config);
	f.logic->random().enableCallLog(true);
	Object *wrapper=f.spawn("PartA",200,200);REQUIRE(wrapper);auto *horde=dynamic_cast<HordeContain *>(wrapper->getContain());REQUIRE(horde);
	const auto stops=f.logic->report().stops;
	CHECK(std::count(stops.begin(),stops.end(),"[S-5017] BFME1 AOD: inherited death-filter fields are retained; death-filter execution remains unported")==1);
	std::vector<int> sites;
	for(const auto &call:f.logic->random().callLog())if(call.file.find("AODHordeContain.cpp")!=std::string::npos){sites.push_back(call.line);CHECK_FALSE(call.drewNumber);}
	CHECK(sites==std::vector<int>{0x209,0x20A,0x20B,0x20E,0x20F,0x210,0x209,0x20A,0x20B,0x20E,0x20F,0x210});
	Object *member=horde->getContainedItemsList()->front();REQUIRE(member);
	Coord3D base=horde->getMemberFormationPosition(member);CHECK(base.z==9.5f);
	Object *large=f.make("Large",f.teamOf("Bob"));REQUIRE(large);large->setPosition(&base);
	REQUIRE_FALSE(large->isEffectivelyDead());const auto *sizeValue=large->getTemplate()->findField("DeadCollideSize");REQUIRE(sizeValue);const auto *rawSize=std::get_if<long long>(sizeValue);REQUIRE(rawSize);CHECK(*rawSize==2);
	horde->trackLargeUnit(large);Coord3D lifted=horde->getMemberFormationPosition(member);CHECK(lifted.x==base.x);CHECK(lifted.y==base.y);CHECK(lifted.z==109.5f);
	XferSave save;horde->xfer(save);StateHasher before;horde->crc(before);
	XferLoad load(save.data());horde->xfer(load);StateHasher after;horde->crc(after);CHECK(after.value()==before.value());
	f.frames(1);horde->trackLargeUnit(large); // same id does not refresh the timestamp.
	f.frames(1);CHECK(horde->getMemberFormationPosition(member).z==109.5f);
	f.frames(1);CHECK(horde->getMemberFormationPosition(member).z==59.5f);
	f.frames(1);CHECK(horde->getMemberFormationPosition(member).z==9.5f);
	member->setModelConditionState(CombatNames::modelCondition("JUST_BUILT"),false);member->getAIUpdateInterface()->aiBusy(CMD_FROM_AI);
	member->setPosition(&base);horde->runMemberPass();CHECK(member->getPosition()->z==9.5f);
	wrapper->setModelConditionState(CombatNames::modelCondition("OATH_FULLFILLED"),true);wrapper->setModelConditionState(CombatNames::modelCondition("MOVING"),true);
	horde->update();horde->update(); // first mirrors the condition, second uses the accumulated native scatter distance.
	Coord3D scattered=horde->getMemberFormationPosition(member);CHECK(scattered.z>base.z); // tolerance: positive native scatter, no fixed frame-dependent wave assumed.
	XferSave scatteredSave;horde->xfer(scatteredSave);StateHasher scatteredHash;horde->crc(scatteredHash);
	XferLoad scatteredLoad(scatteredSave.data());horde->xfer(scatteredLoad);StateHasher restored;horde->crc(restored);CHECK(restored.value()==scatteredHash.value());
}

TEST_CASE("bfme1 ai: AOD contacts select special damage apply retaliation and retain firing through the deadline")
{
	const char config[]=
		"Object Ghost\n KindOf = INFANTRY\n Body = ActiveBody Body\n MaxHealth = 100\n End\n Behavior = PhysicsBehavior Physics\n End\n"
		" Behavior = AODCrushCollide Crush\n Damage = 7\n DamageType = UNRESISTABLE\n SpecialObject = ANY +MONSTER\n SpecialDamage = 11\n SpecialDamageType = UNRESISTABLE\n SelfDamage = 3\n SelfDamageType = UNRESISTABLE\n End\nEnd\n"
		"Object Victim\n KindOf = INFANTRY\n Body = ActiveBody Body\n MaxHealth = 100\n End\n Behavior = PhysicsBehavior Physics\n End\nEnd\n"
		"Object Special\n KindOf = MONSTER\n DeadCollideSize = LARGE\n Body = ActiveBody Body\n MaxHealth = 100\n End\n Behavior = PhysicsBehavior Physics\n End\nEnd\n";
	for(bool original:{false,true})
	{
		movetest::MoveWorld f(100,100,"",original);f.buildMap();
		if(original){initest::Fixture parse({{"Data\\INI\\Default\\SubsystemLegend.ini", "; BFME1"}});parse.env.blocks=f.w.fx.env.blocks;parse.env.macros=f.w.fx.env.macros;REQUIRE_MESSAGE(initest::loadError(parse.env,"aod-contact.ini",config).empty(),config);}
		else REQUIRE(f.w.load(config).empty());
		Object *ghost=f.make("Ghost",f.teamOf("Alice"));REQUIRE(ghost);
		if(!original){CHECK(ghost->findModule("AODCrushCollide")->isUnported());continue;}
		auto *collide=ghost->findModule("AODCrushCollide")->getCollide();REQUIRE(collide);
		Object *victim=f.make("Victim",f.teamOf("Bob")),*special=f.make("Special",f.teamOf("Bob"));REQUIRE(victim);REQUIRE(special);
		collide->onCollide(victim,nullptr,nullptr);CHECK(victim->getBodyModule()->getHealth()==93);CHECK(ghost->getBodyModule()->getHealth()==100);
		collide->onCollide(special,nullptr,nullptr);CHECK(special->getBodyModule()->getHealth()==89);CHECK(ghost->getBodyModule()->getHealth()==97);
		ghost->setModelConditionState(CombatNames::modelCondition("OATH_FULLFILLED"),true);collide->onCollide(victim,nullptr,nullptr);CHECK(victim->getBodyModule()->getHealth()==93);
		ghost->setModelConditionState(CombatNames::modelCondition("OATH_FULLFILLED"),false);
		const int firing=CombatNames::modelCondition("FIRING_A");CHECK(ghost->testModelCondition(firing));f.frames(10);CHECK(ghost->testModelCondition(firing));f.frames(1);CHECK_FALSE(ghost->testModelCondition(firing));
	}
}

TEST_CASE("bfme1 ai: horse reform clamps positive speed and exchanges the native adjacent slot pair")
{
	std::string config=combinationObjects();
	bool multipleRanks=false;
	SUBCASE("one rank uses adjacent odd-even pairs") {}
	SUBCASE("two ranks allow all later same-template members")
	{
		multipleRanks=true;const auto pos=config.find(" Position:X:20 Y:0 Position:X:30 Y:0");REQUIRE(pos!=std::string::npos);
		config.replace(pos,std::string(" Position:X:20 Y:0 Position:X:30 Y:0").size(),"\n RankInfo = RankNumber:2 UnitType:MemberOfHorde Position:X:20 Y:0 Position:X:30 Y:0");
	}
	for(size_t pos=0;(pos=config.find("Behavior = HordeContain",pos))!=std::string::npos;pos+=27)
		config.replace(pos,std::string("Behavior = HordeContain").size(),"Behavior = HorseHordeContain");
	movetest::MoveWorld f(100,100,"",true);f.buildMap();
	initest::Fixture parse({{"Data\\INI\\Default\\SubsystemLegend.ini", "; BFME1"}});
	parse.env.blocks=f.w.fx.env.blocks;parse.env.macros=f.w.fx.env.macros;
	REQUIRE_MESSAGE(initest::loadError(parse.env,"horse.ini",config).empty(),config);
	Object *wrapper=f.spawn("Combined",200,200);REQUIRE(wrapper);auto *horde=dynamic_cast<HordeContain *>(wrapper->getContain());REQUIRE(horde);
	auto *loco=wrapper->getAIUpdateInterface()->curLocomotor();REQUIRE(loco);
	loco->setSpeed(2);horde->beginReform();CHECK(loco->speed()==0);
	loco->setSpeed(-2);horde->beginReform();CHECK(loco->speed()==-2);
	std::vector<Object *> members(horde->getContainedItemsList()->begin(),horde->getContainedItemsList()->end());REQUIRE(members.size()==4);
	REQUIRE(horde->getMemberSlot(members[1])==1);REQUIRE(horde->getMemberSlot(members[2])==2);
	Coord3D one=horde->getMemberFormationPosition(members[1]),two=horde->getMemberFormationPosition(members[2]);members[1]->setPosition(&two);members[2]->setPosition(&one);
	horde->endReform();CHECK(horde->getMemberSlot(members[1])==2);CHECK(horde->getMemberSlot(members[2])==1);
	CHECK(horde->getMemberSlot(members[0])==0);CHECK(horde->getMemberSlot(members[3])==3);
	Coord3D zero=horde->getMemberFormationPosition(members[0]),three=horde->getMemberFormationPosition(members[3]);members[0]->setPosition(&three);members[3]->setPosition(&zero);
	horde->endReform();CHECK(horde->getMemberSlot(members[0])==(multipleRanks?3:0));CHECK(horde->getMemberSlot(members[3])==(multipleRanks?0:3));
}

#include "GameLogic/AI/Bfme1ContactManager.h"
#include "GameLogic/Object/Contain/Bfme1HordeRuntime.h"
namespace
{
class ContactObserver final : public BehaviorModule,public CollideModuleInterface
{
	std::vector<std::pair<ObjectID,ObjectID>> &m_calls;
	bool &m_remove;
public:
	ContactObserver(Thing *t,const ModuleData *d,std::vector<std::pair<ObjectID,ObjectID>> &calls,bool &remove):BehaviorModule(t,d),m_calls(calls),m_remove(remove){}
	CollideModuleInterface *getCollide() override{return this;}
	bool wouldLikeToCollideWith(const Object &) const override{return false;}
	void onCollide(Object *other,const Coord3D *,const Coord3D *) override
	{
		m_calls.emplace_back(getObject()->getID(),other->getID());
		if(m_remove)other->friend_setOutOfCollisionManager(true);
	}
};
const char contactObjects[]=
	"Object ContactProbe\n KindOf = INFANTRY\n Geometry = BOX\n GeometryMajorRadius = 10\n GeometryMinorRadius = 10\n GeometryHeight = 20\n"
	" Behavior = AutoHealBehavior Probe\n End\nEnd\n";
void bindContactObserver(movetest::MoveWorld &f,std::vector<std::pair<ObjectID,ObjectID>> &calls,bool &remove)
{
	f.w.modules.bindModuleProc("AutoHealBehavior",MODULETYPE_BEHAVIOR,[&calls,&remove](Thing *t,const ModuleData *d,const ModuleFactory::ModuleTemplate &){return std::make_unique<ContactObserver>(t,d,calls,remove);});
	REQUIRE(f.w.load(contactObjects).empty());
}
}
TEST_CASE("bfme1 ai: persistent contacts cache tests follow bucket order and separate height and groups")
{
	movetest::MoveWorld f(100,100,"",true);f.buildMap();std::vector<std::pair<ObjectID,ObjectID>> calls;bool remove=false;bindContactObserver(f,calls,remove);
	Object *a=f.spawn("ContactProbe",200,200),*b=f.spawn("ContactProbe",201,200),*c=f.spawn("ContactProbe",202,200);REQUIRE(a);REQUIRE(b);REQUIRE(c);
	REQUIRE(f.ai->usesNativeContacts());f.ai->processCollisions();
	const auto *manager=f.ai->nativeContacts();REQUIRE(manager);CHECK(manager->contacts()==3);CHECK(manager->tests()==3);CHECK(calls.size()==6);
	// Native 0x009A3770 / 0x009A30B0: buckets ascending, each lower-ID side first.
	std::vector<std::pair<ObjectID,ObjectID>> pairs{{a->getID(),b->getID()},{a->getID(),c->getID()},{b->getID(),c->getID()}};
	std::sort(pairs.begin(),pairs.end(),[](auto x,auto y){return ((x.first<<16)+x.second)%0x493u<((y.first<<16)+y.second)%0x493u;});
	std::vector<std::pair<ObjectID,ObjectID>> expected;for(auto p:pairs){expected.push_back(p);expected.emplace_back(p.second,p.first);}CHECK(calls==expected);
	calls.clear();f.ai->processCollisions();CHECK(manager->tests()==0);CHECK(calls==expected);
	XferSave save;f.ai->xfer(save);StateHasher before;f.ai->crc(before);
	XferLoad load(save.data());f.ai->xfer(load);StateHasher after;f.ai->crc(after);CHECK(after.value()==before.value());
	XferSave again;f.ai->xfer(again);CHECK(again.data()==save.data());
	auto legacy=save.data();REQUIRE(!legacy.empty());legacy[0]=2;XferLoad legacyLoad(legacy);CHECK_THROWS_WITH(f.ai->xfer(legacyLoad),"BFME1 legacy AI snapshot has no persistent contact state");
	legacy[0]=3;XferLoad chainLoad(legacy);CHECK_THROWS_WITH(f.ai->xfer(chainLoad),"BFME1 legacy AI snapshot has no scalar reservation state");
	StateHasher unchanged;f.ai->crc(unchanged);CHECK(unchanged.value()==before.value());calls.clear();f.ai->processCollisions();CHECK(manager->tests()==0);CHECK(calls==expected);
	Coord3D above{202,200,100};c->setPosition(&above);calls.clear();f.ai->processCollisions();CHECK(manager->contacts()==1);CHECK(calls==std::vector<std::pair<ObjectID,ObjectID>>{{a->getID(),b->getID()},{b->getID(),a->getID()}});
	// A transform invalidates a cached hit even when its bounds stay the same.
	a->setOrientation(0.1f);f.ai->processCollisions();CHECK(manager->tests()==1);
	a->friend_setContainedBy(c);b->friend_setContainedBy(c);calls.clear();f.ai->processCollisions();CHECK(manager->contacts()==0);CHECK(calls.empty());
	a->friend_setContainedBy(nullptr);b->friend_setContainedBy(nullptr);f.ai->processCollisions();CHECK(manager->contacts()==1);
	movetest::MoveWorld later;later.buildMap();later.spawn("Walker",200,200);CHECK_FALSE(later.ai->usesNativeContacts());
}
TEST_CASE("bfme1 ai: cached misses invalidate and first contact removal suppresses the reverse callback")
{
	movetest::MoveWorld f(100,100,"",true);f.buildMap();std::vector<std::pair<ObjectID,ObjectID>> calls;bool remove=false;bindContactObserver(f,calls,remove);
	// Box circles overlap, actual boxes do not: broad phase creates a cached miss.
	Object *a=f.spawn("ContactProbe",200,200),*b=f.spawn("ContactProbe",222,200);REQUIRE(a);REQUIRE(b);
	f.ai->processCollisions();auto *manager=f.ai->nativeContacts();REQUIRE(manager);CHECK(manager->contacts()==1);CHECK(manager->tests()==1);CHECK(calls.empty());
	f.ai->processCollisions();CHECK(manager->tests()==0);CHECK(calls.empty());
	Coord3D touch{219,200,0};b->setPosition(&touch);remove=true;f.ai->processCollisions();CHECK(manager->tests()==1);
	REQUIRE(calls.size()==1);CHECK(calls.front()==std::make_pair(a->getID(),b->getID()));CHECK(b->isOutOfCollisionManager());
	calls.clear();f.ai->processCollisions();CHECK(calls.empty());CHECK(manager->contacts()==0);
}
TEST_CASE("bfme1 ai: native frozen formation owns melee destinations and survives continuation")
{
	movetest::MoveWorld f(100,100,"",true);f.buildMap();
	Object *wrapper=f.spawn("Horde",300,300),*target=f.spawn("Block",450,300,0,f.teamOf("Bob"));REQUIRE(wrapper);REQUIRE(target);
	auto *h=dynamic_cast<Bfme1HordeContain *>(wrapper->getContain());REQUIRE(h);
	const auto members=f.membersOf(wrapper);REQUIRE(members.size()==15);f.frames(1);
	Coord3D initial=*members.front()->getPosition();
	for(size_t i=1;i<members.size();++i){Coord3D far{100,100+(float)i*20,0};members[i]->setPosition(&far);}
	wrapper->setStatus((unsigned)CombatNames::status("STAND_GROUND"),true);
	CHECK(h->beginNativeMelee(*target));CHECK(h->frozenFormation());
	CHECK(h->getMemberFormationPosition(members.front()).x==initial.x);
	h->tickNativeMelee(*target);Coord3D goal=h->getMemberFormationPosition(members.front());
	CHECK(goal.x>initial.x); // tolerance: the native search must advance toward this unobstructed target.CHECK(goal.x-initial.x<=60); // tolerance: native footprint limits the ring search.
	XferSave save;h->xfer(save);StateHasher before;h->crc(before);
	h->endNativeMelee();CHECK_FALSE(h->frozenFormation());
	XferLoad load(save.data());h->xfer(load);StateHasher restored;h->crc(restored);CHECK(restored.value()==before.value());CHECK(h->frozenFormation());CHECK(h->getMemberFormationPosition(members.front()).x==goal.x);
	// A frozen invalid record falls back to the member, never an RW strategy destination.
	h->beginNativeMelee(*target);Coord3D moved=initial;moved.y+=13;members.front()->setPosition(&moved);CHECK(h->getMemberFormationPosition(members.front()).y==moved.y);
	h->endNativeMelee();CHECK_FALSE(h->frozenFormation());
	// A failed search waits 20 frames, but losing a member forces a retry now.
	Object *retryOwner=f.spawn("Horde",650,650);REQUIRE(retryOwner);
	auto *retry=dynamic_cast<Bfme1HordeContain *>(retryOwner->getContain());REQUIRE(retry);
	auto retryMembers=f.membersOf(retryOwner);REQUIRE(retryMembers.size()==15);f.frames(1);
	retryOwner->setStatus((unsigned)CombatNames::status("STAND_GROUND"),true);
	Coord3D blocked=*target->getPosition();retryMembers.front()->setPosition(&blocked);retry->beginNativeMelee(*target);retry->tickNativeMelee(*target);
	CHECK(retry->getMemberFormationPosition(retryMembers.front()).x==blocked.x);
	Coord3D free{320,300,0};retryMembers.front()->setPosition(&free);retry->removeFromContain(retryMembers.back());retry->tickNativeMelee(*target);
	CHECK(retry->getMemberFormationPosition(retryMembers.front()).x<blocked.x); // tolerance: the failed destination is abandoned before its 20-frame delay expires.
	retry->setMeleeEngaged(true);retry->setMeleeEngaged(false);CHECK_FALSE(retry->frozenFormation());
}
TEST_CASE("bfme1 ai: polar formation scales narrow passages without changing height or later editions")
{
	class NarrowTerrain : public pathtest::SyntheticTerrain
	{
	public:bool narrow=false;NarrowTerrain():SyntheticTerrain(100,100){}bool passagePlane(const Coord3D &) const override{return narrow;}
	} terrain;
	movetest::MoveWorld f(100,100,"",true);f.ai->newMap(terrain);
	Object *o=f.spawn("Horde",300,300,0.7f);REQUIRE(o);auto *h=dynamic_cast<Bfme1HordeContain *>(o->getContain());REQUIRE(h);auto members=f.membersOf(o);REQUIRE(!members.empty());
	Coord3D pos=*o->getPosition();pos.z=17;o->setPosition(&pos);auto normal=h->getMemberFormationPosition(members.front());terrain.narrow=true;auto narrow=h->getMemberFormationPosition(members.front());
	// tolerance: subtracting stored world positions introduces float rounding after the native 40 percent radius scale.
	CHECK(narrow.x-pos.x==doctest::Approx((normal.x-pos.x)*0.4f).epsilon(0.00002));CHECK(narrow.y-pos.y==doctest::Approx((normal.y-pos.y)*0.4f).epsilon(0.00002));CHECK(narrow.z==17);
}

TEST_CASE("bfme1 ai: near-slot hub snaps before orienting and ordinary reform exchanges equal-template members")
{
	class HubAccess final : public Bfme1HordeContain
	{
	public:using Bfme1HordeContain::Bfme1HordeContain;using Bfme1HordeContain::moveHub;
	};
	std::string config=combinationObjects();movetest::MoveWorld f(100,100,config.c_str(),true);f.buildMap();
	Object *o=f.spawn("PartA",300,300);REQUIRE(o);auto *h=dynamic_cast<Bfme1HordeContain *>(o->getContain());REQUIRE(h);
	auto members=f.membersOf(o);REQUIRE(members.size()==2);Coord3D p0=h->getMemberFormationPosition(members[0]),p1=h->getMemberFormationPosition(members[1]);
	members[0]->setPosition(&p1);members[1]->setPosition(&p0);h->endReform();CHECK(h->getMemberSlot(members[0])==1);CHECK(h->getMemberSlot(members[1])==0);
	HubAccess hub(o,static_cast<const HordeContainBehaviorData *>(h->getModuleData()));
	Object *m=members.front();Coord3D start{400,400,0};m->setPosition(&start);m->setOrientation(0);
	auto *ai=m->getAIUpdateInterface();REQUIRE(ai);f.ai->pathfinder().removeGoal(ai->adapter());
	Coord3D near{400.25f,400,0};hub.moveHub(*m,near,1.0f);CHECK(m->getPosition()->x==near.x);CHECK(m->getOrientation()==0);CHECK(ai->mover().goalType()==AIGOAL_ANGLE);
}

TEST_CASE("bfme1 ai: contact endpoint ties preserve approach history and teleport rebuilds retain pairs")
{
	movetest::MoveWorld f(100,100,"",true);f.buildMap();std::vector<std::pair<ObjectID,ObjectID>> calls;bool remove=false;bindContactObserver(f,calls,remove);
	Object *a=f.spawn("ContactProbe",200,200),*b=f.spawn("ContactProbe",200,200);REQUIRE(a);REQUIRE(b);
	f.ai->processCollisions();auto *manager=f.ai->nativeContacts();REQUIRE(manager);CHECK(manager->contacts()==1);
	Coord3D p{200,200,20};b->setPosition(&p);f.ai->processCollisions();CHECK(manager->contacts()==1); // existing MIN remains before equal MAX.
	p.z=20.25f;b->setPosition(&p);f.ai->processCollisions();CHECK(manager->contacts()==0);
	p.z=20;b->setPosition(&p);f.ai->processCollisions();CHECK(manager->contacts()==0); // strict crossing: equal endpoints keep their previous order.
	p={800,800,0};a->setPosition(&p);b->setPosition(&p);calls.clear();f.ai->processCollisions();CHECK(manager->contacts()==1);CHECK(calls==std::vector<std::pair<ObjectID,ObjectID>>{{a->getID(),b->getID()},{b->getID(),a->getID()}});
	calls.clear();f.ai->processCollisions();CHECK(manager->tests()==0);CHECK(calls.size()==2);
}

TEST_CASE("bfme1 ai: melee readiness checks ordinary ground paths while preserving portal paths")
{
	movetest::MoveWorld f(100,100,"",true);f.buildMap();Object *o=f.spawn("Horde",300,300),*target=f.spawn("Block",350,300,0,f.teamOf("Bob"));REQUIRE(o);REQUIRE(target);f.frames(1);
	auto *h=dynamic_cast<Bfme1HordeContain *>(o->getContain());REQUIRE(h);Object *member=f.membersOf(o).front();auto *ai=member->getAIUpdateInterface();REQUIRE(ai);
	for(bool portal:{true,false})
	{
		ai->aiBusy(CMD_FROM_AI);REQUIRE(ai->isBusy());
		const Coord3D start=*member->getPosition(),end{start.x+20,start.y,start.z};auto *path=new Path;
		path->appendNode(&start,LAYER_GROUND);path->appendNode(&end,LAYER_GROUND);path->getFirstNode()->setNextOptimized(path->getLastNode());
		if(portal)path->getLastNode()->setWaypointID(7);path->markOptimized();path->updateClosestSegment(start);
		ai->hordeMemberMoveTo(end);ai->mover().setPath(path);ai->mover().startingMove();REQUIRE(ai->isMoving());REQUIRE(path->hasExplicitZ()==portal);
		h->nativeTargetReady(*target);CHECK(ai->isBusy()==portal); // ordinary walking is not the native 0x001B3FE0 portal predicate.
		ai->mover().destroyPath();
	}
}

TEST_CASE("bfme1 ai: readiness acquires an adjacent enemy for a horde member outside the mood scan")
{
	WeaponStores stores;stores.install();movetest::MoveWorld f(100,100,"",true);
	f.w.fx.env.blocks.registerBlock("Weapon",[](INI *ini){WeaponStore::parseWeaponTemplateDefinitionGlobal(ini);});
	REQUIRE(f.w.load("Weapon MemberSword\n AttackRange = 30\n MeleeWeapon = Yes\n DelayBetweenShots = 1000\n DamageNugget\n Damage = 1\n Radius = 0\n DamageType = SLASH\n DeathType = NORMAL\n End\nEnd\n").empty());
	REQUIRE(f.w.load(
		"ChildObject ArmedMember MemberOfHorde\n KindOf = INFANTRY SELECTABLE CAN_ATTACK\n VisionRange = 200\n WeaponSet\n Conditions = None\n Weapon = PRIMARY MemberSword\n End\n Body = ActiveBody Body\n MaxHealth = 100\n End\nEnd\n"
		"Object NativeMeleeHorde\n KindOf = HORDE MELEE_HORDE\n Geometry = CYLINDER\n GeometryMajorRadius = 10\n Behavior = HordeAIUpdate AI\n End\n Behavior = HordeContain Contain\n RankInfo = RankNumber:1 UnitType:ArmedMember Position:X:0 Y:0\n InitialPayload = ArmedMember 1\n End\n LocomotorSet\n Locomotor = HordeLoco\n Condition = SET_NORMAL\n Speed = 50\n End\nEnd\n").empty());
	f.buildMap();Object *horde=f.spawn("NativeMeleeHorde",300,300,0,f.teamOf("Bob")),*enemy=f.spawn("ArmedMember",320,300,0,f.teamOf("Alice"));REQUIRE(horde);REQUIRE(enemy);f.frames(1);
	auto *h=dynamic_cast<Bfme1HordeContain *>(horde->getContain());REQUIRE(h);Object *member=f.membersOf(horde).front();auto *ai=member->getAIUpdateInterface();REQUIRE(ai);
	Coord3D at{300,300,0};member->setPosition(&at);at.x=320;enemy->setPosition(&at);ai->aiBusy(CMD_FROM_AI);
	CHECK(member->testStatus((unsigned)CombatNames::status("HORDE_MEMBER")));
	CHECK(ai->getNextMoodTarget(false,false)==nullptr);
	REQUIRE(member->getWeapons());REQUIRE(member->getWeapons()->currentWeapon());CHECK(member->getWeapons()->canAttackObject(*enemy,CMD_FROM_AI,false));
	auto &ownerMachine=horde->getAIUpdateInterface()->stateMachine();ownerMachine.setTemporaryStateRW(AI_BUSY,-1);CHECK_FALSE(h->nativeTargetReady(*enemy));ownerMachine.clearTemporaryState();
	CHECK(h->nativeTargetReady(*enemy));CHECK(ai->isAttacking());CHECK(ai->currentVictim()==enemy);CHECK_FALSE(ai->isBusy());
}

#include "GameLogic/Object/Contain/Bfme1MeleeRuntime.h"
#include "GameLogic/Module/SiegeModules.h"

namespace {
struct NativeMeleeFixture
{
	WeaponStores stores;
	movetest::MoveWorld f{100,100,"",true};
	NativeMeleeFixture()
	{
		stores.install();
		f.w.fx.env.blocks.registerBlock("Weapon",[](INI *ini){WeaponStore::parseWeaponTemplateDefinitionGlobal(ini);});
		REQUIRE(f.w.load("Weapon NativeSword\n AttackRange = 30\n MeleeWeapon = Yes\n DelayBetweenShots = 1000\n DamageNugget\n Damage = 1\n Radius = 0\n DamageType = SLASH\n DeathType = NORMAL\n End\nEnd\n").empty());
		REQUIRE(f.w.load(
			"ChildObject NativeSoldier MemberOfHorde\n KindOf = INFANTRY SELECTABLE CAN_ATTACK\n CrushableLevel = 0\n VisionRange = 200\n WeaponSet\n Conditions = None\n Weapon = PRIMARY NativeSword\n End\n Body = ActiveBody Body\n MaxHealth = 100\n End\nEnd\n"
			"ChildObject NativeCrusher NativeSoldier\n CrusherLevel = 2\n MinCrushVelocityPercent = 25%\nEnd\n"
			"ChildObject NativeMine NativeSoldier\n KindOf = INFANTRY SELECTABLE CAN_ATTACK MINE\nEnd\n"
			"Object NativeWall\n KindOf = STRUCTURE WALK_ON_TOP_OF_WALL\n Geometry = BOX\n GeometryMajorRadius = 50\n GeometryMinorRadius = 50\n GeometryHeight = 100\nEnd\n"
			"ChildObject NativeWallUpgrade NativeWall\n KindOf = STRUCTURE WALK_ON_TOP_OF_WALL WALL_UPGRADE\nEnd\n"
			"Object NativeTower\n KindOf = STRUCTURE SIEGE_TOWER\n Geometry = BOX\n GeometryMajorRadius = 10\n GeometryMinorRadius = 10\n Behavior = DynamicPortalBehaviour Portal\n TopAttackPos = X:100 Y:0 Z:100\n TopAttackRadius = 20\n End\n Behavior = SiegeDeploySpecialPower Deploy\n End\nEnd\n").empty());
		f.buildMap();
	}
	Object *spawn(const char *type,float x,float y,const char *team="Bob"){auto *o=f.spawn(type,x,y,0,f.teamOf(team));REQUIRE(o);return o;}
};
}

TEST_CASE("bfme1 ai: wall melee view uses shape reach wall rims and the upgrade exception")
{
	NativeMeleeFixture fx;auto &f=fx.f;Object *soldier=fx.spawn("NativeSoldier",440,500),*wall=fx.spawn("NativeWall",500,500,"Alice"),*upgrade=fx.spawn("NativeWallUpgrade",500,700,"Alice");
	CHECK_FALSE(Bfme1Melee::view(*soldier,*soldier->getPosition(),*wall));
	auto *rim=f.ai->pathfinder().getCell(LAYER_GROUND,42,49);REQUIRE(rim);rim->setLayer(LAYER_WALL_FIRST);rim->setType(PathfindCell::CELL_BRIDGE_IMPASSABLE);
	CHECK(Bfme1Melee::view(*soldier,*soldier->getPosition(),*wall));
	Coord3D at{440,700,0};soldier->setPosition(&at);CHECK(Bfme1Melee::view(*soldier,at,*upgrade));
	at.x=422;soldier->setPosition(&at);CHECK_FALSE(Bfme1Melee::view(*soldier,at,*upgrade)); // gap 20 equals the native strict threshold.
}

TEST_CASE("bfme1 ai: moving-away guard resolves horde members and uses strict quarter speed")
{
	NativeMeleeFixture fx;Object *a=fx.spawn("NativeSoldier",300,300),*b=fx.spawn("NativeSoldier",320,300,"Alice");auto *ai=b->getAIUpdateInterface();REQUIRE(ai);auto *l=ai->curLocomotor();REQUIRE(l);
	const float quarter=SimMath::pc24Mul(l->getMaxSpeedForCondition(ai->locomotorHost()),0.25f);
	l->setSpeed(quarter);CHECK_FALSE(Bfme1Melee::targetMovingAway(*a,b));l->setSpeed(SimMath::pc24Add(quarter,1));CHECK(Bfme1Melee::targetMovingAway(*a,b));
	b->setOrientation(3.14159274f);CHECK_FALSE(Bfme1Melee::targetMovingAway(*a,b));
}

TEST_CASE("bfme1 ai: melee recipients enforce busy stealth crush and reciprocal mine guards")
{
	NativeMeleeFixture fx;Object *a=fx.spawn("NativeSoldier",300,300),*b=fx.spawn("NativeSoldier",320,300,"Alice");auto *ai=a->getAIUpdateInterface(),*bi=b->getAIUpdateInterface();REQUIRE(ai);REQUIRE(bi);
	ai->aiBusy(CMD_FROM_AI);CHECK_FALSE(Bfme1Melee::receive(*a,*b));ai->aiIdle(CMD_FROM_AI);
	a->setStatus((unsigned)CombatNames::status("UNCONTROLLABLY_SCARED"),true);CHECK_FALSE(Bfme1Melee::receive(*a,*b));a->setStatus((unsigned)CombatNames::status("UNCONTROLLABLY_SCARED"),false);
	a->setStatus((unsigned)CombatNames::status("STEALTHED"),true);CHECK_FALSE(Bfme1Melee::receive(*a,*b));a->setStatus((unsigned)CombatNames::status("STEALTHED"),false);
	CHECK(Bfme1Melee::receive(*a,*b));CHECK(ai->currentVictim()==b);CHECK(bi->currentVictim()==a);CHECK_FALSE(ai->nativeMeleeState().responding);CHECK_FALSE(bi->nativeMeleeState().responding);
	Object *crusher=fx.spawn("NativeCrusher",500,300),*victim=fx.spawn("NativeSoldier",520,300,"Alice");auto *ci=crusher->getAIUpdateInterface();REQUIRE(ci);auto *l=ci->curLocomotor();REQUIRE(l);
	l->setSpeed(l->getMaxSpeedForCondition(ci->locomotorHost()));CHECK_FALSE(Bfme1Melee::receive(*crusher,*victim));l->setSpeed(0);CHECK(Bfme1Melee::receive(*crusher,*victim));
	Object *fresh=fx.spawn("NativeSoldier",700,300),*mine=fx.spawn("NativeMine",720,300,"Alice");CHECK(Bfme1Melee::receive(*fresh,*mine));CHECK(mine->getAIUpdateInterface()->currentVictim()==nullptr);
}

TEST_CASE("bfme1 ai: recipients keep attack goals locks and temporary-machine ownership and save the acquisition byte")
{
	NativeMeleeFixture fx;Object *a=fx.spawn("NativeSoldier",300,300),*b=fx.spawn("NativeSoldier",600,300,"Alice"),*c=fx.spawn("NativeSoldier",300,320,"Alice");auto *ai=a->getAIUpdateInterface();REQUIRE(ai);ai->aiAttackObject(b,CMD_FROM_AI);REQUIRE(ai->isAttacking());
	CHECK_FALSE(Bfme1Melee::receive(*a,*c));Bfme1Melee::autoAcquired(*ai);CHECK(ai->nativeMeleeState().autoTarget);
	XferSave save;ai->xferAIState(save);StateHasher before;ai->crc(before);ai->nativeMeleeState().autoTarget=false;XferLoad load(save.data());ai->xferAIState(load);StateHasher after;ai->crc(after);CHECK(after.value()==before.value());CHECK(ai->nativeMeleeState().autoTarget);
	ai->stateMachine().lock();CHECK(Bfme1Melee::receive(*a,*c));CHECK(ai->stateMachine().isLocked());CHECK(ai->stateMachine().goalObject()==c);ai->stateMachine().unlock();
	ai->aiIdle(CMD_FROM_AI);CHECK_FALSE(ai->nativeMeleeState().autoTarget);ai->beginTemporaryStateMachine();CHECK(Bfme1Melee::receive(*a,*c));CHECK(ai->isIdle());CHECK(ai->hasTemporaryStateMachine());ai->endTemporaryStateMachine();
}

TEST_CASE("bfme1 ai: attack-move acquisition marks an automatic target and an accepted order clears it")
{
	NativeMeleeFixture fx;Object *a=fx.spawn("NativeSoldier",300,300),*b=fx.spawn("NativeSoldier",320,300,"Alice");auto *ai=a->getAIUpdateInterface();REQUIRE(ai);
	Coord3D goal{600,300,0};REQUIRE(ai->armAttackMove(goal,CMD_FROM_AI));ai->aiMoveToPosition(goal,CMD_FROM_AI);ai->setNextMoodCheckTime(0);
	ai->update();CHECK(ai->currentVictim()==b);CHECK(ai->nativeMeleeState().autoTarget);
	ai->aiBusy(CMD_FROM_AI);CHECK_FALSE(ai->nativeMeleeState().autoTarget);
}

TEST_CASE("bfme1 ai: scalar reservation replacement cannot resurrect an earlier owner and later editions retain lists")
{
	for(bool original:{false,true})
	{
		movetest::MoveWorld f(100,100,"",original);f.buildMap();auto *a=f.spawn("Walker",300,300),*b=f.spawn("Walker",300,300);REQUIRE(a);REQUIRE(b);auto &pf=f.ai->pathfinder();auto *aa=a->getAIUpdateInterface(),*bb=b->getAIUpdateInterface();REQUIRE(aa);REQUIRE(bb);
		pf.updatePos(aa->adapter());pf.updatePos(bb->adapter());auto *cell=pf.getCell(LAYER_GROUND,30,30);REQUIRE(cell);REQUIRE(cell->occupants(OCC_POSITION));CHECK(cell->occupants(OCC_POSITION)->owner==(original?a->getID():b->getID()));
		pf.removePos(bb->adapter());CHECK(cell->hasOccupants(OCC_POSITION));pf.removePos(aa->adapter());CHECK_FALSE(cell->hasOccupants(OCC_POSITION));
		pf.updateGoal(aa->adapter(),a->getPosition(),LAYER_GROUND);pf.updateGoal(bb->adapter(),b->getPosition(),LAYER_GROUND);pf.removeGoal(bb->adapter());CHECK(cell->hasOccupants(OCC_GROUND_GOAL)==!original);
		Coord3D at{600,600,0};a->setPosition(&at);pf.updatePos(aa->adapter());pf.updateGoal(aa->adapter(),&at,LAYER_GROUND);pf.updateGoal(bb->adapter(),&at,LAYER_GROUND);
		auto *occupied=pf.getCell(LAYER_GROUND,60,60);REQUIRE(occupied);REQUIRE(occupied->occupants(OCC_GROUND_GOAL));CHECK(occupied->occupants(OCC_GROUND_GOAL)->owner==(original?a->getID():b->getID()));

	}
}

TEST_CASE("bfme1 ai: siege melee top view requires deployment and uses strict XY and height bounds")
{
	NativeMeleeFixture fx;Object *soldier=fx.spawn("NativeSoldier",700,600),*tower=fx.spawn("NativeTower",600,600,"Alice");auto *deploy=SiegeDeploySpecialPower::of(*tower);REQUIRE(deploy);
	Coord3D from{700,600,100};CHECK_FALSE(Bfme1Melee::view(*soldier,from,*tower));
	// A deployed continuation is a valid input to this geometry query. The
	// existing deployment suite covers the transitions; retain this module's
	// normal base serialization and change only its phase in the 38-byte tail.
	XferSave saved;deploy->xfer(saved);auto bytes=saved.data();REQUIRE(bytes.size()>=38); // tolerance: the base module has a variable-sized prefix; require room for the fixed 38-byte deployment tail.
	const size_t phase=bytes.size()-38;CHECK(bytes[phase]==0);bytes[phase]=3;
	XferLoad load(bytes);deploy->xfer(load);REQUIRE(deploy->isDeployed());
	CHECK(Bfme1Melee::view(*soldier,from,*tower));
	from.z=120;CHECK_FALSE(Bfme1Melee::view(*soldier,from,*tower));from.z=80;CHECK_FALSE(Bfme1Melee::view(*soldier,from,*tower));from.z=100;
	from.x=728;CHECK_FALSE(Bfme1Melee::view(*soldier,from,*tower));from.x=727;CHECK(Bfme1Melee::view(*soldier,from,*tower));
	from.x=700;from.y=628;CHECK_FALSE(Bfme1Melee::view(*soldier,from,*tower));from.y=627;CHECK(Bfme1Melee::view(*soldier,from,*tower));
}

TEST_CASE("bfme1 ai: native horde goal registration requires at least two contained members")
{
	movetest::MoveWorld f(100,100,"",true);f.buildMap();auto *o=f.spawn("Horde",300,300);REQUIRE(o);auto *ai=o->getAIUpdateInterface();REQUIRE(ai);auto *h=o->getContain();REQUIRE(h);
	f.ai->pathfinder().updatePos(ai->adapter());
	auto *current=f.ai->pathfinder().getCell(LAYER_GROUND,30,30);REQUIRE(current);REQUIRE(current->occupants(OCC_POSITION));CHECK(current->occupants(OCC_POSITION)->owner==o->getID());CHECK_FALSE(current->hasOccupants(OCC_HORDE_POSITION));
	Coord3D goal{600,600,0};f.ai->pathfinder().updateGoal(ai->adapter(),&goal,LAYER_GROUND);
	CHECK(Bfme1Melee::footprintGoals(*f.spawn("Walker",600,600),goal)==std::vector<ObjectID>{o->getID()});
	while(h->getContainCount()>1){Object *member=h->getContainedItemsList()->back();h->removeFromContain(member);f.logic->destroyObject(member);}
	goal={700,700,0};f.ai->pathfinder().updateGoal(ai->adapter(),&goal,LAYER_GROUND);
	const auto cell=f.ai->pathfinder().pathfindGoalCell(o->getID());CHECK(cell.x==60);CHECK(cell.y==60); // native early return keeps its previous goal.
}

TEST_CASE("bfme1 ai: scalar position displacement requires goal ownership and a stationary owner is protected")
{
	NativeMeleeFixture fx;auto &pf=fx.f.ai->pathfinder();Object *a=fx.spawn("NativeSoldier",400,400),*b=fx.spawn("NativeSoldier",400,400),*c=fx.spawn("NativeSoldier",400,400);
	auto *aa=a->getAIUpdateInterface(),*bb=b->getAIUpdateInterface(),*cc=c->getAIUpdateInterface();
	pf.updatePos(aa->adapter());pf.updatePos(bb->adapter());pf.updatePos(cc->adapter());auto *cell=pf.getCell(LAYER_GROUND,40,40);REQUIRE(cell);REQUIRE(cell->occupants(OCC_POSITION));CHECK(cell->occupants(OCC_POSITION)->owner==a->getID());
	pf.updateGoal(bb->adapter(),b->getPosition(),LAYER_GROUND);CHECK(cell->occupants(OCC_POSITION)->owner==b->getID());CHECK(cell->occupants(OCC_GROUND_GOAL)->owner==b->getID());
	pf.updateGoal(cc->adapter(),c->getPosition(),LAYER_GROUND);CHECK(cell->occupants(OCC_POSITION)->owner==b->getID());CHECK(cell->occupants(OCC_GROUND_GOAL)->owner==b->getID());
	pf.removeGoal(bb->adapter());pf.removeGoal(cc->adapter());pf.updateGoal(cc->adapter(),c->getPosition(),LAYER_GROUND);CHECK(cell->occupants(OCC_POSITION)->owner==c->getID());CHECK(cell->occupants(OCC_GROUND_GOAL)->owner==c->getID());
}

TEST_CASE("bfme1 ai: raised footprint goal query retains the native position-owner return")
{
	NativeMeleeFixture fx;auto &pf=fx.f.ai->pathfinder();
	Object *query=fx.spawn("NativeSoldier",300,300),*goal=fx.spawn("NativeSoldier",500,500),*position=fx.spawn("NativeSoldier",700,700);
	Coord3D at{600,600,0};pf.updateGoal(goal->getAIUpdateInterface()->adapter(),&at,LAYER_GROUND);
	position->setPosition(&at);pf.updatePos(position->getAIUpdateInterface()->adapter());
	CHECK(Bfme1Melee::footprintGoals(*query,at)==std::vector<ObjectID>{goal->getID()});
	// Layer 2 has no deck here: native's raised branch falls back to ground
	// but still appends position instead of goal. The query's footprint is
	// several cells wide; goal-based dedup therefore repeats that position.
	query->getAIUpdateInterface()->setLayer(static_cast<PathfindLayerEnum>(2));
	auto ids=Bfme1Melee::footprintGoals(*query,at);REQUIRE_FALSE(ids.empty());
	for(auto id:ids)CHECK(id==position->getID());
	pf.removePos(position->getAIUpdateInterface()->adapter());
	ids=Bfme1Melee::footprintGoals(*query,at);REQUIRE_FALSE(ids.empty());for(auto id:ids)CHECK(id==0);
}

#include "GameLogic/AI/AIClosestEnemy.h"
#include "GameLogic/ScriptEngine/AttackPriority.h"
#include "GameLogic/AI/AIGroup.h"
TEST_CASE("bfme1 ai: collision ignore preserves the native asymmetric recipient guards and object continuation")
{
 movetest::MoveWorld f(100,100,"",true);f.buildMap();std::vector<std::pair<ObjectID,ObjectID>> calls;bool remove=false;bindContactObserver(f,calls,remove);
 Object *a=f.spawn("ContactProbe",200,200),*b=f.spawn("ContactProbe",201,200);REQUIRE(a);REQUIRE(b);
 a->setBfme1CollisionIgnoreID(b->getID());f.ai->processCollisions();CHECK(calls.empty());
 a->setBfme1CollisionIgnoreID(9999);calls.clear();f.ai->processCollisions();
 CHECK(calls==std::vector<std::pair<ObjectID,ObjectID>>{{a->getID(),b->getID()}}); // reverse refuses any nonzero ignore on its other side.
 Object *savedObject=f.spawn("Walker",600,600);REQUIRE(savedObject);savedObject->setBfme1CollisionIgnoreID(9999);
 savedObject->setBfme1FiringTarget(b->getID());XferSave saved;savedObject->xfer(saved);StateHasher before;savedObject->crc(before);
 savedObject->setBfme1CollisionIgnoreID(0);CHECK(savedObject->takeBfme1FiringTarget()==b->getID());CHECK(savedObject->takeBfme1FiringTarget()==0);
 XferLoad loaded(saved.data());savedObject->xfer(loaded);StateHasher after;savedObject->crc(after);CHECK(before.value()==after.value());CHECK(savedObject->bfme1CollisionIgnoreID()==9999);CHECK(savedObject->takeBfme1FiringTarget()==b->getID());
 a->setBfme1CollisionIgnoreID(0);calls.clear();f.ai->processCollisions();CHECK(calls.size()==2);
}

TEST_CASE("bfme1 ai: quarrel truncates native integer parameters and saves fighters spectators and backup records")
{
 movetest::MoveWorld f(100,100,"",true);f.buildMap();Object *o=f.spawn("Horde",500,500),*s=f.spawn("Walker",100,500);REQUIRE(o);REQUIRE(s);
 auto *h=dynamic_cast<Bfme1HordeContain*>(o->getContain());REQUIRE(h);auto members=f.membersOf(o);REQUIRE(members.size()==15);
 HordeContainInterface::ConditionFlags spectators{},fighters{};const unsigned fighting=CombatNames::modelCondition("QUARRELSOME_FIGHTING");fighters[fighting>>5]|=1u<<(fighting&31);
 f.logic->random().enableCallLog(true);h->beginQuarrel(50.9f,50.9f,spectators,fighters);
 CHECK(h->quarrelDistances().size()==13);CHECK(h->quarrelFighter(0)!=h->quarrelFighter(1));
 for(const auto &entry:h->quarrelDistances())CHECK(entry.second==50);
 unsigned picks=0,spread=0;for(const auto &call:f.logic->random().callLog()){picks+=call.line==0x1B34;spread+=call.line==0x1B4F;}CHECK(picks==1);CHECK(spread==13);
 Object *a=f.logic->findObjectByID(h->quarrelFighter(0)),*b=f.logic->findObjectByID(h->quarrelFighter(1));REQUIRE(a);REQUIRE(b);
 Coord3D bp{500,500,0},ap{510,500,0},goal{};b->setPosition(&bp);a->setPosition(&ap);float angle=0;
 CHECK(h->emotionMemberOrder(*a,nullptr,goal,angle)==0);CHECK(goal.x==520);CHECK(goal.y==500);CHECK(a->testModelCondition((int)fighting));
 XferSave save;h->xfer(save);StateHasher before;h->crc(before);h->endQuarrel();CHECK(h->quarrelDistances().empty());XferLoad load(save.data());h->xfer(load);StateHasher after;h->crc(after);CHECK(before.value()==after.value());CHECK(h->quarrelDistances().size()==13);
 a->setModelConditionState(AIUpdateInterface::modelConditionBit("SELECTED"),true);CHECK(h->emotionMemberOrder(*a,nullptr,goal,angle)==2);CHECK(h->quarrelDistances().empty());
 o->setStatus((unsigned)CombatNames::status("UNCONTROLLABLY_SCARED"),true);h->recordBackUp(s);CHECK(h->backUpRecords().size()==15);
 XferSave backup;h->xfer(backup);StateHasher backupHash;h->crc(backupHash);h->recordBackUp(a);XferLoad reload(backup.data());h->xfer(reload);StateHasher restored;h->crc(restored);CHECK(backupHash.value()==restored.value());
 h->setMembersBusy();for(auto *m:members){REQUIRE(m->getAIUpdateInterface());CHECK(m->getAIUpdateInterface()->isBusy());CHECK(m->getAIUpdateInterface()->lastCommandSource()==CMD_FROM_SCRIPT);}
}

TEST_CASE("bfme1 ai: default target priorities use closest search while named priorities use raw integer scores")
{
 NativeMeleeFixture fx;Object *a=fx.spawn("NativeSoldier",300,300),*near=fx.spawn("NativeSoldier",330,300,"Alice"),*far=fx.spawn("NativeMine",370,300,"Alice");
 fx.f.logic->partition().setRegion(0,0,1000,1000);fx.f.logic->partition().update();
 auto &priorities=fx.f.logic->scriptEngine().attackPriorities();auto *named=priorities.findOrCreate("NativeSelection");REQUIRE(named);named->setPriority(far->getTemplate(),1000);
 struct Refuse final:PartitionFilter{bool allow(Object &)override{return false;}}refuse;
 CHECK(AI::findClosestEnemy(*a,200,0,priorities.getDefaultAttackInfo(),&refuse,0)==near); // default info ignores the scored-only optional filter.
 CHECK(AI::findClosestEnemy(*a,200,0,named,nullptr,0)==far);CHECK(AI::findClosestEnemy(*a,200,0,named,&refuse,0)==nullptr);
 a->setStatus((unsigned)CombatNames::status("NO_AUTO_ACQUIRE"),true);
 CHECK(AI::findClosestEnemy(*a,200,AI::AUTO_ACQUIRE,nullptr,nullptr,0)==near); // worker has no NO_AUTO guard; the mood caller does.
 REQUIRE(a->getAIUpdateInterface());CHECK(a->getAIUpdateInterface()->getNextMoodTarget(false,true)==nullptr);
}

TEST_CASE("bfme1 ai: player stop state persists and constrains melee reach before a mood scan consumes its firing target")
{
 NativeMeleeFixture fx;Object *a=fx.spawn("NativeSoldier",300,300),*b=fx.spawn("NativeSoldier",390,300,"Alice");auto *ai=a->getAIUpdateInterface();REQUIRE(ai);
 CHECK(AI::canAttackFromHere(*a,*b)); // native MeleeAcquireLimitDist defaults to 100.
 AIGroup group(*fx.f.logic,{a->getID()});group.groupIdle(CMD_FROM_PLAYER);CHECK(ai->nativeMeleeState().stopRetaliation);CHECK_FALSE(AI::canAttackFromHere(*a,*b));
 XferSave saved;ai->xferAIState(saved);StateHasher before;ai->crc(before);ai->nativeMeleeState().stopRetaliation=false;XferLoad loaded(saved.data());ai->xferAIState(loaded);StateHasher after;ai->crc(after);CHECK(before.value()==after.value());CHECK(ai->nativeMeleeState().stopRetaliation);
 ai->nativeMeleeState().stopRetaliation=false;a->setBfme1FiringTarget(b->getID());
 CHECK(ai->getNextMoodTarget(false,false)==b);CHECK(a->takeBfme1FiringTarget()==0);
 Coord3D outside{417,300,0};b->setPosition(&outside);CHECK_FALSE(AI::canAttackFromHere(*a,*b));
}

#include "GameLogic/Map/TerrainLogic.h"
TEST_CASE("bfme1 ai: bridge end clips inset edges with inclusive ten-unit height and uses the actual unit position")
{
 Bfme1BridgeGeometry b;b.layer=2;b.fromLeft={300,300,20};b.fromRight={300,400,20};b.toLeft={600,300,20};b.toRight={600,400,20};
 CHECK(b.endMeetsBounds(299,309,301,311));CHECK_FALSE(b.endMeetsBounds(299,298,301,309));CHECK(b.endMeetsBounds(599,350,601,351));CHECK_FALSE(b.endMeetsBounds(450,350,460,360));
 CHECK(b.interactsWithEnd({300,350,30},8));CHECK(b.interactsWithEnd({300,350,10},8));CHECK_FALSE(b.interactsWithEnd({300,350,30.25f},8));CHECK_FALSE(b.interactsWithEnd({450,350,20},8));
 b.layer=1;CHECK_FALSE(b.interactsWithEnd({300,350,20},8));b.layer=2;
 NativeMeleeFixture fx;TerrainLogic terrain;terrain.addBfme1Bridge(b);fx.f.logic->setTerrain(&terrain);
 Object *query=fx.spawn("NativeSoldier",300,350),*goal=fx.spawn("NativeSoldier",700,700);Coord3D p{300,350,20};query->setPosition(&p);query->getAIUpdateInterface()->setLayer((PathfindLayerEnum)2);
 Coord3D hypothetical{500,500,0};fx.f.ai->pathfinder().updateGoal(goal->getAIUpdateInterface()->adapter(),&hypothetical,LAYER_GROUND);
 CHECK(terrain.bfme1InteractsWithBridgeEnd(2,*query->getPosition(),8));CHECK_FALSE(terrain.bfme1InteractsWithBridgeEnd(3,*query->getPosition(),8));
 // No deck cells exist in this fixture: the raised lookup already falls
 // back to ground. The bridge-end extra scan must preserve goal dedup.
 auto owners=Bfme1Melee::footprintGoals(*query,hypothetical);CHECK_FALSE(owners.empty());
 fx.f.logic->setTerrain(nullptr);
}

TEST_CASE("bfme1 ai: weapon fire records a horde root once and ordinary targets preserve the pending reference")
{
 NativeMeleeFixture fx;Object *shooter=fx.spawn("NativeSoldier",300,300),*ordinary=fx.spawn("NativeSoldier",320,300,"Alice");
 Object *root=fx.f.spawn("Horde",350,300,0,fx.f.teamOf("Alice"));REQUIRE(root);auto members=fx.f.membersOf(root);REQUIRE_FALSE(members.empty());
 auto *weapons=shooter->getWeapons();REQUIRE(weapons);REQUIRE(weapons->currentWeapon());
 weapons->fireCurrentWeapon(members.front(),nullptr);CHECK(shooter->takeBfme1FiringTarget()==root->getID());CHECK(shooter->takeBfme1FiringTarget()==0);
 shooter->setBfme1FiringTarget(root->getID());weapons->fireCurrentWeapon(ordinary,nullptr);CHECK(shooter->takeBfme1FiringTarget()==root->getID());
}

#include "GameClient/Bfme1MapBridges.h"
#include "GameLogic/Module/SpecialAbilityModules.h"
#include "GameLogic/Map/TerrainPathfindSource.h"
#include "GameLogic/MapObjectLoop.h"

TEST_CASE("bfme1 ai: plain Bridge cells connect ground at entries and retain their sloped geometry in snapshots")
{
 struct Terrain:pathtest::SyntheticTerrain {
  std::vector<Bfme1BridgeGeometry> bridges;
  Terrain():SyntheticTerrain(100,100){}
  const std::vector<Bfme1BridgeGeometry> *bfme1Bridges() const override{return &bridges;}
 } terrain;
 Bfme1BridgeGeometry b;b.layer=2;b.from={200,350,20};b.to={600,350,20};
 b.fromLeft={200,300,20};b.fromRight={200,400,20};b.toLeft={600,300,20};b.toRight={600,400,20};terrain.bridges.push_back(b);
 Pathfinder pf(pathtest::testConfig(),nullptr);pf.newMap(terrain);
 REQUIRE(pf.raisedLayerCount()==1);
 const auto layer=(PathfindLayerEnum)2;
 REQUIRE(pf.raisedCellAt(layer,35,35));CHECK(pf.raisedCellAt(layer,35,35)->getType()==PathfindCell::CELL_CLEAR);
 REQUIRE(pf.raisedCellAt(layer,19,35));CHECK(pf.raisedCellAt(layer,19,35)->getConnectLayer()==LAYER_GROUND);
 CHECK(pf.getCell(LAYER_GROUND,19,35)->getConnectLayer()==layer);
 CHECK(pf.raisedCellAt(layer,35,29)->getType()==PathfindCell::CELL_IMPASSABLE);
 CHECK(pf.raisedConnectCell(layer,0).x==19);CHECK(pf.raisedConnectCell(layer,1).x==60);
 CHECK(pf.getLayerAt(350,350,10)==LAYER_GROUND);CHECK(pf.getLayerAt(350,350,11)==layer);
 CHECK(pf.getLayerHeight(350,350,layer)==20);CHECK(pf.getHighestLayerHeight(350,350)==20);
 StateHasher before;pf.crc(before);XferSave saved;pf.xfer(saved);
 Pathfinder loaded(pathtest::testConfig(),nullptr);loaded.newMap(terrain);XferLoad input(saved.data());loaded.xfer(input);StateHasher after;loaded.crc(after);
 CHECK(before.value()==after.value());CHECK(loaded.getLayerAt(350,350,11)==layer);CHECK(loaded.getLayerHeight(350,350,layer)==20);
 // Native Bridge priority precedes the inherited ramp fallback at an overlap.
 struct RampObject:pathtest::TestObject{PathfindWallGeometry wall;const PathfindWallGeometry *getWallGeometry()const override{return &wall;}} ramp;
 ramp.id=42;ramp.structure=true;ramp.pos={350,350,0};ramp.geometry.type=PATHFIND_GEOMETRY_BOX;ramp.geometry.majorRadius=ramp.geometry.minorRadius=1;ramp.kinds.insert(PK_WALK_ON_TOP_OF_WALL);
 PathfindWallSurface surface;surface.loX=300;surface.hiX=400;surface.loY=320;surface.hiY=380;surface.height=40;
 PathfindWallTriangle a,c;a.v[0]={300,320,0};a.v[1]={400,320,40};a.v[2]={400,380,40};c.v[0]={300,320,0};c.v[1]={400,380,40};c.v[2]={300,380,0};surface.triangles={a,c};ramp.wall.ramps.push_back(surface);
 pf.addObjectToPathfindMap(ramp);REQUIRE(pf.isPointOnRamp(350,350));CHECK(pf.getLayerAt(350,350,20)==layer);
 pathtest::TestObject query;query.pos={350,350,20};CHECK(pf.getLayerAt(query,query.pos)==layer);
 CHECK_FALSE(b.contains(300,450));CHECK(b.contains(200,300));
 terrain.water.push_back({300,0,500,1000,30});
 movetest::MoveWorld units(100,100,"",true);units.ai->newMap(terrain);auto *walker=units.spawn("Walker",150,350);REQUIRE(walker);const Coord3D target{650,350,0};
 std::unique_ptr<Path> crossing(units.ai->pathfinder().findPath(&walker->getAIUpdateInterface()->adapter(),pathtest::groundLoco(),walker->getPosition(),&target));
 REQUIRE(crossing);bool crossed=false;for(auto *node=crossing->getFirstNode();node;node=node->getNext())crossed=crossed || node->getLayer()==layer;CHECK(crossed);
 b.to.z=b.toLeft.z=b.toRight.z=60;CHECK(b.height(400,350)==40);float height=b.height(200,350);std::uint32_t heightBits=0;std::memcpy(&heightBits,&height,sizeof(height));// pin: native plane/intersection PC24 operations store 20 plus one float ulp here.
 CHECK(heightBits==0x41A00001u);CHECK(b.height(600,350)==60);
}

TEST_CASE("bfme1 ai: general quick reachability preserves BFME1 immobile and surface-mask rules")
{
 for(bool bfme1:{false,true}){
  movetest::MoveWorld f(100,100,"",bfme1);f.buildMap();auto *o=f.spawn("Walker",200,200);REQUIRE(o);auto *ai=o->getAIUpdateInterface();REQUIRE(ai);
  const Coord3D to{250,200,0};o->setStatus((unsigned)CombatNames::status("IMMOBILE"),true);
  CHECK(f.ai->pathfinder().quickDoesPathExist(ai->adapter(),o->getPosition(),&to,nullptr)==bfme1);
  o->setStatus((unsigned)CombatNames::status("IMMOBILE"),false);
  PathfindLocomotorInfo loco;loco.validSurfaces=LOCOMOTORSURFACE_DEEP_WATER;
  if(bfme1)CHECK_FALSE(f.ai->pathfinder().quickDoesPathExist(ai->adapter(),o->getPosition(),&to,&loco));
 }
}

TEST_CASE("bfme1 ai: general cell passability keeps BFME1 crusher and deep-water differences")
{
 struct World:pathtest::TestWorld{bool bfme1=false;bool usesScalarReservations() const override{return bfme1;}} world;
 pathtest::SyntheticTerrain terrain(20,20);
 for(bool bfme1:{false,true}){
  world.bfme1=bfme1;Pathfinder pf(pathtest::testConfig(),&world);pf.newMap(terrain);
  auto *cell=pf.getCell(LAYER_GROUND,10,10);REQUIRE(cell);REQUIRE(cell->allocateInfo(pf.pool(),{10,10}));cell->info()->m_obstacleID=999;
  cell->setType(PathfindCell::CELL_OBSTACLE);PathfindMovement movement;movement.surf=LOCOMOTORSURFACE_OBSTACLE;movement.bC=true;movement.i8=-1;
  CHECK(pf.validMovementPosition(movement,cell)==bfme1);
  cell->info()->m_obstacleID=0;cell->setType(PathfindCell::CELL_DEEP_WATER);movement.bC=false;movement.surf=LOCOMOTORSURFACE_DEEP_WATER;
  CHECK(pf.validMovementPosition(movement,cell)==!bfme1);
 }
}

TEST_CASE("bfme1 ai: special mood chasing is explicit saved state and survives an AI snapshot")
{
 NativeMeleeFixture fx;auto *o=fx.spawn("NativeSoldier",300,300);auto *ai=o->getAIUpdateInterface();REQUIRE(ai);
 ai->nativeMeleeState().allowedToChase=true;XferSave saved;ai->xferAIState(saved);StateHasher before;ai->crc(before);
 ai->nativeMeleeState().allowedToChase=false;XferLoad input(saved.data());ai->xferAIState(input);StateHasher after;ai->crc(after);
 CHECK(ai->nativeMeleeState().allowedToChase);CHECK(before.value()==after.value());
}

TEST_CASE("bfme1 ai: charging weapons select the native machine and fire at the charge origin")
{
 NativeMeleeFixture fx;auto &f=fx.f;
 REQUIRE(f.w.load("Weapon NativeCharge\n AttackRange = 60\n CanFireWhileCharging = Yes\n DelayBetweenShots = 1000\n DamageNugget\n Damage = 1\n Radius = 10\n DamageType = CRUSH\n DeathType = NORMAL\n End\nEnd\nChildObject NativeCharger NativeSoldier\n WeaponSet\n Conditions = None\n Weapon = PRIMARY NativeCharge\n End\nEnd\n").empty());
 Coord3D victimPosition{350,300,0};Coord3D through{400,300,0};bool triggers=true;
 SUBCASE("straight charge"){}
 SUBCASE("native signed negative angle passes"){victimPosition={300,250,0};through={300,200,0};}
 SUBCASE("positive angle outside aim delta waits"){victimPosition={300,350,0};triggers=false;}
 SUBCASE("exact unmodified range waits"){victimPosition={360,300,0};triggers=false;}
 auto *o=fx.spawn("NativeCharger",300,300),*victim=fx.spawn("NativeSoldier",victimPosition.x,victimPosition.y,"Alice");auto *ai=o->getAIUpdateInterface();REQUIRE(ai);
 o->getWeapons()->loadExtraWeapon(*o->getWeapons()->currentWeapon());CHECK(ai->aiAttackObject(victim,CMD_FROM_PLAYER));
 auto *attack=dynamic_cast<AIAttackState*>(ai->stateMachine().currentState());REQUIRE(attack);REQUIRE(attack->attackMachine());
 CHECK(attack->attackMachine()->name()=="AttackFireWhileChargingMachine");
 attack->attackMachine()->updateStateMachine();CHECK(o->testModelCondition(CombatNames::modelCondition("CHARGING"))==triggers);
 if(!triggers){CHECK(attack->attackMachine()->goalObjectID()==victim->getID());ai->aiIdle(CMD_FROM_PLAYER);return;}
 CHECK(attack->attackMachine()->goalObjectID()==INVALID_ID);CHECK(ai->mover().requestedDestination().x==through.x);CHECK(ai->mover().requestedDestination().y==through.y);
 XferSave save;ai->xferAIState(save);StateHasher before;ai->crc(before);XferLoad load(save.data());ai->xferAIState(load);StateHasher after;ai->crc(after);CHECK(before.value()==after.value());
 ai->aiIdle(CMD_FROM_PLAYER);CHECK_FALSE(o->testModelCondition(CombatNames::modelCondition("CHARGING")));
}

TEST_CASE("bfme1 ai: charge special power sets charging condition and location-only intent faces before aborting")
{
 SpecialPowerStore powers;struct Scope{SpecialPowerStore *saved=TheSpecialPowerStore;~Scope(){TheSpecialPowerStore=saved;}} scope;TheSpecialPowerStore=&powers;
 NativeMeleeFixture fx;auto &f=fx.f;f.logic->combat().setAutoAcquireEnabled(false);f.w.fx.env.blocks.registerBlock("SpecialPower",[](INI *ini){SpecialPowerStore::parseSpecialPowerDefinitionGlobal(ini);});
 REQUIRE(f.w.load("SpecialPower NativeChargePower\n Enum = SPECIAL_CHARGE_ATTACK\n ReloadTime = 0\nEnd\nChildObject NativeChargeCaster NativeSoldier\n Behavior = SpecialPowerModule Power\n SpecialPowerTemplate = NativeChargePower\n End\n Behavior = SpecialAbilityUpdate ChargeAbility\n SpecialPowerTemplate = NativeChargePower\n StartAbilityRange = 10\n ChargeAttackSpeedBoost = Yes\n End\nEnd\n").empty());
 auto *o=fx.spawn("NativeChargeCaster",300,300),*victim=fx.spawn("NativeSoldier",350,300,"Alice");auto *ability=dynamic_cast<SpecialAbilityUpdate*>(o->findModule("SpecialAbilityUpdate"));REQUIRE(ability);
 const auto *power=powers.findSpecialPowerTemplate("NativeChargePower");REQUIRE(power);
 ability->initiateIntentToDoSpecialPower(power,victim,nullptr,0,0);f.frames(1);CHECK(o->testModelCondition(CombatNames::modelCondition("CHARGING")));
 ability->finishAbility(false,true);CHECK(o->getAIUpdateInterface()->isIdle());
 const Coord3D location{300,400,0};ability->initiateIntentToDoSpecialPower(power,nullptr,&location,0,0);ability->update();
 CHECK(o->getAIUpdateInterface()->currentStateId()==0x25);CHECK(o->getAIUpdateInterface()->stateMachine().goalPosition().y==400);
 XferSave saved;ability->xfer(saved);StateHasher before;ability->crc(before);XferLoad loaded(saved.data());ability->xfer(loaded);StateHasher after;ability->crc(after);CHECK(before.value()==after.value());
 ability->finishAbility(false,true);
}

TEST_CASE("bfme1 ai retail: Bridge producer reads the pristine retail model and creates the canonical object before classification")
{
 const char *install=std::getenv("BFME1_INSTALL");if(!install || !*install){retailtest::printSkip("BFME1 Bridge assets (BFME1_INSTALL unset)");return;}
 Fixture fx(install);ArchiveW3DFileSource source(fx.fs);WW3DAssetManager assets(source);LiveGame game(*fx.world,fx.fs,assets,fx.options);LiveGame::Options options;options.mapName="map mp fords of isen";std::string error;
 REQUIRE_MESSAGE(game.load(options,&error),error);
 // Authored test endpoints over a retail map, using the retail roads.ini and W3D.
 // This is an asset-backed producer check, not a claim that Fords has endpoint bridges.
 std::vector<MapObject> objects(2);objects[0].m_flags=FLAG_BRIDGE_POINT1;objects[1].m_flags=FLAG_BRIDGE_POINT2;
 objects[0].m_objectName="TEST_EuropeanBridgeWide";objects[0].m_location={300,500,20};objects[1].m_location={700,500,40};
 objects[0].m_properties.setAsciiString("objectName","RetailRoadBridge");
 TerrainLogic terrain;terrain.init(game.map().heightMap,game.map().chunks);std::vector<std::string> stops;
 REQUIRE_MESSAGE(Bfme1MapBridges::create(fx.fs,assets,game.logic(),terrain,objects,stops,&error),error);
 REQUIRE(terrain.bfme1Bridges().size()==1);const auto &b=terrain.bfme1Bridges().front();auto *object=game.logic().findObjectByID(b.objectID);REQUIRE(object);
 CHECK(object->getTemplate()->getName()=="GenericBridge");CHECK(object->getName()=="RetailRoadBridge");// tolerance: the two retail model edges must enclose nonzero width; no authored width is guessed.
 CHECK(b.fromLeft.y>b.fromRight.y);
 // tolerance: the far edge uses the same model-derived width, preserving its orientation.
 CHECK(b.toLeft.y>b.toRight.y);
 CHECK(b.from.z==SimMath::fstpDword(SimMath::pc24AddW(SimMath::pc24AddW(terrain.getGroundHeight(300,500),20),0.25f)));
 TerrainPathfindSource provider(terrain,game.map().heightMap,game.map().chunks);Pathfinder pf(pathtest::testConfig(),nullptr);pf.newMap(provider);
 CHECK(pf.raisedLayerCount()==1);CHECK(pf.getLayerAt(500,500,b.height(500,500))==(PathfindLayerEnum)2);
 CHECK_FALSE(stops.empty());
 // Mounted Mumak charge weapon through the native machine, with an explicit
 // primary lock as retail's charge special-power trigger uses.
 auto *mumak=game.createObject("MordorMumakil",0,{400,800,0},0,&error);REQUIRE_MESSAGE(mumak,error);
 auto *victim=game.createObject("RohanPeasant",0,{550,800,0},0,&error);REQUIRE_MESSAGE(victim,error);
 auto *ai=mumak->getAIUpdateInterface();auto *weapons=mumak->getWeapons();REQUIRE(ai);REQUIRE(weapons);REQUIRE(weapons->weaponInSlot(0));
 CHECK(weapons->weaponInSlot(0)->getTemplate()->m_canFireWhileCharging);weapons->setWeaponLock(0,LOCKED_TEMPORARILY);weapons->loadExtraWeapon(*weapons->weaponInSlot(0));
 CHECK(ai->aiForceAttackObject(victim,CMD_FROM_PLAYER));auto *attack=dynamic_cast<AIAttackState*>(ai->stateMachine().currentState());REQUIRE(attack);REQUIRE(attack->attackMachine());
 CHECK(attack->attackMachine()->name()=="AttackFireWhileChargingMachine");attack->attackMachine()->updateStateMachine();CHECK(mumak->testModelCondition(CombatNames::modelCondition("CHARGING")));
 CHECK(ai->curLocomotorSet()==4);ai->aiIdle(CMD_FROM_PLAYER);CHECK(ai->curLocomotorSet()==0);CHECK_FALSE(mumak->testModelCondition(CombatNames::modelCondition("CHARGING")));
}
