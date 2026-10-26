@tool
extends EditorExportPlugin
## Lane REPRO-1: which files of the launcher project go into its pack, decided in an order that does not depend on the host.
##
## Why not the preset's include_filter / exclude_filter: Godot 4.7.2 applies both with DirAccess in the file system's raw directory order
## (editor/export/editor_export_platform.cpp, _edit_files_with_filter: list_dir_begin / get_next, unsorted) on the HashSet of paths that
## export_project_files then writes in iteration order. That HashSet iterates in insertion order and its erase moves the last key into
## the hole (core/templates/hash_set.h), so the order of the inserts (include_filter) and of the erases (exclude_filter "tests/*") is the
## order of the files in the pack. On ext4 the directory order is the htree hash order, seeded per file system: the same commit exported
## on the reference PC and on the Deck put build_info.json and the font licences at different offsets, and v0.3.0-preview.5's launcher packages
## differed (REPRO-1). Resources come from EditorFileSystem, which sorts; with both filters empty nothing reorders them, so:
## - SKIP: resources that must not ship are dropped per file in _export_file (skip() leaves the HashSet alone);
## - ADD: non-resource files (type TextFile, which _export_find_resources leaves out) are added in _export_begin in sorted path order;
##   Godot saves them before the resources, in add_file order (export_project_files, add_shared_objects_and_extra_files_from_export_plugins).
## A file of ADD that EditorFileSystem exports as a resource (build_info.json: a JSON resource in 4.7) is left to the export, so it is in
## the pack once.

## Godot's filter syntax (String.matchn on the path below res://)
## (release_key.pub and release_keys.pub are CryptoKey resources by their extension; the keys are compiled into build_info.json)
const SKIP: PackedStringArray = ["tests/*", "addons/*", "release_key.pub", "release_keys.pub"]
## each must match at least one file of the project
const ADD: PackedStringArray = ["build_info.json", "fonts/OFL-*.txt"]

## build_launcher.sh fails the export on a line holding this
const ERROR_PREFIX := "PACK FILES FAIL: "


func _get_name() -> String:
	return "OpenBFMEPackFiles"


func _export_begin(_features: PackedStringArray, _is_debug: bool, _path: String, _flags: int) -> void:
	var files := PackedStringArray()
	_collect("res://", files)
	for pattern in ADD:
		if not _matches_any(files, pattern):
			push_error(ERROR_PREFIX + "no file matches %s" % pattern)
	var efs := EditorInterface.get_resource_filesystem()
	for f in files:
		if not _matches(f, ADD) or _matches(f, SKIP):
			continue
		var type := efs.get_file_type(f)
		if type != "" and type != "TextFile":
			continue  # a resource: the export takes it, in EditorFileSystem's order
		var data := FileAccess.get_file_as_bytes(f)
		if FileAccess.get_open_error() != OK:
			push_error(ERROR_PREFIX + "cannot read %s (error %d)" % [f, FileAccess.get_open_error()])
			continue
		add_file(f, data, false)


func _export_file(path: String, _type: String, _features: PackedStringArray) -> void:
	if _matches(path, SKIP):
		skip()


func _matches_any(files: PackedStringArray, pattern: String) -> bool:
	for f in files:
		if f.trim_prefix("res://").matchn(pattern):
			return true
	return false


func _matches(path: String, patterns: PackedStringArray) -> bool:
	var rel := path.trim_prefix("res://")
	for pattern in patterns:
		if rel.matchn(pattern):
			return true
	return false


## every file below `dir`, its own files in sorted order, then its subdirectories in sorted order (names starting with "." skipped, like
## Godot's filter walk)
func _collect(dir: String, out: PackedStringArray) -> void:
	var da := DirAccess.open(dir)
	if da == null:
		push_error(ERROR_PREFIX + "cannot list %s (error %d)" % [dir, DirAccess.get_open_error()])
		return
	var names := da.get_files()
	names.sort()
	for n in names:
		out.append(dir.path_join(n))
	var dirs := da.get_directories()
	dirs.sort()
	for d in dirs:
		if not d.begins_with("."):
			_collect(dir.path_join(d), out)
