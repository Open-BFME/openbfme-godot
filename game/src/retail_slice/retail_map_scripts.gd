class_name RetailMapScripts
extends RefCounted

## Deterministic tier-1 interpreter for decoded SAGE WorldBuilder map scripts.
##
## ============================================================================
## ARCHIVED REFERENCE - FROZEN, NOT A LIVE LANE. DO NOT EXTEND.
## ============================================================================
## Superseded by game/src/script/ (vocabulary-driven dispatch, script_executor.gd).
## Kept, deliberately, because this is the ONLY code that ever demonstrated a
## SAGE interpreter reaching into the simulation and changing it: the
## PLAYER_SET_MONEY family writes team_resources, which feeds
## RetailSliceSim.state_hash(). That worked example is the reason it is worth
## more as a reference than as 308 deleted lines.
##
## Its condition model and load-time fail-closed accounting were carried over
## into script_executor.gd. Its hand-rolled opcode switch was NOT, and must not
## be: duplicating the vocabulary in a match statement is exactly what the
## dispatch system exists to prevent.
##
## The proof runner (game/tests/retail_map_script_runner.gd) is kept GREEN on
## purpose. If it ever fails, this reference has rotted and that is worth
## knowing before anyone trusts it again.
##
## Full rationale: docs/reference/tier1-map-script-interpreter.md
## Owner decision 2026-07-26: archive in place, do not delete.
## ============================================================================
##
## Input format: decoded Script payload dictionaries exactly as produced by
## the skirmish contract extractor
## (.private/retail-work/reports/skirmish-script-contract/skirmish_script_contract.json,
## sources[i].scripts[j].payload), which mirrors the importer's sage_scb JSON.
##
## Execution model: call step(sim) once per sim tick after sim.tick(). Every
## active, non-subroutine script evaluates its condition blocks (OrCondition
## records are OR'd together; Condition records inside one block are AND'd),
## then executes its ScriptAction records on true or ScriptActionFalse records
## on false. Interpreter state (counters, flags, timers, script activity) is
## fully deterministic; the only sim mutation implemented so far is the
## PLAYER_SET_MONEY action family, which writes team_resources and therefore
## participates in RetailSliceSim.state_hash().
##
## Fail-closed accounting: every opcode occurrence in a loaded script is
## recorded into either the `implemented` or `unimplemented` histogram at load
## time, and every unimplemented opcode reached during evaluation/execution is
## additionally counted in `runtime_unimplemented`. Nothing is skipped
## silently.

# Matches RetailSliceSim.TICK_SECONDS (10 logic ticks per second).
const TICK_SECONDS := 0.1

# SAGE comparison enum used by the COUNTER condition's argumentType-6 slot.
const COMPARE_LESS := 0
const COMPARE_LESS_EQUAL := 1
const COMPARE_EQUAL := 2
const COMPARE_GREATER_EQUAL := 3
const COMPARE_GREATER := 4
const COMPARE_NOT_EQUAL := 5

# Decoded argument type codes observed in the tier-1 skirmish contract.
const ARGUMENT_INTEGER := 0
const ARGUMENT_REAL := 1
const ARGUMENT_COUNTER_NAME := 4
const ARGUMENT_FLAG_NAME := 5
const ARGUMENT_COMPARISON := 6
const ARGUMENT_BOOLEAN := 8
const ARGUMENT_PLAYER := 11

const IMPLEMENTED_CONDITIONS := {
	"CONDITION_TRUE": true,
	"CONDITION_FALSE": true,
	"COUNTER": true,
	"FLAG": true,
	"TIMER_EXPIRED": true,
}

const IMPLEMENTED_ACTIONS := {
	"SET_COUNTER": true,
	"INCREMENT_COUNTER": true,
	"SET_FLAG": true,
	"SET_MILLISECOND_TIMER": true,
	"PLAYER_SET_MONEY": true,
}

# Interpreter variable state. Counter and flag namespaces are shared across
# all loaded scripts, matching the retail per-player script environment.
var counters: Dictionary = {}
var flags: Dictionary = {}
# Timer name -> absolute interpreter tick at which the timer expires. A timer
# stays expired once its tick has passed, matching retail TIMER_EXPIRED.
var timers: Dictionary = {}
var tick_index: int = 0

# Player-reference text (for example "<This Player>") -> sim team integer.
var player_team_bindings: Dictionary = {}
var default_player_team: int = 0

# Load-time opcode census (fail-closed accounting).
var implemented: Dictionary = {}
var unimplemented: Dictionary = {}
# Unimplemented opcodes actually reached while stepping.
var runtime_unimplemented: Dictionary = {}

var _scripts: Array[Dictionary] = []


func load_contract_source(source: Dictionary) -> int:
	## Loads every script from one `sources[i]` entry of the contract JSON.
	var loaded := 0
	for row: Variant in Array(source.get("scripts", [])):
		var payload: Dictionary = (row as Dictionary).get("payload", {})
		if not payload.is_empty():
			load_script_payload(payload)
			loaded += 1
	return loaded


func load_script_payloads(payloads: Array) -> int:
	var loaded := 0
	for payload: Variant in payloads:
		if payload is Dictionary and not (payload as Dictionary).is_empty():
			load_script_payload(payload)
			loaded += 1
	return loaded


func load_script_payload(payload: Dictionary) -> void:
	var condition_blocks: Array[Array] = []
	var true_actions: Array[Dictionary] = []
	var false_actions: Array[Dictionary] = []
	for record: Variant in Array(payload.get("records", [])):
		var record_row: Dictionary = record
		var record_name := String(record_row.get("name", ""))
		var value: Dictionary = record_row.get("value", {})
		if record_name == "OrCondition":
			var block: Array[Dictionary] = []
			for child: Variant in Array(value.get("records", [])):
				var child_row: Dictionary = child
				if String(child_row.get("name", "")) == "Condition":
					block.append(_content_row(child_row.get("value", {}), true))
			condition_blocks.append(block)
		elif record_name == "ScriptAction":
			true_actions.append(_content_row(value, false))
		elif record_name == "ScriptActionFalse":
			false_actions.append(_content_row(value, false))
	var interval_seconds := float(payload.get("evaluationInterval", 0))
	_scripts.append({
		"name": String(payload.get("name", "")),
		"active": bool(payload.get("isActive", true)),
		"subroutine": bool(payload.get("isSubroutine", false)),
		"deactivate_upon_success": bool(payload.get("deactivateUponSuccess", false)),
		"eval_interval_ticks": maxi(1, int(round(interval_seconds / TICK_SECONDS))) if interval_seconds > 0.0 else 1,
		"condition_blocks": condition_blocks,
		"true_actions": true_actions,
		"false_actions": false_actions,
	})


func script_count() -> int:
	return _scripts.size()


func active_script_names() -> Array[String]:
	var names: Array[String] = []
	for state in _scripts:
		if bool(state["active"]):
			names.append(String(state["name"]))
	return names


func step(sim) -> void:
	## Advances the interpreter by one tick against the supplied RetailSliceSim.
	tick_index += 1
	for state in _scripts:
		if not bool(state["active"]) or bool(state["subroutine"]):
			continue
		var interval := int(state["eval_interval_ticks"])
		if interval > 1 and tick_index % interval != 0:
			continue
		var fired := _evaluate_condition_blocks(state["condition_blocks"])
		var actions: Array[Dictionary] = state["true_actions"] if fired else state["false_actions"]
		for action in actions:
			if bool(action["enabled"]):
				_execute_action(action, sim)
		if fired and bool(state["deactivate_upon_success"]):
			state["active"] = false


func _content_row(value: Dictionary, is_condition: bool) -> Dictionary:
	var internal: Dictionary = value.get("internalName", {})
	var opcode := String(internal.get("name", "<missing>"))
	var row := {
		"opcode": opcode,
		"arguments": Array(value.get("arguments", [])),
		"enabled": bool(value.get("enabled", true)),
	}
	if is_condition:
		row["inverted"] = bool(value.get("inverted", false))
	var known: bool = (
		IMPLEMENTED_CONDITIONS.has(opcode) if is_condition else IMPLEMENTED_ACTIONS.has(opcode)
	)
	var bucket := implemented if known else unimplemented
	bucket[opcode] = int(bucket.get(opcode, 0)) + 1
	return row


func _evaluate_condition_blocks(blocks: Array[Array]) -> bool:
	# OrCondition blocks are OR'd; conditions inside one block are AND'd.
	# A script without any condition block never fires (fail-closed).
	for block: Array in blocks:
		var block_true := true
		var considered := false
		for condition: Dictionary in block:
			if not bool(condition["enabled"]):
				continue
			considered = true
			if not _evaluate_condition(condition):
				block_true = false
				break
		if block_true and considered:
			return true
	return false


func _evaluate_condition(condition: Dictionary) -> bool:
	var opcode := String(condition["opcode"])
	var arguments: Array = condition["arguments"]
	var inverted := bool(condition.get("inverted", false))
	var result: bool
	match opcode:
		"CONDITION_TRUE":
			result = true
		"CONDITION_FALSE":
			result = false
		"COUNTER":
			var name := _argument_text(arguments, ARGUMENT_COUNTER_NAME)
			var comparison := _argument_integer(arguments, ARGUMENT_COMPARISON, COMPARE_EQUAL)
			var value := _argument_integer(arguments, ARGUMENT_INTEGER, 0)
			result = _compare(int(counters.get(name, 0)), comparison, value)
		"FLAG":
			var name := _argument_text(arguments, ARGUMENT_FLAG_NAME)
			var expected := _argument_integer(arguments, ARGUMENT_BOOLEAN, 1) != 0
			result = bool(flags.get(name, false)) == expected
		"TIMER_EXPIRED":
			var name := _argument_text(arguments, ARGUMENT_COUNTER_NAME)
			result = timers.has(name) and tick_index >= int(timers[name])
		_:
			# Fail-closed: an unimplemented condition makes its AND-block
			# false regardless of inversion, and the miss is counted.
			runtime_unimplemented[opcode] = int(runtime_unimplemented.get(opcode, 0)) + 1
			return false
	return not result if inverted else result


func _execute_action(action: Dictionary, sim) -> void:
	var opcode := String(action["opcode"])
	var arguments: Array = action["arguments"]
	match opcode:
		"SET_COUNTER":
			counters[_argument_text(arguments, ARGUMENT_COUNTER_NAME)] = _argument_integer(arguments, ARGUMENT_INTEGER, 0)
		"INCREMENT_COUNTER":
			# Retail argument order is [amount, counter]; resolve by type.
			var name := _argument_text(arguments, ARGUMENT_COUNTER_NAME)
			counters[name] = int(counters.get(name, 0)) + _argument_integer(arguments, ARGUMENT_INTEGER, 1)
		"SET_FLAG":
			flags[_argument_text(arguments, ARGUMENT_FLAG_NAME)] = _argument_integer(arguments, ARGUMENT_BOOLEAN, 1) != 0
		"SET_MILLISECOND_TIMER":
			# UNIT: the argument is SECONDS despite the action's name.
			#
			# Measured, not assumed: across 1,510 decoded msec-family actions in
			# 142 retail .map files the values mass at 3/5/10/60/300/1800 and
			# never approach 30000, and the player-visible countdowns only make
			# sense as seconds (a "Timer - Daybreak" of 1800 is 30 minutes, not
			# 1.8). Fractional values (0.1, 10.5) also rule out frames, and the
			# vocabulary table declares the parameter REAL.
			#
			# Reading it as milliseconds - which this file did until the
			# measurement was made - divides every retail timer by 1000, and the
			# maxi(1, ...) floor then collapses all of them to a single tick. The
			# 30-minute daybreak timer expired in 0.1 seconds.
			var name := _argument_text(arguments, ARGUMENT_COUNTER_NAME)
			var seconds := _argument_real(arguments, ARGUMENT_REAL, 0.0)
			timers[name] = tick_index + maxi(1, int(ceil(seconds / TICK_SECONDS)))
		"PLAYER_SET_MONEY":
			var player := _argument_text(arguments, ARGUMENT_PLAYER)
			var amount := _argument_integer(arguments, ARGUMENT_INTEGER, 0)
			var team := int(player_team_bindings.get(player, default_player_team))
			sim.team_resources[team] = amount
		_:
			runtime_unimplemented[opcode] = int(runtime_unimplemented.get(opcode, 0)) + 1


func _compare(left: int, comparison: int, right: int) -> bool:
	match comparison:
		COMPARE_LESS:
			return left < right
		COMPARE_LESS_EQUAL:
			return left <= right
		COMPARE_EQUAL:
			return left == right
		COMPARE_GREATER_EQUAL:
			return left >= right
		COMPARE_GREATER:
			return left > right
		COMPARE_NOT_EQUAL:
			return left != right
	return false


func _argument_text(arguments: Array, argument_type: int) -> String:
	for argument: Variant in arguments:
		var row: Dictionary = argument
		if int(row.get("argumentType", -1)) == argument_type:
			return String(row.get("text", ""))
	return ""


func _argument_integer(arguments: Array, argument_type: int, fallback: int) -> int:
	for argument: Variant in arguments:
		var row: Dictionary = argument
		if int(row.get("argumentType", -1)) == argument_type:
			return int(row.get("integer", fallback))
	return fallback


func _argument_real(arguments: Array, argument_type: int, fallback: float) -> float:
	for argument: Variant in arguments:
		var row: Dictionary = argument
		if int(row.get("argumentType", -1)) == argument_type:
			return float(row.get("real", fallback))
	return fallback
