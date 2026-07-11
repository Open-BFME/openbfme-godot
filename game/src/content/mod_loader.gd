extends Node
## Discovers content packs under res://data and user://mods.
## Pack load order: base first, then mods sorted by pack.json priority.

const BASE_PATH := "res://data"
const RES_MODS := "res://mods"
const USER_MODS := "user://mods"

func list_pack_roots() -> Array[String]:
	var roots: Array[String] = []
	_collect_packs(BASE_PATH, roots)
	_collect_packs(RES_MODS, roots)
	_ensure_user_mods_dir()
	_collect_packs(USER_MODS, roots)
	# Also allow absolute external content via project setting / env
	var external := OS.get_environment("OPENBFME_CONTENT")
	if external != "" and DirAccess.dir_exists_absolute(external):
		_collect_packs(external, roots)
	roots.sort_custom(func(a: String, b: String) -> bool:
		return _pack_priority(a) < _pack_priority(b)
	)
	return roots

func _ensure_user_mods_dir() -> void:
	var abs := ProjectSettings.globalize_path(USER_MODS)
	if not DirAccess.dir_exists_absolute(abs):
		DirAccess.make_dir_recursive_absolute(abs)

func _collect_packs(root: String, into: Array[String]) -> void:
	if root.begins_with("res://") or root.begins_with("user://"):
		var dir := DirAccess.open(root)
		if dir == null:
			return
		dir.list_dir_begin()
		var name := dir.get_next()
		while name != "":
			if dir.current_is_dir() and not name.begins_with("."):
				var pack := root.path_join(name)
				if FileAccess.file_exists(pack.path_join("pack.json")):
					into.append(pack)
			name = dir.get_next()
		dir.list_dir_end()
	else:
		# absolute filesystem path
		var dir := DirAccess.open(root)
		if dir == null:
			return
		if FileAccess.file_exists(root.path_join("pack.json")):
			into.append(root)
			return
		dir.list_dir_begin()
		var name := dir.get_next()
		while name != "":
			if dir.current_is_dir() and not name.begins_with("."):
				var pack := root.path_join(name)
				if FileAccess.file_exists(pack.path_join("pack.json")):
					into.append(pack)
			name = dir.get_next()
		dir.list_dir_end()

func _pack_priority(pack_root: String) -> int:
	var path := pack_root.path_join("pack.json")
	var data: Variant = _read_json(path)
	if typeof(data) == TYPE_DICTIONARY:
		return int((data as Dictionary).get("priority", 100))
	return 100

func _read_json(path: String) -> Variant:
	if not FileAccess.file_exists(path):
		return null
	var f: FileAccess = FileAccess.open(path, FileAccess.READ)
	if f == null:
		return null
	var txt: String = f.get_as_text()
	var json := JSON.new()
	if json.parse(txt) != OK:
		push_error("JSON parse failed: %s @ %s" % [json.get_error_message(), path])
		return null
	return json.data
