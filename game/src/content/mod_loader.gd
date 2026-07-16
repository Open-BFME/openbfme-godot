extends Node
## Discovers repository packs, mods, and one durable user-owned content pack.
## Pack load order is deterministic: ascending priority, then normalized path.

const BASE_PATH := "res://data"
const RES_MODS := "res://mods"
const USER_MODS := "user://mods"
const USER_PACK_CACHE := "user://content-packs"
const USER_PACK_SELECTION := "user://content-packs/selection.json"
const PACK_CACHE_SETTING := "openbfme/content/user_pack_cache"
const PACK_SELECTION_SETTING := "openbfme/content/user_pack_selection"
const SELECTION_SCHEMA := "openbfme.pack-selection"
const SELECTION_VERSION := 0

var diagnostics: Array[String] = []


func list_pack_roots() -> Array[String]:
	diagnostics.clear()
	var roots: Array[String] = []
	_collect_packs(BASE_PATH, roots)
	_collect_packs(RES_MODS, roots)
	_ensure_dir(USER_MODS)
	_collect_packs(USER_MODS, roots)

	# Developer/CI override. This remains ephemeral; normal installs use the
	var external := OS.get_environment("OPENBFME_CONTENT")
	var external_selected := ""
	if external != "":
		if DirAccess.dir_exists_absolute(external):
			if is_valid_pack_root(external):
				# An explicit immutable bundle root is a complete selection, not
				# a supplement directory. It must suppress the durable user pack.
				external_selected = external
				roots.append(external_selected)
			else:
				# A private workspace mirrors the normal immutable cache layout:
				# selection.json points through <pack-id>/<bundle-hash>, while
				# immediate child packs may provide audited supplements. A strict
				# completion build is already a composed closure, so loading sibling
				# packs would let stale leaves override the selected bundle.
				var external_selection := external.path_join("selection.json")
				if FileAccess.file_exists(external_selection):
					external_selected = selected_user_pack_root(external, external_selection)
					if external_selected != "":
						roots.append(external_selected)
				if external_selected == "" or not _is_strict_completion_pack(external_selected):
					_collect_packs(external, roots)
		else:
			_diagnose("OPENBFME_CONTENT does not exist: %s" % external)

	# A generated retail bundle is opt-in. The explicit developer/CI selection
	# replaces the durable user selection so two versions of the same ruleset
	# cannot merge merely because both caches exist.
	_ensure_dir(user_pack_cache_root())
	if external_selected == "":
		var selected := selected_user_pack_root()
		if selected != "":
			roots.append(selected)

	var unique: Array[String] = []
	var seen: Dictionary = {}
	for root in roots:
		var key := _comparison_path(root)
		if not seen.has(key):
			seen[key] = true
			unique.append(root)
	unique.sort_custom(func(a: String, b: String) -> bool:
		var pa := _pack_priority(a)
		var pb := _pack_priority(b)
		if pa != pb:
			return pa < pb
		return _comparison_path(a) < _comparison_path(b)
	)
	return unique


func user_pack_cache_root() -> String:
	return String(ProjectSettings.get_setting(PACK_CACHE_SETTING, USER_PACK_CACHE))


func user_pack_selection_path() -> String:
	return String(ProjectSettings.get_setting(PACK_SELECTION_SETTING, USER_PACK_SELECTION))


func user_pack_cache_absolute() -> String:
	return _absolute_path(user_pack_cache_root())


func selected_user_pack_root(cache_root: String = "", selection_path: String = "") -> String:
	var cache := user_pack_cache_root() if cache_root == "" else cache_root
	var selection := user_pack_selection_path() if selection_path == "" else selection_path
	if not FileAccess.file_exists(selection):
		return ""
	var raw: Variant = _read_json(selection)
	if typeof(raw) != TYPE_DICTIONARY:
		_diagnose("Content selection is not a JSON object: %s" % selection)
		return ""
	var config := raw as Dictionary
	if String(config.get("schema", "")) != SELECTION_SCHEMA or int(config.get("schemaVersion", -1)) != SELECTION_VERSION:
		_diagnose("Unsupported content selection schema: %s" % selection)
		return ""
	var relative := String(config.get("activePack", ""))
	var selected := resolve_pack_path(cache, relative)
	if selected == "":
		_diagnose("Content selection has an unsafe activePack path: %s" % relative)
		return ""
	if not is_valid_pack_root(selected):
		_diagnose("Selected content pack is invalid or missing: %s" % selected)
		return ""
	return selected


func select_user_pack(relative_pack: String, cache_root: String = "", selection_path: String = "") -> String:
	## Persist a cache-relative selection. Returns an empty string on success.
	var cache := user_pack_cache_root() if cache_root == "" else cache_root
	var selection := user_pack_selection_path() if selection_path == "" else selection_path
	var selected := resolve_pack_path(cache, relative_pack)
	if selected == "":
		return "unsafe_active_pack_path"
	if not is_valid_pack_root(selected):
		return "invalid_pack_root"
	_ensure_dir(selection.get_base_dir())
	var file := FileAccess.open(selection, FileAccess.WRITE)
	if file == null:
		return "selection_write_failed"
	file.store_string(JSON.stringify({
		"schema": SELECTION_SCHEMA,
		"schemaVersion": SELECTION_VERSION,
		"activePack": relative_pack.replace("\\", "/"),
	}, "  ") + "\n")
	file.close()
	return ""


func is_valid_pack_root(pack_root: String) -> bool:
	var pack_path := resolve_pack_path(pack_root, "pack.json")
	if pack_path == "" or not FileAccess.file_exists(pack_path):
		return false
	var raw: Variant = _read_json(pack_path)
	if typeof(raw) != TYPE_DICTIONARY:
		return false
	var meta := raw as Dictionary
	if String(meta.get("id", "")).strip_edges() == "":
		return false
	if meta.has("schema"):
		if String(meta.get("schema", "")) != "openbfme.content-pack" or int(meta.get("schemaVersion", -1)) != 0:
			return false
		var policy: Variant = meta.get("dataPolicy", {})
		if typeof(policy) == TYPE_DICTIONARY and bool((policy as Dictionary).get("externalPathsAllowed", false)):
			return false
	return true


func is_safe_relative_path(relative_path: String) -> bool:
	if relative_path == "" or relative_path != relative_path.strip_edges():
		return false
	var normalized := relative_path.replace("\\", "/")
	if normalized.begins_with("/") or normalized.begins_with("~") or normalized.contains(":"):
		return false
	for segment in normalized.split("/", false):
		if segment == "" or segment == "." or segment == "..":
			return false
	return true


func resolve_pack_path(pack_root: String, relative_path: String) -> String:
	## Resolve a declared pack-relative path while enforcing lexical and physical
	## containment. A symlink/junction anywhere at or below the pack root is a
	## fail-closed boundary violation, even when its text still looks contained.
	if pack_root == "" or not is_safe_relative_path(relative_path):
		return ""
	var root := pack_root.replace("\\", "/").trim_suffix("/")
	var candidate := root.path_join(relative_path.replace("\\", "/")).simplify_path()
	if not path_is_within(root, candidate):
		return ""
	if _path_has_link_component(root, candidate):
		return ""
	return candidate


func path_is_within(root_path: String, candidate_path: String) -> bool:
	var root := _comparison_path(_absolute_path(root_path)).trim_suffix("/")
	var candidate := _comparison_path(_absolute_path(candidate_path))
	return candidate.begins_with(root + "/")


func _path_has_link_component(root_path: String, candidate_path: String) -> bool:
	var root := _absolute_path(root_path).trim_suffix("/")
	var candidate := _absolute_path(candidate_path)
	if not path_is_within(root, candidate):
		return true
	# Reject a pack/cache root that is itself a link or junction. Parents above
	# the configured root define the trusted storage location and are out of this
	# pack-relative boundary.
	var root_name := root.get_file()
	if root_name != "":
		var root_status := _link_status(root.get_base_dir(), root_name)
		if root_status != 0:
			return true
	var relative := candidate.substr(root.length() + 1)
	var current := root
	for segment in relative.split("/", false):
		var status := _link_status(current, segment)
		if status != 0:
			return true
		current = current.path_join(segment)
	return false


func _link_status(parent_path: String, child_name: String) -> int:
	## 0 = ordinary/missing entry, 1 = link/reparse point, -1 = unknown.
	## Unknown is rejected by callers so an unreadable parent cannot weaken the
	## containment boundary.
	var parent := DirAccess.open(parent_path)
	if parent == null:
		return -1
	return 1 if parent.is_link(child_name) else 0


func _ensure_dir(path: String) -> void:
	var absolute := _absolute_path(path)
	if not DirAccess.dir_exists_absolute(absolute):
		var err := DirAccess.make_dir_recursive_absolute(absolute)
		if err != OK:
			_diagnose("Could not create content directory %s (error %d)" % [absolute, err])


func _collect_packs(root: String, into: Array[String]) -> void:
	var dir := DirAccess.open(root)
	if dir == null:
		return
	if is_valid_pack_root(root):
		into.append(root)
		return
	dir.list_dir_begin()
	var name := dir.get_next()
	while name != "":
		if dir.current_is_dir() and not name.begins_with("."):
			var pack := root.path_join(name)
			if is_valid_pack_root(pack):
				into.append(pack)
		name = dir.get_next()
	dir.list_dir_end()


func _pack_priority(pack_root: String) -> int:
	var path := resolve_pack_path(pack_root, "pack.json")
	var data: Variant = _read_json(path)
	if typeof(data) == TYPE_DICTIONARY:
		return int((data as Dictionary).get("priority", 100))
	return 100


func _is_strict_completion_pack(pack_root: String) -> bool:
	var path := resolve_pack_path(pack_root, "pack.json")
	var data: Variant = _read_json(path)
	return (
		typeof(data) == TYPE_DICTIONARY
		and bool((data as Dictionary).get("profile_build_complete", false))
	)


func _read_json(path: String) -> Variant:
	if not FileAccess.file_exists(path):
		return null
	var file: FileAccess = FileAccess.open(path, FileAccess.READ)
	if file == null:
		return null
	var text: String = file.get_as_text()
	var json := JSON.new()
	if json.parse(text) != OK:
		push_error("JSON parse failed: %s @ %s" % [json.get_error_message(), path])
		return null
	return json.data


func _absolute_path(path: String) -> String:
	if path.begins_with("res://") or path.begins_with("user://"):
		return ProjectSettings.globalize_path(path).replace("\\", "/").simplify_path()
	return path.replace("\\", "/").simplify_path()


func _comparison_path(path: String) -> String:
	var normalized := path.replace("\\", "/").simplify_path()
	return normalized.to_lower() if OS.get_name() == "Windows" else normalized


func _diagnose(message: String) -> void:
	diagnostics.append(message)
	push_warning("[ModLoader] %s" % message)
