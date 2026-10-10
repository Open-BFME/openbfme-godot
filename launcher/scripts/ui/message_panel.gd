## Lane UI-3: the launcher's two dialogs in one card. An error: a title, what happened in plain words, "Copy details" (the message, the
## launcher's version and platform: home folder already taken out) and "Open log folder". A question: the text, Cancel and a gold button.
extends Control

signal open_logs

const W := preload("res://scripts/ui/widgets.gd")
const VectorIcon := preload("res://scripts/ui/vector_icon.gd")
const Style := preload("res://scripts/ui/style.gd")
const Paths := preload("res://scripts/ui/paths.gd")

var _icon: TextureRect
var _title: Label
var _text: Label
var _copy: Button
var _logs: Button
var _cancel: Button
var _ok: Button
var _details := ""
var _on_ok := Callable()


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
	head.add_child(_title)
	m.body.add_child(head)
	_text = W.wrap("")
	_text.custom_minimum_size.x = 540
	m.body.add_child(_text)
	var row := W.hbox(10)
	_copy = W.button("Copy details", "copy")
	_copy.pressed.connect(func() -> void:
		DisplayServer.clipboard_set(_details)
		_copy.text = "Copied")
	row.add_child(_copy)
	_logs = W.button("Open log folder", "folder")
	_logs.pressed.connect(func() -> void: open_logs.emit())
	row.add_child(_logs)
	row.add_child(W.spacer())
	_cancel = W.button("Cancel")
	_cancel.pressed.connect(func() -> void: visible = false)
	row.add_child(_cancel)
	_ok = W.button("Close", "", "GoldButton")
	_ok.custom_minimum_size.x = 120
	_ok.pressed.connect(func() -> void:
		visible = false
		if _on_ok.is_valid():
			_on_ok.call())
	row.add_child(_ok)
	m.body.add_child(row)


## an error the player should see (the status line keeps a one-line copy); `about` goes into the copied details
func show_error(title: String, text: String, about: String) -> void:
	_title.text = title
	_text.text = Paths.scrub(text)
	_details = Paths.scrub("%s\n%s\n%s" % [title, text, about])
	_icon.visible = true
	_copy.visible = true
	_copy.text = "Copy details"
	_logs.visible = true
	_cancel.visible = false
	_ok.text = "Close"
	_on_ok = Callable()
	visible = true


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
	visible = true
