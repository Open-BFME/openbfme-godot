// OpenBFME unit tests. GPL-3.0.
// Lane PERF-1: the performance benchmarks of the simulation and the proof that the optimisations keep it identical. The benchmarks are measurements
// (skipped by default; run them with --no-skip):
//
//   openbfme_tests -tc="perf1 bench*" --no-skip
//
//   * "perf1 bench: big battle": MODULES-2's partition bench scenario (about 1080 objects: Gondor / Mordor hordes, heroes, attack trolls fighting on
//     map mp fall back 4p), every frame's state hash and the logic time per frame (OPENBFME_BENCH_FRAMES, default 400);
//   * "perf1 bench: 4-player skirmish": four medium computer players (Men, Mordor, Elves, Isengard, free for all) on map mp fall back 4p for
//     OPENBFME_BENCH_FRAMES frames (default 6000 = 20 game minutes at 5 logic frames per second).
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
#include "GameLogic/NewGame/NewGame.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SkirmishAI/SkirmishAIData.h"
#include "GameLogic/SkirmishAI/SkirmishAIManager.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"

#include <chrono>
#include <cstdint>
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
void setUpBigBattle(Rig &rig)
{
	GameLogic &logic = rig.logic();
	Player *enemy = rig.game->players().findPlayerWithName("Player_2");
	REQUIRE(enemy);
	float sx = 0.0f, sy = 0.0f;
	int n = 0;
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		sx += o->getPosition()->x;
		sy += o->getPosition()->y;
		++n;
	}
	REQUIRE(n > 0);
	const float cx = sx / (float)n, cy = sy / (float)n;
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
TEST_CASE("perf1: the big battle (400 frames) and a 4-player AI skirmish after another AI game (6000 frames) hash as the build before PERF-1 did in every frame")
{
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

TEST_CASE("perf1: the skirmish AI's unit name keys are created when the ArmyDefinitions load; keyOf only looks them up (review r2, was S-1080)")
{
	NameKeyGenerator keys;
	keys.init();
	SkirmishAIStore store(keys);
	const NameKeyType next = keys.nextId();
	CHECK(store.keyOf("PerfOneProbeUnit") == NAMEKEY_INVALID); // a game never creates a key
	CHECK(keys.findKey("PerfOneProbeUnit") == NAMEKEY_INVALID);
	CHECK(keys.nextId() == next);
	const NameKeyType made = keys.nameToKey("PerfOneProbeUnit");
	CHECK(store.keyOf("PerfOneProbeUnit") == made);
	for (const std::string &l : SkirmishAIManager::stopLines())
	{
		CHECK(l.rfind("[S-1080]", 0) != 0); // closed
	}
}
