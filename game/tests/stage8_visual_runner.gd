extends SceneTree
## godot --headless --path game -s res://tests/stage8_visual_runner.gd

var passed: int = 0
var failed: int = 0


const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
# Turns a GDScript runtime error inside `_run` — which unwinds past every
# `quit()` and would otherwise leave this headless process idling forever —
# into a loud non-zero exit. See tests/runner_watchdog.gd.
var _runner_watchdog := RunnerWatchdogScript.new()


func _initialize() -> void:
	_runner_watchdog.start(self, "STAGE8_VISUAL_RUNNER")
	call_deferred("_run")


func _run() -> void:
	var packed: PackedScene = load("res://scenes/stage8_lab.tscn")
	_check("stage8_lab_scene_loads", packed != null)
	if packed == null:
		_finish()
		return
	var lab: Node = packed.instantiate()
	root.add_child(lab)
	current_scene = lab
	await process_frame
	await process_frame
	var world: RefCounted = lab.get("world")
	_check("stage8_real_lab_instantiates", world != null)
	if world == null:
		_finish()
		return
	world.set_paused(true)
	lab.call("_refresh")
	_test_map_and_board(lab, world)
	_test_groups_and_formations(lab, world)
	_test_save_load_ui(lab, world)
	_test_pause_speed_and_map_switch(lab)
	lab.call("cleanup_slot", 8)
	var prior: WeakRef = weakref(lab)
	lab.queue_free()
	current_scene = null
	await process_frame
	await process_frame
	_check("stage8_visual_cleanup_releases_scene", prior.get_ref() == null)
	_finish()


func _test_map_and_board(lab: Node, world: RefCounted) -> void:
	var selector := lab.get("map_selector") as OptionButton
	_check("stage8_hud_lists_four_external_maps", selector != null and selector.item_count == 4)
	_check("stage8_lab_spawns_playable_sides", world.entity_ids().size() == 8 and int(world.entity(1)["team"]) == 0 and int(world.entity(8)["team"]) == 1)
	_check("stage8_lab_selects_five_blue_units", Array(lab.get("selected_ids")) == [1, 2, 3, 4, 5])
	_check("stage8_status_shows_tick_and_hash", String((lab.get("status_label") as Label).text).contains("HASH") and String((lab.get("status_label") as Label).text).contains(world.state_hash_text()))
	_check("stage8_board_exposes_map_blockers", world.is_blocked(Vector2i(9, 2)) and not world.is_blocked(Vector2i(2, 7)))


func _test_groups_and_formations(lab: Node, world: RefCounted) -> void:
	var subset: Array[int] = [5, 3, 1]
	lab.call("select_units", subset, false)
	var assigned: Dictionary = lab.call("assign_group", 1)
	_check("visual_group_assignment_is_sorted", Array(assigned.get("entity_ids", [])) == [1, 3, 5])
	var empty_selection: Array[int] = []
	lab.call("select_units", empty_selection, false)
	_check("visual_group_recall_restores_selection", Array(lab.call("recall_group", 1)) == [1, 3, 5] and Array(lab.get("selected_ids")) == [1, 3, 5])
	var group_buttons: Dictionary = lab.get("group_buttons")
	_check("visual_hud_has_groups_one_through_nine", group_buttons.size() == 9 and group_buttons.has(1) and group_buttons.has(9))
	var line: Dictionary = lab.call("order_formation", "line", Vector2i(11, 6))
	_check("visual_line_button_path_is_authoritative", bool(line.get("ok", false)) and String(world.entity(1)["formation"]) == "line")
	var wedge: Dictionary = lab.call("order_formation", "wedge", Vector2i(11, 6))
	_check("visual_wedge_button_path_is_authoritative", bool(wedge.get("ok", false)) and String(world.entity(3)["formation"]) == "wedge")
	var column: Dictionary = lab.call("order_formation", "column", Vector2i(11, 6))
	_check("visual_column_button_path_is_authoritative", bool(column.get("ok", false)) and String(world.entity(5)["formation"]) == "column")
	var buttons: Dictionary = lab.get("formation_buttons")
	_check("visual_hud_binds_all_three_formations", buttons.size() == 3)


func _test_save_load_ui(lab: Node, world: RefCounted) -> void:
	lab.call("cleanup_slot", 8)
	world.set_paused(false)
	world.advance(3)
	world.damage_unit(3, 41)
	world.set_paused(true)
	var saved_hash: int = world.state_hash()
	var saved_cell: Vector2i = world.entity(1)["cell"]
	var write: Dictionary = lab.call("save_slot", 8)
	_check("visual_save_button_path_writes_slot", bool(write.get("ok", false)) and int(write.get("bytes", 0)) > 400)
	world.set_paused(false)
	world.advance(8)
	world.damage_unit(3, 999)
	_check("visual_fixture_mutates_after_save", world.state_hash() != saved_hash)
	var read: Dictionary = lab.call("load_slot", 8)
	_check("visual_load_button_path_restores_slot", bool(read.get("ok", false)))
	_check("visual_load_restores_exact_hash_hp_order_and_cell", world.state_hash() == saved_hash and int(world.entity(3)["health"]) == 59 and Vector2i(world.entity(1)["cell"]) == saved_cell)
	_check("visual_load_feedback_reports_hash", String((lab.get("feedback_label") as Label).text).contains(world.state_hash_text()))
	lab.call("cleanup_slot", 8)


func _test_pause_speed_and_map_switch(lab: Node) -> void:
	var world: RefCounted = lab.get("world")
	world.set_paused(false)
	lab.call("toggle_pause")
	var tick_before: int = int(world.tick_index)
	world.advance(4)
	_check("visual_pause_freezes_authority", world.paused and int(world.tick_index) == tick_before)
	lab.call("toggle_pause")
	_check("visual_speed_cycles_one_two_four", int(lab.call("cycle_speed")) == 2 and int(lab.call("cycle_speed")) == 4 and int(lab.call("cycle_speed")) == 1)
	for map_id: String in ["slate-foothills", "emberwood-edge", "sable-barrens", "verdant-crossing"]:
		_check("visual_switches_map_" + map_id, String(lab.call("switch_map", map_id)) == "" and String(lab.get("world").map_id) == map_id and lab.get("world").entity_ids().size() == 8)
	world = lab.get("world")
	_check("visual_map_switch_resets_clean_state", int(world.tick_index) == 0 and world.validate_state() == "", world.validate_state())


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("PASS %s%s" % [name, " " + detail if detail != "" else ""])
	else:
		failed += 1
		print("FAIL %s%s" % [name, " " + detail if detail != "" else ""])


func _finish() -> void:
	if failed == 0:
		print("STAGE8_VISUAL_PROOF PASS assertions=%d" % passed)
		quit(0)
	else:
		print("STAGE8_VISUAL_PROOF FAIL assertions=%d failed=%d" % [passed + failed, failed])
		quit(1)
