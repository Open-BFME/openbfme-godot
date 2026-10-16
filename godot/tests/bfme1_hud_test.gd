extends SceneTree
func _initialize() -> void: call_deferred("_run")
func _run() -> void:
	root.size = Vector2i(1280, 720)
	if OS.get_environment("BFME1_INSTALL").is_empty():
		print("BFME1 PLOT SKIP: set BFME1_INSTALL")
		quit()
		return
	var rohan := OS.get_cmdline_user_args().has("--rohan")
	if OS.get_environment("BFME1_INSTALL").is_empty():
		print("BFME1 HUD SKIP: set BFME1_INSTALL")
		quit()
		return
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
	var hover: Rect2 = hud.get_hud6_state().radial.buttons[0].rect
	hud.inject_mouse_move(hover.get_center())
	for i in 120: await process_frame
	var help: Dictionary = hud.get_hud6_state().help
	assert(help.shown and help.text_ops > 0 and not help.name.is_empty(), str(help))
	var keep := 0
	for id in world.get_object_ids():
		if world.get_object(id).template == "GondorCastleBaseKeep": keep = id; break
	await _select_object(hud, world, camera, keep)
	for i in 120: await process_frame
	var report: Dictionary = hud.get_report()
	assert(report.errors.is_empty(), str(report.errors))
	assert(str(report.notes).contains("CommandButtons.Command1 = HIGandalTheGrey"), str(report.notes))

	var click: Dictionary = hud.find_button_window("CommandButtons.Command1")
	assert(click.found,str(click))
	hud.inject_mouse_move(Vector2(click.x,click.y))
	hud.inject_mouse_button(1,true,Vector2(click.x,click.y),false)
	hud.inject_mouse_button(1,false,Vector2(click.x,click.y),false)
	for i in 10: await process_frame
	assert(str(hud.get_state().palantir.last_commands).contains("OnBttnCommand(Command1)"))
	print("BFME1 HUD BUTTONS PASS: building slots, real click, plot tooltip text")
	quit()

func _select_object(hud, world, camera, id: int) -> void:
	var object: Dictionary = world.get_object(id)
	hud.camera_look_at(Vector2(object.x, object.y))
	for i in 10: await process_frame
	var pixel: Vector2 = camera.unproject_position(Vector3(object.x, object.z, -object.y))
	hud.inject_mouse_move(pixel)
	hud.inject_mouse_button(1, true, pixel, false)
	hud.inject_mouse_button(1, false, pixel, false)
	for i in 4: await process_frame
	world.advance(0.2)
	assert(hud.get_selection().has(id), str(hud.get_selection()))
