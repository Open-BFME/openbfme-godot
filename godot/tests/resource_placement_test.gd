extends SceneTree

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	root.size = Vector2i(1280, 720)
	var fs = ClassDB.instantiate("RetailFileSystem")
	assert(fs.mount_retail().ok)
	var world = ClassDB.instantiate("GameWorld")
	root.add_child(world)
	assert(world.setup(fs).ok)
	var loaded = world.load_map("map mp fall back 4p", {"slots": [
		{"player":"Player_1", "faction":"FactionMen", "human":true},
		{"player":"Player_2", "faction":"FactionMordor", "human":false}]})
	assert(loaded.ok, str(loaded))
	var player = world.execute_lua("for id,p in pairs(bfme.players) do if p.name=='Player_1' then return id end end")
	assert(player.ok and not player.output.is_empty(), str(player))
	var position := Vector2(1800, 1800)
	var id = world.create_object("MenPorter", int(player.output), position.x, position.y, 0)
	assert(id > 0)
	world.advance(0.2)
	var camera := Camera3D.new()
	root.add_child(camera)
	var hud = ClassDB.instantiate("InGameHudNode")
	root.add_child(hud)
	var setup = hud.setup(fs, world, camera, "Player_1", {"camera_start":Vector3(position.x,0,-position.y), "alternate_mouse":true})
	assert(setup.ok, str(setup))
	hud.set_controller_layout(true)
	hud.select_controller_group(PackedInt64Array([id]))
	await process_frame
	world.advance(0.2)
	await process_frame
	await process_frame
	var construction := {}
	for command in hud.get_command_buttons():
		if command.get("template", "") == "GondorFarm":
			construction = command
	assert(not construction.is_empty(), str(hud.get_command_buttons()))
	assert(hud.activate_command(construction.slot, construction.in_palantir, false))
	var at := Vector2(2800, 2600)
	hud.camera_look_at(at)
	await process_frame
	await process_frame
	hud.inject_mouse_move(hud.world_to_pixel(at))
	for i in 8: await process_frame
	var preview: Dictionary = hud.get_placement_ghost_state()
	assert(preview.resource_visible, str(preview))
	assert(preview.resource_radius > 0, str(preview))
	assert(preview.resource_share > 0 and preview.resource_share <= 1, str(preview))
	hud.camera_look_at(Vector2(1800,1800))
	await process_frame
	await process_frame
	hud.inject_mouse_move(hud.world_to_pixel(Vector2(1800,1800)))
	for i in 8: await process_frame
	preview = hud.get_placement_ghost_state()
	assert(preview.resource_visible and preview.resource_share >= 0 and preview.resource_share <= 1)
	hud.controller_deselect()
	for i in 4: await process_frame
	assert(not hud.get_placement_ghost_state().resource_visible)
	print("resource placement preview: PASS")
	hud.queue_free()
	world.queue_free()
	camera.queue_free()
	await process_frame
	quit()
