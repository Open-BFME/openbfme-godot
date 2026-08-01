class_name SageWorldQuery
extends RefCounted

## The return type of every READ on a SageScriptWorld facet.
##
## WHY A RESULT OBJECT AND NOT A BARE VALUE
## ----------------------------------------
## A script condition that cannot be evaluated must NOT evaluate to a plausible
## number. `TEAM_HAS_UNITS` returning `false` because the world models no teams
## is indistinguishable, at the call site, from a team that genuinely has no
## units - and the first one silently advances a mission on a lie. Every read
## therefore answers two things at once: whether the world could answer at all
## (`ok`), and the answer (`value`). A handler that ignores `ok` is a bug, and
## the stub world makes that bug loud (see script_world_stub.gd).
##
## USAGE IN A HANDLER
## ------------------
##     var q := ctx.world.teams().unit_count(args.text(0))
##     if not q.ok:
##         ctx["detail"] = q.detail
##         return Dispatch.Status.WORLD_REFUSED
##     ctx.env.set_counter(args.text(1), q.as_int())
##
## `detail` always names the facet method that could not answer, so the gap log
## entry identifies the missing world surface rather than just the action.

var ok: bool = false
var value: Variant = null

## When `ok` is false: which facet method refused, and why. Empty when ok.
var detail: String = ""


static func hit(result_value: Variant) -> SageWorldQuery:
	## The world answered. `result_value` may legitimately be null/0/false.
	var query := SageWorldQuery.new()
	query.ok = true
	query.value = result_value
	return query


static func refused(method: String, reason: String = "") -> SageWorldQuery:
	## The world cannot answer this question at all. `method` is the fully
	## qualified facet method ("teams.unit_count"), which is what ends up in the
	## gap log, so keep it exact.
	var query := SageWorldQuery.new()
	query.ok = false
	query.value = null
	query.detail = (
		"world does not implement %s" % method
		if reason == ""
		else "world does not implement %s (%s)" % [method, reason]
	)
	return query


# --- Typed readers --------------------------------------------------------
#
# A refused query reads as a neutral value so a handler that has ALREADY checked
# `ok` and taken the refusal path cannot crash on the way out. These are not a
# fallback: reading a refused query without checking `ok` is a handler bug, and
# the stub world reports it.


func as_int(fallback: int = 0) -> int:
	if not ok or value == null:
		return fallback
	return int(value)


func as_float(fallback: float = 0.0) -> float:
	if not ok or value == null:
		return fallback
	return float(value)


func as_bool(fallback: bool = false) -> bool:
	if not ok or value == null:
		return fallback
	return bool(value)


func as_string(fallback: String = "") -> String:
	if not ok or value == null:
		return fallback
	return String(value)


func as_vector3(fallback: Vector3 = Vector3.ZERO) -> Vector3:
	if not ok or not (value is Vector3):
		return fallback
	return value as Vector3


func as_array() -> Array:
	if not ok or not (value is Array):
		return []
	return value as Array
