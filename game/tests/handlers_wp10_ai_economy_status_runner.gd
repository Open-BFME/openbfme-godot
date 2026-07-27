extends SceneTree

## Proof runner for WP10-ai-economy-objectstatus, the AI-critical subset
## (wp10_ai_economy_status.gd).
##
## Covers, in order:
##   1. registration    - both AI-called members registered, nothing blocked,
##                        no overlap and no silent absence
##   2. status action   - team/status/polarity read positionally; the polarity
##                        is delivered BOTH ways (set and clear), never dropped
##   3. credits         - THE OPERAND-DIRECTION PIN: the authored INT is the
##                        LEFT operand and the live credit count the RIGHT one,
##                        proven with unequal values under asymmetric operators
##                        so the reversed read gives a DIFFERENT answer
##   4. conditions      - true, false, and the third answer: a read the world
##                        cannot answer NEVER comes back as a plain false, and
##                        the polled condition has no side effects
##   5. refusal + arity - a world without the facets refuses per method; wrong
##                        arity and wrong-coded arguments are rejected before
##                        any handler runs
##
## Every fixture value is SYNTHETIC. The call SHAPES are modelled on the retail
## AI libraries (a credit threshold under <=, a set-then-clear status pair)
## because those shapes are what the handlers must survive, but no retail
## payload text is reproduced here.
##
## Invocation:
##   Godot_v4.7-stable_win64_console.exe --headless --path game \
##     -s res://tests/handlers_wp10_ai_economy_status_runner.gd

const Registry := preload("res://src/script/handlers/_registry.gd")
const Dispatch := preload("res://src/script/script_dispatch.gd")
const CoreHandlers := preload("res://src/script/script_handlers_core.gd")
const Env := preload("res://src/script/script_env.gd")
const GapLog := preload("res://src/script/script_gaps.gd")
const ParamTypes := preload("res://src/script/script_param_types.gd")

## The 2 members this package serves. Written out rather than derived from the
## module, so that a member quietly disappearing from register() fails here
## instead of agreeing with itself.
const SERVED_ACTIONS := [
	"TEAM_CHANGE_OBJECT_STATUS",
]

const SERVED_CONDITIONS := [
	"PLAYER_HAS_CREDITS",
]

var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	_test_registration_covers_the_ai_subset()
	_test_status_change_reads_team_status_and_polarity_positionally()
	_test_status_change_delivers_both_polarities()
	_test_credits_direction_authored_int_is_the_left_operand()
	_test_credits_answers_truthfully()
	_test_credits_rejects_an_unknown_operator()
	_test_an_unanswerable_credits_read_is_never_a_plain_false()
	_test_credits_condition_has_no_side_effects()
	_test_command_refusal_is_reported()
	_test_arity_is_enforced()
	_test_argument_coding_is_enforced_where_observed()
	print("HANDLERS_WP10_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


# --- Fixture helpers ------------------------------------------------------


func _argument(
	argument_type: int, integer: int = 0, real: float = 0.0, text: String = ""
) -> Dictionary:
	return {"argumentType": argument_type, "integer": integer, "real": real, "text": text}


func _name_arg(value: String) -> Dictionary:
	# TEAM and OBJECT_STATUS have no observed wire code in this repo, so
	# SageScriptArgs does not assert one. The integer field carries a DECOY: a
	# handler that read the wrong field would get 424242 instead of the name,
	# loudly rather than plausibly.
	return _argument(99, 424242, 0.0, value)


func _player_arg(value: String) -> Dictionary:
	return _argument(ParamTypes.ARGUMENT_PLAYER, 424242, 0.0, value)


func _int_arg(value: int, decoy_text: String = "DECOY_TEXT") -> Dictionary:
	return _argument(ParamTypes.ARGUMENT_INTEGER, value, 0.0, decoy_text)


func _comparison_arg(value: int, decoy_text: String = "DECOY_OPERATOR") -> Dictionary:
	return _argument(ParamTypes.ARGUMENT_COMPARISON, value, 0.0, decoy_text)


func _boolean_arg(value: bool, decoy_text: String = "DECOY_NOT_A_BOOL") -> Dictionary:
	# BOOLEAN is integer-carried ({"false": 0, "true": 1}) with observed wire
	# code ARGUMENT_BOOLEAN. The decoy text is what a wrong-field read would
	# deliver.
	return _argument(ParamTypes.ARGUMENT_BOOLEAN, 1 if value else 0, 0.0, decoy_text)


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
		"world": EconWorld.new(20260726),
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
		"the_wp10_subset_package_is_present",
		(outcome["packages"] as Array).has("WP10-ai-economy-objectstatus"),
		str(outcome["packages"])
	)
	_check(
		"the_catalog_package_name_remains_free_for_the_completing_package",
		not (outcome["packages"] as Array).has("WP10-economy-objectstatus-objectlists"),
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

	# 1 + 1 = 2, the exact AI-called WP10 membership
	# (game/data/retail_ai_call_census.json). This package registers no gaps,
	# so both members must land in the coverage-counted sets and NEITHER may
	# appear among the blocked names.
	_check(
		"the_subset_is_two_members",
		SERVED_ACTIONS.size() + SERVED_CONDITIONS.size() == 2,
		"served=%d conditions=%d" % [SERVED_ACTIONS.size(), SERVED_CONDITIONS.size()]
	)
	var wrongly_blocked: Array[String] = []
	for name: String in SERVED_ACTIONS + SERVED_CONDITIONS:
		if dispatch.blocked_names().has(name):
			wrongly_blocked.append(name)
	_check(
		"no_served_member_is_blocked",
		wrongly_blocked.is_empty(),
		str(wrongly_blocked)
	)
	_check(
		"both_members_count_as_coverage",
		dispatch.implemented_actions().has("TEAM_CHANGE_OBJECT_STATUS")
		and dispatch.implemented_conditions().has("PLAYER_HAS_CREDITS")
	)


# --- 2. Object status -----------------------------------------------------


func _test_status_change_reads_team_status_and_polarity_positionally() -> void:
	var harness := _harness()
	var world: EconWorld = harness["world"]

	# TEAM_CHANGE_OBJECT_STATUS(TEAM, OBJECT_STATUS, BOOLEAN): team FIRST,
	# status name SECOND - both text, so nothing but position separates them -
	# and the polarity LAST, read from the integer field (the decoy text is
	# what a wrong-field read would send).
	var status := _act(harness, "TEAM_CHANGE_OBJECT_STATUS", [
		_name_arg("Synthetic Siege Line"),
		_name_arg("SYNTH_STATUS_UNSELECTABLE"),
		_boolean_arg(true),
	])
	_check(
		"the_status_change_is_served",
		status == Dispatch.Status.OK,
		"status=%d" % status
	)
	_check(
		"team_then_status_then_polarity_reach_the_units_facet_on_team_scope",
		world.calls.has(
			"units.set_object_status|%d|Synthetic Siege Line|SYNTH_STATUS_UNSELECTABLE|true"
			% SageScriptWorld.Scope.TEAM
		),
		str(world.calls)
	)
	_check(
		"team_scope_is_distinct_from_unit_and_player_scope",
		SageScriptWorld.Scope.TEAM != SageScriptWorld.Scope.UNIT
		and SageScriptWorld.Scope.TEAM != SageScriptWorld.Scope.PLAYER
	)


func _test_status_change_delivers_both_polarities() -> void:
	# THE POLARITY IS LOAD-BEARING. Retail authors set-then-clear pairs; a
	# handler that hardcoded either value would invert the authored state on
	# every opposite-polarity site. Both directions must arrive verbatim.
	var harness := _harness()
	var world: EconWorld = harness["world"]

	_act(harness, "TEAM_CHANGE_OBJECT_STATUS", [
		_name_arg("Synthetic Siege Line"), _name_arg("SYNTH_STATUS_X"), _boolean_arg(true),
	])
	_act(harness, "TEAM_CHANGE_OBJECT_STATUS", [
		_name_arg("Synthetic Siege Line"), _name_arg("SYNTH_STATUS_X"), _boolean_arg(false),
	])
	_check(
		"a_true_polarity_arrives_as_set",
		world.calls.has(
			"units.set_object_status|%d|Synthetic Siege Line|SYNTH_STATUS_X|true"
			% SageScriptWorld.Scope.TEAM
		),
		str(world.calls)
	)
	_check(
		"a_false_polarity_arrives_as_clear_not_as_a_second_set",
		world.calls.has(
			"units.set_object_status|%d|Synthetic Siege Line|SYNTH_STATUS_X|false"
			% SageScriptWorld.Scope.TEAM
		),
		str(world.calls)
	)


# --- 3. Credits: the operand-direction pin --------------------------------


func _test_credits_direction_authored_int_is_the_left_operand() -> void:
	# PLAYER_HAS_CREDITS(INT, COMPARISON, PLAYER) reads, per the engine's own
	# template: "<INT> is <COMPARISON> the number of credits possessed by
	# <PLAYER>". The authored INT is the LEFT operand. Fixture: 700 credits.
	#
	#   authored "900 > credits"  -> 900 > 700  -> TRUE.
	#   reversed (credits > 900)  -> 700 > 900  -> false: a handler that
	#     normalised the direction to PLAYER_HAS_OBJECT_COMPARISON's would
	#     answer differently, not accidentally equally.
	#   index swap (integer(1) as the value, integer(0) as the operator) reads
	#     operator 900, outside the 0..5 table -> BAD_ARGUMENTS, also distinct.
	var harness := _harness()
	var world: EconWorld = harness["world"]
	world.money["SYNTH_PLAYER_1"] = 700

	_check(
		"nine_hundred_is_greater_than_seven_hundred_credits",
		_cond(harness, "PLAYER_HAS_CREDITS", [
			_int_arg(900), _comparison_arg(ParamTypes.COMPARE_GREATER),
			_player_arg("SYNTH_PLAYER_1"),
		])
	)
	_check(
		"five_hundred_is_not_greater_than_seven_hundred_credits",
		not _cond(harness, "PLAYER_HAS_CREDITS", [
			_int_arg(500), _comparison_arg(ParamTypes.COMPARE_GREATER),
			_player_arg("SYNTH_PLAYER_1"),
		])
	)
	# The affordability shape retail authors: "threshold <= credits".
	_check(
		"an_affordable_threshold_reads_true_under_less_equal",
		_cond(harness, "PLAYER_HAS_CREDITS", [
			_int_arg(500), _comparison_arg(ParamTypes.COMPARE_LESS_EQUAL),
			_player_arg("SYNTH_PLAYER_1"),
		])
	)
	_check(
		"an_unaffordable_threshold_reads_false_under_less_equal",
		not _cond(harness, "PLAYER_HAS_CREDITS", [
			_int_arg(900), _comparison_arg(ParamTypes.COMPARE_LESS_EQUAL),
			_player_arg("SYNTH_PLAYER_1"),
		])
	)


func _test_credits_answers_truthfully() -> void:
	var harness := _harness()
	var world: EconWorld = harness["world"]
	world.money["SYNTH_PLAYER_1"] = 300

	_check(
		"an_exact_credit_count_reads_true_under_equal",
		_cond(harness, "PLAYER_HAS_CREDITS", [
			_int_arg(300), _comparison_arg(ParamTypes.COMPARE_EQUAL),
			_player_arg("SYNTH_PLAYER_1"),
		])
	)
	# The world ANSWERED false here - this is a real "no", not a refusal, and
	# it must arrive without a gap.
	var before := (harness["dispatch"] as SageScriptDispatch).gaps.entries.size()
	_check(
		"a_missed_credit_count_reads_false_under_equal",
		not _cond(harness, "PLAYER_HAS_CREDITS", [
			_int_arg(299), _comparison_arg(ParamTypes.COMPARE_EQUAL),
			_player_arg("SYNTH_PLAYER_1"),
		])
	)
	_check(
		"an_answered_false_records_no_gap",
		(harness["dispatch"] as SageScriptDispatch).gaps.entries.size() == before,
		str((harness["dispatch"] as SageScriptDispatch).gaps.to_lines())
	)


func _test_credits_rejects_an_unknown_operator() -> void:
	# ParamTypes.compare_int answers `false` for any operator it does not
	# know, which would turn a mis-read operator into a confident "no" at an
	# AI spending gate. Out-of-table operators must therefore be refused, with
	# a bad-arguments gap, before any comparison happens.
	var harness := _harness()
	var world: EconWorld = harness["world"]
	world.money["SYNTH_PLAYER_1"] = 700

	_check(
		"an_out_of_table_operator_evaluates_false",
		not _cond(harness, "PLAYER_HAS_CREDITS", [
			_int_arg(500), _comparison_arg(6), _player_arg("SYNTH_PLAYER_1"),
		])
	)
	_check(
		"an_out_of_table_operator_records_a_bad_arguments_gap",
		(harness["dispatch"] as SageScriptDispatch).gaps.has(
			"condition", "PLAYER_HAS_CREDITS", GapLog.REASON_BAD_ARGUMENTS
		),
		str((harness["dispatch"] as SageScriptDispatch).gaps.to_lines())
	)


# --- 4. The third answer, and side effects --------------------------------


func _test_an_unanswerable_credits_read_is_never_a_plain_false() -> void:
	# "500 is not <= this player's credits" stalls one purchase; "the world
	# does not model money" must keep the gate shut WITH a gap on the record.
	# Both read false at the call site and differ only in the gap - which is
	# the difference.
	var harness := _harness()
	var world: EconWorld = harness["world"]
	var dispatch: SageScriptDispatch = harness["dispatch"]
	world.refuse(SageScriptWorld.CAP_PLAYER_MONEY)
	world.expect_refusals()

	_check(
		"an_unanswerable_credits_read_evaluates_false",
		not _cond(harness, "PLAYER_HAS_CREDITS", [
			_int_arg(500), _comparison_arg(ParamTypes.COMPARE_LESS_EQUAL),
			_player_arg("SYNTH_PLAYER_1"),
		])
	)
	_check(
		"an_unanswerable_credits_read_records_a_world_refused_gap",
		dispatch.gaps.has(
			"condition", "PLAYER_HAS_CREDITS", GapLog.REASON_WORLD_REFUSED
		),
		str(dispatch.gaps.to_lines())
	)
	_check(
		"the_refusal_names_the_missing_world_method",
		world.refused("economy.money"),
		str(world.refusal_log)
	)


func _test_credits_condition_has_no_side_effects() -> void:
	# This condition is polled by the AI economy loop an unpredictable number
	# of times per spending decision, so any mutation is a determinism bug. It
	# may neither command the world nor touch the env.
	var harness := _harness()
	var world: EconWorld = harness["world"]
	var env: SageScriptEnv = harness["env"]
	world.money["SYNTH_PLAYER_1"] = 700

	for _repeat in range(3):
		_cond(harness, "PLAYER_HAS_CREDITS", [
			_int_arg(500), _comparison_arg(ParamTypes.COMPARE_LESS_EQUAL),
			_player_arg("SYNTH_PLAYER_1"),
		])

	_check(
		"the_condition_issued_no_world_commands",
		world.calls.is_empty(),
		str(world.calls)
	)
	_check(
		"the_condition_left_the_players_money_alone",
		int(world.money["SYNTH_PLAYER_1"]) == 700,
		str(world.money)
	)
	_check(
		"the_condition_left_the_script_environment_alone",
		env.snapshot()["counters"] == {} and env.snapshot()["flags"] == {}
	)


# --- 5. Command refusal, arity, and argument coding -----------------------


func _test_command_refusal_is_reported() -> void:
	# A world that models neither object status nor money must produce a
	# structured gap per member, never a quiet success. The BASE
	# SageScriptWorld is exactly that world.
	var dispatch := Dispatch.new()
	CoreHandlers.register_all(dispatch)
	Registry.register_all(dispatch)
	var env := Env.new()
	var bare := SageScriptWorld.new()

	var status := dispatch.execute_action(
		_record("TEAM_CHANGE_OBJECT_STATUS", [
			_name_arg("Synthetic Siege Line"), _name_arg("SYNTH_STATUS_X"),
			_boolean_arg(true),
		]),
		env, bare, "fixture"
	)
	_check(
		"a_world_without_a_units_facet_refuses_rather_than_pretending",
		status == Dispatch.Status.WORLD_REFUSED,
		"status=%d" % status
	)
	_check(
		"the_status_refusal_names_the_missing_facet_method",
		dispatch.gaps.has("action", "TEAM_CHANGE_OBJECT_STATUS", GapLog.REASON_WORLD_REFUSED)
		and String(
			dispatch.gaps.entries[
				"action|TEAM_CHANGE_OBJECT_STATUS|%s" % GapLog.REASON_WORLD_REFUSED
			]["detail"]
		).contains("units.set_object_status"),
		str(dispatch.gaps.to_lines())
	)


func _test_arity_is_enforced() -> void:
	# Arity is checked against the sourced signature before the handler runs,
	# so a positional read can never quietly slide onto a neighbouring
	# argument. One short and one long case per served member.
	var harness := _harness()
	_check(
		"a_short_status_change_argument_list_is_rejected",
		_act(harness, "TEAM_CHANGE_OBJECT_STATUS", [
			_name_arg("Only Team"), _name_arg("SYNTH_STATUS_X")
		]) == Dispatch.Status.BAD_ARGUMENTS
	)
	_check(
		"a_long_status_change_argument_list_is_rejected",
		_act(harness, "TEAM_CHANGE_OBJECT_STATUS", [
			_name_arg("A"), _name_arg("B"), _boolean_arg(true), _boolean_arg(false)
		]) == Dispatch.Status.BAD_ARGUMENTS
	)
	_check(
		"a_short_credits_argument_list_is_rejected_and_the_gate_stays_shut",
		not _cond(harness, "PLAYER_HAS_CREDITS", [
			_int_arg(500), _comparison_arg(ParamTypes.COMPARE_LESS_EQUAL)
		])
		and (harness["dispatch"] as SageScriptDispatch).gaps.has(
			"condition", "PLAYER_HAS_CREDITS", GapLog.REASON_BAD_ARGUMENTS
		),
		str((harness["dispatch"] as SageScriptDispatch).gaps.to_lines())
	)
	_check(
		"a_long_credits_argument_list_is_rejected",
		not _cond(harness, "PLAYER_HAS_CREDITS", [
			_int_arg(500), _comparison_arg(ParamTypes.COMPARE_LESS_EQUAL),
			_player_arg("SYNTH_PLAYER_1"), _int_arg(7)
		])
	)


func _test_argument_coding_is_enforced_where_observed() -> void:
	# SageScriptArgs asserts argument-type CODES only where this repo has
	# observed one (INT, COMPARISON, BOOLEAN and PLAYER here). For those slots
	# a mis-coded record is rejected before the handler runs. TEAM and
	# OBJECT_STATUS have no observed code, so validation deliberately declines
	# to assert on them - which is exactly why every handler read is positional
	# and every fixture carries a decoy in the unread field.
	var harness := _harness()
	var world: EconWorld = harness["world"]
	world.money["SYNTH_PLAYER_1"] = 700

	# The BOOLEAN slot of the status action, mis-coded as a generic name
	# argument (code 99, expected ARGUMENT_BOOLEAN).
	_check(
		"a_mis_coded_polarity_is_rejected_before_dispatch",
		_act(harness, "TEAM_CHANGE_OBJECT_STATUS", [
			_name_arg("Synthetic Siege Line"), _name_arg("SYNTH_STATUS_X"),
			_name_arg("NOT_A_BOOL"),
		]) == Dispatch.Status.BAD_ARGUMENTS
	)

	# The INT, COMPARISON and PLAYER slots of the condition, each mis-coded
	# the same way in turn.
	_check(
		"a_mis_coded_credit_threshold_is_rejected_before_dispatch",
		not _cond(harness, "PLAYER_HAS_CREDITS", [
			_name_arg("NOT_AN_INT"), _comparison_arg(ParamTypes.COMPARE_LESS_EQUAL),
			_player_arg("SYNTH_PLAYER_1"),
		])
		and (harness["dispatch"] as SageScriptDispatch).gaps.has(
			"condition", "PLAYER_HAS_CREDITS", GapLog.REASON_BAD_ARGUMENTS
		)
	)
	_check(
		"a_mis_coded_comparison_slot_is_rejected_before_dispatch",
		not _cond(harness, "PLAYER_HAS_CREDITS", [
			_int_arg(500), _int_arg(ParamTypes.COMPARE_LESS_EQUAL),
			_player_arg("SYNTH_PLAYER_1"),
		])
	)
	_check(
		"a_mis_coded_player_slot_is_rejected_before_dispatch",
		not _cond(harness, "PLAYER_HAS_CREDITS", [
			_int_arg(500), _comparison_arg(ParamTypes.COMPARE_LESS_EQUAL),
			_name_arg("SYNTH_PLAYER_1"),
		])
	)

	# TEAM and OBJECT_STATUS slots carry no observed code, so a foreign code
	# passes validation BY DESIGN and the handler must still read the TEXT
	# field - the decoy integer in the same slot is what a wrong-field read
	# would deliver.
	var status := _act(harness, "TEAM_CHANGE_OBJECT_STATUS", [
		_argument(ParamTypes.ARGUMENT_COUNTER_NAME, 424242, 0.0, "Synthetic Siege Line"),
		_name_arg("SYNTH_STATUS_X"),
		_boolean_arg(true),
	])
	_check(
		"an_unobserved_team_code_is_accepted_and_still_read_as_text",
		status == Dispatch.Status.OK
		and world.calls.has(
			"units.set_object_status|%d|Synthetic Siege Line|SYNTH_STATUS_X|true"
			% SageScriptWorld.Scope.TEAM
		),
		"status=%d calls=%s" % [status, str(world.calls)]
	)


# --- Stub world -----------------------------------------------------------
#
# SageScriptWorldStub already answers the money read (economy.money forwards
# to the base stub's player_money over the `money` dictionary, behind
# CAP_PLAYER_MONEY - which is also how the refusal test turns it off). Only
# the Units facet is taught HERE, because the shared stub is an existing file
# that ~17 concurrent packages also depend on. Every command is recorded as a
# flat string so a test asserts on the exact arguments that reached the world.


class EconWorld:
	extends SageScriptWorldStub

	var calls: Array[String] = []

	func _make_units() -> SageScriptWorld.Units:
		return StubUnits.new()


class StubUnits:
	extends SageScriptWorld.Units

	func set_object_status(
		scope: int, name: String, status: String, enabled: bool
	) -> bool:
		(world as EconWorld).calls.append(
			"units.set_object_status|%d|%s|%s|%s"
			% [scope, name, status, str(enabled).to_lower()]
		)
		return true


# --- Reporting ------------------------------------------------------------


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("HANDLERS_WP10 PASS %s%s" % [name, " (%s)" % detail if detail != "" else ""])
	else:
		failed += 1
		printerr("HANDLERS_WP10 FAIL %s%s" % [name, " (%s)" % detail if detail != "" else ""])
