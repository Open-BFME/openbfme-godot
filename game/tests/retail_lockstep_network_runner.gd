extends SceneTree

const SimScript = preload("res://src/retail_slice/retail_slice_sim.gd")
const SessionScript = preload("res://src/retail_slice/retail_lockstep_session.gd")

var passed := 0
var failed := 0
var host_sim
var client_sim
var host_session
var client_session
var _sampled_hash_ticks: Dictionary = {}
var _network_hashes_equal := true
var _selection_hashes_equal := true


const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
# Turns a GDScript runtime error inside `_run` — which unwinds past every
# `quit()` and would otherwise leave this headless process idling forever —
# into a loud non-zero exit. See tests/runner_watchdog.gd.
var _runner_watchdog := RunnerWatchdogScript.new()


func _initialize() -> void:
	_runner_watchdog.start(self, "RETAIL_LOCKSTEP_NETWORK_RUNNER")
	call_deferred("_run")


func _make_sim():
	var sim = SimScript.new()
	# This is the accepted Step-1 fixture verbatim: compact real gameplay,
	# deterministic setup({}, {}), and no presentation or scene-tree state.
	sim._rules = _harness_rules()
	sim.setup({}, {})
	sim.ai_enabled = true
	for structure_id in sim.structure_ids():
		if structure_id != 1003:
			sim.structures.erase(structure_id)
	for entity_id in sim.entity_ids():
		if not [1, 2, 3, 101].has(entity_id):
			sim.entities.erase(entity_id)
	sim.expansion_pads.clear()
	sim._ai_production_plan.clear()
	sim.force_ai_construction_complete()
	return sim


func _harness_rules() -> Dictionary:
	return {
		# Q80: the 8 core manifest tables are required; this fixture supplies
		# the labeled SYNTHETIC default_manifest() explicitly.
		"faction_manifest": preload("res://src/retail_slice/retail_faction_manifest.gd").default_manifest(),
		"enable_base_loop": true,
		"starting_resources": 10000,
		"ai_attack_delay_ticks": 4000,
		"unit_rules": {
			SimScript.SOLDIER_OBJECT_ID: _unit_rule(SimScript.SOLDIER_HORDE_ID, false),
			SimScript.ARCHER_OBJECT_ID: _unit_rule(SimScript.ARCHER_OBJECT_ID, false),
			SimScript.TOWER_GUARD_OBJECT_ID: _unit_rule(SimScript.TOWER_GUARD_OBJECT_ID, false),
			SimScript.KNIGHT_OBJECT_ID: _unit_rule(SimScript.KNIGHT_OBJECT_ID, false),
			SimScript.BUILDER_OBJECT_ID: _unit_rule(SimScript.BUILDER_OBJECT_ID, true),
		},
	}


func _unit_rule(horde_id: String, is_builder: bool) -> Dictionary:
	return {
		"horde_id": horde_id,
		"speed": 1.0,
		"speed_source": 10.0,
		"acceleration": 1.0,
		"acceleration_source": 10.0,
		"turn_rate_degrees_per_second": 180.0,
		"braking": 1.0,
		"braking_source": 10.0,
		"attack_range": 1.15,
		"attack_range_source": 11.5,
		"minimum_attack_range": 0.0,
		"minimum_attack_range_source": 0.0,
		"vision_range": 40.0,
		"vision_range_source": 400.0,
		"delay_between_shots_ms": 600.0,
		"pre_attack_delay_ms": 200.0,
		"firing_duration_ms": 200.0,
		"attack_period_ticks": 10,
		"pre_attack_ticks": 2,
		"firing_duration_ticks": 2,
		"member_damage": 10,
		"member_health": 200,
		"member_count": 1,
		"formation_positions": [Vector3.ZERO],
		"provenance": {},
		"is_builder": is_builder,
	}


func _run() -> void:
	host_sim = _make_sim()
	client_sim = _make_sim()
	host_sim.ai_enabled = false
	client_sim.ai_enabled = false
	host_session = SessionScript.new(host_sim)
	client_session = SessionScript.new(client_sim)
	var host_error: Error = host_session.host(0)
	var join_error: Error = client_session.join("127.0.0.1", host_session.bound_port) if host_error == OK else ERR_CANT_CONNECT
	var handshake_ok: bool = host_error == OK and join_error == OK and _pump_handshake(10000)
	_check("handshake_team_assignment", handshake_ok \
		and host_session.local_team == 0 and host_session.peer_team == 1 \
		and client_session.local_team == 1 and client_session.peer_team == 0)
	if not handshake_ok:
		_finish()
		return

	var move_destination := Vector2(-15.0, -10.0)
	var original_destination := Vector2(host_sim.entity(1).get("destination", Vector2.ZERO))
	var original_client_destination := Vector2(client_sim.entity(1).get("destination", Vector2.ZERO))
	host_session.submit_local("issue_move", {"ids": [1], "destination": move_destination})
	host_session.submit_local("issue_attack", {"ids": [2], "target_id": 101})
	host_session.submit_local("queue_unit", {"producer": 1003, "unit_type": SimScript.SOLDIER_HORDE_ID})
	client_session.submit_local("issue_move", {"ids": [101], "destination": Vector2(18.0, 14.0)})
	client_session.submit_local("issue_stop", {"ids": [101]})
	var destination_change_tick := -1
	var client_destination_change_tick := -1
	var run_ok := true
	var reached_tick_600 := false
	for _iteration in range(200000):
		_apply_divergent_selection()
		_pump_once_to_limit(600)
		if destination_change_tick < 0 \
			and not Vector2(host_sim.entity(1).get("destination", Vector2.ZERO)).is_equal_approx(original_destination):
			destination_change_tick = host_sim.tick_index
		if client_destination_change_tick < 0 \
			and not Vector2(client_sim.entity(1).get("destination", Vector2.ZERO)).is_equal_approx(original_client_destination):
			client_destination_change_tick = client_sim.tick_index
		_sample_equal_hash()
		if host_sim.tick_index >= 600 and client_sim.tick_index >= 600 \
			and host_session._hash_barrier_tick < 0 and client_session._hash_barrier_tick < 0:
			reached_tick_600 = true
			break
		if host_session.desynced or client_session.desynced:
			run_ok = false
			break
	if not reached_tick_600:
		run_ok = false
	var input_delay_ok: bool = destination_change_tick == host_session.input_delay_ticks \
		and client_destination_change_tick == client_session.input_delay_ticks
	_check("scripted_commands_hashes_through_tick_600", run_ok and host_sim.tick_index == 600 \
		and client_sim.tick_index == 600 and _network_hashes_equal and not host_session.desynced and not client_session.desynced)
	_check("input_delay_exact_execution_tick", input_delay_ok, "host_tick=%d client_tick=%d expected=%d" % [destination_change_tick, client_destination_change_tick, host_session.input_delay_ticks])

	var client_last_acknowledged_tick: int = host_session.remote_ack_tick
	var client_tick_before_stall: int = client_sim.tick_index
	for _iteration in range(1000):
		host_session.poll()
		host_session.advance_if_ready()
	var host_tick_after_stall: int = host_sim.tick_index
	var stall_bound_ok: bool = host_tick_after_stall <= client_last_acknowledged_tick + host_session.input_delay_ticks \
		and client_sim.tick_index == client_tick_before_stall
	var recovered := _pump_to_tick(660, 200000, true)
	_check("lockstep_stall_and_recovery", stall_bound_ok and recovered \
		and host_sim.state_hash() == client_sim.state_hash(),
		"host_tick=%d client_ack=%d client_tick=%d" % [host_tick_after_stall, client_last_acknowledged_tick, client_tick_before_stall])

	host_session.submit_local("pause", {})
	var pause_target: int = host_sim.tick_index + host_session.input_delay_ticks
	var reached_pause := _pump_to_tick(pause_target, 50000, false)
	var paused_tick: int = host_sim.tick_index
	var pause_same_tick: bool = reached_pause and host_sim.clock_paused and client_sim.clock_paused \
		and host_session.pause_command_tick == pause_target and client_session.pause_command_tick == pause_target
	for _iteration in range(250):
		_pump_once(true, true)
	var stayed_paused: bool = host_sim.tick_index == paused_tick and client_sim.tick_index == paused_tick
	client_session.submit_local("resume", {})
	var resume_target: int = client_sim.tick_index + client_session.input_delay_ticks
	var resumed := _pump_to_tick(resume_target + 60, 100000, false)
	var pause_resume_ok: bool = pause_same_tick and stayed_paused and resumed \
		and not host_sim.clock_paused and not client_sim.clock_paused \
		and host_session.pause_command_tick == resume_target and client_session.pause_command_tick == resume_target \
		and host_sim.state_hash() == client_sim.state_hash()
	_check("pause_resume_scheduled_lockstep", pause_resume_ok,
		"pause_tick=%d resume_tick=%d final_tick=%d" % [pause_target, resume_target, host_sim.tick_index])

	for _iteration in range(1000):
		host_session.poll()
		if host_session._remote_bundle_contiguous >= client_session._bundle_sent_through:
			break
	var spoof_tick: int = host_session._remote_bundle_contiguous + 1
	var spoof_rejections_before: int = host_session.rejected_remote_commands
	var spoof_hash_before: String = host_sim.state_hash()
	client_session._send_envelope({
		"kind": "bundle",
		"tick": spoof_tick,
		"commands": [{"tick": spoof_tick, "team": 0, "seq": 999999, "type": "pause", "args": {}}],
		"ack": 0,
	})
	client_session._connection.flush()
	for _iteration in range(1000):
		host_session.poll()
		if host_session.rejected_remote_commands > spoof_rejections_before:
			break
	var anti_spoof_ok: bool = host_session.rejected_remote_commands == spoof_rejections_before + 1 \
		and host_sim.state_hash() == spoof_hash_before and not host_sim.clock_paused
	_check("anti_spoof_wrong_team_rejected", anti_spoof_ok,
		"before=%d after=%d hash_equal=%s paused=%s spoof_tick=%d remote_contiguous=%d" % [
			spoof_rejections_before, host_session.rejected_remote_commands,
			str(host_sim.state_hash() == spoof_hash_before), str(host_sim.clock_paused),
			spoof_tick, host_session._remote_bundle_contiguous,
		])
	_check("selection_divergence_excluded_from_network_hash", _selection_hashes_equal and _network_hashes_equal)

	var corrupt_tick: int = host_sim.tick_index
	var corrupt_entity: Dictionary = client_sim.entity(1)
	corrupt_entity["position"] = Vector2(corrupt_entity.get("position", Vector2.ZERO)) + Vector2(7.0, -3.0)
	for _iteration in range(100000):
		_pump_once(true, true)
		if host_session.desynced and client_session.desynced:
			break
	var stopped_tick: int = host_sim.tick_index
	for _iteration in range(100):
		_pump_once(true, true)
	var desync_ok: bool = host_session.desynced and client_session.desynced \
		and host_session.desync_tick == client_session.desync_tick \
		and host_session.desync_tick > corrupt_tick \
		and host_session.desync_tick - corrupt_tick <= 30 \
		and host_sim.tick_index == stopped_tick and client_sim.tick_index == stopped_tick
	_check("desync_detection_and_stop", desync_ok,
		"corrupt_tick=%d desync_tick=%d" % [corrupt_tick, host_session.desync_tick])
	await _test_created_hero_exchange()
	_finish()


# --- created heroes across seats ---------------------------------------------
#
# A Create-a-Hero profile is saved under `user://`, so two peers NEVER have the
# same set. Injecting each peer's own local heroes would build two different
# production rosters and desync on the first purchase. The lobby therefore
# exchanges them, and this phase proves the two halves of that claim: the
# exchange converges (both peers hold the same table and derive a byte-identical
# launch roster), and the agreed table produces byte-identical rule input on both
# peers, after which two sims carrying DIFFERENT seats' heroes hash the same.

const CahHeroes = preload("res://src/content/cah_heroes.gd")
const Adapter = preload("res://src/retail_slice/playable_unit_runtime_adapter.gd")
const CAH_PRODUCER := "MenFortress"
const CAH_PRODUCER_KIND := "men_fortress"
const CAH_SEAT_A_ID := "aaaaaaaaaaaa1111aaaa1111"
const CAH_SEAT_B_ID := "bbbbbbbbbbbb2222bbbb2222"


func _test_created_hero_exchange() -> void:
	await process_frame
	await process_frame
	var content_db = root.get_node_or_null("ContentDB")
	var system_value: Variant = content_db.get("cah_system_runtime") if content_db != null else {}
	var system: Dictionary = (
		system_value as Dictionary if typeof(system_value) == TYPE_DICTIONARY else {}
	)
	_check("cah_table_mounted", CahHeroes.system_is_valid(system))
	if not CahHeroes.system_is_valid(system):
		return
	var profile_a := CahHeroes.new_profile(system, "Seat A", 0, 0)
	profile_a["heroId"] = CAH_SEAT_A_ID
	var profile_b := CahHeroes.new_profile(system, "Seat B", 0, 1)
	profile_b["heroId"] = CAH_SEAT_B_ID

	_test_created_hero_lobby_exchange(profile_a, profile_b)
	_test_created_hero_lockstep(system, profile_a, profile_b)


func _test_created_hero_lobby_exchange(profile_a: Dictionary, profile_b: Dictionary) -> void:
	## Sim-less lobby sessions on a real socket, exactly the pre-game phase.
	var host = SessionScript.new(null)
	var guest = SessionScript.new(null)
	var host_error: Error = host.host(0)
	var join_error: Error = guest.join("127.0.0.1", host.bound_port) if host_error == OK else ERR_CANT_CONNECT
	var seated := host_error == OK and join_error == OK and _pump_pair(host, guest, func() -> bool:
		return host.handshake_complete and guest.handshake_complete and guest.local_seat == 1)
	_check("cah_lobby_seated", seated)
	if not seated:
		host.close()
		guest.close()
		return
	host.send_lobby_profile("Host", "men", 0, true)
	guest.send_lobby_profile("Guest", "men", 1, true)
	_check("cah_host_announces_its_heroes", host.send_lobby_heroes([profile_a]))
	_check("cah_guest_announces_its_heroes", guest.send_lobby_heroes([profile_b]))
	var converged := _pump_pair(host, guest, func() -> bool:
		return host.lobby_seat_heroes.size() == 2 and guest.lobby_seat_heroes.size() == 2 \
			and host.lobby_seats.size() == 2 and guest.lobby_seats.size() == 2)
	_check("cah_hero_tables_converge", converged)
	if converged:
		_check(
			"cah_hero_tables_are_byte_identical",
			var_to_bytes(host.lobby_seat_heroes) == var_to_bytes(guest.lobby_seat_heroes)
		)
		var host_roster: Array = host.build_lobby_roster()
		var guest_roster: Array = guest.build_lobby_roster()
		_check(
			"cah_launch_roster_carries_every_seats_heroes",
			host_roster.size() == 2
				and var_to_bytes(host_roster) == var_to_bytes(guest_roster)
				and (host_roster[0] as Dictionary).get("heroes", []).size() == 1
				and (host_roster[1] as Dictionary).get("heroes", []).size() == 1
		)
		_check(
			"cah_launch_roster_binds_heroes_to_seats",
			int((host_roster[0] as Dictionary).get("seat", -1)) == 0
				and int((host_roster[1] as Dictionary).get("seat", -1)) == 1
		)
	# THE HOST RULE. AllowCustomHeroes off empties the agreed table for EVERY
	# seat, on every peer, so a disabled match cannot half-apply.
	host.send_lobby_settings("bfme2.map.rivendell", 1200, 1.0, false, false)
	var settings_reached := _pump_pair(host, guest, func() -> bool:
		return not guest.custom_heroes_allowed())
	_check("cah_host_toggle_reaches_the_guest", settings_reached)
	_check(
		"ring_rule_and_seed_are_host_authoritative",
		settings_reached
			and host.ring_heroes_allowed() == guest.ring_heroes_allowed()
			and int(host.lobby_settings.get("logic_random_seed", 0)) != 0
			and int(host.lobby_settings.get("logic_random_seed", 0)) == int(guest.lobby_settings.get("logic_random_seed", -1))
	)
	_check(
		"lobby_settings_envelopes_are_byte_identical_with_ring_fields",
		settings_reached and var_to_bytes(host.lobby_settings) == var_to_bytes(guest.lobby_settings)
	)
	if settings_reached:
		var host_disabled: Array = host.build_lobby_roster()
		var guest_disabled: Array = guest.build_lobby_roster()
		_check(
			"cah_host_toggle_strips_every_seats_heroes",
			host_disabled.size() == 2
				and var_to_bytes(host_disabled) == var_to_bytes(guest_disabled)
				and (host_disabled[0] as Dictionary).get("heroes", []).is_empty()
				and (host_disabled[1] as Dictionary).get("heroes", []).is_empty()
		)
	host.send_lobby_settings("bfme2.map.rivendell", 1200, 1.0, false, true)
	_pump_pair(host, guest, func() -> bool: return guest.custom_heroes_allowed())
	# NEVER TRUST REMOTE BYTES. A non-canonical document (the same object with
	# unsorted keys) and an over-cap list are both refused outright.
	var rejected_before: int = host.rejected_packets
	host._receive_lobby_heroes(1, {"kind": "lobby.heroes", "heroes": ["{\"b\":1,\"a\":2}"]})
	var oversized: Array = []
	for _index in range(SessionScript.LOBBY_HEROES_PER_SEAT_MAX + 1):
		oversized.append(SessionScript.canonical_hero_document(profile_b))
	host._receive_lobby_heroes(1, {"kind": "lobby.heroes", "heroes": oversized})
	host._receive_lobby_heroes(1, {"kind": "lobby.heroes", "heroes": [42]})
	# A LIST THAT CANNOT FIT A PACKET MUST BE REFUSED, not accepted and then
	# dropped by the transport: the caller's "announce nothing rather than a
	# truncated set" fallback can only fire if the send says no.
	var fat := profile_b.duplicate(true)
	fat["name"] = "F".repeat(SessionScript.LOBBY_NAME_MAX)
	fat["awards"] = []
	for _index in range(4000):
		(fat["awards"] as Array).append("Award_%d" % _index)
	_check("cah_unsendable_hero_list_is_refused", not guest.send_lobby_heroes([fat]))
	# THE CAP MUST BE MEASURED IN THE PACKET'S OWN UNIT. A document built from
	# three-byte codepoints is under any character cap and three times over the
	# byte budget it was sized against; four such seats overflow the host's
	# rebroadcast and every guest is left on a stale table.
	var wide := profile_b.duplicate(true)
	wide["awards"] = []
	var wide_award := "".rpad(512, "中")
	while (
		JSON.stringify(wide, "", true).to_utf8_buffer().size()
		< SessionScript.LOBBY_HERO_BYTES_MAX
	):
		(wide["awards"] as Array).append(wide_award)
	var wide_document := SessionScript.canonical_hero_document(wide)
	_check(
		"cah_wide_utf8_hero_is_measured_in_bytes",
		wide_document.length() <= SessionScript.LOBBY_HERO_BYTES_MAX
			and wide_document.to_utf8_buffer().size() > SessionScript.LOBBY_HERO_BYTES_MAX
			and not guest.send_lobby_heroes([wide])
			and SessionScript.validated_hero_documents([wide_document]).is_empty()
	)
	_check(
		"cah_hero_caps_fit_one_packet",
		SessionScript.MAX_SEATS * SessionScript.LOBBY_HEROES_BYTES_MAX
			< SessionScript.MAX_PACKET_BYTES
	)
	_check(
		"cah_malformed_hero_payloads_fail_closed",
		host.rejected_packets == rejected_before + 3
			and var_to_bytes(host.lobby_seat_heroes.get(1, [])) == var_to_bytes([
				SessionScript.canonical_hero_document(profile_b)
			])
	)
	host.close()
	guest.close()


func _test_created_hero_lockstep(
	system: Dictionary, profile_a: Dictionary, profile_b: Dictionary
) -> void:
	var seat_rows: Array = [
		{"seat": 0, "team": 0, "faction": "men", "heroes": [SessionScript.canonical_hero_document(profile_a)]},
		{"seat": 1, "team": 1, "faction": "men", "heroes": [SessionScript.canonical_hero_document(profile_b)]},
	]
	var fieldable := {"FixtureRosterHero": _cah_roster_anchor()}
	# Each peer derives the roster from the AGREED bytes, independently.
	var peer_a_documents := CahHeroes.seat_roster_documents(system, fieldable, seat_rows, "men")
	var peer_b_documents := CahHeroes.seat_roster_documents(
		system, fieldable, [seat_rows[1], seat_rows[0]], "men"
	)
	_check("cah_both_seats_reach_the_roster", peer_a_documents.size() == 2)
	_test_created_hero_id_hijack(system, profile_a, fieldable)
	_test_created_hero_id_shape(system, profile_a)
	_check(
		"cah_peers_derive_byte_identical_rosters",
		var_to_bytes(peer_a_documents) == var_to_bytes(peer_b_documents)
	)
	var object_a := "CreateAHero__%s" % CAH_SEAT_A_ID
	var object_b := "CreateAHero__%s" % CAH_SEAT_B_ID
	var record_a: Dictionary = (
		((peer_a_documents.get(object_a, {}) as Dictionary).get("registration", {}) as Dictionary)
			.get("createAHero", {}) as Dictionary
	)
	var record_b: Dictionary = (
		((peer_a_documents.get(object_b, {}) as Dictionary).get("registration", {}) as Dictionary)
			.get("createAHero", {}) as Dictionary
	)
	_check(
		"cah_ordinals_derive_from_seat_and_hero_id",
		int(record_a.get("ownerTeam", -1)) == 0 and int(record_b.get("ownerTeam", -1)) == 1
			and _roster_ordinal(peer_a_documents[object_a]) != _roster_ordinal(peer_a_documents[object_b])
	)

	var runtimes: Dictionary = fieldable.duplicate(true)
	for object_id in peer_a_documents:
		runtimes[String(object_id)] = peer_a_documents[object_id]
	var sim_a = _cah_sim(runtimes)
	var sim_b = _cah_sim(runtimes)
	if sim_a == null or sim_b == null:
		return
	var unit_a := Adapter.runtime_unit_id(peer_a_documents[object_a] as Dictionary)
	var unit_b := Adapter.runtime_unit_id(peer_a_documents[object_b] as Dictionary)
	_check(
		"cah_ownership_refuses_the_other_seats_hero",
		String(sim_a.queue_unit(0, 1, unit_b).get("reason", "")) == "created-hero-not-owned"
			and String(sim_a.queue_unit(1, 2, unit_a).get("reason", "")) == "created-hero-not-owned"
	)
	_check(
		"cah_producer_surface_hides_the_other_seats_hero",
		_cah_owned_production(sim_a, 0).has(unit_a)
			and not _cah_owned_production(sim_a, 0).has(unit_b)
			and _cah_owned_production(sim_a, 1).has(unit_b)
	)
	# BOTH seats' purchases execute on BOTH sims, through the command queue, in
	# the canonical (team, seq) order the network applies them in.
	for sim in [sim_a, sim_b]:
		sim.submit_command({
			"tick": sim.tick_index + 2, "team": 0, "seq": 1, "type": "queue_unit",
			"args": {"producer": 1, "unit_type": unit_a},
		})
		sim.submit_command({
			"tick": sim.tick_index + 2, "team": 1, "seq": 2, "type": "queue_unit",
			"args": {"producer": 2, "unit_type": unit_b},
		})
	var hashes_equal := true
	for _tick in range(1200):
		sim_a.tick()
		sim_b.tick()
		if sim_a.tick_index % 30 == 0:
			hashes_equal = hashes_equal and sim_a.state_hash() == sim_b.state_hash()
	_check("cah_two_seat_state_hashes_stay_identical", hashes_equal)
	var spawned_a := _cah_spawned(sim_a, unit_a)
	var spawned_b := _cah_spawned(sim_a, unit_b)
	_check(
		"cah_each_seat_fields_its_own_hero",
		spawned_a != 0 and spawned_b != 0
			and int((sim_a.entity(spawned_a) as Dictionary).get("team", -1)) == 0
			and int((sim_a.entity(spawned_b) as Dictionary).get("team", -1)) == 1
	)


func _test_created_hero_id_hijack(
	system: Dictionary, profile_a: Dictionary, fieldable: Dictionary
) -> void:
	## THE HIJACK. Every seat sees every other seat's hero ids in the host's
	## table, and the object id a created hero occupies is derived from that id.
	## A guest that re-announces the id of a hero on a LOWER seat would, walking
	## seats ascending, overwrite the victim's document and delete their hero
	## from the match - silently, on every peer, so not even a desync would
	## reveal it. First claim wins and the loser is refused BY NAME.
	var thief := profile_a.duplicate(true)
	thief["name"] = "Thief"
	var rows: Array = [
		{
			"seat": 0, "team": 0, "faction": "men",
			"heroes": [SessionScript.canonical_hero_document(profile_a)],
		},
		{
			"seat": 1, "team": 1, "faction": "men",
			"heroes": [SessionScript.canonical_hero_document(thief)],
		},
	]
	var admission := CahHeroes.admitted_seat_heroes(system, rows)
	var admitted: Array = admission.get("admitted", []) as Array
	var refusals: Array = admission.get("refusals", []) as Array
	var claimed_refusal := false
	for refusal in refusals:
		if String(refusal).contains(CAH_SEAT_A_ID) and String(refusal).contains("seat 1"):
			claimed_refusal = true
	_check(
		"cah_hero_id_hijack_is_refused",
		admitted.size() == 1
			and int((admitted[0] as Dictionary).get("seat", -1)) == 0
			and claimed_refusal
	)
	var documents := CahHeroes.seat_roster_documents(system, fieldable, rows, "men")
	_check(
		"cah_hijacked_victim_keeps_its_hero",
		documents.size() == 1
			and documents.has("CreateAHero__%s" % CAH_SEAT_A_ID)
			and int(
				(((documents["CreateAHero__%s" % CAH_SEAT_A_ID] as Dictionary)
					.get("registration", {}) as Dictionary)
					.get("createAHero", {}) as Dictionary).get("ownerTeam", -1)
			) == 0
	)


func _test_created_hero_id_shape(system: Dictionary, profile_a: Dictionary) -> void:
	## A hero id reaches the object id, the command id, the sim's unit-type
	## space, the exclusion log and the filesystem. Remote bytes may not put
	## anything in it but the shape this module's own generator produces.
	var hostile := [
		"../../etc",
		"a/b\nc",
		"__engine__/HERO_BUILD/MenFortress",
		"A".repeat(24),
		"abc",
		"a".repeat(4000),
		"aaaaaaaaaaaa1111aaaa111",
	]
	var all_refused := true
	for candidate in hostile:
		var probe := profile_a.duplicate(true)
		probe["heroId"] = String(candidate)
		if CahHeroes.validate_profile(system, probe).is_empty():
			all_refused = false
	_check("cah_hostile_hero_ids_are_refused", all_refused)
	_check(
		"cah_generated_hero_ids_are_accepted",
		CahHeroes.validate_profile(system, profile_a).is_empty()
	)


func _roster_ordinal(document: Dictionary) -> int:
	var production: Array = (
		(document.get("registration", {}) as Dictionary).get("production", []) as Array
	)
	return int((production[0] as Dictionary).get("rosterOrdinal", -1)) if not production.is_empty() else -1


func _cah_spawned(sim, unit_type: String) -> int:
	for entity_id in sim.entity_ids():
		if String((sim.entity(entity_id) as Dictionary).get("unit_type", "")) == unit_type:
			return int(entity_id)
	return 0


static func _cah_fixture_manifest() -> Dictionary:
	var manifest: Dictionary = preload("res://src/retail_slice/retail_faction_manifest.gd").default_manifest()
	manifest["spawn_roster"] = []
	return manifest


func _cah_sim(runtimes: Dictionary):
	var sim = SimScript.new()
	sim._apply_gameplay_rules({
		# Q80: the 8 core manifest tables are required; this CAH fixture runs
		# on the labeled SYNTHETIC default_manifest() with an empty spawn
		# roster (spawn_initial_battalions is false and unit_rules is empty,
		# so the default gondor roster would fail entry validation unused).
		"faction_manifest": _cah_fixture_manifest(),
		"enable_base_loop": true,
		"playable_unit_runtimes": runtimes,
		"producer_kind_by_source_object": {CAH_PRODUCER: CAH_PRODUCER_KIND},
		"unit_rules": {},
		"starting_resources": 100000,
		"command_point_cap": 10000,
		"source_map_transform_scale": 0.1,
		"spawn_initial_battalions": false,
	})
	_check("cah_sim_configures", String(sim.configuration_error) == "", String(sim.configuration_error))
	if String(sim.configuration_error) != "":
		return null
	sim.ai_enabled = false
	sim.entities.clear()
	sim.structures.clear()
	sim.expansion_pads.clear()
	for producer_id in [1, 2]:
		sim.structures[producer_id] = {
			"id": producer_id,
			"team": producer_id - 1,
			"health": 10000,
			"construction_progress": 1.0,
			"structure_kind": CAH_PRODUCER_KIND,
			"position": Vector2(0.0, 40.0 * float(producer_id)),
			"rally": Vector2(10.0, 40.0 * float(producer_id)),
			# The full surface, so the OWNERSHIP gate is what refuses a
			# cross-seat purchase rather than the production list beating it
			# to the answer.
			"production": sim.production_rule_ids(),
			"queue": [],
			"completed_upgrades": [],
		}
	return sim


func _cah_owned_production(sim, team: int) -> Array:
	var offered: Array = []
	for unit_type in sim.production_rule_ids():
		if sim.created_hero_owner_team(String(unit_type)) in [-1, team]:
			offered.append(String(unit_type))
	return offered


func _cah_roster_anchor() -> Dictionary:
	## A retail hero on the hero-roster surface: the producer a created hero
	## hangs off, and the ordinal it must not collide with.
	return {
		"schema": "openbfme.playable-unit-runtime",
		"schemaVersion": 0,
		"objectId": "FixtureRosterHero",
		"category": "hero",
		"descriptorSha256": "5".repeat(64),
		"recipeSha256": "6".repeat(64),
		"resourceIds": [],
		"registration": {
			"production": [{
				"producerObjectId": CAH_PRODUCER,
				"commandSetId": "__engine__/BuildableHeroesMP",
				"commandId": "__engine__/HERO_BUILD/FixtureRosterHero",
				"surface": "hero-roster",
				"slot": 0,
				"rosterOrdinal": 1,
				"prerequisites": [],
				"commandSetTransition": [],
			}],
			"composition": {
				"containerObjectId": "FixtureRosterHero",
				"primaryMemberObjectId": "FixtureRosterHero",
				"members": [{"objectId": "FixtureRosterHero", "count": 1}],
			},
			"simulation": {
				"displayName": "OBJECT:FixtureRosterHero",
				"buildCost": 1000,
				"buildTimeSeconds": 5.0,
				"commandPoints": 10,
				"memberCount": 1,
				"memberHealth": 2000,
				"speed": 50.0,
				"visionRange": 150.0,
				"combat": {
					"attackRange": 40.0, "minimumAttackRange": 0.0,
					"delayBetweenShotsMs": 1000.0, "preAttackDelayMs": 250.0,
					"firingDurationMs": 250.0, "damage": 100,
				},
				"movement": {"acceleration": 210.0, "braking": 210.0, "turnRateDegreesPerSecond": 720.0},
				"formation": {"memberCount": 1, "positions": [{"x": 0.0, "y": 0.0}]},
			},
			"ui": {
				"portraitImageIds": ["HIFixture"],
				"commands": [{
					"commandId": "__engine__/HERO_BUILD/FixtureRosterHero",
					"fields": {
						"ButtonImage": ["HIFixture"],
						"TextLabel": ["OBJECT:FixtureRosterHero"],
						"DescriptLabel": ["CONTROLBAR:FixtureRosterHero"],
					},
					"audioRoutes": [],
				}],
			},
		},
	}


func _pump_pair(host, guest, condition: Callable) -> bool:
	for iteration in range(20000):
		host.poll()
		guest.poll()
		if condition.call():
			return true
		if iteration % 100 == 99:
			OS.delay_msec(1)
	return false


func _pump_handshake(iteration_budget: int) -> bool:
	for iteration in range(iteration_budget):
		host_session.poll()
		client_session.poll()
		if host_session.handshake_complete and client_session.handshake_complete:
			return true
		if iteration % 100 == 99:
			OS.delay_msec(1)
	return false


func _pump_once(poll_host: bool, poll_client: bool) -> void:
	if poll_host:
		host_session.poll()
	if poll_client:
		client_session.poll()
	if poll_host:
		host_session.advance_if_ready()
	if poll_client:
		client_session.advance_if_ready()
	if poll_host:
		host_session.poll()
	if poll_client:
		client_session.poll()


func _pump_to_tick(target_tick: int, iteration_budget: int, track_selection: bool) -> bool:
	for _iteration in range(iteration_budget):
		if track_selection:
			_apply_divergent_selection()
		_pump_once_to_limit(target_tick)
		_sample_equal_hash()
		if host_session.desynced or client_session.desynced:
			return false
		if host_sim.tick_index >= target_tick and client_sim.tick_index >= target_tick \
			and host_session._hash_barrier_tick < 0 and client_session._hash_barrier_tick < 0:
			return true
	return false


func _apply_divergent_selection() -> void:
	var host_selection: Array[int] = [1, 2]
	var client_selection: Array[int] = [101]
	host_sim.select_many(host_selection)
	client_sim.select_many(client_selection)


func _pump_once_to_limit(target_tick: int) -> void:
	host_session.poll()
	client_session.poll()
	if host_sim.tick_index < target_tick:
		host_session.advance_if_ready()
	if client_sim.tick_index < target_tick:
		client_session.advance_if_ready()
	host_session.poll()
	client_session.poll()


func _sample_equal_hash() -> void:
	if host_sim.tick_index != client_sim.tick_index or host_sim.tick_index <= 0 or host_sim.tick_index % 30 != 0:
		return
	if _sampled_hash_ticks.has(host_sim.tick_index):
		return
	_sampled_hash_ticks[host_sim.tick_index] = true
	_network_hashes_equal = _network_hashes_equal and host_sim.state_hash() == client_sim.state_hash()


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("RETAIL_LOCKSTEP_NET PASS %s" % name)
	else:
		failed += 1
		printerr("RETAIL_LOCKSTEP_NET FAIL %s%s" % [name, " (%s)" % detail if detail != "" else ""])


func _finish() -> void:
	if host_session != null:
		host_session.close()
	if client_session != null:
		client_session.close()
	print("RETAIL_LOCKSTEP_NET_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)
