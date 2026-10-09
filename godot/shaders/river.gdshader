// OpenBFME. GPL-3.0.
//
// River ribbon: port of the RotWK 2.01 riverwater.fxo technique Default_M pixel shader (D3DX9 listing in
// the spec author's maps-scratch/fxo/riverwater.fx.clean.txt, ps_2_0):
//   river  = texture(RiverSampler, uv0)       sparkle = texture(SparklesSampler, uv0)
//   noise  = texture(NoiseSampler, world.xy/16 + UVScrollPerSecond*Time)
//   edge   = texture(OpacitySampler, uv1)     (the "alpha edge" texture)
//   rgb    = vtx.rgb*river.rgb + edge.rgb + sparkle.rgb*noise.rgb
//   a      = river.a * vtx.a * edge.a * Opacity.w;  rgb *= shroud
// Space: mesh in Godot's frame (x, z, -y); world.xy = (G.x, -G.z).
//
// NOT retail geometry: the retail mesh/UV generation of rivers (WaterRenderObj) is not ported, the UV
// layout here (uv0 = across 0..1, along = distance/river_uv_length; uv1 = across, 0.5) and the strip built
// from the cross-section lines are named assumptions (stop S-035).
shader_type spatial;
render_mode unshaded, blend_mix, depth_draw_never, cull_disabled;

uniform sampler2D river_tex : filter_linear_mipmap, repeat_enable, hint_default_white;
uniform sampler2D sparkle_tex : filter_linear_mipmap, repeat_enable, hint_default_black;
uniform sampler2D noise_tex : filter_linear_mipmap, repeat_enable, hint_default_white;
uniform sampler2D edge_tex : filter_linear_mipmap, repeat_disable, hint_default_white;
uniform vec2 uv_scroll_per_second = vec2(0.0, 0.0);
uniform float opacity = 1.0;
uniform float shroud = 1.0;
uniform float output_gamma = 2.2;

varying vec2 v_noise_uv;

void vertex() {
	vec2 world_xy = vec2(VERTEX.x, -VERTEX.z);
	v_noise_uv = world_xy * 0.0625 + TIME * uv_scroll_per_second;
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
	vec4 river = texture(river_tex, UV);
	vec3 sparkle = texture(sparkle_tex, UV).rgb;
	vec3 noise = texture(noise_tex, v_noise_uv).rgb;
	vec4 edge = texture(edge_tex, UV2);
	vec3 rgb = COLOR.rgb * river.rgb + edge.rgb + sparkle * noise;
	rgb *= shroud;
	ALBEDO = retail_output(rgb);
	ALPHA = clamp(river.a * COLOR.a * edge.a * opacity, 0.0, 1.0);
}
