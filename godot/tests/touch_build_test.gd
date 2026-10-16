extends SceneTree
## Native building button queue clocks, including the ClockOverlay texture path.
func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	root.size = Vector2i(1280, 720)
	var fs = ClassDB.instantiate("RetailFileSystem")
	assert(fs.mount_retail().ok)
	var world = ClassDB.instantiate("GameWorld")
	root.add_child(world)
	assert(world.setup(fs).ok)
	assert(world.load_map("map mp fall back 4p", {"slots": [
		{"player":"Player_1", "faction":"FactionMen", "human":true},
		{"player":"Player_2", "faction":"FactionMordor", "human":false}]}).ok)
	var owner: Dictionary = world.execute_lua("for id,p in pairs(bfme.players) do if p.name=='Player_1' then p.money=100000; return id end end")
	assert(owner.ok, str(owner))
	world.update_lua()
	var camera := Camera3D.new()
	root.add_child(camera)
	var hud = ClassDB.instantiate("InGameHudNode")
	root.add_child(hud)
	assert(hud.setup(fs, world, camera, "Player_1", {"camera_start":Vector3(1800,0,-1800),"prewarm_buildings":true}).ok)
	var warmed_models: int = hud.get_placement_ghost_state().model_loads
	assert(warmed_models > 0)
	var builder: int = world.create_object("MenPorter", int(owner.output), 1800, 1800, 0)
	assert(builder > 0)
	world.advance(0.2)
	hud.select_controller_group(PackedInt64Array([builder]))
	for i in 4: await process_frame
	var command := {}
	for entry in hud.get_command_buttons():
		if entry.command == "DOZER_CONSTRUCT": command = entry; break
	assert(not command.is_empty())
	assert(hud.activate_command(command.slot, command.in_palantir, false))
	Engine.set_meta("touch_build_controls", true)
	var finger := InputEventScreenTouch.new()
	finger.index = 0
	finger.position = Vector2(600,360)
	finger.pressed = true
	Input.parse_input_event(finger.duplicate())
	await process_frame
	var drag := InputEventScreenDrag.new()
	drag.index = 0
	drag.position = Vector2(640,360)
	Input.parse_input_event(drag.duplicate())
	await process_frame
	finger.position = drag.position
	finger.pressed = false
	Input.parse_input_event(finger.duplicate())
	await process_frame
	for i in 3: await process_frame
	assert(hud.get_placement().anchored and not hud.get_placement().touch_rotating)
	var positioned: Dictionary = hud.get_placement()
	finger.position = Vector2(900,450)
	finger.pressed = true
	Input.parse_input_event(finger.duplicate())
	await process_frame
	finger.pressed = false
	Input.parse_input_event(finger.duplicate())
	await process_frame
	for i in 3: await process_frame
	assert(hud.get_placement().touch_rotating)
	assert(is_equal_approx(positioned.x, hud.get_placement().x) and is_equal_approx(positioned.y, hud.get_placement().y))
	assert(hud.touch_build("position", Vector2(640,360)))
	var initial: Dictionary = hud.get_placement()
	assert(initial.placing and initial.anchored)
	assert(not hud.get_placement().touch_rotating)
	assert(hud.touch_build("lock", Vector2.ZERO))
	assert(hud.get_placement().touch_rotating)
	assert(hud.touch_build("rotate", Vector2.RIGHT))
	var rotated: Dictionary = hud.get_placement()
	assert(is_equal_approx(initial.x,rotated.x) and is_equal_approx(initial.y,rotated.y))
	var model_loads: int = hud.get_placement_ghost_state().model_loads
	assert(model_loads == warmed_models, "first placement must use preloaded models")
	assert(hud.touch_build("cancel", Vector2.ZERO))
	for i in 3: await process_frame
	assert(not hud.get_placement().placing)
	hud.select_controller_group(PackedInt64Array([builder]))
	for i in 4: await process_frame
	assert(hud.activate_command(command.slot, command.in_palantir, false))
	assert(hud.touch_build("position", Vector2(640,360)))
	for i in 4: await process_frame
	assert(hud.get_placement_ghost_state().model_loads == model_loads, "reopening placement should reuse loaded preview models")
	print("TOUCH BUILD POSITION ROTATE CANCEL PASS")
	quit()
