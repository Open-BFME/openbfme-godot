class_name SnapshotTemplateMeshResolver
extends RefCounted
## Resolves snapshot-v1 template indexes to mounted pack GLB meshes.
##
## The cooked simulation bundle names retail Object templates (for example
## GondorFighter), while ContentDB indexes presentation documents by stable
## bundle ids. The projected pack rows retain sourceObjectId, which is the
## authoritative bridge between those two namespaces. Animation remains a
## separate renderer hook; this helper extracts only the first triangle mesh.

var _names_by_index: Dictionary = {}
var _cache: Dictionary = {}


func configure(template_rows: Array[Dictionary]) -> void:
	_names_by_index.clear()
	_cache.clear()
	for row in template_rows:
		var index := int(row.get("index", -1))
		var name := String(row.get("name", ""))
		if index >= 0 and not name.is_empty():
			_names_by_index[index] = name


func template_name(template_index: int) -> String:
	return String(_names_by_index.get(template_index, ""))


func resolve(template_index: int) -> Dictionary:
	if _cache.has(template_index):
		return (_cache[template_index] as Dictionary).duplicate()
	var result := {"mesh": null, "path": "", "template": template_name(template_index)}
	var name := String(result["template"])
	var content_db := _autoload("ContentDB")
	var mod_loader := _autoload("ModLoader")
	if name.is_empty() or content_db == null or mod_loader == null:
		_cache[template_index] = result
		return result.duplicate()
	var definition := _definition_for_template(content_db, name)
	if definition.is_empty():
		_cache[template_index] = result
		return result.duplicate()
	var path := String(content_db.resolve_mesh_path(definition))
	var pack_root := String(definition.get("_pack_root", ""))
	if (
		path.get_extension().to_lower() != "glb"
		or pack_root.is_empty()
		or not bool(mod_loader.path_is_within(pack_root, path))
	):
		_cache[template_index] = result
		return result.duplicate()
	var mesh := _load_first_surface(path)
	if mesh != null:
		result["mesh"] = mesh
		result["path"] = path
	_cache[template_index] = result
	return result.duplicate()


func _definition_for_template(content_db: Node, template_name_value: String) -> Dictionary:
	var direct: Dictionary = content_db.get_bundle_object(template_name_value)
	if not direct.is_empty():
		return direct
	var target := template_name_value.to_lower()
	var ids: Array = content_db.bundle_objects.keys()
	ids.sort_custom(func(a: Variant, b: Variant) -> bool:
		return String(a).naturalnocasecmp_to(String(b)) < 0
	)
	for id_value in ids:
		var row: Dictionary = content_db.bundle_objects[id_value]
		if String(row.get("sourceObjectId", "")).to_lower() == target:
			return row
	return {}


func _load_first_surface(path: String) -> ArrayMesh:
	var document := GLTFDocument.new()
	var state := GLTFState.new()
	if document.append_from_file(path, state) != OK:
		return null
	var scene := document.generate_scene(state) as Node3D
	if scene == null:
		return null
	var mesh := _first_mesh_surface(scene, Transform3D.IDENTITY)
	scene.free()
	return mesh


func _first_mesh_surface(node: Node, parent_transform: Transform3D) -> ArrayMesh:
	var transform := parent_transform
	if node is Node3D:
		transform = parent_transform * (node as Node3D).transform
	if node is MeshInstance3D:
		var source := (node as MeshInstance3D).mesh
		if (
			source != null
			and source.get_surface_count() > 0
			and source.surface_get_primitive_type(0) == Mesh.PRIMITIVE_TRIANGLES
		):
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


func _autoload(node_name: String) -> Node:
	var tree := Engine.get_main_loop() as SceneTree
	return null if tree == null else tree.root.get_node_or_null(node_name)
