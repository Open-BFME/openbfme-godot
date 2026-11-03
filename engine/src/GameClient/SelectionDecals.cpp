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
#include "GameLogic/ObjectTemplateInfo.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>

namespace
{
// lane SELPERF-1: what one BuildSelectionDecals call looks up once per level instead of once per member (a horde's members share their level): the
// level's decal template by the level's name, the opacity of the throb by the template
struct DecalCache
{
	const std::string *levelName = nullptr; ///< the last level name looked up (compared by value)
	const ExperienceLevelTemplate::SelectionDecal *decal = nullptr;
	const ExperienceLevelTemplate::SelectionDecal *opacityOf = nullptr;
	int opacity = 0;
};

const ExperienceLevelTemplate::SelectionDecal *decalOf(const Object &obj, DecalCache &cache)
{
	// the drawable's decal template (+ 0x44C) is the one ExperienceTracker::setLevel gave it (RW 0x79DACD): the current level's SelectionDecal
	const ExperienceTracker *xp = obj.getExperienceTracker();
	if (!xp || !TheExperienceLevelSystem)
	{
		return nullptr;
	}
	const std::string &name = xp->getLevelName();
	if (cache.levelName && *cache.levelName == name)
	{
		return cache.decal;
	}
	const ExperienceLevelTemplate *level = TheExperienceLevelSystem->findLevel(name);
	cache.levelName = &name;
	cache.decal = level ? &level->m_selectionDecal : nullptr;
	return cache.decal;
}

// RadiusDecal::update's opacity (0..255) and turn at a client frame
int throbOpacity(const SelectionDecal &d, const SelectionDecalSettings &s, unsigned clientFrame)
{
	int opacity = 0;
	if (s.drawIconUI)
	{
		const unsigned phase = clientFrame % d.throbCycle;
		const float percent = 0.5f * (std::sin((float)phase * 6.28318548f / (float)d.throbCycle) + 1.0f);
		opacity = (int)(((d.opacityMax - d.opacityMin) * percent + d.opacityMin) * 255.0f);
	}
	return std::max(0, std::min(255, opacity));
}

float turnAngle(const SelectionDecal &d, unsigned clientFrame)
{
	return d.rotationsPerMinute != 0.0f ? (float)clientFrame * (d.rotationsPerMinute * 6.28318548f / 60.0f / 30.0f) : 0.0f;
}

std::uint32_t indicatorColor(const Object &obj)
{
	// RW 0x6916CD -> RW 0x68B6F5: the team's controlling player's colour (the custom indicator and the disguise colour are S-2522 / S-2456's)
	const Team *team = obj.getTeam();
	const Player *owner = team ? team->getControllingPlayer() : nullptr;
	return owner ? owner->getPlayerColor() : 0xFF000000u;
}

void addDecal(std::vector<SelectionDecal> &out, const Object &obj, const DrawableManager *drawables, unsigned count, std::uint32_t color,
	const SelectionDecalSettings &s, unsigned clientFrame, DecalCache &cache)
{
	const ExperienceLevelTemplate::SelectionDecal *t = decalOf(obj, cache);
	if (!t || t->texture.empty() || count == 0 || t->minRadius == 0.0f || t->maxRadius == 0.0f)
	{
		return; // RW 0x732BC0 .. 0x732BF8
	}
	out.emplace_back();
	SelectionDecal &d = out.back();
	d.object = obj.getID();
	const Drawable *draw = drawables ? drawables->findByObject(obj.getID()) : nullptr;
	d.position = draw ? *draw->getPosition() : *obj.getPosition();
	d.drawZ = d.position.z; // lane SELWALL-1
	d.drawable = draw ? (unsigned)draw->getID() : 0u;
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
	// RadiusDecal::update: the throb between OpacityMin and OpacityMax over ceil(OpacityThrobTime * 30) client frames (the same for every decal of the template)
	d.throbCycle = std::max(1u, (unsigned)std::ceil(t->opacityThrobTime * 30.0f));
	d.opacityMin = t->opacityMin;
	d.opacityMax = t->opacityMax;
	d.rotationsPerMinute = t->rotationsPerMinute;
	if (cache.opacityOf != t)
	{
		cache.opacity = throbOpacity(d, s, clientFrame);
		cache.opacityOf = t;
	}
	d.angle = turnAngle(d, clientFrame);
	d.color = ((std::uint32_t)cache.opacity << 24) | (color & 0x00FFFFFFu);
}
} // namespace

std::vector<SelectionDecal> BuildSelectionDecals(GameLogic &logic, const DrawableManager *drawables, const Player *local, const std::vector<ObjectID> &selected,
	const SelectionDecalSettings &s, unsigned clientFrame)
{
	std::vector<SelectionDecal> out;
	BuildSelectionDecals(logic, drawables, local, selected, s, clientFrame, out);
	return out;
}

void BuildSelectionDecals(GameLogic &logic, const DrawableManager *drawables, const Player *local, const std::vector<ObjectID> &selected,
	const SelectionDecalSettings &s, unsigned clientFrame, std::vector<SelectionDecal> &out)
{
	out.clear();
	if (!s.showSelectedUnitMarker)
	{
		return; // RW 0x4B2AA4: GlobalData + 0x9A5
	}
	DecalCache cache;
	std::vector<ObjectID> done; // a horde's members already marked (a few dozen at most: a linear search, no allocation per member)
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
				addDecal(out, *obj, drawables, (unsigned)horde->getContainedItemsList()->size(), indicatorColor(*obj), s, clientFrame, cache);
				continue;
			}
			// the contained members, then the registered ones not contained (the two lists overlap: one decal per member)
			done.clear();
			auto first = [&done](ObjectID m) {
				if (std::find(done.begin(), done.end(), m) != done.end())
				{
					return false;
				}
				done.push_back(m);
				return true;
			};
			for (const Object *m : *horde->getContainedItemsList())
			{
				if (m && !m->isDestroyed() && first(m->getID()))
				{
					addDecal(out, *m, drawables, 1, indicatorColor(*m), s, clientFrame, cache);
				}
			}
			for (auto mid : horde->core().registeredMembers())
			{
				const Object *m = logic.findObjectByID((ObjectID)mid);
				if (m && !m->isDestroyed() && first(m->getID()))
				{
					addDecal(out, *m, drawables, 1, indicatorColor(*m), s, clientFrame, cache);
				}
			}
			continue;
		}
		addDecal(out, *obj, drawables, 1, indicatorColor(*obj), s, clientFrame, cache);
	}
}

bool RefreshSelectionDecals(const DrawableManager *drawables, const SelectionDecalSettings &settings, unsigned clientFrame, std::vector<SelectionDecal> &list)
{
	bool changed = false;
	const SelectionDecal *last = nullptr; // the throb is the template's: a horde's members share it
	int opacity = 0;
	for (SelectionDecal &d : list)
	{
		// the drawable by its slot (DrawableManager never reuses an id), else by the object (a drawable made after the build)
		const Drawable *draw = drawables && d.drawable ? drawables->find((DrawableID)d.drawable) : nullptr;
		if (!draw || draw->getObjectID() != d.object)
		{
			draw = drawables ? drawables->findByObject(d.object) : nullptr;
			d.drawable = draw ? (unsigned)draw->getID() : 0u;
		}
		if (draw)
		{
			// lane SELWALL-1: against the drawable's own height (an AIRBORNE_TARGET decal's position.z is its layer's, MarkRaisedSelectionDecals)
			const Coord3D &p = *draw->getPosition();
			if (p.x != d.position.x || p.y != d.position.y || p.z != d.drawZ)
			{
				changed = true;
				d.position = p;
				d.drawZ = p.z;
			}
		}
		if (!last || last->throbCycle != d.throbCycle || last->opacityMin != d.opacityMin || last->opacityMax != d.opacityMax)
		{
			opacity = throbOpacity(d, settings, clientFrame);
		}
		last = &d;
		const float angle = turnAngle(d, clientFrame);
		const std::uint32_t color = ((std::uint32_t)opacity << 24) | (d.color & 0x00FFFFFFu);
		changed = changed || angle != d.angle || color != d.color;
		d.angle = angle;
		d.color = color;
	}
	return changed;
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
// lane HERORING-1: the styles flushDecals draws with the blended alpha shader (RW 0x509A1F -> ShaderClass 0x1198B3) and setOpacity colours as an alpha
// decal (RW 0x73228B, mask 0x1420): SHADOW_ALPHA_DECAL 0x20 and SHADOW_ALPHA_DECAL_DYNAMIC 0x400. Not 0x2000 (SHADOW_DECAL_TERRAIN_CLAIM): RotWK moves it onto the claim grid
// (RW 0x50C48B) and colours its vertices from the TerrainResourceManager (RW 0x50D264)
constexpr std::uint32_t kAlphaStyles = 0x20u | 0x400u;

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
		if (!(d.style & kMergeStyle) || d.size <= 0.0f || d.raised) // lane SELWALL-1: the terrain leaves a raised decal out (the shader's sel_merge)
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

// ---- lane SELWALL-1: the decals above the terrain --------------------------------------------------------------------------------------------------------

bool MarkRaisedSelectionDecals(std::vector<SelectionDecal> &list, const std::function<float(float, float)> &floorHeight,
	const std::function<bool(ObjectID, const Coord3D &, float &)> &groundedHeight)
{
	bool changed = false;
	for (SelectionDecal &d : list)
	{
		float z = 0.0f;
		const Coord3D drawn{ d.position.x, d.position.y, d.drawZ };
		if (groundedHeight && groundedHeight(d.object, drawn, z) && z != d.position.z)
		{
			d.position.z = z; // RW 0x68DB35: the layer's height under an AIRBORNE_TARGET object
			changed = true;
		}
		// RW 0x50CDE4 .. 0x50CDF9 (= BFME2 0x50C841, decomp rva0010C841, W3DProjectedShadowDecalHelpers.cpp:1097-1098): the terrain part is drawn while
		// pos.z - floor < maxHeight
		const bool raised = !(d.position.z - floorHeight(d.position.x, d.position.y) < kSelectionDecalMaxHeight);
		changed = changed || raised != d.raised;
		d.raised = raised;
	}
	return changed;
}

bool MarkRaisedSelectionDecals(std::vector<SelectionDecal> &list, const GameLogic &logic)
{
	// RW 0x68DB35 (BFME2 0x68D4E0, ObjectVisualPositionBFME2.cpp): with status bit 6 (AIRBORNE_TARGET, RW 0xD8AFF0) the visual position's height is
	// getLayerHeight(x, y, getLayerForDestination(object, position)), RW 0x68DB8B .. 0x68DBE1
	static const int airborne = ObjectTemplateInfoBuilder::objectStatusIndex("AIRBORNE_TARGET");
	return MarkRaisedSelectionDecals(
		list, [&logic](float x, float y) { return logic.getGroundHeight(x, y); },
		[&logic](ObjectID id, const Coord3D &at, float &z) {
			const Object *o = airborne >= 0 ? logic.findObjectByID(id) : nullptr;
			if (!o || !o->testStatus((unsigned)airborne))
			{
				return false;
			}
			z = logic.getLayerHeightForDestination(at.x, at.y, at.z);
			return true;
		});
}

// ---- lane SELPERF-1: the merge decals for the terrain shader ---------------------------------------------------------------------------------------------

bool BuildMergeDecalGrid(const std::vector<SelectionDecal> &decals, const std::function<bool(const std::string &, MergeDecalAtlasRect &)> &rect,
	const SelectionDecalSettings &settings, MergeDecalGrid &out, std::string *error)
{
	out.simple = settings.useSimpleMergeDecals;
	out.alphaRef = settings.useSimpleMergeDecals ? std::max(0, std::min(255, (int)(settings.opacityOfSimpleMergeDecals * 255.0f))) : kPassAlphaRef;
	out.decals = 0;
	out.alphaDecals = 0;
	out.maxPerCell = 0;
	out.maxAlphaPerCell = 0;
	out.cellsX = out.cellsY = 0;
	struct Item
	{
		const SelectionDecal *d;
		MergeDecalAtlasRect tex, cut;
		bool alpha;
		float half; ///< the half extent binned (lane HERORING-1: an alpha decal's circumcircle when it is turned)
	};
	// the merge decals first (records 0 .. decals - 1, the layout SELPERF-1's tests read), then the alpha decals (lane HERORING-1)
	std::vector<Item> merge, alpha;
	// lane HERORING-1: an alpha decal that cannot be drawn is left out with its error in `error` (the grid is still built: a merge decal's missing texture
	// fails it, as before)
	if (error)
	{
		error->clear();
	}
	auto note = [&](const std::string &e) {
		if (error)
		{
			*error += (error->empty() ? "" : "; ") + e;
		}
	};
	merge.reserve(decals.size());
	for (const SelectionDecal &d : decals)
	{
		if (d.size <= 0.0f)
		{
			continue;
		}
		if (d.style & kMergeStyle)
		{
			Item it{ &d, {}, {}, false, d.size * 0.5f };
			if (!rect(d.texture, it.tex) || it.tex.w <= 0 || it.tex.h <= 0)
			{
				note("selection decal texture " + d.texture + " not found");
				return false;
			}
			if (!settings.useSimpleMergeDecals && (d.texture2.empty() || !rect(d.texture2, it.cut) || it.cut.w <= 0 || it.cut.h <= 0))
			{
				note("selection decal " + d.texture + ": its Texture2 '" + d.texture2 + "' (the stencil pass's cut-out) not found");
				return false;
			}
			merge.push_back(it);
		}
		else if (d.style & kAlphaStyles)
		{
			// lane HERORING-1: RotWK's flushDecals draws these with one blended shader (RW 0x509A1F: ShaderClass 0x1198B3, SRC_ALPHA / ONE_MINUS_SRC_ALPHA,
			// no alpha test); only Texture is drawn (Texture2 of a non-merge decal is not used)
			Item it{ &d, {}, {}, true, d.size * 0.5f * (d.angle != 0.0f ? 1.41421356f : 1.0f) };
			if (!rect(d.texture, it.tex) || it.tex.w <= 0 || it.tex.h <= 0)
			{
				note("selection decal texture " + d.texture + " not found (the decal is not drawn)");
				continue;
			}
			alpha.push_back(it);
		}
		else
		{
			// lane HERORING-1: the additive (0x40 / 0x800), multiplicative (0x01) and RotWK's 0x10000 decals blend otherwise (RW 0x509A1F), and
			// SHADOW_DECAL_TERRAIN_CLAIM 0x2000 is placed and coloured otherwise (RW 0x50C48B / 0x50D264); RotWK's
			// SelectionDecal blocks use only SHADOW_MERGE_DECAL and SHADOW_ALPHA_DECAL, so no other style is drawn
			char buf[32];
			std::snprintf(buf, sizeof(buf), "0x%X", (unsigned)d.style);
			note("selection decal " + d.texture + ": its style " + buf + " is neither SHADOW_MERGE_DECAL nor an alpha decal (not drawn)");
		}
	}
	std::vector<Item> items = merge;
	items.insert(items.end(), alpha.begin(), alpha.end());
	out.decals = merge.size();
	out.alphaDecals = alpha.size();
	if (items.empty())
	{
		out.texels.clear();
		out.rows = 0;
		return true;
	}
	float minX = 0.0f, minY = 0.0f, maxX = 0.0f, maxY = 0.0f;
	for (size_t i = 0; i < items.size(); ++i)
	{
		const SelectionDecal &d = *items[i].d;
		const float h = items[i].half;
		if (i == 0)
		{
			minX = d.position.x - h;
			maxX = d.position.x + h;
			minY = d.position.y - h;
			maxY = d.position.y + h;
		}
		minX = std::min(minX, d.position.x - h);
		maxX = std::max(maxX, d.position.x + h);
		minY = std::min(minY, d.position.y - h);
		maxY = std::max(maxY, d.position.y + h);
	}
	// cells as wide as the median decal (most decals touch four; the shader's bounds test costs one texel fetch per listed decal), at most
	// kMaxCellsPerAxis a side
	std::vector<float> sizes;
	sizes.reserve(items.size());
	for (const Item &it : items)
	{
		sizes.push_back(it.d->size);
	}
	std::nth_element(sizes.begin(), sizes.begin() + (std::ptrdiff_t)(sizes.size() / 2), sizes.end());
	const float span = std::max(maxX - minX, maxY - minY);
	out.minX = minX;
	out.minY = minY;
	out.cellSize = std::max(sizes[sizes.size() / 2], span / (float)MergeDecalGrid::kMaxCellsPerAxis);
	out.cellsX = std::max(1, std::min(MergeDecalGrid::kMaxCellsPerAxis, (int)std::ceil((maxX - minX) / out.cellSize)));
	out.cellsY = std::max(1, std::min(MergeDecalGrid::kMaxCellsPerAxis, (int)std::ceil((maxY - minY) / out.cellSize)));
	auto cellRange = [&](const Item &it, int &x0, int &x1, int &y0, int &y1) {
		const SelectionDecal &d = *it.d;
		const float h = it.half;
		x0 = std::max(0, std::min(out.cellsX - 1, (int)std::floor((d.position.x - h - minX) / out.cellSize)));
		x1 = std::max(0, std::min(out.cellsX - 1, (int)std::floor((d.position.x + h - minX) / out.cellSize)));
		y0 = std::max(0, std::min(out.cellsY - 1, (int)std::floor((d.position.y - h - minY) / out.cellSize)));
		y1 = std::max(0, std::min(out.cellsY - 1, (int)std::floor((d.position.y + h - minY) / out.cellSize)));
	};
	const size_t cells = (size_t)out.cellsX * (size_t)out.cellsY;
	// each cell's merge entries, then (after every merge entry of the grid) its alpha entries, each list in the decals' order
	std::vector<int> start(cells + 1, 0), astart(cells + 1, 0);
	for (const Item &it : items)
	{
		int x0, x1, y0, y1;
		cellRange(it, x0, x1, y0, y1);
		std::vector<int> &count = it.alpha ? astart : start;
		for (int cy = y0; cy <= y1; ++cy)
		{
			for (int cx = x0; cx <= x1; ++cx)
			{
				++count[(size_t)cy * (size_t)out.cellsX + (size_t)cx + 1];
			}
		}
	}
	for (size_t c = 0; c < cells; ++c)
	{
		out.maxPerCell = std::max(out.maxPerCell, start[c + 1]);
		out.maxAlphaPerCell = std::max(out.maxAlphaPerCell, astart[c + 1]);
		start[c + 1] += start[c];
		astart[c + 1] += astart[c];
	}
	const size_t mergeEntries = (size_t)start[cells];
	const size_t entries = mergeEntries + (size_t)astart[cells];
	out.cellOffset = (int)(items.size() * 4);
	out.indexOffset = out.cellOffset + (int)cells;
	const size_t texels = (size_t)out.indexOffset + entries;
	out.rows = (int)((texels + MergeDecalGrid::kWidth - 1) / MergeDecalGrid::kWidth);
	// every texel the shader reads is written below; the last row's tail is never read (the buffer keeps its capacity from frame to frame)
	out.texels.resize((size_t)out.rows * MergeDecalGrid::kWidth * 4);
	float *t = out.texels.data();
	for (size_t i = 0; i < items.size(); ++i)
	{
		const SelectionDecal &d = *items[i].d;
		float *p = t + i * 16;
		p[0] = d.position.x;
		p[1] = d.position.y;
		p[2] = d.size;
		p[3] = (float)(d.color >> 24);
		p[4] = (float)((d.color >> 16) & 0xFFu);
		p[5] = (float)((d.color >> 8) & 0xFFu);
		p[6] = (float)(d.color & 0xFFu);
		p[7] = d.position.z; // lane SELWALL-1: a wall surface's height window (sel_merge_at)
		const MergeDecalAtlasRect &a = items[i].tex, &b = items[i].cut;
		p[8] = (float)a.x;
		p[9] = (float)a.y;
		p[10] = (float)a.w;
		p[11] = (float)a.h;
		if (items[i].alpha)
		{
			// lane HERORING-1: an alpha decal's turn as its cosine and sine (sel_alpha_at); no Texture2
			p[12] = std::cos(d.angle);
			p[13] = std::sin(d.angle);
			p[14] = p[15] = 0.0f;
		}
		else
		{
			p[12] = (float)b.x;
			p[13] = (float)b.y;
			p[14] = (float)b.w;
			p[15] = (float)b.h;
		}
	}
	for (size_t c = 0; c < cells; ++c)
	{
		float *p = t + ((size_t)out.cellOffset + c) * 4;
		p[0] = (float)start[c];
		p[1] = (float)(start[c + 1] - start[c]);
		p[2] = (float)(mergeEntries + (size_t)astart[c]); // lane HERORING-1: the cell's alpha entries
		p[3] = (float)(astart[c + 1] - astart[c]);
	}
	std::vector<int> fill(start.begin(), start.end() - 1), afill(astart.begin(), astart.end() - 1);
	for (size_t i = 0; i < items.size(); ++i)
	{
		int x0, x1, y0, y1;
		cellRange(items[i], x0, x1, y0, y1);
		for (int cy = y0; cy <= y1; ++cy)
		{
			for (int cx = x0; cx <= x1; ++cx)
			{
				const size_t c = (size_t)cy * (size_t)out.cellsX + (size_t)cx;
				const size_t at = items[i].alpha ? mergeEntries + (size_t)afill[c]++ : (size_t)fill[c]++;
				float *e = t + ((size_t)out.indexOffset + at) * 4;
				e[0] = items[i].d->position.x;
				e[1] = items[i].d->position.y;
				e[2] = items[i].d->size;
				e[3] = items[i].d->raised ? -(float)(i + 1) : (float)i; // lane SELWALL-1: the terrain skips a raised decal
			}
		}
	}
	return true;
}

namespace
{
// selection_decals.gdshaderinc sel_bilinear: the atlas rect's texel at (u, v) in [0, 1], v = 0 the top row, clamped to the rect (as sample() above)
void atlasSample(const DecalImage &atlas, const float r[4], float u, float v, float out[4])
{
	const float x = u * r[2] - 0.5f, y = v * r[3] - 0.5f;
	const float fx0 = std::floor(x), fy0 = std::floor(y);
	const float fx = x - fx0, fy = y - fy0;
	const int lox = (int)r[0], loy = (int)r[1], hix = lox + (int)r[2] - 1, hiy = loy + (int)r[3] - 1;
	const int ax = std::max(lox, std::min(hix, lox + (int)fx0)), ay = std::max(loy, std::min(hiy, loy + (int)fy0));
	const int bx = std::max(lox, std::min(hix, lox + (int)fx0 + 1)), by = std::max(loy, std::min(hiy, loy + (int)fy0 + 1));
	auto texel = [&](int tx, int ty, int c) { return (float)atlas.rgba[((size_t)ty * (size_t)atlas.width + (size_t)tx) * 4 + (size_t)c] / 255.0f; };
	for (int c = 0; c < 4; ++c)
	{
		const float top = texel(ax, ay, c) * (1.0f - fx) + texel(bx, ay, c) * fx;
		const float bottom = texel(ax, by, c) * (1.0f - fx) + texel(bx, by, c) * fx;
		out[c] = top * (1.0f - fy) + bottom * fy;
	}
}
} // namespace

bool EvaluateMergeDecalGrid(const MergeDecalGrid &grid, const DecalImage &atlas, float x, float y, std::uint8_t rgba[4])
{
	return EvaluateMergeDecalGrid(grid, atlas, x, y, 0.0f, DecalReceiver::Terrain, rgba);
}

bool EvaluateMergeDecalGrid(const MergeDecalGrid &grid, const DecalImage &atlas, float x, float y, float z, DecalReceiver receiver, std::uint8_t rgba[4])
{
	if (grid.decals == 0 || !atlas.rgba)
	{
		return false;
	}
	const float gx = (x - grid.minX) / grid.cellSize, gy = (y - grid.minY) / grid.cellSize;
	if (gx < 0.0f || gy < 0.0f || gx >= (float)grid.cellsX || gy >= (float)grid.cellsY)
	{
		return false;
	}
	const float *t = grid.texels.data();
	const float *cell = t + ((size_t)grid.cellOffset + (size_t)((int)gy * grid.cellsX + (int)gx)) * 4;
	const int first = (int)cell[0], count = (int)cell[1];
	bool covered = false;
	bool drawn = false;
	float best[4] = { 0, 0, 0, 0 };
	// the two passes walk the list from its end: every cut-out is tested (any one covers the point: nothing is drawn), a disc only until one passes
	// (pass 1's last disc drawn wins); the simple pass walks it from the start (its first disc that passes wins)
	for (int i = 0; i < count; ++i)
	{
		const int k = grid.simple ? i : count - 1 - i;
		const float *e = t + ((size_t)grid.indexOffset + (size_t)(first + k)) * 4;
		const float h = e[2] * 0.5f;
		const float u = (x - (e[0] - h)) / e[2], v = ((e[1] + h) - y) / e[2];
		if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f)
		{
			continue;
		}
		// lane SELWALL-1: the terrain leaves a raised decal out; a wall surface takes a decal only within the height window of its point
		if (receiver == DecalReceiver::Terrain && e[3] < 0.0f)
		{
			continue;
		}
		const float *d = t + (size_t)(e[3] < 0.0f ? -e[3] - 1.0f : e[3]) * 16;
		if (receiver == DecalReceiver::Wall && !(std::fabs(z - d[7]) <= kSelectionDecalMaxHeight))
		{
			continue;
		}
		float s[4];
		if (!grid.simple)
		{
			atlasSample(atlas, d + 12, u, v, s);
			if (std::floor(s[3] * d[3] + 0.5f) >= (float)grid.alphaRef)
			{
				covered = true; // pass 0: a cut-out in the stencil here, so pass 1 draws nothing here whatever the other decals are
				break;
			}
		}
		if (drawn)
		{
			continue; // the last disc that passes is known: only the remaining cut-outs matter
		}
		atlasSample(atlas, d + 8, u, v, s);
		const float a = std::floor(s[3] * d[3] + 0.5f);
		if (a >= (float)grid.alphaRef)
		{
			best[0] = s[0] * d[4];
			best[1] = s[1] * d[5];
			best[2] = s[2] * d[6];
			best[3] = grid.simple ? a : 255.0f;
			drawn = true;
			if (grid.simple)
			{
				break;
			}
		}
	}
	if (!drawn || covered)
	{
		return false;
	}
	for (int c = 0; c < 4; ++c)
	{
		rgba[c] = (std::uint8_t)std::max(0.0f, std::min(255.0f, std::floor(best[c] + 0.5f)));
	}
	return true;
}

bool BuildSelectionDecalGrid(const std::vector<SelectionDecal> &decals, const std::function<bool(const std::string &, MergeDecalAtlasRect &)> &rect,
	const SelectionDecalSettings &settings, MergeDecalGrid &out, std::string *error)
{
	if (BuildMergeDecalGrid(decals, rect, settings, out, error))
	{
		return true;
	}
	std::vector<SelectionDecal> others;
	for (const SelectionDecal &d : decals)
	{
		if (!(d.style & kMergeStyle))
		{
			others.push_back(d);
		}
	}
	std::string notes; // the other decals' notes are already in `error` from the first build
	if (!others.empty() && BuildMergeDecalGrid(others, rect, settings, out, &notes))
	{
		return true;
	}
	out = MergeDecalGrid();
	return false;
}

int SelectionDecalShaderMode(const MergeDecalGrid &grid)
{
	return (grid.decals == 0 ? 0 : (grid.simple ? 2 : 1)) + (grid.alphaDecals > 0 ? 4 : 0);
}

bool EvaluateSelectionDecals(const MergeDecalGrid &grid, const DecalImage &atlas, float x, float y, float z, DecalReceiver receiver,
	const std::uint8_t surface[3], std::uint8_t out[3], const float *shadow)
{
	float c[3] = { (float)surface[0], (float)surface[1], (float)surface[2] };
	for (int ch = 0; shadow && ch < 3; ++ch)
	{
		c[ch] *= shadow[ch]; // the shadowed surface the decals are drawn over
	}
	bool drawn = false;
	// lane HERORING-1: the alpha decals in their list order, each blended over the surface (RW 0x509A1F's ShaderClass 0x1198B3: SRC_ALPHA /
	// ONE_MINUS_SRC_ALPHA, texture x diffuse, MODULATE at RW 0x5373FC; the diffuse the colour with the opacity as alpha, setOpacity RW 0x73228B)
	if (grid.alphaDecals > 0 && atlas.rgba)
	{
		const float gx = (x - grid.minX) / grid.cellSize, gy = (y - grid.minY) / grid.cellSize;
		if (gx >= 0.0f && gy >= 0.0f && gx < (float)grid.cellsX && gy < (float)grid.cellsY)
		{
			const float *t = grid.texels.data();
			const float *cell = t + ((size_t)grid.cellOffset + (size_t)((int)gy * grid.cellsX + (int)gx)) * 4;
			const int first = (int)cell[2], count = (int)cell[3];
			for (int k = 0; k < count; ++k)
			{
				const float *e = t + ((size_t)grid.indexOffset + (size_t)(first + k)) * 4;
				const float dx = x - e[0], dy = y - e[1];
				if (dx * dx + dy * dy > 0.5f * e[2] * e[2])
				{
					continue; // outside the turned square's circumcircle
				}
				if (receiver == DecalReceiver::Terrain && e[3] < 0.0f)
				{
					continue;
				}
				const float *d = t + (size_t)(e[3] < 0.0f ? -e[3] - 1.0f : e[3]) * 16;
				if (receiver == DecalReceiver::Wall && !(std::fabs(z - d[7]) <= kSelectionDecalMaxHeight))
				{
					continue;
				}
				// the top-down projection (BFME2 decomp W3DProjectedShadowDecalHelpers.cpp): u along the turned x axis, v toward -y at angle 0
				const float co = d[12], si = d[13];
				const float u = (dx * co + dy * si) / e[2] + 0.5f, v = (dx * si - dy * co) / e[2] + 0.5f;
				if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f)
				{
					continue;
				}
				float s[4];
				atlasSample(atlas, d + 8, u, v, s);
				const float a = s[3] * d[3] / 255.0f;
				for (int ch = 0; ch < 3; ++ch)
				{
					c[ch] = c[ch] * (1.0f - a) + s[ch] * d[4 + ch] * a;
				}
				drawn = drawn || a > 0.0f;
			}
		}
	}
	// the merge list after the alpha list (renderShadows: RW 0x50DE6C after the alpha list's flushes, RW 0x50D9EB .. 0x50DA6A)
	std::uint8_t m[4] = { 0, 0, 0, 0 };
	if (EvaluateMergeDecalGrid(grid, atlas, x, y, z, receiver, m))
	{
		const float ma = (float)m[3] / 255.0f;
		for (int ch = 0; ch < 3; ++ch)
		{
			c[ch] = c[ch] * (1.0f - ma) + (float)m[ch] * ma;
		}
		drawn = true;
	}
	for (int ch = 0; ch < 3; ++ch)
	{
		out[ch] = (std::uint8_t)std::max(0.0f, std::min(255.0f, std::floor(c[ch] + 0.5f)));
	}
	return drawn;
}

// ---- lane SELPERF-1: the merge decals' atlas ------------------------------------------------------------------------------------------------------------

bool MergeDecalAtlas::rect(const std::string &name, MergeDecalAtlasRect &out) const
{
	for (size_t i = 0; i < m_names.size(); ++i)
	{
		if (m_names[i] == name)
		{
			out = m_rects[i];
			return true;
		}
	}
	return false;
}

void MergeDecalAtlas::clear()
{
	m_names.clear();
	m_rects.clear();
	m_rejected.clear();
	m_pixels.clear();
	m_height = 0;
}

bool MergeDecalAtlas::update(const std::vector<SelectionDecal> &decals, const SelectionDecalSettings &settings,
	const std::function<DecalImage(const std::string &)> &image, std::vector<std::string> &errors)
{
	// the merge decals' textures (Texture2 only for the two passes) and, lane HERORING-1, the alpha decals' Texture, not in the atlas yet
	std::vector<std::string> names = m_names;
	auto known = [&](const std::string &n) {
		return std::find(names.begin(), names.end(), n) != names.end() || std::find(m_rejected.begin(), m_rejected.end(), n) != m_rejected.end();
	};
	for (const SelectionDecal &d : decals)
	{
		const bool merge = (d.style & kMergeStyle) != 0;
		if (!merge && !(d.style & kAlphaStyles))
		{
			continue;
		}
		for (const std::string *name : { &d.texture, &d.texture2 })
		{
			if ((name == &d.texture2 && (settings.useSimpleMergeDecals || !merge)) || name->empty() || known(*name))
			{
				continue;
			}
			const DecalImage img = image(*name);
			if (img.width <= 0 || img.height <= 0 || !img.rgba)
			{
				continue; // missing: not in the atlas (BuildMergeDecalGrid reports it)
			}
			if (img.width > kWidth)
			{
				errors.push_back("selection decal texture " + *name + " is wider than the atlas (" + std::to_string(img.width) + " texels)");
				m_rejected.push_back(*name);
				continue;
			}
			names.push_back(*name);
		}
	}
	if (names.size() == m_names.size())
	{
		return false;
	}
	// pack everything again in local copies, then commit
	std::vector<MergeDecalAtlasRect> rects;
	std::vector<DecalImage> images;
	int x = 0, y = 0, rowH = 0;
	for (const std::string &name : names)
	{
		const DecalImage img = image(name);
		if (img.width <= 0 || img.height <= 0 || !img.rgba)
		{
			errors.push_back("selection decal texture " + name + " went missing while the atlas was packed");
			return false; // the atlas stays as it was
		}
		if (x + img.width > kWidth)
		{
			x = 0;
			y += rowH;
			rowH = 0;
		}
		rects.push_back(MergeDecalAtlasRect{ x, y, img.width, img.height });
		images.push_back(img);
		x += img.width;
		rowH = std::max(rowH, img.height);
	}
	const int height = std::max(1, y + rowH);
	std::vector<std::uint8_t> px((size_t)kWidth * (size_t)height * 4, 0);
	for (size_t i = 0; i < rects.size(); ++i)
	{
		const MergeDecalAtlasRect &r = rects[i];
		for (int row = 0; row < r.h; ++row)
		{
			std::copy_n(images[i].rgba + (size_t)row * (size_t)r.w * 4, (size_t)r.w * 4, px.begin() + (std::ptrdiff_t)(((size_t)(r.y + row) * kWidth + (size_t)r.x) * 4));
		}
	}
	m_names.swap(names);
	m_rects.swap(rects);
	m_pixels.swap(px);
	m_height = height;
	return true;
}
