extends Node
## Loads JSON content packs. Mods override by id (later packs win).

const BootProfile = preload("res://src/core/boot_profile.gd")

const MAX_MAP_CATALOG_ROWS := 256
const MAX_MAP_CATALOG_BYTES := 1024 * 1024
const MAX_COOKED_MAP_BYTES := 2 * 1024 * 1024
const MAX_MAP_CATALOG_ROW_FIELDS := 64
const MAX_COOKED_MAP_ROOT_FIELDS := 128
const MAX_MAP_ID_LENGTH := 256
const MAX_MAP_PATH_LENGTH := 1024
# Registry sizes are per PACK, and a pack now composes every faction it was
# built from, so both counts scale with faction count rather than being a
# single roster. Measured: single-faction Men v-slice = 16 units / 22
# structures; six-faction v-slice = 82 units / 130 structures (~14 units and
# ~22 structures per added faction). 130 was already at 51% of the old 256
# structure ceiling, so a 7th faction plus mod content would have tripped it.
# Both raised to 2,048 — still a hard resource-exhaustion guard, with room for
# a fully composed pack an order of magnitude larger than today's.
const MAX_PLAYABLE_UNIT_RUNTIME_BYTES := 4 * 1024 * 1024
const MAX_PLAYABLE_UNIT_RUNTIMES_PER_PACK := 2_048
const MAX_PLAYABLE_STRUCTURE_RUNTIME_BYTES := 4 * 1024 * 1024
const MAX_PLAYABLE_STRUCTURE_RUNTIMES_PER_PACK := 2_048
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
	"authored-construct-command", "authored-wall-upgrade-command", "engine-spawned-composite", "wall-template",
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
var spellbook_runtime: Dictionary = {}
## The Create-a-Hero class table (`openbfme.cah-system-runtime`): the seven
## classes, their subclasses, the five attribute ladders and the point budgets,
## compiled by the importer out of retail's createaherosystem.ini.
##
## EMPTY IS A NORMAL STATE, not a failure. No pack has to carry this document,
## and until one does the MY HEROES screen has nothing to build a hero out of
## and says so by name. Nothing else in the game reads it, so an absent table
## costs exactly the Create-a-Hero feature and nothing else.
var cah_system_runtime: Dictionary = {}
## Effect-object presentation rows registered from every admitted spellbook
## runtime document's `presentation.visualBindings`: object id -> the binding
## row the importer emitted (status / model / sourceW3d / memberObjectId).
## Summoned units, groves and trees are NOT playable-unit documents, so the
## roster projection never registers them; without this table every summoned
## object reached AssetFactory with no mesh path and fell back to the synthetic
## multi-part kit (the "blue units"). Objects retail authors with `Model = None`
## are recorded here too, as `authored-invisible`, and stay invisible.
var spellbook_visual_bindings: Dictionary = {}
## Effect objects a spellbook bound to no converted model, with the importer's
## verbatim reason. Surfaced for the runners rather than silently substituted.
var spellbook_unconverted_visuals: Array[String] = []
var playable_unit_runtimes: Dictionary = {}
## Every admitted copy of a playableUnit.* document: casefolded object id to
## the load-ordered list of per-pack documents. Shared retail units (the
## evil-faction MordorWorker ships in the isengard/mordor/wild packs, each
## bound to that faction's own lumber mill) legitimately carry one document
## per faction pack. The flat registry above keeps only the last-loaded copy;
## faction-scoped consumers (the manifest) resolve their own pack's copy from
## this index instead.
var playable_unit_runtime_pack_index: Dictionary = {}
## Load-ordered per-pack bundle object members / animation capabilities
## projected from playableUnit.* documents. Shared retail units (MordorWorker)
## project the same member and capability ids from every faction pack; the
## flat bundle_objects / animation_capabilities tables keep only the
## last-loaded pack's rows, so pack-scoped lookups resolve through these.
var bundle_object_pack_index: Dictionary = {}
var animation_capability_pack_index: Dictionary = {}
## Pack-file keys of playableUnit.* documents skipped during load, with the
## skip reason ("<key>:<detail>"). Surfaced so roster composition can record
## them as exclusions instead of the roster silently narrowing.
var skipped_playable_unit_documents: Array[String] = []
var playable_structure_runtimes: Dictionary = {}
var animation_capabilities: Dictionary = {}
var bundle_maps: Dictionary = {}
## Map ids that a pack published through a `mapCatalog` document, in authored
## order. Empty when no loaded pack ships a catalog, which is what keeps the
## skirmish menu on its authored fallback list instead of a one-entry list.
var catalog_map_ids: Array[String] = []
var retail_ui_images: Dictionary = {}
var retail_strings: Dictionary = {}
var retail_audio_events: Dictionary = {}
var retail_audio_multisounds: Dictionary = {}
var retail_audio_samples: Dictionary = {}
var globals: Dictionary = {}
var damage_matrix: Dictionary = {}
## Authored per-faction music binding (`openbfme.music`), contributed by any
## pack that declares a `music` file. Music is edition-wide rather than
## per-faction content - every faction selects out of the same track closure -
## so it ships as its own supplemental pack and the LAST declaring pack wins.
var music_document: Dictionary = {}
var pack_meta: Array = []
var pack_roots: Array[String] = []
var asset_roots: Array[String] = []
var _asset_exists_cache: Dictionary = {}
var _asset_exists_mutex := Mutex.new()
## Asset resolutions proven on the MAIN THREAD before a validation fan-out, and
## thereafter only ever READ. See _freeze_asset_resolutions for why this table
## exists and exactly which resolutions are eligible to enter it.
var _frozen_resolutions: Dictionary = {}


## Env-gated reload profiler (OPENBFME_PROFILE_CONTENTDB=1): prints cumulative
## milliseconds per pack/stage since the given mark. Zero cost when disabled.
var _profile_db_enabled := OS.get_environment("OPENBFME_PROFILE_CONTENTDB") == "1"
var _profile_db_started_ms := 0


func _profile_db(label: String, mark: int) -> void:
	if not _profile_db_enabled:
		return
	var now := Time.get_ticks_msec()
	print("CONTENTDB_PROFILE stage=%s delta_ms=%d total_ms=%d" % [label, now - mark, now - _profile_db_started_ms])


## Fine-grained accumulators for work that happens INSIDE a worker-pool fan-out,
## where "time since the previous mark" says nothing useful. Each bucket records
## a call count and summed microseconds; the totals are printed once per reload.
## Guarded by the asset-exists mutex because validation runs concurrently.
## Only touched when OPENBFME_PROFILE_CONTENTDB=1, so the release path is a
## single boolean test.
##
## Gated SEPARATELY from the coarse stage table (OPENBFME_PROFILE_CONTENTDB_DEEP=1)
## because each bucket costs a mutex acquisition, and taking one per
## resolve_pack_path call across eight validation threads changes the very
## contention it is trying to measure - the coarse table read 4,775 ms with the
## deep probes on and 3,055 ms with them off, on the same code.
var _probe_calls: Dictionary = {}
var _probe_usec: Dictionary = {}
var _probe_enabled := OS.get_environment("OPENBFME_PROFILE_CONTENTDB_DEEP") == "1"


func _probe(label: String, started_usec: int) -> void:
	if not _probe_enabled:
		return
	var spent := Time.get_ticks_usec() - started_usec
	_asset_exists_mutex.lock()
	_probe_calls[label] = int(_probe_calls.get(label, 0)) + 1
	_probe_usec[label] = int(_probe_usec.get(label, 0)) + spent
	_asset_exists_mutex.unlock()


func _probe_count(label: String) -> void:
	if not _probe_enabled:
		return
	_asset_exists_mutex.lock()
	_probe_calls[label] = int(_probe_calls.get(label, 0)) + 1
	_asset_exists_mutex.unlock()


func _probe_report() -> void:
	if not _probe_enabled:
		return
	var labels := _probe_calls.keys()
	labels.sort()
	for label_value in labels:
		var label := String(label_value)
		print("CONTENTDB_PROBE %-34s calls=%-8d ms=%d" % [
			label, int(_probe_calls.get(label, 0)), int(_probe_usec.get(label, 0)) / 1000
		])
	_probe_calls.clear()
	_probe_usec.clear()


func _ready() -> void:
	BootProfile.mark("autoload:ModLoader+SimClock")
	reload()
	BootProfile.mark("autoload:ContentDB.reload")

func reload() -> void:
	_profile_db_started_ms = Time.get_ticks_msec()
	_asset_exists_cache.clear()
	# Same generation lifetime as _asset_exists_cache: both describe a pack tree
	# that is immutable only while a load generation is active.
	_frozen_resolutions.clear()
	# Per-generation registry snapshots (see _registry_snapshot) must never
	# outlive the tables they were copied from.
	_registry_snapshots.clear()
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
	spellbook_runtime.clear()
	spellbook_visual_bindings.clear()
	spellbook_unconverted_visuals.clear()
	playable_unit_runtimes.clear()
	playable_unit_runtime_pack_index.clear()
	bundle_object_pack_index.clear()
	animation_capability_pack_index.clear()
	skipped_playable_unit_documents.clear()
	playable_structure_runtimes.clear()
	animation_capabilities.clear()
	bundle_maps.clear()
	catalog_map_ids.clear()
	retail_ui_images.clear()
	retail_strings.clear()
	retail_audio_events.clear()
	retail_audio_multisounds.clear()
	retail_audio_samples.clear()
	globals.clear()
	damage_matrix.clear()
	music_document.clear()
	pack_meta.clear()
	pack_roots.clear()
	asset_roots.clear()
	var scan_mark := Time.get_ticks_msec()
	var scanned_roots := ModLoader.list_pack_roots()
	_profile_db("list_pack_roots(%d)" % scanned_roots.size(), scan_mark)
	for root in scanned_roots:
		var pack_mark := Time.get_ticks_msec()
		_load_pack(root)
		_profile_db("pack:%s" % root.get_file(), pack_mark)
	_probe_report()
	# Self-reported, because reload() is no longer bracketed by two autoload
	# marks: it is reached lazily now, so "time since the previous mark" would
	# attribute it to whatever stage happened to call it. measure() states the
	# work actually done, which is what a budget should be asserted against.
	BootProfile.measure("ContentDB.reload", Time.get_ticks_msec() - _profile_db_started_ms)
	Events.content_reloaded.emit()
	# `factions`/`units`/`buildings` are the LEGACY demo registries (the bundled
	# demo directory), not the retail content census: they read like a content
	# count and are not one. The playable* counters below come from the real
	# runtime registries the game actually fields.
	print("[ContentDB] legacy-demo: packs=%d units=%d buildings=%d factions=%d powers=%d research=%d maps=%d" % [
		pack_meta.size(), units.size(), buildings.size(), factions.size(), powers.size(), research.size(), maps.size()
	])
	var playable_factions := get_playable_faction_ids()
	print("[ContentDB] playable: factions=%d (%s) units=%d structures=%d skipped_units=%d" % [
		playable_factions.size(),
		", ".join(playable_factions),
		playable_unit_runtimes.size(),
		playable_structure_runtimes.size(),
		skipped_playable_unit_documents.size(),
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
	var dirs_mark := Time.get_ticks_msec()
	_load_dir_into(ModLoader.resolve_pack_path(root, "units"), units, root)
	_load_dir_into(ModLoader.resolve_pack_path(root, "buildings"), buildings, root)
	_load_dir_into(ModLoader.resolve_pack_path(root, "factions"), factions, root)
	_load_dir_into(ModLoader.resolve_pack_path(root, "abilities"), abilities, root)
	_load_dir_into(ModLoader.resolve_pack_path(root, "powers"), powers, root)
	_load_dir_into(ModLoader.resolve_pack_path(root, "research"), research, root)
	_load_dir_into(ModLoader.resolve_pack_path(root, "maps"), maps, root)
	_profile_db("  legacy_dirs", dirs_mark)
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
	var pack_mark := Time.get_ticks_msec()
	_load_declared_rows(root, String(declared.get("objects", "")), "objects", bundle_objects)
	_profile_db("  objects", pack_mark)
	_load_retail_unit_rules(root, String(declared.get("unitRules", "")))
	_load_ranger_runtime(root, String(declared.get("rangerRuntime", "")))
	_load_trebuchet_runtime(root, String(declared.get("trebuchetRuntime", "")))
	_load_spellbook_runtimes(root, declared)
	_load_cah_system_runtime(root, declared)
	_profile_db("  rules+runtimes+spellbook", pack_mark)
	_load_playable_unit_runtimes(root, declared)
	_profile_db("  playable_units", pack_mark)
	_load_playable_structure_runtimes(root, declared)
	_profile_db("  playable_structures", pack_mark)
	_load_declared_rows(root, String(declared.get("animationCapabilities", "")), "capabilities", animation_capabilities)
	_load_retail_ui_manifest(root, String(declared.get("uiManifest", "")))
	_load_retail_strings(root, String(declared.get("strings", "")))
	_profile_db("  capabilities+ui+strings", pack_mark)
	_load_retail_audio_manifest(root, String(declared.get("audioEvents", "")))
	_load_music_document(root, String(declared.get("music", "")))
	_profile_db("  audio_manifest", pack_mark)
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


func _load_music_document(root: String, relative: String) -> bool:
	## A malformed or foreign-schema music document is IGNORED, never merged:
	## half a music binding would resolve some factions and silently mute
	## others, which is worse than the honest "no music pack is installed".
	if relative == "":
		return false
	var document := _read_declared_document(root, relative)
	if String(document.get("schema", "")) != "openbfme.music" or int(document.get("schemaVersion", -1)) != 0:
		return false
	for key in ["factions", "playlists", "tracks"]:
		if typeof(document.get(key, null)) != TYPE_DICTIONARY:
			return false
	document["_source"] = ModLoader.resolve_pack_path(root, relative)
	document["_pack_root"] = root
	music_document = document
	return true


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
		# Authored catalog order is the map list's order; a map reached only
		# through a pack's entryMap is deliberately not enumerable, because a
		# host pack's entry map is not a skirmish menu offering.
		if not catalog_map_ids.has(map_id):
			catalog_map_ids.append(map_id)
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


func _load_spellbook_runtimes(root: String, declared: Dictionary) -> void:
	## Packs declare spellbook runtime documents as spellbook.<slug>. Prefer the
	## first valid openbfme.spellbook-runtime document; later packs may replace.
	var keys: Array[String] = []
	for value in declared.keys():
		var key := String(value)
		if key.begins_with("spellbook.") or key == "spellbookRuntime":
			keys.append(key)
	keys.sort_custom(func(a: String, b: String) -> bool: return a.naturalnocasecmp_to(b) < 0)
	for key in keys:
		var relative := String(declared.get(key, ""))
		if relative == "" or not ModLoader.is_safe_relative_path(relative):
			continue
		var document := _read_declared_document_bounded(root, relative, MAX_PLAYABLE_UNIT_RUNTIME_BYTES)
		if document.is_empty():
			continue
		if (
			String(document.get("schema", "")) != "openbfme.spellbook-runtime"
			or int(document.get("schemaVersion", -1)) != 0
		):
			continue
		var powers: Array = (((document.get("registration", {}) as Dictionary).get("powerTree", {}) as Dictionary).get("powers", []) as Array)
		if powers.is_empty():
			continue
		document["_source"] = ModLoader.resolve_pack_path(root, relative)
		document["_pack_root"] = root
		document["_pack_file_key"] = key
		_register_spellbook_visual_bindings(root, document)
		spellbook_runtime = document
		# Last valid wins across packs (matches other registry overwrite policy).


func _load_cah_system_runtime(root: String, declared: Dictionary) -> void:
	## Packs declare the Create-a-Hero class table as `cah.system`.
	##
	## Last valid wins across packs, matching every other registry here. A
	## document whose schema or version does not match EXACTLY is skipped rather
	## than adapted: the table is the only source of the attribute arithmetic,
	## and a half-understood table would produce heroes with quietly wrong
	## numbers instead of no heroes at all.
	var relative := String(declared.get("cah.system", declared.get("cahSystem", "")))
	if relative == "" or not ModLoader.is_safe_relative_path(relative):
		return
	var document := _read_declared_document_bounded(root, relative, MAX_PLAYABLE_UNIT_RUNTIME_BYTES)
	if document.is_empty():
		return
	if (
		String(document.get("schema", "")) != "openbfme.cah-system-runtime"
		or int(document.get("schemaVersion", -1)) != 0
	):
		return
	var registration: Dictionary = document.get("registration", {}) as Dictionary
	if (registration.get("classes", []) as Array).is_empty():
		return
	if (registration.get("attributeGroups", []) as Array).is_empty():
		return
	document["_source"] = ModLoader.resolve_pack_path(root, relative)
	document["_pack_root"] = root
	document["_pack_file_key"] = "cah.system"
	cah_system_runtime = document


func _register_spellbook_visual_bindings(root: String, document: Dictionary) -> void:
	## Publish one spellbook's converted effect geometry as ordinary bundle
	## objects so the existing presentation bridge can find it.
	##
	## A power's summoned objects are not playable units and have no
	## playableUnit.* document, so nothing ever put their models in
	## `bundle_objects`; every summon presented as the synthetic kit mesh. The
	## importer now converts the models retail authors on those objects and
	## records them here, including the horde-container rows whose presented art
	## is their authored MemberObject's (a horde marker draws nothing).
	##
	## Registration NEVER overwrites a row an objects.json or playableUnit.*
	## document already owns: those carry animation capabilities this static
	## effect binding does not.
	var presentation: Dictionary = (document.get("registration", {}) as Dictionary).get("presentation", {}) as Dictionary
	var bindings: Dictionary = presentation.get("visualBindings", {}) as Dictionary
	var objects: Dictionary = bindings.get("objects", {}) as Dictionary
	for object_id_value in objects.keys():
		var object_id := String(object_id_value)
		var row_value: Variant = objects[object_id_value]
		if object_id == "" or typeof(row_value) != TYPE_DICTIONARY:
			continue
		var row := (row_value as Dictionary).duplicate(true)
		row["_pack_root"] = root
		spellbook_visual_bindings[object_id] = row
		var status := String(row.get("status", ""))
		if status == "unconverted":
			var note := "%s:%s" % [object_id, String(row.get("reason", ""))]
			if not spellbook_unconverted_visuals.has(note):
				spellbook_unconverted_visuals.append(note)
			continue
		if status != "model" and status != "horde-member":
			continue  # authored-invisible stays invisible
		var model := String(row.get("model", ""))
		if model == "" or not ModLoader.is_safe_relative_path(model):
			continue
		if bundle_objects.has(object_id):
			continue
		bundle_objects[object_id] = {
			"id": object_id,
			"kind": "member",
			"displayName": object_id,
			"presentation": {"model": model},
			"_pack_root": root,
			"_source": ModLoader.resolve_pack_path(root, model),
			"_spellbookEffectObject": true,
			"_spellbookVisualStatus": status,
		}
		if not bundle_object_pack_index.has(object_id):
			bundle_object_pack_index[object_id] = []
		(bundle_object_pack_index[object_id] as Array).append(bundle_objects[object_id])


func _load_playable_unit_runtimes(root: String, declared: Dictionary) -> bool:
	## Load every well-formed playable unit from this pack. Invalid documents
	## are skipped with a diagnostic instead of discarding the whole faction —
	## incomplete cooks (missing auxiliary GLBs / residual W3D jobs) must not
	## blank the vertical slice roster.
	var keys: Array[String] = []
	for value in declared.keys():
		var key := String(value)
		if key.begins_with("playableUnit."):
			keys.append(key)
	keys.sort_custom(func(a: String, b: String) -> bool: return a.naturalnocasecmp_to(b) < 0)
	if keys.size() > MAX_PLAYABLE_UNIT_RUNTIMES_PER_PACK:
		push_warning("ContentDB: playable unit count exceeds pack limit at %s" % root)
		return false
	var pending: Dictionary = {}
	var pending_folded: Dictionary = {}
	var skipped: Array[String] = []
	var relatives: Array[String] = []
	for key in keys:
		relatives.append(String(declared.get(key, "")))
	var prefetch_mark := Time.get_ticks_msec()
	var prefetched := _prefetch_declared_documents(root, relatives, MAX_PLAYABLE_UNIT_RUNTIME_BYTES)
	_profile_db("    units.prefetch(%d)" % relatives.size(), prefetch_mark)
	_freeze_asset_resolutions(root, prefetched)
	var validate_mark := Time.get_ticks_msec()
	var validations := _validate_playable_unit_documents_batch(root, prefetched)
	_profile_db("    units.validate", validate_mark)
	var admit_mark := Time.get_ticks_msec()
	for key_index in keys.size():
		var key := keys[key_index]
		var relative := relatives[key_index]
		if relative == "" or not ModLoader.is_safe_relative_path(relative):
			skipped.append("%s:unsafe-path" % key)
			continue
		var document: Dictionary = prefetched[key_index]
		if document.has("_parse_error"):
			push_error("JSON parse failed: %s @ %s" % [String(document.get("_parse_error", "")), String(document.get("_parse_path", ""))])
			document = {}
		if not validations[key_index]:
			skipped.append("%s:invalid-runtime" % key)
			continue
		var object_id := String(document["objectId"])
		var folded := object_id.to_lower()
		if pending_folded.has(folded):
			skipped.append("%s:duplicate-id" % key)
			continue
		var collision := false
		for existing_id_value in playable_unit_runtimes.keys():
			if String(existing_id_value).to_lower() == folded and String(existing_id_value) != object_id:
				collision = true
				break
		if collision:
			skipped.append("%s:id-collision" % key)
			continue
		document["_source"] = ModLoader.resolve_pack_path(root, relative)
		document["_pack_root"] = root
		document["_pack_file_key"] = key
		pending[object_id] = document
		pending_folded[folded] = object_id
	_profile_db("    units.admit", admit_mark)
	var project_mark := Time.get_ticks_msec()
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
		var hero_displaced: Array[String] = []
		var hero_collision := false
		for route_value in production:
			var route := route_value as Dictionary
			if String(route.get("surface", "")) != "hero-roster":
				continue
			var ordinal_key := "%s:%d" % [String(route.get("producerObjectId", "")).to_lower(), int(route.get("rosterOrdinal", 0))]
			if hero_ordinals.has(ordinal_key) and String(hero_ordinals[ordinal_key]).to_lower() != object_id.to_lower():
				# Later-loaded packs (active selection loads last) replace the
				# earlier hero at this producer ordinal rather than skipping the
				# active roster. Same-pack / same-id reloads keep first-wins.
				var existing_id := String(hero_ordinals[ordinal_key])
				var existing_doc: Dictionary = playable_unit_runtimes.get(existing_id, {}) as Dictionary
				var existing_root := String(existing_doc.get("_pack_root", ""))
				var pending_root := String(document.get("_pack_root", ""))
				if existing_root != "" and pending_root != "" and existing_root != pending_root:
					if not hero_displaced.has(existing_id):
						hero_displaced.append(existing_id)
					hero_ordinals[ordinal_key] = object_id
					continue
				hero_collision = true
				break
			hero_ordinals[ordinal_key] = object_id
		if hero_collision:
			skipped.append("%s:hero-ordinal-collision" % object_id)
			continue
		for displaced_id in hero_displaced:
			if playable_unit_runtimes.has(displaced_id):
				playable_unit_runtimes.erase(displaced_id)
			var displaced_folded := displaced_id.to_lower()
			if playable_unit_runtime_pack_index.has(displaced_folded):
				playable_unit_runtime_pack_index.erase(displaced_folded)
		var projection := _playable_unit_projection(document)
		if projection.is_empty():
			skipped.append("%s:empty-projection" % object_id)
			continue
		projections[object_id] = projection
	_profile_db("    units.project", project_mark)
	var publish_mark := Time.get_ticks_msec()
	for object_id_value in projections.keys():
		var object_id := String(object_id_value)
		var document := pending[object_id] as Dictionary
		var projection := projections[object_id] as Dictionary
		playable_unit_runtimes[object_id] = document
		# Record every admitted per-pack copy: a shared unit's later pack
		# overwrites the flat slot above, and faction-scoped consumers resolve
		# their own pack's document from this index.
		var folded := object_id.to_lower()
		if not playable_unit_runtime_pack_index.has(folded):
			playable_unit_runtime_pack_index[folded] = []
		(playable_unit_runtime_pack_index[folded] as Array).append(document)
		var member := projection["member"] as Dictionary
		var capability := projection["capability"] as Dictionary
		var member_key := String(member["id"])
		# The projected capability only carries the states the shared adapter
		# maps from coreAnimations. When the pack also ships an authored
		# capability for the same object (e.g. the porter's construct work
		# loops), keep its extra states instead of silently dropping them.
		var replaced_object: Dictionary = bundle_objects.get(member_key, {}) as Dictionary
		var authored_capability: Dictionary = animation_capabilities.get(
			String(replaced_object.get("animationCapabilityId", "")), {}
		) as Dictionary
		if not authored_capability.is_empty():
			var projected_states: Dictionary = capability.get("states", {}) as Dictionary
			var authored_states: Dictionary = authored_capability.get("states", {}) as Dictionary
			for state_name_value in authored_states.keys():
				var state_name := String(state_name_value)
				if not projected_states.has(state_name):
					projected_states[state_name] = (authored_states[state_name] as Dictionary).duplicate(true)
			capability["states"] = projected_states
		member = _merge_projected_member(member, replaced_object, document)
		bundle_objects[member_key] = member
		animation_capabilities[String(capability["id"])] = capability
		# Shared units project the same member/capability ids from every faction
		# pack; the flat tables above keep only the last-loaded pack's rows.
		if not bundle_object_pack_index.has(member_key):
			bundle_object_pack_index[member_key] = []
		(bundle_object_pack_index[member_key] as Array).append(member)
		var capability_key := String(capability["id"])
		if not animation_capability_pack_index.has(capability_key):
			animation_capability_pack_index[capability_key] = []
		(animation_capability_pack_index[capability_key] as Array).append(capability)
	_profile_db("    units.publish", publish_mark)
	if not skipped.is_empty():
		for skipped_value in skipped:
			skipped_playable_unit_documents.append(String(skipped_value))
		push_warning(
			"ContentDB: skipped %d playable unit(s) in %s: %s"
			% [skipped.size(), root.get_file(), ", ".join(skipped)]
		)
	return true


func get_skipped_playable_unit_documents() -> Array[String]:
	return skipped_playable_unit_documents.duplicate()


func get_playable_faction_ids() -> Array[String]:
	## Faction slugs with at least one admitted playableUnit.* AND one
	## playableStructure.* runtime document, resolved through the manifest's
	## object-id prefixes. This is the real playable-content census; the legacy
	## `factions` dictionary counts a bundled demo directory instead.
	var prefixes: Dictionary = load(
		"res://src/retail_slice/retail_faction_manifest.gd"
	).FACTION_OBJECT_PREFIXES
	var result: Array[String] = []
	for faction_value in prefixes.keys():
		var faction_id := String(faction_value)
		var faction_prefixes: Array = prefixes[faction_value] as Array
		if (
			_registry_has_prefix(playable_unit_runtimes, faction_prefixes)
			and _registry_has_prefix(playable_structure_runtimes, faction_prefixes)
		):
			result.append(faction_id)
	result.sort()
	return result


func _registry_has_prefix(registry: Dictionary, prefixes: Array) -> bool:
	for object_id_value in registry.keys():
		var object_id := String(object_id_value).to_lower()
		for prefix_value in prefixes:
			if object_id.begins_with(String(prefix_value).to_lower()):
				return true
	return false


func get_playable_unit_runtime_pack_index() -> Dictionary:
	## Casefolded object id to the load-ordered per-pack documents admitted to
	## the playable-unit registry. Entries with two or more copies are shared
	## retail units whose flat-registry document is only the last-loaded pack's.
	return _registry_snapshot("unit_pack_index", playable_unit_runtime_pack_index)


func _collect_document_asset_references(documents: Array, into: Dictionary) -> void:
	## Every asset path the playable-unit / playable-structure validators will
	## probe, gathered without validating anything. Over-collecting is harmless:
	## a reference that no check reaches is resolved once and never read, and a
	## reference this misses is still resolved by the validator itself.
	for document_value in documents:
		if typeof(document_value) != TYPE_DICTIONARY:
			continue
		var registration_value: Variant = (document_value as Dictionary).get("registration")
		if typeof(registration_value) != TYPE_DICTIONARY:
			continue
		var registration := registration_value as Dictionary
		var visual: Variant = registration.get("visual")
		if typeof(visual) == TYPE_DICTIONARY:
			var components: Variant = (visual as Dictionary).get("components")
			if typeof(components) == TYPE_ARRAY:
				for component_value in components as Array:
					if typeof(component_value) == TYPE_DICTIONARY:
						var output := String((component_value as Dictionary).get("output", ""))
						if output != "":
							into[output] = true
		for bindings_key in ["imageBindings", "audioBindings"]:
			var bindings: Variant = registration.get(bindings_key)
			if typeof(bindings) != TYPE_DICTIONARY:
				continue
			for paths_value in (bindings as Dictionary).values():
				var paths: Array = paths_value if typeof(paths_value) == TYPE_ARRAY else [paths_value]
				for path_value in paths:
					if typeof(path_value) == TYPE_STRING and String(path_value) != "":
						into[String(path_value)] = true


func _freeze_asset_resolutions(root: String, documents: Array) -> void:
	## Resolve, here on the main thread, every asset reference the validation
	## fan-out is about to probe, into a table the fan-out only READS.
	##
	## WHY THIS EXISTS. MEASURED: the eight-thread validation fan-out spent
	## 1,658 ms of wall time doing 1,101 ms of single-threaded work. It was not
	## short of parallelism, it was queueing - every resolve_asset took roughly
	## ten turns on ModLoader's path mutex and ContentDB's asset-exists mutex,
	## several of them with a filesystem syscall held underneath. Doing the
	## resolution once, up front, turns each of the fan-out's 15,177 probes into
	## one lock-free dictionary read.
	##
	## NOTHING IS SKIPPED. Every reference is resolved in full, through the same
	## resolve_asset the validator would have called, and the validator still
	## checks every one of its results exactly as before. What changes is who
	## does the resolving and how many times: once per DISTINCT reference instead
	## of once per occurrence.
	##
	## ADMISSION RULE. A resolution enters the table only when the PREFERRED pack
	## root produced it. resolve_asset tries the preferred root first and returns
	## on the first hit, so such an answer is a function of (root, reference)
	## alone. Anything else - a reference that fell through to another pack root,
	## or that resolved nowhere - depends on `pack_roots`, which is still growing
	## as packs load, so it is deliberately NOT frozen and is re-derived in full
	## on every call.
	var references: Dictionary = {}
	_collect_document_asset_references(documents, references)
	if references.is_empty():
		return
	# Warm the directory index in parallel first. The directory list is a hint
	# derived the same way resolve_asset builds its candidate; a wrong guess only
	# wastes one enumeration and cannot change any answer (see
	# ModLoader.warm_directory_index).
	var warm_mark := Time.get_ticks_msec()
	var directories: Dictionary = {}
	var root_prefix := root.replace("\\", "/").trim_suffix("/") + "/"
	for reference_value in references.keys():
		var reference := String(reference_value).replace("\\", "/")
		if not ModLoader.is_safe_relative_path(reference):
			continue
		var pack_relative := reference if reference.begins_with("assets/") else "assets/" + reference
		# PURE STRING DERIVATION, DELIBERATELY. Calling resolve_pack_path here
		# instead cost 756 ms, because it did the whole cold per-file link walk
		# serially and left warm_directory_index with nothing to parallelise.
		# This is only naming directories to enumerate; resolve_asset below still
		# performs the full containment and link proof on every reference.
		directories[(root_prefix + pack_relative).get_base_dir()] = true
	ModLoader.warm_directory_index(directories.keys())
	_profile_db("    units.warm_dirs(%d)" % directories.size(), warm_mark)
	var freeze_mark := Time.get_ticks_msec()
	var keys: Array = references.keys()
	var resolutions: Array = []
	resolutions.resize(keys.size())
	# Safe to fan out now that every directory the references name is indexed:
	# resolve_asset performs no filesystem call here, so the path mutexes are
	# held for dictionary reads only and the convoy that made the ORIGINAL
	# fan-out slower than a single thread cannot form. Each element writes its
	# own pre-sized slot; _frozen_resolutions is populated on the main thread
	# below, so nothing reads a table while it is being written.
	if keys.size() == 1 or OS.get_processor_count() <= 1:
		for index in keys.size():
			resolutions[index] = resolve_asset(String(keys[index]), root)
	else:
		var group := WorkerThreadPool.add_group_task(
			func(element: int) -> void:
				resolutions[element] = resolve_asset(String(keys[element]), root),
			keys.size()
		)
		WorkerThreadPool.wait_for_group_task_completion(group)
	var frozen := 0
	for index in keys.size():
		var resolved := String(resolutions[index])
		if resolved == "":
			continue
		# Prove the preferred root owns this resolution before freezing it.
		if not ModLoader.path_is_within(root, resolved):
			continue
		_frozen_resolutions[root + "\n" + String(keys[index])] = resolved
		frozen += 1
	_profile_db("    units.freeze(%d/%d)" % [frozen, references.size()], freeze_mark)


func _validate_playable_unit_documents_batch(root: String, documents: Array) -> Array:
	## Per-document validation is read-only against the registries (asset path
	## probes go through mutex-guarded caches), so it fans out across the worker
	## pool. The returned bool array parallels `documents`.
	var results: Array = []
	results.resize(documents.size())
	if documents.size() > 1 and OS.get_processor_count() > 1:
		var group := WorkerThreadPool.add_group_task(
			func(element: int) -> void:
				var element_mark := Time.get_ticks_usec()
				results[element] = _validate_playable_unit_runtime(root, documents[element])
				_probe("validate.unit.document", element_mark),
			documents.size()
		)
		WorkerThreadPool.wait_for_group_task_completion(group)
	else:
		for index in documents.size():
			var index_mark := Time.get_ticks_usec()
			results[index] = _validate_playable_unit_runtime(root, documents[index])
			_probe("validate.unit.document", index_mark)
	return results


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
			or surface not in ["command-socket", "hero-roster", "banner-carrier"]
			or (surface == "command-socket" and (slot < 1 or roster_ordinal != 0))
			or (surface == "hero-roster" and (roster_ordinal < 1 or slot != 0))
			# Engine banner surface: horde-spawned carriers have no retail
			# command slot/ordinal; producer is the banner object itself.
			or (surface == "banner-carrier" and (slot != 0 or roster_ordinal != 0))
			or (category == "hero" and surface == "command-socket" and not _has_authored_command_socket_evidence(route))
			or (category != "hero" and surface == "hero-roster")
			or (surface == "banner-carrier" and category == "hero")
		):
			return false
	if typeof(registration.composition) != TYPE_DICTIONARY or typeof(registration.gameplay) != TYPE_DICTIONARY:
		return false
	if registration.has("abilities"):
		# Additive hero-ability contract (converter-emitted SPECIAL_POWER rows).
		# Presence is optional so older packs and fixtures keep loading; when
		# present the rows must be well formed.
		var abilities_value: Variant = registration.get("abilities")
		if typeof(abilities_value) != TYPE_ARRAY:
			return false
		for ability_value in abilities_value as Array:
			if typeof(ability_value) != TYPE_DICTIONARY:
				return false
			var ability := ability_value as Dictionary
			var effect: Variant = ability.get("effect")
			var implementation: Variant = ability.get("implementation")
			if (
				String(ability.get("id", "")) == ""
				# TOGGLE_WEAPONSET rows author no SpecialPower template.
				or (String(ability.get("specialPowerId", "")) == "" and String(ability.get("command", "")) != "TOGGLE_WEAPONSET")
				or int(ability.get("slot", 0)) < 1
				or String(ability.get("targeting", "")) not in ["self", "point", "enemy-object"]
				or typeof(ability.get("button")) != TYPE_DICTIONARY
				or typeof(effect) != TYPE_DICTIONARY
				or String((effect as Dictionary).get("kind", "")) not in ["none", "weapon-blast", "heal", "summon", "attribute-modifier", "leadership-aura", "weapon-toggle", "terror", "mount-toggle", "capture-building", "experience-grant", "arrow-storm", "stealth-toggle", "teleport", "curse", "leadership-strip"]
				or typeof(implementation) != TYPE_DICTIONARY
				or String((implementation as Dictionary).get("status", "")) not in ["implemented", "unimplemented", "passive"]
			):
				return false
	if typeof(registration.visual) != TYPE_DICTIONARY or typeof(registration.ui) != TYPE_DICTIONARY:
		return false
	var visual := registration.visual as Dictionary
	var components: Variant = visual.get("components")
	var core: Variant = visual.get("coreAnimations")
	if typeof(components) != TYPE_ARRAY or (components as Array).is_empty() or typeof(core) != TYPE_DICTIONARY:
		return false
	var default_count := 0
	var default_resolved := 0
	for component_value in components as Array:
		if typeof(component_value) != TYPE_DICTIONARY:
			return false
		var component := component_value as Dictionary
		var output := String(component.get("output", ""))
		var is_default := bool(component.get("default", false))
		if output == "":
			return false
		var component_mark := Time.get_ticks_usec()
		var resolved := resolve_asset(output, root)
		_probe("validate.unit.component_asset", component_mark)
		# Default presentation must resolve. Auxiliary/conditional skins
		# (upgrade shells, death models, mounted variants) may be absent when
		# an incomplete cook drops residual W3D jobs — those units still field.
		if is_default:
			default_count += 1
			if resolved != "":
				default_resolved += 1
		elif resolved == "" and not is_default:
			continue
	if default_count != 1 or default_resolved != 1:
		return false
	for state_key_value in (core as Dictionary).keys():
		var state_key := String(state_key_value)
		var state_value: Variant = (core as Dictionary)[state_key_value]
		# Standard clip bindings are non-empty arrays of {identifier}.
		# Death may instead be a separate-model binding object produced by the
		# pack compiler (binding=separate-model + output glb).
		if typeof(state_value) == TYPE_DICTIONARY:
			var death_row := state_value as Dictionary
			if (
				state_key != "death"
				or String(death_row.get("binding", "")) != "separate-model"
				or String(death_row.get("output", "")).strip_edges() == ""
			):
				return false
			# Death GLB may be absent in incomplete cooks; the unit still fields
			# on its default visual. Do not fail-closed the whole registry.
			continue
		if typeof(state_value) != TYPE_ARRAY or (state_value as Array).is_empty():
			return false
		for binding_value in state_value as Array:
			if typeof(binding_value) != TYPE_DICTIONARY or String((binding_value as Dictionary).get("identifier", "")) == "":
				return false
	for bindings_key in ["imageBindings", "audioBindings"]:
		var bindings: Variant = registration.get(bindings_key)
		if typeof(bindings) != TYPE_DICTIONARY:
			return false
		# Built once per binding GROUP, not per path: this loop runs ~15,000
		# times a boot and a concatenation per iteration is not free even when
		# the probe that consumes it is switched off.
		var binding_probe_label: String = "validate.unit.binding_asset:" + String(bindings_key)
		for paths_value in (bindings as Dictionary).values():
			var paths: Array = paths_value if typeof(paths_value) == TYPE_ARRAY else [paths_value]
			for path_value in paths:
				if typeof(path_value) != TYPE_STRING:
					return false
				var binding_mark := Time.get_ticks_usec()
				var binding_resolved := resolve_asset(String(path_value), root)
				_probe(binding_probe_label, binding_mark)
				if binding_resolved == "":
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
		# Command-referenced ids retail itself never localizes (RotWK added
		# CONTROLBAR:ConstructBlackRiderHorde to commandbutton.ini and never
		# added the string to lotr.str). The compiler records them explicitly;
		# treating their absence as a broken pack deleted the whole unit from
		# the roster and left a dead barracks button.
		var source_null_string_ids: Dictionary = {}
		if registration.has("sourceNullStringIds"):
			var source_null_value: Variant = registration.get("sourceNullStringIds")
			if typeof(source_null_value) != TYPE_ARRAY:
				return false
			for source_null_id_value in source_null_value as Array:
				if typeof(source_null_id_value) != TYPE_STRING or String(source_null_id_value).strip_edges() == "":
					return false
				if string_bindings.has(String(source_null_id_value)):
					return false
				source_null_string_ids[String(source_null_id_value)] = true
		var required_string_ids: Dictionary = {}
		for command_value in (registration.ui as Dictionary).get("commands", []):
			if typeof(command_value) != TYPE_DICTIONARY:
				return false
			var fields := (command_value as Dictionary).get("fields", {}) as Dictionary
			for field in ["TextLabel", "DescriptLabel"]:
				for string_id_value in fields.get(field, []):
					required_string_ids[String(string_id_value)] = true
		# Coverage, not exact size: ability-bearing heroes bind strings for
		# their powers (Athelas, BladeMaster, …) beyond the train command's
		# label/tooltip. Every command-referenced id must be bound with
		# non-empty text; additional bound ids are valid pack content.
		for required_id_value in required_string_ids.keys():
			var required_id := String(required_id_value)
			if source_null_string_ids.has(required_id):
				continue
			if typeof(string_bindings.get(required_id)) != TYPE_STRING or String(string_bindings.get(required_id, "")).strip_edges() == "":
				return false
		for string_id_value in string_bindings.keys():
			if String(string_id_value).strip_edges() == "" or typeof(string_bindings[string_id_value]) != TYPE_STRING or String(string_bindings[string_id_value]).strip_edges() == "":
				return false
	return true


## Retail model conditions that name an addressable special ability rather than
## a generic combat pose. Mirrors `_ABILITY_STATE_TOKENS` in the importer's
## `playable_unit_pack_compiler.py`; kept here as well because the derivation
## also runs against packs cooked before the compiler sealed the block.
const ABILITY_STATE_CONDITIONS: Dictionary = {
	"SPECIAL_WEAPON_ONE": "specialWeaponOne",
	"SPECIAL_WEAPON_TWO": "specialWeaponTwo",
	"SPECIAL_WEAPON_THREE": "specialWeaponThree",
	"USING_SPECIAL_ABILITY": "usingSpecialAbility",
}
## Retail's four-phase cast envelope (boromir.ini:127-178 authors all four for
## SPECIAL_POWER_1). The bare condition is the fire frame itself.
const ABILITY_PHASE_CONDITIONS: Dictionary = {
	"UNPACKING": "unpack",
	"PREPARING": "prepare",
	"PACKING": "pack",
}
const ABILITY_DEFAULT_PHASE := "cast"


func _ability_animation_key(conditions: Array) -> Array:
	## `[abilityState, phase]` named by one AnimationState's conditions, or an
	## empty array when the row names no addressable ability — or names more
	## than one, which cannot be addressed unambiguously and is left alone
	## rather than guessed at.
	var tokens: Array[String] = []
	for value in conditions:
		tokens.append(String(value).to_upper())
	var state := ""
	for token in tokens:
		var mapped := String(ABILITY_STATE_CONDITIONS.get(token, ""))
		if mapped == "":
			if token.begins_with("SPECIAL_POWER_") and token.substr(14).is_valid_int():
				mapped = "specialPower%d" % token.substr(14).to_int()
			elif token.begins_with("PACKING_TYPE_") and token.substr(13).is_valid_int():
				mapped = "packingType%d" % token.substr(13).to_int()
		if mapped == "":
			continue
		if state != "" and state != mapped:
			return []
		state = mapped
	if state == "":
		return []
	var phase := ABILITY_DEFAULT_PHASE
	for token in tokens:
		var mapped_phase := String(ABILITY_PHASE_CONDITIONS.get(token, ""))
		if mapped_phase == "":
			continue
		if phase != ABILITY_DEFAULT_PHASE:
			return []
		phase = mapped_phase
	return [state, phase]


func _ability_animation_states(visual: Dictionary) -> Dictionary:
	## Capability states for the authored ability poses this unit can address,
	## keyed `ability:<abilityState>:<phase>`.
	##
	## A cast used to be indistinguishable from a normal swing: the pack
	## compiler folds every SPECIAL_WEAPON_* AnimationState into the generic
	## "attack" semantic state, so Gandalf's staff-lowering wizard blast clip
	## (`GUGandalfG_SKL.GUGandalfG_SPCL`, gandalf.ini:305) reached the cooked
	## mesh but nothing could ask for it by name. The core lane is deliberately
	## left as-is — this is a parallel, additive index over the very same
	## authored rows.
	##
	## Reads the compiler's sealed `visual.abilityAnimations` block when the
	## pack carries one, and otherwise derives the same mapping from
	## `visual.authoredAnimationStates`, which every cooked pack already ships.
	var result: Dictionary = {}
	var sealed_value: Variant = visual.get("abilityAnimations", null)
	if typeof(sealed_value) == TYPE_DICTIONARY:
		for state_value in (sealed_value as Dictionary).keys():
			var phases_value: Variant = (sealed_value as Dictionary)[state_value]
			if typeof(phases_value) != TYPE_DICTIONARY:
				continue
			for phase_value in (phases_value as Dictionary).keys():
				var bindings: Variant = (phases_value as Dictionary)[phase_value]
				if typeof(bindings) != TYPE_ARRAY:
					continue
				_append_ability_state(
					result, String(state_value), String(phase_value), bindings as Array
				)
		return result
	var authored: Variant = visual.get("authoredAnimationStates", [])
	if typeof(authored) != TYPE_ARRAY:
		return result
	var grouped: Dictionary = {}
	for row_value in authored as Array:
		if typeof(row_value) != TYPE_DICTIONARY:
			continue
		var row := row_value as Dictionary
		if String(row.get("runtimeSupport", "")).begins_with("excluded"):
			continue
		var conditions: Variant = row.get("conditions", [])
		if typeof(conditions) != TYPE_ARRAY:
			continue
		var key := _ability_animation_key(conditions as Array)
		if key.is_empty():
			continue
		var group_key := "%s\u001f%s" % [String(key[0]), String(key[1])]
		if not grouped.has(group_key):
			grouped[group_key] = []
		(grouped[group_key] as Array).append(row)
	for group_key_value in grouped.keys():
		var parts := String(group_key_value).split("\u001f")
		_append_ability_state(result, parts[0], parts[1], grouped[group_key_value] as Array)
	return result


func _append_ability_state(
	result: Dictionary, state: String, phase: String, bindings: Array
) -> void:
	var clips: Array[String] = []
	for binding_value in bindings:
		if typeof(binding_value) != TYPE_DICTIONARY:
			continue
		var identifier := String((binding_value as Dictionary).get("identifier", ""))
		if identifier == "":
			continue
		if not clips.has(identifier):
			clips.append(identifier)
	if clips.is_empty():
		return
	clips.sort_custom(func(a: String, b: String) -> bool: return a.naturalnocasecmp_to(b) < 0)
	result["ability:%s:%s" % [state, phase]] = {
		"clips": clips,
		"mode": "loop" if phase == "prepare" else "once",
		"useWeaponTiming": false,
	}


func _merge_projected_member(
	member: Dictionary, replaced: Dictionary, document: Dictionary
) -> Dictionary:
	## Overlay the playable-unit projection onto the bundle object it supersedes.
	##
	## The playable-unit lane owns the presentation model, the animation
	## capability AND the gameplay numbers — `_gameplay_rules` deliberately
	## routes a unit to the lane's own `registration.simulation` when the bundle
	## object carries no `simulation` block, so that block must stay absent here.
	##
	## What the lane does not own is the unit's *identity art*. The bare
	## projection carries only `presentation.model`, so assigning it over
	## `bundle_objects` dropped `presentation.icon` and
	## `presentation.commandIcon` outright and every unit the lane admitted lost
	## its portrait and command icon. The composed six-faction pack made that
	## visible everywhere because it admits far more playable-unit documents
	## than the single-faction cook did.
	##
	## So: inherit the presentation-only keys, nothing else. Units the legacy
	## `data/objects.json` lane never published have nothing to inherit, so
	## their icons come from the playable-unit document's own authored
	## `imageBindings` (`ui.portraitImageIds` for the portrait, the train
	## command's `ButtonImage` for the command icon). Nothing is invented: an id
	## the pack did not bind simply stays empty.
	var merged: Dictionary = member.duplicate(true)
	var inherited_presentation: Dictionary = (
		replaced.get("presentation", {}) as Dictionary
	)
	if String(merged.get("displayName", "")) == "" and String(replaced.get("displayName", "")) != "":
		merged["displayName"] = replaced["displayName"]
	var presentation: Dictionary = (member.get("presentation", {}) as Dictionary).duplicate(true)
	for key in ["icon", "commandIcon"]:
		if String(presentation.get(key, "")) == "" and String(inherited_presentation.get(key, "")) != "":
			presentation[key] = inherited_presentation[key]
	var registration: Dictionary = document.get("registration", {}) as Dictionary
	var image_bindings: Dictionary = registration.get("imageBindings", {}) as Dictionary
	var ui: Dictionary = registration.get("ui", {}) as Dictionary
	if String(presentation.get("icon", "")) == "":
		for portrait_id_value in ui.get("portraitImageIds", []) as Array:
			var relative := String(image_bindings.get(String(portrait_id_value), ""))
			if relative != "":
				presentation["icon"] = relative
				break
	if String(presentation.get("commandIcon", "")) == "":
		for command_value in ui.get("commands", []) as Array:
			if typeof(command_value) != TYPE_DICTIONARY:
				continue
			var fields: Dictionary = (command_value as Dictionary).get("fields", {}) as Dictionary
			for image_id_value in fields.get("ButtonImage", []) as Array:
				var relative := String(image_bindings.get(String(image_id_value), ""))
				if relative != "":
					presentation["commandIcon"] = relative
					break
			if String(presentation.get("commandIcon", "")) != "":
				break
	merged["presentation"] = presentation
	return merged


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
		var raw_bindings: Variant = core[state_value]
		var ranked: Array[Dictionary] = []
		if typeof(raw_bindings) == TYPE_ARRAY:
			for binding_value in raw_bindings as Array:
				if typeof(binding_value) != TYPE_DICTIONARY:
					continue
				var binding := binding_value as Dictionary
				var identifier := String(binding.get("identifier", ""))
				if identifier == "":
					continue
				# Prefer generic locomotion/idle clips (fewest conditions) so
				# BACKING_UP / WANDER variants do not become the default move.
				var conditions: Array = binding.get("conditions", []) as Array
				ranked.append({
					"identifier": identifier,
					"condition_count": conditions.size(),
					"rank": _playable_clip_rank(state, identifier, conditions),
				})
		elif typeof(raw_bindings) == TYPE_DICTIONARY:
			# separate-model death: no clip identifiers on the primary visual.
			pass
		else:
			continue
		ranked.sort_custom(func(a: Dictionary, b: Dictionary) -> bool:
			if int(a["rank"]) != int(b["rank"]):
				return int(a["rank"]) < int(b["rank"])
			if int(a["condition_count"]) != int(b["condition_count"]):
				return int(a["condition_count"]) < int(b["condition_count"])
			return String(a["identifier"]).naturalnocasecmp_to(String(b["identifier"])) < 0
		)
		var clips: Array[String] = []
		for entry in ranked:
			var identifier := String(entry["identifier"])
			if not clips.has(identifier):
				clips.append(identifier)
		states[state] = {
			"clips": clips,
			"mode": "loop" if state in ["idle", "move"] else "once",
			"useWeaponTiming": state == "attack",
		}
	var ability_states := _ability_animation_states(visual)
	for ability_state_value in ability_states.keys():
		var ability_state := String(ability_state_value)
		if not states.has(ability_state):
			states[ability_state] = ability_states[ability_state_value]
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


func _playable_clip_rank(state: String, identifier: String, conditions: Array) -> int:
	## Lower is preferred for the default clip of a locomotion/combat state.
	var id_lower := identifier.to_lower()
	var conds: Array[String] = []
	for value in conditions:
		conds.append(String(value).to_upper())
	if state == "move":
		if conds.has("BACKING_UP") or id_lower.contains("baka") or id_lower.contains("back"):
			return 40
		if conds.has("WANDER") or id_lower.contains("wlk"):
			return 30
		if conds.has("EMOTION_LOOK_TO_SKY"):
			return 25
		if conds == ["MOVING"] or (conds.is_empty() and (id_lower.contains("run") or id_lower.contains("wlk") == false)):
			return 0 if id_lower.contains("run") else 5
		if conds.has("MOVING"):
			return 10
		return 20
	if state == "idle":
		if conds.is_empty():
			return 0 if id_lower.contains("idla") or id_lower.contains("idle") else 5
		return 15
	if state == "attack":
		if conds.has("MOVING"):
			return 20
		return conds.size()
	return conds.size()


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
	## Load well-formed playable structures; skip invalid residual W3D/lifecycle
	## gaps without discarding the rest of the faction base (fortress/farm/etc.).
	var keys: Array[String] = []
	for value in declared.keys():
		var key := String(value)
		if key.begins_with("playableStructure."):
			keys.append(key)
	keys.sort_custom(func(a: String, b: String) -> bool: return a.naturalnocasecmp_to(b) < 0)
	if keys.size() > MAX_PLAYABLE_STRUCTURE_RUNTIMES_PER_PACK:
		push_warning("ContentDB: playable structure count exceeds pack limit at %s" % root)
		return false
	var pending: Dictionary = {}
	var pending_folded: Dictionary = {}
	var skipped: Array[String] = []
	var relatives: Array[String] = []
	for key in keys:
		relatives.append(String(declared.get(key, "")))
	var prefetched := _prefetch_declared_documents(root, relatives, MAX_PLAYABLE_STRUCTURE_RUNTIME_BYTES)
	for key_index in keys.size():
		var key := keys[key_index]
		var relative := relatives[key_index]
		if relative == "" or not ModLoader.is_safe_relative_path(relative):
			skipped.append("%s:unsafe-path" % key)
			continue
		var document: Dictionary = prefetched[key_index]
		if document.has("_parse_error"):
			push_error("JSON parse failed: %s @ %s" % [String(document.get("_parse_error", "")), String(document.get("_parse_path", ""))])
			document = {}
		if not _validate_playable_structure_runtime(root, document):
			skipped.append("%s:invalid-runtime" % key)
			continue
		var object_id := String(document["objectId"])
		var folded := object_id.to_lower()
		if pending_folded.has(folded):
			skipped.append("%s:duplicate-id" % key)
			continue
		var collision := false
		for existing_id_value in playable_structure_runtimes.keys():
			if String(existing_id_value).to_lower() == folded and String(existing_id_value) != object_id:
				collision = true
				break
		if collision:
			skipped.append("%s:id-collision" % key)
			continue
		document["_source"] = ModLoader.resolve_pack_path(root, relative)
		document["_pack_root"] = root
		document["_pack_file_key"] = key
		pending[object_id] = document
		pending_folded[folded] = object_id
	for object_id_value in pending.keys():
		playable_structure_runtimes[String(object_id_value)] = pending[object_id_value]
	if not skipped.is_empty():
		push_warning(
			"ContentDB: skipped %d playable structure(s) in %s: %s"
			% [skipped.size(), root.get_file(), ", ".join(skipped)]
		)
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
	return _validate_playable_structure_lifecycle(root, presentation.get("buildingLifecycle"), gameplay, registration.get("production") as Dictionary, maximum_health, object_id)


func _validate_playable_structure_production(value: Variant) -> bool:
	if typeof(value) != TYPE_DICTIONARY:
		return false
	var production := value as Dictionary
	var evidence := String(production.get("evidence", ""))
	var routes_value: Variant = production.get("routes")
	if evidence not in PLAYABLE_STRUCTURE_PRODUCTION_EVIDENCE or typeof(routes_value) != TYPE_ARRAY:
		return false
	var routes := routes_value as Array
	var authored := evidence in ["authored-construct-command", "authored-wall-upgrade-command"]
	if authored and routes.is_empty():
		return false
	if not authored and not routes.is_empty():
		return false
	for route_value in routes:
		if typeof(route_value) != TYPE_DICTIONARY:
			return false
		var route := route_value as Dictionary
		if (
			String(route.get("surface", "")) not in ["construct", "wall-upgrade"]
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


func _validate_playable_structure_lifecycle(root: String, value: Variant, gameplay: Dictionary, production: Dictionary, maximum_health: int, source_object_id: String) -> bool:
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
	var health := gameplay.get("health") as Dictionary
	var primary := health.get("primary") as Dictionary
	var damaged := _playable_structure_health_number(primary.get("maxHealthDamaged"))
	var really_damaged := _playable_structure_health_number(primary.get("maxHealthReallyDamaged"))
	var has_damage_rule := damaged > 0 and really_damaged > 0
	if not has_damage_rule and (damaged > 0 or really_damaged > 0):
		return false
	var facts := lifecycle.get("simulationFacts") as Dictionary
	var construction_value: Variant = facts.get("construction")
	if typeof(construction_value) != TYPE_DICTIONARY:
		return false
	var construction := construction_value as Dictionary
	var construction_omitted := construction.has("status")
	if construction_omitted:
		# The omission marker must match the production evidence class.
		var status := String(construction.get("status", ""))
		var evidence := String(production.get("evidence", ""))
		if status == "never-constructed-engine-spawned-composite":
			if evidence != "engine-spawned-composite":
				return false
		elif status == "never-constructed-wall-template":
			if evidence != "wall-template":
				return false
		elif status != "no-authored-construction-states":
			return false
	var expected_phases: Array[String] = []
	for candidate_value in PLAYABLE_STRUCTURE_PRESENTED_PHASES:
		var candidate := String(candidate_value)
		if construction_omitted and candidate == "construction":
			continue
		if not has_damage_rule and candidate in ["damaged", "really-damaged"]:
			continue
		expected_phases.append(candidate)
	var phases := lifecycle.get("phases") as Array
	if phases.size() != expected_phases.size():
		return false
	for index in phases.size():
		if typeof(phases[index]) != TYPE_DICTIONARY:
			return false
		var row := phases[index] as Dictionary
		var phase := String(row.get("phase", ""))
		if phase != expected_phases[index]:
			return false
		var expected_next: Variant = null
		if phase not in ["post-rubble", "post-collapse"]:
			expected_next = expected_phases[index + 1]
		if not _validate_playable_structure_phase_row(root, row, expected_next):
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
	if not Array(declared_covered).has("intact"):
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
	if _playable_structure_health_number({"value": facts.get("maximumHealth")}) != maximum_health:
		return false
	var rule_value: Variant = facts.get("damageStateRule")
	if has_damage_rule:
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
	else:
		# The omission must be explicit and can never hide authored thresholds.
		if rule_value != null:
			return false
		if String(facts.get("damageStateRuleStatus", "")) != "no-authored-damage-thresholds":
			return false
	return true


func _validate_playable_structure_phase_row(root: String, row: Dictionary, expected_next: Variant) -> bool:
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
	if expected_next != null:
		if typeof(next_phase) != TYPE_STRING or String(next_phase) != String(expected_next):
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


func _has_authored_command_socket_evidence(route: Dictionary) -> bool:
	## Retail summons some heroes through a producer's authored construct
	## command instead of a fortress roster slot (Treebeard is trained by the
	## Ent Moot's Command_ConstructEntTreeBeard socket). Such a route is only
	## valid for a hero when it carries the authored INI provenance the
	## importer records for command sockets; anything else fails closed.
	var source_value: Variant = route.get("source")
	if typeof(source_value) != TYPE_DICTIONARY:
		return false
	var source := source_value as Dictionary
	for field in ["producerIni", "commandSetIni", "commandButtonIni"]:
		if String(source.get(field, "")).strip_edges() == "":
			return false
	return true


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


## Bounded bulk read of declared pack documents. Containment resolution and the
## size bound stay on the calling thread exactly as _read_declared_document_bounded
## does; only the byte read + JSON parse (the expensive part on slow storage)
## fans out across the worker pool. The returned array parallels `relatives`;
## entries are {} wherever the sequential reader would have returned {}.
func _prefetch_declared_documents(root: String, relatives: Array[String], maximum_bytes: int) -> Array:
	var paths: Array[String] = []
	paths.resize(relatives.size())
	for index in relatives.size():
		var relative := relatives[index]
		var path := ""
		if relative != "" and maximum_bytes > 0:
			var resolved := ModLoader.resolve_pack_path(root, relative)
			if resolved != "" and FileAccess.file_exists(resolved):
				var file := FileAccess.open(resolved, FileAccess.READ)
				if file != null:
					var length := file.get_length()
					file.close()
					if length > 0 and length <= maximum_bytes:
						path = resolved
		paths[index] = path
	var documents: Array = []
	documents.resize(relatives.size())
	for index in documents.size():
		documents[index] = {}
	var readable: Array[int] = []
	for index in paths.size():
		if paths[index] != "":
			readable.append(index)
	if readable.size() == 1 or OS.get_processor_count() <= 1:
		for index in readable:
			documents[index] = _parse_json_document(paths[index])
	elif readable.size() > 1:
		# Group task on the global pool: each element writes its own pre-sized
		# slot, so no shared-state locking is needed.
		var group := WorkerThreadPool.add_group_task(
			func(element: int) -> void:
				var index := readable[element]
				documents[index] = _parse_json_document(paths[index]),
			readable.size()
		)
		WorkerThreadPool.wait_for_group_task_completion(group)
	return documents


static func _parse_json_document(path: String) -> Dictionary:
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null:
		return {}
	var text := file.get_as_text()
	file.close()
	var json := JSON.new()
	if json.parse(text) != OK:
		# Diagnostics are emitted by the caller on the main thread; flag the
		# failure with a sentinel the caller can recognize.
		return {"_parse_error": json.get_error_message(), "_parse_path": path}
	var raw: Variant = json.data
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


func get_bundle_object_for_pack(id: String, pack_root: String) -> Dictionary:
	## The bundle object projected from a specific pack's playableUnit document.
	## Shared retail units project the same member id from several packs; the
	## flat table keeps only the last-loaded pack's row. Falls back to the flat
	## row when no pack-scoped variant exists.
	if pack_root != "":
		for row_value in bundle_object_pack_index.get(id, []) as Array:
			var row := row_value as Dictionary
			if String(row.get("_pack_root", "")) == pack_root:
				return row.duplicate(true)
	return bundle_objects.get(id, {})


func get_animation_capability_for_pack(id: String, pack_root: String) -> Dictionary:
	## The animation capability projected from a specific pack's playableUnit
	## document; same cross-pack shared-id scoping as get_bundle_object_for_pack.
	if pack_root != "":
		for row_value in animation_capability_pack_index.get(id, []) as Array:
			var row := row_value as Dictionary
			if String(row.get("_pack_root", "")) == pack_root:
				return row.duplicate(true)
	return animation_capabilities.get(id, {})

func get_retail_unit_rules(id: String) -> Dictionary:
	return retail_unit_rules.get(id, {})


func get_ranger_runtime() -> Dictionary:
	return ranger_runtime.duplicate(true)


func get_trebuchet_runtime() -> Dictionary:
	return trebuchet_runtime.duplicate(true)


func get_spellbook_runtime() -> Dictionary:
	return spellbook_runtime.duplicate(true)


func get_playable_unit_runtime(object_id: String) -> Dictionary:
	return (playable_unit_runtimes.get(object_id, {}) as Dictionary).duplicate(true)


func get_playable_unit_runtimes() -> Dictionary:
	return _registry_snapshot("unit_runtimes", playable_unit_runtimes)


func get_playable_structure_runtime(object_id: String) -> Dictionary:
	return (playable_structure_runtimes.get(object_id, {}) as Dictionary).duplicate(true)


func get_playable_structure_runtimes() -> Dictionary:
	return _registry_snapshot("structure_runtimes", playable_structure_runtimes)


## MEASURED: these three whole-registry getters each did a `duplicate(true)` of
## every playable document on EVERY call. The menu's faction/map availability
## sweep calls them about seven times per faction across seven factions, so a
## single boot deep-copied the entire content registry roughly fifty times -
## 3.1 s of the 4.3 s that stage cost, spent copying data that had not changed.
##
## The defensive copy is kept, but taken ONCE per load generation and shared.
## What callers get is still isolated from ContentDB's own tables: mutating a
## snapshot can never corrupt the registries the simulation loads from, which is
## the property the copy existed to guarantee.
##
## WHAT DID CHANGE: two callers now share one snapshot object instead of each
## holding a private copy, so a caller that MUTATED its result would now be
## visible to the next caller. Every call site was checked before this went in -
## all fourteen read only (`.keys()`, `.values()`, iteration, `.get()`), and the
## one that keeps its result as state (retail_vertical_slice.playable_unit_runtimes)
## never assigns into it. If a future caller needs to mutate, it must take its
## own `.duplicate(true)` rather than this being reverted to per-call copying.
##
## Snapshots are dropped in reload() so a generation can never serve stale rows.
var _registry_snapshots: Dictionary = {}


func _registry_snapshot(key: String, source: Dictionary) -> Dictionary:
	var cached: Variant = _registry_snapshots.get(key)
	if cached is Dictionary:
		return cached as Dictionary
	var snapshot := source.duplicate(true)
	_registry_snapshots[key] = snapshot
	return snapshot


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

func resolve_playable_structure_image_path(object_id: String, image_id: String) -> String:
	## Structure docs carry their converted UI crops under
	## registration.presentation.imageBindings (construct-button icon and
	## selection portrait); resolve one against the doc's own pack root.
	var document: Dictionary = playable_structure_runtimes.get(object_id, {})
	if document.is_empty():
		return ""
	var registration: Dictionary = document.get("registration", {}) as Dictionary
	var bindings: Dictionary = (registration.get("presentation", {}) as Dictionary).get("imageBindings", {}) as Dictionary
	var relative: Variant = bindings.get(image_id)
	if typeof(relative) != TYPE_STRING:
		return ""
	return resolve_asset(String(relative), String(document.get("_pack_root", "")))


func get_animation_capability(id: String) -> Dictionary:
	return animation_capabilities.get(id, {})

func get_bundle_map(id: String) -> Dictionary:
	return bundle_maps.get(id, {})


## Retail map categories the SKIRMISH lobby may offer. Campaign, cinematic,
## tutorial, shell and system maps are cooked and catalogued, but they are not
## skirmish offerings and must never be mixed into the map list.
##
## `wotr-battle` is excluded on purpose. Retail's `map wor ...` maps carry
## `isMultiplayer = yes` in the registry, so a corpus defined by that flag alone
## sweeps 50 War of the Ring living-world battle maps into a skirmish list.
## Those maps are picked by the strategic layer when a WOTR territory is
## attacked, never from the skirmish map list; a caller that wants them asks for
## the category by name.
const LOBBY_MAP_CATEGORIES: Array[String] = ["skirmish"]
## Every category a lobby-shaped surface may ask for, skirmish plus the WOTR
## battle maps the strategic layer resolves.
const PLAYABLE_MAP_CATEGORIES: Array[String] = ["skirmish", "wotr-battle"]


func list_catalog_maps(categories: Array = LOBBY_MAP_CATEGORIES) -> Array[Dictionary]:
	## Every catalog-published map of the requested categories, in authored
	## order, as {"id", "name", "players", "category"}. Fails closed per row: a
	## map whose document carries no authored player count reports 0 rather than
	## a guess. A row that declares no category predates categorised discovery
	## and is retained, so an already-cooked pack keeps its list.
	var rows: Array[Dictionary] = []
	for map_id in catalog_map_ids:
		var document := bundle_maps.get(map_id, {}) as Dictionary
		if document.is_empty():
			continue
		var category := String(document.get("category", ""))
		if category != "" and not categories.has(category):
			continue
		rows.append({
			"id": map_id,
			"name": String(document.get("displayName", map_id)),
			"players": int(document.get("playerCount", 0)),
			"category": category,
		})
	return rows


func get_retail_ui_image(id: String) -> Dictionary:
	return retail_ui_images.get(id.to_lower(), {})


func resolve_retail_ui_image_path(id: String) -> String:
	var row := get_retail_ui_image(id)
	if row.is_empty():
		return ""
	return resolve_asset(String(row.get("path", "")), String(row.get("_pack_root", "")))


func prefetch_retail_ui_assets(pack_roots_hint: Array = []) -> int:
	## First-touch warm-up for the HUD's cold-boot image validation. The files
	## are exactly the ones the bind resolvers produce; reads fan out across the
	## worker pool so cloud/seek latency overlaps instead of serializing on the
	## main thread, and the bytes are discarded. Registry content and every
	## later validation result are unchanged.
	var roots: Array = []
	for value in pack_roots_hint:
		roots.append(String(value))
	if roots.is_empty():
		roots = pack_roots.duplicate()
	var paths: Array = []
	var seen: Dictionary = {}
	for documents_value in playable_unit_runtime_pack_index.values():
		for document_value in documents_value as Array:
			var document := document_value as Dictionary
			var root := String(document.get("_pack_root", ""))
			if not roots.has(root):
				continue
			var bindings: Dictionary = (document.get("registration", {}) as Dictionary).get("imageBindings", {}) as Dictionary
			for relative_value in bindings.values():
				var resolved := resolve_asset(String(relative_value), root)
				if resolved != "" and resolved.get_extension().to_lower() == "png" and not seen.has(resolved):
					seen[resolved] = true
					paths.append(resolved)
	for row_value in retail_ui_images.values():
		var row := row_value as Dictionary
		var root := String(row.get("_pack_root", ""))
		if not roots.has(root):
			continue
		var resolved := resolve_asset(String(row.get("path", "")), root)
		if resolved != "" and resolved.get_extension().to_lower() == "png" and not seen.has(resolved):
			seen[resolved] = true
			paths.append(resolved)
	if paths.is_empty():
		return 0
	if paths.size() == 1 or OS.get_processor_count() <= 1:
		for path in paths:
			_prefetch_file_bytes(path)
	else:
		var group := WorkerThreadPool.add_group_task(
			func(element: int) -> void: _prefetch_file_bytes(paths[element]),
			paths.size()
		)
		WorkerThreadPool.wait_for_group_task_completion(group)
	return paths.size()


static func _prefetch_file_bytes(path: String) -> void:
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null:
		return
	while not file.eof_reached():
		file.get_buffer(1024 * 1024)
	file.close()


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
	# Lock-free read of a table that is written only on the main thread, between
	# fan-outs (see _freeze_asset_resolutions). Entries are admitted ONLY when
	# the preferred pack root itself resolved the reference, and in that case the
	# loop below returns on its first iteration with an answer that depends on
	# nothing but (preferred_pack_root, reference) - not on pack_roots, which
	# grows as packs load. So a frozen hit is the same string the full path
	# would have produced, at this or any later point in the same generation.
	if preferred_pack_root != "":
		var frozen: Variant = _frozen_resolutions.get(preferred_pack_root + "\n" + rel_path)
		if frozen is String:
			return frozen as String
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
		var resolve_mark := Time.get_ticks_usec()
		var base := ModLoader.resolve_pack_path(pack_root, pack_relative)
		_probe("resolve_asset.resolve_pack_path", resolve_mark)
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
	# Registries only grow between reloads and pack files are immutable while a
	# generation is active, so both answers below are stable for the generation.
	# Mutex-guarded: playable runtime documents validate concurrently on the
	# worker pool.
	#
	# MEASURED: one FileAccess.file_exists per declared asset cost 596 ms for the
	# 12,027 assets a boot validates. Those files sit in 361 directories, and one
	# DirAccess enumeration per directory returns every name in 85 ms.
	#
	# ONLY A POSITIVE IS TAKEN FROM THE LISTING, and a positive is exactly the
	# fact file_exists reports: the name is an entry in that directory. A
	# negative or an unreadable directory falls through to the original
	# predicate unchanged, so nothing this used to admit can be refused because
	# the listing did not see it - res:// paths served from a .pck and
	# imported-only resources still reach ResourceLoader.exists.
	#
	# The listing is consulted BEFORE the memo because it is already the cheaper
	# of the two: one mutexed dictionary read against the memo's read plus write.
	# Only the paths it cannot settle are worth memoizing.
	var listed_mark := Time.get_ticks_usec()
	var listed := ModLoader.directory_contains(path.get_base_dir(), path.get_file())
	_probe("asset_exists.listed", listed_mark)
	if listed == 1:
		return true
	_asset_exists_mutex.lock()
	var cached: Variant = _asset_exists_cache.get(path)
	_asset_exists_mutex.unlock()
	if cached is bool:
		_probe_count("asset_exists.hit")
		return cached as bool
	# Memoized per reload generation: boot validation probes the same asset
	# paths repeatedly. A stale positive still fails closed later at the real
	# open; a stale negative only hides files added mid-generation, which the
	# registries would not see either.
	var miss_mark := Time.get_ticks_usec()
	var exists := FileAccess.file_exists(path) or ResourceLoader.exists(path)
	_probe("asset_exists.miss", miss_mark)
	_asset_exists_mutex.lock()
	_asset_exists_cache[path] = exists
	_asset_exists_mutex.unlock()
	return exists


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

## See events.gd:_init - per-autoload compile attribution for the boot profiler.
## No-op unless boot profiling is on.
func _init() -> void:
	BootProfile.mark("autoload_compiled:ContentDB")
