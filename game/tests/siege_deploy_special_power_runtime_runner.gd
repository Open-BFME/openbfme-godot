extends SceneTree

const Sim = preload("res://src/retail_slice/retail_slice_sim.gd")
const Adapter = preload("res://src/retail_slice/playable_unit_runtime_adapter.gd")
const EXPECTED := 28
var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var sim := _sim()
	var rules := Adapter.ability_rules(_document())
	_check("adapter_accepts_exact_siege_graph", rules.size() == 1)
	_check("adapter_preserves_any_relation_object_target", String((rules[0] as Dictionary).get("targeting", "")) == "object")
	_check("adapter_preserves_delays", int((rules[0] as Dictionary).get("effect", {}).get("lowerDelayMs", -1)) == 1200 and int((rules[0] as Dictionary).get("effect", {}).get("raiseDelayMs", -1)) == 2000)
	var malformed := _document(); (((malformed["registration"] as Dictionary)["abilities"] as Array)[0] as Dictionary)["effect"]["lowerDelayMs"] = -1
	_check("adapter_rejects_negative_delay", Adapter.ability_rules(malformed).is_empty())
	var malformed_receipt := _document(); (((malformed_receipt["registration"] as Dictionary)["abilities"] as Array)[0] as Dictionary)["effect"]["modelReceipts"] = "missing"
	_check("adapter_rejects_malformed_model_receipts", Adapter.ability_rules(malformed_receipt).is_empty())

	var actor := sim.entities[1] as Dictionary
	sim._unit_ability_rules["FixtureSiege"] = rules
	sim._attach_stop_special_power_contract(actor, _stop_contract())
	var cast := sim.cast_ability(1, "Command_Deploy", Vector2(10, 0), 0)
	_check("deploy_cast_succeeds", bool(cast.get("ok", false)) and int(cast.get("target_id", 0)) == 50)
	var channel := actor.get("siege_deploy_channel", {}) as Dictionary
	_check("lower_delay_is_exact_ticks", String(channel.get("phase", "")) == "lowering" and int(channel.get("phase_end_tick", -1)) == 12)
	_check("skip_adjust_preserves_position", Vector2(actor.get("position", Vector2.ONE)) == Vector2.ZERO)
	_check("cast_interrupts_route", (actor.get("route", []) as Array).is_empty() and String(actor.get("order_kind", "x")) == "")
	_check("initiate_sound_is_preserved", String(channel.get("initiate_sound_id", "")) == "SiegeLadderVoiceAttackMS")
	_check("wall_model_seam_is_receipted", (channel.get("model_receipts", []) as Array).size() == 1 and float(channel.get("extra_wall_distance_source", -1.0)) == 15.0)
	sim.tick_index = 11; sim._step_siege_deploy(actor)
	_check("does_not_deploy_early", String((actor.get("siege_deploy_channel", {}) as Dictionary).get("phase", "")) == "lowering")
	sim.tick_index = 12; sim._step_siege_deploy(actor)
	_check("deploys_on_exact_tick", String((actor.get("siege_deploy_channel", {}) as Dictionary).get("phase", "")) == "deployed" and bool((actor.get("object_status", {}) as Dictionary).get("DEPLOYED", false)))
	_check("passengers_evacuate_on_deploy", not sim.entity_container.has(2) and sim.passenger_count(1) == 0)
	_check("passenger_status_is_restored", not (sim.entities[2] as Dictionary).has("transport_prior_status") and (sim.entities[2] as Dictionary).get("object_status", {}) == {"ORIGINAL": true})
	_check("deploy_event_names_evacuated_passenger", _event_value(sim, "ability.siege_deployed", "evacuated_ids") == [2])
	var hash := sim.state_hash(); var snap := sim.snapshot(); var restored := _sim()
	_check("deployed_snapshot_restores", restored.restore(snap))
	_check("deployed_state_hash_round_trips", restored.state_hash() == hash and bool(((restored.entities[1] as Dictionary).get("object_status", {}) as Dictionary).get("DEPLOYED", false)))

	var stop := sim.activate_stop_special_power(1, "SpecialAbilityStop", 0)
	_check("exact_stop_starts_retraction", bool(stop.get("ok", false)) and String((actor.get("siege_deploy_channel", {}) as Dictionary).get("phase", "")) == "retracting")
	_check("raise_delay_is_exact_ticks", int((actor.get("siege_deploy_channel", {}) as Dictionary).get("phase_end_tick", -1)) == 32)
	sim.tick_index = 31; sim._step_siege_deploy(actor)
	_check("does_not_retract_early", actor.has("siege_deploy_channel") and bool((actor.get("object_status", {}) as Dictionary).get("DEPLOYED", false)))
	sim.tick_index = 32; sim._step_siege_deploy(actor)
	_check("retracts_on_exact_tick", not actor.has("siege_deploy_channel") and not bool((actor.get("object_status", {}) as Dictionary).get("DEPLOYED", false)) and not actor.has("siege_deployed"))
	_check("retract_event_emitted", int(_event_value(sim, "ability.siege_retracted", "target_id")) == 50)
	_check("stop_without_active_deploy_refuses", String(sim.activate_stop_special_power(1, "SpecialAbilityStop", 0).get("reason", "")) == "stop-power-not-active")
	var wrong_owner := sim.cast_ability(1, "Command_Deploy", Vector2(10, 0), 1)
	_check("wrong_owner_refuses", String(wrong_owner.get("reason", "")) == "wrong-owner")
	var wrong_target := sim.cast_ability(1, "Command_Deploy", Vector2(100, 0), 0)
	_check("missing_target_refuses", String(wrong_target.get("reason", "")) in ["object-filter-refused", "siege-deploy-target-missing"])
	(sim.structures[50] as Dictionary)["kind_of"] = ["STRUCTURE"]
	var plain_structure := sim.cast_ability(1, "Command_Deploy", Vector2(10, 0), 0)
	_check("retail_filter_accepts_plain_structure_union", bool(plain_structure.get("ok", false)))
	actor.erase("siege_deploy_channel"); actor.erase("siege_deployed"); actor.erase("object_status")
	var unseen := (rules[0] as Dictionary).duplicate(true); unseen["ability_id"] = "Command_Unseen"; unseen["effect"] = ((rules[0] as Dictionary).get("effect", {}) as Dictionary).duplicate(true); (unseen["effect"] as Dictionary)["skipAdjustPosition"] = false; sim._unit_ability_rules["FixtureSiege"].append(unseen)
	(sim.structures[50] as Dictionary)["kind_of"] = ["STRUCTURE", "WALK_ON_TOP_OF_WALL"]
	var unseen_result := sim.cast_ability(1, "Command_Unseen", Vector2(10, 0), 0)
	_check("unseen_position_adjustment_fails_closed", String(unseen_result.get("reason", "")) == "siege-deploy-position-adjustment-unsupported")

	if passed + failed != EXPECTED:
		failed += 1; push_error("SIEGE_DEPLOY_RUNTIME_FAIL liveness")
	print("SIEGE_DEPLOY_RUNTIME_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


func _document() -> Dictionary:
	return {"registration":{"stringBindings":{},"abilities":[{"id":"Command_Deploy","slot":1,"specialPowerId":"SpecialAbilitySiegeDeploy","targeting":"object","cooldownMs":0,"button":{"iconIds":[],"labelIds":[],"tooltipIds":[],"options":["NEED_TARGET_ENEMY_OBJECT","NEED_TARGET_ALLY_OBJECT","NEED_TARGET_NEUTRAL_OBJECT"]},"effect":{"kind":"siege-deploy","specialPowerTemplateId":"SpecialAbilitySiegeDeploy","targetMode":"TARGET_STRUCTURE","lowerDelayMs":1200,"raiseDelayMs":2000,"evacuatePassengersOnDeploy":true,"skipAdjustPosition":true,"initiateSoundId":"SiegeLadderVoiceAttackMS","extraWallDistanceSource":15.0,"modelReceipts":["wall-contact-offset:ExtraWallDistance requires retail docking geometry"]},"implementation":{"status":"implemented","reason":"","limitations":[]},"levelGate":{},"specialPowerContract":{"objectFilter":["NONE","+STRUCTURE","+WALK_ON_TOP_OF_WALL"]}}]}}


func _stop_contract() -> Dictionary:
	return {"module":"StopSpecialPower","runtimeStatus":"deferred","extraction":"typed","tag":"ModuleTag_Stop","sourceIni":"fixture/siege.ini","line":50,"fields":{"SpecialPowerTemplate":{"value":"SpecialAbilityStop"},"StopPowerTemplate":{"value":"SpecialAbilitySiegeDeploy"}},"effectGraph":{"kind":"stop-special-power","specialPowerTemplateId":"SpecialAbilityStop","stopPowerTemplateId":"SpecialAbilitySiegeDeploy","targetMode":"SELF","interruptsCurrentOrder":true,"linkedModule":{"kind":"SiegeDeploySpecialPower","tag":"ModuleTag_Deploy","sourceIni":"fixture/siege.ini","line":20},"sourceIni":"fixture/siege.ini","line":50}}


func _sim() -> RetailSliceSim:
	var sim: RetailSliceSim = Sim.new()
	var unit_rule := {"horde_id":Sim.SOLDIER_HORDE_ID,"category":"infantry","speed":1.0,"speed_source":10.0,"acceleration":1.0,"acceleration_source":10.0,"turn_rate_degrees_per_second":180.0,"braking":1.0,"braking_source":10.0,"attack_range":0.1,"attack_range_source":1.0,"minimum_attack_range":0.0,"minimum_attack_range_source":0.0,"vision_range":1.0,"vision_range_source":10.0,"delay_between_shots_ms":1000.0,"pre_attack_delay_ms":0.0,"firing_duration_ms":0.0,"attack_period_ticks":10,"pre_attack_ticks":0,"firing_duration_ticks":0,"member_damage":1,"member_health":100,"member_count":1,"formation_positions":[Vector3.ZERO],"provenance":{}}
	var unit_rules := {}; for object_id in [Sim.SOLDIER_OBJECT_ID, Sim.SOLDIER_HORDE_ID, Sim.ARCHER_OBJECT_ID, Sim.TOWER_GUARD_OBJECT_ID, Sim.KNIGHT_OBJECT_ID]: unit_rules[object_id] = unit_rule
	sim.setup({}, {"source_unit_scale":0.1,"unit_rules":unit_rules}); sim.ai_enabled=false; sim.base_loop_enabled=false
	sim.entities.clear(); sim.structures.clear(); sim.events.clear()
	sim.entities[1] = {"id":1,"team":0,"health":100,"unit_type":"FixtureSiege","level":1,"ability_states":{},"position":Vector2.ZERO,"destination":Vector2.ZERO,"route":[Vector2(3,0)],"order_kind":"move","state":"run","target_id":0,"target_kind":"","attack_move":false,"kind_of":["SIEGEENGINE"]}
	sim.entities[2] = {"id":2,"team":0,"health":100,"position":Vector2.ZERO,"object_status":{"CONTAINED":true},"transport_prior_status":{"ORIGINAL":true}}
	sim.structures[50] = {"id":50,"team":1,"health":100,"position":Vector2(10,0),"kind_of":["STRUCTURE","WALK_ON_TOP_OF_WALL"]}
	sim.containment[1] = [2]; sim.entity_container[2] = 1
	return sim


func _event_value(sim: RetailSliceSim, kind: String, key: String) -> Variant:
	for index in range(sim.events.size() - 1, -1, -1):
		var event := sim.events[index] as Dictionary
		if String(event.get("kind", "")) == kind:
			return event.get(key)
	return null


func _check(name: String, condition: bool) -> void:
	if condition: passed += 1
	else: failed += 1; push_error("SIEGE_DEPLOY_RUNTIME_FAIL " + name)
