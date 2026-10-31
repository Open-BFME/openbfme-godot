## Numeric GPU regression of the actual terrain receiver. No retail assets required.
## Run without --headless: godot --path godot --script res://tests/object_shadow_render_test.gd
extends SceneTree

var failures := 0

func _initialize() -> void:
	call_deferred("_run")

func _check(value: bool, message: String) -> void:
	print("  ", "ok " if value else "FAIL ", message)
	if not value:
		failures += 1

func _sample(viewport: SubViewport, position: Vector2i, expected: float, message: String) -> void:
	var color := viewport.get_texture().get_image().get_pixelv(position)
	# A two-byte tolerance covers the sRGB framebuffer's quantization and shadow-map filtering.
	_check(absf(color.r - expected) <= 2.0 / 255.0 and absf(color.g - expected) <= 2.0 / 255.0 and absf(color.b - expected) <= 2.0 / 255.0,
		"%s: got %s, expected %.3f" % [message, color, expected])

func _settle() -> void:
	for i in 5:
		await process_frame
		await RenderingServer.frame_post_draw

func _run() -> void:
	if DisplayServer.get_name() == "headless":
		print("SKIP: object shadow render test requires a GPU")
		quit(77)
		return
	var viewport := SubViewport.new()
	viewport.size = Vector2i(256, 256)
	viewport.own_world_3d = true
	viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	root.add_child(viewport)
	var builder = ClassDB.instantiate("MapTerrainBuilder")
	var atlas := Image.create(4, 4, false, Image.FORMAT_RGBA8)
	atlas.fill(Color(0.8, 0.8, 0.8))
	var patch: MeshInstance3D = builder.build_test_patch({"base_atlas": atlas,
		"uv0": PackedVector2Array([Vector2(0.5, 0.5), Vector2(0.5, 0.5), Vector2(0.5, 0.5), Vector2(0.5, 0.5)])})
	viewport.add_child(patch)
	patch.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	var material: ShaderMaterial = patch.mesh.surface_get_material(0)
	material.set_shader_parameter("debug_mode", 0)
	material.set_shader_parameter("overbright", false)
	material.set_shader_parameter("sun_color", Vector3.ZERO)
	var camera := Camera3D.new()
	viewport.add_child(camera)
	camera.projection = Camera3D.PROJECTION_ORTHOGONAL
	camera.size = 10.0
	camera.near = 0.1
	camera.far = 50.0
	camera.position = Vector3(5.0, 10.0, -5.0)
	camera.rotation_degrees = Vector3(-90.0, 0.0, 0.0)
	camera.make_current()
	var sun := DirectionalLight3D.new()
	viewport.add_child(sun)
	sun.rotation_degrees = Vector3(-90.0, 0.0, 0.0)
	var shadows = load("res://scripts/object_shadows.gd")
	var lighting := {"shadow_color": 0xff808080}
	shadows.configure(sun, lighting, 0)
	var caster := MeshInstance3D.new()
	caster.mesh = BoxMesh.new()
	caster.mesh.size = Vector3(3.0, 1.0, 3.0)
	caster.position = Vector3(5.0, 2.0, -5.0)
	caster.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_SHADOWS_ONLY
	viewport.add_child(caster)
	await _settle()
	_sample(viewport, Vector2i(128, 128), 0.8, "disabled shadows preserve lighting")
	shadows.configure(sun, lighting, 3)
	sun.directional_shadow_max_distance = 30.0
	await _settle()
	_sample(viewport, Vector2i(128, 128), 0.8 * 128.0 / 255.0, "shadow multiplies in gamma space")
	_sample(viewport, Vector2i(25, 25), 0.8, "unshadowed terrain keeps its brightness")
	caster.position.x += 4.0
	await _settle()
	_sample(viewport, Vector2i(128, 128), 0.8, "moving caster clears its old shadow")
	caster.position.x -= 4.0
	shadows.configure(sun, lighting, 0)
	await _settle()
	_sample(viewport, Vector2i(128, 128), 0.8, "runtime off restores the original output")
	shadows.disable()
	viewport.queue_free()
	print("OBJECT SHADOW RENDER ", "PASS" if failures == 0 else "FAIL")
	quit(0 if failures == 0 else 1)
