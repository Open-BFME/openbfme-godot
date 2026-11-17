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
## Presentation only (S-1335): authored geometry and intensity curves drive each effect. Linear curve interpolation is an inference; retail spline easing and client-frame quantization remain unverified. INFERENCE (S-1335, the effect
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
uniform float intensity = 1.0;
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
	ALPHA = opacity * clamp(intensity, 0.0, 1.0);
}
"""

const EFFECTS := {"fill": "FilledOwnershipEffect", "border": "BordersEffect", "highlight": "HomeRegionHighlight", "edge": "RegionSelectionEffect",
	"hover": "MouseoverEffectFlareup", "mouseout": "MouseoutEffectFlareup", "unified": "UnifiedEffect"}

var meshes := {}      # kind -> {region index (or territory sub-object name) -> MeshInstance3D}
var textures := {}
var definitions := {}
var active_effects := {}
var material_cache := {}
var hover_region := -1
var border_color := Color8(30, 6, 6)
var neutral_color := Color.WHITE
var overlay_materials: Array = []
var gamma_out := true
var _shaders := {}

static func definition_for(data: Dictionary, campaign_name: String) -> Dictionary:
	for campaign in data.get("region_campaigns", []):
		if campaign.name != campaign_name:
			continue
		for effect in data.get("region_effects", []):
			if effect.name == campaign.effects:
				return effect
	push_error("[S-1335] WotR: no region effect definition for " + campaign_name)
	return {}

func build(fs: Object, regions: Array, effects: Dictionary, in_shell := false) -> void:
	border_color = effects.get("border_color", border_color)
	neutral_color = effects.get("neutral_color", neutral_color)
	var builder: RefCounted = ClassDB.instantiate("W3DModelBuilder")
	for role in EFFECTS:
		for definition in effects.get("definitions", []):
			if definition.name != EFFECTS[role] or (in_shell and not definition.load_in_shell):
				continue
			if definition.get("has_transform_curve", false):
				push_error("[S-1335] WotR: effect transform curve is not reproduced: " + str(definition.name))
				continue
			for g in range(definition.geometry.size()):
				var kind: String = role + ":" + str(g)
				definitions[kind] = definition
				meshes[kind] = {}
				textures[kind] = {}
				var model_name: String = definition.geometry[g]
				var model: Node3D = builder.build_model(fs, model_name)
				if model == null:
					push_error("WotR region effect %s: %s" % [model_name, JSON.stringify(builder.get_report())])
					continue
				add_child(model)
				var rep: Dictionary = builder.get_report()
				if not rep.get("errors", []).is_empty():
					push_error("WotR region effect %s: %s" % [model_name, JSON.stringify(rep.errors)])
				var by_name := {}
				var prefix := model_name.to_upper() + "_"
				for mesh in model.find_children("*", "MeshInstance3D", true, false):
					var sub: String = str(mesh.name).to_upper().trim_prefix(prefix)
					by_name[sub] = mesh
					mesh.visible = false
				if role == "unified":
					# LMR_RegFill / LMR_RegEdge name territories, rather than individual regions.
					meshes[kind] = by_name
				else:
					for i in range(regions.size()):
						var sub: String = str(regions[i].sub_object).to_upper()
						if by_name.has(sub):
							meshes[kind][i] = by_name[sub]
				for r in meshes[kind]:
					var src: Material = meshes[kind][r].get_active_material(0)
					if src is BaseMaterial3D and src.albedo_texture != null:
						textures[kind][r] = src.albedo_texture
	if meshes.get("fill:0", {}).is_empty():
		push_error("[S-1335] WotR: FilledOwnershipEffect has no region geometry")
	for kind in definitions:
		if kind.begins_with("border:"):
			for r in meshes[kind]:
				_show(kind, r, border_color)

func set_region(r: int, fill: Color, highlight: Color, edge: Color) -> void:
	_set_role("fill", r, fill)
	_set_role("highlight", r, highlight)
	_set_role("edge", r, edge)

func _set_role(role: String, r: Variant, color: Color) -> void:
	for kind in definitions:
		if kind.begins_with(role + ":"):
			_show(kind, r, color)

func set_selection(r: int, color: Color) -> void:
	for kind in definitions:
		if kind.begins_with("edge:"):
			for region in meshes[kind]:
				_show(kind, region, color if region == r else Color.TRANSPARENT)

func set_hover(r: int, color: Color) -> void:
	if r == hover_region:
		if r >= 0:
			_set_role("hover", r, color)
		return
	if hover_region >= 0:
		_set_role("hover", hover_region, Color.TRANSPARENT)
		_set_role("mouseout", hover_region, neutral_color)
	hover_region = r
	if r >= 0:
		_set_role("mouseout", r, Color.TRANSPARENT)
		_set_role("hover", r, color)

func show_unified(territory: String, color: Color) -> void:
	_set_role("unified", territory.to_upper(), color)

func clear_regions() -> void:
	for role in ["fill", "highlight", "edge"]:
		for kind in definitions:
			if kind.begins_with(role + ":"):
				for r in meshes[kind]:
					_show(kind, r, Color.TRANSPARENT)

## Linear presentation sampling; spline tangents and retail's client-frame rounding stay S-1335.
static func curve_value(points: Array, seconds: float) -> float:
	if points.is_empty():
		return 1.0
	if seconds <= float(points[0].time):
		return float(points[0].position.x)
	for i in range(1, points.size()):
		var previous: Dictionary = points[i - 1]
		var next: Dictionary = points[i]
		if seconds <= float(next.time):
			var duration := float(next.time) - float(previous.time)
			if duration <= 0.0:
				return float(next.position.x)
			return lerpf(float(previous.position.x), float(next.position.x), (seconds - float(previous.time)) / duration)
	return float(points[-1].position.x)

func _process(delta: float) -> void:
	advance(delta)

func advance(delta: float) -> void:
	for kind in active_effects:
		for r in active_effects[kind]:
			var effect: Dictionary = active_effects[kind][r]
			effect.time += delta
			var points: Array = definitions[kind].points
			var value := curve_value(points, effect.time)
			var mesh: MeshInstance3D = meshes[kind][r]
			if mesh.material_override is ShaderMaterial:
				mesh.material_override.set_shader_parameter("intensity", value)
			elif mesh.material_override is StandardMaterial3D:
				mesh.material_override.albedo_color = effect.color * value
			mesh.visible = value > 0.0
			if kind.begins_with("unified:") or kind.begins_with("mouseout:"):
				if not points.is_empty() and effect.time >= float(points[-1].time):
					mesh.visible = false

## the fill meshes carry the region pick bodies (the retail pick-boxes are LMR_Fill's sub-objects, RegionObject)
func fill_meshes() -> Dictionary:
	return meshes.get("fill:0", {})


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
		var code := OVERLAY_SHADER % blend
		ShaderBudget.check(code, "wotr_region_fx overlay " + blend)
		sh.code = code
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


func _show(kind: String, r: Variant, c: Color) -> void:
	var mesh: MeshInstance3D = meshes.get(kind, {}).get(r, null)
	if mesh == null:
		return
	if not active_effects.has(kind):
		active_effects[kind] = {}
	if c.a <= 0.0:
		mesh.visible = false
		active_effects[kind].erase(r)
		return
	if active_effects[kind].has(r) and active_effects[kind][r].color == c:
		return # unchanged state must neither restart a curve nor allocate a material
	active_effects[kind][r] = {"time": 0.0, "color": c}
	var role := kind.get_slice(":", 0)
	var black := Color.BLACK
	if not material_cache.has(kind):
		material_cache[kind] = {}
	var material: Material = material_cache[kind].get(r)
	if material == null:
		if role == "highlight":
			var m := StandardMaterial3D.new()
			m.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
			m.cull_mode = BaseMaterial3D.CULL_DISABLED
			m.no_depth_test = true # S-1335: the ring otherwise loses its depth test in valleys
			m.render_priority = 3
			material = m
		else:
			var fill: bool = role in ["fill", "hover", "mouseout"]
			var blend := "blend_mix" if fill or role == "border" else "blend_add, depth_test_disabled"
			var priority := 1 if role == "fill" else (2 if role == "border" else 4)
			material = overlay_material(blend, Color8(91, 91, 91) if fill else black,
				Color8(225, 225, 225) if fill else black, c,
				(0.35 if fill else 1.0) * float(definitions[kind].opacity), textures.get(kind, {}).get(r, null), priority)
		material_cache[kind][r] = material
	mesh.material_override = material
	var value := curve_value(definitions[kind].points, 0.0)
	if material is ShaderMaterial:
		material.set_shader_parameter("emissive", Vector3(c.r, c.g, c.b))
		material.set_shader_parameter("intensity", value)
	else:
		material.albedo_color = c * value
	mesh.visible = value > 0.0
