extends SceneTree
## Tests for manifest field refusals (Q80).
## Missing required fields now refuse configuration instead of silently falling back
## to invented constants.

const SimScript = preload("res://src/retail_slice/retail_slice_sim.gd")
const ManifestScript = preload("res://src/retail_slice/retail_faction_manifest.gd")

var passed := 0
var failed := 0

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	test_manifest_missing_unit_production_rules()
	test_manifest_missing_structure_build_rules()
	test_manifest_with_all_fields_refuses_incomplete()
	_finish()

func _finish() -> void:
	print("Manifest fallback refusal tests: %d passed, %d failed" % [passed, failed])
	quit(0 if failed == 0 else 1)

func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("PASS %s" % name)
	else:
		failed += 1
		print("FAIL %s" % name)
		if detail:
			print("  %s" % detail)

func test_manifest_missing_unit_production_rules() -> void:
	var sim := SimScript.new()
	var rules = {
		"enable_base_loop": false,
		"spawn_initial_battalions": false,
		"faction_manifest": {
			# Complete manifest except unit_production_rules
			"ai_production_plan": [SimScript.SOLDIER_HORDE_ID],
			"structure_kinds": ["fortress"],
			"structure_max_health": {"fortress": 7500},
			"structure_build_rules": {"fortress": {"cost": 5000, "seconds": 120.0}},
			"unit_damage_types": {},
			"structure_armor": {},
			"spawn_roster": [],
		},
		"unit_rules": {},
	}

	sim.setup({}, rules)
	if sim.configuration_error == "":
		_check("missing_unit_production_rules_refusal", false, "Expected configuration to refuse")
	else:
		_check("missing_unit_production_rules_refusal",
			sim.configuration_error.contains("unit_production_rules") and
			sim.configuration_error.contains("missing required field"),
			"Error message: %s" % sim.configuration_error)


func test_manifest_missing_structure_build_rules() -> void:
	var sim := SimScript.new()
	var rules = {
		"enable_base_loop": false,
		"spawn_initial_battalions": false,
		"faction_manifest": {
			# Complete manifest except structure_build_rules
			"unit_production_rules": {},
			"ai_production_plan": [SimScript.SOLDIER_HORDE_ID],
			"structure_kinds": ["fortress"],
			"structure_max_health": {"fortress": 7500},
			"unit_damage_types": {},
			"structure_armor": {},
			"spawn_roster": [],
		},
		"unit_rules": {},
	}

	sim.setup({}, rules)
	if sim.configuration_error == "":
		_check("missing_structure_build_rules_refusal", false, "Expected configuration to refuse")
	else:
		_check("missing_structure_build_rules_refusal",
			sim.configuration_error.contains("structure_build_rules") and
			sim.configuration_error.contains("missing required field"),
			"Error message: %s" % sim.configuration_error)


func test_manifest_with_all_fields_refuses_incomplete() -> void:
	## Verify that a complete manifest still configures successfully.
	## This uses default_manifest() which provides all required fields and a minimal
	## complete set of unit_rules. If a pack provides all its own fields properly,
	## configuration succeeds with no fallback to invented constants.

	## Create synthetic minimal unit rules for the default manifest
	var minimal_unit_rules = {
		SimScript.SOLDIER_OBJECT_ID: {
			"category": "infantry",
			"cost_rule": "soldier_cost",
			"build_ticks_rule": "soldier_build_ticks",
			"default_cost": 200,
			"default_build_ticks": 200,
		},
		SimScript.ARCHER_OBJECT_ID: {
			"category": "ranged",
			"cost_rule": "archer_cost",
			"build_ticks_rule": "archer_build_ticks",
			"default_cost": 200,
			"default_build_ticks": 200,
		},
		SimScript.KNIGHT_OBJECT_ID: {
			"category": "cavalry",
			"cost_rule": "knight_cost",
			"build_ticks_rule": "knight_build_ticks",
			"default_cost": 550,
			"default_build_ticks": 250,
		},
		SimScript.TOWER_GUARD_OBJECT_ID: {
			"category": "specialist",
			"cost_rule": "tower_guard_cost",
			"build_ticks_rule": "tower_guard_build_ticks",
			"default_cost": 400,
			"default_build_ticks": 200,
		},
		SimScript.BUILDER_OBJECT_ID: {
			"category": "builder",
			"is_builder": true,
			"default_cost": 0,
			"default_build_ticks": 100,
		},
	}

	var manifest = ManifestScript.default_manifest()
	# NON-VACUOUS oracle (Q80 round-3 verifier): override two values with
	# synthetic markers that differ from every constant, then assert the
	# MARKER lands in the sim. default_manifest() alone equals the constants,
	# so sampling it proves nothing about which source won.
	var soldier_rule := (manifest["unit_production_rules"] as Dictionary).get(SimScript.SOLDIER_HORDE_ID, {}) as Dictionary
	soldier_rule = soldier_rule.duplicate(true)
	soldier_rule["default_cost"] = 4321
	(manifest["unit_production_rules"] as Dictionary)[SimScript.SOLDIER_HORDE_ID] = soldier_rule
	var fortress_build := (manifest["structure_build_rules"] as Dictionary).get("fortress", {}) as Dictionary
	fortress_build = fortress_build.duplicate(true)
	fortress_build["cost"] = 8765
	(manifest["structure_build_rules"] as Dictionary)["fortress"] = fortress_build

	var sim := SimScript.new()
	var rules = {
		"enable_base_loop": false,
		"spawn_initial_battalions": false,
		"faction_manifest": manifest,
		"unit_rules": minimal_unit_rules,
	}

	sim.setup({}, rules)
	if sim.configuration_error != "":
		_check("manifest_complete_configures", false, "Configuration failed: %s" % sim.configuration_error)
		return

	# The synthetic markers must be what the sim carries — proving the
	# manifest, not any constant, is the source of truth.
	_check("manifest_unit_production_rules_from_manifest",
		int((sim._unit_production_rules.get(SimScript.SOLDIER_HORDE_ID, {}) as Dictionary).get("default_cost", -1)) == 4321,
		"soldier default_cost should be the manifest marker 4321, got %s" % str((sim._unit_production_rules.get(SimScript.SOLDIER_HORDE_ID, {}) as Dictionary).get("default_cost", -1)))
	_check("manifest_structure_build_rules_from_manifest",
		int((sim._structure_build_rules.get("fortress", {}) as Dictionary).get("cost", -1)) == 8765,
		"fortress cost should be the manifest marker 8765, got %s" % str((sim._structure_build_rules.get("fortress", {}) as Dictionary).get("cost", -1)))
