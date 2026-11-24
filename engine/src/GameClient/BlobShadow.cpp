// OpenBFME. GPL-3.0.
// See GameClient/BlobShadow.h.

#include "GameClient/BlobShadow.h"
#include "GameClient/DecalShadowFades.h"
#include "GameClient/DrawablePick.h"

#include "Common/Thing/ThingTemplate.h"
#include "GameClient/SelectionDecals.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DModelDraw.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <variant>

namespace BlobShadow
{
namespace
{
constexpr int kDecal = 0x1;
constexpr int kVolumeOrDecal = 0x4000 | 0x8000;

float numberField(const ThingTemplate &t, const char *name, float fallback)
{
	const FieldValue *v = t.findField(name);
	if (!v)
	{
		return fallback;
	}
	if (const float *f = std::get_if<float>(v))
	{
		return *f;
	}
	if (const long long *i = std::get_if<long long>(v))
	{
		return (float)*i;
	}
	return fallback;
}

bool boolField(const ThingTemplate &t, const char *name, bool fallback)
{
	const FieldValue *v = t.findField(name);
	const bool *b = v ? std::get_if<bool>(v) : nullptr;
	return b ? *b : fallback;
}

std::string textureFile(const std::string &name)
{
	// RW 0x50BFA6: an empty ShadowTexture (length <= 1) is "shadow"; RW 0x50B690 appends ".tga"
	const std::string base = name.size() <= 1 ? std::string("shadow") : name;
	return base.find('.') == std::string::npos ? base + ".tga" : base;
}

bool isDecal(int type, bool useShadowVolumes)
{
	return (type & kDecal) != 0 || (!useShadowVolumes && (type & kVolumeOrDecal) != 0);
}
} // namespace

bool resolve(const ThingTemplate *object, const W3DModelDrawModuleData *module, const ModelConditionInfo *state, bool useShadowVolumes, Spec &out)
{
	if (module && module->m_shadowForceDisable)
	{
		return false;
	}
	out = Spec();
	if (object)
	{
		// the template's fields (ThingTemplate table RW 0xDA44E8; the defaults of its constructor RW 0x740102 .. 0x740172)
		out.maxHeight = numberField(*object, "ShadowMaxHeight", 20.0f);
		out.rotating = boolField(*object, "ShadowIsRotatingWithObject", true);
		out.overrideLOD = boolField(*object, "ShadowOverrideLODVisibility", false);
	}
	if (state && state->hasShadow)
	{
		const ModelShadowInfo &s = state->shadow;
		if (!isDecal(s.type, useShadowVolumes))
		{
			return false;
		}
		out.texture = textureFile(s.texture);
		out.sizeX = s.sizeX;
		out.sizeY = s.sizeY;
		out.offsetX = s.offsetX;
		out.offsetY = s.offsetY;
		if (s.maxHeight > 0.0f)
		{
			out.maxHeight = s.maxHeight;
		}
		out.overrideLOD = out.overrideLOD || s.overrideLODVisibility;
		return true;
	}
	if (!object)
	{
		return false;
	}
	const FieldValue *field = object->findField("Shadow");
	const long long *bits = field ? std::get_if<long long>(field) : nullptr;
	if (!bits || !isDecal((int)*bits, useShadowVolumes))
	{
		return false;
	}
	const FieldValue *tex = object->findField("ShadowTexture");
	const std::string *texName = tex ? std::get_if<std::string>(tex) : nullptr;
	out.texture = textureFile(texName ? *texName : std::string());
	out.sizeX = numberField(*object, "ShadowSizeX", 0.0f);
	out.sizeY = numberField(*object, "ShadowSizeY", 0.0f);
	out.offsetX = numberField(*object, "ShadowOffsetX", 0.0f);
	out.offsetY = numberField(*object, "ShadowOffsetY", 0.0f);
	return true;
}

bool modelExtent(const RenderObjPrototype *model, float &extentX, float &extentY)
{
	extentX = extentY = 0.0f;
	float lo[3], hi[3];
	if (!model || !DrawablePick::bindPoseBounds(*model, lo, hi))
	{
		return false;
	}
	extentX = 0.5f * (hi[0] - lo[0]);
	extentY = 0.5f * (hi[1] - lo[1]);
	return true;
}

bool quad(const Spec &spec, float x, float y, float angle, float extentX, float extentY, Quad &out)
{
	// RW 0x50BFE8 .. 0x50C06F: a zero size takes twice the local box extent (Y negated: the v axis mirrored)
	const float sizeX = spec.sizeX != 0.0f ? spec.sizeX : 2.0f * extentX;
	const float sizeY = spec.sizeY != 0.0f ? spec.sizeY : -2.0f * extentY;
	if (sizeX == 0.0f || sizeY == 0.0f)
	{
		return false; // BFME2 rva0010C841 draws nothing
	}
	const float a = spec.rotating ? angle : 0.0f;
	const float co = std::cos(a), si = std::sin(a);
	const Coord3D c = DecalShadowCentre(Coord3D{ x, y, 0.0f }, spec.offsetX, spec.offsetY, a);
	out.cx = c.x;
	out.cy = c.y;
	out.ux = 0.5f * sizeX * co;
	out.uy = 0.5f * sizeX * si;
	out.vx = 0.5f * sizeY * si;
	out.vy = -0.5f * sizeY * co;
	return true;
}

bool resolveDecal(const ThingTemplate *object, const W3DModelDrawModuleData *module, const ModelConditionInfo *state, DecalSpec &out)
{
	if (module && module->m_shadowForceDisable)
	{
		return false;
	}
	out = DecalSpec();
	if (object)
	{
		// the template's fields, as resolve's (ThingTemplate table RW 0xDA44E8; the defaults of its constructor RW 0x740102 .. 0x740172)
		out.maxHeight = numberField(*object, "ShadowMaxHeight", 20.0f);
		out.rotating = boolField(*object, "ShadowIsRotatingWithObject", true);
		out.overrideLOD = boolField(*object, "ShadowOverrideLODVisibility", false);
	}
	if (state && state->hasShadow)
	{
		const ModelShadowInfo &s = state->shadow;
		if ((s.type & kDecalStyles) == 0)
		{
			return false;
		}
		out.type = s.type & kDecalStyles;
		out.texture = textureFile(s.texture);
		out.sizeX = s.sizeX;
		out.sizeY = s.sizeY;
		out.offsetX = s.offsetX;
		out.offsetY = s.offsetY;
		if (s.maxHeight > 0.0f)
		{
			out.maxHeight = s.maxHeight;
		}
		out.overrideLOD = out.overrideLOD || s.overrideLODVisibility;
		out.opacityPeak = state->shadowOpacityPeak;
		out.opacityStart = state->shadowOpacityStart;
		out.opacityEnd = state->shadowOpacityEnd;
		out.fadeInMs = state->shadowOpacityFadeInTime;
		out.fadeOutMs = state->shadowOpacityFadeOutTime;
		return true;
	}
	if (!object)
	{
		return false;
	}
	const FieldValue *field = object->findField("Shadow");
	const long long *bits = field ? std::get_if<long long>(field) : nullptr;
	if (!bits || ((int)*bits & kDecalStyles) == 0)
	{
		return false;
	}
	out.type = (int)*bits & kDecalStyles;
	const FieldValue *tex = object->findField("ShadowTexture");
	const std::string *texName = tex ? std::get_if<std::string>(tex) : nullptr;
	out.texture = textureFile(texName ? *texName : std::string());
	out.sizeX = numberField(*object, "ShadowSizeX", 0.0f);
	out.sizeY = numberField(*object, "ShadowSizeY", 0.0f);
	out.offsetX = numberField(*object, "ShadowOffsetX", 0.0f);
	out.offsetY = numberField(*object, "ShadowOffsetY", 0.0f);
	out.opacityPeak = (unsigned)numberField(*object, "ShadowOpacityPeak", 255.0f);
	out.opacityStart = (unsigned)numberField(*object, "ShadowOpacityStart", 0.0f);
	out.opacityEnd = (unsigned)numberField(*object, "ShadowOpacityEnd", 0.0f);
	out.fadeInMs = numberField(*object, "ShadowOpacityFadeInTime", 0.0f);
	out.fadeOutMs = numberField(*object, "ShadowOpacityFadeOutTime", 0.0f);
	return true;
}

bool decal(const DecalSpec &spec, float x, float y, float z, float angle, SelectionDecal &out)
{
	if (spec.sizeX == 0.0f || spec.sizeY == 0.0f || spec.type == 0)
	{
		return false;
	}
	out = SelectionDecal();
	out.texture = spec.texture;
	out.style = (std::uint32_t)spec.type;
	out.color = DecalShadowColor(spec.opacityPeak);
	out.size = spec.sizeX;
	out.sizeY = spec.sizeY;
	out.angle = angle;
	out.position = DecalShadowCentre(Coord3D{ x, y, z }, spec.offsetX, spec.offsetY, angle);
	out.drawZ = z;
	return true;
}

bool isDynamic(int type)
{
	// RW 0x4B1BC6: the shadow type is exactly one of the three
	return type == 0x400 || type == 0x800 || type == 0x10000;
}

namespace
{
unsigned msToFrames(float ms)
{
	// RW 0x4C4F74 .. 0x4C4F89: ms * 0.03 (RW 0xD9F624), _ftol (truncated)
	const float frames = ms * 0.03f;
	return frames > 0.0f ? (unsigned)frames : 0u;
}

unsigned ramp(unsigned from, unsigned to, unsigned now, unsigned a, unsigned b)
{
	// RW 0x50B90E .. 0x50B972: from + (now - a) / (b - a) * (to - from), in doubles, rounded up (RW 0xBD0588), stored as an integer
	const double f = (double)(now - a) / (double)(b - a);
	return (unsigned)std::ceil((double)from + f * ((double)to - (double)from));
}
} // namespace

DecalFade fadeIn(const DecalSpec &spec, unsigned now)
{
	DecalFade f;
	f.t0 = now;
	f.t1 = now + msToFrames(spec.fadeInMs);
	f.start = spec.opacityStart;
	f.peak = spec.opacityPeak;
	f.endOpacity = spec.opacityEnd;
	f.fadeOutFrames = msToFrames(spec.fadeOutMs);
	return f;
}

DecalFade fadeOut(const DecalFade &fade, unsigned now)
{
	DecalFade f = fade;
	f.t0 = now;
	f.t1 = now;
	f.hold = now;
	f.end = now + fade.fadeOutFrames;
	f.life = now + fade.fadeOutFrames;
	return f;
}

bool opacityAt(const DecalFade &f, unsigned now, unsigned &opacity)
{
	if (f.life <= now)
	{
		return false;
	}
	if (now < f.t0)
	{
		opacity = 0;
	}
	else if (now < f.t1)
	{
		opacity = ramp(f.start, f.peak, now, f.t0, f.t1);
	}
	else if (now < f.hold)
	{
		opacity = f.peak;
	}
	else if (now < f.end)
	{
		opacity = ramp(f.peak, f.endOpacity, now, f.hold, f.end);
	}
	else
	{
		opacity = f.endOpacity;
	}
	return true;
}

void DecalFades::clear()
{
	m_views.clear();
	m_going.clear();
	m_now = 0;
}

void DecalFades::begin(unsigned now, bool useShadowDecals)
{
	m_now = now;
	for (auto it = m_views.begin(); it != m_views.end();)
	{
		it->second.kept = false;
		it = drawnAtLOD(it->second.spec, useShadowDecals) ? std::next(it) : m_views.erase(it);
	}
	m_going.erase(std::remove_if(m_going.begin(), m_going.end(), [&](const Going &g) { return !drawnAtLOD(g.spec, useShadowDecals); }), m_going.end());
}

void DecalFades::hide(std::uint32_t drawable)
{
	const auto it = m_views.find(drawable);
	if (it != m_views.end())
	{
		it->second.hasLast = false;
	}
}

void DecalFades::hide(std::uint32_t drawable, const void *draw)
{
	const auto it = m_views.find(drawable);
	if (it != m_views.end() && it->second.draw == draw)
	{
		it->second.hasLast = false;
	}
}

void DecalFades::release(const View &v)
{
	// RW 0x508B40: a _DYNAMIC decal fades out where it was last drawn; one that was not seen, or another type, goes at once
	if (v.hasLast && isDynamic(v.spec.type))
	{
		m_going.push_back({ v.last, fadeOut(v.fade, m_now), v.spec });
	}
}

bool DecalFades::keep(std::uint32_t drawable, ObjectID object, const void *draw, const DecalSpec &spec, unsigned &opacity)
{
	const auto found = m_views.find(drawable);
	const bool same = found != m_views.end() && found->second.object == object && found->second.spec == spec;
	if (found != m_views.end() && !same)
	{
		release(found->second);
	}
	View &v = m_views[drawable];
	if (!same)
	{
		v = View();
		v.object = object;
		v.spec = spec;
		v.fade = fadeIn(spec, m_now);
	}
	v.draw = draw;
	v.kept = true;
	v.hasLast = false; // until drawn()
	opacity = spec.opacityPeak;
	return !isDynamic(spec.type) || opacityAt(v.fade, m_now, opacity);
}

void DecalFades::drawn(std::uint32_t drawable, const SelectionDecal &decal)
{
	const auto it = m_views.find(drawable);
	if (it != m_views.end())
	{
		it->second.last = decal;
		it->second.hasLast = true;
	}
}

void DecalFades::end(std::vector<SelectionDecal> &out)
{
	for (auto it = m_views.begin(); it != m_views.end();)
	{
		if (it->second.kept)
		{
			++it;
			continue;
		}
		release(it->second);
		it = m_views.erase(it);
	}
	for (auto it = m_going.begin(); it != m_going.end();)
	{
		unsigned opacity = 0;
		if (!opacityAt(it->fade, m_now, opacity))
		{
			it = m_going.erase(it);
			continue;
		}
		out.push_back(it->decal);
		out.back().color = DecalShadowColor(opacity);
		++it;
	}
}
} // namespace BlobShadow
