extends SceneTree
## Focused regression: Osgiliath roadless and Rhun geometry-only maps must both
## complete the real RetailFordsBattlefield configuration path.

const MAP_IDS: Array[String] = [
	"rotwk.map.osgiliath",
	"rotwk.map.rhun",
]

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
	var map_data_script = load("res://src/retail_slice/retail_map_data.gd")
	var battlefield_script = load("res://src/retail_slice/retail_fords_battlefield.gd")
	_check("runtime_scripts_compile", map_data_script != null and battlefield_script != null)
	if map_data_script == null or battlefield_script == null:
		_finish()
		return

	for map_id in MAP_IDS:
		var definition := content_db.call("get_bundle_map", map_id) as Dictionary
		_check("%s_registered" % map_id, not definition.is_empty())
		if definition.is_empty():
			continue
		var map_data = map_data_script.new()
		var loaded := bool(map_data.load_from_pack(String(definition.get("_pack_root", "")), definition))
		_check("%s_load" % map_id, loaded, String(map_data.error))
		if not loaded:
			continue
		var battlefield = battlefield_script.new()
		root.add_child(battlefield)
		var configured := bool(battlefield.configure(map_data))
		_check("%s_battlefield_configure" % map_id, configured, String(battlefield.error))
		if configured:
			_check("%s_battlefield_source_contract" % map_id, bool(battlefield.source_driven), String(battlefield.error))
			_check(
				"%s_empty_road_presentation" % map_id,
				not bool(battlefield.road_material_source_driven)
					and int(battlefield.road_material_count) == 0
					and int(battlefield.road_source_edge_count) == 0
					and int(battlefield.road_vertex_count) == 0
					and int(battlefield.road_triangle_count) == 0
					and int(battlefield.road_mesh_instance_count) == 0
					and battlefield.road_container == null
			)
		if map_id == "rotwk.map.osgiliath":
			_check(
				"osgiliath_roadless_contract",
				int(map_data.road_type_count) == 0
					and int(map_data.road_segment_count) == 0
					and int(map_data.road_material_count) == 0
			)
		elif map_id == "rotwk.map.rhun":
			_check(
				"rhun_geometry_only_contract",
				int(map_data.road_type_count) == 1
					and int(map_data.road_segment_count) == 42
					and int(map_data.road_material_count) == 0
					and int(map_data.terrain_tile_escape_count) == 711
			)
		battlefield.queue_free()
		await process_frame

	_finish()


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("SOL_DEEPER_MAPS PASS %s" % name)
	else:
		failed += 1
		printerr("SOL_DEEPER_MAPS FAIL %s | %s" % [name, detail])


func _finish() -> void:
	print("SOL_DEEPER_MAPS_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)
