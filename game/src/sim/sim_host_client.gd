class_name SimHostClient
extends RefCounted
## Non-blocking pipe client for OpenBfme.Host's line-delimited JSON protocol.
##
## Godot 4.7's OS.execute_with_pipe is used with non-blocking pipes. Each
## steady-state response line gets at most READ_TIMEOUT_MS (500 ms) of polling;
## step(K) performs one such bounded read per requested tick, so a frame that
## requests one tick can never wait longer than one steady-state read timeout.
## Initial .NET cold start is allowed STARTUP_TIMEOUT_MS (5 s) and launch should
## be performed from a loading transition, not a gameplay frame.

const READ_TIMEOUT_MS := 500
const STARTUP_TIMEOUT_MS := 5000
const POLL_DELAY_MS := 1

var _stdio: FileAccess
var _stderr: FileAccess
var _pid := -1
var _host_path := ""
var _last_error := ""
var _last_launch_reply: Dictionary = {}


func launch(match: Dictionary, templates_path: String) -> bool:
	return _launch_source(match, "templates", templates_path)


func launch_bundle(match: Dictionary, bundle_path: String, map_path: String = "") -> bool:
	return _launch_source(match, "bundle", bundle_path, map_path)


func _launch_source(
	match: Dictionary, source_field: String, source_path: String, map_path: String = ""
) -> bool:
	if _stdio != null:
		_set_error("host is already running")
		return false
	if not _start_host():
		return false
	var request := {"op": "launch", "match": _wire_match(match)}
	request[source_field] = source_path
	if not map_path.is_empty():
		request["map"] = map_path
	var reply := _exchange_with_timeout(request, STARTUP_TIMEOUT_MS)
	if String(reply.get("op", "")) != "launched":
		_set_error(_reply_error("launch", reply))
		_close_pipes()
		return false
	_last_launch_reply = reply.duplicate(true)
	return true


func templates() -> Array[Dictionary]:
	var result: Array[Dictionary] = []
	var reply := _exchange({"op": "templates"})
	if String(reply.get("op", "")) != "templates":
		_set_error(_reply_error("templates", reply))
		return result
	var rows: Variant = reply.get("templates", [])
	if not (rows is Array):
		_set_error("templates reply has no template array")
		return result
	for row_value in rows as Array:
		if row_value is Dictionary:
			result.append((row_value as Dictionary).duplicate(true))
	return result


func spawn(template_name: String, player: int, position: Vector2) -> Dictionary:
	var reply := _exchange({
		"op": "spawn",
		"template": template_name,
		"player": player,
		"x": position.x,
		"y": position.y,
	})
	if String(reply.get("op", "")) != "spawned":
		_set_error(_reply_error("spawn", reply))
		return {}
	return reply


func send_commands(bundle: Dictionary) -> bool:
	var reply := _exchange({"op": "commands", "bundle": bundle})
	if String(reply.get("op", "")) != "ack":
		_set_error(_reply_error("commands", reply))
		return false
	return true


func step(ticks: int) -> Array[Dictionary]:
	var snapshots: Array[Dictionary] = []
	if ticks < 0 or not _write_request({"op": "step", "ticks": ticks}):
		if ticks < 0:
			_set_error("step ticks must be non-negative")
		return snapshots
	for _index in ticks:
		var reply := _read_document(READ_TIMEOUT_MS)
		if String(reply.get("op", "")) != "snapshot":
			_set_error(_reply_error("step", reply))
			return snapshots
		var snapshot_value: Variant = reply.get("snapshot")
		if not (snapshot_value is Dictionary):
			_set_error("step reply has no snapshot dictionary")
			return snapshots
		snapshots.append(snapshot_value as Dictionary)
	return snapshots


func hash() -> String:
	var reply := _exchange({"op": "hash"})
	if String(reply.get("op", "")) != "hash":
		_set_error(_reply_error("hash", reply))
		return ""
	return String(reply.get("hash", ""))


func save() -> Dictionary:
	var reply := _exchange({"op": "save"})
	if String(reply.get("op", "")) != "save":
		_set_error(_reply_error("save", reply))
		return {}
	return reply


func join(state: String, tick: int, catchup: Array) -> Dictionary:
	var reply := _exchange_with_timeout(
		{"op": "join", "state": state, "tick": tick, "catchup": catchup},
		STARTUP_TIMEOUT_MS
	)
	if String(reply.get("op", "")) != "joined":
		_set_error(_reply_error("join", reply))
		return {}
	return reply


func diff(state: String, path: String = "") -> Dictionary:
	var request := {"op": "diff", "state": state}
	if not path.is_empty():
		request["path"] = path
	var reply := _exchange(request)
	if String(reply.get("op", "")) != "diff":
		_set_error(_reply_error("diff", reply))
		return {}
	return reply


func record(path: String) -> bool:
	var reply := _exchange({"op": "record", "path": path})
	if String(reply.get("op", "")) != "recording":
		_set_error(_reply_error("record", reply))
		return false
	return true


func replay(path: String, verify: bool = true) -> Dictionary:
	if _stdio != null:
		_set_error("host is already running")
		return {}
	if not _start_host():
		return {}
	if not _write_request({"op": "replay", "path": path, "verify": verify}):
		_close_pipes()
		return {}
	var progress: Array[Dictionary] = []
	while true:
		var reply := _read_document(STARTUP_TIMEOUT_MS * 6 if progress.is_empty() else READ_TIMEOUT_MS)
		var op := String(reply.get("op", ""))
		if op == "replay_progress":
			progress.append(reply)
			continue
		if op == "replay_done":
			return {"progress": progress, "done": reply}
		_set_error(_reply_error("replay", reply))
		_close_pipes()
		return {}
	return {}


func quit() -> bool:
	if _stdio == null:
		return true
	var reply := _exchange({"op": "quit"})
	var clean := String(reply.get("op", "")) == "quit"
	if not clean:
		_set_error(_reply_error("quit", reply))
	_close_pipes()
	return clean


func last_error() -> String:
	return _last_error


func host_path() -> String:
	return _host_path


func launch_reply() -> Dictionary:
	return _last_launch_reply.duplicate(true)


func _exchange(request: Dictionary) -> Dictionary:
	return _exchange_with_timeout(request, READ_TIMEOUT_MS)


func _exchange_with_timeout(request: Dictionary, timeout_ms: int) -> Dictionary:
	if not _write_request(request):
		return {}
	return _read_document(timeout_ms)


func _start_host() -> bool:
	var resolved := _resolve_host_path()
	if resolved.is_empty():
		_set_error("OpenBfme.Host executable was not found")
		return false
	_host_path = String(resolved[0])
	print("SIM_HOST_EXECUTABLE source=%s path=%s" % [String(resolved[1]), _host_path])
	var pipe := OS.execute_with_pipe(_host_path, PackedStringArray(), false)
	if pipe.is_empty():
		_set_error("OS.execute_with_pipe could not start %s" % _host_path)
		return false
	_stdio = pipe.get("stdio") as FileAccess
	_stderr = pipe.get("stderr") as FileAccess
	_pid = int(pipe.get("pid", -1))
	if _stdio == null or _pid <= 0:
		_set_error("host pipe did not return stdio and pid")
		_close_pipes()
		return false
	return true


func _write_request(request: Dictionary) -> bool:
	if _stdio == null:
		_set_error("host is not running")
		return false
	_stdio.store_line(JSON.stringify(request))
	_stdio.flush()
	var error := _stdio.get_error()
	if error != OK:
		_set_error("host pipe write failed with error %d" % error)
		return false
	return true


func _read_document(timeout_ms: int) -> Dictionary:
	if _stdio == null:
		_set_error("host is not running")
		return {}
	var deadline := Time.get_ticks_msec() + timeout_ms
	while Time.get_ticks_msec() <= deadline:
		var line := _stdio.get_line()
		if not line.is_empty():
			var parsed: Variant = JSON.parse_string(line)
			if parsed is Dictionary:
				return parsed as Dictionary
			_set_error("host returned malformed JSON: %s" % line)
			return {}
		if _pid > 0 and not OS.is_process_running(_pid):
			_set_error("host process %d exited before replying%s" % [_pid, _stderr_suffix()])
			return {}
		OS.delay_msec(POLL_DELAY_MS)
	_set_error("host response timed out after %d ms%s" % [timeout_ms, _stderr_suffix()])
	return {}


func _resolve_host_path() -> Array:
	var configured := OS.get_environment("OPENBFME_SIM_HOST").strip_edges()
	if not configured.is_empty() and FileAccess.file_exists(configured):
		return [configured, "OPENBFME_SIM_HOST"]
	var release := ProjectSettings.globalize_path(
		"res://../engine/OpenBfme.Host/bin/Release/net8.0/OpenBfme.Host.exe"
	)
	if FileAccess.file_exists(release):
		return [release, "release"]
	var debug := ProjectSettings.globalize_path(
		"res://../engine/OpenBfme.Host/bin/Debug/net8.0/OpenBfme.Host.exe"
	)
	if FileAccess.file_exists(debug):
		return [debug, "debug"]
	return []


func _wire_match(match: Dictionary) -> Dictionary:
	var wire := match.duplicate(true)
	wire["seed"] = int(wire.get("seed", 0))
	var rules_value: Variant = wire.get("rules")
	if rules_value is Dictionary:
		var rules := rules_value as Dictionary
		rules["tick_ms"] = int(rules.get("tick_ms", 0))
		rules["starting_resources"] = int(rules.get("starting_resources", 0))
	var players_value: Variant = wire.get("players")
	if players_value is Array:
		for player_value in players_value as Array:
			if not (player_value is Dictionary):
				continue
			var player := player_value as Dictionary
			player["seat"] = int(player.get("seat", 0))
			player["team"] = int(player.get("team", 0))
			for optional_integer in ["color", "start_position"]:
				if player.has(optional_integer):
					player[optional_integer] = int(player[optional_integer])
	return wire


func _reply_error(operation: String, reply: Dictionary) -> String:
	if String(reply.get("op", "")) == "error":
		return "%s failed: %s" % [operation, String(reply.get("message", "host error"))]
	return "%s received no valid reply" % operation


func _stderr_suffix() -> String:
	if _stderr == null:
		return ""
	var line := _stderr.get_line().strip_edges()
	return "" if line.is_empty() else " (stderr: %s)" % line


func _set_error(message: String) -> void:
	_last_error = message
	push_error("[SimHostClient] %s" % message)


func _close_pipes() -> void:
	if _stdio != null:
		_stdio.close()
	if _stderr != null:
		_stderr.close()
	_stdio = null
	_stderr = null
	_pid = -1
	_last_launch_reply = {}
