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
##     SlowTurnRadius = 0, FastTurnRadius = 9, Braking = 2000,
##     MinTurnSpeed = 10%.
##   object/evilfaction/units/evilmen/mumakil.ini:1739-1743
##     MordorMumakil binds MumakilLocomotor at speed 50.
##   locomotor.ini:1347-1367
##     MumakilLocomotor TurnTime = 9000 (40 deg/s), Acceleration = 750,
##     SlowTurnRadius = 2, FastTurnRadius = 50, Braking = 1000,
##     MinTurnSpeed = 12%.
##   object/goodfaction/units/men/gondorfighter.ini:851-855
##     GondorFighter binds HumanLocomotor at NORMAL_FOOT_MED_MEMBER_SPEED.
##   gamedata.ini:7903
##     NORMAL_FOOT_MED_MEMBER_SPEED = 55.
##   locomotor.ini:142-152
##     HumanLocomotor TurnTime = 500 (720 deg/s), Acceleration/Braking = 510,
##     MinTurnSpeed = 0%.
##   locomotor.ini:835-854
##     NormalCavalryHordeLocomotor TurnTime = 1000 (360 deg/s), Acceleration =
##     800, Braking = 1500, SlowTurnRadius = 0, FastTurnRadius = 48,
##     MinTurnSpeed = 100%.
##   object/goodfaction/hordes/men/menhordes.ini:1822-1825
##     RohanRohirrimHorde binds NormalCavalryHordeLocomotor at
##     NORMAL_MOUNTED_MED_HORDE_SPEED = 100.
##   locomotor.ini:178-188; wildhordes.ini:1858-1862
##     WildBabyDrakeHorde binds FiredrakeLocomotor at speed 120; the locomotor
##     authors TurnTime 1500, SlowTurnRadius 2, FastTurnRadius 30, and
##     MinTurnSpeed 100%.
##   locomotor.ini:2944-2956; wildhordes.ini:816-820; gamedata.ini:7911
##     WildSpiderlingHorde binds TestWallScalingHordeLocomotor at
##     NORMAL_MOUNTED_FAST_HORDE_SPEED = 110; the locomotor authors TurnTime
##     2000, SlowTurnRadius 33, FastTurnRadius 33, and MinTurnSpeed 100%.
##   locomotor.ini:2438-2446; men/units/men/porter.ini:238
##     MenPorter binds PorterLocomotor at speed 60. The compiled locomotor
##     authors TurnTime 1500, Acceleration/Braking 1000, MinTurnSpeed 100%, and
##     authors neither SlowTurnRadius nor FastTurnRadius.
##
## The sim's authored source-unit scale is 0.1, so the sealed runtime values are
## 9/150/200 and 5.5/51/51 respectively. No movement number below is invented.

const SimScript := preload("res://src/retail_slice/retail_slice_sim.gd")
const AdapterScript := preload("res://src/retail_slice/playable_unit_runtime_adapter.gd")
const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")

const EXPECTED_CHECKS := 44
const EPSILON := 0.0001
const SOURCE_SCALE := 0.1
const ARC_RADIUS_TOLERANCE := 0.02
const SELECTED_MEN_PACK := "rotwk-men-vslice/8f40f2af6bf8ea40cb6eb2e44ee262ad92da2364bbebd297fc122a4604dc5fa4"
const SELECTED_MORDOR_PACK := "rotwk-mordor-vslice/82143fea3daa8021704810e9a958519e6ea0b23677107aa639ae532b72ee5899"
const ROHIRRIM_DESCRIPTOR_SHA256 := "d232ddc20de1be3ec6e83debf785e15ae0d59b54d37b907754ae5a8ba7c7b84d"
const MUMAKIL_DESCRIPTOR_SHA256 := "50c65fb71ffb10725367afae9411fd4cb8223aff7d4b1289ba66cddf658a1bcc"
const MEN_PORTER_DESCRIPTOR_SHA256 := "a86465903f5699df909ff9a6cc850a7738ee5ff8458346b211748b80b5ca861f"

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
	var content_db = root.get_node_or_null("ContentDB")
	var selected_rohirrim := _selected_normalized_rule(
		content_db, "RohanRohirrimHorde", SELECTED_MEN_PACK, ROHIRRIM_DESCRIPTOR_SHA256
	)
	var selected_mumakil := _selected_normalized_rule(
		content_db, "MordorMumakil", SELECTED_MORDOR_PACK, MUMAKIL_DESCRIPTOR_SHA256
	)
	var selected_no_fast := _selected_no_fast_kinematics_rule(
		content_db, "MenPorter", SELECTED_MEN_PACK, MEN_PORTER_DESCRIPTOR_SHA256
	)
	_check(
		"selected_pack_kinematics_documents_are_pinned",
		not selected_rohirrim.is_empty()
			and not selected_mumakil.is_empty()
			and not selected_no_fast.is_empty()
	)

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

	var rohirrim_corrections: Array[Dictionary] = []
	for correction_degrees in [30.0, 90.0, 179.0]:
		var correction := _exercise_rohirrim_correction(correction_degrees)
		rohirrim_corrections.append(correction)
		_check(
			"rohirrim_%d_degree_correction_has_no_frozen_ticks" % int(correction_degrees),
			int(correction["frozen_ticks"]) == 0,
			str(correction)
		)
		_check(
			"rohirrim_%d_degree_heading_is_rate_bounded" % int(correction_degrees),
			float(correction["maximum_heading_delta_degrees"])
				<= 360.0 * SimScript.TICK_SECONDS + EPSILON,
			str(correction)
		)
		_check(
			"rohirrim_%d_degree_translation_is_fast_arc_bounded" % int(correction_degrees),
			float(correction["maximum_step_distance"])
				<= 4.8 * deg_to_rad(360.0) * SimScript.TICK_SECONDS + EPSILON,
			str(correction)
		)
	var all_rohirrim_rows_preserve_fast_turn_radius := true
	for correction in rohirrim_corrections:
		all_rohirrim_rows_preserve_fast_turn_radius = (
			all_rohirrim_rows_preserve_fast_turn_radius
			and bool(correction["has_fast_turn_radius"])
			and is_equal_approx(float(correction["fast_turn_radius"]), 4.8)
		)
	_check(
		"rohirrim_runtime_row_preserves_authored_fast_turn_radius",
		all_rohirrim_rows_preserve_fast_turn_radius,
		str(rohirrim_corrections)
	)
	var rohirrim_arrival := _exercise_lateral_arrival(_rohirrim_horde_rule(), "RohanRohirrimHorde")
	_check(
		"rohirrim_lateral_waypoint_still_arrives_and_idles",
		bool(rohirrim_arrival["arrived"]) and String(rohirrim_arrival["state"]) == "idle",
		str(rohirrim_arrival)
	)

	var pivot_crush := _exercise_zero_translation_pivot_crush()
	_check(
		"zero_translation_pivot_does_not_crush_adjacent_unit",
		Vector2(pivot_crush["displacement"]).length_squared() <= EPSILON * EPSILON
			and int(pivot_crush["damage_dealt"]) == 0
			and int(pivot_crush["trample_events"]) == 0,
		str(pivot_crush)
	)

	var adapter_fast_turn := _exercise_fast_turn_radius_adapter()
	_check(
		"adapter_scales_authored_fast_turn_radius",
		bool(adapter_fast_turn["authored_has_key"])
			and is_equal_approx(float(adapter_fast_turn["authored_value"]), 4.8),
		str(adapter_fast_turn)
	)
	_check(
		"adapter_preserves_authored_zero_fast_turn_radius",
		bool(adapter_fast_turn["zero_has_key"])
			and is_zero_approx(float(adapter_fast_turn["zero_value"])),
		str(adapter_fast_turn)
	)
	_check(
		"adapter_keeps_unauthored_fast_turn_radius_absent",
		not bool(adapter_fast_turn["absent_has_key"]),
		str(adapter_fast_turn)
	)
	_check(
		"adapter_admits_min_below_100_mumakil_fast_turn_radius",
		bool(adapter_fast_turn["mumakil_has_key"])
			and is_equal_approx(float(adapter_fast_turn["mumakil_value"]), 5.0),
		str(adapter_fast_turn)
	)

	_check(
		"selected_rohirrim_admits_authored_two_radius_contract",
		is_equal_approx(float(selected_rohirrim.get("slow_turn_radius", -1.0)), 0.0)
			and is_equal_approx(float(selected_rohirrim.get("fast_turn_radius", -1.0)), 4.8)
			and is_equal_approx(float(selected_rohirrim.get("min_turn_speed", -1.0)), 1.0),
		str(selected_rohirrim)
	)
	var rohirrim_arc := _exercise_radius_trace(
		selected_rohirrim, "SelectedRohanRohirrimHorde", 179.0, 7
	)
	_check(
		"selected_rohirrim_179_degree_positions_trace_authored_48_radius",
		int(rohirrim_arc.get("radius_samples", 0)) == 7
			and absf(float(rohirrim_arc.get("mean_radius", 0.0)) - 4.8) <= ARC_RADIUS_TOLERANCE
			and absf(float(rohirrim_arc.get("minimum_radius", 0.0)) - 4.8) <= ARC_RADIUS_TOLERANCE
			and absf(float(rohirrim_arc.get("maximum_radius", 0.0)) - 4.8) <= ARC_RADIUS_TOLERANCE,
		"tolerance=%.3f %s" % [ARC_RADIUS_TOLERANCE, str(rohirrim_arc)]
	)
	var standing_start := _exercise_standing_start(selected_rohirrim)
	_check(
		"selected_rohirrim_standing_start_has_zero_frozen_ticks",
		int(standing_start.get("frozen_ticks", -1)) == 0,
		str(standing_start)
	)

	_check(
		"selected_mumakil_admits_authored_50_2_12_percent_contract",
		is_equal_approx(float(selected_mumakil.get("fast_turn_radius", -1.0)), 5.0)
			and is_equal_approx(float(selected_mumakil.get("slow_turn_radius", -1.0)), 0.2)
			and is_equal_approx(float(selected_mumakil.get("min_turn_speed", -1.0)), 0.12),
		str(selected_mumakil)
	)
	var mumakil_fast := _exercise_single_radius_mode(selected_mumakil, true)
	_check(
		"mumakil_above_threshold_uses_fast_radius",
		absf(float(mumakil_fast.get("implied_radius", 0.0)) - 5.0) <= ARC_RADIUS_TOLERANCE,
		str(mumakil_fast)
	)
	var mumakil_slow := _exercise_single_radius_mode(selected_mumakil, false)
	_check(
		"mumakil_below_threshold_uses_slow_radius",
		absf(float(mumakil_slow.get("implied_radius", 0.0)) - 0.2) <= ARC_RADIUS_TOLERANCE,
		str(mumakil_slow)
	)

	var no_fast_probe := _probe_no_fast_kinematics_identity(selected_no_fast, "SelectedMenPorter")
	_check(
		"compiled_men_porter_keeps_unauthored_fast_turn_radius_absent",
		not selected_no_fast.has("fast_turn_radius"),
		str(selected_no_fast)
	)
	_check(
		"compiled_men_porter_kinematics_stay_prechange_byte_identical",
		bool(no_fast_probe.get("byte_identical", false)),
		str(no_fast_probe)
	)

	var rohirrim_multi := _exercise_multi_leg_arrival(selected_rohirrim, "SelectedRohirrimMultiLeg")
	_check(
		"selected_rohirrim_multi_leg_route_arrives_and_idles",
		bool(rohirrim_multi.get("arrived", false)) and String(rohirrim_multi.get("state", "")) == "idle",
		str(rohirrim_multi)
	)
	var mumakil_multi := _exercise_multi_leg_arrival(selected_mumakil, "SelectedMumakilMultiLeg")
	_check(
		"selected_mumakil_multi_leg_route_arrives_and_idles",
		bool(mumakil_multi.get("arrived", false)) and String(mumakil_multi.get("state", "")) == "idle",
		str(mumakil_multi)
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


func _exercise_rohirrim_correction(correction_degrees: float) -> Dictionary:
	var sim = _make_sim(_rohirrim_horde_rule(), "RohanRohirrimHorde")
	var row: Dictionary = sim.entities[1]
	row["facing"] = Vector2.RIGHT
	row["current_speed"] = float(row["speed"])
	row["route"] = [Vector2.RIGHT.rotated(deg_to_rad(correction_degrees)) * 100.0]
	var turn_limit_degrees := 360.0 * SimScript.TICK_SECONDS
	var turn_ticks := maxi(1, ceili(correction_degrees / turn_limit_degrees))
	var frozen_ticks := 0
	var maximum_heading_delta_degrees := 0.0
	var maximum_step_distance := 0.0
	for _tick_index in turn_ticks:
		var before_position := Vector2(row["position"])
		var before_facing := Vector2(row["facing"]).normalized()
		sim._step_route(row)
		var displacement := Vector2(row["position"]) - before_position
		var after_facing := Vector2(row["facing"]).normalized()
		if displacement.length_squared() <= EPSILON * EPSILON:
			frozen_ticks += 1
		maximum_step_distance = maxf(maximum_step_distance, displacement.length())
		maximum_heading_delta_degrees = maxf(
			maximum_heading_delta_degrees,
			rad_to_deg(absf(wrapf(after_facing.angle() - before_facing.angle(), -PI, PI)))
		)
	return {
		"correction_degrees": correction_degrees,
		"turn_ticks": turn_ticks,
		"frozen_ticks": frozen_ticks,
		"maximum_heading_delta_degrees": maximum_heading_delta_degrees,
		"maximum_step_distance": maximum_step_distance,
		"has_fast_turn_radius": row.has("fast_turn_radius"),
		"fast_turn_radius": row.get("fast_turn_radius", -1.0),
		"position": row["position"],
	}


func _exercise_radius_trace(
		rule: Dictionary, unit_type: String, correction_degrees: float, ticks: int
) -> Dictionary:
	var sim = _make_sim(rule, unit_type)
	var row: Dictionary = sim.entities[1]
	row["facing"] = Vector2.RIGHT
	row["current_speed"] = float(row["speed"])
	row["route"] = [Vector2.RIGHT.rotated(deg_to_rad(correction_degrees)) * 1000000.0]
	var radii: Array[float] = []
	var positions: Array[Vector2] = [Vector2(row["position"])]
	for _tick_index in 60:
		var before_position := Vector2(row["position"])
		var before_facing := Vector2(row["facing"]).normalized()
		sim._step_route(row)
		var displacement := Vector2(row["position"]) - before_position
		var after_facing := Vector2(row["facing"]).normalized()
		var heading_delta := absf(wrapf(after_facing.angle() - before_facing.angle(), -PI, PI))
		if heading_delta > EPSILON and displacement.length() > EPSILON:
			radii.append(displacement.length() / heading_delta)
		positions.append(Vector2(row["position"]))
		if radii.size() >= ticks:
			break
	var total := 0.0
	var minimum := INF
	var maximum := 0.0
	for radius in radii:
		total += radius
		minimum = minf(minimum, radius)
		maximum = maxf(maximum, radius)
	return {
		"radius_samples": radii.size(),
		"mean_radius": total / float(radii.size()) if not radii.is_empty() else 0.0,
		"minimum_radius": minimum if not radii.is_empty() else 0.0,
		"maximum_radius": maximum,
		"radii": radii,
		"positions": positions,
	}


func _exercise_standing_start(rule: Dictionary) -> Dictionary:
	var sim = _make_sim(rule, "SelectedRohirrimStandingStart")
	var row: Dictionary = sim.entities[1]
	row["facing"] = Vector2.RIGHT
	row["current_speed"] = 0.0
	row["route"] = [Vector2(0.0, 1000000.0)]
	var frozen_ticks := 0
	var step_distances: Array[float] = []
	for _tick_index in 10:
		var before := Vector2(row["position"])
		sim._step_route(row)
		var step_distance := Vector2(row["position"]).distance_to(before)
		step_distances.append(step_distance)
		if step_distance <= EPSILON:
			frozen_ticks += 1
	return {
		"frozen_ticks": frozen_ticks,
		"step_distances": step_distances,
		"position": row["position"],
		"current_speed": row["current_speed"],
	}


func _exercise_single_radius_mode(rule: Dictionary, fast_mode: bool) -> Dictionary:
	var sim = _make_sim(rule, "SelectedMumakilFast" if fast_mode else "SelectedMumakilSlow")
	var row: Dictionary = sim.entities[1]
	row["facing"] = Vector2.RIGHT
	if fast_mode:
		row["current_speed"] = float(row["speed"])
		row["route"] = [Vector2.RIGHT.rotated(deg_to_rad(30.0)) * 1000000.0]
	else:
		# Mumakil Braking=100 sim-units/s^2. At speed 0.5 its authored stop
		# distance is 0.00125, so this 0.001 waypoint selects a genuine sub-12%
		# braking tick. Facing almost away prevents the arrival snap.
		row["current_speed"] = 0.5
		row["route"] = [Vector2.RIGHT.rotated(deg_to_rad(179.0)) * 0.001]
	var before_position := Vector2(row["position"])
	var before_facing := Vector2(row["facing"]).normalized()
	sim._step_route(row)
	var displacement := Vector2(row["position"]) - before_position
	var after_facing := Vector2(row["facing"]).normalized()
	var heading_delta := absf(wrapf(after_facing.angle() - before_facing.angle(), -PI, PI))
	return {
		"mode": "fast" if fast_mode else "slow",
		"step_distance": displacement.length(),
		"heading_delta_radians": heading_delta,
		"implied_radius": displacement.length() / heading_delta if heading_delta > EPSILON else 0.0,
		"current_speed": row["current_speed"],
	}


func _exercise_multi_leg_arrival(rule: Dictionary, unit_type: String) -> Dictionary:
	var sim = _make_sim(rule, unit_type)
	var row: Dictionary = sim.entities[1]
	row["facing"] = Vector2.RIGHT
	row["current_speed"] = 0.0
	row["route"] = [Vector2(0.0, 2.0), Vector2(2.0, 2.0), Vector2(2.0, 0.0)]
	row["state"] = "run"
	var done_tick := -1
	for tick_index in range(1, 301):
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


func _exercise_zero_translation_pivot_crush() -> Dictionary:
	var sim = _make_sim(_rohirrim_horde_rule(), "RohanRohirrimHorde")
	var victim_rule := _gondor_fighter_rule()
	sim._add_battalion(
		2, SimScript.ENEMY_TEAM, Vector2(1.0, 0.0),
		"AdjacentGondorFighter", "AdjacentGondorFighter", "AdjacentGondorFighterHorde",
		-1, victim_rule
	)
	var row: Dictionary = sim.entities[1]
	var victim: Dictionary = sim.entities[2]
	row["facing"] = Vector2.RIGHT
	row["current_speed"] = 0.0
	# The lateral point is inside the authored 4.8 fast circle, so the unit
	# brakes onto its authored zero slow radius and genuinely pivots in place.
	row["route"] = [Vector2(0.0, 2.0)]
	row["trample_cooldown"] = 0
	var before_position := Vector2(row["position"])
	var health_before := int(victim["health"])
	var event_start: int = sim.events.size()
	sim._step_route(row)
	var trample_events := 0
	for event_index in range(event_start, sim.events.size()):
		if String((sim.events[event_index] as Dictionary).get("kind", "")) == "combat.trample":
			trample_events += 1
	return {
		"displacement": Vector2(row["position"]) - before_position,
		"written_current_speed": row["current_speed"],
		"damage_dealt": health_before - int(victim["health"]),
		"trample_events": trample_events,
	}


func _exercise_fast_turn_radius_adapter() -> Dictionary:
	var source := {
		"unit_type": "bfme2.object.rohanrohirrimhorde",
		"source_object_id": "RohanRohirrimHorde",
		"category": "cavalry",
		"member_count": 1,
		"member_health": 600,
		"speed_source": 100.0,
		"vision_range_source": 500.0,
		"movement": {
			"acceleration": 800.0,
			"braking": 1500.0,
			"turnRateDegreesPerSecond": 360.0,
			"slowTurnRadius": 0.0,
			"fastTurnRadius": 48.0,
			"minTurnSpeed": 1.0,
		},
		"combat": {
			"attackRange": 10.0,
			"delayBetweenShotsMs": 1000.0,
			"preAttackDelayMs": 0.0,
			"firingDurationMs": 0.0,
			"damage": 1,
		},
		"formation": {"positions": [{"x": 0.0, "y": 0.0}]},
	}
	var authored: Dictionary = AdapterScript.normalized_unit_rule(source, 0.1)
	var zero_source: Dictionary = source.duplicate(true)
	(zero_source["movement"] as Dictionary)["fastTurnRadius"] = 0.0
	var zero: Dictionary = AdapterScript.normalized_unit_rule(zero_source, 0.1)
	var absent_source: Dictionary = source.duplicate(true)
	(absent_source["movement"] as Dictionary).erase("fastTurnRadius")
	var absent: Dictionary = AdapterScript.normalized_unit_rule(absent_source, 0.1)
	var mumakil_source: Dictionary = source.duplicate(true)
	mumakil_source["source_object_id"] = "MordorMumakil"
	mumakil_source["speed_source"] = 50.0
	(mumakil_source["movement"] as Dictionary).merge({
		"acceleration": 750.0,
		"braking": 1000.0,
		"turnRateDegreesPerSecond": 40.0,
		"slowTurnRadius": 2.0,
		"fastTurnRadius": 50.0,
		"minTurnSpeed": 0.12,
	}, true)
	var mumakil: Dictionary = AdapterScript.normalized_unit_rule(mumakil_source, 0.1)
	return {
		"authored_has_key": authored.has("fast_turn_radius"),
		"authored_value": authored.get("fast_turn_radius", -1.0),
		"zero_has_key": zero.has("fast_turn_radius"),
		"zero_value": zero.get("fast_turn_radius", -1.0),
		"absent_has_key": absent.has("fast_turn_radius"),
		"mumakil_has_key": mumakil.has("fast_turn_radius"),
		"mumakil_value": mumakil.get("fast_turn_radius", -1.0),
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


func _selected_document(
		content_db: Node, source_id: String, pack_id: String, descriptor_sha256: String
) -> Dictionary:
	if content_db == null:
		return {}
	var document: Dictionary = content_db.get_playable_unit_runtime(source_id)
	var pack_root := String(document.get("_pack_root", "")).replace("\\", "/").trim_suffix("/")
	if (
		document.is_empty()
		or String(document.get("objectId", "")) != source_id
		or not pack_root.to_lower().ends_with(pack_id.to_lower())
		or String(document.get("descriptorSha256", "")) != descriptor_sha256
	):
		printerr(
			"RETAIL_TURN_MODEL selected document mismatch source=%s pack=%s descriptor=%s"
			% [source_id, pack_root, String(document.get("descriptorSha256", ""))]
		)
		return {}
	return document


func _selected_normalized_rule(
		content_db: Node, source_id: String, pack_id: String, descriptor_sha256: String
) -> Dictionary:
	var document := _selected_document(content_db, source_id, pack_id, descriptor_sha256)
	if document.is_empty():
		return {}
	# This runner seals locomotion only. Derive every movement number from the
	# selected compiled document, while using a minimal inert combat/formation
	# envelope so builders (MenPorter) exercise the same normalization path as
	# combat units without requiring an unrelated producer/combat contract.
	var resolved := ((document.get("registration", {}) as Dictionary).get("simulation", {}) as Dictionary).get("resolved", {}) as Dictionary
	var compiled_movement := resolved.get("movement", {}) as Dictionary
	var movement: Dictionary = {}
	for key in compiled_movement:
		movement[key] = _resolved_value(compiled_movement[key])
	var simulation := {
		"unit_type": "bfme2.object.%s" % source_id.to_lower(),
		"source_object_id": source_id,
		"category": String(document.get("category", "")),
		"member_count": 1,
		"member_health": 100,
		"speed_source": float(_resolved_value(resolved.get("speed"))),
		"vision_range_source": 100.0,
		"movement": movement,
		"combat": {
			"attackRange": 10.0,
			"delayBetweenShotsMs": 1000.0,
			"preAttackDelayMs": 0.0,
			"firingDurationMs": 0.0,
			"damage": 1,
		},
		"formation": {"positions": [{"x": 0.0, "y": 0.0}]},
	}
	return AdapterScript.normalized_unit_rule(simulation, SOURCE_SCALE)


func _selected_no_fast_kinematics_rule(
		content_db: Node, source_id: String, pack_id: String, descriptor_sha256: String
) -> Dictionary:
	var document := _selected_document(content_db, source_id, pack_id, descriptor_sha256)
	if document.is_empty():
		return {}
	var compiled_movement := ((document.get("registration", {}) as Dictionary).get("simulation", {}) as Dictionary).get("resolved", {}) as Dictionary
	compiled_movement = compiled_movement.get("movement", {}) as Dictionary
	if compiled_movement.has("fastTurnRadius"):
		return {}
	return _selected_normalized_rule(content_db, source_id, pack_id, descriptor_sha256)


func _probe_no_fast_kinematics_identity(rule: Dictionary, unit_type: String) -> Dictionary:
	var sim = _make_sim(rule, unit_type)
	var row: Dictionary = sim.entities[1]
	row["facing"] = Vector2.RIGHT
	row["current_speed"] = 0.0
	var correction := deg_to_rad(30.0)
	row["route"] = [Vector2.RIGHT.rotated(correction) * 1000000.0]
	# Pre-change no-fast-radius formula: accelerate, apply the authored
	# MinTurnSpeed floor, rotate by authored TurnRate, then translate directly
	# along that bounded facing. The bytes, not approximate floats, are compared.
	var expected_speed := minf(
		float(rule["speed"]), float(rule["acceleration"]) * SimScript.TICK_SECONDS
	)
	expected_speed = maxf(
		expected_speed,
		float(rule["speed"]) * clampf(float(rule.get("min_turn_speed", 0.0)), 0.0, 1.0)
	)
	var expected_turn := minf(
		correction,
		deg_to_rad(float(rule["turn_rate_degrees_per_second"])) * SimScript.TICK_SECONDS
	)
	var expected_facing := Vector2.RIGHT.rotated(expected_turn)
	var expected := [
		expected_facing * expected_speed * SimScript.TICK_SECONDS,
		expected_facing,
		expected_speed,
	]
	sim._step_route(row)
	var actual := [Vector2(row["position"]), Vector2(row["facing"]), float(row["current_speed"])]
	return {
		"byte_identical": var_to_bytes(actual) == var_to_bytes(expected),
		"actual_bytes": var_to_bytes(actual).hex_encode(),
		"expected_bytes": var_to_bytes(expected).hex_encode(),
	}


func _resolved_value(value: Variant) -> Variant:
	return (value as Dictionary).get("value") if typeof(value) == TYPE_DICTIONARY else value


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


func _rohirrim_horde_rule() -> Dictionary:
	var rule := _base_rule()
	rule.merge({
		"category": "cavalry",
		"speed": 10.0,
		"speed_source": 100.0,
		"acceleration": 80.0,
		"acceleration_source": 800.0,
		"braking": 150.0,
		"braking_source": 1500.0,
		"turn_rate_degrees_per_second": 360.0,
		"turn_rate_source": "locomotor:NormalCavalryHordeLocomotor",
		"slow_turn_radius": 0.0,
		"fast_turn_radius": 4.8,
		"min_turn_speed": 1.0,
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
