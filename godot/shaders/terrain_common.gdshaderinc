// OpenBFME. GPL-3.0.
//
// Port of the RotWK 2.01 terrain shader, technique "Default" (shaders/compiled/terrain.fxo, vs_1_1 +
// ps_2_0; D3DX9 disassembly in the spec author's maps-scratch/fxo/terrain.fx.clean.txt, lines
// 5085-5456; maths documented in spec maps-and-terrain.md 3.4 and re-derived from that listing).
//
// Space: the mesh is in Godot's frame G = (x, z, -y) of SAGE world space (x east, y north, z up;
// spec 2.1) with an identity model matrix, so SAGE position = (G.x, -G.z, G.y). Every direction below is
// SAGE space.
//
// Colour space: retail evaluates AND blends in gamma space on raw texel values (D3D9, no sRGB conversion;
// spec 3.10). Godot blends in a linear framebuffer, so the three draw layers (base, blend, extra blend;
// TerrainComposite.h) are composited HERE, per pixel, in gamma space, and the result is converted to linear
// ONCE (terrain_output()), so that Godot's sRGB output encode reproduces the retail byte. A half-alpha
// white layer over a black one gives 0.5 in retail space, not the 0.73 a linear framebuffer blend gives.
//
// Per vertex (vs_1_1):  tangent frame T = (n.z, 0, -n.x), B = (0, n.z, -n.y) from the vertex normal;
//   Lts = (L.T, L.B, L.n); Hts = normalize((normalize(eye - pos) + L) in the same frame).
// Per pixel and layer (ps_2_0):
//   nm = texture(normal atlas); N = normalize(2*nm.xy-1, sqrt(1-x^2-y^2))
//   diff = max(N.Lts, 0); spec = (N.Lts>0 && N.Hts>0) ? (N.Hts)^1200 * nm.b * 0.9 : 0
//   sun = Overbright ? 0.5*SunColor : SunColor
//   lit = (COLOR.rgb + 0.5*LightningColor)*albedo + sun*(diff*albedo + spec); Overbright: lit *= 2
// Composite: lit = mix(mix(lit0, lit1, alpha1), lit2, alpha2) with each layer's UV and alpha interpolated as
// the GPU would have over that layer's own triangulation (interp_*), then
//   rgb = lerp(FogColor, lit*cloud*macro, fogF) * shroud;  output: the sRGB decode of rgb (retail_output)
//
// Cliff cells carry the class block their UVs must wrap into (ClassWrapRect): the UV of a cell that straddles
// the class edge is interpolated unwrapped and wrapped PER PIXEL (wrap_into_class), so no texel of another
// class is ever read; the mip level comes from the unwrapped UV (continuous derivatives).
//
// Not wired here (reported by the builder, stop S-031): the Taint mask path (albedo = base), the
// shadow-receiving variants and the shroud texture (shroud = 1).

uniform sampler2D base_atlas : filter_linear_mipmap, repeat_disable, hint_default_white;
uniform sampler2D normal_atlas : filter_linear_mipmap, repeat_disable, hint_default_black;
uniform sampler2D cloud_tex : filter_linear_mipmap, repeat_enable, hint_default_white;
uniform sampler2D macro_tex : filter_linear_mipmap, repeat_enable, hint_default_white;
// The per-cell layer records (TerrainComposite.h): RGBA32F, 9 texels per record, 512 records per row.
uniform sampler2D records : filter_nearest, repeat_disable, hint_default_black;

uniform vec2 atlas_size = vec2(2048.0, 2048.0);
// ZH builds 3 mip levels (MIP_LEVELS_3) so mips never bleed past the 4-px class gutters; the level of
// detail is computed here and clamped to the same range.
uniform float max_atlas_lod = 2.0;

uniform vec3 sun_dir_sage = vec3(0.0, 0.0, 1.0); // toward the light (see stop S-033: -lightPos)
uniform vec3 sun_color = vec3(1.0);
uniform bool overbright = true;
uniform vec3 lightning_color = vec3(0.0);

uniform float fog_enabled = 0.0;
uniform vec3 fog_color = vec3(0.5);
uniform float fog_range_start = 0.0;
uniform float fog_range_end = 1.0;

uniform vec2 cloud_scale = vec2(0.0015151515);        // 1 / CloudTextureSize (weather.ini 660)
uniform vec2 cloud_offset_per_second = vec2(-0.012, -0.018);
uniform bool macro_stretched = false;                 // EnvironmentData.isMacroTextureStretched
uniform vec2 map_size = vec2(1.0);                    // whole bordered map in world units (INFERRED)
uniform float map_border_width = 0.0;                 // border in world units (INFERRED)
uniform float map_cell_size = 10.0;
uniform float shroud = 1.0;
// VIS-1: the local player's shroud, one texel per shroud cell (L8 = the cell's GameData alpha: ClearAlpha / FogAlpha / ShroudAlpha, 0 opaque, 255 clear),
// over the world rectangle origin .. origin + extent (SAGE x, y); the colour is multiplied by ShroudColor * level (INFERENCE, stop S-567)
uniform sampler2D shroud_tex : filter_linear, repeat_disable, hint_default_white;
uniform vec2 shroud_origin = vec2(0.0);
uniform vec2 shroud_extent = vec2(1.0);
uniform vec3 shroud_color = vec3(1.0);
uniform bool shroud_enabled = false;
varying vec2 v_shroud_uv;
uniform float output_gamma = 2.2; // != 1: decode for Godot's linear output (retail_output)
// Debug views (0 = retail maths): 1 albedo, 2 vertex colour, 3 normal map, 4 uv, 5 lit without cloud/macro,
// 6 cloud*macro, 7 the gamma-space composite of the raw layer albedos converted once (numeric shader test)
uniform int debug_mode = 0;

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

varying vec3 v_lts;
varying vec3 v_hts;
varying float v_fog;
varying vec2 v_cloud;
varying vec2 v_macro;
varying vec2 v_local;        // cell-local coordinates (0..1) of the pixel
varying flat vec4 v_wrap;    // base layer class wrap rect (x0, y0, w, h); w == 0: none
varying flat float v_rec;    // record index of the cell's blend layers, or -1

// Varyings may only be assigned in vertex() itself, so the maths returns its results through out parameters.
void terrain_vertex(vec3 vtx, vec3 nrm, vec3 cam_world, float time, out vec3 lts, out vec3 hts, out float fog, out vec2 cloud, out vec2 macro_uv) {
	vec3 sp = vec3(vtx.x, -vtx.z, vtx.y);
	vec3 n = normalize(vec3(nrm.x, -nrm.z, nrm.y));
	vec3 eye = vec3(cam_world.x, -cam_world.z, cam_world.y);
	vec3 L = normalize(sun_dir_sage);
	vec3 T = vec3(n.z, 0.0, -n.x);
	vec3 B = vec3(0.0, n.z, -n.y);
	lts = vec3(dot(L, T), dot(L, B), dot(L, n));
	vec3 h = normalize(eye - sp) + L;
	hts = normalize(vec3(dot(h, T), dot(h, B), dot(h, n)));

	float dist = length(eye - sp);
	fog = 1.0 - fog_enabled * clamp((dist - fog_range_start) / max(fog_range_end - fog_range_start, 0.0001), 0.0, 1.0);

	cloud = sp.xy * cloud_scale + fract(time * cloud_offset_per_second);
	vec2 macro_tiled = vec2(sp.x, -sp.y) / (66.0 * map_cell_size);
	vec2 macro_stretch = (sp.xy + vec2(map_border_width)) * vec2(1.0 / map_size.x, -1.0 / map_size.y);
	macro_uv = macro_stretched ? macro_stretch : macro_tiled;
}

float atlas_lod(vec2 uv) {
	vec2 p = uv * atlas_size;
	vec2 dx = dFdx(p);
	vec2 dy = dFdy(p);
	float d = max(dot(dx, dx), dot(dy, dy));
	return clamp(0.5 * log2(max(d, 0.00000001)), 0.0, max_atlas_lod);
}

// Reference: TerrainUv::wrapIntoClass (WorldHeightMapUV.cpp). GLSL mod() takes the sign of the divisor.
vec2 wrap_into_class(vec4 rect, vec2 uv) {
	if (rect.z <= 0.0) {
		return uv;
	}
	return rect.xy + mod(uv - rect.xy, rect.zw);
}

// Reference: TerrainComposite::interpolateCorner. c0..c3 = SW, SE, NE, NW; p = cell-local point. flip: diagonal
// SE-NW instead of SW-NE (ZH's two triangles; see TerrainComposite.h).
vec2 interp_vec2(vec2 c0, vec2 c1, vec2 c2, vec2 c3, bool flip, vec2 p) {
	vec2 un_lo = c0 + p.x * (c1 - c0) + p.y * (c2 - c1);
	vec2 un_hi = c0 + p.y * (c3 - c0) + p.x * (c2 - c3);
	vec2 fl_lo = c0 + p.x * (c1 - c0) + p.y * (c3 - c0);
	vec2 fl_hi = c2 + (1.0 - p.y) * (c1 - c2) + (1.0 - p.x) * (c3 - c2);
	vec2 unflipped = (p.y <= p.x) ? un_lo : un_hi;
	vec2 flipped = (p.x + p.y <= 1.0) ? fl_lo : fl_hi;
	return flip ? flipped : unflipped;
}

float interp_float(vec4 c, bool flip, vec2 p) {
	float un_lo = c.x + p.x * (c.y - c.x) + p.y * (c.z - c.y);
	float un_hi = c.x + p.y * (c.w - c.x) + p.x * (c.z - c.w);
	float fl_lo = c.x + p.x * (c.y - c.x) + p.y * (c.w - c.x);
	float fl_hi = c.z + (1.0 - p.y) * (c.y - c.z) + (1.0 - p.x) * (c.w - c.z);
	float unflipped = (p.y <= p.x) ? un_lo : un_hi;
	float flipped = (p.x + p.y <= 1.0) ? fl_lo : fl_hi;
	return flip ? flipped : unflipped;
}

vec4 record_texel(int rec, int k) {
	int row = rec / 512;
	int col = rec - row * 512;
	return texelFetch(records, ivec2(col * 9 + k, row), 0);
}

// One layer's lit colour in retail (gamma) space, before cloud / macro / fog.
vec3 shade_layer(vec2 uv_unwrapped, vec4 rect, vec4 vcol, float lod) {
	vec2 uv = wrap_into_class(rect, uv_unwrapped);
	vec3 albedo = textureLod(base_atlas, uv, lod).rgb;
	if (debug_mode == 7) {
		return albedo;
	}
	vec4 nm = textureLod(normal_atlas, uv, lod);
	vec2 nxy = 2.0 * nm.xy - 1.0;
	float nz = sqrt(max(1.0 - dot(nxy, nxy), 0.0));
	vec3 N = normalize(vec3(nxy, nz));
	vec3 Lt = normalize(v_lts);
	vec3 Ht = normalize(v_hts);
	float NdL = dot(N, Lt);
	float NdH = dot(N, Ht);
	float diff = NdL > 0.0 ? NdL : 0.0;
	float spec = (NdL > 0.0 && NdH > 0.0) ? pow(NdH, 1200.0) * nm.b * 0.9 : 0.0;
	vec3 sun = overbright ? 0.5 * sun_color : sun_color;
	vec3 lit = (vcol.rgb + 0.5 * lightning_color) * albedo + sun * (diff * albedo + spec);
	if (overbright) {
		lit = 2.0 * lit;
	}
	if (debug_mode == 1) { return albedo; }
	if (debug_mode == 2) { return vcol.rgb; }
	if (debug_mode == 3) { return nm.rgb; }
	return lit;
}

vec3 terrain_output(vec2 uv0, vec4 vcol) {
	vec2 p = v_local;
	bool has_rec = v_rec > -0.5;
	int rec = has_rec ? int(v_rec + 0.5) : 0;
	// the record is fetched unconditionally so that every UV below (and its derivatives) is computed in
	// uniform control flow; a cell without a record never uses these values
	vec4 flags = record_texel(rec, 8);
	bool has1 = has_rec && flags.z > 0.5;
	bool has2 = has_rec && flags.w > 0.5;
	vec4 a1 = record_texel(rec, 0);
	vec4 a2 = record_texel(rec, 1);
	vec2 uv1 = interp_vec2(a1.xy, a1.zw, a2.xy, a2.zw, flags.x > 0.5, p);
	float alpha1 = interp_float(record_texel(rec, 2), flags.x > 0.5, p);
	vec4 b1 = record_texel(rec, 4);
	vec4 b2 = record_texel(rec, 5);
	vec2 uv2 = interp_vec2(b1.xy, b1.zw, b2.xy, b2.zw, flags.y > 0.5, p);
	float alpha2 = interp_float(record_texel(rec, 6), flags.y > 0.5, p);
	vec4 rect1 = record_texel(rec, 3);
	vec4 rect2 = record_texel(rec, 7);

	float lod0 = atlas_lod(uv0);
	float lod1 = atlas_lod(uv1);
	float lod2 = atlas_lod(uv2);

	vec3 comp = shade_layer(uv0, v_wrap, vcol, lod0);
	if (has1) {
		comp = mix(comp, shade_layer(uv1, rect1, vcol, lod1), alpha1);
	}
	if (has2) {
		comp = mix(comp, shade_layer(uv2, rect2, vcol, lod2), alpha2);
	}
	if (debug_mode == 4) { return vec3(fract(uv0 * 16.0), 0.0); }
	if (debug_mode == 7) { return retail_output(clamp(comp, 0.0, 1.0)); }
	if (debug_mode == 1 || debug_mode == 2 || debug_mode == 3) { return retail_output(clamp(comp, 0.0, 1.0)); }
	if (debug_mode == 5) { return retail_output(clamp(comp, 0.0, 1.0)); }
	vec3 cloud = texture(cloud_tex, v_cloud).rgb;
	vec3 macro = texture(macro_tex, v_macro).rgb;
	if (debug_mode == 6) { return retail_output(cloud * macro); }
	vec3 col = mix(fog_color, comp * cloud * macro, v_fog);
	col = col * shroud;
	if (shroud_enabled) {
		col = col * shroud_color * texture(shroud_tex, v_shroud_uv).r;
	}
	return retail_output(col);
}
