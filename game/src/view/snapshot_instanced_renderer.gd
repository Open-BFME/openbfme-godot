class_name SnapshotInstancedRenderer
extends Node3D
## Snapshot-v1 presentation consumer using one MultiMeshInstance3D per
## (template, owner). No object slot ever becomes a Node.

const MEMBER_OBJECT_IDS := [
	"bfme2.object.gondor-fighter",
	"bfme2.object.gondor-archer",
	"bfme2.object.gondor-ranger",
]
const REQUIRED_OBJECT_ARRAYS := [
	"id", "template", "owner", "x", "y", "z", "yaw", "health",
	"max_health", "state", "anim", "anim_frame", "flags",
]
const TemplateMeshResolverScript := preload("res://src/view/snapshot_template_mesh_resolver.gd")

class Group:
	extends RefCounted
	var node: MultiMeshInstance3D
	var multimesh: MultiMesh
	var capacity := 0
	var buffer := PackedFloat32Array()

var _groups: Dictionary = {}
var _previous: Dictionary = {}
var _current: Dictionary = {}
var _previous_slots: Dictionary = {}
var _mesh: Mesh
var _mesh_source := "capsule"
var _mesh_path := ""
var _tint_material: ShaderMaterial
var _rendered_instances := 0
var _template_mesh_resolver = TemplateMeshResolverScript.new()
var _resolved_template_indices: Dictionary = {}
var _fallback_template_indices: Dictionary = {}
var _printed_model_summary := false


func _ready() -> void:
	_ensure_mesh()


func submit_snapshot(document: Dictionary) -> bool:
	if not _valid_snapshot(document):
		push_warning("[SnapshotInstancedRenderer] refused malformed snapshot-v1 document")
		return false
	if _current.is_empty():
		_previous = document.duplicate(true)
	else:
		_previous = _current
	_current = document.duplicate(true)
	_previous_slots = _slot_index(_previous)
	return true


func configure_templates(template_rows: Array[Dictionary]) -> void:
	_template_mesh_resolver.configure(template_rows)
	_resolved_template_indices.clear()
	_fallback_template_indices.clear()
	_printed_model_summary = false


func render_interpolated(alpha: float) -> bool:
	if _current.is_empty():
		return false
	_ensure_mesh()
	var clamped_alpha := clampf(alpha, 0.0, 1.0)
	var objects := _current["objects"] as Dictionary
	receive_animation_frames(objects["anim"] as Array, objects["anim_frame"] as Array)
	var grouped: Dictionary = {}
	for slot in int(_current["object_count"]):
		var key := "%d|%d" % [int((objects["template"] as Array)[slot]), int((objects["owner"] as Array)[slot])]
		if not grouped.has(key):
			grouped[key] = []
		(grouped[key] as Array).append(slot)

	_rendered_instances = 0
	for key_value in _groups.keys():
		var key := String(key_value)
		if not grouped.has(key):
			var unused := _groups[key] as Group
			unused.multimesh.visible_instance_count = 0
			unused.node.visible = false
	for key_value in grouped.keys():
		var key := String(key_value)
		var slots := grouped[key] as Array
		var group := _ensure_group(key, slots.size())
		group.multimesh.visible_instance_count = slots.size()
		group.node.visible = not slots.is_empty()
		for instance_index in slots.size():
			var slot := int(slots[instance_index])
			_write_instance(
				group.buffer,
				instance_index,
				_interpolated_transform(objects, slot, clamped_alpha),
				_owner_tint(int((objects["owner"] as Array)[slot]))
			)
		group.multimesh.buffer = group.buffer
		_rendered_instances += slots.size()
	if not _printed_model_summary:
		_printed_model_summary = true
		print(model_resolution_summary())
	return _rendered_instances == int(_current["object_count"])


## Bone-atlas animation is the next render unit. This hook deliberately accepts
## the packed snapshot arrays now so that unit can upload them without changing
## the renderer/snapshot seam established here.
func receive_animation_frames(anim: Array, anim_frame: Array) -> void:
	if anim.size() != anim_frame.size():
		push_warning("[SnapshotInstancedRenderer] anim arrays are not parallel")


func mesh_source() -> String:
	return _mesh_source


func mesh_path() -> String:
	return _mesh_path


func rendered_instance_count() -> int:
	return _rendered_instances


func group_count() -> int:
	return _groups.size()


func model_resolution_summary() -> String:
	return "SIM_HOST_MODEL_RESOLUTION resolved=%d fallback=%d templates=%d" % [
		_resolved_template_indices.size(),
		_fallback_template_indices.size(),
		_resolved_template_indices.size() + _fallback_template_indices.size(),
	]


func _ensure_group(key: String, wanted: int) -> Group:
	var group: Group = _groups.get(key)
	if group == null:
		group = Group.new()
		group.multimesh = MultiMesh.new()
		group.multimesh.transform_format = MultiMesh.TRANSFORM_3D
		group.multimesh.use_custom_data = true
		var template_index := int(key.get_slice("|", 0))
		var resolved: Dictionary = _template_mesh_resolver.resolve(template_index)
		var template_mesh: Mesh = resolved.get("mesh") as Mesh
		if template_mesh != null:
			group.multimesh.mesh = template_mesh
			_resolved_template_indices[template_index] = String(resolved.get("path", ""))
		else:
			group.multimesh.mesh = _mesh
			_fallback_template_indices[template_index] = _mesh_source
		group.node = MultiMeshInstance3D.new()
		group.node.name = "SnapshotBatch_%s" % key.replace("|", "_")
		group.node.multimesh = group.multimesh
		group.node.material_override = _tint_material
		add_child(group.node)
		_groups[key] = group
	if wanted > group.capacity:
		group.capacity = wanted
		group.multimesh.instance_count = wanted
		# 3D transform = 12 floats, custom data = 4 floats.
		group.buffer.resize(wanted * 16)
	return group


func _write_instance(
	buffer: PackedFloat32Array, index: int, transform: Transform3D, tint: Color
) -> void:
	var base := index * 16
	buffer[base] = transform.basis.x.x
	buffer[base + 1] = transform.basis.y.x
	buffer[base + 2] = transform.basis.z.x
	buffer[base + 3] = transform.origin.x
	buffer[base + 4] = transform.basis.x.y
	buffer[base + 5] = transform.basis.y.y
	buffer[base + 6] = transform.basis.z.y
	buffer[base + 7] = transform.origin.y
	buffer[base + 8] = transform.basis.x.z
	buffer[base + 9] = transform.basis.y.z
	buffer[base + 10] = transform.basis.z.z
	buffer[base + 11] = transform.origin.z
	buffer[base + 12] = tint.r
	buffer[base + 13] = tint.g
	buffer[base + 14] = tint.b
	buffer[base + 15] = tint.a


func _interpolated_transform(objects: Dictionary, slot: int, alpha: float) -> Transform3D:
	var id := int((objects["id"] as Array)[slot])
	var current_position := Vector3(
		float((objects["x"] as Array)[slot]),
		float((objects["y"] as Array)[slot]),
		float((objects["z"] as Array)[slot])
	)
	var current_yaw := float((objects["yaw"] as Array)[slot])
	var previous_position := current_position
	var previous_yaw := current_yaw
	if _previous_slots.has(id):
		var previous_objects := _previous["objects"] as Dictionary
		var previous_slot := int(_previous_slots[id])
		previous_position = Vector3(
			float((previous_objects["x"] as Array)[previous_slot]),
			float((previous_objects["y"] as Array)[previous_slot]),
			float((previous_objects["z"] as Array)[previous_slot])
		)
		previous_yaw = float((previous_objects["yaw"] as Array)[previous_slot])
	var position := previous_position.lerp(current_position, alpha)
	var yaw := lerp_angle(previous_yaw, current_yaw, alpha)
	return Transform3D(Basis(Vector3.UP, yaw), position)


func _slot_index(document: Dictionary) -> Dictionary:
	var slots: Dictionary = {}
	if document.is_empty():
		return slots
	var ids := (document["objects"] as Dictionary)["id"] as Array
	for slot in ids.size():
		slots[int(ids[slot])] = slot
	return slots


func _valid_snapshot(document: Dictionary) -> bool:
	if String(document.get("schema", "")) != "openbfme.snapshot.v1":
		return false
	var objects_value: Variant = document.get("objects")
	if not (objects_value is Dictionary):
		return false
	var objects := objects_value as Dictionary
	var count := int(document.get("object_count", -1))
	if count < 0:
		return false
	for key in REQUIRED_OBJECT_ARRAYS:
		var values: Variant = objects.get(key)
		if not (values is Array) or (values as Array).size() != count:
			return false
	return true


func _ensure_mesh() -> void:
	if _mesh != null:
		return
	var resolved := _try_pack_mesh()
	_mesh = resolved[0] as Mesh
	_mesh_path = String(resolved[1])
	if _mesh != null:
		_mesh_source = "glb"
	else:
		var capsule := CapsuleMesh.new()
		capsule.radius = 4.0
		capsule.height = 18.0
		capsule.radial_segments = 6
		capsule.rings = 3
		_mesh = capsule
		_mesh_source = "capsule"
		_mesh_path = ""
	_tint_material = ShaderMaterial.new()
	var shader := Shader.new()
	shader.code = "shader_type spatial; render_mode unshaded; varying vec4 instance_tint; void vertex() { instance_tint = INSTANCE_CUSTOM; } void fragment() { ALBEDO = instance_tint.rgb; ALPHA = instance_tint.a; }"
	_tint_material.shader = shader
	print("SNAPSHOT_RENDERER_MESH source=%s path=%s" % [_mesh_source, _mesh_path if _mesh_path != "" else "<built-in>"])


func _try_pack_mesh() -> Array:
	var content_db := _autoload("ContentDB")
	var mod_loader := _autoload("ModLoader")
	if content_db == null or mod_loader == null:
		return [null, ""]
	for object_id in MEMBER_OBJECT_IDS:
		var definition: Dictionary = content_db.get_bundle_object(object_id)
		if definition.is_empty():
			continue
		var path := String(content_db.resolve_mesh_path(definition))
		if path.get_extension().to_lower() != "glb":
			continue
		var pack_root := String(definition.get("_pack_root", ""))
		if pack_root == "" or not bool(mod_loader.path_is_within(pack_root, path)):
			continue
		var document := GLTFDocument.new()
		var state := GLTFState.new()
		if document.append_from_file(path, state) != OK:
			continue
		var scene := document.generate_scene(state) as Node3D
		if scene == null:
			continue
		var found := _first_mesh_surface(scene, Transform3D.IDENTITY)
		scene.free()
		if found != null:
			return [found, path]
	return [null, ""]


func _autoload(node_name: String) -> Node:
	var tree := Engine.get_main_loop() as SceneTree
	if tree == null:
		return null
	return tree.root.get_node_or_null(node_name)


func _first_mesh_surface(node: Node, parent_transform: Transform3D) -> ArrayMesh:
	var transform := parent_transform
	if node is Node3D:
		transform = parent_transform * (node as Node3D).transform
	if node is MeshInstance3D:
		var source := (node as MeshInstance3D).mesh
		if source != null and source.get_surface_count() > 0 and source.surface_get_primitive_type(0) == Mesh.PRIMITIVE_TRIANGLES:
			var arrays := source.surface_get_arrays(0)
			arrays[Mesh.ARRAY_BONES] = null
			arrays[Mesh.ARRAY_WEIGHTS] = null
			_transform_surface_arrays(arrays, transform)
			var mesh := ArrayMesh.new()
			mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
			return mesh
	for child in node.get_children():
		var found := _first_mesh_surface(child, transform)
		if found != null:
			return found
	return null


func _transform_surface_arrays(arrays: Array, transform: Transform3D) -> void:
	var vertices: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
	for index in vertices.size():
		vertices[index] = transform * vertices[index]
	arrays[Mesh.ARRAY_VERTEX] = vertices
	var normals_value: Variant = arrays[Mesh.ARRAY_NORMAL]
	if normals_value is PackedVector3Array:
		var normals := normals_value as PackedVector3Array
		var normal_basis := transform.basis.inverse().transposed()
		for index in normals.size():
			normals[index] = (normal_basis * normals[index]).normalized()
		arrays[Mesh.ARRAY_NORMAL] = normals


func _owner_tint(owner: int) -> Color:
	match owner:
		0:
			return Color(0.18, 0.42, 1.0, 1.0)
		1:
			return Color(0.92, 0.16, 0.12, 1.0)
		2:
			return Color(0.20, 0.78, 0.30, 1.0)
		3:
			return Color(0.92, 0.72, 0.12, 1.0)
	return Color(0.65, 0.65, 0.65, 1.0)
