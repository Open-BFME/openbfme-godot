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
#include <set>

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
	d["decal_shadows"] = (int64_t)m_decalShadows.size(); // lane DRAWDECAL-1
	Array missing;
	for (const std::string &m : m_blobs.missingTextures())
	{
		missing.push_back(String(m.c_str()));
	}
	d["missing_textures"] = missing;
	return d;
}

Array GameWorld::get_decal_shadows() const
{
	// lane DRAWDECAL-1: this frame's drawable decal shadows as the HUD's grid gets them (SAGE coordinates)
	Array out;
	for (const SelectionDecal &s : m_decalShadows)
	{
		Dictionary e;
		e["object"] = (int64_t)s.object;
		e["drawable"] = (int64_t)s.drawable;
		e["x"] = s.position.x;
		e["y"] = s.position.y;
		e["z"] = s.position.z;
		e["size"] = s.size;
		e["size_y"] = s.sizeY;
		e["angle"] = s.angle;
		e["texture"] = String(s.texture.c_str());
		e["style"] = (int64_t)s.style;
		e["color"] = (int64_t)s.color;
		out.push_back(e);
	}
	return out;
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
		groundZ = m_game->mapGroundHeight(p.x, -p.z); // review: the render thread does not wait for the logic worker here
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
			if (BlobShadow::drawnAtLOD(spec, s_shadowDecals) && BlobShadow::belowMaxHeight(spec, p.z, m_game->mapGroundHeight(p.x, p.y)))
			{
				// lane BLOBSHADOW-2: a zero size takes the model's box (RW 0x50C01D; workers, animals, lairs: 124 retail rows have no size)
				float extentX = 0.0f, extentY = 0.0f;
				if (spec.sizeX == 0.0f || spec.sizeY == 0.0f)
				{
					BlobShadow::modelExtent(e.draw->currentModel(), extentX, extentY);
				}
				BlobShadow::Quad q;
				if (BlobShadow::quad(spec, p.x, p.y, d->getOrientation(), extentX, extentY, q))
				{
					m_blobs.add(spec.texture, q);
				}
			}
			break;
		}
	}
	m_blobs.end();
}

void GameWorld::update_decal_shadows(double deltaMs)
{
	// lane DRAWDECAL-1: every presented drawable's alpha / additive decal shadow (GameClient/BlobShadow.h resolveDecal), whatever the camera sees (the
	// HUD's grid places them); drawn for the same drawables as the blobs: none for a shrouded, fogged, hidden, faded-out or shadowless one
	// (setFullyObscuredByShroud -> enableShadowInvisible). A _DYNAMIC one fades (BlobShadow::DecalFade): in from the frame its shadow appears (the model
	// draw's shadow setup), out from the frame it goes (its drawable gone, or its state naming another or none), standing where it was last drawn.
	// The client clock: 30 frames a second of render time (TheGameClient's frame, as Radar::advanceClock); it stands while the game is paused, as this
	// update does. INFERENCE (S-3304): a state change that names the same decal keeps the shadow (and its fade); retail draws a _DYNAMIC one (0xC00)
	// also while its render object is not really visible (BFME2 renderShadows), here it goes with the drawable's look
	m_decalShadows.clear();
	if (!m_game)
	{
		return;
	}
	m_decalClockMs += deltaMs > 0.0 ? deltaMs : 0.0;
	const unsigned now = (unsigned)(m_decalClockMs * 30.0 / 1000.0);
	DrawableManager &dm = m_game->drawables();
	std::set<uint32_t> seen;
	for (size_t id = 1; id < m_views.size(); ++id)
	{
		const DrawableView &v = m_views[id];
		const Drawable *d = v.valid ? dm.find((DrawableID)id) : nullptr;
		if (!d)
		{
			continue;
		}
		const Coord3D p = *d->getPosition();
		for (const DrawEntry &e : d->entries())
		{
			if (e.kind != W3D_DRAWKIND_MODEL || e.missingModel || !e.draw)
			{
				continue;
			}
			const auto *module = dynamic_cast<const W3DModelDrawModuleData *>(e.data);
			if (!module)
			{
				continue;
			}
			const ModelConditionInfo *state = e.draw->currentModelState();
			bool rendered = false; // as the blobs: a draw without a render object makes no shadow (BFME2 allocateShadows 0xB9B4A)
			for (const std::string &name : state ? state->modelNames : std::vector<std::string>())
			{
				rendered = rendered || (!name.empty() && !isNoneModel(name));
			}
			if (!rendered)
			{
				continue;
			}
			DecalSpecCache &cached = m_decalSpecs[std::make_tuple((const void *)d->getTemplate(), (const void *)module, (const void *)state)];
			if (!cached.resolved)
			{
				cached.has = BlobShadow::resolveDecal(d->getTemplate(), module, state, cached.spec);
				cached.resolved = true;
			}
			// the projected manager keeps no shadow below its ShadowLOD (RW 0x50BF7A): none is made, nothing fades
			if (!cached.has || !BlobShadow::drawnAtLOD(cached.spec, s_shadowDecals))
			{
				continue;
			}
			// one per drawable, as the blobs: the first model draw that names one
			const BlobShadow::DecalSpec &spec = cached.spec;
			seen.insert((uint32_t)id);
			DecalFadeView &fv = m_decalFades[(uint32_t)id];
			if (!fv.valid || fv.object != d->getObjectID() || !(fv.spec == spec))
			{
				if (fv.valid && fv.hasLast && BlobShadow::isDynamic(fv.spec.type))
				{
					m_decalFadingOut.push_back({ fv.last, BlobShadow::fadeOut(fv.fade, now) });
				}
				fv = DecalFadeView();
				fv.valid = true;
				fv.object = d->getObjectID();
				fv.spec = spec;
				fv.fade = BlobShadow::fadeIn(spec, now);
			}
			const bool shown = !v.fogHidden && !v.fogLook && d->drawOpacity() > 0.0f && d->shadowsEnabled() && !e.moduleHidden && !e.conditionHidden;
			SelectionDecal decal;
			if (shown && BlobShadow::belowMaxHeight(spec, p.z, m_game->mapGroundHeight(p.x, p.y)) && BlobShadow::decal(spec, p.x, p.y, p.z, d->getOrientation(), decal))
			{
				decal.drawable = (unsigned)id;
				decal.object = d->getObjectID();
				unsigned opacity = spec.opacityPeak;
				if (BlobShadow::isDynamic(spec.type) && !BlobShadow::opacityAt(fv.fade, now, opacity))
				{
					break;
				}
				decal.color = DecalShadowColor(opacity);
				fv.last = decal;
				fv.hasLast = true;
				m_decalShadows.push_back(std::move(decal));
			}
			break;
		}
	}
	// the shadows that went: a _DYNAMIC one fades out where it was last drawn (RW 0x508B40)
	for (auto it = m_decalFades.begin(); it != m_decalFades.end();)
	{
		if (seen.count(it->first))
		{
			++it;
			continue;
		}
		if (it->second.hasLast && BlobShadow::isDynamic(it->second.spec.type))
		{
			m_decalFadingOut.push_back({ it->second.last, BlobShadow::fadeOut(it->second.fade, now) });
		}
		it = m_decalFades.erase(it);
	}
	for (auto it = m_decalFadingOut.begin(); it != m_decalFadingOut.end();)
	{
		unsigned opacity = 0;
		if (!BlobShadow::opacityAt(it->second, now, opacity))
		{
			it = m_decalFadingOut.erase(it);
			continue;
		}
		SelectionDecal decal = it->first;
		decal.color = DecalShadowColor(opacity);
		m_decalShadows.push_back(std::move(decal));
		++it;
	}
}
