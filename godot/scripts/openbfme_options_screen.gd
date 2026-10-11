## Lane OPTS-1: the OpenBFME options screen (owner, 2026-10-10: "the game also needs like modern options"). NOT A RETAIL SCREEN: RotWK's Options.apt
## stays as retail's; this screen is an OpenBFME extra reached from it. While Options.apt is the top screen of the shell (from the main menu, or from the
## quit menu in a game) an "OPENBFME" button sits in the free band under its Audio Controls panel; it opens this screen over Options.apt.
##
## The screen is Godot UI laid out on Options.apt's 1024 x 768 stage and mapped to the window the way the shell maps the movie (AptMenuPlayer
## stage_to_window), so it scales with the menus. Its skin is the retail menu art loaded at run time through the shell, never committed: the gadget
## images of the APT gadgets (MappedImage AptCheckbox*, AptComboBoxButton*, AptHSlider*, AptListBoxHilite* of aptcomponents.ini, as the gadget layer
## draws them: AptMenuPlayer.shell_mapped_image) and the fonts the movies' requests resolve to (fontsubstitution.ini and the archives' fonts:
## AptMenuPlayer.shell_font, "SachaWynter" for the title, "Albertus MT" for the text, the font of window\apt\*.wnd). The panel frames and the buttons
## are drawn in the style of Options.apt's (thin double lines over a dark fill); they are Godot style boxes, not the movie's shapes.
##
## The settings are scripts/display_settings.gd's (openbfme-display.cfg). Every change applies at once (window mode, resolution, vsync, frame cap,
## counter, render scale, scaler); ACCEPT keeps them and writes the file, CANCEL and Esc put back what was in effect when the screen opened, RESET TO
## DEFAULTS sets the defaults (applied, kept on ACCEPT).
## While the screen is up the shell's and the HUD's input is off (AptMenuPlayer / InGameHud set_process_input(false), restored on close), so a click
## on the screen never reaches Options.apt under it. The screen never calls into the shell's screens: a window change made through it cannot change a
## widget's state (the owner's "maximising greys out the main menu"; tests/opts1_test.gd checks the main menu's and Options.apt's buttons across
## every mode change).
extends CanvasLayer

const DisplaySettings := preload("res://scripts/display_settings.gd")
const STAGE := Vector2(1024, 768)
const ENTRY_RECT := Rect2(412, 616, 200, 30)      # the band under Options.apt's Audio Controls panel (free on both of its pages)
const PANEL_RECT := Rect2(197, 104, 630, 560)
const LABEL_COLOR := Color(0.76, 0.87, 0.95)       # the pale blue of Options.apt's headings (the owner wants no green label text)
const TEXT_COLOR := Color(0.88, 0.92, 0.96)
const TITLE_COLOR := Color(0.62, 0.84, 0.90)
const FRAME_COLOR := Color(0.70, 0.77, 0.84, 0.85)
const WINDOW_MODE_LABELS := {"windowed": "Windowed", "borderless": "Borderless window", "fullscreen": "Fullscreen"}
const VSYNC_LABELS := {"on": "On", "off": "Off", "adaptive": "Adaptive", "mailbox": "Mailbox"}
const SCALER_LABELS := {"bilinear": "Bilinear", "fsr": "AMD FSR 1", "fsr2": "AMD FSR 2"}
const RENDERER_LABELS := {"forward_plus": "Forward+", "mobile": "Mobile", "gl_compatibility": "Compatibility"}

var shell: Node2D                  # the AptMenuPlayer
var hud_getter: Callable           # () -> the InGameHud node or null
var settings: RefCounted           # the settings in effect (display_settings.gd), shared with game.gd and the counter
var settings_path := ""
var overlay: CanvasLayer           # scripts/fps_overlay.gd
var skin_errors := PackedStringArray()

var _stage: Control                # the 1024 x 768 stage, scaled to the window
var _entry: Button
var _screen: Control
var _theme: Theme
var _open := false
var _original: RefCounted          # the settings when the screen opened (CANCEL)
var _runtime := {}                 # what the game ran with when the screen opened (DisplaySettings.snapshot: CLI caps, the window's mode and place)
var _pending: RefCounted           # what the screen shows and has applied
var _applied: RefCounted
var _blocked: Array = []           # [node, was processing input] while the shell's / HUD's input is off
var _entry_hover := false
var _syncing := false
var _controls := {}
var _last_window := Vector2.ZERO


func _ready() -> void:
	layer = 80
	process_mode = Node.PROCESS_MODE_ALWAYS
	_theme = _build_theme()
	_stage = Control.new()
	_stage.name = "Stage"
	_stage.size = STAGE
	_stage.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_stage.theme = _theme
	add_child(_stage)
	_entry = _make_button("OPENBFME", ENTRY_RECT)
	_entry.name = "OpenBFMEEntry"
	_entry.tooltip_text = ""
	_entry.visible = false
	_entry.pressed.connect(open)
	_entry.mouse_entered.connect(_on_entry_hover.bind(true))
	_entry.mouse_exited.connect(_on_entry_hover.bind(false))
	_stage.add_child(_entry)
	_build_screen()
	if overlay != null:
		overlay.screen = self
	for e in skin_errors:
		printerr("GAME DISPLAY skin: ", e)


func is_open() -> bool:
	return _open


func entry_visible() -> bool:
	return _entry.visible


## the window rectangle of a stage rectangle (tests click through it)
func stage_rect_to_window(r: Rect2) -> Rect2:
	var a: Vector2 = shell.stage_to_window(r.position)
	var b: Vector2 = shell.stage_to_window(r.end)
	return Rect2(a, b - a)


func control(name: String) -> Control:
	return _controls.get(name)


func _process(_delta: float) -> void:
	if shell == null:
		return
	var window := Vector2(get_viewport().get_visible_rect().size)
	if window != _last_window:
		_last_window = window
		var a: Vector2 = shell.stage_to_window(Vector2.ZERO)
		var b: Vector2 = shell.stage_to_window(STAGE)
		_stage.position = a
		_stage.scale = (b - a) / STAGE
	var stack: PackedStringArray = shell.shell_stack()
	var options_top := not stack.is_empty() and stack[-1] == "Options.apt"
	if _open and not options_top:
		close(false) # the shell left Options.apt under the screen (a game ended, the quit menu closed): the screen goes with it
	var show_entry := options_top and not _open
	if _entry.visible != show_entry:
		_entry.visible = show_entry
		if not show_entry and _entry_hover:
			_entry_hover = false
			if not _open:
				_block_input(false)


func _input(event: InputEvent) -> void:
	if not _open:
		return
	var key := event as InputEventKey
	if key != null and key.pressed and not key.echo and key.keycode == KEY_ESCAPE:
		get_viewport().set_input_as_handled()
		close(false)


func open() -> void:
	if _open:
		return
	_open = true
	var root := get_tree().root
	_original = settings.copy()
	_runtime = DisplaySettings.snapshot(root)
	# the screen shows what is in effect (a --max-fps cap, a window the player maximised or Alt+Enter made full screen), not only the file
	_pending = DisplaySettings.effective(settings, root, overlay.shown() if overlay != null else settings.show_fps)
	_applied = _pending.copy()
	_block_input(true)
	_entry.visible = false
	_sync_controls()
	_screen.visible = true
	print("GAME DISPLAY options screen open: ", JSON.stringify(_pending.to_dict()))


## ACCEPT (keep = true): the settings shown become the ones in effect and are written; otherwise the ones of the opening are applied again.
func close(keep: bool) -> void:
	if not _open:
		return
	if keep:
		settings.from_dict(_pending.to_dict())
		if not settings_path.is_empty() and not settings.save_file(settings_path):
			printerr("GAME DISPLAY ", settings.errors[-1])
		print("GAME DISPLAY accepted and saved: ", JSON.stringify(settings.to_dict()))
	else:
		# everything the game ran with at the opening comes back (not the file's values: a --max-fps cap or a maximised window stays as it was);
		# the file is untouched (the hotkeys pressed while the screen was up changed only the screen's settings)
		DisplaySettings.restore(get_tree().root, _runtime)
		settings.from_dict(_original.to_dict())
		print("GAME DISPLAY cancelled: %s" % JSON.stringify(DisplaySettings.snapshot(get_tree().root)))
	if overlay != null:
		overlay.preview = null
		overlay.refresh()
	_open = false
	_screen.visible = false
	_block_input(false)


## A setting changed on the screen: applied at once
func set_value(key: String, value) -> void:
	if not _open:
		return
	_pending.from_dict({key: value})
	_apply_pending()
	_sync_controls()


func reset_defaults() -> void:
	if not _open:
		return
	_pending = DisplaySettings.new()
	_apply_pending()
	_sync_controls()


func _apply_pending() -> void:
	var r: Dictionary = _pending.apply(get_tree().root, _applied)
	_applied = _pending.copy()
	if overlay != null:
		overlay.preview = _pending.show_fps # the counter follows the box at once; the file only on ACCEPT
		overlay.refresh()
	print("GAME DISPLAY applied: %s%s" % [", ".join(r.applied), "" if r.problems.is_empty() else " PROBLEMS " + str(r.problems)])


func _on_entry_hover(inside: bool) -> void:
	# the entry is drawn over Options.apt: a click on it must not also reach the movie
	_entry_hover = inside
	if inside:
		_block_input(true)
	elif not _open:
		_block_input(false)


func _changed(key: String, value) -> void:
	if not _syncing:
		set_value(key, value)


## F11 / Alt+Enter while the screen is up (fps_overlay.gd hands them here): a change of the screen's settings like a click on its controls, applied
## at once, written only by ACCEPT
func hotkey(key: String, value) -> void:
	set_value(key, value)


func pending_value(key: String):
	return _pending.to_dict()[key] if _open else null


func _block_input(block: bool) -> void:
	if block:
		if not _blocked.is_empty():
			return
		var nodes: Array = [shell]
		var hud: Node = hud_getter.call() if hud_getter.is_valid() else null
		if hud != null:
			nodes.append(hud)
		for n in nodes:
			_blocked.append([n, n.is_processing_input()])
			n.set_process_input(false)
	else:
		for pair in _blocked:
			if is_instance_valid(pair[0]):
				pair[0].set_process_input(pair[1])
		_blocked.clear()


# ---------------------------------------------------------------------------------------------------------------------------------
# the skin
# ---------------------------------------------------------------------------------------------------------------------------------
func _image(name: String) -> Texture2D:
	if shell == null:
		skin_errors.append("no shell for image %s" % name)
		return null
	var r: Dictionary = shell.shell_mapped_image(name)
	if not r.get("ok", false):
		skin_errors.append(str(r.get("error", name)))
		return null
	var t := AtlasTexture.new()
	t.atlas = r.texture
	t.region = r.region
	return t


func _font(name: String) -> Font:
	var r: Dictionary = shell.shell_font(name, 16.0) if shell != null else {}
	if r.is_empty() or r.get("font") == null:
		skin_errors.append("font %s: not resolved" % name)
		return null
	if r.get("fallback", false):
		skin_errors.append("font %s: neither fontsubstitution.ini nor the archives' fonts have it (the engine's default font draws it)" % name)
	return r.font


func _frame_box(fill: Color, border: Color, width: int) -> StyleBoxFlat:
	var s := StyleBoxFlat.new()
	s.bg_color = fill
	s.border_color = border
	s.set_border_width_all(width)
	s.set_content_margin_all(6)
	return s


func _texture_box(texture: Texture2D, margins: int) -> StyleBox:
	if texture == null:
		return null
	var s := StyleBoxTexture.new()
	s.texture = texture
	s.set_texture_margin_all(margins)
	s.set_content_margin_all(4)
	return s


func _build_theme() -> Theme:
	var t := Theme.new()
	var text_font := _font("Albertus MT")
	if text_font != null:
		t.default_font = text_font
	t.default_font_size = 16
	t.set_color("font_color", "Label", TEXT_COLOR)
	# buttons in the manner of Options.apt's CANCEL / ACCEPT: a steel frame over a dark blue fill, capitals
	var normal := _frame_box(Color(0.04, 0.08, 0.13, 0.92), Color(0.52, 0.64, 0.74), 2)
	normal.shadow_color = Color(0, 0, 0, 0.6)
	normal.shadow_size = 3
	var hover := _frame_box(Color(0.08, 0.15, 0.22, 0.95), Color(0.80, 0.90, 0.98), 2)
	var pressed := _frame_box(Color(0.02, 0.05, 0.09, 0.95), Color(0.80, 0.90, 0.98), 2)
	var disabled := _frame_box(Color(0.04, 0.07, 0.10, 0.6), Color(0.30, 0.36, 0.42), 2)
	for cls in ["Button", "OptionButton"]:
		t.set_stylebox("normal", cls, normal)
		t.set_stylebox("hover", cls, hover)
		t.set_stylebox("pressed", cls, pressed)
		t.set_stylebox("hover_pressed", cls, pressed)
		t.set_stylebox("disabled", cls, disabled)
		t.set_stylebox("focus", cls, StyleBoxEmpty.new())
		t.set_color("font_color", cls, TEXT_COLOR)
		t.set_color("font_hover_color", cls, Color(1, 1, 1))
		t.set_color("font_pressed_color", cls, Color(1, 0.92, 0.6))
		t.set_color("font_disabled_color", cls, Color(0.5, 0.55, 0.6))
	# the combo boxes: window\apt\combobox.wnd's drop-down button image as the arrow, the list box's highlight in the list
	var arrow := _image("AptComboBoxButtonEnabled")
	if arrow != null:
		t.set_icon("arrow", "OptionButton", arrow)
	t.set_constant("arrow_margin", "OptionButton", 2)
	var popup_panel := _frame_box(Color(0.02, 0.04, 0.07, 0.96), FRAME_COLOR, 1)
	t.set_stylebox("panel", "PopupMenu", popup_panel)
	var hilite := _texture_box(_image("AptListBoxHiliteItem"), 4)
	if hilite != null:
		t.set_stylebox("hover", "PopupMenu", hilite)
	t.set_color("font_color", "PopupMenu", TEXT_COLOR)
	t.set_color("font_hover_color", "PopupMenu", Color(1, 1, 1))
	t.set_color("font_disabled_color", "PopupMenu", Color(0.45, 0.5, 0.55))
	# the check boxes: window\apt\checkbox.wnd's images
	var checked := _image("AptCheckboxCheckedEnabled")
	var unchecked := _image("AptCheckboxUncheckedEnabled")
	var checked_off := _image("AptCheckboxCheckedDisabled")
	var unchecked_off := _image("AptCheckboxUncheckedDisabled")
	for pair in [["checked", checked], ["unchecked", unchecked], ["checked_disabled", checked_off], ["unchecked_disabled", unchecked_off]]:
		if pair[1] != null:
			t.set_icon(pair[0], "CheckBox", _scaled_icon(pair[1], 26))
	for state in ["normal", "hover", "pressed", "hover_pressed", "focus", "disabled"]:
		t.set_stylebox(state, "CheckBox", StyleBoxEmpty.new())
	t.set_color("font_color", "CheckBox", LABEL_COLOR)
	t.set_color("font_hover_color", "CheckBox", Color(1, 1, 1))
	t.set_color("font_pressed_color", "CheckBox", LABEL_COLOR)
	t.set_color("font_hover_pressed_color", "CheckBox", Color(1, 1, 1))
	t.set_color("font_focus_color", "CheckBox", LABEL_COLOR)
	# the sliders: window\apt\horzslider.wnd's ribbed bar (AptHSliderOnBar, tiled) in a dark track
	t.set_stylebox("slider", "HSlider", _frame_box(Color(0.0, 0.0, 0.0, 0.55), FRAME_COLOR, 1))
	var bar := _image("AptHSliderOnBar")
	if bar != null:
		var fill := StyleBoxTexture.new()
		fill.texture = bar
		fill.axis_stretch_horizontal = StyleBoxTexture.AXIS_STRETCH_MODE_TILE
		fill.set_content_margin_all(0)
		fill.expand_margin_top = 8
		fill.expand_margin_bottom = 8
		t.set_stylebox("grabber_area", "HSlider", fill)
		t.set_stylebox("grabber_area_highlight", "HSlider", fill)
	var end := _image("AptHSliderOnBarEnd")
	if end != null:
		t.set_icon("grabber", "HSlider", end)
		t.set_icon("grabber_highlight", "HSlider", end)
	t.set_stylebox("normal", "SpinBox", _frame_box(Color(0.0, 0.0, 0.0, 0.55), FRAME_COLOR, 1))
	t.set_stylebox("normal", "LineEdit", _frame_box(Color(0.0, 0.0, 0.0, 0.55), FRAME_COLOR, 1))
	t.set_stylebox("focus", "LineEdit", _frame_box(Color(0.0, 0.0, 0.0, 0.55), Color(0.80, 0.90, 0.98), 1))
	t.set_color("font_color", "LineEdit", TEXT_COLOR)
	t.set_stylebox("panel", "TooltipPanel", popup_panel)
	return t


func _scaled_icon(texture: Texture2D, size: int) -> Texture2D:
	# a 34 x 34 image at the screen's box size; drawn by the stage's scale like the rest
	var img := texture.get_image()
	if img == null:
		return texture
	img.resize(size, size, Image.INTERPOLATE_BILINEAR)
	return ImageTexture.create_from_image(img)


# ---------------------------------------------------------------------------------------------------------------------------------
# the screen
# ---------------------------------------------------------------------------------------------------------------------------------
func _make_button(text: String, rect: Rect2) -> Button:
	var b := Button.new()
	b.text = text
	b.position = rect.position
	b.size = rect.size
	b.focus_mode = Control.FOCUS_NONE
	b.add_theme_font_size_override("font_size", 15)
	return b


func _label(text: String, pos: Vector2, size: Vector2, color: Color, font_size: int, align := HORIZONTAL_ALIGNMENT_LEFT) -> Label:
	var l := Label.new()
	l.text = text
	l.set_meta("game_text", true) # the screen's text (game.gd _dev_text_on_screen)
	l.position = pos
	l.size = size
	l.horizontal_alignment = align
	l.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	l.add_theme_color_override("font_color", color)
	l.add_theme_font_size_override("font_size", font_size)
	l.mouse_filter = Control.MOUSE_FILTER_IGNORE
	return l


func _panel(rect: Rect2, parent: Control) -> Control:
	# Options.apt's panel frame: an outer and an inner thin line over a dark fill
	var outer := Panel.new()
	outer.position = rect.position
	outer.size = rect.size
	outer.add_theme_stylebox_override("panel", _frame_box(Color(0.01, 0.03, 0.06, 0.86), FRAME_COLOR, 1))
	outer.mouse_filter = Control.MOUSE_FILTER_STOP
	parent.add_child(outer)
	var inner := Panel.new()
	inner.position = Vector2(4, 4)
	inner.size = rect.size - Vector2(8, 8)
	inner.add_theme_stylebox_override("panel", _frame_box(Color(0, 0, 0, 0), Color(FRAME_COLOR, 0.55), 1))
	inner.mouse_filter = Control.MOUSE_FILTER_IGNORE
	outer.add_child(inner)
	return outer


func _section(title: String, y: float, parent: Control) -> void:
	parent.add_child(_label(title, Vector2(0, y), Vector2(PANEL_RECT.size.x, 28), TEXT_COLOR, 20, HORIZONTAL_ALIGNMENT_CENTER))
	var rule := ColorRect.new()
	rule.color = Color(FRAME_COLOR, 0.7)
	rule.position = Vector2(40, y + 29)
	rule.size = Vector2(PANEL_RECT.size.x - 80, 1)
	rule.mouse_filter = Control.MOUSE_FILTER_IGNORE
	parent.add_child(rule)


func _row(caption: String, y: float, parent: Control, widget: Control, key: String) -> void:
	parent.add_child(_label(caption, Vector2(48, y), Vector2(250, 30), LABEL_COLOR, 17))
	widget.position = Vector2(318, y)
	if widget.size.x < 1:
		widget.size = Vector2(264, 30)
	parent.add_child(widget)
	_controls[key] = widget


func _option_button(items: Array, key: String) -> OptionButton:
	var o := OptionButton.new()
	o.focus_mode = Control.FOCUS_NONE
	o.size = Vector2(264, 30)
	o.add_theme_font_size_override("font_size", 16)
	for it in items:
		o.add_item(it)
	o.get_popup().add_theme_font_size_override("font_size", 16)
	o.get_popup().about_to_popup.connect(func() -> void: o.get_popup().content_scale_factor = minf(_stage.scale.x, _stage.scale.y))
	o.item_selected.connect(_on_option.bind(key))
	return o


func _build_screen() -> void:
	_screen = Control.new()
	_screen.name = "OpenBFMEOptions"
	_screen.size = STAGE
	_screen.visible = false
	_screen.mouse_filter = Control.MOUSE_FILTER_STOP # nothing under the screen takes a click
	_stage.add_child(_screen)
	var dim := ColorRect.new()
	dim.color = Color(0, 0, 0, 0.55)
	dim.size = STAGE
	dim.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_screen.add_child(dim)
	var panel := _panel(PANEL_RECT, _screen)
	var title := _label("OPENBFME", Vector2(0, 10), Vector2(PANEL_RECT.size.x, 44), TITLE_COLOR, 34, HORIZONTAL_ALIGNMENT_CENTER)
	var title_font := _font("SachaWynter")
	if title_font != null:
		title.add_theme_font_override("font", title_font)
	panel.add_child(title)
	panel.add_child(_label("Display options of this rebuild (not in the original game)", Vector2(0, 50), Vector2(PANEL_RECT.size.x, 22), Color(TEXT_COLOR, 0.75), 13,
		HORIZONTAL_ALIGNMENT_CENTER))

	_section("Display", 78, panel)
	_row("Window mode", 116, panel, _option_button(DisplaySettings.WINDOW_MODES.map(func(m): return WINDOW_MODE_LABELS[m]), "window_mode"), "window_mode")
	_row("Resolution", 152, panel, _option_button([], "resolution"), "resolution")
	_row("Vertical sync", 188, panel, _option_button(["On", "Off", "Adaptive"], "vsync"), "vsync")
	var limits: Array = ["Off"]
	for f in DisplaySettings.FRAME_LIMITS.slice(1):
		limits.append("%d fps" % f)
	limits.append("Custom")
	_row("Frame rate limit", 224, panel, _option_button(limits, "max_fps"), "max_fps")
	var custom := SpinBox.new()
	custom.min_value = 10
	custom.max_value = DisplaySettings.MAX_FPS_LIMIT
	custom.step = 1
	custom.suffix = "fps"
	custom.value = 75 # what Custom starts at
	custom.size = Vector2(110, 30)
	custom.value_changed.connect(func(v: float) -> void: _changed("max_fps", int(v)))
	custom.position = Vector2(590, 224)
	custom.visible = false
	panel.add_child(custom)
	_controls["max_fps_custom"] = custom
	var counter := CheckBox.new()
	counter.text = "Show the frame rate (F11)"
	counter.focus_mode = Control.FOCUS_NONE
	counter.add_theme_font_size_override("font_size", 17)
	counter.position = Vector2(312, 262)
	counter.size = Vector2(300, 32)
	counter.toggled.connect(func(on: bool) -> void: _changed("show_fps", on))
	panel.add_child(counter)
	_controls["show_fps"] = counter

	_section("Rendering", 300, panel)
	var scale := HSlider.new()
	scale.min_value = DisplaySettings.SCALE_MIN * 100.0
	scale.max_value = DisplaySettings.SCALE_MAX * 100.0
	scale.step = 5
	scale.focus_mode = Control.FOCUS_NONE
	scale.size = Vector2(200, 30)
	scale.value_changed.connect(func(v: float) -> void: _changed("render_scale", v / 100.0))
	_row("Render scale", 338, panel, scale, "render_scale")
	var scale_text := _label("100%", Vector2(530, 338), Vector2(60, 30), TEXT_COLOR, 16, HORIZONTAL_ALIGNMENT_RIGHT)
	panel.add_child(scale_text)
	_controls["render_scale_text"] = scale_text
	_row("Upscaling", 374, panel, _option_button(DisplaySettings.SCALERS.map(func(s): return SCALER_LABELS[s]), "scaler"), "scaler")
	var note := _label("", Vector2(30, 410), Vector2(PANEL_RECT.size.x - 60, 60), Color(TEXT_COLOR, 0.8), 13, HORIZONTAL_ALIGNMENT_CENTER)
	note.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	panel.add_child(note)
	_controls["note"] = note

	var cancel := _make_button("CANCEL", Rect2(28, 492, 160, 40))
	cancel.name = "Cancel"
	cancel.pressed.connect(func() -> void: close(false))
	panel.add_child(cancel)
	_controls["cancel"] = cancel
	var defaults := _make_button("RESET TO DEFAULTS", Rect2(215, 492, 200, 40))
	defaults.name = "Defaults"
	defaults.pressed.connect(reset_defaults)
	panel.add_child(defaults)
	_controls["defaults"] = defaults
	var accept := _make_button("ACCEPT", Rect2(442, 492, 160, 40))
	accept.name = "Accept"
	accept.pressed.connect(func() -> void: close(true))
	panel.add_child(accept)
	_controls["accept"] = accept


func _resolution_list() -> Array:
	var screen := DisplayServer.window_get_current_screen()
	return DisplaySettings.resolutions(DisplayServer.screen_get_size(screen), get_tree().root.size)


func _on_option(index: int, key: String) -> void:
	if _syncing:
		return
	match key:
		"window_mode":
			set_value("window_mode", DisplaySettings.WINDOW_MODES[index])
		"resolution":
			var list := _resolution_list()
			if index >= 0 and index < list.size():
				set_value("resolution", DisplaySettings.resolution_text(list[index]))
		"vsync":
			set_value("vsync", ["on", "off", "adaptive"][index])
		"max_fps":
			var limits: Array = DisplaySettings.FRAME_LIMITS
			if index < limits.size():
				set_value("max_fps", limits[index])
			else:
				set_value("max_fps", int((_controls["max_fps_custom"] as SpinBox).value))
		"scaler":
			set_value("scaler", DisplaySettings.SCALERS[index])


## The controls show _pending (no signals while they are set)
func _sync_controls() -> void:
	if _pending == null:
		return
	_syncing = true
	var p = _pending
	(_controls["window_mode"] as OptionButton).select(DisplaySettings.WINDOW_MODES.find(p.window_mode))
	var res: OptionButton = _controls["resolution"]
	res.clear()
	var list := _resolution_list()
	var current: Vector2i = p.resolution if p.resolution != Vector2i.ZERO else get_tree().root.size
	if not list.has(current):
		list.append(current)
	for r in list:
		res.add_item("%d x %d" % [r.x, r.y])
	res.select(list.find(current))
	res.disabled = p.window_mode != "windowed"
	var vs: OptionButton = _controls["vsync"]
	if p.vsync == "mailbox" and vs.item_count == 3:
		vs.add_item("Mailbox")
	vs.select(["on", "off", "adaptive", "mailbox"].find(p.vsync))
	var limit: OptionButton = _controls["max_fps"]
	var custom: SpinBox = _controls["max_fps_custom"]
	var li: int = DisplaySettings.FRAME_LIMITS.find(p.max_fps)
	limit.select(li if li >= 0 else limit.item_count - 1)
	custom.visible = li < 0
	if li < 0:
		custom.value = p.max_fps
	(_controls["show_fps"] as CheckBox).button_pressed = p.show_fps
	(_controls["render_scale"] as HSlider).value = p.render_scale * 100.0
	(_controls["render_scale_text"] as Label).text = "%d%%" % roundi(p.render_scale * 100.0)
	var method := DisplaySettings.renderer()
	var available: Array = DisplaySettings.scalers_for(method)
	var sc: OptionButton = _controls["scaler"]
	for i in DisplaySettings.SCALERS.size():
		sc.set_item_disabled(i, not available.has(DisplaySettings.SCALERS[i]))
	sc.select(DisplaySettings.SCALERS.find(p.scaler))
	var lines := PackedStringArray()
	lines.append("Renderer: %s (%s). FSR 3 and DLSS: later." % [RENDERER_LABELS.get(method, method), ", ".join(available.map(func(x): return SCALER_LABELS[x]))])
	if p.window_mode != "windowed":
		lines.append("Full screen uses the monitor's own resolution; lower the render scale for fewer pixels.")
	elif p.render_scale >= 0.999 and p.scaler != "fsr2":
		lines.append("At 100% the scaler does nothing (FSR 2 still smooths edges).")
	(_controls["note"] as Label).text = "\n".join(lines)
	_syncing = false
