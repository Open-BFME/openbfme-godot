extends SceneTree

## Proof runner for WP22-sciences (wp22_sciences.gd) - the four science /
## spellpoint script names the retail maps and libraries call around the
## spellbook: two ACTIONS (PLAYER_SCIENCE_AVAILABILITY, 12 call sites;
## PLAYER_SET_MAX_SPELLPOINTS, 1) and two CONDITIONS (PLAYER_ACQUIRED_SCIENCE,
## 9; PLAYER_HAS_REACHED_LEVEL_CAP, 1). The kinds come from the sourced tables,
## not from the names: PLAYER_SCIENCE_AVAILABILITY sounds like a question and is
## a command.
##
## Covers, in order:
##   1. registration    - both actions and both conditions registered under the
##                        RIGHT kind, counted as coverage, no registry errors
##   2. per action      - the exact world method is reached with the arguments
##                        in signature order; a world refusal is WORLD_REFUSED
##                        with a structured gap, never OK-and-nothing-happened
##   3. per condition   - TRUE against stub-seeded state, FALSE against
##                        stub-seeded state, and the third answer: a refusal is
##                        a structured WORLD_REFUSED gap, never a plain false
##                        ("the player does not own the science" and "the world
##                        cannot say" are different answers)
##   4. positional args - PLAYER_SCIENCE_AVAILABILITY carries three text slots
##                        side by side and PLAYER_SET_MAX_SPELLPOINTS puts the
##                        cap in the integer payload next to a text player;
##                        every fixture argument carries DECOY payloads in the
##                        other fields so a wrong-field or wrong-slot read is
##                        visibly wrong, and world calls are recorded verbatim
##   5. purity          - conditions write ctx.result only, actions mutate the
##                        world only: the env is untouched either way
##   6. arity           - one wrong-arity case per name, rejected before the
##                        handler runs, world untouched
##
## Every fixture value is SYNTHETIC. No retail payload text is reproduced.
##
## Invocation:
##   <godot> --headless --path game \
##     --script res://tests/handlers_wp22_sciences_runner.gd

const Registry := preload("res://src/script/handlers/_registry.gd")
const Dispatch := preload("res://src/script/script_dispatch.gd")
const CoreHandlers := preload("res://src/script/script_handlers_core.gd")
const Env := preload("res://src/script/script_env.gd")
const GapLog := preload("res://src/script/script_gaps.gd")
const ParamTypes := preload("res://src/script/script_param_types.gd")
const Watchdog := preload("res://tests/runner_watchdog.gd")

## The names this package serves, BY KIND. Written out rather than derived from
## the module, so a member quietly disappearing from register() - or drifting to
## the wrong kind - fails here.
const SERVED_ACTIONS := [
	"PLAYER_SCIENCE_AVAILABILITY",
	"PLAYER_SET_MAX_SPELLPOINTS",
]
const SERVED_CONDITIONS := [
	"PLAYER_ACQUIRED_SCIENCE",
	"PLAYER_HAS_REACHED_LEVEL_CAP",
]

## Synthetic fixture tokens. Shaped like retail call sites, invented in content.
const PLAYER := "Player_1"
const SCIENCE := "SCIENCE_TestBeacon"
const OTHER_SCIENCE := "SCIENCE_TestSecondBeacon"
const AVAILABILITY := "SyntheticHidden"

## LIVENESS GUARD. A GDScript RUNTIME error aborts the enclosing function on
## the spot without propagating, so every later _check() in that function
## silently never runs. A healthy run makes exactly this many checks; any
## other number means something aborted (or an assertion was added without
## updating this) and the result is not to be trusted.
const EXPECTED_CHECKS := 35

var passed := 0
var failed := 0
var _watchdog := Watchdog.new()

## Every world this run created, so facet<->world reference cycles can be
## broken before quit (see the Facet class comment in script_world.gd: a
## dropped world with live facets is a RefCounted cycle that leaks and can AV
## on Windows teardown).
var _worlds: Array = []


func _initialize() -> void:
	_watchdog.start(self, "HANDLERS_WP22", 0, 0, true)
	_watchdog.set_result_provider(func() -> Vector2i: return Vector2i(passed, failed))
	call_deferred("_run")


func _run() -> void:
	_test_registration_covers_the_package()
	_test_science_availability_reaches_the_world()
	_test_science_availability_refusal_is_honest()
	_test_set_max_spellpoints_reaches_the_world()
	_test_set_max_spellpoints_refusal_is_honest()
	_test_acquired_science_answers()
	_test_acquired_science_refusal_is_not_false()
	_test_level_cap_answers()
	_test_level_cap_refusal_is_not_false()
	_test_conditions_have_no_side_effects()
	_test_actions_do_not_touch_the_env()
	_test_arity_is_enforced_per_name()
	var ran := passed + failed
	if ran != EXPECTED_CHECKS:
		failed += 1
		printerr(
			"HANDLERS_WP22 FAIL liveness: ran %d checks, expected %d - a function aborted before its assertions"
			% [ran, EXPECTED_CHECKS]
		)
	print("HANDLERS_WP22_RESULT passed=%d failed=%d" % [passed, failed])
	for world in _worlds:
		(world as SageScriptWorld)._release_facets()
	_worlds.clear()
	_watchdog.stop()
	quit(0 if failed == 0 else 1)


# --- Fixture helpers --------------------------------------------------------


func _argument(
	argument_type: int, integer: int = 0, real: float = 0.0, text: String = ""
) -> Dictionary:
	return {"argumentType": argument_type, "integer": integer, "real": real, "text": text}


func _name_arg(value: String) -> Dictionary:
	# SCIENCE and SCIENCE_AVAILABILITY have no observed wire code in this repo,
	# so SageScriptArgs does not assert one. The integer field carries a DECOY:
	# a handler that read the wrong field would get 424242 instead of the name,
	# loudly rather than plausibly.
	return _argument(99, 424242, 4.2, value)


func _player_arg(value: String) -> Dictionary:
	return _argument(ParamTypes.ARGUMENT_PLAYER, 424242, 4.2, value)


func _int_arg(value: int) -> Dictionary:
	# INT is carried in the `integer` payload field. The text decoy catches a
	# wrong-field read: "DECOY_TEXT" as a spell point cap would be int()'d to 0.
	return _argument(ParamTypes.ARGUMENT_INTEGER, value, 4.2, "DECOY_TEXT")


func _record(name: String, arguments: Array, inverted: bool = false) -> Dictionary:
	return {
		"contentType": 0,
		"internalName": {"name": name, "wireTypeCode": 3},
		"arguments": arguments,
		"enabled": true,
		"inverted": inverted,
	}


func _harness() -> Dictionary:
	var dispatch := Dispatch.new()
	CoreHandlers.register_all(dispatch)
	var outcome := Registry.register_all(dispatch)
	var world := SciencesWorld.new(20260808)
	_worlds.append(world)
	return {
		"dispatch": dispatch,
		"env": Env.new(),
		"world": world,
		"registration": outcome,
	}


func _progression(harness: Dictionary) -> SciencesProgression:
	return (harness["world"] as SciencesWorld).progression() as SciencesProgression


func _players(harness: Dictionary) -> SciencesPlayers:
	return (harness["world"] as SciencesWorld).players() as SciencesPlayers


func _act(harness: Dictionary, name: String, arguments: Array) -> int:
	return (harness["dispatch"] as SageScriptDispatch).execute_action(
		_record(name, arguments), harness["env"], harness["world"], "fixture"
	)


func _cond(harness: Dictionary, name: String, arguments: Array) -> bool:
	return (harness["dispatch"] as SageScriptDispatch).evaluate_condition(
		_record(name, arguments), harness["env"], harness["world"], "fixture"
	)


# --- 1. Registration --------------------------------------------------------


func _test_registration_covers_the_package() -> void:
	var harness := _harness()
	var dispatch: SageScriptDispatch = harness["dispatch"]
	var outcome: Dictionary = harness["registration"]

	_check(
		"registration_reports_no_errors",
		(outcome["errors"] as Array).is_empty(),
		str(outcome["errors"])
	)
	_check(
		"the_wp22_package_is_present",
		(outcome["packages"] as Array).has("WP22-sciences"),
		str(outcome["packages"])
	)

	var missing_actions: Array = SERVED_ACTIONS.filter(
		func(name): return not dispatch.action_handlers.has(name)
	)
	_check("every_served_action_is_registered", missing_actions.is_empty(), str(missing_actions))

	var missing_conditions: Array = SERVED_CONDITIONS.filter(
		func(name): return not dispatch.condition_handlers.has(name)
	)
	_check(
		"every_served_condition_is_registered",
		missing_conditions.is_empty(),
		str(missing_conditions)
	)

	var uncounted: Array = (
		SERVED_ACTIONS.filter(
			func(name): return not dispatch.implemented_actions().has(name)
		)
		+ SERVED_CONDITIONS.filter(
			func(name): return not dispatch.implemented_conditions().has(name)
		)
	)
	_check("every_served_name_counts_as_coverage", uncounted.is_empty(), str(uncounted))

	# The kind split is table-sourced, not name-guessed: the availability
	# "question" is an ACTION (script_action_table.gd:520) and must not have
	# leaked into the condition registry, nor the conditions into the actions.
	_check(
		"no_name_is_registered_under_the_wrong_kind",
		not dispatch.condition_handlers.has("PLAYER_SCIENCE_AVAILABILITY")
		and not dispatch.condition_handlers.has("PLAYER_SET_MAX_SPELLPOINTS")
		and not dispatch.action_handlers.has("PLAYER_ACQUIRED_SCIENCE")
		and not dispatch.action_handlers.has("PLAYER_HAS_REACHED_LEVEL_CAP")
	)


# --- 2/4. PLAYER_SCIENCE_AVAILABILITY (action) --------------------------------


func _test_science_availability_reaches_the_world() -> void:
	# PLAYER_SCIENCE_AVAILABILITY(PLAYER, SCIENCE, SCIENCE_AVAILABILITY) - table
	# row "PLAYER_SCIENCE_AVAILABILITY|PLAYER,SCIENCE,SCIENCE_AVAILABILITY|
	# Player|SHARED|S" (script_action_table.gd:520). Three side-by-side text
	# slots: the recorded triple below is the only thing that proves the science
	# and the availability token did not swap.
	var harness := _harness()
	var status := _act(
		harness, "PLAYER_SCIENCE_AVAILABILITY",
		[_player_arg(PLAYER), _name_arg(SCIENCE), _name_arg(AVAILABILITY)]
	)
	var progression := _progression(harness)
	_check(
		"science_availability_returns_ok", status == Dispatch.Status.OK, "status=%d" % status
	)
	_check(
		"the_availability_triple_arrives_in_signature_order",
		progression.availability_calls == [[PLAYER, SCIENCE, AVAILABILITY]],
		str(progression.availability_calls)
	)
	_check(
		"a_served_availability_change_records_no_gap",
		(harness["dispatch"] as SageScriptDispatch).gaps.is_empty(),
		str((harness["dispatch"] as SageScriptDispatch).gaps.to_lines())
	)


func _test_science_availability_refusal_is_honest() -> void:
	var harness := _harness()
	var world: SciencesWorld = harness["world"]
	var dispatch: SageScriptDispatch = harness["dispatch"]
	world.expect_refusals()
	_progression(harness).serve_availability = false

	var status := _act(
		harness, "PLAYER_SCIENCE_AVAILABILITY",
		[_player_arg(PLAYER), _name_arg(SCIENCE), _name_arg(AVAILABILITY)]
	)
	_check(
		"a_refused_availability_change_is_world_refused_with_a_gap",
		status == Dispatch.Status.WORLD_REFUSED
		and dispatch.gaps.has(
			"action", "PLAYER_SCIENCE_AVAILABILITY", GapLog.REASON_WORLD_REFUSED
		),
		"status=%d gaps=%s" % [status, str(dispatch.gaps.to_lines())]
	)
	_check(
		"the_availability_refusal_names_the_progression_facet_method",
		world.refused("progression.set_science_availability"),
		str(world.refusal_log)
	)


# --- 2/4. PLAYER_SET_MAX_SPELLPOINTS (action) ---------------------------------


func _test_set_max_spellpoints_reaches_the_world() -> void:
	# PLAYER_SET_MAX_SPELLPOINTS(PLAYER, INT) - table row
	# "PLAYER_SET_MAX_SPELLPOINTS|PLAYER,INT|Player|SHARED|S"
	# (script_action_table.gd:528). The cap lives in the INT slot's integer
	# payload; both fixture arguments carry decoys in their other fields, so a
	# wrong-field read arrives as 424242 or 0, never as 77.
	var harness := _harness()
	var status := _act(
		harness, "PLAYER_SET_MAX_SPELLPOINTS", [_player_arg(PLAYER), _int_arg(77)]
	)
	var players := _players(harness)
	_check(
		"set_max_spellpoints_returns_ok", status == Dispatch.Status.OK, "status=%d" % status
	)
	_check(
		"the_cap_is_read_from_the_integer_payload_of_slot_one",
		players.spellpoint_calls == [[PLAYER, 77]],
		str(players.spellpoint_calls)
	)
	_check(
		"a_served_spellpoint_cap_records_no_gap",
		(harness["dispatch"] as SageScriptDispatch).gaps.is_empty(),
		str((harness["dispatch"] as SageScriptDispatch).gaps.to_lines())
	)


func _test_set_max_spellpoints_refusal_is_honest() -> void:
	var harness := _harness()
	var world: SciencesWorld = harness["world"]
	var dispatch: SageScriptDispatch = harness["dispatch"]
	world.expect_refusals()
	_players(harness).serve_spellpoints = false

	var status := _act(
		harness, "PLAYER_SET_MAX_SPELLPOINTS", [_player_arg(PLAYER), _int_arg(77)]
	)
	_check(
		"a_refused_spellpoint_cap_is_world_refused_with_a_gap",
		status == Dispatch.Status.WORLD_REFUSED
		and dispatch.gaps.has(
			"action", "PLAYER_SET_MAX_SPELLPOINTS", GapLog.REASON_WORLD_REFUSED
		),
		"status=%d gaps=%s" % [status, str(dispatch.gaps.to_lines())]
	)
	_check(
		"the_spellpoint_refusal_names_the_players_facet_method",
		world.refused("players.set_max_spell_points"),
		str(world.refusal_log)
	)


# --- 3/4. PLAYER_ACQUIRED_SCIENCE (condition) ---------------------------------


func _test_acquired_science_answers() -> void:
	# PLAYER_ACQUIRED_SCIENCE(PLAYER, SCIENCE) - table row
	# "PLAYER_ACQUIRED_SCIENCE|PLAYER,SCIENCE|Player|SHARED|S"
	# (script_condition_table.gd:148). The fixture is keyed on the exact
	# (player, science) pair, so a slot swap misses it and surfaces as a
	# refusal, not a pass.
	var harness := _harness()
	var progression := _progression(harness)
	progression.teach_science(PLAYER, SCIENCE, true)
	progression.teach_science(PLAYER, OTHER_SCIENCE, false)

	_check(
		"an_owned_science_reads_true",
		_cond(harness, "PLAYER_ACQUIRED_SCIENCE", [_player_arg(PLAYER), _name_arg(SCIENCE)]),
		str((harness["dispatch"] as SageScriptDispatch).gaps.to_lines())
	)
	_check(
		"an_unowned_science_reads_false",
		not _cond(
			harness, "PLAYER_ACQUIRED_SCIENCE",
			[_player_arg(PLAYER), _name_arg(OTHER_SCIENCE)]
		)
	)
	_check(
		"the_player_and_science_reach_the_world_in_signature_order",
		progression.has_science_calls == [[PLAYER, SCIENCE], [PLAYER, OTHER_SCIENCE]],
		str(progression.has_science_calls)
	)
	# A GENUINE false is not a gap: if it were, the gap log could not be used to
	# tell "does not own it" apart from "the world could not say".
	_check(
		"a_genuine_unowned_answer_records_no_gap",
		(harness["dispatch"] as SageScriptDispatch).gaps.is_empty(),
		str((harness["dispatch"] as SageScriptDispatch).gaps.to_lines())
	)


func _test_acquired_science_refusal_is_not_false() -> void:
	# No fixture is taught, so the stub refuses. "The player does not own the
	# science" and "the world does not know" are different answers - only the
	# first may keep a gate shut on the record's own authority.
	var harness := _harness()
	var world: SciencesWorld = harness["world"]
	var dispatch: SageScriptDispatch = harness["dispatch"]
	world.expect_refusals()

	_check(
		"an_unanswerable_science_read_refuses_instead_of_reading_false",
		not _cond(harness, "PLAYER_ACQUIRED_SCIENCE", [_player_arg(PLAYER), _name_arg(SCIENCE)])
		and dispatch.gaps.has(
			"condition", "PLAYER_ACQUIRED_SCIENCE", GapLog.REASON_WORLD_REFUSED
		),
		str(dispatch.gaps.to_lines())
	)
	_check(
		"the_science_refusal_names_the_progression_facet_method",
		world.refused("progression.has_science"),
		str(world.refusal_log)
	)


# --- 3/4. PLAYER_HAS_REACHED_LEVEL_CAP (condition) ----------------------------


func _test_level_cap_answers() -> void:
	# PLAYER_HAS_REACHED_LEVEL_CAP(PLAYER) - table row
	# "PLAYER_HAS_REACHED_LEVEL_CAP|PLAYER|Player|SHARED|S"
	# (script_condition_table.gd:192). One argument; the fixture is keyed on
	# the player name, so a wrong-field read (the decoy 424242) misses it.
	var harness := _harness()
	var players := _players(harness)
	players.teach_level_cap(PLAYER, true)
	players.teach_level_cap("Player_2", false)

	_check(
		"a_capped_player_reads_true",
		_cond(harness, "PLAYER_HAS_REACHED_LEVEL_CAP", [_player_arg(PLAYER)]),
		str((harness["dispatch"] as SageScriptDispatch).gaps.to_lines())
	)
	_check(
		"an_uncapped_player_reads_false",
		not _cond(harness, "PLAYER_HAS_REACHED_LEVEL_CAP", [_player_arg("Player_2")])
	)
	_check(
		"the_player_reaches_the_world_verbatim",
		players.level_cap_calls == [PLAYER, "Player_2"],
		str(players.level_cap_calls)
	)
	_check(
		"a_genuine_uncapped_answer_records_no_gap",
		(harness["dispatch"] as SageScriptDispatch).gaps.is_empty(),
		str((harness["dispatch"] as SageScriptDispatch).gaps.to_lines())
	)


func _test_level_cap_refusal_is_not_false() -> void:
	var harness := _harness()
	var world: SciencesWorld = harness["world"]
	var dispatch: SageScriptDispatch = harness["dispatch"]
	world.expect_refusals()

	_check(
		"an_unanswerable_level_cap_read_refuses_instead_of_reading_false",
		not _cond(harness, "PLAYER_HAS_REACHED_LEVEL_CAP", [_player_arg("Player_9")])
		and dispatch.gaps.has(
			"condition", "PLAYER_HAS_REACHED_LEVEL_CAP", GapLog.REASON_WORLD_REFUSED
		),
		str(dispatch.gaps.to_lines())
	)
	_check(
		"the_level_cap_refusal_names_the_players_facet_method",
		world.refused("players.reached_level_cap"),
		str(world.refusal_log)
	)


# --- 5. Purity ---------------------------------------------------------------


func _test_conditions_have_no_side_effects() -> void:
	# Conditions are evaluated an unpredictable number of times. ctx.result is
	# the ONLY output: no env write, no world mutation.
	var harness := _harness()
	var env: SageScriptEnv = harness["env"]
	var progression := _progression(harness)
	var players := _players(harness)
	progression.teach_science(PLAYER, SCIENCE, true)
	players.teach_level_cap(PLAYER, true)

	for _repeat in range(3):
		_cond(harness, "PLAYER_ACQUIRED_SCIENCE", [_player_arg(PLAYER), _name_arg(SCIENCE)])
		_cond(harness, "PLAYER_HAS_REACHED_LEVEL_CAP", [_player_arg(PLAYER)])

	_check(
		"repeated_evaluation_left_the_script_environment_alone",
		env.snapshot()["counters"] == {} and env.snapshot()["flags"] == {}
	)
	_check(
		"repeated_evaluation_never_mutated_the_world",
		progression.availability_calls.is_empty() and players.spellpoint_calls.is_empty(),
		"availability=%s spellpoints=%s" % [
			str(progression.availability_calls), str(players.spellpoint_calls)
		]
	)


func _test_actions_do_not_touch_the_env() -> void:
	# Actions mutate through the WORLD only; the interpreter state is not theirs.
	var harness := _harness()
	var env: SageScriptEnv = harness["env"]
	env.set_counter("witness", 41)
	env.set_flag("witness_flag", true)
	var before := env.snapshot()

	_act(
		harness, "PLAYER_SCIENCE_AVAILABILITY",
		[_player_arg(PLAYER), _name_arg(SCIENCE), _name_arg(AVAILABILITY)]
	)
	_act(harness, "PLAYER_SET_MAX_SPELLPOINTS", [_player_arg(PLAYER), _int_arg(77)])

	_check(
		"actions_left_the_script_environment_alone",
		env.snapshot() == before,
		"before=%s after=%s" % [str(before), str(env.snapshot())]
	)


# --- 6. Arity ----------------------------------------------------------------


func _test_arity_is_enforced_per_name() -> void:
	# Arity is checked against the sourced signature before the handler runs, so
	# a positional read can never quietly slide onto a neighbouring argument.
	# One wrong-arity case per served name, and the world is never touched.
	var harness := _harness()
	var dispatch: SageScriptDispatch = harness["dispatch"]

	var short_availability := _act(
		harness, "PLAYER_SCIENCE_AVAILABILITY", [_player_arg(PLAYER), _name_arg(SCIENCE)]
	)
	_check(
		"a_two_argument_availability_record_is_rejected_before_the_world",
		short_availability == Dispatch.Status.BAD_ARGUMENTS
		and dispatch.gaps.has(
			"action", "PLAYER_SCIENCE_AVAILABILITY", GapLog.REASON_BAD_ARGUMENTS
		)
		and _progression(harness).availability_calls.is_empty(),
		"status=%d gaps=%s" % [short_availability, str(dispatch.gaps.to_lines())]
	)

	var short_spellpoints := _act(
		harness, "PLAYER_SET_MAX_SPELLPOINTS", [_player_arg(PLAYER)]
	)
	_check(
		"a_one_argument_spellpoint_record_is_rejected_before_the_world",
		short_spellpoints == Dispatch.Status.BAD_ARGUMENTS
		and dispatch.gaps.has(
			"action", "PLAYER_SET_MAX_SPELLPOINTS", GapLog.REASON_BAD_ARGUMENTS
		)
		and _players(harness).spellpoint_calls.is_empty(),
		"status=%d gaps=%s" % [short_spellpoints, str(dispatch.gaps.to_lines())]
	)

	_check(
		"a_one_argument_acquired_science_test_is_rejected",
		not _cond(harness, "PLAYER_ACQUIRED_SCIENCE", [_player_arg(PLAYER)])
		and dispatch.gaps.has(
			"condition", "PLAYER_ACQUIRED_SCIENCE", GapLog.REASON_BAD_ARGUMENTS
		)
		and _progression(harness).has_science_calls.is_empty(),
		str(dispatch.gaps.to_lines())
	)
	_check(
		"a_zero_argument_level_cap_test_is_rejected",
		not _cond(harness, "PLAYER_HAS_REACHED_LEVEL_CAP", [])
		and dispatch.gaps.has(
			"condition", "PLAYER_HAS_REACHED_LEVEL_CAP", GapLog.REASON_BAD_ARGUMENTS
		)
		and _players(harness).level_cap_calls.is_empty(),
		str(dispatch.gaps.to_lines())
	)


# --- Stub world ---------------------------------------------------------------
#
# The shared stub teaches neither the Progression facet nor these two Players
# members, and shared files may not be edited by a package, so the fixtures
# live here (the same placement WP09's StubProgression uses). Every read
# answers from a fixture or REFUSES, so a test that forgot to teach one gets a
# loud refusal rather than a plausible false; every command records its
# arguments verbatim so signature order is asserted, not assumed.


class SciencesProgression:
	extends SageScriptWorld.Progression

	var availability_calls: Array = []
	var has_science_calls: Array = []
	var serve_availability := true

	## "player|science" -> bool owned.
	var _owned: Dictionary = {}

	func teach_science(player: String, science: String, owned: bool) -> void:
		_owned["%s|%s" % [player, science]] = owned

	func set_science_availability(
		player: String, science: String, availability: String
	) -> bool:
		if not serve_availability:
			return _refuse_command("progression.set_science_availability")
		availability_calls.append([player, science, availability])
		return true

	func has_science(player: String, science: String) -> SageWorldQuery:
		var key := "%s|%s" % [player, science]
		if not _owned.has(key):
			return _refuse_query(
				"progression.has_science", "no fixture ownership for '%s'" % key
			)
		has_science_calls.append([player, science])
		return SageWorldQuery.hit(bool(_owned[key]))


class SciencesPlayers:
	extends SageScriptWorldStub.StubPlayers

	var spellpoint_calls: Array = []
	var level_cap_calls: Array = []
	var serve_spellpoints := true

	## player -> bool capped.
	var _capped: Dictionary = {}

	func teach_level_cap(player: String, capped: bool) -> void:
		_capped[player] = capped

	func set_max_spell_points(player: String, amount: int) -> bool:
		if not serve_spellpoints:
			return _refuse_command("players.set_max_spell_points")
		spellpoint_calls.append([player, amount])
		return true

	func reached_level_cap(player: String) -> SageWorldQuery:
		if not _capped.has(player):
			return _refuse_query(
				"players.reached_level_cap", "no fixture cap state for player '%s'" % player
			)
		level_cap_calls.append(player)
		return SageWorldQuery.hit(bool(_capped[player]))


class SciencesWorld:
	extends SageScriptWorldStub

	func _make_progression() -> SageScriptWorld.Progression:
		return SciencesProgression.new()

	func _make_players() -> SageScriptWorld.Players:
		return SciencesPlayers.new()


# --- Reporting ----------------------------------------------------------------


func _check(name: String, condition: bool, detail: String = "") -> void:
	_watchdog.note(name)
	if condition:
		passed += 1
		print("HANDLERS_WP22 PASS %s" % name)
	else:
		failed += 1
		printerr(
			"HANDLERS_WP22 FAIL %s%s" % [name, " (%s)" % detail if detail != "" else ""]
		)
