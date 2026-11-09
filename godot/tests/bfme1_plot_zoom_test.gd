## Real BFME1 blended plot materials retain their source markings across zoom.
extends SceneTree
func _initialize() -> void: call_deferred("_run")
func _run() -> void:
	var install := OS.get_environment("BFME1_INSTALL")
	if install.is_empty():
		print("BFME1 SKIP: set BFME1_INSTALL")
		quit(77)
		return
	var fs = ClassDB.instantiate("RetailFileSystem")
	var mounted: Dictionary = fs.mount_bfme1_path(install)
	print("BFME1 MOUNT ", mounted)
	if not mounted.ok: quit(1); return
	var world = ClassDB.instantiate("GameWorld")
	root.add_child(world)
	var setup: Dictionary = world.setup(fs)
	print("BFME1 SETUP ", setup.get("templates"), " templates, ", setup.get("factions"), " factions")
	for error in setup.errors: print("BFME1 ERROR ", error)
	if not setup.ok: quit(1); return
	for faction in [4, 5]: # BFME1 store: Gondor, Isengard
		var slots: Array = []
		for i in 8:
			slots.append({"state":1,"name":"Closed","accepted":true,"color":-1,"start_pos":-1,"player_template":-1,"team":-1})
		slots[0] = {"state":6,"name":"Test","accepted":true,"color":0,"start_pos":0,"player_template":faction,"team":0}
		var prepared: Dictionary = world.prepare_new_game({"mode":"skirmish","difficulty":1,"rank_points":0,"map_crc":0,"map_size":0,"map_mask":0,"map":"maps/map mp fords of isen/map mp fords of isen.map","seed":9001,"starting_cash":2000,"slots":slots})
		assert(prepared.ok, str(prepared))
		var loaded: Dictionary = world.start_new_game({"logic_thread":false})
		assert(loaded.ok, str(loaded.get("errors")))
		world.set_shroud_drawn(true)
		for i in 30: world.advance(0.2)
		world.set_shroud_drawn(false)
		var camera = Camera3D.new()
		root.add_child(camera)
		camera.current = true
		camera.near = 5
		camera.far = 30000
		var target = Vector3(3000,313,-700)
		var checked := false
		var mipmapped_floor := false
		for node in world.find_children("*", "MultiMeshInstance3D", true, false):
			var name: String = str(node.name).to_lower()
			if "floor" in name:
				for surface in node.multimesh.mesh.get_surface_count():
					var material = node.multimesh.mesh.surface_get_material(surface)
					if material.shader.code.contains("uniform sampler2D w3d_tex0 : filter_linear_mipmap"):
						mipmapped_floor = true
			if not (name.begins_with("gbfoundationx") or name.begins_with("ibfoundationx")):
				continue
			for surface in node.multimesh.mesh.get_surface_count():
				var material = node.multimesh.mesh.surface_get_material(surface)
				assert(material.render_priority == 32, name)
				assert(material.shader.code.contains("uniform sampler2D w3d_tex0 : filter_linear,"), name)
				checked = true
		assert(checked, "Missing rendered foundation material")
		assert(mipmapped_floor, "Castle floors must retain mipmaps")
		for height in [500, 800, 1200, 1600]:
			camera.position = target + Vector3(0,height,height)
			camera.look_at(target)
			await create_timer(0.3).timeout
			await RenderingServer.frame_post_draw
			var shots := OS.get_environment("BFME1_PLOT_SCREENSHOTS")
			if not shots.is_empty():
				DirAccess.make_dir_recursive_absolute(shots)
				root.get_texture().get_image().save_png(shots.path_join("plot-%s-%s.png" % [faction, height]))
		print("BFME1 floor sampling and zoom PASS faction ", faction)
		camera.queue_free()
		await process_frame
	quit()
