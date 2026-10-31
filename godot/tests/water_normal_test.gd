## GPU golden normals from watershader.fxo pixel blobs 8/9, 0 and 3.
## Run with a GPU: godot --rendering-method gl_compatibility --path godot --script res://tests/water_normal_test.gd
extends SceneTree

func _initialize() -> void:
	call_deferred("_run")

func _texture(color: Color) -> ImageTexture:
	var image := Image.create(1, 1, false, Image.FORMAT_RGBAF)
	image.fill(color)
	return ImageTexture.create_from_image(image)

func _run() -> void:
	if not (RenderingServer.get_current_rendering_method() == "gl_compatibility"):
		push_error("Run with --rendering-method gl_compatibility for raw normal readback")
		quit(1)
		return
	var shader := Shader.new()
	var code := FileAccess.get_file_as_string("res://shaders/water_standing.gdshader")
	if not (code.contains("ALBEDO = retail_output(col);")):
		push_error('code.contains("ALBEDO = retail_output(col);")')
		quit(1)
		return
	# Probe the actual production normal, preserving its complete reconstruction.
	shader.code = code.replace("ALBEDO = retail_output(col);", "ALBEDO = n * 0.5 + 0.5;").replace("ALPHA = clamp(alpha, 0.0, 1.0);", "ALPHA = 1.0;")
	var material := ShaderMaterial.new()
	material.shader = shader
	material.set_shader_parameter("bump_tex", _texture(Color(0.25, 0.75, 0.5, 1)))
	material.set_shader_parameter("bump_scale", 0.5)
	material.set_shader_parameter("pca_tex0", _texture(Color(0.125, 0.25, 0.375, 0.5)))
	material.set_shader_parameter("pca_tex1", _texture(Color(0.625, 0.75, 0.875, 1.0)))
	material.set_shader_parameter("pca_tex2", _texture(Color(0.25, 0.375, 0.625, 0.75)))
	material.set_shader_parameter("pca_noise_tex", _texture(Color(0.5, 0, 0, 0.5)))
	var coefficients := Image.create(1, 10, false, Image.FORMAT_RGBAF)
	# Distinct rows catch swapped basis groups; unused rows are poison values.
	var rows := [Color(0.5, 0.375, 0.25, 0), Color(0.125, 0.25, 0.375, 0.5),
		Color(0.5, 0.375, 0.25, 0.125), Color(0.25, 0.5, 0.125, 0.375),
		Color(0.375, 0.125, 0.5, 0.25), Color(0.25, 0.5, 0.375, 0.125),
		Color(0.125, 0.375, 0.25, 0.5), Color(100, 100, 100, 100),
		Color(200, 200, 200, 200), Color(300, 300, 300, 300)]
	for row in rows.size():
		coefficients.set_pixel(0, row, rows[row])
	material.set_shader_parameter("pca_coefficients", ImageTexture.create_from_image(coefficients))
	material.set_shader_parameter("pca_frame_rate", 0.0)
	material.set_shader_parameter("wave_geometry_enabled", false)
	var viewport := SubViewport.new()
	viewport.size = Vector2i(64, 64)
	viewport.own_world_3d = true
	viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	root.add_child(viewport)
	var plane := PlaneMesh.new()
	plane.size = Vector2(8, 8)
	plane.material = material
	var instance := MeshInstance3D.new()
	instance.mesh = plane
	viewport.add_child(instance)
	var camera := Camera3D.new()
	viewport.add_child(camera)
	camera.position = Vector3(0, 5, 3)
	camera.look_at(Vector3.ZERO)
	camera.current = true
	# Fixed independent instruction-trace results (see docs/WATER_PCA_NORMALS.md).
	var expected := [Vector3(-0.40824829, 0.40824829, 0.81649658),
		Vector3(0.73018858, -0.04867924, 0.68150934),
		Vector3(0.69705699, -0.22012326, 0.64949536)]
	for mode in 3:
		material.set_shader_parameter("pca_mode", mode)
		for frame in 5:
			await process_frame
		await RenderingServer.frame_post_draw
		var pixel := viewport.get_texture().get_image().get_pixel(32, 32)
		var actual := Vector3(pixel.r, pixel.g, pixel.b) * 2.0 - Vector3.ONE
		print("normal mode ", mode, ": ", actual)
		if actual.distance_to(expected[mode]) >= 0.025:
			push_error("water normal mode %d: %s expected %s" % [mode, actual, expected[mode]])
			quit(1)
			return
	print("PASS: BumpSpecular, PCA and PCA_U fixed-input GPU normals")
	quit(0)
