// OpenBFME. GPL-3.0.
//
// Road segment ribbon, drawn with the terrain's own effect. Retail (BFME2 W3DRoadBufferDrawRoads.cpp:349-365; RW 0x4d73c4 .. 0x4d7458)
// calls g_00DEBC60->setRenderingMode(2), binds the road texture and its _nrm companion (or the 1x1 fallback, see below) and draws the
// segments through the terrain's rendering method (FlatHeightMap.cpp:816, m_method) under the terrain's light environment, which holds the
// sun (FlatHeightMap.cpp:768-791). That is terrain.fxo technique "Default" (D3DX9 disassembly with tools/render/d3d9_disasm.py):
//   vertex shader  = the single vs_1_1 blob 60 @0x1c780 (the same one the terrain uses: terrain_common.gdshaderinc terrain_vertex());
//   pixel shader   = PSDefault_Array[min(RenderingMode, 4) + 5 * min(NumShadows, 1) + 10 * IsResourceTextureEnabled] (the index
//                    preshader at 0x1c4bc); mode 2, no shadows, no resource texture is element 2 = ps_2_0 blob 37 @0xc160.
// Element 2 differs from the terrain's own element 0 (blob 35) in three places, all ported here:
//   * the normal is 2 * texel.rgb - 1 normalised, WITHOUT rebuilding z from xy (element 0 does);
//   * the specular mask is the normal map's ALPHA (element 0 uses its blue);
//   * the output alpha is base.a * vertex.a (element 0 takes the vertex alpha only), and there is no taint.
// Per pixel (blob 37):
//   N = normalize(2 * nm.rgb - 1); diff = max(N.Lts, 0); spec = (N.Lts > 0 && N.Hts > 0) ? (N.Hts)^1200 * nm.a * 0.9 : 0
//   sun = Overbright ? 0.5 * SunColor : SunColor
//   lit = (COLOR.rgb + 0.5 * LightningColor) * albedo + sun * (diff * albedo + spec); Overbright: lit *= 2
//   rgb = lerp(FogColor, lit * cloud * macro, fogF) * shroud.r;  alpha = base.a * COLOR.a
// COLOR is the segment's static vertex diffuse (ambient + the accent lights, no sun: W3DRoadSegmentUpdateLighting.cpp getStaticDiffuse(x, y, true)).
// The vertex normal is (0, 0, 1) in SAGE space (loadFloat4PtSection stores nz = 1.0f: BFME2 W3DRoadBuffer.cpp:948, RW 0x4d7d32 .. 0x4d7d3c),
// so the tangent frame is world-aligned, whatever the road's direction, exactly as in retail; the mesh carries it as ARRAY_NORMAL.
// Samplers: BaseSamplerWrapped / NormalSamplerWrapped are wrapped, MIN anisotropic (8), MAG / MIP linear (fxo sampler states 170 / 169 / 171 /
// 174); the cloud and macro samplers are wrapped linear (terrain_common.gdshaderinc).
shader_type spatial;
render_mode ambient_light_disabled, specular_disabled, blend_mix, depth_draw_never, cull_disabled;

#include "res://shaders/object_shadow.gdshaderinc"

#include "res://shaders/terrain_common.gdshaderinc"

uniform sampler2D road_tex : filter_linear_mipmap_anisotropic, repeat_enable, hint_default_white;
uniform sampler2D road_normal_tex : filter_linear_mipmap_anisotropic, repeat_enable, hint_default_black;

void vertex() {
	vec3 lts;
	vec3 hts;
	float fog;
	vec2 cloud;
	vec2 macro_uv;
	terrain_vertex(VERTEX, NORMAL, CAMERA_POSITION_WORLD, TIME, lts, hts, fog, cloud, macro_uv);
	v_lts = lts;
	v_hts = hts;
	v_fog = fog;
	v_cloud = cloud;
	v_macro = macro_uv;
	vec3 shroud_world = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
	v_shroud_uv = (vec2(shroud_world.x, -shroud_world.z) - shroud_origin) / shroud_extent;
}

// ps_2_0 blob 37 of terrain.fxo (PSDefault_Array element 2): the lit road colour before cloud / macro / fog, in retail (gamma) space.
vec3 road_lit(vec3 albedo, vec4 nm, vec4 vcol) {
	vec3 N = normalize(2.0 * nm.rgb - 1.0);
	vec3 Lt = normalize(v_lts);
	vec3 Ht = normalize(v_hts);
	float NdL = dot(N, Lt);
	float NdH = dot(N, Ht);
	float diff = NdL > 0.0 ? NdL : 0.0;
	float spec = (NdL > 0.0 && NdH > 0.0) ? pow(NdH, 1200.0) * nm.a * 0.9 : 0.0;
	vec3 sun = overbright ? 0.5 * sun_color : sun_color;
	vec3 lit = (vcol.rgb + 0.5 * lightning_color) * albedo + sun * (diff * albedo + spec);
	if (overbright) {
		lit = 2.0 * lit;
	}
	return lit;
}

void fragment() {
	vec4 base = texture(road_tex, UV);
	vec4 nm = texture(road_normal_tex, UV);
	vec3 lit = road_lit(base.rgb, nm, COLOR);
	vec3 cloud = texture(cloud_tex, v_cloud).rgb;
	vec3 macro = texture(macro_tex, v_macro).rgb;
	vec3 col = mix(fog_color, lit * cloud * macro, v_fog);
	if (shroud_enabled) {
		col = col * shroud_color * texture(shroud_tex, v_shroud_uv).r; // lane ZOOM-1 (S-567), as the terrain
	}
	ALBEDO = retail_output(col);
	ALPHA = base.a * COLOR.a;
	v_shadow_color = col;
	EMISSION = ALBEDO * (1.0 - w3d_shadow.w);
	ALBEDO = vec3(w3d_shadow.w);
}

void light() {
	if (LIGHT_IS_DIRECTIONAL) {
		DIFFUSE_LIGHT += retail_output(v_shadow_color * mix(w3d_shadow.rgb, vec3(1.0), ATTENUATION));
	}
}
