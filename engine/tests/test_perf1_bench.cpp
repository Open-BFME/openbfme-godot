// OpenBFME unit tests. GPL-3.0.
// Lane PERF-1: the performance benchmarks of the simulation and the proof that the optimisations keep it identical. The benchmarks are measurements
// (skipped by default; run them with --no-skip):
//
//   openbfme_tests -tc="perf1 bench*" --no-skip
//
//   * "perf1 bench: big battle": MODULES-2's partition bench scenario (about 1080 objects: Gondor / Mordor hordes, heroes, attack trolls fighting on
//     map mp fall back 4p), every frame's state hash and the logic time per frame (OPENBFME_BENCH_FRAMES, default 400);
//   * "perf1 bench: 4-player skirmish": four medium computer players (Men, Mordor, Elves, Isengard, free for all) on map mp fall back 4p for
//     OPENBFME_BENCH_FRAMES frames (default 6000 = 20 game minutes at 5 logic frames per second);
//   * "perf1 bench: allied blob": the big battle's 24 Gondor hordes with no enemy, all sent to one point, for OPENBFME_BENCH_FRAMES frames (default
//     300): the soldiers overlap, so the contact pairs grow with the square of the crowd; it also prints the pairs per frame.
//
// Each prints the logic time (thread CPU: mean / p95 / p99 / max), the user instructions per frame and the chain of every frame's state hash; equal
// chains before and after a change prove the change kept every frame's state. OPENBFME_BENCH_HASHES=<prefix> writes the per-frame hashes to
// <prefix>.<scenario>.txt. SKIP without ROTWK_INSTALL / BFME2_INSTALL.

#include "doctest.h"

#include "HudTestUtil.h"
#include "PerfBenchUtil.h"
#include "PerfScenarios.h"
#include "StartTestUtil.h"

#include "Common/AsciiString.h"
#include "Common/NameKeyGenerator.h"
#include "Common/Player.h"
#include "Common/Thing/KindOfTokens.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "GameClient/GUI/Skirmish/IniSkirmishSetupSource.h"
#include "GameClient/LiveGame.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/PhysicsBehavior.h"
#include "GameLogic/Module/SlavedUpdate.h"
#include "GameLogic/NewGame/NewGame.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SkirmishAI/SkirmishAIData.h"
#include "GameLogic/SkirmishAI/SkirmishAIManager.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"

#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <cstdio>
#include <string>
#include <vector>

using namespace hudtest;

extern const char *const TheKindOfNames[];

TEST_CASE("perf1: KindOfTokens::indexOf (sorted lookup) returns what the scan of RW 0xDA0E68 in table order returns, for every name in any case")
{
	// the scan the lookup replaced: the first name equal under AsciiStringUtil::compareNoCase
	auto scan = [](const std::string &name) {
		for (int i = 0; TheKindOfNames[i]; ++i)
		{
			if (AsciiStringUtil::compareNoCase(name, TheKindOfNames[i]) == 0)
			{
				return i;
			}
		}
		return -1;
	};
	int names = 0;
	for (int i = 0; TheKindOfNames[i]; ++i, ++names)
	{
		const std::string upper = TheKindOfNames[i];
		std::string lower = upper, mixed = upper;
		AsciiStringUtil::toLower(lower);
		for (size_t k = 0; k < mixed.size(); k += 2)
		{
			mixed[k] = lower[k];
		}
		for (const std::string &n : { upper, lower, mixed, upper + "X", upper.substr(0, upper.size() - 1), "+" + upper })
		{
			INFO(n);
			CHECK(KindOfTokens::indexOf(n) == scan(n));
		}
	}
	CHECK(names > 200);
	for (const char *n : { "", "NONE", "horde", "Horde_", "ZZZZ", "\xff", "A", "_" })
	{
		INFO(n);
		CHECK(KindOfTokens::indexOf(n) == scan(n));
	}
}

namespace perf1
{
// the partition bench's battle (test_partition_bench.cpp): 24 Gondor and 24 Mordor hordes in rows facing each other around the middle of the map's
// objects, Aragorn and Boromir, four attack trolls; two settling frames, then every unit attacks the enemy facing it
// the mean position of the map's objects
void mapCentre(GameLogic &logic, float &cx, float &cy)
{
	float sx = 0.0f, sy = 0.0f;
	int n = 0;
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		sx += o->getPosition()->x;
		sy += o->getPosition()->y;
		++n;
	}
	REQUIRE(n > 0);
	cx = sx / (float)n;
	cy = sy / (float)n;
}

void setUpBigBattle(Rig &rig)
{
	GameLogic &logic = rig.logic();
	Player *enemy = rig.game->players().findPlayerWithName("Player_2");
	REQUIRE(enemy);
	float cx = 0.0f, cy = 0.0f;
	mapCentre(logic, cx, cy);
	const char *ours[] = { "GondorFighterHorde", "GondorFighterHorde", "GondorTowerShieldGuardHorde", "GondorArcherHorde" };
	const char *theirs[] = { "MordorFighterHorde", "MordorFighterHorde", "MordorFighterHorde", "MordorArcherHorde" };
	std::vector<Object *> good, evil;
	const int rows = 4, cols = 6;
	for (int r = 0; r < rows; ++r)
	{
		for (int c = 0; c < cols; ++c)
		{
			const float y = cy + ((float)c - (cols - 1) * 0.5f) * 90.0f;
			good.push_back(rig.make(ours[(r + c) % 4], cx - 260.0f - 90.0f * (float)r, y));
			evil.push_back(rig.make(theirs[(r + c) % 4], cx + 260.0f + 90.0f * (float)r, y, enemy));
		}
	}
	good.push_back(rig.make("GondorAragorn", cx - 200.0f, cy));
	good.push_back(rig.make("GondorBoromir", cx - 200.0f, cy + 60.0f));
	for (int i = 0; i < 4; ++i)
	{
		evil.push_back(rig.make("MordorAttackTroll", cx + 200.0f, cy + ((float)i - 1.5f) * 120.0f, enemy));
	}
	logic.runLogicFrame();
	logic.runLogicFrame();
	for (size_t i = 0; i < good.size(); ++i)
	{
		if (AIUpdateInterface *ai = good[i]->getAIUpdateInterface())
		{
			ai->aiAttackObject(evil[i % evil.size()], CMD_FROM_PLAYER);
		}
	}
	for (size_t i = 0; i < evil.size(); ++i)
	{
		if (AIUpdateInterface *ai = evil[i]->getAIUpdateInterface())
		{
			ai->aiAttackObject(good[i % good.size()], CMD_FROM_PLAYER);
		}
	}
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

void report(const char *scenario, const std::vector<double> &ms, long long instructions, const std::vector<std::uint32_t> &hashes, size_t objectsStart,
	size_t objectsEnd)
{
	const perfbench::FrameStats s = perfbench::stats(ms);
	std::printf("  info: perf1 %s: %zu frames, %zu objects at the start, %zu at the end; logic (thread CPU) mean %.3f ms, p95 %.3f ms, p99 %.3f ms, max %.3f ms "
	            "(frame %d); %.2f M user instructions per frame; final state hash %08x, hash chain %08x\n",
		scenario, ms.size(), objectsStart, objectsEnd, s.mean, s.p95, s.p99, s.max, s.worstFrame, instructions >= 0 ? (double)instructions / (double)ms.size() / 1.0e6 : -1.0,
		hashes.empty() ? 0u : (unsigned)hashes.back(), (unsigned)perfbench::chain(hashes));
	perfbench::writeHashes(scenario, hashes);
}

// the big battle's 24 Gondor hordes on their own (no enemy), two settling frames, then every horde ordered to the same point: the armies pile up there
// and the allied soldiers overlap (S-781), so the contact pairs grow with the square of the crowd
void setUpAlliedBlob(Rig &rig, std::vector<Object *> &hordes)
{
	GameLogic &logic = rig.logic();
	float cx = 0.0f, cy = 0.0f;
	mapCentre(logic, cx, cy);
	const char *ours[] = { "GondorFighterHorde", "GondorFighterHorde", "GondorTowerShieldGuardHorde", "GondorArcherHorde" };
	const int rows = 4, cols = 6;
	for (int r = 0; r < rows; ++r)
	{
		for (int c = 0; c < cols; ++c)
		{
			const float y = cy + ((float)c - (cols - 1) * 0.5f) * 90.0f;
			hordes.push_back(rig.make(ours[(r + c) % 4], cx - 260.0f - 90.0f * (float)r, y));
		}
	}
	logic.runLogicFrame();
	logic.runLogicFrame();
	const Coord3D goal{ cx - 100.0f, cy, 0.0f };
	for (Object *h : hordes)
	{
		REQUIRE(h->getAIUpdateInterface());
		h->getAIUpdateInterface()->aiMoveToPosition(goal, CMD_FROM_PLAYER);
	}
}

// the allied blob on world `sh` for `frames` logic frames; `pairs` (if set) gets each frame's contact pair count
void runAlliedBlob(SharedWorld &sh, int frames, Run &out, std::vector<unsigned long long> *pairs = nullptr)
{
	auto scope = sh.world->enterContext();
	Rig rig(sh);
	GameLogic &logic = rig.logic();
	std::vector<Object *> hordes;
	setUpAlliedBlob(rig, hordes);
	out.objectsStart = countObjects(logic);
	out.ms.reserve((size_t)frames);
	out.hashes.reserve((size_t)frames);
	perfbench::InstructionCounter counter;
	long long instructions = 0;
	for (int f = 0; f < frames; ++f)
	{
		const long long i0 = counter.read();
		const double t0 = perfbench::nowMs();
		logic.runLogicFrame();
		out.ms.push_back(perfbench::nowMs() - t0);
		instructions += counter.read() - i0;
		out.hashes.push_back(logic.computeStateHash());
		if (pairs)
		{
			pairs->push_back(rig.game->ai().collisionPairsLastFrame());
		}
	}
	out.instructions = counter.fd >= 0 ? instructions : -1;
	out.objectsEnd = countObjects(logic);
}

int playerTemplateIndex(const PlayerTemplateStore &store, const std::string &name)
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
} // namespace perf1

namespace perf1
{
// the big battle on world `sh` for `frames` logic frames
void runBigBattle(SharedWorld &sh, int frames, Run &out)
{
	auto scope = sh.world->enterContext();
	Rig rig(sh);
	GameLogic &logic = rig.logic();
	setUpBigBattle(rig);
	out.objectsStart = countObjects(logic);
	out.ms.reserve((size_t)frames);
	out.hashes.reserve((size_t)frames);
	perfbench::InstructionCounter counter;
	long long instructions = 0;
	for (int f = 0; f < frames; ++f)
	{
		const long long i0 = counter.read();
		const double t0 = perfbench::nowMs();
		logic.runLogicFrame();
		out.ms.push_back(perfbench::nowMs() - t0);
		instructions += counter.read() - i0;
		out.hashes.push_back(logic.computeStateHash());
	}
	out.instructions = counter.fd >= 0 ? instructions : -1;
	out.objectsEnd = countObjects(logic);
}

// OPENBFME_BENCH_BREAKDOWN=<n>: after the n-th frame of the skirmish, the state hash by section and by object (diff two runs to find what diverged)
void printBreakdown(const GameLogic &logic)
{
	std::vector<GameLogic::StateHashSection> sections;
	std::vector<GameLogic::ObjectStateHash> objects;
	const std::uint32_t total = logic.computeStateHashBreakdown(sections, &objects);
	std::printf("  breakdown frame %u total %08x\n", (unsigned)logic.getFrame(), (unsigned)total);
	for (const GameLogic::StateHashSection &sec : sections)
	{
		std::printf("  section %-32s %08x\n", sec.name.c_str(), (unsigned)sec.value);
	}
	for (const GameLogic::ObjectStateHash &o : objects)
	{
		std::printf("  object %u %-40s %08x\n", (unsigned)o.id, o.templateName.c_str(), (unsigned)o.value);
	}
}

// a skirmish of medium computer players (`factions`, start positions 0.., free for all) on map mp fall back 4p (seed 7) for `frames` logic frames
void runAISkirmish(ArchiveFileSystem &fs, RetailObjectWorld &world, const MapObjectOptions &options, const GameLogicSettings &settings,
	const std::vector<std::string> &factions, int frames, Run &out)
{
	auto scope = world.enterContext();
	std::vector<MapCacheEntry> cache;
	std::string error;
	REQUIRE_MESSAGE(IniSkirmishSetupSource::loadMapCache(fs, cache, &error), error);
	NewGameMessage m;
	m.game.mapName = "maps/map mp fall back 4p/map mp fall back 4p.map";
	m.game.seed = 7;
	m.game.startingCash = 1500;
	for (size_t i = 0; i < factions.size(); ++i)
	{
		SkirmishGameSlot &slot = m.game.slots[i];
		slot.state = SLOT_MED_AI;
		slot.name = std::u16string(u"Player") + (char16_t)(u'0' + (int)i);
		slot.playerTemplate = playerTemplateIndex(world.playerTemplates(), factions[i]);
		REQUIRE(slot.playerTemplate >= 0);
		slot.startPos = (int)i;
		slot.color = (int)i;
		slot.teamNumber = (int)i;
	}
	NewGameStart start(RandomAlgorithm::ZH_CarryChain);
	REQUIRE_MESSAGE(NewGame::prepareNewGame(m, world.playerTemplates(), settings, cache, RandomAlgorithm::ZH_CarryChain, start, &error), error);
	ArchiveW3DFileSource source(fs);
	WW3DAssetManager assets(source);
	LiveGame game(world, fs, assets, options);
	LiveGame::Options o;
	o.start = &start;
	const auto loadStart = std::chrono::steady_clock::now();
	REQUIRE_MESSAGE(game.load(o, &error), error);
	out.loadMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - loadStart).count();
	GameLogic &logic = game.logic();
	out.objectsStart = out.peakObjects = countObjects(logic);
	out.ms.reserve((size_t)frames);
	out.hashes.reserve((size_t)frames);
	perfbench::InstructionCounter counter;
	long long instructions = 0;
	for (int f = 0; f < frames; ++f)
	{
		const unsigned before = logic.getFrame();
		const long long i0 = counter.read();
		const double t0 = perfbench::nowMs();
		const int ran = game.advance(0.2);
		const double t = perfbench::nowMs() - t0;
		instructions += counter.read() - i0;
		REQUIRE(ran == 1);
		REQUIRE(logic.getFrame() == before + 1u);
		out.ms.push_back(t);
		out.hashes.push_back(logic.computeStateHash());
		if (const char *at = std::getenv("OPENBFME_BENCH_BREAKDOWN"); at && std::atoi(at) == f + 1)
		{
			printBreakdown(logic);
		}
		if ((f % 100) == 0)
		{
			out.peakObjects = std::max(out.peakObjects, countObjects(logic));
		}
	}
	out.instructions = counter.fd >= 0 ? instructions : -1;
	out.objectsEnd = countObjects(logic);
}

const std::vector<std::string> &fourFactions()
{
	static const std::vector<std::string> f = { "FactionMen", "FactionMordor", "FactionElves", "FactionIsengard" };
	return f;
}

// a world loaded the way hudtest::shared() and starttest::shared() load theirs, owned by the caller: no game has run on it (its name keys, stores and
// caches are the load's)
bool loadFreshWorld(SharedWorld &w, GameLogicSettings &settings, std::string &error)
{
	w.mount = retailtest::pureMount();
	if (!w.mount || !w.mount->fs)
	{
		error = w.mount ? w.mount->error : "no retail mount";
		return false;
	}
	w.world = std::make_unique<RetailObjectWorld>(*w.mount->fs);
	if (!w.world->load(&error) || !MapObjectGameData::load(*w.mount->fs, w.options, &error) || !MapObjectGameData::loadPlayerTemplates(*w.mount->fs, w.options, &error) ||
		!MapCreationHooks::load(*w.mount->fs, w.options.creationScripts, &error) || !MouseSettings::load(*w.mount->fs, w.mouse, &error) ||
		!GameLogicSettingsLoader::load(*w.mount->fs, settings, &error))
	{
		return false;
	}
	w.metaEnv = std::make_unique<INIEnvironment>();
	w.metaEnv->fileSystem = w.mount->fs.get();
	w.meta.registerBlocks(*w.metaEnv);
	return w.meta.load(*w.metaEnv, "Data\\INI\\CommandMap.ini", &error) && w.meta.load(*w.metaEnv, "CommandMap.ini", &error);
}

// the stored per-frame hashes (tests/data/perf1/*.hashes: one hex value a line)
void storeHashes(const char *name, const std::vector<std::uint32_t> &hashes)
{
	const std::string path = std::string(OPENBFME_TEST_DATA_DIR) + "/perf1/" + name;
	FILE *f = std::fopen(path.c_str(), "w");
	REQUIRE_MESSAGE(f != nullptr, path);
	for (std::uint32_t h : hashes)
	{
		std::fprintf(f, "%08x\n", (unsigned)h);
	}
	std::fclose(f);
}

std::vector<std::uint32_t> readHashes(const char *name)
{
	std::vector<std::uint32_t> out;
	const std::string path = std::string(OPENBFME_TEST_DATA_DIR) + "/perf1/" + name;
	if (FILE *f = std::fopen(path.c_str(), "r"))
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

// the first frame whose hash differs from the reference (-1: none)
int firstDifference(const std::vector<std::uint32_t> &got, const std::vector<std::uint32_t> &want)
{
	for (size_t i = 0; i < got.size() || i < want.size(); ++i)
	{
		if (i >= got.size() || i >= want.size() || got[i] != want[i])
		{
			return (int)i;
		}
	}
	return -1;
}
} // namespace perf1

TEST_CASE("perf1 bench: big battle (the partition bench's armies) - logic time per frame and every frame's state hash" * doctest::skip())
{
	if (!haveWorld("perf1 bench: big battle"))
	{
		return;
	}
	perf1::Run run;
	perf1::runBigBattle(shared(), perfbench::envInt("OPENBFME_BENCH_FRAMES", 400), run);
	perf1::report("big battle", run.ms, run.instructions, run.hashes, run.objectsStart, run.objectsEnd);
	CHECK(run.objectsEnd < run.objectsStart); // the armies fought
}

TEST_CASE("perf1 bench: allied blob (24 hordes sent to one point) - logic time per frame, contact pairs and every frame's state hash" * doctest::skip())
{
	if (!haveWorld("perf1 bench: allied blob"))
	{
		return;
	}
	perf1::Run run;
	std::vector<unsigned long long> pairs;
	perf1::runAlliedBlob(shared(), perfbench::envInt("OPENBFME_BENCH_FRAMES", 300), run, &pairs);
	perf1::report("allied blob", run.ms, run.instructions, run.hashes, run.objectsStart, run.objectsEnd);
	unsigned long long peak = 0, total = 0;
	for (unsigned long long p : pairs)
	{
		peak = std::max(peak, p);
		total += p;
	}
	std::printf("  info: perf1 allied blob: contact pairs per frame mean %.0f, peak %llu, last %llu\n", pairs.empty() ? 0.0 : (double)total / (double)pairs.size(),
		peak, pairs.empty() ? 0ull : pairs.back());
	if (pairs.size() >= 2) // OPENBFME_BENCH_FRAMES=1 measures one frame: no growth to compare
	{
		CHECK(pairs.back() > pairs.front()); // tolerance: a measurement, not a port check; the hordes piled up (the counts move with any simulation change)
	}
}

TEST_CASE("perf1 bench: 4-player skirmish (medium computer players, 20 game minutes) - logic time per frame and every frame's state hash" * doctest::skip())
{
	OPENBFME_REQUIRE_START(s);
	perf1::Run run;
	perf1::runAISkirmish(*s->mount->fs, *s->world, s->options, s->settings, perf1::fourFactions(), perfbench::envInt("OPENBFME_BENCH_FRAMES", 6000), run);
	perf1::report("4-player skirmish", run.ms, run.instructions, run.hashes, run.objectsStart, run.objectsEnd);
	std::printf("  info: perf1 4-player skirmish: peak objects (sampled every 100 frames) %zu\n", run.peakObjects);
}

// The optimisations of lane PERF-1 (the KindOf lookup, the pathfind adapter's movement info, processCollisions' arrays, the animation lookup memo, the
// client-side ones) must not change a single frame. tests/data/perf1 holds every frame's state hash of both benchmark scenarios measured with the
// build WITHOUT them: archive-legacy-codebase 84035d1a (the last merge, MODULES-3 and END-1 included) plus the S-1080 fix of review r2 (the AI's unit
// name keys created at load), each scenario the first game on a freshly loaded world (`perf1 bench` in a fresh process, OPENBFME_BENCH_HASHES). A merge
// that changes the simulation re-measures them the same way (the merged tree with the PERF-1 source changes reverted, the key fix kept). Here both run on a world this test loads itself:
// the battle first, then a 2-player AI game (Mordor and Isengard: before the S-1080 fix it created their units' keys first and changed every later AI
// game), then the 4-player skirmish, which must hash as the first game of a fresh process did, frame for frame. A change that alters the
// simulation on purpose re-measures the files (and says why); an optimisation never does.
// Lane TARGET-1 re-measured both (OPENBFME_PERF1_REGEN=1 on its tree): RotWK's mood scan and target search change which enemy every unit picks and when
// it looks, and the new AI state and AIData rows are hashed. Community #73 (the formations centred on their hordes, RW 0x86C4EA) and #74 (the destination
// adjustment spiralling around the cell, RW 0x6FA2A4) re-measured both on the merged tree (OPENBFME_PERF1_REGEN=1); #73 alone differs from these from frame 0
// (the big battle) and frame 69 (the skirmish).
// Lane AIDATA-1 re-measured both (OPENBFME_PERF1_REGEN=1 on its tree): a horde running down a fleeing target sets RUNNING_DOWN_FROM_BEHIND on its members
// (RW 0x748650) with the goal's speed in the gap (RW 0x74AA69) and its riders are in melee range within ChaseFromBehindLimit (RW 0x6CC07C, 0 before); the
// battle's first changed frame is 36, the skirmish's 820. Lane IDLEMOTION-1 re-measured the skirmish (OPENBFME_PERF1_REGEN=1 on its tree): the crush state
// requests its point unadjusted and not as a final goal (RW 0x74F28D / RW 0x742425), so the riders' crush paths differ from frame 1184; the big battle is unchanged.
// Lane AIEXPAND-1 re-measured both (OPENBFME_PERF1_REGEN=1): the skirmish AI's state hash holds the EXPANSION heuristic's frame counter (both differ from
// frame 0) and the AIs take expansion targets and run SimpleExpansion (RW 0x97D035 / 0x97D191 / 0x90C50C).
// Merge queue q29 re-measured both once on the merged tree (OPENBFME_PERF1_REGEN=1, a CI host): AIEXPAND-1's frame counter in the skirmish AI's hash moves
// both from frame 0; before it, compared at merge points, the big battle reached its value at AIDATA-1's merge point (DEVPORT-SIM / KILLSRC-1's points differ
// from frame 23) and the skirmish still moved after it.
// Merge queue q30 re-measured both once on the merged tree (OPENBFME_PERF1_REGEN=1, a CI host), compared at each merge point: PORTERFOG-1 (an object out of
// the world keeps its shroud record, RW 0x68C18F / 0x68E31F) moves the skirmish from frame 8 and leaves the battle; #130 (allied clearing runs the move
// state's update and exit, RW 0x749497 -> 0x748E46 / 0x74949C -> 0x748D8A: MOVING while clearing) moves the battle from frame 103 and the skirmish again from frame 2506.
// Lane BFME2CAMP1 re-measured the skirmish on main's tree after q30 (OPENBFME_PERF1_REGEN=1, a CI host): a destroyed creep lair leaves its hole and is
// rebuilt (RebuildHoleExposeDie RW 0x889AAF, RebuildHoleBehavior RW 0x8868C2); the skirmish differs from frame 1566, the big battle is unchanged.
// Lane MODCOMPAT-AI re-measured the skirmish (OPENBFME_PERF1_REGEN=1, a CI host): from frame 23 the retail skirmish AI's SET_UNIT_REFERENCE (four calls at frame 25)
// writes the side's reference map (RW 0x7C2AF6 -> RW 0x608397), which is hashed, and completed upgrades enter the script engine's lists (RW 0x759A1C); the battle is unchanged.
// The capture release re-measured the skirmish (OPENBFME_PERF1_REGEN=1, Windows llvm-mingw): an idle capture ability clears CAPTURING on its capturer
// (RW 0x85266D), so the AI capture squads take their next order after a capture; it moves the skirmish from frame 717 and leaves the battle.
// Merge queue q32 re-measured both once on the merged tree (OPENBFME_PERF1_REGEN=1, a CI host), compared at each merge point: #137 (the rebuild holes),
// MODCOMPAT-AI (the skirmish AI's references and completed upgrades) and #156 (the capture release) each move the skirmish; #140, TEAMEVENTS-1 and #159 move
// neither; the big battle is unchanged.
// Merge queue q33 re-measured them once on the merged tree (OPENBFME_PERF1_REGEN=1, a CI host), compared at each merge point: #164 (the logic-frame
// caches) moves neither; MODCOMPAT-MOD1 (its eleven modules in the update vectors and the object hash, MODCOMPAT-MOD1's own vectors were measured on its
// older base) and #133 (the straight production exits) each move both; #166 (Uber only) moves neither.
// Lane MODCOMPAT-MOD1 re-measured them at GeometryUpgrade's commit (OPENBFME_PERF1_REGEN=1, a CI host): GeometryUpgrade (every object reads its own
// GeometryInfo: the pathfinder footprint, the combat circle and sphere, the partition cells, RW 0x68B244 / 0xAD2CE0; the structures' level shapes switch)
// moves the skirmish from frame 0.
// Lane MODCOMPAT-MOD1 re-measured them at RebuildHoleExposeDie's commit (OPENBFME_PERF1_REGEN=1, a CI host): RebuildHoleExposeDie / RebuildHoleBehavior
// (every object's mask bit, Object + 0x459 bit 1, and the dozers' rebuild byte, AI + 0x4D0, are hashed; a dead creep lair or Wild structure leaves its hole,
// RW 0x889AAF, which rebuilds it, RW 0x8868C2) moves the battle from frame 0 and the skirmish from frame 0.
// Lane MODCOMPAT-MOD1 re-measured them at SlaveWatcherBehavior's commit (OPENBFME_PERF1_REGEN=1, a CI host): SlaveWatcherBehavior (the castle expansions' and
// citadels' watched slave id and its state in the hash; the slave shares the structure's upgrades, RW 0x887AF5, and SlaveAttackNugget orders it, RW 0x910EA1)
// moves the skirmish from frame 0.
// Lane MODCOMPAT-MOD1 re-measured them at CommandButtonHuntUpdate's commit (OPENBFME_PERF1_REGEN=1, a CI host): CommandButtonHuntUpdate (its button and hunt
// state, RW 0x89903C, are in the object hash; the skirmish AI's TEAM_HUNT_WITH_COMMAND_BUTTON hunts with it) moves the battle from frame 0 and the skirmish
// from frame 375.
// Lane MODCOMPAT-MOD1 re-measured them at GrabPassengerSpecialPower's commit (OPENBFME_PERF1_REGEN=1, a CI host): GrabPassengerSpecialPower (the trolls' grab
// power is a special power module with its state in the object hash, RW 0x8C942D; its grab of an object runs, RW 0x8C94D7) moves the battle from frame 0 and
// the skirmish from frame 0.
// Lane MODCOMPAT-MOD1 re-measured them at CritterEmitterUpdate's commit (OPENBFME_PERF1_REGEN=1, a CI host): CritterEmitterUpdate (the BirdEmitter's module
// state, RW 0x8CD256, in the object hash; a unit walking into it draws the logic random for its FXList and OCL, RW 0x8CD297, and the emitter rests out of the
// collision manager, RW 0x8CD1F6) moves the battle from frame 0 and the skirmish from frame 0.
// Lane MODCOMPAT-MOD1 re-measured them at AudioLoopUpgrade's commit (OPENBFME_PERF1_REGEN=1, a CI host): AudioLoopUpgrade (its update module and upgrade mux
// are in the object hash, crc after RW 0x8BBF93's xfer; the upgrade wakes its update after KillAfterMS, RW 0x8BBE71, in the logic's update vectors) moves the
// skirmish from frame 0.
// Lane MODCOMPAT-MOD1 re-measured them at ClickReactionBehavior's commit (OPENBFME_PERF1_REGEN=1, a CI host): ClickReactionBehavior (the trolls' click meter
// and reaction countdown, xfer RW 0x85C5DE, are in the object hash; its update RW 0x85C704 joins the sleepy update vectors) moves the battle from frame 0 and
// the skirmish from frame 0.
// Lane MODCOMPAT-MOD1 re-measured them at PickupStuffUpdate's commit (OPENBFME_PERF1_REGEN=1, a CI host): PickupStuffUpdate (every horde's update module, RW
// 0x8957B7: a human player's sleeps for ever after its first update, which reorders the sleepy update vectors, RW 0x62E982; the AI's kept temporary machine,
// RW 0x6630A9, is in the AI hash) moves the battle from frame 0 and the skirmish from frame 296.
// Lane MODCOMPAT-MOD1 re-measured them at AutoPickUpUpdate's commit (OPENBFME_PERF1_REGEN=1, a CI host): AutoPickUpUpdate (the trolls' and the Watcher's scan
// countdown starts at a logic random value, RW 0x8994BF, and its state is in the object hash; a scan grabs or eats through the grab power, RW 0x89958F) moves
// the battle from frame 0 and the skirmish from frame 0.
// Lane MODCOMPAT-MOD1 re-measured them at BoredUpdate's commit (OPENBFME_PERF1_REGEN=1, a CI host): BoredUpdate (the trolls' scan countdown starts at a logic
// random value, RW 0x899F1F, and its state is in the object hash; an idle troll clubs a troll friend through its bored power, RW 0x899F9D) moves the battle
// from frame 0 and the skirmish from frame 0.
// Lane MODCOMPAT-MOD1 re-measured them at SalvageCrateCollide's commit (OPENBFME_PERF1_REGEN=1, a CI host): SalvageCrateCollide (a unit that touches a crate
// collects it, CrateCollide RW 0x8C06B6, and the crate is destroyed; the AI hordes' pick-ups now begin and end, RW 0x895715) moves the skirmish from frame
// 1577.
// Lane MODCOMPAT-MOD1 re-measured them at the run-down aim fix's commit (OPENBFME_PERF1_REGEN=1, a CI host): the aim state of a unit running a target down
// ends at once (AIAttackAimAtTargetState RW 0x753426 / 0x7524B5: the riders strike on other frames) moves the battle from frame 129 and the skirmish from
// frame 598.
// Lane SPELLFIX-1 re-measured both (OPENBFME_PERF1_REGEN=1, a CI host), each change switched off alone to name its cause: an omitted ShroudClearingRange
// clearing the VisionRange (RW 0x73FB7A / 0x69990F) moves both from frame 0; the economy's PRODUCTION product from the object's pool (levelled resource
// buildings pay their level lists) moves the skirmish again from frame 2910; StartingBusyTime, ClearRemovables and the banner carrier's first update
// (RW 0x89A258) move neither.
// The horde contain fix re-measured both (OPENBFME_PERF1_REGEN=1, Windows llvm-mingw): a horde makes its members where it stands at build complete
// (RW 0x8718ED) and a move no longer puts them on their slots (RW 0x871536); it moves the battle from frame 0 and the skirmish from frame 2510.
// The hierarchical block zones re-measured both (OPENBFME_PERF1_REGEN=1, Windows llvm-mingw): the block search compares block zones (RW 0x9389A6) and
// walks the walkways' connect cells (RW 0x9389CE), so its corridors and the cell searches held to them change; it moves the battle from frame 11 and the
// skirmish from frame 13.
// Merge queue q34 re-measured them once on the merged tree (OPENBFME_PERF1_REGEN=1, a CI host), compared at each merge point: SPELLFIX-1 (an omitted
// ShroudClearingRange clears the VisionRange, RW 0x73FB7A / 0x69990F; the economy's PRODUCTION product) moves the battle, the skirmish and the allied blob;
// #160 (members made where their horde stands, RW 0x8718ED, and left where they are by a move, RW 0x871536) moves the battle and the allied blob, not the
// skirmish; #161 (the block search's block zones RW 0x9389A6 and raised branch RW 0x9389CE) moves all three.
TEST_CASE("perf1: the big battle (400 frames) and a 4-player AI skirmish after another AI game (6000 frames) hash as the build before PERF-1 did in every frame")
{
	// Ambient movement: target idle wandering adds RNG draws and origin state,
	// so these engine references are measured again for this simulation change.
	const std::vector<std::uint32_t> battleRef = perf1::readHashes("big_battle_400.hashes");
	const std::vector<std::uint32_t> skirmishRef = perf1::readHashes("skirmish4_6000.hashes");
	// OPENBFME_PERF1_REGEN=1 rewrites the two reference vectors from this build instead of comparing: run it only when a merged change is MEANT to
	// change the simulation (a retail port), and say so in the commit; a pure optimisation must pass against the stored vectors unchanged
	const char *regenEnv = std::getenv("OPENBFME_PERF1_REGEN");
	const bool regen = regenEnv && *regenEnv == '1';
	if (!regen)
	{
		REQUIRE(battleRef.size() == 400u);
		REQUIRE(skirmishRef.size() == 6000u);
	}
	if (!retailtest::pureMount())
	{
		retailtest::printSkip("perf1 determinism (fresh world)");
		return;
	}
	SharedWorld w;
	GameLogicSettings settings;
	std::string error;
	REQUIRE_MESSAGE(perf1::loadFreshWorld(w, settings, error), error);
	{
		perf1::Run run;
		perf1::runBigBattle(w, 400, run);
		if (regen)
		{
			perf1::storeHashes("big_battle_400.hashes", run.hashes);
		}
		else
		{
			CHECK_MESSAGE(perf1::firstDifference(run.hashes, battleRef) == -1, "big battle: first different frame");
		}
	}
	{
		perf1::Run warmUp;
		perf1::runAISkirmish(*w.mount->fs, *w.world, w.options, settings, { "FactionMordor", "FactionIsengard" }, 300, warmUp);
		CHECK(warmUp.objectsEnd > warmUp.objectsStart); // the computer players built and trained
	}
	{
		perf1::Run run;
		perf1::runAISkirmish(*w.mount->fs, *w.world, w.options, settings, perf1::fourFactions(), 6000, run);
		if (regen)
		{
			perf1::storeHashes("skirmish4_6000.hashes", run.hashes);
		}
		else
		{
			CHECK_MESSAGE(perf1::firstDifference(run.hashes, skirmishRef) == -1, "4-player skirmish: first different frame");
		}
	}
}

// The allied blob (24 hordes sent to one point, 300 frames) on a world this test loads itself, against tests/data/perf1/allied_blob_300.hashes measured
// with the build before the collision module caches (upstream main 87579746): the contact pairs' lookups must not change a frame. OPENBFME_PERF1_REGEN=1
// rewrites the vector, as above. Merge queue q33 measured it on the archive's tree at #164's merge point (where every other PERF-1 / UBER-1 vector is the
// archive's, byte for byte), then once on the merged tree: MODCOMPAT-MOD1 and #133 each move it, #166 keeps it.
TEST_CASE("perf1: the allied blob (300 frames) hashes as the build before the collision module caches did in every frame")
{
	const std::vector<std::uint32_t> blobRef = perf1::readHashes("allied_blob_300.hashes");
	const char *regenEnv = std::getenv("OPENBFME_PERF1_REGEN");
	const bool regen = regenEnv && *regenEnv == '1';
	if (!regen)
	{
		REQUIRE(blobRef.size() == 300u);
	}
	if (!retailtest::pureMount())
	{
		retailtest::printSkip("perf1 allied blob determinism (fresh world)");
		return;
	}
	SharedWorld w;
	GameLogicSettings settings;
	std::string error;
	REQUIRE_MESSAGE(perf1::loadFreshWorld(w, settings, error), error);
	perf1::Run run;
	perf1::runAlliedBlob(w, 300, run);
	if (regen)
	{
		perf1::storeHashes("allied_blob_300.hashes", run.hashes);
	}
	else
	{
		CHECK_MESSAGE(perf1::firstDifference(run.hashes, blobRef) == -1, "allied blob: first different frame");
	}
}

namespace
{
// what PhysicsBehavior::find and SlavedUpdateInterface::of answered before Object kept them (lane PERF-HD): the first such module of the list
template <class T>
T *scannedModule(const Object &o)
{
	for (const std::unique_ptr<BehaviorModule> &m : o.modules())
	{
		if (T *p = dynamic_cast<T *>(m.get()))
		{
			return p;
		}
	}
	return nullptr;
}

bool lookupsAsScanned(Object &o)
{
	return o.moduleLookupsCached() && o.physicsModule() == scannedModule<PhysicsBehavior>(o) && PhysicsBehavior::find(o) == scannedModule<PhysicsBehavior>(o) &&
		o.slavedModule() == scannedModule<SlavedUpdateInterface>(o) && SlavedUpdateInterface::of(o) == scannedModule<SlavedUpdateInterface>(o);
}
} // namespace

TEST_CASE("perf1: every template with a PhysicsBehavior, a SlavedUpdate or an AI keeps the modules the scans find (lane PERF-HD)")
{
	if (!haveWorld("perf1 module lookups"))
	{
		return;
	}
	SharedWorld &s = shared();
	auto scope = s.world->enterContext();
	Rig rig(s);
	GameLogic &logic = rig.logic();
	size_t created = 0, withPhysics = 0, slaved = 0;
	std::vector<std::string> problems;
	for (const ThingTemplate *t : s.world->things().templates())
	{
		const ThingTemplate *tt = t->getFinalOverride();
		bool wanted = false;
		for (const ThingTemplate::Nugget &n : tt->behaviorModules().nuggets())
		{
			wanted = wanted || n.name == "PhysicsBehavior" || n.name == "SlavedUpdate" || n.name.find("AIUpdate") != std::string::npos;
		}
		if (!wanted)
		{
			continue;
		}
		Object *o = logic.newObject(tt, rig.local->getDefaultTeam(), ObjectStatusMaskType{});
		if (!o)
		{
			problems.push_back(tt->getName() + ": not created");
			continue;
		}
		++created;
		withPhysics += scannedModule<PhysicsBehavior>(*o) ? 1 : 0;
		slaved += scannedModule<SlavedUpdateInterface>(*o) ? 1 : 0;
		if (!lookupsAsScanned(*o))
		{
			problems.push_back(tt->getName());
		}
	}
	// RotWK 2.01's templates with a PhysicsBehavior, a SlavedUpdate or an AIUpdate class, their PhysicsBehaviors and their SlavedUpdates
	CHECK(created == 1550u);
	CHECK(withPhysics == 1297u);
	CHECK(slaved == 39u);
	CHECK_MESSAGE(problems.empty(), (problems.empty() ? std::string() : problems.front()));
}

TEST_CASE("perf1: a formation swap (friend_replaceModule) keeps the horde object's and its members' modules as the scans find them (lane PERF-HD)")
{
	if (!haveWorld("perf1 module lookups (formation swap)"))
	{
		return;
	}
	SharedWorld &s = shared();
	auto scope = s.world->enterContext();
	Rig rig(s);
	Object *h = rig.make("GondorTowerShieldGuardHorde", 600.0f, 600.0f);
	rig.logic().runLogicFrame();
	HordeContain *hc = dynamic_cast<HordeContain *>(h->getContain());
	REQUIRE(hc != nullptr);
	CHECK(lookupsAsScanned(*h));
	REQUIRE(hc->canToggleFormation());
	HordeContain *alt = hc->toggleFormation();
	REQUIRE(alt != nullptr);
	CHECK(h->getContain() == alt);
	CHECK(lookupsAsScanned(*h));
	rig.logic().runLogicFrame();
	HordeContain *back = alt->toggleFormation();
	REQUIRE(back != nullptr);
	CHECK(lookupsAsScanned(*h));
	for (const Object *m : *back->getContainedItemsList())
	{
		CHECK(lookupsAsScanned(*const_cast<Object *>(m)));
	}
}

TEST_CASE("perf1: the skirmish AI's unit name keys are created when the ArmyDefinitions load; keyOf only looks them up (review r2, was S-1080)")
{
	NameKeyGenerator keys;
	keys.init();
	SkirmishAIStore store(keys);
	const NameKeyType next = keys.nextId();
	CHECK_THROWS_AS(store.keyOf("PerfOneProbeUnit"), std::logic_error); // a game never creates a key (lane BUGS-1: a missing key is an error)
	CHECK(keys.findKey("PerfOneProbeUnit") == NAMEKEY_INVALID);
	CHECK(keys.nextId() == next);
	const NameKeyType made = keys.nameToKey("PerfOneProbeUnit");
	CHECK(store.keyOf("PerfOneProbeUnit") == made);
	for (const std::string &l : SkirmishAIManager::stopLines())
	{
		CHECK(l.rfind("[S-1080]", 0) != 0); // closed
	}
}
