class_name RetailRoadMaterialBuilder
extends RefCounted
## Rehydrates only the five validated retail Road textures mounted by map data.
##
## The map-data boundary has already checked containment, PNG decoding, and the
## provenance manifest. This builder re-reads and re-hashes each file before it
## becomes a GPU resource so a changed payload cannot slip between validation
## and rendering.

var error := ""
var materials: Dictionary = {}
var textures: Dictionary = {}
var texture_count := 0


func build(map_data: RetailMapData) -> Dictionary:
	_reset()
	if map_data == null or not map_data.ready:
		return _fail("road materials require validated retail map data")
	if map_data.road_material_count <= 0 or map_data.road_material_catalog.size() != map_data.road_material_count or map_data.road_material_count != map_data.road_type_ids.size():
		return _fail("road-material catalog is incomplete")
	var parsed_ids: Array[String] = []
	for row_value in map_data.road_material_catalog:
		var row: Dictionary = row_value
		var road_id := String(row.get("id", ""))
		var path := String(row.get("texture_path", ""))
		var byte_length := int(row.get("texture_byte_length", -1))
		var digest := String(row.get("texture_sha256", "")).to_lower()
		if road_id == "" or parsed_ids.has(road_id) or path == "" or not ModLoader.path_is_within(map_data.map_root, path) or not ModLoader.path_is_within(map_data.pack_root, path):
			return _fail("road texture escaped its validated retail map")
		var png_bytes := _read_exact(path, byte_length)
		if png_bytes.is_empty() or _sha256(png_bytes).to_lower() != digest:
			return _fail("road texture changed after provenance validation")
		var image := Image.new()
		if image.load_png_from_buffer(png_bytes) != OK or image.is_empty():
			return _fail("road texture PNG could not be decoded")
		if image.get_width() != int(row.get("texture_width", 0)) or image.get_height() != int(row.get("texture_height", 0)):
			return _fail("road texture dimensions changed after validation")
		if image.get_format() != Image.FORMAT_RGBA8:
			image.convert(Image.FORMAT_RGBA8)
		if image.generate_mipmaps() != OK:
			return _fail("road texture mipmaps could not be generated")
		var texture := ImageTexture.create_from_image(image)
		if texture == null or texture.get_width() != image.get_width() or texture.get_height() != image.get_height():
			return _fail("road texture GPU resource could not be created")
		var material := StandardMaterial3D.new()
		material.albedo_texture = texture
		material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
		material.cull_mode = BaseMaterial3D.CULL_DISABLED
		material.depth_draw_mode = BaseMaterial3D.DEPTH_DRAW_DISABLED
		material.no_depth_test = false
		material.texture_filter = BaseMaterial3D.TEXTURE_FILTER_LINEAR_WITH_MIPMAPS_ANISOTROPIC
		material.texture_repeat = true
		material.roughness = 0.96
		material.metallic = 0.0
		material.set_meta("source", "converted-retail-sage-road-texture")
		material.set_meta("road_id", road_id)
		material.set_meta("texture_path", path)
		material.set_meta("texture_sha256", digest)
		materials[road_id] = material
		textures[road_id] = texture
		parsed_ids.append(road_id)
	if parsed_ids != map_data.road_type_ids or materials.size() != map_data.road_material_count:
		return _fail("road material resources do not exactly cover the map road IDs")
	texture_count = textures.size()
	return materials


func _read_exact(path: String, expected_size: int) -> PackedByteArray:
	if expected_size <= 0:
		return PackedByteArray()
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null or file.get_length() != expected_size:
		if file != null:
			file.close()
		return PackedByteArray()
	var bytes := file.get_buffer(expected_size)
	file.close()
	return bytes if bytes.size() == expected_size else PackedByteArray()


func _sha256(bytes: PackedByteArray) -> String:
	var context := HashingContext.new()
	if context.start(HashingContext.HASH_SHA256) != OK or context.update(bytes) != OK:
		return ""
	return context.finish().hex_encode()


func _fail(message: String) -> Dictionary:
	if error == "":
		error = message
	materials.clear()
	textures.clear()
	texture_count = 0
	return {}


func _reset() -> void:
	error = ""
	materials.clear()
	textures.clear()
	texture_count = 0
