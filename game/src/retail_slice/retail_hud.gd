class_name RetailHud
extends Control
## Player-facing Stage 15 HUD. Diagnostics still exist, but are opt-in so the
## normal surface reads like a game rather than a proof harness.

signal pause_requested
signal restart_requested
signal main_menu_requested
signal quit_requested
signal group_recall_requested(group: int)
signal group_assign_requested(group: int)
signal train_requested(unit_id: String)
signal music_volume_changed(value: float)
signal voice_volume_changed(value: float)
signal mute_changed(value: bool)

const MinimapScript = preload("res://src/retail_slice/retail_minimap.gd")
const PalantirFrameScript = preload("res://src/retail_slice/retail_palantir_frame.gd")
const RETAIL_TRAIN_ICON_ID := "BGBarracks_Soldiers"
const RETAIL_TRAIN_LABEL_ID := "CONTROLBAR:ConstructGondorFighterHorde"
const RETAIL_TRAIN_TOOLTIP_ID := "CONTROLBAR:ToolTipBuildGondorFighterHorde"
const MAX_RETAIL_COMMAND_ICON_BYTES := 16 * 1024 * 1024
const MAX_RETAIL_COMMAND_ICON_DIMENSION := 4096
const _MISSING_RETAIL_STRING := "\u001fopenbfme-missing-retail-string\u001f"

var minimap: RetailMinimap
var objective_label: Label
var selection_label: Label
var feedback_label: Label
var resource_label: Label
var command_points_label: Label
var train_button: Button
var group_buttons: Dictionary = {}
var pause_panel: PanelContainer
var failure_panel: PanelContainer
var outcome_layer: Control
var outcome_title: Label
var outcome_detail: Label
var diagnostics_panel: PanelContainer
var diagnostics_label: Label
var music_slider: HSlider
var voice_slider: HSlider
var mute_toggle: CheckButton
var retail_train_command_bound := false
var retail_train_icon_aspect_ratio := 0.0
var _retail_train_label := ""
var _built := false
var _normal_button: StyleBoxFlat
var _hover_button: StyleBoxFlat
var _pressed_button: StyleBoxFlat
var _panel: StyleBoxFlat


func _ready() -> void:
	if not _built:
		build()


func build() -> void:
	if _built:
		return
	_built = true
	name = "RetailHud"
	set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	mouse_filter = Control.MOUSE_FILTER_IGNORE
	_build_styles()
	_build_objective_banner()
	_build_palantir()
	_build_command_panel()
	_build_control_groups()
	_build_feedback()
	_build_diagnostics()
	_build_pause_panel()
	_build_outcome_layer()
	_build_failure_panel()


func configure_minimap(simulation: RefCounted, map_data: RefCounted, camera_value: Camera3D = null) -> void:
	minimap.configure(simulation, map_data)
	minimap.world_camera = camera_value


func set_resources(resources: int, command_points: int, command_cap: int) -> void:
	resource_label.text = "%d" % resources
	command_points_label.text = "%d / %d" % [command_points, command_cap]


func set_selection(text: String) -> void:
	selection_label.text = text


func set_objective(text: String) -> void:
	objective_label.text = text


func set_feedback(text: String, warning: bool = false) -> void:
	feedback_label.text = text
	feedback_label.add_theme_color_override("font_color", Color("f3b176") if warning else Color("e6d28a"))


func set_train_state(enabled: bool, label: String = "Train Gondor Soldiers") -> void:
	train_button.disabled = not enabled
	train_button.text = _retail_train_label if retail_train_command_bound else label


func bind_retail_train_command(content_db, expected_pack_root: String, private_parity_mode: bool) -> String:
	## Bind only after the complete private command surface validates. Public or
	## repository-only runs deliberately retain the legal-safe text fallback.
	retail_train_command_bound = false
	retail_train_icon_aspect_ratio = 0.0
	if not private_parity_mode:
		return ""
	if content_db == null:
		return "ContentDB is unavailable; cannot bind the private Barracks command UI."
	if not _built or train_button == null:
		return "The Barracks command button has not been built."

	var image_definition: Dictionary = content_db.get_retail_ui_image(RETAIL_TRAIN_ICON_ID)
	if image_definition.is_empty():
		return "Required UI manifest image '%s' is missing." % RETAIL_TRAIN_ICON_ID
	var image_pack_root := String(image_definition.get("_pack_root", ""))
	if expected_pack_root == "" or image_pack_root != expected_pack_root:
		return "Required UI image '%s' did not come from the selected private pack." % RETAIL_TRAIN_ICON_ID

	var label_text := String(content_db.get_retail_string(RETAIL_TRAIN_LABEL_ID, _MISSING_RETAIL_STRING))
	if label_text == _MISSING_RETAIL_STRING:
		return "Required localized string '%s' is missing." % RETAIL_TRAIN_LABEL_ID
	var tooltip_text := String(content_db.get_retail_string(RETAIL_TRAIN_TOOLTIP_ID, _MISSING_RETAIL_STRING))
	if tooltip_text == _MISSING_RETAIL_STRING:
		return "Required localized string '%s' is missing." % RETAIL_TRAIN_TOOLTIP_ID

	var image_path := String(content_db.resolve_retail_ui_image_path(RETAIL_TRAIN_ICON_ID))
	if image_path == "":
		return "Required UI image '%s' does not resolve inside the selected private pack." % RETAIL_TRAIN_ICON_ID
	if image_path.get_extension().to_lower() != "png":
		return "Required UI image '%s' must resolve to a PNG, got '%s'." % [RETAIL_TRAIN_ICON_ID, image_path.get_file()]
	if not bool(content_db.is_resolved_asset_path(image_path)):
		return "Required UI image '%s' resolved outside the mounted content-pack boundary." % RETAIL_TRAIN_ICON_ID

	var image_file := FileAccess.open(image_path, FileAccess.READ)
	if image_file == null:
		return "Required UI image '%s' could not be opened at its resolved pack path." % RETAIL_TRAIN_ICON_ID
	var encoded_size := image_file.get_length()
	var png_signature := image_file.get_buffer(8)
	image_file.close()
	if encoded_size <= 0 or encoded_size > MAX_RETAIL_COMMAND_ICON_BYTES:
		return "Required UI image '%s' has an unsafe encoded size of %d bytes." % [RETAIL_TRAIN_ICON_ID, encoded_size]
	if png_signature != PackedByteArray([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]):
		return "Required UI image '%s' could not be decoded as PNG (invalid signature)." % RETAIL_TRAIN_ICON_ID

	var decoded := Image.new()
	var decode_error := decoded.load(image_path)
	if decode_error != OK or decoded.is_empty():
		return "Required UI image '%s' could not be decoded as PNG (error %d)." % [RETAIL_TRAIN_ICON_ID, decode_error]
	var source_width := decoded.get_width()
	var source_height := decoded.get_height()
	if (
		source_width <= 0
		or source_height <= 0
		or source_width > MAX_RETAIL_COMMAND_ICON_DIMENSION
		or source_height > MAX_RETAIL_COMMAND_ICON_DIMENSION
	):
		return "Required UI image '%s' has unsafe decoded dimensions %dx%d." % [RETAIL_TRAIN_ICON_ID, source_width, source_height]
	var declared_width := int(image_definition.get("width", source_width))
	var declared_height := int(image_definition.get("height", source_height))
	if declared_width != source_width or declared_height != source_height:
		return "Required UI image '%s' decoded to %dx%d but its manifest declares %dx%d." % [
			RETAIL_TRAIN_ICON_ID,
			source_width,
			source_height,
			declared_width,
			declared_height,
		]

	var texture := ImageTexture.create_from_image(decoded)
	if texture == null:
		return "Required UI image '%s' decoded but could not create a Godot texture." % RETAIL_TRAIN_ICON_ID

	# Button's expanded-icon layout preserves the source texture aspect ratio.
	# Keep source dimensions as metadata so focused runtime tests can prove that
	# no atlas/crop dimensions were lost while crossing the external-pack edge.
	train_button.icon = texture
	train_button.expand_icon = true
	train_button.add_theme_constant_override("icon_max_width", 48)
	train_button.icon_alignment = HORIZONTAL_ALIGNMENT_LEFT
	train_button.text = label_text
	train_button.tooltip_text = tooltip_text
	_retail_train_label = label_text
	retail_train_icon_aspect_ratio = float(source_width) / float(source_height)
	train_button.set_meta("retail_icon_path", image_path)
	train_button.set_meta("retail_icon_source_size", Vector2i(source_width, source_height))
	train_button.set_meta("retail_icon_aspect_ratio", retail_train_icon_aspect_ratio)
	retail_train_command_bound = true
	return ""


func set_control_groups(groups: Dictionary) -> void:
	for group in range(1, 10):
		var count := 0
		var values: Variant = groups.get(group, [])
		if typeof(values) == TYPE_ARRAY:
			count = (values as Array).size()
		var button: Button = group_buttons[group]
		button.text = "%d\n%s" % [group, str(count) if count > 0 else "-"]
		button.tooltip_text = "Group %d: click to recall, Ctrl+click to assign" % group


func show_diagnostics(text: String, visible: bool) -> void:
	diagnostics_label.text = text
	diagnostics_panel.visible = visible


func show_pause(value: bool) -> void:
	pause_panel.visible = value
	if value:
		outcome_layer.visible = false


func show_outcome(winner: int, detail: String = "") -> void:
	pause_panel.visible = false
	outcome_title.text = "VICTORY" if winner == 0 else "DEFEAT"
	outcome_title.add_theme_color_override("font_color", Color("f4d785") if winner == 0 else Color("e37973"))
	outcome_detail.text = detail if detail != "" else ("The enemy fortress has fallen." if winner == 0 else "Your fortress has fallen.")
	outcome_layer.visible = true


func hide_outcome() -> void:
	outcome_layer.visible = false


func show_failure(message: String) -> void:
	failure_panel.visible = true
	var label := failure_panel.get_node("FailureMargin/FailureColumn/Message") as Label
	label.text = message


func hide_failure() -> void:
	failure_panel.visible = false


func apply_audio_values(music: float, voice: float, muted: bool) -> void:
	music_slider.set_value_no_signal(clampf(music, 0.0, 1.0))
	voice_slider.set_value_no_signal(clampf(voice, 0.0, 1.0))
	mute_toggle.set_pressed_no_signal(muted)


func _build_styles() -> void:
	_panel = StyleBoxFlat.new()
	_panel.bg_color = Color(0.018, 0.035, 0.055, 0.92)
	_panel.border_color = Color("6f8491")
	_panel.set_border_width_all(2)
	_panel.set_corner_radius_all(4)
	_panel.shadow_color = Color(0, 0, 0, 0.65)
	_panel.shadow_size = 8
	_panel.content_margin_left = 14
	_panel.content_margin_right = 14
	_panel.content_margin_top = 10
	_panel.content_margin_bottom = 10
	_normal_button = _button_box(Color("112a3d"), Color("617d91"))
	_hover_button = _button_box(Color("1e4e6c"), Color("a7c8d9"))
	_pressed_button = _button_box(Color("2e6785"), Color("e0d09a"))


func _button_box(background: Color, border: Color) -> StyleBoxFlat:
	var box := StyleBoxFlat.new()
	box.bg_color = background
	box.border_color = border
	box.set_border_width_all(2)
	box.set_corner_radius_all(3)
	box.content_margin_left = 10
	box.content_margin_right = 10
	box.content_margin_top = 7
	box.content_margin_bottom = 7
	return box


func _style_button(button: Button) -> void:
	button.add_theme_stylebox_override("normal", _normal_button)
	button.add_theme_stylebox_override("hover", _hover_button)
	button.add_theme_stylebox_override("pressed", _pressed_button)
	button.add_theme_stylebox_override("focus", _hover_button)
	button.add_theme_color_override("font_color", Color("c7dbe5"))
	button.add_theme_color_override("font_hover_color", Color("ffffff"))
	button.add_theme_font_size_override("font_size", 15)


func _build_objective_banner() -> void:
	var banner := PanelContainer.new()
	banner.name = "ObjectiveBanner"
	banner.set_anchors_preset(Control.PRESET_CENTER_TOP)
	banner.offset_left = -340
	banner.offset_top = 16
	banner.offset_right = 340
	banner.offset_bottom = 76
	banner.add_theme_stylebox_override("panel", _panel)
	banner.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(banner)
	objective_label = Label.new()
	objective_label.name = "Objective"
	objective_label.text = "DESTROY THE ENEMY FORTRESS"
	objective_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	objective_label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	objective_label.add_theme_font_size_override("font_size", 21)
	objective_label.add_theme_color_override("font_color", Color("e1d4ab"))
	banner.add_child(objective_label)


func _build_palantir() -> void:
	var dock := Control.new()
	dock.name = "PalantirDock"
	dock.set_anchors_preset(Control.PRESET_BOTTOM_LEFT)
	dock.offset_left = 18
	dock.offset_top = -330
	dock.offset_right = 424
	dock.offset_bottom = -12
	dock.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(dock)
	# The ornamental control paints the Palantir backing and bezel. Keep it
	# behind the radar; drawing it afterward would cover the source map with its
	# opaque inner disc even though input and mapping still worked.
	var frame: Control = PalantirFrameScript.new()
	frame.name = "OrnamentalFrame"
	frame.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	dock.add_child(frame)
	minimap = MinimapScript.new()
	minimap.name = "PalantirRadar"
	minimap.position = Vector2(46, 28)
	minimap.size = Vector2(312, 236)
	minimap.custom_minimum_size = minimap.size
	minimap.mouse_filter = Control.MOUSE_FILTER_IGNORE
	dock.add_child(minimap)
	var resource_strip := PanelContainer.new()
	resource_strip.name = "ResourceStrip"
	resource_strip.position = Vector2(42, 270)
	resource_strip.size = Vector2(320, 42)
	resource_strip.add_theme_stylebox_override("panel", _panel)
	dock.add_child(resource_strip)
	var row := HBoxContainer.new()
	row.alignment = BoxContainer.ALIGNMENT_CENTER
	row.add_theme_constant_override("separation", 34)
	resource_strip.add_child(row)
	var resource_icon := Label.new()
	resource_icon.text = "◆"
	resource_icon.add_theme_color_override("font_color", Color("d6aa55"))
	resource_icon.add_theme_font_size_override("font_size", 20)
	row.add_child(resource_icon)
	resource_label = Label.new()
	resource_label.name = "Resources"
	resource_label.text = "0"
	resource_label.add_theme_color_override("font_color", Color("f1d06e"))
	resource_label.add_theme_font_size_override("font_size", 18)
	row.add_child(resource_label)
	var cp_icon := Label.new()
	cp_icon.text = "⚔"
	cp_icon.add_theme_font_size_override("font_size", 18)
	row.add_child(cp_icon)
	command_points_label = Label.new()
	command_points_label.name = "CommandPoints"
	command_points_label.text = "0 / 200"
	command_points_label.add_theme_color_override("font_color", Color("d5e5ed"))
	row.add_child(command_points_label)


func _build_command_panel() -> void:
	var panel := PanelContainer.new()
	panel.name = "CommandPanel"
	panel.set_anchors_preset(Control.PRESET_BOTTOM_LEFT)
	panel.offset_left = 410
	panel.offset_top = -228
	panel.offset_right = 790
	panel.offset_bottom = -18
	panel.add_theme_stylebox_override("panel", _panel)
	panel.mouse_filter = Control.MOUSE_FILTER_STOP
	add_child(panel)
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 9)
	panel.add_child(column)
	var heading := Label.new()
	heading.text = "MEN OF THE WEST"
	heading.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	heading.add_theme_color_override("font_color", Color("dccb95"))
	heading.add_theme_font_size_override("font_size", 18)
	column.add_child(heading)
	selection_label = Label.new()
	selection_label.name = "SelectionSummary"
	selection_label.text = "No battalion selected"
	selection_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	selection_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	selection_label.add_theme_color_override("font_color", Color("d0e1e9"))
	selection_label.custom_minimum_size.y = 44
	column.add_child(selection_label)
	var command_grid := GridContainer.new()
	command_grid.columns = 2
	command_grid.add_theme_constant_override("h_separation", 8)
	command_grid.add_theme_constant_override("v_separation", 8)
	column.add_child(command_grid)
	train_button = Button.new()
	train_button.name = "TrainSoldiers"
	train_button.text = "Train Gondor Soldiers"
	train_button.tooltip_text = "Queue one 15-member Gondor Soldier battalion"
	train_button.custom_minimum_size = Vector2(166, 58)
	_style_button(train_button)
	train_button.pressed.connect(func() -> void: train_requested.emit("bfme2.object.gondor-fighter-horde"))
	command_grid.add_child(train_button)
	var stop_button := Button.new()
	stop_button.name = "StopOrder"
	stop_button.text = "Stop (S)"
	stop_button.disabled = true
	stop_button.custom_minimum_size = Vector2(166, 58)
	_style_button(stop_button)
	command_grid.add_child(stop_button)


func _build_control_groups() -> void:
	var strip := PanelContainer.new()
	strip.name = "ControlGroupStrip"
	strip.set_anchors_preset(Control.PRESET_CENTER_BOTTOM)
	strip.offset_left = -344
	strip.offset_top = -82
	strip.offset_right = 344
	strip.offset_bottom = -16
	strip.add_theme_stylebox_override("panel", _panel)
	strip.mouse_filter = Control.MOUSE_FILTER_STOP
	add_child(strip)
	var row := HBoxContainer.new()
	row.alignment = BoxContainer.ALIGNMENT_CENTER
	row.add_theme_constant_override("separation", 5)
	strip.add_child(row)
	for group in range(1, 10):
		var button := Button.new()
		button.name = "Group%d" % group
		button.text = "%d\n-" % group
		button.custom_minimum_size = Vector2(66, 46)
		_style_button(button)
		button.gui_input.connect(_on_group_button_input.bind(group))
		row.add_child(button)
		group_buttons[group] = button


func _on_group_button_input(event: InputEvent, group: int) -> void:
	if event is InputEventMouseButton and (event as InputEventMouseButton).pressed and (event as InputEventMouseButton).button_index == MOUSE_BUTTON_LEFT:
		if (event as InputEventMouseButton).ctrl_pressed:
			group_assign_requested.emit(group)
		else:
			group_recall_requested.emit(group)
		accept_event()


func _build_feedback() -> void:
	var panel := PanelContainer.new()
	panel.name = "FeedbackPanel"
	panel.set_anchors_preset(Control.PRESET_BOTTOM_RIGHT)
	panel.offset_left = -510
	panel.offset_top = -150
	panel.offset_right = -18
	panel.offset_bottom = -92
	panel.add_theme_stylebox_override("panel", _panel)
	panel.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(panel)
	feedback_label = Label.new()
	feedback_label.name = "Feedback"
	feedback_label.text = "Select a blue battalion or Barracks."
	feedback_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	feedback_label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	feedback_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	feedback_label.add_theme_color_override("font_color", Color("e6d28a"))
	panel.add_child(feedback_label)


func _build_diagnostics() -> void:
	diagnostics_panel = PanelContainer.new()
	diagnostics_panel.name = "DiagnosticsPanel"
	diagnostics_panel.position = Vector2(16, 16)
	diagnostics_panel.size = Vector2(560, 120)
	diagnostics_panel.add_theme_stylebox_override("panel", _panel)
	diagnostics_panel.mouse_filter = Control.MOUSE_FILTER_IGNORE
	diagnostics_panel.visible = false
	add_child(diagnostics_panel)
	diagnostics_label = Label.new()
	diagnostics_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	diagnostics_label.add_theme_font_size_override("font_size", 13)
	diagnostics_label.add_theme_color_override("font_color", Color("a9c8d7"))
	diagnostics_panel.add_child(diagnostics_label)


func _build_pause_panel() -> void:
	pause_panel = PanelContainer.new()
	pause_panel.name = "PausePanel"
	pause_panel.set_anchors_preset(Control.PRESET_CENTER)
	pause_panel.offset_left = -245
	pause_panel.offset_top = -260
	pause_panel.offset_right = 245
	pause_panel.offset_bottom = 260
	pause_panel.add_theme_stylebox_override("panel", _panel)
	pause_panel.mouse_filter = Control.MOUSE_FILTER_STOP
	pause_panel.visible = false
	add_child(pause_panel)
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 12)
	pause_panel.add_child(column)
	var heading := Label.new()
	heading.text = "BATTLE PAUSED"
	heading.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	heading.add_theme_font_size_override("font_size", 28)
	heading.add_theme_color_override("font_color", Color("d9c996"))
	column.add_child(heading)
	music_slider = _add_slider(column, "Music", func(value: float) -> void: music_volume_changed.emit(value))
	voice_slider = _add_slider(column, "Voice / Sound FX", func(value: float) -> void: voice_volume_changed.emit(value))
	mute_toggle = CheckButton.new()
	mute_toggle.text = "Mute all audio"
	mute_toggle.toggled.connect(func(value: bool) -> void: mute_changed.emit(value))
	column.add_child(mute_toggle)
	_add_action_button(column, "Resume", func() -> void: pause_requested.emit())
	_add_action_button(column, "Restart Battle", func() -> void: restart_requested.emit())
	_add_action_button(column, "Return to Main Menu", func() -> void: main_menu_requested.emit())
	_add_action_button(column, "Quit", func() -> void: quit_requested.emit())


func _add_slider(parent: VBoxContainer, title: String, callback: Callable) -> HSlider:
	var label := Label.new()
	label.text = title
	label.add_theme_color_override("font_color", Color("c8dbe4"))
	parent.add_child(label)
	var slider := HSlider.new()
	slider.min_value = 0.0
	slider.max_value = 1.0
	slider.step = 0.01
	slider.value = 0.8
	slider.custom_minimum_size.y = 28
	slider.value_changed.connect(callback)
	parent.add_child(slider)
	return slider


func _add_action_button(parent: VBoxContainer, text: String, callback: Callable) -> void:
	var button := Button.new()
	button.text = text
	button.custom_minimum_size.y = 42
	_style_button(button)
	button.pressed.connect(callback)
	parent.add_child(button)


func _build_outcome_layer() -> void:
	outcome_layer = Control.new()
	outcome_layer.name = "OutcomeLayer"
	outcome_layer.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	outcome_layer.mouse_filter = Control.MOUSE_FILTER_STOP
	outcome_layer.visible = false
	add_child(outcome_layer)
	var dim := ColorRect.new()
	dim.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	dim.color = Color(0, 0, 0, 0.72)
	dim.mouse_filter = Control.MOUSE_FILTER_IGNORE
	outcome_layer.add_child(dim)
	var panel := PanelContainer.new()
	panel.set_anchors_preset(Control.PRESET_CENTER)
	panel.offset_left = -360
	panel.offset_top = -190
	panel.offset_right = 360
	panel.offset_bottom = 190
	panel.add_theme_stylebox_override("panel", _panel)
	outcome_layer.add_child(panel)
	var column := VBoxContainer.new()
	column.alignment = BoxContainer.ALIGNMENT_CENTER
	column.add_theme_constant_override("separation", 20)
	panel.add_child(column)
	outcome_title = Label.new()
	outcome_title.name = "OutcomeTitle"
	outcome_title.text = "VICTORY"
	outcome_title.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	outcome_title.add_theme_font_size_override("font_size", 64)
	column.add_child(outcome_title)
	outcome_detail = Label.new()
	outcome_detail.name = "OutcomeDetail"
	outcome_detail.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	outcome_detail.add_theme_font_size_override("font_size", 20)
	outcome_detail.add_theme_color_override("font_color", Color("cbd8dd"))
	column.add_child(outcome_detail)
	_add_action_button(column, "Play Again", func() -> void: restart_requested.emit())
	_add_action_button(column, "Main Menu", func() -> void: main_menu_requested.emit())


func _build_failure_panel() -> void:
	failure_panel = PanelContainer.new()
	failure_panel.name = "FailurePanel"
	failure_panel.set_anchors_preset(Control.PRESET_CENTER)
	failure_panel.offset_left = -390
	failure_panel.offset_top = -155
	failure_panel.offset_right = 390
	failure_panel.offset_bottom = 155
	failure_panel.add_theme_stylebox_override("panel", _panel)
	failure_panel.mouse_filter = Control.MOUSE_FILTER_STOP
	failure_panel.visible = false
	add_child(failure_panel)
	var margin := MarginContainer.new()
	margin.name = "FailureMargin"
	margin.add_theme_constant_override("margin_left", 24)
	margin.add_theme_constant_override("margin_right", 24)
	margin.add_theme_constant_override("margin_top", 20)
	margin.add_theme_constant_override("margin_bottom", 20)
	failure_panel.add_child(margin)
	var column := VBoxContainer.new()
	column.name = "FailureColumn"
	column.add_theme_constant_override("separation", 16)
	margin.add_child(column)
	var label := Label.new()
	label.name = "Message"
	label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	label.add_theme_font_size_override("font_size", 18)
	label.add_theme_color_override("font_color", Color("e7bd96"))
	column.add_child(label)
	_add_action_button(column, "Return to Main Menu", func() -> void: main_menu_requested.emit())
