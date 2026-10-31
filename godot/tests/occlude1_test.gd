## Lane OCCLUDE-1: W3DInstancer's behind-building passes (GameClient/RTS3DSceneOcclusion.h) on a retail model. An occluder instance gets the stencil mark
## pass chained after the model's opaque surfaces, an occludee the colour pass; INSTANCE_CUSTOM.w carries the slot (bits 15 .. 19) and the occluder bit (20).
## SKIP (exit 77) without ROTWK_INSTALL / BFME2_INSTALL.
extends SceneTree

var _failures := 0


func _initialize() -> void:
	_run.call_deferred()


func _check(ok: bool, what: String) -> void:
	if not ok:
		_failures += 1
		print("FAIL ", what)


func _custom_w(inst: Node3D) -> Array:
	# INSTANCE_CUSTOM.w of every written instance of every draw (the MultiMesh buffers: 12 transform floats, then the 4 custom floats)
	var out: Array = []
	for child in inst.get_children():
		if not (child is MultiMeshInstance3D) or child.multimesh == null:
			continue
		var buf: PackedFloat32Array = child.multimesh.buffer
		for k in child.multimesh.visible_instance_count:
			out.append(int(buf[k * 16 + 15]))
	return out


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
	var inst: Node3D = ClassDB.instantiate("W3DInstancer")
	root.add_child(inst)
	inst.set_auto_update(false)
	_check(inst.setup(fs).ok, "setup")
	var soldier: int = inst.add_model("GUMAArms_SKN")
	_check(soldier >= 0, "the Gondor soldier's model loads: %s" % [inst.get_errors()])  # tolerance: a model index, any found one
	if soldier < 0:
		quit(1)
		return
	var a: int = inst.add_instance(soldier, Transform3D.IDENTITY, "", 0.0, 1.0)
	var b: int = inst.add_instance(soldier, Transform3D(Basis.IDENTITY, Vector3(10, 0, 0)), "", 0.0, 1.0)
	inst.update_now()
	var plain: Array = _custom_w(inst)
	_check(plain.size() > 0, "drawn: %s" % [plain])  # tolerance: the model's draw count is the data's
	for w in plain:
		_check(w < 32768, "no occlusion bits before any role: %d" % w)  # tolerance: the bits above 15 must be clear, the bone / sway bits are the model's
	var none: Dictionary = inst.get_occlusion_pass_counts()
	_check(none.marks == 0 and none.colors == 0, "no passes before a role: %s" % [none])
	# an occludee of slot 3: the colour pass on every opaque surface, bits 15 .. 19 = 3
	inst.set_instance_occlusion(a, false, 3)
	inst.update_now()
	var after_occludee: Dictionary = inst.get_occlusion_pass_counts()
	_check(after_occludee.marks == 0, "an occludee asks for no mark pass: %s" % [after_occludee])
	_check(after_occludee.colors > 0, "an occludee chains the colour pass: %s" % [after_occludee])  # tolerance: the number of opaque surfaces is the model's
	var ws: Array = _custom_w(inst)
	var tagged := 0
	for w in ws:
		if (w >> 15) == 3:
			tagged += 1
		else:
			_check((w >> 15) == 0, "the other instance has no bits: %d" % w)
	_check(tagged * 2 == ws.size(), "half the written instances carry slot 3: %d of %d" % [tagged, ws.size()])
	# the same model as an occluder too: the mark pass joins the chain (surface -> mark -> colour)
	inst.set_instance_occlusion(b, true, 0)
	inst.update_now()
	var both: Dictionary = inst.get_occlusion_pass_counts()
	_check(both.marks == both.colors, "one mark per coloured surface: %s" % [both])
	var occluder_bits := 0
	for w in _custom_w(inst):
		if (w & 1048576) != 0:
			occluder_bits += 1
			_check(((w >> 15) & 31) == 0, "the occluder has no slot")
	_check(occluder_bits * 2 == ws.size(), "half carry the occluder bit")
	# back to plain: the bits clear, the passes stay (they collapse the instances that do not take part)
	inst.set_instance_occlusion(a, false, 0)
	inst.set_instance_occlusion(b, false, 0)
	inst.update_now()
	for w in _custom_w(inst):
		_check(w < 32768, "cleared: %d" % w)  # tolerance: the bits above 15 must be clear
	_check(inst.get_occlusion_pass_counts().marks == both.marks, "the passes stay chained")
	# an out-of-range slot is refused with a warning, not packed into the occluder bit
	inst.set_instance_occlusion(a, false, 40)
	inst.update_now()
	for w in _custom_w(inst):
		_check(w < 32768, "slot 40 refused: %d" % w)  # tolerance: the bits above 15 must be clear
	print("OCCLUDE1 %s (%d failures)" % ["PASS" if _failures == 0 else "FAIL", _failures])
	quit(0 if _failures == 0 else 1)
