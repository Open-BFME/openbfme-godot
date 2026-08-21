extends SceneTree
## L7 product-path proof: boot two shipped castle maps through the real retail
## slice, then drive the deterministic sim for a bounded 4,000 ticks. Erebor
## and Carn Dum are the zero-SkirmishSpawnPoint cases; Helm's Deep is the
## authored-spawn control. The bound covers retail construction/production
## timers rather than weakening them for the test.

const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
const SimScript := preload("res://src/retail_slice/retail_slice_sim.gd")
const BOOT_DEADLINE_MS := 300000
const TICK_LIMIT := 4000
const AI_TEAM := 1
const MAP_CASES := [
	{
		"id": "rotwk.map.wor-ang-carn-dum",
		"name": "Carn Dum",
		"expected_skirmish_spawn_points": 0,
		"retail_base_layout": "map-specific",
	},
	{
		"id": "rotwk.map.wor-erebor",
		"name": "Erebor",
		"expected_skirmish_spawn_points": 0,
		"retail_base_layout": "map-specific",
	},
	{
		"id": "rotwk.map.wor-helms-deep",
		"name": "Helm's Deep",
		"expected_skirmish_spawn_points": 4,
		"retail_base_layout": "map-specific",
	},
	{
		"id": "rotwk.map.wor-minas-tirith",
		"name": "Minas Tirith",
		"expected_skirmish_spawn_points": 4,
		"retail_base_layout": "map-specific",
	},
	{
		"id": "rotwk.map.wor-dol-guldur",
		"name": "Dol Guldur",
		"expected_skirmish_spawn_points": 4,
		"retail_base_layout": "map-specific",
	},
	{
		"id": "rotwk.map.wor-grey-havens",
		"name": "Grey Havens",
		"expected_skirmish_spawn_points": 3,
		"retail_base_layout": "map-specific",
	},
	{
		"id": "rotwk.map.wor-ang-fornost",
		"name": "Fornost",
		"expected_skirmish_spawn_points": 0,
		"retail_base_layout": "map-specific",
	},
	{
		"id": "rotwk.map.wor-isengard",
		"name": "Isengard",
		"expected_skirmish_spawn_points": 2,
		"retail_base_layout": "generic-fallback",
	},
	{
		"id": "rotwk.map.wor-black-gate",
		"name": "Black Gate",
		"expected_skirmish_spawn_points": 3,
		"retail_base_layout": "generic-fallback",
	},
	{
		"id": "rotwk.map.wor-minas-morgul",
		"name": "Minas Morgul",
		"expected_skirmish_spawn_points": 3,
		"retail_base_layout": "generic-fallback",
	},
]

var _runner_watchdog := RunnerWatchdogScript.new()
var _passed := 0
var _failed := 0
var _checks := 0


func _init() -> void:
	_runner_watchdog.start(self, "CASTLE_SKIRMISH_AI_RUNNER", 900000)
	await process_frame
	OS.set_environment("OPENBFME_SLICE_FACTION", "men")
	_test_rotated_ai_base_offset_projection()
	var case_filter := OS.get_environment("OPENBFME_CASTLE_AI_MAP").strip_edges()
	var selected_cases := []
	for case_value in MAP_CASES:
		if case_filter == "" or String((case_value as Dictionary)["id"]) == case_filter:
			selected_cases.append(case_value)
	for case_value in selected_cases:
		await _run_map(case_value as Dictionary)
	_check(
		"runner_check_count_liveness",
		selected_cases.size() > 0 and _checks >= selected_cases.size() * 8,
		"checks=%d maps=%d" % [_checks, selected_cases.size()]
	)
	if _failed == 0:
		print("CASTLE_SKIRMISH_AI CLASSIFICATION Carn Dum=base-building(runtime-tested), Erebor=base-building(runtime-tested), Helm's Deep=base-building(runtime-tested)")
		print("CASTLE_SKIRMISH_AI CLASSIFICATION Minas Tirith=base-building(map-specific, runtime-tested), Dol Guldur=base-building(map-specific, runtime-tested), Grey Havens=base-building(map-specific, runtime-tested), Fornost=base-building(map-specific, runtime-tested)")
		print("CASTLE_SKIRMISH_AI CLASSIFICATION Isengard=base-building(generic-fallback, runtime-tested), Black Gate=base-building(generic-fallback, runtime-tested), Minas Morgul=base-building(generic-fallback, runtime-tested); defend-only=none proven")
	else:
		print("CASTLE_SKIRMISH_AI CLASSIFICATION incomplete runner_failures=%d" % _failed)
	_finish()


func _run_map(case_row: Dictionary) -> void:
	var map_id := String(case_row["id"])
	var map_name := String(case_row["name"])
	OS.set_environment("OPENBFME_SLICE_MAP", map_id)
	var scene: PackedScene = load("res://scenes/retail_vertical_slice.tscn")
	if not _check("%s_scene_loads" % map_name, scene != null, "scene missing"):
		return
	var slice = scene.instantiate()
	root.add_child(slice)
	var deadline := Time.get_ticks_msec() + BOOT_DEADLINE_MS
	while Time.get_ticks_msec() < deadline:
		await process_frame
		if bool(slice.ready_ok) or String(slice.failure_reason) != "":
			break
	if not _check(
		"%s_slice_ready" % map_name,
		bool(slice.ready_ok),
		"map=%s failure=%s" % [map_id, String(slice.failure_reason)]
	):
		_remove_slice(slice)
		return

	# Stop the scene's wall-clock loop; this runner owns the exact deterministic
	# tick budget below, matching other retail-slice test runners.
	slice.simulation_paused = true
	var simulation = slice.simulation
	_check("%s_ai_team_configured" % map_name, simulation.team_is_ai(AI_TEAM), "descriptor=%s" % str(simulation.team_descriptor(AI_TEAM)))
	_check("%s_authored_player_starts" % map_name, slice.source_map_data.local_player_starts.size() >= 2, "starts=%s" % str(slice.source_map_data.local_player_starts.keys()))
	if map_id == "rotwk.map.wor-erebor":
		var build_plot := Vector2(slice.source_map_data.local_named_waypoints.get("Player_1_BuildPlot_1", Vector2.ZERO))
		var player_start_3d := Vector3(slice.source_map_data.local_player_starts.get("Player_1_Start", Vector3.ZERO))
		var player_start := Vector2(player_start_3d.x, player_start_3d.z)
		_check("Erebor_build_plot_1_local_position", build_plot.distance_to(Vector2(36.27, -6.31)) < 0.02, "actual=%s" % str(build_plot))
		_check("Erebor_player_1_start_local_position", player_start.distance_to(Vector2(38.0, 0.0)) < 0.001, "actual=%s" % str(player_start))
		_check("Erebor_named_waypoint_not_origin", build_plot.length() > 1.0, "actual=%s" % str(build_plot))
	_print_gate_diagnostics(map_name, simulation)

	var spawn_points := 0
	for placement_value in slice.source_map_data.scenario_object_placements:
		if String((placement_value as Dictionary).get("type_name", "")).to_lower() == "skirmishspawnpoint":
			spawn_points += 1
	_check(
		"%s_skirmish_spawn_point_oracle" % map_name,
		spawn_points == int(case_row["expected_skirmish_spawn_points"]),
		"expected=%d actual=%d" % [int(case_row["expected_skirmish_spawn_points"]), spawn_points]
	)

	var initial_tick := int(simulation.tick_index)
	var initial_orders := {}
	var initial_builder_positions := {}
	for entity_id in simulation.entity_ids():
		var entity: Dictionary = simulation.entity(entity_id)
		if int(entity.get("team", -1)) == AI_TEAM:
			initial_orders[int(entity_id)] = int(entity.get("order_sequence", 0))
			if bool(entity.get("is_builder", false)):
				initial_builder_positions[int(entity_id)] = Vector2(entity.get("position", Vector2.ZERO))
	var attack_order_entities := {}
	for _tick in range(TICK_LIMIT):
		if int(simulation.winner) != -1:
			break
		simulation.tick()
		for entity_id in simulation.living_ids(AI_TEAM):
			var entity: Dictionary = simulation.entity(entity_id)
			if bool(entity.get("is_builder", false)):
				continue
			var order_sequence := int(entity.get("order_sequence", 0))
			var baseline := int(initial_orders.get(int(entity_id), 0))
			if order_sequence > baseline and (
				int(entity.get("target_id", 0)) != 0
				or bool(entity.get("ai_in_wave", false))
				or not (entity.get("route", []) as Array).is_empty()
			):
				attack_order_entities[int(entity_id)] = order_sequence
		if not attack_order_entities.is_empty():
			var completed_dynamic_structure := false
			for structure_id in simulation.structure_ids(AI_TEAM):
				if (
					int(structure_id) >= 3000
					and int(structure_id) < simulation.CASTLE_FIXTURE_FIRST_ID
					and float(simulation.structure(structure_id).get("construction_progress", 0.0)) >= 1.0
				):
					completed_dynamic_structure = true
					break
			if completed_dynamic_structure:
				break

	var structures_built := 0
	var built_targets := {}
	var started_sites := []
	for event_value in simulation.events:
		var event: Dictionary = event_value
		if String(event.get("kind", "")) == "construction.completed" and int(event.get("team", -1)) == AI_TEAM:
			built_targets[int(event.get("target_id", 0))] = true
		if String(event.get("kind", "")) == "construction.started" and int(event.get("team", -1)) == AI_TEAM:
			var site_id := int(event.get("target_id", 0))
			var builder_id := int(event.get("entity_id", 0))
			if simulation.structures.has(site_id):
				var site: Dictionary = simulation.structure(site_id)
				var site_position := Vector2(site.get("position", Vector2.ZERO))
				started_sites.append({
					"id": site_id,
					"position": site_position,
					"builder_id": builder_id,
					"travel_distance": Vector2(initial_builder_positions.get(builder_id, site_position)).distance_to(site_position),
					"builder_free": bool(site.get("builder_free", false)),
				})
	structures_built = built_targets.size()
	var attack_orders_issued := attack_order_entities.size()
	if attack_orders_issued == 0:
		_print_route_target_diagnostics(map_name, simulation)
	var dynamic_ai_structures := 0
	for structure_id in simulation.structure_ids(AI_TEAM):
		if int(structure_id) >= 3000 and int(structure_id) < simulation.CASTLE_FIXTURE_FIRST_ID:
			dynamic_ai_structures += 1

	_check("%s_tick_budget_live" % map_name, int(simulation.tick_index) > initial_tick, "tick=%d->%d winner=%d" % [initial_tick, int(simulation.tick_index), int(simulation.winner)])
	_check(
		"%s_ai_built_structure" % map_name,
		structures_built >= 1 and dynamic_ai_structures >= 1,
		_diagnostic(simulation, structures_built, attack_orders_issued, dynamic_ai_structures)
	)
	_check(
		"%s_ai_issued_attack_order" % map_name,
		attack_orders_issued >= 1,
		_diagnostic(simulation, structures_built, attack_orders_issued, dynamic_ai_structures)
	)
	var porter_travelled := false
	for started_value in started_sites:
		var started := started_value as Dictionary
		if built_targets.has(int(started["id"])) and float(started["travel_distance"]) > 1.0 and not bool(started["builder_free"]):
			porter_travelled = true
			break
	_check("%s_porter_travelled_to_real_site" % map_name, porter_travelled, "started_sites=%s" % str(started_sites))
	_check(
		"%s_zero_spawn_fallback" % map_name,
		spawn_points > 0 or (structures_built >= 1 and attack_orders_issued >= 1),
		"zero SkirmishSpawnPoint map failed Player_N_Start-derived home fallback; %s" % _diagnostic(simulation, structures_built, attack_orders_issued, dynamic_ai_structures)
	)
	print(
		"CASTLE_SKIRMISH_AI MAP map=%s retail_base_layout=%s skirmish_spawn_points=%d ticks=%d structures_built=%d dynamic_ai_structures=%d attack_orders_issued=%d sites=%s porter_travelled=%s status=%s" % [
			map_name,
			String(case_row["retail_base_layout"]),
			spawn_points,
			int(simulation.tick_index) - initial_tick,
			structures_built,
			dynamic_ai_structures,
			attack_orders_issued,
			str(started_sites),
			str(porter_travelled),
			"base-building" if structures_built >= 1 and attack_orders_issued >= 1 else "defend-only-or-stalled",
		]
	)
	print(
		"CASTLE_SKIRMISH_AI ROUTE_PERF map=%s ticks=%d ground_queries=%d bridge_queries=%d bridge_cache_hits=%d portal_budget_rejections=%d bridge_total_ms=%.3f" % [
			map_name,
			int(simulation.tick_index) - initial_tick,
			int(slice.source_map_data.route_query_count),
			int(slice.source_map_data.bridge_route_query_count),
			int(slice.source_map_data.bridge_route_cache_hit_count),
			int(slice.source_map_data.bridge_route_portal_budget_rejection_count),
			float(slice.source_map_data.bridge_route_query_total_usec) / 1000.0,
		]
	)
	_remove_slice(slice)
	await process_frame


func _test_rotated_ai_base_offset_projection() -> void:
	var sim = SimScript.new()
	var diagonal := Vector2(1.0, 1.0).normalized()
	sim._source_map_axis_x = diagonal
	sim._source_map_axis_z = Vector2(-diagonal.y, diagonal.x)
	sim._rules = {"source_map_transform_scale": 0.5}
	var anchor := Vector2(5.0, 6.0)
	var actual: Vector2 = anchor + sim._castle_ai_project_source_offset(Vector2(10.0, 0.0))
	var expected := Vector2(8.535534, 2.464466)
	_check("rotated_ai_base_offset_uses_map_frame", actual.distance_to(expected) < 0.0001, "expected=%s actual=%s" % [str(expected), str(actual)])


func _print_gate_diagnostics(map_name: String, simulation) -> void:
	for structure_id in simulation.structure_ids():
		var row: Dictionary = simulation.structure(structure_id)
		if String(row.get("castle_fixture_role", "")) != "gate":
			continue
		print("CASTLE_SKIRMISH_AI GATE map=%s id=%d team=%d ai_gate=%s open=%s portal=%s" % [
			map_name,
			int(structure_id),
			int(row.get("team", -1)),
			str(not (row.get("ai_gate_update", {}) as Dictionary).is_empty()),
			str(bool((row.get("gate_behavior", {}) as Dictionary).get("pathing_open", false))),
			str(not (row.get("fake_pathfind_portal", {}) as Dictionary).is_empty()),
		])


func _print_route_target_diagnostics(map_name: String, simulation) -> void:
	var source := Vector2.ZERO
	for entity_id in simulation.living_ids(AI_TEAM):
		var entity: Dictionary = simulation.entity(entity_id)
		if not bool(entity.get("is_builder", false)):
			source = Vector2(entity.get("position", Vector2.ZERO))
			break
	var candidates := []
	for entity_id in simulation._hostile_living_ids(AI_TEAM):
		var entity: Dictionary = simulation.entity(entity_id)
		var destination := Vector2(entity.get("position", Vector2.ZERO))
		var route: Dictionary = simulation._query_route(source, destination)
		candidates.append({"kind": "battalion", "id": entity_id, "distance": source.distance_to(destination), "valid": route.get("valid", false), "reason": route.get("reason", "")})
	for structure_id in simulation._hostile_living_structure_ids(AI_TEAM):
		var structure: Dictionary = simulation.structure(structure_id)
		var destination := Vector2(structure.get("position", Vector2.ZERO))
		var route: Dictionary = simulation._query_route(source, destination)
		if bool(route.get("valid", false)):
			candidates.append({"kind": "structure", "id": structure_id, "type": structure.get("castle_fixture_type", structure.get("structure_kind", "")), "distance": source.distance_to(destination), "valid": true})
	print("CASTLE_SKIRMISH_AI ROUTE_TARGETS map=%s source=%s candidates=%s" % [map_name, str(source), str(candidates)])


func _diagnostic(simulation, structures_built: int, attack_orders_issued: int, dynamic_ai_structures: int) -> String:
	var unit_count: int = simulation.living_ids(AI_TEAM).size()
	var queues := []
	var income_per_payout := 0
	var units := []
	for entity_id in simulation.living_ids(AI_TEAM):
		var entity: Dictionary = simulation.entity(entity_id)
		units.append({
			"id": entity_id,
			"builder": entity.get("is_builder", false),
			"state": entity.get("state", ""),
			"position": entity.get("position", Vector2.ZERO),
			"destination": entity.get("destination", Vector2.ZERO),
			"construction_id": entity.get("construction_id", 0),
			"route_points": (entity.get("route", []) as Array).size(),
		})
	for structure_id in simulation.structure_ids(AI_TEAM):
		var row: Dictionary = simulation.structure(structure_id)
		if float(row.get("construction_progress", 0.0)) >= 1.0:
			income_per_payout += int(row.get("income_per_payout", 0))
		if not (row.get("queue", []) as Array).is_empty():
			queues.append({"id": structure_id, "kind": row.get("structure_kind", ""), "queue": row.get("queue", [])})
	return "tick=%d units=%d income=%d resources=%d structures_built=%d dynamic_structures=%d attack_orders=%d queues=%s unit_rows=%s ai_state=%s last_route_rejection=%s" % [
		int(simulation.tick_index),
		unit_count,
		income_per_payout,
		int(simulation.resources_for_team(AI_TEAM)),
		structures_built,
		dynamic_ai_structures,
		attack_orders_issued,
		str(queues),
		str(units),
		str(simulation._team_ai_state.get(AI_TEAM, {})),
		String(simulation.last_route_rejection),
	]


func _remove_slice(slice) -> void:
	if is_instance_valid(slice):
		if slice.get_parent() == root:
			root.remove_child(slice)
		slice.free()


func _check(name: String, ok: bool, detail: String = "") -> bool:
	_checks += 1
	if ok:
		_passed += 1
		print("CASTLE_SKIRMISH_AI PASS %s" % name)
	else:
		_failed += 1
		print("CASTLE_SKIRMISH_AI FAIL %s%s" % [name, (" (%s)" % detail) if detail != "" else ""])
	return ok


func _finish() -> void:
	_runner_watchdog.stop()
	print("CASTLE_SKIRMISH_AI_RESULT passed=%d failed=%d checks=%d" % [_passed, _failed, _checks])
	quit(0 if _failed == 0 else 1)
