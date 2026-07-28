class_name RetailShellAptRuntime
extends Control

## Presents the retail BFME2 main-menu shell from its own APT movie.
##
## Mirrors `res://src/retail_slice/retail_hud_apt_runtime.gd`: the importer
## cooks the retail `MainMenu`/`MenuExport`/`MenuFrameAndBg`/`GameWindowGadgets`
## /`Background`/`AptLevel0` closure into a bounded static draw contract plus
## hash-addressed atlases, and this node renders those exact retail triangles.
##
## Fail-closed by construction:
##   * no pack contract  -> `contract_declared` stays false and the caller keeps
##     its hand-built shell;
##   * a contract with unsupported semantics requires an explicit static-subset
##     opt-in, exactly like the palantir lane;
##   * the shell backdrop is a native 3D shellmap (`GameWindowGadgets::View3D`)
##     with no APT payload.  It is reported through `requires_native_backdrop`
##     so the caller keeps its own backdrop rather than the runtime inventing
##     substitute art.

const EXPECTED_SCHEMA := "openbfme.retail-shell-apt-runtime"
const EXPECTED_SCHEMA_VERSION := 0
const EXPECTED_SCENE_ID := "bfme2.ui.shell.mainmenu"
const PACK_FILE_KEY := "shellScene"
const ATLAS_PREFIX := "assets/ui/shell/atlases/"

const MAX_CONTRACT_BYTES := 64 * 1024 * 1024
const MAX_DRAWS := 100000
const MAX_ATLASES := 64

## True once a pack declared a shell contract at all.
var contract_declared := false
## True once a declared contract validated.
var contract_ready := false
## True once there is at least one executable retail draw.
var presentation_ready := false
## True when the contract has zero unsupported-semantics blockers.
var parity_ready := false
## True when the caller opted into the bounded static subset.
var static_subset_opt_in := false
## True when the contract reports a native View3D backdrop requirement.
var requires_native_backdrop := false

var blocker_count := 0
var draw_count := 0
var atlas_count := 0
var button_instance_count := 0
var aggregate_sha256 := ""
var diagnostics: Array = []

var _authored_resolution := Vector2(1024.0, 768.0)
var _atlases: Dictionary = {}
var _display_items: Array = []
var _button_regions: Array = []
var _configured_pack_root := ""


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
	requires_native_backdrop = false
	blocker_count = 0
	draw_count = 0
	atlas_count = 0
	button_instance_count = 0
	aggregate_sha256 = ""
	diagnostics = []
	_authored_resolution = Vector2(1024.0, 768.0)
	_atlases = {}
	_display_items = []
	_button_regions = []
	_configured_pack_root = ""
	queue_redraw()


func _fail(reason: String) -> bool:
	diagnostics.append({"code": "shell-apt-runtime-rejected", "reason": reason})
	contract_ready = false
	presentation_ready = false
	parity_ready = false
	queue_redraw()
	return false


## Load the shell contract a pack declares under `files.shellScene`.
##
## Returns true and leaves `contract_declared` false when the pack simply does
## not ship the shell bundle - that is the fallback path, not an error.
func configure_from_pack(pack_root: String, allow_static_subset := false) -> bool:
	_reset()
	var mod_loader := get_node_or_null("/root/ModLoader")
	if mod_loader == null:
		return _fail("shell APT runtime requires the ModLoader autoload")
	var pack_path: String = mod_loader.resolve_pack_path(pack_root, "pack.json")
	if pack_path == "" or not FileAccess.file_exists(pack_path):
		return _fail("shell APT runtime requires a contained pack document")
	var pack_value: Variant = mod_loader._read_json(pack_path)
	if typeof(pack_value) != TYPE_DICTIONARY:
		return _fail("shell APT runtime pack document is invalid")
	var files_value: Variant = (pack_value as Dictionary).get("files", {})
	if typeof(files_value) != TYPE_DICTIONARY:
		return _fail("shell APT runtime pack files table is invalid")
	var files := files_value as Dictionary
	if not files.has(PACK_FILE_KEY):
		return true
	contract_declared = true
	var relative := String(files.get(PACK_FILE_KEY, ""))
	var path: String = mod_loader.resolve_pack_path(pack_root, relative)
	if not _safe_pack_file(pack_root, path, "json"):
		return _fail("declared shell scene contract is missing or escaped")
	var document := _read_bounded_json(path)
	if document.is_empty():
		return _fail("shell scene contract is invalid or empty")
	return configure_document(document, pack_root, allow_static_subset)


func configure_document(
	document: Dictionary, pack_root: String, allow_static_subset := false
) -> bool:
	_reset()
	contract_declared = true
	static_subset_opt_in = allow_static_subset
	_configured_pack_root = pack_root
	if not _validate_identity(document):
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
			"shell APT has unsupported semantics; static subset was not explicitly enabled"
		)
	presentation_ready = draw_count > 0
	if not presentation_ready:
		return _fail("shell APT contract has no executable retail draws")
	if blocker_count > 0:
		diagnostics.append({
			"code": "shell-apt-static-subset-explicitly-enabled",
			"blockerCount": blocker_count,
			"parityReady": false,
		})
	if requires_native_backdrop:
		diagnostics.append({
			"code": "shell-backdrop-requires-native-view3d",
			"detail": "retail draws a live 3D shellmap; caller keeps its own backdrop",
		})
	queue_redraw()
	return true


func _validate_identity(document: Dictionary) -> bool:
	if (
		String(document.get("schema", "")) != EXPECTED_SCHEMA
		or int(document.get("schemaVersion", -1)) != EXPECTED_SCHEMA_VERSION
		or String(document.get("sceneId", "")) != EXPECTED_SCENE_ID
	):
		return _fail("unexpected shell APT schema or scene identity")
	aggregate_sha256 = String(document.get("aggregateSha256", ""))
	if not _is_sha256(aggregate_sha256):
		return _fail("shell APT aggregate identity is invalid")
	var resolution_value: Variant = document.get("authoredResolution", [])
	if typeof(resolution_value) != TYPE_ARRAY or (resolution_value as Array).size() != 2:
		return _fail("shell authored resolution is invalid")
	var resolution := resolution_value as Array
	_authored_resolution = Vector2(float(resolution[0]), float(resolution[1]))
	if (
		not is_finite(_authored_resolution.x)
		or not is_finite(_authored_resolution.y)
		or _authored_resolution.x <= 0.0
		or _authored_resolution.y <= 0.0
	):
		return _fail("shell authored resolution is out of bounds")
	var policy_value: Variant = document.get("renderPolicy", {})
	if typeof(policy_value) != TYPE_DICTIONARY:
		return _fail("shell render policy is missing")
	var policy := policy_value as Dictionary
	if (
		bool(policy.get("actionScriptExecuted", true))
		or String(policy.get("defaultRuntimeMode", "")) != "fail-closed"
		or not bool(policy.get("staticSubsetRequiresExplicitOptIn", false))
		or bool(policy.get("syntheticFallbackAllowed", true))
		or not bool(policy.get("exactTimelineDisplayLists", false))
		or bool(policy.get("timelinePlaybackBound", true))
		or bool(policy.get("nativeBackdropBound", true))
	):
		return _fail("shell render policy is not the bounded fail-closed contract")
	var blockers_value: Variant = document.get("unsupportedSemantics", [])
	if typeof(blockers_value) != TYPE_ARRAY:
		return _fail("shell unsupported-semantics inventory is invalid")
	for blocker_value in blockers_value as Array:
		if typeof(blocker_value) != TYPE_DICTIONARY:
			return _fail("shell blocker row is invalid")
		var blocker := blocker_value as Dictionary
		if String(blocker.get("code", "")) == "":
			return _fail("shell blocker row lacks a code")
		if String(blocker.get("code", "")) == "shell-backdrop-requires-native-view3d":
			requires_native_backdrop = true
	blocker_count = (blockers_value as Array).size()
	return true


func _load_atlases(document: Dictionary, pack_root: String) -> bool:
	var inventory: Variant = document.get("atlases", [])
	if typeof(inventory) != TYPE_ARRAY:
		return _fail("shell atlas inventory is invalid")
	var rows := inventory as Array
	if rows.size() > MAX_ATLASES:
		return _fail("shell atlas inventory exceeds bounds")
	var mod_loader := get_node_or_null("/root/ModLoader")
	for atlas_value in rows:
		var relative := String(atlas_value)
		if not relative.begins_with(ATLAS_PREFIX) or not relative.ends_with(".png"):
			return _fail("shell atlas inventory contains an unsafe path")
		if mod_loader == null:
			return _fail("shell atlas loading requires the ModLoader autoload")
		var path: String = mod_loader.resolve_pack_path(pack_root, relative)
		if not _safe_pack_file(pack_root, path, "png"):
			return _fail("declared shell atlas is missing or escaped: " + relative)
		var image := Image.new()
		if image.load(path) != OK:
			return _fail("shell atlas could not be decoded: " + relative)
		_atlases[relative] = ImageTexture.create_from_image(image)
	atlas_count = _atlases.size()
	return true


func _build_display_items(document: Dictionary) -> bool:
	var draws_value: Variant = document.get("draws", [])
	if typeof(draws_value) != TYPE_ARRAY:
		return _fail("shell draw list is invalid")
	var rows := draws_value as Array
	if rows.size() > MAX_DRAWS:
		return _fail("shell draw list exceeds bounds")
	var items: Array = []
	for row_value in rows:
		if typeof(row_value) != TYPE_DICTIONARY:
			return _fail("shell draw row is invalid")
		var row := row_value as Dictionary
		var kind := String(row.get("kind", ""))
		if kind != "solid-triangle" and kind != "textured-triangle":
			return _fail("shell draw row has an unsupported kind: " + kind)
		var points := _vector2_array(row.get("points", []))
		if points.size() != 3:
			return _fail("shell draw row is not a triangle")
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
				return _fail("shell draw references an unbound atlas: " + atlas)
			var uvs := _vector2_array(row.get("uvs", []))
			if uvs.size() != 3:
				return _fail("shell textured draw lacks three UVs")
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
		var transform_value: Variant = instance.get("hitTransform", {})
		var translation := Vector2.ZERO
		var matrix := PackedFloat32Array([1.0, 0.0, 0.0, 1.0])
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
		"contractDeclared": contract_declared,
		"contractReady": contract_ready,
		"presentationReady": presentation_ready,
		"parityReady": parity_ready,
		"staticSubsetOptIn": static_subset_opt_in,
		"requiresNativeBackdrop": requires_native_backdrop,
		"aggregateSha256": aggregate_sha256,
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
