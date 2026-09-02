extends SceneTree
## Headless two-host mismatch proof with a deterministic on-disk state diff.

const SimHostClientScript := preload("res://src/sim/sim_host_client.gd")
const WatchdogScript := preload("res://tests/runner_watchdog.gd")

var passed := 0
var failed := 0
var _watchdog := WatchdogScript.new()


func _initialize() -> void:
	_watchdog.start(self, "SIM_HOST_DESYNC", 120_000, 30_000, true)
	_watchdog.set_result_provider(func() -> Vector2i: return Vector2i(passed, failed))
	call_deferred("_run")


func _run() -> void:
	var match := _load_json(_repo_path("contracts/fixtures/match-launch-v1.json"))
	var templates := _repo_path("content/openbfme-test/sim-host/reconnect-templates.json")
	var first = SimHostClientScript.new()
	var second = SimHostClientScript.new()
	_check("first_launch", first.launch(match, templates))
	_check("second_launch", second.launch(match, templates))
	_check("first_command", first.send_commands(_command_bundle(200)))
	_check("second_command", second.send_commands(_command_bundle(420)))
	_check("first_step", first.step(1).size() == 1)
	_check("second_step", second.step(1).size() == 1)
	_check("hash_mismatch_detected", first.hash() != second.hash())
	var first_save: Dictionary = first.save()
	var second_save: Dictionary = second.save()
	_check("both_saves_received", not first_save.is_empty() and not second_save.is_empty())
	var report_path := _repo_path("workspace/logs/lane-net-a/desync-1.json")
	var report: Dictionary = first.diff(String(second_save.get("state", "")), report_path)
	_check("diff_reply", String(report.get("op", "")) == "diff")
	_check("diff_tick", int(report.get("tick", -1)) == 1)
	_check("difference_present", report.get("difference") is Dictionary)
	_check("report_written", FileAccess.file_exists(report_path))
	var disk := _load_json(report_path)
	_check("disk_matches_reply", JSON.stringify(disk) == JSON.stringify(report))
	_check("first_quit", first.quit())
	_check("second_quit", second.quit())
	print("SIM_HOST_DESYNC_RESULT passed=%d failed=%d" % [passed, failed])
	_watchdog.stop()
	quit(0 if failed == 0 else 1)


func _command_bundle(x: int) -> Dictionary:
	return {
		"schema": "openbfme.command.v1",
		"tick": 1,
		"seat": 0,
		"seq": 0,
		"commands": [{
			"type": "move",
			"args": {"objects": [1], "x": x, "y": 80},
		}],
	}


func _check(label: String, condition: bool) -> void:
	_watchdog.note(label)
	if condition:
		passed += 1
	else:
		failed += 1
		printerr("SIM_HOST_DESYNC FAIL %s" % label)


func _load_json(path: String) -> Dictionary:
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null:
		return {}
	var parsed: Variant = JSON.parse_string(file.get_as_text())
	return parsed as Dictionary if parsed is Dictionary else {}


func _repo_path(relative: String) -> String:
	var game_root := ProjectSettings.globalize_path("res://").trim_suffix("/").trim_suffix("\\")
	return game_root.get_base_dir().path_join(relative)
