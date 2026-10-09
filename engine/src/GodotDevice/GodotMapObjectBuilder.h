// OpenBFME. GPL-3.0.
//
// Device layer: draws the objects of one retail map on its terrain (lane MAPOBJ-1). All decisions (which objects get a drawable, where,
// how big, which models and idle animations) are made in the core (GameClient/MapObjectDrawables.h, MapObjectRuntime.h); this class
// only converts SAGE space to Godot's frame (G = (x, z, -y), spec maps-and-terrain.md 2.1) and feeds W3DInstancer nodes:
//
//   * one STATIC W3DInstancer for everything whose pose never changes (trees, shrubs, props, floors, buildings and units whose idle
//     state shows no moving animation): updated once after the build, then its per-frame update is switched off, so the cost of a
//     200,000 instance map is the GPU's alone;
//   * one DYNAMIC W3DInstancer for the drawables whose idle animation moves: each render frame the draw module runtime
//     (W3DScriptedModelDraw::advance) steps the animation and the exact pose is handed to set_instance_pose.
//
// Instancing stays batched (one MultiMesh per model sub object and instancer). Retail house colours are NOT applied (stop S-022).

#pragma once

#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

#include <map>
#include <memory>
#include <string>
#include <vector>

class ArchiveW3DFileSource;
class WW3DAssetManager;
class RetailObjectWorld;
struct LoadedMap;
struct MapObjectDrawables;
class MapObjectRuntime;
struct MapObjectOptions;

namespace godot
{

class RetailFileSystem;
class W3DInstancer;

class MapObjectBuilder : public RefCounted
{
	GDCLASS(MapObjectBuilder, RefCounted)

public:
	MapObjectBuilder();
	~MapObjectBuilder() override;

	// Loads the retail object templates (the subsystem INI load with the real object parsers; a few seconds). { ok, errors, seconds,
	// templates }. Called once per builder; build_objects calls it when it has not run.
	Dictionary setup(const Ref<RetailFileSystem> &fs);

	// Builds the object layer of `map_name` ("map wor fangorn"). Returns a Node3D (add it under the terrain root), or null on failure
	// (see get_report()["errors"]). options (all optional):
	//   animations: bool (default true)   idle animations play; false poses every model once and freezes it
	//   texture_animation: bool (default true) the material clocks (UV scroll ...) run in advance(); independent of `animations`
	//   horde_members: bool (default true) hordes show their members
	//   max_objects: int (default 0 = all) stop after this many drawables (debugging)
	Node3D *build_objects(const Ref<RetailFileSystem> &fs, const String &map_name, const Dictionary &options);

	// Steps the animations of the dynamic drawables and the material clocks of both instancers by `delta` seconds of render time (call once
	// per rendered frame).
	void advance(double delta);

	// { map, errors, stops, timings_ms, objects: { total, by_fate, drawables, ... }, models, instances, ... }
	Dictionary get_report() const { return m_report; }
	// Per-frame statistics of the instancers (instances, draw items, pose ms, ...) and the animated drawable count.
	Dictionary get_stats() const;

protected:
	static void _bind_methods();

private:
	struct Animated
	{
		void *draw = nullptr;        ///< W3DScriptedModelDraw*, owned by m_runtime
		int64_t instance = -1;
		std::vector<std::string> hidden; ///< the hidden sub object list last handed to the instancer
	};

	Dictionary m_report;
	Ref<RetailFileSystem> m_fs;
	std::unique_ptr<RetailObjectWorld> m_world;
	std::unique_ptr<MapObjectOptions> m_options;
	std::unique_ptr<ArchiveW3DFileSource> m_source;
	std::unique_ptr<WW3DAssetManager> m_assets;
	std::unique_ptr<LoadedMap> m_map;
	std::unique_ptr<MapObjectDrawables> m_drawables;
	std::unique_ptr<MapObjectRuntime> m_runtime;
	uint64_t m_staticId = 0, m_dynamicId = 0; ///< ObjectIDs of the two instancer nodes
	std::vector<Animated> m_animated;
	bool m_animationsOn = true;
	bool m_textureAnimationOn = true;
	double m_textureClock = 0.0;   ///< seconds of material clock advanced by advance()
	double m_lastAdvanceMs = 0.0;
	size_t m_poseUpdates = 0;
};

} // namespace godot
