extends SceneTree

const Sim = preload("res://src/retail_slice/retail_slice_sim.gd")
const EXPECTED := 21
var passed := 0
var failed := 0

func _initialize() -> void: call_deferred("_run")

func _run() -> void:
	var sim := _sim(_literal_contract(), true)
	var row := sim.entities[1] as Dictionary
	var policy := row.get("dual_weapon_behavior", {}) as Dictionary
	_check("DualWeaponBehavior_typed_contract_attaches", not policy.is_empty())
	_check("literal_contract_is_executable", bool(policy.get("executable", false)))
	_check("source_distance_is_exact", is_equal_approx(float(policy.get("switch_distance_source", 0.0)), 40.0))
	_check("source_distance_uses_map_scale", is_equal_approx(float(row.get("close_weapon_switch_distance", 0.0)), 4.0))
	_check("close_profile_is_existing_authored_mode", String(policy.get("close_weapon_mode", "")) == "close")
	_check("outside_threshold_uses_default", sim._weapon_mode_for_distance(row, 4.001) == "default")
	_check("exact_threshold_uses_close", sim._weapon_mode_for_distance(row, 4.0) == "close")
	_check("inside_threshold_uses_close", sim._weapon_mode_for_distance(row, 1.0) == "close")
	row["weapon_toggle_mode"] = "toggle"
	_check("player_toggle_overrides_distance", sim._weapon_mode_for_distance(row, 1.0) == "toggle")
	row["weapon_toggle_mode"] = ""
	_check("close_profile_applies_real_weapon_fields", sim._apply_weapon_mode(row, sim._weapon_mode_for_distance(row, 1.0)) and int(row.get("member_damage", 0)) == 30)
	var digest := sim.state_hash(); var snapshot := sim.snapshot(); var restored := _sim(_literal_contract(), true)
	_check("snapshot_restores", restored.restore(snapshot))
	_check("snapshot_hash_round_trips", restored.state_hash() == digest)
	_check("restored_active_mode_is_close", String((restored.entities[1] as Dictionary).get("active_weapon_mode", "")) == "close")
	var unresolved := _sim(_define_contract(), true); var unresolved_row := unresolved.entities[1] as Dictionary; var unresolved_policy := unresolved_row.get("dual_weapon_behavior", {}) as Dictionary
	_check("unresolved_define_is_not_executable", not bool(unresolved_policy.get("executable", true)))
	_check("unresolved_define_is_receipted", (unresolved_policy.get("unsupported_semantics", []) as Array).has("unresolved-switch-distance-define:DUAL_WEAPON_RANGE"))
	_check("unresolved_define_disables_legacy_guess", is_equal_approx(float(unresolved_row.get("close_weapon_switch_distance", -1.0)), 0.0))
	var missing_profile := _sim(_literal_contract(), false); var missing_row := missing_profile.entities[1] as Dictionary
	_check("missing_close_profile_fails_closed", bool(missing_row.get("unsupported_close_weapon", false)) and missing_profile._weapon_mode_for_distance(missing_row, 1.0) == "unsupported-close")
	_check("missing_close_profile_is_receipted", ((((missing_row.get("dual_weapon_behavior", {}) as Dictionary).get("unsupported_semantics", [])) as Array).has("close-weapon-profile-unresolved")))
	var opaque := _literal_contract(); opaque["extraction"] = "opaque"
	_check("opaque_contract_is_ignored", (_sim(opaque, true).entities[1] as Dictionary).get("dual_weapon_behavior", {}).is_empty())
	var malformed := _literal_contract(); (malformed["fields"] as Dictionary)["SwitchWeaponOnCloseRangeDistance"] = {"value": -1, "expression": "-1"}
	_check("negative_runtime_payload_fails_closed", (_sim(malformed, true).entities[1] as Dictionary).get("dual_weapon_behavior", {}).is_empty())
	_check("typed_provenance_is_preserved", String(policy.get("tag", "")) == "ModuleTag_DualWeapon" and int(policy.get("line", 0)) == 10)
	if passed + failed != EXPECTED: failed += 1; printerr("DUAL_WEAPON_BEHAVIOR_RUNTIME_FAIL liveness")
	print("DUAL_WEAPON_BEHAVIOR_RUNTIME_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)

func _literal_contract() -> Dictionary:
	return {"module": "DualWeaponBehavior", "runtimeStatus": "deferred", "extraction": "typed", "tag": "ModuleTag_DualWeapon", "sourceIni": "fixture/unit.ini", "line": 10, "fields": {"SwitchWeaponOnCloseRangeDistance": {"expression": "40", "value": 40, "sourceIni": "fixture/unit.ini", "line": 11}}}

func _define_contract() -> Dictionary:
	return {"module": "DualWeaponBehavior", "runtimeStatus": "deferred", "extraction": "typed", "tag": "ModuleTag_DualWeapon", "sourceIni": "fixture/unit.ini", "line": 10, "fields": {"SwitchWeaponOnCloseRangeDistance": {"expression": "DUAL_WEAPON_RANGE", "define": "DUAL_WEAPON_RANGE", "sourceIni": "fixture/unit.ini", "line": 11}}}

func _sim(contract: Dictionary, with_close: bool) -> RetailSliceSim:
	var default_weapon := {"name": "Default", "weapon_slot": "primary", "attack_range": 10.0, "attack_range_source": 100.0, "minimum_attack_range": 0.0, "minimum_attack_range_source": 0.0, "delay_between_shots_ms": 1000.0, "pre_attack_delay_ms": 0.0, "firing_duration_ms": 0.0, "attack_period_ticks": 10, "pre_attack_ticks": 0, "firing_duration_ticks": 0, "member_damage": 10}
	var modes := {"default": default_weapon, "toggle": default_weapon.duplicate(true)}
	if with_close:
		var close := default_weapon.duplicate(true); close["name"] = "Close"; close["member_damage"] = 30; close["attack_range"] = 2.0; modes["close"] = close
	var rule := {"horde_id": "FixtureDual", "category": "ranged-infantry", "speed": 1.0, "speed_source": 10.0, "acceleration": 1.0, "acceleration_source": 10.0, "turn_rate_degrees_per_second": 180.0, "braking": 1.0, "braking_source": 10.0, "attack_range": 10.0, "attack_range_source": 100.0, "minimum_attack_range": 0.0, "minimum_attack_range_source": 0.0, "vision_range": 1.0, "vision_range_source": 10.0, "delay_between_shots_ms": 1000.0, "pre_attack_delay_ms": 0.0, "firing_duration_ms": 0.0, "attack_period_ticks": 10, "pre_attack_ticks": 0, "firing_duration_ticks": 0, "member_damage": 10, "member_health": 100, "member_count": 1, "formation_positions": [Vector3.ZERO], "weapon_modes": modes, "default_weapon_mode": "default", "close_weapon_mode": "close" if with_close else "", "close_weapon_switch_distance": 99.0, "close_weapon_switch_distance_source": 990.0, "provenance": {}}
	var rules := {}; for id in [Sim.SOLDIER_OBJECT_ID, Sim.SOLDIER_HORDE_ID, Sim.ARCHER_OBJECT_ID, Sim.TOWER_GUARD_OBJECT_ID, Sim.KNIGHT_OBJECT_ID, "FixtureDual"]: rules[id] = rule
	var sim: RetailSliceSim = Sim.new(); sim.setup({}, {"unit_rules": rules, "source_map_transform_scale": 0.1}); sim.ai_enabled = false; sim.base_loop_enabled = false; sim.entities.clear(); sim.structures.clear(); sim._unit_module_contracts["FixtureDual"] = [contract]
	sim._add_battalion(1, Sim.PLAYER_TEAM, Vector2.ZERO, "Fixture", "FixtureDual", "FixtureDual", 0, rule)
	return sim

func _check(name: String, condition: bool) -> void:
	if condition: passed += 1
	else: failed += 1; push_error("DUAL_WEAPON_BEHAVIOR_RUNTIME_FAIL " + name)
