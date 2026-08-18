class_name RetailTreeSway
extends Node
## Wind sway for W3DTreeDraw props. AUTHORED VALUES ONLY.
##
## What retail actually authors, checked against the RotWK 2.01 tree
## (`workspace/retail-work/editions/rotwk/cache/effective-assets/data/ini`):
##
##   * `gamedata.ini:8583  DownwindAngle = -0.785   ; Northeast! AKA "Away and
##     to the right"` — the wind DIRECTION, in radians. This is the only
##     authored sway datum in the whole game.
##   * `naturetrees.ini` — 220 `Draw = W3DTreeDraw` blocks. The complete field
##     set is ModelName/TextureName/DoTopple/ToppleFX/BounceFX/
##     KillWhenFinishedToppling/SinkDistance/SinkTime/TaintedTree/FadeDistance/
##     FadeTarget/StaticModelLODMode/MorphTime/MorphFX/MorphTree. There is no
##     sway field, and the file says so itself at `naturetrees.ini:164`:
##     `; Note no SwayBehavior, as this is handled in the W3DTreeBuffer.`
##   * `cinematicobjects.ini:21112  ClientUpdate = SwayClientUpdate` — body is
##     literally `;nothing`. No amplitude, lean, period or randomness anywhere.
##   * The map format has no sway WorldInfo key (the importer's WorldInfo
##     whitelist in `sage_environment.py:59-70` is camera/weather/name only),
##     and `SET_TREE_SWAY` appears in 0 of the 128 shipped RotWK `.map` files.
##
## So: amplitude, lean, period and randomness are UNAUTHORED. This module used
## to invent them (4 degrees, 45 frames, 0.35 randomness, applied to anything
## whose type name contained "tree"/"grass"/…) and the owner's playtest note
## was "trees don't naturally sway like RotWK, weird looking". Unauthored is now
## STILL, with a named gap — never a made-up breeze.
##
## `SET_TREE_SWAY(wind, sway, lean, frames, randomness)` from a map script still
## applies in full; that is the one authored path into these numbers.

## `gamedata.ini:8583`. Radians, the game's own units.
const RETAIL_DOWNWIND_ANGLE_RADIANS := -0.785
const RETAIL_FRAMES_PER_SECOND := 30.0
const UNAUTHORED_SOURCE := "unauthored-still-until-set-tree-sway"
const AUTHORED_SOURCE := "SET_TREE_SWAY"

var bound_count := 0
var wind_degrees := rad_to_deg(RETAIL_DOWNWIND_ANGLE_RADIANS)
var sway_degrees := 0.0
var lean_degrees := 0.0
var frames_per_sway := 0
var randomness := 0.0
var source := UNAUTHORED_SOURCE
var last_set_tree_sway: Array = []
## Named, not silent. Every reason a tree is standing still.
var gaps: Array[String] = []

var _records: Array[Dictionary] = []
var _time := 0.0


func bind_prop_container(container: Node, sway_type_ids: Variant = null) -> int:
	## Bind the placements the pack says are W3DTreeDraw objects.
	##
	## `sway_type_ids` is the authored set of SAGE type names whose object
	## declares `Draw = W3DTreeDraw` (naturetrees.ini). It is NOT derived from
	## the type name: "StreetLamp" contains "tree" and PTGrass is a W3DTreeDraw
	## while a farm is not — only the draw module decides. Passing `null` means
	## the caller has no authored draw-module table, which is a gap, not a
	## licence to guess: nothing binds.
	_records.clear()
	bound_count = 0
	gaps.clear()
	if sway_degrees <= 0.0:
		gaps.append("no-authored-sway-amplitude:naturetrees.ini-authors-no-sway-field")
	if typeof(sway_type_ids) != TYPE_ARRAY and typeof(sway_type_ids) != TYPE_PACKED_STRING_ARRAY:
		gaps.append("draw-module-table-absent:pack-ships-no-w3dtreedraw-kinds")
		set_process(false)
		return 0
	var authored: Dictionary = {}
	for id_value in sway_type_ids as Array:
		var id_text := String(id_value).strip_edges()
		if id_text != "":
			authored[id_text.to_lower()] = true
	if authored.is_empty():
		gaps.append("draw-module-table-empty:no-w3dtreedraw-placements")
		set_process(false)
		return 0
	if container == null:
		return 0
	for child in container.get_children():
		if not (child is Node3D):
			continue
		var source_type := String(child.get_meta("source_type", ""))
		if not authored.has(source_type.to_lower()):
			continue
		var node := child as Node3D
		_records.append({
			"node": node,
			"base_basis": node.basis,
			"phase": _hash_phase(source_type, int(child.get_meta("source_index", bound_count))),
		})
		node.set_meta("tree_sway", true)
		node.set_meta("tree_sway_source", source)
		bound_count += 1
	set_process(bound_count > 0 and _is_moving())
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
	source = AUTHORED_SOURCE
	last_set_tree_sway = values.duplicate()
	gaps.erase("no-authored-sway-amplitude:naturetrees.ini-authors-no-sway-field")
	for record in _records:
		var node: Node3D = record.get("node")
		if node != null and is_instance_valid(node):
			node.set_meta("tree_sway_source", source)
	set_process(bound_count > 0 and _is_moving())
	return ""


func runtime_contract() -> Dictionary:
	return {
		"schema": "openbfme.tree-sway",
		"schema_version": 1,
		"source": source,
		"bound_count": bound_count,
		"wind_degrees": wind_degrees,
		"wind_radians": deg_to_rad(wind_degrees),
		"wind_source": "gamedata.ini:8583 DownwindAngle" if source == UNAUTHORED_SOURCE else AUTHORED_SOURCE,
		"sway_degrees": sway_degrees,
		"lean_degrees": lean_degrees,
		"frames_per_sway": frames_per_sway,
		"randomness": randomness,
		"still": not _is_moving(),
		"gaps": gaps.duplicate(),
	}


func _is_moving() -> bool:
	return sway_degrees > 0.0 and frames_per_sway > 0


func _process(delta: float) -> void:
	_time += delta
	_apply_pose(_time)


func _apply_pose(time_seconds: float) -> void:
	if _records.is_empty() or not _is_moving():
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
