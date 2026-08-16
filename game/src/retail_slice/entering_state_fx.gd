class_name EnteringStateFX
extends RefCounted
## Fire authored EnteringStateFX lists when the AnimationState selector
## enters a new condition set. Frame-cued FXEvent rows are a sibling kind.

const MODULE := "EnteringStateFX"


static func select(contracts: Array, selected_conditions: Array, previous_conditions: Array = []) -> Dictionary:
	var selected := _tokens(selected_conditions)
	var previous := _tokens(previous_conditions)
	if _same(selected, previous):
		return {
			"source": "typed-entering-state-fx",
			"applied": 0,
			"fxLists": [],
			"entered": false,
		}
	var fx_lists: Array = []
	for raw_value in contracts:
		if typeof(raw_value) != TYPE_DICTIONARY:
			continue
		var row := _normalize(raw_value as Dictionary)
		if row.is_empty():
			continue
		if not _same(row.get("conditions", []) as Array, selected):
			continue
		var fx_id := String(row.get("fxList", ""))
		if fx_id != "":
			fx_lists.append(fx_id)
	return {
		"source": "typed-entering-state-fx",
		"applied": fx_lists.size(),
		"fxLists": fx_lists,
		"entered": true,
		"conditions": selected.duplicate(),
	}


static func _normalize(raw: Dictionary) -> Dictionary:
	if String(raw.get("module", "")) == MODULE:
		if String(raw.get("extraction", "")) != "typed":
			return {}
		if String(raw.get("runtimeStatus", raw.get("runtime_status", ""))) != "executable":
			return {}
		var fields: Dictionary = raw.get("fields", {}) as Dictionary
		return {
			"fxList": _field_string(fields.get("fxList", fields.get("EnteringStateFX", {}))),
			"conditions": _tokens(fields.get("conditions", [])),
			"stateKind": String(fields.get("stateKind", "AnimationState")),
		}
	var fx_id := String(raw.get("fxList", raw.get("EnteringStateFX", "")))
	if fx_id == "":
		return {}
	return {
		"fxList": fx_id,
		"conditions": _tokens(raw.get("conditions", [])),
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


static func _same(left: Array, right: Array) -> bool:
	if left.size() != right.size():
		return false
	for value in left:
		if not right.has(String(value).to_upper()):
			return false
	return true
