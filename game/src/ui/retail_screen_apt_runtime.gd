class_name RetailScreenAptRuntime
extends Control

## Draws ANY cooked retail screen from its own scene contract.
##
## `res://src/ui/retail_shell_apt_runtime.gd` is the main-menu instrument: one
## pinned scene id, one `files.shellScene` pack key, one atlas prefix. This is
## the same proven draw path pointed at the 62 screens the importer's
## `retail_screen_apt_convert` cooks (queue Q117) - Options, ScoreScreen,
## SpellStore, MpGameSetup, the CAH movies, the online lobbies - each addressed
## by movie name rather than by a hardcoded key.
##
## Fail-closed by construction, exactly like the shell lane:
##   * a pack that does not ship the screen leaves `contract_declared` false and
##     the caller keeps whatever it already draws;
##   * a contract carrying unsupported semantics needs an explicit static-subset
##     opt-in, and EVERY cooked screen carries some, because the importer names
##     its gaps instead of hiding them;
##   * a bound contract that produces no draws is a rejection, never an empty
##     screen presented as a success.

const EXPECTED_SCHEMA := "openbfme.retail-screen-scene"
const EXPECTED_SCHEMA_VERSION := 0
const CONTRACT_DIRECTORY := "data/ui/screens/"
const ATLAS_PREFIX := "assets/ui/screens/"

## The frame-selection rules the importer is allowed to have used. A contract
## that names anything else was produced by a policy this runtime has not been
## told about, and binding it would silently present an unknown state.
const KNOWN_FRAME_RULES := [
	"authored-open-label",
	"no-authored-label-frame-zero",
	"caller-supplied-frame",
]

const MAX_CONTRACT_BYTES := 64 * 1024 * 1024
const MAX_DRAWS := 100000
const MAX_ATLASES := 64

## True once a pack declared this screen at all.
var contract_declared := false
## True once a declared contract validated.
var contract_ready := false
## True once there is at least one executable retail draw.
var presentation_ready := false
## True when the contract has zero blockers.
var parity_ready := false
## True when the caller opted into the bounded static subset.
var static_subset_opt_in := false

var movie_name := ""
## The authored frame this screen was cooked at, and the label that named it.
var frame_index := -1
var frame_label := ""
var frame_rule := ""

var blocker_count := 0
var draw_count := 0
var atlas_count := 0
var button_instance_count := 0
var source_aggregate_sha256 := ""
var diagnostics: Array = []

var _authored_resolution := Vector2(1024.0, 768.0)
var _atlases: Dictionary = {}
var _display_items: Array = []
var _button_regions: Array = []


func _ready() -> void:
	mouse_filter = Control.MOUSE_FILTER_IGNORE
	resized.connect(queue_redraw)


func reset_runtime() -> void:
	_reset()


func _reset() -> void:
	contract_declared = false
	contract_ready = false
	presentation_ready = false
	parity_ready = false
	static_subset_opt_in = false
	movie_name = ""
	frame_index = -1
	frame_label = ""
	frame_rule = ""
	blocker_count = 0
	draw_count = 0
	atlas_count = 0
	button_instance_count = 0
	source_aggregate_sha256 = ""
	diagnostics = []
	_authored_resolution = Vector2(1024.0, 768.0)
	_atlases = {}
	_display_items = []
	_button_regions = []
	queue_redraw()


func _fail(reason: String) -> bool:
	diagnostics.append({"code": "screen-apt-runtime-rejected", "reason": reason})
	contract_ready = false
	presentation_ready = false
	parity_ready = false
	queue_redraw()
	return false


## Bind the cooked contract for one screen movie out of a content pack.
##
## Returns true and leaves `contract_declared` false when the pack simply does
## not ship this screen - that is the fallback path, not an error.
func configure_from_pack(
	pack_root: String, movie: String, allow_static_subset := false
) -> bool:
	_reset()
	if not _is_bare_movie_name(movie):
		return _fail("screen movie name is not a bare identifier")
	var mod_loader := get_node_or_null("/root/ModLoader")
	if mod_loader == null:
		return _fail("screen APT runtime requires the ModLoader autoload")
	var relative := CONTRACT_DIRECTORY + movie.to_lower() + "/scene-contract.json"
	var path: String = mod_loader.resolve_pack_path(pack_root, relative)
	if path == "" or not FileAccess.file_exists(path):
		return true
	contract_declared = true
	if not _safe_pack_file(pack_root, path, "json"):
		return _fail("declared screen contract is missing or escaped: " + relative)
	var document := _read_bounded_json(path)
	if document.is_empty():
		return _fail("screen contract is invalid or empty: " + relative)
	return configure_document(document, pack_root, movie, allow_static_subset)


func configure_document(
	document: Dictionary,
	pack_root: String,
	movie: String,
	allow_static_subset := false
) -> bool:
	_reset()
	contract_declared = true
	static_subset_opt_in = allow_static_subset
	if not _validate_identity(document, movie):
		return false
	if not _load_atlases(document, pack_root):
		return false
	if not _build_display_items(document):
		return false
	_build_button_regions(document)
	contract_ready = true
	parity_ready = blocker_count == 0
	if blocker_count > 0 and not allow_static_subset:
		return _fail(
			"screen APT has unsupported semantics; static subset was not explicitly enabled"
		)
	presentation_ready = draw_count > 0
	if not presentation_ready:
		return _fail("screen APT contract has no executable retail draws")
	if blocker_count > 0:
		diagnostics.append({
			"code": "screen-apt-static-subset-explicitly-enabled",
			"movie": movie_name,
			"blockerCount": blocker_count,
			"parityReady": false,
		})
	queue_redraw()
	return true


func _validate_identity(document: Dictionary, movie: String) -> bool:
	if (
		String(document.get("schema", "")) != EXPECTED_SCHEMA
		or int(document.get("schemaVersion", -1)) != EXPECTED_SCHEMA_VERSION
	):
		return _fail("unexpected screen APT schema")
	movie_name = String(document.get("movie", ""))
	if movie_name.to_lower() != movie.to_lower():
		return _fail("screen contract is for a different movie: " + movie_name)
	source_aggregate_sha256 = String(document.get("sourceAggregateSha256", ""))
	if not _is_sha256(source_aggregate_sha256):
		return _fail("screen source aggregate identity is invalid")

	var selection_value: Variant = document.get("frameSelection", {})
	if typeof(selection_value) != TYPE_DICTIONARY:
		return _fail("screen frame selection is missing")
	var selection := selection_value as Dictionary
	frame_rule = String(selection.get("rule", ""))
	if not KNOWN_FRAME_RULES.has(frame_rule):
		return _fail("screen was cooked by an unknown frame rule: " + frame_rule)
	frame_index = int(selection.get("frame", -1))
	if frame_index < 0:
		return _fail("screen frame index is out of bounds")
	# A movie with no authored label reports a null label; that is data, not a
	# gap, and it is exactly why the rule is carried alongside it.
	var label_value: Variant = selection.get("label", null)
	frame_label = String(label_value) if typeof(label_value) == TYPE_STRING else ""

	var stage_value: Variant = document.get("stage", {})
	if typeof(stage_value) != TYPE_DICTIONARY:
		return _fail("screen stage is missing")
	var stage := stage_value as Dictionary
	_authored_resolution = Vector2(
		float(stage.get("width", 0.0)), float(stage.get("height", 0.0))
	)
	if (
		not is_finite(_authored_resolution.x)
		or not is_finite(_authored_resolution.y)
		or _authored_resolution.x <= 0.0
		or _authored_resolution.y <= 0.0
	):
		return _fail("screen authored resolution is out of bounds")

	var blockers_value: Variant = document.get("blockers", [])
	if typeof(blockers_value) != TYPE_ARRAY:
		return _fail("screen blocker inventory is invalid")
	for blocker_value in blockers_value as Array:
		if typeof(blocker_value) != TYPE_DICTIONARY:
			return _fail("screen blocker row is invalid")
		if String((blocker_value as Dictionary).get("code", "")) == "":
			return _fail("screen blocker row lacks a code")
	blocker_count = (blockers_value as Array).size()
	return true


func _load_atlases(document: Dictionary, pack_root: String) -> bool:
	var inventory: Variant = document.get("atlases", [])
	if typeof(inventory) != TYPE_ARRAY:
		return _fail("screen atlas inventory is invalid")
	var rows := inventory as Array
	if rows.size() > MAX_ATLASES:
		return _fail("screen atlas inventory exceeds bounds")
	var mod_loader := get_node_or_null("/root/ModLoader")
	if mod_loader == null:
		return _fail("screen atlas loading requires the ModLoader autoload")
	for atlas_value in rows:
		if typeof(atlas_value) != TYPE_DICTIONARY:
			return _fail("screen atlas row is invalid")
		var relative := String((atlas_value as Dictionary).get("cookedPng", ""))
		if not relative.begins_with(ATLAS_PREFIX) or not relative.ends_with(".png"):
			return _fail("screen atlas inventory contains an unsafe path")
		var path: String = mod_loader.resolve_pack_path(pack_root, relative)
		if not _safe_pack_file(pack_root, path, "png"):
			return _fail("declared screen atlas is missing or escaped: " + relative)
		var image := Image.new()
		if image.load(path) != OK:
			return _fail("screen atlas could not be decoded: " + relative)
		_atlases[relative] = ImageTexture.create_from_image(image)
	atlas_count = _atlases.size()
	return true


func _build_display_items(document: Dictionary) -> bool:
	var draws_value: Variant = document.get("draws", [])
	if typeof(draws_value) != TYPE_ARRAY:
		return _fail("screen draw list is invalid")
	var rows := draws_value as Array
	if rows.size() > MAX_DRAWS:
		return _fail("screen draw list exceeds bounds")
	var items: Array = []
	for row_value in rows:
		if typeof(row_value) != TYPE_DICTIONARY:
			return _fail("screen draw row is invalid")
		var row := row_value as Dictionary
		var kind := String(row.get("kind", ""))
		if kind != "solid-triangle" and kind != "textured-triangle":
			return _fail("screen draw row has an unsupported kind: " + kind)
		var points := _vector2_array(row.get("points", []))
		if points.size() != 3:
			return _fail("screen draw row is not a triangle")
		var color := _color(row.get("color", []))
		var item := {
			"kind": kind,
			"points": points,
			"color": color,
			"displayOrder": int(row.get("displayOrder", 0)),
		}
		if kind == "textured-triangle":
			var atlas := String(row.get("atlas", ""))
			if not _atlases.has(atlas):
				return _fail("screen draw references an unbound atlas: " + atlas)
			var uvs := _vector2_array(row.get("uvs", []))
			if uvs.size() != 3:
				return _fail("screen textured draw lacks three UVs")
			item["texture"] = _atlases[atlas]
			item["uvs"] = uvs
		items.append(item)
	items.sort_custom(func(a, b): return int(a["displayOrder"]) < int(b["displayOrder"]))
	_display_items = items
	draw_count = items.size()
	return true


func _build_button_regions(document: Dictionary) -> void:
	var instances_value: Variant = document.get("buttonInstances", [])
	if typeof(instances_value) != TYPE_ARRAY:
		return
	var regions: Array = []
	for instance_value in instances_value as Array:
		if typeof(instance_value) != TYPE_DICTIONARY:
			continue
		var instance := instance_value as Dictionary
		var vertices := _vector2_array(instance.get("hitVertices", []))
		if vertices.is_empty():
			continue
		var translation := Vector2.ZERO
		var matrix := PackedFloat32Array([1.0, 0.0, 0.0, 1.0])
		var transform_value: Variant = instance.get("hitTransform", {})
		if typeof(transform_value) == TYPE_DICTIONARY:
			var hit := transform_value as Dictionary
			var translation_row: Variant = hit.get("translation", [])
			if typeof(translation_row) == TYPE_ARRAY and (translation_row as Array).size() == 2:
				translation = Vector2(
					float((translation_row as Array)[0]), float((translation_row as Array)[1])
				)
			var matrix_row: Variant = hit.get("matrix", [])
			if typeof(matrix_row) == TYPE_ARRAY and (matrix_row as Array).size() == 4:
				matrix = PackedFloat32Array([
					float((matrix_row as Array)[0]),
					float((matrix_row as Array)[1]),
					float((matrix_row as Array)[2]),
					float((matrix_row as Array)[3]),
				])
		var rect := Rect2()
		var first := true
		for vertex in vertices:
			var mapped := Vector2(
				matrix[0] * vertex.x + matrix[2] * vertex.y + translation.x,
				matrix[1] * vertex.x + matrix[3] * vertex.y + translation.y
			)
			if first:
				rect = Rect2(mapped, Vector2.ZERO)
				first = false
			else:
				rect = rect.expand(mapped)
		regions.append({
			"buttonId": String(instance.get("buttonId", "")),
			"path": String(instance.get("path", "")),
			"authoredRect": rect,
		})
	regions.sort_custom(func(a, b): return String(a["path"]) < String(b["path"]))
	_button_regions = regions
	button_instance_count = regions.size()


## Authored-space button hit rectangles, scaled into the current control size.
func button_regions() -> Array:
	var scale := _runtime_scale()
	var result: Array = []
	for region in _button_regions:
		var rect := region["authoredRect"] as Rect2
		result.append({
			"buttonId": String(region["buttonId"]),
			"path": String(region["path"]),
			"rect": Rect2(rect.position * scale, rect.size * scale),
		})
	return result


func runtime_metadata() -> Dictionary:
	return {
		"movie": movie_name,
		"frameIndex": frame_index,
		"frameLabel": frame_label,
		"frameRule": frame_rule,
		"contractDeclared": contract_declared,
		"contractReady": contract_ready,
		"presentationReady": presentation_ready,
		"parityReady": parity_ready,
		"staticSubsetOptIn": static_subset_opt_in,
		"sourceAggregateSha256": source_aggregate_sha256,
		"atlasCount": atlas_count,
		"drawCount": draw_count,
		"blockerCount": blocker_count,
		"buttonInstanceCount": button_instance_count,
	}


func _runtime_scale() -> Vector2:
	if _authored_resolution.x <= 0.0 or _authored_resolution.y <= 0.0:
		return Vector2.ONE
	return Vector2(size.x / _authored_resolution.x, size.y / _authored_resolution.y)


func _draw() -> void:
	if not presentation_ready:
		return
	var scale := _runtime_scale()
	for item in _display_items:
		var points := PackedVector2Array()
		for point in item["points"] as PackedVector2Array:
			points.append(point * scale)
		var color := item["color"] as Color
		if String(item["kind"]) == "solid-triangle":
			draw_colored_polygon(points, color)
		else:
			var colors := PackedColorArray([color, color, color])
			draw_polygon(points, colors, item["uvs"] as PackedVector2Array, item["texture"] as Texture2D)


func _is_bare_movie_name(movie: String) -> bool:
	if movie.is_empty() or movie.length() > 64:
		return false
	for index in movie.length():
		var code := movie.unicode_at(index)
		var is_digit := code >= 48 and code <= 57
		var is_upper := code >= 65 and code <= 90
		var is_lower := code >= 97 and code <= 122
		if not is_digit and not is_upper and not is_lower and code != 95:
			return false
	return true


func _vector2_array(value: Variant) -> PackedVector2Array:
	var result := PackedVector2Array()
	if typeof(value) != TYPE_ARRAY:
		return result
	for row in value as Array:
		if typeof(row) != TYPE_ARRAY or (row as Array).size() != 2:
			return PackedVector2Array()
		var x := float((row as Array)[0])
		var y := float((row as Array)[1])
		if not is_finite(x) or not is_finite(y):
			return PackedVector2Array()
		result.append(Vector2(x, y))
	return result


func _color(value: Variant) -> Color:
	if typeof(value) != TYPE_ARRAY or (value as Array).size() != 4:
		return Color.TRANSPARENT
	var row := value as Array
	for component in row:
		if not is_finite(float(component)):
			return Color.TRANSPARENT
	return Color(float(row[0]), float(row[1]), float(row[2]), float(row[3]))


func _is_sha256(value: String) -> bool:
	if value.length() != 64:
		return false
	for index in value.length():
		var code := value.unicode_at(index)
		var is_digit := code >= 48 and code <= 57
		var is_lower_hex := code >= 97 and code <= 102
		if not is_digit and not is_lower_hex:
			return false
	return true


func _safe_pack_file(pack_root: String, path: String, extension: String) -> bool:
	if path == "" or not FileAccess.file_exists(path):
		return false
	if not path.to_lower().ends_with("." + extension):
		return false
	var root := ProjectSettings.globalize_path(pack_root).simplify_path()
	var resolved := ProjectSettings.globalize_path(path).simplify_path()
	if root == "" or resolved == "":
		return false
	return resolved.begins_with(root)


func _read_bounded_json(path: String) -> Dictionary:
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null:
		return {}
	var length := file.get_length()
	if length <= 0 or length > MAX_CONTRACT_BYTES:
		file.close()
		return {}
	var text := file.get_as_text()
	file.close()
	var parsed: Variant = JSON.parse_string(text)
	if typeof(parsed) != TYPE_DICTIONARY:
		return {}
	return parsed as Dictionary
