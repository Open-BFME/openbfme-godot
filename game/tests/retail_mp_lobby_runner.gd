extends SceneTree

## Pre-game multiplayer lobby protocol contract, exercised over a real ENet
## loopback pair (same technique as retail_lockstep_network_runner) with NO
## simulation attached — the lobby runs before a sim exists. Covers: handshake
## + default settings seeding, profile exchange, faction-change propagation,
## bounded chat, host-authoritative settings/launch (guest spoofs rejected),
## ready-up gating, byte-identical launch descriptor lists on both peers, and
## leave/disconnect cleanup.

const PUMP_BUDGET := 20000

var passed := 0
var failed := 0
var host_session
var guest_session


const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
# Turns a GDScript runtime error inside `_run` — which unwinds past every
# `quit()` and would otherwise leave this headless process idling forever —
# into a loud non-zero exit. See tests/runner_watchdog.gd.
var _runner_watchdog := RunnerWatchdogScript.new()


func _initialize() -> void:
	_runner_watchdog.start(self, "RETAIL_MP_LOBBY_RUNNER")
	call_deferred("_run")


func _run() -> void:
	var SessionScript = load("res://src/retail_slice/retail_lockstep_session.gd")
	host_session = SessionScript.new()
	guest_session = SessionScript.new()
	var host_error: Error = host_session.host(0)
	var join_error: Error = guest_session.join("127.0.0.1", host_session.bound_port) if host_error == OK else ERR_CANT_CONNECT
	var handshake_ok: bool = host_error == OK and join_error == OK \
		and _pump_until(func() -> bool: return host_session.handshake_complete and guest_session.handshake_complete)
	var defaults: Dictionary = SessionScript.lobby_default_settings()
	_check("lobby_handshake_and_default_settings", handshake_ok \
		and var_to_bytes(host_session.lobby_settings) == var_to_bytes(defaults) \
		and var_to_bytes(guest_session.lobby_settings) == var_to_bytes(defaults))
	if not handshake_ok:
		await _finish()
		return

	# --- profile exchange ----------------------------------------------------
	var host_announced: bool = host_session.send_lobby_profile("Aragorn", "men", 0, false)
	var guest_announced: bool = guest_session.send_lobby_profile("Witch King", "mordor", 1, false)
	var profiles_seen: bool = _pump_until(func() -> bool: return not host_session.lobby_remote_profile.is_empty() and not guest_session.lobby_remote_profile.is_empty())
	_check("profile_exchange_both_names_factions", host_announced and guest_announced and profiles_seen \
		and String(host_session.lobby_remote_profile.get("name", "")) == "Witch King" \
		and String(host_session.lobby_remote_profile.get("faction", "")) == "mordor" \
		and String(guest_session.lobby_remote_profile.get("name", "")) == "Aragorn" \
		and String(guest_session.lobby_remote_profile.get("faction", "")) == "men")

	# The lobby surface, not a direct protocol call: one selected hero is the
	# whole announcement, '-' is an explicit empty announcement, and restoring
	# the pick makes that same one document part of the byte-equal launch roster.
	var LobbyScript = load("res://src/ui/multiplayer_lobby.gd")
	var hero_panel: Panel = LobbyScript.new()
	hero_panel.size = Vector2(1000, 640)
	root.add_child(hero_panel)
	await process_frame
	hero_panel.open(host_session, true, "Aragorn")
	var hero_picker = hero_panel.get("hero_opt")
	var picked_profile := {
		"schema": "openbfme.cah-profile", "schemaVersion": 0,
		"heroId": "0123456789abcdef01234567", "name": "Captain Test",
		"classIndex": 0, "subClassIndex": 0, "attributes": {}, "appearance": {},
		"powers": [], "awards": [], "trackingStats": {}, "systemDescriptorSha256": "",
	}
	var picked_document: String = SessionScript.canonical_hero_document(picked_profile)
	var announce_one := false
	var announce_none := false
	if hero_picker != null:
		hero_picker.add_item("Captain Test")
		hero_picker.set_item_metadata(hero_picker.item_count - 1, picked_profile)
		hero_picker.select(hero_picker.item_count - 1)
		hero_panel._announce_created_heroes()
		announce_one = _pump_until(func() -> bool:
			return guest_session.heroes_for_seat(host_session.local_seat) == [picked_document])
	_check("lobby_picker_announces_exactly_one_selected_hero", announce_one,
		str(guest_session.lobby_seat_heroes))
	var disabled_cleared := false
	if hero_picker != null:
		hero_panel.custom_heroes_opt.select(1)
		hero_panel._on_custom_heroes_selected(1)
		disabled_cleared = _pump_until(func() -> bool:
			return guest_session.heroes_for_seat(host_session.local_seat).is_empty()) \
			and hero_picker.disabled
	_check("custom_heroes_disabled_forces_an_empty_announcement", disabled_cleared)
	if hero_picker != null:
		var disabled_settings := defaults.duplicate(true)
		disabled_settings["allow_custom_heroes"] = false
		host_session.lobby_settings = disabled_settings.duplicate(true)
		hero_panel._apply_settings_to_controls(disabled_settings)
		_pump_until(func() -> bool:
			return guest_session.heroes_for_seat(host_session.local_seat).is_empty())
		var enabled_settings := defaults.duplicate(true)
		enabled_settings["allow_custom_heroes"] = true
		host_session.lobby_settings = enabled_settings.duplicate(true)
		hero_panel._apply_settings_to_controls(enabled_settings)
		var reenabled_announced := _pump_until(func() -> bool:
			return guest_session.heroes_for_seat(host_session.local_seat) == [picked_document])
		_check("custom_heroes_reenabled_reannounces_the_visible_picker_selection",
			reenabled_announced and not hero_picker.disabled)
	if hero_picker != null:
		hero_picker.select(0)
		hero_panel._announce_created_heroes()
		announce_none = _pump_until(func() -> bool:
			return guest_session.heroes_for_seat(host_session.local_seat).is_empty())
	_check("lobby_picker_dash_announces_no_heroes", announce_none,
		str(guest_session.lobby_seat_heroes))
	if hero_picker != null:
		hero_picker.select(hero_picker.item_count - 1)
		hero_panel._announce_created_heroes()
		_pump_until(func() -> bool:
			return guest_session.heroes_for_seat(host_session.local_seat) == [picked_document])
	guest_session.send_lobby_heroes([])
	_pump_until(func() -> bool: return host_session.lobby_seat_heroes.has(guest_session.local_seat))

	guest_session.send_lobby_profile("Witch King", "wild", 5, false)
	var faction_changed: bool = _pump_until(func() -> bool: return String(host_session.lobby_remote_profile.get("faction", "")) == "wild")
	_check("guest_faction_change_propagates", faction_changed \
		and int(host_session.lobby_remote_profile.get("color", -1)) == 5)

	# The shared army ladder covers the six BFME2 factions plus Angmar, and an
	# Angmar profile round-trips like any other ladder entry.
	var expected_ladder: Array[String] = ["men", "elves", "dwarves", "isengard", "mordor", "wild", "angmar"]
	_check("faction_ladder_is_seven_wide", SessionScript.LOBBY_FACTION_IDS == expected_ladder,
		str(SessionScript.LOBBY_FACTION_IDS))
	guest_session.send_lobby_profile("Witch King", "angmar", 5, false)
	var angmar_propagated: bool = _pump_until(func() -> bool: return String(host_session.lobby_remote_profile.get("faction", "")) == "angmar")
	_check("angmar_profile_propagates", angmar_propagated)
	guest_session.send_lobby_profile("Witch King", "wild", 5, false)
	_pump_until(func() -> bool: return String(host_session.lobby_remote_profile.get("faction", "")) == "wild")

	# Invalid profiles fail closed on both the send and the receive side.
	var bad_send_refused: bool = not host_session.send_lobby_profile("x".repeat(25), "men", 0, false) \
		and not host_session.send_lobby_profile("Aragorn", "numenor", 0, false) \
		and not host_session.send_lobby_profile("Aragorn", "men", 99, false)
	var rejects_before: int = host_session.rejected_packets
	guest_session._send_envelope({"kind": "lobby.profile", "name": "y".repeat(30), "faction": "men", "color": 0, "ready": false})
	guest_session._connection.flush()
	var bad_profile_rejected: bool = _pump_until(func() -> bool: return host_session.rejected_packets > rejects_before)
	_check("invalid_profiles_fail_closed", bad_send_refused and bad_profile_rejected \
		and String(host_session.lobby_remote_profile.get("name", "")) == "Witch King")

	# --- chat ------------------------------------------------------------------
	var chat_sent: bool = host_session.send_lobby_chat("gl hf")
	var chat_arrived: bool = _pump_until(func() -> bool: return guest_session.lobby_chat_log.size() >= 1)
	var chat_entry: Dictionary = guest_session.lobby_chat_log[0] if chat_arrived else {}
	_check("chat_round_trips", chat_sent and chat_arrived \
		and String(chat_entry.get("text", "")) == "gl hf" and int(chat_entry.get("team", -1)) == 0 \
		and host_session.lobby_chat_log.size() == 1)

	var oversize := "a".repeat(201)
	var oversize_refused: bool = not guest_session.send_lobby_chat(oversize) and not guest_session.send_lobby_chat("")
	var chat_rejects_before: int = host_session.rejected_packets
	guest_session._send_envelope({"kind": "lobby.chat", "text": oversize})
	guest_session._connection.flush()
	var oversize_spoof_rejected: bool = _pump_until(func() -> bool: return host_session.rejected_packets > chat_rejects_before)
	_check("oversize_chat_rejected", oversize_refused and oversize_spoof_rejected \
		and host_session.lobby_chat_log.size() == 1)

	# --- host-authoritative settings ------------------------------------------
	var guest_settings_refused: bool = not guest_session.send_lobby_settings("bfme2.map.rivendell", 2000, 2.0, true)
	var settings_rejects_before: int = host_session.rejected_packets
	guest_session._send_envelope({"kind": "lobby.settings", "map_id": "bfme2.map.rivendell", "resources": 2000, "cp_factor": 2.0, "build_plots": true})
	guest_session._connection.flush()
	var settings_spoof_rejected: bool = _pump_until(func() -> bool: return host_session.rejected_packets > settings_rejects_before)
	_check("guest_settings_spoof_rejected", guest_settings_refused and settings_spoof_rejected \
		and var_to_bytes(host_session.lobby_settings) == var_to_bytes(defaults))

	var launch_rejects_before: int = host_session.rejected_packets
	guest_session._send_envelope({"kind": "lobby.launch", "roster": [], "settings": {}})
	guest_session._connection.flush()
	var launch_spoof_rejected: bool = _pump_until(func() -> bool: return host_session.rejected_packets > launch_rejects_before)
	_check("guest_launch_spoof_rejected", launch_spoof_rejected and not host_session.lobby_launch_received)

	var bad_settings_refused: bool = not host_session.send_lobby_settings("bfme2.map.helms-deep", 1200, 1.0, false) \
		and not host_session.send_lobby_settings("bfme2.map.rivendell", 1234, 1.0, false) \
		and not host_session.send_lobby_settings("bfme2.map.rivendell", 1200, 3.0, false)
	var settings_sent: bool = host_session.send_lobby_settings("bfme2.map.rivendell", 2000, 2.0, true)
	var settings_arrived: bool = _pump_until(func() -> bool: return String(guest_session.lobby_settings.get("map_id", "")) == "bfme2.map.rivendell")
	_check("host_settings_propagate_validated", bad_settings_refused and settings_sent and settings_arrived \
		and int(guest_session.lobby_settings.get("resources", 0)) == 2000 \
		and float(guest_session.lobby_settings.get("cp_factor", 0.0)) == 2.0 \
		and bool(guest_session.lobby_settings.get("build_plots", false)) \
		and var_to_bytes(guest_session.lobby_settings) == var_to_bytes(host_session.lobby_settings))

	# --- ready gating + launch -------------------------------------------------
	var premature_launch_refused: bool = not host_session.send_lobby_launch()
	var guest_launch_refused: bool = not guest_session.send_lobby_launch()
	host_session.send_lobby_profile("Aragorn", "men", 0, true)
	guest_session.send_lobby_profile("Witch King", "wild", 5, true)
	var both_ready: bool = _pump_until(func() -> bool: return bool(host_session.lobby_remote_profile.get("ready", false)) and bool(guest_session.lobby_remote_profile.get("ready", false)))
	_check("launch_gated_on_ready", premature_launch_refused and guest_launch_refused and both_ready)

	var launch_sent: bool = host_session.send_lobby_launch()
	var guest_accepted: bool = _pump_until(func() -> bool: return guest_session.lobby_launch_received)
	var host_roster_bytes: PackedByteArray = var_to_bytes(host_session.lobby_launch_roster)
	var guest_roster_bytes: PackedByteArray = var_to_bytes(guest_session.lobby_launch_roster)
	var expected_roster: Array = SessionScript.lobby_roster_from_seats(
		host_session.lobby_seats, host_session.lobby_seat_heroes)
	var roster_shape_ok: bool = host_session.lobby_launch_roster.size() == 2 \
		and int((host_session.lobby_launch_roster[0] as Dictionary).get("team", -1)) == 0 \
		and int((host_session.lobby_launch_roster[1] as Dictionary).get("team", -1)) == 1 \
		and String((host_session.lobby_launch_roster[0] as Dictionary).get("faction", "")) == "men" \
		and String((host_session.lobby_launch_roster[1] as Dictionary).get("faction", "")) == "wild" \
		and String((host_session.lobby_launch_roster[0] as Dictionary).get("controller", "")) == "human" \
		and String((host_session.lobby_launch_roster[1] as Dictionary).get("controller", "")) == "human" \
		and (host_session.lobby_launch_roster[0] as Dictionary).get("heroes", []) == [picked_document] \
		and (host_session.lobby_launch_roster[1] as Dictionary).get("heroes", null) == []
	_check("launch_descriptor_lists_byte_identical", launch_sent and guest_accepted and roster_shape_ok \
		and host_roster_bytes == guest_roster_bytes \
		and var_to_bytes(host_session.lobby_launch_settings) == var_to_bytes(guest_session.lobby_launch_settings))
	_check("host_launch_roster_matches_the_external_seat_table_oracle",
		host_roster_bytes == var_to_bytes(expected_roster))
	hero_panel.close_lobby()
	hero_panel.queue_free()
	await process_frame

	# --- lobby panel UI builds and binds ----------------------------------------
	var panel: Panel = LobbyScript.new()
	panel.size = Vector2(1000, 640)
	root.add_child(panel)
	await process_frame
	panel.open(host_session, true, "Aragorn")
	var ui_ok: bool = panel.heading_label != null and panel.heading_label.text == "GAME LOBBY - HOSTING" \
		and panel.name_edit.text == "Aragorn" \
		and panel.army_opt.item_count == 7 and panel.color_opt.item_count == 8 \
		and panel.map_opt.visible and not panel.map_value_label.visible \
		and panel.launch_button.visible and panel.hero_opt != null \
		and panel.remote_name_label.text == "Witch King" \
		and panel.remote_army_label.text == "Goblins" \
		and panel.remote_hero_labels[0].text == "-" \
		and panel.remote_ready_check.button_pressed
	panel.close_lobby()
	var guest_panel: Panel = LobbyScript.new()
	guest_panel.size = Vector2(1000, 640)
	root.add_child(guest_panel)
	await process_frame
	guest_panel.open(guest_session, false, "")
	var guest_ui_ok: bool = guest_panel.name_edit.text == "Challenger" \
		and not guest_panel.map_opt.visible and guest_panel.map_value_label.visible \
		and not guest_panel.launch_button.visible
	guest_panel.close_lobby()
	panel.queue_free()
	guest_panel.queue_free()
	await process_frame
	_check("lobby_panel_builds_for_both_sides", ui_ok and guest_ui_ok)

	# --- chat UX: alone in the lobby works; invalid text gets an honest error ---
	# A host with no challenger yet must still be able to chat: the line lands in
	# the LOCAL log only (nothing to transmit; a later guest never sees pre-join
	# history, retail-consistent) and the input clears without any error.
	var solo_host = SessionScript.new()
	var solo_bind_ok: bool = solo_host.host(0) == OK
	var chat_panel: Panel = LobbyScript.new()
	chat_panel.size = Vector2(1000, 640)
	root.add_child(chat_panel)
	await process_frame
	chat_panel.open(solo_host, true, "Aragorn")
	chat_panel.chat_edit.text = "anyone there?"
	chat_panel._on_chat_send()
	_check("chat_alone_lands_in_local_log", solo_bind_ok \
		and chat_panel.chat_log_label.text == "Aragorn: anyone there?" \
		and chat_panel.chat_edit.text == "" \
		and not chat_panel.status_label.text.contains("Message not sent"))
	# Genuinely invalid text (non-printable-ASCII) is refused with the honest
	# text-validation error, is never appended, and never transmits: the session
	# chat log stays empty (fail-closed contract intact).
	chat_panel.chat_edit.text = "smoke é☕ test"
	chat_panel._on_chat_send()
	_check("invalid_chat_text_gets_honest_error", \
		chat_panel.chat_log_label.text == "Aragorn: anyone there?" \
		and chat_panel.status_label.text.contains("printable ASCII") \
		and solo_host.lobby_chat_log.is_empty())
	chat_panel.close_lobby()
	solo_host.close()
	chat_panel.queue_free()
	await process_frame

	# --- leave / disconnect cleanup ---------------------------------------------
	guest_session.close()
	var guest_cleared: bool = guest_session.lobby_remote_profile.is_empty() \
		and guest_session.lobby_settings.is_empty() and guest_session.lobby_chat_log.is_empty() \
		and not guest_session.lobby_launch_received and guest_session.lobby_launch_roster.is_empty()
	var host_saw_disconnect := false
	for iteration in range(PUMP_BUDGET):
		host_session.poll()
		if not host_session.connected:
			host_saw_disconnect = true
			break
		if iteration % 100 == 99:
			OS.delay_msec(1)
	host_session.close()
	_check("leave_disconnect_cleans_up", guest_cleared and host_saw_disconnect \
		and not host_session.handshake_complete and host_session.lobby_local_profile.is_empty())

	# --- menu wiring: NETWORK flyout -> GAME LOBBY -> leave / launch -------------
	await _run_menu_wiring_checks()
	await _finish()


## Full main_menu integration: hosting/joining through the real flyout signals
## must open the GAME LOBBY page, leaving must return to the NETWORK page, and
## a real two-sided launch must write the full GameState selection and change
## the scene target to the retail loading boot.
func _run_menu_wiring_checks() -> void:
	var SessionScript = load("res://src/retail_slice/retail_lockstep_session.gd")
	var packed: PackedScene = load("res://scenes/boot.tscn")
	var menu = packed.instantiate()
	root.add_child(menu)
	await process_frame
	await process_frame
	var game_state := root.get_node_or_null("GameState")

	# HOST wiring: flyout signal -> session bound -> PAGE_MP_LOBBY.
	menu.multiplayer_flyout.host_requested.emit(27341)
	await process_frame
	var hosted_ok: bool = String(menu.get_current_page()) == "mp_lobby" \
		and menu.multiplayer_lobby.visible \
		and menu.get("_lobby_session") != null \
		and menu.multiplayer_flyout.host_button.disabled \
		and menu.multiplayer_lobby.heading_label.text == "GAME LOBBY - HOSTING"
	_check("menu_host_wiring_reaches_mp_lobby", hosted_ok, "page=%s" % String(menu.get_current_page()))
	if not hosted_ok or game_state == null:
		menu.queue_free()
		await process_frame
		return

	# LEAVE returns to the NETWORK page and releases the session + busy lock.
	menu.multiplayer_lobby._on_leave_pressed()
	await process_frame
	_check("menu_leave_returns_to_network_page",
		String(menu.get_current_page()) == "multiplayer" \
		and menu.get("_lobby_session") == null \
		and not menu.multiplayer_flyout.host_button.disabled \
		and not menu.multiplayer_lobby.visible)

	# JOIN wiring: a raw host accepts the menu's join; the lobby opens as guest.
	var raw_host = SessionScript.new()
	var raw_host_ok: bool = raw_host.host(0) == OK
	menu.multiplayer_flyout.join_requested.emit("127.0.0.1", raw_host.bound_port if raw_host_ok else 27342)
	await process_frame
	var joined_ok: bool = raw_host_ok and String(menu.get_current_page()) == "mp_lobby" \
		and menu.multiplayer_lobby.visible \
		and menu.multiplayer_lobby.heading_label.text == "GAME LOBBY - JOINED" \
		and not menu.multiplayer_lobby.launch_button.visible
	_check("menu_join_wiring_reaches_mp_lobby", joined_ok, "page=%s" % String(menu.get_current_page()))
	menu.multiplayer_lobby._on_leave_pressed()
	raw_host.close()
	await process_frame

	# LAUNCH: re-host, connect a raw ELVES guest, ready both sides, launch. The
	# lobby writes the byte-agreed selection (incl. retail_team_setup) and the
	# menu changes the scene target to the retail loading boot.
	menu.multiplayer_flyout.host_requested.emit(27341)
	await process_frame
	var lobby_session = menu.get("_lobby_session")
	var guest = SessionScript.new()
	var guest_join_ok: bool = lobby_session != null and guest.join("127.0.0.1", 27341) == OK
	var connected: bool = guest_join_ok and await _pump_menu_until(
		func() -> bool: return bool(lobby_session.handshake_complete) and bool(guest.handshake_complete), guest)
	# Connected chat still round-trips through the same UI path in BOTH
	# directions: the host's UI send reaches the guest session, and a guest
	# session line lands in the host's UI chat log.
	var ui_chat_ok := false
	if connected:
		menu.multiplayer_lobby.chat_edit.text = "gl hf"
		menu.multiplayer_lobby._on_chat_send()
		var host_line_arrived: bool = await _pump_menu_until(
			func() -> bool: return guest.lobby_chat_log.size() >= 1, guest)
		var guest_line_sent: bool = guest.send_lobby_chat("hi host")
		var guest_line_arrived: bool = guest_line_sent and await _pump_menu_until(
			func() -> bool: return menu.multiplayer_lobby.chat_log_label.text.contains("hi host"), guest)
		ui_chat_ok = host_line_arrived and guest_line_arrived \
			and String((guest.lobby_chat_log[0] as Dictionary).get("text", "")) == "gl hf" \
			and menu.multiplayer_lobby.chat_log_label.text.contains(": gl hf") \
			and menu.multiplayer_lobby.chat_edit.text == ""
	_check("connected_chat_reaches_both_sides_via_ui", ui_chat_ok)
	var guest_announced: bool = connected and guest.send_lobby_profile("Legolas", "elves", 2, true)
	menu.multiplayer_lobby.local_ready_check.button_pressed = true
	var both_ready: bool = guest_announced and await _pump_menu_until(
		func() -> bool: return bool(lobby_session.lobby_remote_profile.get("ready", false)) \
			and bool(guest.lobby_remote_profile.get("ready", false)) \
			and not menu.multiplayer_lobby.launch_button.disabled, guest)
	if both_ready:
		menu.multiplayer_lobby._on_launch_pressed()
	var guest_accepted: bool = both_ready and await _pump_menu_until(
		func() -> bool: return bool(guest.lobby_launch_received), guest)
	# The launch handler drains the session over a bounded number of frames
	# before changing scene; wait for the deferred scene swap to land.
	var _scene_changed: bool = await _pump_menu_until(
		func() -> bool: return current_scene != null, guest)
	var team_setup: Array = game_state.get("retail_team_setup") as Array
	var game_state_ok: bool = team_setup.size() == 2 \
		and String((team_setup[0] as Dictionary).get("faction", "")) == "men" \
		and String((team_setup[1] as Dictionary).get("faction", "")) == "elves" \
		and String(game_state.get("retail_player_faction")) == "men" \
		and String(game_state.get("retail_enemy_faction")) == "elves" \
		and String(game_state.get("retail_mp_mode")) == "host" \
		and int(game_state.get("retail_mp_port")) == 27341
	# The loading boot may already have swapped to the slice scene by the time
	# it is sampled; either scene proves the launch path targeted the boot.
	var scene_ok: bool = current_scene != null \
		and current_scene.scene_file_path in [
			"res://scenes/retail_loading_boot.tscn",
			"res://scenes/retail_vertical_slice.tscn",
		]
	_check("menu_launch_writes_game_state_and_scene_target",
		guest_accepted and game_state_ok and scene_ok,
		"connected=%s ready=%s accepted=%s state=%s scene=%s" % [
			str(connected), str(both_ready), str(guest_accepted), str(game_state_ok),
			current_scene.scene_file_path if current_scene != null else "<none>",
		])
	guest.close()
	menu.queue_free()
	await process_frame


func _pump_menu_until(condition: Callable, extra_session = null) -> bool:
	## Frame-based pump: the menu lobby polls its own session from _process, so
	## the tree must run; a raw peer session is polled explicitly each frame.
	for _iteration in range(2000):
		if extra_session != null:
			extra_session.poll()
		await process_frame
		if condition.call():
			return true
	return false


func _pump_until(condition: Callable) -> bool:
	for iteration in range(PUMP_BUDGET):
		host_session.poll()
		guest_session.poll()
		if condition.call():
			return true
		if iteration % 100 == 99:
			OS.delay_msec(1)
	return false


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("RETAIL_MP_LOBBY PASS %s" % name)
	else:
		failed += 1
		printerr("RETAIL_MP_LOBBY FAIL %s%s" % [name, " (%s)" % detail if detail != "" else ""])


func _finish() -> void:
	if host_session != null:
		host_session.close()
	if guest_session != null:
		guest_session.close()
	print("RETAIL_MP_LOBBY_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)
