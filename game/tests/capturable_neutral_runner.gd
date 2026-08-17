extends SceneTree
## Neutral capturable camps: CaptureFlag is CAPTURABLE; Inn/Outpost follow
## LINKED_TO_FLAG. Infantry with Command_CaptureBuilding can take them.

const SimScript = preload("res://src/retail_slice/retail_slice_sim.gd")

const EXPECTED_CHECKS := 13
const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
var _watchdog := RunnerWatchdogScript.new()
var passed := 0
var failed := 0


func _initialize() -> void:
	_watchdog.start(self, "CAPTURABLE_NEUTRAL")
	call_deferred("_run")


func _run() -> void:
	_test_off_flag_is_inert()
	_test_seed_pairs_flag_to_inn()
	_test_infantry_capture_transfers_linked_inn()
	_test_uncapturable_inn_is_not_a_direct_target()
	_test_scenario_registry_does_not_steal_capturable_sources()
	_finish()


func _check(label: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("CAPTURABLE_NEUTRAL PASS %s" % label)
	else:
		failed += 1
		printerr("CAPTURABLE_NEUTRAL FAIL %s%s" % [label, "" if detail == "" else " (%s)" % detail])


func _config() -> Dictionary:
	return {
		"source_map_configured": true,
		"capturable_placements": [
			{
				"type_name": "CaptureFlag",
				"structure_kind": "capture_flag",
				"source_index": 2,
				"position": Vector2(10.0, 0.0),
				"yaw": 0.0,
				"capturable": true,
				"linked_to_flag": false,
				"unattackable": true,
				"maximum_health": 1,
			},
			{
				"type_name": "Inn",
				"structure_kind": "inn",
				"source_index": 3,
				"position": Vector2(12.0, 0.5),
				"yaw": 0.0,
				"capturable": false,
				"linked_to_flag": true,
				"unattackable": false,
				"maximum_health": 3000,
			},
		],
	}


func _rules(enable: bool) -> Dictionary:
	return {
		"enable_base_loop": false,
		"spawn_initial_battalions": false,
		"enable_capturable_neutrals": enable,
		"source_map_transform_scale": 1.0,
		"faction_manifest": {
			"spawn_roster": [],
			"structure_armor": _fixture_structure_armor(),
		},
		"unit_rules": {
			"test.fighter": {
				"horde_id": "test.fighter",
				"member_count": 1,
				"member_health": 100,
				"member_damage": 10,
				"speed": 20.0,
				"speed_source": 20.0,
				"acceleration": 20.0,
				"acceleration_source": 20.0,
				"turn_rate_degrees_per_second": 180.0,
				"braking": 20.0,
				"braking_source": 20.0,
				"attack_range": 20.0,
				"attack_range_source": 20.0,
				"minimum_attack_range": 0.0,
				"minimum_attack_range_source": 0.0,
				"vision_range": 40.0,
				"vision_range_source": 40.0,
				"delay_between_shots_ms": 600.0,
				"pre_attack_delay_ms": 200.0,
				"firing_duration_ms": 200.0,
				"attack_period_ticks": 10,
				"pre_attack_ticks": 2,
				"firing_duration_ticks": 2,
				"formation_positions": [Vector3.ZERO],
				"provenance": {},
				"category": "infantry",
			},
		},
	}


func _fixture_structure_armor() -> Dictionary:
	var armor := {}
	for kind_value in SimScript.STRUCTURE_KINDS:
		armor[String(kind_value)] = {
			"set_id": "FixtureArmor",
			"damage_scalar": 1.0,
			"scalars": {"default": 1.0},
		}
	for kind in ["capture_flag", "inn", "outpost", "signal_fire"]:
		armor[kind] = {
			"set_id": "FixtureArmor-%s" % kind,
			"damage_scalar": 1.0,
			"scalars": {"default": 1.0},
		}
	return armor


func _make_sim(enable: bool):
	var sim = SimScript.new()
	sim.setup(_config(), _rules(enable))
	sim.ai_enabled = false
	return sim


func _flag_and_inn(sim) -> Dictionary:
	var flag_id := 0
	var inn_id := 0
	for id in sim.structure_ids():
		var row: Dictionary = sim.structures[id]
		if String(row.get("structure_kind", "")) == "capture_flag":
			flag_id = id
		elif String(row.get("structure_kind", "")) == "inn":
			inn_id = id
	return {"flag": flag_id, "inn": inn_id}


func _test_off_flag_is_inert() -> void:
	var sim = _make_sim(false)
	var ids := _flag_and_inn(sim)
	_check("off_flag_seeds_no_camps", int(ids.get("flag", -1)) == 0 and int(ids.get("inn", -1)) == 0)


func _test_seed_pairs_flag_to_inn() -> void:
	var sim = _make_sim(true)
	var ids := _flag_and_inn(sim)
	_check("seeds_flag_and_inn", int(ids["flag"]) != 0 and int(ids["inn"]) != 0)
	var flag: Dictionary = sim.structures[int(ids["flag"])]
	var inn: Dictionary = sim.structures[int(ids["inn"])]
	_check("flag_is_neutral_and_capturable", int(flag.get("team", -1)) == SimScript.NEUTRAL_TEAM and bool(flag.get("capturable", false)))
	_check("inn_is_linked_not_directly_capturable", bool(inn.get("linked_to_flag", false)) and not bool(inn.get("capturable", false)))
	_check("flag_pairs_nearest_inn", int(flag.get("linked_structure_id", 0)) == int(ids["inn"]) and int(inn.get("linked_structure_id", 0)) == int(ids["flag"]))


func _test_infantry_capture_transfers_linked_inn() -> void:
	var sim = _make_sim(true)
	var ids := _flag_and_inn(sim)
	sim._unit_ability_rules["test.fighter"] = sim._scaled_ability_rules([{
		"ability_id": "Command_CaptureBuilding",
		"slot": 12,
		"targeting": "enemy-object",
		"cooldown_ticks": 0,
		"required_level": 1,
		"level_gate_resolved": true,
		"castable": true,
		"effect": {
			"kind": "capture-building",
			"startAbilityRange": 15.0,
			"unpackMs": 1.0,
			"preparationMs": 100.0,
			"packMs": 1.0,
		},
	}], 1.0)
	sim._add_battalion(21, 0, Vector2(10.0, 0.0), "Fighter", "test.fighter", "test.fighter", 0)
	var cast: Dictionary = sim.cast_ability(21, "Command_CaptureBuilding", Vector2(10.0, 0.0))
	_check("infantry_can_start_capture", bool(cast.get("ok", false)), str(cast))
	sim.advance(3)
	var flag: Dictionary = sim.structures[int(ids["flag"])]
	var inn: Dictionary = sim.structures[int(ids["inn"])]
	_check("flag_and_inn_flip_to_capturing_team", int(flag.get("team", -1)) == 0 and int(inn.get("team", -1)) == 0, "flag=%s inn=%s" % [str(flag.get("team")), str(inn.get("team"))])


func _test_uncapturable_inn_is_not_a_direct_target() -> void:
	var sim = _make_sim(true)
	var ids := _flag_and_inn(sim)
	sim._unit_ability_rules["test.fighter"] = sim._scaled_ability_rules([{
		"ability_id": "Command_CaptureBuilding",
		"slot": 12,
		"targeting": "enemy-object",
		"cooldown_ticks": 0,
		"required_level": 1,
		"level_gate_resolved": true,
		"castable": true,
		"effect": {
			"kind": "capture-building",
			"startAbilityRange": 15.0,
			"unpackMs": 1.0,
			"preparationMs": 100.0,
			"packMs": 1.0,
		},
	}], 1.0)
	sim._add_battalion(22, 0, Vector2(12.0, 0.5), "Fighter", "test.fighter", "test.fighter", 0)
	var inn: Dictionary = sim.structures[int(ids["inn"])]
	var cast: Dictionary = sim.cast_ability(22, "Command_CaptureBuilding", Vector2(inn.get("position", Vector2.ZERO)))
	_check(
		"inn_is_not_a_direct_capture_target",
		not bool(cast.get("ok", false)) and String(cast.get("reason", "")) == "no-capturable-structure",
		str(cast)
	)


func _test_scenario_registry_does_not_steal_capturable_sources() -> void:
	var config := _config()
	config["scenario_object_placements"] = [
		{"type_name": "CaptureFlag", "source_index": 2, "position": Vector2(10.0, 0.0), "source_position": Vector3(10.0, 0.0, 0.0), "yaw": 0.0, "properties": {}},
		{"type_name": "Inn", "source_index": 3, "position": Vector2(12.0, 0.5), "source_position": Vector3(12.0, 0.5, 0.0), "yaw": 0.0, "properties": {}},
	]
	var rules := _rules(true)
	var fixture_armor := (rules.get("faction_manifest", {}) as Dictionary).get("structure_armor", {}) as Dictionary
	fixture_armor.erase("capture_flag")
	fixture_armor.erase("inn")
	rules["game"] = "rotwk"
	rules["enable_scenario_map_placements"] = true
	rules["scenario_structure_runtimes"] = {
		"CaptureFlag": _scenario_structure("CaptureFlag", "captureflag"),
		"Inn": _scenario_structure("Inn", "inn"),
	}
	var sim = SimScript.new()
	sim.setup({}, rules)
	sim.ai_enabled = false
	sim.structures.clear()
	sim._scenario_map_seeded_source_indices.clear()
	sim._scenario_map_placements = (config.get("scenario_object_placements", []) as Array).duplicate(true)
	sim._capturable_placements = (config.get("capturable_placements", []) as Array).duplicate(true)
	sim._next_scenario_structure_id = SimScript.SCENARIO_STRUCTURE_FIRST_ID
	sim._next_capturable_structure_id = SimScript.CAPTURABLE_NEUTRAL_FIRST_ID
	sim._seed_scenario_map_placements()
	sim._seed_capturable_neutrals()
	var ids := _flag_and_inn(sim)
	var flag: Dictionary = sim.structures.get(int(ids.get("flag", 0)), {}) as Dictionary
	var inn: Dictionary = sim.structures.get(int(ids.get("inn", 0)), {}) as Dictionary
	_check("scenario_overlap_keeps_exactly_one_flag_and_inn", sim.structures.size() == 2 and not flag.is_empty() and not inn.is_empty(), str(sim.structures))
	_check("scenario_overlap_preserves_neutral_capturable_flag", int(flag.get("team", -1)) == SimScript.NEUTRAL_TEAM and bool(flag.get("capturable", false)))
	_check("scenario_overlap_preserves_inn_linkage", int(flag.get("linked_structure_id", 0)) == int(ids.get("inn", 0)) and int(inn.get("linked_structure_id", 0)) == int(ids.get("flag", 0)))
	_check("scenario_overlap_uses_descriptor_capture_armor", String((sim._structure_armor.get("capture_flag", {}) as Dictionary).get("set_id", "")) == "")
	sim._unit_ability_rules["test.fighter"] = sim._scaled_ability_rules([{
		"ability_id": "Command_CaptureBuilding", "slot": 12, "targeting": "enemy-object", "cooldown_ticks": 0,
		"required_level": 1, "level_gate_resolved": true, "castable": true,
		"effect": {"kind": "capture-building", "startAbilityRange": 15.0, "unpackMs": 1.0, "preparationMs": 100.0, "packMs": 1.0},
	}], 1.0)
	sim._add_battalion(23, 0, Vector2(10.0, 0.0), "Fighter", "test.fighter", "test.fighter", 0)
	var cast := sim.cast_ability(23, "Command_CaptureBuilding", Vector2(10.0, 0.0))
	sim.advance(3)
	_check("scenario_overlap_capture_transfers_linked_pair", bool(cast.get("ok", false)) and int(flag.get("team", -1)) == 0 and int(inn.get("team", -1)) == 0, str(cast))


func _scenario_structure(object_id: String, slug: String) -> Dictionary:
	return {
		"objectId": object_id,
		"slug": slug,
		"registration": {
			"production": {"evidence": "authored-neutral-map", "routes": []},
			"scenarioAdmission": {"kind": "authored-neutral-non-buildable", "role": "neutral-structure", "surfaces": ["map-placement"], "buildCommandExposed": false},
			"gameplay": {
				"armor": {"setId": null, "semantic": "fixture explicitly authors unmodified damage"},
				"health": {"primary": {"module": "ActiveBody", "maxHealth": {"value": 1000}}},
				"moduleContracts": [], "scalarFields": {},
			},
			"presentation": {"buildingLifecycle": {"simulationFacts": {"maximumHealth": 1000}}},
		},
	}


func _finish() -> void:
	if passed + failed != EXPECTED_CHECKS:
		failed += 1
		printerr("CAPTURABLE_NEUTRAL FAIL expected_checks passed=%d failed=%d expected=%d" % [passed, failed - 1, EXPECTED_CHECKS])
	print("CAPTURABLE_NEUTRAL_RESULT passed=%d failed=%d" % [passed, failed])
	_watchdog.stop()
	quit(0 if failed == 0 else 1)
