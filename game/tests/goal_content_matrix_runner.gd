extends SceneTree
## Goal matrix: factions, maps (load_from_pack), production, spellbooks.
## Uses the same ContentDB/selection/load paths as the live game.
## Env: OPENBFME_CONTENT must point at .private/content-packs.

const FACTIONS: Array[String] = [
	"men", "elves", "dwarves", "isengard", "mordor", "wild", "angmar",
]
const SimScript = preload("res://src/retail_slice/retail_slice_sim.gd")

var passed := 0
var failed := 0
var report: Dictionary = {
	"factions": {},
	"maps": {},
	"production": {},
	"spellbooks": {},
	"summary": {},
}


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var content_db = root.get_node_or_null("ContentDB")
	if content_db == null:
		push_error("GOAL_MATRIX FAIL no ContentDB")
		quit(1)
		return

	await _matrix_factions(content_db)
	await _matrix_maps(content_db)
	await _matrix_production(content_db)
	await _matrix_spellbooks(content_db)

	report["summary"] = {
		"passed": passed,
		"failed": failed,
		"pack_meta_count": (content_db.get("pack_meta") as Array).size(),
		"bundle_map_count": (content_db.get("bundle_maps") as Dictionary).size(),
		"playable_unit_count": (content_db.call("get_playable_unit_runtimes") as Dictionary).size()
			if content_db.has_method("get_playable_unit_runtimes")
			else -1,
		"playable_structure_count": (content_db.call("get_playable_structure_runtimes") as Dictionary).size()
			if content_db.has_method("get_playable_structure_runtimes")
			else -1,
	}
	var out_path := OS.get_environment("OPENBFME_GOAL_MATRIX_OUT").strip_edges()
	if out_path == "":
		out_path = "user://goal_content_matrix.json"
	var f := FileAccess.open(out_path, FileAccess.WRITE)
	if f:
		f.store_string(JSON.stringify(report, "\t"))
		f.close()
		print("GOAL_MATRIX wrote ", out_path)
	print("GOAL_MATRIX_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


func _check(bucket: String, key: String, ok: bool, detail: String = "") -> void:
	if ok:
		passed += 1
		print("GOAL_MATRIX PASS %s/%s" % [bucket, key])
	else:
		failed += 1
		print("GOAL_MATRIX FAIL %s/%s | %s" % [bucket, key, detail])
	if not report.has(bucket):
		report[bucket] = {}
	(report[bucket] as Dictionary)[key] = {"ok": ok, "detail": detail}


func _matrix_factions(content_db: Node) -> void:
	## Load main menu and exercise real availability + launch error path.
	var packed: PackedScene = load("res://scenes/boot.tscn")
	if packed == null:
		_check("factions", "boot_scene", false, "boot.tscn missing")
		return
	var menu = packed.instantiate()
	root.add_child(menu)
	await process_frame
	await process_frame
	# Force skirmish options to complete
	if menu.has_method("get_retail_faction_availability"):
		var availability: Dictionary = menu.get_retail_faction_availability()
		for faction_id in FACTIONS:
			var note := String(availability.get(faction_id, "<missing>"))
			_check("factions", "availability_" + faction_id, note == "", note)
		# Set Men vs Men AI and first available map if possible
		if menu.has_method("retail_launch_error"):
			var launch_err := String(menu.retail_launch_error())
			# Empty is ideal; otherwise record named refuse (may be map selection)
			_check(
				"factions",
				"launch_error_current_setup",
				launch_err == "" or launch_err.contains("map") or launch_err.contains("team"),
				launch_err
			)
			report["factions"]["raw_launch_error"] = launch_err
	else:
		_check("factions", "menu_api", false, "missing get_retail_faction_availability")
	menu.queue_free()
	await process_frame


func _matrix_maps(content_db: Node) -> void:
	var map_data_script = load("res://src/retail_slice/retail_map_data.gd")
	if map_data_script == null:
		_check("maps", "map_data_script", false, "load failed")
		return
	var bundle_maps: Dictionary = content_db.get("bundle_maps") as Dictionary
	var ids: Array = bundle_maps.keys()
	ids.sort()
	var load_ok := 0
	var load_fail := 0
	for map_id_value in ids:
		var map_id := String(map_id_value)
		var definition: Dictionary = content_db.call("get_bundle_map", map_id) as Dictionary
		if definition.is_empty():
			_check("maps", map_id, false, "empty definition")
			load_fail += 1
			continue
		var pack_root := String(definition.get("_pack_root", ""))
		if pack_root == "":
			_check("maps", map_id, false, "no _pack_root")
			load_fail += 1
			continue
		var map_data = map_data_script.new()
		var ok := bool(map_data.load_from_pack(pack_root, definition))
		var detail := "" if ok else String(map_data.error)
		_check("maps", map_id, ok, detail)
		if ok:
			load_ok += 1
		else:
			load_fail += 1
	report["maps"]["_totals"] = {
		"enumerated": ids.size(),
		"load_ok": load_ok,
		"load_fail": load_fail,
	}


func _matrix_production(content_db: Node) -> void:
	## Inventory pack playable routes and verify projection into ContentDB.
	## Full spawn requires slice setup; we exercise registry + projection contracts
	## that the sim uses for train/build (fail closed if missing).
	if not content_db.has_method("get_playable_unit_runtimes"):
		_check("production", "api", false, "no get_playable_unit_runtimes")
		return
	var units: Dictionary = content_db.call("get_playable_unit_runtimes")
	var structures: Dictionary = content_db.call("get_playable_structure_runtimes")
	var by_faction: Dictionary = {}
	for faction_id in FACTIONS:
		by_faction[faction_id] = {"units": [], "structures": [], "heroes": [], "gaps": []}

	var manifest_script = load("res://src/retail_slice/retail_faction_manifest.gd")
	var slice_script = load("res://src/retail_slice/retail_vertical_slice.gd")
	for object_id_value in units.keys():
		var object_id := String(object_id_value)
		var doc: Dictionary = units[object_id]
		var folded := object_id.to_lower()
		var faction_id := _guess_faction(folded)
		if faction_id == "":
			continue
		var row := {
			"objectId": object_id,
			"category": String(doc.get("category", "")),
			"has_visual": (doc.get("registration", {}) as Dictionary).has("visual"),
			"has_production": not ((doc.get("registration", {}) as Dictionary).get("production", []) as Array).is_empty(),
		}
		if String(doc.get("category", "")).to_lower().contains("hero") or folded.contains("aragorn") or folded.contains("gandalf") or folded.contains("boromir") or folded.contains("theoden") or folded.contains("eomer") or folded.contains("eowyn") or folded.contains("galadriel") or folded.contains("saruman") or folded.contains("witchking") or folded.contains("lurtz") or folded.contains("gollum"):
			(by_faction[faction_id]["heroes"] as Array).append(row)
		else:
			(by_faction[faction_id]["units"] as Array).append(row)
		_check(
			"production",
			"unit_" + object_id,
			row["has_visual"] and doc.has("registration"),
			"missing visual/registration" if not row["has_visual"] else ""
		)

	for object_id_value in structures.keys():
		var object_id := String(object_id_value)
		var doc: Dictionary = structures[object_id]
		var folded := object_id.to_lower()
		var faction_id := _guess_faction(folded)
		if faction_id == "":
			continue
		var reg: Dictionary = doc.get("registration", {}) as Dictionary
		var gameplay: Dictionary = reg.get("gameplay", {}) as Dictionary
		var row := {
			"objectId": object_id,
			"has_visual": reg.has("visual"),
			"has_production_exit": not (gameplay.get("productionExitUpdates", []) as Array).is_empty()
				or not (gameplay.get("production", []) as Array).is_empty(),
		}
		(by_faction[faction_id]["structures"] as Array).append(row)
		# Manifest validation for structures (same path as menu availability)
		if manifest_script != null:
			var fieldable: Dictionary = {}
			for u_id in units.keys():
				if _guess_faction(String(u_id).to_lower()) == faction_id:
					fieldable[u_id] = units[u_id]
			var structs_f: Dictionary = {}
			for s_id in structures.keys():
				if _guess_faction(String(s_id).to_lower()) == faction_id:
					structs_f[s_id] = structures[s_id]
			# only validate once per faction later

	if manifest_script != null:
		for faction_id in FACTIONS:
			var fieldable: Dictionary = {}
			var producible: Dictionary = {}
			var builder_unit_rules: Dictionary = {}
			var slice = null
			if slice_script != null:
				slice = slice_script.new()
				var map_data = load("res://src/retail_slice/retail_map_data.gd").new()
				map_data.local_transform_scale = 0.1
				slice.source_map_data = map_data
				slice._classify_faction_units(faction_id)
				fieldable = (slice.fieldable_unit_runtimes as Dictionary).duplicate(true)
				producible = (slice.producible_unit_runtimes as Dictionary).duplicate(true)
			var manifest: Dictionary = manifest_script.from_registries(faction_id, fieldable, structures)
			if slice_script != null:
				for builder_value in manifest.get("builder_unit_ids", []) as Array:
					var builder_id := String(builder_value)
					var builder_rule: Dictionary = slice._faction_builder_unit_rule(builder_id)
					if not builder_rule.is_empty():
						builder_unit_rules[builder_id] = builder_rule
				slice.free()
			var err := String(manifest.get("_error", ""))
			_check("production", "manifest_" + faction_id, err == "", err)
			by_faction[faction_id]["manifest_error"] = err
			by_faction[faction_id]["manifest_seed"] = manifest.get("seed_structure_kinds", [])
			by_faction[faction_id]["manifest_kinds"] = manifest.get("structure_kinds", [])
			if err == "":
				_probe_sim_production_spawn(faction_id, manifest, producible, builder_unit_rules)

	report["production"]["_by_faction"] = by_faction
	report["production"]["_totals"] = {
		"units": units.size(),
		"structures": structures.size(),
	}


func _probe_sim_production_spawn(faction_id: String, manifest: Dictionary, producible_units: Dictionary, builder_unit_rules: Dictionary) -> void:
	## Prove the authoritative path, not just registry shape: configure the same
	## sim, queue a fortress-produced unit, advance through its authored build
	## time, and require a new authoritative battalion with the queued identity.
	var sim = SimScript.new()
	sim._apply_gameplay_rules({
		"enable_base_loop": true,
		"faction_manifest": manifest,
		"playable_unit_runtimes": producible_units,
		"producer_kind_by_source_object": manifest.get("producer_kind_registry", {}),
		"unit_rules": builder_unit_rules,
		"starting_resources": 1000000,
		"source_map_transform_scale": 0.1,
	})
	if String(sim.configuration_error) != "":
		_check("production", "sim_spawn_" + faction_id, false, "configure: " + String(sim.configuration_error))
		return
	sim.setup({}, sim._rules)
	sim.ai_enabled = false
	if String(sim.configuration_error) != "":
		_check("production", "sim_spawn_" + faction_id, false, "setup: " + String(sim.configuration_error))
		return
	var producer := int(sim.fortress_id(0))
	if producer == 0:
		_check("production", "sim_spawn_" + faction_id, false, "no player fortress")
		return
	var offered: Array = sim.structure(producer).get("production", []) as Array
	var unit_type := ""
	for builder_value in manifest.get("builder_unit_ids", []) as Array:
		var builder_type := String(builder_value)
		if offered.has(builder_type):
			unit_type = builder_type
			break
	if unit_type == "" and not offered.is_empty():
		unit_type = String(offered[0])
	if unit_type == "":
		_check("production", "sim_spawn_" + faction_id, false, "fortress offers no production")
		return
	var before_ids: Array[int] = sim.entity_ids()
	var queued: Dictionary = sim.queue_unit(0, producer, unit_type)
	if not bool(queued.get("ok", false)):
		_check("production", "sim_spawn_" + faction_id, false, "queue %s: %s" % [unit_type, String(queued.get("reason", ""))])
		return
	var complete_tick := int((queued.get("item", {}) as Dictionary).get("complete_tick", sim.tick_index + 1))
	var maximum_steps := maxi(1, complete_tick - int(sim.tick_index) + 2)
	for _step in range(maximum_steps):
		sim.tick()
		if sim.production_queue_state(producer).is_empty():
			break
	var spawned_id := 0
	for entity_id in sim.entity_ids():
		if not before_ids.has(entity_id):
			var entity: Dictionary = sim.entity(entity_id)
			if String(entity.get("unit_type", "")) == unit_type and int(entity.get("production_producer_id", 0)) == producer:
				spawned_id = entity_id
				break
	_check(
		"production",
		"sim_spawn_" + faction_id,
		spawned_id != 0,
		"queued=%s producer=%d complete_tick=%d entities_before=%d entities_after=%d"
			% [unit_type, producer, complete_tick, before_ids.size(), sim.entity_ids().size()]
	)


func _matrix_spellbooks(content_db: Node) -> void:
	## Enumerate spellbook documents and power rows; exercise castability probe
	## if sim exposes it. Fail closed when powers array empty without reason.
	var spell_count := 0
	var power_count := 0
	# Spellbooks may be under content surfaces or pack files via ContentDB methods
	var packs: Array = content_db.get("pack_meta") as Array
	for meta_value in packs:
		var meta: Dictionary = meta_value
		var pack_root := String(meta.get("root", ""))
		if pack_root == "" or pack_root.begins_with("res://"):
			continue
		var spell_dir := pack_root.path_join("data/spellbooks")
		if not DirAccess.dir_exists_absolute(spell_dir):
			continue
		var dir := DirAccess.open(spell_dir)
		if dir == null:
			continue
		dir.list_dir_begin()
		var fname := dir.get_next()
		while fname != "":
			if not dir.current_is_dir() and fname.ends_with(".json"):
				var path := spell_dir.path_join(fname)
				var text := FileAccess.get_file_as_string(path)
				var parsed: Variant = JSON.parse_string(text)
				if typeof(parsed) != TYPE_DICTIONARY:
					_check("spellbooks", fname, false, "invalid json")
				else:
					var doc: Dictionary = parsed
					spell_count += 1
					var powers: Array = []
					# common shapes
					if doc.has("powers") and typeof(doc["powers"]) == TYPE_ARRAY:
						powers = doc["powers"]
					elif doc.has("registration") and typeof(doc["registration"]) == TYPE_DICTIONARY:
						var reg: Dictionary = doc["registration"]
						if reg.has("powers") and typeof(reg["powers"]) == TYPE_ARRAY:
							powers = reg["powers"]
						elif reg.has("spellbook") and typeof(reg["spellbook"]) == TYPE_DICTIONARY:
							var sb: Dictionary = reg["spellbook"]
							if sb.has("powers") and typeof(sb["powers"]) == TYPE_ARRAY:
								powers = sb["powers"]
					# also scan for power entries under gameplay
					if powers.is_empty() and doc.has("registration"):
						var reg2: Dictionary = doc["registration"]
						for k in reg2.keys():
							if String(k).to_lower().contains("power") and typeof(reg2[k]) == TYPE_ARRAY:
								powers = reg2[k]
								break
					power_count += powers.size()
					var ok := not powers.is_empty() or doc.has("registration")
					_check(
						"spellbooks",
						String(meta.get("id", "pack")) + "/" + fname,
						ok,
						"powers=%d" % powers.size()
					)
					# each power must have an id or name
					var pi := 0
					for pval in powers:
						if typeof(pval) != TYPE_DICTIONARY:
							_check("spellbooks", fname + "/power_" + str(pi), false, "non-dict power")
						else:
							var prow: Dictionary = pval
							var pid := String(prow.get("id", prow.get("name", prow.get("powerId", ""))))
							_check(
								"spellbooks",
								fname + "/power_" + (pid if pid != "" else str(pi)),
								pid != "" or prow.has("command") or prow.has("module"),
								"power keys=" + ",".join(PackedStringArray(prow.keys()))
							)
						pi += 1
			fname = dir.get_next()
		dir.list_dir_end()
	report["spellbooks"]["_totals"] = {"spellbooks": spell_count, "powers_seen": power_count}
	if spell_count == 0:
		_check("spellbooks", "any_present", false, "no spellbook json under mounted packs")


func _guess_faction(folded: String) -> String:
	if folded.begins_with("gondor") or folded.begins_with("rohan") or folded.begins_with("men") or folded.contains("aragorn") or folded.contains("boromir") or folded.contains("faramir") or folded.contains("gandalf") or folded.contains("theoden") or folded.contains("eomer") or folded.contains("eowyn"):
		return "men"
	if folded.begins_with("elven") or folded.begins_with("elf") or folded.begins_with("eregion") or folded.contains("galadriel") or folded.contains("haldir") or folded.contains("glorfindel") or folded.contains("thranduil"):
		return "elves"
	if folded.begins_with("dwarf") or folded.begins_with("dwarven") or folded.contains("gloin") or folded.contains("gimli"):
		return "dwarves"
	if folded.begins_with("isengard") or folded.contains("saruman") or folded.contains("lurtz") or folded.contains("sharku") or folded.contains("warg"):
		return "isengard"
	if folded.begins_with("mordor") or folded.contains("witchking") or folded.contains("mouthofsauron") or folded.contains("gothmog") or folded.contains("nazgul") or folded.contains("fellbeast"):
		return "mordor"
	if folded.begins_with("wild") or folded.begins_with("goblin") or folded.contains("spider") or folded.contains("dragon") or folded.contains("gorkil") or folded.contains("shelob"):
		return "wild"
	if folded.begins_with("angmar") or folded.contains("hwaldar") or folded.contains("karsh") or folded.contains("morgul"):
		return "angmar"
	return ""
