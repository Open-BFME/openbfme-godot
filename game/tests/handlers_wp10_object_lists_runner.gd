extends SceneTree

## Proof runner for WP10-object-lists (wp10_object_lists.gd): the
## OBJECTLIST_ADDOBJECTTYPE / OBJECTLIST_REMOVEOBJECTTYPE pair.
##
## Covers, in order:
##   1. registration    - both members registered, nothing blocked, the
##                        catalog package name left free
##   2. positional args - two all-text signatures where nothing but position
##                        separates the LIST from the TYPE (a swapped read
##                        would mint a list named after the type)
##   3. the opcode fold - add-vs-remove comes from the OPCODE, never from an
##                        argument, so the spellings cannot cross
##   4. refusal + arity - a world without the list store refuses per action
##                        with a structured gap; wrong arity is rejected
##                        before any handler runs
##
## Every fixture value is SYNTHETIC. The call SHAPES mirror lib_object_lists
## (a list name, a retail type name) because those shapes are what the
## handlers must survive; no retail payload text is reproduced.
##
## Invocation:
##   Godot_v4.7-stable_win64_console.exe --headless --path game \
##     -s res://tests/handlers_wp10_object_lists_runner.gd

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
	_test_registration_covers_the_pair()
	_test_add_reads_list_then_type_and_folds_add_from_the_opcode()
	_test_remove_folds_remove_from_the_opcode()
	_test_world_refusal_is_reported()
	_test_arity_is_enforced()
	print("HANDLERS_WP10_OBJECT_LISTS_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


# --- Fixture helpers ------------------------------------------------------


func _name_arg(value: String) -> Dictionary:
	# OBJECT_TYPE_LIST and OBJECT_TYPE have no observed wire code in this repo,
	# so SageScriptArgs does not assert one. The integer field carries a DECOY:
	# a handler that read the wrong field would get 424242, loudly rather than
	# plausibly.
	return {"argumentType": 99, "integer": 424242, "real": 0.0, "text": value}


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
		"world": ListWorld.new(20260727),
		"registration": outcome,
	}


func _act(harness: Dictionary, name: String, arguments: Array) -> int:
	return (harness["dispatch"] as SageScriptDispatch).execute_action(
		_record(name, arguments), harness["env"], harness["world"], "fixture"
	)


# --- 1. Registration ------------------------------------------------------


func _test_registration_covers_the_pair() -> void:
	var harness := _harness()
	var dispatch: SageScriptDispatch = harness["dispatch"]
	var outcome: Dictionary = harness["registration"]

	_check(
		"registration_reports_no_errors",
		(outcome["errors"] as Array).is_empty(),
		str(outcome["errors"])
	)
	_check(
		"the_wp10_object_lists_package_is_present",
		(outcome["packages"] as Array).has("WP10-object-lists"),
		str(outcome["packages"])
	)
	_check(
		"the_catalog_package_name_remains_free_for_the_completing_package",
		not (outcome["packages"] as Array).has("WP10-economy-objectstatus-objectlists"),
		str(outcome["packages"])
	)
	_check(
		"both_members_are_registered_and_neither_is_blocked",
		dispatch.implemented_actions().has("OBJECTLIST_ADDOBJECTTYPE")
		and dispatch.implemented_actions().has("OBJECTLIST_REMOVEOBJECTTYPE")
		and not dispatch.blocked_names().has("OBJECTLIST_ADDOBJECTTYPE")
		and not dispatch.blocked_names().has("OBJECTLIST_REMOVEOBJECTTYPE")
	)


# --- 2/3. Positional reads and the opcode fold ----------------------------


func _test_add_reads_list_then_type_and_folds_add_from_the_opcode() -> void:
	var harness := _harness()
	var world: ListWorld = harness["world"]

	# OBJECTLIST_ADDOBJECTTYPE(OBJECT_TYPE_LIST, OBJECT_TYPE): list FIRST,
	# type SECOND - both text, so only position separates them.
	var status := _act(harness, "OBJECTLIST_ADDOBJECTTYPE", [
		_name_arg("SYNTH_OFFENSE_LIST"), _name_arg("SynthFighterHorde")
	])
	_check("the_add_is_served", status == Dispatch.Status.OK, "status=%d" % status)
	_check(
		"list_then_type_reach_meta_object_list_change_with_add_true",
		world.calls == ["meta.object_list_change|SYNTH_OFFENSE_LIST|SynthFighterHorde|add"],
		str(world.calls)
	)


func _test_remove_folds_remove_from_the_opcode() -> void:
	var harness := _harness()
	var world: ListWorld = harness["world"]

	var status := _act(harness, "OBJECTLIST_REMOVEOBJECTTYPE", [
		_name_arg("SYNTH_OFFENSE_LIST"), _name_arg("SynthFighterHorde")
	])
	_check("the_remove_is_served", status == Dispatch.Status.OK, "status=%d" % status)
	_check(
		"the_remove_spelling_arrives_with_add_false",
		world.calls == ["meta.object_list_change|SYNTH_OFFENSE_LIST|SynthFighterHorde|remove"],
		str(world.calls)
	)


# --- 4. Refusal and arity -------------------------------------------------


func _test_world_refusal_is_reported() -> void:
	# A world without the list store must produce a structured gap, never a
	# quiet success. The BASE SageScriptWorld is exactly that world.
	var dispatch := Dispatch.new()
	CoreHandlers.register_all(dispatch)
	Registry.register_all(dispatch)
	var env := Env.new()
	var bare := SageScriptWorld.new()

	var status := dispatch.execute_action(
		_record("OBJECTLIST_ADDOBJECTTYPE", [
			_name_arg("SYNTH_OFFENSE_LIST"), _name_arg("SynthFighterHorde")
		]),
		env, bare, "fixture"
	)
	_check(
		"a_world_without_the_list_store_refuses",
		status == Dispatch.Status.WORLD_REFUSED,
		"status=%d" % status
	)
	_check(
		"the_refusal_names_the_missing_facet_method",
		dispatch.gaps.has("action", "OBJECTLIST_ADDOBJECTTYPE", GapLog.REASON_WORLD_REFUSED)
		and String(
			dispatch.gaps.entries[
				"action|OBJECTLIST_ADDOBJECTTYPE|%s" % GapLog.REASON_WORLD_REFUSED
			]["detail"]
		).contains("meta.object_list_change"),
		str(dispatch.gaps.to_lines())
	)


func _test_arity_is_enforced() -> void:
	var harness := _harness()
	_check(
		"a_short_add_argument_list_is_rejected",
		_act(harness, "OBJECTLIST_ADDOBJECTTYPE", [_name_arg("Only List")])
		== Dispatch.Status.BAD_ARGUMENTS
	)
	_check(
		"a_long_add_argument_list_is_rejected",
		_act(harness, "OBJECTLIST_ADDOBJECTTYPE", [
			_name_arg("A"), _name_arg("B"), _name_arg("Surplus")
		]) == Dispatch.Status.BAD_ARGUMENTS
	)
	_check(
		"a_short_remove_argument_list_is_rejected",
		_act(harness, "OBJECTLIST_REMOVEOBJECTTYPE", [_name_arg("Only List")])
		== Dispatch.Status.BAD_ARGUMENTS
	)


# --- Stub world -----------------------------------------------------------
#
# SageScriptWorldStub does not teach the Meta list surface, and the shared
# stub is an existing file ~17 concurrent packages depend on - so the facet
# is taught HERE. Every call is recorded flat (method|list|type|add-or-remove)
# so a test asserts on the exact arguments that reached the world.


class ListWorld:
	extends SageScriptWorldStub

	var calls: Array[String] = []

	func _make_meta() -> SageScriptWorld.Meta:
		return StubMeta.new()


class StubMeta:
	extends SageScriptWorld.Meta

	func object_list_change(list_name: String, object_type: String, add: bool) -> bool:
		(world as ListWorld).calls.append(
			"meta.object_list_change|%s|%s|%s" % [list_name, object_type, "add" if add else "remove"]
		)
		return true


# --- Reporting ------------------------------------------------------------


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("HANDLERS_WP10_OBJECT_LISTS PASS %s%s" % [name, " (%s)" % detail if detail != "" else ""])
	else:
		failed += 1
		printerr("HANDLERS_WP10_OBJECT_LISTS FAIL %s%s" % [name, " (%s)" % detail if detail != "" else ""])
