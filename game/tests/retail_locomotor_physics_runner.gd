extends SceneTree
## SAGE legs-locomotor speed model (rules "retail_locomotor_physics"). Sealed
## kinematic fixture: no grid, structures, combat, RNG, or presentation.
##
## Fixture numbers are the compiled HumanLocomotor / GondorFighter values the
## turn-model runner already pins (locomotor.ini:142-152, gamedata.ini:7903 at
## the sim's 0.1 source scale): speed 5.5, acceleration/braking 51, TurnTime 500
## (720 deg/s). Turn-rate honouring needs the retail_formation_movement opt-in,
## so the turn case sets both flags.
##
## Run:
##   <godot> --headless --path game --script res://tests/retail_locomotor_physics_runner.gd

const SimScript := preload("res://src/retail_slice/retail_slice_sim.gd")
const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")

const EXPECTED_CHECKS := 7
const EPSILON := 0.0001

var passed := 0
var failed := 0
var _watchdog := RunnerWatchdogScript.new()


func _initialize() -> void:
	_watchdog.start(self, "RETAIL_LOCOMOTOR_PHYSICS_RUNNER")
	call_deferred("_run")


func _run() -> void:
	# 1. Flag off: the legacy ramp is byte-identical (pinned runners stay green).
	var legacy = _make_sim(false, false)
	var legacy_row: Dictionary = legacy.entities[1]
	legacy_row["route"] = [Vector2(100.0, 0.0)]
	legacy._step_route(legacy_row)
	_check_close(float(legacy_row["current_speed"]), 5.1, "legacy_ramp_unchanged_when_flag_absent")

	# 2. Straight line from rest: ramps at the authored acceleration.
	var sim = _make_sim(true, false)
	var row: Dictionary = sim.entities[1]
	row["facing"] = Vector2.RIGHT
	row["route"] = [Vector2(100.0, 0.0)]
	sim._step_route(row)
	_check_close(float(row["current_speed"]), 5.1, "straight_line_ramps_at_authored_acceleration")
	sim._step_route(row)
	# 5.1 + 5.1 would overshoot 5.5: clamped to exactly max speed.
	_check_close(float(row["current_speed"]), 5.5, "ramp_never_overshoots_max_speed")

	# 3. Braking distance. Infantry (braking 51) stops inside one 0.1s step, so
	#    the observable case is cavalry: HorseLocomotor speed 10, acceleration
	#    150, braking 200 (locomotor.ini:1026 at 0.1 scale) -> 0.5*10^2/20*1.05
	#    = 2.625 units, longer than the 1.0 unit cruise step. Expect at least one
	#    decelerating tick before the waypoint pops, then arrival.
	var horse = _make_sim(true, false, 10.0, 15.0, 20.0)
	var horse_row: Dictionary = horse.entities[1]
	horse_row["facing"] = Vector2.RIGHT
	horse_row["route"] = [Vector2(100.0, 0.0)]
	var peak := 0.0
	var braked_before_arrival := false
	for _tick in 200:
		var before := float(horse_row["current_speed"])
		horse._step_route(horse_row)
		peak = maxf(peak, before)
		if not horse_row["route"].is_empty() and float(horse_row["current_speed"]) < before - EPSILON:
			braked_before_arrival = true
		if horse_row["route"].is_empty():
			break
	_check(peak >= 10.0 - EPSILON, "cavalry_cruises_at_max_speed_before_braking", str(peak))
	_check(braked_before_arrival, "cavalry_brakes_inside_slow_down_distance")
	_check(horse_row["route"].is_empty(), "cavalry_arrives_at_destination", str(horse_row["position"]))

	# 4. Turn slowdown: 90 degrees off heading is beyond 45, so goal speed is 0
	#    and a cruising horde sheds speed at the authored braking ramp.
	var turning = _make_sim(true, true)
	var turning_row: Dictionary = turning.entities[1]
	turning_row["facing"] = Vector2.RIGHT
	turning_row["current_speed"] = 5.5
	turning_row["route"] = [Vector2(0.0, 100.0)]
	turning._step_route(turning_row)
	_check_close(float(turning_row["current_speed"]), 5.5 - 5.1, "turn_beyond_45_degrees_brakes_toward_zero")

	_finish()


func _make_sim(locomotor_physics: bool, formation_movement: bool, speed := 5.5, acceleration := 51.0, braking := 51.0):
	var rule := {
		"horde_id": "FixtureHorde",
		"category": "infantry",
		"speed": speed,
		"speed_source": speed * 10.0,
		"acceleration": acceleration,
		"acceleration_source": acceleration * 10.0,
		"braking": braking,
		"braking_source": braking * 10.0,
		"turn_rate_degrees_per_second": 720.0,
		"turn_rate_source": "locomotor:HumanLocomotor",
		"attack_range": 1.0,
		"attack_range_source": 10.0,
		"minimum_attack_range": 0.0,
		"minimum_attack_range_source": 0.0,
		"vision_range": 10.0,
		"vision_range_source": 100.0,
		"delay_between_shots_ms": 1000.0,
		"pre_attack_delay_ms": 0.0,
		"firing_duration_ms": 0.0,
		"attack_period_ticks": 10,
		"pre_attack_ticks": 0,
		"firing_duration_ticks": 0,
		"member_damage": 1,
		"member_health": 100,
		"member_count": 1,
		"formation_positions": [Vector3.ZERO],
		"provenance": {},
		"is_builder": false,
	}
	var sim = SimScript.new()
	var rules := {
		"enable_base_loop": true,
		# Q80: the core manifest tables are required or the whole rules
		# dictionary is refused and no flag is read.
		"faction_manifest": preload("res://src/retail_slice/retail_faction_manifest.gd").default_manifest(),
		"unit_rules": {
			SimScript.SOLDIER_OBJECT_ID: rule,
			SimScript.ARCHER_OBJECT_ID: rule,
			SimScript.TOWER_GUARD_OBJECT_ID: rule,
			SimScript.KNIGHT_OBJECT_ID: rule,
		},
	}
	if locomotor_physics:
		rules["retail_locomotor_physics"] = true
	if formation_movement:
		rules["retail_formation_movement"] = true
	sim.setup({}, rules)
	sim.ai_enabled = false
	sim.structures.clear()
	sim.expansion_pads.clear()
	sim.entities.clear()
	var unit_type := SimScript.SOLDIER_OBJECT_ID
	sim._add_battalion(1, SimScript.PLAYER_TEAM, Vector2.ZERO, unit_type, unit_type, unit_type, -1, rule)
	return sim


func _check(condition: bool, label: String, detail: String = "") -> void:
	if condition:
		passed += 1
		print("RETAIL_LOCOMOTOR_PHYSICS PASS %s" % label)
	else:
		failed += 1
		printerr("RETAIL_LOCOMOTOR_PHYSICS FAIL %s%s" % [label, "" if detail == "" else " (%s)" % detail])


func _check_close(actual: float, expected: float, label: String) -> void:
	_check(absf(actual - expected) <= EPSILON, label, "actual=%f expected=%f" % [actual, expected])


func _finish() -> void:
	if passed + failed != EXPECTED_CHECKS:
		failed += 1
		printerr("RETAIL_LOCOMOTOR_PHYSICS FAIL expected_checks expected=%d actual=%d" % [EXPECTED_CHECKS, passed + failed - 1])
	print("RETAIL_LOCOMOTOR_PHYSICS_RESULT passed=%d failed=%d" % [passed, failed])
	_watchdog.stop()
	quit(0 if failed == 0 else 1)
