extends SceneTree

const Adapter = preload("res://src/retail_slice/playable_unit_runtime_adapter.gd")
const Sim = preload("res://src/retail_slice/retail_slice_sim.gd")

var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var source := Adapter.simulation_rule(_document(), false)
	_check(not source.is_empty(), "scenario source rule resolves")
	_check(bool(source.get("scenario_only", false)), "scenario-only receipt survives")
	_check(not source.has("default_cost") and not source.has("default_build_ticks") and not source.has("default_command_points"), "production numbers stay absent")
	var rule := Adapter.normalized_unit_rule(source, 1.0)
	_check(not rule.is_empty() and bool(rule.get("noncombatant", false)), "normalized rule is explicit noncombatant")
	_check(int(rule.get("member_damage", -1)) == 0, "noncombatant damage remains zero")

	var sim = Sim.new()
	sim.setup({}, {"unit_rules": {}, "spawn_initial_battalions": false, "enable_base_loop": false})
	sim.ai_enabled = false
	sim.base_loop_enabled = false
	sim.entities.clear()
	sim.structures.clear()
	sim._add_battalion(1, Sim.PLAYER_TEAM, Vector2.ZERO, "Passive", "PassiveAnimal", "PassiveAnimal", 0, rule)
	var hostile := _hostile_rule()
	sim._add_battalion(2, Sim.ENEMY_TEAM, Vector2(1, 0), "Target", "Target", "Target", 0, hostile)
	_check(int((sim.entities[1] as Dictionary).get("member_damage", -1)) == 0, "sim preserves sealed noncombatant zero")
	_check(bool((sim.entities[1] as Dictionary).get("noncombatant", false)), "sim row preserves noncombatant marker")
	_check(sim.issue_attack([1], 2, Sim.PLAYER_TEAM) == 0, "noncombatant attack is refused")

	var malformed := hostile.duplicate(true)
	malformed.erase("member_damage")
	sim._add_battalion(3, Sim.PLAYER_TEAM, Vector2(0, 1), "Malformed", "Malformed", "Malformed", 0, malformed)
	_check(int((sim.entities[3] as Dictionary).get("member_damage", 0)) == 1, "ordinary missing damage retains clamp")
	var zeroed := hostile.duplicate(true)
	zeroed["member_damage"] = 0
	sim._add_battalion(4, Sim.PLAYER_TEAM, Vector2(0, 2), "Zeroed", "Zeroed", "Zeroed", 0, zeroed)
	_check(int((sim.entities[4] as Dictionary).get("member_damage", 0)) == 1, "ordinary zero damage retains clamp")
	_check(sim.issue_attack([4], 2, Sim.PLAYER_TEAM) == 1, "ordinary hostile attack remains accepted")

	print("SCENARIO_NONCOMBATANT_RUNTIME_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


func _document() -> Dictionary:
	return {
		"objectId": "PassiveAnimal",
		"category": "infantry",
		"registration": {
			"production": [],
			"composition": {"containerObjectId": "PassiveAnimal", "primaryMemberObjectId": "PassiveAnimal"},
			"kindOf": {"container": ["INFANTRY", "NOT_AUTOACQUIRABLE"], "primaryMember": ["INFANTRY", "NOT_AUTOACQUIRABLE"]},
			"simulation": {
				"status": "ready",
				"missing": [],
				"resolved": {
					"scenarioOnly": {"value": true, "disposition": "explicit-scenario-admission"},
					"displayNameId": {"value": "OBJECT:PassiveAnimal"},
					"memberCount": {"value": 1},
					"memberHealth": {"value": 50},
					"speed": {"value": 9},
					"visionRange": {"value": 121},
					"movement": {"acceleration": {"value": 100}, "braking": {"value": 50}, "turnRateDegreesPerSecond": {"value": 90}},
					"formation": {"memberCount": 1, "positions": [{"x": 0, "y": 0}], "source": "singleton-composition"},
					"combat": {"disposition": "noncombatant", "evidence": "no-effective-weapon-or-damage-route", "kindOfEvidence": ["NOT_AUTOACQUIRABLE"]},
				},
			},
		},
	}


func _hostile_rule() -> Dictionary:
	return {
		"horde_id": "Hostile", "category": "infantry", "member_count": 1,
		"member_health": 100, "member_damage": 5, "speed": 1.0, "speed_source": 10.0,
		"acceleration": 1.0, "acceleration_source": 10.0, "braking": 1.0,
		"braking_source": 10.0, "turn_rate_degrees_per_second": 180.0,
		"attack_range": 1.0, "attack_range_source": 10.0, "minimum_attack_range": 0.0,
		"minimum_attack_range_source": 0.0, "vision_range": 10.0, "vision_range_source": 100.0,
		"delay_between_shots_ms": 1000.0, "pre_attack_delay_ms": 0.0,
		"firing_duration_ms": 0.0, "attack_period_ticks": 10, "pre_attack_ticks": 0,
		"firing_duration_ticks": 0, "formation_positions": [Vector3.ZERO], "provenance": {},
	}


func _check(condition: bool, label: String) -> void:
	if condition:
		passed += 1
	else:
		failed += 1
		push_error("SCENARIO_NONCOMBATANT_RUNTIME_FAIL %s" % label)
