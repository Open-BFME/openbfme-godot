extends SceneTree

const SimScript = preload("res://src/retail_slice/retail_slice_sim.gd")
const SessionScript = preload("res://src/retail_slice/retail_lockstep_session.gd")

var passed := 0
var failed := 0
var host_sim
var client_sim
var host_session
var client_session
var _sampled_hash_ticks: Dictionary = {}
var _network_hashes_equal := true
var _selection_hashes_equal := true


func _initialize() -> void:
	call_deferred("_run")


func _make_sim():
	var sim = SimScript.new()
	# This is the accepted Step-1 fixture verbatim: compact real gameplay,
	# deterministic setup({}, {}), and no presentation or scene-tree state.
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
	sim.force_ai_construction_complete()
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


func _run() -> void:
	host_sim = _make_sim()
	client_sim = _make_sim()
	host_sim.ai_enabled = false
	client_sim.ai_enabled = false
	host_session = SessionScript.new(host_sim)
	client_session = SessionScript.new(client_sim)
	var host_error: Error = host_session.host(0)
	var join_error: Error = client_session.join("127.0.0.1", host_session.bound_port) if host_error == OK else ERR_CANT_CONNECT
	var handshake_ok: bool = host_error == OK and join_error == OK and _pump_handshake(10000)
	_check("handshake_team_assignment", handshake_ok \
		and host_session.local_team == 0 and host_session.peer_team == 1 \
		and client_session.local_team == 1 and client_session.peer_team == 0)
	if not handshake_ok:
		_finish()
		return

	var move_destination := Vector2(-15.0, -10.0)
	var original_destination := Vector2(host_sim.entity(1).get("destination", Vector2.ZERO))
	var original_client_destination := Vector2(client_sim.entity(1).get("destination", Vector2.ZERO))
	host_session.submit_local("issue_move", {"ids": [1], "destination": move_destination})
	host_session.submit_local("issue_attack", {"ids": [2], "target_id": 101})
	host_session.submit_local("queue_unit", {"producer": 1003, "unit_type": SimScript.SOLDIER_HORDE_ID})
	client_session.submit_local("issue_move", {"ids": [101], "destination": Vector2(18.0, 14.0)})
	client_session.submit_local("issue_stop", {"ids": [101]})
	var destination_change_tick := -1
	var client_destination_change_tick := -1
	var run_ok := true
	var reached_tick_600 := false
	for _iteration in range(200000):
		_apply_divergent_selection()
		_pump_once_to_limit(600)
		if destination_change_tick < 0 \
			and not Vector2(host_sim.entity(1).get("destination", Vector2.ZERO)).is_equal_approx(original_destination):
			destination_change_tick = host_sim.tick_index
		if client_destination_change_tick < 0 \
			and not Vector2(client_sim.entity(1).get("destination", Vector2.ZERO)).is_equal_approx(original_client_destination):
			client_destination_change_tick = client_sim.tick_index
		_sample_equal_hash()
		if host_sim.tick_index >= 600 and client_sim.tick_index >= 600 \
			and host_session._hash_barrier_tick < 0 and client_session._hash_barrier_tick < 0:
			reached_tick_600 = true
			break
		if host_session.desynced or client_session.desynced:
			run_ok = false
			break
	if not reached_tick_600:
		run_ok = false
	var input_delay_ok: bool = destination_change_tick == host_session.input_delay_ticks \
		and client_destination_change_tick == client_session.input_delay_ticks
	_check("scripted_commands_hashes_through_tick_600", run_ok and host_sim.tick_index == 600 \
		and client_sim.tick_index == 600 and _network_hashes_equal and not host_session.desynced and not client_session.desynced)
	_check("input_delay_exact_execution_tick", input_delay_ok, "host_tick=%d client_tick=%d expected=%d" % [destination_change_tick, client_destination_change_tick, host_session.input_delay_ticks])

	var client_last_acknowledged_tick: int = host_session.remote_ack_tick
	var client_tick_before_stall: int = client_sim.tick_index
	for _iteration in range(1000):
		host_session.poll()
		host_session.advance_if_ready()
	var host_tick_after_stall: int = host_sim.tick_index
	var stall_bound_ok: bool = host_tick_after_stall <= client_last_acknowledged_tick + host_session.input_delay_ticks \
		and client_sim.tick_index == client_tick_before_stall
	var recovered := _pump_to_tick(660, 200000, true)
	_check("lockstep_stall_and_recovery", stall_bound_ok and recovered \
		and host_sim.state_hash() == client_sim.state_hash(),
		"host_tick=%d client_ack=%d client_tick=%d" % [host_tick_after_stall, client_last_acknowledged_tick, client_tick_before_stall])

	host_session.submit_local("pause", {})
	var pause_target: int = host_sim.tick_index + host_session.input_delay_ticks
	var reached_pause := _pump_to_tick(pause_target, 50000, false)
	var paused_tick: int = host_sim.tick_index
	var pause_same_tick: bool = reached_pause and host_sim.clock_paused and client_sim.clock_paused \
		and host_session.pause_command_tick == pause_target and client_session.pause_command_tick == pause_target
	for _iteration in range(250):
		_pump_once(true, true)
	var stayed_paused: bool = host_sim.tick_index == paused_tick and client_sim.tick_index == paused_tick
	client_session.submit_local("resume", {})
	var resume_target: int = client_sim.tick_index + client_session.input_delay_ticks
	var resumed := _pump_to_tick(resume_target + 60, 100000, false)
	var pause_resume_ok: bool = pause_same_tick and stayed_paused and resumed \
		and not host_sim.clock_paused and not client_sim.clock_paused \
		and host_session.pause_command_tick == resume_target and client_session.pause_command_tick == resume_target \
		and host_sim.state_hash() == client_sim.state_hash()
	_check("pause_resume_scheduled_lockstep", pause_resume_ok,
		"pause_tick=%d resume_tick=%d final_tick=%d" % [pause_target, resume_target, host_sim.tick_index])

	for _iteration in range(1000):
		host_session.poll()
		if host_session._remote_bundle_contiguous >= client_session._bundle_sent_through:
			break
	var spoof_tick: int = host_session._remote_bundle_contiguous + 1
	var spoof_rejections_before: int = host_session.rejected_remote_commands
	var spoof_hash_before: String = host_sim.state_hash()
	client_session._send_envelope({
		"kind": "bundle",
		"tick": spoof_tick,
		"commands": [{"tick": spoof_tick, "team": 0, "seq": 999999, "type": "pause", "args": {}}],
		"ack": 0,
	})
	client_session._connection.flush()
	for _iteration in range(1000):
		host_session.poll()
		if host_session.rejected_remote_commands > spoof_rejections_before:
			break
	var anti_spoof_ok: bool = host_session.rejected_remote_commands == spoof_rejections_before + 1 \
		and host_sim.state_hash() == spoof_hash_before and not host_sim.clock_paused
	_check("anti_spoof_wrong_team_rejected", anti_spoof_ok,
		"before=%d after=%d hash_equal=%s paused=%s spoof_tick=%d remote_contiguous=%d" % [
			spoof_rejections_before, host_session.rejected_remote_commands,
			str(host_sim.state_hash() == spoof_hash_before), str(host_sim.clock_paused),
			spoof_tick, host_session._remote_bundle_contiguous,
		])
	_check("selection_divergence_excluded_from_network_hash", _selection_hashes_equal and _network_hashes_equal)

	var corrupt_tick: int = host_sim.tick_index
	var corrupt_entity: Dictionary = client_sim.entity(1)
	corrupt_entity["position"] = Vector2(corrupt_entity.get("position", Vector2.ZERO)) + Vector2(7.0, -3.0)
	for _iteration in range(100000):
		_pump_once(true, true)
		if host_session.desynced and client_session.desynced:
			break
	var stopped_tick: int = host_sim.tick_index
	for _iteration in range(100):
		_pump_once(true, true)
	var desync_ok: bool = host_session.desynced and client_session.desynced \
		and host_session.desync_tick == client_session.desync_tick \
		and host_session.desync_tick > corrupt_tick \
		and host_session.desync_tick - corrupt_tick <= 30 \
		and host_sim.tick_index == stopped_tick and client_sim.tick_index == stopped_tick
	_check("desync_detection_and_stop", desync_ok,
		"corrupt_tick=%d desync_tick=%d" % [corrupt_tick, host_session.desync_tick])
	_finish()


func _pump_handshake(iteration_budget: int) -> bool:
	for iteration in range(iteration_budget):
		host_session.poll()
		client_session.poll()
		if host_session.handshake_complete and client_session.handshake_complete:
			return true
		if iteration % 100 == 99:
			OS.delay_msec(1)
	return false


func _pump_once(poll_host: bool, poll_client: bool) -> void:
	if poll_host:
		host_session.poll()
	if poll_client:
		client_session.poll()
	if poll_host:
		host_session.advance_if_ready()
	if poll_client:
		client_session.advance_if_ready()
	if poll_host:
		host_session.poll()
	if poll_client:
		client_session.poll()


func _pump_to_tick(target_tick: int, iteration_budget: int, track_selection: bool) -> bool:
	for _iteration in range(iteration_budget):
		if track_selection:
			_apply_divergent_selection()
		_pump_once_to_limit(target_tick)
		_sample_equal_hash()
		if host_session.desynced or client_session.desynced:
			return false
		if host_sim.tick_index >= target_tick and client_sim.tick_index >= target_tick \
			and host_session._hash_barrier_tick < 0 and client_session._hash_barrier_tick < 0:
			return true
	return false


func _apply_divergent_selection() -> void:
	var host_selection: Array[int] = [1, 2]
	var client_selection: Array[int] = [101]
	host_sim.select_many(host_selection)
	client_sim.select_many(client_selection)


func _pump_once_to_limit(target_tick: int) -> void:
	host_session.poll()
	client_session.poll()
	if host_sim.tick_index < target_tick:
		host_session.advance_if_ready()
	if client_sim.tick_index < target_tick:
		client_session.advance_if_ready()
	host_session.poll()
	client_session.poll()


func _sample_equal_hash() -> void:
	if host_sim.tick_index != client_sim.tick_index or host_sim.tick_index <= 0 or host_sim.tick_index % 30 != 0:
		return
	if _sampled_hash_ticks.has(host_sim.tick_index):
		return
	_sampled_hash_ticks[host_sim.tick_index] = true
	_network_hashes_equal = _network_hashes_equal and host_sim.state_hash() == client_sim.state_hash()


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("RETAIL_LOCKSTEP_NET PASS %s" % name)
	else:
		failed += 1
		printerr("RETAIL_LOCKSTEP_NET FAIL %s%s" % [name, " (%s)" % detail if detail != "" else ""])


func _finish() -> void:
	if host_session != null:
		host_session.close()
	if client_session != null:
		client_session.close()
	print("RETAIL_LOCKSTEP_NET_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)
