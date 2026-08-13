class_name RetailTreeSway
extends Node
## Applies SET_TREE_SWAY (and a named idle breeze) to bound vegetation GLBs.
##
## W3DTreeDraw's push-aside contract is opaque-deferred. Wind sway is the map
## script action SET_TREE_SWAY(ANGLE wind, ANGLE sway, ANGLE lean, INT frames,
## REAL randomness). Until a map emits that action, trees use a clearly named
## idle breeze so they are not frozen props.

const SOURCE := "script SET_TREE_SWAY + idle-breeze-until-set-tree-sway"
const IDLE_WIND_DEGREES := 0.0
const IDLE_SWAY_DEGREES := 4.0
const IDLE_LEAN_DEGREES := 1.0
const IDLE_FRAMES_PER_SWAY := 45
const IDLE_RANDOMNESS := 0.35
const RETAIL_FRAMES_PER_SECOND := 30.0

const _VEGETATION_TOKENS: Array[String] = [
	"tree", "pine", "oak", "elm", "birch", "willow", "fir", "spruce",
	"mallorn", "aspen", "cedar", "cypress", "palm", "shrub", "bush",
	"fern", "grass", "reed", "hedge", "grove",
]

var bound_count := 0
var wind_degrees := IDLE_WIND_DEGREES
var sway_degrees := IDLE_SWAY_DEGREES
var lean_degrees := IDLE_LEAN_DEGREES
var frames_per_sway := IDLE_FRAMES_PER_SWAY
var randomness := IDLE_RANDOMNESS
var source := "idle-breeze-until-set-tree-sway"
var last_set_tree_sway: Array = []

var _records: Array[Dictionary] = []
var _time := 0.0


static func is_sway_source_type(source_type: String) -> bool:
	var trimmed := source_type.strip_edges()
	if trimmed == "":
		return false
	# SAGE type ids are CamelCase concatenations (PTGrass15, TreeOak01). Split
	# those tokens so StreetLamp does not match "tree" inside "street".
	for part in _camel_tokens(trimmed):
		if _VEGETATION_TOKENS.has(part):
			return true
	return false


static func _camel_tokens(source_type: String) -> PackedStringArray:
	var tokens := PackedStringArray()
	var current := ""
	for index in source_type.length():
		var ch := source_type[index]
		var code := ch.unicode_at(0)
		var is_upper := code >= 65 and code <= 90
		var is_lower := code >= 97 and code <= 122
		var is_digit := code >= 48 and code <= 57
		if current == "":
			if is_upper or is_lower or is_digit:
				current = ch
			continue
		var prev := current[current.length() - 1]
		var prev_code := prev.unicode_at(0)
		var prev_upper := prev_code >= 65 and prev_code <= 90
		var prev_lower := prev_code >= 97 and prev_code <= 122
		var prev_digit := prev_code >= 48 and prev_code <= 57
		var boundary := false
		if is_digit != prev_digit and (is_digit or prev_digit):
			boundary = true
		elif is_upper and prev_lower:
			boundary = true
		elif is_upper and prev_upper and index + 1 < source_type.length():
			var next_code := source_type[index + 1].unicode_at(0)
			if next_code >= 97 and next_code <= 122:
				boundary = true
		elif not (is_upper or is_lower or is_digit):
			boundary = true
		if boundary:
			if current != "":
				tokens.append(current.to_lower())
			current = ch if (is_upper or is_lower or is_digit) else ""
		else:
			current += ch
	if current != "":
		tokens.append(current.to_lower())
	return tokens


func bind_prop_container(container: Node) -> int:
	_records.clear()
	bound_count = 0
	if container == null:
		return 0
	for child in container.get_children():
		if not (child is Node3D):
			continue
		var source_type := String(child.get_meta("source_type", ""))
		if not is_sway_source_type(source_type):
			continue
		var node := child as Node3D
		var record := {
			"node": node,
			"base_basis": node.basis,
			"phase": _hash_phase(source_type, int(child.get_meta("source_index", bound_count))),
		}
		_records.append(record)
		node.set_meta("tree_sway", true)
		node.set_meta("tree_sway_source", source)
		bound_count += 1
	set_process(bound_count > 0)
	_apply_pose(0.0)
	return bound_count


func apply_set_tree_sway(values: Array) -> String:
	if values.size() != 5:
		return "SET_TREE_SWAY requires wind, sway, lean, frames, randomness"
	var wind := float(values[0])
	var sway := float(values[1])
	var lean := float(values[2])
	var frames := int(values[3])
	var random_amount := float(values[4])
	if not is_finite(wind) or not is_finite(sway) or not is_finite(lean) or not is_finite(random_amount):
		return "SET_TREE_SWAY arguments are not finite"
	if frames <= 0:
		return "SET_TREE_SWAY frames per sway must be > 0"
	wind_degrees = wind
	sway_degrees = sway
	lean_degrees = lean
	frames_per_sway = frames
	randomness = random_amount
	source = "SET_TREE_SWAY"
	last_set_tree_sway = values.duplicate()
	for record in _records:
		var node: Node3D = record.get("node")
		if node != null and is_instance_valid(node):
			node.set_meta("tree_sway_source", source)
	return ""


func runtime_contract() -> Dictionary:
	return {
		"schema": "openbfme.tree-sway",
		"schema_version": 0,
		"source": source,
		"bound_count": bound_count,
		"wind_degrees": wind_degrees,
		"sway_degrees": sway_degrees,
		"lean_degrees": lean_degrees,
		"frames_per_sway": frames_per_sway,
		"randomness": randomness,
		"idle_until_script": source == "idle-breeze-until-set-tree-sway",
	}


func _process(delta: float) -> void:
	_time += delta
	_apply_pose(_time)


func _apply_pose(time_seconds: float) -> void:
	if _records.is_empty():
		return
	var period := float(frames_per_sway) / RETAIL_FRAMES_PER_SECOND
	if period <= 0.0:
		return
	var wind_rad := deg_to_rad(wind_degrees)
	var wind_axis := Vector3(cos(wind_rad), 0.0, sin(wind_rad))
	if wind_axis.length_squared() <= 0.000001:
		wind_axis = Vector3.RIGHT
	else:
		wind_axis = wind_axis.normalized()
	for record in _records:
		var node: Node3D = record.get("node")
		if node == null or not is_instance_valid(node):
			continue
		var phase := float(record.get("phase", 0.0)) * randomness
		var wave := sin((time_seconds / period) * TAU + phase)
		var angle := deg_to_rad(lean_degrees + sway_degrees * wave)
		var base: Basis = record.get("base_basis")
		node.basis = base * Basis(wind_axis, angle)


func _hash_phase(source_type: String, source_index: int) -> float:
	var payload := "%s:%d" % [source_type, source_index]
	var hash_value := 0
	for index in payload.length():
		hash_value = (hash_value * 33 + payload.unicode_at(index)) & 0x7fffffff
	return float(hash_value % 1000) / 1000.0 * TAU
