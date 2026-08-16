extends SceneTree
const Sim=preload("res://src/retail_slice/retail_slice_sim.gd")
const EXPECTED:=19
var passed:=0;var failed:=0
func _initialize()->void:call_deferred("_run")
func _run()->void:
	var sim:=_sim();sim._unit_module_contracts[Sim.SOLDIER_HORDE_ID]=[_contract()];_spawn(sim,1,Sim.PLAYER_TEAM);_spawn(sim,90,Sim.ENEMY_TEAM);var row:=sim.entities[1] as Dictionary
	_check("typed_attaches",row.has("inactive_body"));_check("presence_sets_indestructible",bool(row.get("indestructible",false)));var health:=int(row.get("health",0));sim._apply_damage(90,1,999);_check("aggregate_damage_refused",int(row.get("health",0))==health);_check("aggregate_refusal_event",_refused(sim,"entity"));sim._apply_member_damage(90,0,1,999,"battalion",1,0);_check("direct_member_damage_refused",int((row.get("member_health",[]) as Array)[0])==health);_check("member_refusal_event",_refused(sim,"entity-member"));var hash:=sim.state_hash();var snap:=sim.snapshot();var restored:=_sim();_check("snapshot_restores",restored.restore(snap));_check("hash_round_trips",restored.state_hash()==hash);restored._apply_damage(90,1,999);_check("restored_body_remains_indestructible",int((restored.entities[1] as Dictionary).get("health",0))==health)
	var structures:=_sim();structures.register_structure_module_contracts("FixtureInactive",[_contract()]);_add_structure(structures,700,"FixtureInactive");structures._apply_structure_damage(90,700,999);var srow:=structures.structures[700] as Dictionary;_check("structure_lazily_attaches",srow.has("inactive_body"));_check("structure_damage_refused",int(srow.get("health",0))==100);_check("structure_indestructible_flag",bool(srow.get("indestructible",false)));_check("no_death_dispatch",_event_count(structures,"structure.destroyed")==0)
	var map_authored:=_sim();_add_structure(map_authored,701,"NoContract");(map_authored.structures[701] as Dictionary)["indestructible"]=true;map_authored._apply_structure_damage(90,701,999);_check("existing_map_indestructible_path_unchanged",int((map_authored.structures[701] as Dictionary).get("health",0))==100)
	var opaque:=_sim();var c:=_contract();c["extraction"]="opaque";opaque._unit_module_contracts[Sim.SOLDIER_HORDE_ID]=[c];_spawn(opaque,2,Sim.PLAYER_TEAM);_check("opaque_does_not_attach",not (opaque.entities[2] as Dictionary).has("inactive_body"));var before:=int((opaque.entities[2] as Dictionary).get("health",0));opaque._apply_damage(90,2,1);_check("opaque_does_not_grant_immunity",int((opaque.entities[2] as Dictionary).get("health",0))<before)
	var malformed:=_sim();var bad:=_contract();bad["fields"]={"indestructible":false};malformed._unit_module_contracts[Sim.SOLDIER_HORDE_ID]=[bad];_spawn(malformed,3,Sim.PLAYER_TEAM);_check("false_policy_fails_closed",not (malformed.entities[3] as Dictionary).has("inactive_body"));var extra:=_contract();extra["fields"]={"indestructible":true,"invented":true};malformed._unit_module_contracts[Sim.SOLDIER_HORDE_ID]=[extra];_spawn(malformed,4,Sim.PLAYER_TEAM);_check("extra_field_fails_closed",not (malformed.entities[4] as Dictionary).has("inactive_body"));_check("no_activation_api_invented",not sim.has_method("activate_inactive_body"))
	if passed+failed!=EXPECTED:failed+=1;printerr("INACTIVE_BODY_RUNTIME_FAIL liveness")
	print("INACTIVE_BODY_RUNTIME_RESULT passed=%d failed=%d"%[passed,failed]);quit(0 if failed==0 else 1)
func _contract()->Dictionary:return {"module":"InactiveBody","runtimeStatus":"deferred","extraction":"typed","carrier":"Body","tag":"ModuleTag_Body","line":10,"fields":{"indestructible":true}}
func _sim()->RetailSliceSim:
	var rules:={};for id in [Sim.SOLDIER_OBJECT_ID,Sim.SOLDIER_HORDE_ID,Sim.ARCHER_OBJECT_ID,Sim.TOWER_GUARD_OBJECT_ID,Sim.KNIGHT_OBJECT_ID]:rules[id]=_rule()
	var s:RetailSliceSim=Sim.new();s.setup({}, {"unit_rules":rules,"structure_armor":{"fixture":{"damage_scalar":1.0,"scalars":{"default":1.0}}}});s.ai_enabled=false;s.base_loop_enabled=false;s.entities.clear();s.structures.clear();return s
func _spawn(s:RetailSliceSim,id:int,team:int)->void:s._add_battalion(id,team,Vector2.ZERO,"Fixture",Sim.SOLDIER_OBJECT_ID,Sim.SOLDIER_HORDE_ID,0,_rule())
func _add_structure(s:RetailSliceSim,id:int,source:String)->void:s.structures[id]={"id":id,"team":Sim.PLAYER_TEAM,"source_object_id":source,"structure_kind":"fixture","health":100,"maximum_health":100,"position":Vector2.ZERO,"queue":[],"upgrade_queue":[],"damage_remainders":{}}
func _event_count(s:RetailSliceSim,k:String)->int:
	var n:=0
	for v in s.events:
		if String((v as Dictionary).get("kind",""))==k:n+=1
	return n
func _refused(s:RetailSliceSim,target:String)->bool:
	for v in s.events:
		var e:=v as Dictionary
		if String(e.get("kind",""))=="combat.damage_refused" and String(e.get("reason",""))=="inactive-body" and String(e.get("target",""))==target:return true
	return false
func _rule()->Dictionary:return {"horde_id":Sim.SOLDIER_HORDE_ID,"category":"infantry","speed":1.0,"speed_source":10.0,"acceleration":1.0,"acceleration_source":10.0,"turn_rate_degrees_per_second":180.0,"braking":1.0,"braking_source":10.0,"attack_range":0.1,"attack_range_source":1.0,"minimum_attack_range":0.0,"minimum_attack_range_source":0.0,"vision_range":1.0,"vision_range_source":10.0,"delay_between_shots_ms":1000.0,"pre_attack_delay_ms":0.0,"firing_duration_ms":0.0,"attack_period_ticks":10,"pre_attack_ticks":0,"firing_duration_ticks":0,"member_damage":1,"member_health":100,"member_count":1,"formation_positions":[Vector3.ZERO],"provenance":{}}
func _check(n:String,c:bool)->void:
	if c:passed+=1
	else:failed+=1;push_error("INACTIVE_BODY_RUNTIME_FAIL "+n)
