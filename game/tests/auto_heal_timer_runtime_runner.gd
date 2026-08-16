extends SceneTree

## Exact AutoHealBehavior cadence for the importer's closed self-heal subset.
## Retail authors heroes as HERO_HEAL_AMOUNT (30) every 1000ms restarted
## HERO_HEAL_DELAY (15000ms) after the last damage; this runner proves the
## authored milliseconds, the damage-anchored restart, the max-health cap, the
## ungated (HealOnlyIfNotInCombat = No) cadence, the structure path, and that a
## sub-tick HealingDelay refuses instead of inventing a rate.
const Sim = preload("res://src/retail_slice/retail_slice_sim.gd")
var passed := 0
var failed := 0
var _finished := false
var _frames := 0


func _initialize() -> void:
	call_deferred("_run")


func _process(_delta: float) -> bool:
	# Liveness guard: a silent abort inside _run would otherwise idle forever and
	# read as a hang rather than a failure.
	_frames += 1
	if _finished:
		return true
	if _frames > 600:
		push_error("AUTO_HEAL_TIMER_RUNTIME_FAIL runner_aborted_before_reporting")
		print("AUTO_HEAL_TIMER_RUNTIME_RESULT passed=%d failed=%d" % [passed, failed + 1])
		quit(1)
		return true
	return false


func _run() -> void:
	_gated_hero_cadence()
	_ungated_cadence_ignores_damage()
	_max_health_cap_and_dead_members()
	_structure_self_heal()
	_deferred_and_sub_tick_shapes_refuse()
	_finished = true
	print("AUTO_HEAL_TIMER_RUNTIME_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


func _new_sim() -> RetailSliceSim:
	var sim: RetailSliceSim = Sim.new()
	sim.setup({}, {"unit_rules": _rules()})
	sim.ai_enabled = false
	sim.base_loop_enabled = false
	sim.entities.clear()
	sim.structures.clear()
	return sim


func _gated_hero_cadence() -> void:
	var sim := _new_sim()
	sim.register_unit_module_contracts("FixtureHero", [_contract()])
	sim._add_battalion(1, Sim.PLAYER_TEAM, Vector2.ZERO, "FixtureHero", "FixtureHero", "FixtureHero", 0, _hero_rule())
	var hero := sim.entities[1] as Dictionary
	hero["member_health"] = [50]
	hero["health"] = 50
	hero["last_damage_tick"] = 0

	_check("contract_attaches", hero.has("auto_heal_behavior"))
	var heal_behavior := hero.get("auto_heal_behavior", {}) as Dictionary
	_check("healing_delay_1000ms_equals_10ticks", int(heal_behavior.get("healing_delay_ticks", -1)) == 10)
	_check("start_delay_5000ms_equals_50ticks", int(heal_behavior.get("start_delay_ticks", -1)) == 50)
	_check("authored_amount_is_not_a_percentage", int(heal_behavior.get("healing_amount", -1)) == 10)

	for tick in range(1, 50):
		sim.tick_index = tick
		sim._step_auto_heal_updates()
	_check("restart_delay_blocks_early_heal", int(hero["health"]) == 50)

	sim.tick_index = 50
	sim._step_auto_heal_updates()
	_check("heals_exact_amount_at_restart", int(hero["health"]) == 60)

	for tick in range(51, 60):
		sim.tick_index = tick
		sim._step_auto_heal_updates()
	_check("cadence_blocks_ninth_tick", int(hero["health"]) == 60)

	sim.tick_index = 60
	sim._step_auto_heal_updates()
	_check("cadence_heals_on_tenth_tick", int(hero["health"]) == 70)

	hero["last_damage_tick"] = sim.tick_index
	for tick in range(61, 110):
		sim.tick_index = tick
		sim._step_auto_heal_updates()
	_check("new_damage_restarts_delay", int(hero["health"]) == 70)

	sim.tick_index = 110
	sim._step_auto_heal_updates()
	_check("healing_resumes_after_second_restart", int(hero["health"]) == 80)

	# The provisional percentage regeneration must not stack on top.
	sim.tick_index = 111
	sim._step_hero_regeneration()
	_check("provisional_hero_regen_yields_to_authored_contract", int(hero["health"]) == 80)


func _ungated_cadence_ignores_damage() -> void:
	var sim := _new_sim()
	var contract := _contract()
	(contract["fields"] as Dictionary)["HealOnlyIfNotInCombat"] = {"value": false}
	sim.register_unit_module_contracts("FixtureHero", [contract])
	sim._add_battalion(2, Sim.PLAYER_TEAM, Vector2.ZERO, "FixtureHero", "FixtureHero", "FixtureHero", 0, _hero_rule())
	var hero := sim.entities[2] as Dictionary
	hero["member_health"] = [10]
	hero["health"] = 10
	hero["last_damage_tick"] = 0

	for tick in range(1, 50):
		sim.tick_index = tick
		sim._step_auto_heal_updates()
	_check("ungated_start_delay_still_applies", int(hero["health"]) == 10)

	sim.tick_index = 50
	sim._step_auto_heal_updates()
	_check("ungated_heals_after_start_delay", int(hero["health"]) == 20)

	# Damage does not restart an ungated timer: the pulse keeps its phase.
	hero["last_damage_tick"] = 55
	sim.tick_index = 60
	sim._step_auto_heal_updates()
	_check("ungated_damage_does_not_restart_delay", int(hero["health"]) == 30)


func _max_health_cap_and_dead_members() -> void:
	var sim := _new_sim()
	sim.register_unit_module_contracts("FixtureHero", [_contract()])
	var rule := _hero_rule()
	rule["member_count"] = 2
	rule["formation_positions"] = [Vector3.ZERO, Vector3.ZERO]
	sim._add_battalion(3, Sim.PLAYER_TEAM, Vector2.ZERO, "FixtureHero", "FixtureHero", "FixtureHero", 0, rule)
	var battalion := sim.entities[3] as Dictionary
	battalion["member_health"] = [95, 0]
	battalion["health"] = 95
	battalion["last_damage_tick"] = 0

	sim.tick_index = 50
	sim._step_auto_heal_updates()
	var members: Array = battalion["member_health"]
	_check("heal_caps_at_member_maximum", int(members[0]) == 100)
	_check("dead_member_is_not_revived", int(members[1]) == 0)
	_check("aggregate_health_matches_members", int(battalion["health"]) == 100)

	sim.tick_index = 60
	sim._step_auto_heal_updates()
	_check("full_survivor_does_not_overheal", int(battalion["health"]) == 100)


func _structure_self_heal() -> void:
	var sim := _new_sim()
	sim.register_structure_module_contracts("FixtureWell", [_contract()])
	sim.structures[10] = {
		"id": 10, "team": Sim.PLAYER_TEAM, "source_object_id": "FixtureWell",
		"structure_kind": "fixture-well", "health": 500, "maximum_health": 1000,
		"construction_progress": 1.0, "last_damage_tick": 0,
	}
	for tick in range(1, 50):
		sim.tick_index = tick
		sim._step_auto_heal_updates()
	_check("structure_restart_delay_blocks_early_heal", int((sim.structures[10] as Dictionary)["health"]) == 500)
	sim.tick_index = 50
	sim._step_auto_heal_updates()
	_check("structure_heals_authored_amount", int((sim.structures[10] as Dictionary)["health"]) == 510)
	sim.tick_index = 60
	sim._step_auto_heal_updates()
	_check("structure_honors_authored_cadence", int((sim.structures[10] as Dictionary)["health"]) == 520)


func _deferred_and_sub_tick_shapes_refuse() -> void:
	# A deferred importer row (StartsActive = No, area heal, button burst) must
	# not arm a timer, and a sub-tick HealingDelay must refuse outright.
	var sim := _new_sim()
	var deferred := _contract()
	deferred["executable"] = false
	deferred["runtime_status"] = "deferred"
	sim.register_unit_module_contracts("FixtureHero", [deferred])
	sim._add_battalion(4, Sim.PLAYER_TEAM, Vector2.ZERO, "FixtureHero", "FixtureHero", "FixtureHero", 0, _hero_rule())
	var hero := sim.entities[4] as Dictionary
	_check("deferred_row_arms_no_timer", not hero.has("auto_heal_behavior"))

	var sub_tick := _new_sim()
	var fast := _contract()
	(fast["fields"] as Dictionary)["HealingDelay"] = {"milliseconds": 30}
	sub_tick.register_unit_module_contracts("FixtureHero", [fast])
	sub_tick._add_battalion(5, Sim.PLAYER_TEAM, Vector2.ZERO, "FixtureHero", "FixtureHero", "FixtureHero", 0, _hero_rule())
	var fast_hero := sub_tick.entities[5] as Dictionary
	_check("sub_tick_delay_refuses", not fast_hero.has("auto_heal_behavior"))


func _contract() -> Dictionary:
	return {
		"module": "AutoHealBehavior",
		"extraction": "typed",
		"executable": true,
		"runtime_status": "executable",
		"tag": "ModuleTag_FixtureHealing",
		"fields": {
			"StartsActive": {"value": true},
			"HealingAmount": {"value": 10},
			"HealingDelay": {"milliseconds": 1000},
			"StartHealingDelay": {"milliseconds": 5000},
			"HealOnlyIfNotInCombat": {"value": true},
		}
	}


func _check(label: String, condition: bool) -> void:
	if condition:
		passed += 1
	else:
		failed += 1
		push_error("AUTO_HEAL_TIMER_RUNTIME_FAIL %s" % label)


func _rules() -> Dictionary:
	var output := {"FixtureHero": _hero_rule()}
	for object_id in [Sim.SOLDIER_OBJECT_ID, Sim.SOLDIER_HORDE_ID, Sim.ARCHER_OBJECT_ID, Sim.TOWER_GUARD_OBJECT_ID, Sim.KNIGHT_OBJECT_ID]:
		output[object_id] = _hero_rule().duplicate(true)
	return output


func _hero_rule() -> Dictionary:
	return {
		"horde_id": "FixtureHero", "category": "hero", "speed": 1.0,
		"speed_source": 10.0, "acceleration": 1.0, "acceleration_source": 10.0,
		"turn_rate_degrees_per_second": 180.0, "braking": 1.0, "braking_source": 10.0,
		"attack_range": 1.0, "attack_range_source": 10.0, "minimum_attack_range": 0.0,
		"minimum_attack_range_source": 0.0, "vision_range": 10.0, "vision_range_source": 100.0,
		"delay_between_shots_ms": 100.0, "pre_attack_delay_ms": 0.0, "firing_duration_ms": 0.0,
		"attack_period_ticks": 1, "pre_attack_ticks": 0, "firing_duration_ticks": 0,
		"member_damage": 1, "member_health": 100, "member_count": 1,
		"formation_positions": [Vector3.ZERO], "provenance": {},
	}
