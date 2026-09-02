class_name NativeContentLocator
extends RefCounted
## Resolve native-core documents from explicit launch overrides or the launcher's
## durable native selection. Pack selection.json remains a separate contract.

const SELECTION_SCHEMA := "openbfme.native-selection"
const SELECTION_VERSION := 1


static func resolve(preferred_map_path: String = "") -> Dictionary:
	var bundle := OS.get_environment("OPENBFME_BUNDLE").strip_edges()
	var map := OS.get_environment("OPENBFME_MAP").strip_edges()
	var bundle_source := "OPENBFME_BUNDLE" if not bundle.is_empty() else ""
	var map_source := "OPENBFME_MAP" if not map.is_empty() else ""
	var content_root := _content_root()
	var selection_path := content_root.path_join("native/selection.json")
	if bundle.is_empty() or map.is_empty():
		var selection := _read_selection(selection_path)
		if not selection.is_empty():
			if bundle.is_empty():
				bundle = _selected_path(content_root, String(selection.get("bundle", "")))
				bundle_source = "native-selection"
			if map.is_empty():
				var maps: Array = selection.get("maps", []) as Array
				var selected := _select_map(maps, preferred_map_path)
				if not selected.is_empty():
					map = _selected_path(content_root, String(selected.get("path", "")))
					map_source = "native-selection"
	print("NATIVE_CONTENT_LOCATOR bundle source=%s path=%s" % [
		bundle_source if not bundle_source.is_empty() else "unresolved", bundle])
	print("NATIVE_CONTENT_LOCATOR map source=%s path=%s" % [
		map_source if not map_source.is_empty() else "unresolved", map])
	return {
		"bundle": bundle,
		"map": map,
		"bundle_source": bundle_source,
		"map_source": map_source,
		"content_root": content_root,
		"selection": selection_path,
	}


static func _select_map(maps: Array, preferred_map_path: String) -> Dictionary:
	if maps.is_empty():
		return {}
	var preferred_slug := _map_slug(preferred_map_path)
	if not preferred_slug.is_empty():
		for value in maps:
			if value is Dictionary and String((value as Dictionary).get("slug", "")) == preferred_slug:
				return value as Dictionary
	for value in maps:
		if value is Dictionary:
			return value as Dictionary
	return {}


static func _map_slug(path: String) -> String:
	var stem := path.replace("\\", "/").get_file().get_basename().to_lower()
	for prefix in ["map mp ", "map ", "mp "]:
		if stem.begins_with(prefix):
			stem = stem.trim_prefix(prefix)
			break
	var slug := ""
	var separator := false
	for character in stem:
		var code := character.unicode_at(0)
		if (code >= 97 and code <= 122) or (code >= 48 and code <= 57):
			slug += character
			separator = false
		elif not slug.is_empty() and not separator:
			slug += "-"
			separator = true
	return slug.trim_suffix("-")


static func _content_root() -> String:
	var configured := OS.get_environment("OPENBFME_CONTENT").strip_edges()
	if not configured.is_empty():
		return ProjectSettings.globalize_path(configured).simplify_path()
	return OS.get_executable_path().get_base_dir().path_join("content-packs").simplify_path()


static func _read_selection(path: String) -> Dictionary:
	if not FileAccess.file_exists(path):
		return {}
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null:
		return {}
	var value: Variant = JSON.parse_string(file.get_as_text())
	if not (value is Dictionary):
		return {}
	var document := value as Dictionary
	if (
		String(document.get("schema", "")) != SELECTION_SCHEMA
		or int(document.get("version", -1)) != SELECTION_VERSION
	):
		return {}
	return document


static func _selected_path(content_root: String, relative: String) -> String:
	var normalized := relative.replace("\\", "/").strip_edges()
	if normalized.is_empty() or normalized.is_absolute_path():
		return ""
	for part in normalized.split("/", false):
		if part == ".." or part == ".":
			return ""
	return content_root.path_join(normalized).simplify_path()
