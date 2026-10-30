// OpenBFME. GPL-3.0.
//
// Lane WEATHERFX-1 (NOT RETAIL: the Enhanced look's weather, scripts/weather_fx.gd): what the weather does to the ground, on top of the retail
// terrain colour of terrain_common.gdshaderinc (gamma space, before the fog / cloud / macro / shroud terms):
//   wet ground   the ground darkens as rain soaks it and gets a sky sheen and a sun glint, flat ground first, puddles in the hollows of a noise;
//   ripples      rings spreading in the puddles (one drop per cell and cycle, at a cell's own random spot and phase);
//   snow cover   white on the upward faces, building in from the flattest ground and the noise's high spots, melting back the same way;
//   cloud shadow a slow noise of dark cloud shadows drifting with the wind;
//   flash        a lightning flash lighting the ground (a cold white);
//   gloom        Darkness as a moving, uneven gloom (deeper than retail's darkening in places, never lighter).
// Every term is a project shader global (project.godot [shader_globals]) the script sets; with wfx_enabled 0 (the Original look, and Enhanced
// without weather) none of this runs and the terrain is exactly the retail one.

global uniform float wfx_enabled;
global uniform float wfx_time;      // seconds, wrapped by the script (precision)
global uniform float wfx_wet;       // 0 .. 1 wetness of the ground
global uniform float wfx_ripples;   // 0 .. 1 rain on the ground now
global uniform float wfx_snow;      // 0 .. 1 snow cover
global uniform float wfx_clouds;    // 0 .. 1 cloud shadow strength
global uniform vec2 wfx_cloud_offset; // SAGE xy drift of the cloud noise (world units, wrapped by the script)
global uniform float wfx_flash;     // 0 .. 1 lightning flash
global uniform float wfx_gloom;     // 0 .. 1 Darkness gloom
global uniform sampler2D wfx_noise_tex : filter_linear_mipmap, repeat_enable; // a seamless 256 x 256 noise (weather_fx.gd); one fetch per octave

float wfx_hash(vec2 p) {
	p = fract(p * vec2(123.34, 456.21));
	p += dot(p, p + 45.32);
	return fract(p.x * p.y);
}

vec2 wfx_hash2(vec2 p) {
	float h = wfx_hash(p);
	return vec2(h, wfx_hash(p + h + 17.0));
}

// two octaves of the baked noise (the texture is itself a 4-octave fractal; procedural value noise cost ~0.3 ms per use on a Vega 7 iGPU)
float wfx_fbm(vec2 p) {
	float v = 0.7 * texture(wfx_noise_tex, p * (1.0 / 8.0)).r;
	v += 0.3 * texture(wfx_noise_tex, p * (1.0 / 8.0) * 2.7 + vec2(0.37, 0.71)).r;
	return v;
}

// rings on the wet ground: one drop per cell of `cell` units and cycle, the ring's strength 0 .. 1
float wfx_ripple(vec2 p, float cell, float t) {
	float ring = 0.0;
	for (int k = 0; k < 2; k++) {
		vec2 cp = floor(p / cell + vec2(float(k) * 0.5)) - vec2(float(k) * 0.5); // two interleaved grids, half a cell apart
		vec2 h = wfx_hash2(cp + float(k) * 31.0);
		float phase = fract(t * 0.9 + h.x + h.y * 0.37);
		vec2 centre = (cp + 0.2 + 0.6 * h) * cell;
		float r = phase * cell * 0.42;
		float d = length(p - centre);
		float w = 0.18 + phase * 0.35;
		ring += smoothstep(w, 0.0, abs(d - r)) * (1.0 - phase) * (1.0 - phase);
	}
	return clamp(ring, 0.0, 1.0);
}

// `col` the retail terrain colour (gamma space), `p` the SAGE world point, `n` the SAGE vertex normal, `to_eye` the SAGE direction to the camera,
// `sun` the SAGE direction toward the sun, `dark` the cloud effect's darkening of the moment (retail's, already in `col`)
vec3 wfx_terrain(vec3 col, vec3 p, vec3 n, vec3 to_eye, vec3 sun, vec3 dark) {
	float up = clamp(n.z, 0.0, 1.0);
	float flat_ground = smoothstep(0.8, 0.97, up);
	vec3 keep = vec3(1.0) - dark; // the scene's light under the cloud effect
	if (wfx_wet > 0.001) {
		float puddle = 0.0;
		if (flat_ground > 0.0) {
			puddle = smoothstep(0.56, 0.68, wfx_fbm(p.xy * 0.012)) * flat_ground * smoothstep(0.35, 1.0, wfx_wet);
		}
		float soak = wfx_wet * (0.55 + 0.45 * flat_ground);
		col *= mix(1.0, 0.84, soak);
		col = mix(col, col * 0.65 + vec3(0.07, 0.085, 0.1) * keep, puddle * 0.7);
		vec3 r = reflect(-sun, n);
		float glint = pow(max(dot(r, to_eye), 0.0), mix(18.0, 140.0, puddle));
		float fres = pow(1.0 - clamp(dot(n, to_eye), 0.0, 1.0), 4.0);
		col += vec3(0.62, 0.67, 0.75) * keep * (glint * (0.12 + 0.55 * puddle) + fres * 0.18 * puddle) * wfx_wet;
		if (wfx_ripples > 0.001 && puddle > 0.02) {
			// rings only in the puddles (on the rest of the ground they were barely visible from the RTS camera and cost ~0.4 ms on a Vega 7 iGPU)
			float ring = wfx_ripple(p.xy, 9.0, wfx_time);
			col += vec3(0.55, 0.6, 0.66) * keep * ring * wfx_ripples * puddle * 0.6;
		}
	}
	if (wfx_snow > 0.001) {
		float drift = wfx_fbm(p.xy * 0.02 + 3.0);
		float cover = smoothstep(0.62, 0.9, up + (drift - 0.5) * 0.45);
		cover *= smoothstep(0.0, 0.35, wfx_snow * 1.35 - (1.0 - drift) * 0.55);
		// the snow keeps the ground's own shading and texture (readability: paths, plots and the ground's detail stay visible):
		// a cool brightening of the ground's own colour (multiplied, so its texture and shading keep their relative contrast), not a flat white
		// at most to a luma of 0.72 (no clipping: on already white ground it adds nothing and the grain stays)
		float lum = max(dot(col, vec3(0.299, 0.587, 0.114)), 0.02);
		vec3 snowcol = col * vec3(0.95, 1.0, 1.08) * clamp(0.72 / lum, 1.0, 1.6);
		float sparkle = step(0.985, wfx_hash(floor(p.xy * 1.7))) * pow(max(dot(reflect(-sun, n), to_eye), 0.0), 6.0);
		col = mix(col, snowcol + sparkle * 0.25 * keep, cover * 0.7);
	}
	if (wfx_clouds > 0.001) {
		float c = wfx_fbm((p.xy + wfx_cloud_offset) * 0.0011);
		float shade = smoothstep(0.42, 0.7, c);
		col *= 1.0 - 0.32 * wfx_clouds * shade;
	}
	if (wfx_gloom > 0.001) {
		float g = wfx_fbm((p.xy + vec2(wfx_cloud_offset.y, -wfx_cloud_offset.x) * 1.6) * 0.0022 + wfx_time * 0.02);
		vec3 tint = mix(vec3(0.92, 0.88, 0.98), vec3(0.74, 0.66, 0.84), smoothstep(0.35, 0.75, g));
		col *= mix(vec3(1.0), tint, wfx_gloom);
	}
	if (wfx_flash > 0.001) {
		col += (col * 1.6 + vec3(0.16, 0.18, 0.24)) * wfx_flash * (0.5 + 0.5 * up);
	}
	return col;
}
