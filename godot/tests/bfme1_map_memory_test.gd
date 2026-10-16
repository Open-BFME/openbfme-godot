extends SceneTree
func _initialize() -> void: call_deferred("_run")
func _run() -> void:
	var install := OS.get_environment("BFME1_INSTALL")
	if install.is_empty():
		print("BFME1 MAP MEMORY SKIP: set BFME1_INSTALL")
		quit()
		return
	var fs = ClassDB.instantiate("RetailFileSystem")
	assert(fs.mount_bfme1_path(install).ok)
	var world = ClassDB.instantiate("GameWorld")
	root.add_child(world)
	assert(world.setup(fs).ok)
	var path := OS.get_temp_dir().path_join("bfme1-map-memory-%d.ini" % OS.get_process_id())
	DirAccess.remove_absolute(path)
	var selected := "maps/map mp enedwaith/map mp enedwaith.map"
	for run in 2:
		var shell = ClassDB.instantiate("AptMenuPlayer")
		root.add_child(shell)
		assert(shell.boot_shell(fs,world,{"skirmish_ini":path}).ok)
		assert(shell.shell_push("MainMenu.apt"))
		for i in 90: shell.tick(0.033)
		assert(shell.shell_fscommand("AptMainMenu::Skirmish",""))
		for i in 90: shell.tick(0.033)
		if run == 0:
			assert(shell.lobby_apply({"profile":"MapMemory"}).ok)
			for i in 90: shell.tick(0.033)
			assert(shell.lobby_apply({"map":selected}).ok)
			# The selection is saved before the lobby is torn down.
			var saved := FileAccess.get_file_as_string(path)
			assert("enedwaith" in saved)
		else:
			assert(shell.lobby_state().map == selected,str(shell.lobby_state()))
		shell.free()
	DirAccess.remove_absolute(path)
	print("BFME1 MAP MEMORY PASS")
	quit()
