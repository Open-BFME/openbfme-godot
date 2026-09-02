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
const BoneAtlasScript := preload("res://src/view/snapshot_bone_atlas.gd")
const LARGE_ARMY_THRESHOLD := 8000
const LARGE_ARMY_UPLOAD_STRIDE := 6

class Group:
	extends RefCounted
	var node: MultiMeshInstance3D
	var multimesh: MultiMesh
	var capacity := 0
	var buffer := PackedFloat32Array()
	var animated := false
	var material: ShaderMaterial

var _groups: Dictionary = {}
var _previous: Dictionary = {}
var _current: Dictionary = {}
var _previous_slots: Dictionary = {}
var _mesh: Mesh
var _mesh_source := "capsule"
var _mesh_path := ""
var _tint_material: ShaderMaterial
var _rendered_instances := 0
var _snapshot_serial := 0
var _uploaded_serial := -1
var _layout_count := -1
var _grouped_slots: Dictionary = {}
var _template_mesh_resolver = TemplateMeshResolverScript.new()
var _atlas_cache: Dictionary = {}
var _resolved_template_indices: Dictionary = {}
var _fallback_template_indices: Dictionary = {}
var _animated_template_indices: Dictionary = {}
var _atlas_refusals: Dictionary = {}
var _printed_model_summary := false
var _profile_upload_usec := 0
var _profile_upload_max_usec := 0
var _profile_uploads := 0


func _ready() -> void:
	_ensure_mesh()


func submit_snapshot(document: Dictionary) -> bool:
	if not _valid_snapshot(document):
		push_warning("[SnapshotInstancedRenderer] refused malformed snapshot-v1 document")
		return false
	if _current.is_empty():
		_previous = document
	else:
		_previous = _current
	_current = document
	_previous_slots = _slot_index(_previous)
	_snapshot_serial += 1
	return true


func configure_templates(template_rows: Array[Dictionary]) -> void:
	_template_mesh_resolver.configure(template_rows)
	_resolved_template_indices.clear()
	_fallback_template_indices.clear()
	_animated_template_indices.clear()
	_atlas_refusals.clear()
	_atlas_cache.clear()
	_printed_model_summary = false
	_uploaded_serial = -1
	_layout_count = -1
	_grouped_slots.clear()


func render_interpolated(alpha: float) -> bool:
	if _current.is_empty():
		return false
	_ensure_mesh()
	var clamped_alpha := clampf(alpha, 0.0, 1.0)
	_set_interpolation_alpha(clamped_alpha)
	if _uploaded_serial == _snapshot_serial:
		return _rendered_instances == int(_current["object_count"])
	var objects := _current["objects"] as Dictionary
	var object_count := int(_current["object_count"])
	if (
		object_count >= LARGE_ARMY_THRESHOLD
		and _uploaded_serial >= 0
		and _snapshot_serial - _uploaded_serial < LARGE_ARMY_UPLOAD_STRIDE
	):
		return _rendered_instances == object_count
	var upload_started := Time.get_ticks_usec()
	receive_animation_frames(objects["anim"] as Array, objects["anim_frame"] as Array)
	if _layout_count != object_count:
		_grouped_slots.clear()
		for slot in object_count:
			var key := str(int((objects["template"] as Array)[slot]))
			if not _grouped_slots.has(key):
				_grouped_slots[key] = []
			(_grouped_slots[key] as Array).append(slot)
		_layout_count = object_count
	var grouped := _grouped_slots

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
				objects,
				slot,
				_owner_tint(int((objects["owner"] as Array)[slot])),
				int((objects["anim"] as Array)[slot]),
				float((objects["anim_frame"] as Array)[slot]),
				group.animated
			)
		group.multimesh.buffer = group.buffer
		_rendered_instances += slots.size()
	_uploaded_serial = _snapshot_serial
	var upload_elapsed := Time.get_ticks_usec() - upload_started
	_profile_upload_usec += upload_elapsed
	_profile_upload_max_usec = maxi(_profile_upload_max_usec, upload_elapsed)
	_profile_uploads += 1
	if not _printed_model_summary:
		_printed_model_summary = true
		print(model_resolution_summary())
	return _rendered_instances == int(_current["object_count"])


func reset_profile() -> void:
	_profile_upload_usec = 0
	_profile_upload_max_usec = 0
	_profile_uploads = 0


func take_profile() -> Dictionary:
	var result := {
		"upload_usec": _profile_upload_usec,
		"upload_max_usec": _profile_upload_max_usec,
		"uploads": _profile_uploads,
	}
	reset_profile()
	return result


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


func animation_mode() -> String:
	return "atlas" if not _animated_template_indices.is_empty() else "static"


func animated_group_count() -> int:
	var count := 0
	for group_value in _groups.values():
		if (group_value as Group).animated:
			count += 1
	return count


func atlas_refusals() -> Dictionary:
	return _atlas_refusals.duplicate(true)


func model_resolution_summary() -> String:
	return "SIM_HOST_MODEL_RESOLUTION resolved=%d animated=%d fallback=%d atlas_refused=%d templates=%d" % [
		_resolved_template_indices.size(),
		_animated_template_indices.size(),
		_fallback_template_indices.size(),
		_atlas_refusals.size(),
		_resolved_template_indices.size() + _fallback_template_indices.size(),
	]


func _ensure_group(key: String, wanted: int) -> Group:
	var group: Group = _groups.get(key)
	if group == null:
		group = Group.new()
		group.multimesh = MultiMesh.new()
		group.multimesh.transform_format = MultiMesh.TRANSFORM_3D
		group.multimesh.use_colors = true
		group.multimesh.use_custom_data = true
		var template_index := int(key)
		var resolved: Dictionary = _template_mesh_resolver.resolve(template_index)
		var template_mesh: Mesh = resolved.get("mesh") as Mesh
		var path := String(resolved.get("path", ""))
		var atlas := _atlas_for(template_index, path)
		if bool(atlas.get("ok", false)):
			group.multimesh.mesh = atlas.get("mesh") as Mesh
			group.animated = true
			group.material = _atlas_material(atlas)
			_animated_template_indices[template_index] = path
			_resolved_template_indices[template_index] = path
		elif template_mesh != null:
			group.multimesh.mesh = template_mesh
			_resolved_template_indices[template_index] = path
		else:
			group.multimesh.mesh = _mesh
			_fallback_template_indices[template_index] = _mesh_source
		group.node = MultiMeshInstance3D.new()
		group.node.name = "SnapshotBatch_%s" % key.replace("|", "_")
		group.node.multimesh = group.multimesh
		group.node.material_override = group.material if group.material != null else _tint_material
		add_child(group.node)
		_groups[key] = group
	if wanted > group.capacity:
		group.capacity = wanted
		group.multimesh.instance_count = wanted
		# 3D transform = 12, previous xyz+yaw color = 4, custom = 4.
		group.buffer.resize(wanted * 20)
	return group


func _write_instance(
	buffer: PackedFloat32Array,
	index: int,
	objects: Dictionary,
	slot: int,
	tint: Color,
	anim: int,
	anim_frame: float,
	animated: bool
) -> void:
	var base := index * 20
	var yaw := float((objects["yaw"] as Array)[slot])
	var cosine := cos(yaw)
	var sine := sin(yaw)
	buffer[base] = cosine
	buffer[base + 1] = 0.0
	buffer[base + 2] = sine
	buffer[base + 3] = float((objects["x"] as Array)[slot])
	buffer[base + 4] = 0.0
	buffer[base + 5] = 1.0
	buffer[base + 6] = 0.0
	buffer[base + 7] = float((objects["y"] as Array)[slot])
	buffer[base + 8] = -sine
	buffer[base + 9] = 0.0
	buffer[base + 10] = cosine
	buffer[base + 11] = float((objects["z"] as Array)[slot])
	var previous_objects := objects
	var previous_slot := slot
	var id := int((objects["id"] as Array)[slot])
	if _previous_slots.has(id):
		previous_objects = _previous["objects"] as Dictionary
		previous_slot = int(_previous_slots[id])
	buffer[base + 12] = float((previous_objects["x"] as Array)[previous_slot])
	buffer[base + 13] = float((previous_objects["y"] as Array)[previous_slot])
	buffer[base + 14] = float((previous_objects["z"] as Array)[previous_slot])
	buffer[base + 15] = float((previous_objects["yaw"] as Array)[previous_slot])
	if animated:
		buffer[base + 16] = float(clampi(anim, 0, 4))
		buffer[base + 17] = maxf(0.0, anim_frame)
		buffer[base + 18] = _pack_tint(tint)
		buffer[base + 19] = 1.0
	else:
		buffer[base + 16] = tint.r
		buffer[base + 17] = tint.g
		buffer[base + 18] = tint.b
		buffer[base + 19] = tint.a


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
	shader.code = """
shader_type spatial;
render_mode unshaded, skip_vertex_transform;
uniform float interpolation_alpha = 1.0;
varying vec4 instance_tint;
void vertex() {
	float yaw = COLOR.a;
	float c = cos(yaw);
	float s = sin(yaw);
	mat3 previous_basis = mat3(vec3(c, 0.0, -s), vec3(0.0, 1.0, 0.0), vec3(s, 0.0, c));
	vec3 previous_world = previous_basis * VERTEX + COLOR.rgb;
	vec3 previous_view = (VIEW_MATRIX * vec4(previous_world, 1.0)).xyz;
	vec3 current_view = (MODELVIEW_MATRIX * vec4(VERTEX, 1.0)).xyz;
	vec3 previous_normal = (VIEW_MATRIX * vec4(previous_basis * NORMAL, 0.0)).xyz;
	vec3 current_normal = (MODELVIEW_MATRIX * vec4(NORMAL, 0.0)).xyz;
	VERTEX = mix(previous_view, current_view, interpolation_alpha);
	NORMAL = normalize(mix(previous_normal, current_normal, interpolation_alpha));
	instance_tint = INSTANCE_CUSTOM;
}
void fragment() {
	ALBEDO = instance_tint.rgb;
	ALPHA = instance_tint.a;
}
"""
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


func _atlas_for(template_index: int, path: String) -> Dictionary:
	if _atlas_cache.has(template_index):
		return _atlas_cache[template_index] as Dictionary
	if path.is_empty():
		var missing := {"ok": false, "reason": "no resolved GLB"}
		_atlas_cache[template_index] = missing
		return missing
	var template_name := _template_mesh_resolver.template_name(template_index)
	var atlas: Dictionary = BoneAtlasScript.bake(self, path, template_name)
	_atlas_cache[template_index] = atlas
	if not bool(atlas.get("ok", false)):
		_atlas_refusals[template_index] = String(atlas.get("reason", "unknown atlas refusal"))
		print(
			"SNAPSHOT_BONE_ATLAS_STATIC template=%s reason=%s"
			% [template_name, String(_atlas_refusals[template_index])]
		)
	return atlas


func _atlas_material(atlas: Dictionary) -> ShaderMaterial:
	var material := ShaderMaterial.new()
	var shader := Shader.new()
	shader.code = """
shader_type spatial;
render_mode unshaded, skip_vertex_transform;

uniform sampler2D bone_atlas : filter_nearest, repeat_disable;
uniform sampler2D albedo_texture : source_color, filter_linear_mipmap;
uniform bool has_albedo = false;
uniform float interpolation_alpha = 1.0;
uniform vec4 clip_offsets_0_3;
uniform float clip_offset_4;
uniform vec4 clip_counts_0_3;
uniform float clip_count_4;

varying vec3 instance_tint;

float slot_value(vec4 first_four, float fifth, int slot) {
	if (slot == 0) return first_four.x;
	if (slot == 1) return first_four.y;
	if (slot == 2) return first_four.z;
	if (slot == 3) return first_four.w;
	return fifth;
}

mat4 atlas_matrix(int bone, int row) {
	int x = bone * 3;
	vec4 r0 = texelFetch(bone_atlas, ivec2(x, row), 0);
	vec4 r1 = texelFetch(bone_atlas, ivec2(x + 1, row), 0);
	vec4 r2 = texelFetch(bone_atlas, ivec2(x + 2, row), 0);
	return mat4(
		vec4(r0.x, r1.x, r2.x, 0.0),
		vec4(r0.y, r1.y, r2.y, 0.0),
		vec4(r0.z, r1.z, r2.z, 0.0),
		vec4(r0.w, r1.w, r2.w, 1.0)
	);
}

void vertex() {
	int slot = clamp(int(floor(INSTANCE_CUSTOM.r + 0.5)), 0, 4);
	int count = max(1, int(slot_value(clip_counts_0_3, clip_count_4, slot)));
	int local_frame = max(0, int(floor(INSTANCE_CUSTOM.g)));
	if (slot <= 1) local_frame = local_frame % count;
	else local_frame = min(local_frame, count - 1);
	int row = int(slot_value(clip_offsets_0_3, clip_offset_4, slot)) + local_frame;

	mat4 skin =
		atlas_matrix(int(BONE_INDICES.x), row) * BONE_WEIGHTS.x
		+ atlas_matrix(int(BONE_INDICES.y), row) * BONE_WEIGHTS.y
		+ atlas_matrix(int(BONE_INDICES.z), row) * BONE_WEIGHTS.z
		+ atlas_matrix(int(BONE_INDICES.w), row) * BONE_WEIGHTS.w;
	vec4 local_vertex = skin * vec4(VERTEX, 1.0);
	vec3 local_normal = mat3(skin) * NORMAL;
	float previous_yaw = COLOR.a;
	float c = cos(previous_yaw);
	float s = sin(previous_yaw);
	mat3 previous_basis = mat3(vec3(c, 0.0, -s), vec3(0.0, 1.0, 0.0), vec3(s, 0.0, c));
	vec3 previous_world = previous_basis * local_vertex.xyz + COLOR.rgb;
	vec3 previous_view = (VIEW_MATRIX * vec4(previous_world, 1.0)).xyz;
	vec3 current_view = (MODELVIEW_MATRIX * local_vertex).xyz;
	vec3 previous_normal = (VIEW_MATRIX * vec4(previous_basis * local_normal, 0.0)).xyz;
	vec3 current_normal = (MODELVIEW_MATRIX * vec4(local_normal, 0.0)).xyz;
	VERTEX = mix(previous_view, current_view, interpolation_alpha);
	NORMAL = normalize(mix(previous_normal, current_normal, interpolation_alpha));

	float packed = floor(INSTANCE_CUSTOM.b + 0.5);
	float red = floor(packed / 65536.0);
	packed -= red * 65536.0;
	float green = floor(packed / 256.0);
	float blue = packed - green * 256.0;
	instance_tint = vec3(red, green, blue) / 255.0;
}

void fragment() {
	vec4 authored = has_albedo ? texture(albedo_texture, UV) : vec4(1.0);
	ALBEDO = authored.rgb * instance_tint;
	ALPHA = authored.a;
}
"""
	material.shader = shader
	material.set_shader_parameter("bone_atlas", atlas.get("texture"))
	var base_texture := atlas.get("base_texture") as Texture2D
	material.set_shader_parameter("has_albedo", base_texture != null)
	if base_texture != null:
		material.set_shader_parameter("albedo_texture", base_texture)
	var offsets := atlas.get("slot_offsets") as PackedInt32Array
	var counts := atlas.get("slot_counts") as PackedInt32Array
	material.set_shader_parameter(
		"clip_offsets_0_3", Vector4(offsets[0], offsets[1], offsets[2], offsets[3])
	)
	material.set_shader_parameter("clip_offset_4", float(offsets[4]))
	material.set_shader_parameter(
		"clip_counts_0_3", Vector4(counts[0], counts[1], counts[2], counts[3])
	)
	material.set_shader_parameter("clip_count_4", float(counts[4]))
	return material


func _set_interpolation_alpha(alpha: float) -> void:
	if _tint_material != null:
		_tint_material.set_shader_parameter("interpolation_alpha", alpha)
	for group_value in _groups.values():
		var group := group_value as Group
		if group.material != null:
			group.material.set_shader_parameter("interpolation_alpha", alpha)


func _pack_tint(tint: Color) -> float:
	var red := clampi(roundi(tint.r * 255.0), 0, 255)
	var green := clampi(roundi(tint.g * 255.0), 0, 255)
	var blue := clampi(roundi(tint.b * 255.0), 0, 255)
	return float(red * 65536 + green * 256 + blue)


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
