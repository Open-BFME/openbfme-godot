extends SceneTree

## Regression runner for the PRODUCTION WIRING of the script layer - the seam
## that steps SageScriptExecutors inside RetailSliceSim.tick() so the retail
## AI actually executes in a match.
##
## What is under test, by subject:
##
##   1. INERT BY DEFAULT. A sim with no registered executor must be
##      bit-identical to the pre-wiring engine: no state key, no hash
##      movement, no work. (The frozen b177804c pin proves the same property
##      against the real frozen scenario; this runner proves it in isolation.)
##
##   2. THE TICK-ORDERING CONTRACT. A registered executor ticks EXACTLY ONCE
##      per gameplay-advancing sim tick - enforced mechanically by the
##      recorded env/sim clock offset. Paused and decided ticks step no
##      scripts. An out-of-band executor.tick() (any caller other than
##      sim.tick()) and an executor whose tick advances the clock by anything
##      but 1 are both quarantined loudly.
##
##   3. ENV LIFETIME. An env attached to a freed sim refuses every access
##      loudly instead of writing into an orphaned dictionary; an executor
##      with such an env refuses to tick; registration refuses an executor
##      whose env is not attached to THIS sim.
##
##   4. WIRING LIFECYCLE. Freed executors are reported and dropped, never
##      skipped silently; setup() (the player-reachable match reset) rebases
##      the clock offsets and lifts quarantines.
##
##   5. PEER ADOPTION THROUGH THE WIRED PATH. Peer B adopts A's mid-match
##      snapshot, rebuilds its wiring from the same match configuration, and
##      both peers continue in agreement for 100 further ticks with the
##      script layer LIVE - counters advancing, scripts firing, a timer
##      expiring on the same absolute tick for both.
##
## Every fixture is SYNTHETIC; no retail install or content pack is required.
## NOTE: several tests intentionally trigger push_error lines (marked
## EXPECTED ERROR below) - they are the loud refusals under test.
##
## Invocation:
##   Godot_v4.7-stable_win64_console.exe --headless --path game \
##     --script res://tests/retail_script_wiring_runner.gd

const SimScript = preload("res://src/retail_slice/retail_slice_sim.gd")
const WorldScript = preload("res://src/retail_slice/retail_slice_script_world.gd")
const ExecutorScript = preload("res://src/script/script_executor.gd")
const EnvScript = preload("res://src/script/script_env.gd")
const ParamTypes = preload("res://src/script/script_param_types.gd")

const PLAYER := "WiredPlayer"
const ENEMY := "WiredEnemy"

## The 12.0 s timer armed on interpreter tick 1 has 120 ticks remaining;
## env.advance() decrements it on ticks 2..121, so TIMER_EXPIRED first answers
## true on tick 121. Literals, per the timer-unit incident rule.
const TIMER_EXPIRY_TICK := 121
const ADOPTION_TICK := 60
const ADOPTION_RUN_TICKS := 100

## An executor subclass whose tick advances the interpreter clock TWICE - the
## "executor misbehaves" direction of the cadence contract.
class DoubleAdvanceExecutor:
	extends "res://src/script/script_executor.gd"

	func tick() -> void:
		super.tick()
		env.advance()


## LIVENESS. A GDScript runtime error aborts the enclosing function on the spot
## without propagating, so every `_check` after the error site never runs and
## `failed` never increments - an inert runner prints a zero-failure result and
## exits 0. Pinning the number of checks a healthy run makes turns that silent
## abort into a loud failure. Raise it deliberately when tests are added; never
## lower it to make a run go green.
const EXPECTED_CHECKS := 111

var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	_test_inert_by_default()
	_test_wired_scripts_step_once_per_sim_tick()
	_test_frozen_ticks_step_no_scripts()
	_test_registration_refusals()
	_test_registration_refuses_wrong_team_slot()
	_test_out_of_band_executor_tick_is_quarantined()
	_test_double_advancing_executor_is_quarantined()
	_test_freed_executor_is_reported_and_dropped()
	_test_env_of_freed_sim_refuses_loudly()
	_test_match_reset_rebases_the_wiring()
	_test_in_place_restore_rebases_the_wiring()
	_test_peer_adoption_through_the_wired_path()
	var ran := passed + failed
	if ran != EXPECTED_CHECKS:
		failed += 1
		printerr("RETAIL_SCRIPT_WIRING FAIL liveness: ran %d checks, expected %d - a function aborted before its assertions" % [ran, EXPECTED_CHECKS])
	print("RETAIL_SCRIPT_WIRING_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
	else:
		failed += 1
		printerr("FAIL %s%s" % [name, "" if detail == "" else " (%s)" % detail])


# --- Fixtures (the state-boundary runner's synthetic harness) ---------------


func _unit_rule(horde_id: String, is_builder: bool) -> Dictionary:
	return {
		"horde_id": horde_id,
		"speed": 1.0,
		"speed_source": 10.0,
		"acceleration": 1.0,
		"acceleration_source": 10.0,
		"turn_rate_degrees_per_second": 180.0,
		"braking": 1.0,
		"braking_source": 10.0,
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
		"is_builder": is_builder,
	}


func _harness_rules() -> Dictionary:
	return {
		"enable_base_loop": true,
		"starting_resources": 10000,
		"ai_attack_delay_ticks": 100000,
		"unit_rules": {
			SimScript.SOLDIER_OBJECT_ID: _unit_rule(SimScript.SOLDIER_HORDE_ID, false),
			SimScript.ARCHER_OBJECT_ID: _unit_rule(SimScript.ARCHER_OBJECT_ID, false),
			SimScript.TOWER_GUARD_OBJECT_ID: _unit_rule(SimScript.TOWER_GUARD_OBJECT_ID, false),
			SimScript.KNIGHT_OBJECT_ID: _unit_rule(SimScript.KNIGHT_OBJECT_ID, false),
			SimScript.BUILDER_OBJECT_ID: _unit_rule(SimScript.BUILDER_OBJECT_ID, true),
		},
	}


func _make_sim() -> RetailSliceSim:
	var sim: RetailSliceSim = SimScript.new()
	sim._rules = _harness_rules()
	sim.setup({}, {})
	sim.ai_enabled = false
	return sim


func _make_world(sim: RetailSliceSim) -> RetailSliceScriptWorld:
	var world: RetailSliceScriptWorld = WorldScript.new(sim)
	world.bind_player(PLAYER, SimScript.PLAYER_TEAM)
	world.bind_player(ENEMY, SimScript.ENEMY_TEAM)
	world.bind_script_player(PLAYER)
	return world


# --- Decoded-payload fixtures in the exact sage_scb.py shape ----------------


func _argument(argument_type: int, integer: int = 0, real: float = 0.0, text: String = "") -> Dictionary:
	return {"argumentType": argument_type, "integer": integer, "real": real, "text": text}


func _condition(opcode: String, arguments: Array) -> Dictionary:
	return {
		"name": "Condition",
		"version": 6,
		"value": {
			"contentType": 0,
			"internalName": {"name": opcode, "wireTypeCode": 3},
			"arguments": arguments,
			"enabled": true,
			"inverted": false,
		},
	}


func _action(opcode: String, arguments: Array) -> Dictionary:
	return {
		"name": "ScriptAction",
		"version": 3,
		"value": {
			"contentType": 0,
			"internalName": {"name": opcode, "wireTypeCode": 3},
			"arguments": arguments,
			"enabled": true,
		},
	}


func _or_condition(conditions: Array) -> Dictionary:
	return {"name": "OrCondition", "version": 1, "value": {"records": conditions}}


func _script(name: String, records: Array, options: Dictionary = {}) -> Dictionary:
	return {
		"name": name,
		"comment": "",
		"conditionsComment": "",
		"actionsComment": "",
		"isActive": bool(options.get("isActive", true)),
		"deactivateUponSuccess": bool(options.get("deactivateUponSuccess", false)),
		"activeInEasy": true,
		"activeInMedium": true,
		"activeInHard": true,
		"isSubroutine": bool(options.get("isSubroutine", false)),
		"evaluationInterval": int(options.get("evaluationInterval", 0)),
		"actionsFireSequentially": false,
		"loopActions": false,
		"loopCount": 0,
		"sequentialTargetType": 1,
		"sequentialTargetName": "",
		"scope": "ALL",
		"records": records,
	}


## Team 0's program: a per-tick counter, a one-shot arm (counter + 12.0 s
## timer + its own enable bit), a timer-expiry gate, a subroutine tally after
## the gate fires, and a SIM MUTATION (PLAYER_SET_MONEY through the wired
## world) every 3 authored seconds - so the wired path is proven against
## hashed sim rows, not just env state.
func _player_payloads() -> Array:
	var counter_c := _argument(ParamTypes.ARGUMENT_COUNTER_NAME, 0, 0.0, "c")
	var counter_war := _argument(ParamTypes.ARGUMENT_COUNTER_NAME, 0, 0.0, "war_chest")
	var counter_timer := _argument(ParamTypes.ARGUMENT_COUNTER_NAME, 0, 0.0, "assault")
	var counter_tally := _argument(ParamTypes.ARGUMENT_COUNTER_NAME, 0, 0.0, "tally")
	var flag_launched := _argument(ParamTypes.ARGUMENT_FLAG_NAME, 0, 0.0, "assault_launched")
	var true_arg := _argument(ParamTypes.ARGUMENT_BOOLEAN, 1)
	return [
		_script("Arm", [
			_or_condition([_condition("CONDITION_TRUE", [])]),
			_action("SET_COUNTER", [counter_war, _argument(ParamTypes.ARGUMENT_INTEGER, 3)]),
			_action("SET_MILLISECOND_TIMER", [counter_timer, _argument(ParamTypes.ARGUMENT_REAL, 0, 12.0)]),
		], {"deactivateUponSuccess": true}),
		_script("Pump", [
			_or_condition([_condition("CONDITION_TRUE", [])]),
			_action("INCREMENT_COUNTER", [_argument(ParamTypes.ARGUMENT_INTEGER, 1), counter_c]),
		]),
		_script("Assault Gate", [
			_or_condition([_condition("TIMER_EXPIRED", [counter_timer])]),
			_action("SET_FLAG", [flag_launched, true_arg]),
		], {"deactivateUponSuccess": true}),
		_script("Call Sub", [
			_or_condition([_condition("FLAG", [flag_launched, true_arg])]),
			_action("CALL_SUBROUTINE", [_argument(99, 0, 0.0, "Sub Tally")]),
		]),
		_script("Sub Tally", [
			_or_condition([_condition("CONDITION_TRUE", [])]),
			_action("INCREMENT_COUNTER", [_argument(ParamTypes.ARGUMENT_INTEGER, 1), counter_tally]),
		], {"isSubroutine": true}),
		_script("Fund", [
			_or_condition([_condition("CONDITION_TRUE", [])]),
			_action("PLAYER_SET_MONEY", [
				_argument(ParamTypes.ARGUMENT_PLAYER, 0, 0.0, PLAYER),
				_argument(ParamTypes.ARGUMENT_INTEGER, 7777),
			]),
		], {"evaluationInterval": 3}),
	]


## Team 1's program: a per-tick counter and its own money write on a different
## phase, so multi-executor step order is exercised every tick.
func _enemy_payloads() -> Array:
	var counter_ce := _argument(ParamTypes.ARGUMENT_COUNTER_NAME, 0, 0.0, "ce")
	return [
		_script("Pump E", [
			_or_condition([_condition("CONDITION_TRUE", [])]),
			_action("INCREMENT_COUNTER", [_argument(ParamTypes.ARGUMENT_INTEGER, 1), counter_ce]),
		]),
		_script("Fund E", [
			_or_condition([_condition("CONDITION_TRUE", [])]),
			_action("PLAYER_SET_MONEY", [
				_argument(ParamTypes.ARGUMENT_PLAYER, 0, 0.0, ENEMY),
				_argument(ParamTypes.ARGUMENT_INTEGER, 5555),
			]),
		], {"evaluationInterval": 7}),
	]


func _wire_player_executor(sim: RetailSliceSim) -> SageScriptExecutor:
	var world := _make_world(sim)
	var executor: SageScriptExecutor = ExecutorScript.new(world)
	executor.load_script_payloads(_player_payloads())
	_check("fixture: player env attaches", sim.attach_script_env(executor.env, SimScript.PLAYER_TEAM))
	_check("fixture: player executor registers", sim.register_script_executor(executor, SimScript.PLAYER_TEAM))
	return executor


func _wire_enemy_executor(sim: RetailSliceSim) -> SageScriptExecutor:
	var world: RetailSliceScriptWorld = WorldScript.new(sim)
	world.bind_player(PLAYER, SimScript.PLAYER_TEAM)
	world.bind_player(ENEMY, SimScript.ENEMY_TEAM)
	world.bind_script_player(ENEMY)
	var executor: SageScriptExecutor = ExecutorScript.new(world)
	executor.load_script_payloads(_enemy_payloads())
	_check("fixture: enemy env attaches", sim.attach_script_env(executor.env, SimScript.ENEMY_TEAM))
	_check("fixture: enemy executor registers", sim.register_script_executor(executor, SimScript.ENEMY_TEAM))
	return executor


# --- 1. Inert by default ----------------------------------------------------


func _test_inert_by_default() -> void:
	var sim := _make_sim()
	var twin := _make_sim()
	for _tick in 50:
		sim.tick()
		twin.tick()
	_check("no registration: nothing is registered", sim.registered_script_executor_teams().is_empty())
	_check("no registration: zero wiring faults", sim.script_wiring_faults == 0)
	_check(
		"no registration: no script state key in the snapshot",
		not (bytes_to_var(sim.snapshot()) as Dictionary).has("script_env_state")
	)
	_check("no registration: twin sims agree on the hash", sim.state_hash() == twin.state_hash())


# --- 2. The tick-ordering contract ------------------------------------------


func _test_wired_scripts_step_once_per_sim_tick() -> void:
	var sim := _make_sim()
	var twin := _make_sim()  # identical but unwired: the do-nothing baseline
	var executor := _wire_player_executor(sim)
	var enemy := _wire_enemy_executor(sim)
	_check(
		"registered teams report in ascending order",
		sim.registered_script_executor_teams() == [SimScript.PLAYER_TEAM, SimScript.ENEMY_TEAM]
	)
	var cadence_held := true
	for _tick in 30:
		sim.tick()
		twin.tick()
		if executor.env.tick_index != sim.tick_index or enemy.env.tick_index != sim.tick_index:
			cadence_held = false
	_check(
		"each registered executor ticks exactly once per sim tick (env clock == sim clock)",
		cadence_held
	)
	_check("the per-tick script advanced its counter once per tick", executor.env.counter("c") == 30)
	_check("the second executor ran too", enemy.env.counter("ce") == 30)
	# "Fund" (3.0 s interval = 30 interpreter ticks) fired on tick 30 and set
	# team 0's money to 7777 - far below the 10000 the unwired twin holds -
	# so the wired path demonstrably reaches HASHED sim rows, not just env
	# state. (< 9000, not == 7777: post-script economy income lands after.)
	_check(
		"wired scripts mutate hashed sim rows (money write landed)",
		sim.resources_for_team(SimScript.PLAYER_TEAM) < 9000
		and sim.resources_for_team(SimScript.PLAYER_TEAM) < twin.resources_for_team(SimScript.PLAYER_TEAM)
	)
	_check("a scripted match hashes differently from an unscripted twin", sim.state_hash() != twin.state_hash())
	_check("a clean wired run has zero faults", sim.script_wiring_faults == 0)


func _test_frozen_ticks_step_no_scripts() -> void:
	var sim := _make_sim()
	var executor := _wire_player_executor(sim)
	for _tick in 5:
		sim.tick()
	_check("fixture: five live ticks ran scripts", executor.env.counter("c") == 5)

	# A paused clock advances neither the sim tick nor the scripts.
	sim.clock_paused = true
	for _tick in 3:
		sim.tick()
	_check("paused ticks advance no sim clock", sim.tick_index == 5)
	_check("paused ticks step no scripts", executor.env.counter("c") == 5)
	sim.clock_paused = false

	# A decided match consumes sim ticks but freezes gameplay AND scripts.
	sim.winner = SimScript.PLAYER_TEAM
	for _tick in 3:
		sim.tick()
	_check("decided-match ticks advance the sim clock", sim.tick_index == 8)
	_check("decided-match ticks step no scripts", executor.env.counter("c") == 5)

	# Frozen ticks must not poison the cadence contract: the sim and env
	# clocks legitimately drifted apart (8 vs 5), and on resume the scripts
	# pick up cleanly - the tracked expectation follows the ENV clock, not a
	# constant offset from the sim clock.
	sim.winner = -1
	sim.tick()
	_check("scripts resume cleanly after the freeze", executor.env.counter("c") == 6)
	_check("the frozen stretch caused no wiring faults", sim.script_wiring_faults == 0)


# --- Registration refusals --------------------------------------------------


func _test_registration_refusals() -> void:
	var sim := _make_sim()
	# EXPECTED ERROR: null executor.
	_check("null executor refused", not sim.register_script_executor(null, SimScript.PLAYER_TEAM))
	# EXPECTED ERROR: env not attached to any sim.
	var standalone: SageScriptExecutor = ExecutorScript.new()
	_check(
		"executor with a standalone env refused",
		not sim.register_script_executor(standalone, SimScript.PLAYER_TEAM)
	)
	# EXPECTED ERROR: env attached to a DIFFERENT sim - running it here would
	# mutate state this sim never hashes.
	var other := _make_sim()
	var strayer: SageScriptExecutor = ExecutorScript.new()
	_check("fixture: stray env attaches to the other sim", other.attach_script_env(strayer.env, SimScript.PLAYER_TEAM))
	_check(
		"executor whose env is attached to another sim refused",
		not sim.register_script_executor(strayer, SimScript.PLAYER_TEAM)
	)
	# EXPECTED ERROR: unrostered team.
	var homeless: SageScriptExecutor = ExecutorScript.new()
	_check("fixture: env attaches for the unrostered-team probe", sim.attach_script_env(homeless.env, SimScript.PLAYER_TEAM))
	_check("unrostered team refused", not sim.register_script_executor(homeless, 99))
	# EXPECTED ERROR: a second live executor on the same team.
	var first := _wire_player_executor(sim)
	var second: SageScriptExecutor = ExecutorScript.new()
	_check("fixture: second env attaches", sim.attach_script_env(second.env, SimScript.ENEMY_TEAM))
	_check(
		"a live duplicate registration for a team is refused",
		not sim.register_script_executor(second, SimScript.PLAYER_TEAM)
	)
	_check("the first registration survives the duplicate probe", first.env.attached_to(sim))
	_check("the refusals registered nothing extra", sim.registered_script_executor_teams() == [SimScript.PLAYER_TEAM])


func _test_registration_refuses_wrong_team_slot() -> void:
	## Regression for the 0dce37e adversarial review: register_script_executor
	## verified the env was attached to THIS sim but never that it was attached
	## UNDER THE REGISTRATION TEAM. attach(env, PLAYER_TEAM) followed by
	## register(executor, ENEMY_TEAM) was ACCEPTED - the executor ran in team
	## 1's step slot while its scripts wrote team 0's state key, and a swapped
	## PAIR silently inverted the ascending-team-order guarantee with zero
	## wiring faults. The mismatch must refuse loudly at registration.
	var sim := _make_sim()
	var strayer: SageScriptExecutor = ExecutorScript.new()
	strayer.load_script_payloads(_player_payloads())
	_check(
		"fixture: the stray env attaches under PLAYER_TEAM",
		sim.attach_script_env(strayer.env, SimScript.PLAYER_TEAM)
	)
	# EXPECTED ERROR: env keyed to team 0, registration asked for team 1.
	_check(
		"an env attached under team 0 cannot register under team 1",
		not sim.register_script_executor(strayer, SimScript.ENEMY_TEAM)
	)
	_check(
		"the refused registration registered nothing",
		sim.registered_script_executor_teams().is_empty()
	)
	_check(
		"the same executor still registers under its OWN team",
		sim.register_script_executor(strayer, SimScript.PLAYER_TEAM)
	)

	# The swapped pair from the review: both cross-registrations refuse; the
	# straight ones then succeed, keeping slot order and state keys aligned.
	var sim2 := _make_sim()
	var a: SageScriptExecutor = ExecutorScript.new()
	a.load_script_payloads(_player_payloads())
	var b: SageScriptExecutor = ExecutorScript.new()
	b.load_script_payloads(_enemy_payloads())
	_check("fixture: a attaches under team 0", sim2.attach_script_env(a.env, SimScript.PLAYER_TEAM))
	_check("fixture: b attaches under team 1", sim2.attach_script_env(b.env, SimScript.ENEMY_TEAM))
	# EXPECTED ERRORS: the swap is refused in both directions.
	_check(
		"the swapped pair is refused in both directions",
		not sim2.register_script_executor(a, SimScript.ENEMY_TEAM)
		and not sim2.register_script_executor(b, SimScript.PLAYER_TEAM)
	)
	_check(
		"the straight registrations still succeed after the refused swap",
		sim2.register_script_executor(a, SimScript.PLAYER_TEAM)
		and sim2.register_script_executor(b, SimScript.ENEMY_TEAM)
	)
	for _tick in 3:
		sim2.tick()
	_check(
		"the correctly slotted pair runs clean (no faults, both stepped)",
		sim2.script_wiring_faults == 0
		and a.env.tick_index == 3 and b.env.tick_index == 3
	)


# --- Cadence violations -----------------------------------------------------


func _test_out_of_band_executor_tick_is_quarantined() -> void:
	var sim := _make_sim()
	var executor := _wire_player_executor(sim)
	for _tick in 3:
		sim.tick()
	_check("fixture: clean cadence before the violation", executor.env.counter("c") == 3)
	# THE VIOLATION: something other than sim.tick() ticks the executor.
	executor.tick()
	_check("fixture: the out-of-band tick moved the env clock", executor.env.tick_index == 4)
	# EXPECTED ERROR: the next sim tick detects the clock skew and quarantines.
	sim.tick()
	_check("the violation is counted as a wiring fault", sim.script_wiring_faults == 1)
	_check("the violated executor is quarantined", bool(sim._script_executor_faults.get(SimScript.PLAYER_TEAM, false)))
	var counter_after_quarantine := executor.env.counter("c")
	for _tick in 3:
		sim.tick()
	_check(
		"a quarantined executor's scripts stop (no silent re-sync)",
		executor.env.counter("c") == counter_after_quarantine
	)
	_check("the quarantine reports once, not per tick", sim.script_wiring_faults == 1)


func _test_double_advancing_executor_is_quarantined() -> void:
	var sim := _make_sim()
	var world := _make_world(sim)
	var executor := DoubleAdvanceExecutor.new(world)
	executor.load_script_payloads(_player_payloads())
	_check("fixture: double-advance env attaches", sim.attach_script_env(executor.env, SimScript.PLAYER_TEAM))
	_check("fixture: double-advance executor registers", sim.register_script_executor(executor, SimScript.PLAYER_TEAM))
	# EXPECTED ERROR: one executor tick moved the env clock by 2, not 1.
	sim.tick()
	_check("a step that advances the clock by 2 is a wiring fault", sim.script_wiring_faults == 1)
	_check(
		"the misbehaving executor is quarantined",
		bool(sim._script_executor_faults.get(SimScript.PLAYER_TEAM, false))
	)
	var counter_after := executor.env.counter("c")
	sim.tick()
	_check("its scripts no longer run", executor.env.counter("c") == counter_after)


# --- Lifecycle: freed executor, freed sim -----------------------------------


func _test_freed_executor_is_reported_and_dropped() -> void:
	var sim := _make_sim()
	var executor := _wire_player_executor(sim)
	sim.tick()
	_check("fixture: the executor ran while alive", executor.env.counter("c") == 1)
	executor = null  # the owner drops it; the sim holds only a weakref
	# EXPECTED ERROR: freed while registered.
	sim.tick()
	_check("a freed executor is a counted wiring fault", sim.script_wiring_faults == 1)
	_check("its registration is dropped", sim.registered_script_executor_teams().is_empty())
	sim.tick()
	_check("the report happens once, not per tick", sim.script_wiring_faults == 1)


func _test_env_of_freed_sim_refuses_loudly() -> void:
	var sim := _make_sim()
	var env: SageScriptEnv = EnvScript.new()
	_check("fixture: env attaches", sim.attach_script_env(env, SimScript.PLAYER_TEAM))
	env.set_counter("gold", 5)
	var orphan_store: Dictionary = sim.script_env_state
	_check("fixture: the write landed while the sim lived", env.counter("gold") == 5)
	_check("a live attachment is not stale", not env.attachment_stale())
	sim = null  # the sim is freed; the store the env holds becomes an orphan

	_check("the attachment reports stale once the sim is freed", env.attachment_stale())
	# EXPECTED ERROR (once): the first orphaned access reports loudly.
	_check("reads refuse instead of answering from the orphan", env.counter("gold") == 0)
	env.set_counter("gold", 9)
	_check(
		"writes refuse instead of mutating the orphan",
		int((orphan_store.get(SimScript.PLAYER_TEAM, {}) as Dictionary).get("counters", {}).get("gold", 0)) == 5
	)
	_check("the stale env still refuses reads after the write attempt", env.counter("gold") == 0)

	# An executor built over a stale env refuses to tick, loudly and counted.
	var stale_owner := _make_sim()
	var world := _make_world(stale_owner)
	var executor: SageScriptExecutor = ExecutorScript.new(world)
	_check("fixture: executor env attaches", stale_owner.attach_script_env(executor.env, SimScript.PLAYER_TEAM))
	executor.load_script_payloads(_player_payloads())
	executor.tick()
	_check("fixture: the executor ticks while its sim lives", executor.env.tick_index == 1)
	# Sever the executor -> world -> sim chain so dropping the local actually
	# frees the sim (in production the sim only ever dies WITH its world).
	executor.world = null
	world = null
	stale_owner = null  # frees the sim out from under the executor's env
	# EXPECTED ERROR (once): tick refused on a stale env.
	executor.tick()
	executor.tick()
	_check("ticking against a freed sim is refused and counted", executor.stale_env_tick_refusals == 2)
	_check("no interpreter tick happened against the orphan", executor.env.tick_index == 0)


# --- Match reset ------------------------------------------------------------


func _test_match_reset_rebases_the_wiring() -> void:
	var sim := _make_sim()
	var executor := _wire_player_executor(sim)
	for _tick in 10:
		sim.tick()
	_check("fixture: pre-reset scripts ran", executor.env.counter("c") == 10)

	sim.setup({}, {})  # the player-reachable reset (reset_match routes here)
	sim.ai_enabled = false
	_check("reset clears the script-engine state", sim.script_env_state.is_empty())
	for _tick in 5:
		sim.tick()
	_check(
		"after a reset the wiring keeps cadence (env clock == sim clock)",
		executor.env.tick_index == sim.tick_index and sim.tick_index == 5
	)
	_check("the authored one-shots re-fired after the reset", executor.env.counter("war_chest") == 3)
	_check("the per-tick counter restarted cleanly", executor.env.counter("c") == 5)
	_check("no faults from a reset", sim.script_wiring_faults == 0)

	# A reset also lifts a quarantine: the clocks are consistent again.
	executor.tick()  # EXPECTED ERROR on the sim tick below: out-of-band tick
	sim.tick()
	_check("fixture: quarantined before the reset", bool(sim._script_executor_faults.get(SimScript.PLAYER_TEAM, false)))
	sim.setup({}, {})
	sim.ai_enabled = false
	_check("a reset lifts the quarantine", sim._script_executor_faults.is_empty())
	sim.tick()
	_check(
		"the reset wiring runs cleanly again",
		executor.env.counter("c") == 1 and executor.env.tick_index == sim.tick_index
	)


func _test_in_place_restore_rebases_the_wiring() -> void:
	## The control-server save/load path: a WIRED sim restores an EARLIER
	## snapshot of itself mid-match. The env clock jumps backwards with the
	## snapshot (it is hashed state); the tracked cadence expectation must be
	## rebased from the restored value or the next tick would spuriously
	## quarantine a perfectly healthy executor.
	var sim := _make_sim()
	var executor := _wire_player_executor(sim)
	for _tick in 10:
		sim.tick()
	var save := sim.snapshot()
	for _tick in 5:
		sim.tick()
	_check("fixture: the match advanced past the save", executor.env.tick_index == 15)
	_check("fixture: the sim restores its own snapshot", sim.restore(save))
	_check("the env clock rewound with the snapshot", executor.env.tick_index == 10)
	for _tick in 3:
		sim.tick()
	_check("scripts keep running after an in-place restore", executor.env.counter("c") == 13)
	_check("an in-place restore causes no wiring faults", sim.script_wiring_faults == 0)


# --- Peer adoption through the wired path -----------------------------------


func _test_peer_adoption_through_the_wired_path() -> void:
	## Peer A runs a scripted match to ADOPTION_TICK through sim.tick() (the
	## production seam, both teams scripted). Peer B builds a FRESH sim,
	## adopts A's snapshot, rebuilds the same wiring from the same match
	## configuration, and both peers then advance ADOPTION_RUN_TICKS more
	## ticks in lockstep with the script layer live.
	var sim_a := _make_sim()
	var executor_a := _wire_player_executor(sim_a)
	var enemy_a := _wire_enemy_executor(sim_a)
	for _tick in ADOPTION_TICK:
		sim_a.tick()
	_check("fixture: A's scripts ran to the adoption point", executor_a.env.counter("c") == ADOPTION_TICK)

	var sim_b := _make_sim()
	_check("fixture: B adopts A's snapshot", sim_b.restore(sim_a.snapshot()))
	var executor_b := _wire_player_executor(sim_b)
	var enemy_b := _wire_enemy_executor(sim_b)
	_check("adoption: hashes agree before any further tick", sim_a.state_hash() == sim_b.state_hash())
	_check(
		"adoption: B's executor reads the adopted interpreter clock",
		executor_b.env.tick_index == ADOPTION_TICK
	)

	var first_divergence := -1
	var expiry_a := -1
	var expiry_b := -1
	for _step in ADOPTION_RUN_TICKS:
		sim_a.tick()
		sim_b.tick()
		if expiry_a < 0 and executor_a.env.flag("assault_launched"):
			expiry_a = executor_a.env.tick_index
		if expiry_b < 0 and executor_b.env.flag("assault_launched"):
			expiry_b = executor_b.env.tick_index
		if first_divergence < 0 and sim_a.state_hash() != sim_b.state_hash():
			first_divergence = sim_a.tick_index
	_check(
		"peers agree on the state hash on every one of the %d post-adoption ticks" % ADOPTION_RUN_TICKS,
		first_divergence < 0,
		"first divergence at sim tick %d" % first_divergence
	)
	_check(
		"the AI actually ran: counters advanced through the window",
		executor_a.env.counter("c") == ADOPTION_TICK + ADOPTION_RUN_TICKS
		and executor_b.env.counter("c") == ADOPTION_TICK + ADOPTION_RUN_TICKS
		and enemy_a.env.counter("ce") == enemy_b.env.counter("ce")
	)
	_check(
		"the AI actually ran: scripts fired on the adopting peer",
		executor_b.scripts_fired > 0 and int(executor_b.action_outcomes.get("ok", 0)) > 0
	)
	_check(
		"the timer A armed expires on the same absolute tick for both peers",
		expiry_a == TIMER_EXPIRY_TICK and expiry_b == TIMER_EXPIRY_TICK,
		"a=%d b=%d" % [expiry_a, expiry_b]
	)
	_check(
		"the post-expiry subroutine tallies identically on both peers",
		executor_a.env.counter("tally") == executor_b.env.counter("tally")
		and executor_a.env.counter("tally") == ADOPTION_TICK + ADOPTION_RUN_TICKS - TIMER_EXPIRY_TICK + 1
	)
	_check("no wiring faults on either peer", sim_a.script_wiring_faults == 0 and sim_b.script_wiring_faults == 0)
