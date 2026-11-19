extends SceneTree
func _initialize() -> void: call_deferred("_run")
func _run() -> void:
	var install := OS.get_environment("BFME1_INSTALL")
	if install.is_empty():
		print("BFME1 SKIP: set BFME1_INSTALL")
		quit(77)
		return
	var fs = ClassDB.instantiate("RetailFileSystem")
	var mounted: Dictionary = fs.mount_bfme1_path(install)
	if not mounted.ok: quit(1); return
	var world = ClassDB.instantiate("GameWorld")
	root.add_child(world)
	var setup: Dictionary = world.setup(fs)
	if not setup.ok: quit(1); return
	var shell = ClassDB.instantiate("AptMenuPlayer")
	root.add_child(shell)
	var boot: Dictionary = shell.boot_shell(fs, world, {"skirmish_ini": OS.get_temp_dir().path_join("bfme1-options-test-" + str(Time.get_ticks_usec()) + ".ini")})
	if not boot.ok: quit(1); return
	if not shell.shell_push("MainMenu.apt"): print(shell.get_shell_report()); quit(1); return
	for i in 90: shell.tick(0.033)

	var display = load("res://scripts/openbfme_options_screen.gd").new()
	display.shell = shell
	display.settings = preload("res://scripts/display_settings.gd").new()
	root.add_child(display)
	assert(shell.shell_fscommand("AptMainMenu::Options", ""))
	for i in 120: shell.tick(0.033)
	await process_frame
	await process_frame
	assert(display.is_open(), "BFME1 opens the shared replacement Settings menu")
	assert(shell.openbfme_page_state().menu)
	assert(display.current_tab() == "Display")
	var page = shell.openbfme_page_state()
	for action in ["cancel", "reset", "accept"]:
		assert(page.buttons.has(action), "Replacement menu handles the BFME1 bottom bar")
	for gadget in shell.list_gadgets():
		if gadget.name.begins_with("Options::"):
			assert(gadget.hidden, "Original settings controls stay hidden: " + gadget.name)
	var report = shell.get_shell_report()
	assert(report.errors.is_empty(), str(report.errors))
	assert(report.calls_without_function.is_empty(), str(report.calls_without_function))
	display.close(false)
	for i in 60: shell.tick(0.033)
	assert(shell.shell_stack()[-1] == "MainMenu.apt", "Cancel returns to main menu")
	assert(shell.shell_fscommand("AptMainMenu::Options", ""))
	for i in 120: shell.tick(0.033)
	await process_frame
	await process_frame
	assert(display.is_open(), "Replacement settings reopen")
	display.close(false)
	for i in 60: shell.tick(0.033)
	print("BFME1 OPTIONS PASS: shared replacement menu, hidden original controls, bottom bar and reopen")
	quit(0)
