extends SceneTree
const Sim=preload("res://src/retail_slice/retail_slice_sim.gd")
const EXPECTED:=11
var passed:=0;var failed:=0
func _initialize()->void:call_deferred("_run")
func _run()->void:
	var sim:=_sim();sim._unit_experience_rules[Sim.SOLDIER_HORDE_ID]=_experience_rule();_spawn(sim,1);var row:=sim.entities[1] as Dictionary
	_check("initial_rank",int(row.get("level",0))==1);_check("presentation_receipts",(row.get("experience_presentation_receipts",[]) as Array).has("presentation_binding:SelectionDecal") and (row.get("experience_presentation_receipts",[]) as Array).has("presentation_binding:LevelUpPresentation"))
	(row.get("member_health",[]) as Array)[0]=0;row["health"]=200;sim._apply_playable_unit_death_policy(row,"NORMAL",[0]);_check("one_member_awards_exact_xp",int(row.get("experience_xp",0))==3);_check("event_records_member_and_amount",_has_event(sim,"experience.own_guys_die",1,3))
	(row.get("member_health",[]) as Array)[1]=0;row["health"]=100;sim._apply_playable_unit_death_policy(row,"NORMAL",[1]);_check("second_member_accumulates",int(row.get("experience_xp",0))==6)
	var snap:=sim.snapshot();var hash:=sim.state_hash();var restored:=_sim();_check("snapshot_restores",restored.restore(snap));_check("hash_round_trips",restored.state_hash()==hash);_check("xp_round_trips",int((restored.entities[1] as Dictionary).get("experience_xp",0))==6)
	var dead:=_sim();dead._unit_experience_rules[Sim.SOLDIER_HORDE_ID]=_experience_rule();_spawn(dead,2);var dead_row:=dead.entities[2] as Dictionary;dead_row["member_health"]=[0,0,0];dead_row["health"]=0;dead._apply_playable_unit_death_policy(dead_row,"NORMAL",[0,1,2]);_check("full_horde_death_awards_nothing",int(dead_row.get("experience_xp",0))==0)
	var absent:=_sim();absent._unit_experience_rules[Sim.SOLDIER_HORDE_ID]={"initial_rank":1,"max_level":1,"levels":[{"rank":1,"required_experience":0,"experience_award":1,"experience_award_known":true}]};_spawn(absent,3);var absent_row:=absent.entities[3] as Dictionary;(absent_row.get("member_health",[]) as Array)[0]=0;absent_row["health"]=200;absent._apply_playable_unit_death_policy(absent_row,"NORMAL",[0]);_check("absent_field_fails_closed",int(absent_row.get("experience_xp",0))==0)
	var ranked:=_sim();ranked._unit_experience_rules[Sim.SOLDIER_HORDE_ID]=_experience_rule();_spawn(ranked,4);var rank_row:=ranked.entities[4] as Dictionary;rank_row["level"]=2;(rank_row.get("member_health",[]) as Array)[0]=0;rank_row["health"]=200;ranked._apply_playable_unit_death_policy(rank_row,"NORMAL",[0]);_check("current_level_selects_award",int(rank_row.get("experience_xp",0))==4)
	print("EXPERIENCE_OWN_GUYS_DIE_RUNTIME_RESULT passed=%d failed=%d"%[passed,failed]);quit(0 if failed==0 and passed==EXPECTED else 1)
func _experience_rule()->Dictionary:return {"initial_rank":1,"max_level":2,"levels":[{"rank":1,"required_experience":0,"experience_award":1,"experience_award_known":true,"experience_award_own_guys_die":3,"selection_decal":{"style":"SHADOW_ALPHA_DECAL"},"level_up_presentation":{"emotionType":"CHEER"}},{"rank":2,"required_experience":100,"experience_award":2,"experience_award_known":true,"experience_award_own_guys_die":4}]}
func _sim()->RetailSliceSim:
	var rules:={};for oid in [Sim.SOLDIER_OBJECT_ID,Sim.SOLDIER_HORDE_ID,Sim.ARCHER_OBJECT_ID,Sim.TOWER_GUARD_OBJECT_ID,Sim.KNIGHT_OBJECT_ID]:rules[oid]=_rule()
	var s:RetailSliceSim=Sim.new();s.setup({}, {"unit_rules":rules});s.ai_enabled=false;s.base_loop_enabled=false;s.entities.clear();s.structures.clear();return s
func _spawn(s:RetailSliceSim,id:int)->void:s._add_battalion(id,0,Vector2.ZERO,"Horde",Sim.SOLDIER_OBJECT_ID,Sim.SOLDIER_HORDE_ID,0,_rule());s._attach_experience_state(s.entities[id] as Dictionary)
func _rule()->Dictionary:return {"horde_id":Sim.SOLDIER_HORDE_ID,"category":"infantry","speed":1.0,"speed_source":10.0,"acceleration":1.0,"acceleration_source":10.0,"turn_rate_degrees_per_second":180.0,"braking":1.0,"braking_source":10.0,"attack_range":1.0,"attack_range_source":1.0,"minimum_attack_range":0.0,"minimum_attack_range_source":0.0,"vision_range":10.0,"vision_range_source":10.0,"delay_between_shots_ms":1000.0,"pre_attack_delay_ms":0.0,"firing_duration_ms":0.0,"attack_period_ticks":10,"pre_attack_ticks":0,"firing_duration_ticks":0,"member_damage":1,"member_health":100,"member_count":3,"formation_positions":[Vector3.ZERO,Vector3.ONE,Vector3(2,0,0)],"provenance":{}}
func _has_event(s:RetailSliceSim,k:String,m:int,a:int)->bool:
	for e in s.events:
		if String(e.get("kind",""))==k and int(e.get("members",0))==m and int(e.get("amount",0))==a:return true
	return false
func _check(l:String,c:bool)->void:if c:passed+=1
else:failed+=1;push_error("EXPERIENCE_OWN_GUYS_DIE_RUNTIME_FAIL "+l)
