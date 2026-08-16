extends SceneTree
const Sim = preload("res://src/retail_slice/retail_slice_sim.gd")
const EXPECTED := 28
var passed := 0
var failed := 0
func _initialize() -> void: call_deferred("_run")
func _run() -> void:
	var sim := _sim()
	var source := sim.entities[1] as Dictionary
	sim._attach_large_group_bonus_contract(source, _large_group())
	_check("large_group_attaches", source.has("large_group_bonus"))
	sim._step_large_group_bonus_updates()
	_check("threshold_inactive", not bool((source["large_group_bonus"] as Dictionary).get("active", true)))
	(sim.entities[3] as Dictionary)["position"] = Vector2(5, 0)
	sim.tick_index += 2; sim._step_large_group_bonus_updates()
	_check("threshold_activates", bool((source["large_group_bonus"] as Dictionary).get("active", false)))
	_check("modifier_applied", (source.get("timed_modifiers", {}) as Dictionary).has("large-group:FixtureGroupBonus"))
	(sim.entities[3] as Dictionary)["position"] = Vector2(15, 0)
	sim.tick_index += 2; sim._step_large_group_bonus_updates()
	_check("ruboff_hysteresis", bool((source["large_group_bonus"] as Dictionary).get("active", false)))
	(sim.entities[3] as Dictionary)["position"] = Vector2(30, 0)
	sim.tick_index += 2; sim._step_large_group_bonus_updates()
	_check("ruboff_removes", not bool((source["large_group_bonus"] as Dictionary).get("active", true)) and not (source.get("timed_modifiers", {}) as Dictionary).has("large-group:FixtureGroupBonus"))

	var queue := sim.structures[50] as Dictionary
	sim._attach_container_family_contract(queue, _queue())
	_check("queue_distinct_module", String((queue["horde_transport"] as Dictionary).get("module")) == "ProductionQueueHordeContain")
	_check("queue_capacity", int(queue.get("transport_capacity", 0)) == 2)
	_check("queue_presentation_receipts", ((queue["horde_transport"] as Dictionary).get("unsupported_semantics", []) as Array).has("enter_sound_requires_audio_binding"))
	_check("queue_admits", bool(sim.load_transport_entity(50, 4).get("ok", false)))
	_check("queue_entry_offset", Vector2((sim.entities[4] as Dictionary).get("position")).is_equal_approx(Vector2(0, 4.5)))
	var queue_exit := sim.request_transport_exit(4)
	sim.tick_index = int(queue_exit.get("exit_tick", sim.tick_index)); sim._step_ship_runtime()
	_check("queue_exit_offset", Vector2((sim.entities[4] as Dictionary).get("position")).is_equal_approx(Vector2(0, -4.5)))

	var siege := sim.structures[60] as Dictionary
	sim._attach_siege_engine_contain_contract(siege, _siege())
	_check("siege_attaches", String((siege["horde_transport"] as Dictionary).get("module")) == "SiegeEngineContain")
	_check("initial_crew_receipted", ((siege["horde_transport"] as Dictionary).get("unsupported_semantics", []) as Array).has("initial_crew_requires_object_factory"))
	_check("rider_filter_refuses_crew", String(sim.load_transport_entity(60, 5).get("reason", "")) == "passenger-filter-refused")
	_check("crew_filter_refuses_rider", String(sim.load_siege_crew(60, 6).get("reason", "")) == "crew-filter-refused")
	_check("crew_admitted", bool(sim.load_siege_crew(60, 5).get("ok", false)))
	_check("crew_status_applied", bool(((sim.entities[5] as Dictionary).get("object_status", {}) as Dictionary).get("UNATTACKABLE", false)))
	_check("crew_speed_exact", is_equal_approx(float(siege.get("siege_speed_multiplier", 0.0)), 0.25))
	_check("rider_admitted", bool(sim.load_transport_entity(60, 6).get("ok", false)))
	_check("siege_capacity_full", String(sim.load_transport_entity(60, 7).get("reason", "")) == "capacity-full")
	var crew_exit := sim.request_transport_exit(5)
	_check("siege_exit_delay", int(crew_exit.get("exit_tick", -1)) == sim.tick_index + 5)
	sim.tick_index = int(crew_exit.get("exit_tick")); sim._step_ship_runtime()
	_check("crew_aggressive_exit", String((sim.entities[5] as Dictionary).get("stance", "")) == "Aggressive")
	_check("crew_status_restored", not ((sim.entities[5] as Dictionary).get("object_status", {}) as Dictionary).has("UNATTACKABLE"))
	siege["health"] = 0; sim._step_ship_runtime()
	_check("siege_death_kills_rider", int((sim.entities[6] as Dictionary).get("health", 1)) == 0 and not sim.entity_container.has(6))
	var snap := sim.snapshot(); var hash := sim.state_hash(); var restored := _sim()
	_check("snapshot_restore", restored.restore(snap))
	_check("hash_restore", restored.state_hash() == hash)
	var opaque := {}; sim._attach_siege_engine_contain_contract(opaque, {"module":"SiegeEngineContain","extraction":"opaque","fields":{}})
	_check("opaque_fails_closed", not opaque.has("horde_transport"))
	print("LARGE_GROUP_SIEGE_QUEUE_RUNTIME_RESULT passed=%d failed=%d" % [passed, failed]); quit(0 if failed == 0 and passed == EXPECTED else 1)
func _large_group() -> Dictionary: return {"module":"LargeGroupBonusUpdate","extraction":"typed","fields":{"UpdateRate":{"milliseconds":200},"HordeMemberFilter":{"value":["ANY","+INFANTRY"]},"Count":{"value":3},"Radius":{"value":100.0},"RubOffRadius":{"value":200.0},"AlliesOnly":{"value":true},"AttributeModifier":{"value":"FixtureGroupBonus"}}}
func _queue() -> Dictionary: return {"module":"ProductionQueueHordeContain","extraction":"typed","fields":{"ObjectStatusOfContained":{"value":["UNSELECTABLE","ENCLOSED"]},"ContainMax":{"value":2},"DamagePercentToUnits":{"ratio":0.0},"PassengerFilter":{"value":["ANY","+INFANTRY"]},"AllowEnemiesInside":{"value":false},"AllowNeutralInside":{"value":false},"AllowAlliesInside":{"value":true},"NumberOfExitPaths":{"value":1},"EntryPosition":{"value":{"x":0,"y":0,"z":0}},"EntryOffset":{"value":{"x":0,"y":45,"z":0}},"ExitOffset":{"value":{"x":0,"y":-45,"z":0}},"EnterSound":{"value":"FixtureEnter"}}}
func _siege() -> Dictionary: return {"module":"SiegeEngineContain","extraction":"typed","fields":{"ObjectStatusOfCrew":{"value":["UNSELECTABLE","UNATTACKABLE"]},"Slots":{"value":1},"DamagePercentToUnits":{"ratio":1.0},"PassengerFilter":{"value":["NONE","+CAN_RIDE_BATTERING_RAM"]},"KillPassengersOnDeath":{"value":true},"AllowAlliesInside":{"value":true},"AllowEnemiesInside":{"value":false},"AllowNeutralInside":{"value":false},"CrewFilter":{"value":["NONE","+INFANTRY","-CAN_RIDE_BATTERING_RAM"]},"CrewMax":{"value":2},"InitialCrew":{"object":"FixtureCrew","count":2},"ExitDelay":{"milliseconds":500},"NumberOfExitPaths":{"value":0},"GoAggressiveOnExit":{"value":true},"SpeedPercentPerCrew":{"fraction":0.25},"PassengerBonePrefix":[{"passengerBone":"CREWBONE","kindOf":"INFANTRY"}],"BoneSpecificConditionState":[{"boneIndex":1,"conditionState":"PASSENGER_VARIATION_1"}]}}
func _sim() -> RetailSliceSim:
	var rules := {}; for id in [Sim.SOLDIER_OBJECT_ID,Sim.SOLDIER_HORDE_ID,Sim.ARCHER_OBJECT_ID,Sim.TOWER_GUARD_OBJECT_ID,Sim.KNIGHT_OBJECT_ID]: rules[id] = _rule()
	var sim:RetailSliceSim=Sim.new(); sim.setup({}, {"unit_rules":rules,"source_unit_scale":0.1,"attribute_modifier_rules":{"FixtureGroupBonus":{"effects":[{"kind":"damageMultiplier","value":1.2}],"category":"GROUP","duration_ms":500}}}); sim.ai_enabled=false; sim.base_loop_enabled=false; sim.entities.clear(); sim.structures.clear()
	for id in range(1,8): sim.entities[id]=_unit(id,["INFANTRY","CAN_RIDE_BATTERING_RAM"] if id>=6 else ["INFANTRY"])
	(sim.entities[1] as Dictionary)["position"]=Vector2.ZERO; (sim.entities[2] as Dictionary)["position"]=Vector2(5,0); (sim.entities[3] as Dictionary)["position"]=Vector2(30,0)
	for id in range(4,8): (sim.entities[id] as Dictionary)["position"]=Vector2(100,0)
	sim.structures[50]={"id":50,"team":0,"health":100,"position":Vector2.ZERO}; sim.structures[60]={"id":60,"team":0,"health":100,"position":Vector2(50,0)}; return sim
func _unit(id:int,kinds:Array)->Dictionary:return {"id":id,"team":0,"health":100,"maximum_health":100,"member_health":[100],"position":Vector2.ZERO,"category":"infantry","kind_of":kinds,"object_status":{},"timed_modifiers":{}}
func _rule()->Dictionary:return {"horde_id":Sim.SOLDIER_HORDE_ID,"category":"infantry","speed":1.0,"speed_source":10.0,"acceleration":1.0,"acceleration_source":10.0,"turn_rate_degrees_per_second":180.0,"braking":1.0,"braking_source":10.0,"attack_range":1.0,"attack_range_source":1.0,"minimum_attack_range":0.0,"minimum_attack_range_source":0.0,"vision_range":10.0,"vision_range_source":10.0,"delay_between_shots_ms":1000.0,"pre_attack_delay_ms":0.0,"firing_duration_ms":0.0,"attack_period_ticks":10,"pre_attack_ticks":0,"firing_duration_ticks":0,"member_damage":1,"member_health":100,"member_count":1,"formation_positions":[Vector3.ZERO],"provenance":{}}
func _check(label:String,condition:bool)->void:
	if condition: passed += 1
	else: failed += 1; push_error("LARGE_GROUP_SIEGE_QUEUE_RUNTIME_FAIL "+label)
