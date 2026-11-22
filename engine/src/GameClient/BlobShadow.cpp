// OpenBFME. GPL-3.0.
// See GameClient/BlobShadow.h.

#include "GameClient/BlobShadow.h"

#include "Common/Thing/ThingTemplate.h"
#include "GameClient/SelectionDecals.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DModelDraw.h"

#include <cmath>
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
} // namespace BlobShadow
