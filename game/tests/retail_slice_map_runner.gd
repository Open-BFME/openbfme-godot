extends SceneTree
## Headless boot gate for the non-default retail slice maps.
##
## The slice resolves its map from OPENBFME_SLICE_MAP (see
## retail_vertical_slice._resolve_slice_map_id); this runner reads the same
## variable, boots the slice scene, and verifies the map's cooked terrain,
## water, props, spawns, and navigation end-to-end. Fords of Isen II itself is
## covered byte-exactly by retail_slice_runner.gd; this gate covers the other
## four maps of the bfme2-five-maps-106-private pack (and re-boots Fords
## through the default resolution path when asked).

const FIVE_MAPS_PACK_ID := "bfme2-five-maps-106-private"
const INITIALIZATION_WATCHDOG_MS := 10000
# Cooked-source facts read from the pack's maps/<slug>/terrain.json and
# water.json metadata; they are independent constants here on purpose.
const EXPECTED_MAPS := {
	"bfme2.map.fords-of-isen-ii": {
		"width": 415, "height": 353, "terrain_textures": 66, "player_starts": 2,
		"standing_water": 4, "rivers": 8, "ford_gates": 3, "five_maps_pack": false,
	},
	"bfme2.map.rivendell": {
		"width": 520, "height": 520, "terrain_textures": 60, "player_starts": 3,
		"standing_water": 5, "rivers": 0, "ford_gates": 0, "five_maps_pack": true,
	},
	"bfme2.map.mount-doom": {
		"width": 600, "height": 600, "terrain_textures": 34, "player_starts": 4,
		"standing_water": 0, "rivers": 15, "ford_gates": 0, "five_maps_pack": true,
	},
	"bfme2.map.dagorlad": {
		"width": 540, "height": 520, "terrain_textures": 84, "player_starts": 6,
		"standing_water": 14, "rivers": 0, "ford_gates": 0, "five_maps_pack": true,
	},
	"bfme2.map.mordor": {
		"width": 560, "height": 560, "terrain_textures": 27, "player_starts": 8,
		"standing_water": 1, "rivers": 4, "ford_gates": 0, "five_maps_pack": true,
	},
}

var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	root.size = Vector2i(1920, 1080)
	var requested_map := OS.get_environment("OPENBFME_SLICE_MAP").strip_edges().to_lower()
	if not EXPECTED_MAPS.has(requested_map):
		printerr("Set OPENBFME_SLICE_MAP to one of: %s" % ", ".join(EXPECTED_MAPS.keys()))
		_finish(requested_map)
		return
	var expected: Dictionary = EXPECTED_MAPS[requested_map]

	var packed: PackedScene = load("res://scenes/retail_vertical_slice.tscn")
	_check("scene_parses", packed != null)
	if packed == null:
		_finish(requested_map)
		return
	var slice = packed.instantiate()
	root.add_child(slice)
	await process_frame
	await process_frame

	_check("slice_ready", bool(slice.ready_ok), String(slice.failure_reason))
	var initialization_total_ms := int(slice.initialization_metrics_ms.get("ready_complete", -1))
	_check("initialization_completes_before_watchdog", initialization_total_ms >= 0 and initialization_total_ms <= INITIALIZATION_WATCHDOG_MS, "%d ms" % initialization_total_ms)
	_check("map_id_matches_requested", String(slice.map_id) == requested_map, String(slice.map_id))
	if bool(expected.get("five_maps_pack", false)):
		_check("map_pack_is_five_maps_supplement", String(slice.map_pack_root).contains(FIVE_MAPS_PACK_ID), String(slice.map_pack_root))
	else:
		_check("map_pack_is_selected_faction_pack", String(slice.map_pack_root) == String(slice.selected_pack_root), String(slice.map_pack_root))
	_check("map_preview_and_art_loaded", bool(slice.map_preview_loaded) and bool(slice.map_art_loaded))
	_check("terrain_is_source_driven", bool(slice.source_driven_terrain))

	if slice.source_map_data == null or not bool(slice.source_map_data.ready):
		_check("cooked_source_map_mounted", false, String(slice.source_map_data.error if slice.source_map_data != null else "missing"))
		_finish(requested_map)
		return
	var map_data = slice.source_map_data
	_check("cooked_source_map_mounted", true)
	_check("source_heightmap_exact", int(map_data.width) == int(expected.width) and int(map_data.height) == int(expected.height) and int(map_data.heightmap_bytes) == int(expected.width) * int(expected.height) * 2, "%dx%d" % [int(map_data.width), int(map_data.height)])
	_check("source_terrain_textures_exact", int(map_data.terrain_texture_count) == int(expected.terrain_textures) and map_data.terrain_material_catalog.size() == int(expected.terrain_textures), str(map_data.terrain_texture_count))
	_check("source_player_starts_exact", int(map_data.player_start_count) == int(expected.player_starts), str(map_data.player_start_count))
	_check("source_water_inventory_exact", int(map_data.standing_water_count) == int(expected.standing_water) and int(map_data.river_count) == int(expected.rivers), "standing=%d rivers=%d" % [int(map_data.standing_water_count), int(map_data.river_count)])
	_check("source_binary_not_packaged", not bool(map_data.source_binary_packaged))
	_check("ford_gates_match_map_authorship", map_data.ford_gates.size() == int(expected.ford_gates) and int(slice.crossing_count) == int(expected.ford_gates), str(map_data.ford_gates.size()))
	if int(expected.ford_gates) == 0:
		_check("roadless_map_has_empty_road_layer", int(map_data.road_type_count) == 0 and int(map_data.road_segment_count) == 0 and int(map_data.road_material_count) == 0)
	_check("playable_border_declared", int(map_data.border_width) > 0 and map_data.playable_grid_max.x > map_data.playable_grid_min.x and map_data.playable_grid_max.y > map_data.playable_grid_min.y)
	_check("map_outline_and_camera_bounds_present", map_data.map_outline.size() >= 3 and map_data.local_bounds.size.x > 0.0 and map_data.local_bounds.size.y > 0.0)
	_check("navigation_built_once", bool(map_data.navigation_ready) and int(map_data.navigation_build_count) == 1 and int(map_data.navigation_walkable_count) > 0, "walkable=%d water_blocked=%d corridors=%d" % [int(map_data.navigation_walkable_count), int(map_data.navigation_water_blocked_count), int(map_data.navigation_ford_corridor_count)])

	if slice.battlefield == null or not bool(slice.battlefield.source_driven):
		_check("battlefield_source_driven", false, String(slice.battlefield.error if slice.battlefield != null else "missing"))
		_finish(requested_map)
		return
	var battlefield = slice.battlefield
	_check("battlefield_source_driven", true)
	var expected_vertices := int(expected.width) * int(expected.height)
	var expected_triangles := (int(expected.width) - 1) * (int(expected.height) - 1) * 2
	_check("terrain_mesh_uses_every_exact_sample_and_quad", bool(battlefield.terrain_exact_grid_ready) and int(battlefield.terrain_vertex_count) == expected_vertices and int(battlefield.terrain_triangle_count) == expected_triangles, "vertices=%d triangles=%d" % [int(battlefield.terrain_vertex_count), int(battlefield.terrain_triangle_count)])
	_check("passability_overlay_covers_source_bits", int(battlefield.impassable_vertex_count) == int(map_data.impassable_count), str(battlefield.impassable_vertex_count))
	_check("terrain_material_is_source_driven", bool(battlefield.terrain_material_source_driven) and int(battlefield.terrain_texture_array_size) == int(expected.terrain_textures), str(battlefield.terrain_texture_array_size))
	_check("terrain_tile_data_texture_covers_grid", battlefield.terrain_tile_data_texture != null and battlefield.terrain_tile_data_texture.get_width() == int(expected.width) and battlefield.terrain_tile_data_texture.get_height() == int(expected.height))
	_check("water_mesh_built_from_source", int(battlefield.water_surface_count) == int(expected.standing_water) + int(expected.rivers) and int(battlefield.water_triangle_count) > 0, "surfaces=%d triangles=%d" % [int(battlefield.water_surface_count), int(battlefield.water_triangle_count)])
	_check("props_match_map_bindings", int(battlefield.bound_retail_prop_count) == int(map_data.bound_prop_placement_count) and int(battlefield.bound_retail_structure_count) == int(map_data.bound_structure_placement_count) and int(battlefield.generic_prop_count) == map_data.generic_prop_placements.size(), "bound_props=%d bound_structures=%d generic=%d" % [int(battlefield.bound_retail_prop_count), int(battlefield.bound_retail_structure_count), int(battlefield.generic_prop_count)])

	if slice.simulation == null:
		_check("simulation_uses_source_map_configuration", false)
		_finish(requested_map)
		return
	_check("simulation_uses_source_map_configuration", bool(slice.simulation.source_map_configured))
	_check("local_player_starts_present", map_data.local_player_starts.has("Player_1_Start") and map_data.local_player_starts.has("Player_2_Start"))
	_check("player_starts_are_walkable", _starts_walkable(map_data))
	_check("route_between_player_starts_valid", _route_between_starts_valid(map_data))
	# The sim seeds the two fortresses from the map's home layout; the rest of
	# each base is built in-match by builders, so the seeded roster is the two
	# fortresses.
	_check("fortresses_seeded_for_two_players", int(slice.simulation.fortress_id(0)) != 0 and int(slice.simulation.fortress_id(1)) != 0 and slice.structure_nodes.size() >= 2, str(slice.structure_nodes.size()))
	_check("home_layout_uses_source_navigation", _home_layout_walkable(slice))
	_check("minimap_uses_source_geometry", String(slice.minimap.mapping_mode) == "source-derived-local-transform" and bool(slice.minimap.source_geometry_loaded))

	var initial_tick := int(slice.simulation.tick_index)
	for _frame in range(90):
		await process_frame
	_check("simulation_ticks_advance", int(slice.simulation.tick_index) > initial_tick, "tick=%d" % int(slice.simulation.tick_index))

	print("MAP_BOOT %s terrain=%dx%d textures=%d water=%d props_bound=%d props_unresolved=%d walkable=%d structures=%d tick=%d" % [
		requested_map,
		int(map_data.width), int(map_data.height),
		int(map_data.terrain_texture_count),
		int(battlefield.water_surface_count),
		int(battlefield.bound_retail_prop_count),
		int(map_data.unresolved_prop_placement_count),
		int(map_data.navigation_walkable_count),
		slice.structure_nodes.size(),
		int(slice.simulation.tick_index),
	])
	_finish(requested_map)


func _starts_walkable(map_data) -> bool:
	for start_name in ["Player_1_Start", "Player_2_Start"]:
		var start := Vector3(map_data.local_player_starts.get(start_name, Vector3.INF))
		if start == Vector3.INF:
			return false
		# Start waypoints may sit on source-impassable or out-of-border cells;
		# what matters is that navigation snapping finds a walkable anchor.
		var resolved: Vector2 = map_data.resolve_walkable_position(Vector2(start.x, start.z))
		if not map_data.is_navigation_walkable(map_data.local_to_grid_cell(resolved)):
			return false
	return true


func _route_between_starts_valid(map_data) -> bool:
	var player_one := Vector3(map_data.local_player_starts.get("Player_1_Start", Vector3.INF))
	var player_two := Vector3(map_data.local_player_starts.get("Player_2_Start", Vector3.INF))
	if player_one == Vector3.INF or player_two == Vector3.INF:
		return false
	var one: Vector2 = map_data.resolve_walkable_position(Vector2(player_one.x, player_one.z))
	var two: Vector2 = map_data.resolve_walkable_position(Vector2(player_two.x, player_two.z))
	var forward: Dictionary = map_data.query_route(one, two)
	if not bool(forward.get("valid", false)):
		return false
	var backward: Dictionary = map_data.query_route(two, one)
	return bool(backward.get("valid", false))


func _home_layout_walkable(slice) -> bool:
	if slice.source_map_data == null:
		return false
	for id in slice.simulation.structure_ids():
		var position := Vector2(slice.simulation.structure(id).get("position", Vector2.INF))
		var cell: Vector2i = slice.source_map_data.local_to_grid_cell(position)
		if not slice.source_map_data.is_grid_inside_navigation(cell) or not slice.source_map_data.is_navigation_walkable(cell):
			return false
	return true


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("RETAIL_SLICE_MAP PASS %s" % name)
	else:
		failed += 1
		printerr("RETAIL_SLICE_MAP FAIL %s %s" % [name, detail])


func _finish(map_id: String) -> void:
	print("RETAIL_SLICE_MAP_RESULT map=%s passed=%d failed=%d" % [map_id, passed, failed])
	quit(0 if failed == 0 else 1)
