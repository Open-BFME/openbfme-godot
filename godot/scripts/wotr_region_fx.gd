extends Node3D
## Lane WOTR-2: the Living World region effects (LivingWorldRegionEffects.ini, WotRRegionEffects), drawn for the strategic map and the setup's preview.
## Retail draws each effect's Geometry model per region sub-object in a colour times the effect's intensity curve (RW 0xC4C7C0 .. 0xC4C874: Borders,
## MouseoverFlareup, MouseoutFlareup, ConqueredEvenglow, ConqueredFlareup, FilledOwnership, HomeRegionHighlight, RegionSelection, Unified). The models'
## own W3D materials decide the look (read from the retail files, mirrored here):
##   LMR_Fill      the region sub-objects: alpha blended (SRC_ALPHA / ONE_MINUS_SRC_ALPHA), lit (ambient 225, diffuse 91, emissive 0), opacity
##                 0.35: the owner's colour as a pastel wash; its IMPASSABLE sub-objects are additive, emissive white, opacity 0.5 (not drawn here)
##   LMR_Border    alpha blended (SRC_ALPHA / ONE_MINUS_SRC_ALPHA), emissive white: the borders in RegionBorderColor, always shown
##   LMR_Highlight opaque (ONE / ZERO), depth written, textured: the home region's ring (the bright outline of a capital / a shell start position)
##   LMR_Edge      additive, textured: the selection outline
## Presentation only (S-1335); the intensity curves are taken at their final value (the time animation is not reproduced). INFERENCE (S-1335, the effect
## draw code was not read): the effect's colour takes the place of the material's emissive colour (a black-emissive fill then glows in the owner's colour,
## the white-emissive border turns RegionBorderColor). The blended kinds are lit and composited as the W3D shaders are (defaultw3d.fxo without a texture:
## colour = saturate((sum(light * max(N.L, 0)) * diffuse + ambient light * ambient + emissive) * 0.5) * 2, lit by the W3D light set the strategic map sets
## from LivingWorldMapInfo, LivingWorldGame.apply_map_lighting) and hand over gamma-space values where the camera composites in gamma space (RENDER-3,
## GodotGammaComposite.h: Forward+), linear values otherwise.

const OVERLAY_SHADER := """
shader_type spatial;
render_mode unshaded, cull_disabled, depth_draw_never, %s;
global uniform vec3 w3d_amb_obj;
global uniform mat3 w3d_lcol_obj;
global uniform mat3 w3d_ldir_obj;
uniform vec3 mat_diffuse = vec3(0.0);
uniform vec3 mat_ambient = vec3(0.0);
uniform vec3 emissive = vec3(1.0);
uniform float opacity = 1.0;
uniform bool textured = false;
uniform sampler2D tex : source_color, filter_linear_mipmap, repeat_enable;
uniform bool gamma_out = true;
varying vec3 v_lit;
void vertex() {
	vec3 n = normalize(MODEL_NORMAL_MATRIX * NORMAL);
	vec3 d = vec3(0.0);
	for (int i = 0; i < 3; i++) {
		d += w3d_lcol_obj[i] * max(dot(n, w3d_ldir_obj[i]), 0.0);
	}
	v_lit = clamp((d * mat_diffuse + w3d_amb_obj * mat_ambient + emissive) * 0.5, 0.0, 1.0);
}
void fragment() {
	vec3 c = clamp(v_lit * 2.0, 0.0, 1.0);
	if (textured) {
		c *= texture(tex, UV).rgb;
	}
	ALBEDO = gamma_out ? c : mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(0.04045, c));
	ALPHA = opacity;
}
"""

const MODELS := {"fill": "LMR_Fill", "border": "LMR_Border", "highlight": "LMR_Highlight", "edge": "LMR_Edge"}

var meshes := {}      # kind -> {region index -> MeshInstance3D}
var textures := {}    # kind -> {region index -> Texture2D} (the model's own texture, for the textured kinds)
var border_color := Color8(30, 6, 6)
var overlay_materials: Array = []   # the ShaderMaterials of the blended kinds (their gamma_out follows the camera)
var gamma_out := true
var _shaders := {}


func build(fs: Object, regions: Array, effects: Dictionary) -> void:
	if effects.has("border_color"):
		border_color = effects.border_color
	var builder: RefCounted = ClassDB.instantiate("W3DModelBuilder")
	for kind in MODELS.keys():
		meshes[kind] = {}
		textures[kind] = {}
		var model: Node3D = builder.build_model(fs, MODELS[kind])
		if model == null:
			push_error("WotR region effects: %s did not build: %s" % [MODELS[kind], JSON.stringify(builder.get_report())])
			continue
		add_child(model)
		var rep: Dictionary = builder.get_report()
		if not rep.get("errors", []).is_empty():
			push_error("WotR region effects: %s: %s" % [MODELS[kind], JSON.stringify(rep.errors)])
		var by_name := {}
		for n in model.find_children("*", "MeshInstance3D", true, false):
			by_name[str(n.name).to_upper()] = n
			n.visible = false
		var prefix := str(model.name).to_upper() + "_"
		for i in range(regions.size()):
			var key: String = prefix + str(regions[i].sub_object).to_upper()
			if not by_name.has(key):
				continue
			var mesh: MeshInstance3D = by_name[key]
			meshes[kind][i] = mesh
			var src: Material = mesh.get_active_material(0)
			if src is BaseMaterial3D and (src as BaseMaterial3D).albedo_texture != null:
				textures[kind][i] = (src as BaseMaterial3D).albedo_texture
	# the borders are always shown (BordersEffect)
	for i in meshes.get("border", {}).keys():
		_show("border", i, border_color)


## the effects of region `r`: `fill` / `highlight` / `edge` in `color` (a Color with a == 0 hides that effect)
func set_region(r: int, fill: Color, highlight: Color, edge: Color) -> void:
	_show("fill", r, fill)
	_show("highlight", r, highlight)
	_show("edge", r, edge)


func clear_regions() -> void:
	for kind in ["fill", "highlight", "edge"]:
		for i in meshes.get(kind, {}).keys():
			meshes[kind][i].visible = false


## the fill meshes carry the region pick bodies (the retail pick-boxes are LMR_Fill's sub-objects, RegionObject)
func fill_meshes() -> Dictionary:
	return meshes.get("fill", {})


## the camera showing the effects composites its transparent pass in gamma space (a compositor: GammaComposite on Forward+): the blended kinds hand over
## gamma values, linear ones otherwise (the OpenGL renderer)
func update_gamma() -> void:
	var cam := get_viewport().get_camera_3d() if is_inside_tree() else null
	var g := cam != null and cam.compositor != null
	if g == gamma_out:
		return
	gamma_out = g
	for m in overlay_materials:
		m.set_shader_parameter("gamma_out", g)


func overlay_material(blend: String, diffuse: Color, ambient: Color, emissive: Color, opacity: float, tex: Texture2D, priority: int) -> ShaderMaterial:
	if not _shaders.has(blend):
		var sh := Shader.new()
		sh.code = OVERLAY_SHADER % blend
		_shaders[blend] = sh
	var m := ShaderMaterial.new()
	m.shader = _shaders[blend]
	m.set_shader_parameter("mat_diffuse", Vector3(diffuse.r, diffuse.g, diffuse.b))
	m.set_shader_parameter("mat_ambient", Vector3(ambient.r, ambient.g, ambient.b))
	m.set_shader_parameter("emissive", Vector3(emissive.r, emissive.g, emissive.b))
	m.set_shader_parameter("opacity", opacity)
	m.set_shader_parameter("textured", tex != null)
	if tex != null:
		m.set_shader_parameter("tex", tex)
	m.set_shader_parameter("gamma_out", gamma_out)
	m.render_priority = priority
	overlay_materials.append(m)
	return m


func _show(kind: String, r: int, c: Color) -> void:
	var mesh: MeshInstance3D = meshes.get(kind, {}).get(r, null)
	if mesh == null:
		return
	if c.a <= 0.0:
		mesh.visible = false
		return
	var old: Material = mesh.material_override
	if old != null:
		overlay_materials.erase(old)
	var black := Color(0, 0, 0)
	match kind:
		"fill":
			# LMR_Fill's region material: ambient 225, diffuse 91, opacity 0.35, the colour as its emissive
			mesh.material_override = overlay_material("blend_mix", Color8(91, 91, 91), Color8(225, 225, 225), c, 0.35, null, 1)
		"border":
			mesh.material_override = overlay_material("blend_mix", black, black, c, 1.0, null, 2)
		"highlight":
			# [presentation] the ribbon in the colour itself: its EXgrad texture's coordinates do not come through the model builder (sampled at its dark
			# end the rim drew dark); retail's rim is the bright colour (the owner's RotWK 2.01 screenshots). Opaque: Godot's opaque pass stays linear
			var m := StandardMaterial3D.new()
			m.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
			m.cull_mode = BaseMaterial3D.CULL_DISABLED
			m.albedo_color = Color(c.r, c.g, c.b)
			m.render_priority = 3
			m.no_depth_test = true # the ribbon hugs the terrain; drawn over LivingMap (it lost the depth test in valleys) [presentation]
			mesh.material_override = m
		"edge":
			var em := overlay_material("blend_add, depth_test_disabled", black, black, c, 1.0, textures.get(kind, {}).get(r, null), 4)
			mesh.material_override = em
	mesh.visible = true
