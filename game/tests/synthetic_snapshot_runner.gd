extends SceneTree
## Headless contract gate for the deterministic synthetic snapshot producer.

const SyntheticSnapshotScript := preload("res://src/view/synthetic_snapshot.gd")
const WatchdogScript := preload("res://tests/runner_watchdog.gd")
const TICKS := 120
const OBJECTS := 50
const TEMPLATES := 5

var passed := 0
var failed := 0
var _watchdog := WatchdogScript.new()


func _initialize() -> void:
	_watchdog.start(self, "SYNTHETIC_SNAPSHOT", 120_000, 30_000, true)
	_watchdog.set_result_provider(func() -> Vector2i: return Vector2i(passed, failed))
	call_deferred("_run")


func _run() -> void:
	var fixture := _load_fixture()
	_check("fixture_loaded", not fixture.is_empty())
	if fixture.is_empty():
		_finish()
		return
	var saw_fighting := false
	var saw_dying := false
	var saw_removal := false
	for seed in range(1, 4):
		_watchdog.note("seed_%d" % seed)
		var generator = SyntheticSnapshotScript.new(seed, OBJECTS, TEMPLATES)
		var previous_tick := -1
		for step in TICKS + 1:
			var document: Dictionary = generator.snapshot() if step == 0 else generator.next_tick()
			_validate_document(document, fixture, seed, step)
			_check("seed_%d_step_%d_tick_increments" % [seed, step], int(document["tick"]) == previous_tick + 1)
			previous_tick = int(document["tick"])
			var objects := document["objects"] as Dictionary
			saw_fighting = saw_fighting or (objects["state"] as Array).has(2)
			saw_dying = saw_dying or _has_dying_flag(objects["flags"] as Array)
			saw_removal = saw_removal or int(document["object_count"]) < OBJECTS

		var twin_a = SyntheticSnapshotScript.new(seed, OBJECTS, TEMPLATES)
		var twin_b = SyntheticSnapshotScript.new(seed, OBJECTS, TEMPLATES)
		var final_a: Dictionary = twin_a.snapshot()
		var final_b: Dictionary = twin_b.snapshot()
		for _tick in TICKS:
			final_a = twin_a.next_tick()
			final_b = twin_b.next_tick()
		_check(
			"seed_%d_twin_tick_120_identical" % seed,
			JSON.stringify(final_a) == JSON.stringify(final_b)
		)

	_check("generated_fighting_state", saw_fighting)
	_check("generated_dying_flag_before_removal", saw_dying)
	_check("generated_object_removal", saw_removal)
	_finish()


func _validate_document(document: Dictionary, fixture: Dictionary, seed: int, step: int) -> void:
	var label := "seed_%d_step_%d" % [seed, step]
	_check(label + "_schema", String(document.get("schema", "")) == "openbfme.snapshot.v1")
	_check(label + "_top_keys", _has_keys(document, fixture.keys()))
	var objects_value: Variant = document.get("objects")
	_check(label + "_objects_dictionary", objects_value is Dictionary)
	if not (objects_value is Dictionary):
		return
	var objects := objects_value as Dictionary
	var fixture_objects := fixture["objects"] as Dictionary
	_check(label + "_object_keys", _has_keys(objects, fixture_objects.keys()))
	var object_count := int(document.get("object_count", -1))
	var lengths_match := true
	for key in fixture_objects.keys():
		var values: Variant = objects.get(key)
		if not (values is Array) or (values as Array).size() != object_count:
			lengths_match = false
	_check(label + "_parallel_lengths", lengths_match)

	var ids := objects.get("id", []) as Array
	var unique: Dictionary = {}
	var ids_valid := true
	for value in ids:
		var id := int(value)
		if id == 0 or unique.has(id):
			ids_valid = false
		unique[id] = true
	_check(label + "_ids_unique_nonzero", ids_valid)
	_check(label + "_hash_sha256", _is_lower_hex_64(String(document.get("hash", ""))))
	_check(label + "_players_populated", not (document.get("players", []) as Array).is_empty())
	_check(label + "_events_populated", object_count == 0 or not (document.get("events", []) as Array).is_empty())
	_validate_row_keys(document, fixture, label)


func _validate_row_keys(document: Dictionary, fixture: Dictionary, label: String) -> void:
	var fixture_hordes := fixture["hordes"] as Array
	var fixture_players := fixture["players"] as Array
	var fixture_events := fixture["events"] as Array
	var horde_keys: Array = (fixture_hordes[0] as Dictionary).keys()
	var player_keys: Array = (fixture_players[0] as Dictionary).keys()
	var event_keys := _common_dictionary_keys(fixture_events)
	var rows_valid := true
	for row in document["hordes"] as Array:
		rows_valid = rows_valid and row is Dictionary and _has_keys(row as Dictionary, horde_keys)
	for row in document["players"] as Array:
		rows_valid = rows_valid and row is Dictionary and _has_keys(row as Dictionary, player_keys)
	for row in document["events"] as Array:
		rows_valid = rows_valid and row is Dictionary and _has_keys(row as Dictionary, event_keys)
	_check(label + "_fixture_row_keys", rows_valid)


func _load_fixture() -> Dictionary:
	var game_root := ProjectSettings.globalize_path("res://").trim_suffix("/").trim_suffix("\\")
	var path := game_root.get_base_dir().path_join("contracts/fixtures/snapshot-v1.json")
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null:
		printerr("SYNTHETIC_SNAPSHOT FAIL fixture open %s" % path)
		return {}
	var parsed: Variant = JSON.parse_string(file.get_as_text())
	return parsed as Dictionary if parsed is Dictionary else {}


func _common_dictionary_keys(rows: Array) -> Array:
	if rows.is_empty() or not (rows[0] is Dictionary):
		return []
	var common: Array = (rows[0] as Dictionary).keys()
	for row_value in rows:
		if not (row_value is Dictionary):
			return []
		for index in range(common.size() - 1, -1, -1):
			if not (row_value as Dictionary).has(common[index]):
				common.remove_at(index)
	return common


func _has_keys(value: Dictionary, required: Array) -> bool:
	for key in required:
		if not value.has(key):
			return false
	return true


func _has_dying_flag(flags: Array) -> bool:
	for value in flags:
		if (int(value) & 4) != 0:
			return true
	return false


func _is_lower_hex_64(value: String) -> bool:
	if value.length() != 64:
		return false
	for character in value:
		if not "0123456789abcdef".contains(character):
			return false
	return true


func _check(label: String, condition: bool) -> void:
	if condition:
		passed += 1
	else:
		failed += 1
		printerr("SYNTHETIC_SNAPSHOT FAIL %s" % label)


func _finish() -> void:
	print("SYNTHETIC_SNAPSHOT_RESULT passed=%d failed=%d" % [passed, failed])
	_watchdog.stop()
	quit(0 if failed == 0 else 1)
