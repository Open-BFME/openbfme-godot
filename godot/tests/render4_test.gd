## The map's colour grade and hardware fog (lane RENDER-4, stops S-1650 / S-1651). Needs a real display and the retail installs:
##
##   DISPLAY=:1 ROTWK_INSTALL=... BFME2_INSTALL=... godot --path godot --script res://tests/render4_test.gd
##
## "map mp fall back 4p" carries a LookupTablePostEffect (VRivendell_vol.tga, BlendFactor 0.263) and map.ini fog (HardwareFogColor 246 / 220 / 190,
## 500 .. 1700). The built map reports the effect applied and the S-1650 stop, its PostEffects host installs the grade last in the camera's compositor
## (after the gamma pair) and the grade runs every frame without a problem; the map's fog is on (the W3D / particle fog globals take the same values);
## the pixels of a frame differ from the
## same frame with the grade taken off. "map mp evendim" has an empty PostEffectsChunk: nothing is installed. A headless run prints SKIP (exit 77).
extends SceneTree

const EXIT_PASS := 0
const EXIT_FAIL := 1
const EXIT_SKIP := 77

var _failures := 0


func _initialize() -> void:
	var code: int = await _run()
	print("RENDER4 %s" % ["PASS" if code == EXIT_PASS else ("SKIP" if code == EXIT_SKIP else "FAIL")])
	quit(code)


func _check(ok: bool, what: String) -> void:
	print(("  ok   " if ok else "  FAIL ") + what)
	if not ok:
		_failures += 1


func _frames(n: int) -> void:
	for i in n:
		await process_frame
	await RenderingServer.frame_post_draw


func _run() -> int:
	if DisplayServer.get_name() == "headless":
		print("SKIP: needs a display (the compositor effects run on the GPU)")
		return EXIT_SKIP
	if not ClassDB.class_exists("MapPostEffectsHost"):
		print("SKIP: the openbfme extension is not loaded")
		return EXIT_SKIP
	if OS.get_environment("ROTWK_INSTALL").is_empty() or OS.get_environment("BFME2_INSTALL").is_empty():
		print("SKIP: ROTWK_INSTALL / BFME2_INSTALL not set")
		return EXIT_SKIP
	var fs: RefCounted = ClassDB.instantiate("RetailFileSystem")
	var mount: Dictionary = fs.mount_retail()
	_check(mount.ok, "mount_retail %s" % [mount.get("errors", [])])
	if not mount.ok:
		return EXIT_FAIL

	var vp := SubViewport.new()
	vp.size = Vector2i(320, 180)
	vp.own_world_3d = true
	vp.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	root.add_child(vp)
	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color.BLACK
	env.tonemap_mode = Environment.TONE_MAPPER_LINEAR
	var cam := Camera3D.new()
	cam.environment = env
	cam.far = 60000.0
	vp.add_child(cam)
	cam.current = true

	var builder: Object = ClassDB.instantiate("MapTerrainBuilder")
	var map: Node3D = builder.build_map(fs, "map mp fall back 4p", {})
	_check(map != null, "fall back: build_map")
	if map == null:
		return EXIT_FAIL
	var rep: Dictionary = builder.get_report()
	var posts: Array = rep.get("post_effects", [])
	_check(posts.size() == 1 and posts[0].name == "LookupTablePostEffect" and posts[0].applied, "fall back: the LookupTablePostEffect is applied %s" % [posts])
	_check(PackedStringArray(rep.get("stops", [])).has("S-1650"), "fall back: the report names S-1650")
	_check(str(rep.get("object_fog", "")).begins_with("S-1651"), "fall back: the report names the object fog (S-1651)")
	var weather: Dictionary = rep.get("mesh", {})
	_check(bool(weather.get("fog_enabled", false)), "fall back: the map's fog is on (the same values feed the W3D fog globals)")

	vp.add_child(map)
	# look over the map centre from above, as the game camera does
	cam.position = Vector3(2000.0, 450.0, -1000.0)
	cam.look_at(Vector3(2000.0, 0.0, -1400.0))
	await _frames(8)
	var host: Node = map.get_node_or_null("PostEffects")
	_check(host != null, "fall back: the map root has the PostEffects host")
	if host == null:
		return EXIT_FAIL
	var stats: Dictionary = host.get_post_effect_stats()
	_check(stats.installed and int(stats.passes) > 0 and str(stats.problem).is_empty(), "fall back: the grade runs (%s)" % stats)
	_check(absf(float(stats.blend) - 0.263) < 1e-6, "fall back: BlendFactor 0.263")
	var effects: Array = cam.compositor.compositor_effects
	_check(effects.size() >= 3 and effects[effects.size() - 1].get_class() == "LookupTablePostEffect", "fall back: the grade is the camera's last effect (after the gamma decode)")
	var graded: Image = vp.get_texture().get_image()

	# the same frame without the grade: take the effect off the compositor for a few frames
	var list: Array = cam.compositor.compositor_effects
	var grade = list.pop_back()
	host.set_process_internal(false)
	cam.compositor.compositor_effects = list
	await _frames(4)
	var plain: Image = vp.get_texture().get_image()
	var diff := 0.0
	for y in range(0, plain.get_height(), 8):
		for x in range(0, plain.get_width(), 8):
			var a: Color = graded.get_pixel(x, y)
			var b: Color = plain.get_pixel(x, y)
			diff += absf(a.r - b.r) + absf(a.g - b.g) + absf(a.b - b.b)
	_check(diff > 0.5, "fall back: the grade changes the picture (summed difference %.3f)" % diff)
	list.push_back(grade)
	cam.compositor.compositor_effects = list
	host.set_process_internal(true)

	# lane GPUPERF-1: with the gamma pair running (a GammaCompositeHost keeps it; a white quad at alpha 0.25 is drawn in the transparent pass), the
	# decode is left to the grade (fused_passes counts them); with a pass-through effect between the decode and the grade the two run apart. Both show
	# the same picture of a still scene (the map hidden: its clouds and water move with the time; an opaque colour ramp behind the veil).
	map.visible = false
	var ramp := MeshInstance3D.new()
	var ramp_mesh := QuadMesh.new()
	ramp_mesh.size = Vector2(100, 60) # the camera's view at z -30 is 82 x 46: the ramp fills it, so the whole gradient is visible
	ramp.mesh = ramp_mesh
	var ramp_mat := ShaderMaterial.new()
	ramp_mat.shader = Shader.new()
	ramp_mat.shader.code = "shader_type spatial; render_mode unshaded, fog_disabled, cull_disabled; void fragment() { ALBEDO = vec3(fract(UV.x * 7.0), UV.y, 1.0 - UV.x); }"
	ramp.material_override = ramp_mat
	cam.add_child(ramp)
	ramp.position = Vector3(0, 0, -30)
	var keeper: Node = ClassDB.instantiate("GammaCompositeHost")
	vp.add_child(keeper)
	var veil := MeshInstance3D.new()
	var veil_mesh := QuadMesh.new()
	veil_mesh.size = Vector2(30, 40) # the left half of the view at z -20 (54 x 31), with a margin
	veil.mesh = veil_mesh
	var veil_mat := ShaderMaterial.new()
	veil_mat.shader = Shader.new()
	veil_mat.shader.code = "shader_type spatial; render_mode unshaded, fog_disabled, blend_mix, depth_draw_never, cull_disabled; void fragment() { ALBEDO = vec3(1.0); ALPHA = 0.25; }"
	veil.material_override = veil_mat
	cam.add_child(veil)
	veil.position = Vector3(-14.0, 0, -20) # left half of the picture: the ramp alone on the right
	await _frames(6)
	var f0: Dictionary = host.get_post_effect_stats()
	await _frames(6)
	var f1: Dictionary = host.get_post_effect_stats()
	# tolerance: one fused pass per rendered frame; the six awaited main-loop frames are not guaranteed to render six SubViewport frames on a shared,
	# loaded machine, so the fused count is a lower bound (a regression that fused twice per frame would still show >= 6, but it would break the
	# pixel comparison below, and f3 == f2 pins the separate path)
	_check(int(f1.fused_passes) - int(f0.fused_passes) >= 6, "fall back: the grade takes over the gamma decode (%d fused passes in 6 frames)" % (int(f1.fused_passes) - int(f0.fused_passes)))
	var fused: Image = vp.get_texture().get_image()
	# the ramp's whole gradient is in view and the veil lies on its left half only (review: with 4000-unit quads 20-30 units from the camera the test
	# sampled a few units around the ramp's centre, and the half-width veil still covered every pixel)
	veil.visible = false
	await _frames(4)
	var bare: Image = vp.get_texture().get_image()
	veil.visible = true
	await _frames(4)
	var lo := 1.0
	var hi := 0.0
	var worst_left := 0.0
	var worst_right := 0.0
	for y in range(10, bare.get_height() - 10, 7):
		for x in range(10, bare.get_width() - 10, 7):
			var b: Color = bare.get_pixel(x, y)
			lo = minf(lo, b.r)
			hi = maxf(hi, b.r)
			var f: Color = fused.get_pixel(x, y)
			if x < 140:
				# tolerance: the veil adds 25 % of white in retail's gamma space; the engine's encode/decode round trip is approximate (largest residual
				# 9 / 255 at the darkest reds), 10 / 255 still rejects a missing veil, a veil over the wrong half, or another alpha (0.5 would be ~64)
				var d := Color(f.r - b.r, f.g - b.g, f.b - b.b)
				var e := Color(0.25 * (1.0 - b.r), 0.25 * (1.0 - b.g), 0.25 * (1.0 - b.b))
				worst_left = maxf(worst_left, maxf(absf(d.r - e.r), maxf(absf(d.g - e.g), absf(d.b - e.b))))
			elif x > 180:
				worst_right = maxf(worst_right, maxf(absf(f.r - b.r), maxf(absf(f.g - b.g), absf(f.b - b.b))))
	# tolerance: the ramp's red channel is a seven-period sawtooth across the whole view, so its sampled span is 0.33 .. 0.99; half of that range is the
	# floor for a camera that sees most of the quad
	_check(hi - lo > 0.5, "fall back: the ramp spans its gradient (%0.2f .. %0.2f), not one colour" % [lo, hi])
	# tolerance: see the loop's note (the gamma encode/decode round trip leaves up to 9 / 255 at the darkest reds)
	_check(worst_left < 10.0 / 255.0, "fall back: the veil covers the left half and adds a quarter white (largest error %.2f / 255)" % (worst_left * 255.0))
	# tolerance: the right half is outside the veil: the same frame's pixels, so only readback rounding can differ
	_check(worst_right < 2.0 / 255.0, "fall back: the ramp outside the veil is unchanged (largest error %.2f / 255)" % (worst_right * 255.0))
	var shot := OS.get_environment("RENDER4_SHOT")
	if not shot.is_empty():
		_check(fused.save_png(shot) == OK, "fall back: screenshot saved to %s" % shot)
		_check(bare.save_png(shot.get_basename() + "-bare.png") == OK, "fall back: bare screenshot saved")
	var through_script := GDScript.new()
	through_script.source_code = "extends CompositorEffect\nfunc _init():\n\teffect_callback_type = EFFECT_CALLBACK_TYPE_POST_TRANSPARENT\nfunc _render_callback(_t, _d):\n\tpass\n"
	through_script.reload()
	var through: CompositorEffect = through_script.new()
	var with_through: Array = cam.compositor.compositor_effects
	with_through.insert(with_through.size() - 1, through)
	cam.compositor.compositor_effects = with_through
	await _frames(6)
	var f2: Dictionary = host.get_post_effect_stats()
	await _frames(6)
	var f3: Dictionary = host.get_post_effect_stats()
	# tolerance: the extra pass-through effect produces one decode pass per rendered frame; the frame count itself can vary on a loaded machine, so the
	# count only has to be strictly greater (the fused count staying exactly the same pins that this pair did not fuse)
	_check(int(f3.fused_passes) == int(f2.fused_passes) and int(f3.passes) > int(f2.passes), "fall back: with an effect between them the decode and the grade run apart (%d fused)" % (int(f3.fused_passes) - int(f2.fused_passes)))
	var apart: Image = vp.get_texture().get_image()
	var worst := 0.0
	var differing := 0
	for y in range(0, apart.get_height(), 2):
		for x in range(0, apart.get_width(), 2):
			var a: Color = fused.get_pixel(x, y)
			var b: Color = apart.get_pixel(x, y)
			var d := maxf(absf(a.r - b.r), maxf(absf(a.g - b.g), absf(a.b - b.b)))
			worst = maxf(worst, d)
			differing += 1 if d > 0.0 else 0
	print("  info fall back: %d of %d sampled pixels differ" % [differing, (apart.get_height() / 2) * (apart.get_width() / 2)])
	_check(differing == 0, "fall back: the fused grade shows the separate passes' picture exactly (largest difference %.2f / 255)" % (worst * 255.0))
	with_through.erase(through)
	cam.compositor.compositor_effects = with_through
	veil.queue_free()
	ramp.queue_free()
	map.visible = true
	keeper.queue_free()
	await _frames(4)

	# leaving the tree takes the grade off the camera
	vp.remove_child(map)
	await _frames(2)
	var left := false
	for e in cam.compositor.compositor_effects:
		left = left or e.get_class() == "LookupTablePostEffect"
	_check(not left, "fall back: the map leaving the tree takes the grade off the camera")
	map.free()

	var builder2: Object = ClassDB.instantiate("MapTerrainBuilder")
	var map2: Node3D = builder2.build_map(fs, "map mp evendim", {})
	_check(map2 != null and map2.get_node_or_null("PostEffects") == null, "evendim: no post effect, no host")
	var rep2: Dictionary = builder2.get_report()
	_check((rep2.get("post_effects", []) as Array).is_empty(), "evendim: no post effect reported")
	if map2 != null:
		map2.free()
	vp.queue_free()
	await _frames(2)
	return EXIT_PASS if _failures == 0 else EXIT_FAIL
