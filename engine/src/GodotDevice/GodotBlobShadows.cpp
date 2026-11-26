// OpenBFME. GPL-3.0.
// See GodotDevice/GodotBlobShadows.h.

#include "GodotDevice/GodotBlobShadows.h"
#include "GodotDevice/GodotW3DMaterial.h"

#include <godot_cpp/classes/canvas_item.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/classes/viewport_texture.hpp>

#include <algorithm>

namespace godot
{
BlobShadowLayer::~BlobShadowLayer()
{
	// the nodes went with the parent (it frees its children first); only the globals are left to clear
	if (RenderingServer *rs = RenderingServer::get_singleton())
	{
		rs->global_shader_parameter_set(StringName("blob_shadow_rect"), Vector4(0.0f, 0.0f, 0.0f, 0.0f));
	}
}

SubViewport *BlobShadowLayer::alive() const
{
	return m_viewportId ? Object::cast_to<SubViewport>(ObjectDB::get_instance(m_viewportId)) : nullptr;
}

void BlobShadowLayer::attach(Node *parent, TextureLoader load)
{
	if (alive() || !parent)
	{
		return;
	}
	m_batches.clear(); // a former viewport's batches went with it
	m_load = std::move(load);
	m_viewport = memnew(SubViewport);
	m_viewport->set_name("BlobShadowViewport");
	m_viewport->set_size(Vector2i(kSize, kSize));
	m_viewport->set_disable_3d(true);
	m_viewport->set_transparent_background(false);
	m_viewport->set_update_mode(SubViewport::UPDATE_DISABLED);
	m_viewport->set_clear_mode(SubViewport::CLEAR_MODE_ALWAYS);
	parent->add_child(m_viewport);
	m_viewportId = m_viewport->get_instance_id();
	// cleared to white: a texel no blob covers multiplies by 1
	m_clear = memnew(ColorRect);
	m_clear->set_color(Color(1, 1, 1, 1));
	m_clear->set_position(Vector2());
	m_clear->set_size(Vector2(kSize, kSize));
	m_viewport->add_child(m_clear);
	m_quad.instantiate();
	m_quad->set_size(Vector2(1, 1));
	m_material.instantiate();
	Ref<Shader> shader = ResourceLoader::get_singleton()->load("res://shaders/blob_shadow_quad.gdshader");
	m_material->set_shader(shader);
}

void BlobShadowLayer::detach()
{
	disable();
	if (SubViewport *vp = alive())
	{
		if (vp->get_parent())
		{
			vp->get_parent()->remove_child(vp);
		}
		memdelete(vp); // its children (the clear rect, the batches) go with it
	}
	m_viewport = nullptr;
	m_viewportId = 0;
	m_clear = nullptr;
	m_batches.clear();
}

BlobShadowLayer::Batch *BlobShadowLayer::batch(const std::string &file)
{
	auto it = m_batches.find(file);
	if (it != m_batches.end())
	{
		return &it->second;
	}
	Batch &b = m_batches[file];
	const Ref<Texture2D> texture = m_load ? m_load(file) : Ref<Texture2D>();
	if (texture.is_null())
	{
		m_missing.push_back(file); // reported once by the caller (GameWorld's blob report); retail's asset manager draws a missing texture as nothing
		return &b;
	}
	b.mm.instantiate();
	b.mm->set_transform_format(MultiMesh::TRANSFORM_2D);
	b.mm->set_mesh(m_quad);
	b.node = memnew(MultiMeshInstance2D);
	b.node->set_name(String("Blobs_") + String(file.c_str()));
	b.node->set_multimesh(b.mm);
	b.node->set_texture(texture);
	b.node->set_material(m_material);
	b.node->set_texture_filter(CanvasItem::TEXTURE_FILTER_LINEAR);
	m_viewport->add_child(b.node);
	b.valid = true;
	return &b;
}

void BlobShadowLayer::begin(float x0, float y0, float width, float height)
{
	m_x0 = x0;
	m_y0 = y0;
	m_sx = width > 0.0f ? (float)kSize / width : 0.0f;
	m_sy = height > 0.0f ? (float)kSize / height : 0.0f;
	for (auto &it : m_batches)
	{
		it.second.count = 0;
		it.second.buffer.clear();
	}
	m_active = alive() != nullptr && m_sx > 0.0f && m_sy > 0.0f;
}

void BlobShadowLayer::add(const std::string &textureFile, const BlobShadow::Quad &q)
{
	if (!m_active)
	{
		return;
	}
	Batch *b = batch(textureFile);
	if (!b || !b->valid)
	{
		return;
	}
	// the unit quad (QuadMesh 1 x 1 about the origin) stretched to the blob's full u / v sides, in the viewport's texels (texel row 0 = min y)
	const float ox = (q.cx - m_x0) * m_sx, oy = (q.cy - m_y0) * m_sy;
	const float xx = 2.0f * q.ux * m_sx, xy = 2.0f * q.uy * m_sy; // basis x: the u side
	const float yx = 2.0f * q.vx * m_sx, yy = 2.0f * q.vy * m_sy; // basis y: the v side
	// MultiMesh TRANSFORM_2D: (x.x, y.x, 0, origin.x, x.y, y.y, 0, origin.y)
	const float rec[8] = { xx, yx, 0.0f, ox, xy, yy, 0.0f, oy };
	b->buffer.insert(b->buffer.end(), rec, rec + 8);
	++b->count;
}

void BlobShadowLayer::end()
{
	RenderingServer *rs = RenderingServer::get_singleton();
	if (!m_active || !rs)
	{
		disable();
		return;
	}
	int total = 0;
	for (auto &it : m_batches)
	{
		Batch &b = it.second;
		if (!b.valid)
		{
			continue;
		}
		if (b.mm->get_instance_count() < b.count)
		{
			b.mm->set_instance_count(std::max(b.count, b.mm->get_instance_count() * 2));
		}
		PackedFloat32Array data;
		data.resize((int64_t)b.mm->get_instance_count() * 8);
		std::copy(b.buffer.begin(), b.buffer.end(), data.ptrw());
		b.mm->set_buffer(data);
		b.mm->set_visible_instance_count(b.count);
		b.node->set_visible(b.count > 0);
		total += b.count;
	}
	m_lastCount = total;
	if (total == 0)
	{
		disable();
		return;
	}
	m_viewport->set_update_mode(SubViewport::UPDATE_ALWAYS);
	// the globals exist once W3D_Ensure_Light_Globals ran (a set before is lost): the texture goes with every frame's rectangle
	W3D_Ensure_Light_Globals();
	rs->global_shader_parameter_set(StringName("blob_shadow_tex"), m_viewport->get_texture()->get_rid()); // a sampler global takes the RID (as sel_data)
	rs->global_shader_parameter_set(StringName("blob_shadow_rect"), Vector4(m_x0, m_y0, (float)kSize / m_sx, (float)kSize / m_sy));
}

void BlobShadowLayer::disable()
{
	m_active = false;
	m_lastCount = 0;
	if (RenderingServer *rs = RenderingServer::get_singleton())
	{
		rs->global_shader_parameter_set(StringName("blob_shadow_rect"), Vector4(0.0f, 0.0f, 0.0f, 0.0f));
	}
	if (SubViewport *vp = alive())
	{
		vp->set_update_mode(SubViewport::UPDATE_DISABLED);
	}
}
} // namespace godot
