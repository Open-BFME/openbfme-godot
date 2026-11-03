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
		print("BFME1 MAP MEMORY SKIP: set BFME1_INSTALL")
		quit()
		return
	var fs = ClassDB.instantiate("RetailFileSystem")
	if not _check(fs.mount_bfme1_path(install).ok):
		_finish("")
		return
	var world = ClassDB.instantiate("GameWorld")
	root.add_child(world)
	if not _check(world.setup(fs).ok):
		_finish("")
		return
	var path := OS.get_temp_dir().path_join("bfme1-map-memory-%d.ini" % OS.get_process_id())
	DirAccess.remove_absolute(path)
	var selected := "maps/map mp enedwaith/map mp enedwaith.map"
	for run in 2:
		var shell = ClassDB.instantiate("AptMenuPlayer")
		root.add_child(shell)
		_check(shell.boot_shell(fs,world,{"skirmish_ini":path}).ok)
		_check(shell.shell_push("MainMenu.apt"))
		for i in 90: shell.tick(0.033)
		_check(shell.shell_fscommand("AptMainMenu::Skirmish",""))
		for i in 90: shell.tick(0.033)
		if run == 0:
			_check(shell.lobby_apply({"profile":"MapMemory"}).ok)
			for i in 90: shell.tick(0.033)
			_check(shell.lobby_apply({"map":selected}).ok)
			# The selection is saved before the lobby is torn down.
			var saved := FileAccess.get_file_as_string(path)
			_check("enedwaith" in saved)
		else:
			_check(shell.lobby_state().map == selected,str(shell.lobby_state()))
		shell.free()
	DirAccess.remove_absolute(path)
	_finish("BFME1 MAP MEMORY PASS")
