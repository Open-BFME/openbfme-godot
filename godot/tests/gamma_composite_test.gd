## Gamma-space compositing of the transparent pass (lane RENDER-3, stop S-831; review r2). Needs a real display:
##
##   DISPLAY=:1 ROTWK_INSTALL=... BFME2_INSTALL=... godot --path godot --script res://tests/gamma_composite_test.gd
##
## Retail (D3D9, no sRGB conversion) blends the displayed gamma values: white at alpha 0.25 over a displayed grey 0.5 shows 0.5 + 0.25 * 0.5 = 0.625,
## an opaque grey 0.5 stays 0.5. Cases (each in its own own_world_3d SubViewport, so each installs on its own camera):
##   * MSAA off / 2x / 4x: the opaque grey and the white quarter over it (the encode rewrites every sample of the multisample attachment);
##   * a MapTerrainBuilder map alone, and the same with a W3DInstancer next to it: the map root installs the pair, every road / river / standing water
##     material hands over gamma values (output_gamma 1), and the road shader's grey 0.5 at alpha 0.5 over grey 0.5 shows 0.5.
## A headless run prints SKIP (exit 77).
extends SceneTree

const EXIT_PASS := 0
const EXIT_FAIL := 1
const EXIT_SKIP := 77
const TOL := 2.0 / 255.0
const GREY_LINEAR := 0.21404114 # the linear value Godot encodes to the displayed 0.5

var _failures := 0


func _initialize() -> void:
	var code: int = await _run()
	print("GAMMA COMPOSITE %s" % ["PASS" if code == EXIT_PASS else ("SKIP" if code == EXIT_SKIP else "FAIL")])
	quit(code)


func _check(ok: bool, what: String) -> void:
	print(("  ok   " if ok else "  FAIL ") + what)
	if not ok:
		_failures += 1


func _frames(n: int) -> void:
	for i in n:
		await process_frame
	await RenderingServer.frame_post_draw


## A SubViewport with its own world, a black linear environment and an orthographic camera looking down -Z at the origin.
func _viewport() -> Array:
	var vp := SubViewport.new()
	vp.size = Vector2i(128, 128)
	vp.own_world_3d = true
	vp.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	root.add_child(vp)
	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color.BLACK
	env.tonemap_mode = Environment.TONE_MAPPER_LINEAR
	var cam := Camera3D.new()
	cam.environment = env
	cam.projection = Camera3D.PROJECTION_ORTHOGONAL
	cam.size = 2.0
	cam.position = Vector3(0, 0, 4)
	vp.add_child(cam)
	cam.current = true
	return [vp, cam]


func _quad(parent: Node, z: float, code: String) -> MeshInstance3D:
	var mi := MeshInstance3D.new()
	var mesh := QuadMesh.new()
	mesh.size = Vector2(8, 8)
	mi.mesh = mesh
	mi.position.z = z
	var mat := ShaderMaterial.new()
	mat.shader = Shader.new()
	mat.shader.code = code
	mi.material_override = mat
	parent.add_child(mi)
	return mi


func _centre(vp: SubViewport) -> Color:
	var img := vp.get_texture().get_image()
	return img.get_pixel(vp.size.x / 2, vp.size.y / 2)


func _near(c: Color, v: float) -> bool:
	return absf(c.r - v) <= TOL and absf(c.g - v) <= TOL and absf(c.b - v) <= TOL


func _ours(cam: Camera3D) -> int:
	var n := 0
	if cam.compositor != null:
		for e in cam.compositor.compositor_effects:
			if e != null and e.get_class() == "GammaCompositeEffect":
				n += 1
	return n


func _run() -> int:
	if DisplayServer.get_name() == "headless":
		print("SKIP: this test renders; run it with DISPLAY=:1, not --headless")
		return EXIT_SKIP
	await _case_msaa(false)
	await _case_msaa(true)
	await _case_two_viewports()
	await _case_retirement()
	var rotwk := OS.get_environment("ROTWK_INSTALL")
	if rotwk.is_empty() or OS.get_environment("BFME2_INSTALL").is_empty():
		print("SKIP (terrain cases): set ROTWK_INSTALL and BFME2_INSTALL")
	else:
		var fs: RefCounted = ClassDB.instantiate("RetailFileSystem")
		var mount: Dictionary = fs.mount_retail()
		_check(mount.ok, "mount_retail %s" % [mount.get("errors", [])])
		if mount.ok:
			await _case_terrain(fs, false)
			await _case_terrain(fs, true)
	return EXIT_PASS if _failures == 0 else EXIT_FAIL


## Opaque displayed grey 0.5 (linear 0.214 from an opaque shader), then white ALPHA 0.25 in front of it (a transparent-pass shader handing over the gamma
## value 1.0): 0.5 and 0.625 with MSAA off, 2x and 4x; the viewport keeps the requested MSAA (nothing is turned off). Review r3: in both MSAA encodes
## (the default per pixel from the resolved image, and the exact per-sample one of the project setting openbfme/rendering/gamma_exact_msaa_edges).
## An opaque edge through the centre of pixel column 64 (displayed grey 0.8 left of it over 0.5, half of the 2x / 4x standard samples covered) tells
## them apart: the exact encode shows retail's average of displayed values (0.8 + 0.5) / 2 = 0.65, the default one Godot's linear average 0.6716.
func _case_msaa(exact: bool) -> void:
	ProjectSettings.set_setting("openbfme/rendering/gamma_exact_msaa_edges", exact)
	var mode := "exact" if exact else "default"
	var v := _viewport()
	var vp: SubViewport = v[0]
	vp.add_child(ClassDB.instantiate("W3DInstancer")) # an installer of the pair
	_quad(vp, -1.0, "shader_type spatial; render_mode unshaded, fog_disabled; void fragment() { ALBEDO = vec3(%s); }" % GREY_LINEAR)
	var front := _quad(vp, 0.0, "shader_type spatial; render_mode unshaded, fog_disabled, blend_mix, depth_draw_never; void fragment() { ALBEDO = vec3(1.0); ALPHA = 0.25; }")
	var edge := _quad(vp, -0.5, "shader_type spatial; render_mode unshaded, fog_disabled; void fragment() { ALBEDO = vec3(0.603827); }")
	(edge.mesh as QuadMesh).size = Vector2(4, 8)
	edge.position.x = 1.0 / 128.0 - 2.0 # its right edge through the centre of pixel column 64
	for aa in [Viewport.MSAA_DISABLED, Viewport.MSAA_2X, Viewport.MSAA_4X]:
		vp.msaa_3d = aa
		front.visible = false
		edge.visible = false
		await _frames(8)
		var g := _centre(vp)
		_check(_near(g, 0.5), "%s MSAA %d: an opaque displayed grey 0.5 stays 0.5 under the pair: %.4f" % [mode, aa, g.r])
		front.visible = true
		await _frames(8)
		var w := _centre(vp)
		_check(_near(w, 0.625), "%s MSAA %d: white at alpha 0.25 over grey 0.5 shows retail's 0.625: %.4f" % [mode, aa, w.r])
		_check(vp.msaa_3d == aa, "%s MSAA %d: the viewport keeps the requested MSAA (%d)" % [mode, aa, vp.msaa_3d])
		if aa != Viewport.MSAA_DISABLED:
			front.visible = false
			edge.visible = true
			await _frames(8)
			var e := vp.get_texture().get_image().get_pixel(64, 64)
			var want := 0.65 if exact else 0.6716
			_check(_near(e, want), "%s MSAA %d: the opaque edge pixel shows %.4f (expected %.4f)" % [mode, aa, e.r, want])
	var cam: Camera3D = v[1]
	_check(_ours(cam) == 2, "%s: the pair is installed once on the camera (%d effects of ours)" % [mode, _ours(cam)])
	var lines := "\n".join(_fx_unverified())
	_check(not lines.contains("MSAA is turned off"), "%s: no MSAA fallback was reported" % mode)
	_check(lines.contains("opaque geometry edges keep Godot's linear-space sample average") != exact, "%s: the default encode's edge line is reported only in the default mode" % mode)
	vp.queue_free()
	await _frames(2)
	ProjectSettings.set_setting("openbfme/rendering/gamma_exact_msaa_edges", false)


## Review r3 (leak): two SubViewports render at the same time with 2x and 4x MSAA. After a warm-up nothing of the encode is created again (framebuffers,
## pipelines; the attachments alternate on the shared effect) during 600 more frames, and the video memory does not grow.
func _case_two_viewports() -> void:
	var hosts := []
	var vps := []
	for aa in [Viewport.MSAA_2X, Viewport.MSAA_4X]:
		var v := _viewport()
		var vp: SubViewport = v[0]
		vp.msaa_3d = aa
		var host = ClassDB.instantiate("GammaCompositeHost")
		vp.add_child(host)
		hosts.append(host)
		vps.append(vp)
		_quad(vp, -1.0, "shader_type spatial; render_mode unshaded, fog_disabled; void fragment() { ALBEDO = vec3(%s); }" % GREY_LINEAR)
	await _frames(60)
	var before: Dictionary = hosts[0].get_gamma_stats()
	var mem0 := Performance.get_monitor(Performance.RENDER_VIDEO_MEM_USED)
	await _frames(600)
	var after: Dictionary = hosts[0].get_gamma_stats()
	var mem1 := Performance.get_monitor(Performance.RENDER_VIDEO_MEM_USED)
	print("  info two viewports: stats before %s after %s, video memory %.1f -> %.1f MB" % [before, after, mem0 / 1048576.0, mem1 / 1048576.0])
	_check(after.msaa_encodes - before.msaa_encodes >= 1000, "the two MSAA viewports were encoded every frame (%d encodes)" % (after.msaa_encodes - before.msaa_encodes))
	_check(after.framebuffers_created == before.framebuffers_created, "no framebuffer is created per frame (%d -> %d)" % [before.framebuffers_created, after.framebuffers_created])
	_check(after.pipelines_created == before.pipelines_created, "no pipeline is created per frame (%d -> %d)" % [before.pipelines_created, after.pipelines_created])
	_check(after.sample_copies_created == before.sample_copies_created, "no sample copy is created per frame")
	_check(absf(mem1 - mem0) < 1048576.0, "the video memory stays put (%.2f MB change)" % ((mem1 - mem0) / 1048576.0))
	for vp in vps:
		vp.queue_free()
	await _frames(2)


## Review r4: the exact mode's per-attachment sample copies are retired within a few frames when (a) the exact mode is switched off, (b) the viewport turns
## MSAA off, (c) the last MSAA viewport is deleted, without any new MSAA viewport (no MSAA callback) to trigger it.
func _case_retirement() -> void:
	var host_probe = null
	for scenario in ["exact mode off", "MSAA off", "viewport deleted"]:
		ProjectSettings.set_setting("openbfme/rendering/gamma_exact_msaa_edges", true)
		var vps := []
		var hosts := []
		for aa in [Viewport.MSAA_2X, Viewport.MSAA_4X]:
			var v := _viewport()
			var vp: SubViewport = v[0]
			vp.size = Vector2i(512, 512)
			vp.msaa_3d = aa
			var host = ClassDB.instantiate("GammaCompositeHost")
			vp.add_child(host)
			_quad(vp, -1.0, "shader_type spatial; render_mode unshaded, fog_disabled; void fragment() { ALBEDO = vec3(%s); }" % GREY_LINEAR)
			vps.append(vp)
			hosts.append(host)
		await _frames(20)
		var warm: Dictionary = hosts[0].get_gamma_stats()
		var mem_warm := Performance.get_monitor(Performance.RENDER_VIDEO_MEM_USED)
		_check(warm.sample_copies_live >= 2, "%s: the exact mode holds a sample copy per MSAA attachment (%d live)" % [scenario, warm.sample_copies_live])
		match scenario:
			"exact mode off":
				ProjectSettings.set_setting("openbfme/rendering/gamma_exact_msaa_edges", false)
				await _frames(1) # ensure() (main thread) reads the setting; the viewports keep their MSAA and stay encoded in the default mode
			"MSAA off":
				for vp in vps:
					vp.msaa_3d = Viewport.MSAA_DISABLED
			"viewport deleted":
				for vp in vps:
					vp.queue_free()
				vps.clear()
		await _frames(4)
		var stats: Dictionary = hosts[0].get_gamma_stats() if is_instance_valid(hosts[0]) else warm
		# the host is gone with a deleted viewport: read the global counter through a fresh host
		if not is_instance_valid(hosts[0]):
			if host_probe == null:
				host_probe = ClassDB.instantiate("GammaCompositeHost")
			stats = host_probe.get_gamma_stats()
		var mem_after := Performance.get_monitor(Performance.RENDER_VIDEO_MEM_USED)
		print("  info %s: live copies %d -> %d, video memory %.1f -> %.1f MB" % [scenario, warm.sample_copies_live, stats.sample_copies_live, mem_warm / 1048576.0, mem_after / 1048576.0])
		_check(stats.sample_copies_live == 0, "%s: every sample copy is released within 4 frames, with no new MSAA viewport (%d live)" % [scenario, stats.sample_copies_live])
		for vp in vps:
			vp.queue_free()
		await _frames(3)
	if host_probe != null:
		host_probe.free()
	ProjectSettings.set_setting("openbfme/rendering/gamma_exact_msaa_edges", false)


func _fx_unverified() -> PackedStringArray:
	var fx = ClassDB.instantiate("FXPlayer")
	var fs: RefCounted = ClassDB.instantiate("RetailFileSystem")
	var lines: PackedStringArray = []
	if OS.get_environment("ROTWK_INSTALL").is_empty():
		fx.free()
		return lines
	var mount: Dictionary = fs.mount_retail()
	if mount.ok:
		fx.setup(fs)
		lines = fx.get_unverified()
	fx.free()
	return lines


## A map built by MapTerrainBuilder alone (and with a W3DInstancer beside it): the map root installs the pair; every road, river and standing water
## material has output_gamma 1; a road-shader quad (grey 0.5 at alpha 0.5, the map's road output_gamma) over an opaque grey 0.5 shows 0.5.
func _case_terrain(fs: RefCounted, with_instancer: bool) -> void:
	var what := "terrain + W3DInstancer" if with_instancer else "terrain only"
	var v := _viewport()
	var vp: SubViewport = v[0]
	var cam: Camera3D = v[1]
	var builder: RefCounted = ClassDB.instantiate("MapTerrainBuilder")
	var map: Node3D = builder.build_map(fs, "map wor fangorn", {"fog": false})
	_check(map != null, "%s: build_map" % what)
	if map == null:
		vp.queue_free()
		return
	_check(map.get_class() == "GammaCompositeHost", "%s: the map root is a GammaCompositeHost (%s)" % [what, map.get_class()])
	map.position = Vector3(100000, 0, 0) # out of the camera's view: only its installation and materials are under test here
	vp.add_child(map)
	if with_instancer:
		vp.add_child(ClassDB.instantiate("W3DInstancer"))
	await _frames(4)
	_check(_ours(cam) == 2, "%s: the pair is installed once on the camera (%d)" % [what, _ours(cam)])
	var counts := {"road": 0, "river": 0, "water_standing": 0}
	var wrong := []
	var road_gamma := -1.0
	var stack: Array = [map]
	while not stack.is_empty():
		var n: Node = stack.pop_back()
		for c in n.get_children():
			stack.push_back(c)
		if not (n is MeshInstance3D):
			continue
		var mi := n as MeshInstance3D
		var mats: Array = []
		if mi.material_override != null:
			mats.append(mi.material_override)
		if mi.mesh != null:
			for s in mi.mesh.get_surface_count():
				if mi.get_active_material(s) != null:
					mats.append(mi.get_active_material(s))
		for m in mats:
			if not (m is ShaderMaterial) or m.shader == null:
				continue
			for k in counts.keys():
				if m.shader.resource_path.ends_with(k + ".gdshader"):
					counts[k] += 1
					var g = m.get_shader_parameter("output_gamma")
					if g == null or absf(float(g) - 1.0) > 1e-6:
						wrong.append("%s %s" % [k, g])
					if k == "road":
						road_gamma = float(g)
	print("  info %s: transparent terrain materials %s" % [what, counts])
	_check(counts.road + counts.river + counts.water_standing > 0, "%s: the map has road / river / standing water materials" % what)
	_check(wrong.is_empty(), "%s: every road / river / standing water material has output_gamma 1 %s" % [what, wrong])
	# the road shader with the map's output_gamma over an opaque grey 0.5
	_quad(vp, -1.0, "shader_type spatial; render_mode unshaded, fog_disabled; void fragment() { ALBEDO = vec3(%s); }" % GREY_LINEAR)
	var road := MeshInstance3D.new()
	var mesh := QuadMesh.new()
	mesh.size = Vector2(8, 8)
	road.mesh = mesh
	var rm := ShaderMaterial.new()
	rm.shader = load("res://shaders/road.gdshader")
	var img := Image.create(2, 2, false, Image.FORMAT_RGBA8)
	img.fill(Color(0.5, 0.5, 0.5, 0.5))
	rm.set_shader_parameter("road_tex", ImageTexture.create_from_image(img))
	rm.set_shader_parameter("output_gamma", road_gamma if road_gamma > 0.0 else 1.0)
	road.material_override = rm
	vp.add_child(road)
	await _frames(8)
	var c := _centre(vp)
	_check(_near(c, 0.5), "%s: the road shader's grey 0.5 at alpha 0.5 over grey 0.5 shows retail's 0.5: %.4f" % [what, c.r])
	vp.queue_free()
	await _frames(2)
