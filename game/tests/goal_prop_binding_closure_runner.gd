extends SceneTree
## Focused runtime proof for the goal-official-72 prop-binding closure.
## Env: OPENBFME_CONTENT, OPENBFME_GOAL_PROP_BINDING_OUT (optional JSON report).

const REQUIRED_MAP_IDS: Array[String] = [
	"rotwk.map.fords-of-isen-ii",
	"rotwk.map.rhun",
	"rotwk.map.osgiliath",
]

var passed := 0
var failed := 0
var report: Dictionary = {"maps": {}, "summary": {}}


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var content_db = root.get_node_or_null("ContentDB")
	if content_db == null:
		push_error("GOAL_PROP_BINDING_CLOSURE FAIL no ContentDB")
		quit(1)
		return
	await process_frame
	await process_frame

	var map_data_script = load("res://src/retail_slice/retail_map_data.gd")
	var battlefield_script = load("res://src/retail_slice/retail_fords_battlefield.gd")
	var asset_factory_script = load("res://src/view/asset_factory.gd")
	if map_data_script == null or battlefield_script == null or asset_factory_script == null:
		push_error("GOAL_PROP_BINDING_CLOSURE FAIL runtime scripts")
		quit(1)
		return

	var bundle_maps: Dictionary = content_db.get("bundle_maps") as Dictionary
	var ids: Array = bundle_maps.keys()
	ids.sort()
	var aggregate_bound := 0
	var aggregate_logical := 0
	var aggregate_unresolved := 0
	var loaded := 0
	var zero_bound_maps: Array[String] = []
	for map_id_value in ids:
		var map_id := String(map_id_value)
		if not map_id.begins_with("rotwk.map."):
			continue
		var definition: Dictionary = content_db.call("get_bundle_map", map_id) as Dictionary
		var pack_root := String(definition.get("_pack_root", ""))
		var map_data = map_data_script.new()
		var ok := not definition.is_empty() and pack_root != "" and bool(
			map_data.load_from_pack(pack_root, definition)
		)
		if not ok:
			_check(map_id, false, String(map_data.error))
			continue
		loaded += 1
		var bound_count := int(map_data.bound_prop_placement_count) + int(
			map_data.bound_structure_placement_count
		)
		var logical_count := int(map_data.logical_prop_placement_count)
		var unresolved_count := int(map_data.unresolved_prop_placement_count)
		aggregate_bound += bound_count
		aggregate_logical += logical_count
		aggregate_unresolved += unresolved_count
		if bound_count == 0:
			zero_bound_maps.append(map_id)
		(report["maps"] as Dictionary)[map_id] = {
			"ok": true,
			"boundPlacementCount": bound_count,
			"boundPropPlacementCount": int(map_data.bound_prop_placement_count),
			"logicalPlacementCount": logical_count,
			"unresolvedPlacementCount": unresolved_count,
		}
		if REQUIRED_MAP_IDS.has(map_id):
			var battlefield = battlefield_script.new()
			battlefield.name = "GoalPropBinding_" + map_id.trim_prefix("rotwk.map.")
			root.add_child(battlefield)
			var configured := bool(battlefield.configure(map_data))
			_check(
				"focused_configure_" + map_id.trim_prefix("rotwk.map."),
				configured,
				String(battlefield.error),
			)
			var spawned_props := int(battlefield.bound_retail_prop_count)
			var spawned_meshes := int(battlefield.bound_retail_mesh_instance_count)
			_check(
				"focused_spawn_" + map_id.trim_prefix("rotwk.map."),
				configured
					and int(map_data.bound_prop_placement_count) > 0
					and spawned_props == int(map_data.bound_prop_placement_count),
				"declared=%d spawned=%d" % [
					int(map_data.bound_prop_placement_count),
					spawned_props,
				],
			)
			_check(
				"focused_meshes_" + map_id.trim_prefix("rotwk.map."),
				configured and spawned_meshes >= spawned_props and spawned_meshes > 0,
				"props=%d meshes=%d" % [spawned_props, spawned_meshes],
			)
			var runtime_row: Dictionary = (report["maps"] as Dictionary)[map_id]
			runtime_row["configured"] = configured
			runtime_row["spawnedBoundPropCount"] = spawned_props
			runtime_row["spawnedMeshInstanceCount"] = spawned_meshes
			(report["maps"] as Dictionary)[map_id] = runtime_row
			print(
				"GOAL_PROP_BINDING_SPAWN %s configured=%s props=%d meshes=%d unresolved=%d" % [
					map_id,
					str(configured).to_lower(),
					spawned_props,
					spawned_meshes,
					unresolved_count,
				]
			)
			root.remove_child(battlefield)
			battlefield.free()
			asset_factory_script.clear_mesh_cache()

	_check("official_rotwk_load_count", loaded == 72, "loaded=%d expected=72" % loaded)
	_check(
		"aggregate_bound_placements",
		aggregate_bound > 0,
		"bound=%d logical=%d unresolved=%d" % [
			aggregate_bound,
			aggregate_logical,
			aggregate_unresolved,
		],
	)
	for required_id in REQUIRED_MAP_IDS:
		var row: Dictionary = (report["maps"] as Dictionary).get(required_id, {}) as Dictionary
		var bound_count := int(row.get("boundPlacementCount", 0))
		_check(
			"focused_bound_" + required_id.trim_prefix("rotwk.map."),
			bound_count > 0,
			"boundPlacementCount=%d" % bound_count,
		)
		print(
			"GOAL_PROP_BINDING_MAP %s bound=%d logical=%d unresolved=%d" % [
				required_id,
				bound_count,
				int(row.get("logicalPlacementCount", 0)),
				int(row.get("unresolvedPlacementCount", 0)),
			]
		)

	report["summary"] = {
		"passed": passed,
		"failed": failed,
		"loaded": loaded,
		"boundPlacementCount": aggregate_bound,
		"logicalPlacementCount": aggregate_logical,
		"unresolvedPlacementCount": aggregate_unresolved,
		"zeroBoundMaps": zero_bound_maps,
	}
	var out_path := OS.get_environment("OPENBFME_GOAL_PROP_BINDING_OUT").strip_edges()
	if out_path != "":
		var file := FileAccess.open(out_path, FileAccess.WRITE)
		if file != null:
			file.store_string(JSON.stringify(report, "\t"))
			file.close()
			print("GOAL_PROP_BINDING_CLOSURE wrote ", out_path)
	print(
		"GOAL_PROP_BINDING_CLOSURE_RESULT passed=%d failed=%d maps=%d bound=%d logical=%d unresolved=%d" % [
			passed,
			failed,
			loaded,
			aggregate_bound,
			aggregate_logical,
			aggregate_unresolved,
		]
	)
	quit(0 if failed == 0 else 1)


func _check(key: String, ok: bool, detail: String = "") -> void:
	if ok:
		passed += 1
		print("GOAL_PROP_BINDING_CLOSURE PASS %s" % key)
	else:
		failed += 1
		print("GOAL_PROP_BINDING_CLOSURE FAIL %s | %s" % [key, detail])
