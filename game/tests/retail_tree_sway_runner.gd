extends SceneTree
## Tree sway: AUTHORED values only (queue Q33, owner playtest note 2026-08-18
## "trees don't naturally sway like RotWK, weird looking").
##
## Retail authority for every check below:
##   `gamedata.ini:8583  DownwindAngle = -0.785` — the wind direction, and the
##     only authored sway datum in RotWK.
##   `naturetrees.ini` — 220 `Draw = W3DTreeDraw` blocks, no sway field in any
##     of them; `naturetrees.ini:164` says so outright ("Note no SwayBehavior,
##     as this is handled in the W3DTreeBuffer").
##   `cinematicobjects.ini:21112 ClientUpdate = SwayClientUpdate` — body is
##     `;nothing`.
##   `SET_TREE_SWAY(ANGLE,ANGLE,ANGLE,INT,REAL)` — the one authored path into
##     amplitude/lean/period/randomness. It appears in 0 of the 128 shipped
##     RotWK `.map` files, so unauthored (= still) is the normal case.

const TreeSwayScript = preload("res://src/retail_slice/retail_tree_sway.gd")

const EXPECTED_CHECKS := 13
const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
var _watchdog := RunnerWatchdogScript.new()
var passed := 0
var failed := 0


func _initialize() -> void:
	_watchdog.start(self, "RETAIL_TREE_SWAY")
	call_deferred("_run")


func _run() -> void:
	_test_no_name_matching()
	_test_set_tree_sway_contract()
	_test_unauthored_is_still()
	_test_authored_sway_applies()
	_finish()


func _check(label: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("TREE_SWAY PASS %s" % label)
	else:
		failed += 1
		printerr("TREE_SWAY FAIL %s%s" % [label, "" if detail == "" else " (%s)" % detail])


func _test_no_name_matching() -> void:
	## Binding is by the object's DRAW MODULE, never by its type name. A type
	## name says nothing: `StreetLamp` contains "tree", and `PTGrass02`
	## (natureprop.ini:146) IS a W3DTreeDraw while a farm is not.
	var sway: Node = TreeSwayScript.new()
	root.add_child(sway)
	_check(
		"no_vegetation_token_classifier_exists",
		not sway.has_method("is_sway_source_type"),
		"is_sway_source_type must not come back"
	)
	var container := Node3D.new()
	root.add_child(container)
	var oak := _placement(container, "TreeOak01", 1)
	_placement(container, "FarmTemplate", 2)
	var lamp := _placement(container, "StreetLamp", 3)
	# No authored draw-module table: nothing binds, and the reason is named.
	var unbound := int(sway.call("bind_prop_container", container))
	var no_table: Dictionary = sway.call("runtime_contract")
	_check("absent_draw_module_table_binds_nothing", unbound == 0, "bound=%d" % unbound)
	_check(
		"absent_draw_module_table_is_a_named_gap",
		Array(no_table.get("gaps", [])).has("draw-module-table-absent:pack-ships-no-w3dtreedraw-kinds"),
		str(no_table.get("gaps", []))
	)
	# With the authored table, the type name is irrelevant: a W3DTreeDraw lamp
	# binds and a non-W3DTreeDraw oak does not.
	var bound := int(sway.call("bind_prop_container", container, ["StreetLamp"]))
	_check("draw_module_table_decides_binding", bound == 1, "bound=%d" % bound)
	_check(
		"binding_ignores_the_type_name",
		bool(lamp.get_meta("tree_sway", false)) and not oak.has_meta("tree_sway"),
		"lamp=%s oak=%s" % [str(lamp.has_meta("tree_sway")), str(oak.has_meta("tree_sway"))]
	)
	container.free()
	sway.free()


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


func _test_unauthored_is_still() -> void:
	var sway: Node = TreeSwayScript.new()
	root.add_child(sway)
	var container := Node3D.new()
	root.add_child(container)
	var tree := _placement(container, "TreeBanyan2", 7)
	var bound := int(sway.call("bind_prop_container", container, ["TreeBanyan2"]))
	var contract: Dictionary = sway.call("runtime_contract")
	# gamedata.ini:8583 DownwindAngle = -0.785 rad — bound, and it is the only
	# thing bound: amplitude/lean/period stay at zero because nothing authors
	# them.
	_check(
		"wind_direction_is_downwind_angle",
		is_equal_approx(float(contract.get("wind_radians", 0.0)), -0.785),
		str(contract.get("wind_radians", 0.0))
	)
	_check(
		"unauthored_map_leaves_trees_still",
		bound == 1
			and bool(contract.get("still", false))
			and is_equal_approx(float(contract.get("sway_degrees", -1.0)), 0.0),
		str(contract)
	)
	_check(
		"still_trees_name_their_gap",
		Array(contract.get("gaps", [])).has("no-authored-sway-amplitude:naturetrees.ini-authors-no-sway-field"),
		str(contract.get("gaps", []))
	)
	var before := tree.basis
	sway.call("_process", 0.5)
	sway.call("_process", 0.5)
	_check("still_trees_do_not_move", tree.basis.is_equal_approx(before), str(tree.basis))
	container.free()
	sway.free()


func _test_authored_sway_applies() -> void:
	var sway: Node = TreeSwayScript.new()
	root.add_child(sway)
	var container := Node3D.new()
	root.add_child(container)
	var tree := _placement(container, "TreeBanyan2", 9)
	sway.call("bind_prop_container", container, ["TreeBanyan2"])
	sway.call("apply_set_tree_sway", [90.0, 8.0, 2.0, 20, 0.2])
	var before := tree.basis
	sway.call("_process", 0.25)
	_check(
		"authored_set_tree_sway_moves_the_tree",
		not tree.basis.is_equal_approx(before)
			and String(sway.call("runtime_contract").get("source", "")) == "SET_TREE_SWAY",
		str(sway.call("runtime_contract"))
	)
	container.free()
	sway.free()


func _placement(container: Node3D, source_type: String, source_index: int) -> Node3D:
	var node := Node3D.new()
	node.set_meta("source_type", source_type)
	node.set_meta("source_index", source_index)
	container.add_child(node)
	return node


func _finish() -> void:
	if passed + failed != EXPECTED_CHECKS:
		failed += 1
		printerr("TREE_SWAY FAIL expected_checks passed=%d failed=%d expected=%d" % [passed, failed - 1, EXPECTED_CHECKS])
	print("RETAIL_TREE_SWAY_RESULT passed=%d failed=%d" % [passed, failed])
	_watchdog.stop()
	quit(0 if failed == 0 else 1)
