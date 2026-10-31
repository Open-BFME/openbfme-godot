## Per-instance shadow batches share geometry and poses, without leaking eligibility to another instance.
## Requires ROTWK_INSTALL and BFME2_INSTALL and a GPU (the dummy backend cannot read MultiMesh buffers).
extends SceneTree

var failures := 0

func _initialize() -> void:
	call_deferred("_run")

func _check(ok: bool, message: String) -> void:
	print("  ", "ok " if ok else "FAIL ", message)
	if not ok:
		failures += 1

func _run() -> void:
	if DisplayServer.get_name() == "headless":
		print("SKIP: MultiMesh buffer readback requires a GPU")
		quit(77)
		return
	if OS.get_environment("ROTWK_INSTALL").is_empty() or OS.get_environment("BFME2_INSTALL").is_empty():
		print("SKIP: retail install paths required")
		quit(77)
		return
	var fs = ClassDB.instantiate("RetailFileSystem")
	if not fs.mount_retail().ok:
		quit(1)
		return
	var ini: String = fs.read_file("data/ini/object/goodfaction/units/men/gondorfighter.ini").get_string_from_utf8()
	var model_name := RetailIni.find_default_model(ini, "GondorFighter")
	var inst = ClassDB.instantiate("W3DInstancer")
	root.add_child(inst)
	_check(inst.setup(fs).ok, "instancer setup")
	inst.set_auto_update(false)
	inst.set_playing(false)
	var model: int = inst.add_model(model_name)
	_check(model >= 0, "soldier model builds")
	if model < 0:
		inst.free()
		quit(1)
		return
	var a: int = inst.add_instance(model, Transform3D.IDENTITY, "GUManMocap_IDLB", 0.0, 1.0)
	var b: int = inst.add_instance(model, Transform3D(Basis.IDENTITY, Vector3(100, 0, 0)), "GUManMocap_IDLB", 0.0, 1.0)
	inst.set_instance_cast_shadow(a, true)
	inst.update_now()
	var shadow_nodes: Array[MultiMeshInstance3D] = []
	for child in inst.get_children():
		if child is MultiMeshInstance3D and child.cast_shadow == GeometryInstance3D.SHADOW_CASTING_SETTING_SHADOWS_ONLY:
			shadow_nodes.append(child)
	_check(not shadow_nodes.is_empty(), "eligible soldier creates shadow batches")
	for shadow in shadow_nodes:
		_check(shadow.multimesh.visible_instance_count == 1, "only the eligible instance casts")
		_check(shadow.multimesh.get_instance_transform(0) == Transform3D.IDENTITY, "shadow uses caster transform")
	inst.set_instance_cast_shadow(a, false)
	inst.set_instance_cast_shadow(b, true)
	inst.update_now()
	for shadow in shadow_nodes:
		_check(shadow.multimesh.visible_instance_count == 1, "toggling eligibility does not duplicate batches")
		_check(shadow.multimesh.get_instance_transform(0).origin == Vector3(100, 0, 0), "the other instance now casts")
	var camera := Camera3D.new()
	root.add_child(camera)
	camera.position = Vector3(0, 0, 10000)
	camera.far = 10.0
	camera.make_current()
	inst.set_pose_culling(true)
	var before: PackedFloat32Array = inst.get_instance_palette(b)
	ClassDB.class_call_static("W3DInstancer", "set_shadow_maps_enabled", false)
	inst.set_global_time(0.5)
	inst.update_now()
	_check(inst.get_instance_palette(b) == before, "without shadow maps an off-camera caster is pose-culled")
	ClassDB.class_call_static("W3DInstancer", "set_shadow_maps_enabled", true)
	inst.set_global_time(1.0)
	inst.update_now()
	_check(inst.get_instance_palette(b) != before, "off-camera caster continues animating")
	ClassDB.class_call_static("W3DInstancer", "set_shadow_maps_enabled", false)
	var names := PackedStringArray()
	for sub in inst.get_model_report(model).sub_objects:
		names.append(str(sub.name))
	inst.set_instance_hidden_subobjects(b, names)
	inst.update_now()
	for shadow in shadow_nodes:
		_check(shadow.multimesh.visible_instance_count == 0, "hidden subobjects cast no shadow")
	inst.set_instance_hidden_subobjects(b, PackedStringArray())
	inst.set_instance_opacity(b, 0.0)
	inst.update_now()
	for shadow in shadow_nodes:
		_check(shadow.multimesh.visible_instance_count == 0, "invisible instance casts no shadow")
	inst.set_instance_opacity(b, 1.0)
	inst.remove_instance(b)
	inst.update_now()
	for shadow in shadow_nodes:
		_check(shadow.multimesh.visible_instance_count == 0, "removed caster leaves no shadow")
	inst.free()
	camera.free()
	print("OBJECT SHADOW INSTANCES ", "PASS" if failures == 0 else "FAIL")
	quit(0 if failures == 0 else 1)
