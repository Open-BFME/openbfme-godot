class_name Stage2Hud
extends Stage1Hud
## Bundle-driven build/train controls and readable integer economy state.

signal build_requested(type_code: int)
signal train_requested(type_code: int)
signal rally_requested

var economy_label: Label
var stage2_hint: Label
var build_row: HBoxContainer
var train_row: HBoxContainer
var _building_defs: Array[Dictionary] = []
var _blueprint_defs: Dictionary = {}
var _shown_building_id: int = 0
var _shown_complete: bool = false
var _shown_trains: String = ""
var _building_title: Label
var _maximum_queue_length: int = 5

func _ready() -> void:
	super._ready()
	_build_stage2_ui()

func _build_stage2_ui() -> void:
	objective_label.text = "STAGE 2  |  Build, train five hordes, destroy the red fortress"
	var root := get_node("Root") as Control
	var economy_panel := PanelContainer.new()
	economy_panel.name = "EconomyPanel"
	economy_panel.set_anchors_preset(Control.PRESET_TOP_WIDE)
	economy_panel.offset_left = 220.0
	economy_panel.offset_top = 68.0
	economy_panel.offset_right = -220.0
	economy_panel.offset_bottom = 112.0
	economy_panel.mouse_filter = Control.MOUSE_FILTER_IGNORE
	root.add_child(economy_panel)
	economy_label = Label.new()
	economy_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	economy_label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	economy_label.add_theme_font_size_override("font_size", 18)
	economy_label.text = "Supplies 0  |  Earned 0  |  Battalions 0 / 0"
	economy_panel.add_child(economy_label)

	var action_panel := PanelContainer.new()
	action_panel.name = "Stage2Actions"
	action_panel.set_anchors_preset(Control.PRESET_BOTTOM_WIDE)
	action_panel.offset_left = 10.0
	action_panel.offset_top = -190.0
	action_panel.offset_right = -10.0
	action_panel.offset_bottom = -120.0
	action_panel.mouse_filter = Control.MOUSE_FILTER_STOP
	root.add_child(action_panel)
	var margin := MarginContainer.new()
	margin.add_theme_constant_override("margin_left", 10)
	margin.add_theme_constant_override("margin_right", 10)
	margin.add_theme_constant_override("margin_top", 6)
	margin.add_theme_constant_override("margin_bottom", 6)
	action_panel.add_child(margin)
	var column := VBoxContainer.new()
	margin.add_child(column)
	stage2_hint = Label.new()
	stage2_hint.text = "Build menu is loaded from the legal-safe bundle"
	column.add_child(stage2_hint)
	var rows := HBoxContainer.new()
	rows.add_theme_constant_override("separation", 18)
	column.add_child(rows)
	build_row = HBoxContainer.new()
	build_row.add_theme_constant_override("separation", 6)
	rows.add_child(build_row)
	var divider := VSeparator.new()
	rows.add_child(divider)
	train_row = HBoxContainer.new()
	train_row.add_theme_constant_override("separation", 6)
	train_row.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	rows.add_child(train_row)
	_set_mouse_filter_recursive(economy_panel, Control.MOUSE_FILTER_IGNORE)

func configure_catalog(buildings: Array, blueprints: Array, maximum_queue_length: int = 5) -> void:
	_maximum_queue_length = maxi(1, maximum_queue_length)
	_building_defs.clear()
	_blueprint_defs.clear()
	for value in buildings:
		var row: Dictionary = value
		_building_defs.append(row)
	_building_defs.sort_custom(func(a: Dictionary, b: Dictionary) -> bool: return int(a.get("build_menu_slot", -1)) < int(b.get("build_menu_slot", -1)))
	for value in blueprints:
		var row: Dictionary = value
		_blueprint_defs[int(row.get("type_code", 0))] = row
	_rebuild_build_buttons()
	show_build_menu()

func set_economy(resources: int, total_earned: int, used: int, reserved: int, cap: int) -> void:
	economy_label.text = "Supplies %d  |  Earned %d  |  Battalions %d + %d queued / %d" % [resources, total_earned, used, reserved, cap]

func set_stage2_hint(text_value: String) -> void:
	stage2_hint.text = text_value

func show_build_menu() -> void:
	_clear_children(train_row)
	_shown_building_id = 0
	_shown_complete = false
	_shown_trains = ""
	_building_title = null
	var label := Label.new()
	label.text = "Select a completed producer to train"
	train_row.add_child(label)

func show_building(row: Dictionary) -> void:
	var queue: Array = row.get("queue", [])
	var progress_ticks := int(row.get("progress_ticks", 0))
	var construction_ticks := maxi(1, int(row.get("construction_ticks", 1)))
	var complete := bool(row.get("complete", false))
	var building_id := int(row.get("id", 0))
	var trains_signature := str(row.get("trains", []))
	var needs_rebuild := _building_title == null or _shown_building_id != building_id or _shown_complete != complete or _shown_trains != trains_signature
	if needs_rebuild:
		_clear_children(train_row)
		_shown_building_id = building_id
		_shown_complete = complete
		_shown_trains = trains_signature
		_building_title = Label.new()
		train_row.add_child(_building_title)
	var train_codes: Array = row.get("trains", [])
	if complete and needs_rebuild and not train_codes.is_empty():
		for code_value in train_codes:
			var code := int(code_value)
			var definition: Dictionary = _blueprint_defs.get(code, {})
			var button := Button.new()
			button.text = "%s  %d" % [_friendly_name(String(definition.get("display_name", "Horde %d" % code))), int(definition.get("cost", 0))]
			button.tooltip_text = "Queue one battalion. Queued battalions reserve population."
			button.pressed.connect(_on_train_button_pressed.bind(code))
			train_row.add_child(button)
		var rally := Button.new()
		rally.text = "Set Rally"
		rally.tooltip_text = "Choose walkable ground for newly trained battalions."
		rally.pressed.connect(_on_rally_button_pressed)
		train_row.add_child(rally)
	var state_text := "Ready" if complete else "Constructing %d%%" % clampi(progress_ticks * 100 / construction_ticks, 0, 100)
	_building_title.text = "Building %d  |  %s  |  Queue %d / %d" % [building_id, state_text, queue.size(), _maximum_queue_length]

func _rebuild_build_buttons() -> void:
	_clear_children(build_row)
	for definition in _building_defs:
		if int(definition.get("build_menu_slot", -1)) < 0:
			continue
		var code := int(definition.get("type_code", 0))
		var button := Button.new()
		button.text = "%s  %d" % [_friendly_name(String(definition.get("display_name", "Build %d" % code))), int(definition.get("cost", 0))]
		button.tooltip_text = "Place a %dx%d-cell footprint" % [int(definition.get("width_cells", 1)), int(definition.get("height_cells", 1))]
		button.pressed.connect(_on_build_button_pressed.bind(code))
		build_row.add_child(button)

func _clear_children(node: Node) -> void:
	for child in node.get_children():
		node.remove_child(child)
		child.queue_free()

func _on_build_button_pressed(type_code: int) -> void:
	build_requested.emit(type_code)

func _on_train_button_pressed(type_code: int) -> void:
	train_requested.emit(type_code)

func _on_rally_button_pressed() -> void:
	rally_requested.emit()

func _friendly_name(value: String) -> String:
	var result := value
	if result.contains("."):
		result = result.get_slice(".", result.get_slice_count(".") - 1)
	return result.replace("-", " ").replace("_", " ").capitalize()
