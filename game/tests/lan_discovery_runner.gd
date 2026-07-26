extends SceneTree

## LAN / Radmin VPN discovery contract: the beacon codec is fail-closed on
## every hostile shape, a real UDP round-trip on localhost produces an entry,
## entries expire when the host stops beaconing, and the network
## classification puts 26.0.0.0/8 (Radmin VPN) in its own bucket.

const DiscoveryScript = preload("res://src/net/lan_discovery.gd")

## Deliberately not DISCOVERY_PORT: a developer running the menu next to the
## test must never make this runner flaky.
const TEST_PORT := 26114
const RECEIVE_ATTEMPTS := 2000

var passed := 0
var failed := 0
var _added: Array[String] = []
var _updated: Array[String] = []
var _removed: Array[String] = []


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	_test_codec()
	_test_classification()
	_test_local_surface()
	_test_socket_round_trip()
	await _test_flyout_integration()
	_finish()


# --- codec -------------------------------------------------------------------

func _test_codec() -> void:
	var fields := {
		"host_name": "Ancalagon",
		"map_name": "Fords of Isen II",
		"faction_name": "Men",
		"players": 1,
		"max_players": 2,
		"game_port": 26015,
	}
	var encoded: PackedByteArray = DiscoveryScript.encode_beacon(fields, 7)
	_check("beacon_fits_wire_budget",
		not encoded.is_empty() and encoded.size() <= DiscoveryScript.MAX_PACKET_BYTES,
		"size=%d" % encoded.size())
	var decoded: Dictionary = DiscoveryScript.decode_beacon(encoded)
	_check("beacon_round_trip",
		String(decoded.get("hostName", "")) == "Ancalagon"
			and String(decoded.get("mapName", "")) == "Fords of Isen II"
			and String(decoded.get("factionName", "")) == "Men"
			and int(decoded.get("players", -1)) == 1
			and int(decoded.get("maxPlayers", -1)) == 2
			and int(decoded.get("gamePort", -1)) == 26015
			and int(decoded.get("beacon", -1)) == 7,
		str(decoded))
	_check("record_carries_schema_for_a_future_lobby_service",
		String(decoded.get("schema", "")) == DiscoveryScript.SCHEMA
			and int(decoded.get("schemaVersion", -1)) == DiscoveryScript.SCHEMA_VERSION)

	# Long/hostile display text is clamped at encode time, so a peer can never
	# use its own name to push the packet past the budget or inject markup.
	var long_fields := fields.duplicate()
	long_fields["host_name"] = "[b]" + "N".repeat(400)
	long_fields["map_name"] = "M".repeat(400)
	var long_encoded: PackedByteArray = DiscoveryScript.encode_beacon(long_fields, 0)
	var long_decoded: Dictionary = DiscoveryScript.decode_beacon(long_encoded)
	_check("oversized_display_text_clamped_not_dropped",
		not long_encoded.is_empty()
			and long_encoded.size() <= DiscoveryScript.MAX_PACKET_BYTES
			and String(long_decoded.get("hostName", "")).length() <= DiscoveryScript.HOST_NAME_MAX
			and not String(long_decoded.get("hostName", "")).contains("[")
			and String(long_decoded.get("mapName", "")).length() <= DiscoveryScript.MAP_NAME_MAX,
		"size=%d name=%s" % [long_encoded.size(), String(long_decoded.get("hostName", ""))])

	_check("empty_payload_rejected", DiscoveryScript.decode_beacon(PackedByteArray()).is_empty())
	_check("malformed_json_rejected",
		DiscoveryScript.decode_beacon("{\"schema\": \"openbfme.game-beacon\"".to_utf8_buffer()).is_empty())
	_check("non_dict_json_rejected",
		DiscoveryScript.decode_beacon("[1, 2, 3]".to_utf8_buffer()).is_empty())
	_check("plain_text_rejected",
		DiscoveryScript.decode_beacon("hello?".to_utf8_buffer()).is_empty())

	var oversized := _payload_bytes({
		"schema": DiscoveryScript.SCHEMA,
		"schemaVersion": DiscoveryScript.SCHEMA_VERSION,
		"hostName": "Player", "mapName": "Fords of Isen II", "factionName": "Men",
		"players": 1, "maxPlayers": 2, "gamePort": 26015, "beacon": 1,
		"padding": "P".repeat(1024),
	})
	_check("oversized_payload_rejected",
		oversized.size() > DiscoveryScript.MAX_PACKET_BYTES
			and DiscoveryScript.decode_beacon(oversized).is_empty(),
		"size=%d" % oversized.size())

	_check("wrong_schema_rejected",
		DiscoveryScript.decode_beacon(_payload_bytes(_valid_payload({"schema": "evil.beacon"}))).is_empty())
	_check("missing_schema_rejected",
		DiscoveryScript.decode_beacon(_payload_bytes(_valid_payload({"schema": null}))).is_empty())
	_check("wrong_schema_version_rejected",
		DiscoveryScript.decode_beacon(_payload_bytes(_valid_payload({"schemaVersion": 99}))).is_empty())
	_check("non_numeric_schema_version_rejected",
		DiscoveryScript.decode_beacon(_payload_bytes(_valid_payload({"schemaVersion": "zero"}))).is_empty())
	_check("privileged_game_port_rejected",
		DiscoveryScript.decode_beacon(_payload_bytes(_valid_payload({"gamePort": 80}))).is_empty())
	_check("out_of_range_game_port_rejected",
		DiscoveryScript.decode_beacon(_payload_bytes(_valid_payload({"gamePort": 99999}))).is_empty())
	_check("impossible_player_count_rejected",
		DiscoveryScript.decode_beacon(_payload_bytes(_valid_payload({"players": 9}))).is_empty())
	_check("negative_beacon_sequence_rejected",
		DiscoveryScript.decode_beacon(_payload_bytes(_valid_payload({"beacon": -1}))).is_empty())


func _valid_payload(overrides: Dictionary) -> Dictionary:
	var payload := {
		"schema": DiscoveryScript.SCHEMA,
		"schemaVersion": DiscoveryScript.SCHEMA_VERSION,
		"hostName": "Player",
		"mapName": "Fords of Isen II",
		"factionName": "Men",
		"players": 1,
		"maxPlayers": 2,
		"gamePort": 26015,
		"beacon": 1,
	}
	for key in overrides:
		if overrides[key] == null:
			payload.erase(key)
		else:
			payload[key] = overrides[key]
	return payload


func _payload_bytes(payload: Dictionary) -> PackedByteArray:
	return JSON.stringify(payload).to_utf8_buffer()


# --- classification ----------------------------------------------------------

func _test_classification() -> void:
	_check("radmin_subnet_classified",
		DiscoveryScript.classify_address("26.0.0.1") == DiscoveryScript.NETWORK_RADMIN
			and DiscoveryScript.classify_address("26.255.13.4") == DiscoveryScript.NETWORK_RADMIN)
	_check("private_lan_classified",
		DiscoveryScript.classify_address("192.168.1.50") == DiscoveryScript.NETWORK_LAN
			and DiscoveryScript.classify_address("10.0.0.7") == DiscoveryScript.NETWORK_LAN
			and DiscoveryScript.classify_address("172.20.4.9") == DiscoveryScript.NETWORK_LAN
			and DiscoveryScript.classify_address("127.0.0.1") == DiscoveryScript.NETWORK_LAN)
	_check("public_and_garbage_classified_other",
		DiscoveryScript.classify_address("8.8.8.8") == DiscoveryScript.NETWORK_OTHER
			and DiscoveryScript.classify_address("172.32.0.1") == DiscoveryScript.NETWORK_OTHER
			and DiscoveryScript.classify_address("260.1.1.1") == DiscoveryScript.NETWORK_OTHER
			and DiscoveryScript.classify_address("") == DiscoveryScript.NETWORK_OTHER
			and DiscoveryScript.classify_address("not-an-ip") == DiscoveryScript.NETWORK_OTHER)
	_check("network_labels_are_plain_text",
		DiscoveryScript.network_label(DiscoveryScript.NETWORK_RADMIN) == "Radmin"
			and DiscoveryScript.network_label(DiscoveryScript.NETWORK_LAN) == "LAN")
	_check("discovery_port_distinct_from_default_game_port",
		DiscoveryScript.DISCOVERY_PORT != 26015)


func _test_local_surface() -> void:
	var by_network: Dictionary = DiscoveryScript.local_addresses_by_network()
	_check("local_addresses_bucketed",
		by_network.has(DiscoveryScript.NETWORK_RADMIN) and by_network.has(DiscoveryScript.NETWORK_LAN))
	var radmin: String = DiscoveryScript.local_radmin_address()
	_check("local_radmin_address_is_radmin_or_absent",
		radmin == "" or DiscoveryScript.classify_address(radmin) == DiscoveryScript.NETWORK_RADMIN,
		radmin)
	var preferred: String = DiscoveryScript.preferred_local_address()
	_check("preferred_local_address_is_usable",
		preferred.is_valid_ip_address()
			and (radmin == "" or preferred == radmin),
		preferred)
	var targets: PackedStringArray = DiscoveryScript.broadcast_targets()
	_check("broadcast_targets_include_limited_broadcast",
		targets.has("255.255.255.255") and targets.size() >= 1)


# --- live socket -------------------------------------------------------------

func _test_socket_round_trip() -> void:
	var browser = DiscoveryScript.new()
	browser.game_added.connect(func(key: String, _record: Dictionary) -> void: _added.append(key))
	browser.game_updated.connect(func(key: String, _record: Dictionary) -> void: _updated.append(key))
	browser.game_removed.connect(func(key: String) -> void: _removed.append(key))
	var bind_error: Error = browser.start_browsing(TEST_PORT)
	_check("browser_binds_discovery_port", bind_error == OK and browser.browsing, "error=%d" % bind_error)
	if bind_error != OK:
		browser.close()
		return

	var sender := PacketPeerUDP.new()
	sender.set_dest_address("127.0.0.1", TEST_PORT)
	var fields := {
		"host_name": "Ancalagon",
		"map_name": "Fords of Isen II",
		"faction_name": "Men",
		"players": 1,
		"max_players": 2,
		"game_port": 26015,
	}
	sender.put_packet(DiscoveryScript.encode_beacon(fields, 1))
	var key := "127.0.0.1:26015"
	_check("beacon_received_and_listed", _await_entry(browser, key),
		"added=%s rejected=%d" % [str(_added), browser.rejected_packets])
	var record: Dictionary = browser.entry(key)
	_check("entry_carries_host_facts",
		String(record.get("hostName", "")) == "Ancalagon"
			and String(record.get("address", "")) == "127.0.0.1"
			and int(record.get("gamePort", 0)) == 26015
			and String(record.get("network", "")) == DiscoveryScript.NETWORK_LAN
			and int(record.get("beacon", -1)) == 1,
		str(record))
	_check("added_signal_fired_once", _added.size() == 1 and _added[0] == key, str(_added))

	# A second beacon updates in place (same sender + game port = same key).
	sender.put_packet(DiscoveryScript.encode_beacon(fields, 2))
	var updated := false
	for _attempt in range(RECEIVE_ATTEMPTS):
		browser.poll(0.0)
		if not _updated.is_empty():
			updated = true
			break
		OS.delay_msec(1)
	_check("repeat_beacon_updates_existing_entry",
		updated and browser.entries().size() == 1
			and int(browser.entry(key).get("beacon", -1)) == 2,
		"updated=%s count=%d" % [str(updated), browser.entries().size()])

	# Junk and hostile sources never reach the list and never spam the console.
	var rejected_before: int = browser.rejected_packets
	sender.put_packet("{ not json".to_utf8_buffer())
	sender.put_packet(_payload_bytes(_valid_payload({"schema": "evil.beacon"})))
	var junk_seen := false
	for _attempt in range(RECEIVE_ATTEMPTS):
		browser.poll(0.0)
		if browser.rejected_packets >= rejected_before + 2:
			junk_seen = true
			break
		OS.delay_msec(1)
	_check("junk_packets_counted_and_dropped",
		junk_seen and browser.entries().size() == 1,
		"rejected=%d count=%d" % [browser.rejected_packets, browser.entries().size()])

	var foreign_before: int = browser.rejected_packets
	browser._ingest("8.8.8.8", DiscoveryScript.encode_beacon(fields, 3))
	_check("packet_from_unexpected_source_dropped",
		browser.rejected_packets == foreign_before + 1 and browser.entries().size() == 1)

	# Expiry: the internal clock is fed by poll(delta), so a headless test can
	# fast-forward past ENTRY_TIMEOUT_MSEC without sleeping.
	browser.poll((DiscoveryScript.ENTRY_TIMEOUT_MSEC + 1000.0) / 1000.0)
	_check("entry_expires_after_timeout",
		browser.entries().is_empty() and _removed.size() == 1 and _removed[0] == key,
		"removed=%s" % str(_removed))

	# Advertising is a no-op teardown when it was never started, and the browse
	# socket releases cleanly so a second flyout visit can re-bind.
	browser.stop_browsing()
	_check("stop_browsing_releases_port", not browser.browsing)
	var rebind = DiscoveryScript.new()
	var rebind_error: Error = rebind.start_browsing(TEST_PORT)
	_check("discovery_port_rebindable_after_close", rebind_error == OK, "error=%d" % rebind_error)
	rebind.close()
	browser.close()
	sender.close()


# --- flyout integration ------------------------------------------------------

## The NETWORK flyout must list what discovery finds and, on selection, fill
## the SAME manual fields the existing join path already reads - discovery is
## additive, never a second code path into the session.
func _test_flyout_integration() -> void:
	var packed: PackedScene = load("res://scenes/boot.tscn")
	var menu := packed.instantiate()
	root.add_child(menu)
	await process_frame
	await process_frame
	var flyout = menu.get_node_or_null("Center/MultiplayerFlyout")
	if flyout == null or flyout.discovery == null:
		_check("flyout_owns_a_discovery_module", false)
		menu.queue_free()
		await process_frame
		return
	_check("flyout_owns_a_discovery_module", true)
	_check("flyout_shows_a_local_address",
		flyout.local_address_edit != null
			and not flyout.local_address_edit.editable
			and flyout.local_address_edit.text.is_valid_ip_address(),
		flyout.local_address_edit.text if flyout.local_address_edit != null else "<null>")
	_check("radmin_hint_is_plain_text_only",
		flyout.network_hint_label != null
			and flyout.network_hint_label.text.contains("Radmin")
			and not flyout.network_hint_label.text.contains("["),
		flyout.network_hint_label.text if flyout.network_hint_label != null else "<null>")

	# Feed a Radmin-subnet beacon straight into the flyout's own module: the
	# row must appear and selecting it must fill Host IP + Port.
	var fields := {
		"host_name": "Radmin Host",
		"map_name": "Fords of Isen II",
		"faction_name": "Men",
		"players": 1,
		"max_players": 2,
		"game_port": 26015,
	}
	flyout.discovery._ingest("26.13.44.7", DiscoveryScript.encode_beacon(fields, 1))
	await process_frame
	var row_text: String = flyout.games_list.get_item_text(0) if flyout.games_list.item_count > 0 else ""
	_check("discovered_game_appears_in_the_list",
		flyout.games_list.item_count == 1
			and row_text.contains("Radmin Host")
			and row_text.contains("Fords of Isen II")
			and row_text.contains("1/2")
			and row_text.contains("Radmin")
			and row_text.contains("26.13.44.7"),
		row_text)

	flyout.join_address_edit.text = "127.0.0.1"
	flyout.join_port_edit.text = "1"
	flyout.games_list.select(0)
	flyout.games_list.item_selected.emit(0)
	_check("selecting_a_game_fills_the_manual_join_fields",
		flyout.join_address_edit.text == "26.13.44.7" and flyout.join_port_edit.text == "26015",
		"%s:%s" % [flyout.join_address_edit.text, flyout.join_port_edit.text])
	_check("manual_entry_still_validates_unchanged",
		flyout.validate_address("192.168.1.50") == ""
			and flyout.validate_address("not-an-ip") != ""
			and flyout.validate_port(str(DiscoveryScript.DISCOVERY_PORT)) == "",
		"manual paths must survive the additive list")

	menu.queue_free()
	await process_frame


func _await_entry(browser, key: String) -> bool:
	for _attempt in range(RECEIVE_ATTEMPTS):
		browser.poll(0.0)
		if not browser.entry(key).is_empty():
			return true
		OS.delay_msec(1)
	return false


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("LAN_DISCOVERY PASS %s" % name)
	else:
		failed += 1
		printerr("LAN_DISCOVERY FAIL %s%s" % [name, " (%s)" % detail if detail != "" else ""])


func _finish() -> void:
	print("LAN_DISCOVERY_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)
