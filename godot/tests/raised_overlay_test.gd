## GPU regression: terrain-height overlays must remain visible on Celduin's scenery deck.
## Uses synthetic overlay textures/data with the real retail scene and W3D materials.
## -- --captures=DIR saves viewport evidence, never extracted assets.
extends SceneTree
var failures := 0
var data: Image
var data_texture: ImageTexture
var vp: SubViewport

func _initialize() -> void:
	call_deferred("run")

func check(ok: bool, reason: String) -> void:
	print("RAISED OVERLAY ", "ok " if ok else "FAIL ", reason)
	if not ok:
		failures += 1

func snapshot() -> Image:
	for i in 5:
		await process_frame
		RenderingServer.force_draw(true, 0.0)
	return vp.get_texture().get_image()

func capture(name: String, image: Image) -> void:
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--captures="):
			var folder := arg.trim_prefix("--captures=")
			DirAccess.make_dir_recursive_absolute(folder)
			check(image.save_png(folder.path_join(name + ".png")) == OK, "save " + name)

func run() -> void:
	if DisplayServer.get_name() == "headless":
		print("SKIP: requires GPU")
		quit(77)
		return
	if OS.get_environment("ROTWK_INSTALL").is_empty() or OS.get_environment("BFME2_INSTALL").is_empty():
		print("SKIP: requires ROTWK_INSTALL and BFME2_INSTALL")
		quit(77)
		return
	var fs = ClassDB.instantiate("RetailFileSystem")
	if not fs.mount_retail().ok:
		printerr("RAISED OVERLAY: mount failed")
		quit(1)
		return
	vp = SubViewport.new()
	vp.size = Vector2i(640, 480)
	vp.own_world_3d = true
	vp.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	root.add_child(vp)
	var world = ClassDB.instantiate("GameWorld")
	vp.add_child(world)
	if not world.setup(fs).ok or not world.load_map("map good celduin", {"terrain": true, "animations": false, "fog": false}).ok:
		printerr("RAISED OVERLAY: scene setup failed")
		quit(1)
		return
	world.advance(0.2)
	world.set_auto_advance(false)
	world.set_process(false)
	world.set_shroud_drawn(false)
	# Remove terrain from the test: any changed pixels must come from an object receiver.
	world.get_node("Terrain").hide()
	var camera := Camera3D.new()
	vp.add_child(camera)
	camera.projection = Camera3D.PROJECTION_ORTHOGONAL
	camera.size = 240.0
	camera.position = Vector3(2760.5, 800.0, -1450.5)
	camera.rotation_degrees.x = -90.0
	camera.current = true
	RenderingServer.global_shader_parameter_set("sel_grid2", Vector4.ZERO)
	RenderingServer.global_shader_parameter_set("radius_cursor_transform", Vector4.ZERO)
	var before := await snapshot()
	capture("deck-before", before)
	var atlas := Image.create(2, 2, false, Image.FORMAT_RGBA8)
	atlas.fill(Color(0.0, 1.0, 0.0, 1.0))
	var atlas_texture := ImageTexture.create_from_image(atlas)
	RenderingServer.global_shader_parameter_set("radius_cursor_texture", atlas_texture)
	RenderingServer.global_shader_parameter_set("radius_cursor_params", Vector4(0.6, 1.0, 0.0, 0.0))
	RenderingServer.global_shader_parameter_set("radius_cursor_transform", Vector4(2760.5, 1450.5, 160.0, 0.0))
	var radius := await snapshot()
	check(radius.get_data() != before.get_data(), "ability cursor changes object pixels with terrain hidden")
	capture("deck-radius", radius)
	RenderingServer.global_shader_parameter_set("radius_cursor_params", Vector4(0.6, 0.0, 0.0, 0.0))
	check((await snapshot()).get_data() != before.get_data(), "additive ability cursor also changes object pixels")
	RenderingServer.global_shader_parameter_set("radius_cursor_transform", Vector4.ZERO)
	check((await snapshot()).get_data() == before.get_data(), "cancelling cursor restores object appearance")
	# One alpha claim at terrain height zero, and one cell/index entry. Its mask is 2x2.
	data = Image.create(1024, 1, false, Image.FORMAT_RGBAF)
	data.fill(Color(0, 0, 0, 0))
	data.set_pixel(0, 0, Color(2760.5, 1450.5, 160, 180))
	data.set_pixel(1, 0, Color(255, 255, 255, 0))
	data.set_pixel(2, 0, Color(0, 0, 2, 2))
	data.set_pixel(3, 0, Color(1, 0, 160, 6))
	data.set_pixel(4, 0, Color(0, 0, 0, 1))
	data.set_pixel(5, 0, Color(2760.5, 1450.5, 160, 0))
	data.set_pixel(6, 0, Color(268, 137, 2, 2))
	for i in range(7, 11):
		data.set_pixel(i, 0, Color(255, 0, 0, 0))
	data_texture = ImageTexture.create_from_image(data)
	RenderingServer.global_shader_parameter_set("sel_data", data_texture)
	RenderingServer.global_shader_parameter_set("sel_atlas", atlas_texture)
	RenderingServer.global_shader_parameter_set("sel_grid", Vector4(0, 0, 8192, 1))
	RenderingServer.global_shader_parameter_set("sel_grid2", Vector4(1, 4, 5, 4))
	var resource := await snapshot()
	check(resource.get_data() != before.get_data(), "masked farm overlay changes deck pixels despite terrain-height origin")
	capture("deck-resource", resource)
	for i in range(7, 11):
		data.set_pixel(i, 0, Color(0, 0, 0, 0))
	data_texture.update(data)
	check((await snapshot()).get_data() == before.get_data(), "zero claimable mask leaves the deck untouched")
	RenderingServer.global_shader_parameter_set("sel_grid2", Vector4.ZERO)
	vp.queue_free()
	await process_frame
	print("RAISED OVERLAY ", "PASS" if failures == 0 else "FAIL")
	quit(0 if failures == 0 else 1)
