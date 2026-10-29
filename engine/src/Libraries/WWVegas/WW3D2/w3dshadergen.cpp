// OpenBFME. GPL-3.0. See w3dshadergen.h.

#include "Libraries/WWVegas/WW3D2/w3dshadergen.h"
#include "GameEngineDevice/W3DDevice/GameClient/W3DHardwareFog.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace
{

// Expression of ZH shader.cpp:690-910 stage 1 colour ops: l = current (stage 0 result), o = this stage's texture.
// D3D operands: Arg1 = TEXTURE, Arg2 = CURRENT unless a case says otherwise (SUBR, MODALPHAADDCOLOR).
std::string detailColorExpr(int func)
{
	switch (func)
	{
	case W3D_DETAILCOLOR_DETAIL: return "o.rgb";
	case W3D_DETAILCOLOR_SCALE: return "l.rgb * o.rgb";
	case W3D_DETAILCOLOR_INVSCALE: return "o.rgb + l.rgb * (vec3(1.0) - o.rgb)"; // D3DTOP_ADDSMOOTH
	case W3D_DETAILCOLOR_ADD: return "o.rgb + l.rgb";
	case W3D_DETAILCOLOR_SUB: return "o.rgb - l.rgb";   // D3DTOP_SUBTRACT, Arg1 = TEXTURE (the code, not the enum's comment)
	case W3D_DETAILCOLOR_SUBR: return "l.rgb - o.rgb";  // Arg1 = CURRENT
	case W3D_DETAILCOLOR_BLEND: return "o.rgb * o.a + l.rgb * (1.0 - o.a)"; // D3DTOP_BLENDTEXTUREALPHA
	case W3D_DETAILCOLOR_DETAILBLEND: return "o.rgb * l.a + l.rgb * (1.0 - l.a)"; // D3DTOP_BLENDCURRENTALPHA
	case W3D_DETAILCOLOR_ADDSIGNED: return "o.rgb + l.rgb - vec3(0.5)";
	case W3D_DETAILCOLOR_ADDSIGNED2X: return "(o.rgb + l.rgb - vec3(0.5)) * 2.0";
	case W3D_DETAILCOLOR_SCALE2X: return "l.rgb * o.rgb * 2.0";
	case W3D_DETAILCOLOR_MODALPHAADDCOLOR: return "l.rgb + l.a * o.rgb"; // D3DTOP_MODULATEALPHA_ADDCOLOR, Arg1 = CURRENT
	}
	return "l.rgb";
}

std::string detailAlphaExpr(int func)
{
	switch (func)
	{
	case W3D_DETAILALPHA_DETAIL: return "o.a";
	case W3D_DETAILALPHA_SCALE: return "l.a * o.a";
	case W3D_DETAILALPHA_INVSCALE: return "o.a + l.a * (1.0 - o.a)";
	}
	return "l.a";
}

std::string fmt(float v)
{
	char buf[32];
	std::snprintf(buf, sizeof(buf), "%.6f", (double)v);
	return buf;
}

} // namespace

float W3D_Lit_Specular(float ndl, float ndh, float power)
{
	const float p = std::max(-W3D_LIT_POWER_LIMIT, std::min(W3D_LIT_POWER_LIMIT, power));
	return (ndl > 0.0f && ndh > 0.0f) ? std::pow(ndh, p) : 0.0f;
}

float W3D_NormalMapped_Specular(const float nt[3], const float lt[3], const float ht[3], float exponent)
{
	const float ndl = nt[0] * lt[0] + nt[1] * lt[1] + nt[2] * lt[2];
	const float ndh = nt[0] * ht[0] + nt[1] * ht[1] + nt[2] * ht[2];
	return (ndh > 0.0f && ndl > 0.0f) ? std::pow(ndh, exponent) : 0.0f;
}

void W3D_FixedFunction_House_Color(const float t0[4], const float diffuse[3], float diffuseAlpha, const float specular[3], bool secondaryGradient,
	const float hc[4], float rgb[3], float *alpha)
{
	float a = t0[3] * diffuseAlpha;
	for (int i = 0; i < 3; ++i)
	{
		const float lit = t0[i] * diffuse[i] * 2.0f;
		rgb[i] = lit + (hc[i] - lit) * hc[3];
	}
	if (secondaryGradient)
	{
		for (int i = 0; i < 3; ++i)
		{
			const float spec = std::min(specular[i] * 2.0f, 1.0f);
			rgb[i] += a * spec; // stage 2: MODULATEALPHA_ADDCOLOR(CURRENT, SPECULAR)
			rgb[i] += spec;     // D3DRS_SPECULARENABLE
		}
	}
	*alpha = a;
}

std::string W3D_FixedFunction_House_Color_Stop(bool converted, bool lit, int secGradient)
{
	if (converted || !lit || secGradient == 0)
	{
		return std::string();
	}
	return "[S-6023] fixed-function house colour with the secondary gradient set: the specular is added after the BLENDTEXTUREALPHA blend twice (stage 2 "
		"MODULATEALPHA_ADDCOLOR(CURRENT, SPECULAR) scaled by the current alpha, then D3DRS_SPECULARENABLE), as RW 0x53764F-0x5376A4 / 0x537AE5 set up; the caps "
		"(Max_Textures_Per_Pass > 2, ModAlphaAddClr) are assumed set and D3D9's fixed-function specular rules are taken from the runtime's documentation";
}

void W3D_SimpleFx_Uv_Matrix(const float transform[4], float timeSeconds, float row0[4], float row1[4])
{
	row0[0] = transform[0];
	row0[1] = 0.0f;
	row0[2] = timeSeconds * transform[2];
	row0[3] = 0.0f;
	row1[0] = 0.0f;
	row1[1] = transform[1];
	row1[2] = timeSeconds * transform[3];
	row1[3] = 0.0f;
}

std::string W3DShaderKey::Id() const
{
	char buf[256];
	std::snprintf(buf, sizeof(buf), "s%d%d%d_c%d_b%d_dw%d%d_at%d%d_u%d_t%d_g%d_dc%d_da%d_dcg%d_fx%d%d_tx%d%d_uv2%d_gen%d%d_vd%d%d_pj%d%d_cl%d%d%d%d_nl%d%d_tg%d", Skin, Dual, CullDisabled, Camera,
		Blend, DepthWrite, DepthTestDisabled, AlphaTest, AlphaTestLessEqual, Unshaded, Texturing, PriGradient, DetailColor, DetailAlpha, HasDCG, Fx,
		NormalMap, Tex[0], Tex[1], Uv2, TexGen[0], TexGen[1], ViewDependent[0], ViewDependent[1], Projection[0], Projection[1], ClampU[0], ClampV[0],
		ClampU[1], ClampV[1], NoLod[0], NoLod[1], Tangents);
	std::string id = buf;
	if (HouseColor) id += "_hc1"; // appended only when set: every existing id stays what it was
	if (HouseColorBaked) id += "_hcb"; // lane CAH-2
	if (SimpleFx) id += "_sfx1";
	if (ConvertedFx) id += "_cfx1"; // retail's DefaultW3D.fx replacement of a classic material (w3dfxconvert.h)
	if (SecGradient && HouseColor) id += "_sg" + std::to_string(SecGradient); // D3DRS_SPECULARENABLE: only the house colour combine reads it
	if (Fog) id += "_fog" + std::to_string(Fog); // lane RENDER-4
	if (AlphaOverride) id += "_ao"; // lane BUILD-5 r3
	if (OcclusionPass) id += "_occl" + std::to_string(OcclusionPass); // lane OCCLUDE-1
	return id;
}

std::string Generate_W3D_Shader_Code(const W3DShaderKey &k, W3DSamplerPolicy samplers)
{
	std::string c;
	auto line = [&](const std::string &s) { c += s; c += "\n"; };
	// Godot 4.7.2 Forward+: shared anisotropic-repeat uses Metal slot 17, even with one texture.
	// Clamp's anisotropic sampler is slot 11 and remains usable; linear-mipmap-repeat is slot 15.
	const std::string repeatMipFilter = samplers == W3DSamplerPolicy::Metal16 ? "filter_linear_mipmap" : "filter_linear_mipmap_anisotropic";

	line("shader_type spatial;");
	// ---- render modes ----
	std::string modes;
	auto mode = [&](const char *m) { modes += modes.empty() ? "" : ", "; modes += m; };
	// lane BUILD-5 r3: an opaque surface of a translucent drawable (AlphaOverride): ZH's theAlphaShader, SRC_ALPHA / ONE_MINUS_SRC_ALPHA with depth writes kept
	const bool alphaOverride = k.AlphaOverride && k.Blend == W3D_GODOT_BLEND_OPAQUE;
	switch ((W3DGodotBlend)k.Blend)
	{
	case W3D_GODOT_BLEND_OPAQUE:
		if (alphaOverride)
		{
			mode("blend_mix");
			mode(k.DepthWrite ? "depth_draw_always" : "depth_draw_never");
		}
		break;
	case W3D_GODOT_BLEND_MIX: case W3D_GODOT_BLEND_NONE: mode("blend_mix"); break;
	case W3D_GODOT_BLEND_ADD: case W3D_GODOT_BLEND_ADD_ALPHA: case W3D_GODOT_BLEND_APPROX_ADD: mode("blend_add"); break;
	case W3D_GODOT_BLEND_MUL: mode("blend_mul"); break;
	case W3D_GODOT_BLEND_PREMUL: mode("blend_premul_alpha"); break;
	}
	if (k.Blend != W3D_GODOT_BLEND_OPAQUE)
	{
		mode(k.DepthWrite ? "depth_draw_always" : "depth_draw_never");
	}
	else if (!k.DepthWrite)
	{
		mode("depth_draw_never");
	}
	if (k.DepthTestDisabled) mode("depth_test_disabled");
	mode(k.CullDisabled ? "cull_disabled" : "cull_back");
	// RENDER-1: every W3D surface is drawn unshaded: the lighting of the retail effects (defaultw3d.fxo for classic materials,
	// normalmapped.fxo for FX materials) is computed here from the map's light environment (W3DObjectLighting, globals below), in
	// the gamma space D3D9 works in, and converted to linear once at the end (Godot sRGB-encodes the output again).
	mode("unshaded");
	mode("shadows_disabled");
	if (k.OcclusionPass)
	{
		// lane OCCLUDE-1: drawn in the transparent pass after the opaque scene; writes no depth
		modes = k.OcclusionPass == W3D_OCCLUSION_PASS_COLOR ? "blend_mix, depth_draw_never, depth_test_inverted" : "blend_mix, depth_draw_never";
		mode(k.CullDisabled ? "cull_disabled" : "cull_back");
		mode("unshaded");
		mode("shadows_disabled");
	}
	line("render_mode " + modes + ";");
	if (k.OcclusionPass == W3D_OCCLUSION_PASS_MARK)
	{
		// retail: the occluders write bit 0x80 where they pass the depth test (RW 0x47061D .. 0x4706F0)
		line("stencil_mode write, compare_always, " + std::to_string(W3D_OCCLUSION_STENCIL_MARK) + ";");
	}
	else if (k.OcclusionPass == W3D_OCCLUSION_PASS_COLOR)
	{
		// passes where DONE < stencil (the mark) and replaces it with DONE: one colour per pixel, as retail's one quad per colour (RW 0x46E411: stencil EQUAL)
		line("stencil_mode read, write, compare_less, " + std::to_string(W3D_OCCLUSION_STENCIL_DONE) + ";");
	}

	// ---- uniforms ----
	line("uniform sampler2D w3d_palette : filter_nearest, repeat_disable;");
	// material colours: classic = the vertex material (defaultw3d.fxo ColorDiffuse + Opacity, ColorAmbient, ColorEmissive,
	// ColorSpecular, Shininess); FX = normalmapped.fxo DiffuseColor, AmbientColor, SpecularColor, SpecularExponent, BumpScale
	line("uniform vec4 w3d_diffuse = vec4(1.0);");
	line("uniform vec3 w3d_ambient_mat = vec3(1.0);");
	line("uniform vec3 w3d_emissive = vec3(0.0);");
	line("uniform vec3 w3d_spec_color = vec3(0.0);");
	line("uniform float w3d_shininess = 1.0;");
	line("uniform float w3d_alpha_ref = 0.376;");
	line("uniform float w3d_bump = 1.0;");
	// the light environment (W3DObjectLighting: Sas.AmbientLight[0].Color, Sas.DirectionalLight[0..2] Color / Direction, already
	// doubled when the map asks for it); columns of the matrices = lights 0..2, directions toward the light in Godot world space
	// lane BUILD-5 r2: Drawable::colorTint (RW 0x6757BB; DONOR ZH W3DScene.cpp:662-698, RTS3DScene::Customized_Render): the drawable's tint colour is added to every
	// global light's diffuse and to the equivalent ambient while it is lit. Per node (a W3DInstancer per tinted drawable: the placement ghost), 0 elsewhere
	line("instance uniform vec3 w3d_tint = vec3(0.0);");
	line("global uniform vec3 w3d_amb_obj;");
	line("global uniform mat3 w3d_lcol_obj;");
	line("global uniform mat3 w3d_ldir_obj;");
	line("global uniform vec3 w3d_amb_inf;");
	line("global uniform mat3 w3d_lcol_inf;");
	line("global uniform mat3 w3d_ldir_inf;");
	line("global uniform float w3d_fogged_frac;"); // lane VISUAL-2: RW 0x46DFD0 FogAlpha / ClearAlpha
	if (k.OcclusionPass == W3D_OCCLUSION_PASS_COLOR)
	{
		// lane OCCLUDE-1: the players' occluded colours by slot (0xAARRGGBB as RGBA8 texels, RTS3DSceneOcclusion::occludedColor) and the switch
		// (GameData UseBehindBuildingMarker and TheGameLogic + 0x99)
		line("global uniform sampler2D w3d_occl_colors;");
		line("global uniform float w3d_occl_on;");
	}
	if (!k.Skin && k.Camera == 0)
	{
		// lane VISUAL-2: the vegetation SwayOffsets (RW 0x4E61EE; W3DVegetationSway.h), entries 1 .. 10 (0 is no sway)
		for (int i = 1; i <= 10; ++i)
		{
			line("global uniform vec4 w3d_sway" + std::to_string(i) + ";");
		}
	}
	for (int s = 0; s < 2; ++s)
	{
		if (k.Tex[s])
		{
			// no source_color: the retail arithmetic runs on the stored (gamma space) texel values
			const bool clamp = k.ClampU[s] && k.ClampV[s];
			std::string hint = k.NoLod[s] ? "filter_linear" : (clamp ? "filter_linear_mipmap_anisotropic" : repeatMipFilter);
			hint += clamp ? ", repeat_disable" : ", repeat_enable";
			line("uniform sampler2D w3d_tex" + std::to_string(s) + " : " + hint + ";");
		}
		line("uniform vec4 w3d_m" + std::to_string(s) + "r0 = vec4(1.0, 0.0, 0.0, 0.0);");
		line("uniform vec4 w3d_m" + std::to_string(s) + "r1 = vec4(0.0, 1.0, 0.0, 0.0);");
	}
	if (k.NormalMap)
	{
		line("uniform sampler2D w3d_normal : " + repeatMipFilter + ", repeat_enable;");
	}
	if (k.HouseColor)
	{
		line("uniform sampler2D w3d_hc_tex : " + repeatMipFilter + ", repeat_enable; // housecolor.ini texture: the team colour mask is its alpha");
	}
	// Varyings each take a location; Intel Arc (Mesa ANV) offers Forward+ 29 for normal-mapped buildings, Metal 31. The two UV sets
	// share one vec4 (the same interpolation), and the world position stays a vertex() local: only the vertex lighting reads it.
	line("varying vec4 v_uv;"); // xy UV, zw UV2
	line("varying vec4 v_col;");
	line("varying float v_fade;");
	line("varying vec3 v_vpos;");
	line("varying vec3 v_vnrm;");
	line("varying vec3 v_wnrm;");
	line("varying vec3 v_wview;");
	// One flat varying carries per-instance lighting and team colour (separate scalars exceed the location budget).
	line("varying flat vec3 v_instance;"); // x infantry light, y fog fraction, z packed house colour
	if (k.Fx)
	{
		// normalmapped.fxo vs_2_0 (blob 22) oT1..oT3 / oT4..oT6: per light the tangent-space light vector and the normalized
		// tangent-space half vector, interpolated and used by the pixel shader WITHOUT renormalization
		for (int i = 0; i < 3; ++i)
		{
			line("varying vec3 v_lt" + std::to_string(i) + ";");
			line("varying vec3 v_ht" + std::to_string(i) + ";");
		}
	}
	else
	{
		line("varying vec4 v_lit0;"); // defaultw3d.fxo oD0 (saturated by the colour interpolator)
		line("varying vec3 v_lit1;"); // defaultw3d.fxo oD1
	}

	// ---- helpers ----
	line("vec4 w3d_row(int base, int bone, int r) {");
	line("\tint idx = base + bone * 3 + r;");
	line("\treturn texelFetch(w3d_palette, ivec2(idx & " + std::to_string(W3D_PALETTE_WIDTH - 1) + ", idx >> " + std::to_string(W3D_PALETTE_WIDTH_LOG2) + "), 0);");
	line("}");
	line("vec3 w3d_point(int base, int bone, vec3 p) {");
	line("\tvec4 a = w3d_row(base, bone, 0); vec4 b = w3d_row(base, bone, 1); vec4 c = w3d_row(base, bone, 2);");
	line("\treturn vec3(dot(a.xyz, p) + a.w, dot(b.xyz, p) + b.w, dot(c.xyz, p) + c.w);");
	line("}");
	line("vec3 w3d_dir(int base, int bone, vec3 d) {");
	line("\tvec4 a = w3d_row(base, bone, 0); vec4 b = w3d_row(base, bone, 1); vec4 c = w3d_row(base, bone, 2);");
	line("\treturn vec3(dot(a.xyz, d), dot(b.xyz, d), dot(c.xyz, d));");
	line("}");
	line("vec3 w3d_to_godot(vec3 v) { return vec3(v.x, v.z, -v.y); }");
	line("vec3 w3d_to_linear(vec3 c) { return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(0.04045, c)); }");
	if (k.Fog != W3DHardwareFog::FOG_DISABLE)
	{
		// lane RENDER-4 (S-1651): the map's hardware fog (globals w3d_fog / w3d_fog_color, W3DHardwareFog.h)
		c += W3DHardwareFog::GlslFor((W3DHardwareFog::FogMode)k.Fog);
	}
	// clamp one axis of a repeat-enabled sampler (CLAMP_U xor CLAMP_V)
	for (int s = 0; s < 2; ++s)
	{
		if (k.Tex[s] && (k.ClampU[s] != k.ClampV[s]))
		{
			line("vec2 w3d_fix" + std::to_string(s) + "(vec2 uv) {");
			line("\tvec2 h = 0.5 / vec2(textureSize(w3d_tex" + std::to_string(s) + ", 0));");
			line(std::string("\t") + (k.ClampU[s] ? "uv.x = clamp(uv.x, h.x, 1.0 - h.x);" : "uv.y = clamp(uv.y, h.y, 1.0 - h.y);"));
			line("\treturn uv;");
			line("}");
		}
	}

	// ---- vertex ----
	line("void vertex() {");
	line("\tfloat px = INSTANCE_CUSTOM.x;");
	line("\tint base = px < 0.0 ? int(-px + 0.5) - 1 : int(px + 0.5);");
	line("\tv_instance.x = px < 0.0 ? 1.0 : 0.0;");
	// lane VISUAL-2: INSTANCE_CUSTOM.w = rigid bone + 1024 * sway type + 16384 * fogged (W3DInstancer)
	line("\tint wi = int(INSTANCE_CUSTOM.w + 0.5);");
	line("\tv_instance.y = (wi & 16384) != 0 ? w3d_fogged_frac : 1.0;");
	line("\tvec3 p; vec3 n;");
	if (k.Skin)
	{
		line("\tint b0 = int(CUSTOM0.x + 0.5);");
		line("\tfloat w0 = CUSTOM0.z;");
		line("\tp = w3d_point(base, b0, VERTEX) * w0;");
		line("\tn = w3d_dir(base, b0, NORMAL) * w0;");
		if (k.Tangents)
		{
			line("\tvec3 tg = w3d_dir(base, b0, TANGENT) * w0;");
			line("\tvec3 bn = w3d_dir(base, b0, BINORMAL) * w0;");
		}
		if (k.Dual)
		{
			line("\tif (CUSTOM0.w > 0.0) {");
			line("\t\tint b1 = int(CUSTOM0.y + 0.5);");
			line("\t\tp += w3d_point(base, b1, CUSTOM1.xyz) * CUSTOM0.w;");
			line("\t\tn += w3d_dir(base, b1, CUSTOM2.xyz) * CUSTOM0.w;");
			line("\t}");
		}
	}
	else
	{
		line("\tint b = wi & 1023;"); // lane VISUAL-2: the rigid bone below the sway type and the fogged bit
		if (k.Camera != 0)
		{
			// ZH dx8renderer.cpp:1818-1847: only the mesh position is kept; the orientation faces the camera.
			// ORIENTED: Obj_Look_At(pos, cameraPos); ALIGNED: Obj_Look_At(pos, pos + cameraZ). Roll 0, W3D Z up.
			line("\tvec3 pos = w3d_point(base, b, vec3(0.0));");
			line("\tvec3 camg = (inverse(MODEL_MATRIX) * (INV_VIEW_MATRIX * vec4(0.0, 0.0, 0.0, 1.0))).xyz;");
			line("\tvec3 camw = vec3(camg.x, -camg.z, camg.y);");
			if (k.Camera == MESH_CAMERA_ORIENTED)
			{
				line("\tvec3 fx = normalize(camw - pos);");
			}
			else
			{
				line("\tvec3 zg = (inverse(MODEL_MATRIX) * (INV_VIEW_MATRIX * vec4(0.0, 0.0, 1.0, 0.0))).xyz;");
				line("\tvec3 fx = normalize(vec3(zg.x, -zg.z, zg.y));");
			}
			line("\tvec3 zz = normalize(vec3(0.0, 0.0, 1.0) - fx * fx.z);");
			line("\tvec3 yy = cross(zz, fx);");
			line("\tp = pos + fx * VERTEX.x + yy * VERTEX.y + zz * VERTEX.z;");
			line("\tn = fx * NORMAL.x + yy * NORMAL.y + zz * NORMAL.z;");
		}
		else
		{
			line("\tp = w3d_point(base, b, VERTEX);");
			line("\tn = w3d_dir(base, b, NORMAL);");
			if (k.Tangents)
			{
				line("\tvec3 tg = w3d_dir(base, b, TANGENT);");
				line("\tvec3 bn = w3d_dir(base, b, BINORMAL);");
			}
		}
	}
	line("\tVERTEX = w3d_to_godot(p);");
	if (!k.Skin && k.Camera == 0)
	{
		// lane VISUAL-2: vegetation.fxo's vertex shader (blob 5): position += (position.z - the tree's base z) * SwayOffsets[type], in world space (W3D Z up
		// = Godot Y); the base is the instance's origin (W3DTreeBuffer's tree location, RW 0x4EC93A). The normal is not changed (retail's is not)
		line("\tint sway = (wi >> 10) & 15;");
		line("\tif (sway > 0) {");
		line("\t\tvec4 sw = sway == 1 ? w3d_sway1 : sway == 2 ? w3d_sway2 : sway == 3 ? w3d_sway3 : sway == 4 ? w3d_sway4 : sway == 5 ? w3d_sway5 : sway == 6 ? w3d_sway6 : sway == 7 ? w3d_sway7 : sway == 8 ? w3d_sway8 : sway == 9 ? w3d_sway9 : w3d_sway10;");
		line("\t\tfloat h = (MODEL_MATRIX * vec4(VERTEX, 1.0)).y - MODEL_MATRIX[3].y;");
		line("\t\tVERTEX += inverse(mat3(MODEL_MATRIX)) * (w3d_to_godot(sw.xyz) * h);");
		line("\t}");
	}
	line("\tNORMAL = normalize(w3d_to_godot(n));");
	if (k.Tangents)
	{
		line("\tTANGENT = normalize(w3d_to_godot(tg));");
		line("\tBINORMAL = normalize(w3d_to_godot(bn));");
	}
	line(k.Uv2 ? "\tv_uv = vec4(UV, UV2);" : "\tv_uv = vec4(UV, UV);");
	line("\tv_col = COLOR;");
	line("\tv_fade = INSTANCE_CUSTOM.y;");
	if (k.HouseColor)
	{
		line("\tv_instance.z = INSTANCE_CUSTOM.z;");
	}
	line("\tv_vpos = (MODELVIEW_MATRIX * vec4(VERTEX, 1.0)).xyz;");
	line("\tv_vnrm = normalize((MODELVIEW_NORMAL_MATRIX * NORMAL));");
	line("\tv_wnrm = normalize(MODEL_NORMAL_MATRIX * NORMAL);");
	line("\tv_wview = mat3(INV_VIEW_MATRIX) * normalize(v_vpos);");
	line("\tvec3 wpos = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;");
	if (k.Fx)
	{
		// blob 22: world normal / binormal / tangent (World * vector, normalized), frame x = -binormal, y = -tangent, z = normal;
		// L_t = (L.-B, L.-T, L.N); H_t = normalize(((V + L).-B, (V + L).-T, (V + L).N)) with V = normalize(eye - position)
		line("\t{");
		line("\t\tvec3 wn = normalize(mat3(MODEL_MATRIX) * NORMAL);");
		if (k.Tangents)
		{
			line("\t\tvec3 fx = -normalize(mat3(MODEL_MATRIX) * w3d_to_godot(bn));");
			line("\t\tvec3 fy = -normalize(mat3(MODEL_MATRIX) * w3d_to_godot(tg));");
		}
		else
		{
			// a mesh without tangents (no normal map is applied, the texel normal is (0, 0, 1)): any orthonormal frame around the normal
			line("\t\tvec3 ax = abs(wn.y) < 0.9 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);");
			line("\t\tvec3 fx = normalize(cross(ax, wn));");
			line("\t\tvec3 fy = cross(wn, fx);");
		}
		line("\t\tvec3 vv = normalize(CAMERA_POSITION_WORLD - wpos);");
		line("\t\tmat3 ld = v_instance.x > 0.5 ? w3d_ldir_inf : w3d_ldir_obj;");
		for (int i = 0; i < 3; ++i)
		{
			const std::string si = std::to_string(i);
			line("\t\tv_lt" + si + " = vec3(dot(ld[" + si + "], fx), dot(ld[" + si + "], fy), dot(ld[" + si + "], wn));");
			line("\t\tv_ht" + si + " = normalize(vec3(dot(vv + ld[" + si + "], fx), dot(vv + ld[" + si + "], fy), dot(vv + ld[" + si + "], wn)));");
		}
		line("\t}");
	}
	else
	{
		// defaultw3d.fxo vertex lighting (vs_2_0 permutations, e.g. blob 151 of tools/render/d3d9_disasm.py): for the three directional
		// lights `lit(N.L, N.H, -, Shininess)` with H = normalize(V + L), V = normalize(eye - position);
		// oD0.rgb = (sum(colour * max(N.L, 0)) * ColorDiffuse + AmbientLight * ColorAmbient + ColorEmissive) * vertex colour * 0.5,
		// oD0.a = vertex alpha * Opacity, oD1.rgb = sum(colour * lit.z) * ColorSpecular * 0.5; the interpolators saturate both
		line("\t{");
		line("\t\tvec3 wn = normalize(MODEL_NORMAL_MATRIX * NORMAL);");
		line("\t\tvec3 vv = normalize(CAMERA_POSITION_WORLD - wpos);");
		line("\t\tbool inf = v_instance.x > 0.5;");
		line("\t\tmat3 lc = inf ? w3d_lcol_inf : w3d_lcol_obj;");
		line("\t\tmat3 ld = inf ? w3d_ldir_inf : w3d_ldir_obj;");
		line("\t\tvec3 amb = inf ? w3d_amb_inf : w3d_amb_obj;");
		line("\t\tlc[0] += w3d_tint; lc[1] += w3d_tint; lc[2] += w3d_tint; amb += w3d_tint;");
		line("\t\tlc *= v_instance.y; amb *= v_instance.y;"); // lane VISUAL-2: the fogged light environment (RW 0x46DFBA: the lights' diffuse and ambient times the fraction)
		line("\t\tvec3 dsum = vec3(0.0);");
		line("\t\tvec3 ssum = vec3(0.0);");
		line("\t\tfor (int i = 0; i < 3; i++) {");
		line("\t\t\tfloat ndl = dot(wn, ld[i]);");
		line("\t\t\tfloat ndh = dot(wn, normalize(vv + ld[i]));");
		line("\t\t\tdsum += lc[i] * max(ndl, 0.0);");
		// D3D9 lit clamps the power to [-127.9961, 127.9961]
		line("\t\t\tssum += lc[i] * ((ndl > 0.0 && ndh > 0.0) ? pow(ndh, clamp(w3d_shininess, " + fmt(-W3D_LIT_POWER_LIMIT) + ", " + fmt(W3D_LIT_POWER_LIMIT) + ")) : 0.0);");
		line("\t\t}");
		line(k.HasDCG ? "\t\tvec4 vc = COLOR;" : "\t\tvec4 vc = vec4(1.0);");
		line("\t\tv_lit0 = clamp(vec4((dsum * w3d_diffuse.rgb + amb * w3d_ambient_mat + w3d_emissive) * vc.rgb * 0.5, vc.a * w3d_diffuse.a), 0.0, 1.0);");
		line("\t\tv_lit1 = clamp(ssum * w3d_spec_color * 0.5, 0.0, 1.0);");
		line("\t}");
	}
	if (k.OcclusionPass)
	{
		// lane OCCLUDE-1: an instance that does not take part collapses to a point (no fragments); the colour pass carries the slot in v_instance.z
		line("\tint occl = wi >> " + std::to_string(W3D_OCCLUSION_SLOT_SHIFT) + ";");
		if (k.OcclusionPass == W3D_OCCLUSION_PASS_MARK)
		{
			line("\tif ((occl & " + std::to_string(W3D_OCCLUSION_OCCLUDER_BIT >> W3D_OCCLUSION_SLOT_SHIFT) + ") == 0) { VERTEX = vec3(0.0); }");
		}
		else
		{
			line("\tint slot = occl & " + std::to_string(W3D_OCCLUSION_SLOTS - 1) + ";");
			line("\tif (slot == 0 || w3d_occl_on < 0.5) { VERTEX = vec3(0.0); }");
			line("\tv_instance.z = float(slot);");
		}
	}
	line("}");

	// ---- fragment ----
	line("void fragment() {");
	if (k.Blend == W3D_GODOT_BLEND_NONE)
	{
		line("\tALBEDO = vec3(0.0);");
		line("\tALPHA = 0.0;");
		line("}");
		return c;
	}
	// per stage coordinates: matrix rows applied to (u, v, 1, 0) or to the generated vector (x, y, z, 1) in D3D camera space
	for (int s = 0; s < 2; ++s)
	{
		if (!k.Tex[s]) continue;
		const std::string ss = std::to_string(s);
		std::string in;
		switch ((W3DTexGen)k.TexGen[s])
		{
		case W3D_TEXGEN_UV:
			in = std::string("vec4(") + (s == 0 ? "v_uv.xy" : "v_uv.zw") + ", 1.0, 0.0)";
			break;
		case W3D_TEXGEN_CAMERA_NORMAL:
			in = k.ViewDependent[s] ? "vec4(vec3(v_wnrm.x, -v_wnrm.z, v_wnrm.y), 1.0)" : "vec4(v_vnrm.x, v_vnrm.y, -v_vnrm.z, 1.0)";
			break;
		case W3D_TEXGEN_CAMERA_REFLECTION:
			if (k.ViewDependent[s])
			{
				line("\tvec3 rw" + ss + " = reflect(v_wview, normalize(v_wnrm));");
				in = "vec4(rw" + ss + ".x, -rw" + ss + ".z, rw" + ss + ".y, 1.0)";
			}
			else
			{
				line("\tvec3 rv" + ss + " = reflect(normalize(v_vpos), normalize(v_vnrm));");
				in = "vec4(rv" + ss + ".x, rv" + ss + ".y, -rv" + ss + ".z, 1.0)";
			}
			break;
		case W3D_TEXGEN_CAMERA_POSITION_PROJECTED:
			line("\tvec4 clip" + ss + " = PROJECTION_MATRIX * vec4(v_vpos, 1.0);");
			in = "vec4(clip" + ss + ".xy / clip" + ss + ".w, 1.0, 0.0)";
			break;
		}
		line("\tvec4 in" + ss + " = " + in + ";");
		line("\tvec2 uvs" + ss + " = vec2(dot(w3d_m" + ss + "r0, in" + ss + "), dot(w3d_m" + ss + "r1, in" + ss + "));");
		if (k.ClampU[s] != k.ClampV[s]) line("\tuvs" + ss + " = w3d_fix" + ss + "(uvs" + ss + ");");
	}
	line("\tvec4 t0 = vec4(1.0);");
	line("\tvec4 t1 = vec4(1.0);");
	if (k.Tex[0]) line("\tt0 = texture(w3d_tex0, uvs0);");
	// House colour (stops S-022 / S-119): `hc` is the recoloured house colour texel of the stage 0 texture (housecolor.ini), its alpha the team region. Retail
	// combines it AFTER the diffuse light, not into the base texel (the override below the secondary texture code), so only its value is derived here.
	// Classic surfaces only: normalmapped.fxo and simple.fxo have no house colour parameter.
	const bool houseColor = k.HouseColor && k.Tex[0] && k.Texturing && !k.Fx && !k.SimpleFx;
	if (houseColor && k.HouseColorBaked)
	{
		// lane CAH-2 (S-1408): the texture was recoloured per texel before filtering (RW 0x531C77 on the CPU, mipmaps made from the recoloured level)
		line("\tvec4 hc = texture(w3d_hc_tex, uvs0);");
	}
	else if (houseColor)
	{
		// the map builder's per-instance colour (INFERENCE: retail has no per-instance colour, it recolours a texture per palette): the recolour of kind 1
		// (RW 0x531C77: out = (R * colour) >> 8 per channel of the 8 bit colour, alpha kept) applied to the filtered texel; the team colour is 8 bit
		// sRGB packed by W3D_Pack_House_Color, none (<= 0.5) leaves the surface as it is
		line("\tvec4 hc = vec4(0.0);");
		line("\tbool hc_on = v_instance.z > 0.5;");
		line("\tif (hc_on) {");
		line("\t\tfloat hv = v_instance.z - 1.0;");
		line("\t\tvec3 team = vec3(floor(hv / 65536.0), floor(mod(hv, 65536.0) / 256.0), mod(hv, 256.0));");
		line("\t\tvec4 hr = texture(w3d_hc_tex, uvs0);");
		line("\t\thc = vec4(floor(hr.r * 255.0 * team / 256.0) / 255.0, hr.a);");
		line("\t}");
	}
	if (k.Tex[1]) line("\tt1 = texture(w3d_tex1, uvs1);");
	line("\tvec4 cur;");
	if (k.Fx)
	{
		// normalmapped.fxo ps_2_0 (blob 6 of tools/render/d3d9_disasm.py): n = normalize(2 * normal texel - 1 with xy * BumpScale) in the
		// frame x = -binormal, y = -tangent, z = normal; per light L (toward the light) and H = normalize(V + L) in that frame:
		// colour = tex * oD0 * 2 + sum(Light.Color * (tex * DiffuseColor * [N.L > 0] N.L + SpecularColor * pow(N.H, SpecularExponent) * [N.H > 0][N.L > 0]))
		// with oD0.rgb = saturate(AmbientLight * AmbientColor * 0.5) (the preshader's c5 and the vertex shader's 0.5), alpha = tex.a
		// (oD0.a = OpacityOverride * 0.5, doubled). The cloud texture factor is not applied (S-390).
		if (k.NormalMap)
		{
			line("\tvec3 nt = texture(w3d_normal, uvs0).rgb * 2.0 - 1.0;");
			line("\tnt.xy *= w3d_bump;");
			line("\tnt = normalize(nt);");
		}
		else
		{
			line("\tvec3 nt = vec3(0.0, 0.0, 1.0);");
		}
		line("\tvec3 lts[3] = vec3[3](v_lt0, v_lt1, v_lt2);");
		line("\tvec3 hts[3] = vec3[3](v_ht0, v_ht1, v_ht2);");
		line("\tbool inf = v_instance.x > 0.5;");
		line("\tmat3 lc = inf ? w3d_lcol_inf : w3d_lcol_obj;");
		line("\tvec3 amb = inf ? w3d_amb_inf : w3d_amb_obj;");
		line("\tlc[0] += w3d_tint; lc[1] += w3d_tint; lc[2] += w3d_tint; amb += w3d_tint;");
		line("\tlc *= v_instance.y; amb *= v_instance.y;"); // lane VISUAL-2: the fogged light environment
		line("\tvec3 base = t0.rgb * w3d_diffuse.rgb;");
		line("\tvec3 col = t0.rgb * clamp(amb * w3d_ambient_mat * 0.5, 0.0, 1.0) * 2.0;");
		line("\tfor (int i = 0; i < 3; i++) {");
		line("\t\tfloat ndl = dot(nt, lts[i]);");
		line("\t\tfloat ndh = dot(nt, hts[i]);");
		line("\t\tfloat on = ndl > 0.0 ? 1.0 : 0.0;");
		line("\t\tfloat sp = (ndh > 0.0 && ndl > 0.0) ? pow(ndh, w3d_shininess) : 0.0;");
		line("\t\tcol += lc[i] * (base * ndl * on + w3d_spec_color * sp);");
		line("\t}");
		line("\tcur = vec4(col, t0.a);");
	}
	else if (k.SimpleFx)
	{
		// RENDER-2: simple.fxo ps_1_1 (blob 0): r0.rgb = t0 * v0 with v0 = oD0 = ColorEmissive (saturated by the colour interpolator), r0.a = t0.a;
		// the fog lerp is not applied (S-390)
		line("\tcur = vec4(t0.rgb * clamp(w3d_emissive, 0.0, 1.0), t0.a);");
	}
	else if (k.Texturing && k.PriGradient == W3D_GRADIENT_DISABLE)
	{
		line("\tcur = t0;");
	}
	else
	{
		// defaultw3d.fxo ps_2_0: colour = tex0 * (oD0 + oD1) * 2, alpha = tex0.a * oD0.a (no primary gradient parameter: every lit
		// classic surface takes this; the secondary texture modes follow below)
		line("\tvec4 diff = vec4(v_lit0.rgb + v_lit1, v_lit0.a);");
		line(k.Texturing ? "\tcur = vec4(t0.rgb * diff.rgb * 2.0, t0.a * diff.a);" : "\tcur = vec4(diff.rgb * 2.0, diff.a);");
	}
	if (k.Tex[1] && k.Texturing && k.ConvertedFx && (k.DetailColor == W3D_DETAILCOLOR_DETAILBLEND || k.DetailColor == W3D_DETAILCOLOR_MODALPHAADDCOLOR))
	{
		// defaultw3d.fxo ps_2_0, NumTextures 2 (PS_Array[8] and [11] of tools/render/d3d9_disasm.py: blobs 195 and 198, MeshMatDescFXShaderConversion.cpp:416-433). The
		// modes combine the texels with the LIGHT terms, before the final x2, not with the already doubled colour:
		//   DetailBlend (mode 2):       r3 = t1 - t0 * (v0 + v1); r0 = t0 * v0, alpha t0.a * v0.a; r0.rgb = r0.rgb' + alpha * r3; * cloud; * 2
		//                               = 2 * lerp(t0 * (diffuse + specular), t1, alpha)   (t1 is doubled with the rest)
		//   ModAlphaAddColor (mode 3):  r0 = t0 * v0; r0.rgb += v1 * r0.a; * cloud; * 2, Texture_1 is not sampled
		//                               = 2 * (t0 * diffuse + specular * alpha)
		// The cloud factor is not applied (S-390). An unlit surface (primary gradient Disable) takes the same expressions: its v0 / v1 are the vertex stage's, and
		// the material factory gives such a converted surface the conversion's parameters (ColorDiffuse / ColorAmbient / ColorSpecular 0, ColorEmissive 1,
		// Opacity 1: MeshMatDescFXShaderConversion.cpp:286-327, RW 0x599865-0x599a62), which the vertex shaders (VS_Array, every permutation: oD0.rgb =
		// (lights * ColorDiffuse + ambient * ColorAmbient + ColorEmissive) * vertex colour * 0.5, oD0.a = vertex alpha * Opacity * OpacityOverride,
		// oD1 = specular * ColorSpecular * 0.5; tools/render/test_secondary_texture_binary_facts.py) turn into v0 = 0.5 * vertex colour, v1 = 0
		line("\t{ float al = t0.a * v_lit0.a;");
		if (k.DetailColor == W3D_DETAILCOLOR_DETAILBLEND)
		{
			line("\t\tcur = vec4(2.0 * mix(t0.rgb * (v_lit0.rgb + v_lit1), t1.rgb, al), al); }");
		}
		else
		{
			line("\t\tcur = vec4(2.0 * (t0.rgb * v_lit0.rgb + v_lit1 * al), al); }");
		}
	}
	else if (k.Tex[1] && k.Texturing && (k.DetailColor != W3D_DETAILCOLOR_DISABLE || k.DetailAlpha != W3D_DETAILALPHA_DISABLE))
	{
		// fixed function (ShaderClassApply.cpp:1325-1445) and the converted Scale / Scale2X (the same product: blobs 189, 192): stage 1 combines with the stage 0 result
		// (the doubled lit texel); DetailBlend is D3DTOP_BLENDCURRENTALPHA(TEXTURE, CURRENT) = lerp(CURRENT, TEXTURE, CURRENT.a), the texture NOT doubled
		line("\t{ vec4 l = cur; vec4 o = t1;");
		line("\t\tcur = vec4(" + detailColorExpr(k.DetailColor) + ", " + detailAlphaExpr(k.DetailAlpha) + "); }");
	}
	if (houseColor)
	{
		// The house colour combine; the mesh's own second texture is dropped (retail replaces it: MeshClass::RecolorHouseColor, RW 0x54BDE0).
		line(k.HouseColorBaked ? "\t{" : "\tif (hc_on) {");
		if (k.PriGradient == W3D_GRADIENT_DISABLE)
		{
			// unlit: FX: v0 = ColorEmissive 1 * 0.5 so lerp(t0 * 0.5, hc * 0.5, hc.a) * 2; fixed function: BLENDTEXTUREALPHA over the texel: the same
			line("\t\tcur = vec4(mix(t0.rgb, hc.rgb, hc.a), t0.a);");
		}
		else if (k.ConvertedFx)
		{
			// defaultw3d.fxo ps_2_0 with HouseColorEnable (PS_Array[144 + ...]; blob 342 of tools/render/d3d9_disasm.py): NumTextures 2 and SecondaryTextureBlendMode 3,
			//   r0 = Texture_0 * oD0; r2.rgb = hc * 0.5 - r0.rgb; r0.rgb += hc.a * r2.rgb; r0.rgb += oD1 * r0.a; * cloud; * 2
			// = 2 * (lerp(t0 * diffuse light, hc * 0.5, hc.a) + specular * alpha): the house colour region is not lit (hc * 0.5 * 2), the specular is added after
			// the lerp and scaled by the alpha. The cloud factor is not applied (S-390)
			line("\t\tfloat al = t0.a * v_lit0.a;");
			line("\t\tcur = vec4(2.0 * (mix(t0.rgb * v_lit0.rgb, hc.rgb * 0.5, hc.a) + v_lit1 * al), al);");
		}
		else
		{
			// fixed function (ShaderClassApply.cpp:1425-1436, RotWK RW 0x53764F-0x5376A4): stage 0 modulates the diffuse (x2), stage 1
			// BLENDTEXTUREALPHA(TEXTURE, CURRENT) with the recoloured texture, the alpha stays the current one. The specular is NOT part of the lit term here
			// (it was blended away under an opaque house region): it is added after the blend (below)
			line("\t\tcur = vec4(mix(t0.rgb * v_lit0.rgb * 2.0, hc.rgb, hc.a), t0.a * v_lit0.a);");
		}
		if (!k.ConvertedFx && k.DetailAlpha != W3D_DETAILALPHA_DISABLE)
		{
			line("\t\t{ vec4 l = cur; vec4 o = hc; cur.a = " + detailAlphaExpr(k.DetailAlpha) + "; }");
		}
		if (!k.ConvertedFx && k.PriGradient != W3D_GRADIENT_DISABLE && k.SecGradient != 0)
		{
			// The vertex specular exists only with D3DRS_SPECULARENABLE = the shader's secondary gradient bit (RW 0x537AE5: (word >> 13) & 1). With the
			// stage 2 setup of DETAILCOLOR_MAX (stage 2 applied when Max_Textures_Per_Pass > 2, RW 0x537973; MODULATEALPHA_ADDCOLOR(CURRENT, SPECULAR) when
			// the caps allow it, RW 0x537688) the specular is added TWICE: cur.rgb + cur.a * spec (stage 2, the alpha after the blend and any DetailAlpha), then
			// + spec (the pipeline's specular add after the last stage). INFERENCE (stop S-6023): the caps are taken as set; the D3D9 fixed-function rules
			// (vertex specular zero without SPECULARENABLE, added after the cascade with it) are the runtime's, not game.dat's. The lit specular is the vertex
			// shader's v_lit1 (x2: the 0.5 of the effect convention), saturated like a vertex colour
			line("\t\tvec3 spec = min(v_lit1 * 2.0, vec3(1.0));");
			line("\t\tcur.rgb += cur.a * spec;");
			line("\t\tcur.rgb += spec;");
		}
		line("\t}");
	}
	line("\tfloat a = cur.a;");
	if (k.AlphaTest)
	{
		line(std::string("\tif (") + (k.AlphaTestLessEqual ? "a > w3d_alpha_ref" : "a < w3d_alpha_ref") + ") { discard; }");
	}
	if (k.Blend == W3D_GODOT_BLEND_OPAQUE && !alphaOverride)
	{
		// retail fades opaque meshes through the pivot fade; stay in the opaque pass with a dither
		line("\tif (v_fade < 0.999) {");
		line("\t\tfloat ign = fract(52.9829189 * fract(dot(FRAGCOORD.xy, vec2(0.06711056, 0.00583715))));");
		line("\t\tif (v_fade <= ign) { discard; }");
		line("\t}");
	}
	if (k.OcclusionPass)
	{
		// lane OCCLUDE-1: after the surface's own alpha test and dither, the pass's output
		if (k.OcclusionPass == W3D_OCCLUSION_PASS_MARK)
		{
			line("\tALBEDO = vec3(0.0);");
			line("\tALPHA = 0.0;");
		}
		else
		{
			line("\tvec4 oc = texelFetch(w3d_occl_colors, ivec2(int(v_instance.z + 0.5), 0), 0);");
			line("\tALBEDO = oc.rgb;"); // gamma space, as the other blended surfaces (RENDER-3, S-831)
			line("\tALPHA = oc.a;");
		}
		line("}");
		return c;
	}
	if (k.Fog != W3DHardwareFog::FOG_DISABLE)
	{
		// lane RENDER-4 (S-1651): the fog stage after the pixel shader (saturated output, D3D9), at the eye range of the fragment (the effects' vertex
		// fog, RW normalmapped.fxo / simple.fxo: |eye - world position|, here per pixel)
		line("	cur.rgb = w3d_apply_fog(clamp(cur.rgb, 0.0, 1.0), length(v_vpos));");
	}
	// the D3D9 frame buffer saturates; the gamma space result is handed to Godot as linear (sRGB-encoded again on output)
	// RENDER-3 (S-831): a blended surface draws in the transparent pass, which composites in retail's gamma space (GodotGammaComposite.h): it
	// hands over the gamma value itself; an opaque one is converted once here
	if (k.Blend == W3D_GODOT_BLEND_OPAQUE && !alphaOverride)
	{
		line("\tALBEDO = w3d_to_linear(clamp(cur.rgb, 0.0, 1.0));");
	}
	else
	{
		line("\tALBEDO = clamp(cur.rgb, 0.0, 1.0);");
	}
	switch ((W3DGodotBlend)k.Blend)
	{
	case W3D_GODOT_BLEND_OPAQUE:
		if (alphaOverride)
		{
			line("\tALPHA = a * v_fade;"); // the material opacity times the texture alpha (DX8: Set_Opacity(override) on the vertex material)
		}
		break;
	case W3D_GODOT_BLEND_MIX: line("\tALPHA = a * v_fade;"); break;
	case W3D_GODOT_BLEND_ADD: case W3D_GODOT_BLEND_APPROX_ADD: line("\tALPHA = v_fade;"); break;
	case W3D_GODOT_BLEND_ADD_ALPHA: line("\tALPHA = a * v_fade;"); break;
	case W3D_GODOT_BLEND_MUL: line("\tALBEDO = mix(vec3(1.0), clamp(cur.rgb, 0.0, 1.0), v_fade);"); break;
	case W3D_GODOT_BLEND_PREMUL: line("\tALPHA = a * v_fade;"); break;
	case W3D_GODOT_BLEND_NONE: break;
	}
	line("}");
	return c;
}
