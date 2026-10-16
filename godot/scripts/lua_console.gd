## BFME-style companion console, inspired by BFME2Lua's Command / Log editor.
extends CanvasLayer

signal toggled(open: bool)
var execute: Callable
var history_path := "user://console_history.cfg"
var controller_options_path := "user://controller_options.cfg"
var _root: Control
var _panel: PanelContainer
var _scroll: ScrollContainer
var _output: TextEdit
var _log: TextEdit
var _entry: CodeEdit
var _tabs: TabContainer
var _status: Label
var _history: Array[String] = []
var _history_index := 0
var _draft := ""
var _layout_queued := false
const GOLD := Color("c4a76a")
const INK := Color("10191e")
const TEXT := Color("e3dfcf")
const MUTED := Color("99aaa9")
const UI_SCALE := 2.0

class OrnateFrame extends PanelContainer:
	func _draw() -> void:
		var gold := Color("c4a76a")
		for corner in [Vector2(9, 9), Vector2(size.x - 9, 9), Vector2(9, size.y - 9), size - Vector2(9, 9)]:
			var direction := Vector2(1 if corner.x < size.x / 2 else -1, 1 if corner.y < size.y / 2 else -1)
			draw_line(corner, corner + Vector2(25 * direction.x, 0), gold, 2)
			draw_line(corner, corner + Vector2(0, 25 * direction.y), gold, 2)
			var center: Vector2 = corner + direction * 5
			draw_colored_polygon(PackedVector2Array([center + Vector2(0, -3), center + Vector2(3, 0), center + Vector2(0, 3), center + Vector2(-3, 0)]), gold)

func _box(color: Color, border: Color, padding: int = 10) -> StyleBoxFlat:
	var box := StyleBoxFlat.new()
	box.bg_color = color
	box.border_color = border
	box.set_border_width_all(1)
	box.set_content_margin_all(padding)
	box.set_corner_radius_all(2)
	return box

func _label(text: String, color: Color = TEXT, font_size: int = 14) -> Label:
	var label := Label.new()
	label.text = text
	label.add_theme_color_override("font_color", color)
	label.add_theme_font_size_override("font_size", font_size)
	return label

func _button(text: String, action: Callable) -> Button:
	var button := Button.new()
	button.text = text
	button.mouse_default_cursor_shape = Control.CURSOR_POINTING_HAND
	button.pressed.connect(action)
	button.focus_mode = Control.FOCUS_NONE
	return button

func _text_view() -> TextEdit:
	var view := TextEdit.new()
	view.editable = false
	view.wrap_mode = TextEdit.LINE_WRAPPING_BOUNDARY
	view.custom_minimum_size.y = 85
	view.size_flags_vertical = Control.SIZE_EXPAND_FILL
	view.add_theme_font_size_override("font_size", 14)
	return view

func _ready() -> void:
	_load_history()
	layer = 100
	_root = Control.new()
	_root.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	add_child(_root)
	var veil := ColorRect.new()
	veil.color = Color(0.015, 0.025, 0.03, 0.66)
	veil.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	_root.add_child(veil)
	_panel = OrnateFrame.new()
	_root.add_child(_panel)
	_panel.minimum_size_changed.connect(_request_layout)
	var theme := Theme.new()
	var serif := SystemFont.new()
	serif.font_names = PackedStringArray(["Palatino", "Georgia", "DejaVu Serif"])
	serif.oversampling = UI_SCALE
	theme.default_font = serif
	theme.default_font_size = 15
	theme.set_color("font_color", "Label", TEXT)
	for state in ["normal", "hover", "pressed", "focus"]:
		theme.set_stylebox(state, "Button", _box(Color("28383c") if state == "hover" else Color("19272d"), GOLD if state != "normal" else Color("655f48"), 8))
		theme.set_color("font_color" if state == "normal" else "font_" + state + "_color", "Button", TEXT)
	var mono := SystemFont.new()
	mono.font_names = PackedStringArray(["Menlo", "Consolas", "DejaVu Sans Mono"])
	mono.oversampling = UI_SCALE
	for type in ["TextEdit", "CodeEdit"]:
		theme.set_font("font", type, mono)
		theme.set_color("font_color", type, TEXT)
		theme.set_color("font_readonly_color", type, TEXT)
		theme.set_color("caret_color", type, GOLD)
		theme.set_color("selection_color", type, Color("415c65"))
		theme.set_stylebox("normal", type, _box(Color("0c1418"), Color("425252")))
		theme.set_stylebox("read_only", type, _box(Color("101b20"), Color("354648")))
		theme.set_stylebox("focus", type, _box(Color.TRANSPARENT, GOLD, 0))
	theme.set_stylebox("panel", "TabContainer", _box(Color("142126"), Color("6d674e")))
	theme.set_stylebox("tab_selected", "TabContainer", _box(Color("26383b"), GOLD, 9))
	theme.set_stylebox("tab_unselected", "TabContainer", _box(Color("142126"), Color("465352"), 9))
	theme.set_color("font_selected_color", "TabContainer", GOLD)
	theme.set_color("font_unselected_color", "TabContainer", MUTED)
	_panel.theme = theme
	var frame := _box(INK, GOLD, 22)
	frame.shadow_color = Color(0, 0, 0, 0.7)
	frame.shadow_size = 20
	_panel.add_theme_stylebox_override("panel", frame)
	var rows := VBoxContainer.new()
	var chrome := VBoxContainer.new()
	chrome.add_theme_constant_override("separation", 10)
	_panel.add_child(chrome)
	_scroll = ScrollContainer.new()
	_scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	chrome.add_child(_scroll)
	rows.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	rows.size_flags_vertical = Control.SIZE_EXPAND_FILL
	rows.add_theme_constant_override("separation", 10)
	_scroll.add_child(rows)
	var header := HBoxContainer.new()
	rows.add_child(header)
	var title := _label("LUA CONSOLE", GOLD, 25)
	title.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	header.add_child(title)
	header.add_child(_button("Close  `", toggle))
	rows.add_child(_label("P A L A N T Í R   /   S C R I P T I N G", MUTED, 11))
	_tabs = TabContainer.new()
	_tabs.size_flags_vertical = Control.SIZE_EXPAND_FILL
	rows.add_child(_tabs)
	var command := VBoxContainer.new()
	command.name = "Command"
	command.add_theme_constant_override("separation", 8)
	_tabs.add_child(command)
	_output = _text_view()
	command.add_child(_output)
	var editor_heading := HBoxContainer.new()
	editor_heading.add_child(_label("SCRIPT", GOLD, 12))
	var editor_hint := _label("Enter to run · Shift+Enter for a new line", MUTED, 12)
	editor_hint.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	editor_hint.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	editor_heading.add_child(editor_hint)
	command.add_child(editor_heading)
	_entry = CodeEdit.new()
	_entry.placeholder_text = 'bfme.players.localPlayer.money = 10000'
	_entry.custom_minimum_size.y = 110
	_entry.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_entry.size_flags_stretch_ratio = 0.5
	_entry.gutters_draw_line_numbers = true
	_entry.indent_size = 4
	_entry.indent_use_spaces = true
	_entry.add_theme_font_size_override("font_size", 14)
	_entry.add_theme_color_override("font_color", TEXT)
	var syntax := CodeHighlighter.new()
	syntax.number_color = Color("d5b875")
	syntax.symbol_color = Color("b2c4c5")
	syntax.function_color = Color("a3c6ce")
	syntax.member_variable_color = TEXT
	for keyword in ["and", "break", "do", "else", "elseif", "end", "false", "for", "function", "goto", "if", "in", "local", "nil", "not", "or", "repeat", "return", "then", "true", "until", "while"]:
		syntax.add_keyword_color(keyword, Color("d1ad73"))
	syntax.add_color_region('"', '"', Color("9cc599"))
	syntax.add_color_region("'", "'", Color("9cc599"))
	syntax.add_color_region("--[[", "]]", Color("748c89"))
	syntax.add_color_region("--", "", Color("748c89"), true)
	_entry.syntax_highlighter = syntax
	command.add_child(_entry)
	_entry.gui_input.connect(_entry_input)
	_log = _text_view()
	_log.name = "Log"
	_tabs.add_child(_log)
	_build_cheats()
	_build_controller_options()
	preload("res://scripts/wall_balance_options.gd").build(_tabs)
	_tabs.tab_changed.connect(func(_index: int) -> void:
		if _tabs.current_tab == 0:
			_entry.grab_focus())
	var toolbar := HFlowContainer.new()
	toolbar.add_theme_constant_override("separation", 8)
	chrome.add_child(toolbar)
	_status = _label("READY · Lua 5.4.9", MUTED, 12)
	_status.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	toolbar.add_child(_status)
	toolbar.add_child(_button("Reload scripts", reload_scripts))
	toolbar.add_child(_button("Clear", _clear_visible))
	toolbar.add_child(_button("Run script", func() -> void:
		_tabs.current_tab = 0
		_submit(_entry.text)))
	var footer := _label("` toggle · Esc close · ↑ / ↓ history · Ctrl+Shift+F5 reload", MUTED, 12)
	footer.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	chrome.add_child(footer)
	_append("BFME scripting console\nInspect the realm, shape the battle.\n\nTry  bfme.match.getStatus()  or  bfme.players.localPlayer.money\n")
	get_viewport().size_changed.connect(_layout)
	_layout()
	_root.hide()

func _build_cheats() -> void:
	var scroll := ScrollContainer.new()
	scroll.name = "Cheats"
	scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	_tabs.add_child(scroll)
	var rows := VBoxContainer.new()
	rows.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	rows.add_theme_constant_override("separation", 10)
	scroll.add_child(rows)
	rows.add_child(_label("SHAPE THE BATTLE", GOLD, 16))
	var hint := _label("Applies to the current match. Results appear in Log.", MUTED, 12)
	hint.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	rows.add_child(hint)
	var gold_row := HFlowContainer.new()
	rows.add_child(gold_row)
	gold_row.add_child(_label("Local player gold"))
	var gold := SpinBox.new()
	gold.name = "GoldAmount"
	gold.min_value = 0
	gold.max_value = 2147483647
	gold.step = 100
	gold.value = 100000
	gold.custom_minimum_size.x = 150
	gold_row.add_child(gold)
	gold_row.add_child(_button("Set gold", func() -> void:
		_submit("assert(bfme.players.localPlayer, 'No local player in this match').money = %d" % int(gold.value))))
	var quick := HFlowContainer.new()
	rows.add_child(quick)
	quick.add_child(_button("+100k gold", func() -> void:
		_submit("local p=assert(bfme.players.localPlayer,'No local player in this match'); p.money=math.min(2147483647,p.money+100000)")))
	quick.add_child(_button("+100k CP", func() -> void:
		_submit("local p=assert(bfme.players.localPlayer,'No local player in this match'); p.commandPoints=math.min(1000000,p.commandPoints+100000)")))
	var visibility := HFlowContainer.new()
	rows.add_child(visibility)
	visibility.add_child(_button("Reveal map", func() -> void:
		_submit("assert(bfme.players.localPlayer,'No local player in this match'):revealMap()")))
	visibility.add_child(_button("Disable fog of war", func() -> void:
		_submit("assert(bfme.players.localPlayer,'No local player in this match'):setFogOfWar(false)")))
	visibility.add_child(_button("Restore fog of war", func() -> void:
		_submit("assert(bfme.players.localPlayer,'No local player in this match'):setFogOfWar(true)")))
	var speed_row := HFlowContainer.new()
	rows.add_child(speed_row)
	speed_row.add_child(_label("Game speed"))
	var speed := SpinBox.new()
	speed.name = "GameSpeed"
	speed.min_value = 0
	speed.max_value = 100
	speed.step = 0.25
	speed.value = 1
	speed.custom_minimum_size.x = 110
	speed_row.add_child(speed)
	speed_row.add_child(_button("Apply speed", func() -> void:
		_submit("bfme.match.setGameSpeed(%s)" % str(speed.value))))
	rows.add_child(_label("0 stops time · 1 normal · 2 twice as fast · max 100", MUTED, 12))
	var actions := HFlowContainer.new()
	rows.add_child(actions)
	actions.add_child(_button("Normal speed", func() -> void:
		speed.value = 1
		_submit("bfme.match.setGameSpeed(1)")))
	actions.add_child(_button("Pause", func() -> void: _submit("bfme.match.pause()")))
	actions.add_child(_button("Resume", func() -> void: _submit("bfme.match.unpause()")))
	# lane LUAAI-1: the computer players to the Lua skirmish AI (godot/dev/05_skirmish_ai.lua) and back
	var ai_row := HFlowContainer.new()
	rows.add_child(ai_row)
	ai_row.add_child(_button("Computers: Lua AI", func() -> void: _submit("assert(luaai,'Reload scripts first'); return luaai.enableAll()")))
	ai_row.add_child(_button("Computers: native AI", func() -> void: _submit("assert(luaai,'Reload scripts first'); luaai.disableAll()")))
	ai_row.add_child(_button("Lua AI status", func() -> void: _submit("assert(luaai,'Reload scripts first'); return luaai.status()")))

func _build_controller_options() -> void:
	var rows := VBoxContainer.new()
	rows.name = "Controller"
	rows.add_theme_constant_override("separation", 12)
	_tabs.add_child(rows)
	rows.add_child(_label("CONTROLLER OPTIONS", GOLD, 16))
	var row := HFlowContainer.new()
	rows.add_child(row)
	row.add_child(_label("Selection assistance radius"))
	var radius := SpinBox.new()
	radius.name = "SelectionAssistRadius"
	radius.min_value = 0
	radius.max_value = 200
	radius.step = 1
	radius.suffix = " px"
	radius.value = 48
	var saved := ConfigFile.new()
	if saved.load(controller_options_path) == OK:
		radius.value = float(saved.get_value("controller", "selection_assist_radius", 48))
	row.add_child(radius)
	row.add_child(_button("Apply radius", func() -> void:
		_submit("controller.config.selectionAssistRadius = %d" % int(radius.value))
		saved.set_value("controller", "selection_assist_radius", int(radius.value))
		var error := saved.save(controller_options_path)
		if error != OK:
			_append("Could not save controller options: " + error_string(error) + "\n")))
	var grid_row := HFlowContainer.new()
	rows.add_child(grid_row)
	grid_row.add_child(_label("Placement grid spacing"))
	var grid := SpinBox.new()
	grid.name = "PlacementGridSpacing"
	grid.min_value = 10
	grid.max_value = 200
	grid.step = 5
	grid.value = float(saved.get_value("controller", "placement_grid_spacing", 50))
	grid.suffix = " map units"
	grid_row.add_child(grid)
	grid_row.add_child(_button("Apply grid", func() -> void:
		_submit("controller.config.placementGridSpacing = %d" % int(grid.value))
		saved.set_value("controller", "placement_grid_spacing", int(grid.value))
		var error := saved.save(controller_options_path)
		if error != OK:
			_append("Could not save controller options: " + error_string(error) + "\n")))
	rows.add_child(_label("Hold LT or Shift during placement to show the grid and snap position and rotation (45°).", MUTED, 12))
	var hint := _label("A selects the nearest friendly, unselected unit when nothing is directly under the reticle. Radius scales from 720p. Set 0 to disable. Saved between sessions.", MUTED, 12)
	hint.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	rows.add_child(hint)

func _layout() -> void:
	var viewport_size := get_viewport().get_visible_rect().size
	var logical_size := viewport_size / UI_SCALE
	var compact := logical_size.y < 600
	_entry.custom_minimum_size.y = 70 if compact else 110
	_output.custom_minimum_size.y = 40 if compact else 85
	_log.custom_minimum_size.y = 40 if compact else 85
	_panel.scale = Vector2.ONE * UI_SCALE
	_panel.size = Vector2(minf(1060, logical_size.x - 16), minf(740, logical_size.y - 16))
	_panel.position = (viewport_size - _panel.size * UI_SCALE) / 2
	_panel.queue_redraw()

func _request_layout() -> void:
	if not _layout_queued:
		_layout_queued = true
		call_deferred("_settle_layout")

func _settle_layout() -> void:
	_layout_queued = false
	_layout()

static func is_toggle_event(event: InputEvent) -> bool:
	if not event is InputEventKey:
		return false
	var backtick: bool = event.keycode == KEY_QUOTELEFT or event.physical_keycode == KEY_QUOTELEFT
	return (backtick and not event.shift_pressed and not event.ctrl_pressed and not event.alt_pressed and not event.meta_pressed) or (event.keycode == KEY_C and event.ctrl_pressed and event.shift_pressed and not event.alt_pressed and not event.meta_pressed)

func _input(event: InputEvent) -> void:
	if event is InputEventKey and event.keycode == KEY_F5 and event.ctrl_pressed and event.shift_pressed:
		if event.pressed and not event.echo:
			reload_scripts()
		get_viewport().set_input_as_handled()
	elif is_toggle_event(event):
		if event.pressed and not event.echo:
			toggle()
		get_viewport().set_input_as_handled()
	elif is_open() and event is InputEventKey and event.keycode == KEY_ESCAPE:
		if event.pressed and not event.echo:
			toggle()
		get_viewport().set_input_as_handled()

func is_open() -> bool:
	return _root != null and _root.visible

func toggle() -> void:
	_root.visible = not _root.visible
	if _root.visible:
		_request_layout()
		if _tabs.current_tab == 0:
			_entry.grab_focus()
	else:
		_entry.release_focus()
	toggled.emit(_root.visible)

func _write(view: TextEdit, text: String) -> void:
	# Append through the editor API so selecting / copying old results stays undisturbed.
	view.insert_text(text, view.get_line_count() - 1, view.get_line(view.get_line_count() - 1).length())
	if view.text.length() > 200000:
		view.text = view.text.right(160000)
	_scroll_to_bottom.call_deferred(view)

func _scroll_to_bottom(view: TextEdit) -> void:
	# Wrapped text updates the scroll range after insertion. Read the new range
	# once that update has run, even if the user had scrolled up before the append.
	if is_instance_valid(view):
		view.set_v_scroll(view.get_v_scroll_bar().max_value)

func _append(text: String) -> void:
	_write(_output, text)
	_write(_log, text)

func _clear_visible() -> void:
	(_output if _tabs.current_tab == 0 else _log).text = ""
	_entry.grab_focus() if _tabs.current_tab == 0 else _log.grab_focus()

func _load_history() -> void:
	var config := ConfigFile.new()
	var error := config.load(history_path)
	if error != OK and error != ERR_FILE_NOT_FOUND:
		push_warning("Could not load console history: " + error_string(error))
	if error == OK:
		var commands = config.get_value("console", "history", [])
		if commands is Array:
			for command in commands:
				if command is String and not command.strip_edges().is_empty():
					_history.append(command)
			_history = _history.slice(maxi(0, _history.size() - 256))
	_history_index = _history.size()

func _save_history() -> void:
	var config := ConfigFile.new()
	config.set_value("console", "history", _history)
	var error := config.save(history_path)
	if error != OK:
		push_warning("Could not save console history: " + error_string(error))

func _submit(code: String) -> void:
	if code.strip_edges().is_empty():
		return
	if _history.is_empty() or _history[-1] != code:
		_history.append(code)
		if _history.size() > 256:
			_history.pop_front()
		_save_history()
	_history_index = _history.size()
	_draft = ""
	_entry.clear()
	if code.strip_edges() == "clear":
		_clear_visible()
		return
	_append("\n› " + code + "\n")
	var result: Dictionary = execute.call(code)
	var output := str(result.get("output", ""))
	if not output.is_empty():
		_append(output + ("" if output.ends_with("\n") else "\n"))
	elif not result.get("ok", false):
		_append("Command failed.\n")
	var ok: bool = result.get("ok", false)
	_status.text = "READY · command complete" if ok else "ERROR · see output"
	_status.add_theme_color_override("font_color", MUTED if ok else Color("e8a18b"))
	if _tabs.current_tab == 0:
		_entry.grab_focus()

func reload_scripts() -> void:
	_append("\n› Reload scripts\n")
	var result: Dictionary = execute.call("bfme.console.reload()")
	var ok: bool = result.get("ok", false)
	var output := str(result.get("output", ""))
	_append(output + ("" if output.ends_with("\n") else "\n"))
	_status.text = "READY · scripts reloaded" if ok else "RELOAD ERROR · see output"
	_status.add_theme_color_override("font_color", MUTED if ok else Color("e8a18b"))

func _entry_input(event: InputEvent) -> void:
	if not event is InputEventKey or not event.pressed:
		return
	if event.keycode == KEY_ENTER or event.keycode == KEY_KP_ENTER:
		if not event.shift_pressed:
			_entry.accept_event()
			if not event.echo:
				_submit(_entry.text)
		return
	if event.keycode != KEY_UP and event.keycode != KEY_DOWN:
		return
	# Keep normal multiline cursor movement away from the first / last editor line.
	if event.keycode == KEY_UP and _entry.get_caret_line() != 0:
		return
	if event.keycode == KEY_DOWN and _entry.get_caret_line() != _entry.get_line_count() - 1:
		return
	if _history_index == _history.size():
		_draft = _entry.text
	_history_index = clampi(_history_index + (-1 if event.keycode == KEY_UP else 1), 0, _history.size())
	_entry.text = _draft if _history_index == _history.size() else _history[_history_index]
	_entry.set_caret_line(_entry.get_line_count() - 1)
	_entry.set_caret_column(_entry.get_line(_entry.get_line_count() - 1).length())
	_entry.accept_event()
