extends SceneTree

## RENDERED evidence for the shroud overlay: the GPU actually draws unexplored
## ground black, remembered ground at retail's half stop, and visible ground
## untouched.
##
## WHY THIS RUNNER EXISTS INSTEAD OF A FULL-SCENE CAPTURE.
## `retail_render_capture_runner.gd` boots the whole vertical slice and would be
## the better evidence, and this lane extended it with an
## `OPENBFME_CAPTURE_ASSERT_SHROUD` path that does exactly that. On this machine
## it cannot run: the mounted content pack fails the slice's own capability gate
## (`roster_audio_closure`) before the scene ever reaches `ready`, which is a
## content condition this lane did not create and must not paper over. So the
## shader gets its own runner, one that needs no pack at all.
##
## WHAT IT ACTUALLY PROVES, and the one thing it does not.
## The shader text under test is the SHIPPED text. It is read out of
## `retail_terrain_material_builder._shader_source()` at runtime and exactly one
## line is substituted - the terrain texture blend, which needs map data this
## runner has none of - so every uniform declaration, the UV derivation, the
## out-of-bounds branch and the modulate are the real ones. If someone renames
## that line the substitution fails and this runner goes red rather than
## quietly testing a shader nobody ships.
##
## It does NOT prove the slice binds the texture to the material; that is what
## the extended capture path proves, and it is named as un-run in the report.
## `retail_fog_of_war_runner.gd` covers the binding call itself
## (`apply_to_terrain` and the overlay's own state) without a GPU.
##
## The oracle for the three expected pixel values is retail gamedata.ini:
##   ClearAlpha 255 -> 1.000, FogAlpha 127 -> 0.498, ShroudAlpha 0 -> 0.000
## in retail's inverted convention, where the byte says how much you can SEE.
##
## Invocation (NOT headless - it needs a real device):
##   <godot> --audio-driver Dummy --path game \
##     --script res://tests/retail_shroud_render_runner.gd

const Fog := preload("res://src/retail_slice/retail_fog_of_war.gd")
const Overlay := preload("res://src/retail_slice/retail_shroud_overlay.gd")
const Watchdog := preload("res://tests/runner_watchdog.gd")

const SLICE_SCALE := 0.02649232738129
const VIEWPORT := Vector2i(512, 512)
## The line the substitution replaces. Quoted verbatim from the shipped shader.
const TERRAIN_BLEND_LINE := "	vec3 terrain_color = mix(mix(base_color, primary_color, primary_factor), three_way_color, three_way_factor);"
const EXPECTED_CHECKS := 10

var passed := 0
var failed := 0
var _watchdog := Watchdog.new()


func _initialize() -> void:
	_watchdog.start(self, "RETAIL_SHROUD_RENDER", 0, 0, true)
	_watchdog.set_result_provider(func() -> Vector2i: return Vector2i(passed, failed))
	call_deferred("_run")


func _run() -> void:
	if DisplayServer.get_name() == "headless":
		printerr("RETAIL_SHROUD_RENDER FAIL this runner renders and cannot run headless")
		_watchdog.stop()
		quit(1)
		return
	await _render_and_assert()
	var ran := passed + failed
	if ran != EXPECTED_CHECKS:
		failed += 1
		printerr(
			"RETAIL_SHROUD_RENDER FAIL liveness: ran %d checks, expected %d - a coroutine aborted"
			% [ran, EXPECTED_CHECKS]
		)
	print("RETAIL_SHROUD_RENDER_RESULT passed=%d failed=%d" % [passed, failed])
	_watchdog.stop()
	quit(0 if failed == 0 else 1)


## The shipped terrain shader lives inside a `"""` literal in this file.
const TERRAIN_BUILDER_PATH := "res://src/retail_slice/retail_terrain_material_builder.gd"
const SHADER_FUNCTION_MARKER := "func _shader_source() -> String:"


func _test_shader() -> Shader:
	## The shipped terrain shader with its texture blend stubbed to white, so
	## ALBEDO reduces to the shroud modulate alone and a sampled pixel IS the
	## visibility value. Lighting is switched off on the material below, which
	## makes the `lighting` term exactly vec3(1.0).
	##
	## The source is read as TEXT rather than by preloading the builder class:
	## that class touches the `ModLoader` autoload, which does not exist in a
	## `--script` runner, so preloading it fails the whole file to compile. Text
	## extraction keeps the property that matters - the shader under test is
	## byte-for-byte the one the game ships - and the extraction itself is
	## asserted, so a refactor cannot silently leave this runner testing nothing.
	var file := FileAccess.get_file_as_string(TERRAIN_BUILDER_PATH)
	var function_index := file.find(SHADER_FUNCTION_MARKER)
	# The opening and closing delimiters are found by search rather than by
	# quoting the whole `func ...: return """` header, because the header spans a
	# line break and the file's line endings are not this runner's business.
	var body_start := -1 if function_index < 0 else file.find("\"\"\"", function_index)
	var body_end := -1 if body_start < 0 else file.find("\"\"\"", body_start + 3)
	_check(
		"the shipped terrain shader literal was located",
		function_index >= 0 and body_start >= 0 and body_end > body_start,
		"marker=%d open=%d close=%d in %s" % [
			function_index, body_start, body_end, TERRAIN_BUILDER_PATH
		]
	)
	if function_index < 0 or body_start < 0 or body_end <= body_start:
		return null
	body_start += 3
	var source := file.substr(body_start, body_end - body_start)
	_check(
		"the shipped terrain shader declares the shroud uniforms",
		source.contains("uniform sampler2D shroud_texture")
			and source.contains("uniform vec4 shroud_bounds")
			and source.contains("uniform bool shroud_enabled")
			and source.contains("shroud_visibility(sage_world_position)")
	)
	_check(
		"the terrain blend line this runner stubs is still in the shipped shader",
		source.contains(TERRAIN_BLEND_LINE),
		"the shader was refactored; re-quote TERRAIN_BLEND_LINE or this runner tests nothing"
	)
	if not source.contains(TERRAIN_BLEND_LINE):
		return null
	source = source.replace(TERRAIN_BLEND_LINE, "	vec3 terrain_color = vec3(1.0);")
	var shader := Shader.new()
	shader.code = source
	return shader


func _render_and_assert() -> void:
	# Size the window and the root viewport FIRST, and let two frames pass. Doing
	# this after the scene is built leaves `unproject_position` working against
	# the old viewport size while the captured image is the new one, which puts
	# every sample off the edge and reads back a uniformly black corner - the
	# render looks broken when it is fine.
	DisplayServer.window_set_mode(DisplayServer.WINDOW_MODE_WINDOWED)
	DisplayServer.window_set_size(VIEWPORT)
	get_root().size = VIEWPORT
	await process_frame
	await process_frame
	var shader := _test_shader()
	if shader == null:
		return

	# A grid with one cell of each state, built through the real model.
	var fog = Fog.new()
	fog.configure(Vector2(-40.0, -40.0), Vector2(40.0, 40.0), SLICE_SCALE)
	fog.enabled = true
	fog.begin_look_pass()
	fog.add_look(0, Vector2(-20.0, -20.0), 4.0)
	fog.commit_look_pass()
	fog.begin_look_pass()
	fog.add_look(0, Vector2(20.0, 20.0), 4.0)
	fog.commit_look_pass()
	var clear_point := Vector2(20.0, 20.0)
	var fogged_point := Vector2(-20.0, -20.0)
	var shrouded_point := Vector2(0.0, 0.0)
	_check(
		"the fixture grid carries one cell of each retail state",
		int(fog.state_at(0, clear_point)) == Fog.CLEAR
			and int(fog.state_at(0, fogged_point)) == Fog.FOGGED
			and int(fog.state_at(0, shrouded_point)) == Fog.SHROUDED,
		"clear=%d fogged=%d shrouded=%d" % [
			int(fog.state_at(0, clear_point)),
			int(fog.state_at(0, fogged_point)),
			int(fog.state_at(0, shrouded_point)),
		]
	)

	var overlay = Overlay.new()
	overlay.configure(fog, 0)
	_check("the overlay rebuilt its texture", overlay.update(true))

	var material := ShaderMaterial.new()
	material.shader = shader
	material.set_shader_parameter("sage_lighting_enabled", false)
	material.set_shader_parameter("passability_debug_enabled", false)
	overlay.apply_to_terrain(material)
	_check(
		"apply_to_terrain armed the shader",
		bool(material.get_shader_parameter("shroud_enabled"))
			and material.get_shader_parameter("shroud_texture") != null
	)

	# A quad lying in the XZ plane over exactly the grid's extent, viewed from
	# straight above through an orthogonal camera of the same extent, so world
	# XZ maps linearly to screen XY and a sample point's pixel is computable.
	var bounds: Rect2 = fog.grid_bounds()
	var root := get_root()
	var scene := Node3D.new()
	root.add_child(scene)
	var quad := MeshInstance3D.new()
	var mesh := ArrayMesh.new()
	var vertices := PackedVector3Array([
		Vector3(bounds.position.x, 0.0, bounds.position.y),
		Vector3(bounds.end.x, 0.0, bounds.position.y),
		Vector3(bounds.end.x, 0.0, bounds.end.y),
		Vector3(bounds.position.x, 0.0, bounds.position.y),
		Vector3(bounds.end.x, 0.0, bounds.end.y),
		Vector3(bounds.position.x, 0.0, bounds.end.y),
	])
	var normals := PackedVector3Array()
	var colors := PackedColorArray()
	var uvs := PackedVector2Array()
	for _index in range(vertices.size()):
		normals.append(Vector3.UP)
		colors.append(Color.BLACK)
		uvs.append(Vector2.ZERO)
	var arrays := []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = vertices
	arrays[Mesh.ARRAY_NORMAL] = normals
	arrays[Mesh.ARRAY_COLOR] = colors
	arrays[Mesh.ARRAY_TEX_UV] = uvs
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
	mesh.surface_set_material(0, material)
	quad.mesh = mesh
	scene.add_child(quad)

	var camera := Camera3D.new()
	camera.projection = Camera3D.PROJECTION_ORTHOGONAL
	camera.size = bounds.size.x
	camera.near = 0.1
	camera.far = 400.0
	camera.global_transform = Transform3D(
		Basis.looking_at(Vector3.DOWN, Vector3.FORWARD),
		Vector3(bounds.get_center().x, 100.0, bounds.get_center().y)
	)
	scene.add_child(camera)
	camera.current = true

	for _frame in range(12):
		await process_frame
	await RenderingServer.frame_post_draw
	var image := root.get_texture().get_image()
	if image == null or image.is_empty() 			or image.get_width() != VIEWPORT.x or image.get_height() != VIEWPORT.y:
		failed += 1
		printerr("RETAIL_SHROUD_RENDER FAIL the viewport produced %s instead of %s" % [
			Vector2i(image.get_width(), image.get_height()) if image != null else Vector2i(-1, -1),
			VIEWPORT,
		])
		return

	var brightest := 0.0
	for y in range(0, image.get_height(), 8):
		for x in range(0, image.get_width(), 8):
			brightest = maxf(brightest, image.get_pixel(x, y).r)
	print("RETAIL_SHROUD_RENDER_DIAG brightest=%.4f cam=%s screen_clear=%s screen_shroud=%s bounds=%s tex=%s" % [
		brightest,
		camera.global_transform.origin,
		camera.unproject_position(Vector3(clear_point.x, 0.0, clear_point.y)),
		camera.unproject_position(Vector3(shrouded_point.x, 0.0, shrouded_point.y)),
		fog.grid_bounds(),
		overlay.texture(),
	])
	# READ BACK IN LINEAR SPACE. The shader writes retail's visibility value
	# directly into ALBEDO, but `get_image()` hands back the sRGB-encoded
	# framebuffer, so the fog stop comes out at 0.733 rather than 0.498 (0.0 and
	# 1.0 are transfer fixed points, which is why only the middle stop looked
	# wrong). Converting here compares the value the SHADER produced against the
	# value retail authored, instead of comparing a display encoding to a number.
	var clear_pixel := _sample(image, camera, clear_point).srgb_to_linear()
	var fogged_pixel := _sample(image, camera, fogged_point).srgb_to_linear()
	var shrouded_pixel := _sample(image, camera, shrouded_point).srgb_to_linear()
	print("RETAIL_SHROUD_RENDER samples clear=%s fogged=%s shrouded=%s" % [
		clear_pixel, fogged_pixel, shrouded_pixel
	])
	# 4/255 of tolerance: the sampler interpolates between cell centres, so a
	# sample is only exactly on a stop when it lands on a centre, and the
	# framebuffer quantises to 8 bits.
	var tolerance := 0.02
	_check(
		"visible ground renders at retail's ClearAlpha (255 -> 1.0)",
		absf(clear_pixel.r - 1.0) <= tolerance,
		"r=%.4f" % clear_pixel.r
	)
	_check(
		"remembered ground renders at retail's FogAlpha (127 -> 0.498)",
		absf(fogged_pixel.r - 127.0 / 255.0) <= tolerance,
		"r=%.4f expected=%.4f" % [fogged_pixel.r, 127.0 / 255.0]
	)
	_check(
		"unexplored ground renders at retail's ShroudAlpha (0 -> black)",
		absf(shrouded_pixel.r) <= tolerance,
		"r=%.4f" % shrouded_pixel.r
	)
	_check(
		"the three states are ordered and distinct on screen",
		shrouded_pixel.r < fogged_pixel.r - 0.2 and fogged_pixel.r < clear_pixel.r - 0.2,
		"shroud=%.4f fog=%.4f clear=%.4f" % [shrouded_pixel.r, fogged_pixel.r, clear_pixel.r]
	)

	var output := ProjectSettings.globalize_path(
		"res://../workspace/scratch/opusfog-shroud-shader.png"
	)
	if DirAccess.make_dir_recursive_absolute(output.get_base_dir()) == OK:
		if image.save_png(output) == OK and FileAccess.file_exists(output):
			print("RETAIL_SHROUD_RENDER_ARTIFACT path=%s" % output)
	scene.queue_free()
	await process_frame


func _sample(image: Image, camera: Camera3D, world_xz: Vector2) -> Color:
	# `unproject_position` answers in the CAMERA VIEWPORT's coordinate space, and
	# this project's window/stretch settings do not let a `--script` runner force
	# that space to equal the captured image's pixel grid (measured: a 512x512
	# capture off a 1882-wide visible rect). Rescaling by the ratio is the honest
	# correction; clamping, which is what this runner did first, silently read
	# the frame's black corner and reported a working shader as broken.
	var screen := camera.unproject_position(Vector3(world_xz.x, 0.0, world_xz.y))
	var visible_size := camera.get_viewport().get_visible_rect().size
	if visible_size.x <= 0.0 or visible_size.y <= 0.0:
		return Color(-1.0, -1.0, -1.0, -1.0)
	var scaled := Vector2(
		screen.x * float(image.get_width()) / visible_size.x,
		screen.y * float(image.get_height()) / visible_size.y
	)
	var pixel := Vector2i(roundi(scaled.x), roundi(scaled.y))
	if pixel.x < 0 or pixel.y < 0 or pixel.x >= image.get_width() or pixel.y >= image.get_height():
		# Never clamp. A clamped sample reads the frame's corner and would report
		# "black" for a shroud that rendered perfectly, which is exactly the false
		# red this runner chased once already.
		printerr("RETAIL_SHROUD_RENDER FAIL sample %s projected to %s, outside the %dx%d capture" % [
			world_xz, pixel, image.get_width(), image.get_height()
		])
		return Color(-1.0, -1.0, -1.0, -1.0)
	return image.get_pixelv(pixel)


func _check(name: String, condition: bool, detail: String = "") -> void:
	_watchdog.note(name)
	if condition:
		passed += 1
		print("RETAIL_SHROUD_RENDER PASS %s" % name)
	else:
		failed += 1
		printerr(
			"RETAIL_SHROUD_RENDER FAIL %s%s" % [name, " (%s)" % detail if detail != "" else ""]
		)
