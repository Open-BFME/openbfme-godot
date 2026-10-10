## Headless test of lane UI-4 (the owner's report on v0.3.0-preview.1, 2026-10-10: maximising the window greyed out the main menu's buttons):
## scenes/game.tscn runs as a child process with --resize-check (game.gd _run_resize_check): the main menu settles, the window (headless: the root
## viewport) is resized 1280x720 -> 1920x1080 -> back with the Options nav closed and open, and the stray input a window manager sends around a
## maximise (a release with no press, a press outside the window released over a button, a pointer move) goes over the Options and Solo Play nav
## entries. Every clip frame, button hittability and the screen stack must stay as they were, and no shell request may be sent.
##
##   godot --headless --path godot --script res://tests/ui4_resize_test.gd
##
## Needs ROTWK_INSTALL and BFME2_INSTALL (prints SKIP and exits 77 without). Exit codes: 0 pass, 1 fail, 77 skip.
extends SceneTree


func _initialize() -> void:
	var code: int = _run()
	print("UI4 RESIZE TEST %s" % ["PASS" if code == 0 else ("SKIP" if code == 77 else "FAIL")])
	quit(code)


func _run() -> int:
	if OS.get_environment("ROTWK_INSTALL").is_empty() or OS.get_environment("BFME2_INSTALL").is_empty():
		print("SKIP ui4_resize_test: ROTWK_INSTALL / BFME2_INSTALL not set")
		return 77
	var output: Array = []
	var code := OS.execute(OS.get_executable_path(), ["--headless", "--path", ProjectSettings.globalize_path("res://"), "--", "--resize-check", "--no-intro"],
		output, true)
	var text: String = "".join(output)
	var verdict := ""
	var checked := 0
	for line in text.split("\n"):
		if line.begins_with("UI4 RESIZE"):
			print("  ", line)
		if line.begins_with("UI4 RESIZE PASS") or line.begins_with("UI4 RESIZE FAIL ("):
			verdict = line
		if line.contains(" checked, 0 changed"):
			checked += 1
	var ok := code == 0 and verdict.begins_with("UI4 RESIZE PASS") and checked == 19
	if not ok:
		print("  game.tscn exit %d, verdict '%s', %d clean comparisons of 19" % [code, verdict, checked])
		return 1
	return 0
