extends SceneTree

const SimScript = preload("res://src/retail_slice/retail_slice_sim.gd")

var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var sim = SimScript.new()
	sim.setup({}, {
		"enable_base_loop": true,
		"starting_resources": 10000,
		"member_health": 100,
		"unit_rules": _unit_rules(),
		"farm_income": 25,
	})
	sim.ai_enabled = false
	_check("source_backed_builder_spawns_for_player", sim.entities.has(3) and bool(sim.entity(3).get("is_builder", false)))
	_check("combat_units_cannot_construct", not bool(sim.issue_construct([1], "farm", Vector2(-20, 30)).get("ok", false)))
	var before_resources := sim.resources_for_team(SimScript.PLAYER_TEAM)
	var result := sim.issue_construct([3], "farm", Vector2(-20, 30))
	_check("builder_accepts_farm_placement", bool(result.get("ok", false)), str(result))
	var structure_id := int(result.get("structure_id", 0))
	_check("farm_uses_ini_cost_and_time", int(result.get("cost", 0)) == 300 and int(result.get("build_ticks", 0)) == 220, str(result))
	_check("construction_cost_is_deducted_once", sim.resources_for_team(SimScript.PLAYER_TEAM) == before_resources - 300)
	_check("construction_site_starts_at_zero_progress", is_equal_approx(float(sim.structure(structure_id).get("construction_progress", -1.0)), 0.0))
	for _tick in range(300):
		if String(sim.entity(3).get("state", "")) == "construct":
			break
		sim.advance(1)
	_check("builder_routes_to_site_and_enters_work_state", String(sim.entity(3).get("state", "")) == "construct", str(sim.entity(3)))
	var progress_before := float(sim.structure(structure_id).get("construction_progress", 0.0))
	sim.advance(1)
	_check("working_builder_advances_authoritative_progress", float(sim.structure(structure_id).get("construction_progress", 0.0)) > progress_before)
	while float(sim.structure(structure_id).get("construction_progress", 0.0)) < 1.0 and sim.tick_index < 1000:
		sim.advance(1)
	_check("farm_completes_after_ini_build_duration", is_equal_approx(float(sim.structure(structure_id).get("construction_progress", 0.0)), 1.0))
	_check("builder_returns_idle_after_completion", int(sim.entity(3).get("construction_id", -1)) == 0 and String(sim.entity(3).get("state", "")) == "idle")
	_check("completed_farm_joins_economy_loop", int(sim.structure(structure_id).get("income_per_payout", 0)) == 25)
	_check("construction_completion_event_is_emitted", _event_count(sim.events, "construction.completed", 3, structure_id) == 1)
	print("RETAIL_BUILDER_CONSTRUCTION_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


func _unit_rules() -> Dictionary:
	var result := {}
	for object_id in [SimScript.SOLDIER_OBJECT_ID, SimScript.ARCHER_OBJECT_ID, SimScript.TOWER_GUARD_OBJECT_ID, SimScript.KNIGHT_OBJECT_ID]:
		result[object_id] = _unit_rule(object_id, 5, false)
	result[SimScript.BUILDER_OBJECT_ID] = _unit_rule(SimScript.BUILDER_OBJECT_ID, 1, true)
	return result


func _unit_rule(object_id: String, member_count: int, builder: bool) -> Dictionary:
	return {
		"horde_id": object_id,
		"member_count": member_count,
		"member_health": 500 if builder else 100,
		"member_damage": 1 if builder else 10,
		"speed": 6.0 if builder else 5.5,
		"speed_source": 60.0 if builder else 55.0,
		"acceleration": 6.0,
		"acceleration_source": 60.0,
		"turn_rate_degrees_per_second": 360.0,
		"braking": 6.0,
		"braking_source": 60.0,
		"attack_range": 0.0 if builder else 1.15,
		"attack_range_source": 0.0 if builder else 11.5,
		"minimum_attack_range": 0.0,
		"minimum_attack_range_source": 0.0,
		"vision_range": 2.5 if builder else 17.5,
		"vision_range_source": 25.0 if builder else 175.0,
		"delay_between_shots_ms": 1000.0,
		"pre_attack_delay_ms": 0.0,
		"firing_duration_ms": 0.0,
		"attack_period_ticks": 10,
		"pre_attack_ticks": 0,
		"firing_duration_ticks": 0,
		"formation_positions": [Vector3.ZERO] if builder else [Vector3.ZERO, Vector3.RIGHT, Vector3.LEFT, Vector3.FORWARD, Vector3.BACK],
		"stances": {"default": "Battle", "cycleOrder": ["HoldGround", "Battle", "Aggressive"], "states": {"HoldGround": {}, "Battle": {}, "Aggressive": {}}},
		"is_builder": builder,
		"provenance": {},
	}


func _event_count(events: Array, kind: String, entity_id: int, target_id: int) -> int:
	var result := 0
	for event_value in events:
		var event: Dictionary = event_value
		if String(event.get("kind", "")) == kind and int(event.get("entity_id", 0)) == entity_id and int(event.get("target_id", 0)) == target_id:
			result += 1
	return result


func _check(label: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("RETAIL_BUILDER_CONSTRUCTION PASS %s" % label)
	else:
		failed += 1
		push_error("RETAIL_BUILDER_CONSTRUCTION FAIL %s%s" % [label, " (%s)" % detail if detail != "" else ""])
