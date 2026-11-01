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

# Selected-unit ground powers use the same textured terrain decal as spell-book powers.
# Runs against the explicitly mounted profile, including enabled mods.
func _initialize() -> void: call_deferred("_run")
func _run() -> void:
	if OS.get_environment("ROTWK_INSTALL").is_empty() or OS.get_environment("BFME2_INSTALL").is_empty():
		print("SKIP: set ROTWK_INSTALL and BFME2_INSTALL")
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
	var loaded: Dictionary = world.load_map("map mp fall back 4p", {"terrain": true, "slots": [{"player": "Player_1", "faction": "FactionMen", "human": true}]})
	if not _check(loaded.ok, str(loaded)):
		_finish("")
		return
	var owner := -1
	for i in loaded.players.size():
		if String(loaded.players[i]).contains("'Player_1'"): owner = i
	_check(owner != -1)
	var hero = world.create_object("ElvenElrond", owner, 2000, 1800, 0)
	# tolerance: live object IDs are allocated dynamically; zero is the invalid ID.
	_check(hero != 0)
	world.gain_hero_levels(hero, 10)
	world.ready_object_powers(hero)
	var camera := Camera3D.new()
	root.add_child(camera)
	var hud = ClassDB.instantiate("InGameHudNode")
	root.add_child(hud)
	if not _check(hud.setup(fs, world, camera, "Player_1", {"camera_start": Vector3(2000, 0, -1800)}).ok):
		_finish("")
		return
	world.set_shroud_drawn(false)
	hud.select_object_with_message(hero)
	for i in 20:
		world.advance(1.0 / 30.0)
		await process_frame
	_check(hud.press_command_button("Command_SpecialPowerSpawnWhirlwind"), str(hud.get_command_buttons()))
	var cursor: Dictionary = {}
	var pixel := Vector2.ZERO
	var aim := Vector2.ZERO
	for target in [Vector2(2050, 1800), Vector2(1950, 1800), Vector2(2000, 1850), Vector2(2000, 1750)]:
		var point: Vector2 = hud.world_to_pixel(target)
		hud.inject_mouse_move(point)
		for i in 4: await process_frame
		cursor = hud.get_spellbook_state().radius_cursor
		if cursor.visible and cursor.valid:
			pixel = point
			aim = target
			break
	if not _check(pixel != Vector2.ZERO, str(cursor)):
		_finish("")
		return
	_check(cursor.type == "NaturesWrathRadiusCursor", str(cursor))
	_check(cursor.texture == "SCCNaturesWrath", str(cursor))
	_check(cursor.size == 140.0, str(cursor))
	_check(cursor.rgb == 0xFFFFFF, str(cursor))
	_check(cursor.drawn and cursor.error.is_empty(), str(cursor))
	_check(cursor.pointer == "None", str(cursor))
	var hidden: Image
	var bounds := Rect2()
	if DisplayServer.get_name() != "headless":
		# Freeze the clocks and scene so the pixel comparison isolates the overlay.
		Engine.time_scale = 0.0
		hud.set_process(false)
		world.set_process(false)
		await RenderingServer.frame_post_draw
		var shown := root.get_texture().get_image()
		for arg in OS.get_cmdline_user_args():
			if arg.begins_with("--shot="): shown.save_png(arg.trim_prefix("--shot="))
		RenderingServer.global_shader_parameter_set("radius_cursor_transform", Vector4.ZERO)
		await RenderingServer.frame_post_draw
		hidden = root.get_texture().get_image()
		bounds = Rect2(pixel, Vector2.ZERO)
		for offset in [Vector2(-70, -70), Vector2(-70, 70), Vector2(70, -70), Vector2(70, 70)]:
			bounds = bounds.expand(hud.world_to_pixel(aim + offset))
		bounds = bounds.intersection(Rect2(Vector2.ZERO, Vector2(root.size)))
		var brighter_pixels := 0
		for y in range(int(floor(bounds.position.y)), int(ceil(bounds.end.y))):
			for x in range(int(floor(bounds.position.x)), int(ceil(bounds.end.x))):
				var before := shown.get_pixel(x, y)
				var after := hidden.get_pixel(x, y)
				if before.get_luminance() - after.get_luminance() > 0.02: brighter_pixels += 1
		# tolerance: a visible white overlay must brighten pixels; counts vary with GPU sampling and antialiasing.
		_check(brighter_pixels > 0, "cursor state was active but no white texture reached the terrain pixels")
		# Restore the texture with the frozen scene before exercising cancellation.
		hud.set_process(true)
		await process_frame
		await process_frame
		hud.set_process(false)
		await RenderingServer.frame_post_draw
		_check(root.get_texture().get_image().get_data() == shown.get_data())

	hud.inject_mouse_button(2, true, pixel, false)
	hud.inject_mouse_button(2, false, pixel, false)
	# The HUD publishes cancellation while the scene and elapsed time stay frozen.
	hud.set_process(true)
	for i in 4: await process_frame
	_check(not hud.get_spellbook_state().radius_cursor.visible)
	if hidden != null:
		await RenderingServer.frame_post_draw
		var cancelled := root.get_texture().get_image()
		# The scene stays frozen: cancellation must remove exactly the same pixels
		# as disabling the shader transform, including a stale terrain texture.
		_check(cancelled.get_region(Rect2i(bounds)).get_data() == hidden.get_region(Rect2i(bounds)).get_data(), "cancel left cursor pixels on the terrain")
	_finish("HERO RADIUS CURSOR PASS: white SCCNaturesWrath terrain decal, diameter 140; cancel clears it")
