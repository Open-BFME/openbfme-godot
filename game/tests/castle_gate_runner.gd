extends SceneTree

const Watchdog := preload("res://tests/runner_watchdog.gd")
const BOOT_DEADLINE_MS := 300000
const FRIEND_ID := 99001
const ENEMY_ID := 99002

var passed := 0
var failed := 0
var _watchdog := Watchdog.new()


func _initialize() -> void:
	_watchdog.start(self, "CASTLE_GATE", 600000)
	_watchdog.set_result_provider(func() -> Vector2i: return Vector2i(passed, failed))
	call_deferred("_run")


func _check(name: String, condition: bool, detail: String = "") -> void:
	_watchdog.note(name)
	if condition:
		passed += 1
		print("CASTLE_GATE PASS %s" % name)
	else:
		failed += 1
		printerr("CASTLE_GATE FAIL %s%s" % [name, " (%s)" % detail if detail != "" else ""])


func _run() -> void:
	OS.set_environment("OPENBFME_SLICE_FACTION", "men")
	OS.set_environment("OPENBFME_SLICE_MAP", "rotwk.map.wor-erebor")
	var scene: PackedScene = load("res://scenes/retail_vertical_slice.tscn")
	_check("scene_loads", scene != null)
	if scene == null:
		_finish()
		return
	var slice = scene.instantiate()
	root.add_child(slice)
	var deadline := Time.get_ticks_msec() + BOOT_DEADLINE_MS
	while Time.get_ticks_msec() < deadline:
		await process_frame
		if bool(slice.ready_ok) or String(slice.failure_reason) != "":
			break
	_check("erebor_slice_ready", bool(slice.ready_ok), String(slice.failure_reason))
	if not bool(slice.ready_ok):
		_finish()
		return
	var sim = slice.simulation
	slice.set_process(false)
	slice.set_physics_process(false)
	sim.ai_enabled = false
	var gate: Dictionary = {}
	for structure_id in sim.structure_ids():
		var row: Dictionary = sim.structure(structure_id)
		if String(row.get("castle_fixture_type", "")) == "EreborGateDoors":
			gate = row
			break
	_check("erebor_gate_row_exists", not gate.is_empty())
	var policy: Dictionary = gate.get("gate_behavior", {})
	var gate_placement: Dictionary = {}
	for placement_value in slice.source_map_data.castle_fixture_placements:
		var placement: Dictionary = placement_value
		if int(placement.get("source_index", -1)) == int(gate.get("source_index", -2)):
			gate_placement = placement
			break
	_check(
		"gate_behavior_matches_open_by_default",
		not policy.is_empty() and bool(policy.get("open", false)),
		"gate_behavior=%s structure=%s placement_gate=%s" % [str(policy), str(gate), str(gate_placement.get("gate", {}))]
	)
	_check("gate_carries_three_named_geometries", (gate.get("gate_geometries", {}) as Dictionary).keys().size() == 3)
	var gate_id := int(gate.get("id", 0))
	var gate_at := Vector2(gate.get("position", Vector2.ZERO))
	var across := Vector2.DOWN.rotated(float(gate.get("facing_radians", 0.0))).normalized()
	var start := gate_at - across * 0.8
	var destination := gate_at + across * 0.8
	# Isolate the production gate footprint from adjacent authored castle pieces;
	# this runner is about the gate's changing geometry, not the whole courtyard.
	for structure_id in sim.structure_ids():
		if structure_id != gate_id:
			sim.structures[structure_id]["position"] = Vector2(sim.structures[structure_id].get("position", Vector2.ZERO)) + Vector2(1000.0, 1000.0)
	sim._note_structure_table_mutation()
	sim.base_loop_enabled = false
	sim.winner = -1
	sim._add_battalion(FRIEND_ID, int(gate.get("team", 1)), gate_at, "Gate friend")
	sim._add_battalion(ENEMY_ID, 0 if int(gate.get("team", 1)) != 0 else 1, gate_at, "Gate enemy")
	for entity_id in sim.entity_ids():
		if entity_id not in [FRIEND_ID, ENEMY_ID]:
			sim.entities[entity_id]["position"] = gate_at + Vector2(1000.0 + float(entity_id % 31), 1000.0)
			sim.entities[entity_id]["route"] = []
			sim.entities[entity_id]["state"] = "idle"
	var friend: Dictionary = sim.entity(FRIEND_ID)
	var enemy: Dictionary = sim.entity(ENEMY_ID)
	enemy["health"] = 0
	_force_gate(gate, false)
	_check("friendly_request_inside_gate_opens", bool(sim.request_gate_open(gate_id, FRIEND_ID).get("ok", false)) and bool((gate.get("gate_behavior", {}) as Dictionary).get("open", false)))
	_force_gate(gate, false)
	_check("closed_gate_deflects_friendly_order", not _ordered_crosses(sim, friend, start, destination, across, int(gate.get("team", 1)), 20))
	_force_gate(gate, true)
	_check("open_gate_allows_friendly_order", _ordered_crosses(sim, friend, start, destination, across, int(gate.get("team", 1)), 20))
	friend["health"] = 0
	enemy["health"] = int(enemy.get("maximum_health", 1))
	_force_gate(gate, true)
	_check("erebor_gate_without_portal_allows_enemy_request", sim.gate_portal_allows(gate_id, ENEMY_ID))
	_check("erebor_open_gate_allows_enemy_order", _ordered_crosses(sim, enemy, start, destination, across, int(enemy.get("team", 0)), 20))
	_force_gate(gate, false)
	_check("erebor_closed_gate_blocks_enemy_order", not _ordered_crosses(sim, enemy, start, destination, across, int(enemy.get("team", 0)), 20))
	friend["health"] = int(friend.get("maximum_health", 1))
	enemy["health"] = 0
	_force_gate(gate, false)
	sim.request_gate_open(gate_id, FRIEND_ID)
	friend["position"] = gate_at + across * 20.0
	sim.advance(int((gate.get("gate_behavior", {}) as Dictionary).get("reset_ticks", 0)) + 1)
	_check("gate_closes_after_authored_reset", not bool((gate.get("gate_behavior", {}) as Dictionary).get("open", true)))
	gate["health"] = 0
	_check("destroyed_gate_allows_crossing", _ordered_crosses(sim, friend, start, destination, across, int(gate.get("team", 1)), 20))
	gate["health"] = int(gate.get("maximum_health", 1))
	_check("gate_resting_state_is_in_state_signature", _twin_toggle_signatures_track_one_sided_change(slice.source_map_data.simulation_configuration(), slice.gameplay_rules, gate.get("source_index", -1)))
	root.remove_child(slice)
	slice.free()
	await process_frame
	await _check_helms_deep_authored_gate(scene)
	_test_configuration_based_seam()
	await _test_toggle_gate_command()
	_finish()


func _force_gate(gate: Dictionary, open: bool) -> void:
	var policy: Dictionary = gate.get("gate_behavior", {})
	policy["open"] = open
	policy["pathing_open"] = open
	policy["open_fraction"] = 1.0 if open else 0.0
	policy["close_tick"] = -1
	gate["gate_behavior"] = policy


func _ordered_crosses(sim, unit: Dictionary, start: Vector2, destination: Vector2, across: Vector2, team: int, ticks: int) -> bool:
	sim.winner = -1
	unit["position"] = start
	unit["route"] = []
	unit["destination"] = start
	unit["state"] = "idle"
	unit["current_speed"] = 0.0
	unit["facing"] = across
	unit["group_speed_cap"] = 0.0
	var accepted: int = sim.issue_move([int(unit.get("id", 0))] as Array[int], destination, "order.move", team)
	if accepted != 1:
		return false
	sim.advance(ticks)
	var progress := (Vector2(unit.get("position", start)) - (start + destination) * 0.5).dot(across)
	return progress > 0.35


func _twin_toggle_signatures_track_one_sided_change(configuration: Dictionary, rules: Dictionary, source_index: int) -> bool:
	var SimScript: GDScript = load("res://src/retail_slice/retail_slice_sim.gd")
	var left = SimScript.new()
	var right = SimScript.new()
	left.setup(configuration, rules)
	right.setup(configuration, rules)
	var left_gate_id := 0
	var right_gate_id := 0
	for structure_id in left.structure_ids():
		if int(left.structure(structure_id).get("source_index", -2)) == source_index:
			left_gate_id = structure_id
			break
	for structure_id in right.structure_ids():
		if int(right.structure(structure_id).get("source_index", -2)) == source_index:
			right_gate_id = structure_id
			break
	if left_gate_id == 0 or right_gate_id == 0:
		return false
	_force_gate(left.structure(left_gate_id), not bool((left.structure(left_gate_id).get("gate_behavior", {}) as Dictionary).get("open", false)))
	if left.state_signature() == right.state_signature():
		return false
	_force_gate(right.structure(right_gate_id), bool((left.structure(left_gate_id).get("gate_behavior", {}) as Dictionary).get("open", false)))
	return left.state_signature() == right.state_signature()


func _check_helms_deep_authored_gate(scene: PackedScene) -> void:
	OS.set_environment("OPENBFME_SLICE_MAP", "rotwk.map.wor-helms-deep")
	var slice = scene.instantiate()
	root.add_child(slice)
	var deadline := Time.get_ticks_msec() + BOOT_DEADLINE_MS
	while Time.get_ticks_msec() < deadline:
		await process_frame
		if bool(slice.ready_ok) or String(slice.failure_reason) != "":
			break
	_check("helms_deep_slice_ready", bool(slice.ready_ok), String(slice.failure_reason))
	if not bool(slice.ready_ok):
		root.remove_child(slice)
		slice.free()
		return
	var sim = slice.simulation
	slice.set_process(false)
	slice.set_physics_process(false)
	sim.ai_enabled = false
	sim.base_loop_enabled = false
	var gate: Dictionary = {}
	for structure_id in sim.structure_ids():
		var row: Dictionary = sim.structure(structure_id)
		if String(row.get("castle_fixture_type", "")) == "RBHelmsDeepGateDoorBig":
			gate = row
			break
	_check("helms_deep_authored_gate_row_exists", not gate.is_empty())
	if gate.is_empty():
		root.remove_child(slice)
		slice.free()
		return
	# RotWK's effective Helm's Deep object authors 450x225 and both portal
	# allowances No.  The selected maps pack predates these fields, so the
	# runner injects those exact authored values onto the real placed gate row.
	gate["ai_gate_update"] = {"trigger_width_source": Vector2(450.0, 225.0)}
	gate["fake_pathfind_portal"] = {"allow_enemies": false, "allow_non_skirmish_ai": false}
	gate["gate_command_set"] = "CastleGateCommandSet_NoSell"
	gate["gate_command_rows"] = [
		{"slot": 1, "commandId": "Command_ToggleGate"},
		{"slot": 2, "commandId": "Command_StartSelfRepair"},
	]
	var toggle_entry: Dictionary = slice._gate_toggle_radial_entry(gate)
	slice.hud.sync_radial_commands(Vector2(320.0, 240.0), [toggle_entry] if not toggle_entry.is_empty() else [])
	_check(
		"authored_gate_command_set_shows_toggle_button",
		not toggle_entry.is_empty()
		and slice.hud.radial_command_count() == 1
		and String(toggle_entry.get("label_id", "")) == "CONTROLBAR:ToggleGateOpenClose"
		and String(toggle_entry.get("tooltip_id", "")) == "CONTROLBAR:ToolTipToggleGate"
		and String(toggle_entry.get("image_id", "")) == "BRWall_PosternGateOpenClose",
		str(toggle_entry)
	)
	var gate_without_command := gate.duplicate(true)
	gate_without_command.erase("gate_command_rows")
	var absent_entry: Dictionary = slice._gate_toggle_radial_entry(gate_without_command)
	slice.hud.sync_radial_commands(Vector2(320.0, 240.0), [absent_entry] if not absent_entry.is_empty() else [])
	_check("gate_without_authored_toggle_command_has_no_button", absent_entry.is_empty() and slice.hud.radial_command_count() == 0)
	var gate_id := int(gate.get("id", 0))
	var gate_at := Vector2(gate.get("position", Vector2.ZERO))
	for structure_id in sim.structure_ids():
		if structure_id != gate_id:
			sim.structures[structure_id]["position"] = Vector2(sim.structures[structure_id].get("position", Vector2.ZERO)) + Vector2(1000.0, 1000.0)
	sim._note_structure_table_mutation()
	sim._add_battalion(FRIEND_ID, int(gate.get("team", 1)), gate_at, "Gate friend")
	sim._add_battalion(ENEMY_ID, 0 if int(gate.get("team", 1)) != 0 else 1, gate_at, "Gate enemy")
	for entity_id in sim.entity_ids():
		if entity_id not in [FRIEND_ID, ENEMY_ID]:
			sim.entities[entity_id]["health"] = 0
	var friend: Dictionary = sim.entity(FRIEND_ID)
	var enemy: Dictionary = sim.entity(ENEMY_ID)
	enemy["health"] = 0
	var source_scale := float(slice.gameplay_rules.get("source_map_transform_scale", 0.0))
	_check("helms_deep_uses_positive_map_transform_scale", source_scale > 0.0, str(source_scale))
	var half_width := 450.0 * source_scale * 0.5
	_force_gate(gate, false)
	friend["position"] = gate_at + Vector2(half_width - 0.01, 0.0)
	sim.advance(1)
	_check("helms_deep_friend_inside_authored_trigger_opens", bool((gate.get("gate_behavior", {}) as Dictionary).get("open", false)))
	_force_gate(gate, false)
	friend["position"] = gate_at + Vector2(half_width + 0.01, 0.0)
	sim.advance(1)
	_check("helms_deep_friend_outside_authored_trigger_stays_closed", not bool((gate.get("gate_behavior", {}) as Dictionary).get("open", false)), "scale=%s half_width=%s" % [source_scale, half_width])
	# The live map happens to normalize to 0.1 today, the obsolete fallback's
	# value.  Probe a second valid transform scale so this check detects which
	# rule key the implementation actually reads instead of passing by accident.
	var probe_scale := source_scale * 0.5
	sim._rules["source_map_transform_scale"] = probe_scale
	_force_gate(gate, false)
	friend["position"] = gate_at + Vector2(450.0 * probe_scale * 0.5 + 0.01, 0.0)
	sim.advance(1)
	_check("helms_deep_trigger_reads_map_transform_scale", not bool((gate.get("gate_behavior", {}) as Dictionary).get("open", false)), "probe_scale=%s" % probe_scale)
	sim._rules["source_map_transform_scale"] = source_scale
	friend["position"] = gate_at
	_check("helms_deep_portal_allows_friendly", sim.gate_portal_allows(gate_id, FRIEND_ID))
	_check("helms_deep_portal_denies_enemy", not sim.gate_portal_allows(gate_id, ENEMY_ID))
	_force_gate(gate, false)
	_check("helms_deep_closed_gate_allows_friendly_open_request", bool(sim.request_gate_open(gate_id, FRIEND_ID).get("ok", false)))
	var across := Vector2.DOWN.rotated(float(gate.get("facing_radians", 0.0))).normalized()
	var start := gate_at - across * 0.8
	var destination := gate_at + across * 0.8
	friend["health"] = 0
	enemy["health"] = int(enemy.get("maximum_health", 1))
	_force_gate(gate, true)
	_check("helms_deep_open_portal_deflects_enemy_order", not _ordered_crosses(sim, enemy, start, destination, across, int(enemy.get("team", 0)), 20))
	root.remove_child(slice)
	slice.free()
	await process_frame



func _test_configuration_based_seam() -> void:
	# SEAM TEST (the one the maps republish activates): a sim built from a
	# CONFIGURATION whose castle_fixture_placements carry the full gate block
	# seeds through setup() -> _seed_castle_fixtures() (production path, no
	# row-key injection) and the structure row carries every field.
	var SimScript: GDScript = load("res://src/retail_slice/retail_slice_sim.gd")
	var sim = SimScript.new()
	var config: Dictionary = {
		"castle_fixture_placements": [
			{
				"type_name": "RBHelmsDeepGateDoorBig",
				"role": "gate",
				"kind_of": ["STRUCTURE", "IMMOBILE", "SELECTABLE", "WALL_GATE"],
				"source_index": 0,
				"position": Vector2(10.0, 10.0),
				"elevation": 0.0,
				"yaw": 0.0,
				"owner": "Player_1",
				"maximum_health": 20000.0,
				"armor": "DefaultWallArmor",
				"indestructible": false,
				# helmsdeepbuildings.ini (pure effective-assets): 6227 CommandSet,
				# 6274 GateOpenAndCloseBehavior, 6286-6288 portal, 6291-6293 AIGateUpdate.
				"gate": {
					"openByDefault": true,
					"resetMilliseconds": 12200.0,
					"percentOpenForPathing": 50.0,
					"geometries": {
						"Closed": {"major": 130.0, "minor": 7.5, "height": 140.0, "offset": [0.0, 0.0, 0.0]},
						"OpenLeft": {"major": 7.5, "minor": 60.0, "height": 140.0, "offset": [-115.0, 68.0, 0.0]},
						"OpenRight": {"major": 7.5, "minor": 60.0, "height": 140.0, "offset": [115.0, 68.0, 0.0]},
					},
					"commandSet": "CastleGateCommandSet_NoSell",
					"commandSetRows": [{"slot": 1, "commandId": "Command_ToggleGate"}],
					"aiGateUpdate": {"triggerWidthX": 450.0, "triggerWidthY": 225.0},
					"fakePathfindPortal": {"allowEnemies": false, "allowNonSkirmishAIUnits": false},
				},
			},
		],
	}
	var rules: Dictionary = {"enable_castle_fixtures": true, "source_map_transform_scale": 0.1}
	# _apply_map_configuration needs a full cooked map (spawns, three ford
	# gates...) or falls back and drops fixtures, so - as the garrison and
	# wall-defense runners do - install the validated PLACEMENT at the sim's
	# fixture seam and run the production seeder; no structure-row keys are
	# injected, the gate block travels placement -> row through the seeder.
	sim.setup({}, rules)
	sim._castle_fixture_placements = (config["castle_fixture_placements"] as Array).duplicate(true)
	sim._seed_castle_fixtures()
	var gate_found: Dictionary = {}
	for structure_id in sim.structure_ids():
		var row: Dictionary = sim.structure(structure_id)
		if String(row.get("structure_kind", "")) == "castle_fixture" and int(row.get("source_index", -2)) == 0:
			gate_found = row
			break
	_check("seam_config_gate_seeded", not gate_found.is_empty(), "seeded structures: %d" % sim.structure_ids().size())
	_check("seam_gate_carries_command_set", String(gate_found.get("gate_command_set", "")) == "CastleGateCommandSet_NoSell")
	var cmd_rows: Array = gate_found.get("gate_command_rows", []) as Array
	_check("seam_gate_carries_command_rows", cmd_rows.size() == 1)
	if not cmd_rows.is_empty():
		var row0: Dictionary = cmd_rows[0]
		_check("seam_first_row_has_toggle_gate", int(row0.get("slot", 0)) == 1 and String(row0.get("commandId", "")) == "Command_ToggleGate")
	_check("seam_gate_carries_ai_update", gate_found.has("ai_gate_update"))
	var ai_update: Dictionary = gate_found.get("ai_gate_update", {}) as Dictionary
	_check("seam_ai_update_has_450x225", Vector2(ai_update.get("trigger_width_source", Vector2.ZERO)) == Vector2(450.0, 225.0), "got %s" % str(ai_update))
	_check("seam_gate_carries_portal", gate_found.has("fake_pathfind_portal"))
	var portal: Dictionary = gate_found.get("fake_pathfind_portal", {}) as Dictionary
	_check("seam_portal_denies_enemies", portal.has("allow_enemies") and bool(portal.get("allow_enemies", true)) == false)
	_check("seam_portal_denies_non_skirmish_ai", portal.has("allow_non_skirmish_ai") and bool(portal.get("allow_non_skirmish_ai", true)) == false)
	# AllowNonSkirmishAIUnits is authored, cooked and stored but consumed by
	# nothing in the sim (no skirmish-AI unit flag exists): the gap is NAMED.
	var gaps: Array = gate_found.get("gate_portal_gaps", []) as Array
	_check("seam_allow_non_skirmish_ai_gap_named", gaps.has("AllowNonSkirmishAIUnits"), "gaps=%s" % str(gaps))
	_check("seam_gate_behavior_open_by_default", bool((gate_found.get("gate_behavior", {}) as Dictionary).get("open", false)))

func _test_toggle_gate_command() -> void:
	# TOGGLE_GATE TEST: on the Erebor live sim, submit toggle_gate via submit_command.
	# This test is already covered by the main Erebor flow, but explicitly verify the
	# command submission path exists.
	OS.set_environment("OPENBFME_SLICE_FACTION", "men")
	OS.set_environment("OPENBFME_SLICE_MAP", "rotwk.map.wor-erebor")
	var scene: PackedScene = load("res://scenes/retail_vertical_slice.tscn")
	var slice = scene.instantiate()
	root.add_child(slice)
	var deadline := Time.get_ticks_msec() + BOOT_DEADLINE_MS
	while Time.get_ticks_msec() < deadline:
		await process_frame
		if bool(slice.ready_ok) or String(slice.failure_reason) != "":
			break
	
	if not bool(slice.ready_ok):
		root.remove_child(slice)
		slice.free()
		return
	
	var sim = slice.simulation
	slice.set_process(false)
	slice.set_physics_process(false)
	sim.ai_enabled = false
	
	# Find the gate
	var gate: Dictionary = {}
	var gate_id := 0
	for structure_id in sim.structure_ids():
		var row: Dictionary = sim.structure(structure_id)
		if String(row.get("castle_fixture_type", "")) == "EreborGateDoors":
			gate = row
			gate_id = structure_id
			break
	
	if gate_id == 0:
		root.remove_child(slice)
		slice.free()
		return
	
	sim.base_loop_enabled = false
	sim._add_battalion(FRIEND_ID, int(gate.get("team", 1)), Vector2.ZERO, "Toggle friend")
	sim._add_battalion(ENEMY_ID, 0 if int(gate.get("team", 1)) != 0 else 1, Vector2.ZERO, "Toggle enemy")
	
	# Test toggle command on friendly team
	_force_gate(gate, false)
	var cmd = {"type": "toggle_gate", "args": {"structure_id": gate_id}, "tick": sim.tick_index + 1, "team": int(gate.get("team", 1)), "seq": 1}
	sim.submit_command(cmd)
	sim.advance(1)
	_check("toggle_gate_friendly_opens_gate", bool(gate.get("gate_behavior", {}).get("open", false)), str(sim.last_command_result))
	
	# Toggle again on the now-OPEN gate: closes it (manual close, close_tick -1)
	var cmd2 = {"type": "toggle_gate", "args": {"structure_id": gate_id}, "tick": sim.tick_index + 1, "team": int(gate.get("team", 1)), "seq": 2}
	sim.submit_command(cmd2)
	sim.advance(1)
	_check("toggle_gate_friendly_closes_gate", not bool(gate.get("gate_behavior", {}).get("open", false)) and not bool(gate.get("gate_behavior", {}).get("pathing_open", true)), str(sim.last_command_result))
	
	# Test toggle from enemy team
	var enemy_team = 0 if int(gate.get("team", 1)) != 0 else 1
	var cmd3 = {"type": "toggle_gate", "args": {"structure_id": gate_id}, "tick": sim.tick_index + 1, "team": enemy_team, "seq": 3}
	var before_state = bool(gate.get("gate_behavior", {}).get("open", false))
	sim.submit_command(cmd3)
	sim.advance(1)
	var after_state = bool(gate.get("gate_behavior", {}).get("open", false))
	_check("toggle_gate_enemy_denied", before_state == after_state, "before=%s after=%s result=%s" % [before_state, after_state, sim.last_command_result])
	
	# Test toggle on destroyed gate
	gate["health"] = 0
	var cmd4 = {"type": "toggle_gate", "args": {"structure_id": gate_id}, "tick": sim.tick_index + 1, "team": int(gate.get("team", 1)), "seq": 4}
	var before_destroyed = bool(gate.get("gate_behavior", {}).get("open", false))
	sim.submit_command(cmd4)
	sim.advance(1)
	var after_destroyed = bool(gate.get("gate_behavior", {}).get("open", false))
	_check("toggle_gate_destroyed_fails", before_destroyed == after_destroyed, "before=%s after=%s result=%s" % [before_destroyed, after_destroyed, sim.last_command_result])
	
	root.remove_child(slice)
	slice.free()



func _finish() -> void:
	print("CASTLE_GATE_RESULT passed=%d failed=%d" % [passed, failed])
	_watchdog.stop()
	quit(0 if failed == 0 else 1)
