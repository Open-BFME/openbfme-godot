extends SceneTree

## Proof runner for WP20-skirmish-conditions (wp20_skirmish_conditions.gd) -
## the game-mode and proximity CONDITIONS that gate skirmish map init scripts
## (IS_GAME_MODE_ACTIVE guards every SkirmishGollum init script; without it the
## spawn scripts never fire even once the spawn action works).
##
## Covers, in order:
##   1. registration    - all 5 conditions registered by the package, counted
##                        as coverage, with no registry errors
##   2. per condition   - TRUE against stub-seeded state, FALSE against
##                        stub-seeded state, and the third answer: a world
##                        refusal is a structured WORLD_REFUSED gap, never a
##                        plain false (one refusal assert per facet used -
##                        meta and players - plus one per condition here,
##                        because every one of these gates a spawn script)
##   3. positional args - the two comparison signatures put integer-valued
##                        COMPARISON and INT slots side by side; fixtures are
##                        chosen so a swapped or inverted read answers
##                        DIFFERENTLY, and world reads are recorded verbatim
##                        so signature order is asserted, not assumed
##   4. operator table  - an out-of-table comparison operator is
##                        BAD_ARGUMENTS, never a quiet false (compare_int
##                        answers false for operators it does not know)
##   5. purity          - conditions write ctx.result only: repeated
##                        evaluation leaves the env untouched
##   6. arity           - one wrong-arity case per condition, rejected before
##                        the handler runs
##
## Every fixture value is SYNTHETIC. No retail payload text is reproduced.
##
## Invocation:
##   <godot> --headless --path game \
##     --script res://tests/handlers_wp20_skirmish_conditions_runner.gd

const Registry := preload("res://src/script/handlers/_registry.gd")
const Dispatch := preload("res://src/script/script_dispatch.gd")
const CoreHandlers := preload("res://src/script/script_handlers_core.gd")
const Env := preload("res://src/script/script_env.gd")
const GapLog := preload("res://src/script/script_gaps.gd")
const ParamTypes := preload("res://src/script/script_param_types.gd")
const Watchdog := preload("res://tests/runner_watchdog.gd")

## The 5 conditions this package serves. Written out rather than derived from
## the module, so a member quietly disappearing from register() fails here.
const SERVED_CONDITIONS := [
	"IS_GAME_MODE_ACTIVE",
	"HAS_EVA_EVENT_PLAYED_IN_LAST_N_SECONDS",
	"IS_NUM_OF_UNITS_BELONGING_TO_PLAYER_NEAR_EVA_EVENT_LAST_PLAYED_LOCATION_COMPARISON_INT",
	"PLAYER_HAS_NUMBER_UNITS_DISTANCE_FROM_OBJECT",
	"PLAYER_HAS_NUMBER_OBJECTS_WITH_MODELCONDITION",
]

## HAS_EVA_EVENT_PLAYED_IN_LAST_N_SECONDS carries no PLAYER argument; the
## handler must act as the script-owning player via the retail token the
## world's player resolution defines. Asserted verbatim below.
const THIS_PLAYER := "<This Player>"

## LIVENESS GUARD. A GDScript RUNTIME error aborts the enclosing function on
## the spot without propagating, so every later _check() in that function
## silently never runs. A healthy run makes exactly this many checks; any
## other number means something aborted (or an assertion was added without
## updating this) and the result is not to be trusted.
const EXPECTED_CHECKS := 38

var passed := 0
var failed := 0
var _watchdog := Watchdog.new()

## Every world this run created, so facet<->world reference cycles can be
## broken before quit (see the Facet class comment in script_world.gd: a
## dropped world with live facets is a RefCounted cycle that leaks and can AV
## on Windows teardown).
var _worlds: Array = []


func _initialize() -> void:
	_watchdog.start(self, "HANDLERS_WP20", 0, 0, true)
	_watchdog.set_result_provider(func() -> Vector2i: return Vector2i(passed, failed))
	call_deferred("_run")


func _run() -> void:
	_test_registration_covers_the_package()
	_test_game_mode_matches_the_authored_mode_string()
	_test_game_mode_refusal_is_not_false()
	_test_eva_event_recency_window()
	_test_eva_event_refusal_is_not_false()
	_test_units_near_last_eva_event_comparison()
	_test_units_near_refusal_is_not_false()
	_test_units_distance_from_object()
	_test_units_distance_refusal_is_not_false()
	_test_objects_with_model_condition()
	_test_model_condition_refusal_is_not_false()
	_test_conditions_have_no_side_effects()
	_test_arity_is_enforced_per_condition()
	var ran := passed + failed
	if ran != EXPECTED_CHECKS:
		failed += 1
		printerr(
			"HANDLERS_WP20 FAIL liveness: ran %d checks, expected %d - a function aborted before its assertions"
			% [ran, EXPECTED_CHECKS]
		)
	print("HANDLERS_WP20_RESULT passed=%d failed=%d" % [passed, failed])
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
	# TEXT_STRING, EVA_EVENT, MODEL_CONDITION and UNIT have no observed wire
	# code in this repo, so SageScriptArgs does not assert one. The integer
	# field carries a DECOY: a handler that read the wrong field would get
	# 424242 instead of the name, loudly rather than plausibly.
	return _argument(99, 424242, 0.0, value)


func _player_arg(value: String) -> Dictionary:
	return _argument(ParamTypes.ARGUMENT_PLAYER, 424242, 0.0, value)


func _int_arg(value: int) -> Dictionary:
	return _argument(ParamTypes.ARGUMENT_INTEGER, value, 0.0, "DECOY_TEXT")


func _comparison_arg(value: int) -> Dictionary:
	return _argument(ParamTypes.ARGUMENT_COMPARISON, value, 0.0, "DECOY_OPERATOR")


func _real_arg(value: float) -> Dictionary:
	# REAL is carried in the `real` payload field (wire code 1, observed). The
	# integer decoy catches a wrong-field read: 424242 seconds/units of radius
	# would flip every window and proximity fixture below.
	return _argument(ParamTypes.ARGUMENT_REAL, 424242, value, "DECOY_TEXT")


func _record(name: String, arguments: Array) -> Dictionary:
	return {
		"contentType": 0,
		"internalName": {"name": name, "wireTypeCode": 3},
		"arguments": arguments,
		"enabled": true,
		"inverted": false,
	}


func _harness() -> Dictionary:
	var dispatch := Dispatch.new()
	CoreHandlers.register_all(dispatch)
	var outcome := Registry.register_all(dispatch)
	var world := SkirmishWorld.new(20260808)
	_worlds.append(world)
	return {
		"dispatch": dispatch,
		"env": Env.new(),
		"world": world,
		"registration": outcome,
	}


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
		"the_wp20_package_is_present",
		(outcome["packages"] as Array).has("WP20-skirmish-conditions"),
		str(outcome["packages"])
	)

	var missing: Array[String] = []
	for name: String in SERVED_CONDITIONS:
		if not dispatch.condition_handlers.has(name):
			missing.append(name)
	_check("every_served_condition_is_registered", missing.is_empty(), str(missing))

	var uncounted: Array[String] = []
	var implemented := dispatch.implemented_conditions()
	for name: String in SERVED_CONDITIONS:
		if not implemented.has(name):
			uncounted.append(name)
	_check(
		"every_served_condition_counts_as_coverage", uncounted.is_empty(), str(uncounted)
	)


# --- 2. IS_GAME_MODE_ACTIVE -------------------------------------------------


func _test_game_mode_matches_the_authored_mode_string() -> void:
	# IS_GAME_MODE_ACTIVE(TEXT_STRING) - table row
	# "IS_GAME_MODE_ACTIVE|TEXT_STRING|GameType|BFME2ROTWK_PLUS|S".
	var harness := _harness()
	var world: SkirmishWorld = harness["world"]
	world.active_game_mode = "SyntheticModeSkirmish"

	_check(
		"the_matching_game_mode_reads_true",
		_cond(harness, "IS_GAME_MODE_ACTIVE", [_name_arg("SyntheticModeSkirmish")]),
		str((harness["dispatch"] as SageScriptDispatch).gaps.to_lines())
	)
	_check(
		"a_different_game_mode_reads_false",
		not _cond(harness, "IS_GAME_MODE_ACTIVE", [_name_arg("SyntheticModeCampaign")])
	)
	_check(
		"the_mode_comparison_is_case_sensitive",
		not _cond(harness, "IS_GAME_MODE_ACTIVE", [_name_arg("syntheticmodeskirmish")])
	)
	_check(
		"the_mode_string_reaches_the_meta_facet_verbatim",
		world.reads.has("meta.game_mode|SyntheticModeSkirmish"),
		str(world.reads)
	)


func _test_game_mode_refusal_is_not_false() -> void:
	# The stub was taught no game mode, so the read REFUSES. "The mode is not
	# active" and "the world does not know the mode" are different answers -
	# only the first may be a plain false.
	var harness := _harness()
	var world: SkirmishWorld = harness["world"]
	var dispatch: SageScriptDispatch = harness["dispatch"]
	world.expect_refusals()

	_check(
		"an_unanswerable_game_mode_refuses_instead_of_reading_false",
		not _cond(harness, "IS_GAME_MODE_ACTIVE", [_name_arg("SyntheticModeSkirmish")])
		and dispatch.gaps.has(
			"condition", "IS_GAME_MODE_ACTIVE", GapLog.REASON_WORLD_REFUSED
		),
		str(dispatch.gaps.to_lines())
	)
	_check(
		"the_game_mode_refusal_names_the_meta_facet_method",
		world.refused("meta.game_mode"),
		str(world.refusal_log)
	)


# --- 2. HAS_EVA_EVENT_PLAYED_IN_LAST_N_SECONDS -------------------------------


func _test_eva_event_recency_window() -> void:
	# HAS_EVA_EVENT_PLAYED_IN_LAST_N_SECONDS(EVA_EVENT, REAL) - table row
	# "HAS_EVA_EVENT_PLAYED_IN_LAST_N_SECONDS|EVA_EVENT,REAL|Player|BFME2ROTWK_PLUS|S".
	# No PLAYER slot is authored, so the handler must act as the script-owning
	# player via the "<This Player>" token. The fixture is keyed on the exact
	# token, so a handler that passed anything else misses it and refuses.
	var harness := _harness()
	var world: SkirmishWorld = harness["world"]
	world.set_eva_seconds_ago(THIS_PLAYER, "SyntheticEvaEvent", 5.0)

	_check(
		"an_event_played_inside_the_window_reads_true",
		_cond(harness, "HAS_EVA_EVENT_PLAYED_IN_LAST_N_SECONDS", [
			_name_arg("SyntheticEvaEvent"), _real_arg(10.0)
		]),
		str((harness["dispatch"] as SageScriptDispatch).gaps.to_lines())
	)
	_check(
		"an_event_played_outside_the_window_reads_false",
		not _cond(harness, "HAS_EVA_EVENT_PLAYED_IN_LAST_N_SECONDS", [
			_name_arg("SyntheticEvaEvent"), _real_arg(2.0)
		])
	)
	_check(
		"the_acting_player_is_the_this_player_token_and_args_arrive_in_order",
		world.reads.has(
			"players.eva_event_played_within|%s|SyntheticEvaEvent|10.0" % THIS_PLAYER
		),
		str(world.reads)
	)


func _test_eva_event_refusal_is_not_false() -> void:
	var harness := _harness()
	var world: SkirmishWorld = harness["world"]
	var dispatch: SageScriptDispatch = harness["dispatch"]
	world.expect_refusals()

	_check(
		"an_unanswerable_eva_recency_read_refuses_instead_of_reading_false",
		not _cond(harness, "HAS_EVA_EVENT_PLAYED_IN_LAST_N_SECONDS", [
			_name_arg("SyntheticEvaEvent"), _real_arg(10.0)
		])
		and dispatch.gaps.has(
			"condition",
			"HAS_EVA_EVENT_PLAYED_IN_LAST_N_SECONDS",
			GapLog.REASON_WORLD_REFUSED
		),
		str(dispatch.gaps.to_lines())
	)
	_check(
		"the_eva_refusal_names_the_players_facet_method",
		world.refused("players.eva_event_played_within"),
		str(world.refusal_log)
	)


# --- 2/3/4. IS_NUM_OF_UNITS_..._COMPARISON_INT --------------------------------


func _test_units_near_last_eva_event_comparison() -> void:
	# IS_NUM_OF_UNITS_BELONGING_TO_PLAYER_NEAR_EVA_EVENT_LAST_PLAYED_LOCATION_COMPARISON_INT
	#   (PLAYER, REAL, EVA_EVENT, COMPARISON, INT) - table row 94.
	#
	# THE FIXTURE MAKES EVERY MIS-READ VISIBLE. World count = 3, operator =
	# COMPARE_GREATER (4), threshold = 2:
	#   * correct read:                       3 > 2  -> true
	#   * COMPARISON/INT slots swapped:       operator becomes 2 (==),
	#                                         threshold becomes 4, 3 == 4 -> false
	#   * comparison inverted (threshold op count): 2 > 3 -> false
	# The fixture is also keyed on (player, radius), so a radius read from the
	# wrong slot (the decoy 424242 in the REAL argument's integer field, or the
	# INT slot) misses the fixture and surfaces as a refusal, not a pass.
	var harness := _harness()
	var world: SkirmishWorld = harness["world"]
	var dispatch: SageScriptDispatch = harness["dispatch"]
	world.set_units_near("Player_1", 40.0, 3)

	_check(
		"greater_than_compares_the_world_count_against_the_authored_threshold",
		_cond(
			harness,
			"IS_NUM_OF_UNITS_BELONGING_TO_PLAYER_NEAR_EVA_EVENT_LAST_PLAYED_LOCATION_COMPARISON_INT",
			[
				_player_arg("Player_1"), _real_arg(40.0), _name_arg("SyntheticEvaEvent"),
				_comparison_arg(ParamTypes.COMPARE_GREATER), _int_arg(2)
			]
		),
		str(dispatch.gaps.to_lines())
	)
	_check(
		"the_comparison_answers_false_when_the_count_fails_it",
		not _cond(
			harness,
			"IS_NUM_OF_UNITS_BELONGING_TO_PLAYER_NEAR_EVA_EVENT_LAST_PLAYED_LOCATION_COMPARISON_INT",
			[
				_player_arg("Player_1"), _real_arg(40.0), _name_arg("SyntheticEvaEvent"),
				_comparison_arg(ParamTypes.COMPARE_LESS), _int_arg(2)
			]
		)
	)
	_check(
		"the_player_and_radius_reach_the_world_in_signature_order",
		world.reads.has("players.units_near_last_eva_event|Player_1|40.0"),
		str(world.reads)
	)
	_check(
		"an_out_of_table_operator_is_bad_arguments_not_a_quiet_false",
		not _cond(
			harness,
			"IS_NUM_OF_UNITS_BELONGING_TO_PLAYER_NEAR_EVA_EVENT_LAST_PLAYED_LOCATION_COMPARISON_INT",
			[
				_player_arg("Player_1"), _real_arg(40.0), _name_arg("SyntheticEvaEvent"),
				_comparison_arg(6), _int_arg(3)
			]
		)
		and dispatch.gaps.has(
			"condition",
			"IS_NUM_OF_UNITS_BELONGING_TO_PLAYER_NEAR_EVA_EVENT_LAST_PLAYED_LOCATION_COMPARISON_INT",
			GapLog.REASON_BAD_ARGUMENTS
		),
		str(dispatch.gaps.to_lines())
	)


func _test_units_near_refusal_is_not_false() -> void:
	# The fixture asks ">= 0", which a count-defaulting-to-zero implementation
	# would answer TRUE: refusing it proves the zero was never invented.
	var harness := _harness()
	var world: SkirmishWorld = harness["world"]
	var dispatch: SageScriptDispatch = harness["dispatch"]
	world.expect_refusals()

	_check(
		"an_unanswerable_proximity_count_refuses_instead_of_reading_as_zero",
		not _cond(
			harness,
			"IS_NUM_OF_UNITS_BELONGING_TO_PLAYER_NEAR_EVA_EVENT_LAST_PLAYED_LOCATION_COMPARISON_INT",
			[
				_player_arg("Player_9"), _real_arg(40.0), _name_arg("SyntheticEvaEvent"),
				_comparison_arg(ParamTypes.COMPARE_GREATER_EQUAL), _int_arg(0)
			]
		)
		and dispatch.gaps.has(
			"condition",
			"IS_NUM_OF_UNITS_BELONGING_TO_PLAYER_NEAR_EVA_EVENT_LAST_PLAYED_LOCATION_COMPARISON_INT",
			GapLog.REASON_WORLD_REFUSED
		),
		str(dispatch.gaps.to_lines())
	)
	_check(
		"the_proximity_refusal_names_the_players_facet_method",
		world.refused("players.units_near_last_eva_event"),
		str(world.refusal_log)
	)


# --- 2/3/4. PLAYER_HAS_NUMBER_UNITS_DISTANCE_FROM_OBJECT ----------------------


func _test_units_distance_from_object() -> void:
	# PLAYER_HAS_NUMBER_UNITS_DISTANCE_FROM_OBJECT
	#   (PLAYER, COMPARISON, INT, REAL, UNIT) - table row 181.
	# The fixture is keyed on the FULL (player, empty-type, origin, distance)
	# tuple in world-signature order, so a handler that rotated the arguments
	# (distance as count, unit as player) misses the fixture and refuses.
	var harness := _harness()
	var world: SkirmishWorld = harness["world"]
	var dispatch: SageScriptDispatch = harness["dispatch"]
	world.set_units_within("Player_1", "", "SyntheticTower", 75.0, 3)

	_check(
		"the_count_within_distance_is_compared_against_the_authored_threshold",
		_cond(harness, "PLAYER_HAS_NUMBER_UNITS_DISTANCE_FROM_OBJECT", [
			_player_arg("Player_1"), _comparison_arg(ParamTypes.COMPARE_GREATER),
			_int_arg(2), _real_arg(75.0), _name_arg("SyntheticTower")
		]),
		str(dispatch.gaps.to_lines())
	)
	_check(
		"the_distance_comparison_answers_false_when_the_count_fails_it",
		not _cond(harness, "PLAYER_HAS_NUMBER_UNITS_DISTANCE_FROM_OBJECT", [
			_player_arg("Player_1"), _comparison_arg(ParamTypes.COMPARE_EQUAL),
			_int_arg(999), _real_arg(75.0), _name_arg("SyntheticTower")
		])
	)
	_check(
		"the_every_type_form_and_signature_order_reach_the_world",
		world.reads.has("players.object_count_within_distance|Player_1||SyntheticTower|75.0"),
		str(world.reads)
	)
	_check(
		"an_out_of_table_distance_operator_is_bad_arguments",
		not _cond(harness, "PLAYER_HAS_NUMBER_UNITS_DISTANCE_FROM_OBJECT", [
			_player_arg("Player_1"), _comparison_arg(-1),
			_int_arg(3), _real_arg(75.0), _name_arg("SyntheticTower")
		])
		and dispatch.gaps.has(
			"condition",
			"PLAYER_HAS_NUMBER_UNITS_DISTANCE_FROM_OBJECT",
			GapLog.REASON_BAD_ARGUMENTS
		),
		str(dispatch.gaps.to_lines())
	)


func _test_units_distance_refusal_is_not_false() -> void:
	var harness := _harness()
	var world: SkirmishWorld = harness["world"]
	var dispatch: SageScriptDispatch = harness["dispatch"]
	world.expect_refusals()

	_check(
		"an_unanswerable_distance_count_refuses_instead_of_reading_as_zero",
		not _cond(harness, "PLAYER_HAS_NUMBER_UNITS_DISTANCE_FROM_OBJECT", [
			_player_arg("Player_9"), _comparison_arg(ParamTypes.COMPARE_GREATER_EQUAL),
			_int_arg(0), _real_arg(75.0), _name_arg("SyntheticTower")
		])
		and dispatch.gaps.has(
			"condition",
			"PLAYER_HAS_NUMBER_UNITS_DISTANCE_FROM_OBJECT",
			GapLog.REASON_WORLD_REFUSED
		),
		str(dispatch.gaps.to_lines())
	)
	_check(
		"the_distance_refusal_names_the_players_facet_method",
		world.refused("players.object_count_within_distance"),
		str(world.refusal_log)
	)


# --- 2/3/4. PLAYER_HAS_NUMBER_OBJECTS_WITH_MODELCONDITION ---------------------


func _test_objects_with_model_condition() -> void:
	# PLAYER_HAS_NUMBER_OBJECTS_WITH_MODELCONDITION
	#   (PLAYER, MODEL_CONDITION, COMPARISON, INT) - table row 177.
	# The SHARED stub already teaches players.object_count_with_model_condition
	# keyed on (player, model_condition): a handler that read the condition
	# name from the wrong slot misses the fixture and REFUSES, which the first
	# check below would report as a wrong answer.
	var harness := _harness()
	var world: SkirmishWorld = harness["world"]
	var dispatch: SageScriptDispatch = harness["dispatch"]
	world.set_player_model_condition_count("Player_1", "SYNTHETIC_MC_BURNING", 2)

	_check(
		"the_model_condition_count_is_compared_against_the_authored_threshold",
		_cond(harness, "PLAYER_HAS_NUMBER_OBJECTS_WITH_MODELCONDITION", [
			_player_arg("Player_1"), _name_arg("SYNTHETIC_MC_BURNING"),
			_comparison_arg(ParamTypes.COMPARE_EQUAL), _int_arg(2)
		]),
		str(dispatch.gaps.to_lines())
	)
	_check(
		"the_model_condition_comparison_answers_false_when_the_count_fails_it",
		not _cond(harness, "PLAYER_HAS_NUMBER_OBJECTS_WITH_MODELCONDITION", [
			_player_arg("Player_1"), _name_arg("SYNTHETIC_MC_BURNING"),
			_comparison_arg(ParamTypes.COMPARE_GREATER), _int_arg(5)
		])
	)
	_check(
		"an_out_of_table_model_condition_operator_is_bad_arguments",
		not _cond(harness, "PLAYER_HAS_NUMBER_OBJECTS_WITH_MODELCONDITION", [
			_player_arg("Player_1"), _name_arg("SYNTHETIC_MC_BURNING"),
			_comparison_arg(6), _int_arg(2)
		])
		and dispatch.gaps.has(
			"condition",
			"PLAYER_HAS_NUMBER_OBJECTS_WITH_MODELCONDITION",
			GapLog.REASON_BAD_ARGUMENTS
		),
		str(dispatch.gaps.to_lines())
	)


func _test_model_condition_refusal_is_not_false() -> void:
	var harness := _harness()
	var world: SkirmishWorld = harness["world"]
	var dispatch: SageScriptDispatch = harness["dispatch"]
	world.expect_refusals()

	_check(
		"an_unanswerable_model_condition_count_refuses_instead_of_reading_as_zero",
		not _cond(harness, "PLAYER_HAS_NUMBER_OBJECTS_WITH_MODELCONDITION", [
			_player_arg("Player_9"), _name_arg("SYNTHETIC_MC_BURNING"),
			_comparison_arg(ParamTypes.COMPARE_GREATER_EQUAL), _int_arg(0)
		])
		and dispatch.gaps.has(
			"condition",
			"PLAYER_HAS_NUMBER_OBJECTS_WITH_MODELCONDITION",
			GapLog.REASON_WORLD_REFUSED
		),
		str(dispatch.gaps.to_lines())
	)
	_check(
		"the_model_condition_refusal_names_the_players_facet_method",
		world.refused("players.object_count_with_model_condition"),
		str(world.refusal_log)
	)


# --- 5. Purity ---------------------------------------------------------------


func _test_conditions_have_no_side_effects() -> void:
	# Conditions are evaluated an unpredictable number of times (these five
	# gate polled init scripts), so any env or world mutation is a determinism
	# bug. ctx.result is the ONLY output.
	var harness := _harness()
	var world: SkirmishWorld = harness["world"]
	var env: SageScriptEnv = harness["env"]
	world.active_game_mode = "SyntheticModeSkirmish"
	world.set_eva_seconds_ago(THIS_PLAYER, "SyntheticEvaEvent", 5.0)
	world.set_units_near("Player_1", 40.0, 3)
	world.set_units_within("Player_1", "", "SyntheticTower", 75.0, 3)
	world.set_player_model_condition_count("Player_1", "SYNTHETIC_MC_BURNING", 2)

	for _repeat in range(3):
		_cond(harness, "IS_GAME_MODE_ACTIVE", [_name_arg("SyntheticModeSkirmish")])
		_cond(harness, "HAS_EVA_EVENT_PLAYED_IN_LAST_N_SECONDS", [
			_name_arg("SyntheticEvaEvent"), _real_arg(10.0)
		])
		_cond(
			harness,
			"IS_NUM_OF_UNITS_BELONGING_TO_PLAYER_NEAR_EVA_EVENT_LAST_PLAYED_LOCATION_COMPARISON_INT",
			[
				_player_arg("Player_1"), _real_arg(40.0), _name_arg("SyntheticEvaEvent"),
				_comparison_arg(ParamTypes.COMPARE_GREATER), _int_arg(2)
			]
		)
		_cond(harness, "PLAYER_HAS_NUMBER_UNITS_DISTANCE_FROM_OBJECT", [
			_player_arg("Player_1"), _comparison_arg(ParamTypes.COMPARE_GREATER),
			_int_arg(2), _real_arg(75.0), _name_arg("SyntheticTower")
		])
		_cond(harness, "PLAYER_HAS_NUMBER_OBJECTS_WITH_MODELCONDITION", [
			_player_arg("Player_1"), _name_arg("SYNTHETIC_MC_BURNING"),
			_comparison_arg(ParamTypes.COMPARE_EQUAL), _int_arg(2)
		])

	_check(
		"repeated_evaluation_left_the_script_environment_alone",
		env.snapshot()["counters"] == {} and env.snapshot()["flags"] == {}
	)


# --- 6. Arity ----------------------------------------------------------------


func _test_arity_is_enforced_per_condition() -> void:
	# Arity is checked against the sourced signature before the handler runs,
	# so a positional read can never quietly slide onto a neighbouring
	# argument. One wrong-arity case per served condition.
	var harness := _harness()
	var dispatch: SageScriptDispatch = harness["dispatch"]

	_check(
		"a_zero_argument_game_mode_test_is_rejected",
		not _cond(harness, "IS_GAME_MODE_ACTIVE", [])
		and dispatch.gaps.has(
			"condition", "IS_GAME_MODE_ACTIVE", GapLog.REASON_BAD_ARGUMENTS
		),
		str(dispatch.gaps.to_lines())
	)
	_check(
		"a_one_argument_eva_recency_test_is_rejected",
		not _cond(harness, "HAS_EVA_EVENT_PLAYED_IN_LAST_N_SECONDS", [
			_name_arg("SyntheticEvaEvent")
		])
		and dispatch.gaps.has(
			"condition",
			"HAS_EVA_EVENT_PLAYED_IN_LAST_N_SECONDS",
			GapLog.REASON_BAD_ARGUMENTS
		),
		str(dispatch.gaps.to_lines())
	)
	_check(
		"a_four_argument_proximity_comparison_is_rejected",
		not _cond(
			harness,
			"IS_NUM_OF_UNITS_BELONGING_TO_PLAYER_NEAR_EVA_EVENT_LAST_PLAYED_LOCATION_COMPARISON_INT",
			[
				_player_arg("Player_1"), _real_arg(40.0),
				_comparison_arg(ParamTypes.COMPARE_GREATER), _int_arg(2)
			]
		)
		and dispatch.gaps.has(
			"condition",
			"IS_NUM_OF_UNITS_BELONGING_TO_PLAYER_NEAR_EVA_EVENT_LAST_PLAYED_LOCATION_COMPARISON_INT",
			GapLog.REASON_BAD_ARGUMENTS
		),
		str(dispatch.gaps.to_lines())
	)
	_check(
		"a_four_argument_distance_test_is_rejected",
		not _cond(harness, "PLAYER_HAS_NUMBER_UNITS_DISTANCE_FROM_OBJECT", [
			_player_arg("Player_1"), _comparison_arg(ParamTypes.COMPARE_GREATER),
			_int_arg(2), _real_arg(75.0)
		])
		and dispatch.gaps.has(
			"condition",
			"PLAYER_HAS_NUMBER_UNITS_DISTANCE_FROM_OBJECT",
			GapLog.REASON_BAD_ARGUMENTS
		),
		str(dispatch.gaps.to_lines())
	)
	_check(
		"a_three_argument_model_condition_test_is_rejected",
		not _cond(harness, "PLAYER_HAS_NUMBER_OBJECTS_WITH_MODELCONDITION", [
			_player_arg("Player_1"), _name_arg("SYNTHETIC_MC_BURNING"),
			_comparison_arg(ParamTypes.COMPARE_EQUAL)
		])
		and dispatch.gaps.has(
			"condition",
			"PLAYER_HAS_NUMBER_OBJECTS_WITH_MODELCONDITION",
			GapLog.REASON_BAD_ARGUMENTS
		),
		str(dispatch.gaps.to_lines())
	)


# --- Stub world ---------------------------------------------------------------
#
# SageScriptWorldStub already teaches players.object_count_with_model_condition
# (fixture-or-refuse), which this package's most-shared member needs - that
# behaviour is inherited rather than re-taught. What the shared stub does NOT
# teach - meta.game_mode and the three proximity/recency reads - is taught
# HERE rather than in the shared stub, because the shared stub is an existing
# file that ~17 concurrent packages also depend on. Every read is recorded as
# a flat string in world-signature order so a test asserts on the exact
# arguments that reached the world; every read answers from a fixture or
# REFUSES, so a test that forgot to set one gets a loud refusal rather than a
# plausible zero. Radii/distances/seconds are formatted "%.1f" so the fixture
# key and the assertion string cannot drift apart on float printing.


class SkirmishWorld:
	extends SageScriptWorldStub

	var reads: Array[String] = []

	## null until taught; a String once taught. meta.game_mode refuses while
	## untaught, because "no mode set" and "mode not active" differ.
	var active_game_mode: Variant = null

	## "player|event" -> float seconds since the event last played.
	var eva_ago: Dictionary = {}

	## "player|radius" -> int units near the last EVA event location.
	var units_near_counts: Dictionary = {}

	## "player|object_type|origin|distance" -> int units within distance.
	var units_within_counts: Dictionary = {}

	func set_eva_seconds_ago(player: String, event: String, seconds_ago: float) -> void:
		eva_ago["%s|%s" % [player, event]] = seconds_ago

	func set_units_near(player: String, radius: float, count: int) -> void:
		units_near_counts["%s|%.1f" % [player, radius]] = count

	func set_units_within(
		player: String, object_type: String, origin: String, distance: float, count: int
	) -> void:
		units_within_counts["%s|%s|%s|%.1f" % [player, object_type, origin, distance]] = count

	func _make_players() -> SageScriptWorld.Players:
		return SkirmishPlayers.new()

	func _make_meta() -> SageScriptWorld.Meta:
		return SkirmishMeta.new()


class SkirmishMeta:
	extends SageScriptWorld.Meta

	func game_mode(mode: String) -> SageWorldQuery:
		var stub := world as SkirmishWorld
		stub.reads.append("meta.game_mode|%s" % mode)
		if stub.active_game_mode == null:
			return _refuse_query("meta.game_mode", "no fixture game mode was taught")
		return SageWorldQuery.hit(String(stub.active_game_mode) == mode)


class SkirmishPlayers:
	extends SageScriptWorldStub.StubPlayers

	func eva_event_played_within(
		player: String, event: String, seconds: float
	) -> SageWorldQuery:
		var stub := world as SkirmishWorld
		stub.reads.append(
			"players.eva_event_played_within|%s|%s|%.1f" % [player, event, seconds]
		)
		var key := "%s|%s" % [player, event]
		if not stub.eva_ago.has(key):
			return _refuse_query(
				"players.eva_event_played_within", "no fixture recency for '%s'" % key
			)
		return SageWorldQuery.hit(float(stub.eva_ago[key]) <= seconds)

	func units_near_last_eva_event(player: String, radius: float) -> SageWorldQuery:
		var stub := world as SkirmishWorld
		var key := "%s|%.1f" % [player, radius]
		stub.reads.append("players.units_near_last_eva_event|%s" % key)
		if not stub.units_near_counts.has(key):
			return _refuse_query(
				"players.units_near_last_eva_event", "no fixture count for '%s'" % key
			)
		return SageWorldQuery.hit(int(stub.units_near_counts[key]))

	func object_count_within_distance(
		player: String, object_type: String, origin: String, distance: float
	) -> SageWorldQuery:
		var stub := world as SkirmishWorld
		var key := "%s|%s|%s|%.1f" % [player, object_type, origin, distance]
		stub.reads.append("players.object_count_within_distance|%s" % key)
		if not stub.units_within_counts.has(key):
			return _refuse_query(
				"players.object_count_within_distance", "no fixture count for '%s'" % key
			)
		return SageWorldQuery.hit(int(stub.units_within_counts[key]))


# --- Reporting ----------------------------------------------------------------


func _check(name: String, condition: bool, detail: String = "") -> void:
	_watchdog.note(name)
	if condition:
		passed += 1
		print("HANDLERS_WP20 PASS %s" % name)
	else:
		failed += 1
		printerr(
			"HANDLERS_WP20 FAIL %s%s" % [name, " (%s)" % detail if detail != "" else ""]
		)
