extends SceneTree
const Sim=preload("res://src/retail_slice/retail_slice_sim.gd")
const EXPECTED:=18
var passed:=0;var failed:=0
func _initialize()->void:call_deferred("_run")
func _run()->void:
	var admitted:=_sim([_contract()]);var attacker:=admitted.entities[1] as Dictionary;var victim:=admitted.entities[2] as Dictionary
	_check("typed_marker_attaches",victim.has("squish_collide"));var policy:=victim.get("squish_collide",{}) as Dictionary;_check("marker_records_authored_admission",String(policy.get("admission",""))=="authored-victim-collision");_check("marker_does_not_invent_crush_inputs",not policy.has("damage") and not policy.has("crushable_level"));var before:=int(victim.get("health",0));admitted._try_cavalry_trample(attacker);_check("authored_collision_applies_crush_weapon",int(victim.get("health",0))==before-25);_check("crush_event_emitted",_events(admitted,"combat.crush")==1);_check("no_refusal_for_marker",_events(admitted,"combat.crush_refused")==0)
	var refused:=_sim([{"module":"HordeAIUpdate","extraction":"typed","fields":{}}]);var refused_before:=int((refused.entities[2] as Dictionary).get("health",0));refused._try_cavalry_trample(refused.entities[1]);_check("descriptor_without_marker_refuses",int((refused.entities[2] as Dictionary).get("health",0))==refused_before);_check("missing_marker_reason",_events(refused,"combat.crush_refused")==1);_check("refusal_does_not_start_cooldown",int((refused.entities[1] as Dictionary).get("trample_cooldown",-1))==0)
	var higher:=_sim([_contract()]);(higher.entities[2] as Dictionary)["crushable_level"]=2;var high_before:=int((higher.entities[2] as Dictionary).get("health",0));higher._try_cavalry_trample(higher.entities[1]);_check("crusher_level_must_exceed_victim",int((higher.entities[2] as Dictionary).get("health",0))==high_before);_check("level_refusal_no_damage_event",_events(higher,"combat.crush")==0)
	var slow:=_sim([_contract()]);(slow.entities[1] as Dictionary)["current_speed"]=3.9;var slow_before:=int((slow.entities[2] as Dictionary).get("health",0));slow._try_cavalry_trample(slow.entities[1]);_check("minimum_velocity_still_required",int((slow.entities[2] as Dictionary).get("health",0))==slow_before)
	var snap:=admitted.snapshot();var hash:=admitted.state_hash();var restored:=_sim([]);_check("snapshot_restores",restored.restore(snap));_check("marker_hash_round_trips",restored.state_hash()==hash);_check("restored_marker_present",(restored.entities[2] as Dictionary).has("squish_collide"))
	var opaque:=_sim([]);var c:=_contract();c["extraction"]="opaque";opaque._unit_module_contracts[Sim.SOLDIER_HORDE_ID]=[c];opaque.entities.clear();_spawn(opaque,1,Sim.PLAYER_TEAM,true);_spawn(opaque,2,Sim.ENEMY_TEAM,false);_check("opaque_marker_fails_closed",not (opaque.entities[2] as Dictionary).has("squish_collide"));var malformed:=_sim([]);var bad:=_contract();bad["fields"]={"invented":true};malformed._unit_module_contracts[Sim.SOLDIER_HORDE_ID]=[bad];malformed.entities.clear();_spawn(malformed,1,Sim.PLAYER_TEAM,true);_spawn(malformed,2,Sim.ENEMY_TEAM,false);_check("nonempty_marker_fails_closed",not (malformed.entities[2] as Dictionary).has("squish_collide"))
	var legacy:=_sim([]);legacy._unit_module_contracts.clear();legacy.entities.clear();_spawn(legacy,1,Sim.PLAYER_TEAM,true);_spawn(legacy,2,Sim.ENEMY_TEAM,false);var legacy_before:=int((legacy.entities[2] as Dictionary).get("health",0));legacy._try_cavalry_trample(legacy.entities[1]);_check("legacy_descriptorless_fixture_remains_compatible",int((legacy.entities[2] as Dictionary).get("health",0))<legacy_before)
	if passed+failed!=EXPECTED:failed+=1;printerr("SQUISH_COLLIDE_RUNTIME_FAIL liveness")
	print("SQUISH_COLLIDE_RUNTIME_RESULT passed=%d failed=%d"%[passed,failed]);quit(0 if failed==0 else 1)
func _contract()->Dictionary:return {"module":"SquishCollide","runtimeStatus":"deferred","extraction":"typed","tag":"ModuleTag_Squish","line":10,"fields":{}}
func _sim(victim_contracts:Array)->RetailSliceSim:
	var rules:={};for id in [Sim.SOLDIER_OBJECT_ID,Sim.SOLDIER_HORDE_ID,Sim.ARCHER_OBJECT_ID,Sim.TOWER_GUARD_OBJECT_ID,Sim.KNIGHT_OBJECT_ID]:rules[id]=_rule(false)
	var s:RetailSliceSim=Sim.new();s.setup({}, {"unit_rules":rules});s.ai_enabled=false;s.base_loop_enabled=false;s.entities.clear();s.structures.clear();s._unit_module_contracts[Sim.SOLDIER_HORDE_ID]=victim_contracts;_spawn(s,1,Sim.PLAYER_TEAM,true);_spawn(s,2,Sim.ENEMY_TEAM,false);return s
func _spawn(s:RetailSliceSim,id:int,team:int,crusher:bool)->void:
	s._add_battalion(id,team,Vector2(0.0 if crusher else 0.5,0),"Fixture",Sim.SOLDIER_OBJECT_ID,Sim.SOLDIER_HORDE_ID,0,_rule(crusher));var r:=s.entities[id] as Dictionary;r["kind_of"]=["CAVALRY"] if crusher else ["INFANTRY"];r["current_speed"]=10.0 if crusher else 0.0
func _rule(crusher:bool)->Dictionary:
	var r={"horde_id":Sim.SOLDIER_HORDE_ID,"category":"cavalry" if crusher else "infantry","speed":10.0,"speed_source":100.0,"acceleration":1.0,"acceleration_source":10.0,"turn_rate_degrees_per_second":180.0,"braking":1.0,"braking_source":10.0,"attack_range":0.1,"attack_range_source":1.0,"minimum_attack_range":0.0,"minimum_attack_range_source":0.0,"vision_range":1.0,"vision_range_source":10.0,"delay_between_shots_ms":1000.0,"pre_attack_delay_ms":0.0,"firing_duration_ms":0.0,"attack_period_ticks":10,"pre_attack_ticks":0,"firing_duration_ticks":0,"member_damage":1,"member_health":100,"member_count":1,"formation_positions":[Vector3.ZERO],"provenance":{},"crushable_level":0}
	if crusher:
		r.merge({"crusher_level":2,"crush_damage":25,"min_crush_velocity_percent":40.0},true)
	return r
func _events(s:RetailSliceSim,k:String)->int:
	var n:=0;for v in s.events:
		if String((v as Dictionary).get("kind",""))==k:n+=1
	return n
func _check(n:String,c:bool)->void:
	if c:passed+=1
	else:failed+=1;push_error("SQUISH_COLLIDE_RUNTIME_FAIL "+n)
