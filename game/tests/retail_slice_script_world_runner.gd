extends SceneTree

## Proof runner for RetailSliceScriptWorld - the first SageScriptWorld backed
## by the real RetailSliceSim.
##
## Covers, in order:
##   1. bindings        - player/team name bindings validate against the
##                        roster, stay 1:1 where owner() needs it, and reject
##                        rebinds
##   2. base world      - capability set, world_frame, debug sink
##   3. players facet   - exists/faction/command points/building_count/
##                        relation_to, with the strict refusals for unbound
##                        names and unmodeled building classes
##   4. teams facet     - exists/unit_count/owner/stop, including the
##                        disband refusal
##   5. orders facet    - move_to/attack_move_to (POSITION), attack (TEAM
##                        target, exact-tie lowest-id pick), stand_ground,
##                        plus scope and target-kind refusals
##   6. combat facet    - spellbook power ready/cast, player_all_destroyed
##   7. progression     - upgrade reads, build_upgrade research queueing,
##                        the science surface, any_hero_reached_rank
##   8. economy facet   - money read/set/give with per-player refusal
##   9. meta facet      - player_count, multiplayer_outcome across a real
##                        victory resolution
##  10. base building   - the unpack pair (paid charges, free does not, the
##                        UNIT_REF destination binds), base-anchored builds,
##                        per-base buildability, the shared object /
##                        unit-reference namespace (call-time resolution,
##                        flag names unshadowable), the script-player
##                        binding, and the subsystem's hash inertness for a
##                        match that configures no bases (the state-pin
##                        property)
##  11. type identity   - players.object_count_of_types over recorded row
##                        identity (provenance, runtime-id slugs, the
##                        structure kind registry), the aggregate player
##                        tokens, include_dead, the sim-owned
##                        OBJECT_TYPE_LIST stores (meta.object_list_change),
##                        units.has_command_points_to_build against the
##                        queue admission numbers, and orders.move_to's
##                        NEAREST_TYPE targets with the exact
##                        distance/kind/lowest-id tie-break
##  12. read-only-ness  - every implemented QUERY leaves state_hash()
##                        untouched across repeated evaluation
##  13. determinism     - two independently built sims driven through two
##                        independently built worlds give identical answers
##                        and identical state hashes, including the
##                        tie-breaking attack target pick, the base surface
##                        and the object-type censuses
##
## Every fixture is SYNTHETIC (the pin runner's harness rules); no retail
## install or content pack is required.
##
## Invocation:
##   Godot_v4.7-stable_win64_console.exe --headless --path game \
##     --script res://tests/retail_slice_script_world_runner.gd

const SimScript = preload("res://src/retail_slice/retail_slice_sim.gd")
const WorldScript = preload("res://src/retail_slice/retail_slice_script_world.gd")
const ParamTypes = preload("res://src/script/script_param_types.gd")
const ManifestScript = preload("res://src/retail_slice/retail_faction_manifest.gd")

const PLAYER := "PlayerOne"
const ENEMY := "EnemyOne"
const PLAYER_TEAM_NAME := "teamPlayerOne"
const ENEMY_TEAM_NAME := "teamEnemyOne"

## LIVENESS. A GDScript runtime error aborts the enclosing function on the spot
## without propagating, so every `_check` after the error site never runs and
## `failed` never increments - an inert runner prints a zero-failure result and
## exits 0. Pinning the number of checks a healthy run makes turns that silent
## abort into a loud failure. Raise it deliberately when tests are added; never
## lower it to make a run go green.
## 410 -> 475: RAISED by the 65 checks the three object-name-registry tests
## add (the eight reads/binds the shared namespace can answer, the refusals
## that stay refusals, and the reference-handle semantics). Nothing removed.
## 475 -> 489: RAISED by the 14 checks _test_teams_was_destroyed adds - two
## fixture preconditions plus the dead-battalions precondition (3), the four
## _check_hit answers at 2 each (8: living, structures-only, wiped, the other
## team), the two refusals (2) and the read-only hash check (1).
## 3 + 8 + 2 + 1 = 14. Nothing removed.
## 489 -> 495: RAISED by the 6 checks the snapshot-boundary block in the same
## test adds - three _check_hit answers at 2 each (the wiped team, the
## surviving team, and the structures-only team, each read back out of a sim
## that adopted a snapshot). Nothing removed.
## 495 -> 498: RAISED by the 3 checks the battalions-only mirror case adds
## (one fixture precondition plus one _check_hit at 2). It exists because a
## mutation run proved severing the ENTITY carry from snapshot() left all
## six checks above green. Nothing removed.
## 500 -> 510: RAISED by the historical ANY_HERO_REACHED_RANK proof: death
## persistence (2), non-simultaneous second-hero attainment (2), revival at
## rank 1 cannot lower the recorded peak (2), nonpositive refusal (1), and
## snapshot adoption plus its answer (3). Nothing removed.
## 510 -> 514: RAISED by direct deterministic-boundary checks: adopted
## canonical hero-history rows and authoritative state_hash equality (2), plus
## the reset-match query proving history was cleared (2). Nothing removed.
## 535 -> 553: RAISED by the 18 script-team registry checks: two distinct
## same-owner identities, typed membership reads, exact owner/state isolation,
## hash visibility, snapshot adoption, and setup reset.
## 553 -> 554: recruitment-availability flag joins typed membership in the
## same snapshot/reset proof.
## 554 -> 556: malformed string/bool ids prove typed handles never coerce.
## 556 -> 559: TEAM_AVAILABLE_FOR_RECRUITMENT reaches the named-team registry,
## explicit false moves/crosses the snapshot boundary, and unknown teams refuse.
## 559 -> 573: START_POSITION_IS proof: missing/negative assignments refuse
## (2), internal index 7 answers authored position 8 (2), <This Player> binds
## and answers the same value (3), the read is hash-inert (1), and an injected
## unset roster cannot be overwritten by legacy map defaults (2), and menu
## normalization proves positive, zero, malformed and duplicate cases (4).
## 598 -> 620: TEAM_TRANSFER_TO_PLAYER proof covers exact qualified lookup,
## token-aware destination, controlling-owner mutation, state preservation,
## snapshot/hash, idempotence, incomplete-membership and ambiguous-destination
## refusal at both world and sim authority boundaries, and all remaining
## scoped refusal boundaries (26).
const EXPECTED_CHECKS := 624

var passed := 0
var failed := 0
var worlds_to_release: Array = []


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	_test_bindings()
	_test_base_world()
	_test_logic_random_stream()
	_test_players_exists_and_faction()
	_test_players_start_position()
	_test_start_position_roster_normalization()
	_test_players_command_points()
	_test_players_building_count()
	_test_players_relation_to()
	_test_teams_reads()
	_test_named_script_team_registry()
	_test_team_transfer_to_player()
	_test_teams_was_destroyed()
	_test_teams_stop()
	_test_teams_behavior_state()
	_test_teams_behavior_state_determinism()
	_test_orders_move_and_attack_move()
	_test_orders_scope_and_target_refusals()
	_test_orders_attack_nearest()
	_test_orders_attack_exact_tie_prefers_lowest_id()
	_test_orders_stand_ground()
	_test_combat_player_all_destroyed()
	_test_science_and_powers()
	_test_progression_upgrades()
	_test_progression_hero_rank()
	_test_economy_money()
	_test_meta_outcomes()
	_test_script_player_binding()
	_test_ai_base_unpackable()
	_test_ai_base_unpack()
	_test_ai_build_base_building()
	_test_players_can_build_at_base()
	_test_reference_namespace()
	_test_base_state_is_hash_inert()
	_test_players_object_count_of_types()
	_test_progression_object_veterancy()
	_test_single_player_token_routing()
	_test_object_type_list_editing()
	_test_units_has_command_points_to_build()
	_test_orders_move_to_nearest_type()
	_test_nearest_type_exact_tie_break()
	_test_queries_are_read_only()
	_test_twin_worlds_agree()
	_test_named_object_reads()
	_test_named_object_refusals()
	_test_units_set_reference()
	_release_worlds()
	var ran := passed + failed
	if ran != EXPECTED_CHECKS:
		failed += 1
		printerr("RETAIL_SLICE_SCRIPT_WORLD FAIL liveness: ran %d checks, expected %d - a function aborted before its assertions" % [ran, EXPECTED_CHECKS])
	print("RETAIL_SLICE_SCRIPT_WORLD_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


func _check(name: String, condition: bool) -> void:
	if condition:
		passed += 1
	else:
		failed += 1
		printerr("FAIL %s" % name)


func _check_hit(name: String, query: SageWorldQuery, expected: Variant) -> void:
	_check("%s (answered)" % name, query.ok)
	if query.ok:
		_check("%s (value)" % name, query.value == expected)


func _check_refused(name: String, query: SageWorldQuery) -> void:
	_check("%s (refused)" % name, not query.ok and query.detail != "")


# --- Fixtures -------------------------------------------------------------


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


func _harness_rules() -> Dictionary:
	return {
		"enable_base_loop": true,
		"starting_resources": 10000,
		"ai_attack_delay_ticks": 100000,
		# The versioned pack-faction -> retail-side table every real match's
		# rules carry (players.faction answers SIDE TOKENS, never pack ids).
		"retail_faction_sides": ManifestScript.retail_faction_sides(),
		"unit_rules": {
			SimScript.SOLDIER_OBJECT_ID: _unit_rule(SimScript.SOLDIER_HORDE_ID, false),
			SimScript.ARCHER_OBJECT_ID: _unit_rule(SimScript.ARCHER_OBJECT_ID, false),
			SimScript.TOWER_GUARD_OBJECT_ID: _unit_rule(SimScript.TOWER_GUARD_OBJECT_ID, false),
			SimScript.KNIGHT_OBJECT_ID: _unit_rule(SimScript.KNIGHT_OBJECT_ID, false),
			SimScript.BUILDER_OBJECT_ID: _unit_rule(SimScript.BUILDER_OBJECT_ID, true),
		},
	}


func _make_sim(roster: Array = []) -> RetailSliceSim:
	var sim: RetailSliceSim = SimScript.new()
	sim._rules = _harness_rules()
	if not roster.is_empty():
		sim.configure_team_roster(roster)
	sim.setup({}, {})
	sim.ai_enabled = false
	return sim


func _make_world(sim: RetailSliceSim) -> RetailSliceScriptWorld:
	var world: RetailSliceScriptWorld = WorldScript.new(sim)
	worlds_to_release.append(world)
	world.bind_player(PLAYER, SimScript.PLAYER_TEAM)
	world.bind_player(ENEMY, SimScript.ENEMY_TEAM)
	world.bind_team(PLAYER_TEAM_NAME, SimScript.PLAYER_TEAM)
	world.bind_team(ENEMY_TEAM_NAME, SimScript.ENEMY_TEAM)
	return world


func _test_players_start_position() -> void:
	var missing_sim := _make_sim()
	var missing_world := _make_world(missing_sim)
	_check_refused(
		"players.start_position refuses without an authoritative assignment",
		missing_world.players().start_position(PLAYER)
	)

	var configured_sim := _make_sim([
		{"team": 0, "faction": "men", "start_index": 7},
		{"team": 1, "faction": "men", "start_index": 0},
	])
	var configured_world := _make_world(configured_sim)
	_check_hit(
		"players.start_position converts internal index 7 to authored position 8",
		configured_world.players().start_position(PLAYER),
		8
	)
	_check("start-position script player binds", configured_world.bind_script_player(PLAYER))
	_check_hit(
		"players.start_position resolves <This Player>",
		configured_world.players().start_position(
			RetailSliceScriptWorld.THIS_PLAYER_TOKEN
		),
		8
	)
	var before_hash := configured_sim.state_hash()
	configured_world.players().start_position(PLAYER)
	_check(
		"players.start_position is read-only",
		configured_sim.state_hash() == before_hash
	)

	var invalid_sim := _make_sim([
		{"team": 0, "faction": "men", "start_index": -1},
		{"team": 1, "faction": "men", "start_index": 0},
	])
	var invalid_world := _make_world(invalid_sim)
	_check_refused(
		"players.start_position refuses a negative internal assignment",
		invalid_world.players().start_position(PLAYER)
	)

	var unset_sim := _make_sim([
		{"team": 0, "faction": "men"},
		{"team": 1, "faction": "men"},
	])
	unset_sim._apply_map_configuration(_start_map_configuration({0: 1, 1: 0}))
	var unset_world := _make_world(unset_sim)
	_check(
		"injected unset roster is not backfilled from legacy map defaults",
		not unset_sim.team_descriptor(0).has("start_index")
	)
	_check_refused(
		"players.start_position refuses after legacy defaults meet injected unset roster",
		unset_world.players().start_position(PLAYER)
	)


func _start_map_configuration(team_starts: Dictionary) -> Dictionary:
	var gates: Array = []
	for x in [10.0, 20.0, 30.0]:
		gates.append({
			"edge_a": Vector2(x, 0.0),
			"edge_b": Vector2(x, 2.0),
			"center": Vector2(x, 1.0),
		})
	return {
		"spawn_positions": {
			1: Vector2(-10.0, -1.0),
			2: Vector2(-10.0, 1.0),
			101: Vector2(10.0, -1.0),
			102: Vector2(10.0, 1.0),
		},
		"ford_gates": gates,
		"player_starts": {},
		"route_provider": RouteStub.new(),
		"playable_outline": PackedVector2Array(),
		"team_start_indices": team_starts,
	}


class RouteStub:
	extends RefCounted
	func query_route(_start: Vector2, _goal: Vector2, _radius: float = 0.0) -> Array:
		return []


func _test_start_position_roster_normalization() -> void:
	var positive: Array = SimScript.normalize_authored_start_assignments([
		{"team": 0, "faction": "men", "start_index": 1},
		{"team": 1, "faction": "men", "start_index": 3},
	])
	_check(
		"menu positive starts normalize from one-based to zero-based",
		positive.size() == 2
			and int((positive[0] as Dictionary).get("start_index", -1)) == 0
			and int((positive[1] as Dictionary).get("start_index", -1)) == 2
	)
	var placeholder: Array = SimScript.normalize_authored_start_assignments([
		{"team": 0, "faction": "men", "start_index": 0},
	])
	_check(
		"menu zero start remains authoritatively absent",
		placeholder.size() == 1
			and not (placeholder[0] as Dictionary).has("start_index")
	)
	var malformed: Array = SimScript.normalize_authored_start_assignments([
		{"team": 0, "faction": "men", "start_index": "1"},
		{"team": 1, "faction": "men", "start_index": -1},
	])
	_check(
		"menu malformed starts are never coerced",
		malformed.size() == 2
			and bool((malformed[0] as Dictionary).get("start_index_invalid", false))
			and bool((malformed[1] as Dictionary).get("start_index_invalid", false))
			and not (malformed[0] as Dictionary).has("start_index")
			and not (malformed[1] as Dictionary).has("start_index")
	)
	var duplicate: Array = SimScript.normalize_authored_start_assignments([
		{"team": 0, "faction": "men", "start_index": 2},
		{"team": 1, "faction": "men", "start_index": 2},
	])
	_check(
		"menu duplicate starts invalidate both rows",
		duplicate.size() == 2
			and bool((duplicate[0] as Dictionary).get("start_index_invalid", false))
			and bool((duplicate[1] as Dictionary).get("start_index_invalid", false))
			and not (duplicate[0] as Dictionary).has("start_index")
			and not (duplicate[1] as Dictionary).has("start_index")
	)


func _track_world(world: RetailSliceScriptWorld) -> RetailSliceScriptWorld:
	worlds_to_release.append(world)
	return world


func _release_worlds() -> void:
	## Facets are cached by their world and point back to it. Release every
	## runner-created world explicitly so successful checks cannot hide leaks.
	for world in worlds_to_release:
		for facet in world._facets.values():
			facet.world = null
		world._facets.clear()
	worlds_to_release.clear()


func _test_named_script_team_registry() -> void:
	var sim := _make_sim()
	var world := _make_world(sim)
	var baseline_hash := sim.state_hash()
	var player_entities := sim.living_ids(SimScript.PLAYER_TEAM)
	var player_structures := sim.living_structure_ids(SimScript.PLAYER_TEAM)
	var alpha_handles: Array = [{"kind": "entity", "id": int(player_entities[0])}]
	if not player_structures.is_empty():
		alpha_handles.append({"kind": "structure", "id": int(player_structures[0])})
	var beta_handles: Array = [{"kind": "entity", "id": int(player_entities[1])}]
	_check(
		"script-team member id strings never coerce to entity ids",
		not bool(sim.register_script_team("badStringId", SimScript.PLAYER_TEAM, false, [{"kind": "entity", "id": str(player_entities[0])}]).get("ok", false))
	)
	_check(
		"script-team boolean ids never coerce to entity ids",
		not bool(sim.register_script_team("badBoolId", SimScript.PLAYER_TEAM, false, [{"kind": "entity", "id": true}]).get("ok", false))
	)
	_check(
		"first named sub-player team binds",
		world.bind_script_team("teamAlpha", PLAYER, alpha_handles)
	)
	_check(
		"second same-owner team remains a distinct identity",
		world.bind_script_team("teamBeta", PLAYER, beta_handles)
	)
	_check_hit(
		"team unit_count counts only entity handles, not structures",
		world.teams().unit_count("teamAlpha"),
		1
	)
	_check_hit(
		"same-owner sibling keeps its own member set",
		world.teams().unit_count("teamBeta"),
		1
	)
	_check_hit(
		"named team retains its exact player owner",
		world.teams().owner("teamAlpha"),
		PLAYER
	)
	_check(
		"named teams sharing an owner do not alias one registry row",
		(sim.script_teams["teamAlpha"] as Dictionary) != (sim.script_teams["teamBeta"] as Dictionary)
	)
	_check(
		"team state writes against the named identity",
		world.teams().set_state("teamAlpha", "AI_SYNTH_ALPHA")
	)
	_check_hit(
		"same-owner sibling state remains independent",
		world.teams().state("teamBeta"),
		""
	)
	_check(
		"installing outcome-bearing team registry state changes the hash",
		sim.state_hash() != baseline_hash
	)
	var before_recruitable_override := sim.state_hash()
	_check(
		"explicit false recruitment availability writes through the live world",
		world.teams().set_available_for_recruitment("teamAlpha", false)
		and (sim.script_teams["teamAlpha"] as Dictionary).has("recruitable")
		and not bool((sim.script_teams["teamAlpha"] as Dictionary)["recruitable"])
	)
	_check(
		"explicit false recruitment availability changes authoritative state",
		sim.state_hash() != before_recruitable_override
	)
	_check(
		"recruitment availability refuses an unknown team identity",
		not world.teams().set_available_for_recruitment("teamGhost", true)
	)
	var state := bytes_to_var(sim.snapshot()) as Dictionary
	_check(
		"snapshot carries typed named-team registry rows",
		state.has("script_teams")
		and ((state["script_teams"] as Dictionary)["teamAlpha"] as Dictionary).has("members")
		and ((state["script_teams"] as Dictionary)["teamAlpha"] as Dictionary).has("recruitable")
		and not bool(((state["script_teams"] as Dictionary)["teamAlpha"] as Dictionary)["recruitable"])
	)
	var adopted := _make_sim()
	_check("snapshot with named teams restores", adopted.restore(sim.snapshot()))
	_check("adopted named-team registry has the same hash", adopted.state_hash() == sim.state_hash())
	_check(
		"adopted membership is byte-equivalent",
		adopted.script_team_members("teamAlpha", true)
		== sim.script_team_members("teamAlpha", true)
	)
	_check(
		"adopted registry preserves an explicit false recruitment override",
		(adopted.script_teams["teamAlpha"] as Dictionary).has("recruitable")
		and not bool((adopted.script_teams["teamAlpha"] as Dictionary)["recruitable"])
	)
	adopted.setup({}, {})
	_check(
		"setup retains configured identity but clears old member handles",
		adopted.script_teams.has("teamAlpha")
		and not (adopted.script_teams["teamAlpha"] as Dictionary).has("members")
		and not (adopted.script_teams["teamAlpha"] as Dictionary).has("recruitable")
	)


func _test_team_transfer_to_player() -> void:
	var sim := _make_sim()
	var world := _make_world(sim)
	_check(
		"team transfer binds the exact civilian player",
		world.bind_player("PlyrCivilian", SimScript.NEUTRAL_TEAM)
	)
	_check(
		"team transfer binds complete memberless inheritance team evidence",
		world.bind_script_team(
			"Player_1_Inherit",
			"PlyrCivilian",
			[],
			true,
			[],
			0,
			true
		)
	)
	_check("team transfer binds executing player", world.bind_script_player(PLAYER))
	_check_hit(
		"inheritance team begins controlled by exact civilian player",
		world.teams().owner("PlyrCivilian/Player_1_Inherit"),
		"PlyrCivilian"
	)
	_check(
		"inheritance team accepts independent mutable state before transfer",
		world.teams().set_state("Player_1_Inherit", "AI_INHERIT_READY")
			and world.teams().set_available_for_recruitment(
				"Player_1_Inherit", false
			)
	)
	var before_transfer_hash := sim.state_hash()
	_check(
		"qualified inheritance team transfers to executing player",
		world.teams().transfer_to_player(
			"PlyrCivilian/Player_1_Inherit",
			WorldScript.THIS_PLAYER_TOKEN
		)
	)
	_check_hit(
		"team owner reads authoritative transferred controller",
		world.teams().owner("Player_1_Inherit"),
		PLAYER
	)
	var transferred := sim.script_teams["Player_1_Inherit"] as Dictionary
	_check(
		"team transfer preserves identity complete membership state and flags",
		String((sim.team_behavior_states["Player_1_Inherit"] as Dictionary).get("state", ""))
				== "AI_INHERIT_READY"
			and not bool(transferred.get("membership_incomplete", false))
			and bool(transferred.get("marker_only", false))
			and (transferred.get("members", []) as Array).is_empty()
			and transferred.has("recruitable")
			and not bool(transferred["recruitable"])
	)
	_check(
		"team controlling-owner transfer is hash-visible",
		sim.state_hash() != before_transfer_hash
	)
	var after_transfer_hash := sim.state_hash()
	_check(
		"team transfer is idempotent for the same destination",
		world.teams().transfer_to_player(
			"PlyrCivilian/Player_1_Inherit", PLAYER
		)
			and sim.state_hash() == after_transfer_hash
	)
	_check(
		"malformed multi-slash team qualifier refuses without mutation",
		not world.teams().transfer_to_player(
			"PlyrCivilian/extra/Player_1_Inherit", ENEMY
		)
			and sim.state_hash() == after_transfer_hash
	)
	_check(
		"wrong team-owner qualifier refuses without mutation",
		not world.teams().transfer_to_player(
			"WrongOwner/Player_1_Inherit", ENEMY
		)
			and sim.state_hash() == after_transfer_hash
	)
	_check(
		"unknown destination player refuses without mutation",
		not world.teams().transfer_to_player(
			"PlyrCivilian/Player_1_Inherit", "Nobody"
		)
			and sim.state_hash() == after_transfer_hash
	)
	_check(
		"incomplete inheritance team binds as preserved refusal evidence",
		world.bind_script_team(
			"IncompleteInherit",
			"PlyrCivilian",
			[],
			false,
			["UnresolvedCombatObject"],
			1
		)
	)
	var before_incomplete_refusal := sim.state_hash()
	_check(
		"incomplete or unmodeled team refuses transfer without mutation",
		not world.teams().transfer_to_player(
			"PlyrCivilian/IncompleteInherit", PLAYER
		)
			and sim.state_hash() == before_incomplete_refusal
			and int(
				(sim.script_team_owner("IncompleteInherit") as Dictionary).get(
					"owner", -1
				)
			) == SimScript.NEUTRAL_TEAM
	)
	_check(
		"second noncombatant player name may share the neutral sim owner",
		world.bind_player("PlyrNeutralAlias", SimScript.NEUTRAL_TEAM)
	)
	var before_ambiguous_destination := sim.state_hash()
	_check(
		"aliased noncombatant destination refuses without mutation",
		not world.teams().transfer_to_player(
			"Player_1_Inherit", "PlyrNeutralAlias"
		)
			and sim.state_hash() == before_ambiguous_destination
			and int(
				(sim.script_team_owner("Player_1_Inherit") as Dictionary).get(
					"owner", -1
				)
			) == SimScript.PLAYER_TEAM
	)
	var before_direct_noncombatant := sim.state_hash()
	_check(
		"authoritative sim also refuses a noncombatant destination",
		not bool(
			sim.transfer_script_team_controlling_player(
				"Player_1_Inherit", SimScript.NEUTRAL_TEAM
			).get("ok", false)
		)
			and sim.state_hash() == before_direct_noncombatant
	)
	var snapshot := sim.snapshot()
	var adopted := _make_sim()
	var adopted_world := _make_world(adopted)
	adopted_world.bind_player("PlyrCivilian", SimScript.NEUTRAL_TEAM)
	adopted_world.bind_script_team(
		"Player_1_Inherit",
		"PlyrCivilian",
		[],
		true,
		[],
		0,
		true
	)
	_check(
		"transferred controlling owner restores with identical state hash",
		adopted.restore(snapshot) and adopted.state_hash() == sim.state_hash()
	)
	_check_hit(
		"restored world resolves transferred controlling player",
		adopted_world.teams().owner("Player_1_Inherit"),
		PLAYER
	)
	var player_entity := int(sim.living_ids(SimScript.PLAYER_TEAM)[0])
	_check(
		"materialized combat team refuses controlling-player shortcut",
		world.bind_script_team(
			"MaterializedTeam",
			PLAYER,
			[{"kind": "entity", "id": player_entity}]
		)
			and not world.teams().transfer_to_player(
				"PlayerOne/MaterializedTeam", ENEMY
			)
	)
	_check(
		"default whole-roster team refuses inheritance-team transfer",
		not world.teams().transfer_to_player(PLAYER_TEAM_NAME, ENEMY)
	)
	sim.winner = SimScript.PLAYER_TEAM
	_check(
		"post-match team transfer refuses without changing owner",
		not world.teams().transfer_to_player("Player_1_Inherit", ENEMY)
			and int(
				(sim.script_team_owner("Player_1_Inherit") as Dictionary).get(
					"owner", -1
				)
			) == SimScript.PLAYER_TEAM
	)


func _spellbook_document() -> Dictionary:
	return {
		"schema": "openbfme.spellbook-runtime",
		"registration": {
			"spellBook": {"intrinsicSciences": []},
			"powerTree": {
				"sciences": [
					{
						"id": "SCIENCE_TestHeal",
						"purchase": {"slot": 1},
						"pointCostMP": {"value": 1},
						"prerequisiteGroups": [],
					},
				],
				"powers": [
					{
						"id": "SpellBookTestHeal",
						"requiredSciences": ["SCIENCE_TestHeal"],
						"cast": {"slot": 1, "options": ["NEED_TARGET_POS"], "iconIds": []},
						"reloadTimeMs": {"value": 5000.0},
						"radiusCursorRadius": {"value": 40.0},
						"effect": {
							"module": "PlayerHealSpecialPower",
							"fields": [
								{"key": "HealAmount", "value": "0.5"},
								{"key": "HealAsPercent", "value": "Yes"},
								{"key": "HealAffects", "value": "ANY"},
								{"key": "HealRadius", "value": "50", "resolved": 50.0},
							],
							"references": {},
						},
					},
				],
			},
			"leaves": {},
		},
	}


func _structure_id_of_kind(sim: RetailSliceSim, team: int, kind: String) -> int:
	for structure_id in sim.structure_ids(team):
		if String((sim.structures[structure_id] as Dictionary).get("structure_kind", "")) == kind:
			return structure_id
	return 0


func _inject_research_contract(sim: RetailSliceSim) -> void:
	## Post-setup injection of a synthetic barracks research so the upgrade
	## surface is exercisable without a forge-bearing pack. The per-team
	## contract tables alias this global dict for the default roster.
	sim._structure_upgrade_contracts["Upgrade_TestTech"] = {
		"structure_kind": "barracks",
		"cost": 300,
		"duration_ticks": 10,
		"level_cap": 99,
		"levels_to_gain": 0,
		"cancelable": true,
		"to_command_set": "",
		"team_tech": true,
	}


# --- 1. Bindings ----------------------------------------------------------


func _test_bindings() -> void:
	var sim := _make_sim()
	var world: RetailSliceScriptWorld = _track_world(WorldScript.new(sim))
	_check("bind_player accepts a rostered team", world.bind_player(PLAYER, 0))
	_check("bind_player rejects an unknown team", not world.bind_player("Ghost", 7))
	_check("bind_player rejects an empty name", not world.bind_player("", 1))
	_check("bind_player is idempotent for the same pair", world.bind_player(PLAYER, 0))
	_check(
		"bind_player rejects rebinding a name to another team",
		not world.bind_player(PLAYER, 1)
	)
	_check(
		"bind_player rejects a second name on a bound team",
		not world.bind_player("PlayerOneAlias", 0)
	)
	_check("bind_team accepts a rostered team", world.bind_team(PLAYER_TEAM_NAME, 0))
	_check("bind_team allows an alias onto the same team", world.bind_team("teamAlias", 0))
	_check(
		"bind_team rejects rebinding a name to another team",
		not world.bind_team(PLAYER_TEAM_NAME, 1)
	)
	_check("bind_team rejects an unknown team", not world.bind_team("teamGhost", 42))


# --- 2. Base world --------------------------------------------------------


func _test_base_world() -> void:
	var sim := _make_sim()
	var world := _make_world(sim)
	_check("supports player money", world.supports(SageScriptWorld.CAP_PLAYER_MONEY))
	_check("supports debug output", world.supports(SageScriptWorld.CAP_DEBUG_OUTPUT))
	_check(
		"supports CAP_RANDOM (the sim owns the logic stream)",
		world.supports(SageScriptWorld.CAP_RANDOM)
	)
	_check("world_frame starts at the sim tick", world.world_frame() == 0)
	sim.advance(3)
	_check("world_frame follows the sim tick", world.world_frame() == 3)
	_check("debug_message is accepted", world.debug_message("DEBUG_STRING", "hello"))


# --- 2b. The logic random stream ------------------------------------------
#
# The generator is retail's GameLogic lagged add-with-carry (see the
# logic-random section in retail_slice_sim.gd). Every literal below was
# minted by an INDEPENDENT Python implementation of the same retail source
# (RandomValue.cpp), so a change to the GDScript constants, the carry rule,
# the increment cascade, the seed expansion or the range mapping breaks
# these checks immediately - the algorithm is pinned, not just "some
# deterministic stream".


func _test_logic_random_stream() -> void:
	# Raw 32-bit draws, seed 0: the generator itself, no range mapping.
	var words: Array = SimScript._logic_random_seed_words(0)
	var raw: Array = []
	for _draw in range(8):
		raw.append(SimScript._logic_random_draw32(words))
	_check(
		"seed 0 raw draw vector matches the independent reference",
		raw == [
			1436176877, 659466229, 3894933472, 1991661106,
			2132492267, 3941127662, 1359026287, 1702175322,
		]
	)
	_check(
		"seed 0 generator words after 8 draws match the reference",
		words == [1702175322, 343149034, 2925250409, 3021019884, 489626297, 1876900716]
	)
	_check(
		"seed 42 raw draws diverge from seed 0 as the reference says",
		SimScript._logic_random_draw32(SimScript._logic_random_seed_words(42)) == 1436177129
	)

	# The mapped surface through the world adapter (seed 0 via default rules:
	# no logic_random_seed key). [1..3] is the spell-list-choice shape.
	var sim := _make_sim()
	var world := _make_world(sim)
	var choices: Array = []
	for _draw in range(12):
		choices.append(world.random_int(1, 3))
	_check(
		"seed 0 [1,3] sequence matches the reference (both bounds inclusive)",
		choices == [3, 2, 2, 2, 3, 3, 2, 1, 3, 3, 3, 1]
	)
	var seeded := _make_sim()
	seeded._rules["logic_random_seed"] = 42
	var seeded_world := _make_world(seeded)
	var seeded_choices: Array = []
	for _draw in range(12):
		seeded_choices.append(seeded_world.random_int(5, 9))
	_check(
		"a rules-configured seed selects a different pinned sequence",
		seeded_choices == [9, 6, 9, 8, 6, 6, 6, 6, 6, 9, 9, 5]
	)

	# Retail's edge semantics, INCLUDING stream position (position is part of
	# the lockstep contract, so a "harmless" shortcut that skips or adds a
	# draw is a desync).
	var edge := _make_sim()
	var edge_world := _make_world(edge)
	_check("low == high answers that value", edge_world.random_int(7, 7) == 7)
	_check(
		"low == high CONSUMED a draw (retail delta=1 still draws)",
		edge_world.random_int(1, 3) == 2
	)
	var edge2 := _make_sim()
	var edge2_world := _make_world(edge2)
	_check("high == low - 1 answers high (retail delta==0)", edge2_world.random_int(5, 4) == 4)
	_check(
		"high == low - 1 did NOT consume a draw (retail returns before drawing)",
		edge2_world.random_int(1, 3) == 3
	)

	# random_real: REAL bounds truncate toward zero and ONE integer is drawn
	# (retail's script engine routes REAL bounds through the C-int
	# GameLogicRandomValue; see the adapter comment).
	var real_sim := _make_sim()
	var real_world := _make_world(real_sim)
	_check(
		"random_real truncates REAL bounds toward zero and draws the int stream",
		real_world.random_real(2.9, 5.9) == 3.0
	)
	_check(
		"random_real truncation toward zero holds for negative bounds too",
		real_world.random_real(-3.7, -1.2) == -2.0
	)

	# Determinism: two sims with identical rules replay the identical
	# sequence; the draw tally is diagnostic, not hashed.
	var twin_a := _make_sim()
	var twin_b := _make_sim()
	var world_a := _make_world(twin_a)
	var world_b := _make_world(twin_b)
	var seq_a: Array = []
	var seq_b: Array = []
	for _draw in range(8):
		seq_a.append(world_a.random_int(1, 100))
		seq_b.append(world_b.random_int(1, 100))
	_check(
		"twin sims replay the identical mapped sequence",
		seq_a == seq_b and seq_a == [78, 30, 73, 7, 68, 63, 88, 23]
	)
	_check(
		"the draw tally is a process-local diagnostic that counted every draw",
		twin_a.logic_random_draws == 8
	)


# --- 3. Players -----------------------------------------------------------


func _test_players_exists_and_faction() -> void:
	var sim := _make_sim()
	var world := _make_world(sim)
	_check_hit("players.exists for a bound player", world.players().exists(PLAYER), true)
	_check_hit(
		"players.exists is false for an unbound name (exhaustive bindings)",
		world.players().exists("Nobody"),
		false
	)
	_check_refused(
		"players.faction refuses when the descriptor has no faction",
		world.players().faction(PLAYER)
	)
	var faction_sim := _make_sim([
		{"team": 0, "faction": "men", "is_ai": false},
		{"team": 1, "faction": "men", "is_ai": true},
	])
	var faction_world := _make_world(faction_sim)
	# players.faction answers the RETAIL SIDE TOKEN (playertemplate.ini
	# `Side = Men`), never the lowercase pack id the descriptor carries:
	# retail's SKIRMISH_PLAYER_FACTION compares player->getSide() by exact
	# string, and the corpus authors "Men"/"Isengard"/... - a pack id answered
	# here turns every live-match faction gate false-but-plausible.
	_check_hit(
		"players.faction answers the retail side token for the descriptor's pack faction",
		faction_world.players().faction(PLAYER),
		"Men"
	)
	var unmapped_sim := _make_sim([
		{"team": 0, "faction": "modfolk", "is_ai": false},
		{"team": 1, "faction": "men", "is_ai": true},
	])
	var unmapped_world := _make_world(unmapped_sim)
	var unmapped_query := unmapped_world.players().faction(PLAYER)
	_check_refused(
		"players.faction refuses a pack faction with no retail side mapping",
		unmapped_query
	)
	_check(
		"the unmapped-faction refusal names the faction and the mapping table",
		unmapped_query.detail.contains("modfolk")
			and unmapped_query.detail.contains("retail_faction_sides")
	)
	# The mapping is data the match configuration must carry: a sim whose
	# rules ship NO retail_faction_sides table refuses rather than passing
	# the pack id through as if it were a side.
	var tableless_sim: RetailSliceSim = SimScript.new()
	var tableless_rules := _harness_rules()
	tableless_rules.erase("retail_faction_sides")
	tableless_sim._rules = tableless_rules
	tableless_sim.configure_team_roster([
		{"team": 0, "faction": "men", "is_ai": false},
		{"team": 1, "faction": "men", "is_ai": true},
	])
	tableless_sim.setup({}, {})
	tableless_sim.ai_enabled = false
	var tableless_world := _make_world(tableless_sim)
	_check_refused(
		"players.faction refuses when the rules carry no retail_faction_sides table",
		tableless_world.players().faction(PLAYER)
	)
	# Mapping resolution is exact on the pack id: the census's old hand-fed
	# side-token descriptor ("Men") is NOT a pack faction id and must refuse,
	# not silently pass through because it already looks like a side.
	var side_token_sim := _make_sim([
		{"team": 0, "faction": "Men", "is_ai": false},
		{"team": 1, "faction": "men", "is_ai": true},
	])
	var side_token_world := _make_world(side_token_sim)
	_check_refused(
		"players.faction refuses a descriptor carrying a side token instead of a pack id",
		side_token_world.players().faction(PLAYER)
	)


func _test_players_command_points() -> void:
	var sim := _make_sim()
	var world := _make_world(sim)
	var total := world.players().command_points_total(PLAYER)
	_check_hit("players.command_points_total is the cap", total, sim.command_point_cap)
	var used_before := world.players().command_points_used(PLAYER)
	_check(
		"players.command_points_used answers the committed points",
		used_before.ok and used_before.as_int() == sim.command_points_for_team(0)
	)
	var available_before := world.players().command_points_available(PLAYER)
	_check(
		"players.command_points_available is cap minus committed before queueing",
		available_before.ok
		and available_before.as_int() == sim.command_point_cap - used_before.as_int()
	)
	var barracks := _structure_id_of_kind(sim, 0, "barracks")
	var queued: Dictionary = sim.queue_unit(0, barracks, SimScript.SOLDIER_HORDE_ID)
	_check("fixture: soldier horde queues at the barracks", bool(queued.get("ok", false)))
	var reserved := int((queued.get("item", {}) as Dictionary).get("command_points", 0))
	_check("fixture: the queued soldier reserves command points", reserved > 0)
	var available_after := world.players().command_points_available(PLAYER)
	_check(
		"players.command_points_available subtracts queue-reserved points",
		available_after.ok
		and available_after.as_int() == available_before.as_int() - reserved
	)
	_check_refused(
		"players.command_points_available refuses an unbound player",
		world.players().command_points_available("Nobody")
	)


func _test_players_building_count() -> void:
	var sim := _make_sim()
	var world := _make_world(sim)
	var seeded := sim.living_structure_ids(0).size()
	_check_hit(
		"players.building_count with empty class counts every living structure",
		world.players().building_count(PLAYER, ""),
		seeded
	)
	_check_hit(
		"players.building_count counts one farm",
		world.players().building_count(PLAYER, "farm"),
		1
	)
	var farm := _structure_id_of_kind(sim, 0, "farm")
	(sim.structures[farm] as Dictionary)["health"] = 0
	_check_hit(
		"players.building_count drops a razed farm",
		world.players().building_count(PLAYER, "farm"),
		0
	)
	_check_hit(
		"players.building_count empty-class drops the razed farm too",
		world.players().building_count(PLAYER, ""),
		seeded - 1
	)
	_check_refused(
		"players.building_count refuses an unmodeled building class",
		world.players().building_count(PLAYER, "inn")
	)


func _test_players_relation_to() -> void:
	var relation: Dictionary = ParamTypes.ENUMS["RELATION"]
	var sim := _make_sim()
	var world := _make_world(sim)
	_check_hit(
		"players.relation_to hostile roster pair is Enemy",
		world.players().relation_to(PLAYER, ENEMY),
		int(relation["Enemy"])
	)
	_check_hit(
		"players.relation_to self is Friend",
		world.players().relation_to(PLAYER, PLAYER),
		int(relation["Friend"])
	)
	_check_refused(
		"players.relation_to refuses an unbound other player",
		world.players().relation_to(PLAYER, "Nobody")
	)
	var allied_sim := _make_sim([
		{"team": 0, "faction": "", "is_ai": false, "alliance": "east"},
		{"team": 1, "faction": "", "is_ai": true, "alliance": "east"},
	])
	var allied_world := _make_world(allied_sim)
	_check_hit(
		"players.relation_to shared alliance is Friend",
		allied_world.players().relation_to(PLAYER, ENEMY),
		int(relation["Friend"])
	)


# --- 4. Teams -------------------------------------------------------------


func _test_teams_reads() -> void:
	var sim := _make_sim()
	var world := _make_world(sim)
	_check_hit("teams.exists for a bound team", world.teams().exists(PLAYER_TEAM_NAME), true)
	_check_hit(
		"teams.exists is false for an unbound name", world.teams().exists("teamGhost"), false
	)
	_check_hit(
		"teams.unit_count counts living battalions",
		world.teams().unit_count(PLAYER_TEAM_NAME),
		sim.living_ids(0).size()
	)
	var before := sim.living_ids(0).size()
	(sim.entities[2] as Dictionary)["health"] = 0
	_check_hit(
		"teams.unit_count drops a dead battalion",
		world.teams().unit_count(PLAYER_TEAM_NAME),
		before - 1
	)
	_check_hit(
		"teams.owner answers the bound player name",
		world.teams().owner(PLAYER_TEAM_NAME),
		PLAYER
	)
	var half_bound: RetailSliceScriptWorld = _track_world(WorldScript.new(sim))
	half_bound.bind_team(PLAYER_TEAM_NAME, 0)
	_check_refused(
		"teams.owner refuses when no player name is bound to the team",
		half_bound.teams().owner(PLAYER_TEAM_NAME)
	)


func _test_teams_was_destroyed() -> void:
	## TEAM_DESTROYED. Retail's evaluateIsDestroyed is
	## `theTeam ? !theTeam->hasAnyObjects() : false` - a LEVEL read, not an
	## edge, and hasAnyObjects INCLUDES STRUCTURES (hasAnyUnits, which
	## TEAM_HAS_UNITS uses, is the one that excludes them). The structure arm
	## below is the assertion that separates the two: wiring this member to
	## unit_count() == 0 would report a team down to its fortress as
	## destroyed, which retail does not.
	var sim := _make_sim()
	var world := _make_world(sim)
	_check("fixture: the bound team has living battalions", not sim.living_ids(0).is_empty())
	_check(
		"fixture: the bound team has living structures",
		not sim.living_structure_ids(0).is_empty()
	)
	_check_hit(
		"teams.was_destroyed is false while the team has objects",
		world.teams().was_destroyed(PLAYER_TEAM_NAME),
		false
	)
	for entity_id in sim.living_ids(0):
		(sim.entities[entity_id] as Dictionary)["health"] = 0
	_check(
		"fixture: every battalion of the bound team is now dead",
		sim.living_ids(0).is_empty()
	)
	_check_hit(
		"teams.was_destroyed stays FALSE while only structures remain (hasAnyObjects, not hasAnyUnits)",
		world.teams().was_destroyed(PLAYER_TEAM_NAME),
		false
	)
	for structure_id in sim.living_structure_ids(0):
		(sim.structures[structure_id] as Dictionary)["health"] = 0
	_check_hit(
		"teams.was_destroyed is true once no living object remains",
		world.teams().was_destroyed(PLAYER_TEAM_NAME),
		true
	)
	_check_hit(
		"teams.was_destroyed is unaffected for the other bound team",
		world.teams().was_destroyed(ENEMY_TEAM_NAME),
		false
	)
	_check_refused(
		"teams.was_destroyed refuses an unbound name (unbound is not proof of nonexistence)",
		world.teams().was_destroyed("teamGhost")
	)
	_check_refused(
		"teams.was_destroyed refuses '<This Team>' (no executing-team context)",
		world.teams().was_destroyed("<This Team>")
	)
	var before := sim.state_hash()
	world.teams().was_destroyed(PLAYER_TEAM_NAME)
	world.teams().was_destroyed(ENEMY_TEAM_NAME)
	world.teams().was_destroyed("teamGhost")
	_check(
		"teams.was_destroyed leaves state_hash untouched (condition path)",
		sim.state_hash() == before
	)

	# THE SNAPSHOT BOUNDARY. This member adds no state of its own, so the
	# assertion that matters is that everything it reads is inside what
	# snapshot()/restore() reproduce: a peer adopting a snapshot must answer
	# TEAM_DESTROYED exactly as the peer that wrote it. Sever either carry
	# (the entity rows or the structure rows) and the corresponding branch
	# below goes red.
	var adopted: RetailSliceSim = SimScript.new()
	adopted._rules = _harness_rules()
	adopted.setup({}, {})
	adopted.ai_enabled = false
	adopted.restore(sim.snapshot())
	var adopted_world := _make_world(adopted)
	_check_hit(
		"teams.was_destroyed survives snapshot/restore for the wiped team",
		adopted_world.teams().was_destroyed(PLAYER_TEAM_NAME),
		true
	)
	_check_hit(
		"teams.was_destroyed survives snapshot/restore for the surviving team",
		adopted_world.teams().was_destroyed(ENEMY_TEAM_NAME),
		false
	)
	var structures_only: RetailSliceSim = SimScript.new()
	structures_only._rules = _harness_rules()
	structures_only.setup({}, {})
	structures_only.ai_enabled = false
	var mid := _make_sim()
	for entity_id in mid.living_ids(0):
		(mid.entities[entity_id] as Dictionary)["health"] = 0
	structures_only.restore(mid.snapshot())
	_check_hit(
		"teams.was_destroyed survives snapshot/restore with only structures left",
		_make_world(structures_only).teams().was_destroyed(PLAYER_TEAM_NAME),
		false
	)
	# The mirror case, and it is not redundant: a mutation run proved that
	# WITHOUT it, severing the ENTITY carry from snapshot() left every
	# assertion above green - each of them happens to answer the same way
	# with an empty entity table. Only a team that is alive SOLELY through
	# its battalions distinguishes the two.
	var battalions_only: RetailSliceSim = SimScript.new()
	battalions_only._rules = _harness_rules()
	battalions_only.setup({}, {})
	battalions_only.ai_enabled = false
	var razed := _make_sim()
	for structure_id in razed.living_structure_ids(0):
		(razed.structures[structure_id] as Dictionary)["health"] = 0
	_check(
		"fixture: the razed team still has living battalions",
		not razed.living_ids(0).is_empty() and razed.living_structure_ids(0).is_empty()
	)
	battalions_only.restore(razed.snapshot())
	_check_hit(
		"teams.was_destroyed survives snapshot/restore with only battalions left",
		_make_world(battalions_only).teams().was_destroyed(PLAYER_TEAM_NAME),
		false
	)


func _test_teams_stop() -> void:
	var sim := _make_sim()
	var world := _make_world(sim)
	_check(
		"fixture: move order puts the team in motion",
		world.orders().move_to(
			SageScriptWorld.Scope.TEAM,
			PLAYER_TEAM_NAME,
			SageScriptWorld.target_position(Vector3(-20.0, 0.0, -14.0))
		)
	)
	_check(
		"fixture: a battalion is running", String((sim.entities[1] as Dictionary)["state"]) == "run"
	)
	_check("teams.stop is accepted", world.teams().stop(PLAYER_TEAM_NAME, false))
	_check(
		"teams.stop idles the battalion",
		String((sim.entities[1] as Dictionary)["state"]) == "idle"
	)
	_check(
		"teams.stop refuses the disband variant",
		not world.teams().stop(PLAYER_TEAM_NAME, true)
	)
	_check(
		"teams.stop refuses an unbound team", not world.teams().stop("teamGhost", false)
	)


func _test_teams_behavior_state() -> void:
	## The team-behavior-state surface: the retail TEAM_STATE string
	## (default "", exact case-sensitive storage) and the custom-state token
	## SET (sorted membership; the enable flag inserts/removes). Sourced
	## semantics are documented on the sim store; this proves the adapter
	## delivers them.
	var sim := _make_sim()
	var world := _make_world(sim)

	# TEAM_STATE: retail's default for a team never set is the EMPTY string
	# (Team's m_state is default-constructed), so "" here is a truthful
	# answer, and TEAM_STATE_IS against any non-empty token is false.
	_check_hit(
		"teams.state answers retail's empty default for a never-set team",
		world.teams().state(PLAYER_TEAM_NAME),
		""
	)
	_check(
		"teams.set_state is accepted for a bound team",
		world.teams().set_state(PLAYER_TEAM_NAME, "AI_SYNTH_ATTACKING")
	)
	_check_hit(
		"teams.state reads back the exact token, case preserved",
		world.teams().state(PLAYER_TEAM_NAME),
		"AI_SYNTH_ATTACKING"
	)
	_check(
		"fixture: a second set_state overwrites (one string per team)",
		world.teams().set_state(PLAYER_TEAM_NAME, "AI_SYNTH_RETREATING")
	)
	_check_hit(
		"teams.state answers the overwritten token, not the first",
		world.teams().state(PLAYER_TEAM_NAME),
		"AI_SYNTH_RETREATING"
	)
	_check(
		"setting the empty string returns the team to the retail default",
		world.teams().set_state(PLAYER_TEAM_NAME, "")
		and world.teams().state(PLAYER_TEAM_NAME).value == ""
	)

	# Custom states: a SET of independent tokens (the writer's boolean is
	# what rules the shape), answered as a sorted Array.
	_check_hit(
		"teams.custom_state answers the empty set for a team never toggled",
		world.teams().custom_state(PLAYER_TEAM_NAME),
		[]
	)
	_check(
		"fixture: two tokens enable (deliberately out of sorted order)",
		world.teams().set_custom_state(PLAYER_TEAM_NAME, "AI_SYNTH_B", true)
		and world.teams().set_custom_state(PLAYER_TEAM_NAME, "AI_SYNTH_A", true)
	)
	_check_hit(
		"teams.custom_state answers the sorted token set",
		world.teams().custom_state(PLAYER_TEAM_NAME),
		["AI_SYNTH_A", "AI_SYNTH_B"]
	)
	_check(
		"a duplicate enable is a set no-op",
		world.teams().set_custom_state(PLAYER_TEAM_NAME, "AI_SYNTH_A", true)
		and world.teams().custom_state(PLAYER_TEAM_NAME).value == ["AI_SYNTH_A", "AI_SYNTH_B"]
	)
	_check(
		"disable removes exactly the named token",
		world.teams().set_custom_state(PLAYER_TEAM_NAME, "AI_SYNTH_B", false)
		and world.teams().custom_state(PLAYER_TEAM_NAME).value == ["AI_SYNTH_A"]
	)
	_check(
		"disabling an absent token is a successful set no-op",
		world.teams().set_custom_state(PLAYER_TEAM_NAME, "AI_SYNTH_NEVER_SET", false)
	)
	_check(
		"an empty custom-state token refuses (it names nothing)",
		not world.teams().set_custom_state(PLAYER_TEAM_NAME, "", true)
	)
	# The adapter's copies are defensive: mutating an answered array must not
	# reach the authoritative store (a condition path could otherwise write).
	var leaked: Array = world.teams().custom_state(PLAYER_TEAM_NAME).value
	leaked.append("AI_SYNTH_INJECTED")
	_check(
		"the answered token array is a defensive copy",
		world.teams().custom_state(PLAYER_TEAM_NAME).value == ["AI_SYNTH_A"]
	)

	# "<This Team>" - the spelling the retail AI authors at essentially every
	# call site - refuses with the missing executing-team context named; the
	# script player's whole roster would be the WRONG team. And the spelling
	# can never be bound as a literal name to shadow the token.
	_check_refused(
		"teams.state refuses '<This Team>' (no executing-team context)",
		world.teams().state("<This Team>")
	)
	_check(
		"teams.set_state refuses '<This Team>' too",
		not world.teams().set_state("<This Team>", "AI_SYNTH_X")
	)
	_check(
		"bind_team refuses the reserved '<This Team>' spelling as a name",
		not world.bind_team("<This Team>", SimScript.PLAYER_TEAM)
	)
	_check_refused(
		"teams.exists refuses '<This Team>' rather than answering false",
		world.teams().exists("<This Team>")
	)

	# Unbound names refuse (the sub-player team registry is unmodeled; an
	# unbound name is not proof of nonexistence for a STATE read).
	_check_refused(
		"teams.state refuses an unbound team name",
		world.teams().state("teamGhost")
	)
	_check(
		"teams.set_custom_state refuses an unbound team name",
		not world.teams().set_custom_state("teamGhost", "AI_SYNTH_A", true)
	)

	# Still refusing on this facet, deliberately (see the SliceTeams class
	# comment): the attitude write without its mood-matrix consumption would
	# be a silent semantic no-op, not a service.
	_check(
		"teams.set_attitude keeps its refusal (mood matrix unmodeled)",
		not world.teams().set_attitude(PLAYER_TEAM_NAME, 2)
	)

	# Writes refuse once the match is resolved (the teams.stop precedent);
	# reads still answer - the state is still a fact of the match.
	var decided := _make_sim()
	var decided_world := _make_world(decided)
	_check(
		"fixture: a pre-resolution write lands",
		decided_world.teams().set_state(PLAYER_TEAM_NAME, "AI_SYNTH_HOLD")
	)
	decided.winner = 0
	_check(
		"team-state writes refuse after the match is resolved",
		not decided_world.teams().set_state(PLAYER_TEAM_NAME, "AI_SYNTH_LATE")
		and not decided_world.teams().set_custom_state(PLAYER_TEAM_NAME, "AI_SYNTH_A", true)
	)
	_check_hit(
		"team-state reads still answer after resolution",
		decided_world.teams().state(PLAYER_TEAM_NAME),
		"AI_SYNTH_HOLD"
	)


func _test_teams_behavior_state_determinism() -> void:
	## Requirement: two INDEPENDENTLY built sims driven through two
	## independently built worlds by the identical write sequence agree on
	## state_hash() and on every new query - the property lockstep peers
	## depend on. Iteration order inside the store never reaches an answer
	## (tokens are kept sorted; the state is a single string), and this is
	## where that claim is enforced.
	var sim_a := _make_sim()
	var world_a := _make_world(sim_a)
	var sim_b := _make_sim()
	var world_b := _make_world(sim_b)
	for world in [world_a, world_b]:
		var teams := (world as RetailSliceScriptWorld).teams()
		teams.set_state(PLAYER_TEAM_NAME, "AI_SYNTH_ATTACKING")
		teams.set_state(ENEMY_TEAM_NAME, "AI_SYNTH_DEFENDING")
		teams.set_custom_state(PLAYER_TEAM_NAME, "AI_SYNTH_B", true)
		teams.set_custom_state(PLAYER_TEAM_NAME, "AI_SYNTH_A", true)
		teams.set_custom_state(ENEMY_TEAM_NAME, "AI_SYNTH_C", true)
		teams.set_custom_state(PLAYER_TEAM_NAME, "AI_SYNTH_B", false)
	_check(
		"independently built sims agree on the state hash after identical writes",
		sim_a.state_hash() == sim_b.state_hash()
	)
	_check(
		"independently built worlds agree on every team-state query",
		world_a.teams().state(PLAYER_TEAM_NAME).value
		== world_b.teams().state(PLAYER_TEAM_NAME).value
		and world_a.teams().state(ENEMY_TEAM_NAME).value
		== world_b.teams().state(ENEMY_TEAM_NAME).value
		and world_a.teams().custom_state(PLAYER_TEAM_NAME).value
		== world_b.teams().custom_state(PLAYER_TEAM_NAME).value
		and world_a.teams().custom_state(ENEMY_TEAM_NAME).value
		== world_b.teams().custom_state(ENEMY_TEAM_NAME).value
	)
	_check(
		"the agreed answers are the authored ones, not a vacuous agreement",
		world_a.teams().state(PLAYER_TEAM_NAME).value == "AI_SYNTH_ATTACKING"
		and world_a.teams().custom_state(PLAYER_TEAM_NAME).value == ["AI_SYNTH_A"]
		and world_a.teams().custom_state(ENEMY_TEAM_NAME).value == ["AI_SYNTH_C"]
	)


# --- 5. Orders ------------------------------------------------------------


func _test_orders_move_and_attack_move() -> void:
	var sim := _make_sim()
	var world := _make_world(sim)
	_check(
		"orders.move_to TEAM scope POSITION target is accepted",
		world.orders().move_to(
			SageScriptWorld.Scope.TEAM,
			PLAYER_TEAM_NAME,
			SageScriptWorld.target_position(Vector3(-20.0, 5.0, -14.0))
		)
	)
	var row: Dictionary = sim.entities[1]
	_check("move order lands as order_kind move", String(row["order_kind"]) == "move")
	_check(
		"move destination maps world (x,z) onto sim (x,y)",
		Vector2(row["destination"]).distance_to(Vector2(-20.0, -14.0)) < 0.001
	)
	_check(
		"orders.attack_move_to PLAYER scope is accepted",
		world.orders().attack_move_to(
			SageScriptWorld.Scope.PLAYER,
			PLAYER,
			SageScriptWorld.target_position(Vector3(-18.0, 0.0, -10.0))
		)
	)
	_check(
		"attack-move order lands as order_kind attack_move",
		String((sim.entities[1] as Dictionary)["order_kind"]) == "attack_move"
	)


func _test_orders_scope_and_target_refusals() -> void:
	var sim := _make_sim()
	var world := _make_world(sim)
	_check(
		"orders.move_to refuses UNIT scope (no object-name binding)",
		not world.orders().move_to(
			SageScriptWorld.Scope.UNIT,
			"SomeNamedUnit",
			SageScriptWorld.target_position(Vector3.ZERO)
		)
	)
	_check(
		"orders.move_to refuses a WAYPOINT target (no map geometry)",
		not world.orders().move_to(
			SageScriptWorld.Scope.TEAM,
			PLAYER_TEAM_NAME,
			SageScriptWorld.target_waypoint("Waypoint01")
		)
	)
	_check(
		"orders.attack refuses an OBJECT target (no object-name binding)",
		not world.orders().attack(
			SageScriptWorld.Scope.TEAM,
			PLAYER_TEAM_NAME,
			SageScriptWorld.target_object("SomeNamedUnit")
		)
	)
	_check(
		"orders.attack refuses an unbound target team",
		not world.orders().attack(
			SageScriptWorld.Scope.TEAM,
			PLAYER_TEAM_NAME,
			SageScriptWorld.target_team("teamGhost")
		)
	)


func _position_enemy_spread(sim: RetailSliceSim, near_a: Vector2, near_b: Vector2) -> void:
	## Attacker 1 at the origin; enemy 101/102 at the given spots; the other
	## enemy battalions far away so they never win the pick.
	(sim.entities[1] as Dictionary)["position"] = Vector2.ZERO
	(sim.entities[101] as Dictionary)["position"] = near_a
	(sim.entities[102] as Dictionary)["position"] = near_b
	(sim.entities[103] as Dictionary)["position"] = Vector2(50.0, 50.0)
	(sim.entities[104] as Dictionary)["position"] = Vector2(60.0, 60.0)


func _test_orders_attack_nearest() -> void:
	var sim := _make_sim()
	var world := _make_world(sim)
	_position_enemy_spread(sim, Vector2(10.0, 0.0), Vector2(0.0, 5.0))
	_check(
		"orders.attack TEAM target is accepted",
		world.orders().attack(
			SageScriptWorld.Scope.TEAM,
			PLAYER_TEAM_NAME,
			SageScriptWorld.target_team(ENEMY_TEAM_NAME)
		)
	)
	_check(
		"orders.attack picks the strictly nearest target member",
		int((sim.entities[1] as Dictionary)["target_id"]) == 102
	)


func _test_orders_attack_exact_tie_prefers_lowest_id() -> void:
	var sim := _make_sim()
	var world := _make_world(sim)
	# 101 and 102 are EXACTLY equidistant from the lowest-id attacker
	# (distance_squared 100.0 on both sides): only the documented total order
	# (distance, then lowest id) decides.
	_position_enemy_spread(sim, Vector2(10.0, 0.0), Vector2(-10.0, 0.0))
	_check(
		"orders.attack tie order is accepted",
		world.orders().attack(
			SageScriptWorld.Scope.TEAM,
			PLAYER_TEAM_NAME,
			SageScriptWorld.target_team(ENEMY_TEAM_NAME)
		)
	)
	_check(
		"orders.attack exact tie resolves to the lowest candidate id",
		int((sim.entities[1] as Dictionary)["target_id"]) == 101
	)
	_check(
		"orders.attack refuses when the target team has no living member",
		not _attack_on_emptied_team()
	)


func _attack_on_emptied_team() -> bool:
	var sim := _make_sim()
	var world := _make_world(sim)
	for id in sim.living_ids(1):
		(sim.entities[id] as Dictionary)["health"] = 0
	return world.orders().attack(
		SageScriptWorld.Scope.TEAM,
		PLAYER_TEAM_NAME,
		SageScriptWorld.target_team(ENEMY_TEAM_NAME)
	)


func _test_orders_stand_ground() -> void:
	var sim := _make_sim()
	var world := _make_world(sim)
	var player_ids := sim.living_ids(0)
	_check("stand-ground fixture has two player battalions", player_ids.size() >= 2)
	_check(
		"stand-ground first named sub-team binds",
		world.bind_script_team(
			"StandAlpha", PLAYER, [{"kind": "entity", "id": int(player_ids[0])}]
		)
	)
	_check(
		"stand-ground same-owner sibling binds independently",
		world.bind_script_team(
			"StandBeta", PLAYER, [{"kind": "entity", "id": int(player_ids[1])}]
		)
	)
	sim.issue_set_stance(player_ids, "Aggressive", 0)
	_check(
		"orders.stand_ground named TEAM scope is accepted",
		world.orders().stand_ground(SageScriptWorld.Scope.TEAM, "StandAlpha", true)
	)
	_check(
		"TEAM SET touches only the addressed named sub-team",
		String((sim.entities[player_ids[0]] as Dictionary)["stance"]) == "HoldGround"
		and String((sim.entities[player_ids[1]] as Dictionary)["stance"]) == "Aggressive"
	)
	_check(
		"orders.stand_ground CLEAR is accepted",
		world.orders().stand_ground(SageScriptWorld.Scope.TEAM, "StandAlpha", false)
	)
	_check(
		"TEAM CLEAR selects Battle without restoring or contaminating its sibling",
		String((sim.entities[player_ids[0]] as Dictionary)["stance"]) == "Battle"
		and String((sim.entities[player_ids[1]] as Dictionary)["stance"]) == "Aggressive"
	)
	_check(
		"orders.stand_ground PLAYER scope is accepted",
		world.orders().stand_ground(SageScriptWorld.Scope.PLAYER, PLAYER, true)
	)
	var all_hold := true
	for id in player_ids:
		if String((sim.entities[id] as Dictionary)["stance"]) != "HoldGround":
			all_hold = false
	_check("PLAYER scope applies HoldGround to the resolved whole roster", all_hold)
	_check(
		"orders.stand_ground refuses an unknown TEAM identity",
		not world.orders().stand_ground(
			SageScriptWorld.Scope.TEAM, "StandGhost", true
		)
	)
	_check(
		"orders.stand_ground refuses <This Team> without execution context",
		not world.orders().stand_ground(
			SageScriptWorld.Scope.TEAM, "<This Team>", false
		)
	)
	_check(
		"orders.stand_ground refuses UNIT scope",
		not world.orders().stand_ground(
			SageScriptWorld.Scope.UNIT, "SomeNamedUnit", true
		)
	)


# --- 6. Combat ------------------------------------------------------------


func _test_combat_player_all_destroyed() -> void:
	var sim := _make_sim()
	var world := _make_world(sim)
	_check_hit(
		"combat.player_all_destroyed is false while anything lives",
		world.combat().player_all_destroyed(ENEMY, false),
		false
	)
	for id in sim.living_ids(1):
		(sim.entities[id] as Dictionary)["health"] = 0
	_check_hit(
		"combat.player_all_destroyed stays false while structures live",
		world.combat().player_all_destroyed(ENEMY, false),
		false
	)
	for structure_id in sim.living_structure_ids(1):
		(sim.structures[structure_id] as Dictionary)["health"] = 0
	_check_hit(
		"combat.player_all_destroyed is true after the full wipe",
		world.combat().player_all_destroyed(ENEMY, false),
		true
	)
	_check_refused(
		"combat.player_all_destroyed refuses the build-facilities-only variant",
		world.combat().player_all_destroyed(ENEMY, true)
	)


# --- 7. Sciences and powers -----------------------------------------------


func _test_science_and_powers() -> void:
	var sim := _make_sim()
	var world := _make_world(sim)
	_check_refused(
		"progression.has_science refuses without a spellbook document",
		world.progression().has_science(PLAYER, "SCIENCE_TestHeal")
	)
	_check(
		"fixture: synthetic spellbook document configures",
		sim.configure_spellbook_runtime(_spellbook_document())
	)
	_check_hit(
		"progression.science_purchase_points answers the seeded pool",
		world.progression().science_purchase_points(PLAYER),
		sim.power_points(0)
	)
	_check_hit(
		"progression.has_science is false before purchase",
		world.progression().has_science(PLAYER, "SCIENCE_TestHeal"),
		false
	)
	_check_refused(
		"progression.has_science refuses a science outside the tree",
		world.progression().has_science(PLAYER, "SCIENCE_Bogus")
	)
	_check_hit(
		"combat.special_power_ready is false before the power is owned",
		world.combat().special_power_ready(
			SageScriptWorld.Scope.PLAYER, PLAYER, "SpellBookTestHeal"
		),
		false
	)
	_check_hit(
		"progression.can_purchase_science is true with points in hand",
		world.progression().can_purchase_science(PLAYER, "SCIENCE_TestHeal"),
		true
	)
	_check(
		"progression.purchase_science buys the power",
		world.progression().purchase_science(PLAYER, "SCIENCE_TestHeal")
	)
	_check_hit(
		"progression.has_science is true after purchase",
		world.progression().has_science(PLAYER, "SCIENCE_TestHeal"),
		true
	)
	_check_hit(
		"progression.can_purchase_science is false once owned",
		world.progression().can_purchase_science(PLAYER, "SCIENCE_TestHeal"),
		false
	)
	_check(
		"progression.purchase_science refuses a second purchase",
		not world.progression().purchase_science(PLAYER, "SCIENCE_TestHeal")
	)
	_check_hit(
		"combat.special_power_ready is true when owned and off cooldown",
		world.combat().special_power_ready(
			SageScriptWorld.Scope.PLAYER, PLAYER, "SpellBookTestHeal"
		),
		true
	)
	_check_refused(
		"combat.special_power_ready refuses a power outside the document",
		world.combat().special_power_ready(
			SageScriptWorld.Scope.PLAYER, PLAYER, "SpellBookBogus"
		)
	)
	# Wound a member so the heal has a target, then cast at its position.
	var row: Dictionary = sim.entities[1]
	(row["member_health"] as Array)[0] = 100
	row["health"] = 100
	var at := Vector2(row["position"])
	_check(
		"combat.fire_special_power casts the owned power",
		world.combat().fire_special_power(
			SageScriptWorld.Scope.PLAYER,
			PLAYER,
			"SpellBookTestHeal",
			SageScriptWorld.target_position(Vector3(at.x, 0.0, at.y))
		)
	)
	_check("the cast actually healed the battalion", int(row["health"]) > 100)
	_check_hit(
		"combat.special_power_ready is false while recharging",
		world.combat().special_power_ready(
			SageScriptWorld.Scope.PLAYER, PLAYER, "SpellBookTestHeal"
		),
		false
	)
	_check(
		"combat.fire_special_power refuses while recharging",
		not world.combat().fire_special_power(
			SageScriptWorld.Scope.PLAYER,
			PLAYER,
			"SpellBookTestHeal",
			SageScriptWorld.target_position(Vector3(at.x, 0.0, at.y))
		)
	)
	_check(
		"combat.fire_special_power refuses TEAM scope",
		not world.combat().fire_special_power(
			SageScriptWorld.Scope.TEAM,
			PLAYER_TEAM_NAME,
			"SpellBookTestHeal",
			SageScriptWorld.target_position(Vector3.ZERO)
		)
	)


# --- 8. Progression upgrades ----------------------------------------------


func _test_progression_upgrades() -> void:
	var sim := _make_sim()
	var world := _make_world(sim)
	_inject_research_contract(sim)
	_check_refused(
		"progression.has_upgrade refuses an unmodeled upgrade",
		world.progression().has_upgrade(SageScriptWorld.Scope.PLAYER, PLAYER, "Upgrade_Bogus")
	)
	_check_refused(
		"progression.has_upgrade refuses TEAM scope",
		world.progression().has_upgrade(
			SageScriptWorld.Scope.TEAM, PLAYER_TEAM_NAME, "Upgrade_TestTech"
		)
	)
	_check_hit(
		"progression.has_upgrade is false before research",
		world.progression().has_upgrade(SageScriptWorld.Scope.PLAYER, PLAYER, "Upgrade_TestTech"),
		false
	)
	var resources_before := sim.resources_for_team(0)
	_check(
		"progression.build_upgrade queues the research",
		world.progression().build_upgrade(PLAYER, "Upgrade_TestTech")
	)
	_check(
		"build_upgrade charged the contract cost",
		sim.resources_for_team(0) == resources_before - 300
	)
	var barracks := _structure_id_of_kind(sim, 0, "barracks")
	_check(
		"build_upgrade queued at the lowest living structure of the kind",
		sim.structure_upgrade_queue_state(barracks).size() == 1
	)
	_check(
		"progression.build_upgrade refuses an unknown upgrade",
		not world.progression().build_upgrade(PLAYER, "Upgrade_Bogus")
	)
	sim.advance(12)
	_check_hit(
		"progression.has_upgrade is true after the research completes",
		world.progression().has_upgrade(SageScriptWorld.Scope.PLAYER, PLAYER, "Upgrade_TestTech"),
		true
	)
	_check_hit(
		"progression.unit_count_with_upgrade is zero before any horde equips",
		world.progression().unit_count_with_upgrade(PLAYER, "Upgrade_TestTech"),
		0
	)
	(sim.entities[1] as Dictionary)["applied_upgrades"] = {"Upgrade_TestTech": 1}
	_check_hit(
		"progression.unit_count_with_upgrade counts an equipped horde",
		world.progression().unit_count_with_upgrade(PLAYER, "Upgrade_TestTech"),
		1
	)
	_check_refused(
		"progression.unit_count_with_upgrade refuses an unmodeled upgrade",
		world.progression().unit_count_with_upgrade(PLAYER, "Upgrade_Bogus")
	)


func _test_progression_hero_rank() -> void:
	var sim := _make_sim()
	var world := _make_world(sim)
	_check_hit(
		"any_hero_reached_rank is false with no heroes fielded",
		world.progression().any_hero_reached_rank(PLAYER, 1, 2),
		false
	)
	# Promote the archer battalion to an authored hero, then drive the real
	# authored XP transition that must record the historical attainment.
	var hero: Dictionary = sim.entities[2]
	hero["category"] = "hero"
	hero["level"] = 1
	hero["experience_xp"] = 0
	sim._unit_experience_rules[String(hero["unit_type"])] = {
		"initial_rank": 1,
		"max_level": 3,
		"levels": [
			{"rank": 1, "required_experience": 0},
			{"rank": 3, "required_experience": 100},
		],
	}
	sim._record_hero_rank_attainment(hero)
	sim._award_experience(hero, 100)
	_check_hit(
		"any_hero_reached_rank sees the authored hero at rank",
		world.progression().any_hero_reached_rank(PLAYER, 1, 3),
		true
	)
	_check_hit(
		"any_hero_reached_rank is false above the hero's rank",
		world.progression().any_hero_reached_rank(PLAYER, 1, 4),
		false
	)
	_check_hit(
		"any_hero_reached_rank honors the requested hero count",
		world.progression().any_hero_reached_rank(PLAYER, 2, 3),
		false
	)
	hero["health"] = 0
	_check_hit(
		"rank attainment remains true after the qualifying hero dies",
		world.progression().any_hero_reached_rank(PLAYER, 1, 3),
		true
	)
	# A second distinct hero reaches rank 3 later. The first is already dead,
	# so a living-only scan cannot satisfy this count.
	var second_hero: Dictionary = sim.entities[1]
	second_hero["category"] = "hero"
	second_hero["level"] = 1
	second_hero["experience_xp"] = 0
	sim._unit_experience_rules[String(second_hero["unit_type"])] = {
		"initial_rank": 1,
		"max_level": 3,
		"levels": [
			{"rank": 1, "required_experience": 0},
			{"rank": 3, "required_experience": 100},
		],
	}
	sim._record_hero_rank_attainment(second_hero)
	sim._award_experience(second_hero, 100)
	_check_hit(
		"heroes reaching rank at different times accumulate historically",
		world.progression().any_hero_reached_rank(PLAYER, 2, 3),
		true
	)
	# Revival uses the same production/experience identity at rank 1. Recording
	# it again must preserve, not lower or double-count, the historical peak.
	sim._record_hero_rank_attainment({
		"team": SimScript.PLAYER_TEAM,
		"unit_type": String(hero["unit_type"]),
		"category": "hero",
		"level": 1,
	})
	_check_hit(
		"a revived rank-1 hero preserves the historical peak without double-counting",
		world.progression().any_hero_reached_rank(PLAYER, 2, 3),
		true
	)
	# An UNAUTHORED hero makes the negative answer unknowable - but a positive
	# answer from an authored hero still stands.
	(sim.entities[3] as Dictionary)["category"] = "hero"
	sim._record_hero_rank_attainment(sim.entities[3])
	_check_hit(
		"an authored hero at rank still answers true alongside an unauthored one",
		world.progression().any_hero_reached_rank(PLAYER, 1, 3),
		true
	)
	_check_refused(
		"the negative refuses while an unauthored hero's rank is unknown",
		world.progression().any_hero_reached_rank(PLAYER, 1, 4)
	)
	_check_refused(
		"a nonpositive hero count refuses instead of inventing vacuous truth",
		world.progression().any_hero_reached_rank(PLAYER, 0, 3)
	)
	var adopted := _make_sim()
	_check("historical hero-rank state restores from a snapshot", adopted.restore(sim.snapshot()))
	var adopted_world := _make_world(adopted)
	_check(
		"snapshot adoption preserves the canonical hero-rank history rows",
		adopted.state_snapshot().get("hero_peak_ranks", []) \
			== sim.state_snapshot().get("hero_peak_ranks", [])
	)
	_check(
		"snapshot adoption preserves the authoritative state hash",
		adopted.state_hash() == sim.state_hash()
	)
	_check_hit(
		"the adopting peer answers from restored historical attainment",
		adopted_world.progression().any_hero_reached_rank(PLAYER, 2, 3),
		true
	)
	adopted.setup({}, {})
	_check_hit(
		"setup clears historical hero-rank attainment for the next match",
		adopted_world.progression().any_hero_reached_rank(PLAYER, 1, 3),
		false
	)


# --- 9. Economy -----------------------------------------------------------


func _test_economy_money() -> void:
	var sim := _make_sim()
	var world := _make_world(sim)
	_check_hit(
		"economy.money answers the team resources",
		world.economy().money(PLAYER),
		10000
	)
	_check("economy.set_money is accepted", world.economy().set_money(PLAYER, 5000))
	_check("economy.set_money wrote the sim", sim.resources_for_team(0) == 5000)
	_check("economy.give_money adds", world.economy().give_money(PLAYER, 250))
	_check("economy.give_money wrote the sim", sim.resources_for_team(0) == 5250)
	_check("economy.give_money subtracts", world.economy().give_money(PLAYER, -250))
	_check("economy.give_money subtraction wrote the sim", sim.resources_for_team(0) == 5000)
	_check_refused(
		"economy.money refuses an unbound player", world.economy().money("Nobody")
	)
	_check(
		"economy.set_money refuses an unbound player",
		not world.economy().set_money("Nobody", 1)
	)
	_check(
		"legacy player_money reads a bound player", world.player_money(PLAYER) == 5000
	)


# --- 10. Meta -------------------------------------------------------------


func _test_meta_outcomes() -> void:
	var sim := _make_sim()
	var world := _make_world(sim)
	_check_hit(
		"meta.player_count counts the rostered teams",
		world.meta().player_count(false),
		2
	)
	_check_hit(
		"meta.multiplayer_outcome defeat is false while the match runs",
		world.meta().multiplayer_outcome(ENEMY, "defeat"),
		false
	)
	_check_hit(
		"meta.multiplayer_outcome allied_victory is false while the match runs",
		world.meta().multiplayer_outcome(PLAYER, "allied_victory"),
		false
	)
	_check_refused(
		"meta.multiplayer_outcome refuses an unknown outcome token",
		world.meta().multiplayer_outcome(PLAYER, "sideways")
	)
	# Raze the enemy fortress and let the sim resolve victory for real.
	var enemy_fortress := _structure_id_of_kind(sim, 1, "fortress")
	(sim.structures[enemy_fortress] as Dictionary)["health"] = 0
	sim.advance(1)
	_check("fixture: the sim resolved a winner", sim.winner == 0)
	_check_hit(
		"meta.multiplayer_outcome defeat is true for the razed player",
		world.meta().multiplayer_outcome(ENEMY, "defeat"),
		true
	)
	_check_hit(
		"meta.multiplayer_outcome allied_defeat is true for the razed player",
		world.meta().multiplayer_outcome(ENEMY, "allied_defeat"),
		true
	)
	_check_hit(
		"meta.multiplayer_outcome allied_victory is true for the winner",
		world.meta().multiplayer_outcome(PLAYER, "allied_victory"),
		true
	)
	_check_hit(
		"meta.multiplayer_outcome defeat is false for the winner",
		world.meta().multiplayer_outcome(PLAYER, "defeat"),
		false
	)


# --- 10. Base building ----------------------------------------------------


func _configure_base_building(sim: RetailSliceSim) -> void:
	## Synthetic base-building surface: one expansion rule and three base
	## flags (positions far from every seeded structure; placement is
	## authored configuration, not a click).
	sim.configure_expansion_rules({
		"synth_pit": {
			"cost": 300,
			"seconds": 5.0,
			"health": 500,
			"pad_kinds": ["corner", "side"],
			"name": "Synth Pit",
			"object_id": "SynthPitType",
		},
	})
	sim.configure_unpackable_bases({
		"BASE_FLAG_1": {"position": Vector2(60.0, 60.0), "cost": 500},
		"BASE_FLAG_2": {"position": Vector2(70.0, -60.0), "cost": 500},
		"BASE_FLAG_3": {"position": Vector2(-70.0, 60.0), "cost": 700},
	})


func _make_base_world(sim: RetailSliceSim) -> RetailSliceScriptWorld:
	_configure_base_building(sim)
	var world := _make_world(sim)
	world.bind_script_player(PLAYER)
	return world


func _test_script_player_binding() -> void:
	var sim := _make_sim()
	var world := _make_world(sim)
	_check(
		"bind_script_player rejects a name not bound as a player",
		not world.bind_script_player("Nobody")
	)
	_check("bind_script_player accepts a bound player", world.bind_script_player(PLAYER))
	_check(
		"bind_script_player is idempotent for the same name",
		world.bind_script_player(PLAYER)
	)
	_check(
		"bind_script_player rejects rebinding to another player",
		not world.bind_script_player(ENEMY)
	)


func _test_ai_base_unpackable() -> void:
	var sim := _make_sim()
	_configure_base_building(sim)
	var world := _make_world(sim)
	_check_refused(
		"ai.base_unpackable refuses '<This Player>' before a script player is bound",
		world.ai().base_unpackable("BASE_FLAG_1", "<This Player>")
	)
	world.bind_script_player(PLAYER)
	_check_hit(
		"ai.base_unpackable is true for a packed flag",
		world.ai().base_unpackable("BASE_FLAG_1", PLAYER),
		true
	)
	_check_hit(
		"ai.base_unpackable resolves '<This Player>' to the script player",
		world.ai().base_unpackable("BASE_FLAG_1", "<This Player>"),
		true
	)
	_check_refused(
		"ai.base_unpackable refuses a name outside the base-flag table",
		world.ai().base_unpackable("GHOST_FLAG", PLAYER)
	)
	_check_refused(
		"ai.base_unpackable refuses an unbound player name",
		world.ai().base_unpackable("BASE_FLAG_1", "Nobody")
	)
	_check(
		"fixture: the flag unpacks paid",
		world.ai().base_unpack("BASE_FLAG_1", false, "AI_EXPANSION_1")
	)
	_check_hit(
		"ai.base_unpackable turns false once the flag is unpacked",
		world.ai().base_unpackable("BASE_FLAG_1", PLAYER),
		false
	)
	_check_hit(
		"ai.base_unpackable is false for EVERY player once claimed (no per-player model)",
		world.ai().base_unpackable("BASE_FLAG_1", ENEMY),
		false
	)


func _test_ai_base_unpack() -> void:
	var sim := _make_sim()
	_configure_base_building(sim)
	var unbound_world := _make_world(sim)
	_check(
		"ai.base_unpack refuses without a script player to act as",
		not unbound_world.ai().base_unpack("BASE_FLAG_1", false, "AI_EXPANSION_1")
	)
	var world := _make_base_world(_make_sim())
	var base_sim := world.sim
	var before := base_sim.resources_for_team(0)
	_check(
		"ai.base_unpack (paid) is accepted",
		world.ai().base_unpack("BASE_FLAG_1", false, "AI_EXPANSION_1")
	)
	_check(
		"the paid unpack charged the authored cost",
		base_sim.resources_for_team(0) == before - 500
	)
	var handle := world.resolve_script_object("AI_EXPANSION_1")
	var base_id := int(handle.get("id", 0))
	_check(
		"the UNIT_REF destination binds to the unpacked structure",
		String(handle.get("kind", "")) == "structure" and base_id != 0
	)
	var row: Dictionary = base_sim.structures.get(base_id, {})
	_check(
		"the unpacked base is a completed fortress of the script player's team",
		String(row.get("structure_kind", "")) == "fortress"
		and int(row.get("team", -1)) == 0
		and float(row.get("construction_progress", 0.0)) >= 1.0
	)
	_check(
		"the unpacked base carries expansion pads",
		not base_sim.expansion_pad_states(base_id).is_empty()
	)
	_check(
		"a second unpack of the same flag refuses",
		not world.ai().base_unpack("BASE_FLAG_1", false, "AI_EXPANSION_2")
	)
	_check(
		"the refused second unpack bound nothing",
		world.resolve_script_object("AI_EXPANSION_2").is_empty()
	)
	# THE FREE/PAID DISTINCTION, under poverty so it cannot pass by accident:
	# with 100 resources a paid unpack refuses and the FREE spelling of the
	# very same flag succeeds without touching the treasury.
	base_sim.team_resources[0] = 100
	_check(
		"a paid unpack refuses on insufficient resources",
		not world.ai().base_unpack("BASE_FLAG_2", false, "AI_BASE")
	)
	_check(
		"the refused paid unpack left the flag packed",
		world.ai().base_unpackable("BASE_FLAG_2", PLAYER).value == true
	)
	_check(
		"the free unpack of the same flag succeeds",
		world.ai().base_unpack("BASE_FLAG_2", true, "AI_BASE")
	)
	_check(
		"the free unpack charged nothing",
		base_sim.resources_for_team(0) == 100
	)
	_check(
		"the free unpack bound its reference too",
		String(world.resolve_script_object("AI_BASE").get("kind", "")) == "structure"
	)
	# Reference validation precedes the sim mutation: binding onto a base-flag
	# name is refused and the whole action must be a no-op.
	var hash_before := base_sim.state_hash()
	_check(
		"an unpack whose reference would shadow a flag name refuses",
		not world.ai().base_unpack("BASE_FLAG_3", false, "BASE_FLAG_1")
	)
	_check(
		"the refused reference binding mutated nothing",
		base_sim.state_hash() == hash_before
	)


func _test_ai_build_base_building() -> void:
	var world := _make_base_world(_make_sim())
	var sim := world.sim
	_check(
		"fixture: the home flag unpacks free as AI_BASE",
		world.ai().base_unpack("BASE_FLAG_1", true, "AI_BASE")
	)
	var base_id := int(world.resolve_script_object("AI_BASE").get("id", 0))
	var before := sim.resources_for_team(0)
	_check(
		"ai.build_base_building builds at the referenced base",
		world.ai().build_base_building("SynthPitType", "AI_BASE", "AI_PIT_1")
	)
	_check(
		"the build charged the expansion rule's cost",
		sim.resources_for_team(0) == before - 300
	)
	var pit_id := int(world.resolve_script_object("AI_PIT_1").get("id", 0))
	var pit: Dictionary = sim.structures.get(pit_id, {})
	_check(
		"the new building is the asked-for kind INSIDE the asked-for base",
		String(pit.get("structure_kind", "")) == "synth_pit"
		and int(pit.get("expansion_of_fortress", 0)) == base_id
	)
	_check(
		"the build occupied a pad of that base",
		int((sim.expansion_pad_states(base_id)[0] as Dictionary).get("expansion_structure_id", 0)) == pit_id
	)
	# The FLAG NAME keeps resolving to the base it became - the shared
	# namespace rule retail leans on when it reads a bound name as a UNIT.
	_check(
		"building at the flag name reaches the same unpacked base",
		world.ai().build_base_building("SynthPitType", "BASE_FLAG_1", "AI_PIT_2")
	)
	_check(
		"ai.build_base_building refuses an unmodeled building type",
		not world.ai().build_base_building("GhostType", "AI_BASE", "AI_PIT_3")
	)
	_check(
		"ai.build_base_building refuses an unknown base name",
		not world.ai().build_base_building("SynthPitType", "GHOST_BASE", "AI_PIT_3")
	)
	_check(
		"ai.build_base_building refuses a still-packed flag as the base",
		not world.ai().build_base_building("SynthPitType", "BASE_FLAG_2", "AI_PIT_3")
	)
	# WRONG OWNER. A second world executes ENEMY's scripts against the SAME
	# sim; the flag name resolves to PLAYER's base, and the sim refuses to
	# build there rather than building at someone else's base.
	var enemy_world := _make_world(sim)
	enemy_world.bind_script_player(ENEMY)
	_check(
		"another script player cannot build at this base",
		not enemy_world.ai().build_base_building("SynthPitType", "BASE_FLAG_1", "AI_PIT_3")
	)
	# Pad exhaustion: the layout carries 6 pads, 2 are used above; 4 more
	# builds fill the base and the next refuses (no-free-pad), which is also
	# what flips can_build_at_base below.
	for index in range(4):
		_check(
			"pad %d/4 of the remaining pads accepts a build" % (index + 1),
			world.ai().build_base_building("SynthPitType", "AI_BASE", "AI_PIT_FILL_%d" % index)
		)
	_check(
		"a build at a full base refuses",
		not world.ai().build_base_building("SynthPitType", "AI_BASE", "AI_PIT_OVERFLOW")
	)
	_check(
		"the refused overflow build bound nothing",
		world.resolve_script_object("AI_PIT_OVERFLOW").is_empty()
	)


func _test_players_can_build_at_base() -> void:
	var world := _make_base_world(_make_sim())
	var sim := world.sim
	_check_refused(
		"players.can_build_at_base refuses an unknown base name",
		world.players().can_build_at_base(PLAYER, "GHOST_BASE", "")
	)
	_check_hit(
		"players.can_build_at_base is false at a still-packed flag (no base there yet)",
		world.players().can_build_at_base(PLAYER, "BASE_FLAG_1", ""),
		false
	)
	_check(
		"fixture: the flag unpacks free as AI_BASE",
		world.ai().base_unpack("BASE_FLAG_1", true, "AI_BASE")
	)
	_check_hit(
		"the empty-type 'anything at all' variant is true with free pads",
		world.players().can_build_at_base(PLAYER, "AI_BASE", ""),
		true
	)
	_check_hit(
		"the typed variant is true for a modeled type with a matching free pad",
		world.players().can_build_at_base(PLAYER, "AI_BASE", "SynthPitType"),
		true
	)
	_check_refused(
		"the typed variant refuses an unmodeled type (false would be a guess)",
		world.players().can_build_at_base(PLAYER, "AI_BASE", "GhostType")
	)
	_check_hit(
		"another player's base answers false (ownership is part of the question)",
		world.players().can_build_at_base(ENEMY, "AI_BASE", ""),
		false
	)
	_check_hit(
		"'<This Player>' resolves through the script player",
		world.players().can_build_at_base("<This Player>", "AI_BASE", ""),
		true
	)
	_check_refused(
		"an unbound player name refuses",
		world.players().can_build_at_base("Nobody", "AI_BASE", "")
	)
	# Fill every pad; the answer must flip to false - this is the check that
	# breaks if the base anchor or the pad accounting is ignored.
	var base_id := int(world.resolve_script_object("AI_BASE").get("id", 0))
	var pad_count := sim.expansion_pad_states(base_id).size()
	for index in range(pad_count):
		_check(
			"fixture: pad %d/%d fills" % [index + 1, pad_count],
			world.ai().build_base_building("SynthPitType", "AI_BASE", "AI_FILL_%d" % index)
		)
	_check_hit(
		"a full base answers false for the empty-type variant",
		world.players().can_build_at_base(PLAYER, "AI_BASE", ""),
		false
	)
	_check_hit(
		"a full base answers false for the typed variant",
		world.players().can_build_at_base(PLAYER, "AI_BASE", "SynthPitType"),
		false
	)
	# A razed base answers false: there is nothing to build at.
	(sim.structures[base_id] as Dictionary)["health"] = 0
	_check_hit(
		"a razed base answers false",
		world.players().can_build_at_base(PLAYER, "AI_BASE", ""),
		false
	)


func _test_reference_namespace() -> void:
	var world := _make_base_world(_make_sim())
	_check(
		"an unknown name resolves to nothing",
		world.resolve_script_object("GHOST").is_empty()
	)
	_check(
		"a flag name resolves as a base flag",
		String(world.resolve_script_object("BASE_FLAG_1").get("kind", "")) == "base_flag"
	)
	_check(
		"fixture: two flags unpack behind distinct references",
		world.ai().base_unpack("BASE_FLAG_1", true, "AI_REF")
		and world.ai().base_unpack("BASE_FLAG_2", true, "AI_OTHER")
	)
	var first_id := int(world.resolve_script_object("AI_REF").get("id", 0))
	var other_id := int(world.resolve_script_object("AI_OTHER").get("id", 0))
	_check("distinct unpacks bind distinct structures", first_id != 0 and first_id != other_id)
	# References are handles resolved AT BIND TIME and mutable by design:
	# re-pointing AI_REF at a third base moves AI_REF alone - the earlier
	# handle bound as AI_OTHER stays aimed where it was resolved.
	_check(
		"fixture: AI_REF re-points to a third unpack",
		world.ai().base_unpack("BASE_FLAG_3", true, "AI_REF")
	)
	var repointed := int(world.resolve_script_object("AI_REF").get("id", 0))
	_check("the re-pointed reference resolves to the new structure", repointed != first_id)
	_check(
		"the untouched reference still resolves to its bind-time structure",
		int(world.resolve_script_object("AI_OTHER").get("id", 0)) == other_id
	)


func _test_base_state_is_hash_inert() -> void:
	# THE STATE-PIN PROPERTY. A match that configures no base flags must
	# contribute NOTHING to the authoritative state - the frozen
	# cross-platform pin (retail_state_pin_runner.gd) rests on exactly this.
	var sim := _make_sim()
	var decoded: Dictionary = bytes_to_var(sim.snapshot())
	_check(
		"an unconfigured sim carries NO unpackable_bases key at all",
		not decoded.has("unpackable_bases")
	)
	var pristine := sim.state_hash()
	sim.configure_unpackable_bases({})
	_check(
		"configuring an EMPTY table stays absent (empty-is-absent is canonical)",
		sim.state_hash() == pristine
	)
	_configure_base_building(sim)
	_check(
		"configuring real flags DOES move the hash (the state is not invisible)",
		sim.state_hash() != pristine
	)
	_check(
		"the configured table serializes",
		(bytes_to_var(sim.snapshot()) as Dictionary).has("unpackable_bases")
	)
	sim.configure_unpackable_bases({})
	_check(
		"clearing the table returns to the pristine hash exactly",
		sim.state_hash() == pristine
	)
	# Snapshot/restore round-trips the configured AND unpacked state.
	var world := _make_base_world(_make_sim())
	world.ai().base_unpack("BASE_FLAG_1", false, "AI_EXPANSION_1")
	var restored: RetailSliceSim = SimScript.new()
	restored._rules = _harness_rules()
	restored.setup({}, {})
	_check("a snapshot with unpacked bases restores", restored.restore(world.sim.snapshot()))
	_check(
		"the restored sim carries the identical authoritative hash",
		restored.state_hash() == world.sim.state_hash()
	)
	_check(
		"the restored sim still knows which team unpacked the flag",
		int(restored.unpackable_base_state("BASE_FLAG_1").get("unpacked_by", -1)) == 0
	)


# --- 11. Object-type identity ---------------------------------------------


func _identity_rules() -> Dictionary:
	## Harness rules enriched with the retail identity a pack records: the
	## soldier rule carries provenance whose source name does NOT slug-match
	## its runtime id (so the provenance path is provably load-bearing), the
	## archer carries none (the runtime-id fallback path is provably
	## load-bearing too), and a structure kind registry maps synthetic retail
	## names onto the seeded kinds.
	var rules := _harness_rules()
	var soldier: Dictionary = (rules["unit_rules"] as Dictionary)[SimScript.SOLDIER_OBJECT_ID]
	soldier["provenance"] = {
		"source_object_id": "SynthSoldierHorde",
		"source_contract": "test-fixture",
	}
	rules["producer_kind_by_source_object"] = {
		"SynthFarmHouse": "farm",
		"SynthFortressCitadel": "fortress",
	}
	return rules


func _make_identity_world() -> RetailSliceScriptWorld:
	var sim: RetailSliceSim = SimScript.new()
	sim._rules = _identity_rules()
	sim.setup({}, {})
	sim.ai_enabled = false
	var world := _make_world(sim)
	world.bind_script_player(PLAYER)
	return world


func _test_players_object_count_of_types() -> void:
	var world := _make_identity_world()
	var sim: RetailSliceSim = world.sim
	var players := world.players()
	# Roster per team: soldier + archer + builder battalions, and the five
	# seeded structure kinds. Every expectation below is an exact census.
	_check_hit(
		"a provenance-recorded retail name counts the soldier row",
		players.object_count_of_types(PLAYER, "SynthSoldierHorde", false),
		1
	)
	_check_hit(
		"retail-name matching folds case (SAGE INI lookups are case-insensitive)",
		players.object_count_of_types(PLAYER, "SYNTHSOLDIERHORDE", false),
		1
	)
	_check_hit(
		"a provenance-free row resolves through the runtime-id slug",
		players.object_count_of_types(PLAYER, "GondorArcher", false),
		1
	)
	_check_hit(
		"the MEMBER object name does not count the horde row (granularity: a row is one retail horde)",
		players.object_count_of_types(PLAYER, "GondorFighter", false),
		0
	)
	_check_hit(
		"a registry-mapped structure name counts the team's farms",
		players.object_count_of_types(PLAYER, "SynthFarmHouse", false),
		1
	)
	_check_hit(
		"the fortress registry name counts the enemy fortress for the enemy",
		players.object_count_of_types(ENEMY, "SynthFortressCitadel", false),
		1
	)
	_check_hit(
		"an unfieldable name counts a TRUE zero over the enumerable census",
		players.object_count_of_types(PLAYER, "MordorLumberMill", false),
		0
	)
	# A declared list counts the union of its members; an unfieldable member
	# contributes its true zero.
	_check(
		"fixture: the offense list builds",
		world.meta().object_list_change("Synth_Offense", "SynthSoldierHorde", true)
		and world.meta().object_list_change("Synth_Offense", "GondorArcher", true)
		and world.meta().object_list_change("Synth_Offense", "GhostType", true)
	)
	_check_hit(
		"a declared list counts the union of its members",
		players.object_count_of_types(PLAYER, "Synth_Offense", false),
		2
	)
	# Aggregate player tokens (the retail AI's authored spellings). Sums, not
	# per-player disjunctions - the counter-writing action proves the shape.
	_check_hit(
		"'<This Player>' resolves through the bound script player",
		players.object_count_of_types(RetailSliceScriptWorld.THIS_PLAYER_TOKEN, "Synth_Offense", false),
		2
	)
	# Token direction is proven with ASYMMETRIC counts (only the enemy fields
	# a knight; only the anchor fields an archer), so a swapped relation
	# cannot pass by symmetry.
	_check_hit(
		"the plural enemies token counts the hostile roster's knight",
		players.object_count_of_types(
			RetailSliceScriptWorld.THIS_PLAYERS_ENEMIES_TOKEN, "GondorKnight", false
		),
		1
	)
	_check_hit(
		"the enemies token excludes the anchor's own roster",
		players.object_count_of_types(
			RetailSliceScriptWorld.THIS_PLAYERS_ENEMIES_TOKEN, "GondorArcher", false
		),
		0
	)
	_check_hit(
		"the allies-incl-self token includes the anchor's archer",
		players.object_count_of_types(
			RetailSliceScriptWorld.THIS_PLAYERS_ALLIES_TOKEN, "GondorArcher", false
		),
		1
	)
	_check_hit(
		"the allies-incl-self token excludes the hostile knight",
		players.object_count_of_types(
			RetailSliceScriptWorld.THIS_PLAYERS_ALLIES_TOKEN, "GondorKnight", false
		),
		0
	)
	# include_dead: a razed/dead row leaves the living census but stays
	# countable while its row persists.
	var enemy_soldier: Dictionary = sim.entities[101]
	enemy_soldier["health"] = 0
	(enemy_soldier["member_health"] as Array)[0] = 0
	_check_hit(
		"a dead row leaves the living census",
		players.object_count_of_types(ENEMY, "SynthSoldierHorde", false),
		0
	)
	_check_hit(
		"include_dead still counts the persisting dead row",
		players.object_count_of_types(ENEMY, "SynthSoldierHorde", true),
		1
	)
	# Refusals: what cannot be resolved is refused, never guessed.
	_check_refused(
		"an empty OBJECT_TYPE_LIST name refuses",
		players.object_count_of_types(PLAYER, "", false)
	)
	_check_refused(
		"an unbound player name refuses",
		players.object_count_of_types("Nobody", "SynthSoldierHorde", false)
	)
	_check_refused(
		"the singular current-enemy token refuses (no current-enemy model)",
		players.object_count_of_types(
			RetailSliceScriptWorld.THIS_PLAYERS_ENEMY_TOKEN, "SynthSoldierHorde", false
		)
	)
	var tokenless := _make_world(_make_sim())
	_check_refused(
		"the enemies token refuses without a bound script player",
		tokenless.players().object_count_of_types(
			RetailSliceScriptWorld.THIS_PLAYERS_ENEMIES_TOKEN, "SynthSoldierHorde", false
		)
	)


func _test_progression_object_veterancy() -> void:
	var world := _make_identity_world()
	var sim: RetailSliceSim = world.sim
	var progression := world.progression()
	_check_hit(
		"a rank-one matching battalion does not satisfy rank >= 2",
		progression.has_object_of_veterancy(
			PLAYER, "SynthSoldierHorde", ParamTypes.COMPARE_GREATER_EQUAL, 2
		),
		false
	)
	(sim.entities[1] as Dictionary)["level"] = 2
	_check_hit(
		"single-type fallback finds a living rank-two battalion",
		progression.has_object_of_veterancy(
			PLAYER, "SynthSoldierHorde", ParamTypes.COMPARE_GREATER_EQUAL, 2
		),
		true
	)
	_check_hit(
		"the sourced comparison is applied to each matching object's rank",
		progression.has_object_of_veterancy(
			PLAYER, "SynthSoldierHorde", ParamTypes.COMPARE_GREATER, 2
		),
		false
	)
	_check_hit(
		"the player predicate does not inspect another owner's matching row",
		progression.has_object_of_veterancy(
			ENEMY, "SynthSoldierHorde", ParamTypes.COMPARE_GREATER_EQUAL, 2
		),
		false
	)
	var farm := _structure_id_of_kind(sim, SimScript.PLAYER_TEAM, "farm")
	(sim.structures[farm] as Dictionary)["level"] = 3
	_check_hit(
		"the predicate includes living structures and exact-rank comparison",
		progression.has_object_of_veterancy(
			PLAYER, "SynthFarmHouse", ParamTypes.COMPARE_EQUAL, 3
		),
		true
	)
	_check(
		"fixture: the veteran object list builds",
		world.meta().object_list_change(
			"Synth_Veterans", "SynthSoldierHorde", true
		)
		and world.meta().object_list_change(
			"Synth_Veterans", "SynthFarmHouse", true
		)
	)
	_check_hit(
		"the observed retail list-like token grammar expands before rank testing",
		progression.has_object_of_veterancy(
			PLAYER, "Synth_Veterans", ParamTypes.COMPARE_GREATER_EQUAL, 3
		),
		true
	)
	(sim.structures[farm] as Dictionary)["health"] = 0
	_check_hit(
		"a dead rank-three object is excluded from the present-tense predicate",
		progression.has_object_of_veterancy(
			PLAYER, "Synth_Veterans", ParamTypes.COMPARE_GREATER_EQUAL, 3
		),
		false
	)
	_check_hit(
		"an unfielded exact type is a truthful false over the enumerable census",
		progression.has_object_of_veterancy(
			PLAYER, "GhostType", ParamTypes.COMPARE_GREATER_EQUAL, 2
		),
		false
	)
	_check_refused(
		"an empty object type refuses",
		progression.has_object_of_veterancy(
			PLAYER, "", ParamTypes.COMPARE_GREATER_EQUAL, 2
		)
	)
	_check_refused(
		"an invalid comparison refuses",
		progression.has_object_of_veterancy(PLAYER, "SynthSoldierHorde", 99, 2)
	)
	_check_refused(
		"an unbound player refuses",
		progression.has_object_of_veterancy(
			"Nobody", "SynthSoldierHorde", ParamTypes.COMPARE_GREATER_EQUAL, 2
		)
	)
	var before := sim.state_hash()
	progression.has_object_of_veterancy(
		PLAYER, "Synth_Veterans", ParamTypes.COMPARE_GREATER_EQUAL, 2
	)
	_check(
		"the veteran-object predicate leaves authoritative state untouched",
		sim.state_hash() == before
	)


func _test_single_player_token_routing() -> void:
	## Regression for the 0dce37e execution census's 84% gap: every
	## single-player facet slot resolved its argument through the literal
	## binding table alone, so the retail-authored "<This Player>" token
	## refused everywhere OUTSIDE the base-building surface - 4,197
	## SKIRMISH_PLAYER_FACTION world-refusals in one 600-tick census run. The
	## fix is ROUTING, not new capability: every single-player slot resolves
	## through _resolve_single_player_team, and the tokens that have no
	## single-team answer keep refusing - each for its own stated reason.
	var sim := _make_sim([
		{"team": 0, "faction": "men", "is_ai": false},
		{"team": 1, "faction": "isengard", "is_ai": true},
	])
	var world := _make_world(sim)
	world.bind_script_player(PLAYER)
	var this_player := RetailSliceScriptWorld.THIS_PLAYER_TOKEN

	# The census's exact shape: players.faction("<This Player>"). The roster
	# carries the PRODUCTION descriptor shape (lowercase pack ids); the world
	# answers the retail side token through the rules' side table.
	_check_hit(
		"players.faction resolves '<This Player>' (the 4,197-refusal shape)",
		world.players().faction(this_player),
		"Men"
	)
	_check_hit(
		"players.command_points_used resolves '<This Player>'",
		world.players().command_points_used(this_player),
		sim.command_points_for_team(0)
	)
	_check_hit(
		"players.building_count resolves '<This Player>'",
		world.players().building_count(this_player, ""),
		sim.living_structure_ids(0).size()
	)
	_check_hit(
		"players.relation_to resolves '<This Player>' in BOTH slots",
		world.players().relation_to(this_player, this_player),
		int((RetailSliceScriptWorld.ParamTypes.ENUMS["RELATION"] as Dictionary)["Friend"])
	)
	_check_hit(
		"players.exists answers true for a resolvable '<This Player>'",
		world.players().exists(this_player),
		true
	)
	_check_hit(
		"economy.money resolves '<This Player>'",
		world.economy().money(this_player),
		10000
	)
	_check(
		"economy.set_money resolves '<This Player>'",
		world.economy().set_money(this_player, 4321)
	)
	_check(
		"the token money write landed on the SCRIPT PLAYER's team, no other",
		sim.resources_for_team(0) == 4321 and sim.resources_for_team(1) == 10000
	)
	_check(
		"legacy set_player_money resolves '<This Player>' (PLAYER_SET_MONEY's path)",
		world.set_player_money(this_player, 1234) and sim.resources_for_team(0) == 1234
	)
	_check(
		"legacy player_money resolves '<This Player>'",
		world.player_money(this_player) == 1234
	)
	_check_hit(
		"progression.science_purchase_points resolves '<This Player>'",
		world.progression().science_purchase_points(this_player),
		sim.power_points(0)
	)
	_check_hit(
		"combat.player_all_destroyed resolves '<This Player>'",
		world.combat().player_all_destroyed(this_player, false),
		false
	)
	_check_hit(
		"meta.multiplayer_outcome resolves '<This Player>'",
		world.meta().multiplayer_outcome(this_player, "defeat"),
		false
	)
	_check(
		"orders PLAYER scope resolves '<This Player>'",
		world.orders().stand_ground(
			SageScriptWorld.Scope.PLAYER, this_player, true
		)
	)

	# The refusals that MUST survive: turning any of these into an answer
	# would be a guess, not a fix.
	_check_refused(
		"players.faction refuses the plural enemies token (a set, not one player)",
		world.players().faction(RetailSliceScriptWorld.THIS_PLAYERS_ENEMIES_TOKEN)
	)
	_check_refused(
		"players.faction refuses the allies-incl-self token (a set, not one player)",
		world.players().faction(RetailSliceScriptWorld.THIS_PLAYERS_ALLIES_TOKEN)
	)
	_check_refused(
		"players.faction refuses '<All Players>' (a set, not one player)",
		world.players().faction(RetailSliceScriptWorld.ALL_PLAYERS_TOKEN)
	)
	_check_refused(
		"players.faction refuses the singular current-enemy token (no current-enemy model)",
		world.players().faction(RetailSliceScriptWorld.THIS_PLAYERS_ENEMY_TOKEN)
	)
	_check(
		"economy.set_money refuses the singular current-enemy token",
		not world.economy().set_money(RetailSliceScriptWorld.THIS_PLAYERS_ENEMY_TOKEN, 1)
	)
	_check_refused(
		"players.faction refuses '<Local Player>' (per-seat, desync-bait under lockstep)",
		world.players().faction(RetailSliceScriptWorld.LOCAL_PLAYER_TOKEN)
	)
	_check_refused(
		"players.exists refuses an unresolvable token instead of answering false",
		_make_world(_make_sim()).players().exists(this_player)
	)
	var tokenless := _make_world(_make_sim())
	_check_refused(
		"players.faction refuses '<This Player>' without a bound script player",
		tokenless.players().faction(this_player)
	)
	# A binding may never shadow token resolution: on a world with NO existing
	# bindings (so nothing else can reject the call), a reserved token spelling
	# is refused as a player name.
	var bare: RetailSliceScriptWorld = _track_world(WorldScript.new(sim))
	_check(
		"bind_player rejects a reserved token spelling as a player name",
		not bare.bind_player(RetailSliceScriptWorld.LOCAL_PLAYER_TOKEN, 1)
	)


func _test_object_type_list_editing() -> void:
	var world := _make_identity_world()
	var sim: RetailSliceSim = world.sim
	var players := world.players()
	_check(
		"fixture: a two-member list builds",
		world.meta().object_list_change("Synth_Edit", "SynthSoldierHorde", true)
		and world.meta().object_list_change("Synth_Edit", "GondorArcher", true)
	)
	_check(
		"a duplicate add is a retail set no-op that still succeeds",
		world.meta().object_list_change("Synth_Edit", "SynthSoldierHorde", true)
		and (sim.script_object_type_lists["Synth_Edit"] as Array).size() == 2
	)
	_check(
		"members are stored sorted (canonical set serialization)",
		sim.script_object_type_lists["Synth_Edit"] == ["GondorArcher", "SynthSoldierHorde"]
	)
	_check(
		"removing a member narrows the census",
		world.meta().object_list_change("Synth_Edit", "GondorArcher", false)
	)
	_check_hit(
		"the narrowed list counts only its remaining member",
		players.object_count_of_types(PLAYER, "Synth_Edit", false),
		1
	)
	_check(
		"an absent-member remove is a retail no-op that still succeeds",
		world.meta().object_list_change("Synth_Edit", "NeverAdded", false)
	)
	_check(
		"removing the last member erases the list key itself",
		world.meta().object_list_change("Synth_Edit", "SynthSoldierHorde", false)
		and not sim.has_object_type_list("Synth_Edit")
	)
	_check_hit(
		"an emptied list's name reads as a single type again (the retail fallback)",
		players.object_count_of_types(PLAYER, "Synth_Edit", false),
		0
	)
	_check(
		"an empty list name refuses",
		not world.meta().object_list_change("", "SynthSoldierHorde", true)
	)
	_check(
		"an empty object type refuses",
		not world.meta().object_list_change("Synth_Edit", "", true)
	)


func _test_units_has_command_points_to_build() -> void:
	var sim := _make_sim()
	var world := _make_world(sim)
	world.bind_script_player(PLAYER)
	var units := world.units()
	# The soldier horde costs 60 CP (the default manifest rule). The seeded
	# roster commits 120 (soldier + archer; the builder is 0), and the cap is
	# the sim's command_point_cap - so the expected verdict is derivable
	# exactly from the queue admission numbers.
	var headroom: int = (
		sim.command_point_cap
		- sim.command_points_for_team(SimScript.PLAYER_TEAM)
		- world._queued_command_points(SimScript.PLAYER_TEAM)
	)
	var cost: int = sim.unit_command_point_cost(SimScript.SOLDIER_HORDE_ID)
	_check("fixture: the soldier rule carries a positive CP cost", cost > 0)
	_check_hit(
		"the retail spelling answers exactly the queue admission verdict",
		units.has_command_points_to_build(PLAYER, "GondorFighterHorde"),
		headroom >= cost
	)
	_check_hit(
		"'<This Player>' resolves through the script player",
		units.has_command_points_to_build(
			RetailSliceScriptWorld.THIS_PLAYER_TOKEN, "GondorFighterHorde"
		),
		headroom >= cost
	)
	# Exhaust the headroom and the verdict flips with the same numbers.
	sim.team_command_points[SimScript.PLAYER_TEAM] = sim.command_point_cap
	_check_hit(
		"a capped-out player has no room to build",
		units.has_command_points_to_build(PLAYER, "GondorFighterHorde"),
		false
	)
	_check_refused(
		"a type without a production rule refuses (its cost is unknowable)",
		units.has_command_points_to_build(PLAYER, "MordorLumberMill")
	)
	_check_refused(
		"an empty object type refuses",
		units.has_command_points_to_build(PLAYER, "")
	)
	_check_refused(
		"an unbound player refuses",
		units.has_command_points_to_build("Nobody", "GondorFighterHorde")
	)


func _test_orders_move_to_nearest_type() -> void:
	var world := _make_identity_world()
	var sim: RetailSliceSim = world.sim
	_configure_base_building(sim)
	var orders := world.orders()
	var own_farm := _structure_id_of_kind(sim, SimScript.PLAYER_TEAM, "farm")
	var enemy_farm := _structure_id_of_kind(sim, SimScript.ENEMY_TEAM, "farm")
	_check("fixture: both farms exist", own_farm != 0 and enemy_farm != 0)
	var origin := Vector2((sim.entities[1] as Dictionary)["position"])
	# The sim-level pick: any-owner prefers the mover's own (closer) farm;
	# the owner filter redirects to the enemy's.
	var any_pick: Dictionary = sim.nearest_object_of_types(origin, ["SynthFarmHouse"], [])
	_check(
		"the any-owner nearest farm is the mover's own (closer) farm",
		bool(any_pick.get("found", false)) and int(any_pick.get("id", 0)) == own_farm
	)
	var enemy_pick: Dictionary = sim.nearest_object_of_types(
		origin, ["SynthFarmHouse"], [SimScript.ENEMY_TEAM]
	)
	_check(
		"the owner filter redirects the pick to the enemy farm",
		bool(enemy_pick.get("found", false)) and int(enemy_pick.get("id", 0)) == enemy_farm
	)
	# A marker type the sim cannot field refuses - the modeling gap must stay
	# visible, never become a silent stand-down.
	_check(
		"a marker type the sim cannot field refuses",
		not orders.move_to(
			SageScriptWorld.Scope.TEAM,
			PLAYER_TEAM_NAME,
			SageScriptWorld.target_nearest_type("Center1", "")
		)
	)
	# A fieldable type with no living instance is the retail no-op: success,
	# and NOTHING moved (the hash is untouched because no order was issued).
	var before := sim.state_hash()
	_check(
		"a fieldable type with no living instance is a truthful retail no-op",
		orders.move_to(
			SageScriptWorld.Scope.TEAM,
			PLAYER_TEAM_NAME,
			SageScriptWorld.target_nearest_type("SynthPitType", "")
		)
	)
	_check("the no-op issued no order at all", sim.state_hash() == before)
	# The owned move through the world: the enemies token resolves and the
	# roster is ordered at the enemy farm's position.
	_check(
		"the enemies-token owned move is served",
		orders.move_to(
			SageScriptWorld.Scope.TEAM,
			PLAYER_TEAM_NAME,
			SageScriptWorld.target_nearest_type(
				"SynthFarmHouse", RetailSliceScriptWorld.THIS_PLAYERS_ENEMIES_TOKEN
			)
		)
	)
	_check(
		"the moving roster's destination is the enemy farm",
		Vector2((sim.entities[1] as Dictionary)["destination"])
		== Vector2((sim.structures[enemy_farm] as Dictionary)["position"])
	)
	# A list name resolves before the search, exactly like the census path.
	world.meta().object_list_change("Synth_Move_List", "SynthFarmHouse", true)
	_check(
		"a declared list name resolves as the move's type argument",
		orders.move_to(
			SageScriptWorld.Scope.TEAM,
			PLAYER_TEAM_NAME,
			SageScriptWorld.target_nearest_type("Synth_Move_List", "")
		)
	)
	_check(
		"the list-driven move re-aimed the roster at the nearer own farm",
		Vector2((sim.entities[1] as Dictionary)["destination"])
		== Vector2((sim.structures[own_farm] as Dictionary)["position"])
	)
	# An owner that cannot resolve refuses (the singular token has no model).
	_check(
		"an unresolvable owner refuses the owned move",
		not orders.move_to(
			SageScriptWorld.Scope.TEAM,
			PLAYER_TEAM_NAME,
			SageScriptWorld.target_nearest_type(
				"SynthFarmHouse", RetailSliceScriptWorld.THIS_PLAYERS_ENEMY_TOKEN
			)
		)
	)


func _test_nearest_type_exact_tie_break() -> void:
	## The total order under EXACT ties: equal squared distance resolves to
	## battalions before structures, then to the lowest id - written with
	## coordinates whose squared distances are exactly representable, so the
	## tie is a true float equality, not an approximation.
	var world := _make_identity_world()
	var sim: RetailSliceSim = world.sim
	var origin := Vector2.ZERO
	# Two soldier battalions (ids 1 < 101) at mirrored positions: d^2 = 25.
	(sim.entities[1] as Dictionary)["position"] = Vector2(-3.0, 4.0)
	(sim.entities[101] as Dictionary)["position"] = Vector2(3.0, 4.0)
	var pick: Dictionary = sim.nearest_object_of_types(origin, ["SynthSoldierHorde"], [])
	_check(
		"an exact distance tie between battalions resolves to the LOWEST id",
		int(pick.get("id", 0)) == 1 and String(pick.get("kind", "")) == "battalion"
	)
	# A structure at the same exact squared distance loses to a battalion.
	var own_farm := _structure_id_of_kind(sim, SimScript.PLAYER_TEAM, "farm")
	(sim.structures[own_farm] as Dictionary)["position"] = Vector2(0.0, 5.0)
	var mixed: Dictionary = sim.nearest_object_of_types(origin, ["SynthSoldierHorde", "SynthFarmHouse"], [])
	_check(
		"an exact battalion/structure tie resolves to the battalion",
		int(mixed.get("id", 0)) == 1 and String(mixed.get("kind", "")) == "battalion"
	)
	# Remove the battalions from contention: the structure tie is real too.
	(sim.entities[1] as Dictionary)["position"] = Vector2(80.0, 80.0)
	(sim.entities[101] as Dictionary)["position"] = Vector2(-80.0, 80.0)
	var enemy_farm := _structure_id_of_kind(sim, SimScript.ENEMY_TEAM, "farm")
	(sim.structures[enemy_farm] as Dictionary)["position"] = Vector2(5.0, 0.0)
	var farms: Dictionary = sim.nearest_object_of_types(origin, ["SynthFarmHouse"], [])
	_check(
		"an exact structure/structure tie resolves to the LOWEST structure id",
		int(farms.get("id", 0)) == mini(own_farm, enemy_farm)
	)


# --- 12. Read-only sweep --------------------------------------------------


func _test_queries_are_read_only() -> void:
	var sim := _make_sim()
	var world := _make_base_world(sim)
	_inject_research_contract(sim)
	sim.configure_spellbook_runtime(_spellbook_document())
	world.ai().base_unpack("BASE_FLAG_1", true, "AI_BASE")
	sim.advance(5)
	var before := sim.state_hash()
	for _round in range(3):
		world.world_frame()
		world.players().exists(PLAYER)
		world.players().exists("Nobody")
		world.players().faction(PLAYER)
		world.players().command_points_available(PLAYER)
		world.players().command_points_total(PLAYER)
		world.players().command_points_used(PLAYER)
		world.players().building_count(PLAYER, "")
		world.players().building_count(PLAYER, "farm")
		world.players().building_count(PLAYER, "inn")
		world.players().relation_to(PLAYER, ENEMY)
		world.teams().exists(PLAYER_TEAM_NAME)
		world.teams().unit_count(PLAYER_TEAM_NAME)
		world.teams().owner(PLAYER_TEAM_NAME)
		world.combat().special_power_ready(
			SageScriptWorld.Scope.PLAYER, PLAYER, "SpellBookTestHeal"
		)
		world.combat().player_all_destroyed(ENEMY, false)
		world.progression().has_upgrade(
			SageScriptWorld.Scope.PLAYER, PLAYER, "Upgrade_TestTech"
		)
		world.progression().unit_count_with_upgrade(PLAYER, "Upgrade_TestTech")
		world.progression().has_science(PLAYER, "SCIENCE_TestHeal")
		world.progression().can_purchase_science(PLAYER, "SCIENCE_TestHeal")
		world.progression().science_purchase_points(PLAYER)
		world.progression().has_object_of_veterancy(
			PLAYER, "GondorArcher", ParamTypes.COMPARE_GREATER_EQUAL, 2
		)
		world.progression().any_hero_reached_rank(PLAYER, 1, 2)
		world.economy().money(PLAYER)
		world.meta().player_count(true)
		world.meta().multiplayer_outcome(PLAYER, "defeat")
		world.meta().multiplayer_outcome(PLAYER, "allied_victory")
		world.meta().multiplayer_outcome(PLAYER, "allied_defeat")
		world.ai().base_unpackable("BASE_FLAG_2", PLAYER)
		world.ai().base_unpackable("BASE_FLAG_1", "<This Player>")
		world.ai().base_unpackable("GHOST_FLAG", PLAYER)
		world.players().can_build_at_base(PLAYER, "AI_BASE", "")
		world.players().can_build_at_base(PLAYER, "AI_BASE", "SynthPitType")
		world.players().can_build_at_base(PLAYER, "AI_BASE", "GhostType")
		world.players().can_build_at_base(PLAYER, "BASE_FLAG_2", "")
		world.players().object_count_of_types(PLAYER, "GondorArcher", false)
		world.players().object_count_of_types(PLAYER, "GondorArcher", true)
		world.players().object_count_of_types(PLAYER, "GhostType", false)
		world.players().object_count_of_types(
			RetailSliceScriptWorld.THIS_PLAYERS_ENEMIES_TOKEN, "GondorArcher", false
		)
		world.players().object_count_of_types(
			RetailSliceScriptWorld.THIS_PLAYERS_ALLIES_TOKEN, "GondorArcher", false
		)
		world.units().has_command_points_to_build(PLAYER, "GondorFighterHorde")
		world.units().has_command_points_to_build(PLAYER, "GhostType")
		world.resolve_script_object("AI_BASE")
	_check(
		"every implemented query leaves the authoritative state hash untouched",
		sim.state_hash() == before
	)


# --- 13. Twin determinism -------------------------------------------------


func _twin_fixture() -> Array:
	## One deterministic build of the sim+world pair, driven ONLY through
	## world-facing commands after setup, so two invocations must agree
	## bit-for-bit. Includes the base-building surface: an unpack chain, a
	## base-anchored build and the reference bindings they leave behind.
	var sim := _make_sim()
	var world := _make_base_world(sim)
	_inject_research_contract(sim)
	sim.configure_spellbook_runtime(_spellbook_document())
	_position_enemy_spread(sim, Vector2(10.0, 0.0), Vector2(-10.0, 0.0))
	world.economy().set_money(PLAYER, 8000)
	world.progression().purchase_science(PLAYER, "SCIENCE_TestHeal")
	world.progression().build_upgrade(PLAYER, "Upgrade_TestTech")
	world.ai().base_unpack("BASE_FLAG_1", true, "AI_BASE")
	world.ai().base_unpack("BASE_FLAG_2", false, "AI_EXPANSION_1")
	world.ai().build_base_building("SynthPitType", "AI_BASE", "AI_PIT_1")
	# The object-type surface: a script-built list and a nearest-of-type move
	# aimed through it at the pit the build above just minted.
	world.meta().object_list_change("Synth_Targets", "SynthPitType", true)
	world.meta().object_list_change("Synth_Targets", "GhostType", true)
	world.orders().move_to(
		SageScriptWorld.Scope.TEAM,
		PLAYER_TEAM_NAME,
		SageScriptWorld.target_nearest_type("Synth_Targets", "")
	)
	world.orders().attack(
		SageScriptWorld.Scope.TEAM,
		PLAYER_TEAM_NAME,
		SageScriptWorld.target_team(ENEMY_TEAM_NAME)
	)
	world.orders().stand_ground(SageScriptWorld.Scope.PLAYER, ENEMY, true)
	sim.advance(30)
	return [sim, world]


func _query_battery(world: RetailSliceScriptWorld) -> Array:
	return [
		world.world_frame(),
		world.players().command_points_available(PLAYER).value,
		world.players().command_points_used(PLAYER).value,
		world.players().building_count(PLAYER, "").value,
		world.players().relation_to(PLAYER, ENEMY).value,
		world.teams().unit_count(PLAYER_TEAM_NAME).value,
		world.teams().unit_count(ENEMY_TEAM_NAME).value,
		world.teams().owner(ENEMY_TEAM_NAME).value,
		world.combat().special_power_ready(
			SageScriptWorld.Scope.PLAYER, PLAYER, "SpellBookTestHeal"
		).value,
		world.combat().player_all_destroyed(ENEMY, false).value,
		world.progression().has_upgrade(
			SageScriptWorld.Scope.PLAYER, PLAYER, "Upgrade_TestTech"
		).value,
		world.progression().has_science(PLAYER, "SCIENCE_TestHeal").value,
		world.progression().science_purchase_points(PLAYER).value,
		world.progression().has_object_of_veterancy(
			PLAYER, "Synth_Targets", ParamTypes.COMPARE_GREATER_EQUAL, 1
		).value,
		world.economy().money(PLAYER).value,
		world.meta().player_count(false).value,
		world.meta().multiplayer_outcome(PLAYER, "defeat").value,
		world.ai().base_unpackable("BASE_FLAG_1", PLAYER).value,
		world.ai().base_unpackable("BASE_FLAG_3", "<This Player>").value,
		world.players().can_build_at_base(PLAYER, "AI_BASE", "").value,
		world.players().can_build_at_base(PLAYER, "AI_EXPANSION_1", "SynthPitType").value,
		world.resolve_script_object("AI_BASE"),
		world.resolve_script_object("AI_EXPANSION_1"),
		world.resolve_script_object("AI_PIT_1"),
		world.players().object_count_of_types(PLAYER, "Synth_Targets", false).value,
		world.players().object_count_of_types(PLAYER, "Synth_Targets", true).value,
		world.players().object_count_of_types(
			RetailSliceScriptWorld.THIS_PLAYERS_ENEMIES_TOKEN, "Synth_Targets", false
		).value,
		world.units().has_command_points_to_build(PLAYER, "GondorFighterHorde").value,
		world.sim.object_type_list_names(),
		world.sim.resolve_object_type_names("Synth_Targets"),
		world.sim.nearest_object_of_types(Vector2.ZERO, ["SynthPitType"], []),
	]


func _test_twin_worlds_agree() -> void:
	var first := _twin_fixture()
	var second := _twin_fixture()
	var sim_a: RetailSliceSim = first[0]
	var sim_b: RetailSliceSim = second[0]
	_check(
		"twin sims driven through twin worlds share one state hash",
		sim_a.state_hash() == sim_b.state_hash()
	)
	_check(
		"twin worlds picked the identical tie-broken attack target",
		int((sim_a.entities[1] as Dictionary)["target_id"])
		== int((sim_b.entities[1] as Dictionary)["target_id"])
	)
	var battery_a := _query_battery(first[1])
	var battery_b := _query_battery(second[1])
	_check("twin worlds answer the full query battery identically", battery_a == battery_b)


# --- Object-name registry: the reads the shared namespace can answer -------
#
# RETAIL SEMANTICS ARE SOURCED, NOT GUESSED (C&C Generals/Zero Hour GPL
# ScriptEngine + ScriptConditions, the codebase BFME's ScriptEngine derives
# from; the BFME binary reversal under
# workspace/scratch/Open-BFME-research/reverse/whale_scriptengine confirms the
# identical template shape for every member):
#   evaluateNamedUnitExists      theUnit && !theUnit->isEffectivelyDead()
#   evaluateNamedCreated         getUnitNamed(...) != NULL  (dead flag NOT read)
#   evaluateNamedUnitDestroyed   theUnit ? isEffectivelyDead() : didUnitExist()
#   evaluateNamedUnitDying       theUnit && isEffectivelyDead()
#   getUnitNamed                 NULL for an unknown name; conditions then FALSE
# The last one is the line this adapter deliberately does NOT copy: retail's
# false comes off a COMPLETE name table, this namespace is a subset of the
# names a map may author, so an unknown name refuses instead. That asymmetry
# is asserted below rather than left to a comment.


func _test_named_object_reads() -> void:
	var sim := _make_sim()
	var world := _make_base_world(sim)
	# A PACKED flag: retail's flag object stands on the map, so it exists, was
	# "created", is not destroyed and is not dying.
	_check_hit("units.exists is true for a packed base flag", world.units().exists("BASE_FLAG_1"), true)
	_check_hit("units.was_created is true for a packed base flag", world.units().was_created("BASE_FLAG_1"), true)
	_check_hit("units.was_destroyed is false for a packed base flag", world.units().was_destroyed("BASE_FLAG_1"), false)
	_check_hit("units.is_dying is false for a packed base flag", world.units().is_dying("BASE_FLAG_1"), false)
	_check_hit(
		"units.position answers the packed flag's authored position",
		world.units().position("BASE_FLAG_1"),
		Vector3(60.0, 0.0, 60.0)
	)
	_check_hit(
		"units.is_owned_by is false for a packed neutral flag and a rostered player",
		world.units().is_owned_by("BASE_FLAG_1", PLAYER),
		false
	)
	_check_hit(
		"units.is_owned_by resolves <This Player> while the flag remains neutral",
		world.units().is_owned_by("BASE_FLAG_1", "<This Player>"),
		false
	)
	# Unpack it: now it resolves to a live fortress the script player owns.
	_check("fixture: BASE_FLAG_1 unpacks behind AI_BASE", world.ai().base_unpack("BASE_FLAG_1", true, "AI_BASE"))
	_check_hit("units.owner answers the unpacking player", world.units().owner("BASE_FLAG_1"), PLAYER)
	_check_hit("units.owner answers identically through the bound reference", world.units().owner("AI_BASE"), PLAYER)
	_check_hit(
		"units.is_owned_by answers true for the unpacking player",
		world.units().is_owned_by("BASE_FLAG_1", PLAYER),
		true
	)
	_check_hit(
		"units.is_owned_by resolves <This Player> world-side",
		world.units().is_owned_by("AI_BASE", "<This Player>"),
		true
	)
	_check_hit(
		"units.is_owned_by answers false for the enemy",
		world.units().is_owned_by("AI_BASE", ENEMY),
		false
	)
	_check_hit("units.health_percent is 100 for an undamaged base", world.units().health_percent("AI_BASE"), 100.0)
	_check_hit("units.exists is true for the unpacked base", world.units().exists("AI_BASE"), true)
	# Kill it. The sim keeps a destroyed structure row at health 0 - retail's
	# "pointer still non-NULL, object effectively dead" state.
	var structure_id := int(world.resolve_script_object("AI_BASE").get("id", 0))
	_check("fixture: the unpacked base has a structure id", structure_id != 0)
	(sim.structures[structure_id] as Dictionary)["health"] = 0
	_check_hit("units.exists is false once the object is effectively dead", world.units().exists("AI_BASE"), false)
	_check_hit("units.is_dying is true once the object is effectively dead", world.units().is_dying("AI_BASE"), true)
	_check_hit("units.was_destroyed is true once the object is effectively dead", world.units().was_destroyed("AI_BASE"), true)
	_check_hit(
		"units.was_created stays true for a dead object (retail does NOT read the dead flag)",
		world.units().was_created("AI_BASE"),
		true
	)
	_check_hit("units.health_percent reports 0 for the dead object", world.units().health_percent("AI_BASE"), 0.0)
	_check_hit(
		"units.is_owned_by retains controlling-player ownership while the dead object row exists",
		world.units().is_owned_by("AI_BASE", PLAYER),
		true
	)
	# Remove the row entirely - retail's nulled name-table pointer.
	sim.structures.erase(structure_id)
	_check_hit("units.was_created is false once the object is gone", world.units().was_created("AI_BASE"), false)
	_check_hit("units.was_destroyed stays true once the object is gone", world.units().was_destroyed("AI_BASE"), true)
	_check_refused("units.position refuses for an object that is gone", world.units().position("AI_BASE"))
	_check_refused("units.owner refuses for an object that is gone", world.units().owner("AI_BASE"))
	_check_hit(
		"units.is_owned_by remains false once the known object is gone",
		world.units().is_owned_by("AI_BASE", PLAYER),
		false
	)
	_check_refused("units.health_percent refuses for an object that is gone", world.units().health_percent("AI_BASE"))


func _test_named_object_refusals() -> void:
	var world := _make_base_world(_make_sim())
	# THE ASYMMETRY WITH RETAIL, ASSERTED. Retail answers false here off a
	# complete name table; this namespace is a subset, so false would be a
	# confident wrong answer and the method refuses instead.
	_check_refused("units.exists refuses a name outside the namespace", world.units().exists("GHOST"))
	_check_refused("units.was_created refuses a name outside the namespace", world.units().was_created("GHOST"))
	_check_refused("units.was_destroyed refuses a name outside the namespace", world.units().was_destroyed("GHOST"))
	_check_refused("units.is_dying refuses a name outside the namespace", world.units().is_dying("GHOST"))
	_check_refused("units.position refuses a name outside the namespace", world.units().position("GHOST"))
	_check_refused("units.owner refuses a name outside the namespace", world.units().owner("GHOST"))
	_check_refused(
		"units.is_owned_by refuses a name outside the namespace",
		world.units().is_owned_by("GHOST", PLAYER)
	)
	_check_refused(
		"units.is_owned_by refuses an unbound player",
		world.units().is_owned_by("BASE_FLAG_1", "Nobody")
	)
	_check_refused("units.health_percent refuses a name outside the namespace", world.units().health_percent("GHOST"))
	# Case sensitivity: retail's name table compares with strcmp, not stricmp.
	_check_refused("the namespace is case-sensitive, like retail's strcmp", world.units().exists("base_flag_1"))
	# A packed flag has no owner this simulation can name and no health of its
	# own - both refuse rather than answering about a different object.
	_check_refused("units.owner refuses a packed base flag (no neutral player name)", world.units().owner("BASE_FLAG_1"))
	_check_refused("units.health_percent refuses a packed base flag (the health is the fortress's)", world.units().health_percent("BASE_FLAG_1"))
	# The four members that stay blocked, each for a sourced reason.
	_check_refused("units.is_totally_dead still refuses (no object-removal edge)", world.units().is_totally_dead("BASE_FLAG_1"))
	_check_refused("units.stance still refuses (the namespace holds no battalion)", world.units().stance("BASE_FLAG_1"))
	_check_refused("orders.in_alt_formation still refuses (the namespace holds no battalion)", world.orders().in_alt_formation("BASE_FLAG_1"))
	_check("units.stop still refuses (the namespace holds no battalion)", not world.units().stop("BASE_FLAG_1", true))


func _test_units_set_reference() -> void:
	var sim := _make_sim()
	var world := _make_base_world(sim)
	# SET_UNIT_REFERENCE's real retail shape: aim a reference at a base flag
	# nobody has unpacked (32 of its 40 authored call sites).
	_check(
		"set_reference binds a reference to a PACKED base flag",
		world.units().set_reference("AI_CURRENT_CONSTRUCTION_SITE", "BASE_FLAG_1")
	)
	_check(
		"the reference resolves exactly as the flag's own name does",
		world.resolve_script_object("AI_CURRENT_CONSTRUCTION_SITE")
		== world.resolve_script_object("BASE_FLAG_1")
	)
	# SET_UNIT_REFERENCE_TO_REFERENCE: copies the SOURCE's CURRENT binding.
	_check(
		"set_reference copies a reference to another reference",
		world.units().set_reference("AI_COPY", "AI_CURRENT_CONSTRUCTION_SITE")
	)
	_check(
		"the copy took the source's bind-time value",
		world.resolve_script_object("AI_COPY") == world.resolve_script_object("BASE_FLAG_1")
	)
	_check(
		"fixture: the source re-points to a different flag",
		world.units().set_reference("AI_CURRENT_CONSTRUCTION_SITE", "BASE_FLAG_2")
	)
	_check(
		"the re-pointed source moved",
		world.resolve_script_object("AI_CURRENT_CONSTRUCTION_SITE")
		== world.resolve_script_object("BASE_FLAG_2")
	)
	_check(
		"the copy did NOT follow the source (a handle was stored, not the name)",
		world.resolve_script_object("AI_COPY") == world.resolve_script_object("BASE_FLAG_1")
	)
	# Structure-valued bind, and the refusals.
	_check("fixture: BASE_FLAG_3 unpacks behind AI_BASE", world.ai().base_unpack("BASE_FLAG_3", true, "AI_BASE"))
	_check("set_reference binds a reference to a structure-valued name", world.units().set_reference("AI_SITE", "AI_BASE"))
	_check(
		"the structure-valued copy resolves to the same structure",
		int(world.resolve_script_object("AI_SITE").get("id", 0))
		== int(world.resolve_script_object("AI_BASE").get("id", 0))
	)
	_check("set_reference refuses a source outside the namespace", not world.units().set_reference("AI_X", "GHOST"))
	_check("set_reference refuses an empty destination", not world.units().set_reference("", "BASE_FLAG_1"))
	_check(
		"set_reference refuses a destination that would shadow a base flag",
		not world.units().set_reference("BASE_FLAG_2", "BASE_FLAG_1")
	)
	var unbound := _make_base_world(_make_sim())
	unbound._script_player = ""
	_check(
		"set_reference refuses with no script player bound (no namespace to bind in)",
		not unbound.units().set_reference("AI_X", "BASE_FLAG_1")
	)
