extends SceneTree
## godot --headless --path game -s res://tests/stage5_visual_runner.gd

var passed: int = 0
var failed: int = 0


const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
# Turns a GDScript runtime error inside `_run` — which unwinds past every
# `quit()` and would otherwise leave this headless process idling forever —
# into a loud non-zero exit. See tests/runner_watchdog.gd.
var _runner_watchdog := RunnerWatchdogScript.new()


func _initialize() -> void:
	_runner_watchdog.start(self, "STAGE5_VISUAL_RUNNER")
	call_deferred("_run")


func _run() -> void:
	var packed: PackedScene = load("res://scenes/stage5_lab.tscn")
	_check("stage5_scene_loads", packed != null)
	if packed == null:
		_finish()
		return
	var arena: Node = packed.instantiate()
	root.add_child(arena)
	current_scene = arena
	await process_frame
	await process_frame
	await process_frame
	var world: RefCounted = arena.get("world")
	var board: Node2D = arena.get_node("Board")
	var hud: CanvasLayer = arena.get_node("Hud")
	_check("stage5_scene_instantiates_real_lab", world != null and board != null and hud != null and String(arena.get("definition_error")) == "")
	if world == null:
		_finish()
		return
	arena.call("set_simulation_paused", true)
	_test_external_tree_and_presentation(arena, world, board, hud)
	_test_shared_target_state_machine(arena, hud)
	_test_global_weather_building_and_rewards(arena, hud)
	var final_world: RefCounted = arena.get("world")
	_check("stage5_visual_world_state_valid", final_world.validate_state() == "", final_world.validate_state())

	var prior_arena: WeakRef = weakref(arena)
	arena.call("_return_to_menu")
	await process_frame
	await process_frame
	await process_frame
	_check("stage5_menu_return_loads_boot", current_scene != null and current_scene.scene_file_path == "res://scenes/boot.tscn")
	_check("stage5_menu_return_releases_lab", prior_arena.get_ref() == null)
	if current_scene != null:
		current_scene.queue_free()
		current_scene = null
	await process_frame
	await process_frame
	_finish()


func _test_external_tree_and_presentation(arena: Node, world: RefCounted, board: Node2D, hud: CanvasLayer) -> void:
	var document: Dictionary = arena.get("definition_document")
	var definitions: Array = arena.get("power_definitions")
	_check("visual_uses_external_powers_contract", String(document.get("schema", "")) == "openbfme.powers" and int(document.get("rulesVersion", 0)) == 1 and int(Dictionary(document["rules"]).get("unitDefeatPowerPoints", 0)) == 1 and int(Dictionary(document["rules"]).get("buildingDefeatPowerPoints", 0)) == 2)
	_check("visual_binds_all_external_powers", definitions.size() == 7 and Dictionary(hud.get("power_buttons")).size() == 7)
	var tier_columns: Dictionary = hud.get("tier_columns")
	_check("visual_builds_four_column_tier_tree", tier_columns.size() == 4 and tier_columns.has(1) and tier_columns.has(2) and tier_columns.has(3) and tier_columns.has(4))
	var tier_shapes_ok: bool = true
	for tier: int in range(1, 5):
		var column := tier_columns[tier] as VBoxContainer
		var expected_children: int = 3 if tier < 4 else 2
		tier_shapes_ok = tier_shapes_ok and column != null and column.get_child_count() == expected_children and String((column.get_child(0) as Label).text).contains("TIER %d" % tier)
	_check("visual_tier_tree_groups_prerequisite_levels", tier_shapes_ok)
	var buttons_readable: bool = true
	for raw_button: Variant in Dictionary(hud.get("power_buttons")).values():
		var button := raw_button as Button
		buttons_readable = buttons_readable and button != null and button.custom_minimum_size.x >= 150 and button.custom_minimum_size.y >= 88 and button.tooltip_text.contains("Spend gate")
	_check("visual_tier_buttons_have_readable_layout", buttons_readable)
	_check("visual_board_renders_declared_grid", board.call("board_size") == Vector2(768, 480) and int(world.width) == 16 and int(world.height) == 10)
	_check("visual_lab_spawns_units_and_buildings", world.entity_ids() == [100, 101, 102, 103, 1000, 1001] and String(world.entity(1000)["kind"]) == "building" and String(world.entity(102)["kind"]) == "unit")
	var text: String = String((hud.get("points_label") as Label).text) + String((hud.get("weather_label") as Label).text) + String((hud.get("targeting_label") as Label).text)
	_check("visual_status_text_is_ascii_clean", text.contains("AVAILABLE 2") and text.contains("GLOBAL WEATHER: clear") and not text.contains("Ã"))
	_check("visual_hash_is_exposed_in_hud", String((hud.get("sim_label") as Label).text).contains("HASH ") and String((hud.get("sim_label") as Label).text).contains(String(world.state_hash_text())))


func _test_shared_target_state_machine(arena: Node, hud: CanvasLayer) -> void:
	arena.call("reset_lab")
	arena.call("set_simulation_paused", true)
	var world: RefCounted = arena.get("world")
	var buttons: Dictionary = hud.get("power_buttons")
	(buttons[3] as Button).emit_signal("pressed")
	_check("visual_prerequisite_rejection_is_readable", String((hud.get("feedback_label") as Label).text).contains("prerequisite locked"))
	(buttons[1] as Button).emit_signal("pressed")
	(buttons[2] as Button).emit_signal("pressed")
	_check("visual_tier_one_buttons_spend_starting_points", int(world.team_state(0)["available_points"]) == 0 and int(world.team_state(0)["spent_points"]) == 2)
	(hud.get("xp_button") as Button).emit_signal("pressed")
	(hud.get("xp_button") as Button).emit_signal("pressed")
	(buttons[3] as Button).emit_signal("pressed")
	_check("visual_xp_unlocks_next_tier_without_softlock", world.is_power_unlocked(0, 3) and int(world.team_state(0)["spent_points"]) == 4)
	(buttons[3] as Button).emit_signal("pressed")
	_check("visual_position_mode_uses_shared_arming_state", int(arena.get("armed_power_code")) == 3 and String(board_mode(arena)).contains("position") and String((hud.get("targeting_label") as Label).text).contains("POSITION"))
	var area: Dictionary = arena.call("handle_cell_clicked", Vector2i(10, 4))
	_check("visual_position_mode_resolves_through_shared_state", bool(area.get("ok", false)) and int(arena.get("armed_power_code")) == 0 and Array(area.get("affected_ids", [])) == [102, 103])
	_check("visual_area_damage_updates_board_entities", int(world.entity(102)["health"]) == 880 and int(world.entity(103)["health"]) == 680 and String((buttons[3] as Button).text).contains("COOLDOWN 8"))

	arena.call("reset_lab")
	arena.call("set_simulation_paused", true)
	world = arena.get("world")
	buttons = hud.get("power_buttons")
	(buttons[1] as Button).emit_signal("pressed")
	(buttons[1] as Button).emit_signal("pressed")
	_check("visual_entity_mode_uses_same_armed_state", int(arena.get("armed_power_code")) == 1 and String(board_mode(arena)) == "friendly_entity")
	var heal: Dictionary = arena.call("handle_cell_clicked", Vector2i(3, 3))
	_check("visual_friendly_entity_target_resolves", bool(heal.get("ok", false)) and int(world.entity(100)["health"]) == 890 and int(arena.get("armed_power_code")) == 0)
	(buttons[2] as Button).emit_signal("pressed")
	(buttons[2] as Button).emit_signal("pressed")
	var strike: Dictionary = arena.call("handle_cell_clicked", Vector2i(11, 3))
	_check("visual_hostile_entity_target_resolves", bool(strike.get("ok", false)) and int(world.entity(102)["health"]) == 820)
	_check("visual_cooldown_rejection_is_authoritative", _reason(arena.call("request_cast", 2, {"entity_id": 102})) == "cooldown")


func _test_global_weather_building_and_rewards(arena: Node, hud: CanvasLayer) -> void:
	arena.call("reset_lab")
	arena.call("set_simulation_paused", true)
	var world: RefCounted = arena.get("world")
	world.grant_power_points(0, 20)
	for code: int in [1, 2, 3, 4, 5, 6, 7]:
		arena.call("request_unlock", code)
	var global_result: Dictionary = arena.call("request_power_action", 5)
	_check("visual_global_mode_uses_shared_arm_and_resolve_path", bool(global_result.get("ok", false)) and int(arena.get("armed_power_code")) == 0 and not Dictionary(world.weather).is_empty())
	_check("visual_weather_feedback_is_readable", String((hud.get("weather_label") as Label).text).contains("TEMPEST") and String((hud.get("weather_label") as Label).text).contains("4 TICKS LEFT"))
	var red_before: int = int(world.entity(102)["health"])
	arena.call("advance_ticks", 1)
	_check("visual_weather_tick_updates_world_and_hud", int(world.entity(102)["health"]) == red_before - 25 and String((hud.get("weather_label") as Label).text).contains("3 TICKS LEFT"))

	var building_action: Dictionary = arena.call("request_power_action", 6)
	_check("visual_building_mode_arms_shared_state", bool(building_action.get("ok", false)) and int(arena.get("armed_power_code")) == 6 and String(board_mode(arena)) == "hostile_building")
	var siege: Dictionary = arena.call("handle_cell_clicked", Vector2i(12, 6))
	_check("visual_building_damage_resolves_from_board", bool(siege.get("ok", false)) and int(world.entity(1001)["health"]) == 1440)

	world.apply_damage(100, 300)
	world.apply_damage(1000, 300)
	var renewal: Dictionary = arena.call("request_power_action", 7)
	_check("visual_global_heal_uses_shared_global_path", bool(renewal.get("ok", false)) and int(arena.get("armed_power_code")) == 0 and int(world.entity(100)["health"]) == 530 and int(world.entity(1000)["health"]) == 820)

	arena.call("reset_lab")
	arena.call("set_simulation_paused", true)
	world = arena.get("world")
	var buttons: Dictionary = hud.get("power_buttons")
	(buttons[2] as Button).emit_signal("pressed")
	world.apply_damage(int(arena.get("red_unit_id")), 850)
	var points_before: int = int(world.team_state(0)["available_points"])
	(buttons[2] as Button).emit_signal("pressed")
	var defeat: Dictionary = arena.call("handle_cell_clicked", Vector2i(11, 3))
	_check("visual_unit_defeat_awards_power_point", bool(defeat.get("ok", false)) and int(defeat.get("power_points_earned", 0)) == 1 and int(world.team_state(0)["available_points"]) == points_before + 1)
	_check("visual_power_point_reward_updates_hud", String((hud.get("points_label") as Label).text).contains("AVAILABLE %d" % (points_before + 1)))


func board_mode(arena: Node) -> String:
	return String((arena.get_node("Board") as Node2D).get("targeting_mode"))


func _reason(result: Dictionary) -> String:
	return String(result.get("reason", ""))


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("PASS %s%s" % [name, " " + detail if detail != "" else ""])
	else:
		failed += 1
		print("FAIL %s%s" % [name, " " + detail if detail != "" else ""])


func _finish() -> void:
	if failed == 0:
		print("STAGE5_VISUAL_PROOF PASS assertions=%d" % passed)
		quit(0)
	else:
		print("STAGE5_VISUAL_PROOF FAIL assertions=%d failed=%d" % [passed + failed, failed])
		quit(1)
