// OpenBFME unit tests. GPL-3.0.
// Lane SAVE-4c on the retail game (docs/SAVES.md T1, T2, T4, T5, T8 as far as SAVE-4c's blocks allow, and the measurement the brief asks for): the five
// blocks (obfme:Partition, obfme:Emotions, obfme:AIWorld with the AI update modules, obfme:Shroud, obfme:SkirmishAI) of a real 4-AI skirmish on Fall Back 8p
// are saved at frame 3,000, loaded into a second identical game whose state of those blocks was first replaced by an earlier frame's (so the load must
// rebuild it, not find it), and give the same state hash section by section, the same bytes saved again and the same hash on every one of 300 later frames.
// The objects themselves are obfme:Objects' (SAVE-4b, not merged): both games keep their own, identical objects. SKIP loudly without ROTWK_INSTALL /
// BFME2_INSTALL.

#include "HordeCompleteTestUtil.h"
#include "doctest.h"

#include "StartTestUtil.h"

#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"

#include "Common/GameState.h"
#include "Common/JobSystem.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "Common/PlayerTemplate.h"
#include "GameClient/GUI/Skirmish/IniSkirmishSetupSource.h"
#include "GameClient/LiveGame.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/NewGame/NewGame.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/System/EmotionSystem.h"
#include "GameLogic/System/ShroudManager.h"
#include "Common/PlayerList.h"
#include "GameLogic/AI/AIGarrisonStates.h"
#include "GameLogic/AI/AIClosestEnemy.h"
#include "GameLogic/AI/AIGuard.h"
#include "GameLogic/AI/AIHunt.h"
#include "GameLogic/Module/GiantBirdAIUpdate.h"
#include "GameLogic/AI/AIStateMachine.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Module/HeroAbilityModules.h"
#include "GameLogic/VictoryConditions.h"
#include "GameLogic/SkirmishAI/SkirmishAIData.h"
#include "GameLogic/SkirmishAI/SkirmishAIManager.h"
#include "Common/StateHash.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"

#include "LogicTestUtil.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <functional>
#include <set>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace
{
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

const std::vector<MapCacheEntry> &mapCache(starttest::Shared &s)
{
	static std::vector<MapCacheEntry> cache;
	if (cache.empty())
	{
		std::string error;
		REQUIRE_MESSAGE(IniSkirmishSetupSource::loadMapCache(*s.mount->fs, cache, &error), error);
	}
	return cache;
}

// four Medium AIs (Men, Mordor, Elves, Isengard) on the 8-player Fall Back, each on a team of its own (the game SAVE-4a measured)
NewGameMessage fourPlayerMessage(const starttest::Shared &s, std::uint32_t seed)
{
	NewGameMessage m;
	m.game.mapName = "maps/map mp fall back 8p/map mp fall back 8p.map";
	m.game.seed = seed;
	m.game.startingCash = 1500;
	const char *factions[4] = { "FactionMen", "FactionMordor", "FactionElves", "FactionIsengard" };
	for (int i = 0; i < 4; ++i)
	{
		SkirmishGameSlot &sl = m.game.slots[(size_t)i];
		sl.state = SLOT_MED_AI;
		sl.name = u"Computer";
		sl.playerTemplate = templateIndex(s.world->playerTemplates(), factions[i]);
		REQUIRE(sl.playerTemplate != -1);
		sl.startPos = i;
		sl.color = i;
		sl.teamNumber = i;
	}
	return m;
}

struct Game
{
	NewGameStart start{ RandomAlgorithm::ZH_CarryChain };
	std::unique_ptr<ArchiveW3DFileSource> source;
	std::unique_ptr<WW3DAssetManager> assets;
	std::unique_ptr<LiveGame> game;
	GameLogic &logic() { return game->logic(); }
};

void load(starttest::Shared &s, const NewGameMessage &m, Game &out)
{
	std::string error;
	REQUIRE_MESSAGE(NewGame::prepareNewGame(m, s.world->playerTemplates(), s.settings, mapCache(s), RandomAlgorithm::ZH_CarryChain, out.start, &error), error);
	out.source = std::make_unique<ArchiveW3DFileSource>(*s.mount->fs);
	out.assets = std::make_unique<WW3DAssetManager>(*out.source);
	out.game = std::make_unique<LiveGame>(*s.world, *s.mount->fs, *out.assets, s.options);
	LiveGame::Options o;
	o.start = &out.start;
	REQUIRE_MESSAGE(out.game->load(o, &error), error);
}

void step(Game &g)
{
	REQUIRE(g.game->advance(0.2) == 1);
	g.game->drainFrames();
}

void runTo(Game &g, UnsignedInt frame)
{
	while (g.logic().getFrame() < frame)
	{
		step(g);
	}
	REQUIRE(g.logic().getFrame() == frame);
}

const std::vector<std::string> k4c = { "obfme:Partition", "obfme:Emotions", "obfme:AIWorld", "obfme:Shroud", "obfme:SkirmishAI" };
// the state hash sections the five blocks hold ("objects" through the AI update modules; the objects' other state is the same in both games)
const std::vector<std::string> k4cSections = { "partition manager", "emotion system", "pathfinder and AI movers", "shroud", "skirmish AI", "objects" };

std::map<std::string, std::uint32_t> sectionValues(const GameLogic &logic)
{
	std::vector<GameLogic::StateHashSection> sections;
	logic.computeStateHashBreakdown(sections, nullptr);
	std::map<std::string, std::uint32_t> out;
	for (const GameLogic::StateHashSection &s : sections)
	{
		out[s.name] = s.value;
	}
	return out;
}

std::vector<ObjectID> objectIds(const GameLogic &logic)
{
	std::vector<ObjectID> ids;
	for (const Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		ids.push_back(o->getID());
	}
	std::sort(ids.begin(), ids.end());
	return ids;
}

std::vector<ObjectID> aiIds(const GameLogic &logic)
{
	std::vector<ObjectID> ids;
	for (const Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getAIUpdateInterface())
		{
			ids.push_back(o->getID());
		}
	}
	std::sort(ids.begin(), ids.end());
	return ids;
}

std::string loadError(GameLogic &logic, const std::vector<std::string> &tokens, const std::vector<std::uint8_t> &bytes)
{
	try
	{
		logic.gameState().loadBlocks(tokens, bytes);
	}
	catch (const std::exception &e)
	{
		return e.what();
	}
	return "";
}

// the offset of a one-block stream's payload: the 16-byte stream header, the token (a u8 length and the bytes), "BLOK" and the end offset
size_t payloadOffset(const std::string &token)
{
	return 16 + 1 + token.size() + 8;
}
} // namespace

TEST_CASE("save4c retail T1 / T2: a 4-AI skirmish's partition, emotions, AI world (the pathfinder and every AI module), shroud and skirmish AI at frame "
		  "3000 are loaded into an identical game whose state of those blocks was first replaced (an earlier frame's partition, AI world and skirmish AI, each "
		  "AI module's state of a minute earlier, a shrouded map, an empty emotion list): every state hash section is the saved game's, the load saves the same bytes again, and the two games' "
		  "hashes are equal on each of the next 300 frames")
{
	OPENBFME_REQUIRE_START(s);
	const NewGameMessage m = fourPlayerMessage(*s, 4123);
	Game a, b;
	load(*s, m, a);
	load(*starttest::peerWorld(), m, b); // one live game per world (S-3808): the second game has its own
	// the earlier frame: the first of 2950 .. 2999 whose partition entries and pathfinder views name objects alive at 3000 and whose AI modules are
	// 3000's (that state must load into the game at 3000); the first, so the state differs from 3000's in as many fields as the window allows
	const std::vector<std::string> kEarlier = { "obfme:Partition", "obfme:AIWorld", "obfme:SkirmishAI" };
	// and each AI module's own state a minute earlier (frame 2700), for every module alive at both frames
	runTo(a, 2700);
	std::map<ObjectID, std::vector<std::uint8_t>> minuteEarlier;
	for (Object *o = a.logic().getFirstObject(); o; o = o->getNextObject())
	{
		if (AIUpdateInterface *ai = o->getAIUpdateInterface())
		{
			XferSave xs;
			ai->xferAIState(xs);
			minuteEarlier[o->getID()] = xs.data();
		}
	}
	runTo(a, 2950);
	std::map<UnsignedInt, std::vector<std::uint8_t>> earlier;
	std::map<UnsignedInt, std::vector<ObjectID>> earlierRefs, earlierAIs;
	// review r5: the target is the first frame from 3000 on (up to 3100) with such an earlier frame among its 50 before (the factory map's events changed
	// the game: at 3000 itself an object made on the last frame can leave no earlier frame whose AI modules are the target's)
	auto record = [&] {
		GameLogic &l = a.logic();
		const UnsignedInt f = l.getFrame();
		l.gameState().saveBlocks(kEarlier, earlier[f]);
		std::vector<ObjectID> refs;
		for (Object *o = l.getFirstObject(); o; o = o->getNextObject())
		{
			if (l.partition().isRegistered(*o) || l.aiWorld()->findAdapter((PathfindObjectID)o->getID()))
			{
				refs.push_back(o->getID());
			}
		}
		std::sort(refs.begin(), refs.end());
		earlierRefs[f] = refs;
		earlierAIs[f] = aiIds(l);
	};
	while (a.logic().getFrame() < 3000)
	{
		record();
		step(a);
	}
	UnsignedInt chosen = 0;
	for (;;)
	{
		const UnsignedInt target = a.logic().getFrame();
		const std::vector<ObjectID> objects = objectIds(a.logic()), ais = aiIds(a.logic());
		for (const auto &e : earlierRefs)
		{
			if (e.first + 50 >= target && std::includes(objects.begin(), objects.end(), e.second.begin(), e.second.end()) && earlierAIs[e.first] == ais)
			{
				chosen = e.first;
				break;
			}
		}
		if (chosen != 0 || target >= 3100)
		{
			break;
		}
		record();
		step(a);
	}
	// tolerance: a precondition (some frame of the window must load into the target; the chosen one is reported)
	REQUIRE_MESSAGE(chosen != 0, "no target frame of 3000 .. 3100 has an earlier frame among its 50 before with its references alive and its AI modules");
	const UnsignedInt targetFrame = a.logic().getFrame();
	runTo(b, targetFrame);
	REQUIRE(b.logic().computeStateHash() == a.logic().computeStateHash());
	const std::vector<ObjectID> ais = aiIds(a.logic());
	MESSAGE("SAVE-4c T1: the target is frame " << targetFrame << ", the earlier state frame " << chosen << "'s (" << ais.size() << " AI modules)");

	std::vector<std::uint8_t> saved;
	a.logic().gameState().saveBlocks(k4c, saved);
	const std::map<std::string, std::uint32_t> want = sectionValues(a.logic());

	// b's state of the five blocks is replaced, so each of their sections differs from a's
	b.logic().gameState().loadBlocks(kEarlier, earlier[chosen]);
	size_t older = 0;
	for (Object *o = b.logic().getFirstObject(); o; o = o->getNextObject())
	{
		auto it = minuteEarlier.find(o->getID());
		if (it != minuteEarlier.end() && o->getAIUpdateInterface())
		{
			XferLoad xl(it->second);
			o->getAIUpdateInterface()->xferAIState(xl);
			REQUIRE(xl.atEnd());
			++older;
		}
	}
	MESSAGE("SAVE-4c T1: " << older << " AI modules hold their frame-2700 state before the load");
	REQUIRE(older > 0); // tolerance: a precondition (the perturbation reaches modules alive at both frames; their number is reported above)
	REQUIRE(!b.logic().emotions().scaryOrHeroes().empty());
	b.logic().emotions().reset();
	REQUIRE(b.logic().shroud() != nullptr);
	for (int p = 0; p < b.logic().players().getPlayerCount(); ++p)
	{
		b.logic().shroud()->shroudMapForPlayer(p);
	}
	{
		const std::map<std::string, std::uint32_t> before = sectionValues(b.logic());
		for (const std::string &sec : k4cSections)
		{
			INFO("section " << sec);
			REQUIRE(want.count(sec) == 1);
			CHECK(before.at(sec) != want.at(sec));
		}
	}
	// per player, the shroud's looker counts differ before the load (review r2: per-player shroud checks)
	ShroudManager &sa = *a.logic().shroud(), &sb = *b.logic().shroud();
	REQUIRE(sa.cellCountX() == sb.cellCountX());
	REQUIRE(sa.cellCountY() == sb.cellCountY());
	auto lookersOf = [](const ShroudManager &m, int p) {
		std::vector<int> v;
		for (int y = 0; y < m.cellCountY(); ++y)
		{
			for (int x = 0; x < m.cellCountX(); ++x)
			{
				v.push_back(m.lookerCount(p, x, y));
			}
		}
		return v;
	};
	const std::vector<int> slotPlayers = a.logic().skirmishAI().infoPlayers(); // the game's four (AI) players
	REQUIRE(slotPlayers.size() == 4u);
	for (int p : slotPlayers)
	{
		INFO("player " << p);
		CHECK(lookersOf(sb, p) != lookersOf(sa, p));
	}
	// the load at frame 3000: every section (the five blocks' and all the others) is a's again
	b.logic().gameState().loadBlocks(k4c, saved);
	CHECK(sectionValues(b.logic()) == want);
	for (int p = 0; p < ShroudManager::kMaxPlayers; ++p)
	{
		INFO("player " << p);
		CHECK(lookersOf(sb, p) == lookersOf(sa, p));
	}
	// the skirmish AI's plans and timers, AI by AI (review r2): the tactics in flight with their steps, the targets, the attacked-at records, the build
	// queues, the phase, the retry timer and the spell book assist timers
	{
		const SkirmishAIManager &ma = a.logic().skirmishAI(), &mb = b.logic().skirmishAI();
		REQUIRE(ma.groups().size() == mb.groups().size());
		size_t tactics = 0;
		for (size_t gi = 0; gi < ma.groups().size(); ++gi)
		{
			const AISkirmishGroup &ga = *ma.groups()[gi], &gb = *mb.groups()[gi];
			REQUIRE(ga.players.size() == gb.players.size());
			for (const auto &kv : ga.players)
			{
				INFO("the AI of player " << kv.first);
				REQUIRE(gb.players.count(kv.first) == 1);
				const AISkirmishPlayer &pa = *kv.second, &pb = *gb.players.at(kv.first);
				REQUIRE(pa.brain.tactics.size() == pb.brain.tactics.size());
				tactics += pa.brain.tactics.size();
				for (size_t i = 0; i < pa.brain.tactics.size(); ++i)
				{
					const AITactic &ta = pa.brain.tactics[i], &tb = pb.brain.tactics[i];
					CHECK(ta.kind == tb.kind);
					CHECK(ta.id == tb.id);
					CHECK(ta.step == tb.step);
					CHECK(ta.step2 == tb.step2);
					CHECK(ta.started == tb.started);
					CHECK(ta.readyFrame == tb.readyFrame);
					CHECK(ta.launchFrame == tb.launchFrame);
					CHECK(ta.teams.size() == tb.teams.size());
				}
				CHECK(pa.brain.targets.size() == pb.brain.targets.size());
				CHECK(pa.brain.bestTarget == pb.brain.bestTarget);
				CHECK(pa.brain.attackedAt.size() == pb.brain.attackedAt.size());
				CHECK(pa.build.pending.size() == pb.build.pending.size());
				CHECK(pa.build.inProgress.size() == pb.build.inProgress.size());
				CHECK(pa.build.phase == pb.build.phase);
				CHECK(pa.retryTimer == pb.retryTimer);
			}
		}
		// tolerance: a precondition (the 4-AI game at frame 3000 has tactics in flight, so the plans are exercised; each is compared above)
		CHECK(tactics != 0u);
		SkirmishAIManager &wa = a.logic().skirmishAI(), &wb = b.logic().skirmishAI();
		CHECK(wa.spellBookAssistTimers() == wb.spellBookAssistTimers());
	}
	CHECK(b.logic().computeStateHash() == a.logic().computeStateHash());
	// T2: the loaded state saves the same bytes
	std::vector<std::uint8_t> again;
	b.logic().gameState().saveBlocks(k4c, again);
	CHECK(again == saved);
	// T1 as far as SAVE-4c allows: the two games go on equal, frame by frame (the AI players decide and order as before the load)
	for (int i = 0; i < 300; ++i)
	{
		step(a);
		step(b);
		const std::uint32_t ha = a.logic().computeStateHash(), hb = b.logic().computeStateHash();
		if (ha != hb)
		{
			const std::map<std::string, std::uint32_t> sa = sectionValues(a.logic()), sb = sectionValues(b.logic());
			std::string differ;
			for (const auto &kv : sa)
			{
				differ += sb.count(kv.first) && sb.at(kv.first) == kv.second ? "" : " '" + kv.first + "'";
			}
			FAIL("frame " << a.logic().getFrame() << ": the hashes differ in" << differ);
		}
	}
	CHECK(a.logic().getFrame() == targetFrame + 300);
}

TEST_CASE("save4c retail T4: a real 4-player skirmish's snapshot is refused naming exactly the blocks SAVE-4b and 4d must port (SAVE-4c's five have their "
		  "xfer)")
{
	OPENBFME_REQUIRE_START(s);
	Game g;
	load(*s, fourPlayerMessage(*s, 4120), g);
	runTo(g, 50);
	GameLogic &logic = g.logic();
	CHECK(logic.gameState().undeclaredSections(logic.stateHashSectionNames()).empty());
	for (const std::string &t : k4c)
	{
		const GameState::Block *b = logic.gameState().findBlock(t);
		REQUIRE(b != nullptr);
		CHECK(b->snapshot != nullptr);
		CHECK(b->owner == "SAVE-4c");
	}
	std::vector<std::uint8_t> out;
	std::string error;
	REQUIRE_MESSAGE(logic.saveSnapshot(out, &error), error); // lane SAVE-4e: SAVE-4b and 4d merged
	CHECK(!out.empty());
}

TEST_CASE("save4c retail T5: each block's version window (RW 0xA1F5B9's two messages), a snapshot of another moment's AI modules, a partition entry of an "
		  "object the game lacks and a synthetic game's blocks (no AI world, no shroud) are named refusals")
{
	OPENBFME_REQUIRE_START(s);
	const NewGameMessage m = fourPlayerMessage(*s, 4124);
	Game late, early;
	load(*s, m, late);
	load(*starttest::peerWorld(), m, early); // one live game per world (S-3808)
	runTo(late, 600);
	runTo(early, 50);
	for (const std::string &t : k4c)
	{
		std::vector<std::uint8_t> one;
		late.logic().gameState().saveBlocks({ t }, one);
		const size_t at = payloadOffset(t);
		REQUIRE(one.size() > at); // tolerance: an index precondition (the version byte must exist before it is read)
		// every SAVE-4c block starts with its version: 1, except obfme:SkirmishAI's: (earliest 2, current 2) since lane AI5 (the upgrade builders and the money
		// reserve), (3, 3) since community #113 (the capture-flag registration history, which older snapshots cannot recover), (3, 4) since lane AIEXPAND-1 (the
		// EXPANSION heuristic's frame counter)
		const bool skirmish = t == "obfme:SkirmishAI";
		const std::uint8_t version = skirmish ? 4 : 1, earliest = skirmish ? 3 : 1;
		REQUIRE(one[at] == version);
		std::vector<std::uint8_t> above = one, below = one;
		above[at] = (std::uint8_t)(version + 1);
		below[at] = (std::uint8_t)(earliest - 1);
		CHECK(loadError(late.logic(), { t }, above) == "block '" + t + "': Don't know how to load version " + std::to_string(version + 1) + " (most recent is " +
			std::to_string(version) + ")");
		CHECK(loadError(late.logic(), { t }, below) == "block '" + t + "': Found version " + std::to_string(earliest - 1) + " but the earliest supported is " +
			std::to_string(earliest));
	}
	// another moment of the same game: the AI modules are not the same set
	std::vector<std::uint8_t> ai;
	late.logic().gameState().saveBlocks({ "obfme:AIWorld" }, ai);
	const size_t lateAIs = late.logic().aiWorld()->liveAIs(), earlyAIs = early.logic().aiWorld()->liveAIs();
	REQUIRE(lateAIs != earlyAIs);
	CHECK(loadError(early.logic(), { "obfme:AIWorld" }, ai) ==
		"block 'obfme:AIWorld': ai world: the snapshot holds " + std::to_string(lateAIs) + " AI modules, the loaded game " + std::to_string(earlyAIs));
	// a partition entry whose object the loaded game lacks (the newest entry of frame 600, after the frame-50 game's last object): named by the post-process
	std::vector<std::uint8_t> part;
	late.logic().gameState().saveBlocks({ "obfme:Partition" }, part);
	const size_t first = payloadOffset("obfme:Partition") + 1 + 5 * 4 + 1 + 4; // version, the region and scale, the region flag, the entry count
	REQUIRE(part.size() >= first + 4); // tolerance: an index precondition (the first entry id must exist before it is read)
	ObjectID newest = 0;
	std::memcpy(&newest, &part[first], 4);
	REQUIRE(early.logic().findObjectByID(newest) == nullptr);
	CHECK(loadError(early.logic(), { "obfme:Partition" }, part) == "partition: the entry of object " + std::to_string(newest) + " has no object in the loaded game");
	// a synthetic game (no map: no AI world, no shroud manager) saves the two blocks with their subsystem absent; a real game refuses them
	logictest::LogicWorld w;
	std::vector<std::uint8_t> noWorld, noShroud;
	w.logic->gameState().saveBlocks({ "obfme:AIWorld" }, noWorld);
	w.logic->gameState().saveBlocks({ "obfme:Shroud" }, noShroud);
	CHECK(loadError(late.logic(), { "obfme:AIWorld" }, noWorld) == "block 'obfme:AIWorld': the snapshot's game has no AI world, this game has one");
	CHECK(loadError(late.logic(), { "obfme:Shroud" }, noShroud) == "block 'obfme:Shroud': the snapshot's game has no shroud manager, this game has one");
}

TEST_CASE("save4c retail T8 as far as SAVE-4c allows: under Uber at 1, 4 and 16 logic threads the same game's five blocks save the same bytes at frame 300")
{
	OPENBFME_REQUIRE_START(s);
	const int before = JobSystem::logic().threadCount();
	const NewGameMessage m = fourPlayerMessage(*s, 4125);
	std::vector<std::uint8_t> first;
	for (int threads : { 1, 4, 16 })
	{
		INFO("threads " << threads);
		JobSystem::logic().setThreadCount(threads);
		Game g;
		load(*s, m, g);
		g.logic().setSimulationProfile(SimulationProfile::Uber);
		runTo(g, 300);
		std::vector<std::uint8_t> stream;
		g.logic().gameState().saveBlocks(k4c, stream);
		if (first.empty())
		{
			first = stream;
		}
		CHECK(stream == first);
	}
	JobSystem::logic().setThreadCount(before);
}

TEST_CASE("save4c retail measurement: the five blocks' size and write time on the 4-AI Fall Back 8p game at frame 3000")
{
	OPENBFME_REQUIRE_START(s);
	Game g;
	load(*s, fourPlayerMessage(*s, 4121), g);
	runTo(g, 3000);
	GameLogic &logic = g.logic();
	std::string sizes;
	size_t total = 0;
	for (const std::string &t : k4c)
	{
		std::vector<std::uint8_t> one;
		logic.gameState().saveBlocks({ t }, one);
		const size_t payload = one.size() - 16 - 7; // without the stream's 16-byte header and the 7-byte end token
		total += payload;
		sizes += t + " " + std::to_string(payload) + " B; ";
	}
	double best = 1e9;
	std::vector<std::uint8_t> w;
	for (int i = 0; i < 20; ++i)
	{
		const auto t0 = std::chrono::steady_clock::now();
		logic.gameState().saveBlocks(k4c, w);
		const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
		best = ms < best ? ms : best;
	}
	CHECK(w.size() == 16 + total + 7);
	// the AI modules' share of obfme:AIWorld (each module's state on its own, without the block's shared string table)
	size_t modules = 0;
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (AIUpdateInterface *ai = o->getAIUpdateInterface())
		{
			XferSave xs;
			ai->xferAIState(xs);
			modules += xs.data().size();
		}
	}
	MESSAGE("SAVE-4c blocks of a 4-player skirmish at frame 3000 (" << logic.getObjectCount() << " objects, " << logic.aiWorld()->liveAIs()
		<< " AI modules): " << w.size() << " bytes (" << sizes << "), written in " << best << " ms (best of 20); the AI modules alone (each with its own "
		<< "stop lines) " << modules << " bytes");
}

namespace
{
std::uint32_t u32At(const std::vector<std::uint8_t> &b, size_t at)
{
	REQUIRE(at + 4 <= b.size()); // tolerance: an index precondition (the word must exist before it is read)
	std::uint32_t v = 0;
	std::memcpy(&v, &b[at], 4);
	return v;
}

std::vector<std::uint8_t> withU32(std::vector<std::uint8_t> b, size_t at, std::uint32_t v)
{
	REQUIRE(at + 4 <= b.size()); // tolerance: an index precondition (the word must exist before it is written)
	std::memcpy(&b[at], &v, 4);
	return b;
}

// a one-block stream's payload size (without the stream header, the token, the BLOK header and the end token)
size_t payloadSize(const std::vector<std::uint8_t> &one, const std::string &token)
{
	return one.size() - payloadOffset(token) - 7;
}

std::string countError(std::uint32_t count, size_t minBytes, size_t left)
{
	return "a list declares " + std::to_string(count) + " entries of at least " + std::to_string(minBytes) + " bytes, more than the " + std::to_string(left) +
		" bytes left";
}

// the five blocks as the game would save them now (a refused load must leave these bytes as they were)
std::vector<std::uint8_t> savedBlocks(GameLogic &logic)
{
	std::vector<std::uint8_t> out;
	logic.gameState().saveBlocks(k4c, out);
	return out;
}

// the sections a refused load must leave as they were
std::vector<std::uint32_t> sectionsOf(const GameLogic &logic, const std::vector<std::string> &names)
{
	const std::map<std::string, std::uint32_t> all = sectionValues(logic);
	std::vector<std::uint32_t> out;
	for (const std::string &n : names)
	{
		REQUIRE(all.count(n) == 1);
		out.push_back(all.at(n));
	}
	return out;
}
} // namespace

TEST_CASE("save4c retail hostile inputs (SAVES.md section 8, rules 3 to 5): for each block an oversized count, a dangling and a duplicate reference and an "
		  "out-of-range value are refused by their text, before anything is allocated from them or linked: the blocks' state hash sections stay as they were")
{
	OPENBFME_REQUIRE_START(s);
	Game g;
	load(*s, fourPlayerMessage(*s, 4126), g);
	runTo(g, 600);
	GameLogic &logic = g.logic();
	const std::uint32_t big = 0x7FFFFFF0u;
	auto refused = [&](const std::string &token, const std::vector<std::string> &sections, const std::vector<std::uint8_t> &bytes, const std::string &want) {
		INFO(token << ": " << want);
		const std::vector<std::uint32_t> before = sectionsOf(logic, sections);
		const std::vector<std::uint8_t> savedBefore = savedBlocks(logic);
		CHECK(loadError(logic, { token }, bytes) == want);
		CHECK(sectionsOf(logic, sections) == before);
		CHECK(savedBlocks(logic) == savedBefore); // review r3: the next save's bytes, not only the hash
	};

	// ---- obfme:Partition: version, the region (5 reals), the region flag, the entry count, the entries (id, layer + 1, x, y, mask) ----
	{
		const std::string t = "obfme:Partition";
		std::vector<std::uint8_t> one;
		logic.gameState().saveBlocks({ t }, one);
		const size_t p = payloadOffset(t), count = p + 22, e0 = p + 26, e1 = e0 + 20;
		REQUIRE(u32At(one, count) >= 2u); // tolerance: a precondition (two entries to make one the other's twin)
		const std::uint32_t id0 = u32At(one, e0);
		refused(t, { "partition manager" }, withU32(one, count, big), "block '" + t + "': " + countError(big, 20, payloadSize(one, t) - 26));
		refused(t, { "partition manager" }, withU32(one, e0, big), "partition: the entry of object " + std::to_string(big) + " has no object in the loaded game");
		refused(t, { "partition manager" }, withU32(one, e1, id0), "partition: object " + std::to_string(id0) + " has two entries");
		refused(t, { "partition manager" }, withU32(one, e0 + 4, 99), "block '" + t + "': partition: the entry of object " + std::to_string(id0) + " has the layer 98");
		refused(t, { "partition manager" }, withU32(one, e0 + 8, 128),
			"block '" + t + "': partition: the entry of object " + std::to_string(id0) + " has the cell 128, " + std::to_string(u32At(one, e0 + 12)) + " / mask " +
				std::to_string(u32At(one, e0 + 16)) + " outside the 128 cells");
	}
	// ---- obfme:Emotions: version, the id count, the ids ----
	{
		const std::string t = "obfme:Emotions";
		std::vector<std::uint8_t> one;
		logic.gameState().saveBlocks({ t }, one);
		const size_t p = payloadOffset(t), count = p + 1, i0 = p + 5, i1 = i0 + 4;
		REQUIRE(u32At(one, count) >= 2u); // tolerance: a precondition (two heroes to make one the other's twin)
		const std::uint32_t id0 = u32At(one, i0);
		const int scary = ObjectTemplateInfoBuilder::kindOfIndex("SCARY"), hero = ObjectTemplateInfoBuilder::kindOfIndex("HERO");
		ObjectID plain = INVALID_ID;
		for (const Object *o = logic.getFirstObject(); o && plain == INVALID_ID; o = o->getNextObject())
		{
			plain = o->isKindOf((unsigned)scary) || o->isKindOf((unsigned)hero) ? INVALID_ID : o->getID();
		}
		REQUIRE(plain != INVALID_ID);
		const std::string plainName = logic.findObjectByID(plain)->getTemplate()->getName();
		refused(t, { "emotion system" }, withU32(one, count, big), "block '" + t + "': " + countError(big, 4, payloadSize(one, t) - 5));
		refused(t, { "emotion system" }, withU32(one, i0, big), "emotions: the scary / hero list names object " + std::to_string(big) + ", which the loaded game lacks");
		refused(t, { "emotion system" }, withU32(one, i1, id0), "emotions: the scary / hero list names object " + std::to_string(id0) + " twice");
		refused(t, { "emotion system" }, withU32(one, i0, plain),
			"emotions: the scary / hero list names object " + std::to_string(plain) + " (" + plainName + "), which is neither SCARY nor HERO");
	}
	// ---- obfme:Shroud: the presence wrapper (version, flag), version, counter, unlook frames, 6 reals, the grid, the cell count, 80 planes of runs, the
	// unlook queue, the records by slot ----
	{
		const std::string t = "obfme:Shroud";
		std::vector<std::uint8_t> one;
		logic.gameState().saveBlocks({ t }, one);
		const size_t p = payloadOffset(t), countX = p + 35, cellsAt = p + 43;
		const std::uint32_t cells = u32At(one, cellsAt);
		size_t at = p + 47;
		for (int plane = 0; plane < 80; ++plane)
		{
			for (std::uint32_t covered = 0; covered < cells; at += 6)
			{
				covered += u32At(one, at);
			}
		}
		const size_t pendingAt = at;
		at += 4 + 32 * (size_t)u32At(one, pendingAt);
		const size_t slotsAt = at;
		at += 4;
		std::vector<size_t> records; // the offsets of the in-use records' ids
		for (std::uint32_t slot = 0; slot < u32At(one, slotsAt) && records.size() < 2; ++slot)
		{
			REQUIRE(at < one.size()); // tolerance: an index precondition (the slot's in-use byte must exist)
			if (!one[at])
			{
				at += 1;
				continue;
			}
			records.push_back(at + 1);
			const size_t coveredAt = at + 71;
			at = coveredAt + 4 + 4 * (size_t)u32At(one, coveredAt) + 60 + 13;
		}
		REQUIRE(records.size() == 2u);
		const std::uint32_t id0 = u32At(one, records[0]);
		const size_t status0 = records[0] - 1 + 75 + 4 * (size_t)u32At(one, records[0] - 1 + 71);
		std::vector<std::uint8_t> badStatus = one;
		badStatus[status0] = 9;
		refused(t, { "shroud" }, withU32(one, pendingAt, big), "block '" + t + "': " + countError(big, 32, payloadSize(one, t) - (pendingAt + 4 - p)));
		refused(t, { "shroud" }, withU32(one, p + 47, big), "block '" + t + "': shroud: a run of " + std::to_string(big) + " cells at cell 0 of " + std::to_string(cells));
		refused(t, { "shroud" }, withU32(one, countX, u32At(one, countX) + 1),
			"block '" + t + "': shroud: the snapshot's grid is " + std::to_string(u32At(one, countX) + 1) + " x " + std::to_string(u32At(one, countX + 4)) +
				" cells, this game's " + std::to_string(u32At(one, countX)) + " x " + std::to_string(u32At(one, countX + 4)));
		refused(t, { "shroud" }, withU32(one, records[0], big), "shroud: the record of object " + std::to_string(big) + " has no object in the loaded game");
		refused(t, { "shroud" }, withU32(one, records[1], id0), "block '" + t + "': shroud: object " + std::to_string(id0) + " has two records");
		refused(t, { "shroud" }, badStatus, "block '" + t + "': shroud: a record's status 9 is outside 0 .. 4");
	}
	// ---- obfme:SkirmishAI: version, started, the attacked-at ids, the EXPANSION heuristic's frame counter (version 3), the assist timers (count, 8 bytes
	// each), the data flag and hash, the info players ----
	{
		const std::string t = "obfme:SkirmishAI";
		std::vector<std::uint8_t> one;
		logic.gameState().saveBlocks({ t }, one);
		const size_t p = payloadOffset(t), assist = p + 10;
		const size_t info = assist + 4 + 8 * (size_t)u32At(one, assist) + 5;
		REQUIRE(u32At(one, info) >= 2u); // tolerance: a precondition (two info players to make one the other's twin)
		const int players = logic.players().getPlayerCount();
		refused(t, { "skirmish AI" }, withU32(one, assist, big), "block '" + t + "': " + countError(big, 8, payloadSize(one, t) - 14));
		refused(t, { "skirmish AI" }, withU32(one, info + 4, 99),
			"block '" + t + "': skirmish ai: a player info player 99 is outside 0 .. " + std::to_string(players - 1));
		refused(t, { "skirmish AI" }, withU32(one, info + 8, u32At(one, info + 4)), "block '" + t + "': skirmish ai: a player is twice among the player info players");
	}
	// ---- obfme:AIWorld: the presence wrapper, version, the map flag, the AI module count and ids, then the pathfinder (version, ready, configuration hash,
	// tunneling, ignored obstacle, cell counter, pool flag, the class word runs) ----
	{
		const std::string t = "obfme:AIWorld";
		const std::vector<std::string> secs = { "pathfinder and AI movers", "objects" };
		std::vector<std::uint8_t> one;
		logic.gameState().saveBlocks({ t }, one);
		const size_t p = payloadOffset(t), count = p + 4, id0 = p + 8, id1 = p + 12;
		const std::uint32_t ais = u32At(one, count);
		REQUIRE(ais == (std::uint32_t)logic.aiWorld()->liveAIs());
		const size_t run0 = id0 + 4 * (size_t)ais + 16, word0 = run0 + 4;
		const PathfindGridStats grid = logic.aiWorld()->pathfinder().gridStats();
		const std::uint32_t cells = (std::uint32_t)(grid.width * grid.height);
		refused(t, secs, withU32(one, count, big), "block '" + t + "': ai world: the snapshot holds " + std::to_string(big) + " AI modules, the loaded game " + std::to_string(ais));
		refused(t, secs, withU32(one, id0, big),
			"block '" + t + "': ai world: the snapshot's AI of object " + std::to_string(big) + " where the loaded game has object " + std::to_string(u32At(one, id0)) + "'s");
		refused(t, secs, withU32(one, id1, u32At(one, id0)),
			"block '" + t + "': ai world: the snapshot's AI of object " + std::to_string(u32At(one, id0)) + " where the loaded game has object " +
				std::to_string(u32At(one, id1)) + "'s");
		refused(t, secs, withU32(one, run0, big), "block '" + t + "': pathfinder: a run of " + std::to_string(big) + " class words at cell 0 of " + std::to_string(cells));
		refused(t, secs, withU32(one, word0, (u32At(one, word0) & ~0xFu) | 9u), "block '" + t + "': pathfinder: a cell type 9 is outside 0 .. 7");
	}
}

// ---- review r2 (Sol): the reproductions, kept as regression tests; every refusal leaves the live game's state hash as it was ----

TEST_CASE("save4c review r2: an AI module refused late (its Lua snapshot already read, then a command source of 99) is refused by name and the module is as "
		  "before (the load is one transaction)")
{
	OPENBFME_REQUIRE_START(s);
	Game g;
	load(*s, fourPlayerMessage(*s, 4126), g);
	runTo(g, 50);
	AIUpdateInterface *ai = nullptr;
	for (Object *o = g.logic().getFirstObject(); o && !ai; o = o->getNextObject())
	{
		ai = o->getAIUpdateInterface();
	}
	REQUIRE(ai != nullptr);
	XferSave xs;
	ai->xferAIState(xs);
	// the body: version (1), the 19 Lua condition words (+ 1), the last command's source (+ 77)
	std::vector<std::uint8_t> bad = withU32(xs.data(), 1, u32At(xs.data(), 1) ^ 0x01000000u);
	bad = withU32(bad, 77, 99);
	const std::uint32_t before = g.logic().computeStateHash();
	std::string error;
	try
	{
		XferLoad xl(bad);
		ai->xferAIState(xl);
	}
	catch (const XferException &e)
	{
		error = e.what();
	}
	CHECK(error == "ai: the last command's source 99 is outside 0 .. 2");
	CHECK(g.logic().computeStateHash() == before);
}

TEST_CASE("save4c review r2: a whole AI world block whose LAST AI module is malformed is refused by name after the pathfinder and every other module were "
		  "read, and the live game's hash is unchanged")
{
	OPENBFME_REQUIRE_START(s);
	Game g;
	load(*s, fourPlayerMessage(*s, 4126), g);
	runTo(g, 300);
	const std::string t = "obfme:AIWorld";
	std::vector<std::uint8_t> one;
	g.logic().gameState().saveBlocks({ t }, one);
	// the last module's BLOK ("BLOK" little-endian, then its end offset): its body's command source at + 8 + 77
	const std::uint8_t blok[4] = { 'K', 'O', 'L', 'B' };
	size_t last = 0;
	for (size_t i = payloadOffset(t); i + 4 <= one.size(); ++i)
	{
		last = std::memcmp(&one[i], blok, 4) == 0 ? i : last;
	}
	REQUIRE(last > payloadOffset(t)); // tolerance: a precondition (the block holds AI modules)
	const std::vector<std::uint8_t> bad = withU32(one, last + 8 + 77, 99);
	// the module's id: the last of the block's id list
	const std::uint32_t ais = u32At(one, payloadOffset(t) + 4);
	const std::uint32_t lastId = u32At(one, payloadOffset(t) + 8 + 4 * ((size_t)ais - 1));
	const std::string name = g.logic().findObjectByID(lastId)->getTemplate()->getName();
	const std::uint32_t before = g.logic().computeStateHash();
	CHECK(loadError(g.logic(), { t }, bad) == "block '" + t + "': the AI of object " + std::to_string(lastId) + " (" + name +
		"): ai: the last command's source 99 is outside 0 .. 2");
	CHECK(g.logic().computeStateHash() == before);
	// and the intact block still loads (the restore left a consistent world)
	CHECK(loadError(g.logic(), { t }, one).empty());
	CHECK(g.logic().computeStateHash() == before);
}

TEST_CASE("save4c review r2: the skirmish AI's best target, a tactic's waypoint step, a build request's producer and an assist timer's player are checked "
		  "after they are read, refused by name, the live state unchanged")
{
	OPENBFME_REQUIRE_START(s);
	Game g;
	load(*s, fourPlayerMessage(*s, 4126), g);
	runTo(g, 50);
	const std::string t = "obfme:SkirmishAI";
	SkirmishAIManager &mgr = g.logic().skirmishAI();
	REQUIRE(!mgr.groups().empty());
	AISkirmishPlayer *ai = mgr.groups().front()->players.begin()->second.get();
	std::vector<std::uint8_t> original;
	g.logic().gameState().saveBlocks({ t }, original);
	const int players = g.logic().players().getPlayerCount();
	auto check = [&](const std::string &want) {
		std::vector<std::uint8_t> bad;
		g.logic().gameState().saveBlocks({ t }, bad); // a save writes what the state holds
		g.logic().gameState().loadBlocks({ t }, original);
		const std::uint32_t before = g.logic().computeStateHash();
		const std::vector<std::uint8_t> savedBefore = savedBlocks(g.logic());
		CHECK(loadError(g.logic(), { t }, bad) == "block '" + t + "': " + want);
		CHECK(g.logic().computeStateHash() == before);
		CHECK(savedBlocks(g.logic()) == savedBefore);
	};
	SUBCASE("best target")
	{
		ai->brain.bestTarget = 0x7FFFFFF0;
		check("skirmish ai: the best target 2147483632 is outside -1 .. " + std::to_string((long long)ai->brain.targets.size() - 1));
	}
	SUBCASE("waypoint step")
	{
		AITactic tac;
		tac.kind = "FlankAttack";
		tac.kindIndex = 2;
		tac.step = -1;
		ai->brain.tactics.push_back(tac);
		check("skirmish ai: a FlankAttack tactic's step -1 is outside 0 .. 0");
	}
	SUBCASE("request producer")
	{
		auto r = std::make_shared<AIBuildRequest>();
		r->producer = 0x7FFFFFF0;
		ai->build.pending.push_back(r);
		check("skirmish ai: the producer of a build request 2147483632 is an object id the game has not issued (the next is " +
			std::to_string(g.logic().getNextObjectID()) + ")");
	}
	SUBCASE("assist timer player")
	{
		mgr.spellBookAssistTimers().push_back({ 99, 123 });
		check("skirmish ai: the player of a spell book assist timer 99 is outside 0 .. " + std::to_string(players - 1));
	}
	SUBCASE("two first AIs in a group")
	{
		for (auto &kv : mgr.groups().front()->players)
		{
			kv.second->firstOfGroup = true;
			kv.second->brain.chooser = true;
		}
		const size_t n = mgr.groups().front()->players.size();
		if (n == 1)
		{
			ai->brain.chooser = false; // a group of one: break the chooser instead
			check("skirmish ai: player " + std::to_string(mgr.groups().front()->players.begin()->first) +
				"'s target chooser does not follow its place in the group");
		}
		else
		{
			check("skirmish ai: a group has " + std::to_string(n) + " first AIs");
		}
	}
	SUBCASE("assist timer twice")
	{
		mgr.spellBookAssistTimers().push_back({ 0, 1 });
		mgr.spellBookAssistTimers().push_back({ 0, 2 });
		check("skirmish ai: player 0 has two spell book assist timers");
	}
}

TEST_CASE("save4c review r2: a path's optimised link to its own node or to an earlier node is refused by name (a walk of the optimised chain could not end)")
{
	Path path;
	const Coord3D a{ 0, 0, 0 }, b{ 10, 0, 0 }, c{ 20, 0, 0 };
	path.appendNode(&a, LAYER_GROUND);
	path.appendNode(&b, LAYER_GROUND);
	path.appendNode(&c, LAYER_GROUND);
	XferSave xs;
	path.xfer(xs);
	// version, 2 flags, a real, a coord (19), the node count (+ 4): node i at 23 + 37 i, its optimised link at + 33
	auto refused = [&](const std::vector<std::uint8_t> &bad) {
		Path loaded;
		std::string error;
		try
		{
			XferLoad xl(bad);
			loaded.xfer(xl);
		}
		catch (const XferException &e)
		{
			error = e.what();
		}
		CHECK(loaded.getFirstNode() == nullptr); // nothing was linked into the path that was loaded
		return error;
	};
	CHECK(refused(withU32(xs.data(), 23 + 33, 0)) == "path: node 0's optimised link to node 0 of 3 does not run forward");
	CHECK(refused(withU32(xs.data(), 23 + 37 * 2 + 33, 1)) == "path: node 2's optimised link to node 1 of 3 does not run forward");
	CHECK(refused(withU32(xs.data(), 23 + 37 + 33, 3)) == "path: node 1's optimised link to node 3 of 3 does not run forward");
	Path ok;
	XferLoad xl(xs.data());
	ok.xfer(xl);
	CHECK(ok.nodeCount() == 3u);
}

TEST_CASE("save4c coverage (review r2): units attacking, moving, guarding, hunting and entering a garrison are saved mid-order and loaded into an identical "
		  "game whose AI modules first held their state of 300 frames earlier (the latest frame of 1500 .. 3000 where the actors exist): each unit's state, sub machine, goal and path, and its module's hash are the "
		  "saved ones, and both games go on equal")
{
	OPENBFME_REQUIRE_START(s);
	// lane BANNER-1 (the banner carriers refill the AIs' hordes as RotWK does): in seed 4127's game FactionMen (the attacker's and the garrison's player) has
	// no armed unit left from frame 1500 on (scanned 600 .. 4500), so the scene moves to seed 4132, whose frame 1500 holds the actors (seeds 4128 .. 4132
	// scanned: 4128 at 1200 only, 4129 .. 4131 none)
	// merge queue q28 (#117's fortress module in the update schedule moves the AIs' game): seed 4132 has no frame of 1500 .. 3000 holding the actors (the
	// #96 merge point alone still had them at 1500), so the scene moves to seed 4133 (scene at 1500); seeds 4133 .. 4138 measured on the merged tree: 4133 ..
	// 4137 hold the actors (1500 / 2100 / 1800 / 2700 / 2700), 4138's rider is no longer in its garrison order at the stepped frame
	// merge queue q29 (measured at each merge point: seed 4133 holds the actors through IDLEMOTION-1 and none from FBSIM-1 on, whose computer players' phantom
	// sites move the AIs' game): seed 4133 has no frame of 1500 .. 3000 holding the actors on the merged tree, so the scene moves to seed 4136 (scene at 3000);
	// seeds 4134 .. 4141 measured on the merged tree: 4136 / 4137 / 4138 / 4140 hold the actors (3000 / 1800 / 2700 / 1800); 4134, 4135, 4139 and 4141 fail
	// later in the scene (the attacker's order refused, the garrison rider not entering, an actor's order no longer in flight in the saved game)
	// merge queue q33 (measured on the merged tree: MODCOMPAT-MOD1 refuses the attacker's order at 4136's scene, and #133 leaves FactionMen no unit beside
	// the actors): seed 4136 fails; seeds 4136 .. 4145 measured there: 4137 / 4138 / 4143 / 4144 hold the actors (scene 3000 each); 4136, 4139 .. 4142 and 4145
	// fail (no unit beside the actors, no scene, the attacker's order refused, an actor's order no longer in flight), so the scene moves to seed 4137
	const NewGameMessage m = fourPlayerMessage(*s, 4137);
	// lane DEVPORT-7: the scene frame is found, not assumed. The test pinned 2700 / 3000, but which units of this 4-AI game live then is the AI's play,
	// not a retail fact: with BloodthirstyUpdate ported (an update module on every Mordor / Isengard horde, woken at creation and every 2 s + 1 frame as
	// retail's, RW 0x85123F / BFME2 0x44E0AA; it changes nothing in this game, but its update slots reorder the others', RW 0x62B921) the same seed has
	// FactionMen (the attacker's and the garrison's player) defeated at frame 2600. A first game of the same seed is scanned every 300 frames from 1500
	// to 3000 for the latest frame whose actors (below) exist among the objects alive 300 frames before; none: the test fails (it never runs on nothing)
	const int dozerKind = ObjectTemplateInfoBuilder::kindOfIndex("DOZER"), structureKind = ObjectTemplateInfoBuilder::kindOfIndex("STRUCTURE");
	const int hordeKind = ObjectTemplateInfoBuilder::kindOfIndex("HORDE");
	const int hordeMember = ObjectTemplateInfoBuilder::objectStatusIndex("HORDE_MEMBER");
	auto armed = [&](Object *o) { return !o->isKindOf((unsigned)dozerKind) && (o->getWeapons() != nullptr || o->isKindOf((unsigned)hordeKind)); };
	// the order's gate (RW 0x667174, side-effect free) must take the order: attack object 0xB, move 0, hunt 0x12
	auto takes = [](Object *o, int command) { return o->getAIUpdateInterface()->allowedToRespondToCommand(CMD_FROM_SCRIPT, command); };
	// the candidates of a player: mobile AI objects with a state machine, not horde members, alive at both frames
	auto candidates = [&](GameLogic &l, const std::set<ObjectID> &before) {
		std::map<int, std::vector<Object *>> out;
		for (Object *o = l.getFirstObject(); o; o = o->getNextObject())
		{
			AIUpdateInterface *ai = o->getAIUpdateInterface();
			if (ai && before.count(o->getID()) == 1 && o->getControllingPlayer() && !ai->isImmobile() && !(hordeMember >= 0 && o->testStatus((unsigned)hordeMember)) &&
				ai->stateMachineOrNull())
			{
				out[o->getControllingPlayer()->getPlayerIndex()].push_back(o);
			}
		}
		return out;
	};
	auto aiIds = [](GameLogic &l) {
		std::set<ObjectID> ids;
		for (Object *o = l.getFirstObject(); o; o = o->getNextObject())
		{
			if (o->getAIUpdateInterface())
			{
				ids.insert(o->getID());
			}
		}
		return ids;
	};
	// lane AIDATA-1: the hunter must have a victim for the hunt's first scan (AIHuntState::findVictim's search, RW 0x701443 with 0x4A over 9999.9; without
	// one the hunt ends at that scan, RW 0x7481F9)
	auto hasPrey = [](Object *o) {
		return AI::findClosestEnemy(*o, 9999.9f, AI::CAN_ATTACK | AI::ATTACK_BUILDINGS | AI::AUTO_ACQUIRE, o->getAIUpdateInterface()->attackInfo(), nullptr, 0) != nullptr;
	};
	UnsignedInt scene = 0;
	{
		Game c;
		load(*s, m, c);
		GameLogic &lc = c.logic();
		const std::vector<int> ids = lc.skirmishAI().infoPlayers();
		REQUIRE(ids.size() == 4u);
		for (UnsignedInt t = 1500; t <= 3000; t += 300)
		{
			runTo(c, t - 300);
			const std::set<ObjectID> before = aiIds(lc);
			runTo(c, t);
			std::map<int, std::vector<Object *>> by = candidates(lc, before);
			auto any = [&](int p, const std::function<bool(Object *)> &ok) {
				return std::any_of(by[p].begin(), by[p].end(), [&](Object *o) { return ok(o); });
			};
			int menUnits = 0;
			for (auto &kv : by)
			{
				const Player *p = lc.players().getNthPlayer(kv.first);
				menUnits += p && p->getPlayerTemplate() && p->getPlayerTemplate()->getName() == "FactionMen" ? (int)kv.second.size() : 0;
			}
			// the attacker (first player), the guard (third), the hunter (fourth, lane AIDATA-1: or any other player, see below), and a second unit of
			// FactionMen beside the attacker for the garrison
			bool hunterFound = false;
			for (int p : ids)
			{
				hunterFound = hunterFound || any(p, [&](Object *o) { return armed(o) && takes(o, 0x12) && GarrisonRules::isMobile(*o) && hasPrey(o); });
			}
			if (any(ids[0], [&](Object *o) { return armed(o) && takes(o, 0xB); }) && any(ids[2], [&](Object *o) { return takes(o, 0); }) && hunterFound &&
				menUnits >= 2)
			{
				scene = t;
			}
		}
	}
	REQUIRE_MESSAGE(scene != 0, "no frame of 1500 .. 3000 (every 300) has the actors among the objects alive 300 frames before"); // tolerance: 0 is "none found"; the frame itself is the AI's play
	MESSAGE("save4c units attacking: the scene is at frame " << scene << " (its modules' earlier state from frame " << scene - 300 << ")");
	// lane AIDATA-1: the hunter must also keep its hunt through the frame the test steps: a unit the skirmish AI is walking somewhere gets its next order from
	// the AI in that frame (the frame-2400 scene's only candidate of the fourth player, an Isengard pike horde, was back in AI_MOVE_TO). A scratch game of its
	// own, at the scene, gives every candidate of every player the hunt and steps once; the candidates still hunting are the ones the two games below may
	// choose, the fourth player's first, else the first of another player that is not an actor yet (the scratch game is gone before they load: one live game
	// per world, S-3808)
	std::set<ObjectID> keepsHunt;
	{
		Game d;
		load(*s, m, d);
		GameLogic &ld = d.logic();
		runTo(d, scene - 300);
		const std::set<ObjectID> before = aiIds(ld);
		runTo(d, scene);
		std::vector<ObjectID> tried;
		std::map<int, std::vector<Object *>> by = candidates(ld, before);
		for (Object *o : [&] {
				 std::vector<Object *> all;
				 for (int p : ld.skirmishAI().infoPlayers())
				 {
					 all.insert(all.end(), by[p].begin(), by[p].end());
				 }
				 return all;
			 }())
		{
			if (armed(o) && takes(o, 0x12) && GarrisonRules::isMobile(*o) && hasPrey(o) && o->getAIUpdateInterface()->aiHunt(CMD_FROM_SCRIPT))
			{
				tried.push_back(o->getID());
			}
		}
		step(d);
		for (ObjectID id : tried)
		{
			const Object *o = ld.findObjectByID(id);
			if (o && o->getAIUpdateInterface() && o->getAIUpdateInterface()->currentStateId() == AI_HUNT)
			{
				keepsHunt.insert(id);
			}
		}
	}
	REQUIRE(!keepsHunt.empty());
	const UnsignedInt pre = scene - 300;
	Game a, b;
	load(*s, m, a);
	load(*starttest::peerWorld(), m, b); // one live game per world (S-3808): the second game has its own
	runTo(a, pre);
	runTo(b, pre);
	// b's AI modules at the frame 300 before the scene (each module's own state, kept to replace b's later; the comments below say 2700 / 3000)
	std::map<ObjectID, std::vector<std::uint8_t>> older;
	for (Object *o = b.logic().getFirstObject(); o; o = o->getNextObject())
	{
		if (AIUpdateInterface *ai = o->getAIUpdateInterface())
		{
			XferSave xs;
			ai->xferAIState(xs);
			older[o->getID()] = xs.data();
		}
	}
	runTo(a, scene);
	runTo(b, scene);
	GameLogic &la = a.logic();
	// (merge queue q16: only objects that already lived at frame 2700 are candidates, since b's modules are put back to that frame by id; with BACKLOG-5's
	// move path the game builds units between frames 2700 and 3000, and the first unit of a player could be a new one)
	// the actors, chosen in a by rules both games share: four mobile AI objects of four different players that are not horde members (hordes and heroes),
	// the attacker's victim an object of another player, and a garrison: a mobile AI object and a container of its player that accepts it
	std::map<int, std::vector<Object *>> byPlayer;
	for (Object *o = la.getFirstObject(); o; o = o->getNextObject())
	{
		AIUpdateInterface *ai = o->getAIUpdateInterface();
		if (ai && older.count(o->getID()) == 1 && o->getControllingPlayer() && !ai->isImmobile() && !(hordeMember >= 0 && o->testStatus((unsigned)hordeMember)) && ai->stateMachineOrNull())
		{
			byPlayer[o->getControllingPlayer()->getPlayerIndex()].push_back(o);
		}
	}
	// one actor per player, by role: the attacker and the hunter armed (not a worker), the mover and the guard any mobile one; the victim an enemy
	// structure of the mover's player
	const std::vector<int> playerIds = la.skirmishAI().infoPlayers(); // the four skirmish players (the map's neutral critters are not actors)
	REQUIRE(playerIds.size() == 4u);
	// the hunter is preferably no member of its AI's tactic teams: a member takes that AI's team orders (AITacticalAI's team move: RW 0x8F18AA passes
	// the player's source), which may replace the hunt in the frame the test steps (measured: with the formations centred on their hordes, RW 0x86C4EA,
	// and the destination adjustment spiralling around its cell, RW 0x6FA2A4, the fourth player's first crossbow horde was a member and walked on in
	// AI_MOVE_TO). Only a preference: a player may have every armed unit in a team (the first player has, at the scene's frame), and the other roles
	// keep the first fit
	auto inAITeam = [&](const Object *o) {
		const AISkirmishPlayer *p = la.skirmishAI().findAI(o->getControllingPlayer()->getPlayerIndex());
		if (!p)
		{
			return false;
		}
		for (const AITactic &tac : p->brain.tactics)
		{
			for (const AITacticTeam &team : tac.teams)
			{
				if (std::find(team.members.begin(), team.members.end(), o->getID()) != team.members.end())
				{
					return true;
				}
			}
		}
		return false;
	};
	auto firstOf = [&](int player, const std::function<bool(Object *)> &ok) -> Object * {
		for (Object *o : byPlayer[player])
		{
			if (ok(o))
			{
				return o;
			}
		}
		return nullptr;
	};
	Object *attacker = firstOf(playerIds[0], [&](Object *o) { return armed(o) && takes(o, 0xB); });
	// the move order (300 east of the unit, as below) must be reachable, or it ends at once; and the unit must be standing without a path: a unit that just
	// computed one has its new request deferred (AIMover::requestPath's 2-frame rule, 5 frames later) and its old path's end can finish the order at once,
	// and a blocked unit patches its old path instead of searching (RW 0x6662FD .. 0x6665B5). Lane ATTACKPATH-1: the second player's first unit at frame
	// 2700 is a porter walking a path, so these rules apply to the first choice too
	auto reachableMover = [&](Object *o) {
		AIUpdateInterface *ai = o->getAIUpdateInterface();
		if (ai->mover().blockedFrames() > 0 || ai->mover().path() != nullptr)
		{
			return false;
		}
		const Coord3D from = *o->getPosition();
		Coord3D to = from;
		to.x += 300.0f;
		to.z = la.getGroundHeight(to.x, to.y);
		return ai->world().pathfinder().clientSafeQuickDoesPathExist(ai->locomotorInfo(), &from, &to);
	};
	Object *mover = firstOf(playerIds[1], [&](Object *o) { return takes(o, 0) && reachableMover(o); });
	Object *guard = firstOf(playerIds[2], [&](Object *o) { return takes(o, 0); });
	auto huntable = [&](Object *o) { return armed(o) && takes(o, 0x12) && GarrisonRules::isMobile(*o) && hasPrey(o) && keepsHunt.count(o->getID()) == 1; }; // RW 0x66450F's test
	Object *hunter = firstOf(playerIds[3], [&](Object *o) { return huntable(o) && !inAITeam(o); });
	hunter = hunter ? hunter : firstOf(playerIds[3], huntable);
	for (int p : playerIds)
	{
		Object *c = firstOf(p, [&](Object *o) { return huntable(o) && o != attacker && o != mover && o != guard; });
		hunter = hunter ? hunter : c;
	}
	if (!mover)
	{
		// merge queue q16: the second player has only horde members and new units at frame 2700 (nothing older to snapshot): its role goes to the first
		// older unit of any other player that is not an actor yet
		for (int p : playerIds)
		{
			// the move order (300 east of the unit, as below) must be reachable, or it ends at once
			Object *c = firstOf(p, [&](Object *o) { return takes(o, 0) && o != attacker && o != guard && o != hunter && reachableMover(o); });
			mover = mover ? mover : c;
		}
	}
	REQUIRE(attacker != nullptr);
	REQUIRE(mover != nullptr);
	REQUIRE(guard != nullptr);
	REQUIRE(hunter != nullptr);
	// the victim: the second player's first living structure the attacker's weapons may attack (aiAttackObject's own test, ObjectWeapons::canAttackObject;
	// review of #161: the AIs' game at frame 3,000 moves with every simulation change, and its first structure was one the attacker could not attack)
	Object *victim = nullptr;
	for (Object *o = la.getFirstObject(); o && !victim; o = o->getNextObject())
	{
		const bool candidate = o->getControllingPlayer() && o->getControllingPlayer()->getPlayerIndex() == playerIds[1] && o->isKindOf((unsigned)structureKind) &&
			!o->isEffectivelyDead();
		victim = candidate && attacker->getWeapons() && attacker->getWeapons()->canAttackObject(*o, CMD_FROM_SCRIPT, false) ? o : nullptr;
	}
	REQUIRE(victim != nullptr);
	// a garrison: Fall Back's skirmish has no garrisonable building, so both games get the same GondorKeep (the garrison tests' keep) for the Men player,
	// next to that player's first mobile unit that is not an actor; the rider is that player's first such unit the keep accepts
	Object *rider = nullptr, *garrison = nullptr;
	const ThingTemplate *keep = s->world->things().findTemplate("GondorKeep");
	REQUIRE(keep != nullptr);
	int men = -1;
	for (auto &kv : byPlayer)
	{
		const Player *p = la.players().getNthPlayer(kv.first);
		men = p && p->getPlayerTemplate() && p->getPlayerTemplate()->getName() == "FactionMen" ? kv.first : men;
	}
	REQUIRE(men != -1);
	std::vector<Object *> menUnits;
	for (Object *u : byPlayer[men])
	{
		if (u != attacker && u != mover && u != guard && u != hunter)
		{
			menUnits.push_back(u);
		}
	}
	REQUIRE(!menUnits.empty());
	ObjectID keepId = INVALID_ID;
	// the keep's spot: the first of the offsets from that unit (80 east, then the other directions and distances) where the unit's pathfinder reaches the
	// keep's four corners and its entry point (the entry offset is 50 east of the keep: HordeGarrisonContain's EntryOffset), so the garrison order is
	// legal (review r7: the same fixed spot, 80 east, lay on the neutral Amon Hen structures once the archive's AI changes moved the game's state at
	// frame 3,000, and the order was refused by RW 0x82CBD5's path test)
	Coord3D keepSpot{};
	{
		Object *near = menUnits.front();
		Pathfinder &pf = near->getAIUpdateInterface()->world().pathfinder();
		const Coord3D from = *near->getPosition();
		auto reaches = [&](float x, float y) {
			Coord3D q{ x, y, la.getGroundHeight(x, y) };
			return pf.clientSafeQuickDoesPathExist(near->getAIUpdateInterface()->locomotorInfo(), &from, &q);
		};
		bool found = false;
		for (float r : { 80.0f, 140.0f, 200.0f, 260.0f })
		{
			for (int d = 0; d < 8 && !found; ++d)
			{
				const float ang = (float)d * 0.78539816f;
				const float cx = from.x + r * std::cos(ang), cy = from.y + r * std::sin(ang);
				bool clear = reaches(cx + 50.0f, cy);
				for (float ox : { -40.0f, 40.0f })
					for (float oy : { -40.0f, 40.0f })
						clear = clear && reaches(cx + ox, cy + oy);
				if (clear)
				{
					keepSpot = Coord3D{ cx, cy, la.getGroundHeight(cx, cy) };
					found = true;
				}
			}
		}
		REQUIRE(found); // tolerance: a precondition (the first Men unit has open ground within 260 of it)
	}
	for (Game *g : { &a, &b })
	{
		GameLogic &l = g->logic();
		Object *k = l.newObject(keep, l.players().getNthPlayer(men)->getDefaultTeam(), ObjectStatusMaskType{});
		REQUIRE(k != nullptr);
		Coord3D p = keepSpot;
		k->setPosition(&p);
		testutil::completeHorde(k); // a horde makes its members where it stands (onBuildComplete, RW 0x8718ED)
		REQUIRE((keepId == INVALID_ID || keepId == k->getID()));
		keepId = k->getID();
	}
	garrison = la.findObjectByID(keepId);
	REQUIRE(garrison->getContain() != nullptr);
	for (Object *u : menUnits)
	{
		if (!rider && garrison->getContain()->isValidContainerFor(*u, true, false) && GarrisonRules::canEnterObject(*u, garrison, CMD_FROM_SCRIPT, 0, true) &&
			u->getAIUpdateInterface()->allowedToRespondToCommand(CMD_FROM_SCRIPT, 0x42))
		{
			rider = u;
		}
	}
	REQUIRE(rider != nullptr);
	// the same orders in both games (scripts' orders: the skirmish AI's own orders come as CMD_FROM_AI)
	for (Game *g : { &a, &b })
	{
		GameLogic &l = g->logic();
		auto ai = [&](Object *o) { return l.findObjectByID(o->getID())->getAIUpdateInterface(); };
		REQUIRE(ai(attacker)->aiAttackObject(l.findObjectByID(victim->getID()), CMD_FROM_SCRIPT));
		Coord3D far = *mover->getPosition();
		far.x += 300.0f;
		ai(mover)->aiMoveToPosition(far, CMD_FROM_SCRIPT);
		ai(guard)->guardHere(GUARDMODE_NORMAL);
		REQUIRE(ai(hunter)->aiHunt(CMD_FROM_SCRIPT));
		REQUIRE(ai(rider)->aiEnter(l.findObjectByID(garrison->getID()), CMD_FROM_SCRIPT));
	}
	step(a);
	step(b);
	REQUIRE(a.logic().computeStateHash() == b.logic().computeStateHash());
	struct Want
	{
		Object *object;
		unsigned state;
	};
	// the rider's garrison order is in flight: in the walk to the keep (56), or, merge queue q16, already in the enter state (15): with BACKLOG-5's move path the
	// rider's move to the keep's entry point is complete within the frame the test steps. Either is the garrison order saved mid-flight; anything else fails
	const unsigned riderState = la.findObjectByID(rider->getID())->getAIUpdateInterface()->currentStateId();
	CHECK((riderState == AI_MOVE_TO_POSITION_AND_ENTER || riderState == AI_ENTER));
	const std::vector<Want> wants = { { attacker, AI_ATTACK_OBJECT }, { mover, AI_MOVE_TO }, { guard, AI_GUARD }, { hunter, AI_HUNT },
		{ rider, riderState } };
	auto moduleHash = [](GameLogic &l, Object *o) {
		StateHasher h;
		l.findObjectByID(o->getID())->getAIUpdateInterface()->crc(h);
		return h.value();
	};
	// what each actor holds in a: its order's state, and that state's own machine (the attack's sub machine, the hunt's and the guard's)
	struct Held
	{
		unsigned state, sub;
		bool hasSub;
		ObjectID goal;
		int goalType;
		size_t pathNodes;
		std::uint32_t hash;
	};
	auto held = [&](GameLogic &l, Object *o) {
		AIUpdateInterface *ai = l.findObjectByID(o->getID())->getAIUpdateInterface();
		Held h{};
		h.state = ai->currentStateId();
		AIState *cur = ai->stateMachine().currentState();
		if (auto *att = dynamic_cast<AIAttackState *>(cur))
		{
			h.hasSub = att->attackMachine() != nullptr;
			h.sub = h.hasSub ? att->attackMachine()->currentStateId() : 0u;
		}
		else if (auto *hunt = dynamic_cast<AIHuntState *>(cur))
		{
			h.hasSub = hunt->huntMachine() != nullptr;
			h.sub = h.hasSub ? hunt->huntMachine()->currentStateId() : 0u;
		}
		else if (auto *g = dynamic_cast<AIGuardState *>(cur))
		{
			h.hasSub = g->guardMachine() != nullptr;
			h.sub = h.hasSub ? g->guardMachine()->currentStateId() : 0u;
		}
		h.goal = ai->stateMachine().goalObjectID();
		h.goalType = ai->mover().goalType();
		h.pathNodes = ai->mover().path() ? ai->mover().path()->nodeCount() : 0u;
		h.hash = moduleHash(l, o);
		return h;
	};
	std::vector<Held> inA;
	for (const Want &w : wants)
	{
		INFO("actor " << w.object->getTemplate()->getName() << " (" << w.object->getID() << ")");
		inA.push_back(held(la, w.object));
		CHECK(inA.back().state == w.state); // the order is in flight in the saved game
	}
	CHECK(inA[0].goal == victim->getID());
	CHECK(inA[0].hasSub); // the attack's sub machine runs
	CHECK(inA[2].hasSub); // the guard machine runs
	CHECK(inA[3].hasSub); // the hunt machine runs
	CHECK(inA[1].goalType != (int)AIGOAL_NONE);
	std::vector<std::uint8_t> saved;
	la.gameState().saveBlocks({ "obfme:AIWorld" }, saved);
	// b's modules go back to their frame-2700 state (where the module exists at both frames), so the actors' modules differ from a's
	for (Object *o = b.logic().getFirstObject(); o; o = o->getNextObject())
	{
		auto it = older.find(o->getID());
		if (it != older.end() && o->getAIUpdateInterface())
		{
			XferLoad xl(it->second);
			o->getAIUpdateInterface()->xferAIState(xl);
		}
	}
	for (const Want &w : wants)
	{
		INFO("actor " << w.object->getID());
		REQUIRE(older.count(w.object->getID()) == 1);
		CHECK(moduleHash(b.logic(), w.object) != moduleHash(la, w.object));
	}
	b.logic().gameState().loadBlocks({ "obfme:AIWorld" }, saved);
	for (size_t i = 0; i < wants.size(); ++i)
	{
		INFO("actor " << wants[i].object->getID());
		const Held h = held(b.logic(), wants[i].object);
		CHECK(h.state == inA[i].state);
		CHECK(h.sub == inA[i].sub);
		CHECK(h.hasSub == inA[i].hasSub);
		CHECK(h.goal == inA[i].goal);
		CHECK(h.goalType == inA[i].goalType);
		CHECK(h.pathNodes == inA[i].pathNodes);
		CHECK(h.hash == inA[i].hash);
	}
	CHECK(b.logic().computeStateHash() == la.computeStateHash());
	for (int i = 0; i < 150; ++i)
	{
		step(a);
		step(b);
		REQUIRE(a.logic().computeStateHash() == b.logic().computeStateHash());
	}
}

TEST_CASE("save4c hostile inputs, nested containers and caps (review r2): an oversized count inside a partition node, a shroud record's coverage and a skirmish "
		  "AI record's army list is refused by name; a shroud slot count within the bytes left but beyond the allocation budget is refused; the state is unchanged")
{
	OPENBFME_REQUIRE_START(s);
	Game g;
	load(*s, fourPlayerMessage(*s, 4126), g);
	runTo(g, 600);
	GameLogic &logic = g.logic();
	const std::uint32_t big = 0x7FFFFFF0u;
	auto refused = [&](const std::string &token, const std::vector<std::uint8_t> &bytes, const std::string &want) {
		INFO(token << ": " << want);
		const std::uint32_t before = logic.computeStateHash();
		const std::vector<std::uint8_t> savedBefore = savedBlocks(logic);
		CHECK(loadError(logic, { token }, bytes) == want);
		CHECK(logic.computeStateHash() == before);
		CHECK(savedBlocks(logic) == savedBefore);
	};
	{
		const std::string t = "obfme:Partition";
		std::vector<std::uint8_t> one;
		logic.gameState().saveBlocks({ t }, one);
		const size_t p = payloadOffset(t);
		const size_t dirtyAt = p + 26 + 20 * (size_t)u32At(one, p + 22);
		const size_t nodesAt = dirtyAt + 4 + 4 * (size_t)u32At(one, dirtyAt);
		REQUIRE(u32At(one, nodesAt) > 0u); // tolerance: a precondition (the trees hold nodes)
		const size_t listAt = nodesAt + 4 + 8; // node 0: its index, its count below, its list's count
		refused(t, withU32(one, listAt, big), "block '" + t + "': " + countError(big, 4, payloadSize(one, t) - (listAt + 4 - p)));
	}
	{
		const std::string t = "obfme:Shroud";
		std::vector<std::uint8_t> one;
		logic.gameState().saveBlocks({ t }, one);
		const size_t p = payloadOffset(t);
		const std::uint32_t cells = u32At(one, p + 43);
		size_t at = p + 47;
		for (int plane = 0; plane < 80; ++plane)
		{
			for (std::uint32_t covered = 0; covered < cells; at += 6)
			{
				covered += u32At(one, at);
			}
		}
		at += 4 + 32 * (size_t)u32At(one, at);
		const size_t slotsAt = at;
		at += 4;
		while (!one.at(at))
		{
			++at; // free slots
		}
		const size_t coveredAt = at + 71; // the first record in use: its coverage count
		refused(t, withU32(one, coveredAt, big), "block '" + t + "': " + countError(big, 4, payloadSize(one, t) - (coveredAt + 4 - p)));
		// a cell covered twice by one record: the first record in use whose coverage holds two cells (the second becomes the first)
		size_t rec = at;
		for (std::uint32_t slot = 0; slot < u32At(one, slotsAt); ++slot)
		{
			if (!one.at(rec))
			{
				rec += 1;
				continue;
			}
			if (u32At(one, rec + 71) >= 2u) // tolerance: the search's test (a record with two covered cells)
			{
				break;
			}
			rec = rec + 71 + 4 + 4 * (size_t)u32At(one, rec + 71) + 60 + 13;
		}
		REQUIRE(u32At(one, rec + 71) >= 2u); // tolerance: a precondition (some record covers two cells)
		const std::uint32_t firstCell = u32At(one, rec + 71 + 4);
		refused(t, withU32(one, rec + 71 + 8, firstCell), "block '" + t + "': shroud: object " + std::to_string(u32At(one, rec + 1)) + " covers the cell " +
			std::to_string(firstCell) + " twice");
		// as many slots as there are bytes left (each at least its in-use byte): within the encoded budget, beyond the allocation budget (a record's memory
		// is far more than 64 bytes per byte of stream)
		const std::uint32_t left = (std::uint32_t)(payloadSize(one, t) - (slotsAt + 4 - p));
		const std::string error = loadError(logic, { t }, withU32(one, slotsAt, left));
		const std::string head = "block '" + t + "': the load would allocate ", tail = " bytes, more than 64 times the stream's " + std::to_string(one.size());
		CHECK(error.rfind(head, 0) == 0);
		CHECK(error.size() > head.size() + tail.size()); // tolerance: the allocated amount between the two fixed parts is the load's own accounting
		CHECK(error.compare(error.size() - tail.size(), tail.size(), tail) == 0);
	}
	{
		const std::string t = "obfme:SkirmishAI";
		std::vector<std::uint8_t> one;
		logic.gameState().saveBlocks({ t }, one);
		const size_t p = payloadOffset(t), assist = p + 10;
		const size_t info = assist + 4 + 8 * (size_t)u32At(one, assist) + 5;
		const size_t records = info + 4 + 4 * (size_t)u32At(one, info);
		REQUIRE(u32At(one, records) > 0u); // tolerance: a precondition (the game has player info records)
		const size_t army = records + 4 + 4 + 1; // record 0: its player, the collector's version, its army list's count
		refused(t, withU32(one, army, big), "block '" + t + "': " + countError(big, 4, payloadSize(one, t) - (army + 4 - p)));
	}
}

// ---- review r3 (Sol): framing inside the transaction, the views' presence, ownership of live references, the flight locomotor ----

namespace
{
// a one-block stream with one more payload byte: a zero at the block's end, the block's end offset moved past it
std::vector<std::uint8_t> withExtraByte(std::vector<std::uint8_t> one, const std::string &token)
{
	const size_t endAt = payloadOffset(token) - 4;
	const std::uint32_t end = u32At(one, endAt);
	one.insert(one.begin() + end, (std::uint8_t)0);
	return withU32(one, endAt, end + 1);
}

// the offset of the first AI module's body in an obfme:AIWorld stream (after its "BLOK" and end offset)
size_t firstModuleBody(const std::vector<std::uint8_t> &one)
{
	const std::uint8_t blok[4] = { 'K', 'O', 'L', 'B' };
	for (size_t i = payloadOffset("obfme:AIWorld"); i + 4 <= one.size(); ++i)
	{
		if (std::memcmp(&one[i], blok, 4) == 0)
		{
			return i + 8;
		}
	}
	FAIL("no AI module in the block");
	return 0;
}

AISkirmishPlayer *aiOfSide(GameLogic &l, const std::string &side)
{
	for (const auto &g : l.skirmishAI().groups())
	{
		for (const auto &kv : g->players)
		{
			if (l.players().getNthPlayer(kv.first)->getSide() == side)
			{
				return kv.second.get();
			}
		}
	}
	return nullptr;
}
} // namespace

TEST_CASE("save4c review r3: a block with one byte after its state is refused by name before anything stays changed (the framing check is inside the "
		  "transaction / before the commit): the hash and the next save's bytes are as before, for each of the five blocks")
{
	OPENBFME_REQUIRE_START(s);
	Game g;
	load(*s, fourPlayerMessage(*s, 4126), g);
	runTo(g, 300);
	GameLogic &logic = g.logic();
	const std::uint32_t before = logic.computeStateHash();
	const std::vector<std::uint8_t> savedBefore = savedBlocks(logic);
	for (const std::string &t : k4c)
	{
		INFO(t);
		std::vector<std::uint8_t> one;
		logic.gameState().saveBlocks({ t }, one);
		if (t == "obfme:AIWorld")
		{
			const size_t body = firstModuleBody(one);
			one = withU32(one, body + 1, u32At(one, body + 1) ^ 0x01000000u); // a valid change of a module (a Lua snapshot bit): it must not stay
		}
		const std::string error = loadError(logic, { t }, withExtraByte(one, t));
		if (t == "obfme:AIWorld")
		{
			CHECK(error == "block '" + t + "': ai world: 1 bytes of the block are left after its state");
		}
		else if (t == "obfme:SkirmishAI")
		{
			CHECK(error == "block '" + t + "': skirmish ai: 1 bytes of the block are left after its state");
		}
		else
		{
			// the blocks that commit in their post-process: GameState's own check stops the load before it
			const std::uint32_t end = u32At(one, payloadOffset(t) - 4);
			CHECK(error == "block '" + t + "': a block's load read less than it holds (at offset " + std::to_string(end) + ", the block ends at " +
				std::to_string(end + 1) + ")");
		}
		CHECK(logic.computeStateHash() == before);
		CHECK(savedBlocks(logic) == savedBefore);
	}
}

TEST_CASE("save4c review r3: a pathfinder view's presence is state: a refused AI world load leaves no view it made (the next save's bytes are the same), "
		  "and a loaded one gives exactly the saved views")
{
	OPENBFME_REQUIRE_START(s);
	Game a, b;
	const NewGameMessage m = fourPlayerMessage(*s, 4126);
	load(*s, m, a);
	load(*starttest::peerWorld(), m, b); // one live game per world (S-3808): the second game has its own
	runTo(a, 50);
	runTo(b, 50);
	Object *extra = nullptr;
	for (Object *o = a.logic().getFirstObject(); o && !extra; o = o->getNextObject())
	{
		extra = a.logic().aiWorld()->findAdapter(o->getID()) ? nullptr : o;
	}
	REQUIRE(extra != nullptr);
	const ObjectID id = extra->getID();
	REQUIRE(b.logic().aiWorld()->findAdapter(id) == nullptr);
	a.logic().aiWorld()->adapterFor(*extra); // a has one view more than b
	const std::string t = "obfme:AIWorld";
	std::vector<std::uint8_t> original, good, after;
	b.logic().gameState().saveBlocks({ t }, original);
	a.logic().gameState().saveBlocks({ t }, good);
	REQUIRE(good != original);
	const std::vector<std::uint8_t> bad = withU32(good, firstModuleBody(good) + 77, 99); // refused after the views were read
	const std::uint32_t before = b.logic().computeStateHash();
	const std::string name = b.logic().findObjectByID(u32At(good, payloadOffset(t) + 8))->getTemplate()->getName();
	CHECK(loadError(b.logic(), { t }, bad) == "block '" + t + "': the AI of object " + std::to_string(u32At(good, payloadOffset(t) + 8)) + " (" + name +
		"): ai: the last command's source 99 is outside 0 .. 2");
	CHECK(b.logic().computeStateHash() == before);
	CHECK(b.logic().aiWorld()->findAdapter(id) == nullptr);
	b.logic().gameState().saveBlocks({ t }, after);
	CHECK(after == original);
	// the intact block: b gets a's views exactly
	b.logic().gameState().loadBlocks({ t }, good);
	CHECK(b.logic().aiWorld()->findAdapter(id) != nullptr);
	b.logic().gameState().saveBlocks({ t }, after);
	CHECK(after == good);
	// and back: the view b's own snapshot does not name goes again
	b.logic().gameState().loadBlocks({ t }, original);
	CHECK(b.logic().aiWorld()->findAdapter(id) == nullptr);
	b.logic().gameState().saveBlocks({ t }, after);
	CHECK(after == original);
}

TEST_CASE("save4c review r3: a live factory of another player in an AI's unit builder is refused by name (it could queue units with the rival's money); "
		  "a factory that changes owner leaves the old owner's unit builder (RW 0x8F0C69 -> 0x9A051D), so no saved game holds one")
{
	OPENBFME_REQUIRE_START(s);
	Game g;
	load(*s, fourPlayerMessage(*s, 4126), g);
	runTo(g, 50);
	GameLogic &l = g.logic();
	const std::string t = "obfme:SkirmishAI";
	AISkirmishPlayer *ai = aiOfSide(l, "Men");
	REQUIRE(ai != nullptr);
	const int aiPlayer = ai->playerIndex; // (a load makes the AIs again: `ai` is looked up again after each)
	Player *rival = nullptr;
	for (int p : l.skirmishAI().infoPlayers())
	{
		rival = p != aiPlayer ? l.players().getNthPlayer(p) : rival;
	}
	REQUIRE(rival != nullptr);
	const ThingTemplate *barracks = s->world->things().findTemplate("GondorBarracks");
	REQUIRE(barracks != nullptr);
	auto place = [&](Player &owner, float x) {
		Object *f = l.newObject(barracks, owner.getDefaultTeam(), ObjectStatusMaskType{});
		REQUIRE(f != nullptr);
		Coord3D pos{ x, 800.0f, l.getGroundHeight(x, 800.0f) };
		f->setPosition(&pos);
		testutil::completeHorde(f); // a horde makes its members where it stands (onBuildComplete, RW 0x8718ED)
		return f;
	};
	SUBCASE("a rival's factory in the snapshot")
	{
		Object *f = place(*rival, 1500.0f);
		REQUIRE(f->getProductionUpdate() != nullptr);
		const int cash = rival->getMoney()->countMoney();
		std::vector<std::uint8_t> original, bad;
		l.gameState().saveBlocks({ t }, original);
		ai->build.units.factories.emplace(ai->store->keyOf("GondorFighterHorde"), f->getID());
		auto req = std::make_shared<AIBuildRequest>();
		req->unit = true;
		req->templateName = "GondorFighterHorde";
		req->producer = f->getID();
		ai->build.pending.push_back(req);
		l.gameState().saveBlocks({ t }, bad);
		l.gameState().loadBlocks({ t }, original);
		const std::uint32_t before = l.computeStateHash();
		const std::vector<std::uint8_t> savedBefore = savedBlocks(l);
		CHECK(loadError(l, { t }, bad) == "block '" + t + "': skirmish ai: a unit factory " + std::to_string(f->getID()) + " belongs to player " +
			std::to_string(rival->getPlayerIndex()) + ", not to the AI's player " + std::to_string(aiPlayer));
		CHECK(l.computeStateHash() == before);
		CHECK(savedBlocks(l) == savedBefore);
		// the game goes on without a unit queued at the rival's barracks or a coin of its money spent by this AI
		ai = aiOfSide(l, "Men");
		AIBaseBuilder::update(l, *ai);
		CHECK(f->getProductionUpdate()->getProductionCount() == 0);
		CHECK(rival->getMoney()->countMoney() == cash);
	}
	SUBCASE("a factory that changes owner")
	{
		Player *own = l.players().getNthPlayer(aiPlayer);
		Object *f = place(*own, 1500.0f);
		const NameKeyType key = ai->store->keyOf("GondorFighterHorde");
		ai->build.units.factories.emplace(key, f->getID()); // as RW 0x9A0838 lists a finished structure of the player
		std::vector<std::uint8_t> owned;
		l.gameState().saveBlocks({ t }, owned);
		CHECK(loadError(l, { t }, owned).empty()); // the AI's own live factory loads
		ai = aiOfSide(l, "Men");
		f->setTeam(rival->getDefaultTeam()); // the capture's owner change (RW 0x6A99CB with the old owner, RW 0x6A9D9C with the new)
		bool listed = false;
		for (const auto &kv : ai->build.units.factories)
		{
			listed = listed || kv.second == f->getID();
		}
		CHECK_FALSE(listed);
		std::vector<std::uint8_t> afterCapture;
		l.gameState().saveBlocks({ t }, afterCapture);
		CHECK(loadError(l, { t }, afterCapture).empty()); // what the game holds after a capture loads
	}
}

TEST_CASE("save4c review r3: a giant bird saved mid-flight: its module (the flight locomotor's preferred height, lift cap and hover included) loads into "
		  "the same bird of an identical game holding an earlier state, saves the same bytes, has the same hash, and both games stay equal for 75 frames")
{
	OPENBFME_REQUIRE_START(s);
	Game a, b;
	const NewGameMessage m = fourPlayerMessage(*s, 4126);
	load(*s, m, a);
	load(*starttest::peerWorld(), m, b); // one live game per world (S-3808): the second game has its own
	runTo(a, 50);
	runTo(b, 50);
	ObjectID id = INVALID_ID;
	for (Game *g : { &a, &b })
	{
		GameLogic &l = g->logic();
		AISkirmishPlayer *mordor = aiOfSide(l, "Mordor");
		REQUIRE(mordor != nullptr);
		// merge queue q23: each game's bird is made from its own world's template: TARGET-1's AttackPriority tables are keyed by the template (RW 0x603AF6), so a
		// bird of the other world's template was not found in game B's table and B's creeps chose their targets differently from A's (the games parted at frame 124)
		const ThingTemplate *beast = l.things().findTemplate("MordorWitchKingOnFellBeast");
		REQUIRE(beast != nullptr);
		Object *o = l.newObject(beast, l.players().getNthPlayer(mordor->playerIndex)->getDefaultTeam(), ObjectStatusMaskType{});
		REQUIRE(o != nullptr);
		Coord3D pos{ 1500.0f, 800.0f, l.getGroundHeight(1500.0f, 800.0f) };
		o->setPosition(&pos);
		testutil::completeHorde(o); // a horde makes its members where it stands (onBuildComplete, RW 0x8718ED)
		REQUIRE((id == INVALID_ID || id == o->getID()));
		id = o->getID();
	}
	auto bird = [&](Game &g) {
		GiantBirdAIUpdate *ai = dynamic_cast<GiantBirdAIUpdate *>(g.logic().findObjectByID(id)->getAIUpdateInterface());
		REQUIRE(ai != nullptr);
		return ai;
	};
	step(a);
	step(b);
	for (Game *g : { &a, &b })
	{
		bird(*g)->aiMoveToPosition(Coord3D{ 2400.0f, 800.0f, 0.0f }, CMD_FROM_SCRIPT);
	}
	step(a);
	step(b);
	XferSave earlier;
	bird(b)->xferAIState(earlier);
	for (int i = 0; i < 15; ++i)
	{
		step(a);
		step(b);
	}
	REQUIRE(a.logic().computeStateHash() == b.logic().computeStateHash());
	GiantBirdAIUpdate *ai = bird(a), *bi = bird(b);
	REQUIRE(ai->spline().valid());
	REQUIRE(ai->currentStateId() == (unsigned)GiantBirdAIUpdate::STATE_FLY_TO); // mid-flight
	REQUIRE(ai->curLocomotor() != nullptr);
	CHECK(ai->curLocomotor()->preferredHeight() != 0.0f); // tolerance: a precondition (the flight locomotor's own field is not its default)
	XferSave saved;
	ai->xferAIState(saved);
	{
		XferLoad xl(earlier.data());
		bi->xferAIState(xl); // b's bird holds its state of 15 frames earlier
	}
	StateHasher ha, hb;
	ai->crc(ha);
	bi->crc(hb);
	CHECK(ha.value() != hb.value());
	{
		XferLoad xl(saved.data());
		bi->xferAIState(xl);
		CHECK(xl.atEnd());
	}
	XferSave again;
	bi->xferAIState(again);
	CHECK(again.data() == saved.data());
	StateHasher ha2, hb2, la, lb;
	ai->crc(ha2);
	bi->crc(hb2);
	CHECK(ha2.value() == hb2.value());
	ai->curLocomotor()->crc(la);
	bi->curLocomotor()->crc(lb);
	CHECK(la.value() == lb.value());
	CHECK(bi->curLocomotor()->preferredHeight() == ai->curLocomotor()->preferredHeight());
	CHECK(a.logic().computeStateHash() == b.logic().computeStateHash());
	for (int i = 0; i < 75; ++i)
	{
		step(a);
		step(b);
		REQUIRE(a.logic().computeStateHash() == b.logic().computeStateHash());
	}
}

TEST_CASE("stealthfly1: a giant bird saved on its queued waypoints (follow path state 0x3F6, two points left) loads into the same bird of an identical game, "
		  "saves the same bytes and both games stay equal for 75 frames; a machine saved before a build added states loads (they keep their constructor's fields)")
{
	OPENBFME_REQUIRE_START(s);
	Game a, b;
	const NewGameMessage m = fourPlayerMessage(*s, 4126);
	load(*s, m, a);
	load(*starttest::peerWorld(), m, b);
	runTo(a, 50);
	runTo(b, 50);
	ObjectID id = INVALID_ID;
	for (Game *g : { &a, &b })
	{
		GameLogic &l = g->logic();
		AISkirmishPlayer *mordor = aiOfSide(l, "Mordor");
		REQUIRE(mordor != nullptr);
		// merge queue q23: each game's bird is made from its own world's template: TARGET-1's AttackPriority tables are keyed by the template (RW 0x603AF6), so a
		// bird of the other world's template was not found in game B's table and B's creeps chose their targets differently from A's (the games parted at frame 124)
		const ThingTemplate *beast = l.things().findTemplate("MordorWitchKingOnFellBeast");
		REQUIRE(beast != nullptr);
		Object *o = l.newObject(beast, l.players().getNthPlayer(mordor->playerIndex)->getDefaultTeam(), ObjectStatusMaskType{});
		REQUIRE(o != nullptr);
		Coord3D pos{ 1500.0f, 800.0f, l.getGroundHeight(1500.0f, 800.0f) };
		o->setPosition(&pos);
		testutil::completeHorde(o); // a horde makes its members where it stands (onBuildComplete, RW 0x8718ED)
		REQUIRE((id == INVALID_ID || id == o->getID()));
		id = o->getID();
	}
	auto bird = [&](Game &g) {
		GiantBirdAIUpdate *ai = dynamic_cast<GiantBirdAIUpdate *>(g.logic().findObjectByID(id)->getAIUpdateInterface());
		REQUIRE(ai != nullptr);
		return ai;
	};
	step(a);
	step(b);
	for (Game *g : { &a, &b })
	{
		bird(*g)->aiMoveToPosition(Coord3D{ 1900.0f, 800.0f, 0.0f }, CMD_FROM_PLAYER);
		bird(*g)->aiFollowPathAppend(Coord3D{ 1900.0f, 1200.0f, 0.0f }, CMD_FROM_PLAYER);
		bird(*g)->aiFollowPathAppend(Coord3D{ 1500.0f, 1200.0f, 0.0f }, CMD_FROM_PLAYER);
	}
	step(a);
	step(b);
	XferSave earlier;
	bird(b)->xferAIState(earlier);
	for (int i = 0; i < 15; ++i)
	{
		step(a);
		step(b);
	}
	REQUIRE(a.logic().computeStateHash() == b.logic().computeStateHash());
	GiantBirdAIUpdate *ai = bird(a), *bi = bird(b);
	REQUIRE(ai->currentStateId() == (unsigned)GiantBirdAIUpdate::STATE_FOLLOW_PATH);
	REQUIRE(ai->stateMachine().goalPath().size() == 3);
	XferSave saved;
	ai->xferAIState(saved);
	{
		XferLoad xl(earlier.data());
		bi->xferAIState(xl);
	}
	{
		XferLoad xl(saved.data());
		bi->xferAIState(xl);
		CHECK(xl.atEnd());
	}
	XferSave again;
	bi->xferAIState(again);
	CHECK(again.data() == saved.data());
	StateHasher ha, hb;
	ai->crc(ha);
	bi->crc(hb);
	CHECK(ha.value() == hb.value());
	CHECK(a.logic().computeStateHash() == b.logic().computeStateHash());
	for (int i = 0; i < 75; ++i)
	{
		step(a);
		step(b);
		REQUIRE(a.logic().computeStateHash() == b.logic().computeStateHash());
	}

	// SAVES.md section 5, mechanism 1: an older build's machine had fewer states. Saved {5, 7}: loads into {5, 6, 7} (6 keeps its fields), refused by {5, 8}
	// (no 7) and by {5} (more saved states than it has)
	auto machine = [&](const std::vector<unsigned> &ids) {
		auto mm = std::make_unique<AIStateMachine>(*ai, "STEALTHFLY1Machine");
		for (unsigned i : ids)
		{
			mm->defineState(i, std::make_unique<AIWaitState>(*mm), i, i);
		}
		return mm;
	};
	auto older = machine({ 5, 7 });
	older->setGoalPosition(Coord3D{ 12.0f, 34.0f, 56.0f });
	older->setGoalPath({ Coord3D{ 1.0f, 2.0f, 3.0f }, Coord3D{ 4.0f, 5.0f, 6.0f } });
	XferSave old;
	older->xfer(old);
	{
		auto newer = machine({ 5, 6, 7 });
		XferLoad xl(old.data());
		newer->xfer(xl);
		CHECK(xl.atEnd());
		CHECK(newer->goalPosition().y == 34.0f);
		CHECK(newer->goalPath().size() == 2);
	}
	const std::vector<std::vector<unsigned>> refusing = { { 5, 8 }, { 5 } };
	for (const std::vector<unsigned> &ids : refusing)
	{
		auto other = machine(ids);
		XferLoad xl(old.data());
		CHECK_THROWS_AS(other->xfer(xl), XferException);
	}
}

// keep one factory's entries only (a test's own change, the same in every game it runs in)
void onlyFactory(AIUnitBuilderState &u, ObjectID id)
{
	for (auto it = u.factories.begin(); it != u.factories.end();)
	{
		it = it->second != id ? u.factories.erase(it) : std::next(it);
	}
}

// the entries of one factory for one unit (the AI's own structures are listed from their placement, RW 0x8F06BF, so a key has several factories)
size_t entriesOf(const AIUnitBuilderState &u, NameKeyType key, ObjectID id)
{
	size_t n = 0;
	auto range = u.factories.equal_range(key);
	for (auto it = range.first; it != range.second; ++it)
	{
		n += it->second == id ? 1u : 0u;
	}
	return n;
}

TEST_CASE("save4c review r4: a capture's snapshot loads and the game goes on as the uninterrupted one: a captured factory (its AI's composition computed "
		  "again, RW 0x9A051D), a captured dispatched unit and a recaptured barracks (registered again, RW 0x8F06BF -> 0x9A0838, and queueing units again)")
{
	OPENBFME_REQUIRE_START(s);
	const NewGameMessage m = fourPlayerMessage(*s, 9021);
	Game a, b; // a goes on uninterrupted; b loads a's snapshot over an older state of its own and goes on
	starttest::Shared *peer = starttest::peerWorld(); // one live game per world (S-3808)
	// a game runs in its world's context: the command sets the AI's factories are found by live in the process-wide stores
	auto in = [&](Game &g, const std::function<void()> &fn) {
		auto scope = (&g == &a ? s : peer)->world->enterContext();
		fn();
	};
	load(*s, m, a);
	load(*peer, m, b);
	in(a, [&] { runTo(a, 50); });
	in(b, [&] { runTo(b, 50); });
	std::vector<std::uint8_t> older; // b's own skirmish AI before the capture: the state the snapshot replaces
	b.logic().gameState().saveBlocks({ "obfme:SkirmishAI" }, older);
	auto barracksOf = [](GameLogic &l, AISkirmishPlayer &ai) {
		const ThingTemplate *tt = l.things().findTemplate("GondorBarracks");
		REQUIRE(tt != nullptr);
		Object *f = l.newObject(tt, l.players().getNthPlayer(ai.playerIndex)->getDefaultTeam(), ObjectStatusMaskType{});
		REQUIRE(f != nullptr);
		Coord3D pos{ 1500.0f, 800.0f, l.getGroundHeight(1500.0f, 800.0f) };
		f->setPosition(&pos);
		testutil::completeHorde(f); // a horde makes its members where it stands (onBuildComplete, RW 0x8718ED)
		return f;
	};
	auto listed = [](const AISkirmishPlayer &ai, ObjectID id) {
		bool in = false;
		for (const auto &kv : ai.build.units.factories)
		{
			in = in || kv.second == id;
		}
		return in;
	};
	// the same legal events in both games; `check` is the scenario's own expectations on game a
	auto scenario = [&](const std::function<void(Game &, bool check)> &events) {
		in(a, [&] { events(a, true); });
		in(b, [&] { events(b, false); });
		REQUIRE(a.logic().computeStateHash() == b.logic().computeStateHash());
		std::vector<std::uint8_t> saved;
		a.logic().gameState().saveBlocks(k4c, saved);
		std::string e;
		in(b, [&] { e = loadError(b.logic(), { "obfme:SkirmishAI" }, older); }); // b holds a state of before the capture ...
		REQUIRE(e.empty());
		REQUIRE(b.logic().computeStateHash() != a.logic().computeStateHash());
		in(b, [&] { e = loadError(b.logic(), k4c, saved); }); // ... and loads the snapshot saved after it
		INFO(e);
		REQUIRE(e.empty());
		REQUIRE(b.logic().computeStateHash() == a.logic().computeStateHash());
		for (int i = 0; i < 150; ++i)
		{
			in(a, [&] { step(a); });
			in(b, [&] { step(b); });
			REQUIRE(a.logic().computeStateHash() == b.logic().computeStateHash());
		}
	};
	SUBCASE("a captured factory")
	{
		scenario([&](Game &g, bool check) {
			GameLogic &l = g.logic();
			AISkirmishPlayer *men = aiOfSide(l, "Men"), *mordor = aiOfSide(l, "Mordor");
			REQUIRE(men != nullptr);
			REQUIRE(mordor != nullptr);
			Object *f = barracksOf(l, *men);
			AIBaseBuilder::update(l, *men);
			REQUIRE(listed(*men, f->getID()));
			onlyFactory(men->build.units, f->getID()); // the AI's own barracks under construction is listed too (RW 0x8F06BF): the captured one alone
			men->build.units.recompute = false;
			f->setTeam(l.players().getNthPlayer(mordor->playerIndex)->getDefaultTeam()); // the capture's owner change
			if (check)
			{
				CHECK_FALSE(listed(*men, f->getID()));
				CHECK(men->build.units.recompute); // RW 0x9A055D
			}
			AIBaseBuilder::update(l, *men);
			if (check)
			{
				CHECK(men->build.units.composition[men->store->keyOf("GondorFighterHorde")] == 0.0f); // no barracks left to build it
			}
		});
	}
	SUBCASE("a captured dispatched unit")
	{
		scenario([&](Game &g, bool check) {
			GameLogic &l = g.logic();
			AISkirmishPlayer *men = aiOfSide(l, "Men"), *mordor = aiOfSide(l, "Mordor");
			REQUIRE(men != nullptr);
			REQUIRE(mordor != nullptr);
			const ThingTemplate *tt = l.things().findTemplate("GondorFighterHorde");
			REQUIRE(tt != nullptr);
			Object *u = l.newObject(tt, l.players().getNthPlayer(men->playerIndex)->getDefaultTeam(), ObjectStatusMaskType{});
			REQUIRE(u != nullptr);
			Coord3D pos{ 1500.0f, 800.0f, l.getGroundHeight(1500.0f, 800.0f) };
			u->setPosition(&pos);
			testutil::completeHorde(u); // a horde makes its members where it stands (onBuildComplete, RW 0x8718ED)
			// lane TARGET-1: held by NO_AUTO_ACQUIRE: a computer player's idle unit takes what it sees (RotWK's mood scan, RW 0x66844A), and from this spot the
			// horde charged what it saw 460 to the east and died there before frame 351; the dispatch (RW 0x9A3040) does not read the status
			u->setStatusAcrossHorde((unsigned)ObjectTemplateInfoBuilder::objectStatusIndex("NO_AUTO_ACQUIRE"), true);
			men->disabled = true; // the team builder takes the idle unit once its AI runs again (RW 0x9A3040)
			runTo(g, 351);
			men = aiOfSide(l, "Men");
			men->disabled = false;
			men->brain.tactics.clear();
			AITacticalAI::addAttackedAtForTest(l, *men, *u->getPosition());
			AITacticalAI::updateTeamBuilder(l, *men);
			REQUIRE(men->brain.dispatched.count(u->getID()) == 1u);
			u->setTeam(l.players().getNthPlayer(mordor->playerIndex)->getDefaultTeam());
			if (check)
			{
				CHECK(men->brain.dispatched.count(u->getID()) == 1u); // the dispatch history keeps it (it orders nothing)
				for (const AITactic &tac : men->brain.tactics)
				{
					for (const AITacticTeam &team : tac.teams)
					{
						CHECK(std::find(team.members.begin(), team.members.end(), u->getID()) == team.members.end());
					}
				}
			}
		});
	}
	SUBCASE("a recaptured barracks")
	{
		scenario([&](Game &g, bool check) {
			GameLogic &l = g.logic();
			AISkirmishPlayer *men = aiOfSide(l, "Men"), *mordor = aiOfSide(l, "Mordor");
			REQUIRE(men != nullptr);
			REQUIRE(mordor != nullptr);
			Object *f = barracksOf(l, *men);
			AIBaseBuilder::update(l, *men);
			f->setTeam(l.players().getNthPlayer(mordor->playerIndex)->getDefaultTeam());
			AIBaseBuilder::update(l, *men);
			f->setTeam(l.players().getNthPlayer(men->playerIndex)->getDefaultTeam()); // captured back
			if (check)
			{
				CHECK(listed(*men, f->getID())); // in the event (RW 0x8F06BF -> 0x9A0838), before any update
			}
			AIBaseBuilder::update(l, *men);
		});
		// after the load both games' AIs queue a GondorFighterHorde at the recaptured barracks for a request of their own
		for (Game *g : { &a, &b })
		{
			auto scope = (g == &a ? s : peer)->world->enterContext();
			GameLogic &l = g->logic();
			AISkirmishPlayer *men = aiOfSide(l, "Men");
			REQUIRE(men != nullptr);
			Object *f = nullptr;
			for (Object *o = l.getFirstObject(); o; o = o->getNextObject())
			{
				f = listed(*men, o->getID()) && o->getTemplate()->getName() == "GondorBarracks" ? o : f;
			}
			REQUIRE(f != nullptr);
			ProductionUpdateInterface *production = f->getProductionUpdate();
			REQUIRE(production != nullptr);
			production->cancelAllProduction();
			l.players().getNthPlayer(men->playerIndex)->depositMoney(10000, false);
			men->build.units.disabled = true;
			men->build.baseBuilderDisabled = true;
			men->build.economy.disabled = true;
			men->build.pending.clear();
			onlyFactory(men->build.units, f->getID()); // the recaptured barracks alone
			auto request = std::make_shared<AIBuildRequest>();
			request->unit = true;
			request->templateName = "GondorFighterHorde";
			request->priority = 1.0f;
			men->build.pending.push_back(request);
			AIBaseBuilder::update(l, *men);
			CHECK(production->countUnitTypeInQueue(l.things().findTemplate("GondorFighterHorde")) == 1);
		}
		REQUIRE(a.logic().computeStateHash() == b.logic().computeStateHash());
		// and the queued horde comes out of the recaptured barracks in both games: a GondorFighterHorde of the AI's player made by it
		auto madeBy = [&](GameLogic &l) {
			AISkirmishPlayer *men = aiOfSide(l, "Men");
			int made = 0;
			for (const Object *o = l.getFirstObject(); o; o = o->getNextObject())
			{
				made += o->getTemplate()->getName() == "GondorFighterHorde" && o->getControllingPlayer() &&
					o->getControllingPlayer()->getPlayerIndex() == men->playerIndex && l.findObjectByID(o->getProducerID()) &&
					l.findObjectByID(o->getProducerID())->getTemplate()->getName() == "GondorBarracks" && listed(*men, o->getProducerID());
			}
			return made;
		};
		const int madeA = madeBy(a.logic());
		for (int i = 0; i < 150; ++i)
		{
			in(a, [&] { step(a); });
			in(b, [&] { step(b); });
		}
		CHECK(madeBy(a.logic()) == madeA + 1);
		CHECK(madeBy(b.logic()) == madeA + 1);
		REQUIRE(a.logic().computeStateHash() == b.logic().computeStateHash());
	}
}

TEST_CASE("save4c reviewer round5: round4 factory cleanup and legal dispatched capture")
{
	OPENBFME_REQUIRE_START(s);
	Game g;
	load(*s, fourPlayerMessage(*s, 9021), g);
	auto context = s->world->enterContext();
	runTo(g, 50);
	GameLogic &l = g.logic();
	AISkirmishPlayer *men = aiOfSide(l, "Men"), *mordor = aiOfSide(l, "Mordor");
	REQUIRE(men);
	REQUIRE(mordor);
	const int oldIndex = men->playerIndex;
	Player *old = l.players().getNthPlayer(oldIndex), *rival = l.players().getNthPlayer(mordor->playerIndex);
	auto make = [&](const char *name, Player *owner) {
		const ThingTemplate *tt = l.things().findTemplate(name);
		REQUIRE(tt);
		Object *o = l.newObject(tt, owner->getDefaultTeam(), ObjectStatusMaskType{});
		REQUIRE(o);
		Coord3D pos{1500.0f, 800.0f, l.getGroundHeight(1500.0f, 800.0f)};
		o->setPosition(&pos);
		testutil::completeHorde(o); // a horde makes its members where it stands (onBuildComplete, RW 0x8718ED)
		return o;
	};
	SUBCASE("captured factory recomputes and cannot spend the rival's money")
	{
		Object *f = make("GondorBarracks", old);
		AIBaseBuilder::update(l, *men);
		REQUIRE(entriesOf(men->build.units, men->store->keyOf("GondorFighterHorde"), f->getID()) == 1);
		f->getProductionUpdate()->cancelAllProduction();
		onlyFactory(men->build.units, f->getID()); // (review r5) the AI's own barracks under construction is listed too, from its placement
		men->build.units.recompute = false;
		f->setTeam(rival->getDefaultTeam());
		CHECK(f->getControllingPlayer() == rival);
		CHECK(men->build.units.recompute);
		REQUIRE(entriesOf(men->build.units, men->store->keyOf("GondorFighterHorde"), f->getID()) == 0);
		AIBaseBuilder::update(l, *men);
		CHECK(men->build.units.composition.at(men->store->keyOf("GondorFighterHorde")) == 0.0f);
		men->build.units.disabled = true;
		men->build.baseBuilderDisabled = true;
		men->build.economy.disabled = true;
		men->build.pending.clear();
		auto req = std::make_shared<AIBuildRequest>();
		req->unit = true;
		req->templateName = "GondorFighterHorde";
		req->priority = 1.0f;
		men->build.pending.push_back(req);
		const auto cash = rival->getMoney()->countMoney();
		AIBaseBuilder::update(l, *men);
		CHECK(f->getProductionUpdate()->getProductionCount() == 0);
		CHECK(rival->getMoney()->countMoney() == cash);
	}
	SUBCASE("a genuinely dispatched unit legally captured saves and loads")
	{
		Object *u = make("GondorFighterHorde", old);
		// lane TARGET-1: held by NO_AUTO_ACQUIRE: a computer player's idle unit takes what it sees (RotWK's mood scan, RW 0x66844A), and from this spot the
		// horde charged what it saw 460 to the east and died there before frame 351; the dispatch (RW 0x9A3040) does not read the status
		u->setStatusAcrossHorde((unsigned)ObjectTemplateInfoBuilder::objectStatusIndex("NO_AUTO_ACQUIRE"), true);
		men->disabled = true;
		runTo(g, 351);
		men->disabled = false;
		men->brain.tactics.clear();
		AITacticalAI::addAttackedAtForTest(l, *men, *u->getPosition());
		AITacticalAI::updateTeamBuilder(l, *men);
		REQUIRE(men->brain.dispatched.count(u->getID()) == 1);
		u->setTeam(rival->getDefaultTeam());
		CHECK(u->getControllingPlayer() == rival);
		CHECK(men->brain.dispatched.count(u->getID()) == 1);
	}
	std::vector<std::uint8_t> saved, after;
	l.gameState().saveBlocks(k4c, saved);
	CHECK(loadError(l, k4c, saved).empty());
	l.gameState().saveBlocks(k4c, after);
	CHECK(after == saved);
}

TEST_CASE("save4c reviewer round5: recapture registration is synchronous as RW 0x8F06BF and can produce a named unit with the unit builder disabled")
{
	OPENBFME_REQUIRE_START(s);
	Game g;
	load(*s, fourPlayerMessage(*s, 9021), g);
	auto context = s->world->enterContext();
	runTo(g, 50);
	GameLogic &l = g.logic();
	AISkirmishPlayer *men = aiOfSide(l, "Men"), *mordor = aiOfSide(l, "Mordor");
	REQUIRE(men);
	REQUIRE(mordor);
	Player *owner = l.players().getNthPlayer(men->playerIndex);
	Object *f = l.newObject(l.things().findTemplate("GondorBarracks"), owner->getDefaultTeam(), ObjectStatusMaskType{});
	REQUIRE(f);
	Coord3D pos{1500.0f, 800.0f, l.getGroundHeight(1500.0f, 800.0f)};
	f->setPosition(&pos);
	testutil::completeHorde(f); // a horde makes its members where it stands (onBuildComplete, RW 0x8718ED)
	AIBaseBuilder::update(l, *men);
	f->getProductionUpdate()->cancelAllProduction();
	men->build.pending.clear();
	men->build.units.disabled = true; // skip autonomous requests; retail's ownership event still registers the factory
	men->build.baseBuilderDisabled = true;
	men->build.economy.disabled = true;
	owner->depositMoney(10000, false);
	SUBCASE("control: the original registered barracks") {}
	SUBCASE("recaptured barracks")
	{
		f->setTeam(l.players().getNthPlayer(mordor->playerIndex)->getDefaultTeam());
		f->setTeam(owner->getDefaultTeam());
		CHECK(f->getControllingPlayer() == owner);
		CHECK(entriesOf(men->build.units, men->store->keyOf("GondorFighterHorde"), f->getID()) == 1); // retail: before any update
	}
	std::set<ObjectID> before;
	for (const Object *o = l.getFirstObject(); o; o = o->getNextObject()) before.insert(o->getID());
	auto req = std::make_shared<AIBuildRequest>();
	req->unit = true;
	req->templateName = "GondorFighterHorde";
	req->priority = 1.0f;
	men->build.pending.push_back(req);
	AIBaseBuilder::update(l, *men);
	CHECK(f->getProductionUpdate()->countUnitTypeInQueue(l.things().findTemplate("GondorFighterHorde")) == 1);
	// Isolate the queue's effect: disable each AI after issuing, and watch actual named production.
	for (const auto &group : l.skirmishAI().groups()) for (const auto &kv : group->players) kv.second->disabled = true;
	runTo(g, 200);
	std::vector<ObjectID> made;
	for (const Object *o = l.getFirstObject(); o; o = o->getNextObject())
		if (!before.count(o->getID()) && o->getTemplate()->getName() == "GondorFighterHorde" && o->getControllingPlayer() == owner) made.push_back(o->getID());
	CHECK(made.size() == 1);
}

TEST_CASE("save4c reviewer round5: audit live dozer factories and owned tactic members through a player leaving and special-power defection")
{
	OPENBFME_REQUIRE_START(s);
	Game g;
	load(*s, fourPlayerMessage(*s, 9021), g);
	auto context = s->world->enterContext();
	runTo(g, 50);
	GameLogic &l = g.logic();
	AISkirmishPlayer *men = aiOfSide(l, "Men"), *isengard = aiOfSide(l, "Isengard");
	REQUIRE(men);
	REQUIRE(isengard);
	Player *old = l.players().getNthPlayer(men->playerIndex), *ally = l.players().getNthPlayer(isengard->playerIndex);
	auto checkSave = [&] {
		std::vector<std::uint8_t> saved, again;
		l.gameState().saveBlocks(k4c, saved);
		const auto hash = l.computeStateHash();
		const std::string error = loadError(l, k4c, saved);
		INFO("frame " << l.getFrame());
		INFO(error);
		CHECK(error.empty());
		CHECK(l.computeStateHash() == hash);
		l.gameState().saveBlocks(k4c, again);
		CHECK(saved == again);
	};
	SUBCASE("player leaves with structures, assets transferred to ally")
	{
		old->setPlayerRelationship(ally, ALLIES);
		ally->setPlayerRelationship(old, ALLIES);
		std::vector<ObjectID> structures;
		for (Object *o = l.getFirstObject(); o; o = o->getNextObject())
			if (o->getControllingPlayer() == old && o->isKindOfName("STRUCTURE")) structures.push_back(o->getID());
		REQUIRE_FALSE(structures.empty());
		l.victory().selfDestruct(old, true);
		CHECK(old->isDefeated());
		for (ObjectID id : structures) CHECK(l.findObjectByID(id)->getControllingPlayer() == ally);
		checkSave();
		runTo(g, 200);
		checkSave();
	}
	SUBCASE("player surrenders with structures, objects die")
	{
		l.victory().selfDestruct(old, false);
		CHECK(old->isDefeated());
		checkSave();
		runTo(g, 200);
		checkSave();
	}
	SUBCASE("Saruman DominateEnemySpecialPower converts a tactic member")
	{
		Object *u = l.newObject(l.things().findTemplate("GondorFighterHorde"), old->getDefaultTeam(), ObjectStatusMaskType{});
		REQUIRE(u);
		Coord3D pos{1500.0f, 800.0f, l.getGroundHeight(1500.0f, 800.0f)};
		u->setPosition(&pos);
		testutil::completeHorde(u); // a horde makes its members where it stands (onBuildComplete, RW 0x8718ED)
		// lane TARGET-1: held by NO_AUTO_ACQUIRE: a computer player's idle unit takes what it sees (RotWK's mood scan, RW 0x66844A), and from this spot the
		// horde charged what it saw 460 to the east and died there before frame 351; the dispatch (RW 0x9A3040) does not read the status
		u->setStatusAcrossHorde((unsigned)ObjectTemplateInfoBuilder::objectStatusIndex("NO_AUTO_ACQUIRE"), true);
		men->disabled = true;
		runTo(g, 351);
		men->disabled = false;
		men->brain.tactics.clear();
		AITacticalAI::addAttackedAtForTest(l, *men, *u->getPosition());
		AITacticalAI::updateTeamBuilder(l, *men);
		REQUIRE(men->brain.dispatched.count(u->getID()) == 1);
		// Seed an ordinary tactic; let the real team builder recruit the unit.
		AITactic tactic;
		tactic.kind = "SimpleAttack";
		tactic.teams.emplace_back();
		tactic.teams.back().type = 1;
		tactic.teams.back().maximum = 1;
		men->brain.tactics.push_back(tactic);
		AITacticalAI::updateTeamBuilder(l, *men);
		REQUIRE(men->brain.tactics.back().teams.front().members == std::vector<ObjectID>{u->getID()});
		men->disabled = true; // isolate ownership cleanup from later tactical orders
		checkSave();
		men = aiOfSide(l, "Men"); // loading recreates the AI
		Object *hero = l.newObject(l.things().findTemplate("IsengardSaruman"), ally->getDefaultTeam(), ObjectStatusMaskType{});
		REQUIRE(hero);
		pos.x = 1540.0f;
		hero->setPosition(&pos);
		testutil::completeHorde(hero); // a horde makes its members where it stands (onBuildComplete, RW 0x8718ED)
		auto *power = dynamic_cast<DominateEnemySpecialPower *>(hero->findModule("DominateEnemySpecialPower"));
		REQUIRE(power);
		power->initiateIntentToDoSpecialPower(power->abilityData()->m_specialPowerTemplate, u, nullptr, 0, 0);
		for (int i = 0; i < 40 && u->getControllingPlayer() == old; ++i) step(g);
		REQUIRE(u->getControllingPlayer() == ally);
		// The brain may retire the now empty tactic in this same frame.
		for (const AITactic &held : men->brain.tactics)
			for (const AITacticTeam &team : held.teams)
				CHECK(std::find(team.members.begin(), team.members.end(), u->getID()) == team.members.end());
		checkSave();
		runTo(g, l.getFrame() + 150);
		checkSave();
	}
}

TEST_CASE("save4c review r7 (archive 593e83cd): the state machine kept across a temporary defection (BACKLOG-4, RW AI + 0x38) and a base's fortress rebuild task "
		  "(BACKLOG-6, RW AIBase + 0x28) are saved: each loads into an identical game holding another state, saves the same bytes, has the same hash, and both "
		  "games stay equal (the defection ending included)")
{
	OPENBFME_REQUIRE_START(s);
	Game a, b;
	const NewGameMessage m = fourPlayerMessage(*s, 9021);
	starttest::Shared *peer = starttest::peerWorld(); // one live game per world (S-3808)
	load(*s, m, a);
	load(*peer, m, b);
	auto in = [&](Game &g, const std::function<void()> &fn) {
		auto scope = (&g == &a ? s : peer)->world->enterContext();
		fn();
	};
	in(a, [&] { runTo(a, 50); });
	in(b, [&] { runTo(b, 50); });
	ObjectID id = INVALID_ID;
	for (Game *g : { &a, &b })
	{
		in(*g, [&] {
			GameLogic &l = g->logic();
			AISkirmishPlayer *men = aiOfSide(l, "Men");
			REQUIRE(men != nullptr);
			Object *o = l.newObject(l.things().findTemplate("GondorFighterHorde"), l.players().getNthPlayer(men->playerIndex)->getDefaultTeam(), ObjectStatusMaskType{});
			REQUIRE(o != nullptr);
			Coord3D pos{ 1500.0f, 800.0f, l.getGroundHeight(1500.0f, 800.0f) };
			o->setPosition(&pos);
			testutil::completeHorde(o); // a horde makes its members where it stands (onBuildComplete, RW 0x8718ED)
			REQUIRE((id == INVALID_ID || id == o->getID()));
			id = o->getID();
			// the order the AI keeps while the object defects, then a new machine's order
			men->disabled = true; // the skirmish AI gives this unit no orders of its own
			AIUpdateInterface *ai = o->getAIUpdateInterface();
			step(*g);
			ai->aiMoveToPosition(Coord3D{ 2000.0f, 800.0f, 0.0f }, CMD_FROM_SCRIPT);
			step(*g);
			ai->beginDefectedStateMachine();
			REQUIRE(ai->hasDefectedStateMachine());
			ai->aiMoveToPosition(Coord3D{ 1500.0f, 1400.0f, 0.0f }, CMD_FROM_SCRIPT);
			step(*g);
		});
	}
	auto module = [&](Game &g) { return g.logic().findObjectByID(id)->getAIUpdateInterface(); };
	// the games agree where SAVE-4c owns the state: the unit's place and every hash section but the objects' (which holds the update scheduler's wake
	// times, SAVE-4b: ending b's defection woke the module, so the games' module updates fall on different frames afterwards and the module's per-update
	// snapshot of the model conditions differs; the module's own bytes are compared at the load, below)
	auto agree = [&] {
		std::string differ;
		const auto sa = sectionValues(a.logic()), sb = sectionValues(b.logic());
		for (const auto &kv : sa)
		{
			differ += kv.first != "objects" && (!sb.count(kv.first) || sb.at(kv.first) != kv.second) ? " '" + kv.first + "'" : "";
		}
		const Coord3D *pa = a.logic().findObjectByID(id)->getPosition(), *pb = b.logic().findObjectByID(id)->getPosition();
		differ += pa->x != pb->x || pa->y != pb->y || pa->z != pb->z ? " the unit's position" : "";
		return differ;
	};
	// the base's fortress task: a request kept by the first base, in neither queue
	const ThingTemplate *keepTemplate = s->world->things().findTemplate("GondorKeep");
	REQUIRE(keepTemplate != nullptr);
	for (Game *g : { &a, &b })
	{
		in(*g, [&] {
			AISkirmishPlayer *men = aiOfSide(g->logic(), "Men");
			REQUIRE(men != nullptr);
			REQUIRE(!men->build.bases.empty());
			auto req = std::make_shared<AIBuildRequest>();
			req->basePosition = Coord3D{ 100.0f, 200.0f, 0.0f };
			req->templateName = "GondorKeep";
			req->reserveMoney = true;
			men->build.bases[0].fortress = req;
			++men->build.fortressRebuilds;
		});
	}
	REQUIRE(a.logic().computeStateHash() == b.logic().computeStateHash());
	std::vector<std::uint8_t> savedAI, savedModule;
	in(a, [&] {
		a.logic().gameState().saveBlocks({ "obfme:SkirmishAI" }, savedAI);
		XferSave xs;
		module(a)->xferAIState(xs);
		savedModule = xs.data();
	});
	// b holds another state of both: its defection ends, its fortress task goes
	in(b, [&] {
		module(b)->endDefectedStateMachine();
		REQUIRE_FALSE(module(b)->hasDefectedStateMachine());
		AISkirmishPlayer *men = aiOfSide(b.logic(), "Men");
		men->build.bases[0].fortress.reset();
		REQUIRE(b.logic().computeStateHash() != a.logic().computeStateHash());
		XferLoad ml(savedModule);
		module(b)->xferAIState(ml);
		REQUIRE(ml.atEnd());
		CHECK(module(b)->hasDefectedStateMachine());
		const std::string e = loadError(b.logic(), { "obfme:SkirmishAI" }, savedAI);
		INFO(e);
		REQUIRE(e.empty());
		AISkirmishPlayer *loaded = aiOfSide(b.logic(), "Men");
		CHECK(static_cast<bool>(loaded->build.bases[0].fortress));
		CHECK(loaded->build.fortressRebuilds == 1u);
		XferSave again;
		module(b)->xferAIState(again);
		CHECK(again.data() == savedModule);
		std::vector<std::uint8_t> savedAgain;
		b.logic().gameState().saveBlocks({ "obfme:SkirmishAI" }, savedAgain);
		CHECK(savedAgain == savedAI);
	});
	for (Game *g : { &a, &b })
	{
		in(*g, [&] { module(*g)->wakeUpNow(); });
	}
	REQUIRE(agree() == "");
	for (int i = 0; i < 60; ++i)
	{
		in(a, [&] { step(a); });
		in(b, [&] { step(b); });
		const std::string d = agree();
		REQUIRE_MESSAGE(d == "", "frame " << a.logic().getFrame() << ":" << d);
	}
	for (Game *g : { &a, &b }) // the defection ends: the kept machine is the machine again (the order before the defection)
	{
		in(*g, [&] { module(*g)->endDefectedStateMachine(); });
	}
	REQUIRE(agree() == "");
	for (int i = 0; i < 60; ++i)
	{
		in(a, [&] { step(a); });
		in(b, [&] { step(b); });
		const std::string d = agree();
		REQUIRE_MESSAGE(d == "", "frame " << a.logic().getFrame() << ":" << d);
	}
}
