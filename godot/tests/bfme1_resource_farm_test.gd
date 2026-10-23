## Original-asset resource-plot construction and healthy level-2 Gondor farm regression.
## Run with BFME1_INSTALL; supports --headless.
extends SceneTree

func _initialize() -> void:
	create_timer(120).timeout.connect(func(): push_error("BFME1 resource farm test timed out"); quit(1))
	call_deferred("_run")

func _run() -> void:
	var install := OS.get_environment("BFME1_INSTALL")
	if install.is_empty():
		print("BFME1 SKIP: set BFME1_INSTALL")
		quit(77)
		return
	root.size = Vector2i(1280, 720)
	var fs = ClassDB.instantiate("RetailFileSystem")
	assert(fs.mount_bfme1_path(install).ok)
	var world = ClassDB.instantiate("GameWorld")
	root.add_child(world)
	assert(world.setup(fs).ok)
	var slots: Array = []
	for i in 8:
		slots.append({"state":1,"name":"Closed","accepted":true,"color":-1,"start_pos":-1,"player_template":-1,"team":-1})
	slots[0] = {"state":6,"name":"FloorTest","accepted":true,"color":0,"start_pos":0,"player_template":4,"team":0}
	assert(world.prepare_new_game({"mode":"skirmish","difficulty":1,"rank_points":0,"map_crc":0,"map_size":0,"map_mask":0,"map":"maps/map mp fords of isen/map mp fords of isen.map","seed":9001,"starting_cash":100000,"slots":slots}).ok)
	assert(world.start_new_game({"logic_thread":false}).ok)
	world.set_shroud_drawn(false)
	var plot := 0
	for id in world.get_object_ids():
		if world.get_object(id).template == "EconomyPlotFlagMuliplayer":
			plot = id
			break
	assert(world.get_object(plot).ok, "Missing resource plot")
	var site: Dictionary = world.get_object(plot)
	world.create_object("GondorFighter", 3, site.x, site.y, 0)
	for i in 150: world.advance(0.2)
	assert(world.get_object(plot).owner == "Player_1", "Resource plot was not captured")
	var camera := Camera3D.new()
	root.add_child(camera)
	var hud = ClassDB.instantiate("InGameHudNode")
	root.add_child(hud)
	assert(hud.setup(fs, world, camera, "Player_1", {"camera_start":Vector3(site.x, 0, -site.y)}).ok)
	hud.select_controller_group(PackedInt64Array([plot]))
	for i in 90: await process_frame
	var command := {}
	for button in hud.get_command_buttons():
		if button.command == "CASTLE_UNPACK": command = button
	assert(not command.is_empty(), "Captured plot has no build command")
	assert(hud.activate_command(command.slot, command.in_palantir, false))
	var farm := 0
	for i in 100:
		world.advance(0.2)
		await process_frame
		for object in world.get_player_objects("Player_1"):
			if object.template == "GondorFarmMultiplayer": farm = object.id
		if farm != 0: break
	assert(world.get_object(farm).ok, "Resource plot did not create a farm")
	var initial: Dictionary = world.get_object(farm)
	assert(initial.under_construction, "Farm appeared complete at birth")
	assert(initial.construction_percent == 0.0, str(initial))
	assert(initial.health == 1.0, str(initial))
	assert(initial.conditions.has("ACTIVELY_BEING_CONSTRUCTED"))
	assert(initial.models.has("GBFarm_A"), str(initial))
	var progressed := false
	for i in 600:
		world.advance(0.2)
		if i % 5 == 0: await process_frame
		var object: Dictionary = world.get_object(farm)
		if object.under_construction:
			# tolerance: progress must pass the initial 0 before completion; worker travel can delay it.
			progressed = progressed or object.construction_percent > 0.0
		else: break
	assert(progressed, "Farm skipped its construction progress")
	for i in 5: await process_frame
	var complete: Dictionary = world.get_object(farm)
	assert(not complete.under_construction, "Farm never finished")
	assert(complete.construction_percent == -1.0)
	assert(complete.health == complete.max_health)
	assert(complete.damage_state == 0)
	assert(world.get_ability_state(farm).level == 2)
	assert(not complete.conditions.has("DAMAGED"))
	assert(not complete.conditions.has("REALLYDAMAGED"))
	assert(complete.hidden_subobjects.has("GBFARM_SKN.WINDOW_F01"), str(complete))
	assert(complete.hidden_subobjects.has("GBFARM_SKN.WINDOW_N01"), str(complete))
	assert(complete.models.has("GBFarm_SKN"), str(complete))
	# A farm spawned healthy at level 2 must receive the initial visibility too,
	# even if it never sends a changed model-condition mask to the drawable.
	var spawned: int = world.create_object("GondorFarmMultiplayer", 3, site.x + 180, site.y, 0)
	world.debug_set_object_rank(spawned, 2)
	for i in 5:
		world.advance(0.2)
		await process_frame
	assert(world.get_object(spawned).hidden_subobjects.has("GBFARM_SKN.WINDOW_F01"))
	print("BFME1 RESOURCE FARM PASS: timed build, level 2 completion, healthy fire windows hidden")
	quit()
