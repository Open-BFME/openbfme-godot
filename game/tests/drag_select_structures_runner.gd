extends SceneTree
## Drag-select (band-box) must follow retail's structure rule.
##
## Retail truth (OpenSAGE `SelectionSystem.MultiSelect`, the faithful SAGE
## band-box reimplementation — the OpenSAGE SelectionSystem (Logic)):
##   foreach own object whose RoughCollider intersects the box frustum:
##       non-structure -> selectedObjects.Add(id)
##       structure     -> structure ??= id   (remembered, not added)
##   if (selectedObjects.Count == 0 && structure.HasValue)
##       selectedObjects.Add(structure.Value);
## i.e. a drag box selects own units; a structure caught in the box is
## selected ONLY when the box caught no units at all.
##
## The slice bug: `_finish_box_selection` scanned battalions only and returned
## when the box held none, so a box over your own buildings never selected
## anything ("drag-selection box doesn't select buildings").
##
## Run:
##   OPENBFME_CONTENT=<repo>/workspace/content-packs godot --headless --path game \
##     --script res://tests/drag_select_structures_runner.gd

const BOOT_DEADLINE_MS := 300000
const Watchdog := preload("res://tests/runner_watchdog.gd")

## LIVENESS (rulebook T3): a mid-body script error aborts the enclosing
## function without propagating. Completion is marked only at the END of each
## section, and the exact check count must match EXPECTED_CHECKS.
const EXPECTED_TESTS := 3
const EXPECTED_CHECKS := 14
const MINIMUM_CHECKS := 12

var passed := 0
var failed := 0
var _completed: Array[String] = []
var _watchdog := Watchdog.new()


func _initialize() -> void:
	_watchdog.start(self, "DRAG_SELECT")
	call_deferred("_run")


func _run() -> void:
	root.size = Vector2i(1920, 1080)
	var slice_scene: PackedScene = load("res://scenes/retail_vertical_slice.tscn")
	if not _check("slice_scene_loads", slice_scene != null):
		return _finish()
	var slice = slice_scene.instantiate()
	root.add_child(slice)
	var deadline := Time.get_ticks_msec() + BOOT_DEADLINE_MS
	while Time.get_ticks_msec() < deadline:
		await process_frame
		if bool(slice.ready_ok) or String(slice.failure_reason) != "":
			break
	if not _check("slice_boots", bool(slice.ready_ok), String(slice.failure_reason)):
		return _finish()
	# Let presentation build battalion/structure nodes.
	for _i in range(5):
		await process_frame

	var sim = slice.simulation
	var team := int(slice.local_team)
	var fortress := int(sim.fortress_id(team))
	if not _check("fortress_exists", fortress != 0, "local seat seeded no fortress"):
		return _finish()
	var battalions: Array = sim.living_ids(team)
	if not _check("starting_army_exists", not battalions.is_empty(), "local seat seeded no battalions"):
		return _finish()

	_test_structure_only_box_selects_the_structure(slice, sim, team, fortress)
	_test_box_with_a_unit_ignores_the_structure(slice, sim, team, fortress, battalions)
	_test_box_beside_the_footprint_selects_nothing(slice, sim, team, fortress)

	_finish()


func _finish() -> void:
	if _completed.size() != EXPECTED_TESTS:
		# Every early return above already failed a named check; this catches a
		# section that silently never ran.
		failed += 1
		push_error("DRAG_SELECT_FAIL liveness: %d/%d sections reported (%s)" % [
			_completed.size(), EXPECTED_TESTS, ", ".join(_completed)
		])
	var ran := passed + failed
	if ran != EXPECTED_CHECKS:
		failed += 1
		push_error("DRAG_SELECT_FAIL liveness: ran %d checks, expected %d - a function aborted before its assertions" % [
			ran, EXPECTED_CHECKS
		])
	elif ran < MINIMUM_CHECKS:
		failed += 1
		push_error("DRAG_SELECT_FAIL liveness: only %d checks ran, expected at least %d" % [
			ran, MINIMUM_CHECKS
		])
	print("DRAG_SELECT_RESULT passed=%d failed=%d sections=%d" % [passed, failed, _completed.size()])
	quit(0 if failed == 0 else 1)


func _screen_rect_around(slice, world_position: Vector2, half_size: Vector2) -> Rect2:
	var screen: Vector2 = slice.camera.unproject_position(
		Vector3(world_position.x, 0.0, world_position.y)
	)
	return Rect2(screen - half_size, half_size * 2.0)


func _drag_box(slice, rect: Rect2) -> void:
	slice._drag_select_origin = rect.position
	slice._finish_box_selection(rect.position + rect.size, false)


func _test_structure_only_box_selects_the_structure(slice, sim, team: int, fortress: int) -> void:
	## Retail: a band-box over own structures and NO units selects the
	## structure (the remembered fallback fires because no unit was caught).
	var fortress_row: Dictionary = sim.structure(fortress)
	slice._center_camera_on(Vector2(fortress_row.get("position", Vector2.ZERO)))
	# A box hugging the fortress centre: inside the footprint, expected to hold
	# no battalion. Preconditions are asserted, not assumed.
	var rect := _screen_rect_around(slice, Vector2(fortress_row.get("position", Vector2.ZERO)), Vector2(24, 24))
	var unit_in_box := false
	for id_value in sim.living_ids(team):
		var node: Node3D = slice.battalion_nodes.get(int(id_value), null)
		if node == null or not is_instance_valid(node):
			continue
		if rect.has_point(slice.camera.unproject_position(node.global_position)):
			unit_in_box = true
	_check("precondition_no_unit_in_the_structure_box", not unit_in_box)
	if unit_in_box:
		_completed.append("structure-only-box")
		return
	sim.clear_selection()
	slice.selected_structure_id = 0
	_drag_box(slice, rect)
	var selected := int(slice.selected_structure_id)
	_check("structure_only_box_selects_a_structure", selected != 0)
	if selected == 0:
		_completed.append("structure-only-box")
		return
	var selected_row: Dictionary = sim.structure(selected)
	_check(
		"box_selected_structure_is_own_and_living",
		int(selected_row.get("team", -1)) == team and int(selected_row.get("health", 0)) > 0
	)
	_check("box_structure_pick_cleared_unit_selection", (sim.selected_ids as Array).is_empty())
	# A click resolves castle pieces to their fortress; the box must take the
	# same resolution so the player never lands on a command-less BSE piece.
	_check(
		"box_structure_pick_uses_the_piece_resolution",
		selected == int(slice._selection_target_structure(selected))
	)
	_completed.append("structure-only-box")


func _test_box_with_a_unit_ignores_the_structure(slice, sim, team: int, fortress: int, battalions: Array) -> void:
	## Retail: when the box catches ANY own unit, the structure fallback does
	## not fire — the box selects units only.
	var battalion_id := int(battalions[0])
	var node: Node3D = slice.battalion_nodes.get(battalion_id, null)
	if not _check("battalion_node_exists", node != null and is_instance_valid(node)):
		_completed.append("unit-box-wins")
		return
	var fortress_row: Dictionary = sim.structure(fortress)
	slice._center_camera_on(Vector2(fortress_row.get("position", Vector2.ZERO)))
	var battalion_screen: Vector2 = slice.camera.unproject_position(node.global_position)
	var fortress_pos_b := Vector2(fortress_row.get("position", Vector2.ZERO))
	var fortress_screen: Vector2 = slice.camera.unproject_position(
		Vector3(fortress_pos_b.x, 0.0, fortress_pos_b.y)
	)
	var rect := Rect2(battalion_screen, Vector2.ZERO).expand(fortress_screen).abs().grow(12.0)
	sim.clear_selection()
	slice.selected_structure_id = 0
	_drag_box(slice, rect)
	_check("unit_in_box_is_selected", (sim.selected_ids as Array).has(battalion_id))
	_check("structure_in_unit_box_is_not_selected", int(slice.selected_structure_id) == 0)
	_completed.append("unit-box-wins")


func _test_box_beside_the_footprint_selects_nothing(slice, sim, team: int, fortress: int) -> void:
	## The footprint is the authored geometry, not an inflated blob: a box
	## wholly clear of the fortress silhouette must select nothing.
	var fortress_row: Dictionary = sim.structure(fortress)
	var fortress_pos := Vector2(fortress_row.get("position", Vector2.ZERO))
	slice._center_camera_on(fortress_pos)
	var radius := float(slice._structure_pick_radius(fortress, String(fortress_row.get("structure_kind", ""))))
	# Two footprint radii to the side: clear of any margin, still on screen.
	var beside := fortress_pos + Vector2(radius * 2.5 + 1.0, 0.0)
	var rect := _screen_rect_around(slice, beside, Vector2(20, 20))
	sim.clear_selection()
	slice.selected_structure_id = 0
	_drag_box(slice, rect)
	_check("box_beside_the_footprint_selects_no_structure", int(slice.selected_structure_id) == 0)
	_check("box_beside_the_footprint_selects_no_units", (sim.selected_ids as Array).is_empty())
	_completed.append("empty-ground-box")


func _check(label: String, condition: bool, detail := "") -> bool:
	_watchdog.note(label)
	if condition:
		passed += 1
	else:
		failed += 1
		push_error("DRAG_SELECT_FAIL %s%s" % [label, "" if detail == "" else " (%s)" % detail])
	return condition
