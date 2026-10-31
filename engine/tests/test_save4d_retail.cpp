// OpenBFME unit tests. GPL-3.0.
// Lane SAVE-4d on the retail game (docs/SAVES.md T1 / T2, the brief's measurement): a 4-player skirmish (four Medium AIs on Fall Back 8p) at frame 3,000. The
// SAVE-4d blocks of game A load into an identical game B whose script engine, victory, weather, economy and audio gate were changed first: each of those
// state hash sections takes A's value again (it differed), B's other sections stay as they were, B's whole state hash equals A's, and the two games run
// 100 more frames with equal hashes (T1 as far as these blocks allow); B saves the same bytes (T2). A real tree of the map toppled and harvested in A loads
// into B; loading it twice is refused (T5). The blocks' sizes and write time are reported. SKIP loudly without ROTWK_INSTALL / BFME2_INSTALL.

#include "doctest.h"

#include "StartTestUtil.h"

#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"

#include "Common/GameState.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/Xfer.h"
#include "GameClient/GUI/Skirmish/IniSkirmishSetupSource.h"
#include "GameClient/LiveGame.h"
#include "GameLogic/Economy.h"
#include "GameLogic/GlobalWeatherSystem.h"
#include "GameLogic/LargeGroupAudioLink.h"
#include "GameLogic/Map/TerrainTrees.h"
#include "GameLogic/NewGame/NewGame.h"
#include "GameLogic/Module/TerrainResourceBehavior.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ScriptEngine/ScriptEngine.h"
#include "GameLogic/System/TerrainResourceManager.h"
#include "GameLogic/VictoryConditions.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"

#include <algorithm>
#include <chrono>
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

// four Medium AIs (Men, Mordor, Elves, Isengard) on the 8-player Fall Back, each on a team of its own
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

void runTo(Game &g, UnsignedInt frame)
{
	while (g.game->logic().getFrame() < frame)
	{
		REQUIRE(g.game->advance(0.2) == 1);
		g.game->drainFrames();
	}
	REQUIRE(g.game->logic().getFrame() == frame);
}

const std::vector<std::string> kOurs = { "obfme:ScriptEngine", "obfme:Victory", "obfme:Weather", "obfme:Economy", "obfme:CreateAHero",
	"obfme:TerrainTrees", "obfme:LargeGroupAudio" };
const std::vector<std::string> kOurSections = { "script engine", "victory", "weather", "economy", "create-a-hero", "terrain trees", "large group audio" };

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

// the sections of `all` that are (ours) or are not (!ours) SAVE-4d's
std::map<std::string, std::uint32_t> part(const std::map<std::string, std::uint32_t> &all, bool ours)
{
	std::map<std::string, std::uint32_t> out;
	for (const auto &kv : all)
	{
		const bool isOurs = std::find(kOurSections.begin(), kOurSections.end(), kv.first) != kOurSections.end();
		if (isOurs == ours)
		{
			out.insert(kv);
		}
	}
	return out;
}
} // namespace

TEST_CASE("save4d retail: a 4-player skirmish at frame 3,000: the SAVE-4d blocks restore a changed game's script engine, victory, weather, economy and "
		  "audio gate, the two games then run on with equal hashes (T1 for these blocks, T2), a real tree loads, and the blocks are measured")
{
	OPENBFME_REQUIRE_START(s);
	const NewGameMessage m = fourPlayerMessage(*s, 4250);
	Game a, b;
	load(*s, m, a);
	// lane SAVE-4e (merge, S-3808): one live game per world: the second game has a world of its own, entered while it loads
	starttest::Shared *peer = starttest::peerWorld();
	REQUIRE(peer);
	REQUIRE_MESSAGE(peer->world, peer->error);
	{
		auto peerContext = peer->world->enterContext();
		load(*peer, m, b);
	}
	runTo(a, 3000);
	runTo(b, 3000);
	GameLogic &la = a.game->logic();
	GameLogic &lb = b.game->logic();
	REQUIRE(la.computeStateHash() == lb.computeStateHash());
	const std::map<std::string, std::uint32_t> sectionsA = sectionValues(la);
	// the real game has these sections (the Create-a-Hero and tree sections only once a game has one / a tree changed)
	for (const char *name : { "script engine", "victory", "weather", "economy", "large group audio" })
	{
		CHECK_MESSAGE(sectionsA.count(name) == 1u, name);
	}
	CHECK(sectionsA.count("create-a-hero") == 0u);
	// lane TREES1: the AIs' crushers (RW 0x6858BE) and the structures their workers place (RW 0x684BCA) change trees in play: the section is there exactly
	// when a tree changed, the same in both games
	const bool playChanged = la.trees().anyChanged();
	size_t playToppled = 0, playCleared = 0;
	for (const TerrainTrees::Record &r : la.trees().records())
	{
		playToppled += r.changed && r.toppled ? 1u : 0u;
		playCleared += r.changed && r.clientId == 0 ? 1u : 0u;
	}
	MESSAGE("SAVE-4d: trees changed in play by frame 3000: " << playToppled << " toppled, " << playCleared << " cleared");
	CHECK(sectionsA.count("terrain trees") == (playChanged ? 1u : 0u));
	CHECK(lb.trees().anyChanged() == playChanged);
	REQUIRE(la.scriptEngine().loaded());
	REQUIRE(la.economy().resources().claimants().size() > 0u); // tolerance: a precondition (the AIs' farms and buildings claim ground), the exact list is compared below

	std::vector<std::uint8_t> stream;
	la.gameState().saveBlocks(kOurs, stream);

	// the measurement (no threshold: SAVES.md section 4 budgets the whole snapshot): each block's size, the blocks' write time, the best of 20 writes
	std::string sizes;
	for (const std::string &t : kOurs)
	{
		std::vector<std::uint8_t> one;
		la.gameState().saveBlocks({ t }, one);
		sizes += t + " " + std::to_string(one.size() - 16 - 7) + " B; "; // without the stream's 16-byte header and the 7-byte end token
	}
	double best = 1e9;
	for (int i = 0; i < 20; ++i)
	{
		const auto t0 = std::chrono::steady_clock::now();
		std::vector<std::uint8_t> w;
		la.gameState().saveBlocks(kOurs, w);
		const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
		best = ms < best ? ms : best;
	}
	MESSAGE("SAVE-4d blocks of a 4-player skirmish at frame 3000 (" << la.getObjectCount() << " objects, " << la.economy().resources().claimants().size()
		<< " resource claimants, " << la.trees().records().size() << " trees): " << stream.size() << " bytes (" << sizes << "), written in " << best
		<< " ms (best of 20)");

	// B's SAVE-4d state changed through the subsystems' own entry points
	lb.scriptEngine().flag("Probe") = true;
	lb.victory().setScriptFlag91(true);
	lb.weather().setWeather(2, 0, 50);
	// merge queue q23: a claimant's release (RW 0x75BB8F) also zeroes its TerrainResourceBehavior share and re-applies its neighbours' claims (RW 0x75B923):
	// object state outside these blocks (at frame 3,000 the list's first is now a finished IsengardUrukPit with a share). The test puts the shares back
	// through the module so that B differs from A in these blocks alone (checked below: B's objects section is A's)
	std::map<ObjectID, float> shares;
	for (Object *o = lb.getFirstObject(); o; o = o->getNextObject())
	{
		if (const TerrainResourceBehavior *mod = dynamic_cast<const TerrainResourceBehavior *>(o->findModule("TerrainResourceBehavior")))
		{
			shares[o->getID()] = mod->claimShare();
		}
	}
	const TerrainResourceManager::Claimant firstClaim = lb.economy().resources().claimants().front();
	lb.economy().resources().unclaim(*lb.findObjectByID(firstClaim.id), firstClaim.radius); // RW 0x75BE1C: as when the object dies
	for (const auto &kv : shares)
	{
		dynamic_cast<TerrainResourceBehavior *>(lb.findObjectByID(kv.first)->findModule("TerrainResourceBehavior"))->setClaimShare(kv.second);
	}
	lb.largeGroupAudio().enable(1);
	const std::map<std::string, std::uint32_t> changedB = sectionValues(lb);
	CHECK(changedB.at("objects") == sectionsA.at("objects")); // merge queue q23: the unclaim's share changes were put back
	for (const char *name : { "script engine", "victory", "weather", "economy", "large group audio" })
	{
		CHECK_MESSAGE(changedB.at(name) != sectionsA.at(name), name);
	}
	lb.gameState().loadBlocks(kOurs, stream);
	const std::map<std::string, std::uint32_t> loadedB = sectionValues(lb);
	CHECK(part(loadedB, true) == part(sectionsA, true));
	CHECK(part(loadedB, false) == part(changedB, false)); // nothing else moved
	CHECK(lb.computeStateHash() == la.computeStateHash());
	CHECK(lb.economy().resources().claimants().size() == la.economy().resources().claimants().size());
	CHECK(lb.economy().resources().blockedCells() == la.economy().resources().blockedCells());
	CHECK(lb.scriptEngine().findFlag("Probe") == nullptr);
	std::vector<std::uint8_t> again;
	lb.gameState().saveBlocks(kOurs, again);
	CHECK(again == stream); // T2

	// T1 for these blocks: both games run on, every frame's hash equal
	for (UnsignedInt f = 3001; f <= 3100; ++f)
	{
		runTo(a, f);
		runTo(b, f);
		REQUIRE_MESSAGE(la.computeStateHash() == lb.computeStateHash(), "frame " << f);
	}

	// a real tree of the map: one toppled, one harvested bare in A; its block loads into B (whose trees are the map's)
	TerrainTrees &ta = la.trees();
	const std::vector<TerrainTrees::Record> &records = ta.records();
	REQUIRE(records.size() > 1u); // tolerance: a precondition (the map has trees); the two used are found by the logic's own query below
	// the logic's own query (RW 0x67F52B) at the first standing tree, then at the first standing harvestable tree after it
	TerrainTrees::Record *first = nullptr, *bare = nullptr;
	for (const TerrainTrees::Record &r : records)
	{
		if (r.clientId == 0)
		{
			continue;
		}
		if (!first)
		{
			first = ta.findClosestTree(r.position, 1.0f, true, 0);
		}
		else if (!bare && r.harvestable)
		{
			bare = ta.findClosestTree(r.position, 1.0f, true, 2);
		}
	}
	REQUIRE(first);
	REQUIRE(bare);
	REQUIRE(first != bare);
	const size_t toppled = (size_t)(first - &records[0]), harvested = (size_t)(bare - &records[0]);
	const Coord3D from{ first->position.x - 10.0f, first->position.y, first->position.z };
	ta.toppleTree(*first, from, nullptr);
	ta.harvestTree(*bare, bare->value);
	REQUIRE(sectionValues(la).count("terrain trees") == 1u);
	std::vector<std::uint8_t> trees;
	la.gameState().saveBlocks({ "obfme:TerrainTrees" }, trees);
	REQUIRE(sectionValues(lb).count("terrain trees") == (playChanged ? 1u : 0u));
	lb.gameState().loadBlocks({ "obfme:TerrainTrees" }, trees);
	CHECK(sectionValues(lb).at("terrain trees") == sectionValues(la).at("terrain trees"));
	const TerrainTrees::Record &tb = lb.trees().records()[toppled];
	CHECK(tb.toppled);
	CHECK(tb.toppleDirection.x == 10.0f);
	CHECK(lb.trees().records()[harvested].clientId == 0u); // cleared (RW 0x67D3C2)
	// lane SAVE-4e: a game whose trees changed since its START loads the block too (its records go back to the map's first): B's own change is undone,
	// the snapshot's stand (what a refused load's put-back needs, LiveGame::loadSnapshot)
	std::vector<TerrainTrees::Record> &rb = const_cast<std::vector<TerrainTrees::Record> &>(lb.trees().records());
	TerrainTrees::Record *other = nullptr;
	for (TerrainTrees::Record &r : rb)
	{
		other = !other && &r - &rb[0] != (std::ptrdiff_t)toppled && &r - &rb[0] != (std::ptrdiff_t)harvested && r.clientId != 0 && !r.toppled ? &r : other;
	}
	REQUIRE(other);
	const size_t otherIndex = (size_t)(other - &rb[0]);
	lb.trees().toppleTree(*other, Coord3D{ other->position.x + 10.0f, other->position.y, other->position.z }, nullptr);
	REQUIRE(lb.trees().records()[otherIndex].toppled);
	REQUIRE(sectionValues(lb).at("terrain trees") != sectionValues(la).at("terrain trees"));
	lb.gameState().loadBlocks({ "obfme:TerrainTrees" }, trees);
	CHECK(sectionValues(lb).at("terrain trees") == sectionValues(la).at("terrain trees"));
	CHECK_FALSE(lb.trees().records()[otherIndex].toppled);
	CHECK(lb.trees().records()[toppled].toppled);
}

namespace
{
std::uint32_t u32At(const std::vector<std::uint8_t> &b, size_t at)
{
	return (std::uint32_t)b[at] | ((std::uint32_t)b[at + 1] << 8) | ((std::uint32_t)b[at + 2] << 16) | ((std::uint32_t)b[at + 3] << 24);
}

void putU32At(std::vector<std::uint8_t> &b, size_t at, std::uint32_t v)
{
	for (int i = 0; i < 4; ++i)
	{
		b[at + (size_t)i] = (std::uint8_t)(v >> (8 * i));
	}
}

std::string treesLoadError(GameLogic &logic, const std::vector<std::uint8_t> &stream)
{
	try
	{
		logic.gameState().loadBlocks({ "obfme:TerrainTrees" }, stream);
		return "(loaded)";
	}
	catch (const XferException &e)
	{
		return e.what();
	}
}
} // namespace

TEST_CASE("save4d retail trees: changed records of version 1 (no running index, 43 bytes at their shortest) load; a bucket link other than the map's "
		  "(a self-link) and a record count beyond the block are refused by name, the trees unchanged")
{
	OPENBFME_REQUIRE_START(s);
	const NewGameMessage m = fourPlayerMessage(*s, 4252);
	Game a, b;
	load(*s, m, a);
	// lane SAVE-4e (merge, S-3808): one live game per world: the second game has a world of its own, entered while it loads
	starttest::Shared *peer = starttest::peerWorld();
	REQUIRE(peer);
	REQUIRE_MESSAGE(peer->world, peer->error);
	{
		auto peerContext = peer->world->enterContext();
		load(*peer, m, b);
	}
	runTo(a, 10);
	runTo(b, 10);
	GameLogic &la = a.game->logic();
	GameLogic &lb = b.game->logic();
	TerrainTrees &ta = la.trees();
	const std::vector<TerrainTrees::Record> &records = ta.records();
	TerrainTrees::Record *first = nullptr;
	for (const TerrainTrees::Record &r : records)
	{
		if (r.clientId != 0)
		{
			first = ta.findClosestTree(r.position, 1.0f, true, 0);
			break;
		}
	}
	REQUIRE(first);
	const size_t index = (size_t)(first - &records[0]);
	REQUIRE(first->next != (std::int16_t)index); // tolerance: a precondition (a self-link is the hostile value below)
	ta.toppleTree(*first, Coord3D{ first->position.x - 10.0f, first->position.y, first->position.z }, nullptr);
	std::vector<std::uint8_t> v2;
	la.gameState().saveBlocks({ "obfme:TerrainTrees" }, v2);
	// the stream: the 16-byte header, the token, "BLOK" and the block's end offset; the payload: version, record count, next index, frame, speed, the
	// changed count; then the record: its index, version, position, client id, name, toppled, direction, value, link, two flags, running index
	const size_t payload = 16 + 1 + std::string("obfme:TerrainTrees").size() + 8;
	const size_t count = payload + 1 + 4 + 4 + 4 + 4;
	REQUIRE(u32At(v2, count) == 1u);
	const size_t rec = count + 4;
	REQUIRE(u32At(v2, rec) == index);
	const size_t name = rec + 4 + 1 + 12 + 4;
	const size_t link = name + 1 + v2[name] + 1 + 12 + 4;
	REQUIRE(v2[rec + 4] == 2);
	REQUIRE((std::int16_t)(v2[link] | (v2[link + 1] << 8)) == first->next);
	const std::uint32_t before = lb.computeStateHash();

	std::vector<std::uint8_t> bad = v2;
	bad[link] = (std::uint8_t)index;
	bad[link + 1] = (std::uint8_t)(index >> 8);
	CHECK(treesLoadError(lb, bad) == "block 'obfme:TerrainTrees': terrain trees: tree " + std::to_string(index) + ": a bucket link to " +
			std::to_string(index) + " (the map's is " + std::to_string(first->next) + ")");
	bad = v2;
	putU32At(bad, count, 0x10000000u);
	CHECK(treesLoadError(lb, bad) == "block 'obfme:TerrainTrees': a list declares 268435456 entries of at least 43 bytes, more than the " +
			std::to_string(v2.size() - 7 - rec) + " bytes left");
	CHECK(lb.computeStateHash() == before);
	CHECK_FALSE(lb.trees().anyChanged());

	// version 1: the same record without its running index (the block 4 bytes shorter, its end offset with it)
	std::vector<std::uint8_t> v1(v2.begin(), v2.end() - 7 - 4);
	v1[rec + 4] = 1;
	v1.insert(v1.end(), v2.end() - 7, v2.end());
	putU32At(v1, payload - 4, u32At(v2, payload - 4) - 4);
	REQUIRE(treesLoadError(lb, v1) == "(loaded)");
	CHECK(lb.trees().records()[index].toppled);
	CHECK(lb.trees().records()[index].index == records[index].index); // the map's running index kept
	std::map<std::string, std::uint32_t> sa = sectionValues(la), sb = sectionValues(lb);
	CHECK(sb.at("terrain trees") == sa.at("terrain trees"));
}
