## Deterministic CPU submission regression. Run through pose_buffer_regression.py (runtime synthetic W3D source).
## --reference records old behavior with the diagnostic getter only; optimization assertions run on the fixed extension.
extends SceneTree

var _failures := 0
var _checks := 0
var _steps: Array = []
var _submissions: Array = []
var _inst: Node3D
var _optimized := true
var _snapshot_path := ""
var _previous_palette := PackedFloat32Array()


func _initialize() -> void:
	for arg in OS.get_cmdline_user_args():
		if arg == "--reference":
			_optimized = false
		elif arg.begins_with("--snapshot="):
			_snapshot_path = arg.trim_prefix("--snapshot=")
	_run.call_deferred()


func _check(ok: bool, what: String) -> void:
	_checks += 1
	if not ok:
		_failures += 1
		print("FAIL ", what)


func _draws() -> Array:
	return _inst.get_draw_buffers()


func _pose(id: int, clip: String, frame: float) -> void:
	_check(_inst.set_instance_pose(id, clip, frame, "", 0.0, 0.0), "pose request")


func _step(label: String, expected_draw_writes: int) -> void:
	_inst.update_now()
	var resources: Array = []
	for child in _inst.get_children():
		if child is MultiMeshInstance3D:
			resources.append(child.multimesh)
	var cpu: Array = _draws()
	_check(resources.size() == cpu.size(), label + ": resource count")
	for k in cpu.size():
		_check(resources[k].buffer.to_byte_array() == cpu[k].buffer.to_byte_array(), label + ": MultiMesh resource buffer")
		_check(resources[k].visible_instance_count == cpu[k].order.size(), label + ": MultiMesh resource visible count")
	var stats: Dictionary = _inst.get_stats()
	if _optimized:
		_check(int(stats.buffer_draw_writes) == expected_draw_writes, "%s: draw writes %s expected %d" % [label, stats.buffer_draw_writes, expected_draw_writes])
	var draws: Array = []
	for draw: Dictionary in _draws():
		draws.append([draw.model, draw.subobject, Array(draw.order), draw.buffer.to_byte_array().hex_encode()])
	var palettes: Array = []
	for id in _inst.get_instance_count():
		palettes.append(_inst.get_instance_palette(id).to_byte_array().hex_encode())
	# Compare exact full capacity buffers (including unused tails), counts, stable instance order, live palettes and cached reports.
	_steps.append([label, draws, palettes, stats.visible_draw_instances, stats.dithered_fade_instances,
		stats.sorted_draw_items, stats.interleaved_batch_pairs, stats.pose_evaluations])
	_submissions.append([label, stats.buffer_writes, stats.buffer_draw_writes, stats.buffer_bytes])
	print("STEP %s draws=%d poses=%d bytes=%d" % [label, stats.get("buffer_draw_writes", -1), stats.pose_evaluations, stats.get("buffer_bytes", -1)])


func _run() -> void:
	if OS.get_environment("ROTWK_INSTALL").is_empty() or OS.get_environment("BFME2_INSTALL").is_empty():
		print("SKIP: set ROTWK_INSTALL and BFME2_INSTALL")
		quit(77)
		return
	var fs: RefCounted = ClassDB.instantiate("RetailFileSystem")
	var mount: Dictionary = fs.mount_retail()
	_check(mount.ok, "mount: %s" % [mount.get("errors", [])])
	if not mount.ok:
		quit(1)
		return
	_inst = ClassDB.instantiate("W3DInstancer")
	root.add_child(_inst)
	_inst.set_auto_update(false)
	_inst.set_playing(false)
	_inst.set_worker_threads(2)
	_check(_inst.setup(fs).ok, "setup")
	_inst.set_sort_camera(Transform3D.IDENTITY)
	var opaque: int = _inst.add_model("PB_OPAQUE")
	var mixed: int = _inst.add_model("PB_MIX")
	var flat: int = _inst.add_model("PB_FLAT.M")
	_check([opaque, mixed, flat] == [0, 1, 2], "three synthetic models")
	if [opaque, mixed, flat] != [0, 1, 2]:
		print(_inst.get_errors())
		quit(1)
		return
	for model in [opaque, mixed]:
		for i in 3:
			var id: int = _inst.add_instance(model, Transform3D(Basis.IDENTITY, Vector3(0, 0, -8.0 * (i + 1))), "PB_MOVE", 0.0, 1.0)
			_check(id == model * 3 + i, "stable creation ids")
	_check(_inst.add_instance(flat, Transform3D.IDENTITY, "", 0.0, 1.0) == 6, "skeletonless instance")
	_step("initial", 6)
	_check(_draws().size() == 6, "two opaque, three mixed and one skeletonless draw")
	_previous_palette = _inst.get_instance_palette(0)
	_inst.set_global_time(1.0)
	_step("clock_bone_only", 1)
	if _optimized:
		_check(_inst.get_stats().buffer_bytes == 256, "bone-only update submits one capacity-four sorted buffer")
	_check(_inst.get_instance_palette(0).to_byte_array() != _previous_palette.to_byte_array(), "bone-only clock change updates palette")
	_step("stopped_clock", 0)
	for id in 6:
		_pose(id, "PB_MOVE", 2.0)
	_step("explicit_bone_only", 1)
	_pose(3, "PB_MOVE", 1.0)
	_pose(4, "PB_MOVE", 2.0)
	_pose(5, "PB_MOVE", 0.0)
	_step("sorted_depth_crossing", 1)
	_check(Array(_draws()[4].order) == [3, 5, 4], "stationary camera sorts animated pivot depths")
	_inst.set_sort_camera(Transform3D(Basis(Vector3.UP, PI), Vector3.ZERO))
	_step("camera_turn", 1)
	_check(Array(_draws()[4].order) == [4, 5, 3], "camera movement reverses sorted order")
	_inst.transform = Transform3D(Basis.IDENTITY, Vector3(0, 0, 5))
	_step("node_transform", 1)
	_inst.set_sort_camera(Transform3D.IDENTITY)
	_step("camera_reset", 1)
	_pose(0, "PB_VIS", 1.0)
	_step("pivot_visibility_off", 1)
	_check(Array(_draws()[1].order) == [1, 2], "visibility filters skin draw at its pivot")
	_pose(0, "PB_VIS", 2.0)
	_step("visibility_unchanged", 0)
	_pose(0, "PB_VIS", 3.0)
	_step("pivot_visibility_on", 1)
	_check(Array(_draws()[1].order) == [0, 1, 2], "visibility restores insertion order")
	_pose(0, "PB_FADE", 1.0)
	_step("pivot_fade_half", 1)
	_check(_draws()[0].buffer[13] == 0.5, "fade reaches instance custom data")
	_check(_inst.get_stats().dithered_fade_instances == 1, "fade report")
	_step("fade_cached", 0)
	_check(_inst.get_stats().dithered_fade_instances == 1, "skipped draw retains fade report")
	_pose(0, "PB_FADE", 2.0)
	_step("pivot_fade_zero", 1)
	_check(Array(_draws()[0].order) == [1, 2], "zero fade filters rigid draw")
	_pose(0, "PB_FADE", 3.0)
	_step("pivot_fade_one", 1)
	_check(Array(_draws()[0].order) == [0, 1, 2], "fade restores rigid draw")
	# Culling's on-demand query mutates Pose before update_now. It must not hide a pending visibility change from invalidation.
	_inst.set_pose_culling(true)
	_pose(0, "PB_VIS", 1.0)
	_inst.get_bone_position(0, 2)
	_step("bone_query_before_update", 1)
	_check(Array(_draws()[1].order) == [1, 2], "query preserves pending membership update")
	_inst.set_pose_culling(false)
	_inst.set_instance_transform(1, Transform3D(Basis(Vector3.UP, 0.5), Vector3(7, 8, 9)))
	_step("instance_transform", 2)
	_inst.set_instance_house_color(1, Color(0.25, 0.5, 0.75, 1))
	_step("tint", 2)
	_inst.set_instance_infantry_light(1, true)
	_step("infantry_light", 2)
	_inst.set_instance_fogged(1, true)
	_step("fog", 2)
	_inst.set_instance_sway(1, 2)
	_step("sway", 2)
	_inst.set_instance_opacity(1, 0.5)
	_step("opacity_half", 2)
	_inst.set_instance_opacity(1, 0.0)
	_step("opacity_zero", 2)
	_check(Array(_draws()[0].order) == [0, 2], "opacity zero filters instance")
	_inst.set_instance_opacity(1, 1.0)
	_check(_inst.set_instance_hidden_subobjects(1, ["PB_OPAQUE.RIGID"]), "hide subobject")
	_step("hidden_subobject", 2)
	_check(Array(_draws()[0].order) == [0, 2], "hidden subobject filters draw")
	_check(_inst.set_instance_hidden_subobjects(1, []), "show subobject")
	_step("shown_subobject", 2)
	# The merged palette allocator moves a static block into the animated pool (three pivots, nine texels).
	var late: int = _inst.add_instance(opaque, Transform3D.IDENTITY, "", 0.0, 1.0)
	_check(late == 7, "late bind instance")
	_step("add_bind", 2)
	var old_base: float = _draws()[0].buffer[3 * 16 + 12]
	_pose(late, "PB_MOVE", 0.0)
	_step("bind_to_animation", 2)
	_check(_draws()[0].buffer[3 * 16 + 12] == old_base + 9.0, "animation relocates palette and rewrites its draw records")
	# Growth must rewrite capacity; removals retain the same untouched tail bytes as the baseline.
	_check(_inst.add_instance(opaque, Transform3D.IDENTITY, "PB_MOVE", 0.0, 1.0) == 8, "growth instance")
	_step("capacity_growth", 2)
	_check(_draws()[0].buffer.size() == 96, "capacity grows four to six")
	_check(_inst.remove_instance(1), "remove opaque")
	_step("remove_opaque", 2)
	_check(_inst.remove_instance(3), "remove sorted first")
	_step("remove_sorted", 3)
	_check(_inst.remove_instance(4) and _inst.remove_instance(5), "remove sorted remainder")
	_step("sorted_batch_gone", 3)
	_check(_inst.get_stats().sorted_draw_items == 0, "sorted report clears after last removal")
	_check(_inst.remove_instance(6), "remove skeletonless")
	_step("remove_skeletonless", 1)
	_check(_inst.add_instance(flat, Transform3D.IDENTITY, "", 0.0, 1.0) == 9, "skeletonless palette reuse")
	_step("skeletonless_reuse", 1)
	_step("settled", 0)
	_check(_inst.get_errors().size() == 0, "no engine errors")
	_sort_batches(fs)
	_opaque_population(fs)
	_reuse_nondefault_draw_state(fs)
	if not _snapshot_path.is_empty():
		var output := FileAccess.open(_snapshot_path, FileAccess.WRITE)
		output.store_string(JSON.stringify(_steps))
		var submissions := FileAccess.open(_snapshot_path + ".submissions.json", FileAccess.WRITE)
		submissions.store_string(JSON.stringify(_submissions))
	_inst.free()
	print("POSE BUFFER %s checks=%d failures=%d steps=%d mode=%s" % ["PASS" if _failures == 0 else "FAIL", _checks, _failures, _steps.size(), "optimized" if _optimized else "reference observation"])
	quit(0 if _failures == 0 else 1)


func _reset(fs: RefCounted) -> void:
	_inst.free()
	_inst = ClassDB.instantiate("W3DInstancer")
	root.add_child(_inst)
	_inst.set_auto_update(false)
	_inst.set_playing(false)
	_inst.set_worker_threads(2)
	_check(_inst.setup(fs).ok, "fresh setup")
	_inst.set_sort_camera(Transform3D.IDENTITY)


func _sort_batches(fs: RefCounted) -> void:
	_reset(fs)
	for model in 2:
		_check(_inst.add_model("PB_MIX") == model, "second sorted batch model")
		for i in 2:
			var depth := 10.0 + 10.0 * model + 20.0 * i
			_check(_inst.add_instance(model, Transform3D(Basis.IDENTITY, Vector3(0, 0, -depth)), "PB_MOVE", 0.0, 1.0) == model * 2 + i, "sorted batch instance")
	_step("two_batches_initial", 6)
	_check(_inst.get_stats().interleaved_batch_pairs == 1, "two sorted intervals interleave")
	for id in 2:
		_pose(id, "PB_MOVE", 2.0)
	_step("one_batch_depth_changed", 1)
	_check(_inst.get_stats().interleaved_batch_pairs == 0, "one updated batch separates from retained batch")
	_step("two_batches_cached", 0)
	_check(_inst.get_stats().sorted_draw_items == 2, "both sorted reports survive cached buffers")
	for id in 2:
		_pose(id, "PB_MOVE", 0.0)
	_step("one_batch_interleaves_again", 1)
	_check(_inst.get_stats().interleaved_batch_pairs == 1, "interleaving returns with retained second batch")


func _opaque_population(fs: RefCounted) -> void:
	_reset(fs)
	_check(_inst.add_model("PB_OPAQUE") == 0, "opaque population model")
	for id in 100:
		_check(_inst.add_instance(0, Transform3D(Basis.IDENTITY, Vector3(id, 0, 0)), "PB_MOVE", 0.0, 1.0) == id, "opaque population instance")
	_step("army_initial", 2)
	for id in 100:
		_pose(id, "PB_MOVE", 1.0)
	_step("army_stationary_bones", 0)
	_check(_inst.get_stats().pose_evaluations == 100, "all stationary opaque instances posed")
	# Record writes remain conservative whenever any instance transform changes; all poses also change in each ratio case.
	var frame := 0
	for moving in [100, 10, 1, 0]:
		for id in 100:
			_pose(id, "PB_MOVE", float(frame))
		for id in moving:
			_inst.set_instance_transform(id, Transform3D(Basis.IDENTITY, Vector3(id, moving, frame)))
		_step("army_moving_%d_percent" % moving, 0 if moving == 0 else 2)
		frame = (frame + 1) % 3
	_check(_inst.add_instance(0, Transform3D.IDENTITY, "PB_MOVE", 0.0, 1.0) == 100, "army grows beyond capacity")
	_step("army_growth", 2)
	_check(_draws()[0].buffer.size() == 2400, "army capacity grows 100 to 150")
	_check(_inst.remove_instance(100), "army removal leaves spare capacity")
	_step("army_remove", 2)
	for id in 100:
		_pose(id, "PB_MOVE", 1.5)
	_step("army_bones_spare_capacity", 0)
	_check(_inst.get_stats().pose_evaluations == 100, "all remaining poses change with spare capacity")
	var retained_palette: PackedFloat32Array = _inst.get_instance_palette(0)
	var retained_base: float = _draws()[0].buffer[12]
	# 114 three-pivot allocations cross public main's 1024-texel palette row boundary.
	for id in range(101, 115):
		_check(_inst.add_instance(0, Transform3D.IDENTITY, "PB_MOVE", 0.0, 1.0) == id, "palette growth instance")
	_step("army_palette_growth", 2)
	_check(_inst.get_stats().palette_rows == 2, "palette image grows to two rows")
	_check(_inst.get_instance_palette(0).to_byte_array() == retained_palette.to_byte_array(), "palette growth retains existing matrices")
	_check(_draws()[0].buffer[12] == retained_base, "palette growth retains existing public base")
	for id in 100:
		_pose(id, "PB_MOVE", 2.0)
	_step("army_after_palette_growth_bones", 0)
	_check(_inst.get_errors().size() == 0, "no population errors")


func _reuse_nondefault_draw_state(fs: RefCounted) -> void:
	_reset(fs)
	_check(_inst.add_model("PB_OPAQUE") == 0, "reuse model")
	for id in 3:
		_check(_inst.add_instance(0, Transform3D.IDENTITY, "PB_MOVE", 0.0, 1.0) == id, "reuse original instance")
	_pose(0, "PB_VIS", 1.0)
	_pose(1, "PB_FADE", 1.0)
	_step("reuse_nondefault_state", 2)
	_check(Array(_draws()[0].order) == [0, 1, 2], "fade-half instance is a member")
	_check(Array(_draws()[1].order) == [1, 2], "invisible instance is absent")
	_check(_inst.remove_instance(0) and _inst.remove_instance(1), "remove instances with nondefault state")
	_step("reuse_remove_nondefault", 2)
	_check(_inst.add_instance(0, Transform3D.IDENTITY, "PB_MOVE", 0.0, 1.0) == 3, "first new public id")
	_check(_inst.add_instance(0, Transform3D.IDENTITY, "PB_MOVE", 0.0, 1.0) == 4, "second new public id")
	_step("reuse_new_instances", 2)
	_check(Array(_draws()[0].order) == [2, 3, 4] and Array(_draws()[1].order) == [2, 3, 4], "new instances retain creation order and default membership")
	_check(_draws()[0].buffer[16 + 13] == 1.0 and _draws()[0].buffer[32 + 13] == 1.0, "new instances have default fade")
	for id in [2, 3, 4]:
		_pose(id, "PB_MOVE", 1.0)
	_step("reuse_bone_only", 0)
	_check(_inst.get_stats().pose_evaluations == 3, "reused state is coherent for later bone changes")
	_check(_inst.get_errors().is_empty(), "no reuse errors")
