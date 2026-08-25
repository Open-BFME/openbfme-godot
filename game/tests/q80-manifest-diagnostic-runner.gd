extends SceneTree
## Diagnostic test to identify which manifest field is missing or has changed.

const SimScript = preload("res://src/retail_slice/retail_slice_sim.gd")
const ManifestScript = preload("res://src/retail_slice/retail_faction_manifest.gd")

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	# Check what fields are in the default manifest
	var manifest = ManifestScript.default_manifest()

	var required_keys := ["unit_production_rules", "ai_production_plan", "structure_kinds",
		"structure_max_health", "structure_build_rules", "unit_damage_types",
		"structure_armor", "spawn_roster"]

	print("[DIAGNOSTIC] Checking default_manifest fields:")
	for key in required_keys:
		var present = manifest.has(key)
		var value_str = "PRESENT" if present else "MISSING"
		print("  %s: %s" % [key, value_str])
		if present:
			var val = manifest.get(key)
			print("    Type: %s, Size: %d" % [typeof(val), (val as Dictionary if typeof(val) == TYPE_DICTIONARY else (val as Array if typeof(val) == TYPE_ARRAY else 0)).size() if val else 0])

	# Now try to configure with it
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

	var sim := SimScript.new()
	var rules = {
		"enable_base_loop": false,
		"spawn_initial_battalions": false,
		"faction_manifest": manifest,
		"unit_rules": minimal_unit_rules,
	}

	print("\n[DIAGNOSTIC] Attempting configuration...")
	sim.setup({}, rules)
	if sim.configuration_error != "":
		print("  CONFIGURATION FAILED: %s" % sim.configuration_error)
	else:
		print("  Configuration succeeded")
		print("  _unit_production_rules size: %d" % sim._unit_production_rules.size())
		print("  _structure_kinds size: %d" % sim._structure_kinds.size())
		print("  _structure_max_health size: %d" % sim._structure_max_health.size())
		print("  _structure_armor size: %d" % sim._structure_armor.size())

	print("\n[DIAGNOSTIC] Done - exiting")
	quit(0)
