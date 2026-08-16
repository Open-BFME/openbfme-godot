extends SceneTree

const Sim = preload("res://src/retail_slice/retail_slice_sim.gd")
const EXPECTED := 18
var passed := 0
var failed := 0

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	var sim := _sim(true)
	sim._unit_module_contracts[Sim.SOLDIER_HORDE_ID] = [_contract()]
	_spawn(sim, 1, 1)
	var row := sim.entities[1] as Dictionary
	var policy := row.get("pickup_stuff_update", {}) as Dictionary
	_check("typed_contract_attaches", not policy.is_empty())
	_check("filter_preserved", (policy.get("filter", []) as Array) == ["NONE", "+CRATE"])
	_check("range_preserved", is_equal_approx(float(policy.get("scan_range_source", 0.0)), 200.0))
	_check("interval_quantized", int(policy.get("scan_interval_ticks", 0)) == 5)
	var far := sim.register_pickup_object(["CRATE"], Vector2(25, 0), "Far")
	var wrong := sim.register_pickup_object(["RING"], Vector2(2, 0), "Wrong")
	var tied_high := sim.register_pickup_object(["CRATE"], Vector2(10, 0), "TieHigh")
	var tied_low := sim.register_pickup_object(["CRATE"], Vector2(-10, 0), "TieLow")
	_check("ids_are_deterministic", [far, wrong, tied_high, tied_low] == [60000, 60001, 60002, 60003])
	sim._step_pickup_stuff_updates()
	_check("lower_id_wins_equal_distance", int(row.get("pickup_target_id", 0)) == tied_high)
	_check("matching_pickup_routes", String(row.get("order_kind", "")) == "pickup" and not (row.get("route", []) as Array).is_empty())
	_check("wrong_kind_ignored", int(row.get("pickup_target_id", 0)) != wrong)
	var first_next := int((row["pickup_stuff_update"] as Dictionary).get("next_scan_tick", 0))
	sim.remove_pickup_object(tied_high)
	sim._step_pickup_stuff_updates()
	_check("cadence_prevents_early_rescan", int(row.get("pickup_target_id", 0)) == tied_high and first_next == 5)
	sim.tick_index = 5
	sim._step_pickup_stuff_updates()
	_check("rescan_selects_next_candidate", int(row.get("pickup_target_id", 0)) == tied_low)
	var snapshot := sim.snapshot()
	var hash_before := sim.state_hash()
	var restored := _sim(true)
	_check("snapshot_restores", restored.restore(snapshot))
	_check("hash_round_trips", restored.state_hash() == hash_before)
	_check("pickup_world_round_trips", restored.pickup_objects == sim.pickup_objects)
	_check("schedule_round_trips", int(((restored.entities[1] as Dictionary).get("pickup_stuff_update", {}) as Dictionary).get("next_scan_tick", -1)) == 10)
	var human := _sim(false)
	human._unit_module_contracts[Sim.SOLDIER_HORDE_ID] = [_contract()]
	_spawn(human, 2, 0)
	human.register_pickup_object(["CRATE"], Vector2(5, 0))
	human._step_pickup_stuff_updates()
	_check("skirmish_ai_only_blocks_human_lane", not (human.entities[2] as Dictionary).has("pickup_target_id"))
	var outside := _sim(true)
	outside._unit_module_contracts[Sim.SOLDIER_HORDE_ID] = [_contract()]
	_spawn(outside, 3, 1)
	outside.register_pickup_object(["CRATE"], Vector2(21, 0))
	outside._step_pickup_stuff_updates()
	_check("source_range_is_scaled", not (outside.entities[3] as Dictionary).has("pickup_target_id"))
	var opaque := _sim(true)
	var opaque_contract := _contract()
	opaque_contract["extraction"] = "opaque"
	opaque._unit_module_contracts[Sim.SOLDIER_HORDE_ID] = [opaque_contract]
	_spawn(opaque, 4, 1)
	opaque.register_pickup_object(["CRATE"], Vector2(5, 0))
	opaque._step_pickup_stuff_updates()
	_check("opaque_contract_fails_closed", not (opaque.entities[4] as Dictionary).has("pickup_target_id"))
	_check("removed_pickup_absent_after_restore", not restored.pickup_objects.has(tied_high))
	var ran := passed + failed
	if ran != EXPECTED:
		failed += 1
		push_error("PICKUP_STUFF_UPDATE_RUNTIME_FAIL liveness ran=%d expected=%d" % [ran, EXPECTED])
	print("PICKUP_STUFF_UPDATE_RUNTIME_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)

func _contract() -> Dictionary:
	return {"module":"PickupStuffUpdate","extraction":"typed","tag":"ModuleTag_Pickup","line":8,"fields":{"SkirmishAIOnly":{"value":true},"StuffToPickUp":{"value":["NONE","+CRATE"]},"ScanRange":{"value":200},"ScanIntervalSeconds":{"seconds":0.5,"milliseconds":500,"value":0.5}}}

func _sim(ai: bool) -> RetailSliceSim:
	var rules := {}
	for object_id in [Sim.SOLDIER_OBJECT_ID,Sim.SOLDIER_HORDE_ID,Sim.ARCHER_OBJECT_ID,Sim.TOWER_GUARD_OBJECT_ID,Sim.KNIGHT_OBJECT_ID]:
		rules[object_id] = _rule().duplicate(true)
	var sim: RetailSliceSim = Sim.new()
	sim.setup({}, {"unit_rules":rules,"source_map_transform_scale":0.1})
	sim.ai_enabled = ai
	sim.base_loop_enabled = false
	sim.entities.clear()
	sim.structures.clear()
	return sim

func _spawn(sim: RetailSliceSim, id: int, team: int) -> void:
	sim._add_battalion(id, team, Vector2.ZERO, "Fixture", Sim.SOLDIER_OBJECT_ID, Sim.SOLDIER_HORDE_ID, 0, _rule())

func _rule() -> Dictionary:
	return {"horde_id":Sim.SOLDIER_HORDE_ID,"category":"infantry","speed":1.0,"speed_source":10.0,"acceleration":1.0,"acceleration_source":10.0,"turn_rate_degrees_per_second":180.0,"braking":1.0,"braking_source":10.0,"attack_range":1.0,"attack_range_source":1.0,"minimum_attack_range":0.0,"minimum_attack_range_source":0.0,"vision_range":10.0,"vision_range_source":10.0,"delay_between_shots_ms":1000.0,"pre_attack_delay_ms":0.0,"firing_duration_ms":0.0,"attack_period_ticks":10,"pre_attack_ticks":0,"firing_duration_ticks":0,"member_damage":1,"member_health":100,"member_count":1,"formation_positions":[Vector3.ZERO],"provenance":{}}

func _check(label: String, condition: bool) -> void:
	if condition:
		passed += 1
	else:
		failed += 1
		push_error("PICKUP_STUFF_UPDATE_RUNTIME_FAIL %s" % label)
