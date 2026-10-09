// OpenBFME. GPL-3.0. The terrain: ONE opaque draw per chunk that composites the base layer and the two
// blend layers per pixel in retail's gamma space and converts once (see terrain_common.gdshaderinc and
// engine TerrainComposite.h). Replaces the former three-draw base / blend / blend layout, which blended in
// Godot's linear framebuffer and let the blend layers draw over water. The blend state of the retail layers
// is INFERRED (SRCALPHA / INVSRCALPHA, the effect passes declare none, spec 3.2; stop S-032).
//
// Vertex inputs: UV = base layer atlas UV, UV2 = cell-local coordinates of the corner, CUSTOM0 = base layer
// class wrap rect (x0, y0, w, h), CUSTOM1.x = record index of the cell's blend layers (-1: none).
shader_type spatial;
render_mode unshaded, cull_back;

#include "res://shaders/terrain_common.gdshaderinc"

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
}

void fragment() {
	ALBEDO = terrain_output(UV, COLOR);
}
