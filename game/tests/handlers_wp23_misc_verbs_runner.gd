extends SceneTree

## Proof runner for WP23-misc-verbs (wp23_misc_verbs.gd) - the six remaining
## misc unit/player script actions: UNIT_SET_MODELCONDITION_FOR_DURATION,
## PLAYER_RELATES_PLAYER, DESELECT, IDLE_ALL_UNITS, PLAYER_FORCE_EMOTION and
## FIND_HOME_BASE_OF_PLAYER.
##
## Covers, in order:
##   1. registration    - all 6 actions registered by the package and counted
##                        as coverage, with no registry errors
##   2. per action      - the served path against a stub-taught world with the
##                        exact facet call (arguments in world-signature order)
##                        asserted verbatim, plus the refusal path: a world that
##                        cannot serve the call is WORLD_REFUSED with a
##                        structured gap naming the facet method - never OK
##   3. value guards    - the two inexpressible authorings refuse LOUDLY:
##                        a modelcondition PERCENT other than 100 (the facet has
##                        no percent slot; 100 is the only identity value) and a
##                        0-second duration (the facet's <= 0 sentinel means
##                        INDEFINITE, so serving 0 would invert a momentary
##                        flash into a forever-condition); negative durations
##                        are BAD_ARGUMENTS
##   4. the sink route  - DESELECT is presentation (selection is deliberately
##                        excluded from the sim state_hash - see WP07): it goes
##                        to the UI presentation sink with zero values, and a
##                        world with no sink refuses rather than pretending
##   5. the bind route  - FIND_HOME_BASE_OF_PLAYER queries players.home_base
##                        and BINDS the answer through units.set_reference;
##                        both legs' refusals surface, and a refused query
##                        binds nothing
##   6. arity           - one wrong-arity case per action, rejected before the
##                        handler runs
##
## Every fixture value is SYNTHETIC. No retail payload text is reproduced.
##
## Invocation:
##   <godot> --headless --path game \
##     --script res://tests/handlers_wp23_misc_verbs_runner.gd

const Registry := preload("res://src/script/handlers/_registry.gd")
const Dispatch := preload("res://src/script/script_dispatch.gd")
const CoreHandlers := preload("res://src/script/script_handlers_core.gd")
const Env := preload("res://src/script/script_env.gd")
const GapLog := preload("res://src/script/script_gaps.gd")
const ParamTypes := preload("res://src/script/script_param_types.gd")
const Watchdog := preload("res://tests/runner_watchdog.gd")

## The 6 actions this package serves. Written out rather than derived from the
## module, so a member quietly disappearing from register() fails here.
const SERVED_ACTIONS := [
	"UNIT_SET_MODELCONDITION_FOR_DURATION",
	"PLAYER_RELATES_PLAYER",
	"DESELECT",
	"IDLE_ALL_UNITS",
	"PLAYER_FORCE_EMOTION",
	"FIND_HOME_BASE_OF_PLAYER",
]

## IDLE_ALL_UNITS authors NO player argument - retail idles every unit on the
## map - so the handler must fan out over the retail all-players token and let
## the WORLD own its resolution (wp20 precedent for "<This Player>"). Asserted
## verbatim below.
const ALL_PLAYERS := "<All Players>"

## SageScriptEnv default is 10 ticks per second (ticks_from_seconds rounds up),
## so 6.0 s -> 60 ticks and 2.5 s -> 25 ticks. Pinned here so a silent change
## to the default timebase fails this runner rather than drifting the fixtures.
const TICKS_FOR_6_SECONDS := 60
const TICKS_FOR_2_5_SECONDS := 25

## LIVENESS GUARD. A GDScript RUNTIME error aborts the enclosing function on
## the spot without propagating, so every later _check() in that function
## silently never runs. A healthy run makes exactly this many checks; any
## other number means something aborted (or an assertion was added without
## updating this) and the result is not to be trusted.
const EXPECTED_CHECKS := 54

var passed := 0
var failed := 0
var _watchdog := Watchdog.new()

## Every world this run created, so facet<->world reference cycles can be
## broken before quit (see the Facet class comment in script_world.gd: a
## dropped world with live facets is a RefCounted cycle that leaks and can AV
## on Windows teardown).
var _worlds: Array = []


func _initialize() -> void:
	_watchdog.start(self, "HANDLERS_WP23", 0, 0, true)
	_watchdog.set_result_provider(func() -> Vector2i: return Vector2i(passed, failed))
	call_deferred("_run")


func _run() -> void:
	_test_registration_covers_the_package()
	_test_modelcondition_for_duration_serves_positionally()
	_test_modelcondition_percent_other_than_100_is_refused()
	_test_modelcondition_zero_duration_is_refused_not_made_indefinite()
	_test_modelcondition_negative_duration_is_bad_arguments()
	_test_modelcondition_world_refusal_is_not_ok()
	_test_relates_player_serves_positionally()
	_test_relates_player_world_refusal_is_not_ok()
	_test_deselect_emits_on_the_ui_sink()
	_test_deselect_without_a_sink_refuses()
	_test_idle_all_units_fans_out_over_all_players()
	_test_idle_all_units_world_refusal_is_not_ok()
	_test_force_emotion_serves_with_ticks()
	_test_force_emotion_negative_duration_is_bad_arguments()
	_test_force_emotion_world_refusal_is_not_ok()
	_test_find_home_base_binds_the_answer()
	_test_find_home_base_query_refusal_binds_nothing()
	_test_find_home_base_bind_refusal_surfaces()
	_test_arity_is_enforced_per_action()
	var ran := passed + failed
	if ran != EXPECTED_CHECKS:
		failed += 1
		printerr(
			"HANDLERS_WP23 FAIL liveness: ran %d checks, expected %d - a function aborted before its assertions"
			% [ran, EXPECTED_CHECKS]
		)
	print("HANDLERS_WP23_RESULT passed=%d failed=%d" % [passed, failed])
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
	# UNIT, MODEL_CONDITION and UNIT_REF have no observed wire code in this
	# repo, so SageScriptArgs does not assert one. The integer field carries a
	# DECOY: a handler that read the wrong field would get 424242 instead of
	# the name, loudly rather than plausibly.
	return _argument(99, 424242, 0.0, value)


func _player_arg(value: String) -> Dictionary:
	return _argument(ParamTypes.ARGUMENT_PLAYER, 424242, 0.0, value)


func _enum_arg(value: int) -> Dictionary:
	# RELATION / EMOTION / PERCENT: integer payload, no observed wire code. The
	# text field carries a decoy so a text-field read is loud.
	return _argument(99, value, 424242.0, "DECOY_TEXT")


func _real_arg(value: float) -> Dictionary:
	# REAL is carried in the `real` payload field (wire code 1, observed). The
	# integer decoy catches a wrong-field read: 424242 seconds of duration
	# would produce an absurd tick count in the logged world call below.
	return _argument(ParamTypes.ARGUMENT_REAL, 424242, value, "DECOY_TEXT")


func _boolean_arg(value: bool) -> Dictionary:
	return _argument(ParamTypes.ARGUMENT_BOOLEAN, 1 if value else 0, 424242.0, "DECOY_TEXT")


func _record(name: String, arguments: Array) -> Dictionary:
	return {
		"contentType": 0,
		"internalName": {"name": name, "wireTypeCode": 3},
		"arguments": arguments,
		"enabled": true,
		"inverted": false,
	}


func _harness(world: SageScriptWorld = null) -> Dictionary:
	var dispatch := Dispatch.new()
	CoreHandlers.register_all(dispatch)
	var outcome := Registry.register_all(dispatch)
	if world == null:
		world = MiscWorld.new(20260808)
	_worlds.append(world)
	return {
		"dispatch": dispatch,
		"env": Env.new(),
		"world": world,
		"registration": outcome,
	}


func _act(harness: Dictionary, name: String, arguments: Array) -> int:
	return (harness["dispatch"] as SageScriptDispatch).execute_action(
		_record(name, arguments), harness["env"], harness["world"], "fixture"
	)


func _gap_detail(
	dispatch: SageScriptDispatch, kind: String, name: String, reason: String
) -> String:
	## The gap detail recorded for `kind|name|reason`, or "" if no such gap
	## exists. Returns a String rather than indexing the entry dictionary
	## directly so that a MISSING gap fails the assertion that wanted it,
	## instead of throwing and taking the rest of the run down.
	var key := "%s|%s|%s" % [kind, name, reason]
	if not dispatch.gaps.entries.has(key):
		return ""
	return String((dispatch.gaps.entries[key] as Dictionary)["detail"])


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
		"the_wp23_package_is_present",
		(outcome["packages"] as Array).has("WP23-misc-verbs"),
		str(outcome["packages"])
	)

	var missing: Array[String] = []
	for name: String in SERVED_ACTIONS:
		if not dispatch.action_handlers.has(name):
			missing.append(name)
	_check("every_served_action_is_registered", missing.is_empty(), str(missing))

	var uncounted: Array[String] = []
	var implemented := dispatch.implemented_actions()
	for name: String in SERVED_ACTIONS:
		if not implemented.has(name):
			uncounted.append(name)
	_check(
		"every_served_action_counts_as_coverage", uncounted.is_empty(), str(uncounted)
	)


# --- 2/3. UNIT_SET_MODELCONDITION_FOR_DURATION -------------------------------


func _test_modelcondition_for_duration_serves_positionally() -> void:
	# UNIT_SET_MODELCONDITION_FOR_DURATION(UNIT, MODEL_CONDITION, REAL, PERCENT)
	#   "UNIT_SET_MODELCONDITION_FOR_DURATION|UNIT,MODEL_CONDITION,REAL,PERCENT|Unit|SHARED|S"
	#   (script_action_table.gd:925).
	# The two leading names are both text-carried, so only POSITION separates
	# subject from condition; the fixture names are mutually distinguishable in
	# the logged call so a swapped read produces a DIFFERENT string. Duration
	# 6.0 s must arrive as 60 ticks with enabled=true (a FOR_DURATION set is an
	# enable), and the identity percent 100 must be consumed by the handler,
	# never forwarded.
	var harness := _harness()
	var world: MiscWorld = harness["world"]
	var dispatch: SageScriptDispatch = harness["dispatch"]

	var status := _act(harness, "UNIT_SET_MODELCONDITION_FOR_DURATION", [
		_name_arg("Synthetic_Subject"),
		_name_arg("SYNTHETIC_MC_FLAILING"),
		_real_arg(6.0),
		_enum_arg(100),
	])
	_check(
		"a_100_percent_duration_set_dispatches_ok",
		status == Dispatch.Status.OK,
		"status=%d gaps=%s" % [status, str(dispatch.gaps.to_lines())]
	)
	_check(
		"the_subject_condition_enable_and_ticks_arrive_in_facet_order",
		world.calls == [
			"units.set_model_condition|Synthetic_Subject|SYNTHETIC_MC_FLAILING|true|%d"
			% TICKS_FOR_6_SECONDS
		],
		str(world.calls)
	)
	_check(
		"the_served_set_records_no_gap",
		not dispatch.gaps.has(
			"action", "UNIT_SET_MODELCONDITION_FOR_DURATION", GapLog.REASON_WORLD_REFUSED
		),
		str(dispatch.gaps.to_lines())
	)


func _test_modelcondition_percent_other_than_100_is_refused() -> void:
	# The facet surface units.set_model_condition(object_name, condition,
	# enabled, duration_ticks) has NO percent slot. 100 is the identity value
	# under either sourced reading of PERCENT (whole subject / full intensity);
	# any other value would change the outcome the map authored, and dropping
	# an outcome-changing argument is forbidden - so it refuses, naming the
	# missing slot, and never reaches the world.
	var harness := _harness()
	var world: MiscWorld = harness["world"]
	var dispatch: SageScriptDispatch = harness["dispatch"]

	var status := _act(harness, "UNIT_SET_MODELCONDITION_FOR_DURATION", [
		_name_arg("Synthetic_Subject"),
		_name_arg("SYNTHETIC_MC_FLAILING"),
		_real_arg(6.0),
		_enum_arg(40),
	])
	_check(
		"a_non_identity_percent_is_world_refused_not_ok",
		status == Dispatch.Status.WORLD_REFUSED,
		"status=%d" % status
	)
	_check(
		"the_percent_refusal_names_the_missing_slot",
		_gap_detail(
			dispatch, "action", "UNIT_SET_MODELCONDITION_FOR_DURATION",
			GapLog.REASON_WORLD_REFUSED
		).contains("percent"),
		str(dispatch.gaps.to_lines())
	)
	_check(
		"the_refused_percent_never_reached_the_world",
		world.calls.is_empty(),
		str(world.calls)
	)


func _test_modelcondition_zero_duration_is_refused_not_made_indefinite() -> void:
	# units.set_model_condition documents duration_ticks <= 0 (with enabled)
	# as the INDEFINITE variant, so an authored 0-second flash cannot be
	# expressed: converting it would turn a momentary condition into a
	# forever-condition - an inversion, not an approximation (the same verdict
	# WP11 applied to TEAM_GUARD_FOR_SECONDS at 0).
	var harness := _harness()
	var world: MiscWorld = harness["world"]
	var dispatch: SageScriptDispatch = harness["dispatch"]

	var status := _act(harness, "UNIT_SET_MODELCONDITION_FOR_DURATION", [
		_name_arg("Synthetic_Subject"),
		_name_arg("SYNTHETIC_MC_FLAILING"),
		_real_arg(0.0),
		_enum_arg(100),
	])
	_check(
		"a_zero_second_duration_is_world_refused_not_made_indefinite",
		status == Dispatch.Status.WORLD_REFUSED,
		"status=%d" % status
	)
	_check(
		"the_zero_duration_refusal_names_the_sentinel",
		_gap_detail(
			dispatch, "action", "UNIT_SET_MODELCONDITION_FOR_DURATION",
			GapLog.REASON_WORLD_REFUSED
		).contains("indefinite"),
		str(dispatch.gaps.to_lines())
	)
	_check(
		"the_refused_zero_duration_never_reached_the_world",
		world.calls.is_empty(),
		str(world.calls)
	)


func _test_modelcondition_negative_duration_is_bad_arguments() -> void:
	var harness := _harness()
	var world: MiscWorld = harness["world"]

	_check(
		"a_negative_duration_is_bad_arguments",
		_act(harness, "UNIT_SET_MODELCONDITION_FOR_DURATION", [
			_name_arg("Synthetic_Subject"),
			_name_arg("SYNTHETIC_MC_FLAILING"),
			_real_arg(-2.0),
			_enum_arg(100),
		]) == Dispatch.Status.BAD_ARGUMENTS
	)
	_check(
		"the_negative_duration_never_reached_the_world",
		world.calls.is_empty(),
		str(world.calls)
	)


func _test_modelcondition_world_refusal_is_not_ok() -> void:
	# The BASE SageScriptWorld models nothing: every Units command inherits the
	# refusing base. Dispatching against it must produce WORLD_REFUSED plus a
	# structured gap naming the facet method, never OK.
	var harness := _harness(SageScriptWorld.new())
	var dispatch: SageScriptDispatch = harness["dispatch"]

	var status := _act(harness, "UNIT_SET_MODELCONDITION_FOR_DURATION", [
		_name_arg("Synthetic_Subject"),
		_name_arg("SYNTHETIC_MC_FLAILING"),
		_real_arg(6.0),
		_enum_arg(100),
	])
	_check(
		"a_refused_modelcondition_set_is_world_refused_not_ok",
		status == Dispatch.Status.WORLD_REFUSED,
		"status=%d" % status
	)
	_check(
		"the_modelcondition_refusal_names_the_units_facet_method",
		_gap_detail(
			dispatch, "action", "UNIT_SET_MODELCONDITION_FOR_DURATION",
			GapLog.REASON_WORLD_REFUSED
		).contains("units.set_model_condition"),
		str(dispatch.gaps.to_lines())
	)


# --- 2. PLAYER_RELATES_PLAYER ------------------------------------------------


func _test_relates_player_serves_positionally() -> void:
	# PLAYER_RELATES_PLAYER(PLAYER, PLAYER, RELATION)
	#   "PLAYER_RELATES_PLAYER|PLAYER,PLAYER,RELATION|Player|SHARED|S"
	#   (script_action_table.gd:515).
	# TWO PLAYERS, AND THE ORDER DECIDES WHOSE DIPLOMACY ROW IS WRITTEN:
	# argument 0 is the SUBJECT whose stance changes, argument 1 the player it
	# is now felt toward. Both are PLAYER-typed strings, so nothing but
	# position separates them; the fixture names differ so a swap is a
	# different logged string. Relation 2 is the sourced "Friend".
	var harness := _harness()
	var world: MiscWorld = harness["world"]
	var dispatch: SageScriptDispatch = harness["dispatch"]

	var status := _act(harness, "PLAYER_RELATES_PLAYER", [
		_player_arg("Synthetic_Player_1"),
		_player_arg("Synthetic_Player_2"),
		_enum_arg(2),
	])
	_check(
		"the_relation_write_dispatches_ok",
		status == Dispatch.Status.OK,
		"status=%d gaps=%s" % [status, str(dispatch.gaps.to_lines())]
	)
	_check(
		"subject_other_and_relation_arrive_in_facet_order",
		world.calls == ["players.set_relation_to|Synthetic_Player_1|Synthetic_Player_2|2"],
		str(world.calls)
	)
	_check(
		"the_served_relation_write_records_no_gap",
		not dispatch.gaps.has(
			"action", "PLAYER_RELATES_PLAYER", GapLog.REASON_WORLD_REFUSED
		),
		str(dispatch.gaps.to_lines())
	)


func _test_relates_player_world_refusal_is_not_ok() -> void:
	var harness := _harness(SageScriptWorld.new())
	var dispatch: SageScriptDispatch = harness["dispatch"]

	var status := _act(harness, "PLAYER_RELATES_PLAYER", [
		_player_arg("Synthetic_Player_1"),
		_player_arg("Synthetic_Player_2"),
		_enum_arg(2),
	])
	_check(
		"a_refused_relation_write_is_world_refused_not_ok",
		status == Dispatch.Status.WORLD_REFUSED,
		"status=%d" % status
	)
	_check(
		"the_relation_refusal_names_the_players_facet_method",
		_gap_detail(
			dispatch, "action", "PLAYER_RELATES_PLAYER", GapLog.REASON_WORLD_REFUSED
		).contains("players.set_relation_to"),
		str(dispatch.gaps.to_lines())
	)


# --- 4. DESELECT -------------------------------------------------------------


func _test_deselect_emits_on_the_ui_sink() -> void:
	# DESELECT() - "DESELECT|-|Player|SHARED|S" (script_action_table.gd:183).
	# Selection is per-seat presentation state (the sim deliberately excludes
	# it from state_hash - WP07's OBJECT_FORCE_SELECT verdict), and the one
	# facet that touches selection, units.set_selected, takes an OBJECT NAME
	# this zero-argument action does not author. So it emits on the UI
	# presentation sink under its own InternalName with zero values.
	var harness := _harness()
	var world: MiscWorld = harness["world"]
	var dispatch: SageScriptDispatch = harness["dispatch"]

	var status := _act(harness, "DESELECT", [])
	_check(
		"deselect_dispatches_ok",
		status == Dispatch.Status.OK,
		"status=%d gaps=%s" % [status, str(dispatch.gaps.to_lines())]
	)
	var sink := world.ui() as SageScriptWorldStub.RecordingSink
	_check(
		"deselect_reaches_the_ui_sink_with_zero_values",
		sink.count_for("DESELECT") == 1 and sink.values_for("DESELECT") == [],
		str(sink.records)
	)
	_check(
		"the_served_deselect_records_no_gap",
		not dispatch.gaps.has("action", "DESELECT", GapLog.REASON_WORLD_REFUSED),
		str(dispatch.gaps.to_lines())
	)


func _test_deselect_without_a_sink_refuses() -> void:
	# A world with no UI adapter refuses, and that refusal is a structured
	# WORLD_REFUSED gap naming the action - never OK: a deselect that silently
	# did not happen is the failure the sink subsystem exists to make visible.
	var harness := _harness(SageScriptWorld.new())
	var dispatch: SageScriptDispatch = harness["dispatch"]

	var status := _act(harness, "DESELECT", [])
	_check(
		"a_sinkless_deselect_is_world_refused_not_ok",
		status == Dispatch.Status.WORLD_REFUSED,
		"status=%d" % status
	)
	_check(
		"the_deselect_refusal_names_the_missing_ui_sink",
		_gap_detail(dispatch, "action", "DESELECT", GapLog.REASON_WORLD_REFUSED)
			.contains("UI presentation sink"),
		str(dispatch.gaps.to_lines())
	)


# --- 2. IDLE_ALL_UNITS -------------------------------------------------------


func _test_idle_all_units_fans_out_over_all_players() -> void:
	# IDLE_ALL_UNITS() - "IDLE_ALL_UNITS|-|Scripting|SHARED|S"
	# (script_action_table.gd:265). Retail idles EVERY unit on the map, so the
	# handler must pass the all-players token, never a single seat (which
	# would silently shrink the order's scope), and 0 ticks - the facet
	# documents no indefinite sentinel, so 0 is a plain momentary idle (a
	# stop), which is exactly what the retail action means.
	var harness := _harness()
	var world: MiscWorld = harness["world"]
	var dispatch: SageScriptDispatch = harness["dispatch"]

	var status := _act(harness, "IDLE_ALL_UNITS", [])
	_check(
		"idle_all_units_dispatches_ok",
		status == Dispatch.Status.OK,
		"status=%d gaps=%s" % [status, str(dispatch.gaps.to_lines())]
	)
	_check(
		"the_idle_order_is_player_scoped_on_the_all_players_token_for_zero_ticks",
		world.calls == ["orders.idle_for_ticks|player|%s|0" % ALL_PLAYERS],
		str(world.calls)
	)
	_check(
		"the_served_idle_records_no_gap",
		not dispatch.gaps.has("action", "IDLE_ALL_UNITS", GapLog.REASON_WORLD_REFUSED),
		str(dispatch.gaps.to_lines())
	)


func _test_idle_all_units_world_refusal_is_not_ok() -> void:
	var harness := _harness(SageScriptWorld.new())
	var dispatch: SageScriptDispatch = harness["dispatch"]

	var status := _act(harness, "IDLE_ALL_UNITS", [])
	_check(
		"a_refused_idle_all_is_world_refused_not_ok",
		status == Dispatch.Status.WORLD_REFUSED,
		"status=%d" % status
	)
	_check(
		"the_idle_refusal_names_the_orders_facet_method",
		_gap_detail(dispatch, "action", "IDLE_ALL_UNITS", GapLog.REASON_WORLD_REFUSED)
			.contains("orders.idle_for_ticks"),
		str(dispatch.gaps.to_lines())
	)


# --- 2/3. PLAYER_FORCE_EMOTION -----------------------------------------------


func _test_force_emotion_serves_with_ticks() -> void:
	# PLAYER_FORCE_EMOTION(PLAYER, EMOTION, REAL)
	#   "PLAYER_FORCE_EMOTION|PLAYER,EMOTION,REAL|Player|SHARED|S"
	#   (script_action_table.gd:504).
	# EMOTION is an integer payload (6 = sourced "TERROR") and the REAL is a
	# duration in SECONDS that must reach the facet as TICKS: 2.5 s -> 25.
	# The EMOTION and duration are integer-and-real neighbours, so the real
	# argument carries an integer decoy - a wrong-field read shows up as
	# 424242 in the logged call.
	var harness := _harness()
	var world: MiscWorld = harness["world"]
	var dispatch: SageScriptDispatch = harness["dispatch"]

	var status := _act(harness, "PLAYER_FORCE_EMOTION", [
		_player_arg("Synthetic_Player_1"),
		_enum_arg(6),
		_real_arg(2.5),
	])
	_check(
		"the_forced_emotion_dispatches_ok",
		status == Dispatch.Status.OK,
		"status=%d gaps=%s" % [status, str(dispatch.gaps.to_lines())]
	)
	_check(
		"player_emotion_and_tick_duration_arrive_in_facet_order",
		world.calls == [
			"players.force_emotion|Synthetic_Player_1|6|%d" % TICKS_FOR_2_5_SECONDS
		],
		str(world.calls)
	)
	_check(
		"the_served_emotion_records_no_gap",
		not dispatch.gaps.has(
			"action", "PLAYER_FORCE_EMOTION", GapLog.REASON_WORLD_REFUSED
		),
		str(dispatch.gaps.to_lines())
	)


func _test_force_emotion_negative_duration_is_bad_arguments() -> void:
	var harness := _harness()
	var world: MiscWorld = harness["world"]

	_check(
		"a_negative_emotion_duration_is_bad_arguments",
		_act(harness, "PLAYER_FORCE_EMOTION", [
			_player_arg("Synthetic_Player_1"),
			_enum_arg(6),
			_real_arg(-1.0),
		]) == Dispatch.Status.BAD_ARGUMENTS
	)
	_check(
		"the_negative_emotion_duration_never_reached_the_world",
		world.calls.is_empty(),
		str(world.calls)
	)


func _test_force_emotion_world_refusal_is_not_ok() -> void:
	var harness := _harness(SageScriptWorld.new())
	var dispatch: SageScriptDispatch = harness["dispatch"]

	var status := _act(harness, "PLAYER_FORCE_EMOTION", [
		_player_arg("Synthetic_Player_1"),
		_enum_arg(6),
		_real_arg(2.5),
	])
	_check(
		"a_refused_emotion_is_world_refused_not_ok",
		status == Dispatch.Status.WORLD_REFUSED,
		"status=%d" % status
	)
	_check(
		"the_emotion_refusal_names_the_players_facet_method",
		_gap_detail(
			dispatch, "action", "PLAYER_FORCE_EMOTION", GapLog.REASON_WORLD_REFUSED
		).contains("players.force_emotion"),
		str(dispatch.gaps.to_lines())
	)


# --- 5. FIND_HOME_BASE_OF_PLAYER ---------------------------------------------


func _test_find_home_base_binds_the_answer() -> void:
	# FIND_HOME_BASE_OF_PLAYER(PLAYER, UNIT_REF, BOOLEAN)
	#   "FIND_HOME_BASE_OF_PLAYER|PLAYER,UNIT_REF,BOOLEAN|-|SHARED|S"
	#   (script_action_table.gd:229).
	# A RESULT BINDER, not an order: the handler queries players.home_base for
	# the PLAYER and binds the answered object name to the UNIT_REF through
	# units.set_reference (destination first - the WP16 shape). The fixture
	# player, reference and answer are three mutually distinguishable names,
	# so a rotated read produces a different logged string.
	var harness := _harness()
	var world: MiscWorld = harness["world"]
	var dispatch: SageScriptDispatch = harness["dispatch"]
	world.home_bases["Synthetic_Player_1"] = "Synthetic_Fortress"

	var status := _act(harness, "FIND_HOME_BASE_OF_PLAYER", [
		_player_arg("Synthetic_Player_1"),
		_name_arg("Synthetic_Base_Ref"),
		_boolean_arg(true),
	])
	_check(
		"the_home_base_bind_dispatches_ok",
		status == Dispatch.Status.OK,
		"status=%d gaps=%s" % [status, str(dispatch.gaps.to_lines())]
	)
	_check(
		"the_query_and_the_bind_arrive_in_facet_order",
		world.calls == [
			"players.home_base|Synthetic_Player_1",
			"units.set_reference|Synthetic_Base_Ref|Synthetic_Fortress",
		],
		str(world.calls)
	)
	_check(
		"the_reference_now_names_the_answered_home_base",
		world.references.get("Synthetic_Base_Ref", "") == "Synthetic_Fortress",
		str(world.references)
	)
	_check(
		"the_served_bind_records_no_gap",
		not dispatch.gaps.has(
			"action", "FIND_HOME_BASE_OF_PLAYER", GapLog.REASON_WORLD_REFUSED
		),
		str(dispatch.gaps.to_lines())
	)


func _test_find_home_base_query_refusal_binds_nothing() -> void:
	# No fixture home base was taught, so the query REFUSES - and a refused
	# query must bind NOTHING: writing a guessed (or empty) binding would let
	# every downstream reader resolve a base the world never answered.
	var harness := _harness()
	var world: MiscWorld = harness["world"]
	var dispatch: SageScriptDispatch = harness["dispatch"]
	world.expect_refusals()

	var status := _act(harness, "FIND_HOME_BASE_OF_PLAYER", [
		_player_arg("Synthetic_Player_9"),
		_name_arg("Synthetic_Base_Ref"),
		_boolean_arg(true),
	])
	_check(
		"an_unanswerable_home_base_query_is_world_refused_not_ok",
		status == Dispatch.Status.WORLD_REFUSED
		and dispatch.gaps.has(
			"action", "FIND_HOME_BASE_OF_PLAYER", GapLog.REASON_WORLD_REFUSED
		),
		"status=%d gaps=%s" % [status, str(dispatch.gaps.to_lines())]
	)
	_check(
		"the_query_refusal_names_the_players_facet_method",
		world.refused("players.home_base"),
		str(world.refusal_log)
	)
	_check(
		"a_refused_query_bound_nothing",
		world.references.is_empty(),
		str(world.references)
	)


func _test_find_home_base_bind_refusal_surfaces() -> void:
	# The query answers but the reference store refuses the write: the second
	# leg's refusal must surface exactly like the first - never swallowed into
	# an OK because "the query part worked".
	var harness := _harness()
	var world: MiscWorld = harness["world"]
	var dispatch: SageScriptDispatch = harness["dispatch"]
	world.home_bases["Synthetic_Player_1"] = "Synthetic_Fortress"
	world.refuse_reference_binds = true
	world.expect_refusals()

	var status := _act(harness, "FIND_HOME_BASE_OF_PLAYER", [
		_player_arg("Synthetic_Player_1"),
		_name_arg("Synthetic_Base_Ref"),
		_boolean_arg(true),
	])
	_check(
		"a_refused_bind_is_world_refused_not_ok",
		status == Dispatch.Status.WORLD_REFUSED
		and _gap_detail(
			dispatch, "action", "FIND_HOME_BASE_OF_PLAYER", GapLog.REASON_WORLD_REFUSED
		).contains("units.set_reference"),
		"status=%d gaps=%s" % [status, str(dispatch.gaps.to_lines())]
	)
	_check(
		"the_refused_bind_left_the_reference_store_empty",
		world.references.is_empty(),
		str(world.references)
	)


# --- 6. Arity ----------------------------------------------------------------


func _test_arity_is_enforced_per_action() -> void:
	# Arity is checked against the sourced signature before the handler runs,
	# so a positional read can never quietly slide onto a neighbouring
	# argument. One wrong-arity case per served action.
	var harness := _harness()
	var dispatch: SageScriptDispatch = harness["dispatch"]

	_check(
		"a_three_argument_modelcondition_set_is_rejected",
		_act(harness, "UNIT_SET_MODELCONDITION_FOR_DURATION", [
			_name_arg("Synthetic_Subject"),
			_name_arg("SYNTHETIC_MC_FLAILING"),
			_real_arg(6.0),
		]) == Dispatch.Status.BAD_ARGUMENTS
		and dispatch.gaps.has(
			"action", "UNIT_SET_MODELCONDITION_FOR_DURATION", GapLog.REASON_BAD_ARGUMENTS
		),
		str(dispatch.gaps.to_lines())
	)
	_check(
		"a_two_argument_relation_write_is_rejected",
		_act(harness, "PLAYER_RELATES_PLAYER", [
			_player_arg("Synthetic_Player_1"),
			_player_arg("Synthetic_Player_2"),
		]) == Dispatch.Status.BAD_ARGUMENTS
		and dispatch.gaps.has(
			"action", "PLAYER_RELATES_PLAYER", GapLog.REASON_BAD_ARGUMENTS
		),
		str(dispatch.gaps.to_lines())
	)
	_check(
		"a_one_argument_deselect_is_rejected",
		_act(harness, "DESELECT", [_name_arg("Synthetic_Subject")])
			== Dispatch.Status.BAD_ARGUMENTS
		and dispatch.gaps.has("action", "DESELECT", GapLog.REASON_BAD_ARGUMENTS),
		str(dispatch.gaps.to_lines())
	)
	_check(
		"a_one_argument_idle_all_is_rejected",
		_act(harness, "IDLE_ALL_UNITS", [_name_arg("Synthetic_Subject")])
			== Dispatch.Status.BAD_ARGUMENTS
		and dispatch.gaps.has("action", "IDLE_ALL_UNITS", GapLog.REASON_BAD_ARGUMENTS),
		str(dispatch.gaps.to_lines())
	)
	_check(
		"a_two_argument_emotion_is_rejected",
		_act(harness, "PLAYER_FORCE_EMOTION", [
			_player_arg("Synthetic_Player_1"),
			_enum_arg(6),
		]) == Dispatch.Status.BAD_ARGUMENTS
		and dispatch.gaps.has(
			"action", "PLAYER_FORCE_EMOTION", GapLog.REASON_BAD_ARGUMENTS
		),
		str(dispatch.gaps.to_lines())
	)
	_check(
		"a_two_argument_home_base_bind_is_rejected",
		_act(harness, "FIND_HOME_BASE_OF_PLAYER", [
			_player_arg("Synthetic_Player_1"),
			_name_arg("Synthetic_Base_Ref"),
		]) == Dispatch.Status.BAD_ARGUMENTS
		and dispatch.gaps.has(
			"action", "FIND_HOME_BASE_OF_PLAYER", GapLog.REASON_BAD_ARGUMENTS
		),
		str(dispatch.gaps.to_lines())
	)


# --- Stub world ---------------------------------------------------------------
#
# The shared SageScriptWorldStub teaches no Units or Orders facet and none of
# the Players commands this package needs, so the slice is taught HERE rather
# than in the shared stub (an existing file the concurrent packages also
# depend on). Every command is recorded as a flat string in world-signature
# order so a test asserts on the exact arguments that reached the world; every
# read answers from a fixture or REFUSES, so a test that forgot to set one
# gets a loud refusal rather than a plausible answer. The UI sink is the
# shared stub's RecordingSink, inherited.


class MiscWorld:
	extends SageScriptWorldStub

	var calls: Array[String] = []

	## player -> home base object name. Only fixture players resolve.
	var home_bases: Dictionary = {}

	## reference name -> bound object name (the world-side reference store).
	var references: Dictionary = {}

	## When true, units.set_reference refuses - the bind-leg refusal fixture.
	var refuse_reference_binds := false

	func _make_units() -> SageScriptWorld.Units:
		return MiscStubUnits.new()

	func _make_orders() -> SageScriptWorld.Orders:
		return MiscStubOrders.new()

	func _make_players() -> SageScriptWorld.Players:
		return MiscStubPlayers.new()


class MiscStubUnits:
	extends SageScriptWorld.Units

	func set_model_condition(
		object_name: String, condition: String, enabled: bool, duration_ticks: int
	) -> bool:
		(world as MiscWorld).calls.append(
			"units.set_model_condition|%s|%s|%s|%d"
			% [object_name, condition, str(enabled), duration_ticks]
		)
		return true

	func set_reference(reference: String, object_name: String) -> bool:
		var stub := world as MiscWorld
		if stub.refuse_reference_binds:
			return _refuse_command(
				"units.set_reference", "fixture refuses reference binds"
			)
		stub.calls.append("units.set_reference|%s|%s" % [reference, object_name])
		stub.references[reference] = object_name
		return true


class MiscStubOrders:
	extends SageScriptWorld.Orders

	func idle_for_ticks(scope: int, name: String, ticks: int) -> bool:
		(world as MiscWorld).calls.append(
			"orders.idle_for_ticks|%s|%s|%d"
			% [SageScriptWorld.scope_label(scope), name, ticks]
		)
		return true


class MiscStubPlayers:
	extends SageScriptWorldStub.StubPlayers

	func set_relation_to(player: String, other: String, relation: int) -> bool:
		(world as MiscWorld).calls.append(
			"players.set_relation_to|%s|%s|%d" % [player, other, relation]
		)
		return true

	func force_emotion(player: String, emotion: int, duration_ticks: int) -> bool:
		(world as MiscWorld).calls.append(
			"players.force_emotion|%s|%d|%d" % [player, emotion, duration_ticks]
		)
		return true

	func home_base(player: String) -> SageWorldQuery:
		var stub := world as MiscWorld
		stub.calls.append("players.home_base|%s" % player)
		if not stub.home_bases.has(player):
			return _refuse_query(
				"players.home_base", "no fixture home base for '%s'" % player
			)
		return SageWorldQuery.hit(String(stub.home_bases[player]))


# --- Reporting ----------------------------------------------------------------


func _check(name: String, condition: bool, detail: String = "") -> void:
	_watchdog.note(name)
	if condition:
		passed += 1
		print("HANDLERS_WP23 PASS %s" % name)
	else:
		failed += 1
		printerr(
			"HANDLERS_WP23 FAIL %s%s" % [name, " (%s)" % detail if detail != "" else ""]
		)
