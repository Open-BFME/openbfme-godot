class_name ParticleSysBone
extends RefCounted
## Generic ParticleSysBone attachment for drawables.
##
## Authored rows name a bone and an FXParticleSystem. AnimationState-scoped
## rows start/stop with the selector's picked condition set. ModelConditionState
## rows stay up while their flags are a subset of the live model conditions.
## FollowBone parents the emitter to a BoneAttachment3D; NONE anchors the host.
## BurstDelay / InitialDelay are composed from RetailFxTiming, not reimplemented.

const MODULE := "ParticleSysBone"
const HOST_NAME := "ParticleSysBoneHost"
const FxTimingScript := preload("res://src/retail_slice/fx_timing.gd")


static func select(attachments: Array, selected_conditions: Array, active_conditions: Array = []) -> Dictionary:
	var selected := _tokens(selected_conditions)
	var active := _tokens(active_conditions)
	if active.is_empty():
		active = selected.duplicate()
	var applied: Array = []
	for raw_value in attachments:
		if typeof(raw_value) != TYPE_DICTIONARY:
			continue
		var row := _normalize(raw_value as Dictionary)
		if row.is_empty():
			continue
		var conditions: Array = row.get("conditions", []) as Array
		var kind := String(row.get("stateKind", "AnimationState"))
		if kind == "ModelConditionState":
			if _subset(conditions, active):
				applied.append(row)
			continue
		if _same_tokens(conditions, selected):
			applied.append(row)
	return {
		"source": "typed-particle-sys-bone",
		"applied": applied.size(),
		"attachments": applied,
	}


static func apply(visual: Node, attachments: Array, fx_definitions: Dictionary = {}, seed: int = 1) -> Dictionary:
	_clear_host(visual)
	if visual == null or not (visual is Node):
		return {"source": "typed-particle-sys-bone", "applied": 0, "nodes": []}
	var host := Node3D.new()
	host.name = HOST_NAME
	(visual as Node).add_child(host)
	var nodes: Array = []
	var timed := 0
	for raw_value in attachments:
		if typeof(raw_value) != TYPE_DICTIONARY:
			continue
		var row := raw_value as Dictionary
		var bone := String(row.get("bone", row.get("anchorBone", "")))
		var system_id := String(row.get("particleSystem", row.get("particleSystemId", "")))
		if system_id == "":
			continue
		var follow := bool(row.get("followBone", false))
		var emitter := _make_emitter(host, visual as Node, bone, system_id, follow)
		if emitter == null:
			continue
		if _attach_fx_timing(emitter, fx_definitions, system_id, seed + nodes.size()):
			timed += 1
		nodes.append({
			"bone": bone,
			"particleSystem": system_id,
			"followBone": follow,
			"path": str(emitter.get_path()),
		})
	return {
		"source": "typed-particle-sys-bone",
		"applied": nodes.size(),
		"timed": timed,
		"nodes": nodes,
	}


static func _normalize(raw: Dictionary) -> Dictionary:
	if String(raw.get("module", "")) == MODULE:
		if String(raw.get("extraction", "")) != "typed":
			return {}
		if String(raw.get("runtimeStatus", raw.get("runtime_status", ""))) != "executable":
			return {}
		var fields: Dictionary = raw.get("fields", {}) as Dictionary
		return {
			"bone": _field_string(fields.get("bone", fields.get("Bone", {}))),
			"particleSystem": _field_string(fields.get("particleSystem", fields.get("ParticleSystem", {}))),
			"followBone": _field_bool(fields.get("FollowBone", fields.get("followBone", {}))),
			"conditions": _field_tokens(fields.get("conditions", [])),
			"stateKind": String(fields.get("stateKind", "AnimationState")),
		}
	var bone := String(raw.get("bone", raw.get("anchorBone", raw.get("anchor_bone", ""))))
	var system_id := String(raw.get("particleSystem", raw.get("particleSystemId", raw.get("particle_system_id", ""))))
	if bone == "" or system_id == "":
		return {}
	return {
		"bone": bone,
		"particleSystem": system_id,
		"followBone": bool(raw.get("followBone", raw.get("follow_bone", false))),
		"conditions": _field_tokens(raw.get("conditions", raw.get("modelConditions", raw.get("model_conditions", [])))),
		"stateKind": String(raw.get("stateKind", raw.get("state_kind", "AnimationState"))),
	}


static func _make_emitter(host: Node3D, visual: Node, bone: String, system_id: String, follow: bool) -> Node3D:
	var emitter := Node3D.new()
	emitter.name = "PSB_%s" % system_id
	emitter.set_meta("particle_system_id", system_id)
	emitter.set_meta("anchor_bone", bone)
	emitter.set_meta("follow_bone", follow)
	var folded := bone.strip_edges().to_upper()
	if folded == "" or folded == "NONE":
		host.add_child(emitter)
		return emitter
	var skeleton := _first_skeleton(visual)
	if skeleton == null:
		host.add_child(emitter)
		emitter.set_meta("bone_missing", true)
		return emitter
	var bone_index := _find_bone(skeleton, bone)
	if bone_index < 0:
		host.add_child(emitter)
		emitter.set_meta("bone_missing", true)
		return emitter
	if follow:
		var attachment := BoneAttachment3D.new()
		attachment.name = "Follow_%s" % bone
		attachment.bone_idx = bone_index
		skeleton.add_child(attachment)
		attachment.add_child(emitter)
		return emitter
	host.add_child(emitter)
	emitter.global_transform = skeleton.global_transform * skeleton.get_bone_global_pose(bone_index)
	return emitter


static func _attach_fx_timing(emitter: Node, fx_definitions: Dictionary, system_id: String, seed: int) -> bool:
	var definition: Variant = fx_definitions.get(system_id, {})
	if typeof(definition) != TYPE_DICTIONARY or (definition as Dictionary).is_empty():
		return false
	var contract: Dictionary = FxTimingScript.contract_from_definition(definition as Dictionary)
	if contract.is_empty() and (definition as Dictionary).has("initialDelayFrames"):
		contract = (definition as Dictionary).duplicate(true)
	if contract.is_empty():
		return false
	var timing: Node = FxTimingScript.new()
	timing.name = "RetailFxTiming"
	emitter.add_child(timing)
	return bool(timing.call("configure", contract, seed))


static func _clear_host(visual: Node) -> void:
	if visual == null:
		return
	var existing := visual.get_node_or_null(HOST_NAME)
	if existing != null:
		existing.free()
	var skeleton := _first_skeleton(visual)
	if skeleton == null:
		return
	for child in skeleton.get_children():
		if child is BoneAttachment3D and String(child.name).begins_with("Follow_"):
			child.free()


static func _first_skeleton(node: Node) -> Skeleton3D:
	if node is Skeleton3D:
		return node as Skeleton3D
	for child in node.get_children():
		var found := _first_skeleton(child)
		if found != null:
			return found
	return null


static func _find_bone(skeleton: Skeleton3D, bone: String) -> int:
	var folded := bone.strip_edges().to_upper()
	for index in range(skeleton.get_bone_count()):
		if String(skeleton.get_bone_name(index)).to_upper() == folded:
			return index
	return -1


static func _tokens(value: Variant) -> Array:
	var out: Array = []
	var raw: Variant = value
	if typeof(value) == TYPE_DICTIONARY:
		raw = (value as Dictionary).get("value", [])
	if typeof(raw) != TYPE_ARRAY:
		return out
	for item in raw as Array:
		var token := String(item).to_upper()
		if token != "" and not out.has(token):
			out.append(token)
	return out


static func _field_tokens(value: Variant) -> Array:
	return _tokens(value)


static func _field_string(value: Variant) -> String:
	if typeof(value) == TYPE_DICTIONARY:
		return String((value as Dictionary).get("value", ""))
	return String(value)


static func _field_bool(value: Variant) -> bool:
	if typeof(value) == TYPE_DICTIONARY:
		return bool((value as Dictionary).get("value", false))
	return bool(value)


static func _subset(required: Array, active: Array) -> bool:
	for value in required:
		if not active.has(String(value).to_upper()):
			return false
	return true


static func _same_tokens(left: Array, right: Array) -> bool:
	if left.size() != right.size():
		return false
	for value in left:
		if not right.has(String(value).to_upper()):
			return false
	return true
