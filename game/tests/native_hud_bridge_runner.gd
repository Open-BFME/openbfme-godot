extends SceneTree
## Headless native match -> retail APT HUD bridge proof.

const WatchdogScript := preload("res://tests/runner_watchdog.gd")
const WAIT_FRAMES := 1800

var passed := 0
var failed := 0
var _watchdog := WatchdogScript.new()


func _initialize() -> void:
	_watchdog.start(self, "NATIVE_HUD", 600_000, 30_000, true)
	_watchdog.set_result_provider(func() -> Vector2i: return Vector2i(passed, failed))
	call_deferred("_run")


func _run() -> void:
	var packed := load("res://scenes/sim_host_match.tscn") as PackedScene
	_check("match_scene_loaded", packed != null)
	if packed == null:
		_finish()
		return
	var match_scene = packed.instantiate()
	root.add_child(match_scene)
	current_scene = match_scene
	for frame in WAIT_FRAMES:
		if match_scene.startup_failed() or match_scene.is_running():
			break
		if frame % 300 == 0:
			_watchdog.note("wait_%d" % frame)
		await process_frame
	_check("native_match_running", match_scene.is_running(), match_scene.startup_error())
	if not match_scene.is_running():
		match_scene.queue_free()
		_finish()
		return
	var bridge = match_scene.hud_bridge()
	_check("bridge_configured", bridge != null and bridge.hud != null, bridge.error if bridge != null else "missing")
	_check("apt_runtime_instantiated", bridge.hud.retail_apt_runtime != null)
	_check("apt_runtime_configured", bridge.apt_pack_root != "" and bridge.hud.retail_apt_runtime.contract_declared)

	var snapshot: Dictionary = match_scene.latest_snapshot()
	var hordes := snapshot.get("hordes", []) as Array
	var selected_horde := 0
	for value in hordes:
		var row := value as Dictionary
		if int(row.get("owner", -1)) == 0:
			selected_horde = int(row.get("id", 0))
			break
	_check("player_horde_present", selected_horde > 0)
	bridge.set_selection([selected_horde])
	var selected: Array[Dictionary] = bridge.selected_rows()
	_check("selected_horde_row", selected.size() == 1)
	if not selected.is_empty():
		var row := selected[0] as Dictionary
		_check("selected_members", int(row.get("member_count", 0)) > 0)
		_check("selected_health", float(row.get("health", 0.0)) > 0.0 and float(row.get("max_health", 0.0)) >= float(row.get("health", 0.0)))
		_check("selected_template", not String(row.get("template_name", "")).is_empty())
		_check("selected_portrait", not String(row.get("portrait", "")).is_empty())
	_check("selected_command_set", not bridge.selected_command_buttons().is_empty())

	var objects := snapshot.get("objects", {}) as Dictionary
	var object_ids := objects.get("id", []) as Array
	var fortress_id := 0
	var train_row: Dictionary = {}
	for value in object_ids:
		var object_id := int(value)
		bridge.set_selection([object_id])
		var facts: Array[Dictionary] = bridge.selected_rows()
		if facts.is_empty() or not String((facts[0] as Dictionary).get("template_name", "")).contains("Fortress"):
			continue
		for command_value in bridge.selected_command_buttons():
			var command := command_value as Dictionary
			if String(command.get("type", "")) == "train":
				fortress_id = object_id
				train_row = command
				break
		if fortress_id > 0:
			break
	_check("fortress_train_button", fortress_id > 0 and not train_row.is_empty())
	if fortress_id > 0:
		var client = match_scene.host_client()
		client.stop_packed_stream()
		for queued_snapshot in client.take_stream_snapshots():
			bridge.accept_snapshot(queued_snapshot)
		bridge.set_selection([fortress_id])
		_check("train_button_acknowledged", bridge.press_command(train_row) and bridge.last_acknowledged, client.last_error())
		var bundle: Dictionary = bridge.last_bundle
		var commands := bundle.get("commands", []) as Array
		var args := (commands[0] as Dictionary).get("args", {}) as Dictionary if commands.size() == 1 else {}
		_check("train_bundle_schema", String(bundle.get("schema", "")) == "openbfme.command.v1")
		_check("train_bundle_type", commands.size() == 1 and String((commands[0] as Dictionary).get("type", "")) == "train")
		_check("train_bundle_template", String(args.get("template", "")) == String(train_row.get("template", "")) and not String(args.get("template", "")).is_empty())
		_check("train_bundle_fortress", (args.get("objects", []) as Array) == [fortress_id])

	var player := (snapshot.get("players", []) as Array)[0] as Dictionary
	var readouts: Dictionary = bridge.player_readouts(int(player.get("index", 0)))
	_check("resources_match_snapshot", int(readouts.get("resources", -1)) == int(player.get("resources", -2)))
	_check("command_points_match_snapshot", int(readouts.get("command_points", -1)) == int(player.get("command_points", -2)) and int(readouts.get("command_points_max", -1)) == int(player.get("command_points_max", -2)))
	_check("power_points_match_snapshot", int(readouts.get("power_points", -1)) == int(player.get("power_points", -2)))
	match_scene.shutdown()
	match_scene.queue_free()
	await process_frame
	_finish()


func _check(name: String, condition: bool, detail: String = "") -> void:
	_watchdog.note(name)
	if condition:
		passed += 1
	else:
		failed += 1
		printerr("NATIVE_HUD FAIL %s%s" % [name, " (%s)" % detail if detail != "" else ""])


func _finish() -> void:
	print("NATIVE_HUD_RESULT passed=%d failed=%d" % [passed, failed])
	_watchdog.stop()
	quit(0 if failed == 0 else 1)
