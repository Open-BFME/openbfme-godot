extends SceneTree

## Proof runner for WP11-basebuilding-ai-tail (wp11_ai_basebuilding_tail.gd) -
## one member, SERVED through the corrected facet signature.
##
## Covers, in order:
##   1. registration - the tail package coexists with the WP11 subset package
##                     with no errors, no name overlap, and the catalog
##                     package name still free; BUILD_BASE_BUILDING is served
##                     and counted as coverage
##   2. the service  - the three text arguments reach ai.build_base_building
##                     positionally (building type, then the BASE object, then
##                     the UNIT_REF destination - all text-carried, so only
##                     position separates them), with NO invented player; a
##                     world refusal is a structured gap naming the method
##   3. arity        - wrong-arity records are BAD_ARGUMENTS before any
##                     handler runs
##
## Every fixture value is SYNTHETIC. The call SHAPE is modelled on the retail
## AI libraries (a base anchor, then a destination reference) because that
## shape is what the positional reads rest on, but no retail payload text is
## reproduced here.
##
## Invocation:
##   Godot_v4.7-stable_win64_console.exe --headless --path game \
##     -s res://tests/handlers_wp11_ai_basebuilding_tail_runner.gd

const Registry := preload("res://src/script/handlers/_registry.gd")
const Dispatch := preload("res://src/script/script_dispatch.gd")
const CoreHandlers := preload("res://src/script/script_handlers_core.gd")
const Env := preload("res://src/script/script_env.gd")
const GapLog := preload("res://src/script/script_gaps.gd")

var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	_test_registration_coexists_with_the_wp11_subset()
	_test_the_member_serves_type_base_reference_in_order()
	_test_a_world_refusal_is_a_structured_gap()
	_test_arity_is_enforced()
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
	_check(
		"the_member_is_served_and_counted_as_coverage",
		dispatch.implemented_actions().has("BUILD_BASE_BUILDING"),
		str(dispatch.implemented_actions())
	)
	_check(
		"the_member_is_no_longer_on_the_blocked_list",
		not dispatch.blocked_names().has("BUILD_BASE_BUILDING"),
		str(dispatch.blocked_names())
	)

	# With this file, all nine AI-called WP11 members are accounted for: the
	# sibling's seven served plus its one gap, plus this ninth member served
	# (game/data/retail_ai_call_census.json). Only the tactical-marker build
	# remains blocked.
	_check(
		"only_the_tactical_marker_build_remains_blocked_in_wp11",
		dispatch.blocked_names().has("BUILD_BASE_BUILDING_PER_TACTICAL_MARKER")
		and not dispatch.blocked_names().has("NAMED_BASE_UNPACK")
		and not dispatch.blocked_names().has("NAMED_BASE_UNPACK_FREE"),
		str(dispatch.blocked_names())
	)


# --- 2. The service --------------------------------------------------------


func _test_the_member_serves_type_base_reference_in_order() -> void:
	var harness := _harness()
	var world: TailWorld = harness["world"]

	# BUILD_BASE_BUILDING(OBJECT_TYPE, UNIT, UNIT_REF): the building type
	# FIRST, the base object SECOND, the destination reference LAST. All
	# three are text-carried, so only position separates them - a rotated
	# read would try to build a base named after a building type - and the
	# stub records the exact tuple that arrived. NO player argument exists
	# anywhere in this chain: the world acts as its configured script player,
	# and the stub's signature would reject an invented one loudly.
	var status := _act(harness, "BUILD_BASE_BUILDING", [
		_name_arg("SyntheticBarracksType"),
		_name_arg("SYNTH_SITE_REF"),
		_name_arg("SYNTH_BUILT_REF"),
	])
	_check("the_build_is_served", status == Dispatch.Status.OK, "status=%d" % status)
	_check(
		"type_base_and_reference_arrive_in_signature_order",
		world.calls.has(
			"ai.build_base_building|SyntheticBarracksType|SYNTH_SITE_REF|SYNTH_BUILT_REF"
		),
		str(world.calls)
	)


func _test_a_world_refusal_is_a_structured_gap() -> void:
	var harness := _harness()
	var world: TailWorld = harness["world"]
	var dispatch: SageScriptDispatch = harness["dispatch"]
	world.refuse_builds = true

	var status := _act(harness, "BUILD_BASE_BUILDING", [
		_name_arg("SyntheticBarracksType"),
		_name_arg("SYNTH_NOWHERE_REF"),
		_name_arg("SYNTH_BUILT_REF"),
	])
	_check(
		"a_world_refused_build_reports_world_refused",
		status == Dispatch.Status.WORLD_REFUSED,
		"status=%d" % status
	)
	_check(
		"the_refusal_is_recorded_as_a_gap_naming_the_world_method",
		dispatch.gaps.has("action", "BUILD_BASE_BUILDING", GapLog.REASON_WORLD_REFUSED)
		and String(
			dispatch.gaps.entries[
				"action|BUILD_BASE_BUILDING|%s" % GapLog.REASON_WORLD_REFUSED
			]["detail"]
		).contains("ai.build_base_building"),
		str(dispatch.gaps.to_lines())
	)


# --- 3. Arity --------------------------------------------------------------


func _test_arity_is_enforced() -> void:
	# Argument validation runs before ANY handler: a wrong-arity record is
	# BAD_ARGUMENTS and never reaches the world.
	var harness := _harness()
	var world: TailWorld = harness["world"]
	_check(
		"a_short_build_argument_list_is_bad_arguments",
		_act(harness, "BUILD_BASE_BUILDING", [
			_name_arg("SyntheticBarracksType"), _name_arg("SYNTH_SITE_REF")
		]) == Dispatch.Status.BAD_ARGUMENTS
	)
	_check(
		"a_long_build_argument_list_is_bad_arguments",
		_act(harness, "BUILD_BASE_BUILDING", [
			_name_arg("A"), _name_arg("B"), _name_arg("C"), _name_arg("Surplus")
		]) == Dispatch.Status.BAD_ARGUMENTS
	)
	_check(
		"neither_bad_arity_record_reached_the_world",
		world.calls.is_empty(),
		str(world.calls)
	)


# --- Stub world -----------------------------------------------------------
#
# The Ai facet is taught ONE method, with the CORRECTED signature: the stub
# records the exact argument tuple so the runner asserts on what reached the
# world, and its refuse_builds switch exercises the structured-gap path.


class TailWorld:
	extends SageScriptWorldStub

	var calls: Array[String] = []
	var refuse_builds := false

	func _make_ai() -> SageScriptWorld.Ai:
		return StubAi.new()


class StubAi:
	extends SageScriptWorld.Ai

	func build_base_building(
		building_type: String, base: String, result_reference: String
	) -> bool:
		var stub := world as TailWorld
		if stub.refuse_builds:
			return _refuse_command("ai.build_base_building", "fixture refuses builds")
		stub.calls.append(
			"ai.build_base_building|%s|%s|%s" % [building_type, base, result_reference]
		)
		return true


# --- Reporting ------------------------------------------------------------


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
