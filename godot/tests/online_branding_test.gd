## Real OnlineShell draw regression. Needs retail data and a window/GPU (not --headless).
## godot --path godot --script res://tests/online_branding_test.gd -- --captures=/absolute/external/folder
## Captures are optional and must remain outside the repository. No lobby requests are sent.
## Optional --open-website clicks the real seal hit target once and checks the recorded request (never launches a browser).
extends SceneTree

const ExternalOpen := preload("res://scripts/external_open.gd")

var failures := 0


func _check(ok: bool, message: String) -> void:
	print("  ok   " if ok else "  FAIL ", message)
	if not ok:
		failures += 1


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	if DisplayServer.get_name() == "headless":
		print("ONLINE BRANDING SKIP: needs a window and GPU")
		quit(77)
		return
	if not ClassDB.class_exists("AptMenuPlayer"):
		push_error("OpenBFME extension is not loaded")
		quit(1)
		return
	var captures := ""
	var open_website := "--open-website" in OS.get_cmdline_user_args()
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--captures="):
			captures = arg.trim_prefix("--captures=")
	var fs: RefCounted = ClassDB.instantiate("RetailFileSystem")
	var mounted: Dictionary = fs.mount_retail()
	if not mounted.ok:
		print("ONLINE BRANDING SKIP: retail data unavailable: ", mounted.errors)
		quit(77)
		return
	var world: Node = ClassDB.instantiate("GameWorld")
	root.add_child(world)
	var setup: Dictionary = world.setup(fs)
	_check(setup.ok, "object world setup")
	if not setup.ok:
		print(setup.errors)
		quit(1)
		return
	for size in [Vector2i(1024, 768), Vector2i(2560, 1440)]:
		var viewport := SubViewport.new()
		viewport.size = size
		viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS
		root.add_child(viewport)
		var player: Node2D = ClassDB.instantiate("AptMenuPlayer")
		player.auto_process = false
		viewport.add_child(player)
		var boot: Dictionary = player.boot_shell(fs, world, {"seed": 1})
		_check(boot.ok, "shell boot %s" % size)
		if not boot.ok:
			print(boot.errors)
			quit(1)
			return
		var online: Dictionary = player.set_online(world, {"lobby_url": "https://play.openbfme.com"})
		_check(online.ok, "online context")
		_check(player.shell_push("OnlineShell.apt"), "OnlineShell opened")
		for frame in 150:
			player.tick(0.034)
		player.render(true)
		var seals := 0
		for op in player.describe_ops():
			if "OnlineLogin.GameSpy.GameSpy" in op:
				print(op)
				_check("tex=brand:onlinelogin:118 " in op, "the seal draw uses OpenBFME, never the retail atlas")
				seals += 1
		_check(seals == 1, "exactly one replacement seal draw at %s" % size)
		var tooltip: Dictionary = player.fetch_text("TOOLTIP:OnlineShell/OnlineLogin/GamespyShortcut")
		_check(tooltip.get("found", false) and tooltip.get("text", "") == "OpenBFME", "OpenBFME tooltip")
		await process_frame
		await process_frame
		await RenderingServer.frame_post_draw
		var image := viewport.get_texture().get_image()
		_check(image.get_size() == size, "exact render target dimensions")
		if not captures.is_empty():
			var path := captures.path_join("online-%dx%d.png" % [size.x, size.y])
			_check(image.save_png(path) == OK, "saved " + path)
		if open_website:
			ExternalOpen.requests.clear()
			open_website = false
			var opened: Array = []
			player.shell_service.connect(func(kind: String, url: String, result: String) -> void:
				if kind == "project_website":
					opened.append([url, result])
			)
			var button: Dictionary = player.find_button(2, "OnlineLogin.GameSpy")
			_check(button.get("found", false), "seal has a real hit target")
			if button.get("found", false):
				player.post_mouse_move_stage(Vector2(button.x, button.y))
				player.tick(0.034)
				player.post_mouse_button(true)
				player.tick(0.034)
				player.post_mouse_button(false)
				player.tick(0.034)
				_check(opened == [["https://openbfme.com", "0"]], "seal click reports a successful project URL request")
				_check(ExternalOpen.requests == [{"kind": "url", "target": "https://openbfme.com"}], "seal click records the fixed URL without opening a browser")
		viewport.queue_free()
		await process_frame
	print("ONLINE BRANDING ", "PASS" if failures == 0 else "FAIL")
	quit(0 if failures == 0 else 1)
