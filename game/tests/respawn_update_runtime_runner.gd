extends SceneTree
const Sim=preload("res://src/retail_slice/retail_slice_sim.gd")
const EXPECTED:=14
var passed:=0;var failed:=0
func _initialize()->void:call_deferred("_run")
func _run()->void:
	var sim:=_sim();sim._unit_module_contracts[Sim.SOLDIER_HORDE_ID]=[_contract(true)];_spawn(sim,1);_anchor(sim,90);var row:=sim.entities[1] as Dictionary;row["level"]=2
	var policy:=row.get("respawn_update",{}) as Dictionary;_check("typed_attaches",not policy.is_empty());_check("presentation_receipts",(policy.get("unsupported_semantics",[]) as Array).has("presentation_binding:ButtonImage"));sim._expire_lifetime_entity(1,row,"NORMAL")
	_check("death_schedules",sim.respawn_schedules.has(1));var schedule:=sim.respawn_schedules[1] as Dictionary;_check("level_entry_overrides",int(schedule.get("cost",0))==20 and int(schedule.get("ready_tick",0))==3)
	sim.tick_index=2;sim._step_respawn_updates();_check("timer_blocks_early",int(row.get("health",0))==0);var snap:=sim.snapshot();var hash:=sim.state_hash();var restored:=_sim();_check("snapshot_restore",restored.restore(snap));_check("hash_restore",restored.state_hash()==hash)
	sim.tick_index=3;sim._step_respawn_updates();var revived:=sim.entities[1] as Dictionary;_check("respawns_at_anchor",int(revived.get("health",0))==50 and Vector2(revived.get("position",Vector2.ONE))==Vector2(7,0));_check("cost_deducted",sim.resources_for_team(0)==80);_check("template_consumed",String(revived.get("unit_type",""))=="AlternateHero")
	var manual:=_sim();manual._unit_module_contracts[Sim.SOLDIER_HORDE_ID]=[_contract(false)];_spawn(manual,2);_anchor(manual,91);var m:=manual.entities[2] as Dictionary;manual._expire_lifetime_entity(2,m,"NORMAL");manual.tick_index=3;manual._step_respawn_updates();_check("manual_waits_for_request",int(m.get("health",0))==0);manual.team_resources[0]=0;_check("manual_cost_gate",String(manual.request_respawn(2).get("reason",""))=="insufficient-resources");manual.team_resources[0]=100;_check("manual_request",bool(manual.request_respawn(2).get("ok",false)));manual.tick_index=5;manual._step_respawn_updates();_check("manual_completes",int((manual.entities[2] as Dictionary).get("health",0))>0)
	print("RESPAWN_UPDATE_RUNTIME_RESULT passed=%d failed=%d"%[passed,failed]);quit(0 if failed==0 and passed==EXPECTED else 1)
func _contract(auto:bool)->Dictionary:return {"module":"RespawnUpdate","extraction":"typed","tag":"Respawn","line":2,"fields":{"DeathAnim":{"value":"DYING"},"ButtonImage":{"value":"HeroButton"},"AutoRespawnAtObjectFilter":{"value":["NONE","+CASTLE_KEEP"]},"RespawnAsTemplate":{"value":"AlternateHero"},"RespawnRules":{"autoSpawn":auto,"cost":10,"timeMilliseconds":500,"healthPercent":50.0},"RespawnEntry":[{"level":2,"cost":20,"timeMilliseconds":300}]}}
func _sim()->RetailSliceSim:
	var rules:={};for oid in [Sim.SOLDIER_OBJECT_ID,Sim.SOLDIER_HORDE_ID,Sim.ARCHER_OBJECT_ID,Sim.TOWER_GUARD_OBJECT_ID,Sim.KNIGHT_OBJECT_ID]:rules[oid]=_rule()
	var s:RetailSliceSim=Sim.new();s.setup({}, {"unit_rules":rules,"source_map_transform_scale":1.0});s.ai_enabled=false;s.base_loop_enabled=false;s.entities.clear();s.structures.clear();s.team_resources[0]=100;return s
func _spawn(s:RetailSliceSim,id:int)->void:s._add_battalion(id,0,Vector2.ZERO,"Hero",Sim.SOLDIER_OBJECT_ID,Sim.SOLDIER_HORDE_ID,0,_rule())
func _anchor(s:RetailSliceSim,id:int)->void:s.structures[id]={"id":id,"team":0,"health":100,"position":Vector2(7,0),"structure_kind":"keep","kind_of":["CASTLE_KEEP"]}
func _rule()->Dictionary:return {"horde_id":Sim.SOLDIER_HORDE_ID,"category":"hero","speed":1.0,"speed_source":10.0,"acceleration":1.0,"acceleration_source":10.0,"turn_rate_degrees_per_second":180.0,"braking":1.0,"braking_source":10.0,"attack_range":1.0,"attack_range_source":1.0,"minimum_attack_range":0.0,"minimum_attack_range_source":0.0,"vision_range":10.0,"vision_range_source":10.0,"delay_between_shots_ms":1000.0,"pre_attack_delay_ms":0.0,"firing_duration_ms":0.0,"attack_period_ticks":10,"pre_attack_ticks":0,"firing_duration_ticks":0,"member_damage":1,"member_health":100,"member_count":1,"formation_positions":[Vector3.ZERO],"provenance":{}}
func _check(l:String,c:bool)->void:if c:passed+=1
else:failed+=1;push_error("RESPAWN_UPDATE_RUNTIME_FAIL "+l)
