// OpenBFME. GPL-3.0. Opt-in deterministic ten-minute P16 acceptance run.
// Uses real startup, recording and command-log save/load. No simulation overrides.
#include "doctest.h"
#include "P16TestUtil.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Recorder.h"
#include "Common/System/SaveGame/SaveGameFile.h"
#include "GameClient/CommandLogLoad.h"
#include "GameNetwork/LockstepDriver.h"
#include "GameLogic/SkirmishAI/SkirmishAIManager.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>

namespace
{
constexpr int framesPerMinute = 5 * 60;
constexpr int duration = 10 * framesPerMinute;
constexpr int savedAt = 5 * framesPerMinute;
struct Run
{
	std::vector<std::uint32_t> hashes;
	std::vector<LockstepDriver::CensusRow> census;
	SaveGameFile save;
};

SimulationProfile selectedProfile()
{
	const char *value = std::getenv("OPENBFME_P16_PROFILE");
	if (!value || std::string(value) == "enhanced") return SimulationProfile::Faithful;
	REQUIRE_MESSAGE(std::string(value) == "uber", "OPENBFME_P16_PROFILE must be enhanced or uber");
	return SimulationProfile::Uber;
}

std::string prefix()
{
	const char *value = std::getenv("OPENBFME_P16_OUTPUT_PREFIX");
	return value ? value : "p16";
}

void writeEvidence(const Run &run, const std::string &name)
{
	REQUIRE(run.hashes.size() == run.census.size());
	std::ofstream out(prefix() + "-" + name + ".csv");
	REQUIRE(out.good());
	out << "frame,state_hash,logic_us,battalions,troops,objects\n";
	for (size_t i = 0; i < run.hashes.size(); ++i)
	{
		const auto &row = run.census[i];
		out << row.frame << ',' << run.hashes[i] << ',' << row.simUs << ',' << row.battalions << ',' << row.troops << ',' << row.objects << '\n';
	}
	out.flush();
	REQUIRE(out.good());
	// One-minute windows ending at the requested minute, not cumulative or wall-time windows.
	for (int minute : { 1, 5, 10 })
	{
		if ((int)run.census.size() < minute * framesPerMinute) continue;
		std::int64_t sum = 0, worst = 0;
		for (int i = (minute - 1) * framesPerMinute; i < minute * framesPerMinute; ++i)
		{
			REQUIRE(run.census[(size_t)i].simUs >= 0); // tolerance: elapsed clock durations may be zero but never negative
			sum += run.census[(size_t)i].simUs;
			worst = std::max(worst, run.census[(size_t)i].simUs);
		}
		std::printf("P16 %s minute=%d samples=%d logic_avg_ms=%.6f logic_worst_ms=%.6f\n", name.c_str(), minute, framesPerMinute,
			(double)sum / (framesPerMinute * 1000.0), (double)worst / 1000.0);
	}
}

Run record(const std::string &name)
{
	auto world = p16test::freshWorld();
	REQUIRE(world);
	REQUIRE_MESSAGE(world->world, world->error);
	auto context = world->world->enterContext();
	p16test::Game game;
	NewGameMessage message = p16test::message(*world);
	message.game.simulationProfile = selectedProfile();
	std::string error;
	REQUIRE_MESSAGE(game.load(*world, message, &error), error);
	LiveGame &live = *game.live;
	REQUIRE(live.frame() == 1);
	REQUIRE(live.logic().skirmishAI().infoPlayers().size() == 16);
	REQUIRE(live.logic().skirmishAI().farmSites().size() == 64);
	for (int i = 1; i <= 16; ++i)
	{
		const Player *p = live.players().findPlayerWithName("Player_" + std::to_string(i));
		REQUIRE(p);
		const auto *ai = live.logic().skirmishAI().findAI(p->getPlayerIndex());
		REQUIRE(ai);
		REQUIRE_FALSE(ai->disabled);
		REQUIRE_FALSE(ai->build.bases.empty());
		REQUIRE(ai->build.bases.front().tmpl);
	}
	Run run;
	ReplayWriter writer;
	writer.setKeepCopy(true);
	ReplayHeader header;
	header.game = message;
	header.profile = ProfileIdentity::compute(world->mount->identityArchives, RandomAlgorithm::ZH_CarryChain, {}, message.game.simulationProfile);
	std::ofstream manifest(prefix() + "-" + name + "-manifest.txt");
	REQUIRE(manifest.good());
	manifest << header.profile.text << "seed " << message.game.seed << "\nframes " << duration << "\n";
	for (const auto &archive : world->mount->archives) manifest << "verified-mount " << archive << '\n';
	manifest.flush();
	REQUIRE(manifest.good());
	std::ofstream activity(prefix() + "-" + name + "-ai.csv");
	REQUIRE(activity.good());
	activity << "minute,start,disabled,structures_executed,farms_built,unit_requests,units_out\n";
	REQUIRE_MESSAGE(writer.open("", header, &error), error);
	LockstepDriver driver(nullptr, &writer);
	driver.setCensus(true);
	driver.setOnCompleted([&](const FrameCompletion &frame) {
		REQUIRE(frame.hashed);
		REQUIRE(frame.frame == run.hashes.size() + 2);
		run.hashes.push_back(frame.hash);
	});
	RegisterLogicCRCHandler(live.dispatch());
	live.setFrameDriver(&driver);
	for (int i = 1; i <= duration; ++i)
	{
		REQUIRE(live.advance(0.2) == 1);
		if (i % framesPerMinute == 0)
		{
			for (int start = 1; start <= 16; ++start)
			{
				const Player *player = live.players().findPlayerWithName("Player_" + std::to_string(start));
				REQUIRE(player);
				const auto *ai = live.logic().skirmishAI().findAI(player->getPlayerIndex());
				REQUIRE(ai);
				activity << i / framesPerMinute << ',' << start << ',' << ai->disabled << ',' << ai->build.executed << ','
					<< ai->build.economy.built << ',' << ai->build.units.made << ',' << ai->build.units.queued << '\n';
			}
			activity.flush();
			REQUIRE(activity.good());
			std::printf("P16 %s minute=%d frame=%u\n", name.c_str(), i / framesPerMinute, live.frame());
			std::fflush(stdout);
		}
		if (i == savedAt)
		{
			live.drainFrames();
			auto &sh = run.save.header;
			sh.buildVersion = "p16-test";
			sh.engineId = ProfileIdentity::buildId();
			sh.profile = header.profile;
			sh.executionProfile = SaveGameFile::executionProfileOf(header.profile.text);
			sh.gameKind = 2;
			sh.frame = live.frame();
			sh.stateHash = live.logic().computeStateHash();
			sh.description = "P16 minute five";
			sh.mapPath = message.game.mapName;
			REQUIRE(sh.frame == savedAt + 1);
			REQUIRE_MESSAGE(writer.logUpTo(sh.frame, sh.stateHash, run.save.log, &error), error);
		}
	}
	live.drainFrames();
	writer.close(live.frame(), live.logic().computeStateHash());
	REQUIRE(writer.errors().empty());
	REQUIRE(driver.errors().empty());
	run.census = driver.censusLog();
	live.setFrameDriver(nullptr);
	REQUIRE(run.hashes.size() == duration);
	std::ofstream diagnostics(prefix() + "-" + name + "-diagnostics.txt");
	REQUIRE(diagnostics.good());
	const auto report = live.report();
	for (const auto &line : report.errors) diagnostics << "error " << line << '\n';
	for (const auto &line : live.logic().skirmishAI().report()) diagnostics << "ai " << line << '\n';
	diagnostics.flush();
	REQUIRE(diagnostics.good());
	REQUIRE_MESSAGE(report.errors.empty(), "P16 runtime errors: see diagnostics artifact");
	writeEvidence(run, name);
	return run;
}
} // namespace

TEST_CASE("p16 soak: sixteen AI ten minutes twice and minute-five save continuation" * doctest::skip())
{
	// Unlike optional retail unit tests, explicitly requesting the acceptance soak requires the data.
	REQUIRE_MESSAGE(retailtest::pureMount(), "Set ROTWK_INSTALL and BFME2_INSTALL for the P16 acceptance soak");
	const Run first = record("run1");
	const Run second = record("run2");
	REQUIRE(first.hashes == second.hashes);
	std::string error;
	SaveGameFile save;
	REQUIRE_MESSAGE(SaveGameFile::parse(first.save.encode(), save, &error), error);
	CommandLogLoad loader;
	REQUIRE_MESSAGE(loader.prepare(save, &error), error);
	REQUIRE(loader.log().header.game.game.mapStartCount == 16);
	auto world = p16test::freshWorld();
	REQUIRE(world);
	REQUIRE_MESSAGE(world->world, world->error);
	auto context = world->world->enterContext();
	p16test::Game game;
	REQUIRE_MESSAGE(game.load(*world, loader.log().header.game, &error), error);
	LiveGame &live = *game.live;
	REQUIRE_MESSAGE(loader.begin(live, &error), error);
	while (loader.step(live, 100) == CommandLogLoad::State::CatchingUp) {}
	REQUIRE_MESSAGE(loader.state() == CommandLogLoad::State::Ready, loader.failure());
	REQUIRE(loader.hashesCompared() == savedAt);
	REQUIRE(live.logic().computeStateHash() == first.hashes[savedAt - 1]);
	ReplayWriter writer;
	writer.setKeepCopy(true);
	REQUIRE_MESSAGE(loader.handOver(live, writer, "", selectedProfile(), &error), error);
	Run resumed;
	LockstepDriver driver(nullptr, &writer);
	driver.setCensus(true);
	driver.setOnCompleted([&](const FrameCompletion &frame) {
		REQUIRE(frame.hashed);
		REQUIRE(frame.frame == savedAt + resumed.hashes.size() + 2);
		REQUIRE(frame.hash == first.hashes[(size_t)frame.frame - 2]);
		resumed.hashes.push_back(frame.hash);
	});
	live.setFrameDriver(&driver);
	for (int i = savedAt; i < duration; ++i) REQUIRE(live.advance(0.2) == 1);
	live.drainFrames();
	writer.close(live.frame(), live.logic().computeStateHash());
	REQUIRE(writer.errors().empty());
	REQUIRE(driver.errors().empty());
	resumed.census = driver.censusLog();
	live.setFrameDriver(nullptr);
	REQUIRE(resumed.hashes.size() == duration - savedAt);
	// Resumed CSV keeps absolute frame numbers; do not label its relative windows as minutes 1/5/10.
	std::ofstream out(prefix() + "-resume.csv");
	REQUIRE(out.good());
	out << "frame,state_hash\n";
	for (size_t i = 0; i < resumed.hashes.size(); ++i) out << savedAt + i + 2 << ',' << resumed.hashes[i] << '\n';
	out.flush();
	REQUIRE(out.good());
}
