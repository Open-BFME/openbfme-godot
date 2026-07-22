class_name AssetFactory
extends RefCounted
## Resolve pack meshes into Node3D. OBJ is parsed into ArrayMesh (never fake BoxMesh stubs).

const RetailHouseColorScript = preload("res://src/retail_slice/retail_house_color.gd")

static var _mesh_cache: Dictionary = {}
static var _private_retail_pack_cache: Dictionary = {}
# Bound raised with the full-faction rosters: a faction fields 30+ unique unit
# GLBs plus bound map props, and a 16-entry cache thrashed (evict-then-reparse)
# across the boot roster and the first production wave.
const MAX_MESH_CACHE_ENTRIES := 64

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
		var tinted_surfaces := _tint_if_needed(loaded, side, bool(def.get("hero", false)))
		root.add_child(loaded)
		root.set_meta("authored", true)
		root.set_meta("mesh_path", mesh_path)
		root.set_meta("mesh_kind", _mesh_kind(loaded))
		root.set_meta("team_tinted_surfaces", tinted_surfaces)
		_annotate_rig_and_animation(root, loaded)
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
		_annotate_rig_and_animation(root, loaded)
	else:
		var kit := _kit_building_multipart(def, side)
		root.add_child(kit)
		root.set_meta("authored", true)
		root.set_meta("mesh_path", "kit_multipart:%s" % type_id)
		root.set_meta("mesh_kind", "multipart_kit")
	return root


static func make_bundle_object_visual(object_id: String, side: int, source_unit_scale: float = 0.0) -> Node3D:
	## Presentation bridge for consolidated content-bundle v0 objects. Imported
	## GLBs keep their Skeleton3D, skins, materials, and AnimationPlayer nodes.
	var definition: Dictionary = ContentDB.get_bundle_object(object_id)
	var root := Node3D.new()
	root.name = "Object_%s" % object_id
	var mesh_path := ContentDB.resolve_mesh_path(definition)
	var loaded := _try_load_model(mesh_path)
	if loaded:
		if is_finite(source_unit_scale) and source_unit_scale > 0.0:
			# Retail W3D geometry and the cooked map share SAGE world units. The
			# selected-map runtime therefore applies its single validated source-to-
			# local scale instead of independently normalizing every model by height.
			loaded.scale = Vector3.ONE * source_unit_scale
			# Hero skins often pivot above the soles. Snap the mesh AABB floor to
			# y=0 so units stand on the terrain instead of floating in a T-pose.
			if String(definition.get("kind", "")) != "structure":
				var grounded := _aabb_of(loaded)
				if is_finite(grounded.position.y) and absf(grounded.position.y) > 0.0001:
					loaded.position.y -= grounded.position.y
		else:
			var presentation: Variant = definition.get("presentation", {})
			var target_height := 2.4 if String(definition.get("kind", "")) != "structure" else 7.0
			if typeof(presentation) == TYPE_DICTIONARY:
				var millimeters := float((presentation as Dictionary).get("heightMillimeters", 0.0))
				if millimeters > 0.0:
					target_height = millimeters / 1000.0
			_scale_to_height(loaded, target_height)
		var private_retail := _is_private_retail_definition(definition)
		var tinted_surfaces := 0 if private_retail else _tint_if_needed(loaded, side, false)
		var house_colored := 0
		if private_retail:
			# Exact retail house color: recolor only mask-marked pixels using the
			# pack's converted HouseColor masks. Invented whole-material tints
			# stay suppressed in private parity mode.
			house_colored = RetailHouseColorScript.apply(loaded, side, String(definition.get("_pack_root", "")))
		root.add_child(loaded)
		root.set_meta("authored", true)
		root.set_meta("mesh_path", mesh_path)
		root.set_meta("mesh_kind", _mesh_kind(loaded))
		root.set_meta("content_object_id", object_id)
		root.set_meta("animation_capability_id", String(definition.get("animationCapabilityId", "")))
		root.set_meta("team_tinted_surfaces", tinted_surfaces)
		root.set_meta("house_color_surfaces", house_colored)
		var retail_color_status := "retail-house-color-masked" if house_colored > 0 else "source-OkToChangeModelColor-awaiting-exact-house-color-no-invented-tint"
		root.set_meta("team_color_status", retail_color_status if private_retail else "fallback-team-tint")
		root.set_meta("team", side)
		root.set_meta("source_unit_scale", source_unit_scale if source_unit_scale > 0.0 else 0.0)
		_annotate_rig_and_animation(root, loaded)
	else:
		var fallback := _kit_building_multipart(definition, side) if String(definition.get("kind", "")) == "structure" else _kit_unit_multipart(definition, side)
		root.add_child(fallback)
		root.set_meta("authored", false)
		root.set_meta("mesh_path", "")
		root.set_meta("mesh_kind", "multipart_kit")
		root.set_meta("content_object_id", object_id)
	return root


static func preflight_explicit_model_path(resolved_model_path: String, pack_root: String = "") -> String:
	## Validate a resolved, contained GLB without instantiating its full scene.
	## Lifecycle consumers use this for every phase before lazily loading any
	## phase other than the intact scale reference and the initially active one.
	var normalized := resolved_model_path.replace("\\", "/")
	if normalized == "":
		return "resolved model path is empty"
	if pack_root != "" and not ModLoader.path_is_within(pack_root, normalized):
		return "resolved model path escapes the selected pack"
	if not ContentDB.is_resolved_asset_path(normalized):
		return "resolved model path is not a contained pack asset"
	if normalized.get_extension().to_lower() != "glb":
		return "explicit lifecycle model is not a GLB"
	var file := FileAccess.open(normalized, FileAccess.READ)
	if file == null:
		return "explicit lifecycle GLB cannot be opened"
	var file_length := file.get_length()
	if file_length < 12:
		return "explicit lifecycle GLB is shorter than its header"
	var header := file.get_buffer(12)
	if header.size() != 12 or header.slice(0, 4).get_string_from_ascii() != "glTF":
		return "explicit lifecycle GLB has an invalid magic header"
	if header.decode_u32(4) != 2:
		return "explicit lifecycle GLB is not version 2"
	if int(header.decode_u32(8)) != file_length:
		return "explicit lifecycle GLB length header does not match the file"
	return ""


static func make_explicit_model_visual(
	resolved_model_path: String,
	side: int,
	content_object_id: String = ""
) -> Node3D:
	## Fail-closed bridge for an already-resolved authored GLB. Unlike the
	## generic bundle bridge, this helper never normalizes each model and never
	## creates a procedural fallback. The lifecycle owner applies one transform
	## derived from the intact body's AABB to every phase/component.
	if preflight_explicit_model_path(resolved_model_path) != "":
		return null
	var loaded := _try_load_model(resolved_model_path)
	if loaded == null:
		return null
	var root := Node3D.new()
	root.name = "ExplicitModel_%s" % resolved_model_path.get_file().get_basename()
	var definition: Dictionary = ContentDB.get_bundle_object(content_object_id) if content_object_id != "" else {}
	var private_retail := _is_private_retail_definition(definition)
	var tinted_surfaces := 0 if private_retail else _tint_if_needed(loaded, side, false)
	var house_colored := 0
	if private_retail:
		house_colored = RetailHouseColorScript.apply(loaded, side, String(definition.get("_pack_root", "")))
	root.add_child(loaded)
	root.set_meta("authored", true)
	root.set_meta("mesh_path", resolved_model_path)
	root.set_meta("mesh_kind", _mesh_kind(loaded))
	root.set_meta("content_object_id", content_object_id)
	root.set_meta("team_tinted_surfaces", tinted_surfaces)
	root.set_meta("house_color_surfaces", house_colored)
	root.set_meta("team_color_status", ("retail-house-color-masked" if house_colored > 0 else "source-OkToChangeModelColor-awaiting-exact-house-color-no-invented-tint") if private_retail else "fallback-team-tint")
	root.set_meta("team", side)
	_annotate_rig_and_animation(root, loaded)
	return root


static func model_aabb(node: Node3D) -> AABB:
	## Public read-only geometry query used to establish a shared lifecycle
	## transform from the intact body only.
	var meshes: Array = []
	_collect_meshes(node, meshes)
	if meshes.is_empty():
		return AABB(Vector3.ZERO, Vector3.ZERO)
	return _aabb_of(node)


static func _annotate_rig_and_animation(root: Node3D, loaded: Node3D) -> void:
	var animation_names: Array[String] = []
	var stack: Array[Node] = [loaded]
	var has_skeleton := false
	while not stack.is_empty():
		var node: Node = stack.pop_back() as Node
		if node is Skeleton3D:
			has_skeleton = true
		elif node is AnimationPlayer:
			for animation_name in (node as AnimationPlayer).get_animation_list():
				var clip := String(animation_name)
				if clip != "RESET" and not animation_names.has(clip):
					animation_names.append(clip)
		for child in node.get_children():
			stack.append(child)
	animation_names.sort()
	root.set_meta("has_skeleton", has_skeleton)
	root.set_meta("animation_clips", animation_names)

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

static func preload_models_threaded(paths: Array) -> void:
	## Batch-warm the mesh cache for a set of GLB paths. The expensive GLB binary
	## parse (GLTFDocument.append_from_file) fans out across the worker pool;
	## scene generation and cache insertion stay on the calling thread, so later
	## _try_load_model calls on the same paths are ordinary cache hits. Paths
	## that are missing, cached, or imported (ResourceLoader-visible) keep their
	## established lazy-load behavior exactly.
	var threaded: Array[String] = []
	var seen: Dictionary = {}
	for path_value in paths:
		var path := String(path_value)
		if path == "" or seen.has(path):
			continue
		seen[path] = true
		if _mesh_cache.has(path):
			continue
		var extension := path.get_extension().to_lower()
		if extension != "glb" and extension != "gltf":
			continue
		if ResourceLoader.exists(path):
			# Imported resources load fast through the engine; keep that path.
			_try_load_model(path)
			continue
		if not FileAccess.file_exists(path):
			continue
		threaded.append(path)
	if threaded.is_empty():
		return
	if threaded.size() == 1 or OS.get_processor_count() <= 1:
		for path in threaded:
			_try_load_model(path)
		return
	var parsed: Array = []
	parsed.resize(threaded.size())
	var group := WorkerThreadPool.add_group_task(
		func(element: int) -> void:
			var document := GLTFDocument.new()
			var state := GLTFState.new()
			if document.append_from_file(threaded[element], state) == OK:
				parsed[element] = {"document": document, "state": state},
		threaded.size()
	)
	WorkerThreadPool.wait_for_group_task_completion(group)
	for index in threaded.size():
		var entry: Variant = parsed[index]
		if typeof(entry) != TYPE_DICTIONARY:
			# Parse failed on the worker: the lazy path retries and applies the
			# established failure handling at the call site.
			continue
		var node: Node3D = (entry["document"] as GLTFDocument).generate_scene(entry["state"] as GLTFState) as Node3D
		if node != null:
			_cache_model(threaded[index], node)
			# _cache_model stores a duplicate; the generated original is never
			# parented on this warm path and must be freed or it leaks at exit.
			node.free()


static func _try_load_model(path: String) -> Node3D:
	if path == null or String(path) == "":
		return null
	if _mesh_cache.has(path):
		var cached: Node = _mesh_cache[path] as Node
		if is_instance_valid(cached):
			return cached.duplicate() as Node3D
		_mesh_cache.erase(path)
	var abs_path := path
	if path.begins_with("res://") or path.begins_with("user://"):
		abs_path = ProjectSettings.globalize_path(path)
	if not FileAccess.file_exists(path) and not FileAccess.file_exists(abs_path) and not ResourceLoader.exists(path):
		return null
	var node: Node3D = null
	var use_path := path if ResourceLoader.exists(path) else abs_path
	# The resolver already chooses a legacy OBJ when appropriate. An explicit
	# bundle GLB stays a GLB so its rig, skin, materials, and clips survive.
	if use_path.ends_with(".glb") or use_path.ends_with(".gltf"):
		node = _load_gltf(use_path)
	elif use_path.ends_with(".obj"):
		node = _load_obj_arraymesh(use_path)
	if node:
		_cache_model(path, node)
	return node


static func _cache_model(path: String, node: Node3D) -> void:
	while _mesh_cache.size() >= MAX_MESH_CACHE_ENTRIES:
		var oldest_key: Variant = _mesh_cache.keys()[0]
		var oldest: Variant = _mesh_cache.get(oldest_key)
		_mesh_cache.erase(oldest_key)
		if oldest is Node and is_instance_valid(oldest):
			(oldest as Node).free()
	_mesh_cache[path] = node.duplicate()


static func clear_mesh_cache() -> void:
	for cached in _mesh_cache.values():
		if cached is Node and is_instance_valid(cached):
			(cached as Node).free()
	_mesh_cache.clear()


static func mesh_cache_size() -> int:
	return _mesh_cache.size()

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


static func load_texture_asset(path: String) -> Texture2D:
	## Load a contained pack texture even when it lives outside res:// and has no
	## Godot import sidecar/cache. Intended for portraits, icons, and UI atlases.
	if not ContentDB.is_resolved_asset_path(path):
		return null
	if ResourceLoader.exists(path):
		return load(path) as Texture2D
	var absolute := ProjectSettings.globalize_path(path) if path.begins_with("res://") or path.begins_with("user://") else path
	var image := Image.new()
	if image.load(absolute) != OK:
		return null
	return ImageTexture.create_from_image(image)

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

static func _tint_if_needed(node: Node3D, side: int, hero: bool) -> int:
	# GLTF materials normally live on ArrayMesh surfaces rather than as a
	# MeshInstance override. Duplicate the mesh and its material resources so the
	# immutable cached scene and embedded textures remain untouched while the two
	# teams are visibly distinct.
	var col := Color(0.35, 0.62, 1.0) if side == 0 else Color(1.0, 0.42, 0.35)
	if hero:
		col = Color(1.0, 0.88, 0.35) if side == 0 else Color(0.7, 0.35, 0.9)
	var mis: Array = []
	_collect_meshes(node, mis)
	var tinted_surfaces := 0
	for mi in mis:
		var m: MeshInstance3D = mi
		if m.material_override is StandardMaterial3D:
			var mat := _duplicate_tinted_material(m.material_override as StandardMaterial3D, col)
			m.material_override = mat
			tinted_surfaces += 1
			continue
		if m.mesh == null:
			continue
		# Keep ownership explicit. A private mesh plus private material resources
		# avoids mutating the cache and avoids renderer teardown hazards associated
		# with dynamic per-surface instance overrides on imported skinned GLTFs.
		var private_mesh: Mesh = m.mesh.duplicate(false) as Mesh
		m.mesh = private_mesh
		for surface in range(private_mesh.get_surface_count()):
			var source: Material = private_mesh.surface_get_material(surface)
			if source is StandardMaterial3D:
				private_mesh.surface_set_material(surface, _duplicate_tinted_material(source as StandardMaterial3D, col))
				tinted_surfaces += 1
	return tinted_surfaces


static func _duplicate_tinted_material(source: StandardMaterial3D, team_color: Color) -> StandardMaterial3D:
	var material: StandardMaterial3D = source.duplicate(false) as StandardMaterial3D
	var target := Color(team_color.r, team_color.g, team_color.b, material.albedo_color.a)
	material.albedo_color = material.albedo_color.lerp(target, 0.42)
	return material


static func _is_private_retail_definition(definition: Dictionary) -> bool:
	var pack_root := String(definition.get("_pack_root", ""))
	if pack_root == "" or pack_root.begins_with("res://"):
		return false
	if _private_retail_pack_cache.has(pack_root):
		return bool(_private_retail_pack_cache[pack_root])
	var pack_path := ModLoader.resolve_pack_path(pack_root, "pack.json")
	var pack_value: Variant = ModLoader._read_json(pack_path)
	var result := typeof(pack_value) == TYPE_DICTIONARY and String((pack_value as Dictionary).get("id", "")) == "bfme2-men-vslice"
	_private_retail_pack_cache[pack_root] = result
	return result

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
