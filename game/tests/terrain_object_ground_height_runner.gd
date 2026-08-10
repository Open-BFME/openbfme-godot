extends SceneTree
## Pins the SAGE terrain-grid origin against the retail engine's own contract.
##
## THE BUG THIS EXISTS FOR (user report, 2026-08-10): fortress wall segments
## floated in the air above the ground on Amon Sul Fortress. Root cause: SAGE
## anchors world XY at the INNER (playable) map corner, so heightmap grid index
## (BorderWidth, BorderWidth) - not (0, 0) - sits at world (0, 0). Every cooked
## record (objects, waypoints, water, roads) is stored in that world frame, and
## the importer resolves each object's ground Z with the border-shifted sample
## (importer/openbfme_importer/sage_map.py `_HeightMap.sample_world_z`). The
## runtime terrain grid was NOT shifted, so the whole ground surface sat
## border_width * horizontal_scale source units away from every object it was
## supposed to carry - 400 units on Amon Sul (border 40), which on a hill fort
## is the difference between a wall on the rampart and a wall over the valley.
##
## ORACLE (external, never this repo's own runtime):
##   OpenSAGE src/OpenSage.Game/Terrain/HeightMap.cs
##     ConvertWorldCoordinates: p = p / HorizontalScale + BorderWidth
##     GetPosition(x, y):       ((x - BorderWidth) * 10, (y - BorderWidth) * 10, h)
##     GetHeight(float, float): bilinear over the four surrounding samples
##   OpenSAGE src/OpenSage.Game/Logic/Object/GameObject.cs
##     height = HeightMap.GetHeight(position.X, position.Y) + mapObject.Position.Z
##   i.e. an authored object's Z float is an OFFSET ABOVE THE TERRAIN.
## Leg B re-derives nothing: it compares the runtime sample against the ground Z
## the importer already cooked into each real map's objects.json.
##
## Leg A (synthetic, always runs) has teeth on its own: revert the border shift
## in retail_map_data.gd and every assertion below goes red.

const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")

const SYNTHETIC_WIDTH := 12
const SYNTHETIC_HEIGHT := 10
const SYNTHETIC_BORDER := 3
const HORIZONTAL_SCALE := 10.0
const VERTICAL_SCALE := 0.0390625
const MAX_REAL_MAPS := 8
## Checked first when present, ahead of the alphabetical fill: Amon Sul Fortress
## is the user's floating-wall repro (border 40, hill fort) and Fords of Isen II
## is the long-standing slice baseline (border 20, the smallest displacement in
## the catalog, which is why it looked almost right).
const PRIORITY_MAP_SLUGS: Array[String] = ["amon-sul-fortress", "fords-of-isen-ii"]
## Cooked ground Z is float32 in JSON; the runtime samples float64 over the same
## uint16 grid. 0.01 source units is ~1/16000 of a typical map elevation range,
## and ~0.00026 units after the battlefield transform.
const GROUND_TOLERANCE := 0.01

var passed := 0
var failed := 0
var skipped := 0
var _map_data_script
var _runner_watchdog := RunnerWatchdogScript.new()


func _initialize() -> void:
	_runner_watchdog.start(self, "TERRAIN_GROUND")
	call_deferred("_run")


func _check(name: String, ok: bool, detail: String = "") -> void:
	if ok:
		passed += 1
		print("TERRAIN_GROUND PASS %s" % name)
	else:
		failed += 1
		print("TERRAIN_GROUND FAIL %s | %s" % [name, detail])


func _skip(name: String, detail: String) -> void:
	skipped += 1
	print("TERRAIN_GROUND SKIP %s | %s" % [name, detail])


func _run() -> void:
	# Loaded at run time, not preloaded: retail_map_data.gd references the
	# ModLoader autoload, which is not resolvable while the main-loop script
	# itself is being compiled.
	_map_data_script = load("res://src/retail_slice/retail_map_data.gd")
	_check("map_data_script_parses", _map_data_script != null)
	if _map_data_script != null:
		_run_synthetic_leg()
		_run_real_map_leg()
	print("TERRAIN_GROUND SUMMARY passed=%d failed=%d skipped=%d" % [passed, failed, skipped])
	_runner_watchdog.stop()
	quit(1 if failed > 0 else 0)


# --- Leg A: synthetic grid, identity local transform -------------------------


func _synthetic_raw(grid_x: int, grid_y: int) -> int:
	# A ramp in both axes so any grid misalignment changes the sampled height.
	return grid_x * 137 + grid_y * 61


func _build_synthetic():
	var map_data = _map_data_script.new()
	map_data.width = SYNTHETIC_WIDTH
	map_data.height = SYNTHETIC_HEIGHT
	map_data.border_width = SYNTHETIC_BORDER
	map_data.horizontal_scale = HORIZONTAL_SCALE
	map_data.vertical_scale = VERTICAL_SCALE
	map_data.playable_grid_min = Vector2i(SYNTHETIC_BORDER, SYNTHETIC_BORDER)
	map_data.playable_grid_max = Vector2i(SYNTHETIC_WIDTH - SYNTHETIC_BORDER, SYNTHETIC_HEIGHT - SYNTHETIC_BORDER)
	var samples := PackedByteArray()
	samples.resize(SYNTHETIC_WIDTH * SYNTHETIC_HEIGHT * 2)
	for grid_y in range(SYNTHETIC_HEIGHT):
		for grid_x in range(SYNTHETIC_WIDTH):
			var raw := _synthetic_raw(grid_x, grid_y)
			var offset := (grid_y * SYNTHETIC_WIDTH + grid_x) * 2
			samples[offset] = raw & 0xFF
			samples[offset + 1] = (raw >> 8) & 0xFF
	map_data.height_samples = samples
	_make_local_identity(map_data)
	return map_data


func _make_local_identity(map_data) -> void:
	# With unit scale, zero origin and the default axes, local space is exactly
	# the cooked Godot world space: source_to_local() and
	# local_to_source_horizontal() both become the identity. That keeps this
	# runner pointed at the grid mapping under test instead of the unrelated
	# player-start-derived battlefield transform.
	map_data.local_transform_scale = 1.0
	map_data.local_transform_origin = Vector2.ZERO
	map_data.local_axis_x = Vector2.RIGHT
	map_data.local_axis_z = Vector2.DOWN
	map_data.reference_elevation = 0.0


## OpenSAGE HeightMap.GetHeight(float x, float y), transcribed.
func _oracle_ground(map_data, world_x: float, world_y: float) -> float:
	var border := float(map_data.border_width)
	var grid_x := clampf(world_x / HORIZONTAL_SCALE + border, 0.0, float(map_data.width - 1))
	var grid_y := clampf(world_y / HORIZONTAL_SCALE + border, 0.0, float(map_data.height - 1))
	var x0 := int(floor(grid_x))
	var y0 := int(floor(grid_y))
	var x1 := mini(x0 + 1, int(map_data.width) - 1)
	var y1 := mini(y0 + 1, int(map_data.height) - 1)
	var fx := grid_x - float(x0)
	var fy := grid_y - float(y0)
	var low := float(map_data.height_raw_at(x0, y0)) * (1.0 - fx) + float(map_data.height_raw_at(x1, y0)) * fx
	var high := float(map_data.height_raw_at(x0, y1)) * (1.0 - fx) + float(map_data.height_raw_at(x1, y1)) * fx
	return (low * (1.0 - fy) + high * fy) * float(map_data.vertical_scale)


func _run_synthetic_leg() -> void:
	var map_data = _build_synthetic()

	# 1. Grid index (BorderWidth, BorderWidth) is world origin, not grid (0,0).
	var inner: Vector3 = map_data.terrain_local_at(SYNTHETIC_BORDER, SYNTHETIC_BORDER)
	_check(
		"inner_corner_grid_cell_is_world_origin",
		absf(inner.x) < 0.0001 and absf(inner.z) < 0.0001,
		"terrain_local_at(%d,%d)=%s" % [SYNTHETIC_BORDER, SYNTHETIC_BORDER, str(inner)]
	)
	_check(
		"inner_corner_carries_its_own_elevation",
		absf(inner.y - float(_synthetic_raw(SYNTHETIC_BORDER, SYNTHETIC_BORDER)) * VERTICAL_SCALE) < 0.0001,
		"y=%f" % inner.y
	)

	# 2. Grid index zero sits at negative world coordinates (the border ring).
	var outer: Vector3 = map_data.terrain_local_at(0, 0)
	_check(
		"grid_zero_is_outside_the_playable_corner",
		absf(outer.x + float(SYNTHETIC_BORDER) * HORIZONTAL_SCALE) < 0.0001
		and absf(outer.z - float(SYNTHETIC_BORDER) * HORIZONTAL_SCALE) < 0.0001,
		"terrain_local_at(0,0)=%s" % str(outer)
	)

	# 3. Round trips both ways.
	var grid_of_origin: Vector2 = map_data.local_to_grid_float(Vector2.ZERO)
	_check(
		"world_origin_maps_to_the_border_grid_cell",
		grid_of_origin.is_equal_approx(Vector2(float(SYNTHETIC_BORDER), float(SYNTHETIC_BORDER))),
		"grid=%s" % str(grid_of_origin)
	)
	var local_of_cell: Vector2 = map_data.grid_to_local_horizontal(Vector2i(SYNTHETIC_BORDER + 2, SYNTHETIC_BORDER + 1))
	_check(
		"grid_to_local_round_trips_through_local_to_grid",
		Vector2(map_data.local_to_grid_float(local_of_cell)).is_equal_approx(Vector2(float(SYNTHETIC_BORDER + 2), float(SYNTHETIC_BORDER + 1))),
		"local=%s" % str(local_of_cell)
	)

	# 4. The ground sample agrees with the retail bilinear oracle, including at
	#    fractional positions between grid vertices.
	var probes := [
		Vector2(0.0, 0.0),
		Vector2(15.0, 5.0),
		Vector2(37.5, 22.25),
		Vector2(-10.0, -10.0),
		Vector2(float(SYNTHETIC_WIDTH - 2 * SYNTHETIC_BORDER) * HORIZONTAL_SCALE, float(SYNTHETIC_HEIGHT - 2 * SYNTHETIC_BORDER) * HORIZONTAL_SCALE),
	]
	var worst := 0.0
	var worst_probe := Vector2.ZERO
	for probe_value in probes:
		var probe: Vector2 = probe_value
		# Cooked Godot horizontal space is (sage.x, -sage.y).
		var sampled := float(map_data.local_ground_height(Vector2(probe.x, -probe.y)))
		var expected := _oracle_ground(map_data, probe.x, probe.y)
		var delta := absf(sampled - expected)
		if delta > worst:
			worst = delta
			worst_probe = probe
	_check(
		"ground_sample_matches_the_retail_bilinear_oracle",
		worst < 0.0001,
		"worst delta=%f at sage%s" % [worst, str(worst_probe)]
	)

	# 5. The playable outline starts at world zero and spans the declared extent.
	map_data.playable_world_extent = Vector2(
		float(SYNTHETIC_WIDTH - 2 * SYNTHETIC_BORDER) * HORIZONTAL_SCALE,
		float(SYNTHETIC_HEIGHT - 2 * SYNTHETIC_BORDER) * HORIZONTAL_SCALE
	)
	map_data._build_map_outline()
	var bounds: Rect2 = map_data.local_bounds
	_check(
		"playable_outline_spans_world_zero_to_the_declared_extent",
		absf(bounds.position.x) < 0.0001
		and absf(bounds.size.x - Vector2(map_data.playable_world_extent).x) < 0.0001
		and absf(bounds.size.y - Vector2(map_data.playable_world_extent).y) < 0.0001,
		"bounds=%s extent=%s" % [str(bounds), str(map_data.playable_world_extent)]
	)


# --- Leg B: real cooked maps ------------------------------------------------


func _run_real_map_leg() -> void:
	var map_dirs := _discover_cooked_maps()
	if map_dirs.is_empty():
		_skip("real_cooked_maps", "no cooked map directories under OPENBFME_CONTENT")
		return
	for map_dir_value in map_dirs:
		_check_real_map(String(map_dir_value))


func _discover_cooked_maps() -> Array:
	var content_root := OS.get_environment("OPENBFME_CONTENT").strip_edges()
	if content_root == "" or not DirAccess.dir_exists_absolute(content_root):
		return []
	# The same map slug is cooked into several packs; one copy per slug is enough
	# and keeps the reported check names unique.
	var by_slug: Dictionary = {}
	for pack_name in _sorted_dirs(content_root):
		var pack_root: String = content_root.path_join(String(pack_name))
		for build_name in _sorted_dirs(pack_root):
			var maps_root: String = pack_root.path_join(String(build_name)).path_join("maps")
			if not DirAccess.dir_exists_absolute(maps_root):
				continue
			for map_name in _sorted_dirs(maps_root):
				var slug := String(map_name)
				if by_slug.has(slug):
					continue
				var map_dir: String = maps_root.path_join(slug)
				if FileAccess.file_exists(map_dir.path_join("terrain.json")) and FileAccess.file_exists(map_dir.path_join("objects.json")):
					by_slug[slug] = map_dir
	var found: Array = []
	for priority_slug in PRIORITY_MAP_SLUGS:
		if by_slug.has(priority_slug):
			found.append(by_slug[priority_slug])
	for slug in by_slug:
		if found.size() >= MAX_REAL_MAPS:
			break
		if not PRIORITY_MAP_SLUGS.has(slug):
			found.append(by_slug[slug])
	return found


func _sorted_dirs(root: String) -> Array:
	var names := Array(DirAccess.get_directories_at(root))
	names.sort()
	return names


func _read_json(path: String) -> Dictionary:
	if not FileAccess.file_exists(path):
		return {}
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null:
		return {}
	var text := file.get_as_text()
	file.close()
	var parsed: Variant = JSON.parse_string(text)
	return parsed if parsed is Dictionary else {}


func _check_real_map(map_dir: String) -> void:
	var label := map_dir.get_file()
	var terrain := _read_json(map_dir.path_join("terrain.json"))
	var objects_document := _read_json(map_dir.path_join("objects.json"))
	var height_section: Dictionary = terrain.get("height", {})
	var heightmap_section: Dictionary = height_section.get("heightmap", {})
	var grid_width := int(height_section.get("width", 0))
	var grid_height := int(height_section.get("height", 0))
	var heightmap_path := map_dir.path_join(String(heightmap_section.get("path", "")))
	if grid_width <= 1 or grid_height <= 1 or not FileAccess.file_exists(heightmap_path):
		_skip("real_map_%s" % label, "cooked terrain is unreadable")
		return
	var samples := FileAccess.get_file_as_bytes(heightmap_path)
	if samples.size() != grid_width * grid_height * 2:
		_skip("real_map_%s" % label, "heightmap byte count %d" % samples.size())
		return

	var map_data = _map_data_script.new()
	map_data.width = grid_width
	map_data.height = grid_height
	map_data.border_width = int(height_section.get("borderWidth", 0))
	map_data.horizontal_scale = float(height_section.get("horizontalScale", 0.0))
	map_data.vertical_scale = float(height_section.get("verticalScale", 0.0))
	map_data.height_samples = samples
	_make_local_identity(map_data)
	if float(map_data.horizontal_scale) <= 0.0 or float(map_data.vertical_scale) <= 0.0 or int(map_data.border_width) <= 0:
		_skip("real_map_%s" % label, "cooked terrain metadata is incomplete")
		return

	var objects: Array = objects_document.get("objects", [])
	var compared := 0
	var worst := 0.0
	var worst_type := ""
	var worst_position := Vector3.ZERO
	for object_value in objects:
		var object: Dictionary = object_value
		var sage_position: Array = object.get("sagePosition", [])
		var godot_position: Array = object.get("godotPosition", [])
		if sage_position.size() != 3 or godot_position.size() != 3:
			continue
		if String(object.get("typeName", "")) == "*Waypoints/Waypoint":
			continue
		# The retail contract under test: effective world Y is the terrain sample
		# at the object's XZ plus the authored Z offset. Objects authored flush to
		# the ground (offset 0) must land exactly on the terrain surface.
		var authored_offset := float(sage_position[2])
		var expected_ground := float(godot_position[1]) - authored_offset
		var sampled := float(map_data.local_ground_height(Vector2(float(godot_position[0]), float(godot_position[2]))))
		var delta := absf(sampled - expected_ground)
		compared += 1
		if delta > worst:
			worst = delta
			worst_type = String(object.get("typeName", ""))
			worst_position = Vector3(float(sage_position[0]), float(sage_position[1]), authored_offset)
	if compared == 0:
		_skip("real_map_%s" % label, "no comparable object placements")
		return
	_check(
		"real_map_%s_objects_sit_on_the_terrain" % label,
		worst <= GROUND_TOLERANCE,
		"compared=%d worst=%.4f source units on %s at sage%s (border=%d)" % [
			compared, worst, worst_type, str(worst_position), int(map_data.border_width)
		]
	)
