// OpenBFME. GPL-3.0.
//
// Lane WEATHERFX-1 (NOT RETAIL: the Enhanced look's weather, scripts/weather_fx.gd): falling rain streaks or snowflakes, ONE MultiMesh of quads
// (identity instance transforms; the instance index seeds every drop). Each drop lives in a box that is anchored to the WORLD (a box position
// wraps by the box size, so the drops do not swim when the camera pans) and recentred on the camera's view; it falls at its own speed, slanted by
// the wind, and is drawn
//   rain: as a streak along its velocity (a camera-facing ribbon at least ~1 pixel wide, its alpha scaled down when widened, so a far drop does
//         not vanish or sparkle), brightest at the head;
//   snow: as a soft round flake facing the camera that sways in the wind's gusts, bigger and softer near the camera (a depth-of-field look).
// Both fade where they meet the ground (the depth texture: no hard cut where a drop enters the terrain) and near the camera. The colour follows the
// scene's light (`light`, the darkening of the cloud effect) and the lightning flash.
shader_type spatial;
render_mode unshaded, blend_mix, depth_draw_never, cull_disabled, shadows_disabled, world_vertex_coords, fog_disabled;

uniform sampler2D depth_tex : hint_depth_texture, filter_nearest;
uniform bool snow = false;
uniform vec3 box_min = vec3(0.0);   // Godot frame
uniform vec3 box_size = vec3(1000.0, 400.0, 1000.0);
uniform float fall_speed = 600.0;   // units / s
uniform vec2 wind = vec2(0.0);      // Godot x / z units / s
uniform float drop_length = 22.0;
uniform float drop_width = 0.35;
uniform float px_world = 0.001;     // the size of a pixel at distance 1 (2 tan(fov / 2) / viewport height)
uniform float visible_fraction = 1.0; // the drops drawn (the rest collapse to nothing): the rain's strength and the detail level
uniform float opacity = 0.5;
uniform vec3 light = vec3(1.0);
uniform float flash = 0.0;
uniform float gust = 0.0;           // snow: the gusts' strength
uniform float time = 0.0;

varying float v_alpha;
varying vec2 v_uv;

vec3 drop_hash(float n) {
	vec3 p = fract(vec3(n) * vec3(0.1031, 0.1030, 0.0973));
	p += dot(p, p.yxz + 33.33);
	return fract((p.xxy + p.yxx) * p.zyx);
}

float wrap_axis(float r, float lo, float size) {
	return lo + mod(r - lo, size);
}

void vertex() {
	float id = float(INSTANCE_ID);
	vec3 r = drop_hash(id + 1.0);
	vec3 r2 = drop_hash(id * 1.37 + 91.0);
	v_uv = UV;
	v_alpha = 0.0;
	if (r2.z > visible_fraction) {
		VERTEX = vec3(0.0);
	} else {
		float speed = fall_speed * (0.8 + 0.4 * r2.x);
		vec2 w = wind;
		vec3 p;
		if (snow) {
			// gusts: a slow wave over space and time pushes the flakes along the wind, plus each flake's own flutter
			float g = sin(time * 0.35 + r.x * 6.0) * 0.5 + 0.5;
			g = g * g * gust;
			w = wind * (1.0 + g * 3.0) + vec2(sin(time * 1.3 + id), cos(time * 1.1 + id * 0.7)) * 6.0;
		}
		float t = time;
		p.x = wrap_axis(r.x * 4096.0 + t * w.x, box_min.x, box_size.x);
		p.z = wrap_axis(r.z * 4096.0 + t * w.y, box_min.z, box_size.z);
		p.y = wrap_axis(r.y * 4096.0 - t * speed, box_min.y, box_size.y);
		if (snow) {
			p.x += sin(time * (0.7 + r2.y) + id) * 4.0;
			p.z += cos(time * (0.6 + r2.x) + id * 1.3) * 4.0;
		}
		vec3 to_cam = CAMERA_POSITION_WORLD - p;
		float dist = length(to_cam);
		vec3 view = to_cam / max(dist, 0.001);
		float min_w = dist * px_world * 0.9;
		if (snow) {
			// small flakes, at least a pixel and dimmer far away; the flakes near the camera fade out entirely (readability: no big blurry
			// flakes over the units, the owner 2026-10-29)
			float size = mix(0.9, 1.8, r2.y);
			float drawn = max(size, min_w * 1.4);
			v_alpha = opacity * (size / drawn) * smoothstep(160.0, 320.0, dist);
			vec3 side = normalize(cross(view, vec3(0.0, 1.0, 0.0)));
			vec3 upv = cross(side, view);
			VERTEX = p + side * (UV.x - 0.5) * drawn + upv * (UV.y - 0.5) * drawn;
		} else {
			vec3 axis = normalize(vec3(w.x, -speed, w.y));
			vec3 side = cross(axis, view);
			float sl = length(side);
			side = sl > 0.0001 ? side / sl : vec3(1.0, 0.0, 0.0);
			float width = max(drop_width, min_w);
			float len = drop_length * (0.7 + 0.6 * r2.y);
			v_alpha = opacity * (drop_width / width) * smoothstep(40.0, 160.0, dist);
			VERTEX = p + side * (UV.x - 0.5) * width - axis * UV.y * len;
		}
	}
}

void fragment() {
	float a = v_alpha;
	if (snow) {
		vec2 d = v_uv * 2.0 - 1.0;
		a *= smoothstep(1.0, 0.15, dot(d, d));
	} else {
		float across = 1.0 - abs(v_uv.x * 2.0 - 1.0);
		a *= across * (1.0 - v_uv.y) * (0.35 + 0.65 * (1.0 - v_uv.y));
	}
	// soft where the drop meets the ground: the scene's depth behind the fragment
	float scene = texture(depth_tex, SCREEN_UV).r;
	vec4 sv = INV_PROJECTION_MATRIX * vec4(SCREEN_UV * 2.0 - 1.0, scene, 1.0);
	float scene_dist = -sv.z / sv.w;
	float frag_dist = -VERTEX.z;
	a *= clamp((scene_dist - frag_dist) / (snow ? 12.0 : 25.0), 0.0, 1.0);
	vec3 base = snow ? vec3(0.95, 0.96, 1.0) : vec3(0.72, 0.77, 0.86);
	ALBEDO = base * light + vec3(0.8, 0.85, 1.0) * flash;
	ALPHA = clamp(a, 0.0, 1.0);
}
