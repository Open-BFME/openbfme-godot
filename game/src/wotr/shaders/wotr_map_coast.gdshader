shader_type spatial;

// RETAIL'S SHORELINE STRIPS, BOTH STAGES COMPOSITED. The living map's six
// LM_COAST* sub-objects are authored as a TWO-STAGE material:
//
//   stage 0 - `SkyBoxNightClouds`, the sky-reflection pass. Retail's renderer
//     feeds it environment-style coordinates; drawn alone on the mesh UVs it
//     is an opaque night sky lying on a beach, which is exactly why round 1
//     refused to draw these strips at all.
//   stage 1 - `WtrWhitecap_01`, the foam, on the mesh's own UVs, animated in
//     retail by texture-coordinate animation.
//
// This shader is the composition that refusal was waiting for: the foam stage
// carries the surface, masked by its own brightness so only the whitecaps
// land on the sea, and the reflection stage rides under it as the faint cool
// sheen a reflection is - sampled with a slow drift because a static
// environment sample is a picture of a reflection, not one. Both textures are
// retail's, byte for byte; the geometry and its UVs are retail's; the drift
// and lap rates are presentation values standing in for retail's animation,
// the same claim `wotr_map_cloud.gdshader` states for its clouds.
render_mode blend_mix, depth_draw_never, cull_disabled, unshaded;

uniform sampler2D whitecap : source_color;
uniform sampler2D reflection : source_color;
// The foam's colour lift and overall strength. White foam, slightly cool.
uniform vec4 foam_tint : source_color = vec4(0.90, 0.95, 1.0, 1.0);
uniform float foam_strength : hint_range(0.0, 2.0) = 1.0;
// How strongly the stage-0 reflection shows through under the foam.
uniform float reflection_strength : hint_range(0.0, 1.0) = 0.14;
// UV per second along the strip, and the lapping pulse rate. Slow: surf.
uniform vec2 drift = vec2(0.020, 0.0);
uniform float lap_speed = 0.8;

void fragment() {
	// Two samples of the same foam, drifting against each other, is what makes
	// the caps CHURN rather than slide as one printed sheet.
	vec3 cap_a = texture(whitecap, UV + drift * TIME).rgb;
	vec3 cap_b = texture(whitecap, UV * 1.19 - drift * TIME * 0.63 + vec2(0.0, 0.37)).rgb;
	// The lap: the whole strip breathes, offset along its length so the surf
	// arrives as a travelling swell instead of blinking in unison.
	float lap = 0.72 + 0.28 * sin(TIME * lap_speed + UV.x * 9.0);
	vec3 foam = (cap_a * 0.62 + cap_b * 0.55) * lap;
	// The foam texture is DXT1 - no alpha channel - so its own brightness is
	// the mask, which is what a whitecap sheet on black means. The smoothstep
	// floor matters: without it the strip's dim texels gave the whole ribbon
	// (and the lakes it covers) a flat milky cast, and Lake Evendim glowed
	// like a lamp. Foam is CAPS or nothing.
	float mask = smoothstep(0.12, 0.60, max(foam.r, max(foam.g, foam.b)));
	vec3 sheen = texture(reflection, UV * 0.31 + vec2(TIME * 0.004, 0.0)).rgb;
	// The reflection rides INSIDE the foam's own mask rather than flooding the
	// strip: it is the cool light the caps catch, not a second surface.
	ALBEDO = foam * foam_tint.rgb * foam_strength + sheen * reflection_strength * mask;
	ALPHA = clamp(mask * foam_strength, 0.0, 1.0);
}
