extends SceneTree

# Executable receipt: RepairSpecialPower authored WorkerAIUpdate rate/contact.
const Sim = preload("res://src/retail_slice/retail_slice_sim.gd")
const Adapter = preload("res://src/retail_slice/playable_unit_runtime_adapter.gd")
const EXPECTED := 18
var passed := 0
var failed := 0


func _initialize() -> void:
	create_timer(20.0).timeout.connect(func() -> void: push_error("REPAIR_SPECIAL_POWER_RUNTIME_FAIL watchdog"); quit(1))
	call_deferred("_run")


func _run() -> void:
	var graph := _graph("authored")
	_check("adapter_accepts_authored_repair_graph", Adapter.ability_rules(_doc(graph)).size() == 1)
	var malformed := graph.duplicate(true); (malformed.get("targeting", {}) as Dictionary)["relation"] = "ENEMY"
	_check("adapter_rejects_non_allied_repair_graph", Adapter.ability_rules(_doc(malformed)).is_empty())
	var sim := _sim(); sim._unit_ability_rules[Sim.SOLDIER_HORDE_ID] = sim._scaled_ability_rules([_ability(graph)], 0.1); _spawn_worker(sim); sim._attach_hero_ability_state(sim.entities[1] as Dictionary)
	sim.structures[10] = _structure(10, 0, 500)
	var resources_before := sim.team_resources.duplicate(true)
	var cast := sim.cast_ability(1, "Command_Repair", Vector2(1, 0), 0)
	_check("damaged_allied_contact_accepts", bool(cast.get("ok", false)) and int(cast.get("target_id", 0)) == 10)
	_check("no_authored_cost_spends_nothing", sim.team_resources == resources_before)
	_check("repair_channel_preserves_exact_rate", is_equal_approx(float(((sim.entities[1] as Dictionary).get("repair_structure_channel", {}) as Dictionary).get("max_health_fraction_per_second", 0.0)), 0.002))
	for tick in range(1, 5): sim.tick_index=tick; sim._step_repair_structure(sim.entities[1] as Dictionary)
	_check("fractional_repair_does_not_round_up", int((sim.structures[10] as Dictionary).get("health", 0)) == 500)
	var snap := sim.snapshot(); var hash := sim.state_hash(); var restored := _sim()
	_check("fractional_repair_snapshot_hash_restores", restored.restore(snap) and restored.state_hash() == hash)
	for tick in range(5, 11): sim.tick_index=tick; sim._step_repair_structure(sim.entities[1] as Dictionary)
	for tick in range(5, 11): restored.tick_index=tick; restored._step_repair_structure(restored.entities[1] as Dictionary)
	_check("authored_rate_applies_exactly_over_time", int((sim.structures[10] as Dictionary).get("health", 0)) == 502)
	_check("restored_repair_path_is_identical", sim.state_hash() == restored.state_hash())
	(sim.structures[10] as Dictionary)["health"] = 999; ((sim.entities[1] as Dictionary).get("repair_structure_channel", {}) as Dictionary)["fractional_health"] = 0.8
	sim.tick_index=11; sim._step_repair_structure(sim.entities[1] as Dictionary); sim.tick_index=12; sim._step_repair_structure(sim.entities[1] as Dictionary)
	_check("repair_stops_at_exact_maximum", int((sim.structures[10] as Dictionary).get("health", 0)) == 1000 and not (sim.entities[1] as Dictionary).has("repair_structure_channel"))
	_check("repair_finished_receipt_emits", _has_event(sim, "ability.repair_finished"))

	var unresolved := _sim(); unresolved._unit_ability_rules[Sim.SOLDIER_HORDE_ID]=unresolved._scaled_ability_rules([_ability(_graph("engine-default-unresolved"))],0.1); _spawn_worker(unresolved); unresolved._attach_hero_ability_state(unresolved.entities[1] as Dictionary); unresolved.structures[10]=_structure(10,0,500)
	var refused := unresolved.cast_ability(1,"Command_Repair",Vector2(1,0),0)
	_check("engine_default_rate_remains_explicit_refusal", String(refused.get("reason", "")) == "repair-rate-engine-default-unresolved")
	_check("refused_rate_does_not_arm_cooldown", int(((((unresolved.entities[1] as Dictionary).get("ability_states",{}) as Dictionary).get("Command_Repair",{}) as Dictionary).get("cooldown_ready_tick",-1))) == 0)

	var invalid := _sim(); invalid._unit_ability_rules[Sim.SOLDIER_HORDE_ID]=invalid._scaled_ability_rules([_ability(graph)],0.1); _spawn_worker(invalid); invalid._attach_hero_ability_state(invalid.entities[1] as Dictionary); invalid.structures[10]=_structure(10,1,500)
	_check("enemy_structure_is_refused", String(invalid.cast_ability(1,"Command_Repair",Vector2(1,0),0).get("reason", "")) == "no-damaged-allied-structure-at-repair-contact")
	(invalid.structures[10] as Dictionary)["team"]=0; (invalid.structures[10] as Dictionary)["health"]=1000
	_check("undamaged_structure_is_refused", String(invalid.cast_ability(1,"Command_Repair",Vector2(1,0),0).get("reason", "")) == "no-damaged-allied-structure-at-repair-contact")
	(invalid.structures[10] as Dictionary)["health"]=500
	_check("outside_authored_footprint_contact_is_refused", String(invalid.cast_ability(1,"Command_Repair",Vector2(50,0),0).get("reason", "")) == "no-damaged-allied-structure-at-repair-contact")

	var interrupted := _sim(); interrupted._unit_ability_rules[Sim.SOLDIER_HORDE_ID]=interrupted._scaled_ability_rules([_ability(graph)],0.1); _spawn_worker(interrupted); interrupted._attach_hero_ability_state(interrupted.entities[1] as Dictionary); interrupted.structures[10]=_structure(10,0,500); interrupted.cast_ability(1,"Command_Repair",Vector2(1,0),0); (interrupted.entities[1] as Dictionary)["order_sequence"]=1; interrupted.tick_index=1; interrupted._step_repair_structure(interrupted.entities[1] as Dictionary)
	_check("new_order_interrupts_repair", not (interrupted.entities[1] as Dictionary).has("repair_structure_channel"))
	_check("interrupt_does_not_apply_health", int((interrupted.structures[10] as Dictionary).get("health",0)) == 500)
	_finish()


func _sim() -> RetailSliceSim:
	var rules := {}; var base := _rule()
	for object_id in [Sim.SOLDIER_OBJECT_ID,Sim.SOLDIER_HORDE_ID,Sim.ARCHER_OBJECT_ID,Sim.TOWER_GUARD_OBJECT_ID,Sim.KNIGHT_OBJECT_ID]: rules[object_id]=base
	var sim:RetailSliceSim=Sim.new(); sim.setup({}, {"unit_rules":rules,"source_map_transform_scale":0.1}); sim.ai_enabled=false; sim.base_loop_enabled=false; sim.entities.clear(); sim.structures.clear(); return sim


func _spawn_worker(sim: RetailSliceSim) -> void:
	var rule:=_rule(); sim._rules["unit_rules"][Sim.SOLDIER_OBJECT_ID]=rule; sim._add_battalion(1,0,Vector2.ZERO,"RohanPeasant",Sim.SOLDIER_OBJECT_ID,Sim.SOLDIER_HORDE_ID,0,rule)


func _rule()->Dictionary:
	return {"source_object_id":"RohanPeasant","horde_id":Sim.SOLDIER_HORDE_ID,"category":"worker","speed":1.0,"speed_source":10.0,"acceleration":1.0,"acceleration_source":10.0,"turn_rate_degrees_per_second":180.0,"braking":1.0,"braking_source":10.0,"attack_range":1.0,"attack_range_source":10.0,"minimum_attack_range":0.0,"minimum_attack_range_source":0.0,"vision_range":20.0,"vision_range_source":200.0,"delay_between_shots_ms":1000.0,"pre_attack_delay_ms":0.0,"firing_duration_ms":0.0,"attack_period_ticks":10,"pre_attack_ticks":0,"firing_duration_ticks":0,"member_damage":10,"member_health":100,"member_count":1,"formation_positions":[Vector3.ZERO],"provenance":{}}


func _structure(id:int,team:int,health:int)->Dictionary:
	return {"id":id,"team":team,"position":Vector2(1,0),"health":health,"maximum_health":1000,"footprint_radius_source":20.0,"source_object_id":"FixtureStructure","structure_kind":"farm"}


func _graph(status:String)->Dictionary:
	var rate := {"status":status,"sourceIni":"object.ini","line":2}
	if status=="authored": rate["maxHealthFractionPerSecond"]=0.002
	return {"kind":"repair-structure","specialPowerTemplateId":"SpecialRepairStructure","targeting":{"relation":"ALLY","kindOf":["STRUCTURE"],"requiresDamaged":true,"rangeMode":"REPAIR_CONTACT_POINT"},"repairRate":rate,"contactPoint":{"name":"Repair","authored":true,"sourceIni":"object.ini","line":3},"economy":{"status":"no-authored-resource-field","resourceCost":null},"sourceIni":"object.ini","line":1}


func _ability(graph:Dictionary)->Dictionary:return {"ability_id":"Command_Repair","special_power_id":"SpecialRepairStructure","targeting":"point","cooldown_ticks":0,"required_level":1,"level_gate_resolved":true,"castable":true,"effect":graph,"special_power_contract":{}}
func _doc(graph:Dictionary)->Dictionary:return {"registration":{"abilities":[{"id":"Command_Repair","slot":1,"targeting":"point","specialPowerId":"SpecialRepairStructure","cooldownMs":0,"button":{},"effect":graph,"implementation":{"status":"implemented","reason":"","limitations":[]},"levelGate":{}}]}}
func _has_event(sim:RetailSliceSim,kind:String)->bool:
	for value in sim.events:
		if String((value as Dictionary).get("kind",""))==kind:return true
	return false
func _check(label:String,condition:bool)->void:
	if condition:passed+=1
	else:failed+=1;push_error("REPAIR_SPECIAL_POWER_RUNTIME_FAIL "+label)
func _finish()->void:
	if passed+failed!=EXPECTED:failed+=1;push_error("REPAIR_SPECIAL_POWER_RUNTIME_FAIL count")
	print("REPAIR_SPECIAL_POWER_RUNTIME_RESULT passed=%d failed=%d"%[passed,failed]);quit(0 if failed==0 else 1)
