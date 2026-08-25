extends SceneTree
## Diagnostic test for state pin scenario - check if empty manifest gets required fields check.

const SimScript = preload("res://src/retail_slice/retail_slice_sim.gd")

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	var sim = SimScript.new()
	var rules = {
		"enable_base_loop": true,
		"starting_resources": 10000,
		"unit_rules": {
			SimScript.SOLDIER_OBJECT_ID: _unit_rule(SimScript.SOLDIER_HORDE_ID, false),
			SimScript.ARCHER_OBJECT_ID: _unit_rule(SimScript.ARCHER_OBJECT_ID, false),
		},
		# NOTE: No faction_manifest provided, like the state pin test
	}

	print("[DIAGNOSTIC] Running setup without faction_manifest...")
	sim.setup({}, rules)

	if sim.configuration_error != "":
		print("[RESULT] Configuration FAILED (as expected): %s" % sim.configuration_error)
	else:
		print("[RESULT] Configuration SUCCEEDED (unexpected!)")
		print("[RESULT] This means the required_keys check is not firing")
		print("[RESULT] The simulation is using fallback defaults")
	quit(0)

func _unit_rule(unit_type: String, is_builder: bool) -> Dictionary:
	return {
		"category": "infantry" if not is_builder else "builder",
		"is_builder": is_builder,
		"default_cost": 200,
		"default_build_ticks": 100,
	}
