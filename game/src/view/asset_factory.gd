class_name AssetFactory
extends RefCounted
## Resolve pack meshes into Node3D. OBJ is parsed into ArrayMesh (never fake BoxMesh stubs).

static var _mesh_cache: Dictionary = {}

static func make_unit_visual(type_id: String, side: int) -> Node3D:
	var def: Dictionary = ContentDB.get_unit(type_id)
	var root := Node3D.new()
	root.name = "Unit_%s" % type_id
	var mesh_path := ContentDB.resolve_mesh_path(def)
	var loaded := _try_load_model(mesh_path)
	if loaded:
		var th := 2.4
		if bool(def.get("hero", false)):
			th = 2.9
		if bool(def.get("monster", false)):
			th = 5.2
		if bool(def.get("cavalry", false)):
			th = 2.6
		_scale_to_height(loaded, th)
		_tint_if_needed(loaded, side, bool(def.get("hero", false)))
		root.add_child(loaded)
		root.set_meta("authored", true)
		root.set_meta("mesh_path", mesh_path)
		root.set_meta("mesh_kind", _mesh_kind(loaded))
	else:
		# multi-part kit (cylinders+boxes) — still multi-primitive, not single capsule
		var kit := _kit_unit_multipart(def, side)
		root.add_child(kit)
		root.set_meta("authored", true)
		root.set_meta("mesh_path", "kit_multipart:%s" % type_id)
		root.set_meta("mesh_kind", "multipart_kit")
	return root

static func make_building_visual(type_id: String, side: int) -> Node3D:
	var def: Dictionary = ContentDB.get_building(type_id)
	var root := Node3D.new()
	root.name = "Bld_%s" % type_id
	var mesh_path := ContentDB.resolve_mesh_path(def)
	var loaded := _try_load_model(mesh_path)
	if loaded:
		var target_h := 6.0
		if bool(def.get("fortress", false)):
			target_h = 12.0
		elif bool(def.get("tower", false)):
			target_h = 9.0
		elif bool(def.get("wall", false)):
			target_h = 4.0
		_scale_to_height(loaded, target_h)
		root.add_child(loaded)
		root.set_meta("authored", true)
		root.set_meta("mesh_path", mesh_path)
		root.set_meta("mesh_kind", _mesh_kind(loaded))
	else:
		var kit := _kit_building_multipart(def, side)
		root.add_child(kit)
		root.set_meta("authored", true)
		root.set_meta("mesh_path", "kit_multipart:%s" % type_id)
		root.set_meta("mesh_kind", "multipart_kit")
	return root

static func _mesh_kind(node: Node3D) -> String:
	var mis: Array = []
	_collect_meshes(node, mis)
	if mis.is_empty():
		return "empty"
	if mis.size() == 1:
		var m: Mesh = (mis[0] as MeshInstance3D).mesh
		if m is BoxMesh:
			return "single_box"
		if m is CapsuleMesh:
			return "single_capsule"
		if m is ArrayMesh:
			return "array_mesh"
		return "single_other"
	return "multipart"

static func _collect_meshes(n: Node, out: Array) -> void:
	if n is MeshInstance3D:
		out.append(n)
	for c in n.get_children():
		_collect_meshes(c, out)

static func is_blockout_visual(node: Node3D) -> bool:
	var kind := String(node.get_meta("mesh_kind", ""))
	if kind in ["single_box", "single_capsule", "empty"]:
		return true
	var path := String(node.get_meta("mesh_path", ""))
	if path.begins_with("kit:") and not path.begins_with("kit_multipart:"):
		return true
	return false

static func _try_load_model(path: String) -> Node3D:
	if path == null or String(path) == "":
		return null
	if _mesh_cache.has(path):
		return (_mesh_cache[path] as Node).duplicate() as Node3D
	var abs_path := path
	if path.begins_with("res://"):
		abs_path = ProjectSettings.globalize_path(path)
	if not FileAccess.file_exists(path) and not FileAccess.file_exists(abs_path) and not ResourceLoader.exists(path):
		return null
	var node: Node3D = null
	var use_path := path if FileAccess.file_exists(path) or ResourceLoader.exists(path) else abs_path
	# Prefer substantial sibling .obj first — many pack GLBs use meshopt/quantization
	# which Godot cannot load at runtime without extensions.
	var obj_sibling := use_path.get_basename() + ".obj"
	if FileAccess.file_exists(obj_sibling):
		var of := FileAccess.open(obj_sibling, FileAccess.READ)
		if of and of.get_length() >= 1000:
			node = _load_obj_arraymesh(obj_sibling)
	if node == null and (use_path.ends_with(".glb") or use_path.ends_with(".gltf")):
		node = _load_gltf(use_path)
	elif node == null and use_path.ends_with(".obj"):
		node = _load_obj_arraymesh(use_path)
	if node:
		_mesh_cache[path] = node.duplicate()
	return node

static func _load_gltf(path: String) -> Node3D:
	if ResourceLoader.exists(path):
		var res = load(path)
		if res is PackedScene:
			return (res as PackedScene).instantiate() as Node3D
	var doc := GLTFDocument.new()
	var state := GLTFState.new()
	var err := doc.append_from_file(path, state)
	if err != OK:
		return null
	return doc.generate_scene(state) as Node3D

static func _load_obj_arraymesh(path: String) -> Node3D:
	## Real OBJ → ArrayMesh. Rejects empty / tiny geometry.
	var f := FileAccess.open(path, FileAccess.READ)
	if f == null:
		return null
	var text := f.get_as_text()
	if text.length() < 1000:
		return null
	var positions: PackedVector3Array = PackedVector3Array()
	var faces: Array = [] # each: PackedInt32Array of position indices
	for line in text.split("\n"):
		var s := line.strip_edges()
		if s.begins_with("v "):
			var p := s.split(" ", false)
			if p.size() >= 4:
				positions.append(Vector3(float(p[1]), float(p[2]), float(p[3])))
		elif s.begins_with("f "):
			var p2 := s.split(" ", false)
			var idx := PackedInt32Array()
			for i in range(1, p2.size()):
				var tok := String(p2[i]).split("/")[0]
				if tok.is_valid_int():
					idx.append(int(tok) - 1)
			if idx.size() >= 3:
				faces.append(idx)
	if positions.size() < 8 or faces.is_empty():
		return null
	var st := SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)
	for face in faces:
		var ids: PackedInt32Array = face
		# fan triangulation
		for i in range(1, ids.size() - 1):
			for vi in [ids[0], ids[i], ids[i + 1]]:
				if vi >= 0 and vi < positions.size():
					st.add_vertex(positions[vi])
	st.generate_normals()
	var mesh: ArrayMesh = st.commit()
	if mesh == null or mesh.get_surface_count() == 0:
		return null
	var root := Node3D.new()
	var mi := MeshInstance3D.new()
	mi.mesh = mesh
	var mat := StandardMaterial3D.new()
	mat.albedo_color = Color(0.62, 0.58, 0.52)
	mat.roughness = 0.7
	mi.material_override = mat
	root.add_child(mi)
	root.set_meta("vertex_count", positions.size())
	root.set_meta("face_count", faces.size())
	return root

static func _scale_to_height(node: Node3D, target_h: float) -> void:
	var aabb := _aabb_of(node)
	var h := aabb.size.y
	if h < 0.001:
		h = 1.0
	var s := target_h / h
	node.scale = Vector3.ONE * s
	var aabb2 := _aabb_of(node)
	node.position.y -= aabb2.position.y

static func _aabb_of(node: Node3D) -> AABB:
	var acc := AABB(Vector3.ZERO, Vector3.ONE)
	var found := false
	var stack: Array = [[node, Transform3D.IDENTITY]]
	while stack.size() > 0:
		var item: Array = stack.pop_back()
		var n: Node = item[0]
		var xf: Transform3D = item[1]
		if n is Node3D:
			xf = xf * (n as Node3D).transform
		if n is MeshInstance3D:
			var mi := n as MeshInstance3D
			if mi.mesh:
				var a: AABB = xf * mi.mesh.get_aabb()
				if not found:
					acc = a
					found = true
				else:
					acc = acc.merge(a)
		for c in n.get_children():
			stack.append([c, xf])
	return acc

static func _tint_if_needed(node: Node3D, side: int, hero: bool) -> void:
	# light team tint on materials for readability
	var col := Color(0.45, 0.65, 1.0) if side == 0 else Color(1.0, 0.4, 0.35)
	if hero:
		col = Color(1.0, 0.88, 0.35) if side == 0 else Color(0.7, 0.35, 0.9)
	var mis: Array = []
	_collect_meshes(node, mis)
	for mi in mis:
		var m: MeshInstance3D = mi
		if m.material_override is StandardMaterial3D:
			var mat: StandardMaterial3D = m.material_override.duplicate()
			mat.albedo_color = mat.albedo_color.lerp(col, 0.35)
			m.material_override = mat

static func _kit_unit_multipart(def: Dictionary, side: int) -> Node3D:
	var root := Node3D.new()
	var col := Color(0.3, 0.55, 1.0) if side == 0 else Color(0.9, 0.25, 0.2)
	if bool(def.get("hero", false)):
		col = Color(1.0, 0.85, 0.2) if side == 0 else Color(0.55, 0.2, 0.7)
	# legs, torso, head, arms — never a single capsule alone
	_add_cyl(root, Vector3(-0.15, 0.35, 0), 0.12, 0.7, col)
	_add_cyl(root, Vector3(0.15, 0.35, 0), 0.12, 0.7, col)
	_add_box(root, Vector3(0, 1.0, 0), Vector3(0.5, 0.7, 0.28), col)
	_add_cyl(root, Vector3(0, 1.55, 0), 0.16, 0.3, col)
	_add_cyl(root, Vector3(-0.35, 1.2, 0), 0.08, 0.55, col)
	_add_cyl(root, Vector3(0.35, 1.2, 0), 0.08, 0.55, col)
	return root

static func _kit_building_multipart(def: Dictionary, side: int) -> Node3D:
	var root := Node3D.new()
	var col := Color(0.5, 0.55, 0.65) if side == 0 else Color(0.4, 0.2, 0.18)
	var h := 5.0
	if bool(def.get("fortress", false)):
		h = 10.0
	_add_box(root, Vector3(0, h * 0.5, 0), Vector3(5, h, 5), col)
	_add_box(root, Vector3(0, h + 0.3, 0), Vector3(5.5, 0.6, 5.5), col.darkened(0.2))
	_add_cyl(root, Vector3(2, h + 0.6, 2), 0.4, 2.0, col.darkened(0.1))
	return root

static func _add_box(parent: Node3D, pos: Vector3, size: Vector3, col: Color) -> void:
	var mi := MeshInstance3D.new()
	var m := BoxMesh.new()
	m.size = size
	mi.mesh = m
	mi.position = pos
	var mat := StandardMaterial3D.new()
	mat.albedo_color = col
	mi.material_override = mat
	parent.add_child(mi)

static func _add_cyl(parent: Node3D, pos: Vector3, r: float, h: float, col: Color) -> void:
	var mi := MeshInstance3D.new()
	var m := CylinderMesh.new()
	m.top_radius = r
	m.bottom_radius = r
	m.height = h
	mi.mesh = m
	mi.position = pos
	var mat := StandardMaterial3D.new()
	mat.albedo_color = col
	mi.material_override = mat
	parent.add_child(mi)
