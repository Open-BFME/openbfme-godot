extends SceneTree
func _initialize() -> void: call_deferred("_run")
func _run() -> void:
	create_timer(120).timeout.connect(func(): printerr("FORMATION TIMEOUT"); quit(1))
	root.size = Vector2i(1280,720)
	var fs = ClassDB.instantiate("RetailFileSystem")
	assert(fs.mount_retail().ok)
	var world = ClassDB.instantiate("GameWorld")
	root.add_child(world)
	assert(world.setup(fs).ok)
	var loaded: Dictionary = world.load_map("map mp fall back 4p",{"slots":[{"player":"Player_1","faction":"FactionMen","human":true}]})
	assert(loaded.ok)
	var player := -1
	for candidate in world.get_economy().players:
		if candidate.name == "Player_1": player = int(candidate.index)
	assert(player != -1)
	var unit_template := OS.get_environment("FORMATION_UNIT")
	if unit_template.is_empty(): unit_template = "MenPorter"
	# Keep map creeps from killing porters while arrival and final facing are measured.
	for id in world.get_object_ids(): world.destroy_object(id)
	world.advance(0.2)
	var units := PackedInt64Array()
	for i in 4: units.append(world.create_object(unit_template,player,1800+i*40,1800,0))
	var camera = Camera3D.new()
	root.add_child(camera)
	var hud = ClassDB.instantiate("InGameHudNode")
	root.add_child(hud)
	assert(hud.setup(fs,world,camera,"Player_1",{"test_hooks":true,"camera_start":Vector3(2100,0,-2000),"alternate_mouse":not ("--standard" in OS.get_cmdline_user_args())}).ok)
	assert(hud.select_objects_with_message(units)==units.size())
	world.advance(0.2)
	for i in 4: await process_frame
	var start: Vector2 = hud.world_to_pixel(Vector2(2100,2100))
	hud.inject_mouse_move(start)
	hud.inject_mouse_button(MOUSE_BUTTON_LEFT,true,start)
	hud.inject_mouse_button(MOUSE_BUTTON_RIGHT,true,start)
	for i in 3: await process_frame
	var preview: Dictionary = hud.get_placement_ghost_state()
	assert(preview.formation_visible and preview.formation_slots.size()==4,str(preview))
	assert(preview.formation_textures.has("FPHeroDecal.tga" if unit_template=="MenPorter" else "FPInfSingle.tga"),str(preview))
	assert(preview.formation_textures.has("FPFacingArrowHead.tga"),str(preview))
	assert(preview.formation_textures.has("FPFacingArrowBody.tga"),str(preview))
	assert(preview.formation_textures.has("FPSmallFacingArrow.tga"),str(preview))
	var narrow: Array = preview.formation_slots
	var end: Vector2 = hud.world_to_pixel(Vector2(2400,2100))
	hud.inject_mouse_move(end)
	for i in 3: await process_frame
	preview = hud.get_placement_ghost_state()
	assert(preview.formation_visible and preview.formation_slots.size()==4,str(preview))
	assert(preview.formation_slots != narrow,"Drag did not change formation width")
	assert(hud.get_selection().size()==4,"Formation changed selection")
	if DisplayServer.get_name() != "headless" and not OS.get_environment("FORMATION_SHOT").is_empty():
		await RenderingServer.frame_post_draw
		assert(root.get_texture().get_image().save_png(OS.get_environment("FORMATION_SHOT")) == OK)
	if "--preview-only" in OS.get_cmdline_user_args():
		print("FORMATION PREVIEW PASS: ", unit_template)
		quit()
		return
	var slots: Array = preview.formation_slots
	for i in 4:
		var unit: Dictionary = world.get_object(units[i])
		# tolerance: the preview must leave the initial position within locomotor stopping precision.
		assert(abs(unit.x-(1800+i*40))<2,"Preview issued an early order")
	hud.inject_mouse_button(MOUSE_BUTTON_LEFT,false,end)
	hud.inject_mouse_button(MOUSE_BUTTON_RIGHT,false,end)
	for i in 3: await process_frame
	assert("MSG_DO_MOVETO_FORMATION" in str(hud.get_message_log()))
	await create_timer(2.1).timeout
	assert(not hud.get_placement_ghost_state().formation_visible)
	assert(hud.get_selection().size()==4,"Releasing the chord deselected units")
	# Allow the slower porters to finish paths to the wider decal-sized rows.
	for i in 150: world.advance(0.2)
	for slot in slots:
		var unit: Dictionary = world.get_object(slot.id)
		assert(unit.ok,str([unit,slot]))
		# tolerance: cell snapping and original pathfinder arrival clearance.
		assert(Vector2(unit.x,unit.y).distance_to(Vector2(slot.x,slot.y))<45,str([unit,slot]))
		# tolerance: final-facing completion uses a 0.31415927-radian threshold.
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
	print("FORMATION PLACEMENT PASS")
	hud.queue_free()
	world.queue_free()
	camera.queue_free()
	await process_frame
	quit()
