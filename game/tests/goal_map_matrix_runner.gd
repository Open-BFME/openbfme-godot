extends SceneTree
## Map-only matrix: every ContentDB bundle map through real load_from_pack.
## Env: OPENBFME_CONTENT, OPENBFME_GOAL_MAP_MATRIX_OUT (optional JSON report path).

const Watchdog = preload("res://tests/runner_watchdog.gd")
const ROTWK_MAP_PACK_DIGEST := "1739b61386b8242aafee7c46c2f2639f950dd8d5d7292687d2c10702b1e9972b"
const ROTWK_MAP_CATALOG_SHA256 := "cad332168625e4ed19d5b5dd6468cd1744a60129d25262fff582ab7fbfd58764"
const EXPECTED_ROTWK_MAPS := 74

var passed := 0
var failed := 0
var report: Dictionary = {"maps": {}, "summary": {}}
var watchdog = Watchdog.new()


func _initialize() -> void:
	watchdog.start(self, "GOAL_MAP_MATRIX", 0, 0, true)
	watchdog.set_result_provider(func() -> Vector2i: return Vector2i(passed, failed))
	call_deferred("_run")


func _run() -> void:
	var content_db = root.get_node_or_null("ContentDB")
	if content_db == null:
		push_error("GOAL_MAP_MATRIX FAIL no ContentDB")
		watchdog.stop()
		quit(1)
		return
	await process_frame
	await process_frame

	var map_data_script = load("res://src/retail_slice/retail_map_data.gd")
	if map_data_script == null:
		push_error("GOAL_MAP_MATRIX FAIL map_data script")
		watchdog.stop()
		quit(1)
		return

	var bundle_maps: Dictionary = content_db.get("bundle_maps") as Dictionary
	var ids: Array = bundle_maps.keys()
	ids.sort()
	var rotwk_ids: Array[String] = []
	var rotwk_pack_roots: Dictionary = {}
	var load_ok := 0
	var load_fail := 0
	for map_id_value in ids:
		var map_id := String(map_id_value)
		watchdog.note(map_id)
		if map_id.begins_with("rotwk.map."):
			rotwk_ids.append(map_id)
		var definition: Dictionary = content_db.call("get_bundle_map", map_id) as Dictionary
		if definition.is_empty():
			_check(map_id, false, "empty definition")
			load_fail += 1
			continue
		var pack_root := String(definition.get("_pack_root", ""))
		if pack_root == "":
			_check(map_id, false, "no _pack_root")
			load_fail += 1
			continue
		if map_id.begins_with("rotwk.map."):
			rotwk_pack_roots[pack_root.replace("\\", "/")] = true
		var map_data = map_data_script.new()
		var ok := bool(map_data.load_from_pack(pack_root, definition))
		var detail := "" if ok else String(map_data.error)
		_check(map_id, ok, detail)
		if ok:
			if map_id == "rotwk.map.rhun":
				var source_max_tile := -1
				var fringe_escape_count := 0
				var rendered_escape_count := 0
				for index in range(map_data.terrain_tile_indices.size()):
					var tile_value := int(map_data.terrain_tile_indices[index])
					source_max_tile = maxi(source_max_tile, int(tile_value))
					if int(map_data._terrain_texture_index_for_tile_value_strict(tile_value)) < 0:
						var on_non_rendered_fringe := (
							index % int(map_data.width) == int(map_data.width) - 1
							or index >= (int(map_data.height) - 1) * int(map_data.width)
						)
						if on_non_rendered_fringe:
							fringe_escape_count += 1
						else:
							rendered_escape_count += 1
				_check(
					"rhun_source_tile_fallback",
					int(map_data.terrain_tile_escape_count) == 711
						and fringe_escape_count == 711
						and rendered_escape_count == 0
						and source_max_tile == 31511
						and int(map_data.terrain_texture_index_for_tile_value(source_max_tile)) == 0,
					"escapes=%d fringe=%d rendered=%d maxTileValue=%d fallback=%d" % [
						int(map_data.terrain_tile_escape_count),
						fringe_escape_count,
						rendered_escape_count,
						source_max_tile,
						int(map_data.terrain_texture_index_for_tile_value(source_max_tile)),
					],
				)
			load_ok += 1
		else:
			load_fail += 1

	var rotwk_ok := 0
	for map_id in rotwk_ids:
		if bool((report["maps"] as Dictionary).get(map_id, {}).get("ok", false)):
			rotwk_ok += 1
	var selected_rotwk_root := String(rotwk_pack_roots.keys()[0]) if rotwk_pack_roots.size() == 1 else ""
	_check(
		"selected_rotwk_map_pack_identity",
		selected_rotwk_root.ends_with("/rotwk-playable-maps-private/%s" % ROTWK_MAP_PACK_DIGEST),
		"roots=%s" % str(rotwk_pack_roots.keys()),
	)
	var selected_catalog_path := selected_rotwk_root.path_join("data/maps.json")
	_check(
		"selected_rotwk_map_catalog_identity",
		selected_rotwk_root != "" and FileAccess.get_sha256(selected_catalog_path) == ROTWK_MAP_CATALOG_SHA256,
		"path=%s sha256=%s" % [selected_catalog_path, FileAccess.get_sha256(selected_catalog_path) if selected_rotwk_root != "" else ""],
	)
	_check("official_rotwk_count", rotwk_ids.size() == EXPECTED_ROTWK_MAPS, "rotwk_maps=%d expected=%d" % [rotwk_ids.size(), EXPECTED_ROTWK_MAPS])
	_check("official_rotwk_all_load", rotwk_ok == EXPECTED_ROTWK_MAPS, "rotwk_load_ok=%d expected=%d" % [rotwk_ok, EXPECTED_ROTWK_MAPS])

	report["summary"] = {
		"passed": passed,
		"failed": failed,
		"bundle_map_count": ids.size(),
		"rotwk_map_count": rotwk_ids.size(),
		"load_ok": load_ok,
		"load_fail": load_fail,
		"rotwk_load_ok": rotwk_ok,
	}
	var out_path := OS.get_environment("OPENBFME_GOAL_MAP_MATRIX_OUT").strip_edges()
	if out_path == "":
		out_path = OS.get_environment("OPENBFME_GOAL_MATRIX_OUT").strip_edges()
	if out_path != "":
		var f := FileAccess.open(out_path, FileAccess.WRITE)
		if f != null:
			f.store_string(JSON.stringify(report, "\t"))
			f.close()
			print("GOAL_MAP_MATRIX wrote ", out_path)
	print("GOAL_MAP_MATRIX_RESULT passed=%d failed=%d rotwk=%d/%d load_ok=%d" % [
		passed, failed, rotwk_ok, rotwk_ids.size(), load_ok
	])
	watchdog.stop()
	quit(0 if failed == 0 else 1)


func _check(key: String, ok: bool, detail: String = "") -> void:
	if ok:
		passed += 1
		print("GOAL_MAP_MATRIX PASS %s" % key)
	else:
		failed += 1
		print("GOAL_MAP_MATRIX FAIL %s | %s" % [key, detail])
	(report["maps"] as Dictionary)[key] = {"ok": ok, "detail": detail}
