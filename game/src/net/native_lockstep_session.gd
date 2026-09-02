class_name NativeLockstepSession
extends RefCounted
## Deterministic native-core lockstep over reliable ENet packets.
##
## This deliberately uses ENetMultiplayerPeer through MultiplayerPeer's packet
## API, not scene RPC.  The simulation never sees sockets: it receives only the
## complete command-v1 bundles for one tick, ordered by (team, seq, seat).

signal connected(seat: int)
signal disconnected(seat: int)
signal desync_detected(tick: int, local_hash: String, remote_hash: String, report_path: String)
signal status_changed(message: String)

const WIRE_SCHEMA := "openbfme.lockstep-wire.v1"
const HOST_PEER_ID := 1
const MAX_SEATS := 8
const CONTROL_CHANNEL := 0
const BUNDLE_CHANNEL := 1
const HASH_CHANNEL := 2

var local_seat := -1
var current_tick := 0
var input_delay := 3
var hash_interval := 30
var stalled_ticks := 0
var hash_checks := 0
var rejected_packets := 0
var handshake_complete := false
var paused := false

var _client
var _peer: ENetMultiplayerPeer
var _is_host := false
var _match: Dictionary = {}
var _launch_identity := ""
var _bundle_identity := ""
var _map_identity := ""
var _seat_count := 2
var _peer_seats: Dictionary = {}
var _seat_peers: Dictionary = {}
var _bundles: Dictionary = {}
var _hashes: Dictionary = {}
var _pending_commands: Array = []
var _next_seq := 0
var _primed := false
var _handshake_sent := false
var _last_snapshot: Dictionary = {}
var _step_in_flight := false
var _rejoin_thread: Thread
var _rejoin_future_bundles: Array[Dictionary] = []
var _checkpoint_tick := 0
var _checkpoint_state := ""
var _applied_bundles: Array[Dictionary] = []
var _desync_report_path := ""
var _desync_tick := -1
var _desync_local_hash := ""
var _send_delay_ms := 0
var _delayed_packets: Array[Dictionary] = []
var _last_error := ""


func configure(
	sim_client,
	match_launch: Dictionary,
	bundle_identity: String,
	map_identity: String,
	input_delay_ticks: int = 3,
	hash_interval_ticks: int = 30,
	desync_report_path: String = ""
) -> bool:
	if sim_client == null or match_launch.is_empty():
		return _fail("lockstep requires a launched SimHostClient and match-launch document")
	if not _sha256(bundle_identity) or map_identity.is_empty():
		return _fail("lockstep requires bundle and map identities")
	if input_delay_ticks < 1 or hash_interval_ticks < 1:
		return _fail("lockstep input delay and hash interval must be positive")
	_client = sim_client
	_client.set_startup_timeout(120_000)
	_match = match_launch.duplicate(true)
	_launch_identity = JSON.stringify(_match).sha256_text()
	_bundle_identity = bundle_identity.to_lower()
	_map_identity = map_identity.to_lower()
	input_delay = input_delay_ticks
	hash_interval = hash_interval_ticks
	_desync_report_path = desync_report_path
	var players := _match.get("players", []) as Array
	_seat_count = clampi(players.size(), 1, MAX_SEATS)
	var saved: Dictionary = _client.save()
	if saved.is_empty():
		return _fail("lockstep could not read initial host state")
	current_tick = int(saved.get("tick", 0))
	_checkpoint_tick = current_tick
	_checkpoint_state = String(saved.get("state", ""))
	return true


func host(port: int = 7777) -> Error:
	if _client == null:
		_fail("configure must precede host")
		return ERR_UNCONFIGURED
	_peer = ENetMultiplayerPeer.new()
	var result: Error = _peer.create_server(port, MAX_SEATS - 1, 3)
	if result != OK:
		_fail("ENet listen failed on port %d (error %d)" % [port, result])
		return result
	_is_host = true
	local_seat = 0
	_peer_seats[HOST_PEER_ID] = 0
	_seat_peers[0] = HOST_PEER_ID
	if _seat_count == 1:
		_activate()
	status_changed.emit("Listening on UDP %d" % port)
	return OK


func join(address: String, port: int = 7777, requested_seat: int = -1) -> Error:
	if _client == null:
		_fail("configure must precede join")
		return ERR_UNCONFIGURED
	_peer = ENetMultiplayerPeer.new()
	var result: Error = _peer.create_client(address, port, 3)
	if result != OK:
		_fail("ENet connect failed for %s:%d (error %d)" % [address, port, result])
		return result
	_is_host = false
	local_seat = requested_seat
	status_changed.emit("Connecting to %s:%d" % [address, port])
	return OK


func poll() -> void:
	if _peer == null:
		return
	if not _is_host and _peer.get_connection_status() == MultiplayerPeer.CONNECTION_DISCONNECTED:
		if handshake_complete:
			handshake_complete = false
			disconnected.emit(local_seat)
			status_changed.emit("Disconnected from lockstep host")
		return
	_peer.poll()
	_flush_delayed_packets()
	if not _is_host and not _handshake_sent \
		and _peer.get_connection_status() == MultiplayerPeer.CONNECTION_CONNECTED:
		_handshake_sent = true
		_send_to(HOST_PEER_ID, _handshake_message(local_seat), CONTROL_CHANNEL)
	while _peer.get_available_packet_count() > 0:
		var sender := _peer.get_packet_peer()
		var bytes := _peer.get_packet()
		var line := bytes.get_string_from_utf8()
		if not line.ends_with("\n"):
			_reject("packet is not newline terminated")
			continue
		var parsed: Variant = JSON.parse_string(line.trim_suffix("\n"))
		if not (parsed is Dictionary):
			_reject("packet is not a JSON object")
			continue
		_receive(sender, parsed as Dictionary)
	_complete_rejoin_if_ready()


func local_commands(bundle: Dictionary) -> bool:
	if paused or bundle.is_empty() or not (bundle.get("commands") is Array):
		return false
	for command in bundle.get("commands", []) as Array:
		if not (command is Dictionary):
			return false
		_pending_commands.append((command as Dictionary).duplicate(true))
	return true


func tick_ready() -> bool:
	poll()
	if not handshake_complete or paused:
		return false
	var rows := _bundles.get(current_tick + 1, {}) as Dictionary
	return rows.size() == _seat_count


func step() -> Dictionary:
	poll()
	if not handshake_complete or paused:
		stalled_ticks += 1
		return {}
	_finalize_local_bundle(current_tick + input_delay)
	poll()
	var tick := current_tick + 1
	if not _step_in_flight:
		if not tick_ready():
			stalled_ticks += 1
			return {}
		var rows := _ordered_bundles(tick)
		for bundle in rows:
			if (bundle.get("commands", []) as Array).is_empty():
				continue
			if not _client.send_commands(bundle):
				_fail("native host command failed: %s" % _client.last_error())
				paused = true
				return {}
			_applied_bundles.append(bundle.duplicate(true))
		_step_in_flight = true
	var snapshots: Array[Dictionary] = _client.step(1, "packed")
	if snapshots.size() != 1:
		if _client.has_pending_step():
			stalled_ticks += 1
			return {}
		_fail("native host step failed: %s" % _client.last_error())
		paused = true
		return {}
	_step_in_flight = false
	_last_snapshot = snapshots[0]
	current_tick = int(_last_snapshot.get("tick", tick))
	_bundles.erase(tick)
	if current_tick % hash_interval == 0:
		_publish_hash()
	return _last_snapshot


func inject_send_delay(milliseconds: int) -> void:
	_send_delay_ms = maxi(0, milliseconds)


func last_error() -> String:
	return _last_error


func shutdown() -> void:
	if _rejoin_thread != null:
		_rejoin_thread.wait_to_finish()
		_rejoin_thread = null
	if _peer != null:
		_peer.close()
	_peer = null
	_delayed_packets.clear()
	handshake_complete = false


func _handshake_message(requested_seat: int) -> Dictionary:
	return {
		"schema": WIRE_SCHEMA,
		"kind": "handshake",
		"launch_sha256": _launch_identity,
		"match_launch": _match.duplicate(true),
		"effective_tree_sha256": _bundle_identity,
		"map_identity": _map_identity,
		"input_delay": input_delay,
		"requested_seat": requested_seat,
	}


func _receive(sender: int, message: Dictionary) -> void:
	if String(message.get("schema", "")) != WIRE_SCHEMA:
		_reject("wrong wire schema")
		return
	match String(message.get("kind", "")):
		"handshake": _receive_handshake(sender, message)
		"welcome": _receive_welcome(sender, message)
		"start": _receive_start(sender, message)
		"bundle": _receive_bundle(sender, message)
		"hash": _receive_hash(sender, message)
		"desync_request": _receive_desync_request(sender, message)
		"desync_state": _receive_desync_state(sender, message)
		"rejoin": _receive_rejoin(sender, message)
		_: _reject("unknown message kind")


func _receive_handshake(sender: int, message: Dictionary) -> void:
	if not _is_host or not _identity_matches(message):
		_reject("handshake identity mismatch")
		return
	var requested := int(message.get("requested_seat", -1))
	var reconnect := requested > 0 and requested < _seat_count and _seat_peers.has(requested)
	var seat := requested if reconnect else _first_open_seat()
	if seat < 1:
		_reject("no open player seat")
		return
	if reconnect:
		var previous_peer := int(_seat_peers[seat])
		_peer_seats.erase(previous_peer)
	else:
		seat = _first_open_seat()
	_peer_seats[sender] = seat
	_seat_peers[seat] = sender
	_send_to(sender, {
		"schema": WIRE_SCHEMA, "kind": "welcome", "seat": seat,
		"seat_count": _seat_count, "input_delay": input_delay,
		"reconnect": reconnect, "tick": current_tick,
	}, CONTROL_CHANNEL)
	if reconnect:
		_send_rejoin(sender)
	else:
		connected.emit(seat)
	if _seat_peers.size() == _seat_count:
		_activate()
		_broadcast({"schema": WIRE_SCHEMA, "kind": "start", "tick": current_tick}, CONTROL_CHANNEL)


func _receive_welcome(sender: int, message: Dictionary) -> void:
	if _is_host or sender != HOST_PEER_ID:
		_reject("welcome did not come from host")
		return
	local_seat = int(message.get("seat", -1))
	_seat_count = int(message.get("seat_count", 0))
	if local_seat < 1 or local_seat >= _seat_count or int(message.get("input_delay", 0)) != input_delay:
		_reject("welcome seat or input delay is invalid")
		return
	connected.emit(local_seat)


func _receive_start(sender: int, message: Dictionary) -> void:
	# Reconnect activation is completed by the asynchronous sidecar restore. The
	# host's ordered start marker may arrive while that worker is still running.
	if not _is_host and sender == HOST_PEER_ID and _rejoin_thread != null:
		return
	if _is_host or sender != HOST_PEER_ID or int(message.get("tick", -1)) != current_tick:
		_reject("start message is invalid")
		return
	_activate()


func _activate() -> void:
	handshake_complete = true
	_prime()
	status_changed.emit("Lockstep ready as seat %d" % local_seat)


func _prime() -> void:
	if _primed:
		return
	_primed = true
	for tick in range(current_tick + 1, current_tick + input_delay):
		_finalize_local_bundle(tick)


func _finalize_local_bundle(tick: int) -> void:
	var rows := _bundles.get(tick, {}) as Dictionary
	if rows.has(local_seat):
		return
	var bundle := {
		"schema": "openbfme.command.v1", "tick": tick,
		"seat": local_seat, "seq": _next_seq,
		"commands": _pending_commands.duplicate(true),
	}
	_pending_commands.clear()
	_next_seq += 1
	_store_bundle(bundle)
	var message := {"schema": WIRE_SCHEMA, "kind": "bundle", "bundle": bundle}
	if _is_host:
		_broadcast(message, BUNDLE_CHANNEL)
	else:
		_send_to(HOST_PEER_ID, message, BUNDLE_CHANNEL)


func _receive_bundle(sender: int, message: Dictionary) -> void:
	# ENet orders packets within each channel, not across channels. A host bundle
	# may therefore beat the control-channel start packet to a welcomed guest.
	var welcomed_guest := not _is_host and _handshake_sent and sender == HOST_PEER_ID
	if (not handshake_complete and not welcomed_guest) or not (message.get("bundle") is Dictionary):
		_reject("bundle arrived before welcome or has no document")
		return
	var bundle := _normalize_bundle(message["bundle"] as Dictionary)
	var seat := int(bundle.get("seat", -1))
	if not _sender_owns_seat(sender, seat) or not _valid_bundle(bundle):
		_reject("bundle sender or document is invalid")
		return
	if not _store_bundle(bundle):
		return
	if _is_host:
		_broadcast({"schema": WIRE_SCHEMA, "kind": "bundle", "bundle": bundle}, BUNDLE_CHANNEL, sender)


func _normalize_bundle(source: Dictionary) -> Dictionary:
	var bundle := source.duplicate(true)
	for field in ["tick", "seat", "seq"]:
		bundle[field] = int(bundle.get(field, -1))
	var commands := bundle.get("commands", []) as Array
	for command_value in commands:
		if not (command_value is Dictionary):
			continue
		var args := (command_value as Dictionary).get("args", {}) as Dictionary
		for field in ["object", "target", "index", "count"]:
			if args.has(field):
				args[field] = int(args[field])
		if args.get("objects") is Array:
			var object_ids := args.get("objects", []) as Array
			for index in object_ids.size():
				object_ids[index] = int(object_ids[index])
	return bundle


func _valid_bundle(bundle: Dictionary) -> bool:
	return bundle.size() == 5 \
		and String(bundle.get("schema", "")) == "openbfme.command.v1" \
		and int(bundle.get("tick", -1)) > current_tick \
		and int(bundle.get("seat", -1)) >= 0 \
		and int(bundle.get("seat", -1)) < _seat_count \
		and int(bundle.get("seq", -1)) >= 0 \
		and bundle.get("commands") is Array


func _store_bundle(bundle: Dictionary) -> bool:
	var tick := int(bundle.get("tick", -1))
	var seat := int(bundle.get("seat", -1))
	var rows := _bundles.get(tick, {}) as Dictionary
	if rows.has(seat):
		if JSON.stringify(rows[seat]) != JSON.stringify(bundle):
			_reject("conflicting duplicate bundle")
		return false
	rows[seat] = bundle.duplicate(true)
	_bundles[tick] = rows
	return true


func _ordered_bundles(tick: int) -> Array[Dictionary]:
	var result: Array[Dictionary] = []
	var rows := _bundles.get(tick, {}) as Dictionary
	for value in rows.values():
		result.append((value as Dictionary).duplicate(true))
	result.sort_custom(func(a: Dictionary, b: Dictionary) -> bool:
		var team_a := _team_for_seat(int(a.get("seat", -1)))
		var team_b := _team_for_seat(int(b.get("seat", -1)))
		if team_a != team_b: return team_a < team_b
		var seq_a := int(a.get("seq", -1))
		var seq_b := int(b.get("seq", -1))
		return seq_a < seq_b if seq_a != seq_b else int(a.get("seat", -1)) < int(b.get("seat", -1))
	)
	return result


func _team_for_seat(seat: int) -> int:
	for value in _match.get("players", []) as Array:
		var player := value as Dictionary
		if int(player.get("seat", -1)) == seat:
			return int(player.get("team", seat))
	return seat


func _publish_hash() -> void:
	var digest: String = _client.hash()
	if digest.is_empty():
		_fail("native host hash failed: %s" % _client.last_error())
		paused = true
		return
	_store_hash(current_tick, local_seat, digest)
	var message := {
		"schema": WIRE_SCHEMA, "kind": "hash", "tick": current_tick,
		"seat": local_seat, "hash": digest,
	}
	if _is_host: _broadcast(message, HASH_CHANNEL)
	else: _send_to(HOST_PEER_ID, message, HASH_CHANNEL)


func _receive_hash(sender: int, message: Dictionary) -> void:
	var seat := int(message.get("seat", -1))
	var tick := int(message.get("tick", -1))
	var digest := String(message.get("hash", ""))
	if not _sender_owns_seat(sender, seat) or tick < 0 or not _sha256(digest):
		_reject("hash message is invalid")
		return
	_store_hash(tick, seat, digest)
	if _is_host:
		_broadcast(message, HASH_CHANNEL, sender)


func _store_hash(tick: int, seat: int, digest: String) -> void:
	var rows := _hashes.get(tick, {}) as Dictionary
	rows[seat] = digest
	_hashes[tick] = rows
	if rows.size() != _seat_count:
		return
	hash_checks += 1
	var values := rows.values()
	for value in values:
		if String(value) != String(values[0]):
			_begin_desync(tick, String(rows.get(local_seat, "")), String(value))
			return
	_hashes.erase(tick)


func _begin_desync(tick: int, local_hash: String, remote_hash: String) -> void:
	if _desync_tick >= 0:
		return
	_desync_tick = tick
	_desync_local_hash = local_hash
	paused = true
	var saved: Dictionary = _client.save()
	if _is_host:
		_broadcast({
			"schema": WIRE_SCHEMA, "kind": "desync_request", "tick": tick,
			"hash": local_hash,
		}, CONTROL_CHANNEL)
	else:
		_send_to(HOST_PEER_ID, {
			"schema": WIRE_SCHEMA, "kind": "desync_state", "tick": tick,
			"seat": local_seat, "hash": local_hash, "state": String(saved.get("state", "")),
		}, CONTROL_CHANNEL)
	status_changed.emit("DESYNC at tick %d - match paused" % tick)
	desync_detected.emit(tick, local_hash, remote_hash, "")


func _receive_desync_request(sender: int, message: Dictionary) -> void:
	if _is_host or sender != HOST_PEER_ID:
		_reject("desync request did not come from host")
		return
	var saved: Dictionary = _client.save()
	paused = true
	_send_to(HOST_PEER_ID, {
		"schema": WIRE_SCHEMA, "kind": "desync_state", "tick": int(message.get("tick", -1)),
		"seat": local_seat, "hash": _client.hash(), "state": String(saved.get("state", "")),
	}, CONTROL_CHANNEL)
	status_changed.emit("DESYNC at tick %d - match paused" % int(message.get("tick", -1)))


func _receive_desync_state(sender: int, message: Dictionary) -> void:
	if not _is_host or not _sender_owns_seat(sender, int(message.get("seat", -1))):
		_reject("desync state sender is invalid")
		return
	var path := _desync_report_path
	if path.is_empty():
		path = ProjectSettings.globalize_path("user://native-lockstep-desync-%d.json" % int(message.get("tick", current_tick)))
	var report: Dictionary = _client.diff(String(message.get("state", "")), path)
	desync_detected.emit(
		int(message.get("tick", current_tick)), _desync_local_hash,
		String(message.get("hash", "")), path if not report.is_empty() else ""
	)


func _send_rejoin(peer_id: int) -> void:
	var pending: Array = _applied_bundles.duplicate(true)
	var through := current_tick
	for tick_value in _bundles.keys():
		var tick := int(tick_value)
		if tick <= current_tick:
			continue
		through = maxi(through, tick)
		for bundle in _ordered_bundles(tick):
			pending.append(bundle)
	_send_to(peer_id, {
		"schema": WIRE_SCHEMA, "kind": "rejoin", "tick": _checkpoint_tick,
		"target_tick": current_tick, "state": _checkpoint_state, "bundles": pending,
		"pending_through": through,
	}, CONTROL_CHANNEL)


func _receive_rejoin(sender: int, message: Dictionary) -> void:
	if _is_host or sender != HOST_PEER_ID or not (message.get("bundles") is Array):
		_reject("rejoin message is invalid")
		return
	if _rejoin_thread != null:
		_reject("rejoin restore is already running")
		return
	var target_tick := int(message.get("target_tick", -1))
	var catchup: Array = []
	_rejoin_future_bundles.clear()
	for value in message.get("bundles", []) as Array:
		if not (value is Dictionary):
			continue
		var bundle := _normalize_bundle(value as Dictionary)
		if int(bundle.get("tick", -1)) <= target_tick:
			if not (bundle.get("commands", []) as Array).is_empty():
				catchup.append(bundle)
		else:
			_rejoin_future_bundles.append(bundle)
	var checkpoint_state := String(message.get("state", ""))
	var checkpoint_tick := int(message.get("tick", -1))
	paused = true
	handshake_complete = false
	_rejoin_thread = Thread.new()
	var started := _rejoin_thread.start(func() -> Dictionary:
		# A restarted peer has already reconstructed the exact launched checkpoint.
		# Keep its live derived module caches after the sidecar verifies the
		# checkpoint hash, then replay the authoritative command history onto it.
		return _client.join(checkpoint_state, checkpoint_tick, catchup, target_tick, true)
	)
	if started != OK:
		_rejoin_thread = null
		_fail("native host rejoin worker could not start")
		return
	status_changed.emit("Restoring lockstep state through tick %d" % target_tick)


func _complete_rejoin_if_ready() -> void:
	if _rejoin_thread == null or _rejoin_thread.is_alive():
		return
	var joined_value: Variant = _rejoin_thread.wait_to_finish()
	_rejoin_thread = null
	var joined: Dictionary = joined_value as Dictionary if joined_value is Dictionary else {}
	if joined.is_empty():
		_fail("native host rejoin failed: %s" % _client.last_error())
		paused = true
		return
	current_tick = int(joined.get("tick", -1))
	_bundles.clear()
	for bundle in _rejoin_future_bundles:
		_store_bundle(bundle)
	_rejoin_future_bundles.clear()
	_primed = false
	paused = false
	_activate()
	status_changed.emit("Rejoined at tick %d" % current_tick)


func _identity_matches(message: Dictionary) -> bool:
	return String(message.get("launch_sha256", "")) == _launch_identity \
		and String(message.get("effective_tree_sha256", "")) == _bundle_identity \
		and String(message.get("map_identity", "")) == _map_identity \
		and int(message.get("input_delay", -1)) == input_delay \
		and message.get("match_launch") is Dictionary \
		and JSON.stringify(message.get("match_launch")) == JSON.stringify(_match)


func _sender_owns_seat(sender: int, seat: int) -> bool:
	if _is_host:
		return int(_peer_seats.get(sender, -1)) == seat
	return sender == HOST_PEER_ID and seat != local_seat and seat >= 0 and seat < _seat_count


func _first_open_seat() -> int:
	for seat in range(1, _seat_count):
		if not _seat_peers.has(seat):
			return seat
	return -1


func _broadcast(message: Dictionary, channel: int, except_peer: int = 0) -> void:
	for peer_value in _peer_seats.keys():
		var peer_id := int(peer_value)
		if peer_id != HOST_PEER_ID and peer_id != except_peer:
			_send_to(peer_id, message, channel)


func _send_to(peer_id: int, message: Dictionary, channel: int) -> void:
	var payload := (JSON.stringify(message) + "\n").to_utf8_buffer()
	if _send_delay_ms > 0:
		_delayed_packets.append({
			"due": Time.get_ticks_msec() + _send_delay_ms,
			"peer": peer_id, "channel": channel, "payload": payload,
		})
		return
	_put_packet(peer_id, channel, payload)


func _flush_delayed_packets() -> void:
	var now := Time.get_ticks_msec()
	for index in range(_delayed_packets.size() - 1, -1, -1):
		var row := _delayed_packets[index]
		if int(row.get("due", 0)) > now:
			continue
		_put_packet(int(row["peer"]), int(row["channel"]), row["payload"] as PackedByteArray)
		_delayed_packets.remove_at(index)


func _put_packet(peer_id: int, channel: int, payload: PackedByteArray) -> void:
	if _peer == null:
		return
	_peer.set_target_peer(peer_id)
	_peer.transfer_channel = channel
	_peer.transfer_mode = MultiplayerPeer.TRANSFER_MODE_RELIABLE
	var result := _peer.put_packet(payload)
	if result != OK:
		_fail("ENet packet send failed with error %d" % result)


func _sha256(value: String) -> bool:
	if value.length() != 64:
		return false
	for character in value:
		if not "0123456789abcdef".contains(character.to_lower()):
			return false
	return true


func _reject(reason: String) -> void:
	rejected_packets += 1
	push_warning("[NativeLockstepSession] rejected packet: %s" % reason)


func _fail(reason: String) -> bool:
	_last_error = reason
	push_error("[NativeLockstepSession] %s" % reason)
	return false
