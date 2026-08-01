extends SceneTree

## End-to-end proof that KeepObjectDie moduleContracts are executable and
## consumed by RetailSliceSim death policy through the real spawn attach path.
##
## Path under test:
##   descriptor.moduleContracts (runtimeStatus=executable)
##     -> PlayableUnitAdapter.module_contracts
##     -> sim._unit_module_contracts (registered before spawn)
##     -> spawn calls _attach_module_contracts
##     -> keep_object_die_policy + death-type match
##     -> readable corpse kept; excluded death types do not keep
##
## Invocation:
##   Godot_v4.7-stable_win64_console.exe --headless --path game \
##     -s res://tests/module_contracts_keep_object_die_runner.gd

const Adapter = preload("res://src/retail_slice/playable_unit_runtime_adapter.gd")
const Sim = preload("res://src/retail_slice/retail_slice_sim.gd")

const EXPECTED_CHECKS := 11

var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var document := _document_with_keep_object_die([])
	var contracts := Adapter.module_contracts(document)
	_check("adapter_projects_keep_object_die_contract", contracts.size() == 1)
	var contract: Dictionary = contracts[0] if contracts.size() > 0 else {}
	_check("contract_module_name", String(contract.get("module", "")) == "KeepObjectDie")
	_check("contract_is_executable", bool(contract.get("executable", false)))
	_check(
		"contract_destroy_on_death_false",
		bool((contract.get("fields", {}) as Dictionary).get("destroyOnDeath", true)) == false
	)

	var with_keep: RetailSliceSim = _make_sim()
	var eid := _spawn_with_contracts(with_keep, contracts, [{
		"owner_role": "object",
		"death_types": "ALL",
		"excluded_death_types": [],
	}])
	_check("spawn_attach_sets_keep_object_die_flag", bool(
		(with_keep.entities[eid] as Dictionary).get("keep_object_die", false)
	))
	_check(
		"spawn_attach_records_policy",
		((with_keep.entities[eid] as Dictionary).get("keep_object_die_policy", {}) as Dictionary)
			.get("death_types", "") == "ALL"
	)
	with_keep._apply_member_damage(1, 0, eid, 99999, "battalion", 0, 0)
	_check(
		"keep_object_die_overrides_destroy_die_erase",
		with_keep.entities.has(eid)
			and int((with_keep.entities[eid] as Dictionary).get("health", -1)) == 0
			and int((with_keep.entities[eid] as Dictionary).get("corpse_expire_tick", -1))
				== with_keep.tick_index + Sim.CORPSE_LIFETIME_TICKS
	)

	var destroy_only: RetailSliceSim = _make_sim()
	var destroy_eid := _spawn_with_contracts(destroy_only, [], [{
		"owner_role": "object",
		"death_types": "ALL",
		"excluded_death_types": [],
	}])
	destroy_only._apply_member_damage(1, 0, destroy_eid, 99999, "battalion", 0, 0)
	_check(
		"destroy_die_still_erases_without_keep_contract",
		not destroy_only.entities.has(destroy_eid)
	)

	var excluded_doc := _document_with_keep_object_die(["TOPPLED"])
	var excluded_contracts := Adapter.module_contracts(excluded_doc)
	var toppled_sim: RetailSliceSim = _make_sim()
	var toppled_eid := _spawn_with_contracts(toppled_sim, excluded_contracts, [{
		"owner_role": "object",
		"death_types": "ALL",
		"excluded_death_types": [],
	}])
	var toppled_row: Dictionary = toppled_sim.entities[toppled_eid]
	var excluded_list: Array = (
		(toppled_row.get("keep_object_die_policy", {}) as Dictionary)
			.get("excluded_death_types", []) as Array
	)
	_check(
		"excluded_policy_still_attaches_keep_flag",
		bool(toppled_row.get("keep_object_die", false))
			and excluded_list.size() == 1
			and String(excluded_list[0]) == "TOPPLED"
	)
	# TOPPLED is excluded from KeepObjectDie → DestroyDie erase proceeds.
	toppled_row["health"] = 0
	toppled_row["destroy_die"] = [{
		"owner_role": "object",
		"death_types": "ALL",
		"excluded_death_types": [],
	}]
	var no_members: Array[int] = []
	var policy: Dictionary = toppled_sim._apply_playable_unit_death_policy(
		toppled_row, "TOPPLED", no_members
	)
	_check(
		"toppled_excluded_does_not_keep_when_destroy_die_matches",
		bool(policy.get("destroy_object", false)) == true
	)
	toppled_row["health"] = 0
	policy = toppled_sim._apply_playable_unit_death_policy(
		toppled_row, "NORMAL", no_members
	)
	_check(
		"normal_death_still_kept_under_all_minus_toppled",
		bool(policy.get("destroy_object", false)) == false
	)

	var ran := passed + failed
	if ran != EXPECTED_CHECKS:
		failed += 1
		printerr(
			"MODULE_CONTRACTS_KEEP_OBJECT_DIE FAIL liveness: ran %d checks, expected %d"
			% [ran, EXPECTED_CHECKS]
		)
	print("MODULE_CONTRACTS_KEEP_OBJECT_DIE_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


func _make_sim() -> RetailSliceSim:
	var rules: Dictionary = {}
	for object_id in [
		Sim.SOLDIER_OBJECT_ID,
		Sim.ARCHER_OBJECT_ID,
		Sim.TOWER_GUARD_OBJECT_ID,
		Sim.KNIGHT_OBJECT_ID,
	]:
		rules[object_id] = _unit_rule([])
	var sim: RetailSliceSim = Sim.new()
	sim.setup({}, {
		"member_health": 100,
		"unit_rules": rules,
		"faction_manifest": {
			"structure_armor": _fixture_structure_armor(),
		},
	})
	sim.ai_enabled = false
	return sim


func _spawn_with_contracts(
	sim: RetailSliceSim, contracts: Array, destroy_die: Array
) -> int:
	## Register contracts then spawn through script_spawn_entity so attach runs
	## on the real spawn path (no manual _attach_module_contracts).
	var unit_type := Sim.SOLDIER_HORDE_ID
	var object_id := Sim.SOLDIER_OBJECT_ID
	var rule := _unit_rule(destroy_die)
	# script_spawn_entity / spawn_script_object look up unit rules by type.
	(sim._rules.get("unit_rules", {}) as Dictionary)[object_id] = rule
	(sim._rules.get("unit_rules", {}) as Dictionary)[unit_type] = rule
	if not contracts.is_empty():
		sim._unit_module_contracts[unit_type] = contracts.duplicate(true)
		sim._unit_module_contracts[object_id] = contracts.duplicate(true)
	var result: Dictionary = sim.script_spawn_entity(
		unit_type, Sim.ENEMY_TEAM, Vector2(50, 50)
	)
	if not bool(result.get("ok", false)):
		push_error("spawn failed: %s" % String(result.get("reason", "")))
		return -1
	return int(result.get("entity_id", -1))


func _document_with_keep_object_die(excluded: Array) -> Dictionary:
	return {
		"objectId": "GondorFighter",
		"category": "infantry",
		"registration": {
			"composition": {
				"containerObjectId": "GondorFighter",
				"primaryMemberObjectId": "GondorFighter",
			},
			"moduleContracts": [{
				"module": "KeepObjectDie",
				"fields": {
					"deathTypes": "ALL",
					"excludedDeathTypes": excluded.duplicate(),
					"destroyOnDeath": false,
				},
				"runtimeStatus": "executable",
				"extraction": "typed",
				"carrier": "Behavior",
				"sourceIni": "data/ini/object/fixture.ini",
				"line": 42,
				"tag": "ModuleTag_IWantRubble",
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
						"module": "KeepObjectDie",
						"fields": {
							"deathTypes": "ALL",
							"excludedDeathTypes": excluded.duplicate(),
							"destroyOnDeath": false,
						},
						"runtimeStatus": "executable",
						"extraction": "typed",
						"carrier": "Behavior",
						"sourceIni": "data/ini/object/fixture.ini",
						"line": 42,
						"tag": "ModuleTag_IWantRubble",
					}],
				},
			},
		},
	}


func _unit_rule(destroy_die: Array) -> Dictionary:
	var rule := {
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
	if not destroy_die.is_empty():
		rule["destroy_die"] = destroy_die.duplicate(true)
	return rule


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
		push_error("MODULE_CONTRACTS_KEEP_OBJECT_DIE_FAIL %s" % label)
