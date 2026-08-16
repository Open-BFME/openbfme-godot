extends SceneTree

const Adapter = preload("res://src/retail_slice/playable_unit_runtime_adapter.gd")
const Sim = preload("res://src/retail_slice/retail_slice_sim.gd")
const EXPECTED_CHECKS := 23

var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var contract := _typed_contract()
	var projected := Adapter.module_contracts(_document(contract))
	_check("adapter_projects_typed_contract", projected.size() == 1)
	_check("typed_contract_remains_honestly_deferred", not bool((projected[0] as Dictionary).get("executable", true)))

	var sim := _make_sim()
	_check("resolved_death_weapon_rule_registers", sim.register_death_weapon_rule("FixtureDeathWeapon", {
		"damage": 40.0,
		"radius_source": 50.0,
		"damage_type": "CRUSH",
		"affects": "ENEMIES",
	}))
	sim._unit_module_contracts[Sim.SOLDIER_HORDE_ID] = projected
	_spawn(sim, 1, Sim.PLAYER_TEAM, Vector2.ZERO)
	_spawn(sim, 2, Sim.PLAYER_TEAM, Vector2(50.0, 50.0)) # survivor keeps match live
	_spawn(sim, 90, Sim.ENEMY_TEAM, Vector2(1.0, -2.0))
	_spawn(sim, 91, Sim.ENEMY_TEAM, Vector2(50.0, -50.0))
	(sim.entities[1] as Dictionary)["object_status"] = {"DEATH_1": true, "DEPLOYED": true}
	_check("spawn_attaches_typed_death_weapon", ((sim.entities[1] as Dictionary).get("fire_weapon_when_dead", []) as Array).size() == 1)
	sim._attach_module_contracts(sim.entities[1] as Dictionary)
	_check("repeated_module_attachment_is_idempotent", ((sim.entities[1] as Dictionary).get("fire_weapon_when_dead", []) as Array).size() == 1)
	sim._apply_member_damage(90, 0, 1, 999, "battalion", 1, 0, "", "CRUSHED")
	_check("matching_death_and_required_status_schedule_once", sim._pending_power_effects.size() == 1)
	var scheduled: Dictionary = sim._pending_power_effects[0]
	_check("authored_delay_converts_to_three_ticks", int(scheduled.get("fire_tick", -1)) == sim.tick_index + 3)
	_check("local_xy_offset_scales_into_world_point", Vector2(scheduled.get("point", Vector2.ZERO)).is_equal_approx(Vector2(1.0, -2.0)))
	_check("vertical_weapon_offset_is_preserved", float(scheduled.get("height_source", 0.0)) == 3.0)
	sim._apply_playable_unit_death_policy(sim.entities[1] as Dictionary, "CRUSHED", [])
	_check("repeat_death_callback_cannot_schedule_twice", sim._pending_power_effects.size() == 1)

	var snapshot := sim.snapshot()
	var snapshot_hash := sim.state_hash()
	var restored := _make_sim()
	_check("scheduled_weapon_snapshot_restores", restored.restore(snapshot))
	_check("scheduled_weapon_snapshot_hash_round_trips", restored.state_hash() == snapshot_hash)
	sim.advance(2)
	restored.advance(2)
	_check("weapon_does_not_fire_before_authored_delay", int((sim.entities[90] as Dictionary).get("health", 0)) == 100)
	sim.advance(1)
	restored.advance(1)
	_check("resolved_death_weapon_applies_radial_damage", int((sim.entities[90] as Dictionary).get("health", 0)) == 60)
	if restored.state_hash() != sim.state_hash():
		_print_state_diff(sim._authoritative_state(), restored._authoritative_state())
	_check("restored_schedule_fires_identically", restored.state_hash() == sim.state_hash())
	_check("death_weapon_schedule_consumed_once", sim._pending_power_effects.is_empty())

	var mismatch := _make_sim()
	mismatch._unit_module_contracts[Sim.SOLDIER_HORDE_ID] = projected
	_spawn(mismatch, 10, Sim.PLAYER_TEAM, Vector2.ZERO)
	(mismatch.entities[10] as Dictionary)["object_status"] = {"DEATH_1": true, "DEPLOYED": true}
	mismatch._apply_member_damage(0, 0, 10, 999, "battalion", 1, 0, "", "NORMAL")
	_check("death_types_none_plus_crushed_rejects_normal", mismatch._pending_power_effects.is_empty())

	var exempt := _make_sim()
	exempt._unit_module_contracts[Sim.SOLDIER_HORDE_ID] = projected
	_spawn(exempt, 20, Sim.PLAYER_TEAM, Vector2.ZERO)
	(exempt.entities[20] as Dictionary)["object_status"] = {"DEATH_1": true, "DEPLOYED": true, "SOLD": true}
	exempt._apply_member_damage(0, 0, 20, 999, "battalion", 1, 0, "", "CRUSHED")
	_check("exempt_status_suppresses_schedule", exempt._pending_power_effects.is_empty())

	var missing_required := _make_sim()
	missing_required._unit_module_contracts[Sim.SOLDIER_HORDE_ID] = projected
	_spawn(missing_required, 30, Sim.PLAYER_TEAM, Vector2.ZERO)
	(missing_required.entities[30] as Dictionary)["object_status"] = {"DEATH_1": true}
	missing_required._apply_member_damage(0, 0, 30, 999, "battalion", 1, 0, "", "CRUSHED")
	_check("all_required_status_tokens_must_be_present", missing_required._pending_power_effects.is_empty())

	var last_carrier := _make_sim()
	last_carrier._unit_module_contracts[Sim.SOLDIER_HORDE_ID] = projected
	_spawn(last_carrier, 40, Sim.PLAYER_TEAM, Vector2.ZERO)
	_spawn(last_carrier, 41, Sim.ENEMY_TEAM, Vector2(20.0, 0.0))
	(last_carrier.entities[40] as Dictionary)["object_status"] = {"DEATH_1": true, "DEPLOYED": true}
	last_carrier._apply_member_damage(41, 0, 40, 999, "battalion", 1, 0, "", "CRUSHED")
	last_carrier.advance(1)
	_check("pending_death_weapon_defers_match_victory", last_carrier.winner == -1)

	var construction_blocked := _make_sim()
	construction_blocked.register_structure_module_contracts(
		"FixtureFortress", Adapter.module_contracts(_document(_structure_contract(false)))
	)
	_add_structure(construction_blocked, 100, 0.5)
	construction_blocked._apply_structure_damage(0, 100, 999)
	_check("active_during_construction_false_suppresses_structure_weapon", construction_blocked._pending_power_effects.is_empty())
	var construction_active := _make_sim()
	construction_active.register_structure_module_contracts(
		"FixtureFortress", Adapter.module_contracts(_document(_structure_contract(true)))
	)
	_add_structure(construction_active, 101, 0.5)
	construction_active._apply_structure_damage(0, 101, 999)
	_check("active_during_construction_true_schedules_structure_weapon", construction_active._pending_power_effects.size() == 1)
	construction_active._step_pending_power_effects()
	_check("unresolved_structure_weapon_fails_closed_and_consumes_schedule", construction_active._pending_power_effects.is_empty())

	var ran := passed + failed
	if ran != EXPECTED_CHECKS:
		failed += 1
		printerr("FIRE_WEAPON_WHEN_DEAD_FAIL liveness ran=%d expected=%d" % [ran, EXPECTED_CHECKS])
	print("FIRE_WEAPON_WHEN_DEAD_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


func _typed_contract() -> Dictionary:
	return {
		"module": "FireWeaponWhenDeadBehavior",
		"fields": {
			"deathTypes": "NONE",
			"includedDeathTypes": ["CRUSHED"],
			"excludedDeathTypes": [],
			"RequiredStatus": {"authored": "DEATH_1 DEPLOYED", "value": ["DEATH_1", "DEPLOYED"]},
			"ExemptStatus": {"authored": "SOLD", "value": ["SOLD"]},
			"StartsActive": {"authored": "Yes", "value": true},
			"ActiveDuringConstruction": {"authored": "No", "value": false},
			"DelayTime": {"authored": "300", "milliseconds": 300.0},
			"DeathWeapon": {"authored": "FixtureDeathWeapon", "value": "FixtureDeathWeapon"},
			"WeaponOffset": {"authored": "X:10 Y:-20 Z:3", "value": {"x": 10.0, "y": -20.0, "z": 3.0}},
		},
		"runtimeStatus": "deferred",
		"extraction": "typed",
		"carrier": "Behavior",
		"sourceIni": "data/ini/object/fixture.ini",
		"line": 42,
		"tag": "ModuleTag_DeathWeapon",
	}


func _document(contract: Dictionary) -> Dictionary:
	return {"registration": {"simulation": {"resolved": {"moduleContracts": [contract]}}}}


func _structure_contract(active_during_construction: bool) -> Dictionary:
	var contract := _typed_contract()
	var fields: Dictionary = contract.get("fields", {}) as Dictionary
	fields["deathTypes"] = "ALL"
	fields["includedDeathTypes"] = []
	fields["RequiredStatus"] = {"authored": "", "value": []}
	fields["ExemptStatus"] = {"authored": "", "value": []}
	fields["ActiveDuringConstruction"] = {
		"authored": "Yes" if active_during_construction else "No",
		"value": active_during_construction,
	}
	fields["DelayTime"] = {"authored": "0", "milliseconds": 0.0}
	contract["fields"] = fields
	return contract


func _add_structure(sim: RetailSliceSim, id: int, construction: float) -> void:
	sim.structures[id] = {
		"id": id, "team": Sim.PLAYER_TEAM, "kind": "structure",
		"structure_kind": "fortress", "source_object_id": "FixtureFortress",
		"position": Vector2.ZERO, "health": 10, "maximum_health": 10,
		"construction_progress": construction, "completed_upgrades": [],
		"queue": [], "upgrade_queue": [], "damage_remainders": {},
	}


func _make_sim() -> RetailSliceSim:
	var rule := _unit_rule()
	var rules := {}
	for object_id in [Sim.SOLDIER_OBJECT_ID, Sim.SOLDIER_HORDE_ID, Sim.ARCHER_OBJECT_ID, Sim.TOWER_GUARD_OBJECT_ID, Sim.KNIGHT_OBJECT_ID]:
		rules[object_id] = rule.duplicate(true)
	var sim: RetailSliceSim = Sim.new()
	sim.setup({}, {
		"unit_rules": rules,
		"source_map_transform_scale": 0.1,
		"faction_manifest": {"structure_armor": _fixture_structure_armor()},
	})
	sim.ai_enabled = false
	sim.base_loop_enabled = false
	sim.entities.clear()
	sim.structures.clear()
	return sim


func _spawn(sim: RetailSliceSim, id: int, team: int, at: Vector2) -> void:
	sim._add_battalion(id, team, at, "Fixture", Sim.SOLDIER_OBJECT_ID, Sim.SOLDIER_HORDE_ID, 0, _unit_rule())


func _unit_rule() -> Dictionary:
	return {
		"horde_id": Sim.SOLDIER_HORDE_ID, "category": "infantry",
		"speed": 1.0, "speed_source": 10.0,
		"acceleration": 1.0, "acceleration_source": 10.0,
		"turn_rate_degrees_per_second": 180.0,
		"braking": 1.0, "braking_source": 10.0,
		"attack_range": 0.1, "attack_range_source": 1.0,
		"minimum_attack_range": 0.0, "minimum_attack_range_source": 0.0,
		"vision_range": 1.0, "vision_range_source": 10.0,
		"delay_between_shots_ms": 1000.0, "pre_attack_delay_ms": 0.0,
		"firing_duration_ms": 0.0, "attack_period_ticks": 10,
		"pre_attack_ticks": 0, "firing_duration_ticks": 0,
		"member_damage": 1, "member_health": 100, "member_count": 1,
		"formation_positions": [Vector3.ZERO], "provenance": {},
	}


func _fixture_structure_armor() -> Dictionary:
	var armor := {}
	for kind_value in Sim.STRUCTURE_KINDS:
		armor[String(kind_value)] = {"set_id": "FixtureArmor", "damage_scalar": 1.0, "scalars": {"default": 1.0}}
	return armor


func _check(label: String, condition: bool) -> void:
	if condition:
		passed += 1
	else:
		failed += 1
		push_error("FIRE_WEAPON_WHEN_DEAD_FAIL %s" % label)


func _print_state_diff(expected: Variant, actual: Variant, path: String = "state", depth: int = 0) -> void:
	if depth > 7 or typeof(expected) != typeof(actual):
		printerr("FIRE_WEAPON_WHEN_DEAD_STATE_DIFF %s expected=%s actual=%s" % [path, var_to_str(expected), var_to_str(actual)])
		return
	if typeof(expected) == TYPE_DICTIONARY:
		var keys: Array = (expected as Dictionary).keys()
		for key_value in (actual as Dictionary).keys():
			if key_value not in keys:
				keys.append(key_value)
		keys.sort_custom(func(a: Variant, b: Variant) -> bool: return str(a) < str(b))
		for key_value in keys:
			var has_expected := (expected as Dictionary).has(key_value)
			var has_actual := (actual as Dictionary).has(key_value)
			if not has_expected or not has_actual:
				printerr("FIRE_WEAPON_WHEN_DEAD_STATE_DIFF %s.%s presence expected=%s actual=%s" % [path, str(key_value), has_expected, has_actual])
				continue
			var expected_value: Variant = (expected as Dictionary)[key_value]
			var actual_value: Variant = (actual as Dictionary)[key_value]
			if expected_value != actual_value:
				_print_state_diff(expected_value, actual_value, "%s.%s" % [path, str(key_value)], depth + 1)
		return
	if typeof(expected) == TYPE_ARRAY:
		if (expected as Array).size() != (actual as Array).size():
			printerr("FIRE_WEAPON_WHEN_DEAD_STATE_DIFF %s.size expected=%d actual=%d" % [path, (expected as Array).size(), (actual as Array).size()])
			return
		for index in (expected as Array).size():
			if (expected as Array)[index] != (actual as Array)[index]:
				_print_state_diff((expected as Array)[index], (actual as Array)[index], "%s[%d]" % [path, index], depth + 1)
		return
	if expected != actual:
		printerr("FIRE_WEAPON_WHEN_DEAD_STATE_DIFF %s expected=%s actual=%s" % [path, var_to_str(expected), var_to_str(actual)])
