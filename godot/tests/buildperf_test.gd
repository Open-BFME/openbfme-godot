## Lane FOGFX-1 (FB-0055, a player on v0.4.0 alpha 3: "a brief 30-40% FPS drop when selecting a building for the builder to construct").
##
##   godot --headless --path godot --script res://tests/buildperf_test.gd
##
## Plays a skirmish (Men) with the scripted scenario play1_player.gd buildperf: the builder is selected and its first four structures are placed twice
## each (the pointer circling over the ground), against idle windows with the same pointer moves. FAIL (play1_player.gd _scenario_buildperf): a
## placement's median frame (main thread CPU) above x1.75 the idle one, a frame more than 50 ms over the idle median, the Palantir part above 2.5 ms a
## frame, a second placement of a structure that loads its model again, more than one bib material per bib texture. Before the fix, measured on a
## GTX 1060: every placement's first frame spent 9 - 14 ms in the ghost (a new instancer and model, a new bib material) and a farm's claim feedback
## 6.3 ms a frame in the Palantir part. Its BUILDPERF lines are printed. Needs ROTWK_INSTALL and BFME2_INSTALL (else SKIP, exit 77).
## Exit codes: 0 pass, 1 fail, 77 skip.
extends SceneTree


func _initialize() -> void:
	var code: int = _run()
	print("BUILDPERF %s" % ["PASS" if code == 0 else ("SKIP" if code == 77 else "FAIL")])
	quit(code)


func _run() -> int:
	if OS.get_environment("ROTWK_INSTALL").is_empty() or OS.get_environment("BFME2_INSTALL").is_empty():
		print("SKIP: set ROTWK_INSTALL and BFME2_INSTALL")
		return 77
	if not ClassDB.class_exists("GameWorld"):
		print("FAIL: the openbfme extension is not loaded")
		return 1
	var output: Array = []
	var args := ["--headless", "--path", ProjectSettings.globalize_path("res://"), "--", "--auto", "--play1=buildperf", "--seed=42", "--res=1280x720",
		"--faction=FactionMen", "--buildperf-count=4"]
	var code := OS.execute(OS.get_executable_path(), args, output, true)
	var text: String = "".join(output)
	var ok := false
	for line in text.split("\n"):
		if (line.begins_with("BUILDPERF") and not line.begins_with("BUILDPERF frame")) or line.begins_with("PLAY1") or line.begins_with("GAME FAIL"):
			print("    " + line)
		if line == "PLAY1 buildperf: ok":
			ok = true
	print("  %s game.tscn --play1=buildperf exits %d" % ["ok  " if code == 0 else "FAIL", code])
	print("  %s PLAY1 buildperf: ok" % ["ok  " if ok else "FAIL"])
	return 0 if code == 0 and ok else 1
