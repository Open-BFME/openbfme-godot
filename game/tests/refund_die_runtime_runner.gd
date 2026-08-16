extends SceneTree

const Sim = preload("res://src/retail_slice/retail_slice_sim.gd")
const EXPECTED := 24
var passed := 0
var failed := 0

func _initialize() -> void: call_deferred("_run")

func _run() -> void:
	var sim := _sim(_contract(50.0, "Upgrade_Defiance", ["ANY", "+GondorMarketPlace"]), true, true)
	var dying := sim.structures[1000] as Dictionary
	var policies := dying.get("refund_die", []) as Array
	var policy := policies[0] as Dictionary if not policies.is_empty() else {}
	_check("RefundDie_typed_contract_attaches", not policy.is_empty())
	_check("percentage_fraction_is_exact", is_equal_approx(float(policy.get("fraction", 0.0)), 0.5))
	_check("upgrade_condition_is_exact", String(policy.get("upgrade_required", "")) == "Upgrade_Defiance")
	_check("building_filter_tokens_are_exact", policy.get("building_required", []) == ["ANY", "+GondorMarketPlace"])
	_check("death_scope_is_explicit", String(policy.get("death_scope", "")) == "all-structure-deaths-module-defined")
	var before := sim.resources_for_team(Sim.PLAYER_TEAM)
	sim._apply_structure_death_refund(dying)
	_check("qualified_death_refunds_exact_percent", sim.resources_for_team(Sim.PLAYER_TEAM) == before + 300)
	_check("refund_event_emitted_once", _event_count(sim, "economy.refund") == 1)
	_check("policy_consumed_once", bool(policy.get("consumed", false)))
	sim._apply_structure_death_refund(dying)
	_check("repeated_death_callback_does_not_double_refund", sim.resources_for_team(Sim.PLAYER_TEAM) == before + 300 and _event_count(sim, "economy.refund") == 1)
	var digest := sim.state_hash(); var snapshot := sim.snapshot(); var restored := _sim(_contract(50.0, "Upgrade_Defiance", ["ANY", "+GondorMarketPlace"]), true, true)
	_check("snapshot_restores", restored.restore(snapshot))
	_check("snapshot_hash_round_trips", restored.state_hash() == digest)
	var restored_policies := (restored.structures[1000] as Dictionary).get("refund_die", []) as Array
	_check("consumed_state_restores", not restored_policies.is_empty() and bool((restored_policies[0] as Dictionary).get("consumed", false)))
	var no_upgrade := _sim(_contract(50.0, "Upgrade_Defiance", ["ANY", "+GondorMarketPlace"]), false, true); var no_upgrade_before := no_upgrade.resources_for_team(Sim.PLAYER_TEAM); no_upgrade._apply_structure_death_refund(no_upgrade.structures[1000] as Dictionary)
	_check("missing_upgrade_blocks_refund", no_upgrade.resources_for_team(Sim.PLAYER_TEAM) == no_upgrade_before)
	var no_upgrade_policies := (no_upgrade.structures[1000] as Dictionary).get("refund_die", []) as Array
	_check("failed_condition_does_not_consume", no_upgrade_policies.is_empty() or not bool((no_upgrade_policies[0] as Dictionary).get("consumed", false)))
	var no_building := _sim(_contract(50.0, "Upgrade_Defiance", ["ANY", "+GondorMarketPlace"]), true, false); var no_building_before := no_building.resources_for_team(Sim.PLAYER_TEAM); no_building._apply_structure_death_refund(no_building.structures[1000] as Dictionary)
	_check("missing_required_building_blocks_refund", no_building.resources_for_team(Sim.PLAYER_TEAM) == no_building_before)
	var unconditional := _sim(_contract(20.0, "", []), false, false); var unconditional_before := unconditional.resources_for_team(Sim.PLAYER_TEAM); unconditional._apply_structure_death_refund(unconditional.structures[1000] as Dictionary)
	_check("unconditional_retail_shape_refunds", unconditional.resources_for_team(Sim.PLAYER_TEAM) == unconditional_before + 120)
	var death_path := _sim(_contract(50.0, "Upgrade_Defiance", ["ANY", "+GondorMarketPlace"]), true, true); (death_path.structures[1000] as Dictionary)["health"] = 100; var death_before := death_path.resources_for_team(Sim.PLAYER_TEAM); death_path._apply_structure_damage(0, 1000, 1000, "SIEGE")
	_check("authoritative_damage_death_path_refunds", int((death_path.structures[1000] as Dictionary).get("health", -1)) == 0 and death_path.resources_for_team(Sim.PLAYER_TEAM) == death_before + 300)
	_check("damage_path_dispatches_single_refund", _event_count(death_path, "economy.refund") == 1)
	var opaque := _contract(50.0, "", []); opaque["extraction"] = "opaque"; var opaque_sim := _sim(opaque, false, false); var opaque_before := opaque_sim.resources_for_team(Sim.PLAYER_TEAM); opaque_sim._apply_structure_death_refund(opaque_sim.structures[1000] as Dictionary)
	_check("opaque_contract_fails_closed", opaque_sim.resources_for_team(Sim.PLAYER_TEAM) == opaque_before and not (opaque_sim.structures[1000] as Dictionary).has("refund_die"))
	var malformed := _contract(50.0, "", []); ((malformed["fields"] as Dictionary)["RefundPercent"] as Dictionary)["fraction"] = 1.5
	_check("malformed_fraction_fails_closed", not (_sim(malformed, false, false).structures[1000] as Dictionary).has("refund_die"))
	var missing_cost := _sim(_contract(50.0, "", []), false, false); missing_cost._structure_build_rules.erase("fixture"); var missing_before := missing_cost.resources_for_team(Sim.PLAYER_TEAM); missing_cost._apply_structure_death_refund(missing_cost.structures[1000] as Dictionary); var missing_policies := (missing_cost.structures[1000] as Dictionary).get("refund_die", []) as Array; var missing_policy := missing_policies[0] as Dictionary if not missing_policies.is_empty() else {}
	_check("missing_cost_fails_closed", missing_cost.resources_for_team(Sim.PLAYER_TEAM) == missing_before)
	_check("missing_cost_is_receipted", (missing_policy.get("unsupported_semantics", []) as Array).has("structure-build-cost-unresolved"))
	_check("missing_cost_does_not_consume", not bool(missing_policy.get("consumed", false)))
	_check("typed_provenance_is_preserved", String(policy.get("tag", "")) == "ModuleTag_refund" and int(policy.get("line", 0)) == 10)
	if passed + failed != EXPECTED: failed += 1; printerr("REFUND_DIE_RUNTIME_FAIL liveness")
	print("REFUND_DIE_RUNTIME_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)

func _contract(percent: float, upgrade: String, building_filter: Array) -> Dictionary:
	var fields := {"RefundPercent": {"authored": "%s%%" % percent, "percent": percent, "fraction": percent / 100.0, "sourceIni": "fixture/structure.ini", "line": 11}}
	if upgrade != "": fields["UpgradeRequired"] = {"authored": upgrade, "value": upgrade, "sourceIni": "fixture/structure.ini", "line": 12}
	if not building_filter.is_empty(): fields["BuildingRequired"] = {"authored": " ".join(building_filter), "value": building_filter.duplicate(), "sourceIni": "fixture/structure.ini", "line": 13}
	return {"module": "RefundDie", "runtimeStatus": "deferred", "extraction": "typed", "tag": "ModuleTag_refund", "sourceIni": "fixture/structure.ini", "line": 10, "fields": fields}

func _sim(contract: Dictionary, owns_upgrade: bool, has_required_building: bool) -> RetailSliceSim:
	var rule := {"horde_id": Sim.SOLDIER_HORDE_ID, "category": "infantry", "speed": 1.0, "speed_source": 10.0, "acceleration": 1.0, "acceleration_source": 10.0, "turn_rate_degrees_per_second": 180.0, "braking": 1.0, "braking_source": 10.0, "attack_range": 0.1, "attack_range_source": 1.0, "minimum_attack_range": 0.0, "minimum_attack_range_source": 0.0, "vision_range": 1.0, "vision_range_source": 10.0, "delay_between_shots_ms": 1000.0, "pre_attack_delay_ms": 0.0, "firing_duration_ms": 0.0, "attack_period_ticks": 10, "pre_attack_ticks": 0, "firing_duration_ticks": 0, "member_damage": 1, "member_health": 100, "member_count": 1, "formation_positions": [Vector3.ZERO], "provenance": {}}
	var rules := {}; for id in [Sim.SOLDIER_OBJECT_ID, Sim.SOLDIER_HORDE_ID, Sim.ARCHER_OBJECT_ID, Sim.TOWER_GUARD_OBJECT_ID, Sim.KNIGHT_OBJECT_ID]: rules[id] = rule
	var sim: RetailSliceSim = Sim.new(); sim.setup({}, {"unit_rules": rules}); sim.ai_enabled = false; sim.base_loop_enabled = false; sim.structures.clear(); sim.team_resources[Sim.PLAYER_TEAM] = 1000; sim._structure_build_rules["fixture"] = {"cost": 600}
	sim.register_structure_module_contracts("FixtureRefund", [contract]); sim.structures[1000] = {"id": 1000, "team": Sim.PLAYER_TEAM, "source_object_id": "FixtureRefund", "structure_kind": "fixture", "health": 0, "maximum_health": 100}; sim._attach_structure_module_contracts(sim.structures[1000] as Dictionary)
	if owns_upgrade: sim.team_upgrades[Sim.PLAYER_TEAM] = {"Upgrade_Defiance": true}
	if has_required_building: sim.structures[1001] = {"id": 1001, "team": Sim.PLAYER_TEAM, "source_object_id": "GondorMarketPlace", "structure_kind": "marketplace", "category": "structure", "kind_of": ["STRUCTURE", "GondorMarketPlace"], "health": 100, "maximum_health": 100}
	return sim

func _event_count(sim: RetailSliceSim, kind: String) -> int:
	var count := 0; for value in sim.events: if String((value as Dictionary).get("kind", "")) == kind: count += 1
	return count

func _check(name: String, condition: bool) -> void:
	if condition: passed += 1
	else: failed += 1; push_error("REFUND_DIE_RUNTIME_FAIL " + name)
