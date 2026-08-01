shader_type spatial;

// RETAIL'S CLOUD CARDS, ANIMATED. The living map's `BORDERCLOUD` (the cloud
// ring that surrounds Middle-earth past the coastline) and `LM_CLOUDLAYER`
// (the thin drift of cloud over the map itself) were previously HELD BACK with
// the stated reason that retail animates them and this lane did not animate
// anything - a static smoke card would have been worse than none. This shader
// is the animation that reason was waiting for: retail's own texture, retail's
// own geometry, and a slow UV drift in place of retail's texture-coordinate
// animation.
//
// The TEXTURE is retail's, byte for byte. The tint is a presentation value the
// map view states where it sets it: `lm_border01.dds` is authored as a white
// cloud mask, and retail's own strategic screen shows it as the cold blue-grey
// weather bank visible in every reference capture.
render_mode blend_mix, depth_draw_never, cull_disabled, unshaded;

uniform sampler2D cloud_texture : source_color;
uniform vec4 tint : source_color = vec4(1.0, 1.0, 1.0, 1.0);
// UV per second. Slow by design: weather, not smoke.
uniform vec2 drift = vec2(0.0030, 0.0011);
// Optional world-space edge fade. LM_CLOUDLAYER is a hard-edged plane that
// overhangs the terrain by ~700 world units on every side; against the void
// its cut edge reads as a sheet of glass. When `fade_rect` is non-degenerate
// (max > min) the cloud fades out between the rect and `fade_margin` units
// beyond it, so the drift dies into the weather instead of ending on a line.
// Godot-space X and Z: (min_x, min_z, max_x, max_z).
uniform vec4 fade_rect = vec4(0.0, 0.0, 0.0, 0.0);
uniform float fade_margin = 500.0;

varying vec3 world_pos;

void vertex() {
	world_pos = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
}

void fragment() {
	vec4 sample_color = texture(cloud_texture, UV + drift * TIME);
	float fade = 1.0;
	if (fade_rect.z > fade_rect.x && fade_rect.w > fade_rect.y) {
		float outside = max(
			max(fade_rect.x - world_pos.x, world_pos.x - fade_rect.z),
			max(fade_rect.y - world_pos.z, world_pos.z - fade_rect.w));
		fade = 1.0 - clamp(outside / max(fade_margin, 1.0), 0.0, 1.0);
	}
	ALBEDO = sample_color.rgb * tint.rgb;
	ALPHA = sample_color.a * tint.a * fade;
}
