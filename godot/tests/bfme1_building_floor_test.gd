## Original-asset regression: production bibs must contribute pixels above the castle floor.
## Run with BFME1_INSTALL and a graphical renderer (not --headless).
extends SceneTree

func _initialize() -> void:
	create_timer(120).timeout.connect(func(): push_error("BFME1 building floor test timed out"); quit(1))
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
	var plots: Array = []
	for id in world.get_object_ids():
		var object: Dictionary = world.get_object(id)
		if object.template == "GondorBuildingFoundation" and object.owner == "Player_1":
			plots.append(object)
	var camera := Camera3D.new()
	root.add_child(camera)
	camera.current = true
	camera.near = 5.0
	camera.far = 30000.0
	world.set_shroud_drawn(false)
	for index in 2:
		var plot: Dictionary = plots[index]
		var template: String = ["GondorStable", "GondorArcherRange"][index]
		var model: String = ["gbstable_bib", "gbarcheryn_bib"][index]
		# Remove the empty marking as normal foundation construction does.
		world.destroy_object(plot.id)
		world.set_time_scale(1.0)
		var id: int = world.create_object(template, 3, plot.x, plot.y, 0)
		assert(world.get_object(id).ok)
		var target := Vector3(plot.x, plot.z, -plot.y)
		camera.position = target + Vector3(0, 250, 220)
		camera.look_at(target)
		for i in 10: world.advance(0.2)
		for i in 5: await process_frame
		var overlays: Array = []
		for node in world.find_children("*", "MultiMeshInstance3D", true, false):
			if str(node.name).to_lower().begins_with(model):
				overlays.append(node)
		assert(not overlays.is_empty(), "Missing floor: " + template)
		for node in overlays:
			for surface in node.multimesh.mesh.get_surface_count():
				var material = node.multimesh.mesh.surface_get_material(surface)
				assert(material.render_priority == 32)
				assert(not material.shader.code.contains("depth_test_disabled"))
		# Pixel comparisons must use the same simulation/animation state.
		world.set_time_scale(0.0)
		await RenderingServer.frame_post_draw
		var shown := root.get_texture().get_image()
		for node in overlays: node.visible = false
		await process_frame
		await RenderingServer.frame_post_draw
		var hidden := root.get_texture().get_image()
		for node in overlays: node.visible = true
		var point := camera.unproject_position(target)
		var different := 0
		for y in range(int(point.y) - 100, int(point.y) + 101):
			for x in range(int(point.x) - 150, int(point.x) + 151):
				if shown.get_pixel(x, y) != hidden.get_pixel(x, y): different += 1
		# tolerance: any changed pixel proves the bib contributes above the castle surface.
		assert(different > 0, template + " floor is covered by the castle floor")
		var shots := OS.get_environment("BFME1_FLOOR_SCREENSHOTS")
		if not shots.is_empty():
			DirAccess.make_dir_recursive_absolute(shots)
			shown.save_png(shots.path_join(template + ".png"))
		print("BFME1 BUILDING FLOOR PASS ", template, " changed pixels ", different)
	quit()
