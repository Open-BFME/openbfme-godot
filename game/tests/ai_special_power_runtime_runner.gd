extends SceneTree

const Sim = preload("res://src/retail_slice/retail_slice_sim.gd")
const EXPECTED := 29
var passed := 0
var failed := 0

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	var sim := _sim()
	sim._unit_module_contracts[Sim.SOLDIER_HORDE_ID] = [
		_contract("BlastAI", 10, "Command_FixtureBlast", "AI_SPECIAL_POWER_RANGED_AOE_ATTACK", {"SpecialPowerRadius": _literal(20), "SpecialPowerRange": _literal(100)}),
		_contract("BuffAI", 20, "Command_FixtureBuff", "AI_SPECIAL_POWER_BASIC_SELF_BUFF", {}),
		_contract("UnknownAI", 30, "Command_Nothing", "AI_INVENTED", {}),
		_contract("DefineAI", 40, "Command_FixtureBlast", "AI_SPECIAL_POWER_RANGED_AOE_ATTACK", {"SpecialPowerRange": {"expression":"MISSING_DEFINE","sourceIni":"fixture.ini","line":44}}),
	]
	sim._unit_ability_rules[Sim.SOLDIER_HORDE_ID] = sim._scaled_ability_rules([_blast_rule(), _buff_rule()], 0.1)
	_spawn(sim, 1, 1, Vector2.ZERO)
	_spawn(sim, 2, 0, Vector2(8, 0))
	_spawn(sim, 3, 0, Vector2(8.5, 0))
	_spawn(sim, 4, 0, Vector2(4, 0))
	var source := sim.entities[1] as Dictionary
	var policies := source.get("ai_special_power_updates", []) as Array
	_check("all_typed_rows_attach", policies.size() == 4)
	_check("literal_radius_preserved", is_equal_approx(float((policies[0] as Dictionary).get("radius_source", 0.0)), 20.0))
	_check("one_tick_update_cadence", int((policies[0] as Dictionary).get("next_check_tick", -1)) == 0)
	_check("unknown_type_fail_closed_receipt", String(((policies[2] as Dictionary).get("unsupported_semantics", []) as Array)[0]).begins_with("unsupported_ai_type:"))
	_check("unresolved_define_fail_closed_receipt", String(((policies[3] as Dictionary).get("unsupported_semantics", []) as Array)[0]) == "unresolved_range_define:MISSING_DEFINE")
	var clustered_before := int((sim.entities[2] as Dictionary).get("health", 0)) + int((sim.entities[3] as Dictionary).get("health", 0))
	var isolated_before := int((sim.entities[4] as Dictionary).get("health", 0))
	sim._step_ai_special_power_updates()
	policies = source.get("ai_special_power_updates", []) as Array
	_check("highest_density_target_selected", int((policies[0] as Dictionary).get("last_target_id", 0)) == 2)
	_check("cluster_receives_cast", int((sim.entities[2] as Dictionary).get("health", 0)) + int((sim.entities[3] as Dictionary).get("health", 0)) < clustered_before)
	_check("isolated_enemy_outside_radius_unhurt", int((sim.entities[4] as Dictionary).get("health", 0)) == isolated_before)
	_check("authored_priority_stops_after_success", int((policies[1] as Dictionary).get("attempt_count", 0)) == 0)
	_check("cast_uses_real_cooldown", int((((source.get("ability_states", {}) as Dictionary)["Command_FixtureBlast"] as Dictionary).get("cooldown_ready_tick", 0))) == 5)
	sim.tick_index = 1
	sim._step_ai_special_power_updates()
	policies = source.get("ai_special_power_updates", []) as Array
	_check("cooldown_failure_allows_next_authored_row", int((policies[1] as Dictionary).get("cast_count", 0)) == 1)
	_check("self_buff_uses_cast_path", (source.get("timed_modifiers", {}) as Dictionary).has("ability:Command_FixtureBuff"))
	_check("unsupported_rows_never_attempt", int((policies[2] as Dictionary).get("attempt_count", 0)) == 0 and int((policies[3] as Dictionary).get("attempt_count", 0)) == 0)
	var snap := sim.snapshot()
	var before_hash := sim.state_hash()
	var restored := _sim()
	_check("snapshot_restores", restored.restore(snap))
	_check("state_hash_round_trips", restored.state_hash() == before_hash)
	_check("policy_counters_round_trip", int((((restored.entities[1] as Dictionary).get("ai_special_power_updates", []) as Array)[0] as Dictionary).get("attempt_count", 0)) == int((policies[0] as Dictionary).get("attempt_count", 0)))

	var random_a := _random_sim(77)
	var random_b := _random_sim(77)
	random_a._step_ai_special_power_updates(); random_b._step_ai_special_power_updates()
	var random_policy_a := (((random_a.entities[1] as Dictionary).get("ai_special_power_updates", []) as Array)[0] as Dictionary)
	var random_policy_b := (((random_b.entities[1] as Dictionary).get("ai_special_power_updates", []) as Array)[0] as Dictionary)
	_check("randomized_target_consumes_logic_rng", random_a.logic_random_draws == 1)
	_check("randomized_target_is_seed_deterministic", int(random_policy_a.get("last_target_id", 0)) == int(random_policy_b.get("last_target_id", 0)))
	_check("randomized_twin_hashes_match", random_a.state_hash() == random_b.state_hash())

	var stance_sim := _stance_sim()
	stance_sim._step_ai_special_power_updates()
	_check("idle_ai_selects_battle_stance", String((stance_sim.entities[1] as Dictionary).get("stance", "")) == "Battle")
	(stance_sim.entities[1] as Dictionary)["target_id"] = 2
	(stance_sim.entities[1] as Dictionary)["state"] = "attack"
	stance_sim.tick_index = 1; stance_sim._step_ai_special_power_updates()
	_check("engaged_ai_selects_aggressive_stance", String((stance_sim.entities[1] as Dictionary).get("stance", "")) == "Aggressive")
	(stance_sim.entities[1] as Dictionary)["hold_ground"] = true
	stance_sim.tick_index = 2; stance_sim._step_ai_special_power_updates()
	_check("hold_ground_condition_selects_stance", String((stance_sim.entities[1] as Dictionary).get("stance", "")) == "HoldGround")

	var structure_sim := _structure_gate_sim()
	var structure_policy := (((structure_sim.entities[1] as Dictionary).get("ai_special_power_updates", []) as Array)[0] as Dictionary)
	var chosen := structure_sim._ai_special_power_target(structure_sim.entities[1] as Dictionary, structure_policy)
	_check("structure_spell_rejects_occupied_site", bool(chosen.get("ok", false)) and int(chosen.get("id", 0)) == 3)

	var range_sim := _sim()
	range_sim._rules["ai_special_power_defines"] = {"FIXTURE_RANGE": 90}
	var resolved_contract := _contract("Defined", 1, "Command_FixtureBlast", "AI_SPECIAL_POWER_RANGED_AOE_ATTACK", {"SpecialPowerRange":{"expression":"FIXTURE_RANGE","sourceIni":"fixture.ini","line":5}})
	var probe := {"position":Vector2.ZERO}; range_sim._attach_ai_special_power_contract(probe, resolved_contract)
	var resolved_policy := ((probe.get("ai_special_power_updates", []) as Array)[0] as Dictionary)
	_check("authored_define_resolves_from_rules", is_equal_approx(float(resolved_policy.get("range_source", 0.0)), 90.0) and (resolved_policy.get("unsupported_semantics", []) as Array).is_empty())
	var spell_sim := _spellbook_sim()
	var spell_source := spell_sim.entities[1] as Dictionary
	var spell_policy := ((spell_source.get("ai_special_power_updates", []) as Array)[0] as Dictionary)
	var spell_target := spell_sim._ai_special_power_target(spell_source, spell_policy)
	var spell_before := int((spell_sim.entities[2] as Dictionary).get("health", 0))
	var spell_cast := spell_sim._ai_special_power_cast(1, spell_source, spell_policy, Vector2(spell_target.get("point", Vector2.ZERO)))
	_check("spellbook_command_maps_to_power_id", bool(spell_cast.get("ok", false)))
	_check("spellbook_route_uses_real_effect", int((spell_sim.entities[2] as Dictionary).get("health", 0)) > spell_before)
	_check("spellbook_route_arms_real_cooldown", int(((spell_sim._power_cooldown_until[1] as Dictionary).get("SpellBookHeal", 0))) == 12)
	_check("success_event_has_typed_identity", _has_ai_cast_event(sim, "Command_FixtureBlast", "AI_SPECIAL_POWER_RANGED_AOE_ATTACK"))
	_check("ai_disabled_does_not_advance", _disabled_does_not_advance())
	finish()

func _random_sim(seed: int) -> RetailSliceSim:
	var sim := _sim(seed)
	sim._unit_module_contracts[Sim.SOLDIER_HORDE_ID] = [_contract("Random", 1, "Command_FixtureBlast", "AI_SPECIAL_POWER_RANGED_AOE_ATTACK", {"SpecialPowerRange":_literal(100),"RandomizeTargetLocation":{"value":true}})]
	sim._unit_ability_rules[Sim.SOLDIER_HORDE_ID] = sim._scaled_ability_rules([_blast_rule()], 0.1)
	_spawn(sim,1,1,Vector2.ZERO); _spawn(sim,2,0,Vector2(5,0)); _spawn(sim,3,0,Vector2(6,0))
	return sim

func _stance_sim() -> RetailSliceSim:
	var sim := _sim()
	sim._rules["attribute_modifier_rules"] = {"FixtureStanceAggressive":{"damage_mult":1.2},"FixtureStanceHoldGround":{"armor_mult":0.8}}
	sim._unit_module_contracts[Sim.SOLDIER_HORDE_ID] = [
		{"module":"StancesBehavior","extraction":"typed","tag":"Stances","line":1,"fields":{"StanceTemplate":{"value":"Fixture"}}},
		_contract("Battle",2,"Command_SetStanceBattle","AI_SPECIAL_POWER_STANCEBATTLE",{}),
		_contract("Aggressive",3,"Command_SetStanceAggressive","AI_SPECIAL_POWER_STANCEAGGRESSIVE",{}),
		_contract("Hold",4,"Command_SetStanceHoldGround","AI_SPECIAL_POWER_STANCEHOLDGROUND",{}),
	]
	_spawn(sim,1,1,Vector2.ZERO); _spawn(sim,2,0,Vector2(2,0))
	return sim

func _structure_gate_sim() -> RetailSliceSim:
	var sim := _sim()
	sim._unit_module_contracts[Sim.SOLDIER_HORDE_ID] = [_contract("StructureSpell",1,"Command_FixtureBlast","AI_SPECIAL_POWER_TARGETAOE_SUMMON",{"SpecialPowerRadius":_literal(20),"SpecialPowerRange":_literal(100),"SpellMakesAStructure":{"value":true}})]
	sim._unit_ability_rules[Sim.SOLDIER_HORDE_ID] = sim._scaled_ability_rules([_blast_rule()],0.1)
	_spawn(sim,1,1,Vector2.ZERO); _spawn(sim,2,0,Vector2(5,0)); _spawn(sim,3,0,Vector2(8,0))
	sim.structures[100] = {"id":100,"team":0,"health":100,"position":Vector2(5,0),"structure_kind":"fortress"}
	return sim

func _spellbook_sim() -> RetailSliceSim:
	var sim := _sim()
	sim._unit_module_contracts[Sim.SOLDIER_HORDE_ID] = [_contract("SpellHeal",1,"Command_SpellBookHeal","AI_SPELLBOOK_HEAL",{"SpecialPowerRadius":_literal(100),"SpecialPowerRange":_literal(200)})]
	_spawn(sim,1,1,Vector2.ZERO); _spawn(sim,2,1,Vector2.ZERO)
	var ally := sim.entities[2] as Dictionary; ally["member_health"] = [40]; ally["health"] = 40
	sim._spellbook_ready = true
	sim._spellbook_powers = {"SpellBookHeal":{"castable":true,"reload_ticks":12,"science_id":"ScienceHeal","effect":{"kind":"heal","range_source":100.0,"amount":0.5,"as_percent":true,"affects":"ALL ALLIES"}}}
	sim._spellbook_order.assign(["SpellBookHeal"])
	sim.purchased_powers[1] = ["SpellBookHeal"]
	sim._power_cooldown_until[1] = {}
	return sim

func _disabled_does_not_advance() -> bool:
	var sim := _random_sim(11); sim.ai_enabled = false
	var before := (((sim.entities[1] as Dictionary).get("ai_special_power_updates", []) as Array)[0] as Dictionary).duplicate(true)
	sim._step_ai_special_power_updates()
	return (((sim.entities[1] as Dictionary).get("ai_special_power_updates", []) as Array)[0] as Dictionary) == before and sim.logic_random_draws == 0

func _contract(tag: String, line: int, command: String, ai_type: String, extras: Dictionary) -> Dictionary:
	var fields := {"CommandButtonName":{"value":command,"sourceIni":"fixture.ini","line":line+1},"SpecialPowerAIType":{"value":ai_type,"sourceIni":"fixture.ini","line":line+2}}
	for key in extras: fields[key] = extras[key]
	return {"module":"AISpecialPowerUpdate","runtimeStatus":"deferred","extraction":"typed","tag":tag,"sourceIni":"fixture.ini","line":line,"fields":fields}

func _literal(value: float) -> Dictionary:
	return {"expression":str(value),"value":value,"sourceIni":"fixture.ini","line":9}

func _blast_rule() -> Dictionary:
	return {"ability_id":"Command_FixtureBlast","special_power_id":"SpecialFixtureBlast","targeting":"point","cooldown_ticks":5,"required_level":1,"level_gate_resolved":true,"castable":true,"effect":{"kind":"weapon-blast","damage":25,"damageRadius":20.0,"attackRange":100.0},"special_power_contract":{}}

func _buff_rule() -> Dictionary:
	return {"ability_id":"Command_FixtureBuff","special_power_id":"SpecialFixtureBuff","targeting":"self","cooldown_ticks":10,"required_level":1,"level_gate_resolved":true,"castable":true,"effect":{"kind":"attribute-modifier","durationMs":1000,"modifier":{"damage_mult":1.2}},"special_power_contract":{}}

func _sim(seed: int = 5) -> RetailSliceSim:
	var rules := {}; for oid in [Sim.SOLDIER_OBJECT_ID,Sim.SOLDIER_HORDE_ID,Sim.ARCHER_OBJECT_ID,Sim.TOWER_GUARD_OBJECT_ID,Sim.KNIGHT_OBJECT_ID]: rules[oid] = _unit_rule()
	var sim: RetailSliceSim = Sim.new(); sim.setup({}, {"unit_rules":rules,"source_map_transform_scale":0.1,"logic_random_seed":seed,"team_roster":[{"team":0,"is_ai":false},{"team":1,"is_ai":true}]})
	sim.ai_enabled = true; sim.base_loop_enabled = false; sim.entities.clear(); sim.structures.clear()
	return sim

func _spawn(sim: RetailSliceSim, id: int, team: int, point: Vector2) -> void:
	sim._add_battalion(id,team,point,"Fixture",Sim.SOLDIER_OBJECT_ID,Sim.SOLDIER_HORDE_ID,0,_unit_rule()); sim._attach_hero_ability_state(sim.entities[id] as Dictionary)

func _unit_rule() -> Dictionary:
	return {"horde_id":Sim.SOLDIER_HORDE_ID,"category":"hero","speed":1.0,"speed_source":10.0,"acceleration":1.0,"acceleration_source":10.0,"turn_rate_degrees_per_second":180.0,"braking":1.0,"braking_source":10.0,"attack_range":1.0,"attack_range_source":1.0,"minimum_attack_range":0.0,"minimum_attack_range_source":0.0,"vision_range":20.0,"vision_range_source":200.0,"delay_between_shots_ms":1000.0,"pre_attack_delay_ms":0.0,"firing_duration_ms":0.0,"attack_period_ticks":10,"pre_attack_ticks":0,"firing_duration_ticks":0,"member_damage":1,"member_health":100,"member_count":1,"formation_positions":[Vector3.ZERO],"provenance":{}}

func _has_ai_cast_event(sim: RetailSliceSim, command: String, ai_type: String) -> bool:
	for event_value in sim.events:
		var event := event_value as Dictionary
		if String(event.get("kind", "")) == "ai_special_power.cast" and String(event.get("command", "")) == command and String(event.get("ai_type", "")) == ai_type: return true
	return false

func _check(label: String, condition: bool) -> void:
	if condition: passed += 1
	else: failed += 1; push_error("AI_SPECIAL_POWER_RUNTIME_FAIL %s" % label)

func finish() -> void:
	var ran := passed + failed
	if ran != EXPECTED: failed += 1; push_error("AI_SPECIAL_POWER_RUNTIME_FAIL liveness ran=%d expected=%d" % [ran,EXPECTED])
	print("AI_SPECIAL_POWER_RUNTIME_RESULT passed=%d failed=%d" % [passed,failed])
	quit(0 if failed == 0 else 1)
