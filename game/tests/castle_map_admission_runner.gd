extends SceneTree
## Retail castle admission proof against the cooked playable-maps pack.
## The pack now ships the castleSiege contract on every castle row, so this
## runner verifies the REAL cooked metadata end to end: catalog row, document
## load, named blockers, and the battlefield refusing gameplay by name.

const Watchdog := preload("res://tests/runner_watchdog.gd")
const PACK_ID := "rotwk-playable-maps-private"
const CATALOG_MAX_BYTES := 1024 * 1024
const DOCUMENT_MAX_BYTES := 8 * 1024 * 1024
const EXPECTED_CHECKS := 47
const AMON_SUL := "Amon Sul Fortress"
const BLOCKERS: Array[String] = [
	"walkable-walls",
	"defendable-gates",
	"wall-garrisons",
	"wall-mounted-defenses",
	"skirmish-ai-libraries",
]
const CONTRACT := {
	"family": "retail-castle-siege-skirmish",
	"gameplayStatus": "blocked-named-gaps",
	"blockers": BLOCKERS,
	"admissionPolicy": "document-loadable-lobby-visible-gameplay-fails-closed",
}
const CASTLE_STARTS := {
	"Carn Dum": 2,
	"Fornost": 3,
	"Black Gate": 3,
	"Dol Guldur": 4,
	"Erebor": 2,
	"Grey Havens": 3,
	"Helm's Deep": 4,
	"Isengard": 2,
	"Minas Morgul": 3,
	"Minas Tirith": 4,
}

var passed := 0
var failed := 0
var _watchdog := Watchdog.new()


func _initialize() -> void:
	_watchdog.start(self, "CASTLE_MAP_ADMISSION", 0, 0, true)
	_watchdog.set_result_provider(func() -> Vector2i: return Vector2i(passed, failed))
	call_deferred("_run")


func _run() -> void:
	var loader := root.get_node_or_null("ModLoader")
	_check("mod_loader_available", loader != null)
	if loader == null:
		_finish()
		return
	var content_root := OS.get_environment("OPENBFME_CONTENT").strip_edges()
	var pack_root := ""
	var candidates: Array[String] = []
	var active_pack := String(loader.selected_user_pack_root(
		content_root, content_root.path_join("selection.json")))
	if active_pack != "":
		candidates.append(active_pack)
	candidates.append_array(loader.selected_pack_supplements(
		content_root, content_root.path_join("selection.json")))
	for candidate_value in candidates:
		var candidate := String(candidate_value)
		if candidate.replace("\\", "/").contains("/%s/" % PACK_ID):
			pack_root = candidate
			break
	_check("castle_pack_present", pack_root != "", pack_root)
	print("CASTLE_MAP_ADMISSION pack_root=%s" % pack_root)
	if pack_root == "":
		_finish()
		return
	var definitions := _castle_definitions(loader, pack_root)
	_check("all_castle_documents_present", definitions.size() == CASTLE_STARTS.size() + 1, str(definitions.keys()))
	var definition := definitions.get("Erebor", {}) as Dictionary
	_check("erebor_compiled_document_present", not definition.is_empty())
	if definition.is_empty():
		_finish()
		return
	var map_data_script = load("res://src/retail_slice/retail_map_data.gd")
	var battlefield_script = load("res://src/retail_slice/retail_fords_battlefield.gd")
	_check("castle_runtime_scripts_parse", map_data_script != null and battlefield_script != null)
	if map_data_script == null or battlefield_script == null:
		_finish()
		return

	for display_name in CASTLE_STARTS:
		var candidate := definitions.get(display_name, {}) as Dictionary
		_check("%s_cooked_contract_exact" % String(display_name).to_snake_case(),
			(candidate.get("castleSiege", {}) as Dictionary) == CONTRACT,
			str(candidate.get("castleSiege", null)))
		var candidate_data = map_data_script.new()
		_check("%s_map_data_loads" % String(display_name).to_snake_case(),
			bool(candidate_data.load_from_pack(pack_root, candidate)), String(candidate_data.error))
		_check("%s_authored_spawns" % String(display_name).to_snake_case(),
			int(candidate_data.player_start_count) == int(CASTLE_STARTS[display_name]),
			str(candidate_data.player_start_count))
	var amon_definition := definitions.get(AMON_SUL, {}) as Dictionary
	_check("amon_sul_real_document_present", not amon_definition.is_empty())
	var amon_data = map_data_script.new()
	_check("amon_sul_map_data_loads_without_castle_contract",
		bool(amon_data.load_from_pack(pack_root, amon_definition)), String(amon_data.error))
	_check("amon_sul_has_no_castle_refusal", amon_data.castle_gameplay_blockers.is_empty(),
		str(amon_data.castle_gameplay_blockers))
	var amon_battlefield = battlefield_script.new()
	root.add_child(amon_battlefield)
	_check("amon_sul_battlefield_configures_as_skirmish",
		bool(amon_battlefield.configure(amon_data)), String(amon_battlefield.error))
	# Exercise the battlefield's own teardown while it is still in-tree; this
	# clears its retained material/mesh references and queues every generated
	# child through Godot's normal deletion path.
	amon_battlefield._clear_generated()
	var asset_factory_script = load("res://src/view/asset_factory.gd")
	asset_factory_script.clear_mesh_cache()
	await process_frame
	await process_frame
	root.remove_child(amon_battlefield)
	amon_battlefield.free()
	var objects := _read_bounded(loader, pack_root, "maps/wor-erebor/objects.json", DOCUMENT_MAX_BYTES)
	var type_names: Dictionary = {}
	for value in objects.get("objects", []) as Array:
		if typeof(value) == TYPE_DICTIONARY:
			type_names[String((value as Dictionary).get("typeName", ""))] = true
	_check("erebor_wall_objects_preserved", type_names.has("EreborWall01"))
	_check("erebor_gate_objects_preserved", type_names.has("EreborMainGate"))
	_check("erebor_garrison_objects_preserved", type_names.has("EBGarrisonableTower"))

	# The cooked Erebor document carries the real contract now - no injection.
	var contracted := definition.duplicate(true)
	var contracted_data = map_data_script.new()
	_check("contracted_castle_document_loads", bool(contracted_data.load_from_pack(pack_root, contracted)), String(contracted_data.error))
	_check("named_castle_gaps_surface_exactly", contracted_data.castle_gameplay_blockers == BLOCKERS,
		str(contracted_data.castle_gameplay_blockers))
	var malformed := definition.duplicate(true)
	var malformed_contract := CONTRACT.duplicate(true)
	malformed_contract["blockers"] = ["walkable-walls"]
	malformed["castleSiege"] = malformed_contract
	var malformed_data = map_data_script.new()
	_check("incomplete_castle_gap_inventory_fails_closed",
		not bool(malformed_data.load_from_pack(pack_root, malformed))
		and String(malformed_data.error) == "invalid castleSiege blocker inventory",
		String(malformed_data.error))
	var battlefield = battlefield_script.new()
	root.add_child(battlefield)
	# Owner 2026-08-19: castle maps admit; the gaps stay NAMED on the battlefield.
	var configured := bool(battlefield.configure(contracted_data))
	_check("castle_gameplay_admits_with_named_gaps", configured
		and battlefield.castle_gameplay_gaps == BLOCKERS,
		"configured=%s error=%s gaps=%s" % [str(configured), String(battlefield.error), str(battlefield.castle_gameplay_gaps)])
	# Erebor's bone-only retail props are exactly the allowlisted emitter
	# types (WtrflSteam, WtrflHaze, WhtDrftCloud); nothing else was absorbed.
	var bone_only: Array = battlefield.bone_only_bound_props
	var types_seen := {}
	for entry in bone_only:
		types_seen[String(entry).get_slice(":", 0)] = true
	var expected_types := {"WtrflSteam": true, "WtrflHaze": true, "WhtDrftCloud": true}
	_check("erebor_bone_only_props_are_exactly_the_allowlisted_emitters",
		not bone_only.is_empty() and types_seen == expected_types, str(bone_only))
	root.remove_child(battlefield)
	battlefield.free()
	# Drop the last script/local references before the rendering servers drain.
	# SceneTree.quit() otherwise begins native teardown while the coroutine still
	# owns the fully configured Amon Sul presentation graph.
	amon_battlefield = null
	battlefield = null
	amon_data = null
	contracted_data = null
	malformed_data = null
	map_data_script = null
	battlefield_script = null
	asset_factory_script = null
	definitions.clear()
	objects.clear()
	type_names.clear()
	# Defer the quit until this coroutine returns so its local map/resource
	# references are released before Godot tears down the rendering servers.
	call_deferred("_finish")


func _castle_definitions(loader, pack_root: String) -> Dictionary:
	var catalog := _read_bounded(loader, pack_root, "data/maps.json", CATALOG_MAX_BYTES)
	var definitions: Dictionary = {}
	for value in catalog.get("maps", []) as Array:
		if typeof(value) != TYPE_DICTIONARY:
			continue
		var row := value as Dictionary
		var display_name := String(row.get("displayName", ""))
		if not CASTLE_STARTS.has(display_name) and display_name != AMON_SUL:
			continue
		var relative := String(row.get("map", ""))
		var document := _read_bounded(loader, pack_root, relative, DOCUMENT_MAX_BYTES)
		if document.is_empty():
			continue
		var merged := row.duplicate(true)
		merged.merge(document, true)
		merged["_source"] = loader.resolve_pack_path(pack_root, relative)
		merged["_pack_root"] = pack_root
		definitions[display_name] = merged
	return definitions


func _read_bounded(loader, pack_root: String, relative: String, maximum: int) -> Dictionary:
	if not loader.is_safe_relative_path(relative):
		return {}
	var path: String = loader.resolve_pack_path(pack_root, relative)
	if path == "" or not FileAccess.file_exists(path):
		return {}
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null or file.get_length() <= 0 or file.get_length() > maximum:
		return {}
	file.close()
	var raw: Variant = loader._read_json(path)
	return raw as Dictionary if typeof(raw) == TYPE_DICTIONARY else {}


func _check(name: String, condition: bool, detail: String = "") -> void:
	_watchdog.note(name)
	if condition:
		passed += 1
		print("CASTLE_MAP_ADMISSION PASS %s" % name)
	else:
		failed += 1
		printerr("CASTLE_MAP_ADMISSION FAIL %s %s" % [name, detail])


func _finish() -> void:
	var ran := passed + failed
	if ran != EXPECTED_CHECKS:
		failed += 1
		printerr("CASTLE_MAP_ADMISSION FAIL liveness ran %d checks, expected %d - a function aborted before its assertions" % [ran, EXPECTED_CHECKS])
	print("CASTLE_MAP_ADMISSION_RESULT passed=%d failed=%d" % [passed, failed])
	_watchdog.stop()
	quit(0 if failed == 0 else 1)
