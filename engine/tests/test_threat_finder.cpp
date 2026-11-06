// OpenBFME unit tests (lane RAI-THREAT): the named map threat areas of RotWK 2.01. Run only when ROTWK_INSTALL / BFME2_INSTALL are set (otherwise
// they print SKIP). The retail case loads the Angmar campaign mission, whose ObjectsList places four "ThreatFinder" markers (RW 0x7EDECE's map
// objects); the script readers are RW 0x7C68AA (SET_COUNTER_TO_THREAT_FINDER_THREAT) and RW 0x608BCA's KindOf THREAT_FINDER branch.

#include "doctest.h"
#include "BuildTestUtil.h"
#include "ScriptTestUtil.h"
#include "StartTestUtil.h"

#include "Common/Dict.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "GameClient/GUI/Skirmish/IniSkirmishSetupSource.h"
#include "GameClient/LiveGame.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/ThreatFinderUpdate.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ScriptEngine/ScriptEngine.h"
#include "GameLogic/SkirmishAI/AIThreatFinder.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace
{

// the campaign mission's map objects (LiveGame -> MapObjectLoop -> Object::friend_onMapPlacement)
struct CampaignGame
{
	std::unique_ptr<ArchiveW3DFileSource> source;
	std::unique_ptr<WW3DAssetManager> assets;
	std::unique_ptr<LiveGame> game;

	void load(starttest::Shared &s, const std::string &map)
	{
		source = std::make_unique<ArchiveW3DFileSource>(*s.mount->fs);
		assets = std::make_unique<WW3DAssetManager>(*source);
		game = std::make_unique<LiveGame>(*s.world, *s.mount->fs, *assets, s.options);
		LiveGame::Options o;
		o.mapName = map;
		o.campaign = true;
		std::string error;
		REQUIRE_MESSAGE(game->load(o, &error), error);
	}

	GameLogic &logic() { return game->logic(); }
};

// the lobby's result: slot 0 a human (`faction0`), slot 1 a computer of `aiState` (`faction1`) on Evendim, start positions 0 and 4
bool startSkirmish(starttest::Shared &s, const std::string &faction0, const std::string &faction1, SlotState aiState, std::uint32_t seed,
	buildtest::Game &out, std::string *error)
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
	message.game.slots[1].state = aiState;
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

std::vector<Object *> objectsOfTemplate(GameLogic &logic, const char *templateName)
{
	std::vector<Object *> out;
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getTemplate() && o->getTemplate()->getName() == templateName)
		{
			out.push_back(o);
		}
	}
	return out;
}

int counterValue(ScriptEngine &engine, const char *name)
{
	return engine.counter(name).value;
}

bool hasNote(const ScriptEngine &engine, const std::string &part)
{
	for (const auto &kv : engine.stats().notes)
	{
		if (kv.first.find(part) != std::string::npos)
		{
			return true;
		}
	}
	return false;
}

} // namespace

TEST_CASE("threat finder retail: the Angmar mission's four ThreatFinder markers get named finders with the template's DefaultRadius (RW 0x7EDECE)")
{
	OPENBFME_REQUIRE_START(s);
	CampaignGame g;
	g.load(*s, "map ang angmar");
	GameLogic &logic = g.logic();
	const std::vector<Object *> markers = objectsOfTemplate(logic, "ThreatFinder");
	REQUIRE(markers.size() == 4); // the map's four "Fort 1/2/3 Revealer" and "Dragon Location" markers
	for (Object *marker : markers)
	{
		REQUIRE(!marker->getName().empty());
		auto *module = dynamic_cast<ThreatFinderUpdate *>(marker->findModule("ThreatFinderUpdate"));
		REQUIRE_MESSAGE(module, marker->getName());
		REQUIRE(module->finder());
		CHECK(module->finder()->getRadius() == 100.0f); // ThreatFinder / TacticalMarkerBase's DefaultRadius = 100.0f
		AIThreatFinder *finder = logic.threatFinders().find(marker->getName());
		REQUIRE_MESSAGE(finder, marker->getName());
		CHECK(finder == module->finder());
		// RW 0x7EDDB6: the update copies the object's position into the finder
		const Coord3D before = finder->getPosition();
		Coord3D moved = *marker->getPosition();
		moved.x += 25.0f;
		marker->setPosition(&moved);
		CHECK(module->update() == UPDATE_SLEEP_NONE);
		const Coord3D after = finder->getPosition();
		CHECK(after.x == moved.x);
		CHECK(after.y == moved.y);
		CHECK(after.z == moved.z);
		CHECK(before.x != after.x);
	}
	// the names the map's SidesList gives the markers are the manager's keys
	for (const char *name : { "Fort 1 Revealer", "Fort 2 Revealer", "Fort 3 Revealer", "Dragon Location" })
	{
		CHECK_MESSAGE(logic.threatFinders().find(name) != nullptr, name);
	}
}

TEST_CASE("threat finder: SET_COUNTER_TO_THREAT_FINDER_THREAT and the KindOf THREAT_FINDER unit-threat branch read the named area (RW 0x7C68AA / 0x608BCA)")
{
	OPENBFME_REQUIRE_START(s);
	buildtest::Game g;
	std::string error;
	REQUIRE_MESSAGE(startSkirmish(*s, "FactionMen", "FactionMordor", SLOT_EASY_AI, 7301, g, &error), error);
	GameLogic &logic = g.live->logic();
	const Player *men = playerAtStart(logic, 0);
	const Player *mordor = playerAtStart(logic, 4);
	REQUIRE(men != nullptr);
	REQUIRE(mordor != nullptr);

	const Coord3D area{ 500.0f, 500.0f, 0.0f };
	Object *marker = g.live->createObject("ThreatFinder", men->getPlayerIndex(), area, 0.0f, &error);
	REQUIRE_MESSAGE(marker, error);
	marker->setName("Test Threat Area");
	Dict properties;
	properties.setReal("objectThreatFinderRadius", 250.0f);
	marker->friend_onMapPlacement(&properties); // MapObjectLoop's call with the map's properties (RW 0x694692)
	auto *module = dynamic_cast<ThreatFinderUpdate *>(marker->findModule("ThreatFinderUpdate"));
	REQUIRE(module);
	REQUIRE(module->finder());
	CHECK(module->finder()->getRadius() == 250.0f);
	CHECK(logic.threatFinders().find("Test Threat Area") == module->finder());

	// two Mordor hordes inside the area (their members too), one Men horde outside it
	const Coord3D near1{ area.x + 60.0f, area.y, 0.0f };
	const Coord3D near2{ area.x, area.y + 90.0f, 0.0f };
	const Coord3D far{ area.x + 600.0f, area.y, 0.0f };
	Object *enemy1 = g.live->createObject("MordorFighterHorde", mordor->getPlayerIndex(), near1, 0.0f, &error);
	REQUIRE_MESSAGE(enemy1, error);
	Object *enemy2 = g.live->createObject("MordorFighterHorde", mordor->getPlayerIndex(), near2, 0.0f, &error);
	REQUIRE_MESSAGE(enemy2, error);
	Object *ally = g.live->createObject("GondorFighterHorde", men->getPlayerIndex(), far, 0.0f, &error);
	REQUIRE_MESSAGE(ally, error);
	g.live->advance(0.2); // the module's update copies the marker's position; the hordes' members exist
	REQUIRE(module->finder()->getPosition().x == marker->getPosition()->x);

	// the value the skirmish AI's own finder math computes at the same position and radius: the same AIThreatFinder scan the targets use (S-417 / S-418)
	const AIThreatFinder::ThreatRecord expected = AIThreatFinder::scanSum(logic, *marker->getPosition(), 250.0f, false, *men, 0);
	// tolerance: the point is that the action reads the same value as the AI's scan; the per-unit threat values are pinned by test_skirmish_ai_u22
	REQUIRE(expected.v[0] > 0.0f); // the two enemy hordes and their members

	ScriptEngine &engine = logic.scriptEngine();
	const ScriptActionRec finderAction = scripttest::act("SET_COUNTER_TO_THREAT_FINDER_THREAT",
		{ scripttest::sp(4, "Threat Finder Counter"), scripttest::sp(67, "Test Threat Area"), scripttest::sp(66, "Enemies"),
			scripttest::sp(11, men->getPlayerName()) });
	engine.executeAction(finderAction);
	CHECK(counterValue(engine, "Threat Finder Counter") == SimMath::cvttss2si(expected.v[0]));

	// RW 0x608BCA's finder branch ignores its radius parameter and reads the finder's own 250: a 1.0 parameter would find nothing
	const ScriptActionRec unitAction = scripttest::act("SET_COUNTER_TO_UNIT_THREAT",
		{ scripttest::sp(4, "Unit Threat Counter"), scripttest::sp(14, "Test Threat Area"), scripttest::rp(1.0f) });
	engine.executeAction(unitAction);
	CHECK(counterValue(engine, "Unit Threat Counter") == SimMath::cvttss2si(expected.v[0]));

	// RW 0x608BCA's helper always writes the counter at 0x608CFA: a missing unit and a marker without a finder leave 0, not the old value
	engine.counter("Unit Threat Counter").value = 12345;
	engine.executeAction(scripttest::act("SET_COUNTER_TO_UNIT_THREAT", { scripttest::sp(4, "Unit Threat Counter"), scripttest::sp(14, "No Such Unit"), scripttest::rp(1.0f) }));
	CHECK(counterValue(engine, "Unit Threat Counter") == 0);
	Object *unplaced = g.live->createObject("ThreatFinder", men->getPlayerIndex(), Coord3D{ -400.0f, -400.0f, 0.0f }, 0.0f, &error);
	REQUIRE_MESSAGE(unplaced, error);
	unplaced->setName("No Finder Marker");
	engine.counter("Unit Threat Counter").value = 12345;
	engine.executeAction(scripttest::act("SET_COUNTER_TO_UNIT_THREAT", { scripttest::sp(4, "Unit Threat Counter"), scripttest::sp(14, "No Finder Marker"), scripttest::rp(1.0f) }));
	CHECK(counterValue(engine, "Unit Threat Counter") == 0);
	// the team branch without a team writes the same zero
	engine.counter("Team Threat Counter").value = 12345;
	engine.executeAction(scripttest::act("SET_COUNTER_TO_TEAM_THREAT", { scripttest::sp(4, "Team Threat Counter"), scripttest::sp(3, "No Such Team") }));
	CHECK(counterValue(engine, "Team Threat Counter") == 0);

	// RW 0x7C68AA's warning for a name the manager lacks; the counter is left alone
	const int before = counterValue(engine, "Threat Finder Counter");
	const ScriptActionRec missing = scripttest::act("SET_COUNTER_TO_THREAT_FINDER_THREAT",
		{ scripttest::sp(4, "Threat Finder Counter"), scripttest::sp(67, "No Such Area"), scripttest::sp(66, "Enemies"),
			scripttest::sp(11, men->getPlayerName()) });
	engine.executeAction(missing);
	CHECK(counterValue(engine, "Threat Finder Counter") == before);
	CHECK(hasNote(engine, "WARNING - Threat Finder not found during script execution: No Such Area."));

	// RW 0x6A85B6's mask must name exactly one player: <All Players> names none, so the action leaves the counter alone (lane RAI-THREAT)
	engine.counter("Mask Counter").value = -12345;
	engine.executeAction(scripttest::act("SET_COUNTER_TO_THREAT_FINDER_THREAT",
		{ scripttest::sp(4, "Mask Counter"), scripttest::sp(67, "Test Threat Area"), scripttest::sp(66, "Enemies"), scripttest::sp(11, "<All Players>") }));
	CHECK(counterValue(engine, "Mask Counter") == -12345);

	// a relationship that is neither Enemies nor Allies returns before the lookup (RW 0x7C691A)
	const ScriptActionRec neutral = scripttest::act("SET_COUNTER_TO_THREAT_FINDER_THREAT",
		{ scripttest::sp(4, "Threat Finder Counter"), scripttest::sp(67, "No Such Area"), scripttest::sp(66, "Neutral"),
			scripttest::sp(11, men->getPlayerName()) });
	int notesBefore = 0;
	for (const auto &kv : engine.stats().notes)
	{
		notesBefore += kv.first.find("No Such Area") != std::string::npos ? (int)kv.second : 0;
	}
	engine.executeAction(neutral);
	int notesAfter = 0;
	for (const auto &kv : engine.stats().notes)
	{
		notesAfter += kv.first.find("No Such Area") != std::string::npos ? (int)kv.second : 0;
	}
	CHECK(notesAfter == notesBefore);

	// the finder travels in the snapshot: the load rebuilds the manager entry (RW 0x7EDF70 -> 0x7EDC93) with the radius and position
	std::vector<std::uint8_t> bytes;
	REQUIRE_MESSAGE(logic.saveSnapshot(bytes, &error), error);
	REQUIRE_MESSAGE(g.live->loadSnapshot(bytes, &error), error);
	GameLogic &loaded = g.live->logic();
	AIThreatFinder *loadedFinder = loaded.threatFinders().find("Test Threat Area");
	REQUIRE(loadedFinder != nullptr);
	CHECK(loadedFinder->getRadius() == 250.0f);
	Object *loadedMarker = loaded.scriptEngine().getUnitNamed("Test Threat Area");
	REQUIRE(loadedMarker != nullptr);
	CHECK(loadedFinder->getPosition().x == loadedMarker->getPosition()->x);
	CHECK(loadedFinder->getPosition().y == loadedMarker->getPosition()->y);
	ScriptEngine &loadedEngine = loaded.scriptEngine();
	const ScriptActionRec afterLoad = scripttest::act("SET_COUNTER_TO_THREAT_FINDER_THREAT",
		{ scripttest::sp(4, "Loaded Counter"), scripttest::sp(67, "Test Threat Area"), scripttest::sp(66, "Enemies"),
			scripttest::sp(11, men->getPlayerName()) });
	loadedEngine.executeAction(afterLoad);
	CHECK(counterValue(loadedEngine, "Loaded Counter") == SimMath::cvttss2si(expected.v[0]));

	// the marker's destruction removes the manager entry (RW 0x7EDE52)
	loaded.destroyObject(loadedMarker);
	g.live->advance(0.2);
	CHECK(loaded.threatFinders().find("Test Threat Area") == nullptr);
}
