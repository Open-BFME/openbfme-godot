// OpenBFME. GPL-3.0.
// Lane BLOBSHADOW-1: the GameWorld side of retail's SHADOW_DECAL blob shadows (GameClient/BlobShadow.h, GodotDevice/GodotBlobShadows.h). Client only:
// it reads the presented drawables after refresh_views and draws nothing into the simulation.

#include "GodotDevice/GodotGameWorld.h"

#include "GameClient/BlobShadow.h"
#include "GameClient/Drawable.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DModelDraw.h"
#include "GodotDevice/GodotRetailFileSystem.h"
#include "GodotDevice/GodotW3DModelBuilder.h"

#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/viewport.hpp>

#include <algorithm>
#include <cmath>

using namespace godot;

void GameWorld::set_shadow_decals(bool decals, bool volumes)
{
	s_shadowDecals = decals;
	s_shadowVolumes = volumes;
}

Dictionary GameWorld::get_blob_report() const
{
	Dictionary d;
	d["count"] = m_blobs.lastCount();
	d["rect"] = Array::make(m_blobRect[0], m_blobRect[1], m_blobRect[2], m_blobRect[3]);
	d["decals"] = s_shadowDecals;
	Array missing;
	for (const std::string &m : m_blobs.missingTextures())
	{
		missing.push_back(String(m.c_str()));
	}
	d["missing_textures"] = missing;
	return d;
}

namespace
{
bool isNoneModel(const std::string &name)
{
	return name.size() == 4 && (name[0] | 0x20) == 'n' && (name[1] | 0x20) == 'o' && (name[2] | 0x20) == 'n' && (name[3] | 0x20) == 'e';
}

// the camera's ground footprint (SAGE x / y bounds of the four viewport corners' rays on the plane z = `groundZ`), clamped to `maxSpan` around the
// centre ray's ground point. False without a camera.
bool groundFootprint(Viewport *vp, float groundZ, float maxSpan, float out[4])
{
	Camera3D *cam = vp ? vp->get_camera_3d() : nullptr;
	if (!cam)
	{
		return false;
	}
	const Vector2 size = vp->get_visible_rect().size;
	auto hit = [&](const Vector2 &px, Vector2 &sage) {
		const godot::Vector3 o = cam->project_ray_origin(px);
		const godot::Vector3 n = cam->project_ray_normal(px);
		// Godot (x, y, z) = SAGE (x, z, -y); a ray that never comes down to the plane stops at maxSpan from the camera
		float t = maxSpan;
		if (n.y < -1e-4f)
		{
			t = std::min(maxSpan, (groundZ - o.y) / n.y);
		}
		const godot::Vector3 p = o + n * std::max(t, 0.0f);
		sage = Vector2(p.x, -p.z);
	};
	Vector2 c, a, b, d, e;
	hit(size * 0.5f, c);
	hit(Vector2(0, 0), a);
	hit(Vector2(size.x, 0), b);
	hit(Vector2(0, size.y), d);
	hit(size, e);
	float x0 = std::min({ a.x, b.x, d.x, e.x }), x1 = std::max({ a.x, b.x, d.x, e.x });
	float y0 = std::min({ a.y, b.y, d.y, e.y }), y1 = std::max({ a.y, b.y, d.y, e.y });
	const float half = 0.5f * maxSpan;
	x0 = std::max(x0, c.x - half);
	x1 = std::min(x1, c.x + half);
	y0 = std::max(y0, c.y - half);
	y1 = std::min(y1, c.y + half);
	const float margin = 40.0f; // a blob whose centre is just outside still reaches in
	out[0] = x0 - margin;
	out[1] = y0 - margin;
	out[2] = (x1 - x0) + 2.0f * margin;
	out[3] = (y1 - y0) + 2.0f * margin;
	return out[2] > 0.0f && out[3] > 0.0f;
}
} // namespace

void GameWorld::update_blob_shadows()
{
	if (!m_game || m_fs.is_null() || !is_inside_tree())
	{
		return;
	}
	if (!s_shadowDecals)
	{
		// ShadowLOD Off, High, UltraHigh: retail draws no blob (RW 0x50BF7A; INFERENCE, S-3304: the few ShadowOverrideLODVisibility objects are not
		// kept at those levels here)
		m_blobs.disable();
		return;
	}
	if (m_blobSpecsVolumes != s_shadowVolumes)
	{
		m_blobSpecs.clear();
		m_blobSpecsVolumes = s_shadowVolumes;
	}
	// the ground height under the view's centre: the footprint's plane
	Viewport *vp = get_viewport();
	Camera3D *cam = vp ? vp->get_camera_3d() : nullptr;
	if (!cam)
	{
		m_blobs.disable();
		return;
	}
	const godot::Vector3 camPos = cam->get_global_position();
	const godot::Vector3 fwd = -cam->get_global_transform().basis.get_column(2);
	float groundZ = 0.0f;
	if (fwd.y < -1e-4f)
	{
		const godot::Vector3 p = camPos + fwd * (-camPos.y / fwd.y);
		groundZ = m_game->logic().getGroundHeight(p.x, -p.z);
	}
	float rect[4];
	if (!groundFootprint(vp, groundZ, BlobShadowLayer::kMaxSpan, rect))
	{
		m_blobs.disable();
		return;
	}
	if (!m_blobs.attached())
	{
		Ref<RetailFileSystem> fs = m_fs;
		Ref<W3DModelBuilder> loader;
		loader.instantiate();
		m_blobs.attach(this, [fs, loader](const std::string &file) { return loader->load_texture(fs, String(file.c_str())); });
	}
	std::copy(rect, rect + 4, m_blobRect);
	m_blobs.begin(rect[0], rect[1], rect[2], rect[3]);
	DrawableManager &dm = m_game->drawables();
	GameLogic &logic = m_game->logic();
	for (size_t id = 1; id < m_views.size(); ++id)
	{
		const DrawableView &v = m_views[id];
		if (!v.valid || v.fogHidden || v.fogLook)
		{
			// the presented look leaves it undrawn (shroud, a hidden or contained drawable) or draws a fogged ghost: retail hides the blob of a drawable
			// whose cell is FOGGED or worse (setFullyObscuredByShroud -> enableShadowInvisible, BFME2 GameClientDrawableTOC.cpp, W3DProjectedShadow.cpp)
			continue;
		}
		const Drawable *d = dm.find((DrawableID)id);
		if (!d || d->drawOpacity() <= 0.0f || !d->shadowsEnabled())
		{
			continue;
		}
		const Coord3D p = *d->getPosition();
		if (p.x < rect[0] || p.y < rect[1] || p.x > rect[0] + rect[2] || p.y > rect[1] + rect[3])
		{
			continue;
		}
		for (const DrawEntry &e : d->entries())
		{
			if (e.kind != W3D_DRAWKIND_MODEL || e.missingModel || e.moduleHidden || e.conditionHidden || !e.draw)
			{
				continue;
			}
			const auto *module = dynamic_cast<const W3DModelDrawModuleData *>(e.data);
			if (!module)
			{
				continue;
			}
			const ModelConditionInfo *state = e.draw->currentModelState();
			// a draw without a render object makes no shadow (BFME2 allocateShadows 0xB9B4A: only with a render object; a horde's Model = None)
			bool rendered = false;
			for (const std::string &name : state ? state->modelNames : std::vector<std::string>())
			{
				rendered = rendered || (!name.empty() && !isNoneModel(name));
			}
			if (!rendered)
			{
				continue;
			}
			BlobSpecCache &cached = m_blobSpecs[std::make_tuple((const void *)d->getTemplate(), (const void *)module, (const void *)state)];
			if (!cached.resolved)
			{
				cached.has = BlobShadow::resolve(d->getTemplate(), module, state, s_shadowVolumes, cached.spec);
				cached.resolved = true;
			}
			if (!cached.has)
			{
				continue;
			}
			const BlobShadow::Spec &spec = cached.spec;
			// one blob per drawable: the first model draw that names one (the object template's shadow belongs to the drawable, not to each module)
			if (BlobShadow::drawnAtLOD(spec, s_shadowDecals) && BlobShadow::belowMaxHeight(spec, p.z, logic.getGroundHeight(p.x, p.y)))
			{
				BlobShadow::Quad q;
				if (BlobShadow::quad(spec, p.x, p.y, d->getOrientation(), 0.0f, 0.0f, q))
				{
					m_blobs.add(spec.texture, q);
				}
			}
			break;
		}
	}
	m_blobs.end();
}
