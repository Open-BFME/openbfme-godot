class_name Stage8Lab
extends Node2D
## Playable Stage 8 map/save/control-group/formation laboratory.

const WorldScript = preload("res://src/proof_stage8/proof_world.gd")
const SaveCodecScript = preload("res://src/proof_stage8/save_codec.gd")
const CELL: float = 32.0
const ORIGIN: Vector2 = Vector2(28, 170)

var world: RefCounted
var maps_document: Dictionary = {}
var selected_ids: Array[int] = []
var accumulator: float = 0.0
var feedback: String = ""
var map_selector: OptionButton
var status_label: Label
var selection_label: Label
var feedback_label: Label
var formation_buttons: Dictionary = {}
var group_buttons: Dictionary = {}
var save_button: Button
var load_button: Button
var pause_button: Button
var speed_button: Button


func _ready() -> void:
	_build_hud()
	var error: String = _load_maps()
	if error != "":
		set_feedback(error)
		return
	_configure_map(String(Dictionary(Array(maps_document["maps"])[0])["id"]))


func _process(delta: float) -> void:
	if world == null:
		return
	accumulator += minf(delta, 0.2)
	while accumulator >= 0.2:
		accumulator -= 0.2
		world.advance_frame()
	_refresh()


func _unhandled_input(event: InputEvent) -> void:
	if world == null:
		return
	if event is InputEventMouseButton:
		var mouse := event as InputEventMouseButton
		if mouse.pressed and mouse.button_index == MOUSE_BUTTON_LEFT:
			var id: int = pick_unit(mouse.position)
			if id != 0:
				select_units([id], mouse.shift_pressed)
			get_viewport().set_input_as_handled()
	if event is InputEventKey:
		var key := event as InputEventKey
		if not key.pressed or key.echo:
			return
		if key.keycode >= KEY_1 and key.keycode <= KEY_9:
			var group: int = key.keycode - KEY_0
			if key.ctrl_pressed:
				assign_group(group)
			else:
				recall_group(group)
		elif key.keycode == KEY_F5:
			save_slot(1)
		elif key.keycode == KEY_F9:
			load_slot(1)
		elif key.keycode == KEY_SPACE:
			toggle_pause()
		else:
			return
		get_viewport().set_input_as_handled()


func select_units(ids: Array[int], additive: bool = false) -> void:
	var next: Array[int] = []
	if additive:
		next = selected_ids.duplicate()
	for id: int in ids:
		var unit: Dictionary = world.entity(id)
		if not unit.is_empty() and int(unit.get("team", -1)) == 0 and bool(unit.get("alive", false)) and not next.has(id):
			next.append(id)
	next.sort()
	selected_ids = next
	set_feedback("Selected %d blue formations" % selected_ids.size())


func assign_group(group: int) -> Dictionary:
	var result: Dictionary = world.assign_control_group(group, selected_ids)
	set_feedback("Group %d assigned: %s" % [group, str(result.get("entity_ids", []))])
	return result


func recall_group(group: int) -> Array[int]:
	selected_ids = world.recall_control_group(group)
	set_feedback("Group %d recalled: %s" % [group, str(selected_ids)])
	return selected_ids


func order_formation(kind: String, anchor: Vector2i = Vector2i(-1, -1)) -> Dictionary:
	if anchor.x < 0:
		anchor = Vector2i(int(world.map_definition["widthCells"]) / 2, int(world.map_definition["heightCells"]) / 2)
	var result: Dictionary = world.order_formation(selected_ids, kind, anchor)
	set_feedback("%s formation %s" % [kind.capitalize(), "accepted" if bool(result.get("ok", false)) else "rejected: " + String(result.get("reason", ""))])
	return result


func save_slot(slot: int = 1) -> Dictionary:
	var result: Dictionary = world.save_slot(slot)
	set_feedback("Saved slot %d (%d bytes)" % [slot, int(result.get("bytes", 0))] if bool(result.get("ok", false)) else "Save rejected: " + String(result.get("reason", "")))
	return result


func load_slot(slot: int = 1) -> Dictionary:
	var result: Dictionary = world.load_slot(slot)
	selected_ids = world.recall_control_group(1)
	set_feedback("Loaded slot %d · exact hash %s" % [slot, world.state_hash_text()] if bool(result.get("ok", false)) else "Load rejected: " + String(result.get("reason", "")))
	return result


func toggle_pause() -> void:
	world.set_paused(not bool(world.paused))
	set_feedback("Simulation paused" if world.paused else "Simulation resumed")


func cycle_speed() -> int:
	var next: int = 2 if int(world.game_speed) == 1 else (4 if int(world.game_speed) == 2 else 1)
	world.set_game_speed(next)
	set_feedback("Game speed %dx" % next)
	return next


func switch_map(map_id: String) -> String:
	var error: String = _configure_map(map_id)
	set_feedback("Loaded map %s" % String(world.map_definition.get("displayName", map_id)) if error == "" else "Map rejected: " + error)
	return error


func pick_unit(screen_position: Vector2) -> int:
	if world == null:
		return 0
	var best: int = 0
	var best_distance: float = 18.0
	for id: int in world.entity_ids():
		var unit: Dictionary = world.entity(id)
		var distance: float = screen_position.distance_to(_cell_center(Vector2i(unit["cell"])))
		if distance < best_distance:
			best = id
			best_distance = distance
	return best


func set_feedback(text: String) -> void:
	feedback = text
	if feedback_label != null:
		feedback_label.text = text
	_refresh()


func cleanup_slot(slot: int = 1) -> void:
	SaveCodecScript.delete_slot(slot)


func _configure_map(id: String) -> String:
	world = WorldScript.new()
	var error: String = world.setup(maps_document, id)
	if error != "":
		return error
	var starts: Array = world.map_definition["startCells"]
	var blue_start: Array = starts[0]
	var red_start: Array = starts[1]
	for index: int in range(5):
		world.add_unit(0, Vector2i(int(blue_start[0]) + index % 3, int(blue_start[1]) - 1 + index / 3), 100)
	for index: int in range(3):
		world.add_unit(1, Vector2i(int(red_start[0]) - index, int(red_start[1])), 110)
	selected_ids = [1, 2, 3, 4, 5]
	world.assign_control_group(1, selected_ids)
	accumulator = 0.0
	_refresh()
	return ""


func _load_maps() -> String:
	var path: String = ProjectSettings.globalize_path("res://../content/openbfme-test/data/stage8_maps.json")
	var parser := JSON.new()
	if parser.parse(FileAccess.get_file_as_string(path)) != OK or typeof(parser.data) != TYPE_DICTIONARY:
		return "Stage 8 maps failed to load"
	maps_document = parser.data
	for value: Variant in Array(maps_document.get("maps", [])):
		var row: Dictionary = value
		map_selector.add_item(String(row["displayName"]))
		map_selector.set_item_metadata(map_selector.item_count - 1, String(row["id"]))
	return "" if map_selector.item_count == 4 else "Stage 8 requires four maps"


func _build_hud() -> void:
	var layer := CanvasLayer.new()
	add_child(layer)
	var root_control := Control.new()
	root_control.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	root_control.mouse_filter = Control.MOUSE_FILTER_IGNORE
	layer.add_child(root_control)
	var title := Label.new()
	title.position = Vector2(28, 22)
	title.text = "STAGE 8 · MAP / SAVE / FORMATION LAB"
	title.add_theme_font_size_override("font_size", 28)
	title.add_theme_color_override("font_color", Color("83d8ff"))
	root_control.add_child(title)
	status_label = Label.new()
	status_label.position = Vector2(28, 64)
	status_label.add_theme_font_size_override("font_size", 17)
	root_control.add_child(status_label)
	selection_label = Label.new()
	selection_label.position = Vector2(28, 96)
	selection_label.add_theme_color_override("font_color", Color("ffd36f"))
	root_control.add_child(selection_label)

	var panel := PanelContainer.new()
	panel.set_anchors_preset(Control.PRESET_RIGHT_WIDE)
	panel.offset_left = -610
	panel.offset_top = 22
	panel.offset_right = -22
	panel.offset_bottom = -22
	panel.mouse_filter = Control.MOUSE_FILTER_STOP
	root_control.add_child(panel)
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 10)
	panel.add_child(column)
	var heading := Label.new()
	heading.text = "Evening Skirmish Controls"
	heading.add_theme_font_size_override("font_size", 22)
	column.add_child(heading)
	map_selector = OptionButton.new()
	map_selector.item_selected.connect(func(index: int) -> void: switch_map(String(map_selector.get_item_metadata(index))))
	column.add_child(map_selector)
	var formation_row := HBoxContainer.new()
	for kind: String in ["line", "wedge", "column"]:
		var button := Button.new()
		button.text = kind.capitalize()
		button.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		button.pressed.connect(order_formation.bind(kind))
		formation_row.add_child(button)
		formation_buttons[kind] = button
	column.add_child(formation_row)
	var group_grid := GridContainer.new()
	group_grid.columns = 3
	for group: int in range(1, 10):
		var button := Button.new()
		button.text = "Recall %d" % group
		button.pressed.connect(recall_group.bind(group))
		group_grid.add_child(button)
		group_buttons[group] = button
	column.add_child(group_grid)
	var assign := Button.new()
	assign.text = "Assign Selection to Group 1 (Ctrl+1)"
	assign.pressed.connect(assign_group.bind(1))
	column.add_child(assign)
	var save_row := HBoxContainer.new()
	save_button = Button.new()
	save_button.text = "Save Slot 1 (F5)"
	save_button.pressed.connect(save_slot.bind(1))
	load_button = Button.new()
	load_button.text = "Load Slot 1 (F9)"
	load_button.pressed.connect(load_slot.bind(1))
	save_row.add_child(save_button)
	save_row.add_child(load_button)
	column.add_child(save_row)
	var time_row := HBoxContainer.new()
	pause_button = Button.new()
	pause_button.text = "Pause (Space)"
	pause_button.pressed.connect(toggle_pause)
	speed_button = Button.new()
	speed_button.text = "Cycle Speed"
	speed_button.pressed.connect(cycle_speed)
	time_row.add_child(pause_button)
	time_row.add_child(speed_button)
	column.add_child(time_row)
	var help := Label.new()
	help.text = "Click blue markers to select; Shift-click adds.\n1-9 recall groups, Ctrl+1-9 assigns.\nFormations preserve stable entity-ID slots.\nSave captures HP, orders, groups, clock, speed, map and stats."
	help.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	help.add_theme_color_override("font_color", Color("9eb7c4"))
	column.add_child(help)
	feedback_label = Label.new()
	feedback_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	feedback_label.add_theme_color_override("font_color", Color("9ef0b6"))
	column.add_child(feedback_label)


func _refresh() -> void:
	if world == null:
		return
	status_label.text = "%s · %dx · %s · TICK %d · HASH %s" % [String(world.map_definition.get("displayName", world.map_id)), int(world.game_speed), "PAUSED" if world.paused else "LIVE", int(world.tick_index), world.state_hash_text()]
	selection_label.text = "SELECTED %s · GROUP 1 %s · ORDERS %d · DISTANCE %d" % [str(selected_ids), str(world.recall_control_group(1)), int(world.stats["orders_issued"]), int(world.stats["distance_cells"])]
	pause_button.text = "Resume" if world.paused else "Pause (Space)"
	speed_button.text = "Speed %dx" % int(world.game_speed)
	queue_redraw()


func _draw() -> void:
	if world == null:
		return
	var width: int = int(world.map_definition["widthCells"])
	var height: int = int(world.map_definition["heightCells"])
	draw_rect(Rect2(ORIGIN, Vector2(width, height) * CELL), Color("0c1d29"), true)
	for y: int in range(height):
		for x: int in range(width):
			var cell := Vector2i(x, y)
			var rect := Rect2(ORIGIN + Vector2(cell) * CELL, Vector2.ONE * CELL)
			draw_rect(rect, Color("142d39") if (x + y) % 2 == 0 else Color("102733"), true)
			if world.is_blocked(cell):
				draw_rect(rect.grow(-3), Color("614846"), true)
			draw_rect(rect, Color(0.3, 0.55, 0.65, 0.18), false, 1)
	for value: Variant in Array(world.map_definition.get("resourceCells", [])):
		var pair: Array = value
		draw_circle(_cell_center(Vector2i(int(pair[0]), int(pair[1]))), 7, Color("f2cd62"))
	for id: int in world.entity_ids():
		var unit: Dictionary = world.entity(id)
		var center: Vector2 = _cell_center(Vector2i(unit["cell"]))
		var color := Color("55b8ff") if int(unit["team"]) == 0 else Color("ff716a")
		if not bool(unit["alive"]): color = Color("59636b")
		if selected_ids.has(id): draw_arc(center, 13, 0, TAU, 24, Color("ffd166"), 3)
		draw_circle(center, 9, color)
		draw_string(ThemeDB.fallback_font, center + Vector2(-6, 4), str(id), HORIZONTAL_ALIGNMENT_LEFT, -1, 11, Color("071018"))
		if String(unit["order"]) == "move":
			draw_line(center, _cell_center(Vector2i(unit["destination"])), Color(color, 0.45), 1.5)


func _cell_center(cell: Vector2i) -> Vector2:
	return ORIGIN + (Vector2(cell) + Vector2(0.5, 0.5)) * CELL
