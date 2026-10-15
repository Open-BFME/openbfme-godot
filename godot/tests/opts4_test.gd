## Headless test of lane OPTS-4 (three Options bugs of v0.4.0-alpha.3): the game with --opts4-check (scripts/opts4_check.gd) on an Options.ini of the
## test's own, at 1024x768 and at 2560x1440 (the owner's size): an open drop-down list is drawn over the rows below it and takes their clicks, the
## sliders' tick rows stay inside their windows, the volume sliders act while they move (the audio request log has the volume lines and the samples).
##
##   godot --headless --path godot --script res://tests/opts4_test.gd
##
## Needs ROTWK_INSTALL and BFME2_INSTALL (a SKIP line without). Exit codes: 0 pass, 1 fail.
extends SceneTree

var _failures := 0


func _initialize() -> void:
	var code: int = await _run()
	print("OPTS4 %s" % ["PASS" if code == 0 else "FAIL"])
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
	var dir := OS.get_user_data_dir().path_join("opts4-test")
	DirAccess.make_dir_recursive_absolute(dir)
	var file := dir.path_join("opts4-check-options.ini")
	var options := "AmbientVolume = 60.000000\nMovieVolume = 70.000000\nMusicVolume = 70.000000\nSFXVolume = 80.000000\nStaticGameLOD = High\nVoiceVolume = 75.000000\n"
	for size in ["1024x768", "2560x1440"]:
		var f := FileAccess.open(file, FileAccess.WRITE)
		f.store_string(options)
		f.close()
		var log := dir.path_join("opts4-audio-%s.tsv" % size)
		OS.set_environment("OPENBFME_AUDIO_LOG", log)
		_game_run(["--opts4-check", "--opts4-size=" + size, "--options-file=" + file], "the Options check at " + size)
		_check(FileAccess.get_file_as_string(file) == options, "CANCEL wrote nothing: the check's Options.ini is as it was (%s)" % size)
		DirAccess.remove_absolute(log)
	OS.set_environment("OPENBFME_AUDIO_LOG", "")
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
		if line.begins_with("OPTS4 "):
			print("    ", line)
			if line.begins_with("OPTS4 RESULT"):
				result = line
		elif line.contains("SCRIPT ERROR") or line.contains("Parse Error"):
			print("    ", line)
			_failures += 1
	_check(code == 0 and result.ends_with(" 0 failed") and not result.begins_with("OPTS4 RESULT: 0 ok"),
		"%s (%s): exit %d, %s, %.0f s" % [what, " ".join(args), code, result if not result.is_empty() else "NO RESULT", (Time.get_ticks_msec() - t0) / 1000.0])
