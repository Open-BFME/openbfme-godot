// OpenBFME. GPL-3.0.
//
// Device layer: the pathfinder debug view (lane PATH-1). Builds the pathfind grid of one retail map through the core
// (GameLogic/AI/AIPathfind.h over GameLogic/Map/TerrainPathfindSource.h, the structures of the object loop as footprints) and hands
// Godot what the map viewer draws: a coloured overlay of the classified cells and the polyline of a computed path. All decisions are
// made in the core; this class only converts SAGE space to Godot's frame (G = (x, z, -y), spec maps-and-terrain.md 2.1).

#pragma once

#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/vector2.hpp>

#include <memory>
#include <string>

class RetailObjectWorld;
struct LoadedMap;
class Pathfinder;
class TerrainLogic;
class TerrainPathfindSource;
struct MapPathfindObjectSet;
struct PathfindWorldStub;

namespace godot
{

class RetailFileSystem;

class PathfindView : public RefCounted
{
	GDCLASS(PathfindView, RefCounted)

public:
	PathfindView();
	~PathfindView() override;

	// Loads the retail object templates (a few seconds); called once, build() calls it when it has not run.
	Dictionary setup(const Ref<RetailFileSystem> &fs);

	// Builds the grid of `map_name` ("map mp evendim"). options: objects (bool, default true: the structures become footprints).
	// Returns { ok, errors, stops, map, width, height, cells: {clear, water, ...}, structures, fences, timings_ms }.
	Dictionary build(const Ref<RetailFileSystem> &fs, const String &map_name, const Dictionary &options);

	// The coloured cells as a triangle list in Godot space: { vertices: PackedVector3Array, colors: PackedColorArray,
	// count: cells drawn }. options: lift (float, default 1.0: height above the ground), pinched (bool, default false: show the
	// pinched flag of clear cells), planes (bool, default false: show the two map planes).
	Dictionary get_overlay(const Dictionary &options);

	// A path between two SAGE (x, y) positions for a ground unit of the given footprint radius (cells). Returns { found,
	// optimized: PackedVector3Array, cells: PackedVector3Array (the unoptimised node chain), length, seconds, zones, stops }.
	Dictionary find_path(const Vector2 &from, const Vector2 &to, const Dictionary &options);
	// two far apart clear cells the zones connect whose search examines the most cells (the viewer's --pf-auto); {from, to} in world units
	Dictionary pick_endpoints(const Dictionary &options);

	// the SAGE bounds of the grid: { min: Vector2, max: Vector2 }
	Dictionary get_bounds() const;

protected:
	static void _bind_methods();

private:
	struct Impl;
	std::unique_ptr<Impl> m_impl;
};

} // namespace godot
