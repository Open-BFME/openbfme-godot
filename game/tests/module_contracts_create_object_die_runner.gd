extends SceneTree

## CreateObjectDie moduleContracts: executable + death queues pending OCL.
##
## Invocation:
##   Godot_v4.7-stable_win64_console.exe --headless --path game \
##     -s res://tests/module_contracts_create_object_die_runner.gd

const Adapter = preload("res://src/retail_slice/playable_unit_runtime_adapter.gd")
const Sim = preload("res://src/retail_slice/retail_slice_sim.gd")

const EXPECTED_CHECKS := 7

var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var document := _document_create_object_die()
	var contracts := Adapter.module_contracts(document)
	_check("adapter_projects_create_object_die", contracts.size() == 1)
	_check("contract_executable", bool(contracts[0].get("executable", false)))
	_check(
		"contract_creation_list",
		String((contracts[0].get("fields", {}) as Dictionary)
			.get("CreationList", {}).get("value", "")) == "OCL_FixtureDrop"
	)

	var sim: RetailSliceSim = _make_sim()
	var eid := _spawn_with_contracts(sim, contracts)
	_check("spawn_sets_create_object_die", bool(
		(sim.entities[eid] as Dictionary).get("create_object_die", false)
	))
	sim._apply_member_damage(1, 0, eid, 99999, "battalion", 0, 0)
	_check(
		"death_queues_pending_creation_list",
		sim.create_object_die_pending.size() == 1
			and String((sim.create_object_die_pending[0] as Dictionary)
				.get("creation_list", "")) == "OCL_FixtureDrop"
	)
	_check(
		"pending_carries_source_and_team",
		int((sim.create_object_die_pending[0] as Dictionary).get("source_entity", -1)) == eid
			and int((sim.create_object_die_pending[0] as Dictionary).get("team", -2))
				== Sim.ENEMY_TEAM
	)

	var toppled_doc := _document_create_object_die(["TOPPLED"])
	var toppled_contracts := Adapter.module_contracts(toppled_doc)
	var toppled_sim: RetailSliceSim = _make_sim()
	var toppled_eid := _spawn_with_contracts(toppled_sim, toppled_contracts)
	var row: Dictionary = toppled_sim.entities[toppled_eid]
	row["health"] = 0
	var no_members: Array[int] = []
	toppled_sim._apply_playable_unit_death_policy(row, "TOPPLED", no_members)
	_check(
		"excluded_toppled_does_not_queue",
		toppled_sim.create_object_die_pending.is_empty()
	)

	var ran := passed + failed
	if ran != EXPECTED_CHECKS:
		failed += 1
		printerr(
			"MODULE_CONTRACTS_CREATE_OBJECT_DIE FAIL liveness: ran %d expected %d"
			% [ran, EXPECTED_CHECKS]
		)
	print("MODULE_CONTRACTS_CREATE_OBJECT_DIE_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


func _make_sim() -> RetailSliceSim:
	var rules: Dictionary = {}
	for object_id in [
		Sim.SOLDIER_OBJECT_ID,
		Sim.ARCHER_OBJECT_ID,
		Sim.TOWER_GUARD_OBJECT_ID,
		Sim.KNIGHT_OBJECT_ID,
	]:
		rules[object_id] = _unit_rule()
	var sim: RetailSliceSim = Sim.new()
	sim.setup({}, {
		"member_health": 100,
		"unit_rules": rules,
		"faction_manifest": {"structure_armor": _fixture_structure_armor()},
	})
	sim.ai_enabled = false
	return sim


func _spawn_with_contracts(sim: RetailSliceSim, contracts: Array) -> int:
	var unit_type := Sim.SOLDIER_HORDE_ID
	var object_id := Sim.SOLDIER_OBJECT_ID
	var rule := _unit_rule()
	(sim._rules.get("unit_rules", {}) as Dictionary)[object_id] = rule
	(sim._rules.get("unit_rules", {}) as Dictionary)[unit_type] = rule
	sim._unit_module_contracts[unit_type] = contracts.duplicate(true)
	sim._unit_module_contracts[object_id] = contracts.duplicate(true)
	var result: Dictionary = sim.script_spawn_entity(
		unit_type, Sim.ENEMY_TEAM, Vector2(40, 40)
	)
	return int(result.get("entity_id", -1))


func _document_create_object_die(excluded: Array = []) -> Dictionary:
	return {
		"objectId": "GondorFighter",
		"category": "infantry",
		"registration": {
			"composition": {
				"containerObjectId": "GondorFighter",
				"primaryMemberObjectId": "GondorFighter",
			},
			"moduleContracts": [{
				"module": "CreateObjectDie",
				"fields": {
					"deathTypes": "ALL",
					"excludedDeathTypes": excluded.duplicate(),
					"CreationList": {
						"authored": "OCL_FixtureDrop",
						"value": "OCL_FixtureDrop",
						"sourceIni": "data/ini/object/fixture.ini",
						"line": 10,
					},
				},
				"runtimeStatus": "executable",
				"extraction": "typed",
				"carrier": "Behavior",
				"sourceIni": "data/ini/object/fixture.ini",
				"line": 10,
				"tag": "ModuleTag_Drop",
			}],
			"simulation": {
				"displayName": "Fixture",
				"buildCost": 100,
				"buildTimeSeconds": 1.0,
				"commandPoints": 1,
				"memberCount": 1,
				"memberHealth": 100,
				"speed": 10.0,
				"visionRange": 100.0,
				"combat": {
					"attackRange": 10.0,
					"minimumAttackRange": 0.0,
					"delayBetweenShotsMs": 100.0,
					"preAttackDelayMs": 0.0,
					"firingDurationMs": 0.0,
					"damage": 10,
				},
				"movement": {
					"acceleration": 10.0,
					"braking": 10.0,
					"turnRateDegreesPerSecond": 180.0,
				},
				"formation": {
					"memberCount": 1,
					"positions": [{"x": 0.0, "y": 0.0}],
				},
				"resolved": {
					"moduleContracts": [{
						"module": "CreateObjectDie",
						"fields": {
							"deathTypes": "ALL",
							"excludedDeathTypes": excluded.duplicate(),
							"CreationList": {
								"authored": "OCL_FixtureDrop",
								"value": "OCL_FixtureDrop",
								"sourceIni": "data/ini/object/fixture.ini",
								"line": 10,
							},
						},
						"runtimeStatus": "executable",
						"extraction": "typed",
						"carrier": "Behavior",
						"sourceIni": "data/ini/object/fixture.ini",
						"line": 10,
						"tag": "ModuleTag_Drop",
					}],
				},
			},
		},
	}


func _unit_rule() -> Dictionary:
	return {
		"horde_id": Sim.SOLDIER_HORDE_ID,
		"speed": 1.0,
		"speed_source": 10.0,
		"acceleration": 1.0,
		"acceleration_source": 10.0,
		"turn_rate_degrees_per_second": 180.0,
		"braking": 1.0,
		"braking_source": 10.0,
		"attack_range": 1.0,
		"attack_range_source": 10.0,
		"minimum_attack_range": 0.0,
		"minimum_attack_range_source": 0.0,
		"vision_range": 10.0,
		"vision_range_source": 100.0,
		"delay_between_shots_ms": 100.0,
		"pre_attack_delay_ms": 0.0,
		"firing_duration_ms": 0.0,
		"attack_period_ticks": 1,
		"pre_attack_ticks": 0,
		"firing_duration_ticks": 0,
		"member_damage": 100,
		"member_health": 100,
		"member_count": 1,
		"formation_positions": [Vector3.ZERO],
		"provenance": {},
	}


func _fixture_structure_armor() -> Dictionary:
	var armor := {}
	for kind_value in Sim.STRUCTURE_KINDS:
		armor[String(kind_value)] = {
			"set_id": "FixtureArmor",
			"damage_scalar": 1.0,
			"scalars": {"default": 1.0},
		}
	return armor


func _check(label: String, condition: bool) -> void:
	if condition:
		passed += 1
	else:
		failed += 1
		push_error("MODULE_CONTRACTS_CREATE_OBJECT_DIE_FAIL %s" % label)
