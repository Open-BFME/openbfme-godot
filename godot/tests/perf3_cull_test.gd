## Lane PERF-3: W3DInstancer's pose culling changes no pose that can be seen.
##
##   godot --headless --path godot --script res://tests/perf3_cull_test.gd
##
## Two instancers hold the same 61 soldiers along a line, each playing its idle clip from its own start; one culls the poses outside the camera's frustum
## (set_pose_culling, as GameWorld's live objects do), the other poses everything. The clock runs while the camera looks at one end of the line, then at the
## other end: every instance whose origin is in the frustum has, after every update, the palette (its pivots' matrices) of the instancer that poses
## everything, bit for bit; a culled instance's bone query poses it on demand and answers as the other instancer does. Needs ROTWK_INSTALL and
## BFME2_INSTALL (else SKIP, exit 77). Exit codes: 0 pass, 1 fail, 77 skip.
extends SceneTree

const EXIT_PASS := 0
const EXIT_FAIL := 1
const EXIT_SKIP := 77

var _failures := 0


func _initialize() -> void:
	var code: int = await _run()
	print("PERF3 CULL %s" % ["PASS" if code == EXIT_PASS else ("SKIP" if code == EXIT_SKIP else "FAIL")])
	quit(code)


func _check(ok: bool, what: String) -> void:
	print("  %s %s" % ["ok  " if ok else "FAIL", what])
	if not ok:
		_failures += 1


func _run() -> int:
	if OS.get_environment("ROTWK_INSTALL").is_empty() or OS.get_environment("BFME2_INSTALL").is_empty():
		print("SKIP: set ROTWK_INSTALL and BFME2_INSTALL")
		return EXIT_SKIP
	if not ClassDB.class_exists("RetailFileSystem"):
		print("FAIL: the openbfme extension is not loaded")
		return EXIT_FAIL
	var fs: RefCounted = ClassDB.instantiate("RetailFileSystem")
	var mount: Dictionary = fs.mount_retail()
	if not mount.ok:
		print("FAIL: mount_retail: ", mount.errors)
		return EXIT_FAIL
	# the soldier of the smoke test (GondorFighter's default model, posed by the GUManMocap clips)
	var ini: String = fs.read_file("data\\ini\\object\\goodfaction\\units\\men\\gondorfighter.ini").get_string_from_utf8()
	var model_name := RetailIni.find_default_model(ini, "GondorFighter")
	if model_name.is_empty():
		print("FAIL: no default model for GondorFighter")
		return EXIT_FAIL
	var camera := Camera3D.new()
	root.add_child(camera)
	camera.make_current()
	var culled: Node3D = ClassDB.instantiate("W3DInstancer")
	var full: Node3D = ClassDB.instantiate("W3DInstancer")
	var ids: Array[int] = []
	var where: Array[Vector3] = []
	for inst in [culled, full]:
		root.add_child(inst)
		_check(inst.setup(fs).ok, "setup")
		inst.set_playing(false)
		inst.set_auto_update(false)
		var model: int = inst.add_model(model_name)
		if model < 0:
			print("FAIL: the soldier model did not build: ", inst.get_errors())
			return EXIT_FAIL
		for i in 61:
			var p := Vector3(-3000.0 + 100.0 * float(i), 0.0, -150.0)
			var id: int = inst.add_instance(model, Transform3D(Basis.IDENTITY, p), "GUManMocap_IDLB", 0.13 * float(i), 1.0)
			if inst == culled:
				ids.append(id)
				where.append(p)
	culled.set_pose_culling(true)
	full.set_pose_culling(false)
	var compared := 0
	var culled_seen := 0
	var mismatches := 0
	for step in 40:
		# the first half looks at the line's left end, the second half at its right end (the instances there were culled while the clock ran)
		camera.position = Vector3(-2400.0 if step < 20 else 2100.0, 40.0, 0.0)
		await process_frame
		var t := 0.07 * float(step)
		for inst in [culled, full]:
			inst.set_global_time(t)
			inst.update_now()
		culled_seen = maxi(culled_seen, int(culled.get_stats().get("pose_culled", 0)))
		for k in ids.size():
			if not camera.is_position_in_frustum(where[k]):
				continue
			compared += 1
			if culled.get_instance_palette(ids[k]) != full.get_instance_palette(ids[k]):
				mismatches += 1
	_check(compared > 40, "on-screen instances compared over 40 updates: %d" % compared)
	_check(culled_seen > 40, "the culling instancer left poses pending (at most %d in an update)" % culled_seen)
	_check(mismatches == 0, "every on-screen instance's palette equals the unculled instancer's (%d mismatches)" % mismatches)
	# the clock moves while the right end is off screen, then stops; the right end comes into view on the stopped clock
	camera.position = Vector3(-2400.0, 40.0, 0.0)
	await process_frame
	for inst in [culled, full]:
		inst.set_global_time(4.0)
		inst.update_now()
	camera.position = Vector3(2100.0, 40.0, 0.0)
	await process_frame
	for inst in [culled, full]:
		inst.update_now()
	var stopped_compared := 0
	var stopped_mismatches := 0
	for k in ids.size():
		if camera.is_position_in_frustum(where[k]):
			stopped_compared += 1
			if culled.get_instance_palette(ids[k]) != full.get_instance_palette(ids[k]):
				stopped_mismatches += 1
	_check(stopped_compared > 0 and stopped_mismatches == 0, "the stopped clock's poses are shown when culled instances come into view (%d compared, %d mismatches)" % [stopped_compared, stopped_mismatches])
	# explicit poses (GameWorld's way: set_instance_pose, no clock) requested while the right end is off screen are shown when it comes into view
	camera.position = Vector3(-2400.0, 40.0, 0.0)
	await process_frame
	for inst in [culled, full]:
		for k in ids.size():
			inst.set_instance_pose(ids[k], "GUManMocap_IDLB", 5.0 + float(k), "", 0.0, 0.0)
		inst.update_now()
	camera.position = Vector3(2100.0, 40.0, 0.0)
	await process_frame
	var explicit_compared := 0
	var explicit_mismatches := 0
	for inst in [culled, full]:
		inst.update_now()
	for k in ids.size():
		if camera.is_position_in_frustum(where[k]):
			explicit_compared += 1
			if culled.get_instance_palette(ids[k]) != full.get_instance_palette(ids[k]):
				explicit_mismatches += 1
	_check(explicit_compared > 0 and explicit_mismatches == 0, "explicit poses asked for off screen are shown when on screen (%d compared, %d mismatches)" % [explicit_compared, explicit_mismatches])
	# a culled instance asked for a bone is posed on demand
	camera.position = Vector3(-2400.0, 40.0, 0.0)
	await process_frame
	for inst in [culled, full]:
		inst.set_global_time(3.33)
		inst.update_now()
	var far_id: int = ids[ids.size() - 1]
	_check(not camera.is_position_in_frustum(where[ids.size() - 1]), "the last soldier is off screen")
	var bones_equal := true
	for b in 23:
		bones_equal = bones_equal and culled.get_bone_position(far_id, b) == full.get_bone_position(far_id, b)
	_check(bones_equal, "a culled instance's bone query answers with the pose of the current clock")
	var reported := false
	for w in culled.get_warnings():
		reported = reported or String(w).begins_with("[S-1730] ")
	var full_reported := false
	for w in full.get_warnings():
		full_reported = full_reported or String(w).begins_with("[S-1730] ")
	_check(reported and not full_reported, "the culling instancer reports stop S-1730 in get_warnings(), the other does not")
	_check(culled.get_errors().is_empty() and full.get_errors().is_empty(), "no instancer errors")
	return EXIT_PASS if _failures == 0 else EXIT_FAIL
