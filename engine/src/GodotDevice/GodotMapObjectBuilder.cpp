// OpenBFME. GPL-3.0.
// See GodotDevice/GodotMapObjectBuilder.h.

#include "GodotDevice/GodotMapObjectBuilder.h"

#include "Common/ArchiveFileSystem.h"
#include "Common/AsciiString.h"
#include "GameClient/MapClassification.h"
#include "GameClient/MapObjectDrawables.h"
#include "GameClient/MapObjectRuntime.h"
#include "GameClient/MapUtil.h"
#include "GameLogic/Map/TerrainLogic.h"
#include "GameLogic/Object/RetailObjectWorld.h"
#include "GodotDevice/GodotRetailFileSystem.h"
#include "GodotDevice/GodotW3DInstancer.h"
#include "GodotDevice/GodotW3DMaterial.h"
#include "GameEngineDevice/W3DDevice/GameClient/W3DObjectLighting.h"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/basis.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/transform3d.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <set>

namespace godot
{

namespace
{
String toGodot(const std::string &s)
{
	return String::utf8(s.c_str(), (int64_t)s.size());
}

std::string toNative(const String &s)
{
	CharString utf8 = s.utf8();
	return std::string(utf8.get_data(), (size_t)utf8.length());
}

double nowMs()
{
	using namespace std::chrono;
	return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

bool optBool(const Dictionary &o, const char *key, bool def)
{
	return o.has(key) ? (bool)o[key] : def;
}

int64_t optInt(const Dictionary &o, const char *key, int64_t def)
{
	return o.has(key) ? (int64_t)o[key] : def;
}

// SAGE (x east, y north, z up) -> Godot (x, z, -y): the basis of the drawable (columns X, Y, Z of the SAGE orientation, stored row major)
// conjugated by the axis permutation, times the instance scale; the origin through the same map.
Transform3D drawableTransform(const MapObjectDrawable &d)
{
	// P rows: g.x = s.x, g.y = s.z, g.z = -s.y
	static const float P[3][3] = { { 1, 0, 0 }, { 0, 0, 1 }, { 0, -1, 0 } };
	float b[3][3];
	for (int i = 0; i < 3; ++i)
	{
		for (int j = 0; j < 3; ++j)
		{
			float v = 0.0f;
			for (int a = 0; a < 3; ++a)
			{
				for (int c = 0; c < 3; ++c)
				{
					v += P[i][a] * d.basis[a * 3 + c] * P[j][c];
				}
			}
			b[i][j] = v * d.scale;
		}
	}
	Basis basis(Vector3(b[0][0], b[1][0], b[2][0]), Vector3(b[0][1], b[1][1], b[2][1]), Vector3(b[0][2], b[1][2], b[2][2])); // columns
	return Transform3D(basis, Vector3(d.position.x, d.position.z, -d.position.y));
}

PackedStringArray toPacked(const std::vector<std::string> &v)
{
	PackedStringArray a;
	for (const std::string &s : v)
	{
		a.push_back(toGodot(s));
	}
	return a;
}

Dictionary countsToDict(const std::map<std::string, size_t> &m, size_t limit = 0)
{
	std::vector<std::pair<std::string, size_t>> v(m.begin(), m.end());
	if (limit)
	{
		std::stable_sort(v.begin(), v.end(), [](const auto &a, const auto &b) { return a.second > b.second; });
		if (v.size() > limit)
		{
			v.resize(limit);
		}
	}
	Dictionary d;
	for (const auto &kv : v)
	{
		d[toGodot(kv.first)] = (int64_t)kv.second;
	}
	return d;
}

// Hands the exact pose of a draw frame to the instancer (clip pair, frames, blend).
bool applyFrame(W3DInstancer *inst, int64_t instance, const W3DDrawFrame &f)
{
	String clip0, clip1;
	if (f.trackCount > 0)
	{
		clip0 = toGodot(f.tracks[0].clipName);
	}
	if (f.blending && f.trackCount > 1)
	{
		clip1 = toGodot(f.tracks[1].clipName);
	}
	return inst->set_instance_pose(instance, clip0, f.frame0, clip1, f.frame1, f.blendPercentage);
}
} // namespace

void MapObjectBuilder::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("setup", "fs"), &MapObjectBuilder::setup);
	ClassDB::bind_method(D_METHOD("build_objects", "fs", "map_name", "options"), &MapObjectBuilder::build_objects, DEFVAL(Dictionary()));
	ClassDB::bind_method(D_METHOD("advance", "delta"), &MapObjectBuilder::advance);
	ClassDB::bind_method(D_METHOD("get_report"), &MapObjectBuilder::get_report);
	ClassDB::bind_method(D_METHOD("get_stats"), &MapObjectBuilder::get_stats);
}

MapObjectBuilder::MapObjectBuilder() = default;
MapObjectBuilder::~MapObjectBuilder() = default;

Dictionary MapObjectBuilder::setup(const Ref<RetailFileSystem> &fs)
{
	Dictionary result;
	Array errors;
	if (fs.is_null() || !fs->is_mounted() || fs->archive_fs() == nullptr)
	{
		errors.push_back("retail file system is not mounted");
	}
	else if (!m_world || m_fs != fs)
	{
		m_fs = fs;
		m_runtime.reset();
		m_drawables.reset();
		m_map.reset();
		m_world.reset(new RetailObjectWorld(*fs->archive_fs()));
		m_options.reset(new MapObjectOptions());
		std::string error;
		const double t0 = nowMs();
		if (!m_world->load(&error))
		{
			errors.push_back(toGodot(error));
			m_world.reset();
		}
		else if (!MapObjectGameData::load(*fs->archive_fs(), *m_options, &error) || !MapObjectGameData::loadPlayerTemplates(*fs->archive_fs(), *m_options, &error)
			|| !MapCreationHooks::load(*fs->archive_fs(), m_options->creationScripts, &error))
		{
			errors.push_back(toGodot(error));
			m_world.reset();
		}
		else
		{
			for (const SubsystemLoadReport::FileError &e : m_world->report().errors)
			{
				errors.push_back(toGodot(e.file + ": " + e.message));
			}
			result["seconds"] = (nowMs() - t0) / 1000.0;
			result["templates"] = (int64_t)m_world->things().templateCount();
			m_source.reset(new ArchiveW3DFileSource(*fs->archive_fs()));
			m_assets.reset(new WW3DAssetManager(*m_source));
		}
	}
	result["ok"] = errors.is_empty();
	result["errors"] = errors;
	return result;
}

Node3D *MapObjectBuilder::build_objects(const Ref<RetailFileSystem> &fsRef, const String &map_name, const Dictionary &options)
{
	m_report = Dictionary();
	Array errors, stops;
	Dictionary timings;
	m_report["errors"] = errors;
	m_report["stops"] = stops;
	m_report["timings_ms"] = timings;
	m_report["map"] = map_name;
	auto fail = [&](const std::string &message) -> Node3D * {
		errors.push_back(toGodot(message));
		UtilityFunctions::push_error("MapObjectBuilder: ", toGodot(message));
		return nullptr;
	};
	if (fsRef.is_null() || !fsRef->is_mounted())
	{
		return fail("retail file system is not mounted");
	}
	if (!m_world || m_fs != fsRef)
	{
		const Dictionary s = setup(fsRef);
		if (!(bool)s["ok"])
		{
			const Array e = s["errors"];
			for (int64_t i = 0; i < e.size(); ++i)
			{
				errors.push_back(e[i]);
			}
			return nullptr;
		}
		timings["object_world_load"] = (double)s["seconds"] * 1000.0;
	}
	m_animationsOn = optBool(options, "animations", true);
	m_textureAnimationOn = optBool(options, "texture_animation", true);
	m_textureClock = 0.0;
	const int64_t maxObjects = optInt(options, "max_objects", 0);

	// drop the previous map (its drawables point into the template factory the map.ini step resets)
	m_animated.clear();
	m_runtime.reset();
	m_drawables.reset();
	m_map.reset();

	RetailFileSystem &rfs = *fsRef.ptr();
	std::string lowerName = AsciiStringUtil::lowered(toNative(map_name));
	const std::string path = MapClassification::mapPath(lowerName);
	std::vector<uint8_t> bytes;
	std::string error;
	if (!rfs.readBytes(path, bytes, &error))
	{
		return fail(error);
	}
	// the shared pipeline: map -> terrain -> map.ini -> classification (GameClient/MapClassification, the live game's too)
	m_map.reset(new LoadedMap());
	m_drawables.reset(new MapObjectDrawables());
	TerrainLogic terrain;
	MapClassification::Result cr;
	if (!MapClassification::run(bytes, path, lowerName, *m_world, *m_options, *m_map, terrain, *m_drawables, nullptr, cr, &error))
	{
		return fail(error);
	}
	timings["load_map"] = cr.secondsLoad * 1000.0;
	const RetailObjectWorld::MapIniResult &mi = cr.mapIni;
	for (const std::string &e : mi.errors)
	{
		errors.push_back(toGodot(e));
	}
	{
		Dictionary d;
		d["map_ini"] = mi.mapIniFound;
		d["solo_ini"] = mi.soloIniFound;
		d["definitions"] = toPacked(mi.definitions);
		d["other_blocks_not_applied"] = (int64_t)mi.blocksRecorded;
		m_report["map_ini"] = d;
	}
	timings["map_ini"] = cr.secondsMapIni * 1000.0;
	timings["object_loop"] = cr.secondsBuild * 1000.0;
	double t0 = nowMs();
	const MapObjectReport &orep = m_drawables->report;
	for (const std::string &e : orep.errors)
	{
		errors.push_back(toGodot(e));
	}
	for (const std::string &s : orep.stops)
	{
		stops.push_back(toGodot(s));
	}

	t0 = nowMs();
	m_runtime.reset(new MapObjectRuntime(*m_assets));
	m_runtime->setCreationScripts(&m_options->creationScripts);
	m_runtime->build(*m_drawables);
	timings["draw_runtime"] = nowMs() - t0;
	const MapRuntimeReport &rrep = m_runtime->report();
	for (const std::string &e : rrep.errors)
	{
		errors.push_back(toGodot(e));
	}
	for (const std::string &s : rrep.stops)
	{
		stops.push_back(toGodot(s));
	}

	// ---- the scene -----------------------------------------------------------------------------------------------------
	Node3D *root = memnew(Node3D);
	root->set_name("MapObjects");
	W3DInstancer *staticInst = memnew(W3DInstancer);
	staticInst->set_name("StaticObjects");
	W3DInstancer *dynamicInst = memnew(W3DInstancer);
	dynamicInst->set_name("AnimatedObjects");
	root->add_child(staticInst);
	root->add_child(dynamicInst);
	m_staticId = staticInst->get_instance_id();
	m_dynamicId = dynamicInst->get_instance_id();
	for (W3DInstancer *inst : { staticInst, dynamicInst })
	{
		const Dictionary s = inst->setup(fsRef);
		if (!(bool)s["ok"])
		{
			const Array e = s["errors"];
			for (int64_t i = 0; i < e.size(); ++i)
			{
				errors.push_back(e[i]);
			}
		}
	}
	const bool houseColors = optBool(options, "house_colors", true);
	staticInst->set_house_colors_enabled(houseColors);
	dynamicInst->set_house_colors_enabled(houseColors);
	staticInst->set_auto_update(false);
	dynamicInst->set_playing(false); // time only enters through set_instance_pose

	t0 = nowMs();
	std::map<std::string, int64_t> staticModels, dynamicModels;
	size_t tinted = 0, skippedMissing = 0, instancesStatic = 0, instancesDynamic = 0, missingModels = 0, poseFailures = 0, hiddenSets = 0, drawablesDone = 0, skippedHiddenModule = 0;
	std::set<size_t> drawablesSeen;
	const int infantryLightBit = MapObjectCreation::kindOfIndex("INFANTRY");
	for (MapPlacedModel &p : m_runtime->models())
	{
		if (maxObjects > 0 && drawablesSeen.size() >= (size_t)maxObjects && !drawablesSeen.count(p.drawable))
		{
			continue;
		}
		if (p.missingModel)
		{
			++skippedMissing;
			continue;
		}
		if (p.moduleHidden)
		{
			++skippedHiddenModule; // a script hid this draw module (Drawable::showModule, RW 0x6789B4)
			continue;
		}
		drawablesSeen.insert(p.drawable);
		const MapObjectDrawable &d = m_drawables->drawables[p.drawable];
		const Transform3D xf = drawableTransform(d);
		const bool animatedPlay = p.animated && m_animationsOn && p.draw;
		W3DInstancer *target = animatedPlay ? dynamicInst : staticInst;
		std::map<std::string, int64_t> &cache = animatedPlay ? dynamicModels : staticModels;
		W3DDrawFrame frame;
		std::string modelName = p.modelName;
		if (p.draw)
		{
			frame = p.draw->frame();
			modelName = frame.modelName;
		}
		const std::string key = AsciiStringUtil::lowered(modelName);
		auto it = cache.find(key);
		int64_t model;
		if (it == cache.end())
		{
			model = target->add_model(toGodot(modelName));
			cache[key] = model;
		}
		else
		{
			model = it->second;
		}
		if (model < 0)
		{
			++missingModels;
			continue;
		}
		const int64_t instance = target->add_instance(model, xf, String(), 0.0, 1.0);
		if (instance < 0)
		{
			++missingModels;
			continue;
		}
		(animatedPlay ? instancesDynamic : instancesStatic) += 1;
		if (infantryLightBit >= 0 && d.info && d.info->kindOf_test(infantryLightBit))
		{
			target->set_instance_infantry_light(instance, true); // RENDER-1: the infantry light set (S-390)
		}
		if (houseColors && d.sideIndex >= 0 && m_drawables->sides[(size_t)d.sideIndex].hasColor)
		{
			const std::uint32_t c = m_drawables->sides[(size_t)d.sideIndex].colorRGB;
			target->set_instance_house_color(instance, Color(((c >> 16) & 255) / 255.0f, ((c >> 8) & 255) / 255.0f, (c & 255) / 255.0f, 1.0f));
			++tinted;
		}
		if (p.draw)
		{
			if (!applyFrame(target, instance, frame))
			{
				++poseFailures;
			}
			std::vector<std::string> hidden = frame.hiddenSubObjects;
			if (!hidden.empty())
			{
				target->set_instance_hidden_subobjects(instance, toPacked(hidden));
				++hiddenSets;
			}
			if (animatedPlay)
			{
				Animated a;
				a.draw = p.draw.get();
				a.instance = instance;
				a.hidden = std::move(hidden);
				m_animated.push_back(std::move(a));
			}
		}
		++drawablesDone;
	}
	staticInst->update_now();
	if (m_animated.empty())
	{
		dynamicInst->set_auto_update(false);
	}
	else
	{
		dynamicInst->update_now();
	}
	timings["instancing"] = nowMs() - t0;
	timings["total"] = 0.0;
	for (const Variant &k : timings.keys())
	{
		timings["total"] = (double)timings["total"] + (double)timings[k];
	}

	// ---- the report ------------------------------------------------------------------------------------------------------
	{
		// camera focus points for the viewer's presets: the first horde member, the first structure and the first non-horde unit
		const int structureBit = MapObjectCreation::kindOfIndex("STRUCTURE");
		const int infantryBit = MapObjectCreation::kindOfIndex("INFANTRY");
		Dictionary focus;
		bool haveHorde = false, haveStructure = false, haveUnit = false;
		for (const MapObjectDrawable &d : m_drawables->drawables)
		{
			const bool drawsModel = !d.draws.empty() && std::any_of(d.draws.begin(), d.draws.end(), [](const MapDrawModule &m) { return !m.model.empty(); });
			if (!drawsModel || d.fate != MAPOBJ_OBJECT)
			{
				continue;
			}
			const Vector3 g(d.position.x, d.position.z, -d.position.y);
			if (!focus.has("tinted") && d.sideIndex >= 0 && m_drawables->sides[(size_t)d.sideIndex].colorSource == MapSidePlayer::COLOR_MAP &&
				(d.hordeMember || d.info->kindOf_test(infantryBit)))
			{
				focus["tinted"] = g; // a unit of a side whose colour the map sets
			}
			if (d.hordeMember && !haveHorde)
			{
				focus["horde"] = g;
				haveHorde = true;
			}
			else if (!d.hordeMember && d.info->kindOf_test(structureBit) && !haveStructure &&
				(d.sideIndex >= 0 && m_drawables->sides[(size_t)d.sideIndex].hasColor)) // a player's building (not a rock or a village house)
			{
				focus["structure"] = g;
				haveStructure = true;
			}
			else if (!d.hordeMember && d.info->kindOf_test(infantryBit) && !haveUnit)
			{
				focus["unit"] = g;
				haveUnit = true;
			}
		}
		m_report["focus"] = focus;
	}
	if (m_map->chunks.hasGlobalLighting)
	{
		// the lighting of OBJECTS at the map's time of day (GlobalLighting objects[0]: ZH TerrainObjectsLighting, the main light); the
		// viewer turns it into a DirectionalLight3D and the ambient colour. SAGE light direction (the way the light travels) -> Godot.
		const GlobalLightingData &gl = m_map->chunks.lighting;
		const TimeOfDayLights &tod = gl.tod[std::max(0, std::min(3, gl.timeOfDay - 1))];
		const GlobalLight &L = tod.objects[0];
		Dictionary lit;
		lit["time_of_day"] = gl.timeOfDay;
		lit["ambient"] = Color(L.ambient[0], L.ambient[1], L.ambient[2]);
		lit["diffuse"] = Color(L.diffuse[0], L.diffuse[1], L.diffuse[2]);
		lit["direction"] = Vector3(L.lightPos[0], L.lightPos[2], -L.lightPos[1]);
		Array accents;
		for (int i = 1; i < 3; ++i)
		{
			Dictionary a;
			a["ambient"] = Color(tod.objects[i].ambient[0], tod.objects[i].ambient[1], tod.objects[i].ambient[2]);
			a["diffuse"] = Color(tod.objects[i].diffuse[0], tod.objects[i].diffuse[1], tod.objects[i].diffuse[2]);
			a["direction"] = Vector3(tod.objects[i].lightPos[0], tod.objects[i].lightPos[2], -tod.objects[i].lightPos[1]);
			accents.push_back(a);
		}
		lit["accent_lights"] = accents;
		// RENDER-1: the W3D shaders light every model from the light environment of the retail effects (W3DObjectLighting)
		W3DObjectLighting env;
		std::string lightError;
		if (W3DObjectLightingUtil::fromMap(gl, env, &lightError))
		{
			W3D_Apply_Object_Lighting(env);
			lit["effect_color_scale"] = env.colorScale;
		}
		else
		{
			lit["error"] = toGodot(lightError);
		}
		m_report["lighting"] = lit;
	}
	{
		Dictionary od;
		od["total"] = (int64_t)orep.objects;
		Dictionary fates;
		for (size_t f = 0; f < MAPOBJ_FATE_COUNT; ++f)
		{
			fates[MapObjectFateName((MapObjectFate)f)] = (int64_t)orep.byFate[f];
		}
		od["by_fate"] = fates;
		od["by_template_top"] = countsToDict(orep.byTemplate, 30);
		od["templates"] = (int64_t)orep.byTemplate.size();
		od["by_draw_class"] = countsToDict(orep.byDrawClass);
		od["not_drawn"] = countsToDict(orep.notDrawnByReason);
		od["unresolved"] = countsToDict(orep.unresolved);
		od["case_mismatch"] = countsToDict(orep.caseMismatch);
		od["unported_keys"] = countsToDict(orep.unportedKeys);
		od["creation_hooks_scanned"] = orep.creationHooksScanned;
		od["creation_hook_objects"] = (int64_t)orep.creationHookObjects;
		od["creation_hooks_by_function"] = countsToDict(orep.creationHooksByFunction);
		od["creation_hooks_by_template"] = countsToDict(orep.creationHooksByTemplate);
		od["creation_hook_hides"] = countsToDict(orep.creationHookHides);
		od["creation_hook_shows"] = countsToDict(orep.creationHookShows);
		od["drawables"] = (int64_t)orep.drawables;
		od["drawn_models"] = (int64_t)orep.drawnModels;
		od["moved_by_anchor"] = (int64_t)orep.movedByAnchor;
		od["aligned_to_terrain"] = (int64_t)orep.alignedToTerrain;
		od["with_prototype_scale"] = (int64_t)orep.withPrototypeScale;
		od["hordes"] = (int64_t)orep.hordes;
		od["horde_slots"] = (int64_t)orep.hordeSlots;
		od["horde_payload"] = (int64_t)orep.hordePayload;
		od["horde_members"] = (int64_t)orep.hordeMembers;
		od["horde_unplaced"] = (int64_t)orep.hordeUnplaced;
		{
			Dictionary owners; // owner side -> { drawables, colour (0xRRGGBB or -1), source }
			std::map<int, int64_t> perSide;
			for (const MapObjectDrawable &d : m_drawables->drawables)
			{
				if (d.fate == MAPOBJ_OBJECT || d.fate == MAPOBJ_OBJECT_BRIDGE)
				{
					++perSide[d.sideIndex];
				}
			}
			for (const auto &kv : perSide)
			{
				Dictionary e;
				e["drawables"] = kv.second;
				if (kv.first >= 0)
				{
					const MapSidePlayer &sp = m_drawables->sides[(size_t)kv.first];
					e["color"] = sp.hasColor ? (int64_t)sp.colorRGB : (int64_t)-1;
					e["source"] = sp.colorSource == MapSidePlayer::COLOR_MAP ? "map" : sp.colorSource == MapSidePlayer::COLOR_FACTION ? "faction" : "none";
					e["faction"] = toGodot(sp.faction);
					owners[toGodot(sp.name.empty() ? std::string("<neutral>") : sp.name)] = e;
				}
				else
				{
					owners["<unowned>"] = e;
				}
			}
			od["owners"] = owners;
		}
		od["sides_with_map_color"] = (int64_t)orep.sidesWithMapColor;
		od["sides_with_faction_color"] = (int64_t)orep.sidesWithFactionColor;
		od["sides_without_color"] = (int64_t)orep.sidesWithoutColor;
		od["night_flagged"] = (int64_t)orep.nightFlagged;
		od["snow_flagged"] = (int64_t)orep.snowFlagged;
		m_report["objects"] = od;
		Dictionary rd;
		rd["placed_models"] = (int64_t)rrep.placed;
		rd["model_draws"] = (int64_t)rrep.modelDraws;
		rd["animated"] = (int64_t)rrep.animated;
		rd["static_models"] = (int64_t)rrep.staticModels;
		rd["tree_draws"] = (int64_t)rrep.treeDraws;
		rd["prop_draws"] = (int64_t)rrep.propDraws;
		rd["floor_draws"] = (int64_t)rrep.floorDraws;
		rd["distinct_models"] = (int64_t)rrep.distinctModels.size();
		rd["build_seconds"] = rrep.buildSeconds;
		rd["data_defects"] = countsToDict(rrep.dataDefects);
		rd["hide_misses"] = countsToDict(rrep.hideMisses);
		rd["ignored_draw_fields"] = countsToDict(rrep.ignoredDrawFields);
		rd["scripts_run"] = (int64_t)rrep.scriptsRun;
		rd["creation_hook_objects"] = (int64_t)rrep.creationHookObjects;
		rd["creation_handlers"] = countsToDict(rrep.creationHandlers);
		rd["creation_list_objects"] = (int64_t)rrep.creationListObjects;
		rd["creation_draws"] = (int64_t)rrep.creationDraws;
		rd["module_requests"] = (int64_t)rrep.moduleRequests;
		rd["modules_hidden"] = (int64_t)rrep.modulesHidden;
		m_report["runtime"] = rd;
		Dictionary id;
		id["static_instances"] = (int64_t)instancesStatic;
		id["animated_instances"] = (int64_t)instancesDynamic;
		id["static_models"] = (int64_t)staticModels.size();
		id["animated_models"] = (int64_t)dynamicModels.size();
		id["missing_models"] = (int64_t)missingModels;
		id["house_colored_instances"] = (int64_t)tinted;
		id["skipped_retail_data_defects"] = (int64_t)skippedMissing;
		id["skipped_hidden_modules"] = (int64_t)skippedHiddenModule;
		id["pose_failures"] = (int64_t)poseFailures;
		id["instances_with_hidden_sub_objects"] = (int64_t)hiddenSets;
		id["drawables_placed"] = (int64_t)drawablesDone;
		m_report["instancing"] = id;
	}
	{
		PackedStringArray warnings;
		for (W3DInstancer *inst : { staticInst, dynamicInst })
		{
			const PackedStringArray w = inst->get_warnings();
			for (int64_t i = 0; i < w.size(); ++i)
			{
				if (!warnings.has(w[i]))
				{
					warnings.push_back(w[i]);
				}
			}
			const PackedStringArray e = inst->get_errors();
			for (int64_t i = 0; i < e.size(); ++i)
			{
				errors.push_back(e[i]);
			}
		}
		m_report["instancer_warnings"] = warnings;
	}
	return root;
}

void MapObjectBuilder::advance(double delta)
{
	const double t0 = nowMs();
	// Material clocks (UV scrolling, rotation, fades of the time-variant mappers) run on both instancers whatever the draw modules do:
	// the static instancer batches poses once (no per-frame update), the animated one only updates when a pose changes, and neither
	// would ever advance a mapper otherwise. Only the mapper matrices are recomputed here, never the poses or the buffers.
	if (m_textureAnimationOn)
	{
		m_textureClock += delta;
		for (const uint64_t id : { m_staticId, m_dynamicId })
		{
			if (id == 0 || !UtilityFunctions::is_instance_id_valid(id))
			{
				continue;
			}
			if (W3DInstancer *ti = Object::cast_to<W3DInstancer>(UtilityFunctions::instance_from_id(id)))
			{
				ti->set_global_time(m_textureClock);
				if (!ti->get_auto_update())
				{
					ti->update_mappers(); // an instancer that updates itself every frame applies the clock in its own update_now
				}
			}
		}
	}
	if (m_animated.empty() || !m_animationsOn)
	{
		m_lastAdvanceMs = nowMs() - t0;
		return;
	}
	if (!UtilityFunctions::is_instance_id_valid(m_dynamicId))
	{
		m_animated.clear();
		return;
	}
	W3DInstancer *inst = Object::cast_to<W3DInstancer>(UtilityFunctions::instance_from_id(m_dynamicId));
	if (!inst)
	{
		m_animated.clear();
		return;
	}
	const double ms = delta * 1000.0;
	for (Animated &a : m_animated)
	{
		W3DScriptedModelDraw *draw = static_cast<W3DScriptedModelDraw *>(a.draw);
		draw->advance(ms);
		const W3DDrawFrame f = draw->frame();
		applyFrame(inst, a.instance, f);
		if (f.hiddenSubObjects != a.hidden)
		{
			inst->set_instance_hidden_subobjects(a.instance, toPacked(f.hiddenSubObjects));
			a.hidden = f.hiddenSubObjects;
		}
		++m_poseUpdates;
	}
	m_lastAdvanceMs = nowMs() - t0;
}

Dictionary MapObjectBuilder::get_stats() const
{
	Dictionary d;
	d["animated_drawables"] = (int64_t)m_animated.size();
	d["last_advance_ms"] = m_lastAdvanceMs;
	d["pose_updates"] = (int64_t)m_poseUpdates;
	d["texture_clock"] = m_textureClock;
	if (UtilityFunctions::is_instance_id_valid(m_staticId))
	{
		if (W3DInstancer *s = Object::cast_to<W3DInstancer>(UtilityFunctions::instance_from_id(m_staticId)))
		{
			d["static"] = s->get_stats();
		}
	}
	if (UtilityFunctions::is_instance_id_valid(m_dynamicId))
	{
		if (W3DInstancer *s = Object::cast_to<W3DInstancer>(UtilityFunctions::instance_from_id(m_dynamicId)))
		{
			d["animated"] = s->get_stats();
		}
	}
	return d;
}

} // namespace godot
