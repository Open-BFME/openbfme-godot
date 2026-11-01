// OpenBFME unit tests. GPL-3.0.
// Lane BFME2MODE-0 (docs/EDITIONS.md, AGENTS.md): Phase 0 of the game editions. BFME2 1.06's archives mounted ALONE (the developer switch
// OPENBFME_EDITION=bfme2 mounts exactly this) load every INI, every template and every BFME2 map's object list with only the NAMED errors below, and a
// computer-vs-computer skirmish on a BFME2 map runs deterministically on today's (RotWK) rules. Phase 0 plays RotWK's rules on BFME2 data: the named
// errors are the RotWK grammar's gaps for BFME2 rows, not BFME2 behaviour. Needs BFME2_INSTALL (the RotWK install is not mounted); SKIPs otherwise.

#include "doctest.h"

#include "Common/BfmeGame.h"
#include "Common/GameEdition.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "GameClient/GUI/Skirmish/IniSkirmishSetupSource.h"
#include "GameClient/LiveGame.h"
#include "GameClient/MapObjectDrawables.h"
#include "GameClient/MapUtil.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Map/TerrainLogic.h"
#include "GameLogic/NewGame/NewGame.h"
#include "GameLogic/Object/RetailObjectWorld.h"
#include "GameLogic/SkirmishAI/SkirmishAIEditionRules.h"
#include "GameLogic/Module/ProjectileModules.h"
#include "GameClient/GUI/Shell/Shell.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"
#include "MapCorpusUtil.h"
#include "RetailTestMount.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

using namespace mapcorpus;

namespace
{
struct Bfme2World
{
	retailtest::Mount *mount = nullptr;
	std::unique_ptr<RetailObjectWorld> world;
	MapObjectOptions options;
	GameLogicSettings settings;
	std::string error;
};

// BFME2 1.06 alone, as OPENBFME_EDITION=bfme2 mounts it; the loaders' first failure is kept in `error` (the test reports it)
bool loadBfme2(Bfme2World &s)
{
	s.mount = retailtest::bfme2Mount();
	if (!s.mount || !s.mount->fs)
	{
		return s.mount != nullptr;
	}
	s.world = std::make_unique<RetailObjectWorld>(*s.mount->fs);
	if (!s.world->load(&s.error))
	{
		s.world.reset();
	}
	else if (!MapObjectGameData::load(*s.mount->fs, s.options, &s.error) || !MapObjectGameData::loadPlayerTemplates(*s.mount->fs, s.options, &s.error) ||
		!MapCreationHooks::load(*s.mount->fs, s.options.creationScripts, &s.error) || !GameLogicSettingsLoader::load(*s.mount->fs, s.settings, &s.error))
	{
		s.world.reset();
	}
	return true;
}

Bfme2World *shared()
{
	static Bfme2World s;
	static bool built = false, available = false;
	if (!built)
	{
		built = true;
		available = loadBfme2(s);
	}
	return available ? &s : nullptr;
}

#define OPENBFME_REQUIRE_BFME2(var)                                                                                          \
	Bfme2World *var = shared();                                                                                              \
	if (!var)                                                                                                                \
	{                                                                                                                        \
		std::printf("SKIP: %s needs BFME2_INSTALL (retail files); not run\n", "a BFME2MODE-0 test (" __FILE__ ")");          \
		return;                                                                                                              \
	}                                                                                                                        \
	REQUIRE_MESSAGE(var->mount->fs != nullptr, var->mount->error);                                                           \
	REQUIRE_MESSAGE(var->world != nullptr, var->error);                                                                      \
	auto var##Context = var->world->enterContext()

std::string dirOf(const std::string &path)
{
	return path.substr(0, path.find_last_of("/\\"));
}

// every INI load error of BFME2 1.06 alone, "<file>: <the message's first line>" (the named list of docs/EDITIONS.md). Empty since BFME2 1.06's
// BezierProjectileBehavior row PreLandingEmotionAffectsAllies (BFME2 table 0xC41B58, dropped from RotWK's 0xC58368) is a BFME2 row
// (ProjectileModules.cpp kBezierBfme2Rows): before it, evilfactionsubobjects.ini stopped at line 1590 and its 53 later templates were undefined
const std::set<std::string> &namedIniErrors()
{
	// merge queue q34: the War of the Ring branch's Living World parser reads BFME2 1.06's RiskCampaign.ini, whose AddPlayer row (RW 0x6E309A's table)
	// is one of S-1321's unported rows (docs/EDITIONS.md)
	static const std::set<std::string> named = { "Data\\INI\\Campaigns\\RiskCampaign.ini: [S-1321] Living World field 'AddPlayer' is not ported (its retail parser was not read)" };
	return named;
}
} // namespace

TEST_CASE("bfme2mode0: the data decides the edition (BFME2 1.06 alone is BFME2 data, not BFME1; RotWK over BFME2 is not BFME2 data)")
{
	GameEdition e = GameEdition::BFME2_106;
	std::string error;
	CHECK(parseGameEdition("", e, &error));
	CHECK(e == GameEdition::RotWK201);
	CHECK(parseGameEdition("bfme2", e, &error));
	CHECK(e == GameEdition::BFME2_106);
	CHECK(parseGameEdition("rotwk", e, &error));
	CHECK(e == GameEdition::RotWK201);
	CHECK_FALSE(parseGameEdition("BFME2 ", e, &error));
	CHECK(error == "unknown game edition 'BFME2 ' (OPENBFME_EDITION / --edition takes rotwk or bfme2)");
	CHECK(std::string(gameEditionName(GameEdition::BFME2_106)) == "bfme2");
	CHECK(std::string(gameEditionName(GameEdition::RotWK201)) == "rotwk");

	retailtest::Mount *b2 = retailtest::bfme2Mount();
	retailtest::Mount *pure = retailtest::pureMount();
	if (!b2 || !pure)
	{
		retailtest::printSkip("bfme2mode0 data detection");
		return;
	}
	REQUIRE_MESSAGE(b2->fs != nullptr, b2->error);
	REQUIRE_MESSAGE(pure->fs != nullptr, pure->error);
	CHECK(isBfme2Data(*b2->fs));
	CHECK_FALSE(isBfme1Data(*b2->fs));
	CHECK_FALSE(isBfme2Data(*pure->fs));
	CHECK_FALSE(isBfme1Data(*pure->fs));
	// BFME2 1.06 alone mounts no RotWK archive
	for (const std::string &a : b2->archives)
	{
		INFO(a);
		CHECK(a.rfind("bfme2:", 0) == 0);
	}
}

TEST_CASE("bfme2mode0: the skirmish AI's edition variant: FarmKillSquad's team priority is RotWK's 500 and BFME2's 30 (both game.dat files, retail_oracle)")
{
	// RW 0x9BB49D / BFME2 0x9AD00B (SkirmishAIEditionRules.h); tools/retail_oracle/test_edition_oracle.py runs both retail instructions
	CHECK(SkirmishAIEditionRules::farmKillPriority(GameEdition::RotWK201) == 500);
	CHECK(SkirmishAIEditionRules::farmKillPriority(GameEdition::BFME2_106) == 30);
}

TEST_CASE("bfme2mode0: the projectile edition variant: BFME2's PreLandingEmotionAffectsAllies row loads only from BFME2 data, and only the BFME2 edition reads it")
{
	BezierProjectileBehaviorModuleData d;
	CHECK(d.m_preLandingEmotionAffectsAllies); // the BFME2 data ctor 0x85B4F1 writes 1
	CHECK(d.preLandingRelationshipMask(GameEdition::RotWK201) == 7); // RW 0x85F31A: every object
	CHECK(d.preLandingRelationshipMask(GameEdition::BFME2_106) == 7);
	d.m_preLandingEmotionAffectsAllies = false;
	CHECK(d.preLandingRelationshipMask(GameEdition::RotWK201) == 7);
	CHECK(d.preLandingRelationshipMask(GameEdition::BFME2_106) == 3); // BFME2 0x85C2B8: enemies and neutral
	OPENBFME_REQUIRE_BFME2(s);
	const ThingTemplate *heads = s->world->things().findTemplate("MordorCatapultHumanHeadsProjectile");
	REQUIRE(heads != nullptr); // evilfactionsubobjects.ini line 1590 sets the row to No
	const BezierProjectileBehaviorModuleData *bezier = nullptr;
	for (const ThingTemplate::Nugget &n : heads->behaviorModules().nuggets())
	{
		if (n.name == "BezierProjectileBehavior")
		{
			bezier = dynamic_cast<const BezierProjectileBehaviorModuleData *>(n.data.get());
		}
	}
	REQUIRE(bezier != nullptr);
	CHECK_FALSE(bezier->m_preLandingEmotionAffectsAllies);
	CHECK(bezier->preLandingRelationshipMask(GameEdition::BFME2_106) == 3);
}

TEST_CASE("bfme2mode0: BFME2 1.06's GameData has no ShellMapOn: BFME2 data takes GlobalData's constructor default, RotWK data still needs the key")
{
	bool on = false;
	std::string error;
	CHECK(Shell::readShellMapOnForData("GameData\n  ShellMapName = Maps\\ShellMap1\\ShellMap1.map\nEnd\n", true, on, &error));
	CHECK(on); // BFME2 0x6369FD
	on = true;
	CHECK(Shell::readShellMapOnForData("GameData\n  ShellMapOn = No\nEnd\n", true, on, &error));
	CHECK_FALSE(on);
	CHECK_FALSE(Shell::readShellMapOnForData("GameData\n  ShellMapName = x\nEnd\n", false, on, &error));
	CHECK(error == "GameData has no ShellMapOn");
}

TEST_CASE("bfme2mode0: BFME2 1.06 alone loads every INI and template; the only errors are the named RotWK-grammar gaps")
{
	OPENBFME_REQUIRE_BFME2(s);
	const SubsystemLoadReport &report = s->world->report();
	std::set<std::string> seen;
	for (const SubsystemLoadReport::FileError &e : report.errors)
	{
		// the message's first line (an INI error adds "Error parsing field ... line N." after a blank line)
		const std::string line = e.file + ": " + e.message.substr(0, e.message.find('\n'));
		std::printf("  info: bfme2 load error: %s\n", (e.file + ": " + e.message).c_str());
		seen.insert(line);
	}
	for (const std::string &line : seen)
	{
		INFO("an INI load error that is not in the named list: " << line);
		CHECK(namedIniErrors().count(line) == 1);
	}
	for (const std::string &line : namedIniErrors())
	{
		INFO("a named INI load error that no longer happens (remove it from the list and docs/EDITIONS.md): " << line);
		CHECK(seen.count(line) == 1);
	}
	for (const std::string &n : report.noLegendEntry)
	{
		std::printf("  info: bfme2 legend has no entry for %s\n", n.c_str());
	}
	for (const std::string &n : report.unverifiedOrder)
	{
		std::printf("  info: bfme2 legend entry outside RotWK's order: %s\n", n.c_str());
	}
	std::printf("  info: bfme2 object world: %zu templates, %zu load errors, %.1f s\n", s->world->things().templateCount(), report.errors.size(),
		s->world->loadSeconds());
	// BFME2 1.06's templates (RotWK 2.01 + BFME2 1.06: 4657, test_mapobj_retail.cpp)
	CHECK(s->world->things().templateCount() == 4238);
}

TEST_CASE("bfme2mode0: every BFME2 1.06 map's object list resolves against BFME2's templates")
{
	OPENBFME_REQUIRE_BFME2(s);
	FilenameList list;
	s->mount->fs->getFileListInDirectory("", "", "*.map", list, true);
	std::printf("  info: bfme2 maps: %zu\n", list.size());
	std::map<std::string, size_t> unresolved, caseMismatch;
	std::vector<std::string> errors, mapIniErrors, readErrors;
	size_t totalObjects = 0;
	std::vector<std::uint8_t> bytes;
	std::string err;
	for (const std::string &path : list)
	{
		const std::string key = slashes(lowerStr(path), '/');
		if (!s->mount->fs->readFile(path, bytes, &err))
		{
			readErrors.push_back(key + ": " + err);
			continue;
		}
		LoadedMap m;
		MapReadOptions opt;
		if (!MapReader::load(bytes, key, opt, m, &err))
		{
			readErrors.push_back(key + ": " + err);
			continue;
		}
		TerrainLogic terrain;
		std::vector<std::string> problems;
		terrain.init(m.heightMap, m.chunks, &problems);
		const RetailObjectWorld::MapIniResult mi = s->world->applyMapIni(slashes(dirOf(path), '\\'));
		for (const std::string &e : mi.errors)
		{
			mapIniErrors.push_back(key + ": " + e);
		}
		MapObjectDrawables out;
		MapObjectCreation::build(m, key, s->world->things(), terrain, s->options, out);
		totalObjects += out.report.objects;
		for (const std::string &e : out.report.errors)
		{
			errors.push_back(key + ": " + e);
		}
		for (const auto &kv : out.report.unresolved) unresolved[kv.first] += kv.second;
		for (const auto &kv : out.report.caseMismatch) caseMismatch[kv.first] += kv.second;
	}
	for (const std::string &e : readErrors) std::printf("  info: bfme2 map read error: %s\n", e.c_str());
	for (const std::string &e : mapIniErrors) std::printf("  info: bfme2 map.ini error: %s\n", e.c_str());
	for (const std::string &e : errors) std::printf("  info: bfme2 map object error: %s\n", e.c_str());
	for (const auto &kv : unresolved) std::printf("  info: bfme2 unresolved template %s x%zu\n", kv.first.c_str(), kv.second);
	for (const auto &kv : caseMismatch) std::printf("  info: bfme2 case-mismatched template %s x%zu\n", kv.first.c_str(), kv.second);
	std::printf("  info: bfme2 map objects: %zu over %zu maps\n", totalObjects, list.size());
	CHECK(list.size() == 120);
	CHECK(readErrors.empty());
	CHECK(mapIniErrors.empty());
	CHECK(errors.empty());
	CHECK(unresolved.empty());
	CHECK(caseMismatch.empty());
	CHECK(totalObjects == 123461);
}

namespace
{
int factionIndex(const PlayerTemplateStore &store, const std::string &name)
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

struct Bfme2Run
{
	std::vector<std::uint32_t> hashes;
	int defeatFrame = -1;
	std::string progress;
};

// two Medium computers on BFME2 1.06's own Evendim (bfme2:maps.big), each frame's state hash
void runBfme2Versus(Bfme2World &s, const std::string &a, const std::string &b, std::uint32_t seed, int frames, Bfme2Run &out)
{
	NewGameMessage m;
	m.game.mapName = "maps/map mp evendim/map mp evendim.map";
	m.game.seed = seed;
	m.game.startingCash = 1500;
	const std::string factions[2] = { a, b };
	for (int i = 0; i < 2; ++i)
	{
		SkirmishGameSlot &slot = m.game.slots[i];
		slot.state = (SlotState)(SLOT_EASY_AI + 1);
		slot.name = i == 0 ? u"Player0" : u"Player1";
		slot.playerTemplate = factionIndex(s.world->playerTemplates(), factions[i]);
		REQUIRE_MESSAGE(slot.playerTemplate != -1, "no BFME2 player template " << factions[i]);
		slot.startPos = i;
		slot.color = i;
		slot.teamNumber = i;
	}
	std::vector<MapCacheEntry> cache;
	std::string error;
	REQUIRE_MESSAGE(IniSkirmishSetupSource::loadMapCache(*s.mount->fs, cache, &error), error);
	NewGameStart start(RandomAlgorithm::ZH_CarryChain);
	REQUIRE_MESSAGE(NewGame::prepareNewGame(m, s.world->playerTemplates(), s.settings, cache, RandomAlgorithm::ZH_CarryChain, start, &error), error);
	ArchiveW3DFileSource source(*s.mount->fs);
	WW3DAssetManager assets(source);
	LiveGame game(*s.world, *s.mount->fs, assets, s.options);
	LiveGame::Options o;
	o.start = &start;
	REQUIRE_MESSAGE(game.load(o, &error), error);
	GameLogic &logic = game.logic();
	REQUIRE(logic.edition() == GameEdition::BFME2_106); // the mounted data decides
	const Player *players[2] = { playerAtStart(logic, 0), playerAtStart(logic, 1) };
	REQUIRE(players[0] != nullptr);
	REQUIRE(players[1] != nullptr);
	for (int i = 0; i < frames; ++i)
	{
		game.advance(0.2);
		out.hashes.push_back(logic.computeStateHash());
		if ((logic.getFrame() % 1000) == 0)
		{
			out.progress += "frame " + std::to_string(logic.getFrame()) + ":";
			for (int k = 0; k < 2; ++k)
			{
				const ScoreKeeper &sk = players[k]->getScoreKeeper();
				out.progress += " [" + std::to_string(k) + " built " + std::to_string(sk.unitsBuilt()) + "u/" + std::to_string(sk.structuresBuilt()) + "s destroyed " +
					std::to_string(sk.totalUnitsDestroyed()) + "u/" + std::to_string(sk.totalStructuresDestroyed()) + "s]";
			}
			out.progress += "\n";
		}
		for (int k = 0; k < 2; ++k)
		{
			if (players[k]->isDefeated() && out.defeatFrame < 0)
			{
				out.defeatFrame = (int)logic.getFrame();
			}
		}
	}
}

// the hash list's digest (the PLAN rule-5 rotate-add step over the hashes), printed so two hosts' runs can be compared
std::uint32_t digest(const std::vector<std::uint32_t> &hashes)
{
	std::uint32_t crc = 0;
	for (std::uint32_t h : hashes)
	{
		crc = ((crc << 1) | (crc >> 31)) + h;
	}
	return crc;
}
} // namespace

TEST_CASE("bfme2mode0: a BFME2-map computer-vs-computer skirmish on RotWK's rules agrees with itself on every frame hash (3000 frames)")
{
	OPENBFME_REQUIRE_BFME2(s);
	const int frames = 3000;
	Bfme2Run a, b;
	runBfme2Versus(*s, "FactionMordor", "FactionMen", 2u, frames, a);
	runBfme2Versus(*s, "FactionMordor", "FactionMen", 2u, frames, b);
	std::printf("%s", a.progress.c_str());
	std::printf("  info: bfme2 ai-vs-ai: %d frames, final hash 0x%08X, digest 0x%08X, defeat frame %d\n", frames, a.hashes.back(), digest(a.hashes),
		a.defeatFrame);
	// a host-to-host comparison: OPENBFME_BFME2MODE0_HASHES=<file> writes "<frame> 0x<hash>" per frame of the first run
	if (const char *path = std::getenv("OPENBFME_BFME2MODE0_HASHES"))
	{
		std::ofstream f(path);
		for (size_t i = 0; i < a.hashes.size(); ++i)
		{
			char line[48];
			std::snprintf(line, sizeof line, "%zu 0x%08X\n", i + 1, a.hashes[i]);
			f << line;
		}
	}
	REQUIRE(a.hashes.size() == (size_t)frames);
	REQUIRE(b.hashes.size() == (size_t)frames);
	CHECK(a.hashes == b.hashes);
}
