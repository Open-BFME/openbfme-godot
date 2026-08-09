extends SceneTree

## Proof runner for WP13-special-powers-combat, AI-critical subset
## (wp13_ai_powers_combat.gd).
##
## Covers, in order:
##   1. registration    - all 8 AI-used members registered and counted as
##                        coverage, ZERO gap-registered (this package's whole
##                        subset is expressible), the unsuffixed catalog name
##                        left free, no registration errors
##   2. positional args - the two traps: COMPARISON/INT side by side in both
##                        health conditions (fixtures chosen so a swapped or
##                        inverted read gives a DIFFERENT answer), and the
##                        attacker/victim PLAYER pair around the INT in
##                        PLAYER_DESTROYED_N_BUILDINGS_PLAYER (the stub
##                        teaches the fixture in ONE direction, so a swapped
##                        read refuses instead of answering)
##   3. float compare   - health thresholds compare as FLOATS: a fixture at
##                        62.5% distinguishes ParamTypes.compare from an
##                        int-truncating compare at the authored boundary
##   4. scope folding   - both command-button spellings reach the ONE folded
##                        world method with the correct Scope and a SELF target
##   5. conditions      - true, false, and the third answer: a read the world
##                        cannot answer NEVER comes back as a plain false, and
##                        NAMED_NOT_DESTROYED is proven to be the STRAIGHT
##                        units.exists read (retail's evaluateNamedUnitExists;
##                        not a was_destroyed negation, which inverts retail's
##                        false for a nonexistent name - handler comment)
##   6. decisions       - the two documented interpretive decisions are
##                        pinned: >= threshold (N-1 / N / N+1 all asserted)
##                        and empty-team-reads-false for the ALL condition
##   7. arity and types - one wrong-arity case per member; wrong-type cases
##                        for every slot with an OBSERVED wire code
##                        (COMPARISON, INT, PLAYER); slots with UNOBSERVED
##                        codes (TEAM, UNIT, COMMANDBUTTON_ABILITY_*) are
##                        deliberately not asserted by the validator, which is
##                        itself asserted, with the decoy-payload fixtures
##                        proving the field discipline instead
##
## Every fixture value is SYNTHETIC. The call SHAPES are modelled on the retail
## AI libraries (battalion ability tokens, hero liveness gates, health-percent
## gates) because those shapes are what the handlers must survive, but no
## retail payload text is reproduced here.
##
## Invocation:
##   Godot_v4.7-stable_win64_console.exe --headless --path game \
##     -s res://tests/handlers_wp13_ai_powers_combat_runner.gd

const Registry := preload("res://src/script/handlers/_registry.gd")
const Dispatch := preload("res://src/script/script_dispatch.gd")
const CoreHandlers := preload("res://src/script/script_handlers_core.gd")
const Env := preload("res://src/script/script_env.gd")
const GapLog := preload("res://src/script/script_gaps.gd")
const ParamTypes := preload("res://src/script/script_param_types.gd")

## The 8 members this package serves. Written out rather than derived from the
## module, so that a member quietly disappearing from register() fails here
## instead of agreeing with itself.
const SERVED_ACTIONS := [
	"TEAM_USE_COMMANDBUTTON_ABILITY",
	"NAMED_USE_COMMANDBUTTON_ABILITY",
]

const SERVED_CONDITIONS := [
	"NAMED_NOT_DESTROYED",
	"TEAM_DESTROYED",
	"EVAL_TEAM_HEALTH",
	"UNIT_HEALTH",
	"TEAM_IS_ATTACKED_AND_CANNOT_RETALIATE_ALL",
	"PLAYER_DESTROYED_N_BUILDINGS_PLAYER",
]

## Deliberately EMPTY, and asserted empty: unlike the WP11/WP15/WP17 AI
## subsets, every authored argument of all eight signatures has a slot on the
## existing world surface, so nothing is gap-registered. If a later edit
## demotes a served member to a gap registration, the registration test below
## disagrees with this list loudly.
const GAP_REGISTERED := []

## LIVENESS GUARD. A GDScript RUNTIME error aborts the enclosing function
## on the spot without propagating, so every later _check() in that function
## silently never runs and `failed` never moves - the runner then prints a
## zero-failure result and exits 0 with SCRIPT ERROR lines above it. This is
## the exact count a HEALTHY run makes; if the run makes any other number,
## something aborted (or an assertion was added without updating this) and
## the result is not to be trusted.
const EXPECTED_CHECKS := 59

var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	_test_registration_covers_the_ai_subset()
	_test_team_ability_reaches_the_folded_method_with_team_scope()
	_test_named_ability_reaches_the_folded_method_with_unit_scope()
	_test_command_refusal_is_reported()
	_test_named_not_destroyed_is_the_exists_read()
	_test_team_destroyed_is_the_straight_read()
	_test_team_health_reads_operator_and_threshold_positionally()
	_test_health_comparison_is_float_not_truncated()
	_test_unit_health_mirrors_the_team_condition()
	_test_an_unknown_operator_is_rejected_before_the_world_is_asked()
	_test_retaliate_all_requires_the_full_roster()
	_test_destroyed_n_buildings_is_an_at_least_threshold()
	_test_destroyed_n_buildings_direction_is_pinned_by_the_fixture()
	_test_an_unanswerable_condition_is_never_a_plain_false()
	_test_conditions_have_no_side_effects()
	_test_arity_is_enforced_per_member()
	_test_argument_type_codes_are_enforced_where_observed()
	var ran := passed + failed
	if ran != EXPECTED_CHECKS:
		failed += 1
		printerr("HANDLERS_WP13 FAIL liveness: ran %d checks, expected %d - a function aborted before its assertions" % [ran, EXPECTED_CHECKS])
	print("HANDLERS_WP13_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


# --- Fixture helpers ------------------------------------------------------


func _argument(
	argument_type: int, integer: int = 0, real: float = 0.0, text: String = ""
) -> Dictionary:
	return {"argumentType": argument_type, "integer": integer, "real": real, "text": text}


func _name_arg(value: String) -> Dictionary:
	# TEAM, UNIT and the COMMANDBUTTON_ABILITY_* types have no observed wire
	# code in this repo, so SageScriptArgs does not assert one. The integer
	# field carries a DECOY: a handler that read the wrong field would get
	# 424242 instead of the name, loudly rather than plausibly.
	return _argument(99, 424242, 424242.0, value)


func _player_arg(value: String) -> Dictionary:
	return _argument(ParamTypes.ARGUMENT_PLAYER, 424242, 424242.0, value)


func _int_arg(value: int, decoy_text: String = "DECOY_TEXT") -> Dictionary:
	return _argument(ParamTypes.ARGUMENT_INTEGER, value, 424242.0, decoy_text)


func _comparison_arg(value: int, decoy_text: String = "DECOY_OPERATOR") -> Dictionary:
	# COMPARISON is one of the few parameter types this repo HAS observed a
	# wire code for (6), so SageScriptArgs asserts it. The decoy text is what
	# a wrong-field read would return.
	return _argument(ParamTypes.ARGUMENT_COMPARISON, value, 424242.0, decoy_text)


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
		"world": CombatWorld.new(20260726),
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
		"the_wp13_ai_package_is_present",
		(outcome["packages"] as Array).has("WP13-ai-powers-combat"),
		str(outcome["packages"])
	)
	# The unsuffixed catalog name must stay FREE for the agent completing the
	# rest of WP13; this file claiming it would block that landing.
	_check(
		"the_catalog_package_name_is_left_free",
		not (outcome["packages"] as Array).has("WP13-special-powers-combat"),
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

	# 2 + 6 + 0 = 8, the exact AI-used WP13 membership the census records
	# (game/data/retail_ai_call_census.json, workPackage ==
	# "WP13-special-powers-combat").
	_check(
		"the_subset_is_eight_members",
		SERVED_ACTIONS.size() + SERVED_CONDITIONS.size() + GAP_REGISTERED.size() == 8,
		"actions=%d conditions=%d gapped=%d" % [
			SERVED_ACTIONS.size(), SERVED_CONDITIONS.size(), GAP_REGISTERED.size()
		]
	)

	# Nothing in this package is gap-registered, and every member counts as
	# real coverage - a member sliding into blocked_names() or out of the
	# implemented lists is a regression this test names.
	var wrongly_blocked: Array[String] = []
	var not_coverage: Array[String] = []
	for name: String in SERVED_ACTIONS:
		if dispatch.blocked_names().has(name):
			wrongly_blocked.append(name)
		if not dispatch.implemented_actions().has(name):
			not_coverage.append(name)
	for name: String in SERVED_CONDITIONS:
		if dispatch.blocked_names().has(name):
			wrongly_blocked.append(name)
		if not dispatch.implemented_conditions().has(name):
			not_coverage.append(name)
	_check("no_member_is_gap_registered", wrongly_blocked.is_empty(), str(wrongly_blocked))
	_check("every_member_counts_as_coverage", not_coverage.is_empty(), str(not_coverage))


# --- 4. The command-button pair -------------------------------------------


func _test_team_ability_reaches_the_folded_method_with_team_scope() -> void:
	# TEAM_USE_COMMANDBUTTON_ABILITY(TEAM, COMMANDBUTTON_ABILITY_TEAM). The
	# world folds the whole family into orders.use_command_button; the TEAM
	# spelling must arrive with Scope.TEAM, the roster name from argument 0,
	# the button token from argument 1, and a SELF target (the bare ABILITY
	# spelling names no other target). The recorded call string pins all four.
	var harness := _harness()
	var world: CombatWorld = harness["world"]
	var status := _act(harness, "TEAM_USE_COMMANDBUTTON_ABILITY", [
		_name_arg("Synthetic_Assault_Team"), _name_arg("Command_SyntheticAbility")
	])
	_check(
		"team_ability_arrives_with_team_scope_and_a_self_target",
		status == Dispatch.Status.OK
		and world.calls.has(
			"orders.use_command_button|team|Synthetic_Assault_Team"
			+ "|Command_SyntheticAbility|self"
		),
		"status=%d calls=%s" % [status, str(world.calls)]
	)


func _test_named_ability_reaches_the_folded_method_with_unit_scope() -> void:
	# NAMED_USE_COMMANDBUTTON_ABILITY(UNIT, COMMANDBUTTON_ABILITY_UNIT). Same
	# folded method, Scope.UNIT - the scope is load-bearing (team and object
	# names live in different world namespaces), so it is pinned separately
	# from the team spelling above.
	var harness := _harness()
	var world: CombatWorld = harness["world"]
	var status := _act(harness, "NAMED_USE_COMMANDBUTTON_ABILITY", [
		_name_arg("SyntheticHero"), _name_arg("Command_SyntheticHeroPower")
	])
	_check(
		"named_ability_arrives_with_unit_scope_and_a_self_target",
		status == Dispatch.Status.OK
		and world.calls.has(
			"orders.use_command_button|unit|SyntheticHero|Command_SyntheticHeroPower|self"
		),
		"status=%d calls=%s" % [status, str(world.calls)]
	)


func _test_command_refusal_is_reported() -> void:
	# A world that does not model command-button abilities must produce a
	# structured gap per action, never a quiet success. The BASE
	# SageScriptWorld is exactly that world.
	var dispatch := Dispatch.new()
	CoreHandlers.register_all(dispatch)
	Registry.register_all(dispatch)
	var env := Env.new()

	for name: String in SERVED_ACTIONS:
		var status := dispatch.execute_action(
			_record(name, [_name_arg("Synthetic_Subject"), _name_arg("Command_Synthetic")]),
			env, SageScriptWorld.new(), "fixture"
		)
		_check(
			"a_refused_%s_is_world_refused_not_ok" % name.to_lower(),
			status == Dispatch.Status.WORLD_REFUSED,
			"status=%d" % status
		)
		_check(
			"the_%s_refusal_names_the_missing_facet_method" % name.to_lower(),
			dispatch.gaps.has("action", name, GapLog.REASON_WORLD_REFUSED)
			and String(
				dispatch.gaps.entries[
					"action|%s|%s" % [name, GapLog.REASON_WORLD_REFUSED]
				]["detail"]
			).contains("orders.use_command_button"),
			str(dispatch.gaps.to_lines())
		)


# --- 5. The destruction reads ---------------------------------------------


func _test_named_not_destroyed_is_the_exists_read() -> void:
	# NAMED_NOT_DESTROYED(UNIT) is the STRAIGHT units.exists read (retail's
	# evaluateNamedUnitExists - handler comment): a standing object (the
	# world answers exists=true) reads TRUE, a destroyed or nonexistent one
	# (exists=false) reads FALSE, with NO negation anywhere - a negated
	# wiring would invert retail's false for a nonexistent name. The refusal
	# case is covered separately in the unanswerable test.
	var harness := _harness()
	var world: CombatWorld = harness["world"]
	world.set_unit_scalar("SyntheticHero", "exists", true)
	world.set_unit_scalar("Synthetic_Fallen", "exists", false)

	_check(
		"a_standing_object_reads_true",
		_cond(harness, "NAMED_NOT_DESTROYED", [_name_arg("SyntheticHero")])
	)
	_check(
		"a_destroyed_object_reads_false",
		not _cond(harness, "NAMED_NOT_DESTROYED", [_name_arg("Synthetic_Fallen")])
	)


func _test_team_destroyed_is_the_straight_read() -> void:
	# TEAM_DESTROYED(TEAM): un-negated pass-through of teams.was_destroyed.
	var harness := _harness()
	var world: CombatWorld = harness["world"]
	world.set_team_scalar("Synthetic_Wiped_Team", "was_destroyed", true)
	world.set_team_scalar("Synthetic_Assault_Team", "was_destroyed", false)

	_check(
		"a_destroyed_team_reads_true",
		_cond(harness, "TEAM_DESTROYED", [_name_arg("Synthetic_Wiped_Team")])
	)
	_check(
		"a_living_team_reads_false",
		not _cond(harness, "TEAM_DESTROYED", [_name_arg("Synthetic_Assault_Team")])
	)


# --- 2/3. The health comparisons ------------------------------------------


func _test_team_health_reads_operator_and_threshold_positionally() -> void:
	# EVAL_TEAM_HEALTH(TEAM, COMPARISON, INT). Argument 1 is the OPERATOR,
	# argument 2 the THRESHOLD, both integer-valued, so no type- or
	# field-search could recover the order.
	#
	# THE FIXTURE IS CHOSEN TO MAKE EVERY MIS-READ VISIBLE. World health =
	# 62.5, operator = COMPARE_GREATER (4), threshold = 60:
	#   * correct read:            62.5 > 60          -> true
	#   * indices 1/2 swapped:     operator becomes 60, out of table
	#                                                 -> BAD_ARGUMENTS, false
	#   * comparison inverted
	#     (threshold op health):   60 > 62.5          -> false
	# A fixture like (==, equal values) would answer true under several of
	# those mutations; this one does not.
	var harness := _harness()
	var world: CombatWorld = harness["world"]
	world.set_team_scalar("Synthetic_Assault_Team", "health_percent", 62.5)

	_check(
		"greater_than_compares_the_world_health_against_the_authored_threshold",
		_cond(harness, "EVAL_TEAM_HEALTH", [
			_name_arg("Synthetic_Assault_Team"),
			_comparison_arg(ParamTypes.COMPARE_GREATER), _int_arg(60)
		])
	)
	_check(
		"the_comparison_answers_false_when_the_health_fails_it",
		not _cond(harness, "EVAL_TEAM_HEALTH", [
			_name_arg("Synthetic_Assault_Team"),
			_comparison_arg(ParamTypes.COMPARE_LESS), _int_arg(60)
		])
	)
	# The full operator table, against the same 62.5.
	_check(
		"every_sourced_operator_is_honoured",
		_cond(harness, "EVAL_TEAM_HEALTH", [
			_name_arg("Synthetic_Assault_Team"),
			_comparison_arg(ParamTypes.COMPARE_LESS_EQUAL), _int_arg(63)
		])
		and _cond(harness, "EVAL_TEAM_HEALTH", [
			_name_arg("Synthetic_Assault_Team"),
			_comparison_arg(ParamTypes.COMPARE_GREATER_EQUAL), _int_arg(62)
		])
		and _cond(harness, "EVAL_TEAM_HEALTH", [
			_name_arg("Synthetic_Assault_Team"),
			_comparison_arg(ParamTypes.COMPARE_NOT_EQUAL), _int_arg(62)
		])
		and not _cond(harness, "EVAL_TEAM_HEALTH", [
			_name_arg("Synthetic_Assault_Team"),
			_comparison_arg(ParamTypes.COMPARE_EQUAL), _int_arg(62)
		])
	)


func _test_health_comparison_is_float_not_truncated() -> void:
	# The world answers 62.5; the authored threshold is the INT 62. The float
	# compare answers 62.5 > 62 -> TRUE. An implementation that truncated the
	# health to int first would compute 62 > 62 -> false, flipping the gate
	# exactly at the authored boundary - which is where a retail health gate
	# does its work.
	var harness := _harness()
	var world: CombatWorld = harness["world"]
	world.set_team_scalar("Synthetic_Assault_Team", "health_percent", 62.5)
	world.set_unit_scalar("SyntheticHero", "health_percent", 62.5)

	_check(
		"team_health_compares_the_fraction_not_a_truncation",
		_cond(harness, "EVAL_TEAM_HEALTH", [
			_name_arg("Synthetic_Assault_Team"),
			_comparison_arg(ParamTypes.COMPARE_GREATER), _int_arg(62)
		])
		and _cond(harness, "EVAL_TEAM_HEALTH", [
			_name_arg("Synthetic_Assault_Team"),
			_comparison_arg(ParamTypes.COMPARE_LESS), _int_arg(63)
		])
	)
	_check(
		"unit_health_compares_the_fraction_not_a_truncation",
		_cond(harness, "UNIT_HEALTH", [
			_name_arg("SyntheticHero"),
			_comparison_arg(ParamTypes.COMPARE_GREATER), _int_arg(62)
		])
	)


func _test_unit_health_mirrors_the_team_condition() -> void:
	# UNIT_HEALTH(UNIT, COMPARISON, INT): same shape as EVAL_TEAM_HEALTH,
	# keyed by object name on the units facet.
	var harness := _harness()
	var world: CombatWorld = harness["world"]
	world.set_unit_scalar("SyntheticHero", "health_percent", 25.0)

	_check(
		"a_wounded_unit_passes_a_below_threshold_gate",
		_cond(harness, "UNIT_HEALTH", [
			_name_arg("SyntheticHero"),
			_comparison_arg(ParamTypes.COMPARE_LESS), _int_arg(50)
		])
	)
	_check(
		"a_wounded_unit_fails_an_above_threshold_gate",
		not _cond(harness, "UNIT_HEALTH", [
			_name_arg("SyntheticHero"),
			_comparison_arg(ParamTypes.COMPARE_GREATER_EQUAL), _int_arg(50)
		])
	)


func _test_an_unknown_operator_is_rejected_before_the_world_is_asked() -> void:
	# ParamTypes.compare answers `false` for an operator it does not know.
	# Letting that through would turn "this handler does not understand the
	# operator" into a confident "no" at an AI health gate, so an out-of-table
	# operator must be BAD_ARGUMENTS - and it must be decided BEFORE the world
	# read: the team below has NO health fixture, so a handler that asked the
	# world first would report WORLD_REFUSED instead.
	var harness := _harness()
	var dispatch: SageScriptDispatch = harness["dispatch"]

	_check(
		"an_out_of_table_operator_reads_false_at_the_call_site",
		not _cond(harness, "EVAL_TEAM_HEALTH", [
			_name_arg("Synthetic_Unfixtured_Team"), _comparison_arg(6), _int_arg(50)
		])
	)
	_check(
		"the_team_condition_records_bad_arguments_not_world_refused",
		dispatch.gaps.has("condition", "EVAL_TEAM_HEALTH", GapLog.REASON_BAD_ARGUMENTS)
		and not dispatch.gaps.has(
			"condition", "EVAL_TEAM_HEALTH", GapLog.REASON_WORLD_REFUSED
		),
		str(dispatch.gaps.to_lines())
	)
	_check(
		"the_unit_condition_applies_the_same_rejection",
		not _cond(harness, "UNIT_HEALTH", [
			_name_arg("Synthetic_Unfixtured_Unit"), _comparison_arg(-1), _int_arg(50)
		])
		and dispatch.gaps.has("condition", "UNIT_HEALTH", GapLog.REASON_BAD_ARGUMENTS),
		str(dispatch.gaps.to_lines())
	)
	_check(
		"no_unfinished_question_reached_the_world",
		(harness["world"] as CombatWorld).refusal_log.is_empty(),
		str((harness["world"] as CombatWorld).refusal_log)
	)


# --- 6. The two interpretive decisions ------------------------------------


func _test_retaliate_all_requires_the_full_roster() -> void:
	# TEAM_IS_ATTACKED_AND_CANNOT_RETALIATE_ALL(TEAM): true only when EVERY
	# living member is in the attacked-and-cannot-retaliate state. Partial
	# (the _ANY reading, a mutation this fixture exists to catch) and empty
	# (the vacuous-truth reading) must both be false.
	var harness := _harness()
	var world: CombatWorld = harness["world"]

	world.set_team_scalar("Synthetic_Cornered", "unit_count", 3)
	world.set_team_scalar("Synthetic_Cornered", "cannot_retaliate_count", 3)
	world.set_team_scalar("Synthetic_Fighting", "unit_count", 3)
	world.set_team_scalar("Synthetic_Fighting", "cannot_retaliate_count", 2)
	world.set_team_scalar("Synthetic_Emptied", "unit_count", 0)
	world.set_team_scalar("Synthetic_Emptied", "cannot_retaliate_count", 0)

	_check(
		"a_fully_cornered_team_reads_true",
		_cond(harness, "TEAM_IS_ATTACKED_AND_CANNOT_RETALIATE_ALL", [
			_name_arg("Synthetic_Cornered")
		])
	)
	_check(
		"a_partially_cornered_team_reads_false",
		not _cond(harness, "TEAM_IS_ATTACKED_AND_CANNOT_RETALIATE_ALL", [
			_name_arg("Synthetic_Fighting")
		])
	)
	# Decision 2 (see the module): an empty team is not being attacked, so the
	# vacuous "all zero of them" must NOT read true.
	_check(
		"an_empty_team_reads_false_not_vacuously_true",
		not _cond(harness, "TEAM_IS_ATTACKED_AND_CANNOT_RETALIATE_ALL", [
			_name_arg("Synthetic_Emptied")
		])
	)


func _test_destroyed_n_buildings_is_an_at_least_threshold() -> void:
	# PLAYER_DESTROYED_N_BUILDINGS_PLAYER(PLAYER, INT, PLAYER), decision 1
	# (see the module): an AT-LEAST threshold over a monotone ledger count.
	# N-1, N and N+1 are all asserted, because the N+1 case is what separates
	# >= from == : an equality reading would answer false once the ledger
	# passes the authored threshold, silencing the gate forever.
	var harness := _harness()
	var world: CombatWorld = harness["world"]
	world.set_buildings_destroyed("Player_1", "Player_2", 4)

	_check(
		"a_count_above_the_threshold_reads_true",
		_cond(harness, "PLAYER_DESTROYED_N_BUILDINGS_PLAYER", [
			_player_arg("Player_1"), _int_arg(3), _player_arg("Player_2")
		])
	)
	_check(
		"a_count_meeting_the_threshold_exactly_reads_true",
		_cond(harness, "PLAYER_DESTROYED_N_BUILDINGS_PLAYER", [
			_player_arg("Player_1"), _int_arg(4), _player_arg("Player_2")
		])
	)
	_check(
		"a_count_below_the_threshold_reads_false",
		not _cond(harness, "PLAYER_DESTROYED_N_BUILDINGS_PLAYER", [
			_player_arg("Player_1"), _int_arg(5), _player_arg("Player_2")
		])
	)


func _test_destroyed_n_buildings_direction_is_pinned_by_the_fixture() -> void:
	# The attacker/victim pair is positionally load-bearing: the stub keys its
	# ledger on (attacker, victim) and this test teaches ONE direction only.
	# The reversed question - has Player_2 destroyed Player_1's buildings -
	# must REFUSE (no fixture), not answer from the taught direction: a
	# handler that swapped indices 0 and 2 would hit exactly this refusal on
	# the taught case, making the swap loud instead of plausible.
	var harness := _harness()
	var world: CombatWorld = harness["world"]
	var dispatch: SageScriptDispatch = harness["dispatch"]
	world.set_buildings_destroyed("Player_1", "Player_2", 4)
	world.expect_refusals()

	_check(
		"the_reverse_direction_refuses_rather_than_reusing_the_fixture",
		not _cond(harness, "PLAYER_DESTROYED_N_BUILDINGS_PLAYER", [
			_player_arg("Player_2"), _int_arg(1), _player_arg("Player_1")
		])
		and dispatch.gaps.has(
			"condition", "PLAYER_DESTROYED_N_BUILDINGS_PLAYER", GapLog.REASON_WORLD_REFUSED
		),
		str(dispatch.gaps.to_lines())
	)


# --- 5. The third answer --------------------------------------------------


func _test_an_unanswerable_condition_is_never_a_plain_false() -> void:
	# A world that cannot answer must produce a REFUSAL - false at the call
	# site, but with a gap on the record. Two of these fixtures are traps for
	# invented defaults:
	#   * NAMED_NOT_DESTROYED: a world that DEFAULTED exists to true would
	#     read TRUE here ("still standing") - ignorance turned into
	#     confidence, the worst outcome in this file.
	#   * the >= 0 threshold reads: a count or health defaulting to zero
	#     would satisfy them - refusing proves the zero was never invented.
	var harness := _harness()
	var world: CombatWorld = harness["world"]
	var dispatch: SageScriptDispatch = harness["dispatch"]
	world.expect_refusals()
	# Nothing below has fixtures, so every read refuses.

	_check(
		"an_unanswerable_not_destroyed_reads_false_never_still_standing",
		not _cond(harness, "NAMED_NOT_DESTROYED", [_name_arg("Synthetic_Unknown")])
		and dispatch.gaps.has(
			"condition", "NAMED_NOT_DESTROYED", GapLog.REASON_WORLD_REFUSED
		),
		str(dispatch.gaps.to_lines())
	)
	_check(
		"an_unanswerable_team_destroyed_refuses",
		not _cond(harness, "TEAM_DESTROYED", [_name_arg("Synthetic_Unknown_Team")])
		and dispatch.gaps.has("condition", "TEAM_DESTROYED", GapLog.REASON_WORLD_REFUSED),
		str(dispatch.gaps.to_lines())
	)
	_check(
		"an_unanswerable_team_health_refuses_instead_of_reading_as_zero",
		not _cond(harness, "EVAL_TEAM_HEALTH", [
			_name_arg("Synthetic_Unknown_Team"),
			_comparison_arg(ParamTypes.COMPARE_GREATER_EQUAL), _int_arg(0)
		])
		and dispatch.gaps.has("condition", "EVAL_TEAM_HEALTH", GapLog.REASON_WORLD_REFUSED),
		str(dispatch.gaps.to_lines())
	)
	_check(
		"an_unanswerable_unit_health_refuses_instead_of_reading_as_zero",
		not _cond(harness, "UNIT_HEALTH", [
			_name_arg("Synthetic_Unknown"),
			_comparison_arg(ParamTypes.COMPARE_GREATER_EQUAL), _int_arg(0)
		])
		and dispatch.gaps.has("condition", "UNIT_HEALTH", GapLog.REASON_WORLD_REFUSED),
		str(dispatch.gaps.to_lines())
	)
	_check(
		"an_unanswerable_retaliate_all_refuses",
		not _cond(harness, "TEAM_IS_ATTACKED_AND_CANNOT_RETALIATE_ALL", [
			_name_arg("Synthetic_Unknown_Team")
		])
		and dispatch.gaps.has(
			"condition",
			"TEAM_IS_ATTACKED_AND_CANNOT_RETALIATE_ALL",
			GapLog.REASON_WORLD_REFUSED
		),
		str(dispatch.gaps.to_lines())
	)
	_check(
		"an_unanswerable_buildings_count_refuses_instead_of_reading_as_zero",
		not _cond(harness, "PLAYER_DESTROYED_N_BUILDINGS_PLAYER", [
			_player_arg("Player_9"), _int_arg(0), _player_arg("Player_8")
		])
		and dispatch.gaps.has(
			"condition", "PLAYER_DESTROYED_N_BUILDINGS_PLAYER", GapLog.REASON_WORLD_REFUSED
		),
		str(dispatch.gaps.to_lines())
	)
	_check(
		"the_refusals_name_the_missing_world_methods",
		world.refused("units.exists")
		and world.refused("teams.was_destroyed")
		and world.refused("combat.team_health_percent")
		and world.refused("units.health_percent")
		and world.refused("teams.attacked_and_cannot_retaliate_count")
		and world.refused("combat.buildings_destroyed_by"),
		str(world.refusal_log)
	)


func _test_conditions_have_no_side_effects() -> void:
	# Conditions are evaluated an unpredictable number of times, so any
	# mutation is a determinism bug. None of the six may touch the env or
	# command the world - including the two-read ALL condition, whose queries
	# must stay reads.
	var harness := _harness()
	var world: CombatWorld = harness["world"]
	var env: SageScriptEnv = harness["env"]
	world.set_unit_scalar("SyntheticHero", "exists", true)
	world.set_unit_scalar("SyntheticHero", "health_percent", 62.5)
	world.set_team_scalar("Synthetic_Assault_Team", "was_destroyed", false)
	world.set_team_scalar("Synthetic_Assault_Team", "health_percent", 62.5)
	world.set_team_scalar("Synthetic_Assault_Team", "unit_count", 3)
	world.set_team_scalar("Synthetic_Assault_Team", "cannot_retaliate_count", 3)
	world.set_buildings_destroyed("Player_1", "Player_2", 4)

	for _repeat in range(3):
		_cond(harness, "NAMED_NOT_DESTROYED", [_name_arg("SyntheticHero")])
		_cond(harness, "TEAM_DESTROYED", [_name_arg("Synthetic_Assault_Team")])
		_cond(harness, "EVAL_TEAM_HEALTH", [
			_name_arg("Synthetic_Assault_Team"),
			_comparison_arg(ParamTypes.COMPARE_GREATER), _int_arg(60)
		])
		_cond(harness, "UNIT_HEALTH", [
			_name_arg("SyntheticHero"),
			_comparison_arg(ParamTypes.COMPARE_GREATER), _int_arg(60)
		])
		_cond(harness, "TEAM_IS_ATTACKED_AND_CANNOT_RETALIATE_ALL", [
			_name_arg("Synthetic_Assault_Team")
		])
		_cond(harness, "PLAYER_DESTROYED_N_BUILDINGS_PLAYER", [
			_player_arg("Player_1"), _int_arg(3), _player_arg("Player_2")
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


# --- 7. Arity and argument coding, per member ------------------------------


func _test_arity_is_enforced_per_member() -> void:
	# Arity is checked against the sourced signature before the handler runs,
	# so a positional read can never quietly slide onto a neighbouring
	# argument. One wrong-arity case per member.
	var harness := _harness()
	var world: CombatWorld = harness["world"]
	var dispatch: SageScriptDispatch = harness["dispatch"]
	world.set_unit_scalar("SyntheticHero", "exists", true)
	world.set_team_scalar("Synthetic_Assault_Team", "was_destroyed", false)

	_check(
		"a_one_argument_team_ability_is_rejected",
		_act(harness, "TEAM_USE_COMMANDBUTTON_ABILITY", [
			_name_arg("Synthetic_Assault_Team")
		]) == Dispatch.Status.BAD_ARGUMENTS
	)
	_check(
		"a_three_argument_named_ability_is_rejected",
		_act(harness, "NAMED_USE_COMMANDBUTTON_ABILITY", [
			_name_arg("SyntheticHero"), _name_arg("Command_Synthetic"),
			_name_arg("Synthetic_Extra")
		]) == Dispatch.Status.BAD_ARGUMENTS
	)
	_check(
		"a_zero_argument_not_destroyed_is_rejected",
		not _cond(harness, "NAMED_NOT_DESTROYED", [])
		and dispatch.gaps.has(
			"condition", "NAMED_NOT_DESTROYED", GapLog.REASON_BAD_ARGUMENTS
		),
		str(dispatch.gaps.to_lines())
	)
	_check(
		"a_two_argument_team_destroyed_is_rejected",
		not _cond(harness, "TEAM_DESTROYED", [
			_name_arg("Synthetic_Assault_Team"), _name_arg("Synthetic_Extra")
		])
		and dispatch.gaps.has("condition", "TEAM_DESTROYED", GapLog.REASON_BAD_ARGUMENTS),
		str(dispatch.gaps.to_lines())
	)
	_check(
		"a_two_argument_team_health_is_rejected",
		not _cond(harness, "EVAL_TEAM_HEALTH", [
			_name_arg("Synthetic_Assault_Team"),
			_comparison_arg(ParamTypes.COMPARE_GREATER)
		])
		and dispatch.gaps.has("condition", "EVAL_TEAM_HEALTH", GapLog.REASON_BAD_ARGUMENTS),
		str(dispatch.gaps.to_lines())
	)
	_check(
		"a_four_argument_unit_health_is_rejected",
		not _cond(harness, "UNIT_HEALTH", [
			_name_arg("SyntheticHero"), _comparison_arg(ParamTypes.COMPARE_GREATER),
			_int_arg(60), _int_arg(60)
		])
		and dispatch.gaps.has("condition", "UNIT_HEALTH", GapLog.REASON_BAD_ARGUMENTS),
		str(dispatch.gaps.to_lines())
	)
	_check(
		"a_two_argument_retaliate_all_is_rejected",
		not _cond(harness, "TEAM_IS_ATTACKED_AND_CANNOT_RETALIATE_ALL", [
			_name_arg("Synthetic_Assault_Team"), _name_arg("Synthetic_Extra")
		])
		and dispatch.gaps.has(
			"condition",
			"TEAM_IS_ATTACKED_AND_CANNOT_RETALIATE_ALL",
			GapLog.REASON_BAD_ARGUMENTS
		),
		str(dispatch.gaps.to_lines())
	)
	_check(
		"a_two_argument_buildings_condition_is_rejected",
		not _cond(harness, "PLAYER_DESTROYED_N_BUILDINGS_PLAYER", [
			_player_arg("Player_1"), _int_arg(3)
		])
		and dispatch.gaps.has(
			"condition", "PLAYER_DESTROYED_N_BUILDINGS_PLAYER", GapLog.REASON_BAD_ARGUMENTS
		),
		str(dispatch.gaps.to_lines())
	)
	_check(
		"no_rejected_call_reached_the_world",
		world.calls.is_empty() and world.refusal_log.is_empty(),
		"calls=%s refusals=%s" % [str(world.calls), str(world.refusal_log)]
	)


func _test_argument_type_codes_are_enforced_where_observed() -> void:
	# COMPARISON (6), INT (0) and PLAYER (11) have OBSERVED wire codes, so
	# SageScriptArgs asserts them: a record carrying the wrong code in one of
	# those slots is BAD_ARGUMENTS before the handler runs. TEAM, UNIT and the
	# COMMANDBUTTON_ABILITY_* types have NO observed code, so the validator
	# DECLINES to assert there by design (an invented expectation would reject
	# valid retail data) - that policy is asserted too, and for those slots
	# the decoy payloads in every fixture above stand in: a wrong-FIELD read
	# returns 424242 instead of the name and fails the value assertions.
	var harness := _harness()
	var world: CombatWorld = harness["world"]
	var dispatch: SageScriptDispatch = harness["dispatch"]
	world.set_team_scalar("Synthetic_Assault_Team", "health_percent", 62.5)
	world.set_unit_scalar("SyntheticHero", "health_percent", 62.5)
	world.set_buildings_destroyed("Player_1", "Player_2", 4)

	_check(
		"a_miscoded_team_health_comparison_slot_is_rejected",
		not _cond(harness, "EVAL_TEAM_HEALTH", [
			_name_arg("Synthetic_Assault_Team"), _name_arg("DECOY"), _int_arg(60)
		])
		and dispatch.gaps.has("condition", "EVAL_TEAM_HEALTH", GapLog.REASON_BAD_ARGUMENTS),
		str(dispatch.gaps.to_lines())
	)
	_check(
		"a_miscoded_unit_health_threshold_slot_is_rejected",
		not _cond(harness, "UNIT_HEALTH", [
			_name_arg("SyntheticHero"), _comparison_arg(ParamTypes.COMPARE_GREATER),
			_name_arg("60")
		])
		and dispatch.gaps.has("condition", "UNIT_HEALTH", GapLog.REASON_BAD_ARGUMENTS),
		str(dispatch.gaps.to_lines())
	)
	_check(
		"a_miscoded_buildings_player_slot_is_rejected",
		not _cond(harness, "PLAYER_DESTROYED_N_BUILDINGS_PLAYER", [
			_name_arg("Player_1"), _int_arg(3), _player_arg("Player_2")
		])
		and dispatch.gaps.has(
			"condition", "PLAYER_DESTROYED_N_BUILDINGS_PLAYER", GapLog.REASON_BAD_ARGUMENTS
		),
		str(dispatch.gaps.to_lines())
	)
	_check(
		"a_miscoded_buildings_threshold_slot_is_rejected",
		not _cond(harness, "PLAYER_DESTROYED_N_BUILDINGS_PLAYER", [
			_player_arg("Player_1"), _player_arg("3"), _player_arg("Player_2")
		])
	)
	# The validator's documented restraint: an unobserved-code slot (TEAM
	# here) is NOT rejected on its code - the record below still answers,
	# because rejecting it would need an invented expectation. The name is
	# read from the TEXT field regardless of the bogus code, which the fixture
	# proves by answering.
	_check(
		"an_unobserved_code_slot_is_not_asserted_and_the_text_field_is_read",
		_cond(harness, "EVAL_TEAM_HEALTH", [
			_argument(55, 424242, 424242.0, "Synthetic_Assault_Team"),
			_comparison_arg(ParamTypes.COMPARE_GREATER), _int_arg(60)
		])
	)
	_check(
		"no_miscoded_call_reached_the_world",
		world.calls.is_empty(),
		str(world.calls)
	)


# --- Stub world -----------------------------------------------------------
#
# SageScriptWorldStub teaches only the Players facet; this package reads
# Teams, Units and Combat and commands Orders, none of which the shared stub
# covers. They are taught HERE rather than in the shared stub, because the
# shared stub is an existing file that concurrent packages also depend on.
# Every command is recorded as a flat string so a test asserts on the exact
# arguments that reached the world, including the scope label and target kind;
# every read answers from a fixture or REFUSES, so a test that forgot to set
# one gets a loud refusal rather than a plausible zero.


class CombatWorld:
	extends SageScriptWorldStub

	var calls: Array[String] = []

	## "team|key" and "unit|key" -> fixture value; "attacker|victim" -> int.
	var team_scalars: Dictionary = {}
	var unit_scalars: Dictionary = {}
	var destroyed_ledger: Dictionary = {}

	func set_team_scalar(team: String, key: String, value: Variant) -> void:
		team_scalars["%s|%s" % [team, key]] = value

	func set_unit_scalar(unit: String, key: String, value: Variant) -> void:
		unit_scalars["%s|%s" % [unit, key]] = value

	func set_buildings_destroyed(attacker: String, victim: String, count: int) -> void:
		destroyed_ledger["%s|%s" % [attacker, victim]] = count

	func _make_teams() -> SageScriptWorld.Teams:
		return AiStubTeams.new()

	func _make_units() -> SageScriptWorld.Units:
		return AiStubUnits.new()

	func _make_combat() -> SageScriptWorld.Combat:
		return AiStubCombat.new()

	func _make_orders() -> SageScriptWorld.Orders:
		return AiStubOrders.new()


class AiStubTeams:
	extends SageScriptWorld.Teams

	func _scalar(team: String, key: String) -> SageWorldQuery:
		var stub := world as CombatWorld
		var fixture_key := "%s|%s" % [team, key]
		if not stub.team_scalars.has(fixture_key):
			return _refuse_query("teams.%s" % key, "no fixture value for team '%s'" % team)
		return SageWorldQuery.hit(stub.team_scalars[fixture_key])

	func was_destroyed(team: String) -> SageWorldQuery:
		return _scalar(team, "was_destroyed")

	func unit_count(team: String) -> SageWorldQuery:
		return _scalar(team, "unit_count")

	func attacked_and_cannot_retaliate_count(team: String) -> SageWorldQuery:
		var stub := world as CombatWorld
		var fixture_key := "%s|cannot_retaliate_count" % team
		if not stub.team_scalars.has(fixture_key):
			return _refuse_query(
				"teams.attacked_and_cannot_retaliate_count",
				"no fixture value for team '%s'" % team
			)
		return SageWorldQuery.hit(stub.team_scalars[fixture_key])


class AiStubUnits:
	extends SageScriptWorld.Units

	func _scalar(unit: String, key: String) -> SageWorldQuery:
		var stub := world as CombatWorld
		var fixture_key := "%s|%s" % [unit, key]
		if not stub.unit_scalars.has(fixture_key):
			return _refuse_query("units.%s" % key, "no fixture value for object '%s'" % unit)
		return SageWorldQuery.hit(stub.unit_scalars[fixture_key])

	func exists(object_name: String) -> SageWorldQuery:
		return _scalar(object_name, "exists")

	func health_percent(object_name: String) -> SageWorldQuery:
		return _scalar(object_name, "health_percent")


class AiStubCombat:
	extends SageScriptWorld.Combat

	func team_health_percent(team: String) -> SageWorldQuery:
		var stub := world as CombatWorld
		var fixture_key := "%s|health_percent" % team
		if not stub.team_scalars.has(fixture_key):
			return _refuse_query(
				"combat.team_health_percent", "no fixture value for team '%s'" % team
			)
		return SageWorldQuery.hit(stub.team_scalars[fixture_key])

	func buildings_destroyed_by(player: String, victim: String) -> SageWorldQuery:
		var stub := world as CombatWorld
		var fixture_key := "%s|%s" % [player, victim]
		if not stub.destroyed_ledger.has(fixture_key):
			return _refuse_query(
				"combat.buildings_destroyed_by",
				"no fixture ledger for attacker '%s' victim '%s'" % [player, victim]
			)
		return SageWorldQuery.hit(int(stub.destroyed_ledger[fixture_key]))


class AiStubOrders:
	extends SageScriptWorld.Orders

	func use_command_button(
		scope: int, name: String, command_button: String, target: Dictionary
	) -> bool:
		var kind := int(target.get("kind", -1))
		var kind_label := (
			"self" if kind == SageScriptWorld.TargetKind.SELF else "kind-%d" % kind
		)
		(world as CombatWorld).calls.append(
			"orders.use_command_button|%s|%s|%s|%s"
			% [SageScriptWorld.scope_label(scope), name, command_button, kind_label]
		)
		return true


# --- Reporting ------------------------------------------------------------


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("HANDLERS_WP13 PASS %s%s" % [name, " (%s)" % detail if detail != "" else ""])
	else:
		failed += 1
		printerr("HANDLERS_WP13 FAIL %s%s" % [name, " (%s)" % detail if detail != "" else ""])
