## Lane GPUPERF-2: the W3D instancer's draw culling (W3DInstancer.set_draw_culling). Three instances of a retail soldier, one in front of the camera,
## one far to its side, one behind it: with culling on only the one in view is in the MultiMeshes, and their boxes hold it; an extra frustum (the
## caster PiP's, set_extra_pose_frustum) looking at the side one brings it back at the next frame_pre_draw; the camera turning to it does too; with
## culling off every instance is drawn again. The draw order (get_draw_order) keeps every member either way.
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
	print("DRAW CULL %s" % ["PASS" if _failures == 0 else "FAIL"])
	quit(EXIT_PASS if _failures == 0 else EXIT_FAIL)
