extends CanvasLayer
var hud: Callable
var gameplay: Callable
var open_menu: Callable
var open_console: Callable
var compact_mode := false
var expanded := false
var menu_button: Button
var menu_panel: PanelContainer
var placement_background: PanelContainer
var bar: GridContainer
var placement_panel: VBoxContainer
var placement_hint: Label
var build_button: Button
var placement_rotation := 0.0
var _building := ""
func _ready() -> void:
	layer = 35
	compact_mode = OS.get_model_name().begins_with("iPhone") or get_viewport().get_visible_rect().size.aspect() >= 1.9
	menu_panel = _panel()
	add_child(menu_panel)
	var menu_contents := VBoxContainer.new()
	menu_contents.add_theme_constant_override("separation", 12)
	menu_panel.add_child(menu_contents)
	menu_button = _button("Touch menu", Vector2(180, 72))
	menu_button.pressed.connect(func() -> void: expanded = not expanded)
	menu_contents.add_child(menu_button)
	bar = GridContainer.new()
	bar.columns = 2 if compact_mode else 4
	bar.add_theme_constant_override("h_separation", 12)
	bar.add_theme_constant_override("v_separation", 12)
	menu_contents.add_child(bar)
	for title in ["Deselect units", "Stop", "Menu", "Console"]:
		var button := _button(title, Vector2(180, 72))
		button.pressed.connect(_activate.bind(title))
		bar.add_child(button)
	placement_background = _panel()
	add_child(placement_background)
	placement_panel = VBoxContainer.new()
	placement_panel.add_theme_constant_override("separation", 12)
	placement_background.add_child(placement_panel)
	placement_background.hide()
	placement_hint = Label.new()
	placement_hint.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	placement_hint.add_theme_font_size_override("font_size", 20)
	placement_panel.add_child(placement_hint)
	var controls := GridContainer.new()
	controls.columns = 3
	controls.add_theme_constant_override("h_separation", 12)
	controls.add_theme_constant_override("v_separation", 12)
	placement_panel.add_child(controls)
	for title in ["Reposition", "Rotate left", "Rotate right", "Build", "Cancel"]:
		var button := _button(title, Vector2(170, 72))
		button.pressed.connect(_placement_action.bind(title))
		controls.add_child(button)
		if title == "Build": build_button = button
func _process(_delta: float) -> void:
	var active: bool = gameplay.is_valid() and gameplay.call()
	var viewport_size := get_viewport().get_visible_rect().size
	var ui_scale := maxf(1.0, viewport_size.y / 768.0)
	menu_panel.scale = Vector2.ONE * ui_scale
	placement_background.scale = Vector2.ONE * ui_scale
	menu_panel.visible = active
	menu_button.visible = compact_mode
	menu_button.text = "Close touch menu" if expanded else "Touch menu"
	bar.visible = active and (not compact_mode or expanded)
	menu_panel.size = menu_panel.get_combined_minimum_size()
	menu_panel.position = Vector2((viewport_size.x - menu_panel.size.x * ui_scale) * 0.5, 16 * ui_scale)
	var live: Node = hud.call()
	var placement: Dictionary = live.get_placement() if live != null and live.has_method("get_placement") else {}
	placement_panel.visible = active and placement.get("placing", false)
	placement_background.visible = placement_panel.visible
	placement_background.size = placement_background.get_combined_minimum_size()
	placement_background.position = Vector2((viewport_size.x - placement_background.size.x * ui_scale) * 0.5, menu_panel.position.y + menu_panel.size.y * ui_scale + 12 * ui_scale)
	if placement_panel.visible:
		if _building != placement.get("template", ""):
			_building = placement.get("template", "")
			placement_rotation = 0.0
		var positioned: bool = placement.get("anchored", false)
		var legal: bool = placement.get("legal", -1) == 0
		var rotating: bool = placement.get("touch_rotating", false)
		placement_hint.text = "Drag to position · tap anywhere to lock and rotate" if not rotating else ("Drag to rotate · tap to build" if legal else "Cannot build here · choose Reposition")
		build_button.disabled = not positioned or not rotating or not legal
	else:
		_building = ""
	Engine.set_meta("touch_build_controls", active)
	var blocked := menu_panel.get_global_rect() if active else Rect2()
	if placement_panel.visible: blocked = blocked.merge(placement_background.get_global_rect())
	Engine.set_meta("touch_toolbar_rect", blocked)
func _exit_tree() -> void:
	Engine.remove_meta("touch_toolbar_rect")
	Engine.remove_meta("touch_build_controls")
func _activate(title: String) -> void:
	var live: Node = hud.call()
	if live == null: return
	match title:
		"Deselect units": live.deselect_all()
		"Stop":
			live.inject_key(31, true) # DirectInput S
			live.inject_key(31, false)
		"Menu": open_menu.call()
		"Console":
			if open_console.is_valid(): open_console.call()
	expanded = false

func _placement_action(title: String) -> void:
	var live: Node = hud.call()
	if live == null: return
	match title:
		"Cancel": live.touch_build("cancel", Vector2.ZERO)
		"Build": live.touch_build("confirm", Vector2.ZERO)
		"Reposition": live.touch_build("reposition", Vector2.ZERO)
		_:
			placement_rotation += -PI / 8.0 if title == "Rotate left" else PI / 8.0
			live.touch_build("rotate", Vector2.from_angle(placement_rotation))

func _panel() -> PanelContainer:
	var panel := PanelContainer.new()
	var style := StyleBoxFlat.new()
	style.bg_color = Color("18202b")
	style.border_color = Color("8497af")
	style.set_border_width_all(2)
	style.set_corner_radius_all(12)
	style.content_margin_left = 14
	style.content_margin_right = 14
	style.content_margin_top = 14
	style.content_margin_bottom = 14
	panel.add_theme_stylebox_override("panel", style)
	return panel

func _button(title: String, minimum: Vector2) -> Button:
	var button := Button.new()
	button.text = title
	button.custom_minimum_size = minimum
	button.focus_mode = Control.FOCUS_NONE
	button.add_theme_font_size_override("font_size", 24)
	for state in ["normal", "hover", "pressed", "disabled"]:
		var style := StyleBoxFlat.new()
		style.bg_color = Color("40546c") if state == "pressed" else Color("29394c")
		if state == "disabled": style.bg_color = Color("202733")
		style.border_color = Color("8497af")
		style.set_border_width_all(2)
		style.set_corner_radius_all(8)
		button.add_theme_stylebox_override(state, style)
	return button
