extends SceneTree

## Eight-player lockstep contract (F3), over real ENet loopback peers.
##
## Two phases, deliberately separated:
##   PHASE A runs sim-less sessions and covers the LOBBY: seat assignment,
##     join/leave, a full game refusing a ninth peer, the all-seats-confirm
##     launch gate, and byte-identical launch rosters on all eight peers.
##   PHASE B attaches a real RetailSliceSim to each of the eight peers and
##     covers the MATCH: deterministic command ordering across every peer,
##     host relay of guest-to-guest input, and agreeing state hashes.
##
## The central assertion is ordering. Every peer must apply every peer's
## commands for tick T in one identical order; the canonical rule is
## (team ascending, then seq ascending). The test proves it three ways: the
## pure sorter is stable under shuffled input, every peer's ordered view of
## each tick is byte-identical, and the eight simulations agree on their state
## hash after ticking — which is only possible if they applied the same
## commands in the same order.

const SimScript = preload("res://src/retail_slice/retail_slice_sim.gd")
const SessionScript = preload("res://src/retail_slice/retail_lockstep_session.gd")

const SEATS := 8
const PUMP_BUDGET := 60000
## Commands are issued after a warmup so they sit comfortably inside
## RetailLockstepSession.COMMAND_HISTORY_TICKS at assertion time.
const COMMAND_TICK := 60
const TARGET_TICK := 90

var passed := 0
var failed := 0
var _sessions: Array = []
var _sims: Array = []


func _initialize() -> void:
	call_deferred("_run")


## The rectangle main_menu actually gives the lobby panel: it reuses the NETWORK
## flyout's size from scenes/boot.tscn. Eight seat rows have to fit in THIS, not
## in whatever roomier size a test happens to construct.
const REAL_PANEL_SIZE := Vector2(800, 504)


func _run() -> void:
	_test_canonical_order_is_pure()
	await _test_lobby_panel_fits_the_real_panel()
	_test_lobby_phase()
	_test_lockstep_phase()
	_finish()


## Eight rows plus settings, chat, status and the action row must all fit
## inside the real panel without overlapping. Absolute positioning gives no
## error when it overflows — it just draws off the bottom edge — so the
## geometry is asserted rather than eyeballed.
func _test_lobby_panel_fits_the_real_panel() -> void:
	var LobbyScript = load("res://src/ui/multiplayer_lobby.gd")
	var panel: Panel = LobbyScript.new()
	panel.size = REAL_PANEL_SIZE
	root.add_child(panel)
	await process_frame
	_check("lobby_builds_one_row_per_seat",
		panel.remote_name_labels.size() == SEATS - 1 \
			and panel.remote_ready_checks.size() == SEATS - 1 \
			and panel.remote_team_opts.size() == SEATS - 1,
		"rows=%d" % panel.remote_name_labels.size())
	var overflowing: Array[String] = []
	for child in panel.get_children():
		if not (child is Control):
			continue
		var control := child as Control
		var bottom := control.position.y + control.size.y
		var right := control.position.x + control.size.x
		if bottom > REAL_PANEL_SIZE.y or right > REAL_PANEL_SIZE.x \
			or control.position.x < 0.0 or control.position.y < 0.0:
			overflowing.append("%s@%s+%s" % [control.name, str(control.position), str(control.size)])
	_check("every_lobby_control_fits_inside_the_real_panel", overflowing.is_empty(),
		", ".join(overflowing).substr(0, 300))
	# Vertical bands: seat rows, then chat, then status, then the action row —
	# none running into the next. Seat rows must also not overlap each OTHER,
	# which is what a control height smaller than Godot's themed minimum would
	# silently cause.
	var last_row_bottom: float = panel.remote_team_opts[SEATS - 2].position.y \
		+ panel.remote_team_opts[SEATS - 2].size.y
	var rows_disjoint := true
	for row in range(1, SEATS - 1):
		var previous: OptionButton = panel.remote_team_opts[row - 1]
		if panel.remote_team_opts[row].position.y < previous.position.y + previous.size.y:
			rows_disjoint = false
	_check("seat_rows_do_not_overlap_each_other", rows_disjoint,
		"pitch=%.0f height=%.0f" % [panel.ROW_PITCH, panel.remote_team_opts[0].size.y])
	var chat_top: float = panel.chat_log_label.position.y
	var chat_bottom: float = panel.chat_edit.position.y + panel.chat_edit.size.y
	var status_top: float = panel.status_label.position.y
	var status_bottom: float = status_top + panel.status_label.size.y
	var buttons_top: float = panel.launch_button.position.y
	_check("lobby_bands_do_not_collide",
		last_row_bottom <= chat_top and chat_bottom <= status_top \
			and status_bottom <= buttons_top,
		"rows=%.0f chat=%.0f..%.0f status=%.0f..%.0f buttons=%.0f" % [
			last_row_bottom, chat_top, chat_bottom, status_top, status_bottom, buttons_top])
	# The settings block sits BESIDE the seat rows; it must clear the widest
	# row column instead of sitting on top of it.
	var rows_right_edge: float = panel.local_ready_check.position.x + panel.local_ready_check.size.x
	var settings_left: float = panel.map_opt.position.x
	var settings_bottom: float = panel.build_mode_opt.position.y + panel.build_mode_opt.size.y
	_check("settings_column_clears_the_seat_rows",
		settings_left >= rows_right_edge and settings_bottom <= chat_top,
		"rows_right=%.0f settings_left=%.0f settings_bottom=%.0f chat_top=%.0f" % [
			rows_right_edge, settings_left, settings_bottom, chat_top])
	# The 1v1 aliases must still point at row 1, or the two-player callers that
	# read remote_name_label directly would silently go blind.
	_check("primary_remote_aliases_point_at_row_one",
		panel.remote_name_label == panel.remote_name_labels[0] \
			and panel.remote_army_label == panel.remote_army_labels[0] \
			and panel.remote_ready_check == panel.remote_ready_checks[0])
	panel.queue_free()
	await process_frame


# --- phase 0: the ordering rule itself ---------------------------------------

## The sorter must be a TOTAL order that ignores input order entirely: the same
## command set shuffled any which way must always come out the same.
func _test_canonical_order_is_pure() -> void:
	var commands: Array = []
	for seat in SEATS:
		var team: int = SessionScript.SEAT_TEAM_IDS[seat]
		for seq in 3:
			commands.append({"tick": 10, "team": team, "seq": seq, "type": "issue_stop", "args": {}})
	var forward := SessionScript.canonical_command_order(commands)
	var reversed_input := commands.duplicate(true)
	reversed_input.reverse()
	var backward := SessionScript.canonical_command_order(reversed_input)
	# A deterministic shuffle (no RNG: the assertion must not depend on a seed).
	var interleaved: Array = []
	for index in commands.size():
		interleaved.append(commands[(index * 7 + 3) % commands.size()])
	var shuffled := SessionScript.canonical_command_order(interleaved)
	var ascending := true
	var previous_team := -1
	var previous_seq := -1
	for command_value in forward:
		var command := command_value as Dictionary
		var team := int(command["team"])
		var seq := int(command["seq"])
		if team < previous_team or (team == previous_team and seq <= previous_seq):
			ascending = false
			break
		previous_team = team
		previous_seq = seq
	_check("canonical_order_is_total_and_input_independent",
		ascending and forward.size() == SEATS * 3 \
			and var_to_bytes(forward) == var_to_bytes(backward) \
			and var_to_bytes(forward) == var_to_bytes(shuffled))
	# The seat -> team ladder must stay strictly increasing, or "order by team"
	# and "order by seat" would stop being the same statement.
	var ladder_increasing := true
	for seat in range(1, SessionScript.SEAT_TEAM_IDS.size()):
		if SessionScript.SEAT_TEAM_IDS[seat] <= SessionScript.SEAT_TEAM_IDS[seat - 1]:
			ladder_increasing = false
	_check("seat_team_ladder_strictly_increasing_and_skips_neutral",
		ladder_increasing and SessionScript.SEAT_TEAM_IDS.size() == SEATS \
			and not SessionScript.SEAT_TEAM_IDS.has(2),
		str(SessionScript.SEAT_TEAM_IDS))


# --- phase A: lobby ----------------------------------------------------------

func _test_lobby_phase() -> void:
	var host = SessionScript.new()
	var host_ok: bool = host.host(0) == OK
	var sessions: Array = [host]
	for _index in range(SEATS - 1):
		var guest = SessionScript.new()
		guest.join("127.0.0.1", host.bound_port)
		sessions.append(guest)
	var seated: bool = host_ok and _pump(sessions, func() -> bool:
		if host.seat_count() != SEATS:
			return false
		for index in range(1, sessions.size()):
			if not bool(sessions[index].handshake_complete):
				return false
		return true)
	var seats_dense := host.occupied_seats() == [0, 1, 2, 3, 4, 5, 6, 7]
	var guest_seats_unique: Array[int] = []
	for index in range(1, sessions.size()):
		guest_seats_unique.append(int(sessions[index].local_seat))
	guest_seats_unique.sort()
	_check("eight_peers_seated_densely_and_uniquely",
		seated and seats_dense and guest_seats_unique == [1, 2, 3, 4, 5, 6, 7],
		"seats=%s guests=%s" % [str(host.occupied_seats()), str(guest_seats_unique)])
	if not seated:
		for session in sessions:
			session.close()
		return

	# Each seat's team must be the ladder entry, on every peer's own view.
	var teams_ok := true
	for session in sessions:
		if int(session.local_team) != SessionScript.team_for_seat(int(session.local_seat)):
			teams_ok = false
		for row_value in session.lobby_seats:
			var row := row_value as Dictionary
			if int(row["team"]) != SessionScript.team_for_seat(int(row["seat"])):
				teams_ok = false
	_check("every_seat_maps_to_its_ladder_team", teams_ok)

	# A ninth peer must be refused WITH A REASON, not left to time out.
	var overflow = SessionScript.new()
	overflow.join("127.0.0.1", host.bound_port)
	var overflow_sessions := sessions.duplicate()
	overflow_sessions.append(overflow)
	var overflow_refused: bool = _pump(overflow_sessions, func() -> bool: return String(overflow.join_refused_reason) != "")
	_check("ninth_peer_refused_with_a_stated_reason",
		overflow_refused and String(overflow.join_refused_reason).to_lower().contains("full") \
			and host.seat_count() == SEATS,
		String(overflow.join_refused_reason))
	overflow.close()

	# Leaving in the lobby frees the seat, and everyone is told.
	var leaver = sessions[SEATS - 1]
	var freed_seat := int(leaver.local_seat)
	leaver.close()
	sessions.remove_at(SEATS - 1)
	var seat_freed: bool = _pump(sessions, func() -> bool:
		if host.seat_count() != SEATS - 1:
			return false
		for index in range(1, sessions.size()):
			if sessions[index].seat_count() != SEATS - 1:
				return false
		return true)
	_check("leaving_frees_the_seat_on_every_peer", seat_freed and not host.occupied_seats().has(freed_seat),
		"host_seats=%s" % str(host.occupied_seats()))

	# ...and the freed seat is handed to the next joiner.
	var replacement = SessionScript.new()
	replacement.join("127.0.0.1", host.bound_port)
	sessions.append(replacement)
	var refilled: bool = _pump(sessions, func() -> bool:
		return host.seat_count() == SEATS and bool(replacement.handshake_complete))
	_check("freed_seat_is_reused_by_the_next_joiner",
		refilled and int(replacement.local_seat) == freed_seat,
		"seat=%d expected=%d" % [int(replacement.local_seat), freed_seat])

	# --- profiles + the all-seats-confirm gate --------------------------------
	for index in sessions.size():
		var session = sessions[index]
		var seat := int(session.local_seat)
		session.send_lobby_profile(
			"Player %d" % (seat + 1),
			SessionScript.LOBBY_FACTION_IDS[seat % SessionScript.LOBBY_FACTION_IDS.size()],
			seat % SessionScript.LOBBY_COLOR_NAMES.size(),
			false)
	var announced: bool = _pump(sessions, func() -> bool:
		if host.lobby_seats.size() != SEATS:
			return false
		for row_value in host.lobby_seats:
			if not bool((row_value as Dictionary).get("announced", false)):
				return false
		return true)
	_check("all_eight_profiles_reach_the_host", announced)
	_check("launch_refused_while_nobody_is_ready",
		not host.send_lobby_launch() and not host.all_seats_ready() \
			and host.seats_not_ready().size() == SEATS)

	# Ready everybody except ONE seat: the gate must still hold, and the holdout
	# must be named rather than the launch just silently doing nothing.
	var holdout_seat := -1
	for index in sessions.size():
		var session = sessions[index]
		var seat := int(session.local_seat)
		if seat == SEATS - 2:
			holdout_seat = seat
			continue
		session.send_lobby_profile(
			"Player %d" % (seat + 1),
			SessionScript.LOBBY_FACTION_IDS[seat % SessionScript.LOBBY_FACTION_IDS.size()],
			seat % SessionScript.LOBBY_COLOR_NAMES.size(),
			true)
	var all_but_one: bool = _pump(sessions, func() -> bool: return host.seats_not_ready().size() == 1)
	_check("launch_refused_until_the_last_seat_confirms",
		all_but_one and not host.send_lobby_launch() \
			and host.seats_not_ready() == ["Player %d" % (holdout_seat + 1)],
		str(host.seats_not_ready()))

	for index in sessions.size():
		var session = sessions[index]
		if int(session.local_seat) != holdout_seat:
			continue
		session.send_lobby_profile(
			"Player %d" % (holdout_seat + 1),
			SessionScript.LOBBY_FACTION_IDS[holdout_seat % SessionScript.LOBBY_FACTION_IDS.size()],
			holdout_seat % SessionScript.LOBBY_COLOR_NAMES.size(),
			true)
	var everyone_ready: bool = _pump(sessions, func() -> bool: return bool(host.all_seats_ready()))
	var launched: bool = everyone_ready and host.send_lobby_launch()
	var all_accepted: bool = launched and _pump(sessions, func() -> bool:
		for index in range(1, sessions.size()):
			if not bool(sessions[index].lobby_launch_received):
				return false
		return true)
	_check("launch_only_after_every_seat_confirms", everyone_ready and launched and all_accepted)

	var roster_bytes := var_to_bytes(host.lobby_launch_roster)
	var rosters_identical := true
	for index in range(1, sessions.size()):
		if var_to_bytes(sessions[index].lobby_launch_roster) != roster_bytes:
			rosters_identical = false
	var roster_teams: Array = []
	for descriptor_value in host.lobby_launch_roster:
		roster_teams.append(int((descriptor_value as Dictionary).get("team", -1)))
	_check("launch_roster_byte_identical_on_all_eight_peers",
		all_accepted and rosters_identical and host.lobby_launch_roster.size() == SEATS \
			and roster_teams == SessionScript.SEAT_TEAM_IDS,
		"teams=%s" % str(roster_teams))

	# Late join is out of scope and must be refused explicitly.
	var latecomer = SessionScript.new()
	latecomer.join("127.0.0.1", host.bound_port)
	var late_sessions := sessions.duplicate()
	late_sessions.append(latecomer)
	var late_refused: bool = _pump(late_sessions, func() -> bool: return String(latecomer.join_refused_reason) != "")
	_check("late_join_after_launch_refused_with_a_reason",
		late_refused and String(latecomer.join_refused_reason).to_lower().contains("already started"),
		String(latecomer.join_refused_reason))
	latecomer.close()
	for session in sessions:
		session.close()


# --- phase B: lockstep -------------------------------------------------------

func _test_lockstep_phase() -> void:
	for _index in range(SEATS):
		_sims.append(_make_sim())
	var host = SessionScript.new(_sims[0])
	var host_ok: bool = host.host(0) == OK
	_sessions = [host]
	for index in range(1, SEATS):
		var guest = SessionScript.new(_sims[index])
		guest.join("127.0.0.1", host.bound_port)
		_sessions.append(guest)
	# Nobody may tick until the whole agreed roster is seated.
	for session in _sessions:
		session.required_seat_count = SEATS
	var seated: bool = host_ok and _pump(_sessions, func() -> bool:
		for session in _sessions:
			if not bool(session.handshake_complete) or session.seat_count() != SEATS:
				return false
		return true)
	_check("eight_sim_peers_all_handshake", seated)
	if not seated:
		return

	# Tick everyone to a common point FIRST, so the commands below land inside
	# the retained command-history window rather than being pruned before the
	# ordering assertion can look at them.
	var warmed: bool = _pump_ticks(COMMAND_TICK)
	_check("eight_peers_reach_a_common_warmup_tick", warmed,
		"ticks=%s" % str(_sims.map(func(s): return s.tick_index)))
	if not warmed:
		return

	# One command per seat, so every seat contributes to the ordering, plus a
	# second command from seat 3 whose journey to seat 5 can only be via the
	# host's relay (guests are not connected to each other).
	for index in _sessions.size():
		var session = _sessions[index]
		session.submit_local("issue_move", {"ids": [1], "destination": Vector2(float(index) - 4.0, 2.0)})
	_sessions[3].submit_local("issue_stop", {"ids": [1]})

	var reached: bool = _pump_ticks(TARGET_TICK)
	var no_desync := true
	for session in _sessions:
		if bool(session.desynced):
			no_desync = false
	_check("eight_peers_tick_to_target_without_desync", reached and no_desync,
		"ticks=%s" % str(_sims.map(func(s): return s.tick_index)))

	var reference_hash: String = _sims[0].state_hash()
	var hashes_agree := true
	for sim in _sims:
		if sim.state_hash() != reference_hash:
			hashes_agree = false
	_check("all_eight_simulations_agree_on_state_hash", hashes_agree, reference_hash.substr(0, 16))

	# THE ordering assertion: for every tick still in the history window, each
	# peer's canonical view must be byte-identical to seat 0's.
	var compared_ticks := 0
	var command_ticks := 0
	var orders_identical := true
	for tick in range(1, TARGET_TICK + 1):
		var reference: Array = _sessions[0].ordered_commands_for_tick(tick)
		if reference.is_empty():
			continue
		command_ticks += 1
		var reference_bytes := var_to_bytes(reference)
		for index in range(1, _sessions.size()):
			compared_ticks += 1
			if var_to_bytes(_sessions[index].ordered_commands_for_tick(tick)) != reference_bytes:
				orders_identical = false
	_check("every_peer_orders_each_tick_identically",
		orders_identical and command_ticks > 0 and compared_ticks == command_ticks * (SEATS - 1),
		"command_ticks=%d comparisons=%d" % [command_ticks, compared_ticks])

	# Every seat's command must actually be in the shared order (nothing was
	# quietly dropped), and it must be sorted by the ladder team.
	var seen_teams: Dictionary = {}
	var sorted_by_team := true
	for tick in range(1, TARGET_TICK + 1):
		var ordered: Array = _sessions[0].ordered_commands_for_tick(tick)
		var previous := -1
		for command_value in ordered:
			var command := command_value as Dictionary
			seen_teams[int(command["team"])] = true
			if int(command["team"]) < previous:
				sorted_by_team = false
			previous = int(command["team"])
	var all_seats_present := true
	for team in SessionScript.SEAT_TEAM_IDS:
		if not seen_teams.has(team):
			all_seats_present = false
	_check("all_eight_seats_commands_present_and_team_sorted",
		all_seats_present and sorted_by_team, str(seen_teams.keys()))

	# Relay proof: seat 3 issued two commands and is not connected to seat 5,
	# so seat 5 can only hold them because the host forwarded them.
	var relayed := 0
	var seat_three_team: int = SessionScript.team_for_seat(3)
	for tick in range(1, TARGET_TICK + 1):
		for command_value in _sessions[5].ordered_commands_for_tick(tick):
			if int((command_value as Dictionary)["team"]) == seat_three_team:
				relayed += 1
	_check("guest_to_guest_commands_arrive_via_host_relay", relayed == 2, "relayed=%d" % relayed)


# --- harness -----------------------------------------------------------------

func _pump(sessions: Array, condition: Callable) -> bool:
	for iteration in range(PUMP_BUDGET):
		for session in sessions:
			session.poll()
		if condition.call():
			return true
		if iteration % 100 == 99:
			OS.delay_msec(1)
	return false


func _pump_ticks(target_tick: int) -> bool:
	for iteration in range(PUMP_BUDGET):
		for session in _sessions:
			session.poll()
		for session in _sessions:
			session.advance_if_ready()
		for session in _sessions:
			session.poll()
		var done := true
		for index in _sims.size():
			if _sims[index].tick_index < target_tick or _sessions[index]._hash_barrier_tick >= 0:
				done = false
		if done:
			return true
		for session in _sessions:
			if bool(session.desynced):
				return false
		if iteration % 100 == 99:
			OS.delay_msec(1)
	return false


func _make_sim():
	## The accepted Step-1 fixture, verbatim from retail_lockstep_network_runner:
	## compact real gameplay, deterministic setup({}, {}), no presentation state.
	var sim = SimScript.new()
	sim._rules = _harness_rules()
	sim.setup({}, {})
	sim.ai_enabled = false
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
	return {"faction_manifest": _q80_harness_manifest(), 
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


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("MP_EIGHT_PEER PASS %s" % name)
	else:
		failed += 1
		printerr("MP_EIGHT_PEER FAIL %s%s" % [name, " (%s)" % detail if detail != "" else ""])


func _finish() -> void:
	for session in _sessions:
		session.close()
	print("MP_EIGHT_PEER_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


static func _q80_harness_manifest() -> Dictionary:
	# Q80: labeled SYNTHETIC default_manifest() for this harness (its unit
	# rules cover the default roster).
	return preload("res://src/retail_slice/retail_faction_manifest.gd").default_manifest()
