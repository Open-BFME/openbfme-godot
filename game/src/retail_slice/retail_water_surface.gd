class_name RetailWaterSurface
extends RefCounted
## Builds the battlefield water material from WaterSet + WaterTransparency
## plus the cooked map water row. The old teal ripple is not a retail number.

const AtmosphereScript = preload("res://src/retail_slice/retail_sage_atmosphere.gd")

var error := ""
var last_contract: Dictionary = {}


func build_material(
	time_of_day: String,
	pack_root: String,
	map_row: Dictionary = {}
) -> ShaderMaterial:
	error = ""
	last_contract = {}
	var water_set: Dictionary = AtmosphereScript.water_set(time_of_day)
	if water_set.is_empty():
		return _fail("unknown WaterSet time-of-day '%s'" % time_of_day)
	var transparency: Dictionary = AtmosphereScript.water_transparency()
	var standing_tint := AtmosphereScript.color_rgb8(transparency.get("standing_water_color_rgb8", []))
	var diffuse := AtmosphereScript.color_rgba8(water_set.get("diffuse_rgba8", []))
	var transparent_diffuse := AtmosphereScript.color_rgba8(water_set.get("transparent_diffuse_rgba8", []))
	var vertex_color := AtmosphereScript.color_rgb8(water_set.get("vertex_color_rgb8", []))
	if standing_tint.a <= 0.0 or diffuse.a <= 0.0 or vertex_color.a <= 0.0:
		return _fail("WaterSet or WaterTransparency color is incomplete")

	var map_color := _map_color(map_row)
	var albedo := Color(
		standing_tint.r * diffuse.r * map_color.r * vertex_color.r,
		standing_tint.g * diffuse.g * map_color.g * vertex_color.g,
		standing_tint.b * diffuse.b * map_color.b * vertex_color.b,
		1.0
	)
	var river_alpha := 1.0
	if map_row.has("alpha"):
		river_alpha = clampf(float(map_row.get("alpha", 1.0)), 0.0, 1.0)
	var alpha := clampf(
		transparent_diffuse.a
		* float(transparency.get("transparent_water_min_opacity", 1.0))
		* float(transparency.get("river_transparency_multiplier", 1.0))
		* river_alpha,
		0.0,
		1.0
	)
	var additive := bool(transparency.get("additive_blending", false)) or bool(map_row.get("additive", false))
	var u_scroll := float(water_set.get("u_scroll_per_ms", 0.0))
	var v_scroll := float(water_set.get("v_scroll_per_ms", 0.0))
	var map_scroll := float(map_row.get("uv_scroll_speed", map_row.get("uvScrollSpeed", 0.0)))
	var repeat_count := int(water_set.get("water_repeat_count", 0))
	var depth := float(transparency.get("transparent_water_depth", 0.0))
	if repeat_count <= 0 or not is_finite(depth) or depth <= 0.0:
		return _fail("WaterSet repeat or WaterTransparency depth is invalid")

	var requested_texture := String(transparency.get("standing_water_texture", ""))
	var map_river_texture := _map_river_texture(map_row)
	if map_river_texture != "":
		requested_texture = map_river_texture
	var resolved := resolve_pack_texture(pack_root, requested_texture)
	var bump_resolved := resolve_pack_texture(pack_root, String(map_row.get("bump_map", map_row.get("bumpMap", ""))))

	var shader := Shader.new()
	shader.code = _shader_source(additive)
	var material := ShaderMaterial.new()
	material.shader = shader
	material.set_shader_parameter("albedo_tint", Vector3(albedo.r, albedo.g, albedo.b))
	material.set_shader_parameter("water_alpha", alpha)
	material.set_shader_parameter("u_scroll_per_ms", u_scroll)
	material.set_shader_parameter("v_scroll_per_ms", v_scroll)
	material.set_shader_parameter("map_uv_scroll", map_scroll)
	material.set_shader_parameter("water_repeat", float(repeat_count))
	material.set_shader_parameter("transparent_depth", depth)
	material.set_shader_parameter("has_surface_texture", resolved.get("texture") != null)
	material.set_shader_parameter("surface_texture", resolved.get("texture"))
	material.set_shader_parameter("has_bump_texture", bump_resolved.get("texture") != null)
	material.set_shader_parameter("bump_texture", bump_resolved.get("texture"))
	last_contract = {
		"schema": "openbfme.sage-water-surface",
		"schema_version": 0,
		"time_of_day": AtmosphereScript.normalize_time_of_day(time_of_day),
		"water_set_source": AtmosphereScript.WATER_INI_PATH,
		"transparency_source": AtmosphereScript.WATER_INI_PATH,
		"diffuse_rgba8": water_set.get("diffuse_rgba8", []),
		"transparent_diffuse_rgba8": water_set.get("transparent_diffuse_rgba8", []),
		"standing_water_color_rgb8": transparency.get("standing_water_color_rgb8", []),
		"vertex_color_rgb8": water_set.get("vertex_color_rgb8", []),
		"albedo": albedo,
		"alpha": alpha,
		"u_scroll_per_ms": u_scroll,
		"v_scroll_per_ms": v_scroll,
		"map_uv_scroll": map_scroll,
		"water_repeat_count": repeat_count,
		"transparent_water_depth": depth,
		"additive": additive,
		"requested_surface_texture": requested_texture,
		"surface_texture_status": String(resolved.get("status", "")),
		"surface_texture_path": String(resolved.get("path", "")),
		"requested_bump_texture": String(map_row.get("bump_map", map_row.get("bumpMap", ""))),
		"bump_texture_status": String(bump_resolved.get("status", "")),
		"sky_texture": String(water_set.get("sky_texture", "")),
		"sky_env_status": "unresolved-SkyEnv.tga-not-a-world-sky",
		"radar_water_color_rgb8": transparency.get("radar_water_color_rgb8", []),
	}
	material.set_meta("source", "water.ini WaterSet+WaterTransparency and cooked map water row")
	material.set_meta("water_contract", last_contract.duplicate(true))
	return material


func resolve_pack_texture(pack_root: String, requested_name: String) -> Dictionary:
	var name := requested_name.strip_edges()
	if name == "":
		return {"status": "absent-request", "texture": null, "path": ""}
	if pack_root == "":
		return {"status": "unresolved-in-pack", "texture": null, "path": ""}
	if name.begins_with("SkyEnv"):
		return {"status": "unresolved-skyenv-not-world-albedo", "texture": null, "path": ""}
	var stem := name.get_basename().to_lower()
	if stem == "":
		return {"status": "unresolved-empty-stem", "texture": null, "path": ""}
	var candidates: Array[String] = [
		pack_root.path_join("assets/textures/environment/%s.png" % stem),
		pack_root.path_join("assets/textures/water/%s.png" % stem),
		pack_root.path_join("assets/textures/%s.png" % stem),
		pack_root.path_join("assets/textures/environment/fords/%s.png" % stem),
	]
	for path in candidates:
		if not FileAccess.file_exists(path):
			continue
		if not _path_is_within(pack_root, path):
			return {"status": "escaped-pack-root", "texture": null, "path": path}
		var image := Image.new()
		var err := image.load(path)
		if err != OK or image.is_empty():
			return {"status": "unreadable-pack-texture", "texture": null, "path": path}
		var texture := ImageTexture.create_from_image(image)
		return {"status": "bound-pack-png", "texture": texture, "path": path}
	return {"status": "unresolved-in-pack", "texture": null, "path": ""}


func _map_color(map_row: Dictionary) -> Color:
	var raw: Variant = map_row.get("color_rgb", map_row.get("colorRgb", null))
	if typeof(raw) != TYPE_ARRAY or (raw as Array).size() < 3:
		return Color.WHITE
	return AtmosphereScript.color_rgb8(raw as Array)


func _map_river_texture(map_row: Dictionary) -> String:
	var textures: Variant = map_row.get("textures", {})
	if typeof(textures) != TYPE_DICTIONARY:
		return ""
	var river := String((textures as Dictionary).get("river", "")).strip_edges()
	if river == "" or river.to_lower().begins_with("twwaterempty"):
		return ""
	return river


func _shader_source(additive: bool) -> String:
	var blend := "blend_add" if additive else "blend_mix"
	return """
shader_type spatial;
render_mode %s, depth_prepass_alpha, cull_disabled;
uniform vec3 albedo_tint = vec3(0.725, 0.725, 0.725);
uniform float water_alpha = 0.5;
uniform float u_scroll_per_ms = 0.002;
uniform float v_scroll_per_ms = 0.002;
uniform float map_uv_scroll = 0.0;
uniform float water_repeat = 32.0;
uniform float transparent_depth = 3.0;
uniform bool has_surface_texture = false;
uniform sampler2D surface_texture : source_color, filter_linear_mipmap, repeat_enable;
uniform bool has_bump_texture = false;
uniform sampler2D bump_texture : hint_normal, filter_linear_mipmap, repeat_enable;
varying vec3 world_position;
void vertex() {
	world_position = VERTEX;
}
void fragment() {
	float tile = max(water_repeat, 1.0);
	vec2 uv = world_position.xz / tile;
	float seconds = TIME;
	uv += vec2(u_scroll_per_ms, v_scroll_per_ms) * seconds * 1000.0;
	uv += vec2(map_uv_scroll, map_uv_scroll) * seconds;
	vec3 color = albedo_tint;
	float alpha = clamp(water_alpha, 0.0, 1.0);
	if (has_surface_texture) {
		vec4 sampled = texture(surface_texture, uv);
		color *= sampled.rgb;
		alpha *= sampled.a;
	} else {
		float authored_wave = sin(uv.x * 6.283185 + uv.y * 4.188790);
		color += albedo_tint * authored_wave * 0.04;
	}
	if (has_bump_texture) {
		vec3 bump = texture(bump_texture, uv).xyz * 2.0 - 1.0;
		NORMAL = normalize(NORMAL + bump * 0.25);
	}
	float depth_fade = clamp(transparent_depth / max(transparent_depth, 0.001), 0.0, 1.0);
	ALBEDO = color;
	ALPHA = alpha * depth_fade;
	METALLIC = 0.08;
	ROUGHNESS = 0.22;
	SPECULAR = 0.65;
}
""" % blend


func _path_is_within(root: String, path: String) -> bool:
	var resolved_root := root.replace("\\", "/").rstrip("/")
	var resolved_path := path.replace("\\", "/")
	return resolved_path == resolved_root or resolved_path.begins_with(resolved_root + "/")


func _fail(reason: String) -> ShaderMaterial:
	error = reason
	return null
