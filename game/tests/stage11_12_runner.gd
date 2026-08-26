extends SceneTree
## Focused deterministic contracts for retail-slice control groups and routes.

const SimScript = preload("res://src/retail_slice/retail_slice_sim.gd")

var passed := 0
var failed := 0


class DeterministicRouteProvider:
	extends RefCounted

	func query_route(from: Vector2, to: Vector2) -> Dictionary:
		if to.x <= -90.0:
			return {"valid": false, "reason": "test-blocked", "points": [], "cells": [], "ford_name": ""}
		if is_equal_approx(to.x, 88.0):
			return {"valid": true, "reason": "", "points": [], "cells": [], "ford_name": ""}
		var points: Array[Vector2] = []
		var bend := Vector2(to.x, from.y)
		if not bend.is_equal_approx(from):
			points.append(bend)
		if points.is_empty() or not points[-1].is_equal_approx(to):
			points.append(to)
		var cells: Array[Vector2i] = []
		for point in points:
			cells.append(Vector2i(roundi(point.x), roundi(point.y)))
		return {"valid": true, "reason": "", "points": points, "cells": cells, "ford_name": "test-ford"}


const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
# Turns a GDScript runtime error inside `_run` — which unwinds past every
# `quit()` and would otherwise leave this headless process idling forever —
# into a loud non-zero exit. See tests/runner_watchdog.gd.
var _runner_watchdog := RunnerWatchdogScript.new()


func _initialize() -> void:
	_runner_watchdog.start(self, "STAGE11_12_RUNNER")
	call_deferred("_run")


func _run() -> void:
	_test_control_group_contract()
	_test_pending_route_snapshot_and_rejection()
	_test_attack_rejection_is_transactional()
	_test_arrival_clears_pending_route()
	_test_deterministic_replay_signature()
	_finish()


## The sim seeds its opening roster ONLY from selected-pack unit rules and
## fail-closes (`missing selected-pack unit rule`) on anything it was not given
## — `_rules` starts empty and nothing in the sim ever populates it, so a
## no-argument `setup()` spawns zero battalions and every entity assertion here
## reads an empty roster. Deterministic contract runners therefore own their
## stats, exactly like tests/retail_formation_movement_runner.gd:56.
func _make_sim():
	var simulation = SimScript.new()
	simulation.setup({}, _harness_rules())
	simulation.ai_enabled = false
	return simulation


func _harness_rules() -> Dictionary:
	return {"faction_manifest": preload("res://src/retail_slice/retail_faction_manifest.gd").default_manifest(), 
		"unit_rules": {
			SimScript.SOLDIER_OBJECT_ID: _unit_rule(SimScript.SOLDIER_HORDE_ID),
			SimScript.ARCHER_OBJECT_ID: _unit_rule(SimScript.ARCHER_OBJECT_ID),
			SimScript.TOWER_GUARD_OBJECT_ID: _unit_rule(SimScript.TOWER_GUARD_OBJECT_ID),
			SimScript.KNIGHT_OBJECT_ID: _unit_rule(SimScript.KNIGHT_OBJECT_ID),
		},
	}


func _unit_rule(horde_id: String) -> Dictionary:
	return {
		"horde_id": horde_id,
		"speed": 10.0,
		"speed_source": 100.0,
		# Unauthored ramp on purpose: the sim then falls back to 10x the row's
		# own max speed per second, so a test that overrides `speed` (the
		# arrival case sets 1000) still runs at its cap from tick one. A fixed
		# acceleration would silently pin those rows to a slow ramp instead.
		"acceleration": 0.0,
		"acceleration_source": 0.0,
		"turn_rate_degrees_per_second": 3600.0,
		"braking": 0.0,
		"braking_source": 0.0,
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
		"is_builder": false,
	}


func _test_control_group_contract() -> void:
	var simulation = _make_sim()
	var reset_rows: Array[Dictionary] = simulation.control_groups_snapshot()
	_check("control_groups_reset_one_through_nine", reset_rows.size() == 9 and int(reset_rows[0]["group"]) == 1 and int(reset_rows[8]["group"]) == 9 and Array(reset_rows[0]["entity_ids"]).is_empty() and Array(reset_rows[8]["entity_ids"]).is_empty())
	var assigned: Dictionary = simulation.assign_control_group(1, [2, 101, 2, 999, 1])
	_check("control_group_assign_filters_sorts_and_deduplicates", bool(assigned.get("ok", false)) and Array(assigned.get("entity_ids", [])) == [1, 2], str(assigned))
	_check("control_group_recall_is_stable", simulation.recall_control_group(1) == [1, 2], str(simulation.recall_control_group(1)))
	_check("control_group_nine_supported", bool(simulation.assign_control_group(9, [2]).get("ok", false)) and simulation.recall_control_group(9) == [2])
	var before_invalid: String = JSON.stringify(simulation.control_groups_snapshot())
	var invalid: Dictionary = simulation.assign_control_group(0, [1])
	_check("invalid_control_group_rejected_without_mutation", not bool(invalid.get("ok", true)) and JSON.stringify(simulation.control_groups_snapshot()) == before_invalid, str(invalid))
	var first: Dictionary = simulation.entity(1)
	first["health"] = 0
	simulation.prune_control_groups()
	_check("control_group_prune_removes_defeated_members", simulation.recall_control_group(1) == [2])
	var signature_with_groups := simulation.state_signature()
	var snapshot: Dictionary = simulation.state_snapshot()
	_check("state_snapshot_contains_all_control_groups", Array(snapshot.get("control_groups", [])).size() == 9 and simulation.control_group_snapshot() == simulation.control_groups_snapshot())
	simulation.reset_control_groups()
	_check("control_group_reset_clears_every_slot", _all_control_groups_empty(simulation.control_groups_snapshot()))
	_check("state_signature_covers_control_groups", simulation.state_signature() != signature_with_groups)


func _test_pending_route_snapshot_and_rejection() -> void:
	var simulation = _make_sim()
	simulation.route_provider = DeterministicRouteProvider.new()
	var destination := Vector2(-20.0, 6.0)
	_check("multi_battalion_move_is_accepted", simulation.issue_move([2, 1, 2], destination) == 2)
	var first: Dictionary = simulation.entity(1)
	var second: Dictionary = simulation.entity(2)
	_check("accepted_order_has_shared_deterministic_sequence", int(first["order_sequence"]) == 1 and int(second["order_sequence"]) == 1)
	var snapshot: Dictionary = simulation.state_snapshot()
	var first_row := _snapshot_entity(snapshot, 1)
	_check("snapshot_contains_pending_destination_route_and_cells", Array(first_row.get("destination", [])) == [-20.0, 6.0] and not Array(first_row.get("route", [])).is_empty() and not Array(first_row.get("route_cells", [])).is_empty() and String(first_row.get("route_ford", "")) == "test-ford", str(first_row))
	_check("snapshot_contains_order_sequence_cursor", int(first_row.get("order_sequence", 0)) == 1 and int(snapshot.get("next_order_sequence", 0)) == 2)
	var before_route: Array = Array(first["route"]).duplicate(true)
	var before_cells: Array = Array(first["route_cells"]).duplicate(true)
	var before_destination := Vector2(first["destination"])
	var before_sequence := int(first["order_sequence"])
	var before_signature := simulation.state_signature()
	_check("blocked_move_is_rejected", simulation.issue_move([1], Vector2(-99.0, 5.0)) == 0 and simulation.last_route_rejection == "test-blocked")
	_check("blocked_move_preserves_prior_valid_route", Array(first["route"]) == before_route and Array(first["route_cells"]) == before_cells and Vector2(first["destination"]).is_equal_approx(before_destination) and int(first["order_sequence"]) == before_sequence)
	_check("rejected_move_does_not_advance_state_signature", simulation.state_signature() == before_signature)
	_check("empty_valid_route_is_rejected_transactionally", simulation.issue_move([1], Vector2(88.0, 5.0)) == 0 and simulation.last_route_rejection == "empty-route" and simulation.state_signature() == before_signature)


func _test_attack_rejection_is_transactional() -> void:
	var simulation = _make_sim()
	simulation.route_provider = DeterministicRouteProvider.new()
	_check("attack_rejection_setup_move", simulation.issue_move([1], Vector2(-20.0, -8.0)) == 1)
	var enemy: Dictionary = simulation.entity(101)
	enemy["position"] = Vector2(-99.0, 5.0)
	var player: Dictionary = simulation.entity(1)
	var before_route: Array = Array(player["route"]).duplicate(true)
	var before_destination := Vector2(player["destination"])
	var before_target := int(player["target_id"])
	var before_sequence := int(player["order_sequence"])
	var before_signature := simulation.state_signature()
	_check("blocked_attack_is_rejected", simulation.issue_attack([1], 101) == 0 and simulation.last_route_rejection == "test-blocked")
	_check("blocked_attack_preserves_prior_valid_order", Array(player["route"]) == before_route and Vector2(player["destination"]).is_equal_approx(before_destination) and int(player["target_id"]) == before_target and int(player["order_sequence"]) == before_sequence)
	_check("rejected_attack_does_not_advance_state_signature", simulation.state_signature() == before_signature)


func _test_arrival_clears_pending_route() -> void:
	var simulation = _make_sim()
	simulation.route_provider = DeterministicRouteProvider.new()
	var destination := Vector2(-34.0, -10.0)
	var player: Dictionary = simulation.entity(1)
	player["speed"] = 1000.0
	_check("arrival_setup_move", simulation.issue_move([1], destination) == 1 and not Array(player["route"]).is_empty())
	# Drain the route instead of pinning a tick count: this contract is about the
	# bookkeeping AT arrival, not about how many ticks the ramp takes. The route
	# stepper consumes at most one waypoint per tick and brakes into the final
	# one, so the two-leg test route is several ticks long; the bound just keeps
	# a stalled route from hanging the runner.
	var arrival_ticks := 0
	while not Array(player["route"]).is_empty() and arrival_ticks < 16:
		simulation.tick()
		arrival_ticks += 1
	var snapshot_row := _snapshot_entity(simulation.state_snapshot(), 1)
	_check("arrival_reaches_exact_destination", Vector2(player["position"]).is_equal_approx(destination) and Vector2(player["destination"]).is_equal_approx(destination))
	_check("arrival_clears_all_pending_route_metadata", Array(player["route"]).is_empty() and Array(player["route_cells"]).is_empty() and String(player["route_ford"]).is_empty() and Array(snapshot_row.get("route", [])).is_empty() and Array(snapshot_row.get("route_cells", [])).is_empty())
	_check("arrival_becomes_idle_without_losing_order_sequence", String(player["state"]) == "idle" and int(player["order_sequence"]) == 1)


func _test_deterministic_replay_signature() -> void:
	var first = _make_sim()
	var second = _make_sim()
	for simulation in [first, second]:
		simulation.route_provider = DeterministicRouteProvider.new()
		var group_ids: Array[int] = [2, 1]
		var move_ids: Array[int] = [1, 2]
		simulation.assign_control_group(1, group_ids)
		simulation.issue_move(move_ids, Vector2(-20.0, 6.0))
		simulation.tick()
	_check("control_group_and_pending_order_replay_is_deterministic", first.state_signature() == second.state_signature(), "%s != %s" % [first.state_signature(), second.state_signature()])


func _snapshot_entity(snapshot: Dictionary, entity_id: int) -> Dictionary:
	for value: Variant in Array(snapshot.get("entities", [])):
		var row: Dictionary = value
		if int(row.get("id", 0)) == entity_id:
			return row
	return {}


func _all_control_groups_empty(rows: Array[Dictionary]) -> bool:
	if rows.size() != 9:
		return false
	for index in range(rows.size()):
		if int(rows[index].get("group", 0)) != index + 1 or not Array(rows[index].get("entity_ids", [])).is_empty():
			return false
	return true


func _check(label: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("PASS %s" % label)
		return
	failed += 1
	printerr("FAIL %s%s" % [label, " :: %s" % detail if not detail.is_empty() else ""])


func _finish() -> void:
	print("STAGE 11/12 TESTS: %d passed, %d failed" % [passed, failed])
	quit(0 if failed == 0 else 1)
