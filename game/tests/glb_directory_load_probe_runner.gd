extends SceneTree
## Headless load probe for freshly cooked GLBs that are not in a pack yet.
##
## The asset cook lane produces GLB files in a staging directory outside any
## pack root, so nothing in ContentDB can see them and no existing runner can
## open them. This probe walks a caller-supplied directory and drives each file
## through the same GLTFDocument path AssetFactory._load_gltf uses, then reports
## what the engine actually built: surfaces, skeleton bones, animation clips.
##
## The directory comes from OPENBFME_GLB_PROBE_DIR. There is no default and no
## fallback: an unset variable, a missing directory, or a directory with no GLB
## in it is a failure, because a silent zero-file pass would be indistinguishable
## from a successful probe.
##
## Every file must parse and carry geometry. A rig is only demanded when the
## caller sets OPENBFME_GLB_PROBE_REQUIRE_RIG=1, because static and hierarchical
## props legitimately convert to a GLB with no skeleton and no clips -- asserting
## a rig on those would fail correct output. Bone and clip counts are printed
## either way so a caller can read what actually came out.
##
## A retail pivot-only model (the adapter's --proven-pivot-only-model case) has
## no geometry at all by construction. Those file names must be named in
## OPENBFME_GLB_PROBE_PIVOT_ONLY, comma separated: they are then required to be
## geometry-free and to still carry a rig, so a silently empty conversion of a
## normal model cannot hide behind the exemption.

var passed := 0
var failed := 0
var require_rig := false
var pivot_only: PackedStringArray = PackedStringArray()


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	require_rig = OS.get_environment("OPENBFME_GLB_PROBE_REQUIRE_RIG") == "1"
	var declared := OS.get_environment("OPENBFME_GLB_PROBE_PIVOT_ONLY").strip_edges()
	if declared != "":
		for entry in declared.split(","):
			var trimmed := entry.strip_edges()
			if trimmed != "":
				pivot_only.append(trimmed)
	var directory := OS.get_environment("OPENBFME_GLB_PROBE_DIR")
	_check("probe_directory_supplied", directory != "",
		"set OPENBFME_GLB_PROBE_DIR to the staging directory to probe")
	if directory == "":
		return _finish()

	var paths: Array[String] = []
	_scan(directory, paths, 0)
	paths.sort()
	_check("probe_directory_contains_glb", not paths.is_empty(),
		"no .glb under %s" % directory)
	if paths.is_empty():
		return _finish()

	print("GLB_PROBE directory=%s files=%d require_rig=%s" % [
		directory, paths.size(), str(require_rig),
	])
	for path in paths:
		_probe(path)

	_finish()


func _probe(path: String) -> void:
	var name := path.get_file()
	var document := GLTFDocument.new()
	var state := GLTFState.new()
	var error := document.append_from_file(path, state)
	if error != OK:
		_check("parsed:%s" % name, false, "append_from_file error %d" % error)
		return
	var scene := document.generate_scene(state) as Node3D
	if scene == null:
		_check("parsed:%s" % name, false, "generate_scene returned null")
		return

	var surfaces := 0
	var vertices := 0
	var bones := 0
	var clips: PackedStringArray = PackedStringArray()
	for node in _descendants(scene):
		if node is MeshInstance3D:
			var mesh := (node as MeshInstance3D).mesh
			if mesh != null:
				surfaces += mesh.get_surface_count()
				for surface in range(mesh.get_surface_count()):
					var arrays := mesh.surface_get_arrays(surface)
					var positions: Variant = arrays[Mesh.ARRAY_VERTEX]
					if positions is PackedVector3Array:
						vertices += (positions as PackedVector3Array).size()
		elif node is Skeleton3D:
			bones = maxi(bones, (node as Skeleton3D).get_bone_count())
		elif node is AnimationPlayer:
			for library_name in (node as AnimationPlayer).get_animation_library_list():
				var library := (node as AnimationPlayer).get_animation_library(library_name)
				for clip_name in library.get_animation_list():
					clips.append(String(clip_name))

	print("GLB_PROBE file=%s surfaces=%d vertices=%d bones=%d clips=%d" % [
		name, surfaces, vertices, bones, clips.size(),
	])
	if pivot_only.has(name):
		_check("pivot_only_is_geometry_free:%s" % name, surfaces == 0 and vertices == 0,
			"surfaces=%d vertices=%d" % [surfaces, vertices])
		_check("pivot_only_keeps_rig:%s" % name, bones > 0, "bones=%d" % bones)
		scene.free()
		return

	_check("has_geometry:%s" % name, surfaces > 0 and vertices > 0,
		"surfaces=%d vertices=%d" % [surfaces, vertices])
	if require_rig:
		_check("has_skeleton:%s" % name, bones > 0, "bones=%d" % bones)
		_check("has_animation_clips:%s" % name, clips.size() > 0,
			"clips=%d" % clips.size())
	scene.free()


func _descendants(node: Node) -> Array[Node]:
	var out: Array[Node] = [node]
	var index := 0
	while index < out.size():
		for child in out[index].get_children():
			out.append(child)
		index += 1
	return out


func _scan(directory: String, found: Array[String], depth: int) -> void:
	if depth > 8:
		return
	var dir := DirAccess.open(directory)
	if dir == null:
		return
	var subdirectories: Array[String] = []
	dir.list_dir_begin()
	var name := dir.get_next()
	while name != "":
		if name != "." and name != "..":
			var child := directory.path_join(name)
			if dir.current_is_dir():
				subdirectories.append(child)
			elif name.get_extension().to_lower() == "glb":
				found.append(child)
		name = dir.get_next()
	dir.list_dir_end()
	subdirectories.sort()
	for subdirectory in subdirectories:
		_scan(subdirectory, found, depth + 1)


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("GLB_PROBE PASS %s" % name)
	else:
		failed += 1
		printerr("GLB_PROBE FAIL %s%s" % [name, " (%s)" % detail if detail != "" else ""])


func _finish() -> void:
	print("GLB_PROBE_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)
