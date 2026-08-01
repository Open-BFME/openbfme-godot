extends SceneTree

## Proof runner for WP06-audio-video.
##
## WHAT A PRESENTATION PACKAGE CAN BE TESTED AGAINST
## ================================================
## Nothing in this package has an observable simulation effect - that is its
## defining property. So the only honest assertion is against the AUDIO SINK
## RECORDING: dispatch action X with arguments A, and the stub's recording sink
## must hold exactly one call to op X carrying exactly A, positionally. See the
## PresentationSink comment in script_world.gd for why the recording is the
## design rather than a fallback.
##
## Covers, in order:
##   1. registration    - all 53 implemented actions and all 6 gap-registered
##                        members are claimed, and the package registers clean
##   2. exhaustive      - EVERY implemented action is dispatched with synthetic
##      round-trip        arguments built from its own declared signature, and
##                        the recording must reproduce them in order. 53 members
##                        asserted, not a hand-picked handful
##   3. ordering traps  - the five interchangeable REALs of AUDIO_FADE_VOLUME
##                        and the adjacent fadeout/fadein BOOLEANs, asserted
##                        with values chosen so a swap cannot pass
##   4. no simulation   - env is untouched, including the FLAG the
##      leakage           MUSIC_*_AND_NOTIFY family names
##   5. honest refusal  - a world with no audio sink refuses into a gap rather
##                        than pretending it played
##   6. blocked members - the 5 conditions and 1 action produce a
##                        `blocked-subsystem` gap naming their subsystem, are
##                        false / refused, and are not counted as coverage
##
## Synthetic values only. No retail content is loaded and no retail-sourced
## duration is pinned: a lane-authority question is open on retail script
## revisions, and a fixture that quoted one would go stale silently.
##
## Invocation:
##   Godot_v4.7-stable_win64_console.exe --headless --path game \
##     -s res://tests/handlers_wp06_audio_video_runner.gd

const Registry := preload("res://src/script/handlers/_registry.gd")
const Dispatch := preload("res://src/script/script_dispatch.gd")
const CoreHandlers := preload("res://src/script/script_handlers_core.gd")
const Vocabulary := preload("res://src/script/script_vocabulary.gd")
const Env := preload("res://src/script/script_env.gd")
const WorldStub := preload("res://src/script/script_world_stub.gd")
const GapLog := preload("res://src/script/script_gaps.gd")
const ParamTypes := preload("res://src/script/script_param_types.gd")
const Wp06 := preload("res://src/script/handlers/wp06_audio_video.gd")

## LIVENESS GUARD. A GDScript RUNTIME error aborts the enclosing function
## on the spot without propagating, so every later _check() in that function
## silently never runs and `failed` never moves - the runner then prints a
## zero-failure result and exits 0 with SCRIPT ERROR lines above it. This is
## the exact count a HEALTHY run makes; if the run makes any other number,
## something aborted (or an assertion was added without updating this) and
## the result is not to be trusted.
const EXPECTED_CHECKS := 42

var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	_test_registration()
	_test_every_action_round_trips_its_signature()
	_test_the_five_real_fade_arguments()
	_test_adjacent_fadeout_fadein_booleans()
	_test_name_before_value_signatures()
	_test_nothing_is_resolved_or_converted()
	_test_no_simulation_leakage()
	_test_a_world_without_an_audio_sink_refuses()
	_test_blocked_members()
	_report()
	var ran := passed + failed
	if ran != EXPECTED_CHECKS:
		failed += 1
		printerr("HANDLERS_WP06 FAIL liveness: ran %d checks, expected %d - a function aborted before its assertions" % [ran, EXPECTED_CHECKS])
	print("HANDLERS_WP06_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


# --- Fixture helpers ------------------------------------------------------


func _argument(
	argument_type: int, integer: int = 0, real: float = 0.0, text: String = ""
) -> Dictionary:
	return {"argumentType": argument_type, "integer": integer, "real": real, "text": text}


func _bool_arg(value: bool) -> Dictionary:
	return _argument(ParamTypes.ARGUMENT_BOOLEAN, 1 if value else 0)


func _real_arg(value: float) -> Dictionary:
	return _argument(ParamTypes.ARGUMENT_REAL, 0, value)


func _int_arg(value: int) -> Dictionary:
	return _argument(ParamTypes.ARGUMENT_INTEGER, value)


func _flag_arg(name: String) -> Dictionary:
	return _argument(ParamTypes.ARGUMENT_FLAG_NAME, 0, 0.0, name)


func _text_arg(value: String) -> Dictionary:
	# Parameter types this repo has observed no SCB code for are read from the
	# text field and their code is not asserted on by SageScriptArgs.validate().
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
	var outcome := Registry.register_all(dispatch)
	return {
		"dispatch": dispatch,
		"env": Env.new(),
		"world": WorldStub.new(20260726),
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


func _sink(harness: Dictionary) -> SageScriptWorldStub.RecordingSink:
	return (harness["world"] as SageScriptWorldStub).audio() as SageScriptWorldStub.RecordingSink


# --- 1. Registration ------------------------------------------------------


func _test_registration() -> void:
	var harness := _harness()
	var dispatch: SageScriptDispatch = harness["dispatch"]
	var outcome: Dictionary = harness["registration"]

	_check(
		"registration_reports_no_errors",
		(outcome["errors"] as Array).is_empty(),
		str(outcome["errors"])
	)
	_check(
		"the_package_is_present",
		(outcome["packages"] as Array).has(Wp06.PACKAGE),
		str(outcome["packages"])
	)

	var expected := Wp06.implemented_action_names()
	_check(
		"the_package_serves_fifty_three_actions",
		expected.size() == 53,
		"declared=%d" % expected.size()
	)

	var missing: Array[String] = []
	for name: String in expected:
		if not dispatch.action_handlers.has(name):
			missing.append(name)
	_check("every_implemented_action_is_registered", missing.is_empty(), str(missing))

	# All 53 must be COUNTED as coverage - a package whose members register but
	# do not count would be reporting work it did not do, in reverse.
	var implemented := dispatch.implemented_actions()
	var uncounted: Array[String] = []
	for name: String in expected:
		if not implemented.has(name):
			uncounted.append(name)
	_check("every_implemented_action_counts_as_coverage", uncounted.is_empty(), str(uncounted))

	# And the six gap-registered members must be claimed too, so a map using one
	# cannot fall through to a plain `unimplemented` gap.
	var blocked := dispatch.blocked_names()
	var unclaimed: Array[String] = []
	for name: Variant in (
		Wp06.BLOCKED_PLAYBACK_CONDITIONS
		+ Wp06.BLOCKED_MUSIC_STATE_CONDITIONS
		+ Wp06.BLOCKED_REINFORCEMENT_ACTIONS
	):
		if not blocked.has(String(name)):
			unclaimed.append(String(name))
	_check("every_gap_registered_member_is_claimed", unclaimed.is_empty(), str(unclaimed))
	_check(
		"the_package_accounts_for_all_fifty_nine_members",
		expected.size()
		+ Wp06.BLOCKED_PLAYBACK_CONDITIONS.size()
		+ Wp06.BLOCKED_MUSIC_STATE_CONDITIONS.size()
		+ Wp06.BLOCKED_REINFORCEMENT_ACTIONS.size() == 59
	)


# --- 2. Exhaustive signature round-trip -----------------------------------


func _synthetic_argument(param_type: String, index: int) -> Dictionary:
	## One synthetic argument for a declared parameter type. Values are chosen to
	## be DISTINCT PER INDEX, which is the whole point: if a handler read slot 3
	## where the signature says slot 1, the round-trip assertion below sees a
	## different value rather than an identical one.
	match param_type:
		"BOOLEAN", "BOOL":
			# Alternates with index, so a swapped pair of adjacent booleans is
			# always visible.
			return _bool_arg(index % 2 == 0)
		"INT", "PERCENT":
			return _int_arg(100 + index)
		"REAL", "SECONDS", "ANGLE":
			return _real_arg(float(index) + 0.5)
		"FLAG":
			return _flag_arg("flag_%d" % index)
	return _text_arg("%s_%d" % [param_type.to_lower(), index])


func _expected_value(param_type: String, index: int) -> Variant:
	## What the sink must receive for that synthetic argument. Written from the
	## PARAMETER TYPE, independently of the handler's own decoding, so this is an
	## assertion rather than a restatement of the implementation.
	match param_type:
		"BOOLEAN", "BOOL":
			return index % 2 == 0
		"INT", "PERCENT":
			return 100 + index
		"REAL", "SECONDS", "ANGLE":
			return float(index) + 0.5
		"FLAG":
			return "flag_%d" % index
	return "%s_%d" % [param_type.to_lower(), index]


func _test_every_action_round_trips_its_signature() -> void:
	## Drives all 53 implemented actions through the dispatcher with arguments
	## built from each one's OWN declared signature, and requires the recording
	## to reproduce them positionally. This is the assertion that a single shared
	## handler body is safe: every member is exercised, not a representative few.
	var harness := _harness()
	var sink := _sink(harness)

	var wrong_status: Array[String] = []
	var wrong_values: Array[String] = []
	var wrong_op: Array[String] = []
	var total_arguments := 0

	for name: String in Wp06.implemented_action_names():
		var entry := Vocabulary.entry_by_name(Vocabulary.Kind.ACTION, name)
		var params: Array = entry.get("params", [])
		var arguments: Array = []
		var expected: Array = []
		for index in range(params.size()):
			arguments.append(_synthetic_argument(String(params[index]), index))
			expected.append(_expected_value(String(params[index]), index))
		total_arguments += params.size()

		sink.clear()
		var status := _act(harness, name, arguments)
		if status != Dispatch.Status.OK:
			wrong_status.append("%s->%d" % [name, status])
			continue
		if sink.ops() != [name]:
			wrong_op.append("%s recorded %s" % [name, str(sink.ops())])
			continue
		if sink.values_for(name) != expected:
			wrong_values.append(
				"%s got %s want %s" % [name, str(sink.values_for(name)), str(expected)]
			)

	_check("every_action_is_served", wrong_status.is_empty(), str(wrong_status))
	_check("every_action_records_its_own_op_name_once", wrong_op.is_empty(), str(wrong_op))
	_check(
		"every_action_round_trips_its_arguments_positionally",
		wrong_values.is_empty(),
		str(wrong_values)
	)
	# A guard on the guard: if the signatures ever became empty, or a member
	# quietly lost an argument, the round-trip above would still pass while
	# asserting less than it claims to. 95 is the total declared parameter count
	# of this package's 53 implemented actions, counted from the vocabulary
	# tables independently of the code under test. It is pinned exactly rather
	# than as a floor, so a signature GAINING an argument is caught too.
	_check(
		"the_round_trip_carried_every_declared_argument",
		total_arguments == 95,
		"arguments asserted=%d expected=95" % total_arguments
	)


# --- 3. Ordering traps ----------------------------------------------------


func _test_the_five_real_fade_arguments() -> void:
	## AUDIO_FADE_VOLUME(REAL from, REAL to, REAL rise, REAL hold, REAL fall).
	## Five arguments of one type: no type-search can order these, and no
	## validation would catch a permutation. Five distinct values, in a
	## deliberately non-monotonic order so an accidental sort would not pass.
	var harness := _harness()
	var sink := _sink(harness)

	var status := _act(harness, "AUDIO_FADE_VOLUME", [
		_real_arg(0.25),  # from
		_real_arg(0.9),   # to
		_real_arg(4.0),   # rise seconds
		_real_arg(1.5),   # hold seconds
		_real_arg(7.0),   # fall seconds
	])
	_check("audio_fade_volume_is_served", status == Dispatch.Status.OK, "status=%d" % status)
	_check(
		"audio_fade_volume_keeps_all_five_reals_in_declaration_order",
		sink.values_for("AUDIO_FADE_VOLUME") == [0.25, 0.9, 4.0, 1.5, 7.0],
		str(sink.values_for("AUDIO_FADE_VOLUME"))
	)


func _test_adjacent_fadeout_fadein_booleans() -> void:
	## Eleven signatures in this package end in (BOOLEAN fadeout, BOOLEAN
	## fadein). Every fixture below sets them to DIFFERENT values, so a handler
	## that swapped the pair fails; a fixture using true/true would not.
	var harness := _harness()
	var sink := _sink(harness)

	# MUSIC_SET_TRACK(MUSIC, BOOLEAN fadeout, BOOLEAN fadein)
	_act(harness, "MUSIC_SET_TRACK", [
		_text_arg("Rohan_Theme"), _bool_arg(true), _bool_arg(false)
	])
	_check(
		"music_set_track_keeps_fadeout_before_fadein",
		sink.values_for("MUSIC_SET_TRACK") == ["Rohan_Theme", true, false],
		str(sink.values_for("MUSIC_SET_TRACK"))
	)

	# AUDIO_POP_MUSIC(BOOLEAN fadeout, BOOLEAN fadein) - no name to anchor on,
	# so position is the only thing distinguishing them.
	_act(harness, "AUDIO_POP_MUSIC", [_bool_arg(false), _bool_arg(true)])
	_check(
		"audio_pop_music_keeps_its_two_bare_booleans_in_order",
		sink.values_for("AUDIO_POP_MUSIC") == [false, true],
		str(sink.values_for("AUDIO_POP_MUSIC"))
	)

	# MUSIC_PLAY_TRACK_FINITE_TIMES_AND_NOTIFY(MUSIC, INT, BOOLEAN, BOOLEAN, FLAG)
	# The longest of the family: a name, a count, the boolean pair, and a FLAG
	# last. The FLAG must reach the recording even though nothing sets it.
	_act(harness, "MUSIC_PLAY_TRACK_FINITE_TIMES_AND_NOTIFY", [
		_text_arg("Siege_Loop"), _int_arg(3), _bool_arg(true), _bool_arg(false),
		_flag_arg("siege_music_done"),
	])
	_check(
		"and_notify_keeps_track_count_fadeout_fadein_flag_in_order",
		sink.values_for("MUSIC_PLAY_TRACK_FINITE_TIMES_AND_NOTIFY")
		== ["Siege_Loop", 3, true, false, "siege_music_done"],
		str(sink.values_for("MUSIC_PLAY_TRACK_FINITE_TIMES_AND_NOTIFY"))
	)

	# The music-scripting twin has an identical signature and a different name.
	# Collapsing the two families would show up here as the wrong op recorded.
	_act(harness, "MUSIC_SCRIPT_SET_TRACK", [
		_text_arg("Score_Cue"), _bool_arg(false), _bool_arg(true)
	])
	_check(
		"the_music_scripting_family_records_under_its_own_name",
		sink.values_for("MUSIC_SCRIPT_SET_TRACK") == ["Score_Cue", false, true]
		and sink.count_for("MUSIC_SCRIPT_SET_TRACK") == 1
		and sink.count_for("MUSIC_SET_TRACK") == 1,
		str(sink.ops())
	)


func _test_name_before_value_signatures() -> void:
	## AUDIO_OVERRIDE_VOLUME_TYPE(AUDIO, REAL) puts the NAME first and the value
	## second; AUDIO_SET_REVERB_SUPPRESSION_POLYGON(TRIGGER_AREA, PERCENT) does
	## the same with an integer percent. PERCENT is carried in the INTEGER field,
	## not the real field, so a handler reading it as a real would emit 0.0.
	var harness := _harness()
	var sink := _sink(harness)

	_act(harness, "AUDIO_OVERRIDE_VOLUME_TYPE", [_text_arg("OrcHorn"), _real_arg(35.0)])
	_check(
		"override_volume_type_reads_name_then_value",
		sink.values_for("AUDIO_OVERRIDE_VOLUME_TYPE") == ["OrcHorn", 35.0],
		str(sink.values_for("AUDIO_OVERRIDE_VOLUME_TYPE"))
	)

	_act(harness, "AUDIO_SET_REVERB_SUPPRESSION_POLYGON", [_text_arg("Cave01"), _int_arg(40)])
	_check(
		"reverb_suppression_reads_percent_from_the_integer_field",
		sink.values_for("AUDIO_SET_REVERB_SUPPRESSION_POLYGON") == ["Cave01", 40],
		str(sink.values_for("AUDIO_SET_REVERB_SUPPRESSION_POLYGON"))
	)

	# A no-argument action must record an EMPTY list, not a list with a stray
	# slot in it.
	_act(harness, "TOGGLE_AVI_CAPTURE", [])
	_check(
		"a_no_argument_action_records_an_empty_value_list",
		sink.count_for("TOGGLE_AVI_CAPTURE") == 1
		and sink.values_for("TOGGLE_AVI_CAPTURE") == [],
		str(sink.values_for("TOGGLE_AVI_CAPTURE"))
	)

	# Arity is enforced before the handler runs, and the handler refuses again if
	# the signature and record ever disagree. Either way it is never emitted.
	sink.clear()
	var short_status := _act(harness, "AUDIO_FADE_VOLUME", [_real_arg(1.0)])
	_check(
		"a_short_argument_list_is_rejected_and_never_emitted",
		short_status == Dispatch.Status.BAD_ARGUMENTS and sink.records.is_empty(),
		"status=%d recorded=%s" % [short_status, str(sink.ops())]
	)


func _test_nothing_is_resolved_or_converted() -> void:
	## The sink receives the map's own arguments. A waypoint stays a NAME (it is
	## not looked up into a position), a team stays a NAME (it is not expanded
	## into a roster), and a duration stays in the units the map wrote. Resolving
	## any of them would mean reading the simulation from a presentation handler.
	var harness := _harness()
	var sink := _sink(harness)

	_act(harness, "PLAY_SOUND_EFFECT_AT", [_text_arg("HornBlast"), _text_arg("wp_gate")])
	_check(
		"play_sound_effect_at_emits_the_waypoint_name_not_a_position",
		sink.values_for("PLAY_SOUND_EFFECT_AT") == ["HornBlast", "wp_gate"],
		str(sink.values_for("PLAY_SOUND_EFFECT_AT"))
	)

	_act(harness, "PLAY_SOUND_EFFECT_AT_TEAM", [_text_arg("Charge"), _text_arg("teamRiders")])
	_check(
		"play_sound_effect_at_team_emits_the_team_name_not_a_roster",
		sink.values_for("PLAY_SOUND_EFFECT_AT_TEAM") == ["Charge", "teamRiders"],
		str(sink.values_for("PLAY_SOUND_EFFECT_AT_TEAM"))
	)

	# DISPLAY_CINEMATIC_TEXT(LOCALIZED_TEXT, FONT_TYPE, INT seconds). The 6 stays
	# 6 - it is NOT converted to the 60 interpreter ticks a facet would want,
	# because a sink is not a facet and 60 appears in no map.
	_act(harness, "DISPLAY_CINEMATIC_TEXT", [
		_text_arg("OBJECTIVE_HOLD_THE_GATE"), _text_arg("CineFont"), _int_arg(6)
	])
	_check(
		"cinematic_text_seconds_are_not_converted_to_ticks",
		sink.values_for("DISPLAY_CINEMATIC_TEXT")
		== ["OBJECTIVE_HOLD_THE_GATE", "CineFont", 6],
		str(sink.values_for("DISPLAY_CINEMATIC_TEXT"))
	)


# --- 4. No simulation leakage ---------------------------------------------


func _test_no_simulation_leakage() -> void:
	## The package-defining property: after driving every implemented action, the
	## interpreter state must be exactly as it started.
	var harness := _harness()
	var env: SageScriptEnv = harness["env"]

	for name: String in Wp06.implemented_action_names():
		var params: Array = Vocabulary.entry_by_name(Vocabulary.Kind.ACTION, name).get("params", [])
		var arguments: Array = []
		for index in range(params.size()):
			arguments.append(_synthetic_argument(String(params[index]), index))
		_act(harness, name, arguments)

	var snapshot := env.snapshot()
	_check(
		"driving_every_action_left_the_interpreter_state_empty",
		snapshot["counters"] == {} and snapshot["flags"] == {},
		"counters=%s flags=%s" % [str(snapshot["counters"]), str(snapshot["flags"])]
	)

	# Specifically the MUSIC_*_AND_NOTIFY family, which is DOCUMENTED as setting
	# a flag when the track finishes. It must not set it now (the track has not
	# played) and must not set it later from local playback timing (that would
	# feed lockstep state from the audio device). The flag name reaching the
	# recording is what keeps the limitation visible instead of silent.
	var notify := _harness()
	_act(notify, "MUSIC_PUSH_TRACK_FINITE_TIMES_AND_NOTIFY", [
		_text_arg("Coronation"), _int_arg(1), _bool_arg(true), _bool_arg(false),
		_flag_arg("coronation_done"),
	])
	_check(
		"and_notify_does_not_set_its_flag_from_a_presentation_handler",
		not (notify["env"] as SageScriptEnv).flag("coronation_done"),
		"flag was set"
	)
	_check(
		"but_the_flag_name_is_visible_in_the_recording",
		(_sink(notify).values_for("MUSIC_PUSH_TRACK_FINITE_TIMES_AND_NOTIFY") as Array)
			.has("coronation_done"),
		str(_sink(notify).values_for("MUSIC_PUSH_TRACK_FINITE_TIMES_AND_NOTIFY"))
	)


# --- 5. Honest refusal ----------------------------------------------------


func _test_a_world_without_an_audio_sink_refuses() -> void:
	## The BASE SageScriptWorld has no presentation adapter wired. Every action
	## in this package must then produce a structured gap rather than reporting
	## that it played a sound it did not play.
	var bare := Dispatch.new()
	Registry.register_all(bare)
	var world := SageScriptWorld.new()
	var env := Env.new()

	var not_refused: Array[String] = []
	for name: String in Wp06.implemented_action_names():
		var params: Array = Vocabulary.entry_by_name(Vocabulary.Kind.ACTION, name).get("params", [])
		var arguments: Array = []
		for index in range(params.size()):
			arguments.append(_synthetic_argument(String(params[index]), index))
		if bare.execute_action(_record(name, arguments), env, world, "fixture") \
				!= Dispatch.Status.WORLD_REFUSED:
			not_refused.append(name)
	_check(
		"every_action_refuses_when_no_audio_sink_is_wired",
		not_refused.is_empty(),
		str(not_refused)
	)
	_check(
		"the_refusal_is_recorded_as_a_world_refused_gap",
		bare.gaps.has("action", "PLAY_SOUND_EFFECT", GapLog.REASON_WORLD_REFUSED),
		str(bare.gaps.to_lines())
	)
	_check(
		"the_gap_names_the_missing_presentation_sink",
		String(
			bare.gaps.entries[
				"action|PLAY_SOUND_EFFECT|%s" % GapLog.REASON_WORLD_REFUSED
			]["detail"]
		).contains("audio"),
		str(bare.gaps.to_lines())
	)


# --- 6. Gap-registered members --------------------------------------------


func _test_blocked_members() -> void:
	var harness := _harness()
	var dispatch: SageScriptDispatch = harness["dispatch"]

	# CONDITIONS. The rule that matters: a condition that could not be evaluated
	# must never masquerade as a genuine `false`. It is answered false - the same
	# CONDITION_FALLBACK any unevaluable condition gets, so an unevaluable gate
	# never fires - but it ALSO leaves a gap naming itself, which is what makes
	# it distinguishable from a real false at the point it matters: the log.
	_check(
		"has_finished_audio_is_false",
		not _cond(harness, "HAS_FINISHED_AUDIO", [_text_arg("HornBlast")])
	)
	_check(
		"has_finished_audio_records_a_blocked_gap_rather_than_answering",
		dispatch.gaps.has("condition", "HAS_FINISHED_AUDIO", GapLog.REASON_BLOCKED_SUBSYSTEM)
		and not dispatch.gaps.has(
			"condition", "HAS_FINISHED_AUDIO", GapLog.REASON_UNIMPLEMENTED
		),
		str(dispatch.gaps.to_lines())
	)

	# Inversion must not turn an unevaluable condition into a true. This is the
	# ok-propagation rule at its sharpest: `NOT (could not evaluate)` is still
	# not an answer, and evaluate_condition returns the fallback REGARDLESS of
	# the inverted flag. A gate reading "if the speech has NOT finished" must not
	# fire just because the question was unanswerable.
	_check(
		"an_inverted_blocked_condition_is_still_false",
		not (dispatch.evaluate_condition(
			_record("HAS_FINISHED_SPEECH", [_text_arg("Gandalf_Line01")], true),
			harness["env"], harness["world"], "fixture"
		))
	)

	_check(
		"has_finished_video_is_false_and_gapped",
		not _cond(harness, "HAS_FINISHED_VIDEO", [_text_arg("Intro")])
		and dispatch.gaps.has(
			"condition", "HAS_FINISHED_VIDEO", GapLog.REASON_BLOCKED_SUBSYSTEM
		)
	)
	_check(
		"music_track_has_completed_is_false_and_gapped",
		not _cond(harness, "MUSIC_TRACK_HAS_COMPLETED", [_text_arg("Theme"), _int_arg(2)])
		and dispatch.gaps.has(
			"condition", "MUSIC_TRACK_HAS_COMPLETED", GapLog.REASON_BLOCKED_SUBSYSTEM
		)
	)
	_check(
		"music_is_playing_from_script_is_false_and_gapped",
		not _cond(harness, "MUSIC_IS_PLAYING_FROM_SCRIPT", [])
		and dispatch.gaps.has(
			"condition", "MUSIC_IS_PLAYING_FROM_SCRIPT", GapLog.REASON_BLOCKED_SUBSYSTEM
		)
	)

	# The two condition groups carry DIFFERENT subsystem strings, because the
	# obstacles are different and a single generic string would lose that.
	_check(
		"the_playback_gap_names_the_playback_subsystem",
		String(
			dispatch.gaps.entries[
				"condition|HAS_FINISHED_AUDIO|%s" % GapLog.REASON_BLOCKED_SUBSYSTEM
			]["detail"]
		).contains("playback"),
		str(dispatch.gaps.to_lines())
	)
	_check(
		"the_music_state_gap_names_the_music_scripting_subsystem",
		String(
			dispatch.gaps.entries[
				"condition|MUSIC_IS_PLAYING_FROM_SCRIPT|%s" % GapLog.REASON_BLOCKED_SUBSYSTEM
			]["detail"]
		).contains("music-scripting"),
		str(dispatch.gaps.to_lines())
	)

	# THE ACTION. CALL_IN_REINFORCEMENTS_WITHOUT_MOVIE is simulation, not
	# presentation. The assertion that matters is that it did NOT reach the audio
	# sink: emitting it there because its name mentions a movie would look like
	# success and call in no reinforcements.
	var status := _act(harness, "CALL_IN_REINFORCEMENTS_WITHOUT_MOVIE", [_text_arg("Army_Rohan")])
	_check(
		"call_in_reinforcements_without_movie_reports_blocked",
		status == Dispatch.Status.BLOCKED,
		"status=%d" % status
	)
	_check(
		"call_in_reinforcements_without_movie_never_reaches_the_audio_sink",
		_sink(harness).count_for("CALL_IN_REINFORCEMENTS_WITHOUT_MOVIE") == 0,
		str(_sink(harness).ops())
	)
	_check(
		"the_reinforcement_gap_names_the_campaign_subsystem",
		String(
			dispatch.gaps.entries[
				"action|CALL_IN_REINFORCEMENTS_WITHOUT_MOVIE|%s"
				% GapLog.REASON_BLOCKED_SUBSYSTEM
			]["detail"]
		).contains("reinforcement"),
		str(dispatch.gaps.to_lines())
	)

	# None of the six may be counted as coverage.
	var counted: Array[String] = []
	for name: Variant in (
		Wp06.BLOCKED_PLAYBACK_CONDITIONS
		+ Wp06.BLOCKED_MUSIC_STATE_CONDITIONS
		+ Wp06.BLOCKED_REINFORCEMENT_ACTIONS
	):
		if dispatch.implemented_actions().has(String(name)) \
				or dispatch.implemented_conditions().has(String(name)):
			counted.append(String(name))
	_check("no_gap_registered_member_counts_as_coverage", counted.is_empty(), str(counted))


# --- Reporting ------------------------------------------------------------


func _report() -> void:
	var harness := _harness()
	var dispatch: SageScriptDispatch = harness["dispatch"]
	print(
		"HANDLERS_WP06 MEMBERS implemented=%d blocked=%d total=%d"
		% [
			Wp06.implemented_action_names().size(),
			Wp06.BLOCKED_PLAYBACK_CONDITIONS.size()
			+ Wp06.BLOCKED_MUSIC_STATE_CONDITIONS.size()
			+ Wp06.BLOCKED_REINFORCEMENT_ACTIONS.size(),
			59,
		]
	)
	print("HANDLERS_WP06 PACKAGES %s" % str((harness["registration"] as Dictionary)["packages"]))
	var coverage := dispatch.coverage()
	print(
		"HANDLERS_WP06 COVERAGE actions implemented=%d conditions implemented=%d blocked=%d"
		% [
			int((coverage["actions"] as Dictionary)["implemented"]),
			int((coverage["conditions"] as Dictionary)["implemented"]),
			(coverage["blocked_on_subsystem"] as Array).size(),
		]
	)


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("HANDLERS_WP06 PASS %s%s" % [name, " (%s)" % detail if detail != "" else ""])
	else:
		failed += 1
		printerr("HANDLERS_WP06 FAIL %s%s" % [name, " (%s)" % detail if detail != "" else ""])
