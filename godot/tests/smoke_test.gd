## Headless smoke test against real retail files.
##
##   godot --headless --path godot --script res://tests/smoke_test.gd
##
## Needs ROTWK_INSTALL and BFME2_INSTALL. Without them it prints SKIP and exits 77; it never
## passes silently. Exit codes: 0 pass, 1 fail, 77 skip.
##
## Truth for the geometry check is the W3D file itself: this script walks the raw bytes in
## GDScript (independent of the C++ loader) and compares every mesh header's vertex and
## triangle counts with what the extension built.
extends SceneTree

const EXIT_PASS := 0
const EXIT_FAIL := 1
const EXIT_SKIP := 77

const UNIT_INI := "data\\ini\\object\\goodfaction\\units\\men\\gondorfighter.ini"
const UNIT_OBJECT := "GondorFighter"
const PATCHED_INI := "data\\ini\\weapon.ini" # shipped by INI.big AND _patch201ini.big (and BFME2 ini.big)
const MISSING_TEXTURE_MODEL := "CUCrow_A"
const MISSING_TEXTURE := "CUCrow.tga"
const W3D_CHUNK_MESH := 0x0
const W3D_CHUNK_MESH_HEADER3 := 0x1F

var _failures := 0


func _initialize() -> void:
	var code: int = await _run()
	print("SMOKE %s" % ["PASS" if code == EXIT_PASS else ("SKIP" if code == EXIT_SKIP else "FAIL")])
	quit(code)


func _check(ok: bool, what: String) -> void:
	if ok:
		print("  ok   ", what)
	else:
		print("  FAIL ", what)
		_failures += 1


func _run() -> int:
	var rotwk := OS.get_environment("ROTWK_INSTALL")
	var bfme2 := OS.get_environment("BFME2_INSTALL")
	if rotwk.is_empty() or bfme2.is_empty():
		print("SKIP: set ROTWK_INSTALL and BFME2_INSTALL to run the retail smoke test (ROTWK_INSTALL='%s', BFME2_INSTALL='%s')" % [rotwk, bfme2])
		return EXIT_SKIP
	if not ClassDB.class_exists("RetailFileSystem"):
		print("FAIL: the openbfme extension is not loaded (run build.bat, then godot --headless --import)")
		return EXIT_FAIL

	# 1. mount pure RotWK 2.01
	var fs: RefCounted = ClassDB.instantiate("RetailFileSystem")
	var mount_start := Time.get_ticks_msec()
	var mount: Dictionary = fs.mount_retail() # always verifies every archive's size and md5
	var mount_ms := Time.get_ticks_msec() - mount_start
	if not mount.ok:
		print("FAIL: mount_retail: ", "\n  ".join(mount.errors))
		return EXIT_FAIL
	print("  info mount_retail %d ms: %d archives hashed, %d md5 cache hits" % [mount_ms, mount.archives_hashed, mount.md5_cache_hits])
	_check(mount.archives_hashed + mount.md5_cache_hits == mount.mounted.size(), "every mounted archive was md5-verified (%d hashed + %d cached = %d)" % [mount.archives_hashed, mount.md5_cache_hits, mount.mounted.size()])
	var mounted: Array = mount.mounted
	var by_install := {"rotwk": 0, "bfme2": 0}
	for m in mounted:
		by_install[m.install] += 1
	_check(by_install.rotwk == 106 and by_install.bfme2 == 107, "mounted 106 RotWK 2.01 + 107 BFME2 1.06 archives (got %d + %d)" % [by_install.rotwk, by_install.bfme2])
	var community: Array[String] = []
	for m in mounted:
		var p: String = String(m.path).to_lower()
		if "202" in p or "hdrotwk" in p:
			community.append(m.path)
	_check(community.is_empty(), "no 2.02/HD archive mounted %s" % [community])
	print("  info excluded on disk (not mounted): ", ", ".join(mount.excluded_found))

	# 2. precedence: the 2.01 INI patch beats INI.big and BFME2's ini.big
	var owner: String = fs.get_archive_for_file(PATCHED_INI)
	_check(owner.to_lower().get_file() == "_patch201ini.big" and owner.to_lower().contains("rotwk"), "%s comes from RotWK _patch201ini.big (got %s)" % [PATCHED_INI, owner])

	# 3. a known INI file
	var ini_bytes: PackedByteArray = fs.read_file(UNIT_INI)
	_check(ini_bytes.size() > 0, "read %s (%d bytes)" % [UNIT_INI, ini_bytes.size()])
	var ini := ini_bytes.get_string_from_utf8()
	_check(ini.contains("Object " + UNIT_OBJECT), "INI defines Object " + UNIT_OBJECT)
	var model := RetailIni.find_default_model(ini, UNIT_OBJECT)
	_check(not model.is_empty(), "%s default model is %s" % [UNIT_OBJECT, model])
	if model.is_empty():
		return EXIT_FAIL

	# 4. raw W3D headers (independent parse)
	var w3d_path := "art\\w3d\\%s\\%s.w3d" % [model.substr(0, 2).to_lower(), model.to_lower()]
	var raw: PackedByteArray = fs.read_file(w3d_path)
	_check(raw.size() > 0, "read %s (%d bytes)" % [w3d_path, raw.size()])
	var headers := _mesh_headers(raw)
	_check(not headers.is_empty(), "%d mesh headers in %s" % [headers.size(), w3d_path])

	# 5. build through the extension and compare
	var builder: RefCounted = ClassDB.instantiate("W3DModelBuilder")
	var node: Node3D = builder.build_model(fs, model)
	var report: Dictionary = builder.get_report()
	_check(node != null, "built %s" % model)
	_check(report.errors.is_empty(), "builder reported no errors %s" % [report.errors])
	if node == null:
		return EXIT_FAIL
	print("  info hierarchy %s, %d pivots, skipped %s" % [report.get("hierarchy", "?"), report.get("pivot_count", 0), report.skipped])

	var built := {}
	for mi in node.find_children("*", "MeshInstance3D", true, false):
		var mesh: Mesh = mi.mesh
		var verts := -1
		var tris := 0
		for s in mesh.get_surface_count():
			var n: int = mesh.surface_get_array_len(s)
			_check(verts == -1 or verts == n, "%s surfaces share one vertex array" % mi.name)
			verts = n
			tris += mesh.surface_get_array_index_len(s) / 3
			var mat: BaseMaterial3D = mesh.surface_get_material(s)
			_check(mat != null and mat.albedo_texture != null, "%s surface %d has its retail texture" % [mi.name, s])
		built[String(mi.get_meta("w3d_mesh", mi.name)).to_upper()] = [verts, tris]
	for name in headers:
		var want: Array = headers[name]
		var got: Array = built.get(name, [-1, -1])
		_check(got == want, "%s: header %d vertices / %d triangles, built %d / %d" % [name, want[0], want[1], got[0], got[1]])
	_check(built.size() == headers.size(), "every mesh in the file was built (%d of %d)" % [built.size(), headers.size()])
	node.free()

	# 6. a texture that fails to load is reported by EVERY build (Sol scaffold finding 8: the second build
	# used to get the cached null and a magenta surface with no error). MISSING_TEXTURE_MODEL's only texture,
	# CUCrow.tga, is in no pure 2.01 + 1.06 archive (found by scanning the archives, not by this loader).
	var errors_by_build: Array = []
	for build in 2:
		var missing_node: Node3D = builder.build_model(fs, MISSING_TEXTURE_MODEL)
		var missing_errors: Array = builder.get_report().errors
		_check(missing_node != null, "build %d of %s produced a node" % [build + 1, MISSING_TEXTURE_MODEL])
		var reported := false
		for e in missing_errors:
			if String(e).contains("texture not found: " + MISSING_TEXTURE):
				reported = true
		_check(reported, "build %d of %s reports the missing texture %s (errors: %s)" % [build + 1, MISSING_TEXTURE_MODEL, MISSING_TEXTURE, missing_errors])
		errors_by_build.append(missing_errors.size())
		if missing_node != null:
			missing_node.free()
	_check(errors_by_build[0] == errors_by_build[1], "both builds report the same number of errors (%s)" % [errors_by_build])

	# 7. W3DInstancer: GPU-skinned instances of the soldier playing its idle clip (no Skeleton3D, no per-unit nodes).
	# Truth for the clip length is the animation file's own header, read here in GDScript.
	var clip_raw: PackedByteArray = fs.read_file("art\\w3d\\gu\\gumanmocap_idlb.w3d")
	var clip_frames := _compressed_anim_frames(clip_raw)
	_check(clip_frames == 154, "gumanmocap_idlb.w3d header: %d frames (the survey counted 154)" % clip_frames)
	var instancer: Node3D = ClassDB.instantiate("W3DInstancer")
	root.add_child(instancer)
	var setup: Dictionary = instancer.setup(fs)
	_check(setup.ok, "W3DInstancer.setup %s" % [setup.errors])
	var soldier: int = instancer.add_model(model)
	_check(soldier >= 0, "instancer built %s" % model)
	if soldier >= 0:
		var model_report: Dictionary = instancer.get_model_report(soldier)
		_check(model_report.errors.is_empty(), "instancer model report has no errors %s" % [model_report.errors])
		_check(model_report.draw_items == headers.size(), "one MultiMesh per sub object mesh: %d of the file's %d" % [model_report.draw_items, headers.size()])
		_check(model_report.pivots == 23, "skeleton %s has 23 pivots (got %d)" % [model_report.hierarchy, model_report.pivots])
		var clip: Dictionary = instancer.get_clip_info(soldier, "GUManMocap_IDLB")
		_check(clip.ok and clip.frames == clip_frames, "clip GUManMocap_IDLB resolved through the hierarchy prefix: %s" % [clip])
		var missing: Dictionary = instancer.get_clip_info(soldier, "GUManMocap_NOSUCHCLIP")
		_check(not missing.ok, "an unknown clip is an error, not a silent bind pose")
		var first: int = instancer.add_instance(soldier, Transform3D.IDENTITY, "GUManMocap_IDLB", 0.0, 1.0)
		_check(first >= 0, "instance created with its idle clip")
		instancer.set_playing(false)
		instancer.set_global_time(0.0)
		instancer.update_now()
		var at_start: Array[Vector3] = []
		for b in 23:
			at_start.append(instancer.get_bone_position(first, b))
		instancer.set_global_time(float(clip_frames / 2) / 30.0)
		instancer.update_now()
		var moved := 0.0
		for b in 23:
			moved += (instancer.get_bone_position(first, b) - at_start[b]).abs().length()
		_check(moved > 0.05, "the idle clip moves the bones between frame 0 and frame %d (summed displacement %.3f)" % [clip_frames / 2, moved])
		var stats: Dictionary = instancer.get_stats()
		_check(stats.visible_draw_instances == model_report.draw_items, "every sub object of the one instance is drawn (%d)" % stats.visible_draw_instances)
		_check(stats.shaders >= 1 and stats.textures >= 1, "generated %d shader(s), loaded %d texture(s)" % [stats.shaders, stats.textures])
		_check(instancer.get_errors().is_empty(), "instancer recorded no errors %s" % [instancer.get_errors()])
		_instancer_stop_reports(instancer, soldier, clip_frames)
		_instancer_pool_stress(instancer, soldier)
	_instancer_sorting(instancer)
	_instancer_runtime_stops(fs)
	instancer.free()
	_map_terrain(fs)
	_map_objects(fs)
	await _menu_player(fs)
	_game_world(fs)
	await _fx_player(fs)
	_game_world_orders(fs)

	return EXIT_PASS if _failures == 0 else EXIT_FAIL


## Review finding P1: every acceptance stop a model uses must appear in its report and in get_warnings() the moment it is built.
## The expectations are derived from the report's own independent per-sub-object fields (vertex counts, flags), not from the stop text.
func _instancer_stop_reports(instancer: Node3D, model: int, clip_frames: int) -> void:
	var report: Dictionary = instancer.get_model_report(model)
	_check(report.has("stops"), "model report carries a stops dictionary")
	var stops: Dictionary = report.stops
	var warnings: PackedStringArray = instancer.get_warnings()
	print("  info soldier stops: ", stops.keys())
	var dual_vertices := 0
	var zero_weights := 0
	for sub in report.sub_objects:
		if sub.get("dual_bone", false):
			dual_vertices += int(sub.vertices)
		zero_weights += int(sub.get("zero_weight_vertices", 0))
	_check(stops.has("S-021") == (dual_vertices > 0), "S-021 is reported exactly when a sub object is dual-bone (%d vertices)" % dual_vertices)
	if dual_vertices > 0:
		_check(stops["S-021"].count == dual_vertices, "S-021 covers every dual-bone vertex: %d of %d" % [stops["S-021"].count, dual_vertices])
	_check(stops.has("S-020") == (zero_weights > 0), "S-020 is reported exactly when zero-weight vertices exist (%d)" % zero_weights)
	_check(stops.has("S-023") == (report.fx_surfaces > 0), "S-023 is reported exactly when FX surfaces exist (%d)" % report.fx_surfaces)
	if report.fx_surfaces > 0:
		_check(stops["S-023"].hits == report.fx_surfaces, "S-023 hits equal the report's FX surface count: %d of %d" % [stops["S-023"].hits, report.fx_surfaces])
	for id in stops.keys():
		_check(String(id).begins_with("S-") and stops[id].messages.size() > 0, "stop %s has messages" % id)
		for m in stops[id].messages:
			_check(String(m).begins_with("[%s] " % id), "message of %s starts with its id: %s" % [id, String(m).substr(0, 40)])
			_check(report.notes.has(m), "stop message is in the model notes: %s" % String(m).substr(0, 40))
			_check(warnings.has(m), "stop message is in get_warnings(): %s" % String(m).substr(0, 40))

	# S-024: a bare clip name is resolved through the model's hierarchy and says so; a dotted name does not.
	var bare: Dictionary = instancer.get_clip_info(model, "GUManMocap_IDLB")
	_check(bare.ok and bare.has("stop") and String(bare.stop).begins_with("[S-024] "), "bare clip name reports S-024: %s" % [bare.get("stop", "")])
	_check(instancer.get_warnings().has(bare.get("stop", "")), "the S-024 message is in get_warnings()")
	var dotted: Dictionary = instancer.get_clip_info(model, String(report.hierarchy) + ".GUManMocap_IDLB")
	_check(dotted.ok and not dotted.has("stop"), "a hierarchy-prefixed clip name raises no S-024")

	# S-028 / S-029 do not fire for the compressed idle clip on opaque meshes
	instancer.update_now()
	var stats: Dictionary = instancer.get_stats()
	_check(stats.inexact_pose_instances == 0, "no S-028 pose for a compressed motion-channel clip (%d)" % stats.inexact_pose_instances)
	_check(stats.dithered_fade_instances == 0, "no S-029 dither while every fade is 1 (%d)" % stats.dithered_fade_instances)


## Review finding P1: worker pool generations. Rebuilds the pool repeatedly with 100 instances (above the 64 that go parallel) and checks
## that every update gives every instance the pose of its clock, whatever generation of workers ran it.
func _instancer_pool_stress(instancer: Node3D, model: int) -> void:
	var ids: Array[int] = []
	for i in 100:
		ids.append(instancer.add_instance(model, Transform3D(Basis.IDENTITY, Vector3(float(i) * 2.0, 0.0, 0.0)), "GUManMocap_IDLB", 0.0, 1.0))
	instancer.set_playing(false)
	var mismatches := 0
	var updates := 0
	for iteration in 120:
		instancer.set_worker_threads(1 + (iteration * 7) % 12)
		instancer.set_global_time(float(iteration % 17) * 0.05)
		instancer.update_now()
		updates += 1
		# every instance has the same clip and start: bone 7 sits at the same offset from its own origin
		var ref: Vector3 = instancer.get_bone_position(ids[0], 7)
		for k in [1, 31, 63, 64, 99]:
			if not instancer.get_bone_position(ids[k], 7).is_equal_approx(ref):
				mismatches += 1
	_check(mismatches == 0, "worker pool: %d updates over %d generations, %d instances: every instance had its pose" % [updates, updates, ids.size()])
	_check(instancer.get_stats().worker_threads == 1 + (119 * 7) % 12, "set_worker_threads sticks (%d)" % instancer.get_stats().worker_threads)
	_check(instancer.get_errors().is_empty(), "no errors after the pool stress %s" % [instancer.get_errors()])


## Review finding P2: blended instances are drawn back to front by camera depth. Two overlapping alpha instances of a retail effect mesh
## (EXHEALING: a sorted blended surface), inserted near-first.
func _instancer_sorting(instancer: Node3D) -> void:
	var effect: int = instancer.add_model("EXHEALING")
	_check(effect >= 0, "instancer built EXHEALING %s" % [instancer.get_errors()])
	if effect < 0:
		return
	instancer.set_sort_camera(Transform3D(Basis.IDENTITY, Vector3(0, 0, 10))) # looks down -Z
	var near_id: int = instancer.add_instance(effect, Transform3D(Basis.IDENTITY, Vector3(0, 0, 5)), "", 0.0, 1.0)
	var far_id: int = instancer.add_instance(effect, Transform3D(Basis.IDENTITY, Vector3(0, 0, -5)), "", 0.0, 1.0)
	var mid_id: int = instancer.add_instance(effect, Transform3D(Basis.IDENTITY, Vector3(0, 0, 0)), "", 0.0, 1.0)
	instancer.update_now()
	var order: PackedInt32Array = instancer.get_draw_order(effect, 0)
	_check(order == PackedInt32Array([far_id, mid_id, near_id]), "blended instances inserted near-first are drawn far to near: %s (near %d, far %d, mid %d)" % [order, near_id, far_id, mid_id])
	_check(instancer.get_stats().sorted_draw_items >= 1, "the sorted draw item is counted")
	# the camera on the other side flips the order
	instancer.set_sort_camera(Transform3D(Basis(Vector3.UP, PI), Vector3(0, 0, -10))) # looks down +Z
	instancer.update_now()
	order = instancer.get_draw_order(effect, 0)
	_check(order == PackedInt32Array([near_id, mid_id, far_id]), "camera moved behind the instances flips the order: %s" % [order])


## Review round 2, P2 findings: the S-028 / S-029 reports must fire when their conditions are met, not only stay silent when they are not.
## Triggers are retail clips found by scanning the corpus (test_w3d_render_corpus.cpp prints them): AUBOMSHIP_A pivot 1 fades to 0.50 at
## frame 28 (fade channel read by the C++ loader, an opaque draw item hangs on that pivot); DBFTOWER_D3's clip is a raw animation, the
## arm whose summation order is stop S-028.
func _instancer_runtime_stops(fs: RefCounted) -> void:
	var inst: Node3D = ClassDB.instantiate("W3DInstancer")
	root.add_child(inst)
	var setup: Dictionary = inst.setup(fs)
	_check(setup.ok, "second instancer setup %s" % [setup.errors])
	inst.set_sort_camera(Transform3D(Basis.IDENTITY, Vector3.ZERO)) # looks down -Z; depth = -z
	inst.set_playing(false)

	# S-029 fade dither
	var ship: int = inst.add_model("AUBOMSHIP_A")
	_check(ship >= 0, "built AUBOMSHIP_A %s" % [inst.get_errors()])
	var ship_clip := "AUBOMSHIP_A.AUBOMSHIP_A"
	var info: Dictionary = inst.get_clip_info(ship, ship_clip)
	_check(info.ok, "clip %s resolves %s" % [ship_clip, info])
	var ship_instance: int = inst.add_instance(ship, Transform3D.IDENTITY, ship_clip, 0.0, 1.0)
	inst.set_global_time(28.0 / float(info.frame_rate))
	inst.update_now()
	var dithered: int = inst.get_stats().dithered_fade_instances
	_check(dithered == 3, "frame 28: pivot 1 fades to 0.50 and its three opaque draw items are dithered (%d)" % dithered)
	var saw_fade := false
	for w in inst.get_warnings():
		if String(w).begins_with("[S-029] ") and String(w).contains("opaque surfaces drawn with a screen-door dither"):
			saw_fade = true
	_check(saw_fade, "the S-029 fade dither warning is in get_warnings() %s" % [inst.get_warnings()])
	inst.set_instance_clip(ship_instance, "", 0.0, 1.0)
	inst.update_now()
	_check(inst.get_stats().dithered_fade_instances == 0, "bind pose again: the counter returns to 0")

	# S-028 raw animation
	var tower: int = inst.add_model("DBFTOWER_D3")
	_check(tower >= 0, "built DBFTOWER_D3 %s" % [inst.get_errors()])
	var tower_clip := "DBFTOWER_D3SKL.DBFTOWER_D3AN"
	var tinfo: Dictionary = inst.get_clip_info(tower, tower_clip)
	var tower_instance: int = inst.add_instance(tower, Transform3D(Basis.IDENTITY, Vector3(0, 0, -500)), tower_clip, 0.0, 1.0)
	inst.set_global_time(0.0)
	inst.update_now()
	var inexact: int = inst.get_stats().inexact_pose_instances
	_check(inexact == 1, "one instance plays a raw animation: S-028 counts it (%d)" % inexact)
	var saw_pose := false
	for w in inst.get_warnings():
		if String(w).begins_with("[S-028] 1 pose(s)"):
			saw_pose = true
	_check(saw_pose, "the S-028 raw / blend pose warning is in get_warnings() %s" % [inst.get_warnings()])
	inst.set_instance_clip(tower_instance, "", 0.0, 1.0)
	inst.update_now()
	_check(inst.get_stats().inexact_pose_instances == 0, "a bind pose is exact again (%d)" % inst.get_stats().inexact_pose_instances)
	inst.free()

	# S-029 cross-batch ordering: group A (EXBALROGLIGHT) at depths 20 and 2, group B (EXBALROGLIGHT2) at depth 10; both blend at priority 0
	var scene := _batch_scene(fs, "EXBALROGLIGHT", [20.0, 2.0], "EXBALROGLIGHT2", [10.0])
	var pairs: int = scene.stats.interleaved_batch_pairs
	_check(scene.same_priorities, "both effect models report the same sort priorities %s %s" % [scene.priorities_a, scene.priorities_b])
	_check(pairs == 1, "A at depths 20 and 2 with B at 10: one interleaved batch pair (%d)" % pairs)
	var saw_batch := false
	for w in scene.warnings:
		if String(w).begins_with("[S-029] ") and String(w).contains("batch by batch"):
			saw_batch = true
	_check(saw_batch, "the S-029 batch ordering warning is in get_warnings() %s" % [scene.warnings])
	# identical depth ranges (A at 20 and 2, B at 20 and 2): no whole-batch order is back to front either
	var identical := _batch_scene(fs, "EXBALROGLIGHT", [20.0, 2.0], "EXBALROGLIGHT2", [20.0, 2.0])
	_check(identical.stats.interleaved_batch_pairs == 1, "A {20, 2} and B {20, 2}: one interleaved batch pair (%d)" % identical.stats.interleaved_batch_pairs)
	var saw_identical := false
	for w in identical.warnings:
		if String(w).begins_with("[S-029] ") and String(w).contains("batch by batch"):
			saw_identical = true
	_check(saw_identical, "the S-029 batch ordering warning is in get_warnings() for identical ranges %s" % [identical.warnings])
	# touching ranges (A {20, 10}, B {10, 2}): A then B is back to front, nothing to report
	var touching := _batch_scene(fs, "EXBALROGLIGHT", [20.0, 10.0], "EXBALROGLIGHT2", [10.0, 2.0])
	_check(touching.stats.interleaved_batch_pairs == 0, "touching ranges: no pair reported (%d)" % touching.stats.interleaved_batch_pairs)
	# B in front of both A instances: a back-to-front order exists, nothing to report
	var separable := _batch_scene(fs, "EXBALROGLIGHT", [20.0, 12.0], "EXBALROGLIGHT2", [5.0])
	_check(separable.stats.interleaved_batch_pairs == 0, "A at 20 and 12 with B at 5: separable, no pair reported (%d)" % separable.stats.interleaved_batch_pairs)
	# the same interleaved depths with different render priorities (EXHEALING -4, EXHEALING_L 0): the renderer orders them, nothing to report
	var priority := _batch_scene(fs, "EXHEALING", [20.0, 2.0], "EXHEALING_L", [10.0])
	_check(not priority.same_priorities, "EXHEALING and EXHEALING_L have different sort priorities %s %s" % [priority.priorities_a, priority.priorities_b])
	_check(priority.stats.interleaved_batch_pairs == 0, "interleaved depths at different priorities: not reported (%d)" % priority.stats.interleaved_batch_pairs)
	# SMOOTH-1 review r1: the interleaved pair disappears with its batches, whether the instances are removed or hidden (camera unchanged)
	_batch_cleared(fs, false)
	_batch_cleared(fs, true)


## A {20, 2} and B {10} (one interleaved pair), then every instance removed (hide = false) or every sub object hidden (hide = true): no
## visible instance, empty draw orders and no interleaved pair, at once and on the next unchanged update.
func _batch_cleared(fs: RefCounted, hide: bool) -> void:
	var how := "hidden" if hide else "removed"
	var inst: Node3D = ClassDB.instantiate("W3DInstancer")
	root.add_child(inst)
	inst.setup(fs)
	inst.set_sort_camera(Transform3D(Basis.IDENTITY, Vector3.ZERO))
	var models := [inst.add_model("EXBALROGLIGHT"), inst.add_model("EXBALROGLIGHT2")]
	var ids: Array = []
	for d in [20.0, 2.0]:
		ids.append([models[0], inst.add_instance(models[0], Transform3D(Basis.IDENTITY, Vector3(0, 0, -d)), "", 0.0, 1.0)])
	ids.append([models[1], inst.add_instance(models[1], Transform3D(Basis.IDENTITY, Vector3(0, 0, -10.0)), "", 0.0, 1.0)])
	inst.update_now()
	_check(inst.get_stats().interleaved_batch_pairs == 1, "batches to be %s: one interleaved pair first (%d)" % [how, inst.get_stats().interleaved_batch_pairs])
	for e in ids:
		if hide:
			var names := PackedStringArray()
			for so in inst.get_model_report(e[0]).sub_objects:
				names.append(so.name)
			inst.set_instance_hidden_subobjects(e[1], names)
		else:
			inst.remove_instance(e[1])
	for pass_name in ["at once", "on the next unchanged update"]:
		inst.update_now()
		var st: Dictionary = inst.get_stats()
		var orders_empty: bool = inst.get_draw_order(models[0], 0).is_empty() and inst.get_draw_order(models[1], 0).is_empty()
		_check(st.visible_draw_instances == 0 and orders_empty and st.interleaved_batch_pairs == 0,
			"every batch %s, %s: visible %d, draw orders empty %s, interleaved pairs %d" % [how, pass_name, st.visible_draw_instances, orders_empty, st.interleaved_batch_pairs])
	inst.free()


## Two models, one instance per depth (camera at the origin looking down -Z, so depth = -z); returns the stats, warnings and the models' sort priorities.
func _batch_scene(fs: RefCounted, name_a: String, depths_a: Array, name_b: String, depths_b: Array) -> Dictionary:
	var inst: Node3D = ClassDB.instantiate("W3DInstancer")
	root.add_child(inst)
	inst.setup(fs)
	inst.set_sort_camera(Transform3D(Basis.IDENTITY, Vector3.ZERO))
	var model_a: int = inst.add_model(name_a)
	var model_b: int = inst.add_model(name_b)
	_check(model_a >= 0 and model_b >= 0, "built %s and %s %s" % [name_a, name_b, inst.get_errors()])
	for d in depths_a:
		inst.add_instance(model_a, Transform3D(Basis.IDENTITY, Vector3(0, 0, -d)), "", 0.0, 1.0)
	for d in depths_b:
		inst.add_instance(model_b, Transform3D(Basis.IDENTITY, Vector3(0, 0, -d)), "", 0.0, 1.0)
	inst.update_now()
	var pa: Array = inst.get_model_report(model_a).sub_objects[0].get("sort_priorities", [])
	var pb: Array = inst.get_model_report(model_b).sub_objects[0].get("sort_priorities", [])
	var out := {"stats": inst.get_stats(), "warnings": inst.get_warnings(), "priorities_a": pa, "priorities_b": pb, "same_priorities": pa == pb and not pa.is_empty()}
	inst.free()
	return out


## NumFrames of the first W3D_CHUNK_COMPRESSED_ANIMATION (0x280) in the file: its header chunk (0x281) holds
## Version, Name[16], HierarchyName[16], NumFrames. -1 when there is none.
func _compressed_anim_frames(data: PackedByteArray) -> int:
	var pos := 0
	while pos + 8 <= data.size():
		var id := data.decode_u32(pos)
		var size := data.decode_u32(pos + 4) & 0x7FFFFFFF
		if id == 0x280:
			var sub := pos + 8
			if data.decode_u32(sub) == 0x281:
				return data.decode_u32(sub + 8 + 36)
		pos += 8 + size
	return -1


## { "CONTAINER.MESH": [NumVertices, NumTris] } for every W3D_CHUNK_MESH in the file.
func _mesh_headers(data: PackedByteArray) -> Dictionary:
	var out := {}
	var pos := 0
	while pos + 8 <= data.size():
		var id := data.decode_u32(pos)
		var size := data.decode_u32(pos + 4) & 0x7FFFFFFF
		if id == W3D_CHUNK_MESH:
			var sub := pos + 8
			var end := sub + size
			while sub + 8 <= end:
				var sid := data.decode_u32(sub)
				var ssize := data.decode_u32(sub + 4) & 0x7FFFFFFF
				if sid == W3D_CHUNK_MESH_HEADER3:
					var h := sub + 8
					# Version, Attributes, MeshName[16], ContainerName[16], NumTris, NumVertices
					var mesh_name := data.slice(h + 8, h + 24).get_string_from_ascii()
					var container := data.slice(h + 24, h + 40).get_string_from_ascii()
					var num_tris := data.decode_u32(h + 40)
					var num_verts := data.decode_u32(h + 44)
					var full := mesh_name if container.is_empty() else container + "." + mesh_name
					out[full.to_upper()] = [num_verts, num_tris]
				sub += 8 + ssize
		pos += 8 + size
	return out


## MAP-1: build a real retail map through MapTerrainBuilder. EVERY expectation comes from the oracle survey
## (engine/tests/data/map-survey.json, written by tools/maps/map_survey.py from the spec author's independent
## parser), never from the builder's own counts. "map wor fangorn" has standing water (2 areas), rivers (9) and a
## post effect (1), so the water, river and post-effect paths are exercised. This test runs headless (the dummy
## renderer): it builds the scene, adds it to the tree and forces a draw, but cannot rasterise; the rendered pixels
## are checked by tests/shader_test.gd (a windowed run: the shader's numbers, and the same map rendered).
func _map_terrain(fs: RefCounted) -> void:
	const MAP := "map wor fangorn"
	const KEY := "maps/map wor fangorn/map wor fangorn.map"
	_check(ClassDB.class_exists("MapTerrainBuilder"), "MapTerrainBuilder is registered")
	if not ClassDB.class_exists("MapTerrainBuilder"):
		return
	var survey_path := ProjectSettings.globalize_path("res://").path_join("../engine/tests/data/map-survey.json")
	var survey: Dictionary = JSON.parse_string(FileAccess.get_file_as_string(survey_path))
	_check(not survey.is_empty(), "read the oracle survey %s" % survey_path)
	var want: Dictionary = survey.maps[KEY]
	var w: int = int(want.heightMap.w)
	var h: int = int(want.heightMap.h)
	var cells_x := w - 1
	var cells_y := h - 1

	# S-039: loose maps in the install folders are contamination (PLAN rule 7). A clean install has none, the
	# developer's Windows install had two 0-byte libraries: whatever the mount found must be well-formed, and
	# the map report below must carry exactly the mount's findings (and S-039 if and only if there are any).
	var loose: Array = fs.get_loose_files()
	var loose_paths: Array = []
	for f in loose:
		loose_paths.append(String(f.path))
		_check(String(f.line).begins_with("S-039 contamination (PLAN rule 7)") and String(f.line).contains(String(f.path)), "loose file %s carries the S-039 line" % f.path)
	loose_paths.sort()
	print("  info mount loose files: ", loose_paths)

	var builder: RefCounted = ClassDB.instantiate("MapTerrainBuilder")
	var start := Time.get_ticks_msec()
	var root: Node3D = builder.build_map(fs, MAP, {"fog": false})
	var ms := Time.get_ticks_msec() - start
	var report: Dictionary = builder.get_report()
	_check(root != null, "built %s" % MAP)
	if root == null:
		print("  info map errors ", report.get("errors", []))
		_failures += 1
		return
	# SkyEnv.tga is referenced by standing-water areas and exists in no pure 2.01 / 1.06 archive (reported)
	var other_errors: Array = []
	for e in report.errors:
		if not String(e).contains("SkyEnv"):
			other_errors.append(e)
	_check(other_errors.is_empty(), "the only build errors are the known missing SkyEnv*.tga %s" % [other_errors])
	var info: Dictionary = report.map_info
	_check(info.width == w and info.height == h and info.border == int(want.heightMap.border), "%s is %d x %d, border %d (survey)" % [MAP, w, h, int(want.heightMap.border)])
	_check(info.waypoints == int(want.waypoints.objects), "%d waypoints (survey %d)" % [info.waypoints, int(want.waypoints.objects)])
	var mesh: Dictionary = report.mesh
	_check(mesh.cells == cells_x * cells_y, "%d cells (survey grid %d x %d)" % [mesh.cells, cells_x, cells_y])
	var chunks_want: int = int(ceil(cells_x / 32.0)) * int(ceil(cells_y / 32.0))
	_check(mesh.chunks == chunks_want, "%d chunks (want %d)" % [mesh.chunks, chunks_want])
	# blend layers: the survey counts every grid cell, the mesh draws all but the last row and column
	var bt: Dictionary = want.btdstats
	var slack := w + h - 1
	_check(int(bt.cellsBlended) - mesh.blend_cells >= 0 and int(bt.cellsBlended) - mesh.blend_cells <= slack, "blend layer cells %d (survey %d, never-drawn edge cells <= %d)" % [mesh.blend_cells, int(bt.cellsBlended), slack])
	_check(int(bt.cellsExtra) - mesh.extra_blend_cells >= 0 and int(bt.cellsExtra) - mesh.extra_blend_cells <= slack, "extra layer cells %d (survey %d)" % [mesh.extra_blend_cells, int(bt.cellsExtra)])
	_check(mesh.composite_layer1_cells == mesh.blend_cells and mesh.composite_layer2_cells == mesh.extra_blend_cells, "the composite carries every blend layer cell (%d, %d)" % [mesh.composite_layer1_cells, mesh.composite_layer2_cells])
	_check(mesh.draw_passes == 1, "the terrain is one opaque composite pass")
	# water, rivers, post effects: survey counts
	var water: Dictionary = report.water
	_check(water.areas_in_map == (want.standingWater as Array).size(), "%d standing water areas (survey %d)" % [water.areas_in_map, (want.standingWater as Array).size()])
	_check(water.areas + water.triangulation_failed == water.areas_in_map and water.areas > 0, "every water area is rendered or counted as failed (%d + %d)" % [water.areas, water.triangulation_failed])
	var rivers: Dictionary = report.rivers
	_check(rivers.areas_in_map == (want.rivers as Array).size(), "%d river areas (survey %d)" % [rivers.areas_in_map, (want.rivers as Array).size()])
	_check(rivers.rendered > 0 and rivers.rendered <= rivers.areas_in_map, "%d rivers rendered" % rivers.rendered)
	var posts: Array = report.post_effects
	_check(posts.size() == int(want.postEffects) and posts.size() == 1, "%d post effect reported (survey %d)" % [posts.size(), int(want.postEffects)])
	for p in posts:
		# lane RENDER-4 (S-1650): the LookupTablePostEffect colour grade is applied
		_check(p.applied == true and String(p.lookup_image).to_lower().ends_with(".tga"), "post effect %s lookup %s is applied (S-1650)" % [p.name, p.lookup_image])
	# stops: the load-time and render stops, the loose files and the height maths
	var stops: Array = report.stops
	for id in ["S-030", "S-031", "S-032", "S-033", "S-034", "S-035", "S-036", "S-037", "S-038", "S-060"]:
		_check(stops.has(id), "stop %s is reported with the map" % id)
	var report_loose_paths: Array = []
	for f in (report.loose_files as Array):
		report_loose_paths.append(String(f.path))
	report_loose_paths.sort()
	_check(report_loose_paths == loose_paths, "the map report carries the mount's loose files %s" % [loose_paths])
	_check(stops.has("S-039") == (not loose_paths.is_empty()), "S-039 is reported with the map if and only if the mount found loose files (%d)" % loose_paths.size())

	# draw order, as retail: the terrain composite (opaque, priority 0) first, then water, rivers and roads, which
	# are blended over it. The former three-draw terrain had blend layers at priorities above the water.
	var terrain_mat: ShaderMaterial = null
	var terrain_meshes := root.get_node("Terrain").find_children("*", "MeshInstance3D", true, false)
	_check(terrain_meshes.size() == chunks_want, "%d terrain mesh instances for %d chunks" % [terrain_meshes.size(), chunks_want])
	var terrain_priority := -1
	for mi in terrain_meshes:
		var m: ShaderMaterial = (mi as MeshInstance3D).mesh.surface_get_material(0)
		terrain_mat = m
		terrain_priority = m.render_priority
		_check((mi as MeshInstance3D).mesh.get_surface_count() == 1, "%s is a single surface (one composite pass)" % mi.name)
		break
	_check(terrain_mat != null and not terrain_mat.shader.code.contains("blend_mix") and not terrain_mat.shader.code.contains("depth_draw_never"), "the terrain shader is opaque (no alpha blend, writes depth)")
	for group in ["StandingWater", "Rivers"]:
		var node := root.get_node_or_null(group)
		_check(node != null, "the scene has a %s node" % group)
		if node == null:
			continue
		for mi in node.find_children("*", "MeshInstance3D", true, false):
			var m: ShaderMaterial = (mi as MeshInstance3D).mesh.surface_get_material(0)
			if m != null:
				_check(m.render_priority > terrain_priority and m.shader.code.contains("blend_mix"), "%s/%s draws after the opaque terrain (priority %d > %d, blended)" % [group, mi.name, m.render_priority, terrain_priority])
	var meshes := root.find_children("*", "MeshInstance3D", true, false)

	# present a frame (the dummy renderer in headless mode: the scene is entered and drawn, not rasterised)
	get_root().add_child(root)
	RenderingServer.force_draw(false)
	_check(root.get_parent() == get_root(), "the map scene was added to the scene tree and a frame was drawn")
	print("  info %s built in %d ms: %d cells, %d vertices, %d triangles, %d mesh instances" % [MAP, ms, mesh.cells, mesh.vertices, mesh.triangles, meshes.size()])
	root.queue_free()


## Lane MAPOBJ-1: the objects of one real map (trees, buildings, hordes), built through the object builder and the W3D instancer. The
## expectations are the MAP-1 survey's counts (independent of the C++ reader) and consistency relations of the report; the rules
## themselves are pinned by the core tests (engine/tests/test_mapobj_*.cpp).
## The state entries of the time-variant mappers of `source` (mesh name) and `stage` in an instancer.
func _mapper_rows(inst: Node3D, source: String, stage: int) -> Array:
	var rows: Array = []
	for st in inst.get_mapper_state():
		if st.source == source and st.stage == stage:
			rows.append(st)
	return rows


func _map_objects(fs: RefCounted) -> void:
	const MAP := "map ang rhudaur"
	const KEY := "maps/map ang rhudaur/map ang rhudaur.map"
	_check(ClassDB.class_exists("MapObjectBuilder"), "MapObjectBuilder is registered")
	if not ClassDB.class_exists("MapObjectBuilder"):
		return
	var survey_path := ProjectSettings.globalize_path("res://").path_join("../engine/tests/data/map-survey.json")
	var survey: Dictionary = JSON.parse_string(FileAccess.get_file_as_string(survey_path))
	var want: Dictionary = survey.maps[KEY]
	var builder: RefCounted = ClassDB.instantiate("MapObjectBuilder")
	var setup: Dictionary = builder.setup(fs)
	_check(setup.ok and setup.templates == 4657, "the retail object templates load without error (%d templates, %.1f s)" % [setup.get("templates", 0), setup.get("seconds", 0.0)])
	if not setup.ok:
		print("  info object world errors ", setup.errors)
		return
	var start := Time.get_ticks_msec()
	var root: Node3D = builder.build_objects(fs, MAP, {})
	var ms := Time.get_ticks_msec() - start
	var rep: Dictionary = builder.get_report()
	_check(root != null, "built the objects of %s in %d ms" % [MAP, ms])
	if root == null:
		print("  info object errors ", rep.get("errors", []))
		_failures += 1
		return
	_check(rep.errors.is_empty(), "the object build recorded no error (%d: %s)" % [rep.errors.size(), rep.errors.slice(0, 3)])
	var o: Dictionary = rep.objects
	var fates: Dictionary = o.by_fate
	# every ObjectsList entry has exactly one fate, and the survey's independent counts hold
	_check(o.total == int(want.objects.count), "%d objects (survey %d)" % [o.total, int(want.objects.count)])
	var fate_sum := 0
	for k in fates:
		fate_sum += int(fates[k])
	_check(fate_sum == o.total, "the fates account for every object (%d of %d)" % [fate_sum, o.total])
	var flagged := 0
	for k in want.objects.flags:
		if (int(k) & (2 | 4 | 0x10 | 0x20)) != 0:
			flagged += int(want.objects.flags[k])
	_check(int(fates["road/bridge point (terrain side)"]) == flagged, "%d road / bridge points (survey flags %d)" % [fates["road/bridge point (terrain side)"], flagged])
	_check(int(fates["no template: waypoint"]) == int(want.waypoints.objects), "%d waypoints (survey %d)" % [fates["no template: waypoint"], int(want.waypoints.objects)])
	_check(int(fates["UNRESOLVED template"]) == 0 and o.unresolved.is_empty() and o.case_mismatch.is_empty(), "every template name resolves")
	_check(int(fates["culled by the reader (z)"]) == 0, "the reader drops no object of this map")
	# trees, buildings and a horde are present
	_check(int(fates["client-only tree"]) + int(fates["client-only shrub"]) > 1000, "trees and shrubs stand on the map (%d + %d)" % [fates["client-only tree"], fates["client-only shrub"]])
	_check(int(fates["object"]) > 100, "%d full objects (buildings, units, hordes)" % fates["object"])
	_check(o.hordes > 50 and o.horde_members > o.hordes and o.horde_unplaced == 0 and o.horde_members == o.horde_payload,
		"%d hordes with %d members (payload %d, none without a slot)" % [o.hordes, o.horde_members, o.horde_payload])
	_check(o.moved_by_anchor == 0 and o.unported_keys.has("objectInitialHealth"), "initial-state keys are reported unported (S-110)")
	# the draw runtime and the instancing agree with the loop
	var r: Dictionary = rep.runtime
	var i: Dictionary = rep.instancing
	_check(r.placed_models == o.drawn_models, "the runtime places every model the loop says is drawn (%d)" % r.placed_models)
	_check(i.missing_models == 0 and i.pose_failures == 0, "every placed model was instanced and posed (missing %d, pose failures %d)" % [i.missing_models, i.pose_failures])
	_check(i.static_instances + i.animated_instances == r.placed_models, "static %d + animated %d instances = %d placed models" % [i.static_instances, i.animated_instances, r.placed_models])
	_check(r.animated > 100 and i.animated_instances == r.animated, "%d drawables play an idle animation" % r.animated)
	_check(i.instances_with_hidden_sub_objects > 0, "the BeginScript bodies hide sub objects of %d instances" % i.instances_with_hidden_sub_objects)
	_check(rep.has("lighting") and rep.has("focus"), "object lighting and camera focus points are reported")
	var stops: Array = rep.stops
	var stop_ids := {}
	for line in stops:
		stop_ids[String(line).substr(1, 5)] = true
	for id in ["S-110", "S-111", "S-112", "S-113", "S-114", "S-115", "S-116", "S-117", "S-118"]:
		_check(stop_ids.has(id), "stop %s is reported with the objects" % id)
	_check(rep.map_ini.map_ini == true, "the map's map.ini was loaded (load type 2)")
	# the scene: two instancers, batched (a few hundred MultiMeshes at most, not one node per object)
	var instancers := root.find_children("*", "W3DInstancer", false, false)
	_check(instancers.size() == 2, "the object layer is two W3DInstancer nodes")
	var static_stats: Dictionary = instancers[0].get_stats()
	_check(static_stats.instances == i.static_instances, "the static instancer holds %d instances" % static_stats.instances)
	var multimeshes := root.find_children("*", "MultiMeshInstance3D", true, false)
	_check(multimeshes.size() < o.drawables / 4, "instancing is batched: %d MultiMeshes for %d drawables" % [multimeshes.size(), o.drawables])
	_check(root.find_children("*", "MeshInstance3D", true, false).is_empty(), "no per-object mesh node")
	# texture animation (review fix): the static instancer batches its poses once and never updates by itself, the animated one only updates
	# on pose changes, and the mappers (UV scroll of RIVERFOAM, stage 0: u offset -0.01 per second, v offset -0.1 per second) used to stay at
	# clock 0 on both. advance() runs the material clock on both instancers.
	var static_inst: Node3D = instancers[0]
	var animated_inst: Node3D = instancers[1]
	var foam0 := _mapper_rows(static_inst, "RIVERFOAM", 0)
	_check(foam0.size() == 1, "the static instancer has the time-variant mapper of RIVERFOAM stage 0 (%d)" % foam0.size())
	if foam0.size() == 1:
		_check(is_zero_approx(foam0[0].r0.z) and is_zero_approx(foam0[0].r1.z), "at clock 0 the RIVERFOAM UV offset is 0 (%s %s)" % [foam0[0].r0, foam0[0].r1])
	var animated_before: Array = animated_inst.get_mapper_state()
	# animation: advancing the render time steps the draw modules and moves the poses
	get_root().add_child(root)
	var before: Dictionary = builder.get_stats()
	for step in 3:
		builder.advance(0.1)
	var after: Dictionary = builder.get_stats()
	_check(after.pose_updates == before.pose_updates + 3 * after.animated_drawables and after.animated_drawables == r.animated, "advancing 0.3 s updated the pose of each of %d animated drawables 3 times" % after.animated_drawables)
	var foam3 := _mapper_rows(static_inst, "RIVERFOAM", 0)
	_check(foam3.size() == 1 and is_equal_approx(foam3[0].r1.z, 0.97), "after 0.3 s the RIVERFOAM v offset is 0.97 (%s)" % [foam3[0].r1 if foam3.size() == 1 else "missing"])
	builder.advance(0.7)
	var foam1 := _mapper_rows(static_inst, "RIVERFOAM", 0)
	_check(foam1.size() == 1 and absf(foam1[0].r1.z - 0.9) < 0.002 and absf(foam1[0].r0.z - 0.99) < 0.002,
		"after one second the RIVERFOAM UV offset is (0.99, 0.9): %s %s" % [foam1[0].r0 if foam1.size() == 1 else "missing", foam1[0].r1 if foam1.size() == 1 else ""])
	_check(is_equal_approx(builder.get_stats().texture_clock, 1.0), "the builder's texture clock is 1.0 s (%s)" % builder.get_stats().texture_clock)
	# the animated instancer applies the clock in its own update (its _process in a running game)
	animated_inst.update_now()
	var animated_after: Array = animated_inst.get_mapper_state()
	var moved := 0
	for k in animated_after.size():
		if animated_after[k].r0 != animated_before[k].r0 or animated_after[k].r1 != animated_before[k].r1:
			moved += 1
	_check(animated_after.size() > 0 and moved > 0, "the animated instancer's mappers moved with the clock (%d of %d)" % [moved, animated_after.size()])
	RenderingServer.force_draw(false)
	print("  info %s objects built in %d ms: %d objects, %d drawables, %d models (%d static, %d animated instances), %d MultiMeshes" % [MAP, ms, o.total, o.drawables, o.drawn_models, i.static_instances, i.animated_instances, multimeshes.size()])
	root.queue_free()

## APT-3 (menus-apt.md A8): the retail main menu through AptMenuPlayer - boot, draw, and a click through the viewport's input path
## (window -> stage mapping, AptInput), then the Skirmish entry of the Solo Play list.  Expected command names are what the retail
## MainMenu bytecode sends (engine/tests/test_apt_menu.cpp); the numbers of triangles are the core test's job (test_apt_canvas.cpp).
func _menu_player(fs: RefCounted) -> void:
	print("  -- menu player (AptMenuPlayer)")
	_check(ClassDB.class_exists("AptMenuPlayer"), "the AptMenuPlayer class is registered")
	var player: Node2D = ClassDB.instantiate("AptMenuPlayer")
	player.auto_process = false
	get_root().add_child(player)
	var boot: Dictionary = player.boot(fs, {
		"levels": [[0, "AptLevel0"], [1, "MainMenu"]],
		"extern_values": {"InGame": "1", "InBetaDemo": "0", "InDreamMachineDemo": "0", "DoTrace": "0", "MainMenuUnlockBonusCampaign": "0"},
		"component_movies": ["GameWindowGadgets"],
	})
	_check(boot.ok, "AptMenuPlayer.boot %s" % [boot.errors])
	if not boot.ok:
		player.free()
		return
	_check(boot.strings > 10000 and boot.duplicate_labels == 36, "the string table has %d strings, 36 repeated labels (S-131): %d" % [boot.strings, boot.duplicate_labels])
	var font_names := " ".join(boot.fonts)
	_check(font_names.contains("= Albertus MT") and font_names.contains("= Omnia LT Std"), "the corpus fonts loaded: %s" % [boot.fonts])
	var steps := 0
	for i in 10:
		steps += player.tick(0.033)
	player.render(true)
	_check(steps == 10, "10 ticks of 33 ms ran 10 player steps (got %d)" % steps)
	_check(player.get_stage_size() == Vector2(1024, 768), "the stage is 1024 x 768 (got %s)" % [player.get_stage_size()])
	var shown: Dictionary = player.invoke(1, "ShowMainMenu", PackedStringArray())
	_check(shown.ok, "the engine call ShowMainMenu reached the movie %s" % [shown.error])
	for i in 60:
		player.tick(0.033)
	player.render(true)
	var stats: Dictionary = player.get_stats()
	_check(stats.last_ops > 0 and stats.last_triangles > 0 and stats.canvas_items > 0, "the main menu drew %d ops, %d triangles in %d canvas items" % [stats.last_ops, stats.last_triangles, stats.canvas_items])
	_check(stats.texture_loads >= 1, "%d atlas textures were uploaded (%.1f ms)" % [stats.texture_loads, stats.texture_load_ms])
	var ops: PackedStringArray = player.describe_ops()
	var texts := PackedStringArray()
	for line in ops:
		if line.begins_with("text "):
			texts.append(line.get_slice("'", 1))
	texts.sort()
	_check(texts == PackedStringArray(["MULTIPLAYER", "MY HEROES", "OPTIONS", "QUIT", "SOLO PLAY"]), "the five main menu captions come from the string table: %s" % [texts])
	var report: Dictionary = player.get_report()
	_check(report.placeholders == PackedStringArray(["_type=RenderImage @ _level1.Image"]), "the main menu's Image clip is reported as a native placeholder (S-136): %s" % [report.placeholders])
	_check(report.errors.is_empty() and report.missing_labels.is_empty() and report.font_fallbacks.is_empty(), "no render errors, missing labels or font fallbacks %s %s %s" % [report.errors, report.missing_labels, report.font_fallbacks])
	_check(report.unverified.has("tga-origin") and report.unverified.has("uv-matrix-order") and report.unverified.has("text-layout"), "the unverified rendering rules in force are reported %s" % [report.unverified])
	# events: the movie started its screen once, through the host
	var events: Array = player.take_events()
	var commands := []
	for e in events:
		if e.kind == "fscommand":
			commands.append(e.a)
	_check(commands.count("AptMainMenu::OnInitialized") == 1, "AptMainMenu::OnInitialized was sent once (commands %s)" % [commands])
	# the window -> stage mapping (stretch, the donor rule) and its inverse
	var stage_size: Vector2 = player.get_stage_size()
	await process_frame # the root window is in the tree from here on (push_input needs it)
	var window_size: Vector2 = get_root().get_visible_rect().size
	print("  info root visible rect ", window_size, " player sees stage->window of the corner ", player.stage_to_window(stage_size))
	_check(player.window_to_stage(window_size).is_equal_approx(stage_size), "the window's far corner is the stage's far corner")
	var probe := Vector2(200, 300)
	_check(player.window_to_stage(player.stage_to_window(probe)).is_equal_approx(probe), "stage -> window -> stage round trip")
	player.fit = true
	player.render(true)
	_check(player.window_to_stage(player.stage_to_window(probe)).is_equal_approx(probe), "the same in fit mode")
	player.fit = false
	player.render(true)
	# a click through the viewport: open the Solo Play list, then Skirmish
	var click := func(path: String) -> bool:
		var button: Dictionary = player.find_button(1, path)
		if not button.found:
			return false
		var at: Vector2 = player.stage_to_window(Vector2(button.x, button.y))
		var motion := InputEventMouseMotion.new()
		motion.position = at
		get_root().push_input(motion)
		player.tick(0.033)
		var press := InputEventMouseButton.new()
		press.button_index = MOUSE_BUTTON_LEFT
		press.pressed = true
		press.position = at
		get_root().push_input(press)
		player.tick(0.033)
		var release := InputEventMouseButton.new()
		release.button_index = MOUSE_BUTTON_LEFT
		release.pressed = false
		release.position = at
		get_root().push_input(release)
		player.tick(0.033)
		return true
	player.take_events()
	_check(click.call("SoloPlayNav"), "found the Solo Play button")
	for i in 60:
		player.tick(0.033)
	player.render(true)
	var opened_ops: int = player.get_stats().last_ops
	var opened := []
	for e in player.take_events():
		if e.kind == "fscommand":
			opened.append(e.a)
	_check(opened.has("AptMainMenu::OnSoloPlayNav"), "clicking Solo Play sent AptMainMenu::OnSoloPlayNav %s" % [opened])
	_check(click.call("SoloPlayNav.Skirmish"), "found the Skirmish entry of the opened list")
	for i in 3:
		player.tick(0.033)
	var picked := []
	for e in player.take_events():
		if e.kind == "fscommand":
			picked.append(e.a)
	_check(picked.count("AptMainMenu::Skirmish") == 1, "clicking Skirmish sent AptMainMenu::Skirmish once %s" % [picked])
	# the opened list is more to draw than the closed menu
	_check(opened_ops > stats.last_ops, "the opened list added canvas ops (%d -> %d)" % [stats.last_ops, opened_ops])
	# a level the shell pushes: Options draws, and the report names what is not drawn
	var options: Dictionary = player.load_movie(2, "Options")
	_check(options.ok, "Options.apt loads as _level2 %s" % [options.error])
	player.set_level_visible(1, false)
	for i in 20:
		player.tick(0.033)
	player.render(true)
	var options_stats: Dictionary = player.get_stats()
	_check(options_stats.last_triangles > 0 and options_stats.last_text_ops > 10, "the Options screen drew %d triangles and %d texts" % [options_stats.last_triangles, options_stats.last_text_ops])
	print("  info menu player: step %.0f us, render list %.0f us, canvas build %.0f us, canvas submit %.0f us per rebuild" % [options_stats.steps_us, options_stats.list_us, options_stats.canvas_us, options_stats.submit_us])
	_check(player.get_report().unverified_input == PackedStringArray(["key-codes", "mouse-wheel", "click-through"]), "the input bridge reports its unverified rules (S-137)")
	player.free()
	# the viewer scene: the stub shell pushes Skirmish.apt for the Skirmish command (S-138); run as a separate headless Godot
	var output: Array = []
	# lane RELEASE-1: in an exported package (tools/release/test_export.sh) res:// is the pack next to the executable, which loads it again by itself
	var project_args := ["--path", ProjectSettings.globalize_path("res://")] if FileAccess.file_exists("res://project.godot") else []
	var code := OS.execute(OS.get_executable_path(), ["--headless"] + project_args + ["res://scenes/menu_viewer.tscn", "--", "--check", "--clicks=SoloPlayNav,SoloPlayNav.Skirmish"], output, true)
	var log := "\n".join(PackedStringArray(output))
	_check(code == 0, "the menu viewer scene ran its scripted click-through (exit %d)" % code)
	_check(log.contains("FSCOMMAND AptMainMenu::OnInitialized ()") and log.contains("FSCOMMAND AptMainMenu::Skirmish ()"), "the viewer logged the fscommands of the click-through")
	_check(log.contains("SHELL push Skirmish as _level2"), "the stub shell pushed Skirmish.apt as _level2 (S-138)")
	_check(log.contains("FSCOMMAND AptSkirmish::OnInitialized ()"), "Skirmish.apt started its screen (AptSkirmish::OnInitialized)")

## Lane LOGIC-1: the live game through the GameWorld node: retail's scheduler ticking live objects (hordes creating their members through
## HordeContain), drawables synced to the instancers, destruction, and two worlds hashing alike. The rules are pinned by the core tests
## (engine/tests/test_logic_*.cpp); this checks the Godot side builds, runs and agrees with them.
func _game_world(fs: RefCounted) -> void:
	_check(ClassDB.class_exists("GameWorld"), "GameWorld is registered")
	if not ClassDB.class_exists("GameWorld"):
		return
	var world: Node3D = ClassDB.instantiate("GameWorld")
	var setup: Dictionary = world.setup(fs)
	_check(setup.ok and setup.templates == 4657 and setup.factions == 7, "GameWorld.setup: %d templates, %d playable factions (%.1f s)" % [setup.get("templates", 0), setup.get("factions", 0), setup.get("seconds", 0.0)])
	if not setup.ok:
		print("  info game world setup errors ", setup.errors)
		return
	var start := Time.get_ticks_msec()
	var rep: Dictionary = world.load_map("map good celduin", {"seed": 5})
	var ms := Time.get_ticks_msec() - start
	_check(rep.ok, "load_map built map good celduin as live objects in %d ms" % ms)
	if not rep.ok:
		print("  info game world errors ", rep.errors)
		return
	var o: Dictionary = rep.objects
	var l: Dictionary = rep.logic
	var d: Dictionary = rep.drawables
	# MAPOBJ-1's classification of this map: 375 full objects, 91 hordes with 1,461 members (test_logic_retail.cpp pins the same numbers)
	_check(o.created == 375 and o.hordes == 91 and o.contained == 1461, "%d objects from the map list, %d hordes made %d members" % [o.created, o.hordes, o.contained])
	# BUILD-1: the first logic frame (run by the load) unpacks the map's castle centres: 8 more objects (their keeps and plots from the base layouts of Bases.big);
	# lane CASTLE-1: MordorFortress_Celduin_1's PreBuiltList (MordorWallCatapultExpansion -2, four times) pre-builds 4 more on its pads (RW 0x79C265 -> 0x79A5EC)
	# lane MOD-4: the map's spawners (SpawnBehavior) make their first 2 spawns in that frame (one model draw each)
	_check(l.objects == o.created + o.contained + 14 and l.objects == 1850, "%d live objects" % l.objects)
	_check(d.live == l.objects and d.created == l.objects, "every live object has a drawable (%d)" % d.live)
	_check(d.model_draws == 3363 and d.animated_model_draws > 100, "%d model draws, %d animated" % [d.model_draws, d.animated_model_draws])
	_check(rep.errors.is_empty(), "no error in the load report %s" % [rep.errors])
	_check(world.get_frame() == 1 and world.get_object_count() == l.objects, "the first logic frame ran at load (frame %d, %d objects)" % [world.get_frame(), world.get_object_count()])
	var ids := {}
	for line in rep.stops:
		ids[String(line).substr(1, 5)] = true
	for id in ["S-080", "S-140", "S-141", "S-142", "S-143", "S-147", "S-149", "S-150", "S-151", "S-250", "S-253", "S-259"]:
		_check(ids.has(id), "stop %s is reported" % id)
	# lane ECON-1: the economy as the overlay reads it
	var eco: Dictionary = world.get_economy()
	_check(eco.frame == 1 and eco.players.size() > 1 and eco.resource_grid.width > 0 and eco.resource_grid.blocked >= 0, "get_economy: frame %d, %d players, claim grid %d x %d" % [eco.frame, eco.players.size(), eco.resource_grid.width, eco.resource_grid.height])
	var playable_cp_ok := true
	for p in eco.players:
		if p.playable:
			playable_cp_ok = playable_cp_ok and p.cp_limit >= 100 and p.cp_available == p.cp_limit - p.cp_used and p.money >= 0
	_check(playable_cp_ok, "every playable player has its command point limit (base 100) and its cash")
	var instancers := world.find_children("*", "W3DInstancer", false, false)
	_check(instancers.size() == 2, "the scene is two W3DInstancer nodes (static client-only layer, live layer)")
	# lane RENDER-1: one "Streaks" node draws every W3DStreakDraw trail (one ImmediateMesh), the only mesh node of the world
	var mesh_nodes: Array = world.find_children("*", "MeshInstance3D", true, false).filter(func(n): return n.name != "Streaks")
	_check(mesh_nodes.is_empty(), "no per-object mesh node (the streak layer is one node)")
	var live_inst: Node3D = world.get_node("LiveObjects")
	_check(live_inst.get_stats().instances == d.models_shown and d.models_shown > 1500, "the live instancer holds an instance per model shown (%d of %d model draws)" % [live_inst.get_stats().instances, d.model_draws])
	# the objects: a horde and its members
	var horde_id: int = -1
	for id in world.get_object_ids():
		var info: Dictionary = world.get_object(id)
		if info.members.size() > 3:
			horde_id = id
			break
	_check(horde_id > 0, "a horde object exists")
	var horde: Dictionary = world.get_object(horde_id)
	var member: Dictionary = world.get_object(horde.members[0])
	_check(member.contained_by == horde_id and member.template != horde.template, "the first member (%s) is contained by its horde %s" % [member.template, horde.template])
	_check(absf(member.x - horde.x) < 400.0 and absf(member.y - horde.y) < 400.0, "the member stands near its horde (%.0f, %.0f vs %.0f, %.0f)" % [member.x, member.y, horde.x, horde.y])
	# retail's clock: 0.2 s is one logic frame, three 0.1 s steps are 1.5 frames
	var h0: int = world.get_state_hash()
	world.advance(0.2)
	_check(world.get_frame() == 2 and world.get_state_hash() != h0, "0.2 s ran one logic frame (frame %d)" % world.get_frame())
	world.advance(0.1)
	_check(world.get_frame() == 2 and absf(world.get_alpha() - 0.5) < 0.01, "0.1 s later no new frame, alpha %.2f" % world.get_alpha())
	world.advance(0.1)
	_check(world.get_frame() == 3, "another 0.1 s completes frame 3")
	# lane FX-2: the live effect player (FXPlayer child kept across loads, LiveFX on the logic and the drawables) with its stops
	var fxr: Dictionary = world.get_fx_report()
	_check(fxr.ok and (fxr.setup_errors as Array).is_empty(), "the live FX player loaded the retail FX data %s" % [fxr.setup_errors])
	_check(world.get_fx_player() != null and world.get_fx_player().get_parent() == world, "the live FX player is a child of the world")
	_check((fxr.get("missing_fx_lists", []) as Array).is_empty(), "every FXList the live game called exists %s" % [fxr.get("missing_fx_lists", [])])
	var fxids := {}
	for line in fxr.stops:
		fxids[String(line).substr(1, 5)] = true
	for id in ["S-680", "S-681", "S-682", "S-683", "S-685", "S-686"]:
		_check(fxids.has(id), "FX stop %s is reported" % id)
	# lane FX-3: S-684 is retired (retail never loads ParticleSystem.ini); names retail resolves to NULL are listed, not played
	_check(not fxids.has("S-684") and fxr.has("unresolved_particle_systems"), "S-684 is retired and the unresolved ParticleSysBone names are listed")
	get_root().add_child(world)
	RenderingServer.force_draw(false)
	# destruction: a member dies, then its horde (ZH OpenContain::onDelete destroys the members)
	var count_before: int = world.get_object_count()
	_check(world.destroy_object(horde.members[0]), "destroy_object takes a member")
	world.advance(0.2)
	_check(world.get_object_count() == count_before - 1 and not world.get_object(horde.members[0]).ok, "the member is gone after the next logic frame (%d objects)" % world.get_object_count())
	var survivors: int = world.get_object(horde_id).members.size()
	_check(survivors == horde.members.size() - 1, "its horde keeps the other %d members" % survivors)
	_check(world.destroy_object(horde_id), "destroy_object takes the horde")
	world.advance(0.2)
	_check(not world.get_object(horde_id).ok, "the horde is gone")
	_check(world.get_object_count() == count_before - 1 - 1 - survivors, "its members went with it (%d objects)" % world.get_object_count())
	var st: Dictionary = world.get_stats()
	_check(st.drawables == world.get_object_count(), "the drawables follow the objects (%d)" % st.drawables)
	# two worlds with the same inputs hash alike at the same frame
	var other: Node3D = ClassDB.instantiate("GameWorld")
	other.setup(fs)
	var rep2: Dictionary = other.load_map("map good celduin", {"seed": 5})
	_check(rep2.ok, "a second world loads")
	for k in 3:
		other.advance(0.2)
	var third: Node3D = ClassDB.instantiate("GameWorld")
	third.setup(fs)
	third.load_map("map good celduin", {"seed": 5})
	for k in 12:
		third.advance(0.05)
	_check(other.get_frame() == third.get_frame() and other.get_state_hash() == third.get_state_hash(),
		"two worlds fed 0.6 s in different step sizes agree at frame %d: hash %d == %d" % [other.get_frame(), other.get_state_hash(), third.get_state_hash()])
	# SMOOTH-1 (S-810): the logic on its worker thread, fed in 24 render steps, completes the same frames with the same state as the main thread
	var fourth: Node3D = ClassDB.instantiate("GameWorld")
	fourth.setup(fs)
	fourth.load_map("map good celduin", {"seed": 5, "logic_thread": true})
	for k in 24:
		fourth.advance(0.025)
	_check(fourth.get_logic_thread() and fourth.get_frame() == other.get_frame() and fourth.get_state_hash() == other.get_state_hash(),
		"the logic worker thread agrees with the main thread at frame %d: hash %d == %d" % [fourth.get_frame(), fourth.get_state_hash(), other.get_state_hash()])
	fourth.set_logic_thread(false)
	fourth.advance(0.2)
	_check(not fourth.get_logic_thread() and fourth.get_frame() == other.get_frame() + 1, "the worker switches off between frames, nothing dropped (frame %d)" % fourth.get_frame())
	fourth.free()
	var skirm := {"slots": [
		{"player": "Player_1", "faction": "FactionMen", "human": true, "team": 0},
		{"player": "Player_2", "faction": "FactionMordor", "human": false, "team": 1}]}
	var sk: Dictionary = other.load_map("map mp fall back 4p", skirm)
	_check(sk.ok and sk.objects.created == 175 and sk.objects.hordes == 0, "a skirmish map loads with lobby slots (%d objects)" % sk.objects.created)
	var found := false
	for p in sk.players:
		found = found or String(p).contains("'Player_1' FactionMen")
	_check(found and String(sk.players[10]).ends_with("$1500"), "the lobby slot gave Player_1 the Men and GameData's default starting cash: %s" % [sk.players.slice(10, 12)])
	_production(other, third, skirm, sk)
	_heroes(other, skirm) # lane HERO-1
	print("  info game world: celduin in %d ms, %d objects, %d drawables, logic frame %.1f ms, sync %.1f ms" % [ms, l.objects, d.live, world.get_stats().last_logic_ms, world.get_stats().last_sync_ms])
	other.queue_free()
	third.queue_free()
	world.queue_free()

## FX-1: the retail FX data and the FXPlayer. Truth for the counts is a GDScript line scan of the two INI files (independent of the C++
## parsers): the number of distinct `FXParticleSystem <name>` / `FXList <name>` definitions.
func _unique_definitions(fs: RefCounted, path: String, keyword: String) -> int:
	var names := {}
	for line in fs.read_file(path).get_string_from_utf8().split("\n"):
		var t: String = line.get_slice(";", 0).strip_edges()
		if t.begins_with(keyword + " "):
			names[t.substr(keyword.length() + 1).strip_edges()] = true
	return names.size()


func _fx_player(fs: RefCounted) -> void:
	if not ClassDB.class_exists("FXPlayer"):
		_check(false, "the FXPlayer class is registered")
		return
	var fx: Node3D = ClassDB.instantiate("FXPlayer")
	root.add_child(fx)
	var setup: Dictionary = fx.setup(fs)
	_check(setup.ok, "FXPlayer.setup parsed the retail FX INI files %s" % [setup.errors])
	var want_ps := _unique_definitions(fs, "data\\ini\\fxparticlesystem.ini", "FXParticleSystem")
	var want_fx := _unique_definitions(fs, "data\\ini\\fxlist.ini", "FXList")
	_check(want_ps > 1500 and want_fx > 500, "independent scan: %d particle systems, %d FXLists" % [want_ps, want_fx])
	_check(setup.particle_systems == want_ps, "particle system count equals the independent scan: %d of %d" % [setup.particle_systems, want_ps])
	_check(setup.fx_lists == want_fx, "FXList count equals the independent scan: %d of %d" % [setup.fx_lists, want_fx])
	_check(fx.list_particle_systems().size() == want_ps and fx.list_fx_lists().size() == want_fx, "the list_* methods return every name")
	# a particle system emits, the batches reach the scene tree
	_check(fx.play_particle_system("burningTreeFire", Vector3.ZERO) != 0, "burningTreeFire plays")
	_check(fx.play_particle_system("NoSuchSystemName", Vector3.ZERO) == 0, "an unknown particle system is refused (id 0), not defaulted")
	_check(not fx.play_fx_list("NoSuchFXList", Vector3.ZERO, false), "an unknown FXList is refused")
	for i in 40:
		fx.step_once()
	fx.rebuild()
	var stats: Dictionary = fx.get_stats()
	_check(stats.systems >= 1 and stats.particles > 0, "40 steps of burningTreeFire: %d systems, %d particles" % [stats.systems, stats.particles])
	_check(stats.batches >= 1 and stats.gpu_instances + stats.sprites + stats.mesh_vertices > 0, "the particles reached draw batches (%d batches, %d sprites, %d gpu, %d vertices)" % [stats.batches, stats.sprites, stats.gpu_instances, stats.mesh_vertices])
	var multis := 0
	for c in fx.get_children():
		if c is MultiMeshInstance3D or c is MeshInstance3D:
			multis += 1
	_check(multis >= 1, "the batches are %d MultiMesh / mesh children of the player" % multis)
	# the simulation is deterministic for one seed: two players, same steps, same count
	fx.clear()
	fx.seed(77)
	fx.play_fx_list("FX_OilBarrelExplosion", Vector3.ZERO, false)
	for i in 12:
		fx.step_once()
	var first: Dictionary = fx.get_stats()
	fx.clear()
	fx.seed(77)
	fx.play_fx_list("FX_OilBarrelExplosion", Vector3.ZERO, false)
	for i in 12:
		fx.step_once()
	var second: Dictionary = fx.get_stats()
	_check(first.particles > 0 and first.particles == second.particles, "same seed, same particle count (%d, %d)" % [first.particles, second.particles])
	# W3D model emitters (BFME2 w3d.big only)
	fx.clear()
	var em: Dictionary = fx.play_w3d_emitter("e_fire_sm", Vector3.ZERO)
	_check(em.ok, "the W3D emitter e_fire_sm loads %s" % [em.error])
	var bad: Dictionary = fx.play_w3d_emitter("e_no_such_emitter", Vector3.ZERO)
	_check(not bad.ok and not str(bad.error).is_empty(), "a missing emitter file is an error with a message: %s" % [bad.error])
	for i in 60:
		fx.step_once()
	fx.rebuild()
	stats = fx.get_stats()
	_check(stats.emitters == 1 and stats.mesh_vertices > 0, "e_fire_sm after 60 steps draws %d vertices" % stats.mesh_vertices)
	# every stop the stack hit is reported as text with its id
	var unverified: PackedStringArray = fx.get_unverified()
	var joined := "\n".join(unverified)
	_check(joined.contains("S-093") and joined.contains("S-190"), "the S-093 and S-190 stop lines are reported")
	# lane RENDER-4: the fog is applied (S-1651); the S-198 line keeps the GPU particles' unfogged blends
	_check(joined.contains("S-198: the CPU particles fog as RW 0x9620b7"), "the S-198 fog line is reported")
	fx.free()

## Lane PROD-1: a player command queues a horde at a barracks, it is paid, built in retail's frames and its members stand outside; two worlds given
## the same commands agree on the state hash.
func _production(world: Node3D, other: Node3D, skirm: Dictionary, rep: Dictionary) -> void:
	var player := -1
	for i in rep.players.size():
		if String(rep.players[i]).contains("'Player_1' FactionMen"):
			player = i
	_check(player > 0, "production: the lobby slot's player index is %d" % player)
	var hashes: Array = []
	for w in [world, other]:
		if w != world:
			w.load_map("map mp fall back 4p", skirm)
		var b: int = w.create_object("GondorBarracks", player, 2500.0, 2500.0, 0.0)
		_check(b > 0, "production: create_object made a barracks (id %d)" % b)
		w.advance(0.2)
		var money_before: int = w.get_production(player, b).money
		w.queue_unit(player, b, "GondorFighterHorde", false)
		w.set_rally_point(player, b, 2500.0, 2300.0)
		w.advance(0.2) # the commands run in the next logic frame
		var pr: Dictionary = w.get_production(player, b)
		_check(pr.queue.size() == 1 and pr.errors.is_empty() and pr.dispatch_errors.is_empty(), "production: the command queued one entry %s" % [pr.queue])
		_check(money_before - pr.money == 250 or money_before - pr.money == pr.queue[0].cost, "production: the unit was paid at queue time (%d -> %d)" % [money_before, pr.money])
		var frames_before: int = w.get_frame()
		for k in 120:
			w.advance(0.5)
		pr = w.get_production(player, b)
		var members := 0
		for id in w.get_object_ids():
			var o: Dictionary = w.get_object(id)
			if o.template == "GondorFighter" and o.owner == "Player_1":
				members += 1
		_check(pr.queue.is_empty() and members > 0, "production: the horde was built in %d logic frames and %d members exist" % [w.get_frame() - frames_before, members])
		hashes.append(w.get_state_hash())
	_check(hashes[0] == hashes[1], "production: two worlds given the same commands hash alike %s" % [hashes])
	# the produced horde walks to the rally point (lane MOVE-1: the AI runs production's commands): after 60 s its members stand on their slots around it
	# lane PATH-2: create_object now registers the barracks' footprint (retail RW 0x62C98D), so the barracks stands on a site the placement rules accept
	# ((2350, 2500); at the map centre its exit lies on a cliff cell a member cannot path out of)
	world.load_map("map mp fall back 4p", skirm)
	var rb: int = world.create_object("GondorBarracks", player, 2350.0, 2500.0, 0.0)
	world.advance(0.2)
	world.queue_unit(player, rb, "GondorFighterHorde", false)
	world.set_rally_point(player, rb, 2350.0, 2200.0)
	for k in 160:
		world.advance(0.5)
	var horde_obj := {}
	var around := 0
	for id in world.get_object_ids():
		var o: Dictionary = world.get_object(id)
		if o.template == "GondorFighterHorde" and o.owner == "Player_1":
			horde_obj = o
	_check(not horde_obj.is_empty() and Vector2(horde_obj.x - 2350.0, horde_obj.y - 2200.0).length() < 30.0 and horde_obj.idle, "production: the produced horde walked to the rally point (%.0f, %.0f) and stopped" % [horde_obj.get("x", -1), horde_obj.get("y", -1)])
	for m in horde_obj.get("members", []):
		var mo: Dictionary = world.get_object(m)
		if Vector2(mo.x - horde_obj.x, mo.y - horde_obj.y).length() < 120.0 and not mo.moving:
			around += 1
	_check(around == horde_obj.get("members", []).size() and around > 5, "production: its %d members stand around it, not moving" % around)

## Lane HERO-1: the hero list of a lobby slot game (BuildableHeroesMP at the game start, RW 0x6B16EC), a hero recruited from the fortress by its list index, killed
## (a revive record) and revived through the same command path. The rules are pinned by engine/tests/test_hero_*.cpp; this checks the live game's wiring.
func _heroes(world: Node3D, skirm: Dictionary) -> void:
	var rep: Dictionary = world.load_map("map mp fall back 4p", skirm)
	var player := -1
	for i in rep.players.size():
		if String(rep.players[i]).contains("'Player_1' FactionMen"):
			player = i
	var list: Array = world.get_heroes(player)
	_check(list.size() == 8 and list[0].template == "CreateAHero", "heroes: Player_1's hero list holds the 8 BuildableHeroesMP of FactionMen (%d)" % list.size())
	var index := -1
	for h in list:
		if h.template == "RohanEomer":
			index = h.index
	var keep: int = world.create_object("MenFortressCitadel", player, 2350.0, 2500.0, 0.0)
	world.give_money(player, 5000)
	world.advance(0.2)
	world.queue_hero(player, keep, index)
	var hero := 0
	for k in 120:
		world.advance(0.5)
		for id in world.get_object_ids():
			var o: Dictionary = world.get_object(id)
			if o.template == "RohanEomer" and o.owner == "Player_1":
				hero = id
		if hero > 0:
			break
	_check(hero > 0 and world.get_heroes(player).size() == 7, "heroes: RohanEomer was recruited from the fortress by its list index (object %d)" % hero)
	world.kill_hero(hero)
	world.advance(0.2)
	var dead := -1
	for h in world.get_heroes(player, keep):
		if h.template == "RohanEomer" and h.dead:
			dead = h.index
	_check(dead >= 0, "heroes: the dead hero is back in the list as a revive record (index %d)" % dead)
	world.queue_hero(player, keep, dead)
	var back := 0
	for k in 160:
		world.advance(0.5)
		for id in world.get_object_ids():
			var o: Dictionary = world.get_object(id)
			if o.template == "RohanEomer" and o.owner == "Player_1" and id != hero and o.get("health", 0.0) > 0.0:
				back = id
		if back > 0:
			break
	_check(back > 0, "heroes: RohanEomer was revived (object %d)" % back)

## Lane MOVE-1: orders through the lockstep command path. A horde and a walker spawned on open ground of "map mp evendim" are ordered with order_move: they walk
## with the right model conditions, the members follow their slots, a stop stops them, and two worlds fed the same orders in different step sizes agree.
## The rules are pinned by engine/tests/test_move_*.cpp; this checks the Godot side drives them.
func _game_world_orders(fs: RefCounted) -> void:
	if not ClassDB.class_exists("GameWorld"):
		return
	var slots := {"slots": [
		{"player": "Player_1", "faction": "FactionMordor", "human": true, "team": 0},
		{"player": "Player_2", "faction": "FactionMen", "human": false, "team": 1}]}
	var worlds: Array = []
	var ids: Array = []
	for k in 2:
		var w: Node3D = ClassDB.instantiate("GameWorld")
		w.setup(fs)
		var rep: Dictionary = w.load_map("map mp evendim", slots)
		_check(rep.ok, "orders: world %d loads" % k)
		var pidx := -1
		for i in rep.players.size():
			if String(rep.players[i]).contains("'Player_1'"):
				pidx = i
		_check(pidx >= 0, "orders: Player_1 is in the player list (index %d)" % pidx)
		# lane PATH-2: x 4300 (was 4000): the multi-shape footprints of the village north of (4000, 1300) now cover all its houses, so the line north from
		# there bends around them; at 4300 the ground north is open
		var horde: int = w.create_object("MordorFighterHorde", pidx, 4300.0, 1300.0, 0.0)
		var walker: int = w.create_object("MordorFighter", pidx, 4200.0, 1300.0, 0.0)
		_check(horde > 0 and walker > 0, "orders: create_object made a horde (#%d) and a fighter (#%d)" % [horde, walker])
		worlds.append(w)
		ids.append([horde, walker])
	var a: Node3D = worlds[0]
	var b: Node3D = worlds[1]
	var horde_id: int = ids[0][0]
	var walker_id: int = ids[0][1]
	a.advance(0.4)
	b.advance(0.4)
	var before: Dictionary = a.get_object(horde_id)
	_check(before.ok and before.has_ai and before.idle and not before.moving, "orders: the spawned horde has an AI and stands idle")
	var r: Dictionary = a.order_move([horde_id, walker_id], 4300.0, 1600.0, {})
	_check(r.ok and r.objects == 2 and r.messages == 2, "orders: order_move queued a select and a move message %s" % [r])
	var r2: Dictionary = b.order_move(ids[1], 4300.0, 1600.0, {})
	_check(r2.ok, "orders: the second world takes the same order")
	_check(not a.order_move([], 1.0, 1.0, {}).ok, "orders: an order without objects is refused, not ignored")
	_check(not a.order_move([horde_id], 1.0, 1.0, {"type": "teleport"}).ok, "orders: an unknown order type is refused")
	# 1.2 s in 0.1 s steps in one world and in 0.05 s steps in the other
	for i in 12:
		a.advance(0.1)
	for i in 24:
		b.advance(0.05)
	var h1: Dictionary = a.get_object(horde_id)
	var w1: Dictionary = a.get_object(walker_id)
	_check(h1.moving and h1.conditions.has("MOVING") and absf(h1.angle - PI / 2.0) < 0.05, "orders: the horde walks (angle %.0f degrees, conditions %s)" % [rad_to_deg(h1.angle), h1.conditions])
	_check(h1.y > before.y + 15.0 and absf(h1.x - before.x) < 5.0, "orders: the horde moved north: (%.0f, %.0f) from (%.0f, %.0f)" % [h1.x, h1.y, before.x, before.y])
	_check(w1.y > 1330.0, "orders: the fighter walks too (%.0f, %.0f)" % [w1.x, w1.y])
	var transport := 0
	for m in h1.members:
		if a.get_object(m).conditions.has("TRANSPORT_MOVING"):
			transport += 1
	_check(transport == h1.members.size() and transport > 10, "orders: the members of the marching horde carry TRANSPORT_MOVING (%d of %d)" % [transport, h1.members.size()])
	_check(a.get_frame() == b.get_frame() and a.get_state_hash() == b.get_state_hash(), "orders: two worlds fed the same order in different step sizes agree at frame %d (hash %d == %d)" % [a.get_frame(), a.get_state_hash(), b.get_state_hash()])
	var stats: Dictionary = a.get_stats()
	_check(stats.commands.moves == 1 and stats.commands.rejected == 0, "orders: the command path counted one move %s" % [stats.commands])
	# stop
	var s: Dictionary = a.order_stop([horde_id, walker_id])
	_check(s.ok, "orders: order_stop queued")
	for i in 20:
		a.advance(0.1)
	var h2: Dictionary = a.get_object(horde_id)
	_check(h2.idle and not h2.moving and not h2.conditions.has("MOVING"), "orders: the stopped horde is idle without MOVING (conditions %s)" % [h2.conditions])
	for m in h2.members:
		var mi: Dictionary = a.get_object(m)
		if mi.conditions.has("TRANSPORT_MOVING"):
			_check(false, "orders: a member still carries TRANSPORT_MOVING after the stop")
			break
	a.queue_free()
	b.queue_free()
