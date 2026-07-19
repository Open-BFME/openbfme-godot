extends Node
## Loads JSON content packs. Mods override by id (later packs win).

const MAX_MAP_CATALOG_ROWS := 256
const MAX_MAP_CATALOG_BYTES := 1024 * 1024
const MAX_COOKED_MAP_BYTES := 2 * 1024 * 1024
const MAX_MAP_CATALOG_ROW_FIELDS := 64
const MAX_COOKED_MAP_ROOT_FIELDS := 128
const MAX_MAP_ID_LENGTH := 256
const MAX_MAP_PATH_LENGTH := 1024
const MAX_PLAYABLE_UNIT_RUNTIME_BYTES := 4 * 1024 * 1024
const MAX_PLAYABLE_UNIT_RUNTIMES_PER_PACK := 512
const MAX_PLAYABLE_STRUCTURE_RUNTIME_BYTES := 4 * 1024 * 1024
const MAX_PLAYABLE_STRUCTURE_RUNTIMES_PER_PACK := 256
const PLAYABLE_STRUCTURE_LIFECYCLE_PHASES: Array[String] = [
	"construction", "intact", "damaged", "really-damaged", "rubble", "post-rubble",
]
const PLAYABLE_STRUCTURE_PRESENTED_PHASES: Array[String] = [
	"construction", "intact", "damaged", "really-damaged", "collapsing", "rubble", "post-rubble", "post-collapse",
]
const PLAYABLE_STRUCTURE_ANIMATION_MODES: Array[String] = [
	"none", "manual-progress", "loop", "loop-random", "once",
]
const PLAYABLE_STRUCTURE_PRODUCTION_EVIDENCE: Array[String] = [
	"authored-construct-command", "engine-spawned-composite", "wall-template",
]

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
var trebuchet_runtime: Dictionary = {}
var playable_unit_runtimes: Dictionary = {}
var playable_structure_runtimes: Dictionary = {}
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
	trebuchet_runtime.clear()
	playable_unit_runtimes.clear()
	playable_structure_runtimes.clear()
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
	_load_trebuchet_runtime(root, String(declared.get("trebuchetRuntime", "")))
	_load_playable_unit_runtimes(root, declared)
	_load_playable_structure_runtimes(root, declared)
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


func _load_trebuchet_runtime(root: String, relative: String) -> void:
	if relative == "":
		return
	var document := _read_declared_document(root, relative)
	if (
		String(document.get("schema", "")) != "openbfme.trebuchet-runtime-contract"
		or int(document.get("schemaVersion", -1)) != 0
		or String(document.get("capabilityStatus", "")) != "bounded-direct-structure-ready"
	):
		return
	var workshop: Dictionary = bundle_objects.get("bfme2.object.gondor-workshop", {})
	var trebuchet: Dictionary = bundle_objects.get("bfme2.object.gondor-trebuchet", {})
	if (
		String(workshop.get("_pack_root", "")) != root
		or String(trebuchet.get("_pack_root", "")) != root
		or String(workshop.get("kind", "")) != "structure"
		or String(trebuchet.get("kind", "")) != "member"
	):
		return
	var presentation: Dictionary = trebuchet.get("presentation", {}) as Dictionary
	for key in ["model", "deathModel", "projectileModel"]:
		var asset := String(presentation.get(key, ""))
		if asset == "" or resolve_asset(asset, root) == "":
			return
	document["_source"] = ModLoader.resolve_pack_path(root, relative)
	document["_pack_root"] = root
	trebuchet_runtime = document


func _load_playable_unit_runtimes(root: String, declared: Dictionary) -> bool:
	## Generic playable units are an atomic per-pack registry. A malformed or
	## colliding declaration rejects this pack's entire playable-unit delta so a
	## half-loaded faction can never leak into production or the HUD.
	var keys: Array[String] = []
	for value in declared.keys():
		var key := String(value)
		if key.begins_with("playableUnit."):
			keys.append(key)
	keys.sort_custom(func(a: String, b: String) -> bool: return a.naturalnocasecmp_to(b) < 0)
	if keys.size() > MAX_PLAYABLE_UNIT_RUNTIMES_PER_PACK:
		return false
	var pending: Dictionary = {}
	var pending_folded: Dictionary = {}
	for key in keys:
		var relative := String(declared.get(key, ""))
		if relative == "" or not ModLoader.is_safe_relative_path(relative):
			return false
		var document := _read_declared_document_bounded(root, relative, MAX_PLAYABLE_UNIT_RUNTIME_BYTES)
		if not _validate_playable_unit_runtime(root, document):
			return false
		var object_id := String(document["objectId"])
		var folded := object_id.to_lower()
		if pending_folded.has(folded):
			return false
		for existing_id_value in playable_unit_runtimes.keys():
			if String(existing_id_value).to_lower() == folded and String(existing_id_value) != object_id:
				return false
		document["_source"] = ModLoader.resolve_pack_path(root, relative)
		document["_pack_root"] = root
		document["_pack_file_key"] = key
		pending[object_id] = document
		pending_folded[folded] = object_id
	var projections: Dictionary = {}
	var hero_ordinals: Dictionary = {}
	for existing_id_value in playable_unit_runtimes.keys():
		var existing_id := String(existing_id_value)
		var existing_document := playable_unit_runtimes[existing_id] as Dictionary
		var existing_production: Array = (existing_document.get("registration", {}) as Dictionary).get("production", [])
		for route_value in existing_production:
			var route := route_value as Dictionary
			if String(route.get("surface", "")) == "hero-roster":
				var ordinal_key := "%s:%d" % [String(route.get("producerObjectId", "")).to_lower(), int(route.get("rosterOrdinal", 0))]
				hero_ordinals[ordinal_key] = existing_id
	for object_id_value in pending.keys():
		var object_id := String(object_id_value)
		var document := pending[object_id] as Dictionary
		var production: Array = (document.get("registration", {}) as Dictionary).get("production", [])
		for route_value in production:
			var route := route_value as Dictionary
			if String(route.get("surface", "")) != "hero-roster":
				continue
			var ordinal_key := "%s:%d" % [String(route.get("producerObjectId", "")).to_lower(), int(route.get("rosterOrdinal", 0))]
			if hero_ordinals.has(ordinal_key) and String(hero_ordinals[ordinal_key]).to_lower() != object_id.to_lower():
				return false
			hero_ordinals[ordinal_key] = object_id
		var projection := _playable_unit_projection(document)
		if projection.is_empty():
			return false
		projections[object_id] = projection
	for object_id_value in pending.keys():
		var object_id := String(object_id_value)
		var document := pending[object_id] as Dictionary
		var projection := projections[object_id] as Dictionary
		playable_unit_runtimes[object_id] = document
		var member := projection["member"] as Dictionary
		var capability := projection["capability"] as Dictionary
		bundle_objects[String(member["id"])] = member
		animation_capabilities[String(capability["id"])] = capability
	return true


func _validate_playable_unit_runtime(root: String, document: Dictionary) -> bool:
	if (
		String(document.get("schema", "")) != "openbfme.playable-unit-runtime"
		or int(document.get("schemaVersion", -1)) != 0
		or String(document.get("objectId", "")).strip_edges() == ""
		or String(document.get("objectId", "")).length() > 256
		or String(document.get("category", "")) not in ["infantry", "ranged-infantry", "cavalry", "hero", "siege", "monster", "naval"]
		or not _is_sha256(String(document.get("descriptorSha256", "")))
		or not _is_sha256(String(document.get("recipeSha256", "")))
	):
		return false
	var resource_ids: Variant = document.get("resourceIds")
	var registration_value: Variant = document.get("registration")
	if typeof(resource_ids) != TYPE_ARRAY or typeof(registration_value) != TYPE_DICTIONARY:
		return false
	var seen_resources: Dictionary = {}
	for value in resource_ids as Array:
		if typeof(value) != TYPE_STRING or String(value).strip_edges() == "":
			return false
		var folded := String(value).to_lower()
		if seen_resources.has(folded):
			return false
		seen_resources[folded] = true
	var registration := registration_value as Dictionary
	var category := String(document.get("category", ""))
	for required in ["production", "composition", "gameplay", "simulation", "capabilities", "visual", "ui", "imageBindings", "audioRoutes", "audioBindings", "audioResolution", "unsupportedCapabilities"]:
		if not registration.has(required):
			return false
	if typeof(registration.production) != TYPE_ARRAY or (registration.production as Array).is_empty():
		return false
	for route_value in registration.production as Array:
		if typeof(route_value) != TYPE_DICTIONARY:
			return false
		var route := route_value as Dictionary
		var surface := String(route.get("surface", ""))
		var slot := int(route.get("slot", 0))
		var roster_ordinal := int(route.get("rosterOrdinal", 0))
		if (
			String(route.get("producerObjectId", "")) == ""
			or String(route.get("commandSetId", "")) == ""
			or String(route.get("commandId", "")) == ""
			or surface not in ["command-socket", "hero-roster"]
			or (surface == "command-socket" and (slot < 1 or roster_ordinal != 0))
			or (surface == "hero-roster" and (roster_ordinal < 1 or slot != 0))
			or (category == "hero" and surface != "hero-roster")
			or (category != "hero" and surface == "hero-roster")
		):
			return false
	if typeof(registration.composition) != TYPE_DICTIONARY or typeof(registration.gameplay) != TYPE_DICTIONARY:
		return false
	if typeof(registration.visual) != TYPE_DICTIONARY or typeof(registration.ui) != TYPE_DICTIONARY:
		return false
	var visual := registration.visual as Dictionary
	var components: Variant = visual.get("components")
	var core: Variant = visual.get("coreAnimations")
	if typeof(components) != TYPE_ARRAY or (components as Array).is_empty() or typeof(core) != TYPE_DICTIONARY:
		return false
	var default_count := 0
	for component_value in components as Array:
		if typeof(component_value) != TYPE_DICTIONARY:
			return false
		var component := component_value as Dictionary
		var output := String(component.get("output", ""))
		if output == "" or resolve_asset(output, root) == "":
			return false
		if bool(component.get("default", false)):
			default_count += 1
	if default_count != 1:
		return false
	for state_value in (core as Dictionary).values():
		if typeof(state_value) != TYPE_ARRAY or (state_value as Array).is_empty():
			return false
		for binding_value in state_value as Array:
			if typeof(binding_value) != TYPE_DICTIONARY or String((binding_value as Dictionary).get("identifier", "")) == "":
				return false
	for bindings_key in ["imageBindings", "audioBindings"]:
		var bindings: Variant = registration.get(bindings_key)
		if typeof(bindings) != TYPE_DICTIONARY:
			return false
		for paths_value in (bindings as Dictionary).values():
			var paths: Array = paths_value if typeof(paths_value) == TYPE_ARRAY else [paths_value]
			for path_value in paths:
				if typeof(path_value) != TYPE_STRING or resolve_asset(String(path_value), root) == "":
					return false
	var image_bindings := registration.get("imageBindings", {}) as Dictionary
	if registration.has("imageBindingMetadata"):
		var image_metadata_value: Variant = registration.get("imageBindingMetadata")
		if typeof(image_metadata_value) != TYPE_DICTIONARY:
			return false
		var image_metadata := image_metadata_value as Dictionary
		if image_metadata.size() != image_bindings.size():
			return false
		for image_id_value in image_bindings.keys():
			var image_id := String(image_id_value)
			var metadata_value: Variant = image_metadata.get(image_id)
			if typeof(metadata_value) != TYPE_DICTIONARY:
				return false
			var metadata := metadata_value as Dictionary
			if metadata.size() != 2 or not metadata.has("width") or not metadata.has("height"):
				return false
			for axis in ["width", "height"]:
				var dimension: Variant = metadata[axis]
				if typeof(dimension) not in [TYPE_INT, TYPE_FLOAT] or not is_finite(float(dimension)) or float(dimension) != float(int(dimension)) or int(dimension) <= 0:
					return false
	if registration.has("stringBindings"):
		var string_bindings_value: Variant = registration.get("stringBindings")
		if typeof(string_bindings_value) != TYPE_DICTIONARY:
			return false
		var string_bindings := string_bindings_value as Dictionary
		var required_string_ids: Dictionary = {}
		for command_value in (registration.ui as Dictionary).get("commands", []):
			if typeof(command_value) != TYPE_DICTIONARY:
				return false
			var fields := (command_value as Dictionary).get("fields", {}) as Dictionary
			for field in ["TextLabel", "DescriptLabel"]:
				for string_id_value in fields.get(field, []):
					required_string_ids[String(string_id_value)] = true
		if not string_bindings.is_empty() and string_bindings.size() != required_string_ids.size():
			return false
		for string_id_value in string_bindings.keys():
			if not required_string_ids.has(String(string_id_value)) or String(string_id_value).strip_edges() == "" or typeof(string_bindings[string_id_value]) != TYPE_STRING or String(string_bindings[string_id_value]).strip_edges() == "":
				return false
	return true


func _playable_unit_projection(document: Dictionary) -> Dictionary:
	var registration: Dictionary = document.get("registration", {}) as Dictionary
	var composition: Dictionary = registration.get("composition", {}) as Dictionary
	var visual: Dictionary = registration.get("visual", {}) as Dictionary
	var components: Array = visual.get("components", []) as Array
	var default_component: Dictionary = {}
	for value in components:
		if typeof(value) == TYPE_DICTIONARY and bool((value as Dictionary).get("default", false)):
			default_component = value as Dictionary
			break
	var source_member := String(composition.get("primaryMemberObjectId", ""))
	var model := String(default_component.get("output", ""))
	if source_member == "" or model == "":
		return {}
	var member_id := _playable_runtime_id(source_member)
	var capability_id := "playable-unit:" + String(document.get("objectId", "")).to_lower()
	var states: Dictionary = {}
	var core: Dictionary = visual.get("coreAnimations", {}) as Dictionary
	for state_value in core.keys():
		var state := String(state_value)
		var clips: Array[String] = []
		for binding_value in core[state] as Array:
			var identifier := String((binding_value as Dictionary).get("identifier", ""))
			if identifier != "" and not clips.has(identifier):
				clips.append(identifier)
		states[state] = {
			"clips": clips,
			"mode": "loop" if state in ["idle", "move"] else "once",
			"useWeaponTiming": state == "attack",
		}
	var root := String(document.get("_pack_root", ""))
	return {
		"member": {
			"id": member_id,
			"kind": "member",
			"sourceObjectId": source_member,
			"animationCapabilityId": capability_id,
			"presentation": {"model": model},
			"_source": String(document.get("_source", "")),
			"_pack_root": root,
		},
		"capability": {
			"id": capability_id,
			"states": states,
			"unresolvedAnimationTracks": 0,
			"source": "openbfme.playable-unit-runtime",
			"_source": String(document.get("_source", "")),
			"_pack_root": root,
		},
	}


func _playable_runtime_id(source_id: String) -> String:
	var output := ""
	var previous_dash := false
	for index in source_id.length():
		var code := source_id.unicode_at(index)
		var is_upper := code >= 65 and code <= 90
		var is_lower := code >= 97 and code <= 122
		var is_digit := code >= 48 and code <= 57
		if is_upper and index > 0 and not previous_dash:
			var previous := source_id.unicode_at(index - 1)
			if (previous >= 97 and previous <= 122) or (previous >= 48 and previous <= 57):
				output += "-"
		if is_upper or is_lower or is_digit:
			output += String.chr(code).to_lower()
			previous_dash = false
		elif not previous_dash and output != "":
			output += "-"
			previous_dash = true
	return "bfme2.object." + output.trim_suffix("-")


func _load_playable_structure_runtimes(root: String, declared: Dictionary) -> bool:
	## Generic playable structures are an atomic per-pack registry mirroring the
	## playable-unit loader. A malformed or colliding declaration rejects this
	## pack's entire playable-structure delta so a half-loaded faction base can
	## never leak into the slice's base loop or construction surfaces.
	var keys: Array[String] = []
	for value in declared.keys():
		var key := String(value)
		if key.begins_with("playableStructure."):
			keys.append(key)
	keys.sort_custom(func(a: String, b: String) -> bool: return a.naturalnocasecmp_to(b) < 0)
	if keys.size() > MAX_PLAYABLE_STRUCTURE_RUNTIMES_PER_PACK:
		return false
	var pending: Dictionary = {}
	var pending_folded: Dictionary = {}
	for key in keys:
		var relative := String(declared.get(key, ""))
		if relative == "" or not ModLoader.is_safe_relative_path(relative):
			return false
		var document := _read_declared_document_bounded(root, relative, MAX_PLAYABLE_STRUCTURE_RUNTIME_BYTES)
		if not _validate_playable_structure_runtime(root, document):
			return false
		var object_id := String(document["objectId"])
		var folded := object_id.to_lower()
		if pending_folded.has(folded):
			return false
		for existing_id_value in playable_structure_runtimes.keys():
			if String(existing_id_value).to_lower() == folded and String(existing_id_value) != object_id:
				return false
		document["_source"] = ModLoader.resolve_pack_path(root, relative)
		document["_pack_root"] = root
		document["_pack_file_key"] = key
		pending[object_id] = document
		pending_folded[folded] = object_id
	for object_id_value in pending.keys():
		playable_structure_runtimes[String(object_id_value)] = pending[object_id_value]
	return true


func _validate_playable_structure_runtime(root: String, document: Dictionary) -> bool:
	var object_id := String(document.get("objectId", ""))
	if (
		String(document.get("schema", "")) != "openbfme.playable-structure-runtime"
		or int(document.get("schemaVersion", -1)) != 0
		or object_id.strip_edges() == ""
		or object_id.length() > 256
		or String(document.get("slug", "")) == ""
		or String(document.get("slug", "")) != _playable_structure_slug(object_id)
		or not _is_sha256(String(document.get("descriptorSha256", "")))
		or not _is_sha256(String(document.get("recipeSha256", "")))
		or not _is_sha256(String(document.get("runtimeSha256", "")))
	):
		return false
	var registration_value: Variant = document.get("registration")
	if typeof(registration_value) != TYPE_DICTIONARY:
		return false
	var registration := registration_value as Dictionary
	for required in ["production", "gameplay", "presentation", "unsupportedVisualReferences"]:
		if not registration.has(required):
			return false
	if typeof(registration.unsupportedVisualReferences) != TYPE_ARRAY:
		return false
	if not _validate_playable_structure_production(registration.get("production")):
		return false
	var gameplay_value: Variant = registration.get("gameplay")
	if typeof(gameplay_value) != TYPE_DICTIONARY:
		return false
	var gameplay := gameplay_value as Dictionary
	var maximum_health := _validate_playable_structure_gameplay(gameplay)
	if maximum_health <= 0:
		return false
	var presentation_value: Variant = registration.get("presentation")
	if typeof(presentation_value) != TYPE_DICTIONARY:
		return false
	var presentation := presentation_value as Dictionary
	if typeof(presentation.get("ui")) != TYPE_DICTIONARY or typeof(presentation.get("audioRoutes")) != TYPE_DICTIONARY:
		return false
	return _validate_playable_structure_lifecycle(root, presentation.get("buildingLifecycle"), gameplay, maximum_health, object_id)


func _validate_playable_structure_production(value: Variant) -> bool:
	if typeof(value) != TYPE_DICTIONARY:
		return false
	var production := value as Dictionary
	var evidence := String(production.get("evidence", ""))
	var routes_value: Variant = production.get("routes")
	if evidence not in PLAYABLE_STRUCTURE_PRODUCTION_EVIDENCE or typeof(routes_value) != TYPE_ARRAY:
		return false
	var routes := routes_value as Array
	if evidence == "authored-construct-command" and routes.is_empty():
		return false
	if evidence != "authored-construct-command" and not routes.is_empty():
		return false
	for route_value in routes:
		if typeof(route_value) != TYPE_DICTIONARY:
			return false
		var route := route_value as Dictionary
		if (
			String(route.get("surface", "")) != "construct"
			or String(route.get("builderObjectId", "")).strip_edges() == ""
			or String(route.get("commandSetId", "")).strip_edges() == ""
			or String(route.get("commandId", "")).strip_edges() == ""
			or String(route.get("commandKind", "")).strip_edges() == ""
			or int(route.get("slot", 0)) < 1
			or typeof(route.get("prerequisites")) != TYPE_ARRAY
		):
			return false
		for prerequisite_value in route.get("prerequisites") as Array:
			if typeof(prerequisite_value) != TYPE_STRING or String(prerequisite_value).strip_edges() == "":
				return false
	return true


func _validate_playable_structure_gameplay(gameplay: Dictionary) -> int:
	## Returns the proven MaxHealth, or 0 when the gameplay contract is invalid.
	## Foundation-only structures (health null) never reach runtime documents.
	for required in ["health", "trainedCommandSets", "scalarFields"]:
		if not gameplay.has(required):
			return 0
	if typeof(gameplay.get("scalarFields")) != TYPE_DICTIONARY:
		return 0
	var trained_value: Variant = gameplay.get("trainedCommandSets")
	if typeof(trained_value) != TYPE_ARRAY:
		return 0
	for trained_row_value in trained_value as Array:
		if typeof(trained_row_value) != TYPE_DICTIONARY:
			return 0
		var trained_row := trained_row_value as Dictionary
		if (
			String(trained_row.get("id", "")).strip_edges() == ""
			or String(trained_row.get("kind", "")) not in ["direct", "upgraded"]
			or typeof(trained_row.get("slots")) != TYPE_ARRAY
		):
			return 0
		for slot_value in trained_row.get("slots") as Array:
			if typeof(slot_value) != TYPE_DICTIONARY:
				return 0
			var slot_row := slot_value as Dictionary
			if int(slot_row.get("slot", 0)) < 1 or String(slot_row.get("commandId", "")).strip_edges() == "":
				return 0
	var health_value: Variant = gameplay.get("health")
	if typeof(health_value) != TYPE_DICTIONARY:
		return 0
	var primary_value: Variant = (health_value as Dictionary).get("primary")
	if typeof(primary_value) != TYPE_DICTIONARY:
		return 0
	return _playable_structure_health_number((primary_value as Dictionary).get("maxHealth"))


func _playable_structure_health_number(value: Variant) -> int:
	if typeof(value) != TYPE_DICTIONARY:
		return 0
	var number: Variant = (value as Dictionary).get("value")
	if typeof(number) not in [TYPE_INT, TYPE_FLOAT] or not is_finite(float(number)) or float(number) != float(int(number)):
		return 0
	return int(number) if int(number) > 0 else 0


func _validate_playable_structure_lifecycle(root: String, value: Variant, gameplay: Dictionary, maximum_health: int, source_object_id: String) -> bool:
	## Validates the composed presenter-grade version-1 lifecycle presentation
	## document (the exact shape RetailStructure's generic v1 branch consumes).
	if typeof(value) != TYPE_DICTIONARY:
		return false
	var lifecycle := value as Dictionary
	if (
		String(lifecycle.get("schema", "")) != "openbfme.building-lifecycle-presentation"
		or int(lifecycle.get("schemaVersion", -1)) != 1
		or String(lifecycle.get("evidenceProfile", "")) != "composed-structure-runtime"
		or String(lifecycle.get("objectId", "")) != _playable_runtime_id(source_object_id)
		or String(lifecycle.get("initialPhase", "")) != "intact"
		or typeof(lifecycle.get("phases")) != TYPE_ARRAY
		or typeof(lifecycle.get("phaseCoverage")) != TYPE_DICTIONARY
		or typeof(lifecycle.get("simulationFacts")) != TYPE_DICTIONARY
	):
		return false
	var phases := lifecycle.get("phases") as Array
	if phases.size() != PLAYABLE_STRUCTURE_PRESENTED_PHASES.size():
		return false
	for index in phases.size():
		if typeof(phases[index]) != TYPE_DICTIONARY:
			return false
		var row := phases[index] as Dictionary
		var phase := String(row.get("phase", ""))
		if phase != PLAYABLE_STRUCTURE_PRESENTED_PHASES[index]:
			return false
		if not _validate_playable_structure_phase_row(root, row, phase not in ["post-rubble", "post-collapse"]):
			return false
	var coverage := lifecycle.get("phaseCoverage") as Dictionary
	var declared_covered: Variant = coverage.get("covered")
	var declared_missing: Variant = coverage.get("missing")
	if typeof(declared_covered) != TYPE_ARRAY or typeof(declared_missing) != TYPE_ARRAY:
		return false
	var partition: Array = []
	partition.append_array(declared_covered)
	partition.append_array(declared_missing)
	partition.sort()
	var expected_partition: Array = Array(PLAYABLE_STRUCTURE_LIFECYCLE_PHASES.duplicate())
	expected_partition.sort()
	if partition != expected_partition:
		return false
	if not Array(declared_covered).has("intact") or not Array(declared_covered).has("construction"):
		return false
	var bib_value: Variant = lifecycle.get("bib")
	if typeof(bib_value) == TYPE_DICTIONARY:
		var bib := bib_value as Dictionary
		var bib_visual: Dictionary = bib.get("visual", {}) as Dictionary
		var bib_path := String(bib_visual.get("glb", ""))
		if (
			typeof(bib.get("duringConstruction")) != TYPE_BOOL
			or String(bib_visual.get("mode", "")) != "glb"
			or bib_path.get_extension().to_lower() != "glb"
			or not ModLoader.is_safe_relative_path(bib_path)
			or resolve_asset(bib_path, root) == ""
		):
			return false
	elif bib_value != null:
		return false
	var facts := lifecycle.get("simulationFacts") as Dictionary
	if _playable_structure_health_number({"value": facts.get("maximumHealth")}) != maximum_health:
		return false
	var health := gameplay.get("health") as Dictionary
	var primary := health.get("primary") as Dictionary
	var damaged := _playable_structure_health_number(primary.get("maxHealthDamaged"))
	var really_damaged := _playable_structure_health_number(primary.get("maxHealthReallyDamaged"))
	var rule_value: Variant = facts.get("damageStateRule")
	if typeof(rule_value) != TYPE_DICTIONARY:
		return false
	var rule := rule_value as Dictionary
	var rule_damaged := _playable_structure_health_number({"value": rule.get("damagedThreshold")})
	var rule_really_damaged := _playable_structure_health_number({"value": rule.get("reallyDamagedThreshold")})
	if (
		rule_damaged != damaged
		or rule_really_damaged != really_damaged
		or really_damaged >= damaged
		or damaged >= maximum_health
	):
		return false
	return true


func _validate_playable_structure_phase_row(root: String, row: Dictionary, has_next: bool) -> bool:
	if typeof(row.get("visual")) != TYPE_DICTIONARY or typeof(row.get("animation")) != TYPE_DICTIONARY:
		return false
	if typeof(row.get("sourceConditionSets")) != TYPE_ARRAY:
		return false
	if String(row.get("transitionAuthority", "")) != "deterministic-simulation":
		return false
	var visual := row.get("visual") as Dictionary
	var visual_mode := String(visual.get("mode", ""))
	if visual_mode == "glb":
		var path := String(visual.get("glb", ""))
		if (
			path == ""
			or path.get_extension().to_lower() != "glb"
			or not ModLoader.is_safe_relative_path(path)
			or resolve_asset(path, root) == ""
		):
			return false
	elif visual_mode == "no-render":
		if String(visual.get("sourceIdentifier", "")).strip_edges() == "":
			return false
	else:
		return false
	var animation := row.get("animation") as Dictionary
	var mode := String(animation.get("mode", ""))
	if mode not in PLAYABLE_STRUCTURE_ANIMATION_MODES:
		return false
	var clip: Variant = animation.get("clip")
	if mode == "none":
		if clip != null:
			return false
	elif typeof(clip) != TYPE_STRING or String(clip).strip_edges() == "" or visual_mode != "glb":
		return false
	if String(row.get("phase", "")) == "construction" and mode != "manual-progress":
		return false
	var next_phase: Variant = row.get("nextPhase")
	if has_next:
		var order := PLAYABLE_STRUCTURE_PRESENTED_PHASES.find(String(row.get("phase", "")))
		if typeof(next_phase) != TYPE_STRING or String(next_phase) != PLAYABLE_STRUCTURE_PRESENTED_PHASES[order + 1]:
			return false
	elif next_phase != null:
		return false
	return true


func _playable_structure_slug(value: String) -> String:
	## Mirrors the importer's slug rule exactly: casefold, collapse every
	## non-alphanumeric run to one dash, and strip edge dashes. Unlike the
	## camel-splitting runtime ids, authored capitalization is not a separator.
	var output := ""
	var previous_dash := false
	var folded := value.to_lower()
	for index in folded.length():
		var code := folded.unicode_at(index)
		var is_lower := code >= 97 and code <= 122
		var is_digit := code >= 48 and code <= 57
		if is_lower or is_digit:
			output += String.chr(code)
			previous_dash = false
		elif not previous_dash and output != "":
			output += "-"
			previous_dash = true
	return output.trim_suffix("-")


func _is_sha256(value: String) -> bool:
	return value.length() == 64 and value.is_valid_hex_number(false)


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


func get_trebuchet_runtime() -> Dictionary:
	return trebuchet_runtime.duplicate(true)


func get_playable_unit_runtime(object_id: String) -> Dictionary:
	return (playable_unit_runtimes.get(object_id, {}) as Dictionary).duplicate(true)


func get_playable_unit_runtimes() -> Dictionary:
	return playable_unit_runtimes.duplicate(true)


func get_playable_structure_runtime(object_id: String) -> Dictionary:
	return (playable_structure_runtimes.get(object_id, {}) as Dictionary).duplicate(true)


func get_playable_structure_runtimes() -> Dictionary:
	return playable_structure_runtimes.duplicate(true)


func resolve_playable_unit_image_path(object_id: String, image_id: String) -> String:
	var document: Dictionary = playable_unit_runtimes.get(object_id, {})
	if document.is_empty():
		return ""
	var registration: Dictionary = document.get("registration", {}) as Dictionary
	var bindings: Dictionary = registration.get("imageBindings", {}) as Dictionary
	var relative: Variant = bindings.get(image_id)
	if typeof(relative) != TYPE_STRING:
		return ""
	return resolve_asset(String(relative), String(document.get("_pack_root", "")))

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
