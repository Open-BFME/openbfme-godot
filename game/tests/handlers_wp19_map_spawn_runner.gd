extends SceneTree

## Proof runner for WP19-map-spawn (wp19_map_spawn.gd).
##
## Covers, in order:
##   1. registration    - CREATE_NAMED_ON_TEAM_AT_WAYPOINT is served and the
##                        _WITH_ORIENTATION variant is gap-registered (the world
##                        surface has no angle slot on create_on_team_at), with
##                        the gap never counted as coverage
##   2. the served path - the four text-carried names are read POSITIONALLY
##                        (UNIT, OBJECT_TYPE, TEAM, WAYPOINT - all four are
##                        text, so only position separates them; the fixture
##                        names are mutually distinguishable in the logged call
##                        so a swapped read produces a DIFFERENT string), the
##                        spawn joins the NAMED TEAM at the NAMED WAYPOINT, and
##                        the WP16 read-side condition NAMED_CREATED flips from
##                        false to true for the created name through the same
##                        dispatch
##   3. refusals        - a missing waypoint and a world that models nothing
##                        both come back WORLD_REFUSED with a structured gap
##                        naming units.create_on_team_at - never a quiet OK
##   4. arity           - a three-argument spawn is rejected before the world
##                        is ever consulted
##   5. gap register    - the orientation variant reports BLOCKED, names the
##                        missing angle slot, and never reaches the world
##
## Every fixture value is SYNTHETIC. The call SHAPE is the retail skirmish
## creature/Gollum spawn script shape (a named spawn on a team at a map
## waypoint), but no retail payload text is reproduced here.
##
## Invocation:
##   Godot_v4.7-stable_win64_console.exe --headless --path game \
##     --script res://tests/handlers_wp19_map_spawn_runner.gd

const Registry := preload("res://src/script/handlers/_registry.gd")
const Dispatch := preload("res://src/script/script_dispatch.gd")
const CoreHandlers := preload("res://src/script/script_handlers_core.gd")
const Env := preload("res://src/script/script_env.gd")
const GapLog := preload("res://src/script/script_gaps.gd")
const ParamTypes := preload("res://src/script/script_param_types.gd")
const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")

const ACTION := "CREATE_NAMED_ON_TEAM_AT_WAYPOINT"
const ORIENTATION_ACTION := "CREATE_NAMED_ON_TEAM_AT_WAYPOINT_WITH_ORIENTATION"

## LIVENESS GUARD. A GDScript RUNTIME error aborts the enclosing function on
## the spot without propagating, so every later _check() in that function
## silently never runs and `failed` never moves. If the run makes any other
## number of checks, something aborted (or an assertion was added without
## updating this) and the result is not to be trusted.
const EXPECTED_CHECKS := 21

var passed := 0
var failed := 0

## Turns a hung/aborted headless run into a loud non-zero exit.
## See tests/runner_watchdog.gd.
var _runner_watchdog := RunnerWatchdogScript.new()


func _initialize() -> void:
	_runner_watchdog.start(self, "HANDLERS_WP19")
	_runner_watchdog.set_result_provider(func() -> Vector2i: return Vector2i(passed, failed))
	call_deferred("_run")


func _run() -> void:
	_test_registration()
	_test_spawn_creates_the_named_object_on_the_team_at_the_waypoint()
	_test_a_missing_waypoint_refuses_instead_of_spawning_elsewhere()
	_test_a_refusing_world_records_a_refusal_not_a_silent_success()
	_test_arity_is_enforced()
	_test_the_orientation_variant_is_gap_registered()
	var ran := passed + failed
	if ran != EXPECTED_CHECKS:
		failed += 1
		printerr(
			"HANDLERS_WP19 FAIL liveness: ran %d checks, expected %d - a function aborted before its assertions"
			% [ran, EXPECTED_CHECKS]
		)
	print("HANDLERS_WP19_RESULT passed=%d failed=%d" % [passed, failed])
	_runner_watchdog.stop()
	quit(0 if failed == 0 else 1)


# --- Fixture helpers ------------------------------------------------------


func _argument(
	argument_type: int, integer: int = 0, real: float = 0.0, text: String = ""
) -> Dictionary:
	return {"argumentType": argument_type, "integer": integer, "real": real, "text": text}


func _name_arg(value: String) -> Dictionary:
	# UNIT, OBJECT_TYPE, TEAM and WAYPOINT have no observed wire code in this
	# repo, so SageScriptArgs does not assert one. The integer field carries a
	# DECOY: a handler that read the wrong field would get 424242 instead of
	# the name, loudly rather than plausibly.
	return _argument(99, 424242, 0.0, value)


func _real_arg(value: float) -> Dictionary:
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
	return {
		"dispatch": dispatch,
		"env": Env.new(),
		"world": SpawnWorld.new(20260808),
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


func _test_registration() -> void:
	var harness := _harness()
	var dispatch: SageScriptDispatch = harness["dispatch"]
	var outcome: Dictionary = harness["registration"]

	_check(
		"registration_reports_no_errors",
		(outcome["errors"] as Array).is_empty(),
		str(outcome["errors"])
	)
	_check(
		"the_wp19_package_is_present",
		(outcome["packages"] as Array).has("WP19-map-spawn"),
		str(outcome["packages"])
	)
	_check(
		"create_named_on_team_at_waypoint_has_a_handler",
		dispatch.action_handlers.has(ACTION)
		and dispatch.implemented_actions().has(ACTION),
		str(dispatch.implemented_actions())
	)
	_check(
		"the_orientation_variant_is_gap_registered",
		dispatch.blocked_names().has(ORIENTATION_ACTION),
		str(dispatch.blocked_names())
	)
	_check(
		"the_gap_registered_variant_is_not_counted_as_coverage",
		not dispatch.implemented_actions().has(ORIENTATION_ACTION)
	)


# --- 2. The served path ---------------------------------------------------


func _test_spawn_creates_the_named_object_on_the_team_at_the_waypoint() -> void:
	# CREATE_NAMED_ON_TEAM_AT_WAYPOINT(UNIT, OBJECT_TYPE, TEAM, WAYPOINT).
	# All FOUR arguments are text-carried names, so nothing but position
	# separates them; every fixture name is distinguishable in the logged call,
	# so any swapped read produces a different string, not an equal one.
	var harness := _harness()
	var world: SpawnWorld = harness["world"]
	var dispatch: SageScriptDispatch = harness["dispatch"]
	world.add_waypoint("Synthetic_Lair_Waypoint")

	_check(
		"named_created_is_false_before_the_spawn",
		not _cond(harness, "NAMED_CREATED", [_name_arg("Synthetic_Creature")]),
		str(dispatch.gaps.to_lines())
	)

	var status := _act(harness, ACTION, [
		_name_arg("Synthetic_Creature"),
		_name_arg("Synthetic_Creature_Type"),
		_name_arg("Synthetic_Lair_Team"),
		_name_arg("Synthetic_Lair_Waypoint"),
	])
	_check(
		"the_spawn_dispatches_ok",
		status == Dispatch.Status.OK,
		"status=%d gaps=%s" % [status, str(dispatch.gaps.to_lines())]
	)
	_check(
		"the_handler_reads_its_four_names_positionally",
		world.calls == [
			(
				"units.create_on_team_at|Synthetic_Creature_Type|Synthetic_Lair_Team"
				+ "|waypoint:Synthetic_Lair_Waypoint|Synthetic_Creature"
			)
		],
		str(world.calls)
	)
	_check(
		"the_object_joins_the_named_team",
		world.created.get("Synthetic_Creature", "") == "Synthetic_Lair_Team",
		str(world.created)
	)
	_check(
		"named_created_flips_for_the_created_name",
		_cond(harness, "NAMED_CREATED", [_name_arg("Synthetic_Creature")]),
		str(dispatch.gaps.to_lines())
	)
	_check(
		"the_served_spawn_records_no_gap",
		not dispatch.gaps.has("action", ACTION, GapLog.REASON_WORLD_REFUSED)
		and not dispatch.gaps.has("action", ACTION, GapLog.REASON_UNIMPLEMENTED),
		str(dispatch.gaps.to_lines())
	)


# --- 3. Refusals ----------------------------------------------------------


func _test_a_missing_waypoint_refuses_instead_of_spawning_elsewhere() -> void:
	# The waypoint is a load-bearing argument: a spawn "somewhere" is not the
	# spawn the map authored. A world that cannot resolve it must refuse, and
	# the refusal must land on the gap log - never a quiet success.
	var harness := _harness()
	var world: SpawnWorld = harness["world"]
	var dispatch: SageScriptDispatch = harness["dispatch"]
	world.expect_refusals()
	# "Synthetic_Missing_Waypoint" has no fixture, so the spawn refuses.

	var status := _act(harness, ACTION, [
		_name_arg("Synthetic_Creature"),
		_name_arg("Synthetic_Creature_Type"),
		_name_arg("Synthetic_Lair_Team"),
		_name_arg("Synthetic_Missing_Waypoint"),
	])
	_check(
		"a_missing_waypoint_is_world_refused_not_ok",
		status == Dispatch.Status.WORLD_REFUSED,
		"status=%d" % status
	)
	_check(
		"the_refusal_names_the_world_seam",
		dispatch.gaps.has("action", ACTION, GapLog.REASON_WORLD_REFUSED)
		and world.refused("units.create_on_team_at"),
		"gaps=%s refusals=%s" % [str(dispatch.gaps.to_lines()), str(world.refusal_log)]
	)
	_check("nothing_was_spawned", world.created.is_empty(), str(world.created))


func _test_a_refusing_world_records_a_refusal_not_a_silent_success() -> void:
	# The BASE SageScriptWorld models nothing: every Units command inherits the
	# refusing base. Dispatching against it must produce WORLD_REFUSED plus a
	# structured gap, never OK.
	var dispatch := Dispatch.new()
	CoreHandlers.register_all(dispatch)
	Registry.register_all(dispatch)
	var env := Env.new()

	var status := dispatch.execute_action(
		_record(ACTION, [
			_name_arg("Synthetic_Creature"),
			_name_arg("Synthetic_Creature_Type"),
			_name_arg("Synthetic_Lair_Team"),
			_name_arg("Synthetic_Lair_Waypoint"),
		]),
		env,
		SageScriptWorld.new(),
		"fixture"
	)
	_check(
		"a_refused_spawn_is_world_refused_not_ok",
		status == Dispatch.Status.WORLD_REFUSED,
		"status=%d" % status
	)
	_check(
		"the_refusal_is_a_structured_gap_naming_the_facet_method",
		dispatch.gaps.has("action", ACTION, GapLog.REASON_WORLD_REFUSED)
		and _gap_detail(dispatch, "action", ACTION, GapLog.REASON_WORLD_REFUSED)
			.contains("units.create_on_team_at"),
		str(dispatch.gaps.to_lines())
	)


# --- 4. Arity -------------------------------------------------------------


func _test_arity_is_enforced() -> void:
	# Arity is checked against the sourced signature before the handler runs,
	# so a positional read can never quietly slide onto a neighbouring slot.
	var harness := _harness()
	var world: SpawnWorld = harness["world"]
	world.add_waypoint("Synthetic_Lair_Waypoint")

	_check(
		"a_three_argument_spawn_is_rejected",
		_act(harness, ACTION, [
			_name_arg("Synthetic_Creature"),
			_name_arg("Synthetic_Creature_Type"),
			_name_arg("Synthetic_Lair_Team"),
		]) == Dispatch.Status.BAD_ARGUMENTS
	)
	_check(
		"no_rejected_call_reached_the_world",
		world.calls.is_empty(),
		str(world.calls)
	)


# --- 5. The gap-registered orientation variant ------------------------------


func _test_the_orientation_variant_is_gap_registered() -> void:
	# CREATE_NAMED_ON_TEAM_AT_WAYPOINT_WITH_ORIENTATION(UNIT, OBJECT_TYPE,
	# TEAM, WAYPOINT, REAL) authors a facing ANGLE the world surface has no
	# slot for (units.create_on_team_at carries no angle), and dropping an
	# authored facing would spawn objects facing a direction the map never
	# chose. It reports BLOCKED with the missing surface named, and never
	# reaches the world.
	var harness := _harness()
	var world: SpawnWorld = harness["world"]
	var dispatch: SageScriptDispatch = harness["dispatch"]
	world.add_waypoint("Synthetic_Lair_Waypoint")

	var status := _act(harness, ORIENTATION_ACTION, [
		_name_arg("Synthetic_Creature"),
		_name_arg("Synthetic_Creature_Type"),
		_name_arg("Synthetic_Lair_Team"),
		_name_arg("Synthetic_Lair_Waypoint"),
		_real_arg(90.0),
	])
	_check(
		"the_orientation_variant_reports_blocked",
		status == Dispatch.Status.BLOCKED,
		"status=%d" % status
	)
	_check(
		"the_blocked_gap_names_the_missing_angle_slot",
		dispatch.gaps.has(
			"action", ORIENTATION_ACTION, GapLog.REASON_BLOCKED_SUBSYSTEM
		)
		and _gap_detail(
			dispatch, "action", ORIENTATION_ACTION, GapLog.REASON_BLOCKED_SUBSYSTEM
		).contains("angle"),
		str(dispatch.gaps.to_lines())
	)
	_check(
		"the_blocked_variant_never_reached_the_world",
		world.calls.is_empty() and world.created.is_empty(),
		"calls=%s created=%s" % [str(world.calls), str(world.created)]
	)


# --- Stub world -----------------------------------------------------------
#
# The shared SageScriptWorldStub teaches no Units facet at all, so the slice
# this runner needs is taught HERE rather than in the shared stub (an existing
# file the concurrent packages also depend on). Every command is recorded as a
# flat string so a test asserts on the exact arguments that reached the world;
# every read answers from a fixture or REFUSES, so a test that forgot to set
# one gets a loud refusal rather than a plausible spawn.


class SpawnWorld:
	extends SageScriptWorldStub

	var calls: Array[String] = []

	## waypoint name -> true. Only fixture waypoints resolve.
	var waypoints: Dictionary = {}

	## created object name -> owning team name.
	var created: Dictionary = {}

	func add_waypoint(waypoint: String) -> void:
		waypoints[waypoint] = true

	func _make_units() -> SageScriptWorld.Units:
		return SpawnStubUnits.new()


class SpawnStubUnits:
	extends SageScriptWorld.Units

	func create_on_team_at(
		object_type: String, team: String, target: Dictionary, object_name: String
	) -> SageWorldQuery:
		var stub := world as SpawnWorld
		var target_text := "kind=%d" % int(target.get("kind", -1))
		if int(target.get("kind", -1)) == SageScriptWorld.TargetKind.WAYPOINT:
			target_text = "waypoint:%s" % String(target.get("name", ""))
		stub.calls.append(
			"units.create_on_team_at|%s|%s|%s|%s"
			% [object_type, team, target_text, object_name]
		)
		if int(target.get("kind", -1)) != SageScriptWorld.TargetKind.WAYPOINT:
			return _refuse_query(
				"units.create_on_team_at", "fixture models only waypoint targets"
			)
		var waypoint := String(target.get("name", ""))
		if not stub.waypoints.has(waypoint):
			return _refuse_query(
				"units.create_on_team_at", "no fixture waypoint '%s'" % waypoint
			)
		stub.created[object_name] = team
		return SageWorldQuery.hit(object_name)

	func was_created(object_name: String) -> SageWorldQuery:
		# NAMED_CREATED's read side: name-table presence (wp16_named_created).
		return SageWorldQuery.hit((world as SpawnWorld).created.has(object_name))


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
		print("HANDLERS_WP19 PASS %s%s" % [name, " (%s)" % detail if detail != "" else ""])
	else:
		failed += 1
		printerr("HANDLERS_WP19 FAIL %s%s" % [name, " (%s)" % detail if detail != "" else ""])
