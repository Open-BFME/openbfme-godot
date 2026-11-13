## Lane CRASHDIALOG-1: the dialog a player sees after a crash: what the report holds (the rows, and the exact report behind "View"), a progress bar
## while it is sent, then the report's reference; or, with automatic reports off, the question first. One file, byte for byte the same in the game
## (godot/scripts/crash/crash_dialog.gd) and the launcher (launcher/scripts/ui/crash_dialog.gd): tools/release/test_crash_upload.py checks they match.
## The words come from the caller (the launcher's are translated), the sending from the caller's crash client (config.client, crash_client.gd).
##
## The bar shows the report's stages, not bytes: Godot's HTTPRequest does not say how much of a request body has left (the report is at most 256 KiB).
## While the request runs the bar eases towards SEND_END and never reaches it; only the service's answer fills it.
##
## Nothing leaves this PC before the report is queued; "Don't send" takes it out of the queue again. Closing the dialog while it sends leaves the report
## queued, for the next start (crash_client.gd cancels the request when it leaves the tree).
extends PanelContainer

signal state_changed(state: int)

enum State { IDLE, ASK, SENDING, SENT, KEPT, NOT_SENT }

const ACCENT := Color("f0d891")
const CAPTION := Color("c9a54e")
const LINE := Color("4d3d1f")
const RULE := Color(1, 1, 1, 0.07)
const TEXT := Color("ece2c9")
const TEXT2 := Color("cdbf9f")
const TEXT3 := Color("9c917a")
const WARN := Color("e0a84a")
const GOOD := Color("8fd08a")
const INK := Color("2a1b05")
const GOLD_BUTTON := Color("c99a3a")
const FILL := Color("d9ad4a")
const FILL_DONE := Color("6fae6a")
const WIDTH := 640

const BUILT := 0.10
const QUEUED := 0.25
const SEND_END := 0.90
const SEND_EASE_SECONDS := 2.0

var state: int = State.IDLE
var reference := ""
var error_text := ""
var progress := 0.0

var _config := {}
var _strings := {}
var _client: Node
var _queued_name := ""
var _send_seconds := 0.0
var _title: Label
var _subtitle: Label
var _mark: Label
var _section: Label
var _rows: GridContainer
var _rows_frame: PanelContainer
var _note: Label
var _view: Button
var _report: TextEdit
var _bar_row: Control
var _bar_label: Label
var _bar_percent: Label
var _bar: ProgressBar
var _reference_row: Control
var _reference_label: Label
var _automatic: CheckBox
var _logs: Button
var _dont_send: Button
var _close: Button
var _retry: Button
var _send: Button
var _restart: Button


## config: {strings (the words: required_strings()), data, json, wire (CrashClient.build's result), queue, install_id, client (crash_client.gd's
## script), automatic (send at once, or ask first); optional: fonts {title, body}, log_line, subtitle (replaces the crash wording), on_close, on_logs,
## on_restart (no restart button without it), on_automatic(on: bool) (no check box without it)}
func setup(config: Dictionary) -> void:
	_config = config
	_strings = config.strings
	if _title == null:
		_build()
	_rows_fill(summary_rows(config.data, _strings))
	_report.text = JSON.stringify(config.data, "  ", true)
	_view.text = _strings.view % size_text(config.wire.size())
	_note.text = _strings.not_included
	_logs.visible = (config.get("on_logs", Callable()) as Callable).is_valid()


## Starts: asks first, or sends now. Call once, in the tree.
func start() -> void:
	if _config.get("automatic", false):
		_begin_send()
	else:
		_show(State.ASK)


## The labelled rows of what a report holds, from the report itself. [[label, value]...]
static func summary_rows(data: Dictionary, s: Dictionary) -> Array:
	var client: Dictionary = data.get("client", {})
	var context: Dictionary = data.get("context", {})
	var settings: Dictionary = data.get("settings", {})
	var system := " ".join([str(data.get("os_name", "")), str(data.get("os_version", ""))]).strip_edges()
	var game := [str(context.get("mode", "unknown"))]
	if str(context.get("map", "")) not in ["", "unknown"]:
		game.push_front(str(context.map).get_file())
	var factions: Array = context.get("factions", [])
	if not factions.is_empty():
		game.append(", ".join(factions.map(func(f: Variant) -> String: return str(f))))
	var width: Variant = settings.get("resolution_width", "unknown")
	var height: Variant = settings.get("resolution_height", "unknown")
	var display := "" if typeof(width) == TYPE_STRING or typeof(height) == TYPE_STRING else "%d x %d" % [width, height]
	var vsync: Variant = settings.get("vsync", "unknown")
	if typeof(vsync) == TYPE_BOOL:
		display += ("" if display.is_empty() else ", ") + (s.vsync_on if vsync else s.vsync_off)
	var rows := [
		[s.row_version, "%s, %s" % [str(client.get("version", data.get("version", "unknown"))), system]],
		[s.row_pc, "%s, %s" % [str(data.get("cpu_name", "unknown")), str(data.get("gpu_name", "unknown"))]],
		[s.row_game, ", ".join(game)],
		[s.row_crash, s.lines % (data.get("backtrace", []) as Array).size()],
		[s.row_log, s.log_lines % (data.get("log_tail", []) as Array).size()],
		[s.row_id, s.id_value],
	]
	if not display.is_empty(): # a report without them (the launcher's) has no such row
		rows.insert(3, [s.row_display, display])
	return rows


## "38 KB" (at least 1)
static func size_text(bytes: int) -> String:
	return "%d KB" % maxi(1, int(ceil(bytes / 1024.0)))


## The bar while the request runs: eases from QUEUED towards SEND_END, which it never reaches.
static func send_fraction(seconds: float) -> float:
	return QUEUED + (SEND_END - QUEUED) * (1.0 - exp(-maxf(seconds, 0.0) / SEND_EASE_SECONDS))


## The words the caller gives, every one required (`%s` where the code fills in a value)
static func required_strings() -> PackedStringArray:
	return PackedStringArray(["title", "subtitle", "ask_subtitle", "what", "what_ask", "report_section", "not_included", "view", "hide", "row_version", "row_pc",
		"row_game", "row_display", "row_crash", "row_log", "row_id", "id_value", "lines", "log_lines", "vsync_on", "vsync_off", "preparing",
		"sending", "sent_label", "sent_title", "sent_subtitle", "kept_title", "kept_subtitle", "refused_title", "refused_subtitle", "reference", "copy",
		"copied", "open_logs", "dont_send", "send", "close", "retry", "restart", "automatic"])


func _ready() -> void:
	if _title == null:
		_build()
	set_process(false)


func _process(delta: float) -> void:
	_send_seconds += delta
	_set_progress(send_fraction(_send_seconds))


func _begin_send() -> void:
	error_text = ""
	_set_progress(BUILT)
	if _queued_name.is_empty():
		var queued: Dictionary = _config.client.enqueue(_config.queue, _config.wire)
		for dropped: Array in queued.dropped:
			_log("CRASH report %s dropped: %s" % dropped)
		if not queued.ok:
			error_text = str(queued.error)
			_log("CRASH report not queued: " + error_text)
			_show(State.KEPT)
			return
		_queued_name = str(queued.path).get_file()
		_log("CRASH report queued: " + _queued_name)
	_set_progress(QUEUED)
	_send_seconds = 0.0
	_show(State.SENDING)
	set_process(true)
	if _client == null:
		_client = _config.client.new()
		_client.name = "CrashDialogUpload"
		_client.log_line = Callable(self, "_log")
		add_child(_client)
	var summary: Dictionary = await _client.upload_queue(_config.queue, _config.install_id, _queued_name)
	if state != State.SENDING or not is_inside_tree():
		return # "Don't send" came first
	set_process(false)
	for sent: Array in summary.sent:
		if sent[0] == _queued_name:
			reference = str(sent[1])
			_queued_name = ""
			_set_progress(1.0)
			_show(State.SENT)
			return
	for dropped: Array in summary.dropped:
		if dropped[0] == _queued_name:
			error_text = str(dropped[1])
			_queued_name = ""
			_show(State.NOT_SENT)
			return
	error_text = str(summary.error)
	_show(State.KEPT)


func _on_dont_send() -> void:
	var was := state
	_show(State.IDLE) # first: the upload's answer is not this dialog's any more
	set_process(false)
	if _client != null:
		_client.cancel()
	if not _queued_name.is_empty():
		var removed := DirAccess.remove_absolute(ProjectSettings.globalize_path((_config.queue as String).path_join(_queued_name)))
		_log("CRASH report %s not sent: the player chose so%s" % [_queued_name, "" if removed == OK else " (it stays queued: %s)" % error_string(removed)])
		_queued_name = ""
	else:
		_log("CRASH the player chose not to send the report (asked: %s)" % ("yes" if was == State.ASK else "no"))
	_finish()


func _on_send() -> void:
	if _automatic.visible and _automatic.button_pressed:
		(_config.on_automatic as Callable).call(true)
	_begin_send()


func _finish() -> void:
	var on_close: Callable = _config.get("on_close", Callable())
	if on_close.is_valid():
		on_close.call()


func _call_optional(key: String) -> void:
	var c: Callable = _config.get(key, Callable())
	if c.is_valid():
		c.call()


func _log(text: String) -> void:
	var c: Callable = _config.get("log_line", Callable())
	if c.is_valid():
		c.call(text)
	else:
		print(text)


func _set_progress(value: float) -> void:
	progress = value
	_bar.value = value * 100.0
	_bar_percent.text = "%d%%" % int(round(value * 100.0))


func _show(next: int) -> void:
	state = next
	var s := _strings
	var problem := next in [State.KEPT, State.NOT_SENT]
	var done := next == State.SENT
	match next:
		State.ASK:
			_title.text = s.title
			_subtitle.text = _config.get("subtitle", s.ask_subtitle)
		State.SENDING:
			_title.text = s.title
			_subtitle.text = _config.get("subtitle", s.subtitle)
		State.SENT:
			_title.text = s.sent_title
			_subtitle.text = s.sent_subtitle
		State.KEPT:
			_title.text = s.kept_title
			_subtitle.text = s.kept_subtitle
		State.NOT_SENT:
			_title.text = s.refused_title
			_subtitle.text = s.refused_subtitle
	_mark.text = "✓" if done else "!"
	_mark.add_theme_color_override("font_color", GOOD if done else WARN)
	var before_sent := next in [State.ASK, State.SENDING]
	_section.visible = before_sent
	_rows_frame.visible = before_sent and not _report.visible
	_note.visible = before_sent
	_view.visible = before_sent
	_report.visible = before_sent and _report.visible
	_section.text = _section_text()
	_bar_row.visible = next in [State.SENDING, State.SENT]
	_bar_label.text = s.sent_label if done else s.sending % size_text((_config.wire as PackedByteArray).size())
	_bar.add_theme_stylebox_override("fill", _box(FILL_DONE if done else FILL, 4))
	_reference_row.visible = done and not reference.is_empty()
	_reference_label.text = reference
	_automatic.visible = next == State.ASK and (_config.get("on_automatic", Callable()) as Callable).is_valid()
	_dont_send.visible = before_sent
	_send.visible = next == State.ASK
	_retry.visible = next == State.KEPT and not _queued_name.is_empty()
	_close.visible = done or problem
	_restart.visible = (done or problem) and (_config.get("on_restart", Callable()) as Callable).is_valid()
	state_changed.emit(next)
	if is_inside_tree():
		var focus: Button = _send if _send.visible else (_restart if _restart.visible else (_close if _close.visible else _dont_send))
		focus.grab_focus.call_deferred()


func _on_view() -> void:
	_report.visible = not _report.visible
	_rows_frame.visible = not _report.visible
	_section.text = _section_text()
	_view.text = _strings.hide if _report.visible else _strings.view % size_text((_config.wire as PackedByteArray).size())


func _section_text() -> String:
	if _report.visible:
		return _strings.report_section
	return _strings.what_ask if state == State.ASK else _strings.what


func _rows_fill(rows: Array) -> void:
	for child in _rows.get_children():
		child.free()
	for row: Array in rows:
		var name_label := _label(str(row[0]), TEXT2)
		name_label.custom_minimum_size.x = 130
		_rows.add_child(name_label)
		var value := _label(str(row[1]), TEXT)
		value.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		value.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
		_rows.add_child(value)


func _build() -> void:
	process_mode = Node.PROCESS_MODE_ALWAYS # a paused tree (the fatal error) still sends
	custom_minimum_size.x = WIDTH
	var look := _box(Color("141317f5"), 10)
	look.border_color = LINE
	look.set_border_width_all(1)
	look.content_margin_left = 22
	look.content_margin_right = 22
	look.content_margin_top = 20
	look.content_margin_bottom = 20
	add_theme_stylebox_override("panel", look)
	var body := VBoxContainer.new()
	body.add_theme_constant_override("separation", 10)
	add_child(body)
	var head := HBoxContainer.new()
	head.add_theme_constant_override("separation", 12)
	body.add_child(head)
	_mark = _label("!", WARN)
	_mark.add_theme_font_size_override("font_size", 22)
	_mark.size_flags_vertical = Control.SIZE_SHRINK_BEGIN
	head.add_child(_mark)
	var words := VBoxContainer.new()
	words.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	head.add_child(words)
	_title = _label("", ACCENT)
	_title.add_theme_font_size_override("font_size", 20)
	var fonts: Dictionary = _config.get("fonts", {})
	if fonts.has("title"):
		_title.add_theme_font_override("font", fonts.title)
	words.add_child(_title)
	_subtitle = _label("", TEXT2)
	_subtitle.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	words.add_child(_subtitle)
	_section = _label("", CAPTION)
	_section.add_theme_font_size_override("font_size", 12)
	body.add_child(_section)
	_rows_frame = PanelContainer.new()
	var frame := _box(Color(12 / 255.0, 12 / 255.0, 14 / 255.0, 0.72), 7)
	frame.border_color = RULE
	frame.set_border_width_all(1)
	frame.content_margin_left = 11
	frame.content_margin_right = 11
	frame.content_margin_top = 8
	frame.content_margin_bottom = 8
	_rows_frame.add_theme_stylebox_override("panel", frame)
	body.add_child(_rows_frame)
	_rows = GridContainer.new()
	_rows.columns = 2
	_rows.add_theme_constant_override("h_separation", 12)
	_rows.add_theme_constant_override("v_separation", 6)
	_rows_frame.add_child(_rows)
	_report = TextEdit.new()
	_report.editable = false
	_report.visible = false
	_report.wrap_mode = TextEdit.LINE_WRAPPING_BOUNDARY
	_report.custom_minimum_size = Vector2(0, 236)
	_report.add_theme_font_size_override("font_size", 12)
	_report.add_theme_color_override("font_readonly_color", TEXT2)
	body.add_child(_report)
	var note_row := HBoxContainer.new()
	note_row.add_theme_constant_override("separation", 8)
	body.add_child(note_row)
	_note = _label("", TEXT3)
	_note.add_theme_font_size_override("font_size", 12)
	_note.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_note.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	note_row.add_child(_note)
	_view = Button.new()
	_view.flat = true
	_view.add_theme_color_override("font_color", ACCENT)
	_view.add_theme_font_size_override("font_size", 12)
	_view.pressed.connect(_on_view)
	note_row.add_child(_view)
	_bar_row = VBoxContainer.new()
	_bar_row.add_theme_constant_override("separation", 5)
	body.add_child(_bar_row)
	var bar_words := HBoxContainer.new()
	_bar_row.add_child(bar_words)
	_bar_label = _label("", TEXT2)
	_bar_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	bar_words.add_child(_bar_label)
	_bar_percent = _label("", TEXT2)
	bar_words.add_child(_bar_percent)
	_bar = ProgressBar.new()
	_bar.show_percentage = false
	_bar.custom_minimum_size.y = 10
	var trough := _box(Color("050506"), 4)
	trough.border_color = LINE
	trough.set_border_width_all(1)
	_bar.add_theme_stylebox_override("background", trough)
	_bar.add_theme_stylebox_override("fill", _box(FILL, 4))
	_bar_row.add_child(_bar)
	_reference_row = HBoxContainer.new()
	_reference_row.add_theme_constant_override("separation", 10)
	body.add_child(_reference_row)
	var reference_name := _label("", TEXT2)
	_reference_row.add_child(reference_name)
	_reference_label = _label("", TEXT)
	_reference_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_reference_row.add_child(_reference_label)
	var copy := _button("")
	_reference_row.add_child(copy)
	copy.pressed.connect(func() -> void:
		DisplayServer.clipboard_set(reference)
		copy.text = _strings.copied)
	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", 10)
	body.add_child(row)
	_automatic = CheckBox.new()
	_automatic.add_theme_color_override("font_color", TEXT2)
	_automatic.add_theme_icon_override("unchecked", _check_icon(false))
	_automatic.add_theme_icon_override("checked", _check_icon(true))
	row.add_child(_automatic)
	_logs = _button("")
	_logs.pressed.connect(func() -> void: _call_optional("on_logs"))
	row.add_child(_logs)
	var gap := Control.new()
	gap.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	row.add_child(gap)
	_dont_send = _button("")
	_dont_send.pressed.connect(_on_dont_send)
	row.add_child(_dont_send)
	_close = _button("")
	_close.pressed.connect(_finish)
	row.add_child(_close)
	_retry = _button("")
	_retry.pressed.connect(_begin_send)
	row.add_child(_retry)
	_send = _gold_button("")
	_send.pressed.connect(_on_send)
	row.add_child(_send)
	_restart = _gold_button("")
	_restart.pressed.connect(func() -> void: _call_optional("on_restart"))
	row.add_child(_restart)
	if fonts.has("body"):
		for control: Control in [_subtitle, _section, _note, _view, _bar_label, _bar_percent, reference_name, _reference_label, copy, _automatic, _logs,
				_dont_send, _close, _retry, _send, _restart]:
			control.add_theme_font_override("font", fonts.body)
	# the words that never change (the rest are set by _show)
	var s: Dictionary = _config.get("strings", {})
	if not s.is_empty():
		reference_name.text = s.reference
		copy.text = s.copy
		_automatic.text = s.automatic
		_logs.text = s.open_logs
		_dont_send.text = s.dont_send
		_close.text = s.close
		_retry.text = s.retry
		_send.text = s.send
		_restart.text = s.restart
		_title.text = s.title
		_subtitle.text = _config.get("subtitle", s.subtitle)
		_bar_label.text = s.preparing
		_section.text = s.what
	for hidden: Control in [_automatic, _bar_row, _reference_row, _dont_send, _close, _retry, _send, _restart]:
		hidden.visible = false


func _label(text: String, color: Color) -> Label:
	var label := Label.new()
	label.text = text
	label.add_theme_color_override("font_color", color)
	return label


func _button(text: String) -> Button:
	var button := Button.new()
	button.text = text
	button.custom_minimum_size.y = 32
	var normal := _box(Color(1, 1, 1, 0.05), 7)
	normal.border_color = LINE
	normal.set_border_width_all(1)
	normal.content_margin_left = 14
	normal.content_margin_right = 14
	button.add_theme_stylebox_override("normal", normal)
	var hover := normal.duplicate() as StyleBoxFlat
	hover.bg_color = Color(1, 1, 1, 0.1)
	button.add_theme_stylebox_override("hover", hover)
	button.add_theme_stylebox_override("pressed", hover)
	var focus := normal.duplicate() as StyleBoxFlat
	focus.bg_color = Color(0, 0, 0, 0)
	focus.border_color = ACCENT
	button.add_theme_stylebox_override("focus", focus)
	button.add_theme_color_override("font_color", TEXT)
	button.add_theme_color_override("font_hover_color", ACCENT)
	return button


func _gold_button(text: String) -> Button:
	var button := _button(text)
	var gold := button.get_theme_stylebox("normal").duplicate() as StyleBoxFlat
	gold.bg_color = GOLD_BUTTON
	gold.border_color = Color("a77a22")
	button.add_theme_stylebox_override("normal", gold)
	var hover := gold.duplicate() as StyleBoxFlat
	hover.bg_color = Color("e9be56")
	button.add_theme_stylebox_override("hover", hover)
	button.add_theme_stylebox_override("pressed", hover)
	for color: String in ["font_color", "font_hover_color", "font_pressed_color", "font_focus_color"]:
		button.add_theme_color_override(color, INK)
	return button


## the check box's two pictures: Godot's default ones hardly show on this dark panel
func _check_icon(checked: bool) -> ImageTexture:
	const SIZE := 16
	var image := Image.create_empty(SIZE, SIZE, false, Image.FORMAT_RGBA8)
	for y in SIZE:
		for x in SIZE:
			var edge := x == 0 or y == 0 or x == SIZE - 1 or y == SIZE - 1
			var inside := x >= 4 and y >= 4 and x < SIZE - 4 and y < SIZE - 4
			image.set_pixel(x, y, TEXT3 if edge else (ACCENT if checked and inside else Color("050506")))
	return ImageTexture.create_from_image(image)


func _box(color: Color, radius: int) -> StyleBoxFlat:
	var box := StyleBoxFlat.new()
	box.bg_color = color
	box.set_corner_radius_all(radius)
	return box
