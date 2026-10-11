## Lane OPTS-1: the frame-rate counter and the OpenBFME display hotkeys (NOT RETAIL: owner, 2026-10-10).
##   F11        shows / hides the frame-rate counter (saved as show_fps in openbfme-display.cfg)
##   Alt+Enter  toggles between a window and full screen (the last full-screen kind chosen, borderless by default; saved as window_mode)
## Both keys are free in RotWK 2.01's CommandMap.ini (no CommandMap record uses KEY_F11 with any modifier, or KEY_ENTER with ALT; checked against the
## retail file by tools/opts/test_opts1_hotkeys.py), so no retail command loses its key. The node sits under the scene root after the game, so it sees
## a key before the shell, the HUD and the game do, and a key it acts on goes no further.
## The counter: frames per second and the mean frame time of the last half second, drawn over everything in the top-left corner.
extends CanvasLayer

const DisplaySettings := preload("res://scripts/display_settings.gd")

const HOTKEYS := [
	{"name": "OPENBFME_TOGGLE_FPS", "key": KEY_F11, "alt": false, "commandmap": "KEY_F11", "modifiers": "any"},
	{"name": "OPENBFME_TOGGLE_FULLSCREEN", "key": KEY_ENTER, "alt": true, "commandmap": "KEY_ENTER", "modifiers": "ALT"},
]

var settings: RefCounted     # scripts/display_settings.gd, the settings in effect (shared with game.gd and the options screen)
var settings_path := ""
var session_fps := false     # --fps: shown for this run whatever the file says
var preview = null           # the options screen's box while the screen is up (true / false), else null
var screen: CanvasLayer      # scripts/openbfme_options_screen.gd: while it is up the keys change its settings, not the file
var _label: Label
var _frames := 0
var _since_us := 0
var _text := ""


func _ready() -> void:
	layer = 120
	_label = Label.new()
	_label.name = "FpsCounter"
	_label.set_meta("game_text", true) # a player's option, not developer text (game.gd _dev_text_on_screen)
	_label.position = Vector2(8, 4)
	_label.add_theme_color_override("font_color", Color(1.0, 0.92, 0.6))
	_label.add_theme_color_override("font_outline_color", Color.BLACK)
	_label.add_theme_constant_override("outline_size", 4)
	_label.add_theme_font_size_override("font_size", 16)
	_label.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(_label)
	_since_us = Time.get_ticks_usec()
	refresh()


func shown() -> bool:
	if preview != null:
		return preview
	return session_fps or (settings != null and settings.show_fps)


func refresh() -> void:
	_label.visible = shown()


func counter_text() -> String:
	return _label.text


func _process(_delta: float) -> void:
	if not _label.visible:
		_frames = 0
		_since_us = Time.get_ticks_usec()
		return
	_frames += 1
	var now := Time.get_ticks_usec()
	var span := now - _since_us
	if span >= 500000 or _label.text.is_empty():
		var ms := (span / 1000.0) / maxi(_frames, 1)
		_label.text = "%d fps  %.1f ms" % [roundi(Engine.get_frames_per_second()), ms]
		_frames = 0
		_since_us = now


func _input(event: InputEvent) -> void:
	var key := event as InputEventKey
	if key == null or not key.pressed or key.echo:
		return
	if key.keycode == KEY_F11 and not key.alt_pressed and not key.ctrl_pressed and not key.shift_pressed:
		get_viewport().set_input_as_handled()
		toggle_fps()
	elif (key.keycode == KEY_ENTER or key.keycode == KEY_KP_ENTER) and key.alt_pressed and not key.ctrl_pressed and not key.shift_pressed:
		get_viewport().set_input_as_handled()
		toggle_fullscreen()


func toggle_fps() -> void:
	if settings == null:
		return
	var on := not shown()
	if screen != null and screen.is_open():
		screen.hotkey("show_fps", on) # the screen's settings, written by its ACCEPT, put back by its CANCEL
		print("GAME DISPLAY frame-rate counter %s (F11, options screen)" % ("on" if on else "off"))
		return
	session_fps = false
	settings.show_fps = on
	_save(["show_fps"])
	refresh()
	print("GAME DISPLAY frame-rate counter %s (F11)" % ("on" if shown() else "off"))


## Alt+Enter: windowed <-> full screen, from the mode the window is in. Going full screen uses the kind this run last left (borderless when it never
## had one). Only the window mode changes (and only window_mode is written): a --max-fps cap or the screen's other settings stay.
var _last_fullscreen := "borderless"
func toggle_fullscreen() -> void:
	if settings == null:
		return
	var root := get_tree().root
	var open: bool = screen != null and screen.is_open()
	var current: String = DisplaySettings.window_mode_now(root, screen.pending_value("window_mode") if open else settings.window_mode)
	var next := _last_fullscreen
	if current != "windowed":
		_last_fullscreen = current
		next = "windowed"
	if open:
		screen.hotkey("window_mode", next)
		print("GAME DISPLAY Alt+Enter: %s (options screen)" % next)
		return
	var before = settings.copy()
	before.window_mode = current
	settings.window_mode = next
	var r: Dictionary = settings.apply(root, before)
	_save(["window_mode"])
	print("GAME DISPLAY Alt+Enter: %s %s" % [", ".join(r.applied), "" if r.problems.is_empty() else str(r.problems)])


func _save(keys: Array) -> void:
	if settings_path.is_empty():
		return
	if not settings.save_keys(settings_path, keys):
		printerr("GAME DISPLAY ", settings.errors[-1])
