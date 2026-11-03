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
// class is ever read; the filter footprint comes from the unwrapped UV (continuous derivatives).
//
// Filtering (lane TERRAIN-SAMPLING): the Base / Normal samplers are 8x anisotropic in retail (terrain.fxo); see atlas_gradients(). The shroud texel is the 4-bit red
// nibble of W3DShroud's A4R4G4B4 pixel (RW 0x47266B; engine ShroudTexel.h), sampled bilinearly with clamp (ShroudSampler).
//
// Not wired here (reported by the builder, stop S-031): the Taint mask path (albedo = base) and the shadow-receiving variants.

// lane TERRAIN-SAMPLING: retail's BaseSampler* and NormalSampler* (terrain.fxo, Clamped and Wrapped) are MinFilter ANISOTROPIC (3), MagFilter and MipFilter LINEAR (2),
// MaxAnisotropy 8; only the _L (low detail) samplers are linear. The anisotropic hint makes the hardware filter along the footprint's major axis; the level it
// takes is the viewport's (the OpenBFMEAnisotropic option), and atlas_gradients() caps the footprint's ratio at retail's 8 (stop S-031).
uniform sampler2D base_atlas : filter_linear_mipmap_anisotropic, repeat_disable, hint_default_white;
uniform sampler2D normal_atlas : filter_linear_mipmap_anisotropic, repeat_disable, hint_default_black;
uniform sampler2D cloud_tex : filter_linear_mipmap, repeat_enable, hint_default_white;
uniform sampler2D macro_tex : filter_linear_mipmap, repeat_enable, hint_default_white;
// The per-cell layer records (TerrainComposite.h): RGBA32F, 9 texels per record, 512 records per row.
uniform sampler2D records : filter_nearest, repeat_disable, hint_default_black;

uniform vec2 atlas_size = vec2(2048.0, 2048.0);
// ZH builds 3 mip levels (MIP_LEVELS_3) so mips never bleed past the 4-px class gutters; the level of
// detail is clamped to the same range (atlas_gradients).
uniform float max_atlas_lod = 2.0;
// terrain.fxo MaxAnisotropy of the Base / Normal samplers
uniform float max_anisotropy = 8.0;

uniform vec3 sun_dir_sage = vec3(0.0, 0.0, 1.0); // toward the light (see stop S-033: -lightPos)
uniform vec3 sun_color = vec3(1.0);
uniform bool overbright = true;
uniform vec3 lightning_color = vec3(0.0);
// lane WEATHER-1: the cloud effect's darkening of the terrain vertex lighting (RotWK doTheLight RW 0x468CC0 multiplies every global light's diffuse
// by 1 - darkening while TheCloudEffect is active; the terrain is relit every 10th client frame of a darkening / lightening phase). With
// weather_relit the vertex colour is lit again here from the vertex normal, as HeightMapMesh::vertexLight bakes it (terrain[0] ambient + the
// accent lights 1 and 2, stop S-033), with the lights' diffuse times 1 - weather_terrain_dark, clamped and truncated to the byte; without it the
// baked COLOR is used unchanged.
uniform bool weather_relit = false;
uniform vec3 weather_terrain_dark = vec3(0.0);
uniform vec3 weather_terrain_ambient = vec3(0.0);
uniform vec3 weather_light1_to = vec3(0.0, 0.0, 1.0);
uniform vec3 weather_light1_diffuse = vec3(0.0);
uniform vec3 weather_light2_to = vec3(0.0, 0.0, 1.0);
uniform vec3 weather_light2_diffuse = vec3(0.0);

uniform float fog_enabled = 0.0;
uniform vec3 fog_color = vec3(0.5);
uniform float fog_range_start = 0.0;
global uniform float w3d_fog_shift; // lane PLAY-1: the free camera's extra eye distance (TacticalCamera::fogShift; 0 = retail)
uniform float fog_range_end = 1.0;

uniform vec2 cloud_scale = vec2(0.0015151515);        // 1 / CloudTextureSize (weather.ini 660)
uniform vec2 cloud_offset_per_second = vec2(-0.012, -0.018);
uniform bool macro_stretched = false;                 // EnvironmentData.isMacroTextureStretched
uniform vec2 map_size = vec2(1.0);                    // whole bordered map in world units (INFERRED)
uniform float map_border_width = 0.0;                 // border in world units (INFERRED)
uniform float map_cell_size = 10.0;
uniform float shroud = 1.0;
// VIS-1: the local player's shroud, one texel per shroud cell (L8 = the red nibble times 17 of the cell's A4R4G4B4 texel, W3DShroud RW 0x47266B: GameData ClearAlpha / FogAlpha / ShroudAlpha, 0 opaque, 255 clear, x ShroudColor, kept to 4 bits),
// over the world rectangle origin .. origin + extent (SAGE x, y); the colour is multiplied by that texel (shroud_color stays white: ShroudColor is inside the texel; stop S-567)
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
// lane WEATHERFX-1: the SAGE world point, vertex normal and camera of the Enhanced look's weather (weather_fx.gdshaderinc)
varying vec3 v_wfx_pos;
varying vec3 v_wfx_nrm;
varying vec3 v_wfx_eye;

#include "res://shaders/weather_fx.gdshaderinc"

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

	float dist = length(eye - sp) - w3d_fog_shift;
	fog = 1.0 - fog_enabled * clamp((dist - fog_range_start) / max(fog_range_end - fog_range_start, 0.0001), 0.0, 1.0);

	cloud = sp.xy * cloud_scale + fract(time * cloud_offset_per_second);
	vec2 macro_tiled = vec2(sp.x, -sp.y) / (66.0 * map_cell_size);
	vec2 macro_stretch = (sp.xy + vec2(map_border_width)) * vec2(1.0 / map_size.x, -1.0 / map_size.y);
	macro_uv = macro_stretched ? macro_stretch : macro_tiled;
}

// lane WEATHER-1: the relit vertex colour (see weather_relit); `nrm` is the vertex normal in Godot's frame
vec4 weather_vertex_color(vec3 nrm, vec4 baked) {
	vec3 n = vec3(nrm.x, -nrm.z, nrm.y);
	vec3 k = vec3(1.0) - weather_terrain_dark;
	vec3 c = weather_terrain_ambient;
	c += clamp(dot(weather_light1_to, n), 0.0, 1.0) * weather_light1_diffuse * k;
	c += clamp(dot(weather_light2_to, n), 0.0, 1.0) * weather_light2_diffuse * k;
	c = floor(clamp(c, 0.0, 1.0) * 255.0) / 255.0;
	return vec4(c, baked.a);
}

// The explicit gradients (in UV units) the anisotropic sample takes. The footprint is the one the screen-space derivatives of the unwrapped UV give; the
// hardware reads N = min(ceil(major / minor), level) texels along the major axis at LOD log2(major / N) (Vulkan 16.9 / D3D9's anisotropic filter). Retail's
// MaxAnisotropy is 8: a footprint thinner than 1:8 has its minor axis lengthened to major / 8, so a viewport level of 16 reads no more than retail's 8 probes.
// ZH's three mip levels (max_atlas_lod) bound the LOD: a footprint that would take a deeper one is shrunk to it, because a deeper mip bleeds past the class gutters.
void atlas_gradients(vec2 uv, out vec2 gx, out vec2 gy) {
	vec2 dx = dFdx(uv) * atlas_size;
	vec2 dy = dFdy(uv) * atlas_size;
	float lx = length(dx);
	float ly = length(dy);
	float major = max(lx, ly);
	float minor = min(lx, ly);
	if (minor * max_anisotropy < major) {
		float grow = major / (max_anisotropy * max(minor, 0.00001));
		dx = (lx < ly) ? dx * grow : dx;
		dy = (lx < ly) ? dy : dy * grow;
		minor = major / max_anisotropy;
	}
	float probes = min(ceil(major / max(minor, 0.00001)), max_anisotropy);
	float lod = log2(max(major / probes, 0.00000001));
	float shrink = lod > max_atlas_lod ? exp2(max_atlas_lod - lod) : 1.0;
	gx = dx * shrink / atlas_size;
	gy = dy * shrink / atlas_size;
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
vec3 shade_layer(vec2 uv_unwrapped, vec4 rect, vec4 vcol, vec2 gx, vec2 gy) {
	vec2 uv = wrap_into_class(rect, uv_unwrapped);
	vec3 albedo = textureGrad(base_atlas, uv, gx, gy).rgb;
	if (debug_mode == 7) {
		return albedo;
	}
	vec4 nm = textureGrad(normal_atlas, uv, gx, gy);
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

	vec2 g0x, g0y, g1x, g1y, g2x, g2y;
	atlas_gradients(uv0, g0x, g0y);
	atlas_gradients(uv1, g1x, g1y);
	atlas_gradients(uv2, g2x, g2y);

	vec3 comp = shade_layer(uv0, v_wrap, vcol, g0x, g0y);
	if (has1) {
		comp = mix(comp, shade_layer(uv1, rect1, vcol, g1x, g1y), alpha1);
	}
	if (has2) {
		comp = mix(comp, shade_layer(uv2, rect2, vcol, g2x, g2y), alpha2);
	}
	if (debug_mode == 4) { return vec3(fract(uv0 * 16.0), 0.0); }
	if (debug_mode == 7) { return retail_output(clamp(comp, 0.0, 1.0)); }
	if (debug_mode == 1 || debug_mode == 2 || debug_mode == 3) { return retail_output(clamp(comp, 0.0, 1.0)); }
	if (debug_mode == 5) { return retail_output(clamp(comp, 0.0, 1.0)); }
	if (wfx_enabled > 0.5) {
		// lane WEATHERFX-1: the Enhanced look's weather on the ground (the Original look never runs this: wfx_enabled stays 0)
		comp = wfx_terrain(comp, v_wfx_pos, normalize(v_wfx_nrm), normalize(v_wfx_eye - v_wfx_pos), normalize(sun_dir_sage),
			weather_relit ? weather_terrain_dark : vec3(0.0));
	}
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
