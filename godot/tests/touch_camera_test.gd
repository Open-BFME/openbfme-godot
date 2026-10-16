extends SceneTree
func _initialize() -> void: call_deferred("_run")
func touch(hud: Node, index: int, down: bool, pos: Vector2) -> void:
	var event := InputEventScreenTouch.new()
	event.index = index
	event.pressed = down
	event.position = pos
	Input.parse_input_event(event)
func _run() -> void:
	root.size = Vector2i(1280,720)
	var fs = ClassDB.instantiate("RetailFileSystem")
	assert(fs.mount_retail().ok)
	var world = ClassDB.instantiate("GameWorld")
	root.add_child(world)
	assert(world.setup(fs).ok)
	assert(world.load_map("map mp fall back 4p", {"slots":[{"player":"Player_1","faction":"FactionMen","human":true}]}).ok)
	var camera := Camera3D.new()
	root.add_child(camera)
	var hud = ClassDB.instantiate("InGameHudNode")
	root.add_child(hud)
	assert(hud.setup(fs,world,camera,"Player_1",{"camera_start":Vector3(1800,0,-1800)}).ok)
	for i in 3: await process_frame
	var before: Vector2 = hud.get_camera().position
	touch(hud,0,true,Vector2(500,300))
	var mouse := InputEventMouseButton.new()
	mouse.device = -1
	mouse.button_index = MOUSE_BUTTON_LEFT
	mouse.pressed = true
	mouse.position = Vector2(500,300)
	Input.parse_input_event(mouse)
	touch(hud,1,true,Vector2(600,300))
	for index in [0,1]:
		var drag := InputEventScreenDrag.new()
		drag.index = index
		drag.position = Vector2(540 + index*100,330)
		Input.parse_input_event(drag)
	for i in 3: await process_frame
	assert(hud.get_camera().position.distance_to(before) > 0.1)
	assert(not hud.get_state().drag_box.selecting)
	touch(hud,0,false,Vector2(540,330))
	touch(hud,1,false,Vector2(640,330))
	mouse.pressed = false
	Input.parse_input_event(mouse)
	for i in 3: await process_frame
	assert(not hud.get_state().drag_box.selecting)
	print("TWO FINGER CAMERA PAN PASS")
	quit()
