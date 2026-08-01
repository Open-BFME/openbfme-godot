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


const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
# Turns a GDScript runtime error inside `_run` — which unwinds past every
# `quit()` and would otherwise leave this headless process idling forever —
# into a loud non-zero exit. See tests/runner_watchdog.gd.
var _runner_watchdog := RunnerWatchdogScript.new()


func _initialize() -> void:
	_runner_watchdog.start(self, "RETAIL_MP_TWO_PEER_SMOKE_RUNNER")
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
	# The GAME LOBBY panel is built at the moment a host/join is attempted, not
	# during the menu's _ready (its script and the lockstep session are ~2,500
	# lines the menu does not need to draw itself). This runner reaches into the
	# lobby BEFORE hosting, so it asks for it explicitly.
	host_menu.ensure_multiplayer_lobby()
	guest_menu.ensure_multiplayer_lobby()
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

	# Cross-faction presentation docs, per entity: BOTH peers must render each
	# team's structures/battalions through THAT team's own faction documents
	# (host sees the guest's elves as elves, guest sees the host's Men as Men).
	var host_structs := _structure_presentation_report(host_slice)
	var guest_structs := _structure_presentation_report(guest_slice)
	_check("host_structures_present_per_team_docs", bool(host_structs.ok), String(host_structs.detail))
	_check("guest_structures_present_per_team_docs", bool(guest_structs.ok), String(guest_structs.detail))
	var host_batts := _battalion_presentation_report(host_slice)
	var guest_batts := _battalion_presentation_report(guest_slice)
	_check("host_battalions_present_per_team_docs", bool(host_batts.ok), String(host_batts.detail))
	_check("guest_battalions_present_per_team_docs", bool(guest_batts.ok), String(guest_batts.detail))

	# HUD surfaces are the LOCAL army's: train/portrait specs and the spellbook
	# document each peer's HUD carries.
	_check("train_surfaces_are_local_army",
		_train_surface_matches(host_slice, MEN_PREFIXES) and _train_surface_matches(guest_slice, ELF_PREFIXES),
		"host=%s guest=%s" % [_train_surface_units(host_slice), _train_surface_units(guest_slice)])
	var host_spellbook := _hud_spellbook_faction(host_slice)
	var guest_spellbook := _hud_spellbook_faction(guest_slice)
	_check("spellbook_docs_are_local_faction",
		host_spellbook == "men" and guest_spellbook == "elves",
		"host=%s guest=%s" % [host_spellbook, guest_spellbook])

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

	# The APT selection context must carry each seat's REAL local team (the
	# guest is team 1, not a hardcoded 0). _refresh_hud syncs every frame, so
	# by tick 60 both HUDs have recorded their seat.
	_check("apt_context_carries_real_local_team",
		int(host_slice.get("hud").get("last_selection_context_local_team")) == 0 \
			and int(guest_slice.get("hud").get("last_selection_context_local_team")) == 1,
		"host=%d guest=%d" % [
			int(host_slice.get("hud").get("last_selection_context_local_team")),
			int(guest_slice.get("hud").get("last_selection_context_local_team")),
		])

	await _run_guest_control_checks(host_slice, guest_slice, host_sim, guest_sim, host_session_live, guest_session_live)

	host_slice.queue_free()
	guest_slice.queue_free()
	await process_frame
	_finish()


func _run_guest_control_checks(host_slice, guest_slice, host_sim, guest_sim, host_session_live, guest_session_live) -> void:
	## The user-reported control bugs, end to end on live lockstep peers:
	## 1. the guest's OWN porter spawns on team 1 and IS selectable (selection
	##    gating follows the real local seat, not a hardcoded team 0);
	## 2. selecting it surfaces the build palette and a real construct order
	##    lands as a TEAM 1 structure on BOTH sims;
	## 3. the guest CANNOT select or command team-0 (host) units — orders are
	##    validated against the issuing peer's seat, fail-closed;
	## 4. the guest's (manifest-derived, elves) fortress keeps its full command
	##    set: the hero roster AND the porter train rule;
	## 5. lockstep hashes stay equal through the whole scenario.
	var guest_porter_id := 0
	for id_value in guest_sim.entity_ids():
		var row: Dictionary = guest_sim.entity(int(id_value))
		if int(row.get("team", -1)) == 1 and bool(row.get("is_builder", false)) and int(row.get("health", 0)) > 0:
			guest_porter_id = int(id_value)
			break
	_check("guest_own_porter_spawns_on_team_1", guest_porter_id != 0)
	if guest_porter_id == 0:
		return
	var host_porter_id := 0
	for id_value in guest_sim.entity_ids():
		var row: Dictionary = guest_sim.entity(int(id_value))
		if int(row.get("team", -1)) == 0 and bool(row.get("is_builder", false)) and int(row.get("health", 0)) > 0:
			host_porter_id = int(id_value)
			break

	# Selection gating follows the guest's real seat.
	_check("guest_selects_own_porter", bool(guest_sim.select_only(guest_porter_id)))
	_check("guest_cannot_select_host_units",
		host_porter_id != 0 and not bool(guest_sim.select_only(host_porter_id)) \
			and int(guest_sim.select_many([host_porter_id] as Array[int])) == 0)
	# Re-select the guest porter (the host-unit attempts must not have taken).
	guest_sim.select_only(guest_porter_id)
	guest_slice.set("selected_structure_id", 0)
	guest_slice._sync_presentation()
	await process_frame
	var guest_bar = guest_slice.get("hud").get("retail_side_command_bar")
	_check("guest_own_porter_surfaces_build_palette",
		guest_bar != null and guest_bar.side_buttons().size() > 0,
		"buttons=%d" % (guest_bar.side_buttons().size() if guest_bar != null else -1))

	# The guest's own (derived elves) fortress keeps the full command set.
	var guest_fortress_id: int = int(guest_sim.fortress_id(1))
	var guest_fortress: Dictionary = guest_sim.structure(guest_fortress_id)
	var guest_production: Array = guest_fortress.get("production", [])
	var guest_rules: Dictionary = guest_sim.unit_production_rules_for_team(1)
	var porter_unit_type := String(guest_sim.entity(guest_porter_id).get("unit_type", ""))
	var fortress_hero_count := 0
	var foreign_entries := 0
	for unit_type_value in guest_production:
		var unit_type := String(unit_type_value)
		if not _in_prefix_scope(unit_type, ELF_PREFIXES):
			foreign_entries += 1
		if String((guest_rules.get(unit_type, {}) as Dictionary).get("category", "")) == "hero":
			fortress_hero_count += 1
	_check("guest_fortress_trains_own_porter",
		guest_production.has(porter_unit_type) and guest_rules.has(porter_unit_type),
		"production=%s" % str(guest_production))
	_check("guest_fortress_keeps_hero_roster", fortress_hero_count >= 2,
		"heroes=%d production=%s" % [fortress_hero_count, str(guest_production)])
	_check("guest_fortress_production_stays_in_faction_scope", foreign_entries == 0,
		"production=%s" % str(guest_production))
	# The host's fortress must not offer the guest's faction either.
	var host_fortress: Dictionary = host_sim.structure(int(host_sim.fortress_id(0)))
	var host_foreign := 0
	for unit_type_value in host_fortress.get("production", []) as Array:
		if not _in_prefix_scope(String(unit_type_value), MEN_PREFIXES):
			host_foreign += 1
	_check("host_fortress_production_stays_in_faction_scope", host_foreign == 0,
		"production=%s" % str(host_fortress.get("production", [])))

	# A real construct order through the guest's own porter: pick a provably
	# valid site via the sim's own dry run, then land it over lockstep.
	var construct_kind := ""
	for kind_value in guest_sim.structure_kinds_for_team(1):
		if String(kind_value) != "fortress":
			construct_kind = String(kind_value)
			break
	var porter_position := Vector2(guest_sim.entity(guest_porter_id).get("position", Vector2.ZERO))
	var construct_position := Vector2.ZERO
	for offset in [Vector2(10, 0), Vector2(0, 10), Vector2(-10, 0), Vector2(0, -10), Vector2(14, 6), Vector2(-14, -6), Vector2(18, 0), Vector2(0, 18)]:
		var probe: Dictionary = guest_sim.issue_construct([guest_porter_id] as Array[int], construct_kind, porter_position + offset, true, 1)
		if bool(probe.get("ok", false)):
			construct_position = porter_position + offset
			break
	_check("guest_construct_site_resolves", construct_kind != "" and construct_position != Vector2.ZERO,
		"kind=%s porter_at=%s" % [construct_kind, str(porter_position)])
	var structures_before: int = guest_sim.structure_ids().size()
	guest_slice._apply_local_command("issue_construct", {
		"ids": [guest_porter_id], "structure_kind": construct_kind, "position": construct_position,
	})
	# The guest also attempts to command a HOST unit and to train at the host
	# fortress — both must be rejected by team validation on BOTH sims.
	var host_porter_before := Vector2(guest_sim.entity(host_porter_id).get("position", Vector2.ZERO))
	guest_slice._apply_local_command("issue_move", {"ids": [host_porter_id], "destination": host_porter_before + Vector2(30, 30)})
	guest_slice._apply_local_command("queue_unit", {"producer": int(host_sim.fortress_id(0)), "unit_type": String((host_fortress.get("production", []) as Array)[0]) if not (host_fortress.get("production", []) as Array).is_empty() else "x"})
	var commands_deadline := Time.get_ticks_msec() + TICK_DEADLINE_MS
	var target_tick: int = int(guest_sim.tick_index) + 90
	while Time.get_ticks_msec() < commands_deadline:
		await process_frame
		_sample_hashes(host_sim, guest_sim)
		if bool(host_session_live.desynced) or bool(guest_session_live.desynced):
			break
		if int(host_sim.tick_index) >= target_tick and int(guest_sim.tick_index) >= target_tick:
			break
	var guest_new_structure_ok := false
	var host_new_structure_ok := false
	for sim_pair in [[guest_sim, true], [host_sim, false]]:
		var sim = sim_pair[0]
		for id_value in sim.structure_ids():
			var row: Dictionary = sim.structure(int(id_value))
			if String(row.get("structure_kind", "")) == construct_kind and int(row.get("team", -1)) == 1 and int(row.get("builder_id", 0)) == guest_porter_id:
				if bool(sim_pair[1]):
					guest_new_structure_ok = true
				else:
					host_new_structure_ok = true
	_check("guest_construct_lands_as_team_1_on_both_sims",
		guest_new_structure_ok and host_new_structure_ok and guest_sim.structure_ids().size() > structures_before,
		"guest=%s host=%s" % [str(guest_new_structure_ok), str(host_new_structure_ok)])
	var host_porter_row: Dictionary = host_sim.entity(host_porter_id)
	_check("guest_cannot_command_host_units",
		String(host_porter_row.get("order_kind", "")) != "move" \
			and Array(host_sim.structure(int(host_sim.fortress_id(0))).get("queue", [])).is_empty(),
		"order_kind=%s queue=%s" % [String(host_porter_row.get("order_kind", "")), str(host_sim.structure(int(host_sim.fortress_id(0))).get("queue", []))])
	_check("lockstep_hashes_stay_equal_through_control_scenario",
		hashes_equal and not bool(host_session_live.desynced) and not bool(guest_session_live.desynced),
		"sampled=%s" % str(sampled_hashes.keys()))


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


## Doc-id scopes cover both id namespaces: raw runtime ids ("GondorFighter")
## and projected bundle-object ids ("bfme2.object.gondor-fighter-horde").
const MEN_PREFIXES := [
	"men", "gondor", "rohan",
	"bfme2.object.men", "bfme2.object.gondor", "bfme2.object.rohan",
]
const ELF_PREFIXES := [
	"elven", "eregion",
	"bfme2.object.elven", "bfme2.object.eregion",
]


func _in_prefix_scope(value: String, prefixes: Array) -> bool:
	var lowered := value.to_lower()
	for prefix_value in prefixes:
		if lowered.begins_with(String(prefix_value)):
			return true
	return false


func _team_prefixes(team: int) -> Array:
	return MEN_PREFIXES if team == 0 else ELF_PREFIXES


func _structure_presentation_report(slice) -> Dictionary:
	## Every team-0/1 structure node must carry the OWNING team's manifest doc
	## id for its kind, in that team's faction object scope, with a clean
	## lifecycle contract. Non-vacuous: at least one structure per team.
	var manifests: Dictionary = slice.get("_team_faction_manifests") as Dictionary
	var sim = slice.get("simulation")
	var nodes: Dictionary = slice.get("structure_nodes") as Dictionary
	var counts := {0: 0, 1: 0}
	for id_value in sim.structure_ids():
		var id := int(id_value)
		var row: Dictionary = sim.structure(id)
		var team := int(row.get("team", -1))
		if team != 0 and team != 1:
			continue
		var kind := String(row.get("structure_kind", ""))
		var expected := String((((manifests.get(team, {}) as Dictionary).get("structure_object_ids", {}) as Dictionary)).get(kind, ""))
		var node = nodes.get(id)
		if node == null:
			return {"ok": false, "detail": "%d:%s missing node" % [id, kind]}
		var actual := String(node.get("_bundle_object_id"))
		if expected == "" or actual != expected:
			return {"ok": false, "detail": "%d:%s team=%d expected=%s actual=%s" % [id, kind, team, expected, actual]}
		if String(node.get("contract_error")) != "":
			return {"ok": false, "detail": "%d:%s contract=%s" % [id, kind, String(node.get("contract_error"))]}
		if not _in_prefix_scope(actual, _team_prefixes(team)):
			return {"ok": false, "detail": "%d:%s doc=%s outside team-%d faction scope" % [id, kind, actual, team]}
		counts[team] = int(counts[team]) + 1
	if int(counts[0]) < 1 or int(counts[1]) < 1:
		return {"ok": false, "detail": "vacuous per-team coverage: %s" % str(counts)}
	return {"ok": true, "detail": ""}


func _battalion_presentation_report(slice) -> Dictionary:
	## Every team-0/1 battalion node must present its sim object id through a
	## validated animation capability in the owning team's faction scope.
	var sim = slice.get("simulation")
	var nodes: Dictionary = slice.get("battalion_nodes") as Dictionary
	var capabilities: Dictionary = slice.get("validated_battalion_capabilities") as Dictionary
	var counts := {0: 0, 1: 0}
	for id_value in sim.entity_ids():
		var id := int(id_value)
		var row: Dictionary = sim.entity(id)
		var team := int(row.get("team", -1))
		if team != 0 and team != 1:
			continue
		var node = nodes.get(id)
		if node == null:
			return {"ok": false, "detail": "%d missing node" % id}
		var object_id := String(node.get("object_id"))
		if object_id == "" or object_id != String(row.get("object_id", "")):
			return {"ok": false, "detail": "%d node doc=%s sim doc=%s" % [id, object_id, String(row.get("object_id", ""))]}
		if (capabilities.get(object_id, {}) as Dictionary).is_empty():
			return {"ok": false, "detail": "%d:%s has no validated capability" % [id, object_id]}
		if not _in_prefix_scope(object_id, _team_prefixes(team)):
			return {"ok": false, "detail": "%d doc=%s outside team-%d faction scope" % [id, object_id, team]}
		counts[team] = int(counts[team]) + 1
	if int(counts[0]) < 1 or int(counts[1]) < 1:
		return {"ok": false, "detail": "vacuous per-team coverage: %s" % str(counts)}
	return {"ok": true, "detail": ""}


func _train_surface_matches(slice, prefixes: Array) -> bool:
	## Every train command AND portrait spec on this HUD belongs to the local
	## army's faction scope (and at least one of each exists).
	var hud = slice.get("hud")
	var command_specs: Array = hud.get("_retail_command_specs") as Array
	var portrait_specs: Array = hud.get("_retail_portrait_specs") as Array
	if command_specs.is_empty() or portrait_specs.is_empty():
		return false
	for spec_value in command_specs + portrait_specs:
		if not _in_prefix_scope(String((spec_value as Dictionary).get("unit_id", "")), prefixes):
			return false
	return true


func _train_surface_units(slice) -> String:
	var hud = slice.get("hud")
	var units: Array[String] = []
	for spec_value in hud.get("_retail_command_specs") as Array:
		units.append(String((spec_value as Dictionary).get("unit_id", "")))
	return ",".join(units)


func _hud_spellbook_faction(slice) -> String:
	var doc: Dictionary = slice.get("hud").get("_spellbook_runtime") as Dictionary
	return String((doc.get("target", {}) as Dictionary).get("faction", "")).to_lower()


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
