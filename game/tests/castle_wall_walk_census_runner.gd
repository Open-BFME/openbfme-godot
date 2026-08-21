extends SceneTree
## Q62c rerunnable Minas Tirith wall-component and endpoint census.
##
## Point OPENBFME_CASTLE_WALL_CENSUS_PACK at a maps-pack digest directory.
## The Q56f scratch proof pack is the pre-republish oracle; after republish the
## published digest must produce the same named checks and endpoint hash.

const RunnerWatchdogScript = preload("res://tests/runner_watchdog.gd")

const MINAS_MAP_SLUG := "wor-minas-tirith"
const PROVEN_COMPONENT_COUNT := 22
const PROVEN_POCKET_SIZE := 7496
const PROVEN_LARGEST_COMPONENT_SIZE := 89391
const PROVEN_PORTAL_COUNT := 42
const PROVEN_ENDPOINT_SHA256 := "e8d7303ccc609aa894bb79b739ab235b16c0b861d94475ae29d3c948b0384a97"
const CARDINAL_NEIGHBORS: Array[Vector2i] = [Vector2i.LEFT, Vector2i.RIGHT, Vector2i.UP, Vector2i.DOWN]

var passed := 0
var failed := 0
var expected_checks := 1
var _runner_watchdog := RunnerWatchdogScript.new()


func _initialize() -> void:
	_runner_watchdog.start(self, "CASTLE_WALL_WALK_CENSUS_RUNNER")
	call_deferred("_run")


func _run() -> void:
	# Load after autoload initialization: retail_map_data.gd references the
	# project ModLoader singleton while validating mounted pack paths.
	var map_data_script := load("res://src/retail_slice/retail_map_data.gd") as GDScript
	var pack_root := OS.get_environment("OPENBFME_CASTLE_WALL_CENSUS_PACK").strip_edges()
	var map_slug := OS.get_environment("OPENBFME_CASTLE_WALL_CENSUS_MAP").strip_edges()
	if map_slug == "":
		map_slug = MINAS_MAP_SLUG
	var map_path := pack_root.path_join("maps/%s/map.json" % map_slug) if pack_root != "" else ""
	var definition: Dictionary = _read_dictionary(map_path)
	if not definition.is_empty():
		definition["map"] = "maps/%s/map.json" % map_slug
		definition["_source"] = map_path
		definition["_pack_root"] = pack_root
	var map = map_data_script.new() if map_data_script != null and map_data_script.can_instantiate() else null
	var started_ms := Time.get_ticks_msec()
	var loaded := map != null and not definition.is_empty() and bool(map.load_from_pack(pack_root, definition))
	var elapsed_ms := Time.get_ticks_msec() - started_ms
	print("CASTLE_WALL_WALK_CENSUS_PROFILE map=%s init_ms=%d pack=%s" % [map_slug, elapsed_ms, pack_root])
	_check("map_data_loads_%s" % map_slug, loaded, "path=%s error=%s" % [map_path, String(map.error) if map != null else "script unavailable"])
	if map_slug != MINAS_MAP_SLUG:
		_finish()
		return
	expected_checks = 6
	if not loaded:
		for name in [
			"minas_portal_count_is_q56f_proven",
			"minas_endpoint_set_is_canonical",
			"minas_ground_component_census_is_q56f_proven",
			"minas_authored_start_pocket_is_disconnected",
			"minas_pocket_has_zero_connected_portal_pairs",
		]:
			_check(name, false, "map unavailable")
		_finish()
		return

	var endpoint_lines := _canonical_endpoint_lines(map)
	var endpoint_bytes := "\n".join(endpoint_lines)
	var endpoint_sha256 := _sha256_text(endpoint_bytes)
	var portal_count := (map._walk_surface_portal_cells as Array).size()
	print("CASTLE_WALL_WALK_CENSUS_ENDPOINTS count=%d sha256=%s bytes=%d" % [portal_count, endpoint_sha256, endpoint_bytes.to_utf8_buffer().size()])
	_check(
		"minas_portal_count_is_q56f_proven",
		portal_count == PROVEN_PORTAL_COUNT,
		"portals=%d gaps=%s" % [portal_count, str(map.walk_surface_gap_receipts)]
	)
	_check(
		"minas_endpoint_set_is_canonical",
		endpoint_lines.size() == portal_count and endpoint_sha256 == PROVEN_ENDPOINT_SHA256,
		"lines=%d portals=%d sha256=%s" % [endpoint_lines.size(), portal_count, endpoint_sha256]
	)

	var starts := _authored_start_positions(map)
	var player_one: Vector2 = starts.get("Player_1_Start", Vector2.INF)
	var player_two: Vector2 = starts.get("Player_2_Start", Vector2.INF)
	var player_one_cell: Vector2i = map.local_to_grid_cell(player_one)
	var player_two_cell: Vector2i = map.local_to_grid_cell(player_two)
	var census := _ground_component_census(map, {
		"Player_1_Start": player_one_cell,
		"Player_2_Start": player_two_cell,
	})
	var watched_sizes: Dictionary = census.get("watched_sizes", {}) as Dictionary
	var player_one_size := int(watched_sizes.get("Player_1_Start", 0))
	var player_two_size := int(watched_sizes.get("Player_2_Start", 0))
	print(
		"CASTLE_WALL_WALK_CENSUS_COMPONENTS components=%d largest=%d player_1=%d player_2=%d" % [
			int(census.get("components", 0)),
			int(census.get("largest", 0)),
			player_one_size,
			player_two_size,
		]
	)
	_check(
		"minas_ground_component_census_is_q56f_proven",
		int(census.get("components", 0)) == PROVEN_COMPONENT_COUNT
		and int(census.get("largest", 0)) == PROVEN_LARGEST_COMPONENT_SIZE
		and player_one_size == PROVEN_POCKET_SIZE
		and player_two_size == PROVEN_LARGEST_COMPONENT_SIZE,
		str(census)
	)

	var ground: Dictionary = map.query_route(player_one, player_two)
	var layered: Dictionary = map.query_layered_bridge_route(player_one, player_two)
	print("CASTLE_WALL_WALK_CENSUS_POCKET ground=%s layered=%s" % [str(ground), str(layered)])
	_check(
		"minas_authored_start_pocket_is_disconnected",
		not bool(ground.get("valid", false)) and not bool(layered.get("valid", false)),
		"ground=%s layered=%s" % [str(ground), str(layered)]
	)
	_check(
		"minas_pocket_has_zero_connected_portal_pairs",
		int(layered.get("connected_pairs", -1)) == 0,
		str(layered)
	)
	_finish()


func _read_dictionary(path: String) -> Dictionary:
	if path == "" or not FileAccess.file_exists(path):
		return {}
	var parsed: Variant = JSON.parse_string(FileAccess.get_file_as_string(path))
	return parsed as Dictionary if typeof(parsed) == TYPE_DICTIONARY else {}


func _canonical_endpoint_lines(map) -> Array[String]:
	var lines: Array[String] = []
	for wall_value in map._walk_surface_portal_cells as Array:
		var wall := Vector2i(wall_value)
		var ground := Vector2i(map._walk_surface_ground_portal_by_wall.get(wall, wall))
		lines.append("%d,%d>%d,%d" % [wall.x, wall.y, ground.x, ground.y])
	lines.sort()
	return lines


func _sha256_text(value: String) -> String:
	var context := HashingContext.new()
	context.start(HashingContext.HASH_SHA256)
	context.update(value.to_utf8_buffer())
	return context.finish().hex_encode()


func _authored_start_positions(map) -> Dictionary:
	var result := {}
	for name in ["Player_1_Start", "Player_2_Start"]:
		var value: Variant = map.local_player_starts.get(name)
		if typeof(value) == TYPE_VECTOR3:
			var position := Vector3(value)
			result[name] = Vector2(position.x, position.z)
		elif typeof(value) == TYPE_VECTOR2:
			result[name] = Vector2(value)
	return result


func _ground_component_census(map, watched_cells: Dictionary) -> Dictionary:
	var minimum: Vector2i = map.navigation_grid_min
	var maximum: Vector2i = map.navigation_grid_max
	var region_width := maximum.x - minimum.x + 1
	var region_height := maximum.y - minimum.y + 1
	var visited := PackedByteArray()
	visited.resize(region_width * region_height)
	visited.fill(0)
	var component_count := 0
	var largest := 0
	var watched_sizes := {}
	for grid_y in range(minimum.y, maximum.y + 1):
		for grid_x in range(minimum.x, maximum.x + 1):
			var seed := Vector2i(grid_x, grid_y)
			var seed_index := _region_index(seed, minimum, region_width)
			if visited[seed_index] != 0:
				continue
			visited[seed_index] = 1
			if not map.is_navigation_walkable(seed):
				continue
			var queue: Array[Vector2i] = [seed]
			var cursor := 0
			var size := 0
			var component_watches: Array[String] = []
			while cursor < queue.size():
				var cell := queue[cursor]
				cursor += 1
				size += 1
				for watch_name_value in watched_cells.keys():
					var watch_name := String(watch_name_value)
					if Vector2i(watched_cells[watch_name_value]) == cell:
						component_watches.append(watch_name)
				for offset in CARDINAL_NEIGHBORS:
					var neighbor := cell + offset
					if neighbor.x < minimum.x or neighbor.x > maximum.x or neighbor.y < minimum.y or neighbor.y > maximum.y:
						continue
					var neighbor_index := _region_index(neighbor, minimum, region_width)
					if visited[neighbor_index] != 0:
						continue
					visited[neighbor_index] = 1
					if map.is_navigation_walkable(neighbor):
						queue.append(neighbor)
			component_count += 1
			largest = maxi(largest, size)
			for watch_name in component_watches:
				watched_sizes[watch_name] = size
	return {
		"components": component_count,
		"largest": largest,
		"watched_sizes": watched_sizes,
	}


func _region_index(cell: Vector2i, minimum: Vector2i, region_width: int) -> int:
	return (cell.y - minimum.y) * region_width + cell.x - minimum.x


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("CASTLE_WALL_WALK_CENSUS PASS %s" % name)
	else:
		failed += 1
		printerr("CASTLE_WALL_WALK_CENSUS FAIL %s%s" % [name, " (%s)" % detail if detail != "" else ""])


func _finish() -> void:
	if passed + failed != expected_checks:
		failed += 1
		printerr("CASTLE_WALL_WALK_CENSUS FAIL check_count expected=%d actual=%d" % [expected_checks, passed + failed - 1])
	print("CASTLE_WALL_WALK_CENSUS_RESULT passed=%d failed=%d" % [passed, failed])
	_runner_watchdog.stop()
	quit(0 if failed == 0 else 1)
