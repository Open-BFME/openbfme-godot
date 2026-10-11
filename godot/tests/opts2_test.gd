## Headless test of lane OPTS-2, the Options menu:
##  * scripts/game_options.gd: every OpenBFME key carries the "OpenBFME" prefix, its default is one of its own values (retail behaviour), a value a key
##    does not take is reported and not used, the three mirrored retail keys keep their retail names and defaults;
##  * the game with --opts2-check (scripts/opts2_check.gd) on an Options.ini of the test's own (--options-file): the detail level in effect, Options.apt's
##    pages, every tab of the OpenBFME screen, the extras applied / written / cancelled / reset, no green text.
##
##   godot --headless --path godot --script res://tests/opts2_test.gd
##
## The unit part runs without the games; the game run needs ROTWK_INSTALL and BFME2_INSTALL (a SKIP line without). Exit codes: 0 pass, 1 fail.
extends SceneTree

const GameOptions := preload("res://scripts/game_options.gd")

var _failures := 0


func _initialize() -> void:
	var code: int = await _run()
	print("OPTS2 %s" % ["PASS" if code == 0 else "FAIL"])
	quit(code)


func _check(ok: bool, what: String) -> void:
	print("  ", "ok   " if ok else "FAIL ", what)
	if not ok:
		_failures += 1


func _run() -> int:
	_settings_checks()
	if OS.get_environment("ROTWK_INSTALL").is_empty() or OS.get_environment("BFME2_INSTALL").is_empty():
		print("SKIP: the game run (set ROTWK_INSTALL and BFME2_INSTALL)")
		return 0 if _failures == 0 else 1
	if not ClassDB.class_exists("AptMenuPlayer"):
		_check(false, "the openbfme extension is loaded")
		return 1
	var dir := OS.get_user_data_dir().path_join("opts2-test")
	DirAccess.make_dir_recursive_absolute(dir)
	var file := dir.path_join("opts2-check-options.ini")
	# a file a retail game left (sorted "key = value" lines), with a key of another program
	var f := FileAccess.open(file, FileAccess.WRITE)
	f.store_string("Brightness = 50\nResolution = 1024 768\nScrollFactor = 50\nStaticGameLOD = High\nZZSomeModTool = keep me\n")
	f.close()
	_game_run(["--opts2-check", "--options-file=" + file], 37, "the Options menu check")
	var text := FileAccess.get_file_as_string(file)
	_check(text.contains("ZZSomeModTool = keep me\n") and text.contains("Resolution = 1024 768\n"), "another program's key and the untouched retail entries survive")
	# the live game: a low detail level, a brighter picture, the clock, a faster scroll, always-on health bars, edge scrolling off
	f = FileAccess.open(file, FileAccess.WRITE)
	f.store_string("Brightness = 70\nOpenBFMEEdgeScroll = off\nOpenBFMEGameClock = yes\nOpenBFMEHealthBars = always\nOpenBFMEUIScale = 150\nScrollFactor = 80\n"
		+ "StaticGameLOD = Low\n")
	f.close()
	_game_run(["--auto", "--advance=3", "--opts2-game", "--options-file=" + file], 8, "the settings in a live game")
	DirAccess.remove_absolute(file)
	return 0 if _failures == 0 else 1


func _settings_checks() -> void:
	var bad := PackedStringArray()
	for key in GameOptions.EXTRAS:
		if not String(key).begins_with(GameOptions.PREFIX):
			bad.append(key)
	_check(bad.is_empty() and GameOptions.EXTRAS.size() >= 15, "every OpenBFME key carries the prefix (%d keys) %s" % [GameOptions.EXTRAS.size(), str(bad)])
	var defaults := GameOptions.defaults()
	var invalid := PackedStringArray()
	for key in defaults:
		if not GameOptions.valid(key, defaults[key]):
			invalid.append("%s=%s" % [key, defaults[key]])
	_check(invalid.is_empty(), "every default is a value its key takes %s" % str(invalid))
	_check(defaults.OpenBFMEAntiAliasing == "off" and defaults.OpenBFMELook == "original" and defaults.OpenBFMEMasterVolume == "100" and defaults.OpenBFMEHealthBars == "retail"
		and defaults.OpenBFMEEdgeScroll == "retail" and defaults.OpenBFMEFreeCamera == "no" and defaults.OpenBFMERotateSpeed == "100" and defaults.OpenBFMEUIScale == "100",
		"the defaults are retail behaviour (no anti-aliasing, the original look, the retail camera and health bars, full master volume)")
	# lane ZOOM-1: the one extra whose default is the owner's, not retail's (2026-10-10: "by default have the option to zoom out like 2.5x as far")
	_check(defaults.OpenBFMEMaxZoomOut == "2.5" and GameOptions.valid("OpenBFMEMaxZoomOut", "1") and not GameOptions.valid("OpenBFMEMaxZoomOut", "4"),
		"the Max zoom-out defaults to 2.5x retail's highest camera; 1 is the original game")
	_check(defaults.ScrollFactor == "" and defaults.AllHealthBars == "no" and defaults.AlternateMouseSetup == "no", "the mirrored retail keys keep their retail names and defaults")
	var problems := []
	_check(GameOptions.value_of({"OpenBFMEAntiAliasing": "msaa16"}, "OpenBFMEAntiAliasing", problems) == "off" and problems.size() == 1,
		"a value the key does not take is reported and not used: %s" % str(problems))
	problems.clear()
	_check(GameOptions.value_of({"OpenBFMEMasterVolume": "101"}, "OpenBFMEMasterVolume", problems) == "100" and problems.size() == 1, "a volume above 100 is refused")
	problems.clear()
	_check(GameOptions.value_of({"ScrollFactor": "37"}, "ScrollFactor", problems) == "37" and problems.is_empty(), "a retail value reads as it is")
	_check(GameOptions.value_of({"ScrollFactor": "0"}, "ScrollFactor", problems) == "0" and GameOptions.value_of({}, "ScrollFactor", problems) == ""
		and problems.is_empty(), "ScrollFactor 0 is a value retail takes; no entry stays no entry (the getter's default)")
	_check(not GameOptions.valid("OpenBFMEAudioDevice", "") and GameOptions.valid("OpenBFMEAudioDevice", "Default"), "the output device takes a device name")


func _game_run(args: Array, min_ok: int, what: String) -> void:
	var output: Array = []
	var project := ProjectSettings.globalize_path("res://")
	var t0 := Time.get_ticks_msec()
	var code := OS.execute(OS.get_executable_path(), PackedStringArray(["--headless", "--path", project, "--"] + args), output, true)
	var text: String = "".join(output)
	var result := ""
	var oks := 0
	for line in text.split("\n"):
		if line.begins_with("OPTS2 ") or line.begins_with("GAME OPTIONS"):
			print("    ", line)
			if line.ends_with(": ok"):
				oks += 1
			if line.begins_with("OPTS2 RESULT"):
				result = line
		elif line.contains("SCRIPT ERROR") or line.contains("Parse Error"):
			print("    ", line)
	_check(code == 0 and result.ends_with(" 0 failed") and oks >= min_ok,
		"%s (%s): exit %d, %s, %d ok (at least %d), %.0f s" % [what, " ".join(args), code, result if not result.is_empty() else "NO RESULT", oks, min_ok, (Time.get_ticks_msec() - t0) / 1000.0])
