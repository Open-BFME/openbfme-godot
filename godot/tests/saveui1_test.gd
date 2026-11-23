## OpenBFME. GPL-3.0.
## Headless test of lane SAVE-UI1, the owner's report of 2026-10-15: in a game the quit menu's Save left the front-end background over the game (the
## HUD and the health bars on it) with no way back. The game runs scripts/save_flow.gd run_ui_test (--save-ui-test) twice, with real clicks and keys:
##  * a skirmish (--auto: the main menu, the lobby, the game);
##  * the first RotWK campaign mission started from the main menu's campaign command (--campaign-menu=Expansion1Campaign).
## Each run must print every check of CHECKS by name as "SAVEUI check <name>: ok", then "SAVEUI RESULT <n> checks, 0 failed", and exit 0. The checker
## itself refuses an empty output, a missing or extra check, a FAIL, a missing RESULT line and a non-zero exit (its own cases run first, without the games).
##
##   godot --headless --path godot --script res://tests/saveui1_test.gd
##
## The game runs need ROTWK_INSTALL and BFME2_INSTALL (a SKIP line without). Exit codes: 0 pass, 1 fail.
extends SceneTree

const CHECKS := ["quit-menu", "save-page", "held-key-cancelled", "escape-closes-page", "cancel-closes-page", "options-back", "options-twice", "typed-name",
	"ctrl-z-in-name-field", "saved-back-to-game", "save-list-rows", "modifier-released-in-menu", "modifier-held-through-menu", "load-page-lists", "loaded-plays-on",
	"resume-clears-background"]

var _failures := 0


func _initialize() -> void:
	var code: int = _run()
	print("SAVEUI1 %s" % ["PASS" if code == 0 else "FAIL"])
	quit(code)


func _check(ok: bool, what: String) -> void:
	print("  ", "ok   " if ok else "FAIL ", what)
	if not ok:
		_failures += 1


## the verdict of one game run: "" when it passed, else why not
static func verdict(text: String, code: int) -> String:
	var seen: Array = []
	var result := ""
	for line in text.split("\n"):
		if line.begins_with("SAVEUI check "):
			var rest := line.substr(13)
			var name := rest.get_slice(":", 0)
			if not rest.substr(name.length() + 1).strip_edges().begins_with("ok"):
				return "check %s failed: %s" % [name, line]
			if seen.has(name):
				return "check %s ran twice" % name
			seen.append(name)
		elif line.begins_with("SAVEUI RESULT "):
			result = line
	if seen.is_empty():
		return "no check ran"
	for c in CHECKS:
		if not seen.has(c):
			return "check %s is missing (ran: %s)" % [c, ",".join(seen)]
	for c in seen:
		if not CHECKS.has(c):
			return "check %s is not in the test's list" % c
	if result.is_empty():
		return "no SAVEUI RESULT line"
	if result != "SAVEUI RESULT %d checks, 0 failed: %s" % [CHECKS.size(), ",".join(CHECKS)]:
		return "result line: " + result
	if code != 0:
		return "exit code %d" % code
	return ""


func _checker_cases() -> void:
	var good := ""
	for c in CHECKS:
		good += "SAVEUI check %s: ok detail\n" % c
	good += "SAVEUI RESULT %d checks, 0 failed: %s\n" % [CHECKS.size(), ",".join(CHECKS)]
	_check(verdict(good, 0) == "", "checker: a full passing run passes")
	_check(verdict("", 0) != "", "checker: an empty output fails")
	_check(verdict(good, 1) != "", "checker: a non-zero exit (a crashed game) fails")
	_check(verdict(good.replace("SAVEUI RESULT", "SAVEUI RESUL"), 0) != "", "checker: a missing RESULT line (a truncated run) fails")
	_check(verdict(good.replace("SAVEUI check typed-name: ok detail\n", ""), 0) != "", "checker: a missing check fails")
	_check(verdict(good.replace("typed-name: ok", "typed-name: FAIL"), 0) != "", "checker: a FAIL fails")
	_check(verdict(good + "SAVEUI check extra: ok\n", 0) != "", "checker: an unknown check fails")
	_check(verdict(good + "SAVEUI check quit-menu: ok\n", 0) != "", "checker: a check run twice fails")


func _run() -> int:
	_checker_cases()
	if OS.get_environment("ROTWK_INSTALL").is_empty() or OS.get_environment("BFME2_INSTALL").is_empty():
		print("SKIP: the game runs (set ROTWK_INSTALL and BFME2_INSTALL)")
		return 0 if _failures == 0 else 1
	if not ClassDB.class_exists("AptMenuPlayer"):
		_check(false, "the openbfme extension is loaded")
		return 1
	_script_requests_case()
	_game_run(["--auto", "--res=1024x768"], "a skirmish")
	_game_run(["--campaign-menu=Expansion1Campaign", "--res=1280x720"], "the first campaign mission from the main menu")
	return 0 if _failures == 0 else 1


## lane SAVEUIHELD-1: the campaign run's intro (MAP ANG Angmar, "Intro Cine Part 5" at frame 181: RESET_CAMERA, CAMERA_LETTERBOX_END, ENABLE_INPUT) must
## reach the script side (campaign_cine.gd: the control bar, the HUD's input) even when the logic publishes a batch between campaign_cine.gd's
## take_script_requests and its update_script_view in a render frame. Before the fix update_script_view took that batch and dropped it: under load the game
## stayed in the letterbox with the input off (the camera could not scroll: held-key-cancelled failed at frame ~2380). Here every logic frame is published
## in that gap, on the main thread: deterministic.
func _script_requests_case() -> void:
	var fs: RefCounted = ClassDB.instantiate("RetailFileSystem")
	if not fs.mount_retail().ok:
		_check(false, "script requests: the retail files mount")
		return
	var world: Node3D = ClassDB.instantiate("GameWorld")
	root.add_child(world)
	var rep: Dictionary = world.setup(fs)
	if rep.ok:
		rep = world.load_map("map ang angmar", {"campaign": true, "seed": 4711, "logic_thread": false})
	if not rep.ok:
		_check(false, "script requests: map ang angmar loads (%s)" % str(rep.get("errors", [])).left(300))
		world.queue_free()
		return
	world.set_auto_advance(false)
	var seen: Array = [] # action@frame
	var keyed: Array = [] # action@frame(params): a request taken twice
	var take := func() -> void:
		for r in world.take_script_requests():
			seen.append("%s@%d" % [r.action, int(r.frame)])
			keyed.append("%s@%d(%s) %s" % [r.action, int(r.frame), ",".join((r.params as Array).map(func(p): return "%s/%s/%s" % [p.int, p.real, p.string])), r.script])
	var renders := 0
	while world.get_frame() < 200 and renders < 3000:
		take.call() # campaign_cine.gd _process: the script side's take first
		world.advance(1.0 / 30.0) # the logic publishes its frame now, after that take (the worker publishes at any time)
		world.update_script_view(1000.0 / 30.0, {"x": 0.0, "y": 0.0, "z": 0.0, "angle": 0.0}) # then the director's update in the same render frame
		renders += 1
	take.call()
	var want := ["CAMERA_LETTERBOX_BEGIN@1", "DISABLE_INPUT@1", "MOVE_CAMERA_TO@136", "RESET_CAMERA@181", "CAMERA_LETTERBOX_END@181", "ENABLE_INPUT@181"]
	var missing: Array = want.filter(func(w): return not seen.has(w))
	var twice: Array = []
	for i in keyed.size():
		if keyed.find(keyed[i]) != i and not twice.has(keyed[i]):
			twice.append(keyed[i])
	# tolerance: none; the requests of a frame are compared by name and frame (frame 1 to 200, the map's own intro timers)
	_check(world.get_frame() >= 200 and missing.is_empty() and twice.is_empty(), "script requests published between the script side's take and update_script_view reach it: frame %d, %d requests, missing %s, twice %s" % [
		world.get_frame(), seen.size(), str(missing), str(twice)])
	world.set_logic_thread(false)
	root.remove_child(world)
	world.free()


func _game_run(args: Array, what: String) -> void:
	var dir := OS.get_user_data_dir().path_join("saveui1-test-%d" % Time.get_ticks_usec())
	DirAccess.make_dir_recursive_absolute(dir)
	var output: Array = []
	var project := ProjectSettings.globalize_path("res://")
	var t0 := Time.get_ticks_msec()
	# health bars always on: the run's control that the game's decorations are drawn, and hidden under the menus
	var options := dir.path_join("options.ini")
	var opt := FileAccess.open(options, FileAccess.WRITE)
	opt.store_string("OpenBFMEHealthBars = always\n")
	opt.close()
	var full: Array = ["--headless", "--path", project, "--"] + args + ["--save-ui-test", "--save-dir=" + dir, "--options-file=" + options, "--no-intro"]
	var code := OS.execute(OS.get_executable_path(), PackedStringArray(full), output, true)
	var text: String = "".join(output)
	for line in text.split("\n"):
		if line.begins_with("SAVEUI") or line.contains("SCRIPT ERROR") or line.contains("Parse Error"):
			print("    ", line.left(400))
	var why := verdict(text, code)
	_check(why.is_empty(), "%s (%s): %s, %.0f s" % [what, " ".join(args), "passed" if why.is_empty() else why, (Time.get_ticks_msec() - t0) / 1000.0])
	for f in DirAccess.get_files_at(dir):
		DirAccess.remove_absolute(dir.path_join(f))
	DirAccess.remove_absolute(dir)
