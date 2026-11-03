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

# Retail award clips must bind real images and use their locked grayscale mode.
func _initialize() -> void: call_deferred("_run")
func _run() -> void:
	if OS.get_environment("ROTWK_INSTALL").is_empty() or OS.get_environment("BFME2_INSTALL").is_empty() or OS.get_environment("CAH_TEST_PROFILE").is_empty():
		print("SKIP: set ROTWK_INSTALL, BFME2_INSTALL and CAH_TEST_PROFILE to an empty temporary profile")
		quit(77)
		return
	root.size = Vector2i(1024, 768)
	var fs = ClassDB.instantiate("RetailFileSystem")
	if not _check(fs.mount_retail().ok):
		_finish("")
		return
	var world = ClassDB.instantiate("GameWorld")
	root.add_child(world)
	if not _check(world.setup(fs).ok):
		_finish("")
		return
	var shell = ClassDB.instantiate("AptMenuPlayer")
	root.add_child(shell)
	if not _check(shell.boot_shell(fs, world, {}).ok):
		_finish("")
		return
	if not _check(shell.set_create_a_hero(world, OS.get_environment("CAH_TEST_PROFILE")).ok):
		_finish("")
		return
	shell.set_auto_process(false)
	_check(shell.shell_push("CreateAHero.apt"))
	for i in 80: shell.tick(0.03)
	var level: int = shell.shell_top_level()
	if not _check(shell.shell_invoke(level, "ShowScreen", ["Manager"]).ok):
		_finish("")
		return
	for i in 80: shell.tick(0.03)
	var tab := "Main.mcManagerScreen.Screen.TabNav"
	var button: Dictionary = shell.find_button(level, tab + ".Tab_Nav_Button.UnselectedTabButtons.Awards")
	if not _check(button.found, str(button)):
		_finish("")
		return
	shell.post_mouse_move_stage(Vector2(button.x, button.y))
	shell.post_mouse_button(true)
	shell.post_mouse_button(false)
	for i in 80: shell.tick(0.03)
	var image_path := tab + ".ScreenAwards.AwardList.mcAwards.0.Image"
	_check(shell.get_member(level, image_path, "_imageMap").value == "CahAward:Image_0")
	_check(shell.get_member(level, image_path, "_mode").value == "GRAYSCALE")
	shell.render(true)
	if DisplayServer.get_name() != "headless":
		await RenderingServer.frame_post_draw
		for arg in OS.get_cmdline_user_args():
			if arg.begins_with("--shot="): root.get_texture().get_image().save_png(arg.trim_prefix("--shot="))
	_finish("CAH AWARDS DISPLAY PASS")
