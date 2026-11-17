extends SceneTree
## Retail APT setup with a synthetic preview: both editions' presentation, no map/GPU required.
## godot --headless --path godot --script res://tests/wotr_setup_dialog_test.gd
const Preview := preload("res://scripts/wotr_preview.gd")
var failures := 0

class GameStub extends Node:
	var _shell: Node

class LightingStub extends RefCounted:
	func apply_map_lighting() -> void:
		pass

class RegionFxStub extends Node3D:
	func update_gamma() -> void:
		pass
	func clear_regions() -> void:
		pass
	func set_region(_r: int, _fill: Color, _home: Color, _edge: Color) -> void:
		pass

func check(ok: bool, message: String) -> bool:
	print("WOTR SETUP ", "PASS " if ok else "FAIL ", message)
	if not ok:
		failures += 1
	return ok

func tick(shell: Node, preview = null) -> void:
	for i in 120:
		shell.tick(0.033)
		if preview != null:
			preview.tick()

func click(shell: Node, level: int, path: String) -> bool:
	var button: Dictionary = shell.find_button(level, path)
	if not check(button.get("found", false), "button exists: " + path):
		return false
	shell.post_mouse_move_stage(Vector2(button.x, button.y))
	shell.post_mouse_button(true)
	shell.post_mouse_button(false)
	tick(shell)
	return true

func _initialize() -> void:
	run.call_deferred()

func run() -> void:
	if OS.get_environment("ROTWK_INSTALL").is_empty() or OS.get_environment("BFME2_INSTALL").is_empty():
		print("WOTR SETUP SKIP: set ROTWK_INSTALL and BFME2_INSTALL")
		quit(77)
		return
	var fs = ClassDB.instantiate("RetailFileSystem")
	if not check(fs.mount_retail().ok, "mount retail files"):
		quit(1)
		return
	var world = ClassDB.instantiate("GameWorld")
	root.add_child(world)
	if not check(world.setup(fs).ok, "load world"):
		world.free()
		quit(1)
		return
	for edition in ["RotWK", "BFME2"]:
		exercise(fs, world, edition)
	world.free()
	quit(0 if failures == 0 else 1)

func exercise(fs: RefCounted, world: Node, edition: String) -> void:
	var folder := OS.get_temp_dir().path_join("wotr-setup-%s-%d" % [edition, OS.get_process_id()])
	DirAccess.make_dir_recursive_absolute(folder)
	var shell = ClassDB.instantiate("AptMenuPlayer")
	shell.auto_process = false
	root.add_child(shell)
	var config := {"options_file": folder.path_join("Options.ini"), "skirmish_ini": folder.path_join("Skirmish.ini")}
	if edition == "BFME2":
		config.presentation_fs = fs.create_bfme2_shell()
	if not check(shell.boot_shell(fs, world, config).ok, edition + " shell boots"):
		shell.free()
		return
	shell.shell_push("MainMenu.apt")
	tick(shell)
	shell.shell_fscommand("AptMainMenu::WarOfTheRing", "")
	tick(shell)
	shell.lobby_apply({"profile": "Gimli"})
	tick(shell)
	if shell.find_button(shell.shell_top_level(), "lobby.Continue").get("found", false):
		click(shell, shell.shell_top_level(), "lobby.Continue")
	var initial: Dictionary = shell.strategic_setup({})
	if not check(initial.get("found", false) and initial.get("visible", false), edition + " setup map is shown"):
		shell.free()
		return
	# The retail default is historical: territory picks are deliberately disabled.
	check(not initial.selectable.has(true), edition + " historical scenario fixes start regions")
	check(not shell.strategic_setup({"kind": "click", "region": 0}).taken, edition + " historical pick is rejected")
	check(shell.lobby_apply({"scenario": "WOTRScenario001"}).ok, edition + " choose freeform")
	tick(shell)
	var st: Dictionary = shell.strategic_setup({})
	var region: int = st.selectable.find(true)
	if not check(region >= 0, edition + " freeform offers start regions"):
		shell.free()
		return
	# Exercise the actual preview tick, without loading/rendering the retail 3D map.
	var game := GameStub.new()
	game._shell = shell
	var preview := Preview.new()
	preview.init(game)
	root.add_child(preview)
	preview.set_process(false)
	preview.built_campaign = st.region_campaign
	preview.vp = SubViewport.new()
	preview.add_child(preview.vp)
	preview.lw = LightingStub.new()
	preview.region_fx = RegionFxStub.new()
	preview.vp.add_child(preview.region_fx)
	preview.tick()
	check(preview.view.visible, edition + " preview visible before Start")
	preview.pressed_button = MOUSE_BUTTON_LEFT
	# No positions picked: Start must show the retail NeedStartSpots refusal.
	check(shell.shell_fscommand("AptSkirmish::StartGame", ""), edition + " Start command registered")
	tick(shell)
	preview.tick()
	check(shell.get_shell_report().start_refusals == PackedStringArray([": " + shell.fetch_text("GUI:NeedStartSpots").text]), edition + " Start reports missing positions")
	check(not shell.strategic_setup({}).visible, edition + " dialog covers setup map")
	check(not preview.view.visible, edition + " preview yields to dialog")
	check(preview.vp.render_target_update_mode == SubViewport.UPDATE_DISABLED, edition + " covered preview stops drawing")
	check(preview.pressed_button == -1, edition + " covering dialog cancels map press")
	check(not shell.strategic_setup({"kind": "click", "region": region}).taken, edition + " covered map refuses picks")
	check(shell.strategic_setup({}).holders == [], edition + " dialog click cannot assign a territory")
	# Use the actual OK button, then verify the map returns and accepts picks.
	var dismissed := false
	for level in 16:
		if shell.has_level(level) and shell.find_button(level, "MessageBox.ButtonsOk.MessageBoxOk").get("found", false):
			dismissed = click(shell, level, "MessageBox.ButtonsOk.MessageBoxOk")
			break
	check(dismissed, edition + " dialog dismissed through its OK button")
	tick(shell, preview)
	check(preview.view.visible, edition + " closing dialog restores preview")
	check(preview.vp.render_target_update_mode == SubViewport.UPDATE_ALWAYS, edition + " restored preview resumes drawing")
	check(shell.strategic_setup({"kind": "click", "region": region}).taken, edition + " freeform pick accepted after dialog closes")
	check(shell.strategic_setup({}).holders == [{"region": region, "slot": 0, "color": -1}], edition + " chosen region assigned to local player")
	preview.free()
	game.free()
	shell.free()
