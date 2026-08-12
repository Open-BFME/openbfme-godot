extends SceneTree
## Open-field routing must be a 1-point exact destination, not a cell-center
## staircase. A path that must go around one blocking disc must not wind the
## long way when the short side is open.
##
## RetailMapData.query_route previously always ran AStarGrid2D +
## _compress_route_points (direction-change cell centers). Combined with
## infantry MaxTurnWithoutReform=45 that looks like circling.

const EXPECTED_CHECKS := 10
const EPSILON := 0.0001
const FORD_NAMES: Array[String] = ["ford1", "ford2", "ford3"]

var passed := 0
var failed := 0
var _map_data_script

const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
var _runner_watchdog := RunnerWatchdogScript.new()


func _initialize() -> void:
	_runner_watchdog.start(self, "OPEN_FIELD_ROUTE_RUNNER")
	call_deferred("_run")


func _run() -> void:
	_map_data_script = load("res://src/retail_slice/retail_map_data.gd")
	if _map_data_script == null:
		printerr("OPEN_FIELD_ROUTE FAIL map_script_load")
		failed += 1
		_finish()
		return
	_test_open_field_is_one_point()
	_test_last_point_is_exact_destination()
	_test_blocked_disc_takes_short_side()
	_test_string_pull_collapses_visible_waypoints()
	_test_water_without_ford_is_not_straight()
	_finish()


func _make_map(width: int = 48, height: int = 48) -> RefCounted:
	## Identity-ish transform: local (gx * 10, -gy * 10) <-> cell (gx, gy).
	var map = _map_data_script.new()
	map.width = width
	map.height = height
	map.horizontal_scale = 10.0
	map.border_width = 0
	map.reference_elevation = 0.0
	map.local_transform_scale = 1.0
	map.local_transform_origin = Vector2.ZERO
	map.local_axis_x = Vector2.RIGHT
	map.local_axis_z = Vector2.DOWN
	map.playable_grid_min = Vector2i.ZERO
	map.playable_grid_max = Vector2i(width - 1, height - 1)
	map.navigation_grid_min = Vector2i.ZERO
	map.navigation_grid_max = Vector2i(width - 1, height - 1)
	map._water_cells.resize(width * height)
	map._water_cells.fill(0)
	map._ford_corridor_cells.clear()
	for ford_name in FORD_NAMES:
		var mask := PackedByteArray()
		mask.resize(width * height)
		mask.fill(0)
		map._ford_corridor_cells[ford_name] = mask
	var grid := AStarGrid2D.new()
	grid.region = Rect2i(Vector2i.ZERO, Vector2i(width, height))
	grid.cell_size = Vector2.ONE
	grid.diagonal_mode = AStarGrid2D.DIAGONAL_MODE_NEVER
	grid.default_compute_heuristic = AStarGrid2D.HEURISTIC_MANHATTAN
	grid.default_estimate_heuristic = AStarGrid2D.HEURISTIC_MANHATTAN
	grid.update()
	map._navigation_grid = grid
	map.navigation_ready = true
	map.navigation_walkable_count = width * height
	return map


func _cell_local(map, cell: Vector2i) -> Vector2:
	return map.grid_to_local_horizontal(cell)


func _block_rect(map, min_cell: Vector2i, max_cell: Vector2i) -> void:
	for gy in range(min_cell.y, max_cell.y + 1):
		for gx in range(min_cell.x, max_cell.x + 1):
			map._navigation_grid.set_point_solid(Vector2i(gx, gy), true)


func _mark_water_column(map, x: int, y0: int, y1: int, ford_name: String = "") -> void:
	for gy in range(y0, y1 + 1):
		var cell := Vector2i(x, gy)
		map._water_cells[gy * map.width + x] = 1
		if ford_name == "":
			map._navigation_grid.set_point_solid(cell, true)
		else:
			var mask: PackedByteArray = map._ford_corridor_cells[ford_name]
			mask[gy * map.width + x] = 1
			map._ford_corridor_cells[ford_name] = mask


func _polyline_signed_turn(points: Array) -> float:
	## Accumulated heading change along the polyline, degrees. A long-way
	## orbit around a disc is well over 180; a short-side slide is not.
	if points.size() < 2:
		return 0.0
	var total := 0.0
	var prev_heading := INF
	for index in range(1, points.size()):
		var delta: Vector2 = Vector2(points[index]) - Vector2(points[index - 1])
		if delta.length_squared() <= 0.000001:
			continue
		var heading := rad_to_deg(delta.angle())
		if prev_heading != INF:
			var step := heading - prev_heading
			while step > 180.0:
				step -= 360.0
			while step < -180.0:
				step += 360.0
			total += step
		prev_heading = heading
	return total


func _test_open_field_is_one_point() -> void:
	var map = _make_map()
	var origin := _cell_local(map, Vector2i(6, 8)) + Vector2(1.7, -2.3)
	var dest := _cell_local(map, Vector2i(31, 22)) + Vector2(-2.4, 1.1)
	var result: Dictionary = map.query_route(origin, dest)
	var points: Array = result.get("points", [])
	_check("open_field_route_valid", bool(result.get("valid", false)), str(result.get("reason", "")))
	_check(
		"open_field_is_one_point",
		points.size() == 1,
		"points=%d cells=%d" % [points.size(), (result.get("cells", []) as Array).size()]
	)
	var exact := points.size() == 1 and Vector2(points[0]).is_equal_approx(dest)
	_check("open_field_point_is_exact_destination", exact, str(points))


func _test_last_point_is_exact_destination() -> void:
	var map = _make_map()
	_block_rect(map, Vector2i(18, 4), Vector2i(20, 28))
	var origin := _cell_local(map, Vector2i(8, 16))
	var dest := _cell_local(map, Vector2i(30, 17)) + Vector2(1.25, -0.75)
	var result: Dictionary = map.query_route(origin, dest)
	var points: Array = result.get("points", [])
	_check("blocked_route_valid", bool(result.get("valid", false)), str(result.get("reason", "")))
	var last_exact := (
		not points.is_empty()
		and Vector2(points[points.size() - 1]).is_equal_approx(dest)
	)
	_check("blocked_route_last_point_is_exact", last_exact, str(points))


func _test_blocked_disc_takes_short_side() -> void:
	## Wall closer to the south edge. Origin and dest sit north of the wall's
	## midpoint, so the short way is around the north tip. A long-way south
	## orbit exceeds 180° of signed turn.
	var map = _make_map()
	_block_rect(map, Vector2i(22, 8), Vector2i(24, 36))
	var origin := _cell_local(map, Vector2i(10, 10))
	var dest := _cell_local(map, Vector2i(36, 10))
	var result: Dictionary = map.query_route(origin, dest)
	var points: Array = result.get("points", [])
	_check("short_side_route_valid", bool(result.get("valid", false)) and points.size() >= 1, str(result))
	var max_south := origin.y
	for point_value in points:
		var point := Vector2(point_value)
		if point.y > max_south:
			max_south = point.y
	# Local y = -grid_y * 10, so "south" (higher grid y) is more negative local y.
	# The north tip of the wall is grid y=8 -> local y=-80. Short-side waypoints
	# stay at or above that (local y >= -90). A long-way path drops toward
	# grid y=36 -> local y=-360.
	var wall_south_local := _cell_local(map, Vector2i(23, 36)).y
	var took_long_way := false
	for point_value in points:
		if Vector2(point_value).y < wall_south_local + 40.0:
			took_long_way = true
	var turn := _polyline_signed_turn(_route_polyline(origin, points))
	_check(
		"blocked_disc_does_not_take_long_way",
		not took_long_way and absf(turn) <= 180.0,
		"took_long_way=%s turn=%.1f points=%s" % [took_long_way, turn, points]
	)


func _test_string_pull_collapses_visible_waypoints() -> void:
	var map = _make_map()
	_block_rect(map, Vector2i(20, 12), Vector2i(22, 30))
	var origin := _cell_local(map, Vector2i(8, 20))
	var dest := _cell_local(map, Vector2i(34, 20))
	var result: Dictionary = map.query_route(origin, dest)
	var points: Array = result.get("points", [])
	var cells: Array = result.get("cells", [])
	# A* + compress on DIAGONAL_MODE_NEVER produces a staircase of cell
	# centers (typically 8+). String-pull must collapse mutually-visible
	# corners to a handful of funnel vertices plus the exact dest.
	_check(
		"string_pull_collapses_visible_waypoints",
		bool(result.get("valid", false)) and points.size() >= 1 and points.size() <= 4,
		"points=%d cells=%d" % [points.size(), cells.size()]
	)


func _test_water_without_ford_is_not_straight() -> void:
	var map = _make_map()
	_mark_water_column(map, 20, 0, 47, "")
	var origin := _cell_local(map, Vector2i(8, 20))
	var dest := _cell_local(map, Vector2i(32, 20))
	var result: Dictionary = map.query_route(origin, dest)
	var points: Array = result.get("points", [])
	# Straight LOS crosses non-ford water, so the route must not be the
	# single destination point (either invalid, or a detour).
	var straight := bool(result.get("valid", false)) and points.size() == 1
	_check("water_without_ford_is_not_straight_los", not straight, "result=%s" % result)

	var ford_map = _make_map()
	_mark_water_column(ford_map, 20, 0, 47, "ford1")
	var ford_result: Dictionary = ford_map.query_route(origin, dest)
	var ford_points: Array = ford_result.get("points", [])
	_check(
		"named_ford_corridor_allows_straight_los",
		bool(ford_result.get("valid", false))
			and ford_points.size() == 1
			and Vector2(ford_points[0]).is_equal_approx(dest),
		"result=%s" % ford_result
	)


func _route_polyline(origin: Vector2, points: Array) -> Array:
	var poly: Array = [origin]
	for point_value in points:
		poly.append(Vector2(point_value))
	return poly


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("OPEN_FIELD_ROUTE PASS %s" % name)
	else:
		failed += 1
		printerr("OPEN_FIELD_ROUTE FAIL %s%s" % [name, "" if detail == "" else " (%s)" % detail])


func _finish() -> void:
	if passed + failed != EXPECTED_CHECKS:
		failed += 1
		printerr(
			"OPEN_FIELD_ROUTE FAIL expected_checks (passed=%d failed=%d expected=%d)"
			% [passed, failed - 1, EXPECTED_CHECKS]
		)
	print("OPEN_FIELD_ROUTE_RESULT passed=%d failed=%d" % [passed, failed])
	_runner_watchdog.stop()
	quit(0 if failed == 0 else 1)
