// OpenBFME. GPL-3.0.
//
// Standing water. NOT the retail shader: retail draws standing water with the W3D material's
// WaterShader.FX (bump + environment + depth tint, techniques Default / BumpSpecular / PCA /
// DepthOnly), whose parameters live in a W3D FX chunk (another lane) and which also needs the depth-colour
// LUT and map.ini WaterTransparency. This is a deliberately simple stand-in: a tinted, translucent surface
// with a scrolling ripple bump and a faint sky-environment term (stop S-035). Mesh space: Godot frame.
shader_type spatial;
render_mode unshaded, blend_mix, depth_draw_never, cull_disabled;

uniform sampler2D bump_tex : filter_linear_mipmap, repeat_enable, hint_default_white;
uniform sampler2D sky_tex : filter_linear_mipmap, repeat_enable, hint_default_white;
uniform sampler2D tint_lut : filter_linear, repeat_disable, hint_default_white;
uniform vec3 water_color = vec3(0.18, 0.34, 0.46);
uniform float opacity = 0.78;
uniform float scroll_speed = 0.06;
uniform float tile_world_units = 160.0;
uniform float output_gamma = 2.2;

varying vec2 v_world;

void vertex() {
	v_world = vec2(VERTEX.x, -VERTEX.z);
}

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
	vec2 uv = v_world / tile_world_units + vec2(TIME * scroll_speed, TIME * scroll_speed * 0.6);
	vec3 bump = texture(bump_tex, uv).rgb;
	float ripple = (bump.r - 0.5) * 0.35;
	vec3 sky = texture(sky_tex, v_world / (tile_world_units * 3.0) + bump.xy * 0.04).rgb;
	vec3 tint = texture(tint_lut, vec2(0.5, 0.5)).rgb;
	vec3 col = water_color * tint * (1.0 + ripple) + sky * 0.22;
	ALBEDO = retail_output(col);
	ALPHA = opacity;
}
