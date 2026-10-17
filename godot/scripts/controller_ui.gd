## BFME2Lua-style RT palette. Lua owns navigation; the native HUD owns orders.
extends CanvasLayer
var execute: Callable
var hud: Callable
var bridge: Node
var object_info: Callable
var _cycle := {"heroes": -1, "builders": -1}
var _panel: Control
var _bottom: VBoxContainer
var _commands: HBoxContainer
var _shortcuts: Control
var _title: Label
var _keyboard_hint: Label
var _entries: Array = []
var _state := PackedStringArray(["commands", "commands", "1", "1", "1"])
var _favorites := PackedInt64Array()
var _icons: Dictionary = {}
var _shortcut_layout: Array = []
var _source: Node
var _elapsed := 0.0
var _render_hash := -1
var _render_evil := false
var _owns_store := false
var _native_layout_active := false
var _was_playing := false
var _armed_focus := ""
var _selection_key := ""
var _shortcut_art: TextureRect
var _palette_opened_at := 0
var _reticle: TextureRect
var _cursor_hidden := false

func _ready() -> void:
	layer = 25
	_reticle = TextureRect.new()
	_reticle.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	_reticle.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_reticle.hide()
	add_child(_reticle)
	_keyboard_hint = Label.new()
	_keyboard_hint.text = "Keyboard controller mode · hold Tab for commands · \\ to disable"
	_keyboard_hint.set_anchors_and_offsets_preset(Control.PRESET_CENTER_BOTTOM)
	_keyboard_hint.position = Vector2(-280, -32)
	_keyboard_hint.add_theme_color_override("font_color", Color("f2dca1"))
	_keyboard_hint.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_keyboard_hint.hide()
	add_child(_keyboard_hint)
	_panel = Control.new()
	_panel.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	_panel.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(_panel)
	var bottom := VBoxContainer.new()
	_bottom = bottom
	bottom.set_anchors_and_offsets_preset(Control.PRESET_CENTER_BOTTOM)
	bottom.position = Vector2(-240, -170)
	bottom.size = Vector2(480, 160)
	_panel.add_child(bottom)
	_title = Label.new()
	_title.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_title.custom_minimum_size.x = 0
	bottom.add_child(_title)
	_commands = HBoxContainer.new()
	_commands.alignment = BoxContainer.ALIGNMENT_CENTER
	_commands.add_theme_constant_override("separation", 6)
	bottom.add_child(_commands)
	_shortcut_art = TextureRect.new()
	_shortcut_art.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	_shortcut_art.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_panel.add_child(_shortcut_art)
	_shortcuts = Control.new()
	_shortcuts.set_anchors_and_offsets_preset(Control.PRESET_BOTTOM_LEFT)
	_shortcuts.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_panel.add_child(_shortcuts)
	bridge.palette_toggled.connect(_toggle)
	bridge.controller_disconnected.connect(func() -> void:
		_toggle(false)
		_keyboard_hint.hide()
		_update_reticle(null, false)
		var live: Node = hud.call()
		if live != null and live.has_method("set_controller_layout"):
			live.set_controller_layout(false)
		_native_layout_active = false)
	bridge.command_action.connect(action)
	bridge.formation = func(operation: String, direction: Vector2) -> bool:
		var live: Node = hud.call()
		return live.controller_formation(operation, direction) if live != null and live.has_method("controller_formation") else false
	bridge.area_selection = func(radius: float, commit: bool) -> bool:
		var live: Node = hud.call()
		return live.controller_select_area(radius, commit) if live != null and live.has_method("controller_select_area") else false
	bridge.placement_active = func() -> bool:
		var live: Node = hud.call()
		return live != null and live.has_method("get_placement") and bool(live.get_placement().get("placing", false))
	bridge.placement_aim.connect(func(direction: Vector2) -> void:
		var live: Node = hud.call()
		if live != null and live.has_method("controller_placement_aim"):
			live.controller_placement_aim(direction))
	bridge.camera_pan.connect(func(amount: Vector2) -> void:
		var live: Node = hud.call()
		if live != null and live.is_ready() and live.has_method("camera_pan"):
			live.camera_pan(amount))
	bridge.attack_move = func() -> bool:
		var live: Node = hud.call()
		return live.controller_attack_move() if live != null and live.has_method("controller_attack_move") else false
	bridge.primary_button = func() -> int:
		var live: Node = hud.call()
		return live.get_controller_primary_button(bridge.selection_assist_radius) if live != null and live.has_method("get_controller_primary_button") else MOUSE_BUTTON_LEFT
	bridge.camera_zoom.connect(func(amount: float) -> void:
		var live: Node = hud.call()
		if live != null and live.is_ready():
			var camera: Dictionary = live.get_camera()
			if not camera.is_empty():
				# the camera clamps like the mouse wheel's zoom: the map's min height up to zoom_out_limit (Max zoom-out factor, free camera)
				live.camera_set_height(float(camera.height_above_ground) + amount))
	bridge.camera_rotation.connect(func(radians: float) -> void:
		var live: Node = hud.call()
		if live != null and live.is_ready():
			var camera: Dictionary = live.get_camera()
			if not camera.is_empty():
				live.camera_set_angle_pitch(float(camera.angle) + radians, float(camera.pitch)))
	_panel.hide()
	var saved_options := ConfigFile.new()
	if saved_options.load("user://controller_options.cfg") == OK:
		var radius := clampf(float(saved_options.get_value("controller", "selection_assist_radius", 48)), 0, 200)
		_lua("controller.config.selectionAssistRadius = %s" % str(radius))
		_lua("controller.config.placementGridSpacing = %s" % str(clampf(float(saved_options.get_value("controller", "placement_grid_spacing", 50)), 10, 200)))
	_configure()
	_panel.resized.connect(_layout)
	_layout()

func _layout() -> void:
	var viewport_size := get_viewport().get_visible_rect().size
	var factor := maxf(viewport_size.y / 1080.0, 0.1)
	# Keep the Palantir clear, using the remaining bottom edge for every command.
	var left := minf(640.0, viewport_size.x / factor * 0.45)
	var width := maxf(1.0, viewport_size.x / factor - left - 24.0)
	var count := maxi(1, _entries.size())
	var gap := minf(6.0, width / float(count * 4))
	var side := minf(72.0, (width - gap * (count - 1)) / count)
	_commands.add_theme_constant_override("separation", int(gap))
	for child in _commands.get_children():
		if child is Button:
			child.custom_minimum_size = Vector2.ONE * side
			child.size = Vector2.ONE * side
	_bottom.scale = Vector2.ONE * factor
	_bottom.reset_size()
	_bottom.size = Vector2(width, 132)
	_bottom.position = Vector2(left * factor, viewport_size.y - 146 * factor)


func _lua(code: String) -> String:
	var result: Dictionary = execute.call(code)
	if not result.get("ok", false):
		printerr("CONTROLLER UI: ", result.get("output", ""))
		return ""
	return str(result.output).strip_edges()

func _navigate(value: String) -> void:
	var result := _lua("return controller.ui.navigate(%s,%d)" % [JSON.stringify(value), _entries.size()])
	var fields := result.split("\t")
	if fields.size() == 5:
		_state = fields

func _toggle(open: bool) -> void:
	if Engine.get_meta("adaptive_hud_replaces_native", false):
		_panel.hide()
		return
	if open:
		_palette_opened_at = Time.get_ticks_msec()
	_panel.visible = open
	if not open:
		_armed_focus = ""
		_clear_native_tooltip()
		var live: Node = hud.call()
		if _owns_store and live != null and live.is_ready():
			live.close_spell_store()
		_owns_store = false
		return
	_render_hash = -1
	_refresh()

func _configure() -> void:
	# One snapshot/VM call for all options and shortcut metadata, off the RT-open path.
	var fields := _lua("local c=controller.config; local u=controller.ui; local v={c.placementGridSpacing or 50,c.selectionAssistRadius or 48,c.cameraPanSpeed or 1200,c.centerCursor,c.cameraRotationSpeed,c.cameraZoomSpeed,c.deadzone,c.cursorSpeed,c.menuRepeatDelay,c.menuRepeatRate}; for i=1,4 do local p=u.layout.positions[i]; v[#v+1]=u.shortcuts[i].title; v[#v+1]=p.x; v[#v+1]=p.y; v[#v+1]=u.layout.size; v[#v+1]=u.shortcuts[i].image end; return (table.unpack or unpack)(v)").split("\t")
	if fields.size() != 30: return
	bridge.placement_grid_spacing = clampf(float(fields[0]),10,200)
	bridge.selection_assist_radius = clampf(float(fields[1]),0,200)
	bridge.camera_pan_speed = maxf(float(fields[2]),0)
	bridge.center_cursor = fields[3] != "false"
	bridge.camera_rotation_speed = maxf(float(fields[4]),0)
	bridge.camera_zoom_speed = maxf(float(fields[5]),0)
	bridge.deadzone = clampf(float(fields[6]),0,0.95)
	bridge.cursor_speed = maxf(float(fields[7]),0)
	bridge.menu_repeat_delay = maxi(int(fields[8]),1)
	bridge.menu_repeat_rate = maxi(int(fields[9]),1)
	_shortcut_layout.clear()
	for i in 4: _shortcut_layout.append(fields.slice(10+i*5,15+i*5))

func _process(delta: float) -> void:
	var live: Node = hud.call()
	if live != null and live.has_method("set_placement_snap"):
		live.set_placement_snap(bridge._gameplay() and not bridge._suspended and (bridge._left_trigger or Input.is_key_pressed(KEY_SHIFT)), bridge.placement_grid_spacing)
	if Engine.get_meta("adaptive_hud_replaces_native", false):
		_panel.hide()
		_keyboard_hint.hide()
	var active: bool = bridge._gameplay() and (bridge.keyboard_mode or bridge.device >= 0)
	if (bridge._gameplay() and not _was_playing) or (active and not _native_layout_active):
		_configure() # refresh once when returning from console, menus, or mouse mode
	_was_playing = bridge._gameplay()
	if live != null and live.has_method("set_controller_layout"):
		live.set_controller_layout(active)
	if live != null and live.is_ready():
		_sync_selection(live)
	_native_layout_active = active
	_update_reticle(live, active)
	_keyboard_hint.visible = bridge.keyboard_mode and bridge._gameplay() and not _panel.visible
	if not _panel.visible:
		return
	var art_scale := get_viewport().get_visible_rect().size.y / 1080.0
	_shortcut_art.position = Vector2(50 * art_scale, get_viewport().get_visible_rect().size.y - 550 * art_scale)
	_shortcut_art.size = Vector2(320, 200) * art_scale
	var art_frame := mini(11, int((Time.get_ticks_msec() - _palette_opened_at) * 30 / 1000))
	_shortcut_art.texture = preload("res://scripts/controller_art.gd").texture("shortcut_xbox_%02d" % art_frame)
	_elapsed += delta
	if _elapsed >= 0.15:
		_elapsed = 0.0
		_refresh()

func _sync_selection(live: Node) -> void:
	var key := str(live.get_instance_id()) + ":" + str(live.get_selection())
	if key == _selection_key:
		return
	_selection_key = key
	_armed_focus = ""
	_render_hash = -1
	_navigate("backPalette")

func _refresh() -> void:
	var live: Node = hud.call()
	if live == null or not live.is_ready():
		_panel.hide()
		return
	if not is_instance_valid(_source) or live != _source:
		_source = live
		_icons.clear()
		_favorites.clear()
	_sync_selection(live)
	_entries.clear()
	match _state[0]:
		"commands":
			var seen := {}
			for entry in live.get_command_buttons():
				if int(entry.state) == 0 or seen.has(entry.name):
					continue
				seen[entry.name] = true
				_entries.append(entry)
			# Keep destructive actions after production, upgrades and other commands.
			var demolition := _entries.filter(func(entry): return entry.get("command", "") == "SELL")
			_entries = _entries.filter(func(entry): return entry.get("command", "") != "SELL")
			_entries.append_array(demolition)
		"heroes", "builders":
			_entries = live.get_controller_objects(_state[0])
		"powers":
			var spells: Dictionary = live.get_spellbook_state()
			var store: Dictionary = spells.get("store", {})
			if _owns_store and store.get("open", false):
				var sciences: Array = store.get("sciences", [])
				for i in sciences.size():
					if not str(sciences[i]).is_empty():
						_entries.append({"name": "%s · %s" % [sciences[i], store.states[i]], "purchase": i})
			else:
				_entries.append({"name": "Buy powers", "store": true})
				var slots: Array = spells.get("slots", [])
				for i in slots.size():
					if slots[i] != "_unused" and int(spells.slot_buttons[i]) >= 0:
						_entries.append({"name": "Power %d · %s" % [i + 1, slots[i]], "spell": i})
		"favorites":
			if not _favorites.is_empty():
				_entries.append({"name": "Recall saved group (%d)" % _favorites.size(), "favorite": true})
	_navigate("")
	_render()

func _clear(container: Control) -> void:
	for child in container.get_children():
		container.remove_child(child)
		child.queue_free()

func _button(text: String, selected: bool) -> Button:
	var button := preload("res://scripts/controller_command_button.gd").new()
	button.source = _source
	button.caption = text
	button.selected = selected
	button.evil = _render_evil
	for rim in ["RadialBorder", "RadialOver", "RadialPush"]:
		if not _icons.has(rim): _icons[rim] = _source.get_command_icon(rim)
	button.shared_rims = _icons
	button.custom_minimum_size = Vector2(72, 72)
	return button

func _render() -> void:
	var structure: Array = []
	for entry in _entries:
		var stable: Dictionary = entry.duplicate()
		stable.erase("timer")
		stable.erase("queued")
		structure.append(stable)
	var signature := hash([structure, _state, get_viewport().get_visible_rect().size])
	if signature == _render_hash:
		for i in _entries.size():
			var button = _commands.get_child(i)
			if button.has_method("update_production"):
				button.update_production(int(_entries[i].get("queued", 0)), float(_entries[i].get("timer", -1.0)))
		return
	_render_hash = signature
	_render_evil = bool(_source.get_state().get("controller_evil", false)) if _source.has_method("get_state") else false
	_clear(_commands)
	_clear(_shortcuts)
	_title.text = ("Hold Tab · arrows navigate · Enter select · Backspace cancel · X save group · Y back · %s" if bridge.keyboard_mode else "RT · %s   ◀ ▶ commands   ▲ ▼ shortcuts   A select   B cancel queue   X save group   Y back") % _state[0].capitalize()
	if _entries.is_empty():
		var empty := Label.new()
		empty.text = "X: save selection" if _state[0] == "favorites" else "No available entries"
		_commands.add_child(empty)
	for i in _entries.size():
		var entry: Dictionary = _entries[i]
		var caption := _entry_tooltip(entry)
		var button := _button(caption, _state[1] == "commands" and i == int(_state[2]) - 1)
		button.queued = int(entry.get("queued", 0))
		button.progress = float(entry.get("timer", -1.0))
		var image := str(entry.get("image", ""))
		if not image.is_empty():
			if not _icons.has(image):
				_icons[image] = _source.get_command_icon(image)
			button.artwork = _icons[image]
		button.tooltip_text = ""
		button.mouse_entered.connect(func() -> void: _native_tooltip(entry))
		button.mouse_exited.connect(_selection_tooltip)
		if entry.has("state") and int(entry.state) not in [1, 5]:
			button.modulate = Color(0.55, 0.55, 0.55)
		button.pressed.connect(func() -> void: _activate(entry, false))
		_commands.add_child(button)
	_layout()
	for i in 4:
		var name_text: String = str(_shortcut_layout[i][0]) if _shortcut_layout.size() == 4 else ["Powers", "Heroes", "Builders", "Favorites"][i]
		var button := _button(name_text, _state[1] == "shortcuts" and i == int(_state[3]) - 1)
		var scale_factor := get_viewport().get_visible_rect().size.y / 1080.0
		var coordinates := Vector2(289, 352) + Vector2(-48, 35) * i
		var side := 70.0
		if _shortcut_layout.size() == 4:
			coordinates = Vector2(float(_shortcut_layout[i][1]), float(_shortcut_layout[i][2]))
			side = float(_shortcut_layout[i][3])
		if _shortcut_layout.size() == 4:
			var image_name := str(_shortcut_layout[i][4])
			if not _icons.has(image_name):
				_icons[image_name] = _source.get_command_icon(image_name)
			button.artwork = _icons[image_name]
		button.position = Vector2(coordinates.x, -coordinates.y) * scale_factor
		button.custom_minimum_size = Vector2.ONE * maxf(side * scale_factor, 32.0)
		button.clip_text = true
		button.tooltip_text = ""
		button.mouse_entered.connect(func() -> void: _show_tooltip(name_text))
		button.mouse_exited.connect(_selection_tooltip)
		button.add_theme_font_size_override("font_size", maxi(8, int(12 * scale_factor)))
		button.pressed.connect(func() -> void:
			_lua("controller.ui.focus='shortcuts'; controller.ui.shortcut=%d" % (i + 1))
			action("pressCommand"))
		_shortcuts.add_child(button)
	_selection_tooltip()

func action(value: String) -> void:
	if Engine.get_meta("adaptive_hud_replaces_native", false) and bridge.palette_open:
		return
	if value == "deselect":
		var live: Node = hud.call()
		if live != null and live.is_ready() and live.has_method("controller_deselect"):
			live.controller_deselect()
		return
	if value in ["cycleHeroes", "cycleBuilders", "selectAll", "jumpToSelection"]:
		var live: Node = hud.call()
		if live == null or not live.is_ready():
			return
		_source = live
		if value == "jumpToSelection":
			_focus_selection()
		elif value == "selectAll":
			var ids := PackedInt64Array()
			for unit in live.get_controller_objects("units"):
				ids.append(unit.id)
			live.select_controller_group(ids)
		else:
			var role := "heroes" if value == "cycleHeroes" else "builders"
			var units: Array = live.get_controller_objects(role)
			if not units.is_empty():
				_cycle[role] = (int(_cycle[role]) + 1) % units.size()
				live.select_controller_group(PackedInt64Array([units[_cycle[role]].id]))
				_focus_selection()
		return
	if _source == null or not is_instance_valid(_source):
		return
	if _owns_store and value in ["rightPressCommand", "backPalette"]:
		_source.close_spell_store()
		_owns_store = false
		_refresh()
		return
	if _owns_store and value in ["shortcutUp", "shortcutDown"]:
		_source.close_spell_store()
		_owns_store = false
	if value == "saveFavorites":
		_favorites = PackedInt64Array(_source.get_selection())
	elif value in ["pressCommand", "rightPressCommand"] and _state[1] == "commands":
		if not _entries.is_empty():
			_activate(_entries[clampi(int(_state[2]) - 1, 0, _entries.size() - 1)], value == "rightPressCommand")
	else:
		_navigate(value)
	_refresh()

func _activate(entry: Dictionary, cancel: bool) -> void:
	# Reference palantir.lua: an armed preview/ability confirms its world target.
	if not cancel and (entry.has("slot") or entry.has("spell")) and _source.has_method("get_placement"):
		var placement: Dictionary = _source.get_placement()
		var state: Dictionary = _source.get_state()
		if placement.get("placing", false) or state.get("gui_command", false):
			var position := get_viewport().get_mouse_position()
			_source.inject_mouse_button(MOUSE_BUTTON_LEFT, true, position, false)
			_source.inject_mouse_button(MOUSE_BUTTON_LEFT, false, position, false)
			return
	if entry.has("store"):
		if not cancel:
			_owns_store = _source.open_spell_store()
	elif entry.has("purchase"):
		if not cancel:
			_source.spell_store_click(entry.purchase)
	elif entry.has("id"):
		if not cancel:
			_source.select_controller_group(PackedInt64Array([entry.id]))
	elif entry.has("spell"):
		if not cancel:
			_source.press_spell_slot(entry.spell)
	elif entry.has("favorite"):
		if not cancel:
			_source.select_controller_group(_favorites)
	else:
		_source.activate_command(entry.slot, entry.in_palantir, cancel)
		if not cancel and not bridge.placement_requires_hold and entry.get("command", "") == "DOZER_CONSTRUCT":
			bridge._pad_trigger = false
			bridge._keyboard_trigger = false
			bridge._update_keyboard_mode()

func _focus_selection() -> void:
	if not object_info.is_valid():
		return
	var centre := Vector2.ZERO
	var count := 0
	for id in _source.get_selection():
		var info: Dictionary = object_info.call(id)
		if info.get("ok", false):
			centre += Vector2(info.x, info.y)
			count += 1
	if count > 0:
		_source.camera_look_at(centre / count)

func _exit_tree() -> void:
	if _cursor_hidden:
		Input.mouse_mode = Input.MOUSE_MODE_VISIBLE
	_clear_native_tooltip()
	if hud.is_valid():
		var live: Node = hud.call()
		if is_instance_valid(live) and live.has_method("set_controller_layout"):
			live.set_controller_layout(false)

func _entry_tooltip(entry: Dictionary) -> String:
	var title := str(entry.get("title", ""))
	if title.is_empty():
		title = str(entry.get("name", "")).trim_prefix("Command_")
	var description := str(entry.get("description", ""))
	if not description.is_empty():
		title += "\n" + description
	if entry.has("cost") and int(entry.cost) >= 0:
		title += "\n%d gold · queued %d" % [entry.cost, entry.get("queued", 0)]
	if entry.has("state"):
		var states := {2:"Unavailable: prerequisites or production limit", 3:"Not enough resources", 4:"Not ready", 5:"Active", 6:"Already purchased or researching"}
		if states.has(int(entry.state)):
			title += "\n" + states[int(entry.state)]
	return title

func _selection_tooltip() -> void:
	if _state[1] == "commands" and not _entries.is_empty():
		_native_tooltip(_entries[clampi(int(_state[2]) - 1, 0, _entries.size() - 1)])
	else:
		_clear_native_tooltip()

func _native_tooltip(entry: Dictionary) -> void:
	if is_instance_valid(_source) and _source.has_method("set_controller_hover"):
		_source.set_controller_hover(int(entry.get("slot", -1)), bool(entry.get("in_palantir", true)), int(entry.get("id", 0)), int(entry.get("spell", -1)))
	# Highlight readies builder construction and targeted portrait abilities exactly
	# once per focused command; production and purchases never execute on hover.
	var signature := str(_source.get_instance_id()) + ":" + str(entry.get("name", "")) + ":" + str(entry.get("slot", -1)) + ":" + str(_source.get_selection())
	if not (OS.has_feature("ios") and entry.get("command", "") == "DOZER_CONSTRUCT") and entry.get("preview_on_focus", false) and int(entry.get("state", 0)) in [1, 5] and signature != _armed_focus:
		_armed_focus = signature
		var placement: Dictionary = _source.get_placement() if _source.has_method("get_placement") else {}
		if not (placement.get("placing",false) and not str(entry.get("template", "")).is_empty() and placement.get("template", "") == entry.get("template", "")):
			_source.activate_command(entry.slot, entry.in_palantir, false)
	elif not entry.get("preview_on_focus", false):
		_armed_focus = ""

func _clear_native_tooltip() -> void:
	_armed_focus = ""
	if is_instance_valid(_source) and _source.has_method("set_controller_hover"):
		_source.set_controller_hover(-1, true, 0, -1)

func _show_tooltip(_text: String) -> void:
	_clear_native_tooltip()

func _update_reticle(live: Node, active: bool) -> void:
	_reticle.visible = active and live != null and live.has_method("get_cursor_image")
	if _reticle.visible:
		if not _cursor_hidden:
			Input.mouse_mode = Input.MOUSE_MODE_HIDDEN
			_cursor_hidden = true
		var placement: Dictionary = live.get_placement() if live.has_method("get_placement") else {}
		if placement.get("placing", false) and placement.get("has_ghost", false):
			_reticle.hide()
			return
		var cursor: Dictionary = live.get_cursor_image()
		var state: Dictionary = live.get_state()
		var native := str(cursor.get("name", "")).to_lower()
		var name_text := "reticle"
		var target: Dictionary = live.get_controller_target(bridge.selection_assist_radius) if live.has_method("get_controller_target") else {}
		if target.get("can_select", false):
			name_text = "reticle_a"
		elif "invalid" in native or "notallowed" in native or "nobomb" in native:
			name_text = "reticle_invalid"
		elif state.get("gui_command", false):
			name_text = "reticle_ability"
		elif "hostile2" in native or "capture" in native:
			name_text = "reticle_capture"
		elif "enter" in native or "garrison" in native:
			name_text = "reticle_garrison"
		elif "onering" in native:
			name_text = "reticle_ring"
		elif "attack" in native or "hostile" in native:
			name_text = "reticle_attack"
		elif live.get_selection().is_empty():
			name_text = "reticle_yellow"
		else:
			name_text = "reticle_a"
		_reticle.texture = preload("res://scripts/controller_art.gd").frame(name_text, 15.0)
		_reticle.size = Vector2.ONE * (48.0 * get_viewport().get_visible_rect().size.y / 720.0)
		_reticle.position = get_viewport().get_mouse_position() - _reticle.size * 0.5
		move_child(_reticle, get_child_count() - 1)
	elif _cursor_hidden:
		Input.mouse_mode = Input.MOUSE_MODE_VISIBLE
		_cursor_hidden = false
