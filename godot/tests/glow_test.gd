## Retail's glow pass and the effects-LOD gates on the post chain (lane GLOW-1, stop S-5231). Needs a real display (the passes are compute shaders):
##
##   godot --path godot --script res://tests/glow_test.gd
##
## A bright sphere on the glow layer (W3DInstancer.set_model_glow puts the glow-flagged draws there) and a bright box off it, over a dark background. With the
## GlowEffect of RotWK's environment.ini the glow pass runs (the host reports the choice "glow", the pass count rises), the pixels beside the sphere are
## brighter than the same frame at EffectsLOD Low (choice "none"), the pixels beside the box are not, and a pixel far from both is unchanged. EffectsLOD
## VeryLow / Low draw nothing, Medium and up draw the glow. A block with TerrainGlow or MultipassGlowEnabled reports the S-5231 problem, a block with
## GlowEnabled = No does nothing. With the retail install (ROTWK_INSTALL / BFME2_INSTALL) a map with a colour grade ("map mp fall back 4p") runs the grade and
## not the glow, and its grade is skipped at a low effects LOD. A headless run prints SKIP (exit 77).
extends SceneTree

const EXIT_PASS := 0
const EXIT_FAIL := 1
const EXIT_SKIP := 77
## GlowPass::kGlowLayer, bit 18 (layer 19 in the editor's numbering); the default layer is bit 0
const GLOW_LAYER := 1 << 18

var _failures := 0

const RETAIL := {"enabled": true, "diameter": 15, "intensity": 1.3, "texture_width": 256, "radius_scale1": 0.18, "amplitude1": 0.06, "radius_scale2": 4.5,
	"amplitude2": 0.11, "terrain_glow": false, "multipass": false}


func _initialize() -> void:
	var code: int = await _run()
	print("GLOW1 %s" % ["PASS" if code == EXIT_PASS else ("SKIP" if code == EXIT_SKIP else "FAIL")])
	quit(code)


func _check(ok: bool, what: String) -> void:
	print(("  ok   " if ok else "  FAIL ") + what)
	if not ok:
		_failures += 1


func _frames(n: int) -> void:
	for i in n:
		await process_frame
	await RenderingServer.frame_post_draw


## RotWK's 15 tap kernel (test_glow1.cpp pins the same numbers): the sample positions and z (the 16th is the zero pad)
const KERNEL_X := [-7.0999999, -6.0999999, -5.0999999, -4.0999999, -3.0999999, -2.0999999, -1.10000002, -0.100000001, 0.899999976, 1.89999998, 2.9000001,
	3.9000001, 4.9000001, 5.9000001, 6.9000001, 0.0]
const KERNEL_Z := [0.0509308726, 0.0559427403, 0.0646251887, 0.079900071, 0.103428707, 0.132403284, 0.158165097, 0.169896811, 0.161936015, 0.138170242,
	0.10898675, 0.0839511305, 0.0670622215, 0.0572959259, 0.0517611392, 0.0]
const GLOW_N := 256

var _scene: Image
var _memo := {}


func _store8(v: float) -> float:
	return floorf(clampf(v, 0.0, 1.0) * 255.0 + 0.5) / 255.0


## the glow targets of the reference: level 0 = the scene point sampled at the texel centres, 1 = the horizontal pass, 2 = the vertical pass (CPU, ScreenHilightFilter's arithmetic)
func _texel(level: int, i: int, j: int) -> Vector3:
	i = clampi(i, 0, GLOW_N - 1)
	j = clampi(j, 0, GLOW_N - 1)
	var key := (level * GLOW_N + j) * GLOW_N + i
	if _memo.has(key):
		return _memo[key]
	var out := Vector3.ZERO
	if level == 0:
		var px := clampi(floori((i + 0.5) / GLOW_N * _scene.get_width()), 0, _scene.get_width() - 1)
		var py := clampi(floori((j + 0.5) / GLOW_N * _scene.get_height()), 0, _scene.get_height() - 1)
		var c: Color = _scene.get_pixel(px, py)
		out = Vector3(c.r, c.g, c.b)
	else:
		var u := (i + 0.5) / GLOW_N
		var v := (j + 0.5) / GLOW_N
		var acc := Vector3.ZERO
		for g in 4:
			var r := Vector3.ZERO
			for k in 4:
				var n: int = g * 4 + k
				var off := float(KERNEL_X[n]) * (1.0 / GLOW_N)
				var w := clampf(float(KERNEL_Z[n]) * 1.3, -1.0, 1.0)
				var sample: Vector3 = _bilinear(level - 1, u + (off if level == 1 else 0.0), v + (off if level == 2 else 0.0))
				r = (sample * w + r).clamp(Vector3(-1, -1, -1), Vector3(1, 1, 1))
			r = Vector3(_store8(r.x), _store8(r.y), _store8(r.z))
			if g == 0:
				acc = r
			else:
				var b := r + acc * (Vector3(1, 1, 1) - r)
				acc = Vector3(_store8(b.x), _store8(b.y), _store8(b.z))
		out = acc
	_memo[key] = out
	return out


func _bilinear(level: int, u: float, v: float) -> Vector3:
	var tx := u * GLOW_N - 0.5
	var ty := v * GLOW_N - 0.5
	var x0 := floori(tx)
	var y0 := floori(ty)
	var fx := tx - x0
	var fy := ty - y0
	var a: Vector3 = _texel(level, x0, y0).lerp(_texel(level, x0 + 1, y0), fx)
	var b: Vector3 = _texel(level, x0, y0 + 1).lerp(_texel(level, x0 + 1, y0 + 1), fx)
	return a.lerp(b, fy)


func _luma(c: Color) -> float:
	return c.r + c.g + c.b


## the summed brightness of a (2 * r + 1) square around (x, y)
func _patch(img: Image, x: int, y: int, r: int) -> float:
	var sum := 0.0
	for j in range(y - r, y + r + 1):
		for i in range(x - r, x + r + 1):
			sum += _luma(img.get_pixel(clampi(i, 0, img.get_width() - 1), clampi(j, 0, img.get_height() - 1)))
	return sum


func _bright_material() -> StandardMaterial3D:
	var m := StandardMaterial3D.new()
	m.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	m.albedo_color = Color(1.0, 0.85, 0.4)
	return m


func _run() -> int:
	if DisplayServer.get_name() == "headless":
		print("SKIP: needs a display (the glow passes run on the GPU)")
		return EXIT_SKIP
	if not ClassDB.class_exists("GlowPostEffectHost"):
		print("SKIP: the openbfme extension is not loaded")
		return EXIT_SKIP

	var vp := SubViewport.new()
	vp.size = Vector2i(640, 360)
	vp.own_world_3d = true
	vp.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	root.add_child(vp)
	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0.05, 0.05, 0.08)
	env.tonemap_mode = Environment.TONE_MAPPER_LINEAR
	var cam := Camera3D.new()
	cam.environment = env
	cam.position = Vector3(0, 0, 10)
	vp.add_child(cam)
	cam.current = true

	var sphere := MeshInstance3D.new()
	sphere.mesh = SphereMesh.new()
	sphere.material_override = _bright_material()
	sphere.position = Vector3(-2.5, 0, 0)
	sphere.layers = 1 | GLOW_LAYER
	vp.add_child(sphere)
	var box := MeshInstance3D.new()
	box.mesh = BoxMesh.new()
	box.material_override = _bright_material()
	box.position = Vector3(2.5, 0, 0)
	vp.add_child(box)

	var host: Node3D = ClassDB.instantiate("GlowPostEffectHost")
	host.configure_dictionary(RETAIL, null)
	host.set_effects_lod(4)
	vp.add_child(host)
	await _frames(10)

	# where the shapes land on the 640 x 360 frame
	var sphere_px := Vector2i(roundi(cam.unproject_position(sphere.position).x), roundi(cam.unproject_position(sphere.position).y))
	var box_px := Vector2i(roundi(cam.unproject_position(box.position).x), roundi(cam.unproject_position(box.position).y))
	var radius_px := roundi(abs(cam.unproject_position(sphere.position + Vector3(0.5, 0, 0)).x - sphere_px.x))
	var halo_offset := radius_px + 14 # beside the sphere, inside the blur's reach

	var st: Dictionary = host.get_glow_stats()
	_check(st.choice == "glow" and st.installed and st.active, "RotWK's block at EffectsLOD UltraHigh: the glow runs (%s)" % [st])
	_check(int(st.passes) > 0, "the pass ran (%d)" % int(st.passes))
	_check(str(st.problem).is_empty(), "no problem reported (%s)" % [st.problem])
	_check(int(st.kernel_samples) == 16 and int(st.texture_width) == 256, "the kernel of 15 taps (padded to 16) on a 256 texture (%s)" % [st])
	_check(PackedStringArray(st.unverified).size() == 4, "the unverified list is reported (three gaps and the device limit)")
	var effects: Array = cam.compositor.compositor_effects
	_check(effects.size() >= 1 and effects[effects.size() - 1].get_class() == "GlowPostEffect", "the glow is the camera's last effect (no grade on this scene)")
	var glow_img: Image = vp.get_texture().get_image()

	# the same frame at EffectsLOD Low: neither pass
	host.set_effects_lod(1)
	await _frames(6)
	st = host.get_glow_stats()
	_check(st.choice == "none" and not st.active, "EffectsLOD Low: neither pass (%s)" % [st])
	var plain_img: Image = vp.get_texture().get_image()

	var near_glow := _patch(glow_img, sphere_px.x + halo_offset, sphere_px.y, 2)
	var near_plain := _patch(plain_img, sphere_px.x + halo_offset, sphere_px.y, 2)
	_check(near_glow > near_plain + 0.5, "beside the glow-flagged sphere the glow adds light (%.3f vs %.3f)" % [near_glow, near_plain])
	var box_glow := _patch(glow_img, box_px.x - radius_px - 14, box_px.y, 2)
	var box_plain := _patch(plain_img, box_px.x - radius_px - 14, box_px.y, 2)
	# the box is not on the glow layer; its halo side is also far from the sphere's, so only the sphere's own glow could reach it
	_check(absf(box_glow - box_plain) < 0.05, "beside the box that is not glow-flagged nothing is added (%.3f vs %.3f)" % [box_glow, box_plain])
	var far_glow := _patch(glow_img, 8, 8, 2)
	var far_plain := _patch(plain_img, 8, 8, 2)
	_check(absf(far_glow - far_plain) < 0.01, "far from every shape the frame is unchanged (%.3f vs %.3f)" % [far_glow, far_plain])
	# the glow adds, never darkens: no pixel of the glow frame is darker than the plain one
	var darker := 0
	for y in range(0, glow_img.get_height(), 6):
		for x in range(0, glow_img.get_width(), 6):
			if _luma(glow_img.get_pixel(x, y)) < _luma(plain_img.get_pixel(x, y)) - 0.02:
				darker += 1
	_check(darker == 0, "the screen blend never darkens a pixel (%d darker)" % darker)

	# the GPU result against the CPU reference of ScreenHilightFilter's arithmetic: the glow scene the pass read, resampled, blurred twice, screen blended over the plain frame
	_scene = (host.get_node("GlowScene") as SubViewport).get_texture().get_image()
	_memo.clear()
	var worst := 0.0
	var tested := 0
	var lit := 0
	for dx in range(radius_px - 2, radius_px + 70, 7):
		for dy in [0, -16, 16, 40]:
			var x: int = sphere_px.x + dx
			var y: int = sphere_px.y + dy
			if x < 0 or y < 0 or x >= glow_img.get_width() or y >= glow_img.get_height():
				continue
			var gv := _bilinear(2, (x + 0.5) / glow_img.get_width(), (y + 0.5) / glow_img.get_height())
			var f: Color = plain_img.get_pixel(x, y)
			var expect := Vector3(minf(1.0, gv.x + f.r * (1.0 - gv.x)), minf(1.0, gv.y + f.g * (1.0 - gv.y)), minf(1.0, gv.z + f.b * (1.0 - gv.z)))
			var got: Color = glow_img.get_pixel(x, y)
			worst = maxf(worst, maxf(absf(got.r - expect.x), maxf(absf(got.g - expect.y), absf(got.b - expect.z))))
			tested += 1
			lit += 1 if gv.x > 0.02 else 0
	_check(tested >= 20 and lit >= 6, "the reference covers the halo (%d pixels, %d with glow above 2%%)" % [tested, lit])
	# tolerance: the GPU path is half float linear -> sRGB -> blend -> sRGB -> 8 bit, the reference is the 8 bit pictures: a few steps of 1/255
	_check(worst < 0.02, "the GPU glow matches the CPU reference of the passes (worst channel difference %.4f over %d pixels)" % [worst, tested])

	# the effects LOD gate
	for lod in [0, 1]:
		host.set_effects_lod(lod)
		await _frames(2)
		_check(host.get_choice() == "none", "EffectsLOD %d: none" % lod)
	for lod in [2, 3, 4]:
		host.set_effects_lod(lod)
		await _frames(2)
		_check(host.get_choice() == "glow", "EffectsLOD %d: glow" % lod)

	# GlowEnabled = No: nothing; a block the pass cannot draw: the problem is reported
	var off := RETAIL.duplicate()
	off.enabled = false
	host.configure_dictionary(off, null)
	await _frames(2)
	_check(host.get_choice() == "none", "GlowEnabled = No: none")
	var terrain := RETAIL.duplicate()
	terrain.terrain_glow = true
	host.configure_dictionary(terrain, null)
	await _frames(2)
	st = host.get_glow_stats()
	_check(str(st.problem).begins_with("S-5231: TerrainGlow"), "TerrainGlow = Yes is reported (%s)" % [st.problem])
	var multi := RETAIL.duplicate()
	multi.multipass = true
	host.configure_dictionary(multi, null)
	st = host.get_glow_stats()
	_check(str(st.problem).begins_with("S-5231: MultipassGlowEnabled"), "MultipassGlowEnabled = Yes is reported (%s)" % [st.problem])
	var bad := RETAIL.duplicate()
	bad.diameter = 0
	host.configure_dictionary(bad, null)
	await _frames(2)
	st = host.get_glow_stats()
	_check(not str(st.problem).is_empty() and not st.active, "GlowDiameter = 0 is a problem and the pass stays off (%s)" % [st.problem])
	# the device limit (not retail): past 128 taps the block is refused and reported before the kernel is built; 128 itself draws
	bad.diameter = 129
	host.configure_dictionary(bad, null)
	await _frames(2)
	st = host.get_glow_stats()
	_check(str(st.problem) == "GlowDiameter 129 is above the device limit of 128 taps" and not st.active, "GlowDiameter = 129 is refused and reported (%s)" % [st.problem])
	bad.diameter = 2147483647
	var t0 := Time.get_ticks_msec()
	host.configure_dictionary(bad, null)
	_check(Time.get_ticks_msec() - t0 < 1000 and str(host.get_glow_stats().problem).contains("2147483647"), "GlowDiameter = 2147483647 is refused at once (%d ms)" % [Time.get_ticks_msec() - t0])
	bad.diameter = 128
	host.configure_dictionary(bad, null)
	await _frames(4)
	st = host.get_glow_stats()
	_check(str(st.problem).is_empty() and st.active and int(st.kernel_samples) > 0, "GlowDiameter = 128 is inside the limit and draws (%s, %s samples)" % [st.problem, st.kernel_samples])
	host.configure_dictionary(RETAIL, null)
	await _frames(4)
	_check(host.get_choice() == "glow" and host.get_glow_stats().active, "back to RotWK's block: the glow runs again")

	# a refused GPU allocation (the kernel buffers) is reported, not a silent skip (PLAN rule 10)
	var passes_before := int(host.get_glow_stats().passes)
	host.simulate_gpu_allocation_failure(true)
	host.configure_dictionary(RETAIL, null) # a new kernel: the buffers are allocated again
	await _frames(6)
	st = host.get_glow_stats()
	_check(str(st.problem) == "S-5231: the glow kernel buffers could not be created; the glow pass is not drawn", "a failed kernel buffer allocation is reported (%s)" % [st.problem])
	var passes_failed := int(st.passes)
	await _frames(4)
	_check(int(host.get_glow_stats().passes) == passes_failed, "and the pass does not run on (%d passes)" % passes_failed)
	_check(passes_failed >= passes_before, "the passes already drawn stay counted (%d -> %d)" % [passes_before, passes_failed])
	host.simulate_gpu_allocation_failure(false)
	host.configure_dictionary(RETAIL, null) # reconfiguring clears the failure
	await _frames(6)
	st = host.get_glow_stats()
	_check(str(st.problem).is_empty() and int(st.passes) > passes_failed, "after the allocation works again the glow runs and no problem is reported (%s)" % [st.problem])

	# leaving the tree takes the glow off the camera
	vp.remove_child(host)
	await _frames(2)
	var left := false
	for e in cam.compositor.compositor_effects:
		left = left or e.get_class() == "GlowPostEffect"
	_check(not left, "the host leaving the tree takes the glow off the camera")
	host.free()

	var code := await _grade_part(vp, cam)
	if code == EXIT_PASS:
		code = await _whip_part()
	if code == EXIT_PASS:
		code = await _world_part()
	vp.queue_free()
	await _frames(2)
	if code != EXIT_PASS:
		return code
	return EXIT_PASS if _failures == 0 else EXIT_FAIL


## a map with a colour grade: the grade runs and the glow does not; at a low effects LOD the grade is skipped too
func _grade_part(vp: SubViewport, cam: Camera3D) -> int:
	if OS.get_environment("ROTWK_INSTALL").is_empty() or OS.get_environment("BFME2_INSTALL").is_empty():
		print("  SKIP the colour grade part: ROTWK_INSTALL / BFME2_INSTALL not set")
		return EXIT_PASS
	var fs: RefCounted = ClassDB.instantiate("RetailFileSystem")
	var mount: Dictionary = fs.mount_retail()
	_check(mount.ok, "mount_retail %s" % [mount.get("errors", [])])
	if not mount.ok:
		return EXIT_FAIL
	var builder: Object = ClassDB.instantiate("MapTerrainBuilder")
	var map: Node3D = builder.build_map(fs, "map mp fall back 4p", {})
	_check(map != null, "fall back: build_map")
	if map == null:
		return EXIT_FAIL
	vp.add_child(map)
	cam.far = 60000.0
	cam.position = Vector3(2000.0, 450.0, -1000.0)
	cam.look_at(Vector3(2000.0, 0.0, -1400.0))
	var grade_host: Node = map.get_node_or_null("PostEffects")
	_check(grade_host != null, "fall back: the map root has the PostEffects host")
	if grade_host == null:
		return EXIT_FAIL
	var host: Node3D = ClassDB.instantiate("GlowPostEffectHost")
	host.configure_dictionary(RETAIL, grade_host)
	host.set_effects_lod(4)
	vp.add_child(host)
	await _frames(10)
	var st: Dictionary = host.get_glow_stats()
	var gs: Dictionary = grade_host.get_post_effect_stats()
	_check(st.choice == "color_grade" and not st.active, "a graded map at UltraHigh: the grade, not the glow (%s)" % [st])
	_check(not gs.suppressed and int(gs.passes) > 0 and str(gs.problem).is_empty(), "the grade runs (%s)" % [gs])
	var before := int(gs.passes)
	host.set_effects_lod(1)
	await _frames(6)
	gs = grade_host.get_post_effect_stats()
	var after := int(gs.passes)
	st = host.get_glow_stats()
	_check(st.choice == "none" and gs.suppressed, "a graded map at EffectsLOD Low: neither (%s / %s)" % [st, gs])
	await _frames(6)
	_check(int(grade_host.get_post_effect_stats().passes) == after, "the suppressed grade stopped running (%d passes)" % after)
	host.set_effects_lod(3)
	await _frames(6)
	gs = grade_host.get_post_effect_stats()
	_check(host.get_choice() == "color_grade" and not gs.suppressed and int(gs.passes) > after, "back at High: the grade runs again (%s)" % [gs])
	# the order on the camera: the glow (installed only when it runs) is never after the grade
	var names: Array = []
	for e in cam.compositor.compositor_effects:
		names.append(e.get_class())
	_check(names.size() == 0 or names[names.size() - 1] == "LookupTablePostEffect", "the grade stays the camera's last effect (%s)" % [names])
	vp.remove_child(host)
	host.free()
	vp.remove_child(map)
	map.free()
	return EXIT_PASS


func _frame_light(img: Image) -> float:
	var sum := 0.0
	for y in range(0, img.get_height(), 2):
		for x in range(0, img.get_width(), 2):
			sum += _luma(img.get_pixel(x, y))
	return sum


## the Balrog's whip (neutralunits.ini ModuleTag_WhipDraw: GlowEnabled = Yes, GlowEmissive = Yes), a real W3D model through the instancer: set_model_glow puts its
## draws on the glow layer, so the pass lights the pixels around it; at EffectsLOD Low the same frame has no glow
func _whip_part() -> int:
	if OS.get_environment("ROTWK_INSTALL").is_empty() or OS.get_environment("BFME2_INSTALL").is_empty():
		return EXIT_PASS
	var fs: RefCounted = ClassDB.instantiate("RetailFileSystem")
	var mount: Dictionary = fs.mount_retail()
	if not mount.ok:
		_check(false, "mount_retail %s" % [mount.get("errors", [])])
		return EXIT_FAIL
	var vp := SubViewport.new()
	vp.size = Vector2i(640, 360)
	vp.own_world_3d = true
	vp.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	root.add_child(vp)
	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0.05, 0.05, 0.08)
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = Color.WHITE
	env.tonemap_mode = Environment.TONE_MAPPER_LINEAR
	var cam := Camera3D.new()
	cam.environment = env
	cam.far = 5000.0
	cam.position = Vector3(0, 40, 140)
	vp.add_child(cam)
	cam.current = true
	var inst: Node3D = ClassDB.instantiate("W3DInstancer")
	vp.add_child(inst)
	var setup: Dictionary = inst.setup(fs)
	_check(setup.ok, "whip: the instancer is set up %s" % [setup.get("errors", [])])
	var mid: int = inst.add_model("MUBalsWhip_SKN")
	_check(mid >= 0, "whip: MUBalsWhip_SKN builds")
	if mid < 0:
		vp.queue_free()
		return EXIT_FAIL
	var clip := "MUBalsWhip_SKL.MUBalsWhip_WIPA"
	var id: int = inst.add_instance(mid, Transform3D.IDENTITY, clip, 0.0, 1.0)
	inst.set_instance_pose(id, clip, 6.0, "", 0.0, 0.0)
	inst.update_now()
	var host: Node3D = ClassDB.instantiate("GlowPostEffectHost")
	host.configure_dictionary(RETAIL, null)
	host.set_effects_lod(1)
	vp.add_child(host)
	await _frames(10)
	var plain: Image = vp.get_texture().get_image()
	# GlowEmissive = Yes: the whip's mesh has an emissive material, so it keeps its place on the glow layer
	inst.set_model_glow(mid, true)
	host.set_effects_lod(4)
	await _frames(10)
	var st: Dictionary = host.get_glow_stats()
	_check(st.choice == "glow" and int(st.passes) > 0 and str(st.problem).is_empty(), "whip: the glow runs (%s)" % [st.choice])
	var lit: Image = vp.get_texture().get_image()
	var before := _frame_light(plain)
	var after := _frame_light(lit)
	_check(after > before + 5.0, "whip: the glow-flagged model lights the frame (%.1f vs %.1f)" % [after, before])
	vp.queue_free()
	await _frames(2)
	return EXIT_PASS


## the game world end to end: environment.ini's block is the world's active GlowEffect, the map's glow host is configured from it, a MordorRingWraith
## (obsolete/ringwraith.ini: GlowEnabled = Yes) is drawn from a model of its own on the glow layer, and GameWorld.set_effects_lod reaches the host
func _world_part() -> int:
	if OS.get_environment("ROTWK_INSTALL").is_empty() or OS.get_environment("BFME2_INSTALL").is_empty():
		print("  SKIP the game world part: ROTWK_INSTALL / BFME2_INSTALL not set")
		return EXIT_PASS
	var fs: RefCounted = ClassDB.instantiate("RetailFileSystem")
	if not fs.mount_retail().ok:
		_check(false, "mount_retail")
		return EXIT_FAIL
	var world: Node3D = ClassDB.instantiate("GameWorld")
	root.add_child(world)
	var setup: Dictionary = world.setup(fs)
	var loaded: Dictionary = world.load_map("map good celduin", {"seed": 5}) if setup.ok else {"ok": false}
	_check(setup.ok and loaded.ok, "world: setup and load_map %s" % [setup.get("errors", [])])
	if not (setup.ok and loaded.ok):
		world.queue_free()
		return EXIT_FAIL
	var cam := Camera3D.new()
	cam.far = 60000.0
	cam.position = Vector3(800.0, 400.0, 900.0)
	world.add_child(cam)
	cam.current = true
	var host: Node3D = world.get_glow_host()
	_check(host != null, "world: the loaded game has a glow host")
	if host == null:
		world.queue_free()
		return EXIT_FAIL
	var terrain: Node = world.get_node_or_null("Terrain") # load_map builds no terrain: no grade host either
	var graded: bool = terrain != null and terrain.get_node_or_null("PostEffects") != null
	var st: Dictionary = host.get_glow_stats()
	_check(st.glow_enabled and int(st.kernel_samples) == 16 and int(st.texture_width) == 256 and str(st.problem).is_empty(), "world: environment.ini's block is configured (%s)" % [st])
	_check(PackedStringArray(loaded.get("stops", [])).has("S-5231"), "world: the load report names the stop S-5231")
	var id: int = world.create_object("MordorRingWraith", 0, 800.0, 800.0, 0.0)
	world.advance(0.2) # publish the creation event and resolve its draw model
	_check(id >= 0, "world: a MordorRingWraith is created")
	await _frames(6) # the render side draws the new drawable
	var live := world.get_node_or_null("LiveObjects")
	var glowing := 0
	var plain := 0
	for c in live.get_children():
		if c is MultiMeshInstance3D and (c as MultiMeshInstance3D).multimesh.visible_instance_count > 0:
			if ((c as MultiMeshInstance3D).layers & GLOW_LAYER) != 0:
				glowing += 1
			else:
				plain += 1
	_check(glowing > 0, "world: the ring wraith's draws are on the glow layer (%d glowing, %d plain draws with instances)" % [glowing, plain])
	await _frames(6)
	var expected := "color_grade" if graded else "glow"
	_check(host.get_choice() == expected, "world: UltraHigh, %s (%s)" % [expected, host.get_choice()])
	world.set_effects_lod(1)
	await _frames(4)
	_check(host.get_choice() == "none" and host.get_effects_lod() == 1, "world: GameWorld.set_effects_lod(Low) reaches the host (%s)" % host.get_choice())
	world.set_effects_lod(3)
	await _frames(4)
	_check(host.get_choice() == expected, "world: High: %s again" % expected)
	world.queue_free()
	await _frames(2)
	return EXIT_PASS
