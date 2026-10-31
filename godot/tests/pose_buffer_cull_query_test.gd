## Actual-frustum regression: a queried culled pose is consumed by another instance's model-wide rewrite.
## Restoring the queried instance's old pose must compare against the state that was actually submitted.
extends SceneTree

var _failures := 0
var _checks := 0
var _snapshots: Array = []


func _initialize() -> void:
	_run.call_deferred()


func _check(ok: bool, what: String) -> void:
	_checks += 1
	if not ok:
		_failures += 1
		print("FAIL ", what)


func _record(inst: Node3D, label: String) -> void:
	var cpu: Array = inst.get_draw_buffers()
	var resources: Array = []
	for child in inst.get_children():
		if child is MultiMeshInstance3D:
			resources.append(child.multimesh)
	_check(resources.size() == cpu.size(), label + ": resource count")
	var draws: Array = []
	for k in cpu.size():
		_check(resources[k].buffer.to_byte_array() == cpu[k].buffer.to_byte_array(), label + ": resource bytes")
		_check(resources[k].visible_instance_count == cpu[k].order.size(), label + ": resource visible count")
		draws.append([Array(cpu[k].order), cpu[k].buffer.to_byte_array().hex_encode(), resources[k].visible_instance_count])
	_snapshots.append([label, draws, inst.get_instance_palette(0).to_byte_array().hex_encode(), inst.get_instance_palette(1).to_byte_array().hex_encode()])
	print("CULL QUERY ", label, " orders=", Array(cpu[0].order), ",", Array(cpu[1].order), " culled=", inst.get_stats().pose_culled)


func _run() -> void:
	var fs: RefCounted = ClassDB.instantiate("RetailFileSystem")
	var mount: Dictionary = fs.mount_retail()
	_check(mount.ok, "mount: %s" % [mount.get("errors", [])])
	if not mount.ok:
		quit(1)
		return
	var camera := Camera3D.new()
	root.add_child(camera)
	camera.make_current()
	for mode in ["visibility", "fade"]:
		camera.position.x = 0
		await process_frame
		var inst: Node3D = ClassDB.instantiate("W3DInstancer")
		root.add_child(inst)
		inst.set_auto_update(false)
		inst.set_playing(false)
		_check(inst.setup(fs).ok, "setup")
		_check(inst.add_model("PB_OPAQUE") == 0, "synthetic opaque model")
		_check(inst.add_instance(0, Transform3D(Basis.IDENTITY, Vector3(10000, 0, -20)), "PB_MOVE", 0.0, 1.0) == 0, "instance A")
		_check(inst.add_instance(0, Transform3D(Basis.IDENTITY, Vector3(0, 0, -20)), "PB_MOVE", 0.0, 1.0) == 1, "instance B")
		inst.update_now()
		_record(inst, mode + ":initial")
		_check(not camera.is_position_in_frustum(Vector3(10000, 0, -20)), "actual frustum excludes A")
		_check(camera.is_position_in_frustum(Vector3(0, 0, -20)), "actual frustum includes B")
		inst.set_pose_culling(true)
		var clip := "PB_VIS" if mode == "visibility" else "PB_FADE"
		var frame := 1.0 if mode == "visibility" else 2.0
		var bone := 2 if mode == "visibility" else 1
		var draw := 1 if mode == "visibility" else 0
		_check(inst.set_instance_pose(0, clip, frame, "", 0.0, 0.0), "request hidden or zero fade pose")
		inst.get_bone_position(0, bone)
		inst.set_instance_opacity(1, 0.5)
		inst.update_now()
		_record(inst, mode + ":queried_culled_rewrite")
		_check(inst.get_stats().pose_culled == 1, "A excluded from StaleScratch by actual frustum")
		_check(inst.get_stats().pose_evaluations == 0, "rewrite consumes queried pose without an update evaluation")
		_check(Array(inst.get_draw_buffers()[draw].order) == [1], "rewrite consumes queried membership")
		_check(inst.set_instance_pose(0, clip, 3.0, "", 0.0, 0.0), "restore previously cached visible pose")
		camera.position.x = 10000
		await process_frame
		_check(camera.is_position_in_frustum(Vector3(10000, 0, -20)), "actual frustum includes A again")
		inst.update_now()
		_record(inst, mode + ":returned")
		_check(inst.get_stats().pose_evaluations == 1, "A evaluated on return")
		_check(Array(inst.get_draw_buffers()[draw].order) == [0, 1], mode + ": previously cached pose restores membership")
		var resources: Array = []
		for child in inst.get_children():
			if child is MultiMeshInstance3D:
				resources.append(child.multimesh)
		_check(resources[draw].visible_instance_count == 2, mode + ": returned resource draws both instances")
		_check(inst.get_errors().is_empty(), "no engine errors")
		inst.free()
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--snapshot="):
			var output := FileAccess.open(arg.trim_prefix("--snapshot="), FileAccess.WRITE)
			output.store_string(JSON.stringify(_snapshots))
	camera.free()
	print("CULL QUERY %s checks=%d failures=%d scenarios=2" % ["PASS" if _failures == 0 else "FAIL", _checks, _failures])
	quit(0 if _failures == 0 else 1)
