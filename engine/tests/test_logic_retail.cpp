// OpenBFME retail tests for the live object layer (lane LOGIC-1). They run only when ROTWK_INSTALL and BFME2_INSTALL are set (otherwise
// they print SKIP). They mount pure RotWK 2.01 + BFME2 1.06, load the object world once (about 12 s), and run a skirmish map as live
// objects: players from the map's SidesList with lobby slots on top, every full map object created through ThingFactory::newObject,
// hordes creating their members, 300 logic frames, two runs compared by the state hash.
//
// Independent expectations: the census (workspace/rebuild/census) for the PlayerTemplates, and MAPOBJ-1's static classification of the
// same map for the object and horde member counts and the positions of the members of hordes without RandomOffset.

#include "doctest.h"
#include "GameClient/RenderInterpolation.h"
#include "GameLogic/Combat/CombatNames.h"

#include "Common/AsciiString.h"
#include "GameClient/DrawableManager.h"
#include "GameClient/LiveGame.h"
#include "GameClient/MapClassification.h"
#include "GameClient/MapObjectDrawables.h"
#include "GameClient/MapObjectRuntime.h"
#include "GameClient/MapUtil.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Map/TerrainLogic.h"
#include "GameLogic/MapObjectLoop.h"
#include "GameLogic/Module/UpdateModule.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/RetailObjectWorld.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"
#include "RetailTestMount.h"

#include <algorithm>
#include <iterator>
#include <cstdio>
#include <chrono>
#include <cstdio>
#include <map>
#include <memory>
#include <set>

namespace
{
struct SharedWorld
{
	retailtest::Mount *mount = nullptr;
	std::unique_ptr<RetailObjectWorld> world;
	std::string error;
	MapObjectOptions options;
};

SharedWorld &shared()
{
	static SharedWorld s;
	static bool built = false;
	if (!built)
	{
		built = true;
		s.mount = retailtest::pureMount();
		if (s.mount && s.mount->fs)
		{
			s.world = std::make_unique<RetailObjectWorld>(*s.mount->fs);
			if (!s.world->load(&s.error))
			{
				s.world.reset();
			}
			else if (!MapObjectGameData::load(*s.mount->fs, s.options, &s.error) || !MapObjectGameData::loadPlayerTemplates(*s.mount->fs, s.options, &s.error) ||
				!MapCreationHooks::load(*s.mount->fs, s.options.creationScripts, &s.error))
			{
				s.world.reset();
			}
		}
	}
	return s;
}

bool haveWorld()
{
	SharedWorld &s = shared();
	if (!s.mount)
	{
		retailtest::printSkip("logic retail");
		return false;
	}
	REQUIRE_MESSAGE(s.mount->fs != nullptr, s.mount->error);
	REQUIRE_MESSAGE(s.world != nullptr, s.error);
	return true;
}

std::string lowered(const std::string &s) { return AsciiStringUtil::lowered(s); }

struct RunStats
{
	std::string map;
	size_t classifiedObjects = 0;    // full objects (not horde members) of the static classification
	size_t classifiedMembers = 0;    // placed horde members of the static classification
	size_t classifiedUnplaced = 0;
	size_t classifiedHordes = 0;
	MapObjectLoopResult loop;
	size_t liveObjects = 0;
	size_t hordeModules = 0;
	std::vector<std::uint32_t> hashes;   // at frames 0, 100, 200, 300
	GameLogic::Report report;
	std::vector<std::string> playerErrors, slotErrors;
	size_t positionChecked = 0, positionMismatch = 0, memberStatusMissing = 0, memberNotContained = 0, memberTeamMismatch = 0, randomOffsetHordes = 0;
	double loopSeconds = 0, framesSeconds = 0, hashMillis = 0;
	bool schedulerConsistent = true;
	std::string schedulerProblem;
	std::vector<std::string> playerSummary;
};

void checkScheduler(GameLogic &logic, RunStats &st)
{
	size_t counted = 0;
	for (int p = 0; p < PHASE_COUNT; ++p)
	{
		const std::vector<UpdateModule *> &v = logic.updateVector(p);
		for (size_t i = 0; i < v.size(); ++i)
		{
			if (v[i]->friend_getIndexInLogic() != (int)i || v[i]->friend_getPhaseInLogic() != p)
			{
				st.schedulerConsistent = false;
				st.schedulerProblem = "updates[" + std::to_string(p) + "][" + std::to_string(i) + "] has index " + std::to_string(v[i]->friend_getIndexInLogic()) + " phase " + std::to_string(v[i]->friend_getPhaseInLogic());
				return;
			}
			++counted;
		}
	}
	const std::vector<UpdateModule *> &sl = logic.sleepingVector();
	for (size_t i = 0; i < sl.size(); ++i)
	{
		if (sl[i]->friend_getIndexInLogic() != (int)i || sl[i]->friend_getPhaseInLogic() != -1)
		{
			st.schedulerConsistent = false;
			st.schedulerProblem = "sleeping[" + std::to_string(i) + "] has index " + std::to_string(sl[i]->friend_getIndexInLogic()) + " phase " + std::to_string(sl[i]->friend_getPhaseInLogic());
			return;
		}
		++counted;
	}
	size_t modules = 0;
	for (const Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		for (const auto &m : o->modules())
		{
			modules += const_cast<BehaviorModule &>(*m).asUpdateModule() ? 1 : 0;
		}
	}
	if (modules != counted)
	{
		st.schedulerConsistent = false;
		st.schedulerProblem = "the vectors hold " + std::to_string(counted) + " update modules, the objects have " + std::to_string(modules);
	}
}

} // namespace

TEST_CASE("logic retail: the PlayerTemplate blocks of the install parse with no error and match the census")
{
	if (!haveWorld())
	{
		return;
	}
	SharedWorld &s = shared();
	for (const SubsystemLoadReport::FileError &e : s.world->report().errors)
	{
		INFO(e.file << ": " << e.message);
		CHECK(false);
	}
	const PlayerTemplateStore &store = s.world->playerTemplates();
	// census player_templates: name, side, playable in skirmish, observer, starting building
	struct Expect
	{
		const char *name, *side;
		bool playable, observer;
		const char *building;
	};
	const Expect expect[] = {
		{ "FactionCivilian", "Civilian", false, false, "" }, { "FactionNeutral", "Neutral", false, false, "" }, { "FactionObserver", "Observer", false, true, "" },
		{ "FactionMen", "Men", true, false, "MenFortress" }, { "FactionTutorial", "Men", false, false, "MenFortress" }, { "FactionElves", "Elves", true, false, "ElvenFortress" },
		{ "FactionDwarves", "Dwarves", true, false, "DwarvenFortress" }, { "FactionIsengard", "Isengard", true, false, "IsengardFortress" },
		{ "FactionMordor", "Mordor", true, false, "MordorFortress" }, { "FactionWild", "Wild", true, false, "WildFortress" },
		{ "FactionAngmar", "Angmar", true, false, "AngmarFortress" }, { "FactionArnor", "Arnor", false, false, "ArnorFortress" } };
	REQUIRE(store.getPlayerTemplateCount() == 12);
	for (int i = 0; i < 12; ++i)
	{
		const PlayerTemplate *t = store.getNthPlayerTemplate(i);
		INFO(expect[i].name);
		CHECK(t->getName() == expect[i].name);
		CHECK(t->m_side == expect[i].side);
		CHECK(t->m_isObserver == expect[i].observer);
		CHECK(t->m_startingBuilding == expect[i].building);
		CHECK((t->m_playableSide && !t->m_isObserver) == expect[i].playable);
	}
	CHECK(store.playableSideIndices().size() == 7); // the seven skirmish factions
	for (const std::string &u : s.world->playerTemplates().unverified())
	{
		std::printf("  stop: %s\n", u.c_str());
	}
}

namespace
{
// One complete run: the map as live objects, 300 frames, hashes at frames 0, 100, 200, 300 (the scheduler is checked at each).
void fullRun(const std::string &mapName, const SkirmishSetup &setup, std::vector<std::uint32_t> &hashes, RunStats &statsOut)
{
	SharedWorld &s = shared();
	std::vector<std::uint8_t> bytes;
	std::string err;
	REQUIRE_MESSAGE(s.mount->fs->readFile(MapClassification::mapPath(lowered(mapName)), bytes, &err), err);
	LoadedMap map;
	TerrainLogic terrain;
	MapObjectDrawables classified;
	MapClassification::Result cr;
	REQUIRE_MESSAGE(MapClassification::run(bytes, lowered(mapName), lowered(mapName), *s.world, s.options, map, terrain, classified, nullptr, cr, &err), err);
	CHECK(cr.mapIni.errors.empty());
	for (const MapObjectDrawable &d : classified.drawables)
	{
		if (d.fate == MAPOBJ_OBJECT || d.fate == MAPOBJ_OBJECT_BRIDGE)
		{
			(d.hordeMember ? statsOut.classifiedMembers : statsOut.classifiedObjects) += 1;
		}
	}
	statsOut.classifiedHordes = classified.report.hordes;
	statsOut.classifiedUnplaced = classified.report.hordeUnplaced;
	TeamFactory teams;
	PlayerList players(s.world->nameKeys(), s.world->playerTemplates(), teams);
	GameLogicSettings settings;
	REQUIRE_MESSAGE(GameLogicSettingsLoader::load(*s.mount->fs, settings, &err), err);
	statsOut.playerErrors = players.newGame(map.sides, settings.defaultStartingCash);
	if (!setup.players.empty())
	{
		SkirmishSetup slots = setup;
		slots.defaultStartingCash = settings.defaultStartingCash;
		statsOut.slotErrors = players.applySkirmishSlots(slots);
	}
	for (int i = 0; i < players.getPlayerCount(); ++i)
	{
		const Player *p = players.getNthPlayer(i);
		statsOut.playerSummary.push_back(std::to_string(i) + " '" + p->getPlayerName() + "' " + (p->getPlayerTemplate() ? p->getPlayerTemplate()->getName() : std::string("-")) + " $" + std::to_string(p->getMoney()->countMoney()));
	}
	GameLogic logic(s.world->things(), s.world->modules(), players, RandomAlgorithm::ZH_CarryChain);
	logic.settings() = settings;
	logic.castleTemplates().setLoader(CastleTemplateStore::fileSystemLoader(*s.mount->fs)); // BUILD-1: the map's castles unpack their base layouts
	logic.setTerrain(&terrain);
	logic.random().seedRandom(12345);
	const auto t0 = std::chrono::steady_clock::now();
	statsOut.loop = MapObjectLoop::create(logic, map, classified, nullptr);
	statsOut.loopSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
	statsOut.liveObjects = logic.getObjectCount();
	// positions of the members of hordes without RandomOffset against the static classification
	std::map<size_t, std::vector<const MapObjectDrawable *>> staticMembers;
	for (const MapObjectDrawable &d : classified.drawables)
	{
		if (d.hordeMember)
		{
			staticMembers[d.objectIndex].push_back(&d);
		}
	}
	for (size_t k = 0; k < statsOut.loop.ids.size(); ++k)
	{
		Object *horde = logic.findObjectByID(statsOut.loop.ids[k]);
		HordeContain *hc = nullptr;
		for (const char *cls : { "HordeContain", "HorseHordeContain" })
		{
			if (!hc)
			{
				hc = dynamic_cast<HordeContain *>(horde->findModule(cls));
			}
		}
		if (!hc)
		{
			continue;
		}
		++statsOut.hordeModules;
		const bool randomOffset = hc->hasRandomOffset();
		statsOut.randomOffsetHordes += randomOffset ? 1 : 0;
		for (const Object *m : *hc->getContainedItemsList())
		{
			statsOut.memberNotContained += m->getContainedBy() == horde ? 0 : 1;
			statsOut.memberStatusMissing += m->testStatus((unsigned)ObjectTemplateInfoBuilder::objectStatusIndex("HORDE_MEMBER")) ? 0 : 1;
			statsOut.memberTeamMismatch += m->getTeam() == horde->getTeam() ? 0 : 1;
		}
		auto it = staticMembers.find(statsOut.loop.objectIndices[k]);
		if (randomOffset || it == staticMembers.end())
		{
			continue;
		}
		size_t si = 0;
		for (const Object *m : *hc->getContainedItemsList())
		{
			if (hc->getMemberSlot(m) < 0 || si >= it->second.size())
			{
				continue;
			}
			const MapObjectDrawable &sd = *it->second[si++];
			++statsOut.positionChecked;
			const Coord3D &p = *m->getPosition();
			if (p.x != sd.position.x || p.y != sd.position.y || p.z != sd.position.z || m->getTemplate()->getName() != sd.info->name)
			{
				++statsOut.positionMismatch;
			}
		}
	}
	hashes.push_back(logic.computeStateHash());
	{
		// the hash is meant to be computed every frame by lockstep: it must stay cheap (a bound far above what it costs, to catch a regression to
		// something quadratic or allocating)
		const auto th = std::chrono::steady_clock::now();
		std::uint32_t sink = 0;
		for (int i = 0; i < 20; ++i)
		{
			sink ^= logic.computeStateHash();
		}
		statsOut.hashMillis = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - th).count() / 20.0;
		CHECK(sink == 0);
	}
	const auto t1 = std::chrono::steady_clock::now();
	for (int f = 1; f <= 300; ++f)
	{
		logic.runLogicFrame();
		if (f % 100 == 0)
		{
			hashes.push_back(logic.computeStateHash());
			checkScheduler(logic, statsOut);
		}
	}
	statsOut.framesSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t1).count();
	statsOut.report = logic.report();
	CHECK(logic.getFrame() == 300);
}

void printStats(const RunStats &a)
{
	std::printf("  info: %s: %zu live objects (%zu from the map list incl. %zu in the bridge pass, %zu contained in %zu hordes), loop %.2f s, 300 frames %.2f s, state hash %.3f ms\n", a.map.c_str(),
		a.liveObjects, a.loop.created, a.loop.bridgePass, a.loop.contained, a.loop.hordes, a.loopSeconds, a.framesSeconds, a.hashMillis);
	// (the state hash time is reported, not asserted: a wall-clock threshold is not a correctness gate and fails under sanitizers and loaded machines;
	// performance thresholds belong to an opt-in benchmark under controlled conditions, milestone M8)
	for (const auto &kv : a.loop.ownerFallbacks)
	{
		std::printf("  info: owner fallback: %s (%zu)\n", kv.first.c_str(), kv.second);
	}
	for (const auto &kv : a.loop.unportedKeys)
	{
		std::printf("  info: unported key %s: %zu\n", kv.first.c_str(), kv.second);
	}
	for (const std::string &e : a.playerErrors)
	{
		std::printf("  info: player setup note: %s\n", e.c_str());
	}
	for (const std::string &e : a.report.errors)
	{
		std::printf("  error: %s\n", e.c_str());
	}
	for (const auto &kv : a.report.unportedModules)
	{
		std::printf("  info: unported %-34s modules %5zu objects %5zu\n", kv.first.c_str(), kv.second.modules, kv.second.objects);
	}
}

void checkRun(const RunStats &a, const RunStats &b, const std::vector<std::uint32_t> &h1, const std::vector<std::uint32_t> &h2)
{
	// every full object of the static classification exists live, in the same count
	CHECK(a.slotErrors.empty());
	CHECK(a.loop.errors.empty());
	CHECK(a.loop.created == a.classifiedObjects);
	CHECK(a.loop.created > 50);
	// the members: the static path places members on slots; the live horde also makes the ones that found no slot (the whole payload)
	CHECK(a.loop.contained == a.classifiedMembers + a.classifiedUnplaced);
	CHECK(a.loop.hordes == a.classifiedHordes);
	CHECK(a.liveObjects == a.loop.created + a.loop.contained);
	CHECK(a.hordeModules == a.loop.hordes);
	CHECK(a.memberNotContained == 0);
	CHECK(a.memberStatusMissing == 0);
	CHECK(a.memberTeamMismatch == 0);
	CHECK(a.positionMismatch == 0);
	CHECK(a.schedulerConsistent);
	if (!a.schedulerConsistent)
	{
		FAIL_CHECK(a.schedulerProblem);
	}
	CHECK(a.report.errors.empty());
	// determinism: two runs of the same inputs give the same hash at every checkpoint
	REQUIRE(h1.size() == 4);
	CHECK(h1 == h2);
	CHECK(a.loop.created == b.loop.created);
	CHECK(a.liveObjects == b.liveObjects);
	CHECK(h1[0] != h1[1]); // the frame is in the hash
	// the retail objects that are not ported are reported by class
	CHECK_FALSE(a.report.unportedModules.empty());
}
} // namespace

TEST_CASE("logic retail: a skirmish map loads as live objects with lobby slots; the scheduler stays consistent over 300 frames; two runs hash alike")
{
	if (!haveWorld())
	{
		return;
	}
	SkirmishSetup setup;
	setup.players.push_back({ "Player_1", "FactionMen", true, 0, 0, 0 });
	setup.players.push_back({ "Player_2", "FactionElves", false, 0, 0, 1 });
	setup.players.push_back({ "Player_3", "FactionMordor", false, 1, 0, 2 });
	setup.players.push_back({ "Player_4", "FactionIsengard", false, 1, 0, 3 });
	std::vector<std::uint32_t> h1, h2;
	RunStats a, b;
	a.map = b.map = "map mp fall back 4p";
	fullRun(a.map, setup, h1, a);
	fullRun(b.map, setup, h2, b);
	printStats(a);
	for (const std::string &p : a.playerSummary)
	{
		std::printf("  info: player %s\n", p.c_str());
	}
	for (const std::string &st : a.report.stops)
	{
		std::printf("  stop: %s\n", st.c_str());
	}
	CHECK(a.loop.created == 175); // MAPOBJ-1's classification of this map (the MAP-1 survey counts 1209 objects, 175 full ones)
	CHECK(a.loop.hordes == 0);
	// the lobby slots took over the map's Player_N sides: faction, money (the faction's or the game's), colour and relationships
	REQUIRE(a.playerSummary.size() == 15);
	// no faction defines StartMoney, so each takes GameData's DefaultStartingCash (1500 in gamedata.ini)
	CHECK(a.playerSummary[10] == "10 'Player_1' FactionMen $1500");
	CHECK(a.playerSummary[11] == "11 'Player_2' FactionElves $1500");
	CHECK(a.playerSummary[12] == "12 'Player_3' FactionMordor $1500");
	checkRun(a, b, h1, h2);
}

TEST_CASE("logic retail: hordes create their members as live objects on their formation slots (a map with 91 hordes)")
{
	if (!haveWorld())
	{
		return;
	}
	std::vector<std::uint32_t> h1, h2;
	RunStats a, b;
	a.map = b.map = "map good celduin";
	fullRun(a.map, SkirmishSetup(), h1, a);
	fullRun(b.map, SkirmishSetup(), h2, b);
	printStats(a);
	CHECK(a.loop.hordes == 91);                              // MAPOBJ-1: 91 hordes
	CHECK(a.loop.contained == 1461 + a.classifiedUnplaced);  // 1461 placed members (+ any unplaced)
	CHECK(a.positionChecked > 300);                          // the members of the hordes without RandomOffset
	CHECK(a.randomOffsetHordes + 0 <= a.loop.hordes);
	std::printf("  info: %zu of %zu hordes have RandomOffset (their members are not compared by position); %zu members compared\n", a.randomOffsetHordes, a.loop.hordes, a.positionChecked);
	checkRun(a, b, h1, h2);
}

TEST_CASE("logic retail: live drawables show the same models as MAPOBJ-1's static draw runtime, create with their objects and follow them (a map with hordes)")
{
	if (!haveWorld())
	{
		return;
	}
	SharedWorld &s = shared();
	const std::string mapName = "map good celduin";
	std::vector<std::uint8_t> bytes;
	std::string err;
	REQUIRE_MESSAGE(s.mount->fs->readFile(MapClassification::mapPath(lowered(mapName)), bytes, &err), err);
	LoadedMap map;
	TerrainLogic terrain;
	MapObjectDrawables classified;
	MapClassification::Result cr;
	REQUIRE_MESSAGE(MapClassification::run(bytes, lowered(mapName), lowered(mapName), *s.world, s.options, map, terrain, classified, nullptr, cr, &err), err);
	CHECK(cr.mapIni.errors.empty());

	// the static draw runtime over the full objects of the classification, kept alive: its models are the expectation PER PLACEMENT (MapPlacedModel::drawable is the placement's index
	// in `sub.drawables`, the order the live objects are created in)
	std::multiset<std::string> staticModels;
	size_t staticFullDrawables = 0;
	ArchiveW3DFileSource source(*s.mount->fs);
	WW3DAssetManager assets(source);
	MapObjectDrawables sub;
	std::unique_ptr<MapObjectRuntime> runtime;
	std::vector<std::vector<std::string>> expectedPristine;
	{
		sub.sides = classified.sides;
		for (const MapObjectDrawable &d : classified.drawables)
		{
			if (d.fate == MAPOBJ_OBJECT || d.fate == MAPOBJ_OBJECT_BRIDGE)
			{
				sub.drawables.push_back(d);
			}
		}
		runtime = std::make_unique<MapObjectRuntime>(assets);
		runtime->build(sub);
		expectedPristine.assign(sub.drawables.size(), {});
		for (const MapPlacedModel &p : runtime->models())
		{
			if (p.kind == W3D_DRAWKIND_MODEL && p.draw)
			{
				const std::string &m = p.draw->frame().modelName;
				if (!m.empty())
				{
					staticModels.insert(lowered(m));
					expectedPristine[p.drawable].push_back(lowered(m));
				}
			}
		}
		staticFullDrawables = sub.drawables.size();
	}

	TeamFactory teams;
	PlayerList players(s.world->nameKeys(), s.world->playerTemplates(), teams);
	GameLogicSettings settings;
	REQUIRE_MESSAGE(GameLogicSettingsLoader::load(*s.mount->fs, settings, &err), err);
	players.newGame(map.sides, settings.defaultStartingCash);
	GameLogic logic(s.world->things(), s.world->modules(), players, RandomAlgorithm::ZH_CarryChain);
	logic.settings() = settings;
	logic.castleTemplates().setLoader(CastleTemplateStore::fileSystemLoader(*s.mount->fs)); // BUILD-1: the map's castles unpack their base layouts
	logic.settings().night = map.chunks.hasGlobalLighting && map.chunks.lighting.timeOfDay == 4;
	logic.settings().snowy = map.chunks.hasWorldInfo && map.chunks.worldInfo.getInt("weather") == 1;
	logic.setTerrain(&terrain);
	DrawableManager drawables(assets, logic);
	ClientEventRecorder recorder(logic); // SMOOTH-1: the logic's client calls are ordered events, applied by the render side's manager
	logic.setClientHooks(&recorder);
	const MapObjectLoopResult loop = MapObjectLoop::create(logic, map, classified, [](Object &obj, const MapObjectDrawable &d) { DrawableManager::applyPlacement(obj, d); });
	drawables.applyEvents(recorder.take());
	auto drawableOf = [&drawables](const Object *o) { return drawables.findByObject(o->getID()); };
	CHECK(loop.errors.empty());
	CHECK(logic.getObjectCount() == staticFullDrawables);

	// every live object has a drawable (the retail objects of this map all have draw modules); the drawable ids count in creation order.
	// COMBAT-2: a map object placed with a low initial health has the model conditions of its damage state (DAMAGED / REALLYDAMAGED / RUBBLE) and is drawn with its damaged model
	// (lbhouses_01 -> lbhouses_01_d); MAPOBJ-1's static runtime draws every object pristine. The comparison is exact PER OBJECT: a damaged object must carry exactly the model
	// condition of its body's state, its models with that condition removed are what the static runtime draws (the multisets then agree exactly), and every pristine object shows the
	// models of the static runtime untouched.
	auto modelsOf = [&](const Object *o) {
		drawables.applyEvents(recorder.take()); // the condition changes the logic made since
		std::vector<std::string> out;
		for (const DrawEntry &e : drawableOf(o)->entries())
		{
			if (e.kind == W3D_DRAWKIND_MODEL && e.draw && !e.draw->frame().modelName.empty())
			{
				out.push_back(lowered(e.draw->frame().modelName));
			}
		}
		return out;
	};
	const int damagedBit = CombatNames::modelCondition("DAMAGED"), reallyBit = CombatNames::modelCondition("REALLYDAMAGED"), rubbleBit = CombatNames::modelCondition("RUBBLE");
	// the expected damaged models of placement `i` in state `state`: the static runtime's own draws of that placement re-matched for the placement's flags plus the damage condition
	// (independent of the live object's drawable), restored afterwards
	auto expectedDamaged = [&](size_t i, int bit) {
		ModelConditionFlags flags = sub.drawables[i].flags;
		flags.set(bit);
		std::vector<std::string> out;
		for (const MapPlacedModel &p : runtime->models())
		{
			if (p.drawable == i && p.kind == W3D_DRAWKIND_MODEL && p.draw)
			{
				p.draw->setModelConditionFlags(flags);
				if (!p.draw->frame().modelName.empty())
				{
					out.push_back(lowered(p.draw->frame().modelName));
				}
				p.draw->setModelConditionFlags(sub.drawables[i].flags);
			}
		}
		std::sort(out.begin(), out.end());
		return out;
	};
	size_t withDrawable = 0, damagedObjects = 0, damagedDraws = 0, placement = 0;
	std::multiset<std::string> liveModels;
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject(), ++placement)
	{
		REQUIRE_MESSAGE(drawableOf(o) != nullptr, o->getTemplate()->getName());
		REQUIRE(placement < sub.drawables.size());
		// the object is the placement's: same template, same place
		CHECK_MESSAGE(lowered(o->getTemplate()->getName()) == lowered(sub.drawables[placement].info->name), "placement " << placement);
		CHECK(drawableOf(o)->getObjectID() == o->getID());
		++withDrawable;
		const BodyDamageType state = o->getBodyModule() ? o->getBodyModule()->getDamageState() : BODY_PRISTINE;
		const bool d1 = o->testModelCondition(damagedBit), d2 = o->testModelCondition(reallyBit), d3 = o->testModelCondition(rubbleBit);
		// exactly the condition of the state (and none for a pristine body)
		const bool exactCondition = d1 == (state == BODY_DAMAGED) && d2 == (state == BODY_REALLYDAMAGED) && d3 == (state == BODY_RUBBLE);
		CHECK_MESSAGE(exactCondition, o->getTemplate()->getName());
		std::vector<std::string> shown = modelsOf(o);
		std::sort(shown.begin(), shown.end());
		std::vector<std::string> expected = expectedPristine[placement];
		std::sort(expected.begin(), expected.end());
		if (state != BODY_PRISTINE)
		{
			++damagedObjects;
			const int mine = state == BODY_DAMAGED ? damagedBit : state == BODY_REALLYDAMAGED ? reallyBit : rubbleBit;
			// the live damaged draw is what the static runtime shows for the placement with that condition added
			const std::vector<std::string> wanted = expectedDamaged(placement, mine);
			CHECK_MESSAGE(shown == wanted, o->getTemplate()->getName() << " (placement " << placement << ") shows another damaged draw than the static runtime's");
			// supplemental: the live draw with the condition removed is the pristine one, and restoring the condition restores the damaged one
			Object::ModelConditionBits clear{}, none{};
			for (int bit : { damagedBit, reallyBit, rubbleBit })
			{
				clear[(size_t)bit >> 5] |= 1u << (bit & 31);
			}
			Object::ModelConditionBits set{};
			set[(size_t)mine >> 5] |= 1u << (mine & 31);
			o->clearAndSetModelConditionFlags(clear, none);
			std::vector<std::string> pristine = modelsOf(o);
			std::sort(pristine.begin(), pristine.end());
			o->clearAndSetModelConditionFlags(none, set);
			std::vector<std::string> restored = modelsOf(o);
			std::sort(restored.begin(), restored.end());
			CHECK_MESSAGE(restored == shown, o->getTemplate()->getName() << ": the damaged draw changed when its condition was restored");
			damagedDraws += pristine != shown ? 1 : 0;
			shown = pristine;
		}
		// the (normalised) models of THIS placement are the static runtime's for it, in particular a pristine object shows exactly its own placement's models
		CHECK_MESSAGE(shown == expected, o->getTemplate()->getName() << " (placement " << placement << ") does not show its placement's static models");
		for (const std::string &m : shown)
		{
			liveModels.insert(m);
		}
	}
	CHECK(placement == sub.drawables.size());
	CHECK(withDrawable == logic.getObjectCount());
	CHECK(drawables.liveCount() == logic.getObjectCount());
	CHECK(liveModels == staticModels);
	CHECK(damagedObjects > 0);
	CHECK(damagedDraws > 0); // the damaged state really draws another model (lbhouses_01_d ...)
	std::printf("  info: %zu damaged map objects, %zu of them draw another model than the pristine one\n", damagedObjects, damagedDraws);
	CHECK(liveModels.size() > 1000);
	const DrawableManager::Report r0 = drawables.report();
	for (const std::string &e : r0.errors)
	{
		std::printf("  error: %s\n", e.c_str());
	}
	CHECK(r0.errors.empty());
	CHECK(r0.created == logic.getObjectCount());

	// the events: one "created" per drawable, in creation order, none destroyed yet
	const std::vector<DrawableManager::Event> events = drawables.takeEvents();
	REQUIRE(events.size() == logic.getObjectCount());
	for (size_t i = 0; i < events.size(); ++i)
	{
		CHECK(events[i].created);
		CHECK(events[i].id == i + 1);
	}
	CHECK(drawables.takeEvents().empty());

	// the render transform follows the object: before any record the drawable is where the object is, after a logic frame it interpolates
	Object *someObject = logic.getFirstObject();
	logic.runLogicFrame();
	Coord3D moved = *someObject->getPosition();
	moved.x += 100.0f;
	someObject->setPosition(&moved);
	logic.runLogicFrame(); // phase 2 records the old position as the recorded transform... of this frame's start (moved + 100)
	// recorded = position at the start of the frame (= moved); now move again and check the lerp between recorded and current
	Coord3D again = moved;
	again.x += 40.0f;
	someObject->setPosition(&again);
	// SMOOTH-1: the pose is drawn from a snapshot of the completed state (RW 0x674B1F) with retail's Catmull-Rom (RW 0x6765B9): P0 = the previous
	// record's position (moved - 100), P1 = the record (moved), P2 = the current (moved + 40), P3 = no pending position (the current). By hand,
	// relative to P0: 0.5 * (2 * 100 + 140 * s + (0 - 500 + 560 - 140) * s^2 + (140 - 420 + 300 - 0) * s^3) = 126.25 at s = 0.5
	Drawable *dr = drawableOf(someObject);
	std::shared_ptr<const LogicSnapshot> snap = LogicSnapshot::build(logic, nullptr, false, 0);
	const ObjectSnapshot *rec = snap->find(someObject->getID());
	REQUIRE(rec);
	dr->syncFromSnapshot(*rec, snap->frame, 0.0, true);
	CHECK(dr->getPosition()->x == doctest::Approx(moved.x));
	CHECK(RenderInterpolation::retailPose(*rec, snap->frame, 0.5).position.x == doctest::Approx(moved.x - 100.0f + 126.25f));
	// lane SMOOTH-2: the drawable draws RenderInterpolation::smoothPose: without a look-ahead P3 is the step extrapolated (moved + 80); the Hermite tangents
	// (P2 - P0) / 2 = 70 and (P3 - P1) / 2 = 40 (both under twice the step): 0.125 * 70 + 0.5 * 40 - 0.125 * 40 = 23.75 at s = 0.5
	dr->syncFromSnapshot(*rec, snap->frame, 0.5, true);
	CHECK(dr->getPosition()->x == doctest::Approx(moved.x + 23.75f));
	dr->syncFromSnapshot(*rec, snap->frame, 1.0, true);
	CHECK(dr->getPosition()->x == doctest::Approx(again.x));

	// animations step and a destroyed object takes its drawable with it
	drawables.applyEvents(recorder.take());
	drawables.advance(200.0);
	drawables.syncTransforms(*snap, 0.25, true);
	// BUILD-1: the castles of the map unpacked on the logic's first frame: their pieces' creation events are still queued. Verify them (all creations) and drain them, so the
	// deletion below is measured alone
	{
		const std::vector<DrawableManager::Event> pending = drawables.takeEvents();
		for (const DrawableManager::Event &e : pending)
		{
			CHECK(e.created);
		}
	}
	const size_t liveBefore = drawables.liveCount();
	Object *victim = logic.getLastObject();
	const bool victimHoldsOthers = victim->getContain() != nullptr && victim->getContain()->getContainCount() > 0;
	REQUIRE(drawableOf(victim) != nullptr);
	logic.destroyObject(victim);
	logic.runLogicFrame();                    // phase 5 deletes it
	drawables.applyEvents(recorder.take());
	if (!victimHoldsOthers)
	{
		CHECK(drawables.liveCount() == liveBefore - 1);
		const std::vector<DrawableManager::Event> ev = drawables.takeEvents();
		REQUIRE(ev.size() == 1);
		CHECK_FALSE(ev[0].created);
		CHECK(drawables.find(ev[0].id) == nullptr);
	}
	std::printf("  info: %zu live drawables, %zu model draws (%zu animated), %zu data defects, %zu hide misses\n", r0.live, r0.modelDraws, r0.animatedModelDraws, r0.dataDefects.size(), r0.hideMisses.size());
	logic.reset();
	drawables.applyEvents(recorder.take());
	CHECK(drawables.liveCount() == 0);
	logic.setClientHooks(nullptr);
}

TEST_CASE("logic retail: LiveGame ticks the logic on retail's clock, independent of the render rate, and reports its stops")
{
	if (!haveWorld())
	{
		return;
	}
	SharedWorld &s = shared();
	ArchiveW3DFileSource source(*s.mount->fs);
	WW3DAssetManager assets(source);
	int spawnedObjects = 0;
	auto runGame = [&](double dt, int frames, std::map<UnsignedInt, std::uint32_t> &hashes, LiveGame::Report *reportOut, bool sixTick = false) {
		LiveGame game(*s.world, *s.mount->fs, assets, s.options);
		LiveGame::Options o;
		o.sixTickPacing = sixTick;
		o.mapName = "map mp fall back 4p";
		o.seed = 99;
		o.slots.players.push_back({ "Player_1", "FactionMen", true, 0, 0, 0 });
		o.slots.players.push_back({ "Player_2", "FactionMordor", false, 1, 0, 1 });
		std::string err;
		REQUIRE_MESSAGE(game.load(o, &err), err);
		CHECK(game.frame() == 1); // load runs the first logic frame
		hashes[game.frame()] = game.logic().computeStateHash();
		int ran = 0;
		while (game.frame() < (UnsignedInt)(1 + frames) || (sixTick && game.logic().getLastPhase() != 6))
		{
			const int n = game.advance(dt);
			ran += n;
			// a frame boundary: the hash is only comparable after phase 6 of a frame (six tick pacing stops mid frame between ticks)
			if (!sixTick || game.logic().getLastPhase() == 6)
			{
				hashes[game.frame()] = game.logic().computeStateHash();
			}
			REQUIRE(game.logic().getFrame() <= (UnsignedInt)(1 + frames) + 1);
		}
		CHECK(ran == frames);
		CHECK(game.droppedFrames() == 0);
		if (reportOut)
		{
			*reportOut = game.report();
			// lane MOD-4: the map's spawners (SpawnBehavior) add their spawns, which the map object loop did not create
			spawnedObjects = 0;
			for (Object *x = game.logic().getFirstObject(); x; x = x->getNextObject())
			{
				spawnedObjects += x->findModule("SlavedUpdate") && x->getProducerID() != INVALID_ID ? 1 : 0;
			}
		}
	};
	std::map<UnsignedInt, std::uint32_t> fine, coarse;
	LiveGame::Report rep;
	runGame(0.013, 12, fine, &rep);   // about 77 render frames per second
	runGame(0.2, 12, coarse, nullptr); // one logic frame per render frame
	std::map<UnsignedInt, std::uint32_t> ticks;
	runGame(1.0 / 30.0, 12, ticks, nullptr, true); // retail's pacing: one phase per engine tick
	REQUIRE(fine.size() == 13);
	REQUIRE(coarse.size() == 13);
	for (const auto &kv : coarse)
	{
		INFO("frame " << kv.first);
		REQUIRE(fine.count(kv.first) == 1);
		CHECK(fine.at(kv.first) == kv.second); // the logic never sees the render rate
		REQUIRE(ticks.count(kv.first) == 1);
		CHECK(ticks.at(kv.first) == kv.second); // nor how its phases were spread over the ticks
	}
	// the report carries every stop of every layer and the load-time facts
	CHECK(rep.map == "map mp fall back 4p");
	CHECK(rep.loop.created == rep.classifiedFull);
	CHECK(rep.classifiedFull == 175);
	CHECK(rep.clientOnlyObjects > 500);
	CHECK(rep.logic.objects == rep.loop.created + rep.loop.contained + rep.spellBooks + spawnedObjects); // SPELL-1: the lobby players' spell books; MOD-4: spawns
	CHECK(spawnedObjects > 0);
	CHECK(rep.spellBooks > 0);
	CHECK(rep.drawables.live == rep.logic.objects);
	CHECK(rep.errors.empty());
	std::set<std::string> ids;
	for (const std::string &st : rep.stops)
	{
		if (st.rfind("[S-", 0) == 0)
		{
			ids.insert(st.substr(1, 5));
		}
	}
	for (const char *want : { "S-080", "S-110", "S-140", "S-141", "S-142", "S-143", "S-147", "S-149", "S-150", "S-151" })
	{
		CHECK_MESSAGE(ids.count(want) == 1, "the report lacks " << want);
	}
	std::printf("  info: LiveGame report: %zu objects, %zu client-only, %zu drawables, stops %zu\n", rep.logic.objects, rep.clientOnlyObjects, rep.drawables.live, ids.size());
}

TEST_CASE("logic retail: a live map load makes exactly one creation draw per object through LuaScriptEngine::sendObjectCreated and runs OnCreated (FireArowTip hidden on Haradrim / Rohan archers and horde members)")
{
	if (!haveWorld())
	{
		return;
	}
	SharedWorld &s = shared();
	ArchiveW3DFileSource source(*s.mount->fs);
	WW3DAssetManager assets(source);
	LiveGame game(*s.world, *s.mount->fs, assets, s.options);
	LiveGame::Options o;
	o.mapName = "map good celduin";
	o.seed = 7;
	o.logRandomCalls = true;
	std::string err;
	REQUIRE_MESSAGE(game.load(o, &err), err);
	GameLogic &logic = game.logic();
	const size_t objects = logic.getObjectCount();
	REQUIRE(objects > 1000);
	const LiveGame::Report rep = game.report();

	// ONE creation path: every object drew GetGameLogicRandomValue(1, 999, "GameLogic.cpp", 0x19A7) exactly once and no other call of
	// the logic (build variations, the frame-1 updates) uses those bounds from that line
	std::vector<int> creationDraws;
	size_t otherDraws999 = 0;
	for (const GameLogicRandom::Call &c : logic.random().callLog())
	{
		if (!c.real && c.lo == 1 && c.hi == 999 && c.file == "GameLogic.cpp" && c.line == LuaScriptEngine::kCreationDrawLine)
		{
			creationDraws.push_back(c.result);
		}
		else if (!c.real && c.lo == 1 && c.hi == 999)
		{
			++otherDraws999;
		}
	}
	CHECK(otherDraws999 == 0);
	REQUIRE(creationDraws.size() == objects);
	CHECK(rep.scripting.creations == objects);
	CHECK(rep.drawables.created == objects);
	CHECK(rep.logic.creationsWithoutDispatch == 0);
	// each object's seed is one of those draws and each draw is one object's (a horde draws after its members: its onCreate makes them first, so the
	// order is not the id order)
	std::multiset<int> seeds;
	for (const Object *obj = logic.getFirstObject(); obj; obj = obj->getNextObject())
	{
		seeds.insert(obj->getCreationSeed());
	}
	CHECK(seeds == std::multiset<int>(creationDraws.begin(), creationDraws.end()));
	// the stop S-147 is narrowed, not closed, and says what went through the one path
	bool s147 = false;
	for (const std::string &l : rep.stops)
	{
		s147 = s147 || l.rfind("[S-147] live creation path: " + std::to_string(objects) + " objects", 0) == 0;
	}
	CHECK(s147);

	// OnCreated ran: the handlers of the AILuaEventsList hid FireArowTip on every full object the classification says has such a handler (archers and
	// the members of archer hordes), through LuaGameHost::drawableShowSubObject -> Drawable -> the draw module's script state
	const auto expectIt = game.classified().report.creationHookHides.find("firearowtip");
	REQUIRE(expectIt != game.classified().report.creationHookHides.end());
	size_t hidden = 0, hiddenMembers = 0;
	for (const Object *obj = logic.getFirstObject(); obj; obj = obj->getNextObject())
	{
		bool has = false;
		for (const DrawEntry &e : game.drawables().findByObject(obj->getID())->entries())
		{
			if (!e.draw)
			{
				continue;
			}
			for (const std::string &h : e.draw->frame().hiddenSubObjects)
			{
				// the hidden names are full sub object names ("<container>.<name>")
				const size_t dot = h.rfind('.');
				has = has || AsciiStringUtil::compareNoCase(dot == std::string::npos ? h : h.substr(dot + 1), "FireArowTip") == 0;
			}
		}
		if (has)
		{
			++hidden;
			hiddenMembers += obj->getContainedBy() != nullptr ? 1 : 0;
		}
	}
	CHECK(hidden == expectIt->second);
	CHECK(hidden > 0);
	CHECK(hiddenMembers > 0); // a horde member (its template has the handler, its contain is its horde)
	CHECK(rep.scripting.creationHookObjects > 0);
	CHECK(rep.scripting.creationHandlers.count("OnHaradrimArcherCreated") == 1);
	CHECK(rep.scripting.creationHandlers.count("OnRohanArcherCreated") == 1);
	CHECK(rep.scripting.errors.empty());
	for (const std::string &e : rep.errors)
	{
		std::printf("  error: %s\n", e.c_str());
	}
	for (const auto &kv : rep.scripting.creationHandlers)
	{
		std::printf("  info: handler %s: %zu\n", kv.first.c_str(), kv.second);
	}
	CHECK(rep.errors.empty());
	std::printf("  info: live creation path: %zu objects, %zu creation draws, %zu with an event list, %zu OnCreated handlers ran, FireArowTip hidden on %zu objects (%zu horde members)\n", objects,
		creationDraws.size(), rep.scripting.creationListObjects, rep.scripting.creationHookObjects, hidden, hiddenMembers);
}

TEST_CASE("logic retail: the GameData / AIData / MultiplayerSettings values the object layer reads come from the install")
{
	if (!haveWorld())
	{
		return;
	}
	SharedWorld &s = shared();
	GameLogicSettings g;
	std::string err;
	REQUIRE_MESSAGE(GameLogicSettingsLoader::load(*s.mount->fs, g, &err), err);
	// facts read from the INI files of the install (data\ini\gamedata.ini, default\aidata.ini, multiplayer.ini)
	CHECK(g.forceModelsToFollowTimeOfDay);
	CHECK(g.forceModelsToFollowWeather);
	CHECK(g.unitDamagedThreshold == doctest::Approx(0.65f));
	CHECK(g.unitReallyDamagedThreshold == doctest::Approx(0.4f));
	CHECK(g.enableRepulsors);
	CHECK(g.defaultStartingCash == 1500);
	CHECK(g.initialCredits[0] == 500);
	CHECK(g.initialCredits[1] == 1000);
	CHECK(g.initialCredits[2] == 1500);
	CHECK(g.initialCredits[3] == 2000);
	CHECK(g.initialCredits[4] == 2500);
	CHECK(g.bodyThresholdsLoaded);
	CHECK(g.startingCashLoaded);
	// through the shared INI pipeline: the three files were read, and the fields no row reads are recorded (S-152), nested blocks included
	CHECK(g.filesLoaded.size() == 3);
	CHECK(g.unappliedFields.size() > 100);
	CHECK(g.unappliedFields.count("AIData.SideInfo") == 1);
	CHECK(g.unappliedFields.count("AIData.SkirmishBuildList") == 1);
	CHECK(g.unappliedFields.count("GameData.MapName") == 1);
}
