extends SceneTree
const Sim=preload("res://src/retail_slice/retail_slice_sim.gd")
const EXPECTED:=17
var passed:=0;var failed:=0
func _initialize()->void:call_deferred("_run")
func _run()->void:
	var sim:=_sim();sim.register_structure_module_contracts("FixtureWall",[_contract(true,true)]);_add(sim,700,"FixtureWall");sim._attach_structure_module_contracts(sim.structures[700]);var row:=sim.structures[700] as Dictionary;var p:=row.get("castle_member_behavior",{}) as Dictionary
	_check("typed_attaches",not p.is_empty());_check("membership_marker",bool(p.get("is_castle_member",false)));_check("breach_flag",bool(p.get("counts_for_eva_castle_breached",false)));_check("store_price_preserved",bool(p.get("store_upgrade_price",false)));_check("store_price_fails_closed_receipt",(p.get("unsupported_semantics",[]) as Array).has("StoreUpgradePrice:refund-route-and-percentage-unresolved"));_check("construction_sound_receipt",(p.get("unsupported_semantics",[]) as Array).has("BeingBuiltSound:presentation-audio-route"));_check("eva_ids_preserved",String((p.get("presentation",{}) as Dictionary).get("CampDestroyedOwnerEvaEvent",""))=="EconPlotDestroyed")
	var snap:=sim.snapshot();var hash:=sim.state_hash();var restored:=_sim();_check("snapshot_restores",restored.restore(snap));_check("hash_round_trips",restored.state_hash()==hash)
	sim._apply_structure_damage(1,700,1000,"default");row=sim.structures[700] as Dictionary;_check("destroyed",int(row.get("health",1))==0);_check("member_event_once",_event_count(sim,"castle.member_destroyed")==1);_check("breach_event_when_enabled",_event_count(sim,"castle.breached")==1);sim._dispatch_castle_member_destroyed(700,row,1,"again");_check("death_dispatch_dedupes",_event_count(sim,"castle.member_destroyed")==1)
	var no_breach:=_sim();no_breach.register_structure_module_contracts("FixtureTower",[_contract(false,false)]);_add(no_breach,701,"FixtureTower");no_breach._apply_structure_damage(1,701,1000,"default");_check("no_breach_policy_still_records_member_death",_event_count(no_breach,"castle.member_destroyed")==1);_check("counts_no_suppresses_breach",_event_count(no_breach,"castle.breached")==0);_check("empty_typed_marker_valid",(no_breach.structures[701] as Dictionary).has("castle_member_behavior"))
	var opaque:=_sim();var c:=_contract(true,false);c["extraction"]="opaque";opaque.register_structure_module_contracts("FixtureOpaque",[c]);_add(opaque,702,"FixtureOpaque");opaque._attach_structure_module_contracts(opaque.structures[702]);_check("opaque_fails_closed",not (opaque.structures[702] as Dictionary).has("castle_member_behavior"))
	if passed+failed!=EXPECTED:failed+=1;printerr("CASTLE_MEMBER_BEHAVIOR_RUNTIME_FAIL liveness")
	print("CASTLE_MEMBER_BEHAVIOR_RUNTIME_RESULT passed=%d failed=%d"%[passed,failed]);quit(0 if failed==0 else 1)
func _contract(counts:bool,store:bool)->Dictionary:return {"module":"CastleMemberBehavior","runtimeStatus":"deferred","extraction":"typed","tag":"ModuleTag_Castle","line":10,"fields":{"CountsForEvaCastleBreached":{"value":counts},"StoreUpgradePrice":{"value":store},"BeingBuiltSound":{"value":"BuildingConstructionLoop"},"CampDestroyedOwnerEvaEvent":{"value":"EconPlotDestroyed"},"CampDestroyedAllyEvaEvent":{"value":"AllyEconPlotDestroyed"},"CampDestroyedAttackerEvaEvent":{"value":"EnemyEconPlotDestroyed"}}}
func _sim()->RetailSliceSim:
	var rules:={};for id in [Sim.SOLDIER_OBJECT_ID,Sim.SOLDIER_HORDE_ID,Sim.ARCHER_OBJECT_ID,Sim.TOWER_GUARD_OBJECT_ID,Sim.KNIGHT_OBJECT_ID]:rules[id]=_rule()
	var s:RetailSliceSim=Sim.new();s.setup({}, {"unit_rules":rules,"structure_armor":{"fixture":{"damage_scalar":1.0,"scalars":{"default":1.0}}}});s.ai_enabled=false;s.base_loop_enabled=false;s.entities.clear();s.structures.clear();s._add_battalion(1,Sim.ENEMY_TEAM,Vector2.ZERO,"Attacker",Sim.SOLDIER_OBJECT_ID,Sim.SOLDIER_HORDE_ID,0,_rule());return s
func _add(s:RetailSliceSim,id:int,source:String)->void:s.structures[id]={"id":id,"team":Sim.PLAYER_TEAM,"source_object_id":source,"structure_kind":"fixture","health":100,"maximum_health":100,"position":Vector2.ZERO,"queue":[],"upgrade_queue":[],"damage_remainders":{}}
func _event_count(s:RetailSliceSim,kind:String)->int:
	var n:=0;for v in s.events:
		if String((v as Dictionary).get("kind",""))==kind:n+=1
	return n
func _rule()->Dictionary:return {"horde_id":Sim.SOLDIER_HORDE_ID,"category":"infantry","speed":1.0,"speed_source":10.0,"acceleration":1.0,"acceleration_source":10.0,"turn_rate_degrees_per_second":180.0,"braking":1.0,"braking_source":10.0,"attack_range":0.1,"attack_range_source":1.0,"minimum_attack_range":0.0,"minimum_attack_range_source":0.0,"vision_range":1.0,"vision_range_source":10.0,"delay_between_shots_ms":1000.0,"pre_attack_delay_ms":0.0,"firing_duration_ms":0.0,"attack_period_ticks":10,"pre_attack_ticks":0,"firing_duration_ticks":0,"member_damage":1,"member_health":100,"member_count":1,"formation_positions":[Vector3.ZERO],"provenance":{}}
func _check(n:String,c:bool)->void:
	if c:passed+=1
	else:failed+=1;push_error("CASTLE_MEMBER_BEHAVIOR_RUNTIME_FAIL "+n)
