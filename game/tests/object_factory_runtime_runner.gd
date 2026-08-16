extends SceneTree

const Sim = preload("res://src/retail_slice/retail_slice_sim.gd")
const EXPECTED := 19
var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var sim := _sim()
	var owner := sim.structures[50] as Dictionary
	sim._attach_object_creation_upgrade_contract(owner, _creation())
	_check("creation_attaches", (owner.get("object_creation_upgrades", []) as Array).size() == 1)
	sim._step_object_creation_upgrades()
	_check("requires_all_blocks_partial", _creation_policy(owner).get("scheduled_tick", 0) == -1)
	(owner["completed_upgrades"] as Array).append("Upgrade_B")
	sim._step_object_creation_upgrades()
	_check("delay_scheduled", int(_creation_policy(owner).get("scheduled_tick", 0)) == 2)
	sim.tick()
	_check("creation_not_early", (_creation_policy(owner).get("spawned_ids", []) as Array).is_empty())
	sim.tick()
	var policy := _creation_policy(owner)
	_check("creation_spawns_once", (policy.get("spawned_ids", []) as Array).size() == 1)
	var spawned_id := int((policy.get("spawned_ids", []) as Array)[0])
	var spawned: Dictionary = sim.entity(spawned_id)
	_check("creation_ownership", int(spawned.get("team", -1)) == 0)
	_check("creation_offset", Vector2(spawned.get("position", Vector2.ZERO)).is_equal_approx(Vector2(2, 3)))
	_check("grant_upgrade_applied", (owner.get("completed_upgrades", []) as Array).has("Upgrade_Granted"))
	_check("remove_upgrade_applied", not (owner.get("completed_upgrades", []) as Array).has("Upgrade_A"))
	_check("creation_consumed_once", bool(policy.get("consumed", false)) and (policy.get("spawned_ids", []) as Array).size() == 1)

	var conflict := sim.structures[51] as Dictionary
	sim._attach_object_creation_upgrade_contract(conflict, _creation())
	conflict["completed_upgrades"] = ["Upgrade_A", "Upgrade_B", "Upgrade_Conflict"]
	for _index in 3:
		sim.tick_index += 1
		sim._step_object_creation_upgrades()
	_check("conflict_blocks_creation", (_creation_policy(conflict).get("spawned_ids", []) as Array).is_empty())
	var opaque := {}
	sim._attach_object_creation_upgrade_contract(opaque, {"module": "ObjectCreationUpgrade", "extraction": "opaque", "fields": {}})
	_check("opaque_creation_fails_closed", not opaque.has("object_creation_upgrades"))

	sim.register_ocl_leaf("OCL_Test", {"createObjects": [{"objects": [Sim.ARCHER_OBJECT_ID], "fields": []}]})
	var emitter := sim.structures[52] as Dictionary
	sim._attach_ocl_update_contract(emitter, _ocl())
	_check("ocl_attaches", emitter.has("ocl_update"))
	_check("ocl_exact_first_tick", int((emitter["ocl_update"] as Dictionary).get("next_tick", 0)) == sim.tick_index + 2)
	var before := sim.entity_ids().size()
	sim.tick_index += 1
	sim._step_ocl_updates()
	_check("ocl_not_early", sim.entity_ids().size() == before)
	sim.tick_index += 1
	sim._step_ocl_updates()
	_check("ocl_amount_emitted", sim.entity_ids().size() == before + 2)
	_check("ocl_rearms_exact", int((emitter["ocl_update"] as Dictionary).get("next_tick", 0)) == sim.tick_index + 2)

	var snap := sim.snapshot()
	var hash := sim.state_hash()
	var restored := _sim()
	restored.register_ocl_leaf("OCL_Test", {"createObjects": [{"objects": [Sim.ARCHER_OBJECT_ID], "fields": []}]})
	_check("snapshot_restore", restored.restore(snap))
	_check("hash_restore", restored.state_hash() == hash)
	print("OBJECT_FACTORY_RUNTIME_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 and passed == EXPECTED else 1)


func _creation_policy(owner: Dictionary) -> Dictionary:
	return (owner["object_creation_upgrades"] as Array)[0] as Dictionary


func _creation() -> Dictionary:
	return {"module": "ObjectCreationUpgrade", "extraction": "typed", "fields": {
		"TriggeredBy": {"value": ["Upgrade_A", "Upgrade_B"]},
		"RequiresAllTriggers": {"value": true},
		"ConflictsWith": {"value": ["Upgrade_Conflict"]},
		"Delay": {"milliseconds": 200},
		"Offset": {"value": {"x": 20.0, "y": 30.0, "z": 0.0}},
		"ThingToSpawn": {"value": Sim.SOLDIER_HORDE_ID},
		"GrantUpgrade": {"value": "Upgrade_Granted"},
		"RemoveUpgrade": {"value": "Upgrade_A"},
		"FadeInTime": {"milliseconds": 100},
	}}


func _ocl() -> Dictionary:
	return {"module": "OCLUpdate", "extraction": "typed", "fields": {
		"OCL": {"value": "OCL_Test"},
		"MinDelay": {"milliseconds": 200},
		"MaxDelay": {"milliseconds": 200},
		"Amount": {"value": 2},
	}}


func _sim() -> RetailSliceSim:
	var unit_rules := {}
	for id in [Sim.SOLDIER_OBJECT_ID, Sim.SOLDIER_HORDE_ID, Sim.ARCHER_OBJECT_ID, Sim.TOWER_GUARD_OBJECT_ID, Sim.KNIGHT_OBJECT_ID]:
		unit_rules[id] = _rule()
	var sim: RetailSliceSim = Sim.new()
	sim.setup({}, {"unit_rules": unit_rules, "source_unit_scale": 0.1})
	sim.ai_enabled = false
	sim.base_loop_enabled = false
	sim.entities.clear()
	sim.structures.clear()
	sim.team_resources[0] = 100
	# Keep both armies represented so the full tick integration path does not
	# resolve the deliberately tiny fixture as a completed match.
	sim.entities[19] = _unit(19, 0, Vector2(-100, 0))
	sim.entities[20] = _unit(20, 1, Vector2(100, 0))
	sim.structures[50] = {"id": 50, "team": 0, "health": 100, "position": Vector2.ZERO, "completed_upgrades": ["Upgrade_A"]}
	sim.structures[51] = {"id": 51, "team": 0, "health": 100, "position": Vector2(10, 0), "completed_upgrades": []}
	sim.structures[52] = {"id": 52, "team": 0, "health": 100, "position": Vector2(20, 0), "completed_upgrades": []}
	return sim


func _unit(id: int, team: int, position: Vector2) -> Dictionary:
	return {"id": id, "team": team, "health": 100, "maximum_health": 100,
		"member_maximum_health": 100, "member_health": [100], "position": position,
		"destination": position, "category": "infantry", "state": "idle", "target_id": 0,
		"target_kind": "battalion", "route": [], "route_cells": [], "current_speed": 0.0,
		"speed": 0.0, "attack_cooldown": 0, "attack_move": false, "order_kind": "",
		"auto_acquire_enabled": false, "timed_modifiers": {}, "completed_upgrades": []}


func _rule() -> Dictionary:
	return {
		"horde_id": Sim.SOLDIER_HORDE_ID, "category": "infantry",
		"speed": 1.0, "speed_source": 10.0, "acceleration": 1.0, "acceleration_source": 10.0,
		"turn_rate_degrees_per_second": 180.0, "braking": 1.0, "braking_source": 10.0,
		"attack_range": 1.0, "attack_range_source": 1.0,
		"minimum_attack_range": 0.0, "minimum_attack_range_source": 0.0,
		"vision_range": 10.0, "vision_range_source": 10.0,
		"delay_between_shots_ms": 1000.0, "pre_attack_delay_ms": 0.0, "firing_duration_ms": 0.0,
		"attack_period_ticks": 10, "pre_attack_ticks": 0, "firing_duration_ticks": 0,
		"member_damage": 1, "member_health": 100, "member_count": 1,
		"formation_positions": [Vector3.ZERO], "provenance": {},
	}


func _check(label: String, condition: bool) -> void:
	if condition:
		passed += 1
	else:
		failed += 1
		push_error("OBJECT_FACTORY_RUNTIME_FAIL " + label)
