extends SceneTree
## Two-process localhost proof for native-core lockstep, reconnect, and latency.
##
## The parent launches this same script twice per phase with OS.create_process.
## Each child owns a distinct OpenBfme.Host sidecar and writes its own log under
## workspace/logs/lane-net-b, so the proof crosses real ENet sockets and OS
## process boundaries without putting any network state in the simulation.

const SimHostClientScript := preload("res://src/sim/sim_host_client.gd")
const NativeLockstepSessionScript := preload("res://src/net/native_lockstep_session.gd")
const WatchdogScript := preload("res://tests/runner_watchdog.gd")
const FINAL_TICK := 3000
const DROP_TICK := 1000
const INPUT_DELAY := 3
const HASH_INTERVAL := 30
const PAIR_TIMEOUT_MS := 480_000
const LATENCY_TIMEOUT_MS := 720_000

var passed := 0
var failed := 0
var _watchdog := WatchdogScript.new()
var _child_pids: Array[int] = []
var _pid_files: Array[String] = []


func _initialize() -> void:
	var args := OS.get_cmdline_user_args()
	if args.has("--child"):
		call_deferred("_run_child", _parse_args(args))
		return
	_watchdog.start(self, "NATIVE_LOCKSTEP", 1_800_000, 30_000, true)
	_watchdog.set_result_provider(func() -> Vector2i: return Vector2i(passed, failed))
	call_deferred("_run_parent")


func _run_parent() -> void:
	var log_root := _repo_path("workspace/logs/lane-net-b")
	DirAccess.make_dir_recursive_absolute(log_root)
	var godot := OS.get_executable_path()
	var base_port := 28000 + (OS.get_process_id() % 1000) * 3
	var control := await _run_pair("control", base_port, godot, false, 0)
	_check_pair("control", control, true)
	var control_hash := String((control.get("host", {}) as Dictionary).get("hash", ""))
	var dropped := await _run_pair("drop", base_port + 1, godot, true, 0)
	_check_pair("drop", dropped, false)
	_check(
		"drop_final_matches_control",
		String((dropped.get("host", {}) as Dictionary).get("hash", "")) == control_hash
			and not control_hash.is_empty()
	)
	var latency := await _run_pair("latency", base_port + 2, godot, false, 150)
	_check_pair("latency", latency, false)
	var latency_guest := latency.get("guest", {}) as Dictionary
	_check("latency_stalled", int(latency_guest.get("stalled_ticks", 0)) > 0)
	print(
		"NATIVE_LOCKSTEP_STALLS control_host=%d control_guest=%d drop_host=%d drop_guest=%d latency_host=%d latency_guest=%d"
		% [
			_stalls(control, "host"), _stalls(control, "guest"),
			_stalls(dropped, "host"), _stalls(dropped, "guest"),
			_stalls(latency, "host"), _stalls(latency, "guest"),
		]
	)
	_cleanup_children()
	print("NATIVE_LOCKSTEP_RESULT passed=%d failed=%d" % [passed, failed])
	_watchdog.stop()
	quit(0 if failed == 0 else 1)


func _run_pair(
	phase: String, port: int, godot: String, drop_guest: bool, latency_ms: int
) -> Dictionary:
	_watchdog.note("pair_%s_start" % phase)
	var files := _phase_files(phase)
	for path_value in files.values():
		_remove_exact(String(path_value))
	var host_pid := _spawn_child(godot, "host", phase, port, files, false, 0)
	var pre_drop_hash_checks := 0
	_child_pids.append(host_pid)
	_pid_files.append(String(files.host_pid))
	if not await _wait_for_file(String(files.ready), 120_000):
		_check("%s_host_ready" % phase, false)
		return {}
	var guest_pid := _spawn_child(godot, "guest", phase, port, files, false, latency_ms)
	_child_pids.append(guest_pid)
	_pid_files.append(String(files.guest_pid))
	if drop_guest:
		if not await _wait_for_file(String(files.drop_marker), 240_000):
			_check("drop_marker_at_1000", false)
			return {}
		var marker := _load_json(String(files.drop_marker))
		_check("drop_marker_tick", int(marker.get("tick", -1)) == DROP_TICK)
		pre_drop_hash_checks = int(marker.get("hash_checks", 0))
		_kill_if_running(int(marker.get("sidecar_pid", -1)))
		_kill_if_running(int(marker.get("godot_pid", -1)))
		_kill_if_running(guest_pid)
		await create_timer(5.0).timeout
		var resumed_pid := _spawn_child(godot, "guest", phase, port, files, true, latency_ms)
		_child_pids.append(resumed_pid)
	var timeout := LATENCY_TIMEOUT_MS if latency_ms > 0 else PAIR_TIMEOUT_MS
	var host_ok := await _wait_for_json(String(files.host_result), timeout)
	var guest_ok := await _wait_for_json(String(files.guest_result), timeout)
	_check("%s_host_result" % phase, host_ok)
	_check("%s_guest_result" % phase, guest_ok)
	return {
		"host": _load_json(String(files.host_result)),
		"guest": _load_json(String(files.guest_result)),
		"pre_drop_hash_checks": pre_drop_hash_checks,
	}


func _phase_files(phase: String) -> Dictionary:
	var root := _repo_path("workspace/logs/lane-net-b")
	return {
		"ready": root.path_join("%s-host.ready.json" % phase),
		"drop_marker": root.path_join("%s-guest.drop.json" % phase),
		"host_result": root.path_join("%s-host.result.json" % phase),
		"guest_result": root.path_join("%s-guest.result.json" % phase),
		"host_pid": root.path_join("%s-host.pid.json" % phase),
		"guest_pid": root.path_join("%s-guest.pid.json" % phase),
		"host_log": root.path_join("%s-host.log" % phase),
		"guest_log": root.path_join("%s-guest.log" % phase),
		"host_godot_log": root.path_join("%s-host.godot.log" % phase),
		"guest_godot_log": root.path_join("%s-guest.godot.log" % phase),
		"host_replay": root.path_join("%s-host.replay.json" % phase),
		"guest_replay": root.path_join("%s-guest.replay.json" % phase),
	}


func _spawn_child(
	godot: String,
	role: String,
	phase: String,
	port: int,
	files: Dictionary,
	resume: bool,
	latency_ms: int
) -> int:
	var result_key := "%s_result" % role
	var pid_key := "%s_pid" % role
	var log_key := "%s_log" % role
	var godot_log_key := "%s_godot_log" % role
	var replay_key := "%s_replay" % role
	var args := PackedStringArray([
		"--headless", "--log-file", String(files[godot_log_key]),
		"--path", ProjectSettings.globalize_path("res://"),
		"--script", "res://tests/native_lockstep_runner.gd", "--",
		"--child", role, "--phase", phase, "--port", str(port),
		"--result", String(files[result_key]), "--pid-file", String(files[pid_key]),
		"--log", String(files[log_key]), "--replay", String(files[replay_key]),
		"--ready", String(files.ready), "--drop-marker", String(files.drop_marker),
		"--latency", str(latency_ms), "--resume", "1" if resume else "0",
	])
	var pid := OS.create_process(godot, args, false)
	_check("spawn_%s_%s%s" % [phase, role, "_resume" if resume else ""], pid > 0)
	return pid


func _check_pair(label: String, pair: Dictionary, check_replay: bool) -> void:
	var host := pair.get("host", {}) as Dictionary
	var guest := pair.get("guest", {}) as Dictionary
	_check("%s_both_passed" % label, bool(host.get("ok", false)) and bool(guest.get("ok", false)))
	_check("%s_tick_3000" % label, int(host.get("tick", -1)) == FINAL_TICK and int(guest.get("tick", -1)) == FINAL_TICK)
	_check("%s_hashes_equal" % label, not String(host.get("hash", "")).is_empty() and String(host.get("hash", "")) == String(guest.get("hash", "")))
	var guest_checks := int(guest.get("hash_checks", 0))
	if label == "drop":
		guest_checks += int(pair.get("pre_drop_hash_checks", 0))
	_check("%s_hash_exchange_count" % label, int(host.get("hash_checks", 0)) >= FINAL_TICK / HASH_INTERVAL - 1 and guest_checks >= FINAL_TICK / HASH_INTERVAL - 1)
	_check("%s_damage_events" % label, int(host.get("damage", 0)) > 0 and int(guest.get("damage", 0)) > 0)
	_check("%s_death_events" % label, int(host.get("death", 0)) > 0 and int(guest.get("death", 0)) > 0)
	if check_replay:
		_check("control_replay_hashes_equal", not String(host.get("replay_sha256", "")).is_empty() and String(host.get("replay_sha256", "")) == String(guest.get("replay_sha256", "")))


func _run_child(options: Dictionary) -> void:
	var role := String(options.get("child", ""))
	var phase := String(options.get("phase", ""))
	var log_path := String(options.get("log", ""))
	var result_path := String(options.get("result", ""))
	var replay_path := String(options.get("replay", ""))
	var resume := String(options.get("resume", "0")) == "1"
	_write_text(log_path, "NATIVE_LOCKSTEP_CHILD role=%s phase=%s resume=%s\n" % [role, phase, str(resume)])
	var client = SimHostClientScript.new()
	client.set_startup_timeout(120_000)
	var match_doc := _load_json(_repo_path("contracts/fixtures/match-launch-v1.json"))
	for player_value in match_doc.get("players", []) as Array:
		var player := player_value as Dictionary
		player["controller"] = "human"
		player.erase("ai_difficulty")
	match_doc["mode"] = "multiplayer"
	var bundle_path := OS.get_environment("OPENBFME_BUNDLE").strip_edges()
	var map_path := OS.get_environment("OPENBFME_MAP").strip_edges()
	if bundle_path.is_empty():
		bundle_path = _repo_path("workspace/logs/lane-cook-c/corpus-bundle-full.json")
	if map_path.is_empty():
		map_path = _repo_path("workspace/logs/lane-map-scene/fords.map-v1.json")
	if not FileAccess.file_exists(bundle_path) or not FileAccess.file_exists(map_path):
		_child_fail(client, result_path, log_path, "private bundle or map is absent")
		return
	if not client.launch_bundle_map(match_doc, bundle_path, map_path):
		_child_fail(client, result_path, log_path, "launch: %s" % client.last_error())
		return
	_write_json(String(options.get("pid-file", "")), {
		"godot_pid": OS.get_process_id(), "sidecar_pid": client.process_id(),
	})
	var armies := _spawn_armies(client)
	if armies.is_empty():
		_child_fail(client, result_path, log_path, "army setup: %s" % client.last_error())
		return
	if not client.record(replay_path):
		_child_fail(client, result_path, log_path, "record: %s" % client.last_error())
		return
	var bundle_doc := _load_json(bundle_path)
	var map_doc := _load_json(map_path)
	var bundle_identity := String((bundle_doc.get("source", {}) as Dictionary).get("effective_tree_sha256", ""))
	var map_identity := String((map_doc.get("source", {}) as Dictionary).get("sha256", ""))
	if map_identity.is_empty():
		map_identity = String((match_doc.get("map", {}) as Dictionary).get("path", ""))
	var session = NativeLockstepSessionScript.new()
	if not session.configure(client, match_doc, bundle_identity, map_identity, INPUT_DELAY, HASH_INTERVAL):
		_child_fail(client, result_path, log_path, session.last_error())
		return
	session.status_changed.connect(func(message: String) -> void:
		_append_text(log_path, "NATIVE_LOCKSTEP_STATUS %s\n" % message)
	)
	session.connected.connect(func(seat: int) -> void:
		_append_text(log_path, "NATIVE_LOCKSTEP_CONNECTED seat=%d\n" % seat)
	)
	session.disconnected.connect(func(seat: int) -> void:
		_append_text(log_path, "NATIVE_LOCKSTEP_DISCONNECTED seat=%d\n" % seat)
	)
	var port := int(options.get("port", "0"))
	var network_result: Error
	if role == "host":
		network_result = session.host(port)
	else:
		# Fresh peers take the next seat by connection order; only the restarted
		# process explicitly reclaims its former seat.
		network_result = session.join("127.0.0.1", port, 1 if resume else -1)
		if int(options.get("latency", "0")) > 0:
			session.inject_send_delay(int(options.get("latency", "0")))
	if network_result != OK:
		_child_fail(client, result_path, log_path, session.last_error())
		return
	if role == "host":
		_write_json(String(options.get("ready", "")), {"port": port, "pid": OS.get_process_id()})
	var handshake_deadline := Time.get_ticks_msec() + 120_000
	while not session.handshake_complete and Time.get_ticks_msec() < handshake_deadline:
		session.poll()
		await process_frame
	if not session.handshake_complete:
		_child_fail(client, result_path, log_path, "handshake timeout: %s" % session.last_error())
		return
	var owned := armies.get(role, []) as Array
	var target := Vector2(1500.0, 950.0)
	var command := _attack_move_command(owned, target)
	if not resume and not session.local_commands(command):
		_child_fail(client, result_path, log_path, "initial local command refused")
		return
	var damage := 0
	var death := 0
	var first_followup_sent: bool = session.current_tick >= 297
	var second_followup_sent: bool = session.current_tick >= 596
	var run_deadline := Time.get_ticks_msec() + (LATENCY_TIMEOUT_MS if int(options.get("latency", "0")) > 0 else PAIR_TIMEOUT_MS)
	while session.current_tick < FINAL_TICK and Time.get_ticks_msec() < run_deadline:
		var snapshot: Dictionary = session.step()
		if not session.handshake_complete:
			_child_fail(client, result_path, log_path, "lockstep connection lost")
			return
		if session.paused:
			break
		if not snapshot.is_empty():
			for event_value in snapshot.get("events", []) as Array:
				var kind := String((event_value as Dictionary).get("kind", ""))
				damage += 1 if kind == "damage" else 0
				death += 1 if kind == "death" else 0
		if role == "guest" and session.current_tick >= 297 and not first_followup_sent:
			first_followup_sent = session.local_commands(
				_attack_move_command(owned, Vector2(1200.0, 800.0))
			)
		if role == "guest" and session.current_tick >= 596 and not second_followup_sent:
			second_followup_sent = session.local_commands(
				_attack_move_command(owned, Vector2(1200.0, 800.0))
			)
		if phase == "drop" and role == "guest" and not resume and session.current_tick == DROP_TICK:
			_write_json(String(options.get("drop-marker", "")), {
				"tick": session.current_tick, "godot_pid": OS.get_process_id(),
				"sidecar_pid": client.process_id(), "hash_checks": session.hash_checks,
			})
			while true:
				await create_timer(1.0).timeout
		await process_frame
	var hash_deadline := Time.get_ticks_msec() + 15_000
	var expected_checks := FINAL_TICK / HASH_INTERVAL
	while session.hash_checks < expected_checks and Time.get_ticks_msec() < hash_deadline:
		session.poll()
		await process_frame
	var digest: String = client.hash()
	var sidecar_pid := client.process_id()
	var clean_quit: bool = client.quit()
	session.shutdown()
	var replay_sha := FileAccess.get_sha256(replay_path) if FileAccess.file_exists(replay_path) else ""
	var ok: bool = session.current_tick == FINAL_TICK and not session.paused and clean_quit \
		and not digest.is_empty() and damage > 0 and death > 0
	var result := {
		"ok": ok, "role": role, "phase": phase, "resumed": resume,
		"tick": session.current_tick, "hash": digest, "hash_checks": session.hash_checks,
		"stalled_ticks": session.stalled_ticks, "damage": damage, "death": death,
		"replay_sha256": replay_sha, "rejected_packets": session.rejected_packets,
		"sidecar_pid": sidecar_pid, "clean_quit": clean_quit,
		"error": session.last_error(),
	}
	_write_json(result_path, result)
	_append_text(log_path, "NATIVE_LOCKSTEP_CHILD_RESULT %s\n" % JSON.stringify(result))
	quit(0 if ok else 1)


func _spawn_armies(client) -> Dictionary:
	var catalog: Array[Dictionary] = client.templates()
	if catalog.is_empty():
		return {}
	var names := {
		"p0_fighter": _choose_horde(catalog, "GondorFighterHorde", "gondor", "fighter"),
		"p0_archer": _choose_horde(catalog, "GondorArcherHorde", "gondor", "archer"),
		"p1_fighter": _choose_horde(catalog, "MordorFighterHorde", "mordor", "fighter"),
		"p1_archer": _choose_horde(catalog, "MordorArcherHorde", "mordor", "archer"),
	}
	if names.values().has(""):
		return {}
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
	var host_ids: Array[int] = []
	var guest_ids: Array[int] = []
	for request in requests:
		var reply: Dictionary = client.spawn(String(request[0]), int(request[1]), request[2] as Vector2)
		if reply.is_empty():
			return {}
		if int(request[1]) == 0:
			host_ids.append(int(reply.get("id", 0)))
		else:
			guest_ids.append(int(reply.get("id", 0)))
	return {"host": host_ids, "guest": guest_ids}


func _choose_horde(catalog: Array[Dictionary], preferred: String, faction: String, role: String) -> String:
	var candidates: Array[String] = []
	for row in catalog:
		if not bool(row.get("horde", false)):
			continue
		var name := String(row.get("name", ""))
		if name.nocasecmp_to(preferred) == 0:
			return name
		var folded := name.to_lower()
		if folded.contains(faction) and folded.contains(role):
			candidates.append(name)
	candidates.sort_custom(func(a: String, b: String) -> bool: return a.naturalnocasecmp_to(b) < 0)
	return "" if candidates.is_empty() else candidates[0]


func _attack_move_command(objects: Array, target: Vector2) -> Dictionary:
	return {
		"schema": "openbfme.command.v1", "tick": 1, "seat": 0, "seq": 0,
		"commands": [{
			"type": "attack_move",
			"args": {"objects": objects.duplicate(), "x": target.x, "y": target.y},
		}],
	}


func _child_fail(client, result_path: String, log_path: String, reason: String) -> void:
	var sidecar_pid: int = client.process_id() if client != null else -1
	if client != null:
		client.quit()
	var result := {"ok": false, "tick": -1, "error": reason, "sidecar_pid": sidecar_pid}
	_write_json(result_path, result)
	_append_text(log_path, "NATIVE_LOCKSTEP_CHILD_FAIL %s\n" % reason)
	quit(1)


func _parse_args(args: PackedStringArray) -> Dictionary:
	var result: Dictionary = {}
	var index := 0
	while index < args.size():
		var key := String(args[index])
		if key.begins_with("--") and index + 1 < args.size():
			result[key.trim_prefix("--")] = String(args[index + 1])
			index += 2
		else:
			index += 1
	return result


func _wait_for_file(path: String, timeout_ms: int) -> bool:
	var deadline := Time.get_ticks_msec() + timeout_ms
	while Time.get_ticks_msec() < deadline:
		_watchdog.note("waiting_%s" % path.get_file())
		if FileAccess.file_exists(path):
			return true
		await create_timer(0.05).timeout
	return false


func _wait_for_json(path: String, timeout_ms: int) -> bool:
	var deadline := Time.get_ticks_msec() + timeout_ms
	while Time.get_ticks_msec() < deadline:
		_watchdog.note("waiting_%s" % path.get_file())
		if not _load_json(path).is_empty():
			return true
		await create_timer(0.1).timeout
	return false


func _cleanup_children() -> void:
	for pid_file in _pid_files:
		var row := _load_json(pid_file)
		_kill_if_running(int(row.get("sidecar_pid", -1)))
	for pid in _child_pids:
		_kill_if_running(pid)


func _kill_if_running(pid: int) -> void:
	if pid > 0 and OS.is_process_running(pid):
		OS.kill(pid)


func _remove_exact(path: String) -> void:
	if not path.is_empty() and FileAccess.file_exists(path):
		DirAccess.remove_absolute(path)


func _write_json(path: String, value: Dictionary) -> void:
	if path.is_empty():
		return
	DirAccess.make_dir_recursive_absolute(path.get_base_dir())
	var file := FileAccess.open(path, FileAccess.WRITE)
	if file != null:
		file.store_string(JSON.stringify(value, "\t") + "\n")
		file.close()


func _write_text(path: String, value: String) -> void:
	if path.is_empty():
		return
	DirAccess.make_dir_recursive_absolute(path.get_base_dir())
	var file := FileAccess.open(path, FileAccess.WRITE)
	if file != null:
		file.store_string(value)
		file.close()


func _append_text(path: String, value: String) -> void:
	var file := FileAccess.open(path, FileAccess.READ_WRITE)
	if file == null:
		_write_text(path, value)
		return
	file.seek_end()
	file.store_string(value)
	file.close()


func _load_json(path: String) -> Dictionary:
	if path.is_empty():
		return {}
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null:
		return {}
	var parsed: Variant = JSON.parse_string(file.get_as_text())
	file.close()
	return parsed as Dictionary if parsed is Dictionary else {}


func _repo_path(relative: String) -> String:
	var game_root := ProjectSettings.globalize_path("res://").trim_suffix("/").trim_suffix("\\")
	return game_root.get_base_dir().path_join(relative)


func _stalls(pair: Dictionary, role: String) -> int:
	return int((pair.get(role, {}) as Dictionary).get("stalled_ticks", 0))


func _check(label: String, condition: bool) -> void:
	if _watchdog != null:
		_watchdog.note(label)
	if condition:
		passed += 1
	else:
		failed += 1
		printerr("NATIVE_LOCKSTEP FAIL %s" % label)
