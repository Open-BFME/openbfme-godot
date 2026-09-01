extends SceneTree
## Focused L4 proof. L3 is intentionally not claimed: this uses the exact
## v9.7.7 gate contract against a source-labelled non-cardinal map mutation.
## The independent oracle enumerates authored wall cells from first-principles
## geometry; it never calls the production channel construction helpers.

const Watchdog := preload("res://tests/runner_watchdog.gd")
const NON_CARDINAL_MUTATION_YAW_RADIANS := PI / 6.0
const MUTATION_PROOF_LABEL := "exact-v9.7.7-minas-fixture/non-cardinal-placement-yaw-mutation"
const PIVOT_LOCAL := Vector2(30.0, 40.0)
const ANCHOR_LOCAL_LENGTH := 98.72
const RESET_MILLISECONDS := 6000
const WALL_INWARD_MIN := 90.5
const WALL_INWARD_MAX := 94.5

var passed := 0
var failed := 0
var _watchdog := Watchdog.new()


func _initialize() -> void:
	_watchdog.start(self, "MINAS_GATE_202", 120000)
	call_deferred("_run")


func _run() -> void:
	var MapScript: GDScript = load("res://src/retail_slice/retail_map_data.gd")
	var SimScript: GDScript = load("res://src/retail_slice/retail_slice_sim.gd")
	_check("scripts_loaded", MapScript != null and SimScript != null)
	if MapScript == null or SimScript == null:
		_finish()
		return
	var map = MapScript.new()
	_configure_rotated_map_seam(map)
	map._derive_castle_fixture_placements()
	_check("source_labelled_non_cardinal_yaw", MUTATION_PROOF_LABEL != "" and absf(NON_CARDINAL_MUTATION_YAW_RADIANS) > 0.01)
	_check("authored_fixture_derived", map.castle_fixture_placements.size() == 1)
	var placement := map.castle_fixture_placements[0] as Dictionary
	var gate_block := placement.get("gate", {}) as Dictionary
	var closed_geometry := (gate_block.get("geometries", {}) as Dictionary).get("Closed", {}) as Dictionary
	_check("authored_anchor_survives", gate_block.get("rotationAnchorOffset", []) == [987.2, 0.0])
	_check("named_geometries_survive", (gate_block.get("geometries", {}) as Dictionary).has_all(["Closed", "OpenLeft", "OpenRight"]))
	var expected_origin := PIVOT_LOCAL + Vector2(ANCHOR_LOCAL_LENGTH, 0.0).rotated(NON_CARDINAL_MUTATION_YAW_RADIANS)
	var sign_flipped_origin := PIVOT_LOCAL + Vector2(ANCHOR_LOCAL_LENGTH, 0.0).rotated(-NON_CARDINAL_MUTATION_YAW_RADIANS)
	var actual_origin := Vector2(placement.get("gate_geometry_origin", Vector2.INF))
	_check("rotated_anchor_origin_is_analytic", actual_origin.is_equal_approx(expected_origin), "%s != %s" % [actual_origin, expected_origin])
	_check("yaw_sign_flip_cannot_satisfy_origin", not actual_origin.is_equal_approx(sign_flipped_origin))

	map._build_gate_navigation_channels()
	map.navigation_ready = true
	map._navigation_topology_mutated()
	var source_index := int(placement.get("source_index", -1))
	var production_cells: Array[Vector2i] = map.gate_navigation_channel_cells(source_index)
	var rotated_oracle := _independent_channel_footprint(map, NON_CARDINAL_MUTATION_YAW_RADIANS, closed_geometry)
	var sign_flipped_oracle := _independent_channel_footprint(map, -NON_CARDINAL_MUTATION_YAW_RADIANS, closed_geometry)
	var axis_aligned_oracle := _independent_channel_footprint(map, 0.0, closed_geometry)
	_check("production_channel_is_nonempty", not production_cells.is_empty())
	_check("production_equals_exact_rotated_footprint", _same_cell_set(production_cells, rotated_oracle), "production=%s oracle=%s" % [_cell_set(production_cells).keys(), _cell_set(rotated_oracle).keys()])
	_check("sign_flipped_footprint_is_unequal", not _same_cell_set(rotated_oracle, sign_flipped_oracle))
	_check("axis_aligned_footprint_is_unequal", not _same_cell_set(rotated_oracle, axis_aligned_oracle))
	print("MINAS_GATE_202 FOOTPRINT production=%d rotated=%d sign_flipped=%d axis_aligned=%d" % [production_cells.size(), rotated_oracle.size(), sign_flipped_oracle.size(), axis_aligned_oracle.size()])

	var direction := Vector2.RIGHT.rotated(NON_CARDINAL_MUTATION_YAW_RADIANS)
	var city := PIVOT_LOCAL - direction * 10.0
	var field := PIVOT_LOCAL + direction * 120.0
	map.set_castle_gate_pathing(source_index, false, false)
	_check("closed_before_negative_controls", not bool(map.query_route(city, field).get("valid", false)))
	_check("sign_flipped_control_cannot_open_seam", _control_footprint_cannot_open(map, sign_flipped_oracle, city, field))
	_check("axis_aligned_control_cannot_open_seam", _control_footprint_cannot_open(map, axis_aligned_oracle, city, field))

	# Seed the converted placement through the shipping castle-fixture path.
	# That path alone performs the shipping millisecond-to-tick projection.
	var sim = SimScript.new()
	sim.route_provider = map
	sim._castle_fixture_placements = [placement]
	sim._seed_castle_fixtures()
	var structure_id := _single_seeded_structure_id(sim, source_index)
	_check("shipping_castle_fixture_seeded", structure_id >= 0)
	if structure_id < 0:
		_finish()
		return
	var seeded_gate := sim.structures[structure_id] as Dictionary
	var reset_policy := seeded_gate.get("gate_behavior", {}) as Dictionary
	var tick_milliseconds := float(SimScript.TICK_SECONDS) * 1000.0
	var projected_reset_ticks := int(reset_policy.get("reset_ticks", -1))
	_check("converted_reset_is_exact_6000ms", int(gate_block.get("resetMilliseconds", -1)) == RESET_MILLISECONDS)
	_check("shipping_reset_projection_is_60x100ms", is_equal_approx(tick_milliseconds, 100.0) and projected_reset_ticks == 60 and is_equal_approx(float(projected_reset_ticks) * tick_milliseconds, float(RESET_MILLISECONDS)))
	_check("shipping_seed_opens_exact_rotated_seam", bool(map.query_route(city, field).get("valid", false)) and map.gate_navigation_opened_cell_count(source_index) == rotated_oracle.size())

	var owner_team := int(seeded_gate.get("team", -1))
	var closed: Dictionary = sim.toggle_gate(owner_team, structure_id)
	_check("close_splits_rotated_seam", bool(closed.get("ok", false)) and not bool(map.query_route(city, field).get("valid", false)))
	var reopened: Dictionary = sim.request_gate_open(structure_id)
	_check("request_open_syncs_before_return", bool(reopened.get("ok", false)) and int(reopened.get("close_tick", -1)) == projected_reset_ticks and bool(map.query_route(city, field).get("valid", false)))
	for tick_number in range(1, projected_reset_ticks):
		sim.tick_index = tick_number
		sim._step_gate_updates()
	_check("reset_remains_open_for_59_ticks", bool(map.query_route(city, field).get("valid", false)))
	sim.tick_index = projected_reset_ticks
	sim._step_gate_updates()
	_check("reset_closes_on_exact_tick_60", not bool(map.query_route(city, field).get("valid", false)))
	var combat_reopened: Dictionary = sim.request_gate_open(structure_id)
	var combat_reclosed: Dictionary = sim.toggle_gate(owner_team, structure_id)
	_check("reopen_then_close_before_combat", bool(combat_reopened.get("ok", false)) and bool(combat_reclosed.get("ok", false)) and not bool(map.query_route(city, field).get("valid", false)))
	sim._apply_structure_damage(0, structure_id, int(seeded_gate.get("maximum_health", 0)), "default")
	_check("combat_destruction_opens_zero_health", int((sim.structures[structure_id] as Dictionary).get("health", -1)) == 0 and bool(map.query_route(city, field).get("valid", false)))
	map.set_castle_gate_pathing(source_index, false, false)
	var destroyed_toggle: Dictionary = sim.toggle_gate(owner_team, structure_id)
	_check("breach_cannot_reclose", not bool(destroyed_toggle.get("ok", true)) and String(destroyed_toggle.get("reason", "")) == "gate-destroyed" and bool(map.query_route(city, field).get("valid", false)) and map.gate_navigation_opened_cell_count(source_index) == rotated_oracle.size())
	_finish()


func _independent_channel_footprint(map, candidate_yaw: float, closed_geometry: Dictionary) -> Array[Vector2i]:
	var result: Array[Vector2i] = []
	var facing := Vector2.RIGHT.rotated(candidate_yaw)
	var origin := PIVOT_LOCAL + Vector2(ANCHOR_LOCAL_LENGTH, 0.0).rotated(candidate_yaw)
	var inward := origin.direction_to(PIVOT_LOCAL)
	var major: float = float(closed_geometry.get("majorRadius", 0.0)) * map.local_transform_scale
	var minor: float = float(closed_geometry.get("minorRadius", 0.0)) * map.local_transform_scale
	for x in range(map.width):
		for y in range(map.height):
			var cell := Vector2i(x, y)
			var point := Vector2(cell)
			if not _authored_source_wall_cell(point):
				continue
			var delta := point - origin
			var along := delta.dot(inward)
			var across := absf(delta.dot(Vector2(-inward.y, inward.x)))
			if along >= -major and along <= ANCHOR_LOCAL_LENGTH + major and across <= minor:
				result.append(cell)
	if not origin.is_equal_approx(PIVOT_LOCAL + facing * ANCHOR_LOCAL_LENGTH):
		return []
	return result


func _authored_source_wall_cell(point: Vector2) -> bool:
	var authored_direction := Vector2.RIGHT.rotated(NON_CARDINAL_MUTATION_YAW_RADIANS)
	var inward_distance := (point - PIVOT_LOCAL).dot(authored_direction)
	return inward_distance >= WALL_INWARD_MIN and inward_distance <= WALL_INWARD_MAX


func _cell_set(cells: Array[Vector2i]) -> Dictionary:
	var result := {}
	for cell in cells:
		result["%d,%d" % [cell.x, cell.y]] = true
	return result


func _same_cell_set(first: Array[Vector2i], second: Array[Vector2i]) -> bool:
	return _cell_set(first) == _cell_set(second)


func _control_footprint_cannot_open(map, cells: Array[Vector2i], city: Vector2, field: Vector2) -> bool:
	for cell in cells:
		if map._navigation_grid.is_point_solid(cell):
			map._navigation_grid.set_point_solid(cell, false)
			map.navigation_walkable_count += 1
	map._navigation_topology_mutated()
	var route_opened := bool(map.query_route(city, field).get("valid", false))
	for cell in cells:
		if not map._navigation_grid.is_point_solid(cell):
			map._navigation_grid.set_point_solid(cell, true)
			map.navigation_walkable_count -= 1
	map._navigation_topology_mutated()
	return not route_opened


func _single_seeded_structure_id(sim, source_index: int) -> int:
	var found := -1
	for structure_id_value in sim.structures.keys():
		var structure_id := int(structure_id_value)
		if int((sim.structures[structure_id] as Dictionary).get("source_index", -1)) != source_index:
			continue
		if found >= 0:
			return -1
		found = structure_id
	return found


func _configure_rotated_map_seam(map) -> void:
	map.width = 180
	map.height = 150
	map.border_width = 0
	map.horizontal_scale = 10.0
	map.local_transform_scale = 0.1
	map.local_transform_origin = Vector2.ZERO
	map.local_axis_x = Vector2.RIGHT
	map.local_axis_z = Vector2.UP
	map.reference_elevation = 0.0
	map.navigation_grid_min = Vector2i.ZERO
	map.navigation_grid_max = Vector2i(179, 149)
	map._navigation_grid = AStarGrid2D.new()
	map._navigation_grid.region = Rect2i(0, 0, 180, 150)
	map._navigation_grid.cell_size = Vector2.ONE
	map._navigation_grid.diagonal_mode = AStarGrid2D.DIAGONAL_MODE_NEVER
	map._navigation_grid.update()
	map._water_cells.resize(map.width * map.height)
	map._water_cells.fill(0)
	map.navigation_walkable_count = map.width * map.height
	for x in range(map.width):
		for y in range(map.height):
			var point := Vector2(x, y)
			if not _authored_source_wall_cell(point):
				continue
			map._navigation_grid.set_point_solid(Vector2i(x, y), true)
			map.navigation_walkable_count -= 1
	map.map_fixtures = [{
		"index": 0, "typeName": "MinisGateDoor", "role": "gate",
		"kindOf": ["STRUCTURE", "BLOCKING_GATE", "WALL_GATE"],
		"position": [300.0, 0.0, -400.0], "angle": NON_CARDINAL_MUTATION_YAW_RADIANS,
		"originalOwner": "Player_1/teamPlayer_1", "maxHealth": 2000.0,
		"armor": "DefaultWallArmor", "indestructible": false,
		"enabled": true, "targetable": false,
		"gate": {
			"openByDefault": true, "resetMilliseconds": RESET_MILLISECONDS,
			"percentOpenForPathing": 50.0,
			"rotationAnchorOffset": [987.2, 0.0],
			"geometries": {
				"Closed": {"shape": "BOX", "majorRadius": 7.2, "minorRadius": 58.4, "height": 61.0, "offset": [0.0, 0.0, 0.0]},
				"OpenLeft": {"shape": "BOX", "majorRadius": 25.0, "minorRadius": 3.0, "height": 61.0, "offset": [12.0, 56.0, 0.0]},
				"OpenRight": {"shape": "BOX", "majorRadius": 25.0, "minorRadius": 3.0, "height": 61.0, "offset": [12.0, -56.0, 0.0]},
			},
		},
	}]


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("MINAS_GATE_202 CHECK_PASS %s" % name)
	else:
		failed += 1
		printerr("MINAS_GATE_202 CHECK_FAIL %s%s" % [name, " (%s)" % detail if detail != "" else ""])


func _finish() -> void:
	if failed == 0:
		print("MINAS_GATE_202 PASS checks=%d" % passed)
	else:
		printerr("MINAS_GATE_202 FAIL passed=%d failed=%d" % [passed, failed])
	_watchdog.stop()
	quit(0 if failed == 0 else 1)
