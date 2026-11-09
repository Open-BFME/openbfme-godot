## Lane GPUPERF-2: the W3D instancer's draw culling (W3DInstancer.set_draw_culling). Three instances of a retail soldier, one in front of the camera,
## one far to its side, one behind it: with culling on only the one in view is in the MultiMeshes, and their boxes hold it; an extra frustum (the
## caster PiP's, set_extra_pose_frustum) looking at the side one brings it back at the next frame_pre_draw; the camera turning to it does too; with
## culling off every instance is drawn again. The draw order (get_draw_order) keeps every member either way. Review round: the visible count
## follows a compaction that leaves every member out, a manually updated instancer repacks when the culling is switched off, and an off-screen
## caster keeps its shadow batch and its shadow on the lit ground (the `_case_off_screen_shadow` block).
##
##   DISPLAY=:1 ROTWK_INSTALL=... BFME2_INSTALL=... godot --path godot --script res://tests/draw_cull_test.gd (the compaction runs at frame_pre_draw: it needs
## a display; a headless run prints SKIP)
extends SceneTree

const EXIT_PASS := 0
const EXIT_FAIL := 1
const EXIT_SKIP := 77

var _failures := 0


func _initialize() -> void:
	_run.call_deferred()


func _check(ok: bool, what: String) -> void:
	print(("  ok   " if ok else "  FAIL ") + what)
	if not ok:
		_failures += 1


func _drawn(inst: Node) -> Array:
	var out := []
	for c in inst.get_children():
		if c is MultiMeshInstance3D:
			out.append(c.multimesh.visible_instance_count)
	return out


func _frames(n: int) -> void:
	for i in n:
		await process_frame
	await RenderingServer.frame_post_draw


func _run() -> void:
	if DisplayServer.get_name() == "headless":
		print("SKIP: the compaction runs at frame_pre_draw (needs a display)")
		quit(EXIT_SKIP)
		return
	if OS.get_environment("ROTWK_INSTALL").is_empty() or OS.get_environment("BFME2_INSTALL").is_empty():
		print("SKIP: set ROTWK_INSTALL and BFME2_INSTALL")
		quit(EXIT_SKIP)
		return
	var fs: RefCounted = ClassDB.instantiate("RetailFileSystem")
	var mount: Dictionary = fs.mount_retail()
	_check(mount.ok, "mount_retail %s" % [mount.get("errors", [])])
	var camera := Camera3D.new()
	root.add_child(camera)
	camera.make_current()
	var inst: Node3D = ClassDB.instantiate("W3DInstancer")
	root.add_child(inst)
	inst.set_playing(false)
	_check(inst.setup(fs).ok, "setup")
	var model: int = inst.add_model("GUMAArms_SKN")
	_check(model >= 0, "the soldier model builds")
	var front: int = inst.add_instance(model, Transform3D(Basis.IDENTITY, Vector3(0, 0, -100)))
	var side: int = inst.add_instance(model, Transform3D(Basis.IDENTITY, Vector3(5000, 0, -100)))
	var behind: int = inst.add_instance(model, Transform3D(Basis.IDENTITY, Vector3(0, 0, 3000)))
	await _frames(3)
	var all: Array = _drawn(inst)
	_check(not all.is_empty() and all.all(func(n): return n == 3), "culling off: every instance drawn %s" % [all])
	inst.set_draw_culling(true)
	await _frames(3)
	var culled: Array = _drawn(inst)
	_check(culled.all(func(n): return n == 1), "culling on: only the instance in view is drawn %s" % [culled])
	_check(int(inst.get_stats().draw_culled) == 2 * culled.size(), "the stats count the slots left out (%d)" % int(inst.get_stats().draw_culled))
	_check(Array(inst.get_draw_order(model, 0)).size() == 3, "the draw order keeps every member")
	for c in inst.get_children():
		if c is MultiMeshInstance3D:
			var box: AABB = c.multimesh.custom_aabb
			_check(box.has_point(Vector3(0, 0, -100)) and not box.has_point(Vector3(5000, 0, -100)), "%s: the box holds the drawn instance only %s" % [c.name, box])
			break
	# lane GPUPERF-2 review: the visible count describes the draw (the model's draw items times the instances actually in the lists), also when the last
	# compaction leaves every member out, and it follows cull_draws' repacking
	var items: int = int(inst.get_model_report(model).draw_items)
	_check(int(inst.get_stats().visible_draw_instances) == items, "visible_draw_instances counts the one drawn instance (%d = %d draw items)" % [int(inst.get_stats().visible_draw_instances), items])
	camera.global_transform = Transform3D(Basis.IDENTITY, Vector3(0, 0, -10000))
	camera.look_at(Vector3(0, 0, 0)) # every instance is now beyond the far plane: the draw lists are empty, not "unavailable"
	await _frames(2)
	var st: Dictionary = inst.get_stats()
	_check(int(st.visible_draw_instances) == 0, "visible_draw_instances is 0 when every instance is off screen (%d)" % int(st.visible_draw_instances))
	_check(int(st.draw_culled) == 3 * items, "the stats count every slot left out (%d)" % int(st.draw_culled))
	camera.global_transform = Transform3D.IDENTITY
	await _frames(2)
	_check(int(inst.get_stats().visible_draw_instances) == items, "visible_draw_instances follows the camera back (%d)" % int(inst.get_stats().visible_draw_instances))
	# the PiP's frustum, looking at the side instance
	var pip := Camera3D.new()
	root.add_child(pip)
	pip.global_transform = Transform3D(Basis.IDENTITY, Vector3(5000, 0, 200))
	_check(pip.is_position_in_frustum(Vector3(5000, 0, -100)) and inst.set_extra_pose_frustum(pip.get_frustum()), "the extra frustum sees the side instance")
	await _frames(2)
	_check(_drawn(inst).all(func(n): return n == 2), "an extra frustum on the side instance draws it too %s" % [_drawn(inst)])
	inst.set_extra_pose_frustum([])
	await _frames(2)
	_check(_drawn(inst).all(func(n): return n == 1), "without the extra frustum it leaves again %s" % [_drawn(inst)])
	# the camera turning round
	camera.look_at(Vector3(0, 0, 3000))
	await _frames(2)
	_check(_drawn(inst).all(func(n): return n == 1), "the camera turned to the one behind: that one is drawn %s" % [_drawn(inst)])
	inst.set_draw_culling(false)
	await _frames(3)
	_check(_drawn(inst).all(func(n): return n == 3), "culling off again: every instance drawn %s" % [_drawn(inst)])
	# lane GPUPERF-2 review: a scene that is updated by hand (GameWorld's static layer, set_auto_update(false)) has no process or frame callback of its
	# own: switching the draw culling off must repack its instances at once, or the ones the last compaction left out stay missing until something else
	# calls update_now
	var manual: Node3D = ClassDB.instantiate("W3DInstancer")
	root.add_child(manual)
	manual.set_playing(false)
	_check(manual.setup(fs).ok, "manual: setup")
	var m2: int = manual.add_model("GUMAArms_SKN")
	manual.add_instance(m2, Transform3D(Basis.IDENTITY, Vector3(0, 0, -100)))
	manual.add_instance(m2, Transform3D(Basis.IDENTITY, Vector3(5000, 0, -100)))
	manual.add_instance(m2, Transform3D(Basis.IDENTITY, Vector3(0, 0, 3000)))
	manual.set_auto_update(false)
	manual.update_now()
	manual.set_draw_culling(true)
	await _frames(2)
	_check(_drawn(manual).all(func(n): return n == 1), "manual: culling on leaves the one in view %s" % [_drawn(manual)])
	manual.set_draw_culling(false) # no frame between: the setter itself must repack a manual scene
	_check(_drawn(manual).all(func(n): return n == 3), "manual: culling off restores every instance at once %s" % [_drawn(manual)])
	manual.queue_free()
	await _case_off_screen_shadow(inst, camera, model, front)
	print("DRAW CULL %s" % ["PASS" if _failures == 0 else "FAIL"])
	quit(EXIT_PASS if _failures == 0 else EXIT_FAIL)


## Lane GPUPERF-2 review (Greptile P1 "Off-screen objects lose shadows"): the shadow batches are built from every caster, not from the camera-visible
## instances. A soldier far above the view leaves a shadow at (-100, 0, -100) on the visible ground; the colour batch does not draw it. Before the fix the
## shadow-only MultiMesh held the in-view soldier only and the ground there stayed lit.
func _case_off_screen_shadow(inst: Node3D, camera: Camera3D, model: int, front: int) -> void:
	var caster: int = inst.add_instance(model, Transform3D(Basis.IDENTITY, Vector3(-100, 200, -100)))
	_check(caster >= 0, "shadow: the off-screen caster builds")
	inst.set_instance_cast_shadow(front, true)
	inst.set_instance_cast_shadow(caster, true)
	ClassDB.class_call_static("W3DInstancer", "set_shadow_maps_enabled", true)
	inst.set_draw_culling(true)
	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0.1, 0.1, 0.1)
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = Color(1, 1, 1)
	env.ambient_light_energy = 0.4
	env.tonemap_mode = Environment.TONE_MAPPER_LINEAR
	var we := WorldEnvironment.new()
	we.environment = env
	root.add_child(we)
	var ground := MeshInstance3D.new()
	var plane := PlaneMesh.new()
	plane.size = Vector2(600, 600)
	ground.mesh = plane
	var mat := StandardMaterial3D.new()
	mat.albedo_color = Color(0.8, 0.8, 0.8)
	mat.roughness = 1.0
	ground.material_override = mat
	root.add_child(ground)
	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-90, 0, 0) # straight down: the caster at y 200 throws its shadow onto the ground below it
	sun.shadow_enabled = true
	sun.light_energy = 1.2
	sun.directional_shadow_mode = DirectionalLight3D.SHADOW_ORTHOGONAL
	sun.directional_shadow_max_distance = 1000.0
	sun.shadow_bias = 0.05
	sun.shadow_normal_bias = 1.0
	root.add_child(sun)
	RenderingServer.directional_shadow_atlas_set_size(2048, false)
	camera.global_transform = Transform3D(Basis.IDENTITY, Vector3(0, 35, 20))
	camera.look_at(Vector3(0, 0, -100))
	await _frames(6)
	var casters := []
	for c in inst.get_children():
		if c is MultiMeshInstance3D and c.cast_shadow == GeometryInstance3D.SHADOW_CASTING_SETTING_SHADOWS_ONLY:
			for i in c.multimesh.visible_instance_count:
				casters.append(c.multimesh.get_instance_transform(i).origin)
	_check(casters.has(Vector3(-100, 200, -100)), "shadow: the off-screen caster stays in the shadow batches %s" % [casters])
	var image: Image = root.get_texture().get_image()
	var shot := OS.get_environment("DRAW_CULL_SHOT")
	if not shot.is_empty():
		_check(image.save_png(shot) == OK, "shadow: screenshot saved to %s" % shot)
	var shadowed: Color = image.get_pixel(clampi(int(camera.unproject_position(Vector3(-100, 0, -100)).x), 0, image.get_width() - 1),
		clampi(int(camera.unproject_position(Vector3(-100, 0, -100)).y), 0, image.get_height() - 1))
	var lit: Color = image.get_pixel(clampi(int(camera.unproject_position(Vector3(100, 0, -100)).x), 0, image.get_width() - 1),
		clampi(int(camera.unproject_position(Vector3(100, 0, -100)).y), 0, image.get_height() - 1))
	# tolerance: the lit sample is the same ground without either shadow; the gap the off-screen caster makes is 1.0 against 0.73, far above 0.06
	_check(shadowed.get_luminance() + 0.06 < lit.get_luminance(), "shadow: the off-screen caster darkens the ground (%s against %s)" % [_fmt(shadowed), _fmt(lit)])


func _fmt(c: Color) -> String:
	return "(%.3f, %.3f, %.3f)" % [c.r, c.g, c.b]
