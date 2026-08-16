extends SceneTree

## SECOND state pin: the frozen scenario WITH the script layer wired and live.
##
## Emits one line:  RETAIL_SCRIPTED_STATE_PIN ticks=<n> hash=<sha256>
##
## The original pin (retail_state_pin_runner.gd, b177804c...) proves a match
## with NO scripts is bit-identical to the pre-wiring engine - the wiring's
## inert-by-default guarantee. It cannot, by design, protect anything the
## scripts DO. This pin is the other half: a frozen scripted scenario running
## through the production seam (RetailSliceSim._step_script_executors, i.e.
## sim.tick() steps the executors - never executor.run_ticks), so any change
## to script evaluation order, interval phasing, timer conversion, the
## env/sim tick cadence, handler behaviour, or the seam's position inside the
## tick moves this hash and is caught.
##
## The fixture is DELIBERATELY FROZEN and self-contained, like the first
## pin: it shares no builders with any other runner, because a pin whose
## scenario can drift is not a pin. If this fixture must change, treat it as
## minting a NEW pin and say so explicitly.
##
## SELF-CHECKING: unlike the first pin (whose hash CI compares across
## platforms), this runner carries its expected hash and fails on mismatch.
## It also proves the pin is STABLE, not merely recorded: two independently
## constructed sims are driven through the identical scenario and must agree
## on every-100th-tick checkpoints and the final hash - and the scripts are
## proven LIVE (counters advanced, the timer expired, money writes landed),
## so a silently inert script layer cannot fake a green pin.
##
## Invocation:
##   Godot_v4.7-stable_win64_console.exe --headless --path game \
##     --script res://tests/retail_scripted_state_pin_runner.gd

const SimScript = preload("res://src/retail_slice/retail_slice_sim.gd")
const WorldScript = preload("res://src/retail_slice/retail_slice_script_world.gd")
const ExecutorScript = preload("res://src/script/script_executor.gd")

const PIN_TICKS := 1500
## Minted 2026-07-27 from the frozen scenario below (Godot 4.7-stable,
## Windows); proven identical across repeated runs and across two
## independently constructed sims in the same run.
##
## DELIBERATELY NOT RE-MINTED, 2026-08-04 (round 18). This pin was ALREADY RED
## before round 18 for an undiagnosed reason: on this tree it measured
## 269127f3...9cdf against the pinned b0acc61e...3774, and the round-17 report
## recorded that same pre-existing mismatch. Round 18's eviction fix moved it
## again, 269127f3... -> 0ae2055f..., from the SAME single cause as the sibling
## `retail_state_pin_runner` (bisected there: reverting `_step_structure_eviction`
## alone restores the previous value exactly;
## .private/scratch/opus25-retail_scripted_state_pin_runner-REVERT-*.out.log).
##
## Re-minting to 0ae2055f... would bury the OLDER drift under a fresh green,
## and that older drift has never been explained. The honest state is: this pin
## is red, it is red for two stacked reasons, one of them is named above and one
## is not, and it stays red until the unnamed one is diagnosed on its own.
##
## ROUND 19: STILL NOT RE-MINTED, and this round did NOT move it. Measured, not
## assumed: round 19's four structure-pathing hunks were each reverted alone on
## this tree and this runner re-run, and the value is 0ae2055f...14c2 in all six
## variants — new, revert-slide, revert-prodexit, revert-engaged, revert-budget,
## revert-all (.private/scratch/opus26-pin-scripted-*.out.log). So the red is
## unchanged and still has exactly the two stacked causes named above; round 19
## added no third. See the sibling runner's round-19 note for why this fixture
## does not exercise the new pathing at its sampled tick, and for the coverage
## gap that follows from a pin that only ever samples the final state.
##
## RE-MINT 2026-08-15 - EXPLICIT CUMULATIVE BASELINE, NOT CREEP-ONLY.
## Superseded value: b0acc61e2a60c4b8e810986b168db12055e8a42ae92bc551dd8df01135c33774
## New value:        b0a3b163289930cbe9d24d4e4cad97f70b8315acfd4a970797d2de905412bfc0
##
## A recursive mint-commit -> current snapshot audit found exactly 20 changed
## leaves. Sixteen predate this tranche: empty damage-component, permanent
## weapon-lock and weapon-mode state on four entities; empty banner/damage
## registries; the accepted fog-reveal closure; and entity 3's documented
## structure-eviction displacement. The remaining four are the inert creep
## keys retired above. HEAD -> working tree differs by only those four keys,
## and reinserting them reproduces HEAD's 0ae2055f...14c2 byte-for-byte.
const EXPECTED_HASH := "b0a3b163289930cbe9d24d4e4cad97f70b8315acfd4a970797d2de905412bfc0"

const PLAYER := "PinPlayer"
const ENEMY := "PinEnemy"

## The 30.0 s timer armed on interpreter tick 1 has 300 ticks remaining;
## env.advance() decrements it on ticks 2..301, so TIMER_EXPIRED first
## answers true on tick 301. Literals, per the timer-unit incident rule.
const TIMER_EXPIRY_TICK := 301

# Decoded-argument type codes, frozen locally (a pin does not import
# constants that could drift under it).
const ARG_INT := 0
const ARG_REAL := 1
const ARG_COUNTER := 4
const ARG_FLAG := 5
const ARG_BOOLEAN := 8
const ARG_PLAYER := 11


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var run_a := _drive_scenario()
	var run_b := _drive_scenario()  # independently constructed, same scenario

	var failures: Array[String] = []
	if not bool(run_a["commands_accepted"]) or not bool(run_b["commands_accepted"]):
		failures.append("a scripted command was rejected during submission")
	if run_a["checkpoints"] != run_b["checkpoints"]:
		failures.append("two independently constructed sims diverged at a checkpoint")
	if String(run_a["hash"]) != String(run_b["hash"]):
		failures.append("two independently constructed sims disagree on the final hash")
	failures.append_array(run_a["liveness_failures"] as Array[String])
	if int(run_a["wiring_faults"]) != 0 or int(run_b["wiring_faults"]) != 0:
		failures.append("the wired run reported script wiring faults")
	if String(run_a["hash"]) != EXPECTED_HASH:
		failures.append(
			"hash %s does not match the pinned %s" % [String(run_a["hash"]), EXPECTED_HASH]
		)

	print("RETAIL_SCRIPTED_STATE_PIN ticks=%d hash=%s" % [PIN_TICKS, String(run_a["hash"])])
	if failures.is_empty():
		quit(0)
		return
	for failure in failures:
		printerr("RETAIL_SCRIPTED_STATE_PIN FAIL %s" % failure)
	quit(1)


func _drive_scenario() -> Dictionary:
	var sim = _make_sim()
	var player_executor = _wire_executor(sim, PLAYER, SimScript.PLAYER_TEAM, _player_payloads())
	var enemy_executor = _wire_executor(sim, ENEMY, SimScript.ENEMY_TEAM, _enemy_payloads())

	var accepted := true
	for cmd in _scripted_log():
		accepted = sim.submit_command(cmd) and accepted

	var checkpoints: Array[String] = []
	for tick in range(1, PIN_TICKS + 1):
		sim.tick()
		if tick % 100 == 0:
			checkpoints.append(sim.state_hash())

	# LIVENESS: the pin is meaningless if the scripts silently did nothing.
	var liveness_failures: Array[String] = []
	if player_executor.env.counter("c") != PIN_TICKS:
		liveness_failures.append(
			"per-tick counter is %d, expected %d" % [player_executor.env.counter("c"), PIN_TICKS]
		)
	if not player_executor.env.flag("assault_launched"):
		liveness_failures.append("the 30 s timer never expired")
	if player_executor.env.counter("tally") != PIN_TICKS - TIMER_EXPIRY_TICK + 1:
		liveness_failures.append(
			"subroutine tally is %d, expected %d"
			% [player_executor.env.counter("tally"), PIN_TICKS - TIMER_EXPIRY_TICK + 1]
		)
	if enemy_executor.env.counter("ce") != PIN_TICKS:
		liveness_failures.append("the enemy executor's per-tick counter did not advance")
	if int(player_executor.action_outcomes.get("ok", 0)) <= 0:
		liveness_failures.append("no script action ever executed OK")
	if not (bytes_to_var(sim.snapshot()) as Dictionary).has("script_env_state"):
		liveness_failures.append("the snapshot carries no script state")

	return {
		"hash": sim.state_hash(),
		"checkpoints": checkpoints,
		"commands_accepted": accepted,
		"liveness_failures": liveness_failures,
		"wiring_faults": sim.script_wiring_faults,
		# Keep the wiring alive until the run is measured (the sim holds the
		# executors by weakref only).
		"executors": [player_executor, enemy_executor],
	}


# --- The frozen simulation fixture (mirrors the b177804c pin's harness) -----


func _make_sim():
	var sim = SimScript.new()
	sim._rules = _harness_rules()
	sim.setup({}, {})
	sim.ai_enabled = true
	for structure_id in sim.structure_ids():
		if structure_id != 1003:
			sim.structures.erase(structure_id)
	for entity_id in sim.entity_ids():
		if not [1, 2, 3, 101].has(entity_id):
			sim.entities.erase(entity_id)
	sim.expansion_pads.clear()
	sim._ai_production_plan.clear()
	sim.force_ai_construction_complete()
	return sim


func _harness_rules() -> Dictionary:
	return {
		"enable_base_loop": true,
		"starting_resources": 10000,
		"ai_attack_delay_ticks": 4000,
		"unit_rules": {
			SimScript.SOLDIER_OBJECT_ID: _unit_rule(SimScript.SOLDIER_HORDE_ID, false),
			SimScript.ARCHER_OBJECT_ID: _unit_rule(SimScript.ARCHER_OBJECT_ID, false),
			SimScript.TOWER_GUARD_OBJECT_ID: _unit_rule(SimScript.TOWER_GUARD_OBJECT_ID, false),
			SimScript.KNIGHT_OBJECT_ID: _unit_rule(SimScript.KNIGHT_OBJECT_ID, false),
			SimScript.BUILDER_OBJECT_ID: _unit_rule(SimScript.BUILDER_OBJECT_ID, true),
		},
	}


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


func _command(tick: int, seq: int, type: String, args: Dictionary, team: int = 0) -> Dictionary:
	return {"tick": tick, "team": team, "seq": seq, "type": type, "args": args}


func _scripted_log() -> Array[Dictionary]:
	return [
		_command(1, 1, "issue_move", {"ids": [1], "destination": Vector2(-20.0, -14.0)}),
		_command(3, 2, "issue_attack_move", {"ids": [2], "destination": Vector2(0.0, 14.0)}),
		_command(5, 3, "issue_toggle_stance", {"ids": [1]}),
		_command(7, 4, "issue_construct", {"ids": [3], "structure_kind": "farm", "position": Vector2(-28.0, -8.0)}),
		_command(9, 5, "queue_unit", {"producer": 1003, "unit_type": "bfme2.object.gondor-fighter-horde"}),
		_command(15, 6, "issue_attack", {"ids": [1], "target_id": 101}),
		_command(30, 8, "issue_stop", {"ids": [2]}),
		_command(30, 7, "issue_move", {"ids": [2], "destination": Vector2(-5.0, 12.0)}),
		_command(700, 9, "issue_set_stance", {"ids": [1], "stance": "HoldGround"}),
		_command(900, 10, "issue_set_formation", {"ids": [2], "formation": "Block"}),
		_command(1100, 11, "issue_stop", {"ids": [1, 2]}),
	]


# --- The frozen script program ----------------------------------------------


func _wire_executor(sim, script_player: String, team: int, payloads: Array):
	var world = WorldScript.new(sim)
	world.bind_player(PLAYER, SimScript.PLAYER_TEAM)
	world.bind_player(ENEMY, SimScript.ENEMY_TEAM)
	world.bind_script_player(script_player)
	var executor = ExecutorScript.new(world)
	executor.load_script_payloads(payloads)
	if not sim.attach_script_env(executor.env, team):
		printerr("RETAIL_SCRIPTED_STATE_PIN FAIL env attach refused for team %d" % team)
		quit(1)
	if not sim.register_script_executor(executor, team):
		printerr("RETAIL_SCRIPTED_STATE_PIN FAIL executor registration refused for team %d" % team)
		quit(1)
	return executor


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


## Team 0's program: env state of every kind (counter, flag, timer, enable
## bit, subroutine) PLUS a sim mutation through the wired world
## (PLAYER_SET_MONEY every 3 authored seconds = 30 interpreter ticks). The
## money write is what makes the seam's POSITION inside the tick pinned:
## economy income and AI spending land after the scripts, so moving
## _step_script_executors() anywhere else in tick() moves this hash.
func _player_payloads() -> Array:
	var counter_c := _argument(ARG_COUNTER, 0, 0.0, "c")
	var counter_war := _argument(ARG_COUNTER, 0, 0.0, "war_chest")
	var counter_timer := _argument(ARG_COUNTER, 0, 0.0, "assault")
	var counter_tally := _argument(ARG_COUNTER, 0, 0.0, "tally")
	var flag_launched := _argument(ARG_FLAG, 0, 0.0, "assault_launched")
	var true_arg := _argument(ARG_BOOLEAN, 1)
	return [
		_script("Pin Arm", [
			_or_condition([_condition("CONDITION_TRUE", [])]),
			_action("SET_COUNTER", [counter_war, _argument(ARG_INT, 3)]),
			_action("SET_MILLISECOND_TIMER", [counter_timer, _argument(ARG_REAL, 0, 30.0)]),
		], {"deactivateUponSuccess": true}),
		_script("Pin Pump", [
			_or_condition([_condition("CONDITION_TRUE", [])]),
			_action("INCREMENT_COUNTER", [_argument(ARG_INT, 1), counter_c]),
		]),
		_script("Pin Assault Gate", [
			_or_condition([_condition("TIMER_EXPIRED", [counter_timer])]),
			_action("SET_FLAG", [flag_launched, true_arg]),
		], {"deactivateUponSuccess": true}),
		_script("Pin Call Sub", [
			_or_condition([_condition("FLAG", [flag_launched, true_arg])]),
			_action("CALL_SUBROUTINE", [_argument(99, 0, 0.0, "Pin Sub Tally")]),
		]),
		_script("Pin Sub Tally", [
			_or_condition([_condition("CONDITION_TRUE", [])]),
			_action("INCREMENT_COUNTER", [_argument(ARG_INT, 1), counter_tally]),
		], {"isSubroutine": true}),
		_script("Pin Fund", [
			_or_condition([_condition("CONDITION_TRUE", [])]),
			_action("PLAYER_SET_MONEY", [
				_argument(ARG_PLAYER, 0, 0.0, PLAYER),
				_argument(ARG_INT, 2500),
			]),
		], {"evaluationInterval": 3}),
	]


## Team 1's program: its own per-tick counter and a money write on a
## different phase (7 s = 70 ticks) that CONSTRAINS the AI's economy - the
## enemy team runs the built-in AI controllers, so its production decisions
## depend on the money the script keeps resetting. Script layer and AI
## controllers are thereby pinned INTERACTING, not side by side.
func _enemy_payloads() -> Array:
	var counter_ce := _argument(ARG_COUNTER, 0, 0.0, "ce")
	return [
		_script("Pin Pump E", [
			_or_condition([_condition("CONDITION_TRUE", [])]),
			_action("INCREMENT_COUNTER", [_argument(ARG_INT, 1), counter_ce]),
		]),
		_script("Pin Fund E", [
			_or_condition([_condition("CONDITION_TRUE", [])]),
			_action("PLAYER_SET_MONEY", [
				_argument(ARG_PLAYER, 0, 0.0, ENEMY),
				_argument(ARG_INT, 900),
			]),
		], {"evaluationInterval": 7}),
	]
