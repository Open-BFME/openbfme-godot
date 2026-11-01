## tools/release/test_launcher_display.py stages the real game display reader beside this test.
extends SceneTree

const GameDisplay := preload("res://scripts/core/game_display.gd")
const MainScene := preload("res://scenes/main.tscn")
var failures := 0
var checks := 0


func check(ok: bool, message: String) -> void:
	checks += 1
	if not ok:
		failures += 1
		printerr("DISPLAY TEST FAIL: ", message)


func _initialize() -> void:
	_run.call_deferred()


func _run() -> void:
	var DisplaySettings = load("res://test_game_display_settings.gd")
	var file := GameDisplay.path()
	check(file.get_base_dir() == OS.get_environment("OPENBFME_GAME_USER_DIR"), "isolated game settings")
	var first := GameDisplay.read(Vector2i(2560, 1440))
	check(first.error == "" and first.resolution == Vector2i.ZERO and first.window_mode == "windowed", "first-run automatic display")
	check(not FileAccess.file_exists(file), "reading never creates preferences")
	check(GameDisplay.choices(Vector2i(1920, 1080), Vector2i(1536, 864)) == DisplaySettings.resolutions(Vector2i(1920, 1080), Vector2i(1536, 864)), "launcher and game resolution choices agree")
	var main := MainScene.instantiate()
	root.add_child(main)
	await process_frame
	main._show_screen("settings")
	main._settings.show_tab("display")
	var ui = main._settings
	check(ui._window_mode.selected == 0 and ui._resolution.selected == 0 and not ui._resolution.disabled, "first-run controls")
	ui.set_display({"window_mode": "borderless", "resolution": Vector2i(1100, 700), "screen_size": Vector2i(3440, 1440), "error": ""})
	var headers: Array = []
	var sizes: Array = []
	for n in ui._resolution.item_count:
		if ui._resolution.is_item_separator(n):
			headers.append(ui._resolution.get_item_text(n))
		elif n != 0:
			sizes.append(GameDisplay.parse_resolution(ui._resolution.get_item_metadata(n)))
	check(headers == ["5:4", "4:3", "11:7", "16:10", "16:9", "21:9"], "aspect-ratio headers include the custom saved size")
	check(sizes.size() == GameDisplay.choices(Vector2i(3440, 1440), Vector2i(1100, 700)).size(), "grouping retains every resolution exactly once")
	check(GameDisplay.aspect_ratio(Vector2i(1366, 768)) == "16:9" and GameDisplay.aspect_ratio(Vector2i(2560, 1080)) == "21:9"
		and GameDisplay.aspect_ratio(Vector2i(5120, 2160)) == "21:9", "rounded display families and higher-resolution equivalents")
	check(ui._resolution.get_item_metadata(ui._resolution.selected) == "1100x700", "saved custom resolution stays selected after grouping")
	ui._resolution.item_selected.emit(1)
	check(not FileAccess.file_exists(file), "group headings cannot save a resolution")
	var game = DisplaySettings.new()
	game.from_dict({"window_mode": "borderless", "resolution": "1536x864", "max_fps": 144, "render_scale": 0.75, "vsync": "off"})
	DirAccess.make_dir_recursive_absolute(file.get_base_dir())
	check(game.save_file(file), "game accepts and saves display choices")
	root.focus_entered.emit()
	check(ui._window_mode.selected == 1 and ui._resolution.get_item_metadata(ui._resolution.selected) == "1536x864", "launcher refreshes game edits on focus")
	check(not ui._resolution.disabled, "borderless allows resolution selection")
	ui._window_mode.item_selected.emit(2)
	var launched = DisplaySettings.new()
	check(launched.load_file(file) and launched.window_mode == "fullscreen" and launched.resolution == Vector2i(1536, 864), "game reads launcher mode change")
	check(launched.max_fps == 144 and launched.vsync == "off" and launched.render_scale == 0.75, "mode edit preserves other game preferences")
	var i := -1
	for n in ui._resolution.item_count:
		if ui._resolution.get_item_metadata(n) == "1280x720": i = n
	check(i != -1, "common resolution offered")
	ui._resolution.item_selected.emit(i)
	var second_game = DisplaySettings.new()
	check(second_game.load_file(file) and second_game.resolution == Vector2i(1280, 720) and second_game.window_mode == "fullscreen", "second game loads the same launcher settings")
	second_game.resolution = Vector2i(1024, 768)
	second_game.save_file(file)
	ui.show_tab("general")
	ui.show_tab("display")
	check(ui._resolution.get_item_metadata(ui._resolution.selected) == "1024x768", "reopening Display reloads game edits")
	ui._resolution.item_selected.emit(0)
	var automatic = DisplaySettings.new()
	check(automatic.load_file(file) and automatic.resolution == Vector2i.ZERO, "automatic resets saved resolution")
	var cfg := ConfigFile.new()
	cfg.load(file)
	cfg.set_value("other", "keep", 42)
	cfg.save(file)
	check(GameDisplay.save("resolution", "1600x900") == "", "resolution save")
	cfg.load(file)
	check(cfg.get_value("other", "keep") == 42 and cfg.get_value("display", "max_fps") == 144, "unowned configuration is preserved")
	var original := FileAccess.get_file_as_string(file)
	check(GameDisplay.save("resolution", "broken") != "" and FileAccess.get_file_as_string(file) == original, "invalid selection cannot change file")
	var broken := FileAccess.open(file, FileAccess.WRITE)
	broken.store_string("[display\nresolution=oops")
	broken.close()
	root.focus_entered.emit()
	check(ui._resolution.disabled and ui._window_mode.disabled and ui._display_error.visible, "malformed settings show an error and disable editing")
	check(ui._display_error.text.contains(file.get_file()) and not ui._display_error.text.contains(file.get_base_dir()), "read error shows only the settings filename")
	check(GameDisplay.save("resolution", "1280x720") != "" and FileAccess.get_file_as_string(file) == "[display\nresolution=oops", "malformed file is not overwritten")
	game.save_file(file)
	ui.show_tab("display")
	check(not ui._resolution.disabled and not ui._display_error.visible, "reopening Display recovers after file repair")
	var blocked := FileAccess.open(file + ".blocked", FileAccess.WRITE)
	blocked.store_string("block directory creation")
	blocked.close()
	var write_error := GameDisplay.save("window_mode", "borderless", file + ".blocked/settings.cfg")
	check(write_error.contains("settings.cfg") and not write_error.contains(file.get_base_dir()), "write error shows only the settings filename")
	main.queue_free()
	await process_frame
	# The real viewport size changes independently of the OS window in fullscreen.
	root.size = Vector2i(1600, 900)
	DisplaySettings.new().apply(root)
	check(root.size == Vector2i(1600, 900), "Automatic windowed startup preserves the existing window size")
	for mode in ["borderless", "fullscreen"]:
		var display = DisplaySettings.new()
		display.from_dict({"window_mode": mode, "resolution": "1024x768"})
		display.apply(root)
		await process_frame
		check(root.content_scale_mode == Window.CONTENT_SCALE_MODE_VIEWPORT and root.get_visible_rect().size == Vector2(1024, 768), "%s renders at selected resolution" % mode)
		check(root.content_scale_aspect == Window.CONTENT_SCALE_ASPECT_KEEP, "aspect ratio is preserved")
		var before: Dictionary = DisplaySettings.snapshot(root)
		var changed = display.copy()
		changed.resolution = Vector2i(1280, 720)
		changed.apply(root, display)
		await process_frame
		check(root.get_visible_rect().size == Vector2(1280, 720), "fullscreen resolution changes live")
		DisplaySettings.restore(root, before)
		await process_frame
		check(root.get_visible_rect().size == Vector2(1024, 768), "Cancel restores the fullscreen viewport")
		var windowed = display.copy()
		windowed.window_mode = "windowed"
		windowed.apply(root, display)
		await process_frame
		check(root.content_scale_mode == Window.CONTENT_SCALE_MODE_DISABLED and root.size == Vector2i(1024, 768), "returning to windowed restores normal window sizing")
	print("DISPLAY TEST %s: %d checks" % ["PASS" if failures == 0 else "FAIL", checks])
	quit(0 if failures == 0 else 1)
