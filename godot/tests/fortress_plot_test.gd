extends SceneTree
func _initialize() -> void: call_deferred("_run")
func _run() -> void:
	var fs = ClassDB.instantiate("RetailFileSystem")
	assert(fs.mount_retail().ok)
	var world = ClassDB.instantiate("GameWorld")
	root.add_child(world)
	assert(world.setup(fs).ok)
	var start_slots: Array = []
	for i in 8: start_slots.append({"state":1,"name":"Closed","accepted":true,"color":-1,"start_pos":-1,"player_template":-1,"team":-1})
	start_slots[0] = {"state":6,"name":"PlotTest","accepted":true,"color":0,"start_pos":0,"player_template":3,"team":0}
	assert(world.prepare_new_game({"mode":"skirmish","difficulty":1,"rank_points":0,"map_crc":0,"map_size":0,"map_mask":0,"map":"maps/map mp evendim/map mp evendim.map","seed":9001,"starting_cash":100000,"slots":start_slots}).ok)
	assert(world.start_new_game({"starting_walls":false,"require_enclosed":false,"logic_thread":false}).ok)
	for i in 8: world.advance(0.2)
	var plot := 0
	for id in world.get_object_ids():
		var object: Dictionary = world.get_object(id)
		if object.get("owner","")=="Player_1" and "ExpansionPad" in object.template: plot=id; break
	assert(plot>0,"No fortress plot")
	var info: Dictionary = world.get_object(plot)
	var camera = Camera3D.new()
	root.add_child(camera)
	var hud = ClassDB.instantiate("InGameHudNode")
	root.add_child(hud)
	assert(hud.setup(fs,world,camera,"Player_1",{"camera_start":Vector3(info.x,0,-info.y)}).ok)
	for attempt in 2:
		hud.select_controller_group(PackedInt64Array([plot]))
		world.advance(0.2)
		for i in 3: await process_frame
		var command := {}
		for button in hud.get_command_buttons():
			if button.command=="FOUNDATION_CONSTRUCT" and button.state==1: command=button; break
		assert(not command.is_empty(),str(hud.get_command_buttons()))
		var existing: Array = world.get_object_ids()
		assert(hud.activate_command(command.slot,command.in_palantir,false))
		world.advance(0.2)
		for i in 4: await process_frame
		world.advance(0.2)
		for i in 3: await process_frame
		assert(world.get_object(plot).drawable_hidden)
		assert(hud.get_selection().is_empty(),str(hud.get_selection()))
		var expansion := 0
		for id in world.get_object_ids():
			var object: Dictionary = world.get_object(id)
			if not existing.has(id) and object.template==command.template: expansion=id
		assert(expansion>0,"No expansion constructed")
		if attempt==0:
			hud.select_controller_group(PackedInt64Array([expansion]))
			world.advance(0.2)
			for i in 3: await process_frame
			var cancel := {}
			for button in hud.get_command_buttons():
				if "CONSTRUCT_CANCEL" in button.command: cancel=button; break
			assert(not cancel.is_empty(),str(hud.get_command_buttons()))
			assert(hud.activate_command(cancel.slot,cancel.in_palantir,false))
		else: assert(world.destroy_object(expansion))
		for i in 3: await process_frame
		for i in 3: world.advance(0.2)
		for i in 3: await process_frame
		assert(not world.get_object(plot).drawable_hidden)
	hud.queue_free()
	world.queue_free()
	camera.queue_free()
	await process_frame
	print("FORTRESS PLOT PASS")
	quit()
