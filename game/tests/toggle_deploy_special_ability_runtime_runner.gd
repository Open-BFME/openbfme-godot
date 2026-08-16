extends SceneTree

const Sim = preload("res://src/retail_slice/retail_slice_sim.gd")
const Adapter = preload("res://src/retail_slice/playable_unit_runtime_adapter.gd")
# Executable evidence for the paired retail Behavior modules:
# DeployStyleAIUpdate + ToggleDeploySpecialAbilityUpdate.
const EXPECTED := 40
var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var rules := Adapter.ability_rules(_document())
	_check("adapter_accepts_exact_toggle_graph", rules.size() == 1)
	_check("adapter_preserves_self_target", String((rules[0] as Dictionary).get("targeting", "")) == "self" and String((rules[0] as Dictionary).get("effect", {}).get("targetMode", "")) == "SELF")
	_check("adapter_preserves_exact_retail_times", int((rules[0] as Dictionary).get("effect", {}).get("unpackTimeMs", -1)) == 2000 and int((rules[0] as Dictionary).get("effect", {}).get("packTimeMs", -1)) == 2000)
	var malformed_target := _document(); (((malformed_target["registration"] as Dictionary)["abilities"] as Array)[0] as Dictionary)["effect"]["targetMode"] = "TARGET_STRUCTURE"
	_check("adapter_rejects_siege_target_conflation", Adapter.ability_rules(malformed_target).is_empty())
	var malformed_duration := _document(); (((malformed_duration["registration"] as Dictionary)["abilities"] as Array)[0] as Dictionary)["effect"]["unpackTimeMs"] = 0
	_check("adapter_rejects_zero_transition", Adapter.ability_rules(malformed_duration).is_empty())
	var malformed_modifier := _document(); (((malformed_modifier["registration"] as Dictionary)["abilities"] as Array)[0] as Dictionary)["effect"]["deployedAttributeModifier"]["durationMs"] = 1
	_check("adapter_rejects_nonpersistent_modifier", Adapter.ability_rules(malformed_modifier).is_empty())
	_check("nonhero_demolisher_has_ability_surface", Adapter.has_ability_surface({"category":"siege","unit_type":"FixtureDemolisher"}, {"FixtureDemolisher":rules}))

	var sim := _sim()
	var actor := sim.entities[1] as Dictionary
	sim._unit_ability_rules["FixtureDemolisher"] = sim._scaled_ability_rules(rules, 0.1)
	var cast := sim.cast_ability(1, "Command_SpecialAbilityDwarvenDemolisherDeploy", Vector2(99, 99), 0)
	_check("deploy_cast_succeeds", bool(cast.get("ok", false)) and String(cast.get("phase", "")) == "unpacking")
	var channel := actor.get("toggle_deploy_channel", {}) as Dictionary
	_check("unpack_is_exact_2000ms", String(channel.get("phase", "")) == "unpacking" and int(channel.get("phase_end_tick", -1)) == 20)
	_check("cast_interrupts_route", (actor.get("route", []) as Array).is_empty() and String(actor.get("order_kind", "x")) == "")
	_check("unpacking_model_condition", (actor.get("model_conditions", []) as Array) == ["UNPACKING"])
	_check("toggle_is_not_siege_deploy", not actor.has("siege_deploy_channel"))
	_check("deploy_sound_receipt", String(_event_value(sim, "ability.toggle_deploy_started", "sound_id")) == "DwarfDemolisherDeployMS")
	_check("unpack_presentation_receipt", String(_event_value(sim, "ability.toggle_deploy_started", "presentation_receipt")) == "model-condition:UNPACKING")
	_check("transition_recast_refuses", String(sim.cast_ability(1, "Command_SpecialAbilityDwarvenDemolisherDeploy", Vector2.ZERO, 0).get("reason", "")) == "toggle-deploy-transition-active")
	_check("unpacking_is_immobile", sim.issue_move([1], Vector2(3, 0), "order.move", 0) == 0 and sim.last_route_rejection == "toggle-deploy-immobile")
	_check("auto_ability_blocks_unpacking", sim._ability_auto_blocked_model_condition(actor, sim._unit_ability_rules["FixtureDemolisher"][0]) == "UNPACKING")
	sim.tick_index = 19; sim._step_toggle_deploy(actor)
	_check("does_not_deploy_early", actor.has("toggle_deploy_channel") and not bool((actor.get("object_status", {}) as Dictionary).get("DEPLOYED", false)))
	sim.tick_index = 20; sim._step_toggle_deploy(actor)
	_check("deploys_on_exact_tick", not actor.has("toggle_deploy_channel") and bool((actor.get("object_status", {}) as Dictionary).get("DEPLOYED", false)) and bool(actor.get("toggle_deployed", false)))
	var modifiers := actor.get("timed_modifiers", {}) as Dictionary
	_check("exact_armor_modifier_is_active", modifiers.has("ability:toggle-deploy") and float((((modifiers["ability:toggle-deploy"] as Dictionary)["modifiers"] as Array)[0] as Dictionary).get("value", 0.0)) == 1.0)
	_check("armor_100_percent_uses_runtime_cap", is_equal_approx(sim._ability_incoming_multiplier(actor), 0.05))
	_check("deployed_is_immobile", sim.issue_move([1], Vector2(3, 0), "order.move", 0) == 0 and sim.last_route_rejection == "toggle-deploy-immobile")
	_check("auto_ability_blocks_deployed", sim._ability_auto_blocked_model_condition(actor, sim._unit_ability_rules["FixtureDemolisher"][0]) == "DEPLOYED")
	var hash := sim.state_hash(); var snapshot := sim.snapshot(); var restored := _sim()
	_check("deployed_snapshot_restores", restored.restore(snapshot))
	_check("deployed_state_hash_round_trips", restored.state_hash() == hash and bool((restored.entities[1] as Dictionary).get("toggle_deployed", false)))
	_check("deployed_event_receipts_model", String(_event_value(sim, "ability.toggle_deployed", "presentation_receipt")) == "model-condition:DEPLOYED")

	var undeploy := sim.cast_ability(1, "Command_SpecialAbilityDwarvenDemolisherDeploy", Vector2.ZERO, 0)
	_check("undeploy_cast_succeeds", bool(undeploy.get("ok", false)) and String(undeploy.get("phase", "")) == "packing")
	channel = actor.get("toggle_deploy_channel", {}) as Dictionary
	_check("pack_is_exact_2000ms", String(channel.get("phase", "")) == "packing" and int(channel.get("phase_end_tick", -1)) == 40)
	_check("undeploy_sound_receipt", String(_event_value(sim, "ability.toggle_deploy_started", "sound_id")) == "DwarfDemolisherUndeployMS")
	_check("armor_remains_during_pack", is_equal_approx(sim._ability_incoming_multiplier(actor), 0.05))
	_check("packing_model_condition", (actor.get("model_conditions", []) as Array) == ["PACKING"])
	_check("packing_is_immobile", sim.issue_move([1], Vector2(3, 0), "order.move", 0) == 0 and sim.last_route_rejection == "toggle-deploy-immobile")
	sim.tick_index = 39; sim._step_toggle_deploy(actor)
	_check("does_not_unpack_early", actor.has("toggle_deploy_channel") and bool((actor.get("object_status", {}) as Dictionary).get("DEPLOYED", false)))
	sim.tick_index = 40; sim._step_toggle_deploy(actor)
	_check("undeploys_on_exact_tick", not actor.has("toggle_deploy_channel") and not bool((actor.get("object_status", {}) as Dictionary).get("DEPLOYED", false)) and not actor.has("toggle_deployed"))
	_check("armor_modifier_clears", is_equal_approx(sim._ability_incoming_multiplier(actor), 1.0) and not actor.has("timed_modifiers"))
	_check("movement_restores", sim.issue_move([1], Vector2(3, 0), "order.move", 0) == 1)
	_check("auto_ability_blocks_moving", sim._ability_auto_blocked_model_condition(actor, sim._unit_ability_rules["FixtureDemolisher"][0]) == "MOVING")
	_check("wrong_owner_refuses", String(sim.cast_ability(1, "Command_SpecialAbilityDwarvenDemolisherDeploy", Vector2.ZERO, 1).get("reason", "")) == "wrong-owner")
	_check("undeployed_event_receipts_model", String(_event_value(sim, "ability.toggle_undeployed", "presentation_receipt")) == "model-condition:CLEAR_DEPLOYED")
	_check("siege_channel_never_created", not actor.has("siege_deploy_channel") and not actor.has("siege_deployed"))

	if passed + failed != EXPECTED:
		failed += 1; push_error("TOGGLE_DEPLOY_RUNTIME_FAIL liveness")
	print("TOGGLE_DEPLOY_RUNTIME_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


func _document() -> Dictionary:
	return {"registration":{"stringBindings":{},"abilities":[{"id":"Command_SpecialAbilityDwarvenDemolisherDeploy","slot":2,"specialPowerId":"SpecialAbilityDwarvenDemolisherDeploy","targeting":"self","cooldownMs":0,"button":{"iconIds":["UCCommon_DemolisherDeploy"],"labelIds":["CONTROLBAR:SpecialAbilityDwarvenDemolisherDeploy"],"tooltipIds":["CONTROLBAR:TooltipSpecialAbilityDwarvenDemolisherDeploy"],"options":["OK_FOR_MULTI_EXECUTE","OK_FOR_MULTI_SELECT"]},"effect":{"kind":"toggle-deploy","autoAcquireEnabled":true,"autoAcquireModes":["ATTACK_BUILDINGS"],"moodAttackCheckRateMs":2500,"mustDeployToAttack":false,"unpackTimeMs":2000,"packTimeMs":2000,"deployedAttributeModifierId":"DwarvenDemolisherDeployModifier","specialPowerTemplateId":"SpecialAbilityDwarvenDemolisherDeploy","targetMode":"SELF","ignoreFacingCheck":true,"soundDeployId":"DwarfDemolisherDeployMS","soundUndeployId":"DwarfDemolisherUndeployMS","deployStyle":{"tag":"ModuleTag_03","sourceIni":"data/ini/object/goodfaction/units/dwarven/dwarvenram.ini","line":380},"deployedAttributeModifier":{"id":"DwarvenDemolisherDeployModifier","modifiers":[{"kind":"ARMOR","value":1.0,"application":"additive"}],"sourceIni":"data/ini/attributemodifier.ini","category":"SPELL","durationMs":0},"autoAbility":true,"triggerWhenReady":true,"autoAbilityBlockedModelConditions":["UNPACKING","DEPLOYED","PACKING","MOVING"]},"implementation":{"status":"implemented","reason":"","limitations":[]},"levelGate":{},"sourceIni":"data/ini/commandbutton.ini"}]}}


func _sim() -> RetailSliceSim:
	var sim: RetailSliceSim = Sim.new()
	var unit_rule := {"horde_id":Sim.SOLDIER_HORDE_ID,"category":"siege","speed":1.0,"speed_source":10.0,"acceleration":1.0,"acceleration_source":10.0,"turn_rate_degrees_per_second":180.0,"braking":1.0,"braking_source":10.0,"attack_range":0.1,"attack_range_source":1.0,"minimum_attack_range":0.0,"minimum_attack_range_source":0.0,"vision_range":1.0,"vision_range_source":10.0,"delay_between_shots_ms":1000.0,"pre_attack_delay_ms":0.0,"firing_duration_ms":0.0,"attack_period_ticks":10,"pre_attack_ticks":0,"firing_duration_ticks":0,"member_damage":1,"member_health":100,"member_count":1,"formation_positions":[Vector3.ZERO],"provenance":{}}
	var unit_rules := {}; for object_id in [Sim.SOLDIER_OBJECT_ID, Sim.SOLDIER_HORDE_ID, Sim.ARCHER_OBJECT_ID, Sim.TOWER_GUARD_OBJECT_ID, Sim.KNIGHT_OBJECT_ID]: unit_rules[object_id] = unit_rule
	sim.setup({}, {"source_unit_scale":0.1,"unit_rules":unit_rules}); sim.ai_enabled=false; sim.base_loop_enabled=false
	sim.entities.clear(); sim.structures.clear(); sim.events.clear()
	sim.entities[1] = {"id":1,"team":0,"health":100,"unit_type":"FixtureDemolisher","level":1,"ability_states":{},"position":Vector2.ZERO,"destination":Vector2.ZERO,"route":[Vector2(3,0)],"order_kind":"move","state":"run","target_id":0,"target_kind":"","attack_move":false,"kind_of":["SIEGEENGINE","MACHINE"]}
	return sim


func _event_value(sim: RetailSliceSim, kind: String, key: String) -> Variant:
	for index in range(sim.events.size() - 1, -1, -1):
		var event := sim.events[index] as Dictionary
		if String(event.get("kind", "")) == kind:
			return event.get(key)
	return null


func _check(name: String, condition: bool) -> void:
	if condition: passed += 1
	else: failed += 1; push_error("TOGGLE_DEPLOY_RUNTIME_FAIL " + name)
