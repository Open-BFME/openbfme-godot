extends SceneTree

## Proof runner for WP17-players, AI-critical subset (wp17_ai_players.gd).
##
## Covers, in order:
##   1. registration    - all 7 AI-used members accounted for and served, with
##                        no overlap and no silent absence
##   2. positional args - the signature that puts two INTEGER-VALUED arguments
##                        side by side (comparison operator, then count), with
##                        fixture values chosen so a swapped or inverted read
##                        gives a DIFFERENT answer rather than an equal one
##   3. payload fields  - COMPARISON and INT are read from `integer`, not
##                        `text`; the operator is validated against the sourced
##                        table instead of letting compare_int guess false
##   4. flag folding    - PLAYER_ENABLE_BASE_CONSTRUCTION passes TRUE to the
##                        folded enable/disable world method
##   5. conditions      - true, false, and the third answer: a read the world
##                        cannot answer NEVER comes back as a plain false
##   6. arity and types - one wrong-arity and one wrong-type case per served
##                        member, rejected before the handler runs
##   7. can-build pair  - the base anchor and the player reach the world in
##                        signature order, the plain spelling passes the
##                        empty "anything at all" type, the typed spelling
##                        passes its authored type, and a world refusal is a
##                        structured gap (these two sat gap-registered until
##                        the world's signature grew the base slot)
##
## Every fixture value is SYNTHETIC. The call SHAPES are modelled on the retail
## AI libraries (an object-type-list name, a "<This Player>" player token, a
## faction token) because those shapes are what the handlers must survive, but
## no retail payload text is reproduced here.
##
## Invocation:
##   Godot_v4.7-stable_win64_console.exe --headless --path game \
##     -s res://tests/handlers_wp17_ai_players_runner.gd

const Registry := preload("res://src/script/handlers/_registry.gd")
const Dispatch := preload("res://src/script/script_dispatch.gd")
const CoreHandlers := preload("res://src/script/script_handlers_core.gd")
const Env := preload("res://src/script/script_env.gd")
const GapLog := preload("res://src/script/script_gaps.gd")
const ParamTypes := preload("res://src/script/script_param_types.gd")

## The 7 members this package serves. Written out rather than derived from
## the module, so that a member quietly disappearing from register() fails
## here instead of agreeing with itself.
const SERVED_ACTIONS := [
	"PLAYER_ENABLE_BASE_CONSTRUCTION",
	"PLAYER_SELL_EVERYTHING",
]

const SERVED_CONDITIONS := [
	"CAN_BUILD_AT_BASE",
	"CAN_BUILD_OBJECTTYPE_AT_BASE",
	"PLAYER_HAS_OBJECT_COMPARISON",
	"SKIRMISH_PLAYER_FACTION",
	"START_POSITION_IS",
]

## LIVENESS GUARD. A GDScript RUNTIME error aborts the enclosing function
## on the spot without propagating, so every later _check() in that function
## silently never runs and `failed` never moves - the runner then prints a
## zero-failure result and exits 0 with SCRIPT ERROR lines above it. This is
## the exact count a HEALTHY run makes; if the run makes any other number,
## something aborted (or an assertion was added without updating this) and
## the result is not to be trusted.
const EXPECTED_CHECKS := 49

var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	_test_registration_covers_the_ai_subset()
	_test_object_comparison_reads_operator_and_count_positionally()
	_test_object_comparison_counts_living_objects_of_the_named_list()
	_test_object_comparison_rejects_an_unknown_operator()
	_test_faction_comparison_is_exact()
	_test_start_position_is_read_as_an_integer()
	_test_an_unanswerable_condition_is_never_a_plain_false()
	_test_conditions_have_no_side_effects()
	_test_enable_base_construction_passes_the_enable_flag()
	_test_sell_everything_reaches_the_players_facet()
	_test_command_refusal_is_reported()
	_test_arity_is_enforced_per_member()
	_test_argument_type_codes_are_enforced_per_member()
	_test_can_build_pair_serves_player_then_base()
	var ran := passed + failed
	if ran != EXPECTED_CHECKS:
		failed += 1
		printerr("HANDLERS_WP17 FAIL liveness: ran %d checks, expected %d - a function aborted before its assertions" % [ran, EXPECTED_CHECKS])
	print("HANDLERS_WP17_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


# --- Fixture helpers ------------------------------------------------------


func _argument(
	argument_type: int, integer: int = 0, real: float = 0.0, text: String = ""
) -> Dictionary:
	return {"argumentType": argument_type, "integer": integer, "real": real, "text": text}


func _name_arg(value: String) -> Dictionary:
	# FACTION, OBJECT_TYPE and UNIT have no observed wire code in this repo, so
	# SageScriptArgs does not assert one. The integer field carries a DECOY: a
	# handler that read the wrong field would get 424242 instead of the name,
	# loudly rather than plausibly.
	return _argument(99, 424242, 0.0, value)


func _player_arg(value: String) -> Dictionary:
	return _argument(ParamTypes.ARGUMENT_PLAYER, 424242, 0.0, value)


func _int_arg(value: int, decoy_text: String = "DECOY_TEXT") -> Dictionary:
	return _argument(ParamTypes.ARGUMENT_INTEGER, value, 0.0, decoy_text)


func _comparison_arg(value: int, decoy_text: String = "DECOY_OPERATOR") -> Dictionary:
	# COMPARISON is one of the few parameter types this repo HAS observed a wire
	# code for (6), so SageScriptArgs asserts it. The decoy text is what a
	# wrong-field read would return.
	return _argument(ParamTypes.ARGUMENT_COMPARISON, value, 0.0, decoy_text)


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
	return {
		"dispatch": dispatch,
		"env": Env.new(),
		"world": PlayerWorld.new(20260726),
		"registration": outcome,
	}


func _act(harness: Dictionary, name: String, arguments: Array) -> int:
	return (harness["dispatch"] as SageScriptDispatch).execute_action(
		_record(name, arguments), harness["env"], harness["world"], "fixture"
	)


func _cond(harness: Dictionary, name: String, arguments: Array) -> bool:
	return (harness["dispatch"] as SageScriptDispatch).evaluate_condition(
		_record(name, arguments), harness["env"], harness["world"], "fixture"
	)


# --- 1. Registration ------------------------------------------------------


func _test_registration_covers_the_ai_subset() -> void:
	var harness := _harness()
	var dispatch: SageScriptDispatch = harness["dispatch"]
	var outcome: Dictionary = harness["registration"]

	_check(
		"registration_reports_no_errors",
		(outcome["errors"] as Array).is_empty(),
		str(outcome["errors"])
	)
	_check(
		"the_wp17_ai_package_is_present",
		(outcome["packages"] as Array).has("WP17-ai-players"),
		str(outcome["packages"])
	)

	var missing: Array[String] = []
	for name: String in SERVED_ACTIONS:
		if not dispatch.action_handlers.has(name):
			missing.append(name)
	for name: String in SERVED_CONDITIONS:
		if not dispatch.condition_handlers.has(name):
			missing.append(name)
	_check("every_served_member_is_registered", missing.is_empty(), str(missing))

	# 2 + 5 = 7, the exact AI-used membership the census records
	# (game/data/retail_ai_call_census.json, workPackage == "WP17-players").
	_check(
		"the_subset_is_seven_members",
		SERVED_ACTIONS.size() + SERVED_CONDITIONS.size() == 7,
		"served=%d conditions=%d" % [SERVED_ACTIONS.size(), SERVED_CONDITIONS.size()]
	)

	# The CAN_BUILD pair graduated from gap-registered to served when the
	# world's signature grew the base slot; nothing of this package may remain
	# on the blocked list.
	var still_blocked: Array[String] = []
	for name: String in SERVED_ACTIONS + SERVED_CONDITIONS:
		if dispatch.blocked_names().has(name):
			still_blocked.append(name)
	_check(
		"no_wp17_member_remains_on_the_blocked_list",
		still_blocked.is_empty(),
		str(still_blocked)
	)


# --- 2/3. PLAYER_HAS_OBJECT_COMPARISON ------------------------------------


func _test_object_comparison_reads_operator_and_count_positionally() -> void:
	# PLAYER_HAS_OBJECT_COMPARISON(PLAYER, COMPARISON, INT, OBJECT_TYPE).
	# Argument 1 is the OPERATOR, argument 2 is the COUNT, both integer-valued,
	# so no type- or field-search could recover the order.
	#
	# THE FIXTURE IS CHOSEN TO MAKE EVERY MIS-READ VISIBLE. World count = 3,
	# operator = COMPARE_GREATER (4), threshold = 2:
	#   * correct read:            3 >  2  -> true
	#   * indices 1/2 swapped:     operator becomes 2 (==), threshold becomes
	#                              4, and 3 == 4      -> false
	#   * comparison inverted
	#     (threshold op count):    2 >  3             -> false
	# A fixture like (==, equal values) would answer true under several of
	# those mutations; this one does not.
	var harness := _harness()
	var world: PlayerWorld = harness["world"]
	world.set_player_object_count("Player_1", "Synthetic_Economy_List", false, 3)

	_check(
		"greater_than_compares_the_world_count_against_the_authored_threshold",
		_cond(harness, "PLAYER_HAS_OBJECT_COMPARISON", [
			_player_arg("Player_1"), _comparison_arg(ParamTypes.COMPARE_GREATER),
			_int_arg(2), _name_arg("Synthetic_Economy_List")
		])
	)
	_check(
		"the_comparison_answers_false_when_the_count_fails_it",
		not _cond(harness, "PLAYER_HAS_OBJECT_COMPARISON", [
			_player_arg("Player_1"), _comparison_arg(ParamTypes.COMPARE_LESS),
			_int_arg(2), _name_arg("Synthetic_Economy_List")
		])
	)
	# The full operator table, against the same count of 3.
	_check(
		"every_sourced_operator_is_honoured",
		_cond(harness, "PLAYER_HAS_OBJECT_COMPARISON", [
			_player_arg("Player_1"), _comparison_arg(ParamTypes.COMPARE_LESS_EQUAL),
			_int_arg(3), _name_arg("Synthetic_Economy_List")
		])
		and _cond(harness, "PLAYER_HAS_OBJECT_COMPARISON", [
			_player_arg("Player_1"), _comparison_arg(ParamTypes.COMPARE_EQUAL),
			_int_arg(3), _name_arg("Synthetic_Economy_List")
		])
		and _cond(harness, "PLAYER_HAS_OBJECT_COMPARISON", [
			_player_arg("Player_1"), _comparison_arg(ParamTypes.COMPARE_GREATER_EQUAL),
			_int_arg(3), _name_arg("Synthetic_Economy_List")
		])
		and _cond(harness, "PLAYER_HAS_OBJECT_COMPARISON", [
			_player_arg("Player_1"), _comparison_arg(ParamTypes.COMPARE_NOT_EQUAL),
			_int_arg(4), _name_arg("Synthetic_Economy_List")
		])
	)


func _test_object_comparison_counts_living_objects_of_the_named_list() -> void:
	var harness := _harness()
	var world: PlayerWorld = harness["world"]

	# The stub keys its fixture on (player, list, include_dead). Only the
	# include_dead=FALSE key is taught: a handler that passed TRUE - or that
	# read the list name from the wrong slot - misses the fixture and the read
	# REFUSES, which this test would report as a wrong answer below.
	world.set_player_object_count("Player_1", "Synthetic_Fortress_List", false, 1)
	world.set_player_object_count("Player_1", "Synthetic_Fortress_List", true, 999)

	_check(
		"the_count_is_the_living_count_of_the_list_named_in_argument_three",
		_cond(harness, "PLAYER_HAS_OBJECT_COMPARISON", [
			_player_arg("Player_1"), _comparison_arg(ParamTypes.COMPARE_EQUAL),
			_int_arg(1), _name_arg("Synthetic_Fortress_List")
		])
		and not _cond(harness, "PLAYER_HAS_OBJECT_COMPARISON", [
			_player_arg("Player_1"), _comparison_arg(ParamTypes.COMPARE_EQUAL),
			_int_arg(999), _name_arg("Synthetic_Fortress_List")
		]),
		str((harness["dispatch"] as SageScriptDispatch).gaps.to_lines())
	)


func _test_object_comparison_rejects_an_unknown_operator() -> void:
	# ParamTypes.compare_int answers `false` for an operator it does not know.
	# Letting that through would turn "this handler does not understand the
	# operator" into a confident "no" at the most-called WP17 gate, so an
	# out-of-table operator must be BAD_ARGUMENTS, not a quiet false.
	var harness := _harness()
	var world: PlayerWorld = harness["world"]
	var dispatch: SageScriptDispatch = harness["dispatch"]
	world.set_player_object_count("Player_1", "Synthetic_Economy_List", false, 3)

	_check(
		"an_out_of_table_operator_reads_false_at_the_call_site",
		not _cond(harness, "PLAYER_HAS_OBJECT_COMPARISON", [
			_player_arg("Player_1"), _comparison_arg(6),
			_int_arg(3), _name_arg("Synthetic_Economy_List")
		])
	)
	_check(
		"an_out_of_table_operator_is_recorded_as_bad_arguments",
		dispatch.gaps.has(
			"condition", "PLAYER_HAS_OBJECT_COMPARISON", GapLog.REASON_BAD_ARGUMENTS
		),
		str(dispatch.gaps.to_lines())
	)


# --- SKIRMISH_PLAYER_FACTION ----------------------------------------------


func _test_faction_comparison_is_exact() -> void:
	# SKIRMISH_PLAYER_FACTION(PLAYER, FACTION). Exact string comparison, with
	# no case folding and no trimming: a mod's two distinct faction tokens must
	# stay distinct here exactly as they do in the retail engine.
	var harness := _harness()
	var world: PlayerWorld = harness["world"]
	world.set_player_scalar("Player_1", "faction", "SyntheticFactionWargs")

	_check(
		"the_matching_faction_reads_true",
		_cond(harness, "SKIRMISH_PLAYER_FACTION", [
			_player_arg("Player_1"), _name_arg("SyntheticFactionWargs")
		])
	)
	_check(
		"a_different_faction_reads_false",
		not _cond(harness, "SKIRMISH_PLAYER_FACTION", [
			_player_arg("Player_1"), _name_arg("SyntheticFactionEnts")
		])
	)
	_check(
		"faction_comparison_is_case_sensitive",
		not _cond(harness, "SKIRMISH_PLAYER_FACTION", [
			_player_arg("Player_1"), _name_arg("syntheticfactionwargs")
		])
	)


# --- START_POSITION_IS ----------------------------------------------------


func _test_start_position_is_read_as_an_integer() -> void:
	# START_POSITION_IS(PLAYER, INT). The index lives in the `integer` payload
	# field; the decoy text in the same slot is what a wrong-field read would
	# compare against. The authored index is compared EXACTLY - no 0/1-based
	# translation - per the handler's contract with players.start_position.
	var harness := _harness()
	var world: PlayerWorld = harness["world"]
	world.set_player_scalar("Player_1", "start_position", 3)

	_check(
		"the_matching_start_spot_reads_true",
		_cond(harness, "START_POSITION_IS", [_player_arg("Player_1"), _int_arg(3)])
	)
	_check(
		"a_different_start_spot_reads_false",
		not _cond(harness, "START_POSITION_IS", [_player_arg("Player_1"), _int_arg(2)])
	)


# --- 5. The third answer --------------------------------------------------


func _test_an_unanswerable_condition_is_never_a_plain_false() -> void:
	# A world that cannot answer must produce a REFUSAL - false at the call
	# site, but with a gap on the record. "This player is not Isengard" and "I
	# have no idea who this player is" are different answers, and only the
	# first may steer a faction-specific build order.
	var harness := _harness()
	var world: PlayerWorld = harness["world"]
	var dispatch: SageScriptDispatch = harness["dispatch"]
	world.expect_refusals()
	# "Player_9" has no fixtures, so every read about it refuses.

	_check(
		"an_unanswerable_object_count_refuses_instead_of_reading_as_zero",
		not _cond(harness, "PLAYER_HAS_OBJECT_COMPARISON", [
			_player_arg("Player_9"), _comparison_arg(ParamTypes.COMPARE_GREATER_EQUAL),
			_int_arg(0), _name_arg("Synthetic_Economy_List")
		])
		and dispatch.gaps.has(
			"condition", "PLAYER_HAS_OBJECT_COMPARISON", GapLog.REASON_WORLD_REFUSED
		),
		str(dispatch.gaps.to_lines())
	)
	# NOTE the fixture above asks ">= 0", which a count-defaulting-to-zero
	# implementation would answer TRUE: refusing it proves the zero was never
	# invented.

	_check(
		"an_unanswerable_faction_refuses",
		not _cond(harness, "SKIRMISH_PLAYER_FACTION", [
			_player_arg("Player_9"), _name_arg("SyntheticFactionWargs")
		])
		and dispatch.gaps.has(
			"condition", "SKIRMISH_PLAYER_FACTION", GapLog.REASON_WORLD_REFUSED
		),
		str(dispatch.gaps.to_lines())
	)
	_check(
		"an_unanswerable_start_position_refuses",
		not _cond(harness, "START_POSITION_IS", [_player_arg("Player_9"), _int_arg(1)])
		and dispatch.gaps.has("condition", "START_POSITION_IS", GapLog.REASON_WORLD_REFUSED),
		str(dispatch.gaps.to_lines())
	)
	_check(
		"the_refusals_name_the_missing_world_methods",
		world.refused("players.object_count_of_types")
		and world.refused("players.faction")
		and world.refused("players.start_position"),
		str(world.refusal_log)
	)


func _test_conditions_have_no_side_effects() -> void:
	# Conditions are evaluated an unpredictable number of times, so any
	# mutation is a determinism bug. None of the three may touch the env or
	# command the world.
	var harness := _harness()
	var world: PlayerWorld = harness["world"]
	var env: SageScriptEnv = harness["env"]
	world.set_player_object_count("Player_1", "Synthetic_Economy_List", false, 3)
	world.set_player_scalar("Player_1", "faction", "SyntheticFactionWargs")
	world.set_player_scalar("Player_1", "start_position", 3)

	for _repeat in range(3):
		_cond(harness, "PLAYER_HAS_OBJECT_COMPARISON", [
			_player_arg("Player_1"), _comparison_arg(ParamTypes.COMPARE_GREATER),
			_int_arg(1), _name_arg("Synthetic_Economy_List")
		])
		_cond(harness, "SKIRMISH_PLAYER_FACTION", [
			_player_arg("Player_1"), _name_arg("SyntheticFactionWargs")
		])
		_cond(harness, "START_POSITION_IS", [_player_arg("Player_1"), _int_arg(3)])

	_check(
		"conditions_issued_no_world_commands",
		world.calls.is_empty(),
		str(world.calls)
	)
	_check(
		"conditions_left_the_script_environment_alone",
		env.snapshot()["counters"] == {} and env.snapshot()["flags"] == {}
	)


# --- 4/6. Actions ---------------------------------------------------------


func _test_enable_base_construction_passes_the_enable_flag() -> void:
	# The world folds PLAYER_ENABLE_BASE_CONSTRUCTION and its DISABLE sibling
	# into one method with a flag. This is the ENABLE spelling, so the flag is
	# TRUE; getting it backwards would freeze the AI's base building with a
	# handler that still returned OK.
	var harness := _harness()
	var world: PlayerWorld = harness["world"]
	var status := _act(harness, "PLAYER_ENABLE_BASE_CONSTRUCTION", [_player_arg("Player_1")])
	_check(
		"enable_base_construction_passes_true_to_the_folded_method",
		status == Dispatch.Status.OK
		and world.calls.has("players.set_base_construction_enabled|Player_1|true"),
		"status=%d calls=%s" % [status, str(world.calls)]
	)


func _test_sell_everything_reaches_the_players_facet() -> void:
	var harness := _harness()
	var world: PlayerWorld = harness["world"]
	var status := _act(harness, "PLAYER_SELL_EVERYTHING", [_player_arg("Player_1")])
	_check(
		"sell_everything_names_the_player_from_argument_zero",
		status == Dispatch.Status.OK and world.calls.has("players.sell_everything|Player_1"),
		"status=%d calls=%s" % [status, str(world.calls)]
	)


func _test_command_refusal_is_reported() -> void:
	# A world that does not model these commands must produce a structured gap
	# per action, never a quiet success. The BASE SageScriptWorld is exactly
	# that world: every Players command inherits the refusing base.
	var dispatch := Dispatch.new()
	CoreHandlers.register_all(dispatch)
	Registry.register_all(dispatch)
	var env := Env.new()

	var enable_status := dispatch.execute_action(
		_record("PLAYER_ENABLE_BASE_CONSTRUCTION", [_player_arg("Player_1")]),
		env, SageScriptWorld.new(), "fixture"
	)
	_check(
		"a_world_without_the_command_refuses_rather_than_pretending",
		enable_status == Dispatch.Status.WORLD_REFUSED,
		"status=%d" % enable_status
	)
	_check(
		"the_enable_refusal_names_the_missing_facet_method",
		dispatch.gaps.has(
			"action", "PLAYER_ENABLE_BASE_CONSTRUCTION", GapLog.REASON_WORLD_REFUSED
		)
		and String(
			dispatch.gaps.entries[
				"action|PLAYER_ENABLE_BASE_CONSTRUCTION|%s" % GapLog.REASON_WORLD_REFUSED
			]["detail"]
		).contains("players.set_base_construction_enabled"),
		str(dispatch.gaps.to_lines())
	)

	var sell_status := dispatch.execute_action(
		_record("PLAYER_SELL_EVERYTHING", [_player_arg("Player_1")]),
		env, SageScriptWorld.new(), "fixture"
	)
	_check(
		"a_refused_sell_everything_is_world_refused_not_ok",
		sell_status == Dispatch.Status.WORLD_REFUSED,
		"status=%d" % sell_status
	)
	_check(
		"the_sell_refusal_names_the_missing_facet_method",
		dispatch.gaps.has("action", "PLAYER_SELL_EVERYTHING", GapLog.REASON_WORLD_REFUSED)
		and String(
			dispatch.gaps.entries[
				"action|PLAYER_SELL_EVERYTHING|%s" % GapLog.REASON_WORLD_REFUSED
			]["detail"]
		).contains("players.sell_everything"),
		str(dispatch.gaps.to_lines())
	)


# --- 6. Arity and argument coding, per served member ----------------------


func _test_arity_is_enforced_per_member() -> void:
	# Arity is checked against the sourced signature before the handler runs,
	# so a positional read can never quietly slide onto a neighbouring
	# argument. One wrong-arity case per served member.
	var harness := _harness()
	var world: PlayerWorld = harness["world"]
	world.set_player_object_count("Player_1", "Synthetic_Economy_List", false, 3)
	world.set_player_scalar("Player_1", "faction", "SyntheticFactionWargs")
	world.set_player_scalar("Player_1", "start_position", 3)

	_check(
		"a_three_argument_object_comparison_is_rejected",
		not _cond(harness, "PLAYER_HAS_OBJECT_COMPARISON", [
			_player_arg("Player_1"), _comparison_arg(ParamTypes.COMPARE_GREATER),
			_int_arg(1)
		])
		and (harness["dispatch"] as SageScriptDispatch).gaps.has(
			"condition", "PLAYER_HAS_OBJECT_COMPARISON", GapLog.REASON_BAD_ARGUMENTS
		),
		str((harness["dispatch"] as SageScriptDispatch).gaps.to_lines())
	)
	_check(
		"a_one_argument_faction_test_is_rejected",
		not _cond(harness, "SKIRMISH_PLAYER_FACTION", [_player_arg("Player_1")])
		and (harness["dispatch"] as SageScriptDispatch).gaps.has(
			"condition", "SKIRMISH_PLAYER_FACTION", GapLog.REASON_BAD_ARGUMENTS
		),
		str((harness["dispatch"] as SageScriptDispatch).gaps.to_lines())
	)
	_check(
		"a_three_argument_start_position_test_is_rejected",
		not _cond(harness, "START_POSITION_IS", [
			_player_arg("Player_1"), _int_arg(3), _int_arg(3)
		])
		and (harness["dispatch"] as SageScriptDispatch).gaps.has(
			"condition", "START_POSITION_IS", GapLog.REASON_BAD_ARGUMENTS
		),
		str((harness["dispatch"] as SageScriptDispatch).gaps.to_lines())
	)
	_check(
		"a_two_argument_enable_base_construction_is_rejected",
		_act(harness, "PLAYER_ENABLE_BASE_CONSTRUCTION", [
			_player_arg("Player_1"), _player_arg("Player_2")
		]) == Dispatch.Status.BAD_ARGUMENTS
	)
	_check(
		"a_zero_argument_sell_everything_is_rejected",
		_act(harness, "PLAYER_SELL_EVERYTHING", []) == Dispatch.Status.BAD_ARGUMENTS
	)
	_check(
		"no_rejected_call_reached_the_world",
		world.calls.is_empty(),
		str(world.calls)
	)


func _test_argument_type_codes_are_enforced_per_member() -> void:
	# PLAYER (11), COMPARISON (6) and INT (0) have OBSERVED wire codes, so
	# SageScriptArgs asserts them. One wrong-type case per served member: an
	# argument carrying the wrong code in an asserted slot is BAD_ARGUMENTS
	# before the handler runs.
	var harness := _harness()
	var world: PlayerWorld = harness["world"]
	world.set_player_object_count("Player_1", "Synthetic_Economy_List", false, 3)
	world.set_player_scalar("Player_1", "faction", "SyntheticFactionWargs")
	world.set_player_scalar("Player_1", "start_position", 3)

	_check(
		"a_miscoded_comparison_slot_is_rejected",
		not _cond(harness, "PLAYER_HAS_OBJECT_COMPARISON", [
			_player_arg("Player_1"), _name_arg("DECOY"),
			_int_arg(1), _name_arg("Synthetic_Economy_List")
		])
		and (harness["dispatch"] as SageScriptDispatch).gaps.has(
			"condition", "PLAYER_HAS_OBJECT_COMPARISON", GapLog.REASON_BAD_ARGUMENTS
		),
		str((harness["dispatch"] as SageScriptDispatch).gaps.to_lines())
	)
	_check(
		"a_miscoded_faction_player_slot_is_rejected",
		not _cond(harness, "SKIRMISH_PLAYER_FACTION", [
			_name_arg("Player_1"), _name_arg("SyntheticFactionWargs")
		])
	)
	_check(
		"a_miscoded_start_position_int_slot_is_rejected",
		not _cond(harness, "START_POSITION_IS", [
			_player_arg("Player_1"), _name_arg("3")
		])
	)
	_check(
		"a_miscoded_enable_base_construction_player_slot_is_rejected",
		_act(harness, "PLAYER_ENABLE_BASE_CONSTRUCTION", [_name_arg("Player_1")])
		== Dispatch.Status.BAD_ARGUMENTS
	)
	_check(
		"a_miscoded_sell_everything_player_slot_is_rejected",
		_act(harness, "PLAYER_SELL_EVERYTHING", [_int_arg(1)])
		== Dispatch.Status.BAD_ARGUMENTS
	)
	_check(
		"no_miscoded_call_reached_the_world",
		world.calls.is_empty(),
		str(world.calls)
	)


# --- 7. The can-build pair -------------------------------------------------


func _test_can_build_pair_serves_player_then_base() -> void:
	# CAN_BUILD_AT_BASE(PLAYER, UNIT) / CAN_BUILD_OBJECTTYPE_AT_BASE(PLAYER,
	# UNIT, OBJECT_TYPE): the player FIRST, the base anchor SECOND, the type
	# (when authored) THIRD. The stub keys its fixture on the FULL
	# (player, base, type) triple in signature order, so a swapped read - the
	# base as the player, or the type as the base - misses the fixture and
	# surfaces as a refusal instead of a quiet pass. These two sat
	# gap-registered until the world's signature grew the base slot; the base
	# anchor is what makes the question per-base instead of base-blind.
	var harness := _harness()
	var world: PlayerWorld = harness["world"]
	var dispatch: SageScriptDispatch = harness["dispatch"]
	world.can_build["Player_1|SYNTHETIC_BASE_FRONT|"] = true
	world.can_build["Player_1|SYNTHETIC_BASE_REAR|"] = false
	world.can_build["Player_1|SYNTHETIC_BASE_FRONT|Synthetic_Barracks_Type"] = false

	_check(
		"the_plain_spelling_passes_player_base_and_the_empty_anything_type",
		_cond(harness, "CAN_BUILD_AT_BASE", [
			_player_arg("Player_1"), _name_arg("SYNTHETIC_BASE_FRONT")
		]),
		str(world.reads)
	)
	# THE ANSWER IS PER BASE - the same player, a different base, a different
	# answer, with no gap recorded for either (both are real answers).
	var before := dispatch.gaps.entries.size()
	_check(
		"a_full_base_reads_false_for_the_same_player",
		not _cond(harness, "CAN_BUILD_AT_BASE", [
			_player_arg("Player_1"), _name_arg("SYNTHETIC_BASE_REAR")
		])
	)
	_check(
		"an_answered_false_records_no_gap",
		dispatch.gaps.entries.size() == before,
		str(dispatch.gaps.to_lines())
	)
	_check(
		"the_typed_spelling_passes_its_authored_type_in_slot_three",
		not _cond(harness, "CAN_BUILD_OBJECTTYPE_AT_BASE", [
			_player_arg("Player_1"), _name_arg("SYNTHETIC_BASE_FRONT"),
			_name_arg("Synthetic_Barracks_Type")
		]),
		str(world.reads)
	)
	_check(
		"both_spellings_reached_the_world_with_their_triples_in_order",
		world.reads.has("players.can_build_at_base|Player_1|SYNTHETIC_BASE_FRONT|")
		and world.reads.has("players.can_build_at_base|Player_1|SYNTHETIC_BASE_REAR|")
		and world.reads.has(
			"players.can_build_at_base|Player_1|SYNTHETIC_BASE_FRONT|Synthetic_Barracks_Type"
		),
		str(world.reads)
	)

	# The third answer: a base the world cannot see refuses - false at the
	# call site WITH a structured gap - and the polled pair mutates nothing.
	world.expect_refusals()
	_check(
		"an_unanswerable_base_reads_false_with_a_world_refused_gap",
		not _cond(harness, "CAN_BUILD_AT_BASE", [
			_player_arg("Player_1"), _name_arg("SYNTHETIC_BASE_NOWHERE")
		])
		and dispatch.gaps.has(
			"condition", "CAN_BUILD_AT_BASE", GapLog.REASON_WORLD_REFUSED
		),
		str(dispatch.gaps.to_lines())
	)
	_check(
		"the_refusal_names_the_world_method",
		world.refused("players.can_build_at_base"),
		str(world.refusal_log)
	)
	_check(
		"the_polled_pair_issued_no_world_commands",
		world.calls.is_empty(),
		str(world.calls)
	)
	_check(
		"a_short_can_build_argument_list_is_rejected",
		not _cond(harness, "CAN_BUILD_AT_BASE", [_player_arg("Player_1")])
		and dispatch.gaps.has(
			"condition", "CAN_BUILD_AT_BASE", GapLog.REASON_BAD_ARGUMENTS
		),
		str(dispatch.gaps.to_lines())
	)


# --- Stub world -----------------------------------------------------------
#
# SageScriptWorldStub already teaches the Players facet the WP01 reads,
# including object_count_of_types, which this package's biggest member needs -
# so the stub's fixture-or-refuse behaviour is inherited rather than
# re-taught. What the shared stub does NOT teach - faction, start_position,
# and this package's two commands - is taught HERE rather than in the shared
# stub, because the shared stub is an existing file that ~17 concurrent
# packages also depend on. Every command is recorded as a flat string so a
# test asserts on the exact arguments that reached the world, including the
# folded boolean; every read answers from a fixture or REFUSES, so a test
# that forgot to set one gets a loud refusal rather than a plausible zero.


class PlayerWorld:
	extends SageScriptWorldStub

	var calls: Array[String] = []
	var reads: Array[String] = []

	## "player|base|object_type" -> bool, keyed on the triple IN SIGNATURE
	## ORDER so that a handler that rotated the arguments misses the fixture
	## and refuses.
	var can_build: Dictionary = {}

	func _make_players() -> SageScriptWorld.Players:
		return AiStubPlayers.new()


class AiStubPlayers:
	extends SageScriptWorldStub.StubPlayers

	func _log(text: String) -> bool:
		(world as PlayerWorld).calls.append(text)
		return true

	func faction(player: String) -> SageWorldQuery:
		return _scalar(player, "faction")

	func start_position(player: String) -> SageWorldQuery:
		return _scalar(player, "start_position")

	func can_build_at_base(player: String, base: String, object_type: String) -> SageWorldQuery:
		var stub := world as PlayerWorld
		var key := "%s|%s|%s" % [player, base, object_type]
		stub.reads.append("players.can_build_at_base|%s" % key)
		if not stub.can_build.has(key):
			return _refuse_query("players.can_build_at_base", "no fixture for '%s'" % key)
		return SageWorldQuery.hit(bool(stub.can_build[key]))

	func set_base_construction_enabled(player: String, enabled: bool) -> bool:
		return _log(
			"players.set_base_construction_enabled|%s|%s" % [player, str(enabled).to_lower()]
		)

	func sell_everything(player: String) -> bool:
		return _log("players.sell_everything|%s" % player)


# --- Reporting ------------------------------------------------------------


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("HANDLERS_WP17 PASS %s%s" % [name, " (%s)" % detail if detail != "" else ""])
	else:
		failed += 1
		printerr("HANDLERS_WP17 FAIL %s%s" % [name, " (%s)" % detail if detail != "" else ""])
