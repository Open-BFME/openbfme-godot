## Headless test of lane OPTS-5 (one Options menu, the OpenBFME one, with the original game's settings among its rows): the game with --opts5-check
## (scripts/opts5_check.gd) on an Options.ini of the test's own at 1024x768, 1280x800, 2560x1440 and 3440x1440 (the entry points, the layout rules of
## every tab, the original settings' effects, ACCEPT / CANCEL / RESET), then --auto --opts5-game (the quit menu's Options over a live game).
##
##   godot --headless --path godot --script res://tests/opts5_test.gd
##
## Needs ROTWK_INSTALL and BFME2_INSTALL (a SKIP line without). Exit codes: 0 pass, 1 fail.
extends SceneTree

var _failures := 0


func _initialize() -> void:
	var code: int = await _run()
	print("OPTS5 %s" % ["PASS" if code == 0 else "FAIL"])
	quit(code)


func _check(ok: bool, what: String) -> void:
	print("  ", "ok   " if ok else "FAIL ", what)
	if not ok:
		_failures += 1


func _run() -> int:
	if OS.get_environment("ROTWK_INSTALL").is_empty() or OS.get_environment("BFME2_INSTALL").is_empty():
		print("SKIP: the game run (set ROTWK_INSTALL and BFME2_INSTALL)")
		return 0
	if not ClassDB.class_exists("AptMenuPlayer"):
		_check(false, "the openbfme extension is loaded")
		return 1
	var dir := OS.get_user_data_dir().path_join("opts5-test")
	DirAccess.make_dir_recursive_absolute(dir)
	var file := dir.path_join("opts5-check-options.ini")
	# every key RotWK's Save writes, in its own format and order: a real ACCEPT with no edit must leave this file's bytes alone
	var options := "AllHealthBars = no\nAmbientVolume = 50.000000\nAudioLOD = Low\nBrightness = 50\nFirewallPortOverride = 0\nMovieVolume = 70.000000\n" \
		+ "MusicVolume = 70.000000\nSFXVolume = 70.000000\nScrollFactor = 50\nSendDelay = no\nStaticGameLOD = High\nUseEAX3 = no\nVoiceVolume = 70.000000\n"
	var sizes := OS.get_environment("OPTS5_SIZES").split(",", false) if not OS.get_environment("OPTS5_SIZES").is_empty() else PackedStringArray(["1024x768", "1280x800", "2560x1440", "3440x1440"])
	for size in sizes:
		var f := FileAccess.open(file, FileAccess.WRITE)
		f.store_string(options)
		f.close()
		_game_run(["--opts5-check", "--opts5-size=" + size, "--options-file=" + file], "the Options menu at " + size)
	var f2 := FileAccess.open(file, FileAccess.WRITE)
	f2.store_string(options)
	f2.close()
	_game_run(["--auto", "--advance=2", "--opts5-game", "--options-file=" + file], "the Options menu over a live game")
	_check(FileAccess.get_file_as_string(file) == options, "the in-game visit wrote nothing (Esc is CANCEL)")
	DirAccess.remove_absolute(file)
	return 0 if _failures == 0 else 1


func _game_run(args: Array, what: String) -> void:
	var output: Array = []
	var project := ProjectSettings.globalize_path("res://")
	var t0 := Time.get_ticks_msec()
	var code := OS.execute(OS.get_executable_path(), PackedStringArray(["--headless", "--path", project, "--"] + args), output, true)
	var text: String = "".join(output)
	var result := ""
	for line in text.split("\n"):
		if line.begins_with("OPTS5 "):
			print("    ", line)
			if line.begins_with("OPTS5 RESULT"):
				result = line
		elif line.contains("SCRIPT ERROR") or line.contains("Parse Error"):
			print("    ", line)
			_failures += 1
	_check(code == 0 and result.ends_with(" 0 failed") and not result.begins_with("OPTS5 RESULT: 0 ok"),
		"%s (%s): exit %d, %s, %.0f s" % [what, " ".join(args), code, result if not result.is_empty() else "NO RESULT", (Time.get_ticks_msec() - t0) / 1000.0])
