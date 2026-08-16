extends RefCounted
const Sim=preload("res://src/retail_slice/retail_slice_sim.gd")
func run(signature:String)->Dictionary:
	var oracle:=FileAccess.get_file_as_string(ProjectSettings.globalize_path("res://../.private/retail-work/editions/rotwk/cache/effective-assets/data/ini/experiencelevels.ini"));var field:=signature.get_slice(".",1)
	if oracle.is_empty() or not oracle.contains(field):return {"ok":false,"detail":"RotWK oracle lacks "+field}
	var sim=Sim.new();var rules:={};rules[Sim.SOLDIER_OBJECT_ID]=_rule();rules[Sim.SOLDIER_HORDE_ID]=_rule();sim.setup({}, {"unit_rules":rules});sim.ai_enabled=false;sim.base_loop_enabled=false;sim.entities.clear();sim.structures.clear();_spawn(sim,1,0);_spawn(sim,2,1)
	var levels=[{"rank":1,"required_experience":0,"experience_award":20,"experience_award_known":true,"health_add":0.0,"damage_add":0.0},{"rank":2,"required_experience":50,"experience_award":25,"experience_award_known":true,"health_add":25.0,"damage_add":5.0}]
	var rule={"initial_rank":1,"max_level":2,"levels":levels};sim._unit_experience_rules[Sim.SOLDIER_HORDE_ID]=rule;sim._attach_experience_state(sim.entities[1] as Dictionary);sim._attach_experience_state(sim.entities[2] as Dictionary)
	match signature:
		"field:experiencelevel.RequiredExperience":
			sim._award_experience(sim.entities[1] as Dictionary,49);var before:=int((sim.entities[1] as Dictionary).get("level",0));sim._award_experience(sim.entities[1] as Dictionary,1);return {"ok":before==1 and int((sim.entities[1] as Dictionary).get("level",0))==2,"detail":"threshold=50"}
		"field:experiencelevel.ExperienceAward":
			sim._award_member_kill_experience(1,sim.entities[2] as Dictionary);return {"ok":int((sim.entities[1] as Dictionary).get("experience_xp",0))==20,"detail":"award=20"}
		"field:experiencelevel.AttributeModifiers":
			var row:=sim.entities[1] as Dictionary;var health_before:=int(row.get("member_maximum_health",0));var damage_before:=int(row.get("member_damage",0));sim._award_experience(row,50);return {"ok":int(row.get("member_maximum_health",0))==health_before+25 and int(row.get("member_damage",0))==damage_before+5,"detail":"health+25 damage+5"}
	return {"ok":false,"detail":"unknown signature"}
func _spawn(sim,id:int,team:int)->void:sim._add_battalion(id,team,Vector2(id*2,0),"Fixture",Sim.SOLDIER_OBJECT_ID,Sim.SOLDIER_HORDE_ID,0,_rule())
func _rule()->Dictionary:return {"horde_id":Sim.SOLDIER_HORDE_ID,"category":"hero","speed":1.0,"speed_source":10.0,"acceleration":1.0,"acceleration_source":10.0,"turn_rate_degrees_per_second":180.0,"braking":1.0,"braking_source":10.0,"attack_range":1.0,"attack_range_source":1.0,"minimum_attack_range":0.0,"minimum_attack_range_source":0.0,"vision_range":10.0,"vision_range_source":10.0,"delay_between_shots_ms":1000.0,"pre_attack_delay_ms":0.0,"firing_duration_ms":0.0,"attack_period_ticks":10,"pre_attack_ticks":0,"firing_duration_ticks":0,"member_damage":10,"member_health":100,"member_count":1,"formation_positions":[Vector3.ZERO],"provenance":{}}
