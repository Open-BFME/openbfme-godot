// OpenBFME. GPL-3.0. Gamma-space compositing of the transparent pass (lane RENDER-3, S-831): see GodotGammaComposite.h.

#include "GodotDevice/GodotGammaComposite.h"

#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/compositor.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/rd_pipeline_color_blend_state.hpp>
#include <godot_cpp/classes/rd_pipeline_color_blend_state_attachment.hpp>
#include <godot_cpp/classes/rd_pipeline_depth_stencil_state.hpp>
#include <godot_cpp/classes/rd_pipeline_multisample_state.hpp>
#include <godot_cpp/classes/rd_pipeline_rasterization_state.hpp>
#include <godot_cpp/classes/rd_sampler_state.hpp>
#include <godot_cpp/classes/rd_shader_source.hpp>
#include <godot_cpp/classes/rd_texture_view.hpp>
#include <godot_cpp/classes/rd_shader_spirv.hpp>
#include <godot_cpp/classes/rd_texture_format.hpp>
#include <godot_cpp/classes/rd_uniform.hpp>
#include <godot_cpp/classes/render_data.hpp>
#include <godot_cpp/classes/render_scene_buffers_rd.hpp>
#include <godot_cpp/classes/rendering_device.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/uniform_set_cache_rd.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/variant/typed_array.hpp>

#include <atomic>
#include <map>
#include <mutex>
#include <set>

namespace godot
{

namespace
{
// The sRGB curve both ways, the one Godot's output encoding applies (so encode -> output shows the gamma value itself). Values above 1 (additive
// overbright before the decode) follow the same power segment; negatives are clamped.
const char *kCompute = R"GLSL(
#version 450
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
layout(rgba16f, set = 0, binding = 0) uniform restrict image2D color_image;
layout(push_constant, std430) uniform Params {
	vec2 size;
	float decode;
	float pad;
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
	c.rgb = params.decode > 0.5 ? to_linear(c.rgb) : to_gamma(c.rgb);
	imageStore(color_image, uv, c);
}
)GLSL";

// MSAA (review r2): with a multisample 3D buffer Godot draws the transparent pass into the multisample attachment and resolves it over the single-sample
// layer before POST_TRANSPARENT. The encode therefore rewrites every SAMPLE of the multisample attachment (a copy of it is read per sample by a
// sample-shaded full-screen triangle; reading and writing the attachment itself would be a feedback loop); the decode stays on the resolved layer (the
// resolve averages the gamma-space samples, as a D3D9 resolve averages the displayed values).
const char *kFullscreenVertex = R"GLSL(
#version 450
void main() {
	vec2 p = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
	gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)GLSL";
const char *kEncodeSamplesFragment = R"GLSL(
#version 450
layout(set = 0, binding = 0) uniform sampler2DMS src;
layout(location = 0) out vec4 frag_color;
vec3 to_gamma(vec3 c) {
	c = max(c, vec3(0.0));
	return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), c));
}
void main() {
	vec4 c = texelFetch(src, ivec2(gl_FragCoord.xy), gl_SampleID);
	frag_color = vec4(to_gamma(c.rgb), c.a);
}
)GLSL";

// review r3 (cost): the default MSAA encode. The encode effect asks for the resolved colour (access_resolved_color), so the opaque image is resolved
// once; one invocation per pixel reads it and writes its gamma encoding to every sample of the pixel (no attachment copy, no per-sample shading).
// Interior pixels are exact (their samples are equal); an opaque geometry edge keeps Godot's linear-space average of its samples instead of retail's
// average of displayed values (S-831; the exact per-sample path is the project setting openbfme/rendering/gamma_exact_msaa_edges). Transparent draws
// after it are blended per sample as before.
const char *kEncodePixelsFragment = R"GLSL(
#version 450
layout(set = 0, binding = 0) uniform sampler2D src;
layout(location = 0) out vec4 frag_color;
vec3 to_gamma(vec3 c) {
	c = max(c, vec3(0.0));
	return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), c));
}
void main() {
	vec4 c = texelFetch(src, ivec2(gl_FragCoord.xy), 0);
	frag_color = vec4(to_gamma(c.rgb), c.a);
}
)GLSL";

std::mutex g_mutex;
std::set<std::string> g_problems; // render-thread findings
std::atomic<bool> g_installed{ false };
std::atomic<bool> g_msaaUnsupported{ false }; // set by the render thread when the sample encode cannot run: ensure() then turns MSAA off
std::atomic<bool> g_exactMsaaEdges{ false };  // ProjectSettings openbfme/rendering/gamma_exact_msaa_edges, read on the main thread by ensure()
// review r3: creations of the owned per-attachment / per-format objects (a steady scene creates none per frame)
std::atomic<int64_t> g_framebuffersCreated{ 0 };
std::atomic<int64_t> g_pipelinesCreated{ 0 };
std::atomic<int64_t> g_copiesCreated{ 0 };
std::atomic<int64_t> g_encodes{ 0 };
std::atomic<int64_t> g_copiesLive{ 0 }; // review r4: sample copies currently held (created minus released)
Ref<GammaCompositeEffect> g_encode;
Ref<GammaCompositeEffect> g_decode;

void problem(const std::string &s)
{
	std::lock_guard<std::mutex> lock(g_mutex);
	g_problems.insert(s);
}
} // namespace

GammaCompositeEffect::GammaCompositeEffect()
{
	set_effect_callback_type(EFFECT_CALLBACK_TYPE_PRE_TRANSPARENT);
}

GammaCompositeEffect::~GammaCompositeEffect()
{
	disconnectCleanup();
	RenderingServer *rs = RenderingServer::get_singleton();
	RenderingDevice *rd = rs ? rs->get_rendering_device() : nullptr;
	if (!rd)
	{
		return;
	}
	for (auto &kv : m_attachments)
	{
		releaseAttachment(rd, kv.second);
	}
	for (std::map<int64_t, RID> *cache : { &m_pixelPipelines, &m_samplePipelines })
	{
		for (auto &kv : *cache)
		{
			if (kv.second.is_valid() && rd->render_pipeline_is_valid(kv.second))
			{
				rd->free_rid(kv.second);
			}
		}
	}
	for (RID r : { m_pixelShader, m_sampleShader, m_shader })
	{
		if (r.is_valid())
		{
			rd->free_rid(r); // also frees the compute pipeline made from m_shader
		}
	}
	if (m_sampler.is_valid())
	{
		rd->free_rid(m_sampler);
	}
}

// review r4: the retirement of orphaned attachment entries (a sample copy outlives its attachment when the viewport turns MSAA off or is freed, or the
// exact mode is switched off) must not depend on a later MSAA callback: every frame after drawing, the encode effect queues a prune on the render thread,
// whatever 3D viewports remain. Bound to the effect's lifetime; disconnected in the destructor (and by GammaComposite::shutdown).
void GammaCompositeEffect::connectCleanup()
{
	RenderingServer *rs = RenderingServer::get_singleton();
	if (rs && !m_cleanupConnected)
	{
		rs->connect("frame_post_draw", callable_mp(this, &GammaCompositeEffect::queueCleanup));
		m_cleanupConnected = true;
	}
}

void GammaCompositeEffect::disconnectCleanup()
{
	RenderingServer *rs = RenderingServer::get_singleton();
	if (rs && m_cleanupConnected)
	{
		const Callable c = callable_mp(this, &GammaCompositeEffect::queueCleanup);
		if (rs->is_connected("frame_post_draw", c))
		{
			rs->disconnect("frame_post_draw", c);
		}
	}
	m_cleanupConnected = false;
}

void GammaCompositeEffect::queueCleanup()
{
	if (RenderingServer *rs = RenderingServer::get_singleton())
	{
		rs->call_on_render_thread(callable_mp(this, &GammaCompositeEffect::cleanupOnRenderThread));
	}
}

void GammaCompositeEffect::cleanupOnRenderThread()
{
	RenderingServer *rs = RenderingServer::get_singleton();
	RenderingDevice *rd = rs ? rs->get_rendering_device() : nullptr;
	if (rd && !m_attachments.empty())
	{
		pruneAttachments(rd);
	}
}

void GammaCompositeEffect::setDecode(bool decode)
{
	m_decode = decode;
	if (!decode)
	{
		connectCleanup();
	}
	set_effect_callback_type(decode ? EFFECT_CALLBACK_TYPE_POST_TRANSPARENT : EFFECT_CALLBACK_TYPE_PRE_TRANSPARENT);
	set_access_resolved_color(!decode); // review r3: the per-pixel MSAA encode reads the resolved opaque image
}

void GammaCompositeEffect::_render_callback(int32_t p_effect_callback_type, RenderData *p_render_data)
{
	if (m_failed || !p_render_data)
	{
		return;
	}
	RenderingDevice *rd = RenderingServer::get_singleton()->get_rendering_device();
	if (!rd)
	{
		return;
	}
	if (!m_shader.is_valid())
	{
		Ref<RDShaderSource> src;
		src.instantiate();
		src->set_language(RenderingDevice::SHADER_LANGUAGE_GLSL);
		src->set_stage_source(RenderingDevice::SHADER_STAGE_COMPUTE, String(kCompute));
		Ref<RDShaderSPIRV> spirv = rd->shader_compile_spirv_from_source(src);
		if (spirv.is_null() || !spirv->get_stage_compile_error(RenderingDevice::SHADER_STAGE_COMPUTE).is_empty())
		{
			m_failed = true;
			problem("S-831: the gamma compositing compute shader did not compile; the transparent pass is not converted");
			return;
		}
		m_shader = rd->shader_create_from_spirv(spirv);
		m_pipeline = m_shader.is_valid() ? rd->compute_pipeline_create(m_shader) : RID();
		if (!m_pipeline.is_valid())
		{
			m_failed = true;
			problem("S-831: the gamma compositing pipeline could not be created; the transparent pass is not converted");
			return;
		}
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
	if (!m_decode && buffers->get_msaa_3d() != RenderingServer::VIEWPORT_MSAA_DISABLED)
	{
		encodeSamples(rd, buffers.ptr(), size);
		return;
	}
	for (uint32_t view = 0; view < buffers->get_view_count(); ++view)
	{
		const RID image = buffers->get_color_layer(view);
		Ref<RDTextureFormat> fmt = rd->texture_get_format(image);
		if (fmt.is_null() || fmt->get_format() != RenderingDevice::DATA_FORMAT_R16G16B16A16_SFLOAT)
		{
			problem("S-831: the 3D colour buffer is not RGBA16F (format " + std::to_string(fmt.is_valid() ? (int)fmt->get_format() : -1) +
				"); the transparent pass blends in linear space");
			return;
		}
		Ref<RDUniform> u;
		u.instantiate();
		u->set_uniform_type(RenderingDevice::UNIFORM_TYPE_IMAGE);
		u->set_binding(0);
		u->add_id(image);
		TypedArray<Ref<RDUniform>> uniforms;
		uniforms.push_back(u);
		const RID set = UniformSetCacheRD::get_cache(m_shader, 0, uniforms);
		PackedFloat32Array push;
		push.push_back((float)size.x);
		push.push_back((float)size.y);
		push.push_back(m_decode ? 1.0f : 0.0f);
		push.push_back(0.0f);
		const PackedByteArray bytes = push.to_byte_array();
		const int64_t list = rd->compute_list_begin();
		rd->compute_list_bind_compute_pipeline(list, m_pipeline);
		rd->compute_list_bind_uniform_set(list, set, 0);
		rd->compute_list_set_push_constant(list, bytes, (uint32_t)bytes.size());
		rd->compute_list_dispatch(list, (uint32_t)((size.x + 7) / 8), (uint32_t)((size.y + 7) / 8), 1);
		rd->compute_list_end();
	}
	(void)p_effect_callback_type;
}

RID GammaCompositeEffect::rasterPipeline(RenderingDevice *rd, int64_t format, RenderingDevice::TextureSamples samples, bool perSample)
{
	std::map<int64_t, RID> &cache = perSample ? m_samplePipelines : m_pixelPipelines;
	auto it = cache.find(format);
	if (it != cache.end() && rd->render_pipeline_is_valid(it->second))
	{
		return it->second;
	}
	Ref<RDPipelineRasterizationState> raster;
	raster.instantiate();
	Ref<RDPipelineMultisampleState> ms;
	ms.instantiate();
	ms->set_sample_count(samples);
	// per sample: one invocation per sample reads its own source sample; per pixel: one invocation writes its value to every sample of the pixel
	ms->set_enable_sample_shading(perSample);
	ms->set_min_sample_shading(perSample ? 1.0f : 0.0f);
	Ref<RDPipelineDepthStencilState> depth;
	depth.instantiate();
	Ref<RDPipelineColorBlendStateAttachment> att;
	att.instantiate(); // no blend: the encoded value replaces the sample
	Ref<RDPipelineColorBlendState> blend;
	blend.instantiate();
	TypedArray<Ref<RDPipelineColorBlendStateAttachment>> atts;
	atts.push_back(att);
	blend->set_attachments(atts);
	const RID shader = perSample ? m_sampleShader : m_pixelShader;
	if (it != cache.end() && it->second.is_valid() && rd->render_pipeline_is_valid(it->second))
	{
		rd->free_rid(it->second);
	}
	const RID pipeline = rd->render_pipeline_create(shader, format, -1, RenderingDevice::RENDER_PRIMITIVE_TRIANGLES, raster, ms, depth, blend);
	++g_pipelinesCreated;
	cache[format] = pipeline; // owned: freed in the destructor
	return pipeline;
}

void GammaCompositeEffect::pruneAttachments(RenderingDevice *rd)
{
	// review r3: every framebuffer and sample copy is owned per multisample attachment and released when the attachment is gone (a resized or freed
	// viewport); several cameras keep one entry each instead of re-creating (and losing) one per callback
	for (auto it = m_attachments.begin(); it != m_attachments.end();)
	{
		if (!rd->texture_is_valid(it->second.attachment))
		{
			releaseAttachment(rd, it->second);
			it = m_attachments.erase(it);
		}
		else
		{
			if (!g_exactMsaaEdges && it->second.copy.is_valid())
			{
				if (rd->texture_is_valid(it->second.copy))
				{
					rd->free_rid(it->second.copy); // the exact mode was switched off: its copies are not kept
				}
				--g_copiesLive;
				it->second.copy = RID();
			}
			++it;
		}
	}
}

void GammaCompositeEffect::releaseAttachment(RenderingDevice *rd, AttachmentEntry &e)
{
	if (e.framebuffer.is_valid() && rd->framebuffer_is_valid(e.framebuffer))
	{
		rd->free_rid(e.framebuffer);
	}
	if (e.copy.is_valid())
	{
		--g_copiesLive;
		if (rd->texture_is_valid(e.copy))
		{
			rd->free_rid(e.copy);
		}
	}
	e.framebuffer = RID();
	e.copy = RID();
}

void GammaCompositeEffect::encodeSamples(RenderingDevice *rd, RenderSceneBuffersRD *buffers, const Vector2i &size)
{
	if (m_msaaFailed)
	{
		return;
	}
	auto fail = [&](const std::string &why) {
		m_msaaFailed = true;
		problem("S-831: the multisample transparent attachment could not be gamma-encoded (" + why + "); MSAA is turned off on the cameras showing it");
		g_msaaUnsupported = true;
	};
	if (buffers->get_view_count() != 1)
	{
		fail("several views");
		return;
	}
	const bool perSample = g_exactMsaaEdges;
	const RID msaa = buffers->get_color_layer(0, true);
	const RID resolved = buffers->get_color_layer(0, false);
	Ref<RDTextureFormat> fmt = rd->texture_get_format(msaa);
	if (fmt.is_null() || fmt->get_format() != RenderingDevice::DATA_FORMAT_R16G16B16A16_SFLOAT)
	{
		fail("not an RGBA16F attachment");
		return;
	}
	if (!(fmt->get_usage_bits() & RenderingDevice::TEXTURE_USAGE_COLOR_ATTACHMENT_BIT) ||
		(perSample && !(fmt->get_usage_bits() & RenderingDevice::TEXTURE_USAGE_CAN_COPY_FROM_BIT)))
	{
		fail("the attachment cannot be written or copied from");
		return;
	}
	if (!m_pixelShader.is_valid())
	{
		auto compile = [&](const char *fragment) {
			Ref<RDShaderSource> src;
			src.instantiate();
			src->set_language(RenderingDevice::SHADER_LANGUAGE_GLSL);
			src->set_stage_source(RenderingDevice::SHADER_STAGE_VERTEX, String(kFullscreenVertex));
			src->set_stage_source(RenderingDevice::SHADER_STAGE_FRAGMENT, String(fragment));
			Ref<RDShaderSPIRV> spirv = rd->shader_compile_spirv_from_source(src);
			if (spirv.is_null() || !spirv->get_stage_compile_error(RenderingDevice::SHADER_STAGE_VERTEX).is_empty() ||
				!spirv->get_stage_compile_error(RenderingDevice::SHADER_STAGE_FRAGMENT).is_empty())
			{
				return RID();
			}
			return rd->shader_create_from_spirv(spirv);
		};
		m_pixelShader = compile(kEncodePixelsFragment);
		m_sampleShader = compile(kEncodeSamplesFragment);
		Ref<RDSamplerState> ss;
		ss.instantiate();
		m_sampler = rd->sampler_create(ss);
		if (!m_pixelShader.is_valid() || !m_sampleShader.is_valid())
		{
			fail("the encode shaders did not compile");
			return;
		}
	}
	pruneAttachments(rd);
	AttachmentEntry &e = m_attachments[msaa.get_id()];
	e.attachment = msaa;
	if (!e.framebuffer.is_valid() || !rd->framebuffer_is_valid(e.framebuffer))
	{
		TypedArray<RID> attachments;
		attachments.push_back(msaa);
		e.framebuffer = rd->framebuffer_create(attachments);
		++g_framebuffersCreated;
	}
	if (!e.framebuffer.is_valid())
	{
		fail("the framebuffer could not be created");
		return;
	}
	const RID pipeline = rasterPipeline(rd, rd->framebuffer_get_format(e.framebuffer), fmt->get_samples(), perSample);
	if (!pipeline.is_valid())
	{
		fail("the encode pipeline could not be created");
		return;
	}
	RID source = resolved;
	if (perSample)
	{
		// exact mode: a copy of the multisample attachment, read per sample (reading the attachment while writing it would be a feedback loop)
		if (!e.copy.is_valid() || !rd->texture_is_valid(e.copy))
		{
			Ref<RDTextureFormat> cf;
			cf.instantiate();
			cf->set_texture_type(RenderingDevice::TEXTURE_TYPE_2D);
			cf->set_format(fmt->get_format());
			cf->set_width((uint32_t)size.x);
			cf->set_height((uint32_t)size.y);
			cf->set_samples(fmt->get_samples());
			cf->set_usage_bits(RenderingDevice::TEXTURE_USAGE_SAMPLING_BIT | RenderingDevice::TEXTURE_USAGE_CAN_COPY_TO_BIT);
			Ref<RDTextureView> view;
			view.instantiate();
			e.copy = rd->texture_create(cf, view);
			++g_copiesCreated;
			if (e.copy.is_valid())
			{
				++g_copiesLive;
			}
			if (!e.copy.is_valid())
			{
				fail("the sample copy could not be created");
				return;
			}
		}
		if (rd->texture_copy(msaa, e.copy, Vector3(), Vector3(), Vector3((float)size.x, (float)size.y, 1.0f), 0, 0, 0, 0) != OK)
		{
			fail("the attachment copy failed");
			return;
		}
		source = e.copy;
	}
	else if (e.copy.is_valid())
	{
		if (rd->texture_is_valid(e.copy))
		{
			rd->free_rid(e.copy); // the exact mode was switched off: its copy is not kept
		}
		--g_copiesLive;
		e.copy = RID();
	}
	Ref<RDUniform> u;
	u.instantiate();
	u->set_uniform_type(RenderingDevice::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE);
	u->set_binding(0);
	u->add_id(m_sampler);
	u->add_id(source);
	TypedArray<Ref<RDUniform>> uniforms;
	uniforms.push_back(u);
	const RID set = UniformSetCacheRD::get_cache(perSample ? m_sampleShader : m_pixelShader, 0, uniforms);
	const int64_t list = rd->draw_list_begin(e.framebuffer);
	rd->draw_list_bind_render_pipeline(list, pipeline);
	rd->draw_list_bind_uniform_set(list, set, 0);
	rd->draw_list_draw(list, false, 1, 3);
	rd->draw_list_end();
	++g_encodes;
}

void GammaCompositeHost::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("get_gamma_stats"), &GammaCompositeHost::get_gamma_stats);
}

Dictionary GammaCompositeHost::get_gamma_stats() const
{
	Dictionary d;
	d["msaa_encodes"] = (int64_t)g_encodes;
	d["framebuffers_created"] = (int64_t)g_framebuffersCreated;
	d["pipelines_created"] = (int64_t)g_pipelinesCreated;
	d["sample_copies_created"] = (int64_t)g_copiesCreated;
	d["sample_copies_live"] = (int64_t)g_copiesLive;
	d["exact_msaa_edges"] = (bool)g_exactMsaaEdges;
	return d;
}

void GammaCompositeHost::_notification(int what)
{
	if (what == NOTIFICATION_READY)
	{
		set_process_internal(true);
	}
	if (what == NOTIFICATION_READY || what == NOTIFICATION_INTERNAL_PROCESS)
	{
		GammaComposite::ensure(this);
	}
}

namespace GammaComposite
{
void ensure(Node *node)
{
	if (!node || !node->is_inside_tree())
	{
		return;
	}
	Viewport *vp = node->get_viewport();
	Camera3D *cam = vp ? vp->get_camera_3d() : nullptr;
	if (!cam)
	{
		return;
	}
	if (g_encode.is_null())
	{
		g_encode.instantiate();
		g_encode->setDecode(false);
		g_decode.instantiate();
		g_decode->setDecode(true);
	}
	{
		ProjectSettings *ps = ProjectSettings::get_singleton();
		g_exactMsaaEdges = ps && ps->has_setting("openbfme/rendering/gamma_exact_msaa_edges") &&
			(bool)ps->get_setting("openbfme/rendering/gamma_exact_msaa_edges");
	}
	if (g_msaaUnsupported && vp->get_msaa_3d() != Viewport::MSAA_DISABLED)
	{
		vp->set_msaa_3d(Viewport::MSAA_DISABLED); // the minimum fix of review r2, only when the per-sample encode cannot run (reported as S-831)
	}
	Ref<Compositor> comp = cam->get_compositor();
	if (comp.is_null())
	{
		comp.instantiate();
		cam->set_compositor(comp);
	}
	TypedArray<Ref<CompositorEffect>> effects = comp->get_compositor_effects();
	bool hasEncode = false, hasDecode = false;
	for (int64_t i = 0; i < effects.size(); ++i)
	{
		const Ref<CompositorEffect> e = effects[i];
		hasEncode = hasEncode || e == g_encode;
		hasDecode = hasDecode || e == g_decode;
	}
	if (!hasEncode || !hasDecode)
	{
		if (!hasEncode)
		{
			effects.push_front(g_encode);
		}
		if (!hasDecode)
		{
			effects.push_back(g_decode);
		}
		comp->set_compositor_effects(effects);
	}
	g_installed = true;
}

bool installed()
{
	return g_installed;
}

PackedStringArray unverified()
{
	PackedStringArray out;
	if (!g_installed)
	{
		return out;
	}
	out.push_back("S-831: the transparent pass blends in retail's gamma space (the colour buffer is sRGB-encoded before it and decoded after it); "
				  "inferred: the half-float round trip, the sRGB curve as the display gamma, Godot-built transparent materials other than ours are not converted");
	if (!g_exactMsaaEdges)
	{
		out.push_back("S-831: with MSAA the encode writes each pixel's resolved opaque colour to all its samples: opaque geometry edges keep Godot's "
					  "linear-space sample average, retail averages displayed values (project setting openbfme/rendering/gamma_exact_msaa_edges encodes per sample)");
	}
	out.push_back("S-831: transparent blending uses an HDR buffer without retail's per-draw [0,1] saturation; additive overbright can survive later alpha or "
				  "multiply draws");
	std::lock_guard<std::mutex> lock(g_mutex);
	for (const std::string &p : g_problems)
	{
		out.push_back(String(p.c_str()));
	}
	return out;
}

void shutdown()
{
	if (g_encode.is_valid())
	{
		g_encode->detachCleanup();
	}
	g_encode.unref();
	g_decode.unref();
}
} // namespace GammaComposite

} // namespace godot
