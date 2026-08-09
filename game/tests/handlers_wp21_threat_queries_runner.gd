extends SceneTree

## Proof runner for WP21-threat-queries (wp21_threat_queries.gd) - the two
## radius-scoped threat CONDITIONS the retail AI polls (25 call sites across 3
## libraries), formerly gap-registered by wp09_ai_core.gd and unblocked by the
## owner ruling of 2026-08-08 (slice combat-weight formula accepted as
## authoritative for script conditions for the playtest era).
##
## Covers, in order:
##   1. registration    - both conditions registered by the package, counted as
##                        coverage, no registry errors, NOT dispatch-blocked
##                        (the wp09 handoff actually happened), and no action
##                        form of either name is claimed
##   2. per condition   - TRUE against stub-seeded threat, FALSE against
##                        stub-seeded threat, and the third answer: a world
##                        refusal is a structured WORLD_REFUSED gap, never a
##                        plain false
##   3. positional args - the signature is (subject, COMPARISON, REAL value,
##                        REAL radius): two adjacent REAL slots whose swap
##                        cannot be told apart by type. Fixtures are keyed on
##                        the exact (subject, radius) pair with value != radius,
##                        and world reads are recorded verbatim, so a swapped
##                        read misses the fixture (loud refusal) instead of
##                        passing plausibly
##   4. operator table  - an out-of-table comparison operator is BAD_ARGUMENTS,
##                        never a quiet false, and never reaches the world
##   5. purity          - conditions write ctx.result only: repeated evaluation
##                        leaves the env untouched
##   6. arity           - one wrong-arity case per condition, rejected before
##                        the handler runs
##
## Every fixture value is SYNTHETIC. No retail payload text is reproduced.
##
## Invocation:
##   <godot> --headless --path game \
##     --script res://tests/handlers_wp21_threat_queries_runner.gd

const Registry := preload("res://src/script/handlers/_registry.gd")
const Dispatch := preload("res://src/script/script_dispatch.gd")
const CoreHandlers := preload("res://src/script/script_handlers_core.gd")
const Env := preload("res://src/script/script_env.gd")
const GapLog := preload("res://src/script/script_gaps.gd")
const ParamTypes := preload("res://src/script/script_param_types.gd")
const Watchdog := preload("res://tests/runner_watchdog.gd")

## The 2 conditions this package serves. Written out rather than derived from
## the module, so a member quietly disappearing from register() fails here.
const SERVED_CONDITIONS := [
	"TEAM_THREAT_LEVEL",
	"UNIT_THREAT_LEVEL",
]

## Retail AI call sites author team threat against the acting team's token.
const THIS_TEAM := "<This Team>"

## LIVENESS GUARD. A GDScript RUNTIME error aborts the enclosing function on
## the spot without propagating, so every later _check() in that function
## silently never runs. A healthy run makes exactly this many checks; any
## other number means something aborted (or an assertion was added without
## updating this) and the result is not to be trusted.
const EXPECTED_CHECKS := 25

var passed := 0
var failed := 0
var _watchdog := Watchdog.new()

## Every world this run created, so facet<->world reference cycles can be
## broken before quit (see the Facet class comment in script_world.gd: a
## dropped world with live facets is a RefCounted cycle that leaks and can AV
## on Windows teardown).
var _worlds: Array = []


func _initialize() -> void:
	_watchdog.start(self, "HANDLERS_WP21", 0, 0, true)
	_watchdog.set_result_provider(func() -> Vector2i: return Vector2i(passed, failed))
	call_deferred("_run")


func _run() -> void:
	_test_registration_covers_the_package()
	_test_unit_threat_level_compares_within_radius()
	_test_team_threat_level_compares_within_radius()
	_test_unit_threat_refusal_is_not_false()
	_test_team_threat_refusal_is_not_false()
	_test_out_of_table_operator_is_bad_arguments()
	_test_conditions_have_no_side_effects()
	_test_arity_is_enforced_per_condition()
	var ran := passed + failed
	if ran != EXPECTED_CHECKS:
		failed += 1
		printerr(
			"HANDLERS_WP21 FAIL liveness: ran %d checks, expected %d - a function aborted before its assertions"
			% [ran, EXPECTED_CHECKS]
		)
	print("HANDLERS_WP21_RESULT passed=%d failed=%d" % [passed, failed])
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
	# UNIT and TEAM have no observed wire code in this repo, so SageScriptArgs
	# does not assert one. The integer field carries a DECOY: a handler that
	# read the wrong field would get 424242 instead of the name, loudly rather
	# than plausibly.
	return _argument(99, 424242, 0.0, value)


func _comparison_arg(value: int) -> Dictionary:
	return _argument(ParamTypes.ARGUMENT_COMPARISON, value, 0.0, "DECOY_OPERATOR")


func _real_arg(value: float) -> Dictionary:
	# REAL is carried in the `real` payload field. The integer decoy catches a
	# wrong-field read: a 424242 threat threshold or radius would flip every
	# fixture below.
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
	var world := ThreatWorld.new(20260808)
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
		"the_wp21_package_is_present",
		(outcome["packages"] as Array).has("WP21-threat-queries"),
		str(outcome["packages"])
	)

	var missing: Array[String] = []
	for name: String in SERVED_CONDITIONS:
		if not dispatch.condition_handlers.has(name):
			missing.append(name)
	_check("both_threat_conditions_are_registered", missing.is_empty(), str(missing))

	var uncounted: Array[String] = []
	var implemented := dispatch.implemented_conditions()
	for name: String in SERVED_CONDITIONS:
		if not implemented.has(name):
			uncounted.append(name)
	_check(
		"both_threat_conditions_count_as_coverage", uncounted.is_empty(), str(uncounted)
	)

	# The whole point of this packet: wp09_ai_core.gd used to gap-register the
	# pair as blocked-subsystem, and first-registration-wins means a leftover
	# blocked declaration would shadow this package silently.
	_check(
		"the_threat_pair_is_no_longer_dispatch_blocked",
		not dispatch.blocked_names().has("UNIT_THREAT_LEVEL")
		and not dispatch.blocked_names().has("TEAM_THREAT_LEVEL"),
		str(dispatch.blocked_names())
	)
	# Both names exist as ACTIONS in the BFME2ROTWK-sourced table too; the
	# evidence (all 25 retail AI call sites are condition sites) says this
	# package must not claim the action form.
	_check(
		"the_threat_pair_claims_no_action_form",
		not dispatch.action_handlers.has("UNIT_THREAT_LEVEL")
		and not dispatch.action_handlers.has("TEAM_THREAT_LEVEL")
	)


# --- 2/3. UNIT_THREAT_LEVEL --------------------------------------------------


func _test_unit_threat_level_compares_within_radius() -> void:
	# UNIT_THREAT_LEVEL(UNIT, COMPARISON, REAL, REAL) - table row 299:
	# "UNIT_THREAT_LEVEL|UNIT,COMPARISON,REAL,REAL|-|CNC3KW|S".
	# arg 2 is the threat VALUE, arg 3 is the RADIUS - two adjacent REAL slots.
	# The fixture is keyed on (subject, radius) with value != radius, so a
	# handler that swapped them misses the fixture and REFUSES loudly instead
	# of passing plausibly. World threat = 5.0 within 350.0 of the gate.
	var harness := _harness()
	var world: ThreatWorld = harness["world"]
	world.set_unit_threat("AI_TEST_GATE", 350.0, 5.0)

	_check(
		"unit_threat_below_the_authored_value_reads_true",
		_cond(harness, "UNIT_THREAT_LEVEL", [
			_name_arg("AI_TEST_GATE"), _comparison_arg(ParamTypes.COMPARE_LESS),
			_real_arg(10.0), _real_arg(350.0)
		]),
		str((harness["dispatch"] as SageScriptDispatch).gaps.to_lines())
	)
	_check(
		"the_unit_threat_comparison_answers_false_when_it_fails",
		not _cond(harness, "UNIT_THREAT_LEVEL", [
			_name_arg("AI_TEST_GATE"), _comparison_arg(ParamTypes.COMPARE_GREATER),
			_real_arg(10.0), _real_arg(350.0)
		])
	)
	_check(
		"the_subject_and_radius_reach_the_units_facet_in_signature_order",
		world.reads.has("units.threat_within_radius|AI_TEST_GATE|350.0"),
		str(world.reads)
	)
	_check(
		"a_genuine_unit_threat_answer_records_no_gap",
		(harness["dispatch"] as SageScriptDispatch).gaps.entries.is_empty(),
		str((harness["dispatch"] as SageScriptDispatch).gaps.to_lines())
	)


# --- 2/3. TEAM_THREAT_LEVEL --------------------------------------------------


func _test_team_threat_level_compares_within_radius() -> void:
	# TEAM_THREAT_LEVEL(TEAM, COMPARISON, REAL, REAL) - table row 268:
	# "TEAM_THREAT_LEVEL|TEAM,COMPARISON,REAL,REAL|-|CNC3KW|S".
	# Same adjacent-REAL trap as the unit form; retail AI shape is
	# <This Team> | >= | threat | radius. World threat = 2.0 within 300.0.
	var harness := _harness()
	var world: ThreatWorld = harness["world"]
	world.set_team_threat(THIS_TEAM, 300.0, 2.0)

	_check(
		"team_threat_meeting_the_authored_value_reads_true",
		_cond(harness, "TEAM_THREAT_LEVEL", [
			_name_arg(THIS_TEAM), _comparison_arg(ParamTypes.COMPARE_GREATER_EQUAL),
			_real_arg(1.0), _real_arg(300.0)
		]),
		str((harness["dispatch"] as SageScriptDispatch).gaps.to_lines())
	)
	_check(
		"the_team_threat_comparison_answers_false_when_it_fails",
		not _cond(harness, "TEAM_THREAT_LEVEL", [
			_name_arg(THIS_TEAM), _comparison_arg(ParamTypes.COMPARE_EQUAL),
			_real_arg(1.0), _real_arg(300.0)
		])
	)
	_check(
		"the_team_and_radius_reach_the_teams_facet_in_signature_order",
		world.reads.has("teams.threat_within_radius|%s|300.0" % THIS_TEAM),
		str(world.reads)
	)


# --- 2. Refusals are not false -----------------------------------------------


func _test_unit_threat_refusal_is_not_false() -> void:
	# No fixture is taught, so the read REFUSES. "there is no threat near the
	# gate" and "the world cannot measure threat" are different answers - only
	# the first may be a plain false. A refusal must surface as a structured
	# WORLD_REFUSED gap.
	var harness := _harness()
	var world: ThreatWorld = harness["world"]
	var dispatch: SageScriptDispatch = harness["dispatch"]
	world.expect_refusals()

	_check(
		"an_unanswerable_unit_threat_read_refuses_instead_of_reading_false",
		not _cond(harness, "UNIT_THREAT_LEVEL", [
			_name_arg("AI_TEST_GATE"), _comparison_arg(ParamTypes.COMPARE_LESS),
			_real_arg(10.0), _real_arg(350.0)
		])
		and dispatch.gaps.has(
			"condition", "UNIT_THREAT_LEVEL", GapLog.REASON_WORLD_REFUSED
		),
		str(dispatch.gaps.to_lines())
	)
	_check(
		"the_unit_refusal_names_the_units_facet_method",
		world.refused("units.threat_within_radius"),
		str(world.refusal_log)
	)


func _test_team_threat_refusal_is_not_false() -> void:
	var harness := _harness()
	var world: ThreatWorld = harness["world"]
	var dispatch: SageScriptDispatch = harness["dispatch"]
	world.expect_refusals()

	_check(
		"an_unanswerable_team_threat_read_refuses_instead_of_reading_false",
		not _cond(harness, "TEAM_THREAT_LEVEL", [
			_name_arg(THIS_TEAM), _comparison_arg(ParamTypes.COMPARE_GREATER_EQUAL),
			_real_arg(1.0), _real_arg(300.0)
		])
		and dispatch.gaps.has(
			"condition", "TEAM_THREAT_LEVEL", GapLog.REASON_WORLD_REFUSED
		),
		str(dispatch.gaps.to_lines())
	)
	_check(
		"the_team_refusal_names_the_teams_facet_method",
		world.refused("teams.threat_within_radius"),
		str(world.refusal_log)
	)


# --- 4. Operator table -------------------------------------------------------


func _test_out_of_table_operator_is_bad_arguments() -> void:
	# ParamTypes.compare answers false for any operator it does not know, which
	# would turn "this handler does not understand the operator" into a
	# confident "no threat" at an AI decision gate. Out-of-table operators are
	# refused as BAD_ARGUMENTS before the world is consulted.
	var harness := _harness()
	var world: ThreatWorld = harness["world"]
	var dispatch: SageScriptDispatch = harness["dispatch"]
	world.set_unit_threat("AI_TEST_GATE", 350.0, 5.0)
	world.set_team_threat(THIS_TEAM, 300.0, 2.0)

	_check(
		"an_out_of_table_unit_operator_is_bad_arguments_not_a_quiet_false",
		not _cond(harness, "UNIT_THREAT_LEVEL", [
			_name_arg("AI_TEST_GATE"), _comparison_arg(6),
			_real_arg(10.0), _real_arg(350.0)
		])
		and dispatch.gaps.has(
			"condition", "UNIT_THREAT_LEVEL", GapLog.REASON_BAD_ARGUMENTS
		),
		str(dispatch.gaps.to_lines())
	)
	_check(
		"an_out_of_table_team_operator_is_bad_arguments_not_a_quiet_false",
		not _cond(harness, "TEAM_THREAT_LEVEL", [
			_name_arg(THIS_TEAM), _comparison_arg(-1),
			_real_arg(1.0), _real_arg(300.0)
		])
		and dispatch.gaps.has(
			"condition", "TEAM_THREAT_LEVEL", GapLog.REASON_BAD_ARGUMENTS
		),
		str(dispatch.gaps.to_lines())
	)
	_check(
		"an_out_of_table_operator_never_reaches_the_world",
		world.reads.is_empty(),
		str(world.reads)
	)


# --- 5. Purity ---------------------------------------------------------------


func _test_conditions_have_no_side_effects() -> void:
	# Both conditions gate polled AI scripts and are evaluated an unpredictable
	# number of times per match, so any env or world mutation is a lockstep
	# determinism bug. ctx.result is the ONLY output.
	var harness := _harness()
	var world: ThreatWorld = harness["world"]
	var env: SageScriptEnv = harness["env"]
	world.set_unit_threat("AI_TEST_GATE", 350.0, 5.0)
	world.set_team_threat(THIS_TEAM, 300.0, 2.0)

	for _repeat in range(3):
		_cond(harness, "UNIT_THREAT_LEVEL", [
			_name_arg("AI_TEST_GATE"), _comparison_arg(ParamTypes.COMPARE_LESS),
			_real_arg(10.0), _real_arg(350.0)
		])
		_cond(harness, "TEAM_THREAT_LEVEL", [
			_name_arg(THIS_TEAM), _comparison_arg(ParamTypes.COMPARE_GREATER_EQUAL),
			_real_arg(1.0), _real_arg(300.0)
		])

	_check(
		"repeated_evaluation_left_the_script_environment_alone",
		env.snapshot()["counters"] == {} and env.snapshot()["flags"] == {}
	)
	_check(
		"every_evaluation_reached_the_world_exactly_once",
		world.reads.size() == 6,
		str(world.reads)
	)


# --- 6. Arity ----------------------------------------------------------------


func _test_arity_is_enforced_per_condition() -> void:
	# Arity is checked against the sourced signature before the handler runs,
	# so a positional read can never quietly slide onto a neighbouring
	# argument. One wrong-arity case per served condition; the world is never
	# consulted for either.
	var harness := _harness()
	var world: ThreatWorld = harness["world"]
	var dispatch: SageScriptDispatch = harness["dispatch"]

	_check(
		"a_three_argument_unit_threat_record_is_rejected",
		not _cond(harness, "UNIT_THREAT_LEVEL", [
			_name_arg("AI_TEST_GATE"), _comparison_arg(ParamTypes.COMPARE_LESS),
			_real_arg(10.0)
		])
		and dispatch.gaps.has(
			"condition", "UNIT_THREAT_LEVEL", GapLog.REASON_BAD_ARGUMENTS
		),
		str(dispatch.gaps.to_lines())
	)
	_check(
		"a_five_argument_team_threat_record_is_rejected",
		not _cond(harness, "TEAM_THREAT_LEVEL", [
			_name_arg(THIS_TEAM), _comparison_arg(ParamTypes.COMPARE_GREATER_EQUAL),
			_real_arg(1.0), _real_arg(300.0), _real_arg(999.0)
		])
		and dispatch.gaps.has(
			"condition", "TEAM_THREAT_LEVEL", GapLog.REASON_BAD_ARGUMENTS
		),
		str(dispatch.gaps.to_lines())
	)
	_check(
		"a_bad_arity_record_never_reached_the_world",
		world.reads.is_empty(),
		str(world.reads)
	)


# --- Stub world ---------------------------------------------------------------
#
# SageScriptWorldStub teaches neither teams.threat_within_radius nor
# units.threat_within_radius, so both are taught HERE rather than in the shared
# stub (an existing file ~17 concurrent packages depend on). Every read is
# recorded as a flat string in world-signature order so a test asserts on the
# exact arguments that reached the world; every read answers from a fixture or
# REFUSES, so a test that forgot to set one (or a handler that read the wrong
# REAL slot as the radius) gets a loud refusal rather than a plausible zero.
# Radii are formatted "%.1f" so the fixture key and the assertion string cannot
# drift apart on float printing.
#
# THREAT UNITS (semantic assumption, owner-ratified 2026-08-08): the concrete
# world's threat_within_radius is the retail slice's hostile combat-weight sum
# (retail_slice_script_world.gd:2968 teams / :6083 units) - a slice formula,
# not a reverse-sourced SAGE quantity. This runner therefore asserts the
# COMPARISON MECHANICS against stub-seeded floats and does not pin any absolute
# threat magnitude.


class ThreatWorld:
	extends SageScriptWorldStub

	var reads: Array[String] = []

	## "unit|radius" -> float threat within that radius of the unit.
	var unit_threats: Dictionary = {}

	## "team|radius" -> float threat within that radius of the team.
	var team_threats: Dictionary = {}

	func set_unit_threat(object_name: String, radius: float, threat: float) -> void:
		unit_threats["%s|%.1f" % [object_name, radius]] = threat

	func set_team_threat(team: String, radius: float, threat: float) -> void:
		team_threats["%s|%.1f" % [team, radius]] = threat

	func _make_teams() -> SageScriptWorld.Teams:
		return ThreatTeams.new()

	func _make_units() -> SageScriptWorld.Units:
		return ThreatUnits.new()


class ThreatTeams:
	extends SageScriptWorld.Teams

	func threat_within_radius(team: String, radius: float) -> SageWorldQuery:
		var stub := world as ThreatWorld
		var key := "%s|%.1f" % [team, radius]
		stub.reads.append("teams.threat_within_radius|%s" % key)
		if not stub.team_threats.has(key):
			return _refuse_query(
				"teams.threat_within_radius", "no fixture threat for '%s'" % key
			)
		return SageWorldQuery.hit(float(stub.team_threats[key]))


class ThreatUnits:
	extends SageScriptWorld.Units

	func threat_within_radius(object_name: String, radius: float) -> SageWorldQuery:
		var stub := world as ThreatWorld
		var key := "%s|%.1f" % [object_name, radius]
		stub.reads.append("units.threat_within_radius|%s" % key)
		if not stub.unit_threats.has(key):
			return _refuse_query(
				"units.threat_within_radius", "no fixture threat for '%s'" % key
			)
		return SageWorldQuery.hit(float(stub.unit_threats[key]))


# --- Reporting ----------------------------------------------------------------


func _check(name: String, condition: bool, detail: String = "") -> void:
	_watchdog.note(name)
	if condition:
		passed += 1
		print("HANDLERS_WP21 PASS %s" % name)
	else:
		failed += 1
		printerr(
			"HANDLERS_WP21 FAIL %s%s" % [name, " (%s)" % detail if detail != "" else ""]
		)
