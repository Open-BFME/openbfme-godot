class_name SageScriptArgs
extends RefCounted

## Positional, signature-checked access to one decoded record's arguments.
##
## WHY POSITIONAL
## --------------
## The obvious way to read a SAGE argument list is to search it for the first
## argument whose `argumentType` matches the type you want. That works right up
## until a signature repeats a type - SET_FLAG_TO_FLAG(FLAG, FLAG),
## COUNTER_COUNTER(COUNTER, COMPARISON, COUNTER), SET_RANDOM_COUNTER(COUNTER,
## INT, INT) - at which point it silently reads the wrong argument. Arguments
## are written in declaration order (sage_scb.py `_parse_script_content` reads
## `argument_count` then that many arguments in sequence), so position is the
## correct key and the declared signature is the schema.
##
## VALIDATION
## ----------
## `validate()` checks arity against the signature, and checks each argument's
## `argumentType` code against the expected one WHERE THIS REPO HAS OBSERVED
## that code (SageScriptParamTypes.ARGUMENT_TYPE_FOR_PARAM is partial by
## design). Unobserved parameter types are not asserted on - an invented
## expectation would reject valid data. Entries whose signature the source
## reference itself marks uncertain are arity-exempt and say so.

const ParamTypes := preload("res://src/script/script_param_types.gd")
const Vocabulary := preload("res://src/script/script_vocabulary.gd")

var entry: Dictionary = {}
var raw: Array = []


static func create(vocabulary_entry: Dictionary, arguments: Array) -> SageScriptArgs:
	var args := SageScriptArgs.new()
	args.entry = vocabulary_entry
	args.raw = arguments
	return args


func size() -> int:
	return raw.size()


func signature() -> Array:
	return entry.get("params", [])


func validate() -> Dictionary:
	## Returns {"ok": bool, "detail": String}.
	var params: Array = signature()
	if String(entry.get("confidence", "")) == Vocabulary.CONFIDENCE_UNCERTAIN:
		return {
			"ok": true,
			"detail": "signature marked uncertain by the source reference; arity unchecked",
		}
	if raw.size() != params.size():
		return {
			"ok": false,
			"detail": "expected %d argument(s) %s, decoded record carries %d"
			% [params.size(), str(params), raw.size()],
		}
	for index in range(params.size()):
		var expected := ParamTypes.argument_type_for_param(String(params[index]))
		if expected == ParamTypes.UNKNOWN_CODE:
			continue
		var actual := int((raw[index] as Dictionary).get("argumentType", -1))
		if actual != expected:
			return {
				"ok": false,
				"detail": "argument %d declared %s (code %d) but record carries code %d"
				% [index, String(params[index]), expected, actual],
			}
	return {"ok": true, "detail": ""}


func _slot(index: int) -> Dictionary:
	if index < 0 or index >= raw.size():
		return {}
	return raw[index]


func text(index: int, fallback: String = "") -> String:
	var slot := _slot(index)
	if slot.is_empty():
		return fallback
	return String(slot.get("text", fallback))


func integer(index: int, fallback: int = 0) -> int:
	var slot := _slot(index)
	if slot.is_empty():
		return fallback
	return int(slot.get("integer", fallback))


func real(index: int, fallback: float = 0.0) -> float:
	var slot := _slot(index)
	if slot.is_empty():
		return fallback
	return float(slot.get("real", fallback))


func boolean(index: int, fallback: bool = false) -> bool:
	var slot := _slot(index)
	if slot.is_empty():
		return fallback
	return int(slot.get("integer", 1 if fallback else 0)) != 0


func position(index: int, fallback: Vector3 = Vector3.ZERO) -> Vector3:
	var slot := _slot(index)
	if not slot.has("position"):
		return fallback
	var raw_position: Array = slot["position"]
	if raw_position.size() != 3:
		return fallback
	return Vector3(float(raw_position[0]), float(raw_position[1]), float(raw_position[2]))


func value(index: int) -> Variant:
	## Reads the slot using the field the declared parameter type implies.
	var params: Array = signature()
	if index < 0 or index >= params.size():
		return text(index)
	match ParamTypes.payload_field_for_param(String(params[index])):
		"integer":
			return integer(index)
		"real":
			return real(index)
		"position":
			return position(index)
	return text(index)
