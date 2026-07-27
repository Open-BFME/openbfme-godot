extends SceneTree

## Proof runner for SageScriptExecutor, the component that runs decoded SAGE
## map scripts through the sourced vocabulary (SageScriptDispatch).
##
## Covers, in order:
##   1. condition model    - OrCondition blocks OR'd, Conditions AND'd,
##                           short-circuit, inversion, fail-closed empties
##   2. branches           - ScriptAction on true, ScriptActionFalse on false
##   3. evaluationInterval - authored seconds -> ticks; absent AND zero both
##                           default to per-frame (the measured retail
##                           default); the distribution is observable
##   4. activity via env   - ENABLE_SCRIPT / DISABLE_SCRIPT and
##                           deactivateUponSuccess all read/write
##                           SageScriptEnv.script_enabled; authored isActive
##                           is the fallback; unresolved references reported
##   5. subroutines        - run only via CALL_SUBROUTINE; full-body
##                           evaluation; missing / recursive / disabled calls
##                           refuse honestly and are counted
##   6. world refusal      - a world-touching action on the refusing base
##                           world is counted and gapped, and the run
##                           continues past it
##   7. accounting         - load-time census buckets every opcode; malformed
##                           records are dropped WITH identity; runtime
##                           misses land in the dispatcher gap log
##   8. order semantics    - scripts evaluate in load (document) order,
##                           pinned by non-commuting actions
##   9. determinism        - two independently built executors agree on full
##                           state snapshots for 200 ticks
##  10. real corpus        - optional .private decoded source, skip-if-absent
##
## All fixtures are SYNTHETIC decoded records in the exact sage_scb.py shape;
## the runner passes with no retail install present.
##
## Invocation:
##   Godot_v4.7-stable_win64_console.exe --headless --path game \
##     -s res://tests/script_executor_runner.gd

const Executor := preload("res://src/script/script_executor.gd")
const Dispatch := preload("res://src/script/script_dispatch.gd")
const GapLog := preload("res://src/script/script_gaps.gd")
const ParamTypes := preload("res://src/script/script_param_types.gd")
const Vocabulary := preload("res://src/script/script_vocabulary.gd")

const CONTRACT_RELATIVE_PATH := ".private/retail-work/reports/skirmish-script-contract/skirmish_script_contract.json"

## The interpreter runs at 10 ticks per second. Every expected value below is
## a LITERAL derived from that and from the authored payload - never an
## expression rebuilt from the code under test (see the timer-unit incident in
## retail_map_script_runner.gd for why that rule exists).
const TICKS_PER_SECOND := 10

var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	_test_or_of_ands_condition_model()
	_test_true_false_branches_and_inversion()
	_test_evaluation_interval()
	_test_activity_via_env()
	_test_subroutines()
	_test_world_refusal_does_not_derail()
	_test_load_and_runtime_accounting()
	_test_document_order_semantics()
	_test_twin_determinism()
	_test_real_contract_payloads()
	print("SCRIPT_EXECUTOR_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


# --- Fixture builders (decoded sage_scb.py shape) --------------------------


func _argument(argument_type: int, integer: int = 0, real: float = 0.0, text: String = "") -> Dictionary:
	return {"argumentType": argument_type, "integer": integer, "real": real, "text": text}


func _counter_arg(name: String) -> Dictionary:
	return _argument(ParamTypes.ARGUMENT_COUNTER_NAME, 0, 0.0, name)


func _flag_arg(name: String) -> Dictionary:
	return _argument(ParamTypes.ARGUMENT_FLAG_NAME, 0, 0.0, name)


func _int_arg(value: int) -> Dictionary:
	return _argument(ParamTypes.ARGUMENT_INTEGER, value)


func _bool_arg(value: bool) -> Dictionary:
	return _argument(ParamTypes.ARGUMENT_BOOLEAN, 1 if value else 0)


func _real_arg(value: float) -> Dictionary:
	return _argument(ParamTypes.ARGUMENT_REAL, 0, value)


func _compare_arg(value: int) -> Dictionary:
	return _argument(ParamTypes.ARGUMENT_COMPARISON, value)


func _player_arg(name: String) -> Dictionary:
	return _argument(ParamTypes.ARGUMENT_PLAYER, 0, 0.0, name)


func _text_arg(value: String) -> Dictionary:
	# Parameter types with no observed argumentType code (SCRIPT,
	# SCRIPT_SUBROUTINE, MATH_OPERATOR names read from integer, ...) are not
	# code-checked by validation; 99 marks them as such.
	return _argument(99, 0, 0.0, value)


func _condition(opcode: String, arguments: Array, inverted: bool = false, enabled: bool = true) -> Dictionary:
	return {
		"name": "Condition",
		"version": 6,
		"value": {
			"contentType": 0,
			"internalName": {"name": opcode, "wireTypeCode": 3},
			"arguments": arguments,
			"enabled": enabled,
			"inverted": inverted,
		},
	}


func _or_condition(conditions: Array) -> Dictionary:
	return {"name": "OrCondition", "version": 1, "value": {"records": conditions}}


func _action(
	opcode: String, arguments: Array, record_name: String = "ScriptAction", enabled: bool = true
) -> Dictionary:
	return {
		"name": record_name,
		"version": 3,
		"value": {
			"contentType": 0,
			"internalName": {"name": opcode, "wireTypeCode": 3},
			"arguments": arguments,
			"enabled": enabled,
		},
	}


func _always() -> Dictionary:
	return _or_condition([_condition("CONDITION_TRUE", [])])


func _set_flag_action(name: String, value: bool, record_name: String = "ScriptAction") -> Dictionary:
	return _action("SET_FLAG", [_flag_arg(name), _bool_arg(value)], record_name)


func _increment_action(name: String, amount: int) -> Dictionary:
	# INCREMENT_COUNTER(INT amount, COUNTER) - value first, name second.
	return _action("INCREMENT_COUNTER", [_int_arg(amount), _counter_arg(name)])


func _script(name: String, records: Array, options: Dictionary = {}) -> Dictionary:
	var payload := {
		"name": name,
		"comment": "",
		"conditionsComment": "",
		"actionsComment": "",
		"isActive": bool(options.get("isActive", true)),
		"deactivateUponSuccess": bool(options.get("deactivateUponSuccess", false)),
		"activeInEasy": bool(options.get("activeInEasy", true)),
		"activeInMedium": bool(options.get("activeInMedium", true)),
		"activeInHard": bool(options.get("activeInHard", true)),
		"isSubroutine": bool(options.get("isSubroutine", false)),
		"evaluationInterval": int(options.get("evaluationInterval", 0)),
		"actionsFireSequentially": false,
		"loopActions": false,
		"loopCount": 0,
		"sequentialTargetType": 1,
		"sequentialTargetName": "",
		"scope": "ALL",
		"records": records,
	}
	if bool(options.get("omit_interval", false)):
		payload.erase("evaluationInterval")
	return payload


# --- 1. OR-of-ANDs condition model -----------------------------------------


func _flag_gate_payload(blocks: Array) -> Dictionary:
	return _script("Gate", blocks + [_set_flag_action("fired", true)])


func _or_of_ands_fires(a: bool, b: bool, c: bool) -> bool:
	# Gate: (FLAG a AND FLAG b) OR (FLAG c) -> SET_FLAG fired.
	var executor: SageScriptExecutor = Executor.new()
	executor.load_script_payload(_flag_gate_payload([
		_or_condition([
			_condition("FLAG", [_flag_arg("a"), _bool_arg(true)]),
			_condition("FLAG", [_flag_arg("b"), _bool_arg(true)]),
		]),
		_or_condition([
			_condition("FLAG", [_flag_arg("c"), _bool_arg(true)]),
		]),
	]))
	executor.env.set_flag("a", a)
	executor.env.set_flag("b", b)
	executor.env.set_flag("c", c)
	executor.tick()
	return executor.env.flag("fired")


func _test_or_of_ands_condition_model() -> void:
	_check("and_block_needs_every_condition", not _or_of_ands_fires(true, false, false))
	_check("and_block_fires_when_all_true", _or_of_ands_fires(true, true, false))
	_check("or_blocks_fire_independently", _or_of_ands_fires(false, false, true))
	_check("nothing_true_means_no_fire", not _or_of_ands_fires(false, false, false))

	# A script with NO condition blocks never fires its true branch
	# (fail-closed), and a block whose conditions are all disabled does not
	# fire either - but its false branch still runs.
	var bare: SageScriptExecutor = Executor.new()
	bare.load_script_payload(_script("No Blocks", [
		_set_flag_action("true_ran", true),
		_set_flag_action("false_ran", true, "ScriptActionFalse"),
	]))
	bare.tick()
	_check(
		"script_without_condition_blocks_never_fires",
		not bare.env.flag("true_ran") and bare.env.flag("false_ran")
	)

	var all_disabled: SageScriptExecutor = Executor.new()
	all_disabled.load_script_payload(_script("Disabled Conditions", [
		_or_condition([_condition("CONDITION_TRUE", [], false, false)]),
		_set_flag_action("true_ran", true),
	]))
	all_disabled.tick()
	_check(
		"block_of_disabled_conditions_does_not_fire",
		not all_disabled.env.flag("true_ran")
	)


# --- 2. True/false branches and inversion ----------------------------------


func _test_true_false_branches_and_inversion() -> void:
	var executor: SageScriptExecutor = Executor.new()
	executor.load_script_payloads([
		_script("True Branch", [
			_always(),
			_set_flag_action("tb_true", true),
			_set_flag_action("tb_false", true, "ScriptActionFalse"),
		]),
		_script("False Branch", [
			_or_condition([_condition("CONDITION_FALSE", [])]),
			_set_flag_action("fb_true", true),
			_set_flag_action("fb_false", true, "ScriptActionFalse"),
		]),
		_script("Inverted True", [
			_or_condition([_condition("CONDITION_TRUE", [], true)]),
			_set_flag_action("inv_false", true, "ScriptActionFalse"),
		]),
	])
	executor.tick()
	_check(
		"true_condition_runs_only_script_action",
		executor.env.flag("tb_true") and not executor.env.flag("tb_false")
	)
	_check(
		"false_condition_runs_only_script_action_false",
		executor.env.flag("fb_false") and not executor.env.flag("fb_true")
	)
	_check("inverted_condition_flips_the_branch", executor.env.flag("inv_false"))


# --- 3. evaluationInterval -------------------------------------------------


func _test_evaluation_interval() -> void:
	var executor: SageScriptExecutor = Executor.new()
	executor.load_script_payloads([
		# Authored "every 2 seconds" -> every 20 ticks at 10 ticks/second.
		_script("Slow", [_always(), _increment_action("slow", 1)],
			{"evaluationInterval": 2}),
		# Field explicitly zero -> per-frame (the measured retail default).
		_script("Zero", [_always(), _increment_action("zero", 1)],
			{"evaluationInterval": 0}),
		# Field ABSENT from the payload entirely -> per-frame as well.
		_script("Absent", [_always(), _increment_action("absent", 1)],
			{"omit_interval": true}),
	])
	executor.run_ticks(40)
	# 40 ticks: the 20-tick script fires on ticks 20 and 40 -> 2.
	_check(
		"authored_interval_two_seconds_is_twenty_ticks",
		executor.env.counter("slow") == 2,
		"slow=%d expected=2" % executor.env.counter("slow")
	)
	_check(
		"interval_zero_evaluates_every_tick",
		executor.env.counter("zero") == 40,
		"zero=%d expected=40" % executor.env.counter("zero")
	)
	_check(
		"interval_absent_evaluates_every_tick",
		executor.env.counter("absent") == 40,
		"absent=%d expected=40" % executor.env.counter("absent")
	)

	# The distribution is OBSERVABLE: per-frame pressure is a measured retail
	# failure mode, so the report must show it rather than defaulting it away.
	var report := executor.load_report()
	_check(
		"interval_distribution_is_reported",
		report["interval_histogram_ticks"] == {1: 2, 20: 1}
		and int(report["per_frame_scripts"]) == 2,
		str(report["interval_histogram_ticks"])
	)


# --- 4. Enable/disable through SageScriptEnv -------------------------------


func _test_activity_via_env() -> void:
	var executor: SageScriptExecutor = Executor.new()
	executor.load_script_payloads([
		# Document order: Sleeper BEFORE Waker, so the tick that enables it
		# does not also run it - it wakes on the NEXT tick.
		_script("Sleeper", [_always(), _set_flag_action("woke", true)],
			{"isActive": false}),
		_script("Counter Pump", [_always(), _increment_action("c", 1)]),
		_script("Stop At Three", [
			_or_condition([_condition("COUNTER", [
				_counter_arg("c"), _compare_arg(ParamTypes.COMPARE_EQUAL), _int_arg(3),
			])]),
			_action("DISABLE_SCRIPT", [_text_arg("Counter Pump")]),
		]),
		_script("Waker", [
			_always(),
			_action("ENABLE_SCRIPT", [_text_arg("Sleeper")]),
			_action("ENABLE_SCRIPT", [_text_arg("Ghost Script")]),
		], {"deactivateUponSuccess": true}),
	])

	executor.tick()
	var woke_after_one := executor.env.flag("woke")
	executor.run_ticks(9)

	_check(
		"authored_inactive_script_sleeps_until_enabled",
		not woke_after_one and executor.env.flag("woke"),
		"after_tick_1=%s after_tick_10=%s" % [woke_after_one, executor.env.flag("woke")]
	)
	_check(
		"disable_script_stops_evaluation_via_env",
		executor.env.counter("c") == 3,
		"c=%d expected=3" % executor.env.counter("c")
	)
	_check(
		"activity_lives_in_the_env_not_the_executor",
		executor.env.script_enabled.get("Counter Pump") == false
		and executor.env.script_enabled.get("Sleeper") == true
	)
	# deactivateUponSuccess also writes the env bit, and the script stays off.
	_check(
		"deactivate_upon_success_writes_the_env_bit",
		executor.env.script_enabled.get("Waker") == false
	)
	# Re-enabling through the same env bit resumes evaluation.
	executor.env.set_script_enabled("Counter Pump", true)
	executor.tick()
	_check(
		"re_enabling_the_env_bit_resumes_the_script",
		executor.env.counter("c") == 4,
		"c=%d expected=4" % executor.env.counter("c")
	)
	# An ENABLE_SCRIPT naming no loaded script is visible, not silently inert.
	_check(
		"unresolved_script_reference_is_reported",
		executor.unresolved_script_references() == ["Ghost Script"],
		str(executor.unresolved_script_references())
	)


# --- 5. Subroutines --------------------------------------------------------


func _test_subroutines() -> void:
	var executor: SageScriptExecutor = Executor.new()
	executor.load_script_payloads([
		# Fires exactly once (deactivateUponSuccess), calling the subroutine.
		_script("Caller", [
			_always(),
			_action("CALL_SUBROUTINE", [_text_arg("Sub Add")]),
		], {"deactivateUponSuccess": true}),
		# The subroutine: full body, conditions and all.
		_script("Sub Add", [_always(), _increment_action("sub_c", 5)],
			{"isSubroutine": true}),
		# A subroutine whose condition is false must run its FALSE branch.
		_script("Sub FalseBranch", [
			_or_condition([_condition("CONDITION_FALSE", [])]),
			_set_flag_action("sub_false_ran", true, "ScriptActionFalse"),
		], {"isSubroutine": true}),
		_script("Caller FalseBranch", [
			_always(),
			_action("CALL_SUBROUTINE", [_text_arg("Sub FalseBranch")]),
		], {"deactivateUponSuccess": true}),
		# Missing target: refused, counted, and the NEXT action still runs.
		_script("Caller Missing", [
			_always(),
			_action("CALL_SUBROUTINE", [_text_arg("Nobody Home")]),
			_set_flag_action("after_missing", true),
		], {"deactivateUponSuccess": true}),
		# Recursion: the subroutine calls itself; the inner call is refused.
		_script("Sub Loop", [
			_always(),
			_increment_action("loop_c", 1),
			_action("CALL_SUBROUTINE", [_text_arg("Sub Loop")]),
		], {"isSubroutine": true}),
		_script("Caller Loop", [
			_always(),
			_action("CALL_SUBROUTINE", [_text_arg("Sub Loop")]),
		], {"deactivateUponSuccess": true}),
	])

	executor.run_ticks(5)

	_check(
		"subroutine_runs_when_called",
		executor.env.counter("sub_c") == 5,
		"sub_c=%d expected=5" % executor.env.counter("sub_c")
	)
	# 5 ticks, but the caller fired once; if "Sub Add" self-evaluated it would
	# have kept incrementing every tick.
	_check(
		"subroutine_does_not_self_evaluate",
		executor.env.counter("sub_c") == 5 and executor.subroutine_calls_served >= 1
	)
	_check("subroutine_false_branch_runs", executor.env.flag("sub_false_ran"))
	_check(
		"missing_subroutine_is_counted_and_gapped",
		int(executor.subroutine_misses.get("Nobody Home", 0)) == 1
		and executor.dispatch.gaps.has(
			"action", "CALL_SUBROUTINE", GapLog.REASON_UNIMPLEMENTED
		),
		str(executor.subroutine_misses)
	)
	_check("run_continues_past_a_missing_subroutine", executor.env.flag("after_missing"))
	_check(
		"recursive_subroutine_call_is_refused_not_hung",
		executor.env.counter("loop_c") == 1
		and int(executor.subroutine_recursion_refusals.get("Sub Loop", 0)) == 1,
		"loop_c=%d refusals=%s" % [
			executor.env.counter("loop_c"), str(executor.subroutine_recursion_refusals)
		]
	)

	# A disabled subroutine refuses the call, visibly.
	var gated: SageScriptExecutor = Executor.new()
	gated.load_script_payloads([
		_script("Caller", [
			_always(), _action("CALL_SUBROUTINE", [_text_arg("Sub Gated")]),
		], {"deactivateUponSuccess": true}),
		_script("Sub Gated", [_always(), _increment_action("g", 1)],
			{"isSubroutine": true}),
	])
	gated.env.set_script_enabled("Sub Gated", false)
	gated.tick()
	_check(
		"disabled_subroutine_call_is_refused_and_counted",
		gated.env.counter("g") == 0
		and int(gated.subroutine_disabled_refusals.get("Sub Gated", 0)) == 1
	)

	# Calling a NON-subroutine script by name executes it and counts the
	# anomaly, so a misauthored map is visible without being broken further.
	# "Regular" is an ordinary active script, so on tick 1 it runs TWICE: once
	# via the call (Caller precedes it in document order) and once when its own
	# turn comes - r == 2 asserts both paths ran.
	var odd: SageScriptExecutor = Executor.new()
	odd.load_script_payloads([
		_script("Caller", [
			_always(), _action("CALL_SUBROUTINE", [_text_arg("Regular")]),
		], {"deactivateUponSuccess": true}),
		_script("Regular", [_always(), _increment_action("r", 1)]),
	])
	odd.tick()
	_check(
		"non_subroutine_target_runs_and_is_counted",
		odd.env.counter("r") == 2
		and int(odd.subroutine_calls_to_non_subroutine.get("Regular", 0)) == 1,
		"r=%d non_subroutine=%s" % [odd.env.counter("r"), str(odd.subroutine_calls_to_non_subroutine)]
	)


# --- 6. World refusal does not derail the run ------------------------------


func _test_world_refusal_does_not_derail() -> void:
	# Default world is the refusing base class: PLAYER_SET_MONEY must report
	# WORLD_REFUSED, be counted and gapped, and the actions AFTER it - and the
	# scripts after the script - must still run.
	var executor: SageScriptExecutor = Executor.new()
	executor.load_script_payloads([
		_script("Money Then Flag", [
			_always(),
			_action("PLAYER_SET_MONEY", [_player_arg("<This Player>"), _int_arg(5000)]),
			_set_flag_action("after_refusal", true),
		], {"deactivateUponSuccess": true}),
		_script("Later Script", [_always(), _increment_action("later", 1)],
			{"deactivateUponSuccess": true}),
	])
	executor.tick()
	_check(
		"world_refused_is_counted_in_outcomes",
		int(executor.action_outcomes.get("world-refused", 0)) == 1,
		str(executor.action_outcomes)
	)
	_check(
		"world_refused_is_gapped_with_identity",
		executor.dispatch.gaps.has("action", "PLAYER_SET_MONEY", GapLog.REASON_WORLD_REFUSED),
		str(executor.dispatch.gaps.to_lines())
	)
	_check(
		"run_continues_past_a_world_refusal",
		executor.env.flag("after_refusal") and executor.env.counter("later") == 1
	)


# --- 7. Load-time census and runtime accounting ----------------------------


func _unimplemented_action_name(executor: SageScriptExecutor) -> String:
	## An action that IS in the sourced vocabulary but has NO handler yet.
	## Chosen from candidates and verified against the live dispatch, so this
	## test does not go stale when a work package lands.
	for candidate: String in [
		"UNIT_SPAWN_NAMED_LOCATION_ORIENTATION",  # WP16, no package file yet
		"PLAYER_ADD_SKILLPOINTS",                 # WP13, no package file yet
		"TEAM_WANDER",                            # WP15 exists; may be partial
	]:
		if (
			Vocabulary.has_entry(Vocabulary.Kind.ACTION, candidate)
			and not executor.dispatch.action_handlers.has(candidate)
		):
			return candidate
	return ""


func _test_load_and_runtime_accounting() -> void:
	var executor: SageScriptExecutor = Executor.new()
	var unimplemented := _unimplemented_action_name(executor)
	var records := [
		_always(),
		_action("SET_COUNTER", [_counter_arg("x"), _int_arg(1)]),               # implemented
		_action("SET_COUNTER_TO_CLIENT_RANDOM_VALUE",
			[_counter_arg("x"), _int_arg(0), _int_arg(9)]),                     # deliberate
		_action("MAP_SHROUD_ALL", [_player_arg("<This Player>")]),              # blocked
		_action("TOTALLY_BOGUS_ACTION", []),                                    # unknown
		_action("TOTALLY_BOGUS_DISABLED", [], "ScriptAction", false),           # unknown + disabled
		{"name": "Garbage", "version": 1, "value": {}},                         # malformed
	]
	if unimplemented != "":
		records.append(_action(unimplemented, []))
	executor.load_script_payload(_script("Census", records, {"deactivateUponSuccess": true}))

	# LOAD-TIME: every record is bucketed exactly once, including disabled
	# ones; the malformed record is dropped WITH identity, not silently.
	var report := executor.load_report()
	var buckets: Dictionary = report["census"]
	_check(
		"census_buckets_implemented_deliberate_blocked_unknown",
		int((buckets["implemented"] as Dictionary).get("SET_COUNTER", 0)) == 1
		and int((buckets["implemented"] as Dictionary).get("CONDITION_TRUE", 0)) == 1
		and int((buckets["deliberate"] as Dictionary).get("SET_COUNTER_TO_CLIENT_RANDOM_VALUE", 0)) == 1
		and int((buckets["blocked"] as Dictionary).get("MAP_SHROUD_ALL", 0)) == 1
		and int((buckets["unknown"] as Dictionary).get("TOTALLY_BOGUS_ACTION", 0)) == 1
		and int((buckets["unknown"] as Dictionary).get("TOTALLY_BOGUS_DISABLED", 0)) == 1,
		str(buckets)
	)
	if unimplemented == "":
		_check(
			"census_bucket_unimplemented",
			true,
			"SKIP every candidate action now has a handler; pick new candidates"
		)
	else:
		_check(
			"census_bucket_unimplemented",
			int((buckets["unimplemented"] as Dictionary).get(unimplemented, 0)) == 1,
			"%s -> %s" % [unimplemented, str(buckets["unimplemented"])]
		)
	var expected_records := 6 + (1 if unimplemented != "" else 0)
	_check(
		"every_content_record_is_bucketed_exactly_once",
		executor.census_total() == expected_records,
		"census_total=%d expected=%d" % [executor.census_total(), expected_records]
	)
	_check(
		"malformed_record_is_dropped_with_identity",
		report["malformed_records"] == ["script 'Census': unsupported record 'Garbage'"],
		str(report["malformed_records"])
	)
	_check("disabled_record_is_counted", int(report["disabled_records"]) == 1)

	# RUNTIME: the reached unknown/deliberate/blocked records each produce a
	# gap with reason and identity; the disabled one is never dispatched.
	executor.tick()
	var gaps: SageScriptGapLog = executor.dispatch.gaps
	_check(
		"runtime_unknown_action_is_gapped",
		gaps.count_for("action", "TOTALLY_BOGUS_ACTION", GapLog.REASON_UNKNOWN_NAME) == 1,
		str(gaps.to_lines())
	)
	_check(
		"runtime_deliberate_and_blocked_keep_their_own_reasons",
		gaps.has("action", "SET_COUNTER_TO_CLIENT_RANDOM_VALUE", GapLog.REASON_DELIBERATE)
		and gaps.has("action", "MAP_SHROUD_ALL", GapLog.REASON_BLOCKED_SUBSYSTEM)
	)
	if unimplemented != "":
		_check(
			"runtime_unimplemented_action_is_gapped",
			gaps.has("action", unimplemented, GapLog.REASON_UNIMPLEMENTED)
			or gaps.has("action", unimplemented, GapLog.REASON_BAD_ARGUMENTS),
			str(gaps.to_lines())
		)
	_check(
		"disabled_record_is_never_dispatched",
		not gaps.has("action", "TOTALLY_BOGUS_DISABLED", GapLog.REASON_UNKNOWN_NAME)
	)
	_check(
		"implemented_action_still_served_amid_the_gaps",
		executor.env.counter("x") == 1 and int(executor.action_outcomes.get("ok", 0)) >= 1
	)


# --- 8. Document-order semantics -------------------------------------------


func _test_document_order_semantics() -> void:
	# Two per-frame scripts whose actions do NOT commute:
	#   1st loaded: c += 1
	#   2nd loaded: c *= 2
	# Document order after tick 1: (0+1)*2 = 2; after tick 2: (2+1)*2 = 6.
	# Reversed order would give 1 then 3. This pins evaluation to LOAD
	# SEQUENCE order - the decoded document order retail uses.
	var executor: SageScriptExecutor = Executor.new()
	executor.load_script_payloads([
		_script("Add One", [_always(), _increment_action("c", 1)]),
		_script("Double", [
			_always(),
			_action("COUNTER_MATH_VALUE", [
				_counter_arg("c"),
				_argument(99, ParamTypes.MATH_MULTIPLY),  # MATH_OPERATOR reads integer
				_int_arg(2),
			]),
		]),
	])
	executor.run_ticks(2)
	_check(
		"scripts_evaluate_in_load_document_order",
		executor.env.counter("c") == 6,
		"c=%d expected=6 (reversed order would be 3)" % executor.env.counter("c")
	)


# --- 9. Determinism: twin executors ----------------------------------------


func _determinism_payloads() -> Array:
	## Built fresh per call so the two executors share NO dictionary
	## instances. Exercises counters, flags, timers, intervals, enable bits,
	## subroutines, refusals and unknown opcodes - state of every kind the
	## snapshot carries.
	return [
		_script("Pump", [_always(), _increment_action("c", 1)]),
		_script("Double Slowly", [
			_always(),
			_action("COUNTER_MATH_VALUE", [
				_counter_arg("c"), _argument(99, ParamTypes.MATH_MULTIPLY), _int_arg(2),
			]),
		], {"evaluationInterval": 3}),
		_script("Arm Timer", [
			_always(),
			_action("SET_MILLISECOND_TIMER", [_counter_arg("t"), _real_arg(4.0)]),
		], {"deactivateUponSuccess": true}),
		_script("Timer Gate", [
			_or_condition([_condition("TIMER_EXPIRED", [_counter_arg("t")])]),
			_set_flag_action("expired", true),
			_action("DISABLE_SCRIPT", [_text_arg("Pump")]),
		], {"deactivateUponSuccess": true}),
		_script("Call Sub", [
			_or_condition([_condition("FLAG", [_flag_arg("expired"), _bool_arg(true)])]),
			_action("CALL_SUBROUTINE", [_text_arg("Sub Tally")]),
			_action("CALL_SUBROUTINE", [_text_arg("Missing Sub")]),
		]),
		_script("Sub Tally", [_always(), _increment_action("tally", 1)],
			{"isSubroutine": true}),
		_script("Refuses", [
			_always(),
			_action("PLAYER_SET_MONEY", [_player_arg("<This Player>"), _int_arg(1)]),
			_action("NO_SUCH_OPCODE_EVER", []),
		], {"evaluationInterval": 5}),
	]


func _test_twin_determinism() -> void:
	var twin_a: SageScriptExecutor = Executor.new()
	var twin_b: SageScriptExecutor = Executor.new()
	twin_a.load_script_payloads(_determinism_payloads())
	twin_b.load_script_payloads(_determinism_payloads())

	var first_divergence := -1
	for tick_number in range(1, 201):
		twin_a.tick()
		twin_b.tick()
		if first_divergence < 0 and str(twin_a.state_snapshot()) != str(twin_b.state_snapshot()):
			first_divergence = tick_number
	_check(
		"twin_executors_agree_on_full_state_for_200_ticks",
		first_divergence < 0,
		"first_divergence=%d" % first_divergence
	)
	# The fixture must actually have DONE things, or the equality above is a
	# comparison of two empty ledgers.
	var snapshot := twin_a.state_snapshot()
	_check(
		"twin_fixture_is_not_vacuous",
		twin_a.env.flag("expired")
		and twin_a.env.counter("tally") >= 1
		and int(twin_a.action_outcomes.get("world-refused", 0)) >= 1
		and int(twin_a.subroutine_misses.get("Missing Sub", 0)) >= 1
		and (snapshot["gaps"] as Array).size() >= 2,
		str(snapshot)
	)
	_check(
		"load_reports_agree_between_twins",
		str(twin_a.load_report()) == str(twin_b.load_report())
	)


# --- 10. Optional real decoded corpus (.private, skip-if-absent) -----------


func _contract_path() -> String:
	var game_root := ProjectSettings.globalize_path("res://")
	return game_root.rstrip("/").get_base_dir().path_join(CONTRACT_RELATIVE_PATH)


func _test_real_contract_payloads() -> void:
	var path := _contract_path()
	if not FileAccess.file_exists(path):
		_check("real_contract_source_runs", true, "SKIP contract absent at %s" % path)
		return
	var parsed: Variant = JSON.parse_string(FileAccess.get_file_as_string(path))
	if not (parsed is Dictionary) or (Array((parsed as Dictionary).get("sources", []))).is_empty():
		_check("real_contract_source_runs", false, "contract unreadable or empty")
		return
	var source: Dictionary = Array((parsed as Dictionary)["sources"])[0]
	var executor: SageScriptExecutor = Executor.new()
	var loaded := 0
	for row: Variant in Array(source.get("scripts", [])):
		var payload: Dictionary = (row as Dictionary).get("payload", {})
		if not payload.is_empty():
			executor.load_script_payload(payload)
			loaded += 1
	executor.run_ticks(10)
	var report := executor.load_report()
	_check(
		"real_contract_source_runs",
		loaded > 0 and loaded == int(source.get("scriptCount", -1)),
		"loaded=%d source=%s" % [loaded, str(source.get("path"))]
	)
	var expected_slots := 0
	for count: Variant in (source.get("actionOpcodes", {}) as Dictionary).values():
		expected_slots += int(count)
	for count: Variant in (source.get("conditionOpcodes", {}) as Dictionary).values():
		expected_slots += int(count)
	_check(
		"real_source_opcodes_fully_bucketed",
		executor.census_total() == expected_slots and expected_slots > 0,
		"bucketed=%d expected=%d" % [executor.census_total(), expected_slots]
	)
	print("SCRIPT_EXECUTOR real source=%s report=%s" % [str(source.get("path")), str(report)])
	print("SCRIPT_EXECUTOR real gaps=%s" % str(executor.dispatch.gaps.to_lines()))


# --- Reporting -------------------------------------------------------------


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("SCRIPT_EXECUTOR PASS %s%s" % [name, " (%s)" % detail if detail != "" else ""])
	else:
		failed += 1
		printerr("SCRIPT_EXECUTOR FAIL %s%s" % [name, " (%s)" % detail if detail != "" else ""])
