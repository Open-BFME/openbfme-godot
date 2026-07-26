class_name SageScriptDispatch
extends RefCounted

## Registry and call path for SAGE map-script actions and conditions.
##
## Serves BOTH front ends. A decoded map-script record and a Lua
## `ExecuteAction("NAME", ...)` call arrive at the same `execute_action()`, hit
## the same handler and produce the same gap records, because SAGE gives them
## one vocabulary (see script_vocabulary.gd).
##
##
## HANDLER CONTRACT
## ================
## A handler is a Callable taking exactly one argument - a call context
## Dictionary - and returning an int from `Status`.
##
## The context carries:
##   ctx.name        String   canonical action / condition name
##   ctx.entry       Dictionary  the vocabulary entry (signature, category, ...)
##   ctx.args        SageScriptArgs  positional, signature-checked arguments
##   ctx.env         SageScriptEnv   interpreter state: counters, flags, timers
##   ctx.world       SageScriptWorld the world interface (may be a stub)
##   ctx.script_name String   the script the call came from, for gap identity
##   ctx.dispatch    SageScriptDispatch  self, for recording extra detail
##   ctx.result      bool     CONDITION HANDLERS ONLY: write the answer here
##
## A handler MAY:
##   * read and mutate ctx.env
##   * read and mutate the world, but ONLY through SageScriptWorld methods
##   * read its arguments through ctx.args
##
## A handler MUST NOT:
##   * reference retail_slice_sim.gd or any other concrete world type
##   * touch the scene tree, rendering, input, the filesystem or the clock
##   * keep state between calls - all state belongs in ctx.env or the world
##   * return OK after failing to do the thing; return the honest status and a
##     gap gets recorded
##
## A CONDITION handler must additionally be side-effect free: it may read
## ctx.env and the world but must mutate neither. Conditions are evaluated a
## variable number of times depending on evaluation intervals and short-circuit
## order, so a condition with side effects is a determinism bug.
##
## Arguments are validated before the handler runs. A handler can assume arity
## and (for observed parameter types) argument coding are correct.

const Vocabulary := preload("res://src/script/script_vocabulary.gd")
const GapLog := preload("res://src/script/script_gaps.gd")
const Args := preload("res://src/script/script_args.gd")

enum Status {
	OK,
	UNSUPPORTED,      ## no handler, or the handler declines this variant
	BAD_ARGUMENTS,    ## arguments disagree with the declared signature
	WORLD_REFUSED,    ## world does not implement the needed capability
	DELIBERATE,       ## implementable, refused on purpose (e.g. desync-prone)
}

## Canonical name -> Callable.
var action_handlers: Dictionary = {}
var condition_handlers: Dictionary = {}

## Names that HAVE a handler but whose handler exists only to refuse on purpose
## (see SET_COUNTER_TO_CLIENT_RANDOM_VALUE). They are excluded from the
## implemented counts, because counting a refusal as coverage is exactly the
## kind of flattering arithmetic this repo's accounting exists to prevent.
var deliberate_refusals: Dictionary = {}

var gaps: SageScriptGapLog = GapLog.new()

## Calls served without a gap, per kind. Cheap health signal for a run.
var served_actions: int = 0
var served_conditions: int = 0

## Value an unimplemented / failed condition evaluates to. FALSE is the only
## safe default: a gate that fires because its condition could not be evaluated
## would advance a campaign on a lie.
const CONDITION_FALLBACK := false


func register_action(name: String, handler: Callable) -> bool:
	if not Vocabulary.has_entry(Vocabulary.Kind.ACTION, name):
		push_error(
			"SageScriptDispatch: refusing to register handler for unknown action '%s'" % name
		)
		return false
	action_handlers[name] = handler
	return true


func register_condition(name: String, handler: Callable) -> bool:
	if not Vocabulary.has_entry(Vocabulary.Kind.CONDITION, name):
		push_error(
			"SageScriptDispatch: refusing to register handler for unknown condition '%s'" % name
		)
		return false
	condition_handlers[name] = handler
	return true


func register_deliberate_refusal(name: String, handler: Callable) -> bool:
	## Registers a handler that exists to refuse the action on purpose. It is
	## dispatched normally (so a map using it produces a `deliberate` gap with
	## full identity) but is never counted as coverage.
	if not register_action(name, handler):
		return false
	deliberate_refusals[name] = true
	return true


func implemented_actions() -> Array[String]:
	var out: Array[String] = []
	for name: Variant in action_handlers:
		if not deliberate_refusals.has(name):
			out.append(String(name))
	out.sort()
	return out


func implemented_conditions() -> Array[String]:
	var out: Array[String] = []
	for name: Variant in condition_handlers:
		if not deliberate_refusals.has(name):
			out.append(String(name))
	out.sort()
	return out


func deliberately_refused() -> Array[String]:
	var out: Array[String] = []
	for name: Variant in deliberate_refusals:
		out.append(String(name))
	out.sort()
	return out


func coverage() -> Dictionary:
	var result := Vocabulary.coverage(implemented_actions(), implemented_conditions())
	result["deliberately_refused"] = deliberately_refused()
	return result


# --- Call paths -----------------------------------------------------------


func execute_action(
	record: Dictionary,
	env: SageScriptEnv,
	world: SageScriptWorld,
	script_name: String = "",
	game: int = Vocabulary.Game.UNKNOWN,
	map_version: int = 0
) -> int:
	## `record` is one decoded ScriptAction / ScriptActionFalse `value` payload
	## (sage_scb.py shape), or the equivalent synthesised for a Lua call.
	return _dispatch(
		Vocabulary.Kind.ACTION, record, env, world, script_name, game, map_version
	)["status"]


func evaluate_condition(
	record: Dictionary,
	env: SageScriptEnv,
	world: SageScriptWorld,
	script_name: String = "",
	game: int = Vocabulary.Game.UNKNOWN,
	map_version: int = 0
) -> bool:
	## Returns the condition's truth value, applying the record's `inverted`
	## flag. A condition that could not be evaluated returns CONDITION_FALLBACK
	## regardless of inversion, and has already been recorded as a gap.
	var outcome := _dispatch(
		Vocabulary.Kind.CONDITION, record, env, world, script_name, game, map_version
	)
	if int(outcome["status"]) != Status.OK:
		return CONDITION_FALLBACK
	var result := bool(outcome["result"])
	if bool(record.get("inverted", false)):
		return not result
	return result


func call_by_name(
	kind: int,
	name: String,
	arguments: Array,
	env: SageScriptEnv,
	world: SageScriptWorld,
	script_name: String = "lua"
) -> Dictionary:
	## Front door for SAGE Lua: `ExecuteAction("NAME", ...)` and
	## `EvaluateCondition("NAME", ...)` land here. Same table, same handlers,
	## same gap log as the map-script path.
	return _dispatch(
		kind,
		{"internalName": {"name": name}, "arguments": arguments, "enabled": true},
		env,
		world,
		script_name,
		Vocabulary.Game.UNKNOWN,
		0
	)


func _dispatch(
	kind: int,
	record: Dictionary,
	env: SageScriptEnv,
	world: SageScriptWorld,
	script_name: String,
	game: int,
	map_version: int
) -> Dictionary:
	var kind_label := Vocabulary.kind_label(kind)
	var tick := env.tick_index if env != null else 0
	var internal: Dictionary = record.get("internalName", {})
	var name := String(internal.get("name", ""))
	var content_type := int(record.get("contentType", -1))
	var arguments: Array = record.get("arguments", [])

	var resolved := Vocabulary.resolve(kind, name, content_type, game, map_version)
	if int(resolved["status"]) != Vocabulary.Resolution.OK:
		var reason := _reason_for_resolution(int(resolved["status"]))
		var identity := String(resolved["name"])
		if identity == "":
			identity = "contentType=%d" % content_type
		gaps.record(
			kind_label, identity, reason, script_name, tick,
			String(resolved["detail"]), arguments
		)
		return {"status": Status.UNSUPPORTED, "result": CONDITION_FALLBACK}

	var entry: Dictionary = resolved["entry"]
	var canonical := String(resolved["name"])
	var handlers := action_handlers if kind == Vocabulary.Kind.ACTION else condition_handlers
	if not handlers.has(canonical):
		gaps.record(
			kind_label, canonical, GapLog.REASON_UNIMPLEMENTED, script_name, tick,
			"in the vocabulary (category='%s', signature=%s) but no handler is registered"
			% [String(entry.get("category", "")), str(entry.get("params", []))],
			arguments
		)
		return {"status": Status.UNSUPPORTED, "result": CONDITION_FALLBACK}

	var bound := Args.create(entry, arguments)
	var validation := bound.validate()
	if not bool(validation["ok"]):
		gaps.record(
			kind_label, canonical, GapLog.REASON_BAD_ARGUMENTS, script_name, tick,
			String(validation["detail"]), arguments
		)
		return {"status": Status.BAD_ARGUMENTS, "result": CONDITION_FALLBACK}

	var ctx := {
		"name": canonical,
		"entry": entry,
		"args": bound,
		"env": env,
		"world": world,
		"script_name": script_name,
		"dispatch": self,
		"result": CONDITION_FALLBACK,
	}
	var status := int((handlers[canonical] as Callable).call(ctx))
	if status == Status.OK:
		if kind == Vocabulary.Kind.ACTION:
			served_actions += 1
		else:
			served_conditions += 1
		return {"status": Status.OK, "result": bool(ctx["result"])}

	gaps.record(
		kind_label, canonical, _reason_for_status(status), script_name, tick,
		String(ctx.get("detail", "")), arguments
	)
	return {"status": status, "result": CONDITION_FALLBACK}


func _reason_for_resolution(status: int) -> String:
	match status:
		Vocabulary.Resolution.ERR_UNKNOWN_NAME:
			return GapLog.REASON_UNKNOWN_NAME
		Vocabulary.Resolution.ERR_NO_ID_MAP:
			return GapLog.REASON_NO_ID_MAP
		Vocabulary.Resolution.ERR_UNKNOWN_ID:
			return GapLog.REASON_UNKNOWN_ID
		Vocabulary.Resolution.ERR_ID_NAME_MISMATCH:
			return GapLog.REASON_ID_NAME_MISMATCH
	return GapLog.REASON_UNKNOWN_NAME


func _reason_for_status(status: int) -> String:
	match status:
		Status.BAD_ARGUMENTS:
			return GapLog.REASON_BAD_ARGUMENTS
		Status.WORLD_REFUSED:
			return GapLog.REASON_WORLD_REFUSED
		Status.DELIBERATE:
			return GapLog.REASON_DELIBERATE
	return GapLog.REASON_UNIMPLEMENTED
