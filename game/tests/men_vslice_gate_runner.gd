extends SceneTree
## Men vertical-slice gate: prove ContentDB registries, retail coverage closure,
## five selectable maps with water, and full slice ready_ok twice.
##
## Run:
##   OPENBFME_CONTENT=../workspace/content-packs godot --headless --path . \
##     --script res://tests/men_vslice_gate_runner.gd
##
## Exit 0 only when every gate line prints PASS and both slice boots succeed.

const LOG_RELATIVE := "../workspace/scratch/jobs/men-vslice-gate/gate.log"
const EXPECTED_MAPS: Array[String] = [
	"bfme2.map.fords-of-isen-ii",
	"bfme2.map.rivendell",
	"bfme2.map.mount-doom",
	"bfme2.map.dagorlad",
	"bfme2.map.mordor",
]
const CORE_HORDE_IDS: Array[String] = [
	"GondorFighterHorde",
	"GondorArcherHorde",
	"GondorTowerShieldGuardHorde",
	"GondorKnightHorde",
	"GondorRangerHorde",
	"GondorTrebuchet",
	"MenPorter",
]
const CORE_HERO_IDS: Array[String] = [
	"GondorAragornMP",
	"GondorBoromir",
	"GondorFaramir",
	"GondorGandalf",
	"RohanEomer",
	"RohanEowyn",
	"RohanTheoden",
]
const CORE_STRUCTURE_IDS: Array[String] = [
	"MenFortress",
	"GondorFarm",
	"GondorBarracks",
	"GondorArcherRange",
	"GondorStable",
	"GondorWorkshop",
]

var _log_path := ""
var _passed := 0
var _failed := 0
var _lines: Array[String] = []


const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
# Turns a GDScript runtime error inside `_run` — which unwinds past every
# `quit()` and would otherwise leave this headless process idling forever —
# into a loud non-zero exit. See tests/runner_watchdog.gd.
var _runner_watchdog := RunnerWatchdogScript.new()


func _init() -> void:
	_runner_watchdog.start(self, "MEN_VSLICE_GATE_RUNNER")
	# Gate boots assert the legacy pre-spawned roster; retail play starts from
	# fortress + porter only.
	OS.set_environment("OPENBFME_STARTER_ARMY", "1")
	call_deferred("_run")


func _run() -> void:
	_open_log()
	_log("=== MEN VSLICE GATE START %s ===" % Time.get_datetime_string_from_system())
	_log("OPENBFME_CONTENT=%s" % OS.get_environment("OPENBFME_CONTENT"))
	await process_frame
	await process_frame

	var db := get_root().get_node_or_null("/root/ContentDB")
	if db == null:
		_fail("contentdb_present", "ContentDB autoload missing")
		_finish(2)
		return
	_pass("contentdb_present")

	# --- Pack selection ---
	var packs: Array = db.pack_roots
	_log("pack_roots=%s" % JSON.stringify(packs))
	_check(packs.size() >= 2, "pack_roots_min2", "expected active+supplements, got %d" % packs.size())

	var unit_runtimes: Dictionary = db.get_playable_unit_runtimes()
	var structure_runtimes: Dictionary = db.get_playable_structure_runtimes()
	_log("playable_units=%d" % unit_runtimes.size())
	_log("playable_structures=%d" % structure_runtimes.size())
	_check(unit_runtimes.size() >= 14, "playable_units_ge_14", "got %d" % unit_runtimes.size())
	_check(structure_runtimes.size() >= 20, "playable_structures_ge_20", "got %d" % structure_runtimes.size())

	var unit_ids: Array[String] = []
	for k in unit_runtimes.keys():
		unit_ids.append(String((unit_runtimes[k] as Dictionary).get("objectId", k)))
	unit_ids.sort()
	_log("unit_object_ids=%s" % JSON.stringify(unit_ids))

	var structure_ids: Array[String] = []
	for k in structure_runtimes.keys():
		structure_ids.append(String((structure_runtimes[k] as Dictionary).get("objectId", k)))
	structure_ids.sort()
	_log("structure_object_ids=%s" % JSON.stringify(structure_ids))

	for hid in CORE_HORDE_IDS:
		_check(_has_object_id(unit_runtimes, hid), "core_unit_%s" % hid, "missing from playableUnit registry")
	for hid in CORE_HERO_IDS:
		_check(_has_object_id(unit_runtimes, hid), "core_hero_%s" % hid, "missing from playableUnit registry")
	for sid in CORE_STRUCTURE_IDS:
		_check(_has_object_id(structure_runtimes, sid), "core_structure_%s" % sid, "missing from playableStructure registry")

	var spellbook: Dictionary = db.get_spellbook_runtime() if db.has_method("get_spellbook_runtime") else {}
	_check(not spellbook.is_empty(), "spellbook_runtime", "Men spellbook runtime empty")
	if not spellbook.is_empty():
		_log("spellbook_schema=%s id=%s" % [spellbook.get("schema", ""), spellbook.get("spellBookObjectId", spellbook.get("objectId", ""))])

	# UI icons used by common orders
	for image_id in ["UCCommon_Stop", "UCCommon_AttackMove", "UCCommon_BattleStance", "UCCommon_HoldGroundStance"]:
		var row: Dictionary = db.get_retail_ui_image(image_id)
		var path: String = String(db.resolve_retail_ui_image_path(image_id))
		_check(not row.is_empty() and path != "" and FileAccess.file_exists(path), "ui_image_%s" % image_id, "row/path missing")

	# --- Maps: catalog + water + load via RetailMapData ---
	var content_root := OS.get_environment("OPENBFME_CONTENT").strip_edges()
	var five_root := content_root.path_join("bfme2-five-maps-106-private").replace("\\", "/")
	_check(DirAccess.dir_exists_absolute(five_root), "five_maps_pack_exists", five_root)
	var catalog_path := five_root.path_join("data/maps.json")
	_check(FileAccess.file_exists(catalog_path), "five_maps_catalog_exists", catalog_path)
	var catalog: Dictionary = _read_json(catalog_path)
	_check(String(catalog.get("schema", "")) == "openbfme.map-catalog", "five_maps_catalog_schema", String(catalog.get("schema", "")))
	var catalog_ids: Array[String] = []
	for row_value in catalog.get("maps", []) as Array:
		var row := row_value as Dictionary
		catalog_ids.append(String(row.get("id", "")))
	_log("catalog_map_ids=%s" % JSON.stringify(catalog_ids))
	for mid in EXPECTED_MAPS:
		_check(catalog_ids.has(mid), "catalog_has_%s" % mid.replace(".", "_"), "missing from five-maps catalog")

	var MapDataScript = load("res://src/retail_slice/retail_map_data.gd")
	for mid in EXPECTED_MAPS:
		var ok := await _verify_map(db, five_root, catalog, MapDataScript, mid)
		_check(ok, "map_load_%s" % mid.replace(".", "_"), "map failed load/water/passability")

	# --- Full slice boot x2 (fords default + one alternate map) ---
	var boot1 := await _boot_slice("")
	_check(boot1.get("ready_ok", false), "slice_boot_fords", String(boot1.get("error", "ready_ok false")))
	_log("boot1=%s" % JSON.stringify(boot1))
	# Full-pack retail start: fortresses only (player + enemy).
	if bool(boot1.get("ready_ok", false)):
		var seed_count := int(boot1.get("structures", -1))
		_check(seed_count == 2, "seed_fortress_only", "expected 2 fortresses, got %d" % seed_count)
		var kinds: Array = boot1.get("structure_kinds", []) as Array
		_check(kinds.has("archery_range") or kinds.has("archerrange"), "kind_archery_named", "kinds=%s" % JSON.stringify(kinds))
		_check(kinds.has("barracks"), "kind_barracks_named", "kinds=%s" % JSON.stringify(kinds))
		_check(kinds.has("workshop"), "kind_workshop_named", "kinds=%s" % JSON.stringify(kinds))

	var boot2 := await _boot_slice("bfme2.map.rivendell")
	_check(boot2.get("ready_ok", false), "slice_boot_rivendell", String(boot2.get("error", "ready_ok false")))
	_log("boot2=%s" % JSON.stringify(boot2))

	var boot3 := await _boot_slice("bfme2.map.dagorlad")
	_check(boot3.get("ready_ok", false), "slice_boot_dagorlad", String(boot3.get("error", "ready_ok false")))
	_log("boot3=%s" % JSON.stringify(boot3))

	var boot_md := await _boot_slice("bfme2.map.mount-doom")
	_check(boot_md.get("ready_ok", false), "slice_boot_mount_doom", String(boot_md.get("error", "ready_ok false")))
	_log("boot_mount_doom=%s" % JSON.stringify(boot_md))

	var boot_mo := await _boot_slice("bfme2.map.mordor")
	_check(boot_mo.get("ready_ok", false), "slice_boot_mordor", String(boot_mo.get("error", "ready_ok false")))
	_log("boot_mordor=%s" % JSON.stringify(boot_mo))

	# Second pass of fords for double-test stability
	var boot4 := await _boot_slice("bfme2.map.fords-of-isen-ii")
	_check(boot4.get("ready_ok", false), "slice_boot_fords_retest", String(boot4.get("error", "ready_ok false")))
	_log("boot4=%s" % JSON.stringify(boot4))

	_log("=== MEN VSLICE GATE END passed=%d failed=%d ===" % [_passed, _failed])
	_finish(0 if _failed == 0 else 1)


func _verify_map(db, five_root: String, catalog: Dictionary, MapDataScript, map_id: String) -> bool:
	var map_relative := ""
	for row_value in catalog.get("maps", []) as Array:
		var row := row_value as Dictionary
		if String(row.get("id", "")) == map_id:
			map_relative = String(row.get("map", ""))
			break
	if map_relative == "":
		_log("MAP_FAIL %s no catalog row" % map_id)
		return false
	var map_path := five_root.path_join(map_relative)
	if not FileAccess.file_exists(map_path):
		_log("MAP_FAIL %s missing %s" % [map_id, map_path])
		return false
	var map_doc: Dictionary = _read_json(map_path)
	if String(map_doc.get("schema", "")) != "openbfme.map":
		_log("MAP_FAIL %s bad schema %s" % [map_id, map_doc.get("schema", "")])
		return false
	map_doc["_pack_root"] = five_root
	map_doc["_source"] = map_path
	# For fords, prefer active pack entry when registered
	var registered: Dictionary = db.get_bundle_map(map_id)
	if not registered.is_empty() and map_id == "bfme2.map.fords-of-isen-ii":
		map_doc = registered
	var water_path := map_path.get_base_dir().path_join("water.json")
	if not FileAccess.file_exists(water_path):
		_log("MAP_FAIL %s missing water.json" % map_id)
		return false
	var water_doc: Dictionary = _read_json(water_path)
	if String(water_doc.get("schema", "")) != "openbfme.sage-water":
		_log("MAP_FAIL %s water schema %s" % [map_id, water_doc.get("schema", "")])
		return false
	var rivers: Array = water_doc.get("rivers", []) as Array
	var standing: Array = water_doc.get("standingAreas", []) as Array
	_log("MAP %s water rivers=%d standing=%d" % [map_id, rivers.size(), standing.size()])
	# Load through RetailMapData (same path as slice)
	var map_data = MapDataScript.new()
	var pack_for_map := String(map_doc.get("_pack_root", five_root))
	var loaded := false
	if map_data.has_method("load_from_pack"):
		loaded = bool(map_data.load_from_pack(pack_for_map, map_doc))
	var ready := bool(map_data.get("ready"))
	var nav_ready := bool(map_data.get("navigation_ready"))
	_log(
		"MAP %s load_from_pack=%s ready=%s navigation_ready=%s pack=%s"
		% [map_id, loaded, ready, nav_ready, pack_for_map.get_file()]
	)
	# File-level water + successful RetailMapData configure required.
	return FileAccess.file_exists(map_path) and FileAccess.file_exists(water_path) and loaded and ready


func _boot_slice(map_id: String) -> Dictionary:
	if map_id != "":
		OS.set_environment("OPENBFME_SLICE_MAP", map_id)
	else:
		OS.set_environment("OPENBFME_SLICE_MAP", "")
	var packed = load("res://scenes/retail_vertical_slice.tscn")
	if packed == null:
		return {"ready_ok": false, "error": "missing retail_vertical_slice.tscn"}
	var node = packed.instantiate()
	get_root().add_child(node)
	var t0 := Time.get_ticks_msec()
	var timeout_ms := 45000
	while Time.get_ticks_msec() - t0 < timeout_ms:
		await process_frame
		if bool(node.get("ready_ok")):
			break
		# detect hard fail panels
		var failed := false
		if node.has_method("_fail") == false:
			pass
		# if initialization set ready_ok false and stopped advancing, check after 8s of no phases
	var elapsed := Time.get_ticks_msec() - t0
	var result := {
		"ready_ok": bool(node.get("ready_ok")),
		"map_id": String(node.get("map_id")) if node.get("map_id") != null else map_id,
		"elapsed_ms": elapsed,
		"battalions": (node.battalion_nodes.size() if node.get("battalion_nodes") != null else -1),
		"structures": (node.structure_nodes.size() if node.get("structure_nodes") != null else -1),
		"fieldable": (node.fieldable_unit_runtimes.size() if node.get("fieldable_unit_runtimes") != null else -1),
		"producible": (node.producible_unit_runtimes.size() if node.get("producible_unit_runtimes") != null else -1),
		"faction": String((node.faction_manifest as Dictionary).get("faction", "")) if node.get("faction_manifest") != null else "",
		"structure_kinds": (node.faction_manifest as Dictionary).get("structure_kinds", []) if node.get("faction_manifest") != null else [],
		"seed_structure_kinds": (node.faction_manifest as Dictionary).get("seed_structure_kinds", []) if node.get("faction_manifest") != null else [],
		"failure_reason": String(node.get("failure_reason")) if node.get("failure_reason") != null else "",
	}
	if not bool(result["ready_ok"]):
		var reason := String(result.get("failure_reason", "")).strip_edges()
		if reason != "":
			result["error"] = reason
		else:
			result["error"] = "ready_ok false after %dms" % elapsed
	# cleanup
	node.queue_free()
	await process_frame
	await process_frame
	OS.set_environment("OPENBFME_SLICE_MAP", "")
	return result


func _has_object_id(runtimes: Dictionary, object_id: String) -> bool:
	for key in runtimes.keys():
		var doc: Dictionary = runtimes[key] as Dictionary
		if String(doc.get("objectId", "")).to_lower() == object_id.to_lower():
			return true
		if String(key).to_lower() == object_id.to_lower():
			return true
	return false


func _read_json(path: String) -> Dictionary:
	var f := FileAccess.open(path, FileAccess.READ)
	if f == null:
		return {}
	var text := f.get_as_text()
	f.close()
	var j := JSON.new()
	if j.parse(text) != OK:
		return {}
	return j.data if typeof(j.data) == TYPE_DICTIONARY else {}


func _open_log() -> void:
	var base := ProjectSettings.globalize_path("res://").get_base_dir()
	_log_path = base.path_join(LOG_RELATIVE).replace("\\", "/")
	DirAccess.make_dir_recursive_absolute(_log_path.get_base_dir())
	var f := FileAccess.open(_log_path, FileAccess.WRITE)
	if f:
		f.store_string("")
		f.close()


func _log(line: String) -> void:
	print(line)
	_lines.append(line)
	if _log_path == "":
		return
	var f := FileAccess.open(_log_path, FileAccess.READ_WRITE)
	if f == null:
		f = FileAccess.open(_log_path, FileAccess.WRITE)
	if f:
		f.seek_end()
		f.store_string(line + "\n")
		f.close()


func _pass(name: String) -> void:
	_passed += 1
	_log("GATE PASS %s" % name)


func _fail(name: String, detail: String) -> void:
	_failed += 1
	_log("GATE FAIL %s (%s)" % [name, detail])


func _check(cond: bool, name: String, detail: String) -> void:
	if cond:
		_pass(name)
	else:
		_fail(name, detail)


func _finish(code: int) -> void:
	_log("GATE_RESULT passed=%d failed=%d log=%s" % [_passed, _failed, _log_path])
	quit(code)
