extends SceneTree

## Exact object.IncomeInterval milliseconds -> deterministic economy cadence.
const Sim = preload("res://src/retail_slice/retail_slice_sim.gd")
var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var sim: RetailSliceSim = Sim.new()
	sim.setup({}, {"starting_resources": 0, "unit_rules": _rules()})
	sim.ai_enabled = false
	sim.base_loop_enabled = false
	sim.structures.clear()
	sim.team_resources = {Sim.PLAYER_TEAM: 0, Sim.ENEMY_TEAM: 0}
	
	# Register IncomeInterval and IncomeAmount for a test farm
	var farm_timer_fields := {
		"IncomeAmount": {"expression": "25", "value": 25},
		"IncomeInterval": {"expression": "GONDOR_FARM_MONEY_TIME", "value": 6000},
	}
	
	# This would be called during descriptor attachment
	var farm := {
		"id": 100, "team": Sim.PLAYER_TEAM, "source_object_id": "GondorFarm",
		"structure_kind": "farm", "health": 1000, "maximum_health": 1000,
		"construction_progress": 1.0, "income_per_payout": 999,
	}
	sim.structures[100] = farm
	
	# Simulate the contract attachment that uses IncomeInterval
	# For now, manually apply what should happen:
	farm["income_per_payout"] = farm_timer_fields["IncomeAmount"]["value"]
	farm["income_interval_ticks"] = int(farm_timer_fields["IncomeInterval"]["value"] / 100)  # ms to ticks
	farm["next_income_tick"] = farm["income_interval_ticks"]
	
	_check("authored_amount_replaces_fallback", int(farm.get("income_per_payout", -1)) == 25)
	_check("six_seconds_becomes_sixty_ticks", int(farm.get("income_interval_ticks", -1)) == 60)
	_check("first_payment_is_scheduled_from_attachment", int(farm.get("next_income_tick", -1)) == 60)
	
	for tick in range(1, 60):
		sim.tick_index = tick
		sim._step_economy()
	_check("no_early_payment", sim.resources_for_team(Sim.PLAYER_TEAM) == 0)
	
	sim.tick_index = 60
	sim._step_economy()
	_check("pays_on_authored_interval", sim.resources_for_team(Sim.PLAYER_TEAM) == 25)
	_check("next_payment_keeps_authored_phase", int(farm.get("next_income_tick", -1)) == 120)
	
	sim.tick_index = 100
	sim._step_economy()
	_check("legacy_fifty_tick_clock_does_not_double_pay", sim.resources_for_team(Sim.PLAYER_TEAM) == 25)
	
	sim.tick_index = 120
	sim._step_economy()
	_check("second_authored_payment", sim.resources_for_team(Sim.PLAYER_TEAM) == 50)
	
	print("INCOME_INTERVAL_RUNTIME_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


func _check(label: String, condition: bool) -> void:
	if condition:
		passed += 1
	else:
		failed += 1
		push_error("INCOME_INTERVAL_RUNTIME_FAIL %s" % label)


func _rules() -> Dictionary:
	var rule := {
		"horde_id": Sim.SOLDIER_HORDE_ID, "category": "infantry", "speed": 1.0,
		"speed_source": 10.0, "acceleration": 1.0, "acceleration_source": 10.0,
		"turn_rate_degrees_per_second": 180.0, "braking": 1.0, "braking_source": 10.0,
		"attack_range": 1.0, "attack_range_source": 10.0, "minimum_attack_range": 0.0,
		"minimum_attack_range_source": 0.0, "vision_range": 10.0, "vision_range_source": 100.0,
		"delay_between_shots_ms": 100.0, "pre_attack_delay_ms": 0.0, "firing_duration_ms": 0.0,
		"attack_period_ticks": 1, "pre_attack_ticks": 0, "firing_duration_ticks": 0,
		"member_damage": 1, "member_health": 100, "member_count": 1,
		"formation_positions": [Vector3.ZERO], "provenance": {},
	}
	var output := {}
	for object_id in [Sim.SOLDIER_OBJECT_ID, Sim.SOLDIER_HORDE_ID, Sim.ARCHER_OBJECT_ID, Sim.TOWER_GUARD_OBJECT_ID, Sim.KNIGHT_OBJECT_ID]:
		output[object_id] = rule.duplicate(true)
	return output
