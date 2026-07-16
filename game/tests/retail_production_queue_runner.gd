extends SceneTree

const SimScript = preload("res://src/retail_slice/retail_slice_sim.gd")
var HudScript: Script

var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var sim = SimScript.new()
	sim.setup({}, {
		"enable_base_loop": true,
		"starting_resources": 1000,
		"command_point_cap": 1000,
		"member_health": 100,
		"unit_rules": _unit_rules(15, 40),
		"soldier_cost": 200,
		"soldier_build_ticks": 10,
		"soldier_command_points": 10,
		"tower_guard_cost": 300,
		"tower_guard_build_ticks": 20,
		"tower_guard_command_points": 20,
	})
	sim.ai_enabled = false
	var barracks: int = sim.producer_id(SimScript.PLAYER_TEAM, "barracks")
	var first: Dictionary = sim.queue_unit(SimScript.PLAYER_TEAM, barracks, SimScript.SOLDIER_HORDE_ID)
	var second: Dictionary = sim.queue_unit(SimScript.PLAYER_TEAM, barracks, SimScript.TOWER_GUARD_OBJECT_ID)
	_check("two_jobs_queue", bool(first.get("ok", false)) and bool(second.get("ok", false)))
	var initial := sim.production_queue_state(barracks)
	_check(
		"queue_has_deterministic_timing",
		initial.size() == 2
			and int(initial[0].get("start_tick", -1)) == 0
			and int(initial[0].get("complete_tick", -1)) == 10
			and int(initial[1].get("start_tick", -1)) == 10
			and int(initial[1].get("complete_tick", -1)) == 30,
		str(initial)
	)
	sim.advance(4)
	var progressing := sim.production_queue_state(barracks)
	_check(
		"active_job_reports_live_progress",
		progressing.size() == 2
			and bool(progressing[0].get("active", false))
			and is_equal_approx(float(progressing[0].get("progress", -1.0)), 0.4)
			and not bool(progressing[1].get("active", true)),
		str(progressing)
	)
	var cancelled: Dictionary = sim.cancel_queued_unit(SimScript.PLAYER_TEAM, barracks, 0)
	var reflowed := sim.production_queue_state(barracks)
	_check(
		"cancel_refunds_and_reflows",
		bool(cancelled.get("ok", false))
			and int(cancelled.get("refund", -1)) == 200
			and sim.resources_for_team(SimScript.PLAYER_TEAM) == 700
			and reflowed.size() == 1
			and String(reflowed[0].get("unit_type", "")) == SimScript.TOWER_GUARD_OBJECT_ID
			and int(reflowed[0].get("start_tick", -1)) == 4
			and int(reflowed[0].get("complete_tick", -1)) == 24,
		str(reflowed)
	)
	_check("cancel_event_is_authoritative", _has_event(sim.events, "production.cancelled", barracks))
	sim.advance(19)
	_check("reflowed_job_does_not_complete_early", not sim.entities.has(10))
	sim.advance(1)
	var completed: Dictionary = sim.entity(10)
	var producer_position := Vector2(sim.structure(barracks).get("position", Vector2.ZERO))
	var producer_rally := Vector2(sim.structure(barracks).get("rally", producer_position))
	var door_point := Vector2(completed.get("position", Vector2.ZERO))
	var create_point := Vector2(completed.get("production_exit_destination", Vector2.ZERO))
	_check(
		"reflowed_job_completes_on_exact_tick",
		sim.entities.has(10)
			and String(completed.get("unit_type", "")) == SimScript.TOWER_GUARD_OBJECT_ID
			and sim.production_queue_state(barracks).is_empty()
	)
	_check(
		"completed_horde_appears_at_doorway_before_rally",
		String(completed.get("state", "")) == "run"
			and Vector2(completed.get("production_rally", Vector2.ZERO)).is_equal_approx(producer_rally)
			and is_equal_approx(door_point.distance_to(producer_position), SimScript.PRODUCTION_DOOR_INSET_RADIUS)
			and is_equal_approx(create_point.distance_to(producer_position), SimScript.PRODUCTION_EXIT_RADIUS)
			and is_zero_approx(float(completed.get("production_exit_progress", -1.0))),
		str(completed)
	)
	sim.advance(9)
	completed = sim.entity(10)
	_check(
		"doorway_exit_reveals_members_while_root_moves",
		is_equal_approx(float(completed.get("production_exit_progress", -1.0)), 0.5)
			and Vector2(completed.get("position", Vector2.ZERO)).distance_to(door_point) > 0.0
			and Vector2(completed.get("position", Vector2.ZERO)).distance_to(create_point) > 0.0,
		str(completed)
	)
	sim.advance(8)
	_check(
		"doorway_exit_approaches_final_emergence_tick",
		is_equal_approx(float(sim.entity(10).get("production_exit_progress", -1.0)), 17.0 / 18.0)
			and Vector2(sim.entity(10).get("position", Vector2.ZERO)).distance_to(create_point) < Vector2(sim.entity(10).get("position", Vector2.ZERO)).distance_to(door_point)
	)
	sim.advance(1)
	completed = sim.entity(10)
	_check(
		"completed_horde_then_advances_to_rally",
		is_equal_approx(float(completed.get("production_exit_progress", -1.0)), 1.0)
			and Vector2(completed.get("position", Vector2.ZERO)).distance_to(door_point) > 0.0,
		str(completed)
	)
	_check("completion_event_records_exit_route", _has_completed_exit_event(sim.events, barracks, 10))

	# Load the HUD only after autoload initialization. AssetFactory references the
	# ContentDB autoload and cannot safely form that dependency from the main
	# script's compile-time preload graph.
	HudScript = load("res://src/retail_slice/retail_hud.gd")
	_check("hud_script_available", HudScript != null)
	if HudScript == null:
		print("RETAIL_PRODUCTION_QUEUE_RESULT passed=%d failed=%d" % [passed, failed])
		quit(1)
		return
	var hud = HudScript.new()
	root.add_child(hud)
	hud.build()
	var queue_fixture: Array[Dictionary] = [{
		"index": 0,
		"unit_type": SimScript.SOLDIER_HORDE_ID,
		"progress": 0.42,
		"active": true,
	}]
	hud.set_production_state([SimScript.SOLDIER_HORDE_ID], true, 1, queue_fixture)
	_check(
		"hud_exposes_active_job",
		hud.production_queue_label.visible
			and hud.production_queue_label.text.contains("42%")
			and hud.production_progress.visible
			and is_equal_approx(hud.production_progress.value, 42.0)
			and hud.cancel_production_button.visible
			and not hud.cancel_production_button.disabled
	)
	var cancel_requests: Array[int] = []
	hud.cancel_production_requested.connect(func(queue_index: int) -> void: cancel_requests.append(queue_index))
	hud.cancel_production_button.pressed.emit()
	_check("hud_cancel_targets_active_job", cancel_requests == [0], str(cancel_requests))
	hud.set_production_state([SimScript.SOLDIER_HORDE_ID], true, 0, [])
	_check(
		"hud_reports_ready_queue",
		hud.production_queue_label.visible
			and hud.production_queue_label.text == "Production queue ready"
			and not hud.production_progress.visible
			and not hud.cancel_production_button.visible
	)
	hud.free()

	print("RETAIL_PRODUCTION_QUEUE_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


func _unit_rules(member_count: int, member_damage: int) -> Dictionary:
	return {
		SimScript.SOLDIER_OBJECT_ID: _unit_rule(SimScript.SOLDIER_HORDE_ID, member_count, member_damage, 1.15, 17.5),
		SimScript.ARCHER_OBJECT_ID: _unit_rule(SimScript.ARCHER_OBJECT_ID, member_count, member_damage, 30.0, 37.0),
		SimScript.TOWER_GUARD_OBJECT_ID: _unit_rule(SimScript.TOWER_GUARD_OBJECT_ID, member_count, member_damage, 3.5, 17.5),
		SimScript.KNIGHT_OBJECT_ID: _unit_rule(SimScript.KNIGHT_OBJECT_ID, member_count, member_damage, 1.15, 27.5),
	}


func _unit_rule(horde_id: String, member_count: int, member_damage: int, attack_range: float, vision_range: float) -> Dictionary:
	var positions: Array[Vector3] = []
	for index in range(member_count):
		positions.append(Vector3(float(index), 0.0, 0.0))
	return {
		"horde_id": horde_id,
		"speed": 1.0,
		"speed_source": 10.0,
		"acceleration": 1.0,
		"acceleration_source": 10.0,
		"turn_rate_degrees_per_second": 180.0,
		"braking": 1.0,
		"braking_source": 10.0,
		"attack_range": attack_range,
		"attack_range_source": attack_range * 10.0,
		"minimum_attack_range": 0.0,
		"minimum_attack_range_source": 0.0,
		"vision_range": vision_range,
		"vision_range_source": vision_range * 10.0,
		"delay_between_shots_ms": 600.0,
		"pre_attack_delay_ms": 200.0,
		"firing_duration_ms": 200.0,
		"attack_period_ticks": 10,
		"pre_attack_ticks": 2,
		"firing_duration_ticks": 2,
		"member_damage": member_damage,
		"member_count": member_count,
		"formation_positions": positions,
		"provenance": {},
	}


func _has_event(events: Array, kind: String, entity_id: int) -> bool:
	for event_value in events:
		if typeof(event_value) != TYPE_DICTIONARY:
			continue
		var event: Dictionary = event_value
		if String(event.get("kind", "")) == kind and int(event.get("entity_id", 0)) == entity_id:
			return true
	return false


func _has_completed_exit_event(events: Array, producer: int, battalion: int) -> bool:
	for event_value in events:
		if typeof(event_value) != TYPE_DICTIONARY:
			continue
		var event: Dictionary = event_value
		if (
			String(event.get("kind", "")) == "production.exit_complete"
			and int(event.get("entity_id", 0)) == producer
			and int(event.get("target_id", 0)) == battalion
		):
			return true
	return false


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("RETAIL_PRODUCTION_QUEUE PASS %s" % name)
	else:
		failed += 1
		printerr("RETAIL_PRODUCTION_QUEUE FAIL %s%s" % [name, " (%s)" % detail if detail != "" else ""])
