class_name RetailLockstepSession
extends RefCounted

## Deterministic lockstep session for up to MAX_SEATS players (F3).
##
## TOPOLOGY. Star, host-relayed. The host binds one ENet host that accepts up to
## MAX_GUESTS peers; guests connect only to the host. A guest's tick bundle
## therefore travels guest -> host -> every other guest. A full mesh was
## rejected: it needs N*(N-1)/2 NAT traversals for a product whose whole
## internet story is "one host forwards one port" (see upnp_port_mapping.gd),
## and it gives no ordering benefit, because ordering is not derived from
## arrival (see below).
##
## SEATS AND TEAMS. A SEAT is the network identity (0..MAX_SEATS-1; the host is
## always seat 0). A TEAM is the simulation's player id. They are NOT the same
## number: RetailSliceSim reserves team 2 for neutral/capturable owners, so
## seats map through SEAT_TEAM_IDS, the same ladder main_menu.gd calls
## TEAM_ID_POOL. The mapping is fixed, total, and strictly increasing.
##
## CANONICAL COMMAND ORDER. Every peer must apply every peer's commands for
## tick T in one identical order. That order is:
##
##     (team ascending, then seq ascending)
##
## which RetailSliceSim._apply_pending_commands_for_tick already imposes when it
## drains a tick, so the order is a property of the SIM, not of the network.
## It is total and arrival-order-independent because:
##   * every seat maps to a distinct team, so two commands from different seats
##     always differ in `team` and never need a tie-break;
##   * `seq` is a per-seat monotonically increasing counter, so two commands
##     from the SAME seat always differ in `seq`;
##   * a bundle for tick T is immutable once sent, and a peer cannot advance to
##     T until it holds the bundle from every participating seat, so the set of
##     commands being sorted is identical everywhere.
## Because SEAT_TEAM_IDS is strictly increasing, ordering by team is the same as
## ordering by seat index; the ladder can gain entries but must never be
## reordered.
##
## WIRE SCHEMA. PROTOCOL_VERSION is checked on the very first envelope and a
## mismatch is refused with a stated reason instead of being tolerated. Every
## envelope is validated fail-closed (exact key count, exact types, range
## checks) before it can touch sim or lobby state; anything else increments a
## rejection counter and is dropped without pushing an error, because this runs
## on unauthenticated wire data.
##
## OUT OF SCOPE: late join. Once the host has sent its first tick bundle the
## door is shut, and a peer that arrives afterwards is refused with a reason.

const CommandScript = preload("res://src/retail_slice/retail_command.gd")
const DiagLogScript = preload("res://src/core/diag_log.gd")

## Bumped 1 -> 2 for the N-player handshake (join/welcome replace the old
## symmetric hello) and the relay envelopes. A v1 peer is refused, not guessed at.
const PROTOCOL_VERSION := 2
const MAX_PACKET_BYTES := 256 * 1024
const MAX_COMMANDS_PER_BUNDLE := 256
const HASH_INTERVAL_TICKS := 30
## How many already-executed ticks of command history ordered_commands_for_tick()
## can still answer for. Bounds the memory the record costs; it exists for
## assertions and desync forensics, not for replay.
const COMMAND_HISTORY_TICKS := 64

## Player capacity. MAX_GUESTS is the ENet peer allowance on the host.
const MAX_SEATS := 8
const MAX_GUESTS := MAX_SEATS - 1
const HOST_SEAT := 0
const ENET_CHANNELS := 1
## ENet peer slots the host binds. One MORE than the seats it can fill, so a
## peer arriving at a full or already-started game still gets a connection —
## and therefore a stated reason — instead of being dropped at the transport
## layer and left staring at a timeout. The spare slot is released as soon as
## the refusal is delivered.
const ENET_PEER_ALLOWANCE := MAX_GUESTS + 1

## Seat -> simulation team id. RetailSliceSim.NEUTRAL_TEAM == 2 owns capturable
## and prop entities, so the ladder skips 2; this is index-aligned with
## main_menu.gd TEAM_ID_POOL. MUST stay strictly increasing (see the ordering
## note above) — append only, never reorder.
const SEAT_TEAM_IDS: Array[int] = [0, 1, 3, 4, 5, 6, 7, 8]

## --- Pre-game lobby protocol constants --------------------------------------
## The lobby runs on this same reliable-ordered transport before a sim exists
## (sim == null). Every lobby envelope is validated fail-closed; the seat table,
## settings and launch are HOST-authoritative (a host never accepts them from a
## guest). The value ladders mirror the menu's GAME SETUP constants so peers can
## only ever agree on states the setup screen itself can express.
const LOBBY_NAME_MAX := 24
const LOBBY_CHAT_MAX := 200
const LOBBY_CHAT_LOG_MAX := 64
const LOBBY_FACTION_IDS: Array[String] = ["men", "elves", "dwarves", "isengard", "mordor", "wild", "angmar"]
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
## Alliance ids a host may assign. Default is seat + 1, i.e. free-for-all: every
## seat in its own alliance, which is what a 1v1 already produced (1 vs 2).
const LOBBY_ALLIANCE_MIN := 1
const LOBBY_ALLIANCE_MAX := MAX_SEATS

## --- Created heroes on the wire ---------------------------------------------
## A Create-a-Hero profile is saved under `user://`, so every peer's set is
## DIFFERENT. A lockstep match whose peers each injected their own local heroes
## would build different production rosters and desync on the first purchase, so
## the lobby exchanges them: each seat announces its own, the host is the only
## writer of the table, and the agreed table rides the launch roster where the
## existing byte-equality gate already refuses any disagreement.
##
## CARRIED AS CANONICAL JSON TEXT, not as dictionaries. Godot dictionaries
## preserve insertion order and `var_to_bytes` is order-sensitive, so a
## dictionary that survived a round trip through another peer's parser could
## byte-differ from the same dictionary built locally and fail the launch gate
## for no reason. A JSON document with sorted keys has exactly one spelling, and
## re-canonicalizing on receipt (parse, re-stringify, compare) rejects anything
## that is not already in it — which is also a cheap schema gate on wire data.
##
## THIS FILE KNOWS NOTHING ABOUT CREATE-A-HERO RULES. It bounds and canonicalizes
## bytes. Whether a hero is legal (budget, power prerequisites, the content it
## was built against) is decided by the same `validate_profile` the local screen
## uses, at match boot, on the agreed bytes — identically on every peer.
## THE CAPS ARE SIZED SO THE WHOLE TABLE FITS ONE PACKET. The host rebroadcasts
## every seat's list together, so the binding number is MAX_SEATS x the per-seat
## budget, and that product must stay under MAX_PACKET_BYTES - otherwise a legal
## announcement would be accepted here and then dropped by the transport, which
## is the one failure mode a fail-closed protocol must not have. A saved profile
## is a build order (a class pair and five small integers), so a real one is
## about two kilobytes; these budgets are roomy against that, not tight.
const LOBBY_HEROES_PER_SEAT_MAX := 8
const LOBBY_HERO_BYTES_MAX := 16 * 1024
const LOBBY_HEROES_BYTES_MAX := 24 * 1024

signal lobby_updated
signal lobby_chat_received(team: int, text: String)
signal lobby_launch_accepted(roster: Array)
## A guest was refused (game full, protocol mismatch, match already running).
## Carries the host's stated reason so the UI never has to invent one.
signal join_refused(reason: String)

var input_delay_ticks := 3
var sim: RetailSliceSim
## Network identity of this peer. local_seat is -1 until a guest is welcomed.
var local_seat := -1
var local_team := -1
## Legacy 1v1 mirror: the "primary" remote seat's team (the host for a guest,
## the lowest-numbered occupied guest seat for the host). Kept because the
## 2-player callers and runners read it; N-player code should use
## remote_seats() / lobby_seats instead.
var peer_team := -1
var connected := false
var handshake_complete := false
var desynced := false
var desync_tick := -1
## Seat whose state hash diverged, -1 when none. With more than two players
## this is the actionable half of a desync report.
var desync_seat := -1
var bound_port := 0
var remote_ack_tick := 0
var rejected_packets := 0
var rejected_remote_commands := 0
var pause_command_tick := -1
var pause_command_state := false
## Host's stated reason when this peer's join was refused ("" when it was not).
var join_refused_reason := ""
## Number of seats that must be occupied before ticking may begin. 0 means "no
## explicit requirement, use whoever is connected"; an accepted lobby launch
## sets it from the roster so a peer can never start a match short-handed.
var required_seat_count := 0

var _connection: ENetConnection
## Guest side: the host peer. Host side: the primary guest peer, kept so the
## legacy single-peer helpers (_send_envelope) still mean something.
var _peer: ENetPacketPeer
var _is_host := false
var _next_local_seq := 0
var _join_sent := false
var _local_commands_by_tick: Dictionary = {}
var _bundle_sent_through := 0
## Legacy 1v1 mirror of the per-seat contiguous watermark: the MINIMUM across
## every remote seat, i.e. the tick through which EVERY remote seat has been
## received contiguously. With one remote seat it is that seat's watermark.
var _remote_bundle_contiguous := 0
var _hash_barrier_tick := -1
var _hash_sent := false
var _local_hash := ""
var _pause_commands_by_tick: Dictionary = {}
var _deferred_local_submissions: Array[Dictionary] = []
## tick -> {seat -> Array[command]}, pruned to COMMAND_HISTORY_TICKS. Backs
## ordered_commands_for_tick(); see canonical_command_order().
var _commands_by_tick: Dictionary = {}

## Per-remote-seat lockstep state. Keys are seat ints.
var _remote_bundles: Dictionary = {}      ## seat -> {tick: true}
var _remote_contiguous: Dictionary = {}   ## seat -> int
var _remote_last_seq: Dictionary = {}     ## seat -> int
var _remote_ack: Dictionary = {}          ## seat -> int
var _remote_hashes: Dictionary = {}       ## seat -> {tick: String}

## Seat bookkeeping. _seats is the occupancy + profile table (authoritative on
## the host, a mirror of the host's broadcast on a guest).
var _seats: Dictionary = {}               ## seat -> seat record
var _seat_peers: Dictionary = {}          ## host only: seat -> ENetPacketPeer
var _peer_seats: Dictionary = {}          ## host only: peer id -> seat

## Lobby state (pre-game phase, sim == null). lobby_seats is the canonical
## host-authoritative table both sides derive the launch roster from.
## lobby_local_profile / lobby_remote_profile are the 1v1 mirrors kept for the
## existing two-player callers.
var lobby_seats: Array = []
var lobby_local_profile: Dictionary = {}
var lobby_remote_profile: Dictionary = {}
var lobby_settings: Dictionary = {}
var lobby_chat_log: Array = []
var lobby_launch_roster: Array = []
var lobby_launch_settings: Dictionary = {}
var lobby_launch_received := false
## seat -> Array[String] of canonical hero documents. Host-authoritative like
## the seat table: a guest announces its own and adopts the host's table.
var lobby_seat_heroes: Dictionary = {}


func _init(simulation: RetailSliceSim = null) -> void:
	sim = simulation


# --- seat/team mapping -------------------------------------------------------

## Simulation team id for a seat, or -1 for a seat outside the ladder.
static func team_for_seat(seat: int) -> int:
	if seat < 0 or seat >= SEAT_TEAM_IDS.size():
		return -1
	return SEAT_TEAM_IDS[seat]


## Inverse of team_for_seat; -1 when the team is not a player seat (e.g. the
## sim's neutral team).
static func seat_for_team(team: int) -> int:
	return SEAT_TEAM_IDS.find(team)


## Occupied seats other than this peer's, ascending. These are exactly the
## seats a tick bundle must be received from before the sim may advance.
func remote_seats() -> Array[int]:
	var seats: Array[int] = []
	for seat in _seats:
		if int(seat) != local_seat:
			seats.append(int(seat))
	seats.sort()
	return seats


func occupied_seats() -> Array[int]:
	var seats: Array[int] = []
	for seat in _seats:
		seats.append(int(seat))
	seats.sort()
	return seats


func seat_count() -> int:
	return _seats.size()


# --- canonical command order -------------------------------------------------

## THE ordering rule, in one place: team ascending, then seq ascending.
##
## Deterministic and total for a lockstep tick, because within one tick every
## command carries the (team, seq) pair of its author, every seat maps to a
## distinct team, and seq is a per-seat monotonic counter — so no two commands
## can ever compare equal and no tie-break is needed. It is also independent of
## arrival order: the input array may be in any order at all and the output is
## the same. RetailSliceSim imposes exactly this order when it drains a tick,
## so this function describes the sim's behaviour rather than competing with it.
static func canonical_command_order(commands: Array) -> Array:
	var ordered := commands.duplicate(true)
	ordered.sort_custom(func(a: Dictionary, b: Dictionary) -> bool:
		var a_team := int(a.get("team", -1))
		var b_team := int(b.get("team", -1))
		if a_team != b_team:
			return a_team < b_team
		return int(a.get("seq", -1)) < int(b.get("seq", -1))
	)
	return ordered


## Every command this peer holds for `tick`, in canonical order. Two peers that
## have both received the full bundle set for `tick` must return byte-identical
## arrays; that equality is the lockstep invariant. Returns [] for a tick
## outside the retained history window.
func ordered_commands_for_tick(tick: int) -> Array:
	if not _commands_by_tick.has(tick):
		return []
	var by_seat := _commands_by_tick[tick] as Dictionary
	var flattened: Array = []
	for seat in by_seat:
		for command in (by_seat[seat] as Array):
			flattened.append((command as Dictionary).duplicate(true))
	return canonical_command_order(flattened)


func _record_commands(tick: int, seat: int, commands: Array) -> void:
	if commands.is_empty():
		return
	var by_seat := _commands_by_tick.get(tick, {}) as Dictionary
	var stored: Array = []
	for command in commands:
		stored.append((command as Dictionary).duplicate(true))
	by_seat[seat] = stored
	_commands_by_tick[tick] = by_seat


func _prune_command_history(current_tick: int) -> void:
	var oldest := current_tick - COMMAND_HISTORY_TICKS
	if oldest <= 0:
		return
	for tick in _commands_by_tick.keys():
		if int(tick) < oldest:
			_commands_by_tick.erase(tick)


# --- connection --------------------------------------------------------------

func host(port: int) -> Error:
	close()
	_is_host = true
	local_seat = HOST_SEAT
	local_team = team_for_seat(HOST_SEAT)
	# Legacy 1v1 mirror: with no guest yet, the first seat a guest will take.
	peer_team = team_for_seat(HOST_SEAT + 1)
	_connection = ENetConnection.new()
	var error := _connection.create_host_bound("*", port, ENET_PEER_ALLOWANCE, ENET_CHANNELS)
	if error != OK:
		_connection = null
		return error
	bound_port = _connection.get_local_port()
	_seats[HOST_SEAT] = _seat_record(HOST_SEAT, "", "men", 0, HOST_SEAT + 1, false, false)
	return OK


func join(address: String, port: int) -> Error:
	close()
	_is_host = false
	# Provisional identity so a caller that reads local_team before the welcome
	# lands sees the overwhelmingly common value; the host's welcome is
	# authoritative and overwrites it. No command may be submitted before
	# handshake_complete, so a provisional value can never reach the wire.
	local_seat = HOST_SEAT + 1
	local_team = team_for_seat(local_seat)
	peer_team = team_for_seat(HOST_SEAT)
	_connection = ENetConnection.new()
	var error := _connection.create_host(1, ENET_CHANNELS)
	if error != OK:
		_connection = null
		return error
	_peer = _connection.connect_to_host(address, port, ENET_CHANNELS)
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
	if _connection == null:
		_graceful_close_done = true
		return
	var peers := _active_peers()
	if peers.is_empty():
		_graceful_close_done = true
		return
	_graceful_close_requested = true
	for peer in peers:
		peer.peer_disconnect_later(0)


func poll_graceful_close() -> bool:
	## True once ENet confirmed the deferred disconnects (all queued reliable
	## packets delivered) or there is nothing left to drain.
	if _graceful_close_done or not _graceful_close_requested or _connection == null:
		return true
	var outstanding := _active_peers().size()
	for _index in range(64):
		var event := _connection.service(0)
		if event.is_empty() or int(event[0]) == ENetConnection.EVENT_NONE:
			return outstanding == 0
		if int(event[0]) == ENetConnection.EVENT_DISCONNECT:
			outstanding -= 1
			if outstanding <= 0:
				_graceful_close_done = true
				return true
	return false


func _active_peers() -> Array:
	var peers: Array = []
	if _is_host:
		for seat in _seat_peers:
			var peer := _seat_peers[seat] as ENetPacketPeer
			if peer != null and peer.is_active():
				peers.append(peer)
	elif _peer != null and _peer.is_active():
		peers.append(_peer)
	return peers


func close() -> void:
	# Immediate but NOTIFIED disconnect: the remote peer receives the ENet
	# disconnect event right away (a lobby LEAVE must be visible to the other
	# side), unlike reset() which drops silently into a timeout.
	for peer in _active_peers():
		(peer as ENetPacketPeer).peer_disconnect_now(0)
	if _connection != null:
		_connection.destroy()
	_connection = null
	_peer = null
	local_seat = -1
	# local_team / peer_team are deliberately NOT reset here: host()/join() set
	# them immediately after their own close(), and out-of-tree readers (the
	# vertical slice's HUD/control path) sample them after a session ends.
	connected = false
	handshake_complete = false
	desynced = false
	desync_tick = -1
	desync_seat = -1
	bound_port = 0
	remote_ack_tick = 0
	rejected_packets = 0
	rejected_remote_commands = 0
	pause_command_tick = -1
	pause_command_state = false
	join_refused_reason = ""
	required_seat_count = 0
	_graceful_close_requested = false
	_graceful_close_done = false
	_next_local_seq = 0
	_join_sent = false
	_local_commands_by_tick.clear()
	_bundle_sent_through = 0
	_remote_bundle_contiguous = 0
	_hash_barrier_tick = -1
	_hash_sent = false
	_local_hash = ""
	_pause_commands_by_tick.clear()
	_deferred_local_submissions.clear()
	_commands_by_tick.clear()
	_remote_bundles.clear()
	_remote_contiguous.clear()
	_remote_last_seq.clear()
	_remote_ack.clear()
	_remote_hashes.clear()
	_seats.clear()
	_seat_peers.clear()
	_peer_seats.clear()
	lobby_seats = []
	lobby_local_profile = {}
	lobby_remote_profile = {}
	lobby_settings = {}
	lobby_chat_log = []
	lobby_launch_roster = []
	lobby_launch_settings = {}
	lobby_launch_received = false


# --- local input -------------------------------------------------------------

func submit_local(type: String, args: Dictionary) -> void:
	if sim == null or desynced or input_delay_ticks <= 0:
		return
	# A seat is only final once the host has welcomed this peer; a command
	# stamped with a provisional team would be rejected by everyone else.
	if not handshake_complete or local_seat < 0:
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
	_record_commands(command_tick, local_seat, commands)
	if type == "pause" or type == "resume":
		var pause_commands: Array = _pause_commands_by_tick.get(command_tick, [])
		pause_commands.append(command)
		_pause_commands_by_tick[command_tick] = pause_commands


# --- pump --------------------------------------------------------------------

func poll() -> void:
	if _connection == null:
		return
	_service_events()
	if not connected:
		return
	if not _is_host and not _join_sent and _peer != null and _peer.is_active():
		_send_to_peer(_peer, {"kind": "join", "version": PROTOCOL_VERSION})
		_join_sent = true
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
	# A match must never start (or continue) with fewer seats than the agreed
	# roster: a missing seat's commands would silently never arrive.
	if required_seat_count > 0 and _seats.size() < required_seat_count:
		return false
	if sim.clock_paused and not sim._has_pending_resume_command():
		return false
	var next_tick := sim.tick_index + 1
	if next_tick > _bundle_sent_through:
		return false
	# EVERY participating seat's bundle for the next tick, without exception.
	var seats := remote_seats()
	if seats.is_empty():
		return false
	for seat in seats:
		var bundles := _remote_bundles.get(seat, {}) as Dictionary
		if not bundles.has(next_tick):
			return false
	return true


func advance_if_ready() -> bool:
	if not can_advance():
		return false
	var next_tick := sim.tick_index + 1
	for seat in remote_seats():
		var bundles := _remote_bundles.get(seat, {}) as Dictionary
		bundles.erase(next_tick)
		_remote_bundles[seat] = bundles
	sim.tick()
	if sim.tick_index != next_tick:
		return false
	_prune_command_history(sim.tick_index)
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
				_on_peer_connect(event_peer)
			ENetConnection.EVENT_DISCONNECT:
				_on_peer_disconnect(event_peer)
			ENetConnection.EVENT_RECEIVE:
				_receive_packet(event_peer, event_peer.get_packet())


func _on_peer_connect(event_peer: ENetPacketPeer) -> void:
	if not _is_host:
		if _peer != null and event_peer != _peer:
			event_peer.reset()
			return
		_peer = event_peer
		connected = true
		return
	# The seat is not assigned here: it is assigned when the peer identifies
	# itself with a `join` envelope carrying a protocol version we accept.
	connected = true


func _on_peer_disconnect(event_peer: ENetPacketPeer) -> void:
	if not _is_host:
		if event_peer == _peer:
			connected = false
			handshake_complete = false
		return
	var key := event_peer.get_instance_id()
	if not _peer_seats.has(key):
		# A peer that was refused before it was ever seated (full game, wrong
		# protocol, match already running). It still has to stop counting
		# towards "somebody is connected".
		if _seat_peers.is_empty():
			connected = false
			handshake_complete = false
			_peer = null
		return
	var seat := int(_peer_seats[key])
	_peer_seats.erase(key)
	_seat_peers.erase(seat)
	_release_seat(seat)
	if _peer == event_peer:
		_peer = null
	if _seat_peers.is_empty():
		connected = false
		handshake_complete = false
		_peer = null
	else:
		_peer = _seat_peers[_seat_peers.keys().min()] as ENetPacketPeer
	_refresh_primary_remote()
	_broadcast_seat_table()
	lobby_updated.emit()


func _release_seat(seat: int) -> void:
	_seats.erase(seat)
	# A vacated seat's heroes leave with it, so a later peer that takes the seat
	# never inherits the previous occupant's roster.
	lobby_seat_heroes.erase(seat)
	_remote_bundles.erase(seat)
	_remote_contiguous.erase(seat)
	_remote_last_seq.erase(seat)
	_remote_ack.erase(seat)
	_remote_hashes.erase(seat)
	_recompute_watermarks()
	_rebuild_lobby_seats()


func _lowest_free_seat() -> int:
	for seat in range(HOST_SEAT + 1, MAX_SEATS):
		if not _seats.has(seat):
			return seat
	return -1


# --- send helpers ------------------------------------------------------------

func _send_to_peer(peer: ENetPacketPeer, envelope: Dictionary) -> void:
	if peer == null or not peer.is_active():
		return
	var packet := var_to_bytes(envelope)
	if packet.is_empty() or packet.size() > MAX_PACKET_BYTES:
		return
	peer.send(0, packet, ENetPacketPeer.FLAG_RELIABLE)


## Legacy single-peer send, retained because runners inject raw envelopes with
## it. On a guest it targets the host; on a host it reaches every guest.
func _send_envelope(envelope: Dictionary) -> void:
	if _is_host:
		_broadcast(envelope)
		return
	_send_to_peer(_peer, envelope)


func _broadcast(envelope: Dictionary, except_seat: int = -1) -> void:
	for seat in _seat_peers:
		if int(seat) == except_seat:
			continue
		_send_to_peer(_seat_peers[seat] as ENetPacketPeer, envelope)


func _send_to_seat(seat: int, envelope: Dictionary) -> void:
	if not _seat_peers.has(seat):
		return
	_send_to_peer(_seat_peers[seat] as ENetPacketPeer, envelope)


# --- lockstep send -----------------------------------------------------------

func _send_ready_bundles() -> void:
	# The clock does not start until the agreed roster is actually seated.
	# Without this a host would begin ticking the instant the FIRST guest
	# handshakes, and every later guest would then be turned away by the
	# late-join guard — an eight-player match could never assemble.
	if required_seat_count > 0 and _seats.size() < required_seat_count:
		return
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


# --- receive -----------------------------------------------------------------

func _receive_packet(event_peer: ENetPacketPeer, packet: PackedByteArray) -> void:
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
	var kind := String(envelope["kind"])
	# A guest only ever hears from the host; a host only ever hears from a peer
	# it has already seated (the sole exception being that peer's own `join`).
	if not _is_host and event_peer != _peer:
		rejected_packets += 1
		return
	var sender_seat := -1
	if _is_host:
		if kind == "join":
			_receive_join(event_peer, envelope)
			return
		var key := event_peer.get_instance_id()
		if not _peer_seats.has(key):
			rejected_packets += 1
			return
		sender_seat = int(_peer_seats[key])
	else:
		sender_seat = HOST_SEAT
	match kind:
		"welcome":
			_receive_welcome(envelope)
		"refuse":
			_receive_refuse(envelope)
		"bundle":
			_receive_bundle(sender_seat, envelope)
		"relay":
			_receive_relay(envelope)
		"hash":
			_receive_hash(sender_seat, envelope)
		"relay.hash":
			_receive_relay_hash(envelope)
		"lobby.profile":
			_receive_lobby_profile(sender_seat, envelope)
		"lobby.chat":
			_receive_lobby_chat(sender_seat, envelope)
		"relay.chat":
			_receive_relay_chat(envelope)
		"lobby.seats":
			_receive_lobby_seats(envelope)
		"lobby.heroes":
			_receive_lobby_heroes(sender_seat, envelope)
		"lobby.heroTable":
			_receive_lobby_hero_table(envelope)
		"lobby.settings":
			_receive_lobby_settings(envelope)
		"lobby.launch":
			_receive_lobby_launch(envelope)
		_:
			rejected_packets += 1


## Host side. Seats a guest, or refuses it with a stated reason.
func _receive_join(event_peer: ENetPacketPeer, envelope: Dictionary) -> void:
	if envelope.size() != 2 or typeof(envelope.get("version")) != TYPE_INT:
		rejected_packets += 1
		return
	var key := event_peer.get_instance_id()
	if _peer_seats.has(key):
		# Re-announcing an already-seated peer is a protocol violation.
		rejected_packets += 1
		return
	if int(envelope["version"]) != PROTOCOL_VERSION:
		_refuse(event_peer, "Different game version (needs protocol v%d, this host speaks v%d)." % [
			int(envelope["version"]), PROTOCOL_VERSION,
		])
		return
	# Late join is out of scope: once the first tick bundle has gone out the
	# match is running and a new seat could never be made consistent.
	if _bundle_sent_through > 0 or lobby_launch_received:
		_refuse(event_peer, "That match has already started.")
		return
	var seat := _lowest_free_seat()
	if seat < 0:
		_refuse(event_peer, "The game is full (%d of %d seats taken)." % [_seats.size(), MAX_SEATS])
		return
	_peer_seats[key] = seat
	_seat_peers[seat] = event_peer
	_seats[seat] = _seat_record(seat, "", "men", seat % LOBBY_HOUSE_COLORS.size(), seat + 1, false, false)
	_remote_bundles[seat] = {}
	_remote_contiguous[seat] = 0
	_remote_last_seq[seat] = -1
	_remote_ack[seat] = 0
	_remote_hashes[seat] = {}
	if _peer == null:
		_peer = event_peer
	handshake_complete = true
	if lobby_settings.is_empty():
		lobby_settings = lobby_default_settings()
	_recompute_watermarks()
	_rebuild_lobby_seats()
	_refresh_primary_remote()
	_send_to_peer(event_peer, {
		"kind": "welcome",
		"version": PROTOCOL_VERSION,
		"seat": seat,
		"seats": lobby_seats.duplicate(true),
	})
	# Everyone (including the newcomer) gets the authoritative table, and the
	# newcomer additionally needs the current settings.
	_broadcast_seat_table()
	_broadcast_hero_table()
	if not lobby_settings.is_empty():
		_send_to_peer(event_peer, {
			"kind": "lobby.settings",
			"map_id": String(lobby_settings["map_id"]),
			"resources": int(lobby_settings["resources"]),
			"cp_factor": float(lobby_settings["cp_factor"]),
			"build_plots": bool(lobby_settings["build_plots"]),
			"allow_custom_heroes": bool(lobby_settings.get("allow_custom_heroes", true)),
			"allow_ring_heroes": bool(lobby_settings.get("allow_ring_heroes", true)),
			"logic_random_seed": int(lobby_settings.get("logic_random_seed", 0)),
		})
	# The host's own announced profile is not part of the table until it has
	# been announced; if it already was, replay it to the newcomer so its 1v1
	# mirror (lobby_remote_profile) fills in exactly as in a two-player game.
	if not lobby_local_profile.is_empty():
		_send_to_peer(event_peer, {
			"kind": "lobby.profile",
			"name": String(lobby_local_profile["name"]),
			"faction": String(lobby_local_profile["faction"]),
			"color": int(lobby_local_profile["color"]),
			"ready": bool(lobby_local_profile["ready"]),
		})
	lobby_updated.emit()


func _refuse(event_peer: ENetPacketPeer, reason: String) -> void:
	_send_to_peer(event_peer, {"kind": "refuse", "reason": reason})
	if _connection != null:
		_connection.flush()
	event_peer.peer_disconnect_later(0)


func _receive_refuse(envelope: Dictionary) -> void:
	if _is_host or envelope.size() != 2 or typeof(envelope.get("reason")) != TYPE_STRING:
		rejected_packets += 1
		return
	join_refused_reason = _clean_lobby_text(String(envelope["reason"]), 160)
	handshake_complete = false
	DiagLogScript.emit_failure("sim.join_refused", join_refused_reason)
	join_refused.emit(join_refused_reason)


func _receive_welcome(envelope: Dictionary) -> void:
	if _is_host or envelope.size() != 4 \
		or typeof(envelope.get("version")) != TYPE_INT \
		or typeof(envelope.get("seat")) != TYPE_INT \
		or typeof(envelope.get("seats")) != TYPE_ARRAY \
		or int(envelope["version"]) != PROTOCOL_VERSION:
		rejected_packets += 1
		return
	var seat := int(envelope["seat"])
	if seat <= HOST_SEAT or seat >= MAX_SEATS:
		rejected_packets += 1
		return
	var table := _validated_seat_table(envelope["seats"] as Array)
	if table.is_empty() or not table.has(seat):
		rejected_packets += 1
		return
	local_seat = seat
	local_team = team_for_seat(seat)
	peer_team = team_for_seat(HOST_SEAT)
	_adopt_seat_table(table)
	handshake_complete = true
	# Both peers seed identical default lobby settings at handshake so a host
	# launch without an explicit settings change still verifies byte-equal.
	if lobby_settings.is_empty():
		lobby_settings = lobby_default_settings()
	lobby_updated.emit()


## Applies a bundle authored by `sender_seat`. On the host this also relays it
## to every other guest, which is the only path a guest's input takes to its
## fellow guests.
func _receive_bundle(sender_seat: int, envelope: Dictionary) -> void:
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
	if not _apply_bundle(sender_seat, int(envelope["tick"]), envelope["commands"] as Array, int(envelope["ack"])):
		return
	if _is_host:
		_broadcast({
			"kind": "relay",
			"seat": sender_seat,
			"tick": int(envelope["tick"]),
			"commands": envelope["commands"],
			"ack": int(envelope["ack"]),
		}, sender_seat)


## Guest side: another guest's bundle, forwarded by the host.
func _receive_relay(envelope: Dictionary) -> void:
	if _is_host or sim == null:
		rejected_packets += 1
		return
	if envelope.size() != 5 \
		or typeof(envelope.get("seat")) != TYPE_INT \
		or typeof(envelope.get("tick")) != TYPE_INT \
		or typeof(envelope.get("commands")) != TYPE_ARRAY \
		or typeof(envelope.get("ack")) != TYPE_INT:
		rejected_packets += 1
		return
	var seat := int(envelope["seat"])
	# A relay may only ever describe a seat that is not us and not the host
	# (the host speaks for itself with `bundle`).
	if seat == local_seat or seat == HOST_SEAT or not _seats.has(seat):
		rejected_packets += 1
		return
	_apply_bundle(seat, int(envelope["tick"]), envelope["commands"] as Array, int(envelope["ack"]))


## Shared bundle admission for both the direct and the relayed path. Returns
## false (having counted a rejection) when the bundle was not accepted.
func _apply_bundle(seat: int, bundle_tick: int, commands: Array, ack_tick: int) -> bool:
	if not _seats.has(seat) or seat == local_seat:
		rejected_packets += 1
		return false
	var seat_team := team_for_seat(seat)
	if seat_team < 0:
		rejected_packets += 1
		return false
	var contiguous := int(_remote_contiguous.get(seat, 0))
	if bundle_tick <= 0 or ack_tick < 0 or ack_tick > _bundle_sent_through or commands.size() > MAX_COMMANDS_PER_BUNDLE:
		rejected_packets += 1
		return false
	if bundle_tick <= contiguous:
		return false
	if bundle_tick != contiguous + 1 or bundle_tick <= sim.tick_index:
		rejected_packets += 1
		return false
	var seen_sequences: Dictionary = {}
	var last_sequence := int(_remote_last_seq.get(seat, -1))
	for command_value in commands:
		if typeof(command_value) != TYPE_DICTIONARY:
			rejected_packets += 1
			return false
		var command := command_value as Dictionary
		if not CommandScript.validate(command) \
			or int(command["tick"]) != bundle_tick \
			or int(command["team"]) != seat_team \
			or seen_sequences.has(int(command["seq"])) \
			or int(command["seq"]) <= last_sequence:
			rejected_remote_commands += 1
			return false
		seen_sequences[int(command["seq"])] = true
		last_sequence = int(command["seq"])
	for command_value in commands:
		if not sim.submit_command((command_value as Dictionary).duplicate(true)):
			rejected_packets += 1
			return false
		var command := command_value as Dictionary
		if String(command["type"]) == "pause" or String(command["type"]) == "resume":
			var pause_commands: Array = _pause_commands_by_tick.get(bundle_tick, [])
			pause_commands.append(command.duplicate(true))
			_pause_commands_by_tick[bundle_tick] = pause_commands
	_record_commands(bundle_tick, seat, commands)
	var bundles := _remote_bundles.get(seat, {}) as Dictionary
	bundles[bundle_tick] = true
	_remote_bundles[seat] = bundles
	_remote_last_seq[seat] = last_sequence
	_remote_ack[seat] = maxi(int(_remote_ack.get(seat, 0)), ack_tick)
	while bundles.has(contiguous + 1):
		contiguous += 1
	_remote_contiguous[seat] = contiguous
	_recompute_watermarks()
	return true


## _remote_bundle_contiguous and remote_ack_tick are MINIMA across every remote
## seat: the lockstep clock can only be as far along as its slowest peer.
func _recompute_watermarks() -> void:
	var seats := remote_seats()
	if seats.is_empty():
		_remote_bundle_contiguous = 0
		remote_ack_tick = 0
		return
	var lowest_contiguous := -1
	var lowest_ack := -1
	for seat in seats:
		var contiguous := int(_remote_contiguous.get(seat, 0))
		var ack := int(_remote_ack.get(seat, 0))
		if lowest_contiguous < 0 or contiguous < lowest_contiguous:
			lowest_contiguous = contiguous
		if lowest_ack < 0 or ack < lowest_ack:
			lowest_ack = ack
	_remote_bundle_contiguous = maxi(0, lowest_contiguous)
	remote_ack_tick = maxi(remote_ack_tick, maxi(0, lowest_ack))


func _receive_hash(sender_seat: int, envelope: Dictionary) -> void:
	if envelope.size() != 3 \
		or typeof(envelope.get("tick")) != TYPE_INT \
		or typeof(envelope.get("hash")) != TYPE_STRING:
		rejected_packets += 1
		return
	if not _record_remote_hash(sender_seat, int(envelope["tick"]), String(envelope["hash"])):
		return
	if _is_host:
		_broadcast({
			"kind": "relay.hash",
			"seat": sender_seat,
			"tick": int(envelope["tick"]),
			"hash": String(envelope["hash"]),
		}, sender_seat)
	_compare_hash_if_ready(int(envelope["tick"]))


func _receive_relay_hash(envelope: Dictionary) -> void:
	if _is_host or envelope.size() != 4 \
		or typeof(envelope.get("seat")) != TYPE_INT \
		or typeof(envelope.get("tick")) != TYPE_INT \
		or typeof(envelope.get("hash")) != TYPE_STRING:
		rejected_packets += 1
		return
	var seat := int(envelope["seat"])
	if seat == local_seat or seat == HOST_SEAT or not _seats.has(seat):
		rejected_packets += 1
		return
	if not _record_remote_hash(seat, int(envelope["tick"]), String(envelope["hash"])):
		return
	_compare_hash_if_ready(int(envelope["tick"]))


func _record_remote_hash(seat: int, hash_tick: int, hash_value: String) -> bool:
	if not _seats.has(seat) or seat == local_seat:
		rejected_packets += 1
		return false
	if hash_tick <= 0 or hash_tick % HASH_INTERVAL_TICKS != 0 \
		or hash_value.length() != 64 or not hash_value.is_valid_hex_number(false):
		rejected_packets += 1
		return false
	var seat_hashes := _remote_hashes.get(seat, {}) as Dictionary
	seat_hashes[hash_tick] = hash_value.to_lower()
	_remote_hashes[seat] = seat_hashes
	return true


## The barrier clears only when EVERY remote seat has reported the barrier tick
## and every report matches. A single mismatch stops this peer immediately and
## names the seat that diverged.
func _compare_hash_if_ready(hash_tick: int) -> void:
	if not _hash_sent or hash_tick != _hash_barrier_tick:
		return
	var seats := remote_seats()
	if seats.is_empty():
		return
	for seat in seats:
		var seat_hashes := _remote_hashes.get(seat, {}) as Dictionary
		if not seat_hashes.has(hash_tick):
			return
	for seat in seats:
		var seat_hashes := _remote_hashes.get(seat, {}) as Dictionary
		if String(seat_hashes[hash_tick]) != _local_hash.to_lower():
			desynced = true
			desync_tick = hash_tick
			desync_seat = seat
			# A desync is THE bug report. Until now it only mutated three fields
			# and stopped the peer, which means the evidence lived in memory of a
			# process that is about to be closed by an annoyed player. The hashes
			# are recorded verbatim: "they differed" is not a finding, "seat 2 had
			# X where we had Y at tick N" is.
			DiagLogScript.emit_failure(
				"sim.desync",
				"state hash mismatch at tick %d with seat %d" % [hash_tick, seat],
				"local=%s remote=%s localSeat=%d" % [
					_local_hash.to_lower(), String(seat_hashes[hash_tick]), local_seat
				]
			)
			return
	for seat in seats:
		var seat_hashes := _remote_hashes.get(seat, {}) as Dictionary
		seat_hashes.erase(hash_tick)
		_remote_hashes[seat] = seat_hashes
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
		# RETAIL'S OWN TOGGLE. BFME2's online GAME SETUP carries an
		# AllowCustomHeroes switch, and a host that turns it off is refusing
		# created heroes for EVERY seat - so it empties the agreed hero table
		# rather than filtering at each peer, where two peers could disagree.
		"allow_custom_heroes": true,
		"allow_ring_heroes": true,
		"logic_random_seed": 0,
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


func custom_heroes_allowed() -> bool:
	## Retail's AllowCustomHeroes. Absent on a settings dict from before the
	## toggle existed, which reads as allowed - the historical behaviour.
	return bool(lobby_settings.get("allow_custom_heroes", true))


func ring_heroes_allowed() -> bool:
	return bool(lobby_settings.get("allow_ring_heroes", true))


static func _canonical_profile(player_name: String, faction: String, color: int, ready: bool) -> Dictionary:
	## Canonical key order — profiles feed the byte-equal launch roster.
	return {"name": player_name, "faction": faction, "color": color, "ready": ready}


## Canonical seat record. Fixed key order: the table is byte-compared between
## host and guest, so field order is part of the contract. `announced` is false
## until that seat's player has actually sent a profile; an unannounced seat
## holds a place on the wire but is never treated as a known player.
static func _seat_record(seat: int, player_name: String, faction: String, color: int, alliance: int, ready: bool, announced: bool) -> Dictionary:
	return {
		"seat": seat,
		"team": team_for_seat(seat),
		"name": player_name,
		"faction": faction,
		"color": color,
		"alliance": alliance,
		"ready": ready,
		"announced": announced,
	}


static func _clean_lobby_text(text: String, limit: int) -> String:
	var cleaned := ""
	for index in mini(text.length(), limit):
		var code := text.unicode_at(index)
		if code >= 32 and code < 127:
			cleaned += char(code)
	return cleaned.strip_edges()


# --- seat table --------------------------------------------------------------

func _rebuild_lobby_seats() -> void:
	var seats: Array = []
	for seat in occupied_seats():
		seats.append((_seats[seat] as Dictionary).duplicate(true))
	lobby_seats = seats


func _adopt_seat_table(table: Dictionary) -> void:
	_seats = {}
	for seat in table:
		_seats[int(seat)] = (table[seat] as Dictionary).duplicate(true)
	for seat in remote_seats():
		if not _remote_bundles.has(seat):
			_remote_bundles[seat] = {}
			_remote_contiguous[seat] = 0
			_remote_last_seq[seat] = -1
			_remote_ack[seat] = 0
			_remote_hashes[seat] = {}
	for seat in _remote_bundles.keys():
		if not _seats.has(seat) or int(seat) == local_seat:
			_remote_bundles.erase(seat)
			_remote_contiguous.erase(seat)
			_remote_last_seq.erase(seat)
			_remote_ack.erase(seat)
			_remote_hashes.erase(seat)
	_recompute_watermarks()
	_rebuild_lobby_seats()
	_refresh_primary_remote()


func _broadcast_seat_table() -> void:
	if not _is_host:
		return
	_rebuild_lobby_seats()
	_broadcast({"kind": "lobby.seats", "seats": lobby_seats.duplicate(true)})


# --- created heroes ----------------------------------------------------------

## The one spelling of a hero document on the wire: JSON with sorted keys.
## Returns "" for anything that is not a dictionary or does not survive the
## round trip, which is also how a malformed remote payload is refused.
static func canonical_hero_document(value: Variant) -> String:
	var text := ""
	if typeof(value) == TYPE_DICTIONARY:
		text = JSON.stringify(value, "", true)
	elif typeof(value) == TYPE_STRING:
		text = String(value)
	else:
		return ""
	var parsed: Variant = JSON.parse_string(text)
	if typeof(parsed) != TYPE_DICTIONARY:
		return ""
	# THE PARSE IS NOT OPTIONAL, even for a dictionary this peer built itself.
	# JSON widens every number to float, so a document stringified straight out
	# of memory spells `0` exactly where the same document read back off the
	# wire spells `0.0`. Canonicalizing through a parse makes the two spellings
	# one - without it a peer refuses its OWN hero on the echo.
	return JSON.stringify(parsed, "", true)


## Validates a whole hero list off the wire. Returns the canonical list, or []
## for anything over the caps or not already canonical. An EMPTY list is legal
## and is the normal case: most seats bring no created hero.
static func validated_hero_documents(rows: Variant) -> Array:
	if typeof(rows) != TYPE_ARRAY:
		return []
	var source := rows as Array
	if source.size() > LOBBY_HEROES_PER_SEAT_MAX:
		return []
	var out: Array = []
	var total := 0
	for row_value in source:
		if typeof(row_value) != TYPE_STRING:
			return []
		var text := String(row_value)
		# BYTES, NOT CHARACTERS. The packet cap these budgets are sized against
		# is a byte count, and one codepoint is up to four bytes - so measuring
		# in `length()` let a wide-script document four times over budget pass a
		# cap that promised the whole rebroadcast table would fit one packet.
		var size := text.to_utf8_buffer().size()
		if size > LOBBY_HERO_BYTES_MAX:
			return []
		total += size
		if total > LOBBY_HEROES_BYTES_MAX:
			return []
		var canonical := canonical_hero_document(text)
		if canonical == "" or canonical != text:
			return []
		out.append(canonical)
	return out


func heroes_for_seat(seat: int) -> Array:
	return (lobby_seat_heroes.get(seat, []) as Array).duplicate()


## Announce this peer's own created heroes. Accepts profile dictionaries or
## already-canonical documents; anything that fails the caps is refused whole
## rather than trimmed, so a peer never silently plays with fewer heroes than it
## announced.
func send_lobby_heroes(profiles: Array) -> bool:
	if not handshake_complete or local_seat < 0:
		return false
	var canonical: Array = []
	for profile_value in profiles:
		var document := canonical_hero_document(profile_value)
		if document == "":
			return false
		canonical.append(document)
	var validated := validated_hero_documents(canonical)
	if validated.size() != canonical.size():
		return false
	var envelope := {"kind": "lobby.heroes", "heroes": validated.duplicate()}
	# A packet the transport would silently drop is a refusal HERE. _send_to_peer
	# discards an oversized packet without saying so, and a caller that was told
	# "sent" would then play a match believing it had announced heroes nobody
	# received - the exact split-brain the byte-equal launch gate exists to stop.
	if var_to_bytes(envelope).size() > MAX_PACKET_BYTES:
		return false
	lobby_seat_heroes[local_seat] = validated
	_send_envelope(envelope)
	if _is_host:
		_broadcast_hero_table()
	if _connection != null:
		_connection.flush()
	return true


func _broadcast_hero_table() -> void:
	if not _is_host:
		return
	var envelope := {"kind": "lobby.heroTable", "table": _hero_table_rows()}
	# The caps make this unreachable; the guard is here anyway because the cost
	# of being wrong is a table the transport drops without a word, leaving every
	# guest on a stale view that then refuses the launch while the host proceeds.
	# A denial of launch is cheaper than a desync but it is not free, and a
	# silent one is unbudgeable. Loud beats silent.
	if var_to_bytes(envelope).size() > MAX_PACKET_BYTES:
		rejected_packets += 1
		push_error(
			"RetailLockstepSession refusing a %d-byte created-hero table; the packet limit is %d"
			% [var_to_bytes(envelope).size(), MAX_PACKET_BYTES]
		)
		return
	_broadcast(envelope)


func _hero_table_rows() -> Array:
	## Ascending seat order, so the table has one spelling on every peer.
	var seats: Array = lobby_seat_heroes.keys()
	seats.sort()
	var rows: Array = []
	for seat_value in seats:
		var seat := int(seat_value)
		if not _seats.has(seat):
			continue
		rows.append({"seat": seat, "heroes": (lobby_seat_heroes[seat_value] as Array).duplicate()})
	return rows


func _receive_lobby_heroes(sender_seat: int, envelope: Dictionary) -> void:
	if not handshake_complete or envelope.size() != 2 \
		or typeof(envelope.get("heroes")) != TYPE_ARRAY:
		rejected_packets += 1
		return
	if not _seats.has(sender_seat) or sender_seat == local_seat:
		rejected_packets += 1
		return
	var rows := envelope["heroes"] as Array
	var validated := validated_hero_documents(rows)
	if validated.size() != rows.size():
		rejected_packets += 1
		return
	lobby_seat_heroes[sender_seat] = validated
	if _is_host:
		_broadcast_hero_table()
		if _connection != null:
			_connection.flush()
	lobby_updated.emit()


func _receive_lobby_hero_table(envelope: Dictionary) -> void:
	## Host-authoritative, exactly like the seat table.
	if _is_host or not handshake_complete or envelope.size() != 2 \
		or typeof(envelope.get("table")) != TYPE_ARRAY:
		rejected_packets += 1
		return
	var rows := envelope["table"] as Array
	if rows.size() > MAX_SEATS:
		rejected_packets += 1
		return
	var table: Dictionary = {}
	var previous_seat := -1
	for row_value in rows:
		if typeof(row_value) != TYPE_DICTIONARY:
			rejected_packets += 1
			return
		var row := row_value as Dictionary
		if row.size() != 2 or typeof(row.get("seat")) != TYPE_INT \
			or typeof(row.get("heroes")) != TYPE_ARRAY:
			rejected_packets += 1
			return
		var seat := int(row["seat"])
		if seat < 0 or seat >= MAX_SEATS or seat <= previous_seat:
			rejected_packets += 1
			return
		var announced := row["heroes"] as Array
		var validated := validated_hero_documents(announced)
		if validated.size() != announced.size():
			rejected_packets += 1
			return
		previous_seat = seat
		table[seat] = validated
	# Our own row is the one we announced; the host echoes it back and it must
	# agree, or this peer would field a roster it never asked for.
	if table.has(local_seat) and lobby_seat_heroes.has(local_seat):
		if var_to_bytes(table[local_seat]) != var_to_bytes(lobby_seat_heroes[local_seat]):
			rejected_packets += 1
			return
	lobby_seat_heroes = table
	lobby_updated.emit()


## Keeps the 1v1 mirrors (peer_team, lobby_remote_profile) pointing at the
## primary remote seat: the host for a guest, the lowest-numbered occupied
## guest seat for the host.
func _refresh_primary_remote() -> void:
	var primary := -1
	if _is_host:
		var seats := remote_seats()
		primary = seats[0] if not seats.is_empty() else -1
	else:
		primary = HOST_SEAT if _seats.has(HOST_SEAT) else -1
	if primary < 0:
		peer_team = team_for_seat(HOST_SEAT + 1) if _is_host else team_for_seat(HOST_SEAT)
		lobby_remote_profile = {}
		return
	peer_team = team_for_seat(primary)
	var record := _seats[primary] as Dictionary
	if not bool(record.get("announced", false)):
		lobby_remote_profile = {}
		return
	lobby_remote_profile = _canonical_profile(
		String(record["name"]), String(record["faction"]), int(record["color"]), bool(record["ready"])
	)


## Validates a whole seat table off the wire. Returns {} for anything that is
## not fully well-formed: {seat: record} on success.
func _validated_seat_table(rows: Array) -> Dictionary:
	if rows.is_empty() or rows.size() > MAX_SEATS:
		return {}
	var table: Dictionary = {}
	var previous_seat := -1
	for row_value in rows:
		if typeof(row_value) != TYPE_DICTIONARY:
			return {}
		var row := row_value as Dictionary
		if row.size() != 8 \
			or typeof(row.get("seat")) != TYPE_INT \
			or typeof(row.get("team")) != TYPE_INT \
			or typeof(row.get("name")) != TYPE_STRING \
			or typeof(row.get("faction")) != TYPE_STRING \
			or typeof(row.get("color")) != TYPE_INT \
			or typeof(row.get("alliance")) != TYPE_INT \
			or typeof(row.get("ready")) != TYPE_BOOL \
			or typeof(row.get("announced")) != TYPE_BOOL:
			return {}
		var seat := int(row["seat"])
		if seat < 0 or seat >= MAX_SEATS or seat <= previous_seat:
			return {}
		if int(row["team"]) != team_for_seat(seat):
			return {}
		if not LOBBY_FACTION_IDS.has(String(row["faction"])):
			return {}
		if int(row["color"]) < 0 or int(row["color"]) >= LOBBY_HOUSE_COLORS.size():
			return {}
		if int(row["alliance"]) < LOBBY_ALLIANCE_MIN or int(row["alliance"]) > LOBBY_ALLIANCE_MAX:
			return {}
		if bool(row["announced"]) and not lobby_name_valid(String(row["name"])):
			return {}
		previous_seat = seat
		table[seat] = _seat_record(
			seat, String(row["name"]), String(row["faction"]), int(row["color"]),
			int(row["alliance"]), bool(row["ready"]), bool(row["announced"])
		)
	if not table.has(HOST_SEAT):
		return {}
	return table


# --- launch roster -----------------------------------------------------------

## The launch descriptor list, in the exact retail_team_setup shape the menu's
## GAME SETUP writes ({team, faction, controller, difficulty, alliance, color,
## start_index}). Construction order and every default are fixed here so every
## peer derives a byte-identical list from the same seat table — the roster
## feeds the deterministic sim seed. Seats are emitted in ascending seat order;
## team comes from SEAT_TEAM_IDS; alliance is the host-assigned value, which
## defaults to seat + 1 (free-for-all) and reproduces the historical 1v1 pair
## of alliances 1 and 2 exactly.
static func lobby_roster_from_seats(seats: Array, heroes_by_seat: Dictionary = {}) -> Array:
	var descriptors: Array = []
	for row_value in seats:
		var row := row_value as Dictionary
		var seat := int(row.get("seat", 0))
		descriptors.append({
			"team": team_for_seat(seat),
			"faction": String(row.get("faction", "men")),
			"controller": "human",
			"difficulty": "medium",
			"alliance": int(row.get("alliance", seat + 1)),
			"color": LOBBY_HOUSE_COLORS[clampi(int(row.get("color", 0)), 0, LOBBY_HOUSE_COLORS.size() - 1)],
			"start_index": 0,
			# THE SEAT'S CREATED HEROES, carried on the roster so the launch
			# gate's byte comparison already covers them: a peer whose hero
			# table disagrees with the host's cannot start the match at all,
			# which is the only outcome cheaper than a desync.
			"seat": seat,
			"heroes": (heroes_by_seat.get(seat, []) as Array).duplicate(),
			# DECLARED, not inferred. A created hero is admitted more strictly in
			# a lockstep match than in a skirmish - it must have been built
			# against exactly the mounted table, because leniency there is a
			# desync. The consumer must be able to tell the two rosters apart
			# without guessing from their shape, so the roster says which it is.
			"lockstep": true,
		})
	return descriptors


## Two-player convenience retained for existing callers: host profile first,
## guest second, default free-for-all alliances.
static func lobby_roster_from_profiles(host_profile: Dictionary, guest_profile: Dictionary) -> Array:
	var seats: Array = []
	var profiles := [host_profile, guest_profile]
	for seat in profiles.size():
		var profile := profiles[seat] as Dictionary
		seats.append(_seat_record(
			seat,
			String(profile.get("name", "")),
			String(profile.get("faction", "men")),
			int(profile.get("color", 0)),
			seat + 1,
			bool(profile.get("ready", false)),
			true
		))
	return lobby_roster_from_seats(seats)


func build_lobby_roster() -> Array:
	## Local view of the agreed roster; [] until every occupied seat has
	## announced a profile (a placeholder seat must never reach the sim).
	if lobby_seats.size() < 2:
		return []
	for row_value in lobby_seats:
		if not bool((row_value as Dictionary).get("announced", false)):
			return []
	return lobby_roster_from_seats(
		lobby_seats, lobby_seat_heroes if custom_heroes_allowed() else {}
	)


## True when every occupied seat has announced a profile AND ticked ready.
## This is the "all seats confirm" gate on starting a match.
func all_seats_ready() -> bool:
	if lobby_seats.size() < 2:
		return false
	for row_value in lobby_seats:
		var row := row_value as Dictionary
		if not bool(row.get("announced", false)) or not bool(row.get("ready", false)):
			return false
	return true


## Seats that are still holding the match up, as a player-facing list of names
## (or "Seat N" for a seat that has not announced yet). Empty when ready.
func seats_not_ready() -> Array[String]:
	var pending: Array[String] = []
	for row_value in lobby_seats:
		var row := row_value as Dictionary
		if bool(row.get("announced", false)) and bool(row.get("ready", false)):
			continue
		var label := String(row.get("name", ""))
		if not bool(row.get("announced", false)) or label == "":
			label = "Seat %d" % (int(row.get("seat", 0)) + 1)
		pending.append(label)
	return pending


# --- lobby send --------------------------------------------------------------

func send_lobby_profile(player_name: String, faction: String, color: int, ready: bool) -> bool:
	if not handshake_complete or local_seat < 0 or not _lobby_profile_fields_valid(player_name, faction, color):
		return false
	lobby_local_profile = _canonical_profile(player_name, faction, color, ready)
	if _seats.has(local_seat):
		var record := _seats[local_seat] as Dictionary
		_seats[local_seat] = _seat_record(
			local_seat, player_name, faction, color, int(record.get("alliance", local_seat + 1)), ready, true
		)
		_rebuild_lobby_seats()
	_send_envelope({
		"kind": "lobby.profile",
		"name": player_name,
		"faction": faction,
		"color": color,
		"ready": ready,
	})
	if _is_host:
		_broadcast_seat_table()
	_connection.flush()
	return true


func send_lobby_chat(text: String) -> bool:
	if not handshake_complete or not lobby_chat_valid(text):
		return false
	_append_lobby_chat(local_team, text)
	_send_envelope({"kind": "lobby.chat", "text": text})
	_connection.flush()
	return true


func send_lobby_settings(
	map_id: String,
	resource_amount: int,
	cp_factor: float,
	build_plots: bool,
	allow_custom_heroes: bool = true,
	allow_ring_heroes: bool = true
) -> bool:
	## HOST-ONLY authoritative. A guest calling this is a no-op fail-closed.
	if not _is_host or not handshake_complete \
		or not _lobby_settings_fields_valid(map_id, resource_amount, cp_factor, build_plots):
		return false
	var seed := int(lobby_settings.get("logic_random_seed", 0))
	if seed == 0:
		seed = (Time.get_ticks_usec() ^ int(Time.get_unix_time_from_system())) & 0x7FFFFFFF
	lobby_settings = {
		"map_id": map_id,
		"resources": resource_amount,
		"cp_factor": cp_factor,
		"build_plots": build_plots,
		"allow_custom_heroes": allow_custom_heroes,
		"allow_ring_heroes": allow_ring_heroes,
		"logic_random_seed": seed,
	}
	_send_envelope({
		"kind": "lobby.settings",
		"map_id": map_id,
		"resources": resource_amount,
		"cp_factor": cp_factor,
		"build_plots": build_plots,
		"allow_custom_heroes": allow_custom_heroes,
		"allow_ring_heroes": allow_ring_heroes,
		"logic_random_seed": seed,
	})
	_connection.flush()
	return true


## HOST-ONLY. Assigns a seat's alliance (which side it fights on). Alliance is
## host-authoritative for the same reason settings are: it must be one agreed
## value, not a negotiation.
func send_lobby_seat_alliance(seat: int, alliance: int) -> bool:
	if not _is_host or not handshake_complete or not _seats.has(seat):
		return false
	if alliance < LOBBY_ALLIANCE_MIN or alliance > LOBBY_ALLIANCE_MAX:
		return false
	var record := _seats[seat] as Dictionary
	_seats[seat] = _seat_record(
		seat, String(record["name"]), String(record["faction"]), int(record["color"]),
		alliance, bool(record["ready"]), bool(record["announced"])
	)
	# Changing which side a player is on invalidates everyone's ready flag: the
	# match they agreed to is not the match they would now be starting.
	_clear_all_ready()
	_broadcast_seat_table()
	_refresh_primary_remote()
	lobby_updated.emit()
	_connection.flush()
	return true


func _clear_all_ready() -> void:
	for seat in _seats:
		var record := _seats[seat] as Dictionary
		if not bool(record.get("ready", false)):
			continue
		_seats[seat] = _seat_record(
			int(seat), String(record["name"]), String(record["faction"]), int(record["color"]),
			int(record["alliance"]), false, bool(record["announced"])
		)
		if int(seat) == local_seat and not lobby_local_profile.is_empty():
			lobby_local_profile["ready"] = false
	_rebuild_lobby_seats()


func send_lobby_launch() -> bool:
	## HOST-ONLY. Requires EVERY occupied seat to have announced and confirmed;
	## echoes the full agreed roster and settings so each guest can verify
	## byte-equality against its own derived view before accepting. The host
	## treats its own send as the accepted launch (same roster object).
	if not _is_host or not handshake_complete:
		return false
	if not all_seats_ready():
		return false
	var roster := build_lobby_roster()
	if roster.size() < 2 or roster.size() > MAX_SEATS or lobby_settings.is_empty():
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
	required_seat_count = roster.size()
	lobby_launch_accepted.emit(lobby_launch_roster.duplicate(true))
	return true


func _append_lobby_chat(team: int, text: String) -> void:
	lobby_chat_log.append({"team": team, "text": text})
	while lobby_chat_log.size() > LOBBY_CHAT_LOG_MAX:
		lobby_chat_log.pop_front()


# --- lobby receive -----------------------------------------------------------

func _receive_lobby_profile(sender_seat: int, envelope: Dictionary) -> void:
	if not handshake_complete or envelope.size() != 5 \
		or typeof(envelope.get("name")) != TYPE_STRING \
		or typeof(envelope.get("faction")) != TYPE_STRING \
		or typeof(envelope.get("color")) != TYPE_INT \
		or typeof(envelope.get("ready")) != TYPE_BOOL \
		or not _lobby_profile_fields_valid(String(envelope["name"]), String(envelope["faction"]), int(envelope["color"])):
		rejected_packets += 1
		return
	if not _seats.has(sender_seat):
		rejected_packets += 1
		return
	var record := _seats[sender_seat] as Dictionary
	_seats[sender_seat] = _seat_record(
		sender_seat,
		String(envelope["name"]),
		String(envelope["faction"]),
		int(envelope["color"]),
		int(record.get("alliance", sender_seat + 1)),
		bool(envelope["ready"]),
		true
	)
	_rebuild_lobby_seats()
	_refresh_primary_remote()
	if _is_host:
		# The host is the only writer of the table, so every other guest learns
		# about this profile from the authoritative broadcast.
		_broadcast_seat_table()
		if _connection != null:
			_connection.flush()
	lobby_updated.emit()


func _receive_lobby_seats(envelope: Dictionary) -> void:
	## Seat table is host-authoritative: the host never accepts one from a guest.
	if _is_host or not handshake_complete or envelope.size() != 2 \
		or typeof(envelope.get("seats")) != TYPE_ARRAY:
		rejected_packets += 1
		return
	var table := _validated_seat_table(envelope["seats"] as Array)
	if table.is_empty() or not table.has(local_seat):
		rejected_packets += 1
		return
	_adopt_seat_table(table)
	# The local profile mirror follows the authoritative table for our own seat.
	var own := _seats[local_seat] as Dictionary
	if bool(own.get("announced", false)):
		lobby_local_profile = _canonical_profile(
			String(own["name"]), String(own["faction"]), int(own["color"]), bool(own["ready"])
		)
	lobby_updated.emit()


func _receive_lobby_chat(sender_seat: int, envelope: Dictionary) -> void:
	if not handshake_complete or envelope.size() != 2 \
		or typeof(envelope.get("text")) != TYPE_STRING \
		or not lobby_chat_valid(String(envelope["text"])):
		rejected_packets += 1
		return
	var sender_team := team_for_seat(sender_seat)
	_append_lobby_chat(sender_team, String(envelope["text"]))
	lobby_chat_received.emit(sender_team, String(envelope["text"]))
	if _is_host:
		_broadcast({"kind": "relay.chat", "seat": sender_seat, "text": String(envelope["text"])}, sender_seat)
		if _connection != null:
			_connection.flush()
	lobby_updated.emit()


func _receive_relay_chat(envelope: Dictionary) -> void:
	if _is_host or not handshake_complete or envelope.size() != 3 \
		or typeof(envelope.get("seat")) != TYPE_INT \
		or typeof(envelope.get("text")) != TYPE_STRING \
		or not lobby_chat_valid(String(envelope["text"])):
		rejected_packets += 1
		return
	var seat := int(envelope["seat"])
	if seat == local_seat or not _seats.has(seat):
		rejected_packets += 1
		return
	var sender_team := team_for_seat(seat)
	_append_lobby_chat(sender_team, String(envelope["text"]))
	lobby_chat_received.emit(sender_team, String(envelope["text"]))
	lobby_updated.emit()


func _receive_lobby_settings(envelope: Dictionary) -> void:
	## Settings are host-authoritative: the host never accepts them from a guest.
	if _is_host or not handshake_complete or envelope.size() != 8 \
		or typeof(envelope.get("map_id")) != TYPE_STRING \
		or typeof(envelope.get("resources")) != TYPE_INT \
		or typeof(envelope.get("cp_factor")) != TYPE_FLOAT \
		or typeof(envelope.get("build_plots")) != TYPE_BOOL \
		or typeof(envelope.get("allow_custom_heroes")) != TYPE_BOOL \
		or typeof(envelope.get("allow_ring_heroes")) != TYPE_BOOL \
		or typeof(envelope.get("logic_random_seed")) != TYPE_INT \
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
		"allow_custom_heroes": bool(envelope["allow_custom_heroes"]),
		"allow_ring_heroes": bool(envelope["allow_ring_heroes"]),
		"logic_random_seed": int(envelope["logic_random_seed"]),
	}
	lobby_updated.emit()


func _receive_lobby_launch(envelope: Dictionary) -> void:
	## Guest-side final gate. The echoed roster and settings must byte-match this
	## guest's own derived view (reliable-ordered delivery guarantees every prior
	## seat-table and settings update arrived first), and every seat must be
	## confirmed ready.
	if _is_host or not handshake_complete or envelope.size() != 3 \
		or typeof(envelope.get("roster")) != TYPE_ARRAY \
		or typeof(envelope.get("settings")) != TYPE_DICTIONARY:
		rejected_packets += 1
		return
	if not all_seats_ready():
		rejected_packets += 1
		return
	var expected_roster := build_lobby_roster()
	if expected_roster.size() < 2 or lobby_settings.is_empty():
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
	required_seat_count = expected_roster.size()
	lobby_launch_accepted.emit(lobby_launch_roster.duplicate(true))
