// OpenBFME. GPL-3.0. See fxshadergen.h.
//
// Byte-identical move of the pure string builders that lived in GodotFXPlayer.cpp's anonymous namespace: the
// device layer calls these, and engine/tests/test_fx_shadergen.cpp pins their output to hashes recorded from the
// pre-move implementation.

#include "Libraries/WWVegas/WW3D2/fxshadergen.h"

#include "GameClient/FXParticleSystem.h"
#include "GameEngineDevice/W3DDevice/GameClient/W3DHardwareFog.h"

#include <string>

namespace FXShaderGen
{
namespace
{

using namespace FXParticleSystem;

// ---- blend states of RW ShaderClass::Apply (notes fx-draw.md 3.1 / 3.2) ----------------------------------------------------------------
struct BlendSpec
{
	const char *blend;       // Godot render mode
	const char *depthDraw;
	bool depthTest;
	enum { ADD, MIX, MUL, TEST } mode;
	int alphaByte;           // discard below alphaByte / 255 (D3D alpha test, GREATEREQUAL); 0 = no test
};

BlendSpec blendFor(int shader)
{
	switch (shader)
	{
	case PARTICLE_SHADER_ADDITIVE_ALPHA_TEST: return { "blend_add", "depth_draw_never", true, BlendSpec::ADD, 1 }; // RW 0x194033: alpha test ref 1
	case PARTICLE_SHADER_ALPHA: return { "blend_mix", "depth_draw_never", true, BlendSpec::MIX, 1 };           // RW 0x1180b3 + the implicit ref-1 test
	case PARTICLE_SHADER_ALPHA_TEST: return { "blend_mix", "depth_draw_opaque", true, BlendSpec::TEST, 96 };    // RW 0x15401b: ONE/ZERO, ref 0x60, depth write
	case PARTICLE_SHADER_MULTIPLY: return { "blend_mul", "depth_draw_never", true, BlendSpec::MUL, 0 };                  // RW 0x110053
	case PARTICLE_SHADER_ADDITIVE_NO_DEPTH_TEST: return { "blend_add", "depth_draw_never", false, BlendSpec::ADD, 0 };    // RW 0x114037
	case PARTICLE_SHADER_ALPHA_NO_DEPTH_TEST: return { "blend_mix", "depth_draw_never", false, BlendSpec::MIX, 1 }; // RW 0x1180b7
	default: return { "blend_add", "depth_draw_never", true, BlendSpec::ADD, 0 };                                       // ADDITIVE and every other word
	}
}

// lane RENDER-4 (S-1651): the fog of a CPU particle shader: the draw calls ShaderClass::Enable_Fog (RW 0x537CE0) for the system's ShaderClass word
// (RW 0x9620B7), so additive fades toward black, alpha and alpha-test toward the fog colour, multiply toward white (W3DHardwareFog.h)
W3DHardwareFog::FogMode fogModeFor(int shader)
{
	switch (shader)
	{
	case PARTICLE_SHADER_ADDITIVE_ALPHA_TEST: return W3DHardwareFog::ModeForShaderWord(0x194033u);
	case PARTICLE_SHADER_ALPHA: return W3DHardwareFog::ModeForShaderWord(0x1180B3u);
	case PARTICLE_SHADER_ALPHA_TEST: return W3DHardwareFog::ModeForShaderWord(0x15401Bu);
	case PARTICLE_SHADER_MULTIPLY: return W3DHardwareFog::ModeForShaderWord(0x110053u);
	case PARTICLE_SHADER_ADDITIVE_NO_DEPTH_TEST: return W3DHardwareFog::ModeForShaderWord(0x114037u);
	case PARTICLE_SHADER_ALPHA_NO_DEPTH_TEST: return W3DHardwareFog::ModeForShaderWord(0x1180B7u);
	default: return W3DHardwareFog::ModeForBlend(1, 1); // ADDITIVE: ONE / ONE
	}
}

std::string fogDeclarations(int shader)
{
	return W3DHardwareFog::GlslFor(fogModeFor(shader)) + "varying float fog_range;\n";
}

std::string modes(const BlendSpec &b)
{
	std::string m = "render_mode unshaded, cull_disabled, skip_vertex_transform, fog_disabled, shadows_disabled, ";
	m += b.blend;
	m += ", ";
	m += b.depthDraw;
	if (!b.depthTest)
	{
		m += ", depth_test_disabled";
	}
	m += ";\n";
	return m;
}

// the alpha reference as an exact shader expression: the D3D test compares the 8-bit alpha with the integer reference, so a texel whose alpha equals the
// reference must pass; a decimal literal such as 0.003922 (> 1/255) would reject it
std::string refExpr(int byte) { return "(" + std::to_string(byte) + ".0 / 255.0)"; }

// ---- lane FX-3 round 2: soft particles (OWNER DECISION 2026-10-07, presentation beyond retail; PLAN's presentation policy) -------------------------
// Retail draws particles with a plain LEQUAL depth test (ShaderClass word, RW 0xD9B310), so a puff that reaches into the ground or a model is cut along
// a hard straight line (QA-1 U7, stop S-1440). The soft mode fades each fragment by the view-depth distance between it and the opaque scene behind it
// (Godot's depth texture of the opaque pass): fade = clamp((scene depth - fragment depth) / distance, 0, 1), with the distance scaled to the particle's
// size (kSoftSizeFactor, clamped to [kSoftMinDistance, kSoftMaxDistance] world units; strips and W3D emitter meshes, which carry no size, use
// kSoftMeshDistance). ALPHA blends scale the alpha, additive blends the colour (ONE / ONE adds nothing at 0), multiply blends move toward white (the
// identity of DESTCOLOR / ZERO); the reference-1 alpha test runs on the unfaded alpha so coverage stays retail's. ALPHA_TEST draws in the opaque pass
// with depth writes and keeps the retail cut. With the soft mode off (project setting openbfme/rendering/soft_particles = false, or
// FXPlayer.soft_particles) the generated shader code is exactly the retail code (fx_render_test.gd pins both).
constexpr float kSoftSizeFactor = 0.25f;
constexpr float kSoftMinDistance = 2.0f;
constexpr float kSoftMaxDistance = 16.0f;
constexpr float kSoftMeshDistance = 8.0f;

std::string softUniforms()
{
	return "uniform sampler2D soft_depth : hint_depth_texture, filter_nearest, repeat_disable;\n"
		   "varying float soft_dist;\n"
		   "float soft_fade(vec2 suv, float frag_z, mat4 inv_proj, float dist) {\n"
		   "\tfloat d = texture(soft_depth, suv).r;\n"
		   "\tvec4 v = inv_proj * vec4(suv * 2.0 - 1.0, d, 1.0);\n"
		   "\tfloat scene_z = -v.z / v.w;\n"
		   "\treturn clamp((scene_z - frag_z) / dist, 0.0, 1.0);\n"
		   "}\n";
}

// the vertex-shader line that sets soft_dist from a size expression
std::string softDistance(const std::string &sizeExpr)
{
	return "\tsoft_dist = clamp((" + sizeExpr + ") * " + std::to_string(kSoftSizeFactor) + ", " + std::to_string(kSoftMinDistance) + ", " +
		std::to_string(kSoftMaxDistance) + ");\n";
}

std::string fmtFloat(float v) { return std::to_string(v); }

// the fragment output of a CPU particle: D3D modulate (texture * diffuse); blend state per shader word
std::string fragmentFor(const BlendSpec &b, const char *colorExpr, bool soft = false)
{
	std::string f = "void fragment() {\n\tvec4 t = texture(tex, UV);\n\tvec4 c = t * ";
	f += colorExpr;
	f += ";\n";
	f += "\tc.rgb = w3d_apply_fog(clamp(c.rgb, 0.0, 1.0), fog_range);\n"; // lane RENDER-4 (S-1651): the fog stage after the texture stages
	const bool fade = soft && b.mode != BlendSpec::TEST;
	const std::string fadeLine = "\tfloat soft = soft_fade(SCREEN_UV, -VERTEX.z, INV_PROJECTION_MATRIX, soft_dist);\n";
	switch (b.mode)
	{
	// lane RENDER-3 (S-831): texture and diffuse are retail's raw gamma-space values (no sRGB decode of the texture, D3D9 modulate); the blended
	// modes are drawn in the gamma-space transparent pass (GodotGammaComposite.h) and output the gamma value itself; the alpha-test mode draws in the
	// linear opaque pass and is decoded once
	case BlendSpec::ADD:
		if (b.alphaByte > 0)
		{
			f += "\tif (c.a < " + refExpr(b.alphaByte) + ") discard;\n";
		}
		f += fade ? fadeLine + "\tALBEDO = c.rgb * soft;\n\tALPHA = 1.0;\n" : "\tALBEDO = c.rgb;\n\tALPHA = 1.0;\n"; // ONE / ONE ignores alpha
		break;
	case BlendSpec::MIX:
		f += "\tif (c.a < " + refExpr(b.alphaByte) + ") discard;\n";
		f += fade ? fadeLine + "\tALBEDO = c.rgb;\n\tALPHA = c.a * soft;\n" : "\tALBEDO = c.rgb;\n\tALPHA = c.a;\n";
		break;
	case BlendSpec::TEST:
		// ONE / ZERO with the reference test and depth writes: ALPHA is left unwritten so Godot draws it in the opaque pass with depth writes
		f += "\tif (c.a < " + refExpr(b.alphaByte) + ") discard;\n\tvec3 g = clamp(c.rgb, 0.0, 1.0);\n"
			 "\tALBEDO = mix(g / 12.92, pow((g + 0.055) / 1.055, vec3(2.4)), step(0.04045, g));\n";
		break;
	case BlendSpec::MUL:
		f += fade ? fadeLine + "\tALBEDO = mix(vec3(1.0), c.rgb, soft);\n\tALPHA = 1.0;\n" : "\tALBEDO = c.rgb;\n\tALPHA = 1.0;\n"; // ZERO / SRCCOLOR = DESTCOLOR / ZERO
		break;
	}
	f += "}\n";
	return f;
}

std::string spriteShader(int shader, bool ground, bool soft)
{
	const BlendSpec b = blendFor(shader);
	soft = soft && b.mode != BlendSpec::TEST;
	std::string s = "shader_type spatial;\n" + modes(b);
	s += "uniform sampler2D tex : filter_linear_mipmap, repeat_disable;\n";
	s += "varying vec4 vcol;\n";
	s += fogDeclarations(shader);
	if (soft)
	{
		s += softUniforms();
	}
	s += "void vertex() {\n";
	s += "\tvec3 wp = (MODEL_MATRIX * vec4(0.0, 0.0, 0.0, 1.0)).xyz;\n";
	s += "\tfloat size = INSTANCE_CUSTOM.x;\n\tfloat ang = INSTANCE_CUSTOM.y;\n\tvcol = COLOR;\n";
	if (soft)
	{
		s += softDistance(ground ? "2.0 * size" : "size"); // the drawn edge: size for billboards, 2 * size ground aligned (RW 0x57BF0D)
	}
	if (!ground)
	{
		// PointGroup billboard: the corner (rotated by the angle byte * 2pi/256) is added in VIEW space, edge length = size
		s += "\tfloat a = ang * 0.024543693;\n\tfloat c = cos(a);\n\tfloat s = sin(a);\n";
		s += "\tvec2 q = vec2(VERTEX.x * c - VERTEX.y * s, VERTEX.x * s + VERTEX.y * c);\n";
		s += "\tvec4 vp = VIEW_MATRIX * vec4(wp, 1.0);\n\tvp.xy += q * size;\n\tVERTEX = vp.xyz;\n";
		s += "\tfog_range = length(VERTEX);\n";
	}
	else
	{
		// ground aligned: world-space corners loc +- (X' +- Y') * size, edge length 2 * size; D3DXMatrixRotationZ(angle / 255 * 2pi) applied
		// to the unit axes (X' = (cos, -sin), Y' = (sin, cos) in SAGE xy), then (x, z, -y)
		s += "\tfloat a = ang * 0.024639942;\n\tfloat c = cos(a);\n\tfloat s = sin(a);\n";
		s += "\tvec3 X = vec3(c, 0.0, s);\n\tvec3 Y = vec3(s, 0.0, -c);\n";
		s += "\tint k = int(UV2.x + 0.5);\n\tvec3 off;\n";
		s += "\tif (k == 0) off = X + Y; else if (k == 1) off = X - Y; else if (k == 2) off = -(X + Y); else off = -X + Y;\n";
		s += "\tVERTEX = (VIEW_MATRIX * vec4(wp + off * size, 1.0)).xyz;\n";
		s += "\tfog_range = length(VERTEX);\n";
	}
	s += "}\n";
	s += fragmentFor(b, "vcol", soft);
	return s;
}

// QuadDraw / ButterflyDraw / Streak / Lightning: the vertices are generated on the CPU (world space already)
std::string meshShader(int shader, bool soft)
{
	const BlendSpec b = blendFor(shader);
	soft = soft && b.mode != BlendSpec::TEST;
	std::string s = "shader_type spatial;\n" + modes(b);
	s += "uniform sampler2D tex : filter_linear_mipmap, repeat_disable;\n";
	s += "varying vec4 vcol;\n";
	s += fogDeclarations(shader);
	if (soft)
	{
		s += softUniforms();
		s += "void vertex() {\n\tvcol = COLOR;\n\tsoft_dist = " + fmtFloat(kSoftMeshDistance) + ";\n\tVERTEX = (VIEW_MATRIX * MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;\n\tfog_range = length(VERTEX);\n}\n";
	}
	else
	{
		s += "void vertex() {\n\tvcol = COLOR;\n\tVERTEX = (VIEW_MATRIX * MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;\n\tfog_range = length(VERTEX);\n}\n";
	}
	s += fragmentFor(b, "vcol", soft);
	return s;
}

// GpuDraw: a port of the vs_2_0 / ps_2_0 of gpuparticle.fxo, technique `Default` (notes fx-draw-gpu.md 2.1, SSA listing of the shipped
// bytecode). Inputs per instance: MODEL_MATRIX origin = particle position (SAGE space, the node sits at the identity), COLOR = (life, seed, 0, 0),
// INSTANCE_CUSTOM = (vel.xyz, birth frame). The blend comes from the preshader of Draw.ShaderType: 1 ADDITIVE ONE/ONE, 3 ALPHA
// SRCALPHA/INVSRCALPHA, anything else DESTCOLOR/ZERO; depth test LEQUAL, no depth write, no alpha test.
std::string gpuShader(int shaderType, bool soft)
{
	const char *blend = shaderType == PARTICLE_SHADER_ADDITIVE ? "blend_add" : (shaderType == PARTICLE_SHADER_ALPHA ? "blend_mix" : "blend_mul");
	std::string s = "shader_type spatial;\nrender_mode unshaded, cull_disabled, skip_vertex_transform, fog_disabled, shadows_disabled, depth_draw_never, ";
	s += blend;
	s += ";\n";
	s += R"GLSL(
uniform sampler2D tex : filter_linear_mipmap, repeat_disable;
uniform vec2 tex_layout = vec2(1.0, 1.0);   // (FramesPerRow, TotalFrames)
uniform float speed_mult = 1.0;
uniform vec4 color_keys[4];
uniform vec4 time_keys;
uniform vec2 color_scale;
uniform vec3 gravity;
uniform vec3 drift;
uniform vec2 vel_damp = vec2(1.0);
uniform vec2 size_u;
uniform vec2 size_rate;
uniform vec2 size_rate_damp = vec2(1.0);
uniform vec2 xy_rot;
uniform vec2 xy_rot_rate;
uniform vec2 xy_rot_damp = vec2(1.0);
uniform vec2 z_rot;
uniform vec2 z_rot_rate;
uniform vec2 z_rot_damp = vec2(1.0);
uniform float now;

const float RND[16] = float[16](0.5, 0.15, 0.7, 0.25, 0.9, 0.5, 0.2, 0.3, 0.3, 0.5, 0.2, 0.6, 0.9, 0.7, 0.1, 0.4);
const vec2 CORNER[4] = vec2[4](vec2(-0.5, -0.5), vec2(0.5, -0.5), vec2(0.5, 0.5), vec2(-0.5, 0.5));
const vec2 CORNER_UV[4] = vec2[4](vec2(0.0, 1.0), vec2(1.0, 1.0), vec2(1.0, 0.0), vec2(0.0, 0.0));

varying vec4 vcol;
varying vec2 uv0;
varying vec2 uv1;
varying float lerp_t;
)GLSL";
	const bool fogged = shaderType == PARTICLE_SHADER_ALPHA;
	if (fogged)
	{
		// lane RENDER-4 (S-1651): gpuparticle.fxo's alpha pixel shader (ps_2_0 blob 7) fogs: rgb = lerp(Fog colour, tex * colour, saturate(fog)), the factor
		// from the vertex shader's eye range (vs_2_0 blob 12, Fog[2] / Fog[3]); the other GPU blends are drawn without fog (S-1651)
		s += W3DHardwareFog::GlslFor(W3DHardwareFog::FOG_ENABLE) + "varying float fog_range;\n";
	}
	if (soft)
	{
		s += softUniforms();
	}
	s += R"GLSL(
float rndk(float seed, float k) {
	float f = fract(seed * 0.1875 + k);
	return RND[int(floor(f * 16.0))];
}
float wrap_angle(float a) {
	return fract(a * 0.159155 + 0.5) * 6.28319 - 3.14159;
}
vec2 frame_uv(float g, float fpr, float inv_fpr, float fpr_m1, int corner) {
	float fg = fract(g * inv_fpr);
	float col = floor(fg * fpr_m1);
	float row = floor(g * inv_fpr);
	return vec2(col, row) * inv_fpr + CORNER_UV[corner] * inv_fpr;
}

void vertex() {
	vec3 p0 = MODEL_MATRIX[3].xyz;
	float life = COLOR.r;
	float seed = COLOR.g;
	vec3 vel = INSTANCE_CUSTOM.xyz;
	float birth = INSTANCE_CUSTOM.w;
	float age = now - birth;
	float n = age / life;
	float dead = (life < age) ? 1.0 : 0.0;
	int corner = int(floor(UV2.x * (1.0 - dead) + 0.5));

	float zrot0 = rndk(seed, 1.375) * (z_rot.y - z_rot.x) + z_rot.x;
	float zdamp = rndk(seed, 2.75) * (z_rot_damp.y - z_rot_damp.x) + z_rot_damp.x;
	float rate_rnd = rndk(seed, 10.3125);
	float zrate = rate_rnd * (z_rot_rate.y - z_rot_rate.x) + z_rot_rate.x;
	float thz = wrap_angle((age * zrate) * (zdamp * n) + zrot0);
	float xyrot0 = rndk(seed, 6.1875) * (xy_rot.y - xy_rot.x) + xy_rot.x;
	float xydamp = rndk(seed, 4.125) * (xy_rot_damp.y - xy_rot_damp.x) + xy_rot_damp.x;
	float xyrate = rate_rnd * (xy_rot_rate.y - xy_rot_rate.x) + xy_rot_rate.x;
	float thxy = wrap_angle((age * xyrate) * (n * xydamp) + xyrot0);

	vec3 v = vel + drift + (age * 0.5) * gravity;
	float vd = n * (rndk(seed, 8.9375) * (vel_damp.y - vel_damp.x) + vel_damp.x);
	vec3 d = (age * v) * vd;
	float cxy = cos(thxy);
	float sxy = sin(thxy);
	vec3 p = p0 + vec3(cxy * d.x + sxy * d.y, cxy * d.y - sxy * d.x, d.z);

	float size0 = rndk(seed, 2.0625) * (size_u.y - size_u.x) + size_u.x;
	float rnd_k48 = rndk(seed, 4.8125);
	float srd = rnd_k48 * (size_rate_damp.y - size_rate_damp.x) + size_rate_damp.x;
	float sr0 = rndk(seed, 7.5625) * (size_rate.y - size_rate.x) + size_rate.x;
	float size = (age * sr0) * (n * srd) + size0;

	vec2 cr = CORNER[corner];
	float cz = cos(thz);
	float sz = sin(thz);
	vec2 q = vec2(cr.x * cz + cr.y * sz, cr.y * cz - cr.x * sz);
	vec3 right = INV_VIEW_MATRIX[0].xyz;
	vec3 up = INV_VIEW_MATRIX[1].xyz;
	vec3 pg = vec3(p.x, p.z, -p.y) + size * (q.x * right + q.y * up);   // SAGE (x, y, z) -> Godot (x, z, -y)
	VERTEX = (VIEW_MATRIX * vec4(pg, 1.0)).xyz;
)GLSL";
	if (fogged)
	{
		s += "\tfog_range = length(VERTEX);\n";
	}
	if (soft)
	{
		s += softDistance("size");
	}
	s += R"GLSL(
	// frame animation
	float tf = tex_layout.y;
	float inv_tf = 1.0 / tf;
	float tf_m1 = tf - 1.0;
	float fpr = tex_layout.x;
	float inv_fpr = 1.0 / fpr;
	float fpr_m1 = fpr - 1.0;
	float multi = (tf > 1.0) ? 1.0 : 0.0;
	float f = floor(rnd_k48 * tf) + floor(age * speed_mult);
	float g_cur = fract(f * inv_tf) * tf_m1;
	float g_next = fract((f + 1.0) * inv_tf) * tf_m1;
	vec2 u_cur = frame_uv(g_cur, fpr, inv_fpr, fpr_m1, corner);
	vec2 u_next = frame_uv(g_next, fpr, inv_fpr, fpr_m1, corner);
	uv0 = u_cur;
	uv1 = multi * (u_next - u_cur) + u_cur;
	lerp_t = fract(age * speed_mult) * multi;

	// colour keys over the age (TimeKeys in frames)
	float scalar = rndk(seed, 2.75) * (color_scale.y - color_scale.x) + color_scale.x;
	vec4 k0 = color_keys[0];
	vec4 k3 = color_keys[3];
	vec4 k1 = vec4(clamp(color_keys[1].rgb + scalar, 0.0, 1.0), clamp(color_keys[1].a, 0.0, 1.0));
	vec4 k2 = vec4(clamp(color_keys[2].rgb + scalar, 0.0, 1.0), clamp(color_keys[2].a, 0.0, 1.0));
	float t0 = time_keys.x;
	float t1 = time_keys.y;
	float t2 = time_keys.z;
	float t3 = time_keys.w;
	float inv_d1 = 1.0 / (t1 - t0 + 0.001);
	float inv_d2 = 1.0 / (t2 - t1 + 0.001);
	float inv_d3 = 1.0 / (t3 - t2 + 0.001);
	vec4 seg1 = mix(k0, k1, (age - t0) * inv_d1);
	vec4 seg2 = mix(k1, k2, (age - t1) * inv_d2);
	vec4 seg3 = mix(k2, k3, (age - t2) * inv_d3);
	vec4 col = (age <= t3) ? seg3 : k3;
	col = (age <= t2) ? seg2 : col;
	col = (age <= t1) ? seg1 : col;
	col = (age <= t0) ? k0 : col;
	vcol = col;
}

void fragment() {
	vec4 c = mix(texture(tex, uv0), texture(tex, uv1), lerp_t) * vcol;
)GLSL";
	if (fogged)
	{
		s += "\tc.rgb = w3d_apply_fog(c.rgb, fog_range);\n";
	}
	if (soft)
	{
		// the preshader's three blends: ALPHA scales alpha, ADDITIVE the colour, the DESTCOLOR / ZERO rest moves toward white
		s += "\tfloat soft = soft_fade(SCREEN_UV, -VERTEX.z, INV_PROJECTION_MATRIX, soft_dist);\n";
		if (shaderType == PARTICLE_SHADER_ALPHA)
		{
			s += "\tALBEDO = c.rgb;\n\tALPHA = c.a * soft;\n}\n";
		}
		else if (shaderType == PARTICLE_SHADER_ADDITIVE)
		{
			s += "\tALBEDO = c.rgb * soft;\n\tALPHA = 1.0;\n}\n";
		}
		else
		{
			s += "\tALBEDO = mix(vec3(1.0), c.rgb, soft);\n\tALPHA = 1.0;\n}\n";
		}
		return s;
	}
	if (shaderType == PARTICLE_SHADER_ALPHA)
	{
		s += "\tALBEDO = c.rgb;\n\tALPHA = c.a;\n}\n";
	}
	else
	{
		s += "\tALBEDO = c.rgb;\n\tALPHA = 1.0;\n}\n";
	}
	return s;
}

} // namespace

std::string SpriteShader(int shader, bool ground, bool soft) { return spriteShader(shader, ground, soft); }
std::string MeshShader(int shader, bool soft) { return meshShader(shader, soft); }
std::string GpuShader(int shaderType, bool soft) { return gpuShader(shaderType, soft); }

} // namespace FXShaderGen
