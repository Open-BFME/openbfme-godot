extends SceneTree

var _failures := 0
var _shell


func _initialize() -> void:
	_run.call_deferred()


func _check(condition: bool, message: String) -> void:
	print("BFME1 SKIRMISH UI ", "ok " if condition else "FAIL ", message)
	if not condition:
		_failures += 1


func _advance() -> void:
	for frame in 90:
		_shell.tick(0.033)
		_shell.render(true)


func _click_at(point: Vector2) -> void:
	_shell.post_mouse_move_stage(point)
	_shell.post_mouse_button(true)
	_shell.post_mouse_button(false)
	_advance()


func _capture(folder: String, name: String) -> void:
	if DisplayServer.get_name() == "headless":
		return
	for frame in 4:
		_shell.tick(0.033)
		_shell.render(true)
		await process_frame
	await RenderingServer.frame_post_draw
	_check(root.get_texture().get_image().save_png(folder.path_join(name + ".png")) == OK, name + " screenshot saved")


func _run() -> void:
	var install := OS.get_environment("BFME1_INSTALL")
	if install.is_empty():
		print("BFME1 SKIRMISH UI SKIP: set BFME1_INSTALL")
		quit(77)
		return
	create_timer(180.0).timeout.connect(func(): quit(1))
	root.size = Vector2i(1600, 900)
	var folder := OS.get_environment("BFME1_SKIRMISH_UI_DIR")
	if folder.is_empty():
		folder = OS.get_temp_dir().path_join("bfme1-skirmish-ui-%d" % OS.get_process_id())
	_check(DirAccess.make_dir_recursive_absolute(folder) == OK, "isolated profile directory created")
	var filesystem = ClassDB.instantiate("RetailFileSystem")
	var mount: Dictionary = filesystem.mount_bfme1_path(install)
	_check(mount.ok, "BFME1 archives mounted")
	if not mount.ok:
		quit(1)
		return
	var world = ClassDB.instantiate("GameWorld")
	root.add_child(world)
	var setup: Dictionary = world.setup(filesystem)
	_check(setup.ok, "BFME1 world initialized")
	if not setup.ok:
		quit(1)
		return
	_shell = ClassDB.instantiate("AptMenuPlayer")
	_shell.auto_process = false
	root.add_child(_shell)
	var boot: Dictionary = _shell.boot_shell(filesystem, world, {"skirmish_ini": folder.path_join("Skirmish.ini"), "options_file": folder.path_join("Options.ini")})
	_check(boot.ok, "BFME1 shell booted")
	if not boot.ok:
		quit(1)
		return
	_check(_shell.shell_push("MainMenu.apt"), "retail main menu opened")
	_advance()
	var menu_errors: Dictionary = {}
	for error in _shell.get_shell_report().get("errors", []):
		menu_errors[str(error)] = true
	_check(_shell.shell_fscommand("AptMainMenu::Skirmish", ""), "retail skirmish opened")
	_advance()
	_check(_shell.lobby_apply({"profile": "SkirmishUITest"}).ok, "test profile created")
	_advance()
	_check(_shell.lobby_apply({"map": "maps/map mp fords of isen/map mp fords of isen.map", "slots": [{"slot": 0, "faction": "FactionGondor", "color": 0}, {"slot": 1, "faction": "FactionIsengard", "color": 1}]}).ok, "two-player map and BFME1 armies selected")
	_advance()
	for image_name in ["AptVSliderDownButtonEnabled", "AptVSliderUpButtonEnabled", "AptListBoxHiliteSelectedItem", "AptDifficultyNotConquered", "AptPlayerStart", "ScrollShroud"]:
		var image: Dictionary = _shell.shell_mapped_image(image_name)
		_check(image.ok, image_name + " loads from BFME1 artwork")
	for record in ["APT:SkirmishProfileCreatedNum", "APT:PlayerNameNum", "APT:TotalCareerWinsNum", "APT:TotalCareerLossesNum", "APT:CurrentWinStreakNumA", "APT:BestWinStreakNumA", "APT:TotalTimePlayedNum", "APT:CurFactionNameStr", "APT:NextWinsNumber"]:
		var text: String = _shell.apt_text(record)
		_check(not text.is_empty() and not text.begins_with("MISSING"), record + " populated: " + text)
	_check(_shell.apt_text("APT:PlayerNameNum") == "SkirmishUITest", "profile nickname is bound")
	_check(_shell.apt_text("APT:TotalCareerWinsNum") == "0", "new profile has zero wins")
	_check(_shell.apt_text("APT:TotalCareerLossesNum") == "0", "new profile has zero losses")
	for spot_index in 2:
		var spot: Dictionary = _shell.lobby_gadget_rect("spot/%d" % spot_index)
		_check(spot.get("found", false), "map start control %d exists" % spot_index)
		if spot.get("found", false):
			_click_at(Vector2(spot.x + spot.w * 0.5, spot.y + spot.h * 0.5))
			_check(_shell.lobby_state().slots[spot_index].start == spot_index, "map start control %d assigns the next available player" % spot_index)
	await _capture(folder, "skirmish")
	var combos: Array = []
	var profile_combo_found := false
	var map_type_combo_found := false
	for gadget in _shell.list_gadgets():
		if gadget.kind == "combo" and not gadget.hidden and gadget.name in ["MpGameSetup::MapType", "Skirmish::PlayerProfile"]:
			combos.append(gadget)
			if gadget.name == "MpGameSetup::MapType":
				map_type_combo_found = true
				_check(gadget.entries == 2, "map type offers official and player-made maps")
			else:
				profile_combo_found = true
				_check(gadget.entries >= 3, "profile combo offers personas and profile actions")
	_check(map_type_combo_found, "map type combo exists")
	_check(profile_combo_found, "profile combo exists")
	for combo in combos:
		_check(combo.entries != 0 and combo.text != "Entry", str(combo.name) + " has a selected value")
		_click_at(Vector2(combo.x + combo.w - combo.h * 0.5, combo.y + combo.h * 0.5))
		var opened := false
		for gadget in _shell.list_gadgets():
			if gadget.path == combo.path and gadget.kind == "combo":
				opened = gadget.open
		_check(opened, str(combo.name) + " dropdown opens")
		await _capture(folder, "dropdown-" + str(combo.name).validate_filename())
		_click_at(Vector2(1, 1))
	var report: Dictionary = _shell.get_shell_report()
	for error in report.get("errors", []):
		if str(error).begins_with("gadget draw:") and not menu_errors.has(str(error)):
			_check(false, str(error))
	var diagnostics := FileAccess.open(folder.path_join("report.json"), FileAccess.WRITE)
	diagnostics.store_string(JSON.stringify({"shell": report, "preexisting_menu_errors": menu_errors.keys(), "gadgets": _shell.list_gadgets(), "lobby": _shell.lobby_state()}, "\t"))
	_shell.free()
	world.free()
	print("BFME1 SKIRMISH UI ", "PASS" if _failures == 0 else "FAIL", ": ", _failures, " failures; artifacts ", folder)
	quit(0 if _failures == 0 else 1)
