shader_type spatial;

// RETAIL'S OCEAN SURFACE, IMPLEMENTED. Retail draws the living map's `WATER`
// sub-object with `WaterShader.FX` - a PROCEDURAL shader whose only authored
// property is a dimming factor, with no diffuse texture anywhere in the model.
// This is this project's implementation of the same idea: a translucent,
// animated sea surface standing on retail's own WATER mesh, over the seabed
// retail's own terrain tiles paint. Nothing here is a texture standing in for
// retail art, because retail's own material binds none: the colour, the swell
// and the glint are procedural THERE and procedural HERE.
//
// The palette is a presentation choice stated here rather than a sampled retail
// value: a cold deep-sea blue that agrees with the painted ocean in the terrain
// tiles it lies over (`lm_ocean03/04` and the painted tile seas all sit in this
// range).
//
// depth_draw_never: this surface lies just above the painted sea floor and must
// never occlude a marker, a fill or a landmark standing near the shore.
render_mode blend_mix, depth_draw_never, cull_disabled, specular_schlick_ggx;

// Deep-water and swell-crest colours. `source_color`, so both are authored in
// sRGB the way every other colour in this project is.
uniform vec4 deep_color : source_color = vec4(0.016, 0.075, 0.13, 1.0);
uniform vec4 swell_color : source_color = vec4(0.10, 0.24, 0.32, 1.0);
// How see-through the surface is. Kept translucent so retail's painted seabed
// - which carries the coastline art - reads through the animated surface.
uniform float surface_alpha : hint_range(0.0, 1.0) = 0.55;
// World units per swell repeat and swell speed. Presentation values; they reach
// nothing and the tests never render this shader.
uniform float swell_scale = 0.0045;
uniform float swell_speed = 0.30;
// World-space fade, same contract as the cloud shader's: the WATER plane
// overhangs retail's terrain rectangle to the north and east, and past the
// painted seabed its lit sheen read as a pale sheet against the void. Fades to
// nothing over `fade_margin` units outside `fade_rect` (Godot X/Z:
// min_x, min_z, max_x, max_z); degenerate rect means no fade.
uniform vec4 fade_rect = vec4(0.0, 0.0, 0.0, 0.0);
uniform float fade_margin = 400.0;

varying vec3 world_pos;

void vertex() {
	world_pos = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
}

// Three crossed sine swells. Cheap on purpose: this covers most of the panel
// and must not cost a frame budget a strategy screen has no reason to spend.
float swell(vec2 at, float t) {
	float a = sin(at.x * 5.0 + t * 1.1) + sin(at.y * 7.3 - t * 0.9);
	float b = sin((at.x + at.y) * 3.7 + t * 0.6);
	return (a + b) / 3.0;
}

void fragment() {
	// The mesh has no UVs - retail authored none - so the swell is sampled in
	// WORLD space, which also keeps it continuous across the plane's triangles.
	vec2 at = world_pos.xz * swell_scale * 40.0;
	float t = TIME * swell_speed;
	float crest = swell(at, t) * 0.5 + 0.5;

	// A second, finer layer breaks the first one's regularity.
	float ripple = swell(at * 3.1 + vec2(11.7, 3.9), t * 1.7) * 0.5 + 0.5;
	float lift = mix(crest, ripple, 0.35);

	float fade = 1.0;
	if (fade_rect.z > fade_rect.x && fade_rect.w > fade_rect.y) {
		float outside = max(
			max(fade_rect.x - world_pos.x, world_pos.x - fade_rect.z),
			max(fade_rect.y - world_pos.z, world_pos.z - fade_rect.w));
		fade = 1.0 - clamp(outside / max(fade_margin, 1.0), 0.0, 1.0);
	}
	ALBEDO = mix(deep_color.rgb, swell_color.rgb, lift * 0.55);
	ALPHA = surface_alpha * fade;
	// Wet-surface response: low roughness so the sun lays a glint path on the
	// sea the way it does on retail's living map. The mesh is a flat plane with
	// derived normals, so the glint comes from the material response rather
	// than from a normal map retail never authored.
	ROUGHNESS = 0.26;
	SPECULAR = 0.32;
}
