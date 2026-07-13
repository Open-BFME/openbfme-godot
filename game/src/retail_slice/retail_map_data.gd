class_name RetailMapData
extends RefCounted
## Bounded runtime bridge for the cooker's engine-neutral Fords of Isen II data.
##
## Godot never parses or packages the proprietary SAGE .map. The importer cooks
## a height grid, passability bits, and JSON geometry outside the repository;
## this class validates those files and exposes a small source-derived runtime
## view to the private retail slice.

const MAX_DOCUMENT_BYTES := 2 * 1024 * 1024
const MAX_TERRAIN_CELLS := 1_000_000
const MAX_MAP_OBJECTS := 5000
const MAX_WATER_VERTICES := 4096
const MAX_WAYPOINTS := 256
const MAX_GENERIC_PROPS := 72
const LOCAL_START_SEPARATION := 76.0
const FORD_CORRIDOR_DILATION_CELLS := 5
const MAX_ROUTE_CELLS := 1024
const REQUIRED_FORD_NAMES: Array[String] = ["ford1", "ford2", "ford3"]

var ready := false
var error := ""
var pack_root := ""
var map_root := ""
var width := 0
var height := 0
var heightmap_bytes := 0
var passability_bytes := 0
var impassable_count := 0
var terrain_texture_count := 0
var border_width := 0
var playable_world_extent := Vector2.ZERO
var playable_grid_min := Vector2i.ZERO
var playable_grid_max := Vector2i.ZERO
var raw_elevation_min := 0
var raw_elevation_max := 0
var computed_raw_elevation_min := 0
var computed_raw_elevation_max := 0
var computed_impassable_count := 0
var heightmap_sha256 := ""
var passability_sha256 := ""
var object_count := 0
var waypoint_count := 0
var player_start_count := 0
var standing_water_count := 0
var river_count := 0
var source_binary_packaged := true
var horizontal_scale := 0.0
var vertical_scale := 0.0
var passability_row_stride := 0
var reference_elevation := 0.0
var local_transform_scale := 0.0
var local_transform_origin := Vector2.ZERO
var local_axis_x := Vector2.RIGHT
var local_axis_z := Vector2.DOWN
var local_bounds := Rect2()
var height_samples := PackedByteArray()
var passability_bits := PackedByteArray()
var player_starts: Dictionary = {}
var local_player_starts: Dictionary = {}
var standing_water_polygons: Array = []
var river_strips: Array[Dictionary] = []
var ford_gates: Array[Dictionary] = []
var generic_prop_placements: Array[Dictionary] = []
var map_outline := PackedVector2Array()
var navigation_ready := false
var navigation_walkable_count := 0
var navigation_water_blocked_count := 0
var navigation_ford_corridor_count := 0
var navigation_build_count := 0
var route_query_count := 0
var _navigation_grid: AStarGrid2D
var _water_cells := PackedByteArray()
var _ford_corridor_cells: Dictionary = {}


func load_from_pack(selected_pack_root: String, map_definition: Dictionary) -> bool:
	_reset()
	pack_root = selected_pack_root
	var source_path := String(map_definition.get("_source", ""))
	if source_path == "" or not ModLoader.path_is_within(pack_root, source_path):
		return _fail("map document escaped the selected pack")
	map_root = source_path.get_base_dir()
	source_binary_packaged = bool(map_definition.get("sourceBinaryPackaged", true))
	if source_binary_packaged:
		return _fail("retail source map must not be packaged")

	var terrain := _read_document(String(map_definition.get("terrain", "")), "terrain")
	var objects := _read_document(String(map_definition.get("objects", "")), "objects")
	var waypoints := _read_document(String(map_definition.get("waypoints", "")), "waypoints")
	var water := _read_document(String(map_definition.get("water", "")), "water")
	if terrain.is_empty() or objects.is_empty() or waypoints.is_empty() or water.is_empty():
		return false
	if String(terrain.get("schema", "")) != "openbfme.sage-terrain" or int(terrain.get("schemaVersion", -1)) != 0:
		return _fail("unexpected cooked terrain schema")
	if String(objects.get("schema", "")) != "openbfme.sage-map-objects" or int(objects.get("schemaVersion", -1)) != 0:
		return _fail("unexpected cooked object schema")
	if String(waypoints.get("schema", "")) != "openbfme.sage-waypoints" or int(waypoints.get("schemaVersion", -1)) != 0:
		return _fail("unexpected cooked waypoint schema")
	if String(water.get("schema", "")) != "openbfme.sage-water" or int(water.get("schemaVersion", -1)) != 0:
		return _fail("unexpected cooked water schema")

	if not _load_terrain(terrain):
		return false
	if not _load_waypoints(waypoints):
		return false
	if not _load_water(water):
		return false
	if not _load_objects(objects):
		return false
	_build_map_outline()
	if not _build_navigation():
		return false
	ready = true
	return true


func _load_terrain(terrain: Dictionary) -> bool:
	var height_data := _dictionary(terrain.get("height", {}))
	var height_file := _dictionary(height_data.get("heightmap", {}))
	var passability := _dictionary(terrain.get("passability", {}))
	var blend := _dictionary(terrain.get("blend", {}))
	var grid_stats := _dictionary(blend.get("gridStats", {}))
	width = int(height_data.get("width", 0))
	height = int(height_data.get("height", 0))
	border_width = int(height_data.get("borderWidth", -1))
	horizontal_scale = float(height_data.get("horizontalScale", 0.0))
	vertical_scale = float(height_data.get("verticalScale", 0.0))
	raw_elevation_min = int(height_data.get("rawElevationMin", -1))
	raw_elevation_max = int(height_data.get("rawElevationMax", -1))
	impassable_count = int(grid_stats.get("impassable", -1))
	terrain_texture_count = _array(blend.get("textures", [])).size()
	if width <= 1 or height <= 1 or width * height > MAX_TERRAIN_CELLS:
		return _fail("invalid or unbounded cooked heightmap dimensions")
	if not _finite_positive(horizontal_scale) or not _finite_positive(vertical_scale):
		return _fail("invalid cooked terrain scale")
	if String(height_file.get("encoding", "")) != "uint16" or String(height_file.get("endianness", "")) != "little" or String(height_file.get("order", "")) != "row-major-y-then-x":
		return _fail("unsupported cooked heightmap encoding")
	if String(passability.get("meaning", "")) != "one-is-impassable" or String(passability.get("bitOrder", "")) != "least-significant-bit-first" or not bool(passability.get("rowPadding", false)) or not bool(passability.get("sourceExact", false)):
		return _fail("unsupported cooked passability semantics")
	var extent_values := _array(height_data.get("playableWorldExtent", []))
	if border_width <= 0 or border_width * 2 >= mini(width, height) or extent_values.size() != 2:
		return _fail("invalid cooked playable border metadata")
	playable_world_extent = Vector2(float(extent_values[0]), float(extent_values[1]))
	var expected_extent := Vector2(float(width - border_width * 2), float(height - border_width * 2)) * horizontal_scale
	if not _finite_positive(playable_world_extent.x) or not _finite_positive(playable_world_extent.y) or not playable_world_extent.is_equal_approx(expected_extent):
		return _fail("playable world extent does not match the declared border")
	playable_grid_min = Vector2i(border_width, border_width)
	# SAGE's declared extent is measured from the inset sample to this inclusive
	# far sample: 20..395 and 20..333 for this cook.
	playable_grid_max = Vector2i(width - border_width, height - border_width)

	var height_path := _resolve(String(height_file.get("path", "")))
	var passability_path := _resolve(String(passability.get("path", "")))
	if height_path == "" or passability_path == "":
		return _fail("cooked terrain binaries are missing or unsafe")
	height_samples = _read_bytes(height_path, width * height * 2, "heightmap")
	passability_row_stride = int(passability.get("rowStrideBytes", 0))
	passability_bits = _read_bytes(passability_path, passability_row_stride * height, "passability")
	heightmap_bytes = height_samples.size()
	passability_bytes = passability_bits.size()
	if heightmap_bytes != width * height * 2:
		return _fail("heightmap byte count does not match its metadata")
	if passability_row_stride != (width + 7) / 8 or passability_bytes != passability_row_stride * height:
		return _fail("passability byte count does not match its metadata")
	if impassable_count < 0 or impassable_count > width * height:
		return _fail("invalid cooked impassability count")
	computed_raw_elevation_min = 0x7FFFFFFF
	computed_raw_elevation_max = -1
	computed_impassable_count = 0
	for grid_y in range(height):
		for grid_x in range(width):
			var raw_height := height_raw_at(grid_x, grid_y)
			computed_raw_elevation_min = mini(computed_raw_elevation_min, raw_height)
			computed_raw_elevation_max = maxi(computed_raw_elevation_max, raw_height)
			if is_impassable_at(grid_x, grid_y):
				computed_impassable_count += 1
	if computed_raw_elevation_min != raw_elevation_min or computed_raw_elevation_max != raw_elevation_max:
		return _fail("heightmap raw elevation range does not match its metadata")
	if computed_impassable_count != impassable_count:
		return _fail("passability popcount does not match its metadata")
	heightmap_sha256 = _sha256(height_samples)
	passability_sha256 = _sha256(passability_bits)
	return true


func _load_waypoints(document: Dictionary) -> bool:
	var waypoints := _array(document.get("waypoints", []))
	var starts := _dictionary(document.get("playerStarts", {}))
	waypoint_count = waypoints.size()
	player_start_count = starts.size()
	if waypoint_count != int(document.get("count", -1)) or waypoint_count > MAX_WAYPOINTS:
		return _fail("cooked waypoint count does not match its metadata")
	for row_value in waypoints:
		var row := _dictionary(row_value)
		if row.is_empty() or _vector3(row.get("godotPosition", [])) == Vector3.INF:
			return _fail("invalid cooked waypoint placement")
	for required_name in ["Player_1_Start", "Player_2_Start"]:
		var row := _dictionary(starts.get(required_name, {}))
		var position := _vector3(row.get("godotPosition", []))
		if row.is_empty() or position == Vector3.INF:
			return _fail("missing or invalid %s" % required_name)
		player_starts[required_name] = position
	if player_start_count != 2:
		return _fail("Fords runtime expects exactly two player starts")

	var player_one: Vector3 = player_starts["Player_1_Start"]
	var player_two: Vector3 = player_starts["Player_2_Start"]
	var one_horizontal := Vector2(player_one.x, player_one.z)
	var two_horizontal := Vector2(player_two.x, player_two.z)
	var separation := one_horizontal.distance_to(two_horizontal)
	if separation <= 1.0 or not _finite_positive(separation):
		return _fail("player starts cannot define a local battlefield transform")
	local_transform_origin = (one_horizontal + two_horizontal) * 0.5
	local_axis_x = (one_horizontal - two_horizontal).normalized()
	local_axis_z = Vector2(-local_axis_x.y, local_axis_x.x)
	local_transform_scale = LOCAL_START_SEPARATION / separation
	reference_elevation = (player_one.y + player_two.y) * 0.5
	local_player_starts["Player_1_Start"] = source_to_local(player_one)
	local_player_starts["Player_2_Start"] = source_to_local(player_two)
	return true


func _load_water(document: Dictionary) -> bool:
	var standing := _array(document.get("standingAreas", []))
	var rivers := _array(document.get("rivers", []))
	standing_water_count = standing.size()
	river_count = rivers.size()
	if standing_water_count > 128 or river_count > 128:
		return _fail("unbounded cooked water collection")
	var total_vertices := 0
	for area_value in standing:
		var area := _dictionary(area_value)
		var points := _array(area.get("godotPoints", []))
		if points.size() < 3 or points.size() > 256:
			return _fail("invalid standing-water polygon")
		var local_points := PackedVector3Array()
		for value in points:
			var source_point := _vector3(value)
			if source_point == Vector3.INF:
				return _fail("invalid standing-water vertex")
			local_points.append(source_to_local(source_point))
		standing_water_polygons.append(local_points)
		total_vertices += local_points.size()

	for river_value in rivers:
		var river := _dictionary(river_value)
		var sections := _array(river.get("crossSections", []))
		if sections.size() < 2 or sections.size() > 512:
			return _fail("invalid cooked river strip")
		var local_sections: Array = []
		for section_value in sections:
			var section := _dictionary(section_value)
			var edge_a := _vector3(section.get("godotV0", []))
			var edge_b := _vector3(section.get("godotV1", []))
			if edge_a == Vector3.INF or edge_b == Vector3.INF:
				return _fail("invalid cooked river cross-section")
			local_sections.append(PackedVector3Array([source_to_local(edge_a), source_to_local(edge_b)]))
		total_vertices += local_sections.size() * 2
		river_strips.append({
			"name": String(river.get("name", "")),
			"id": int(river.get("id", -1)),
			"sections": local_sections,
		})
	if total_vertices > MAX_WATER_VERTICES:
		return _fail("unbounded cooked water geometry")
	if not _derive_ford_gates():
		return false
	return true


func _derive_ford_gates() -> bool:
	ford_gates.clear()
	for ford_name in REQUIRED_FORD_NAMES:
		var matching_river: Dictionary = {}
		for river in river_strips:
			if String(river.get("name", "")) == ford_name:
				matching_river = river
				break
		if matching_river.is_empty():
			return _fail("missing cooked %s water geometry" % ford_name)
		var sections: Array = matching_river["sections"]
		var middle_index := sections.size() / 2
		var middle: PackedVector3Array = sections[middle_index]
		var center := Vector3.ZERO
		var sample_count := 0
		for section_value in sections:
			var section: PackedVector3Array = section_value
			center += section[0] + section[1]
			sample_count += 2
		center /= float(sample_count)
		ford_gates.append({
			"name": ford_name,
			"source_river_id": int(matching_river.get("id", -1)),
			"source_section_index": middle_index,
			"edge_a": Vector2(middle[0].x, middle[0].z),
			"edge_b": Vector2(middle[1].x, middle[1].z),
			"center": Vector2(center.x, center.z),
			"elevation": (middle[0].y + middle[1].y) * 0.5,
		})
	return ford_gates.size() == 3


func _load_objects(document: Dictionary) -> bool:
	var objects := _array(document.get("objects", []))
	object_count = objects.size()
	if object_count != int(document.get("count", -1)) or object_count > MAX_MAP_OBJECTS:
		return _fail("cooked map object count does not match its metadata")
	var vegetation: Array[Dictionary] = []
	var rocks: Array[Dictionary] = []
	for object_value in objects:
		var object := _dictionary(object_value)
		var type_name := String(object.get("typeName", ""))
		var source_position := _vector3(object.get("godotPosition", []))
		if object.is_empty() or type_name == "" or type_name.length() > 128 or source_position == Vector3.INF:
			return _fail("invalid cooked object placement")
		var lowered := type_name.to_lower()
		var kind := ""
		if lowered.contains("tree") or lowered.contains("shrub") or lowered.contains("bush"):
			kind = "vegetation"
		elif lowered.contains("rock") or lowered.contains("boulder"):
			kind = "rock"
		if kind == "":
			continue
		var placement := {
			"kind": kind,
			"source_type": type_name,
			"source_index": int(object.get("index", -1)),
			"position": source_to_local(source_position),
			"yaw": float(object.get("godotYawRadians", 0.0)),
		}
		if not _finite_number(float(placement["yaw"])):
			return _fail("invalid cooked object rotation")
		if kind == "vegetation":
			vegetation.append(placement)
		else:
			rocks.append(placement)
	generic_prop_placements.append_array(_even_sample(vegetation, 48))
	generic_prop_placements.append_array(_even_sample(rocks, 24))
	if generic_prop_placements.size() > MAX_GENERIC_PROPS:
		return _fail("generic source-placement preview exceeded its bound")
	return true


func _build_map_outline() -> void:
	var source_min_x := float(playable_grid_min.x) * horizontal_scale
	var source_max_x := float(playable_grid_max.x) * horizontal_scale
	var source_min_y := float(playable_grid_min.y) * horizontal_scale
	var source_max_y := float(playable_grid_max.y) * horizontal_scale
	map_outline = PackedVector2Array([
		_horizontal(source_to_local(Vector3(source_min_x, reference_elevation, -source_min_y))),
		_horizontal(source_to_local(Vector3(source_max_x, reference_elevation, -source_min_y))),
		_horizontal(source_to_local(Vector3(source_max_x, reference_elevation, -source_max_y))),
		_horizontal(source_to_local(Vector3(source_min_x, reference_elevation, -source_max_y))),
	])
	var minimum := map_outline[0]
	var maximum := map_outline[0]
	for point in map_outline:
		minimum = Vector2(minf(minimum.x, point.x), minf(minimum.y, point.y))
		maximum = Vector2(maxf(maximum.x, point.x), maxf(maximum.y, point.y))
	local_bounds = Rect2(minimum, maximum - minimum)


func _build_navigation() -> bool:
	navigation_ready = false
	_navigation_grid = null
	_water_cells.resize(width * height)
	_water_cells.fill(0)
	_ford_corridor_cells.clear()
	for ford_name in REQUIRED_FORD_NAMES:
		var empty_mask := PackedByteArray()
		empty_mask.resize(width * height)
		empty_mask.fill(0)
		_ford_corridor_cells[ford_name] = empty_mask

	for polygon_value in standing_water_polygons:
		_rasterize_local_polygon(_water_cells, polygon_value as PackedVector3Array)
	for river in river_strips:
		var sections: Array = river.get("sections", [])
		var river_name := String(river.get("name", ""))
		for section_index in range(sections.size() - 1):
			var first: PackedVector3Array = sections[section_index]
			var second: PackedVector3Array = sections[section_index + 1]
			var polygon := PackedVector3Array([first[0], second[0], second[1], first[1]])
			_rasterize_local_polygon(_water_cells, polygon)
			if _ford_corridor_cells.has(river_name):
				var ford_mask: PackedByteArray = _ford_corridor_cells[river_name]
				_rasterize_local_polygon(ford_mask, polygon)
				_ford_corridor_cells[river_name] = ford_mask

	for ford_name in REQUIRED_FORD_NAMES:
		var source_mask: PackedByteArray = _ford_corridor_cells[ford_name]
		# Continuous river edges can fall between 10-unit grid samples. A fixed
		# five-cell alignment margin reaches both banks; it only exempts cooked
		# water masking and never clears a source impassability bit.
		var expanded := _dilate_mask(source_mask, FORD_CORRIDOR_DILATION_CELLS)
		_ford_corridor_cells[ford_name] = expanded

	_navigation_grid = AStarGrid2D.new()
	_navigation_grid.region = Rect2i(playable_grid_min, playable_grid_max - playable_grid_min + Vector2i.ONE)
	_navigation_grid.cell_size = Vector2.ONE
	_navigation_grid.diagonal_mode = AStarGrid2D.DIAGONAL_MODE_NEVER
	_navigation_grid.default_compute_heuristic = AStarGrid2D.HEURISTIC_MANHATTAN
	_navigation_grid.default_estimate_heuristic = AStarGrid2D.HEURISTIC_MANHATTAN
	_navigation_grid.update()
	navigation_walkable_count = 0
	navigation_water_blocked_count = 0
	navigation_ford_corridor_count = 0
	for grid_y in range(playable_grid_min.y, playable_grid_max.y + 1):
		for grid_x in range(playable_grid_min.x, playable_grid_max.x + 1):
			var cell := Vector2i(grid_x, grid_y)
			var water_blocked := is_water_cell(cell) and not is_ford_corridor_cell(cell)
			var blocked := is_impassable_at(grid_x, grid_y) or water_blocked
			if blocked:
				_navigation_grid.set_point_solid(cell, true)
				if water_blocked:
					navigation_water_blocked_count += 1
			else:
				navigation_walkable_count += 1
			if is_ford_corridor_cell(cell):
				navigation_ford_corridor_count += 1
	navigation_build_count += 1
	navigation_ready = navigation_walkable_count > 0 and navigation_water_blocked_count > 0 and navigation_ford_corridor_count > 0
	if not navigation_ready:
		return _fail("cooked navigation topology could not be built")
	if is_navigation_walkable(Vector2i(208, 142)):
		return _fail("known source-impassable ford cell became walkable")
	return true


func _rasterize_local_polygon(mask: PackedByteArray, polygon: PackedVector3Array) -> void:
	if polygon.size() < 3:
		return
	var grid_polygon := PackedVector2Array()
	var minimum := Vector2(INF, INF)
	var maximum := Vector2(-INF, -INF)
	for point in polygon:
		var grid_point := local_to_grid_float(Vector2(point.x, point.z))
		grid_polygon.append(grid_point)
		minimum = Vector2(minf(minimum.x, grid_point.x), minf(minimum.y, grid_point.y))
		maximum = Vector2(maxf(maximum.x, grid_point.x), maxf(maximum.y, grid_point.y))
	var minimum_cell := Vector2i(
		clampi(floori(minimum.x), playable_grid_min.x, playable_grid_max.x),
		clampi(floori(minimum.y), playable_grid_min.y, playable_grid_max.y)
	)
	var maximum_cell := Vector2i(
		clampi(ceili(maximum.x), playable_grid_min.x, playable_grid_max.x),
		clampi(ceili(maximum.y), playable_grid_min.y, playable_grid_max.y)
	)
	for grid_y in range(minimum_cell.y, maximum_cell.y + 1):
		for grid_x in range(minimum_cell.x, maximum_cell.x + 1):
			if Geometry2D.is_point_in_polygon(Vector2(grid_x, grid_y), grid_polygon):
				mask[grid_y * width + grid_x] = 1


func _dilate_mask(source: PackedByteArray, radius: int) -> PackedByteArray:
	var result := PackedByteArray()
	result.resize(width * height)
	result.fill(0)
	for grid_y in range(playable_grid_min.y, playable_grid_max.y + 1):
		for grid_x in range(playable_grid_min.x, playable_grid_max.x + 1):
			if source[grid_y * width + grid_x] == 0:
				continue
			for expanded_y in range(maxi(playable_grid_min.y, grid_y - radius), mini(playable_grid_max.y, grid_y + radius) + 1):
				for expanded_x in range(maxi(playable_grid_min.x, grid_x - radius), mini(playable_grid_max.x, grid_x + radius) + 1):
					result[expanded_y * width + expanded_x] = 1
	return result


func query_route(from_local: Vector2, to_local: Vector2) -> Dictionary:
	route_query_count += 1
	if not navigation_ready:
		return {"valid": false, "reason": "navigation-unavailable", "points": [], "cells": []}
	if not is_local_inside_playable(from_local) or not is_local_inside_playable(to_local):
		return {"valid": false, "reason": "outside-playable-area", "points": [], "cells": []}
	var start_cell := local_to_grid_cell(from_local)
	var destination_cell := local_to_grid_cell(to_local)
	if not is_navigation_walkable(destination_cell):
		return {"valid": false, "reason": "blocked-destination", "points": [], "cells": []}
	if not is_navigation_walkable(start_cell):
		start_cell = _nearest_walkable_cell(start_cell, 12)
		if start_cell.x < 0:
			return {"valid": false, "reason": "blocked-origin", "points": [], "cells": []}
	var id_path: Array[Vector2i] = _navigation_grid.get_id_path(start_cell, destination_cell, false)
	if id_path.is_empty() or id_path.size() > MAX_ROUTE_CELLS:
		return {"valid": false, "reason": "no-bounded-route", "points": [], "cells": []}
	var points := _compress_route_points(id_path, to_local)
	return {
		"valid": true,
		"reason": "",
		"points": points,
		"cells": id_path,
		"ford_name": _ford_name_for_cells(id_path),
	}


func query_ford_probe(ford_name: String) -> Dictionary:
	if not REQUIRED_FORD_NAMES.has(ford_name):
		return {"valid": false, "reason": "unknown-ford", "points": [], "cells": []}
	for river in river_strips:
		if String(river.get("name", "")) != ford_name:
			continue
		var sections: Array = river.get("sections", [])
		if sections.size() < 2:
			break
		var grid_step_local := horizontal_scale * local_transform_scale
		# Begin at the middle cross-section, then nudge along the source strip to
		# the nearest section whose exact passability topology connects both banks.
		for section_index in _middle_out_indices(sections.size()):
			var section: PackedVector3Array = sections[section_index]
			var edge_a := Vector2(section[0].x, section[0].z)
			var edge_b := Vector2(section[1].x, section[1].z)
			var direction := edge_a.direction_to(edge_b)
			if direction.is_zero_approx():
				continue
			var start := _find_non_water_bank(edge_a, -direction, grid_step_local, 48)
			var finish := _find_non_water_bank(edge_b, direction, grid_step_local, 48)
			if start.x < 0 or finish.x < 0:
				continue
			var result := query_route(grid_to_local_horizontal(start), grid_to_local_horizontal(finish))
			if not bool(result.get("valid", false)) or String(result.get("ford_name", "")) != ford_name or not _route_contains_named_water(result.get("cells", []), ford_name):
				continue
			result["probe_ford_name"] = ford_name
			result["probe_section_index"] = section_index
			result["probe_bank_a"] = start
			result["probe_bank_b"] = finish
			result["probe_edge_a"] = edge_a
			result["probe_edge_b"] = edge_b
			return result
	return {"valid": false, "reason": "ford-probe-unavailable", "points": [], "cells": []}


func _middle_out_indices(size: int) -> Array[int]:
	var result: Array[int] = []
	var middle := (size - 1) / 2
	result.append(middle)
	for distance in range(1, size):
		var before := middle - distance
		var after := middle + distance
		if before >= 0:
			result.append(before)
		if after < size:
			result.append(after)
	return result


func _route_contains_named_water(cells_value: Variant, ford_name: String) -> bool:
	if typeof(cells_value) != TYPE_ARRAY:
		return false
	for cell_value in cells_value as Array:
		var cell := Vector2i(cell_value)
		if is_water_cell(cell) and is_named_ford_corridor_cell(cell, ford_name):
			return true
	return false


func _find_non_water_bank(edge: Vector2, outward: Vector2, grid_step_local: float, maximum_steps: int) -> Vector2i:
	for step in range(1, maximum_steps + 1):
		var candidate := local_to_grid_cell(edge + outward * grid_step_local * float(step))
		if not is_grid_inside_playable(candidate):
			break
		if is_navigation_walkable(candidate) and not is_water_cell(candidate):
			return candidate
	return Vector2i(-1, -1)


func _compress_route_points(cells: Array[Vector2i], exact_destination: Vector2) -> Array[Vector2]:
	var points: Array[Vector2] = []
	if cells.size() > 1:
		var previous_direction := cells[1] - cells[0]
		for index in range(1, cells.size()):
			var next_direction := previous_direction
			if index + 1 < cells.size():
				next_direction = cells[index + 1] - cells[index]
			if index == cells.size() - 1 or next_direction != previous_direction:
				points.append(grid_to_local_horizontal(cells[index]))
			previous_direction = next_direction
	if points.is_empty() or not points[-1].is_equal_approx(exact_destination):
		points.append(exact_destination)
	return points


func _ford_name_for_cells(cells: Array[Vector2i]) -> String:
	var best_name := ""
	var best_count := 0
	for ford_name in REQUIRED_FORD_NAMES:
		var count := 0
		for cell in cells:
			if is_named_ford_corridor_cell(cell, ford_name):
				count += 1
		if count > best_count:
			best_name = ford_name
			best_count = count
	return best_name


func _nearest_walkable_cell(origin: Vector2i, maximum_radius: int) -> Vector2i:
	if is_navigation_walkable(origin):
		return origin
	for radius in range(1, maximum_radius + 1):
		for offset_y in range(-radius, radius + 1):
			var offset_x := radius - absi(offset_y)
			var candidates: Array[Vector2i] = [Vector2i(origin.x - offset_x, origin.y + offset_y)]
			if offset_x != 0:
				candidates.append(Vector2i(origin.x + offset_x, origin.y + offset_y))
			for candidate in candidates:
				if is_navigation_walkable(candidate):
					return candidate
	return Vector2i(-1, -1)


func is_local_inside_playable(local_position: Vector2) -> bool:
	var grid := local_to_grid_float(local_position)
	return grid.x >= float(playable_grid_min.x) - 0.001 and grid.x <= float(playable_grid_max.x) + 0.001 and grid.y >= float(playable_grid_min.y) - 0.001 and grid.y <= float(playable_grid_max.y) + 0.001


func local_to_grid_float(local_position: Vector2) -> Vector2:
	var source := local_to_source_horizontal(local_position)
	return Vector2(source.x / horizontal_scale, -source.y / horizontal_scale)


func local_to_grid_cell(local_position: Vector2) -> Vector2i:
	var grid := local_to_grid_float(local_position)
	return Vector2i(roundi(grid.x), roundi(grid.y))


func grid_to_local_horizontal(cell: Vector2i) -> Vector2:
	var local := source_to_local(Vector3(float(cell.x) * horizontal_scale, reference_elevation, -float(cell.y) * horizontal_scale))
	return Vector2(local.x, local.z)


func is_grid_inside_playable(cell: Vector2i) -> bool:
	return cell.x >= playable_grid_min.x and cell.x <= playable_grid_max.x and cell.y >= playable_grid_min.y and cell.y <= playable_grid_max.y


func is_navigation_walkable(cell: Vector2i) -> bool:
	return navigation_ready and is_grid_inside_playable(cell) and not _navigation_grid.is_point_solid(cell)


func is_water_cell(cell: Vector2i) -> bool:
	return is_grid_inside_playable(cell) and _water_cells[cell.y * width + cell.x] != 0


func is_ford_corridor_cell(cell: Vector2i) -> bool:
	for ford_name in REQUIRED_FORD_NAMES:
		if is_named_ford_corridor_cell(cell, ford_name):
			return true
	return false


func is_named_ford_corridor_cell(cell: Vector2i, ford_name: String) -> bool:
	if not is_grid_inside_playable(cell) or not _ford_corridor_cells.has(ford_name):
		return false
	var mask: PackedByteArray = _ford_corridor_cells[ford_name]
	return mask[cell.y * width + cell.x] != 0


func source_to_local(source_position: Vector3) -> Vector3:
	var delta := Vector2(source_position.x, source_position.z) - local_transform_origin
	return Vector3(
		delta.dot(local_axis_x) * local_transform_scale,
		(source_position.y - reference_elevation) * local_transform_scale,
		delta.dot(local_axis_z) * local_transform_scale
	)


func local_to_source_horizontal(local_position: Vector2) -> Vector2:
	if local_transform_scale <= 0.0:
		return Vector2.ZERO
	return local_transform_origin + local_axis_x * (local_position.x / local_transform_scale) + local_axis_z * (local_position.y / local_transform_scale)


func terrain_local_at(grid_x: int, grid_y: int) -> Vector3:
	var safe_x := clampi(grid_x, 0, width - 1)
	var safe_y := clampi(grid_y, 0, height - 1)
	var source := Vector3(
		float(safe_x) * horizontal_scale,
		float(height_raw_at(safe_x, safe_y)) * vertical_scale,
		-float(safe_y) * horizontal_scale
	)
	return source_to_local(source)


func height_raw_at(grid_x: int, grid_y: int) -> int:
	if grid_x < 0 or grid_x >= width or grid_y < 0 or grid_y >= height:
		return 0
	var offset := (grid_y * width + grid_x) * 2
	if offset + 1 >= height_samples.size():
		return 0
	return int(height_samples[offset]) | (int(height_samples[offset + 1]) << 8)


func is_impassable_at(grid_x: int, grid_y: int) -> bool:
	if grid_x < 0 or grid_x >= width or grid_y < 0 or grid_y >= height:
		return true
	var offset := grid_y * passability_row_stride + grid_x / 8
	if offset < 0 or offset >= passability_bits.size():
		return true
	return (int(passability_bits[offset]) & (1 << (grid_x % 8))) != 0


func local_ground_height(local_position: Vector2) -> float:
	var source := local_to_source_horizontal(local_position)
	var grid_x := clampi(roundi(source.x / horizontal_scale), 0, width - 1)
	var grid_y := clampi(roundi(-source.y / horizontal_scale), 0, height - 1)
	return (float(height_raw_at(grid_x, grid_y)) * vertical_scale - reference_elevation) * local_transform_scale


func simulation_configuration() -> Dictionary:
	var player_one: Vector3 = local_player_starts.get("Player_1_Start", Vector3(38.0, 0.0, 0.0))
	var player_two: Vector3 = local_player_starts.get("Player_2_Start", Vector3(-38.0, 0.0, 0.0))
	var spawn_positions := {}
	spawn_positions[1] = _walkable_spawn(Vector2(player_two.x, player_two.z - 4.5))
	spawn_positions[2] = _walkable_spawn(Vector2(player_two.x, player_two.z + 4.5))
	spawn_positions[101] = _walkable_spawn(Vector2(player_one.x, player_one.z - 4.5))
	spawn_positions[102] = _walkable_spawn(Vector2(player_one.x, player_one.z + 4.5))
	var player_one_horizontal := Vector2(player_one.x, player_one.z)
	var player_two_horizontal := Vector2(player_two.x, player_two.z)
	return {
		"source_map_configured": ready,
		"route_provider": self,
		"playable_outline": map_outline.duplicate(),
		"spawn_positions": spawn_positions,
		"player_starts": {
			"Player_1_Start": player_one_horizontal,
			"Player_2_Start": player_two_horizontal,
		},
		# Team 0 intentionally uses source Player_2, matching the existing spawn
		# mapping. Every base/rally anchor is snapped to the cooked navigation
		# mask before it enters deterministic simulation.
		"home_layout": {
			0: _home_layout_for(player_two_horizontal, player_one_horizontal),
			1: _home_layout_for(player_one_horizontal, player_two_horizontal),
		},
		"ford_gates": ford_gates.duplicate(true),
	}


func _home_layout_for(home: Vector2, opponent: Vector2) -> Dictionary:
	var inward := home.direction_to(opponent)
	if inward.length_squared() < 0.01:
		inward = Vector2.RIGHT
	var outward := -inward
	var side := Vector2(-inward.y, inward.x)
	return {
		"fortress": _walkable_spawn(home + outward * 9.0),
		"farm": _walkable_spawn(home + side * 11.0 + outward * 2.0),
		"barracks": _walkable_spawn(home - side * 11.0 + outward),
		"archery_range": _walkable_spawn(home + side * 18.0 - outward * 3.0),
		"stable": _walkable_spawn(home - side * 18.0 - outward * 3.0),
		"rally": _walkable_spawn(home + inward * 9.0),
	}


func resolve_walkable_position(candidate: Vector2) -> Vector2:
	return _walkable_spawn(candidate)


func _walkable_spawn(candidate: Vector2) -> Vector2:
	var cell := local_to_grid_cell(candidate)
	if is_navigation_walkable(cell):
		return candidate
	var nearest := _nearest_walkable_cell(cell, 12)
	return grid_to_local_horizontal(nearest) if nearest.x >= 0 else candidate


func _read_document(relative: String, label: String) -> Dictionary:
	var path := _resolve(relative)
	if path == "" or not FileAccess.file_exists(path):
		_fail("missing or unsafe %s document" % label)
		return {}
	var byte_count := _file_size(path)
	if byte_count <= 0 or byte_count > MAX_DOCUMENT_BYTES:
		_fail("invalid or unbounded %s document" % label)
		return {}
	var value: Variant = ModLoader._read_json(path)
	if typeof(value) != TYPE_DICTIONARY:
		_fail("invalid %s document" % label)
		return {}
	return value as Dictionary


func _read_bytes(path: String, expected_size: int, label: String) -> PackedByteArray:
	if expected_size <= 0 or expected_size > MAX_TERRAIN_CELLS * 2:
		_fail("invalid %s size" % label)
		return PackedByteArray()
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null or file.get_length() != expected_size:
		if file != null:
			file.close()
		_fail("%s byte count does not match its metadata" % label)
		return PackedByteArray()
	var bytes := file.get_buffer(expected_size)
	file.close()
	return bytes


func _resolve(relative: String) -> String:
	if relative == "":
		return ""
	var resolved := ModLoader.resolve_pack_path(map_root, relative)
	if resolved == "" or not ModLoader.path_is_within(map_root, resolved) or not ModLoader.path_is_within(pack_root, resolved):
		return ""
	return resolved


func _file_size(path: String) -> int:
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null:
		return -1
	var size := file.get_length()
	file.close()
	return size


func _sha256(bytes: PackedByteArray) -> String:
	var context := HashingContext.new()
	if context.start(HashingContext.HASH_SHA256) != OK:
		return ""
	if context.update(bytes) != OK:
		return ""
	return context.finish().hex_encode()


func _vector3(value: Variant) -> Vector3:
	var values := _array(value)
	if values.size() != 3:
		return Vector3.INF
	var result := Vector3(float(values[0]), float(values[1]), float(values[2]))
	if not _finite_number(result.x) or not _finite_number(result.y) or not _finite_number(result.z):
		return Vector3.INF
	return result


func _even_sample(candidates: Array[Dictionary], limit: int) -> Array[Dictionary]:
	if candidates.size() <= limit:
		return candidates.duplicate(true)
	var result: Array[Dictionary] = []
	for index in range(limit):
		var source_index := int(floor(float(index) * float(candidates.size()) / float(limit)))
		result.append(candidates[source_index].duplicate(true))
	return result


func _horizontal(value: Vector3) -> Vector2:
	return Vector2(value.x, value.z)


func _finite_positive(value: float) -> bool:
	return _finite_number(value) and value > 0.0


func _finite_number(value: float) -> bool:
	return not is_nan(value) and not is_inf(value)


func _dictionary(value: Variant) -> Dictionary:
	return value as Dictionary if typeof(value) == TYPE_DICTIONARY else {}


func _array(value: Variant) -> Array:
	return value as Array if typeof(value) == TYPE_ARRAY else []


func _reset() -> void:
	ready = false
	error = ""
	height_samples = PackedByteArray()
	passability_bits = PackedByteArray()
	player_starts.clear()
	local_player_starts.clear()
	standing_water_polygons.clear()
	river_strips.clear()
	ford_gates.clear()
	generic_prop_placements.clear()
	map_outline = PackedVector2Array()
	navigation_ready = false
	navigation_walkable_count = 0
	navigation_water_blocked_count = 0
	navigation_ford_corridor_count = 0
	navigation_build_count = 0
	route_query_count = 0
	_navigation_grid = null
	_water_cells = PackedByteArray()
	_ford_corridor_cells.clear()


func _fail(message: String) -> bool:
	if error == "":
		error = message
	ready = false
	return false
