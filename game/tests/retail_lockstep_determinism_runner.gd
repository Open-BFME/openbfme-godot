extends SceneTree

const SimScript = preload("res://src/retail_slice/retail_slice_sim.gd")
const CommandScript = preload("res://src/retail_slice/retail_command.gd")

var passed := 0
var failed := 0


const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
# Turns a GDScript runtime error inside `_run` — which unwinds past every
# `quit()` and would otherwise leave this headless process idling forever —
# into a loud non-zero exit. See tests/runner_watchdog.gd.
var _runner_watchdog := RunnerWatchdogScript.new()


func _initialize() -> void:
	_runner_watchdog.start(self, "RETAIL_LOCKSTEP_DETERMINISM_RUNNER")
	call_deferred("_run")


func _make_sim():
	var sim = SimScript.new()
	# The empty-argument setup contract reuses preconfigured rules. Seed a
	# compact deterministic fixture first so every scripted command exercises
	# real gameplay while the required setup({}, {}) call remains the entry.
	sim._rules = _harness_rules()
	sim.setup({}, {})
	sim.ai_enabled = true
	# Keep the 3000-tick proof bounded: one real producer is enough for the
	# production command, while the AI flag remains enabled for every tick.
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


func _command(tick: int, seq: int, type: String, args: Dictionary, team: int = 0) -> Dictionary:
	return {"tick": tick, "team": team, "seq": seq, "type": type, "args": args}


func _scripted_log() -> Array[Dictionary]:
	return [
		_command(1, 1, "issue_move", {"ids": [1], "destination": Vector2(-20.0, -14.0)}),
		_command(3, 2, "issue_attack_move", {"ids": [2], "destination": Vector2(0.0, 14.0)}),
		_command(5, 3, "issue_toggle_stance", {"ids": [1]}),
		_command(7, 4, "issue_construct", {"ids": [3], "structure_kind": "farm", "position": Vector2(-28.0, -8.0)}),
		_command(9, 5, "queue_unit", {"producer": 1003, "unit_type": "bfme2.object.gondor-fighter-horde"}),
		_command(15, 6, "issue_attack", {"ids": [1], "target_id": 101}),
		# Submitted in reverse sequence order below; seq=7 must apply before seq=8.
		_command(30, 8, "issue_stop", {"ids": [2]}),
		_command(30, 7, "issue_move", {"ids": [2], "destination": Vector2(-5.0, 12.0)}),
		_command(1600, 9, "issue_set_stance", {"ids": [1], "stance": "HoldGround"}),
		_command(2000, 10, "issue_set_formation", {"ids": [2], "formation": "Block"}),
		_command(2500, 11, "issue_stop", {"ids": [1, 2]}),
	]


func _submit_phase(sim, commands: Array[Dictionary], through_tick: int, after_tick: int = -1) -> bool:
	var accepted := true
	for cmd in commands:
		var command_tick := int(cmd["tick"])
		if command_tick <= through_tick and command_tick > after_tick:
			accepted = sim.submit_command(cmd) and accepted
	return accepted


func _run() -> void:
	var commands := _scripted_log()
	var sim_a = _make_sim()
	var sim_b = _make_sim()
	var altered = _make_sim()
	var altered_commands := commands.duplicate(true)
	(altered_commands[7] as Dictionary)["args"] = {"ids": [2], "destination": Vector2(12.0, -5.0)}
	var submissions_ok := (
		_submit_phase(sim_a, commands, 1500)
		and _submit_phase(sim_b, commands, 1500)
		and _submit_phase(altered, altered_commands, 1500)
	)
	var twin_ok := submissions_ok
	var sensitivity_ok := false
	var selection_ok := false
	var snapshot_ok := true
	var restored = null
	var snapshot_restored := false
	var first_twin_divergence := -1
	var first_snapshot_divergence := -1

	for expected_tick in range(1, 3001):
		sim_a.tick()
		sim_b.tick()
		altered.tick()
		if restored != null:
			restored.tick()

		var hash_a := sim_a.state_hash()
		var hash_b := sim_b.state_hash()
		if hash_a != hash_b and first_twin_divergence < 0:
			first_twin_divergence = expected_tick
			twin_ok = false
			printerr("RETAIL_LOCKSTEP first twin divergence tick=%d a=%s b=%s" % [expected_tick, hash_a, hash_b])
		if hash_a != altered.state_hash():
			sensitivity_ok = true

		if expected_tick == 1:
			var before_selection := hash_a
			sim_a.select_many([1, 2])
			selection_ok = before_selection == sim_a.state_hash()

		if expected_tick == 1500:
			var bytes := sim_a.snapshot()
			restored = _make_sim()
			snapshot_restored = restored.restore(bytes)
			snapshot_ok = snapshot_ok and snapshot_restored and restored.state_hash() == sim_a.state_hash()
			var remaining_ok := (
				_submit_phase(sim_a, commands, 3000, 1500)
				and _submit_phase(sim_b, commands, 3000, 1500)
				and _submit_phase(altered, altered_commands, 3000, 1500)
				and _submit_phase(restored, commands, 3000, 1500)
			)
			twin_ok = twin_ok and remaining_ok
			snapshot_ok = snapshot_ok and remaining_ok
		elif restored != null:
			var restored_hash: String = restored.state_hash()
			if restored_hash != hash_a and first_snapshot_divergence < 0:
				first_snapshot_divergence = expected_tick
				snapshot_ok = false
				printerr("RETAIL_LOCKSTEP first snapshot divergence tick=%d a=%s restored=%s" % [expected_tick, hash_a, restored_hash])

	var codec_ok := true
	for cmd in commands:
		var encoded := CommandScript.encode(cmd)
		var decoded := CommandScript.decode(encoded)
		codec_ok = codec_ok and not encoded.is_empty() and hash(decoded) == hash(cmd)

	_check("twin_run_determinism_through_tick_3000", twin_ok, "first_divergence=%d" % first_twin_divergence)
	_check("hash_sensitivity_to_altered_destination", sensitivity_ok)
	_check("selection_is_excluded_from_state_hash", selection_ok)
	_check("snapshot_round_trip_through_tick_3000", snapshot_ok and snapshot_restored, "first_divergence=%d" % first_snapshot_divergence)
	_check("command_codec_round_trip", codec_ok)
	print("RETAIL_LOCKSTEP_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("RETAIL_LOCKSTEP PASS %s" % name)
	else:
		failed += 1
		printerr("RETAIL_LOCKSTEP FAIL %s%s" % [name, " (%s)" % detail if detail != "" else ""])
