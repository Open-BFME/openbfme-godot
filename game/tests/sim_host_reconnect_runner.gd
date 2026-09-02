extends SceneTree
## Headless reconnect proof: restore a tick-200 save, catch up to 300, finish at 600.

const SimHostClientScript := preload("res://src/sim/sim_host_client.gd")
const WatchdogScript := preload("res://tests/runner_watchdog.gd")
const SAVE_TICK := 200
const DROP_TICK := 300
const FINAL_TICK := 600

var passed := 0
var failed := 0
var _watchdog := WatchdogScript.new()


func _initialize() -> void:
	_watchdog.start(self, "SIM_HOST_RECONNECT", 240_000, 30_000, true)
	_watchdog.set_result_provider(func() -> Vector2i: return Vector2i(passed, failed))
	call_deferred("_run")


func _run() -> void:
	var match := _load_json(_repo_path("contracts/fixtures/match-launch-v1.json"))
	var templates := _repo_path("content/openbfme-test/sim-host/reconnect-templates.json")
	var control = SimHostClientScript.new()
	var dropped = SimHostClientScript.new()
	_check("control_launch", control.launch(match, templates))
	_check("dropped_launch", dropped.launch(match, templates))
	if failed > 0:
		_finish_clients([control, dropped])
		return

	_check("control_command_1", control.send_commands(_command_bundle(1)))
	_check("dropped_command_1", dropped.send_commands(_command_bundle(1)))
	_check("control_step_200", control.step(SAVE_TICK).size() == SAVE_TICK)
	_check("dropped_step_200", dropped.step(SAVE_TICK).size() == SAVE_TICK)
	var saved: Dictionary = dropped.save()
	_check("save_at_200", int(saved.get("tick", -1)) == SAVE_TICK)
	var catchup: Array = []
	for tick in [SAVE_TICK + 1, DROP_TICK]:
		var bundle := _command_bundle(tick)
		catchup.append(bundle)
		_check("control_command_%d" % tick, control.send_commands(bundle))
		_check("dropped_command_%d" % tick, dropped.send_commands(bundle))
	_check("control_step_300", control.step(DROP_TICK - SAVE_TICK).size() == DROP_TICK - SAVE_TICK)
	_check("dropped_step_300", dropped.step(DROP_TICK - SAVE_TICK).size() == DROP_TICK - SAVE_TICK)
	var control_drop_hash := control.hash()
	_check("simulated_drop_quit", dropped.quit())

	var rejoined = SimHostClientScript.new()
	_check("replacement_launch", rejoined.launch(match, templates))
	var joined: Dictionary = rejoined.join(
		String(saved.get("state", "")), SAVE_TICK, catchup
	)
	_check("join_reaches_300", int(joined.get("tick", -1)) == DROP_TICK)
	_check("join_hash_at_300", String(joined.get("hash", "")) == control_drop_hash)

	for tick in [DROP_TICK + 1, FINAL_TICK]:
		var bundle := _command_bundle(tick)
		_check("control_command_%d" % tick, control.send_commands(bundle))
		_check("rejoined_command_%d" % tick, rejoined.send_commands(bundle))
	_check("control_step_600", control.step(FINAL_TICK - DROP_TICK).size() == FINAL_TICK - DROP_TICK)
	_check("rejoined_step_600", rejoined.step(FINAL_TICK - DROP_TICK).size() == FINAL_TICK - DROP_TICK)
	_check("control_tick_600", control.save().get("tick", -1) == FINAL_TICK)
	_check("reconnected_tick_600", rejoined.save().get("tick", -1) == FINAL_TICK)
	_check("hash_equal_at_600", control.hash() == rejoined.hash())
	_finish_clients([control, rejoined])


func _command_bundle(tick: int) -> Dictionary:
	return {
		"schema": "openbfme.command.v1",
		"tick": tick,
		"seat": 0,
		"seq": tick - 1,
		"commands": [{
			"type": "move",
			"args": {"objects": [1], "x": 200 + tick, "y": 40 + tick},
		}],
	}


func _finish_clients(clients: Array) -> void:
	for client in clients:
		if client != null:
			client.quit()
	print("SIM_HOST_RECONNECT_RESULT passed=%d failed=%d" % [passed, failed])
	_watchdog.stop()
	quit(0 if failed == 0 else 1)


func _check(label: String, condition: bool) -> void:
	_watchdog.note(label)
	if condition:
		passed += 1
	else:
		failed += 1
		printerr("SIM_HOST_RECONNECT FAIL %s" % label)


func _load_json(path: String) -> Dictionary:
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null:
		return {}
	var parsed: Variant = JSON.parse_string(file.get_as_text())
	return parsed as Dictionary if parsed is Dictionary else {}


func _repo_path(relative: String) -> String:
	var game_root := ProjectSettings.globalize_path("res://").trim_suffix("/").trim_suffix("\\")
	return game_root.get_base_dir().path_join(relative)
