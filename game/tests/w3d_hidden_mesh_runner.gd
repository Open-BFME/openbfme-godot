extends SceneTree
## Retail W3D HIDDEN meshes must not render.
##
## Owner playtest 2026-08-26: the Gondor fortress trebuchet towers and the
## castle wall towers showed FLAT UNTEXTURED GRAY TOPS. Those gray caps are the
## W3D placeholder meshes P1 / R1 / R2 (build-plot pad and ramp reference
## surfaces): retail authors them with the per-mesh HIDDEN attribute (and a
## shader with color writes disabled), so the SAGE renderer never draws them.
## The converter faithfully preserves the flag as GLB node extras
## (`{"hidden": true}`) and Godot imports node extras as node metadata, but the
## runtime never consumed it — every pad rendered as an opaque gray polygon.
##
## AssetFactory.hide_w3d_hidden_meshes() now honors the authored flag at GLB
## load. This runner loads the shipped pack models straight through the
## factory's load path and asserts the authored-hidden meshes are invisible
## while the real tower geometry stays visible.

# Loaded lazily in _run(): asset_factory references autoload singletons at
# class scope and cannot compile before the autoloads register.
var AssetFactoryScript: GDScript = null

## Relative pack paths -> {owning structure, hidden mesh names, visible mesh
## names} per the GLB's own authored extras (verified against the selected
## rotwk-men pack). Resolution goes through the OWNING STRUCTURE's pack root —
## the same root the slice presents from — because a supplemental pack
## (bfme2-men-vslice) still ships pre-hidden-extras conversions of the same
## relative paths and a first-pack-wins probe would test the wrong file.
const CASES := {
	"assets/models/structures/mentrebuchetexpansion/intact-damaged-gbftrtowa.glb": {
		"structure": "MenTrebuchetExpansion",
		"hidden": ["P1"],
		"visible": ["GBFTRTOWA"],
	},
	"assets/models/structures/gondorcastlewalltower/intact-gbwalltwr.glb": {
		"structure": "GondorCastleWallTower",
		"hidden": ["P1", "R1", "R2"],
		"visible": ["CYLINDER01"],
	},
}

var passed := 0
var failed := 0

const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
var _runner_watchdog := RunnerWatchdogScript.new()


func _initialize() -> void:
	_runner_watchdog.start(self, "W3D_HIDDEN_MESH_RUNNER")
	call_deferred("_run")


func _run() -> void:
	await process_frame
	await process_frame
	var mod_loader = root.get_node_or_null("ModLoader")
	if not _check("mod_loader_available", mod_loader != null):
		_finish()
		return
	AssetFactoryScript = load("res://src/view/asset_factory.gd")
	if not _check("asset_factory_compiles", AssetFactoryScript != null and AssetFactoryScript.can_instantiate()):
		_finish()
		return
	var content_db = root.get_node_or_null("ContentDB")
	if not _check("content_db_available", content_db != null):
		_finish()
		return
	for relative in CASES.keys():
		var case_row := CASES[relative] as Dictionary
		var absolute := _resolve_from_owning_structure(content_db, mod_loader, String(case_row.get("structure", "")), String(relative))
		if not _check("pack_ships_%s" % String(relative).get_file(), absolute != "", "owning structure's pack does not carry %s" % relative):
			continue
		var node: Node3D = AssetFactoryScript._try_load_model(absolute)
		if not _check("loads_%s" % String(relative).get_file(), node != null):
			continue
		var case := CASES[relative] as Dictionary
		for hidden_name in case.get("hidden", []) as Array:
			var mesh := _find_mesh(node, String(hidden_name))
			if not _check("%s_has_mesh_%s" % [String(relative).get_file(), hidden_name], mesh != null):
				continue
			_check(
				"%s_%s_is_hidden" % [String(relative).get_file(), hidden_name],
				not mesh.visible,
				"authored W3D hidden mesh renders (the flat gray tower cap)"
			)
		for visible_name in case.get("visible", []) as Array:
			var mesh := _find_mesh(node, String(visible_name))
			if not _check("%s_has_mesh_%s" % [String(relative).get_file(), visible_name], mesh != null):
				continue
			_check(
				"%s_%s_stays_visible" % [String(relative).get_file(), visible_name],
				mesh.visible,
				"real tower geometry must not be swept up by the hidden-flag pass"
			)
		# The cache hands out duplicates; the hidden state must survive them.
		var duplicate: Node3D = AssetFactoryScript._try_load_model(absolute)
		if duplicate != null:
			for hidden_name in case.get("hidden", []) as Array:
				var mesh := _find_mesh(duplicate, String(hidden_name))
				_check(
					"%s_%s_hidden_survives_cache_duplicate" % [String(relative).get_file(), hidden_name],
					mesh != null and not mesh.visible
				)
			duplicate.free()
		node.free()
	_finish()


func _finish() -> void:
	print("W3D_HIDDEN_MESH_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


func _resolve_from_owning_structure(content_db, mod_loader, structure_object_id: String, relative: String) -> String:
	## The pack root the slice actually presents this structure from.
	var document: Dictionary = content_db.get_playable_structure_runtime(structure_object_id)
	var pack_root := String(document.get("_pack_root", ""))
	if pack_root == "":
		return ""
	var candidate := String(mod_loader.resolve_pack_path(pack_root, relative))
	if candidate != "" and FileAccess.file_exists(candidate):
		return candidate
	return ""


func _find_mesh(node: Node, mesh_name: String) -> Node3D:
	var stack: Array[Node] = [node]
	while not stack.is_empty():
		var current: Node = stack.pop_back()
		for child in current.get_children():
			stack.append(child)
		if current is MeshInstance3D and String(current.name).to_upper().begins_with(mesh_name.to_upper()):
			return current as Node3D
	return null


func _check(name: String, condition: bool, detail: String = "") -> bool:
	if condition:
		passed += 1
		print("W3D_HIDDEN_MESH PASS %s" % name)
	else:
		failed += 1
		printerr("W3D_HIDDEN_MESH FAIL %s%s" % [name, " (%s)" % detail if detail != "" else ""])
	return condition
