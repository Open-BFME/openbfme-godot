extends SceneTree

## Proof runner for WP09 AI CORE - the seven experience/upgrade/spellbook members
## the retail AI script libraries call.
##
## What this runner is actually trying to catch, in priority order:
##
##   1. THE SCIENCE PAIR IS 16.1% OF ALL RETAIL AI CALLS. PLAYER_PURCHASE_SCIENCE
##      must reach `purchase_science` and NOTHING else - routing it to
##      `grant_science` would hand the AI its whole spellbook free and every
##      "the action returned OK" test would still pass. Asserted directly.
##
##   2. A CONDITION THAT COULD NOT BE EVALUATED MUST NOT LOOK LIKE `false`.
##      PLAYER_CAN_PURCHASE_SCIENCE is the most-called condition in the AI
##      vocabulary. Genuine-false and could-not-answer both make the gate not
##      fire, so the only thing that distinguishes them is the gap log: the tests
##      below assert that a genuine false records NO gap and a refused world read
##      records a `world-refused` one.
##
##   3. THE ARGUMENT TRAP. sage_scb.py fills integer AND real AND text for every
##      argument, so a handler reading the wrong field or the wrong slot gets a
##      plausible value instead of an error. The fixtures below deliberately load
##      DECOY integer and real payloads into the string slots, and put a
##      science-looking string in the player slot, so a type-searching or
##      wrong-field read produces a visibly wrong call rather than a pass.
##
##   4. The gap-registered wall action refuses loudly, names its missing world
##      surface, never touches the world, and is never counted as coverage. The
##      threat condition pair - formerly gap-registered here - is served by
##      WP21-threat-queries since the owner ruling of 2026-08-08, and this
##      runner asserts the handoff: censused here, registered there, evaluable
##      through the full registry, and no longer dispatch-blocked.
##
## Synthetic values only. The retail decode supplied the SHAPES (a player
## indirection, a SCIENCE_* token, a comparison enum, a radius); the values here
## are invented, because retail payloads do not get pinned into this repo.
##
## Invocation:
##   Godot_v4.7-stable_win64_console.exe --headless --path game \
##     -s res://tests/handlers_wp09_ai_core_runner.gd

const Registry := preload("res://src/script/handlers/_registry.gd")
const Dispatch := preload("res://src/script/script_dispatch.gd")
const CoreHandlers := preload("res://src/script/script_handlers_core.gd")
const Env := preload("res://src/script/script_env.gd")
const GapLog := preload("res://src/script/script_gaps.gd")
const ParamTypes := preload("res://src/script/script_param_types.gd")
const Wp09 := preload("res://src/script/handlers/wp09_ai_core.gd")

## Synthetic fixture values. Shaped like the retail call sites the decode
## reported, invented in content.
const PLAYER := "<This Player>"
const SCIENCE := "SCIENCE_TestBeacon"
const OTHER_SCIENCE := "SCIENCE_TestSecondBeacon"
const UPGRADE := "Upgrade_TestFireArrows"

## LIVENESS GUARD. A GDScript RUNTIME error aborts the enclosing function
## on the spot without propagating, so every later _check() in that function
## silently never runs and `failed` never moves - the runner then prints a
## zero-failure result and exits 0 with SCRIPT ERROR lines above it. This is
## the exact count a HEALTHY run makes; if the run makes any other number,
## something aborted (or an assertion was added without updating this) and
## the result is not to be trusted.
const EXPECTED_CHECKS := 73

var passed := 0
var failed := 0
var worlds_to_release: Array = []


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	_test_member_census_matches_the_decode()
	_test_registration()
	_test_purchase_science_reaches_the_world()
	_test_purchase_science_is_not_grant()
	_test_purchase_science_does_not_pre_gate_itself()
	_test_purchase_science_refusal_is_honest()
	_test_can_purchase_science_answers()
	_test_can_purchase_science_refusal_is_not_a_false()
	_test_can_purchase_science_is_side_effect_free()
	_test_can_purchase_science_inversion()
	_test_has_object_of_veterancy()
	_test_build_upgrade_reaches_the_world()
	_test_build_upgrade_is_not_give_upgrade()
	_test_arguments_are_positional_and_typed()
	_test_gap_registered_members()
	_test_a_bare_world_refuses_rather_than_pretending()
	_report()
	_release_worlds()
	var ran := passed + failed
	if ran != EXPECTED_CHECKS:
		failed += 1
		printerr("HANDLERS_WP09 FAIL liveness: ran %d checks, expected %d - a function aborted before its assertions" % [ran, EXPECTED_CHECKS])
	print("HANDLERS_WP09_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


# --- Fixtures ---------------------------------------------------------------


func _argument(
	argument_type: int, integer: int = 0, real: float = 0.0, text: String = ""
) -> Dictionary:
	return {"argumentType": argument_type, "integer": integer, "real": real, "text": text}


func _player_arg(name: String) -> Dictionary:
	# PLAYER is one of the few parameter types this repo has an observed argument
	# code for, so the code is asserted on by SageScriptArgs.validate().
	# The decoy integer/real are the trap: a handler reading the wrong FIELD of
	# the right slot would get 777 / 7.77 instead of the player name.
	return _argument(ParamTypes.ARGUMENT_PLAYER, 777, 7.77, name)


func _name_arg(value: String) -> Dictionary:
	# SCIENCE / UPGRADE / OBJECT_TYPE: no observed argument code, so the code is
	# not asserted on and the payload is read from `text`. Decoys again.
	return _argument(99, 555, 5.55, value)


func _compare_arg(value: int) -> Dictionary:
	return _argument(ParamTypes.ARGUMENT_COMPARISON, value, float(value), "COMPARISON_DECOY")


func _real_arg(value: float) -> Dictionary:
	return _argument(ParamTypes.ARGUMENT_REAL, int(value), value, "REAL_DECOY")


func _int_arg(value: int) -> Dictionary:
	return _argument(ParamTypes.ARGUMENT_INTEGER, value, float(value), "INT_DECOY")


func _record(name: String, arguments: Array, inverted: bool = false) -> Dictionary:
	return {
		"contentType": 0,
		"internalName": {"name": name, "wireTypeCode": 3},
		"arguments": arguments,
		"enabled": true,
		"inverted": inverted,
	}


func _harness() -> Dictionary:
	## Tranche 1 plus every auto-registered package, exactly as the game builds
	## it, over a world whose Progression facet is instrumented.
	var dispatch := Dispatch.new()
	CoreHandlers.register_all(dispatch)
	var outcome := Registry.register_all(dispatch)
	var world := Wp09World.new(20260726)
	worlds_to_release.append(world)
	return {
		"dispatch": dispatch,
		"env": Env.new(),
		"world": world,
		"registration": outcome,
	}


func _progression(harness: Dictionary) -> StubProgression:
	return (harness["world"] as Wp09World).progression() as StubProgression


func _release_worlds() -> void:
	## SageScriptWorld facets point back to their cached world. Break those
	## RefCounted cycles before the process exits so a passing runner is clean.
	for world in worlds_to_release:
		for facet in world._facets.values():
			facet.world = null
		world._facets.clear()
	worlds_to_release.clear()


func _act(harness: Dictionary, name: String, arguments: Array) -> int:
	return (harness["dispatch"] as SageScriptDispatch).execute_action(
		_record(name, arguments), harness["env"], harness["world"], "fixture"
	)


func _cond(harness: Dictionary, name: String, arguments: Array, inverted: bool = false) -> bool:
	return (harness["dispatch"] as SageScriptDispatch).evaluate_condition(
		_record(name, arguments, inverted), harness["env"], harness["world"], "fixture"
	)


# --- 1. Census --------------------------------------------------------------


func _test_member_census_matches_the_decode() -> void:
	## The seven names are evidence, not a judgement call: they are the WP09
	## members the decode of the 14 referenced retail AI libraries reported as
	## called. This asserts the file serves or gap-registers each one exactly
	## once, and claims nothing beyond them - so a later edit that quietly drops
	## the 461-call-site condition, or that scope-creeps into the other 31 WP09
	## members owned by another agent, fails here.
	var declared: Array = (
		Wp09.SERVED_ACTIONS + Wp09.SERVED_CONDITIONS
		+ Wp09.BLOCKED_ACTIONS + Wp09.SERVED_BY_WP21_CONDITIONS
	)
	declared.sort()
	var expected: Array = Wp09.AI_USED_MEMBERS.duplicate()
	expected.sort()
	_check(
		"the_seven_ai_used_wp09_members_are_all_accounted_for",
		declared == expected,
		"declared=%s expected=%s" % [str(declared), str(expected)]
	)
	_check(
		"no_member_is_both_served_and_blocked",
		declared.size() == 7,
		"declared=%s" % str(declared)
	)
	# The wp21 handoff (owner ruling 2026-08-08) is exactly the threat pair -
	# nothing more may quietly move out of this package's ownership, and the
	# handoff list may not overlap what this file still serves or blocks.
	var handoff: Array = Wp09.SERVED_BY_WP21_CONDITIONS.duplicate()
	handoff.sort()
	_check(
		"the_wp21_handoff_is_exactly_the_threat_pair",
		handoff == ["TEAM_THREAT_LEVEL", "UNIT_THREAT_LEVEL"],
		"handoff=%s" % str(handoff)
	)


# --- 2. Registration --------------------------------------------------------


func _test_registration() -> void:
	var harness := _harness()
	var outcome: Dictionary = harness["registration"]
	var dispatch: SageScriptDispatch = harness["dispatch"]

	_check(
		"registration_reports_no_errors",
		(outcome["errors"] as Array).is_empty(),
		str(outcome["errors"])
	)
	_check(
		"the_package_is_registered_under_its_own_name",
		(outcome["packages"] as Array).has(Wp09.PACKAGE),
		str(outcome["packages"])
	)

	var missing_actions: Array = Wp09.SERVED_ACTIONS.filter(
		func(name): return not dispatch.action_handlers.has(name)
	)
	_check("every_served_action_is_registered", missing_actions.is_empty(), str(missing_actions))
	var missing_conditions: Array = Wp09.SERVED_CONDITIONS.filter(
		func(name): return not dispatch.condition_handlers.has(name)
	)
	_check(
		"every_served_condition_is_registered",
		missing_conditions.is_empty(),
		str(missing_conditions)
	)

	var all_blocked: Array = Wp09.BLOCKED_ACTIONS.duplicate()
	var unblocked: Array = all_blocked.filter(
		func(name): return not dispatch.blocked_names().has(name)
	)
	_check("every_gap_registered_member_is_blocked", unblocked.is_empty(), str(unblocked))

	# Blocked members must never inflate coverage - that is the whole reason the
	# blocked bucket exists.
	var counted: Array[String] = []
	for name: String in all_blocked:
		if dispatch.implemented_actions().has(name) \
				or dispatch.implemented_conditions().has(name):
			counted.append(name)
	_check("blocked_members_are_not_counted_as_implemented", counted.is_empty(), str(counted))

	var served_uncounted: Array = Wp09.SERVED_ACTIONS.filter(
		func(name): return not dispatch.implemented_actions().has(name)
	)
	_check(
		"served_actions_are_counted_as_coverage",
		served_uncounted.is_empty(),
		str(served_uncounted)
	)
	_check(
		"both_served_conditions_are_counted_as_coverage",
		dispatch.implemented_conditions().has("PLAYER_CAN_PURCHASE_SCIENCE")
		and dispatch.implemented_conditions().has("PLAYER_HAS_OBJECT_OF_VETERANCY")
	)

	# WP01 is the pilot and must stay whole: if this package had collided with a
	# name WP01 owns, the registrar would have kept WP01's and dropped ours, and
	# the errors array above would say so. Check the pilot's own members survive.
	_check(
		"the_wp01_pilot_is_undisturbed",
		dispatch.action_handlers.has("SET_COUNTER_IN_SECONDS")
		and dispatch.condition_handlers.has("COUNTER_SECONDS")
	)

	# Determinism: two independent builds register identically.
	var twin := _harness()
	_check(
		"two_runs_register_identically",
		dispatch.implemented_actions()
		== (twin["dispatch"] as SageScriptDispatch).implemented_actions()
		and dispatch.implemented_conditions()
		== (twin["dispatch"] as SageScriptDispatch).implemented_conditions()
		and dispatch.blocked_names() == (twin["dispatch"] as SageScriptDispatch).blocked_names()
	)


# --- 3. PLAYER_PURCHASE_SCIENCE --------------------------------------------


func _test_purchase_science_reaches_the_world() -> void:
	var harness := _harness()
	var status := _act(
		harness, "PLAYER_PURCHASE_SCIENCE", [_player_arg(PLAYER), _name_arg(SCIENCE)]
	)
	var progression := _progression(harness)
	_check("purchase_science_returns_ok", status == Dispatch.Status.OK, "status=%d" % status)
	_check(
		"purchase_science_called_the_world_once",
		progression.purchase_calls.size() == 1,
		str(progression.purchase_calls)
	)
	_check(
		"purchase_science_passed_player_then_science_in_that_order",
		progression.purchase_calls == [[PLAYER, SCIENCE]],
		str(progression.purchase_calls)
	)
	_check(
		"purchase_science_recorded_no_gap",
		(harness["dispatch"] as SageScriptDispatch).gaps.is_empty(),
		str((harness["dispatch"] as SageScriptDispatch).gaps.to_lines())
	)


func _test_purchase_science_is_not_grant() -> void:
	# The failure this exists for: `grant_science` gives the science away free
	# with no prerequisites, so a handler wired to it would make all 461 AI call
	# sites "work" while giving the AI the entire spellbook on turn one.
	var harness := _harness()
	_act(harness, "PLAYER_PURCHASE_SCIENCE", [_player_arg(PLAYER), _name_arg(SCIENCE)])
	var progression := _progression(harness)
	_check(
		"purchase_science_never_grants",
		progression.grant_calls.is_empty(),
		str(progression.grant_calls)
	)
	_check(
		"purchase_science_never_changes_science_availability",
		progression.availability_calls.is_empty(),
		str(progression.availability_calls)
	)


func _test_purchase_science_does_not_pre_gate_itself() -> void:
	# The action is authored as an ATTEMPT: eligibility belongs to the attempt,
	# not to a copy of the condition inside the action. A handler that pre-checked
	# can_purchase_science and skipped would (a) duplicate a check that can then
	# disagree with the world's own, and (b) run a condition as a side effect of
	# an action. Here the world would answer "cannot purchase", and the attempt
	# must still be made.
	var harness := _harness()
	var progression := _progression(harness)
	progression.set_can_purchase(PLAYER, SCIENCE, false)
	var status := _act(
		harness, "PLAYER_PURCHASE_SCIENCE", [_player_arg(PLAYER), _name_arg(SCIENCE)]
	)
	_check(
		"purchase_science_still_attempts_when_the_world_says_it_is_unaffordable",
		status == Dispatch.Status.OK and progression.purchase_calls == [[PLAYER, SCIENCE]],
		"status=%d calls=%s" % [status, str(progression.purchase_calls)]
	)
	_check(
		"purchase_science_did_not_evaluate_the_condition_for_itself",
		progression.can_purchase_calls.is_empty(),
		str(progression.can_purchase_calls)
	)


func _test_purchase_science_refusal_is_honest() -> void:
	var harness := _harness()
	var world: Wp09World = harness["world"]
	world.expect_refusals()
	var progression := _progression(harness)
	progression.serve_purchase = false

	var status := _act(
		harness, "PLAYER_PURCHASE_SCIENCE", [_player_arg(PLAYER), _name_arg(SCIENCE)]
	)
	var dispatch: SageScriptDispatch = harness["dispatch"]
	_check(
		"a_world_that_cannot_purchase_gets_world_refused_not_ok",
		status == Dispatch.Status.WORLD_REFUSED,
		"status=%d" % status
	)
	_check(
		"the_refusal_is_recorded_as_a_world_refused_gap",
		dispatch.gaps.has("action", "PLAYER_PURCHASE_SCIENCE", GapLog.REASON_WORLD_REFUSED),
		str(dispatch.gaps.to_lines())
	)
	_check(
		"the_gap_names_the_missing_world_method_and_the_science",
		(
			String(
				dispatch.gaps.entries[
					"action|PLAYER_PURCHASE_SCIENCE|%s" % GapLog.REASON_WORLD_REFUSED
				]["detail"]
			).contains("progression.purchase_science")
			and String(
				dispatch.gaps.entries[
					"action|PLAYER_PURCHASE_SCIENCE|%s" % GapLog.REASON_WORLD_REFUSED
				]["detail"]
			).contains(SCIENCE)
		),
		str(dispatch.gaps.to_lines())
	)


# --- 4. PLAYER_CAN_PURCHASE_SCIENCE ----------------------------------------


func _test_can_purchase_science_answers() -> void:
	var harness := _harness()
	var progression := _progression(harness)
	progression.set_can_purchase(PLAYER, SCIENCE, true)
	progression.set_can_purchase(PLAYER, OTHER_SCIENCE, false)

	_check(
		"can_purchase_true_evaluates_true",
		_cond(harness, "PLAYER_CAN_PURCHASE_SCIENCE", [_player_arg(PLAYER), _name_arg(SCIENCE)])
	)
	_check(
		"can_purchase_false_evaluates_false",
		not _cond(
			harness, "PLAYER_CAN_PURCHASE_SCIENCE",
			[_player_arg(PLAYER), _name_arg(OTHER_SCIENCE)]
		)
	)
	_check(
		"the_condition_asked_about_the_science_in_slot_one",
		progression.can_purchase_calls == [[PLAYER, SCIENCE], [PLAYER, OTHER_SCIENCE]],
		str(progression.can_purchase_calls)
	)
	# A GENUINE false is not a gap. This is the other half of the distinction the
	# next test makes: if genuine-false also logged a gap, the gap log could not
	# be used to tell the two apart.
	_check(
		"a_genuine_false_records_no_gap",
		(harness["dispatch"] as SageScriptDispatch).gaps.is_empty(),
		str((harness["dispatch"] as SageScriptDispatch).gaps.to_lines())
	)
	_check(
		"both_evaluations_were_served",
		(harness["dispatch"] as SageScriptDispatch).served_conditions == 2,
		"served=%d" % (harness["dispatch"] as SageScriptDispatch).served_conditions
	)


func _test_can_purchase_science_refusal_is_not_a_false() -> void:
	# The most load-bearing assertion in this runner. A world that cannot answer
	# must not be read as "the player cannot afford it". The VALUE is false either
	# way - an unevaluable gate must never fire - so the only observable
	# difference is the gap, and that difference has to exist.
	var harness := _harness()
	var world: Wp09World = harness["world"]
	world.expect_refusals()
	# No fixture is set for this science, so the stub refuses the query.
	var value := _cond(
		harness, "PLAYER_CAN_PURCHASE_SCIENCE", [_player_arg(PLAYER), _name_arg(SCIENCE)]
	)
	var dispatch: SageScriptDispatch = harness["dispatch"]
	_check("an_unevaluable_condition_does_not_fire_the_gate", not value)
	_check(
		"an_unevaluable_condition_is_recorded_as_world_refused",
		dispatch.gaps.has(
			"condition", "PLAYER_CAN_PURCHASE_SCIENCE", GapLog.REASON_WORLD_REFUSED
		),
		str(dispatch.gaps.to_lines())
	)
	_check(
		"the_condition_gap_names_the_world_method_and_the_science",
		(
			String(
				dispatch.gaps.entries[
					"condition|PLAYER_CAN_PURCHASE_SCIENCE|%s" % GapLog.REASON_WORLD_REFUSED
				]["detail"]
			).contains("progression.can_purchase_science")
			and String(
				dispatch.gaps.entries[
					"condition|PLAYER_CAN_PURCHASE_SCIENCE|%s" % GapLog.REASON_WORLD_REFUSED
				]["detail"]
			).contains(SCIENCE)
		),
		str(dispatch.gaps.to_lines())
	)
	_check(
		"an_unevaluable_condition_is_not_counted_as_served",
		dispatch.served_conditions == 0,
		"served=%d" % dispatch.served_conditions
	)
	_check(
		"the_world_logged_the_refusal_against_the_facet_method",
		world.refused("progression.can_purchase_science"),
		str(world.refusal_log)
	)


func _test_can_purchase_science_is_side_effect_free() -> void:
	# Conditions are evaluated an unpredictable number of times, so any state
	# change inside one is a lockstep determinism bug.
	var harness := _harness()
	var env: SageScriptEnv = harness["env"]
	var progression := _progression(harness)
	progression.set_can_purchase(PLAYER, SCIENCE, true)
	env.set_counter("witness", 41)
	env.set_flag("witness_flag", true)
	var before := env.snapshot()

	var first := _cond(
		harness, "PLAYER_CAN_PURCHASE_SCIENCE", [_player_arg(PLAYER), _name_arg(SCIENCE)]
	)
	var second := _cond(
		harness, "PLAYER_CAN_PURCHASE_SCIENCE", [_player_arg(PLAYER), _name_arg(SCIENCE)]
	)
	var third := _cond(
		harness, "PLAYER_CAN_PURCHASE_SCIENCE", [_player_arg(PLAYER), _name_arg(SCIENCE)]
	)

	_check(
		"repeated_evaluation_gives_the_same_answer",
		first and second and third
	)
	_check(
		"evaluating_the_condition_did_not_touch_the_interpreter_state",
		env.snapshot() == before,
		"before=%s after=%s" % [str(before), str(env.snapshot())]
	)
	_check(
		"evaluating_the_condition_never_mutated_the_world",
		progression.mutation_count() == 0,
		"purchases=%s grants=%s builds=%s" % [
			str(progression.purchase_calls),
			str(progression.grant_calls),
			str(progression.build_calls),
		]
	)


func _test_can_purchase_science_inversion() -> void:
	# `inverted` is applied by the dispatcher, not by the handler - but an
	# unevaluable condition must stay false THROUGH inversion, or a NOT-wrapped
	# gate would fire precisely because the world could not answer.
	var harness := _harness()
	var progression := _progression(harness)
	progression.set_can_purchase(PLAYER, SCIENCE, true)
	_check(
		"inverting_a_true_condition_yields_false",
		not _cond(
			harness, "PLAYER_CAN_PURCHASE_SCIENCE",
			[_player_arg(PLAYER), _name_arg(SCIENCE)], true
		)
	)

	var refusing := _harness()
	(refusing["world"] as Wp09World).expect_refusals()
	_check(
		"inverting_an_unevaluable_condition_still_yields_false",
		not _cond(
			refusing, "PLAYER_CAN_PURCHASE_SCIENCE",
			[_player_arg(PLAYER), _name_arg(SCIENCE)], true
		)
	)


func _test_has_object_of_veterancy() -> void:
	var object_type := "TestEconomyBuildings"
	var harness := _harness()
	var progression := _progression(harness)
	progression.set_veterancy_answer(
		PLAYER, object_type, ParamTypes.COMPARE_GREATER_EQUAL, 2, true
	)
	_check(
		"veterancy_true_fires_the_gate",
		_cond(
			harness, "PLAYER_HAS_OBJECT_OF_VETERANCY",
			[
				_player_arg(PLAYER), _name_arg(object_type),
				_compare_arg(ParamTypes.COMPARE_GREATER_EQUAL), _int_arg(2),
			]
		)
	)
	_check(
		"veterancy_passes_all_four_arguments_positionally",
		progression.veterancy_calls == [[
			PLAYER, object_type, ParamTypes.COMPARE_GREATER_EQUAL, 2,
		]],
		str(progression.veterancy_calls)
	)
	_check(
		"veterancy_true_records_no_gap",
		(harness["dispatch"] as SageScriptDispatch).gaps.entries.is_empty(),
		str((harness["dispatch"] as SageScriptDispatch).gaps.to_lines())
	)
	progression.set_veterancy_answer(
		PLAYER, object_type, ParamTypes.COMPARE_GREATER_EQUAL, 2, false
	)
	_check(
		"veterancy_genuine_false_does_not_fire",
		not _cond(
			harness, "PLAYER_HAS_OBJECT_OF_VETERANCY",
			[
				_player_arg(PLAYER), _name_arg(object_type),
				_compare_arg(ParamTypes.COMPARE_GREATER_EQUAL), _int_arg(2),
			]
		)
	)
	_check(
		"veterancy_genuine_false_records_no_gap",
		(harness["dispatch"] as SageScriptDispatch).gaps.entries.is_empty(),
		str((harness["dispatch"] as SageScriptDispatch).gaps.to_lines())
	)

	var refusing := _harness()
	(refusing["world"] as Wp09World).expect_refusals()
	_check(
		"veterancy_world_refusal_does_not_fire_and_is_logged",
		not _cond(
			refusing, "PLAYER_HAS_OBJECT_OF_VETERANCY",
			[
				_player_arg(PLAYER), _name_arg(object_type),
				_compare_arg(ParamTypes.COMPARE_GREATER_EQUAL), _int_arg(2),
			]
		)
		and (refusing["dispatch"] as SageScriptDispatch).gaps.has(
			"condition", "PLAYER_HAS_OBJECT_OF_VETERANCY", GapLog.REASON_WORLD_REFUSED
		),
		str((refusing["dispatch"] as SageScriptDispatch).gaps.to_lines())
	)

	var invalid := _harness()
	_check(
		"veterancy_invalid_comparison_is_bad_arguments_before_world_call",
		not _cond(
			invalid, "PLAYER_HAS_OBJECT_OF_VETERANCY",
			[
				_player_arg(PLAYER), _name_arg(object_type),
				_compare_arg(99), _int_arg(2),
			]
		)
		and (invalid["dispatch"] as SageScriptDispatch).gaps.has(
			"condition", "PLAYER_HAS_OBJECT_OF_VETERANCY", GapLog.REASON_BAD_ARGUMENTS
		)
		and _progression(invalid).veterancy_calls.is_empty(),
		str((invalid["dispatch"] as SageScriptDispatch).gaps.to_lines())
	)
	_check(
		"veterancy_condition_does_not_mutate_the_world",
		progression.mutation_count() == 0,
		"mutation_count=%d" % progression.mutation_count()
	)


# --- 5. AI_PLAYER_BUILD_UPGRADE --------------------------------------------


func _test_build_upgrade_reaches_the_world() -> void:
	var harness := _harness()
	var status := _act(
		harness, "AI_PLAYER_BUILD_UPGRADE", [_player_arg(PLAYER), _name_arg(UPGRADE)]
	)
	var progression := _progression(harness)
	_check("build_upgrade_returns_ok", status == Dispatch.Status.OK, "status=%d" % status)
	_check(
		"build_upgrade_passed_player_then_upgrade_in_that_order",
		progression.build_calls == [[PLAYER, UPGRADE]],
		str(progression.build_calls)
	)

	var refusing := _harness()
	(refusing["world"] as Wp09World).expect_refusals()
	(_progression(refusing)).serve_build = false
	var refused_status := _act(
		refusing, "AI_PLAYER_BUILD_UPGRADE", [_player_arg(PLAYER), _name_arg(UPGRADE)]
	)
	_check(
		"a_world_that_cannot_queue_an_upgrade_says_world_refused",
		refused_status == Dispatch.Status.WORLD_REFUSED,
		"status=%d" % refused_status
	)
	_check(
		"the_build_upgrade_gap_names_the_missing_world_method",
		String(
			(refusing["dispatch"] as SageScriptDispatch).gaps.entries[
				"action|AI_PLAYER_BUILD_UPGRADE|%s" % GapLog.REASON_WORLD_REFUSED
			]["detail"]
		).contains("progression.build_upgrade"),
		str((refusing["dispatch"] as SageScriptDispatch).gaps.to_lines())
	)


func _test_build_upgrade_is_not_give_upgrade() -> void:
	# "Have AI <PLAYER> BUILD this upgrade" - it is produced and paid for, not
	# handed over. `give_upgrade` would make every AI faction upgrade instant and
	# free while looking identical from the script's side.
	var harness := _harness()
	_act(harness, "AI_PLAYER_BUILD_UPGRADE", [_player_arg(PLAYER), _name_arg(UPGRADE)])
	var progression := _progression(harness)
	_check(
		"build_upgrade_never_gives_the_upgrade_outright",
		progression.give_upgrade_calls.is_empty(),
		str(progression.give_upgrade_calls)
	)


# --- 6. Arguments -----------------------------------------------------------


func _test_arguments_are_positional_and_typed() -> void:
	# 6a. WRONG ARITY IS A HARD FAILURE, and the world must not be touched.
	var short_harness := _harness()
	var short_status := _act(short_harness, "PLAYER_PURCHASE_SCIENCE", [_player_arg(PLAYER)])
	_check(
		"a_one_argument_purchase_record_is_bad_arguments",
		short_status == Dispatch.Status.BAD_ARGUMENTS,
		"status=%d" % short_status
	)
	_check(
		"a_bad_arity_record_never_reached_the_world",
		_progression(short_harness).mutation_count() == 0,
		str(_progression(short_harness).purchase_calls)
	)
	_check(
		"a_bad_arity_record_is_recorded_as_bad_arguments",
		(short_harness["dispatch"] as SageScriptDispatch).gaps.has(
			"action", "PLAYER_PURCHASE_SCIENCE", GapLog.REASON_BAD_ARGUMENTS
		),
		str((short_harness["dispatch"] as SageScriptDispatch).gaps.to_lines())
	)

	# The condition path answers with a bool, so its arity failure is visible as
	# "did not fire, and said why" rather than as a status code.
	var long_harness := _harness()
	var long_value := _cond(
		long_harness, "PLAYER_CAN_PURCHASE_SCIENCE",
		[_player_arg(PLAYER), _name_arg(SCIENCE), _name_arg(OTHER_SCIENCE)]
	)
	var long_dispatch: SageScriptDispatch = long_harness["dispatch"]
	_check("a_three_argument_condition_record_does_not_fire_the_gate", not long_value)
	_check(
		"a_three_argument_condition_record_is_recorded_as_bad_arguments",
		long_dispatch.gaps.has(
			"condition", "PLAYER_CAN_PURCHASE_SCIENCE", GapLog.REASON_BAD_ARGUMENTS
		),
		str(long_dispatch.gaps.to_lines())
	)
	_check(
		"a_three_argument_condition_record_never_reached_the_world",
		_progression(long_harness).total_calls() == 0,
		str(_progression(long_harness).can_purchase_calls)
	)

	# 6b. THE PLAYER SLOT'S ARGUMENT CODE IS ASSERTED ON. PLAYER is one of the
	# parameter types this repo has an observed code for, so a record carrying a
	# non-player code in slot 0 is a decode disagreement, not something to read
	# through.
	var miscoded := _harness()
	var miscoded_status := _act(
		miscoded, "PLAYER_PURCHASE_SCIENCE", [_name_arg(PLAYER), _name_arg(SCIENCE)]
	)
	_check(
		"a_player_slot_carrying_the_wrong_argument_code_is_rejected",
		miscoded_status == Dispatch.Status.BAD_ARGUMENTS,
		"status=%d" % miscoded_status
	)

	# 6c. THE TRAP ITSELF. Every fixture argument above carries decoy integer and
	# real payloads (777/7.77, 555/5.55). Both signatures are (name, name), so a
	# handler that read the wrong FIELD, or searched for "the string one", would
	# pass a decoy or the wrong slot to the world. Feeding a science-shaped token
	# through the PLAYER slot makes a slot swap unmistakable: if the arguments
	# were transposed the world would be asked to buy "<This Player>" for player
	# "SCIENCE_...".
	var trap := _harness()
	_act(trap, "PLAYER_PURCHASE_SCIENCE", [_player_arg(SCIENCE), _name_arg(PLAYER)])
	_check(
		"the_handler_reads_slot_zero_as_the_player_and_slot_one_as_the_science",
		_progression(trap).purchase_calls == [[SCIENCE, PLAYER]],
		str(_progression(trap).purchase_calls)
	)
	_check(
		"no_decoy_integer_or_real_payload_reached_the_world",
		not str(_progression(trap).purchase_calls).contains("777")
		and not str(_progression(trap).purchase_calls).contains("555"),
		str(_progression(trap).purchase_calls)
	)


# --- 7. The four gap-registered members ------------------------------------


func _test_gap_registered_members() -> void:
	## The wall action stays blocked pending sim surface. The threat pair was
	## unblocked by owner ruling 2026-08-08 and is served by
	## WP21-threat-queries: it must now EVALUATE through the full registry
	## (against this harness's instrumented Teams/Units facets) and record no
	## blocked-subsystem gap.
	var harness := _harness()
	var dispatch: SageScriptDispatch = harness["dispatch"]

	var wall_status := _act(
		harness, "UPGRADE_NEAREST_WALL",
		[
			_name_arg("AI_TEST_BASE"), _name_arg(UPGRADE), _name_arg("TestCastleUpgrade"),
			_name_arg("TestCastleFront"), _name_arg("AI_TEST_REFERENCE"),
		]
	)
	_check(
		"upgrade_nearest_wall_reports_blocked",
		wall_status == Dispatch.Status.BLOCKED,
		"status=%d" % wall_status
	)
	_check(
		"upgrade_nearest_wall_gaps_as_blocked_subsystem",
		dispatch.gaps.has("action", "UPGRADE_NEAREST_WALL", GapLog.REASON_BLOCKED_SUBSYSTEM),
		str(dispatch.gaps.to_lines())
	)

	# The retail AI shapes (AI_GATE | < | 10 | 350 and <This Team> | >= | 1 |
	# 300), against seeded facet threat values that satisfy both comparisons.
	var world: Wp09World = harness["world"]
	world.unit_threat_values["AI_TEST_GATE"] = {350.0: 5.0}
	world.team_threat_values["<This Team>"] = {300.0: 2.0}

	var unit_threat := _cond(
		harness, "UNIT_THREAT_LEVEL",
		[
			_name_arg("AI_TEST_GATE"), _compare_arg(ParamTypes.COMPARE_LESS),
			_real_arg(10.0), _real_arg(350.0),
		]
	)
	_check(
		"the_unit_threat_condition_now_evaluates_through_wp21",
		unit_threat,
		str(dispatch.gaps.to_lines())
	)
	_check(
		"the_unit_threat_evaluation_records_no_blocked_gap",
		not dispatch.gaps.has(
			"condition", "UNIT_THREAT_LEVEL", GapLog.REASON_BLOCKED_SUBSYSTEM
		),
		str(dispatch.gaps.to_lines())
	)

	var team_threat := _cond(
		harness, "TEAM_THREAT_LEVEL",
		[
			_name_arg("<This Team>"), _compare_arg(ParamTypes.COMPARE_GREATER_EQUAL),
			_real_arg(1.0), _real_arg(300.0),
		]
	)
	_check(
		"the_team_threat_condition_now_evaluates_through_wp21",
		team_threat,
		str(dispatch.gaps.to_lines())
	)
	_check(
		"the_team_threat_evaluation_records_no_blocked_gap",
		not dispatch.gaps.has(
			"condition", "TEAM_THREAT_LEVEL", GapLog.REASON_BLOCKED_SUBSYSTEM
		),
		str(dispatch.gaps.to_lines())
	)

	_check(
		"the_threat_pair_did_not_claim_the_action_form_of_its_name",
		not dispatch.action_handlers.has("UNIT_THREAT_LEVEL")
		and not dispatch.action_handlers.has("TEAM_THREAT_LEVEL")
	)
	_check(
		"the_wall_is_dispatch_blocked_and_the_threat_pair_is_not",
		dispatch.blocked_names().has("UPGRADE_NEAREST_WALL")
		and not dispatch.blocked_names().has("UNIT_THREAT_LEVEL")
		and not dispatch.blocked_names().has("TEAM_THREAT_LEVEL")
	)


# --- 8. A world that models none of this -----------------------------------


func _test_a_bare_world_refuses_rather_than_pretending() -> void:
	# The base SageScriptWorld implements no Progression facet at all. Every
	# served member must degrade to WORLD_REFUSED, never to OK-and-nothing-
	# happened, and the condition must not become a confident false.
	var dispatch := Dispatch.new()
	CoreHandlers.register_all(dispatch)
	Registry.register_all(dispatch)
	var bare := SageScriptWorld.new()
	worlds_to_release.append(bare)
	var env := Env.new()

	var purchase := dispatch.execute_action(
		_record("PLAYER_PURCHASE_SCIENCE", [_player_arg(PLAYER), _name_arg(SCIENCE)]),
		env, bare, "fixture"
	)
	var build := dispatch.execute_action(
		_record("AI_PLAYER_BUILD_UPGRADE", [_player_arg(PLAYER), _name_arg(UPGRADE)]),
		env, bare, "fixture"
	)
	_check(
		"a_bare_world_refuses_the_science_purchase",
		purchase == Dispatch.Status.WORLD_REFUSED,
		"status=%d" % purchase
	)
	_check(
		"a_bare_world_refuses_the_upgrade_order",
		build == Dispatch.Status.WORLD_REFUSED,
		"status=%d" % build
	)

	var value := dispatch.evaluate_condition(
		_record("PLAYER_CAN_PURCHASE_SCIENCE", [_player_arg(PLAYER), _name_arg(SCIENCE)]),
		env, bare, "fixture"
	)
	_check("a_bare_world_does_not_claim_the_science_is_affordable", not value)
	_check(
		"a_bare_world_records_the_condition_as_world_refused_not_unimplemented",
		dispatch.gaps.has(
			"condition", "PLAYER_CAN_PURCHASE_SCIENCE", GapLog.REASON_WORLD_REFUSED
		)
		and not dispatch.gaps.has(
			"condition", "PLAYER_CAN_PURCHASE_SCIENCE", GapLog.REASON_UNIMPLEMENTED
		),
		str(dispatch.gaps.to_lines())
	)


# --- Instrumented world -----------------------------------------------------


class StubProgression:
	extends SageScriptWorld.Progression

	## The Progression facet, recording every call. WP09 is the first package to
	## need a SIMULATION facet from the stub world, and script_world_stub.gd may
	## not be edited by a package, so the fixture lives here.
	##
	## Nothing defaults to a plausible answer. `can_purchase_science` refuses
	## unless the test set a fixture, because "no fixture" and "the player cannot
	## afford it" are exactly the two things this package must never conflate.

	var purchase_calls: Array = []
	var build_calls: Array = []
	var grant_calls: Array = []
	var give_upgrade_calls: Array = []
	var availability_calls: Array = []
	var can_purchase_calls: Array = []
	var veterancy_calls: Array = []

	var serve_purchase := true
	var serve_build := true

	var _can_purchase: Dictionary = {}
	var _veterancy_answers: Dictionary = {}

	func set_can_purchase(player: String, science: String, value: bool) -> void:
		_can_purchase["%s|%s" % [player, science]] = value

	func set_veterancy_answer(
		player: String, object_type: String, comparison: int, level: int, value: bool
	) -> void:
		_veterancy_answers[
			"%s|%s|%d|%d" % [player, object_type, comparison, level]
		] = value

	var wall_bound_calls: Array = []

	func mutation_count() -> int:
		return (
			purchase_calls.size() + build_calls.size() + grant_calls.size()
			+ give_upgrade_calls.size() + availability_calls.size()
			+ wall_bound_calls.size()
		)

	func total_calls() -> int:
		return mutation_count() + can_purchase_calls.size() + veterancy_calls.size()

	func purchase_science(player: String, science: String) -> bool:
		purchase_calls.append([player, science])
		if not serve_purchase:
			return _refuse_command("progression.purchase_science")
		return true

	func can_purchase_science(player: String, science: String) -> SageWorldQuery:
		can_purchase_calls.append([player, science])
		var key := "%s|%s" % [player, science]
		if not _can_purchase.has(key):
			return _refuse_query(
				"progression.can_purchase_science",
				"no fixture for player '%s' science '%s'" % [player, science]
			)
		return SageWorldQuery.hit(bool(_can_purchase[key]))

	func has_object_of_veterancy(
		player: String, object_type: String, comparison: int, level: int
	) -> SageWorldQuery:
		veterancy_calls.append([player, object_type, comparison, level])
		var key := "%s|%s|%d|%d" % [player, object_type, comparison, level]
		if not _veterancy_answers.has(key):
			return _refuse_query(
				"progression.has_object_of_veterancy",
				"no fixture for player '%s', type '%s', comparison %d, level %d"
				% [player, object_type, comparison, level]
			)
		return SageWorldQuery.hit(bool(_veterancy_answers[key]))

	func build_upgrade(player: String, upgrade: String) -> bool:
		build_calls.append([player, upgrade])
		if not serve_build:
			return _refuse_command("progression.build_upgrade")
		return true

	# The three neighbours a mis-wired handler would plausibly reach for. They
	# accept and record, so a test can prove they were NOT called; if one of them
	# refused instead, a wrong wiring would show up as a refusal and could be
	# mistaken for an ordinary unimplemented-world gap.

	func grant_science(player: String, science: String) -> bool:
		grant_calls.append([player, science])
		return true

	func give_upgrade(scope: int, name: String, upgrade: String) -> bool:
		give_upgrade_calls.append([scope, name, upgrade])
		return true

	func set_science_availability(
		player: String, science: String, availability: String
	) -> bool:
		availability_calls.append([player, science, availability])
		return true

	func upgrade_nearest_wall_bound(
		base: String,
		upgrade: String,
		object_type: String,
		marker_type: String,
		reference: String
	) -> bool:
		wall_bound_calls.append([base, upgrade, object_type, marker_type, reference])
		return true


class Wp09World:
	extends SageScriptWorldStub

	## The shared stub world plus instrumented Progression/Teams/Units facets
	## for the formerly gap-registered threat and wall members.

	var unit_threat_values: Dictionary = {}  # unit -> {radius: threat}
	var team_threat_values: Dictionary = {}

	func _make_progression() -> SageScriptWorld.Progression:
		return StubProgression.new()

	func _make_teams() -> SageScriptWorld.Teams:
		return Wp09StubTeams.new()

	func _make_units() -> SageScriptWorld.Units:
		return Wp09StubUnits.new()


class Wp09StubTeams:
	extends SageScriptWorld.Teams

	func threat_within_radius(team: String, radius: float) -> SageWorldQuery:
		var fixtures: Dictionary = (world as Wp09World).team_threat_values
		if not fixtures.has(team):
			return _refuse_query("teams.threat_within_radius", "no fixture threat for team")
		var by_radius: Dictionary = fixtures[team]
		if by_radius.has(radius):
			return SageWorldQuery.hit(float(by_radius[radius]))
		# Any recorded radius answers when exact key missing.
		for k in by_radius.keys():
			return SageWorldQuery.hit(float(by_radius[k]))
		return SageWorldQuery.hit(0.0)


class Wp09StubUnits:
	extends SageScriptWorld.Units

	func threat_within_radius(object_name: String, radius: float) -> SageWorldQuery:
		var fixtures: Dictionary = (world as Wp09World).unit_threat_values
		if not fixtures.has(object_name):
			return _refuse_query("units.threat_within_radius", "no fixture threat for unit")
		var by_radius: Dictionary = fixtures[object_name]
		if by_radius.has(radius):
			return SageWorldQuery.hit(float(by_radius[radius]))
		for k in by_radius.keys():
			return SageWorldQuery.hit(float(by_radius[k]))
		return SageWorldQuery.hit(0.0)


# --- Reporting --------------------------------------------------------------


func _report() -> void:
	var harness := _harness()
	var dispatch: SageScriptDispatch = harness["dispatch"]
	print(
		"HANDLERS_WP09 MEMBERS served_actions=%d served_conditions=%d blocked=%d wp21_handoff=%d of ai_used=%d"
		% [
			Wp09.SERVED_ACTIONS.size(),
			Wp09.SERVED_CONDITIONS.size(),
			Wp09.BLOCKED_ACTIONS.size(),
			Wp09.SERVED_BY_WP21_CONDITIONS.size(),
			Wp09.AI_USED_MEMBERS.size(),
		]
	)
	var coverage := dispatch.coverage()
	print(
		"HANDLERS_WP09 COVERAGE actions implemented=%d conditions implemented=%d blocked=%d"
		% [
			int((coverage["actions"] as Dictionary)["implemented"]),
			int((coverage["conditions"] as Dictionary)["implemented"]),
			(coverage["blocked_on_subsystem"] as Array).size(),
		]
	)


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("HANDLERS_WP09 PASS %s%s" % [name, " (%s)" % detail if detail != "" else ""])
	else:
		failed += 1
		printerr("HANDLERS_WP09 FAIL %s%s" % [name, " (%s)" % detail if detail != "" else ""])
