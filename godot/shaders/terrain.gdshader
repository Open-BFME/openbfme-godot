// OpenBFME. GPL-3.0. The terrain: ONE opaque draw per chunk that composites the base layer and the two
// blend layers per pixel in retail's gamma space and converts once (see terrain_common.gdshaderinc and
// engine TerrainComposite.h). Replaces the former three-draw base / blend / blend layout, which blended in
// Godot's linear framebuffer and let the blend layers draw over water. The blend state of the retail layers
// is INFERRED (SRCALPHA / INVSRCALPHA, the effect passes declare none, spec 3.2; stop S-032).
//
// Vertex inputs: UV = base layer atlas UV, UV2 = cell-local coordinates of the corner, CUSTOM0 = base layer
// class wrap rect (x0, y0, w, h), CUSTOM1.x = record index of the cell's blend layers (-1: none).
shader_type spatial;
render_mode ambient_light_disabled, specular_disabled, cull_back;

#include "res://shaders/terrain_common.gdshaderinc"
#include "res://shaders/selection_decals.gdshaderinc"
#include "res://shaders/radius_cursor.gdshaderinc"
#include "res://shaders/object_shadow.gdshaderinc"

varying vec2 v_sage_xy; // lane SELPERF-1: the world point of the pixel (SAGE x, y) for the selection's merge decals
varying vec4 v_weather_vcol; // lane WEATHER-1: the vertex colour relit with the cloud effect's darkening (weather_relit)

void vertex() {
	vec3 lts;
	vec3 hts;
	float fog;
	vec2 cloud;
	vec2 macro_uv;
	terrain_vertex(VERTEX, NORMAL, CAMERA_POSITION_WORLD, TIME, lts, hts, fog, cloud, macro_uv);
	v_lts = lts;
	v_hts = hts;
	v_fog = fog;
	v_cloud = cloud;
	v_macro = macro_uv;
	v_local = UV2;
	v_wrap = CUSTOM0;
	v_rec = CUSTOM1.x;
	vec3 world = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
	v_shroud_uv = (vec2(world.x, -world.z) - shroud_origin) / shroud_extent;
	v_sage_xy = vec2(world.x, -world.z);
	v_weather_vcol = weather_relit ? weather_vertex_color(NORMAL, COLOR) : COLOR;
	v_wfx_pos = vec3(world.x, -world.z, world.y); // lane WEATHERFX-1 (terrain_common.gdshaderinc)
	v_wfx_nrm = vec3(NORMAL.x, -NORMAL.z, NORMAL.y);
	v_wfx_eye = vec3(CAMERA_POSITION_WORLD.x, -CAMERA_POSITION_WORLD.z, CAMERA_POSITION_WORLD.y);
}

void fragment() {
	ALBEDO = terrain_output(UV, weather_relit ? v_weather_vcol : COLOR);
	vec3 ground = ALBEDO;
	float ground_weight = 1.0;
	// lane SELPERF-1: the selection's merge decals over the terrain, as RotWK's stencil passes draw them after it (selection_decals.gdshaderinc)
	vec4 sel = sel_merge(v_sage_xy);
	if (sel.a > 0.0) {
		ALBEDO = mix(ALBEDO, retail_output(sel.rgb), sel.a);
		ground_weight *= 1.0 - sel.a;
	}
	// Composite the retail radius texture with the terrain, as with selection decals.
	vec4 cursor = radius_cursor_sample(v_sage_xy);
	if (radius_cursor_params.y > 0.5) {
		ALBEDO = mix(ALBEDO, retail_output(cursor.rgb), cursor.a);
		ground_weight *= 1.0 - cursor.a;
	} else {
		ALBEDO += retail_output(cursor.rgb) * cursor.a;
	}
	// Keep selection and placement overlays above shadows, as before.
	v_shadow_color = shadow_to_gamma(ground);
	float enabled = debug_mode == 0 ? w3d_shadow.w : 0.0;
	EMISSION = ALBEDO - ground * ground_weight * enabled;
	ALBEDO = vec3(ground_weight * enabled);
}

void light() {
	if (LIGHT_IS_DIRECTIONAL) DIFFUSE_LIGHT += object_shadow_light(ATTENUATION);
}
