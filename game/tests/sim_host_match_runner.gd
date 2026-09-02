extends SceneTree
## Headless end-to-end proof for the first native-core playable match.

const SimHostClientScript := preload("res://src/sim/sim_host_client.gd")
const SimHostMatchScript := preload("res://src/sim/sim_host_match.gd")
const NativeContentLocatorScript := preload("res://src/sim/native_content_locator.gd")
const WatchdogScript := preload("res://tests/runner_watchdog.gd")
const TICKS := 900

var passed := 0
var failed := 0
var _watchdog := WatchdogScript.new()


func _initialize() -> void:
	_watchdog.start(self, "SIM_HOST_MATCH", 300_000, 30_000, true)
	_watchdog.set_result_provider(func() -> Vector2i: return Vector2i(passed, failed))
	call_deferred("_run")


func _run() -> void:
	var match := _load_json(_repo_path("contracts/fixtures/match-launch-v1.json"))
	_check("match_fixture_loaded", not match.is_empty())
	var preferred_map := String((match.get("map", {}) as Dictionary).get("path", ""))
	var native_content: Dictionary = NativeContentLocatorScript.resolve(preferred_map)
	var bundle_path := String(native_content.get("bundle", ""))
	var map_path := String(native_content.get("map", ""))
	if not FileAccess.file_exists(bundle_path):
		print("SIM_HOST_MATCH SKIP bundle absent at %s" % bundle_path)
		_finish()
		return
	_check("selected_map_present", FileAccess.file_exists(map_path))
	_check("host_executable_ready", _ensure_release_host())
	_test_input_mappings()
	if failed > 0:
		_finish()
		return

	var first := _run_session(match, bundle_path, true)
	var second := _run_session(match, bundle_path, false)
	_check("damage_events_occurred", int(first.get("damage", 0)) > 0)
	_check("death_events_occurred", int(first.get("death", 0)) > 0)
	_check("first_hash_shape", _is_lower_hex_64(String(first.get("hash", ""))))
	_check("second_hash_shape", _is_lower_hex_64(String(second.get("hash", ""))))
	_check("twin_hash_after_900", String(first.get("hash", "")) == String(second.get("hash", "")))
	print(
		"SIM_HOST_MATCH_PROOF ticks=%d damage_events=%d death_events=%d twin_hash_equal=%s"
		% [TICKS, int(first.get("damage", 0)), int(first.get("death", 0)), str(String(first.get("hash", "")) == String(second.get("hash", "")))]
	)
	_finish()


func _run_session(match: Dictionary, bundle_path: String, inspect: bool) -> Dictionary:
	var client = SimHostClientScript.new()
	_check("session_launch_%s" % inspect, client.launch_bundle(match, bundle_path))
	if not client.last_error().is_empty():
		return {}
	var catalog: Array[Dictionary] = client.templates()
	_check("session_catalog_%s" % inspect, not catalog.is_empty())
	var replay_path := _repo_path("workspace/logs/lane-net-a/sim-host-match.replay.json")
	if inspect:
		_check("session_record_started", client.record(replay_path))
	var names := {
		"p0_fighter": SimHostMatchScript.choose_horde_template(catalog, "GondorFighterHorde", "gondor", "fighter"),
		"p0_archer": SimHostMatchScript.choose_horde_template(catalog, "GondorArcherHorde", "gondor", "archer"),
		"p1_fighter": SimHostMatchScript.choose_horde_template(catalog, "MordorFighterHorde", "mordor", "fighter"),
		"p1_archer": SimHostMatchScript.choose_horde_template(catalog, "MordorArcherHorde", "mordor", "archer"),
	}
	_check("session_horde_names_%s" % inspect, not names.values().has(""))
	if inspect:
		print(
			"SIM_HOST_MATCH_RUNNER_TEMPLATES player0_fighter=%s player0_archer=%s player1_fighter=%s player1_archer=%s"
			% [names.p0_fighter, names.p0_archer, names.p1_fighter, names.p1_archer]
		)
	var requests := [
		[names.p0_fighter, 0, Vector2(1140.0, 760.0)],
		[names.p0_fighter, 0, Vector2(1200.0, 800.0)],
		[names.p0_fighter, 0, Vector2(1260.0, 840.0)],
		[names.p0_archer, 0, Vector2(1190.0, 910.0)],
		[names.p1_fighter, 1, Vector2(1740.0, 1060.0)],
		[names.p1_fighter, 1, Vector2(1800.0, 1100.0)],
		[names.p1_fighter, 1, Vector2(1860.0, 1140.0)],
		[names.p1_archer, 1, Vector2(1810.0, 990.0)],
	]
	var player_hordes: Array[int] = []
	var opponent_hordes: Array[int] = []
	for request in requests:
		var reply: Dictionary = client.spawn(String(request[0]), int(request[1]), request[2] as Vector2)
		_check("session_spawn_%s_%d" % [inspect, player_hordes.size() + opponent_hordes.size()], not reply.is_empty())
		var id := int(reply.get("id", 0))
		if int(request[1]) == 0:
			player_hordes.append(id)
		else:
			opponent_hordes.append(id)
	_check("session_eight_hordes_%s" % inspect, player_hordes.size() == 4 and opponent_hordes.size() == 4)
	var player_order: Dictionary = SimHostMatchScript.make_command_bundle(
		1, 0, 0, "attack_move", player_hordes, Vector2(1500.0, 950.0)
	)
	_check("session_player_attack_move_%s" % inspect, client.send_commands(player_order))
	for order_index in 3:
		var tick := 1 + order_index * 299
		var opponent_target := Vector2(1500.0, 950.0) if order_index == 0 else Vector2(1200.0, 800.0)
		var opponent_order: Dictionary = SimHostMatchScript.make_command_bundle(
			tick, 1, order_index, "attack_move", opponent_hordes, opponent_target
		)
		_check("session_opponent_order_%s_%d" % [inspect, tick], client.send_commands(opponent_order))

	var damage_events := 0
	var death_events := 0
	var ticks_seen := 0
	var final_snapshot: Dictionary = {}
	while ticks_seen < TICKS:
		var snapshots: Array[Dictionary] = client.step(1)
		if snapshots.is_empty():
			_check("session_step_%s_%d" % [inspect, ticks_seen + 1], false)
			break
		for snapshot in snapshots:
			final_snapshot = snapshot
			for event_value in snapshot.get("events", []) as Array:
				var kind := String((event_value as Dictionary).get("kind", ""))
				damage_events += 1 if kind == "damage" else 0
				death_events += 1 if kind == "death" else 0
		ticks_seen += 1
		if ticks_seen % 30 == 0:
			_watchdog.note("session_%s_tick_%d" % [inspect, ticks_seen])
	_check("session_ticks_900_%s" % inspect, ticks_seen == TICKS)
	if inspect and not final_snapshot.is_empty():
		print("SIM_HOST_MATCH_FINAL_POSITIONS %s" % _position_ranges(final_snapshot))
	var digest := client.hash()
	_check("session_quit_%s" % inspect, client.quit())
	if inspect:
		_verify_replay(replay_path)
	return {"hash": digest, "damage": damage_events, "death": death_events}


func _verify_replay(path: String) -> void:
	_check("recorded_replay_exists", FileAccess.file_exists(path))
	var replay_client = SimHostClientScript.new()
	var result: Dictionary = replay_client.replay(path, true)
	_check("recorded_replay_loaded", not result.is_empty())
	if result.is_empty():
		return
	var progress := result.get("progress", []) as Array
	_check("recorded_replay_tick_count", progress.size() == TICKS)
	var all_hashes_ok := true
	for row_value in progress:
		all_hashes_ok = all_hashes_ok and bool((row_value as Dictionary).get("hash_ok", false))
	_check("recorded_replay_hash_ok_every_tick", all_hashes_ok)
	var done := result.get("done", {}) as Dictionary
	_check("recorded_replay_done_tick", int(done.get("ticks", -1)) == TICKS)
	_check("recorded_replay_no_divergence", done.get("divergence_tick") == null)
	_check("recorded_replay_quit", replay_client.quit())


func _position_ranges(snapshot: Dictionary) -> String:
	var objects := snapshot.get("objects", {}) as Dictionary
	var owners := objects.get("owner", []) as Array
	var xs := objects.get("x", []) as Array
	var zs := objects.get("z", []) as Array
	var ranges := {
		0: [INF, -INF, INF, -INF],
		1: [INF, -INF, INF, -INF],
	}
	for index in owners.size():
		var owner := int(owners[index])
		if not ranges.has(owner):
			continue
		var row := ranges[owner] as Array
		row[0] = minf(float(row[0]), float(xs[index]))
		row[1] = maxf(float(row[1]), float(xs[index]))
		row[2] = minf(float(row[2]), float(zs[index]))
		row[3] = maxf(float(row[3]), float(zs[index]))
	return "p0=%s p1=%s" % [ranges[0], ranges[1]]


func _test_input_mappings() -> void:
	var points: Array[Dictionary] = [
		{"id": 100000, "owner": 0, "point": Vector2(100.0, 100.0)},
		{"id": 100001, "owner": 0, "point": Vector2(300.0, 300.0)},
		{"id": 100004, "owner": 1, "point": Vector2(110.0, 105.0)},
	]
	var selected: Array[int] = SimHostMatchScript.selection_from_screen_points(
		points, Rect2(80.0, 80.0, 80.0, 80.0), 0
	)
	_check("selection_mapping_player_zero", selected == [100000])
	var attack: Dictionary = SimHostMatchScript.right_click_command_from_pick(
		selected, 7, 2, 100004, Vector2(50.0, 60.0)
	)
	_check("right_click_enemy_command_well_formed", _well_formed_bundle(attack, "attack"))
	var move: Dictionary = SimHostMatchScript.right_click_command_from_pick(
		selected, 8, 3, 0, Vector2(50.0, 60.0)
	)
	_check("right_click_ground_command_well_formed", _well_formed_bundle(move, "move"))


func _well_formed_bundle(bundle: Dictionary, expected_type: String) -> bool:
	if (
		String(bundle.get("schema", "")) != "openbfme.command.v1"
		or int(bundle.get("tick", 0)) < 1
		or int(bundle.get("seat", -1)) != 0
		or int(bundle.get("seq", -1)) < 0
	):
		return false
	var commands := bundle.get("commands", []) as Array
	if commands.size() != 1:
		return false
	var command := commands[0] as Dictionary
	var args := command.get("args", {}) as Dictionary
	if String(command.get("type", "")) != expected_type or (args.get("objects", []) as Array).is_empty():
		return false
	return args.has("target") if expected_type == "attack" else args.has("x") and args.has("y")


func _ensure_release_host() -> bool:
	var executable := _repo_path("engine/OpenBfme.Host/bin/Release/net8.0/OpenBfme.Host.exe")
	if FileAccess.file_exists(executable):
		return true
	var output: Array = []
	var exit_code := OS.execute(
		"dotnet",
		PackedStringArray(["build", _repo_path("engine/OpenBfme.Host"), "-c", "Release", "--nologo"]),
		output,
		true
	)
	var log_path := _repo_path("workspace/logs/lane-kernel-6/sim_host_release_build.log")
	DirAccess.make_dir_recursive_absolute(log_path.get_base_dir())
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
		printerr("SIM_HOST_MATCH FAIL %s" % label)


func _finish() -> void:
	print("SIM_HOST_MATCH_RESULT passed=%d failed=%d" % [passed, failed])
	_watchdog.stop()
	quit(0 if failed == 0 else 1)
