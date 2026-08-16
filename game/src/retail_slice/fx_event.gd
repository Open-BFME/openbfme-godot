class_name FXEvent
extends RefCounted
## Fire authored FXEvent lists when the clip/frame clock crosses Frame:N.
##
## Default skipped-cue policy is ignore: a RANDOMSTART / START_FRAME_LAST
## seek that lands past the cue does not fire it. Authored FireWhenSkipped
## is the opt-in (footsteps on RANDOMSTART loops). Cave troll MOVING states
## author both RANDOMSTART and FXEvent without FireWhenSkipped, so ignore
## is the retail default.

const ClipFrameClockScript := preload("res://src/retail_slice/clip_frame_clock.gd")
const MODULE := "FXEvent"


static func select(contracts: Array, selected_conditions: Array, clock: Dictionary) -> Dictionary:
	var selected := _tokens(selected_conditions)
	var current := float(clock.get("frame", -1.0))
	var previous := float(clock.get("previousFrame", -1.0))
	var length_frames := float(clock.get("lengthFrames", -1.0))
	var backwards := bool(clock.get("backwards", false))
	var priming := previous < 0.0
	var fx_lists: Array = []
	var policies: Array = []
	for raw_value in contracts:
		if typeof(raw_value) != TYPE_DICTIONARY:
			continue
		var row := _normalize(raw_value as Dictionary)
		if row.is_empty():
			continue
		if not _same(row.get("conditions", []) as Array, selected):
			continue
		var cue := float(row.get("frame", -1.0))
		var policy := String(row.get("skippedCuePolicy", "ignore"))
		var fire := false
		if priming:
			fire = bool(row.get("fireWhenSkipped", false)) and _already_passed(previous, current, cue, backwards)
		else:
			fire = ClipFrameClockScript.crossed(previous, current, cue, length_frames, backwards)
		if not fire:
			continue
		var fx_id := String(row.get("fxList", ""))
		if fx_id == "":
			continue
		fx_lists.append(fx_id)
		policies.append(policy)
	return {
		"source": "typed-fx-event",
		"applied": fx_lists.size(),
		"fxLists": fx_lists,
		"skippedCuePolicy": "ignore",
		"firedPolicies": policies,
		"primed": priming,
		"frame": current,
		"previousFrame": previous,
		"conditions": selected.duplicate(),
	}


static func _already_passed(previous: float, current: float, cue: float, backwards: bool) -> bool:
	if cue < 0.0 or current < 0.0:
		return false
	if backwards:
		return cue >= current
	return cue <= current


static func _normalize(raw: Dictionary) -> Dictionary:
	if String(raw.get("module", "")) == MODULE:
		if String(raw.get("extraction", "")) != "typed":
			return {}
		if String(raw.get("runtimeStatus", raw.get("runtime_status", ""))) != "executable":
			return {}
		var fields: Dictionary = raw.get("fields", {}) as Dictionary
		return {
			"fxList": _field_string(fields.get("fxList", {})),
			"frame": _field_number(fields.get("frame", {})),
			"conditions": _tokens(fields.get("conditions", [])),
			"fireWhenSkipped": bool((fields.get("FireWhenSkipped", {}) as Dictionary).get("value", false)),
			"skippedCuePolicy": String(fields.get("skippedCuePolicy", "ignore")),
			"stateKind": String(fields.get("stateKind", "AnimationState")),
		}
	var fx_id := String(raw.get("fxList", raw.get("Name", "")))
	if fx_id == "":
		return {}
	return {
		"fxList": fx_id,
		"frame": float(raw.get("frame", raw.get("Frame", -1))),
		"conditions": _tokens(raw.get("conditions", [])),
		"fireWhenSkipped": bool(raw.get("fireWhenSkipped", raw.get("FireWhenSkipped", false))),
		"skippedCuePolicy": String(raw.get("skippedCuePolicy", "ignore")),
		"stateKind": String(raw.get("stateKind", "AnimationState")),
	}


static func _tokens(value: Variant) -> Array:
	var out: Array = []
	var raw: Variant = value
	if typeof(value) == TYPE_DICTIONARY:
		raw = (value as Dictionary).get("value", [])
	if typeof(raw) != TYPE_ARRAY:
		return out
	for item in raw as Array:
		var token := String(item).to_upper()
		if token != "" and not out.has(token):
			out.append(token)
	return out


static func _field_string(value: Variant) -> String:
	if typeof(value) == TYPE_DICTIONARY:
		return String((value as Dictionary).get("value", ""))
	return String(value)


static func _field_number(value: Variant) -> float:
	if typeof(value) == TYPE_DICTIONARY:
		return float((value as Dictionary).get("value", -1))
	return float(value)


static func _same(left: Array, right: Array) -> bool:
	if left.size() != right.size():
		return false
	for value in left:
		if not right.has(String(value).to_upper()):
			return false
	return true
