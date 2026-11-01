// OpenBFME. GPL-3.0. Retail's glow pass on the 3D view (lane GLOW-1, S-5231): see GodotGlowEffect.h.

#include "GodotDevice/GodotGlowEffect.h"

#include "GameEngineDevice/W3DDevice/GameClient/W3DViewDrawPostPass.h"
#include "GodotDevice/GodotGammaComposite.h"
#include "GodotDevice/GodotPostEffects.h"

#include <godot_cpp/classes/compositor.hpp>
#include <godot_cpp/classes/environment.hpp>
#include <godot_cpp/classes/rd_sampler_state.hpp>
#include <godot_cpp/classes/rd_shader_source.hpp>
#include <godot_cpp/classes/rd_shader_spirv.hpp>
#include <godot_cpp/classes/rd_texture_format.hpp>
#include <godot_cpp/classes/rd_texture_view.hpp>
#include <godot_cpp/classes/rd_uniform.hpp>
#include <godot_cpp/classes/render_scene_buffers_rd.hpp>
#include <godot_cpp/classes/rendering_device.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/uniform_set_cache_rd.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/classes/window.hpp>
#include <godot_cpp/classes/world_environment.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/typed_array.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace godot
{

namespace
{
// Glow.pso / Glow.vso on the square glow targets (ScreenHilightFilter.h has the decoded shaders). The passes work on the displayed (gamma) values: the glow
// scene texture holds what Godot displays for the glow layer, the colour buffer is encoded with the sRGB curve first, as GodotPostEffects does.
const char *kResample = R"GLSL(
#version 450
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
layout(rgba8, set = 0, binding = 0) uniform restrict writeonly image2D dst;
layout(set = 0, binding = 1) uniform sampler2D scene;
layout(push_constant, std430) uniform Params {
	float size;
	float pad0;
	float pad1;
	float pad2;
} params;
void main() {
	ivec2 g = ivec2(gl_GlobalInvocationID.xy);
	if (g.x >= int(params.size) || g.y >= int(params.size)) {
		return;
	}
	// point sampling at the centre of the texel of the square target (S-5231)
	vec3 c = textureLod(scene, (vec2(g) + 0.5) / params.size, 0.0).rgb;
	imageStore(dst, g, vec4(c, 1.0));
}
)GLSL";

const char *kBlur = R"GLSL(
#version 450
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
layout(rgba8, set = 0, binding = 0) uniform restrict writeonly image2D dst;
layout(set = 0, binding = 1) uniform sampler2D src;
layout(set = 0, binding = 2, std430) restrict readonly buffer Kernel {
	vec4 tap[]; // weight (z * GlowIntensity), offset u, offset v, unused
} kernel;
layout(push_constant, std430) uniform Params {
	float size;
	int groups;
	float pad0;
	float pad1;
} params;
float store8(float v) {
	return floor(clamp(v, 0.0, 1.0) * 255.0 + 0.5) / 255.0;
}
vec3 store8(vec3 v) {
	return vec3(store8(v.r), store8(v.g), store8(v.b));
}
void main() {
	ivec2 g = ivec2(gl_GlobalInvocationID.xy);
	if (g.x >= int(params.size) || g.y >= int(params.size)) {
		return;
	}
	vec2 uv = (vec2(g) + 0.5) / params.size;
	vec3 acc = vec3(0.0);
	for (int k = 0; k < params.groups; ++k) {
		// Glow.pso (ps_1_1): r0 = t0 * c0; r0 = t1 * c1 + r0; r0 = t2 * c2 + r0; r0 = t3 * c3 + r0, every register saturating
		vec3 r = vec3(0.0);
		for (int i = 0; i < 4; ++i) {
			vec4 t = kernel.tap[k * 4 + i];
			vec3 s = textureLod(src, uv + t.yz, 0.0).rgb;
			r = clamp(s * clamp(t.x, -1.0, 1.0) + r, vec3(-1.0), vec3(1.0));
		}
		r = store8(r);
		if (k == 0) {
			acc = r; // the first group is stored without blending
		} else {
			acc = store8(r + acc * (1.0 - r)); // ONE / INVSRCCOLOR
		}
	}
	imageStore(dst, g, vec4(acc, 1.0));
}
)GLSL";

const char *kComposite = R"GLSL(
#version 450
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
layout(rgba16f, set = 0, binding = 0) uniform restrict image2D color_image;
layout(set = 0, binding = 1) uniform sampler2D glow;
layout(push_constant, std430) uniform Params {
	vec2 size;
	vec2 pad;
} params;
vec3 to_gamma(vec3 c) {
	c = max(c, vec3(0.0));
	return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), c));
}
vec3 to_linear(vec3 c) {
	c = max(c, vec3(0.0));
	return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c));
}
void main() {
	ivec2 uv = ivec2(gl_GlobalInvocationID.xy);
	if (uv.x >= int(params.size.x) || uv.y >= int(params.size.y)) {
		return;
	}
	vec4 c = imageLoad(color_image, uv);
	vec3 frame = clamp(to_gamma(c.rgb), 0.0, 1.0);
	vec3 g = textureLod(glow, (vec2(uv) + 0.5) / params.size, 0.0).rgb;
	// SRCBLEND ONE, DESTBLEND INVSRCCOLOR (RW 0x4FA860)
	c.rgb = to_linear(clamp(g + frame * (1.0 - g), 0.0, 1.0));
	imageStore(color_image, uv, c);
}
)GLSL";

bool compile(RenderingDevice *rd, const char *source, RID &shader, RID &pipeline, std::string &why)
{
	Ref<RDShaderSource> src;
	src.instantiate();
	src->set_language(RenderingDevice::SHADER_LANGUAGE_GLSL);
	src->set_stage_source(RenderingDevice::SHADER_STAGE_COMPUTE, String(source));
	Ref<RDShaderSPIRV> spirv = rd->shader_compile_spirv_from_source(src);
	if (spirv.is_null() || !spirv->get_stage_compile_error(RenderingDevice::SHADER_STAGE_COMPUTE).is_empty())
	{
		why = spirv.is_null() ? "no SPIR-V" : std::string(spirv->get_stage_compile_error(RenderingDevice::SHADER_STAGE_COMPUTE).utf8().get_data());
		return false;
	}
	shader = rd->shader_create_from_spirv(spirv);
	pipeline = shader.is_valid() ? rd->compute_pipeline_create(shader) : RID();
	if (!pipeline.is_valid())
	{
		why = "the compute pipeline could not be created";
		return false;
	}
	return true;
}

RID makeSampler(RenderingDevice *rd, bool linear)
{
	Ref<RDSamplerState> ss;
	ss.instantiate();
	const RenderingDevice::SamplerFilter f = linear ? RenderingDevice::SAMPLER_FILTER_LINEAR : RenderingDevice::SAMPLER_FILTER_NEAREST;
	ss->set_min_filter(f);
	ss->set_mag_filter(f);
	ss->set_mip_filter(f);
	ss->set_repeat_u(RenderingDevice::SAMPLER_REPEAT_MODE_CLAMP_TO_EDGE);
	ss->set_repeat_v(RenderingDevice::SAMPLER_REPEAT_MODE_CLAMP_TO_EDGE);
	ss->set_repeat_w(RenderingDevice::SAMPLER_REPEAT_MODE_CLAMP_TO_EDGE);
	return rd->sampler_create(ss);
}

Ref<RDUniform> imageUniform(int binding, const RID &texture)
{
	Ref<RDUniform> u;
	u.instantiate();
	u->set_uniform_type(RenderingDevice::UNIFORM_TYPE_IMAGE);
	u->set_binding(binding);
	u->add_id(texture);
	return u;
}

Ref<RDUniform> samplerUniform(int binding, const RID &sampler, const RID &texture)
{
	Ref<RDUniform> u;
	u.instantiate();
	u->set_uniform_type(RenderingDevice::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE);
	u->set_binding(binding);
	u->add_id(sampler);
	u->add_id(texture);
	return u;
}

RID makeSquare(RenderingDevice *rd, int width)
{
	Ref<RDTextureFormat> fmt;
	fmt.instantiate();
	fmt->set_format(RenderingDevice::DATA_FORMAT_R8G8B8A8_UNORM); // A8R8G8B8 targets: every pass result is 8 bit
	fmt->set_width((uint32_t)width);
	fmt->set_height((uint32_t)width);
	fmt->set_usage_bits(RenderingDevice::TEXTURE_USAGE_SAMPLING_BIT | RenderingDevice::TEXTURE_USAGE_STORAGE_BIT);
	Ref<RDTextureView> view;
	view.instantiate();
	return rd->texture_create(fmt, view);
}

void dispatch(RenderingDevice *rd, const RID &pipeline, const RID &set, const PackedFloat32Array &push, int groupsX, int groupsY)
{
	const PackedByteArray bytes = push.to_byte_array();
	const int64_t list = rd->compute_list_begin();
	rd->compute_list_bind_compute_pipeline(list, pipeline);
	rd->compute_list_bind_uniform_set(list, set, 0);
	rd->compute_list_set_push_constant(list, bytes, (uint32_t)bytes.size());
	rd->compute_list_dispatch(list, (uint32_t)groupsX, (uint32_t)groupsY, 1);
	rd->compute_list_end();
}
} // namespace

GlowPostEffect::GlowPostEffect()
{
	set_effect_callback_type(EFFECT_CALLBACK_TYPE_POST_TRANSPARENT);
}

GlowPostEffect::~GlowPostEffect()
{
	RenderingServer *rs = RenderingServer::get_singleton();
	RenderingDevice *rd = rs ? rs->get_rendering_device() : nullptr;
	if (rd)
	{
		freeGpu(rd);
		for (RID *r : { &m_shaderResample, &m_shaderBlur, &m_shaderComposite, &m_samplerNearest, &m_samplerLinear })
		{
			if (r->is_valid())
			{
				rd->free_rid(*r); // a shader also frees the pipeline made from it
			}
		}
	}
}

std::string GlowPostEffect::configure(const GlowEffect &effect)
{
	std::string why = ScreenHilightFilter::unusableReason(effect); // before the kernel: an unbounded GlowDiameter would stall the load (S-5231)
	std::vector<ScreenHilightFilter::Sample> kernel;
	if (why.empty())
	{
		kernel = ScreenHilightFilter::createGaussianVector(effect);
		if (kernel.empty())
		{
			why = "no tap of the kernel (GlowDiameter " + std::to_string(effect.glowDiameter) + ", the Radius / Amplitude rows) is above 0.01";
		}
	}
	std::lock_guard<std::mutex> lock(m_mutex);
	m_passH = Pass();
	m_passV = Pass();
	m_problem = why;
	if (!why.empty())
	{
		m_failed = true;
		++m_version;
		return why;
	}
	m_failed = false;
	m_width = effect.glowTextureWidth;
	for (int vertical = 0; vertical < 2; ++vertical)
	{
		Pass &pass = vertical ? m_passV : m_passH;
		const std::vector<ScreenHilightFilter::Group> groups = ScreenHilightFilter::blurPass(kernel, effect.glowIntensity, effect.glowTextureWidth, vertical != 0);
		for (const ScreenHilightFilter::Group &g : groups)
		{
			for (size_t j = 0; j < 4; ++j)
			{
				pass.data.push_back(g.weight[j]);
				pass.data.push_back(g.offset[j][0]);
				pass.data.push_back(g.offset[j][1]);
				pass.data.push_back(0.0f);
			}
		}
		pass.groups = (int)groups.size();
	}
	++m_version;
	return std::string();
}

void GlowPostEffect::setSceneViewport(const RID &viewport)
{
	m_sceneViewport = viewport;
}

std::string GlowPostEffect::problem() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_problem;
}

int GlowPostEffect::kernelSamples() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_passH.groups * 4;
}

void GlowPostEffect::fail(const std::string &why)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_failed = true;
	m_problem = "S-5231: " + why + "; the glow pass is not drawn";
}

void GlowPostEffect::freeGpu(RenderingDevice *rd)
{
	for (RID *r : { &m_texScene, &m_texA, &m_texB, &m_bufferH, &m_bufferV })
	{
		if (r->is_valid())
		{
			rd->free_rid(*r);
			*r = RID();
		}
	}
	m_gpuWidth = 0;
}

bool GlowPostEffect::ensureGpu(RenderingDevice *rd, int width)
{
	if (!m_shaderResample.is_valid())
	{
		std::string why;
		if (!compile(rd, kResample, m_shaderResample, m_pipelineResample, why) || !compile(rd, kBlur, m_shaderBlur, m_pipelineBlur, why) ||
			!compile(rd, kComposite, m_shaderComposite, m_pipelineComposite, why))
		{
			fail("a glow compute shader did not compile (" + why + ")");
			return false;
		}
		m_samplerNearest = makeSampler(rd, false);
		m_samplerLinear = makeSampler(rd, true);
		if (!m_samplerNearest.is_valid() || !m_samplerLinear.is_valid())
		{
			fail("the glow samplers could not be created");
			return false;
		}
	}
	std::lock_guard<std::mutex> lock(m_mutex);
	if (m_gpuWidth != width)
	{
		freeGpu(rd);
		m_texScene = makeSquare(rd, width);
		m_texA = makeSquare(rd, width);
		m_texB = makeSquare(rd, width);
		if (!m_texScene.is_valid() || !m_texA.is_valid() || !m_texB.is_valid())
		{
			m_failed = true;
			m_problem = "S-5231: the glow targets could not be created; the glow pass is not drawn";
			return false;
		}
		m_gpuWidth = width;
		m_gpuVersion = 0;
	}
	if (m_gpuVersion != m_version)
	{
		for (RID *r : { &m_bufferH, &m_bufferV })
		{
			if (r->is_valid())
			{
				rd->free_rid(*r);
				*r = RID();
			}
		}
		auto upload = [&](const Pass &pass) {
			PackedFloat32Array a;
			a.resize((int64_t)pass.data.size());
			std::memcpy(a.ptrw(), pass.data.data(), pass.data.size() * sizeof(float));
			const PackedByteArray bytes = a.to_byte_array();
			if (m_simulateBufferFailure.load())
			{
				return RID(); // the test hook: the allocation of the kernel buffer fails
			}
			return rd->storage_buffer_create((uint32_t)bytes.size(), bytes);
		};
		if (m_passH.groups > 0)
		{
			m_bufferH = upload(m_passH);
			m_bufferV = upload(m_passV);
		}
		m_gpuGroupsH = m_passH.groups;
		m_gpuGroupsV = m_passV.groups;
		m_gpuVersion = m_version;
	}
	if (!m_bufferH.is_valid() || !m_bufferV.is_valid())
	{
		// no silent fallback (PLAN rule 10): a kernel buffer that could not be made is a reported failure, like the targets above
		for (RID *r : { &m_bufferH, &m_bufferV })
		{
			if (r->is_valid())
			{
				rd->free_rid(*r);
				*r = RID();
			}
		}
		m_failed = true;
		m_problem = "S-5231: the glow kernel buffers could not be created; the glow pass is not drawn";
		return false;
	}
	return true;
}

void GlowPostEffect::_render_callback(int32_t p_effect_callback_type, RenderData *p_render_data)
{
	(void)p_effect_callback_type;
	if (m_failed.load() || !m_active.load() || !p_render_data || !m_sceneViewport.is_valid())
	{
		return;
	}
	RenderingServer *rs = RenderingServer::get_singleton();
	RenderingDevice *rd = rs->get_rendering_device();
	if (!rd)
	{
		return;
	}
	const RID sceneTexture = rs->texture_get_rd_texture(rs->viewport_get_texture(m_sceneViewport), false);
	if (!sceneTexture.is_valid())
	{
		return; // the glow scene is not rendered yet (its first frame)
	}
	if (!ensureGpu(rd, m_width.load()))
	{
		return;
	}
	Ref<RenderSceneBuffersRD> buffers = p_render_data->get_render_scene_buffers();
	if (buffers.is_null())
	{
		return;
	}
	const Vector2i size = buffers->get_internal_size();
	if (size.x <= 0 || size.y <= 0)
	{
		return;
	}
	const int n = m_gpuWidth;
	const int g = (n + 7) / 8;
	{
		// 1. the glow scene at the centres of the square grid
		TypedArray<Ref<RDUniform>> u;
		u.push_back(imageUniform(0, m_texScene));
		u.push_back(samplerUniform(1, m_samplerNearest, sceneTexture));
		PackedFloat32Array push;
		push.push_back((float)n);
		push.push_back(0.0f);
		push.push_back(0.0f);
		push.push_back(0.0f);
		dispatch(rd, m_pipelineResample, UniformSetCacheRD::get_cache(m_shaderResample, 0, u), push, g, g);
	}
	for (int vertical = 0; vertical < 2; ++vertical)
	{
		// 2. the horizontal pass (scene -> A), then the vertical pass (A -> B)
		TypedArray<Ref<RDUniform>> u;
		u.push_back(imageUniform(0, vertical ? m_texB : m_texA));
		u.push_back(samplerUniform(1, m_samplerLinear, vertical ? m_texA : m_texScene));
		Ref<RDUniform> k;
		k.instantiate();
		k->set_uniform_type(RenderingDevice::UNIFORM_TYPE_STORAGE_BUFFER);
		k->set_binding(2);
		k->add_id(vertical ? m_bufferV : m_bufferH);
		u.push_back(k);
		PackedByteArray push;
		push.resize(16);
		const float fn = (float)n;
		const int32_t groups = vertical ? m_gpuGroupsV : m_gpuGroupsH;
		const float zero = 0.0f;
		std::memcpy(push.ptrw(), &fn, 4);
		std::memcpy(push.ptrw() + 4, &groups, 4);
		std::memcpy(push.ptrw() + 8, &zero, 4);
		std::memcpy(push.ptrw() + 12, &zero, 4);
		const int64_t list = rd->compute_list_begin();
		rd->compute_list_bind_compute_pipeline(list, m_pipelineBlur);
		rd->compute_list_bind_uniform_set(list, UniformSetCacheRD::get_cache(m_shaderBlur, 0, u), 0);
		rd->compute_list_set_push_constant(list, push, (uint32_t)push.size());
		rd->compute_list_dispatch(list, (uint32_t)g, (uint32_t)g, 1);
		rd->compute_list_end();
	}
	for (uint32_t view = 0; view < buffers->get_view_count(); ++view)
	{
		// 3. the glow texture over the displayed view
		const RID image = buffers->get_color_layer(view);
		Ref<RDTextureFormat> fmt = rd->texture_get_format(image);
		if (fmt.is_null() || fmt->get_format() != RenderingDevice::DATA_FORMAT_R16G16B16A16_SFLOAT)
		{
			fail("the 3D colour buffer is not RGBA16F");
			return;
		}
		TypedArray<Ref<RDUniform>> u;
		u.push_back(imageUniform(0, image));
		u.push_back(samplerUniform(1, m_samplerLinear, m_texB));
		PackedFloat32Array push;
		push.push_back((float)size.x);
		push.push_back((float)size.y);
		push.push_back(0.0f);
		push.push_back(0.0f);
		dispatch(rd, m_pipelineComposite, UniformSetCacheRD::get_cache(m_shaderComposite, 0, u), push, (size.x + 7) / 8, (size.y + 7) / 8);
	}
	++m_passes;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------

GlowPostEffectHost::GlowPostEffectHost()
{
	m_postEffect.instantiate();
}

void GlowPostEffectHost::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("configure_dictionary", "effect", "grade"), &GlowPostEffectHost::configure_dictionary);
	ClassDB::bind_method(D_METHOD("simulate_gpu_allocation_failure", "on"), &GlowPostEffectHost::simulate_gpu_allocation_failure);
	ClassDB::bind_method(D_METHOD("set_effects_lod", "lod"), &GlowPostEffectHost::set_effects_lod);
	ClassDB::bind_method(D_METHOD("get_effects_lod"), &GlowPostEffectHost::get_effects_lod);
	ClassDB::bind_method(D_METHOD("get_glow_stats"), &GlowPostEffectHost::get_glow_stats);
	ClassDB::bind_method(D_METHOD("get_choice"), &GlowPostEffectHost::get_choice);
	ClassDB::bind_method(D_METHOD("update_now"), &GlowPostEffectHost::update_now);
}

void GlowPostEffectHost::configure(const GlowEffect &effect, MapPostEffectsHost *grade)
{
	m_effect = effect;
	m_grade = grade ? ObjectID(grade->get_instance_id()) : ObjectID();
	m_configProblem.clear();
	m_configFatal = false;
	if (effect.glowEnabled)
	{
		m_configProblem = m_postEffect->configure(effect);
		m_configFatal = !m_configProblem.empty();
		// S-5231: what the pass does not draw is reported, not dropped
		if (m_configProblem.empty() && effect.terrainGlow)
		{
			m_configProblem = "S-5231: TerrainGlow = Yes (the terrain drawn in pass mode 3) is not ported; the glow draws the glow-flagged objects only";
		}
		if (m_configProblem.empty() && effect.multipassGlowEnabled)
		{
			m_configProblem = "S-5231: MultipassGlowEnabled = Yes (pass modes 6, 4 and 5) is not ported; the single pass is drawn";
		}
	}
}

void GlowPostEffectHost::configure_dictionary(const Dictionary &d, Node *grade)
{
	GlowEffect e;
	e.glowEnabled = d.get("enabled", false);
	e.glowDiameter = d.get("diameter", 0);
	e.glowIntensity = d.get("intensity", 0.0f);
	e.glowTextureWidth = d.get("texture_width", 0);
	e.radiusScale1 = d.get("radius_scale1", 0.0f);
	e.amplitude1 = d.get("amplitude1", 0.0f);
	e.radiusScale2 = d.get("radius_scale2", 0.0f);
	e.amplitude2 = d.get("amplitude2", 0.0f);
	e.terrainGlow = d.get("terrain_glow", false);
	e.multipassGlowEnabled = d.get("multipass", false);
	configure(e, Object::cast_to<MapPostEffectsHost>(grade));
}

void GlowPostEffectHost::simulate_gpu_allocation_failure(bool on)
{
	m_postEffect->simulateBufferFailure(on);
}

void GlowPostEffectHost::set_effects_lod(int64_t lod)
{
	m_effectsLod = lod;
}

void GlowPostEffectHost::ensureViewport()
{
	if (m_viewport)
	{
		return;
	}
	m_viewport = memnew(SubViewport);
	m_viewport->set_name("GlowScene");
	m_viewport->set_size(Vector2i(512, 512));
	m_viewport->set_update_mode(SubViewport::UPDATE_DISABLED);
	m_viewport->set_msaa_3d(Viewport::MSAA_DISABLED);
	m_viewport->set_use_hdr_2d(false);
	m_camera = memnew(Camera3D);
	m_camera->set_name("GlowCamera");
	m_camera->set_cull_mask(GlowPass::kGlowLayer);
	m_viewport->add_child(m_camera);
	add_child(m_viewport);
	m_camera->make_current();
	m_postEffect->setSceneViewport(m_viewport->get_viewport_rid());
}

void GlowPostEffectHost::syncCamera(Camera3D *main)
{
	// the main view's projection into a render of its aspect (the glow target is square in retail and stretched over the view; the pass resamples it)
	Viewport *vp = main->get_viewport();
	const Vector2 visible = vp ? vp->get_visible_rect().size : Vector2(1920, 1080);
	const int h = 512;
	const int w = std::max(64, (int)std::lround(h * (visible.y > 0 ? visible.x / visible.y : 16.0 / 9.0)));
	if (m_viewport->get_size() != Vector2i(w, h))
	{
		m_viewport->set_size(Vector2i(w, h));
	}
	m_camera->set_global_transform(main->get_global_transform());
	m_camera->set_projection(main->get_projection());
	m_camera->set_keep_aspect_mode(main->get_keep_aspect_mode());
	m_camera->set_fov(main->get_fov());
	m_camera->set_size(main->get_size());
	m_camera->set_frustum_offset(main->get_frustum_offset());
	m_camera->set_near(main->get_near());
	m_camera->set_far(main->get_far());
	m_camera->set_h_offset(main->get_h_offset());
	m_camera->set_v_offset(main->get_v_offset());
	m_camera->set_cull_mask(GlowPass::kGlowLayer);
	if (m_camera->get_environment().is_null())
	{
		// the main view's environment (the ambient light the objects are lit with) on a black background, linear tone mapping, Godot's own glow off
		Ref<Environment> source = main->get_environment();
		if (source.is_null() && get_tree())
		{
			TypedArray<Node> envs = get_tree()->get_root()->find_children("*", "WorldEnvironment", true, false);
			if (envs.size() > 0)
			{
				source = Object::cast_to<WorldEnvironment>(envs[0])->get_environment();
			}
		}
		Ref<Environment> env;
		if (source.is_valid())
		{
			env = source->duplicate();
		}
		if (env.is_null())
		{
			env.instantiate();
		}
		env->set_background(Environment::BG_COLOR);
		env->set_bg_color(Color(0, 0, 0, 1));
		env->set_tonemapper(Environment::TONE_MAPPER_LINEAR);
		env->set_glow_enabled(false);
		env->set_fog_enabled(false);
		m_camera->set_environment(env);
	}
}

void GlowPostEffectHost::ensureOnCamera(Camera3D *camera, bool glowOn)
{
	Ref<Compositor> comp = camera->get_compositor();
	if (comp.is_null())
	{
		if (!glowOn)
		{
			return;
		}
		comp.instantiate();
		camera->set_compositor(comp);
	}
	TypedArray<Ref<CompositorEffect>> effects = comp->get_compositor_effects();
	const int64_t n = effects.size();
	int64_t mine = -1, grade = -1;
	Ref<LookupTablePostEffect> gradeEffect;
	if (MapPostEffectsHost *gh = Object::cast_to<MapPostEffectsHost>(ObjectDB::get_instance(m_grade)))
	{
		gradeEffect = gh->effect();
	}
	for (int64_t i = 0; i < n; ++i)
	{
		const Ref<CompositorEffect> e = effects[i];
		if (e == m_postEffect)
		{
			mine = i;
		}
		if (gradeEffect.is_valid() && e == gradeEffect)
		{
			grade = i;
		}
	}
	if (!glowOn)
	{
		return; // stays where it is (it draws nothing while inactive); removed when the host leaves
	}
	// the order [.. gamma decode, glow, colour grade]: the glow right before the grade, or last without one
	const int64_t wanted = grade >= 0 ? grade - 1 : n - 1;
	if (mine >= 0 && mine == wanted)
	{
		return;
	}
	if (mine >= 0)
	{
		effects.remove_at(mine);
		grade = grade > mine ? grade - 1 : grade;
	}
	if (grade >= 0)
	{
		effects.insert(grade, m_postEffect);
	}
	else
	{
		effects.push_back(m_postEffect);
	}
	comp->set_compositor_effects(effects);
	const ObjectID id(camera->get_instance_id());
	if (std::find(m_cameras.begin(), m_cameras.end(), id) == m_cameras.end())
	{
		m_cameras.push_back(id);
	}
}

void GlowPostEffectHost::removeFromCameras()
{
	for (const ObjectID &id : m_cameras)
	{
		Camera3D *cam = Object::cast_to<Camera3D>(ObjectDB::get_instance(id));
		if (!cam)
		{
			continue;
		}
		Ref<Compositor> comp = cam->get_compositor();
		if (comp.is_null())
		{
			continue;
		}
		TypedArray<Ref<CompositorEffect>> effects = comp->get_compositor_effects();
		bool changed = false;
		for (int64_t i = effects.size() - 1; i >= 0; --i)
		{
			if (Ref<CompositorEffect>(effects[i]) == m_postEffect)
			{
				effects.remove_at(i);
				changed = true;
			}
		}
		if (changed)
		{
			comp->set_compositor_effects(effects);
		}
	}
	m_cameras.clear();
}

void GlowPostEffectHost::update_now()
{
	if (!is_inside_tree())
	{
		return;
	}
	Viewport *vp = get_viewport();
	Camera3D *cam = vp ? vp->get_camera_3d() : nullptr;
	MapPostEffectsHost *grade = Object::cast_to<MapPostEffectsHost>(ObjectDB::get_instance(m_grade));
	// RW 0x487897 .. 0x487A5C: low effects LOD runs neither pass, an active grade pre-empts the glow
	W3DViewDrawPostPass::Inputs in;
	in.passMode = 0;
	in.viewFilter = 0; // the view filters are not ported (S-5231)
	in.effectsLOD = (int)m_effectsLod;
	in.colorGradeActive = grade && grade->hasEffect();
	in.glowEnabled = m_effect.glowEnabled;
	const W3DViewDrawPostPass::Choice choice = W3DViewDrawPostPass::choose(in);
	switch (choice)
	{
	case W3DViewDrawPostPass::Choice::ColorGrade:
		m_choice = "color_grade";
		break;
	case W3DViewDrawPostPass::Choice::Glow:
		m_choice = "glow";
		break;
	case W3DViewDrawPostPass::Choice::SpecialFilter:
		m_choice = "special_filter";
		break;
	default:
		m_choice = "none";
		break;
	}
	if (grade && in.colorGradeActive)
	{
		grade->setSuppressed(choice != W3DViewDrawPostPass::Choice::ColorGrade); // a low effects LOD skips the grade too
	}
	const bool glowOn = choice == W3DViewDrawPostPass::Choice::Glow && !m_configFatal && m_postEffect->problem().empty();
	if (glowOn && cam)
	{
		ensureViewport();
		syncCamera(cam);
		GammaComposite::ensure(m_camera);
		ensureOnCamera(cam, true);
	}
	m_postEffect->setActive(glowOn && cam != nullptr);
	if (m_viewport)
	{
		const bool on = glowOn && cam != nullptr;
		if (on != m_viewportOn)
		{
			m_viewport->set_update_mode(on ? SubViewport::UPDATE_ALWAYS : SubViewport::UPDATE_DISABLED); // an idle glow costs no second scene render
			m_viewportOn = on;
		}
	}
}

Dictionary GlowPostEffectHost::get_glow_stats() const
{
	Dictionary d;
	d["choice"] = String(m_choice.c_str());
	d["glow_enabled"] = m_effect.glowEnabled;
	d["effects_lod"] = m_effectsLod;
	d["installed"] = !m_cameras.empty();
	d["active"] = m_postEffect->active();
	d["passes"] = m_postEffect->passes();
	d["kernel_samples"] = m_postEffect->kernelSamples();
	d["texture_width"] = m_postEffect->textureWidth();
	d["scene_size"] = m_viewport ? m_viewport->get_size() : Vector2i();
	std::string problem = m_postEffect->problem();
	if (problem.empty())
	{
		problem = m_configProblem;
	}
	d["problem"] = String(problem.c_str());
	PackedStringArray unverified;
	for (const char *line : ScreenHilightFilter::unverified())
	{
		unverified.push_back(String(line));
	}
	d["unverified"] = unverified;
	return d;
}

void GlowPostEffectHost::_notification(int what)
{
	if (what == NOTIFICATION_READY)
	{
		set_process_internal(true);
		set_process_priority(1 << 20); // after the nodes that move the camera
	}
	if (what == NOTIFICATION_INTERNAL_PROCESS)
	{
		update_now();
	}
	if (what == NOTIFICATION_EXIT_TREE)
	{
		removeFromCameras();
	}
}

} // namespace godot
