extends SceneTree
## Q51 failing-first/runtime proof for retail walkable wall tops.
##
## Retail authors wall decks and their only legal entries independently:
##   helmsdeepbuildings.ini:1315-1316  WallBoundsMesh=P1, RampMesh1=P2
##   helmsdeepbuildings.ini:1469-1470  WallBoundsMesh=P1, RampMesh1=P2
##   helmsdeepbuildings.ini:1859-1861  WallBoundsMesh=P1, RampMesh1=P2,
##                                    RampMesh2=P3
## The importer projects those roles into fixtures.json. This runner uses a
## small cell projection of the same authored topology so it tests the runtime
## layer deterministically without deriving an oracle from the implementation.

const SimScript = preload("res://src/retail_slice/retail_slice_sim.gd")
const RunnerWatchdogScript = preload("res://tests/runner_watchdog.gd")

const EXPECTED_CHECKS := 32
const WIDTH := 24
const HEIGHT := 16
const FORD_NAMES: Array[String] = ["ford1", "ford2", "ford3"]

var passed := 0
var failed := 0
var _runner_watchdog := RunnerWatchdogScript.new()
var _map_data_script: GDScript


func _initialize() -> void:
	_runner_watchdog.start(self, "CASTLE_WALL_WALK_RUNNER")
	call_deferred("_run")


func _run() -> void:
	_map_data_script = load("res://src/retail_slice/retail_map_data.gd") as GDScript
	if _map_data_script == null or not _map_data_script.can_instantiate():
		_check("walk_surface_layer_installed", false, "retail_map_data.gd did not load")
		for _index in range(EXPECTED_CHECKS - 1):
			_check("runtime_unavailable_%d" % _index, false)
		_finish()
		return
	var map = _make_map()
	var installed := false
	if map.has_method("install_walk_surface_cells_for_test"):
		installed = bool(map.call("install_walk_surface_cells_for_test", _authored_surface_projection()))
	_check("walk_surface_layer_installed", installed, "runtime does not consume fixture walk surfaces")

	var sim = _make_sim(map, _local(map, Vector2i(2, 7)))
	var row: Dictionary = sim.entities[1]
	var selected: Array[int] = [1]
	var deck_destination := _local(map, Vector2i(12, 7))
	var accepted: int = sim.issue_move(selected, deck_destination)
	_check("ordered_onto_deck_order_accepted", accepted == 1, sim.last_route_rejection)
	_check(
		"ordered_onto_deck_route_transits_ramp",
		(row.get("route_surface_roles", []) as Array).has("ramp"),
		str(row.get("route_surface_roles", []))
	)
	_run_route(sim, row)
	_check(
		"ordered_onto_deck_arrives",
		Vector2(row.get("position", Vector2.INF)).is_equal_approx(deck_destination),
		"position=%s rejection=%s" % [row.get("position"), sim.last_route_rejection]
	)
	_check("ordered_onto_deck_finishes_on_deck_layer", String(row.get("pathing_layer", "ground")) == "deck", str(row.get("pathing_layer", "ground")))

	var face_sim = _make_sim(map, _local(map, Vector2i(9, 10)))
	var face_destination := _local(map, Vector2i(11, 12))
	var face_accepted: int = face_sim.issue_move(selected, face_destination)
	_check("wall_face_ascent_refused", face_accepted == 0, "accepted=%d" % face_accepted)
	_check("wall_face_ascent_named_reason", face_sim.last_route_rejection == "wall-face-ascent-refused", face_sim.last_route_rejection)

	var disconnected_destination := _local(map, Vector2i(21, 7))
	var disconnected_accepted: int = sim.issue_move(selected, disconnected_destination)
	_check("deck_to_disconnected_deck_refused", disconnected_accepted == 0, "accepted=%d" % disconnected_accepted)
	_check("deck_to_disconnected_deck_named_reason", sim.last_route_rejection == "disconnected-wall-deck", sim.last_route_rejection)

	var ground_destination := _local(map, Vector2i(2, 7))
	var down_accepted: int = sim.issue_move(selected, ground_destination)
	_check("deck_ordered_back_down_accepted", down_accepted == 1, sim.last_route_rejection)
	_check("deck_ordered_back_down_transits_ramp", (row.get("route_surface_roles", []) as Array).has("ramp"), str(row.get("route_surface_roles", [])))
	_run_route(sim, row)
	_check(
		"deck_ordered_back_down_arrives_on_ground",
		Vector2(row.get("position", Vector2.INF)).is_equal_approx(ground_destination)
		and String(row.get("pathing_layer", "ground")) == "ground",
		"position=%s layer=%s" % [row.get("position"), row.get("pathing_layer", "ground")]
	)

	_test_ground_pockets_bridge_only_through_ramps()
	_test_failed_bridge_component_pair_cache()
	_test_source_component_without_portal_is_budgeted()
	_test_ai_failed_order_backoff()
	_test_distinct_ground_anchor_derivation()
	_test_low_endpoint_pair_narrowing()
	_test_authored_reach_admits_full_mesh_diagonal()
	_test_ground_search_is_bounded_to_authored_reach()
	_test_selected_erebor_vertical_slice()

	_finish()


func _make_map():
	var map = _map_data_script.new()
	map.width = WIDTH
	map.height = HEIGHT
	map.horizontal_scale = 1.0
	map.border_width = 0
	map.reference_elevation = 0.0
	map.local_transform_scale = 1.0
	map.local_transform_origin = Vector2.ZERO
	map.local_axis_x = Vector2.RIGHT
	map.local_axis_z = Vector2.DOWN
	map.playable_grid_min = Vector2i.ZERO
	map.playable_grid_max = Vector2i(WIDTH - 1, HEIGHT - 1)
	map.navigation_grid_min = Vector2i.ZERO
	map.navigation_grid_max = Vector2i(WIDTH - 1, HEIGHT - 1)
	map._water_cells.resize(WIDTH * HEIGHT)
	map._water_cells.fill(0)
	map._ford_corridor_cells.clear()
	for ford_name in FORD_NAMES:
		var mask := PackedByteArray()
		mask.resize(WIDTH * HEIGHT)
		mask.fill(0)
		map._ford_corridor_cells[ford_name] = mask
	var grid := AStarGrid2D.new()
	grid.region = Rect2i(Vector2i.ZERO, Vector2i(WIDTH, HEIGHT))
	grid.cell_size = Vector2.ONE
	grid.diagonal_mode = AStarGrid2D.DIAGONAL_MODE_NEVER
	grid.default_compute_heuristic = AStarGrid2D.HEURISTIC_MANHATTAN
	grid.default_estimate_heuristic = AStarGrid2D.HEURISTIC_MANHATTAN
	grid.update()
	map._navigation_grid = grid
	map.navigation_ready = true
	map.navigation_walkable_count = WIDTH * HEIGHT
	# Wall decks and their slopes are not ground. Only the first authored ramp
	# cell is shared with the ground layer and can become a portal.
	for surface in _authored_surface_projection():
		for cell_value in surface.get("cells", []) as Array:
			var cell := Vector2i(cell_value)
			if String(surface.get("role", "")) != "ramp" or not (surface.get("portal_cells", []) as Array).has(cell):
				grid.set_point_solid(cell, true)
	return map


func _authored_surface_projection() -> Array[Dictionary]:
	return [
		{"id": "main-ramp", "role": "ramp", "cells": _line_cells(5, 8, 7), "portal_cells": [Vector2i(5, 7)]},
		{"id": "main-deck", "role": "deck", "cells": _rect_cells(Vector2i(8, 6), Vector2i(14, 8))},
		{"id": "exit-ramp", "role": "ramp", "cells": _line_cells(14, 17, 7), "portal_cells": [Vector2i(17, 7)]},
		{"id": "orphan-deck", "role": "deck", "cells": _rect_cells(Vector2i(20, 6), Vector2i(22, 8))},
		{"id": "face-only-deck", "role": "deck", "cells": _rect_cells(Vector2i(10, 11), Vector2i(13, 13))},
	]


func _test_ground_pockets_bridge_only_through_ramps() -> void:
	var map = _make_map()
	var installed := bool(map.install_walk_surface_cells_for_test(_authored_surface_projection()))
	for y in range(HEIGHT):
		map._navigation_grid.set_point_solid(Vector2i(9, y), true)
	var start := _local(map, Vector2i(2, 7))
	var destination := _local(map, Vector2i(18, 7))
	var ground: Dictionary = map.query_route(start, destination)
	_check("sealed_ground_pockets_have_no_ground_route", installed and not bool(ground.get("valid", false)), str(ground))
	var bridge: Dictionary = map.query_layered_bridge_route(start, destination)
	_check("sealed_ground_pockets_gain_layered_bridge", bool(bridge.get("valid", false)), "bridge=%s portals=%s in=%s out=%s" % [str(bridge), str(map._walk_surface_portal_cells), str(map.query_route(start, _local(map, Vector2i(5, 7)))), str(map.query_route(_local(map, Vector2i(17, 7)), destination))])
	_check(
		"ground_bridge_is_ramp_deck_ramp",
		(bridge.get("surface_roles", []) as Array).has("ramp")
		and (bridge.get("surface_roles", []) as Array).has("deck"),
		str(bridge.get("surface_roles", []))
	)
	var sim = _make_sim(map, start)
	var selected: Array[int] = [1]
	var accepted: int = sim.issue_move(selected, destination)
	_check(
		"sim_accepts_ground_bridge_only_after_ground_failure",
		accepted == 1 and (sim.entity(1).get("route_surface_roles", []) as Array).has("deck"),
		"accepted=%d rejection=%s roles=%s" % [accepted, sim.last_route_rejection, sim.entity(1).get("route_surface_roles", [])]
	)


func _test_failed_bridge_component_pair_cache() -> void:
	var map = _make_map()
	var installed := bool(map.install_walk_surface_cells_for_test(_authored_surface_projection()))
	for y in range(HEIGHT):
		map._navigation_grid.set_point_solid(Vector2i(9, y), true)
	for y in range(6, 9):
		map._walk_surface_navigation_grid.set_point_solid(Vector2i(11, y), true)
	var has_cache_seam: bool = (
		map.has_method("rebuild_navigation_components_for_test")
		and map.has_method("bridge_route_negative_cache_size")
		and map.has_method("set_navigation_cell_walkable_for_test")
	)
	if not has_cache_seam:
		_check("failed_bridge_pair_is_cached", false, "component-cache seam missing")
		_check("failed_bridge_pair_repeat_is_constant_work", false, "component-cache seam missing")
		_check("navigation_mutation_invalidates_failed_pair", false, "component-cache seam missing")
		return
	map.rebuild_navigation_components_for_test()
	var start := _local(map, Vector2i(2, 7))
	var destination := _local(map, Vector2i(18, 7))
	var first: Dictionary = map.query_layered_bridge_route(start, destination)
	var ground_queries_after_first := int(map.route_query_count)
	_check(
		"failed_bridge_pair_is_cached",
		installed and not bool(first.get("valid", false)) and int(map.bridge_route_negative_cache_size()) == 1,
		"first=%s cache=%d" % [str(first), int(map.bridge_route_negative_cache_size())]
	)
	var second: Dictionary = map.query_layered_bridge_route(start, destination)
	_check(
		"failed_bridge_pair_repeat_is_constant_work",
		not bool(second.get("valid", false))
		and bool(second.get("component_pair_cache_hit", false))
		and int(map.route_query_count) == ground_queries_after_first,
		"second=%s ground=%d->%d" % [str(second), ground_queries_after_first, int(map.route_query_count)]
	)
	map.set_navigation_cell_walkable_for_test(Vector2i(9, 5), true)
	_check(
		"navigation_mutation_invalidates_failed_pair",
		int(map.bridge_route_negative_cache_size()) == 0
		and int(map.navigation_component_id(Vector2i(2, 7))) == int(map.navigation_component_id(Vector2i(18, 7))),
		"cache=%d components=%d/%d" % [int(map.bridge_route_negative_cache_size()), int(map.navigation_component_id(Vector2i(2, 7))), int(map.navigation_component_id(Vector2i(18, 7)))]
	)


func _test_source_component_without_portal_is_budgeted() -> void:
	var map = _make_map()
	var installed := bool(map.install_walk_surface_cells_for_test(_authored_surface_projection()))
	for y in range(HEIGHT):
		map._navigation_grid.set_point_solid(Vector2i(1, y), true)
	var has_budget_seam: bool = map.has_method("ground_portal_component_count")
	if not has_budget_seam:
		_check("portal_ground_components_are_precomputed", false, "portal-component seam missing")
		_check("source_component_without_portal_skips_scan", false, "portal-component seam missing")
		return
	map.rebuild_navigation_components_for_test()
	_check(
		"portal_ground_components_are_precomputed",
		installed and int(map.ground_portal_component_count()) >= 1,
		"installed=%s components=%d" % [str(installed), int(map.ground_portal_component_count())]
	)
	var route_queries_before := int(map.route_query_count)
	var result: Dictionary = map.query_layered_bridge_route(
		_local(map, Vector2i(0, 7)),
		_local(map, Vector2i(18, 7))
	)
	_check(
		"source_component_without_portal_skips_scan",
		not bool(result.get("valid", false))
		and bool(result.get("source_component_has_no_portal", false))
		and int(map.route_query_count) == route_queries_before,
		"result=%s ground=%d->%d" % [str(result), route_queries_before, int(map.route_query_count)]
	)


func _test_ai_failed_order_backoff() -> void:
	var map = _make_map()
	map.install_walk_surface_cells_for_test(_authored_surface_projection())
	for y in range(HEIGHT):
		map._navigation_grid.set_point_solid(Vector2i(9, y), true)
	for y in range(6, 9):
		map._walk_surface_navigation_grid.set_point_solid(Vector2i(11, y), true)
	map.rebuild_navigation_components_for_test()
	var sim = _make_sim(map, _local(map, Vector2i(2, 7)))
	var row: Dictionary = sim.entities[1]
	var destination := _local(map, Vector2i(18, 7))
	var profile := {"scan_interval": 15}
	if not sim.has_method("_ai_assign_target_route_with_backoff"):
		_check("ai_repeated_failed_order_backs_off", false, "AI backoff seam missing")
		_check("ai_backoff_fails_open_after_nav_mutation", false, "AI backoff seam missing")
		return
	sim.tick_index = 15
	var first := bool(sim._ai_assign_target_route_with_backoff(row, "battalion", 99, destination, profile))
	var bridge_queries_after_first := int(map.bridge_route_query_count)
	sim.tick_index = 30
	var second := bool(sim._ai_assign_target_route_with_backoff(row, "battalion", 99, destination, profile))
	var backoff: Dictionary = row.get("ai_route_backoff", {}) as Dictionary
	_check(
		"ai_repeated_failed_order_backs_off",
		not first and not second
		and int(map.bridge_route_query_count) == bridge_queries_after_first
		and int(backoff.get("retry_tick", 0)) > int(sim.tick_index),
		"first=%s second=%s bridge=%d->%d backoff=%s" % [str(first), str(second), bridge_queries_after_first, int(map.bridge_route_query_count), str(backoff)]
	)
	map.set_navigation_cell_walkable_for_test(Vector2i(9, 5), true)
	var after_mutation := bool(sim._ai_assign_target_route_with_backoff(row, "battalion", 99, destination, profile))
	_check(
		"ai_backoff_fails_open_after_nav_mutation",
		after_mutation and not row.has("ai_route_backoff"),
		"accepted=%s revision=%d row=%s" % [str(after_mutation), int(map.navigation_topology_revision), str(row.get("ai_route_backoff", {}))]
	)


func _test_distinct_ground_anchor_derivation() -> void:
	var map = _make_map()
	var wall_endpoint := Vector2i(5, 7)
	map._navigation_grid.set_point_solid(wall_endpoint, true)
	var low := _local(map, wall_endpoint)
	var high := _local(map, Vector2i(7, 7))
	var triangles: Array = [PackedVector3Array([
		Vector3(low.x, 0.0, low.y),
		Vector3(low.x, 0.0, low.y + 1.0),
		Vector3(high.x, 4.0, high.y),
	])]
	var surface_cells: Array[Vector2i] = _line_cells(5, 7, 7)
	var pairs: Array = []
	if map.has_method("_walk_surface_low_endpoint_pairs"):
		pairs = map.call(
			"_walk_surface_low_endpoint_pairs",
			triangles,
			Transform3D.IDENTITY,
			surface_cells
		) as Array
	_check(
		"distinct_ground_anchor_is_derived_from_authored_low_end",
		not pairs.is_empty()
		and Vector2i((pairs[0] as Dictionary).get("wall_cell", Vector2i(-1, -1))) == wall_endpoint
		and map.is_navigation_walkable(Vector2i((pairs[0] as Dictionary).get("ground_cell", Vector2i(-1, -1)))),
		str(pairs)
	)
	var installed := bool(map.install_walk_surface_cells_for_test([
		{"id": "offset-ramp", "role": "ramp", "cells": surface_cells, "portal_pairs": pairs},
		{"id": "offset-deck", "role": "deck", "cells": _rect_cells(Vector2i(7, 6), Vector2i(10, 8))},
	]))
	var route: Dictionary = map.query_layered_route(
		_local(map, Vector2i(2, 7)),
		_local(map, Vector2i(9, 7)),
		"ground"
	)
	_check(
		"distinct_ground_anchor_routes_without_synthesized_wall_cells",
		installed and bool(route.get("valid", false)) and (route.get("surface_roles", []) as Array).has("ramp"),
		"installed=%s pairs=%s route=%s" % [str(installed), str(pairs), str(route)]
	)


func _test_low_endpoint_pair_narrowing() -> void:
	var map = _make_map()
	var left_cell := Vector2i(4, 4)
	var right_cell := Vector2i(6, 4)
	map._navigation_grid.set_point_solid(left_cell, true)
	map._navigation_grid.set_point_solid(right_cell, true)
	var low := _local(map, Vector2i(5, 4))
	var high := _local(map, Vector2i(7, 4))
	var pairs: Array = map.call(
		"_walk_surface_low_endpoint_pairs",
		[PackedVector3Array([
			Vector3(low.x, 0.0, low.y),
			Vector3(low.x, 0.0, low.y + 1.0),
			Vector3(high.x, 4.0, high.y),
		])],
		Transform3D.IDENTITY,
		[left_cell, right_cell] as Array[Vector2i]
	) as Array
	_check(
		"low_endpoint_equal_candidates_narrow_to_one_sorted_pair",
		pairs.size() == 1
		and Vector2i((pairs[0] as Dictionary).get("wall_cell", Vector2i(-1, -1))) == left_cell
		and map.is_navigation_walkable(Vector2i((pairs[0] as Dictionary).get("ground_cell", Vector2i(-1, -1)))),
		str(pairs)
	)


func _test_authored_reach_admits_full_mesh_diagonal() -> void:
	var map = _make_map()
	for y in range(HEIGHT):
		for x in range(WIDTH):
			map._navigation_grid.set_point_solid(Vector2i(x, y), true)
	var target_ground := Vector2i(10, 6)
	map._navigation_grid.set_point_solid(target_ground, false)
	var low_a := _local(map, Vector2i(4, 2))
	var low_b := _local(map, Vector2i(4, 10))
	var high_a := _local(map, Vector2i(6, 2))
	var pairs: Array = map.call(
		"_walk_surface_low_endpoint_pairs",
		[PackedVector3Array([
			Vector3(low_a.x, 0.0, low_a.y),
			Vector3(low_b.x, 0.0, low_b.y),
			Vector3(high_a.x, 4.0, high_a.y),
		])],
		Transform3D.IDENTITY,
		[Vector2i(4, 2), Vector2i(4, 10)] as Array[Vector2i]
	) as Array
	_check(
		"authored_reach_admits_full_mesh_diagonal",
		pairs.size() == 1
		and Vector2i((pairs[0] as Dictionary).get("ground_cell", Vector2i(-1, -1))) == target_ground,
		"target=%s pairs=%s" % [target_ground, str(pairs)]
	)


func _test_ground_search_is_bounded_to_authored_reach() -> void:
	var map = _make_map()
	var low_points: Array[Vector2] = [_local(map, Vector2i(5, 5))]
	var bounds := Rect2i()
	if map.has_method("_walk_surface_ground_search_bounds"):
		bounds = map.call("_walk_surface_ground_search_bounds", low_points, 4.0) as Rect2i
	_check(
		"ground_endpoint_scan_is_bounded_to_authored_reach",
		bounds.has_point(Vector2i(5, 5)) and bounds.get_area() < WIDTH * HEIGHT,
		"bounds=%s full_area=%d" % [bounds, WIDTH * HEIGHT]
	)


func _test_selected_erebor_vertical_slice() -> void:
	var mod_loader = root.get_node_or_null("ModLoader")
	var content_root := OS.get_environment("OPENBFME_CONTENT").strip_edges()
	var pack_root := ""
	var content_db = root.get_node_or_null("ContentDB")
	if mod_loader != null and content_db != null:
		for candidate_value in content_db.get("pack_roots") as Array:
			var candidate := String(candidate_value)
			if candidate.get_base_dir().get_file() == "rotwk-playable-maps-private":
				pack_root = candidate
				break
	var map_path := pack_root.path_join("maps/wor-erebor/map.json") if pack_root != "" else ""
	var definition: Dictionary = {}
	if map_path != "" and FileAccess.file_exists(map_path):
		var parsed: Variant = JSON.parse_string(FileAccess.get_file_as_string(map_path))
		if typeof(parsed) == TYPE_DICTIONARY:
			definition = parsed as Dictionary
			definition["map"] = "maps/wor-erebor/map.json"
			definition["_source"] = map_path
			definition["_pack_root"] = pack_root
	var map = _map_data_script.new()
	var loaded := not definition.is_empty() and bool(map.load_from_pack(pack_root, definition))
	_check("selected_erebor_walk_surface_map_loads", loaded, "pack=%s error=%s" % [pack_root, String(map.error)])
	_check(
		"selected_erebor_walk_surface_layer_is_closed",
		loaded and bool(map.walk_surface_navigation_ready)
		and int(map.walk_surface_cell_count) > 0
		and int(map.walk_surface_ramp_cell_count) > 0
		and (map.walk_surface_gap_receipts as Array).is_empty(),
		"cells=%d ramps=%d portals=%d gaps=%s" % [int(map.walk_surface_cell_count), int(map.walk_surface_ramp_cell_count), (map._walk_surface_portal_cells as Array).size(), str(map.walk_surface_gap_receipts)]
	)
	if not loaded or not bool(map.walk_surface_navigation_ready):
		_check("selected_erebor_portal_to_deck_route_is_authored", false, "wall layer unavailable")
		_check("selected_erebor_unit_arrives_on_deck", false, "wall layer unavailable")
		return
	var best_cells: Array[Vector2i] = []
	for portal_value in map._walk_surface_portal_cells as Array:
		var portal := Vector2i(portal_value)
		for cell_value in map._walk_surface_roles_by_cell.keys():
			var cell := Vector2i(cell_value)
			if String(map._walk_surface_role(cell)) != "deck":
				continue
			var cells: Array[Vector2i] = map._walk_surface_navigation_grid.get_id_path(portal, cell, false)
			if not cells.is_empty() and (best_cells.is_empty() or cells.size() < best_cells.size()):
				best_cells = cells
	var route: Dictionary = {}
	if not best_cells.is_empty():
		route = map.query_layered_route(
			map.grid_to_local_horizontal(best_cells[0]),
			map.grid_to_local_horizontal(best_cells[best_cells.size() - 1]),
			"ground"
		)
	_check(
		"selected_erebor_portal_to_deck_route_is_authored",
		bool(route.get("valid", false))
		and (route.get("surface_roles", []) as Array).has("ramp")
		and (route.get("surface_roles", []) as Array).has("deck")
		and (route.get("point_elevations", []) as Array).size() == (route.get("points", []) as Array).size(),
		str(route)
	)
	if route.is_empty() or not bool(route.get("valid", false)):
		_check("selected_erebor_unit_arrives_on_deck", false, "no authored route")
		return
	var start: Vector2 = map.grid_to_local_horizontal(best_cells[0])
	var destination: Vector2 = map.grid_to_local_horizontal(best_cells[best_cells.size() - 1])
	var sim = _make_sim(map, start)
	var selected: Array[int] = [1]
	var accepted: int = sim.issue_move(selected, destination)
	var row: Dictionary = sim.entity(1)
	_run_route(sim, row, 1024)
	_check(
		"selected_erebor_unit_arrives_on_deck",
		accepted == 1
		and Vector2(row.get("position", Vector2.INF)).is_equal_approx(destination)
		and String(row.get("pathing_layer", "ground")) == "deck"
		and row.has("pathing_elevation"),
		"accepted=%d rejection=%s position=%s destination=%s layer=%s elevation=%s" % [accepted, sim.last_route_rejection, row.get("position"), destination, row.get("pathing_layer", "ground"), row.get("pathing_elevation", "missing")]
	)
func _line_cells(from_x: int, to_x: int, y: int) -> Array[Vector2i]:
	var cells: Array[Vector2i] = []
	for x in range(from_x, to_x + 1):
		cells.append(Vector2i(x, y))
	return cells


func _rect_cells(minimum: Vector2i, maximum: Vector2i) -> Array[Vector2i]:
	var cells: Array[Vector2i] = []
	for y in range(minimum.y, maximum.y + 1):
		for x in range(minimum.x, maximum.x + 1):
			cells.append(Vector2i(x, y))
	return cells


func _make_sim(map, start: Vector2):
	var sim = SimScript.new()
	var configuration := {
		"source_map_configured": true,
		"source_map_axis_x": Vector2.RIGHT,
		"source_map_axis_z": Vector2.DOWN,
		"route_provider": map,
		"playable_outline": PackedVector2Array([
			_local(map, Vector2i.ZERO),
			_local(map, Vector2i(WIDTH - 1, 0)),
			_local(map, Vector2i(WIDTH - 1, HEIGHT - 1)),
			_local(map, Vector2i(0, HEIGHT - 1)),
		]),
		"spawn_positions": {1: start, 2: start + Vector2(0, 1), 101: start + Vector2(1, 0), 102: start + Vector2(1, 1)},
		"player_starts": {"Player_1_Start": start, "Player_2_Start": start},
		# The sim's map-configuration contract requires the three named Fords
		# gates even for a synthetic non-Fords map. They are inert in this runner.
		"ford_gates": [
			{"name": "ford1", "edge_a": start, "edge_b": start, "center": start},
			{"name": "ford2", "edge_a": start, "edge_b": start, "center": start},
			{"name": "ford3", "edge_a": start, "edge_b": start, "center": start},
		],
	}
	var rules := {
		"spawn_initial_battalions": true,
		"unit_rules": {
			SimScript.SOLDIER_OBJECT_ID: _unit_rule(SimScript.SOLDIER_HORDE_ID),
			SimScript.ARCHER_OBJECT_ID: _unit_rule(SimScript.ARCHER_OBJECT_ID),
			SimScript.TOWER_GUARD_OBJECT_ID: _unit_rule(SimScript.TOWER_GUARD_OBJECT_ID),
			SimScript.KNIGHT_OBJECT_ID: _unit_rule(SimScript.KNIGHT_OBJECT_ID),
		},
	}
	sim.setup(configuration, rules)
	sim.ai_enabled = false
	sim.structures.clear()
	sim.expansion_pads.clear()
	var row: Dictionary = sim.entities[1]
	row["position"] = start
	row["facing"] = Vector2.RIGHT
	row["speed"] = 20.0
	row["current_speed"] = 0.0
	row["state"] = "idle"
	row["target_id"] = 0
	row["attack_move"] = false
	sim._spatial_sync(row)
	return sim


func _unit_rule(horde_id: String) -> Dictionary:
	return {
		"horde_id": horde_id,
		"speed": 20.0,
		"speed_source": 20.0,
		"acceleration": 200.0,
		"acceleration_source": 200.0,
		"braking": 200.0,
		"braking_source": 200.0,
		"turn_rate_degrees_per_second": 720.0,
		"attack_range": 1.0,
		"attack_range_source": 1.0,
		"minimum_attack_range": 0.0,
		"minimum_attack_range_source": 0.0,
		"vision_range": 40.0,
		"vision_range_source": 40.0,
		"delay_between_shots_ms": 600.0,
		"pre_attack_delay_ms": 200.0,
		"firing_duration_ms": 200.0,
		"attack_period_ticks": 10,
		"pre_attack_ticks": 2,
		"firing_duration_ticks": 2,
		"member_damage": 10,
		"member_health": 200,
		"member_count": 1,
		"formation_positions": [Vector3.ZERO],
		"provenance": {},
		"is_builder": false,
	}


func _local(map, cell: Vector2i) -> Vector2:
	return map.grid_to_local_horizontal(cell)


func _run_route(sim, row: Dictionary, max_ticks: int = 256) -> void:
	for _tick in range(max_ticks):
		if (row.get("route", []) as Array).is_empty():
			return
		sim._step_route(row)


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("CASTLE_WALL_WALK PASS %s" % name)
	else:
		failed += 1
		printerr("CASTLE_WALL_WALK FAIL %s%s" % [name, " (%s)" % detail if detail != "" else ""])


func _finish() -> void:
	if passed + failed != EXPECTED_CHECKS:
		failed += 1
		printerr("CASTLE_WALL_WALK FAIL check_count expected=%d actual=%d" % [EXPECTED_CHECKS, passed + failed - 1])
	print("CASTLE_WALL_WALK_RESULT passed=%d failed=%d" % [passed, failed])
	_runner_watchdog.stop()
	quit(0 if failed == 0 else 1)
