extends SceneTree
## SET_TREE_SWAY and vegetation classification for bound props.

const TreeSwayScript = preload("res://src/retail_slice/retail_tree_sway.gd")

const EXPECTED_CHECKS := 10
const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
var _watchdog := RunnerWatchdogScript.new()
var passed := 0
var failed := 0


func _initialize() -> void:
	_watchdog.start(self, "RETAIL_TREE_SWAY")
	call_deferred("_run")


func _run() -> void:
	_test_classification()
	_test_set_tree_sway_contract()
	_test_bind_and_idle()
	_finish()


func _check(label: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("TREE_SWAY PASS %s" % label)
	else:
		failed += 1
		printerr("TREE_SWAY FAIL %s%s" % [label, "" if detail == "" else " (%s)" % detail])


func _test_classification() -> void:
	_check("grass_and_trees_sway", TreeSwayScript.is_sway_source_type("PTGrass15") and TreeSwayScript.is_sway_source_type("TreeOak01") and TreeSwayScript.is_sway_source_type("ElvenMallorn"))
	_check("false_friends_do_not_sway", not TreeSwayScript.is_sway_source_type("FarmTemplate") and not TreeSwayScript.is_sway_source_type("CaveTrollLair") and not TreeSwayScript.is_sway_source_type("StreetLamp"))
	_check("shrubs_sway", TreeSwayScript.is_sway_source_type("PSnowyShrub03"))


func _test_set_tree_sway_contract() -> void:
	var sway: Node = TreeSwayScript.new()
	root.add_child(sway)
	_check("wrong_arity_fails_closed", sway.call("apply_set_tree_sway", [45.0, 12.5]) != "")
	_check("zero_frames_fails_closed", sway.call("apply_set_tree_sway", [45.0, 12.5, 3.25, 0, 0.75]) != "")
	var applied := String(sway.call("apply_set_tree_sway", [45.0, 12.5, 3.25, 30, 0.75]))
	var contract: Dictionary = sway.call("runtime_contract")
	_check(
		"set_tree_sway_keeps_map_arguments",
		applied == ""
			and is_equal_approx(float(contract.get("wind_degrees", -1.0)), 45.0)
			and is_equal_approx(float(contract.get("sway_degrees", -1.0)), 12.5)
			and is_equal_approx(float(contract.get("lean_degrees", -1.0)), 3.25)
			and int(contract.get("frames_per_sway", 0)) == 30
			and is_equal_approx(float(contract.get("randomness", -1.0)), 0.75)
			and String(contract.get("source", "")) == "SET_TREE_SWAY",
		str(contract)
	)
	sway.free()


func _test_bind_and_idle() -> void:
	var sway: Node = TreeSwayScript.new()
	root.add_child(sway)
	var container := Node3D.new()
	root.add_child(container)
	var grass := Node3D.new()
	grass.set_meta("source_type", "PTGrass15")
	grass.set_meta("source_index", 4)
	container.add_child(grass)
	var farm := Node3D.new()
	farm.set_meta("source_type", "FarmTemplate")
	farm.set_meta("source_index", 5)
	container.add_child(farm)
	var bound := int(sway.call("bind_prop_container", container))
	var idle: Dictionary = sway.call("runtime_contract")
	_check("only_vegetation_is_bound", bound == 1, "bound=%d" % bound)
	_check("idle_breeze_is_named_until_script", bool(idle.get("idle_until_script", false)) and String(idle.get("source", "")) == "idle-breeze-until-set-tree-sway")
	var before := grass.basis
	sway.call("_process", 0.5)
	_check("idle_breeze_rotates_bound_tree", not grass.basis.is_equal_approx(before))
	sway.call("apply_set_tree_sway", [90.0, 8.0, 2.0, 20, 0.2])
	_check("script_overrides_idle_source", String(sway.call("runtime_contract").get("source", "")) == "SET_TREE_SWAY")
	container.free()
	sway.free()


func _finish() -> void:
	if passed + failed != EXPECTED_CHECKS:
		failed += 1
		printerr("TREE_SWAY FAIL expected_checks passed=%d failed=%d expected=%d" % [passed, failed - 1, EXPECTED_CHECKS])
	print("RETAIL_TREE_SWAY_RESULT passed=%d failed=%d" % [passed, failed])
	_watchdog.stop()
	quit(0 if failed == 0 else 1)
