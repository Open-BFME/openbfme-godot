extends RefCounted
const Sim=preload("res://src/retail_slice/retail_slice_sim.gd")
func run(signature:String)->Dictionary:
	var oracle:=FileAccess.get_file_as_string(ProjectSettings.globalize_path("res://../.private/retail-work/editions/rotwk/cache/effective-assets/data/ini/specialpower.ini"));var field:=signature.get_slice(".",1)
	if oracle.is_empty() or not oracle.contains(field):return {"ok":false,"detail":"RotWK oracle lacks "+field}
	var sim=Sim.new();var rules:={};rules[Sim.SOLDIER_OBJECT_ID]=_rule();rules[Sim.SOLDIER_HORDE_ID]=_rule();sim.setup({}, {"unit_rules":rules,"source_map_transform_scale":0.1});sim.ai_enabled=false;sim.base_loop_enabled=false;sim.entities.clear();sim.structures.clear()
	_spawn(sim,1,0,Vector2.ZERO);_spawn(sim,2,1,Vector2(5,0))
	var contract:={"objectFilter":["NONE","+HERO","-STRUCTURE"],"forbiddenObjectFilter":["NONE","+MACHINE"],"forbiddenObjectRange":60}
	var ability:={"ability_id":"Fixture","special_power_id":"SpecialFixture","targeting":"enemy-object","cooldown_ticks":10,"required_level":1,"level_gate_resolved":true,"castable":true,"effect":{"kind":"heal","radius":20.0,"amount":10.0,"amountKind":"flat"},"special_power_contract":contract}
	sim._unit_ability_rules[Sim.SOLDIER_HORDE_ID]=sim._scaled_ability_rules([ability],0.1);sim._attach_hero_ability_state(sim.entities[1] as Dictionary)
	match signature:
		"field:specialpower.ObjectFilter":
			(sim.entities[2] as Dictionary)["kind_of"]=["STRUCTURE"]
			var verdict:=sim.cast_ability(1,"Fixture",Vector2(5,0),0)
			return {"ok":String(verdict.get("reason",""))=="object-filter-refused","detail":str(verdict)}
		"field:specialpower.ForbiddenObjectFilter":
			(sim.entities[2] as Dictionary)["kind_of"]=["MACHINE"]
			var verdict:=sim.cast_ability(1,"Fixture",Vector2(5,0),0)
			return {"ok":String(verdict.get("reason",""))=="forbidden-object-nearby","detail":str(verdict)}
		"field:specialpower.ForbiddenObjectRange":
			(sim.entities[2] as Dictionary)["kind_of"]=["MACHINE"]
			var near:=sim.cast_ability(1,"Fixture",Vector2(5,0),0);(sim.entities[2] as Dictionary)["position"]=Vector2(7,0);var far:=sim.cast_ability(1,"Fixture",Vector2(5,0),0)
			return {"ok":String(near.get("reason",""))=="forbidden-object-nearby" and String(far.get("reason",""))!="forbidden-object-nearby","detail":"near=%s far=%s"%[str(near),str(far)]}
	return {"ok":false,"detail":"unknown signature"}
func _spawn(sim,id:int,team:int,point:Vector2)->void:sim._add_battalion(id,team,point,"Fixture",Sim.SOLDIER_OBJECT_ID,Sim.SOLDIER_HORDE_ID,0,_rule())
func _rule()->Dictionary:return {"horde_id":Sim.SOLDIER_HORDE_ID,"category":"hero","speed":1.0,"speed_source":10.0,"acceleration":1.0,"acceleration_source":10.0,"turn_rate_degrees_per_second":180.0,"braking":1.0,"braking_source":10.0,"attack_range":1.0,"attack_range_source":1.0,"minimum_attack_range":0.0,"minimum_attack_range_source":0.0,"vision_range":10.0,"vision_range_source":10.0,"delay_between_shots_ms":1000.0,"pre_attack_delay_ms":0.0,"firing_duration_ms":0.0,"attack_period_ticks":10,"pre_attack_ticks":0,"firing_duration_ticks":0,"member_damage":10,"member_health":100,"member_count":1,"formation_positions":[Vector3.ZERO],"provenance":{}}
