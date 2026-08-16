class_name RetailFxTiming
extends Node

## Deterministic SAGE particle burst scheduler. Retail particle delays are
## authored in 30 Hz particle frames; the renderer advances this object by
## elapsed frames and restarts the emitter for every returned burst.

var _rng := RandomNumberGenerator.new()
var _initial_delay := 0.0
var _burst_delay := 0.0
var _remaining := 0.0
var _started := false
var _configured := false
signal burst


func configure(contract: Dictionary, seed: int) -> bool:
	var initial := _range(contract.get("InitialDelay", contract.get("initialDelayFrames")), "InitialDelay", true)
	var burst := _range(contract.get("BurstDelay", contract.get("burstDelayFrames")), "BurstDelay", false)
	if initial.is_empty() or burst.is_empty():
		return false
	_rng.seed = seed
	_initial_delay = _sample(initial)
	_burst_delay = _sample(burst)
	_remaining = _initial_delay
	_started = false
	_configured = true
	return true


func advance_frames(elapsed_frames: float) -> int:
	if not _configured or not is_finite(elapsed_frames) or elapsed_frames < 0.0:
		return 0
	var bursts := 0
	var available := elapsed_frames
	while available >= _remaining:
		available -= _remaining
		bursts += 1
		_started = true
		_remaining = _burst_delay
		if _remaining <= 0.0:
			break
	_remaining -= available
	for _index in range(bursts):
		burst.emit()
	return bursts


func _process(delta: float) -> void:
	advance_frames(delta * 30.0)


func authored_fields() -> Array[String]:
	return ["BurstDelay", "InitialDelay"]


static func contract_from_definition(document: Dictionary) -> Dictionary:
	var typed: Variant = document.get("timing", null)
	if typeof(typed) == TYPE_DICTIONARY:
		return (typed as Dictionary).duplicate(true)
	var result := {
		"initialDelayFrames": {"minimum": 0.0, "maximum": 0.0},
	}
	for entry_value in document.get("entries", []) as Array:
		if typeof(entry_value) != TYPE_DICTIONARY:
			continue
		var entry := entry_value as Dictionary
		if String(entry.get("type", "")) != "block" or String(entry.get("field", "")).to_lower() != "system":
			continue
		for child_value in entry.get("entries", []) as Array:
			if typeof(child_value) != TYPE_DICTIONARY:
				continue
			var child := child_value as Dictionary
			var field := String(child.get("field", ""))
			if field not in ["BurstDelay", "InitialDelay"]:
				continue
			var parsed := _parse_authored_range(String(child.get("value", "")))
			if parsed.is_empty():
				return {}
			result["burstDelayFrames" if field == "BurstDelay" else "initialDelayFrames"] = parsed
	if not result.has("burstDelayFrames"):
		return {}
	return result


static func _parse_authored_range(authored: String) -> Dictionary:
	var tokens := authored.split(" ", false)
	if tokens.size() < 1 or tokens.size() > 2:
		return {}
	if not tokens[0].is_valid_float() or (tokens.size() == 2 and not tokens[1].is_valid_float()):
		return {}
	var minimum := float(tokens[0])
	var maximum := float(tokens[1]) if tokens.size() == 2 else minimum
	if not is_finite(minimum) or not is_finite(maximum) or minimum < 0.0 or maximum < minimum:
		return {}
	return {"minimum": minimum, "maximum": maximum}


func _range(value: Variant, _label: String, allow_zero: bool) -> Dictionary:
	if typeof(value) != TYPE_DICTIONARY:
		return {}
	var row := value as Dictionary
	if typeof(row.get("minimum")) not in [TYPE_INT, TYPE_FLOAT] or typeof(row.get("maximum")) not in [TYPE_INT, TYPE_FLOAT]:
		return {}
	var minimum := float(row.minimum)
	var maximum := float(row.maximum)
	if not is_finite(minimum) or not is_finite(maximum) or minimum < 0.0 or maximum < minimum or (not allow_zero and maximum <= 0.0):
		return {}
	return {"minimum": minimum, "maximum": maximum}


func _sample(row: Dictionary) -> float:
	return _rng.randf_range(float(row.minimum), float(row.maximum))
