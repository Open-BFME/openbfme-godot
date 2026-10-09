// OpenBFME. GPL-3.0. See GodotFXPlayer.h.

#include "GodotDevice/GodotGammaComposite.h"
#include "GodotDevice/GodotFXPlayer.h"

#include "GodotDevice/GodotRetailFileSystem.h"
#include "GodotDevice/GodotW3DMaterial.h"
#include "GameEngineDevice/W3DDevice/GameClient/W3DHardwareFog.h"
#include "GameClient/ParticleTexture.h"

#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/geometry_instance3d.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/aabb.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_color_array.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <set>
#include <type_traits>

namespace godot
{

namespace
{

String toGodot(const std::string &s) { return String::utf8(s.c_str(), (int64_t)s.size()); }
std::string toNative(const String &s)
{
	CharString utf8 = s.utf8();
	return std::string(utf8.get_data(), (size_t)utf8.length());
}

double usSince(const std::chrono::steady_clock::time_point &t0)
{
	return std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
}

// SAGE (x east, y north, z up) -> Godot (x, z, -y)
Vector3 toGodotSpace(const Coord3D &c) { return Vector3(c.x, c.z, -c.y); }
Coord3D toSage(const Vector3 &v) { return Coord3D{ v.x, -v.z, v.y }; }

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

Ref<ArrayMesh> makeQuadMesh()
{
	// corners 0..3 of the point group quad: positions (-0.5, 0.5), (-0.5, -0.5), (0.5, -0.5), (0.5, 0.5); UV (0,0) (0,1) (1,1) (1,0); UV2.x = corner index
	PackedVector3Array v;
	PackedVector2Array uv, uv2;
	const float pos[4][2] = { { -0.5f, 0.5f }, { -0.5f, -0.5f }, { 0.5f, -0.5f }, { 0.5f, 0.5f } };
	const float tc[4][2] = { { 0, 0 }, { 0, 1 }, { 1, 1 }, { 1, 0 } };
	for (int k = 0; k < 4; ++k)
	{
		v.push_back(Vector3(pos[k][0], pos[k][1], 0.0f));
		uv.push_back(Vector2(tc[k][0], tc[k][1]));
		uv2.push_back(Vector2((float)k, 0.0f));
	}
	PackedInt32Array idx;
	const int ids[6] = { 0, 1, 2, 2, 3, 0 }; // RW indices (0,1,2)(2,3,0)
	for (int i : ids)
	{
		idx.push_back(i);
	}
	Array arrays;
	arrays.resize(Mesh::ARRAY_MAX);
	arrays[Mesh::ARRAY_VERTEX] = v;
	arrays[Mesh::ARRAY_TEX_UV] = uv;
	arrays[Mesh::ARRAY_TEX_UV2] = uv2;
	arrays[Mesh::ARRAY_INDEX] = idx;
	Ref<ArrayMesh> m;
	m.instantiate();
	m->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
	m->set_custom_aabb(AABB(Vector3(-1e5f, -1e5f, -1e5f), Vector3(2e5f, 2e5f, 2e5f)));
	return m;
}

} // namespace

FXPlayer::FXPlayer()
{
	// lane FX-3 round 2: the client setting (Project Settings openbfme/rendering/soft_particles; on unless set to false)
	ProjectSettings *ps = ProjectSettings::get_singleton();
	m_soft = !(ps && ps->has_setting("openbfme/rendering/soft_particles")) || (bool)ps->get_setting("openbfme/rendering/soft_particles");
}
FXPlayer::~FXPlayer() {}

void FXPlayer::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("setup", "fs"), &FXPlayer::setup);
	ClassDB::bind_method(D_METHOD("preload_textures", "budget_ms"), &FXPlayer::preload_textures);
	ClassDB::bind_method(D_METHOD("list_particle_systems"), &FXPlayer::list_particle_systems);
	ClassDB::bind_method(D_METHOD("list_fx_lists"), &FXPlayer::list_fx_lists);
	ClassDB::bind_method(D_METHOD("play_particle_system", "name", "position"), &FXPlayer::play_particle_system);
	ClassDB::bind_method(D_METHOD("play_fx_list", "name", "position", "as_object"), &FXPlayer::play_fx_list);
	ClassDB::bind_method(D_METHOD("define_ini", "name", "text"), &FXPlayer::define_ini);
	ClassDB::bind_method(D_METHOD("play_w3d_emitter", "name", "position"), &FXPlayer::play_w3d_emitter);
	ClassDB::bind_method(D_METHOD("clear"), &FXPlayer::clear);
	ClassDB::bind_method(D_METHOD("seed", "seed"), &FXPlayer::seed);
	ClassDB::bind_method(D_METHOD("advance", "delta"), &FXPlayer::advance);
	ClassDB::bind_method(D_METHOD("step_once"), &FXPlayer::step_once);
	ClassDB::bind_method(D_METHOD("rebuild"), &FXPlayer::rebuild);
	ClassDB::bind_method(D_METHOD("set_paused", "paused"), &FXPlayer::set_paused);
	ClassDB::bind_method(D_METHOD("is_paused"), &FXPlayer::is_paused);
	ClassDB::bind_method(D_METHOD("set_max_particles", "count"), &FXPlayer::set_max_particles);
	ClassDB::bind_method(D_METHOD("get_stats"), &FXPlayer::get_stats);
	ClassDB::bind_method(D_METHOD("get_unverified"), &FXPlayer::get_unverified);
	ClassDB::bind_method(D_METHOD("get_events"), &FXPlayer::get_events);
	ClassDB::bind_method(D_METHOD("get_frame"), &FXPlayer::get_frame);
	// lane FX-3 round 2: soft particles (default: the project setting openbfme/rendering/soft_particles, on when absent); false = retail's hard depth cut
	ClassDB::bind_method(D_METHOD("set_soft_particles", "enabled"), &FXPlayer::set_soft_particles);
	ClassDB::bind_method(D_METHOD("get_soft_particles"), &FXPlayer::get_soft_particles);
	ClassDB::bind_method(D_METHOD("set_batch_summary", "enabled"), &FXPlayer::set_batch_summary); // lane RENDER-4
	ClassDB::bind_method(D_METHOD("get_batch_summary"), &FXPlayer::get_batch_summary);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "soft_particles"), "set_soft_particles", "get_soft_particles");
}

void FXPlayer::set_soft_particles(bool enabled)
{
	if (m_soft == enabled)
	{
		return;
	}
	m_soft = enabled;
	for (Batch &b : m_batches)
	{
		b.key.clear(); // every slot rebuilds its node and material with the other shader at the next upload
	}
}

void FXPlayer::_notification(int what)
{
	// lane RENDER-3 (S-831): the particle shaders output gamma-space values; the camera that shows them composites the transparent pass in gamma space
	if (what == NOTIFICATION_READY)
	{
		set_process_internal(true);
	}
	if (what == NOTIFICATION_READY || what == NOTIFICATION_INTERNAL_PROCESS)
	{
		GammaComposite::ensure(this);
	}
}

Dictionary FXPlayer::setup(const Ref<RetailFileSystem> &fs)
{
	Dictionary result;
	Array errors;
	m_fs = fs;
	if (fs.is_null() || !fs->is_mounted() || !fs->archive())
	{
		errors.push_back("the retail file system is not mounted");
		result["ok"] = false;
		result["errors"] = errors;
		return result;
	}
	m_preloaded.clear(); // lane PERF-1 r2: a new setup queues the new data's textures
	m_preloadQueue.clear();
	m_preloadNext = 0;
	m_preloadQueued = false;
	m_playback = std::make_unique<FXPlayback>(*fs->archive(), RandomAlgorithm::RotWK_GameDat_LCG);
	for (const std::string &e : m_playback->loadRetailData())
	{
		errors.push_back(toGodot(e));
	}
	m_renderRandom = std::make_unique<W3DClientRandom>(RandomAlgorithm::RotWK_GameDat_LCG);
	m_renderRandom->seed(1);
	m_builder = std::make_unique<ParticleDrawBuilder>(*m_renderRandom);
	m_quad = makeQuadMesh();
	result["ok"] = errors.is_empty();
	result["errors"] = errors;
	result["particle_systems"] = (int64_t)m_playback->particleTemplates().templateCount();
	result["fx_lists"] = (int64_t)m_playback->fxLists().listCount();
	return result;
}

PackedStringArray FXPlayer::list_particle_systems() const
{
	PackedStringArray out;
	if (m_playback)
	{
		for (const std::string &n : m_playback->particleTemplates().templateNames())
		{
			out.push_back(toGodot(n));
		}
	}
	return out;
}

PackedStringArray FXPlayer::list_fx_lists() const
{
	PackedStringArray out;
	if (m_playback)
	{
		for (const std::string &n : m_playback->fxLists().listNames())
		{
			out.push_back(toGodot(n));
		}
	}
	return out;
}

int64_t FXPlayer::play_particle_system(const String &name, const Vector3 &position)
{
	if (!m_playback)
	{
		return 0;
	}
	return (int64_t)m_playback->playParticleSystem(toNative(name), toSage(position));
}

bool FXPlayer::play_fx_list(const String &name, const Vector3 &position, bool as_object)
{
	return m_playback && m_playback->playFXList(toNative(name), toSage(position), as_object);
}

Array FXPlayer::define_ini(const String &name, const String &text)
{
	Array errors;
	if (!m_playback)
	{
		errors.push_back("FXPlayer.setup has not run");
		return errors;
	}
	for (const std::string &e : m_playback->loadIniText(toNative(name), toNative(text)))
	{
		errors.push_back(toGodot(e));
	}
	return errors;
}

Dictionary FXPlayer::play_w3d_emitter(const String &name, const Vector3 &position)
{
	Dictionary d;
	if (!m_playback)
	{
		d["ok"] = false;
		d["error"] = "FXPlayer.setup has not run";
		return d;
	}
	std::string error;
	ParticleEmitterInstance *e = m_playback->playW3DEmitter(toNative(name), toSage(position), &error);
	d["ok"] = e != nullptr;
	d["error"] = toGodot(error);
	return d;
}

void FXPlayer::clear()
{
	if (m_playback)
	{
		m_playback->emitters().clear();
		m_playback->particles().reset();
		m_playback->clearEvents();
	}
	rebuild();
}

void FXPlayer::seed(int64_t seed)
{
	if (m_playback)
	{
		m_playback->seedClientRandom((std::uint32_t)seed);
	}
}

void FXPlayer::set_max_particles(int64_t count)
{
	if (m_playback)
	{
		m_playback->particles().settings().maxParticleCount = (std::uint32_t)count;
	}
}

void FXPlayer::step_once()
{
	if (!m_playback)
	{
		return;
	}
	const auto t0 = std::chrono::steady_clock::now();
	m_playback->step();
	m_stepUs = usSince(t0);
}

void FXPlayer::advance(double delta)
{
	if (!m_playback)
	{
		return;
	}
	if (!m_paused)
	{
		m_accumulator += delta;
		int steps = 0;
		const double dt = 1.0 / 30.0; // RW: one particle step per Display::draw at the 30 FPS limit
		while (m_accumulator >= dt && steps < 4)
		{
			step_once();
			m_accumulator -= dt;
			++steps;
		}
		if (steps == 4)
		{
			m_accumulator = 0.0; // never spiral
		}
	}
	rebuild();
}

Ref<Texture2D> FXPlayer::loadTexture(const std::string &name)
{
	auto it = m_textures.find(name);
	if (it != m_textures.end())
	{
		return it->second;
	}
	auto pre = m_preloaded.find(name);
	if (pre != m_preloaded.end())
	{
		// lane PERF-1 r2: decoded ahead (preload_textures); its miss, if any, is reported now, at the first use, as a load here would have
		if (!pre->second.missing.empty())
		{
			m_missing.push_back(pre->second.missing);
		}
		Ref<Texture2D> out = pre->second.texture;
		m_preloaded.erase(pre);
		m_textures[name] = out;
		return out;
	}
	std::string missing;
	Ref<Texture2D> out = decodeTexture(name, missing);
	if (!missing.empty())
	{
		m_missing.push_back(missing);
	}
	m_textures[name] = out;
	return out;
}

int64_t FXPlayer::preload_textures(double budget_ms)
{
	if (!m_playback || m_fs.is_null() || !m_fs->archive())
	{
		return 0;
	}
	if (!m_preloadQueued)
	{
		// every ParticleName of the particle system templates, in definition order, once
		m_preloadQueued = true;
		const FXParticleSystemTemplateStore &store = m_playback->particles().templates();
		std::set<std::string> seen;
		for (const std::string &n : store.templateNames())
		{
			const ParticleSystemTemplate *t = store.findTemplate(n);
			const std::string &tex = t ? t->info().m_particleTypeName : std::string();
			if (!tex.empty() && seen.insert(tex).second)
			{
				m_preloadQueue.push_back(tex);
			}
		}
	}
	const auto t0 = std::chrono::steady_clock::now();
	while (m_preloadNext < m_preloadQueue.size() && (budget_ms <= 0.0 || usSince(t0) < budget_ms * 1000.0))
	{
		const std::string &name = m_preloadQueue[m_preloadNext++];
		if (m_textures.count(name) || m_preloaded.count(name))
		{
			continue;
		}
		Preloaded p;
		p.texture = decodeTexture(name, p.missing);
		m_preloaded.emplace(name, std::move(p));
	}
	return (int64_t)(m_preloadQueue.size() - m_preloadNext);
}

Ref<Texture2D> FXPlayer::decodeTexture(const std::string &name, std::string &missing)
{
	Ref<Texture2D> out;
	ArchiveFileSystem &fs = *m_fs->archive();
	const ParticleTextureResolution res = ResolveParticleTexture(name, [&](const std::string &p) { return fs.doesFileExist(p); });
	if (!res.Found)
	{
		missing = name;
	}
	else
	{
		std::vector<std::uint8_t> bytes;
		std::string error;
		if (!fs.readFile(res.Path, bytes, &error))
		{
			missing = name + " (" + error + ")";
		}
		else
		{
			PackedByteArray buffer;
			buffer.resize((int64_t)bytes.size());
			if (!bytes.empty())
			{
				std::memcpy(buffer.ptrw(), bytes.data(), bytes.size());
			}
			Ref<Image> image;
			image.instantiate();
			Error err = FAILED;
			switch (res.Kind)
			{
			case ParticleTextureResolution::DDS: err = image->load_dds_from_buffer(buffer); break;
			case ParticleTextureResolution::TGA: err = image->load_tga_from_buffer(buffer); break;
			case ParticleTextureResolution::JPG: err = image->load_jpg_from_buffer(buffer); break;
			default: break;
			}
			if (err != OK || image->is_empty())
			{
				missing = name + " (could not decode " + res.Path + ")";
			}
			else
			{
				if (!image->has_mipmaps() && !image->is_compressed())
				{
					image->generate_mipmaps();
				}
				out = ImageTexture::create_from_image(image);
			}
		}
	}
	if (out.is_null())
	{
		// retail draws its default texture when a name does not resolve (RW 0x532962): a 1x1 white; the miss is reported in get_stats
		Ref<Image> white = Image::create(1, 1, false, Image::FORMAT_RGBA8);
		white->fill(Color(1, 1, 1, 1));
		out = ImageTexture::create_from_image(white);
	}
	return out;
}

Ref<Shader> FXPlayer::shaderFor(const std::string &key, const std::string &code)
{
	auto it = m_shaders.find(key);
	if (it != m_shaders.end())
	{
		return it->second;
	}
	W3D_Ensure_Light_Globals(); // lane RENDER-4: the particle shaders name the fog globals (w3d_fog / w3d_fog_color): they must exist before they compile
	Ref<Shader> s;
	s.instantiate();
	s->set_code(toGodot(code));
	m_shaders[key] = s;
	return s;
}

FXPlayer::Batch &FXPlayer::batchSlot(size_t index, const std::string &key, int kind)
{
	if (index >= m_batches.size())
	{
		m_batches.resize(index + 1);
	}
	Batch &b = m_batches[index];
	if (b.kind != kind || b.key != key)
	{
		// the slot changes identity: drop its nodes and rebuild them for the new key
		if (b.meshNode)
		{
			b.meshNode->queue_free();
			b.meshNode = nullptr;
		}
		if (b.multiNode)
		{
			b.multiNode->queue_free();
			b.multiNode = nullptr;
		}
		b.multi.unref();
		b.mesh.unref();
		b.material.unref();
		b.kind = kind;
		b.key = key;
		b.lastBuffer = PackedFloat32Array();
		b.haveGpuParams = false;
		b.lastTexture.unref();
		b.haveOrder = false;
	}
	return b;
}

// lane RENDER-4: the per-batch summary (see set_batch_summary); presentation diagnostics only
void FXPlayer::summarize(const std::vector<DrawBatch> &batches)
{
	m_summary.clear();
	for (const DrawBatch &b : batches)
	{
		Dictionary e;
		e["template"] = toGodot(b.templateName);
		e["kind"] = b.kind == DrawBatch::SPRITES ? "sprites" : (b.kind == DrawBatch::GPU ? "gpu" : "mesh");
		e["shader"] = (int64_t)b.shader;
		e["texture"] = toGodot(b.texture);
		e["deferred"] = b.deferred;
		e["ground_aligned"] = b.groundAligned;
		double cx = 0, cy = 0, cz = 0, size = 0, maxSize = 0, r = 0, g = 0, bl = 0, a = 0, maxA = 0;
		size_t n = 0;
		if (b.kind == DrawBatch::SPRITES)
		{
			for (const SpriteInstance &s : b.sprites)
			{
				cx += s.x; cy += s.y; cz += s.z;
				size += s.size; maxSize = std::max(maxSize, (double)s.size);
				r += s.r; g += s.g; bl += s.b; a += s.a; maxA = std::max(maxA, (double)s.a);
				++n;
			}
		}
		else if (b.kind == DrawBatch::GPU)
		{
			for (const GpuInstance &s : b.gpu)
			{
				cx += s.x; cy += s.y; cz += s.z;
				++n;
			}
		}
		else
		{
			for (const MeshVertex &v : b.vertices)
			{
				cx += v.x; cy += v.y; cz += v.z;
				r += v.r; g += v.g; bl += v.b; a += v.a; maxA = std::max(maxA, (double)v.a);
				++n;
			}
		}
		e["count"] = (int64_t)n;
		const double inv = n ? 1.0 / (double)n : 0.0;
		e["centre"] = Vector3((real_t)(cx * inv), (real_t)(cy * inv), (real_t)(cz * inv));
		e["mean_size"] = size * inv;
		e["max_size"] = maxSize;
		e["mean_color"] = Color((float)(r * inv), (float)(g * inv), (float)(bl * inv), (float)(a * inv));
		e["max_alpha"] = maxA;
		m_summary.push_back(e);
	}
}

void FXPlayer::rebuild()
{
	if (!m_playback || !m_builder)
	{
		return;
	}
	DrawCamera cam;
	if (Viewport *vp = get_viewport())
	{
		if (Camera3D *c = vp->get_camera_3d())
		{
			const Transform3D t = c->get_global_transform();
			cam.position = toSage(t.origin);
			cam.right = toSage(t.basis.get_column(0));
			cam.up = toSage(t.basis.get_column(1));
			cam.forward = toSage(-t.basis.get_column(2));
		}
	}
	const auto t0 = std::chrono::steady_clock::now();
	std::vector<DrawBatch> batches = m_builder->build(m_playback->particles(), cam);
	m_emitterStops = BuildEmitterBatches(m_playback->emitters(), cam, batches);
	m_buildUs = usSince(t0);
	if (m_summaryEnabled)
	{
		summarize(batches);
	}
	const auto t1 = std::chrono::steady_clock::now();
	upload(batches);
	m_uploadUs = usSince(t1);
}

// lane PERF-1: upload() compares GpuDrawParams bytewise; that is only the parameters' equality while the struct is 53 four-byte members without padding
static_assert(sizeof(FXParticleSystem::GpuDrawParams) == 53 * 4 && std::is_trivially_copyable<FXParticleSystem::GpuDrawParams>::value,
	"GpuDrawParams changed: review the bytewise comparison in FXPlayer::upload");

void FXPlayer::setBufferIfChanged(Batch &b, const PackedFloat32Array &buf)
{
	if (b.lastBuffer.size() == buf.size() && (buf.is_empty() || std::memcmp(b.lastBuffer.ptr(), buf.ptr(), (size_t)buf.size() * sizeof(float)) == 0))
	{
		return;
	}
	b.multi->set_buffer(buf);
	b.lastBuffer = buf;
}

void FXPlayer::applyOrder(Batch &b, const BatchOrder &order)
{
	if (b.haveOrder && b.lastOrder.priority == order.priority && std::memcmp(&b.lastOrder.sortOffset, &order.sortOffset, sizeof(float)) == 0)
	{
		return;
	}
	b.haveOrder = true;
	b.lastOrder = order;
	b.material->set_render_priority(order.priority);
	if (b.multiNode)
	{
		b.multiNode->set_sorting_offset(order.sortOffset);
	}
	if (b.meshNode)
	{
		b.meshNode->set_sorting_offset(order.sortOffset);
	}
}

void FXPlayer::upload(const std::vector<DrawBatch> &batches)
{
	m_sprites = m_gpu = m_vertices = 0;
	m_batchCount = batches.size();
	// Compositing order. The builder's batch order is the draw order (list order, then the deferred SortLevel 1 systems in reverse); alpha and additive
	// blends do not commute, so only CONSECUTIVE compatible sprite batches are merged into one MultiMesh (a smoke / fire / smoke sequence stays three
	// nodes). The deferred flag is part of the key and the deferred batches keep their particle order.
	struct Merged
	{
		std::string key;
		int kind;
		int shader;
		bool ground;
		bool volume;
		bool deferred;
		std::string texture;
		std::vector<const DrawBatch *> parts;
	};
	std::vector<Merged> merged;
	for (const DrawBatch &b : batches)
	{
		if (b.kind == DrawBatch::SPRITES)
		{
			const std::string key = "S|" + b.texture + "|" + std::to_string(b.shader) + "|" + (b.groundAligned ? "g" : "b") + (b.volume ? "v" : "") + "|" + (b.deferred ? "d" : "n");
			if (!merged.empty() && merged.back().key == key)
			{
				merged.back().parts.push_back(&b);
			}
			else
			{
				merged.push_back({ key, DrawBatch::SPRITES, b.shader, b.groundAligned, b.volume, b.deferred, b.texture, { &b } });
			}
		}
		else
		{
			merged.push_back({ "X|" + std::to_string(b.system) + "|" + std::to_string((int)b.kind) + "|" + std::to_string(b.shader), (int)b.kind, b.shader, false, false, b.deferred, b.texture, { &b } });
		}
	}
	m_sortedBatchSeen = false;
	Transform3D camXf;
	bool haveCam = false;
	if (Viewport *vp = get_viewport())
	{
		if (Camera3D *c = vp->get_camera_3d())
		{
			camXf = c->get_global_transform();
			haveCam = true;
		}
	}
	size_t slot = 0;
	for (const Merged &m : merged)
	{
		const BatchOrder order = OrderForBatch(slot, merged.size());
		Batch &slotRef = batchSlot(slot++, m.key, m.kind);
		Batch *bp = &slotRef;
		const Ref<Texture2D> tex = loadTexture(m.texture);
		if (m.kind == DrawBatch::SPRITES)
		{
			if (!bp->multiNode)
			{
				bp->material.instantiate();
				bp->material->set_shader(shaderFor("sprite|" + std::to_string(m.shader) + "|" + (m.ground ? "g" : "b") + (m_soft ? "|soft" : ""), spriteShader(m.shader, m.ground, m_soft)));
				bp->material->set_shader_parameter("tex", tex);
				bp->multi.instantiate();
				bp->multi->set_transform_format(MultiMesh::TRANSFORM_3D);
				bp->multi->set_use_colors(true);
				bp->multi->set_use_custom_data(true);
				bp->multi->set_mesh(m_quad);
				bp->multiNode = memnew(MultiMeshInstance3D);
				bp->multiNode->set_multimesh(bp->multi);
				bp->multiNode->set_material_override(bp->material);
				bp->multiNode->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
				bp->multiNode->set_custom_aabb(AABB(Vector3(-1e5f, -1e5f, -1e5f), Vector3(2e5f, 2e5f, 2e5f)));
				add_child(bp->multiNode);
			}
			applyOrder(*bp, order);
			// gather: VOLUME_PARTICLE re-submits the set in 6 layers shifted toward the camera (RW 0x57efb0, depth 6, 0.1 / depth)
			std::vector<const SpriteInstance *> all;
			for (const DrawBatch *d : m.parts)
			{
				for (const SpriteInstance &s : d->sprites)
				{
					all.push_back(&s);
				}
			}
			const int layers = m.volume ? 6 : 1;
			const bool sortBack = SpriteBatchDepthSorted(m.shader, m.deferred);
			if (sortBack && haveCam)
			{
				m_sortedBatchSeen = true;
				const Vector3 cp = camXf.origin;
				std::stable_sort(all.begin(), all.end(), [&](const SpriteInstance *a, const SpriteInstance *b) {
					const Vector3 pa = toGodotSpace(Coord3D{ a->x, a->y, a->z }), pb = toGodotSpace(Coord3D{ b->x, b->y, b->z });
					return (pa - cp).length_squared() > (pb - cp).length_squared();
				});
			}
			const size_t count = all.size() * (size_t)layers;
			if ((size_t)bp->multi->get_instance_count() != count)
			{
				bp->multi->set_instance_count((int)count);
			}
			PackedFloat32Array buf;
			buf.resize((int64_t)(count * 20));
			float *w = buf.ptrw();
			size_t n = 0;
			for (int layer = 0; layer < layers; ++layer)
			{
				for (const SpriteInstance *s : all)
				{
					Vector3 p = toGodotSpace(Coord3D{ s->x, s->y, s->z });
					if (layer > 0 && haveCam)
					{
						const Vector3 toCam = (camXf.origin - p).normalized();
						p += toCam * ((float)layer * all.front()->size * (0.1f / 6.0f));
					}
					float *o = w + n * 20;
					o[0] = 1; o[1] = 0; o[2] = 0; o[3] = p.x;
					o[4] = 0; o[5] = 1; o[6] = 0; o[7] = p.y;
					o[8] = 0; o[9] = 0; o[10] = 1; o[11] = p.z;
					o[12] = s->r; o[13] = s->g; o[14] = s->b; o[15] = s->a;
					o[16] = s->size; o[17] = (float)s->angleByte; o[18] = 0; o[19] = 0;
					++n;
				}
			}
			setBufferIfChanged(*bp, buf);
			m_sprites += count;
		}
		else if (m.kind == DrawBatch::GPU)
		{
			const DrawBatch &d = *m.parts[0];
			if (!bp->multiNode)
			{
				bp->material.instantiate();
				bp->material->set_shader(shaderFor("gpu|" + std::to_string(m.shader) + (m_soft ? "|soft" : ""), gpuShader(d.gpuParams.shaderType, m_soft)));
				bp->multi.instantiate();
				bp->multi->set_transform_format(MultiMesh::TRANSFORM_3D);
				bp->multi->set_use_colors(true);
				bp->multi->set_use_custom_data(true);
				// the GPU quad: positions unused, UV2.x = corner index 0..3, UV = unused
				if (m_gpuQuad.is_null())
				{
					PackedVector3Array v;
					PackedVector2Array uv2;
					for (int k = 0; k < 4; ++k)
					{
						v.push_back(Vector3(0, 0, 0));
						uv2.push_back(Vector2((float)k, 0.0f));
					}
					PackedInt32Array idx;
					const int ids[6] = { 0, 1, 2, 0, 2, 3 }; // RW index buffer (b, b+1, b+2), (b, b+2, b+3)
					for (int i : ids)
					{
						idx.push_back(i);
					}
					Array arrays;
					arrays.resize(Mesh::ARRAY_MAX);
					arrays[Mesh::ARRAY_VERTEX] = v;
					arrays[Mesh::ARRAY_TEX_UV2] = uv2;
					arrays[Mesh::ARRAY_INDEX] = idx;
					m_gpuQuad.instantiate();
					m_gpuQuad->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
					m_gpuQuad->set_custom_aabb(AABB(Vector3(-1e5f, -1e5f, -1e5f), Vector3(2e5f, 2e5f, 2e5f)));
				}
				bp->multi->set_mesh(m_gpuQuad);
				bp->multiNode = memnew(MultiMeshInstance3D);
				bp->multiNode->set_multimesh(bp->multi);
				bp->multiNode->set_material_override(bp->material);
				bp->multiNode->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
				bp->multiNode->set_custom_aabb(AABB(Vector3(-1e5f, -1e5f, -1e5f), Vector3(2e5f, 2e5f, 2e5f)));
				add_child(bp->multiNode);
			}
			applyOrder(*bp, order);
			const GpuDrawParams &gp = d.gpuParams;
			Material *mat = bp->material.ptr();
			// lane PERF-1: GpuDrawParams is all 4-byte floats and ints (no padding): equal bytes are the same parameters, which the material already has
			if (bp->lastTexture != tex)
			{
				bp->material->set_shader_parameter("tex", tex);
				bp->lastTexture = tex;
			}
			if (!bp->haveGpuParams || std::memcmp(&bp->lastGpuParams, &gp, sizeof(GpuDrawParams)) != 0)
			{
				bp->haveGpuParams = true;
				bp->lastGpuParams = gp;
				bp->material->set_shader_parameter("tex_layout", Vector2(gp.framesPerRow, gp.totalFrames));
				bp->material->set_shader_parameter("speed_mult", gp.speedMultiplier);
				Array ck;
				for (int i = 0; i < 4; ++i)
				{
					ck.push_back(Color(gp.colorKeys[i][0], gp.colorKeys[i][1], gp.colorKeys[i][2], gp.colorKeys[i][3]));
				}
				bp->material->set_shader_parameter("color_keys", ck);
				bp->material->set_shader_parameter("time_keys", Color(gp.timeKeys[0], gp.timeKeys[1], gp.timeKeys[2], gp.timeKeys[3]));
				bp->material->set_shader_parameter("color_scale", Vector2(gp.colorScale[0], gp.colorScale[1]));
				bp->material->set_shader_parameter("gravity", Vector3(gp.gravity[0], gp.gravity[1], gp.gravity[2]));
				bp->material->set_shader_parameter("drift", Vector3(gp.drift[0], gp.drift[1], gp.drift[2]));
				bp->material->set_shader_parameter("vel_damp", Vector2(gp.velocityDamping[0], gp.velocityDamping[1]));
				bp->material->set_shader_parameter("size_u", Vector2(gp.size[0], gp.size[1]));
				bp->material->set_shader_parameter("size_rate", Vector2(gp.sizeRate[0], gp.sizeRate[1]));
				bp->material->set_shader_parameter("size_rate_damp", Vector2(gp.sizeRateDamping[0], gp.sizeRateDamping[1]));
				bp->material->set_shader_parameter("xy_rot", Vector2(gp.xyRotation[0], gp.xyRotation[1]));
				bp->material->set_shader_parameter("xy_rot_rate", Vector2(gp.xyRotationRate[0], gp.xyRotationRate[1]));
				bp->material->set_shader_parameter("xy_rot_damp", Vector2(gp.xyRotationDamping[0], gp.xyRotationDamping[1]));
				bp->material->set_shader_parameter("z_rot", Vector2(gp.zRotation[0], gp.zRotation[1]));
				bp->material->set_shader_parameter("z_rot_rate", Vector2(gp.zRotationRate[0], gp.zRotationRate[1]));
				bp->material->set_shader_parameter("z_rot_damp", Vector2(gp.zRotationDamping[0], gp.zRotationDamping[1]));
				bp->material->set_shader_parameter("now", gp.now);
			}
			(void)mat;
			const size_t count = d.gpu.size();
			if ((size_t)bp->multi->get_instance_count() != count)
			{
				bp->multi->set_instance_count((int)count);
			}
			PackedFloat32Array buf;
			buf.resize((int64_t)(count * 20));
			float *w = buf.ptrw();
			for (size_t i = 0; i < count; ++i)
			{
				const GpuInstance &g = d.gpu[i];
				const Vector3 p = toGodotSpace(Coord3D{ g.x, g.y, g.z });
				// the shader works in SAGE space for the kinematics: the transform origin carries the SAGE position, converted there
				float *o = w + i * 20;
				o[0] = 1; o[1] = 0; o[2] = 0; o[3] = g.x;
				o[4] = 0; o[5] = 1; o[6] = 0; o[7] = g.y;
				o[8] = 0; o[9] = 0; o[10] = 1; o[11] = g.z;
				o[12] = g.life; o[13] = g.random; o[14] = 0; o[15] = 0;
				o[16] = g.vx; o[17] = g.vy; o[18] = g.vz; o[19] = g.birth;
				(void)p;
			}
			setBufferIfChanged(*bp, buf);
			m_gpu += count;
		}
		else
		{
			const DrawBatch &d = *m.parts[0];
			if (!bp->meshNode)
			{
				bp->material.instantiate();
				bp->material->set_shader(shaderFor("mesh|" + std::to_string(m.shader) + (m_soft ? "|soft" : ""), meshShader(m.shader, m_soft)));
				bp->meshNode = memnew(MeshInstance3D);
				bp->meshNode->set_material_override(bp->material);
				bp->meshNode->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
				add_child(bp->meshNode);
			}
			if (bp->lastTexture != tex)
			{
				bp->material->set_shader_parameter("tex", tex);
				bp->lastTexture = tex;
			}
			applyOrder(*bp, order);
			PackedVector3Array v;
			PackedVector2Array uv;
			PackedColorArray col;
			PackedInt32Array idx;
			v.resize((int64_t)d.vertices.size());
			uv.resize((int64_t)d.vertices.size());
			col.resize((int64_t)d.vertices.size());
			for (size_t i = 0; i < d.vertices.size(); ++i)
			{
				const MeshVertex &mv = d.vertices[i];
				v.set((int64_t)i, toGodotSpace(Coord3D{ mv.x, mv.y, mv.z }));
				uv.set((int64_t)i, Vector2(mv.u, mv.v));
				col.set((int64_t)i, Color(mv.r, mv.g, mv.b, mv.a));
			}
			idx.resize((int64_t)d.indices.size());
			for (size_t i = 0; i < d.indices.size(); ++i)
			{
				idx.set((int64_t)i, (int32_t)d.indices[i]);
			}
			Array arrays;
			arrays.resize(Mesh::ARRAY_MAX);
			arrays[Mesh::ARRAY_VERTEX] = v;
			arrays[Mesh::ARRAY_TEX_UV] = uv;
			arrays[Mesh::ARRAY_COLOR] = col;
			arrays[Mesh::ARRAY_INDEX] = idx;
			Ref<ArrayMesh> am;
			am.instantiate();
			am->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
			am->set_custom_aabb(AABB(Vector3(-1e5f, -1e5f, -1e5f), Vector3(2e5f, 2e5f, 2e5f)));
			bp->meshNode->set_mesh(am);
			m_vertices += d.vertices.size();
		}
	}
	// hide / free the unused slots
	for (size_t i = slot; i < m_batches.size(); ++i)
	{
		Batch &b = m_batches[i];
		if (b.multiNode)
		{
			b.multiNode->queue_free();
			b.multiNode = nullptr;
		}
		if (b.meshNode)
		{
			b.meshNode->queue_free();
			b.meshNode = nullptr;
		}
		b.key.clear();
		b.kind = -1;
	}
	if (slot < m_batches.size())
	{
		m_batches.resize(slot);
	}
}

Dictionary FXPlayer::get_stats() const
{
	Dictionary d;
	if (m_playback)
	{
		d["frame"] = (int64_t)m_playback->clientFrame();
		d["systems"] = (int64_t)m_playback->particles().systemCount();
		d["particles"] = (int64_t)m_playback->particles().particleCount();
		d["emitters"] = (int64_t)m_playback->emitters().emitters().size();
	}
	d["batches"] = (int64_t)m_batchCount;
	d["textures_preload_queue"] = (int64_t)m_preloadQueue.size(); // lane PERF-1 r2: particle textures to preload, decoded ahead and not used yet
	d["textures_preloaded_unused"] = (int64_t)m_preloaded.size();
	d["sprites"] = (int64_t)m_sprites;
	d["gpu_instances"] = (int64_t)m_gpu;
	d["mesh_vertices"] = (int64_t)m_vertices;
	d["step_us"] = m_stepUs;
	d["build_us"] = m_buildUs;
	d["upload_us"] = m_uploadUs;
	PackedStringArray missing;
	for (const std::string &m : m_missing)
	{
		missing.push_back(toGodot(m));
	}
	d["missing_textures"] = missing;
	d["soft_particles"] = m_soft; // lane FX-3 round 2
	return d;
}

PackedStringArray FXPlayer::get_unverified() const
{
	PackedStringArray out;
	if (m_playback)
	{
		for (const std::string &s : m_playback->unverified())
		{
			out.push_back(toGodot(s));
		}
	}
	if (m_builder)
	{
		for (const std::string &s : m_builder->unverified())
		{
			out.push_back(toGodot(s));
		}
	}
	for (const std::string &s : m_emitterStops)
	{
		out.push_back(toGodot(s));
	}
	if (m_playback)
	{
		out.push_back(toGodot(std::string("S-198: ") + FogOmissionText()));
		// lane RENDER-3: the gamma-space compositing of the transparent pass and its inferred remainder (S-831)
		out.append_array(GammaComposite::unverified());
		if (m_sortedBatchSeen)
		{
			out.push_back(toGodot(std::string("S-198: ") + SortApproximationText()));
		}
	}
	return out;
}

Array FXPlayer::get_events() const
{
	Array out;
	if (m_playback)
	{
		for (const FXPlayback::Event &e : m_playback->events())
		{
			Dictionary d;
			d["kind"] = toGodot(e.kind);
			d["detail"] = toGodot(e.detail);
			d["frame"] = (int64_t)e.frame;
			out.push_back(d);
		}
	}
	return out;
}

} // namespace godot
