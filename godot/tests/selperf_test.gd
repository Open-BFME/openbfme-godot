## Lane SELPERF-1: a large selection costs the frame little (the owner on v0.3.0-preview.4: "when I select a bunch of troops, fps drops to like 25-12").
##
##   godot --headless --path godot --script res://tests/selperf_test.gd
##
## Plays a skirmish with the scripted scenario play1_player.gd selperf: 150 hordes of the local player (2250 members) in a block; pairs of windows with
## nothing and with 1 or 150 selected, standing, while the camera pans and while the army marches (Q and a right-click order before every window,
## the window the frames its move hint is drawn in, the camera following); a window counts only with the selection, the movement and the CPU timings
## it needs (play1_player.gd). FAIL when a phase's median pair ratio of the main thread's CPU time per frame exceeds x1.15 (before the
## fix a marching army cost seconds a frame: the CPU-composed merge decals). Its SELPERF lines are printed. Needs ROTWK_INSTALL and BFME2_INSTALL
## (else SKIP, exit 77).
## Exit codes: 0 pass, 1 fail, 77 skip.
extends SceneTree


func _initialize() -> void:
	var code: int = _run()
	print("SELPERF %s" % ["PASS" if code == 0 else ("SKIP" if code == 77 else "FAIL")])
	quit(code)


func _run() -> int:
	if OS.get_environment("ROTWK_INSTALL").is_empty() or OS.get_environment("BFME2_INSTALL").is_empty():
		print("SKIP: set ROTWK_INSTALL and BFME2_INSTALL")
		return 77
	if not ClassDB.class_exists("GameWorld"):
		print("FAIL: the openbfme extension is not loaded")
		return 1
	var output: Array = []
	# the gate's cut of the benchmark (a run of every phase and count takes about 15 minutes): standing, panning and marching (a fresh order before
	# every window, the window its move hint's frames) with 1 and 150 selected, five pairs each; judged on the main thread's CPU time (a headless run
	# has no GPU). About 3 minutes on a quiet host; --selperf-deadline stops it with a FAIL at 600 s, inside verify.sh's 900 s per Godot gate.
	# The gate's budget is x1.15, a regression check: a held selection of 150 costs 0.5 - 0.8 ms a frame (the command bar about 0.3 of it),
	# 6 - 9% of a quiet CI host's 8.5 ms headless frame, so x1.10 there passes and fails from run to run; before this lane the same cut read
	# x1.3 - 1.5 standing and x5 - 150 marching. The +10% target itself is measured on the owner's idle 4090 (tools/perf/selperf_win_idle.sh)
	var args := ["--headless", "--path", ProjectSettings.globalize_path("res://"), "--", "--auto", "--play1=selperf", "--seed=42", "--selperf-phases=idle,pan,march",
		"--selperf-counts=1,150", "--selperf-reps=5", "--selperf-deadline=600", "--selperf-budget=0.15"]
	var code := OS.execute(OS.get_executable_path(), args, output, true)
	var text: String = "".join(output)
	var ok := false
	for line in text.split("\n"):
		if line.begins_with("SELPERF") or line.begins_with("PLAY1") or line.begins_with("GAME FAIL"):
			print("    " + line)
		if line == "PLAY1 selperf: ok":
			ok = true
	print("  %s game.tscn --play1=selperf exits %d" % ["ok  " if code == 0 else "FAIL", code])
	print("  %s PLAY1 selperf: ok" % ["ok  " if ok else "FAIL"])
	return 0 if code == 0 and ok else 1
