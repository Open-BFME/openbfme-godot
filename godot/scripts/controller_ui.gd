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
var _owns_store := false
var _native_layout_active := false
var _armed_focus := ""

func _ready() -> void:
	layer = 25
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
	_shortcuts = Control.new()
	_shortcuts.set_anchors_and_offsets_preset(Control.PRESET_BOTTOM_LEFT)
	_shortcuts.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_panel.add_child(_shortcuts)
	bridge.palette_toggled.connect(_toggle)
	bridge.command_action.connect(action)
	_panel.hide()
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
	_panel.visible = open
	if not open:
		_armed_focus = ""
		_clear_native_tooltip()
		var live: Node = hud.call()
		if _owns_store and live != null and live.is_ready():
			live.close_spell_store()
		_owns_store = false
		return
	_configure()
	_render_hash = -1
	_shortcut_layout.clear()
	for i in 4:
		var fields := _lua("local u=controller.ui; local p=u.layout.positions[%d]; return u.shortcuts[%d].title,p.x,p.y,u.layout.size,u.shortcuts[%d].image" % [i + 1, i + 1, i + 1]).split("\t")
		if fields.size() == 5:
			_shortcut_layout.append(fields)
	_refresh()

func _configure() -> void:
	var config := _lua("return controller.config.deadzone,controller.config.cursorSpeed,controller.config.menuRepeatDelay,controller.config.menuRepeatRate").split("\t")
	if config.size() == 4:
		bridge.deadzone = clampf(float(config[0]), 0.0, 0.95)
		bridge.cursor_speed = maxf(float(config[1]), 0.0)
		bridge.menu_repeat_delay = maxi(int(config[2]), 1)
		bridge.menu_repeat_rate = maxi(int(config[3]), 1)

func _process(delta: float) -> void:
	var live: Node = hud.call()
	var active: bool = bridge._gameplay() and (bridge.keyboard_mode or bridge.device >= 0)
	if live != null and live.has_method("set_controller_layout"):
		live.set_controller_layout(active)
	_native_layout_active = active
	_keyboard_hint.visible = bridge.keyboard_mode and bridge._gameplay() and not _panel.visible
	if not _panel.visible:
		return
	_elapsed += delta
	if _elapsed >= 0.15:
		_elapsed = 0.0
		_refresh()

func _refresh() -> void:
	var live: Node = hud.call()
	if live == null or not live.is_ready():
		_panel.hide()
		return
	if not is_instance_valid(_source) or live != _source:
		_source = live
		_icons.clear()
		_favorites.clear()
	_entries.clear()
	match _state[0]:
		"commands":
			var seen := {}
			for entry in live.get_command_buttons():
				if int(entry.state) == 0 or seen.has(entry.name):
					continue
				seen[entry.name] = true
				_entries.append(entry)
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
	button.custom_minimum_size = Vector2(72, 72)
	return button

func _render() -> void:
	var signature := hash([_entries, _state, get_viewport().get_visible_rect().size])
	if signature == _render_hash:
		return
	_render_hash = signature
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
	if entry.get("preview_on_focus", false) and int(entry.get("state", 0)) in [1, 5] and signature != _armed_focus:
		_armed_focus = signature
		_source.activate_command(entry.slot, entry.in_palantir, false)
	elif not entry.get("preview_on_focus", false):
		_armed_focus = ""

func _clear_native_tooltip() -> void:
	_armed_focus = ""
	if is_instance_valid(_source) and _source.has_method("set_controller_hover"):
		_source.set_controller_hover(-1, true, 0, -1)

func _show_tooltip(_text: String) -> void:
	_clear_native_tooltip()
