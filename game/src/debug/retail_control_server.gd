class_name RetailControlServer
extends RefCounted

## Debug-gated local Game Control API: a loopback-only WebSocket JSON-RPC
## server that lets external tools (AI agents, the automated playtester) drive
## and inspect a running RetailSliceSim without touching the UI.
##
## Activation contract: the host only constructs/starts this server when the
## OPENBFME_CONTROL_PORT environment variable is set (see start_from_env()).
## The socket binds 127.0.0.1 exclusively — never a public interface.
##
## Protocol: each WebSocket text frame is a JSON object
##   {"id": <any>, "method": "state.summary", "params": {...}}
## and every frame gets exactly one reply
##   {"id": <same>, "ok": true, "result": {...}}  or
##   {"id": <same>, "ok": false, "error": "<reason>"}
## Malformed JSON and unknown methods produce error replies, never crashes.
## The host must call poll() once per frame/iteration.

const CommandScript = preload("res://src/retail_slice/retail_command.gd")

const BIND_ADDRESS := "127.0.0.1"
const MAX_STEP_TICKS := 1000
const MAX_PEERS := 8
## args keys whose [x, y] JSON arrays are decoded into Vector2 world points.
const VECTOR2_ARG_KEYS: Array[String] = ["destination", "position", "point", "target_point", "rally"]

var port := 0

var _sim: RefCounted
var _external_stepping_allowed := false
var _tcp: TCPServer
var _peers: Array[WebSocketPeer] = []


func _init(sim: RefCounted, external_stepping_allowed := false) -> void:
	_sim = sim
	_external_stepping_allowed = external_stepping_allowed


## Env-gated activation: only binds when OPENBFME_CONTROL_PORT is set.
func start_from_env() -> bool:
	var port_text := OS.get_environment("OPENBFME_CONTROL_PORT").strip_edges()
	if port_text == "":
		return false
	if not port_text.is_valid_int():
		push_warning("RetailControlServer: OPENBFME_CONTROL_PORT '%s' is not an integer; control API disabled" % port_text)
		return false
	return start(int(port_text)) == OK


## Binds 127.0.0.1:<listen_port> (0 selects an ephemeral port; read `port`).
func start(listen_port: int) -> Error:
	if _tcp != null and _tcp.is_listening():
		return ERR_ALREADY_IN_USE
	_tcp = TCPServer.new()
	var error := _tcp.listen(listen_port, BIND_ADDRESS)
	if error != OK:
		push_warning("RetailControlServer: cannot listen on %s:%d (error %d)" % [BIND_ADDRESS, listen_port, error])
		_tcp = null
		return error
	port = _tcp.get_local_port()
	print("[RetailControlServer] listening on ws://%s:%d" % [BIND_ADDRESS, port])
	return OK


func is_listening() -> bool:
	return _tcp != null and _tcp.is_listening()


func stop() -> void:
	for peer in _peers:
		peer.close()
	_peers.clear()
	if _tcp != null:
		_tcp.stop()
		_tcp = null


## Must be called once per frame/iteration by the host.
func poll() -> void:
	if _tcp == null or not _tcp.is_listening():
		return
	while _tcp.is_connection_available():
		var connection := _tcp.take_connection()
		if connection == null:
			break
		if _peers.size() >= MAX_PEERS:
			connection.disconnect_from_host()
			continue
		var ws := WebSocketPeer.new()
		if ws.accept_stream(connection) == OK:
			_peers.append(ws)
	for peer: WebSocketPeer in _peers.duplicate():
		peer.poll()
		match peer.get_ready_state():
			WebSocketPeer.STATE_OPEN:
				while peer.get_available_packet_count() > 0:
					var packet := peer.get_packet()
					if peer.was_string_packet():
						_handle_frame(peer, packet.get_string_from_utf8())
					else:
						_send(peer, null, false, "binary-frames-unsupported")
			WebSocketPeer.STATE_CLOSED:
				_peers.erase(peer)


func _handle_frame(peer: WebSocketPeer, text: String) -> void:
	var parsed: Variant = JSON.parse_string(text)
	if typeof(parsed) != TYPE_DICTIONARY:
		_send(peer, null, false, "malformed-json")
		return
	var frame := parsed as Dictionary
	var id: Variant = frame.get("id")
	if typeof(frame.get("method")) != TYPE_STRING:
		_send(peer, id, false, "missing-method")
		return
	var method := String(frame["method"])
	var params_value: Variant = frame.get("params", {})
	var params: Dictionary = params_value if typeof(params_value) == TYPE_DICTIONARY else {}
	var reply := _dispatch(method, params)
	_send(peer, id, bool(reply["ok"]), reply["payload"])


func _send(peer: WebSocketPeer, id: Variant, ok: bool, payload: Variant) -> void:
	var frame := {"id": id, "ok": ok}
	if ok:
		frame["result"] = payload
	else:
		frame["error"] = payload
	peer.send_text(JSON.stringify(frame))


func _dispatch(method: String, params: Dictionary) -> Dictionary:
	match method:
		"state.summary":
			return _ok(_state_summary())
		"state.entities":
			return _ok(_entity_rows())
		"state.structures":
			return _ok(_structure_rows())
		"state.hash":
			return _ok({"tick": _sim.tick_index, "hash": _sim.state_hash()})
		"sim.step":
			return _sim_step(params)
		"command.submit":
			return _command_submit(params)
		"command.apply":
			return _command_apply(params)
		"snapshot.save":
			return _ok({"data": Marshalls.raw_to_base64(_sim.snapshot())})
		"snapshot.restore":
			return _snapshot_restore(params)
		"sim.gaps", "meta.commands":
			return _ok({"command_types": Array(CommandScript.COMMAND_TYPES)})
	return {"ok": false, "payload": "unknown-method: %s" % method}


func _ok(payload: Variant) -> Dictionary:
	return {"ok": true, "payload": payload}


func _error(reason: String) -> Dictionary:
	return {"ok": false, "payload": reason}


func _state_summary() -> Dictionary:
	var resources := {}
	for team_value in (_sim.team_resources as Dictionary).keys():
		resources[str(team_value)] = int(_sim.team_resources[team_value])
	return {
		"tick": _sim.tick_index,
		"hash": _sim.state_hash(),
		"team_resources": resources,
		"entity_count": (_sim.entities as Dictionary).size(),
		"structure_count": (_sim.structures as Dictionary).size(),
	}


func _entity_rows() -> Array:
	var rows: Array = []
	for id in _sim.entity_ids():
		var row: Dictionary = _sim.entities[id]
		var position := Vector2(row.get("position", Vector2.ZERO))
		rows.append({
			"id": int(row.get("id", id)),
			"team": int(row.get("team", -1)),
			"position": [position.x, position.y],
			"health": int(row.get("health", 0)),
			"unit_type": String(row.get("unit_type", "")),
		})
	return rows


func _structure_rows() -> Array:
	var rows: Array = []
	for id in _sim.structure_ids():
		var row: Dictionary = _sim.structures[id]
		var position := Vector2(row.get("position", Vector2.ZERO))
		rows.append({
			"id": int(row.get("id", id)),
			"team": int(row.get("team", -1)),
			"position": [position.x, position.y],
			"health": int(row.get("health", 0)),
			"structure_kind": String(row.get("structure_kind", "")),
		})
	return rows


func _sim_step(params: Dictionary) -> Dictionary:
	if not _external_stepping_allowed:
		return _error("stepping-refused: the simulation is driven by a live session/frame loop")
	if typeof(params.get("ticks")) not in [TYPE_INT, TYPE_FLOAT]:
		return _error("invalid-params: ticks must be an integer")
	var ticks := int(params["ticks"])
	if ticks < 1 or ticks > MAX_STEP_TICKS:
		return _error("invalid-params: ticks must be in 1..%d" % MAX_STEP_TICKS)
	_sim.advance(ticks)
	return _ok({"tick": _sim.tick_index, "hash": _sim.state_hash()})


func _command_submit(params: Dictionary) -> Dictionary:
	var cmd := _normalized_command(params)
	if cmd.is_empty():
		return _error("invalid-params: command must be an object")
	if not CommandScript.validate(cmd):
		return _error("invalid-command: failed RetailCommand.validate")
	return _ok({"accepted": bool(_sim.submit_command(cmd))})


func _command_apply(params: Dictionary) -> Dictionary:
	var cmd := _normalized_command(params)
	if cmd.is_empty():
		return _error("invalid-params: command must be an object")
	if not CommandScript.validate(cmd):
		return _error("invalid-command: failed RetailCommand.validate")
	_sim.apply_command(cmd)
	return _ok({"applied": true, "last_command_result": _jsonable(_sim.last_command_result)})


func _snapshot_restore(params: Dictionary) -> Dictionary:
	if not _external_stepping_allowed:
		# Restoring under a live frame loop yanks authoritative state out from
		# under the presentation (and would desync any networked session).
		return _error("restore-refused: the simulation is driven by a live session/frame loop")
	if typeof(params.get("data")) != TYPE_STRING:
		return _error("invalid-params: data must be a base64 string")
	var bytes := Marshalls.base64_to_raw(String(params["data"]))
	if bytes.is_empty():
		return _error("invalid-params: data is not valid base64")
	var restored := bool(_sim.restore(bytes))
	if not restored:
		return _error("restore-failed")
	return _ok({"restored": true, "tick": _sim.tick_index, "hash": _sim.state_hash()})


## JSON transports only doubles/strings/arrays/objects. Rebuild the exact
## 5-key command shape RetailCommand.validate demands: integral scalars become
## ints and whitelisted [x, y] arg arrays become Vector2 world points.
func _normalized_command(params: Dictionary) -> Dictionary:
	var raw_value: Variant = params.get("command")
	if typeof(raw_value) != TYPE_DICTIONARY:
		return {}
	var raw := raw_value as Dictionary
	var args_value: Variant = raw.get("args", {})
	return {
		"tick": _coerced_scalar(raw.get("tick")),
		"team": _coerced_scalar(raw.get("team")),
		"seq": _coerced_scalar(raw.get("seq")),
		"type": raw.get("type"),
		"args": _normalized_args(args_value as Dictionary) if typeof(args_value) == TYPE_DICTIONARY else args_value,
	}


func _normalized_args(raw: Dictionary) -> Dictionary:
	var args := {}
	for key_value in raw.keys():
		var key := String(key_value)
		var value: Variant = raw[key_value]
		if VECTOR2_ARG_KEYS.has(key) and _is_point_array(value):
			args[key] = Vector2(float((value as Array)[0]), float((value as Array)[1]))
		else:
			args[key] = _coerced_scalar(value)
	return args


func _is_point_array(value: Variant) -> bool:
	if typeof(value) != TYPE_ARRAY or (value as Array).size() != 2:
		return false
	for item in value as Array:
		if typeof(item) not in [TYPE_INT, TYPE_FLOAT]:
			return false
	return true


func _coerced_scalar(value: Variant) -> Variant:
	match typeof(value):
		TYPE_FLOAT:
			var number := float(value)
			return int(number) if number == floorf(number) else number
		TYPE_ARRAY:
			var items: Array = []
			for item in value as Array:
				items.append(_coerced_scalar(item))
			return items
	return value


func _jsonable(value: Variant) -> Variant:
	match typeof(value):
		TYPE_VECTOR2:
			return [(value as Vector2).x, (value as Vector2).y]
		TYPE_DICTIONARY:
			var out := {}
			for key in (value as Dictionary).keys():
				out[str(key)] = _jsonable((value as Dictionary)[key])
			return out
		TYPE_ARRAY:
			var items: Array = []
			for item in value as Array:
				items.append(_jsonable(item))
			return items
	return value
