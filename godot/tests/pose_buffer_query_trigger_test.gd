## Query/culling state must be consumed by the same model-wide triggers as the baseline.
## Run through pose_buffer_regression.py. Snapshots include actual MultiMesh bytes/counts and CPU palettes.
extends SceneTree

var _checks := 0
var _failures := 0
var _snapshots: Array = []
var _optimized := true
var _fs: RefCounted
var _camera: Camera3D
var _inst: Node3D


func _initialize() -> void:
	_optimized = not OS.get_cmdline_user_args().has("--reference")
	_run.call_deferred()


func _check(ok: bool, label: String) -> void:
	_checks += 1
	if not ok:
		_failures += 1
		print("FAIL ", label)


func _pose(id: int, clip: String, frame: float) -> void:
	_check(_inst.set_instance_pose(id, clip, frame, "", 0.0, 0.0), "pose request")


func _query(id: int, clip: String, frame: float) -> void:
	_pose(id, clip, frame)
	var bone := 2 if clip == "PB_VIS" else 1
	# Alternate the first on-demand evaluation between the two production query paths.
	var a: Vector3 = _inst.test_bone_position(id, bone) if clip == "PB_VIS" else _inst.get_bone_position(id, bone)
	var b: Vector3 = _inst.get_bone_position(id, bone) if clip == "PB_VIS" else _inst.test_bone_position(id, bone)
	_check(a == b, "native transform and bound position query agree exactly")


func _queue(pending: int, comparisons := -1) -> void:
	if _optimized:
		_check(_inst.get_stats().pending_query_poses == pending, "deduplicated pending query count")
		if comparisons != -1:
			_check(_inst.get_stats().query_pose_comparisons == comparisons, "bounded queried-pose comparisons")


func _resources() -> Array:
	var resources: Array = []
	for child in _inst.get_children():
		if child is MultiMeshInstance3D:
			resources.append(child.multimesh)
	return resources


func _snapshot(label: String) -> void:
	var draws: Array = _inst.get_draw_buffers()
	var resources := _resources()
	_check(resources.size() == draws.size(), label + ": resource count")
	var states: Array = []
	for k in mini(resources.size(), draws.size()):
		_check(resources[k].buffer.to_byte_array() == draws[k].buffer.to_byte_array(), label + ": resource bytes match prepared buffer")
		_check(resources[k].visible_instance_count == draws[k].order.size(), label + ": resource count matches prepared order")
		states.append([draws[k].model, draws[k].subobject, Array(draws[k].order), resources[k].buffer.to_byte_array().hex_encode(), resources[k].visible_instance_count])
	var palettes: Array = []
	for id in _inst.get_instance_count():
		palettes.append(_inst.get_instance_palette(id).to_byte_array().hex_encode())
	var stats: Dictionary = _inst.get_stats()
	_snapshots.append([label, states, palettes, stats.pose_evaluations, stats.pose_culled, stats.visible_draw_instances,
		stats.dithered_fade_instances, stats.sorted_draw_items, stats.interleaved_batch_pairs])
	print("QUERY TRIGGER %s writes=%d pending=%d comparisons=%d" % [label, stats.buffer_draw_writes, stats.get("pending_query_poses", -1), stats.get("query_pose_comparisons", -1)])


func _pair(model := "PB_OPAQUE", initial_update := true) -> void:
	if is_instance_valid(_inst):
		_check(_inst.get_errors().is_empty(), "no engine errors")
		_inst.free()
	_camera.position = Vector3.ZERO
	await process_frame
	_inst = ClassDB.instantiate("W3DInstancer")
	root.add_child(_inst)
	_inst.set_auto_update(false)
	_inst.set_playing(false)
	_inst.set_sort_camera(Transform3D.IDENTITY)
	_check(_inst.setup(_fs).ok, "setup")
	_check(_inst.add_model(model) == 0, "model")
	_check(_inst.add_instance(0, Transform3D(Basis.IDENTITY, Vector3(10000, 0, -20)), "PB_MOVE", 0.0, 1.0) == 0, "A")
	_check(_inst.add_instance(0, Transform3D(Basis.IDENTITY, Vector3(0, 0, -20)), "PB_MOVE", 0.0, 1.0) == 1, "B")
	if initial_update:
		_inst.update_now()
	_check(not _camera.is_position_in_frustum(Vector3(10000, 0, -20)), "actual frustum excludes A")
	_check(_camera.is_position_in_frustum(Vector3(0, 0, -20)), "actual frustum includes B")
	_inst.set_pose_culling(true)


func _five_reproductions() -> void:
	for mode in ["visibility_bone", "fade_half_bone", "fade_zero_visibility", "visibility_fade", "mixed_visibility_bone"]:
		await _pair("PB_MIX" if mode.begins_with("mixed") else "PB_OPAQUE")
		_snapshot(mode + ":initial")
		var clip := "PB_FADE" if mode.begins_with("fade") else "PB_VIS"
		_query(0, clip, 2.0 if mode.begins_with("fade_zero") else 1.0)
		_queue(1)
		_pose(1, "PB_VIS" if mode == "fade_zero_visibility" else ("PB_FADE" if mode == "visibility_fade" else "PB_MOVE"), 1.0)
		_inst.update_now()
		_snapshot(mode + ":pose_trigger")
		_check(_inst.get_stats().pose_culled == 1 and _inst.get_stats().pose_evaluations == 1, "only in-frustum B evaluated")
		var draw := 0 if clip == "PB_FADE" else 1
		var draws: Array = _inst.get_draw_buffers()
		var resources := _resources()
		if mode == "fade_half_bone":
			_check(draws[0].buffer[13] == 0.5, "queried half fade consumed")
			_check(resources[0].buffer[13] == 0.5, "actual resource half fade consumed")
		else:
			_check(Array(draws[draw].order) == [1], "queried membership consumed")
			_check(resources[draw].visible_instance_count == 1, "actual resource queried membership consumed")
		_queue(0, 1 if mode in ["fade_zero_visibility", "visibility_fade"] else 2)
		_inst.update_now()
		_snapshot(mode + ":settled")
		_queue(0, 0)


func _query_only_and_deduplication() -> void:
	await _pair()
	_query(0, "PB_VIS", 1.0)
	for repeat in 64:
		_inst.get_bone_position(0, 2)
	_queue(1)
	for repeat in 3:
		_inst.update_now()
		_snapshot("query_only_%d" % repeat)
		_check(Array(_inst.get_draw_buffers()[1].order) == [0, 1], "query alone retains old membership")
		_check(_resources()[1].visible_instance_count == 2, "query alone retains actual resource count")
		_queue(1, 0)
	_pose(1, "PB_MOVE", 0.0) # explicit mode evaluates even though the resulting matrices equal the initial pose
	_inst.update_now()
	_snapshot("query_only_then_equal_pose_trigger")
	_check(_inst.get_stats().pose_evaluations == 1, "equal-result pose evaluation is still a baseline trigger")
	_check(Array(_inst.get_draw_buffers()[1].order) == [1], "pending query consumed on later trigger")
	_queue(0, 2)


func _different_model_and_new_draws() -> void:
	await _pair()
	_query(0, "PB_VIS", 1.0)
	_check(_inst.add_model("PB_MIX") == 1, "new model adds different draws")
	_inst.update_now()
	_snapshot("different_model_empty_draws")
	_check(Array(_inst.get_draw_buffers()[1].order) == [0, 1], "another model's new draws do not consume A")
	_queue(1, 0)
	_check(_inst.add_instance(1, Transform3D(Basis.IDENTITY, Vector3(0, 0, -20)), "PB_MOVE", 0.0, 1.0) == 2, "new model instance")
	_inst.update_now()
	_snapshot("different_model_pose")
	_check(Array(_inst.get_draw_buffers()[1].order) == [0, 1], "another model's pose does not consume A")
	_queue(1, 0)
	_pose(1, "PB_MOVE", 1.0)
	_inst.update_now()
	_snapshot("original_model_pose")
	_check(Array(_inst.get_draw_buffers()[1].order) == [1], "original model trigger consumes pending query")
	_queue(0, 2)


func _query_before_property_rewrite() -> void:
	await _pair()
	_query(0, "PB_VIS", 1.0)
	_inst.set_instance_opacity(1, 0.5)
	_inst.update_now()
	_snapshot("query_then_property_rewrite")
	_check(Array(_inst.get_draw_buffers()[1].order) == [1], "property rewrite consumes query")
	_queue(0, 0)
	_query(0, "PB_VIS", 3.0)
	_inst.update_now()
	_snapshot("requeued_after_property_query_only")
	_check(Array(_inst.get_draw_buffers()[1].order) == [1], "new query alone retains consumed hidden state")
	_queue(1, 0)
	_pose(1, "PB_MOVE", 1.0)
	_inst.update_now()
	_snapshot("requeued_after_property_pose_trigger")
	_check(Array(_inst.get_draw_buffers()[1].order) == [0, 1], "later pose consumes requeued visible state")
	_queue(0, 2)


func _removal_and_fresh_ids() -> void:
	await _pair()
	_query(0, "PB_VIS", 1.0)
	_check(_inst.remove_instance(0), "remove queued instance")
	_inst.get_bone_position(0, 2)
	_check(_inst.add_instance(0, Transform3D(Basis.IDENTITY, Vector3(10000, 0, -20)), "PB_MOVE", 0.0, 1.0) == 2, "fresh public id with reused palette block")
	_inst.update_now()
	_snapshot("removed_pending_and_new_instance")
	_check(Array(_inst.get_draw_buffers()[1].order) == [1], "removed candidate and new culled unevaluated pose contribute no membership")
	_queue(0, 0)
	_query(2, "PB_FADE", 1.0)
	_pose(1, "PB_MOVE", 1.0)
	_inst.update_now()
	_snapshot("fresh_instance_query_trigger")
	_check(_resources()[0].buffer[16 + 13] == 0.5, "fresh id's queried fade is consumed")
	_queue(0, 2)


func _restored_and_latest_queries() -> void:
	await _pair()
	_query(0, "PB_VIS", 1.0)
	_query(0, "PB_VIS", 3.0)
	_queue(1)
	_pose(1, "PB_MOVE", 1.0)
	_inst.update_now()
	_snapshot("queries_restore_original_before_trigger")
	_check(Array(_inst.get_draw_buffers()[1].order) == [0, 1], "latest query restores original state")
	if _optimized:
		_check(_inst.get_stats().buffer_draw_writes == 0, "equal state can clear pending query without submissions")
	_queue(0, 2)
	_query(0, "PB_VIS", 1.0)
	_query(0, "PB_FADE", 1.0)
	_queue(1)
	_pose(1, "PB_MOVE", 2.0)
	_inst.update_now()
	_snapshot("latest_query_changes_clip")
	_check(Array(_inst.get_draw_buffers()[1].order) == [0, 1], "latest clip visibility supersedes earlier hidden query")
	_check(_resources()[0].buffer[13] == 0.5, "latest clip fade is consumed")
	_queue(0, 2)


func _multiple_candidates() -> void:
	await _pair()
	_check(_inst.add_instance(0, Transform3D(Basis.IDENTITY, Vector3(12000, 0, -20)), "PB_MOVE", 0.0, 1.0) == 2, "second offscreen candidate")
	_inst.update_now()
	_query(0, "PB_VIS", 1.0)
	_query(2, "PB_FADE", 1.0)
	_queue(2)
	_pose(1, "PB_MOVE", 1.0)
	_inst.update_now()
	_snapshot("multiple_pending_candidates")
	_check(Array(_inst.get_draw_buffers()[1].order) == [1, 2], "both queued candidates considered")
	_check(_resources()[0].buffer[32 + 13] == 0.5, "second candidate's resource fade consumed")
	_queue(0, 3)


func _camera_and_node_triggers() -> void:
	await _pair("PB_MIX")
	_query(0, "PB_VIS", 1.0)
	_inst.update_now()
	_snapshot("sorted_query_only_stationary_camera")
	_check(Array(_inst.get_draw_buffers()[1].order) == [0, 1], "sorted model query alone retains membership")
	_queue(1, 0)
	_inst.set_sort_camera(Transform3D(Basis.IDENTITY, Vector3(0, 0, 10)))
	_inst.update_now()
	_snapshot("sorted_camera_only_trigger")
	_check(_inst.get_stats().pose_evaluations == 0, "camera-only baseline trigger")
	_check(_resources()[1].visible_instance_count == 1, "sorted camera trigger consumes unsorted queried membership")
	_queue(0, 2)
	_query(0, "PB_VIS", 3.0)
	_inst.transform = Transform3D(Basis.IDENTITY, Vector3(0, 1, 0))
	_inst.update_now()
	_snapshot("sorted_node_only_trigger")
	_check(_inst.get_stats().pose_evaluations == 0, "node-only baseline trigger")
	_check(_resources()[1].visible_instance_count == 2, "sorted node trigger consumes restored membership")
	_queue(0, 2)
	await _pair()
	_query(0, "PB_VIS", 1.0)
	_inst.set_sort_camera(Transform3D(Basis.IDENTITY, Vector3(0, 0, 10)))
	_inst.update_now()
	_snapshot("opaque_camera_only_no_trigger")
	_check(_resources()[1].visible_instance_count == 2, "opaque camera change alone does not consume query")
	_queue(1, 0)
	_pose(1, "PB_MOVE", 1.0)
	_inst.update_now()
	_snapshot("opaque_pose_after_camera_change")
	_check(_resources()[1].visible_instance_count == 1, "later opaque pose consumes query")
	_queue(0, 2)


func _all_culled_and_initial_draws() -> void:
	await _pair()
	_query(0, "PB_VIS", 1.0)
	_pose(1, "PB_MOVE", 1.0)
	_camera.position.x = -10000
	await process_frame
	_inst.update_now()
	_snapshot("all_stale_instances_culled")
	_check(_inst.get_stats().pose_evaluations == 0 and _inst.get_stats().pose_culled == 2, "requests alone are not an evaluation trigger")
	_check(_resources()[1].visible_instance_count == 2, "all-culled update retains prior membership")
	_queue(1, 0)
	_camera.position.x = 0
	await process_frame
	_inst.update_now()
	_snapshot("one_stale_instance_returns")
	_check(_resources()[1].visible_instance_count == 1, "returning B triggers consumption of still-culled A")
	_queue(0, 2)
	await _pair("PB_OPAQUE", false)
	_query(0, "PB_VIS", 1.0)
	_queue(1)
	_inst.update_now()
	_snapshot("query_before_initial_draw_allocation")
	_check(_resources()[1].visible_instance_count == 1, "initial full rewrite consumes pending query")
	_queue(0, 0)


func _unchanged_queries_and_culling_disabled() -> void:
	await _pair()
	for repeat in 16:
		_inst.get_bone_position(0, 2)
	_queue(0)
	_inst.update_now()
	_snapshot("queries_of_unchanged_pose")
	_queue(0, 0)
	_inst.set_pose_culling(false)
	_query(0, "PB_VIS", 1.0)
	_queue(0)
	_inst.update_now()
	_snapshot("query_while_culling_disabled")
	_check(_inst.get_stats().pose_evaluations == 1, "disabled-cull query leaves normal evaluation pending")
	_check(_resources()[1].visible_instance_count == 1, "ordinary evaluation consumes visibility")
	_queue(0, 0)



func _filtered_cache_states() -> void:
	# A full rewrite consumes every pose even when another membership filter excludes it.
	for filter in ["hidden_sub", "opacity_zero"]:
		for clip in ["PB_VIS", "PB_FADE"]:
			await _pair()
			if filter == "hidden_sub":
				_check(_inst.set_instance_hidden_subobjects(0, ["PB_OPAQUE.RIGID", "PB_OPAQUE.SKIN"]), "hide A")
			else:
				_inst.set_instance_opacity(0, 0.0)
			_inst.update_now()
			_query(0, clip, 1.0)
			_inst.set_instance_opacity(1, 0.5)
			_inst.update_now()
			_snapshot(filter + clip + ":consumed_filtered_query")
			_queue(0, 0)
			_query(0, clip, 3.0)
			_pose(1, "PB_MOVE", 1.0)
			_inst.update_now()
			_snapshot(filter + clip + ":restore_while_filtered")
			_queue(0, 2)
			if filter == "hidden_sub":
				_check(_inst.set_instance_hidden_subobjects(0, []), "show A")
			else:
				_inst.set_instance_opacity(0, 1.0)
			_inst.update_now()
			_snapshot(filter + clip + ":unfiltered")
			_check(Array(_inst.get_draw_buffers()[0].order) == [0, 1], "unfiltered rigid membership")
			_check(Array(_inst.get_draw_buffers()[1].order) == [0, 1], "unfiltered skin membership")


func _no_camera() -> void:
	_check(_inst.get_errors().is_empty(), "no previous errors")
	_inst.free()
	_camera.free()
	_inst = ClassDB.instantiate("W3DInstancer")
	root.add_child(_inst)
	_inst.set_auto_update(false)
	_inst.set_playing(false)
	_inst.set_pose_culling(true)
	_check(_inst.setup(_fs).ok, "no-camera setup")
	_check(_inst.add_model("PB_MIX") == 0, "no-camera model")
	_check(_inst.add_instance(0, Transform3D.IDENTITY, "PB_MOVE", 0.0, 1.0) == 0, "no-camera A")
	_inst.update_now()
	_snapshot("no_camera_initial")
	_inst.update_now()
	_snapshot("no_camera_unchanged")
	if _optimized:
		_check(_inst.get_stats().buffer_draw_writes == 1, "no camera still invalidates sorted draw")
	_query(0, "PB_VIS", 1.0)
	_inst.update_now()
	_snapshot("no_camera_queried_visibility")
	_queue(0, 1)
	_check(_resources()[1].visible_instance_count == 0, "no-camera hidden query consumed")
	_query(0, "PB_FADE", 1.0)
	_inst.update_now()
	_snapshot("no_camera_queried_half_fade")
	_check(_resources()[0].buffer[13] == 0.5, "no-camera half fade")
	_query(0, "PB_FADE", 2.0)
	_inst.update_now()
	_snapshot("no_camera_queried_zero_fade")
	_check(_resources()[0].visible_instance_count == 0, "no-camera zero fade")
	_query(0, "PB_FADE", 3.0)
	_inst.update_now()
	_snapshot("no_camera_queried_restored")
	_check(_resources()[0].visible_instance_count == 1, "no-camera restored")


func _run() -> void:
	_fs = ClassDB.instantiate("RetailFileSystem")
	var mount: Dictionary = _fs.mount_retail()
	_check(mount.ok, "mount: %s" % [mount.get("errors", [])])
	if not mount.ok:
		quit(1)
		return
	_camera = Camera3D.new()
	root.add_child(_camera)
	_camera.make_current()
	await _five_reproductions()
	await _query_only_and_deduplication()
	await _different_model_and_new_draws()
	await _query_before_property_rewrite()
	await _removal_and_fresh_ids()
	await _restored_and_latest_queries()
	await _multiple_candidates()
	await _camera_and_node_triggers()
	await _all_culled_and_initial_draws()
	await _unchanged_queries_and_culling_disabled()
	await _filtered_cache_states()
	await _no_camera()
	_check(_inst.get_errors().is_empty(), "no engine errors")
	_inst.free()
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--snapshot="):
			FileAccess.open(arg.trim_prefix("--snapshot="), FileAccess.WRITE).store_string(JSON.stringify(_snapshots))
	print("POSE QUERY TRIGGERS %s checks=%d failures=%d snapshots=%d" % ["PASS" if _failures == 0 else "FAIL", _checks, _failures, _snapshots.size()])
	quit(0 if _failures == 0 else 1)
