extends SceneTree
## Retail castle admission proof against the currently cooked Erebor document.
## The next pack cook adds the castleSiege contract; this runner injects that
## exact metadata over today's immutable document to verify the runtime seam.

const PACK_ID := "rotwk-skirmish-maps-private"
const CATALOG_MAX_BYTES := 1024 * 1024
const DOCUMENT_MAX_BYTES := 8 * 1024 * 1024
const BLOCKERS: Array[String] = [
	"walkable-walls",
	"defendable-gates",
	"wall-garrisons",
	"wall-mounted-defenses",
]
const CONTRACT := {
	"family": "retail-castle-siege-skirmish",
	"gameplayStatus": "blocked-named-gaps",
	"blockers": BLOCKERS,
	"admissionPolicy": "document-loadable-lobby-visible-gameplay-fails-closed",
}
const CASTLE_STARTS := {
	"Amon Sul Fortress": 3,
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


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var loader := root.get_node_or_null("ModLoader")
	_check("mod_loader_available", loader != null)
	if loader == null:
		_finish()
		return
	var content_root := OS.get_environment("OPENBFME_CONTENT").strip_edges()
	var pack_root := ""
	for candidate_value in loader.selected_pack_supplements(
		content_root, content_root.path_join("selection.json")):
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
	_check("all_castle_documents_present", definitions.size() == CASTLE_STARTS.size(), str(definitions.keys()))
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
		var candidate_data = map_data_script.new()
		_check("%s_map_data_loads" % String(display_name).to_snake_case(),
			bool(candidate_data.load_from_pack(pack_root, candidate)), String(candidate_data.error))
		_check("%s_authored_spawns" % String(display_name).to_snake_case(),
			int(candidate_data.player_start_count) == int(CASTLE_STARTS[display_name]),
			str(candidate_data.player_start_count))
	var objects := _read_bounded(loader, pack_root, "maps/erebor/objects.json", DOCUMENT_MAX_BYTES)
	var type_names: Dictionary = {}
	for value in objects.get("objects", []) as Array:
		if typeof(value) == TYPE_DICTIONARY:
			type_names[String((value as Dictionary).get("typeName", ""))] = true
	_check("erebor_wall_objects_preserved", type_names.has("EreborWall01"))
	_check("erebor_gate_objects_preserved", type_names.has("EreborMainGate"))
	_check("erebor_garrison_objects_preserved", type_names.has("EBGarrisonableTower"))

	var contracted := definition.duplicate(true)
	contracted["castleSiege"] = CONTRACT
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
	_check("castle_gameplay_fails_closed_by_name", not bool(battlefield.configure(contracted_data))
		and String(battlefield.error) == "castle gameplay unsupported: " + ", ".join(BLOCKERS),
		String(battlefield.error))
	root.remove_child(battlefield)
	battlefield.queue_free()
	_finish()


func _castle_definitions(loader, pack_root: String) -> Dictionary:
	var catalog := _read_bounded(loader, pack_root, "data/maps.json", CATALOG_MAX_BYTES)
	var definitions: Dictionary = {}
	for value in catalog.get("maps", []) as Array:
		if typeof(value) != TYPE_DICTIONARY:
			continue
		var row := value as Dictionary
		var display_name := String(row.get("displayName", ""))
		if not CASTLE_STARTS.has(display_name):
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
	if condition:
		passed += 1
		print("CASTLE_MAP_ADMISSION PASS %s" % name)
	else:
		failed += 1
		printerr("CASTLE_MAP_ADMISSION FAIL %s %s" % [name, detail])


func _finish() -> void:
	print("CASTLE_MAP_ADMISSION_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)
