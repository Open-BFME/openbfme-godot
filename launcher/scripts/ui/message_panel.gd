## Lane UI-3: the launcher's two dialogs in one card. An error: a title, what happened in plain words, "Copy details" (the message, the
## launcher's version and platform: home folder already taken out) and "Open log folder". A question: the text, Cancel and a gold button.
extends Control

signal open_logs
signal closed  # lane UI-5: the player closed the dialog (an error was seen)

const W := preload("res://scripts/ui/widgets.gd")
const VectorIcon := preload("res://scripts/ui/vector_icon.gd")
const Style := preload("res://scripts/ui/style.gd")
const Paths := preload("res://scripts/ui/paths.gd")
const PlayerMessages := preload("res://scripts/ui/player_messages.gd")
const Log := preload("res://scripts/core/launcher_log.gd")
const L10n := preload("res://scripts/l10n/l10n.gd")

var _icon: TextureRect
var _title: Label
var _text: Label
var _copy: Button
var _logs: Button
var _cancel: Button
var _ok: Button
var _details := ""
var _on_ok := Callable()
var _previous_focus: WeakRef


func _init() -> void:
	set_anchors_preset(Control.PRESET_FULL_RECT)
	var m := W.modal("", 600)
	add_child(m.root)
	visible = false
	var head := W.hbox(12)
	_icon = TextureRect.new()
	_icon.texture = VectorIcon.make("warning", 26)
	_icon.stretch_mode = TextureRect.STRETCH_KEEP_CENTERED
	_icon.modulate = Style.WARN
	head.add_child(_icon)
	_title = W.label("", "Heading")
	_title.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_title.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	head.add_child(_title)
	m.body.add_child(head)
	_text = W.wrap("")
	_text.custom_minimum_size.x = 540
	m.body.add_child(_text)
	var row := W.hbox(10)
	_copy = W.button(L10n.t("launcher.message_panel.copy_details"), "copy")
	_copy.pressed.connect(func() -> void:
		DisplayServer.clipboard_set(_details)
		_copy.text = L10n.t("launcher.message_panel.copied"))
	row.add_child(_copy)
	_logs = W.button(L10n.t("launcher.message_panel.open_logs"), "folder")
	_logs.pressed.connect(func() -> void: open_logs.emit())
	row.add_child(_logs)
	row.add_child(W.spacer())
	_cancel = W.button(L10n.t("common.cancel"))
	_cancel.pressed.connect(_dismiss)
	row.add_child(_cancel)
	_ok = W.button(L10n.t("common.close"), "", "GoldButton")
	_ok.custom_minimum_size.x = 120
	_ok.pressed.connect(func() -> void:
		_dismiss()
		if _on_ok.is_valid():
			_on_ok.call())
	row.add_child(_ok)
	m.body.add_child(row)


## an error the player should see (the status line keeps a one-line copy); `about` goes into the copied details
func show_error(title: String, text: String, about: String, operation := PlayerMessages.Operation.GENERAL) -> void:
	Log.line("LAUNCHER ui dialog: " + Paths.scrub(title + ": " + text))
	_title.text = title
	_text.text = PlayerMessages.error_text(text, operation)
	_details = Paths.scrub("%s\n%s\n%s" % [title, text, about])
	_icon.visible = true
	_copy.visible = true
	_copy.text = L10n.t("launcher.message_panel.copy_details")
	_logs.visible = true
	_cancel.visible = false
	_ok.text = L10n.t("common.close")
	_on_ok = Callable()
	_open()


## a question; `on_ok` runs when the player presses `ok_text`
func ask(title: String, text: String, ok_text: String, on_ok: Callable) -> void:
	_title.text = title
	_text.text = Paths.scrub(text)
	_icon.visible = false
	_copy.visible = false
	_logs.visible = false
	_cancel.visible = true
	_ok.text = ok_text
	_on_ok = on_ok
	_open()


func _open() -> void:
	if not visible and is_inside_tree():
		_previous_focus = weakref(get_viewport().gui_get_focus_owner())
	visible = true
	if is_inside_tree():
		var buttons: Array[Button] = []
		for b in [_copy, _logs, _cancel, _ok]:
			if b.visible and not b.disabled:
				buttons.append(b)
		for i in buttons.size():
			var b := buttons[i]
			var previous := b.get_path_to(buttons[posmod(i - 1, buttons.size())])
			var next := b.get_path_to(buttons[(i + 1) % buttons.size()])
			b.focus_previous = previous
			b.focus_next = next
			b.focus_neighbor_left = previous
			b.focus_neighbor_top = previous
			b.focus_neighbor_right = next
			b.focus_neighbor_bottom = next
		(_cancel if _cancel.visible else _ok).grab_focus.call_deferred()


func _dismiss() -> void:
	visible = false
	closed.emit()
	var previous: Control = _previous_focus.get_ref() if _previous_focus != null else null
	if is_instance_valid(previous) and previous.is_visible_in_tree():
		previous.grab_focus()


func _input(event: InputEvent) -> void:
	if not visible:
		return
	if event.is_action_pressed("ui_cancel"):
		_dismiss()
		get_viewport().set_input_as_handled()


## "<title>: <text>" of the dialog (the tests' check of what the window shows)
func shown_text() -> String:
	return "%s: %s" % [_title.text, _text.text]
