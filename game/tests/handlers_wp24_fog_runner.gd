extends SceneTree

## Proof runner for WP24-fog (wp24_fog.gd) - the four MAP_REVEAL/SHROUD fog
## actions the retail-slice fog surface (SliceFog over sim.parity.fog_*) can
## now serve honestly, unblocked out of WP02-blocked-fog-of-war (packet P6,
## 2026-08-08).
##
## Covers, in order:
##   1. registration    - the 4 served actions are registered and counted as
##                        coverage, ABSENT from the blocked set; the 9 fog
##                        names the surface cannot carry stay blocked and are
##                        never counted as coverage
##   2. border shroud   - ENABLE/DISABLE_BORDER_SHROUD reach
##                        fog.set_border_shroud(true/false) in order, and a
##                        world with no fog facet refuses into a structured
##                        gap naming the facet method - never a quiet OK
##   3. waypoint reveal/shroud - MAP_REVEAL_AT_WAYPOINT / MAP_SHROUD_AT_WAYPOINT
##                        (WAYPOINT, REAL, PLAYER) read POSITIONALLY (waypoint
##                        and player are both text-carried; only position
##                        separates them), resolve the waypoint WORLD-SIDE via
##                        areas.waypoint_position, and hand fog a POSITION
##                        target carrying the authored radius. THE TRAP THIS
##                        PINS: SliceFog's target reader understands POSITION
##                        targets only (retail_slice_script_world.gd
##                        _fog_target_center_radius) - a raw waypoint/area
##                        target would silently reveal around the ORIGIN, so
##                        the stub fog REFUSES any non-POSITION target kind.
##                        MAP_REVEAL_AT_WAYPOINT is the NON-permanent variant;
##                        permanent=false is pinned in the logged call.
##   4. refusals        - a missing waypoint is WORLD_REFUSED naming
##                        areas.waypoint_position, and NO fog call is made:
##                        a reveal "somewhere" is not the reveal the map
##                        authored
##   5. arity           - one wrong-arity case per served action, rejected
##                        before the world is consulted
##   6. still blocked   - MAP_REVEAL_ALL (whole-map extent) and
##                        MAP_REVEAL_PERMANENTLY_AT_WAYPOINT (REVEAL_NAME
##                        registry) report BLOCKED with a gap naming the fog
##                        of war surface, and never reach the world
##
## Every fixture value is SYNTHETIC. No retail payload text is reproduced.
##
## Invocation:
##   <godot> --headless --path game \
##     --script res://tests/handlers_wp24_fog_runner.gd

const Registry := preload("res://src/script/handlers/_registry.gd")
const Dispatch := preload("res://src/script/script_dispatch.gd")
const CoreHandlers := preload("res://src/script/script_handlers_core.gd")
const Env := preload("res://src/script/script_env.gd")
const GapLog := preload("res://src/script/script_gaps.gd")
const ParamTypes := preload("res://src/script/script_param_types.gd")
const Watchdog := preload("res://tests/runner_watchdog.gd")

## The 4 actions this package serves. Written out rather than derived from the
## module, so a member quietly disappearing from register() fails here.
const SERVED_ACTIONS := [
	"ENABLE_BORDER_SHROUD",
	"DISABLE_BORDER_SHROUD",
	"MAP_REVEAL_AT_WAYPOINT",
	"MAP_SHROUD_AT_WAYPOINT",
]

## The 9 fog names the current surface CANNOT carry (whole-map extent,
## trigger-area geometry, named permanent-reveal registry). They stay declared
## in wp02_blocked_fog_of_war.gd; this runner pins that they still block.
const STILL_BLOCKED := [
	"MAP_REVEAL_ALL",
	"MAP_REVEAL_ALL_PERM",
	"MAP_REVEAL_ALL_UNDO_PERM",
	"MAP_REVEAL_IN_TRIGGER",
	"MAP_REVEAL_PERMANENTLY_AT_WAYPOINT",
	"MAP_REVEAL_PERMANENTLY_IN_TRIGGER",
	"MAP_SHROUD_ALL",
	"MAP_UNDO_REVEAL_PERMANENTLY_AT_WAYPOINT",
	"MAP_UNDO_REVEAL_PERMANENTLY_IN_TRIGGER",
]

## LIVENESS GUARD. A GDScript RUNTIME error aborts the enclosing function on
## the spot without propagating, so every later _check() in that function
## silently never runs. A healthy run makes exactly this many checks; any
## other number means something aborted (or an assertion was added without
## updating this) and the result is not to be trusted.
const EXPECTED_CHECKS := 31

var passed := 0
var failed := 0
var _watchdog := Watchdog.new()

## Every world this run created, so facet<->world reference cycles can be
## broken before quit (a dropped world with live facets is a RefCounted cycle
## that leaks and can AV on Windows teardown).
var _worlds: Array = []


func _initialize() -> void:
	_watchdog.start(self, "HANDLERS_WP24", 0, 0, true)
	_watchdog.set_result_provider(func() -> Vector2i: return Vector2i(passed, failed))
	call_deferred("_run")


func _run() -> void:
	_test_registration_covers_the_split()
	_test_border_shroud_round_trip()
	_test_border_shroud_refusal_is_a_structured_gap()
	_test_reveal_at_waypoint_is_position_targeted()
	_test_shroud_at_waypoint_is_position_targeted()
	_test_a_missing_waypoint_refuses_before_any_fog_call()
	_test_arity_is_enforced_per_action()
	_test_the_unservable_names_still_block()
	var ran := passed + failed
	if ran != EXPECTED_CHECKS:
		failed += 1
		printerr(
			"HANDLERS_WP24 FAIL liveness: ran %d checks, expected %d - a function aborted before its assertions"
			% [ran, EXPECTED_CHECKS]
		)
	print("HANDLERS_WP24_RESULT passed=%d failed=%d" % [passed, failed])
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
	# WAYPOINT and REVEAL_NAME have no observed wire code in this repo, so
	# SageScriptArgs does not assert one. The integer field carries a DECOY: a
	# handler that read the wrong field would get 424242 instead of the name,
	# loudly rather than plausibly.
	return _argument(99, 424242, 0.0, value)


func _player_arg(value: String) -> Dictionary:
	return _argument(ParamTypes.ARGUMENT_PLAYER, 424242, 0.0, value)


func _real_arg(value: float) -> Dictionary:
	# REAL is carried in the `real` payload field. The integer decoy catches a
	# wrong-field read: a 424242.0 radius would be loud in the logged fog call.
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
	var world := FogWorld.new(20260808)
	_worlds.append(world)
	return {
		"dispatch": dispatch,
		"env": Env.new(),
		"world": world,
		"registration": outcome,
	}


func _act(harness: Dictionary, name: String, arguments: Array) -> int:
	return (harness["dispatch"] as SageScriptDispatch).execute_action(
		_record(name, arguments), harness["env"], harness["world"], "fixture"
	)


# --- 1. Registration --------------------------------------------------------


func _test_registration_covers_the_split() -> void:
	var harness := _harness()
	var dispatch: SageScriptDispatch = harness["dispatch"]
	var outcome: Dictionary = harness["registration"]

	_check(
		"registration_reports_no_errors",
		(outcome["errors"] as Array).is_empty(),
		str(outcome["errors"])
	)
	_check(
		"the_wp24_package_is_present",
		(outcome["packages"] as Array).has("WP24-fog"),
		str(outcome["packages"])
	)

	var missing: Array[String] = []
	var uncounted: Array[String] = []
	var shadowed: Array[String] = []
	var implemented := dispatch.implemented_actions()
	for name: String in SERVED_ACTIONS:
		if not dispatch.action_handlers.has(name):
			missing.append(name)
		if not implemented.has(name):
			uncounted.append(name)
		if dispatch.blocked_names().has(name):
			shadowed.append(name)
	_check("every_served_fog_action_is_registered", missing.is_empty(), str(missing))
	_check(
		"every_served_fog_action_counts_as_coverage", uncounted.is_empty(), str(uncounted)
	)
	# First-registration-wins: a served name still declared blocked in wp02
	# would be silently shadowed there (wp02 sorts before wp24).
	_check("no_served_fog_action_remains_blocked", shadowed.is_empty(), str(shadowed))

	var unblocked: Array[String] = []
	var counted: Array[String] = []
	for name: String in STILL_BLOCKED:
		if not dispatch.blocked_names().has(name):
			unblocked.append(name)
		if implemented.has(name):
			counted.append(name)
	_check(
		"every_unservable_fog_name_stays_blocked", unblocked.is_empty(), str(unblocked)
	)
	_check(
		"blocked_fog_names_are_not_counted_as_coverage", counted.is_empty(), str(counted)
	)


# --- 2. Border shroud -------------------------------------------------------


func _test_border_shroud_round_trip() -> void:
	# ENABLE_BORDER_SHROUD / DISABLE_BORDER_SHROUD - table rows
	#   "ENABLE_BORDER_SHROUD|-|Map|SHARED|S"   (script_action_table.gd row 212)
	#   "DISABLE_BORDER_SHROUD|-|Map|SHARED|S"  (script_action_table.gd row 185)
	# Zero arguments; the polarity is the action's own name, pinned in the
	# logged facet call so an inverted pair cannot pass.
	var harness := _harness()
	var world: FogWorld = harness["world"]
	var dispatch: SageScriptDispatch = harness["dispatch"]

	var enable_status := _act(harness, "ENABLE_BORDER_SHROUD", [])
	var disable_status := _act(harness, "DISABLE_BORDER_SHROUD", [])
	_check(
		"enable_border_shroud_dispatches_ok",
		enable_status == Dispatch.Status.OK,
		"status=%d gaps=%s" % [enable_status, str(dispatch.gaps.to_lines())]
	)
	_check(
		"disable_border_shroud_dispatches_ok",
		disable_status == Dispatch.Status.OK,
		"status=%d gaps=%s" % [disable_status, str(dispatch.gaps.to_lines())]
	)
	_check(
		"the_border_polarity_reaches_the_fog_facet_in_order",
		world.calls == ["fog.set_border_shroud|true", "fog.set_border_shroud|false"],
		str(world.calls)
	)
	_check(
		"the_served_border_actions_record_no_gap",
		not dispatch.gaps.has(
			"action", "ENABLE_BORDER_SHROUD", GapLog.REASON_WORLD_REFUSED
		)
		and not dispatch.gaps.has(
			"action", "DISABLE_BORDER_SHROUD", GapLog.REASON_WORLD_REFUSED
		)
		and not dispatch.gaps.has(
			"action", "ENABLE_BORDER_SHROUD", GapLog.REASON_BLOCKED_SUBSYSTEM
		),
		str(dispatch.gaps.to_lines())
	)


func _test_border_shroud_refusal_is_a_structured_gap() -> void:
	# The BASE SageScriptWorld models nothing: its Fog facet refuses every
	# command. Dispatching against it must be WORLD_REFUSED plus a structured
	# gap naming fog.set_border_shroud - never a quiet OK.
	var dispatch := Dispatch.new()
	CoreHandlers.register_all(dispatch)
	Registry.register_all(dispatch)
	var bare := SageScriptWorld.new()
	_worlds.append(bare)

	var status := dispatch.execute_action(
		_record("ENABLE_BORDER_SHROUD", []), Env.new(), bare, "fixture"
	)
	_check(
		"a_world_without_fog_refuses_border_shroud",
		status == Dispatch.Status.WORLD_REFUSED,
		"status=%d" % status
	)
	_check(
		"the_border_refusal_is_a_structured_gap_naming_the_facet_method",
		dispatch.gaps.has("action", "ENABLE_BORDER_SHROUD", GapLog.REASON_WORLD_REFUSED)
		and _gap_detail(
			dispatch, "action", "ENABLE_BORDER_SHROUD", GapLog.REASON_WORLD_REFUSED
		).contains("fog.set_border_shroud"),
		str(dispatch.gaps.to_lines())
	)


# --- 3. Waypoint reveal / shroud --------------------------------------------


func _test_reveal_at_waypoint_is_position_targeted() -> void:
	# MAP_REVEAL_AT_WAYPOINT(WAYPOINT, REAL, PLAYER) - table row 300:
	#   "MAP_REVEAL_AT_WAYPOINT|WAYPOINT,REAL,PLAYER|Map|SHARED|S"
	# WAYPOINT and PLAYER are both text-carried, so only position separates
	# them - the fixture waypoint resolves and the fixture player name does
	# NOT, so a swapped read surfaces as a refusal, not a pass. The waypoint
	# resolves WORLD-SIDE (areas.waypoint_position) and fog receives a
	# POSITION target with the authored radius; permanent=false is pinned
	# (this is the non-permanent variant).
	var harness := _harness()
	var world: FogWorld = harness["world"]
	var dispatch: SageScriptDispatch = harness["dispatch"]
	world.add_waypoint("Synthetic_Beacon_Waypoint", Vector3(120.0, 0.0, -40.0))

	var status := _act(harness, "MAP_REVEAL_AT_WAYPOINT", [
		_name_arg("Synthetic_Beacon_Waypoint"),
		_real_arg(25.0),
		_player_arg("Synthetic_Player"),
	])
	_check(
		"the_waypoint_reveal_dispatches_ok",
		status == Dispatch.Status.OK,
		"status=%d gaps=%s" % [status, str(dispatch.gaps.to_lines())]
	)
	_check(
		"the_waypoint_resolves_world_side_then_fog_gets_a_position_target",
		world.calls == [
			"areas.waypoint_position|Synthetic_Beacon_Waypoint",
			(
				"fog.reveal|Synthetic_Player|position:(120.0, 0.0, -40.0)"
				+ "|radius=25.0|permanent=false"
			),
		],
		str(world.calls)
	)
	_check(
		"the_served_reveal_records_no_gap",
		not dispatch.gaps.has(
			"action", "MAP_REVEAL_AT_WAYPOINT", GapLog.REASON_WORLD_REFUSED
		)
		and not dispatch.gaps.has(
			"action", "MAP_REVEAL_AT_WAYPOINT", GapLog.REASON_BLOCKED_SUBSYSTEM
		),
		str(dispatch.gaps.to_lines())
	)


func _test_shroud_at_waypoint_is_position_targeted() -> void:
	# MAP_SHROUD_AT_WAYPOINT(WAYPOINT, REAL, PLAYER) - table row 305:
	#   "MAP_SHROUD_AT_WAYPOINT|WAYPOINT,REAL,PLAYER|Map|SHARED|S"
	# Same shape as the reveal; the facet method (fog.shroud) is what differs,
	# pinned in the logged call.
	var harness := _harness()
	var world: FogWorld = harness["world"]
	var dispatch: SageScriptDispatch = harness["dispatch"]
	world.add_waypoint("Synthetic_Beacon_Waypoint", Vector3(120.0, 0.0, -40.0))

	var status := _act(harness, "MAP_SHROUD_AT_WAYPOINT", [
		_name_arg("Synthetic_Beacon_Waypoint"),
		_real_arg(30.0),
		_player_arg("Synthetic_Player"),
	])
	_check(
		"the_waypoint_shroud_dispatches_ok",
		status == Dispatch.Status.OK,
		"status=%d gaps=%s" % [status, str(dispatch.gaps.to_lines())]
	)
	_check(
		"the_shroud_reaches_fog_with_the_resolved_position_and_radius",
		world.calls == [
			"areas.waypoint_position|Synthetic_Beacon_Waypoint",
			"fog.shroud|Synthetic_Player|position:(120.0, 0.0, -40.0)|radius=30.0",
		],
		str(world.calls)
	)
	_check(
		"the_served_shroud_records_no_gap",
		not dispatch.gaps.has(
			"action", "MAP_SHROUD_AT_WAYPOINT", GapLog.REASON_WORLD_REFUSED
		)
		and not dispatch.gaps.has(
			"action", "MAP_SHROUD_AT_WAYPOINT", GapLog.REASON_BLOCKED_SUBSYSTEM
		),
		str(dispatch.gaps.to_lines())
	)


# --- 4. Refusals -------------------------------------------------------------


func _test_a_missing_waypoint_refuses_before_any_fog_call() -> void:
	# The waypoint is a load-bearing argument: a reveal "somewhere" is not the
	# reveal the map authored. An unresolvable waypoint must be WORLD_REFUSED
	# naming areas.waypoint_position, and fog must never be touched.
	var harness := _harness()
	var world: FogWorld = harness["world"]
	var dispatch: SageScriptDispatch = harness["dispatch"]
	world.expect_refusals()
	# "Synthetic_Missing_Waypoint" has no fixture, so resolution refuses.

	var reveal_status := _act(harness, "MAP_REVEAL_AT_WAYPOINT", [
		_name_arg("Synthetic_Missing_Waypoint"),
		_real_arg(25.0),
		_player_arg("Synthetic_Player"),
	])
	_check(
		"a_missing_waypoint_reveal_is_world_refused_not_ok",
		reveal_status == Dispatch.Status.WORLD_REFUSED,
		"status=%d" % reveal_status
	)
	_check(
		"the_reveal_refusal_names_the_waypoint_resolution_seam",
		dispatch.gaps.has(
			"action", "MAP_REVEAL_AT_WAYPOINT", GapLog.REASON_WORLD_REFUSED
		)
		and world.refused("areas.waypoint_position"),
		"gaps=%s refusals=%s" % [str(dispatch.gaps.to_lines()), str(world.refusal_log)]
	)

	var shroud_status := _act(harness, "MAP_SHROUD_AT_WAYPOINT", [
		_name_arg("Synthetic_Missing_Waypoint"),
		_real_arg(30.0),
		_player_arg("Synthetic_Player"),
	])
	_check(
		"a_missing_waypoint_shroud_is_world_refused_not_ok",
		shroud_status == Dispatch.Status.WORLD_REFUSED,
		"status=%d" % shroud_status
	)
	var fog_calls: Array = world.calls.filter(
		func(entry: String) -> bool: return entry.begins_with("fog.")
	)
	_check(
		"no_fog_call_was_made_for_an_unresolved_waypoint",
		fog_calls.is_empty(),
		str(world.calls)
	)


# --- 5. Arity ----------------------------------------------------------------


func _test_arity_is_enforced_per_action() -> void:
	# Arity is checked against the sourced signature before the handler runs,
	# so a positional read can never quietly slide onto a neighbouring slot.
	var harness := _harness()
	var world: FogWorld = harness["world"]
	var dispatch: SageScriptDispatch = harness["dispatch"]
	world.add_waypoint("Synthetic_Beacon_Waypoint", Vector3(120.0, 0.0, -40.0))

	_check(
		"a_one_argument_border_shroud_is_rejected",
		_act(harness, "ENABLE_BORDER_SHROUD", [_player_arg("Synthetic_Player")])
		== Dispatch.Status.BAD_ARGUMENTS
		and dispatch.gaps.has(
			"action", "ENABLE_BORDER_SHROUD", GapLog.REASON_BAD_ARGUMENTS
		),
		str(dispatch.gaps.to_lines())
	)
	_check(
		"a_two_argument_reveal_is_rejected",
		_act(harness, "MAP_REVEAL_AT_WAYPOINT", [
			_name_arg("Synthetic_Beacon_Waypoint"), _real_arg(25.0)
		]) == Dispatch.Status.BAD_ARGUMENTS,
		str(dispatch.gaps.to_lines())
	)
	_check(
		"a_two_argument_shroud_is_rejected",
		_act(harness, "MAP_SHROUD_AT_WAYPOINT", [
			_name_arg("Synthetic_Beacon_Waypoint"), _real_arg(30.0)
		]) == Dispatch.Status.BAD_ARGUMENTS,
		str(dispatch.gaps.to_lines())
	)
	_check(
		"no_rejected_call_reached_the_world",
		world.calls.is_empty(),
		str(world.calls)
	)


# --- 6. The names that stay blocked ------------------------------------------


func _test_the_unservable_names_still_block() -> void:
	# MAP_REVEAL_ALL needs a whole-map target the fog surface does not carry;
	# MAP_REVEAL_PERMANENTLY_AT_WAYPOINT authors a REVEAL_NAME the undo pair
	# depends on, and no name->region registry exists. Both must report
	# BLOCKED with a gap naming the fog-of-war surface, and never reach the
	# world - see wp02_blocked_fog_of_war.gd for the full grouping.
	var harness := _harness()
	var world: FogWorld = harness["world"]
	var dispatch: SageScriptDispatch = harness["dispatch"]
	world.add_waypoint("Synthetic_Beacon_Waypoint", Vector3(120.0, 0.0, -40.0))

	var all_status := _act(harness, "MAP_REVEAL_ALL", [_player_arg("Synthetic_Player")])
	_check(
		"map_reveal_all_reports_blocked",
		all_status == Dispatch.Status.BLOCKED,
		"status=%d" % all_status
	)
	_check(
		"the_blocked_gap_names_the_fog_of_war_surface",
		dispatch.gaps.has("action", "MAP_REVEAL_ALL", GapLog.REASON_BLOCKED_SUBSYSTEM)
		and _gap_detail(
			dispatch, "action", "MAP_REVEAL_ALL", GapLog.REASON_BLOCKED_SUBSYSTEM
		).contains("fog of war"),
		str(dispatch.gaps.to_lines())
	)

	var named_status := _act(harness, "MAP_REVEAL_PERMANENTLY_AT_WAYPOINT", [
		_name_arg("Synthetic_Beacon_Waypoint"),
		_real_arg(25.0),
		_player_arg("Synthetic_Player"),
		_name_arg("Synthetic_Reveal_Name"),
	])
	_check(
		"a_named_permanent_reveal_reports_blocked",
		named_status == Dispatch.Status.BLOCKED
		and dispatch.gaps.has(
			"action",
			"MAP_REVEAL_PERMANENTLY_AT_WAYPOINT",
			GapLog.REASON_BLOCKED_SUBSYSTEM
		),
		"status=%d gaps=%s" % [named_status, str(dispatch.gaps.to_lines())]
	)
	_check(
		"blocked_names_never_reach_the_world",
		world.calls.is_empty(),
		str(world.calls)
	)


# --- Stub world ---------------------------------------------------------------
#
# The shared SageScriptWorldStub teaches no Areas or Fog facet, so the slice
# this runner needs is taught HERE rather than in the shared stub (an existing
# file other packages also depend on). Every call is recorded as a flat string
# in world-signature order so a test asserts on the exact arguments that
# reached the world; every read answers from a fixture or REFUSES, so a test
# that forgot to set one gets a loud refusal rather than a plausible reveal.
# Positions and radii are formatted "%.1f" so the fixture key and the
# assertion string cannot drift apart on float printing.
#
# THE POSITION-ONLY RULE IS THE POINT: the stub fog refuses any non-POSITION
# target kind, because the real SliceFog target reader silently answers
# center-ZERO/radius-100 for anything else - a handler that passed a raw
# waypoint or area target would reveal around the origin in production, so it
# must FAIL here.


class FogWorld:
	extends SageScriptWorldStub

	var calls: Array[String] = []

	## waypoint name -> Vector3 world position. Only fixture waypoints resolve.
	var waypoint_positions: Dictionary = {}

	func add_waypoint(waypoint: String, position: Vector3) -> void:
		waypoint_positions[waypoint] = position

	func _make_areas() -> SageScriptWorld.Areas:
		return FogStubAreas.new()

	func _make_fog() -> SageScriptWorld.Fog:
		return FogStubFog.new()


class FogStubAreas:
	extends SageScriptWorld.Areas

	func waypoint_position(waypoint: String) -> SageWorldQuery:
		var stub := world as FogWorld
		stub.calls.append("areas.waypoint_position|%s" % waypoint)
		if not stub.waypoint_positions.has(waypoint):
			return _refuse_query(
				"areas.waypoint_position", "no fixture waypoint '%s'" % waypoint
			)
		return SageWorldQuery.hit(stub.waypoint_positions[waypoint])


class FogStubFog:
	extends SageScriptWorld.Fog

	func _position_target_text(method: String, target: Dictionary) -> String:
		## "" when the target is not a POSITION target - the caller must refuse.
		if int(target.get("kind", -1)) != SageScriptWorld.TargetKind.POSITION:
			return ""
		var position: Vector3 = target.get("position", Vector3.ZERO)
		return "%s|position:(%.1f, %.1f, %.1f)|radius=%.1f" % [
			method,
			position.x,
			position.y,
			position.z,
			float(target.get("radius", -1.0)),
		]

	func reveal(player: String, target: Dictionary, permanent: bool) -> bool:
		var stub := world as FogWorld
		var text := _position_target_text("fog.reveal|%s" % player, target)
		if text == "":
			stub.calls.append("fog.reveal|NON_POSITION_TARGET")
			return _refuse_command(
				"fog.reveal",
				"fixture models only POSITION targets - the slice fog surface reads no other kind"
			)
		stub.calls.append("%s|permanent=%s" % [text, "true" if permanent else "false"])
		return true

	func shroud(player: String, target: Dictionary) -> bool:
		var stub := world as FogWorld
		var text := _position_target_text("fog.shroud|%s" % player, target)
		if text == "":
			stub.calls.append("fog.shroud|NON_POSITION_TARGET")
			return _refuse_command(
				"fog.shroud",
				"fixture models only POSITION targets - the slice fog surface reads no other kind"
			)
		stub.calls.append(text)
		return true

	func set_border_shroud(enabled: bool) -> bool:
		(world as FogWorld).calls.append(
			"fog.set_border_shroud|%s" % ("true" if enabled else "false")
		)
		return true


# --- Reporting ----------------------------------------------------------------


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
	_watchdog.note(name)
	if condition:
		passed += 1
		print("HANDLERS_WP24 PASS %s" % name)
	else:
		failed += 1
		printerr(
			"HANDLERS_WP24 FAIL %s%s" % [name, " (%s)" % detail if detail != "" else ""]
		)
