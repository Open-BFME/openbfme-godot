extends SceneTree
func _initialize() -> void: call_deferred("_run")
func _run() -> void:
	root.size = Vector2i(1280,720)
	var fs = ClassDB.instantiate("RetailFileSystem")
	assert(fs.mount_retail().ok)
	var world = ClassDB.instantiate("GameWorld")
	root.add_child(world)
	assert(world.setup(fs).ok)
	assert(world.load_map("map mp fall back 4p",{"slots":[{"player":"Player_1","faction":"FactionMen","human":true}]}).ok)
	var player = world.execute_lua("for id,p in pairs(bfme.players) do if p.name=='Player_1' then return id end end")
	assert(player.ok)
	var units := PackedInt64Array()
	for i in 4: units.append(world.create_object("MenPorter",int(player.output),1800+i*40,1800,0))
	var camera = Camera3D.new()
	root.add_child(camera)
	var hud = ClassDB.instantiate("InGameHudNode")
	root.add_child(hud)
	assert(hud.setup(fs,world,camera,"Player_1",{"camera_start":Vector3(2100,0,-2000),"alternate_mouse":not ("--standard" in OS.get_cmdline_user_args())}).ok)
	hud.select_controller_group(units)
	world.advance(0.2)
	for i in 4: await process_frame
	var start: Vector2 = hud.world_to_pixel(Vector2(2100,2100))
	hud.inject_mouse_move(start)
	hud.inject_mouse_button(MOUSE_BUTTON_LEFT,true,start)
	hud.inject_mouse_button(MOUSE_BUTTON_RIGHT,true,start)
	for i in 3: await process_frame
	var preview: Dictionary = hud.get_placement_ghost_state()
	assert(preview.formation_visible and preview.formation_slots.size()==4,str(preview))
	var narrow: Array = preview.formation_slots
	var end: Vector2 = hud.world_to_pixel(Vector2(2400,2100))
	hud.inject_mouse_move(end)
	for i in 3: await process_frame
	preview = hud.get_placement_ghost_state()
	assert(preview.formation_visible and preview.formation_slots.size()==4,str(preview))
	assert(preview.formation_slots != narrow,"Drag did not change formation width")
	assert(hud.get_selection().size()==4,"Formation changed selection")
	var slots: Array = preview.formation_slots
	for i in 4:
		var unit: Dictionary = world.get_object(units[i])
		assert(abs(unit.x-(1800+i*40))<2,"Preview issued an early order")
	hud.inject_mouse_button(MOUSE_BUTTON_LEFT,false,end)
	hud.inject_mouse_button(MOUSE_BUTTON_RIGHT,false,end)
	for i in 3: await process_frame
	assert(not hud.get_placement_ghost_state().formation_visible)
	assert(hud.get_selection().size()==4,"Releasing the chord deselected units")
	for i in 50: world.advance(0.2)
	for slot in slots:
		var unit: Dictionary = world.get_object(slot.id)
		assert(unit.ok,str([unit,slot]))
		assert(Vector2(unit.x,unit.y).distance_to(Vector2(slot.x,slot.y))<45,str([unit,slot]))
		assert(abs(angle_difference(unit.angle,slot.angle))<0.2,str([unit,slot]))
	# Reverse press order also previews; Escape cancels without deploying.
	hud.inject_mouse_button(MOUSE_BUTTON_RIGHT,true,start)
	hud.inject_mouse_button(MOUSE_BUTTON_LEFT,true,start)
	for i in 3: await process_frame
	assert(hud.get_placement_ghost_state().formation_visible)
	hud.inject_key(0x01,true)
	hud.inject_key(0x01,false)
	for i in 3: await process_frame
	assert(not hud.get_placement_ghost_state().formation_visible)
	hud.inject_mouse_button(MOUSE_BUTTON_LEFT,false,start)
	hud.inject_mouse_button(MOUSE_BUTTON_RIGHT,false,start)
	var controller = preload("res://scripts/controller_input.gd").new()
	controller.gameplay = func() -> bool: return true
	controller.formation = func(operation: String, direction: Vector2) -> bool: return hud.controller_formation(operation,direction)
	controller.area_selection = func(radius: float, commit: bool) -> bool: return hud.controller_select_area(radius,commit)
	controller.command_action.connect(func(action: String): assert(action != "deselect","Chord deselected units"))
	root.add_child(controller)
	controller.set_process(false)
	for first in [JOY_BUTTON_A, JOY_BUTTON_B]:
		var second = JOY_BUTTON_B if first==JOY_BUTTON_A else JOY_BUTTON_A
		var event := InputEventJoypadButton.new()
		event.device=0
		event.pressed=true
		event.button_index=first
		controller._input(event)
		event.button_index=second
		controller._input(event)
		assert(controller._formation_held)
		controller._pan_stick(Vector2(0.8,0.6),0.1)
		for i in 3: await process_frame
		preview=hud.get_placement_ghost_state()
		assert(preview.formation_visible and preview.formation_slots.size()==4,str(preview))
		var aimed: Array = preview.formation_slots
		# The centred pointer must not overwrite the stick's formation aim.
		hud.inject_mouse_move(root.get_visible_rect().get_center())
		for i in 3: await process_frame
		assert(hud.get_placement_ghost_state().formation_slots==aimed)
		event.pressed=false
		event.button_index=first
		controller._input(event)
		event.button_index=second
		controller._input(event)
		for i in 3: await process_frame
		assert(not hud.get_placement_ghost_state().formation_visible)
		assert(not controller._formation_held and controller._wait_stick_neutral)
	controller.queue_free()
	print("FORMATION PLACEMENT PASS")
	hud.queue_free()
	world.queue_free()
	camera.queue_free()
	await process_frame
	quit()
