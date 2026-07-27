class_name SageScriptExecutor
extends RefCounted

## The component that actually RUNS decoded SAGE map scripts.
##
## Owns decoded Script payloads, decides which scripts evaluate on which tick,
## evaluates their condition blocks, executes their actions through
## SageScriptDispatch, and installs SageScriptEnv.subroutine_runner so
## CALL_SUBROUTINE works. Until this file existed the eight handler packages
## under src/script/ were a complete vocabulary with no caller.
##
## Input format: decoded Script payload dictionaries exactly as produced by
## sage_scb.py (`_parse_script`) - the same shape the tier-1 interpreter
## (src/retail_slice/retail_map_scripts.gd) consumes.
##
##
## HOW THIS DIFFERS FROM THE TIER-1 INTERPRETER, AND WHY
## =====================================================
## retail_map_scripts.gd proves the execution model against a hand-rolled
## opcode switch of 10 opcodes. This executor keeps that file's condition
## model, interval conversion and fail-closed accounting discipline, but
## dispatches every action and condition through SageScriptDispatch instead of
## matching opcodes itself. That is the point: the sourced vocabulary, the
## argument validation, the gap log and all ~8 handler packages are reached by
## the same call path a Lua front end will use, so one handler serves every
## caller and every miss is recorded with full identity. This file contains no
## per-opcode knowledge at all.
##
## It also deliberately does NOT read arguments by argumentType search (a
## tier-1 shortcut): SageScriptArgs reads positionally against the declared
## signature, which several signatures require (see script_args.gd).
##
##
## EVALUATION ORDER (the determinism rule)
## =======================================
## Scripts evaluate in LOAD SEQUENCE ORDER: the exact order payloads were
## appended via load_script_payload(s), which mirrors the document order of
## the decoded ScriptList. Within a script: condition blocks in document order,
## conditions within a block in document order (short-circuit AND), actions in
## document order.
##
## ASSUMPTION, NOT A SOURCED FACT: that document order is also the order the
## RETAIL engine evaluates in. It is the natural reading of the format, and
## retail_map_scripts.gd assumed the same, but no reference was found that says
## so. It is recorded here rather than asserted because the consequence is
## specific and silent: two scripts that touch the same flag or counter in one
## tick resolve by this order, so if retail ordered them differently the
## divergence would show up as subtly wrong AI behaviour, never as an error.
## Determinism is unaffected either way - every peer agrees, they would simply
## all agree on the wrong thing. To settle it, observe a retail map whose
## scripts write the same counter in one tick and compare.
##
## This order is total (array index) and machine-independent because it is a
## pure function of the decoded payload sequence the caller supplies -
## identical bytes on every lockstep peer. It never derives from Dictionary
## iteration, filesystem enumeration, instance ids, hashes or the clock.
## Scripts are stored in an Array and only ever iterated by index; the
## name->index map exists solely for subroutine lookup and is never iterated.
##
##
## EVALUATION INTERVAL
## ===================
## Decoded payloads carry `evaluationInterval` (sage_scb.py: u32, authored in
## WorldBuilder as "evaluate every N seconds"). Measurement of the retail
## corpus found 80.7% of AI scripts authored to evaluate EVERY frame, so the
## default when the field is absent or zero is 1 tick - per-frame. That is the
## authored semantic; do not "improve" it. A veteran modder reports that too
## many per-frame scripts is what crashes retail - which is an argument for
## OBSERVABILITY, not for a different default, so load_report() carries the
## full interval distribution of every loaded script.
##
## A non-zero interval converts seconds -> interpreter ticks through
## env.ticks_per_second at load time (floor 1 tick), matching tier-1's
## conversion. A script with interval N evaluates when env.tick_index % N == 0
## - a global phase, identical on every peer.
##
##
## SUBROUTINES
## ===========
## Scripts flagged isSubroutine NEVER self-evaluate; they run only through
## CALL_SUBROUTINE, which reaches this executor via the subroutine_runner
## Callable installed on the env (see script_env.gd for why that seam is a
## Callable). A call runs the target's FULL body: conditions are evaluated and
## the true or false branch executes, exactly as a scheduled evaluation would.
##
## Decisions this file makes, all refusing honestly and counting:
##   * MISSING subroutine: refused, counted in subroutine_misses; the
##     CALL_SUBROUTINE handler then reports UNSUPPORTED and the dispatcher
##     records a gap naming the subroutine.
##   * RECURSION (direct or via a cycle): refused, counted in
##     subroutine_recursion_refusals. Retail behaviour is unsourced and an
##     unbounded cycle would hang the tick, so the call that would re-enter a
##     script already on the call stack fails instead. Iterative re-calls on
##     later ticks are unaffected.
##   * DISABLED subroutine (env enable bit false): refused, counted in
##     subroutine_disabled_refusals. ASSUMPTION, not a sourced fact: whether
##     retail ENABLE/DISABLE_SCRIPT gates subroutine calls could not be
##     sourced. Gating is chosen because it keeps ONE notion of activity (the
##     env's) for every script; the counter makes the assumption's cost
##     visible if a retail map disagrees.
##   * Target exists but is NOT flagged isSubroutine: executed anyway (name
##     lookup is authoritative), counted in subroutine_calls_to_non_subroutine
##     so the anomaly is visible.
##
##
## SCRIPT ACTIVITY
## ===============
## Enable/disable state lives in SageScriptEnv.script_enabled, which the core
## ENABLE_SCRIPT / DISABLE_SCRIPT handlers already write. This executor keeps
## NO parallel activity flag: each tick it asks
## env.is_script_enabled(name, authored_isActive), so the authored default
## applies until a script-control action overrides it, and
## deactivateUponSuccess writes through the same env bit.
## unresolved_script_references() reports enable bits naming no loaded script,
## so a typo'd ENABLE_SCRIPT target is visible instead of silently inert.
##
##
## FAIL-CLOSED ACCOUNTING (carried over from tier-1, extended)
## ===========================================================
## At LOAD time every content record is classified into exactly one census
## bucket - implemented / unimplemented / blocked / deliberate / unknown -
## by opcode, and structurally invalid records land in malformed_records with
## identity. Nothing is dropped without a count.
## At RUN time every dispatched call lands in action_outcomes /
## condition_outcomes by status, and every non-OK call is recorded by the
## dispatcher's gap log with (kind, name, reason, script, tick). A
## WORLD_REFUSED action (expected today: the default world is the refusing
## base class) is counted, gapped, and the run continues - the remaining
## actions of the script and all later scripts still execute.
##
## KNOWN LIMITS, COUNTED RATHER THAN HIDDEN
## ----------------------------------------
##   * Difficulty flags (activeInEasy/Medium/Hard) are NOT applied - every
##     loaded script runs regardless. Scripts whose flags are not uniformly
##     true are listed in load_report().difficulty_variant_scripts.
##   * actionsFireSequentially / loopActions (sequential scripts) are NOT
##     modelled: their actions execute atomically within the tick. Scripts
##     carrying either flag are listed in load_report().sequential_flagged.
##   * Duplicate script names share one env enable bit and only the FIRST is
##     reachable by subroutine call; duplicates are listed in
##     load_report().duplicate_script_names.

const Dispatch := preload("res://src/script/script_dispatch.gd")
const CoreHandlers := preload("res://src/script/script_handlers_core.gd")
const Registry := preload("res://src/script/handlers/_registry.gd")
const Vocabulary := preload("res://src/script/script_vocabulary.gd")
const EnvScript := preload("res://src/script/script_env.gd")
const WorldScript := preload("res://src/script/script_world.gd")

var env: SageScriptEnv
var world: SageScriptWorld
var dispatch: SageScriptDispatch

## Errors from handler-package registration when this executor built its own
## dispatch. Empty on a healthy startup; never silently discarded.
var registration_errors: Array[String] = []

## Load-time census: bucket name -> {opcode -> occurrence count}. Every content
## record in every loaded script lands in exactly one bucket.
var census: Dictionary = {
	"implemented": {},    # vocabulary entry + handler counted as coverage
	"unimplemented": {},  # vocabulary entry, no handler registered
	"blocked": {},        # handler exists only to report a missing subsystem
	"deliberate": {},     # handler exists only to refuse on purpose
	"unknown": {},        # not in the sourced vocabulary at all
}

## Structurally invalid records dropped at load, each with identity.
var malformed_records: Array[String] = []

## Records authored disabled (enabled=false). Census'd by opcode like any
## other record, skipped at runtime, and counted here so the skip is visible.
var disabled_records: int = 0

var duplicate_script_names: Array[String] = []
var sequential_flagged: Array[String] = []
var difficulty_variant_scripts: Array[String] = []

## interval ticks -> number of non-subroutine scripts authored at it.
var interval_histogram: Dictionary = {}

## Runtime tallies. Actions carry their dispatch status (status label ->
## count). Conditions are a plain evaluation count: evaluate_condition()
## returns only the truth value, and a condition that could not be served has
## already been recorded in the dispatcher's gap log - counting a status here
## would be inventing one.
var action_outcomes: Dictionary = {}
var conditions_evaluated: int = 0
var scripts_fired: int = 0

var subroutine_calls_served: int = 0
var subroutine_misses: Dictionary = {}                  # name -> count
var subroutine_recursion_refusals: Dictionary = {}      # name -> count
var subroutine_disabled_refusals: Dictionary = {}       # name -> count
var subroutine_calls_to_non_subroutine: Dictionary = {} # name -> count

var _scripts: Array[Dictionary] = []
var _script_index_by_name: Dictionary = {}  # first-loaded wins; lookup only
var _subroutine_stack: Array[String] = []


func _init(
	world_in: SageScriptWorld = null,
	dispatch_in: SageScriptDispatch = null,
	env_in: SageScriptEnv = null
) -> void:
	env = env_in if env_in != null else EnvScript.new()
	# The default world is the refusing base class ON PURPOSE: every
	# world-touching action then reports WORLD_REFUSED into the gap log, which
	# is the correct, counted outcome until a real world adapter exists.
	world = world_in if world_in != null else WorldScript.new()
	if dispatch_in != null:
		dispatch = dispatch_in
	else:
		dispatch = Dispatch.new()
		CoreHandlers.register_all(dispatch)
		var outcome := Registry.register_all(dispatch)
		for message: Variant in Array(outcome["errors"]):
			registration_errors.append(String(message))
	# Install the subroutine seam through a weakref so env (owned by this
	# executor) does not hold a strong Callable back to it - a RefCounted
	# cycle would leak, and this repo's leak runner would rightly object.
	var owner: WeakRef = weakref(self)
	env.subroutine_runner = func(subroutine_name: String) -> bool:
		var executor: SageScriptExecutor = owner.get_ref()
		if executor == null:
			return false
		return executor._run_subroutine(subroutine_name)


# --- Loading ---------------------------------------------------------------


func load_script_payloads(payloads: Array) -> int:
	var loaded := 0
	for payload: Variant in payloads:
		if payload is Dictionary and not (payload as Dictionary).is_empty():
			load_script_payload(payload)
			loaded += 1
	return loaded


func load_script_payload(payload: Dictionary) -> void:
	var script_name := String(payload.get("name", ""))
	var condition_blocks: Array = []
	var true_actions: Array = []
	var false_actions: Array = []

	for record: Variant in Array(payload.get("records", [])):
		if not (record is Dictionary):
			_malformed(script_name, "non-dictionary record %s" % str(record))
			continue
		var record_row: Dictionary = record
		var record_name := String(record_row.get("name", ""))
		var value: Dictionary = record_row.get("value", {})
		match record_name:
			"OrCondition":
				var block: Array = []
				for child: Variant in Array(value.get("records", [])):
					if not (child is Dictionary):
						_malformed(script_name, "non-dictionary OrCondition child")
						continue
					var child_row: Dictionary = child
					if String(child_row.get("name", "")) != "Condition":
						_malformed(
							script_name,
							"OrCondition contains '%s'" % String(child_row.get("name", ""))
						)
						continue
					var condition_value: Dictionary = child_row.get("value", {})
					_census_record(Vocabulary.Kind.CONDITION, condition_value)
					block.append(condition_value)
				condition_blocks.append(block)
			"ScriptAction":
				_census_record(Vocabulary.Kind.ACTION, value)
				true_actions.append(value)
			"ScriptActionFalse":
				_census_record(Vocabulary.Kind.ACTION, value)
				false_actions.append(value)
			_:
				_malformed(script_name, "unsupported record '%s'" % record_name)

	var is_subroutine := bool(payload.get("isSubroutine", false))
	var interval_ticks := _interval_ticks(payload)
	if not is_subroutine:
		interval_histogram[interval_ticks] = int(interval_histogram.get(interval_ticks, 0)) + 1
	if bool(payload.get("actionsFireSequentially", false)) or bool(payload.get("loopActions", false)):
		sequential_flagged.append(script_name)
	if not (
		bool(payload.get("activeInEasy", true))
		and bool(payload.get("activeInMedium", true))
		and bool(payload.get("activeInHard", true))
	):
		difficulty_variant_scripts.append(script_name)

	if _script_index_by_name.has(script_name):
		duplicate_script_names.append(script_name)
	else:
		_script_index_by_name[script_name] = _scripts.size()

	_scripts.append({
		"name": script_name,
		"authored_active": bool(payload.get("isActive", true)),
		"subroutine": is_subroutine,
		"deactivate_upon_success": bool(payload.get("deactivateUponSuccess", false)),
		"interval_ticks": interval_ticks,
		"condition_blocks": condition_blocks,
		"true_actions": true_actions,
		"false_actions": false_actions,
	})


func _interval_ticks(payload: Dictionary) -> int:
	## Absent or zero -> 1 tick (per-frame), the measured retail default.
	## Positive -> authored seconds converted through the env's tick rate.
	var interval_seconds := float(payload.get("evaluationInterval", 0))
	if interval_seconds <= 0.0:
		return 1
	return maxi(1, int(round(interval_seconds * env.ticks_per_second)))


func _census_record(kind: int, value: Dictionary) -> void:
	var internal: Dictionary = value.get("internalName", {})
	var opcode := String(internal.get("name", ""))
	if opcode == "":
		# A nameless record is still a real record; it is identified by its
		# numeric contentType and will produce a resolution gap at runtime
		# (no BFME2 id map exists - see script_vocabulary.gd).
		opcode = "contentType=%d" % int(value.get("contentType", -1))
	var bucket := "unknown"
	if Vocabulary.has_entry(kind, opcode):
		var handlers: Dictionary = (
			dispatch.action_handlers
			if kind == Vocabulary.Kind.ACTION
			else dispatch.condition_handlers
		)
		if handlers.has(opcode):
			if dispatch.deliberate_refusals.has(opcode):
				bucket = "deliberate"
			elif dispatch.blocked_registrations.has(opcode):
				bucket = "blocked"
			else:
				bucket = "implemented"
		else:
			bucket = "unimplemented"
	var histogram: Dictionary = census[bucket]
	histogram[opcode] = int(histogram.get(opcode, 0)) + 1
	if not bool(value.get("enabled", true)):
		disabled_records += 1


func _malformed(script_name: String, detail: String) -> void:
	var entry := "script '%s': %s" % [script_name, detail]
	malformed_records.append(entry)
	push_error("SageScriptExecutor: malformed record dropped - %s" % entry)


# --- Ticking ---------------------------------------------------------------


func tick() -> void:
	## One interpreter tick: advance the env (timers, tick counter) then
	## evaluate every eligible non-subroutine script in load order.
	env.advance()
	for index in _scripts.size():
		var script: Dictionary = _scripts[index]
		if bool(script["subroutine"]):
			continue  # subroutines run only via CALL_SUBROUTINE
		if not env.is_script_enabled(String(script["name"]), bool(script["authored_active"])):
			continue
		var interval := int(script["interval_ticks"])
		if interval > 1 and env.tick_index % interval != 0:
			continue
		_execute_script(script)


func run_ticks(count: int) -> void:
	for _i in count:
		tick()


func _execute_script(script: Dictionary) -> bool:
	var script_name := String(script["name"])
	var fired := _conditions_fire(script)
	var actions: Array = script["true_actions"] if fired else script["false_actions"]
	for record: Variant in actions:
		var action: Dictionary = record
		if not bool(action.get("enabled", true)):
			continue  # authored off; already counted in disabled_records
		var status := dispatch.execute_action(action, env, world, script_name)
		_tally(action_outcomes, status)
	if fired:
		scripts_fired += 1
		if bool(script["deactivate_upon_success"]):
			env.set_script_enabled(script_name, false)
	return fired


func _conditions_fire(script: Dictionary) -> bool:
	## OrCondition blocks are OR'd together; Condition records inside one block
	## are AND'd (short-circuit). A script with no condition block, or a block
	## with no enabled condition, never fires from it - fail-closed, matching
	## tier-1. A condition the dispatcher could not evaluate returns false and
	## has already been recorded as a gap, so an unevaluable gate cannot fire.
	var script_name := String(script["name"])
	for block: Variant in Array(script["condition_blocks"]):
		var block_true := true
		var considered := false
		for record: Variant in Array(block):
			var condition: Dictionary = record
			if not bool(condition.get("enabled", true)):
				continue
			considered = true
			conditions_evaluated += 1
			if not dispatch.evaluate_condition(condition, env, world, script_name):
				block_true = false
				break
		if block_true and considered:
			return true
	return false


func _tally(outcomes: Dictionary, status: int) -> void:
	var label := _status_label(status)
	outcomes[label] = int(outcomes.get(label, 0)) + 1


static func _status_label(status: int) -> String:
	match status:
		Dispatch.Status.OK:
			return "ok"
		Dispatch.Status.UNSUPPORTED:
			return "unsupported"
		Dispatch.Status.BAD_ARGUMENTS:
			return "bad-arguments"
		Dispatch.Status.WORLD_REFUSED:
			return "world-refused"
		Dispatch.Status.DELIBERATE:
			return "deliberate"
		Dispatch.Status.BLOCKED:
			return "blocked"
	return "status-%d" % status


# --- Subroutines -----------------------------------------------------------


func _run_subroutine(subroutine_name: String) -> bool:
	## Target of env.subroutine_runner. Returns whether the subroutine was
	## found and executed; false routes back through the CALL_SUBROUTINE
	## handler into an UNSUPPORTED status and a gap naming the subroutine.
	if not _script_index_by_name.has(subroutine_name):
		_count(subroutine_misses, subroutine_name)
		return false
	var script: Dictionary = _scripts[int(_script_index_by_name[subroutine_name])]
	if not env.is_script_enabled(subroutine_name, bool(script["authored_active"])):
		_count(subroutine_disabled_refusals, subroutine_name)
		return false
	if _subroutine_stack.has(subroutine_name):
		_count(subroutine_recursion_refusals, subroutine_name)
		return false
	if not bool(script["subroutine"]):
		_count(subroutine_calls_to_non_subroutine, subroutine_name)
	_subroutine_stack.append(subroutine_name)
	_execute_script(script)
	_subroutine_stack.pop_back()
	subroutine_calls_served += 1
	return true


static func _count(histogram: Dictionary, key: String) -> void:
	histogram[key] = int(histogram.get(key, 0)) + 1


# --- Diagnostics -----------------------------------------------------------


func script_count() -> int:
	return _scripts.size()


func has_script(script_name: String) -> bool:
	return _script_index_by_name.has(script_name)


func unresolved_script_references() -> Array[String]:
	## Enable bits written to the env (by ENABLE_SCRIPT / DISABLE_SCRIPT or
	## anything else) that name no loaded script. A typo'd script reference
	## would otherwise be a bit nobody reads - the definition of silent.
	var out: Array[String] = []
	for name: Variant in env.script_enabled:
		if not _script_index_by_name.has(String(name)):
			out.append(String(name))
	out.sort()
	return out


func load_report() -> Dictionary:
	## Order-stable summary of everything loading observed. The interval
	## distribution is here because per-frame script pressure is a measured
	## retail failure mode: it must be visible, not defaulted away.
	var subroutines := 0
	for script in _scripts:
		if bool(script["subroutine"]):
			subroutines += 1
	return {
		"scripts": _scripts.size(),
		"subroutines": subroutines,
		"interval_histogram_ticks": _sorted_view(interval_histogram),
		"per_frame_scripts": int(interval_histogram.get(1, 0)),
		"census": {
			"implemented": _sorted_view(census["implemented"]),
			"unimplemented": _sorted_view(census["unimplemented"]),
			"blocked": _sorted_view(census["blocked"]),
			"deliberate": _sorted_view(census["deliberate"]),
			"unknown": _sorted_view(census["unknown"]),
		},
		"malformed_records": malformed_records.duplicate(),
		"disabled_records": disabled_records,
		"duplicate_script_names": duplicate_script_names.duplicate(),
		"sequential_flagged": sequential_flagged.duplicate(),
		"difficulty_variant_scripts": difficulty_variant_scripts.duplicate(),
		"registration_errors": registration_errors.duplicate(),
	}


func census_total() -> int:
	var total := 0
	for bucket: Variant in census.values():
		for count: Variant in (bucket as Dictionary).values():
			total += int(count)
	return total


func state_snapshot() -> Dictionary:
	## Full order-stable state for determinism comparisons: interpreter state,
	## runtime tallies, subroutine accounting and the gap log. Two executors
	## fed identical payloads and ticked identically must compare equal.
	return {
		"env": env.snapshot(),
		"scripts_fired": scripts_fired,
		"action_outcomes": _sorted_view(action_outcomes),
		"conditions_evaluated": conditions_evaluated,
		"subroutines": {
			"served": subroutine_calls_served,
			"misses": _sorted_view(subroutine_misses),
			"recursion_refusals": _sorted_view(subroutine_recursion_refusals),
			"disabled_refusals": _sorted_view(subroutine_disabled_refusals),
			"non_subroutine_targets": _sorted_view(subroutine_calls_to_non_subroutine),
		},
		"unresolved_script_references": unresolved_script_references(),
		"gaps": dispatch.gaps.to_lines(),
		"served_actions": dispatch.served_actions,
		"served_conditions": dispatch.served_conditions,
	}


static func _sorted_view(histogram: Dictionary) -> Dictionary:
	var keys := histogram.keys()
	keys.sort()
	var out := {}
	for key: Variant in keys:
		out[key] = histogram[key]
	return out
