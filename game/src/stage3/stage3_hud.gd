class_name Stage3Hud
extends Control
## Data-driven build palette and operational readout for the Stage 3 lab.

signal build_selected(definition_id: String)
signal rotate_requested
signal chain_commit_requested
signal chain_cancel_requested
signal gate_toggle_requested
signal reset_requested
signal menu_requested

var build_buttons: Dictionary = {}
var definition_costs: Dictionary = {}
var active_build_id: String = ""
var current_resources: int = 0

var build_list: VBoxContainer
var resource_label: Label
var mode_label: Label
var rotation_label: Label
var chain_label: Label
var commit_button: Button
var cancel_button: Button
var metrics_label: Label
var selection_label: Label
var status_label: Label
var toggle_gate_button: Button


func _ready() -> void:
	mouse_filter = Control.MOUSE_FILTER_IGNORE
	_build_header()
	_build_palette()
	_build_operations()
	_build_instructions()


func configure(definition_root: Dictionary) -> void:
	for child: Node in build_list.get_children():
		child.queue_free()
	build_buttons.clear()
	definition_costs.clear()
	var rows: Array = definition_root.get("structures", [])
	rows.sort_custom(func(left: Dictionary, right: Dictionary) -> bool: return int(left.get("buildMenuSlot", 0)) < int(right.get("buildMenuSlot", 0)))
	for raw_definition: Variant in rows:
		var definition: Dictionary = raw_definition
		var id: String = String(definition.get("id", ""))
		var display_name: String = String(definition.get("displayName", id.capitalize()))
		var cost: int = int(definition.get("cost", 0))
		var button := Button.new()
		button.name = "Build_%s" % id
		button.text = "%s    %d" % [display_name, cost]
		button.tooltip_text = _definition_tooltip(definition)
		button.toggle_mode = true
		button.custom_minimum_size = Vector2(0.0, 42.0)
		button.pressed.connect(_emit_build_selected.bind(id))
		build_list.add_child(button)
		build_buttons[id] = button
		definition_costs[id] = cost
	set_resources(current_resources)


func _emit_build_selected(definition_id: String) -> void:
	build_selected.emit(definition_id)


func set_resources(amount: int) -> void:
	current_resources = amount
	if resource_label != null:
		resource_label.text = "BUILD RESOURCES  %d" % amount
	for raw_id: Variant in build_buttons.keys():
		var id: String = String(raw_id)
		var button: Button = build_buttons[id]
		button.disabled = int(definition_costs.get(id, 0)) > amount


func set_build_mode(definition_id: String, rotation_quarters: int) -> void:
	active_build_id = definition_id
	for raw_id: Variant in build_buttons.keys():
		var id: String = String(raw_id)
		(build_buttons[id] as Button).button_pressed = id == definition_id
	mode_label.text = "MODE  %s" % ("Inspect / command" if definition_id == "" else definition_id.replace("_", " ").capitalize())
	rotation_label.text = "ROTATION  %d degrees  [R]" % (rotation_quarters * 90)


func set_chain_state(count: int) -> void:
	chain_label.text = "Pending wall chain: %d segment%s" % [count, "" if count == 1 else "s"]
	commit_button.disabled = active_build_id != "wall" or count == 0
	cancel_button.disabled = count == 0


func set_metrics(tick: int, revision: int, replans: int, local_edit_ms: float, visible_enemies: int, projectile_count: int) -> void:
	metrics_label.text = "SIMULATION\nTick             %d\nTopology rev     %d\nUnit replans     %d\nLast local edit  %.3f ms\nVisible enemies  %d\nProjectiles      %d" % [
		tick, revision, replans, local_edit_ms, visible_enemies, projectile_count
	]


func set_selection(text: String, gate_selected: bool = false) -> void:
	selection_label.text = "SELECTION\n%s" % text
	toggle_gate_button.disabled = not gate_selected


func set_status(text: String, is_error: bool = false) -> void:
	status_label.text = text
	status_label.add_theme_color_override("font_color", Color("ff817c") if is_error else Color("c9e4ef"))


func _build_header() -> void:
	var panel := _panel("Header", Rect2(280.0, 12.0, -602.0, 58.0), false)
	panel.anchor_right = 1.0
	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", 18)
	panel.add_child(row)
	var title := Label.new()
	title.text = "STAGE 3  FORTIFICATION + FOG LAB"
	title.add_theme_font_size_override("font_size", 24)
	title.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	row.add_child(title)
	var legal := Label.new()
	legal.text = "LEGAL-SAFE PRIMITIVES  /  DATA-DRIVEN"
	legal.add_theme_color_override("font_color", Color("7dd6c2"))
	legal.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	row.add_child(legal)


func _build_palette() -> void:
	var panel := _panel("BuildPanel", Rect2(14.0, 84.0, 256.0, -96.0), true)
	panel.anchor_bottom = 1.0
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 8)
	panel.add_child(column)
	var heading := Label.new()
	heading.text = "FORTIFICATION PALETTE"
	heading.add_theme_font_size_override("font_size", 18)
	column.add_child(heading)
	resource_label = Label.new()
	resource_label.name = "Resources"
	resource_label.text = "BUILD RESOURCES  0"
	resource_label.add_theme_color_override("font_color", Color("ffd36b"))
	column.add_child(resource_label)
	var rule := HSeparator.new()
	column.add_child(rule)
	build_list = VBoxContainer.new()
	build_list.name = "BuildButtons"
	build_list.add_theme_constant_override("separation", 6)
	column.add_child(build_list)
	mode_label = Label.new()
	mode_label.text = "MODE  Inspect / command"
	mode_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	column.add_child(mode_label)
	rotation_label = Label.new()
	rotation_label.text = "ROTATION  0 degrees  [R]"
	column.add_child(rotation_label)
	chain_label = Label.new()
	chain_label.text = "Pending wall chain: 0 segments"
	chain_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	column.add_child(chain_label)
	commit_button = Button.new()
	commit_button.name = "CommitChain"
	commit_button.text = "Commit wall chain  [Enter]"
	commit_button.disabled = true
	commit_button.pressed.connect(func() -> void: chain_commit_requested.emit())
	column.add_child(commit_button)
	cancel_button = Button.new()
	cancel_button.name = "CancelChain"
	cancel_button.text = "Cancel pending chain"
	cancel_button.disabled = true
	cancel_button.pressed.connect(func() -> void: chain_cancel_requested.emit())
	column.add_child(cancel_button)
	var rotate_button := Button.new()
	rotate_button.name = "Rotate"
	rotate_button.text = "Rotate quarter-turn  [R]"
	rotate_button.pressed.connect(func() -> void: rotate_requested.emit())
	column.add_child(rotate_button)


func _build_operations() -> void:
	var panel := _panel("OperationsPanel", Rect2(-308.0, 84.0, 294.0, -96.0), true)
	panel.anchor_left = 1.0
	panel.anchor_right = 1.0
	panel.anchor_bottom = 1.0
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 10)
	panel.add_child(column)
	var heading := Label.new()
	heading.text = "FIELD OPERATIONS"
	heading.add_theme_font_size_override("font_size", 18)
	column.add_child(heading)
	metrics_label = Label.new()
	metrics_label.name = "Metrics"
	metrics_label.text = "SIMULATION"
	metrics_label.add_theme_font_override("font", ThemeDB.fallback_font)
	column.add_child(metrics_label)
	column.add_child(HSeparator.new())
	selection_label = Label.new()
	selection_label.name = "Selection"
	selection_label.text = "SELECTION\nBlue scout"
	selection_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	column.add_child(selection_label)
	toggle_gate_button = Button.new()
	toggle_gate_button.name = "ToggleGate"
	toggle_gate_button.text = "Toggle selected gate  [Space]"
	toggle_gate_button.disabled = true
	toggle_gate_button.pressed.connect(func() -> void: gate_toggle_requested.emit())
	column.add_child(toggle_gate_button)
	column.add_child(HSeparator.new())
	var reset_button := Button.new()
	reset_button.name = "Reset"
	reset_button.text = "Reset lab"
	reset_button.pressed.connect(func() -> void: reset_requested.emit())
	column.add_child(reset_button)
	var menu_button := Button.new()
	menu_button.name = "Menu"
	menu_button.text = "Return to main menu  [Esc]"
	menu_button.pressed.connect(func() -> void: menu_requested.emit())
	column.add_child(menu_button)
	status_label = Label.new()
	status_label.name = "Status"
	status_label.text = "Ready"
	status_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	status_label.size_flags_vertical = Control.SIZE_EXPAND_FILL
	column.add_child(status_label)


func _build_instructions() -> void:
	var panel := _panel("Instructions", Rect2(280.0, -78.0, -602.0, 66.0), false)
	panel.anchor_top = 1.0
	panel.anchor_right = 1.0
	panel.anchor_bottom = 1.0
	var label := Label.new()
	label.text = "CLICK blue scout to select  /  RIGHT-CLICK to move  /  R rotates  /  WALL: multi-click adjacent cells, then Enter  /  WALL TOWER: click a friendly wall  /  CLICK GATE then Space toggles it\nOpen blue gate admits blue only; the red scout remains blocked. Move through the gate to reveal the hidden east field and its automatic tower."
	label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	panel.add_child(label)


func _definition_tooltip(definition: Dictionary) -> String:
	var parts: Array[String] = ["HP %d" % int(definition.get("maximumHealth", 0))]
	if bool(definition.get("canFire", false)):
		parts.append("Range %d" % int(definition.get("rangeCells", 0)))
		parts.append("Damage %d" % int(definition.get("damage", 0)))
	if bool(definition.get("attachment", false)):
		parts.append("Attach to: %s" % ", ".join(definition.get("compatibleBaseKinds", [])))
	return "  /  ".join(parts)


func _panel(name: String, offsets: Rect2, padded: bool) -> PanelContainer:
	var panel := PanelContainer.new()
	panel.name = name
	panel.offset_left = offsets.position.x
	panel.offset_top = offsets.position.y
	panel.offset_right = offsets.position.x + offsets.size.x
	panel.offset_bottom = offsets.position.y + offsets.size.y
	var style := StyleBoxFlat.new()
	style.bg_color = Color(0.055, 0.083, 0.12, 0.96)
	style.border_color = Color(0.24, 0.43, 0.52, 0.92)
	style.set_border_width_all(1)
	style.corner_radius_top_left = 7
	style.corner_radius_top_right = 7
	style.corner_radius_bottom_left = 7
	style.corner_radius_bottom_right = 7
	if padded:
		style.content_margin_left = 14.0
		style.content_margin_right = 14.0
		style.content_margin_top = 14.0
		style.content_margin_bottom = 14.0
	else:
		style.content_margin_left = 12.0
		style.content_margin_right = 12.0
		style.content_margin_top = 8.0
		style.content_margin_bottom = 8.0
	panel.add_theme_stylebox_override("panel", style)
	add_child(panel)
	return panel
