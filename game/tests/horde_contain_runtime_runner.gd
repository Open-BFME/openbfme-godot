extends SceneTree

const Sim = preload("res://src/retail_slice/retail_slice_sim.gd")
const EXPECTED_CHECKS := 23
var passed := 0
var failed := 0

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	var sim := _make_sim()
	sim._unit_module_contracts[Sim.SOLDIER_HORDE_ID] = [_contract()]
	_spawn(sim, 1)
	var row := sim.entities[1] as Dictionary
	_check("typed_horde_contract_attaches", row.has("horde_contain"))
	_check("resolved_initial_payload_materializes", (row.get("horde_contained_members", []) as Array).size() == 3)
	_check("initial_payload_receives_contained_status", bool(((((row.get("horde_contained_members", []) as Array)[0] as Dictionary).get("object_status", {}) as Dictionary).get("UNSELECTABLE", false))))
	_check("capacity_and_filter_bind", int((row["horde_contain"] as Dictionary).get("slots", 0)) == 4)
	var admitted := sim.admit_horde_member(1, "GondorFighter", ["INFANTRY"], 2, 10)
	_check("matching_member_is_admitted", bool(admitted.get("ok", false)))
	var members := row.get("horde_contained_members", []) as Array
	_check("contained_status_is_applied", bool((((members[3] as Dictionary).get("object_status", {}) as Dictionary).get("UNSELECTABLE", false))))
	_check("capacity_full_is_enforced", String(sim.admit_horde_member(1, "Extra", ["INFANTRY"]).get("reason", "")) == "capacity-full")
	var sim_filter := _make_sim()
	sim_filter._unit_module_contracts[Sim.SOLDIER_HORDE_ID] = [_contract()]
	_spawn(sim_filter, 2)
	_check("negative_filter_refuses_siege", String(sim_filter.admit_horde_member(2, "Siege", ["SIEGE"]).get("reason", "")) == "passenger-filter-refused")
	var damaged := sim.apply_horde_contained_damage(1, 3, 4)
	_check("contained_damage_is_deterministic", bool(damaged.get("ok", false)) and int(((row.get("horde_contained_members", []) as Array)[3] as Dictionary).get("health", 0)) == 6)
	var killed := sim.apply_horde_contained_damage(1, 3, 99)
	_check("contained_death_removes_member", bool(killed.get("killed", false)) and (row.get("horde_contained_members", []) as Array).size() == 3)
	var ejected := sim.eject_horde_member(1, 0)
	_check("eject_removes_and_clears_status", bool(ejected.get("ok", false)) and String((ejected.get("member", {}) as Dictionary).get("status", "")) == "ejected" and ((ejected.get("member", {}) as Dictionary).get("object_status", {}) as Dictionary).is_empty())
	var reach := sim.horde_amoeba_melee_reach(1, Vector2(50, 0), false)
	_check("amoeba_outer_range_is_consumed", bool(reach.get("in_range", false)) and is_equal_approx(float(reach.get("outer_range", 0.0)), 100.0))
	var building_reach := sim.horde_amoeba_melee_reach(1, Vector2(120, 0), true)
	_check("amoeba_building_range_is_distinct", bool(building_reach.get("in_range", false)) and is_equal_approx(float(building_reach.get("outer_range", 0.0)), 140.0))
	var snapshot := sim.snapshot()
	var state_hash := sim.state_hash()
	var restored := _make_sim()
	_check("horde_state_snapshot_restores", restored.restore(snapshot))
	_check("horde_state_hash_round_trips", restored.state_hash() == state_hash)
	_check("restored_membership_is_exact", ((restored.entities[1] as Dictionary).get("horde_contained_members", []) as Array) == (row.get("horde_contained_members", []) as Array))
	var receipt := ((row.get("horde_contain", {}) as Dictionary).get("unsupported_semantics", []) as Array)
	_check("model_and_hud_fields_are_receipted", receipt.has("model_or_hud_binding:ShowPips") and receipt.has("model_or_hud_binding:RankInfo") and receipt.has("model_or_hud_binding:RandomOffset"))
	_check("stored_but_unexecuted_formation_fields_are_receipted", receipt.has("unsupported_horde_field:FrontAngle") and receipt.has("unsupported_horde_field:FlankedDelay"))
	var multiplied := sim._resolve_horde_payload_count({"countExpression":"#MULTIPLY(TEST_SIZE 2)","count":{"kind":"multiply","name":"TEST_SIZE","factor":2}}, {"TEST_SIZE":3})
	_check("compiled_multiply_payload_count_resolves", bool(multiplied.get("ok", false)) and int(multiplied.get("count", 0)) == 6)
	var unresolved := _make_sim(false)
	unresolved._unit_module_contracts[Sim.SOLDIER_HORDE_ID] = [_contract()]
	_spawn(unresolved, 3)
	var unresolved_receipt := ((unresolved.entities[3] as Dictionary).get("horde_contain", {}) as Dictionary).get("unsupported_semantics", []) as Array
	_check("unresolved_payload_count_is_receipted", unresolved_receipt.has("unresolved_payload_count:GOOD_MEN_HORDE_SIZE"))
	var opaque := _make_sim()
	var opaque_contract := _contract(); opaque_contract["extraction"] = "opaque"
	opaque._unit_module_contracts[Sim.SOLDIER_HORDE_ID] = [opaque_contract]
	_spawn(opaque, 4)
	_check("opaque_contract_fails_closed", String(opaque.admit_horde_member(4, "Unit", ["INFANTRY"]).get("reason", "")) == "typed-horde-contain-contract-missing")
	var zero := _make_sim(); var zero_contract := _contract(); (zero_contract["fields"] as Dictionary)["Slots"] = {"value":0}; (zero_contract["fields"] as Dictionary)["InitialPayload"] = []
	zero._unit_module_contracts[Sim.SOLDIER_HORDE_ID] = [zero_contract]; _spawn(zero, 5)
	_check("zero_capacity_fails_closed", String(zero.admit_horde_member(5, "Unit", ["INFANTRY"]).get("reason", "")) == "capacity-zero")
	var banner := _make_sim(); var banner_contract := _contract(); (banner_contract["fields"] as Dictionary)["InitialPayload"] = []; (banner_contract["fields"] as Dictionary)["Slots"] = {"value":1}; (banner_contract["fields"] as Dictionary)["PassengerFilter"] = {"value":["ANY"]}; (banner_contract["fields"] as Dictionary)["BannerCarrierDestroyHordeOnDeath"] = {"value":true}; (banner_contract["fields"] as Dictionary)["BannerCarrierHordeDeathType"] = {"value":["NORMAL"]}
	banner._unit_module_contracts[Sim.SOLDIER_HORDE_ID] = [banner_contract]; _spawn(banner, 6); banner.admit_horde_member(6, "GondorBanner", ["BANNER"], 1, 1); var banner_death := banner.apply_horde_contained_damage(6, 0, 1, "NORMAL")
	_check("authored_banner_death_dispatches_horde_death", bool(banner_death.get("horde_killed", false)) and int((banner.entities[6] as Dictionary).get("health", 1)) == 0)
	var ran := passed + failed
	if ran != EXPECTED_CHECKS:
		failed += 1
		printerr("HORDE_CONTAIN_RUNTIME_FAIL liveness ran=%d expected=%d" % [ran, EXPECTED_CHECKS])
	print("HORDE_CONTAIN_RUNTIME_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)

func _contract() -> Dictionary:
	return {"module":"HordeContain","extraction":"typed","tag":"ModuleTag_Horde","line":10,"fields":{
		"ObjectStatusOfContained":{"value":["UNSELECTABLE"]},"InitialPayload":[{"objectId":"GondorFighter","countExpression":"GOOD_MEN_HORDE_SIZE","count":{"kind":"define","name":"GOOD_MEN_HORDE_SIZE"}}],
		"Slots":{"value":4},"PassengerFilter":{"value":["NONE","+INFANTRY","-SIEGE"]},"ShowPips":{"value":false},
		"ThisFormationIsTheMainFormation":{"value":true},"RandomOffset":[{"value":{"x":4.0,"y":5.0}}],
		"RankInfo":[{"clauses":[{"key":"RankNumber","value":"1"},{"key":"UnitType","value":"GondorFighter"},{"key":"Position","value":"X:0"}]}],
		"FrontAngle":{"value":270.0},"FlankedDelay":{"milliseconds":2000.0},"MeleeBehavior":{"value":"Amoeba"},
		"FacingBonus":{"value":30.0},"AngleLimitCos":{"value":-0.25},"InnerRange":{"value":1.0},"OuterRange":{"value":100.0},"OuterRangeBuildings":{"value":140.0}
	}}

func _make_sim(resolve_count: bool = true) -> RetailSliceSim:
	var rules := {}; for object_id in [Sim.SOLDIER_OBJECT_ID,Sim.SOLDIER_HORDE_ID,Sim.ARCHER_OBJECT_ID,Sim.TOWER_GUARD_OBJECT_ID,Sim.KNIGHT_OBJECT_ID]: rules[object_id]=_unit_rule().duplicate(true)
	var gameplay := {"unit_rules":rules,"source_map_transform_scale":1.0}
	if resolve_count: gameplay["horde_payload_counts"]={"GOOD_MEN_HORDE_SIZE":3}
	var sim: RetailSliceSim=Sim.new(); sim.setup({},gameplay); sim.ai_enabled=false;sim.base_loop_enabled=false;sim.entities.clear();sim.structures.clear();return sim

func _spawn(sim: RetailSliceSim,id:int)->void:
	sim._add_battalion(id,Sim.PLAYER_TEAM,Vector2.ZERO,"Fixture",Sim.SOLDIER_OBJECT_ID,Sim.SOLDIER_HORDE_ID,0,_unit_rule())

func _unit_rule()->Dictionary:
	return {"horde_id":Sim.SOLDIER_HORDE_ID,"category":"infantry","speed":1.0,"speed_source":10.0,"acceleration":1.0,"acceleration_source":10.0,"turn_rate_degrees_per_second":180.0,"braking":1.0,"braking_source":10.0,"attack_range":1.0,"attack_range_source":1.0,"minimum_attack_range":0.0,"minimum_attack_range_source":0.0,"vision_range":10.0,"vision_range_source":10.0,"delay_between_shots_ms":1000.0,"pre_attack_delay_ms":0.0,"firing_duration_ms":0.0,"attack_period_ticks":10,"pre_attack_ticks":0,"firing_duration_ticks":0,"member_damage":1,"member_health":100,"member_count":1,"formation_positions":[Vector3.ZERO],"provenance":{}}

func _check(label:String,condition:bool)->void:
	if condition: passed+=1
	else: failed+=1;push_error("HORDE_CONTAIN_RUNTIME_FAIL %s"%label)
