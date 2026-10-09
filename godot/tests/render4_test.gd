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
