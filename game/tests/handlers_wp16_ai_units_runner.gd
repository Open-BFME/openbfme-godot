extends SceneTree

## Proof runner for WP16-units, AI-critical subset (wp16_ai_units.gd).
##
## Covers, in order:
##   1. registration    - all 8 AI-used members accounted for, 6 served and 2
##                        gap-registered, with no overlap and no silent absence
##   2. positional args - the reference family's destination-first signatures,
##                        with fixture values chosen so a swapped read produces
##                        a DIFFERENT logged call rather than an equal one, and
##                        the ownership condition whose two text arguments only
##                        position can tell apart
##   3. flag folding    - GATE_OPEN passes open=true, GATE_CLOSE open=false,
##                        both ready=false, and PLAYER_ENABLE_UNIT_CONSTRUCTION
##                        passes TRUE with the facet's every-type "" - each
##                        spelling's exact values pinned separately, because a
##                        backwards flag still returns OK
##   4. conditions      - true, false, and the third answer: a read the world
##                        cannot answer NEVER comes back as a plain false, and
##                        a placeholder player token is refused rather than
##                        compared literally
##   5. arity and types - one wrong-arity case per served member; wrong-type
##                        cases for every slot with an OBSERVED wire code
##                        (PLAYER, code 11 - the only one this subset has; see
##                        the note at that test)
##   6. gap register    - the 2 blocked members report BLOCKED, name the
##                        missing world signature, never reach the world, and
##                        are not counted as coverage
##
## Every fixture value is SYNTHETIC. The call SHAPES are modelled on the retail
## AI libraries (an AI_*-style reference name, a "<This Player>" token, a gate
## object name) because those shapes are what the handlers must survive, but no
## retail payload text is reproduced here.
##
## Invocation:
##   Godot_v4.7-stable_win64_console.exe --headless --path game \
##     -s res://tests/handlers_wp16_ai_units_runner.gd

const Registry := preload("res://src/script/handlers/_registry.gd")
const Dispatch := preload("res://src/script/script_dispatch.gd")
const CoreHandlers := preload("res://src/script/script_handlers_core.gd")
const Env := preload("res://src/script/script_env.gd")
const GapLog := preload("res://src/script/script_gaps.gd")
const ParamTypes := preload("res://src/script/script_param_types.gd")
const Wp16 := preload("res://src/script/handlers/wp16_ai_units.gd")

## The 6 members this package serves and the 2 it gap-registers. Written out
## rather than derived from the module, so that a member quietly disappearing
## from register() fails here instead of agreeing with itself.
const SERVED_ACTIONS := [
	"SET_UNIT_REFERENCE",
	"SET_UNIT_REFERENCE_TO_REFERENCE",
	"GATE_OPEN",
	"GATE_CLOSE",
	"PLAYER_ENABLE_UNIT_CONSTRUCTION",
	"CREATE_OBJECT",
]

const SERVED_CONDITIONS := [
	"NAMED_OWNED_BY_PLAYER",
	"TYPE_SIGHTED",
]

const GAP_REGISTERED_ACTIONS: Array = []
const GAP_REGISTERED_CONDITIONS: Array = []

## LIVENESS GUARD. A GDScript RUNTIME error aborts the enclosing function
## on the spot without propagating, so every later _check() in that function
## silently never runs and `failed` never moves - the runner then prints a
## zero-failure result and exits 0 with SCRIPT ERROR lines above it. This is
## the exact count a HEALTHY run makes; if the run makes any other number,
## something aborted (or an assertion was added without updating this) and
## the result is not to be trusted.
const EXPECTED_CHECKS := 43

var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	_test_registration_covers_the_ai_subset()
	_test_owned_by_player_compares_the_world_owner_exactly()
	_test_owned_by_player_resolves_placeholder_player_tokens()
	_test_an_unanswerable_condition_is_never_a_plain_false()
	_test_conditions_have_no_side_effects()
	_test_set_unit_reference_binds_destination_first()
	_test_reference_to_reference_copies_source_into_destination()
	_test_gate_spellings_pass_their_exact_flags()
	_test_enable_unit_construction_passes_the_enable_flag()
	_test_command_refusal_is_reported()
	_test_arity_is_enforced_per_member()
	_test_argument_type_codes_are_enforced_where_observed()
	_test_gap_registered_members()
	var ran := passed + failed
	if ran != EXPECTED_CHECKS:
		failed += 1
		printerr("HANDLERS_WP16 FAIL liveness: ran %d checks, expected %d - a function aborted before its assertions" % [ran, EXPECTED_CHECKS])
	print("HANDLERS_WP16_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


# --- Fixture helpers ------------------------------------------------------


func _argument(
	argument_type: int, integer: int = 0, real: float = 0.0, text: String = ""
) -> Dictionary:
	return {"argumentType": argument_type, "integer": integer, "real": real, "text": text}


func _name_arg(value: String) -> Dictionary:
	# UNIT, UNIT_REF, OBJECT_TYPE and TEAM have no observed wire code in this
	# repo, so SageScriptArgs does not assert one. The integer field carries a
	# DECOY: a handler that read the wrong field would get 424242 instead of
	# the name, loudly rather than plausibly.
	return _argument(99, 424242, 0.0, value)


func _player_arg(value: String) -> Dictionary:
	return _argument(ParamTypes.ARGUMENT_PLAYER, 424242, 0.0, value)


func _real_arg(value: float) -> Dictionary:
	# ANGLE is real-valued per PAYLOAD_FIELD_FOR_PARAM; its wire code is
	# unobserved, so ARGUMENT_REAL here is a plausible shape, not an assertion.
	return _argument(ParamTypes.ARGUMENT_REAL, 424242, value, "DECOY_TEXT")


func _position_arg(x: float, y: float, z: float) -> Dictionary:
	# COORD3D arguments carry a 3-float position and no integer/real/text
	# payload (sage_scb.py _POSITION_ARGUMENT_TYPE).
	return {"argumentType": ParamTypes.ARGUMENT_POSITION, "position": [x, y, z]}


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
		"world": UnitWorld.new(20260726),
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
		"the_wp16_ai_package_is_present",
		(outcome["packages"] as Array).has("WP16-ai-units"),
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
	for name: String in GAP_REGISTERED_ACTIONS + GAP_REGISTERED_CONDITIONS:
		if not dispatch.blocked_names().has(name):
			unblocked.append(name)
	_check("every_gap_registered_member_is_blocked", unblocked.is_empty(), str(unblocked))

	# 5 + 1 + 2 = 8, the exact AI-used membership the census records
	# (game/data/retail_ai_call_census.json, workPackage == "WP16-units").
	# If a later edit serves a gap-registered member without removing it from
	# the gap list, or vice versa, these two disagree.
	_check(
		"the_subset_is_eight_members",
		(
			SERVED_ACTIONS.size() + SERVED_CONDITIONS.size()
			+ GAP_REGISTERED_ACTIONS.size() + GAP_REGISTERED_CONDITIONS.size() == 8
		),
		"served_actions=%d served_conditions=%d gapped=%d" % [
			SERVED_ACTIONS.size(), SERVED_CONDITIONS.size(),
			GAP_REGISTERED_ACTIONS.size() + GAP_REGISTERED_CONDITIONS.size()
		]
	)
	var overlap: Array[String] = []
	for name: String in GAP_REGISTERED_ACTIONS:
		if dispatch.implemented_actions().has(name):
			overlap.append(name)
	for name: String in GAP_REGISTERED_CONDITIONS:
		if dispatch.implemented_conditions().has(name):
			overlap.append(name)
	_check(
		"a_gap_registered_member_is_not_counted_as_coverage",
		overlap.is_empty(),
		str(overlap)
	)

	# The gap lists this file asserts on must be the ones the module actually
	# declares, or the two could drift apart while both look right.
	_check(
		"the_modules_gap_lists_match_this_runners",
		(
			Wp16.GAP_CREATE_OBJECT_ACTIONS == GAP_REGISTERED_ACTIONS
			and Wp16.GAP_TYPE_SIGHTED_CONDITIONS == GAP_REGISTERED_CONDITIONS
		),
		"module=%s/%s runner=%s/%s" % [
			str(Wp16.GAP_CREATE_OBJECT_ACTIONS), str(Wp16.GAP_TYPE_SIGHTED_CONDITIONS),
			str(GAP_REGISTERED_ACTIONS), str(GAP_REGISTERED_CONDITIONS)
		]
	)


# --- 2/4. NAMED_OWNED_BY_PLAYER -------------------------------------------


func _test_owned_by_player_compares_the_world_owner_exactly() -> void:
	# NAMED_OWNED_BY_PLAYER(UNIT, PLAYER). The object is argument 0 and the
	# player argument 1; both are text-carried, so only position separates
	# them. ONLY the unit name is taught to the stub: a swapped read asks the
	# world who owns "Player_1", misses the fixture, REFUSES, and the true-case
	# below fails - so an index swap cannot pass this test by luck.
	var harness := _harness()
	var world: UnitWorld = harness["world"]
	world.set_unit_owner("Synthetic_Gate_House", "Player_1")

	_check(
		"the_matching_owner_reads_true",
		_cond(harness, "NAMED_OWNED_BY_PLAYER", [
			_name_arg("Synthetic_Gate_House"), _player_arg("Player_1")
		]),
		str((harness["dispatch"] as SageScriptDispatch).gaps.to_lines())
	)
	_check(
		"a_different_owner_reads_false",
		not _cond(harness, "NAMED_OWNED_BY_PLAYER", [
			_name_arg("Synthetic_Gate_House"), _player_arg("Player_2")
		])
	)
	# Exact comparison: no case folding, no trimming - a mod's two distinct
	# player tokens must stay distinct here exactly as they do in retail.
	_check(
		"the_owner_comparison_is_case_sensitive",
		not _cond(harness, "NAMED_OWNED_BY_PLAYER", [
			_name_arg("Synthetic_Gate_House"), _player_arg("player_1")
		])
	)


func _test_owned_by_player_resolves_placeholder_player_tokens() -> void:
	# Placeholder resolution belongs to the world. The handler must preserve
	# the exact token and argument order so the executing world's player
	# binding, not a literal string comparison, decides ownership.
	var harness := _harness()
	var world: UnitWorld = harness["world"]
	var dispatch: SageScriptDispatch = harness["dispatch"]
	world.set_unit_owner("Synthetic_Gate_House", "Player_1")

	_check(
		"a_placeholder_token_resolves_at_the_call_site",
		_cond(harness, "NAMED_OWNED_BY_PLAYER", [
			_name_arg("Synthetic_Gate_House"), _player_arg("<This Player>")
		])
	)
	_check(
		"the_resolved_token_records_no_gap",
		not dispatch.gaps.has(
			"condition", "NAMED_OWNED_BY_PLAYER", GapLog.REASON_WORLD_REFUSED
		),
		str(dispatch.gaps.to_lines())
	)
	_check(
		"the_handler_preserves_object_then_player_order",
		world.ownership_queries == [
			["Synthetic_Gate_House", "<This Player>"]
		],
		str(world.ownership_queries)
	)
	_check(
		"the_token_case_issued_no_command_or_refusal",
		world.calls.is_empty() and world.refusal_log.is_empty(),
		"calls=%s refusals=%s" % [str(world.calls), str(world.refusal_log)]
	)


func _test_an_unanswerable_condition_is_never_a_plain_false() -> void:
	# A world that cannot answer must produce a REFUSAL - false at the call
	# site, but with a gap on the record. "That object is not owned by this
	# player" and "I have no idea who owns that object" are different answers,
	# and only the first may release the AI's claim logic.
	var harness := _harness()
	var world: UnitWorld = harness["world"]
	var dispatch: SageScriptDispatch = harness["dispatch"]
	world.expect_refusals()
	# "Synthetic_Unknown" has no fixture, so the ownership read refuses.

	_check(
		"an_unanswerable_owner_refuses_instead_of_inventing_one",
		not _cond(harness, "NAMED_OWNED_BY_PLAYER", [
			_name_arg("Synthetic_Unknown"), _player_arg("Player_1")
		])
		and dispatch.gaps.has(
			"condition", "NAMED_OWNED_BY_PLAYER", GapLog.REASON_WORLD_REFUSED
		),
		str(dispatch.gaps.to_lines())
	)
	_check(
		"the_refusal_names_the_missing_world_method",
		world.refused("units.is_owned_by"),
		str(world.refusal_log)
	)


func _test_conditions_have_no_side_effects() -> void:
	# Conditions are evaluated an unpredictable number of times, so any
	# mutation is a determinism bug. The condition may not touch the env or
	# command the world - a token refusal included.
	var harness := _harness()
	var world: UnitWorld = harness["world"]
	var env: SageScriptEnv = harness["env"]
	world.set_unit_owner("Synthetic_Gate_House", "Player_1")

	for _repeat in range(3):
		_cond(harness, "NAMED_OWNED_BY_PLAYER", [
			_name_arg("Synthetic_Gate_House"), _player_arg("Player_1")
		])
		_cond(harness, "NAMED_OWNED_BY_PLAYER", [
			_name_arg("Synthetic_Gate_House"), _player_arg("<This Player>")
		])

	_check(
		"the_condition_issued_no_world_commands",
		world.calls.is_empty(),
		str(world.calls)
	)
	_check(
		"the_condition_left_the_script_environment_alone",
		env.snapshot()["counters"] == {} and env.snapshot()["flags"] == {}
	)


# --- 2/3. The reference store ---------------------------------------------


func _test_set_unit_reference_binds_destination_first() -> void:
	# SET_UNIT_REFERENCE(UNIT_REF, UNIT): argument 0 is the DESTINATION
	# reference and argument 1 the object it points at. The two fixture names
	# are distinguishable in the logged call, so a swapped read produces a
	# different string, not an accidentally equal one.
	var harness := _harness()
	var world: UnitWorld = harness["world"]
	var status := _act(harness, "SET_UNIT_REFERENCE", [
		_name_arg("SYNTH_REF_GATE"), _name_arg("Synthetic_Gate_House")
	])
	_check(
		"set_unit_reference_binds_the_reference_to_the_object",
		status == Dispatch.Status.OK
		and world.calls == ["units.set_reference|SYNTH_REF_GATE|Synthetic_Gate_House"],
		"status=%d calls=%s" % [status, str(world.calls)]
	)


func _test_reference_to_reference_copies_source_into_destination() -> void:
	# SET_UNIT_REFERENCE_TO_REFERENCE(UNIT_REF, UNIT_REF): destination first,
	# source second - two arguments of the SAME type, so only position can
	# tell them apart. Swapped, the source would be overwritten with the
	# destination's stale binding; the exact logged order pins it.
	var harness := _harness()
	var world: UnitWorld = harness["world"]
	var status := _act(harness, "SET_UNIT_REFERENCE_TO_REFERENCE", [
		_name_arg("SYNTH_REF_DEST"), _name_arg("SYNTH_REF_SOURCE")
	])
	_check(
		"reference_to_reference_reaches_the_same_world_seam_destination_first",
		status == Dispatch.Status.OK
		and world.calls == ["units.set_reference|SYNTH_REF_DEST|SYNTH_REF_SOURCE"],
		"status=%d calls=%s" % [status, str(world.calls)]
	)


# --- 3. Folded flags ------------------------------------------------------


func _test_gate_spellings_pass_their_exact_flags() -> void:
	# The world folds GATE_OPEN / GATE_CLOSE / GATE_READY into
	# set_gate_state(name, open, ready). Each spelling's exact flag values are
	# pinned SEPARATELY: a backwards `open` would swing the AI's gate the
	# wrong way while still returning OK, and a stray ready=true would turn a
	# plain open/close into the GATE_READY spelling this file does not own.
	var harness := _harness()
	var world: UnitWorld = harness["world"]

	var open_status := _act(harness, "GATE_OPEN", [_name_arg("Synthetic_Gate")])
	_check(
		"gate_open_passes_open_true_ready_false",
		open_status == Dispatch.Status.OK
		and world.calls == ["units.set_gate_state|Synthetic_Gate|true|false"],
		"status=%d calls=%s" % [open_status, str(world.calls)]
	)

	world.calls.clear()
	var close_status := _act(harness, "GATE_CLOSE", [_name_arg("Synthetic_Gate")])
	_check(
		"gate_close_passes_open_false_ready_false",
		close_status == Dispatch.Status.OK
		and world.calls == ["units.set_gate_state|Synthetic_Gate|false|false"],
		"status=%d calls=%s" % [close_status, str(world.calls)]
	)


func _test_enable_unit_construction_passes_the_enable_flag() -> void:
	# The world folds PLAYER_ENABLE_UNIT_CONSTRUCTION and its DISABLE sibling
	# into one method with a flag, plus an object_type NEITHER spelling
	# carries. This is the ENABLE spelling, so the flag is TRUE, and the type
	# slot is the facet's every-type "" - visible as an empty segment in the
	# logged call, so a handler that invented a type would fail here.
	var harness := _harness()
	var world: UnitWorld = harness["world"]
	var status := _act(
		harness, "PLAYER_ENABLE_UNIT_CONSTRUCTION", [_player_arg("Player_1")]
	)
	_check(
		"enable_unit_construction_passes_true_and_the_every_type_form",
		status == Dispatch.Status.OK
		and world.calls == ["players.set_unit_construction_enabled|Player_1||true"],
		"status=%d calls=%s" % [status, str(world.calls)]
	)


# --- The refusing world ---------------------------------------------------


func _test_command_refusal_is_reported() -> void:
	# A world that does not model these commands must produce a structured gap
	# per action, never a quiet success. The BASE SageScriptWorld is exactly
	# that world: every Units and Players command inherits the refusing base.
	var dispatch := Dispatch.new()
	CoreHandlers.register_all(dispatch)
	Registry.register_all(dispatch)
	var env := Env.new()

	var cases := [
		["SET_UNIT_REFERENCE",
			[_name_arg("SYNTH_REF_GATE"), _name_arg("Synthetic_Gate_House")],
			"units.set_reference"],
		["GATE_OPEN", [_name_arg("Synthetic_Gate")], "units.set_gate_state"],
		["PLAYER_ENABLE_UNIT_CONSTRUCTION", [_player_arg("Player_1")],
			"players.set_unit_construction_enabled"],
	]
	for case: Array in cases:
		var status := dispatch.execute_action(
			_record(String(case[0]), case[1]), env, SageScriptWorld.new(), "fixture"
		)
		_check(
			"a_refused_%s_is_world_refused_not_ok" % String(case[0]).to_lower(),
			status == Dispatch.Status.WORLD_REFUSED,
			"status=%d" % status
		)
		_check(
			"the_%s_refusal_names_the_missing_facet_method" % String(case[0]).to_lower(),
			dispatch.gaps.has("action", String(case[0]), GapLog.REASON_WORLD_REFUSED)
			and _gap_detail(
				dispatch, "action", String(case[0]), GapLog.REASON_WORLD_REFUSED
			).contains(String(case[2])),
			str(dispatch.gaps.to_lines())
		)


# --- 5. Arity and argument coding, per served member ----------------------


func _test_arity_is_enforced_per_member() -> void:
	# Arity is checked against the sourced signature before the handler runs,
	# so a positional read can never quietly slide onto a neighbouring
	# argument. One wrong-arity case per served member.
	var harness := _harness()
	var world: UnitWorld = harness["world"]
	world.set_unit_owner("Synthetic_Gate_House", "Player_1")

	_check(
		"a_one_argument_ownership_test_is_rejected",
		not _cond(harness, "NAMED_OWNED_BY_PLAYER", [_name_arg("Synthetic_Gate_House")])
		and (harness["dispatch"] as SageScriptDispatch).gaps.has(
			"condition", "NAMED_OWNED_BY_PLAYER", GapLog.REASON_BAD_ARGUMENTS
		),
		str((harness["dispatch"] as SageScriptDispatch).gaps.to_lines())
	)
	_check(
		"a_one_argument_set_unit_reference_is_rejected",
		_act(harness, "SET_UNIT_REFERENCE", [_name_arg("SYNTH_REF_GATE")])
		== Dispatch.Status.BAD_ARGUMENTS
	)
	_check(
		"a_three_argument_reference_copy_is_rejected",
		_act(harness, "SET_UNIT_REFERENCE_TO_REFERENCE", [
			_name_arg("SYNTH_REF_DEST"), _name_arg("SYNTH_REF_SOURCE"),
			_name_arg("SYNTH_REF_EXTRA")
		]) == Dispatch.Status.BAD_ARGUMENTS
	)
	_check(
		"a_zero_argument_gate_open_is_rejected",
		_act(harness, "GATE_OPEN", []) == Dispatch.Status.BAD_ARGUMENTS
	)
	_check(
		"a_two_argument_gate_close_is_rejected",
		_act(harness, "GATE_CLOSE", [_name_arg("Synthetic_Gate"), _name_arg("Extra")])
		== Dispatch.Status.BAD_ARGUMENTS
	)
	_check(
		"a_two_argument_enable_unit_construction_is_rejected",
		_act(harness, "PLAYER_ENABLE_UNIT_CONSTRUCTION", [
			_player_arg("Player_1"), _player_arg("Player_2")
		]) == Dispatch.Status.BAD_ARGUMENTS
	)
	_check(
		"no_rejected_call_reached_the_world",
		world.calls.is_empty(),
		str(world.calls)
	)


func _test_argument_type_codes_are_enforced_where_observed() -> void:
	# PLAYER (11) is the ONLY parameter type in this subset with an OBSERVED
	# wire code, so it is the only slot SageScriptArgs asserts on. UNIT,
	# UNIT_REF, OBJECT_TYPE and TEAM are unobserved BY DESIGN
	# (SageScriptParamTypes is partial; an invented expectation would reject
	# valid data), so no wrong-type case can be written for the reference and
	# gate members without inventing a wire code - the exact defect class that
	# table warns against. What stands in for it is the DECOY discipline:
	# every text fixture carries 424242 in its integer field, so a wrong-FIELD
	# read would log 424242 and fail the exact-string call assertions above.
	var harness := _harness()
	var world: UnitWorld = harness["world"]
	world.set_unit_owner("Synthetic_Gate_House", "Player_1")

	_check(
		"a_miscoded_ownership_player_slot_is_rejected",
		not _cond(harness, "NAMED_OWNED_BY_PLAYER", [
			_name_arg("Synthetic_Gate_House"), _name_arg("Player_1")
		])
		and (harness["dispatch"] as SageScriptDispatch).gaps.has(
			"condition", "NAMED_OWNED_BY_PLAYER", GapLog.REASON_BAD_ARGUMENTS
		),
		str((harness["dispatch"] as SageScriptDispatch).gaps.to_lines())
	)
	_check(
		"a_miscoded_enable_unit_construction_player_slot_is_rejected",
		_act(harness, "PLAYER_ENABLE_UNIT_CONSTRUCTION", [_name_arg("Player_1")])
		== Dispatch.Status.BAD_ARGUMENTS
	)
	_check(
		"no_miscoded_call_reached_the_world",
		world.calls.is_empty(),
		str(world.calls)
	)


# --- 6. Gap-registered members --------------------------------------------


func _test_gap_registered_members() -> void:
	## CREATE_OBJECT and TYPE_SIGHTED are now served through team spawn / sighting.
	var harness := _harness()
	var dispatch: SageScriptDispatch = harness["dispatch"]
	var world: UnitWorld = harness["world"]
	world.sighted_triples["Synthetic_Scout|Synthetic_Fortress_Type|Player_2"] = true

	var create_status := _act(harness, "CREATE_OBJECT", [
		_name_arg("Synthetic_Sentry_Type"), _name_arg("Synthetic_Team_Front"),
		_position_arg(100.0, 0.0, 250.0), _real_arg(90.0)
	])
	_check(
		"create_object_is_served",
		create_status == Dispatch.Status.OK,
		"status=%d" % create_status
	)
	_check(
		"create_object_reaches_team_spawn_surface",
		not world.calls.is_empty(),
		str(world.calls)
	)

	var sighted := _cond(harness, "TYPE_SIGHTED", [
		_name_arg("Synthetic_Scout"), _name_arg("Synthetic_Fortress_Type"),
		_player_arg("Player_2")
	])
	_check("type_sighted_evaluates_from_fixture", sighted)
	_check(
		"formerly_gap_members_are_not_dispatch_blocked",
		not dispatch.blocked_names().has("CREATE_OBJECT")
		and not dispatch.blocked_names().has("TYPE_SIGHTED")
	)


# --- Stub world -----------------------------------------------------------
#
# The shared SageScriptWorldStub teaches no Units facet at all, so the whole
# facet is taught HERE rather than in the shared stub, because the shared stub
# is an existing file that the concurrent packages also depend on. Every
# command is recorded as a flat string so a test asserts on the exact
# arguments that reached the world, including both folded booleans and the
# empty every-type slot; every read answers from a fixture or REFUSES, so a
# test that forgot to set one gets a loud refusal rather than a plausible
# owner.


class UnitWorld:
	extends SageScriptWorldStub

	var calls: Array[String] = []
	var ownership_queries: Array = []
	var sighted_triples: Dictionary = {}

	## unit name -> owning player name, for units.owner.
	var unit_owners: Dictionary = {}

	func set_unit_owner(unit: String, owner_player: String) -> void:
		unit_owners[unit] = owner_player

	func _make_units() -> SageScriptWorld.Units:
		return AiStubUnits.new()

	func _make_players() -> SageScriptWorld.Players:
		return AiStubUnitPlayers.new()


class AiStubUnits:
	extends SageScriptWorld.Units

	func _log(text: String) -> bool:
		(world as UnitWorld).calls.append(text)
		return true

	func owner(object_name: String) -> SageWorldQuery:
		var stub := world as UnitWorld
		if not stub.unit_owners.has(object_name):
			return _refuse_query(
				"units.owner", "no fixture owner for object '%s'" % object_name
			)
		return SageWorldQuery.hit(stub.unit_owners[object_name])

	func is_owned_by(object_name: String, player: String) -> SageWorldQuery:
		var stub := world as UnitWorld
		stub.ownership_queries.append([object_name, player])
		if not stub.unit_owners.has(object_name):
			return _refuse_query(
				"units.is_owned_by", "no fixture owner for object '%s'" % object_name
			)
		var resolved_player := "Player_1" if player == "<This Player>" else player
		return SageWorldQuery.hit(
			String(stub.unit_owners[object_name]) == resolved_player
		)

	func set_reference(reference: String, object_name: String) -> bool:
		return _log("units.set_reference|%s|%s" % [reference, object_name])

	func set_gate_state(object_name: String, open: bool, ready: bool) -> bool:
		return _log(
			"units.set_gate_state|%s|%s|%s"
			% [object_name, str(open).to_lower(), str(ready).to_lower()]
		)

	func create_object_on_team(
		object_type: String, team: String, position: Vector3, angle: float
	) -> SageWorldQuery:
		_log(
			"units.create_object_on_team|%s|%s|%s|%s"
			% [object_type, team, str(position), str(angle)]
		)
		return SageWorldQuery.hit("spawned_%s" % object_type)

	func type_sighted_by(
		observer: String, object_type: String, owner: String
	) -> SageWorldQuery:
		var key := "%s|%s|%s" % [observer, object_type, owner]
		var fixtures: Dictionary = (world as UnitWorld).sighted_triples
		return SageWorldQuery.hit(bool(fixtures.get(key, false)))


class AiStubUnitPlayers:
	extends SageScriptWorldStub.StubPlayers

	func set_unit_construction_enabled(
		player: String, object_type: String, enabled: bool
	) -> bool:
		(world as UnitWorld).calls.append(
			"players.set_unit_construction_enabled|%s|%s|%s"
			% [player, object_type, str(enabled).to_lower()]
		)
		return true


# --- Reporting ------------------------------------------------------------


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


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("HANDLERS_WP16 PASS %s%s" % [name, " (%s)" % detail if detail != "" else ""])
	else:
		failed += 1
		printerr("HANDLERS_WP16 FAIL %s%s" % [name, " (%s)" % detail if detail != "" else ""])
