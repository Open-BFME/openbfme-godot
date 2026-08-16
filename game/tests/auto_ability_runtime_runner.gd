extends SceneTree
const Sim=preload("res://src/retail_slice/retail_slice_sim.gd")
const EXPECTED:=14
var passed:=0;var failed:=0
func _initialize()->void: call_deferred("_run")
func _run()->void:
	var sim:=_sim();sim._unit_module_contracts[Sim.SOLDIER_HORDE_ID]=[_contract()];sim._unit_ability_rules[Sim.SOLDIER_HORDE_ID]=sim._scaled_ability_rules([_ability()],0.1)
	_spawn(sim,1,0,Vector2.ZERO);_spawn(sim,2,0,Vector2(5,0));var row:=sim.entities[1] as Dictionary;var ally:=sim.entities[2] as Dictionary;sim._attach_module_contracts(ally);sim.set_auto_ability_active(2,"SpecialAutoHeal",false);ally["member_health"]=[50];ally["health"]=50
	var behavior:=((row.get("auto_ability_behaviors",[]) as Array)[0] as Dictionary)
	_check("typed_attaches",not behavior.is_empty());_check("define_subtract_range",is_equal_approx(float(behavior.get("maximum_range_source",0)),90.0));_check("starts_active",bool(behavior.get("active",false)));_check("idle_ticks",int(behavior.get("idle_ticks",0))==2)
	sim.tick_index=1;sim._step_auto_abilities();_check("idle_gate_blocks",int(ally.get("health",0))==50)
	sim.tick_index=2;sim._step_auto_abilities();_check("query_triggers_cast",int(ally.get("health",0))>50);_check("cooldown_armed",int(((row.get("ability_states",{}) as Dictionary)["AutoHeal"] as Dictionary).get("cooldown_ready_tick",0))==7)
	ally["health"]=50;ally["member_health"]=[50];sim.tick_index=3;sim._step_auto_abilities();_check("cooldown_prevents_recast",int(ally.get("health",0))==50)
	row["object_status"]={"STUNNED":true};ally["health"]=50;ally["member_health"]=[50];sim.tick_index=7;sim._step_auto_abilities();_check("forbidden_status_blocks",int(ally.get("health",0))==50)
	row["object_status"]={};ally["health"]=50;ally["member_health"]=[50];sim.set_auto_ability_active(1,"SpecialAutoHeal",false);sim.tick_index=8;sim._step_auto_abilities();_check("command_toggle_disables",int(ally.get("health",0))==50)
	_check("command_toggle_enables",bool(sim.set_auto_ability_active(1,"SpecialAutoHeal",true).get("ok",false)));sim.tick_index=9;sim._step_auto_abilities();_check("enabled_recasts",int(ally.get("health",0))>50)
	var snap:=sim.snapshot();var hash:=sim.state_hash();var restored:=_sim();_check("snapshot_restore",restored.restore(snap));_check("hash_restore",restored.state_hash()==hash)
	finish()
func _contract()->Dictionary:return {"module":"AutoAbilityBehavior","extraction":"typed","tag":"Auto","line":1,"fields":{"SpecialAbility":{"value":"SpecialAutoHeal"},"StartsActive":{"value":true},"MaxScanRange":{"kind":"subtract","name":"AUTO_RANGE","amount":10},"MinScanRange":{"kind":"literal","value":0},"IdleTimeSeconds":{"milliseconds":200,"seconds":0.2},"ForbiddenStatus":{"value":["STUNNED"]},"AllowSelf":{"value":false},"Query":[{"minimumMatches":1,"filterTokens":["ALLIES","+HERO"]}]}}
func _ability()->Dictionary:return {"ability_id":"AutoHeal","special_power_id":"SpecialAutoHeal","targeting":"point","cooldown_ticks":5,"required_level":1,"level_gate_resolved":true,"castable":true,"effect":{"kind":"heal","radius":100.0,"amount":10.0,"amountKind":"flat"},"special_power_contract":{}}
func _sim()->RetailSliceSim:
	var rules:={};for oid in [Sim.SOLDIER_OBJECT_ID,Sim.SOLDIER_HORDE_ID,Sim.ARCHER_OBJECT_ID,Sim.TOWER_GUARD_OBJECT_ID,Sim.KNIGHT_OBJECT_ID]:rules[oid]=_rule()
	var s:RetailSliceSim=Sim.new();s.setup({}, {"unit_rules":rules,"source_map_transform_scale":0.1,"auto_ability_range_defines":{"AUTO_RANGE":100}});s.ai_enabled=false;s.base_loop_enabled=false;s.entities.clear();s.structures.clear();return s
func _spawn(s:RetailSliceSim,id:int,team:int,p:Vector2)->void:s._add_battalion(id,team,p,"Hero",Sim.SOLDIER_OBJECT_ID,Sim.SOLDIER_HORDE_ID,0,_rule());s._attach_hero_ability_state(s.entities[id] as Dictionary)
func _rule()->Dictionary:return {"horde_id":Sim.SOLDIER_HORDE_ID,"category":"hero","speed":1.0,"speed_source":10.0,"acceleration":1.0,"acceleration_source":10.0,"turn_rate_degrees_per_second":180.0,"braking":1.0,"braking_source":10.0,"attack_range":1.0,"attack_range_source":1.0,"minimum_attack_range":0.0,"minimum_attack_range_source":0.0,"vision_range":10.0,"vision_range_source":10.0,"delay_between_shots_ms":1000.0,"pre_attack_delay_ms":0.0,"firing_duration_ms":0.0,"attack_period_ticks":10,"pre_attack_ticks":0,"firing_duration_ticks":0,"member_damage":1,"member_health":100,"member_count":1,"formation_positions":[Vector3.ZERO],"provenance":{}}
func _check(l:String,c:bool)->void:if c:passed+=1
else:failed+=1;push_error("AUTO_ABILITY_RUNTIME_FAIL "+l)
func finish()->void:print("AUTO_ABILITY_RUNTIME_RESULT passed=%d failed=%d"%[passed,failed]);quit(0 if failed==0 and passed==EXPECTED else 1)
