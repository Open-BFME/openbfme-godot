// OpenBFME unit tests. GPL-3.0.
// Lane UBER-1: the Uber performance mode (docs/UBER.md): the simulation profile in the game setup, the wire, the replay, the profile identity and the
// state hash; mismatches refused; the Uber simulation identical across runs and logic thread counts.

#include "doctest.h"

#include "StartTestUtil.h"
#include "UberScenarios.h"

#include "Common/JobSystem.h"
#include "Common/SimulationProfile.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"
#include "GameNetwork/GameInfo.h"
#include "GameNetwork/LANAPI.h"
#include "GameNetwork/NetPacket.h"
#include "GameNetwork/ProfileIdentity.h"
#include "GameNetwork/Transport.h"
#include "GameClient/GUI/LoadScreenInfo.h"
#include "GameClient/GUI/Skirmish/SkirmishSetup.h"
#include "GameClient/OptionPreferences.h"
#include "Common/Recorder.h"

#include <cstdio>
#include <cstdlib>
#include <memory>

#include <string>
#include <vector>

#ifndef _WIN32
#include <unistd.h>
#endif

TEST_CASE("uber1: the simulation profile travels in the game setup (LAN options, new game message); an unknown profile is refused")
{
	NewGameMessage m;
	m.game.mapName = "maps/a/a.map";
	m.game.seed = 77;
	m.game.simulationProfile = SimulationProfile::Uber;
	NetByteWriter w;
	NetPacket::writeNewGameMessage(w, m);
	NetByteReader r(w.data());
	NewGameMessage back;
	std::string error;
	REQUIRE_MESSAGE(NetPacket::readNewGameMessage(r, back, &error), error);
	CHECK(back.game == m.game);
	CHECK(back.game.simulationProfile == SimulationProfile::Uber);
	NewGameMessage faithful = m;
	faithful.game.simulationProfile = SimulationProfile::Faithful;
	CHECK_FALSE(faithful.game == m.game);
	// the profile is the GameInfo's i32 before the eight slots' handicaps (lane OWNERFIX-2): 2 is no profile this build knows
	std::vector<std::uint8_t> bad = w.data();
	bad[bad.size() - 8 - 4 * LEGACY_MAX_SLOTS] = 2; // lane CHEAT-1: the Cheat Mode i32 ends the GameInfo
	NetByteReader r2(bad);
	CHECK_FALSE(NetPacket::readNewGameMessage(r2, back, &error));
	CHECK(error.find("simulation profile 2") != std::string::npos);
	CHECK(std::string(simulationProfileName(SimulationProfile::Faithful)) == "faithful");
	CHECK(std::string(simulationProfileName(SimulationProfile::Uber)) == "uber");
}

namespace
{
// restores the logic pool's thread count when the test ends
struct LogicThreadsGuard
{
	int before = JobSystem::logic().threadCount();
	~LogicThreadsGuard() { JobSystem::logic().setThreadCount(before); }
};
} // namespace

namespace
{
// tests/data/uber1/<name>: the stored per-frame hashes (one hex value a line)
std::vector<std::uint32_t> readUberHashes(const char *name)
{
	std::vector<std::uint32_t> out;
	if (FILE *f = std::fopen((std::string(OPENBFME_TEST_DATA_DIR) + "/uber1/" + name).c_str(), "r"))
	{
		unsigned v = 0;
		while (std::fscanf(f, "%x", &v) == 1)
		{
			out.push_back((std::uint32_t)v);
		}
		std::fclose(f);
	}
	return out;
}
void storeUberHashes(const char *name, const std::vector<std::uint32_t> &hashes)
{
	const std::string path = std::string(OPENBFME_TEST_DATA_DIR) + "/uber1/" + name;
	FILE *f = std::fopen(path.c_str(), "w");
	REQUIRE_MESSAGE(f != nullptr, path);
	for (std::uint32_t h : hashes)
	{
		std::fprintf(f, "%08x\n", (unsigned)h);
	}
	std::fclose(f);
}
} // namespace

// The Uber profile must give every peer the same state whatever its core count: the bench battle (at least 3,000 living troops, 8 players) for 40
// frames with 1, 4 and 16 logic threads and 4 again, every frame's hash equal to the stored Uber pin vector (tests/data/uber1/battle3000_40.hashes,
// measured with 1 thread by the build that introduced it; OPENBFME_UBER1_REGEN=1 rewrites it, only for a change MEANT to alter the Uber simulation or
// its hash, said in the commit; lane TARGET-1 re-measured it: RotWK's target choice and mood-check timing; community #73 + #74 re-measured it on the merged tree: the formations centred
// on their hordes, RW 0x86C4EA, and the destination adjustment spiralling around the cell, RW 0x6FA2A4, #73 alone differs from it from frame 0; lane AIDATA-1: the horde run-down, RW 0x748650 /
// 0x74AA69 / 0x6CC07C; lane AIEXPAND-1 re-measured it: the skirmish AI's state hash holds the EXPANSION heuristic's frame counter, RW 0x97D035; merge queue q29
// re-measured it once on the merged tree: AIEXPAND-1's counter, before it AIDATA-1's run-down, compared at each merge point; merge queue q30 re-measured it
// once on the merged tree: #130's allied clearing with the move state's update and exit, RW 0x749497 / 0x74949C, from frame 12; PORTERFOG-1 keeps it; lane SPELLFIX-1 re-measured it: an omitted ShroudClearingRange clears the VisionRange, RW 0x73FB7A /
// 0x69990F, from frame 0, its other changes alone keep it; the horde contain fix (#160) re-measured it: members made where their
// horde stands, RW 0x8718ED, and left where they are by a move, RW 0x871536; the hierarchical block zones (#161)
// re-measured it: the block search's block zones, RW 0x9389A6, and raised branch, RW 0x9389CE, change its corridors, from frame 12; merge queue q32
// re-measured it once on the merged tree: MODCOMPAT-AI's script references and completed upgrades, compared at each merge point; the other items keep it; merge queue q33
// re-measured it once on the merged tree, compared at each merge point: #164 keeps it; MODCOMPAT-MOD1 (its modules), #133 (the straight production exits) and #166
// (the Uber path-search cap) each move it; merge queue q34 re-measured it once on the merged tree, compared at each merge point: SPELLFIX-1, #160
// and #161 each move it). The state hash's own dispatches are counted
// apart from the frame's (PERF-2's collision jobs): with more than one
// thread the hash itself ran chunks on workers. The Uber hash is not the Faithful one, and a Faithful hash dispatches nothing.
// Lane MODCOMPAT-MOD1 re-measured the pin vector at GeometryUpgrade's commit (OPENBFME_UBER1_REGEN=1, a CI host): GeometryUpgrade's per-object GeometryInfo
// (RW Object + 0xA8: the object's shapes enter its hash) moves it, from frame 0.
// Lane MODCOMPAT-MOD1 re-measured the pin vector at RebuildHoleExposeDie's commit (OPENBFME_UBER1_REGEN=1, a CI host): RebuildHoleExposeDie /
// RebuildHoleBehavior (the objects' mask bit Object + 0x459 and the dozers' rebuild byte AI + 0x4D0 in the hash), from frame 0.
// Lane MODCOMPAT-MOD1 re-measured the pin vector at CommandButtonHuntUpdate's commit (OPENBFME_UBER1_REGEN=1, a CI host): CommandButtonHuntUpdate's state (RW
// 0x898A5D: the module sleeps forever until a hunt) in the object hash, from frame 0.
// Lane MODCOMPAT-MOD1 re-measured the pin vector at GrabPassengerSpecialPower's commit (OPENBFME_UBER1_REGEN=1, a CI host): GrabPassengerSpecialPower's
// module state (RW 0x8C942D) in the object hash, from frame 0.
// Lane MODCOMPAT-MOD1 re-measured the pin vector at CritterEmitterUpdate's commit (OPENBFME_UBER1_REGEN=1, a CI host): CritterEmitterUpdate's module state
// (xfer RW 0x8CD256) in the object hash, from frame 0.
// Lane MODCOMPAT-MOD1 re-measured the pin vector at AudioLoopUpgrade's commit (OPENBFME_UBER1_REGEN=1, a CI host): AudioLoopUpgrade's update module and
// upgrade mux in the object hash, from frame 0.
// Lane MODCOMPAT-MOD1 re-measured the pin vector at ClickReactionBehavior's commit (OPENBFME_UBER1_REGEN=1, a CI host): ClickReactionBehavior's click meter
// and countdown (RW 0x85C5DE) in the object hash, from frame 0.
// Lane MODCOMPAT-MOD1 re-measured the pin vector at PickupStuffUpdate's commit (OPENBFME_UBER1_REGEN=1, a CI host): PickupStuffUpdate (every horde's update
// joins and leaves the sleepy update vectors, RW 0x62E982, and its scan state is in the hash), from frame 0.
// Lane MODCOMPAT-MOD1 re-measured the pin vector at AutoPickUpUpdate's commit (OPENBFME_UBER1_REGEN=1, a CI host): AutoPickUpUpdate (its constructor's logic
// random draw, RW 0x8994BF, and its scan state in the object hash), from frame 0.
// Lane MODCOMPAT-MOD1 re-measured the pin vector at BoredUpdate's commit (OPENBFME_UBER1_REGEN=1, a CI host): BoredUpdate (its constructor's logic random
// draw, RW 0x899F1F, and its countdown in the object hash), from frame 0.
TEST_CASE("uber1: the Uber battle hashes as its pin vector in every frame with 1, 4 and 16 logic threads and in a repeated run; the hash's own jobs run on "
		  "workers; Faithful hashes differently and serially")
{
	OPENBFME_REQUIRE_START(s);
	LogicThreadsGuard guard;
	const int units = 3000, frames = 40;
	const char *regenEnv = std::getenv("OPENBFME_UBER1_REGEN");
	const bool regen = regenEnv && *regenEnv == '1';
	// Ambient movement: target idle wandering adds RNG draws and origin state,
	// so these engine references are measured again for this simulation change.
	const std::vector<std::uint32_t> pin = readUberHashes("battle3000_40.hashes");
	if (!regen)
	{
		REQUIRE(pin.size() == (size_t)frames);
	}
	std::vector<std::uint32_t> reference;
	for (int threads : { 1, 4, 16, 4 })
	{
		JobSystem::logic().setThreadCount(threads);
		const JobSystem::Stats before = JobSystem::logic().stats();
		uberbench::Run run;
		uberbench::runBattle(*s->mount->fs, *s->world, s->options, s->settings, units, frames, SimulationProfile::Uber, true, run);
		const JobSystem::Stats after = JobSystem::logic().stats();
		INFO("Uber, " << threads << " logic threads");
		REQUIRE(run.hashes.size() == (size_t)frames);
		CHECK(run.troopsStart >= units);
		CHECK(run.troopsEnd < run.troopsStart); // the armies fought: troops died
		CHECK(run.hashDispatches == (std::uint64_t)frames); // every hash dispatched the objects and the other sections once (lane UBER-HASH: in one dispatch)
		CHECK(after.dispatches - before.dispatches > run.hashDispatches); // the frames' own jobs are counted apart
		if (threads > 1)
		{
			CHECK(run.hashWorkerChunks > 0); // the hash itself ran on several threads
		}
		else
		{
			CHECK(run.hashWorkerChunks == 0);
		}
		if (reference.empty())
		{
			reference = run.hashes;
			if (regen)
			{
				storeUberHashes("battle3000_40.hashes", reference);
			}
			else
			{
				for (int f = 0; f < frames; ++f)
				{
					INFO("pin, frame " << f);
					CHECK(reference[(size_t)f] == pin[(size_t)f]);
				}
			}
		}
		else
		{
			for (int f = 0; f < frames; ++f)
			{
				INFO("frame " << f);
				CHECK(run.hashes[(size_t)f] == reference[(size_t)f]);
			}
		}
	}
	JobSystem::logic().setThreadCount(4);
	uberbench::Run faithful;
	uberbench::runBattle(*s->mount->fs, *s->world, s->options, s->settings, units, 1, SimulationProfile::Faithful, true, faithful);
	REQUIRE(faithful.hashes.size() == 1u);
	CHECK(faithful.hashes[0] != reference[0]);
	// lane PERF-HD: the Faithful hash records its sections' words on the logic pool (lane UBER-HASH: the objects and the other sections in one
	// dispatch) and chains them in the list order on the calling thread: the one serial walk's value (the Faithful pins and the PERF-1 vectors prove
	// it), found faster
	CHECK(faithful.hashDispatches == 1u);
}

namespace
{
ProfileIdentity identity(SimulationProfile p)
{
	MountedArchive a;
	a.install = "rotwk";
	a.canonicalPath = "test.big";
	a.size = 1;
	a.md5 = "0123456789abcdef0123456789abcdef";
	return ProfileIdentity::compute({ a }, RandomAlgorithm::ZH_CarryChain, {}, p);
}

std::uint16_t uberPortBase()
{
#ifdef _WIN32
	const int pid = 4321;
#else
	const int pid = (int)getpid();
#endif
	return (std::uint16_t)(40008 + (pid % 1000) * 16); // test_mp2_lan.cpp's range + 8: the two tests never share a port
}

// a setup source with nothing in it (the load screen's summary needs no faction)
class EmptySetup : public SkirmishSetupSource
{
public:
	const std::vector<SkirmishFaction> &factions() const override { return m_factions; }
	const std::vector<SkirmishColor> &colors() const override { return m_colors; }
	const std::vector<MapCacheEntry> &maps() const override { return m_maps; }
	int defaultStartingCash() const override { return 0; }
	const std::vector<int> &startingCashChoices() const override { return m_cash; }
	int mapContentsMask(const MapCacheEntry &) const override { return 0; }
	bool fileExists(const std::string &) const override { return false; }
	bool hasFileSystem() const override { return false; }

private:
	std::vector<SkirmishFaction> m_factions;
	std::vector<SkirmishColor> m_colors;
	std::vector<MapCacheEntry> m_maps;
	std::vector<int> m_cash;
};

// three LAN lobbies on localhost (test_mp2_lan.cpp's harness), each with its own simulation profile
struct UberLan
{
	std::vector<std::unique_ptr<LANAPI>> peers;
	std::vector<std::vector<LANAPI::Event>> events;
	std::uint64_t clock = 1000;
	explicit UberLan(const std::vector<std::pair<std::u16string, SimulationProfile>> &who)
	{
		LANAPI::Options o;
		o.playerTemplateCount = 16;
		o.colorCount = 10;
		o.lobbyPortBase = uberPortBase();
		o.lobbyPorts = 8;
		o.broadcast = false;
		o.extraTargets = { 0x7F000001u };
		o.resendMs = 300;
		o.actionTimeoutMs = 2000;
		for (const auto &w : who)
		{
			peers.push_back(std::make_unique<LANAPI>(identity(w.second), w.first, o));
			std::string error;
			REQUIRE_MESSAGE(peers.back()->open(&error), error);
		}
		events.resize(peers.size());
	}
	void run(int ticks)
	{
		for (int t = 0; t < ticks; ++t)
		{
			clock += 20;
			for (size_t i = 0; i < peers.size(); ++i)
			{
				peers[i]->update(clock);
				for (LANAPI::Event &e : peers[i]->takeEvents())
				{
					events[i].push_back(e);
				}
			}
			NetSleepMilliseconds(1);
		}
	}
	const LANAPI::Event *last(size_t peer, LANAPI::Event::Kind k) const
	{
		for (auto it = events[peer].rbegin(); it != events[peer].rend(); ++it)
		{
			if (it->kind == k)
			{
				return &*it;
			}
		}
		return nullptr;
	}
};
} // namespace

TEST_CASE("uber1: the profile identity says 'profile uber' for the Uber mode and keeps the Faithful text unchanged; the difference names the line")
{
	const ProfileIdentity faithful = identity(SimulationProfile::Faithful), uber = identity(SimulationProfile::Uber);
	CHECK(faithful.text.find("\nprofile enhanced\n") != std::string::npos);
	CHECK(faithful.text == identity(SimulationProfile::Faithful).text);
	CHECK(faithful == ProfileIdentity::compute({ MountedArchive{ "rotwk", "test.big", "/x/test.big", 1, "0123456789abcdef0123456789abcdef" } },
						  RandomAlgorithm::ZH_CarryChain)); // the default argument is the Faithful identity of the builds before UBER-1
	CHECK(uber.text.find("\nprofile uber\n") != std::string::npos);
	CHECK(uber != faithful);
	CHECK(faithful.simulationProfile() == SimulationProfile::Faithful);
	CHECK(uber.simulationProfile() == SimulationProfile::Uber);
	CHECK(ProfileIdentity::fromText(uber.text).simulationProfile() == SimulationProfile::Uber);
	const std::string diff = ProfileIdentity::difference(faithful, uber);
	CHECK(diff.find("ours: 'profile enhanced' theirs: 'profile uber'") != std::string::npos);
}

TEST_CASE("uber1 lan: a Faithful player is refused at an Uber host's join, an Uber player joins; the host's game and the start run Uber; options of "
		  "another profile are refused")
{
	UberLan lan({ { u"UberHost", SimulationProfile::Uber }, { u"Faithful", SimulationProfile::Faithful }, { u"UberToo", SimulationProfile::Uber } });
	LANAPI &host = *lan.peers[0];
	SkirmishGameInfo g;
	g.mapName = "maps/map mp evendim/map mp evendim.map";
	g.startingCash = 1500;
	g.slots[1].state = SLOT_OPEN;
	g.slots[2].state = SLOT_OPEN;
	g.simulationProfile = SimulationProfile::Faithful; // a setup that did not know the option: the host's identity decides
	host.requestGameCreate(g);
	lan.run(10);
	REQUIRE(host.currentGame());
	CHECK(host.currentGame()->info.simulationProfile == SimulationProfile::Uber);
	// the Faithful player sees the game but cannot join it
	REQUIRE(lan.peers[1]->games().size() == 1);
	lan.peers[1]->requestGameJoin(0);
	lan.run(10);
	REQUIRE(lan.last(1, LANAPI::Event::GameJoin));
	CHECK(lan.last(1, LANAPI::Event::GameJoin)->ret == LANAPI::RET_CRC_MISMATCH);
	CHECK(lan.peers[1]->inLobby());
	CHECK(lan.peers[1]->games()[0].info.simulationProfile == SimulationProfile::Uber); // and would refuse its options anyway
	// the Uber player joins and sees an Uber game
	lan.peers[2]->requestGameJoin(0);
	lan.run(15);
	REQUIRE(lan.peers[2]->currentGame());
	CHECK(lan.peers[2]->currentGame()->info.simulationProfile == SimulationProfile::Uber);
	// the host's own options keep the profile whatever the setup says
	SkirmishGameInfo next = host.currentGame()->info;
	next.simulationProfile = SimulationProfile::Faithful;
	CHECK(host.hostSetOptions(next));
	lan.run(5);
	CHECK(host.currentGame()->info.simulationProfile == SimulationProfile::Uber);
	lan.peers[2]->requestAccept(true);
	lan.run(10);
	std::string reason;
	REQUIRE_MESSAGE(host.hostStartGame(&reason), reason);
	lan.run(20);
	REQUIRE(lan.peers[2]->started());
	CHECK(lan.peers[2]->start().game.game.simulationProfile == SimulationProfile::Uber);
	CHECK(lan.peers[2]->start().profile.simulationProfile() == SimulationProfile::Uber);
	CHECK(host.errors().empty());
	CHECK(lan.peers[2]->errors().empty());
}

TEST_CASE("uber1: a replay carries the Uber profile in its identity and its game; a Faithful player's playback refuses it, an Uber player's accepts it")
{
	NewGameMessage m;
	m.game.mapName = "maps/a/a.map";
	m.game.seed = 5;
	m.game.simulationProfile = SimulationProfile::Uber;
	const std::string path = "uber1_test.replay";
	ReplayHeader h;
	h.game = m;
	h.profile = identity(SimulationProfile::Uber);
	std::string error;
	{
		ReplayWriter rw;
		REQUIRE_MESSAGE(rw.open(path, h, &error), error);
		rw.close(1, 0x1u);
	}
	ReplayFile f;
	REQUIRE_MESSAGE(ReplayFile::load(path, f, &error), error);
	std::remove(path.c_str());
	CHECK(f.header.game.game.simulationProfile == SimulationProfile::Uber);
	CHECK(f.profileMismatch(identity(SimulationProfile::Uber)).empty());
	const std::string mismatch = f.profileMismatch(identity(SimulationProfile::Faithful));
	CHECK(mismatch.find("profile uber") != std::string::npos);
}

TEST_CASE("uber1: the OpenBFME option OpenBFMEUberPerformance picks the simulation profile (absent or no: Faithful) and the load screen names the mode")
{
	OptionPreferences o;
	CHECK(o.simulationProfile() == SimulationProfile::Faithful);
	o.parse("OpenBFMEUberPerformance = yes\r\n");
	CHECK(o.simulationProfile() == SimulationProfile::Uber);
	o.parse("OpenBFMEUberPerformance = no\r\n");
	CHECK(o.simulationProfile() == SimulationProfile::Faithful);
	CHECK(std::string(OptionPreferences::kUberPerformance).rfind(OptionPreferences::kOpenBFMEPrefix, 0) == 0); // an OpenBFME key carries the prefix
	SkirmishGameInfo g;
	g.slots[0].state = SLOT_PLAYER;
	g.simulationProfile = SimulationProfile::Uber;
	EmptySetup setup;
	CHECK(makeLoadScreenInfo(g, setup, nullptr).rulesSummary == u"Uber performance mode");
	g.legendaryHeroes = 4;
	CHECK(makeLoadScreenInfo(g, setup, nullptr).rulesSummary.find(u"; Uber performance mode") != std::u16string::npos);
	g.legendaryHeroes = LegendaryHeroes::kOff;
	g.simulationProfile = SimulationProfile::Faithful;
	CHECK(makeLoadScreenInfo(g, setup, nullptr).rulesSummary.empty());
}

namespace
{
// a replay header's bytes in format 5 (Recorder.h) with any identity and game, as a broken or forged writer would make them
std::vector<std::uint8_t> forgedReplay(const ProfileIdentity &identity, const NewGameMessage &game)
{
	std::vector<std::uint8_t> bytes = { 'O', 'B', 'F', 'M', 'E', 'R', 'E', 'P' };
	NetByteWriter w;
	w.u32(8); // format 8 (lane OWNERFIX-2: 7 adds the slots' handicaps; 8 ANTICHEAT-1's VIEW record): the GameInfo without CHEAT-1's Cheat Mode (format 11)
	w.str(identity.text);
	bytes.insert(bytes.end(), w.data().begin(), w.data().end());
	NetByteWriter g;
	NetPacket::writeNewGameMessage(g, game);
	bytes.insert(bytes.end(), g.data().begin(), g.data().end() - 4); // the Cheat Mode i32 ends the GameInfo
	w = NetByteWriter();
	w.i32(0);
	w.u32((std::uint32_t)RandomAlgorithm::ZH_CarryChain);
	for (int i = 0; i < game.game.serializedSlotCount(); ++i)
	{
		w.i32(-1);
	}
	w.i32(2);
	w.i32(100);
	bytes.insert(bytes.end(), w.data().begin(), w.data().end());
	return bytes;
}
} // namespace

TEST_CASE("uber1 (review): a replay header whose identity and game name different simulation profiles is never written, never read, never played")
{
	NewGameMessage uberGame;
	uberGame.game.mapName = "maps/a/a.map";
	uberGame.game.simulationProfile = SimulationProfile::Uber;
	NewGameMessage faithfulGame = uberGame;
	faithfulGame.game.simulationProfile = SimulationProfile::Faithful;
	std::string error;
	// the writer: a Faithful identity with an Uber game, and the reverse, are refused before the file is made
	for (const auto &pair : { std::make_pair(identity(SimulationProfile::Faithful), uberGame), std::make_pair(identity(SimulationProfile::Uber), faithfulGame) })
	{
		ReplayHeader h;
		h.profile = pair.first;
		h.game = pair.second;
		CHECK_FALSE(h.profileConflict().empty());
		ReplayWriter rw;
		const std::string path = "uber1_conflict.replay";
		std::remove(path.c_str());
		CHECK_FALSE(rw.open(path, h, &error));
		CHECK(error.find("replay header: the identity names the simulation profile") != std::string::npos);
		FILE *f = std::fopen(path.c_str(), "rb");
		CHECK(f == nullptr); // nothing written
		if (f)
		{
			std::fclose(f);
		}
	}
	// the reader: the same header forged byte by byte is refused (Sol's probe: a Faithful identity with an Uber setup loaded and matched a Faithful player)
	ReplayFile file;
	CHECK_FALSE(ReplayFile::parse(forgedReplay(identity(SimulationProfile::Faithful), uberGame), file, &error));
	CHECK(error.find("but the game runs uber") != std::string::npos);
	CHECK_FALSE(ReplayFile::parse(forgedReplay(identity(SimulationProfile::Uber), faithfulGame), file, &error));
	CHECK(error.find("but the game runs faithful") != std::string::npos);
	// consistent headers read (the forger is format 5 itself)
	REQUIRE_MESSAGE(ReplayFile::parse(forgedReplay(identity(SimulationProfile::Uber), uberGame), file, &error), error);
	CHECK(file.header.game.game.simulationProfile == SimulationProfile::Uber);
	REQUIRE_MESSAGE(ReplayFile::parse(forgedReplay(identity(SimulationProfile::Faithful), faithfulGame), file, &error), error);
	// playback: a header held in memory with an equal identity but another game profile is still a mismatch
	ReplayFile mixed;
	mixed.header.profile = identity(SimulationProfile::Faithful);
	mixed.header.game = uberGame;
	CHECK(mixed.profileMismatch(identity(SimulationProfile::Faithful)).find("the replay's game runs the simulation profile uber") != std::string::npos);
	mixed.header.game = faithfulGame;
	CHECK(mixed.profileMismatch(identity(SimulationProfile::Faithful)).empty());
}
