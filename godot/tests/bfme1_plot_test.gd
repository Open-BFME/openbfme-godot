extends SceneTree
func _initialize() -> void: call_deferred("_run")
func _run() -> void:
	root.size = Vector2i(1280, 720)
	if OS.get_environment("BFME1_INSTALL").is_empty():
		print("BFME1 PLOT SKIP: set BFME1_INSTALL")
		quit()
		return
	var rohan := OS.get_cmdline_user_args().has("--rohan")
	var fs = ClassDB.instantiate("RetailFileSystem")
	assert(fs.mount_bfme1_path(OS.get_environment("BFME1_INSTALL")).ok)
	var world = ClassDB.instantiate("GameWorld")
	root.add_child(world)
	assert(world.setup(fs).ok)
	var start_slots: Array = []
	for i in 8: start_slots.append({"state":1,"name":"Closed","accepted":true,"color":-1,"start_pos":-1,"player_template":-1,"team":-1})
	start_slots[0] = {"state":6,"name":"PlotTest","accepted":true,"color":0,"start_pos":0,"player_template":3 if rohan else 4,"team":0}
	assert(world.prepare_new_game({"mode":"skirmish","difficulty":1,"rank_points":0,"map_crc":0,"map_size":0,"map_mask":0,"map":"maps/map mp fords of isen/map mp fords of isen.map","seed":9001,"starting_cash":100000,"slots":start_slots}).ok)
	assert(world.start_new_game({"starting_walls":false,"require_enclosed":false,"logic_thread":false}).ok)
	for i in 8: world.advance(0.2)
	var plot := 0
	for id in world.get_object_ids():
		var object: Dictionary = world.get_object(id)
		if object.get("owner","")=="Player_1" and object.template==("RohanBuildingFoundation" if rohan else "GondorBuildingFoundation"): plot=id; break
	assert(plot>0,"No fortress plot")
	var info: Dictionary = world.get_object(plot)
	var camera = Camera3D.new()
	root.add_child(camera)
	var hud = ClassDB.instantiate("InGameHudNode")
	root.add_child(hud)
	assert(hud.setup(fs,world,camera,"Player_1",{"camera_start":Vector3(info.x,0,-info.y)}).ok)
	world.set_shroud_drawn(false)
	for i in 10: await process_frame
	var pixel: Vector2 = camera.unproject_position(Vector3(info.x,info.z,-info.y))
	var probe: Dictionary = hud.pick_probe(pixel,plot)
	assert(probe.picked == plot, str(probe))
	hud.inject_mouse_move(pixel)
	hud.inject_mouse_button(1,true,pixel,false)
	hud.inject_mouse_button(1,false,pixel,false)
	for i in 4: await process_frame
	world.advance(0.2)
	assert(hud.get_selection().has(plot), str(hud.get_selection()))
	for i in 90: await process_frame
	var radial: Dictionary = hud.get_hud6_state().radial
	assert(radial.count > 0 and radial.draws > 0, str(radial))
	for error in hud.get_report().errors:
		assert(not "BuildingRadialButtons" in error and not "RadialBorders" in error, error)
	var command := {}
	for button in hud.get_command_buttons():
		if button.command == "FOUNDATION_CONSTRUCT" and button.state == 1:
			command = button
			break
	assert(not command.is_empty(), str(hud.get_command_buttons()))
	var existing: Array = world.get_object_ids()
	assert(hud.press_command_button(command.name))
	for i in 5:
		world.advance(0.2)
		await process_frame
	assert(world.get_object(plot).drawable_hidden)
	assert(hud.get_selection().is_empty(), str(hud.get_selection()))
	var building := 0
	for id in world.get_object_ids():
		var object: Dictionary = world.get_object(id)
		if not existing.has(id) and object.template == command.template: building = id
	assert(building > 0, "Plot command did not start construction")
	assert(world.destroy_object(building))
	for i in 5:
		world.advance(0.2)
		await process_frame
	assert(not world.get_object(plot).drawable_hidden)
	print("BFME1 PLOT PASS: clicked plot, opened menu, started ",command.template)
	quit()
