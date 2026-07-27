extends SceneTree

## Proof runner for WP11-basebuilding-ai, the AI-critical subset
## (wp11_ai_basebuilding.gd).
##
## Covers, in order:
##   1. registration    - all 8 AI-called members accounted for, 7 served and 1
##                        gap-registered, with no overlap and no silent absence
##   2. positional args - the condition that puts the base flag before the
##                        player, the guard that puts two TEAMs side by side,
##                        and the unpack pair that puts the base flag before
##                        its UNIT_REF destination
##   3. durations       - INT seconds reach the world as TICKS via the same
##                        conversion the seconds-counters use; the guard's 0-
##                        second sentinel collision refuses instead of guarding
##                        forever; negatives are BAD_ARGUMENTS
##   4. targets         - guard-team sends a TEAM target, guard-for-seconds a
##                        SELF target, the reinforcement an OBJECT target, all
##                        through the world's tagged target dictionaries
##   5. conditions      - true, false, and the third answer: a read the world
##                        cannot answer NEVER comes back as a plain false, and
##                        the polled condition has no side effects
##   6. unpacking       - NAMED_BASE_UNPACK reaches ai.base_unpack with
##                        free=false and NAMED_BASE_UNPACK_FREE with free=true
##                        (the fold comes from the OPCODE, never the
##                        arguments), both delivering the reference; a world
##                        refusal is a structured gap
##   7. refusal + arity - a world without the facets refuses per method; wrong
##                        arity and wrong-coded arguments are rejected before
##                        any handler runs
##   8. gap register    - the blocked member reports BLOCKED, names the missing
##                        world signature, and is not counted as coverage
##
## Every fixture value is SYNTHETIC. The call SHAPES are modelled on the retail
## AI libraries (a "<This Player>" style token, an integer seconds value, a
## base-flag-then-player order) because those shapes are what the handlers must
## survive, but no retail payload text is reproduced here.
##
## Invocation:
##   Godot_v4.7-stable_win64_console.exe --headless --path game \
##     -s res://tests/handlers_wp11_ai_basebuilding_runner.gd

const Registry := preload("res://src/script/handlers/_registry.gd")
const Dispatch := preload("res://src/script/script_dispatch.gd")
const CoreHandlers := preload("res://src/script/script_handlers_core.gd")
const Env := preload("res://src/script/script_env.gd")
const GapLog := preload("res://src/script/script_gaps.gd")
const ParamTypes := preload("res://src/script/script_param_types.gd")
const Wp11 := preload("res://src/script/handlers/wp11_ai_basebuilding.gd")

## The 7 members this package serves and the 1 it gap-registers. Written out
## rather than derived from the module, so that a member quietly disappearing
## from register() fails here instead of agreeing with itself.
const SERVED_ACTIONS := [
	"CREATE_REINFORCEMENT_TEAM_AT_UNIT_POSITION",
	"NAMED_BASE_UNPACK",
	"NAMED_BASE_UNPACK_FREE",
	"TEAM_GUARD_FOR_SECONDS",
	"TEAM_GUARD_TEAM",
	"TEAM_IDLE_FOR_SECONDS",
]

const SERVED_CONDITIONS := [
	"NAMED_BASE_UNPACKABLE_FOR_PLAYER",
]

const GAP_REGISTERED := [
	"BUILD_BASE_BUILDING_PER_TACTICAL_MARKER",
]

var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	_test_registration_covers_the_ai_subset()
	_test_condition_reads_base_flag_then_player()
	_test_condition_answers_truthfully()
	_test_an_unanswerable_condition_is_never_a_plain_false()
	_test_condition_has_no_side_effects()
	_test_guard_team_actor_and_target_order()
	_test_guard_for_seconds_converts_and_respects_the_sentinel()
	_test_idle_for_seconds_converts()
	_test_reinforcement_team_target_and_ownership()
	_test_base_unpack_pair_serves_flag_then_reference()
	_test_command_refusal_is_reported()
	_test_arity_is_enforced()
	_test_argument_coding_is_enforced_where_observed()
	_test_gap_registered_members()
	print("HANDLERS_WP11_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


# --- Fixture helpers ------------------------------------------------------


func _argument(
	argument_type: int, integer: int = 0, real: float = 0.0, text: String = ""
) -> Dictionary:
	return {"argumentType": argument_type, "integer": integer, "real": real, "text": text}


func _name_arg(value: String) -> Dictionary:
	# TEAM, UNIT, UNIT_REF and OBJECT_TYPE have no observed wire code in this
	# repo, so SageScriptArgs does not assert one. The integer field carries a
	# DECOY: a handler that read the wrong field would get 424242 instead of
	# the name, loudly rather than plausibly.
	return _argument(99, 424242, 0.0, value)


func _player_arg(value: String) -> Dictionary:
	return _argument(ParamTypes.ARGUMENT_PLAYER, 424242, 0.0, value)


func _int_arg(value: int, decoy_text: String = "DECOY_TEXT") -> Dictionary:
	return _argument(ParamTypes.ARGUMENT_INTEGER, value, 0.0, decoy_text)


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
		"world": BaseWorld.new(20260726),
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


func _ticks(harness: Dictionary, seconds: int) -> int:
	# The expected tick count is computed through the SAME env conversion the
	# handler must use, so this runner asserts agreement with the seconds-
	# counter rounding rather than re-inventing (and possibly re-diverging) it.
	return (harness["env"] as SageScriptEnv).ticks_from_seconds(float(seconds))


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
		"the_wp11_subset_package_is_present",
		(outcome["packages"] as Array).has("WP11-basebuilding-ai"),
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

	# 6 + 1 + 1 = 8, the exact AI-called membership this subset was scoped to
	# (game/data/retail_ai_call_census.json; the ninth AI-called WP11 member,
	# BUILD_BASE_BUILDING, is served by the tail package). If a later edit
	# serves the gap-registered member without removing it from the gap list,
	# or vice versa, these two disagree.
	_check(
		"the_subset_is_eight_members",
		SERVED_ACTIONS.size() + SERVED_CONDITIONS.size() + GAP_REGISTERED.size() == 8,
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
	var declared: Array = Wp11.GAP_BUILD_PER_MARKER_ACTIONS.duplicate()
	declared.sort()
	_check(
		"the_modules_gap_list_matches_this_runners",
		declared == GAP_REGISTERED,
		"module=%s runner=%s" % [str(declared), str(GAP_REGISTERED)]
	)


# --- 2. Condition argument order ------------------------------------------


func _test_condition_reads_base_flag_then_player() -> void:
	# NAMED_BASE_UNPACKABLE_FOR_PLAYER(UNIT, PLAYER): base flag FIRST, player
	# SECOND. The fixture is keyed on the (flag, player) pair in that order, so
	# a swapped read asks about ("SYNTH_PLAYER_1", "SYNTH_BASE_SITE"), finds no
	# fixture, and surfaces as a refusal instead of a quiet pass.
	var harness := _harness()
	var world: BaseWorld = harness["world"]
	world.unpackable["SYNTH_BASE_SITE|SYNTH_PLAYER_1"] = true

	_check(
		"the_base_flag_is_argument_zero_and_the_player_argument_one",
		_cond(harness, "NAMED_BASE_UNPACKABLE_FOR_PLAYER", [
			_name_arg("SYNTH_BASE_SITE"), _player_arg("SYNTH_PLAYER_1")
		])
	)
	_check(
		"the_read_reached_the_ai_facet_with_both_names_in_order",
		world.reads.has("ai.base_unpackable|SYNTH_BASE_SITE|SYNTH_PLAYER_1"),
		str(world.reads)
	)


# --- 3. Condition truthfulness --------------------------------------------


func _test_condition_answers_truthfully() -> void:
	var harness := _harness()
	var world: BaseWorld = harness["world"]
	world.unpackable["SYNTH_SITE_A|SYNTH_PLAYER_1"] = true
	world.unpackable["SYNTH_SITE_B|SYNTH_PLAYER_1"] = false

	_check(
		"an_unpackable_base_reads_true",
		_cond(harness, "NAMED_BASE_UNPACKABLE_FOR_PLAYER", [
			_name_arg("SYNTH_SITE_A"), _player_arg("SYNTH_PLAYER_1")
		])
	)
	# The world ANSWERED false here - this is a real "no", not a refusal, and
	# it must arrive without a gap.
	var before := (harness["dispatch"] as SageScriptDispatch).gaps.entries.size()
	_check(
		"a_non_unpackable_base_reads_false",
		not _cond(harness, "NAMED_BASE_UNPACKABLE_FOR_PLAYER", [
			_name_arg("SYNTH_SITE_B"), _player_arg("SYNTH_PLAYER_1")
		])
	)
	_check(
		"an_answered_false_records_no_gap",
		(harness["dispatch"] as SageScriptDispatch).gaps.entries.size() == before,
		str((harness["dispatch"] as SageScriptDispatch).gaps.to_lines())
	)


func _test_an_unanswerable_condition_is_never_a_plain_false() -> void:
	# The most important test in this file. "This base cannot unpack" releases
	# the AI economy to spend elsewhere; "the world does not model unpacking"
	# must keep the gate shut WITH a gap on the record. The two both read false
	# at the call site and differ only in the gap - which is the difference.
	var harness := _harness()
	var world: BaseWorld = harness["world"]
	var dispatch: SageScriptDispatch = harness["dispatch"]
	world.expect_refusals()
	# "SYNTH_NOWHERE_SITE" has no fixture, so the read about it refuses.

	_check(
		"an_unanswerable_unpackable_read_evaluates_false",
		not _cond(harness, "NAMED_BASE_UNPACKABLE_FOR_PLAYER", [
			_name_arg("SYNTH_NOWHERE_SITE"), _player_arg("SYNTH_PLAYER_1")
		])
	)
	_check(
		"an_unanswerable_unpackable_read_records_a_world_refused_gap",
		dispatch.gaps.has(
			"condition", "NAMED_BASE_UNPACKABLE_FOR_PLAYER", GapLog.REASON_WORLD_REFUSED
		),
		str(dispatch.gaps.to_lines())
	)
	_check(
		"the_refusal_names_the_missing_world_method",
		world.refused("ai.base_unpackable"),
		str(world.refusal_log)
	)


func _test_condition_has_no_side_effects() -> void:
	# This condition is polled by the AI economy loop an unpredictable number
	# of times per decision, so any mutation is a determinism bug. It may
	# neither command the world nor touch the env.
	var harness := _harness()
	var world: BaseWorld = harness["world"]
	var env: SageScriptEnv = harness["env"]
	world.unpackable["SYNTH_SITE_A|SYNTH_PLAYER_1"] = true

	for _repeat in range(3):
		_cond(harness, "NAMED_BASE_UNPACKABLE_FOR_PLAYER", [
			_name_arg("SYNTH_SITE_A"), _player_arg("SYNTH_PLAYER_1")
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


# --- 4. Guard and idle ----------------------------------------------------


func _test_guard_team_actor_and_target_order() -> void:
	var harness := _harness()
	var world: BaseWorld = harness["world"]

	# TEAM_GUARD_TEAM(TEAM, TEAM). Two arguments of the SAME type, so no
	# type-search could ever recover the order: argument 0 takes the order,
	# argument 1 is guarded. Swapped, the screen would guard itself behind the
	# team it was meant to protect.
	var status := _act(harness, "TEAM_GUARD_TEAM", [
		_name_arg("Screen Detachment"), _name_arg("Siege Train")
	])
	_check(
		"guard_team_is_served",
		status == Dispatch.Status.OK,
		"status=%d" % status
	)
	_check(
		"the_first_team_guards_and_the_second_is_guarded",
		world.calls.has(
			"orders.guard|%d|Screen Detachment|team:Siege Train|0"
			% SageScriptWorld.Scope.TEAM
		),
		str(world.calls)
	)
	_check(
		"team_scope_is_distinct_from_unit_and_player_scope",
		SageScriptWorld.Scope.TEAM != SageScriptWorld.Scope.UNIT
		and SageScriptWorld.Scope.TEAM != SageScriptWorld.Scope.PLAYER
	)


func _test_guard_for_seconds_converts_and_respects_the_sentinel() -> void:
	var harness := _harness()
	var world: BaseWorld = harness["world"]

	# TEAM_GUARD_FOR_SECONDS(TEAM, INT): the INT is read from the integer
	# field (the decoy text is what a wrong-field read would send), converted
	# to ticks through the env, and the team guards ITSELF - its current
	# position - not some other target.
	var status := _act(harness, "TEAM_GUARD_FOR_SECONDS", [
		_name_arg("Patrol"), _int_arg(15, "DECOY_NOT_A_DURATION")
	])
	_check(
		"fifteen_seconds_arrive_as_ticks_with_a_self_target",
		status == Dispatch.Status.OK
		and world.calls.has(
			"orders.guard|%d|Patrol|self|%d"
			% [SageScriptWorld.Scope.TEAM, _ticks(harness, 15)]
		),
		"status=%d calls=%s" % [status, str(world.calls)]
	)
	_check(
		"the_tick_conversion_is_not_an_identity",
		_ticks(harness, 15) != 15,
		"ticks=%d" % _ticks(harness, 15)
	)

	# THE SENTINEL COLLISION. orders.guard reads duration_ticks <= 0 as
	# "indefinitely", so an authored 0-second guard cannot be expressed:
	# serving it would turn a no-op into a forever-guard. It must refuse, and
	# the refusal must never reach the world.
	world.calls.clear()
	var zero := _act(harness, "TEAM_GUARD_FOR_SECONDS", [
		_name_arg("Patrol"), _int_arg(0)
	])
	_check(
		"a_zero_second_guard_refuses_rather_than_guarding_forever",
		zero == Dispatch.Status.WORLD_REFUSED,
		"status=%d" % zero
	)
	_check(
		"the_refused_zero_duration_does_not_reach_the_world_at_all",
		world.calls.is_empty(),
		str(world.calls)
	)
	_check(
		"the_refused_zero_duration_is_recorded_as_a_gap",
		(harness["dispatch"] as SageScriptDispatch).gaps.has(
			"action", "TEAM_GUARD_FOR_SECONDS", GapLog.REASON_WORLD_REFUSED
		),
		str((harness["dispatch"] as SageScriptDispatch).gaps.to_lines())
	)

	# A negative duration has no defined meaning at all.
	_check(
		"a_negative_guard_duration_is_bad_arguments",
		_act(harness, "TEAM_GUARD_FOR_SECONDS", [
			_name_arg("Patrol"), _int_arg(-5)
		]) == Dispatch.Status.BAD_ARGUMENTS
	)


func _test_idle_for_seconds_converts() -> void:
	var harness := _harness()
	var world: BaseWorld = harness["world"]

	# TEAM_IDLE_FOR_SECONDS(TEAM, INT): same conversion, no sentinel -
	# idle_for_ticks documents no indefinite meaning for 0, so 0 is a plain
	# zero-length idle and is exactly expressible.
	var status := _act(harness, "TEAM_IDLE_FOR_SECONDS", [
		_name_arg("Sequencer Team"), _int_arg(30, "DECOY_NOT_A_DURATION")
	])
	_check(
		"thirty_seconds_arrive_as_ticks_on_the_team_scope",
		status == Dispatch.Status.OK
		and world.calls.has(
			"orders.idle_for_ticks|%d|Sequencer Team|%d"
			% [SageScriptWorld.Scope.TEAM, _ticks(harness, 30)]
		),
		"status=%d calls=%s" % [status, str(world.calls)]
	)

	var zero := _act(harness, "TEAM_IDLE_FOR_SECONDS", [
		_name_arg("Sequencer Team"), _int_arg(0)
	])
	_check(
		"a_zero_second_idle_is_served_as_zero_ticks",
		zero == Dispatch.Status.OK
		and world.calls.has(
			"orders.idle_for_ticks|%d|Sequencer Team|0" % SageScriptWorld.Scope.TEAM
		),
		"status=%d calls=%s" % [zero, str(world.calls)]
	)
	_check(
		"a_negative_idle_duration_is_bad_arguments",
		_act(harness, "TEAM_IDLE_FOR_SECONDS", [
			_name_arg("Sequencer Team"), _int_arg(-1)
		]) == Dispatch.Status.BAD_ARGUMENTS
	)


# --- 5. Reinforcements ----------------------------------------------------


func _test_reinforcement_team_target_and_ownership() -> void:
	var harness := _harness()
	var world: BaseWorld = harness["world"]

	# CREATE_REINFORCEMENT_TEAM_AT_UNIT_POSITION(TEAM, UNIT): the team
	# template first, the anchor object second, delivered as an OBJECT target
	# (the world resolves its position). The player slot is EMPTY by contract:
	# neither vocabulary spelling of the action carries one, so the world must
	# derive ownership from the team template rather than this handler
	# inventing a player the map never authored.
	var status := _act(harness, "CREATE_REINFORCEMENT_TEAM_AT_UNIT_POSITION", [
		_name_arg("Synthetic Response Team"), _name_arg("SYNTH_ANCHOR_BASE")
	])
	_check(
		"the_reinforcement_is_served",
		status == Dispatch.Status.OK,
		"status=%d" % status
	)
	_check(
		"the_team_template_anchors_on_an_object_target_with_no_invented_player",
		world.calls.has(
			"ai.create_reinforcement_team||Synthetic Response Team|object:SYNTH_ANCHOR_BASE"
		),
		str(world.calls)
	)


# --- 6. Base unpacking -----------------------------------------------------


func _test_base_unpack_pair_serves_flag_then_reference() -> void:
	var harness := _harness()
	var world: BaseWorld = harness["world"]

	# NAMED_BASE_UNPACK(UNIT, UNIT_REF): the base flag FIRST, the DESTINATION
	# reference LAST, and the free flag FALSE - it comes from the opcode. The
	# reference must arrive INTACT: it is the load-bearing argument the pair
	# was gap-registered over until the world's signature grew its slot.
	var paid := _act(harness, "NAMED_BASE_UNPACK", [
		_name_arg("SYNTH_BASE_SITE"), _name_arg("SYNTH_EXPANSION_REF")
	])
	_check("the_paid_unpack_is_served", paid == Dispatch.Status.OK, "status=%d" % paid)
	_check(
		"the_flag_reference_and_paid_flag_arrive_in_order",
		world.calls.has("ai.base_unpack|SYNTH_BASE_SITE|false|SYNTH_EXPANSION_REF"),
		str(world.calls)
	)

	# The FREE spelling folds onto the same world method with free=true and
	# nothing else different - a fold that came from the opcode, so a map
	# cannot author a paid unpack into a free one.
	world.calls.clear()
	var free := _act(harness, "NAMED_BASE_UNPACK_FREE", [
		_name_arg("SYNTH_BASE_SITE"), _name_arg("SYNTH_HOME_REF")
	])
	_check("the_free_unpack_is_served", free == Dispatch.Status.OK, "status=%d" % free)
	_check(
		"the_free_spelling_arrives_with_free_true",
		world.calls.has("ai.base_unpack|SYNTH_BASE_SITE|true|SYNTH_HOME_REF"),
		str(world.calls)
	)

	# A world refusal (unknown flag, no script player, ...) is a structured
	# gap naming the world method - never a quiet success.
	world.calls.clear()
	world.refuse_unpacks = true
	var refused := _act(harness, "NAMED_BASE_UNPACK", [
		_name_arg("SYNTH_NOWHERE_SITE"), _name_arg("SYNTH_REF")
	])
	_check(
		"a_world_refused_unpack_is_reported_as_a_gap",
		refused == Dispatch.Status.WORLD_REFUSED
		and (harness["dispatch"] as SageScriptDispatch).gaps.has(
			"action", "NAMED_BASE_UNPACK", GapLog.REASON_WORLD_REFUSED
		),
		"status=%d gaps=%s" % [refused, str((harness["dispatch"] as SageScriptDispatch).gaps.to_lines())]
	)


# --- 7. Command refusal, arity, and argument coding -----------------------


func _test_command_refusal_is_reported() -> void:
	# A world that models neither orders nor skirmish AI must produce a
	# structured gap per action, never a quiet success. The BASE
	# SageScriptWorld is exactly that world.
	var dispatch := Dispatch.new()
	CoreHandlers.register_all(dispatch)
	Registry.register_all(dispatch)
	var env := Env.new()
	var bare := SageScriptWorld.new()

	var status := dispatch.execute_action(
		_record("TEAM_GUARD_TEAM", [_name_arg("Screen"), _name_arg("Column")]),
		env, bare, "fixture"
	)
	_check(
		"a_world_without_an_orders_facet_refuses_rather_than_pretending",
		status == Dispatch.Status.WORLD_REFUSED,
		"status=%d" % status
	)
	_check(
		"the_guard_refusal_names_the_missing_facet_method",
		dispatch.gaps.has("action", "TEAM_GUARD_TEAM", GapLog.REASON_WORLD_REFUSED)
		and String(
			dispatch.gaps.entries[
				"action|TEAM_GUARD_TEAM|%s" % GapLog.REASON_WORLD_REFUSED
			]["detail"]
		).contains("orders.guard"),
		str(dispatch.gaps.to_lines())
	)

	var reinforcement := dispatch.execute_action(
		_record("CREATE_REINFORCEMENT_TEAM_AT_UNIT_POSITION", [
			_name_arg("Synthetic Response Team"), _name_arg("SYNTH_ANCHOR_BASE")
		]),
		env, bare, "fixture"
	)
	_check(
		"a_world_without_an_ai_facet_refuses_the_reinforcement",
		reinforcement == Dispatch.Status.WORLD_REFUSED
		and dispatch.gaps.has(
			"action", "CREATE_REINFORCEMENT_TEAM_AT_UNIT_POSITION",
			GapLog.REASON_WORLD_REFUSED
		),
		"status=%d gaps=%s" % [reinforcement, str(dispatch.gaps.to_lines())]
	)


func _test_arity_is_enforced() -> void:
	# Arity is checked against the sourced signature before the handler runs,
	# so a positional read can never quietly slide onto a neighbouring
	# argument. One short and one long case per served member.
	var harness := _harness()
	_check(
		"a_short_guard_team_argument_list_is_rejected",
		_act(harness, "TEAM_GUARD_TEAM", [_name_arg("Only One")])
		== Dispatch.Status.BAD_ARGUMENTS
	)
	_check(
		"a_long_guard_team_argument_list_is_rejected",
		_act(harness, "TEAM_GUARD_TEAM", [
			_name_arg("A"), _name_arg("B"), _name_arg("Surplus")
		]) == Dispatch.Status.BAD_ARGUMENTS
	)
	_check(
		"a_short_guard_for_seconds_argument_list_is_rejected",
		_act(harness, "TEAM_GUARD_FOR_SECONDS", [_name_arg("Patrol")])
		== Dispatch.Status.BAD_ARGUMENTS
	)
	_check(
		"a_long_idle_for_seconds_argument_list_is_rejected",
		_act(harness, "TEAM_IDLE_FOR_SECONDS", [
			_name_arg("Patrol"), _int_arg(5), _int_arg(6)
		]) == Dispatch.Status.BAD_ARGUMENTS
	)
	_check(
		"a_short_reinforcement_argument_list_is_rejected",
		_act(harness, "CREATE_REINFORCEMENT_TEAM_AT_UNIT_POSITION", [
			_name_arg("Synthetic Response Team")
		]) == Dispatch.Status.BAD_ARGUMENTS
	)
	_check(
		"a_short_unpack_argument_list_is_rejected",
		_act(harness, "NAMED_BASE_UNPACK", [_name_arg("SYNTH_BASE_SITE")])
		== Dispatch.Status.BAD_ARGUMENTS
	)
	_check(
		"a_long_free_unpack_argument_list_is_rejected",
		_act(harness, "NAMED_BASE_UNPACK_FREE", [
			_name_arg("SYNTH_BASE_SITE"), _name_arg("SYNTH_REF"), _name_arg("Surplus")
		]) == Dispatch.Status.BAD_ARGUMENTS
	)
	_check(
		"a_short_condition_argument_list_is_rejected_and_the_gate_stays_shut",
		not _cond(harness, "NAMED_BASE_UNPACKABLE_FOR_PLAYER", [
			_name_arg("SYNTH_SITE_A")
		])
		and (harness["dispatch"] as SageScriptDispatch).gaps.has(
			"condition", "NAMED_BASE_UNPACKABLE_FOR_PLAYER", GapLog.REASON_BAD_ARGUMENTS
		),
		str((harness["dispatch"] as SageScriptDispatch).gaps.to_lines())
	)


func _test_argument_coding_is_enforced_where_observed() -> void:
	# SageScriptArgs asserts argument-type CODES only where this repo has
	# observed one (INT and PLAYER here). For those slots a mis-coded record
	# is rejected before the handler runs. TEAM/UNIT/UNIT_REF have no observed
	# code, so validation deliberately declines to assert on them - which is
	# exactly why every handler read is positional and every fixture carries a
	# decoy in the unread field.
	var harness := _harness()
	var world: BaseWorld = harness["world"]

	# The INT slot of the two duration actions, mis-coded as a generic name
	# argument (code 99, expected ARGUMENT_INTEGER).
	_check(
		"a_mis_coded_guard_duration_is_rejected_before_dispatch",
		_act(harness, "TEAM_GUARD_FOR_SECONDS", [
			_name_arg("Patrol"), _name_arg("NOT_AN_INT")
		]) == Dispatch.Status.BAD_ARGUMENTS
	)
	_check(
		"a_mis_coded_idle_duration_is_rejected_before_dispatch",
		_act(harness, "TEAM_IDLE_FOR_SECONDS", [
			_name_arg("Patrol"), _name_arg("NOT_AN_INT")
		]) == Dispatch.Status.BAD_ARGUMENTS
	)

	# The PLAYER slot of the condition, mis-coded the same way.
	world.unpackable["SYNTH_SITE_A|SYNTH_PLAYER_1"] = true
	_check(
		"a_mis_coded_player_argument_is_rejected_before_dispatch",
		not _cond(harness, "NAMED_BASE_UNPACKABLE_FOR_PLAYER", [
			_name_arg("SYNTH_SITE_A"), _name_arg("SYNTH_PLAYER_1")
		])
		and (harness["dispatch"] as SageScriptDispatch).gaps.has(
			"condition", "NAMED_BASE_UNPACKABLE_FOR_PLAYER", GapLog.REASON_BAD_ARGUMENTS
		),
		str((harness["dispatch"] as SageScriptDispatch).gaps.to_lines())
	)

	# TEAM slots carry no observed code, so a foreign code passes validation
	# BY DESIGN and the handler must still read the TEXT field - the decoy
	# integer in the same slot is what a wrong-field read would deliver.
	var status := _act(harness, "TEAM_GUARD_TEAM", [
		_argument(ParamTypes.ARGUMENT_COUNTER_NAME, 424242, 0.0, "Screen Detachment"),
		_name_arg("Siege Train"),
	])
	_check(
		"an_unobserved_team_code_is_accepted_and_still_read_as_text",
		status == Dispatch.Status.OK
		and world.calls.has(
			"orders.guard|%d|Screen Detachment|team:Siege Train|0"
			% SageScriptWorld.Scope.TEAM
		),
		"status=%d calls=%s" % [status, str(world.calls)]
	)


# --- 8. Gap-registered members --------------------------------------------


func _test_gap_registered_members() -> void:
	var harness := _harness()
	var dispatch: SageScriptDispatch = harness["dispatch"]
	var world: BaseWorld = harness["world"]

	# The tactical-marker build names every argument the world surface lacks.
	# It must report BLOCKED and must not reach the world at all.
	var marker_status := _act(harness, "BUILD_BASE_BUILDING_PER_TACTICAL_MARKER", [
		_name_arg("SyntheticFarmType"),
		_argument(ParamTypes.ARGUMENT_INTEGER, 1),
		_name_arg("SyntheticMarkerType"),
		_name_arg("SYNTH_SITE_REF"),
		_name_arg("SYNTH_BUILT_REF"),
	])
	_check(
		"the_marker_build_reports_blocked",
		marker_status == Dispatch.Status.BLOCKED,
		"status=%d" % marker_status
	)
	_check(
		"the_marker_build_never_reaches_the_world",
		world.calls.is_empty(),
		str(world.calls)
	)
	var marker_detail := _gap_detail(dispatch, "BUILD_BASE_BUILDING_PER_TACTICAL_MARKER")
	_check(
		"the_marker_gap_names_the_needed_signature_including_the_side_bit",
		marker_detail.contains("ai.build_base_building_per_tactical_marker")
		and marker_detail.contains("near_or_far: int")
		and marker_detail.contains("result_reference"),
		str(dispatch.gaps.to_lines())
	)
	_check(
		"the_gap_registered_member_recorded_a_blocked_gap",
		dispatch.gaps.has(
			"action", "BUILD_BASE_BUILDING_PER_TACTICAL_MARKER",
			GapLog.REASON_BLOCKED_SUBSYSTEM
		),
		str(dispatch.gaps.to_lines())
	)


# --- Stub world -----------------------------------------------------------
#
# SageScriptWorldStub teaches only the Players facet, and this package touches
# Ai and Orders. Those facets are taught HERE rather than in the shared stub,
# because the shared stub is an existing file that ~17 concurrent packages
# also depend on. Every command is recorded as a flat string - including the
# tagged target dictionary, flattened as kind:name - so a test asserts on the
# exact arguments that reached the world; every read answers from a fixture or
# REFUSES, so a test that forgot to set one gets a loud refusal rather than a
# plausible false.


class BaseWorld:
	extends SageScriptWorldStub

	var calls: Array[String] = []
	var reads: Array[String] = []

	## "unit|player" -> bool. Keyed on the pair IN SIGNATURE ORDER so that a
	## handler that swapped the arguments misses the fixture and refuses.
	var unpackable: Dictionary = {}

	## When true the stub's ai.base_unpack refuses, exercising the handler's
	## world-refusal path.
	var refuse_unpacks := false

	func _make_ai() -> SageScriptWorld.Ai:
		return StubAi.new()

	func _make_orders() -> SageScriptWorld.Orders:
		return StubOrders.new()

	## Flattens a target_*() dictionary for call-log assertions. Only the kinds
	## this package emits are given names; anything else is its raw kind
	## number, which no assertion matches - so an unexpected target FAILS
	## instead of aliasing onto an expected one. Lives on the world (not the
	## runner) because GDScript inner classes cannot see outer-script statics.
	func target_label(target: Dictionary) -> String:
		var kind := int(target.get("kind", -1))
		match kind:
			SageScriptWorld.TargetKind.TEAM:
				return "team:%s" % String(target.get("name", ""))
			SageScriptWorld.TargetKind.OBJECT:
				return "object:%s" % String(target.get("name", ""))
			SageScriptWorld.TargetKind.SELF:
				return "self"
		return "kind-%d" % kind


class StubAi:
	extends SageScriptWorld.Ai

	func base_unpackable(object_name: String, player: String) -> SageWorldQuery:
		var stub := world as BaseWorld
		var key := "%s|%s" % [object_name, player]
		stub.reads.append("ai.base_unpackable|%s" % key)
		if not stub.unpackable.has(key):
			return _refuse_query(
				"ai.base_unpackable", "no fixture for '%s'" % key
			)
		return SageWorldQuery.hit(bool(stub.unpackable[key]))

	func base_unpack(object_name: String, free: bool, result_reference: String) -> bool:
		# Records the FULL argument tuple - the flag, the free/paid fold and
		# the UNIT_REF destination - so the served pair is asserted on exactly
		# what reached the world, in order.
		var stub := world as BaseWorld
		if stub.refuse_unpacks:
			return _refuse_command("ai.base_unpack", "fixture refuses unpacks")
		stub.calls.append(
			"ai.base_unpack|%s|%s|%s" % [object_name, str(free).to_lower(), result_reference]
		)
		return true

	func create_reinforcement_team(
		player: String, team: String, target: Dictionary
	) -> bool:
		var stub := world as BaseWorld
		stub.calls.append(
			"ai.create_reinforcement_team|%s|%s|%s" % [
				player, team, stub.target_label(target)
			]
		)
		return true


class StubOrders:
	extends SageScriptWorld.Orders

	func guard(scope: int, name: String, target: Dictionary, duration_ticks: int) -> bool:
		var stub := world as BaseWorld
		stub.calls.append(
			"orders.guard|%d|%s|%s|%d" % [
				scope, name, stub.target_label(target), duration_ticks
			]
		)
		return true

	func idle_for_ticks(scope: int, name: String, ticks: int) -> bool:
		(world as BaseWorld).calls.append(
			"orders.idle_for_ticks|%d|%s|%d" % [scope, name, ticks]
		)
		return true


# --- Reporting ------------------------------------------------------------


func _gap_detail(dispatch: SageScriptDispatch, name: String) -> String:
	## The blocked-subsystem gap detail recorded for `name`, or "" if no such
	## gap exists. Returns a String rather than indexing the entry dictionary
	## directly so that a MISSING gap fails the assertion that wanted it,
	## instead of throwing and taking the rest of the run down with it.
	var key := "action|%s|%s" % [name, GapLog.REASON_BLOCKED_SUBSYSTEM]
	if not dispatch.gaps.entries.has(key):
		return ""
	return String((dispatch.gaps.entries[key] as Dictionary)["detail"])


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("HANDLERS_WP11 PASS %s%s" % [name, " (%s)" % detail if detail != "" else ""])
	else:
		failed += 1
		printerr("HANDLERS_WP11 FAIL %s%s" % [name, " (%s)" % detail if detail != "" else ""])
