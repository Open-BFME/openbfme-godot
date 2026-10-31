// OpenBFME unit tests. GPL-3.0.
// Lane ONLINE16-FIX (the bisect of the skirmish Start SIGSEGV on lane/online16, 67f009f28 MAX_SLOTS 8 -> 16): the Godot client always gives the
// lobby the battle-honors store (ShellEnvironment::skirmishHonors); AptSkirmish::startGame filled MAX_SLOTS (16) honors slots into an eight-entry
// array and overwrote the record's map key, so pressing Start crashed in memmove. This drives the real Skirmish.apt lobby with that store, presses
// StartGame, and runs the game the lobby emitted to its first logic frame: on a retail eight-start map and on the sixteen-start P16 fixture.

#include "doctest.h"

#include "P16TestUtil.h"
#include "StartShellFx.h"

#include "GameClient/GUI/Skirmish/RealTimeStats.h"
#include "GameClient/GUI/Skirmish/SkirmishSetup.h"

namespace
{
// the lobby's maps: the mounted cache, plus the P16 fixture (generated in memory, never on disk)
class LobbyMaps : public WorldSkirmishSetupSource
{
public:
	std::vector<MapCacheEntry> all;
	const std::vector<MapCacheEntry> &maps() const override { return all; }
};

void startFromLobby(starttest::Shared &s, const std::string &mapKey, bool wide)
{
	using namespace starttest;
	std::string error;
	if (wide)
	{
		p16test::mount(s);
	}
	ShellFx fx(s, 4242);
	LobbyMaps maps;
	REQUIRE_MESSAGE(maps.load(*s.world, s.settings, *s.mount->fs, &error), error);
	maps.all = maps.WorldSkirmishSetupSource::maps();
	if (wide)
	{
		MapCacheEntry p16 = p16test::cacheEntry();
		p16.displayName = u"P16 fixture";
		maps.all.push_back(p16);
	}
	MemorySkirmishProfiles profiles;
	REQUIRE(profiles.add(u"Gimli"));
	REQUIRE(profiles.setCurrent(u"Gimli"));
	SkirmishHonorsGame honors; // what the Godot client always provides (GodotAptPlayer::skirmishHonors)
	fx.environment.skirmish = &maps;
	fx.environment.profiles = &profiles;
	fx.environment.skirmishHonors = &honors;
	fx.shell->push("Skirmish.apt");
	auto *screen = dynamic_cast<AptSkirmish *>(fx.shell->top());
	REQUIRE(screen);
	fx.tick(90);
	REQUIRE(screen->setup());
	const std::vector<std::string> keys = screen->mapListKeys();
	int row = -1;
	for (size_t i = 0; i < keys.size(); ++i)
	{
		row = keys[i] == mapKey ? (int)i : row;
	}
	REQUIRE_MESSAGE(row >= 0, "the map " << mapKey << " is not in the lobby's list"); // tolerance: any row of the list (its order follows the map cache)
	wheelAndClickRow(fx, screen->mapListWindow(), row);
	REQUIRE(screen->setup()->info().mapName == mapKey);
	CHECK(screen->setup()->info().mapStartCount == (wide ? 16 : LEGACY_MAX_SLOTS));
	GadgetComboBoxSetSelectedPos(screen->slotGadget(1, "Player"), 4, false); // Open, Closed, Easy, Medium, Hard
	REQUIRE(screen->setup()->info().slots[1].state == SLOT_HARD_AI);
	AptButtonInst *start = firstButtonIn(fx.at(screen->level(), "lobby.StartGame"));
	REQUIRE(start);
	clickButton(fx, *start);
	fx.tick(30);
	REQUIRE(fx.sink.messages.size() == 1);
	// the honors record of the game: every game slot, the map key intact
	REQUIRE(honors.valid);
	CHECK(honors.mapKey == mapKey);
	CHECK(honors.localSlot == 0);
	CHECK(honors.slots[0].state == SLOT_PLAYER);
	CHECK(honors.slots[1].state == SLOT_HARD_AI);
	CHECK(honors.slots[(size_t)MAX_SLOTS - 1].state == SLOT_CLOSED);
	// the game the lobby emitted, to its first logic frame
	NewGameStart started(RandomAlgorithm::ZH_CarryChain);
	REQUIRE_MESSAGE(NewGame::prepareNewGame(fx.sink.messages[0], s.world->playerTemplates(), s.settings, maps.all, RandomAlgorithm::ZH_CarryChain, started,
		&error), error);
	ArchiveW3DFileSource source(*s.mount->fs);
	WW3DAssetManager assets(source);
	LiveGame game(*s.world, *s.mount->fs, assets, s.options);
	LiveGame::Options o;
	o.start = &started;
	REQUIRE_MESSAGE(game.load(o, &error), error);
	REQUIRE(game.frame() == 1);
	game.advance(0.2);
	CHECK(game.frame() == 2);
	CHECK(game.report().errors.empty());
}
} // namespace

TEST_CASE("online16-fix retail: StartGame in the skirmish lobby with the honors store starts the game (8-start map; the Start SIGSEGV)")
{
	auto s = p16test::freshWorld();
	if (!s)
	{
		retailtest::printSkip("online16-fix skirmish start (8-start map)");
		return;
	}
	REQUIRE_MESSAGE(s->world, s->error);
	auto context = s->world->enterContext();
	startFromLobby(*s, "maps/map mp evendim/map mp evendim.map", false);
}

TEST_CASE("online16-fix retail: StartGame in the skirmish lobby with the honors store starts the game (16-start P16 map; the Start SIGSEGV)")
{
	auto s = p16test::freshWorld();
	if (!s)
	{
		retailtest::printSkip("online16-fix skirmish start (16-start map)");
		return;
	}
	REQUIRE_MESSAGE(s->world, s->error);
	auto context = s->world->enterContext();
	startFromLobby(*s, p16test::mapKey, true);
}
