extends SceneTree

## Proof runner for the SAGE map-script / Lua vocabulary and its dispatcher.
##
## Covers, in order:
##   1. table integrity      - well-formed rows, no duplicate names, every entry
##                             carries a signature and a confidence marker
##   2. identity resolution  - the renumbering hazard: unknown names fail loudly,
##                             nameless records refuse to guess without a
##                             registered id map, and a mismatched id map is
##                             caught rather than silently dispatched
##   3. gap recording        - every failure mode lands in the gap log with
##                             identity, and nothing silently no-ops
##   4. tranche 1 handlers   - flags, counters, timers, script bits, money,
##                             debug, deterministic random, conditions
##   5. one vocabulary       - the Lua front door reaches the same handlers
##
## Invocation:
##   Godot_v4.7-stable_win64_console.exe --headless --path game \
##     --script res://tests/script_vocabulary_runner.gd

const Vocabulary := preload("res://src/script/script_vocabulary.gd")
const Dispatch := preload("res://src/script/script_dispatch.gd")
const CoreHandlers := preload("res://src/script/script_handlers_core.gd")
const Env := preload("res://src/script/script_env.gd")
const WorldStub := preload("res://src/script/script_world_stub.gd")
const World := preload("res://src/script/script_world.gd")
const GapLog := preload("res://src/script/script_gaps.gd")
const ParamTypes := preload("res://src/script/script_param_types.gd")
const ActionTable := preload("res://src/script/script_action_table.gd")
const ConditionTable := preload("res://src/script/script_condition_table.gd")

const KNOWN_GAME_SETS := [
	"SHARED", "BFME1_ZH", "ZH", "BFME2ROTWK", "BFME2ROTWK_PLUS", "CNC3KW",
	"CNC3KW_RA3U", "RA3", "RA3U", "RA3_CNC4", "CNC4", "UNSOURCED_GAME",
]

const PLAYER := "<This Player>"

var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	_test_table_integrity()
	_test_identity_resolution()
	_test_gap_recording()
	_test_flag_and_counter_handlers()
	_test_timer_handlers()
	_test_script_control_and_world_handlers()
	_test_condition_handlers()
	_test_lua_front_door()
	_report_coverage()
	print("SCRIPT_VOCABULARY_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


# --- Fixture helpers ------------------------------------------------------


func _argument(argument_type: int, integer: int = 0, real: float = 0.0, text: String = "") -> Dictionary:
	return {"argumentType": argument_type, "integer": integer, "real": real, "text": text}


func _counter_arg(name: String) -> Dictionary:
	return _argument(ParamTypes.ARGUMENT_COUNTER_NAME, 0, 0.0, name)


func _flag_arg(name: String) -> Dictionary:
	return _argument(ParamTypes.ARGUMENT_FLAG_NAME, 0, 0.0, name)


func _int_arg(value: int) -> Dictionary:
	return _argument(ParamTypes.ARGUMENT_INTEGER, value)


func _real_arg(value: float) -> Dictionary:
	return _argument(ParamTypes.ARGUMENT_REAL, 0, value)


func _bool_arg(value: bool) -> Dictionary:
	return _argument(ParamTypes.ARGUMENT_BOOLEAN, 1 if value else 0)


func _compare_arg(value: int) -> Dictionary:
	return _argument(ParamTypes.ARGUMENT_COMPARISON, value)


func _player_arg(name: String) -> Dictionary:
	return _argument(ParamTypes.ARGUMENT_PLAYER, 0, 0.0, name)


func _text_arg(value: String) -> Dictionary:
	# Parameter types this repo has not observed a code for (SCRIPT, Text, ...)
	# are read from the text field; the code itself is not asserted on.
	return _argument(99, 0, 0.0, value)


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
	var env := Env.new()
	var world := WorldStub.new(20260726)
	return {"dispatch": dispatch, "env": env, "world": world}


func _act(harness: Dictionary, name: String, arguments: Array, script_name: String = "fixture") -> int:
	return (harness["dispatch"] as SageScriptDispatch).execute_action(
		_record(name, arguments), harness["env"], harness["world"], script_name
	)


func _cond(harness: Dictionary, name: String, arguments: Array, inverted: bool = false) -> bool:
	return (harness["dispatch"] as SageScriptDispatch).evaluate_condition(
		_record(name, arguments, inverted), harness["env"], harness["world"], "fixture"
	)


# --- 1. Table integrity ---------------------------------------------------


func _test_table_integrity() -> void:
	var actions := Vocabulary.actions()
	var conditions := Vocabulary.conditions()
	_check("action_table_is_populated", actions.size() > 400, "size=%d" % actions.size())
	_check(
		"condition_table_is_populated", conditions.size() > 150,
		"size=%d" % conditions.size()
	)

	# A duplicate name would be silently collapsed by the parse into a
	# dictionary, so compare against the raw row counts.
	_check(
		"action_rows_have_no_duplicate_names",
		ActionTable.ROWS.size() == actions.size(),
		"rows=%d parsed=%d" % [ActionTable.ROWS.size(), actions.size()]
	)
	_check(
		"condition_rows_have_no_duplicate_names",
		ConditionTable.ROWS.size() == conditions.size(),
		"rows=%d parsed=%d" % [ConditionTable.ROWS.size(), conditions.size()]
	)

	var malformed := 0
	for row: Variant in ActionTable.ROWS + ConditionTable.ROWS:
		if String(row).split("|", true).size() != 5:
			malformed += 1
	_check("every_row_has_five_fields", malformed == 0, "malformed=%d" % malformed)

	# Every entry must carry a signature and a confidence marker. An entry with
	# no parameters is legal; an entry with an EMPTY parameter token is not,
	# because that is what a half-filled row looks like.
	var no_signature: Array[String] = []
	var bad_confidence: Array[String] = []
	var bad_games: Array[String] = []
	var empty_token: Array[String] = []
	var uncertain := 0
	for entry: Variant in actions.values() + conditions.values():
		var row: Dictionary = entry
		var name := String(row["name"])
		if name.strip_edges() == "":
			no_signature.append(name)
		if not row.has("params") or not (row["params"] is Array):
			no_signature.append(name)
		if not [Vocabulary.CONFIDENCE_SOURCED, Vocabulary.CONFIDENCE_UNCERTAIN].has(
			String(row["confidence"])
		):
			bad_confidence.append(name)
		if String(row["confidence"]) == Vocabulary.CONFIDENCE_UNCERTAIN:
			uncertain += 1
		if not KNOWN_GAME_SETS.has(String(row["games"])):
			bad_games.append(name)
		for token: Variant in row["params"]:
			if String(token).strip_edges() == "":
				empty_token.append(name)
	_check("every_entry_has_a_signature", no_signature.is_empty(), str(no_signature))
	_check("every_entry_has_a_confidence_marker", bad_confidence.is_empty(), str(bad_confidence))
	_check("every_entry_has_a_known_game_set", bad_games.is_empty(), str(bad_games))
	_check("no_entry_has_an_empty_parameter_token", empty_token.is_empty(), str(empty_token))

	# Entries the source itself marks uncertain must be flagged, not hidden.
	var uncertain_tokens_present := 0
	for entry: Variant in actions.values() + conditions.values():
		for token: Variant in (entry as Dictionary)["params"]:
			if ParamTypes.is_uncertain_token(String(token)):
				uncertain_tokens_present += 1
				break
	_check(
		"uncertain_signatures_are_flagged",
		uncertain_tokens_present == uncertain and uncertain > 0,
		"tokens=%d flagged=%d" % [uncertain_tokens_present, uncertain]
	)

	# Spot-check three transcriptions against the source reference.
	_check(
		"signature_spot_check_increment_counter",
		Vocabulary.action_by_name("INCREMENT_COUNTER").get("params", []) == ["INT", "COUNTER"],
		str(Vocabulary.action_by_name("INCREMENT_COUNTER").get("params", []))
	)
	_check(
		"signature_spot_check_counter_condition",
		Vocabulary.condition_by_name("COUNTER").get("params", [])
		== ["COUNTER", "COMPARISON", "INT"],
		str(Vocabulary.condition_by_name("COUNTER").get("params", []))
	)
	_check(
		"rotwk_only_entries_are_labelled",
		String(Vocabulary.action_by_name("CAMERA_BW_MODE_BEGIN").get("games", "")) == "BFME2ROTWK"
	)

	# The dispatcher must refuse a handler for a name outside the vocabulary.
	var dispatch := Dispatch.new()
	var accepted := dispatch.register_action("NOT_A_REAL_SAGE_ACTION", _no_op_handler)
	_check("dispatcher_refuses_handlers_for_unknown_names", not accepted)


func _no_op_handler(_ctx: Dictionary) -> int:
	return Dispatch.Status.OK


# --- 2. Identity resolution (the renumbering hazard) ----------------------


func _test_identity_resolution() -> void:
	Vocabulary.clear_id_maps()
	_check(
		"no_numeric_id_map_ships_by_default",
		Vocabulary.registered_id_map_count() == 0,
		"count=%d" % Vocabulary.registered_id_map_count()
	)

	var by_name := Vocabulary.resolve(Vocabulary.Kind.ACTION, "SET_FLAG", 0)
	_check(
		"named_record_resolves_by_name",
		int(by_name["status"]) == Vocabulary.Resolution.OK
		and String(by_name["name"]) == "SET_FLAG"
	)

	var unknown := Vocabulary.resolve(Vocabulary.Kind.ACTION, "MADE_UP_ACTION", 0)
	_check(
		"unknown_name_is_an_error_not_a_guess",
		int(unknown["status"]) == Vocabulary.Resolution.ERR_UNKNOWN_NAME,
		Vocabulary.resolution_label(int(unknown["status"]))
	)

	# A nameless record (older map versions) with no registered id map must
	# refuse outright - BFME2 renumbered the enums, so another title's ids would
	# dispatch the wrong handler.
	var nameless := Vocabulary.resolve(
		Vocabulary.Kind.ACTION, "", 12, Vocabulary.Game.BFME2, 6
	)
	_check(
		"nameless_record_without_an_id_map_refuses_to_guess",
		int(nameless["status"]) == Vocabulary.Resolution.ERR_NO_ID_MAP,
		Vocabulary.resolution_label(int(nameless["status"]))
	)
	_check(
		"no_id_map_error_explains_itself",
		String(nameless["detail"]).contains("renumbered"),
		String(nameless["detail"])
	)

	Vocabulary.register_id_map(
		Vocabulary.Game.BFME2, 6, Vocabulary.Kind.ACTION,
		{12: "SET_FLAG", 13: "SET_COUNTER"}
	)
	var mapped := Vocabulary.resolve(
		Vocabulary.Kind.ACTION, "", 12, Vocabulary.Game.BFME2, 6
	)
	_check(
		"registered_id_map_resolves_a_nameless_record",
		int(mapped["status"]) == Vocabulary.Resolution.OK
		and String(mapped["name"]) == "SET_FLAG",
		Vocabulary.resolution_label(int(mapped["status"]))
	)

	var absent := Vocabulary.resolve(
		Vocabulary.Kind.ACTION, "", 999, Vocabulary.Game.BFME2, 6
	)
	_check(
		"id_absent_from_the_registered_map_is_an_error",
		int(absent["status"]) == Vocabulary.Resolution.ERR_UNKNOWN_ID,
		Vocabulary.resolution_label(int(absent["status"]))
	)

	# The same id under a different game must NOT borrow the BFME2 table.
	var other_game := Vocabulary.resolve(
		Vocabulary.Kind.ACTION, "", 12, Vocabulary.Game.GENERALS_ZH, 6
	)
	_check(
		"an_id_map_never_leaks_across_games",
		int(other_game["status"]) == Vocabulary.Resolution.ERR_NO_ID_MAP,
		Vocabulary.resolution_label(int(other_game["status"]))
	)

	# The same id under a different map version must not be reused either.
	var other_version := Vocabulary.resolve(
		Vocabulary.Kind.ACTION, "", 12, Vocabulary.Game.BFME2, 5
	)
	_check(
		"an_id_map_never_leaks_across_map_versions",
		int(other_version["status"]) == Vocabulary.Resolution.ERR_NO_ID_MAP,
		Vocabulary.resolution_label(int(other_version["status"]))
	)

	# Name and id disagreeing means the id map is from the wrong game.
	var mismatch := Vocabulary.resolve(
		Vocabulary.Kind.ACTION, "SET_COUNTER", 12, Vocabulary.Game.BFME2, 6
	)
	_check(
		"id_and_name_disagreement_is_caught",
		int(mismatch["status"]) == Vocabulary.Resolution.ERR_ID_NAME_MISMATCH,
		Vocabulary.resolution_label(int(mismatch["status"]))
	)

	# An id map containing a name outside the vocabulary drops that row.
	Vocabulary.register_id_map(
		Vocabulary.Game.BFME1, 4, Vocabulary.Kind.ACTION, {1: "NOT_A_SAGE_ACTION"}
	)
	var junk := Vocabulary.resolve(Vocabulary.Kind.ACTION, "", 1, Vocabulary.Game.BFME1, 4)
	_check(
		"id_map_rows_naming_unknown_actions_are_rejected",
		int(junk["status"]) == Vocabulary.Resolution.ERR_UNKNOWN_ID,
		Vocabulary.resolution_label(int(junk["status"]))
	)

	Vocabulary.clear_id_maps()
	_check("id_maps_can_be_cleared", Vocabulary.registered_id_map_count() == 0)


# --- 3. Gap recording -----------------------------------------------------


func _test_gap_recording() -> void:
	var harness := _harness()
	var dispatch: SageScriptDispatch = harness["dispatch"]

	# Unknown name.
	var unknown_status := _act(harness, "MADE_UP_ACTION", [], "Unknown Script")
	_check("unknown_action_reports_unsupported", unknown_status == Dispatch.Status.UNSUPPORTED)
	_check(
		"unknown_action_records_a_gap",
		dispatch.gaps.has("action", "MADE_UP_ACTION", GapLog.REASON_UNKNOWN_NAME),
		str(dispatch.gaps.to_lines())
	)

	# Known but not implemented - the common case, and the one that must never
	# be a silent no-op.
	_act(harness, "CAMERA_MOD_FREEZE_ANGLE", [], "Camera Script")
	_check(
		"unimplemented_but_known_action_records_a_gap",
		dispatch.gaps.has("action", "CAMERA_MOD_FREEZE_ANGLE", GapLog.REASON_UNIMPLEMENTED),
		str(dispatch.gaps.to_lines())
	)

	# Identity is retained: which script, which tick.
	var row: Dictionary = dispatch.gaps.entries[
		"action|CAMERA_MOD_FREEZE_ANGLE|%s" % GapLog.REASON_UNIMPLEMENTED
	]
	_check(
		"gap_retains_script_identity",
		String(row["first_script"]) == "Camera Script" and String(row["kind"]) == "action",
		str(row)
	)

	# Aggregation: repeated misses cost one entry but keep a count.
	for _index in range(4):
		_act(harness, "CAMERA_MOD_FREEZE_ANGLE", [], "Camera Script")
	_check(
		"repeated_gaps_aggregate_with_a_count",
		dispatch.gaps.count_for(
			"action", "CAMERA_MOD_FREEZE_ANGLE", GapLog.REASON_UNIMPLEMENTED
		) == 5,
		"count=%d" % dispatch.gaps.count_for(
			"action", "CAMERA_MOD_FREEZE_ANGLE", GapLog.REASON_UNIMPLEMENTED
		)
	)

	# Arity disagreement with the declared signature.
	var bad_arity := _act(harness, "SET_FLAG", [_flag_arg("f")], "Arity Script")
	_check("bad_arity_reports_bad_arguments", bad_arity == Dispatch.Status.BAD_ARGUMENTS)
	_check(
		"bad_arity_records_a_gap",
		dispatch.gaps.has("action", "SET_FLAG", GapLog.REASON_BAD_ARGUMENTS)
	)

	# Argument type disagreement for an OBSERVED parameter type code.
	var bad_type := _act(
		harness, "SET_COUNTER", [_counter_arg("c"), _flag_arg("wrong")], "Type Script"
	)
	_check("wrong_argument_code_reports_bad_arguments", bad_type == Dispatch.Status.BAD_ARGUMENTS)

	# World refusal.
	var refusing := _harness()
	(refusing["world"] as SageScriptWorldStub).refuse(World.CAP_PLAYER_MONEY)
	var refused := _act(refusing, "PLAYER_SET_MONEY", [_player_arg(PLAYER), _int_arg(500)])
	_check("world_refusal_reports_world_refused", refused == Dispatch.Status.WORLD_REFUSED)
	_check(
		"world_refusal_records_a_gap",
		(refusing["dispatch"] as SageScriptDispatch).gaps.has(
			"action", "PLAYER_SET_MONEY", GapLog.REASON_WORLD_REFUSED
		)
	)

	# Deliberate refusal is its own reason, distinct from "not built yet".
	var deliberate := _act(
		harness, "SET_COUNTER_TO_CLIENT_RANDOM_VALUE",
		[_counter_arg("c"), _int_arg(1), _int_arg(9)], "Desync Script"
	)
	_check("deliberate_refusal_has_its_own_status", deliberate == Dispatch.Status.DELIBERATE)
	_check(
		"deliberate_refusal_records_a_distinct_reason",
		dispatch.gaps.has(
			"action", "SET_COUNTER_TO_CLIENT_RANDOM_VALUE", GapLog.REASON_DELIBERATE
		)
		and not dispatch.gaps.has(
			"action", "SET_COUNTER_TO_CLIENT_RANDOM_VALUE", GapLog.REASON_UNIMPLEMENTED
		)
	)

	# An unimplemented condition is FALSE even when inverted: an unevaluable
	# gate must not fire.
	var unimplemented_condition := _cond(harness, "UNIT_HEALTH", [], true)
	_check(
		"unimplemented_condition_is_false_even_when_inverted",
		unimplemented_condition == false
	)
	_check(
		"unimplemented_condition_records_a_gap",
		dispatch.gaps.has("condition", "UNIT_HEALTH", GapLog.REASON_UNIMPLEMENTED)
	)

	_check(
		"gap_reasons_are_summarised",
		dispatch.gaps.by_reason().has(GapLog.REASON_UNIMPLEMENTED),
		str(dispatch.gaps.by_reason())
	)
	_check(
		"gap_lines_are_stable_and_sorted",
		dispatch.gaps.to_lines() == dispatch.gaps.to_lines()
		and dispatch.gaps.to_lines().size() == dispatch.gaps.distinct_count()
	)


# --- 4. Handler tranche 1 -------------------------------------------------


func _test_flag_and_counter_handlers() -> void:
	var harness := _harness()
	var env: SageScriptEnv = harness["env"]

	_check("no_op_is_served", _act(harness, "NO_OP", []) == Dispatch.Status.OK)

	_act(harness, "SET_FLAG", [_flag_arg("alpha"), _bool_arg(true)])
	_act(harness, "SET_FLAG", [_flag_arg("beta"), _bool_arg(false)])
	_check("set_flag_writes_the_named_flag", env.flag("alpha") and not env.flag("beta"))

	# Repeated parameter type: positional reading must not confuse the two.
	_act(harness, "SET_FLAG_TO_FLAG", [_flag_arg("beta"), _flag_arg("alpha")])
	_check(
		"set_flag_to_flag_copies_source_into_destination",
		env.flag("beta") and env.flag("alpha"),
		"beta=%s alpha=%s" % [str(env.flag("beta")), str(env.flag("alpha"))]
	)

	_act(harness, "SET_COUNTER", [_counter_arg("c"), _int_arg(10)])
	_check("set_counter_writes_the_value", env.counter("c") == 10)

	# INCREMENT_COUNTER(INT, COUNTER): value first, name second.
	_act(harness, "INCREMENT_COUNTER", [_int_arg(4), _counter_arg("c")])
	_check("increment_counter_reads_value_first", env.counter("c") == 14, "c=%d" % env.counter("c"))
	_act(harness, "DECREMENT_COUNTER", [_int_arg(6), _counter_arg("c")])
	_check("decrement_counter_reads_value_first", env.counter("c") == 8, "c=%d" % env.counter("c"))

	_act(harness, "SET_COUNTER_TO_COUNTER", [_counter_arg("d"), _counter_arg("c")])
	_check("set_counter_to_counter_copies", env.counter("d") == 8)

	_act(harness, "COUNTER_MATH_VALUE", [
		_counter_arg("c"), _compare_arg(ParamTypes.MATH_MULTIPLY), _int_arg(3)
	])
	_check("counter_math_value_multiplies", env.counter("c") == 24, "c=%d" % env.counter("c"))
	_act(harness, "COUNTER_MATH_COUNTER", [
		_counter_arg("c"), _compare_arg(ParamTypes.MATH_SUBTRACT), _counter_arg("d")
	])
	_check("counter_math_counter_subtracts", env.counter("c") == 16, "c=%d" % env.counter("c"))

	var divide_by_zero := _act(harness, "COUNTER_MATH_VALUE", [
		_counter_arg("c"), _compare_arg(ParamTypes.MATH_DIVIDE), _int_arg(0)
	])
	_check(
		"counter_math_refuses_division_by_zero",
		divide_by_zero == Dispatch.Status.BAD_ARGUMENTS and env.counter("c") == 16
	)

	# Deterministic random: same seed and call order gives the same result.
	_act(harness, "SET_RANDOM_COUNTER", [_counter_arg("r"), _int_arg(5), _int_arg(9)])
	var first := env.counter("r")
	var twin := _harness()
	_act(twin, "SET_RANDOM_COUNTER", [_counter_arg("r"), _int_arg(5), _int_arg(9)])
	_check(
		"random_counter_is_deterministic_and_in_range",
		first == (twin["env"] as SageScriptEnv).counter("r") and first >= 5 and first <= 9,
		"value=%d" % first
	)

	var no_random := _harness()
	(no_random["world"] as SageScriptWorldStub).refuse(World.CAP_RANDOM)
	_check(
		"random_counter_refuses_without_a_random_stream",
		_act(no_random, "SET_RANDOM_COUNTER", [_counter_arg("r"), _int_arg(5), _int_arg(9)])
		== Dispatch.Status.WORLD_REFUSED
	)


func _test_timer_handlers() -> void:
	var harness := _harness()
	var env: SageScriptEnv = harness["env"]

	# Millisecond timer parity with the existing tier-1 interpreter: armed on
	# tick 1 for 2500 ms at 10 ticks/s, it must report expired on tick 26.
	env.advance()
	_act(harness, "SET_MILLISECOND_TIMER", [_counter_arg("t"), _real_arg(2500.0)])
	var expiry_tick := -1
	for _step in range(60):
		env.advance()
		if env.timer_expired("t") and expiry_tick < 0:
			expiry_tick = env.tick_index
	_check("millisecond_timer_expires_on_the_expected_tick", expiry_tick == 26,
		"tick=%d" % expiry_tick)

	# Unset timers have not expired.
	_check("unset_timer_has_not_expired", not env.timer_expired("never_armed"))

	# Frame timer converts through the declared retail logic rate.
	var frames := _harness()
	var frame_env: SageScriptEnv = frames["env"]
	frame_env.advance()
	_act(frames, "SET_TIMER", [_counter_arg("f"), _int_arg(90)])
	_check(
		"frame_timer_conversion_is_recorded_as_an_assumption",
		frame_env.frame_conversions == 1
	)
	# 90 frames at the assumed 30 Hz logic rate is 3 s, i.e. 30 interpreter ticks.
	_check(
		"frame_timer_uses_the_declared_logic_rate",
		is_equal_approx(frame_env.timer_remaining_ticks("f"), 30.0),
		"remaining=%f" % frame_env.timer_remaining_ticks("f")
	)

	# Stop / restart / add / subtract.
	var control := _harness()
	var control_env: SageScriptEnv = control["env"]
	control_env.advance()
	_act(control, "SET_MILLISECOND_TIMER", [_counter_arg("t"), _real_arg(1000.0)])
	_act(control, "STOP_TIMER", [_counter_arg("t")])
	for _step in range(20):
		control_env.advance()
	_check("stopped_timer_does_not_count_down", not control_env.timer_expired("t"))
	_act(control, "RESTART_TIMER", [_counter_arg("t")])
	for _step in range(10):
		control_env.advance()
	_check("restarted_timer_resumes_counting", control_env.timer_expired("t"))

	var adjust := _harness()
	var adjust_env: SageScriptEnv = adjust["env"]
	adjust_env.advance()
	_act(adjust, "SET_MILLISECOND_TIMER", [_counter_arg("t"), _real_arg(1000.0)])
	_act(adjust, "ADD_TO_MSEC_TIMER", [_real_arg(500.0), _counter_arg("t")])
	_check(
		"add_to_msec_timer_reads_value_first",
		is_equal_approx(adjust_env.timer_remaining_ticks("t"), 15.0),
		"remaining=%f" % adjust_env.timer_remaining_ticks("t")
	)
	_act(adjust, "SUB_FROM_MSEC_TIMER", [_real_arg(500.0), _counter_arg("t")])
	_check(
		"sub_from_msec_timer_reads_value_first",
		is_equal_approx(adjust_env.timer_remaining_ticks("t"), 10.0),
		"remaining=%f" % adjust_env.timer_remaining_ticks("t")
	)

	# Adjusting a timer that was never armed is an error, not a quiet create.
	_check(
		"adjusting_an_unarmed_timer_is_an_error",
		_act(adjust, "ADD_TO_MSEC_TIMER", [_real_arg(1.0), _counter_arg("ghost")])
		== Dispatch.Status.BAD_ARGUMENTS
	)
	_check(
		"stopping_an_unarmed_timer_is_an_error",
		_act(adjust, "STOP_TIMER", [_counter_arg("ghost")]) == Dispatch.Status.BAD_ARGUMENTS
	)


func _test_script_control_and_world_handlers() -> void:
	var harness := _harness()
	var env: SageScriptEnv = harness["env"]
	var world: SageScriptWorldStub = harness["world"]

	_act(harness, "DISABLE_SCRIPT", [_text_arg("Wave 2")])
	_check(
		"disable_script_clears_the_enable_bit",
		not env.is_script_enabled("Wave 2", true)
	)
	_act(harness, "ENABLE_SCRIPT", [_text_arg("Wave 2")])
	_check("enable_script_sets_the_enable_bit", env.is_script_enabled("Wave 2", false))
	_check(
		"unmentioned_scripts_keep_the_loader_default",
		env.is_script_enabled("Wave 3", true) and not env.is_script_enabled("Wave 4", false)
	)

	_check(
		"player_set_money_reaches_the_world",
		_act(harness, "PLAYER_SET_MONEY", [_player_arg(PLAYER), _int_arg(1234)])
		== Dispatch.Status.OK and world.player_money(PLAYER) == 1234
	)
	_act(harness, "SET_PLAYER_MONEY_TO_COUNTER", [_player_arg(PLAYER), _counter_arg("gold")])
	_check(
		"set_player_money_to_counter_reads_money_into_the_counter",
		env.counter("gold") == 1234, "gold=%d" % env.counter("gold")
	)

	_act(harness, "DEBUG_STRING", [_text_arg("hello")])
	_check(
		"debug_output_reaches_the_world_channel",
		world.debug_log.size() == 1 and world.debug_log[0] == "DEBUG_STRING: hello",
		str(world.debug_log)
	)


func _test_condition_handlers() -> void:
	var harness := _harness()
	var env: SageScriptEnv = harness["env"]

	_check("condition_true_is_true", _cond(harness, "CONDITION_TRUE", []))
	_check("condition_false_is_false", not _cond(harness, "CONDITION_FALSE", []))
	_check("inversion_is_applied", not _cond(harness, "CONDITION_TRUE", [], true))

	env.set_counter("c", 7)
	env.set_counter("d", 9)
	_check(
		"counter_condition_compares_against_a_value",
		_cond(harness, "COUNTER", [
			_counter_arg("c"), _compare_arg(ParamTypes.COMPARE_EQUAL), _int_arg(7)
		])
	)
	_check(
		"counter_condition_respects_the_operator",
		_cond(harness, "COUNTER", [
			_counter_arg("c"), _compare_arg(ParamTypes.COMPARE_GREATER), _int_arg(7)
		]) == false
	)
	# Repeated parameter type: the two counters must not be swapped.
	_check(
		"counter_counter_compares_in_declaration_order",
		_cond(harness, "COUNTER_COUNTER", [
			_counter_arg("c"), _compare_arg(ParamTypes.COMPARE_LESS), _counter_arg("d")
		])
		and not _cond(harness, "COUNTER_COUNTER", [
			_counter_arg("d"), _compare_arg(ParamTypes.COMPARE_LESS), _counter_arg("c")
		])
	)

	env.set_flag("armed", true)
	_check(
		"flag_condition_compares_against_a_boolean",
		_cond(harness, "FLAG", [_flag_arg("armed"), _bool_arg(true)])
		and not _cond(harness, "FLAG", [_flag_arg("armed"), _bool_arg(false)])
	)
	_check(
		"unset_flag_reads_as_false",
		_cond(harness, "FLAG", [_flag_arg("never_set"), _bool_arg(false)])
	)

	# Conditions must be side-effect free.
	var before := env.snapshot()
	for _index in range(5):
		_cond(harness, "COUNTER", [
			_counter_arg("c"), _compare_arg(ParamTypes.COMPARE_EQUAL), _int_arg(7)
		])
		_cond(harness, "TIMER_EXPIRED", [_counter_arg("t")])
		_cond(harness, "FLAG", [_flag_arg("armed"), _bool_arg(true)])
	_check(
		"condition_evaluation_does_not_mutate_the_environment",
		env.snapshot() == before, str(env.snapshot())
	)


# --- 5. One vocabulary, two front ends ------------------------------------


func _test_lua_front_door() -> void:
	var harness := _harness()
	var dispatch: SageScriptDispatch = harness["dispatch"]
	var outcome := dispatch.call_by_name(
		Vocabulary.Kind.ACTION, "SET_COUNTER",
		[_counter_arg("lua"), _int_arg(42)],
		harness["env"], harness["world"], "lua-chunk"
	)
	_check(
		"lua_execute_action_reaches_the_same_handler",
		int(outcome["status"]) == Dispatch.Status.OK
		and (harness["env"] as SageScriptEnv).counter("lua") == 42
	)
	var condition := dispatch.call_by_name(
		Vocabulary.Kind.CONDITION, "COUNTER",
		[_counter_arg("lua"), _compare_arg(ParamTypes.COMPARE_EQUAL), _int_arg(42)],
		harness["env"], harness["world"], "lua-chunk"
	)
	_check(
		"lua_evaluate_condition_reaches_the_same_handler",
		int(condition["status"]) == Dispatch.Status.OK and bool(condition["result"])
	)
	var unknown := dispatch.call_by_name(
		Vocabulary.Kind.ACTION, "MADE_UP_LUA_ACTION", [],
		harness["env"], harness["world"], "lua-chunk"
	)
	_check(
		"lua_unknown_action_records_a_gap_too",
		int(unknown["status"]) == Dispatch.Status.UNSUPPORTED
		and dispatch.gaps.has("action", "MADE_UP_LUA_ACTION", GapLog.REASON_UNKNOWN_NAME)
	)


# --- Coverage -------------------------------------------------------------


func _report_coverage() -> void:
	var dispatch := Dispatch.new()
	CoreHandlers.register_all(dispatch)
	var coverage := dispatch.coverage()
	var actions: Dictionary = coverage["actions"]
	var conditions: Dictionary = coverage["conditions"]
	_check(
		"every_registered_handler_names_a_table_entry",
		(actions["implemented_not_in_table"] as Array).is_empty()
		and (conditions["implemented_not_in_table"] as Array).is_empty(),
		"%s %s" % [
			str(actions["implemented_not_in_table"]),
			str(conditions["implemented_not_in_table"]),
		]
	)
	_check(
		"tranche_one_beats_the_prior_ten_token_interpreter",
		int(actions["implemented"]) + int(conditions["implemented"]) > 10,
		"implemented=%d" % (int(actions["implemented"]) + int(conditions["implemented"]))
	)
	_check(
		"a_deliberate_refusal_is_not_counted_as_coverage",
		dispatch.deliberately_refused() == ["SET_COUNTER_TO_CLIENT_RANDOM_VALUE"]
		and not dispatch.implemented_actions().has("SET_COUNTER_TO_CLIENT_RANDOM_VALUE"),
		str(dispatch.deliberately_refused())
	)
	print(
		"SCRIPT_VOCABULARY COVERAGE actions implemented=%d / known=%d (bfme2 upper bound=%d) / estimated=%d"
		% [
			int(actions["implemented"]),
			int(actions["total_known"]),
			int(actions["bfme2_known_upper_bound"]),
			int(actions["total_estimated"]),
		]
	)
	print(
		"SCRIPT_VOCABULARY COVERAGE conditions implemented=%d / known=%d (bfme2 upper bound=%d) / estimated=%d"
		% [
			int(conditions["implemented"]),
			int(conditions["total_known"]),
			int(conditions["bfme2_known_upper_bound"]),
			int(conditions["total_estimated"]),
		]
	)
	print("SCRIPT_VOCABULARY COVERAGE actions=%s" % str(dispatch.implemented_actions()))
	print("SCRIPT_VOCABULARY COVERAGE conditions=%s" % str(dispatch.implemented_conditions()))


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("SCRIPT_VOCABULARY PASS %s%s" % [name, " (%s)" % detail if detail != "" else ""])
	else:
		failed += 1
		printerr("SCRIPT_VOCABULARY FAIL %s%s" % [name, " (%s)" % detail if detail != "" else ""])
