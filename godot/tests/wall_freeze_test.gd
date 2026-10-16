extends SceneTree
func _initialize() -> void: call_deferred("_run")
func _run() -> void:
	root.size = Vector2i(1280,720)
	var fs = ClassDB.instantiate("RetailFileSystem")
	assert(fs.mount_retail().ok)
	var world = ClassDB.instantiate("GameWorld")
	root.add_child(world)
	assert(world.setup(fs).ok)
	assert(world.load_map("map mp fall back 4p", {"slots":[{"player":"Player_1","faction":"FactionElves","human":true}]}).ok)
	var player = world.execute_lua("for id,p in pairs(bfme.players) do if p.name=='Player_1' then p.money=100000; return id end end")
	assert(player.ok)
	var hub_template = "MenWallHubSmall" if "--men" in OS.get_cmdline_user_args() else "ElvenCastleWallHub"
	var hub = world.create_object(hub_template,int(player.output),1800,1800,0)
	assert(hub > 0)
	var camera = Camera3D.new()
	root.add_child(camera)
	var hud = ClassDB.instantiate("InGameHudNode")
	root.add_child(hud)
	assert(hud.setup(fs,world,camera,"Player_1",{"camera_start":Vector3(2000,0,-1800),"alternate_mouse":true}).ok)
	hud.set_controller_layout(true)
	hud.select_controller_group(PackedInt64Array([hub]))
	world.advance(0.2)
	for i in 4: await process_frame
	var span := {}
	for command in hud.get_command_buttons():
		if command.get("template", "") == hub_template: span = command
	assert(not span.is_empty(),str(hud.get_command_buttons()))
	assert(hud.activate_command(span.slot,span.in_palantir,false))
	var loads := 0
	for pass_index in 3:
		for distance in [200,320,450,600,450,320,200]:
			hud.inject_mouse_move(hud.world_to_pixel(Vector2(1800+distance,1800)))
			for i in 2: await process_frame
			var preview: Dictionary = hud.get_placement_ghost_state()
			assert(preview.icons.size() > 1,str(preview))
		if pass_index == 0: loads = hud.get_placement_ghost_state().model_loads
		else: assert(hud.get_placement_ghost_state().model_loads == loads,"Preview reloaded wall models")
	hud.controller_deselect()
	assert(world.build2_wall_span(hub,hub_template,1800,1800,2200,1800,0))
	for i in 300: world.advance(0.2)
	print("WALL FREEZE PASS")
	hud.queue_free()
	world.queue_free()
	camera.queue_free()
	await process_frame
	quit()
