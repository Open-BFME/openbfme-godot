## Real W3D caster/receiver integration on synthetic terrain. Requires retail assets and a GPU.
extends SceneTree

var failures := 0

func _initialize() -> void:
	call_deferred("_run")

func _check(ok: bool, message: String) -> void:
	print("  ", "ok " if ok else "FAIL ", message)
	if not ok:
		failures += 1

func _capture(viewport: SubViewport, name: String) -> Image:
	for i in 5:
		await process_frame
		await RenderingServer.frame_post_draw
	var image := viewport.get_texture().get_image()
	var folder := OS.get_environment("OPENBFME_SHADOW_CAPTURE_DIR")
	if not folder.is_empty():
		DirAccess.make_dir_recursive_absolute(folder)
		image.save_png(folder.path_join(name + ".png"))
	return image

func _run() -> void:
	if DisplayServer.get_name() == "headless" or OS.get_environment("ROTWK_INSTALL").is_empty() or OS.get_environment("BFME2_INSTALL").is_empty():
		print("SKIP: retail assets and GPU required")
		quit(77)
		return
	var fs = ClassDB.instantiate("RetailFileSystem")
	if not fs.mount_retail().ok:
		quit(1)
		return
	var viewport := SubViewport.new()
	viewport.size = Vector2i(400, 300)
	viewport.own_world_3d = true
	viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	root.add_child(viewport)
	var inst = ClassDB.instantiate("W3DInstancer")
	viewport.add_child(inst)
	_check(inst.setup(fs).ok, "renderer setup")
	inst.set_shadow_receiving(true)
	inst.set_auto_update(false)
	inst.set_playing(false)
	var model: int = inst.add_model("GBBarracks_SKN")
	_check(model >= 0, "barracks model builds")
	var id: int = inst.add_instance(model, Transform3D.IDENTITY, "", 0.0, 1.0)
	inst.set_instance_cast_shadow(id, true)
	inst.update_now()
	var builder = ClassDB.instantiate("MapTerrainBuilder")
	var atlas := Image.create(4, 4, false, Image.FORMAT_RGBA8)
	atlas.fill(Color(0.8, 0.8, 0.8))
	var patch: MeshInstance3D = builder.build_test_patch({"base_atlas": atlas,
		"uv0": PackedVector2Array([Vector2(0.5, 0.5), Vector2(0.5, 0.5), Vector2(0.5, 0.5), Vector2(0.5, 0.5)])})
	viewport.add_child(patch)
	patch.scale = Vector3(40, 1, 40)
	patch.position = Vector3(-200, -0.1, 200)
	patch.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	var material: ShaderMaterial = patch.mesh.surface_get_material(0)
	material.set_shader_parameter("debug_mode", 0)
	material.set_shader_parameter("overbright", false)
	material.set_shader_parameter("sun_color", Vector3.ZERO)
	var camera := Camera3D.new()
	viewport.add_child(camera)
	camera.projection = Camera3D.PROJECTION_ORTHOGONAL
	camera.size = 400
	camera.far = 2000
	camera.position = Vector3(200, 220, 300)
	camera.look_at(Vector3(0, 20, 0))
	camera.make_current()
	var sun := DirectionalLight3D.new()
	viewport.add_child(sun)
	sun.basis = Basis.looking_at(Vector3(-0.8, -1, -0.3).normalized())
	var shadows = load("res://scripts/object_shadows.gd")
	var lighting := {"shadow_color": 0xff808080}
	shadows.configure(sun, lighting, 0)
	var before := await _capture(viewport, "building-off")
	shadows.configure(sun, lighting, 3)
	sun.directional_shadow_max_distance = 1000
	var after := await _capture(viewport, "building-on")
	var darkened := 0
	var brightened := 0
	for y in before.get_height():
		for x in before.get_width():
			var difference := after.get_pixel(x, y).get_luminance() - before.get_pixel(x, y).get_luminance()
			# Ignore two-byte quantization noise from the equivalent emission/light output paths.
			if difference < -2.0 / 255.0:
				darkened += 1
			elif difference > 2.0 / 255.0:
				brightened += 1
	# A visible building shadow covers hundreds of pixels; isolated numerical differences cannot satisfy this.
	_check(darkened > 500, "W3D casts and receives a visible shadow (%d pixels)" % darkened)
	_check(brightened == 0, "enabling shadows adds no light")
	shadows.configure(sun, lighting, 0)
	var restored := await _capture(viewport, "building-restored")
	_check(restored.get_data() == before.get_data(), "runtime off restores the exact baseline")
	shadows.disable()
	viewport.free()
	print("OBJECT SHADOW BUILDING ", "PASS" if failures == 0 else "FAIL")
	quit(0 if failures == 0 else 1)
