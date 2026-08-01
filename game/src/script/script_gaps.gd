class_name SageScriptGapLog
extends RefCounted

## Structured record of everything the script vocabulary could not do.
##
## This repository treats a silent fallback as a defect class: a script that
## quietly skips an action it does not implement produces a run that looks
## healthy and is wrong. Every such moment lands here instead, carrying enough
## identity to act on it - which script, which action or condition, why, and
## what the arguments looked like the first time it happened.
##
## Gaps are aggregated by (kind, name, reason) so a per-tick action that misses
## ten thousand times costs one entry, but the first occurrence keeps its full
## context.

## Why a call could not be served. These are the only sanctioned reasons; a new
## failure mode gets a new reason rather than being folded into an existing one.
const REASON_UNIMPLEMENTED := "unimplemented"          # in the table, no handler
const REASON_UNKNOWN_NAME := "unknown-name"            # not in the table at all
const REASON_NO_ID_MAP := "no-id-map"                  # nameless record, no id table
const REASON_UNKNOWN_ID := "unknown-id"                # id absent from the id table
const REASON_ID_NAME_MISMATCH := "id-name-mismatch"    # id table is for another game
const REASON_BAD_ARGUMENTS := "bad-arguments"          # arity / type disagreement
const REASON_WORLD_REFUSED := "world-refused"          # world lacks the capability
const REASON_DELIBERATE := "deliberate"                # implementable, refused on purpose
const REASON_BLOCKED_SUBSYSTEM := "blocked-subsystem"  # named, but the subsystem does not exist

## `blocked-subsystem` is distinct from `unimplemented` on purpose. "Nobody has
## written the handler yet" and "the simulation has no fog of war for the
## handler to talk to" are different states with different owners: the first is
## scheduling, the second is a product decision. Folding them together would
## hide 32 actions behind a number that looks like backlog.

## Insertion-ordered "kind|name|reason" -> aggregated record.
var entries: Dictionary = {}
var total: int = 0


func record(
	kind: String,
	name: String,
	reason: String,
	script_name: String,
	tick: int,
	detail: String = "",
	arguments: Array = []
) -> void:
	total += 1
	var key := "%s|%s|%s" % [kind, name, reason]
	if entries.has(key):
		var existing: Dictionary = entries[key]
		existing["count"] = int(existing["count"]) + 1
		return
	entries[key] = {
		"kind": kind,
		"name": name,
		"reason": reason,
		"count": 1,
		"first_script": script_name,
		"first_tick": tick,
		"detail": detail,
		"first_arguments": arguments.duplicate(true),
	}


func is_empty() -> bool:
	return entries.is_empty()


func distinct_count() -> int:
	return entries.size()


func count_for(kind: String, name: String, reason: String) -> int:
	var key := "%s|%s|%s" % [kind, name, reason]
	if not entries.has(key):
		return 0
	return int(entries[key]["count"])


func has(kind: String, name: String, reason: String) -> bool:
	return entries.has("%s|%s|%s" % [kind, name, reason])


func names_for_reason(reason: String) -> Array[String]:
	var out: Array[String] = []
	for record_row: Variant in entries.values():
		if String((record_row as Dictionary)["reason"]) == reason:
			out.append(String((record_row as Dictionary)["name"]))
	out.sort()
	return out


func by_reason() -> Dictionary:
	var out := {}
	for record_row: Variant in entries.values():
		var row: Dictionary = record_row
		var reason := String(row["reason"])
		out[reason] = int(out.get(reason, 0)) + int(row["count"])
	return out


func to_lines() -> Array[String]:
	## Human-readable, sorted so two runs of the same scenario diff cleanly.
	var keys := entries.keys()
	keys.sort()
	var out: Array[String] = []
	for key: Variant in keys:
		var row: Dictionary = entries[key]
		out.append(
			"%s %s reason=%s count=%d first_script='%s' first_tick=%d%s"
			% [
				row["kind"],
				row["name"],
				row["reason"],
				int(row["count"]),
				row["first_script"],
				int(row["first_tick"]),
				"" if String(row["detail"]) == "" else " detail=%s" % row["detail"],
			]
		)
	return out


func clear() -> void:
	entries = {}
	total = 0
