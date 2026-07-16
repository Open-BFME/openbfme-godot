class_name RetailSelectionDecal
extends Node3D
## Source-textured Men battalion selection decal.
##
## BFME2 declares MenGenericLevel1 as a SHADOW_MERGE_DECAL using two 256x256
## retail textures. This runtime keeps those exact leaves and source radii,
## merges the per-member footprints once, and projects the result onto the
## terrain. Pixel-level merge/color/throb parity remains an oracle gate.

const CONTRACT_PATH := "effects/men-selection-decal.json"
const CONTRACT_SCHEMA := "openbfme.men-selection-decal"
const CONTRACT_SCHEMA_VERSION := 0
const SOURCE_STYLE := "SHADOW_MERGE_DECAL"
const SOURCE_MEMBER_LIMIT := 40
const SOURCE_TEXTURE_SIZE := Vector2i(256, 256)
const PIXELS_PER_SOURCE_UNIT := 2.0
const TERRAIN_DECAL_CULL_MASK := 1 << 1
# Median selected-Men outline color sampled from the 2026-07-15 BFME2 1.06
# Fords retail capture (unit movement and Warg-lair combat sequences).
const PLAYER_SELECTION_COLOR := Color(0.314, 0.380, 0.624, 1.0)
const SOURCE_TEXTURE_EVIDENCE := [
	{
		"role": "Texture",
		"name": "decal_G_level1",
		"virtualPath": "art/compiledtextures/de/decal_g_level1.dds",
		"sha256": "ac07d037d5bb2a792737300230455f1cc4e2452de79870784e01e5e499430ef5",
	},
	{
		"role": "Texture2",
		"name": "decal_good_CO",
		"virtualPath": "art/compiledtextures/de/decal_good_co.dds",
		"sha256": "4c7147389e37eccd7fde6f05329c607a07fc59f08c5e80712fb3aee8389337d6",
	},
]

static var _source_image_cache: Dictionary = {}
static var _merged_texture_cache: Dictionary = {}

var contract_ready := false
var error := ""
var source_status := "unconfigured"
var source_member_count := 0
var merged_texture_size := Vector2i.ZERO
var source_min_radius := 0.0
var source_max_radius := 0.0
var source_opacity_min := 0.0
var source_opacity_max := 0.0
var _pack_root := ""
var _local_scale := 0.0
var _member_positions: Array[Vector3] = []
var _living_mask: Array[bool] = []
var _source_textures: Array[Image] = []
var _decal: Decal
var _selected := false


func configure(
	pack_root: String,
	local_scale: float,
	member_positions: Array[Vector3]
) -> String:
	contract_ready = false
	error = ""
	source_status = "fail-closed"
	_pack_root = pack_root
	_local_scale = local_scale
	_member_positions = member_positions.duplicate()
	_living_mask.clear()
	for _position in _member_positions:
		_living_mask.append(true)
	if _pack_root == "" or _pack_root.begins_with("res://"):
		return _fail("selection decal requires the selected private retail pack")
	if not is_finite(_local_scale) or _local_scale <= 0.0:
		return _fail("selection decal requires a positive source-to-local scale")
	if _member_positions.is_empty() or _member_positions.size() > SOURCE_MEMBER_LIMIT:
		return _fail("selection decal member count is outside the source limit")
	var contract_path := _resolve_pack_path(CONTRACT_PATH)
	if not _path_is_within_pack(contract_path) or not FileAccess.file_exists(contract_path):
		return _fail("selected pack has no Men selection decal contract")
	var contract_value: Variant = _read_json(contract_path)
	if typeof(contract_value) != TYPE_DICTIONARY:
		return _fail("Men selection decal contract is not a JSON object")
	var contract := contract_value as Dictionary
	var contract_error := _validate_contract(contract)
	if contract_error != "":
		return _fail(contract_error)
	var texture_paths: Array = contract.get("textures", []) as Array
	_source_textures.clear()
	for texture_value in texture_paths:
		var relative := String(texture_value)
		var resolved := _resolve_pack_path(relative)
		if not _path_is_within_pack(resolved) or not FileAccess.file_exists(resolved):
			return _fail("Men selection decal texture is missing or escapes the pack")
		var cache_key := resolved.to_lower()
		var image: Image = _source_image_cache.get(cache_key) as Image
		if image == null:
			image = Image.new()
			if image.load(resolved) != OK or image.get_size() != SOURCE_TEXTURE_SIZE:
				return _fail("Men selection decal texture is not an exact 256x256 image")
			image.convert(Image.FORMAT_RGBA8)
			if not _has_visible_alpha(image):
				return _fail("Men selection decal texture has no visible alpha")
			_source_image_cache[cache_key] = image
		_source_textures.append(image)
	if _source_textures.size() != 2:
		return _fail("Men selection decal requires both source textures")
	_build_decal()
	if _decal == null:
		return _fail("Men selection decal could not create its projection node")
	contract_ready = true
	source_member_count = _member_positions.size()
	source_status = "source-textures-radii-and-merge-style-bound-oracle-color-throb-pending"
	return ""


func set_selected(value: bool) -> void:
	_selected = value
	if _decal != null:
		if contract_ready and value and _decal.texture_albedo == null:
			_build_merged_texture()
		_decal.visible = contract_ready and value and _decal.texture_albedo != null


func set_living_mask(mask: Array[bool]) -> void:
	if not contract_ready or mask.size() != _member_positions.size() or mask == _living_mask:
		return
	_living_mask = mask.duplicate()
	if _selected or (_decal != null and _decal.texture_albedo != null):
		_build_merged_texture()


func set_member_positions(positions: Array[Vector3]) -> void:
	if not contract_ready or positions.size() != _member_positions.size():
		return
	var changed := false
	for index in range(positions.size()):
		if positions[index].distance_squared_to(_member_positions[index]) > 0.0001:
			changed = true
			break
	if not changed:
		return
	_member_positions = positions.duplicate()
	if _selected or (_decal != null and _decal.texture_albedo != null):
		_build_merged_texture()


func projected_decal_count() -> int:
	return int(_decal != null and is_instance_valid(_decal))


func _validate_contract(contract: Dictionary) -> String:
	if (
		String(contract.get("schema", "")) != CONTRACT_SCHEMA
		or int(contract.get("schemaVersion", -1)) != CONTRACT_SCHEMA_VERSION
		or String(contract.get("experienceLevel", "")) != "MenGenericLevel1"
		or String(contract.get("style", "")) != SOURCE_STYLE
		or int(contract.get("maxSelectedUnits", -1)) != SOURCE_MEMBER_LIMIT
	):
		return "Men selection decal identity or style changed"
	var source: Variant = contract.get("source")
	if typeof(source) != TYPE_DICTIONARY:
		return "Men selection decal source evidence is missing"
	var source_contract := source as Dictionary
	if (
		String(source_contract.get("ini", "")) != "data/ini/experiencelevels.ini"
		or source_contract.get("textures", []) != SOURCE_TEXTURE_EVIDENCE
	):
		return "Men selection decal source evidence changed"
	var textures: Variant = contract.get("textures")
	if typeof(textures) != TYPE_ARRAY or (textures as Array).size() != 2:
		return "Men selection decal texture list changed"
	for texture_value in textures as Array:
		var texture := String(texture_value).replace("\\", "/")
		if not texture.begins_with("assets/textures/selection/") or texture.get_extension().to_lower() != "png":
			return "Men selection decal texture route is unsafe"
	source_min_radius = float(contract.get("minRadiusSource", 0.0))
	source_max_radius = float(contract.get("maxRadiusSource", 0.0))
	source_opacity_min = float(contract.get("opacityMin", -1.0))
	source_opacity_max = float(contract.get("opacityMax", -1.0))
	if (
		not is_equal_approx(source_min_radius, 50.0)
		or not is_equal_approx(source_max_radius, 200.0)
		or not is_equal_approx(source_opacity_min, 0.8)
		or not is_equal_approx(source_opacity_max, 1.0)
	):
		return "Men selection decal source bounds changed"
	return ""


func _has_visible_alpha(image: Image) -> bool:
	for y in range(image.get_height()):
		for x in range(image.get_width()):
			if image.get_pixel(x, y).a > 0.0:
				return true
	return false


func _resolve_pack_path(relative: String) -> String:
	return ProjectSettings.globalize_path(_pack_root).replace("\\", "/").simplify_path().path_join(relative.replace("\\", "/")).simplify_path()


func _path_is_within_pack(path: String) -> bool:
	var root_path := ProjectSettings.globalize_path(_pack_root).replace("\\", "/").simplify_path().trim_suffix("/")
	var candidate := ProjectSettings.globalize_path(path).replace("\\", "/").simplify_path()
	return candidate.to_lower().begins_with(root_path.to_lower() + "/")


func _read_json(path: String) -> Variant:
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null:
		return null
	var value: Variant = JSON.parse_string(file.get_as_text())
	file.close()
	return value


func _build_decal() -> void:
	if _decal == null:
		_decal = Decal.new()
		_decal.name = "SourceMenSelectionMergeDecal"
		_decal.cull_mask = TERRAIN_DECAL_CULL_MASK
		_decal.modulate = Color(
			PLAYER_SELECTION_COLOR.r,
			PLAYER_SELECTION_COLOR.g,
			PLAYER_SELECTION_COLOR.b,
			source_opacity_max
		)
		_decal.upper_fade = 0.0
		_decal.lower_fade = 0.0
		_decal.visible = false
		add_child(_decal)


# Presentation calibration: the contract's 50-source-unit radius is the
# horde-level decal value; drawing it per member merges into an oversized
# blob. The rendered per-member radius is tightened while the source contract
# values stay untouched; final parity is a capture-pair oracle item.
const MEMBER_RADIUS_PRESENTATION_SCALE := 0.45


func _build_merged_texture() -> void:
	if _decal == null or _source_textures.size() != 2:
		return
	var member_radius := source_min_radius * MEMBER_RADIUS_PRESENTATION_SCALE
	var radius_pixels := ceili(member_radius * PIXELS_PER_SOURCE_UNIT)
	var diameter_pixels := radius_pixels * 2
	var source_centers: Array[Vector2] = []
	var minimum := Vector2(INF, INF)
	var maximum := Vector2(-INF, -INF)
	for index in range(_member_positions.size()):
		if index >= _living_mask.size() or not _living_mask[index]:
			continue
		var position := _member_positions[index]
		var center := Vector2(position.x, position.z) / _local_scale
		source_centers.append(center)
		minimum = minimum.min(center - Vector2.ONE * member_radius)
		maximum = maximum.max(center + Vector2.ONE * member_radius)
	if source_centers.is_empty():
		_decal.texture_albedo = null
		merged_texture_size = Vector2i.ZERO
		return
	var source_extent := maximum - minimum
	var canvas_size := Vector2i(
		maxi(1, ceili(source_extent.x * PIXELS_PER_SOURCE_UNIT)),
		maxi(1, ceili(source_extent.y * PIXELS_PER_SOURCE_UNIT))
	)
	var local_center := (minimum + maximum) * 0.5 * _local_scale
	var decal_size := Vector3(source_extent.x * _local_scale, 4.0, source_extent.y * _local_scale)
	var cache_key := _merged_cache_key()
	var cached_value: Variant = _merged_texture_cache.get(cache_key)
	if typeof(cached_value) == TYPE_DICTIONARY:
		var cached := cached_value as Dictionary
		var cached_texture := cached.get("texture") as Texture2D
		if cached_texture != null:
			_decal.texture_albedo = cached_texture
			merged_texture_size = cached.get("size", canvas_size) as Vector2i
			_decal.position = Vector3(local_center.x, 1.5, local_center.y)
			_decal.size = decal_size
			return
	var coverage := Image.create(canvas_size.x, canvas_size.y, false, Image.FORMAT_RGBA8)
	coverage.fill(Color.TRANSPARENT)
	var ornament := Image.create(canvas_size.x, canvas_size.y, false, Image.FORMAT_RGBA8)
	ornament.fill(Color.TRANSPARENT)
	var coverage_stamp := _source_textures[0].duplicate()
	coverage_stamp.resize(diameter_pixels, diameter_pixels, Image.INTERPOLATE_BILINEAR)
	var ornament_stamp := _source_textures[1].duplicate()
	ornament_stamp.resize(diameter_pixels, diameter_pixels, Image.INTERPOLATE_BILINEAR)
	for center in source_centers:
		var pixel_center := ((center as Vector2) - minimum) * PIXELS_PER_SOURCE_UNIT
		var destination := Vector2i(
			roundi(pixel_center.x) - radius_pixels,
			roundi(pixel_center.y) - radius_pixels
		)
		coverage.blend_rect(coverage_stamp, Rect2i(Vector2i.ZERO, coverage_stamp.get_size()), destination)
		ornament.blend_rect(ornament_stamp, Rect2i(Vector2i.ZERO, ornament_stamp.get_size()), destination)
	# SHADOW_MERGE_DECAL is a merged outline, not fifteen independently blended
	# opaque disks.  Texture supplies the union coverage and Texture2 supplies
	# the authored edge ornament.  Keeping only the inner erosion difference
	# removes covered interior pixels and suppresses seams between members.
	var canvas := _compose_merged_outline(coverage, ornament, maxi(2, roundi(float(diameter_pixels) * 0.035)))
	var merged_texture := ImageTexture.create_from_image(canvas)
	_decal.texture_albedo = merged_texture
	merged_texture_size = canvas_size
	_decal.position = Vector3(local_center.x, 1.5, local_center.y)
	_decal.size = decal_size
	_merged_texture_cache[cache_key] = {
		"texture": merged_texture,
		"size": canvas_size,
	}


func _merged_cache_key() -> String:
	var fields := [
		_pack_root.to_lower(),
		"%.8f" % _local_scale,
		"%.3f" % source_min_radius,
	]
	for index in range(_member_positions.size()):
		var position := _member_positions[index]
		fields.append("%.4f,%.4f,%d" % [position.x, position.z, int(index < _living_mask.size() and _living_mask[index])])
	return "|".join(fields)


func _compose_merged_outline(coverage: Image, ornament: Image, edge_width: int) -> Image:
	var result := Image.create(coverage.get_width(), coverage.get_height(), false, Image.FORMAT_RGBA8)
	result.fill(Color.TRANSPARENT)
	var offsets := [
		Vector2i(edge_width, 0), Vector2i(-edge_width, 0),
		Vector2i(0, edge_width), Vector2i(0, -edge_width),
		Vector2i(edge_width, edge_width), Vector2i(edge_width, -edge_width),
		Vector2i(-edge_width, edge_width), Vector2i(-edge_width, -edge_width),
	]
	for y in range(coverage.get_height()):
		for x in range(coverage.get_width()):
			var coverage_alpha := coverage.get_pixel(x, y).a
			if coverage_alpha <= 0.0:
				continue
			var eroded_alpha := 1.0
			for offset in offsets:
				var sample := Vector2i(x, y) + (offset as Vector2i)
				if sample.x < 0 or sample.y < 0 or sample.x >= coverage.get_width() or sample.y >= coverage.get_height():
					eroded_alpha = 0.0
					break
				eroded_alpha = minf(eroded_alpha, coverage.get_pixelv(sample).a)
			var edge_alpha := maxf(0.0, coverage_alpha - eroded_alpha)
			if edge_alpha <= 0.0:
				continue
			var ornament_pixel := ornament.get_pixel(x, y)
			ornament_pixel.a *= edge_alpha
			result.set_pixel(x, y, ornament_pixel)
	return result


func _fail(reason: String) -> String:
	error = reason
	contract_ready = false
	source_status = "fail-closed: " + reason
	if _decal != null:
		_decal.visible = false
	return reason
