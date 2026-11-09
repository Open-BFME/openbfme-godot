## Settings entry and footer regression across BFME1, BFME2 presentation and RotWK.
## Requires the corresponding *_INSTALL paths; writes only an isolated temporary profile.
extends SceneTree

const Screen := preload("res://scripts/openbfme_options_screen.gd")
const Settings := preload("res://scripts/display_settings.gd")
const Options := preload("res://scripts/game_options.gd")
var failures := 0
var cases := 0
var completed := 0


func _initialize() -> void:
	_run.call_deferred()


func check(ok: bool, message: String) -> bool:
	print("OPTIONS OPEN ", "PASS " if ok else "FAIL ", message)
	if not ok:
		failures += 1
	return ok


func advance(shell: Node, screen: CanvasLayer) -> void:
	for frame in 100:
		shell.tick(0.033)
		screen._process(0.0)
	shell.render(true)


func click(shell: Node, screen: CanvasLayer, point: Vector2) -> void:
	shell.post_mouse_move_stage(point)
	shell.post_mouse_button(true)
	shell.post_mouse_button(false)
	advance(shell, screen)


func _run() -> void:
	create_timer(180.0).timeout.connect(func(): quit(1))
	root.size = Vector2i(1280, 720)
	var first := OS.get_environment("BFME1_INSTALL")
	if not first.is_empty():
		var fs = ClassDB.instantiate("RetailFileSystem")
		if check(fs.mount_bfme1_path(first).ok, "BFME1 mount"):
			await exercise(fs, null, "BFME1")
	else:
		print("OPTIONS OPEN SKIP BFME1: set BFME1_INSTALL")
	if not OS.get_environment("ROTWK_INSTALL").is_empty() and not OS.get_environment("BFME2_INSTALL").is_empty():
		var fs = ClassDB.instantiate("RetailFileSystem")
		if check(fs.mount_retail().ok, "RotWK mount"):
			await exercise(fs, null, "RotWK")
			await exercise(fs, fs.create_bfme2_shell(), "BFME2")
	else:
		print("OPTIONS OPEN SKIP RotWK/BFME2: set ROTWK_INSTALL and BFME2_INSTALL")
	print("OPTIONS OPEN RESULT: %d cases, %d failures" % [cases, failures])
	quit(1 if failures or completed != cases else (77 if cases == 0 else 0))


func exercise(fs: RefCounted, presentation, edition: String) -> void:
	cases += 1
	var folder := OS.get_temp_dir().path_join("openbfme-options-%s-%d" % [edition, OS.get_process_id()])
	DirAccess.make_dir_recursive_absolute(folder)
	var path := folder.path_join("Options.ini")
	var file := FileAccess.open(path, FileAccess.WRITE)
	file.store_string("Brightness = 50\nStaticGameLOD = High\n")
	file.close()
	var world = ClassDB.instantiate("GameWorld")
	root.add_child(world)
	if not check(world.setup(fs).ok, edition + " world"):
		world.free()
		return
	var audio = ClassDB.instantiate("GameAudio")
	root.add_child(audio)
	if not check(audio.boot(fs, {}).ok, edition + " audio"):
		audio.free()
		world.free()
		return
	var shell = ClassDB.instantiate("AptMenuPlayer")
	shell.auto_process = false
	root.add_child(shell)
	var config := {"options_file": path, "skirmish_ini": folder.path_join("Skirmish.ini"), "default_volumes": PackedFloat32Array([0.7, 0.7, 0.7, 0.7, 0.7]), "display_modes": [Vector2i(1024, 768)], "current_resolution": Vector2i(1024, 768)}
	if presentation != null:
		config.presentation_fs = presentation
	if not check(shell.boot_shell(fs, world, config).ok, edition + " shell"):
		shell.free()
		audio.free()
		world.free()
		return
	var options := Options.new()
	options.shell = shell
	root.add_child(options)
	var screen := Screen.new()
	screen.shell = shell
	screen.settings = Settings.new()
	screen.settings_path = folder.path_join("display.cfg")
	screen.game_options = options
	screen.audio_getter = func() -> Node: return audio
	root.add_child(screen)
	screen.set_process(false)
	shell.shell_push("MainMenu.apt")
	advance(shell, screen)
	var untouched := FileAccess.get_file_as_string(path)
	for attempt in 3:
		var exit_button: String = ["cancel", "accept", "cancel"][attempt]
		check(shell.shell_fscommand("AptMainMenu::Options", "false"), edition + " settings command")
		advance(shell, screen)
		if attempt == 0 and OS.get_environment("OPTIONS_OPEN_SHOTS") != "" and DisplayServer.get_name() != "headless":
			await process_frame
			await RenderingServer.frame_post_draw
			root.get_texture().get_image().save_png(OS.get_environment("OPTIONS_OPEN_SHOTS").path_join(edition + "-settings.png"))
		if not check(screen.is_open(), edition + " settings page opens"):
			print(shell.get_shell_report())
			break
		check(shell.shell_stack() == PackedStringArray(["MainMenu.apt", "Options.apt"]), edition + " options covers main menu")
		check(screen._rows.get("Retail.Brightness", {}).get("value", -1) == [50, 50, 65][attempt], edition + " initial Display tab has the saved brightness without changing tabs")
		var state: Dictionary = shell.openbfme_page_state()
		check(state.buttons.size() == 3, edition + " all footer buttons exist")
		var retail: Dictionary = shell.options_retail_rows()
		if edition == "BFME1":
			check(retail["Retail.Detail"].label == shell.fetch_text("APT:Detail").text, "BFME1 detail label uses its own string")
			check(retail["Retail.AdvancedOption0"].kind == "text" and not retail["Retail.AdvancedOption0"].enabled, "BFME1 custom graphics are explicitly unavailable")
			check(retail["Retail.AdvancedOption0"].help == "[S-5017] BFME1 custom graphics controls are unverified; BFME2/RotWK's nine-option layout does not apply.", "BFME1 custom graphics stop is pinned")
			check(not retail.has("Retail.AdvancedOption1"), "BFME1 does not offer RotWK custom graphics rows")
		for gadget in shell.list_gadgets():
			if str(gadget.name).begins_with("Options::"):
				check(gadget.hidden, edition + " retail control hidden: " + str(gadget.name))
		for tab in screen.TABS:
			screen._show_tab(tab)
			shell.render(true)
			check(screen.current_tab() == tab and shell.openbfme_page_state().open, edition + " " + tab + " tab")
		screen._show_tab("Display")
		var brightness := 20 if attempt == 2 else 65
		check(shell.openbfme_page_choose("Retail.Brightness", brightness).ok, edition + " brightness edited")
		advance(shell, screen)
		check(shell.options_retail_rows()["Retail.Brightness"].value == brightness, edition + " brightness preview")
		var rect: Rect2 = shell.openbfme_page_state().buttons.get(exit_button, Rect2())
		if not check(rect.has_area(), edition + " " + exit_button + " hit area"):
			break
		click(shell, screen, rect.get_center())
		check(not screen.is_open() and shell.shell_stack() == PackedStringArray(["MainMenu.apt"]), edition + " " + exit_button + " returns to main menu")
		if exit_button == "cancel":
			check(FileAccess.get_file_as_string(path) == untouched, edition + " cancel preserves Options.ini")
		else:
			check(shell.get_option("Brightness") == "65", edition + " accept saves brightness")
			untouched = FileAccess.get_file_as_string(path)
	for line in shell.get_shell_report().note_lines:
		check(not str(line).contains("cannot open") and not str(line).contains("[S-3403]"), edition + " no options opening/layout failure: " + str(line))
	check(shell.get_shell_report().errors.is_empty(), edition + " no shell rendering errors: " + str(shell.get_shell_report().errors))
	completed += 1
	screen.free()
	options.free()
	shell.free()
	audio.free()
	world.free()
