extends SceneTree

## WAR OF THE RING, FROM THE MAIN MENU AND BACK, over a REAL living-world
## document.
##
## `wotr_battle_bridge_runner.gd` proves the strategic->tactical chain on an
## authored fixture. It proves nothing about whether a player can reach it, and
## until now nothing could: there was no scene, no screen and no menu entry.
## This runner drives the honest production boundary and then a labelled future
## plumbing seam:
##
##   1. the document is FOUND and a real campaign is seated
##   2. the SCREEN offers only legal staging regions and targets
##   3. production ATTACK requests RTS and is DENIED by the six named gaps before
##      a commitment enters the strategic hash or a launcher signal is emitted
##   4. a test-only, direct State.begin_battle() admits the exact synthetic RTS
##      commitment solely to exercise handoff/report scene-boundary plumbing
##   5. the generic tactical harness runs that synthetic configuration; this is
##      NOT current production reachability or War of the Ring simulation parity
##   6. its synthetic result proves the existing strategic settlement plumbing
##   7. with NO document the menu entry REFUSES loudly rather than inventing one
##
## The future-admission leg never weakens tactical_admission(), forges receipt
## evidence, or claims that the current production ATTACK can launch RTS.
##
## WITHOUT A DOCUMENT this runner exits 3, exactly like
## `wotr_livingworld_pack_runner.gd`, because legs 1-7 have nothing real to run
## against and a green result on absence would mean nothing. Leg 8 - the refusal
## - is proven inside the same run by clearing the discovery environment for the
## duration of one menu boot, so it is never the reason the run is short.
##
## THE TACTICAL HARNESS is the bridge runner's: a real `RetailSliceSim` on its
## own fallback map geometry with authored unit rules. The slice SCENE (art,
## camera, HUD) needs a cooked map and a content pack and is not what this proves;
## what it proves is that the roster and rules the simulation was configured from
## came out of the commitment and nowhere else.

const SessionScript = preload("res://src/wotr/wotr_session.gd")
const StateScript = preload("res://src/wotr/wotr_state.gd")
const WorldScript = preload("res://src/wotr/wotr_world.gd")
const BattleScript = preload("res://src/wotr/wotr_battle.gd")
const HandoffScript = preload("res://src/wotr/wotr_handoff.gd")
const ScreenScript = preload("res://src/ui/wotr_screen.gd")
const SimScript = preload("res://src/retail_slice/retail_slice_sim.gd")

## The pack maps a battle may be fought on in this runner. Fixed rather than
## discovered so the battlefield binding - and therefore the commitment digest -
## is reproducible on a machine with no content packs mounted.
const HARNESS_MAP_IDS: Array = [
	"bfme2.map.dagorlad", "bfme2.map.fords-of-isen-ii", "bfme2.map.mordor",
	"bfme2.map.mount-doom", "bfme2.map.rivendell",
]

## LIVENESS. A GDScript runtime error aborts the enclosing function without ever
## reaching a `_check`, so a runner that only counts failures reports GREEN when
## its fixture collapses. Raise this deliberately when tests are added; never
## lower it to make a run go green.
## 85 -> 90: the real document's seven victory types, the hero ledger the real
## ownership sets now seed (per-template spawn resolution), the fresh-campaign
## victory evaluation, the version 3 brief surface inside the digested brief,
## and the ledger surviving the scene change.
const EXPECTED_CHECKS := 116

var passed := 0
var failed := 0
var _evidenced_document_path := ""
var _evidenced_document_bytes := PackedByteArray()


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	# LEG 8 FIRST, and deliberately: it is the only leg that needs the discovery
	# environment empty, and running it first means a machine WITH a document
	# still proves the refusal rather than skipping it.
	await _test_the_menu_refuses_without_a_document()

	var found: Dictionary = SessionScript.locate_document(_content_pack_roots())
	if not bool(found.get("ok", false)):
		printerr("WOTR_ROUND_TRIP MISSING %s" % String(found.get("reason", "")))
		quit(3)
		return
	print("WOTR_ROUND_TRIP document %s (%s)" % [String(found["path"]), String(found["source"])])

	var session = _begin_session(found)
	if session == null:
		_finish()
		return
	_test_phase_snapshot_contract(session)
	# RETAIL SEATS BOTH SIDES DEEP IN THEIR OWN TERRITORY. On the real BFME2 map
	# no region can legally attack anything on turn one, so the campaign has to be
	# marched to a border before there is an attack to make. That is a leg of its
	# own and it is asserted as one.
	_test_the_campaign_can_march_to_a_front(session)
	_test_the_screen_offers_only_legal_moves(session)
	var configured := _test_a_selection_becomes_a_commitment(session)
	if configured.is_empty():
		_finish()
		return
	_test_battle_transport_refuses_unready_evidenced_input(session)
	_test_the_commitment_configures_and_runs_a_real_match(session, configured)
	_test_the_session_survives_the_scene_change(session)
	_test_the_result_moves_the_map(session, configured)
	await _test_the_menu_reaches_it(found)
	_finish()


# --- leg 8: no document, no War of the Ring ----------------------------------

## The document search must fail LOUDLY and the menu entry must refuse. A blank
## strategic screen would be bad; a populated one built from invented regions
## would be indistinguishable from a real campaign, which is why this is asserted
## at the menu seam and not only at the loader.
func _test_the_menu_refuses_without_a_document() -> void:
	var saved_doc := OS.get_environment(SessionScript.DOCUMENT_ENV)
	var saved_content := OS.get_environment(SessionScript.CONTENT_ENV)
	OS.set_environment(SessionScript.DOCUMENT_ENV, "")
	OS.set_environment(SessionScript.CONTENT_ENV, "")

	var blind: Dictionary = SessionScript.locate_document([])
	_check("with_nothing_to_find_the_search_fails", not bool(blind.get("ok", false)))
	_check("the_failure_names_the_pack_document_it_looked_for",
		String(blind.get("reason", "")).contains(SessionScript.PACK_DOCUMENT_RELATIVE),
		String(blind.get("reason", "")))
	_check("the_failure_names_the_environment_override_it_looked_at",
		String(blind.get("reason", "")).contains(SessionScript.DOCUMENT_ENV))
	_check("the_failure_carries_no_document",
		(blind.get("document", {}) as Dictionary).is_empty())

	var menu = _boot_menu()
	if menu != null:
		await process_frame
		_check("the_menu_records_why_war_of_the_ring_is_unavailable",
			String(menu.wotr_unavailable_reason()) != "")
		_check("the_war_of_the_ring_entry_is_disabled",
			(menu.get_node("Center/WarOfTheRing") as Button).disabled)
		_check("the_disabled_entry_says_so_on_its_face",
			(menu.get_node("Center/WarOfTheRing") as Button).text.contains("UNAVAILABLE"),
			(menu.get_node("Center/WarOfTheRing") as Button).text)
		_check("the_entry_carries_the_reason_as_its_tooltip",
			(menu.get_node("Center/WarOfTheRing") as Button).tooltip_text
				== String(menu.wotr_unavailable_reason()))
		# THE REFUSAL ITSELF. Navigation must be rejected, not merely discouraged.
		_check("navigating_to_war_of_the_ring_is_refused", not bool(menu.show_page("wotr")))
		_check("the_refused_navigation_left_the_page_alone",
			String(menu.get_current_page()) != "wotr", String(menu.get_current_page()))
		# The strategic screen's ~22k-line script is compiled at the moment the
		# page is navigated to, so a REFUSED navigation never builds the node at
		# all. That is a stronger form of "stayed hidden", not a weaker one:
		# nothing was constructed, so nothing can be showing.
		var strategic := menu.get_node_or_null("Center/WotrScreen") as Control
		_check("the_strategic_screen_stayed_hidden",
			strategic == null or not strategic.visible)
		menu.queue_free()
		await process_frame
		await process_frame

	OS.set_environment(SessionScript.DOCUMENT_ENV, saved_doc)
	OS.set_environment(SessionScript.CONTENT_ENV, saved_content)


# --- legs 1+2: a real document seats a real campaign -------------------------

func _begin_session(found: Dictionary):
	# Evidence never points at the actual pack/private document. Copy the exact
	# bytes into disposable user data and parse the very copy the receipt hashes.
	_evidenced_document_bytes = _read_all_bytes(String(found["path"]))
	_evidenced_document_path = "user://wotr-round-trip-evidenced-document.json"
	_write_all_bytes(_evidenced_document_path, _evidenced_document_bytes)
	var document_value: Variant = JSON.parse_string(_evidenced_document_bytes.get_string_from_utf8())
	var document := document_value as Dictionary
	var probe := SessionScript.new()
	var probe_world = load("res://src/wotr/wotr_world.gd").new()
	_check("the_real_document_loads", probe_world.load_from_dict(document, ""), str(probe_world.errors))
	_check("the_real_document_carries_the_measured_retail_region_count",
		probe_world.region_ids.size() >= 38, "regions=%d" % probe_world.region_ids.size())
	probe.world = probe_world
	# Every faction available, so the seating exercise is about the document
	# rather than about which pack happens to be converted on this machine.
	var availability: Dictionary = {}
	for pack_faction in SessionScript.FACTION_BINDINGS.values():
		availability[String(pack_faction)] = ""
	var options := probe.seat_options(availability)
	_check("the_document_offers_seatable_player_templates", options.size() >= 2,
		"%d templates" % options.size())
	var seats: Array = []
	for option in options:
		seats.append({
			"template": String(option["template"]),
			"team": seats.size() + 1,
			"controller": StateScript.CONTROLLER_HUMAN if seats.is_empty() else StateScript.CONTROLLER_AI,
		})
		if seats.size() == 2:
			break
	var scenarios := probe.startable_scenarios(2)
	_check("the_campaign_offers_a_startable_two_seat_scenario", not scenarios.is_empty())
	if scenarios.is_empty() or seats.size() < 2:
		return null

	var session := SessionScript.new()
	_check("the_session_begins_on_the_real_document",
		session.begin_evidenced(document, probe_world.campaign_name,
			String(scenarios[0]), seats, {}, PackedStringArray(),
			_round_trip_identity(_evidenced_document_path)),
		str(session.refusals))
	session.document_path = _evidenced_document_path
	session.document_source = "env"
	if session.state == null:
		return null
	_check("both_seats_hold_authored_territory",
		session.state.regions_owned_by(0).size() > 0 and session.state.regions_owned_by(1).size() > 0,
		"seat0=%d seat1=%d" % [session.state.regions_owned_by(0).size(), session.state.regions_owned_by(1).size()])
	_check("the_active_seat_is_the_human_one",
		String((session.state.players[session.state.active_player()] as Dictionary)["controller"])
			== StateScript.CONTROLLER_HUMAN)
	# THE RETAIL SCENARIO'S OWN RULES ARRIVED. RotWK authors seven victory types
	# on every WOTR scenario, the ownership sets spawn REAL hero armies (each
	# seat's own, resolved per template - the shipped document carries ten
	# different `HeroArmy1` rows), and a fresh campaign must evaluate as ended
	# for nobody.
	_check("the_real_scenario_authors_its_seven_victory_types",
		(session.world.scenario(session.scenario_name).get("victory_types", []) as Array).size() == 7,
		session.scenario_name)
	_check("the_campaign_opens_with_a_living_hero_ledger",
		not session.state.heroes.is_empty(),
		"heroes=%d" % session.state.heroes.size())
	var standing: Dictionary = session.victory_status()
	_check("a_fresh_real_campaign_ends_nothing",
		bool(standing.get("ok", false))
			and (standing.get("defeated_players", PackedInt32Array()) as PackedInt32Array).is_empty()
			and int(standing.get("victorious_team", StateScript.NEUTRAL)) == StateScript.NEUTRAL,
		str(standing))
	return session


# --- leg 2b: armies march, through the screen --------------------------------

## Walk the active seat's armies along OWNED regions until one of them stands
## next to something it can attack. Every step goes through the screen, so this
## also proves the march path a player would actually use.
##
## Bounded and deterministic: at most one step per region in the world, always
## the lowest-sorted next hop of a breadth-first search over owned regions, so it
## terminates and takes the same route every run.
func _test_the_campaign_can_march_to_a_front(session) -> void:
	var screen := ScreenScript.new()
	screen.build()
	screen.configure(session, HARNESS_MAP_IDS, "")
	root.add_child(screen)
	var start := _first_staging_with_armies(session)
	_check("the_seat_starts_with_an_army_somewhere_it_owns", start != "")
	var marched := 0
	var current := start
	# WHETHER A MARCH WAS EVER REQUIRED. The loop below breaks immediately when
	# the starting region already borders something attackable, which is a
	# legitimate board state - retail seats some scenarios directly on the front.
	# Capturing this BEFORE the loop is what lets the assertion below distinguish
	# "did not need to march" from "could not march".
	var start_targets: PackedStringArray = session.attack_targets(start)
	var start_had_attack_targets: bool = not start_targets.is_empty()
	for _step in range(session.world.region_ids.size()):
		if not session.attack_targets(current).is_empty():
			break
		var next_hop := _next_hop_toward_a_front(session, current)
		if next_hop == "":
			break
		screen.select_region(current)
		if not screen.move_to(next_hop):
			break
		marched += 1
		current = next_hop
	# A CORRECTION, NOT A RELAXATION. The old assertion was `marched > 0`, which
	# asserted a premise the harness never established: that the seat starts away
	# from the front. Under the RotWK document seat 0 starts on `Arnor`, which
	# already borders five attackable regions, so the loop correctly breaks at
	# step 0 and the check failed on a board state that is not a defect. (The
	# proof it was the harness and not marching: the very next check,
	# `the_army_now_stands_where_it_can_attack`, passed on the same run - a real
	# marching failure would have failed both.)
	#
	# The property actually worth asserting is an EXACT DISJUNCTION with no
	# slack: either the army marched, or a march was never required because the
	# start was already a front. If `move_armies` breaks while a march IS
	# required, `start_had_attack_targets` is false and this still fails - which
	# is the mutation the check has to keep catching.
	_check("the_army_marched_when_a_march_was_required",
		marched > 0 or start_had_attack_targets,
		"marched=%d start_had_attack_targets=%s" % [marched, start_had_attack_targets])
	_check("the_army_now_stands_where_it_can_attack",
		not session.attack_targets(current).is_empty(), current)
	_check("marching_moved_the_army_rather_than_copying_it",
		session.state.armies_in_region(start).is_empty() or start == current,
		"%s still holds %d" % [start, session.state.armies_in_region(start).size()])
	# A march into a region the seat does not own must be REFUSED - that is an
	# attack, and an attack must go through the commitment.
	var enemy := _first_region_owned_by(session, 1)
	_check("a_march_into_enemy_territory_is_refused",
		enemy == "" or not screen.move_to(enemy), enemy)
	screen.queue_free()


## The lowest-sorted next region on a shortest path from `from_region` to any
## owned region that borders something attackable. "" when none exists.
func _next_hop_toward_a_front(session, from_region: String) -> String:
	var player: int = session.state.active_player()
	var previous: Dictionary = {from_region: ""}
	var queue: Array[String] = [from_region]
	var goal := ""
	while not queue.is_empty() and goal == "":
		var current: String = queue.pop_front()
		var neighbours: PackedStringArray = session.world.neighbours(current)
		var sorted_neighbours: Array[String] = []
		for neighbour in neighbours:
			sorted_neighbours.append(String(neighbour))
		sorted_neighbours.sort()
		for neighbour in sorted_neighbours:
			if previous.has(neighbour):
				continue
			if session.state.owner_of(neighbour) != player:
				continue
			previous[neighbour] = current
			if not session.world.neighbours(neighbour).is_empty() and _borders_an_enemy(session, neighbour):
				goal = neighbour
				break
			queue.append(neighbour)
	if goal == "":
		return ""
	var walk := goal
	while String(previous.get(walk, "")) != from_region and String(previous.get(walk, "")) != "":
		walk = String(previous[walk])
	return walk


func _borders_an_enemy(session, region_id: String) -> bool:
	var player: int = session.state.active_player()
	for neighbour in session.world.neighbours(region_id):
		if session.state.owner_of(neighbour) != player:
			return true
	return false


func _first_staging_with_armies(session) -> String:
	var staging: PackedStringArray = session.staging_regions()
	return String(staging[0]) if not staging.is_empty() else ""


func _first_region_owned_by(session, seat: int) -> String:
	for region_id in session.state.regions_owned_by(seat):
		return String(region_id)
	return ""


# --- leg 3: the screen offers only what the rules allow ----------------------

func _test_the_screen_offers_only_legal_moves(session) -> void:
	var screen := ScreenScript.new()
	screen.build()
	screen.configure(session, HARNESS_MAP_IDS, "")
	root.add_child(screen)

	var rows: Array = session.region_rows()
	_check("the_screen_shows_every_region_of_the_campaign",
		rows.size() == session.world.region_ids.size(),
		"%d vs %d" % [rows.size(), session.world.region_ids.size()])
	var owned_shown := 0
	var unpositioned := 0
	for row in rows:
		if int((row as Dictionary)["owner"]) != StateScript.NEUTRAL:
			owned_shown += 1
		if not bool((row as Dictionary)["has_position"]):
			unpositioned += 1
	_check("the_map_shows_ownership", owned_shown > 0, "%d owned regions" % owned_shown)
	# A region retail leaves without a custom centre point must be REPORTED
	# rather than placed somewhere invented. It used to be true that "no authored
	# centre point" and "not on the map" were the same set, because
	# `livingmap.w3d` carries no per-region mesh to take a centre from. They are
	# no longer the same set: `lmr_fill.w3d` DOES carry one mesh per region, and
	# the region-geometry converter computes an area-weighted centroid of
	# retail's own triangles for each, which is derivation from shipped geometry
	# rather than invention.
	#
	# So the invariant tightens rather than loosens. What must hold is an EXACT
	# equality with no slack in it:
	#
	#   listed as unplaced  ==  unpositioned  -  placed from a derived centroid
	#
	# With no region bundle converted, the second term is 0 and this is the
	# original assertion unchanged. With one converted, every region it covers
	# must move out of the list and none may linger - a region reported both
	# "placed" and "NOT ON THE MAP" on the same screen is the contradiction this
	# now catches, and it shipped for one frame while this lane was written.
	var centroid_placed := PackedStringArray()
	if screen.map3d != null and screen.map3d.has_map():
		centroid_placed = screen.map3d.centroid_placed_regions
	var expected_listed := unpositioned - centroid_placed.size()
	_check("regions_without_an_authored_position_are_reported_not_placed",
		screen.unplaced_host.get_child_count() == expected_listed,
		"%d unpositioned, %d placed from a centroid derived off retail's fill triangles, %d listed, %d expected" % [
			unpositioned, centroid_placed.size(),
			screen.unplaced_host.get_child_count(), expected_listed])

	var staging: PackedStringArray = session.staging_regions()
	_check("the_screen_offers_staging_regions_the_seat_actually_holds", not staging.is_empty())
	var illegal_staging := ""
	for region_id in staging:
		if session.state.owner_of(region_id) != session.state.active_player():
			illegal_staging = region_id
		elif session.state.armies_in_region(region_id).is_empty():
			illegal_staging = region_id
	_check("no_offered_staging_region_is_illegal", illegal_staging == "", illegal_staging)

	# A region the seat does not own must be REFUSED as a staging choice, not
	# quietly accepted and then refused deeper down.
	var enemy_region := ""
	for row in rows:
		if int((row as Dictionary)["owner"]) == 1:
			enemy_region = String((row as Dictionary)["id"])
			break
	_check("an_enemy_region_cannot_be_staged_from",
		enemy_region != "" and not screen.select_region(enemy_region))
	_check("the_refused_staging_choice_selected_nothing",
		session.selected_region.is_empty(), session.selected_region)

	var staged := _first_staging_with_targets(session)
	_check("some_owned_region_can_reach_an_enemy_region", staged != "")
	if staged == "":
		screen.queue_free()
		return
	_check("staging_from_an_owned_armed_region_is_accepted", screen.select_region(staged))
	var targets: PackedStringArray = session.attack_targets(staged)
	_check("the_screen_offers_targets_for_the_staged_region", not targets.is_empty())
	var illegal_target := ""
	for target in targets:
		if session.state.owner_of(target) == session.state.active_player():
			illegal_target = target
		elif not session.world.are_adjacent(staged, target):
			illegal_target = target
	_check("no_offered_target_is_illegal", illegal_target == "", illegal_target)
	# A non-adjacent region must be refused even though it is a real region.
	var distant := _non_adjacent_region(session, staged)
	_check("a_non_adjacent_region_is_refused_as_a_target",
		distant == "" or not screen.select_target(distant), distant)
	_check("attack_is_not_armed_without_a_target", not screen.can_attack_now())
	screen.queue_free()


# --- leg 4: the selection becomes the commitment -----------------------------

## THE RULE FROM 867447e, at the seam where it is easiest to break: what the
## player picked must arrive at the simulation THROUGH the commitment. So the
## commitment is checked to describe the selection, the state is checked to have
## ADMITTED it, and the roster handed onward is checked to be a projection of the
## admitted record rather than of anything the screen was holding.
func _test_a_selection_becomes_a_commitment(session) -> Dictionary:
	var screen := ScreenScript.new()
	screen.build()
	screen.configure(session, HARNESS_MAP_IDS, "")
	root.add_child(screen)
	var staged := _first_staging_with_targets(session)
	screen.select_region(staged)
	var targets: PackedStringArray = session.attack_targets(staged)
	var target := String(targets[0])
	_check("the_target_is_selectable", screen.select_target(target))
	_check("attack_is_armed_by_a_complete_selection", screen.can_attack_now())

	var before_hash := String(session.state.state_hash())
	var battle_signals: Array = []
	screen.battle_committed.connect(func(value: Dictionary): battle_signals.append(value))
	var production_attempt: Dictionary = screen.commit_selected_attack()
	var expected_rts_refusals := PackedStringArray()
	for gap in HandoffScript.UNSUPPORTED_BY_TACTICAL_SIM:
		expected_rts_refusals.append(
			"RTS tactical admission refused: unsupported tactical gap '%s'" % gap)
	var actual_rts_refusals := production_attempt.get(
		"refusals", PackedStringArray()) as PackedStringArray
	_check("production_attack_honestly_denies_the_exact_six_gap_prefix_and_feed",
		not bool(production_attempt.get("ok", false))
			and actual_rts_refusals.size() > expected_rts_refusals.size()
			and actual_rts_refusals.slice(0, expected_rts_refusals.size())
				== expected_rts_refusals
			and "\n".join(Array(actual_rts_refusals)).contains("reinforcement_feed")
			and session.state.pending_battle.is_empty()
			and battle_signals.is_empty()
			and String(session.state.state_hash()) == before_hash,
		str(actual_rts_refusals))
	screen.queue_free()

	# A fixed auto-only campaign truthfully overrides ATTACK's RTS request and
	# completes through the existing auto-resolve path without a launcher signal.
	var fixed_auto := SessionScript.new()
	fixed_auto.world = session.world
	fixed_auto.state = StateScript.new()
	fixed_auto.state.setup(session.world, [
		{"template": String((session.state.players[0] as Dictionary)["template"])}])
	assert(fixed_auto.state.restore(session.state.snapshot()))
	fixed_auto.state.battle_type = StateScript.BATTLE_TYPE_AUTO_RESOLVE
	fixed_auto.autoresolve = session.autoresolve
	fixed_auto.autoresolve_bindings = session.autoresolve_bindings
	fixed_auto.selected_region = staged
	fixed_auto.selected_target = target
	var fixed_screen := ScreenScript.new()
	fixed_screen.build()
	fixed_screen.configure(fixed_auto, HARNESS_MAP_IDS, "")
	root.add_child(fixed_screen)
	var fixed_signals: Array = []
	fixed_screen.battle_committed.connect(
		func(value: Dictionary): fixed_signals.append(value))
	var fixed_result: Dictionary = fixed_screen.commit_selected_attack()
	_check("fixed_auto_only_attack_routes_auto_and_honestly_refuses_missing_bundle",
		not bool(fixed_result.get("ok", false))
			and fixed_signals.is_empty()
			and not fixed_auto.state.pending_battle.is_empty()
			and String(fixed_auto.state.pending_battle.get("battle_type", ""))
				== StateScript.BATTLE_TYPE_AUTO_RESOLVE
			and not (fixed_result.get("refusals", PackedStringArray()) as PackedStringArray).is_empty()
			and fixed_screen.last_auto_resolve.is_empty(),
		str(fixed_result.get("refusals", [])))
	fixed_screen.queue_free()

	# FUTURE-ADMISSION SEAM ONLY. Production ATTACK above remains denied. Admit the
	# exact RTS commitment at the low-level state boundary solely so the historical
	# scene-change/result plumbing below remains exercised without claiming current
	# RTS simulation parity or weakening tactical_admission().
	var future_brief := HandoffScript.build_request(
		session.world, session.state, session.state.active_player(), target)
	var configured: Dictionary = BattleScript.configure(
		future_brief, SessionScript.FACTION_BINDINGS,
		session.battlefield_bindings(HARNESS_MAP_IDS), StateScript.BATTLE_TYPE_RTS)
	assert(bool(configured.get("ok", false)))
	assert(session.state.begin_battle(configured["commitment"]),
		"future-admission fixture could not enter the hash boundary")
	var commitment := configured["commitment"] as Dictionary

	_check("the_commitment_names_the_region_the_player_selected",
		String(commitment["region"]) == target, "%s vs %s" % [String(commitment["region"]), target])
	_check("the_commitment_names_the_region_the_player_staged_from",
		String(commitment["staging_region"]) == staged)
	_check("the_commitment_names_the_active_seat_as_attacker",
		int(commitment["attacker"]) == session.state.active_player())
	_check("the_commitment_names_the_regions_owner_as_defender",
		int(commitment["defender"]) == 1)
	# THE ADMISSION. A commitment that was minted but never admitted would leave
	# the tactical match unauthorised by anything the hash covers.
	_check("the_strategic_state_admitted_the_commitment",
		String(session.state.pending_battle.get("region", "")) == target,
		str(session.state.pending_battle))
	_check("admitting_it_changed_the_strategic_hash",
		String(session.state.state_hash()) != before_hash)
	var brief: Dictionary = HandoffScript.build_request(
		session.world, _rebuilt_pre_battle_state(session),
		int(commitment["attacker"]), target)
	_check("the_commitment_digests_the_brief_it_came_from",
		BattleScript.commitment_matches_brief(commitment, brief))
	var directly_configured := BattleScript.configure(
		brief, SessionScript.FACTION_BINDINGS,
		session.battlefield_bindings(HARNESS_MAP_IDS), "")
	_check("the_session_returns_configures_reinforcement_feed_unchanged",
		(configured.get("reinforcement_feed", {}) as Dictionary)
			== (directly_configured.get("reinforcement_feed", {}) as Dictionary),
		str(configured.get("reinforcement_feed", {})))
	# The version 3 strategic surface rides the REAL brief: territory bonuses,
	# standing buildings, hero levels and the named data gaps - all derived from
	# hashed state, all inside the digest the check above just proved.
	_check("the_version_3_brief_carries_the_strategic_bonus_surface",
		int(brief.get("schema_version", -1)) == 3
			and (brief["region"] as Dictionary).has("standing_buildings")
			and (brief["region"] as Dictionary).has("territory")
			and (brief["attacker"] as Dictionary).has("territory_bonuses")
			and (brief["defender"] as Dictionary).has("unified_territories")
			and not (brief["data_gaps"] as Array).is_empty(),
		str(brief.get("region", {}).keys() if brief.has("region") else brief.keys()))
	# THE BATTLEFIELD. It is a stand-in and it is recorded; a battlefield chosen
	# outside the commitment would be the desync of 867447e with a new name.
	_check("the_bound_battlefield_is_recorded_in_the_commitment",
		HARNESS_MAP_IDS.has(String(commitment["battlefield_map"])),
		String(commitment["battlefield_map"]))
	_check("the_commitment_still_records_the_regions_own_retail_map",
		String(commitment["map_name"]) == String(session.world.region(target).get("map_name", "")))
	_check("the_battlefield_binding_is_reproducible",
		String(session.battlefield_bindings(HARNESS_MAP_IDS)[String(commitment["map_name"])])
			== String(commitment["battlefield_map"]))
	# THE PROJECTION. The roster the caller may hand a simulation is re-derived
	# from the ADMITTED record, so nothing the screen held can reach the sim.
	_check("the_roster_is_a_projection_of_the_admitted_commitment",
		(configured["team_roster"] as Array) == BattleScript.team_roster_for(session.state.pending_battle),
		str(configured["team_roster"]))
	_check("the_human_seat_reaches_the_roster_as_not_ai",
		not bool(((configured["team_roster"] as Array)[0] as Dictionary)["is_ai"]))
	# A SECOND commit while one battle is live must be refused, not stacked.
	var second: Dictionary = session.commit_attack(target, HARNESS_MAP_IDS)
	_check("a_second_attack_while_a_battle_is_in_flight_is_refused",
		not bool(second.get("ok", false)))
	return configured


# --- Packet 4: consume-once battle transport ---------------------------------

func _test_battle_transport_refuses_unready_evidenced_input(session) -> void:
	var resumed := SessionScript.new()
	var payload: Dictionary = session.handoff_payload()
	_check("battle_seam_records_are_not_smuggled_into_the_hashed_handoff",
		not payload.has("wotr_battle_transport") and not payload.has("wotr_battle_report"))
	_check("transport_fixture_adopts_only_after_evidence_verification",
		resumed.adopt_evidenced_handoff(payload), str(resumed.refusals))
	var target := String(resumed.state.pending_battle.get("region", ""))
	# Rebuild the exact adopted snapshot at its pre-admission Tactical boundary;
	# clear_battle() alone deliberately leaves the authoritative phase in Battle.
	resumed.state = _rebuilt_pre_battle_state(resumed)
	resumed.state.battle_type = StateScript.BATTLE_TYPE_RTS
	var brief := HandoffScript.build_request(
		resumed.world, resumed.state, resumed.state.active_player(), target)
	var configured := BattleScript.configure(
		brief, SessionScript.FACTION_BINDINGS,
		resumed.battlefield_bindings(HARNESS_MAP_IDS), StateScript.BATTLE_TYPE_RTS)
	assert(resumed.state.begin_battle(configured["commitment"]),
		"synthetic RTS fixture was not admitted: %s" % resumed.state._last_rejection())
	# The current private evidenced campaign honestly has no authored cadence and
	# no bound strategic unit rows. That is the no-enable blocker, not permission
	# for this test to forge a receipt or invent a feed.
	_check("real_evidenced_rts_configuration_carries_a_refused_feed",
		not bool((configured["reinforcement_feed"] as Dictionary).get("ok", false)),
		str((configured["reinforcement_feed"] as Dictionary).get("refusals", [])))
	var before_hash := String(resumed.state.state_hash())
	var before_events: Array = resumed.state.events.duplicate(true)
	var transport: Dictionary = resumed.battle_transport(configured)
	_check("battle_transport_refuses_the_real_unready_feed_by_name",
		transport.is_empty()
			and not resumed.refusals.is_empty()
			and String(resumed.refusals[0]).contains("reinforcement feed was refused"),
		str(resumed.refusals))
	_check("refused_evidenced_transport_is_hash_and_event_neutral",
		String(resumed.state.state_hash()) == before_hash
			and resumed.state.events == before_events
			and not resumed.state.pending_battle.is_empty())


# --- leg 5: the match the commitment authorises actually runs ----------------

func _test_the_commitment_configures_and_runs_a_real_match(session, configured: Dictionary) -> void:
	var sim := _tactical_match(configured)
	var commitment := session.state.pending_battle as Dictionary
	_check("the_bound_attacker_faction_reached_the_simulation",
		String((sim.team_descriptor(BattleScript.ATTACKER_TEAM) as Dictionary).get("faction", ""))
			== String(commitment["attacker_faction"]))
	_check("the_bound_defender_faction_reached_the_simulation",
		String((sim.team_descriptor(BattleScript.DEFENDER_TEAM) as Dictionary).get("faction", ""))
			== String(commitment["defender_faction"]))
	_check("the_strategic_purse_reached_the_simulation",
		int(sim.team_resources[BattleScript.ATTACKER_TEAM])
			== int((configured["gameplay_rules"] as Dictionary).get("starting_resources", 10000)),
		str(sim.team_resources))
	_check("the_match_is_undecided_before_it_is_fought",
		sim.winner == BattleScript.UNDECIDED)
	# AN UNDECIDED MATCH MUST NOT APPLY. -1 matches neither team and would take
	# the defender branch, destroying the attacking army for nothing.
	var premature: Dictionary = BattleScript.apply_outcome(session.state, sim.winner)
	_check("an_undecided_match_is_refused_by_the_outcome_path",
		not bool(premature["ok"]))
	_check("the_refused_outcome_left_the_battle_in_flight",
		not session.state.pending_battle.is_empty())
	_decide(sim, BattleScript.DEFENDER_TEAM)
	_check("the_tactical_match_decides_for_the_attacker",
		sim.winner == BattleScript.ATTACKER_TEAM, "winner=%d" % sim.winner)
	_last_winner = sim.winner


# --- leg 7: the session survives the scene change ----------------------------

func _test_the_session_survives_the_scene_change(session) -> void:
	var payload: Dictionary = session.handoff_payload()
	_check("the_handoff_carries_a_schema", String(payload.get("schema", "")) == SessionScript.HANDOFF_SCHEMA)
	_check("the_handoff_carries_no_presentation_state",
		not payload.has("selected_region") and not payload.has("hover_region"), str(payload.keys()))
	_check("the_handoff_carries_the_input_receipt_verbatim",
		payload.get("input_receipt", {}) == session.input_receipt
			and not session.input_receipt.is_empty())
	var adopted := SessionScript.new()
	_check("the_handoff_rebuilds_the_session", adopted.adopt_evidenced_handoff(payload), str(adopted.refusals))
	_check("the_adopted_receipt_is_identical", adopted.input_receipt == session.input_receipt)
	_check("the_rebuilt_session_hashes_identically",
		String(adopted.state.state_hash()) == String(session.state.state_hash()))
	_check("the_rebuilt_session_still_has_the_battle_in_flight",
		String(adopted.state.pending_battle.get("region", ""))
			== String(session.state.pending_battle.get("region", "")))
	_check("the_rebuilt_session_keeps_the_hero_ledger",
		adopted.state.heroes == session.state.heroes
			and not adopted.state.heroes.is_empty(),
		"heroes=%d" % adopted.state.heroes.size())

	# Three adversarial admissions, all checked before the receiver may mutate.
	var drift_receiver: Variant = _sentinel_receiver()
	var drift_world: Variant = drift_receiver.world; var drift_state: Variant = drift_receiver.state
	var changed := _evidenced_document_bytes.duplicate()
	changed[0] = int(changed[0]) ^ 1
	_write_all_bytes(_evidenced_document_path, changed)
	var drift_ok: bool = drift_receiver.adopt_evidenced_handoff(payload)
	_write_all_bytes(_evidenced_document_path, _evidenced_document_bytes)
	_check("document_byte_drift_is_refused", not drift_ok)
	_check("document_byte_drift_has_a_named_refusal", not drift_receiver.refusals.is_empty())
	_check("document_byte_drift_leaves_receiver_unchanged",
		drift_receiver.world == drift_world and drift_receiver.state == drift_state)

	var other_path := "user://wotr-round-trip-other-existing-document.json"
	_write_all_bytes(other_path, _evidenced_document_bytes)
	var wrong_path := payload.duplicate(true); wrong_path["document_path"] = other_path
	var path_receiver: Variant = _sentinel_receiver()
	var path_world: Variant = path_receiver.world; var path_state: Variant = path_receiver.state
	var path_ok: bool = path_receiver.adopt_evidenced_handoff(wrong_path)
	_check("payload_path_mismatch_is_refused", not path_ok)
	_check("payload_path_mismatch_has_a_named_refusal", not path_receiver.refusals.is_empty())
	_check("payload_path_mismatch_leaves_receiver_unchanged",
		path_receiver.world == path_world and path_receiver.state == path_state)

	var wrong_source := payload.duplicate(true); wrong_source["document_source"] = "pack"
	var source_receiver: Variant = _sentinel_receiver()
	var source_world: Variant = source_receiver.world; var source_state: Variant = source_receiver.state
	var source_ok: bool = source_receiver.adopt_evidenced_handoff(wrong_source)
	_check("payload_source_mismatch_is_refused", not source_ok)
	_check("payload_source_mismatch_has_a_named_refusal", not source_receiver.refusals.is_empty())
	_check("payload_source_mismatch_leaves_receiver_unchanged",
		source_receiver.world == source_world and source_receiver.state == source_state)

	# The legacy low-level seam remains explicitly unreceipted.
	var truncated := payload.duplicate(true)
	truncated["snapshot"] = PackedByteArray()
	var refused := SessionScript.new()
	_check("a_handoff_without_a_snapshot_is_refused", not refused.adopt_handoff(truncated))
	_check("the_refused_handoff_left_no_half_session",
		refused.state == null and refused.world == null)


# --- leg 6: the result moves the map -----------------------------------------

func _test_the_result_moves_the_map(session, configured: Dictionary) -> void:
	var commitment: Dictionary = session.state.pending_battle.duplicate(true)
	var region := String(commitment["region"])
	var attacker := int(commitment["attacker"])
	var committed: PackedInt32Array = commitment["committed_armies"]
	var before_owner: int = session.state.owner_of(region)
	var before_turn: int = session.state.turn_index
	_check("the_region_starts_in_the_defenders_hands", before_owner != attacker)

	var outcome: Dictionary = session.resolve_battle(_last_winner)
	_check("the_outcome_applies", bool(outcome["ok"]), str(outcome["refusals"]))
	_check("the_outcome_names_the_attacking_seat", int(outcome["winner_player"]) == attacker)
	_check("the_region_changed_hands", session.state.owner_of(region) == attacker,
		"owner=%d" % session.state.owner_of(region))
	_check("the_committed_army_advanced_into_it",
		Array(session.state.armies_in_region(region)).has(int(committed[0])),
		str(session.state.armies_in_region(region)))
	_check("the_transaction_closed", session.state.pending_battle.is_empty())
	_check("the_turn_passed_to_the_next_seat", session.state.turn_index > before_turn)
	# The map the SCREEN would now draw must show the capture: a result applied
	# to state that the view never re-reads is a result the player cannot see.
	var owner_after := StateScript.NEUTRAL
	for row in session.region_rows():
		if String((row as Dictionary)["id"]) == region:
			owner_after = int((row as Dictionary)["owner"])
	_check("the_strategic_view_shows_the_capture", owner_after == attacker)
	# The same result must not apply twice.
	var replay: Dictionary = session.resolve_battle(_last_winner)
	_check("the_same_result_cannot_be_applied_twice", not bool(replay["ok"]))


# --- the menu actually reaches all of it -------------------------------------

func _test_the_menu_reaches_it(found: Dictionary) -> void:
	# The menu searches the mounted packs and then the environment; point the
	# environment at the document this run already found so the seam is exercised
	# on a machine with no living-world pack.
	var saved := OS.get_environment(SessionScript.DOCUMENT_ENV)
	OS.set_environment(SessionScript.DOCUMENT_ENV, String(found["path"]))
	var menu = _boot_menu()
	if menu == null:
		OS.set_environment(SessionScript.DOCUMENT_ENV, saved)
		return
	await process_frame
	_check("the_menu_finds_the_document", String(menu.wotr_unavailable_reason()) == "",
		String(menu.wotr_unavailable_reason()))
	var entry := menu.get_node("Center/WarOfTheRing") as Button
	_check("the_war_of_the_ring_entry_is_live", not entry.disabled and entry.text == "WAR OF THE RING")

	# A CAMPAIGN NEEDS TWO FIELDABLE FACTIONS. With fewer converted the menu
	# refuses BY NAME rather than seating a session whose battles could never be
	# fought - assert that refusal first, on an empty availability map.
	# FORCE THE AVAILABILITY SWEEP FIRST. It is stepped across idle frames now, and
	# every reader of `_skirmish_availability` forces the rest of it - including
	# `_start_wotr_session()` below. Overriding the table while the sweep still had
	# steps left would simply be overwritten by them, and this check would then be
	# measuring the machine's real conversion state instead of the empty map it
	# means to test. `get_retail_faction_availability()` is the public reader whose
	# whole job is to guarantee the sweep is complete when it returns.
	menu.get_retail_faction_availability()
	menu._skirmish_availability = {}
	menu._wotr_session = null
	_check("with_no_converted_faction_the_campaign_refuses_to_seat",
		not bool(menu.show_page("wotr")))
	_check("the_seating_refusal_names_the_conversion_gap",
		String(menu.wotr_unavailable_reason()).contains("converted"),
		String(menu.wotr_unavailable_reason()))

	# Now the wiring itself, independent of which factions this machine happens
	# to have converted: declare the document's own factions fieldable and open
	# the page for real.
	var availability: Dictionary = {}
	for pack_faction in SessionScript.FACTION_BINDINGS.values():
		availability[String(pack_faction)] = ""
	menu._skirmish_availability = availability
	menu._wotr_unavailable_reason = ""
	menu._wotr_session = null
	_check("the_entry_opens_the_strategic_page", bool(menu.show_page("wotr")))
	_check("the_strategic_screen_is_visible", (menu.get_node("Center/WotrScreen") as Control).visible)
	var screen = menu.get_node("Center/WotrScreen")
	_check("the_menus_session_is_seated_on_the_real_document",
		screen.session != null and screen.session.state != null
			and screen.session.world.region_ids.size() >= 38)
	if screen.session == null:
		menu.queue_free()
		await process_frame
		OS.set_environment(SessionScript.DOCUMENT_ENV, saved)
		return
	# March the menu's own session to a front, exactly as a player would, then
	# commit - so the launch projection below is built from a real commitment.
	var current := _first_staging_with_armies(screen.session)
	for _step in range(screen.session.world.region_ids.size()):
		if not screen.session.attack_targets(current).is_empty():
			break
		var next_hop := _next_hop_toward_a_front(screen.session, current)
		if next_hop == "":
			break
		screen.select_region(current)
		if not screen.move_to(next_hop):
			break
		current = next_hop
	var staged := _first_staging_with_targets(screen.session)
	_check("the_menus_session_offers_a_staging_region", staged != "")
	if staged == "":
		staged = current
	screen.select_region(staged)
	var offered: PackedStringArray = screen.session.attack_targets(staged)
	screen.select_target(String(offered[0]) if not offered.is_empty() else "")
	# The MENU's own map list, so the battlefield the commitment records is one
	# this machine can actually boot; the harness list is the fallback for a
	# machine with no content pack mounted at all.
	var menu_maps: Array = menu.wotr_available_map_ids()
	var configured: Dictionary = screen.session.commit_attack(
		screen.session.selected_target, menu_maps if not menu_maps.is_empty() else HARNESS_MAP_IDS)
	_check("the_menu_can_commit_an_attack_from_its_own_session",
		bool(configured.get("ok", false)), str(configured.get("refusals", PackedStringArray())))
	var commitment := configured.get("commitment", {}) as Dictionary
	var descriptors: Array = menu.wotr_team_descriptors(configured)
	var battlefield := String(commitment.get("battlefield_map", ""))
	if descriptors.size() == 2:
		menu._wotr_selected_hero_document = "{\"heroId\":\"0123456789abcdef01234567\",\"name\":\"Wotr Pick\"}"
		descriptors = menu.wotr_team_descriptors(configured)
		_check("wotr_human_fields_exactly_the_one_picked_created_hero",
			(descriptors[0] as Dictionary).get("heroes", []) == [menu._wotr_selected_hero_document]
				and (descriptors[1] as Dictionary).get("heroes", null) == [],
			str(descriptors))
		menu._wotr_selected_hero_document = ""
		descriptors = menu.wotr_team_descriptors(configured)
		_check("wotr_unpicked_created_hero_fields_none",
			(descriptors[0] as Dictionary).get("heroes", null) == [], str(descriptors))
	else:
		_check("wotr_human_fields_exactly_the_one_picked_created_hero", false,
			"battlefield was unexpectedly unseatable")
		_check("wotr_unpicked_created_hero_fields_none", false,
			"battlefield was unexpectedly unseatable")
	# EITHER the roster is projected from the commitment, OR the battlefield has
	# fewer than two authored player starts on this machine and the menu refused
	# to seat it. Both are correct; a roster that was neither would be invented.
	var projected: bool = descriptors.size() == 2 		and String((descriptors[0] as Dictionary)["faction"]) == String(commitment.get("attacker_faction", "")) 		and String((descriptors[1] as Dictionary)["faction"]) == String(commitment.get("defender_faction", ""))
	var honestly_unseatable: bool = descriptors.is_empty() 		and menu._read_map_start_indices(battlefield).size() < 2
	_check("the_launch_roster_is_projected_from_the_commitment_or_refused_by_name",
		projected or honestly_unseatable,
		"descriptors=%s battlefield=%s" % [str(descriptors), battlefield])
	# Exercise the actual menu launcher guard with this admitted auto-resolve
	# commitment. It must refuse before scene launch or consume-once seam writes.
	menu._game_state.set("wotr_handoff", {})
	menu._game_state.set("wotr_battle_transport", {})
	menu._game_state.set("wotr_battle_report", {})
	menu._on_wotr_battle_committed(configured)
	_check("menu_launcher_guard_refuses_non_rts_without_partial_seam_state",
		String(commitment.get("battle_type", "")) == StateScript.BATTLE_TYPE_AUTO_RESOLVE
			and not menu._launch_in_progress
			and (menu._game_state.get("wotr_handoff") as Dictionary).is_empty()
			and (menu._game_state.get("wotr_battle_transport") as Dictionary).is_empty()
			and (menu._game_state.get("wotr_battle_report") as Dictionary).is_empty()
			and screen.message_label.text.contains("not an RTS battle"),
		screen.message_label.text)
	menu.queue_free()
	await process_frame
	OS.set_environment(SessionScript.DOCUMENT_ENV, saved)


# --- helpers -----------------------------------------------------------------

var _last_winner := BattleScript.UNDECIDED


func _round_trip_identity(document_path: String) -> Dictionary:
	var content_db := root.get_node_or_null("ContentDB")
	var mod_loader := root.get_node_or_null("ModLoader")
	if mod_loader == null:
		return {}
	var meta: Array = []
	if content_db != null:
		meta = (content_db.get("pack_meta") as Array).duplicate(true)
	var selection: Dictionary = {}
	var selection_path := String(mod_loader.get("active_selection_path")).strip_edges()
	if not selection_path.is_empty():
		selection = {"kind": "selection.json", "path": selection_path}
	elif not meta.is_empty():
		selection = {"kind": "immutableBundleRoot",
			"root": String((meta[0] as Dictionary).get("root", ""))}
	var absent := {"status": "absent",
		"reason": "round-trip fixture intentionally loads no optional config bundle"}
	return {
		"document_path": document_path,
		"document_source": "env",
		"active_content_source": String(mod_loader.get("active_content_source")),
		"selection": selection,
		"pack_meta": meta,
		"config_bundles": {
			"autoresolve": absent.duplicate(),
			"autoresolve_bindings": absent.duplicate(),
			"ai_template": absent.duplicate(),
			"building_catalogue": absent.duplicate(),
		},
	}


func _read_all_bytes(path: String) -> PackedByteArray:
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null: return PackedByteArray()
	var bytes := file.get_buffer(file.get_length())
	file.close()
	return bytes


func _write_all_bytes(path: String, bytes: PackedByteArray) -> void:
	var file := FileAccess.open(path, FileAccess.WRITE)
	if file == null: return
	file.store_buffer(bytes)
	file.close()


func _sentinel_receiver() -> Variant:
	var receiver := SessionScript.new()
	receiver.world = WorldScript.new()
	receiver.state = StateScript.new()
	return receiver


func _content_pack_roots() -> Array:
	var roots: Array = []
	var content_db := root.get_node_or_null("ContentDB")
	if content_db == null:
		return roots
	for meta_value in (content_db.get("pack_meta") as Array):
		roots.append(String((meta_value as Dictionary).get("root", "")))
	roots.sort()
	return roots


func _boot_menu():
	var packed: PackedScene = load("res://scenes/boot.tscn")
	if packed == null:
		_check("boot_scene_parses", false)
		return null
	var menu := packed.instantiate()
	root.add_child(menu)
	return menu


## The lowest-sorted region this seat can stage a real attack from - one it owns,
## with an army, that reaches something it does not own. Sorted iteration, so the
## choice is the same on every run.
func _first_staging_with_targets(session) -> String:
	for region_id in session.staging_regions():
		if not session.attack_targets(region_id).is_empty():
			return region_id
	return ""


func _non_adjacent_region(session, from_region: String) -> String:
	for region_id in session.world.region_ids:
		if region_id == from_region:
			continue
		if not session.world.are_adjacent(from_region, region_id):
			return region_id
	return ""


## The state as it was BEFORE the battle opened, so the brief can be rebuilt and
## digested against the commitment. Restoring a snapshot would be simpler but
## would also prove less: this rebuilds from the live state by closing the
## transaction on a copy.
func _rebuilt_pre_battle_state(session):
	var copy := StateScript.new()
	copy.setup(session.world, [{"template": String((session.state.players[0] as Dictionary)["template"])}])
	copy.restore(session.state.snapshot())
	copy.clear_battle()
	# clear_battle closes only the transaction record; a pre-admission fixture is
	# authoritatively Tactical as well as empty.
	copy.phase = StateScript.PHASE_TACTICAL
	return copy


func _tactical_match(configured: Dictionary) -> SimScript:
	var sim := SimScript.new()
	var rules := _harness_rules()
	rules.merge(configured["gameplay_rules"] as Dictionary, true)
	sim._rules = rules
	sim.configure_team_roster(configured["team_roster"] as Array)
	sim.setup({}, {})
	sim.ai_enabled = false
	return sim


func _decide(sim: SimScript, losing_team: int) -> void:
	var fortress: int = sim.fortress_id(losing_team)
	if fortress == 0:
		printerr("WOTR_ROUND_TRIP FAIL harness seeded no fortress for team %d" % losing_team)
		return
	sim._apply_structure_damage(1 - losing_team, fortress, 999999)
	sim.tick()


func _harness_rules() -> Dictionary:
	return {
		"enable_base_loop": true,
		"starting_resources": 10000,
		"ai_attack_delay_ticks": 100000,
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
		print("WOTR_ROUND_TRIP PASS %s" % name)
	else:
		failed += 1
		printerr("WOTR_ROUND_TRIP FAIL %s%s" % [name, " (%s)" % detail if detail != "" else ""])


func _test_phase_snapshot_contract(session) -> void:
	var original_hash: String = session.state.state_hash()
	var peer = StateScript.new()
	peer.world = session.world
	_check("phase v2 snapshot restores", peer.restore(session.state.snapshot()))
	_check("phase v2 hash is identical", peer.state_hash() == original_hash)

	var legacy: Dictionary = session.state.authoritative_state()
	legacy["schema_version"] = 1
	legacy.erase("phase")
	legacy.erase("pending_retreats")
	var migrated = StateScript.new()
	migrated.world = session.world
	_check("phase v1 migrates to tactical and empty retreat", migrated.restore(var_to_bytes(legacy))
		and migrated.phase == StateScript.PHASE_TACTICAL and migrated.pending_retreats.is_empty())

	var before := peer.state_hash()
	var junk_phase: Dictionary = session.state.authoritative_state()
	junk_phase["phase"] = "planning"
	_check("junk v2 phase refuses all-or-nothing", not peer.restore(var_to_bytes(junk_phase))
		and peer.state_hash() == before)
	var junk_type: Dictionary = session.state.authoritative_state()
	junk_type["pending_retreats"] = "not rows"
	_check("junk v2 retreat type refuses all-or-nothing", not peer.restore(var_to_bytes(junk_type))
		and peer.state_hash() == before)


func _finish() -> void:
	var ran := passed + failed
	if ran != EXPECTED_CHECKS:
		failed += 1
		printerr("WOTR_ROUND_TRIP FAIL liveness: ran %d checks, expected %d - a function aborted before its assertions" % [ran, EXPECTED_CHECKS])
	print("WOTR_ROUND_TRIP_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)
