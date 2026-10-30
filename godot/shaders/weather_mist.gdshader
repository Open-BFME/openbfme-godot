// OpenBFME. GPL-3.0.
//
// Lane WEATHERFX-1 (NOT RETAIL: the Enhanced look's weather, scripts/weather_fx.gd): a layer of low mist. A horizontal sheet a little above the
// low ground of the view; where the terrain is below the sheet (valleys, water) the mist between the sheet and the ground shows, thicker the deeper
// the ground lies under it (the depth texture), and the sheet fades out softly where the ground rises through it, so the mist settles in the
// hollows. A slow drifting noise (two octaves sliding along the wind at different speeds) breaks it up; the edges of the sheet fade out.
shader_type spatial;
render_mode unshaded, blend_mix, depth_draw_never, cull_disabled, shadows_disabled, fog_disabled;

uniform sampler2D depth_tex : hint_depth_texture, filter_nearest;
uniform sampler2D noise_tex : filter_linear_mipmap, repeat_enable; // the baked seamless noise (weather_fx.gd)
uniform vec3 tint = vec3(0.75, 0.78, 0.82);
uniform float density = 0.0;     // 0 .. 1
uniform float depth_range = 60.0; // the ground this far under the sheet gives the full mist
uniform vec2 drift = vec2(0.0);  // SAGE-free world drift (Godot x / z units), wrapped by the script
uniform float time = 0.0;
uniform float swirl = 0.0;       // Darkness: a faster, curling gloom

varying vec3 v_world;
varying vec2 v_local;

float m_noise(vec2 p) {
	return texture(noise_tex, p * (1.0 / 8.0)).r;
}

float m_fbm(vec2 p) {
	return 0.7 * m_noise(p) + 0.3 * m_noise(p * 2.7 + vec2(3.1, 5.3));
}

void vertex() {
	v_world = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
	v_local = UV;
}

void fragment() {
	if (density <= 0.0) {
		discard;
	}
	float scene = texture(depth_tex, SCREEN_UV).r;
	vec4 sv = INV_PROJECTION_MATRIX * vec4(SCREEN_UV * 2.0 - 1.0, scene, 1.0);
	float scene_dist = -sv.z / sv.w;
	float frag_dist = -VERTEX.z;
	float behind = max(scene_dist - frag_dist, 0.0);
	// how deep the ground is under the sheet along this view ray, as a height (the camera looks down at ~37 degrees)
	vec3 view_dir = normalize((INV_VIEW_MATRIX * vec4(VERTEX, 0.0)).xyz);
	float under = behind * clamp(-view_dir.y, 0.15, 1.0);
	float thick = smoothstep(0.0, depth_range, under);
	if (thick <= 0.002) {
		discard; // the ground at or above the sheet: no mist, no noise to evaluate
	}
	vec2 p = v_world.xz;
	// one warp noise (two for Darkness's curling gloom), then the wisps
	vec2 q = vec2(m_noise(p * 0.004 + drift * 0.004), swirl > 0.5 ? m_noise(p * 0.004 - drift * 0.003 + 7.0) : 0.5);
	float n = m_fbm(p * 0.0065 + q * (1.2 + swirl * 2.0) + drift * 0.006 + time * 0.01 * swirl);
	float wisps = smoothstep(0.3, 0.8, n);
	vec2 e = abs(v_local * 2.0 - 1.0);
	float edge = (1.0 - smoothstep(0.6, 1.0, e.x)) * (1.0 - smoothstep(0.6, 1.0, e.y));
	float near = smoothstep(30.0, 200.0, frag_dist);
	ALBEDO = tint * (0.85 + 0.3 * n);
	ALPHA = clamp(density * thick * (0.35 + 0.65 * wisps) * edge * near, 0.0, 0.85);
}
