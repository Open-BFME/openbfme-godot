extends SceneTree

## Proof runner for WP11-basebuilding-ai-tail (wp11_ai_basebuilding_tail.gd) -
## one member, gap-registered, and the proof that gap-registering it is
## correct.
##
## Covers, in order:
##   1. registration - the tail package coexists with the WP11 subset package
##                     with no errors, no name overlap, and the catalog
##                     package name still free; BUILD_BASE_BUILDING is blocked
##                     and never counted as coverage
##   2. the gap      - a call reports BLOCKED, never reaches the world (the
##                     stub's ai.build_base_building exists precisely so a
##                     serving handler would be CAUGHT), and the recorded gap
##                     names the exact missing world signature
##   3. arity        - validation runs BEFORE the blocked handler, so a
##                     wrong-arity record is BAD_ARGUMENTS, not BLOCKED
##
## Every fixture value is SYNTHETIC. The call SHAPE is modelled on the retail
## AI libraries (a base anchor, then a destination reference) because that
## shape is what the gap classification rests on, but no retail payload text
## is reproduced here.
##
## Invocation:
##   Godot_v4.7-stable_win64_console.exe --headless --path game \
##     -s res://tests/handlers_wp11_ai_basebuilding_tail_runner.gd

const Registry := preload("res://src/script/handlers/_registry.gd")
const Dispatch := preload("res://src/script/script_dispatch.gd")
const CoreHandlers := preload("res://src/script/script_handlers_core.gd")
const Env := preload("res://src/script/script_env.gd")
const GapLog := preload("res://src/script/script_gaps.gd")
const Tail := preload("res://src/script/handlers/wp11_ai_basebuilding_tail.gd")

const GAP_REGISTERED := [
	"BUILD_BASE_BUILDING",
]

var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	_test_registration_coexists_with_the_wp11_subset()
	_test_the_gap_registered_member_blocks_and_names_the_missing_signature()
	_test_arity_is_enforced_before_the_block()
	print("HANDLERS_WP11_TAIL_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


# --- Fixture helpers ------------------------------------------------------


func _argument(
	argument_type: int, integer: int = 0, real: float = 0.0, text: String = ""
) -> Dictionary:
	return {"argumentType": argument_type, "integer": integer, "real": real, "text": text}


func _name_arg(value: String) -> Dictionary:
	# OBJECT_TYPE, UNIT and UNIT_REF have no observed wire code in this repo,
	# so SageScriptArgs does not assert one. The integer field carries a DECOY:
	# a handler that read the wrong field would get 424242 instead of the name,
	# loudly rather than plausibly.
	return _argument(99, 424242, 0.0, value)


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
		"world": TailWorld.new(20260726),
		"registration": outcome,
	}


func _act(harness: Dictionary, name: String, arguments: Array) -> int:
	return (harness["dispatch"] as SageScriptDispatch).execute_action(
		_record(name, arguments), harness["env"], harness["world"], "fixture"
	)


# --- 1. Registration ------------------------------------------------------


func _test_registration_coexists_with_the_wp11_subset() -> void:
	var harness := _harness()
	var dispatch: SageScriptDispatch = harness["dispatch"]
	var outcome: Dictionary = harness["registration"]

	# No errors is the load-bearing check here: the tail file shares the
	# handlers directory with wp11_ai_basebuilding.gd, and any name or PACKAGE
	# overlap between them would surface exactly here, loudly.
	_check(
		"registration_reports_no_errors",
		(outcome["errors"] as Array).is_empty(),
		str(outcome["errors"])
	)
	_check(
		"the_tail_package_and_the_wp11_subset_package_are_both_present",
		(outcome["packages"] as Array).has("WP11-basebuilding-ai-tail")
		and (outcome["packages"] as Array).has("WP11-basebuilding-ai"),
		str(outcome["packages"])
	)
	_check(
		"the_catalog_package_name_remains_free_for_the_completing_package",
		not (outcome["packages"] as Array).has("WP11-ai-basebuilding"),
		str(outcome["packages"])
	)

	var unblocked: Array[String] = []
	for name: String in GAP_REGISTERED:
		if not dispatch.blocked_names().has(name):
			unblocked.append(name)
	_check("the_gap_registered_member_is_blocked", unblocked.is_empty(), str(unblocked))

	# With this file, all nine AI-called WP11 members are accounted for: the
	# sibling's five served and three gapped, plus this ninth gap
	# (game/data/retail_ai_call_census.json).
	var wp11_blocked: Array[String] = []
	for name: String in [
		"BUILD_BASE_BUILDING",
		"BUILD_BASE_BUILDING_PER_TACTICAL_MARKER",
		"NAMED_BASE_UNPACK",
		"NAMED_BASE_UNPACK_FREE",
	]:
		if not dispatch.blocked_names().has(name):
			wp11_blocked.append(name)
	_check(
		"all_four_wp11_base_building_gaps_are_now_on_the_record",
		wp11_blocked.is_empty(),
		str(wp11_blocked)
	)

	_check(
		"the_gap_registered_member_is_not_counted_as_coverage",
		not dispatch.implemented_actions().has("BUILD_BASE_BUILDING"),
		str(dispatch.implemented_actions())
	)

	# The gap list this file asserts on must be the one the module actually
	# declares, or the two could drift apart while both look right.
	var declared: Array = Tail.GAP_BUILD_ACTIONS.duplicate()
	declared.sort()
	_check(
		"the_modules_gap_list_matches_this_runners",
		declared == GAP_REGISTERED,
		"module=%s runner=%s" % [str(declared), str(GAP_REGISTERED)]
	)


# --- 2. The gap -----------------------------------------------------------


func _test_the_gap_registered_member_blocks_and_names_the_missing_signature() -> void:
	var harness := _harness()
	var dispatch: SageScriptDispatch = harness["dispatch"]
	var world: TailWorld = harness["world"]

	# BUILD_BASE_BUILDING(OBJECT_TYPE, UNIT, UNIT_REF) carries a base anchor
	# and a result reference the world cannot accept. It must report BLOCKED
	# and must not reach the world at all - serving it through
	# ai.build_base_building(player, building_type, slot, marker) would invent
	# a player, drop the base, and leave the reference unbound for every later
	# script that reads it.
	var status := _act(harness, "BUILD_BASE_BUILDING", [
		_name_arg("SyntheticBarracksType"),
		_name_arg("SYNTH_SITE_REF"),
		_name_arg("SYNTH_BUILT_REF"),
	])
	_check(
		"the_gap_registered_action_reports_blocked",
		status == Dispatch.Status.BLOCKED,
		"status=%d" % status
	)
	_check(
		"the_gap_registered_action_never_reaches_the_world",
		world.calls.is_empty(),
		str(world.calls)
	)
	_check(
		"the_gap_names_the_needed_world_signature",
		_gap_detail(dispatch, "BUILD_BASE_BUILDING").contains(
			"ai.build_base_building(building_type: String, base: String, "
			+ "result_reference: String)"
		),
		str(dispatch.gaps.to_lines())
	)
	_check(
		"the_gap_names_both_missing_halves",
		_gap_detail(dispatch, "BUILD_BASE_BUILDING").contains("base")
		and _gap_detail(dispatch, "BUILD_BASE_BUILDING").contains("result reference"),
		str(dispatch.gaps.to_lines())
	)
	_check(
		"the_block_is_recorded_under_the_blocked_subsystem_reason",
		dispatch.gaps.has(
			"action", "BUILD_BASE_BUILDING", GapLog.REASON_BLOCKED_SUBSYSTEM
		),
		str(dispatch.gaps.to_lines())
	)


# --- 3. Arity precedes the block ------------------------------------------


func _test_arity_is_enforced_before_the_block() -> void:
	# Argument validation runs before ANY handler, the shared blocked handler
	# included: a wrong-arity record must be reported as its own defect
	# (BAD_ARGUMENTS), not folded into the subsystem gap.
	var harness := _harness()
	_check(
		"a_short_build_argument_list_is_bad_arguments_not_blocked",
		_act(harness, "BUILD_BASE_BUILDING", [
			_name_arg("SyntheticBarracksType"), _name_arg("SYNTH_SITE_REF")
		]) == Dispatch.Status.BAD_ARGUMENTS
	)
	_check(
		"a_long_build_argument_list_is_bad_arguments_not_blocked",
		_act(harness, "BUILD_BASE_BUILDING", [
			_name_arg("A"), _name_arg("B"), _name_arg("C"), _name_arg("Surplus")
		]) == Dispatch.Status.BAD_ARGUMENTS
	)


# --- Stub world -----------------------------------------------------------
#
# The Ai facet is taught ONE method here - and it is a trap, not a fixture:
# ai.build_base_building exists in this stub precisely so that a handler
# serving the gap-registered action would be CAUGHT (the call would appear in
# the log) rather than refused into a false pass.


class TailWorld:
	extends SageScriptWorldStub

	var calls: Array[String] = []

	func _make_ai() -> SageScriptWorld.Ai:
		return StubAi.new()


class StubAi:
	extends SageScriptWorld.Ai

	func build_base_building(
		player: String, building_type: String, slot: int, marker: String
	) -> bool:
		(world as TailWorld).calls.append(
			"ai.build_base_building|%s|%s|%d|%s" % [player, building_type, slot, marker]
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
		print(
			"HANDLERS_WP11_TAIL PASS %s%s"
			% [name, " (%s)" % detail if detail != "" else ""]
		)
	else:
		failed += 1
		printerr(
			"HANDLERS_WP11_TAIL FAIL %s%s"
			% [name, " (%s)" % detail if detail != "" else ""]
		)
