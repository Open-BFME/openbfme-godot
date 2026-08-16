extends SceneTree
const Sim=preload("res://src/retail_slice/retail_slice_sim.gd")
const EXPECTED:=18
var passed:=0;var failed:=0
func _initialize()->void:call_deferred("_run")
func _run()->void:
	var sim:=_sim();sim._unit_module_contracts[Sim.SOLDIER_HORDE_ID]=[_contract()];_spawn(sim,1);_spawn(sim,90,Sim.ENEMY_TEAM)
	var row:=sim.entities[1] as Dictionary;var p:=row.get("emotion_tracker",{}) as Dictionary
	_check("typed_attaches",not p.is_empty());_check("filters_preserved",p.get("afraid_of",[])==["NONE","+MONSTER"] and p.get("always_afraid_of",[])==["NONE","+HERO"]);_check("define_range_resolved",is_equal_approx(float(p.get("fear_scan_distance_source",0)),200.0));_check("literal_range_resolved",is_equal_approx(float(p.get("taunt_distance_source",0)),50.0));_check("delay_quantized",int(p.get("taunt_update_ticks",0))==10);_check("immune_level_preserved",int(p.get("immune_to_fear_level",0))==2);_check("ignore_veterancy_preserved",bool(p.get("ignore_veterancy",false)));_check("presentation_social_receipts",(p.get("unsupported_semantics",[]) as Array).size()==2)
	var bad:=sim.trigger_entity_emotion(1,"Missing",5);_check("unauthored_emotion_refused",not bool(bad.get("ok",false)))
	var plain:=sim.trigger_entity_emotion(1,"Alert_Base",3);_check("plain_requires_caller_duration",bool(plain.get("ok",false)) and int(plain.get("duration_ticks",0))==3);_check("plain_state_authoritative",String(row.get("active_emotion",""))=="Alert_Base")
	var over:=sim.trigger_entity_emotion(1,"Taunt_Base",999);_check("override_uses_authored_duration",bool(over.get("ok",false)) and int(over.get("duration_ticks",0))==70 and bool(row.get("active_emotion_override",false)))
	var snap:=sim.snapshot();var hash:=sim.state_hash();var restored:=_sim();_check("snapshot_restores",restored.restore(snap));_check("hash_round_trip",restored.state_hash()==hash);sim.advance(69);restored.advance(69);_check("restored_timer_deterministic",restored.state_hash()==sim.state_hash() and String((sim.entities[1] as Dictionary).get("active_emotion",""))=="Taunt_Base");sim.tick();restored.tick();_check("expires_exactly",not (sim.entities[1] as Dictionary).has("active_emotion") and restored.state_hash()==sim.state_hash())
	var opaque:=_sim();var c:=_contract();c["extraction"]="opaque";opaque._unit_module_contracts[Sim.SOLDIER_HORDE_ID]=[c];_spawn(opaque,2);_check("opaque_fails_closed",not (opaque.entities[2] as Dictionary).has("emotion_tracker"));var unresolved:=_sim(false);unresolved._unit_module_contracts[Sim.SOLDIER_HORDE_ID]=[_contract()];_spawn(unresolved,3);_check("unresolved_define_receipted",((unresolved.entities[3]["emotion_tracker"] as Dictionary).get("unsupported_semantics",[]) as Array).has("unresolved_expression:FearScanDistance=FEAR_RANGE"))
	if passed+failed!=EXPECTED:failed+=1;printerr("EMOTION_TRACKER_RUNTIME_FAIL liveness")
	print("EMOTION_TRACKER_RUNTIME_RESULT passed=%d failed=%d"%[passed,failed]);quit(0 if failed==0 else 1)
func _contract()->Dictionary:return {"module":"EmotionTrackerUpdate","runtimeStatus":"deferred","extraction":"typed","tag":"ModuleTag_Emotion","line":10,"fields":{"AfraidOf":{"value":["NONE","+MONSTER"]},"AlwaysAfraidOf":{"value":["NONE","+HERO"]},"FearScanDistance":{"expression":"FEAR_RANGE"},"TauntAndPointDistance":{"expression":"50","value":50.0},"HeroScanDistance":{"expression":"100","value":100.0},"TauntAndPointUpdateDelay":{"milliseconds":1000},"TauntAndPointExcluded":{"value":["NONE","+HERO"]},"PointAt":{"value":["NONE","+INFANTRY"]},"QuarrelProbability":{"percent":0.1,"fraction":0.001},"ImmuneToFearLevel":{"value":2},"IgnoreVeterancy":{"value":true},"AddEmotion":[{"name":"Alert_Base","override":false,"line":20},{"name":"Taunt_Base","override":true,"line":21,"Duration":{"milliseconds":7000}}]}}
func _sim(defines:bool=true)->RetailSliceSim:
	var rules:={}
	for id in [Sim.SOLDIER_OBJECT_ID,Sim.SOLDIER_HORDE_ID,Sim.ARCHER_OBJECT_ID,Sim.TOWER_GUARD_OBJECT_ID,Sim.KNIGHT_OBJECT_ID]:rules[id]=_rule()
	var s:RetailSliceSim=Sim.new()
	var g={"unit_rules":rules}
	if defines:g["emotion_range_defines"]={"FEAR_RANGE":200.0}
	s.setup({},g);s.ai_enabled=false;s.base_loop_enabled=false;s.entities.clear();s.structures.clear()
	return s
func _spawn(s:RetailSliceSim,id:int,team:int=Sim.PLAYER_TEAM)->void:s._add_battalion(id,team,Vector2.ZERO,"Fixture",Sim.SOLDIER_OBJECT_ID,Sim.SOLDIER_HORDE_ID,0,_rule())
func _rule()->Dictionary:return {"horde_id":Sim.SOLDIER_HORDE_ID,"category":"infantry","speed":1.0,"speed_source":10.0,"acceleration":1.0,"acceleration_source":10.0,"turn_rate_degrees_per_second":180.0,"braking":1.0,"braking_source":10.0,"attack_range":0.1,"attack_range_source":1.0,"minimum_attack_range":0.0,"minimum_attack_range_source":0.0,"vision_range":1.0,"vision_range_source":10.0,"delay_between_shots_ms":1000.0,"pre_attack_delay_ms":0.0,"firing_duration_ms":0.0,"attack_period_ticks":10,"pre_attack_ticks":0,"firing_duration_ticks":0,"member_damage":1,"member_health":100,"member_count":1,"formation_positions":[Vector3.ZERO],"provenance":{}}
func _check(n:String,c:bool)->void:
	if c:passed+=1
	else:failed+=1;push_error("EMOTION_TRACKER_RUNTIME_FAIL "+n)
