extends SceneTree
## Headless end-to-end proof for the native .NET sim host boundary.

const SimHostClientScript := preload("res://src/sim/sim_host_client.gd")
const WatchdogScript := preload("res://tests/runner_watchdog.gd")
const TICKS := 300
const SNAPSHOT_KEYS := [
	"schema", "tick", "tick_ms", "hash", "object_count", "objects",
	"hordes", "players", "events",
]
const OBJECT_KEYS := [
	"id", "template", "owner", "x", "y", "z", "yaw", "health",
	"max_health", "state", "anim", "anim_frame", "flags",
]

var passed := 0
var failed := 0
var _watchdog := WatchdogScript.new()


func _initialize() -> void:
	_watchdog.start(self, "SIM_HOST", 180_000, 30_000, true)
	_watchdog.set_result_provider(func() -> Vector2i: return Vector2i(passed, failed))
	call_deferred("_run")


func _run() -> void:
	_check("host_executable_ready", _ensure_release_host())
	var match := _load_json(_repo_path("contracts/fixtures/match-launch-v1.json"))
	var templates_path := _repo_path("content/openbfme-test/sim-host/templates.json")
	_check("match_fixture_loaded", not match.is_empty())
	_check("templates_fixture_exists", FileAccess.file_exists(templates_path))
	if failed > 0:
		_finish()
		return

	var first := _run_session(match, templates_path, true)
	var second := _run_session(match, templates_path, false)
	_check("first_session_hash", not String(first.get("hash", "")).is_empty())
	_check("second_session_hash", not String(second.get("hash", "")).is_empty())
	_check("twin_hash_after_300", String(first.get("hash", "")) == String(second.get("hash", "")))
	_finish()


func _run_session(match: Dictionary, templates_path: String, inspect: bool) -> Dictionary:
	var client = SimHostClientScript.new()
	_check("session_launch_%s" % inspect, client.launch(match, templates_path))
	if not client.last_error().is_empty():
		return {}
	_check("session_move_%s" % inspect, client.send_commands(_command_bundle(1, 0, "move", 420.0, 260.0)))
	_check(
		"session_attack_move_%s" % inspect,
		client.send_commands(_command_bundle(151, 1, "attack_move", 760.0, 520.0))
	)
	var snapshots: Array[Dictionary] = client.step(TICKS)
	_check("session_snapshot_count_%s" % inspect, snapshots.size() == TICKS)
	if inspect and snapshots.size() == TICKS:
		_validate_snapshots(snapshots)
	var digest := client.hash()
	_check("session_hash_shape_%s" % inspect, _is_lower_hex_64(digest))
	_check("session_quit_%s" % inspect, client.quit())
	return {"hash": digest}


func _validate_snapshots(snapshots: Array[Dictionary]) -> void:
	var prior_tick := 0
	var all_valid := true
	for snapshot in snapshots:
		all_valid = all_valid and _has_keys(snapshot, SNAPSHOT_KEYS)
		var objects_value: Variant = snapshot.get("objects")
		if not (objects_value is Dictionary):
			all_valid = false
			continue
		var objects := objects_value as Dictionary
		all_valid = all_valid and _has_keys(objects, OBJECT_KEYS)
		var count := int(snapshot.get("object_count", -1))
		for key in OBJECT_KEYS:
			var values: Variant = objects.get(key)
			all_valid = all_valid and values is Array and (values as Array).size() == count
		var tick := int(snapshot.get("tick", -1))
		all_valid = all_valid and tick == prior_tick + 1
		prior_tick = tick
	_check("snapshots_required_keys_parallel_ticks", all_valid)

	var first_positions := _positions(snapshots[0])
	var final_positions := _positions(snapshots[-1])
	var moved := 0
	for id_value in first_positions.keys():
		if final_positions.has(id_value) and first_positions[id_value] != final_positions[id_value]:
			moved += 1
	_check("moved_objects_changed_x_z", moved >= 2)


func _command_bundle(tick: int, seq: int, type: String, x: float, z: float) -> Dictionary:
	return {
		"schema": "openbfme.command.v1",
		"tick": tick,
		"seat": 0,
		"seq": seq,
		"commands": [{"type": type, "args": {"objects": [1, 12, 13], "x": x, "y": z}}],
	}


func _positions(snapshot: Dictionary) -> Dictionary:
	var result: Dictionary = {}
	var objects := snapshot["objects"] as Dictionary
	var ids := objects["id"] as Array
	for index in ids.size():
		result[int(ids[index])] = Vector2(
			float((objects["x"] as Array)[index]), float((objects["z"] as Array)[index])
		)
	return result


func _ensure_release_host() -> bool:
	var executable := _repo_path("engine/OpenBfme.Host/bin/Release/net8.0/OpenBfme.Host.exe")
	if FileAccess.file_exists(executable):
		return true
	var log_path := _repo_path("workspace/logs/lane-kernel-c/sim_host_build.log")
	DirAccess.make_dir_recursive_absolute(log_path.get_base_dir())
	var output: Array = []
	var exit_code := OS.execute(
		"dotnet",
		PackedStringArray(["build", _repo_path("engine/OpenBfme.Host"), "-c", "Release", "--nologo"]),
		output,
		true
	)
	var log := FileAccess.open(log_path, FileAccess.WRITE)
	if log != null:
		log.store_string("".join(output))
		log.close()
	return exit_code == 0 and FileAccess.file_exists(executable)


func _load_json(path: String) -> Dictionary:
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null:
		return {}
	var parsed: Variant = JSON.parse_string(file.get_as_text())
	return parsed as Dictionary if parsed is Dictionary else {}


func _repo_path(relative: String) -> String:
	var game_root := ProjectSettings.globalize_path("res://").trim_suffix("/").trim_suffix("\\")
	return game_root.get_base_dir().path_join(relative)


func _has_keys(value: Dictionary, required: Array) -> bool:
	for key in required:
		if not value.has(key):
			return false
	return true


func _is_lower_hex_64(value: String) -> bool:
	if value.length() != 64:
		return false
	for character in value:
		if not "0123456789abcdef".contains(character):
			return false
	return true


func _check(label: String, condition: bool) -> void:
	_watchdog.note(label)
	if condition:
		passed += 1
	else:
		failed += 1
		printerr("SIM_HOST FAIL %s" % label)


func _finish() -> void:
	print("SIM_HOST_RESULT passed=%d failed=%d" % [passed, failed])
	_watchdog.stop()
	quit(0 if failed == 0 else 1)
