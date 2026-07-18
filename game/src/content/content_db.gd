extends Node
## Loads JSON content packs. Mods override by id (later packs win).

const MAX_MAP_CATALOG_ROWS := 256
const MAX_MAP_CATALOG_BYTES := 1024 * 1024
const MAX_COOKED_MAP_BYTES := 2 * 1024 * 1024
const MAX_MAP_CATALOG_ROW_FIELDS := 64
const MAX_COOKED_MAP_ROOT_FIELDS := 128
const MAX_MAP_ID_LENGTH := 256
const MAX_MAP_PATH_LENGTH := 1024

var units: Dictionary = {}
var buildings: Dictionary = {}
var factions: Dictionary = {}
var abilities: Dictionary = {}
var powers: Dictionary = {}
var research: Dictionary = {}
var maps: Dictionary = {}
var bundle_objects: Dictionary = {}
var retail_unit_rules: Dictionary = {}
var ranger_runtime: Dictionary = {}
var animation_capabilities: Dictionary = {}
var bundle_maps: Dictionary = {}
var retail_ui_images: Dictionary = {}
var retail_strings: Dictionary = {}
var retail_audio_events: Dictionary = {}
var retail_audio_multisounds: Dictionary = {}
var retail_audio_samples: Dictionary = {}
var globals: Dictionary = {}
var damage_matrix: Dictionary = {}
var pack_meta: Array = []
var pack_roots: Array[String] = []
var asset_roots: Array[String] = []

func _ready() -> void:
	reload()

func reload() -> void:
	# Cached GLTF scene roots are keyed by pack path. Invalidate them before the
	# catalog changes so immutable versioned retail packs cannot accumulate
	# orphan scene roots over a long-running content-selection session.
	var asset_factory = load("res://src/view/asset_factory.gd")
	if asset_factory:
		asset_factory.clear_mesh_cache()
	units.clear()
	buildings.clear()
	factions.clear()
	abilities.clear()
	powers.clear()
	research.clear()
	maps.clear()
	bundle_objects.clear()
	retail_unit_rules.clear()
	ranger_runtime.clear()
	animation_capabilities.clear()
	bundle_maps.clear()
	retail_ui_images.clear()
	retail_strings.clear()
	retail_audio_events.clear()
	retail_audio_multisounds.clear()
	retail_audio_samples.clear()
	globals.clear()
	damage_matrix.clear()
	pack_meta.clear()
	pack_roots.clear()
	asset_roots.clear()
	for root in ModLoader.list_pack_roots():
		_load_pack(root)
	Events.content_reloaded.emit()
	print("[ContentDB] packs=%d units=%d buildings=%d factions=%d powers=%d research=%d maps=%d" % [
		pack_meta.size(), units.size(), buildings.size(), factions.size(), powers.size(), research.size(), maps.size()
	])

func _load_pack(root: String) -> void:
	var pack_path := ModLoader.resolve_pack_path(root, "pack.json")
	var raw: Variant = ModLoader._read_json(pack_path)
	var meta: Dictionary = {}
	if typeof(raw) == TYPE_DICTIONARY:
		meta = (raw as Dictionary).duplicate(true)
	else:
		meta = {"id": root.get_file(), "priority": 100}
	meta["root"] = root
	pack_meta.append(meta)
	pack_roots.append(root)
	asset_roots.append(ModLoader.resolve_pack_path(root, "assets"))
	_load_dir_into(ModLoader.resolve_pack_path(root, "units"), units, root)
	_load_dir_into(ModLoader.resolve_pack_path(root, "buildings"), buildings, root)
	_load_dir_into(ModLoader.resolve_pack_path(root, "factions"), factions, root)
	_load_dir_into(ModLoader.resolve_pack_path(root, "abilities"), abilities, root)
	_load_dir_into(ModLoader.resolve_pack_path(root, "powers"), powers, root)
	_load_dir_into(ModLoader.resolve_pack_path(root, "research"), research, root)
	_load_dir_into(ModLoader.resolve_pack_path(root, "maps"), maps, root)
	_load_bundle_v0(root, meta)
	var gpath := ModLoader.resolve_pack_path(root, "globals.json")
	if FileAccess.file_exists(gpath):
		var g: Variant = ModLoader._read_json(gpath)
		if typeof(g) == TYPE_DICTIONARY:
			globals.merge(g as Dictionary, true)
	var mpath := ModLoader.resolve_pack_path(root, "damage_matrix.json")
	if FileAccess.file_exists(mpath):
		var m: Variant = ModLoader._read_json(mpath)
		if typeof(m) == TYPE_DICTIONARY:
			damage_matrix = m as Dictionary

func _load_dir_into(dir_path: String, table: Dictionary, pack_root: String) -> void:
	var dir := DirAccess.open(dir_path)
	if dir == null:
		return
	dir.list_dir_begin()
	var fname: String = dir.get_next()
	while fname != "":
		if not dir.current_is_dir() and fname.ends_with(".json"):
			var full := dir_path.path_join(fname)
			var data: Variant = ModLoader._read_json(full)
			if typeof(data) == TYPE_DICTIONARY:
				var d: Dictionary = data
				var id := String(d.get("id", fname.get_basename()))
				d["id"] = id
				d["_source"] = full
				d["_pack_root"] = pack_root
				if table.has(id):
					var merged: Dictionary = (table[id] as Dictionary).duplicate(true)
					merged.merge(d, true)
					table[id] = merged
				else:
					table[id] = d
		fname = dir.get_next()
	dir.list_dir_end()


func _load_bundle_v0(root: String, meta: Dictionary) -> void:
	## Index the plan's consolidated v0 documents for the presentation bridge.
	## The legacy match catalogs above remain separate until the authoritative
	## simulation consumes the v0 schema directly.
	if String(meta.get("schema", "")) != "openbfme.content-pack" or int(meta.get("schemaVersion", -1)) != 0:
		return
	var files: Variant = meta.get("files", {})
	if typeof(files) != TYPE_DICTIONARY:
		return
	var declared := files as Dictionary
	_load_declared_rows(root, String(declared.get("objects", "")), "objects", bundle_objects)
	_load_retail_unit_rules(root, String(declared.get("unitRules", "")))
	_load_ranger_runtime(root, String(declared.get("rangerRuntime", "")))
	_load_declared_rows(root, String(declared.get("animationCapabilities", "")), "capabilities", animation_capabilities)
	_load_retail_ui_manifest(root, String(declared.get("uiManifest", "")))
	_load_retail_strings(root, String(declared.get("strings", "")))
	_load_retail_audio_manifest(root, String(declared.get("audioEvents", "")))
	for key in ["entryMap", "stage2Map"]:
		var relative := String(declared.get(key, ""))
		var map_doc := _read_declared_document(root, relative)
		if not map_doc.is_empty():
			var map_id := String(map_doc.get("id", ""))
			if map_id != "":
				map_doc["_source"] = ModLoader.resolve_pack_path(root, relative)
				map_doc["_pack_root"] = root
				bundle_maps[map_id] = map_doc
	_load_map_catalog(root, String(declared.get("mapCatalog", "")))


func _load_map_catalog(root: String, relative: String) -> bool:
	## Catalog loading is atomic. A malformed declaration cannot partially add
	## maps before a later unsafe or incompatible row is discovered.
	if relative == "":
		return false
	var document := _read_declared_document_bounded(root, relative, MAX_MAP_CATALOG_BYTES)
	if String(document.get("schema", "")) != "openbfme.map-catalog" or int(document.get("schemaVersion", -1)) != 0:
		return false
	var values: Variant = document.get("maps", null)
	if typeof(values) != TYPE_ARRAY:
		return false
	var rows := values as Array
	if rows.is_empty() or rows.size() > MAX_MAP_CATALOG_ROWS:
		return false

	var pending: Dictionary = {}
	for value in rows:
		if typeof(value) != TYPE_DICTIONARY:
			return false
		var catalog_row := (value as Dictionary).duplicate(true)
		if catalog_row.size() > MAX_MAP_CATALOG_ROW_FIELDS:
			return false
		var map_id := String(catalog_row.get("id", ""))
		var map_relative := String(catalog_row.get("map", ""))
		if map_id == "" or map_id.length() > MAX_MAP_ID_LENGTH or map_id != map_id.strip_edges() or pending.has(map_id):
			return false
		if map_relative == "" or map_relative.length() > MAX_MAP_PATH_LENGTH or not ModLoader.is_safe_relative_path(map_relative):
			return false
		var map_doc := _read_declared_document_bounded(root, map_relative, MAX_COOKED_MAP_BYTES)
		if map_doc.size() > MAX_COOKED_MAP_ROOT_FIELDS:
			return false
		if String(map_doc.get("schema", "")) != "openbfme.map" or int(map_doc.get("schemaVersion", -1)) != 0:
			return false
		if String(map_doc.get("id", "")) != map_id:
			return false

		# Catalog-only discovery metadata (player count, preview, routing status,
		# and the map path itself) is retained, while the cooked map document is
		# authoritative for fields it defines.
		var merged := catalog_row
		merged.merge(map_doc, true)
		merged["map"] = map_relative
		merged["_source"] = ModLoader.resolve_pack_path(root, map_relative)
		merged["_pack_root"] = root
		pending[map_id] = merged

	for map_id_value in pending.keys():
		var map_id := String(map_id_value)
		var row := pending[map_id] as Dictionary
		if bundle_maps.has(map_id):
			var existing := (bundle_maps[map_id] as Dictionary).duplicate(true)
			existing.merge(row, true)
			bundle_maps[map_id] = existing
		else:
			bundle_maps[map_id] = row
	return true


func _load_declared_rows(root: String, relative: String, collection_key: String, table: Dictionary) -> void:
	var document := _read_declared_document(root, relative)
	var values: Variant = document.get(collection_key, [])
	if typeof(values) != TYPE_ARRAY:
		return
	for value in values:
		if typeof(value) != TYPE_DICTIONARY:
			continue
		var row := (value as Dictionary).duplicate(true)
		var id := String(row.get("id", ""))
		if id == "":
			continue
		row["_source"] = ModLoader.resolve_pack_path(root, relative)
		row["_pack_root"] = root
		if table.has(id):
			var merged := (table[id] as Dictionary).duplicate(true)
			merged.merge(row, true)
			table[id] = merged
		else:
			table[id] = row


func _load_retail_unit_rules(root: String, relative: String) -> void:
	var document := _read_declared_document(root, relative)
	if String(document.get("schema", "")) != "openbfme.retail-unit-rules" or int(document.get("schemaVersion", -1)) != 0:
		return
	var values: Variant = document.get("units", [])
	if typeof(values) != TYPE_ARRAY or (values as Array).size() != 4:
		return
	var pending: Dictionary = {}
	for value in values as Array:
		if typeof(value) != TYPE_DICTIONARY:
			return
		var row := (value as Dictionary).duplicate(true)
		var id := String(row.get("id", ""))
		var horde_id := String(row.get("hordeId", ""))
		if id == "" or horde_id == "" or pending.has(id):
			return
		row["_source"] = ModLoader.resolve_pack_path(root, relative)
		row["_pack_root"] = root
		pending[id] = row
	for id in pending:
		retail_unit_rules[id] = pending[id]


func _load_ranger_runtime(root: String, relative: String) -> void:
	if relative == "":
		return
	var document := _read_declared_document(root, relative)
	if (
		String(document.get("schema", "")) != "openbfme.ranger-runtime-contract"
		or int(document.get("schemaVersion", -1)) != 0
		or String(document.get("capabilityStatus", "")) != "rules-and-prerequisite-ready"
	):
		return
	var pack: Dictionary = ModLoader._read_json(ModLoader.resolve_pack_path(root, "pack.json")) as Dictionary
	if String(pack.get("id", "")) == "bfme2-men-ranger-overlay":
		var expected := OS.get_environment("OPENBFME_REVIEWED_RANGER_OVERLAY_SHA256").to_lower()
		var actual := _pack_tree_sha256(root)
		if expected.length() != 64 or not expected.is_valid_hex_number(false) or actual != expected:
			return
		document["_reviewed_content_sha256"] = actual
	document["_source"] = ModLoader.resolve_pack_path(root, relative)
	document["_pack_root"] = root
	ranger_runtime = document


func _pack_tree_sha256(root: String) -> String:
	var files: Array[String] = []
	if not _collect_pack_files(root, "", files):
		return ""
	files.sort()
	var context := HashingContext.new()
	if context.start(HashingContext.HASH_SHA256) != OK:
		return ""
	for relative in files:
		var path := ModLoader.resolve_pack_path(root, relative)
		var digest := FileAccess.get_sha256(path).to_lower()
		if digest.length() != 64:
			return ""
		if context.update((relative + "\n" + digest + "\n").to_utf8_buffer()) != OK:
			return ""
	return context.finish().hex_encode()


func _collect_pack_files(root: String, relative: String, output: Array[String]) -> bool:
	var path := root if relative == "" else ModLoader.resolve_pack_path(root, relative)
	var directory := DirAccess.open(path)
	if directory == null:
		return false
	directory.list_dir_begin()
	var name := directory.get_next()
	while name != "":
		if directory.is_link(name):
			directory.list_dir_end()
			return false
		var child_relative := name if relative == "" else relative.path_join(name)
		if directory.current_is_dir():
			if not _collect_pack_files(root, child_relative, output):
				directory.list_dir_end()
				return false
		else:
			output.append(child_relative.replace("\\", "/"))
		name = directory.get_next()
	directory.list_dir_end()
	return true


func _read_declared_document(root: String, relative: String) -> Dictionary:
	if relative == "":
		return {}
	var path := ModLoader.resolve_pack_path(root, relative)
	if path == "" or not FileAccess.file_exists(path):
		return {}
	var raw: Variant = ModLoader._read_json(path)
	return raw as Dictionary if typeof(raw) == TYPE_DICTIONARY else {}


func _read_declared_document_bounded(root: String, relative: String, maximum_bytes: int) -> Dictionary:
	if relative == "" or maximum_bytes <= 0:
		return {}
	var path := ModLoader.resolve_pack_path(root, relative)
	if path == "" or not FileAccess.file_exists(path):
		return {}
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null or file.get_length() <= 0 or file.get_length() > maximum_bytes:
		return {}
	file.close()
	var raw: Variant = ModLoader._read_json(path)
	return raw as Dictionary if typeof(raw) == TYPE_DICTIONARY else {}


func _load_retail_ui_manifest(root: String, relative: String) -> void:
	var document := _read_declared_document(root, relative)
	if String(document.get("schema", "")) != "openbfme.ui-manifest" or int(document.get("schemaVersion", -1)) != 0:
		return
	var images: Variant = document.get("images", [])
	if typeof(images) != TYPE_ARRAY:
		return
	for value in images:
		if typeof(value) != TYPE_DICTIONARY:
			continue
		var row := (value as Dictionary).duplicate(true)
		var id := String(row.get("id", ""))
		var path := String(row.get("path", ""))
		if id == "" or path == "" or not ModLoader.is_safe_relative_path(path):
			continue
		row["_pack_root"] = root
		row["_source"] = ModLoader.resolve_pack_path(root, relative)
		retail_ui_images[id.to_lower()] = row


func _load_retail_strings(root: String, relative: String) -> void:
	var document := _read_declared_document(root, relative)
	if String(document.get("schema", "")) != "openbfme.localized-strings" or int(document.get("schemaVersion", -1)) != 0:
		return
	var values: Variant = document.get("strings", {})
	if typeof(values) != TYPE_DICTIONARY:
		return
	for key_value in (values as Dictionary).keys():
		var id := String(key_value)
		var text_value: Variant = (values as Dictionary).get(key_value)
		if id != "" and typeof(text_value) == TYPE_STRING:
			retail_strings[id.to_lower()] = String(text_value)


func _load_retail_audio_manifest(root: String, relative: String) -> void:
	var document := _read_declared_document(root, relative)
	if String(document.get("schema", "")) != "openbfme.audio-events" or int(document.get("schemaVersion", -1)) != 1:
		return
	_load_retail_audio_definitions(root, relative, document.get("events", {}), retail_audio_events)
	_load_retail_audio_definitions(root, relative, document.get("multisounds", {}), retail_audio_multisounds)
	var samples: Variant = document.get("samples", {})
	if typeof(samples) != TYPE_DICTIONARY:
		return
	for key_value in (samples as Dictionary).keys():
		var id := String(key_value)
		var path_value: Variant = (samples as Dictionary).get(key_value)
		if id == "" or typeof(path_value) != TYPE_STRING:
			continue
		var path := String(path_value)
		if path == "" or not ModLoader.is_safe_relative_path(path):
			continue
		retail_audio_samples[id.to_lower()] = {
			"id": id,
			"path": path,
			"_pack_root": root,
			"_source": ModLoader.resolve_pack_path(root, relative),
		}


func _load_retail_audio_definitions(root: String, relative: String, values: Variant, target: Dictionary) -> void:
	if typeof(values) != TYPE_DICTIONARY:
		return
	for key_value in (values as Dictionary).keys():
		var id := String(key_value)
		var row_value: Variant = (values as Dictionary).get(key_value)
		if id == "" or typeof(row_value) != TYPE_DICTIONARY:
			continue
		var row := (row_value as Dictionary).duplicate(true)
		row["id"] = id
		row["_pack_root"] = root
		row["_source"] = ModLoader.resolve_pack_path(root, relative)
		target[id.to_lower()] = row

func get_unit(id: String) -> Dictionary:
	return units.get(id, {})

func get_building(id: String) -> Dictionary:
	return buildings.get(id, {})

func get_faction(id: String) -> Dictionary:
	return factions.get(id, {})

func get_ability(id: String) -> Dictionary:
	return abilities.get(id, {})

func get_power(id: String) -> Dictionary:
	return powers.get(id, {})

func get_research(id: String) -> Dictionary:
	return research.get(id, {})

func get_map(id: String) -> Dictionary:
	return maps.get(id, {})

func get_bundle_object(id: String) -> Dictionary:
	return bundle_objects.get(id, {})

func get_retail_unit_rules(id: String) -> Dictionary:
	return retail_unit_rules.get(id, {})


func get_ranger_runtime() -> Dictionary:
	return ranger_runtime.duplicate(true)

func get_animation_capability(id: String) -> Dictionary:
	return animation_capabilities.get(id, {})

func get_bundle_map(id: String) -> Dictionary:
	return bundle_maps.get(id, {})


func get_retail_ui_image(id: String) -> Dictionary:
	return retail_ui_images.get(id.to_lower(), {})


func resolve_retail_ui_image_path(id: String) -> String:
	var row := get_retail_ui_image(id)
	if row.is_empty():
		return ""
	return resolve_asset(String(row.get("path", "")), String(row.get("_pack_root", "")))


func get_retail_string(id: String, fallback: String = "") -> String:
	return String(retail_strings.get(id.to_lower(), fallback))


func get_retail_audio_event(id: String) -> Dictionary:
	return retail_audio_events.get(id.to_lower(), {})


func get_retail_audio_multisound(id: String) -> Dictionary:
	return retail_audio_multisounds.get(id.to_lower(), {})


func resolve_retail_audio_sample_path(id: String) -> String:
	var row: Dictionary = retail_audio_samples.get(id.to_lower(), {})
	if row.is_empty():
		return ""
	return resolve_asset(String(row.get("path", "")), String(row.get("_pack_root", "")))

func matrix_mul(damage_type: String, armor_type: String) -> float:
	if damage_matrix.is_empty():
		return 1.0
	var row: Variant = damage_matrix.get(damage_type, {})
	if typeof(row) != TYPE_DICTIONARY:
		return 1.0
	return float((row as Dictionary).get(armor_type, 1.0))

func unit_ids_for_faction(faction_id: String) -> Array[String]:
	var out: Array[String] = []
	for id in units.keys():
		var u: Dictionary = units[id]
		if String(u.get("faction", "")) == faction_id:
			out.append(id)
	return out

func train_build_roster_ids() -> Dictionary:
	## All unit/building ids used by four-faction skirmish rosters.
	var unit_ids: Dictionary = {}
	var building_ids: Dictionary = {}
	for fid in ["gondor", "mordor", "elves", "goblins"]:
		var f: Dictionary = get_faction(fid)
		for b in f.get("buildings", []):
			building_ids[String(b)] = true
			var bd: Dictionary = get_building(String(b))
			for t in bd.get("trains", []):
				unit_ids[String(t)] = true
			for h in bd.get("heroes", []):
				unit_ids[String(h)] = true
		for su in f.get("starting_units", []):
			unit_ids[String(su)] = true
	return {"units": unit_ids.keys(), "buildings": building_ids.keys()}

func resolve_asset(rel_path: String, preferred_pack_root: String = "") -> String:
	## Asset references are pack-relative (`assets/...`) in bundle v0. Legacy
	## catalogs may remain assets-relative (`models/...`). Absolute paths and
	## traversal are never accepted from content data.
	if rel_path == "" or rel_path == null:
		return ""
	var reference := rel_path.replace("\\", "/")
	if reference.begins_with("res://") or reference.begins_with("user://") or reference.is_absolute_path():
		if preferred_pack_root != "":
			return reference if ModLoader.path_is_within(preferred_pack_root, reference) and _asset_exists(reference) else ""
		for pack_root in pack_roots:
			if ModLoader.path_is_within(pack_root, reference) and _asset_exists(reference):
				return reference
		return ""
	if not ModLoader.is_safe_relative_path(reference):
		return ""

	var roots: Array[String] = []
	if preferred_pack_root != "":
		roots.append(preferred_pack_root)
	# v0 references are explicitly pack-relative and cannot silently borrow a
	# missing retail asset from another pack. Legacy assets-relative mods retain
	# their established fallback chain.
	if not reference.begins_with("assets/") or preferred_pack_root == "":
		for index in range(pack_roots.size() - 1, -1, -1):
			var root: String = pack_roots[index]
			if root != preferred_pack_root:
				roots.append(root)
	for pack_root in roots:
		var pack_relative := reference if reference.begins_with("assets/") else "assets/" + reference
		var base := ModLoader.resolve_pack_path(pack_root, pack_relative)
		if base == "":
			continue
		if _asset_exists(base):
			return base
		if reference.get_extension() == "":
			for extension in [".glb", ".obj", ".png", ".webp", ".wav", ".ogg", ".mp3"]:
				var candidate: String = base + String(extension)
				if _asset_exists(candidate):
					return candidate
	return ""


func _asset_exists(path: String) -> bool:
	return FileAccess.file_exists(path) or ResourceLoader.exists(path)


func is_resolved_asset_path(path: String) -> bool:
	if path == "" or not _asset_exists(path):
		return false
	for pack_root in pack_roots:
		if ModLoader.path_is_within(pack_root, path):
			return true
	return false

func resolve_mesh_path(def: Dictionary) -> String:
	var mesh: String = String(def.get("mesh", ""))
	var id := String(def.get("id", ""))
	var pack_root := String(def.get("_pack_root", ""))
	var presentation: Variant = def.get("presentation", {})
	var explicit_model := String(def.get("modelPath", def.get("model", "")))
	if typeof(presentation) == TYPE_DICTIONARY:
		var p := presentation as Dictionary
		explicit_model = String(p.get("modelPath", p.get("model", p.get("mesh", explicit_model))))
	if explicit_model != "":
		var explicit_path := resolve_asset(explicit_model, pack_root)
		if explicit_path.ends_with(".glb") or explicit_path.ends_with(".gltf"):
			return explicit_path
		if _is_substantial_obj(explicit_path):
			return explicit_path
	# Prefer substantial OBJ (runtime-safe) then GLB. Skip tiny OBJ stubs.
	var candidates: Array[String] = []
	if mesh != "":
		if mesh.get_extension() == "":
			candidates.append(mesh + ".obj")
			candidates.append(mesh + ".glb")
		candidates.append(mesh)
	if id != "":
		candidates.append("models/units/%s.obj" % id)
		candidates.append("models/buildings/%s.obj" % id)
		candidates.append("models/units/%s.glb" % id)
		candidates.append("models/buildings/%s.glb" % id)
		candidates.append("models/units/%s" % id)
		candidates.append("models/buildings/%s" % id)
	var best_glb := ""
	for c in candidates:
		var path := resolve_asset(c, pack_root)
		if path == "":
			continue
		if path.ends_with(".obj"):
			if _is_substantial_obj(path):
				return path
			continue
		if (path.ends_with(".glb") or path.ends_with(".gltf")) and best_glb == "":
			best_glb = path
	return best_glb


func _is_substantial_obj(path: String) -> bool:
	if not path.ends_with(".obj"):
		return false
	var absolute := ProjectSettings.globalize_path(path) if path.begins_with("res://") or path.begins_with("user://") else path
	if not FileAccess.file_exists(absolute):
		return false
	var file := FileAccess.open(absolute, FileAccess.READ)
	return file != null and file.get_length() >= 1000

func resolve_icon_path(def: Dictionary) -> String:
	var icon: String = String(def.get("icon", ""))
	var presentation: Variant = def.get("presentation", {})
	if typeof(presentation) == TYPE_DICTIONARY:
		icon = String((presentation as Dictionary).get("icon", icon))
	if icon == "":
		return ""
	return resolve_asset(icon, String(def.get("_pack_root", "")))
