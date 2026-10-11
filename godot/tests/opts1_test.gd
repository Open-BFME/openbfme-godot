## Headless test of lane OPTS-1, the OpenBFME display options (not retail):
##  * scripts/display_settings.gd: the file's values read, written and rejected (a bad value is an error and keeps the default), the Resolution list, the
##    scalers each renderer has, apply() on the window (frame cap, render scale, scaler; a maximised window is left alone when only the cap changes);
##  * the game with --display-check (scripts/opts1_check.gd): the OPENBFME button over Options.apt, every control of the screen, ACCEPT / CANCEL /
##    RESET TO DEFAULTS / Esc, F11 and Alt+Enter, and the owner's "maximising greys out the main menu": the main menu's and Options.apt's buttons
##    unchanged across a maximise, resizes and every window mode;
##  * the game with --auto --display-game: the screen over a live game (the HUD takes no input while it is up, the menus' stage follows a resolution
##    change, Esc closes the screen, then Options, then the quit menu).
##
##   godot --headless --path godot --script res://tests/opts1_test.gd
##
## The unit part runs without the games; the two game runs need ROTWK_INSTALL and BFME2_INSTALL (SKIP lines without). Exit codes: 0 pass, 1 fail.
extends SceneTree

const DisplaySettings := preload("res://scripts/display_settings.gd")

var _failures := 0


func _initialize() -> void:
	var code: int = await _run()
	print("OPTS1 %s" % ["PASS" if code == 0 else "FAIL"])
	quit(code)


func _check(ok: bool, what: String) -> void:
	print("  ", "ok   " if ok else "FAIL ", what)
	if not ok:
		_failures += 1


func _run() -> int:
	_settings_checks()
	await _apply_checks()
	if OS.get_environment("ROTWK_INSTALL").is_empty() or OS.get_environment("BFME2_INSTALL").is_empty():
		print("SKIP: the game runs (set ROTWK_INSTALL and BFME2_INSTALL)")
		return 0 if _failures == 0 else 1
	if not ClassDB.class_exists("AptMenuPlayer"):
		_check(false, "the openbfme extension is loaded")
		return 1
	_game_run(["--display-check", "--max-fps=77"], 52, "the menu check (a command-line cap the file does not hold)")
	_game_run(["--auto", "--advance=2", "--display-game"], 12, "the in-game check")
	return 0 if _failures == 0 else 1


func _settings_checks() -> void:
	var dir := OS.get_user_data_dir().path_join("opts1-test")
	DirAccess.make_dir_recursive_absolute(dir)
	var path := dir.path_join(DisplaySettings.FILE_NAME)
	DirAccess.remove_absolute(path)
	var d = DisplaySettings.new()
	_check(d.load_file(path) and d.errors.is_empty(), "a missing file is the first run: the defaults, no error")
	_check(d.to_dict() == {"window_mode": "windowed", "resolution": "", "vsync": "on", "max_fps": 0, "show_fps": false, "render_scale": 1.0, "scaler": "bilinear"},
		"the defaults change nothing of the game's start (windowed, vsync on, no cap, 100%% bilinear): %s" % JSON.stringify(d.to_dict()))
	d.from_dict({"window_mode": "borderless", "resolution": "1600x900", "vsync": "adaptive", "max_fps": 144, "show_fps": true, "render_scale": 0.65, "scaler": "fsr2"})
	_check(d.errors.is_empty() and d.save_file(path), "a full set of values is accepted and written")
	var e = DisplaySettings.new()
	_check(e.load_file(path) and e.equals(d), "the file reads back the same: %s" % JSON.stringify(e.to_dict()))
	var text := FileAccess.get_file_as_string(path)
	_check(text.contains("[display]") and text.contains("window_mode=\"borderless\"") and text.contains("resolution=\"1600x900\""),
		"the file is a ConfigFile with a [display] section (the launcher reads it)")
	var f := FileAccess.open(path, FileAccess.WRITE)
	f.store_string("[display]\nwindow_mode=\"maximised\"\nresolution=\"big\"\nvsync=\"fast\"\nmax_fps=-5\nshow_fps=1\nrender_scale=0.2\nscaler=\"dlss\"\ncolour=3\n[other]\nx=1\n")
	f.close()
	var g = DisplaySettings.new()
	_check(not g.load_file(path), "a file with bad values reports them")
	_check(g.errors.size() == 9, "every bad value, the unknown key and the unknown section is an error (%d): %s" % [g.errors.size(), str(g.errors)])
	_check(g.to_dict() == DisplaySettings.new().to_dict(), "the bad values leave the defaults")
	_check(g.errors[0].begins_with(path), "an error names the file")
	f = FileAccess.open(path, FileAccess.WRITE)
	f.store_string("[display\nwindow_mode = \"windowed")
	f.close()
	var h = DisplaySettings.new()
	_check(not h.load_file(path) and h.errors.size() == 1, "a file that cannot be parsed is an error: %s" % str(h.errors))
	DirAccess.remove_absolute(path)
	DirAccess.remove_absolute(dir)
	_check(DisplaySettings.parse_resolution("1920x1080") == Vector2i(1920, 1080) and DisplaySettings.parse_resolution(" 800X600 ") == Vector2i(800, 600)
		and DisplaySettings.parse_resolution("100x100").x < 0 and DisplaySettings.parse_resolution("1920").x < 0, "resolution text")
	var list: Array = DisplaySettings.resolutions(Vector2i(1920, 1080), Vector2i(1500, 1000))
	_check(list.has(Vector2i(1920, 1080)) and list.has(Vector2i(1500, 1000)) and list.has(Vector2i(1280, 720)) and not list.has(Vector2i(2560, 1440))
		and list.front() == Vector2i(800, 600) and list.back() == Vector2i(1920, 1080), "the Resolution list of a 1920 x 1080 monitor: %s" % str(list))
	_check(DisplaySettings.resolutions(Vector2i.ZERO, Vector2i(64, 64)).size() == DisplaySettings.COMMON_RESOLUTIONS.size() + 1, "no monitor known: every common size")
	_check(DisplaySettings.scalers_for("forward_plus") == ["bilinear", "fsr", "fsr2"] and DisplaySettings.scalers_for("mobile") == ["bilinear", "fsr"]
		and DisplaySettings.scalers_for("gl_compatibility") == ["bilinear"], "the scalers of Forward+, Mobile and Compatibility")
	_check(DisplaySettings.window_mode_of("borderless") == Window.MODE_FULLSCREEN and DisplaySettings.window_mode_of("fullscreen") == Window.MODE_EXCLUSIVE_FULLSCREEN
		and DisplaySettings.window_mode_of("windowed") == Window.MODE_WINDOWED, "borderless is Godot's fullscreen, fullscreen its exclusive fullscreen")


func _apply_checks() -> void:
	var window: Window = root
	var a = DisplaySettings.new()
	a.max_fps = 120
	a.render_scale = 0.5
	a.scaler = "fsr"
	var r: Dictionary = a.apply(window, null)
	_check(Engine.max_fps == 120 and is_equal_approx(window.scaling_3d_scale, 0.5) and window.scaling_3d_mode == Viewport.SCALING_3D_MODE_FSR,
		"apply: the frame cap, the render scale and the scaler (%s)" % ", ".join(r.applied))
	var method := RenderingServer.get_current_rendering_method()
	var b = a.copy()
	b.scaler = "fsr2"
	r = b.apply(window, a)
	if method == "forward_plus":
		_check(window.scaling_3d_mode == Viewport.SCALING_3D_MODE_FSR2 and r.problems.is_empty(), "FSR 2 on Forward+")
	else:
		_check(window.scaling_3d_mode == Viewport.SCALING_3D_MODE_BILINEAR and r.problems.size() == 1, "FSR 2 on %s is reported and bilinear used: %s" % [method, str(r.problems)])
	# a window the player maximised stays maximised when the settings change something else (the mode and the size are only set when they change)
	window.mode = Window.MODE_MAXIMIZED
	var size_before := window.size
	var c = b.copy()
	c.max_fps = 60
	c.resolution = Vector2i(1024, 768)
	b.resolution = Vector2i(1024, 768)
	c.apply(window, b)
	_check(window.size == size_before and Engine.max_fps == 60, "only the changed settings touch the window (size %s)" % str(window.size))
	window.mode = Window.MODE_WINDOWED
	var dflt = DisplaySettings.new()
	dflt.apply(window, c)
	_check(Engine.max_fps == 0 and is_equal_approx(window.scaling_3d_scale, 1.0) and window.scaling_3d_mode == Viewport.SCALING_3D_MODE_BILINEAR, "the defaults put Godot's back")
	await process_frame


func _game_run(args: Array, min_ok: int, what: String) -> void:
	var output: Array = []
	var project := ProjectSettings.globalize_path("res://")
	var t0 := Time.get_ticks_msec()
	var code := OS.execute(OS.get_executable_path(), PackedStringArray(["--headless", "--path", project, "--"] + args), output, true)
	var text: String = "".join(output)
	var result := ""
	var oks := 0
	for line in text.split("\n"):
		if line.begins_with("DISPLAY "):
			print("    ", line)
			if line.ends_with(": ok"):
				oks += 1
			if line.begins_with("DISPLAY RESULT"):
				result = line
	_check(code == 0 and result.ends_with(" 0 failed") and oks >= min_ok,
		"%s (%s): exit %d, %s, %d ok (at least %d), %.0f s" % [what, " ".join(args), code, result if not result.is_empty() else "NO RESULT", oks, min_ok, (Time.get_ticks_msec() - t0) / 1000.0])
