extends SceneTree

## Headless proof of the debug Game Control API: a control server wrapping a
## deterministic sim fixture is exercised end-to-end by a WebSocketPeer client
## in the same process (every method, plus malformed/unknown error paths).

const SimScript = preload("res://src/retail_slice/retail_slice_sim.gd")
const CommandScript = preload("res://src/retail_slice/retail_command.gd")
const ControlServerScript = preload("res://src/debug/retail_control_server.gd")

const PUMP_LIMIT := 4000

var passed := 0
var failed := 0

var _servers: Array = []
var _next_request_id := 0


func _initialize() -> void:
	call_deferred("_run")


func _make_sim():
	var sim = SimScript.new()
	# The empty-argument setup contract reuses preconfigured rules. Seed a
	# compact deterministic fixture first so every exercised method reads and
	# mutates real gameplay while the required setup({}, {}) call remains the
	# entry (same fixture as retail_lockstep_determinism_runner).
	sim._rules = _harness_rules()
	sim.setup({}, {})
	sim.ai_enabled = true
	for structure_id in sim.structure_ids():
		if structure_id != 1003:
			sim.structures.erase(structure_id)
	for entity_id in sim.entity_ids():
		if not [1, 2, 3, 101].has(entity_id):
			sim.entities.erase(entity_id)
	sim.expansion_pads.clear()
	sim._ai_production_plan.clear()
	sim._ai_build_order_index = SimScript.AI_BUILD_ORDER.size()
	sim._enemy_ai_construction_attempted = true
	sim._enemy_ai_construction_resolved = true
	return sim


func _harness_rules() -> Dictionary:
	return {
		"enable_base_loop": true,
		"starting_resources": 10000,
		"ai_attack_delay_ticks": 4000,
		"unit_rules": {
			SimScript.SOLDIER_OBJECT_ID: _unit_rule(SimScript.SOLDIER_HORDE_ID, false),
			SimScript.ARCHER_OBJECT_ID: _unit_rule(SimScript.ARCHER_OBJECT_ID, false),
			SimScript.TOWER_GUARD_OBJECT_ID: _unit_rule(SimScript.TOWER_GUARD_OBJECT_ID, false),
			SimScript.KNIGHT_OBJECT_ID: _unit_rule(SimScript.KNIGHT_OBJECT_ID, false),
			SimScript.BUILDER_OBJECT_ID: _unit_rule(SimScript.BUILDER_OBJECT_ID, true),
		},
	}


func _unit_rule(horde_id: String, is_builder: bool) -> Dictionary:
	return {
		"horde_id": horde_id,
		"speed": 1.0,
		"speed_source": 10.0,
		"acceleration": 1.0,
		"acceleration_source": 10.0,
		"turn_rate_degrees_per_second": 180.0,
		"braking": 1.0,
		"braking_source": 10.0,
		"attack_range": 1.15,
		"attack_range_source": 11.5,
		"minimum_attack_range": 0.0,
		"minimum_attack_range_source": 0.0,
		"vision_range": 40.0,
		"vision_range_source": 400.0,
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
		"is_builder": is_builder,
	}


func _pump(client: WebSocketPeer = null) -> void:
	for server in _servers:
		server.poll()
	if client != null:
		client.poll()


func _connect_client(port: int) -> WebSocketPeer:
	var client := WebSocketPeer.new()
	if client.connect_to_url("ws://127.0.0.1:%d" % port) != OK:
		return null
	for _iteration in range(PUMP_LIMIT):
		_pump(client)
		if client.get_ready_state() == WebSocketPeer.STATE_OPEN:
			return client
		if client.get_ready_state() == WebSocketPeer.STATE_CLOSED:
			return null
		OS.delay_msec(1)
	return null


func _await_reply(client: WebSocketPeer) -> Dictionary:
	for _iteration in range(PUMP_LIMIT):
		_pump(client)
		if client.get_available_packet_count() > 0:
			var parsed: Variant = JSON.parse_string(client.get_packet().get_string_from_utf8())
			return parsed as Dictionary if typeof(parsed) == TYPE_DICTIONARY else {}
		OS.delay_msec(1)
	return {}


func _rpc(client: WebSocketPeer, method: String, params: Dictionary = {}) -> Dictionary:
	_next_request_id += 1
	client.send_text(JSON.stringify({"id": _next_request_id, "method": method, "params": params}))
	var reply := _await_reply(client)
	if int(reply.get("id", -1)) != _next_request_id:
		return {}
	return reply


func _result(reply: Dictionary) -> Dictionary:
	var value: Variant = reply.get("result", {})
	return value as Dictionary if typeof(value) == TYPE_DICTIONARY else {}


func _entity_position(reply: Dictionary, entity_id: int) -> Vector2:
	for row_value in reply.get("result", []) as Array:
		var row := row_value as Dictionary
		if int(row.get("id", -1)) == entity_id:
			var point: Array = row.get("position", [])
			if point.size() == 2:
				return Vector2(float(point[0]), float(point[1]))
	return Vector2.INF


func _command(tick: int, seq: int, type: String, args: Dictionary, team: int = 0) -> Dictionary:
	return {"tick": tick, "team": team, "seq": seq, "type": type, "args": args}


func _run() -> void:
	var sim = _make_sim()
	var server = ControlServerScript.new(sim, true)
	var locked_server = ControlServerScript.new(sim, false)
	_servers = [server, locked_server]
	var started := server.start(0) == OK and locked_server.start(0) == OK
	_check("server_binds_ephemeral_loopback_ports", started and server.port > 0 and locked_server.port > 0)
	if not started:
		print("RETAIL_CONTROL_API_RESULT passed=%d failed=%d" % [passed, failed])
		quit(1)
		return
	var client := _connect_client(server.port)
	var locked_client := _connect_client(locked_server.port)
	_check("clients_complete_websocket_handshake", client != null and locked_client != null)
	if client == null or locked_client == null:
		print("RETAIL_CONTROL_API_RESULT passed=%d failed=%d" % [passed, failed])
		quit(1)
		return

	# state.summary
	var summary := _result(_rpc(client, "state.summary"))
	_check(
		"state_summary_reports_live_sim",
		int(summary.get("tick", -1)) == sim.tick_index
		and String(summary.get("hash", "")) == sim.state_hash()
		and int(summary.get("entity_count", -1)) == sim.entities.size()
		and int(summary.get("structure_count", -1)) == sim.structures.size()
		and int((summary.get("team_resources", {}) as Dictionary).get("0", -1)) == int(sim.team_resources[0])
	)

	# state.entities / state.structures
	var entities_reply := _rpc(client, "state.entities")
	var entity_rows: Array = entities_reply.get("result", []) as Array
	var first_entity: Dictionary = entity_rows[0] as Dictionary if not entity_rows.is_empty() else {}
	_check(
		"state_entities_lists_rows_with_expected_fields",
		entity_rows.size() == sim.entities.size()
		and first_entity.has("id") and first_entity.has("team") and first_entity.has("health")
		and first_entity.has("unit_type") and (first_entity.get("position", []) as Array).size() == 2
	)
	var structure_rows: Array = _rpc(client, "state.structures").get("result", []) as Array
	var first_structure: Dictionary = structure_rows[0] as Dictionary if not structure_rows.is_empty() else {}
	_check(
		"state_structures_lists_rows_with_expected_fields",
		structure_rows.size() == sim.structures.size()
		and int(first_structure.get("id", -1)) == 1003
		and first_structure.has("health") and first_structure.has("structure_kind")
		and (first_structure.get("position", []) as Array).size() == 2
	)

	# state.hash
	var hash_reply := _result(_rpc(client, "state.hash"))
	var hash_before_step := String(hash_reply.get("hash", ""))
	_check(
		"state_hash_matches_sim",
		int(hash_reply.get("tick", -1)) == sim.tick_index and hash_before_step == sim.state_hash()
	)

	# sim.step (allowed): advances tick and changes hash
	var step_result := _result(_rpc(client, "sim.step", {"ticks": 10}))
	_check(
		"sim_step_advances_tick_and_changes_hash",
		int(step_result.get("tick", -1)) == 10
		and sim.tick_index == 10
		and String(step_result.get("hash", "")) != hash_before_step
	)

	# sim.step bounds and refusal on the live-session server
	var oversized_reply := _rpc(client, "sim.step", {"ticks": 5000})
	_check("sim_step_rejects_out_of_bounds_ticks", oversized_reply.get("ok") == false)
	var refused_reply := _rpc(locked_client, "sim.step", {"ticks": 1})
	_check(
		"sim_step_refused_when_external_stepping_disallowed",
		refused_reply.get("ok") == false and sim.tick_index == 10
	)
	var live_snapshot := _result(_rpc(locked_client, "snapshot.save"))
	var restore_refused := _rpc(locked_client, "snapshot.restore", {"data": live_snapshot.get("data", "")})
	_check(
		"snapshot_restore_refused_when_external_stepping_disallowed",
		restore_refused.get("ok") == false
			and String(restore_refused.get("error", "")).begins_with("restore-refused")
			and sim.tick_index == 10
	)

	# command.submit: scheduled move accepted, applied at its tick
	var submit_result := _result(_rpc(client, "command.submit", {
		"command": _command(sim.tick_index + 5, 1, "issue_move", {"ids": [1], "destination": [-20.0, -14.0]}),
	}))
	_check("command_submit_accepts_valid_future_command", bool(submit_result.get("accepted", false)))
	var stale_submit := _result(_rpc(client, "command.submit", {
		"command": _command(0, 2, "issue_move", {"ids": [1], "destination": [-20.0, -14.0]}),
	}))
	_check("command_submit_rejects_past_tick", submit_result.get("accepted") == true and stale_submit.get("accepted") == false)
	var invalid_submit := _rpc(client, "command.submit", {
		"command": _command(sim.tick_index + 5, 3, "issue_teleport", {"ids": [1]}),
	})
	_check("command_submit_errors_on_unknown_command_type", invalid_submit.get("ok") == false)

	# command.apply: immediate move visibly relocates entity 2
	var before_apply := _entity_position(_rpc(client, "state.entities"), 2)
	var apply_result := _result(_rpc(client, "command.apply", {
		"command": _command(sim.tick_index, 4, "issue_move", {"ids": [2], "destination": [5.0, 12.0]}),
	}))
	_rpc(client, "sim.step", {"ticks": 20})
	var after_apply := _entity_position(_rpc(client, "state.entities"), 2)
	_check(
		"command_apply_moves_entity",
		bool(apply_result.get("applied", false))
		and before_apply != Vector2.INF and after_apply != Vector2.INF
		and before_apply.distance_to(after_apply) > 0.5
	)

	# snapshot.save / snapshot.restore round-trip
	var saved := _result(_rpc(client, "snapshot.save"))
	var saved_data := String(saved.get("data", ""))
	var saved_tick: int = sim.tick_index
	var saved_hash: String = sim.state_hash()
	_rpc(client, "sim.step", {"ticks": 50})
	var diverged: bool = sim.state_hash() != saved_hash
	var restore_result := _result(_rpc(client, "snapshot.restore", {"data": saved_data}))
	_check(
		"snapshot_save_restore_round_trips_hash",
		saved_data != "" and diverged
		and bool(restore_result.get("restored", false))
		and int(restore_result.get("tick", -1)) == saved_tick
		and String(restore_result.get("hash", "")) == saved_hash
		and sim.state_hash() == saved_hash
	)
	var bad_restore := _rpc(client, "snapshot.restore", {"data": "!!!not-base64!!!"})
	_check("snapshot_restore_errors_on_invalid_data", bad_restore.get("ok") == false)

	# discoverability
	var meta_types: Array = _result(_rpc(client, "meta.commands")).get("command_types", []) as Array
	var gaps_types: Array = _result(_rpc(client, "sim.gaps")).get("command_types", []) as Array
	_check(
		"meta_commands_and_sim_gaps_list_command_types",
		meta_types == Array(CommandScript.COMMAND_TYPES) and gaps_types == meta_types
	)

	# protocol error paths never crash
	client.send_text("{this is not json")
	var malformed_reply := _await_reply(client)
	_check(
		"malformed_json_yields_error_reply",
		malformed_reply.get("ok") == false and String(malformed_reply.get("error", "")) == "malformed-json"
	)
	var unknown_reply := _rpc(client, "state.nonsense")
	_check(
		"unknown_method_yields_error_reply",
		unknown_reply.get("ok") == false and String(unknown_reply.get("error", "")).begins_with("unknown-method")
	)
	var survived := _result(_rpc(client, "state.hash"))
	_check("server_survives_error_paths", String(survived.get("hash", "")) == sim.state_hash())

	client.close()
	locked_client.close()
	server.stop()
	locked_server.stop()
	print("RETAIL_CONTROL_API_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("RETAIL_CONTROL_API PASS %s" % name)
	else:
		failed += 1
		printerr("RETAIL_CONTROL_API FAIL %s%s" % [name, " (%s)" % detail if detail != "" else ""])
