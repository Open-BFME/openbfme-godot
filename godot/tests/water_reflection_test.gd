## The planar water reflection on the GPU (lane WATER-REFLECT, stop S-035): the mirror camera, the reflection layer, the real water shader's sampling and the
## terrain shader's plane clip. Needs a GPU and a window (NOT --headless).
##
##   godot --path godot --script res://tests/water_reflection_test.gd
##
## Part 1 (no retail data): a water quad drawn with res://shaders/water_standing.gdshader (BumpSpecular, ReflectionStrength 1) at the plane y = 0, a WaterReflectionView,
## two boxes on the reflection layer. The expected pixel is worked out here from the mirror geometry (the ray from the camera through the box's mirror image meets the
## plane at W; W seen by the viewing camera is the pixel that must show the box), never read from the engine. A vertical flip, a mirrored x or a wrong plane lands elsewhere.
## Part 2: the real terrain shader (MapTerrainBuilder.build_test_patch, debug mode 7) seen by a camera whose cull mask is the reflection layer alone: drawn above the
## plane (global w3d_reflect_y), clipped below it.
##
## Part 3 (needs ROTWK_INSTALL and BFME2_INSTALL): W3D models through the W3DInstancer: the mirror and mirror-only instance bits and the plane clip of the generated shader.
##
## Exit codes: 0 pass, 1 fail, 77 skip (no display / no extension).
extends SceneTree

const EXIT_SKIP := 77
const REFLECTION_LAYER := 1 << 18

var _failures := 0


func _initialize() -> void:
	call_deferred("_run")


func _fail(message: String) -> void:
	_failures += 1
	push_error("FAIL: " + message)


func _check(ok: bool, message: String) -> void:
	if ok:
		print("  ok   ", message)
	else:
		_fail(message)


func _box(parent: Node, center: Vector3, size: float, color: Color, layers: int) -> MeshInstance3D:
	var mesh := BoxMesh.new()
	mesh.size = Vector3.ONE * size
	var material := StandardMaterial3D.new()
	material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	material.albedo_color = color
	mesh.material = material
	var instance := MeshInstance3D.new()
	instance.mesh = mesh
	instance.position = center
	instance.layers = layers
	parent.add_child(instance)
	return instance


func _water_material(shader: Shader) -> ShaderMaterial:
	var material := ShaderMaterial.new()
	material.shader = shader
	material.set_shader_parameter("bump_high", true)
	material.set_shader_parameter("reflection_strength", 1.0)
	material.set_shader_parameter("reflection_perturbation", 0.0)
	material.set_shader_parameter("blend_terrain_alpha", false)
	material.set_shader_parameter("material_diffuse", Vector4(0.15, 0.3, 0.35, 1.0))
	material.set_shader_parameter("output_gamma", 2.2)
	return material


## where the mirror image of `p` (in the plane y = plane_y) seen from `camera_pos` meets the plane
func _plane_hit(camera_pos: Vector3, p: Vector3, plane_y: float) -> Vector3:
	var mirrored := Vector3(p.x, 2.0 * plane_y - p.y, p.z)
	var t := (plane_y - camera_pos.y) / (mirrored.y - camera_pos.y)
	return camera_pos + (mirrored - camera_pos) * t


func _close(a: Color, b: Color, bytes: int) -> bool:
	var tol := bytes / 255.0
	return absf(a.r - b.r) <= tol and absf(a.g - b.g) <= tol and absf(a.b - b.b) <= tol # tolerance: 8-bit round trips through two encodes


func _patch_spec() -> Dictionary:
	var atlas := Image.create(16, 4, false, Image.FORMAT_RGBA8)
	atlas.fill(Color(1, 1, 1, 1))
	return {
		"base_atlas": atlas,
		"uv0": PackedVector2Array([Vector2(0.05, 0.1), Vector2(0.2, 0.1), Vector2(0.2, 0.9), Vector2(0.05, 0.9)]),
		"flip": false,
		"layers": [],
	}


func _part1() -> void:
	var stage := SubViewport.new()
	stage.size = Vector2i(800, 450)
	stage.own_world_3d = true
	stage.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	root.add_child(stage)
	# the water shader names globals the extension registers when the first W3D shader or the terrain loads: build a patch first
	var builder = ClassDB.instantiate("MapTerrainBuilder")
	var warm = builder.build_test_patch(_patch_spec()) # loads the terrain shader, which registers the globals
	if warm != null:
		warm.free()
	var plane_y := 12.0
	var shader: Shader = load("res://shaders/water_standing.gdshader")
	var water := MeshInstance3D.new()
	var quad := PlaneMesh.new()
	quad.size = Vector2(1600, 1600)
	var water_material := _water_material(shader)
	quad.material = water_material
	water.mesh = quad
	water.position = Vector3(0, plane_y, 0)
	stage.add_child(water)
	var red := Color(0.8, 0.15, 0.15)
	var green := Color(0.1, 0.7, 0.2)
	var red_pos := Vector3(80, 52, -60)
	var green_pos := Vector3(-150, 40, -200)
	_box(stage, red_pos, 30.0, red, 1 | REFLECTION_LAYER)
	_box(stage, green_pos, 40.0, green, 1 | REFLECTION_LAYER)
	var camera := Camera3D.new()
	camera.far = 5000.0
	stage.add_child(camera)
	camera.position = Vector3(0, 150, 300)
	camera.look_at(Vector3(0, plane_y, 0))
	camera.current = true
	var reflection = ClassDB.instantiate("WaterReflectionView")
	var half := 800.0
	var polygon := PackedVector3Array([Vector3(-half, plane_y, -half), Vector3(half, plane_y, -half), Vector3(half, plane_y, half), Vector3(-half, plane_y, half)])
	reflection.configure(plane_y, [polygon])
	stage.add_child(reflection)
	water_material.set_shader_parameter("reflection_tex", reflection.get_reflection_texture())
	water_material.set_shader_parameter("reflection_enabled", true)
	for frame in 8:
		await process_frame
	await RenderingServer.frame_post_draw
	var state: Dictionary = reflection.get_state()
	print("REFLECTION STATE ", state)
	_check(state.error == "" and state.updating and state.visible_polygons == 1, "the water polygon is in view: the pass runs")
	_check(state.size == Vector2i(910, 512), "the texture is 512 rows high and 512 x the viewing aspect wide (retail: 512 x 512)")
	# the mirror camera: the viewing camera reflected in y = 12, its up negated
	var mirror: Transform3D = reflection.get_reflection_camera_transform()
	_check(mirror.origin.is_equal_approx(Vector3(0, 2.0 * plane_y - 150.0, 300)), "the mirror camera sits at the mirror image of the viewing camera")
	_check(is_equal_approx(mirror.basis.determinant(), 1.0), "the mirror camera is a proper rotation")
	var up := camera.global_transform.basis.y
	_check(mirror.basis.y.is_equal_approx(-Vector3(up.x, -up.y, up.z)), "its up is the reflected up, negated")
	var image := stage.get_texture().get_image()
	for item in [[red_pos, red, "red"], [green_pos, green, "green"]]:
		var p: Vector3 = item[0]
		var hit := _plane_hit(camera.position, p, plane_y)
		var screen := camera.unproject_position(hit)
		var got := image.get_pixelv(Vector2i(roundi(screen.x), roundi(screen.y)))
		print("  ", item[2], " box: water point ", hit, " pixel ", screen, " shows ", got.to_html(false), " wants ", (item[1] as Color).to_html(false))
		_check(_close(got, item[1], 10), "the water shows the %s box at the pixel of its mirror image" % item[2])
	# a point of the water whose mirrored ray meets no box is black (the reflection camera clears to black)
	var empty_hit := _plane_hit(camera.position, Vector3(-60, 60, 0), plane_y)
	var empty_screen := camera.unproject_position(empty_hit)
	_check(empty_screen.x > 0.0 and empty_screen.x < 800.0 and empty_screen.y > 0.0 and empty_screen.y < 450.0, "the empty control point is on screen") # tolerance: inside the viewport
	var empty := image.get_pixelv(Vector2i(roundi(empty_screen.x), roundi(empty_screen.y)))
	_check(empty.r < 0.03 and empty.g < 0.03 and empty.b < 0.03, "water whose mirror ray meets nothing is black") # tolerance: 8-bit black
	# the camera turns away: no polygon in view, the pass stops (RW 0x47F695 answers false)
	camera.look_at(Vector3(0, 2000, 300), Vector3(0, 0, -1)) # straight up: the water is behind the camera
	for frame in 4:
		await process_frame
	state = reflection.get_state()
	_check(state.visible_polygons == 0 and not state.updating, "no water in view: the reflection stops updating")
	stage.queue_free()


func _part2() -> void:
	# the real terrain shader through the test patch (white base layer, debug mode 7), seen by a camera whose cull mask is the reflection layer alone
	var builder = ClassDB.instantiate("MapTerrainBuilder")
	var spec := _patch_spec()
	for case_data in [[-5.0, true, "plane below the terrain: drawn"], [5.0, false, "plane above the terrain: clipped"]]:
		var view := SubViewport.new()
		view.size = Vector2i(32, 32)
		view.own_world_3d = true
		view.render_target_update_mode = SubViewport.UPDATE_ALWAYS
		root.add_child(view)
		var patch: Node3D = builder.build_test_patch(spec)
		if patch == null:
			_fail("build_test_patch returned null")
			view.queue_free()
			return
		(patch as MeshInstance3D).layers = 1 | REFLECTION_LAYER
		view.add_child(patch)
		var camera := Camera3D.new()
		camera.projection = Camera3D.PROJECTION_ORTHOGONAL
		camera.size = 10.0
		camera.near = 0.1
		camera.far = 50.0
		camera.cull_mask = REFLECTION_LAYER
		view.add_child(camera)
		camera.position = Vector3(5.0, 10.0, -5.0)
		camera.rotation_degrees = Vector3(-90.0, 0.0, 0.0)
		camera.current = true
		RenderingServer.global_shader_parameter_set("w3d_reflect_y", float(case_data[0]))
		for frame in 6:
			await process_frame
		await RenderingServer.frame_post_draw
		var pixel := view.get_texture().get_image().get_pixel(16, 16)
		var lit: bool = pixel.r > 0.5 and pixel.g > 0.5 and pixel.b > 0.5 # tolerance: white against the black clear
		_check(lit == case_data[1], "terrain shader, reflection pass, " + case_data[2] + " (pixel " + pixel.to_html(false) + ")")
		# the same patch seen by a normal camera is drawn whatever the plane (the clip belongs to the reflection pass)
		camera.cull_mask = 0xFFFFF
		for frame in 4:
			await process_frame
		await RenderingServer.frame_post_draw
		pixel = view.get_texture().get_image().get_pixel(16, 16)
		_check(pixel.r > 0.5 and pixel.g > 0.5 and pixel.b > 0.5, "terrain shader, main camera, plane at %s: drawn (pixel %s)" % [str(case_data[0]), pixel.to_html(false)]) # tolerance: white against the black clear
		view.queue_free()
	RenderingServer.global_shader_parameter_set("w3d_reflect_y", -1.0e30)


## the pixels of a 128 x 128 view that differ from the clear colour, in the band around world x
func _lit_in_band(image: Image, clear: Color, world_x: float) -> int:
	var column := int((world_x + 20.0) / 120.0 * 128.0)
	var lit := 0
	for y in range(0, 128):
		for x in range(column - 8, column + 8):
			var c := image.get_pixel(x, y)
			if absf(c.r - clear.r) + absf(c.g - clear.g) + absf(c.b - clear.b) > 0.15: # tolerance: a drawn pixel against the clear colour
				lit += 1
	return lit


func _view(stage: SubViewport, cull_mask: int) -> SubViewport:
	var view := SubViewport.new()
	view.size = Vector2i(128, 128)
	view.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	stage.add_child(view) # own_world_3d false: the stage's world
	var camera := Camera3D.new()
	camera.projection = Camera3D.PROJECTION_ORTHOGONAL
	camera.size = 120.0
	camera.near = 0.1
	camera.far = 400.0
	camera.cull_mask = cull_mask
	view.add_child(camera)
	camera.position = Vector3(40.0, 200.0, 0.0)
	camera.rotation_degrees = Vector3(-90.0, 0.0, 0.0)
	camera.current = true
	return view


## W3D models through the instancer: the mirror bits (INSTANCE_CUSTOM.w) and the plane clip of the generated shader
func _part3() -> void:
	if OS.get_environment("ROTWK_INSTALL").is_empty() or OS.get_environment("BFME2_INSTALL").is_empty():
		print("SKIP part 3: needs ROTWK_INSTALL and BFME2_INSTALL")
		return
	var fs: RefCounted = ClassDB.instantiate("RetailFileSystem")
	if not fs.mount_retail().ok:
		_fail("retail mount")
		return
	var stage := SubViewport.new()
	stage.size = Vector2i(64, 64)
	stage.own_world_3d = true
	stage.render_target_update_mode = SubViewport.UPDATE_DISABLED
	root.add_child(stage)
	var instancer = ClassDB.instantiate("W3DInstancer")
	stage.add_child(instancer)
	var setup: Dictionary = instancer.setup(fs)
	_check(setup.ok, "the instancer sets up on the retail files")
	var model: int = instancer.add_model("ExCatRock01")
	_check(model >= 0 and instancer.get_model_report(model).errors.is_empty(), "the rock model builds") # tolerance: an index
	var plain: int = instancer.add_instance(model, Transform3D(Basis(), Vector3(0, 0, 0)), "", 0.0, 1.0)
	var mirror: int = instancer.add_instance(model, Transform3D(Basis(), Vector3(40, 0, 0)), "", 0.0, 1.0)
	var sky: int = instancer.add_instance(model, Transform3D(Basis(), Vector3(80, 0, 0)), "", 0.0, 1.0)
	instancer.set_instance_mirror(mirror, true, false)  # CAN_CAST_REFLECTIONS / the mirror flag
	instancer.set_instance_mirror(sky, true, true)      # SKYBOX (mirror and reflection-only)
	instancer.update_now()
	var main_view := _view(stage, 0xFFFFF)
	var reflection_view := _view(stage, REFLECTION_LAYER)
	RenderingServer.global_shader_parameter_set("w3d_reflect_y", -100.0)
	for frame in 8:
		await process_frame
	await RenderingServer.frame_post_draw
	var clear := Color(0, 0, 0)
	var empty_main := main_view.get_texture().get_image()
	clear = empty_main.get_pixel(2, 2) # the corner holds no model
	var main_image := main_view.get_texture().get_image()
	var reflection_image := reflection_view.get_texture().get_image()
	var counts := {
		"main": [_lit_in_band(main_image, clear, 0.0), _lit_in_band(main_image, clear, 40.0), _lit_in_band(main_image, clear, 80.0)],
		"reflection": [_lit_in_band(reflection_image, clear, 0.0), _lit_in_band(reflection_image, clear, 40.0), _lit_in_band(reflection_image, clear, 80.0)],
	}
	print("INSTANCER BANDS ", counts)
	# a main camera draws the plain and the mirror instance, not the reflection-only one (RW 0x470D53); the reflection camera draws the mirror and the sky
	_check(counts.main[0] > 0 and counts.main[1] > 0 and counts.main[2] == 0, "main camera: plain and mirror models drawn, the SKYBOX one hidden") # tolerance: pixel counts
	_check(counts.reflection[0] == 0 and counts.reflection[1] > 0 and counts.reflection[2] > 0, "reflection camera: only the mirror models, plain one absent") # tolerance: pixel counts
	# the plane above the models clips everything the reflection camera sees; the main camera is untouched
	RenderingServer.global_shader_parameter_set("w3d_reflect_y", 100.0)
	for frame in 6:
		await process_frame
	await RenderingServer.frame_post_draw
	main_image = main_view.get_texture().get_image()
	reflection_image = reflection_view.get_texture().get_image()
	_check(_lit_in_band(reflection_image, clear, 40.0) == 0 and _lit_in_band(reflection_image, clear, 80.0) == 0, "the reflection pass clips the models below the plane")
	_check(_lit_in_band(main_image, clear, 0.0) > 0 and _lit_in_band(main_image, clear, 40.0) > 0, "the main camera is not clipped") # tolerance: pixel counts
	RenderingServer.global_shader_parameter_set("w3d_reflect_y", -1.0e30)
	stage.queue_free()


func _run() -> void:
	create_timer(120).timeout.connect(func(): push_error("water reflection test timed out"); quit(1))
	if DisplayServer.get_name() == "headless" or not ClassDB.class_exists("WaterReflectionView"):
		print("SKIP: needs a window and the openbfme extension")
		quit(EXIT_SKIP)
		return
	await _part1()
	await _part2()
	await _part3()
	if _failures == 0:
		print("WATER REFLECTION PASS")
		quit(0)
	else:
		print("WATER REFLECTION FAILED: ", _failures)
		quit(1)
