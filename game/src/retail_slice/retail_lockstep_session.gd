class_name RetailLockstepSession
extends RefCounted

const CommandScript = preload("res://src/retail_slice/retail_command.gd")
const PROTOCOL_VERSION := 1
const MAX_PACKET_BYTES := 256 * 1024
const MAX_COMMANDS_PER_BUNDLE := 256
const HASH_INTERVAL_TICKS := 30

## --- Pre-game lobby protocol constants --------------------------------------
## The lobby runs on this same reliable-ordered transport before a sim exists
## (sim == null). Every lobby envelope is validated fail-closed; settings and
## launch are HOST-authoritative (a host never accepts them from the guest).
## The value ladders mirror the menu's GAME SETUP constants so both peers can
## only ever agree on states the setup screen itself can express.
const LOBBY_NAME_MAX := 24
const LOBBY_CHAT_MAX := 200
const LOBBY_CHAT_LOG_MAX := 64
const LOBBY_FACTION_IDS: Array[String] = ["men", "elves", "dwarves", "isengard", "mordor", "wild"]
const LOBBY_MAP_IDS: Array[String] = [
	"bfme2.map.fords-of-isen-ii",
	"bfme2.map.rivendell",
	"bfme2.map.mount-doom",
	"bfme2.map.dagorlad",
	"bfme2.map.mordor",
]
const LOBBY_RESOURCE_VALUES: Array[int] = [500, 1000, 1200, 2000, 5000, 10000, 50000]
const LOBBY_CP_FACTORS: Array[float] = [0.5, 1.0, 2.0, 4.0]
## BFME2 1.06 house-color rows, index-aligned with the menu's HOUSE_COLORS.
## The lobby transports a color INDEX; both peers map it through this shared
## palette so the launch descriptors are byte-identical.
const LOBBY_COLOR_NAMES: Array[String] = ["Blue", "Red", "Green", "Yellow", "Orange", "Purple", "Teal", "Pink"]
const LOBBY_HOUSE_COLORS: Array[Color] = [
	Color8(45, 77, 172), Color8(166, 32, 28), Color8(46, 125, 50), Color8(214, 198, 46),
	Color8(217, 124, 30), Color8(124, 63, 160), Color8(46, 158, 155), Color8(214, 107, 168),
]

signal lobby_updated
signal lobby_chat_received(team: int, text: String)
signal lobby_launch_accepted(roster: Array)

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

## Lobby state (pre-game phase, sim == null). Profiles are canonical dicts
## {name, faction, color, ready}; settings is the host-authoritative canonical
## dict {map_id, resources, cp_factor, build_plots}.
var lobby_local_profile: Dictionary = {}
var lobby_remote_profile: Dictionary = {}
var lobby_settings: Dictionary = {}
var lobby_chat_log: Array = []
var lobby_launch_roster: Array = []
var lobby_launch_settings: Dictionary = {}
var lobby_launch_received := false


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


## Graceful teardown (two-phase, poll-driven). A hard close() right after a
## send provably drops the flushed-but-undispatched reliable envelope on the
## receiving side: ENet resets a peer's queues when it processes the
## disconnect command, so a peer that services the data and the disconnect in
## the same call loses the data (this stranded the guest's lobby.launch echo).
## begin_graceful_close() requests a deferred disconnect (delivered only after
## every queued reliable packet is acknowledged); the caller keeps calling
## poll_graceful_close() across frames until it returns true, then calls
## close(). The frame-async shape lets an in-process peer poll its own side
## concurrently — a blocking drain cannot.
var _graceful_close_requested := false
var _graceful_close_done := false


func begin_graceful_close() -> void:
	if _connection == null or _peer == null or not _peer.is_active():
		_graceful_close_done = true
		return
	_graceful_close_requested = true
	_peer.peer_disconnect_later(0)


func poll_graceful_close() -> bool:
	## True once ENet confirmed the deferred disconnect (all queued reliable
	## packets delivered) or there is nothing left to drain.
	if _graceful_close_done or not _graceful_close_requested or _connection == null:
		return true
	for _index in range(64):
		var event := _connection.service(0)
		if event.is_empty() or int(event[0]) == ENetConnection.EVENT_NONE:
			return false
		if int(event[0]) == ENetConnection.EVENT_DISCONNECT:
			_graceful_close_done = true
			return true
	return false


func close() -> void:
	if _peer != null and _peer.is_active():
		# Immediate but NOTIFIED disconnect: the remote peer receives the ENet
		# disconnect event right away (a lobby LEAVE must be visible to the
		# other side), unlike reset() which drops silently into a timeout.
		_peer.peer_disconnect_now(0)
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
	_graceful_close_requested = false
	_graceful_close_done = false
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
	lobby_local_profile = {}
	lobby_remote_profile = {}
	lobby_settings = {}
	lobby_chat_log = []
	lobby_launch_roster = []
	lobby_launch_settings = {}
	lobby_launch_received = false


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
	if handshake_complete and sim != null:
		_send_ready_bundles()
		_maybe_send_hash()
	_connection.flush()
	_service_events()
	if handshake_complete and sim != null:
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
		"lobby.profile":
			_receive_lobby_profile(envelope)
		"lobby.chat":
			_receive_lobby_chat(envelope)
		"lobby.settings":
			_receive_lobby_settings(envelope)
		"lobby.launch":
			_receive_lobby_launch(envelope)
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
	# Both peers seed identical default lobby settings at handshake so a host
	# launch without an explicit settings change still verifies byte-equal.
	if lobby_settings.is_empty():
		lobby_settings = lobby_default_settings()


func _receive_bundle(envelope: Dictionary) -> void:
	# Command bundles are meaningless before a simulation is attached (lobby
	# phase); fail closed instead of dereferencing a null sim.
	if sim == null:
		rejected_packets += 1
		return
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


## --- Pre-game lobby protocol -------------------------------------------------


func is_host() -> bool:
	return _is_host


static func lobby_default_settings() -> Dictionary:
	## Canonical key order — this dict participates in byte-equality checks.
	return {
		"map_id": "bfme2.map.fords-of-isen-ii",
		"resources": 1200,
		"cp_factor": 1.0,
		"build_plots": false,
	}


static func lobby_name_valid(player_name: String) -> bool:
	## Printable ASCII, 1..LOBBY_NAME_MAX chars, no leading/trailing whitespace.
	if player_name.is_empty() or player_name.length() > LOBBY_NAME_MAX:
		return false
	if player_name != player_name.strip_edges():
		return false
	for index in player_name.length():
		var code := player_name.unicode_at(index)
		if code < 32 or code > 126:
			return false
	return true


static func lobby_chat_valid(text: String) -> bool:
	if text.is_empty() or text.length() > LOBBY_CHAT_MAX:
		return false
	for index in text.length():
		var code := text.unicode_at(index)
		if code < 32 or code > 126:
			return false
	return true


static func _lobby_profile_fields_valid(player_name: String, faction: String, color: int) -> bool:
	return lobby_name_valid(player_name) \
		and LOBBY_FACTION_IDS.has(faction) \
		and color >= 0 and color < LOBBY_HOUSE_COLORS.size()


static func _lobby_settings_fields_valid(map_id: String, resource_amount: int, cp_factor: float, build_plots: bool) -> bool:
	var _unused := build_plots
	return LOBBY_MAP_IDS.has(map_id) \
		and LOBBY_RESOURCE_VALUES.has(resource_amount) \
		and LOBBY_CP_FACTORS.has(cp_factor)


static func _canonical_profile(player_name: String, faction: String, color: int, ready: bool) -> Dictionary:
	## Canonical key order — profiles feed the byte-equal launch roster.
	return {"name": player_name, "faction": faction, "color": color, "ready": ready}


static func lobby_roster_from_profiles(host_profile: Dictionary, guest_profile: Dictionary) -> Array:
	## The launch descriptor list, in the exact retail_team_setup shape the menu's
	## GAME SETUP writes ({team, faction, controller, difficulty, alliance, color,
	## start_index}). Construction order and every default are fixed here so both
	## peers derive byte-identical lists from the same pair of profiles — the
	## roster feeds the deterministic sim seed. Host is always team 0, guest team
	## 1; alliances 1 vs 2 (mutually hostile); start_index 0 keeps the authored
	## start assignment on both machines.
	var descriptors: Array = []
	var profiles := [host_profile, guest_profile]
	for team in profiles.size():
		var profile := profiles[team] as Dictionary
		descriptors.append({
			"team": team,
			"faction": String(profile.get("faction", "men")),
			"controller": "human",
			"difficulty": "medium",
			"alliance": team + 1,
			"color": LOBBY_HOUSE_COLORS[clampi(int(profile.get("color", 0)), 0, LOBBY_HOUSE_COLORS.size() - 1)],
			"start_index": 0,
		})
	return descriptors


func build_lobby_roster() -> Array:
	## Local view of the agreed roster; [] until both profiles are known.
	if lobby_local_profile.is_empty() or lobby_remote_profile.is_empty():
		return []
	var host_profile := lobby_local_profile if _is_host else lobby_remote_profile
	var guest_profile := lobby_remote_profile if _is_host else lobby_local_profile
	return lobby_roster_from_profiles(host_profile, guest_profile)


func send_lobby_profile(player_name: String, faction: String, color: int, ready: bool) -> bool:
	if not handshake_complete or not _lobby_profile_fields_valid(player_name, faction, color):
		return false
	lobby_local_profile = _canonical_profile(player_name, faction, color, ready)
	_send_envelope({
		"kind": "lobby.profile",
		"name": player_name,
		"faction": faction,
		"color": color,
		"ready": ready,
	})
	_connection.flush()
	return true


func send_lobby_chat(text: String) -> bool:
	if not handshake_complete or not lobby_chat_valid(text):
		return false
	_append_lobby_chat(local_team, text)
	_send_envelope({"kind": "lobby.chat", "text": text})
	_connection.flush()
	return true


func send_lobby_settings(map_id: String, resource_amount: int, cp_factor: float, build_plots: bool) -> bool:
	## HOST-ONLY authoritative. A guest calling this is a no-op fail-closed.
	if not _is_host or not handshake_complete \
		or not _lobby_settings_fields_valid(map_id, resource_amount, cp_factor, build_plots):
		return false
	lobby_settings = {
		"map_id": map_id,
		"resources": resource_amount,
		"cp_factor": cp_factor,
		"build_plots": build_plots,
	}
	_send_envelope({
		"kind": "lobby.settings",
		"map_id": map_id,
		"resources": resource_amount,
		"cp_factor": cp_factor,
		"build_plots": build_plots,
	})
	_connection.flush()
	return true


func send_lobby_launch() -> bool:
	## HOST-ONLY. Requires both sides ready; echoes the full agreed roster and
	## settings so the guest can verify byte-equality before accepting. The host
	## treats its own send as the accepted launch (same roster object).
	if not _is_host or not handshake_complete:
		return false
	if not bool(lobby_local_profile.get("ready", false)) or not bool(lobby_remote_profile.get("ready", false)):
		return false
	var roster := build_lobby_roster()
	if roster.size() != 2 or lobby_settings.is_empty():
		return false
	_send_envelope({
		"kind": "lobby.launch",
		"roster": roster,
		"settings": lobby_settings,
	})
	_connection.flush()
	lobby_launch_roster = roster.duplicate(true)
	lobby_launch_settings = lobby_settings.duplicate(true)
	lobby_launch_received = true
	lobby_launch_accepted.emit(lobby_launch_roster.duplicate(true))
	return true


func _append_lobby_chat(team: int, text: String) -> void:
	lobby_chat_log.append({"team": team, "text": text})
	while lobby_chat_log.size() > LOBBY_CHAT_LOG_MAX:
		lobby_chat_log.pop_front()


func _receive_lobby_profile(envelope: Dictionary) -> void:
	if not handshake_complete or envelope.size() != 5 \
		or typeof(envelope.get("name")) != TYPE_STRING \
		or typeof(envelope.get("faction")) != TYPE_STRING \
		or typeof(envelope.get("color")) != TYPE_INT \
		or typeof(envelope.get("ready")) != TYPE_BOOL \
		or not _lobby_profile_fields_valid(String(envelope["name"]), String(envelope["faction"]), int(envelope["color"])):
		rejected_packets += 1
		return
	lobby_remote_profile = _canonical_profile(
		String(envelope["name"]),
		String(envelope["faction"]),
		int(envelope["color"]),
		bool(envelope["ready"])
	)
	lobby_updated.emit()


func _receive_lobby_chat(envelope: Dictionary) -> void:
	if not handshake_complete or envelope.size() != 2 \
		or typeof(envelope.get("text")) != TYPE_STRING \
		or not lobby_chat_valid(String(envelope["text"])):
		rejected_packets += 1
		return
	_append_lobby_chat(peer_team, String(envelope["text"]))
	lobby_chat_received.emit(peer_team, String(envelope["text"]))
	lobby_updated.emit()


func _receive_lobby_settings(envelope: Dictionary) -> void:
	## Settings are host-authoritative: the host never accepts them from a guest.
	if _is_host or not handshake_complete or envelope.size() != 5 \
		or typeof(envelope.get("map_id")) != TYPE_STRING \
		or typeof(envelope.get("resources")) != TYPE_INT \
		or typeof(envelope.get("cp_factor")) != TYPE_FLOAT \
		or typeof(envelope.get("build_plots")) != TYPE_BOOL \
		or not _lobby_settings_fields_valid(
			String(envelope["map_id"]), int(envelope["resources"]),
			float(envelope["cp_factor"]), bool(envelope["build_plots"])):
		rejected_packets += 1
		return
	lobby_settings = {
		"map_id": String(envelope["map_id"]),
		"resources": int(envelope["resources"]),
		"cp_factor": float(envelope["cp_factor"]),
		"build_plots": bool(envelope["build_plots"]),
	}
	lobby_updated.emit()


func _receive_lobby_launch(envelope: Dictionary) -> void:
	## Guest-side final gate. The echoed roster and settings must byte-match the
	## guest's own derived view (reliable-ordered delivery guarantees every prior
	## profile/settings update arrived first), and both sides must be ready.
	if _is_host or not handshake_complete or envelope.size() != 3 \
		or typeof(envelope.get("roster")) != TYPE_ARRAY \
		or typeof(envelope.get("settings")) != TYPE_DICTIONARY:
		rejected_packets += 1
		return
	if not bool(lobby_local_profile.get("ready", false)) or not bool(lobby_remote_profile.get("ready", false)):
		rejected_packets += 1
		return
	var expected_roster := build_lobby_roster()
	if expected_roster.size() != 2 or lobby_settings.is_empty():
		rejected_packets += 1
		return
	var roster := envelope["roster"] as Array
	var settings := envelope["settings"] as Dictionary
	if var_to_bytes(roster) != var_to_bytes(expected_roster) \
		or var_to_bytes(settings) != var_to_bytes(lobby_settings):
		rejected_packets += 1
		return
	lobby_launch_roster = expected_roster.duplicate(true)
	lobby_launch_settings = lobby_settings.duplicate(true)
	lobby_launch_received = true
	lobby_launch_accepted.emit(lobby_launch_roster.duplicate(true))
