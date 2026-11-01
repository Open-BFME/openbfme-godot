// OpenBFME tests. GPL-3.0. Generated terrain and waypoints, never extracted map bytes.
// Chunk layouts: MapTestUtil.h / test_map_chunks.cpp (ZH WorldHeightMap and TerrainLogic writers).
#pragma once

#include "MapTestUtil.h"
#include "StartTestUtil.h"

#include "Common/ArchiveFile.h"
#include "Common/PlayerTemplate.h"
#include "GameClient/LiveGame.h"
#include "GameClient/MapCache.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"
#include "GameLogic/NewGame/NewGame.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"

#include <stdexcept>

namespace p16test
{
inline constexpr const char *mapKey = "maps/p16_fixture/p16_fixture.map";

inline Coord3D position(int zeroBased)
{
	return Coord3D{ 600.0f + 1200.0f * (zeroBased % 4), 600.0f + 1200.0f * (zeroBased / 4), 0.0f };
}

inline MapCacheEntry cacheEntry()
{
	MapCacheEntry e;
	e.name = mapKey;
	e.isMultiplayer = true;
	e.numPlayers = 16;
	e.extentMax = Coord3D{ 4920.0f, 4920.0f, 0.0f };
	for (int i = 0; i < 16; ++i)
	{
		e.startPositions.emplace(i + 1, position(i));
	}
	return e;
}

inline maptest::Bytes mapBytes()
{
	using namespace maptest;
	Toc toc;
	W heights;
	heights.i32(512).i32(512).i32(10).i32(1).i32(492).i32(492).i32(512 * 512).zeros(512 * 512 * 2);
	Bytes chunks = chunk(toc, "HeightMapData", 5, heights.b);
	D world(toc);
	world.i("weather", 0).s("mapName", "p16_fixture").b("isScenarioMultiplayer", false);
	chunks = cat(chunks, chunk(toc, "WorldInfo", 1, world.bytes()));
	Bytes objects, positions;
	for (int i = 0; i < 16; ++i)
	{
		D waypoint(toc);
		waypoint.i("waypointID", i + 1).s("waypointName", "Player_" + std::to_string(i + 1) + "_Start");
		const Coord3D at = position(i);
		objects = cat(objects, chunk(toc, "Object", 3, W().f32(at.x).f32(at.y).f32(at.z).f32(0).i32(0)
			.astr("*Waypoints/Waypoint").raw(waypoint.bytes()).b));
		positions = cat(positions, chunk(toc, "MPPositionInfo", 1, W().u8(1).u8(1).u8(1).i32(-1).i32(0).b));
		for (int dx : { -300, 300 })
		{
			for (int dy : { -300, 300 })
			{
				D farm(toc);
				farm.s("originalOwner", "");
				objects = cat(objects, chunk(toc, "Object", 3, W().f32(at.x + dx).f32(at.y + dy).f32(0).f32(0).i32(0)
					.astr("FarmTemplate").raw(farm.bytes()).b));
			}
		}
	}
	chunks = cat(chunks, chunk(toc, "ObjectsList", 3, objects));
	chunks = cat(chunks, chunk(toc, "MPPositionList", 0, positions));
	W sides, teams;
	sides.u8(1).i32(3);
	teams.i32(3);
	for (const char *name : { "", "PlyrCivilian", "PlyrCreeps" })
	{
		D side(toc), team(toc);
		side.s("playerName", name).s("playerFaction", std::string(name) == "PlyrCivilian" ? "FactionCivilian" : "FactionNeutral")
			.b("playerIsHuman", false);
		sides.raw(side.bytes()).i32(0);
		team.s("teamName", std::string("team") + name).s("teamOwner", name).b("teamIsSingleton", true);
		teams.raw(team.bytes());
	}
	chunks = cat(chunks, chunk(toc, "SidesList", 6, sides.b));
	chunks = cat(chunks, chunk(toc, "Teams", 1, teams.b));
	return file(toc, chunks);
}

class MapArchive : public ArchiveFile
{
public:
	MapArchive() : m_bytes(mapBytes())
	{
		ArchivedFileInfo f;
		f.m_filename = "p16_fixture.map";
		f.m_archiveFilename = getName();
		f.m_size = (std::uint32_t)m_bytes.size();
		addFile("maps\\p16_fixture\\", &f);
	}
	bool readFile(const std::string &path, std::vector<std::uint8_t> &out, std::string *error) override
	{
		if (path != mapKey && path != "maps\\p16_fixture\\p16_fixture.map")
		{
			if (error) *error = "P16 generated archive: unknown file " + path;
			return false;
		}
		out = m_bytes;
		return true;
	}
	std::string getName() const override { return "p16-generated-memory"; }
	std::string getPath() const override { return getName(); }
private:
	maptest::Bytes m_bytes;
};

// lane P16 (review): the generated fixture must never enter the shared pureMount corpus — corpus tests pin the archive map
// count — so the P16 tests run on their own pure-2.01 mount; the fixture archive is mounted there once
inline retailtest::Mount *privateMount()
{
	static std::unique_ptr<retailtest::Mount> m = retailtest::freshPureMount();
	if (m && m->fs && !m->fs->doesFileExist(mapKey))
	{
		m->fs->mountArchive(std::make_unique<MapArchive>(), "p16-generated-memory", true);
	}
	return m.get();
}

// mirrors starttest::load on the private mount (the world's stores load once per Shared, about 12 s)
inline bool loadShared(starttest::Shared &s)
{
	s.mount = privateMount();
	if (!s.mount || !s.mount->fs)
	{
		return false;
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

// the P16 tests' own shared world (loaded once per process); nullptr when the install variables are unset
inline starttest::Shared *shared()
{
	static starttest::Shared s;
	static bool built = false;
	static bool available = false;
	if (!built)
	{
		built = true;
		available = loadShared(s);
	}
	return available ? &s : nullptr;
}

// a fresh world on the private mount, like starttest::freshWorld
inline std::unique_ptr<starttest::Shared> freshWorld()
{
	auto s = std::make_unique<starttest::Shared>();
	if (!loadShared(*s))
	{
		return nullptr;
	}
	return s;
}

inline void mount(starttest::Shared &s)
{
	if (!s.mount->fs->doesFileExist(mapKey))
	{
		s.mount->fs->mountArchive(std::make_unique<MapArchive>(), "p16-generated-memory", true);
	}
}

inline NewGameMessage message(starttest::Shared &s, int humans = 0)
{
	NewGameMessage m;
	m.game.mapName = mapKey;
	m.game.mapStartCount = 16;
	m.game.seed = 160201;
	m.game.startingCash = 1500;
	const char *factions[] = { "FactionMen", "FactionElves", "FactionDwarves", "FactionIsengard", "FactionMordor", "FactionWild", "FactionAngmar" };
	for (int i = 0; i < 16; ++i)
	{
		SkirmishGameSlot &slot = m.game.slots[i];
		slot.state = i < humans ? SLOT_PLAYER : (SlotState)((int)SLOT_EASY_AI + i % 4);
		slot.name = u"P16_";
		const std::string index = std::to_string(i + 1);
		slot.name.append(index.begin(), index.end());
		slot.playerTemplate = -1;
		for (int p = 0; p < s.world->playerTemplates().getPlayerTemplateCount(); ++p)
		{
			if (s.world->playerTemplates().getNthPlayerTemplate(p)->getName() == factions[i % 7]) slot.playerTemplate = p;
		}
		if (slot.playerTemplate < 0) throw std::runtime_error(std::string("P16 missing faction ") + factions[i % 7]);
		slot.startPos = i;
		slot.teamNumber = i / 2;
		slot.color = -1;
		slot.accepted = true;
	}
	return m;
}

struct Game
{
	NewGameStart start{ RandomAlgorithm::ZH_CarryChain };
	std::unique_ptr<ArchiveW3DFileSource> source;
	std::unique_ptr<WW3DAssetManager> assets;
	std::unique_ptr<LiveGame> live;
	bool load(starttest::Shared &s, const NewGameMessage &m, std::string *error)
	{
		mount(s);
		if (!NewGame::prepareNewGame(m, s.world->setupPlayerTemplates(), s.settings, { cacheEntry() }, RandomAlgorithm::ZH_CarryChain, start, error)) return false;
		source = std::make_unique<ArchiveW3DFileSource>(*s.mount->fs);
		assets = std::make_unique<WW3DAssetManager>(*source);
		live = std::make_unique<LiveGame>(*s.world, *s.mount->fs, *assets, s.options);
		LiveGame::Options options;
		options.start = &start;
		return live->load(options, error);
	}
};
} // namespace p16test

#define P16_REQUIRE_START(var)                                                                    \
	starttest::Shared *var = p16test::shared();                                                   \
	if (!var)                                                                                     \
	{                                                                                             \
		retailtest::printSkip("a P16 retail test (" __FILE__ ")");                                   \
		return;                                                                                     \
	}                                                                                             \
	REQUIRE_MESSAGE(var->mount->fs != nullptr, var->mount->error);                                \
	REQUIRE_MESSAGE(var->world != nullptr, var->error);                                           \
	auto var##Context = var->world->enterContext()
