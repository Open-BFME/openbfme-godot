class_name RetailLockstepSession
extends RefCounted

const CommandScript = preload("res://src/retail_slice/retail_command.gd")
const PROTOCOL_VERSION := 1
const MAX_PACKET_BYTES := 256 * 1024
const MAX_COMMANDS_PER_BUNDLE := 256
const HASH_INTERVAL_TICKS := 30

var input_delay_ticks := 3
var sim: RetailSliceSim
var local_team := -1
var peer_team := -1
var connected := false
var handshake_complete := false
var desynced := false
var desync_tick := -1
var bound_port := 0
var remote_ack_tick := 0
var rejected_packets := 0
var rejected_remote_commands := 0
var pause_command_tick := -1
var pause_command_state := false

var _connection: ENetConnection
var _peer: ENetPacketPeer
var _is_host := false
var _next_local_seq := 0
var _hello_sent := false
var _local_commands_by_tick: Dictionary = {}
var _remote_bundles: Dictionary = {}
var _bundle_sent_through := 0
var _remote_bundle_contiguous := 0
var _last_remote_seq := -1
var _hash_barrier_tick := -1
var _hash_sent := false
var _local_hash := ""
var _remote_hashes: Dictionary = {}
var _pause_commands_by_tick: Dictionary = {}
var _deferred_local_submissions: Array[Dictionary] = []


func _init(simulation: RetailSliceSim = null) -> void:
	sim = simulation


func host(port: int) -> Error:
	close()
	_is_host = true
	local_team = 0
	peer_team = 1
	_connection = ENetConnection.new()
	var error := _connection.create_host_bound("*", port, 1, 1)
	if error != OK:
		_connection = null
		return error
	bound_port = _connection.get_local_port()
	return OK


func join(address: String, port: int) -> Error:
	close()
	_is_host = false
	local_team = 1
	peer_team = 0
	_connection = ENetConnection.new()
	var error := _connection.create_host(1, 1)
	if error != OK:
		_connection = null
		return error
	_peer = _connection.connect_to_host(address, port, 1)
	if _peer == null:
		close()
		return ERR_CANT_CONNECT
	return OK


func close() -> void:
	if _peer != null and _peer.is_active():
		_peer.reset()
	if _connection != null:
		_connection.destroy()
	_connection = null
	_peer = null
	connected = false
	handshake_complete = false
	desynced = false
	desync_tick = -1
	bound_port = 0
	remote_ack_tick = 0
	rejected_packets = 0
	rejected_remote_commands = 0
	pause_command_tick = -1
	pause_command_state = false
	_next_local_seq = 0
	_hello_sent = false
	_local_commands_by_tick.clear()
	_remote_bundles.clear()
	_bundle_sent_through = 0
	_remote_bundle_contiguous = 0
	_last_remote_seq = -1
	_hash_barrier_tick = -1
	_hash_sent = false
	_local_hash = ""
	_remote_hashes.clear()
	_pause_commands_by_tick.clear()
	_deferred_local_submissions.clear()


func submit_local(type: String, args: Dictionary) -> void:
	if sim == null or desynced or input_delay_ticks <= 0:
		return
	if _hash_barrier_tick >= 0:
		_deferred_local_submissions.append({"type": type, "args": args.duplicate(true)})
		return
	_submit_local_now(type, args)


func _submit_local_now(type: String, args: Dictionary) -> void:
	var command_tick := sim.tick_index + input_delay_ticks
	# A tick bundle is immutable once sent. Normal callers submit input before
	# poll() finalizes the current tick's delayed bundle.
	if command_tick <= _bundle_sent_through:
		return
	var command := {
		"tick": command_tick,
		"team": local_team,
		"seq": _next_local_seq,
		"type": type,
		"args": args.duplicate(true),
	}
	if not CommandScript.validate(command) or not sim.submit_command(command):
		return
	_next_local_seq += 1
	var commands: Array = _local_commands_by_tick.get(command_tick, [])
	commands.append(command)
	_local_commands_by_tick[command_tick] = commands
	if type == "pause" or type == "resume":
		var pause_commands: Array = _pause_commands_by_tick.get(command_tick, [])
		pause_commands.append(command)
		_pause_commands_by_tick[command_tick] = pause_commands


func poll() -> void:
	if _connection == null:
		return
	_service_events()
	if not connected or _peer == null:
		return
	if not _hello_sent:
		_send_envelope({
			"kind": "hello",
			"version": PROTOCOL_VERSION,
			"team": local_team,
			"peer_team": peer_team,
		})
		_hello_sent = true
	if handshake_complete:
		_send_ready_bundles()
		_maybe_send_hash()
	_connection.flush()
	_service_events()
	if handshake_complete:
		_maybe_send_hash()
		_connection.flush()


func can_advance() -> bool:
	if sim == null or not handshake_complete or desynced or _hash_barrier_tick >= 0:
		return false
	if sim.clock_paused and not sim._has_pending_resume_command():
		return false
	var next_tick := sim.tick_index + 1
	return next_tick <= _bundle_sent_through and _remote_bundles.has(next_tick)


func advance_if_ready() -> bool:
	if not can_advance():
		return false
	var next_tick := sim.tick_index + 1
	_remote_bundles.erase(next_tick)
	sim.tick()
	if sim.tick_index != next_tick:
		return false
	if _pause_commands_by_tick.has(next_tick):
		pause_command_tick = next_tick
		pause_command_state = sim.clock_paused
		_pause_commands_by_tick.erase(next_tick)
	if sim.tick_index % HASH_INTERVAL_TICKS == 0:
		_hash_barrier_tick = sim.tick_index
		_hash_sent = false
		_local_hash = ""
	return true


func _service_events() -> void:
	for _index in range(512):
		var event := _connection.service(0)
		if event.is_empty() or int(event[0]) == ENetConnection.EVENT_NONE:
			return
		var event_peer := event[1] as ENetPacketPeer
		match int(event[0]):
			ENetConnection.EVENT_CONNECT:
				if _peer != null and event_peer != _peer:
					event_peer.reset()
					continue
				_peer = event_peer
				connected = true
			ENetConnection.EVENT_DISCONNECT:
				if event_peer == _peer:
					connected = false
					handshake_complete = false
			ENetConnection.EVENT_RECEIVE:
				if event_peer != _peer:
					rejected_packets += 1
					continue
				_receive_packet(event_peer.get_packet())


func _send_ready_bundles() -> void:
	# At tick T, T + delay remains open for input until the sim leaves T.
	# This also preserves an exact delayed slot for a later resume while paused.
	var send_through := sim.tick_index + input_delay_ticks - 1
	for bundle_tick in range(_bundle_sent_through + 1, send_through + 1):
		var commands: Array = _local_commands_by_tick.get(bundle_tick, [])
		commands.sort_custom(func(a: Dictionary, b: Dictionary) -> bool: return int(a["seq"]) < int(b["seq"]))
		_send_envelope({
			"kind": "bundle",
			"tick": bundle_tick,
			"commands": commands,
			"ack": _remote_bundle_contiguous,
		})
		_bundle_sent_through = bundle_tick


func _maybe_send_hash() -> void:
	if _hash_barrier_tick < 0 or _hash_sent or desynced:
		return
	var required_bundle_tick := _hash_barrier_tick + input_delay_ticks - 1
	if _remote_bundle_contiguous < required_bundle_tick:
		return
	_local_hash = sim.state_hash()
	_hash_sent = true
	_send_envelope({"kind": "hash", "tick": _hash_barrier_tick, "hash": _local_hash})
	_compare_hash_if_ready(_hash_barrier_tick)


func _send_envelope(envelope: Dictionary) -> void:
	if _peer == null or not _peer.is_active():
		return
	var packet := var_to_bytes(envelope)
	if packet.is_empty() or packet.size() > MAX_PACKET_BYTES:
		return
	_peer.send(0, packet, ENetPacketPeer.FLAG_RELIABLE)


func _receive_packet(packet: PackedByteArray) -> void:
	if packet.is_empty() or packet.size() > MAX_PACKET_BYTES:
		rejected_packets += 1
		return
	var decoded: Variant = bytes_to_var(packet)
	if typeof(decoded) != TYPE_DICTIONARY:
		rejected_packets += 1
		return
	var envelope := decoded as Dictionary
	if typeof(envelope.get("kind")) != TYPE_STRING:
		rejected_packets += 1
		return
	match String(envelope["kind"]):
		"hello":
			_receive_hello(envelope)
		"bundle":
			_receive_bundle(envelope)
		"hash":
			_receive_hash(envelope)
		_:
			rejected_packets += 1


func _receive_hello(envelope: Dictionary) -> void:
	if envelope.size() != 4 \
		or typeof(envelope.get("version")) != TYPE_INT \
		or typeof(envelope.get("team")) != TYPE_INT \
		or typeof(envelope.get("peer_team")) != TYPE_INT \
		or int(envelope["version"]) != PROTOCOL_VERSION \
		or int(envelope["team"]) != peer_team \
		or int(envelope["peer_team"]) != local_team:
		rejected_packets += 1
		return
	handshake_complete = true


func _receive_bundle(envelope: Dictionary) -> void:
	if envelope.size() != 4 \
		or typeof(envelope.get("tick")) != TYPE_INT \
		or typeof(envelope.get("commands")) != TYPE_ARRAY \
		or typeof(envelope.get("ack")) != TYPE_INT:
		rejected_packets += 1
		return
	var bundle_tick := int(envelope["tick"])
	var ack_tick := int(envelope["ack"])
	var commands := envelope["commands"] as Array
	if bundle_tick <= 0 or ack_tick < 0 or ack_tick > _bundle_sent_through or commands.size() > MAX_COMMANDS_PER_BUNDLE:
		rejected_packets += 1
		return
	if bundle_tick <= _remote_bundle_contiguous:
		return
	if bundle_tick != _remote_bundle_contiguous + 1 or bundle_tick <= sim.tick_index:
		rejected_packets += 1
		return
	var seen_sequences: Dictionary = {}
	var last_sequence := _last_remote_seq
	for command_value in commands:
		if typeof(command_value) != TYPE_DICTIONARY:
			rejected_packets += 1
			return
		var command := command_value as Dictionary
		if not CommandScript.validate(command) \
			or int(command["tick"]) != bundle_tick \
			or int(command["team"]) != peer_team \
			or seen_sequences.has(int(command["seq"])) \
			or int(command["seq"]) <= last_sequence:
			rejected_remote_commands += 1
			return
		seen_sequences[int(command["seq"])] = true
		last_sequence = int(command["seq"])
	for command_value in commands:
		if not sim.submit_command((command_value as Dictionary).duplicate(true)):
			rejected_packets += 1
			return
		var command := command_value as Dictionary
		if String(command["type"]) == "pause" or String(command["type"]) == "resume":
			var pause_commands: Array = _pause_commands_by_tick.get(bundle_tick, [])
			pause_commands.append(command.duplicate(true))
			_pause_commands_by_tick[bundle_tick] = pause_commands
	_remote_bundles[bundle_tick] = true
	_last_remote_seq = last_sequence
	remote_ack_tick = maxi(remote_ack_tick, ack_tick)
	while _remote_bundles.has(_remote_bundle_contiguous + 1):
		_remote_bundle_contiguous += 1


func _receive_hash(envelope: Dictionary) -> void:
	if envelope.size() != 3 \
		or typeof(envelope.get("tick")) != TYPE_INT \
		or typeof(envelope.get("hash")) != TYPE_STRING:
		rejected_packets += 1
		return
	var hash_tick := int(envelope["tick"])
	var hash_value := String(envelope["hash"])
	if hash_tick <= 0 or hash_tick % HASH_INTERVAL_TICKS != 0 or hash_value.length() != 64 or not hash_value.is_valid_hex_number(false):
		rejected_packets += 1
		return
	_remote_hashes[hash_tick] = hash_value.to_lower()
	_compare_hash_if_ready(hash_tick)


func _compare_hash_if_ready(hash_tick: int) -> void:
	if not _hash_sent or hash_tick != _hash_barrier_tick or not _remote_hashes.has(hash_tick):
		return
	if String(_remote_hashes[hash_tick]) != _local_hash.to_lower():
		desynced = true
		desync_tick = hash_tick
		return
	_remote_hashes.erase(hash_tick)
	_hash_barrier_tick = -1
	_hash_sent = false
	_local_hash = ""
	var deferred := _deferred_local_submissions.duplicate(true)
	_deferred_local_submissions.clear()
	for submission in deferred:
		_submit_local_now(String(submission["type"]), submission["args"] as Dictionary)
