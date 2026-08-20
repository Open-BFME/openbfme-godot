extends SceneTree
## Retail heading-bounded ground movement. This is a sealed kinematic fixture:
## no path grid, structures, combat, RNG, or presentation can affect it.
##
## Retail-authored sources
## (workspace/retail-work/editions/rotwk/cache/effective-assets/data/ini):
##   object/goodfaction/units/men/eomer.ini:861-871
##     mounted RohanEomer binds HeroHorseLocomotor at
##     NORMAL_CAVALRY_FAST_HORDE_SPEED.
##   gamedata.ini:7817
##     NORMAL_CAVALRY_FAST_HORDE_SPEED = 90.
##   locomotor.ini:943-970
##     HeroHorseLocomotor TurnTime = 1500 (240 deg/s), Acceleration = 1500,
##     SlowTurnRadius = 0, Braking = 2000, MinTurnSpeed = 10%.
##   object/evilfaction/units/evilmen/mumakil.ini:1739-1743
##     MordorMumakil binds MumakilLocomotor at speed 50.
##   locomotor.ini:1347-1367
##     MumakilLocomotor TurnTime = 9000 (40 deg/s), Acceleration = 750,
##     SlowTurnRadius = 2, Braking = 1000, MinTurnSpeed = 12%.
##   object/goodfaction/units/men/gondorfighter.ini:851-855
##     GondorFighter binds HumanLocomotor at NORMAL_FOOT_MED_MEMBER_SPEED.
##   gamedata.ini:7903
##     NORMAL_FOOT_MED_MEMBER_SPEED = 55.
##   locomotor.ini:142-152
##     HumanLocomotor TurnTime = 500 (720 deg/s), Acceleration/Braking = 510,
##     MinTurnSpeed = 0%.
##
## The sim's authored source-unit scale is 0.1, so the sealed runtime values are
## 9/150/200 and 5.5/51/51 respectively. No movement number below is invented.

const SimScript := preload("res://src/retail_slice/retail_slice_sim.gd")
const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")

const EXPECTED_CHECKS := 17
const EPSILON := 0.0001

var passed := 0
var failed := 0
var _watchdog := RunnerWatchdogScript.new()


class HalfPlaneNavigation extends RefCounted:
	func is_local_inside_navigation(position: Vector2) -> bool:
		return position.y >= 0.0


class RecoveringNavigation extends RefCounted:
	func is_local_inside_navigation(position: Vector2) -> bool:
		return position.x >= 1.0

	func resolve_walkable_position(_position: Vector2) -> Vector2:
		return Vector2(1.0, 0.0)

	func query_route(_from: Vector2, to: Vector2) -> Dictionary:
		return {"valid": true, "reason": "", "points": [to], "cells": [], "ford_name": ""}


class RecordingParity extends RefCounted:
	var allow := true
	var calls := 0
	var last_from := Vector2.ZERO

	func can_path_between(from: Vector2, _to: Vector2) -> bool:
		calls += 1
		last_from = from
		return allow


func _initialize() -> void:
	_watchdog.start(self, "RETAIL_TURN_MODEL_RUNNER")
	call_deferred("_run")


func _run() -> void:
	var cavalry := _exercise_reverse(_mounted_eomer_rule(), "RohanEomer")
	var cavalry_limit := 240.0 * SimScript.TICK_SECONDS
	_check(
		"mounted_eomer_does_not_flip_on_reverse",
		float(cavalry["first_heading_delta_degrees"]) < 180.0 - EPSILON,
		str(cavalry)
	)
	_check_close(
		float(cavalry["first_heading_delta_degrees"]),
		cavalry_limit,
		"mounted_eomer_heading_is_bounded_by_authored_turn_rate"
	)
	_check(
		"mounted_eomer_velocity_follows_facing",
		float(cavalry["minimum_displacement_facing_dot"]) >= 1.0 - EPSILON,
		str(cavalry)
	)
	_check(
		"mounted_eomer_reverse_traces_an_arc",
		float(cavalry["maximum_lateral_excursion"]) > EPSILON,
		"samples=%s" % str(cavalry["samples"])
	)
	_check(
		"mounted_eomer_arc_has_multiple_position_samples",
		(cavalry["samples"] as Array).size() == 5,
		str(cavalry["samples"])
	)

	var infantry := _exercise_reverse(_gondor_fighter_rule(), "GondorFighter")
	var infantry_limit := 720.0 * SimScript.TICK_SECONDS
	_check_close(
		float(infantry["first_heading_delta_degrees"]),
		infantry_limit,
		"gondor_fighter_heading_is_bounded_by_authored_turn_rate"
	)
	_check(
		"gondor_fighter_velocity_follows_facing",
		float(infantry["minimum_displacement_facing_dot"]) >= 1.0 - EPSILON,
		str(infantry)
	)
	_check(
		"slow_infantry_turns_tighter_than_mounted_eomer",
		float(infantry["first_turn_radius"]) < float(cavalry["first_turn_radius"]),
		"infantry=%.6f cavalry=%.6f" % [
			float(infantry["first_turn_radius"]), float(cavalry["first_turn_radius"])
		]
	)

	var fallback := _exercise_missing_turn_rate_fallback()
	_check(
		"missing_turn_rate_keeps_pre_change_facing_snap",
		Vector2(fallback["facing"]).dot(Vector2.LEFT) >= 1.0 - EPSILON,
		str(fallback)
	)
	_check(
		"missing_turn_rate_keeps_pre_change_direct_translation",
		Vector2(fallback["displacement"]).normalized().dot(Vector2.LEFT) >= 1.0 - EPSILON,
		str(fallback)
	)

	var offsets := [Vector3(-1.0, 0.0, -1.0), Vector3(1.0, 0.0, 1.0)]
	var horde_rule := _gondor_fighter_rule()
	horde_rule["formation_positions"] = offsets.duplicate()
	horde_rule["member_count"] = 2
	var horde_sim = _make_sim(horde_rule, "GondorFighterHorde")
	var horde: Dictionary = horde_sim.entities[1]
	horde["facing"] = Vector2.RIGHT
	horde["current_speed"] = float(horde["speed"])
	horde["route"] = [Vector2(-100.0, 0.0)]
	horde_sim._step_route(horde)
	_check(
		"horde_heading_turn_keeps_member_offsets",
		(horde["formation_positions"] as Array) == offsets,
		str(horde["formation_positions"])
	)

	var edge_sim = _make_sim(_mounted_eomer_rule(), "RohanEomerAtNavigationEdge")
	edge_sim.route_provider = HalfPlaneNavigation.new()
	var edge_row: Dictionary = edge_sim.entities[1]
	edge_row["facing"] = Vector2(0.0, -1.0)
	edge_row["current_speed"] = float(edge_row["speed"])
	edge_row["route"] = [Vector2(-100.0, 0.0)]
	edge_sim._step_route(edge_row)
	_check(
		"heading_bounded_ground_move_stays_on_walkable_navigation",
		Vector2(edge_row["position"]).is_equal_approx(Vector2.ZERO),
		str(edge_row["position"])
	)
	_check_close(
		float(edge_row["current_speed"]),
		0.0,
		"navigation_edge_uses_authored_braking_while_turning"
	)

	var eomer_arrival := _exercise_lateral_arrival(_mounted_eomer_rule(), "RohanEomerMounted")
	_check(
		"mounted_eomer_lateral_waypoint_arrives_and_idles",
		bool(eomer_arrival["arrived"]) and String(eomer_arrival["state"]) == "idle",
		str(eomer_arrival)
	)
	var mumakil_arrival := _exercise_lateral_arrival(_mumakil_rule(), "MordorMumakil")
	_check(
		"mumakil_lateral_waypoint_arrives_and_idles",
		bool(mumakil_arrival["arrived"]) and String(mumakil_arrival["state"]) == "idle",
		str(mumakil_arrival)
	)

	_exercise_blocked_origin_route_contract()

	_finish()


func _exercise_lateral_arrival(rule: Dictionary, unit_type: String) -> Dictionary:
	var sim = _make_sim(rule, unit_type)
	var row: Dictionary = sim.entities[1]
	row["facing"] = Vector2.RIGHT
	row["current_speed"] = 0.0
	row["route"] = [Vector2(0.0, 2.0)]
	row["state"] = "run"
	var done_tick := -1
	for tick_index in range(1, 101):
		sim._step_route(row)
		if (row["route"] as Array).is_empty():
			done_tick = tick_index
			break
	return {
		"arrived": done_tick > 0,
		"done_tick": done_tick,
		"route_size": (row["route"] as Array).size(),
		"state": String(row["state"]),
		"position": Vector2(row["position"]),
	}


func _exercise_blocked_origin_route_contract() -> void:
	var sim = _make_sim(_mounted_eomer_rule(), "BlockedOriginRoute")
	var row: Dictionary = sim.entities[1]
	var provider := RecoveringNavigation.new()
	var parity := RecordingParity.new()
	sim.route_provider = provider
	sim.parity = parity
	var accepted := bool(sim._assign_route(row, Vector2(10.0, 0.0)))
	_check(
		"blocked_origin_routes_from_provider_recovery_through_parity",
		accepted and parity.calls == 1 and parity.last_from.is_equal_approx(Vector2(1.0, 0.0)),
		"accepted=%s calls=%d from=%s" % [accepted, parity.calls, parity.last_from]
	)
	parity.allow = false
	parity.calls = 0
	var refused := not bool(sim._assign_route(row, Vector2(10.0, 0.0)))
	_check(
		"blocked_origin_route_is_refused_when_recovered_path_fails_parity",
		refused and parity.calls == 1 and String(sim.last_route_rejection) == "parity-path-impassable",
		"refused=%s calls=%d reason=%s" % [refused, parity.calls, sim.last_route_rejection]
	)


func _exercise_reverse(rule: Dictionary, unit_type: String) -> Dictionary:
	var sim = _make_sim(rule, unit_type)
	var row: Dictionary = sim.entities[1]
	row["facing"] = Vector2.RIGHT
	row["current_speed"] = float(row["speed"])
	row["route"] = [Vector2(-100.0, 0.0)]
	var samples: Array[Vector2] = [Vector2(row["position"])]
	var first_heading_delta_degrees := 0.0
	var first_turn_radius := 0.0
	var minimum_dot := 1.0
	var maximum_lateral := 0.0
	for sample_index in 4:
		var before_position := Vector2(row["position"])
		var before_facing := Vector2(row["facing"]).normalized()
		sim._step_route(row)
		var after_position := Vector2(row["position"])
		var after_facing := Vector2(row["facing"]).normalized()
		var displacement := after_position - before_position
		var heading_delta := absf(wrapf(after_facing.angle() - before_facing.angle(), -PI, PI))
		if sample_index == 0:
			first_heading_delta_degrees = rad_to_deg(heading_delta)
			first_turn_radius = displacement.length() / heading_delta
		if displacement.length_squared() > EPSILON * EPSILON:
			minimum_dot = minf(minimum_dot, displacement.normalized().dot(after_facing))
		maximum_lateral = maxf(maximum_lateral, absf(after_position.y))
		samples.append(after_position)
	return {
		"first_heading_delta_degrees": first_heading_delta_degrees,
		"first_turn_radius": first_turn_radius,
		"minimum_displacement_facing_dot": minimum_dot,
		"maximum_lateral_excursion": maximum_lateral,
		"samples": samples,
	}


func _exercise_missing_turn_rate_fallback() -> Dictionary:
	var rule := _base_rule()
	rule["speed"] = 5.5
	rule["speed_source"] = 55.0
	rule["acceleration"] = 51.0
	rule["acceleration_source"] = 510.0
	rule["braking"] = 51.0
	rule["braking_source"] = 510.0
	var sim = _make_sim(rule, "FixtureMissingTurnRate")
	var row: Dictionary = sim.entities[1]
	row["facing"] = Vector2.RIGHT
	row["current_speed"] = float(row["speed"])
	row["route"] = [Vector2(-100.0, 0.0)]
	var before := Vector2(row["position"])
	sim._step_route(row)
	return {"facing": row["facing"], "displacement": Vector2(row["position"]) - before}


func _make_sim(rule: Dictionary, unit_type: String):
	var sim = SimScript.new()
	var rules := {
		"enable_base_loop": true,
		"unit_rules": {
			SimScript.SOLDIER_OBJECT_ID: rule,
			SimScript.ARCHER_OBJECT_ID: rule,
			SimScript.TOWER_GUARD_OBJECT_ID: rule,
			SimScript.KNIGHT_OBJECT_ID: rule,
		},
	}
	sim._rules = rules
	sim.setup({}, {})
	sim.ai_enabled = false
	sim.structures.clear()
	sim.expansion_pads.clear()
	sim.entities.clear()
	sim._add_battalion(1, SimScript.PLAYER_TEAM, Vector2.ZERO, unit_type, unit_type, unit_type, -1, rule)
	return sim


func _mounted_eomer_rule() -> Dictionary:
	var rule := _base_rule()
	rule.merge({
		"category": "hero",
		"speed": 9.0,
		"speed_source": 90.0,
		"acceleration": 150.0,
		"acceleration_source": 1500.0,
		"braking": 200.0,
		"braking_source": 2000.0,
		"turn_rate_degrees_per_second": 240.0,
		"turn_rate_source": "locomotor:HeroHorseLocomotor",
		"slow_turn_radius": 0.0,
		"min_turn_speed": 0.10,
	}, true)
	return rule


func _mumakil_rule() -> Dictionary:
	var rule := _base_rule()
	rule.merge({
		"category": "cavalry",
		"speed": 5.0,
		"speed_source": 50.0,
		"acceleration": 75.0,
		"acceleration_source": 750.0,
		"braking": 100.0,
		"braking_source": 1000.0,
		"turn_rate_degrees_per_second": 40.0,
		"turn_rate_source": "locomotor:MumakilLocomotor",
		"slow_turn_radius": 0.2,
		"min_turn_speed": 0.12,
	}, true)
	return rule


func _gondor_fighter_rule() -> Dictionary:
	var rule := _base_rule()
	rule.merge({
		"category": "infantry",
		"speed": 5.5,
		"speed_source": 55.0,
		"acceleration": 51.0,
		"acceleration_source": 510.0,
		"braking": 51.0,
		"braking_source": 510.0,
		"turn_rate_degrees_per_second": 720.0,
		"turn_rate_source": "locomotor:HumanLocomotor",
	}, true)
	return rule


func _base_rule() -> Dictionary:
	return {
		"horde_id": "FixtureHorde",
		"speed": 0.0,
		"speed_source": 0.0,
		"acceleration": 0.0,
		"acceleration_source": 0.0,
		"braking": 0.0,
		"braking_source": 0.0,
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


func _check(label: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("RETAIL_TURN_MODEL PASS %s" % label)
	else:
		failed += 1
		printerr("RETAIL_TURN_MODEL FAIL %s%s" % [label, "" if detail == "" else " (%s)" % detail])


func _check_close(actual: float, expected: float, label: String) -> void:
	_check(label, absf(actual - expected) <= EPSILON, "expected=%.6f actual=%.6f" % [expected, actual])


func _finish() -> void:
	if passed + failed != EXPECTED_CHECKS:
		failed += 1
		printerr("RETAIL_TURN_MODEL FAIL expected_checks expected=%d actual=%d" % [EXPECTED_CHECKS, passed + failed - 1])
	print("RETAIL_TURN_MODEL_RESULT passed=%d failed=%d" % [passed, failed])
	_watchdog.stop()
	quit(0 if failed == 0 else 1)
