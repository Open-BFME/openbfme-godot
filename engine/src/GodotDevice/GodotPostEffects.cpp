// OpenBFME. GPL-3.0. The map's post effects on the 3D view (lane RENDER-4, S-1650): see GodotPostEffects.h.

#include "GodotDevice/GodotPostEffects.h"

#include "GameEngineDevice/W3DDevice/GameClient/W3DLookupTablePostEffect.h"

#include <godot_cpp/classes/compositor.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/rd_sampler_state.hpp>
#include <godot_cpp/classes/rd_shader_source.hpp>
#include <godot_cpp/classes/rd_shader_spirv.hpp>
#include <godot_cpp/classes/rd_texture_format.hpp>
#include <godot_cpp/classes/rd_uniform.hpp>
#include <godot_cpp/classes/render_scene_buffers_rd.hpp>
#include <godot_cpp/classes/rendering_device.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/uniform_set_cache_rd.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/variant/typed_array.hpp>

#include <cstring>

namespace godot
{

namespace
{
// postfx_lookuptable.fxo ps_2_0 on retail's displayed values: texld r2 (frame), texld r1, r2, s1 (volume), lrp r0, BlendFactor, r1, r2. The buffer is
// Godot's linear RGBA16F: encode with the sRGB curve Godot's output applies (GodotGammaComposite.h), saturate as the D3D9 frame buffer holds it, grade,
// decode.
const char *kCompute = R"GLSL(
#version 450
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
layout(rgba16f, set = 0, binding = 0) uniform restrict image2D color_image;
layout(set = 0, binding = 1) uniform sampler3D lookup;
layout(push_constant, std430) uniform Params {
	vec2 size;
	float blend;
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
	vec3 g = clamp(to_gamma(c.rgb), 0.0, 1.0);
	vec3 graded = texture(lookup, g).rgb;
	c.rgb = to_linear(mix(g, graded, params.blend));
	imageStore(color_image, uv, c);
}
)GLSL";
} // namespace

LookupTablePostEffect::LookupTablePostEffect()
{
	set_effect_callback_type(EFFECT_CALLBACK_TYPE_POST_TRANSPARENT);
}

LookupTablePostEffect::~LookupTablePostEffect()
{
	RenderingServer *rs = RenderingServer::get_singleton();
	RenderingDevice *rd = rs ? rs->get_rendering_device() : nullptr;
	if (!rd)
	{
		return;
	}
	if (m_shader.is_valid())
	{
		rd->free_rid(m_shader); // also frees the pipeline made from it
	}
	if (m_sampler.is_valid())
	{
		rd->free_rid(m_sampler);
	}
}

void LookupTablePostEffect::setLookup(const std::vector<std::uint8_t> &volume, float blend)
{
	using W3DLookupTablePostEffect::kSize;
	TypedArray<Ref<Image>> slices;
	for (int z = 0; z < kSize; ++z)
	{
		PackedByteArray bytes;
		bytes.resize((int64_t)kSize * kSize * 4);
		std::memcpy(bytes.ptrw(), volume.data() + (size_t)z * kSize * kSize * 4, (size_t)kSize * kSize * 4);
		slices.push_back(Image::create_from_data(kSize, kSize, false, Image::FORMAT_RGBA8, bytes));
	}
	m_lookup.instantiate();
	m_lookup->create(Image::FORMAT_RGBA8, kSize, kSize, kSize, false, slices);
	m_blend = blend;
}

std::string LookupTablePostEffect::problem() const
{
	return m_problem;
}

void LookupTablePostEffect::_render_callback(int32_t p_effect_callback_type, RenderData *p_render_data)
{
	(void)p_effect_callback_type;
	if (m_failed || !p_render_data || m_lookup.is_null())
	{
		return;
	}
	RenderingServer *rs = RenderingServer::get_singleton();
	RenderingDevice *rd = rs->get_rendering_device();
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
			m_problem = "S-1650: the lookup table compute shader did not compile; the map's colour grade is not applied";
			return;
		}
		m_shader = rd->shader_create_from_spirv(spirv);
		m_pipeline = m_shader.is_valid() ? rd->compute_pipeline_create(m_shader) : RID();
		// LookupTableSampler: MinFilter / MagFilter LINEAR, MipFilter POINT, AddressU / V / W CLAMP (the effect's state block)
		Ref<RDSamplerState> ss;
		ss.instantiate();
		ss->set_min_filter(RenderingDevice::SAMPLER_FILTER_LINEAR);
		ss->set_mag_filter(RenderingDevice::SAMPLER_FILTER_LINEAR);
		ss->set_mip_filter(RenderingDevice::SAMPLER_FILTER_NEAREST);
		ss->set_repeat_u(RenderingDevice::SAMPLER_REPEAT_MODE_CLAMP_TO_EDGE);
		ss->set_repeat_v(RenderingDevice::SAMPLER_REPEAT_MODE_CLAMP_TO_EDGE);
		ss->set_repeat_w(RenderingDevice::SAMPLER_REPEAT_MODE_CLAMP_TO_EDGE);
		m_sampler = rd->sampler_create(ss);
		if (!m_pipeline.is_valid() || !m_sampler.is_valid())
		{
			m_failed = true;
			m_problem = "S-1650: the lookup table pipeline could not be created; the map's colour grade is not applied";
			return;
		}
	}
	const RID lookup = rs->texture_get_rd_texture(m_lookup->get_rid(), false);
	if (!lookup.is_valid())
	{
		return; // the volume is not uploaded yet (its first frame)
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
	for (uint32_t view = 0; view < buffers->get_view_count(); ++view)
	{
		const RID image = buffers->get_color_layer(view);
		Ref<RDTextureFormat> fmt = rd->texture_get_format(image);
		if (fmt.is_null() || fmt->get_format() != RenderingDevice::DATA_FORMAT_R16G16B16A16_SFLOAT)
		{
			m_failed = true;
			m_problem = "S-1650: the 3D colour buffer is not RGBA16F; the map's colour grade is not applied";
			return;
		}
		Ref<RDUniform> u0;
		u0.instantiate();
		u0->set_uniform_type(RenderingDevice::UNIFORM_TYPE_IMAGE);
		u0->set_binding(0);
		u0->add_id(image);
		Ref<RDUniform> u1;
		u1.instantiate();
		u1->set_uniform_type(RenderingDevice::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE);
		u1->set_binding(1);
		u1->add_id(m_sampler);
		u1->add_id(lookup);
		TypedArray<Ref<RDUniform>> uniforms;
		uniforms.push_back(u0);
		uniforms.push_back(u1);
		const RID set = UniformSetCacheRD::get_cache(m_shader, 0, uniforms);
		PackedFloat32Array push;
		push.push_back((float)size.x);
		push.push_back((float)size.y);
		push.push_back(m_blend);
		push.push_back(0.0f);
		const PackedByteArray bytes = push.to_byte_array();
		const int64_t list = rd->compute_list_begin();
		rd->compute_list_bind_compute_pipeline(list, m_pipeline);
		rd->compute_list_bind_uniform_set(list, set, 0);
		rd->compute_list_set_push_constant(list, bytes, (uint32_t)bytes.size());
		rd->compute_list_dispatch(list, (uint32_t)((size.x + 7) / 8), (uint32_t)((size.y + 7) / 8), 1);
		rd->compute_list_end();
	}
	++m_passes;
}

void MapPostEffectsHost::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("get_post_effect_stats"), &MapPostEffectsHost::get_post_effect_stats);
}

Dictionary MapPostEffectsHost::get_post_effect_stats() const
{
	Dictionary d;
	d["installed"] = !m_cameras.empty();
	d["passes"] = m_effect.is_valid() ? m_effect->passes() : (int64_t)0;
	d["blend"] = m_effect.is_valid() ? m_effect->blend() : 0.0f;
	d["problem"] = m_effect.is_valid() ? String(m_effect->problem().c_str()) : String();
	return d;
}

void MapPostEffectsHost::ensureOnCamera()
{
	if (m_effect.is_null() || !is_inside_tree())
	{
		return;
	}
	Viewport *vp = get_viewport();
	Camera3D *cam = vp ? vp->get_camera_3d() : nullptr;
	if (!cam)
	{
		return;
	}
	Ref<Compositor> comp = cam->get_compositor();
	if (comp.is_null())
	{
		comp.instantiate();
		cam->set_compositor(comp);
	}
	TypedArray<Ref<CompositorEffect>> effects = comp->get_compositor_effects();
	const int64_t n = effects.size();
	if (n > 0 && Ref<CompositorEffect>(effects[n - 1]) == m_effect)
	{
		return; // already last (after GammaComposite's decode)
	}
	for (int64_t i = n - 1; i >= 0; --i)
	{
		if (Ref<CompositorEffect>(effects[i]) == m_effect)
		{
			effects.remove_at(i);
		}
	}
	effects.push_back(m_effect);
	comp->set_compositor_effects(effects);
	const ObjectID id(cam->get_instance_id());
	bool known = false;
	for (const ObjectID &c : m_cameras)
	{
		known = known || c == id;
	}
	if (!known)
	{
		m_cameras.push_back(id);
	}
}

void MapPostEffectsHost::removeFromCameras()
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
			if (Ref<CompositorEffect>(effects[i]) == m_effect)
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

void MapPostEffectsHost::_notification(int what)
{
	if (what == NOTIFICATION_READY)
	{
		set_process_internal(true);
	}
	if (what == NOTIFICATION_READY || what == NOTIFICATION_INTERNAL_PROCESS)
	{
		ensureOnCamera();
	}
	if (what == NOTIFICATION_EXIT_TREE)
	{
		removeFromCameras();
	}
}

} // namespace godot
