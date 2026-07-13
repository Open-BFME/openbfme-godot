class_name Stage1Hud
extends CanvasLayer
## Presentation-only HUD for the Stage 1 battle sandbox.

signal attack_move_pressed
signal stop_pressed
signal restart_pressed
signal menu_pressed

var objective_label: Label
var status_label: Label
var selection_label: Label
var command_label: Label
var winner_panel: PanelContainer
var winner_label: Label
var selection_box: Stage1SelectionBox

func _ready() -> void:
	_build_ui()

func _build_ui() -> void:
	var root := Control.new()
	root.name = "Root"
	root.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	root.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(root)

	var top_panel := PanelContainer.new()
	top_panel.name = "TopPanel"
	top_panel.set_anchors_preset(Control.PRESET_TOP_WIDE)
	top_panel.offset_bottom = 58.0
	top_panel.mouse_filter = Control.MOUSE_FILTER_IGNORE
	root.add_child(top_panel)
	var top_margin := MarginContainer.new()
	top_margin.add_theme_constant_override("margin_left", 16)
	top_margin.add_theme_constant_override("margin_right", 16)
	top_margin.add_theme_constant_override("margin_top", 8)
	top_margin.add_theme_constant_override("margin_bottom", 8)
	top_panel.add_child(top_margin)
	var top_row := HBoxContainer.new()
	top_margin.add_child(top_row)
	objective_label = Label.new()
	objective_label.text = "STAGE 1  |  Destroy the red fortress"
	objective_label.add_theme_font_size_override("font_size", 19)
	top_row.add_child(objective_label)
	var spacer := Control.new()
	spacer.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	top_row.add_child(spacer)
	status_label = Label.new()
	status_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	status_label.text = "Tick 0  |  Hash --------"
	top_row.add_child(status_label)

	var side_strip := HBoxContainer.new()
	side_strip.name = "SideStrip"
	side_strip.set_anchors_preset(Control.PRESET_TOP_WIDE)
	side_strip.offset_left = 12.0
	side_strip.offset_top = 70.0
	side_strip.offset_right = -12.0
	side_strip.offset_bottom = 112.0
	side_strip.mouse_filter = Control.MOUSE_FILTER_IGNORE
	root.add_child(side_strip)
	var blue := _side_badge("BLUE GUARD", Color(0.12, 0.38, 0.78, 0.94))
	side_strip.add_child(blue)
	var mid := Control.new()
	mid.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	side_strip.add_child(mid)
	var red := _side_badge("RED RAIDERS", Color(0.72, 0.16, 0.14, 0.94))
	side_strip.add_child(red)

	var command_panel := PanelContainer.new()
	command_panel.name = "CommandPanel"
	command_panel.set_anchors_preset(Control.PRESET_BOTTOM_WIDE)
	command_panel.offset_left = 10.0
	command_panel.offset_top = -112.0
	command_panel.offset_right = -10.0
	command_panel.offset_bottom = -10.0
	command_panel.mouse_filter = Control.MOUSE_FILTER_STOP
	root.add_child(command_panel)
	var command_margin := MarginContainer.new()
	command_margin.add_theme_constant_override("margin_left", 12)
	command_margin.add_theme_constant_override("margin_right", 12)
	command_margin.add_theme_constant_override("margin_top", 10)
	command_margin.add_theme_constant_override("margin_bottom", 10)
	command_panel.add_child(command_margin)
	var command_row := HBoxContainer.new()
	command_row.add_theme_constant_override("separation", 10)
	command_margin.add_child(command_row)
	var info_col := VBoxContainer.new()
	info_col.custom_minimum_size.x = 340.0
	command_row.add_child(info_col)
	selection_label = Label.new()
	selection_label.text = "No hordes selected"
	selection_label.add_theme_font_size_override("font_size", 18)
	info_col.add_child(selection_label)
	command_label = Label.new()
	command_label.text = "LMB select/drag  |  RMB move/attack  |  A attack-move  |  S stop"
	command_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	info_col.add_child(command_label)
	var grow := Control.new()
	grow.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	command_row.add_child(grow)
	command_row.add_child(_command_button("A-Move", func() -> void: attack_move_pressed.emit()))
	command_row.add_child(_command_button("Stop", func() -> void: stop_pressed.emit()))
	command_row.add_child(_command_button("Restart", func() -> void: restart_pressed.emit()))
	command_row.add_child(_command_button("Menu", func() -> void: menu_pressed.emit()))

	winner_panel = PanelContainer.new()
	winner_panel.set_anchors_preset(Control.PRESET_CENTER)
	winner_panel.offset_left = -250.0
	winner_panel.offset_top = -90.0
	winner_panel.offset_right = 250.0
	winner_panel.offset_bottom = 90.0
	winner_panel.visible = false
	winner_panel.mouse_filter = Control.MOUSE_FILTER_STOP
	root.add_child(winner_panel)
	var win_col := VBoxContainer.new()
	win_col.alignment = BoxContainer.ALIGNMENT_CENTER
	winner_panel.add_child(win_col)
	winner_label = Label.new()
	winner_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	winner_label.add_theme_font_size_override("font_size", 30)
	win_col.add_child(winner_label)
	var win_help := Label.new()
	win_help.text = "Press R to restart or Esc for the menu"
	win_help.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	win_col.add_child(win_help)

	selection_box = Stage1SelectionBox.new()
	selection_box.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	root.add_child(selection_box)
	root.move_child(selection_box, 0)
	_set_mouse_filter_recursive(top_panel, Control.MOUSE_FILTER_IGNORE)
	_set_mouse_filter_recursive(side_strip, Control.MOUSE_FILTER_IGNORE)

func _set_mouse_filter_recursive(node: Node, filter: int) -> void:
	if node is Control:
		(node as Control).mouse_filter = filter
	for child in node.get_children():
		_set_mouse_filter_recursive(child, filter)

func _side_badge(text_value: String, color: Color) -> PanelContainer:
	var panel := PanelContainer.new()
	panel.custom_minimum_size = Vector2(184.0, 42.0)
	var style := StyleBoxFlat.new()
	style.bg_color = color
	style.corner_radius_top_left = 6
	style.corner_radius_top_right = 6
	style.corner_radius_bottom_left = 6
	style.corner_radius_bottom_right = 6
	panel.add_theme_stylebox_override("panel", style)
	var label := Label.new()
	label.text = text_value
	label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	label.add_theme_font_size_override("font_size", 17)
	panel.add_child(label)
	return panel

func _command_button(text_value: String, callback: Callable) -> Button:
	var button := Button.new()
	button.text = text_value
	button.custom_minimum_size = Vector2(104.0, 48.0)
	button.pressed.connect(callback)
	return button

func set_sim_status(tick: int, hash_text: String, blue_alive: int, red_alive: int) -> void:
	status_label.text = "Tick %d  |  Hash %s  |  Blue %d / Red %d" % [tick, hash_text, blue_alive, red_alive]

func set_selection(count: int, order_text: String = "") -> void:
	selection_label.text = "%d horde%s selected" % [count, "" if count == 1 else "s"] if count > 0 else "No hordes selected"
	if order_text != "":
		command_label.text = order_text

func set_command_hint(text_value: String) -> void:
	command_label.text = text_value

func show_winner(side_name: String) -> void:
	winner_label.text = "%s VICTORY" % side_name.to_upper()
	winner_panel.visible = true

func hide_winner() -> void:
	winner_panel.visible = false
