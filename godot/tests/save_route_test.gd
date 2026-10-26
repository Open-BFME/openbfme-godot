## Asset-free engine entrypoints, using a complete synthetic save from openbfme_tests.
extends SceneTree

const SaveFlow := preload("res://scripts/save_flow.gd")

class Shell extends Node2D:
	var refused := []

	func save_load_refused(name: String, error: String) -> void:
		refused.append([name, error])


var checks := 0
var failures := 0


func check(ok: bool, label: String) -> void:
	checks += 1
	if not ok:
		failures += 1
		printerr("SAVE ROUTE FAIL: " + label)


func _initialize() -> void:
	_run.call_deferred()


func _run() -> void:
	var path := OS.get_environment("OPENBFME_SAVE_FIXTURE")
	check(ClassDB.class_exists("GameWorld") and FileAccess.file_exists(path), "native extension and synthetic fixture required")
	if failures != 0:
		quit(1)
		return
	var world = ClassDB.instantiate("GameWorld")
	var expected := "Made with Nightly 0.4.12 (2026-10-22). Open it with that version from the launcher."
	var verdict: Dictionary = world.check_save(path)
	var error := str(verdict.get("error", ""))
	check(not verdict.ok and error.begins_with(expected), "direct load check names the originating build and launcher")
	check(not error.contains("another profile") and not error.contains("0123456789abcdef"), "no identity or commit hash in the refusal")
	var rows: Array = world.list_saves(path.get_base_dir())
	var selected := rows.filter(func(row): return row.path == path)
	check(selected.size() == 1 and not selected[0].loadable and str(selected[0].refusal).begins_with(expected), "Load screen listing uses the same refusal")
	# Only mounting and the rendered shell are replaced; startup dispatch, SaveFlow and the native save check are production code.
	var g = load("res://tests/save_route_game.gd").new()
	g._world = world
	g._shell = Shell.new()
	g._save_flow = SaveFlow.new(g)
	var args := OS.get_cmdline_user_args()
	check(not args.is_empty() and g._set_recording_args(args).is_empty() and g._load_file == path and g._replay_file.is_empty(), "--load selects the game's save startup action")
	await g._open_recording()
	check(g._shell.refused.size() == 1 and g._shell.refused[0][1] == error, "startup reaches the native load check and shared refusal path")
	check(g.failed == "Save refused: " + error, "startup refusal is visible even without an open Load page")
	g._save_flow.request("LoadGameFile", path)
	await process_frame
	check(g._shell.refused.size() == 2 and g._shell.refused[0] == g._shell.refused[1], "Load screen and --load use the same save path and refusal")
	g._shell.free()
	g.free()
	world.free()
	print("SAVE ROUTE GAME: %d checks, %d failures" % [checks, failures])
	quit(0 if failures == 0 else 1)
