extends SceneTree
## Find which manifest field is missing from the loaded packs.

const SimScript = preload("res://src/retail_slice/retail_slice_sim.gd")

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	var sim = SimScript.new()
	var rules = {
		"enable_base_loop": true,
		"starting_resources": 10000,
		"unit_rules": {
			SimScript.SOLDIER_OBJECT_ID: {"category": "infantry", "default_cost": 200, "default_build_ticks": 100},
			SimScript.ARCHER_OBJECT_ID: {"category": "ranged", "default_cost": 200, "default_build_ticks": 100},
			SimScript.TOWER_GUARD_OBJECT_ID: {"category": "specialist", "default_cost": 400, "default_build_ticks": 200},
			SimScript.KNIGHT_OBJECT_ID: {"category": "cavalry", "default_cost": 550, "default_build_ticks": 250},
			SimScript.BUILDER_OBJECT_ID: {"category": "builder", "is_builder": true, "default_cost": 0, "default_build_ticks": 100},
		},
	}

	print("[DIAGNOSTIC] Setting up sim...")
	sim._rules = rules
	sim.setup({}, {})  # Empty gameplay_rules, so it uses the _rules above

	if sim.configuration_error != "":
		print("[FOUND] Configuration error: %s" % sim.configuration_error)
		var field = sim.configuration_error.split("'")[1] if "'" in sim.configuration_error else "unknown"
		print("[CRITICAL] Missing field: %s" % field)

		# Try to get the manifest and see what's in it
		var manifest = sim._rules.get("faction_manifest", {})
		if typeof(manifest) == TYPE_DICTIONARY:
			print("[MANIFEST] Faction ID: %s" % manifest.get("faction", "unknown"))
			print("[MANIFEST] Pack ID: %s" % manifest.get("pack_id", "unknown"))
			var required_keys := ["unit_production_rules", "ai_production_plan", "structure_kinds",
				"structure_max_health", "structure_build_rules", "unit_damage_types",
				"structure_armor", "spawn_roster"]
			for key in required_keys:
				var present = manifest.has(key)
				print("[MANIFEST] %s: %s" % [key, "PRESENT" if present else "MISSING"])
	else:
		print("[RESULT] Configuration succeeded (unexpected)")

	quit(0)
