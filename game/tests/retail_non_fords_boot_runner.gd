extends SceneTree
## Regression gate: compiled maps without environment.json use retail's
## no-HardwareFog default and must still reach slice_ready.

const REQUIRED_MAP_ID := "rotwk.map.adorn-river"
const MAX_READY_FRAMES := 1200

var passed := 0
var failed := 0

const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
var _runner_watchdog := RunnerWatchdogScript.new()


func _initialize() -> void:
	_runner_watchdog.start(self, "RETAIL_NON_FORDS_BOOT_RUNNER")
	call_deferred("_run")


func _run() -> void:
	var requested_map := OS.get_environment("OPENBFME_SLICE_MAP").strip_edges().to_lower()
	_check("adorn_river_is_explicitly_selected", requested_map == REQUIRED_MAP_ID, requested_map)
	if requested_map != REQUIRED_MAP_ID:
		_finish()
		return
	var packed := load("res://scenes/retail_vertical_slice.tscn") as PackedScene
	_check("slice_scene_parses", packed != null)
	if packed == null:
		_finish()
		return
	var slice = packed.instantiate()
	root.add_child(slice)
	for _frame in MAX_READY_FRAMES:
		await process_frame
		if bool(slice.ready_ok) or String(slice.failure_reason) != "":
			break
	_check("slice_ready", bool(slice.ready_ok), String(slice.failure_reason))
	_check("selected_non_fords_map_is_loaded", String(slice.map_id) == REQUIRED_MAP_ID, String(slice.map_id))
	var fog: Dictionary = slice.environment_runtime_metadata.get("fog", {}) as Dictionary
	_check(
		"missing_compiled_environment_degrades_to_retail_no_fog",
		bool(slice.ready_ok)
			and slice.linear_fog == null
			and not bool(fog.get("runtime_enabled", true))
			and String(fog.get("degradation", "")).contains("environment.json"),
		str(fog)
	)
	_check("hardware_fog_enable_no_is_legal_fog_off", _disabled_environment_is_accepted(slice))
	slice.queue_free()
	await process_frame
	_finish()


func _disabled_environment_is_accepted(slice) -> bool:
	var fixture_root := ProjectSettings.globalize_path("user://fog-disabled-%d" % Time.get_ticks_usec()).replace("\\", "/")
	var fixture_map_root := fixture_root.path_join("maps/adorn-river")
	if DirAccess.make_dir_recursive_absolute(fixture_map_root) != OK:
		return false
	var fixture_path := fixture_map_root.path_join("environment.json")
	var file := FileAccess.open(fixture_path, FileAccess.WRITE)
	if file == null:
		_remove_fixture(fixture_root, fixture_map_root, fixture_path)
		return false
	file.store_string(JSON.stringify({
		"schema": "openbfme.fords-environment",
		"schemaVersion": 0,
		"mapId": REQUIRED_MAP_ID,
		"fog": {
			"enabled": false,
			"source": {"path": "maps/map mp adorn river/map.ini"},
		},
	}))
	file.close()
	var original_pack_root := String(slice.source_map_data.pack_root)
	var original_map_root := String(slice.source_map_data.map_root)
	slice.source_map_data.pack_root = fixture_root
	slice.source_map_data.map_root = fixture_map_root
	var binding: Dictionary = slice._load_compiled_map_fog()
	slice.source_map_data.pack_root = original_pack_root
	slice.source_map_data.map_root = original_map_root
	_remove_fixture(fixture_root, fixture_map_root, fixture_path)
	return (
		not binding.has("error")
		and not bool(binding.get("enabled", true))
		and String(binding.get("compiled_document", "")) == fixture_path
		and String(binding.get("source_path", "")) == "maps/map mp adorn river/map.ini"
	)


func _remove_fixture(fixture_root: String, fixture_map_root: String, fixture_path: String) -> void:
	if FileAccess.file_exists(fixture_path):
		DirAccess.remove_absolute(fixture_path)
	DirAccess.remove_absolute(fixture_map_root)
	DirAccess.remove_absolute(fixture_root.path_join("maps"))
	DirAccess.remove_absolute(fixture_root)


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("RETAIL_NON_FORDS_BOOT PASS %s" % name)
	else:
		failed += 1
		printerr("RETAIL_NON_FORDS_BOOT FAIL %s%s" % [name, " (%s)" % detail if detail != "" else ""])


func _finish() -> void:
	print("RETAIL_NON_FORDS_BOOT_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)
