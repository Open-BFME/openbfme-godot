## Interactive Lua 4.0.1 console. The owner supplies the executor and manages game input.
extends CanvasLayer

signal toggled(open: bool)
var execute: Callable
var _panel: PanelContainer
var _output: TextEdit
var _entry: LineEdit
var _history: Array[String] = []
var _history_index := 0
var _draft := ""

func _ready() -> void:
	layer = 100
	_panel = PanelContainer.new()
	_panel.set_anchors_and_offsets_preset(Control.PRESET_TOP_WIDE)
	_panel.offset_bottom = 340
	add_child(_panel)
	var rows := VBoxContainer.new()
	_panel.add_child(rows)
	var title := Label.new()
	title.text = "Lua console · F12 / Esc close · Enter run · ↑ / ↓ history"
	rows.add_child(title)
	_output = TextEdit.new()
	_output.editable = false
	_output.custom_minimum_size.y = 260
	_output.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_output.wrap_mode = TextEdit.LINE_WRAPPING_BOUNDARY
	rows.add_child(_output)
	_entry = LineEdit.new()
	_entry.placeholder_text = 'print("hello")  or  =1 + 2'
	rows.add_child(_entry)
	_entry.text_submitted.connect(_submit)
	_entry.gui_input.connect(_entry_input)
	_append("Lua 4.0.1. Menu and game states are separate.\nLive commands disable saving for this match. Use clear to clear output.\n")
	_panel.hide()

func is_open() -> bool:
	return _panel != null and _panel.visible

func toggle() -> void:
	_panel.visible = not _panel.visible
	if _panel.visible:
		_entry.grab_focus()
	else:
		_entry.release_focus()
	toggled.emit(_panel.visible)

func _append(text: String) -> void:
	_output.text += text
	# Bound the retained UI log, even when a command prints a large amount of text.
	if _output.text.length() > 64000:
		_output.text = _output.text.right(64000)
	_output.set_caret_line(_output.get_line_count() - 1)
	_output.set_v_scroll(_output.get_line_count())

func _submit(code: String) -> void:
	if code.strip_edges().is_empty():
		return
	if _history.is_empty() or _history[-1] != code:
		_history.append(code)
		if _history.size() > 100:
			_history.pop_front()
	_history_index = _history.size()
	_draft = ""
	_entry.clear()
	if code.strip_edges() == "clear":
		_output.text = ""
		return
	_append("> " + code + "\n")
	var result: Dictionary = execute.call(code)
	var output := str(result.get("output", ""))
	if not output.is_empty():
		_append(output + ("" if output.ends_with("\n") else "\n"))
	elif not result.get("ok", false):
		_append("Command failed.\n")

func _entry_input(event: InputEvent) -> void:
	if not event is InputEventKey or not event.pressed:
		return
	if event.keycode != KEY_UP and event.keycode != KEY_DOWN:
		return
	if _history_index == _history.size():
		_draft = _entry.text
	_history_index = clampi(_history_index + (-1 if event.keycode == KEY_UP else 1), 0, _history.size())
	_entry.text = _draft if _history_index == _history.size() else _history[_history_index]
	_entry.caret_column = _entry.text.length()
	_entry.accept_event()
