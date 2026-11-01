## Pro-camera grain regression: generated fine detail, isolated lights and a focused foreground.
## No game files. Run with Forward+ and a window; --headless reports SKIP (77).
## Optional user arguments: --out=<directory> and --before=<previous pro_lens_effect.gd>.
## The optional baseline is for local review only; normal tests use a dense gather as reference.
extends SceneTree

const Lens := preload("res://scripts/pro_lens.gd")
const Effect := preload("res://scripts/pro_lens_effect.gd")
var failed := 0


func _check(ok: bool, what: String) -> void:
	print("BOKEH %s: %s" % [what, "ok" if ok else "FAIL"])
	if not ok:
		failed += 1


func _initialize() -> void:
	call_deferred("_run")


func _quad(size: Vector2, texture: Texture2D, at: Vector3) -> MeshInstance3D:
	var mesh := QuadMesh.new()
	mesh.size = size
	var material := StandardMaterial3D.new()
	material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	material.albedo_texture = texture
	material.texture_filter = BaseMaterial3D.TEXTURE_FILTER_NEAREST
	mesh.material = material
	var node := MeshInstance3D.new()
	node.mesh = mesh
	node.position = at
	return node


func _capture(camera: Camera3D, effect) -> Image:
	camera.compositor = Compositor.new()
	camera.compositor.compositor_effects = [effect]
	for i in 8:
		await process_frame
	RenderingServer.force_sync()
	return root.get_texture().get_image()


func _lens(script, taps: int):
	var e = script.new()
	e.focal = 85.0
	e.focus = 20.0
	e.aperture = 1.4
	e.taps = taps
	return e


func _run() -> void:
	if DisplayServer.get_name() == "headless" or RenderingServer.get_rendering_device() == null:
		print("BOKEH SKIP: needs Forward+ and a window")
		quit(77)
		return
	root.size = Vector2i(960, 540)
	var world := Node3D.new()
	root.add_child(world)
	var camera := Camera3D.new()
	world.add_child(camera)
	camera.current = true
	camera.keep_aspect = Camera3D.KEEP_WIDTH
	camera.fov = Lens.fov_from_focal(85.0)
	camera.near = 1.0
	camera.far = 60000.0
	var texture := Image.create(960, 540, false, Image.FORMAT_RGBA8)
	for y in 540:
		for x in 960:
			var v := 0.08 if (x / 2 + y / 2) % 2 == 0 else 0.35
			texture.set_pixel(x, y, Color(v * 0.65, v * 0.85, v))
	# Generated lights, including isolated ones with visible bokeh outlines.
	for center in [Vector2i(140, 120), Vector2i(360, 160), Vector2i(630, 110), Vector2i(810, 240), Vector2i(570, 400), Vector2i(180, 420)]:
		for y in range(-4, 5):
			for x in range(-4, 5):
				if x * x + y * y <= 16:
					texture.set_pixelv(center + Vector2i(x, y), Color(1.0, 0.85, 0.45))
	var width := 4000.0 * tan(deg_to_rad(camera.fov) / 2.0)
	world.add_child(_quad(Vector2(width, width * 9.0 / 16.0), ImageTexture.create_from_image(texture), Vector3(0, 0, -2000)))
	var sharp := Image.create(192, 200, false, Image.FORMAT_RGBA8)
	for y in 200:
		for x in 192:
			var v := 0.15 if (x / 6) % 2 == 0 else 0.8
			sharp.set_pixel(x, y, Color(v, v, v))
	var near_width := 40.0 * tan(deg_to_rad(camera.fov) / 2.0)
	world.add_child(_quad(Vector2(near_width * 0.2, near_width * 200.0 / 960.0), ImageTexture.create_from_image(sharp), Vector3(0, 0, -20)))
	var out := ""
	var before_path := ""
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--out="):
			out = arg.trim_prefix("--out=")
		if arg.begins_with("--before="):
			before_path = arg.trim_prefix("--before=")
	if not out.is_empty():
		var made := DirAccess.make_dir_recursive_absolute(out)
		_check(made == OK, "capture directory created")
	var after_effect = _lens(Effect, 128)
	var after := await _capture(camera, after_effect)
	if not out.is_empty():
		_check(after.save_png(out.path_join("after.png")) == OK, "after saved")
	var reference_effect = _lens(Effect, 2048)
	var reference := await _capture(camera, reference_effect)
	if not out.is_empty():
		_check(reference.save_png(out.path_join("reference.png")) == OK, "reference saved")
	if not before_path.is_empty():
		var before_effect = _lens(load(before_path), 128)
		var before := await _capture(camera, before_effect)
		if not out.is_empty():
			_check(before.save_png(out.path_join("before.png")) == OK, "before saved")
		print("BOKEH before background RMS: ", _error(before, reference, Rect2i(50, 250, 280, 100)))
	var background_error := _error(after, reference, Rect2i(50, 250, 280, 100))
	print("BOKEH after background RMS: ", background_error)
	# tolerance: half an 8-bit level in the smooth background; the old 128-tap gather exceeds this bound.
	_check(background_error < 0.5 / 255.0, "fine background detail resolves without visible grain")
	var stopped_down = _lens(Effect, 128)
	stopped_down.aperture = 0.0
	stopped_down.ev = 0.01 # exercise the no-DOF output path; compare against matching exposure below
	after_effect.ev = 0.01
	var exposed := await _capture(camera, after_effect)
	var sharp_frame := await _capture(camera, stopped_down)
	var background_change := _error(exposed, sharp_frame, Rect2i(50, 250, 280, 100))
	print("BOKEH background RMS versus stopped down: ", background_change)
	# tolerance: generated 2-pixel checkerboard must lose contrast, not pass with the effect disabled.
	_check(background_change > 0.01, "defocused detail actually blurs")
	var focused_error := _error(exposed, sharp_frame, Rect2i(400, 180, 160, 180))
	print("BOKEH focused RMS versus stopped down: ", focused_error)
	# tolerance: one 8-bit level for half-float intermediate rounding; focused stripes must retain their contrast.
	_check(focused_error < 1.0 / 255.0, "focused detail stays sharp")
	var edge_error := _error(exposed, sharp_frame, Rect2i(384, 190, 4, 160))
	print("BOKEH focused silhouette RMS: ", edge_error)
	# tolerance: one 8-bit level at the foreground silhouette, protecting it from the defocused background.
	_check(edge_error < 1.0 / 255.0, "focused silhouette stays sharp")
	root.size = Vector2i(800, 450)
	var resized := await _capture(camera, after_effect)
	_check(resized.get_size() == Vector2i(800, 450), "viewport reaches the requested size")
	var fresh_effect = _lens(Effect, 128)
	fresh_effect.ev = after_effect.ev
	var fresh := await _capture(camera, fresh_effect)
	var resize_error := _error(resized, fresh, Rect2i(8, 60, 784, 380))
	print("BOKEH resized RMS versus fresh effect: ", resize_error)
	# tolerance: one 8-bit level for GPU rounding; resized intermediates must render like a fresh effect.
	_check(resize_error < 1.0 / 255.0, "resized effect matches a fresh effect")
	var resized_sharp := await _capture(camera, stopped_down)
	# tolerance: the defocused checkerboard must still lose contrast after resizing, not silently stop rendering.
	_check(_error(resized, resized_sharp, Rect2i(40, 210, 230, 80)) > 0.01, "blur remains visible after resizing")
	_check(after_effect.problem == "" and reference_effect.problem == "", "shader and colour-buffer format accepted")
	camera.compositor = null
	print("BOKEH RESULT: %s (%d failed)" % ["PASS" if failed == 0 else "FAIL", failed])
	quit(0 if failed == 0 else 1)


func _error(a: Image, b: Image, region: Rect2i) -> float:
	var sum := 0.0
	for y in range(region.position.y, region.end.y):
		for x in range(region.position.x, region.end.x):
			var d := a.get_pixel(x, y).get_luminance() - b.get_pixel(x, y).get_luminance()
			sum += d * d
	return sqrt(sum / region.get_area())
