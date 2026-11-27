// OpenBFME unit tests, PORT-330 (fork issue 847, stop S-190): the FXList block's parse-time name checks.
//
// Expectations come from RotWK game.dat, not from the code below:
//   * a `Sound` nugget's `Name` goes RW 0x73B217 -> RW 0x73AA94: "NoSound" (any case) clears it, anything else asks TheAudio's
//     findAudioEventInfo (RW 0xDE42FC, slot 0x12C) and an unknown name is the code-3 `Invalid Sound '%s'` (the throw at RW 0x73AB1A..0x73AB3F,
//     the format string RW 0xC2500C);
//   * an `EvaEventOwner` / `EvaEventAlly` / `EvaEventEnemy` name goes RW 0x5DE588 -> RW 0x5DE0D8 (TheEva RW 0xDE3670, base name map + 0x48):
//     "None" is -1 without a lookup (RW 0x5DE0E3), an unknown name is -1 and RW 0x5DE14E throws code 3,
//     `Expected a recognized Eva event name or 'None'; got '%s'` (RW 0xBF2678);
//   * both tables are loaded by their own subsystems (TheAudio RW 0x4538DA, TheEva RW 0x5DE837) before TheFXListStore's entry in
//     GameEngine::init, so a retail FXList block always sees them: the checks are never skipped in a loaded world.

#include "doctest.h"

#include "IniTestUtil.h"

#include "Common/Audio/AudioIni.h"
#include "Common/BfmeGame.h"
#include "Common/INI.h"
#include "GameClient/FXList.h"
#include "GameClient/FXPlayback.h"
#include "GameLogic/Object/RetailObjectWorld.h"

#include <memory>
#include <string>
#include <vector>

namespace
{
// RotWK's own data layout: retail's legend marks the mount (Common/BfmeGame.h), so the world picks RotWK's grammar instead of BFME1's
const initest::FileList kRotwkLegend = { { "Data\\INI\\Default\\SubsystemLegend.ini", "; legend\r\n" },
	{ "Data\\INI\\Default\\SubsystemLegendExpansion1.ini", "; the expansion's legend\r\n" } };

// A world and the FXList blocks parsed through it: the world registers its own `FXList` handler (Common/INI/INIBlockStubs.cpp: RW 0x5E2518 into
// TheFXListStore, which its context makes its own store) and answers the parse-time name checks with its TheAudio / TheEva tables
// (MakeWorldFXListParseServices, GameClient/FXListParseServices.cpp).
struct ParseWorld
{
	initest::Fixture fixture;
	RetailObjectWorld world;
	std::unique_ptr<RetailObjectWorld::ContextScope> context;

	explicit ParseWorld(const initest::FileList &files = kRotwkLegend) : fixture(files), world(fixture.fsys) { context = world.enterContext(); }

	// TheAudio's and TheEva's blocks (RW 0x4538DA / RW 0x5DE837) into this world's tables, as the subsystem load fills them
	void addAudio(const std::string &text) { CHECK(initest::loadError(world.iniEnvironment(), "audio.ini", text).empty()); }

	// The synthetic legend names no subsystem, so the startup load reads no file: calling it is what makes reload() legal (MAPRESET-1's rule)
	void loadOk()
	{
		error.clear();
		CHECK(world.load(&error));
		CHECK(error.empty());
	}

	std::string load(const std::string &text, int *code = nullptr) { return initest::loadError(world.iniEnvironment(), "fxlist.ini", text, INI_LOAD_OVERWRITE, code); }

	const FXListStore &lists() const { return world.fxLists(); }

	std::string error;
};

const std::string kAudioTable =
	"AudioEvent KnownSound\n"
	"  Sounds = a.wav b.wav\n"
	"End\n"
	"NewEvaEvent KnownEva\n"
	"  Priority = 5\n"
	"End\n";
} // namespace

TEST_CASE("PORT-330 S-190: an FXList Sound nugget's name is checked against TheAudio while the block parses (RW 0x73B217 -> RW 0x73AA94)")
{
	ParseWorld w;
	w.addAudio(kAudioTable);
	CHECK(w.load("FXList TestSound\n  Sound\n    Name = KnownSound\n  End\nEnd\n").empty());
	REQUIRE(w.lists().findFXList("TestSound") != nullptr);
	REQUIRE(w.lists().findFXList("TestSound")->nuggets().size() == 1);

	// RW 0x73AA94's throw (RW 0xC2500C) is a code-3 INI error naming the event
	int code = 0;
	const std::string error = w.load("FXList TestBadSound\n  Sound\n    Name = NoSuchSoundEvent\n  End\nEnd\n", &code);
	CHECK(code == 3);
	CHECK(error.find("Invalid Sound 'NoSuchSoundEvent'") != std::string::npos);
	// the block does not survive: the list is replaced before its fields are read (RW 0x5E2518), so it holds no Sound nugget
	const FXList *kept = w.lists().findFXList("TestBadSound");
	REQUIRE(kept != nullptr);
	CHECK(kept->nuggets().empty());

	// "NoSound" clears the name without a lookup (RW 0x73AA94's first test), in any case
	CHECK(w.load("FXList TestNoSound\n  Sound\n    Name = NoSound\n  End\n  Sound\n    Name = noSound\n  End\nEnd\n").empty());
	const FXList *silent = w.lists().findFXList("TestNoSound");
	REQUIRE(silent != nullptr);
	REQUIRE(silent->nuggets().size() == 2);
	CHECK(static_cast<const SoundFXNugget *>(silent->nuggets()[0].get())->m_soundName.empty());
}

TEST_CASE("PORT-330 S-190: an FXList EvaEvent name is checked against TheEva while the block parses (RW 0x5DE588 -> RW 0x5DE0D8)")
{
	ParseWorld w;
	w.addAudio(kAudioTable);
	CHECK(w.load("FXList TestEva\n  EvaEvent\n    EvaEventOwner = KnownEva\n    EvaEventAlly = None\n    EvaEventEnemy = DefaultEvaEvent\n  End\nEnd\n").empty());
	const FXList *eva = w.lists().findFXList("TestEva");
	REQUIRE(eva != nullptr);
	REQUIRE(eva->nuggets().size() == 1);
	const EvaEventFXNugget *nugget = static_cast<const EvaEventFXNugget *>(eva->nuggets()[0].get());
	CHECK(nugget->m_owner == "KnownEva");
	CHECK(nugget->m_ally.empty()); // "None" is -1: no event on that relationship
	CHECK(nugget->m_enemy == "DefaultEvaEvent");

	// RW 0x5DE14E: an unknown name is a code-3 INI error (RW 0x5DE0D8 answers -1 for it, which is not "None")
	int code = 0;
	const std::string error = w.load("FXList TestBadEva\n  EvaEvent\n    EvaEventOwner = NoSuchEvaEvent\n  End\nEnd\n", &code);
	CHECK(code == 3);
	CHECK(error.find("Expected a recognized Eva event name or 'None'; got 'NoSuchEvaEvent'") != std::string::npos);

	// the table is TheEva's, not a fixed list: an event the world's Eva table defines is accepted, and a name it does not define is not
	CHECK(w.load("NewEvaEvent LaterEvent\n  Priority = 1\nEnd\n").empty());
	CHECK(w.load("FXList TestLater\n  EvaEvent\n    EvaEventAlly = LaterEvent\n  End\nEnd\n").empty());
	CHECK(w.load("FXList TestCaseSensitive\n  EvaEvent\n    EvaEventAlly = knowneva\n  End\nEnd\n").find("recognized Eva event name") != std::string::npos);
}

TEST_CASE("PORT-330 S-190: the neighbour behaviour holds - a store without a world's tables parses unvalidated and reports it")
{
	// FX-1's fallback (a bare store, e.g. a unit test's): RW would have thrown, here the name is kept and the store says it did not check
	initest::Fixture fixture;
	FXListStore bare;
	fixture.env.blocks.registerBlock("FXList", [&bare](INI *ini) {
		FXListStore *saved = TheFXListStore;
		TheFXListStore = &bare;
		try
		{
			ParseFXListDefinitionGlobal(ini);
		}
		catch (...)
		{
			TheFXListStore = saved;
			throw;
		}
		TheFXListStore = saved;
	});
	CHECK(initest::loadError(fixture.env, "fxlist.ini",
			  "FXList Unchecked\n  Sound\n    Name = NotInAnyTable\n  End\n  EvaEvent\n    EvaEventOwner = NotInAnyTable\n  End\nEnd\n")
			  .empty());
	CHECK(bare.unvalidatedSoundNames() == 1u);
	CHECK(bare.unvalidatedEvaNames() == 1u);
	CHECK(bare.unverified().size() == 2u);
	const FXList *kept = bare.findFXList("Unchecked");
	REQUIRE(kept != nullptr);
	CHECK(static_cast<const SoundFXNugget *>(kept->nuggets()[0].get())->m_soundName == "NotInAnyTable");
}

TEST_CASE("PORT-330 S-190: a loaded world installs the tables' answers on its own FXList store (RW 0x5E2518 through RW 0x73AA94 / RW 0x5DE0D8)")
{
	// A world makes TheAudio (RW 0x4538DA) and TheEva (RW 0x5DE837) current before TheFXListStore (RW 0x63AF2D onward) and owns both; its store
	// gets the answers (RetailObjectWorld::installFxParseServices). Without that installation the blocks below would load without an error and the
	// store would count the names as unchecked.
	initest::Fixture fixture(kRotwkLegend);
	REQUIRE(!isBfme1Data(fixture.fsys)); // RotWK's grammar, as the retail load has it
	RetailObjectWorld world(fixture.fsys);
	const auto context = world.enterContext();
	REQUIRE(world.fxListParseServices() != nullptr); // built with the world, before any load
	// the world's own FXList handler writes into the store this accessor names
	REQUIRE(initest::loadError(world.iniEnvironment(), "audio.ini",
				"AudioEvent WorldKnownSound\n  Sounds = a.wav b.wav\nEnd\nNewEvaEvent WorldKnownEva\n  Priority = 5\nEnd\n")
				.empty());

	CHECK(initest::loadError(world.iniEnvironment(), "fxlist.ini",
			  "FXList WorldKnownList\n  Sound\n    Name = WorldKnownSound\n  End\n  EvaEvent\n    EvaEventOwner = WorldKnownEva\n  End\nEnd\n")
			  .empty());
	REQUIRE(world.fxLists().findFXList("WorldKnownList") != nullptr);

	// a name neither table has is the code-3 INI error retail raises on both paths, through the world's own store
	int code = 0;
	const std::string soundError = initest::loadError(world.iniEnvironment(), "fxlist.ini",
		"FXList WorldBadSound\n  Sound\n    Name = WorldNoSuchSound\n  End\nEnd\n", INI_LOAD_OVERWRITE, &code);
	CHECK(code == 3);
	CHECK(soundError.find("Invalid Sound 'WorldNoSuchSound'") != std::string::npos);
	code = 0;
	const std::string evaError = initest::loadError(world.iniEnvironment(), "fxlist.ini",
		"FXList WorldBadEva\n  EvaEvent\n    EvaEventOwner = WorldNoSuchEva\n  End\nEnd\n", INI_LOAD_OVERWRITE, &code);
	CHECK(code == 3);
	CHECK(evaError.find("Expected a recognized Eva event name or 'None'; got 'WorldNoSuchEva'") != std::string::npos);
}

TEST_CASE("PORT-330 S-190: the answers survive a world reload, which replaces the tables they read (RetailObjectWorld::reload, MAPRESET-1)")
{
	// The client's FX player takes the pointer once (GodotGameWorld::setup) and its playback outlives a reload of the world's state, which retail
	// never does: TheAudio (RW 0x4538DA) and TheEva (RW 0x5DE837) are one instance per process there. So the services answer from the CURRENT state.
	ParseWorld w;
	FXListParseServices *const services = w.world.fxListParseServices();
	REQUIRE(services != nullptr);
	w.addAudio("AudioEvent BeforeReload\n  Sounds = a.wav\nEnd\n");
	CHECK(services->audioEventExists("BeforeReload"));
	REQUIRE(w.load("FXList BeforeReloadList\n  Sound\n    Name = BeforeReload\n  End\nEnd\n").empty());

	w.loadOk();
	w.world.reload();
	// the same object, now reading the rebuilt state: the old table is gone with the state, and the new one answers
	CHECK(w.world.fxListParseServices() == services);
	CHECK_FALSE(services->audioEventExists("BeforeReload"));
	CHECK(w.world.fxLists().findFXList("BeforeReloadList") == nullptr); // the reload's own store, not the old one
	w.addAudio("AudioEvent AfterReload\n  Sounds = b.wav\nEnd\n");
	CHECK(services->audioEventExists("AfterReload"));
	// and the reloaded world's store throws through them, so the store did not keep the freed state's services
	int code = 0;
	CHECK(w.load("FXList ReloadedBad\n  Sound\n    Name = GoneWithTheOldState\n  End\nEnd\n", &code).find("Invalid Sound 'GoneWithTheOldState'") != std::string::npos);
	CHECK(code == 3);
}

TEST_CASE("PORT-330 S-190 port fix: a playback whose world went drops the services and parses unvalidated (GodotGameWorld::setup failing again)")
{
	// The live game's FX player outlives its world: a second setup that replaces the world and then fails (the world load, the map cache or the
	// settings) leaves no world. The setup clears the services on the FX player and its playback (GodotFXPlayer.h setParseServices), so the next
	// parse uses none: unvalidated and reported (S-190), never a call into the destroyed world's tables.
	initest::Fixture fxFiles;
	FXPlayback playback(fxFiles.fsys, RandomAlgorithm::RotWK_GameDat_LCG);
	{
		auto w = std::make_unique<ParseWorld>();
		w->addAudio(kAudioTable);
		playback.setParseServices(w->world.fxListParseServices());
		const std::vector<std::string> checked = playback.loadIniText("checked.ini", "FXList Checked\n  Sound\n    Name = NotKnown\n  End\nEnd\n");
		REQUIRE(checked.size() == 1u);
		CHECK(checked.front().find("Invalid Sound 'NotKnown'") != std::string::npos);
		playback.setParseServices(nullptr); // what the setup does before the world goes
	}
	const std::vector<std::string> errors = playback.loadIniText("after.ini", "FXList AfterTheWorld\n  Sound\n    Name = NotKnown\n  End\nEnd\n");
	CHECK(errors.empty());
	bool reported = false;
	for (const std::string &line : playback.unverified())
	{
		reported = reported || line.find("S-190") != std::string::npos;
	}
	CHECK(reported);
}
