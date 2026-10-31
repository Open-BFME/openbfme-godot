// OpenBFME. GPL-3.0.
//
// Retail watershader.fxo: bump/specular blobs 8/9 and the PCA/PCA_U normal
// reconstruction from pixel blobs 0/3. Coefficient layout/interpolation comes
// from the matched BFME2 water loader/samplers (WaterGeometry.cpp).
// S-035: omitted reflections/shadows, Godot
// TIME and bilinear terrain depth prevent full target technique parity.
shader_type spatial;
render_mode unshaded, blend_mix, depth_draw_never, cull_disabled;

uniform sampler2D bump_tex : filter_linear_mipmap, repeat_enable, hint_default_white;
uniform sampler2D sky_tex : filter_linear_mipmap, repeat_enable, hint_default_white;
uniform sampler2D wave_tex : filter_linear_mipmap, repeat_enable, hint_default_black;
uniform sampler2D terrain_height_tex : filter_linear, repeat_disable;
uniform vec4 material_diffuse = vec4(0.15, 0.3, 0.35, 0.5);
uniform float dimming = 0.8;
uniform float bump_scale = 0.2;
uniform float specular_exponent = 50.0;
uniform vec2 bump_uv_scale = vec2(7.0);
uniform vec2 env_uv_scale = vec2(0.2);
uniform vec2 wave_uv_scale = vec2(7.0);
uniform float wind_angle = 0.0;
uniform float wind_speed = 0.02;
uniform float wave_angle = 0.2;
uniform float wave_speed = 0.0;
uniform vec3 light_direction = vec3(0.0, 0.0, 1.0);
uniform vec3 light_color = vec3(1.0);
uniform vec2 terrain_origin = vec2(0.0);
uniform vec2 terrain_size = vec2(1.0);
uniform float water_height = 0.0;
uniform float max_alpha_depth = 0.0;
uniform float deep_water_alpha = 0.0;
uniform bool blend_terrain_alpha = true;
uniform float output_gamma = 2.2;

// PCA coefficient tracks: mean then X/Y/Z for basis groups 0..2. Explicit
// texel fetch and successor interpolation match the BFME2 CPU samplers.
uniform int pca_mode = 0;
uniform bool wave_geometry_enabled = false;
uniform float wave_time_scale = 1.0;
uniform vec4 gerstner0 = vec4(1.0,0.1,0.1,0.5);
uniform vec4 gerstner1 = vec4(0.00001);
uniform vec4 gerstner2 = vec4(0.00001);
varying vec3 v_wave_normal;
varying float v_froth;
varying float v_depth;

// Retail effect VS13/14: xyz displacement, xyz normal contribution.
void gerstner(vec4 g, vec2 direction, vec3 point, float jitter, float steepness,
	inout vec3 offset, inout vec3 normal) {
	float amplitude=g.x*0.075;
	float frequency=g.y+jitter;
	if (amplitude == 0.0 || frequency == 0.0) return;
	float phase=frequency*dot(point.xy,direction)+TIME*wave_time_scale*g.z;
	float q=g.w*steepness/(frequency*amplitude);
	offset += vec3(q*amplitude*direction*cos(phase),amplitude*sin(phase));
	normal -= vec3(frequency*amplitude*direction*cos(phase),frequency*amplitude*q*sin(phase));
}
uniform sampler2D pca_coefficients : filter_nearest, repeat_disable;
uniform sampler2D pca_tex0 : filter_linear_mipmap, repeat_enable;
uniform sampler2D pca_tex1 : filter_linear_mipmap, repeat_enable;
uniform sampler2D pca_tex2 : filter_linear_mipmap, repeat_enable;
uniform sampler2D pca_froth_tex : filter_linear_mipmap, repeat_enable;
uniform vec2 pca_wave_uv_scale = vec2(0.01, 0.0075);
uniform sampler2D pca_noise_tex : filter_linear_mipmap, repeat_enable;
uniform vec2 pca_uv_scale = vec2(1.5, 3.0);
uniform float pca_frame_rate = 0.0;
uniform vec3 specular_color = vec3(0.8);
uniform float reflection_perturbation = 0.005;

vec4 pca_coefficient(int row) {
	int count = textureSize(pca_coefficients, 0).x;
	float scaled = TIME * pca_frame_rate;
	int frame = int(mod(floor(scaled), float(count)));
	float fraction = fract(scaled);
	return texelFetch(pca_coefficients, ivec2(frame, row), 0) * (1.0 - fraction)
		+ texelFetch(pca_coefficients, ivec2((frame + 1) % count, row), 0) * fraction;
}

varying vec2 v_world;
varying vec3 v_world_position;
// lane ZOOM-1: the local player's shroud (GameWorld::refresh_shroud sets these on the terrain and on this layer): ZH WaterRenderObjClass modulates its
// water with the shroud texture in a second pass / stage (W3DWater.cpp, the ST_SHROUD_TEXTURE passes); the border level outside the map is black
uniform sampler2D shroud_tex : filter_linear, repeat_disable, hint_default_white;
uniform vec2 shroud_origin = vec2(0.0);
uniform vec2 shroud_extent = vec2(1.0);
uniform vec3 shroud_color = vec3(1.0);
uniform bool shroud_enabled = false;
varying vec2 v_shroud_uv;

void vertex() {
	vec3 world = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
	v_wave_normal=vec3(0.0,0.0,1.0);
	v_froth=0.125;
	v_depth=max(world.y-CUSTOM0.z,0.0);
	if (wave_geometry_enabled && pca_mode != 0) {
		vec3 point=vec3(world.x,-world.z,world.y);
		float distance_to_shore=distance(point.xy,CUSTOM0.xy);
		vec3 offset=vec3(0.0);
		if (v_depth<100.0 && distance_to_shore<CUSTOM0.w) {
			vec2 direction=CUSTOM1.xy;
			vec2 turn=vec2(-direction.y,direction.x)*0.4;
			float steepness=pca_mode==2 ? 0.33 : 0.15;
			gerstner(gerstner0,direction,point,fract(point.x)*0.0005,steepness,offset,v_wave_normal);
			gerstner(gerstner1,normalize(direction+turn),point,fract(point.y)*0.0003,steepness,offset,v_wave_normal);
			gerstner(gerstner2,normalize(direction-turn),point,fract(point.z)*0.0003,steepness,offset,v_wave_normal);
			if(pca_mode==2) offset.xy *= 0.75;
			v_wave_normal=normalize(v_wave_normal);
		}
		v_depth=point.z+offset.z-CUSTOM0.z;
		float crest=pow(max((offset.z-0.2)/3.8,0.0),2.0);
		float edge=1.0;
		if(pca_mode==2 && CUSTOM0.w>0.0) edge=distance_to_shore<=CUSTOM0.w*0.5 ? 0.75 : (CUSTOM0.w-distance_to_shore)/(CUSTOM0.w*0.5);
		v_froth += crest*((100.0-v_depth)/100.0)*edge + max((15.0-v_depth)/30.0,0.0);
		world += vec3(offset.x,offset.z,-offset.y);
		VERTEX=(inverse(MODEL_MATRIX)*vec4(world,1.0)).xyz;
	}
	v_world = vec2(world.x, -world.z);
	v_world_position = world;
	vec3 shroud_world = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
	v_shroud_uv = (vec2(shroud_world.x, -shroud_world.z) - shroud_origin) / shroud_extent;
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
	// VS 9 uses world.xy / 600, not a fixed 160-unit repeating tile.
	vec2 base = v_world / 600.0;
	float a0 = wind_angle - wave_angle * 0.5;
	float a1 = wind_angle + wave_angle * 0.5;
	vec2 d0 = vec2(cos(a0), sin(a0));
	vec2 d1 = vec2(cos(a1), sin(a1));
	vec2 b0 = base + TIME * (wind_speed + wave_speed) * d0;
	vec2 b1 = base + TIME * (wind_speed + wave_speed) * d1;
	vec2 w0 = base + TIME * (wind_speed + wave_speed) * d0;
	vec2 w1 = base + TIME * (wind_speed + wave_speed) * d1;
	// D3D rotation rows are (cos, sin), (-sin, cos).
	b0 = vec2(dot(b0, d0), dot(b0, vec2(-d0.y, d0.x))) * bump_uv_scale;
	b1 = vec2(dot(b1, d1), dot(b1, vec2(-d1.y, d1.x))) * bump_uv_scale;
	w0 = vec2(dot(w0, d0), dot(w0, vec2(-d0.y, d0.x))) * wave_uv_scale;
	w1 = vec2(dot(w1, d1), dot(w1, vec2(-d1.y, d1.x))) * wave_uv_scale;
	vec3 n;
	vec3 col;
	vec3 eye_g = normalize(CAMERA_POSITION_WORLD - v_world_position);
	vec3 eye = vec3(eye_g.x, -eye_g.z, eye_g.y);
	vec3 half_vector = normalize(eye + light_direction);
	if (pca_mode != 0) {
		// Retail PCA / PCA_U pixel blobs 0 / 3, with interpolated geometric normal.
		// c0..c6 consume only two basis groups; texture 3 augments both in PCA_U.
		// See docs/WATER_PCA_NORMALS.md and tests/water_normal_test.gd.
		vec2 uv0 = vec2(dot(base, d0), dot(base, vec2(-d0.y, d0.x))) * pca_uv_scale.x;
		vec2 uv1 = vec2(dot(base, d1), dot(base, vec2(-d1.y, d1.x))) * pca_uv_scale.y;
		if (pca_mode == 1) uv1 = uv1.yx;
		vec4 bases03 = texture(pca_tex0, uv0) + texture(pca_tex0, uv1) - vec4(1.0);
		vec4 bases47 = texture(pca_tex1, uv0) + texture(pca_tex1, uv1) - vec4(1.0);
		if (pca_mode == 2) {
			bases03 += texture(pca_tex2, uv0) - vec4(0.5);
			bases47 += texture(pca_tex2, uv1) - vec4(0.5);
		}
		vec3 reconstructed = pca_coefficient(0).xyz
			+ vec3(dot(pca_coefficient(1), bases03), dot(pca_coefficient(2), bases03), dot(pca_coefficient(3), bases03))
			+ vec3(dot(pca_coefficient(4), bases47), dot(pca_coefficient(5), bases47), dot(pca_coefficient(6), bases47));
		vec3 combined = reconstructed * 4.0 + v_wave_normal - vec3(2.0);
		n = combined * inversesqrt(dot(combined, combined) + 0.00001 * 0.00001);
		if (pca_mode == 2) {
			vec4 noise = texture(pca_noise_tex, uv0 * 0.015 + vec2(0.3, 0.1));
			n = mix(n, vec3(0.0, 0.0, 1.0), clamp(noise.r * noise.a * 0.25, 0.0, 1.0));
		}
		float ndotl = max(dot(n, light_direction), 0.0);
		float ndoth = max(dot(n, half_vector), 0.0);
		vec3 env = texture(sky_tex, base * env_uv_scale + reflection_perturbation * n.xy).rgb;
		vec3 highlight = ndotl > 0.0 ? specular_color * pow(ndoth, specular_exponent) : vec3(0.0);
		col = light_color * (env * material_diffuse.rgb * (2.0 * dimming) * ndotl + highlight);
		// VS 13/14 baseline plus interpolated crest and shallow-water foam.
		vec2 perturbation = reflection_perturbation * n.xy;
		col += v_froth * texture(pca_froth_tex, v_world * pca_wave_uv_scale.x + perturbation).rgb
			* texture(pca_froth_tex, v_world * pca_wave_uv_scale.y + perturbation).rgb;
	} else {
		n = 2.0 * (texture(bump_tex, b0).rgb + texture(bump_tex, b1).rgb) - vec3(2.0, 2.0, 1.0);
		n.xy *= bump_scale;
		n = normalize(n);
		float lighting = max(dot(n, light_direction), 0.0) + pow(max(dot(n, half_vector), 0.0), specular_exponent);
		vec3 env = texture(sky_tex, base * env_uv_scale).rgb;
		vec3 waves = texture(wave_tex, w0).rgb * texture(wave_tex, w1).rgb;
		col = env * material_diffuse.rgb * dimming * (2.0 * light_color) * lighting + waves;
	}
	float alpha = material_diffuse.a;
	if (blend_terrain_alpha && max_alpha_depth > 0.0 && deep_water_alpha > 0.0) {
		vec2 grid = (v_world - terrain_origin) / 10.0;
		float ground = texture(terrain_height_tex, (grid + 0.5) / terrain_size).r;
		float depth = max(water_height - ground, 0.0);
		// VS 9: saturate(depth / (MaxAlphaDepth * DeepWaterAlpha^2)) * DeepWaterAlpha.
		alpha = clamp(depth / (max_alpha_depth * deep_water_alpha * deep_water_alpha), 0.0, 1.0) * deep_water_alpha;
	}

	if (wave_geometry_enabled && pca_mode != 0) alpha=clamp(v_depth*0.1,0.0,1.0)*material_diffuse.a;
	if (shroud_enabled) {
		col = col * shroud_color * texture(shroud_tex, v_shroud_uv).r; // lane ZOOM-1
	}
	ALBEDO = retail_output(col);
	ALPHA = clamp(alpha, 0.0, 1.0);
}
