extends SceneTree
## Focused regression probe for empty-road maps and exact structure-ID gating.

const MAP_ID := "rotwk.map.grey-mountains"

var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var content_db = root.get_node_or_null("ContentDB")
	_check("content_db_available", content_db != null)
	if content_db == null:
		_finish()
		return
	var definition := content_db.call("get_bundle_map", MAP_ID) as Dictionary
	_check("grey_mountains_registered", not definition.is_empty())
	if definition.is_empty():
		_finish()
		return
	var pack_root := String(definition.get("_pack_root", ""))
	var map_data_script = load("res://src/retail_slice/retail_map_data.gd")
	var map_data = map_data_script.new()
	var loaded := bool(map_data.load_from_pack(pack_root, definition))
	_check("grey_mountains_load_from_pack", loaded, String(map_data.error))
	if loaded:
		_check(
			"grey_mountains_empty_roads_normalized",
			int(map_data.road_type_count) == 0
				and int(map_data.road_segment_count) == 0
				and int(map_data.road_material_count) == 0
				and String(map_data.roads_path) == ""
				and String(map_data.road_materials_path) == ""
		)
		var stale_material_definition := definition.duplicate(true)
		stale_material_definition["roadMaterials"] = "missing-road-materials.json"
		var stale_material_probe = map_data_script.new()
		_check(
			"empty_roads_ignore_stale_material_pointer",
			bool(stale_material_probe.load_from_pack(pack_root, stale_material_definition)),
			String(stale_material_probe.error)
		)

	var slice_script = load("res://src/retail_slice/retail_vertical_slice.gd")
	_check("slice_script_compiles", slice_script != null)
	if slice_script != null:
		var slice = slice_script.new()
		slice.structure_nodes = {101: null, 202: null}
		var matching_ids: Array[int] = [101, 202]
		var wrong_ids: Array[int] = [101, 303]
		_check("matching_structure_id_set_passes", bool(slice._structure_nodes_match_ids(matching_ids)))
		_check("equal_count_wrong_structure_id_set_fails", not bool(slice._structure_nodes_match_ids(wrong_ids)))
		slice.free()
	_finish()


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("SOL_SLICE_GATE PASS %s" % name)
	else:
		failed += 1
		printerr("SOL_SLICE_GATE FAIL %s %s" % [name, detail])


func _finish() -> void:
	print("SOL_SLICE_GATE_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)
