extends SceneTree

## Negative live-binding acceptance gate for GrabPassengerSpecialPower.
##
## The eight ids below are the exact selected-Fords intersection with the
## effective RotWK naturetrees.ini SELECTABLE + CLUB + TREE definitions. This
## gate deliberately does not manufacture gameplay identity/health from their
## renderable map bindings. Grab/Fling remains deferred while these assertions
## describe the selected pack.
const Sim = preload("res://src/retail_slice/retail_slice_sim.gd")
const Watchdog = preload("res://tests/runner_watchdog.gd")
const MAP_ID := "bfme2.map.fords-of-isen-ii"
const GRAB_TREE_IDS: Array[String] = [
	"TreeBanyan2", "TreeBanyan3", "TreeDead01", "TreeDiospyros2",
	"TreeEvergreen01", "TreeEvergreen01b", "TreeEvergreen01c", "TreeEvergreen01d",
]
const EXPECTED_CANDIDATE_PLACEMENTS := 503
const EXPECTED := 14
var passed := 0
var failed := 0
var watchdog := Watchdog.new()


func _initialize() -> void:
	watchdog.start(self, "GRAB_TREE_LIVE_BINDING", 0, 0, true)
	watchdog.set_result_provider(func() -> Vector2i: return Vector2i(passed, failed))
	call_deferred("_run")


func _run() -> void:
	var db = root.get_node_or_null("ContentDB")
	var map_script: GDScript = load("res://src/retail_slice/retail_map_data.gd") as GDScript
	var battlefield_script: GDScript = load("res://src/retail_slice/retail_fords_battlefield.gd") as GDScript
	_check("dependencies_load", db != null and map_script != null and battlefield_script != null)
	if db == null or map_script == null or battlefield_script == null:
		_finish(); return
	var definition: Dictionary = db.get_bundle_map(MAP_ID)
	var pack_root := String(definition.get("_pack_root", ""))
	_check("selected_retail_map_is_mounted", pack_root != "" and not definition.is_empty())
	var map_data = map_script.new()
	_check("selected_map_loads", map_data.load_from_pack(pack_root, definition), String(map_data.error))
	if not map_data.ready:
		_finish(); return
	var candidates: Array[Dictionary] = []
	for value in map_data.bound_prop_placements:
		var row := value as Dictionary
		if GRAB_TREE_IDS.has(String(row.get("source_type", ""))):
			candidates.append(row)
	_check("retail_grab_tree_intersection_is_exact", candidates.size() == EXPECTED_CANDIDATE_PLACEMENTS, str(candidates.size()))
	_check("candidate_ids_are_exact_source_types", _candidate_type_ids(candidates) == GRAB_TREE_IDS, str(_candidate_type_ids(candidates)))
	_check("renderable_bindings_have_no_gameplay_object_id", _all_string_field_empty(candidates, "object_id"))
	_check("map_rows_only_author_initial_health_percent", _all_initial_health_percent(candidates))
	_check("map_rows_explicitly_mark_candidates_not_targetable", _all_targetable(candidates, false))
	_check("content_db_has_no_tree_gameplay_definitions", _content_db_has_no_definitions(db))

	var battlefield = battlefield_script.new()
	root.add_child(battlefield)
	_check("battlefield_instantiates_selected_prop_bindings", battlefield.configure(map_data), String(battlefield.error))
	var live_nodes := _candidate_nodes(battlefield.retail_prop_container)
	_check("battlefield_has_all_candidate_presentations", live_nodes.size() == candidates.size(), str(live_nodes.size()))
	_check("battlefield_nodes_are_presentation_only", _nodes_have_no_gameplay_identity(live_nodes))

	var sim := _sim()
	_check("retail_sim_admits_no_unproven_tree_entities", _sim_tree_ids(sim).is_empty())
	_check("grab_live_binding_remains_explicitly_deferred", candidates.size() > 0 and _sim_tree_ids(sim).is_empty())
	battlefield.queue_free()
	await process_frame
	await process_frame
	var asset_factory: GDScript = load("res://src/view/asset_factory.gd") as GDScript
	if asset_factory != null:
		asset_factory.clear_mesh_cache()
	await process_frame
	await process_frame
	_finish()


func _candidate_type_ids(rows: Array[Dictionary]) -> Array[String]:
	var ids: Array[String] = []
	for row in rows:
		var id := String(row.get("source_type", ""))
		if not ids.has(id): ids.append(id)
	ids.sort_custom(func(a: String, b: String) -> bool: return a.naturalnocasecmp_to(b) < 0)
	var expected := GRAB_TREE_IDS.duplicate(); expected.sort_custom(func(a: String, b: String) -> bool: return a.naturalnocasecmp_to(b) < 0)
	return ids if ids == expected else ids


func _all_string_field_empty(rows: Array[Dictionary], field: String) -> bool:
	for row in rows:
		if String(row.get(field, "")) != "": return false
	return true


func _all_initial_health_percent(rows: Array[Dictionary]) -> bool:
	for row in rows:
		var properties := row.get("properties", {}) as Dictionary
		if int(properties.get("objectInitialHealth", -1)) != 100 or properties.has("maximumHealth") or properties.has("maxHealth"):
			return false
	return true


func _all_targetable(rows: Array[Dictionary], targetable: bool) -> bool:
	for row in rows:
		if bool((row.get("properties", {}) as Dictionary).get("objectTargetable", not targetable)) != targetable:
			return false
	return true


func _content_db_has_no_definitions(db: Node) -> bool:
	for id in GRAB_TREE_IDS:
		if not (db.get_bundle_object(id) as Dictionary).is_empty(): return false
	return true


func _candidate_nodes(container: Node) -> Array[Node]:
	var result: Array[Node] = []
	if container == null: return result
	for child in container.get_children():
		if GRAB_TREE_IDS.has(String(child.get_meta("source_type", ""))): result.append(child)
	return result


func _nodes_have_no_gameplay_identity(nodes: Array[Node]) -> bool:
	for node in nodes:
		if node.has_meta("content_object_id") or node.has_meta("entity_id") or node.has_meta("maximum_health"):
			return false
	return true


func _sim() -> RetailSliceSim:
	var rules := {}
	for object_id in [Sim.SOLDIER_OBJECT_ID, Sim.SOLDIER_HORDE_ID, Sim.ARCHER_OBJECT_ID, Sim.TOWER_GUARD_OBJECT_ID, Sim.KNIGHT_OBJECT_ID]:
		rules[object_id] = _rule()
	var sim: RetailSliceSim = Sim.new()
	sim.setup({}, {"unit_rules": rules, "source_map_transform_scale": 0.1})
	return sim


func _rule() -> Dictionary:
	return {"source_object_id":"Fixture","horde_id":Sim.SOLDIER_HORDE_ID,"category":"infantry","speed":1.0,"speed_source":10.0,"acceleration":1.0,"acceleration_source":10.0,"turn_rate_degrees_per_second":180.0,"braking":1.0,"braking_source":10.0,"attack_range":1.0,"attack_range_source":10.0,"minimum_attack_range":0.0,"minimum_attack_range_source":0.0,"vision_range":20.0,"vision_range_source":200.0,"delay_between_shots_ms":1000.0,"pre_attack_delay_ms":0.0,"firing_duration_ms":0.0,"attack_period_ticks":10,"pre_attack_ticks":0,"firing_duration_ticks":0,"member_damage":10,"member_health":100,"member_count":1,"formation_positions":[Vector3.ZERO],"provenance":{}}


func _sim_tree_ids(sim: RetailSliceSim) -> Array[int]:
	var ids: Array[int] = []
	for id in sim.entity_ids():
		if GRAB_TREE_IDS.has(String((sim.entities[id] as Dictionary).get("source_object_id", ""))): ids.append(id)
	return ids


func _check(label: String, condition: bool, detail: String = "") -> void:
	if condition: passed += 1
	else: failed += 1; push_error("GRAB_TREE_LIVE_BINDING_FAIL %s %s" % [label, detail])


func _finish() -> void:
	if passed + failed != EXPECTED: failed += 1; push_error("GRAB_TREE_LIVE_BINDING_FAIL count expected=%d actual=%d" % [EXPECTED, passed + failed])
	print("GRAB_TREE_LIVE_BINDING_RESULT passed=%d failed=%d status=deferred reason=selected-map-renderable-props-have-no-gameplay-object-id-or-maximum-health" % [passed, failed])
	watchdog.stop(); quit(0 if failed == 0 else 1)
