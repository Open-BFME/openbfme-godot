extends SceneTree
## Focused data-layer gate for the five cooked retail maps.
##
## Loads RetailMapData + RetailFordsBattlefield directly (no slice scene), so
## it verifies cooked terrain/water/props/navigation per map independent of
## the vertical-slice presentation layer. The full end-to-end slice boot per
## map is gated by retail_slice_map_runner.gd.

const FIVE_MAPS_PACK_ID := "bfme2-five-maps-106-private"
const MAP_CATALOG_MAX_BYTES := 1024 * 1024
const MAP_DOCUMENT_MAX_BYTES := 2 * 1024 * 1024
const EXPECTED_MAPS := {
	"bfme2.map.rivendell": {
		"width": 520, "height": 520, "terrain_textures": 60, "player_starts": 3,
		"standing_water": 5, "rivers": 0, "ford_gates": 0, "objects": 2196,
	},
	"bfme2.map.mount-doom": {
		"width": 600, "height": 600, "terrain_textures": 34, "player_starts": 4,
		"standing_water": 0, "rivers": 15, "ford_gates": 0, "objects": 1298,
	},
	"bfme2.map.dagorlad": {
		"width": 540, "height": 520, "terrain_textures": 84, "player_starts": 6,
		"standing_water": 14, "rivers": 0, "ford_gates": 0, "objects": 2836,
	},
	"bfme2.map.mordor": {
		"width": 560, "height": 560, "terrain_textures": 27, "player_starts": 8,
		"standing_water": 1, "rivers": 4, "ford_gates": 0, "objects": 1369,
	},
}

var passed := 0
var failed := 0
var _mod_loader
var _map_data_script
var _battlefield_script


const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
# Turns a GDScript runtime error inside `_run` — which unwinds past every
# `quit()` and would otherwise leave this headless process idling forever —
# into a loud non-zero exit. See tests/runner_watchdog.gd.
var _runner_watchdog := RunnerWatchdogScript.new()


func _initialize() -> void:
	_runner_watchdog.start(self, "RETAIL_MAP_DATA_RUNNER")
	call_deferred("_run")


func _run() -> void:
	_mod_loader = root.get_node_or_null("ModLoader")
	_check("mod_loader_available", _mod_loader != null)
	if _mod_loader == null:
		_finish()
		return
	_map_data_script = load("res://src/retail_slice/retail_map_data.gd")
	_battlefield_script = load("res://src/retail_slice/retail_fords_battlefield.gd")
	_check("map_scripts_parse", _map_data_script != null and _battlefield_script != null)
	if _map_data_script == null or _battlefield_script == null:
		_finish()
		return
	var content_root := OS.get_environment("OPENBFME_CONTENT").strip_edges()
	var pack_root: String = _mod_loader.resolve_pack_path(content_root, FIVE_MAPS_PACK_ID)
	_check("five_maps_pack_present", pack_root != "" and DirAccess.dir_exists_absolute(pack_root), pack_root)
	if pack_root == "":
		_finish()
		return

	for map_id in EXPECTED_MAPS:
		_run_map(map_id, EXPECTED_MAPS[map_id], pack_root)
	_finish()


func _run_map(map_id: String, expected: Dictionary, pack_root: String) -> void:
	var definition := _catalog_definition(pack_root, map_id)
	_check("%s catalog_definition" % map_id, not definition.is_empty())
	if definition.is_empty():
		return
	var map_data = _map_data_script.new()
	_check("%s map_data_loads" % map_id, bool(map_data.load_from_pack(pack_root, definition)), String(map_data.error))
	if not map_data.ready:
		return
	_check("%s terrain_exact" % map_id, int(map_data.width) == int(expected.width) and int(map_data.height) == int(expected.height) and int(map_data.terrain_texture_count) == int(expected.terrain_textures), "%dx%d t=%d" % [int(map_data.width), int(map_data.height), int(map_data.terrain_texture_count)])
	_check("%s objects_exact" % map_id, int(map_data.object_count) == int(expected.objects), str(map_data.object_count))
	_check("%s water_exact" % map_id, int(map_data.standing_water_count) == int(expected.standing_water) and int(map_data.river_count) == int(expected.rivers))
	_check("%s player_starts_exact" % map_id, int(map_data.player_start_count) == int(expected.player_starts))
	_check("%s ford_gates_absent" % map_id, map_data.ford_gates.size() == int(expected.ford_gates))
	_check("%s navigation_ready" % map_id, bool(map_data.navigation_ready) and int(map_data.navigation_walkable_count) > 0, "walkable=%d water_blocked=%d" % [int(map_data.navigation_walkable_count), int(map_data.navigation_water_blocked_count)])
	_check("%s route_between_starts" % map_id, _route_between_starts(map_data))
	var battlefield = _battlefield_script.new()
	root.add_child(battlefield)
	_check("%s battlefield_builds" % map_id, bool(battlefield.configure(map_data)), String(battlefield.error))
	if battlefield.source_driven:
		var expected_vertices := int(expected.width) * int(expected.height)
		var expected_surfaces := int(expected.standing_water) + int(expected.rivers)
		_check("%s terrain_mesh_exact" % map_id, int(battlefield.terrain_vertex_count) == expected_vertices, str(battlefield.terrain_vertex_count))
		_check("%s water_mesh_built" % map_id, int(battlefield.water_surface_count) == expected_surfaces and int(battlefield.water_triangle_count) > 0, "surfaces=%d" % int(battlefield.water_surface_count))
		_check("%s props_counted" % map_id, int(battlefield.unresolved_prop_placement_count) == int(map_data.unresolved_prop_placement_count) and int(battlefield.generic_prop_count) == map_data.generic_prop_placements.size(), "unresolved=%d generic=%d" % [int(battlefield.unresolved_prop_placement_count), int(battlefield.generic_prop_count)])
	root.remove_child(battlefield)
	battlefield.queue_free()


func _route_between_starts(map_data) -> bool:
	var player_one := Vector3(map_data.local_player_starts.get("Player_1_Start", Vector3.INF))
	var player_two := Vector3(map_data.local_player_starts.get("Player_2_Start", Vector3.INF))
	if player_one == Vector3.INF or player_two == Vector3.INF:
		return false
	# Start waypoints may sit on source-impassable or out-of-border cells
	# (Mount Doom's Player_1 is on a cliff edge, Rivendell's Player_2 below the
	# border); the sim always routes between navigation-snapped positions.
	var one: Vector2 = map_data.resolve_walkable_position(Vector2(player_one.x, player_one.z))
	var two: Vector2 = map_data.resolve_walkable_position(Vector2(player_two.x, player_two.z))
	if not map_data.is_navigation_walkable(map_data.local_to_grid_cell(one)) or not map_data.is_navigation_walkable(map_data.local_to_grid_cell(two)):
		return false
	var forward: Dictionary = map_data.query_route(one, two)
	var backward: Dictionary = map_data.query_route(two, one)
	return bool(forward.get("valid", false)) and bool(backward.get("valid", false))


func _catalog_definition(pack_root: String, map_id: String) -> Dictionary:
	var catalog := _read_bounded(pack_root, "data/maps.json", MAP_CATALOG_MAX_BYTES)
	if String(catalog.get("schema", "")) != "openbfme.map-catalog":
		return {}
	for row_value in catalog.get("maps", []) as Array:
		var row := row_value as Dictionary
		if row == null or String(row.get("id", "")) != map_id:
			continue
		var map_relative := String(row.get("map", ""))
		if map_relative == "" or not _mod_loader.is_safe_relative_path(map_relative):
			return {}
		var map_doc := _read_bounded(pack_root, map_relative, MAP_DOCUMENT_MAX_BYTES)
		if map_doc.is_empty() or String(map_doc.get("id", "")) != map_id:
			return {}
		var merged := row.duplicate(true)
		merged.merge(map_doc, true)
		merged["map"] = map_relative
		merged["_source"] = _mod_loader.resolve_pack_path(pack_root, map_relative)
		merged["_pack_root"] = pack_root
		return merged
	return {}


func _read_bounded(pack_root: String, relative: String, maximum_bytes: int) -> Dictionary:
	var path: String = _mod_loader.resolve_pack_path(pack_root, relative)
	if path == "" or not FileAccess.file_exists(path):
		return {}
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null or file.get_length() <= 0 or file.get_length() > maximum_bytes:
		return {}
	file.close()
	var raw: Variant = _mod_loader._read_json(path)
	return raw as Dictionary if typeof(raw) == TYPE_DICTIONARY else {}


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("RETAIL_MAP_DATA PASS %s" % name)
	else:
		failed += 1
		printerr("RETAIL_MAP_DATA FAIL %s %s" % [name, detail])


func _finish() -> void:
	print("RETAIL_MAP_DATA_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)
