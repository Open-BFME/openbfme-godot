extends SceneTree

## Proof runner for WP15-teams, AI-critical subset (wp15_ai_teams.gd).
##
## Covers, in order:
##   1. registration    - all 24 AI-used members accounted for, 18 served and 6
##                        gap-registered, with no overlap and no silent absence
##   2. positional args - the two signatures that put the DESTINATION first and
##                        the one that puts two TEAMs side by side
##   3. payload fields  - AI_MOOD is read from `integer`, not `text`, and an
##                        out-of-table value reaches the world unmodified
##   4. flag folding    - the world collapses pairs of SAGE actions behind a
##                        boolean; each member must pass the right one
##   5. Scope pattern   - TEAM_HUNT reaches the single orders.hunt() with
##                        Scope.TEAM rather than a team-specific method
##   6. repeat counts   - 0 and 1 are exact identities and are served; a finite
##                        repeat refuses instead of looping forever
##   7. conditions      - true, false, and the third answer: a read the world
##                        cannot answer NEVER comes back as a plain false
##   8. gap register    - the 6 blocked members report BLOCKED, name the missing
##                        world signature, and are not counted as coverage
##
## Every fixture value is SYNTHETIC. The call SHAPES are modelled on the retail
## AI libraries (a team name with a slash, a behaviour-script name, an AI_MOOD
## of -3) because those shapes are what the handlers must survive, but no retail
## payload text is reproduced here.
##
## Invocation:
##   Godot_v4.7-stable_win64_console.exe --headless --path game \
##     -s res://tests/handlers_wp15_ai_teams_runner.gd

const Registry := preload("res://src/script/handlers/_registry.gd")
const Dispatch := preload("res://src/script/script_dispatch.gd")
const CoreHandlers := preload("res://src/script/script_handlers_core.gd")
const Env := preload("res://src/script/script_env.gd")
const GapLog := preload("res://src/script/script_gaps.gd")
const ParamTypes := preload("res://src/script/script_param_types.gd")
const Wp15 := preload("res://src/script/handlers/wp15_ai_teams.gd")

## The 18 members this package serves and the 6 it gap-registers. Written out
## rather than derived from the module, so that a member quietly disappearing
## from register() fails here instead of agreeing with itself.
const SERVED_ACTIONS := [
	"SET_TEAM_REFERENCE",
	"TEAM_DELETE",
	"TEAM_EXECUTE_SEQUENTIAL_SCRIPT",
	"TEAM_EXECUTE_SEQUENTIAL_SCRIPT_LOOPING",
	"TEAM_EXIT_ALL_BUILDINGS",
	"TEAM_HUNT",
	"TEAM_MERGE_INTO_TEAM",
	"TEAM_SET_ATTITUDE",
	"TEAM_SET_STATE",
	"TEAM_STOP",
	"TEAM_STOP_SEQUENTIAL_SCRIPT",
	"TEAM_TRANSFER_TO_PLAYER",
	"UNIT_SET_TEAM",
]

const SERVED_CONDITIONS := [
	"TEAM_CREATED",
	"TEAM_HAS_CUSTOM_STATE",
	"TEAM_HAS_UNITS",
	"TEAM_STATE_IS",
	"TEAM_STATE_IS_NOT",
]

const GAP_REGISTERED := [
	"SET_COUNTER_TO_TEAM_THREAT",
	"SET_REF_TO_NEREST_TEAM_OF_TYPE_OWNED_BY_PLAYER",
	"TEAM_RECRUIT_UNITS",
	"TEAM_RECRUIT_UNITS_FROM_TEAM",
	"TEAM_SET_CUSTOM_STATE",
	"TEAM_STAND_GROUND",
]

## LIVENESS GUARD. A GDScript RUNTIME error aborts the enclosing function
## on the spot without propagating, so every later _check() in that function
## silently never runs and `failed` never moves - the runner then prints a
## zero-failure result and exits 0 with SCRIPT ERROR lines above it. This is
## the exact count a HEALTHY run makes; if the run makes any other number,
## something aborted (or an assertion was added without updating this) and
## the result is not to be trusted.
const EXPECTED_CHECKS := 61

var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	_test_registration_covers_the_ai_subset()
	_test_positional_argument_order()
	_test_ai_mood_is_read_as_an_integer()
	_test_folded_flags()
	_test_hunt_uses_the_scope_pattern()
	_test_sequential_repeat_counts()
	_test_conditions_answer_truthfully()
	_test_an_unanswerable_condition_is_never_a_plain_false()
	_test_custom_state_read_accepts_both_shapes()
	_test_conditions_have_no_side_effects()
	_test_command_refusal_is_reported()
	_test_arity_is_enforced()
	_test_gap_registered_members()
	var ran := passed + failed
	if ran != EXPECTED_CHECKS:
		failed += 1
		printerr("HANDLERS_WP15 FAIL liveness: ran %d checks, expected %d - a function aborted before its assertions" % [ran, EXPECTED_CHECKS])
	print("HANDLERS_WP15_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


# --- Fixture helpers ------------------------------------------------------


func _argument(
	argument_type: int, integer: int = 0, real: float = 0.0, text: String = ""
) -> Dictionary:
	return {"argumentType": argument_type, "integer": integer, "real": real, "text": text}


func _name_arg(value: String) -> Dictionary:
	# TEAM, TEAM_STATE, SCRIPT, TEAM_REF, UNIT and OBJECT_TYPE_LIST have no
	# observed wire code in this repo, so SageScriptArgs does not assert one.
	# The integer field carries a DECOY: a handler that read the wrong field
	# would get 424242 instead of the name, loudly rather than plausibly.
	return _argument(99, 424242, 0.0, value)


func _player_arg(value: String) -> Dictionary:
	return _argument(ParamTypes.ARGUMENT_PLAYER, 0, 0.0, value)


func _counter_arg(value: String) -> Dictionary:
	# COUNTER is one of the few parameter types this repo HAS observed a wire
	# code for, so SageScriptArgs asserts it: passing the generic name argument
	# here is rejected as BAD_ARGUMENTS before the handler ever runs.
	return _argument(ParamTypes.ARGUMENT_COUNTER_NAME, 0, 0.0, value)


func _int_arg(value: int, decoy_text: String = "DECOY_TEXT") -> Dictionary:
	return _argument(ParamTypes.ARGUMENT_INTEGER, value, 0.0, decoy_text)


func _mood_arg(value: int, decoy_text: String) -> Dictionary:
	# AI_MOOD is integer-valued in PAYLOAD_FIELD_FOR_PARAM but has no wire code,
	# so nothing validates it: the decoy text is the only thing standing between
	# a wrong-field read and a silent "".
	return _argument(ParamTypes.ARGUMENT_INTEGER, value, 0.0, decoy_text)


func _bool_arg(value: bool) -> Dictionary:
	return _argument(ParamTypes.ARGUMENT_BOOLEAN, 1 if value else 0)


func _real_arg(value: float) -> Dictionary:
	return _argument(ParamTypes.ARGUMENT_REAL, 0, value)


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
		"world": TeamWorld.new(20260726),
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
		"the_wp15_package_is_present",
		(outcome["packages"] as Array).has("WP15-teams"),
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

	var unblocked: Array[String] = []
	for name: String in GAP_REGISTERED:
		if not dispatch.blocked_names().has(name):
			unblocked.append(name)
	_check("every_gap_registered_member_is_blocked", unblocked.is_empty(), str(unblocked))

	# 18 + 6 = 24, the exact AI-used membership the decoder measured. If a later
	# edit serves a gap-registered member without removing it from the gap list,
	# or vice versa, these two disagree.
	_check(
		"the_subset_is_twenty_four_members",
		SERVED_ACTIONS.size() + SERVED_CONDITIONS.size() + GAP_REGISTERED.size() == 24,
		"served=%d conditions=%d gapped=%d" % [
			SERVED_ACTIONS.size(), SERVED_CONDITIONS.size(), GAP_REGISTERED.size()
		]
	)
	var overlap: Array[String] = []
	for name: String in GAP_REGISTERED:
		if dispatch.implemented_actions().has(name):
			overlap.append(name)
	_check(
		"a_gap_registered_member_is_not_counted_as_coverage",
		overlap.is_empty(),
		str(overlap)
	)

	# The gap list this file asserts on must be the one the module actually
	# declares, or the two could drift apart while both look right.
	var declared: Array = (
		Wp15.GAP_CUSTOM_STATE_ACTIONS + Wp15.GAP_TEAM_THREAT_ACTIONS
		+ Wp15.GAP_RECRUIT_ACTIONS + Wp15.GAP_STAND_GROUND_ACTIONS
		+ Wp15.GAP_REF_TO_NEAREST_ACTIONS
	)
	declared.sort()
	_check(
		"the_modules_gap_list_matches_this_runners",
		declared == GAP_REGISTERED,
		"module=%s runner=%s" % [str(declared), str(GAP_REGISTERED)]
	)


# --- 2. Positional argument order -----------------------------------------


func _test_positional_argument_order() -> void:
	var harness := _harness()
	var world: TeamWorld = harness["world"]

	# TEAM_MERGE_INTO_TEAM(TEAM, TEAM). Two arguments of the SAME type, so no
	# type-search could ever recover the order: argument 0 is absorbed into
	# argument 1. Swapped, this would dissolve the wrong team.
	_act(harness, "TEAM_MERGE_INTO_TEAM", [
		_name_arg("Scout Screen"), _name_arg("Main Assault")
	])
	_check(
		"merge_absorbs_the_first_team_into_the_second",
		world.calls.has("teams.merge_into|Scout Screen|Main Assault"),
		str(world.calls)
	)

	# SET_TEAM_REFERENCE(TEAM_REF, TEAM) - destination FIRST, which is the
	# reverse of how the English description reads.
	_act(harness, "SET_TEAM_REFERENCE", [
		_name_arg("REF_PATROL_GROUP"), _name_arg("Patrol Group")
	])
	_check(
		"set_team_reference_binds_argument_zero_to_argument_one",
		world.calls.has("teams.set_reference|REF_PATROL_GROUP|Patrol Group"),
		str(world.calls)
	)

	# TEAM_TRANSFER_TO_PLAYER(TEAM, PLAYER) - a team name containing a slash,
	# the shape the multiplayer inheritance libraries use, must pass through
	# unnormalised.
	_act(harness, "TEAM_TRANSFER_TO_PLAYER", [
		_name_arg("PlyrNeutral/Slot_4_Inherit"), _player_arg("<This Player>")
	])
	_check(
		"transfer_passes_the_team_name_through_unnormalised",
		world.calls.has("teams.transfer_to_player|PlyrNeutral/Slot_4_Inherit|<This Player>"),
		str(world.calls)
	)

	# UNIT_SET_TEAM(UNIT, TEAM) goes to the UNITS facet even though the catalog
	# files it under WP15.
	_act(harness, "UNIT_SET_TEAM", [_name_arg("SENTRY_POST"), _name_arg("Garrison Team")])
	_check(
		"unit_set_team_reaches_the_units_facet",
		world.calls.has("units.set_team|SENTRY_POST|Garrison Team"),
		str(world.calls)
	)


# --- 3. Payload field -----------------------------------------------------


func _test_ai_mood_is_read_as_an_integer() -> void:
	var harness := _harness()
	var world: TeamWorld = harness["world"]

	# AI_MOOD is an enum, so it lives in the `integer` field. The decoy text in
	# the same slot is what a wrong-field read would return.
	_act(harness, "TEAM_SET_ATTITUDE", [_name_arg("Raiders"), _mood_arg(2, "Passive")])
	_check(
		"attitude_reads_the_integer_field_not_the_text_field",
		world.calls.has("teams.set_attitude|Raiders|2"),
		str(world.calls)
	)

	# THE CEILING RULE. The reference documents AI_MOOD as 0..5, but retail AI
	# authors values outside it. A negative mood must reach the world EXACTLY as
	# authored - not clamped into the documented range, not rejected as bad
	# arguments. This engine implements the content, not the table.
	var status := _act(harness, "TEAM_SET_ATTITUDE", [_name_arg("Raiders"), _mood_arg(-3, "?")])
	_check(
		"an_out_of_table_mood_is_passed_through_unmodified",
		status == Dispatch.Status.OK and world.calls.has("teams.set_attitude|Raiders|-3"),
		"status=%d calls=%s" % [status, str(world.calls)]
	)


# --- 4. Folded flags ------------------------------------------------------


func _test_folded_flags() -> void:
	# The world collapses pairs of SAGE actions behind a boolean. Each member
	# below must pass the flag its own name implies; getting one backwards would
	# disband a team that should have survived, or delete corpses only.
	var harness := _harness()
	var world: TeamWorld = harness["world"]

	_act(harness, "TEAM_STOP", [_name_arg("Escort")])
	_check(
		"team_stop_does_not_disband",
		world.calls.has("teams.stop|Escort|false"),
		str(world.calls)
	)

	_act(harness, "TEAM_DELETE", [_name_arg("Escort")])
	_check(
		"team_delete_is_not_the_living_only_variant",
		world.calls.has("teams.delete|Escort|false"),
		str(world.calls)
	)

	_act(harness, "TEAM_EXIT_ALL_BUILDINGS", [_name_arg("Escort")])
	_check(
		"exit_all_buildings_sets_the_buildings_only_flag",
		world.calls.has("teams.exit_all|Escort|true"),
		str(world.calls)
	)

	_act(harness, "TEAM_EXECUTE_SEQUENTIAL_SCRIPT", [
		_name_arg("Escort"), _name_arg("be_Move Up")
	])
	_check(
		"the_plain_sequential_script_does_not_loop",
		world.calls.has("teams.execute_sequential_script|Escort|be_Move Up|false"),
		str(world.calls)
	)

	_act(harness, "TEAM_SET_STATE", [_name_arg("Escort"), _name_arg("AI_SYNTHETIC_HOLD")])
	_check(
		"team_state_is_carried_as_text",
		world.calls.has("teams.set_state|Escort|AI_SYNTHETIC_HOLD"),
		str(world.calls)
	)

	_act(harness, "TEAM_STOP_SEQUENTIAL_SCRIPT", [_name_arg("Escort")])
	_check(
		"stop_sequential_script_is_served",
		world.calls.has("teams.stop_sequential_script|Escort"),
		str(world.calls)
	)


# --- 5. The Scope pattern -------------------------------------------------


func _test_hunt_uses_the_scope_pattern() -> void:
	var harness := _harness()
	var world: TeamWorld = harness["world"]

	# One world method serves TEAM_HUNT, NAMED_HUNT and PLAYER_HUNT. This package
	# owns the TEAM spelling only, and the empty command button is the documented
	# "hunt with the default weapon" form.
	_act(harness, "TEAM_HUNT", [_name_arg("Hunters")])
	_check(
		"team_hunt_reaches_orders_hunt_with_team_scope_and_no_command_button",
		world.calls.has("orders.hunt|%d|Hunters|" % SageScriptWorld.Scope.TEAM),
		str(world.calls)
	)
	_check(
		"team_scope_is_distinct_from_unit_and_player_scope",
		SageScriptWorld.Scope.TEAM != SageScriptWorld.Scope.UNIT
		and SageScriptWorld.Scope.TEAM != SageScriptWorld.Scope.PLAYER
	)


# --- 6. Repeat counts -----------------------------------------------------


func _test_sequential_repeat_counts() -> void:
	var harness := _harness()
	var world: TeamWorld = harness["world"]

	# 0 means forever - the reference's own words - which is exactly the world's
	# looping=true.
	var forever := _act(harness, "TEAM_EXECUTE_SEQUENTIAL_SCRIPT_LOOPING", [
		_name_arg("Patrol"), _name_arg("be_Circuit"), _int_arg(0)
	])
	_check(
		"repeat_count_zero_loops_forever",
		forever == Dispatch.Status.OK
		and world.calls.has("teams.execute_sequential_script|Patrol|be_Circuit|true"),
		"status=%d calls=%s" % [forever, str(world.calls)]
	)

	# 1 means one pass, which is indistinguishable from the non-looping action.
	var once := _act(harness, "TEAM_EXECUTE_SEQUENTIAL_SCRIPT_LOOPING", [
		_name_arg("Patrol"), _name_arg("be_Once"), _int_arg(1)
	])
	_check(
		"repeat_count_one_runs_through_once",
		once == Dispatch.Status.OK
		and world.calls.has("teams.execute_sequential_script|Patrol|be_Once|false"),
		"status=%d calls=%s" % [once, str(world.calls)]
	)

	# A FINITE repeat cannot be expressed by a boolean. Refusing is the only
	# honest answer: looping forever and running once are both wrong, and both
	# would be invisible.
	world.calls.clear()
	var finite := _act(harness, "TEAM_EXECUTE_SEQUENTIAL_SCRIPT_LOOPING", [
		_name_arg("Patrol"), _name_arg("be_Thrice"), _int_arg(3)
	])
	_check(
		"a_finite_repeat_count_refuses_rather_than_looping_forever",
		finite == Dispatch.Status.WORLD_REFUSED,
		"status=%d" % finite
	)
	_check(
		"a_refused_repeat_count_does_not_reach_the_world_at_all",
		world.calls.is_empty(),
		str(world.calls)
	)
	_check(
		"the_refused_repeat_count_is_recorded_as_a_gap",
		(harness["dispatch"] as SageScriptDispatch).gaps.has(
			"action", "TEAM_EXECUTE_SEQUENTIAL_SCRIPT_LOOPING", GapLog.REASON_WORLD_REFUSED
		),
		str((harness["dispatch"] as SageScriptDispatch).gaps.to_lines())
	)

	# A negative count has no defined meaning at all.
	_check(
		"a_negative_repeat_count_is_bad_arguments",
		_act(harness, "TEAM_EXECUTE_SEQUENTIAL_SCRIPT_LOOPING", [
			_name_arg("Patrol"), _name_arg("be_Nonsense"), _int_arg(-1)
		]) == Dispatch.Status.BAD_ARGUMENTS
	)


# --- 7. Conditions --------------------------------------------------------


func _test_conditions_answer_truthfully() -> void:
	var harness := _harness()
	var world: TeamWorld = harness["world"]

	world.team_state["Vanguard"] = "AI_SYNTHETIC_RETREAT"
	world.team_units["Vanguard"] = 4
	world.team_units["Ghost Company"] = 0
	world.team_created["Vanguard"] = true
	world.team_created["Ghost Company"] = false

	_check(
		"state_is_matches_the_current_state",
		_cond(harness, "TEAM_STATE_IS", [
			_name_arg("Vanguard"), _name_arg("AI_SYNTHETIC_RETREAT")
		])
	)
	_check(
		"state_is_rejects_a_different_state",
		not _cond(harness, "TEAM_STATE_IS", [
			_name_arg("Vanguard"), _name_arg("AI_SYNTHETIC_ADVANCE")
		])
	)
	# Exact comparison: no case folding, or a mod's two distinct states would
	# collide in this engine but not in the retail one.
	_check(
		"state_comparison_is_case_sensitive",
		not _cond(harness, "TEAM_STATE_IS", [
			_name_arg("Vanguard"), _name_arg("ai_synthetic_retreat")
		])
	)
	_check(
		"state_is_not_is_the_complement_when_the_world_can_answer",
		_cond(harness, "TEAM_STATE_IS_NOT", [
			_name_arg("Vanguard"), _name_arg("AI_SYNTHETIC_ADVANCE")
		])
		and not _cond(harness, "TEAM_STATE_IS_NOT", [
			_name_arg("Vanguard"), _name_arg("AI_SYNTHETIC_RETREAT")
		])
	)

	_check(
		"has_units_is_true_for_a_populated_team",
		_cond(harness, "TEAM_HAS_UNITS", [_name_arg("Vanguard")])
	)
	_check(
		"has_units_is_false_for_an_empty_team",
		not _cond(harness, "TEAM_HAS_UNITS", [_name_arg("Ghost Company")])
	)

	_check(
		"team_created_reports_the_edge",
		_cond(harness, "TEAM_CREATED", [_name_arg("Vanguard")])
		and not _cond(harness, "TEAM_CREATED", [_name_arg("Ghost Company")])
	)


func _test_an_unanswerable_condition_is_never_a_plain_false() -> void:
	# The single most important test in this file. A world that cannot answer
	# must produce a REFUSAL - false at the call site, but with a gap on the
	# record - and TEAM_STATE_IS_NOT must not turn that refusal into `true`.
	var harness := _harness()
	var world: TeamWorld = harness["world"]
	var dispatch: SageScriptDispatch = harness["dispatch"]
	world.expect_refusals()
	# "Nowhere Team" has no fixture, so every read about it refuses.

	_check(
		"an_unanswerable_state_is_reads_false",
		not _cond(harness, "TEAM_STATE_IS", [
			_name_arg("Nowhere Team"), _name_arg("AI_SYNTHETIC_RETREAT")
		])
	)
	_check(
		"an_unanswerable_state_is_records_a_world_refused_gap",
		dispatch.gaps.has("condition", "TEAM_STATE_IS", GapLog.REASON_WORLD_REFUSED),
		str(dispatch.gaps.to_lines())
	)

	# The inversion trap. `not (unanswered)` would be TRUE here, which would tell
	# an AI attack script that a team it knows nothing about has finished
	# retreating.
	_check(
		"an_unanswerable_state_is_not_reads_false_rather_than_true",
		not _cond(harness, "TEAM_STATE_IS_NOT", [
			_name_arg("Nowhere Team"), _name_arg("AI_SYNTHETIC_RETREAT")
		])
	)
	_check(
		"an_unanswerable_state_is_not_records_a_world_refused_gap",
		dispatch.gaps.has("condition", "TEAM_STATE_IS_NOT", GapLog.REASON_WORLD_REFUSED),
		str(dispatch.gaps.to_lines())
	)

	# An unanswerable count is not zero, and an unanswerable edge is not "no".
	_check(
		"an_unanswerable_unit_count_refuses_instead_of_reading_as_empty",
		not _cond(harness, "TEAM_HAS_UNITS", [_name_arg("Nowhere Team")])
		and dispatch.gaps.has("condition", "TEAM_HAS_UNITS", GapLog.REASON_WORLD_REFUSED),
		str(dispatch.gaps.to_lines())
	)
	_check(
		"an_unanswerable_created_edge_refuses",
		not _cond(harness, "TEAM_CREATED", [_name_arg("Nowhere Team")])
		and dispatch.gaps.has("condition", "TEAM_CREATED", GapLog.REASON_WORLD_REFUSED),
		str(dispatch.gaps.to_lines())
	)
	_check(
		"the_refusals_name_the_missing_world_methods",
		world.refused("teams.state")
		and world.refused("teams.unit_count")
		and world.refused("teams.was_created"),
		str(world.refusal_log)
	)


func _test_custom_state_read_accepts_both_shapes() -> void:
	# teams.custom_state does not fix its value type. Both defensible shapes must
	# be read correctly, because getting it wrong yields a FALSE for a state that
	# is actually set - a wrong answer, not a refusal.
	var set_shaped := _harness()
	var set_world: TeamWorld = set_shaped["world"]
	set_world.team_custom_state["Assault"] = ["AI_SYNTHETIC_A", "AI_SYNTHETIC_B"]
	_check(
		"an_array_of_tokens_is_a_membership_test",
		_cond(set_shaped, "TEAM_HAS_CUSTOM_STATE", [
			_name_arg("Assault"), _name_arg("AI_SYNTHETIC_B")
		])
		and not _cond(set_shaped, "TEAM_HAS_CUSTOM_STATE", [
			_name_arg("Assault"), _name_arg("AI_SYNTHETIC_C")
		])
	)

	var single := _harness()
	var single_world: TeamWorld = single["world"]
	single_world.team_custom_state["Assault"] = "AI_SYNTHETIC_A"
	_check(
		"a_single_token_is_an_equality_test",
		_cond(single, "TEAM_HAS_CUSTOM_STATE", [
			_name_arg("Assault"), _name_arg("AI_SYNTHETIC_A")
		])
		and not _cond(single, "TEAM_HAS_CUSTOM_STATE", [
			_name_arg("Assault"), _name_arg("AI_SYNTHETIC_B")
		])
	)

	# Anything else refuses rather than defaulting to false.
	var odd := _harness()
	var odd_world: TeamWorld = odd["world"]
	odd_world.team_custom_state["Assault"] = 7
	_check(
		"an_unreadable_custom_state_shape_refuses_rather_than_guessing",
		not _cond(odd, "TEAM_HAS_CUSTOM_STATE", [
			_name_arg("Assault"), _name_arg("AI_SYNTHETIC_A")
		])
		and (odd["dispatch"] as SageScriptDispatch).gaps.has(
			"condition", "TEAM_HAS_CUSTOM_STATE", GapLog.REASON_WORLD_REFUSED
		),
		str((odd["dispatch"] as SageScriptDispatch).gaps.to_lines())
	)


func _test_conditions_have_no_side_effects() -> void:
	# Conditions are evaluated an unpredictable number of times, so any mutation
	# is a determinism bug. None of the five may touch the env or command the
	# world.
	var harness := _harness()
	var world: TeamWorld = harness["world"]
	var env: SageScriptEnv = harness["env"]
	world.team_state["Vanguard"] = "AI_SYNTHETIC_RETREAT"
	world.team_units["Vanguard"] = 2
	world.team_created["Vanguard"] = true
	world.team_custom_state["Vanguard"] = ["AI_SYNTHETIC_A"]

	for _repeat in range(3):
		_cond(harness, "TEAM_STATE_IS", [_name_arg("Vanguard"), _name_arg("AI_SYNTHETIC_RETREAT")])
		_cond(harness, "TEAM_STATE_IS_NOT", [_name_arg("Vanguard"), _name_arg("AI_SYNTHETIC_X")])
		_cond(harness, "TEAM_HAS_UNITS", [_name_arg("Vanguard")])
		_cond(harness, "TEAM_CREATED", [_name_arg("Vanguard")])
		_cond(harness, "TEAM_HAS_CUSTOM_STATE", [
			_name_arg("Vanguard"), _name_arg("AI_SYNTHETIC_A")
		])

	_check(
		"conditions_issued_no_world_commands",
		world.calls.is_empty(),
		str(world.calls)
	)
	_check(
		"conditions_left_the_script_environment_alone",
		env.snapshot()["counters"] == {} and env.snapshot()["flags"] == {}
	)


# --- 8. Command refusal and arity -----------------------------------------


func _test_command_refusal_is_reported() -> void:
	# A world that does not model teams at all must produce a structured gap per
	# action, never a quiet success. The BASE SageScriptWorld is exactly that
	# world: every Teams method inherits the refusing base.
	var dispatch := Dispatch.new()
	CoreHandlers.register_all(dispatch)
	Registry.register_all(dispatch)
	var status := dispatch.execute_action(
		_record("TEAM_STOP", [_name_arg("Escort")]), Env.new(), SageScriptWorld.new(), "fixture"
	)
	_check(
		"a_world_without_a_teams_facet_refuses_rather_than_pretending",
		status == Dispatch.Status.WORLD_REFUSED,
		"status=%d" % status
	)
	_check(
		"the_command_refusal_names_the_missing_facet_method",
		dispatch.gaps.has("action", "TEAM_STOP", GapLog.REASON_WORLD_REFUSED)
		and String(
			dispatch.gaps.entries["action|TEAM_STOP|%s" % GapLog.REASON_WORLD_REFUSED]["detail"]
		).contains("teams.stop"),
		str(dispatch.gaps.to_lines())
	)


func _test_arity_is_enforced() -> void:
	# Arity is checked against the sourced signature before the handler runs, so
	# a positional read can never quietly slide onto a neighbouring argument.
	var harness := _harness()
	_check(
		"a_short_argument_list_is_rejected",
		_act(harness, "TEAM_MERGE_INTO_TEAM", [_name_arg("Only One")])
		== Dispatch.Status.BAD_ARGUMENTS
	)
	_check(
		"a_long_argument_list_is_rejected",
		_act(harness, "TEAM_STOP", [_name_arg("Escort"), _name_arg("Surplus")])
		== Dispatch.Status.BAD_ARGUMENTS
	)
	_check(
		"a_short_condition_argument_list_is_rejected_and_the_gate_stays_shut",
		not _cond(harness, "TEAM_STATE_IS", [_name_arg("Vanguard")])
	)


# --- 9. Gap-registered members --------------------------------------------


func _test_gap_registered_members() -> void:
	var harness := _harness()
	var dispatch: SageScriptDispatch = harness["dispatch"]
	var world: TeamWorld = harness["world"]

	# TEAM_SET_CUSTOM_STATE, the most-called member of the package, carries a
	# BOOLEAN the world cannot accept. It must report BLOCKED and must not reach
	# the world at all - serving it as a plain "set" would invert every call site
	# that authored 0.
	var status := _act(harness, "TEAM_SET_CUSTOM_STATE", [
		_name_arg("Assault"), _name_arg("AI_SYNTHETIC_A"), _bool_arg(false)
	])
	_check("a_gap_registered_action_reports_blocked", status == Dispatch.Status.BLOCKED,
		"status=%d" % status)
	_check(
		"a_gap_registered_action_never_reaches_the_world",
		world.calls.is_empty(),
		str(world.calls)
	)
	_check(
		"the_gap_names_the_missing_world_signature",
		_gap_detail(dispatch, "TEAM_SET_CUSTOM_STATE").contains(
			"teams.set_custom_state(team: String, state: String, enabled: bool)"
		),
		str(dispatch.gaps.to_lines())
	)

	# The radius, the type list and the anchor team are each named in their own
	# gap, so a reader of the gap log learns which argument is missing.
	var threat_status := _act(harness, "SET_COUNTER_TO_TEAM_THREAT", [
		_counter_arg("g_Synthetic_Threat"), _name_arg("Assault"), _real_arg(500.0)
	])
	_check(
		"the_threat_action_reports_blocked_not_bad_arguments",
		threat_status == Dispatch.Status.BLOCKED,
		"status=%d" % threat_status
	)
	_check(
		"the_threat_gap_names_the_missing_radius",
		_gap_detail(dispatch, "SET_COUNTER_TO_TEAM_THREAT").contains(
			"teams.threat(team: String, radius: float)"
		),
		str(dispatch.gaps.to_lines())
	)

	_act(harness, "TEAM_STAND_GROUND", [_name_arg("Assault"), _bool_arg(false)])
	_check(
		"the_stand_ground_gap_explains_that_serving_it_would_invert_the_action",
		_gap_detail(dispatch, "TEAM_STAND_GROUND").contains(
			"orders.stand_ground(scope: int, name: String, enabled: bool)"
		),
		str(dispatch.gaps.to_lines())
	)

	# The two recruitment actions share one gap, which must name the dropped
	# type filter AND the unresolved INT rather than only one of them.
	_act(harness, "TEAM_RECRUIT_UNITS", [
		_name_arg("Assault"), _int_arg(3), _name_arg("Synthetic_Type_List")
	])
	_act(harness, "TEAM_RECRUIT_UNITS_FROM_TEAM", [
		_name_arg("Assault"), _int_arg(1), _name_arg("Synthetic_Type_List"), _name_arg("Reserve")
	])
	_check(
		"the_recruit_gap_names_both_the_type_list_and_the_unresolved_int",
		_gap_detail(dispatch, "TEAM_RECRUIT_UNITS").contains("OBJECT_TYPE_LIST")
		and _gap_detail(dispatch, "TEAM_RECRUIT_UNITS").contains("garbled")
		and _gap_detail(dispatch, "TEAM_RECRUIT_UNITS_FROM_TEAM").contains("OBJECT_TYPE_LIST"),
		str(dispatch.gaps.to_lines())
	)

	_act(harness, "SET_REF_TO_NEREST_TEAM_OF_TYPE_OWNED_BY_PLAYER", [
		_name_arg("Synthetic_Type_List"), _player_arg("<This Player>"),
		_name_arg("Assault"), _name_arg("REF_SYNTHETIC_GATE")
	])
	_check(
		"the_nearest_reference_gap_names_the_missing_anchor_team",
		_gap_detail(dispatch, "SET_REF_TO_NEREST_TEAM_OF_TYPE_OWNED_BY_PLAYER").contains(
			"anchor_team"
		),
		str(dispatch.gaps.to_lines())
	)

	# Every one of the six reported BLOCKED above, so none of them silently
	# degraded into BAD_ARGUMENTS or UNSUPPORTED on the way.
	var not_gapped: Array[String] = []
	for name: String in GAP_REGISTERED:
		if not dispatch.gaps.has("action", name, GapLog.REASON_BLOCKED_SUBSYSTEM):
			not_gapped.append(name)
	_check(
		"all_six_gap_registered_members_recorded_a_blocked_gap",
		not_gapped.is_empty(),
		"missing=%s log=%s" % [str(not_gapped), str(dispatch.gaps.to_lines())]
	)

	# Every gap-registered member reports BLOCKED rather than a silent skip.
	var not_blocked: Array[String] = []
	for name: String in GAP_REGISTERED:
		if not dispatch.blocked_names().has(name):
			not_blocked.append(name)
	_check("all_six_gap_registered_members_are_declared", not_blocked.is_empty(), str(not_blocked))


# --- Stub world -----------------------------------------------------------
#
# SageScriptWorldStub teaches only the Players facet, and this package touches
# Teams, Units and Orders. Those facets are taught HERE rather than in the
# shared stub, because the shared stub is an existing file that ~17 concurrent
# packages also depend on. Every command is recorded as a flat string so a test
# asserts on the exact arguments that reached the world, including the folded
# booleans; every read answers from a fixture or REFUSES, so a test that forgot
# to set one gets a loud refusal rather than a plausible zero.


class TeamWorld:
	extends SageScriptWorldStub

	var calls: Array[String] = []
	var team_state: Dictionary = {}
	var team_units: Dictionary = {}
	var team_created: Dictionary = {}
	var team_custom_state: Dictionary = {}

	func _make_teams() -> SageScriptWorld.Teams:
		return StubTeams.new()

	func _make_units() -> SageScriptWorld.Units:
		return StubUnits.new()

	func _make_orders() -> SageScriptWorld.Orders:
		return StubOrders.new()


class StubTeams:
	extends SageScriptWorld.Teams

	func _log(text: String) -> bool:
		(world as TeamWorld).calls.append(text)
		return true

	func state(team: String) -> SageWorldQuery:
		var fixtures: Dictionary = (world as TeamWorld).team_state
		if not fixtures.has(team):
			return _refuse_query("teams.state", "no fixture state for team '%s'" % team)
		return SageWorldQuery.hit(fixtures[team])

	func unit_count(team: String) -> SageWorldQuery:
		var fixtures: Dictionary = (world as TeamWorld).team_units
		if not fixtures.has(team):
			return _refuse_query("teams.unit_count", "no fixture count for team '%s'" % team)
		return SageWorldQuery.hit(int(fixtures[team]))

	func was_created(team: String) -> SageWorldQuery:
		var fixtures: Dictionary = (world as TeamWorld).team_created
		if not fixtures.has(team):
			return _refuse_query("teams.was_created", "no fixture edge for team '%s'" % team)
		return SageWorldQuery.hit(bool(fixtures[team]))

	func custom_state(team: String) -> SageWorldQuery:
		var fixtures: Dictionary = (world as TeamWorld).team_custom_state
		if not fixtures.has(team):
			return _refuse_query("teams.custom_state", "no fixture states for team '%s'" % team)
		return SageWorldQuery.hit(fixtures[team])

	func set_state(team: String, team_state: String) -> bool:
		return _log("teams.set_state|%s|%s" % [team, team_state])

	func merge_into(team: String, destination: String) -> bool:
		return _log("teams.merge_into|%s|%s" % [team, destination])

	func transfer_to_player(team: String, player: String) -> bool:
		return _log("teams.transfer_to_player|%s|%s" % [team, player])

	func set_reference(reference: String, team: String) -> bool:
		return _log("teams.set_reference|%s|%s" % [reference, team])

	func set_attitude(team: String, mood: int) -> bool:
		return _log("teams.set_attitude|%s|%d" % [team, mood])

	func stop(team: String, disband: bool) -> bool:
		return _log("teams.stop|%s|%s" % [team, str(disband).to_lower()])

	func delete(team: String, living_only: bool) -> bool:
		return _log("teams.delete|%s|%s" % [team, str(living_only).to_lower()])

	func exit_all(team: String, buildings_only: bool) -> bool:
		return _log("teams.exit_all|%s|%s" % [team, str(buildings_only).to_lower()])

	func execute_sequential_script(team: String, script: String, looping: bool) -> bool:
		return _log(
			"teams.execute_sequential_script|%s|%s|%s" % [team, script, str(looping).to_lower()]
		)

	func stop_sequential_script(team: String) -> bool:
		return _log("teams.stop_sequential_script|%s" % team)


class StubUnits:
	extends SageScriptWorld.Units

	func set_team(object_name: String, team: String) -> bool:
		(world as TeamWorld).calls.append("units.set_team|%s|%s" % [object_name, team])
		return true


class StubOrders:
	extends SageScriptWorld.Orders

	func hunt(scope: int, name: String, command_button: String) -> bool:
		(world as TeamWorld).calls.append("orders.hunt|%d|%s|%s" % [scope, name, command_button])
		return true


# --- Reporting ------------------------------------------------------------


func _gap_detail(dispatch: SageScriptDispatch, name: String) -> String:
	## The blocked-subsystem gap detail recorded for `name`, or "" if no such gap
	## exists. Returns a String rather than indexing the entry dictionary
	## directly so that a MISSING gap fails the assertion that wanted it, instead
	## of throwing and taking the rest of the run down with it.
	var key := "action|%s|%s" % [name, GapLog.REASON_BLOCKED_SUBSYSTEM]
	if not dispatch.gaps.entries.has(key):
		return ""
	return String((dispatch.gaps.entries[key] as Dictionary)["detail"])


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("HANDLERS_WP15 PASS %s%s" % [name, " (%s)" % detail if detail != "" else ""])
	else:
		failed += 1
		printerr("HANDLERS_WP15 FAIL %s%s" % [name, " (%s)" % detail if detail != "" else ""])
