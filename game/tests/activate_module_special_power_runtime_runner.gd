extends SceneTree

# Focused executable receipt: ActivateModuleSpecialPower ->
# compiler-emitted activate-module-graph -> RetailSliceSim channel/route state.

const Sim = preload("res://src/retail_slice/retail_slice_sim.gd")
const Adapter = preload("res://src/retail_slice/playable_unit_runtime_adapter.gd")
const EXPECTED := 30
var passed := 0
var failed := 0

func _initialize() -> void:
	create_timer(30.0).timeout.connect(_watchdog)
	call_deferred("_run")

func _watchdog() -> void:
	push_error("ACTIVATE_MODULE_RUNTIME_FAIL watchdog exceeded 30 seconds")
	quit(1)

func _run() -> void:
	var sim := _sim()
	sim._unit_ability_rules[Sim.SOLDIER_HORDE_ID] = sim._scaled_ability_rules([_ability("Command_Graph", true)], 0.1)
	_spawn(sim, 1, 0, Vector2.ZERO); _spawn(sim, 2, 1, Vector2(5, 0)); _spawn(sim, 3, 1, Vector2(6.5, 0))
	var source := sim.entities[1] as Dictionary
	var enemy := sim.entities[2] as Dictionary
	var scaled := (sim._unit_ability_rules[Sim.SOLDIER_HORDE_ID] as Array)[0] as Dictionary
	var graph := scaled.get("effect", {}) as Dictionary
	_check("start_range_scaled", is_equal_approx(float(graph.get("range", 0.0)), 10.0))
	_check("effect_range_scaled", is_equal_approx(float(graph.get("effect_range_scaled", 0.0)), 2.0))
	_check("timings_compile_to_exact_ticks", graph.get("timing_ticks", {}) == {"StartDelay":1,"UnpackTime":2,"PreparationTime":3,"PersistentPrepTime":1,"SpecialPowerDuration":4,"PackTime":2})
	_check("nested_leaf_scaled", is_equal_approx(float(_route_effect(graph, 1).get("damage_radius", 0.0)), 0.0))
	var cast := sim.cast_ability(1, "Command_Graph", Vector2(5, 0), 0)
	_check("cast_schedules_graph", bool(cast.get("ok", false)) and bool(cast.get("scheduled", false)))
	var channel := source.get("activate_module_channel", {}) as Dictionary
	_check("authored_activation_boundary", int(channel.get("activation_tick", -1)) == 7)
	_check("duration_and_pack_boundary", int(channel.get("active_end_tick", -1)) == 11 and int(channel.get("finish_tick", -1)) == 13)
	_check("current_target_identity_captured", int(channel.get("current_target_id", 0)) == 2 and String(channel.get("current_target_kind", "")) == "battalion")
	_check("cooldown_arms_at_cast_start", int(((source.get("ability_states", {}) as Dictionary).get("Command_Graph", {}) as Dictionary).get("cooldown_ready_tick", 0)) == 20)
	var health_before := int(enemy.get("health", 0))
	for tick in range(0, 7):
		sim.tick_index = tick; sim._step_activate_module_graph(source)
	_check("routes_wait_through_authored_prep", int(enemy.get("health", 0)) == health_before and not bool((source.get("activate_module_channel", {}) as Dictionary).get("dispatched", false)))
	var snap := sim.snapshot(); var hash := sim.state_hash(); var restored := _sim()
	_check("snapshot_restores_pre_dispatch", restored.restore(snap))
	_check("pre_dispatch_hash_round_trips", restored.state_hash() == hash)
	sim.tick_index = 7; sim._step_activate_module_graph(source)
	# Direct focused stepping bypasses tick()'s derived spatial-index rebuild;
	# restore correctly leaves that unhashed cache for the next tick boundary.
	restored._spatial_rebuild(); restored.tick_index = 7; restored._step_activate_module_graph(restored.entities[1] as Dictionary)
	_check("current_target_route_deals_damage", int(enemy.get("health", 0)) < health_before)
	_check("self_route_applies_modifier", (source.get("timed_modifiers", {}) as Dictionary).keys().any(func(key: Variant) -> bool: return String(key).begins_with("ability:Command_Graph")))
	_check("location_route_emits_fx", _has_event(sim, "ability.graph_fx"))
	_check("ordered_routes_exact", _route_tags(sim) == ["ModuleTag_SelfBuff", "ModuleTag_Blast", "ModuleTag_FX"])
	_check("route_modes_exact", _route_modes(sim) == ["SELF", "CURRENT_TARGET", "LOCATION"])
	_check("effect_range_fills_missing_leaf_radius", int((sim.entities[3] as Dictionary).get("health", 0)) < 100)
	_check("restored_dispatch_is_identical", restored.state_hash() == sim.state_hash())
	sim.issue_move([1], Vector2(9, 0), "order.move", 0)
	sim.tick_index = 8; sim._step_activate_module_graph(source)
	_check("must_finish_ignores_voluntary_order", source.has("activate_module_channel") and Vector2(source.get("destination", Vector2.ZERO)) == Vector2(9, 0))
	sim.tick_index = 13; sim._step_activate_module_graph(source)
	_check("channel_finishes_after_pack", not source.has("activate_module_channel") and _has_event(sim, "ability.graph_finished"))

	var cancel := _sim(); cancel._unit_ability_rules[Sim.SOLDIER_HORDE_ID] = cancel._scaled_ability_rules([_ability("Command_Cancel", false)], 0.1); _spawn(cancel,1,0,Vector2.ZERO); _spawn(cancel,2,1,Vector2(5,0))
	_check("interruptible_cast_starts", bool(cancel.cast_ability(1,"Command_Cancel",Vector2(5,0),0).get("ok",false)))
	cancel.issue_stop([1],0); cancel.tick_index=1; cancel._step_activate_module_graph(cancel.entities[1] as Dictionary)
	_check("voluntary_order_interrupts_when_allowed", not (cancel.entities[1] as Dictionary).has("activate_module_channel") and _has_event(cancel,"ability.graph_interrupted"))
	_check("interrupted_graph_never_dispatches", _route_tags(cancel).is_empty() and int((cancel.entities[2] as Dictionary).get("health",0)) == 100)

	var knocked := _sim(); knocked._unit_ability_rules[Sim.SOLDIER_HORDE_ID] = knocked._scaled_ability_rules([_ability("Command_Knocked",true)],0.1); _spawn(knocked,1,0,Vector2.ZERO); _spawn(knocked,2,1,Vector2(5,0)); knocked.cast_ability(1,"Command_Knocked",Vector2(5,0),0); (knocked.entities[1] as Dictionary)["knocked_down"]=true; knocked.tick_index=1; knocked._step_activate_module_graph(knocked.entities[1] as Dictionary)
	_check("unavoidable_knockdown_interrupts_must_finish", not (knocked.entities[1] as Dictionary).has("activate_module_channel"))

	var missing := _sim(); missing._unit_ability_rules[Sim.SOLDIER_HORDE_ID] = missing._scaled_ability_rules([_ability("Command_Missing",true)],0.1); _spawn(missing,1,0,Vector2.ZERO)
	_check("current_target_route_fails_closed_at_cast", String(missing.cast_ability(1,"Command_Missing",Vector2(5,0),0).get("reason","")) == "activate-module-current-target-missing")
	var retarget := _sim(); retarget._unit_ability_rules[Sim.SOLDIER_HORDE_ID] = retarget._scaled_ability_rules([_ability("Command_Retarget",true)],0.1); _spawn(retarget,1,0,Vector2.ZERO); _spawn(retarget,2,1,Vector2(5,0)); _spawn(retarget,3,1,Vector2(8,0)); (retarget.entities[1] as Dictionary)["target_id"]=3; (retarget.entities[1] as Dictionary)["target_kind"]="battalion"; retarget.cast_ability(1,"Command_Retarget",Vector2(5,0),0)
	_check("clicked_object_overrides_stale_attack_target", int(((retarget.entities[1] as Dictionary).get("activate_module_channel",{}) as Dictionary).get("current_target_id",0)) == 2)
	_check("adapter_accepts_compiler_graph", Adapter.ability_rules(_adapter_doc(_graph())).size() == 1)
	var malformed := _graph(); (malformed.get("routes",[]) as Array)[0] = {"moduleTag":"X","targetMode":"INVENTED","effect":{"kind":"heal"}}
	_check("adapter_rejects_malformed_target_mode", Adapter.ability_rules(_adapter_doc(malformed)).is_empty())
	var unsupported := _graph(); (((unsupported.get("routes",[]) as Array)[0] as Dictionary)["effect"] as Dictionary)["kind"] = "invented"
	var unsupported_sim := _sim(); unsupported_sim._unit_ability_rules[Sim.SOLDIER_HORDE_ID] = unsupported_sim._scaled_ability_rules([_ability_with_graph("Command_Unsupported",unsupported)],0.1); _spawn(unsupported_sim,1,0,Vector2.ZERO); _spawn(unsupported_sim,2,1,Vector2(5,0)); unsupported_sim.cast_ability(1,"Command_Unsupported",Vector2(5,0),0); unsupported_sim.tick_index=7; unsupported_sim._step_activate_module_graph(unsupported_sim.entities[1] as Dictionary)
	_check("unsupported_nested_leaf_receipted", _first_route_reason(unsupported_sim.entities[1] as Dictionary) == "activate-module-leaf-unsupported:invented")
	finish()

func _ability(command: String, must_finish: bool) -> Dictionary:
	var graph := _graph(); graph["mustFinishAbility"] = must_finish
	return _ability_with_graph(command, graph)

func _ability_with_graph(command: String, graph: Dictionary) -> Dictionary:
	return {"ability_id":command,"special_power_id":"SpecialFixtureGraph","targeting":"enemy-object","cooldown_ticks":20,"required_level":1,"level_gate_resolved":true,"castable":true,"effect":graph,"special_power_contract":{}}

func _graph() -> Dictionary:
	return {"kind":"activate-module-graph","specialPowerTemplateId":"SpecialFixtureGraph","startAbilityRange":100.0,"effectRange":20.0,"mustFinishAbility":true,"unpackingVariation":2,"timingMs":{"StartDelay":100,"UnpackTime":200,"PreparationTime":300,"PersistentPrepTime":100,"SpecialPowerDuration":400,"PackTime":200},"routes":[
		{"moduleTag":"ModuleTag_SelfBuff","targetMode":"SELF","authoredTargetMode":"SELF","targetModuleKind":"SpecialPowerModule","targetSpecialPowerTemplateId":"SpecialBuff","effect":{"kind":"attribute-modifier","durationMs":500,"modifier":{"damage_mult":1.2}}},
		{"moduleTag":"ModuleTag_Blast","targetMode":"CURRENT_TARGET","authoredTargetMode":"OBJECTPOS","targetModuleKind":"SpecialPowerModule","targetSpecialPowerTemplateId":"SpecialBlast","effect":{"kind":"weapon-blast","damage":25,"damageRadius":0.0,"attackRange":100.0}},
		{"moduleTag":"ModuleTag_FX","targetMode":"LOCATION","authoredTargetMode":"TARGETPOS","targetModuleKind":"SpecialPowerModule","targetSpecialPowerTemplateId":"SpecialFX","effect":{"kind":"trigger-fx","fxId":"FX_Fixture"}},
	]}

func _adapter_doc(graph: Dictionary) -> Dictionary:
	return {"registration":{"abilities":[{"id":"Command_Graph","slot":1,"targeting":"enemy-object","specialPowerId":"SpecialFixtureGraph","cooldownMs":1000,"button":{},"effect":graph,"implementation":{"status":"implemented","reason":"","limitations":[]},"levelGate":{}}]}}

func _route_effect(graph:Dictionary,index:int)->Dictionary:
	var routes:=graph.get("routes",[]) as Array
	return (routes[index] as Dictionary).get("effect",{}) as Dictionary if index>=0 and index<routes.size() else {}

func _first_route_reason(row:Dictionary)->String:
	var results:=((row.get("activate_module_channel",{}) as Dictionary).get("route_results",[]) as Array)
	return String((results[0] as Dictionary).get("reason","")) if not results.is_empty() else "missing-route-result"

func _sim() -> RetailSliceSim:
	var rules := {}; for object_id in [Sim.SOLDIER_OBJECT_ID,Sim.SOLDIER_HORDE_ID,Sim.ARCHER_OBJECT_ID,Sim.TOWER_GUARD_OBJECT_ID,Sim.KNIGHT_OBJECT_ID]: rules[object_id]=_rule()
	var sim:RetailSliceSim=Sim.new(); sim.setup({}, {"unit_rules":rules,"source_map_transform_scale":0.1}); sim.ai_enabled=false; sim.base_loop_enabled=false; sim.entities.clear(); sim.structures.clear(); return sim

func _spawn(sim:RetailSliceSim,id:int,team:int,point:Vector2)->void:
	sim._add_battalion(id,team,point,"Fixture",Sim.SOLDIER_OBJECT_ID,Sim.SOLDIER_HORDE_ID,0,_rule()); sim._attach_hero_ability_state(sim.entities[id] as Dictionary)

func _rule()->Dictionary:
	return {"horde_id":Sim.SOLDIER_HORDE_ID,"category":"hero","speed":1.0,"speed_source":10.0,"acceleration":1.0,"acceleration_source":10.0,"turn_rate_degrees_per_second":180.0,"braking":1.0,"braking_source":10.0,"attack_range":1.0,"attack_range_source":1.0,"minimum_attack_range":0.0,"minimum_attack_range_source":0.0,"vision_range":20.0,"vision_range_source":200.0,"delay_between_shots_ms":1000.0,"pre_attack_delay_ms":0.0,"firing_duration_ms":0.0,"attack_period_ticks":10,"pre_attack_ticks":0,"firing_duration_ticks":0,"member_damage":1,"member_health":100,"member_count":1,"formation_positions":[Vector3.ZERO],"provenance":{}}

func _route_tags(sim:RetailSliceSim)->Array:
	var output:Array=[]; for event_value in sim.events:
		var event:=event_value as Dictionary; if String(event.get("kind",""))=="ability.graph_route":output.append(String(event.get("module_tag","")))
	return output

func _route_modes(sim:RetailSliceSim)->Array:
	var output:Array=[]; for event_value in sim.events:
		var event:=event_value as Dictionary; if String(event.get("kind",""))=="ability.graph_route":output.append(String(event.get("target_mode","")))
	return output

func _has_event(sim:RetailSliceSim,kind:String)->bool:
	return sim.events.any(func(event:Dictionary)->bool:return String(event.get("kind",""))==kind)

func _check(label:String,condition:bool)->void:
	if condition:passed+=1
	else:failed+=1;push_error("ACTIVATE_MODULE_RUNTIME_FAIL "+label)

func finish()->void:
	var ran:=passed+failed;if ran!=EXPECTED:failed+=1;push_error("ACTIVATE_MODULE_RUNTIME_FAIL liveness ran=%d expected=%d"%[ran,EXPECTED])
	print("ACTIVATE_MODULE_RUNTIME_RESULT passed=%d failed=%d"%[passed,failed]);quit(0 if failed==0 else 1)
