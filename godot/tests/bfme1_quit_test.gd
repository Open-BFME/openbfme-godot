extends SceneTree
## GDScript assert() only logs a SCRIPT ERROR and the run carries on, so a failed one still ended in PASS / exit 0. Every check goes through
## _check: a failure is recorded and printed as FAIL, and _finish quits 1 when any failed.
var _failures: Array[String] = []

func _check(ok: bool, reason := "") -> bool:
	if not ok:
		_failures.append(reason)
		var stack := get_stack()
		printerr("FAIL: ", reason, " (line ", stack[1].line if stack.size() > 1 else 0, ")")
	return ok

func _finish(pass_message: String) -> void:
	if _failures.is_empty():
		print(pass_message)
		quit()
		return
	printerr(_failures.size(), " check(s) failed")
	quit(1)

func _initialize() -> void: call_deferred("_run")
func _run() -> void:
	var install := OS.get_environment("BFME1_INSTALL")
	if install.is_empty():
		print("BFME1 SKIP: set BFME1_INSTALL")
		quit()
		return
	var fs = ClassDB.instantiate("RetailFileSystem")
	var mounted: Dictionary = fs.mount_bfme1_path(install)
	print("BFME1 MOUNT ", mounted)
	if not mounted.ok: quit(1); return
	var world = ClassDB.instantiate("GameWorld")
	root.add_child(world)
	var setup: Dictionary = world.setup(fs)
	print("BFME1 SETUP ", setup.get("templates"), " templates, ", setup.get("factions"), " factions")
	for error in setup.errors: print("BFME1 ERROR ", error)
	if not setup.ok: quit(1); return
	var shell = ClassDB.instantiate("AptMenuPlayer")
	root.add_child(shell)
	var boot: Dictionary = shell.boot_shell(fs, world, {"skirmish_ini": OS.get_user_data_dir() + "/bfme1-test-skirmish.ini"})
	print("BFME1 SHELL ", boot)
	if not boot.ok: quit(1); return
	var instancer = ClassDB.instantiate("W3DInstancer")
	root.add_child(instancer)
	if not _check(instancer.setup(fs).ok):
		_finish("")
		return
	var floor_id: int = instancer.add_model("GBCampFloor")
	_check(floor_id >= 0)  # tolerance: an index: any found index is valid
	_check(instancer.get_model_report(floor_id).errors.is_empty())
	print("BFME1 CAMP FLOOR PASS")
	for name in ["Options", "Save", "Load", "Resume", "Restart", "ExitMission"]:
		var menu: Dictionary = shell.quit_menu({"kind":"open","context":{"in_game":true,"mode":2,"kind":3}})
		for i in 90: shell.tick(0.033)
		for i in 3: await process_frame
		var labels := {}
		for op in shell.describe_ops():
			if not op.begins_with("text "): continue
			for item in ["Options", "Save", "Load", "Resume", "Restart", "ExitMission"]:
				if ("." + item + ".") in op:
					labels[item] = op.split("'")[1]
		_check(labels.size() == 6, str(labels))
		var unique := {}
		for label in labels.values(): unique[label] = true
		_check(unique.size() == 6, str(labels))
		_check(shell.quit_menu({"kind":"state"}).initialized == 1)
		var button: Dictionary = shell.find_button(menu.level,name)
		if not _check(button.found, name):
			_finish("")
			return
		shell.post_mouse_move_stage(Vector2(button.x,button.y))
		shell.post_mouse_button(true)
		shell.post_mouse_button(false)
		for i in 90: shell.tick(0.033)
		if name in ["Restart", "ExitMission"]:
			var yes: Dictionary = shell.find_button(menu.level, "popup.navSearching.Yes")
			if not _check(yes.found, name):
				_finish("")
				return
			shell.post_mouse_move_stage(Vector2(yes.x,yes.y))
			shell.post_mouse_button(true)
			shell.post_mouse_button(false)
			for i in 90: shell.tick(0.033)
		var expected: String = {"Options":"QuitMenuOptions","Save":"QuitMenuSave","Load":"QuitMenuLoad","Resume":"QuitMenuReturn","Restart":"QuitMenuRestart","ExitMission":"QuitMenuExit"}[name]
		var state: Dictionary = shell.quit_menu({"kind":"state"})
		_check(state.requests.has(expected), str(state))
		print("BFME1 QUIT PASS ",name)
		shell.quit_menu({"kind":"close"})
	for error in shell.get_shell_report().errors:
		_check(not "constant type 8" in error, error)
	_finish("BFME1 PAUSE MENU PASS")
