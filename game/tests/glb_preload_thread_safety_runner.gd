extends SceneTree
## GLB batch-preload thread-safety gate.
##
## AssetFactory.preload_models_threaded fans GLTFDocument.append_from_file out
## across the worker pool. That parse does not only read geometry: it builds the
## ImageTextures for the GLB's materials, and building an ImageTexture calls
## into RenderingServer's texture storage.
##
## Headless Godot runs the DUMMY renderer, whose texture storage owns its
## textures in a plain (non-thread-safe) RID owner:
##
##     servers/rendering/dummy/storage/texture_storage.h
##       RID_PtrOwner<DummyTexture> texture_owner;   // no thread-safe flag
##       virtual void texture_2d_initialize(RID p_texture, ...) {
##           DummyTexture *t = texture_owner.get_or_null(p_texture);
##           ERR_FAIL_NULL(t);
##
## Two workers allocating a texture at the same moment race that owner, the RID
## one of them was handed is never registered, and the follow-up initialize
## reports `Parameter "t" is null.` at texture_storage.h:85. The prop still
## loads — with a texture that was never initialized. That is the exact failure
## observed intermittently in the Fords boot profiles and in four other runner
## logs under workspace/scratch, and it is a silent-wrong-content bug, not a
## cosmetic error line.
##
## This runner is the stress harness for it: it collects real GLB paths from the
## mounted packs and drives the batch preload for many iterations with a cold
## cache each time, so the race gets many chances to fire in one process. The
## checks below cover what a script CAN see (every requested path warmed, no
## node leaked); the engine-level proof is the absence of the null-parameter
## error in this process's stderr across the whole loop.
##
## Iterations default to 30 and are overridable with
## OPENBFME_GLB_PRELOAD_ITERATIONS for longer soaks.

## Loaded lazily inside _run, never preloaded at top level: AssetFactory reaches
## the ContentDB autoload, and a --script runner's own script compiles before the
## autoloads exist. Same rule the sibling runners follow.
var AssetFactoryScript = null

const DEFAULT_ITERATIONS := 30
const MAX_PATHS := 24
const MIN_PATHS := 2

var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var content_db := root.get_node_or_null("ContentDB")
	AssetFactoryScript = load("res://src/view/asset_factory.gd")
	_check("content_db_present", content_db != null)
	_check("asset_factory_parses", AssetFactoryScript != null)
	if content_db == null or AssetFactoryScript == null:
		return _finish()

	var paths := _collect_glb_paths(content_db)
	# A one-path batch takes the serial branch by design, so it would prove
	# nothing about the threaded one. Say so rather than passing vacuously.
	_check("mounted_packs_supply_a_multi_glb_batch", paths.size() >= MIN_PATHS,
		"found %d glb paths" % paths.size())
	if paths.size() < MIN_PATHS:
		return _finish()
	print("GLB_PRELOAD corpus paths=%d first=%s" % [paths.size(), paths[0]])

	# The policy this gate exists to hold: where the renderer's texture storage
	# is not thread-safe, the batch preload must not dispatch to the worker pool.
	var parallel_supported := bool(AssetFactoryScript.parallel_glb_parse_supported())
	var headless := DisplayServer.get_name() == "headless"
	_check("parallel_parse_refused_under_headless_renderer", parallel_supported != headless,
		"parallel_supported=%s headless=%s display_server=%s" % [
			str(parallel_supported), str(headless), DisplayServer.get_name(),
		])

	var iterations := DEFAULT_ITERATIONS
	var override := OS.get_environment("OPENBFME_GLB_PRELOAD_ITERATIONS")
	if override != "" and override.is_valid_int() and int(override) > 0:
		iterations = int(override)

	var started := Time.get_ticks_msec()
	var warmed_every_iteration := true
	var worst_missing := ""
	for iteration in range(iterations):
		AssetFactoryScript.clear_mesh_cache()
		AssetFactoryScript.preload_models_threaded(paths)
		for path in paths:
			if not AssetFactoryScript.has_cached_model(path):
				warmed_every_iteration = false
				if worst_missing == "":
					worst_missing = "iteration %d did not warm %s" % [iteration, path]
		if iteration % 5 == 0:
			print("GLB_PRELOAD iteration=%d cached=%d orphans=%d" % [
				iteration,
				AssetFactoryScript.mesh_cache_size(),
				int(Performance.get_monitor(Performance.OBJECT_ORPHAN_NODE_COUNT)),
			])
	var elapsed := Time.get_ticks_msec() - started

	_check("every_path_warmed_every_iteration", warmed_every_iteration, worst_missing)

	# The batch preload keeps ONE cached node per path. Anything else means the
	# generated originals are being dropped on the floor: Node is not
	# reference-counted, so a dropped one lives until the process exits.
	var orphans_with_cache := int(Performance.get_monitor(Performance.OBJECT_ORPHAN_NODE_COUNT))
	AssetFactoryScript.clear_mesh_cache()
	await process_frame
	var orphans_after_clear := int(Performance.get_monitor(Performance.OBJECT_ORPHAN_NODE_COUNT))
	print("GLB_PRELOAD iterations=%d elapsed_ms=%d orphans_with_cache=%d orphans_after_clear=%d" % [
		iterations, elapsed, orphans_with_cache, orphans_after_clear,
	])
	# Clearing the cache must give back everything the preload allocated: what
	# remains is what the loop leaked, and after N iterations a per-iteration
	# leak is N times as loud as a one-off.
	_check("cleared_cache_leaves_no_preload_orphans", orphans_after_clear == 0,
		"orphans=%d after %d iterations of %d paths" % [orphans_after_clear, iterations, paths.size()])

	_finish()


## Real GLB files from the mounted packs, deterministic order, capped. Uses the
## pack roots ContentDB actually mounted rather than a hard-coded digest path,
## so this runner follows the operator's selection like the game does.
func _collect_glb_paths(content_db: Node) -> Array[String]:
	var found: Array[String] = []
	var pack_meta: Array = content_db.get("pack_meta") as Array
	print("GLB_PRELOAD mounted_packs=%d" % pack_meta.size())
	for entry_value in pack_meta:
		var root_path := String((entry_value as Dictionary).get("root", ""))
		if root_path == "":
			continue
		# res:// GLBs are engine-imported: preload_models_threaded hands those to
		# ResourceLoader and never reaches the worker pool. The threaded branch
		# only ever sees pack files outside res://, so those are the corpus.
		if root_path.begins_with("res://"):
			continue
		var before := found.size()
		_scan_for_glb(root_path, found, 0)
		print("GLB_PRELOAD pack_root=%s glb+%d" % [root_path, found.size() - before])
		if found.size() >= MAX_PATHS:
			break
	found.sort()
	return found


func _scan_for_glb(directory: String, found: Array[String], depth: int) -> void:
	if depth > 6 or found.size() >= MAX_PATHS:
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
			elif name.get_extension().to_lower() == "glb" and found.size() < MAX_PATHS:
				found.append(child)
		name = dir.get_next()
	dir.list_dir_end()
	subdirectories.sort()
	for subdirectory in subdirectories:
		_scan_for_glb(subdirectory, found, depth + 1)


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("GLB_PRELOAD PASS %s" % name)
	else:
		failed += 1
		printerr("GLB_PRELOAD FAIL %s%s" % [name, " (%s)" % detail if detail != "" else ""])


func _finish() -> void:
	print("GLB_PRELOAD_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)
