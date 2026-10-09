// OpenBFME retail tests for the map object creation (lane MAPOBJ-1). Run only when ROTWK_INSTALL and BFME2_INSTALL are set (otherwise
// they print SKIP). They mount pure RotWK 2.01 + BFME2 1.06, load the whole object world (the 12 s subsystem INI load with the real
// object parsers and the typed draw data), then run the object loop over all 181 retail maps with each map's map.ini overrides and
// check it against the independent MAP-1 survey (engine/tests/data/map-survey.json) and the oracle facts cited per assertion.

#include "doctest.h"

#include "Common/AsciiString.h"
#include "Common/MiniJson.h"
#include "GameClient/MapObjectDrawables.h"
#include "GameClient/MapObjectRuntime.h"
#include "GameClient/MapUtil.h"
#include "GameLogic/Object/RetailObjectWorld.h"
#include "GameLogic/Map/TerrainLogic.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"
#include "MapCorpusUtil.h"
#include "RetailTestMount.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

using namespace mapcorpus;

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
			else if (!MapObjectGameData::load(*s.mount->fs, s.options, &s.error) || !MapCreationHooks::load(*s.mount->fs, s.options.creationScripts, &s.error))
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
		retailtest::printSkip("mapobj retail");
		return false;
	}
	REQUIRE_MESSAGE(s.mount->fs != nullptr, s.mount->error);
	REQUIRE_MESSAGE(s.world != nullptr, s.error);
	return true;
}

std::string dirOf(const std::string &path)
{
	return path.substr(0, path.find_last_of("/\\"));
}
} // namespace

TEST_CASE("mapobj retail: the object world loads every retail template with typed draw data and no INI error")
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
	// 3306 Object + 569 ChildObject + 759 ObjectReskin (census: 4634) plus the 23 templates of Crate.ini the census does not count
	// (test_object_model_retail.cpp pins the same 4657)
	CHECK(s.world->things().templateCount() == 4657);
	// every draw module of every template is typed (a raw one would be a class the binary registers and this lane did not bind)
	std::map<std::string, size_t> perClass, rawPerClass;
	for (const ThingTemplate *t : s.world->things().templates())
	{
		for (const ThingTemplate::Nugget &n : t->drawModules().nuggets())
		{
			++perClass[n.name];
			if (const W3DDrawClassInfo *ci = W3DDrawModules::find(n.name))
			{
				if (ci->kind != W3D_DRAWKIND_NOT_DRAWN && !dynamic_cast<const RawModuleData *>(n.data.get()) == false)
				{
					++rawPerClass[n.name];
				}
			}
		}
	}
	for (const auto &kv : perClass)
	{
		std::printf("  info: draw modules %-24s %zu\n", kv.first.c_str(), kv.second);
	}
	CHECK(rawPerClass.empty());
	std::printf("  info: object world loaded in %.1f s, %zu templates\n", s.world->loadSeconds(), s.world->things().templateCount());
}

TEST_CASE("mapobj retail: the object loop over all 181 maps explains every object and agrees with the MAP-1 survey")
{
	if (!haveWorld())
	{
		return;
	}
	SharedWorld &s = shared();
	std::vector<unsigned char> surveyBytes;
	std::string err;
	REQUIRE_MESSAGE(retailtest::readLocalFile(retailtest::dataDir() + "/map-survey.json", surveyBytes, &err), err);
	JsonValue survey;
	REQUIRE_MESSAGE(JsonValue::parse(std::string(surveyBytes.begin(), surveyBytes.end()), survey, &err), err);
	const JsonValue &surveyMaps = J(survey, "maps");

	FilenameList list;
	s.mount->fs->getFileListInDirectory("", "", "*.map", list, true);
	REQUIRE(list.size() == 181);

	std::vector<std::pair<size_t, std::string>> perMapModels;
	std::set<std::string> allModels;
	std::map<std::string, size_t> sideColors, sideFactions;
	std::map<std::string, size_t> runtimeErrors, runtimeDefects, runtimeHideMisses, ignoredFields, hooksByFunction, hooksByTemplate, hookHides, hookShows;
	size_t hookObjects = 0, scriptsRun = 0, mapsWithS091 = 0, mapsWithoutScan = 0, hooksRun = 0, creationDraws = 0;
	std::map<std::string, size_t> handlersRun;
	std::set<std::string> luaStops;
	ArchiveW3DFileSource runtimeSource(*s.mount->fs);
	WW3DAssetManager runtimeAssets(runtimeSource);
	size_t totalObjects = 0, mapsWithOverrides = 0;
	size_t byFate[MAPOBJ_FATE_COUNT] = {};
	std::map<std::string, size_t> drawClasses, notDrawn, unresolved, caseMismatch, unported, ownerProblems, templates;
	size_t hordes = 0, hordeSlots = 0, hordePayload = 0, hordeMembers = 0, hordeUnplaced = 0, hordeRandom = 0;
	size_t drawables = 0, models = 0, moved = 0, aligned = 0, protoScale = 0, night = 0, snow = 0, unowned = 0;
	std::vector<std::string> errors, mapIniErrors;
	std::set<std::string> overrideDefs;
	const auto t0 = std::chrono::steady_clock::now();
	std::vector<std::uint8_t> bytes;
	for (const std::string &path : list)
	{
		const std::string key = slashes(lowerStr(path), '/');
		INFO("map " << key);
		const JsonValue &sv = *surveyMaps.get(key);
		REQUIRE_MESSAGE(s.mount->fs->readFile(path, bytes, &err), err);
		LoadedMap m;
		MapReadOptions opt;
		REQUIRE_MESSAGE(MapReader::load(bytes, key, opt, m, &err), err);
		TerrainLogic terrain;
		std::vector<std::string> problems;
		terrain.init(m.heightMap, m.chunks, &problems);

		const RetailObjectWorld::MapIniResult mi = s.world->applyMapIni(slashes(dirOf(path), '\\'));
		for (const std::string &e : mi.errors)
		{
			mapIniErrors.push_back(key + ": " + e);
		}
		mapsWithOverrides += mi.mapIniFound || mi.soloIniFound;
		for (const std::string &d : mi.definitions)
		{
			overrideDefs.insert(d);
		}

		MapObjectDrawables out;
		MapObjectCreation::build(m, key, s.world->things(), terrain, s.options, out);
		const MapObjectReport &r = out.report;

		// every object has one record, every record one fate
		CHECK(r.objects == (size_t)N(J(J(sv, "objects"), "count")));
		CHECK(out.records.size() == r.objects);
		size_t sum = 0;
		for (size_t f = 0; f < MAPOBJ_FATE_COUNT; ++f)
		{
			sum += r.byFate[f];
			byFate[f] += r.byFate[f];
		}
		CHECK(sum == r.objects);
		// the survey's independent counts: waypoints (isWaypoint objects), road / bridge point flags, and no object the reader drops
		CHECK(r.byFate[MAPOBJ_WAYPOINT] == (size_t)N(J(J(sv, "waypoints"), "objects")));
		{
			size_t flagged = 0;
			for (const auto &kv : J(J(sv, "objects"), "flags").object)
			{
				if (std::stoi(kv.first) & (FLAG_ROAD_FLAGS | FLAG_BRIDGE_FLAGS))
				{
					flagged += (size_t)N(kv.second);
				}
			}
			CHECK(r.byFate[MAPOBJ_ROAD_BRIDGE_POINT] == flagged);
		}
		CHECK(r.byFate[MAPOBJ_CULLED_Z] == 0); // no retail object lies outside the reader's z range
		CHECK(r.byFate[MAPOBJ_NO_TEMPLATE_EMPTY] + r.byFate[MAPOBJ_NO_TEMPLATE_STAR] + r.byFate[MAPOBJ_LIGHT] == 0);
		for (const MapObjectDrawable &d : out.drawables)
		{
			for (const MapDrawModule &m : d.draws)
			{
				if (!m.model.empty())
				{
					allModels.insert(AsciiStringUtil::lowered(m.model));
				}
			}
		}
		for (const std::string &e : r.errors)
		{
			errors.push_back(key + ": " + e);
		}
		totalObjects += r.objects;
		for (const auto &kv : r.byDrawClass) drawClasses[kv.first] += kv.second;
		for (const auto &kv : r.notDrawnByReason) notDrawn[kv.first] += kv.second;
		for (const auto &kv : r.unresolved) unresolved[kv.first] += kv.second;
		for (const auto &kv : r.caseMismatch) caseMismatch[kv.first] += kv.second;
		for (const auto &kv : r.unportedKeys) unported[kv.first] += kv.second;
		for (const auto &kv : r.creationHooksByFunction) hooksByFunction[kv.first] += kv.second;
		for (const auto &kv : r.creationHooksByTemplate) hooksByTemplate[kv.first] += kv.second;
		for (const auto &kv : r.creationHookHides) hookHides[kv.first] += kv.second;
		for (const auto &kv : r.creationHookShows) hookShows[kv.first] += kv.second;
		hookObjects += r.creationHookObjects;
		mapsWithoutScan += r.creationHooksScanned ? 0 : 1;
		for (const auto &kv : r.ownerProblems) ownerProblems[kv.first] += kv.second;
		for (const auto &kv : r.byTemplate) templates[kv.first] += kv.second;

		{
			MapObjectRuntime runtime(runtimeAssets);
			runtime.setCreationScripts(&s.options.creationScripts);
			runtime.build(out);
			for (const std::string &e : runtime.report().errors)
			{
				++runtimeErrors[e.substr(e.find(' ') + 1)];
			}
			for (const auto &kv : runtime.report().dataDefects) runtimeDefects[kv.first] += kv.second;
			for (const auto &kv : runtime.report().hideMisses) runtimeHideMisses[kv.first] += kv.second;
			for (const auto &kv : runtime.report().ignoredDrawFields) ignoredFields[kv.first] += kv.second;
			scriptsRun += runtime.report().scriptsRun;
			hooksRun += runtime.report().creationHookObjects;
			CHECK(runtime.report().creationHookObjects == r.creationHookObjects); // every object the loop counted as having an OnCreated handler ran it
			for (const auto &kv : runtime.report().creationHandlers) handlersRun[kv.first] += kv.second;
			{
				// RW 0x628892: one creation draw per full object / bridge / horde member, hook or not
				size_t full = 0;
				for (const MapObjectDrawable &dr : out.drawables)
				{
					full += ((dr.fate == MAPOBJ_OBJECT || dr.fate == MAPOBJ_OBJECT_BRIDGE) && dr.info) ? 1 : 0;
				}
				CHECK(runtime.report().creationDraws == full);
				creationDraws += full;
			}
			for (const std::string &st : runtime.report().stops) luaStops.insert(st.substr(0, st.find(' ')));
			mapsWithS091 += runtime.report().scriptsRun > 0 ? 1 : 0;
		}
		for (const MapSidePlayer &sd : out.sides) ++sideFactions[sd.faction + (sd.hasColor ? " [colour]" : " [no colour]")];
		for (const MapSidePlayer &sd : out.sides) ++sideColors[sd.hasColor ? (std::to_string(sd.colorRGB) + (sd.name.empty() ? " neutral" : "")) : std::string("none")];
		perMapModels.emplace_back(r.drawnModels, key + " (hordes " + std::to_string(r.hordes) + ", members " + std::to_string(r.hordeMembers) + ", objects " + std::to_string(r.byFate[MAPOBJ_OBJECT]) + ")");
		hordes += r.hordes;
		hordeSlots += r.hordeSlots;
		hordePayload += r.hordePayload;
		hordeMembers += r.hordeMembers;
		hordeUnplaced += r.hordeUnplaced;
		hordeRandom += r.hordeRandomOffset;
		drawables += r.drawables;
		models += r.drawnModels;
		moved += r.movedByAnchor;
		aligned += r.alignedToTerrain;
		protoScale += r.withPrototypeScale;
		night += r.nightFlagged;
		snow += r.snowFlagged;
		unowned += r.unownedSides;
	}
	const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

	std::printf("  info: %zu objects in %zu maps (%.1f s); %zu maps have a map.ini/solo.ini; map.ini object definitions: %zu\n", totalObjects, list.size(),
		seconds, mapsWithOverrides, overrideDefs.size());
	for (size_t f = 0; f < MAPOBJ_FATE_COUNT; ++f)
	{
		std::printf("  info: fate %-34s %zu\n", MapObjectFateName((MapObjectFate)f), byFate[f]);
	}
	std::printf("  info: drawables %zu, drawn models %zu, moved by anchor %zu, aligned %zu, prototype scale %zu, night %zu, snow %zu, unowned %zu\n", drawables,
		models, moved, aligned, protoScale, night, snow, unowned);
	std::printf("  info: hordes %zu: slots %zu, payload %zu, members placed %zu, unplaced %zu, hordes with RandomOffset %zu\n", hordes, hordeSlots, hordePayload, hordeMembers, hordeUnplaced, hordeRandom);
	for (const auto &kv : drawClasses) std::printf("  info: drawn %-28s %zu\n", kv.first.c_str(), kv.second);
	for (const auto &kv : notDrawn) std::printf("  info: not drawn: %-60s %zu\n", kv.first.c_str(), kv.second);
	for (const auto &kv : unported) std::printf("  info: unported key %-28s %zu\n", kv.first.c_str(), kv.second);
	for (const auto &kv : ignoredFields) std::printf("  info: ignored draw field %-40s %zu\n", kv.first.c_str(), kv.second);
	for (const auto &kv : hooksByFunction) std::printf("  info: creation hook function %-32s %zu\n", kv.first.c_str(), kv.second);
	for (const auto &kv : hooksByTemplate) std::printf("  info: creation hook template %-80s %zu\n", kv.first.c_str(), kv.second);
	for (const auto &kv : hookHides) std::printf("  info: creation hook hides %-32s %zu\n", kv.first.c_str(), kv.second);
	for (const auto &kv : hookShows) std::printf("  info: creation hook shows %-32s %zu\n", kv.first.c_str(), kv.second);
	std::printf("  info: creation draws (GetGameLogicRandomValue(1, 999) of RW 0x628892) %zu\n", creationDraws);
	std::printf("  info: creation hook objects %zu (ran %zu), scripts run %zu, maps with scripts %zu, maps unscanned %zu\n", hookObjects, hooksRun, scriptsRun, mapsWithS091, mapsWithoutScan);
	for (const auto &kv : ownerProblems) std::printf("  info: owner problem %-60s %zu\n", kv.first.c_str(), kv.second);
	for (const auto &kv : runtimeErrors) std::printf("  info: runtime error x%zu: %s\n", kv.second, kv.first.substr(0, 200).c_str());
	for (const auto &kv : runtimeDefects) std::printf("  info: retail data defect x%zu: %s\n", kv.second, kv.first.substr(0, 230).c_str());
	for (const auto &kv : runtimeHideMisses) std::printf("  info: hide miss x%zu: %s\n", kv.second, kv.first.c_str());

	for (const auto &kv : sideFactions) std::printf("  info: side faction %s x%zu\n", kv.first.c_str(), kv.second);
	for (const auto &kv : sideColors) std::printf("  info: side colour %s x%zu\n", kv.first.c_str(), kv.second);
	std::sort(perMapModels.rbegin(), perMapModels.rend());
	for (size_t i = 0; i < perMapModels.size() && i < 8; ++i) std::printf("  info: largest %zu models: %s\n", perMapModels[i].first, perMapModels[i].second.c_str());
	for (const auto &kv : unresolved) std::printf("  info: UNRESOLVED %-40s %zu\n", kv.first.c_str(), kv.second);
	for (const auto &kv : caseMismatch) std::printf("  info: CASE %-40s %zu\n", kv.first.c_str(), kv.second);
	for (const std::string &e : mapIniErrors) std::printf("  info: map.ini error %s\n", e.c_str());
	for (size_t i = 0; i < errors.size() && i < 20; ++i) std::printf("  info: error %s\n", errors[i].c_str());
	// the draw runtime over every drawable of every map: nothing unexplained. Two kinds of retail data defects are explained exceptions
	// (found by hand in the INI and the archives, pinned exactly): assets an INI names that no mounted archive holds, and hide / show
	// requests that name no sub object of the model (retail's lookup returns NULL for those and nothing happens; ZH doHideShowSubObjs).
	CHECK(runtimeErrors.empty());
	std::set<std::string> defectKeys, missNames;
	for (const auto &kv : runtimeDefects) defectKeys.insert(kv.first);
	for (const auto &kv : runtimeHideMisses) missNames.insert(kv.first);
	CHECK(defectKeys == std::set<std::string>({
		"W3DFloorDraw: model NBMirkBridge_Bib is not available: render object NBMirkBridge_Bib is not registered (no HLOD or mesh of that name in art\\w3d\\nb\\nbmirkbridge_bib.w3d)",
		"W3DScriptedModelDraw: animation GBForbidPool.GBForbidPool of state STATE_Idle: animation GBForbidPool.GBForbidPool (skeleton none) is not registered",
		"W3DScriptedModelDraw: animation GBWallrampart.GBWallrampart of state STATE_None: animation GBWallrampart.GBWallrampart (skeleton none) is not registered",
		"W3DScriptedModelDraw: animation KURogash_IDLC of state STATE_Idle: animation KURogash_IDLC (skeleton kurogash_skl) is not registered",
		"W3DScriptedModelDraw: animation RUElrond_SKL.RUElrond_IDLCT3 of state Bored_Sword: animation RUElrond_SKL.RUElrond_IDLCT3 (skeleton none) is not registered",
		"W3DScriptedModelDraw: model EUPesnt_SKN1 is not available: render object EUPesnt_SKN1 is not registered (no HLOD or mesh of that name in art\\w3d\\eu\\eupesnt_skn1.w3d)" }));
	// the INI's own misses (before the hooks existed) plus every sub object name a creation hook hides that some model lacks (retail's lookup returns
	// NULL for those and nothing happens): every extra name is one scripts.lua names
	for (const char *n : { "arrow", "arrownock", "icewall", "moduletag_drawfloor", "spear", "v1s", "v2s" })
	{
		CHECK(missNames.count(n) == 1);
	}
	{
		std::string luaLower = AsciiStringUtil::lowered(s.options.creationScripts.luaText);
		for (const std::string &n : missNames)
		{
			if (n != "icewall" && n != "moduletag_drawfloor" && n != "v1s" && n != "v2s")
			{
				INFO(n);
				CHECK(luaLower.find(n) != std::string::npos);
			}
		}
		// lane RENDER-3: scripts.lua's ObjectHideSubObjectPermanently(self, "FireArowTip", true) is a PERMANENT request: RW 0x4C3B25 records it by name
		// without looking at the render object (it applies to the model the Fire Arrow upgrade shows later), so a model without the tip is no miss
		CHECK(missNames.count("firearowtip") == 0);
	}
	CHECK(errors.empty());
	CHECK(mapIniErrors.empty());
	CHECK(unresolved.empty());
	CHECK(caseMismatch.empty());
	CHECK(unowned == 0);
	// the corpus facts of this lane's rules (counts of retail data under the rules above; regression pins of this pipeline: no external
	// survey counts them)
	CHECK(totalObjects == 264616);                 // MAP-1 survey: 264,616 objects over the 181 maps (spec 2.7)
	CHECK(byFate[MAPOBJ_SCORCH] == 412);           // spec 2.7: Scorch objects
	CHECK(byFate[MAPOBJ_WAYPOINT] == 5015);        // spec 2.7: *Waypoints/Waypoint
	CHECK(byFate[MAPOBJ_GENERIC_AI] == 330);       // spec 2.7: *GenericAIObjects/GenericAIObject
	CHECK(moved == 190);                           // objects moved by GeometryRotationAnchorOffset (Minas Tirith family)
	CHECK(hordes == 1241);
	CHECK(hordeUnplaced == 0);                     // every InitialPayload member finds a free slot of a matching rank
	// S-110: the initial-state keys of full objects that are not derived (every key the objects carry, with its object count; the
	// 902 `objectInitialHealth != 100` objects start damaged in retail), regression pins of this pipeline
	CHECK(unported == std::map<std::string, size_t>({
		{ "objectBasePhase", 29516 }, { "objectEnabled", 35500 },
		{ "objectEnabled = false", 3 }, { "objectExperienceLevel", 558 },
		{ "objectInitialHealth", 35500 }, { "objectInitialHealth != 100", 902 },
		{ "objectInitialStance", 764 }, { "objectMaxHPs", 142 },
		{ "objectPowered", 35500 }, { "objectPowered = false", 2 },
		{ "objectUpgradesList", 2858 }, { "objectVeterancy", 142 }, }));
	// S-115: static draw fields stored, not applied (placed models; no map object sets ForceToBack, and DistanceFog = No is not on any map object either:
	// the synthetic tests cover those)
	CHECK(ignoredFields == std::map<std::string, size_t>({
		{ "W3DFloorDraw StaticModelLODMode", 1873 }, { "W3DFloorDraw WeatherTexture", 3166 }, }));
	// S-110: the OnCreated Lua handlers of every full object and horde member (scriptevents.xml AILuaEventsList -> OnCreated, scripts.lua): the
	// loop counts them (below); MapObjectRuntime runs them (pinned further down)
	CHECK(mapsWithoutScan == 0);
	CHECK(hookObjects == 17790);
	CHECK(hooksByFunction == std::map<std::string, size_t>({
		{ "OnAragornCreated", 2 }, { "OnCatapultCreated", 45 },
		{ "OnCavalryCreated", 2294 }, { "OnCreateAHeroFunctions", 6 },
		{ "OnDwarvenBattleWagonCreated", 4 }, { "OnDwarvenGuardianCreated", 300 },
		{ "OnElvenWarriorCreated", 1094 }, { "OnEntCreated", 11 },
		{ "OnEvilMenBlackRiderCreated", 13 }, { "OnEvilPorterCreated", 44 },
		{ "OnEvilShipCreated", 19 }, { "OnFortressCreated", 27 },
		{ "OnGarrisonableCreated", 343 }, { "OnGondorArcherCreated", 2262 },
		{ "OnGondorCavalryCreated", 484 }, { "OnGondorFighterCreated", 3281 },
		{ "OnGoodShipCreated", 14 }, { "OnHaradrimArcherCreated", 156 },
		{ "OnInfantryBannerCreated", 70 }, { "OnIsengardFighterCreated", 2866 },
		{ "OnIsengardWildmanCreated", 1099 }, { "OnLegolasCreated", 4 },
		{ "OnMordorArcherCreated", 361 }, { "OnMordorCorsairCreated", 495 },
		{ "OnMordorFighterCreated", 5140 }, { "OnMordorSauronCreated", 1 },
		{ "OnMountainGiantCreated", 12 }, { "OnRohanArcherCreated", 618 },
		{ "OnRohirrimCreated", 292 }, { "OnShipWrightCreated", 55 },
		{ "OnTrebuchetCreated", 25 }, { "OnTrollCreated", 18 },
		{ "OnTrollSlingCreated", 25 }, { "OnWildGoblinArcherCreated", 1224 },
		{ "OnWildSpiderRiderCreated", 132 }, }));
	// the sub objects those handlers hide permanently (lower case, objects); none shows one permanently
	CHECK(hookHides == std::map<std::string, size_t>({
		{ "armor_upgrade", 44 }, { "arrow_upgrade", 44 }, { "arrowfire", 361 },
		{ "banner", 33 }, { "banner_l", 4 }, { "cauldron", 19 },
		{ "cauldron_fire", 19 }, { "cauldron_top", 19 }, { "crowsnest", 19 },
		{ "dbfbanner", 27 }, { "dwarfhearth", 4 }, { "dwarfhearthfire", 4 },
		{ "evilpart_a", 55 }, { "evilpart_b", 55 }, { "firearowtip", 6921 },
		{ "fireplane", 70 }, { "flag", 19 }, { "forged_blade", 12386 },
		{ "forged_blade01", 495 }, { "forged_blades", 5140 }, { "garrison01", 343 },
		{ "garrison02", 343 }, { "glow", 8511 }, { "glow1", 3281 },
		{ "gold", 44 }, { "goodpart_a", 55 }, { "goodpart_b", 55 },
		{ "hammer1", 3581 }, { "plane02", 2 }, { "projectilerock", 45 },
		{ "shard01", 1 }, { "shard02", 1 }, { "shard03", 1 },
		{ "shard04", 1 }, { "shard05", 1 }, { "shard06", 1 },
		{ "shard07", 1 }, { "shard08", 1 }, { "shard09", 1 },
		{ "shard10", 1 }, { "shard11", 1 }, { "shard12", 1 },
		{ "shard13", 1 }, { "shard14", 1 }, { "shard15", 1 },
		{ "shard16", 1 }, { "shard17", 1 }, { "shard18", 1 },
		{ "shard19", 1 }, { "shard20", 1 }, { "shield", 292 },
		{ "sshield", 484 }, { "sword_upgrades", 44 }, { "torch", 1099 },
		{ "trunk01", 18 }, { "ug_armor", 14 }, { "ug_flaming_01", 14 },
		{ "ug_flaming_02", 14 }, { "ug_flaming_fire", 14 }, }));
	CHECK(hookShows.empty());
	// per template: 119 template / list / handler chains over the same 17,790 objects (the archers whose FireArowTip the fire arrow upgrade shows)
	CHECK(hooksByTemplate.size() == 119);
	CHECK(hooksByTemplate["AngmarDarkRanger: RangerFunctions -> OnGondorArcherCreated"] == 680);
	CHECK(hooksByTemplate["ArnorArcher: GondorArcherFunctions -> OnGondorArcherCreated"] == 665);
	{
		size_t sum = 0;
		for (const auto &kv : hooksByTemplate) sum += kv.second;
		CHECK(sum == 17790);
	}
	// S-110 / S-091: the production map path runs the BeginScript bodies on the real drawable Lua state on 95 maps, and every counted object ran
	// its OnCreated handler on the real logic state (retail inheritance: the child's handler replaces the inherited one, so the handlers that
	// ran are the child lists' own)
	CHECK(scriptsRun == 17526);
	CHECK(mapsWithS091 == 95);
	CHECK(hooksRun == 17790);
	CHECK(creationDraws == 51414); // full objects + bridges + horde members over the 181 maps: one logic draw each (RW 0x628892)
	{
		size_t sum = 0;
		for (const auto &kv : handlersRun) sum += kv.second;
		CHECK(sum == 17790);
	}
	for (const auto &kv : handlersRun) std::printf("  info: OnCreated handler run %-32s %zu\n", kv.first.c_str(), kv.second);
	for (const std::string &st : luaStops) std::printf("  info: lua stop %s\n", st.c_str());
	CHECK(luaStops.count("[S-120]") == 1);
	CHECK(luaStops.count("[S-129]") == 1);
	// every model a drawable shows is a registered W3D model (the draw runtime resolves them by the same names)
	ArchiveW3DFileSource source(*s.mount->fs);
	WW3DAssetManager assets(source);
	std::set<std::string> missing;
	for (const std::string &name : allModels)
	{
		std::string e;
		if (!assets.Create_Render_Obj(name, &e))
		{
			missing.insert(name);
			std::printf("  info: MISSING MODEL %s: %s\n", name.c_str(), e.c_str());
		}
	}
	std::printf("  info: %zu distinct models over the corpus, %zu missing\n", allModels.size(), missing.size());
	// Two retail data defects, found in the archives and the INI by hand: Object ... Model = EUPesnt_SKN1 (cinematicobjects.ini) and
	// W3DFloorDraw ModelName = NBMirkBridge_Bib (neutral\mirkbridge.ini) name models that no mounted archive holds (art\w3d\nb has
	// nbmirkbridge.w3d and nbmirkbridge_d3.w3d only; no EUPesnt_SKN1 file exists). Retail's Create_Render_Obj returns NULL for both.
	CHECK(missing == std::set<std::string>({ "eupesnt_skn1", "nbmirkbridge_bib" }));
}


// One map through the whole core pipeline, with the draw runtime: timing and the model registry.
TEST_CASE("mapobj retail: selected maps build their draw runtime and every placed model exists")
{
	if (!haveWorld())
	{
		return;
	}
	SharedWorld &s = shared();
	const char *maps[] = { "maps/map wor fangorn/map wor fangorn.map" };
	ArchiveW3DFileSource source(*s.mount->fs);
	WW3DAssetManager assets(source);
	for (const char *key : maps)
	{
		INFO("map " << key);
		std::vector<std::uint8_t> bytes;
		std::string err;
		const std::string path = slashes(key, '\\');
		REQUIRE_MESSAGE(s.mount->fs->readFile(path, bytes, &err), err);
		LoadedMap m;
		MapReadOptions opt;
		REQUIRE_MESSAGE(MapReader::load(bytes, key, opt, m, &err), err);
		TerrainLogic terrain;
		terrain.init(m.heightMap, m.chunks, nullptr);
		s.world->applyMapIni(slashes(dirOf(path), '\\'));
		MapObjectDrawables out;
		MapObjectCreation::build(m, key, s.world->things(), terrain, s.options, out);
		MapObjectRuntime runtime(assets);
		runtime.setCreationScripts(&s.options.creationScripts);
		runtime.build(out);
		const MapRuntimeReport &rr = runtime.report();
		std::printf("  info: %s: %zu drawables -> %zu placed models (%zu model draws: %zu animated, %zu static; trees %zu props %zu floors %zu), %zu distinct models, runtime built in %.2f s\n",
			key, out.drawables.size(), rr.placed, rr.modelDraws, rr.animated, rr.staticModels, rr.treeDraws, rr.propDraws, rr.floorDraws, rr.distinctModels.size(),
			rr.buildSeconds);
		for (size_t i = 0; i < rr.errors.size() && i < 20; ++i) std::printf("  info: runtime error %s\n", rr.errors[i].c_str());
		for (const std::string &st : rr.stops) std::printf("  info: runtime stop %s\n", st.substr(0, 160).c_str());
		CHECK(rr.errors.empty());
		size_t missing = 0;
		for (const auto &kv : rr.distinctModels)
		{
			std::string e;
			if (!assets.Create_Render_Obj(kv.first, &e))
			{
				++missing;
				std::printf("  info: MISSING MODEL %s: %s\n", kv.first.c_str(), e.c_str());
			}
		}
		CHECK(missing == 0);
	}
}
