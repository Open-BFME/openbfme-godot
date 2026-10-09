## Pixel test of the Apt canvas device path (AptMenuPlayer.draw_test_ops): the vertex colour modulating a texture and the sibling draw
## order, rendered by the GPU and read back.  Canvas command counts cannot catch either defect.
##
##   godot --path godot --script res://tests/apt_canvas_pixel_test.gd        (needs a GPU and a window; NOT --headless)
##
## 1. The vertex colour (lane UI-2: RotWK folds the whole colour transform, its additive term included, into the vertex colour, RW 0x4A8AD5,
##    and the texture is multiplied by it; there is no separate additive pass): a half-grey, half-alpha texture drawn (a) with white and
##    (b) with vertex colour (1, 0.5, 1, 1) over black.  (b)'s green is half of (a)'s, its red and blue are (a)'s.
## 2. Draw order: 40 stacked, fully overlapping opaque quads, every second one in its own clip layer (a new sibling item each), with a
##    long clip layer in the middle.  Godot sorts siblings by draw index (0 unless set) with an unstable sort; the last quad must be what is
##    on screen.
##
## Exit codes: 0 pass, 1 fail, 77 skip (headless / no extension).
extends SceneTree

const EXIT_PASS := 0
const EXIT_FAIL := 1
const EXIT_SKIP := 77

var _failures := 0


func _initialize() -> void:
	var code := await _run()
	print("APT CANVAS PIXEL TEST %s" % ["PASS" if code == EXIT_PASS else ("SKIP" if code == EXIT_SKIP else "FAIL")])
	quit(code)


func _check(ok: bool, what: String) -> void:
	if ok:
		print("  ok   ", what)
	else:
		print("  FAIL ", what)
		_failures += 1


func _render(ops: Array, textures: Dictionary = {}) -> Image:
	var viewport := SubViewport.new()
	viewport.size = Vector2i(128, 128)
	viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	viewport.transparent_bg = false
	get_root().add_child(viewport)
	var player: Node2D = ClassDB.instantiate("AptMenuPlayer")
	player.auto_process = false
	viewport.add_child(player)
	for name in textures:
		player.add_test_texture(name, textures[name])
	player.draw_test_ops(ops)
	await process_frame
	await process_frame
	await RenderingServer.frame_post_draw
	var image := viewport.get_texture().get_image()
	viewport.queue_free()
	return image


func _run() -> int:
	if DisplayServer.get_name() == "headless":
		print("SKIP: needs a window and a GPU (run without --headless)")
		return EXIT_SKIP
	if not ClassDB.class_exists("AptMenuPlayer"):
		print("FAIL: the openbfme extension is not loaded")
		return EXIT_FAIL
	await process_frame

	# ---- 1. the vertex colour modulates the texture ---------------------------------------------------------------------------------
	var tex := Image.create(4, 4, false, Image.FORMAT_RGBA8)
	tex.fill(Color(0.5, 0.5, 0.5, 0.5))
	var black := {"kind": "mesh", "rect": Rect2(0, 0, 128, 128), "color": Color(0, 0, 0, 1)}
	var ops := [
		black,
		{"kind": "mesh", "rect": Rect2(0, 0, 32, 32), "color": Color(1, 1, 1, 1), "texture": "t"},
		{"kind": "mesh", "rect": Rect2(32, 0, 32, 32), "color": Color(1, 0.5, 1, 1), "texture": "t"},
	]
	var image: Image = await _render(ops, {"t": tex})
	var normal := image.get_pixel(16, 16)
	var tinted := image.get_pixel(48, 16)
	print("  info white %s, green halved %s" % [normal, tinted])
	_check(normal.r > 0.15 and normal.r < 0.40, "the half-grey half-alpha texture over black is visible (%.3f)" % normal.r)
	_check(absf(tinted.g - normal.g * 0.5) < 0.02, "the vertex colour's green 0.5 halves the texture's green (%.3f of %.3f)" % [tinted.g, normal.g])
	_check(absf(tinted.r - normal.r) < 0.02 and absf(tinted.b - normal.b) < 0.02, "red and blue are unchanged")

	# ---- 2. draw order ------------------------------------------------------------------------------------------------------------
	var stack: Array = [black]
	for i in 40:
		var c := Color(float(i) / 40.0, 0.0, 1.0 - float(i) / 40.0, 1.0)
		var op := {"kind": "mesh", "rect": Rect2(16, 16, 96, 96), "color": c}
		if i % 2 == 1 and (i < 20 or i > 30):
			# a clip layer of its own around the quad: a new sibling item
			stack.append({"kind": "mask_begin"})
			stack.append({"kind": "mesh", "rect": Rect2(0, 0, 128, 128), "color": Color(1, 1, 1, 1), "mask_shape": true})
			stack.append({"kind": "mask_content"})
			stack.append(op)
			stack.append({"kind": "mask_end"})
		else:
			stack.append(op)
		if i == 20:
			stack.append({"kind": "mask_begin"})
			stack.append({"kind": "mesh", "rect": Rect2(0, 0, 128, 128), "color": Color(1, 1, 1, 1), "mask_shape": true})
			stack.append({"kind": "mask_content"})
		if i == 30:
			stack.append({"kind": "mask_end"})
	stack.append({"kind": "mesh", "rect": Rect2(16, 16, 96, 96), "color": Color(0, 1, 0, 1)})
	var ordered: Image = await _render(stack)
	var centre := ordered.get_pixel(64, 64)
	print("  info stacked centre pixel ", centre)
	_check(centre.g > 0.9 and centre.r < 0.1 and centre.b < 0.1, "the last (green) quad of 40 overlapping siblings is on top")
	return EXIT_PASS if _failures == 0 else EXIT_FAIL
