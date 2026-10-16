extends SceneTree
func _initialize() -> void: call_deferred("_run")
func touch(hud: Node, index: int, down: bool, pos: Vector2) -> void:
	var event := InputEventMouseButton.new()
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
	var owner = world.execute_lua("for id,p in pairs(bfme.players) do if p.name=='Player_1' then return id end end")
	var unit: int = world.create_object("GondorFighter", int(owner.output), 1800, 1800, 0)
	assert(unit > 0)
	world.advance(0.2)
	hud.select_controller_group(PackedInt64Array([unit]))
	for i in 3: await process_frame
	var before: Dictionary = world.get_object(unit)
	assert(not hud.get_selection().is_empty())
	for down in [true,false]:
		var event := InputEventMouseButton.new()
		event.device = -1
		event.button_index = MOUSE_BUTTON_LEFT
		event.pressed = down
		event.position = Vector2(900,200)
		Input.parse_input_event(event)
		world.advance(0.2)
		await process_frame
	for i in 30:
		world.advance(0.2)
		await process_frame
	var after: Dictionary = world.get_object(unit)
	assert(hud.get_state().keys.touch_orders == 1, str(hud.get_state()))
	assert(Vector2(before.x,before.y).distance_to(Vector2(after.x,after.y)) > 10, str(after))
	assert(not hud.get_selection().is_empty())
	print("TOUCH CONTEXT ORDER ROUTING PASS")
	quit()
