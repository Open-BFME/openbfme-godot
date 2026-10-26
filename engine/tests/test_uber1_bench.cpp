// OpenBFME unit tests. GPL-3.0.
// Lane UBER-1: the Uber performance mode's benchmark (docs/UBER.md). A measurement, skipped by default:
//
//   openbfme_tests -tc="uber bench*" --no-skip
//
// OPENBFME_UBER_UNITS (a comma list, default 10000,20000,40000) armies of at least that many living troops (GameClient/FrameCensus.h: horde members
// count one by one, the horde controllers not), OPENBFME_BENCH_FRAMES logic frames each (default 50),
// OPENBFME_UBER_WARMUP warm-up frames after the order, not measured (default 50 = 10 game seconds: the armies are in contact when the measured frames
// start), OPENBFME_UBER_PROFILE = faithful | uber | both (default both), OPENBFME_LOGIC_THREADS the logic pool's size, OPENBFME_UBER_FRAMES_DUMP=1 every
// measured frame's logic time, phases and hash. Prints per battle: the living troops
// (at the start, the sustained mean over the frames, the minimum, at the end), the TICK (logic + the state hash every recorded game computes) and
// whether it fits the 200 ms of the 5 fps logic clock, the logic frame's wall time (mean / p99 / max) and its six phases, the state hash's time and
// its own job dispatches, the cores used (process CPU / wall). SKIP without ROTWK_INSTALL / BFME2_INSTALL. The client side (render fps, 1% low,
// worst frame) is the game's gate: game.gd --auto --uber1=... (scripts/uber1_player.gd).
//
// The battle: 8 players on map mp fall back 8p (start positions 0..7, players 1-4 FactionMen on team 0, 5-8 FactionMordor on team 1), human slots (no
// computer player acts), seed 7. The armies are rows of hordes, Gondor's west and Mordor's east of the middle of the map, facing each other; two
// settling frames, then every horde attacks the enemy horde facing it (the PERF-1 big battle's orders on a larger scale).

#include "doctest.h"

#include "PerfBenchUtil.h"
#include "StartTestUtil.h"
#include "UberScenarios.h"

#include "Common/JobSystem.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "GameClient/FrameCensus.h"
#include "GameClient/GUI/Skirmish/IniSkirmishSetupSource.h"
#include "GameClient/LiveGame.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Map/TerrainLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/NewGame/NewGame.h"
#include "GameLogic/Object/Object.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>
#ifndef _WIN32
#include <time.h>
#endif

namespace
{
double wallMs()
{
	return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// the process's CPU time (every thread) in ms; -1 where it is not available
double processCpuMs()
{
#ifndef _WIN32
	timespec ts{};
	clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts);
	return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1.0e6;
#else
	return -1.0;
#endif
}

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

size_t countObjects(GameLogic &logic)
{
	size_t n = 0;
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		++n;
	}
	return n;
}

std::vector<int> unitCounts()
{
	std::vector<int> out;
	const char *env = std::getenv("OPENBFME_UBER_UNITS");
	std::stringstream in(env && *env ? env : "10000,20000,40000");
	std::string item;
	while (std::getline(in, item, ','))
	{
		if (!item.empty())
		{
			out.push_back(std::atoi(item.c_str()));
		}
	}
	return out;
}
} // namespace

namespace uberbench
{
void runBattle(ArchiveFileSystem &fs, RetailObjectWorld &world, const MapObjectOptions &options, const GameLogicSettings &settings, int units, int frames,
	SimulationProfile profile, bool hashEveryFrame, Run &out, int warmUp)
{
	auto scope = world.enterContext();
	std::vector<MapCacheEntry> cache;
	std::string error;
	REQUIRE_MESSAGE(IniSkirmishSetupSource::loadMapCache(fs, cache, &error), error);
	NewGameMessage m;
	m.game.mapName = "maps/map mp fall back 8p/map mp fall back 8p.map";
	m.game.seed = 7;
	m.game.startingCash = 1500;
	m.game.simulationProfile = profile;
	for (int i = 0; i < 8; ++i)
	{
		SkirmishGameSlot &slot = m.game.slots[i];
		slot.state = SLOT_PLAYER;
		slot.name = std::u16string(u"Player") + (char16_t)(u'0' + i);
		slot.playerTemplate = templateIndex(world.playerTemplates(), i < 4 ? "FactionMen" : "FactionMordor");
		REQUIRE(slot.playerTemplate >= 0);
		slot.startPos = i;
		slot.color = i;
		slot.teamNumber = i < 4 ? 0 : 1;
	}
	NewGameStart start(RandomAlgorithm::ZH_CarryChain);
	REQUIRE_MESSAGE(NewGame::prepareNewGame(m, world.playerTemplates(), settings, cache, RandomAlgorithm::ZH_CarryChain, start, &error), error);
	ArchiveW3DFileSource source(fs);
	WW3DAssetManager assets(source);
	LiveGame game(world, fs, assets, options);
	LiveGame::Options o;
	o.start = &start;
	REQUIRE_MESSAGE(game.load(o, &error), error);
	GameLogic &logic = game.logic();
	REQUIRE(logic.simulationProfile() == profile);
	out.objectsBefore = countObjects(logic);

	float maxX = 0.0f, maxY = 0.0f;
	REQUIRE(logic.terrain() != nullptr);
	REQUIRE(logic.terrain()->getExtent(0, maxX, maxY));
	const float cx = maxX * 0.5f, cy = maxY * 0.5f;
	const float spacing = 75.0f, gap = 160.0f, margin = 200.0f;
	const int rows = std::max(1, (int)((maxY - 2.0f * margin) / spacing));
	std::vector<Player *> players;
	for (int i = 0; i < 8; ++i)
	{
		Player *p = game.players().findPlayerWithName("Player_" + std::to_string(i + 1));
		REQUIRE(p != nullptr);
		players.push_back(p);
	}
	const char *gondor[] = { "GondorFighterHorde", "GondorFighterHorde", "GondorTowerShieldGuardHorde", "GondorArcherHorde" };
	const char *mordor[] = { "MordorFighterHorde", "MordorFighterHorde", "MordorFighterHorde", "MordorArcherHorde" };
	std::vector<Object *> west, east; // by row-major slot: west[k] faces east[k]
	const double spawnStart = wallMs();
	int troops = 0;
	for (int column = 0; troops < units; ++column)
	{
		const float dx = gap + spacing * (float)column;
		REQUIRE_MESSAGE(cx - dx > margin, "the armies do not fit the map at " << units << " units (column " << column << ")");
		for (int r = 0; r < rows && troops < units; ++r)
		{
			const float y = margin + spacing * (float)r + (float)(column % 2) * spacing * 0.5f;
			const int k = column * rows + r;
			// each column's rows belong to the four players of the team in turn
			Player *pw = players[(size_t)(r % 4)], *pe = players[(size_t)(4 + r % 4)];
			Object *a = game.createObject(gondor[(r + column) % 4], pw->getPlayerIndex(), Coord3D{ cx - dx, y, 0.0f }, 0.0f, &error);
			REQUIRE_MESSAGE(a != nullptr, error);
			Object *b = game.createObject(mordor[(r + column) % 4], pe->getPlayerIndex(), Coord3D{ cx + dx, y, 0.0f }, 3.14159265f, &error);
			REQUIRE_MESSAGE(b != nullptr, error);
			west.push_back(a);
			east.push_back(b);
			(void)k;
			if ((west.size() % 16) == 0)
			{
				troops = FrameCensus::count(logic).troops;
			}
		}
		troops = FrameCensus::count(logic).troops;
	}
	out.units = countObjects(logic) - out.objectsBefore;
	logic.runLogicFrame();
	logic.runLogicFrame();
	for (size_t i = 0; i < west.size(); ++i)
	{
		if (west[i]->isDestroyed() || east[i]->isDestroyed())
		{
			continue;
		}
		if (AIUpdateInterface *ai = west[i]->getAIUpdateInterface())
		{
			ai->aiAttackObject(east[i], CMD_FROM_PLAYER);
		}
		if (AIUpdateInterface *ai = east[i]->getAIUpdateInterface())
		{
			ai->aiAttackObject(west[i], CMD_FROM_PLAYER);
		}
	}
	out.spawnMs = wallMs() - spawnStart;
	out.troopsMade = FrameCensus::count(logic).troops;
	for (int f = 0; f < warmUp; ++f)
	{
		logic.runLogicFrame();
		if (hashEveryFrame)
		{
			(void)logic.computeStateHash();
		}
	}
	out.objectsStart = countObjects(logic);
	{
		const FrameCensus::Counts c = FrameCensus::count(logic);
		out.troopsStart = out.troopsMin = c.troops;
		out.battalionsStart = c.battalions;
	}
	double troopsSum = 0.0;
	// OPENBFME_SAMPLER_FLAG=<file>: the file exists while the measured frames run (a sampling profiler records only then, not the warm-up)
	const char *samplerFlag = std::getenv("OPENBFME_SAMPLER_FLAG");
	if (samplerFlag)
	{
		if (FILE *flag = std::fopen(samplerFlag, "w"))
		{
			std::fclose(flag);
		}
	}
	const double cpu0 = processCpuMs(), wall0 = wallMs();
	for (int f = 0; f < frames; ++f)
	{
		const double t0 = wallMs(), c0 = perfbench::nowMs();
		PhaseTimes ph;
		for (int phase = 1; phase <= 6; ++phase)
		{
			const double p0 = wallMs();
			logic.update(phase);
			ph.ms[phase] = wallMs() - p0;
		}
		out.wallMs.push_back(wallMs() - t0);
		out.cpuMs.push_back(perfbench::nowMs() - c0);
		out.phases.push_back(ph);
		double tick = out.wallMs.back();
		if (hashEveryFrame)
		{
			const JobSystem::Stats s0 = JobSystem::logic().stats();
			const double h0 = wallMs();
			out.hashes.push_back(logic.computeStateHash());
			out.hashMs.push_back(wallMs() - h0);
			const JobSystem::Stats s1 = JobSystem::logic().stats();
			out.hashDispatches += s1.dispatches - s0.dispatches;
			out.hashWorkerChunks += s1.workerChunks - s0.workerChunks;
			tick += out.hashMs.back();
		}
		out.tickMs.push_back(tick);
		const int alive = FrameCensus::count(logic).troops; // between frames, outside the timing
		out.troopsMin = std::min(out.troopsMin, alive);
		out.troopsEnd = alive;
		troopsSum += (double)alive;
	}
	if (samplerFlag)
	{
		std::remove(samplerFlag);
	}
	out.troopsMean = frames > 0 ? troopsSum / (double)frames : 0.0;
	out.framesWallMs = wallMs() - wall0;
	out.processCpuMs = cpu0 >= 0.0 ? processCpuMs() - cpu0 : -1.0;
	out.objectsEnd = countObjects(logic);
}
} // namespace uberbench

TEST_CASE("uber bench: 10k / 20k / 40k unit battles on map mp fall back 8p, faithful and Uber profiles - logic frame wall time, phases, cores used" *
		  doctest::skip())
{
	OPENBFME_REQUIRE_START(s);
	const int frames = perfbench::envInt("OPENBFME_BENCH_FRAMES", 50);
	const int warmUp = perfbench::envInt("OPENBFME_UBER_WARMUP", 50); // 10 game seconds: the armies meet, the measured frames are the battle
	const char *which = std::getenv("OPENBFME_UBER_PROFILE");
	const std::string profiles = which && *which ? which : "both";
	std::vector<SimulationProfile> run;
	if (profiles == "faithful" || profiles == "both")
	{
		run.push_back(SimulationProfile::Faithful);
	}
	if (profiles == "uber" || profiles == "both")
	{
		run.push_back(SimulationProfile::Uber);
	}
	REQUIRE(!run.empty());
	for (int units : unitCounts())
	{
		for (SimulationProfile profile : run)
		{
			uberbench::Run r;
			uberbench::runBattle(*s->mount->fs, *s->world, s->options, s->settings, units, frames, profile, true, r, warmUp);
			const perfbench::FrameStats w = perfbench::stats(r.wallMs), c = perfbench::stats(r.cpuMs), h = perfbench::stats(r.hashMs), t = perfbench::stats(r.tickMs);
			double phase[7] = { 0, 0, 0, 0, 0, 0, 0 };
			for (const uberbench::PhaseTimes &p : r.phases)
			{
				for (int i = 1; i <= 6; ++i)
				{
					phase[i] += p.ms[i] / (double)r.phases.size();
				}
			}
			int over = 0;
			for (double ms : r.tickMs)
			{
				over += ms > 200.0 ? 1 : 0; // the whole tick (logic + the recorded game's state hash) against the 200 ms of the 5 fps logic clock
			}
			std::printf("  info: uber bench %s %d troops: %d made, %d warm-up frames, then living troops %d at the start (%d hordes), sustained mean %.0f, min %d, %d at the end; %zu objects "
						"made (map objects %zu; spawn %.0f ms); %d logic threads, %zu frames: TICK (logic + hash) mean %.2f ms p99 %.2f max %.2f; logic wall "
						"mean %.2f ms p99 %.2f max %.2f (frame %d); thread CPU mean %.2f ms; phases 1..6 mean %.2f / %.2f / %.2f / %.2f / %.2f / %.2f ms; state "
						"hash mean %.2f ms max %.2f (%llu dispatches, %llu chunks on workers); cores used %.2f; ticks over 200 ms %d of %zu; hash chain %08x\n",
				simulationProfileName(profile), units, r.troopsMade, warmUp, r.troopsStart, r.battalionsStart, r.troopsMean, r.troopsMin, r.troopsEnd, r.units, r.objectsBefore,
				r.spawnMs, JobSystem::logic().threadCount(), r.wallMs.size(), t.mean, t.p99, t.max, w.mean, w.p99, w.max, w.worstFrame, c.mean, phase[1],
				phase[2], phase[3], phase[4], phase[5], phase[6], h.mean, h.max, (unsigned long long)r.hashDispatches, (unsigned long long)r.hashWorkerChunks,
				r.processCpuMs >= 0.0 && r.framesWallMs > 0.0 ? r.processCpuMs / r.framesWallMs : -1.0, over, r.tickMs.size(),
				(unsigned)perfbench::chain(r.hashes));
			// OPENBFME_UBER_FRAMES_DUMP=1: every measured frame's logic wall time, its six phases and the state hash (the spikes behind the p99)
			if (std::getenv("OPENBFME_UBER_FRAMES_DUMP"))
			{
				for (size_t f = 0; f < r.wallMs.size(); ++f)
				{
					const uberbench::PhaseTimes &p = f < r.phases.size() ? r.phases[f] : uberbench::PhaseTimes();
					std::printf("    frame %zu: logic %.1f ms (phases %.1f / %.1f / %.1f / %.1f / %.1f / %.1f) hash %.1f ms\n", f, r.wallMs[f], p.ms[1], p.ms[2],
						p.ms[3], p.ms[4], p.ms[5], p.ms[6], f < r.hashMs.size() ? r.hashMs[f] : 0.0);
				}
			}
			std::fflush(stdout);
			CHECK(r.troopsMade >= units);
		}
	}
}
