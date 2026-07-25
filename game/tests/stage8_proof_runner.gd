extends SceneTree
## godot --headless --path game -s res://tests/stage8_proof_runner.gd

const Stage8World = preload("res://src/proof_stage8/proof_world.gd")
const Formation = preload("res://src/proof_stage8/formation_system.gd")
const SaveCodec = preload("res://src/proof_stage8/save_codec.gd")

var passed: int = 0
var failed: int = 0
var maps: Dictionary = {}
var repeat_hash: String = "00000000"
var save_bytes: int = 0


const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
# Turns a GDScript runtime error inside `_run` — which unwinds past every
# `quit()` and would otherwise leave this headless process idling forever —
# into a loud non-zero exit. See tests/runner_watchdog.gd.
var _runner_watchdog := RunnerWatchdogScript.new()


func _initialize() -> void:
	_runner_watchdog.start(self, "STAGE8_PROOF_RUNNER")
	call_deferred("_run")


func _run() -> void:
	_load_maps()
	_test_map_contract()
	if maps.is_empty():
		_finish()
		return
	_test_formations()
	_test_control_groups()
	_test_pause_speed_and_orders()
	_test_text_save_roundtrip()
	_test_file_slot_roundtrip()
	_test_hash_coverage()
	_test_repeat_replay()
	_finish()


func _load_maps() -> void:
	var path: String = ProjectSettings.globalize_path("res://../content/openbfme-test/data/stage8_maps.json")
	_check("stage8_maps_file_exists", FileAccess.file_exists(path), path)
	var parsed: Variant = JSON.parse_string(FileAccess.get_file_as_string(path)) if FileAccess.file_exists(path) else null
	_check("stage8_maps_json_parses", typeof(parsed) == TYPE_DICTIONARY)
	if typeof(parsed) == TYPE_DICTIONARY:
		maps = parsed


func _test_map_contract() -> void:
	if maps.is_empty():
		return
	_check("map_schema_is_legal_safe_bundle", String(maps.get("schema", "")) == "openbfme.skirmish-maps" and int(maps.get("rulesVersion", 0)) == 1)
	var rows: Array = maps.get("maps", [])
	var ids: Array[String] = []
	var valid_cells: bool = rows.size() == 4
	for value: Variant in rows:
		var row: Dictionary = value
		ids.append(String(row.get("id", "")))
		valid_cells = valid_cells and Array(row.get("startCells", [])).size() == 2 and Array(row.get("resourceCells", [])).size() == 4 and not Array(row.get("blockerCells", [])).is_empty()
	ids.sort()
	_check("exactly_four_unique_hand_authored_maps", ids == ["emberwood-edge", "sable-barrens", "slate-foothills", "verdant-crossing"])
	_check("all_maps_have_starts_resources_and_blockers", valid_cells)
	var world: Stage8World = _world()
	_check("world_indexes_all_four_external_maps", world.map_catalog().size() == 4)
	_check("map_switch_resets_authoritative_state", world.add_unit(0, Vector2i(2, 7)) > 0 and world.switch_map("sable-barrens") == "" and world.units.is_empty() and world.map_id == "sable-barrens")
	var bad: Dictionary = maps.duplicate(true)
	bad["unexpected"] = true
	var strict := Stage8World.new() as Stage8World
	_check("map_document_unknown_field_rejected", strict.setup(bad, "verdant-crossing") == "invalid_map_document")


func _test_formations() -> void:
	var formation := Formation.new() as Formation
	var ids: Array[int] = [9, 3, 7, 1]
	var line: Array[Dictionary] = formation.generate(ids, "line", Vector2i(10, 6))
	_check("line_slots_sort_by_entity_id", _assignment_ids(line) == [1, 3, 7, 9])
	_check("line_slots_are_deterministic", _assignment_cells(line) == [Vector2i(8, 6), Vector2i(9, 6), Vector2i(10, 6), Vector2i(11, 6)])
	var column: Array[Dictionary] = formation.generate(ids, "column", Vector2i(10, 6))
	_check("column_slots_are_deterministic", _assignment_cells(column) == [Vector2i(10, 4), Vector2i(10, 5), Vector2i(10, 6), Vector2i(10, 7)])
	var wedge: Array[Dictionary] = formation.generate(ids, "wedge", Vector2i(10, 6))
	_check("wedge_slots_are_deterministic", _assignment_cells(wedge) == [Vector2i(10, 6), Vector2i(9, 7), Vector2i(11, 7), Vector2i(8, 8)])
	_check("unknown_formation_rejected", formation.generate(ids, "circle", Vector2i.ZERO).is_empty())
	var world: Stage8World = _world()
	var spawned: Array[int] = _spawn_blue(world, 4)
	var ordered: Dictionary = world.order_formation([spawned[3], spawned[0], spawned[2], spawned[1]], "wedge", Vector2i(11, 6))
	_check("authoritative_world_stores_wedge_slots", bool(ordered.get("ok", false)) and String(world.entity(spawned[0])["formation"]) == "wedge" and int(world.entity(spawned[0])["formation_slot"]) == 0)
	var before_hash: int = world.state_hash()
	var blocked: Dictionary = world.order_formation(spawned, "line", Vector2i(10, 2))
	_check("blocked_formation_rejected_atomically", String(blocked.get("reason", "")) == "formation_blocked" and world.state_hash() == before_hash)


func _test_control_groups() -> void:
	var world: Stage8World = _world()
	var blue: Array[int] = _spawn_blue(world, 4)
	var red: int = world.add_unit(1, Vector2i(17, 7), 100)
	var assigned: Dictionary = world.assign_control_group(1, [blue[2], red, blue[0], blue[2], 999])
	_check("control_group_filters_enemy_missing_and_duplicate", Array(assigned.get("entity_ids", [])) == [blue[0], blue[2]])
	_check("control_group_recall_is_stable", world.recall_control_group(1) == [blue[0], blue[2]])
	_check("control_groups_support_one_through_nine", bool(world.assign_control_group(9, blue).get("ok", false)) and world.recall_control_group(9) == blue)
	_check("control_group_zero_rejected", not bool(world.assign_control_group(0, blue).get("ok", true)))
	world.damage_unit(blue[0], 999)
	_check("control_group_recall_prunes_defeated", world.recall_control_group(1) == [blue[2]])
	_check("control_group_state_valid", world.validate_state() == "", world.validate_state())


func _test_pause_speed_and_orders() -> void:
	var world: Stage8World = _world()
	var blue: Array[int] = _spawn_blue(world, 3)
	world.order_formation(blue, "line", Vector2i(12, 6))
	world.set_paused(true)
	var before: Dictionary = world.snapshot()
	world.advance(5)
	_check("pause_freezes_tick_and_orders", world.snapshot() == before)
	world.set_paused(false)
	world.set_game_speed(4)
	world.advance_frame()
	_check("game_speed_four_advances_four_ticks", world.tick_index == 4)
	_check("formation_order_moves_units", int(world.stats["distance_cells"]) > 0)
	_check("invalid_game_speed_rejected", not world.set_game_speed(3) and world.game_speed == 4)
	world.declare_victory(0)
	_check("victory_stats_are_authoritative", world.winner == 0 and int(world.stats["victory_tick"]) == world.tick_index)


func _test_text_save_roundtrip() -> void:
	var world: Stage8World = _build_mid_match()
	world.advance(3)
	world.damage_unit(2, 37)
	world.set_paused(true)
	world.set_game_speed(2)
	var saved_hash: int = world.state_hash()
	var text: String = world.serialize_state()
	save_bytes = text.to_utf8_buffer().size()
	_check("save_text_is_canonical_and_nonempty", save_bytes > 400 and text.begins_with("{"))
	var restored: Stage8World = _world()
	var error: String = restored.deserialize_state(text)
	_check("text_save_deserializes_cleanly", error == "", error)
	_check("save_restores_mid_order_positions_and_hp", restored.state_hash() == saved_hash and restored.entity(2)["health"] == 63 and String(restored.entity(1)["order"]) == String(world.entity(1)["order"]))
	_check("save_restores_groups_pause_speed_and_stats", restored.recall_control_group(1) == world.recall_control_group(1) and restored.paused and restored.game_speed == 2 and restored.stats == world.stats)
	_check("save_roundtrip_bytes_are_identical", restored.serialize_state() == text)
	world.set_paused(false)
	world.advance(7)
	_check("mutating_live_world_changes_hash", world.state_hash() != saved_hash)
	_check("load_rewinds_exact_authoritative_hash", world.deserialize_state(text) == "" and world.state_hash() == saved_hash)
	_check("invalid_save_json_rejected", restored.deserialize_state("{bad") == "invalid_save_json")
	var bad_schema: Dictionary = restored.snapshot()
	bad_schema["schema_version"] = 99
	_check("unknown_save_schema_rejected", restored.restore_state(bad_schema) == "invalid_save_schema")


func _test_file_slot_roundtrip() -> void:
	SaveCodec.delete_slot(8)
	var world: Stage8World = _build_mid_match()
	world.advance(4)
	var expected_hash: int = world.state_hash()
	var write: Dictionary = world.save_slot(8)
	_check("file_slot_write_succeeds", bool(write.get("ok", false)) and int(write.get("bytes", 0)) > 400)
	world.advance(10)
	world.damage_unit(1, 50)
	var read: Dictionary = world.load_slot(8)
	_check("file_slot_load_succeeds", bool(read.get("ok", false)))
	_check("file_slot_roundtrip_restores_hash", world.state_hash() == expected_hash)
	_check("invalid_file_slot_rejected", String(world.save_slot(0).get("reason", "")) == "invalid_slot")
	SaveCodec.delete_slot(8)
	_check("file_slot_cleanup_removes_test_artifact", not FileAccess.file_exists(SaveCodec.slot_path(8)))


func _test_hash_coverage() -> void:
	var world: Stage8World = _build_mid_match()
	var base: int = world.state_hash()
	world.paused = true
	_check("hash_covers_pause", world.state_hash() != base)
	world.paused = false
	world.game_speed = 2
	_check("hash_covers_speed", world.state_hash() != base)
	world.game_speed = 1
	world.control_groups[1] = [2]
	_check("hash_covers_control_groups", world.state_hash() != base)
	world.control_groups[1] = [1, 2, 3, 4]
	world.stats["orders_issued"] = int(world.stats["orders_issued"]) + 1
	_check("hash_covers_match_stats", world.state_hash() != base)
	world.stats["orders_issued"] = int(world.stats["orders_issued"]) - 1
	world.entity(1)["destination"] = Vector2i(15, 8)
	_check("hash_covers_pending_orders", world.state_hash() != base)
	world.entity(1)["destination"] = Vector2i(11, 6)
	world._next_unit_id += 1
	_check("hash_covers_future_ids", world.state_hash() != base)


func _test_repeat_replay() -> void:
	var first: Stage8World = _build_mid_match()
	var second: Stage8World = _build_mid_match()
	first.advance(12)
	second.advance(12)
	repeat_hash = first.state_hash_text()
	_check("repeat_stage8_hash_equal", first.state_hash() == second.state_hash(), repeat_hash)
	_check("repeat_stage8_serialization_equal", first.serialize_state() == second.serialize_state())
	_check("repeat_stage8_state_valid", first.validate_state() == "" and second.validate_state() == "", first.validate_state())


func _world() -> Stage8World:
	var world := Stage8World.new() as Stage8World
	var error: String = world.setup(maps, "verdant-crossing")
	if error != "":
		_check("stage8_world_setup", false, error)
	return world


func _spawn_blue(world: Stage8World, count: int) -> Array[int]:
	var result: Array[int] = []
	for index: int in range(count):
		result.append(world.add_unit(0, Vector2i(2 + index, 5), 100))
	return result


func _build_mid_match() -> Stage8World:
	var world: Stage8World = _world()
	var blue: Array[int] = _spawn_blue(world, 4)
	world.add_unit(1, Vector2i(17, 7), 120)
	world.assign_control_group(1, blue)
	world.assign_control_group(9, [blue[1], blue[3]])
	world.order_formation(blue, "line", Vector2i(11, 6))
	return world


func _assignment_ids(rows: Array[Dictionary]) -> Array[int]:
	var result: Array[int] = []
	for row: Dictionary in rows:
		result.append(int(row["entity_id"]))
	return result


func _assignment_cells(rows: Array[Dictionary]) -> Array[Vector2i]:
	var result: Array[Vector2i] = []
	for row: Dictionary in rows:
		result.append(Vector2i(row["cell"]))
	return result


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("PASS %s%s" % [name, " " + detail if detail != "" else ""])
	else:
		failed += 1
		print("FAIL %s%s" % [name, " " + detail if detail != "" else ""])


func _finish() -> void:
	if failed == 0:
		print("STAGE8_METRICS repeat_hash=%s save_bytes=%d assertions=%d" % [repeat_hash, save_bytes, passed])
		print("STAGE8_GODOT_PROOF PASS authority=gdscript-proof assertions=%d" % passed)
		quit(0)
	else:
		print("STAGE8_GODOT_PROOF FAIL authority=gdscript-proof assertions=%d failed=%d" % [passed + failed, failed])
		quit(1)
