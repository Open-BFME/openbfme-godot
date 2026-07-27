extends SceneTree

## Proof runner for WP14-ai-pathing, the AI-critical subset
## (wp14_ai_pathing.gd).
##
## Covers, in order:
##   1. registration    - both AI-called members registered, nothing blocked,
##                        the catalog package name left free
##   2. positional args - two all-text signatures where nothing but position
##                        separates team, type list and owner
##   3. targets         - both members deliver a NEAREST_TYPE tagged target
##                        through orders.move_to; the unowned spelling carries
##                        the documented empty owner, the owned spelling the
##                        authored player, verbatim
##   4. the sentinel    - an authored EMPTY player on the owned spelling is
##                        inexpressible (empty owner IS the any-owner sentinel)
##                        and refuses without reaching the world
##   5. refusal + arity - a world without an orders facet refuses per method;
##                        wrong arity and wrong-coded arguments are rejected
##                        before any handler runs
##
## Every fixture value is SYNTHETIC. The call SHAPES are modelled on the retail
## AI libraries (a "<This Player>" style token, a type-LIST name) because those
## shapes are what the handlers must survive, but no retail payload text is
## reproduced here.
##
## Invocation:
##   Godot_v4.7-stable_win64_console.exe --headless --path game \
##     -s res://tests/handlers_wp14_ai_pathing_runner.gd

const Registry := preload("res://src/script/handlers/_registry.gd")
const Dispatch := preload("res://src/script/script_dispatch.gd")
const CoreHandlers := preload("res://src/script/script_handlers_core.gd")
const Env := preload("res://src/script/script_env.gd")
const GapLog := preload("res://src/script/script_gaps.gd")
const ParamTypes := preload("res://src/script/script_param_types.gd")

## The 2 members this package serves. Written out rather than derived from the
## module, so that a member quietly disappearing from register() fails here
## instead of agreeing with itself.
const SERVED_ACTIONS := [
	"TEAM_MOVE_TO_NEAREST_OBJECT_OF_TYPE",
	"TEAM_MOVE_TO_NEAREST_OBJECT_OF_TYPE_OWNED_BY_PLAYER",
]

var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	_test_registration_covers_the_ai_subset()
	_test_unowned_move_builds_an_any_owner_nearest_type_target()
	_test_owned_move_carries_the_authored_owner_verbatim()
	_test_an_empty_authored_player_is_inexpressible_and_refuses()
	_test_command_refusal_is_reported()
	_test_arity_is_enforced()
	_test_argument_coding_is_enforced_where_observed()
	print("HANDLERS_WP14_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


# --- Fixture helpers ------------------------------------------------------


func _argument(
	argument_type: int, integer: int = 0, real: float = 0.0, text: String = ""
) -> Dictionary:
	return {"argumentType": argument_type, "integer": integer, "real": real, "text": text}


func _name_arg(value: String) -> Dictionary:
	# TEAM and OBJECT_TYPE_LIST have no observed wire code in this repo, so
	# SageScriptArgs does not assert one. The integer field carries a DECOY: a
	# handler that read the wrong field would get 424242 instead of the name,
	# loudly rather than plausibly.
	return _argument(99, 424242, 0.0, value)


func _player_arg(value: String) -> Dictionary:
	return _argument(ParamTypes.ARGUMENT_PLAYER, 424242, 0.0, value)


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
		"world": PathWorld.new(20260726),
		"registration": outcome,
	}


func _act(harness: Dictionary, name: String, arguments: Array) -> int:
	return (harness["dispatch"] as SageScriptDispatch).execute_action(
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
		"the_wp14_subset_package_is_present",
		(outcome["packages"] as Array).has("WP14-ai-pathing"),
		str(outcome["packages"])
	)
	_check(
		"the_catalog_package_name_remains_free_for_the_completing_package",
		not (outcome["packages"] as Array).has("WP14-pathing-movement"),
		str(outcome["packages"])
	)

	var missing: Array[String] = []
	for name: String in SERVED_ACTIONS:
		if not dispatch.action_handlers.has(name):
			missing.append(name)
	_check("every_served_member_is_registered", missing.is_empty(), str(missing))

	# 2 members, the exact AI-called WP14 membership
	# (game/data/retail_ai_call_census.json). Both are served, none blocked:
	# the surface map records NO signatureGap for either - the NEAREST_TYPE
	# target carries every authored argument, and the object-type-identity
	# blocker is the adapter's to report at runtime.
	_check(
		"the_subset_is_two_members",
		SERVED_ACTIONS.size() == 2,
		"served=%d" % SERVED_ACTIONS.size()
	)
	var wrongly_blocked: Array[String] = []
	for name: String in SERVED_ACTIONS:
		if dispatch.blocked_names().has(name):
			wrongly_blocked.append(name)
	_check(
		"no_served_member_is_blocked",
		wrongly_blocked.is_empty(),
		str(wrongly_blocked)
	)
	_check(
		"both_members_count_as_coverage",
		dispatch.implemented_actions().has("TEAM_MOVE_TO_NEAREST_OBJECT_OF_TYPE")
		and dispatch.implemented_actions().has(
			"TEAM_MOVE_TO_NEAREST_OBJECT_OF_TYPE_OWNED_BY_PLAYER"
		)
	)


# --- 2/3. Positional reads and targets ------------------------------------


func _test_unowned_move_builds_an_any_owner_nearest_type_target() -> void:
	var harness := _harness()
	var world: PathWorld = harness["world"]

	# TEAM_MOVE_TO_NEAREST_OBJECT_OF_TYPE(TEAM, OBJECT_TYPE_LIST): team FIRST,
	# type list SECOND - both text, so nothing but position separates them. The
	# type-LIST name passes through verbatim (the world resolves lists, not the
	# handler), and the owner slot is the documented empty "any owner" form.
	var status := _act(harness, "TEAM_MOVE_TO_NEAREST_OBJECT_OF_TYPE", [
		_name_arg("Synthetic Harvest Team"), _name_arg("SYNTH_RESOURCE_LIST")
	])
	_check(
		"the_unowned_move_is_served",
		status == Dispatch.Status.OK,
		"status=%d" % status
	)
	_check(
		"team_then_type_list_reach_orders_move_to_with_an_any_owner_target",
		world.calls.has(
			"orders.move_to|%d|Synthetic Harvest Team|nearest_type:SYNTH_RESOURCE_LIST|owner:"
			% SageScriptWorld.Scope.TEAM
		),
		str(world.calls)
	)
	_check(
		"team_scope_is_distinct_from_unit_and_player_scope",
		SageScriptWorld.Scope.TEAM != SageScriptWorld.Scope.UNIT
		and SageScriptWorld.Scope.TEAM != SageScriptWorld.Scope.PLAYER
	)


func _test_owned_move_carries_the_authored_owner_verbatim() -> void:
	var harness := _harness()
	var world: PathWorld = harness["world"]

	# TEAM_MOVE_TO_NEAREST_OBJECT_OF_TYPE_OWNED_BY_PLAYER(TEAM,
	# OBJECT_TYPE_LIST, PLAYER): three text arguments, positional only. The
	# owner is the entire difference between this member and its unowned
	# sibling; a "<This Player>" style token passes through for the WORLD to
	# resolve, never expanded here.
	var status := _act(harness, "TEAM_MOVE_TO_NEAREST_OBJECT_OF_TYPE_OWNED_BY_PLAYER", [
		_name_arg("Synthetic Raid Team"),
		_name_arg("SYNTH_FARM_LIST"),
		_player_arg("<Synthetic Enemy>"),
	])
	_check(
		"the_owned_move_is_served",
		status == Dispatch.Status.OK,
		"status=%d" % status
	)
	_check(
		"team_type_list_and_owner_arrive_in_signature_order",
		world.calls.has(
			(
				"orders.move_to|%d|Synthetic Raid Team"
				+ "|nearest_type:SYNTH_FARM_LIST|owner:<Synthetic Enemy>"
			)
			% SageScriptWorld.Scope.TEAM
		),
		str(world.calls)
	)


# --- 4. The empty-owner sentinel ------------------------------------------


func _test_an_empty_authored_player_is_inexpressible_and_refuses() -> void:
	# target_nearest_type reserves owner "" as "any owner" - the OTHER action.
	# An authored empty player on the owned spelling would therefore be
	# silently rewritten into the unowned spelling if passed through. It must
	# refuse, the refusal must never reach the world, and the gap must name
	# the sentinel collision.
	var harness := _harness()
	var world: PathWorld = harness["world"]
	var dispatch: SageScriptDispatch = harness["dispatch"]

	var status := _act(harness, "TEAM_MOVE_TO_NEAREST_OBJECT_OF_TYPE_OWNED_BY_PLAYER", [
		_name_arg("Synthetic Raid Team"), _name_arg("SYNTH_FARM_LIST"), _player_arg(""),
	])
	_check(
		"an_empty_authored_player_refuses_rather_than_becoming_any_owner",
		status == Dispatch.Status.WORLD_REFUSED,
		"status=%d" % status
	)
	_check(
		"the_refused_empty_player_does_not_reach_the_world_at_all",
		world.calls.is_empty(),
		str(world.calls)
	)
	_check(
		"the_refusal_is_recorded_as_a_gap_naming_the_sentinel",
		dispatch.gaps.has(
			"action", "TEAM_MOVE_TO_NEAREST_OBJECT_OF_TYPE_OWNED_BY_PLAYER",
			GapLog.REASON_WORLD_REFUSED
		)
		and String(
			dispatch.gaps.entries[
				"action|TEAM_MOVE_TO_NEAREST_OBJECT_OF_TYPE_OWNED_BY_PLAYER|%s"
				% GapLog.REASON_WORLD_REFUSED
			]["detail"]
		).contains("any owner"),
		str(dispatch.gaps.to_lines())
	)


# --- 5. Command refusal, arity, and argument coding -----------------------


func _test_command_refusal_is_reported() -> void:
	# A world without an orders facet must produce a structured gap per
	# action, never a quiet success. The BASE SageScriptWorld is exactly that
	# world.
	var dispatch := Dispatch.new()
	CoreHandlers.register_all(dispatch)
	Registry.register_all(dispatch)
	var env := Env.new()
	var bare := SageScriptWorld.new()

	var unowned := dispatch.execute_action(
		_record("TEAM_MOVE_TO_NEAREST_OBJECT_OF_TYPE", [
			_name_arg("Synthetic Harvest Team"), _name_arg("SYNTH_RESOURCE_LIST")
		]),
		env, bare, "fixture"
	)
	_check(
		"a_world_without_an_orders_facet_refuses_the_unowned_move",
		unowned == Dispatch.Status.WORLD_REFUSED,
		"status=%d" % unowned
	)
	_check(
		"the_unowned_refusal_names_the_missing_facet_method",
		dispatch.gaps.has(
			"action", "TEAM_MOVE_TO_NEAREST_OBJECT_OF_TYPE", GapLog.REASON_WORLD_REFUSED
		)
		and String(
			dispatch.gaps.entries[
				"action|TEAM_MOVE_TO_NEAREST_OBJECT_OF_TYPE|%s"
				% GapLog.REASON_WORLD_REFUSED
			]["detail"]
		).contains("orders.move_to"),
		str(dispatch.gaps.to_lines())
	)

	var owned := dispatch.execute_action(
		_record("TEAM_MOVE_TO_NEAREST_OBJECT_OF_TYPE_OWNED_BY_PLAYER", [
			_name_arg("Synthetic Raid Team"), _name_arg("SYNTH_FARM_LIST"),
			_player_arg("<Synthetic Enemy>"),
		]),
		env, bare, "fixture"
	)
	_check(
		"a_world_without_an_orders_facet_refuses_the_owned_move",
		owned == Dispatch.Status.WORLD_REFUSED
		and dispatch.gaps.has(
			"action", "TEAM_MOVE_TO_NEAREST_OBJECT_OF_TYPE_OWNED_BY_PLAYER",
			GapLog.REASON_WORLD_REFUSED
		),
		"status=%d gaps=%s" % [owned, str(dispatch.gaps.to_lines())]
	)


func _test_arity_is_enforced() -> void:
	# Arity is checked against the sourced signature before the handler runs,
	# so a positional read can never quietly slide onto a neighbouring
	# argument. One short and one long case per served member.
	var harness := _harness()
	_check(
		"a_short_unowned_move_argument_list_is_rejected",
		_act(harness, "TEAM_MOVE_TO_NEAREST_OBJECT_OF_TYPE", [
			_name_arg("Only Team")
		]) == Dispatch.Status.BAD_ARGUMENTS
	)
	_check(
		"a_long_unowned_move_argument_list_is_rejected",
		_act(harness, "TEAM_MOVE_TO_NEAREST_OBJECT_OF_TYPE", [
			_name_arg("A"), _name_arg("B"), _name_arg("Surplus")
		]) == Dispatch.Status.BAD_ARGUMENTS
	)
	_check(
		"a_short_owned_move_argument_list_is_rejected",
		_act(harness, "TEAM_MOVE_TO_NEAREST_OBJECT_OF_TYPE_OWNED_BY_PLAYER", [
			_name_arg("A"), _name_arg("B")
		]) == Dispatch.Status.BAD_ARGUMENTS
	)
	_check(
		"a_long_owned_move_argument_list_is_rejected",
		_act(harness, "TEAM_MOVE_TO_NEAREST_OBJECT_OF_TYPE_OWNED_BY_PLAYER", [
			_name_arg("A"), _name_arg("B"), _player_arg("P"), _name_arg("Surplus")
		]) == Dispatch.Status.BAD_ARGUMENTS
	)


func _test_argument_coding_is_enforced_where_observed() -> void:
	# SageScriptArgs asserts argument-type CODES only where this repo has
	# observed one (PLAYER here). TEAM and OBJECT_TYPE_LIST have no observed
	# code, so validation deliberately declines to assert on them - which is
	# exactly why every handler read is positional and every fixture carries a
	# decoy in the unread field.
	var harness := _harness()
	var world: PathWorld = harness["world"]

	# The PLAYER slot of the owned move, mis-coded as a generic name argument
	# (code 99, expected ARGUMENT_PLAYER).
	_check(
		"a_mis_coded_player_slot_is_rejected_before_dispatch",
		_act(harness, "TEAM_MOVE_TO_NEAREST_OBJECT_OF_TYPE_OWNED_BY_PLAYER", [
			_name_arg("Synthetic Raid Team"), _name_arg("SYNTH_FARM_LIST"),
			_name_arg("<Synthetic Enemy>"),
		]) == Dispatch.Status.BAD_ARGUMENTS
	)

	# TEAM slots carry no observed code, so a foreign code passes validation
	# BY DESIGN and the handler must still read the TEXT field - the decoy
	# integer in the same slot is what a wrong-field read would deliver.
	var status := _act(harness, "TEAM_MOVE_TO_NEAREST_OBJECT_OF_TYPE", [
		_argument(ParamTypes.ARGUMENT_COUNTER_NAME, 424242, 0.0, "Synthetic Harvest Team"),
		_name_arg("SYNTH_RESOURCE_LIST"),
	])
	_check(
		"an_unobserved_team_code_is_accepted_and_still_read_as_text",
		status == Dispatch.Status.OK
		and world.calls.has(
			"orders.move_to|%d|Synthetic Harvest Team|nearest_type:SYNTH_RESOURCE_LIST|owner:"
			% SageScriptWorld.Scope.TEAM
		),
		"status=%d calls=%s" % [status, str(world.calls)]
	)


# --- Stub world -----------------------------------------------------------
#
# SageScriptWorldStub teaches only the Players facet, and this package touches
# Orders. That facet is taught HERE rather than in the shared stub, because
# the shared stub is an existing file that ~17 concurrent packages also depend
# on. Every command is recorded as a flat string - including the tagged target
# dictionary, flattened as nearest_type:name|owner:owner - so a test asserts
# on the exact arguments that reached the world.


class PathWorld:
	extends SageScriptWorldStub

	var calls: Array[String] = []

	func _make_orders() -> SageScriptWorld.Orders:
		return StubOrders.new()

	## Flattens a target_*() dictionary for call-log assertions. Only the kind
	## this package emits is given a name; anything else is its raw kind
	## number, which no assertion matches - so an unexpected target FAILS
	## instead of aliasing onto an expected one. The owner is flattened even
	## when empty, so "any owner" and "owner lost on the way" cannot alias
	## either. Lives on the world (not the runner) because GDScript inner
	## classes cannot see outer-script statics.
	func target_label(target: Dictionary) -> String:
		var kind := int(target.get("kind", -1))
		if kind == SageScriptWorld.TargetKind.NEAREST_TYPE:
			return "nearest_type:%s|owner:%s" % [
				String(target.get("name", "")), String(target.get("owner", ""))
			]
		return "kind-%d" % kind


class StubOrders:
	extends SageScriptWorld.Orders

	func move_to(scope: int, name: String, target: Dictionary) -> bool:
		var stub := world as PathWorld
		stub.calls.append(
			"orders.move_to|%d|%s|%s" % [scope, name, stub.target_label(target)]
		)
		return true


# --- Reporting ------------------------------------------------------------


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("HANDLERS_WP14 PASS %s%s" % [name, " (%s)" % detail if detail != "" else ""])
	else:
		failed += 1
		printerr("HANDLERS_WP14 FAIL %s%s" % [name, " (%s)" % detail if detail != "" else ""])
