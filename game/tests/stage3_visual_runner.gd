extends SceneTree
## Instantiates and drives the real Stage 3 lab scene headlessly.

const WorldScript = preload("res://src/proof_stage3/proof_world.gd")
const StructureScript = preload("res://src/proof_stage3/structure_system.gd")
const LabScript = preload("res://src/stage3/stage3_lab.gd")
const BoardScript = preload("res://src/stage3/stage3_board.gd")
const HudScript = preload("res://src/stage3/stage3_hud.gd")

var passed: int = 0
var failed: int = 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var packed := load("res://scenes/stage3_lab.tscn") as PackedScene
	_check("real_stage3_scene_loads", packed != null)
	if packed == null:
		_finish()
		return
	var lab := packed.instantiate() as LabScript
	root.add_child(lab)
	current_scene = lab
	await process_frame
	lab.process_mode = Node.PROCESS_MODE_DISABLED
	var board := lab.get_node("Board") as BoardScript
	var hud := lab.get_node("Stage3Hud") as HudScript
	_check("real_stage3_scene_ready", lab.world != null and board != null and hud != null)
	_check("real_defenses_file_loaded", String(lab.definitions_root.get("schema", "")) == "openbfme.defenses" and (lab.definitions_root.get("structures", []) as Array).size() == 4)
	_check("data_driven_build_buttons", hud.build_buttons.size() == 4 and hud.build_buttons.has("wall") and (hud.build_buttons["tower"] as Button).text.contains("Sentinel Tower"))
	(hud.build_buttons["wall"] as Button).emit_signal("pressed")
	_check("build_button_binds_its_definition", lab.build_definition_id == "wall")
	(hud.build_buttons["tower"] as Button).emit_signal("pressed")
	_check("each_build_button_keeps_distinct_definition", lab.build_definition_id == "tower")
	(hud.build_buttons["tower"] as Button).emit_signal("pressed")
	_check("active_build_button_toggles_back_to_command_mode", lab.build_definition_id == "")
	lab.select_build("tower")
	var occupied_resources_before: int = lab.world.structures.resources_for(WorldScript.TEAM_BLUE)
	var occupied_revision_before: int = lab.world.topology.revision
	var occupied_result: Dictionary = lab.place_at_cell(Vector2i(5, 7))
	_check("blocking_structure_rejects_live_unit_cell", not bool(occupied_result.get("ok", false)) and String(occupied_result.get("error", "")) == "live_unit_occupied")
	_check("unit_occupied_rejection_is_transactional", lab.world.structures.resources_for(WorldScript.TEAM_BLUE) == occupied_resources_before and lab.world.topology.revision == occupied_revision_before)
	lab.select_build("tower")
	_check("legal_safe_canvas_uses_no_sprite_assets", _has_no_sprite_nodes(lab))
	_check("board_cell_mapping_round_trips", board.screen_to_cell(board.cell_to_screen(Vector2i(6, 4))) == Vector2i(6, 4))

	# The initial east field is genuinely hidden, then reveal and hide it again.
	var initial_snapshot: Dictionary = lab.world.team_filtered_snapshot(WorldScript.TEAM_BLUE)
	_check("initial_enemy_unit_hidden_by_fog", not _snapshot_has_id(initial_snapshot.get("units", []), lab.red_unit_id))
	_check("initial_enemy_tower_hidden_by_fog", not _snapshot_has_id(initial_snapshot.get("structures", []), lab.red_tower_id))
	lab.world.set_unit_cell(lab.blue_unit_id, Vector2i(15, 7))
	lab.world.recompute_visibility()
	var revealed_snapshot: Dictionary = lab.world.team_filtered_snapshot(WorldScript.TEAM_BLUE)
	_check("blue_vision_reveals_enemy_unit", _snapshot_has_id(revealed_snapshot.get("units", []), lab.red_unit_id))
	_check("blue_vision_reveals_enemy_tower", _snapshot_has_id(revealed_snapshot.get("structures", []), lab.red_tower_id))
	lab.world.set_unit_cell(lab.blue_unit_id, Vector2i(5, 7))
	lab.world.recompute_visibility()
	var hidden_again: Dictionary = lab.world.team_filtered_snapshot(WorldScript.TEAM_BLUE)
	_check("fog_exploration_persists_after_hide", lab.world.vision.is_explored(WorldScript.TEAM_BLUE, Vector2i(18, 7)) and not lab.world.vision.is_visible(WorldScript.TEAM_BLUE, Vector2i(18, 7)))
	_check("team_snapshot_hides_enemy_again", not _snapshot_has_id(hidden_again.get("units", []), lab.red_unit_id))

	# Open owner gate routes blue but blocks red, then closing it causes a deterministic replan.
	lab.place_at_cell(Vector2i(5, 7))
	var blue_route: bool = lab.move_selected_to(Vector2i(18, 7))
	var blue: WorldScript.UnitRecord = lab.world.get_unit(lab.blue_unit_id)
	var red: WorldScript.UnitRecord = lab.world.get_unit(lab.red_unit_id)
	_check("open_owner_gate_routes_blue", blue_route and blue.path.has(Vector2i(12, 7)))
	_check("open_owner_gate_blocks_red", not lab.world.order_move(red.id, Vector2i(5, 7)) and red.path.is_empty())
	lab.step_sim(1)
	lab.place_at_cell(Vector2i(12, 7))
	var revision_before_close: int = lab.world.topology.revision
	_check("real_scene_selects_owner_gate", lab.selected_gate_id == lab.demonstration_gate_id)
	_check("real_scene_gate_toggle_closes", lab.toggle_selected_gate() and not lab.world.structures.get_structure(lab.demonstration_gate_id).gate_open)
	lab.step_sim(1)
	_check("gate_topology_change_replans_blue", lab.world.topology.revision == revision_before_close + 1 and blue.replan_count == 1 and blue.path.is_empty())
	_check("real_scene_gate_toggle_reopens", lab.toggle_selected_gate() and lab.world.structures.get_structure(lab.demonstration_gate_id).gate_open)

	# Exercise the real keyboard rotation and multi-click board chain workflow.
	lab.select_build("wall")
	var rotate_key := InputEventKey.new()
	rotate_key.keycode = KEY_R
	rotate_key.pressed = true
	lab._unhandled_input(rotate_key)
	_check("keyboard_quarter_rotation", lab.build_rotation == 1)
	var resources_before_chain: int = lab.world.structures.resources_for(WorldScript.TEAM_BLUE)
	_send_board_click(board, Vector2i(3, 11), MOUSE_BUTTON_LEFT)
	_send_board_click(board, Vector2i(3, 12), MOUSE_BUTTON_LEFT)
	_check("board_multiclick_queues_wall_chain", lab.pending_chain == [Vector2i(3, 11), Vector2i(3, 12)])
	var chain_result: Dictionary = lab.commit_chain()
	var chain_ids: Array = chain_result.get("ids", [])
	_check("real_scene_commits_snapped_chain", bool(chain_result.get("ok", false)) and chain_ids.size() == 2)
	var first_wall: StructureScript.StructureRecord = lab.world.structures.get_structure(int(chain_ids[0]))
	_check("real_scene_chain_keeps_rotation", first_wall != null and first_wall.rotation_quarters == 1 and first_wall.cell == Vector2i(3, 11))
	_check("real_scene_chain_charges_resources", lab.world.structures.resources_for(WorldScript.TEAM_BLUE) == resources_before_chain - 80)

	# Attach the data-driven wall tower and prove it launches a projectile.
	lab.select_build("wall_tower")
	var topology_before_attachment: int = lab.world.topology.revision
	var attachment_result: Dictionary = lab.place_at_cell(Vector2i(3, 11))
	var attachment: StructureScript.StructureRecord = lab.world.structures.get_structure(int(attachment_result.get("id", 0)))
	_check("real_scene_attaches_wall_tower", bool(attachment_result.get("ok", false)) and attachment != null and attachment.attach_to_id == first_wall.id)
	_check("attachment_preserves_walk_topology", lab.world.topology.revision == topology_before_attachment)
	lab.world.set_unit_cell(lab.red_unit_id, Vector2i(5, 11))
	lab.step_sim(1)
	_check("attached_tower_auto_targets", attachment.last_fired_target_id == lab.red_unit_id)
	_check("attached_tower_spawns_projectile", lab.world.structures.active_projectile_count() > 0)

	# Place a standalone tower and a new snapped gate through the real build controls.
	lab.select_build("tower")
	var tower_result: Dictionary = lab.place_at_cell(Vector2i(7, 10))
	var player_tower: StructureScript.StructureRecord = lab.world.structures.get_structure(int(tower_result.get("id", 0)))
	_check("real_scene_places_standalone_tower", bool(tower_result.get("ok", false)) and player_tower != null and player_tower.kind == "tower")
	lab.world.set_unit_cell(lab.red_unit_id, Vector2i(9, 10))
	lab.step_sim(1)
	_check("standalone_tower_auto_targets", player_tower.last_fired_target_id == lab.red_unit_id)
	lab.select_build("gate")
	var gate_result: Dictionary = lab.place_at_cell(Vector2i(6, 12))
	var placed_gate: StructureScript.StructureRecord = lab.world.structures.get_structure(int(gate_result.get("id", 0)))
	_check("real_scene_places_gate", bool(gate_result.get("ok", false)) and placed_gate != null and not placed_gate.gate_open)
	lab.select_build("gate") # Toggle back to inspect mode.
	lab.place_at_cell(Vector2i(6, 12))
	_check("placed_gate_can_be_selected_and_opened", lab.selected_gate_id == placed_gate.id and lab.toggle_selected_gate() and placed_gate.gate_open)
	_check("lab_reports_fast_local_edit", lab.last_local_edit_ms < 100.0, "local_edit_ms=%.3f" % lab.last_local_edit_ms)
	lab.refresh_presentation()
	_check("hud_reports_topology_and_resources", (hud.metrics_label.text.contains("Topology rev") and hud.resource_label.text.contains(str(lab.world.structures.resources_for(WorldScript.TEAM_BLUE)))))

	# Exercise the actual menu transition and verify the Stage 3 entry survives it.
	lab.process_mode = Node.PROCESS_MODE_INHERIT
	lab._return_to_menu()
	await process_frame
	await process_frame
	var boot: Node = current_scene
	_check("menu_return_loads_real_boot_scene", boot != null and boot.name == "Boot")
	_check("main_menu_contains_stage3_entry", boot != null and boot.get_node_or_null("Center/Stage3") is Button)
	_check("stage3_scene_released_on_menu_return", not is_instance_valid(lab))
	if boot != null and is_instance_valid(boot):
		current_scene = null
		boot.free()
	await process_frame
	await process_frame
	_finish()


func _send_board_click(board: BoardScript, cell: Vector2i, button: MouseButton) -> void:
	var event := InputEventMouseButton.new()
	event.button_index = button
	event.pressed = true
	event.position = board.cell_to_screen(cell)
	board._gui_input(event)


func _snapshot_has_id(entries: Array, entity_id: int) -> bool:
	for raw_entry: Variant in entries:
		var entry: Dictionary = raw_entry
		if int(entry.get("id", 0)) == entity_id:
			return true
	return false


func _has_no_sprite_nodes(node: Node) -> bool:
	if node is Sprite2D or node is Sprite3D or node is TextureRect:
		return false
	for child: Node in node.get_children():
		if not _has_no_sprite_nodes(child):
			return false
	return true


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("PASS %s%s" % [name, " " + detail if detail != "" else ""])
	else:
		failed += 1
		print("FAIL %s%s" % [name, " " + detail if detail != "" else ""])


func _finish() -> void:
	if failed == 0:
		print("STAGE3_VISUAL_PROOF PASS assertions=%d" % passed)
		quit(0)
	else:
		print("STAGE3_VISUAL_PROOF FAIL assertions=%d failed=%d" % [passed + failed, failed])
		quit(1)
