class_name RetailTerrainMaterialBuilder
extends RefCounted
## Builds the Fords terrain material directly from the selected cooked pack.
##
## The CPU resolves SAGE's packed tile/description references into bounded data
## textures. The shader follows OpenSAGE's edge, three-way, and cliff UV math.

const RUNTIME_TEXTURE_ARRAY_DIMENSION := 256

var error := ""
var texture_array: Texture2DArray
var tile_data_texture: ImageTexture
var blend_data_texture: ImageTexture
var texture_info_texture: ImageTexture
var cliff_info_texture: ImageTexture
var material: ShaderMaterial
var texture_array_size := 0
var texture_array_dimension := 0
var tile_data_cell_count := 0


func build(map_data: RetailMapData) -> ShaderMaterial:
	_reset()
	if map_data == null or not map_data.ready:
		return _fail("terrain material requires validated retail map data")
	if map_data.terrain_material_catalog.size() != map_data.terrain_texture_count or map_data.terrain_texture_count <= 0:
		return _fail("terrain material catalog is incomplete")
	if not _build_texture_array(map_data):
		return null
	if not _build_data_textures(map_data):
		return null
	var shader := Shader.new()
	shader.code = _shader_source()
	material = ShaderMaterial.new()
	material.shader = shader
	material.set_shader_parameter("terrain_textures", texture_array)
	material.set_shader_parameter("terrain_tile_data", tile_data_texture)
	material.set_shader_parameter("terrain_blend_data", blend_data_texture)
	material.set_shader_parameter("terrain_texture_info", texture_info_texture)
	material.set_shader_parameter("terrain_cliff_info", cliff_info_texture)
	material.set_shader_parameter("terrain_grid_size", Vector2(map_data.width, map_data.height))
	material.set_shader_parameter("terrain_texture_count", map_data.terrain_texture_count)
	material.set_shader_parameter("terrain_cliff_mapping_count", map_data.terrain_cliff_mappings.size())
	material.set_shader_parameter("passability_debug_enabled", false)
	material.set_shader_parameter("sage_lighting_enabled", true)
	material.set_shader_parameter("sage_ambient_color", Vector3.ZERO)
	for index in range(3):
		material.set_shader_parameter("sage_light_%d_color" % index, Vector3.ZERO)
		material.set_shader_parameter("sage_light_%d_direction" % index, Vector3.DOWN)
	material.set_meta("source", "cooked-retail-terrain-materials-and-sage-blends")
	material.set_meta("texture_array_layers", texture_array_size)
	material.set_meta("three_way_blends", map_data.terrain_nonzero_three_way_blend_cell_count)
	material.set_meta("cliff_mappings", map_data.terrain_cliff_mappings.size())
	return material


func _build_texture_array(map_data: RetailMapData) -> bool:
	# The mounted catalog contains 128, 256, and one 512 source. A common 256
	# layer size preserves each texture's normalized SAGE cell layout while
	# keeping the private-slice startup and GPU footprint bounded.
	texture_array_dimension = RUNTIME_TEXTURE_ARRAY_DIMENSION
	if texture_array_dimension <= 0:
		return _fail_bool("terrain texture array has no valid target dimension")
	var layer_count := map_data.terrain_material_catalog.size()
	var images: Array[Image] = []
	images.resize(layer_count)
	var layer_errors: Array[String] = []
	layer_errors.resize(layer_count)
	# Per-layer work (bounded read, hash, decode, resize, mipmaps) is pure image
	# processing against immutable pack files, so it fans out across the worker
	# pool; the Texture2DArray assembly and first-error reporting stay ordered
	# on this thread exactly as the sequential build did.
	var decode_layer := func(index: int) -> void:
		var row: Dictionary = map_data.terrain_material_catalog[index]
		var path := String(row.get("png_path", ""))
		var expected_bytes := int(row.get("png_byte_length", -1))
		# Terrain textures live in the pack-scope material catalog
		# (assets/terrain/...), not under the individual map folder. Containment
		# is pack-root only so multi-map packs (five-maps) can share textures.
		if path == "" or not ModLoader.path_is_within(map_data.pack_root, path):
			layer_errors[index] = "terrain texture escaped the selected retail pack"
			return
		var png_bytes := _read_exact(path, expected_bytes)
		if png_bytes.is_empty() or _sha256(png_bytes).to_lower() != String(row.get("png_sha256", "")).to_lower():
			layer_errors[index] = "terrain texture changed after catalog validation"
			return
		var image := Image.new()
		if image.load_png_from_buffer(png_bytes) != OK:
			layer_errors[index] = "terrain texture PNG could not be decoded"
			return
		var declared_width := int(row.get("width", 0))
		var declared_height := int(row.get("height", 0))
		if image.get_width() != declared_width or image.get_height() != declared_height:
			layer_errors[index] = "terrain texture dimensions do not match the material catalog"
			return
		if image.get_format() != Image.FORMAT_RGBA8:
			image.convert(Image.FORMAT_RGBA8)
		if image.get_width() != texture_array_dimension or image.get_height() != texture_array_dimension:
			image.resize(texture_array_dimension, texture_array_dimension, Image.INTERPOLATE_LANCZOS)
		if image.generate_mipmaps() != OK:
			layer_errors[index] = "terrain texture mipmaps could not be generated"
			return
		images[index] = image
	if layer_count > 1 and OS.get_processor_count() > 1:
		var group := WorkerThreadPool.add_group_task(
			func(element: int) -> void: decode_layer.call(element),
			layer_count
		)
		WorkerThreadPool.wait_for_group_task_completion(group)
	else:
		for index in layer_count:
			decode_layer.call(index)
	for index in layer_count:
		if layer_errors[index] != "":
			return _fail_bool(layer_errors[index])
	texture_array = Texture2DArray.new()
	if texture_array.create_from_images(images) != OK:
		texture_array = null
		return _fail_bool("terrain Texture2DArray could not be created")
	texture_array_size = texture_array.get_layers()
	return texture_array_size == map_data.terrain_texture_count


func _build_data_textures(map_data: RetailMapData) -> bool:
	var cell_count := map_data.width * map_data.height
	if map_data.terrain_tile_indices.size() != cell_count or map_data.terrain_blend_cells.size() != cell_count or map_data.terrain_three_way_blend_cells.size() != cell_count or map_data.terrain_cliff_cells.size() != cell_count:
		return _fail_bool("terrain source grids are incomplete")
	# Resolve the small description table once.  The previous per-cell helper
	# allocated two PackedInt32Arrays for every terrain sample (over 350,000
	# temporary arrays on Fords), even though blend descriptions are immutable.
	# These lookup arrays preserve the exact same source indices/directions and
	# flags while making the full source grid a straight bounded copy.
	var blend_count := map_data.terrain_blend_descriptions.size()
	var blend_textures := PackedInt32Array()
	var blend_directions := PackedInt32Array()
	var blend_flags := PackedInt32Array()
	blend_textures.resize(blend_count + 1)
	blend_directions.resize(blend_count + 1)
	blend_flags.resize(blend_count + 1)
	blend_textures[0] = -1
	for description_index in range(blend_count):
		var description: Dictionary = map_data.terrain_blend_descriptions[description_index]
		var lookup_index := description_index + 1
		blend_textures[lookup_index] = int(description.get("secondary_texture_index", -1))
		blend_directions[lookup_index] = int(description.get("blend_direction", 0))
		var description_flags := 1 if bool(description.get("flipped", false)) else 0
		if bool(description.get("two_sided", false)):
			description_flags |= 2
		blend_flags[lookup_index] = description_flags
	var tile_values := PackedFloat32Array()
	var blend_values := PackedFloat32Array()
	tile_values.resize(cell_count * 4)
	blend_values.resize(cell_count * 4)
	for index in range(cell_count):
		var base_texture_index := map_data.terrain_texture_index_for_tile_value(int(map_data.terrain_tile_indices[index]))
		var primary_index := int(map_data.terrain_blend_cells[index])
		var three_way_index := int(map_data.terrain_three_way_blend_cells[index])
		if base_texture_index < 0 or primary_index < 0 or primary_index > blend_count or three_way_index < 0 or three_way_index > blend_count:
			return _fail_bool("terrain cell could not resolve a material reference")
		var primary_texture := base_texture_index if primary_index == 0 else int(blend_textures[primary_index])
		var three_way_texture := base_texture_index if three_way_index == 0 else int(blend_textures[three_way_index])
		if primary_texture < 0 or three_way_texture < 0:
			return _fail_bool("terrain cell could not resolve a material reference")
		var offset := index * 4
		tile_values[offset] = float(base_texture_index)
		tile_values[offset + 1] = float(primary_texture)
		tile_values[offset + 2] = float(three_way_texture)
		tile_values[offset + 3] = float(map_data.terrain_cliff_cells[index])
		blend_values[offset] = float(blend_directions[primary_index])
		blend_values[offset + 1] = float(blend_flags[primary_index])
		blend_values[offset + 2] = float(blend_directions[three_way_index])
		blend_values[offset + 3] = float(blend_flags[three_way_index])
	tile_data_texture = _float_texture(map_data.width, map_data.height, tile_values)
	blend_data_texture = _float_texture(map_data.width, map_data.height, blend_values)
	if tile_data_texture == null or blend_data_texture == null:
		return _fail_bool("terrain cell data textures could not be created")

	var texture_values := PackedFloat32Array()
	texture_values.resize(map_data.terrain_texture_count * 4)
	for index in range(map_data.terrain_texture_count):
		var row: Dictionary = map_data.terrain_material_catalog[index]
		texture_values[index * 4] = float(row.get("uv_divisor", 0))
	texture_info_texture = _float_texture(map_data.terrain_texture_count, 1, texture_values)
	if texture_info_texture == null:
		return _fail_bool("terrain texture-scale lookup could not be created")

	var cliff_count := map_data.terrain_cliff_mappings.size()
	var cliff_values := PackedFloat32Array()
	cliff_values.resize(maxi(1, cliff_count) * 2 * 4)
	for index in range(cliff_count):
		var row: Dictionary = map_data.terrain_cliff_mappings[index]
		var bottom_left := Vector2(row.get("bottom_left", Vector2.ZERO))
		var bottom_right := Vector2(row.get("bottom_right", Vector2.ZERO))
		var top_left := Vector2(row.get("top_left", Vector2.ZERO))
		var top_right := Vector2(row.get("top_right", Vector2.ZERO))
		var bottom_offset := index * 4
		var top_offset := (maxi(1, cliff_count) + index) * 4
		cliff_values[bottom_offset] = bottom_left.x
		cliff_values[bottom_offset + 1] = bottom_left.y
		cliff_values[bottom_offset + 2] = bottom_right.x
		cliff_values[bottom_offset + 3] = bottom_right.y
		cliff_values[top_offset] = top_left.x
		cliff_values[top_offset + 1] = top_left.y
		cliff_values[top_offset + 2] = top_right.x
		cliff_values[top_offset + 3] = top_right.y
	cliff_info_texture = _float_texture(maxi(1, cliff_count), 2, cliff_values)
	if cliff_info_texture == null:
		return _fail_bool("terrain cliff lookup could not be created")
	tile_data_cell_count = cell_count
	return true


func _blend_gpu_record(map_data: RetailMapData, blend_index: int, base_texture_index: int) -> PackedInt32Array:
	if blend_index == 0:
		return PackedInt32Array([base_texture_index, 0, 0])
	if blend_index < 1 or blend_index > map_data.terrain_blend_descriptions.size():
		return PackedInt32Array()
	var row: Dictionary = map_data.terrain_blend_descriptions[blend_index - 1]
	var flags := 1 if bool(row.get("flipped", false)) else 0
	if bool(row.get("two_sided", false)):
		flags |= 2
	return PackedInt32Array([
		int(row.get("secondary_texture_index", -1)),
		int(row.get("blend_direction", 0)),
		flags,
	])


func _float_texture(width: int, height: int, values: PackedFloat32Array) -> ImageTexture:
	if width <= 0 or height <= 0 or values.size() != width * height * 4:
		return null
	var image := Image.create_from_data(width, height, false, Image.FORMAT_RGBAF, values.to_byte_array())
	if image == null or image.is_empty():
		return null
	return ImageTexture.create_from_image(image)


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


func _shader_source() -> String:
	return """
shader_type spatial;
render_mode cull_disabled, unshaded;

uniform sampler2DArray terrain_textures : source_color, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2D terrain_tile_data : filter_nearest, repeat_disable;
uniform sampler2D terrain_blend_data : filter_nearest, repeat_disable;
uniform sampler2D terrain_texture_info : filter_nearest, repeat_disable;
uniform sampler2D terrain_cliff_info : filter_nearest, repeat_disable;
uniform vec2 terrain_grid_size;
uniform int terrain_texture_count;
uniform int terrain_cliff_mapping_count;
uniform bool passability_debug_enabled = false;
uniform bool sage_lighting_enabled = true;
uniform vec3 sage_ambient_color = vec3(0.0);
uniform vec3 sage_light_0_color = vec3(0.0);
uniform vec3 sage_light_0_direction = vec3(0.0, -1.0, 0.0);
uniform vec3 sage_light_1_color = vec3(0.0);
uniform vec3 sage_light_1_direction = vec3(0.0, -1.0, 0.0);
uniform vec3 sage_light_2_color = vec3(0.0);
uniform vec3 sage_light_2_direction = vec3(0.0, -1.0, 0.0);

// --- Retail shroud overlay ---------------------------------------------------
// One L8 texel per shroud cell carrying retail's own visibility byte
// (gamedata.ini ClearAlpha 255 / FogAlpha 127 / ShroudAlpha 0, where 0 is
// OPAQUE). It modulates the finished terrain texel, which is why unexplored
// ground reads black and remembered ground reads as half-lit terrain rather
// than as a white wash: retail's ShroudColor is white because the layer is a
// MODULATE, not an overlay tint.
//
// filter_linear on purpose. The grid is 40 source units per cell and a nearest
// sample would be visibly blocky; retail interpolates between cell centres and
// so does this. The UV maps a cell's centre to its texel's centre exactly, so
// no half-texel correction is needed: uv = (world - origin) / (cells * size)
// evaluates to (i + 0.5) / N at the centre of cell i.
uniform sampler2D shroud_texture : filter_linear, repeat_disable;
// origin.xy, then the grid's full extent in world units.
uniform vec4 shroud_bounds = vec4(0.0, 0.0, 1.0, 1.0);
uniform bool shroud_enabled = false;
// How dark fully shrouded ground goes. Retail's ShroudAlpha of 0 is total, and
// this stays 0.0; it exists so a debug view can lift the shroud without
// rebuilding the shader.
uniform float shroud_floor = 0.0;

varying vec3 sage_world_normal;
varying vec3 sage_world_position;

void vertex() {
	sage_world_normal = normalize(MODEL_NORMAL_MATRIX * NORMAL);
	sage_world_position = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
}

float shroud_visibility(vec3 world_position) {
	if (!shroud_enabled) {
		return 1.0;
	}
	vec2 uv = (world_position.xz - shroud_bounds.xy) / max(shroud_bounds.zw, vec2(0.0001));
	// Ground outside the grid is ground the player has never been able to see.
	// Clamping instead would smear the border cells outward across the skirt.
	if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
		return shroud_floor;
	}
	return max(texture(shroud_texture, uv).r, shroud_floor);
}

float diagonal_blend(vec2 fractional_uv, bool two_sided) {
	float sum = fractional_uv.x + fractional_uv.y;
	return two_sided
		? 1.0 - clamp(sum - 1.0, 0.0, 1.0)
		: clamp(1.0 - sum, 0.0, 1.0);
}

float blend_factor(int direction, int flags, vec2 fractional_uv) {
	bool flipped = (flags & 1) != 0;
	bool two_sided = (flags & 2) != 0;
	if (flipped) {
		if (direction == 1) {
			fractional_uv.x = 1.0 - fractional_uv.x;
		} else {
			fractional_uv.y = 1.0 - fractional_uv.y;
		}
	}
	if (direction == 1) {
		return fractional_uv.x;
	}
	if (direction == 2) {
		return fractional_uv.y;
	}
	if (direction == 4) {
		fractional_uv = vec2(1.0) - fractional_uv;
		return diagonal_blend(fractional_uv, two_sided);
	}
	if (direction == 8) {
		fractional_uv.y = 1.0 - fractional_uv.y;
		return diagonal_blend(fractional_uv, two_sided);
	}
	return 0.0;
}

vec3 sample_terrain_layer(int texture_index, vec2 sample_uv) {
	texture_index = clamp(texture_index, 0, terrain_texture_count - 1);
	float divisor = max(texelFetch(terrain_texture_info, ivec2(texture_index, 0), 0).r, 1.0);
	vec2 scaled_uv = sample_uv / divisor;
	vec2 dx = dFdx(sample_uv) / divisor;
	vec2 dy = dFdy(sample_uv) / divisor;
	return textureGrad(terrain_textures, vec3(scaled_uv, float(texture_index)), dx, dy).rgb;
}

void fragment() {
	ivec2 maximum_cell = ivec2(terrain_grid_size) - ivec2(1);
	ivec2 cell = clamp(ivec2(floor(UV)), ivec2(0), maximum_cell);
	vec4 tile_datum = texelFetch(terrain_tile_data, cell, 0);
	vec4 blend_datum = texelFetch(terrain_blend_data, cell, 0);
	int base_texture = int(round(tile_datum.r));
	int primary_texture = int(round(tile_datum.g));
	int three_way_texture = int(round(tile_datum.b));
	int cliff_index = int(round(tile_datum.a));
	int primary_direction = int(round(blend_datum.r));
	int primary_flags = int(round(blend_datum.g));
	int three_way_direction = int(round(blend_datum.b));
	int three_way_flags = int(round(blend_datum.a));
	vec2 fractional_uv = fract(UV);
	vec2 sample_uv = UV;
	if (cliff_index > 0 && cliff_index <= terrain_cliff_mapping_count) {
		vec4 bottom = texelFetch(terrain_cliff_info, ivec2(cliff_index - 1, 0), 0);
		vec4 top = texelFetch(terrain_cliff_info, ivec2(cliff_index - 1, 1), 0);
		vec2 bottom_uv = mix(bottom.xy, bottom.zw, fractional_uv.x);
		vec2 top_uv = mix(top.xy, top.zw, fractional_uv.x);
		sample_uv = mix(bottom_uv, top_uv, fractional_uv.y) * 64.0;
	}
	vec3 base_color = sample_terrain_layer(base_texture, sample_uv);
	vec3 primary_color = sample_terrain_layer(primary_texture, sample_uv);
	vec3 three_way_color = sample_terrain_layer(three_way_texture, sample_uv);
	float primary_factor = blend_factor(primary_direction, primary_flags, fractional_uv);
	float three_way_factor = blend_factor(three_way_direction, three_way_flags, fractional_uv);
	vec3 terrain_color = mix(mix(base_color, primary_color, primary_factor), three_way_color, three_way_factor);
	if (passability_debug_enabled && COLOR.r > 0.5) {
		terrain_color = mix(terrain_color, vec3(0.95, 0.08, 0.16), 0.72);
	}
	vec3 lighting = vec3(1.0);
	if (sage_lighting_enabled) {
		vec3 world_normal = normalize(sage_world_normal);
		lighting = sage_ambient_color;
		lighting += max(dot(world_normal, -sage_light_0_direction), 0.0) * sage_light_0_color;
		lighting += max(dot(world_normal, -sage_light_1_direction), 0.0) * sage_light_1_color;
		lighting += max(dot(world_normal, -sage_light_2_direction), 0.0) * sage_light_2_color;
		lighting = clamp(lighting, vec3(0.0), vec3(1.0));
	}
	// This is OpenSAGE's terrain DoLighting contract: summed per-light ambient
	// and Lambert diffuse, saturated once, then multiplied by the terrain texel.
	ALBEDO = terrain_color * lighting * shroud_visibility(sage_world_position);
	ROUGHNESS = 0.92;
	SPECULAR = 0.20;
}
"""


func _fail(message: String) -> ShaderMaterial:
	if error == "":
		error = message
	return null


func _fail_bool(message: String) -> bool:
	if error == "":
		error = message
	return false


func _reset() -> void:
	error = ""
	texture_array = null
	tile_data_texture = null
	blend_data_texture = null
	texture_info_texture = null
	cliff_info_texture = null
	material = null
	texture_array_size = 0
	texture_array_dimension = 0
	tile_data_cell_count = 0
