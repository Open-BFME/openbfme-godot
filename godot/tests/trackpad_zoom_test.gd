extends SceneTree
func _initialize() -> void: call_deferred("_run")
func _run() -> void:
	var fs = ClassDB.instantiate("RetailFileSystem")
	var bfme1 := not OS.get_environment("BFME1_INSTALL").is_empty()
	var mounted: Dictionary = fs.mount_bfme1_path(OS.get_environment("BFME1_INSTALL")) if bfme1 else fs.mount_retail()
	assert(mounted.ok, str(mounted))
	var world = ClassDB.instantiate("GameWorld")
	root.add_child(world)
	assert(world.setup(fs).ok)
	assert(world.load_map("map mp fords of isen" if bfme1 else "map mp fall back 4p", {"slots":[{"player":"Player_1","faction":"FactionGondor" if bfme1 else "FactionMen","human":true}]}).ok)
	var camera := Camera3D.new()
	root.add_child(camera)
	var hud = ClassDB.instantiate("InGameHudNode")
	root.add_child(hud)
	var setup: Dictionary = hud.setup(fs, world, camera, "Player_1", {"camera_start":Vector3(1800,0,-1800)})
	if not setup.ok:
		printerr(setup)
		quit(1)
		return
	for i in 3: await process_frame
	var before: float = hud.get_camera().height_above_ground
	var pinch := InputEventMagnifyGesture.new()
	pinch.position = Vector2(640,360)
	pinch.factor = 1.25
	Input.parse_input_event(pinch.duplicate())
	await process_frame
	var closer: float = hud.get_camera().height_above_ground
	assert(closer < before)
	pinch.factor = 0.8
	Input.parse_input_event(pinch.duplicate())
	await process_frame
	assert(is_equal_approx(hud.get_camera().height_above_ground, before))
	var scroll := InputEventPanGesture.new()
	scroll.position = Vector2(640,360)
	var position_before: Vector2 = hud.get_camera().position
	scroll.delta = Vector2(0,-2)
	Input.parse_input_event(scroll.duplicate())
	await process_frame
	assert(hud.get_camera().position.distance_to(position_before) > 0.01)
	assert(is_equal_approx(hud.get_camera().height_above_ground, before))
	position_before = hud.get_camera().position
	scroll.delta = Vector2(4,0)
	Input.parse_input_event(scroll.duplicate())
	await process_frame
	assert(hud.get_camera().position.distance_to(position_before) > 0.01)
	assert(is_equal_approx(hud.get_camera().height_above_ground, before))
	if OS.get_name() == "macOS":
		for direction in [MOUSE_BUTTON_WHEEL_UP, MOUSE_BUTTON_WHEEL_RIGHT]:
			position_before = hud.get_camera().position
			var wheel := InputEventMouseButton.new()
			wheel.position = Vector2(640,360)
			wheel.button_index = direction
			wheel.factor = 0.25
			wheel.pressed = true
			Input.parse_input_event(wheel)
			await process_frame
			assert(hud.get_camera().position.distance_to(position_before) > 0.01)
			assert(is_equal_approx(hud.get_camera().height_above_ground, before))
	hud.set_input_enabled(false)
	pinch.factor = 1.25
	Input.parse_input_event(pinch.duplicate())
	await process_frame
	assert(is_equal_approx(hud.get_camera().height_above_ground, before))
	hud.set_input_enabled(true)
	pinch.factor = 0.0
	Input.parse_input_event(pinch.duplicate())
	await process_frame
	assert(is_equal_approx(hud.get_camera().height_above_ground, before))
	print("TRACKPAD ZOOM PASS")
	quit()
