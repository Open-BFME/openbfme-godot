extends RefCounted
## Exact retail house-color application. The M3 pack ships the retail
## HouseColor mask textures (data/house-color.json + assets/textures/
## house-color/mask-<sha>.png) which mark, per base texture, the pixels that
## take the player color (banner cloth, armor trim, roof accents). This module
## recolors ONLY masked pixels — final = base * mix(1, teamColor, mask) — so
## the authored texture detail survives and nothing is invented. Models whose
## textures carry no mask stay untouched.
##
## No class_name: scripts loaded by headless --script runners do not register
## global classes; consumers preload this file.

# RotWK multiplayer.ini:52-151 RGBColor rows, in authored slot order. This is
# the one default-palette source for setup, lobby, model masks, and radar blips.
const MULTIPLAYER_COLOR_NAMES: Array[String] = [
	"Blue", "Red", "Gold", "Green", "Orange",
	"Sky Blue", "Purple", "Pink", "Gray", "White",
]
const MULTIPLAYER_COLORS: Array[Color] = [
	Color8(70, 91, 156),
	Color8(158, 56, 42),
	Color8(175, 189, 76),
	Color8(62, 152, 100),
	Color8(206, 135, 69),
	Color8(122, 168, 204),
	Color8(148, 116, 183),
	Color8(204, 159, 188),
	Color8(100, 100, 100),
	Color8(255, 255, 255),
]
const TEAM_COLORS := {
	0: MULTIPLAYER_COLORS[0],
	1: MULTIPLAYER_COLORS[1],
	2: MULTIPLAYER_COLORS[2],
	3: MULTIPLAYER_COLORS[3],
	4: MULTIPLAYER_COLORS[4],
	5: MULTIPLAYER_COLORS[5],
	6: MULTIPLAYER_COLORS[6],
	7: MULTIPLAYER_COLORS[7],
	8: MULTIPLAYER_COLORS[8],
	9: MULTIPLAYER_COLORS[9],
}
## Optional per-team color override (menu house-color selection seam). Empty
## dictionary keeps the authored TEAM_COLORS untouched; a Color entry replaces
## that team's row without touching the mask machinery.
static var team_color_overrides: Dictionary = {}

static var _configured_pack := ""
static var _stem_to_mask_path: Dictionary = {}
static var _mask_textures: Dictionary = {}
static var _materials: Dictionary = {}
static var _shader: Shader = null


## The single authored/match-selected color seam shared by model masks and
## presentation-only consumers such as the radar. Unknown teams keep the
## caller's explicit fallback rather than being guessed into one of two sides.
static func color_for_team(team: int, fallback: Color = Color.WHITE) -> Color:
	if team_color_overrides.has(team):
		return Color(team_color_overrides[team])
	if TEAM_COLORS.has(team):
		return Color(TEAM_COLORS[team])
	return fallback


## Returns the number of surfaces recolored. Safe on any converted scene: only
## BaseMaterial3D surfaces whose albedo texture name matches a mask binding
## change. The mesh is duplicated before material swaps so the shared GLB cache
## stays pristine (same discipline as AssetFactory's fallback tint).
static func apply(node: Node3D, team: int, pack_root: String) -> int:
	if node == null or pack_root == "" or not TEAM_COLORS.has(team):
		return 0
	if not _configure(pack_root):
		return 0
	var team_color := color_for_team(team)
	return _apply_recursive(node, team_color)


## Per-object seam used by Create-a-Hero. It deliberately bypasses the team
## palette while retaining the identical authored mask lookup and shader.
static func apply_with_color(node: Node3D, color: Color, pack_root: String) -> int:
	if node == null or pack_root == "":
		return 0
	if not _configure(pack_root):
		return 0
	return _apply_recursive(node, color)


static func _apply_recursive(node: Node, team_color: Color) -> int:
	var recolored := 0
	if node is MeshInstance3D:
		recolored += _apply_mesh(node as MeshInstance3D, team_color)
	for child in node.get_children():
		recolored += _apply_recursive(child, team_color)
	return recolored


static func _apply_mesh(instance: MeshInstance3D, team_color: Color) -> int:
	var mesh: Mesh = instance.mesh
	if mesh == null:
		return 0
	var targets: Array[int] = []
	for surface in mesh.get_surface_count():
		var material := mesh.surface_get_material(surface)
		if material is BaseMaterial3D and _mask_stem_for(material as BaseMaterial3D) != "":
			targets.append(surface)
	if targets.is_empty():
		return 0
	# Duplicate so the immutable cached scene keeps its untinted materials.
	var private_mesh: Mesh = mesh.duplicate() as Mesh
	instance.mesh = private_mesh
	var recolored := 0
	for surface in targets:
		var source := private_mesh.surface_get_material(surface) as BaseMaterial3D
		var stem := _mask_stem_for(source)
		var replacement := _house_color_material(source, stem, team_color)
		if replacement != null:
			private_mesh.surface_set_material(surface, replacement)
			recolored += 1
	return recolored


static func _mask_stem_for(material: BaseMaterial3D) -> String:
	var texture := material.albedo_texture
	if texture == null:
		return ""
	var stem := String(texture.resource_name).get_basename().to_lower()
	return stem if _stem_to_mask_path.has(stem) else ""


static func _house_color_material(source: BaseMaterial3D, stem: String, team_color: Color) -> ShaderMaterial:
	var key := "%s|%s|%d|%f" % [stem, team_color.to_html(false), source.cull_mode, source.alpha_scissor_threshold]
	if _materials.has(key):
		return _materials[key]
	var mask := _mask_texture(stem)
	if mask == null:
		return null
	if _shader == null:
		_shader = Shader.new()
		_shader.code = """
shader_type spatial;
render_mode cull_back;
uniform sampler2D base_texture : source_color, filter_linear_mipmap;
uniform sampler2D mask_texture : filter_linear_mipmap;
uniform vec4 team_color : source_color = vec4(1.0);
uniform float alpha_scissor : hint_range(0.0, 1.0) = 0.0;
uniform float roughness_value : hint_range(0.0, 1.0) = 1.0;

void fragment() {
	vec4 base = texture(base_texture, UV);
	float mask_value = texture(mask_texture, UV).r;
	ALBEDO = base.rgb * mix(vec3(1.0), team_color.rgb, mask_value);
	if (alpha_scissor > 0.0 && base.a < alpha_scissor) {
		discard;
	}
	ROUGHNESS = roughness_value;
	METALLIC = 0.0;
}
"""
	var material := ShaderMaterial.new()
	material.shader = _shader
	material.set_shader_parameter("base_texture", source.albedo_texture)
	material.set_shader_parameter("mask_texture", mask)
	material.set_shader_parameter("team_color", team_color)
	var scissor := source.alpha_scissor_threshold if source.transparency == BaseMaterial3D.TRANSPARENCY_ALPHA_SCISSOR else 0.0
	material.set_shader_parameter("alpha_scissor", scissor)
	material.set_shader_parameter("roughness_value", source.roughness)
	_materials[key] = material
	return material


static func _mask_texture(stem: String) -> Texture2D:
	if _mask_textures.has(stem):
		return _mask_textures[stem]
	var path := String(_stem_to_mask_path.get(stem, ""))
	var texture := _load_png(path)
	_mask_textures[stem] = texture
	return texture


static func _load_png(path: String) -> Texture2D:
	if path == "" or not FileAccess.file_exists(path):
		return null
	var bytes := FileAccess.get_file_as_bytes(path)
	if bytes.is_empty():
		return null
	var image := Image.new()
	if image.load_png_from_buffer(bytes) != OK:
		return null
	image.generate_mipmaps()
	return ImageTexture.create_from_image(image)


## Builds base-texture-stem -> pack mask-asset-path from the pack's
## house-color manifest plus the provenance manifest (which records the
## retail virtual path each converted mask came from). Fail-closed: any
## malformed row is skipped, never guessed.
static func _configure(pack_root: String) -> bool:
	if _configured_pack == pack_root:
		return not _stem_to_mask_path.is_empty()
	_configured_pack = pack_root
	_stem_to_mask_path = {}
	_mask_textures = {}
	_materials = {}
	var manifest: Variant = _read_json(pack_root.path_join("data/house-color.json"))
	var provenance: Variant = _read_json(pack_root.path_join("provenance/manifest.json"))
	if typeof(manifest) != TYPE_DICTIONARY or typeof(provenance) != TYPE_DICTIONARY:
		return false
	var retail_mask_to_asset: Dictionary = {}
	for entry_value in (provenance as Dictionary).get("entries", []):
		if typeof(entry_value) != TYPE_DICTIONARY:
			continue
		var entry := entry_value as Dictionary
		var virtual_path := String((entry.get("source", {}) as Dictionary).get("virtual_path", "")).to_lower()
		if not virtual_path.begins_with("art/compiledtextures/hc/"):
			continue
		for output_value in entry.get("outputs", []):
			if typeof(output_value) != TYPE_DICTIONARY:
				continue
			var output_path := String((output_value as Dictionary).get("path", ""))
			if output_path.begins_with("assets/textures/house-color/"):
				retail_mask_to_asset[virtual_path] = pack_root.path_join(output_path)
	for model_value in (manifest as Dictionary).get("models", []):
		if typeof(model_value) != TYPE_DICTIONARY:
			continue
		for binding_value in (model_value as Dictionary).get("textureBindings", []):
			if typeof(binding_value) != TYPE_DICTIONARY:
				continue
			var binding := binding_value as Dictionary
			var stem := String(binding.get("baseTexture", "")).get_file().get_basename().to_lower()
			if stem == "" or _stem_to_mask_path.has(stem):
				continue
			# Prefer the png mask when both jpg and png variants were authored.
			var chosen := ""
			for mask_value in binding.get("maskTextures", []):
				var retail_path := String(mask_value).to_lower()
				if not retail_mask_to_asset.has(retail_path):
					continue
				if chosen == "" or retail_path.ends_with(".png"):
					chosen = String(retail_mask_to_asset[retail_path])
			if chosen != "":
				_stem_to_mask_path[stem] = chosen
	return not _stem_to_mask_path.is_empty()


static func _read_json(path: String) -> Variant:
	if not FileAccess.file_exists(path):
		return null
	var text := FileAccess.get_file_as_string(path)
	return JSON.parse_string(text)
