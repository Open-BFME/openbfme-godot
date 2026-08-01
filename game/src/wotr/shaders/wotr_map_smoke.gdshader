shader_type spatial;

// MOUNT DOOM'S SMOKE COLUMN, ANIMATED. The living map's `PLANE03` - the fan
// of cards climbing a thousand world units over Orodruin - was held back with
// the stated reason that retail animates it through particle sequences this
// lane does not implement, and a static smoke card would be worse than none.
// This shader is the animation that reason was waiting for, built from what
// retail actually ships for this surface: TWO frames, `LM_Doom01` (DXT5, with
// a real authored alpha mask - the plume's shape) and `LM_Doom02` (the churn
// detail). The detail frame is cross-faded and sheared INSIDE frame 0's own
// alpha, so the smoke churns without the card's rectangle ever showing.
//
// WHY ONLY MOUNT DOOM: the Orthanc and lava-vent cards' two frames are both
// flat mid-grey sheets - their shape lives in vertex alpha the bundle does
// not carry - so they CANNOT be composed from shipped bytes and stay held
// back, named. See `AMBIENT_SUB_OBJECTS` in `wotr_map_bundle.gd` for the
// measurement.
//
// The FRAMES are retail's, byte for byte, on retail's own cards and UVs. The
// cross-fade and drift rates are presentation values standing in for retail's
// sequence timing, the same claim the cloud and coast shaders state.
render_mode blend_mix, depth_draw_never, cull_disabled, unshaded;

uniform sampler2D frame_a : source_color;
uniform sampler2D frame_b : source_color;
uniform vec4 tint : source_color = vec4(1.0, 1.0, 1.0, 1.0);
// UV per second of churn drift for the DETAIL frame. The MASK frame barely
// moves: it is the plume's silhouette, anchored to the card.
uniform vec2 drift = vec2(0.002, -0.006);
// Cross-fade rate between the two frames, in cycles per second.
uniform float churn_speed = 0.35;

void fragment() {
	float churn = 0.5 + 0.5 * sin(TIME * churn_speed * 6.2831853);
	// The silhouette breathes very slightly rather than sliding: a smoke
	// column that drifted off its own volcano would be a defect, not weather.
	vec4 a = texture(frame_a, UV + drift * TIME * 0.15);
	// The detail frame shears through the silhouette.
	vec4 b = texture(frame_b, UV * 1.07 + drift * TIME + vec2(0.13, 0.0));
	vec3 smoke = mix(a.rgb, b.rgb, churn * 0.6);
	// FRAME 0'S AUTHORED ALPHA IS THE MASK. Frame 1 is opaque churn detail;
	// letting its alpha into the mask made the whole card solid at the top of
	// every cross-fade.
	ALBEDO = smoke * tint.rgb;
	ALPHA = clamp(a.a * tint.a, 0.0, 1.0);
}
