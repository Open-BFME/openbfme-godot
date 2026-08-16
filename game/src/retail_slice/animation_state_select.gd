class_name AnimationStateSelect
extends RefCounted
## Authored AnimationState selection for the drawable/battalion presenter.
##
## Retail W3DScriptedModelDraw picks the AnimationState whose condition tokens
## are a subset of the current model-condition flags, then the most specific
## subset wins. IdleAnimationState is the empty-condition (specificity 0) row.
##
## ParticleSysBone stays deferred. AnimationPriority breaks equal-specificity
## ties (higher wins; missing/zero is a real value, not "unset").
## AnimationBlendTime is frames at 30 FPS; the battalion applies that
## conversion on state transitions.

const RETAIL_ANIMATION_FPS := 30.0
const MODULE := "AnimationState"


static func blend_seconds(raw_frames: Variant) -> float:
	## Retail AnimationBlendTime is frames on the 30 FPS SAGE/W3D clock.
	## 15 frames = 0.5s; 0 frames = an instant cut.
	if raw_frames == null:
		return -1.0
	var frames := float(raw_frames)
	if not is_finite(frames) or frames < 0.0:
		return -1.0
	return frames / RETAIL_ANIMATION_FPS


static func select(contracts: Array, active_conditions: Array) -> Dictionary:
	var active: Dictionary = {}
	for value in active_conditions:
		var flag := String(value).to_upper()
		if flag != "":
			active[flag] = true
	var best: Dictionary = {}
	var best_specificity := -1
	var best_priority := -1
	for contract_value in contracts:
		if typeof(contract_value) != TYPE_DICTIONARY:
			continue
		var row := _normalize_row(contract_value as Dictionary)
		if row.is_empty():
			continue
		if not _conditions_match(row.get("conditions", []) as Array, active):
			continue
		var specificity: int = (row.get("conditions", []) as Array).size()
		var priority := _row_priority(row)
		if specificity < best_specificity:
			continue
		if specificity == best_specificity and not best.is_empty() and priority <= best_priority:
			continue
		best_specificity = specificity
		best_priority = priority
		best = row
	if best.is_empty():
		return {
			"source": "typed-animation-state",
			"applied": 0,
			"clip": "",
			"mode": "",
			"specificity": -1,
			"randomStart": false,
			"blendSeconds": -1.0,
			"priorityDeferred": true,
		}
	var animation: Dictionary = _pick_animation(best)
	var flags: Array = best.get("flags", []) as Array
	return {
		"source": "typed-animation-state",
		"applied": 1,
		"clip": String(animation.get("animationName", "")),
		"mode": String(animation.get("mode", "")),
		"specificity": best_specificity,
		"conditions": (best.get("conditions", []) as Array).duplicate(),
		"stateKind": String(best.get("stateKind", "AnimationState")),
		"stateName": String(best.get("stateName", "")),
		"randomStart": _has_flag(flags, "RANDOMSTART"),
		"startFrameFirst": _has_flag(flags, "START_FRAME_FIRST"),
		"startFrameLast": _has_flag(flags, "START_FRAME_LAST"),
		"playBackwards": String(animation.get("mode", "")).to_upper().contains("BACKWARDS"),
		"maintainFrame": _has_maintain_frame(flags),
		"manualDeferred": String(animation.get("mode", "")).to_upper() == "MANUAL",
		"blendTimeRaw": animation.get("blendTime", null),
		"blendSeconds": blend_seconds(animation.get("blendTime", null)),
		"blendRuntimeSupport": "frames-at-30fps",
		"priority": animation.get("priority", null),
		"priorityDeferred": false,
		"speedFactorRange": (animation.get("speedFactorRange", []) as Array).duplicate(),
		"flags": flags.duplicate(),
	}


static func _normalize_row(raw: Dictionary) -> Dictionary:
	if String(raw.get("module", "")) == MODULE:
		if String(raw.get("extraction", "")) != "typed":
			return {}
		if String(raw.get("runtimeStatus", raw.get("runtime_status", ""))) != "executable":
			return {}
		var fields: Dictionary = raw.get("fields", {}) as Dictionary
		return {
			"stateKind": String(fields.get("stateKind", raw.get("stateKind", "AnimationState"))),
			"stateName": _field_string(fields.get("StateName", {})),
			"conditions": _field_tokens(fields.get("conditions", raw.get("conditions", []))),
			"flags": _field_tokens(fields.get("Flags", raw.get("flags", []))),
			"animations": _field_animations(fields.get("animations", [])),
		}
	var identifier := String(raw.get("identifier", raw.get("animationName", "")))
	var animations: Array = []
	if identifier != "":
		animations.append({
			"animationName": identifier,
			"mode": String(raw.get("AnimationMode", raw.get("mode", ""))),
			"blendTime": raw.get("AnimationBlendTime", raw.get("blendTime", null)),
			"priority": raw.get("AnimationPriority", raw.get("priority", null)),
			"speedFactorRange": raw.get("AnimationSpeedFactorRange", raw.get("speedFactorRange", [])),
		})
	return {
		"stateKind": String(raw.get("stateKind", "AnimationState")),
		"stateName": String(raw.get("StateName", raw.get("stateName", ""))),
		"conditions": _field_tokens(raw.get("conditions", [])),
		"flags": _field_tokens(raw.get("Flags", raw.get("flags", []))),
		"animations": animations,
	}


static func _conditions_match(required: Array, active: Dictionary) -> bool:
	for value in required:
		if not active.has(String(value).to_upper()):
			return false
	return true


static func _row_priority(row: Dictionary) -> int:
	var animation: Dictionary = _pick_animation(row)
	var raw: Variant = animation.get("priority", null)
	if raw == null:
		return 0
	return int(raw)


static func _pick_animation(row: Dictionary) -> Dictionary:
	var animations: Array = row.get("animations", []) as Array
	var best: Dictionary = {}
	var best_priority := -1
	for animation_value in animations:
		if typeof(animation_value) != TYPE_DICTIONARY:
			continue
		var animation := animation_value as Dictionary
		if String(animation.get("animationName", "")) == "":
			continue
		var priority := 0
		if animation.get("priority", null) != null:
			priority = int(animation.get("priority", 0))
		if best.is_empty() or priority > best_priority:
			best = animation
			best_priority = priority
	return best


static func _field_tokens(value: Variant) -> Array:
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


static func _field_animations(value: Variant) -> Array:
	var out: Array = []
	if typeof(value) != TYPE_ARRAY:
		return out
	for item in value as Array:
		if typeof(item) != TYPE_DICTIONARY:
			continue
		var row := item as Dictionary
		var name := String(row.get("animationName", row.get("AnimationName", "")))
		if name == "":
			continue
		out.append({
			"animationName": name,
			"mode": String(row.get("mode", row.get("AnimationMode", ""))).to_upper(),
			"blendTime": row.get("blendTime", row.get("AnimationBlendTime", null)),
			"priority": row.get("priority", row.get("AnimationPriority", null)),
			"speedFactorRange": row.get("speedFactorRange", row.get("AnimationSpeedFactorRange", [])),
		})
	return out


static func _has_maintain_frame(flags: Array) -> bool:
	for value in flags:
		if String(value).to_upper().begins_with("MAINTAIN_FRAME"):
			return true
	return false


static func _has_flag(flags: Array, name: String) -> bool:
	var folded := name.to_upper()
	for value in flags:
		if String(value).to_upper() == folded:
			return true
	return false
