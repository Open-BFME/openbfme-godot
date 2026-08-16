extends SceneTree
const Sim=preload("res://src/retail_slice/retail_slice_sim.gd")
const EXPECTED:=16
var passed:=0;var failed:=0
func _initialize()->void:call_deferred("_run")
func _run()->void:
	var sim:=_sim();sim._unit_module_contracts[Sim.SOLDIER_HORDE_ID]=[_fire_contract(),_delete_contract()];_spawn(sim,1,0,Vector2.ZERO);_spawn(sim,2,1,Vector2(1,0));var source:=sim.entities[1] as Dictionary;var target:=sim.entities[2] as Dictionary
	_check("typed_fire_attaches",source.has("fire_weapon_updates"));_check("typed_deletion_attaches",source.has("deletion_update"));_check("deterministic_lifetime_in_bounds",int((source["deletion_update"] as Dictionary).get("selected_ticks",0))>=3 and int((source["deletion_update"] as Dictionary).get("selected_ticks",0))<=5)
	sim.tick_index=1;sim._step_fire_weapon_updates();_check("charging_gate_blocks",int(target.get("health",0))==100);source["charging_mode"]=true;sim._step_fire_weapon_updates();_check("first_nugget_fires",int(target.get("health",0))==90);_check("one_shot_marks_fired",bool((((source["fire_weapon_updates"] as Dictionary).get("nuggets",[]) as Array)[0] as Dictionary).get("fired",false)))
	sim.tick_index=2;sim._step_fire_weapon_updates();_check("one_shot_does_not_repeat",int(target.get("health",0))==90);var snap:=sim.snapshot();var hash:=sim.state_hash();var restored:=_sim();_check("snapshot_restores",restored.restore(snap));_check("hash_round_trips",restored.state_hash()==hash);var restored_policy:Dictionary=(restored.entities[1] as Dictionary)["fire_weapon_updates"];var restored_nugget:Dictionary=(restored_policy.get("nuggets",[]) as Array)[0];_check("fire_state_round_trips",bool(restored_nugget.get("fired",false)))
	var expire:=int((source["deletion_update"] as Dictionary).get("expire_tick",99));sim.tick_index=expire-1;sim._step_deletion_updates();_check("deletion_waits",int(source.get("health",0))>0);sim.tick_index=expire;sim._step_deletion_updates();_check("deletion_expires_faded",int(source.get("health",1))==0)
	var indefinite:=_sim();indefinite._unit_module_contracts[Sim.SOLDIER_HORDE_ID]=[_delete_contract(true)];_spawn(indefinite,3,0,Vector2.ZERO);indefinite.tick_index=999;indefinite._step_deletion_updates();_check("indefinite_never_deletes",int((indefinite.entities[3] as Dictionary).get("health",0))>0)
	var unresolved:=_sim();unresolved._unit_module_contracts[Sim.SOLDIER_HORDE_ID]=[_delete_define_contract()];_spawn(unresolved,4,0,Vector2.ZERO);var receipt:=((unresolved.entities[4] as Dictionary).get("deletion_update",{}) as Dictionary).get("unsupported_semantics",[]) as Array;_check("unresolved_define_receipted",receipt.has("unresolved_lifetime_expression"));unresolved.tick_index=999;unresolved._step_deletion_updates();_check("unresolved_define_fails_closed",int((unresolved.entities[4] as Dictionary).get("health",0))>0)
	var opaque:=_sim();var opaque_contract:=_fire_contract();opaque_contract["extraction"]="opaque";opaque._unit_module_contracts[Sim.SOLDIER_HORDE_ID]=[opaque_contract];_spawn(opaque,5,0,Vector2.ZERO);_check("opaque_fails_closed",not (opaque.entities[5] as Dictionary).has("fire_weapon_updates"))
	print("FIRE_WEAPON_DELETION_RUNTIME_RESULT passed=%d failed=%d"%[passed,failed]);quit(0 if failed==0 and passed==EXPECTED else 1)
func _fire_contract()->Dictionary:return {"module":"FireWeaponUpdate","extraction":"typed","fields":{"ChargingModeTrigger":{"value":true},"AliveOnly":{"value":true},"FireWeaponNugget":[{"WeaponName":{"value":"Pulse"},"OneShot":{"value":true},"FireDelay":{"milliseconds":100},"Offset":{"value":{"x":0.0,"y":0.0,"z":2.0}}}]}}
func _delete_contract(indefinite:bool=false)->Dictionary:
	var bound={"indefinite":true} if indefinite else {"indefinite":false,"milliseconds":300}
	var high={"indefinite":true} if indefinite else {"indefinite":false,"milliseconds":500}
	return {"module":"DeletionUpdate","extraction":"typed","fields":{"MinLifetime":bound,"MaxLifetime":high}}
func _delete_define_contract()->Dictionary:return {"module":"DeletionUpdate","extraction":"typed","fields":{"MinLifetime":{"indefinite":false,"expression":"UNKNOWN","name":"UNKNOWN"},"MaxLifetime":{"indefinite":false,"expression":"UNKNOWN","name":"UNKNOWN"}}}
func _sim()->RetailSliceSim:
	var rules:={};for oid in [Sim.SOLDIER_OBJECT_ID,Sim.SOLDIER_HORDE_ID,Sim.ARCHER_OBJECT_ID,Sim.TOWER_GUARD_OBJECT_ID,Sim.KNIGHT_OBJECT_ID]:rules[oid]=_rule()
	var s:RetailSliceSim=Sim.new();s.setup({}, {"unit_rules":rules,"source_map_transform_scale":1.0,"death_weapon_rules":{"Pulse":{"damage":10,"radius_source":5,"damage_type":"MAGIC","affects":"ENEMIES"}}});s.ai_enabled=false;s.base_loop_enabled=false;s.entities.clear();s.structures.clear();return s
func _spawn(s:RetailSliceSim,id:int,team:int,p:Vector2)->void:s._add_battalion(id,team,p,"Fixture",Sim.SOLDIER_OBJECT_ID,Sim.SOLDIER_HORDE_ID,0,_rule())
func _rule()->Dictionary:return {"horde_id":Sim.SOLDIER_HORDE_ID,"category":"infantry","speed":1.0,"speed_source":10.0,"acceleration":1.0,"acceleration_source":10.0,"turn_rate_degrees_per_second":180.0,"braking":1.0,"braking_source":10.0,"attack_range":1.0,"attack_range_source":1.0,"minimum_attack_range":0.0,"minimum_attack_range_source":0.0,"vision_range":10.0,"vision_range_source":10.0,"delay_between_shots_ms":1000.0,"pre_attack_delay_ms":0.0,"firing_duration_ms":0.0,"attack_period_ticks":10,"pre_attack_ticks":0,"firing_duration_ticks":0,"member_damage":1,"member_health":100,"member_count":1,"formation_positions":[Vector3.ZERO],"provenance":{}}
func _check(l:String,c:bool)->void:if c:passed+=1
else:failed+=1;push_error("FIRE_WEAPON_DELETION_RUNTIME_FAIL "+l)
