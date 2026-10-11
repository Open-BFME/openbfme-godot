## Lane OPTS-1: game.gd --display-check, the OpenBFME display options as a player uses them (mouse events on the OPENBFME button and the screen's
## buttons, key events for F11 and Alt+Enter), with the owner's "maximising greys out the main menu" as the main check: the main menu's and
## Options.apt's buttons (path, hittable, visible, the clip's frame) are the same before and after every window change, whether the window is
## maximised / resized directly or the mode is changed through the screen. Prints "DISPLAY <name>: ok" / "DISPLAY <name>: FAIL <why>" lines and
## "DISPLAY RESULT: <n> ok, <m> failed"; run() returns the failures. tests/opts1_test.gd runs it headless (window modes are no-ops there, the
## sizes are real); a windowed run (DISPLAY set) also writes the proof screenshots with --display-shots=DIR.
extends RefCounted

const DisplaySettings := preload("res://scripts/display_settings.gd")

var game: Node
var shell: Node
var screen: CanvasLayer
var overlay: CanvasLayer
var shots := ""
var ok_count := 0
var fail_count := 0


func run(p_game: Node, p_shots: String) -> int:
	game = p_game
	shell = game._shell
	screen = game._display_screen
	overlay = game._display_overlay
	shots = p_shots
	var root: Window = game.get_tree().root
	if DisplayServer.get_name() == "headless" and root.size.x < 640:
		root.size = Vector2i(1280, 800) # a headless window is 64 x 64: the stage needs room for the clicks
	await game._step(10)
	await game._wait_main_ready()
	await game._step(60)
	var main_level: int = shell.shell_top_level()
	var main0 := widget_state(main_level)
	check("main menu has buttons", main0.size() >= 5, "only %d buttons" % main0.size())

	# 1. the window changed directly (the system's maximise / restore / resize buttons), with the main menu up
	var sizes := [Vector2i(1600, 900), Vector2i(1024, 768), Vector2i(1280, 800)]
	root.mode = Window.MODE_MAXIMIZED
	await game._step(30)
	compare("main menu after maximise", main0, widget_state(main_level))
	root.mode = Window.MODE_WINDOWED
	for size in sizes:
		root.size = size
		await game._step(30)
		compare("main menu after a resize to %dx%d" % [size.x, size.y], main0, widget_state(main_level))

	# 2. Options.apt from the main menu (its OptionsNav -> Settings), the OPENBFME button over it
	for b in ["OptionsNav", "OptionsNav.Settings"]:
		if not await game._click_button(main_level, b):
			check("open Options", false, "cannot press " + b)
			return finish()
		await game._step(60)
	for i in 20:
		if top() == "Options.apt" and screen.entry_visible():
			break
		await game._step(15)
	check("Options.apt is up", top() == "Options.apt", str(shell.shell_stack()))
	await game._step(120)
	var options_level: int = shell.shell_top_level()
	var options0 := widget_state(options_level)
	check("the OPENBFME button is shown over Options.apt", screen.entry_visible(), "hidden")
	await shot("opts1-entry")
	await click_stage(screen.ENTRY_RECT.get_center())
	check("the OPENBFME button opens the screen", screen.is_open(), "not open")
	check("the shell takes no input while the screen is up", not shell.is_processing_input(), "the shell still processes input")
	if not screen.is_open():
		return finish()

	# review r1 (Sol): the screen opens on what the game runs with (tests/opts1_test.gd starts this run with --max-fps=77, a command-line cap the
	# file does not hold); F11 / Alt+Enter while it is up change the screen's settings only; CANCEL puts back the run's state, not the file's
	var runtime_cap := Engine.max_fps
	var file_before := FileAccess.get_file_as_string(game._display_path) if FileAccess.file_exists(game._display_path) else "(none)"
	check("the screen opens on the cap in effect (%d)" % runtime_cap, screen.pending_value("max_fps") == runtime_cap and (runtime_cap == 0 or (screen.control("max_fps_custom") as SpinBox).visible),
		"shows %s" % str(screen.pending_value("max_fps")))
	select("max_fps", DisplaySettings.FRAME_LIMITS.find(120))
	(screen.control("render_scale") as HSlider).value = 70
	var shown_before: bool = overlay.shown()
	await key(KEY_F11)
	await key(KEY_ENTER, true)
	check("F11 and Alt+Enter over the screen keep its other settings (cap 120, scale 70%)", screen.pending_value("max_fps") == 120 and is_equal_approx(screen.pending_value("render_scale"), 0.7)
		and Engine.max_fps == 120 and is_equal_approx(root.scaling_3d_scale, 0.7), "pending %s, runtime cap %d scale %.2f" % [JSON.stringify(screen.pending_value("max_fps")), Engine.max_fps, root.scaling_3d_scale])
	check("F11 and Alt+Enter over the screen change its settings", screen.pending_value("show_fps") == (not shown_before) and screen.pending_value("window_mode") == "borderless"
		and (screen.control("show_fps") as CheckBox).button_pressed == (not shown_before), "show_fps %s, window_mode %s" % [screen.pending_value("show_fps"), screen.pending_value("window_mode")])
	var file_mid := FileAccess.get_file_as_string(game._display_path) if FileAccess.file_exists(game._display_path) else "(none)"
	check("the keys over the screen write nothing", file_mid == file_before, file_mid)
	await click_panel(screen.control("cancel"))
	var file_after := FileAccess.get_file_as_string(game._display_path) if FileAccess.file_exists(game._display_path) else "(none)"
	check("CANCEL puts back the run's cap %d (not the file's), the scale, the mode and the counter" % runtime_cap, Engine.max_fps == runtime_cap and is_equal_approx(root.scaling_3d_scale, 1.0)
		and overlay.shown() == shown_before and game._display.window_mode == "windowed" and (DisplayServer.get_name() == "headless" or root.mode == Window.MODE_WINDOWED),
		"cap %d, scale %.2f, counter %s, mode %s / %d" % [Engine.max_fps, root.scaling_3d_scale, overlay.shown(), game._display.window_mode, root.mode])
	check("CANCEL leaves the file as it was", file_after == file_before, file_after)
	await click_stage(screen.ENTRY_RECT.get_center())
	check("the screen opens again", screen.is_open(), "not open")

	# 3. every setting through the screen's controls (applied at once)
	var modes: Array = DisplaySettings.WINDOW_MODES
	for i in modes.size():
		select("window_mode", i)
		await game._step(30)
		check("window mode %s applied" % modes[i], game._display_screen._pending.window_mode == modes[i] and (DisplayServer.get_name() == "headless" or root.mode == DisplaySettings.window_mode_of(modes[i])),
			"mode %d" % root.mode)
		compare("Options.apt after window mode %s" % modes[i], options0, widget_state(options_level))
	select("window_mode", 0)
	await game._step(20)
	var res: OptionButton = screen.control("resolution")
	check("resolution list", res.item_count >= 2 and not res.disabled, "%d items, disabled %s" % [res.item_count, res.disabled])
	for i in [0, res.item_count - 1]:
		var text := res.get_item_text(i).replace(" ", "")
		select("resolution", i)
		await game._step(30)
		check("resolution %s applied" % text, DisplaySettings.resolution_text(root.size) == text, "the window is %s" % DisplaySettings.resolution_text(root.size))
		compare("Options.apt after resolution %s" % text, options0, widget_state(options_level))
	select("resolution", 1)
	await game._step(30)
	select("max_fps", DisplaySettings.FRAME_LIMITS.find(60))
	check("frame limit 60", Engine.max_fps == 60, "max_fps %d" % Engine.max_fps)
	select("max_fps", (screen.control("max_fps") as OptionButton).item_count - 1)
	(screen.control("max_fps_custom") as SpinBox).value = 90
	check("custom frame limit 90", Engine.max_fps == 90 and (screen.control("max_fps_custom") as SpinBox).visible, "max_fps %d" % Engine.max_fps)
	select("vsync", 1)
	check("vsync off chosen", game._display_screen._pending.vsync == "off", game._display_screen._pending.vsync)
	(screen.control("render_scale") as HSlider).value = 75
	check("render scale 75%", is_equal_approx(root.scaling_3d_scale, 0.75), "scale %.2f" % root.scaling_3d_scale)
	var method := DisplaySettings.renderer()
	for i in DisplaySettings.SCALERS.size():
		var name: String = DisplaySettings.SCALERS[i]
		if not DisplaySettings.scalers_for(method).has(name):
			check("scaler %s is disabled on %s" % [name, method], (screen.control("scaler") as OptionButton).is_item_disabled(i), "enabled")
			continue
		select("scaler", i)
		await game._step(10)
		check("scaler %s applied" % name, root.scaling_3d_mode == DisplaySettings.scaling_mode_of(name), "mode %d" % root.scaling_3d_mode)
	var box: CheckBox = screen.control("show_fps")
	box.button_pressed = true
	await game._step(40)
	check("the counter shows while its box is ticked", overlay.shown() and not overlay.counter_text().is_empty(), overlay.counter_text())
	await shot("opts1-screen")
	await click_panel(screen.control("accept"))
	check("ACCEPT closes the screen", not screen.is_open(), "still open")
	check("the shell's input is back", shell.is_processing_input(), "still off")
	var saved = DisplaySettings.new()
	saved.load_file(game._display_path)
	var want := {"window_mode": "windowed", "vsync": "off", "max_fps": 90, "show_fps": true, "render_scale": 0.75,
		"scaler": DisplaySettings.SCALERS[DisplaySettings.scalers_for(method).size() - 1]}
	var got: Dictionary = saved.to_dict()
	var diff := PackedStringArray()
	for k in want:
		if str(got[k]) != str(want[k]):
			diff.append("%s %s != %s" % [k, got[k], want[k]])
	check("ACCEPT wrote the file", saved.errors.is_empty() and diff.is_empty(), "%s %s" % [saved.errors, diff])
	await game._step(30)
	compare("Options.apt after the screen closed", options0, widget_state(options_level))

	# 4. CANCEL puts back what was in effect
	await click_stage(screen.ENTRY_RECT.get_center())
	check("the screen opens again", screen.is_open(), "not open")
	(screen.control("render_scale") as HSlider).value = 50
	select("max_fps", 0)
	await click_panel(screen.control("cancel"))
	check("CANCEL restores the render scale and the frame limit", not screen.is_open() and is_equal_approx(root.scaling_3d_scale, 0.75) and Engine.max_fps == 90,
		"scale %.2f, max_fps %d" % [root.scaling_3d_scale, Engine.max_fps])
	# RESET TO DEFAULTS, then Esc: nothing kept
	await click_stage(screen.ENTRY_RECT.get_center())
	await click_panel(screen.control("defaults"))
	check("RESET TO DEFAULTS applies the defaults", is_equal_approx(root.scaling_3d_scale, 1.0) and Engine.max_fps == 0, "scale %.2f, max_fps %d" % [root.scaling_3d_scale, Engine.max_fps])
	await key(KEY_ESCAPE)
	check("Esc closes the screen and restores", not screen.is_open() and Engine.max_fps == 90, "open %s, max_fps %d" % [screen.is_open(), Engine.max_fps])
	check("Esc stayed with the screen (Options.apt is still up)", top() == "Options.apt", str(shell.shell_stack()))

	# 5. the keys (not retail; free in RotWK's CommandMap): F11 the counter, Alt+Enter full screen. Outside the screen a key writes its own key only:
	# a value another program (the launcher) wrote meanwhile stays, and the run's cap is not touched
	var cfg := ConfigFile.new()
	cfg.load(game._display_path)
	cfg.set_value(DisplaySettings.SECTION, "render_scale", 0.55)
	cfg.save(game._display_path)
	await key(KEY_F11)
	check("F11 hides the counter", not overlay.shown() and not game._display.show_fps, "shown")
	await key(KEY_F11)
	await game._step(40)
	check("F11 shows it again", overlay.shown() and game._display.show_fps, "hidden")
	await key(KEY_ENTER, true)
	check("Alt+Enter goes full screen", game._display.window_mode == "borderless", game._display.window_mode)
	await game._step(30)
	compare("Options.apt after Alt+Enter", options0, widget_state(options_level))
	await key(KEY_ENTER, true)
	check("Alt+Enter comes back to a window", game._display.window_mode == "windowed", game._display.window_mode)
	var after_keys = DisplaySettings.new()
	after_keys.load_file(game._display_path)
	check("the keys wrote show_fps and window_mode only (render_scale 0.55 of another writer kept, cap 90 kept)", after_keys.errors.is_empty()
		and is_equal_approx(after_keys.render_scale, 0.55) and after_keys.show_fps and after_keys.window_mode == "windowed" and after_keys.max_fps == 90 and Engine.max_fps == 90
		and is_equal_approx(root.scaling_3d_scale, 0.75), "%s, cap %d, scale %.2f" % [JSON.stringify(after_keys.to_dict()), Engine.max_fps, root.scaling_3d_scale])

	# 6. back to the main menu: the same buttons as before any of this
	if not await game._click_named_button("Cancel"):
		check("leave Options", false, "no Cancel button")
		return finish()
	await game._step(120)
	await game._wait_main_ready()
	await game._step(60)
	check("the OPENBFME button is gone with Options.apt", not screen.entry_visible(), "still shown")
	compare("main menu after Options and every window change", main0, widget_state(shell.shell_top_level()))
	await shot("opts1-fps")
	return finish()


## --display-game: the screen over a live game. Esc and the quit menu's Options (RW 0x921783 opens Options.apt over the game), the OPENBFME button, a
## resolution and a window mode through the screen while the game runs paused under the quit menu, Esc back out step by step.
func run_game(p_game: Node, p_shots: String) -> int:
	game = p_game
	shell = game._shell
	screen = game._display_screen
	overlay = game._display_overlay
	shots = p_shots
	var root: Window = game.get_tree().root
	if DisplayServer.get_name() == "headless" and root.size.x < 640:
		root.size = Vector2i(1280, 800)
		await game._step(10)
	var hud: Node = game._hud
	check("a live game with its HUD", hud != null and game._state == game.State.PLAYING, "state %d" % game._state)
	if hud == null:
		return finish()
	await key(KEY_F11)
	await game._step(40)
	check("F11 shows the counter in the game", overlay.shown(), "hidden")
	await shot("opts1-game-fps")
	await game._press_escape()
	await game._wait_seconds(3.0)
	if not await game._click_quit("Options"):
		check("the quit menu's Options", false, "no Options button")
		return finish()
	for i in 40:
		if top() == "Options.apt" and screen.entry_visible():
			break
		await game._step(10)
	check("Options.apt over the game shows the OPENBFME button", top() == "Options.apt" and screen.entry_visible(), str(shell.shell_stack()))
	await game._step(90)
	var options_level: int = shell.shell_top_level()
	var options0 := widget_state(options_level)
	await click_stage(screen.ENTRY_RECT.get_center())
	check("the screen opens over the game", screen.is_open(), "not open")
	check("the HUD and the shell take no input while it is up", not hud.is_processing_input() and not shell.is_processing_input(),
		"hud %s, shell %s" % [hud.is_processing_input(), shell.is_processing_input()])
	await shot("opts1-game-screen")
	var res: OptionButton = screen.control("resolution")
	var target := -1
	for i in res.item_count:
		if res.get_item_text(i).replace(" ", "") == "1024x768":
			target = i
	if target >= 0:
		select("resolution", target)
		await game._step(40)
		var a: Vector2 = shell.stage_to_window(Vector2.ZERO)
		var b: Vector2 = shell.stage_to_window(screen.STAGE)
		var w := Vector2(root.size)
		check("a resolution change keeps the menus' 1024 x 768 stage on the window", a.distance_to(Vector2.ZERO) < 1.5 and b.distance_to(w) < 1.5,
			"stage %s .. %s on a %s window" % [a, b, w])
		check("the 3D view follows the window", Vector2(game.get_viewport().get_visible_rect().size) == w, str(game.get_viewport().get_visible_rect().size))
	select("window_mode", 1)
	await game._step(30)
	select("window_mode", 0)
	await game._step(30)
	compare("Options.apt over the game after the window changes", options0, widget_state(options_level))
	await key(KEY_ESCAPE)
	check("Esc closes the screen only", not screen.is_open() and top() == "Options.apt", "open %s, stack %s" % [screen.is_open(), shell.shell_stack()])
	check("the HUD's input is back (as it was)", hud.is_processing_input() and shell.is_processing_input(), "hud %s, shell %s" % [hud.is_processing_input(), shell.is_processing_input()])
	await key(KEY_ESCAPE)
	await game._wait_seconds(2.0)
	check("Esc then closes Options (the quit menu stays)", top() != "Options.apt" and not screen.entry_visible(), str(shell.shell_stack()))
	await key(KEY_ESCAPE)
	await game._wait_seconds(2.0)
	check("Esc closes the quit menu: the game runs again", not game._world.is_paused(), "paused")
	await game._step(30)
	await shot("opts1-game-after")
	return finish()


func finish() -> int:
	print("DISPLAY RESULT: %d ok, %d failed" % [ok_count, fail_count])
	return fail_count


func top() -> String:
	var stack: PackedStringArray = shell.shell_stack()
	return "" if stack.is_empty() else stack[-1]


func check(name: String, ok: bool, why: String) -> void:
	if ok:
		ok_count += 1
		print("DISPLAY %s: ok" % name)
	else:
		fail_count += 1
		print("DISPLAY %s: FAIL %s" % [name, why])


## the state a player sees of a screen's buttons: every button's path, whether a click reaches it (hittable: enabled, not covered, not faded below
## the hit alpha), whether it is visible and the frame of the clip it sits in (a greyed-out nav button is another frame / state of its clip)
func widget_state(level: int) -> Dictionary:
	var out := {}
	for b in shell.list_buttons(level):
		var path := String(b.path)
		var rel := path.substr(path.find(".") + 1) if path.begins_with("_level") else path
		var info: Dictionary = shell.instance_info(level, rel)
		var parent: Dictionary = shell.instance_info(level, rel.substr(0, rel.rfind(".")) if rel.contains(".") else "")
		out[path] = "hittable=%s visible=%s frame=%s parent_frame=%s pos=%.0f,%.0f" % [b.hittable, info.get("visible", "?"), info.get("frame", "-"),
			parent.get("frame", "-"), b.x, b.y]
	return out


func compare(name: String, before: Dictionary, after: Dictionary) -> void:
	var diff := PackedStringArray()
	for k in before:
		if not after.has(k):
			diff.append("%s gone" % k)
		elif after[k] != before[k]:
			diff.append("%s: %s -> %s" % [k, before[k], after[k]])
	for k in after:
		if not before.has(k):
			diff.append("%s new: %s" % [k, after[k]])
	check(name + " (%d buttons the same)" % before.size(), diff.is_empty(), "; ".join(diff.slice(0, 6)))


func select(key: String, index: int) -> void:
	var o: OptionButton = screen.control(key)
	o.select(index)
	o.item_selected.emit(index)


func click_stage(p: Vector2) -> void:
	game._mouse_event(p, false, false)
	await game._step(2)
	game._mouse_event(p, true, true)
	await game._step(2)
	game._mouse_event(p, false, true)
	await game._step(6)


func click_panel(c: Control) -> void:
	await click_stage(screen.PANEL_RECT.position + c.position + c.size / 2)


func key(code: Key, alt := false) -> void:
	for pressed in [true, false]:
		var e := InputEventKey.new()
		e.keycode = code
		e.physical_keycode = code
		e.alt_pressed = alt
		e.pressed = pressed
		Input.parse_input_event(e)
		await game._step(3)
	await game._step(6)


func shot(name: String) -> void:
	if shots.is_empty() or DisplayServer.get_name() == "headless":
		return
	await RenderingServer.frame_post_draw
	var image: Image = game._capture_image(name)
	if image == null:
		return
	DirAccess.make_dir_recursive_absolute(shots)
	var path := shots.path_join(name + ".png")
	print("DISPLAY screenshot %s -> %s" % [path, error_string(image.save_png(path))])
