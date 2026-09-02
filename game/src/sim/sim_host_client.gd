class_name SimHostClient
extends RefCounted
## Non-blocking pipe client for OpenBfme.Host's line-delimited JSON protocol.
##
## Godot 4.7's OS.execute_with_pipe is used with non-blocking pipes. Each
## steady-state response line gets at most READ_TIMEOUT_MS (500 ms) of polling;
## step(K) performs one such bounded read per requested tick, so a frame that
## requests one tick can never wait longer than one steady-state read timeout.
## Initial .NET plus whole-corpus bundle/map load is allowed one loading-screen
## budget (30 s); steady-state reads remain bounded to 500 ms.

const READ_TIMEOUT_MS := 500
const STARTUP_TIMEOUT_MS := 30_000
const POLL_DELAY_MS := 1
const PACKED_OBJECT_FORMAT := "openbfme.snapshot.objects.packed.v1"
const PACKED_COLUMNS := [
	"id", "template", "owner", "state", "anim", "flags",
	"x", "y", "z", "yaw", "health", "max_health", "anim_frame",
]
const PACKED_INT_COLUMNS := 6

var _stdio: FileAccess
var _stderr: FileAccess
var _pid := -1
var _host_path := ""
var _last_error := ""
var _last_launch_reply: Dictionary = {}
var _read_buffer := ""
var _profile_read_usec := 0
var _profile_parse_usec := 0
var _profile_concat_usec := 0
var _profile_read_max_usec := 0
var _profile_parse_max_usec := 0
var _profile_documents := 0
var _profile_characters := 0
var _profile_step_usec := 0
var _profile_step_max_usec := 0
var _profile_steps := 0
var _packed_objects: Dictionary = {}
var _packed_tick := -1
var _stream_thread: Thread
var _stream_mutex := Mutex.new()
var _stream_running := false
var _stream_stop := false
var _stream_snapshots: Array[Dictionary] = []
var _stream_commands: Array[Dictionary] = []
var _stream_acknowledged_sequences: Array[int] = []
var _stream_error := ""
var _profile_mutex := Mutex.new()


func launch(match: Dictionary, templates_path: String) -> bool:
	return _launch_source(match, "templates", templates_path)


func launch_bundle(match: Dictionary, bundle_path: String) -> bool:
	return _launch_source(match, "bundle", bundle_path)


func launch_bundle_map(match: Dictionary, bundle_path: String, map_path: String) -> bool:
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


func spawn_at_start(template_name: String, player: int, start: int) -> Dictionary:
	var reply := _exchange({
		"op": "spawn",
		"template": template_name,
		"player": player,
		"start": start,
	})
	if String(reply.get("op", "")) != "spawned":
		_set_error(_reply_error("spawn", reply))
		return {}
	return reply


func send_commands(bundle: Dictionary) -> bool:
	_stream_mutex.lock()
	var streaming := _stream_running
	if streaming:
		_stream_commands.append(bundle.duplicate(true))
	_stream_mutex.unlock()
	if streaming:
		return true
	return _send_commands_now(bundle)


func _send_commands_now(bundle: Dictionary) -> bool:
	var reply := _exchange({"op": "commands", "bundle": bundle})
	if String(reply.get("op", "")) != "ack":
		_set_error(_reply_error("commands", reply))
		return false
	return true


func step(ticks: int, format: String = "json", timeout_ms: int = READ_TIMEOUT_MS) -> Array[Dictionary]:
	var step_started := Time.get_ticks_usec()
	var snapshots: Array[Dictionary] = []
	if format not in ["json", "packed"]:
		_set_error("step format must be json or packed")
		return snapshots
	var request := {"op": "step", "ticks": ticks}
	if format == "packed":
		request["format"] = "packed"
	if ticks < 0 or not _write_request(request):
		if ticks < 0:
			_set_error("step ticks must be non-negative")
		return snapshots
	for _index in ticks:
		var reply := _read_document(timeout_ms)
		if String(reply.get("op", "")) != "snapshot":
			_set_error(_reply_error("step", reply))
			return snapshots
		var snapshot_value: Variant = reply.get("snapshot")
		if not (snapshot_value is Dictionary):
			_set_error("step reply has no snapshot dictionary")
			return snapshots
		var snapshot := snapshot_value as Dictionary
		if String(reply.get("format", "json")) == "packed":
			snapshot = _decode_packed_snapshot(snapshot)
			if snapshot.is_empty():
				return snapshots
		snapshots.append(snapshot)
	var step_elapsed := Time.get_ticks_usec() - step_started
	_profile_mutex.lock()
	_profile_step_usec += step_elapsed
	_profile_step_max_usec = maxi(_profile_step_max_usec, step_elapsed)
	_profile_steps += 1
	_profile_mutex.unlock()
	return snapshots


func start_packed_stream() -> bool:
	_stream_mutex.lock()
	if _stream_running:
		_stream_mutex.unlock()
		return true
	_stream_stop = false
	_stream_error = ""
	_stream_snapshots.clear()
	_stream_commands.clear()
	_stream_acknowledged_sequences.clear()
	_stream_running = true
	_stream_mutex.unlock()
	_stream_thread = Thread.new()
	if _stream_thread.start(_stream_loop) != OK:
		_stream_mutex.lock()
		_stream_running = false
		_stream_mutex.unlock()
		_set_error("packed snapshot stream thread could not start")
		return false
	return true


func take_stream_snapshots() -> Array[Dictionary]:
	_stream_mutex.lock()
	var result := _stream_snapshots.duplicate()
	_stream_snapshots.clear()
	_stream_mutex.unlock()
	return result


func take_stream_command_acknowledgements() -> Array[int]:
	_stream_mutex.lock()
	var result := _stream_acknowledged_sequences.duplicate()
	_stream_acknowledged_sequences.clear()
	_stream_mutex.unlock()
	return result


func stream_error() -> String:
	_stream_mutex.lock()
	var result := _stream_error
	_stream_mutex.unlock()
	return result


func is_streaming() -> bool:
	_stream_mutex.lock()
	var result := _stream_running
	_stream_mutex.unlock()
	return result


func stop_packed_stream() -> void:
	_stream_mutex.lock()
	var thread := _stream_thread
	_stream_stop = true
	_stream_mutex.unlock()
	if thread != null and thread.is_started():
		thread.wait_to_finish()
	_stream_mutex.lock()
	_stream_running = false
	_stream_thread = null
	_stream_mutex.unlock()


func _stream_loop() -> void:
	while true:
		var commands: Array[Dictionary] = []
		_stream_mutex.lock()
		var should_stop := _stream_stop
		commands = _stream_commands.duplicate()
		_stream_commands.clear()
		_stream_mutex.unlock()
		if should_stop:
			break
		for bundle in commands:
			if not _send_commands_now(bundle):
				_stream_fail(_last_error)
				return
			_stream_mutex.lock()
			_stream_acknowledged_sequences.append(int(bundle.get("seq", -1)))
			_stream_mutex.unlock()
		var started := Time.get_ticks_msec()
		var snapshots := step(1, "packed")
		if snapshots.size() != 1:
			_stream_fail(_last_error)
			return
		_stream_mutex.lock()
		_stream_snapshots.append(snapshots[0])
		if _stream_snapshots.size() > 4:
			_stream_snapshots.pop_front()
		_stream_mutex.unlock()
		var remaining := 33 - int(Time.get_ticks_msec() - started)
		if remaining > 0:
			OS.delay_msec(remaining)
	_stream_mutex.lock()
	_stream_running = false
	_stream_mutex.unlock()


func _stream_fail(message: String) -> void:
	_stream_mutex.lock()
	_stream_error = message
	_stream_running = false
	_stream_mutex.unlock()


func _decode_packed_snapshot(snapshot: Dictionary) -> Dictionary:
	var packed_value: Variant = snapshot.get("objects")
	if not (packed_value is Dictionary):
		_set_error("packed snapshot has no objects dictionary")
		return {}
	var packed := packed_value as Dictionary
	if (
		String(packed.get("format", "")) != PACKED_OBJECT_FORMAT
		or String(packed.get("encoding", "")) != "base64+brotli"
		or int(packed.get("column_width_bytes", 0)) != 4
	):
		_set_error("packed snapshot object header is invalid")
		return {}
	var count := int(snapshot.get("object_count", -1))
	if count < 0:
		_set_error("packed snapshot object count is invalid")
		return {}
	var full := bool(packed.get("full", false))
	var slots: Array = []
	if not full:
		if int(packed.get("base_tick", -1)) != _packed_tick or _packed_objects.is_empty():
			_set_error("packed snapshot delta does not match its base tick")
			return {}
		var slots_value: Variant = packed.get("slots")
		if not (slots_value is Array):
			_set_error("packed snapshot delta has no slot array")
			return {}
		slots = slots_value as Array
		for slot_value in slots:
			var slot := int(slot_value)
			if slot < 0 or slot >= count:
				_set_error("packed snapshot delta slot is out of range")
				return {}
	var value_count := count if full else slots.size()
	var compressed := Marshalls.base64_to_raw(String(packed.get("data", "")))
	var uncompressed_bytes := int(packed.get("uncompressed_bytes", -1))
	if uncompressed_bytes < 0:
		_set_error("packed snapshot uncompressed byte count is invalid")
		return {}
	var bytes := compressed.decompress_dynamic(uncompressed_bytes, FileAccess.COMPRESSION_BROTLI)
	if bytes.size() != uncompressed_bytes:
		_set_error("packed snapshot Brotli payload could not be decoded")
		return {}
	if bytes.size() != value_count * PACKED_COLUMNS.size() * 4:
		_set_error("packed snapshot object payload length is invalid")
		return {}
	var objects := {} if full else _packed_objects.duplicate(true)
	for column_index in PACKED_COLUMNS.size():
		var values: Array = [] if full else (objects[PACKED_COLUMNS[column_index]] as Array)
		if full:
			values.resize(count)
		var offset := column_index * value_count * 4
		for value_index in value_count:
			var slot := value_index if full else int(slots[value_index])
			values[slot] = (
				bytes.decode_s32(offset + value_index * 4)
				if column_index < PACKED_INT_COLUMNS
				else bytes.decode_float(offset + value_index * 4)
			)
		objects[PACKED_COLUMNS[column_index]] = values
	_packed_objects = objects
	_packed_tick = int(snapshot.get("tick", -1))
	var decoded := snapshot.duplicate(false)
	decoded["objects"] = objects
	return decoded


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
	# Recording is a one-time startup operation. The whole-corpus host may still
	# be JIT-compiling after the map bootstrap, so use the startup budget here;
	# steady-state commands and snapshot reads retain READ_TIMEOUT_MS.
	var reply := _exchange_with_timeout(
		{"op": "record", "path": path},
		STARTUP_TIMEOUT_MS
	)
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
	stop_packed_stream()
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


func reset_profile() -> void:
	_profile_mutex.lock()
	_reset_profile_unlocked()
	_profile_mutex.unlock()


func _reset_profile_unlocked() -> void:
	_profile_read_usec = 0
	_profile_parse_usec = 0
	_profile_concat_usec = 0
	_profile_read_max_usec = 0
	_profile_parse_max_usec = 0
	_profile_documents = 0
	_profile_characters = 0
	_profile_step_usec = 0
	_profile_step_max_usec = 0
	_profile_steps = 0


func take_profile() -> Dictionary:
	_profile_mutex.lock()
	var result := {
		"read_usec": _profile_read_usec,
		"parse_usec": _profile_parse_usec,
		"concat_usec": _profile_concat_usec,
		"read_max_usec": _profile_read_max_usec,
		"parse_max_usec": _profile_parse_max_usec,
		"documents": _profile_documents,
		"characters": _profile_characters,
		"step_usec": _profile_step_usec,
		"step_max_usec": _profile_step_max_usec,
		"steps": _profile_steps,
	}
	_reset_profile_unlocked()
	_profile_mutex.unlock()
	return result


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
	var read_started := Time.get_ticks_usec()
	var document_parse_usec := 0
	var document_concat_usec := 0
	var document_characters := 0
	var deadline := Time.get_ticks_msec() + timeout_ms
	while Time.get_ticks_msec() <= deadline:
		var fragment := _stdio.get_line()
		if not fragment.is_empty():
			var concat_started := Time.get_ticks_usec()
			_read_buffer += fragment
			document_concat_usec += Time.get_ticks_usec() - concat_started
			document_characters += fragment.length()
			var parser := JSON.new()
			var parse_started := Time.get_ticks_usec()
			if parser.parse(_read_buffer) == OK:
				var parse_elapsed := Time.get_ticks_usec() - parse_started
				document_parse_usec += parse_elapsed
				var parsed: Variant = parser.data
				_read_buffer = ""
				var read_elapsed := Time.get_ticks_usec() - read_started
				_profile_mutex.lock()
				_profile_read_usec += read_elapsed
				_profile_parse_usec += document_parse_usec
				_profile_concat_usec += document_concat_usec
				_profile_characters += document_characters
				_profile_read_max_usec = maxi(_profile_read_max_usec, read_elapsed)
				_profile_parse_max_usec = maxi(_profile_parse_max_usec, document_parse_usec)
				_profile_documents += 1
				_profile_mutex.unlock()
				if parsed is Dictionary:
					return parsed as Dictionary
				_set_error("host returned a JSON value that is not an object")
				return {}
			var parse_elapsed := Time.get_ticks_usec() - parse_started
			document_parse_usec += parse_elapsed
		if _pid > 0 and not OS.is_process_running(_pid):
			var partial := " with %d buffered characters" % _read_buffer.length() if not _read_buffer.is_empty() else ""
			_set_error("host process %d exited before replying%s%s" % [_pid, partial, _stderr_suffix()])
			return {}
		OS.delay_msec(POLL_DELAY_MS)
	var partial := " with %d buffered characters" % _read_buffer.length() if not _read_buffer.is_empty() else ""
	_set_error("host response timed out after %d ms%s%s" % [timeout_ms, partial, _stderr_suffix()])
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
	_read_buffer = ""
	_packed_objects = {}
	_packed_tick = -1
