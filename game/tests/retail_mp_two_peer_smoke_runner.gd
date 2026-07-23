extends SceneTree

## Two-peer localhost smoke for the menu->lobby->launch multiplayer flow, in
## one process (the Game Control API deliberately does not attach to the menu
## and is disabled in multiplayer sessions, so the scripted fallback drives two
## real main_menu instances with two real ENet lockstep sessions instead).
##
## Phase 1 (menu wiring): two boot.tscn menus host/join through the NETWORK
## flyout's real signals, exchange lobby profiles (host Men, guest ELVES),
## ready up, and the host launches. Both lobbies must byte-agree on the roster
## and write the full GameState selection. The menus' scene change is
## intercepted (this runner owns match boot).
##
## Phase 2 (match): two full retail_vertical_slice instances boot against that
## GameState — one host, one join — over a fresh loopback lockstep session.
## Both must reach a ticking sim with DIFFERENT factions (host Men, guest
## Elves presentation; sim roster men vs elves on BOTH machines) and identical
## state hashes through tick 60. This is the check that kills the original
## "guest could only control faction 1" bug.

const LOBBY_PORT := 27351
const TARGET_TICK := 60
const HASH_INTERVAL := 30
const BOOT_DEADLINE_MS := 300000
const TICK_DEADLINE_MS := 180000

var passed := 0
var failed := 0
var host_launch_confirms := 0
var guest_launch_confirms := 0
var sampled_hashes: Dictionary = {}
var hashes_equal := true


func _initialize() -> void:
	OS.set_environment("OPENBFME_MP", "")
	OS.set_environment("OPENBFME_MP_ADDRESS", "")
	OS.set_environment("OPENBFME_MP_PORT", "")
	OS.set_environment("OPENBFME_SLICE_FACTION", "")
	OS.set_environment("OPENBFME_SLICE_MAP", "")
	OS.set_environment("OPENBFME_STARTER_ARMY", "")
	OS.set_environment("OPENBFME_CONTROL_PORT", "")
	call_deferred("_run")


func _run() -> void:
	root.size = Vector2i(1920, 1080)
	var packed: PackedScene = load("res://scenes/boot.tscn")
	if packed == null:
		_check("boot_scene_parses", false)
		_finish()
		return
	var host_menu = packed.instantiate()
	var guest_menu = packed.instantiate()
	root.add_child(host_menu)
	root.add_child(guest_menu)
	await process_frame
	await process_frame
	var game_state := root.get_node_or_null("GameState")

	# --- Phase 1: menu -> lobby -> launch ------------------------------------
	# Intercept the scene change: this runner boots the match itself.
	host_menu.multiplayer_lobby.launch_confirmed.disconnect(host_menu._on_lobby_launch_confirmed)
	guest_menu.multiplayer_lobby.launch_confirmed.disconnect(guest_menu._on_lobby_launch_confirmed)
	host_menu.multiplayer_lobby.launch_confirmed.connect(func() -> void: host_launch_confirms += 1)
	guest_menu.multiplayer_lobby.launch_confirmed.connect(func() -> void: guest_launch_confirms += 1)

	host_menu.multiplayer_flyout.host_requested.emit(LOBBY_PORT)
	await process_frame
	guest_menu.multiplayer_flyout.join_requested.emit("127.0.0.1", LOBBY_PORT)
	await process_frame
	var host_lobby = host_menu.multiplayer_lobby
	var guest_lobby = guest_menu.multiplayer_lobby
	var pages_ok: bool = String(host_menu.get_current_page()) == "mp_lobby" \
		and String(guest_menu.get_current_page()) == "mp_lobby"
	_check("both_menus_reach_mp_lobby", pages_ok,
		"host=%s guest=%s" % [String(host_menu.get_current_page()), String(guest_menu.get_current_page())])
	var host_session = host_menu.get("_lobby_session")
	var guest_session = guest_menu.get("_lobby_session")
	var connected: bool = pages_ok and host_session != null and guest_session != null \
		and await _pump_frames_until(func() -> bool:
			return bool(host_session.handshake_complete) and bool(guest_session.handshake_complete), 4000)
	_check("lobby_sessions_handshake", connected)
	if not connected or game_state == null:
		_finish()
		return

	# Guest picks ELVES (army option index 1) and both sides ready up through
	# the real panel controls.
	guest_lobby.army_opt.select(1)
	guest_lobby._on_profile_edited()
	guest_lobby.local_ready_check.button_pressed = true
	host_lobby.local_ready_check.button_pressed = true
	var both_ready: bool = await _pump_frames_until(func() -> bool:
		return bool(host_session.lobby_remote_profile.get("ready", false)) \
			and bool(guest_session.lobby_remote_profile.get("ready", false)) \
			and String(host_session.lobby_remote_profile.get("faction", "")) == "elves" \
			and not host_lobby.launch_button.disabled, 4000)
	_check("both_ready_guest_is_elves", both_ready)
	if both_ready:
		host_lobby._on_launch_pressed()
	var launch_accepted: bool = both_ready and await _pump_frames_until(func() -> bool:
		return host_launch_confirms >= 1 and guest_launch_confirms >= 1, 4000)
	var team_setup: Array = game_state.get("retail_team_setup") as Array
	var state_ok: bool = team_setup.size() == 2 \
		and String((team_setup[0] as Dictionary).get("faction", "")) == "men" \
		and String((team_setup[1] as Dictionary).get("faction", "")) == "elves" \
		and String(game_state.get("retail_player_faction")) == "men" \
		and String(game_state.get("retail_enemy_faction")) == "elves"
	_check("launch_writes_men_vs_elves_selection", launch_accepted and state_ok,
		"accepted=%s setup=%s" % [str(launch_accepted), str(team_setup)])

	# Tear the menus down (sessions closed so the match can re-bind the port).
	for menu in [host_menu, guest_menu]:
		var session = menu.get("_lobby_session")
		menu.multiplayer_lobby.close_lobby()
		if session != null:
			session.close()
		menu.set("_lobby_session", null)
		menu.queue_free()
	await process_frame
	if not (launch_accepted and state_ok):
		_finish()
		return

	# --- Phase 2: both peers boot the match and tick in lockstep --------------
	var slice_scene: PackedScene = load("res://scenes/retail_vertical_slice.tscn")
	if slice_scene == null:
		_check("slice_scene_parses", false)
		_finish()
		return
	var host_slice = slice_scene.instantiate()
	host_slice.set("_mp_mode", "host")
	host_slice.set("_mp_address", "127.0.0.1")
	host_slice.set("_mp_port_text", "0")
	root.add_child(host_slice)
	var host_booted: bool = await _pump_frames_until(func() -> bool:
		return bool(host_slice.ready_ok) or String(host_slice.failure_reason) != "", 0, BOOT_DEADLINE_MS)
	_check("host_slice_boots_ready", host_booted and bool(host_slice.ready_ok), String(host_slice.failure_reason))
	if not bool(host_slice.ready_ok) or host_slice.get("lockstep_session") == null:
		_finish()
		return
	var match_port: int = int(host_slice.get("lockstep_session").bound_port)

	var guest_slice = slice_scene.instantiate()
	guest_slice.set("_mp_mode", "join")
	guest_slice.set("_mp_address", "127.0.0.1")
	guest_slice.set("_mp_port_text", str(match_port))
	root.add_child(guest_slice)
	var guest_booted: bool = await _pump_frames_until(func() -> bool:
		return bool(guest_slice.ready_ok) or String(guest_slice.failure_reason) != "", 0, BOOT_DEADLINE_MS)
	_check("guest_slice_boots_ready", guest_booted and bool(guest_slice.ready_ok), String(guest_slice.failure_reason))
	if not bool(guest_slice.ready_ok):
		_finish()
		return

	_check("teams_and_rosters_assigned",
		int(host_slice.local_team) == 0 and int(guest_slice.local_team) == 1 \
			and _roster_is_men_vs_elves(host_slice) and _roster_is_men_vs_elves(guest_slice),
		"host_team=%d guest_team=%d" % [int(host_slice.local_team), int(guest_slice.local_team)])

	# The original bug: the guest's presentation surfaces were always faction 1
	# (Men). The guest must now classify and present its OWN elves army while
	# the host keeps Men.
	var guest_heading := _hud_heading(guest_slice)
	var host_heading := _hud_heading(host_slice)
	_check("guest_presents_own_elves_army",
		_classification_matches(guest_slice, ["elven", "eregion"]) \
			and _classification_matches(host_slice, ["men", "gondor", "rohan"]) \
			and guest_heading == "ELVES" and host_heading == "MEN OF THE WEST",
		"guest_heading=%s host_heading=%s" % [guest_heading, host_heading])

	# Tick to 60 in lockstep, sampling the deterministic state hash at every
	# 30-tick barrier both sims cross together.
	var host_sim = host_slice.get("simulation")
	var guest_sim = guest_slice.get("simulation")
	var host_session_live = host_slice.get("lockstep_session")
	var guest_session_live = guest_slice.get("lockstep_session")
	var deadline := Time.get_ticks_msec() + TICK_DEADLINE_MS
	while Time.get_ticks_msec() < deadline:
		await process_frame
		_sample_hashes(host_sim, guest_sim)
		if bool(host_session_live.desynced) or bool(guest_session_live.desynced):
			break
		if int(host_sim.tick_index) >= TARGET_TICK and int(guest_sim.tick_index) >= TARGET_TICK \
			and sampled_hashes.has(30) and sampled_hashes.has(60):
			break
	for tick in [30, 60]:
		var pair: Dictionary = sampled_hashes.get(tick, {})
		print("SMOKE_HASH tick=%d host=%s guest=%s equal=%s" % [
			tick, String(pair.get("host", "<unsampled>")), String(pair.get("guest", "<unsampled>")),
			str(String(pair.get("host", "a")) == String(pair.get("guest", "b"))),
		])
	_check("lockstep_ticks_to_60_identical_hashes",
		int(host_sim.tick_index) >= TARGET_TICK and int(guest_sim.tick_index) >= TARGET_TICK \
			and sampled_hashes.has(30) and sampled_hashes.has(60) and hashes_equal \
			and not bool(host_session_live.desynced) and not bool(guest_session_live.desynced),
		"host_tick=%d guest_tick=%d desync=%s/%s sampled=%s" % [
			int(host_sim.tick_index), int(guest_sim.tick_index),
			str(bool(host_session_live.desynced)), str(bool(guest_session_live.desynced)),
			str(sampled_hashes.keys()),
		])

	host_slice.queue_free()
	guest_slice.queue_free()
	await process_frame
	_finish()


func _sample_hashes(host_sim, guest_sim) -> void:
	var tick: int = int(host_sim.tick_index)
	if tick <= 0 or tick % HASH_INTERVAL != 0 or tick != int(guest_sim.tick_index):
		return
	if sampled_hashes.has(tick):
		return
	var host_hash := String(host_sim.state_hash())
	var guest_hash := String(guest_sim.state_hash())
	sampled_hashes[tick] = {"host": host_hash, "guest": guest_hash}
	hashes_equal = hashes_equal and host_hash == guest_hash


func _roster_is_men_vs_elves(slice) -> bool:
	var manifests: Dictionary = (slice.get("gameplay_rules") as Dictionary).get("team_faction_manifests", {}) as Dictionary
	return String((manifests.get(0, {}) as Dictionary).get("faction", "")) == "men" \
		and String((manifests.get(1, {}) as Dictionary).get("faction", "")) == "elves"


func _classification_matches(slice, prefixes: Array) -> bool:
	## Every fieldable runtime the slice classified locally belongs to the given
	## faction scope (and at least one exists).
	var runtimes: Dictionary = slice.get("fieldable_unit_runtimes") as Dictionary
	if runtimes.is_empty():
		return false
	for key_value in runtimes.keys():
		var key := String(key_value).to_lower()
		var in_scope := false
		for prefix_value in prefixes:
			if key.begins_with(String(prefix_value)):
				in_scope = true
				break
		if not in_scope:
			return false
	return true


func _hud_heading(slice) -> String:
	var hud = slice.get("hud")
	if hud == null:
		return "<no hud>"
	var label = hud.get("_faction_heading_label")
	return String(label.text) if label != null else "<no label>"


func _pump_frames_until(condition: Callable, frame_budget: int, deadline_ms: int = 0) -> bool:
	var deadline := Time.get_ticks_msec() + deadline_ms
	var frames := 0
	while true:
		await process_frame
		if condition.call():
			return true
		frames += 1
		if frame_budget > 0 and frames >= frame_budget:
			return false
		if deadline_ms > 0 and Time.get_ticks_msec() >= deadline:
			return false
	return false


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("RETAIL_MP_SMOKE PASS %s" % name)
	else:
		failed += 1
		printerr("RETAIL_MP_SMOKE FAIL %s%s" % [name, " (%s)" % detail if detail != "" else ""])


func _finish() -> void:
	print("RETAIL_MP_SMOKE_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)
