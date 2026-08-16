extends SceneTree

# Executable receipt: DominateEnemySpecialPower -> compiler-emitted
# dominate-enemy graph -> deterministic RetailSliceSim allegiance channel.

const Sim = preload("res://src/retail_slice/retail_slice_sim.gd")
const Adapter = preload("res://src/retail_slice/playable_unit_runtime_adapter.gd")
const EXPECTED := 44
var passed := 0
var failed := 0


func _initialize() -> void:
	create_timer(30.0).timeout.connect(_watchdog)
	call_deferred("_run")


func _watchdog() -> void:
	push_error("DOMINATE_ENEMY_RUNTIME_FAIL watchdog exceeded 30 seconds")
	quit(1)


func _run() -> void:
	var graph := _graph(true)
	_check("adapter_accepts_retail_graph", Adapter.ability_rules(_adapter_doc(graph)).size() == 1)
	var malformed := graph.duplicate(true); malformed["startAbilityRange"] = -1
	_check("adapter_rejects_negative_range", Adapter.ability_rules(_adapter_doc(malformed)).is_empty())
	var sim := _sim(); sim._unit_ability_rules[Sim.SOLDIER_HORDE_ID] = sim._scaled_ability_rules([_ability(graph)], 0.1)
	_spawn(sim, 1, 0, Vector2.ZERO, "CasterHero", "hero")
	_spawn(sim, 2, 1, Vector2(5, 0), "EnemyInfantryA", "infantry")
	_spawn(sim, 3, 1, Vector2(7, 0), "EnemyInfantryB", "infantry")
	_spawn(sim, 4, 1, Vector2(6, 0), "EnemyHero", "hero")
	_spawn(sim, 5, 1, Vector2(20, 0), "OutsideInfantry", "infantry")
	_spawn(sim, 6, Sim.NEUTRAL_TEAM, Vector2(4, 0), "NeutralWarg", "monster")
	sim._attach_hero_ability_state(sim.entities[1] as Dictionary)
	var source := sim.entities[1] as Dictionary
	var scaled := ((sim._unit_ability_rules[Sim.SOLDIER_HORDE_ID] as Array)[0] as Dictionary).get("effect", {}) as Dictionary
	_check("start_range_scales", is_equal_approx(float(scaled.get("range", 0.0)), 20.0))
	_check("dominate_radius_scales", is_equal_approx(float(scaled.get("dominate_radius_scaled", 0.0)), 3.0))
	_check("timers_compile_to_ticks", scaled.get("timing_ticks", {}) == {"UnpackTime":2,"PreparationTime":1,"FreezeAfterTriggerDuration":2,"TriggerModelConditionDuration":1})
	var cast := sim.cast_ability(1, "Command_DominateFixture", Vector2(5, 0), 0)
	_check("cast_schedules_channel", bool(cast.get("ok", false)) and bool(cast.get("scheduled", false)))
	var channel := source.get("dominate_enemy_channel", {}) as Dictionary
	_check("activation_boundary_exact", int(channel.get("activation_tick", -1)) == 3)
	_check("freeze_boundary_exact", int(channel.get("finish_tick", -1)) == 5)
	_check("cooldown_arms_on_accept", int((((source.get("ability_states", {}) as Dictionary).get("Command_DominateFixture", {}) as Dictionary).get("cooldown_ready_tick", 0))) == 20)
	sim.tick_index = 2; sim._step_dominate_enemy(source)
	_check("no_conversion_before_trigger", int((sim.entities[2] as Dictionary).get("team", -1)) == 1)
	var snap := sim.snapshot(); var hash := sim.state_hash(); var restored := _sim()
	_check("snapshot_restores_channel", restored.restore(snap) and (restored.entities[1] as Dictionary).has("dominate_enemy_channel"))
	_check("channel_hash_round_trips", restored.state_hash() == hash)
	sim.tick_index = 3; sim._step_dominate_enemy(source)
	restored.tick_index = 3; restored._step_dominate_enemy(restored.entities[1] as Dictionary)
	var affected := ((source.get("dominate_enemy_channel", {}) as Dictionary).get("affected_ids", []) as Array)
	_check("radial_targets_are_sorted", affected == [2, 3, 6])
	_check("hostile_units_convert", int((sim.entities[2] as Dictionary).get("team", -1)) == 0 and int((sim.entities[3] as Dictionary).get("team", -1)) == 0)
	_check("neutral_units_convert_when_authored", int((sim.entities[6] as Dictionary).get("team", -1)) == 0)
	_check("excluded_hero_does_not_convert", int((sim.entities[4] as Dictionary).get("team", -1)) == 1)
	_check("outside_radius_does_not_convert", int((sim.entities[5] as Dictionary).get("team", -1)) == 1)
	_check("identity_and_health_preserved", int((sim.entities[2] as Dictionary).get("id", 0)) == 2 and int((sim.entities[2] as Dictionary).get("health", 0)) == 100)
	_check("prior_owner_is_receipted", int((sim.entities[2] as Dictionary).get("dominated_from_team", -1)) == 1)
	_check("converted_orders_are_cleared", int((sim.entities[2] as Dictionary).get("target_id", -1)) == 0 and (sim.entities[2] as Dictionary).get("route", []) == [])
	_check("trigger_event_emitted", _has_event(sim, "ability.dominate_triggered"))
	var trigger := _event(sim, "ability.dominate_triggered")
	_check("presentation_receipts_preserved", String(((trigger.get("presentation", {}) as Dictionary).get("trigger_fx_id", ""))) == "FX_DominateTrigger" and String(((trigger.get("presentation", {}) as Dictionary).get("trigger_sound_id", ""))) == "VoiceDominate")
	_check("restored_trigger_is_identical", restored.state_hash() == sim.state_hash())
	sim.tick_index = 4; sim._step_dominate_enemy(source)
	_check("caster_remains_frozen_after_trigger", source.has("dominate_enemy_channel"))
	sim.tick_index = 5; sim._step_dominate_enemy(source)
	_check("channel_finishes_at_exact_tick", not source.has("dominate_enemy_channel"))
	_check("finish_event_emitted", _has_event(sim, "ability.dominate_finished"))
	_check("wrong_owner_refused", String(sim.cast_ability(1, "Command_DominateFixture", Vector2(5,0), 1).get("reason", "")) == "wrong-owner")

	var temporary := _sim(); var temp_graph := _graph(false); temporary._unit_ability_rules[Sim.SOLDIER_HORDE_ID] = temporary._scaled_ability_rules([_ability(temp_graph)],0.1); _spawn(temporary,1,0,Vector2.ZERO,"CasterHero","hero"); _spawn(temporary,2,1,Vector2(5,0),"EnemyInfantryA","infantry"); temporary._attach_hero_ability_state(temporary.entities[1] as Dictionary)
	var temp_cast := temporary.cast_ability(1,"Command_DominateFixture",Vector2(5,0),0)
	_check("temporary_defect_cast_accepts_bound_target_timer", bool(temp_cast.get("ok", false)))
	temporary.tick_index=3; temporary._step_dominate_enemy(temporary.entities[1] as Dictionary)
	var defect := (temporary.entities[2] as Dictionary).get("temporary_defect", {}) as Dictionary
	_check("temporary_defect_converts_target", int((temporary.entities[2] as Dictionary).get("team",-1)) == 0)
	_check("temporary_defect_exact_30000ms_timer", int(defect.get("started_tick",-1)) == 3 and int(defect.get("expires_tick",-1)) == 303 and int(defect.get("duration_ticks",0)) == 300)
	var temp_snap := temporary.snapshot(); var temp_hash := temporary.state_hash(); var temp_restored := _sim()
	_check("temporary_defect_snapshot_restores", temp_restored.restore(temp_snap) and temp_restored.state_hash() == temp_hash)
	temporary.tick_index=302; temporary._step_temporary_defect(temporary.entities[2] as Dictionary)
	_check("temporary_defect_does_not_restore_early", int((temporary.entities[2] as Dictionary).get("team",-1)) == 0)
	temporary.tick_index=303; temporary._step_temporary_defect(temporary.entities[2] as Dictionary)
	temp_restored.tick_index=303; temp_restored._step_temporary_defect(temp_restored.entities[2] as Dictionary)
	_check("temporary_defect_restores_original_owner_exactly", int((temporary.entities[2] as Dictionary).get("team",-1)) == 1 and not (temporary.entities[2] as Dictionary).has("temporary_defect"))
	_check("temporary_restore_snapshot_path_is_identical", temporary.state_hash() == temp_restored.state_hash())
	_check("temporary_restore_emits_receipt", _has_event(temporary,"ability.temporary_defect_ended"))
	var dead_temp := _sim(); dead_temp._unit_ability_rules[Sim.SOLDIER_HORDE_ID]=dead_temp._scaled_ability_rules([_ability(temp_graph)],0.1); _spawn(dead_temp,1,0,Vector2.ZERO,"CasterHero","hero"); _spawn(dead_temp,2,1,Vector2(5,0),"EnemyInfantryA","infantry"); dead_temp._attach_hero_ability_state(dead_temp.entities[1] as Dictionary); dead_temp.cast_ability(1,"Command_DominateFixture",Vector2(5,0),0); dead_temp.tick_index=3; dead_temp._step_dominate_enemy(dead_temp.entities[1] as Dictionary); (dead_temp.entities[2] as Dictionary)["health"]=0; dead_temp._step_temporary_defect(dead_temp.entities[2] as Dictionary)
	_check("dead_temporary_target_clears_without_resurrection", not (dead_temp.entities[2] as Dictionary).has("temporary_defect") and int((dead_temp.entities[2] as Dictionary).get("health",1)) == 0)

	var empty := _sim(); empty._unit_ability_rules[Sim.SOLDIER_HORDE_ID]=empty._scaled_ability_rules([_ability(graph)],0.1); _spawn(empty,1,0,Vector2.ZERO,"CasterHero","hero"); _spawn(empty,2,1,Vector2(5,0),"EnemyHero","hero"); empty._attach_hero_ability_state(empty.entities[1] as Dictionary)
	_check("empty_filter_result_refuses", String(empty.cast_ability(1,"Command_DominateFixture",Vector2(5,0),0).get("reason","")) == "no-domination-target")
	_check("empty_cast_does_not_arm_cooldown", int(((((empty.entities[1] as Dictionary).get("ability_states",{}) as Dictionary).get("Command_DominateFixture",{}) as Dictionary).get("cooldown_ready_tick",-1))) == 0)
	_check("out_of_range_refuses", String(empty.cast_ability(1,"Command_DominateFixture",Vector2(50,0),0).get("reason","")) == "out-of-range")

	var object_sim := _sim(); var object_graph:=_graph(true); object_sim._unit_ability_rules[Sim.SOLDIER_HORDE_ID]=object_sim._scaled_ability_rules([_ability(object_graph,"enemy-object")],0.1); _spawn(object_sim,1,0,Vector2.ZERO,"CasterHero","hero"); _spawn(object_sim,2,1,Vector2(5,0),"EnemyInfantryA","infantry"); _spawn(object_sim,3,1,Vector2(5.5,0),"EnemyInfantryB","infantry"); object_sim._attach_hero_ability_state(object_sim.entities[1] as Dictionary)
	_check("object_target_cast_schedules", bool(object_sim.cast_ability(1,"Command_DominateFixture",Vector2(5.5,0),0).get("ok",false)))
	object_sim.tick_index=3; object_sim._step_dominate_enemy(object_sim.entities[1] as Dictionary)
	_check("object_target_converts_only_clicked_unit", int((object_sim.entities[2] as Dictionary).get("team",-1))==1 and int((object_sim.entities[3] as Dictionary).get("team",-1))==0)

	var interrupted := _sim(); interrupted._unit_ability_rules[Sim.SOLDIER_HORDE_ID]=interrupted._scaled_ability_rules([_ability(graph)],0.1); _spawn(interrupted,1,0,Vector2.ZERO,"CasterHero","hero"); _spawn(interrupted,2,1,Vector2(5,0),"EnemyInfantryA","infantry"); interrupted._attach_hero_ability_state(interrupted.entities[1] as Dictionary)
	_check("interrupt_fixture_casts", bool(interrupted.cast_ability(1,"Command_DominateFixture",Vector2(5,0),0).get("ok",false)))
	interrupted.issue_stop([1],0); interrupted.tick_index=1; interrupted._step_dominate_enemy(interrupted.entities[1] as Dictionary)
	_check("order_interrupts_before_trigger", not (interrupted.entities[1] as Dictionary).has("dominate_enemy_channel") and _has_event(interrupted,"ability.dominate_interrupted"))
	_check("interrupted_cast_never_converts", int((interrupted.entities[2] as Dictionary).get("team",-1))==1)
	finish()


func _sim() -> RetailSliceSim:
	var rules := {}
	for object_id in [Sim.SOLDIER_OBJECT_ID,Sim.SOLDIER_HORDE_ID,Sim.ARCHER_OBJECT_ID,Sim.TOWER_GUARD_OBJECT_ID,Sim.KNIGHT_OBJECT_ID]: rules[object_id]=_rule("FixtureInfantry","infantry")
	var sim:RetailSliceSim=Sim.new(); sim.setup({}, {"unit_rules":rules,"source_map_transform_scale":0.1}); sim.ai_enabled=false; sim.base_loop_enabled=false; sim.entities.clear(); sim.structures.clear(); return sim


func _spawn(sim:RetailSliceSim,id:int,team:int,point:Vector2,source_id:String,category:String)->void:
	var rule:=_rule(source_id,category); sim._rules["unit_rules"][Sim.SOLDIER_OBJECT_ID]=rule; sim._add_battalion(id,team,point,source_id,Sim.SOLDIER_OBJECT_ID,Sim.SOLDIER_HORDE_ID,0,rule)


func _rule(source_id:String,category:String)->Dictionary:
	return {"source_object_id":source_id,"horde_id":Sim.SOLDIER_HORDE_ID,"category":category,"speed":1.0,"speed_source":10.0,"acceleration":1.0,"acceleration_source":10.0,"turn_rate_degrees_per_second":180.0,"braking":1.0,"braking_source":10.0,"attack_range":1.0,"attack_range_source":10.0,"minimum_attack_range":0.0,"minimum_attack_range_source":0.0,"vision_range":20.0,"vision_range_source":200.0,"delay_between_shots_ms":1000.0,"pre_attack_delay_ms":0.0,"firing_duration_ms":0.0,"attack_period_ticks":10,"pre_attack_ticks":0,"firing_duration_ticks":0,"member_damage":10,"member_health":100,"member_count":1,"formation_positions":[Vector3.ZERO],"provenance":{}}


func _graph(permanent:bool)->Dictionary:
	var graph := {"kind":"dominate-enemy","specialPowerTemplateId":"SpecialAbilityFixtureDominate","startAbilityRange":200.0,"affectsFilter":"ALL -HERO ENEMIES NEUTRAL","permanentlyConvert":permanent,"unpackingVariation":1,"dominateRadius":30.0,"timingMs":{"UnpackTime":200,"PreparationTime":100,"FreezeAfterTriggerDuration":200,"TriggerModelConditionDuration":100},"dominatedFxId":"FX_Dominated","triggerFxId":"FX_DominateTrigger","triggerSoundId":"VoiceDominate","triggerModelCondition":{"namespace":"ModelConditionState","value":"SPECIAL_POWER_1"},"sourceIni":"fixture.ini","line":1}
	if not permanent: graph["temporaryDefectDurationMs"] = 30000; graph["temporaryDefectSourceIni"] = "default/object.ini"; graph["temporaryDefectLine"] = 206
	return graph


func _ability(graph:Dictionary,targeting:String="point")->Dictionary:
	return {"ability_id":"Command_DominateFixture","special_power_id":"SpecialAbilityFixtureDominate","targeting":targeting,"cooldown_ticks":20,"required_level":1,"level_gate_resolved":true,"castable":true,"effect":graph,"special_power_contract":{}}


func _adapter_doc(graph:Dictionary)->Dictionary:
	return {"registration":{"abilities":[{"id":"Command_DominateFixture","slot":1,"targeting":"point","specialPowerId":"SpecialAbilityFixtureDominate","cooldownMs":2000,"button":{},"effect":graph,"implementation":{"status":"implemented","reason":"","limitations":[]},"levelGate":{}}]}}


func _event(sim:RetailSliceSim,kind:String)->Dictionary:
	for value in sim.events:
		if String((value as Dictionary).get("kind",""))==kind:return value as Dictionary
	return {}


func _has_event(sim:RetailSliceSim,kind:String)->bool:return not _event(sim,kind).is_empty()


func _check(label:String,condition:bool)->void:
	if condition:passed+=1
	else:failed+=1;push_error("DOMINATE_ENEMY_RUNTIME_FAIL "+label)


func finish()->void:
	var ran:=passed+failed;if ran!=EXPECTED:failed+=1;push_error("DOMINATE_ENEMY_RUNTIME_FAIL liveness ran=%d expected=%d"%[ran,EXPECTED])
	print("DOMINATE_ENEMY_RUNTIME_RESULT passed=%d failed=%d"%[passed,failed]);quit(0 if failed==0 else 1)
