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

# Boot-path memoization. _link_status performs a DirAccess.open per call, and
# boot-time asset resolution calls it for every path segment of every resolved
# asset (tens of thousands of opens on large retail pack sets). Link status of
# the immutable pack trees cannot change while a load generation is active, so
# results are memoized per (parent, child) and flushed whenever the pack set is
# re-scanned (list_pack_roots) or the selection is rewritten (select_user_pack).
var _link_status_cache: Dictionary = {}
var _link_cache_mutex := Mutex.new()


func clear_path_caches() -> void:
	_link_cache_mutex.lock()
	_link_status_cache.clear()
	_link_cache_mutex.unlock()


func list_pack_roots() -> Array[String]:
	diagnostics.clear()
	clear_path_caches()
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
						roots.append_array(selected_pack_supplements(external, external_selection))
						# Explicit selection is a complete load set (active + named
						# supplements). Never auto-mount sibling packs — stale
						# leaves (e.g. private-leaves UI) would override the
						# selected bundle's registries and fail pack-root gates.
					else:
						_collect_packs(external, roots)
				else:
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
			# LOUD ON PURPOSE. This is the durable pack in the user data
			# directory, reached only because OPENBFME_CONTENT named nothing
			# usable. It can be months old while the repository's real pack has
			# moved on, and a stale pack producing confidently wrong results
			# that pass review is a recorded failure in this project - the
			# reason run_worktree_game.bat exists at all.
			#
			# Until now this path said nothing. The only message was the
			# upstream "OPENBFME_CONTENT does not exist", which does not name
			# what replaced it, and push_warning alone is invisible when the
			# windowed Godot binary is launched. So this also prints to stdout,
			# which reaches the console binary and the user:// log either way.
			var age := _pack_age_description(selected)
			var notice := (
				"Falling back to the DURABLE pack in user data because "
				+ "OPENBFME_CONTENT selected nothing: %s%s. " % [selected, age]
				+ "If you are testing repository content, set OPENBFME_CONTENT "
				+ "to the pack root (see run_worktree_game.bat)."
			)
			_diagnose(notice)
			print("[ModLoader] %s" % notice)
			roots.append(selected)
			roots.append_array(selected_pack_supplements())

	var unique: Array[String] = []
	var seen: Dictionary = {}
	for root in roots:
		var key := _comparison_path(root)
		if not seen.has(key):
			seen[key] = true
			unique.append(root)
	# The active selection must load last among equal priorities: faction packs
	# ship copies of the shared base bundle objects, and the active pack's
	# documents (not a supplement's copy) must win those shared ids.
	var active_key := ""
	if external_selected != "":
		active_key = _comparison_path(external_selected)
	elif external_selected == "":
		var active_user_root := selected_user_pack_root()
		if active_user_root != "":
			active_key = _comparison_path(active_user_root)
	unique.sort_custom(func(a: String, b: String) -> bool:
		var pa := _pack_priority(a)
		var pb := _pack_priority(b)
		if pa != pb:
			return pa < pb
		var a_active := active_key != "" and _comparison_path(a) == active_key
		var b_active := active_key != "" and _comparison_path(b) == active_key
		if a_active != b_active:
			return not a_active
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


func selected_pack_supplements(cache_root: String = "", selection_path: String = "") -> Array[String]:
	## Optional explicit supplement bundles named by the selection document.
	## Each entry is a cache-relative <pack-id>/<bundle-hash> root resolved
	## under the same containment and link rules as the active pack. This is
	## how a converted faction pack loads alongside the selected host bundle
	## without scanning siblings; invalid entries fail closed: they are
	## diagnosed and skipped, never searched for.
	var supplements: Array[String] = []
	var cache := user_pack_cache_root() if cache_root == "" else cache_root
	var selection := user_pack_selection_path() if selection_path == "" else selection_path
	if not FileAccess.file_exists(selection):
		return supplements
	var raw: Variant = _read_json(selection)
	if typeof(raw) != TYPE_DICTIONARY:
		return supplements
	var config := raw as Dictionary
	if String(config.get("schema", "")) != SELECTION_SCHEMA or int(config.get("schemaVersion", -1)) != SELECTION_VERSION:
		return supplements
	var entries: Variant = config.get("supplementalPacks", [])
	if typeof(entries) != TYPE_ARRAY:
		_diagnose("Content selection supplementalPacks is not an array: %s" % selection)
		return supplements
	for entry_value in entries as Array:
		var relative := String(entry_value)
		var supplement := resolve_pack_path(cache, relative)
		if supplement == "":
			_diagnose("Content selection has an unsafe supplementalPacks path: %s" % relative)
			continue
		if not is_valid_pack_root(supplement):
			_diagnose("Supplemental content pack is invalid or missing: %s" % supplement)
			continue
		supplements.append(supplement)
	return supplements


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
	clear_path_caches()
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
	# EXPORTED BUILDS: a res:// path lives inside the .pck, not on disk, so
	# DirAccess.open(globalize_path(...)) returns null and the link probe
	# answers "yes, links" for every bundled root - rejecting the game's OWN
	# base packs as if they were hostile symlinked content. That is exactly
	# what made an exported build report packs=2 units=0 factions=0 while the
	# .pck demonstrably contained all of it: game/data/base carries 33 unit
	# and 4 faction documents, the precise counts a working build reports.
	#
	# Bundled resources cannot contain links, so the probe is skipped for
	# them. Every user:// and absolute external path is still fully checked -
	# the boundary this guard exists to defend is unchanged.
	if not root.begins_with("res://") and _path_has_link_component(root, candidate):
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
	## containment boundary. The cache is mutex-guarded because threaded content
	## loaders resolve assets concurrently.
	var cache_key := parent_path + "|" + child_name
	_link_cache_mutex.lock()
	var cached: Variant = _link_status_cache.get(cache_key)
	_link_cache_mutex.unlock()
	if cached is int:
		return cached as int
	var parent := DirAccess.open(parent_path)
	var status := 0
	if parent == null:
		status = -1
	else:
		status = 1 if parent.is_link(child_name) else 0
	_link_cache_mutex.lock()
	_link_status_cache[cache_key] = status
	_link_cache_mutex.unlock()
	return status


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


## Human-readable age of a pack root, for the durable-fallback notice. The age
## is the point: "stale pack" is only actionable if you can see HOW stale.
## Returns "" when the timestamp cannot be read, rather than inventing one.
func _pack_age_description(pack_root: String) -> String:
	var manifest := pack_root.path_join("pack.json")
	if not FileAccess.file_exists(manifest):
		return ""
	var modified := FileAccess.get_modified_time(manifest)
	if modified <= 0:
		return ""
	var age_seconds := int(Time.get_unix_time_from_system()) - int(modified)
	if age_seconds < 0:
		return ""
	var days := age_seconds / 86400
	var stamp := Time.get_datetime_string_from_unix_time(int(modified), true)
	if days >= 1:
		return " (built %s, %d day%s ago)" % [stamp, days, "" if days == 1 else "s"]
	return " (built %s, today)" % stamp


func _diagnose(message: String) -> void:
	diagnostics.append(message)
	push_warning("[ModLoader] %s" % message)
