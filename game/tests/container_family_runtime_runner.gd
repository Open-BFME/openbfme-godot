extends SceneTree

const Sim = preload("res://src/retail_slice/retail_slice_sim.gd")
const EXPECTED := 30
var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var sim := _sim()
	var transport := sim.structures[50] as Dictionary
	sim._attach_container_family_contract(transport, _transport())
	_check("transport_attaches", String((transport["horde_transport"] as Dictionary).get("module")) == "TransportContain")
	_check("capacity_authored", int(transport.get("transport_capacity", 0)) == 2)
	var first := sim.load_transport_entity(50, 1)
	_check("own_allied_admission", bool(first.get("ok", false)))
	_check("bone_selected", String(first.get("bone", "")) == "PASSENGER")
	_check("contained_status", bool(((sim.entities[1] as Dictionary).get("object_status", {}) as Dictionary).get("UNSELECTABLE", false)))
	_check("entry_offset", Vector2((sim.entities[1] as Dictionary).get("position")).is_equal_approx(Vector2(5, 0)))
	_check("weapon_state_one", (transport.get("contained_weapon_states", []) as Array).has("ONE_STATE"))
	_check("hero_filter_refused", String(sim.load_transport_entity(50, 3).get("reason", "")) == "passenger-filter-refused")
	_check("enemy_refused", String(sim.load_transport_entity(50, 4).get("reason", "")).begins_with("relationship-refused"))
	_check("manual_filter_refused", String(sim.load_transport_entity(50, 5, true).get("reason", "")) == "manual-pickup-filter-refused")
	_check("second_admitted", bool(sim.load_transport_entity(50, 2).get("ok", false)))
	_check("weapon_state_two", (transport.get("contained_weapon_states", []) as Array).has("TWO_STATE"))
	_check("capacity_refused", String(sim.load_transport_entity(50, 5).get("reason", "")) == "capacity-full")
	var exit := sim.request_transport_exit(1)
	_check("exit_delay_exact", int(exit.get("exit_tick", -1)) == sim.tick_index + 2)
	sim.tick_index += 1
	sim._step_ship_runtime()
	_check("exit_not_early", sim.entity_container.has(1))
	sim.tick_index += 1
	sim._step_ship_runtime()
	_check("exit_exact", not sim.entity_container.has(1))
	_check("exit_restores_status", not ((sim.entities[1] as Dictionary).get("object_status", {}) as Dictionary).has("UNSELECTABLE"))
	transport["health"] = 0
	sim._step_ship_runtime()
	_check("transport_death_ejects", not sim.entity_container.has(2))

	var tunnel_a := sim.structures[60] as Dictionary
	var tunnel_b := sim.structures[61] as Dictionary
	sim._attach_container_family_contract(tunnel_a, _tunnel())
	sim._attach_container_family_contract(tunnel_b, _tunnel())
	_check("tunnel_admission", bool(sim.load_transport_entity(60, 5).get("ok", false)))
	var routed := sim.request_tunnel_exit(5, 61)
	_check("tunnel_route_scheduled", bool(routed.get("ok", false)))
	sim.tick_index = int(routed.get("exit_tick", sim.tick_index))
	sim._step_ship_runtime()
	_check("tunnel_route_exits", not sim.entity_container.has(5))
	_check("tunnel_destination_offset", Vector2((sim.entities[5] as Dictionary).get("position")).is_equal_approx(Vector2(110, 0)))
	_check("non_tunnel_destination_refused", not bool(sim.request_tunnel_exit(2, 50).get("ok", false)))

	var garrison := sim.structures[70] as Dictionary
	sim._attach_container_family_contract(garrison, _garrison())
	_check("garrison_uses_distinct_contract", String((garrison["horde_transport"] as Dictionary).get("module")) == "GarrisonContain")
	_check("garrison_admits_ally", bool(sim.load_transport_entity(70, 1).get("ok", false)))
	sim._finish_transport_exit(70, 1)
	var horde_garrison := sim.structures[80] as Dictionary
	sim._attach_container_family_contract(horde_garrison, _horde_garrison())
	_check("horde_garrison_admits", bool(sim.load_transport_entity(80, 1).get("ok", false)))
	horde_garrison["health"] = 0
	sim._step_ship_runtime()
	_check("horde_garrison_death_kills", int((sim.entities[1] as Dictionary).get("health", 1)) == 0 and not sim.entity_container.has(1))

	var snap := sim.snapshot()
	var hash := sim.state_hash()
	var restored := _sim()
	_check("snapshot_restore", restored.restore(snap))
	_check("hash_restore", restored.state_hash() == hash)
	var opaque := {}
	sim._attach_container_family_contract(opaque, {"module": "TransportContain", "extraction": "opaque", "fields": {}})
	_check("opaque_fails_closed", not opaque.has("horde_transport"))
	print("CONTAINER_FAMILY_RUNTIME_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 and passed == EXPECTED else 1)


func _transport() -> Dictionary:
	return {"module": "TransportContain", "extraction": "typed", "fields": {
		"ObjectStatusOfContained": {"value": ["UNSELECTABLE", "CAN_ATTACK"]},
		"PassengerFilter": {"value": ["ANY", "+INFANTRY", "-HERO"]}, "Slots": {"value": 2},
		"ShowPips": {"value": false}, "AllowEnemiesInside": {"value": false},
		"AllowNeutralInside": {"value": false}, "AllowAlliesInside": {"value": true},
		"DamagePercentToUnits": {"ratio": 1.0}, "ExitDelay": {"milliseconds": 200},
		"EjectPassengersOnDeath": {"value": true},
		"ManualPickUpFilter": {"value": ["+MONSTER"]},
		"EntryOffset": {"value": {"x": 50.0, "y": 0.0, "z": 0.0}},
		"PassengerBonePrefix": [{"passengerBone": "PASSENGER", "kindOf": "INFANTRY"}],
		"TypeOneForWeaponState": [{"value": "ONE_STATE"}], "TypeTwoForWeaponState": [{"value": "TWO_STATE"}],
		"BoneSpecificConditionState": [{"boneIndex": 1, "conditionState": "BONE_ONE"}],
	}}


func _tunnel() -> Dictionary:
	return {"module": "TunnelContain", "extraction": "typed", "fields": {
		"ObjectStatusOfContained": {"value": ["UNSELECTABLE"]}, "ContainMax": {"value": 5},
		"DamagePercentToUnits": {"ratio": 0.0}, "PassengerFilter": {"value": ["ANY", "+INFANTRY"]},
		"AllowEnemiesInside": {"value": false}, "AllowNeutralInside": {"value": false},
		"AllowAlliesInside": {"value": true}, "NumberOfExitPaths": {"value": 1},
		"PassengerBonePrefix": [{"passengerBone": "ARROW_", "kindOf": "INFANTRY"}],
		"EntryOffset": {"value": {"x": 0.0, "y": 0.0, "z": 0.0}},
		"ExitOffset": {"value": {"x": 100.0, "y": 0.0, "z": 0.0}},
		"ExitDelay": {"milliseconds": 100}, "KillPassengersOnDeath": {"value": false}, "ShowPips": {"value": false},
	}}


func _garrison() -> Dictionary:
	return {"module": "GarrisonContain", "extraction": "typed", "fields": {
		"ObjectStatusOfContained": {"value": ["UNSELECTABLE"]}, "ContainMax": {"value": 2},
		"PassengerFilter": {"value": ["ANY", "+INFANTRY"]}, "AllowAlliesInside": {"value": true},
		"AllowEnemiesInside": {"value": false},
	}}


func _horde_garrison() -> Dictionary:
	return {"module": "HordeGarrisonContain", "extraction": "typed", "fields": {
		"ObjectStatusOfContained": {"value": ["UNSELECTABLE"]}, "ContainMax": {"value": 2},
		"DamagePercentToUnits": {"ratio": 0.0}, "PassengerFilter": {"value": ["ANY", "+INFANTRY"]},
		"AllowAlliesInside": {"value": true}, "AllowEnemiesInside": {"value": false},
		"EntryOffset": {"value": {"x": 0.0, "y": 0.0, "z": 0.0}},
		"ExitOffset": {"value": {"x": 0.0, "y": 0.0, "z": 0.0}}, "KillPassengersOnDeath": {"value": true},
	}}


func _sim() -> RetailSliceSim:
	var sim: RetailSliceSim = Sim.new()
	var unit_rules := {}
	for id in [Sim.SOLDIER_OBJECT_ID, Sim.SOLDIER_HORDE_ID, Sim.ARCHER_OBJECT_ID, Sim.TOWER_GUARD_OBJECT_ID, Sim.KNIGHT_OBJECT_ID]:
		unit_rules[id] = _rule()
	sim.setup({}, {"unit_rules": unit_rules})
	sim.ai_enabled = false
	sim.base_loop_enabled = false
	sim.winner = -1
	sim.entities.clear()
	sim.structures.clear()
	for id in range(1, 6):
		sim.entities[id] = _unit(id, 1 if id == 4 else 0, ["INFANTRY", "HERO"] if id == 3 else ["INFANTRY"])
	for id in [50, 60, 61, 70, 80]:
		sim.structures[id] = {"id": id, "team": 0, "health": 100, "position": Vector2(100, 0) if id == 61 else Vector2.ZERO}
	return sim


func _rule() -> Dictionary:
	return {"horde_id": Sim.SOLDIER_HORDE_ID, "category": "infantry", "speed": 1.0,
		"speed_source": 10.0, "acceleration": 1.0, "acceleration_source": 10.0,
		"turn_rate_degrees_per_second": 180.0, "braking": 1.0, "braking_source": 10.0,
		"attack_range": 1.0, "attack_range_source": 1.0, "minimum_attack_range": 0.0,
		"minimum_attack_range_source": 0.0, "vision_range": 10.0, "vision_range_source": 10.0,
		"delay_between_shots_ms": 1000.0, "pre_attack_delay_ms": 0.0, "firing_duration_ms": 0.0,
		"attack_period_ticks": 10, "pre_attack_ticks": 0, "firing_duration_ticks": 0,
		"member_damage": 1, "member_health": 100, "member_count": 1,
		"formation_positions": [Vector3.ZERO], "provenance": {}}


func _unit(id: int, team: int, kinds: Array) -> Dictionary:
	return {"id": id, "team": team, "health": 100, "maximum_health": 100, "member_health": [100],
		"position": Vector2.ZERO, "category": "infantry", "kind_of": kinds, "object_status": {}}


func _check(label: String, condition: bool) -> void:
	if condition:
		passed += 1
	else:
		failed += 1
		push_error("CONTAINER_FAMILY_RUNTIME_FAIL " + label)
