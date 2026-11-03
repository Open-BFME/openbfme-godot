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

## Invalid building sites enter the localized, alternating player message log.
func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	if OS.get_environment("ROTWK_INSTALL").is_empty() or OS.get_environment("BFME2_INSTALL").is_empty():
		print("PLAYER MESSAGE LOG SKIP: retail installations required")
		quit(77)
		return
	root.size = Vector2i(1280, 720)
	var fs = ClassDB.instantiate("RetailFileSystem")
	if not _check(fs.mount_retail().ok):
		_finish("")
		return
	var world = ClassDB.instantiate("GameWorld")
	root.add_child(world)
	if not _check(world.setup(fs).ok):
		_finish("")
		return
	_check(world.load_map("map mp fall back 4p", {"slots": [
		{"player":"Player_1", "faction":"FactionMen", "human":true},
		{"player":"Player_2", "faction":"FactionMordor", "human":false}]}).ok)
	var owner: Dictionary = world.execute_lua("for id,p in pairs(bfme.players) do if p.name=='Player_1' then p.money=100000; return id end end")
	if not _check(owner.ok, str(owner)):
		_finish("")
		return
	world.update_lua()
	var camera := Camera3D.new()
	root.add_child(camera)
	var hud = ClassDB.instantiate("InGameHudNode")
	root.add_child(hud)
	if not _check(hud.setup(fs, world, camera, "Player_1", {"camera_start":Vector3(1800,0,-1800)}).ok):
		_finish("")
		return
	_check("[S-6101] player message log: BFME2 recovered six-entry message, positioning and fade routines ported; independent RotWK display-routine verification remains outstanding" in hud.get_report().stops)
	hud.set_edge_scroll(false)
	var builder: int = world.create_object("MenPorter", int(owner.output), 1800, 1800, 0)
	if not _check(world.get_object(builder).ok):
		_finish("")
		return
	world.advance(0.2)
	_check(hud.select_object_with_message(builder))
	for i in 4: await process_frame
	var command := {}
	for entry in hud.get_command_buttons():
		if entry.command == "DOZER_CONSTRUCT": command = entry; break
	if not _check(not command.is_empty(), "no DOZER_CONSTRUCT command button"):
		_finish("")
		return
	_check(hud.press_command_button(command.template))
	create_timer(60).timeout.connect(func(): printerr("PLAYER MESSAGE LOG TIMEOUT"); quit(1))
	hud.inject_mouse_move(Vector2(640,300))
	await create_timer(0.04).timeout
	var site: Dictionary = hud.get_placement()
	var obstacle: int = world.create_object("GondorFarm", int(owner.output), site.x, site.y, 0)
	if not _check(world.get_object(obstacle).ok):
		_finish("")
		return
	world.advance(0.2)
	var rejected := false
	for y in [300, 100, 200, 400, 500]:
		for x in [640, 0, 100, 400, 800, 1200]:
			hud.inject_mouse_move(Vector2(x,y))
			await create_timer(0.04).timeout
			if hud.get_placement().legal != 0:
				hud.inject_mouse_button(1, true, Vector2(x,y), false)
				hud.inject_mouse_button(1, false, Vector2(x,y), false)
				await process_frame
				var first: Array = hud.get_state().player_messages
				if first.is_empty(): continue
				_check(not first[0].text.begins_with("GUI:"))
				_check(not first[0].text.begins_with("MISSING:"))
				hud.inject_mouse_move(Vector2(x,y))
				await create_timer(0.04).timeout
				hud.inject_mouse_button(1, true, Vector2(x,y), false)
				hud.inject_mouse_button(1, false, Vector2(x,y), false)
				await process_frame
				var next: Array = hud.get_state().player_messages
				_check(next.size() == 2, str(next))
				_check(next[0].text == next[1].text)
				_check(next[0].color != next[1].color)
				rejected = true
				break
		if rejected: break
	_check(rejected, "fixture needs an invalid site which emits placement feedback")
	await _check_message_layout(hud)
	if DisplayServer.get_name() != "headless" and not OS.get_environment("MESSAGE_LOG_SHOT").is_empty():
		await RenderingServer.frame_post_draw
		_check(root.get_texture().get_image().save_png(OS.get_environment("MESSAGE_LOG_SHOT")) == OK)
	_finish("PLAYER MESSAGE LOG PASS")

func _check_message_layout(hud) -> void:
	var release := root.get_node("Release")
	var watermark: CanvasLayer = release.get_node("ReleaseWatermark")
	var original_scale := watermark.scale
	var original_size := root.size
	var update_note: Label = watermark.get_node("UpdateNote")
	var original_note := update_note.text
	var original_note_visible := update_note.visible
	update_note.text = "Layout test\nSecond line\nThird line"
	update_note.show()
	var obstacles := get_nodes_in_group("player_message_obstacles")
	_check(obstacles.size() == 3)
	for resolution in [Vector2i(800, 600), Vector2i(1024, 768), Vector2i(1280, 720), Vector2i(1920, 1080), Vector2i(3840, 2160)]:
		root.size = resolution
		for ui_scale in [0.75, 1.0, 1.5, 2.0]:
			watermark.scale = Vector2(ui_scale, ui_scale)
			watermark.show()
			for i in 3: await process_frame
			var top: float = hud.get_hud6_state().message_top
			for control: Control in obstacles:
				if control.is_visible_in_tree():
					var rect: Rect2 = control.get_global_transform_with_canvas() * Rect2(Vector2.ZERO, control.size)
					_check(top >= ceilf(rect.end.y + 2.0 * ui_scale), "message log overlaps release corner") # tolerance: disjoint vertical extents, including scaled shadow padding.
	watermark.scale = original_scale
	root.size = original_size
	watermark.hide()
	for i in 3: await process_frame
	var hidden_top: float = hud.get_hud6_state().message_top
	watermark.show()
	for i in 3: await process_frame
	_check(hud.get_hud6_state().message_top > hidden_top) # tolerance: hiding the corner must release its reserved vertical space.
	update_note.text = original_note
	update_note.visible = original_note_visible
