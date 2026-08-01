extends RefCounted

## LAN / Radmin VPN game discovery.
##
## Hosts broadcast a small UDP beacon once a second; browsers bind the same
## port, collect the beacons and expire the ones that stop arriving. The goal
## is that a player who has joined a Radmin VPN network (a free product that
## puts everyone on one virtual LAN, subnet 26.0.0.0/8) simply SEES the hosted
## games instead of typing an IP address by hand.
##
## Transport choice is deliberately hidden behind a stable RECORD SHAPE: the
## dicts this module hands the UI carry `schema`/`schemaVersion`, so a future
## remote lobby service can return the exact same records and the UI never
## changes. Everything on the wire is untrusted: decode_beacon() validates
## fail-closed and returns {} for anything it does not fully understand, and
## nothing here ever pushes an error (a hostile or broken peer must not be able
## to spam the console).

## Beacon/record schema. Bump SCHEMA_VERSION only for incompatible changes;
## a browser drops records it does not recognise rather than guessing.
const SCHEMA := "openbfme.game-beacon"
const SCHEMA_VERSION := 0

## Discovery port. The default game port is 26015 (multiplayer_flyout.gd
## DEFAULT_PORT), so discovery sits directly below it at 26014: one contiguous
## 26014-26015 range covers both in a firewall rule, it can never collide with
## the ENet socket (hosts may move the game port, but discovery is fixed), and
## the pair stays inside the unassigned 26000-26100 block.
const DISCOVERY_PORT := 26014

## Wire limits. 512 bytes keeps a beacon inside any plausible MTU and bounds
## the work a flood of junk can cause.
const MAX_PACKET_BYTES := 512
const MAX_PACKETS_PER_POLL := 64
const MAX_ENTRIES := 64
const HOST_NAME_MAX := 24
const MAP_NAME_MAX := 40
const FACTION_NAME_MAX := 24
const PORT_MIN := 1024
const PORT_MAX := 65535
const MAX_PLAYER_SLOTS := 8

const BEACON_INTERVAL_MSEC := 1000.0
const ENTRY_TIMEOUT_MSEC := 5000.0

## Network buckets a discovered (or local) address falls into. "radmin" is the
## 26.0.0.0/8 virtual LAN; "lan" is RFC1918 plus loopback (same machine counts
## as local); anything else is "other" and is never trusted as a beacon source.
const NETWORK_RADMIN := "radmin"
const NETWORK_LAN := "lan"
const NETWORK_OTHER := "other"

const RADMIN_SUBNET_LABEL := "26.0.0.0/8"

signal game_added(key: String, record: Dictionary)
signal game_updated(key: String, record: Dictionary)
signal game_removed(key: String)

var browsing := false
var advertising := false
var browse_error := OK
var advertise_error := OK
var rejected_packets := 0

var _listen_socket: PacketPeerUDP
var _send_socket: PacketPeerUDP
var _discovery_port := DISCOVERY_PORT
var _entries: Dictionary = {}
## Monotonic per-instance beacon sequence; a browser can use it to notice a
## host that restarted (sequence went backwards) without any extra state.
var _beacon_sequence := 0
var _advertise_fields: Dictionary = {}
var _advertise_targets: PackedStringArray = PackedStringArray()
var _next_beacon_msec := 0.0
## Internal clock fed by poll(delta), never Time.*: expiry and the beacon
## cadence stay deterministic and a headless test can fast-forward by calling
## poll(6.0) instead of sleeping.
var _clock_msec := 0.0


# --- classification ----------------------------------------------------------

## "radmin" | "lan" | "other". Never errors; a malformed address is "other".
static func classify_address(address: String) -> String:
	var trimmed := address.strip_edges()
	if not trimmed.is_valid_ip_address() or not trimmed.contains("."):
		return NETWORK_OTHER
	var octets := trimmed.split(".")
	if octets.size() != 4:
		return NETWORK_OTHER
	var first := int(octets[0])
	var second := int(octets[1])
	if first == 26:
		return NETWORK_RADMIN
	if first == 10 or first == 127:
		return NETWORK_LAN
	if first == 192 and second == 168:
		return NETWORK_LAN
	if first == 172 and second >= 16 and second <= 31:
		return NETWORK_LAN
	if first == 169 and second == 254:
		return NETWORK_LAN
	return NETWORK_OTHER


static func network_label(network: String) -> String:
	match network:
		NETWORK_RADMIN:
			return "Radmin"
		NETWORK_LAN:
			return "LAN"
		_:
			return "Other"


## Local IPv4 addresses split by bucket: {"radmin": [...], "lan": [...]}.
## Loopback is dropped here (it is never the address to read out to a peer).
static func local_addresses_by_network() -> Dictionary:
	var result := {NETWORK_RADMIN: PackedStringArray(), NETWORK_LAN: PackedStringArray()}
	for address in IP.get_local_addresses():
		var text := String(address)
		if text.begins_with("127."):
			continue
		var network := classify_address(text)
		if network == NETWORK_RADMIN or network == NETWORK_LAN:
			var bucket: PackedStringArray = result[network]
			if not bucket.has(text):
				bucket.append(text)
				result[network] = bucket
	return result


## The Radmin VPN address of this machine, or "" when Radmin is not running.
static func local_radmin_address() -> String:
	var radmin: PackedStringArray = local_addresses_by_network()[NETWORK_RADMIN]
	return radmin[0] if not radmin.is_empty() else ""


## The address a peer should actually be given: Radmin first (it reaches
## players over the internet), then the LAN address, then loopback.
static func preferred_local_address() -> String:
	var by_network := local_addresses_by_network()
	var radmin: PackedStringArray = by_network[NETWORK_RADMIN]
	if not radmin.is_empty():
		return radmin[0]
	var lan: PackedStringArray = by_network[NETWORK_LAN]
	if not lan.is_empty():
		return lan[0]
	return "127.0.0.1"


## Every address a beacon should be sent to: the limited broadcast plus one
## directed broadcast per local interface. Radmin's virtual adapter does not
## always carry 255.255.255.255, so 26.x gets its own 26.255.255.255 (/8, the
## documented Radmin subnet); RFC1918 interfaces are assumed /24, which is the
## overwhelmingly common home-router case and costs one extra packet when wrong.
static func broadcast_targets() -> PackedStringArray:
	var targets := PackedStringArray(["255.255.255.255"])
	for address in IP.get_local_addresses():
		var text := String(address)
		var network := classify_address(text)
		if network != NETWORK_RADMIN and network != NETWORK_LAN:
			continue
		if text.begins_with("127."):
			continue
		var octets := text.split(".")
		if octets.size() != 4:
			continue
		var directed := ""
		if network == NETWORK_RADMIN:
			directed = "%s.255.255.255" % octets[0]
		else:
			directed = "%s.%s.%s.255" % [octets[0], octets[1], octets[2]]
		if not targets.has(directed):
			targets.append(directed)
	return targets


# --- beacon codec ------------------------------------------------------------

## Canonical outbound payload. Caller supplies host_name/map_name/faction_name/
## players/max_players/game_port; everything is clamped here so a long player
## name can never push the packet over MAX_PACKET_BYTES.
static func build_beacon(fields: Dictionary, sequence: int) -> Dictionary:
	return {
		"schema": SCHEMA,
		"schemaVersion": SCHEMA_VERSION,
		"hostName": _clean_text(String(fields.get("host_name", "")), HOST_NAME_MAX, "Player"),
		"mapName": _clean_text(String(fields.get("map_name", "")), MAP_NAME_MAX, "Unknown map"),
		"factionName": _clean_text(String(fields.get("faction_name", "")), FACTION_NAME_MAX, "-"),
		"players": clampi(int(fields.get("players", 1)), 0, MAX_PLAYER_SLOTS),
		"maxPlayers": clampi(int(fields.get("max_players", 2)), 1, MAX_PLAYER_SLOTS),
		"gamePort": clampi(int(fields.get("game_port", 0)), PORT_MIN, PORT_MAX),
		"beacon": maxi(0, sequence),
	}


## Empty PackedByteArray when the payload will not fit; callers must not send.
static func encode_beacon(fields: Dictionary, sequence: int) -> PackedByteArray:
	var bytes := JSON.stringify(build_beacon(fields, sequence)).to_utf8_buffer()
	if bytes.size() > MAX_PACKET_BYTES:
		return PackedByteArray()
	return bytes


## {} for ANY payload that is not a fully valid beacon: oversized, non-UTF8,
## non-JSON, not a dict, wrong schema, wrong version, or out-of-range fields.
## Never pushes an error - this runs on unauthenticated wire data.
static func decode_beacon(bytes: PackedByteArray) -> Dictionary:
	if bytes.is_empty() or bytes.size() > MAX_PACKET_BYTES:
		return {}
	var text := bytes.get_string_from_utf8()
	if text.strip_edges().is_empty():
		return {}
	# JSON.parse_string() ERR_PRINTs on bad input; the instance parser returns a
	# code silently, which is what unauthenticated wire data requires.
	var json := JSON.new()
	if json.parse(text) != OK:
		return {}
	if typeof(json.data) != TYPE_DICTIONARY:
		return {}
	var payload := json.data as Dictionary
	if String(payload.get("schema", "")) != SCHEMA:
		return {}
	if typeof(payload.get("schemaVersion")) != TYPE_FLOAT and typeof(payload.get("schemaVersion")) != TYPE_INT:
		return {}
	if int(payload.get("schemaVersion", -1)) != SCHEMA_VERSION:
		return {}
	var game_port := int(payload.get("gamePort", 0))
	if game_port < PORT_MIN or game_port > PORT_MAX:
		return {}
	var max_players := int(payload.get("maxPlayers", 0))
	if max_players < 1 or max_players > MAX_PLAYER_SLOTS:
		return {}
	var players := int(payload.get("players", 0))
	if players < 0 or players > max_players:
		return {}
	var sequence := int(payload.get("beacon", -1))
	if sequence < 0:
		return {}
	return {
		"schema": SCHEMA,
		"schemaVersion": SCHEMA_VERSION,
		"hostName": _clean_text(String(payload.get("hostName", "")), HOST_NAME_MAX, "Player"),
		"mapName": _clean_text(String(payload.get("mapName", "")), MAP_NAME_MAX, "Unknown map"),
		"factionName": _clean_text(String(payload.get("factionName", "")), FACTION_NAME_MAX, "-"),
		"players": players,
		"maxPlayers": max_players,
		"gamePort": game_port,
		"beacon": sequence,
	}


## Printable-ASCII, length-clamped, never empty. Remote text reaches a Label,
## so control characters and BBCode-ish noise are stripped at the boundary.
static func _clean_text(text: String, limit: int, fallback: String) -> String:
	var cleaned := ""
	for index in mini(text.length(), limit * 4):
		var code := text.unicode_at(index)
		if code >= 32 and code < 127 and code != 91:  # 91 == '['
			cleaned += char(code)
	cleaned = cleaned.strip_edges()
	if cleaned.length() > limit:
		cleaned = cleaned.substr(0, limit)
	return cleaned if cleaned != "" else fallback


# --- host side ---------------------------------------------------------------

## Starts (or re-targets) the once-a-second beacon. Returns OK, or the socket
## error; a failure leaves advertising false and is never fatal to hosting.
func start_advertising(fields: Dictionary, port: int = DISCOVERY_PORT) -> Error:
	_advertise_fields = fields.duplicate(true)
	if advertising:
		# Re-targeting an already-running beacon: keep the sequence monotonic.
		_next_beacon_msec = _clock_msec
		return OK
	_discovery_port = port
	_send_socket = PacketPeerUDP.new()
	_send_socket.set_broadcast_enabled(true)
	_advertise_targets = broadcast_targets()
	advertising = true
	advertise_error = OK
	_next_beacon_msec = _clock_msec
	return OK


func stop_advertising() -> void:
	advertising = false
	if _send_socket != null:
		_send_socket.close()
		_send_socket = null


## Updates the advertised payload without restarting the beacon (player count
## changing as the lobby fills, host renaming themselves, map switch).
func update_advertisement(fields: Dictionary) -> void:
	_advertise_fields = fields.duplicate(true)


# --- browser side ------------------------------------------------------------

## Binds the discovery port. A bind failure (another OpenBFME window on this
## machine already listening, or a locked-down firewall) is reported through
## browse_error and leaves browsing false - manual IP entry must keep working.
func start_browsing(port: int = DISCOVERY_PORT) -> Error:
	if browsing:
		return OK
	_discovery_port = port
	_listen_socket = PacketPeerUDP.new()
	var error := _listen_socket.bind(port, "*", MAX_PACKET_BYTES * MAX_PACKETS_PER_POLL)
	if error != OK:
		_listen_socket = null
		browse_error = error
		return error
	browsing = true
	browse_error = OK
	return OK


func stop_browsing() -> void:
	browsing = false
	if _listen_socket != null:
		_listen_socket.close()
		_listen_socket = null
	var keys := _entries.keys()
	_entries.clear()
	for key in keys:
		game_removed.emit(String(key))


## Discovered games, newest beacon first is not meaningful - sorted by network
## (Radmin before LAN) then host name so the list does not jitter.
func entries() -> Array:
	var values: Array = []
	for key in _entries:
		values.append((_entries[key] as Dictionary).duplicate(true))
	values.sort_custom(func(a: Dictionary, b: Dictionary) -> bool:
		var a_rank := 0 if String(a.get("network", "")) == NETWORK_RADMIN else 1
		var b_rank := 0 if String(b.get("network", "")) == NETWORK_RADMIN else 1
		if a_rank != b_rank:
			return a_rank < b_rank
		if String(a.get("hostName", "")) != String(b.get("hostName", "")):
			return String(a.get("hostName", "")) < String(b.get("hostName", ""))
		return String(a.get("key", "")) < String(b.get("key", ""))
	)
	return values


func entry(key: String) -> Dictionary:
	return (_entries.get(key, {}) as Dictionary).duplicate(true)


## Drives both sides. delta is seconds; the caller is a _process (or a test
## fast-forwarding time).
func poll(delta: float) -> void:
	_clock_msec += maxf(0.0, delta) * 1000.0
	_drain_socket()
	_expire_entries()
	_emit_beacon_if_due()


func close() -> void:
	stop_advertising()
	stop_browsing()


# --- internals ---------------------------------------------------------------

func _drain_socket() -> void:
	if not browsing or _listen_socket == null:
		return
	var budget := MAX_PACKETS_PER_POLL
	while budget > 0 and _listen_socket.get_available_packet_count() > 0:
		budget -= 1
		var bytes := _listen_socket.get_packet()
		var address := _listen_socket.get_packet_ip()
		_ingest(address, bytes)


## Applies one received packet. Everything that fails validation is counted and
## dropped silently: a peer on a hostile network must not be able to fill the
## console or the entry table.
func _ingest(address: String, bytes: PackedByteArray) -> void:
	var network := classify_address(address)
	if network == NETWORK_OTHER:
		# Beacons only ever legitimately arrive from the Radmin subnet or a
		# private LAN; anything else on this port is not ours.
		rejected_packets += 1
		return
	var record := decode_beacon(bytes)
	if record.is_empty():
		rejected_packets += 1
		return
	var key := "%s:%d" % [address, int(record["gamePort"])]
	var known: bool = _entries.has(key)
	if not known and _entries.size() >= MAX_ENTRIES:
		rejected_packets += 1
		return
	record["key"] = key
	record["address"] = address
	record["network"] = network
	record["lastSeenMsec"] = _clock_msec
	_entries[key] = record
	if known:
		game_updated.emit(key, record.duplicate(true))
	else:
		game_added.emit(key, record.duplicate(true))


func _expire_entries() -> void:
	var stale: Array[String] = []
	for key in _entries:
		var record := _entries[key] as Dictionary
		if _clock_msec - float(record.get("lastSeenMsec", 0.0)) > ENTRY_TIMEOUT_MSEC:
			stale.append(String(key))
	for key in stale:
		_entries.erase(key)
		game_removed.emit(key)


func _emit_beacon_if_due() -> void:
	if not advertising or _send_socket == null:
		return
	if _clock_msec < _next_beacon_msec:
		return
	_next_beacon_msec = _clock_msec + BEACON_INTERVAL_MSEC
	var payload := encode_beacon(_advertise_fields, _beacon_sequence)
	if payload.is_empty():
		# Payload could not be made to fit; skip this beat rather than sending
		# something a browser would have to reject anyway.
		advertise_error = ERR_INVALID_DATA
		return
	_beacon_sequence += 1
	for target in _advertise_targets:
		if _send_socket.set_dest_address(String(target), _discovery_port) != OK:
			continue
		var error := _send_socket.put_packet(payload)
		if error != OK:
			advertise_error = error
