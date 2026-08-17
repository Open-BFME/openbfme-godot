extends SceneTree

## Tier-1 dual-run oracle, GDScript half: runs a scripted scenario on the
## retail sim fixture (same _make_sim approach as
## retail_lockstep_determinism_runner.gd) and records a SEMANTIC trace —
## team resources, entity counts, per-entity position/health every
## SAMPLE_INTERVAL ticks — plus the command log and the scenario constants the
## C# OpenBfme.Sim replay (DualRunOracleTests.cs) mirrors. The trace is written
## to workspace/retail-work/reports/dualrun/gdscript_trace.json.
##
## Scope honesty: the C# engine implements a small module subset, so the
## scenario stays inside the genuine overlap — farm income, unit training
## completion, straight-line movement — and the trace records exact
## constants (income interval/amount, build ticks, speed) so divergences are
## semantic findings, not scenario-construction noise.

const SimScript = preload("res://src/retail_slice/retail_slice_sim.gd")
const CommandScript = preload("res://src/retail_slice/retail_command.gd")

const TOTAL_TICKS := 600
const SAMPLE_INTERVAL := 10
const MOVE_COMMAND_TICK := 5
const MOVE_DESTINATION := Vector2(-20.0, -14.0)
const QUEUE_COMMAND_TICK := 9
const GRANT_AFTER_TICK := 20
const GRANT_AMOUNT := 500

var passed := 0
var failed := 0


const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
# Turns a GDScript runtime error inside `_run` — which unwinds past every
# `quit()` and would otherwise leave this headless process idling forever —
# into a loud non-zero exit. See tests/runner_watchdog.gd.
var _runner_watchdog := RunnerWatchdogScript.new()


func _initialize() -> void:
	_runner_watchdog.start(self, "RETAIL_DUALRUN_TRACE_RUNNER")
	call_deferred("_run")


func _make_sim():
	var sim = SimScript.new()
	# The empty-argument setup contract reuses preconfigured rules. Seed a
	# compact deterministic fixture first so every scripted command exercises
	# real gameplay while the required setup({}, {}) call remains the entry.
	sim._rules = _harness_rules()
	sim.setup({}, {})
	sim.ai_enabled = true
	# Keep the fixture bounded: the player farm (1002) exercises the income
	# lane and the player barracks (1003) the production lane; the AI flag
	# remains enabled for every tick (its plan below is neutered).
	for structure_id in sim.structure_ids():
		if not [1002, 1003].has(structure_id):
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
		_command(MOVE_COMMAND_TICK, 1, "issue_move", {"ids": [1], "destination": MOVE_DESTINATION}),
		_command(QUEUE_COMMAND_TICK, 2, "queue_unit", {"producer": 1003, "unit_type": SimScript.SOLDIER_HORDE_ID}),
	]


func _round3(value: float) -> float:
	return roundf(value * 1000.0) / 1000.0


func _sample(sim, tick: int) -> Dictionary:
	var rows: Array = []
	for id in sim.entity_ids():
		var row: Dictionary = sim.entities[id]
		var position := Vector2(row["position"])
		rows.append({
			"id": id,
			"team": int(row["team"]),
			"x": _round3(position.x),
			"y": _round3(position.y),
			"health": int(row["health"]),
		})
	return {
		"tick": tick,
		"team_resources": {
			"0": sim.resources_for_team(SimScript.PLAYER_TEAM),
			"1": sim.resources_for_team(SimScript.ENEMY_TEAM),
		},
		"entity_count": sim.entity_ids().size(),
		"entities": rows,
	}


func _serialized_commands(commands: Array[Dictionary]) -> Array:
	var rows: Array = []
	for cmd in commands:
		var args: Dictionary = cmd["args"]
		var serialized_args: Dictionary = {}
		for key in args.keys():
			var value: Variant = args[key]
			if typeof(value) == TYPE_VECTOR2:
				serialized_args[key] = {"x": (value as Vector2).x, "y": (value as Vector2).y}
			else:
				serialized_args[key] = value
		rows.append({
			"tick": int(cmd["tick"]),
			"team": int(cmd["team"]),
			"seq": int(cmd["seq"]),
			"type": String(cmd["type"]),
			"args": serialized_args,
		})
	# The resource grant is out-of-band: the retail command vocabulary has no
	# grant command, so the harness credits team 0 directly AFTER the
	# GRANT_AFTER_TICK sample is taken. The C# replay mirrors the same
	# boundary with SimWorld.AddTeamResources.
	rows.append({
		"tick": GRANT_AFTER_TICK,
		"team": 0,
		"seq": 3,
		"type": "grant_resources",
		"args": {"amount": GRANT_AMOUNT},
		"out_of_band": true,
		"applied_after_tick_and_sample": true,
	})
	return rows


func _scenario_constants(sim) -> Dictionary:
	var soldier_rule: Dictionary = (_harness_rules()["unit_rules"] as Dictionary)[SimScript.SOLDIER_OBJECT_ID]
	var structures_rows: Array = []
	for id in sim.structure_ids():
		var row: Dictionary = sim.structures[id]
		structures_rows.append({
			"id": id,
			"team": int(row["team"]),
			"structure_kind": String(row["structure_kind"]),
			"x": _round3(Vector2(row["position"]).x),
			"y": _round3(Vector2(row["position"]).y),
			"maximum_health": int(row["maximum_health"]),
			"income_per_payout": int(row.get("income_per_payout", 0)),
		})
	return {
		"tick_seconds": SimScript.TICK_SECONDS,
		"starting_resources": int(sim._rules.get("starting_resources", 0)),
		"farm_payout_interval_ticks": maxi(1, int(sim._rules.get("farm_payout_ticks", 50))),
		"farm_income_per_payout": int((sim.structures[1002] as Dictionary).get("income_per_payout", 0)),
		"unit": {
			"unit_type": SimScript.SOLDIER_HORDE_ID,
			"cost": sim._production_rule_value(SimScript.SOLDIER_HORDE_ID, "cost_rule", "default_cost"),
			"build_ticks": sim._production_rule_value(SimScript.SOLDIER_HORDE_ID, "build_ticks_rule", "default_build_ticks"),
			"speed_units_per_second": float(soldier_rule["speed"]),
			"acceleration_units_per_second_squared": float(soldier_rule["acceleration"]),
			"braking_units_per_second_squared": float(soldier_rule["braking"]),
			"member_health": int(soldier_rule["member_health"]),
			"member_count": int(soldier_rule["member_count"]),
		},
		"structures": structures_rows,
		"semantics_notes": [
			"income: paid during ticks where tick_index % interval == 0, per completed farm",
			"training: queue_unit charges cost at the command tick; the battalion spawns during tick complete_tick = command_tick + build_ticks",
			"movement: accelerating integrator (current_speed += accel*dt up to speed, step = current_speed*dt, braking on final leg); the C# LinearMover is constant-speed, so positions are tolerance-compared, not exact",
		],
	}


func _initial_state(sim) -> Dictionary:
	var rows: Array = []
	for id in sim.entity_ids():
		var row: Dictionary = sim.entities[id]
		var position := Vector2(row["position"])
		rows.append({
			"id": id,
			"team": int(row["team"]),
			"object_id": String(row["object_id"]),
			"x": _round3(position.x),
			"y": _round3(position.y),
			"health": int(row["health"]),
		})
	return {"entities": rows}


func _trace_output_path() -> String:
	var game_dir := ProjectSettings.globalize_path("res://").rstrip("/")
	var repo_root := game_dir.get_base_dir()
	return repo_root.path_join("workspace/retail-work/reports/dualrun/gdscript_trace.json")


func _expected_player_resources(constants: Dictionary, tick: int) -> int:
	# Closed-form oracle for the runner's own consistency gate: starting
	# resources, minus the training cost (charged during QUEUE_COMMAND_TICK),
	# plus the out-of-band grant (visible only after the GRANT_AFTER_TICK
	# sample), plus one farm payout per elapsed interval.
	var unit: Dictionary = constants["unit"]
	var interval := int(constants["farm_payout_interval_ticks"])
	var value := int(constants["starting_resources"])
	if tick >= QUEUE_COMMAND_TICK:
		value -= int(unit["cost"])
	if tick > GRANT_AFTER_TICK:
		value += GRANT_AMOUNT
	value += int(constants["farm_income_per_payout"]) * int(float(tick) / float(interval))
	return value


func _run() -> void:
	var sim = _make_sim()
	var commands := _scripted_log()
	var submissions_ok := true
	for cmd in commands:
		submissions_ok = sim.submit_command(cmd) and submissions_ok
	_check("scripted_commands_accepted", submissions_ok)

	var constants := _scenario_constants(sim)
	var initial := _initial_state(sim)
	var samples: Array = [_sample(sim, 0)]
	for tick in range(1, TOTAL_TICKS + 1):
		sim.tick()
		if tick % SAMPLE_INTERVAL == 0:
			samples.append(_sample(sim, tick))
		if tick == GRANT_AFTER_TICK:
			# Out-of-band grant, applied after this tick's sample was taken.
			sim.team_resources[SimScript.PLAYER_TEAM] = sim.resources_for_team(SimScript.PLAYER_TEAM) + GRANT_AMOUNT

	var trace := {
		"schema": "openbfme.dualrun-trace",
		"version": 1,
		"total_ticks": TOTAL_TICKS,
		"sample_interval": SAMPLE_INTERVAL,
		"teams": [SimScript.PLAYER_TEAM, SimScript.ENEMY_TEAM],
		"constants": constants,
		"initial": initial,
		"commands": _serialized_commands(commands),
		"samples": samples,
	}

	var output := _trace_output_path()
	var parent := output.get_base_dir()
	var directory_ok := DirAccess.make_dir_recursive_absolute(parent) == OK
	_check("trace_directory_created", directory_ok, parent)
	var written := false
	if directory_ok:
		var file := FileAccess.open(output, FileAccess.WRITE)
		if file != null:
			file.store_string(JSON.stringify(trace, "  ", false) + "\n")
			file.close()
			written = true
	_check("trace_file_written", written, output)

	# Round-trip: the written trace must parse back as JSON.
	var round_trip_ok := false
	if written:
		var read_back := FileAccess.get_file_as_string(output)
		round_trip_ok = typeof(JSON.parse_string(read_back)) == TYPE_DICTIONARY
	_check("trace_file_parses_back", round_trip_ok)

	# Internal consistency: the recorded samples must match the closed-form
	# expectations implied by the recorded constants — otherwise the trace
	# would ship a broken oracle to the C# side.
	var resources_ok := true
	var enemy_resources_ok := true
	var counts_ok := true
	var first_resource_mismatch := -1
	var spawn_tick := QUEUE_COMMAND_TICK + int((constants["unit"] as Dictionary)["build_ticks"])
	for sample_value in samples:
		var sample := sample_value as Dictionary
		var tick := int(sample["tick"])
		var recorded := int((sample["team_resources"] as Dictionary)["0"])
		var expected := _expected_player_resources(constants, tick)
		if recorded != expected:
			resources_ok = false
			if first_resource_mismatch < 0:
				first_resource_mismatch = tick
				printerr("RETAIL_DUALRUN resource mismatch tick=%d recorded=%d expected=%d" % [tick, recorded, expected])
		if int((sample["team_resources"] as Dictionary)["1"]) != int(constants["starting_resources"]):
			enemy_resources_ok = false
		var expected_count := 4 + (1 if tick >= spawn_tick else 0)
		if int(sample["entity_count"]) != expected_count:
			counts_ok = false
			printerr("RETAIL_DUALRUN count mismatch tick=%d recorded=%d expected=%d" % [tick, int(sample["entity_count"]), expected_count])
	_check("player_resource_curve_matches_constants", resources_ok, "first_mismatch=%d" % first_resource_mismatch)
	_check("enemy_resources_constant", enemy_resources_ok)
	_check("entity_count_curve_matches_constants", counts_ok, "spawn_tick=%d" % spawn_tick)

	var mover: Dictionary = sim.entities[1]
	var arrived := Vector2(mover["position"]).distance_to(MOVE_DESTINATION) <= 0.01
	_check("move_order_arrived_at_destination", arrived, str(mover["position"]))

	print("RETAIL_DUALRUN_RESULT passed=%d failed=%d trace=%s" % [passed, failed, output])
	quit(0 if failed == 0 else 1)


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("RETAIL_DUALRUN PASS %s" % name)
	else:
		failed += 1
		printerr("RETAIL_DUALRUN FAIL %s%s" % [name, " (%s)" % detail if detail != "" else ""])
