// OpenBFME. GPL-3.0.
//
// Road segment ribbon. Retail draws roads with ST_ROAD_BASE (road texture x static terrain diffuse, optional
// cloud/noise passes, W3DShaderManager); the road geometry comes from W3DRoadBuffer. Only straight
// segments are generated (no tee / curve / cross joins, stop S-036). Colour: texture * vertex colour where
// the vertex colour is the static terrain light at the segment's position (ZH RoadSegment::updateSegLighting).
shader_type spatial;
render_mode unshaded, blend_mix, depth_draw_never, cull_disabled;

uniform sampler2D road_tex : filter_linear_mipmap, repeat_enable, hint_default_white;
uniform float output_gamma = 2.2;
uniform float overbright = 1.0;

// lane RENDER-4: the hand-over to Godot. output_gamma 1.0: the gamma-space value itself (the transparent pass composites in gamma space,
// GodotGammaComposite.h). Any other value: the exact sRGB decode, the inverse of the output encode Godot applies, so the display shows the
// retail byte. The former pow(x, 2.2) did not invert that curve and darkened the dark end of every lit pixel (retail 0.1 displayed 0.073).
vec3 retail_output(vec3 c) {
	c = max(c, vec3(0.0));
	if (output_gamma == 1.0) {
		return c;
	}
	return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c));
}

void fragment() {
	vec4 t = texture(road_tex, UV);
	vec3 col = t.rgb * COLOR.rgb * overbright;
	ALBEDO = retail_output(col);
	ALPHA = t.a;
}
