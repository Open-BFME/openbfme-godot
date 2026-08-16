extends SceneTree

const Adapter = preload("res://src/retail_slice/playable_unit_runtime_adapter.gd")
const Sim = preload("res://src/retail_slice/retail_slice_sim.gd")

var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	_test_adapter_projection_and_exact_unresolved_admission()
	_test_six_passive_animals()
	_test_weighted_mux_rng_and_snapshot()
	_test_presentation_receipt_without_emission()
	_test_blocked_rows_fail_closed()
	print("SLOW_DEATH_RUNTIME_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


func _test_adapter_projection_and_exact_unresolved_admission() -> void:
	var document := _scenario_document([_contract("ModuleTag_Wolf", "ALL", [], [], 10, 3000, 0.7, 8000)])
	var projected := Adapter.module_contracts(document)
	_check("adapter_projects_effect_graph", projected.size() == 1 and String((projected[0] as Dictionary).get("effect_graph", {}).get("executionEligibility", {}).get("status", "")) == "evidence-closed-core")
	_check("exact_slow_death_only_unresolved_scenario_is_admitted", not Adapter.simulation_rule(document, false).is_empty())
	var wrong_missing := document.duplicate(true)
	wrong_missing["registration"]["simulation"]["missing"] = ["combat.weapon"]
	_check("other_unresolved_simulation_stays_closed", Adapter.simulation_rule(wrong_missing, false).is_empty())
	var buildable := document.duplicate(true)
	buildable["registration"]["simulation"]["resolved"].erase("scenarioOnly")
	_check("unresolved_buildable_never_uses_scenario_exception", Adapter.simulation_rule(buildable, false).is_empty())


func _test_six_passive_animals() -> void:
	var authored := {
		"Cow": _passive_contracts("Cow", true, 0.4),
		"Crow": _passive_contracts("Crow", true, 0.4),
		"Dove_white_in_game": _passive_contracts("Dove_white_in_game", false, 0.4),
		"Goat": _passive_contracts("Goat", false, 0.4),
		"Sheep": _passive_contracts("Sheep", true, 0.4),
		"Wolf": _passive_contracts("Wolf", false, 0.7),
	}
	var sim := _sim(11)
	var next_id := 100
	for object_id_value in authored:
		var object_id := String(object_id_value)
		sim._unit_module_contracts[object_id] = authored[object_id]
		_add_dead_candidate(sim, next_id, object_id)
		var row := sim.entities[next_id] as Dictionary
		sim._attach_module_contracts(row)
		row["member_health"] = [0]
		row["health"] = 0
		sim._apply_playable_unit_death_policy(row, "NORMAL", [0])
		var state := row.get("slow_death_state", {}) as Dictionary
		_check("%s_core_attaches" % object_id, not state.is_empty())
		_check("%s_initial_and_exact_destruction" % object_id, state.get("executed_phases", []) == ["INITIAL"] and int(state.get("destruction_tick", -1)) == sim.tick_index + 80)
		_check("%s_midpoint_in_35_65_window" % object_id, int(state.get("midpoint_tick", -1)) >= sim.tick_index + 28 and int(state.get("midpoint_tick", -1)) <= sim.tick_index + 52)
		next_id += 1
	_check("six_animals_are_snapshot_hashed", sim.restore(sim.snapshot()) and sim.state_hash().length() == 64)
	var faded := _sim(19)
	faded._unit_module_contracts["Cow"] = authored["Cow"]
	_add_dead_candidate(faded, 199, "Cow")
	var faded_row := faded.entities[199] as Dictionary
	faded._attach_module_contracts(faded_row)
	faded_row["member_health"] = [0]
	faded_row["health"] = 0
	faded._apply_playable_unit_death_policy(faded_row, "FADED", [0])
	var faded_state := faded_row.get("slow_death_state", {}) as Dictionary
	_check("dual_row_passive_selects_exact_faded_module", String(faded_state.get("selected_tag", "")) == "ModuleTag_FadeoutDeath")
	_check("dual_row_passive_faded_destruction_is_4000ms", int(faded_state.get("destruction_tick", -1)) == faded.tick_index + 40)


func _test_weighted_mux_rng_and_snapshot() -> void:
	var expected := _sim(37)
	var expected_roll := expected.logic_random_int(1, 40)
	var expected_tag := "ModuleTag_Light" if expected_roll <= 10 else "ModuleTag_Heavy"
	var sim := _sim(37)
	sim._unit_module_contracts["Weighted"] = [
		_contract("ModuleTag_Light", "ALL", [], [], 10, 200, 1.0, 1000),
		_contract("ModuleTag_Heavy", "ALL", [], [], 30, 200, 2.0, 1000),
	]
	_add_dead_candidate(sim, 200, "Weighted")
	var row := sim.entities[200] as Dictionary
	sim._attach_module_contracts(row)
	row["member_health"] = [0]
	row["health"] = 0
	sim._apply_playable_unit_death_policy(row, "NORMAL", [0])
	var state := row.get("slow_death_state", {}) as Dictionary
	_check("weighted_choice_matches_retail_inclusive_roll", String(state.get("selected_tag", "")) == expected_tag)
	_check("selection_variances_and_midpoint_consume_four_draws", sim.logic_random_draws == 4)
	var depth_before := float(state.get("sink_depth_source", 0.0))
	sim.tick()
	_check("positive_sink_waits_for_authored_delay", is_equal_approx(float((sim.entities[200]["slow_death_state"] as Dictionary).get("sink_depth_source", 0.0)), depth_before))
	var snap := sim.snapshot()
	var hash := sim.state_hash()
	var restored := _sim(37)
	_check("slow_death_snapshot_restores", restored.restore(snap) and restored.state_hash() == hash)
	sim.tick()
	restored.tick()
	_check("positive_sink_advances_after_delay", float((sim.entities[200]["slow_death_state"] as Dictionary).get("sink_depth_source", 0.0)) > depth_before)
	_check("slow_death_hash_continues_after_restore", sim.state_hash() == restored.state_hash())
	sim.advance(8)
	restored.advance(8)
	_check("final_phase_precedes_exact_destruction", not sim.entities.has(200) and not restored.entities.has(200) and _event_count(sim, "slow_death.phase_receipt", "FINAL") == 1)


func _test_presentation_receipt_without_emission() -> void:
	var sim := _sim(5)
	var contract := _contract("ModuleTag_FX", "ALL", [], [], 10, 0, -2.0, 500)
	contract["fields"]["FX"] = [{"phase": "INITIAL", "references": ["FX_A", "FX_B"], "sourceIni": "fixture.ini", "line": 4}]
	contract["fields"]["Sound"] = [{"phase": "INITIAL", "references": ["WolfVoxDie"], "sourceIni": "fixture.ini", "line": 5}]
	sim._unit_module_contracts["Presentation"] = [contract]
	_add_dead_candidate(sim, 300, "Presentation")
	var row := sim.entities[300] as Dictionary
	sim._attach_module_contracts(row)
	row["member_health"] = [0]
	row["health"] = 0
	sim._apply_playable_unit_death_policy(row, "NORMAL", [0])
	var state := row.get("slow_death_state", {}) as Dictionary
	_check("negative_sink_is_preserved_but_never_moves", float(state.get("sink_rate_source_per_second", 0.0)) == -2.0)
	_check("presentation_is_receipted_not_emitted", (state.get("presentation_receipts", []) as Array).size() == 2 and (state.get("presentation_choices", []) as Array).size() == 1 and not _has_audio_or_fx_event(sim))
	_check("fx_choice_consumes_logic_draw_without_fabricating_fx", sim.logic_random_draws == 5)


func _test_blocked_rows_fail_closed() -> void:
	for blocker in ["OCL", "Weapon", "DoNotRandomizeMidpoint", "HIT_GROUND", "deferred"]:
		var sim := _sim(1)
		var contract := _contract("ModuleTag_Blocked", "ALL", [], [], 10, 0, 1.0, 1000)
		if blocker == "HIT_GROUND":
			contract["fields"]["FX"] = [{"phase": "HIT_GROUND", "references": ["FX_Bad"]}]
		elif blocker == "deferred":
			contract["effect_graph"]["executionEligibility"]["status"] = "deferred"
		else:
			contract["fields"][blocker] = {"value": true}
		sim._unit_module_contracts["Blocked"] = [contract]
		_add_dead_candidate(sim, 400, "Blocked")
		sim._attach_module_contracts(sim.entities[400] as Dictionary)
		_check("blocked_%s_fails_closed" % blocker, not (sim.entities[400] as Dictionary).has("slow_death_core_contracts"))
	var mux := _sim(3)
	mux._unit_module_contracts["Mux"] = [_contract("OnlyFaded", "NONE", ["FADED"], [], 10, 0, 1.0, 1000)]
	_add_dead_candidate(mux, 500, "Mux")
	var row := mux.entities[500] as Dictionary
	mux._attach_module_contracts(row)
	row["member_health"] = [0]
	row["health"] = 0
	mux._apply_playable_unit_death_policy(row, "NORMAL", [0])
	_check("nonmatching_death_type_uses_ordinary_corpse", not row.has("slow_death_state") and int(row.get("corpse_expire_tick", -1)) == mux.tick_index + Sim.CORPSE_LIFETIME_TICKS)


func _contract(tag: String, mode: String, included: Array, excluded: Array, weight: int, sink_delay_ms: int, sink_rate: float, destruction_ms: int) -> Dictionary:
	return {
		"module": "SlowDeathBehavior", "extraction": "typed", "runtimeStatus": "deferred",
		"tag": tag, "source_ini": "data/ini/object/neutral/fixture.ini", "line": 10,
		"effect_graph": {"executionEligibility": {"status": "evidence-closed-core", "blockers": []}},
		"fields": {
			"deathTypes": mode, "includedDeathTypes": included, "excludedDeathTypes": excluded,
			"ProbabilityModifier": {"value": weight}, "SinkDelay": {"milliseconds": sink_delay_ms},
			"SinkRate": {"value": sink_rate}, "DestructionDelay": {"milliseconds": destruction_ms},
		},
	}


func _passive_contracts(object_id: String, has_faded_row: bool, sink_rate: float) -> Array:
	var contracts: Array = []
	if has_faded_row:
		contracts.append(_contract("ModuleTag_FadeoutDeath", "NONE", ["FADED"], [], 10, 0, 0.0, 4000))
		contracts.append(_contract("ModuleTag_05", "ALL", [], ["FADED"], 10, 3000, sink_rate, 8000))
	else:
		contracts.append(_contract("ModuleTag_05", "ALL", [], [], 10, 3000, sink_rate, 8000))
	for contract_value in contracts:
		(contract_value as Dictionary)["source_ini"] = (
			"data/ini/object/nature/placeholdernatureunits.ini"
			if object_id == "Goat"
			else "data/ini/object/nature/natureunits.ini"
		)
	return contracts


func _sim(seed: int) -> RetailSliceSim:
	var rules := {}
	for object_id in [Sim.SOLDIER_OBJECT_ID, Sim.SOLDIER_HORDE_ID, Sim.ARCHER_OBJECT_ID, Sim.TOWER_GUARD_OBJECT_ID, Sim.KNIGHT_OBJECT_ID]:
		rules[object_id] = _unit_rule().duplicate(true)
	var sim: RetailSliceSim = Sim.new()
	sim.setup({}, {"unit_rules": rules, "logic_random_seed": seed})
	sim.ai_enabled = false
	sim.base_loop_enabled = false
	sim.entities.clear()
	sim.structures.clear()
	return sim


func _add_dead_candidate(sim: RetailSliceSim, id: int, unit_type: String) -> void:
	sim.entities[id] = {
		"id": id, "unit_type": unit_type, "object_id": unit_type, "team": 0,
		"health": 1, "member_health": [1], "member_corpse_expire_ticks": [-1],
		"corpse_expire_tick": -1, "position": Vector2.ZERO, "target_id": 0,
		"state": "idle", "category": "creature", "command_points": 0,
	}


func _scenario_document(contracts: Array) -> Dictionary:
	var source_contracts: Array = []
	for contract_value in contracts:
		var contract := (contract_value as Dictionary).duplicate(true)
		contract["effectGraph"] = contract.get("effect_graph", {})
		contract.erase("effect_graph")
		contract["sourceIni"] = contract.get("source_ini", "")
		contract.erase("source_ini")
		source_contracts.append(contract)
	return {"objectId": "Wolf", "category": "creature", "registration": {
		"simulation": {"status": "unresolved", "missing": ["moduleContracts.SlowDeathBehavior"], "resolved": {
			"displayNameId": {"value": "OBJECT:Wolf"}, "memberCount": {"value": 1},
			"memberHealth": {"value": 1}, "speed": {"value": 65.0}, "visionRange": {"value": 100.0},
			"combat": {"disposition": "noncombatant"}, "movement": {"locomotorId": "WolfWalk"},
			"scenarioOnly": {"value": true, "disposition": "explicit-scenario-admission"},
			"moduleContracts": source_contracts,
		}},
	}}


func _unit_rule() -> Dictionary:
	return {"horde_id": Sim.SOLDIER_HORDE_ID, "category": "infantry", "speed": 1.0, "speed_source": 10.0, "acceleration": 1.0, "acceleration_source": 10.0, "turn_rate_degrees_per_second": 180.0, "braking": 1.0, "braking_source": 10.0, "attack_range": 0.1, "attack_range_source": 1.0, "minimum_attack_range": 0.0, "minimum_attack_range_source": 0.0, "vision_range": 1.0, "vision_range_source": 10.0, "delay_between_shots_ms": 1000.0, "pre_attack_delay_ms": 0.0, "firing_duration_ms": 0.0, "attack_period_ticks": 10, "pre_attack_ticks": 0, "firing_duration_ticks": 0, "member_damage": 1, "member_health": 100, "member_count": 1, "formation_positions": [Vector3.ZERO], "provenance": {}}


func _event_count(sim: RetailSliceSim, kind: String, phase: String) -> int:
	var count := 0
	for event_value in sim.events:
		var event := event_value as Dictionary
		if String(event.get("kind", "")) == kind and String(event.get("phase", "")) == phase:
			count += 1
	return count


func _has_audio_or_fx_event(sim: RetailSliceSim) -> bool:
	for event_value in sim.events:
		var kind := String((event_value as Dictionary).get("kind", "")).to_lower()
		if kind.contains("audio") or kind.contains("sound") or kind.contains("fx"):
			return true
	return false


func _check(label: String, condition: bool) -> void:
	if condition:
		passed += 1
	else:
		failed += 1
		printerr("SLOW_DEATH_RUNTIME_FAIL %s" % label)
