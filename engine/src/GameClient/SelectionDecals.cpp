// OpenBFME. GPL-3.0.
// See GameClient/SelectionDecals.h for the target facts.

#include "GameClient/SelectionDecals.h"

#include "Common/Player.h"
#include "Common/Team.h"
#include "GameClient/Drawable.h"
#include "GameClient/DrawableManager.h"
#include "GameLogic/ExperienceLevels.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Object/Contain/HordeContainCore.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/Object/ExperienceTracker.h"
#include "GameLogic/Object/Object.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace
{
const ExperienceLevelTemplate::SelectionDecal *decalOf(const Object &obj)
{
	// the drawable's decal template (+ 0x44C) is the one ExperienceTracker::setLevel gave it (RW 0x79DACD): the current level's SelectionDecal
	const ExperienceTracker *xp = obj.getExperienceTracker();
	if (!xp || !TheExperienceLevelSystem)
	{
		return nullptr;
	}
	const ExperienceLevelTemplate *level = TheExperienceLevelSystem->findLevel(xp->getLevelName());
	return level ? &level->m_selectionDecal : nullptr;
}

std::uint32_t indicatorColor(const Object &obj)
{
	// RW 0x6916CD -> RW 0x68B6F5: the team's controlling player's colour (the custom indicator and the disguise colour are S-2522 / S-2456's)
	const Team *team = obj.getTeam();
	const Player *owner = team ? team->getControllingPlayer() : nullptr;
	return owner ? owner->getPlayerColor() : 0xFF000000u;
}

void addDecal(std::vector<SelectionDecal> &out, const Object &obj, const DrawableManager *drawables, unsigned count, std::uint32_t color,
	const SelectionDecalSettings &s, unsigned clientFrame)
{
	const ExperienceLevelTemplate::SelectionDecal *t = decalOf(obj);
	if (!t || t->texture.empty() || count == 0 || t->minRadius == 0.0f || t->maxRadius == 0.0f)
	{
		return; // RW 0x732BC0 .. 0x732BF8
	}
	SelectionDecal d;
	d.object = obj.getID();
	const Drawable *draw = drawables ? drawables->findByObject(obj.getID()) : nullptr;
	d.position = draw ? *draw->getPosition() : *obj.getPosition();
	// RW 0x7326CC: min + (count - 1) * (max - min) / MaxSelectedUnits, at most max
	const float step = (t->maxRadius - t->minRadius) / (float)(t->maxSelectedUnits ? t->maxSelectedUnits : 1);
	float size = (float)(count - 1) * step + t->minRadius;
	if (size > t->maxRadius)
	{
		size = t->maxRadius;
	}
	d.size = size;
	d.texture = t->texture;
	d.texture2 = t->texture2;
	d.style = t->style;
	// RadiusDecal::update: the throb between OpacityMin and OpacityMax over ceil(OpacityThrobTime * 30) client frames
	int opacity = 0;
	if (s.drawIconUI)
	{
		const unsigned cycle = std::max(1u, (unsigned)std::ceil(t->opacityThrobTime * 30.0f));
		const unsigned phase = clientFrame % cycle;
		const float percent = 0.5f * (std::sin((float)phase * 6.28318548f / (float)cycle) + 1.0f);
		opacity = (int)(((t->opacityMax - t->opacityMin) * percent + t->opacityMin) * 255.0f);
	}
	opacity = std::max(0, std::min(255, opacity));
	if (t->rotationsPerMinute != 0.0f)
	{
		d.angle = (float)clientFrame * (t->rotationsPerMinute * 6.28318548f / 60.0f / 30.0f);
	}
	d.color = ((std::uint32_t)opacity << 24) | (color & 0x00FFFFFFu);
	out.push_back(d);
}
} // namespace

std::vector<SelectionDecal> BuildSelectionDecals(GameLogic &logic, const DrawableManager *drawables, const Player *local, const std::vector<ObjectID> &selected,
	const SelectionDecalSettings &s, unsigned clientFrame)
{
	std::vector<SelectionDecal> out;
	if (!s.showSelectedUnitMarker)
	{
		return out; // RW 0x4B2AA4: GlobalData + 0x9A5
	}
	for (ObjectID id : selected)
	{
		const Object *obj = logic.findObjectByID(id);
		if (!obj || obj->isDestroyed() || !local || obj->getControllingPlayer() != local)
		{
			continue; // RW 0x675851 (isLocallyControlled), RW 0x4B2ABF .. 0x4B2AD4, RW 0x86F51E .. 0x86F527
		}
		const HordeContain *horde = obj->getContain() ? dynamic_cast<const HordeContain *>(obj->getContain()) : nullptr;
		if (horde)
		{
			// RW 0x86F4EE
			if (s.useSimpleHordeDecals)
			{
				addDecal(out, *obj, drawables, (unsigned)horde->getContainedItemsList()->size(), indicatorColor(*obj), s, clientFrame);
				continue;
			}
			// the contained members, then the registered ones not contained (the two lists overlap: one decal per member)
			std::set<ObjectID> done;
			for (const Object *m : *horde->getContainedItemsList())
			{
				if (m && !m->isDestroyed() && done.insert(m->getID()).second)
				{
					addDecal(out, *m, drawables, 1, indicatorColor(*m), s, clientFrame);
				}
			}
			for (auto mid : horde->core().registeredMembers())
			{
				const Object *m = logic.findObjectByID((ObjectID)mid);
				if (m && !m->isDestroyed() && done.insert(m->getID()).second)
				{
					addDecal(out, *m, drawables, 1, indicatorColor(*m), s, clientFrame);
				}
			}
			continue;
		}
		addDecal(out, *obj, drawables, 1, indicatorColor(*obj), s, clientFrame);
	}
	return out;
}

void ApplyDecalLOD(SelectionDecalSettings &settings, int decalLOD)
{
	settings.showSelectedUnitMarker = decalLOD > 0; // RW 0x601C62: GlobalData + 0x9A5 = record + 0x40 > 0
	settings.useSimpleMergeDecals = decalLOD < 2;   // GlobalData + 0x9A7 = record + 0x40 < 2
}

namespace
{
constexpr std::uint32_t kMergeStyle = 0x1000u; // SHADOW_MERGE_DECAL
constexpr int kPassAlphaRef = 0x60;            // INFERENCE (S-2522): ZH ShaderClass::Apply's ALPHAREF for the alpha-tested shaders

// bilinear RGBA at (u, v) in [0, 1], v = 0 the top row (the GPU filters before the alpha test)
void sample(const DecalImage &img, float u, float v, float out[4])
{
	const float x = u * (float)img.width - 0.5f, y = v * (float)img.height - 0.5f;
	const int x0 = (int)std::floor(x), y0 = (int)std::floor(y);
	const float fx = x - (float)x0, fy = y - (float)y0;
	auto texel = [&](int tx, int ty, int c) -> float {
		tx = std::max(0, std::min(img.width - 1, tx));
		ty = std::max(0, std::min(img.height - 1, ty));
		return (float)img.rgba[((size_t)ty * (size_t)img.width + (size_t)tx) * 4 + (size_t)c];
	};
	for (int c = 0; c < 4; ++c)
	{
		const float top = texel(x0, y0, c) * (1.0f - fx) + texel(x0 + 1, y0, c) * fx;
		const float bottom = texel(x0, y0 + 1, c) * (1.0f - fx) + texel(x0 + 1, y0 + 1, c) * fx;
		out[c] = (top * (1.0f - fy) + bottom * fy) / 255.0f;
	}
}
} // namespace

bool ComposeMergeDecals(const std::vector<SelectionDecal> &decals, const std::function<DecalImage(const std::string &)> &image, const SelectionDecalSettings &settings,
	MergedSelectionDecals &out, std::string *error, int maxPixels, float maxTexelsPerUnit)
{
	out = MergedSelectionDecals();
	struct Item
	{
		const SelectionDecal *d;
		DecalImage tex, cut;
	};
	std::vector<Item> items;
	float density = 0.0f;
	bool first = true;
	for (const SelectionDecal &d : decals)
	{
		if (!(d.style & kMergeStyle) || d.size <= 0.0f)
		{
			continue;
		}
		Item it{ &d, image(d.texture), {} };
		if (it.tex.width <= 0 || !it.tex.rgba)
		{
			if (error)
			{
				*error = "selection decal texture " + d.texture + " not found";
			}
			return false;
		}
		if (!settings.useSimpleMergeDecals)
		{
			// pass 0 binds Texture2 (flushDecals, stencil mode 0)
			it.cut = d.texture2.empty() ? DecalImage() : image(d.texture2);
			if (it.cut.width <= 0 || !it.cut.rgba)
			{
				if (error)
				{
					*error = "selection decal " + d.texture + ": its Texture2 '" + d.texture2 + "' (the stencil pass's cut-out) not found";
				}
				return false;
			}
		}
		const float h = d.size * 0.5f;
		if (first)
		{
			out.minX = d.position.x - h;
			out.maxX = d.position.x + h;
			out.minY = d.position.y - h;
			out.maxY = d.position.y + h;
			out.z = d.position.z;
			first = false;
		}
		out.minX = std::min(out.minX, d.position.x - h);
		out.maxX = std::max(out.maxX, d.position.x + h);
		out.minY = std::min(out.minY, d.position.y - h);
		out.maxY = std::max(out.maxY, d.position.y + h);
		out.z = std::min(out.z, d.position.z);
		density = std::max(density, (float)it.tex.width / d.size);
		items.push_back(it);
	}
	out.decals = items.size();
	if (items.empty())
	{
		return true;
	}
	const float spanX = out.maxX - out.minX, spanY = out.maxY - out.minY;
	density = std::min(density, (float)maxPixels / std::max(spanX, spanY));
	if (maxTexelsPerUnit > 0.0f)
	{
		density = std::min(density, maxTexelsPerUnit);
	}
	out.width = std::max(1, (int)std::ceil(spanX * density));
	out.height = std::max(1, (int)std::ceil(spanY * density));
	out.rgba.assign((size_t)out.width * (size_t)out.height * 4, 0);
	std::vector<std::uint8_t> stencil((size_t)out.width * (size_t)out.height, 0);

	// visit each output pixel a decal covers: (world x, y) at the pixel's centre and the decal's (u, v)
	auto forPixels = [&](const SelectionDecal &d, const std::function<void(size_t, float, float)> &fn) {
		const float h = d.size * 0.5f;
		const int px0 = std::max(0, (int)std::floor((d.position.x - h - out.minX) * density));
		const int px1 = std::min(out.width, (int)std::ceil((d.position.x + h - out.minX) * density));
		const int py0 = std::max(0, (int)std::floor((out.maxY - (d.position.y + h)) * density));
		const int py1 = std::min(out.height, (int)std::ceil((out.maxY - (d.position.y - h)) * density));
		for (int py = py0; py < py1; ++py)
		{
			const float y = out.maxY - ((float)py + 0.5f) / density;
			const float v = ((d.position.y + h) - y) / d.size;
			for (int px = px0; px < px1; ++px)
			{
				const float x = out.minX + ((float)px + 0.5f) / density;
				const float u = (x - (d.position.x - h)) / d.size;
				if (u >= 0.0f && u <= 1.0f && v >= 0.0f && v <= 1.0f)
				{
					fn((size_t)py * (size_t)out.width + (size_t)px, u, v);
				}
			}
		}
	};
	auto alpha8 = [](float texelAlpha, std::uint32_t color) { return (int)std::lround(texelAlpha * (float)(color >> 24)); };
	auto write = [&](size_t at, const float t[4], std::uint32_t color, int a) {
		std::uint8_t *p = &out.rgba[at * 4];
		p[0] = (std::uint8_t)std::lround(t[0] * (float)((color >> 16) & 0xFF));
		p[1] = (std::uint8_t)std::lround(t[1] * (float)((color >> 8) & 0xFF));
		p[2] = (std::uint8_t)std::lround(t[2] * (float)(color & 0xFF));
		p[3] = (std::uint8_t)a;
	};
	float t[4];
	if (settings.useSimpleMergeDecals)
	{
		// one pass: stencil NOT EQUAL / REPLACE, the alpha test at OpacityOfSimpleMergeDecals, blended
		const int ref = std::max(0, std::min(255, (int)(settings.opacityOfSimpleMergeDecals * 255.0f)));
		for (const Item &it : items)
		{
			forPixels(*it.d, [&](size_t at, float u, float v) {
				if (stencil[at])
				{
					return;
				}
				sample(it.tex, u, v, t);
				const int a = alpha8(t[3], it.d->color);
				if (a >= ref)
				{
					stencil[at] = 1;
					write(at, t, it.d->color, a);
				}
			});
		}
		return true;
	}
	// pass 0: every cut-out into the stencil (ALWAYS / REPLACE, alpha-tested, no colour)
	for (const Item &it : items)
	{
		forPixels(*it.d, [&](size_t at, float u, float v) {
			sample(it.cut, u, v, t);
			if (alpha8(t[3], it.d->color) >= kPassAlphaRef)
			{
				stencil[at] = 1;
			}
		});
	}
	// pass 1: every disc where no cut-out lies (NOT EQUAL, KEEP), alpha-tested, opaque
	for (const Item &it : items)
	{
		forPixels(*it.d, [&](size_t at, float u, float v) {
			if (stencil[at])
			{
				return;
			}
			sample(it.tex, u, v, t);
			if (alpha8(t[3], it.d->color) >= kPassAlphaRef)
			{
				write(at, t, it.d->color, 255);
			}
		});
	}
	return true;
}
