class_name AssetFactory
extends RefCounted
## Resolve pack meshes into Node3D. OBJ is parsed into ArrayMesh (never fake BoxMesh stubs).

const RetailHouseColorScript = preload("res://src/retail_slice/retail_house_color.gd")
const PackCapability = preload("res://src/content/pack_capability.gd")
const W3DTextureMappersScript = preload("res://src/view/w3d_texture_mappers.gd")
const W3DShaderMaterialsScript = preload("res://src/view/w3d_shader_materials.gd")

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
		# Retail SubObjectsUpgrade ShowSubObjects meshes start hidden
		# (trebuchet.ini:529-531 ShowSubObjects = FirePlane). The converter
		# keeps the mesh in the GLB but does not hide it, so the AABB floor
		# snap treats a 40-source-unit fire billboard as the wheels.
		_hide_default_upgrade_subobjects(loaded)
		var drawable_result := apply_drawable_scripts(loaded, definition, [])
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
		# Borrow HouseColor from the first mounted pack that ships the file so
		# Men banners whose own pack omitted data/house-color.json still bind
		# masks. A supplemental Men file is not "this object is private retail":
		# suppress invented tint only when the owning pack provides house color
		# or a borrowed mask actually recolored surfaces.
		var house_color_root := _house_color_pack_root(definition)
		var house_colored := 0
		if house_color_root != "":
			house_colored = RetailHouseColorScript.apply(loaded, side, house_color_root)
		var private_retail := _is_private_retail_definition(definition) or house_colored > 0
		var tinted_surfaces := 0 if private_retail else _tint_if_needed(loaded, side, false)
		root.add_child(loaded)
		root.set_meta("authored", true)
		root.set_meta("mesh_path", mesh_path)
		root.set_meta("mesh_kind", _mesh_kind(loaded))
		root.set_meta("content_object_id", object_id)
		root.set_meta("animation_capability_id", String(definition.get("animationCapabilityId", "")))
		root.set_meta("team_tinted_surfaces", tinted_surfaces)
		root.set_meta("drawable_actions_applied", int(drawable_result.get("applied", 0)))
		root.set_meta("drawable_actions_unhandled", drawable_result.get("unhandled", []))
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
	content_object_id: String = "",
	definition_override: Dictionary = {}
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
	var definition: Dictionary = definition_override if not definition_override.is_empty() else (ContentDB.get_bundle_object(content_object_id) if content_object_id != "" else {})
	var private_retail := _is_private_retail_definition(definition)
	var tinted_surfaces := 0 if private_retail else _tint_if_needed(loaded, side, false)
	var house_colored := 0
	if private_retail:
		var explicit_color: Variant = definition.get("_house_color")
		house_colored = (
			RetailHouseColorScript.apply_with_color(loaded, explicit_color as Color, String(definition.get("_pack_root", "")))
			if explicit_color is Color
			else RetailHouseColorScript.apply(loaded, side, String(definition.get("_pack_root", "")))
		)
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
			_warm_model_cache(path)
			continue
		if not FileAccess.file_exists(path):
			continue
		threaded.append(path)
	if threaded.is_empty():
		return
	if threaded.size() == 1 or OS.get_processor_count() <= 1 or not parallel_glb_parse_supported():
		if not parallel_glb_parse_supported():
			_announce_serial_glb_parse()
		for path in threaded:
			_warm_model_cache(path)
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
			# Parse failed on the worker. Retry on this thread and say which
			# path, both times: a batch that quietly warms 23 of 24 models
			# leaves the twenty-fourth to fail later, somewhere else.
			push_warning("[AssetFactory] threaded GLB parse failed, retrying serially: %s" % threaded[index])
			if not _warm_model_cache(threaded[index]):
				push_error("[AssetFactory] GLB parse failed on the worker and on retry: %s" % threaded[index])
			continue
		# Animated W3D texture mappers (flags, torches, waterfalls) ride the
		# GLB material extras; tag on the main thread before the scene exists.
		W3DTextureMappersScript.tag_gltf_materials(entry["state"] as GLTFState)
		# Authored W3D One/One additive state (fortress brazier flame + halo);
		# glTF cannot carry it, the preserved shader extras can.
		W3DShaderMaterialsScript.tag_gltf_materials(entry["state"] as GLTFState)
		var node: Node3D = (entry["document"] as GLTFDocument).generate_scene(entry["state"] as GLTFState) as Node3D
		if node == null:
			push_error("[AssetFactory] GLB parsed but generated no scene: %s" % threaded[index])
			continue
		hide_w3d_hidden_meshes(node)
		_cache_model(threaded[index], node)
		# _cache_model stores a duplicate; the generated original is never
		# parented on this warm path and must be freed or it leaks at exit.
		node.free()


## Warm the cache for one path on THIS thread and report whether it landed.
##
## _try_load_model hands back a fresh Node3D the caller owns (a duplicate on a
## cache hit, the generated scene on a miss) while the cache keeps its own copy.
## Node is not reference-counted, so a warm path that ignores that return value
## leaks one node tree per call -- which the batch preload did on both of its
## serial branches, and those branches now carry every headless run.
static func _warm_model_cache(path: String) -> bool:
	var node := _try_load_model(path)
	if node == null:
		return false
	node.free()
	return true


static var _announced_serial_glb_parse := false


## Say it once, out loud, per process: the batch preload is not using the worker
## pool and why. A quiet degradation to a slower path is how "it got slower
## sometime last month" becomes unattributable.
static func _announce_serial_glb_parse() -> void:
	if _announced_serial_glb_parse:
		return
	_announced_serial_glb_parse = true
	print("[AssetFactory] GLB batch preload is SERIAL: the %s renderer's texture storage is not thread-safe (see parallel_glb_parse_supported)." % DisplayServer.get_name())


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


static func has_cached_model(path: String) -> bool:
	var cached: Variant = _mesh_cache.get(path)
	return cached is Node and is_instance_valid(cached)


## Whether GLB parsing may be dispatched to the worker pool.
##
## GLTFDocument.append_from_file does not only read geometry: it builds the
## ImageTextures for the GLB's materials, and that calls into RenderingServer's
## texture storage. Every hardware backend declares its texture RID owner
## thread-safe and marshals the call; the headless DUMMY renderer does not --
## servers/rendering/dummy/storage/texture_storage.h declares a plain
## `RID_PtrOwner<DummyTexture> texture_owner;`. Two workers allocating a texture
## at the same moment race that owner, one RID never lands in the pool, and the
## follow-up texture_2d_initialize reports `Parameter "t" is null.` at
## texture_storage.h:85 -- leaving the prop holding a texture that was never
## initialized. It is a data race, so it fires perhaps once per two hundred
## parses: rare enough to have been logged as noise in six runner logs under
## workspace/scratch and never chased.
##
## MEASURED (glb_preload_thread_safety_runner, 2026-08-16, 30 iterations over
## the same 24 pack GLBs): threaded 4 occurrences, serial 0.
static func parallel_glb_parse_supported() -> bool:
	return DisplayServer.get_name() != "headless"

static func _load_gltf(path: String) -> Node3D:
	if ResourceLoader.exists(path):
		var res = load(path)
		if res is PackedScene:
			var instance := (res as PackedScene).instantiate() as Node3D
			if instance != null:
				hide_w3d_hidden_meshes(instance)
			return instance
	var doc := GLTFDocument.new()
	var state := GLTFState.new()
	var err := doc.append_from_file(path, state)
	if err != OK:
		return null
	W3DTextureMappersScript.tag_gltf_materials(state)
	W3DShaderMaterialsScript.tag_gltf_materials(state)
	var scene := doc.generate_scene(state) as Node3D
	if scene != null:
		hide_w3d_hidden_meshes(scene)
	return scene


## Honor the authored W3D per-mesh HIDDEN flag.
##
## Retail W3D models carry placeholder meshes the engine never renders: the
## build-plot pads and ramp reference surfaces (P1 / R1 / R2 on the Gondor
## castle wall towers and the fortress trebuchet expansion towers). The
## converter preserves the W3D hidden attribute as GLB node extras
## (`{"hidden": true}`), and Godot's GLTFDocument imports node extras as node
## metadata — but nothing consumed it, so every pad rendered as a flat
## untextured gray cap on the tower tops (owner playtest 2026-08-26). Retail
## additionally authors these meshes' shaders with color writes disabled;
## hiding the node is the same picture retail draws. Sub-object upgrade states
## (ShowSubObject) address meshes by name and still re-show anything an
## upgrade authors visible.
static func hide_w3d_hidden_meshes(root: Node3D) -> int:
	var hidden_count := 0
	var stack: Array[Node] = [root]
	while not stack.is_empty():
		var node: Node = stack.pop_back()
		for child in node.get_children():
			stack.append(child)
		if not (node is Node3D):
			continue
		var extras: Variant = node.get_meta("extras", {})
		if typeof(extras) == TYPE_DICTIONARY and bool((extras as Dictionary).get("hidden", false)):
			(node as Node3D).visible = false
			hidden_count += 1
	return hidden_count


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
			if not (n as Node3D).visible:
				continue
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


static func _hide_default_upgrade_subobjects(root: Node) -> void:
	## Meshes retail only reveals through SubObjectsUpgrade ShowSubObjects start
	## hidden. The field trebuchet authors ShowSubObjects = FirePlane
	## (trebuchet.ini:529-531); the fortress-wall child uses ExtraPublicBone
	## FirePlane01. Matching is the authored token, case-insensitive.
	if root is Node3D and _is_default_hidden_subobject(root.name):
		(root as Node3D).visible = false
	for child in root.get_children():
		_hide_default_upgrade_subobjects(child)


static func _is_default_hidden_subobject(node_name: String) -> bool:
	var folded := node_name.to_lower().replace(" ", "")
	return folded == "fireplane" or folded == "fireplane01" or folded.begins_with("fireplane")


static func set_named_subobject_visible(root: Node, token: String, shown: bool) -> int:
	## Reveal or hide every node whose name matches the authored ShowSubObjects
	## token. Returns how many nodes flipped. Used by the fire-stones upgrade.
	var matched := 0
	var want := token.to_lower().replace(" ", "")
	if root is Node3D:
		var folded := root.name.to_lower().replace(" ", "")
		if folded == want or folded.begins_with(want):
			(root as Node3D).visible = shown
			matched += 1
	for child in root.get_children():
		matched += set_named_subobject_visible(child, token, shown)
	return matched


static func set_named_draw_module_visible(root: Node, token: String, shown: bool) -> int:
	## Hide or show one W3DScriptedModelDraw instance by its authored tag
	## (Draw_Trebuchet, ModuleTag_DrawLight). Exact name match; no prefix guess.
	var matched := 0
	var want := token.strip_edges().to_lower()
	if want == "":
		return 0
	if root.name.to_lower() == want and root is Node3D:
		(root as Node3D).visible = shown
		matched += 1
	for child in root.get_children():
		matched += set_named_draw_module_visible(child, token, shown)
	return matched


static func apply_drawable_scripts(root: Node, definition: Dictionary, active_conditions: Array) -> Dictionary:
	## Execute typed W3DScriptedModelDraw BeginScript operations. The default
	## model-condition state passes an empty condition set. Unknown statements
	## and operations without a runtime consumer remain explicit diagnostics.
	##
	## Permanent hide/show bits survive later non-permanent Show/Hide. Prev
	## predicates (Prev == "STATE_X" / CurDrawablePrevAnimationState()) gate
	## SetTransitionAnimState and AllowToContinue. Unknown `if` bodies fail
	## closed instead of executing their inner commands.
	var active: Dictionary = {}
	for value in active_conditions:
		active[String(value).to_upper()] = true
	var source_object := String(definition.get("sourceObjectId", definition.get("id", "")))
	var previous_labels := _drawable_previous_labels(definition, root)
	var permanent_hidden := _drawable_permanent_set(root, "drawable_permanently_hidden")
	var applied := 0
	var unhandled: Array[Dictionary] = []
	var audio_intents: Array[Dictionary] = []
	var transition_anim_state := ""
	var allow_to_continue := false
	var script_index := -1
	for script_value in definition.get("drawableScripts", []) as Array:
		script_index += 1
		if typeof(script_value) != TYPE_DICTIONARY:
			unhandled.append({"reason": "invalid-script-row"})
			continue
		var script := script_value as Dictionary
		var target := String(script.get("targetObject", ""))
		if source_object != "" and target != "" and target.to_lower() != source_object.to_lower():
			continue
		var matches := true
		for condition_value in script.get("conditions", []) as Array:
			if not active.has(String(condition_value).to_upper()):
				matches = false
				break
		if not matches:
			continue
		var skip_depth := 0
		var action_index := -1
		for action_value in script.get("actions", []) as Array:
			action_index += 1
			if typeof(action_value) != TYPE_DICTIONARY:
				unhandled.append({"reason": "invalid-action-row"})
				continue
			var action := action_value as Dictionary
			var raw := String(action.get("raw", "")).strip_edges()
			var operation := String(action.get("operation", ""))
			var arguments: Array = action.get("arguments", []) as Array
			if skip_depth > 0:
				if _drawable_raw_is_if(raw):
					if not _drawable_if_is_self_contained(raw):
						skip_depth += 1
				elif _drawable_raw_is_end(raw):
					skip_depth -= 1
				continue
			var gate := _drawable_parse_if(raw, previous_labels)
			if not gate.is_empty():
				if bool(gate.get("unknown", false)):
					unhandled.append({"operation": operation, "reason": "runtime-unsupported", "raw": raw})
					if not bool(gate.get("self_contained", false)):
						skip_depth = 1
					continue
				if not bool(gate.get("matched", false)):
					if not bool(gate.get("self_contained", false)):
						skip_depth = 1
					continue
				var gated_op := String(gate.get("operation", ""))
				if gated_op != "":
					var gated_args: Array = gate.get("arguments", []) as Array
					var gated := _drawable_execute_operation(
						root, gated_op, gated_args, true, source_object, target, script,
						script_index, action_index, permanent_hidden, audio_intents
					)
					applied += int(gated.get("applied", 0))
					unhandled.append_array(gated.get("unhandled", []) as Array)
					if String(gated.get("transition_anim_state", "")) != "":
						transition_anim_state = String(gated.get("transition_anim_state", ""))
					if bool(gated.get("allow_to_continue", false)):
						allow_to_continue = true
				continue
			if _drawable_raw_is_prev_assign(raw) or _drawable_raw_is_then(raw) or _drawable_raw_is_end(raw):
				applied += 1
				continue
			if not bool(action.get("supported", false)):
				unhandled.append({"operation": operation, "reason": "importer-unsupported", "raw": raw})
				continue
			var executed := _drawable_execute_operation(
				root, operation, arguments, true, source_object, target, script,
				script_index, action_index, permanent_hidden, audio_intents
			)
			applied += int(executed.get("applied", 0))
			unhandled.append_array(executed.get("unhandled", []) as Array)
			if String(executed.get("transition_anim_state", "")) != "":
				transition_anim_state = String(executed.get("transition_anim_state", ""))
			if bool(executed.get("allow_to_continue", false)):
				allow_to_continue = true
	root.set_meta("drawable_permanently_hidden", permanent_hidden.duplicate(true))
	return {
		"applied": applied,
		"unhandled": unhandled,
		"audio_intents": audio_intents,
		"transition_anim_state": transition_anim_state,
		"allow_to_continue": allow_to_continue,
	}


static func _drawable_previous_labels(definition: Dictionary, root: Node) -> Dictionary:
	var labels: Dictionary = {}
	var values: Array = definition.get("previousStateLabels", []) as Array
	if values.is_empty() and root.has_meta("previous_authored_state_labels"):
		values = root.get_meta("previous_authored_state_labels") as Array
	if values.is_empty() and root.has_meta("authored_state_labels"):
		# Before a clip swap, current labels are Prev for the incoming script.
		values = root.get_meta("authored_state_labels") as Array
	for value in values:
		var label := String(value)
		if label != "":
			labels[label] = true
			labels[label.to_upper()] = true
	return labels


static func _drawable_permanent_set(root: Node, meta_name: String) -> Dictionary:
	var stored: Dictionary = {}
	if root.has_meta(meta_name):
		var meta_value: Variant = root.get_meta(meta_name)
		if typeof(meta_value) == TYPE_DICTIONARY:
			stored = (meta_value as Dictionary).duplicate(true)
	return stored


static func _drawable_raw_is_if(raw: String) -> bool:
	return raw.strip_edges().to_lower().begins_with("if ") or raw.strip_edges().to_lower() == "if"


static func _drawable_raw_is_then(raw: String) -> bool:
	return raw.strip_edges().to_lower() == "then"


static func _drawable_raw_is_end(raw: String) -> bool:
	return raw.strip_edges().to_lower() == "end"


static func _drawable_raw_is_prev_assign(raw: String) -> bool:
	var folded := raw.strip_edges().to_lower().replace(" ", "")
	return folded.begins_with("prev=curdrawableprevanimationstate()")


static func _drawable_if_is_self_contained(raw: String) -> bool:
	var folded := raw.strip_edges().to_lower()
	return folded.contains(" then ") and folded.ends_with(" end")


static func _drawable_parse_if(raw: String, previous_labels: Dictionary) -> Dictionary:
	var text := raw.strip_edges()
	if not _drawable_raw_is_if(text):
		return {}
	var body := text.substr(2).strip_edges()
	var predicate := body
	var inline := ""
	var self_contained := false
	var then_at := body.to_lower().find(" then")
	if then_at >= 0:
		predicate = body.substr(0, then_at).strip_edges()
		var rest := body.substr(then_at + 5).strip_edges()
		if rest.to_lower().ends_with(" end"):
			inline = rest.substr(0, rest.length() - 4).strip_edges()
			self_contained = true
		elif rest == "":
			inline = ""
		else:
			inline = rest
			self_contained = rest.to_lower().ends_with("end")
	var pred := _drawable_eval_prev_predicate(predicate, previous_labels)
	if pred.get("unknown", true):
		return {"unknown": true, "self_contained": self_contained}
	var parsed_inline := _drawable_parse_inline_command(inline)
	return {
		"unknown": false,
		"matched": bool(pred.get("matched", false)),
		"self_contained": self_contained,
		"operation": String(parsed_inline.get("operation", "")),
		"arguments": parsed_inline.get("arguments", []),
	}


static func _drawable_eval_prev_predicate(predicate: String, previous_labels: Dictionary) -> Dictionary:
	var text := predicate.strip_edges()
	var quote_match := RegEx.new()
	quote_match.compile("(?i)^(?:Prev|CurDrawablePrevAnimationState\\(\\))\\s*==\\s*[\"']([^\"']+)[\"']\\s*$")
	var found := quote_match.search(text)
	if found == null:
		return {"unknown": true}
	var label := found.get_string(1)
	return {
		"unknown": false,
		"matched": previous_labels.has(label) or previous_labels.has(label.to_upper()),
		"label": label,
	}


static func _drawable_parse_inline_command(inline: String) -> Dictionary:
	var text := inline.strip_edges()
	if text == "":
		return {}
	var call := RegEx.new()
	call.compile("^([A-Za-z_][A-Za-z0-9_]*)\\s*\\((.*)\\)\\s*;?$")
	var found := call.search(text)
	if found == null:
		return {}
	var name := found.get_string(1).to_lower()
	var args_raw := found.get_string(2).strip_edges()
	var args: Array = []
	if args_raw != "":
		var token := args_raw
		if token.length() >= 2 and token[0] in ["\"", "'"] and token[token.length() - 1] == token[0]:
			token = token.substr(1, token.length() - 2)
		args.append(token)
	if name == "curdrawablesettransitionanimstate":
		return {"operation": "set-transition-animation-state", "arguments": args}
	if name == "curdrawableallowtocontinue":
		return {"operation": "allow-to-continue", "arguments": []}
	if name == "curdrawablehidesubobjectpermanently":
		return {"operation": "hide-sub-object-permanently", "arguments": args}
	if name == "curdrawableshowsubobjectpermanently":
		return {"operation": "show-sub-object-permanently", "arguments": args}
	if name == "curdrawablehidesubobject":
		return {"operation": "hide-sub-object", "arguments": args}
	if name == "curdrawableshowsubobject":
		return {"operation": "show-sub-object", "arguments": args}
	if name == "curdrawablehidemodule":
		return {"operation": "hide-module", "arguments": args}
	if name == "curdrawableshowmodule":
		return {"operation": "show-module", "arguments": args}
	if name == "curdrawableplaysound":
		return {"operation": "play-sound", "arguments": args}
	return {}


static func _drawable_execute_operation(
	root: Node,
	operation: String,
	arguments: Array,
	supported: bool,
	source_object: String,
	target: String,
	script: Dictionary,
	script_index: int,
	action_index: int,
	permanent_hidden: Dictionary,
	audio_intents: Array[Dictionary]
) -> Dictionary:
	var applied := 0
	var unhandled: Array[Dictionary] = []
	var transition_anim_state := ""
	var allow_to_continue := false
	if not supported:
		unhandled.append({"operation": operation, "reason": "importer-unsupported"})
		return {"applied": 0, "unhandled": unhandled}
	if operation in ["hide-sub-object", "hide-sub-object-permanently", "show-sub-object", "show-sub-object-permanently"] and arguments.size() == 1:
		var token := String(arguments[0])
		var folded := token.to_lower().replace(" ", "")
		var permanent := operation.ends_with("permanently")
		var shown := operation.begins_with("show-")
		if shown and not permanent and bool(permanent_hidden.get(folded, false)):
			applied += 1
		else:
			var match_count := set_named_subobject_visible(root, token, shown)
			if match_count > 0:
				applied += 1
				if permanent and not shown:
					permanent_hidden[folded] = true
				elif permanent and shown:
					permanent_hidden.erase(folded)
			else:
				unhandled.append({"operation": operation, "reason": "sub-object-not-found", "argument": token})
	elif operation in ["hide-module", "show-module"] and arguments.size() == 1:
		var module_token := String(arguments[0])
		var module_shown := operation == "show-module"
		var module_matches := set_named_draw_module_visible(root, module_token, module_shown)
		if module_matches > 0:
			applied += 1
		else:
			unhandled.append({"operation": operation, "reason": "draw-module-not-found", "argument": module_token})
	elif operation == "set-transition-animation-state" and arguments.size() == 1 and String(arguments[0]) != "":
		transition_anim_state = String(arguments[0])
		applied += 1
	elif operation == "allow-to-continue" and arguments.is_empty():
		allow_to_continue = true
		applied += 1
	elif operation == "play-sound" and arguments.size() == 1 and String(arguments[0]) != "":
		audio_intents.append({
			"event_id": String(arguments[0]),
			"source_object_id": source_object if source_object != "" else target,
			"target_object_id": target,
			"conditions": (script.get("conditions", []) as Array).duplicate(true),
			"script_index": script_index,
			"action_index": action_index,
		})
		applied += 1
	else:
		unhandled.append({"operation": operation, "reason": "runtime-unsupported", "arguments": arguments.duplicate(true)})
	return {
		"applied": applied,
		"unhandled": unhandled,
		"transition_anim_state": transition_anim_state,
		"allow_to_continue": allow_to_continue,
	}


static func _house_color_pack_root(definition: Dictionary) -> String:
	var own := String(definition.get("_pack_root", ""))
	if _pack_has_house_color_file(own):
		return own
	var content_db := Engine.get_main_loop()
	if content_db is SceneTree:
		var db = (content_db as SceneTree).root.get_node_or_null("ContentDB")
		if db != null:
			for root_value in db.pack_roots:
				var root := String(root_value)
				if _pack_has_house_color_file(root):
					return root
	return ""


static func _pack_has_house_color_file(pack_root: String) -> bool:
	if pack_root == "" or pack_root.begins_with("res://"):
		return false
	if not PackCapability.provides_house_color(pack_root):
		return false
	var path := pack_root.replace("\\", "/").path_join("data/house-color.json")
	return FileAccess.file_exists(path)

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
	## Decides house-color recolor (exact, mask-driven) vs the invented team
	## tint. The branch below consumes the pack's converted HouseColor masks, so
	## that is what is asked for here. Asking for the pack id instead sent every
	## other converted pack down the invented-tint path.
	var pack_root := String(definition.get("_pack_root", ""))
	if pack_root == "":
		return false
	if _private_retail_pack_cache.has(pack_root):
		return bool(_private_retail_pack_cache[pack_root])
	var result := PackCapability.provides_house_color(pack_root)
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
