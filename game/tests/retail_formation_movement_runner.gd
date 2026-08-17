extends SceneTree
## Retail troop movement + formation semantics.
##
## Proves the three authored behaviours the sim gained behind the opt-in
## "retail_formation_movement" gameplay rule, and — just as importantly — proves
## the gate itself: with the rule absent every path below falls back to the
## previous behaviour byte-for-byte, so the owner-signed 3000-tick pin in
## retail_state_pin_runner.gd cannot move.
##
## Authored sources (PURE RETAIL tree,
## workspace/retail-work/editions/rotwk/cache/effective-assets/data/ini):
##
##   locomotor.ini:709   NormalMeleeHordeLocomotor TurnTime = 2000  -> 180 deg/s
##   locomotor.ini:717   MaxTurnWithoutReform = 45  "Try to turn beyond this
##                       angle, and we will reform instead of wheel"
##   locomotor.ini:723   TurnWhileMoving = No  (so a reform is stationary)
##   locomotor.ini:849   NormalCavalryHordeLocomotor MaxTurnWithoutReform = 100
##   locomotor.ini:713   WaitForFormation = Yes  "When moving into formations,
##                       these guys stop & wait for others."
##   menhordes.ini:109   GondorFighterHorde      RankInfo depth 20 / lateral 20
##   menhordes.ini:350   GondorFighterHordeBlock RankInfo depth 12 / lateral 10
##
## Full extraction with the complete citation set:
## workspace/scratch/opus17-formation-extraction.md
##
## Deterministic by construction: fixed 0.1 s ticks, no wall clock, no RNG draw
## (asserted), and every iteration over a fixed literal id order.

const SimScript = preload("res://src/retail_slice/retail_slice_sim.gd")

## LIVENESS. A GDScript runtime error aborts the enclosing function on the spot
## without propagating, so every `_check` after the error site never runs and
## `failed` never increments — an inert runner prints a zero-failure result and
## exits 0. Pinning the number of checks a healthy run makes turns that silent
## abort into a loud failure. Raise it deliberately when tests are added; never
## lower it to make a run go green.
const EXPECTED_CHECKS := 31

const EPSILON := 0.0001

var passed := 0
var failed := 0

const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
var _runner_watchdog := RunnerWatchdogScript.new()


func _initialize() -> void:
	_runner_watchdog.start(self, "RETAIL_FORMATION_MOVEMENT_RUNNER")
	call_deferred("_run")


# ---------------------------------------------------------------- harness ----


func _make_sim(retail_movement: bool):
	var sim = SimScript.new()
	sim._rules = _harness_rules(retail_movement)
	sim.setup({}, {})
	sim.ai_enabled = false
	# Pure kinematics: no structures means _deflect_around_structures is a no-op,
	# so a position assertion reads the route stepper and nothing else.
	sim.structures.clear()
	sim.expansion_pads.clear()
	return sim


func _harness_rules(retail_movement: bool) -> Dictionary:
	var rules := {
		"unit_rules": {
			SimScript.SOLDIER_OBJECT_ID: _unit_rule(SimScript.SOLDIER_HORDE_ID),
			SimScript.ARCHER_OBJECT_ID: _unit_rule(SimScript.ARCHER_OBJECT_ID),
			SimScript.TOWER_GUARD_OBJECT_ID: _unit_rule(SimScript.TOWER_GUARD_OBJECT_ID),
			SimScript.KNIGHT_OBJECT_ID: _unit_rule(SimScript.KNIGHT_OBJECT_ID),
		},
	}
	if retail_movement:
		rules["retail_formation_movement"] = true
	return rules


func _unit_rule(horde_id: String) -> Dictionary:
	return {
		"horde_id": horde_id,
		"speed": 10.0,
		"speed_source": 100.0,
		# Acceleration and braking far above `speed` so every tick runs at the
		# speed cap; these tests are about heading and caps, not about the ramp.
		"acceleration": 1000.0,
		"acceleration_source": 10000.0,
		"turn_rate_degrees_per_second": 180.0,
		"braking": 1000.0,
		"braking_source": 10000.0,
		"attack_range": 1.15,
		"attack_range_source": 11.5,
		"minimum_attack_range": 0.0,
		"minimum_attack_range_source": 0.0,
		"vision_range": 40.0,
		"vision_range_source": 400.0,
		"delay_between_shots_ms": 600.0,
		"pre_attack_delay_ms": 200.0,
		"firing_duration_ms": 200.0,
		"attack_period_ticks": 10,
		"pre_attack_ticks": 2,
		"firing_duration_ticks": 2,
		"member_damage": 10,
		"member_health": 200,
		"member_count": 1,
		"formation_positions": [Vector3.ZERO],
		"provenance": {},
		"is_builder": false,
	}


func _prepare(sim, id: int, position: Vector2, facing: Vector2, speed: float, category: String) -> Dictionary:
	var row: Dictionary = sim.entities[id]
	row["position"] = position
	row["facing"] = facing
	row["speed"] = speed
	row["current_speed"] = 0.0
	row["state"] = "idle"
	row["target_id"] = 0
	row["attack_move"] = false
	if category != "":
		row["category"] = category
	sim._spatial_sync(row)
	return row


## Straight-line destination `degrees` off the row's current facing, far enough
## that no test in this file ever reaches it.
func _destination_at(row: Dictionary, degrees: float) -> Vector2:
	return Vector2(row["position"]) + Vector2(row["facing"]).rotated(deg_to_rad(degrees)) * 200.0


func _facing_degrees(row: Dictionary) -> float:
	return rad_to_deg(Vector2(row["facing"]).angle())


func _check(condition: bool, name: String, detail: String = "") -> void:
	if condition:
		passed += 1
		return
	failed += 1
	printerr("RETAIL_FORMATION_MOVEMENT FAIL %s%s" % [name, ("" if detail == "" else " :: " + detail)])


func _check_close(actual: float, expected: float, name: String) -> void:
	_check(
		absf(actual - expected) <= EPSILON,
		name,
		"expected %.6f, got %.6f" % [expected, actual]
	)


# ------------------------------------------------------- turn rate / wheel ----


func _test_turn_rate_limits_heading_change() -> void:
	## locomotor.ini:709 TurnTime = 2000 -> 180 deg/s, so one 0.1 s tick may turn
	## at most 18 degrees. A 30-degree order is INSIDE MaxTurnWithoutReform (45),
	## so the horde wheels: it turns AND advances in the same tick.
	var sim = _make_sim(true)
	var row := _prepare(sim, 1, Vector2.ZERO, Vector2.RIGHT, 10.0, "")
	sim.issue_move([1], _destination_at(row, 30.0))
	sim._step_route(row)

	_check_close(_facing_degrees(row), 18.0, "wheel_turns_at_authored_180_deg_per_second")
	_check(
		Vector2(row["position"]).length() > EPSILON,
		"wheel_advances_while_turning",
		"a wheel must not stop the horde; position stayed at origin"
	)
	# Second tick completes the turn: 12 degrees remain, less than the 18-degree
	# per-tick allowance, so it lands exactly on the heading rather than
	# overshooting it.
	sim._step_route(row)
	_check_close(_facing_degrees(row), 30.0, "wheel_lands_on_heading_without_overshoot")


func _test_legacy_default_snaps_heading_in_one_tick() -> void:
	## Gate proof. Without the rule the sim keeps its previous behaviour exactly:
	## facing is assigned from the movement direction in a single tick.
	var sim = _make_sim(false)
	var row := _prepare(sim, 1, Vector2.ZERO, Vector2.RIGHT, 10.0, "")
	sim.issue_move([1], _destination_at(row, 30.0))
	sim._step_route(row)

	_check_close(_facing_degrees(row), 30.0, "legacy_default_snaps_facing_in_one_tick")
	_check(
		not row.has("group_speed_cap"),
		"legacy_default_adds_no_row_keys",
		"group_speed_cap must stay absent so the pinned state hash cannot move"
	)


func _test_reform_beyond_threshold_pivots_in_place() -> void:
	## locomotor.ini:717 "Try to turn beyond this angle, and we will reform
	## instead of wheel", with TurnWhileMoving = No (:723) making the reform
	## stationary. A 90-degree order therefore holds position while the horde
	## pivots down to the 45-degree threshold: 90 -> 72 -> 54 -> 36, so ticks 1-3
	## do not translate and tick 4 does.
	var sim = _make_sim(true)
	var row := _prepare(sim, 1, Vector2.ZERO, Vector2.RIGHT, 10.0, "")
	sim.issue_move([1], _destination_at(row, 90.0))

	for tick_index in range(3):
		sim._step_route(row)
		_check(
			Vector2(row["position"]).length() <= EPSILON,
			"reform_holds_position_tick_%d" % (tick_index + 1),
			"reforming horde translated at %s" % [row["position"]]
		)
	_check_close(_facing_degrees(row), 54.0, "reform_pivots_at_authored_turn_rate")
	_check(
		float(row["current_speed"]) <= EPSILON,
		"reform_bleeds_speed_to_zero",
		"current_speed %.6f" % [float(row["current_speed"])]
	)

	sim._step_route(row)
	_check(
		Vector2(row["position"]).length() > EPSILON,
		"reform_resumes_advance_once_inside_threshold"
	)


func _test_cavalry_reform_threshold_is_wider() -> void:
	## Cavalry-class horde locomotors author MaxTurnWithoutReform = 100
	## (locomotor.ini:849, :871, :893) against infantry's 45, so a 60-degree turn
	## wheels for horse and reforms for foot. Same order, same tick, opposite
	## outcome — which is exactly the retail feel of cavalry sweeping through a
	## corner that infantry stops to square up for.
	var sim = _make_sim(true)
	var horse := _prepare(sim, 1, Vector2.ZERO, Vector2.RIGHT, 10.0, "cavalry")
	var foot := _prepare(sim, 2, Vector2(0.0, 60.0), Vector2.RIGHT, 10.0, "infantry")
	sim.issue_move([1], _destination_at(horse, 60.0))
	sim.issue_move([2], _destination_at(foot, 60.0))
	sim._step_route(horse)
	sim._step_route(foot)

	_check(
		Vector2(horse["position"]).length() > EPSILON,
		"cavalry_wheels_through_60_degrees"
	)
	_check(
		Vector2(foot["position"]).distance_to(Vector2(0.0, 60.0)) <= EPSILON,
		"infantry_reforms_at_60_degrees"
	)


# ---------------------------------------------------- group speed cohesion ----


func _test_group_move_caps_at_slowest_authored_speed() -> void:
	## locomotor.ini:713 WaitForFormation = Yes, "When moving into formations,
	## these guys stop & wait for others." A mixed selection advances at the pace
	## of its slowest member and arrives together instead of stringing out.
	var sim = _make_sim(true)
	var fast := _prepare(sim, 1, Vector2.ZERO, Vector2.RIGHT, 30.0, "")
	var slow := _prepare(sim, 2, Vector2(0.0, 60.0), Vector2.RIGHT, 10.0, "")
	sim.issue_move([1, 2], Vector2(150.0, 0.0))

	_check_close(float(fast.get("group_speed_cap", 0.0)), 10.0, "group_cap_is_the_slowest_speed")
	_check_close(float(slow.get("group_speed_cap", 0.0)), 10.0, "group_cap_applies_to_every_member")

	var fast_start := Vector2(fast["position"])
	var slow_start := Vector2(slow["position"])
	for _tick_index in range(5):
		sim._step_route(fast)
		sim._step_route(slow)
	_check_close(
		Vector2(fast["position"]).distance_to(fast_start),
		Vector2(slow["position"]).distance_to(slow_start),
		"group_members_advance_together"
	)
	_check(
		float(fast["current_speed"]) <= 10.0 + EPSILON,
		"group_cap_holds_the_fast_member_back",
		"current_speed %.6f exceeded the 10.0 group cap" % [float(fast["current_speed"])]
	)


func _test_single_unit_order_is_capped_at_its_own_speed() -> void:
	var sim = _make_sim(true)
	var row := _prepare(sim, 1, Vector2.ZERO, Vector2.RIGHT, 30.0, "")
	sim.issue_move([1], Vector2(150.0, 0.0))
	_check_close(float(row.get("group_speed_cap", 0.0)), 30.0, "solo_order_caps_at_own_speed")
	sim._step_route(row)
	_check_close(float(row["current_speed"]), 30.0, "solo_order_is_not_slowed")


func _test_arrival_releases_the_group_cap() -> void:
	## The cap belongs to one order, not to the unit: a slow escort must not
	## permanently cripple a fast battalion.
	var sim = _make_sim(true)
	var fast := _prepare(sim, 1, Vector2.ZERO, Vector2.RIGHT, 30.0, "")
	var _slow := _prepare(sim, 2, Vector2(0.0, 60.0), Vector2.RIGHT, 10.0, "")
	sim.issue_move([1, 2], Vector2(2.0, 0.0))
	for _tick_index in range(10):
		sim._step_route(fast)
	_check(
		(fast["route"] as Array).is_empty(),
		"group_order_reaches_its_destination"
	)
	_check_close(float(fast.get("group_speed_cap", -1.0)), 0.0, "arrival_releases_the_group_cap")

	# A fresh solo order must re-derive the cap rather than inherit the escort's.
	sim.issue_move([1], Vector2(150.0, 0.0))
	_check_close(float(fast.get("group_speed_cap", 0.0)), 30.0, "reorder_recomputes_the_group_cap")


func _test_legacy_default_has_no_group_cap() -> void:
	var sim = _make_sim(false)
	var fast := _prepare(sim, 1, Vector2.ZERO, Vector2.RIGHT, 30.0, "")
	var slow := _prepare(sim, 2, Vector2(0.0, 60.0), Vector2.RIGHT, 10.0, "")
	sim.issue_move([1, 2], Vector2(150.0, 0.0))
	_check(
		not fast.has("group_speed_cap") and not slow.has("group_speed_cap"),
		"legacy_default_writes_no_group_cap"
	)
	sim._step_route(fast)
	_check_close(float(fast["current_speed"]), 30.0, "legacy_default_leaves_the_fast_unit_fast")


# ------------------------------------------------------- formation spacing ----


func _test_block_formation_uses_authored_anisotropic_spacing() -> void:
	## GondorFighterHorde (menhordes.ini:109-111) authors depth pitch 20 and
	## lateral pitch 20; its AlternateFormation GondorFighterHordeBlock
	## (menhordes.ini:350-352) authors depth pitch 12 and lateral pitch 10.
	## So Block is 0.60 in depth and 0.50 across — not one isotropic squeeze.
	## The runtime adapter swaps axes, so sim x is lateral and sim z is depth.
	var sim = _make_sim(true)
	var row: Dictionary = sim.entities[1]
	row["formation_positions_base"] = [Vector3(2.0, 0.0, 3.0)]
	row["formation_mode"] = "Block"
	sim._apply_formation_mode(row)

	var slot: Vector3 = (row["formation_positions"] as Array)[0]
	_check_close(slot.x, 1.0, "block_lateral_spacing_is_authored_0_50")
	_check_close(slot.z, 1.8, "block_depth_spacing_is_authored_0_60")


func _test_legacy_block_formation_stays_isotropic() -> void:
	var sim = _make_sim(false)
	var row: Dictionary = sim.entities[1]
	row["formation_positions_base"] = [Vector3(2.0, 0.0, 3.0)]
	row["formation_mode"] = "Block"
	sim._apply_formation_mode(row)

	var slot: Vector3 = (row["formation_positions"] as Array)[0]
	_check_close(slot.x, 1.1, "legacy_block_lateral_stays_at_0_55")
	_check_close(slot.z, 1.65, "legacy_block_depth_stays_at_0_55")


# ------------------------------------------------------------ determinism ----


func _test_retail_movement_consumes_no_rng() -> void:
	## The seeded GameLogicRandom state is empty-is-absent (retail_slice_sim.gd
	## :9141), so a single draw on a movement path would change the state hash of
	## every existing fixture. Wheeling, reforming, and group capping must all be
	## pure arithmetic.
	var sim = _make_sim(true)
	var row := _prepare(sim, 1, Vector2.ZERO, Vector2.RIGHT, 10.0, "")
	sim.issue_move([1], _destination_at(row, 135.0))
	for _tick_index in range(20):
		sim._step_route(row)
	_check(
		sim._logic_random_state.is_empty(),
		"retail_movement_consumes_no_rng",
		"movement drew from the seeded logic RNG"
	)


func _test_retail_movement_replays_identically() -> void:
	## Two independent sims, same order, same tick count, identical trajectory.
	var trace_a := _trace_replay()
	var trace_b := _trace_replay()
	_check(trace_a == trace_b, "retail_movement_replays_identically")
	_check(trace_a.size() == 30, "replay_trace_is_the_expected_length", "got %d" % [trace_a.size()])


func _trace_replay() -> Array:
	var sim = _make_sim(true)
	var row := _prepare(sim, 1, Vector2.ZERO, Vector2.RIGHT, 10.0, "")
	sim.issue_move([1], _destination_at(row, 120.0))
	var trace: Array = []
	for _tick_index in range(30):
		sim._step_route(row)
		trace.append(
			[
				snappedf(Vector2(row["position"]).x, 0.000001),
				snappedf(Vector2(row["position"]).y, 0.000001),
				snappedf(_facing_degrees(row), 0.000001),
			]
		)
	return trace


# ------------------------------------------------------------------- run ----


func _run() -> void:
	_test_turn_rate_limits_heading_change()
	_test_legacy_default_snaps_heading_in_one_tick()
	_test_reform_beyond_threshold_pivots_in_place()
	_test_cavalry_reform_threshold_is_wider()
	_test_group_move_caps_at_slowest_authored_speed()
	_test_single_unit_order_is_capped_at_its_own_speed()
	_test_arrival_releases_the_group_cap()
	_test_legacy_default_has_no_group_cap()
	_test_block_formation_uses_authored_anisotropic_spacing()
	_test_legacy_block_formation_stays_isotropic()
	_test_retail_movement_consumes_no_rng()
	_test_retail_movement_replays_identically()

	var total := passed + failed
	if total != EXPECTED_CHECKS:
		failed += 1
		printerr(
			(
				"RETAIL_FORMATION_MOVEMENT FAIL ran %d checks, expected %d. A "
				+ "GDScript runtime error aborts its function silently — this "
				+ "count is the liveness proof, never lower it to go green."
			)
			% [total, EXPECTED_CHECKS]
		)
	print("RETAIL_FORMATION_MOVEMENT passed=%d failed=%d" % [passed, failed])
	quit(1 if failed > 0 else 0)
