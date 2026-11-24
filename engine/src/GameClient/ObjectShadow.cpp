// OpenBFME. GPL-3.0.
// Object shadow resolution for shadow maps and decal shadows.
//
// TARGET FACTS (RotWK game.dat):
//   RW 0x4B931E: W3DModelDraw::allocateShadows, called on state switches across 7 model draw classes
//     (W3DScriptedModelDraw vtable 0xBE1420, W3DHordeModelDraw 0xBE0FB0, W3DDefaultModelDraw 0xBE0BC8,
//     W3DTreeDraw 0xBE1698, W3DDebrisModelDraw 0xBE0DA0, W3DPropModelDraw 0xBE1508, W3DFloorDraw 0xBE0EF8).
//   RW 0x4C4EE0..0x4C5175: W3DScriptedModelDraw::setModelState and RW 0x47A0AD W3DHordeModelDraw::setModelState:
//     Branch A (RW 0x4C4EE0): condition state shadow (hasShadow, type != 0) takes precedence; sets isRotatingWithObject = true (RW 0x4C4F50).
//     Branch B (RW 0x4C4FD6): ThingTemplate shadow (+0x5F2 != 0).
//     ShadowForceDisable (RW 0x4C4EF8 / 0x4C5013): when true, no shadow is created.
//   RW 0x50BF6A: W3DProjectedShadowManager::addShadow:
//     RW 0x50BF93: texture name defaults to "shadow" if absent or length <= 1.
//     RW 0x50BFF3..0x50C066: size fallback from model bounding box if sizeX or sizeY == 0.
//     RW 0x50C07B: center = DecalShadowCentre(drawablePos, offsetX, offsetY, angle).
//   RW 0x672D28: Drawable::shadowsEnabled (false when dead with SlowDeathBehavior/ShadowWhenDead = No).
//   RW 0x4C50D2, RW 0x73232B: ShadowUseHouseColor calls Shadow::setColor with player color.
//   RW 0x740108..0x740175: ThingTemplate constructor field defaults (+0x4F8..+0x514, +0x5BC..+0x5C4, +0x5FD..+0x5FF).

#include "GameClient/ObjectShadow.h"

#include "Common/Thing/ThingTemplate.h"
#include "GameClient/Drawable.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DModelDraw.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"

#include <algorithm>
#include <cmath>

namespace ObjectShadow
{

namespace
{
float readFloat(const ThingTemplate *tt, const char *name, float defVal)
{
	if (!tt) return defVal;
	const FieldValue *v = tt->findField(name);
	if (!v) return defVal;
	if (const float *f = std::get_if<float>(v)) return *f;
	if (const long long *i = std::get_if<long long>(v)) return (float)*i;
	return defVal;
}

unsigned readUnsigned(const ThingTemplate *tt, const char *name, unsigned defVal)
{
	if (!tt) return defVal;
	const FieldValue *v = tt->findField(name);
	if (!v) return defVal;
	if (const long long *i = std::get_if<long long>(v)) return (unsigned)*i;
	if (const float *f = std::get_if<float>(v)) return (unsigned)*f;
	return defVal;
}

bool readBool(const ThingTemplate *tt, const char *name, bool defVal)
{
	if (!tt) return defVal;
	const FieldValue *v = tt->findField(name);
	if (!v) return defVal;
	if (const bool *b = std::get_if<bool>(v)) return *b;
	if (const long long *i = std::get_if<long long>(v)) return *i != 0;
	return defVal;
}

std::string readString(const ThingTemplate *tt, const char *name, const std::string &defVal)
{
	if (!tt) return defVal;
	const FieldValue *v = tt->findField(name);
	if (!v) return defVal;
	if (const std::string *s = std::get_if<std::string>(v)) return *s;
	return defVal;
}
} // namespace

bool getModelBoundingBox(const RenderObjPrototype *proto, float &extentX, float &extentY)
{
	if (!proto || proto->SubObjects.empty())
	{
		return false;
	}

	const HTreeClass *tree = proto->Tree ? proto->Tree : &proto->DefaultTree;
	HTreePose pose;
	tree->Base_Pose(Matrix3D(), pose);
	const int pivots = (int)pose.Transform.size();
	auto pivot = [&](int i) -> const Matrix3D & {
		static const Matrix3D identity;
		return i >= 0 && i < pivots ? pose.Transform[(size_t)i] : identity;
	};

	float minX = 1e30f, maxX = -1e30f;
	float minY = 1e30f, maxY = -1e30f;
	bool found = false;

	for (const RenderSubObject &sub : proto->SubObjects)
	{
		if (sub.Type != RenderSubObject::SUB_MESH || !sub.Mesh || sub.Mesh->Is_Hidden())
		{
			continue;
		}
		const MeshModelClass &mesh = *sub.Mesh;
		const Matrix3D &piv = pivot(sub.BoneIndex);
		if (!mesh.Vertices.empty())
		{
			for (const W3dVectorStruct &p : mesh.Vertices)
			{
				float w[3];
				for (int r = 0; r < 3; ++r)
				{
					w[r] = piv.Row[r][0] * p.X + piv.Row[r][1] * p.Y + piv.Row[r][2] * p.Z + piv.Row[r][3];
				}
				minX = std::min(minX, w[0]);
				maxX = std::max(maxX, w[0]);
				minY = std::min(minY, w[1]);
				maxY = std::max(maxY, w[1]);
				found = true;
			}
		}
		else
		{
			minX = std::min(minX, mesh.Header.Min.X);
			maxX = std::max(maxX, mesh.Header.Max.X);
			minY = std::min(minY, mesh.Header.Min.Y);
			maxY = std::max(maxY, mesh.Header.Max.Y);
			found = true;
		}
	}

	if (!found)
	{
		for (const RenderSubObject &sub : proto->SubObjects)
		{
			if (sub.Type == RenderSubObject::SUB_BOX && sub.Box && !sub.Aggregate && (sub.Box->Attributes & 1u) != 0)
			{
				extentX = sub.Box->Extent.X;
				extentY = sub.Box->Extent.Y;
				return true;
			}
		}
		return false;
	}

	if (minX > maxX || minY > maxY)
	{
		return false;
	}

	extentX = (maxX - minX) * 0.5f;
	extentY = (maxY - minY) * 0.5f;
	return true;
}

bool resolveDecalShadow(const ThingTemplate *tt, const W3DModelDrawModuleData *module, const ModelConditionInfo *state, DecalShadowDescriptor &out)
{
	out = DecalShadowDescriptor();
	// RW 0x4C4EF8 / RW 0x4C5013: ShadowForceDisable skips shadow creation
	if (module && module->m_shadowForceDisable)
	{
		return false;
	}

	// Branch A (RW 0x4C4EE0..0x4C4FD1): per-condition state shadow takes precedence if defined
	if (state && state->hasShadow && (state->shadow.type & kDecalMask) != 0)
	{
		out.enabled = true;
		out.style = (std::uint32_t)(state->shadow.type & kDecalMask);
		// RW 0x50BF93: default texture "shadow" if empty or single char
		out.texture = state->shadow.texture.size() <= 1 ? "shadow" : state->shadow.texture;
		out.sizeX = state->shadow.sizeX;
		out.sizeY = state->shadow.sizeY;
		out.offsetX = state->shadow.offsetX;
		out.offsetY = state->shadow.offsetY;
		out.sunAngle = state->shadow.sunAngle;
		out.maxHeight = state->shadow.maxHeight > 0.0f ? state->shadow.maxHeight : 20.0f;
		out.isRotatingWithObject = true; // RW 0x4C4F50: condition shadow sets isRotatingWithObject = true
		out.overrideLODVisibility = state->shadow.overrideLODVisibility;
		out.opacityStart = state->shadowOpacityStart;
		out.opacityFadeInTime = state->shadowOpacityFadeInTime;
		out.opacityPeak = state->shadowOpacityPeak;
		out.opacityFadeOutTime = state->shadowOpacityFadeOutTime;
		out.opacityEnd = state->shadowOpacityEnd;
		out.useHouseColor = false;
		return true;
	}

	// Branch B (RW 0x4C4FD6..0x4C5175, RW 0x47A846..0x47A934, RW 0x4B931E): ThingTemplate shadow
	if (!tt)
	{
		return false;
	}
	const FieldValue *shField = tt->findField("Shadow");
	const long long *bits = shField ? std::get_if<long long>(shField) : nullptr;
	if (!bits || (*bits & kDecalMask) == 0)
	{
		return false;
	}

	out.enabled = true;
	out.style = (std::uint32_t)(*bits & kDecalMask);

	// RW 0x50BF93..0x50BFBB: ShadowTexture defaults to "shadow" if absent or length <= 1
	std::string tex = readString(tt, "ShadowTexture", "shadow");
	out.texture = tex.size() <= 1 ? "shadow" : tex;

	out.sizeX = readFloat(tt, "ShadowSizeX", 0.0f);
	out.sizeY = readFloat(tt, "ShadowSizeY", 0.0f);
	out.offsetX = readFloat(tt, "ShadowOffsetX", 0.0f);
	out.offsetY = readFloat(tt, "ShadowOffsetY", 0.0f);
	out.sunAngle = readFloat(tt, "ShadowSunAngle", 0.0f);
	out.maxHeight = readFloat(tt, "ShadowMaxHeight", 20.0f); // RW 0x74015E
	out.overrideLODVisibility = readBool(tt, "ShadowOverrideLODVisibility", false);
	out.useHouseColor = readBool(tt, "ShadowUseHouseColor", false);
	out.isRotatingWithObject = readBool(tt, "ShadowIsRotatingWithObject", false); // RW 0x740172
	out.opacityStart = readUnsigned(tt, "ShadowOpacityStart", 0);
	out.opacityFadeInTime = readFloat(tt, "ShadowOpacityFadeInTime", 0.0f);
	out.opacityPeak = readUnsigned(tt, "ShadowOpacityPeak", 255); // RW 0x740136 default 0xFF
	out.opacityFadeOutTime = readFloat(tt, "ShadowOpacityFadeOutTime", 0.0f);
	out.opacityEnd = readUnsigned(tt, "ShadowOpacityEnd", 0);
	return true;
}

bool buildDrawableDecalShadow(const Drawable &drawable, SelectionDecal &out, bool useShadowDecals, const std::function<void(float &, float &)> &fallbackSize)
{
	// RW 0x672D28: shadowsEnabled false when dead with SlowDeathBehavior/ShadowWhenDead = No
	if (!drawable.shadowsEnabled())
	{
		return false;
	}

	// Shroud & client visibility gates (RW 0x4B66CD, RW 0x4B675E)
	if (drawable.clientHidden() || drawable.shroudLook().hidden || drawable.stealthHidden())
	{
		return false;
	}

	const W3DModelDrawModuleData *moduleData = nullptr;
	const ModelConditionInfo *state = nullptr;
	for (const DrawEntry &e : drawable.entries())
	{
		if (e.kind == W3D_DRAWKIND_MODEL && e.draw && !e.moduleHidden)
		{
			moduleData = dynamic_cast<const W3DModelDrawModuleData *>(e.data);
			state = e.draw->currentModelState();
			break;
		}
	}

	DecalShadowDescriptor desc;
	if (!resolveDecalShadow(drawable.getTemplate(), moduleData, state, desc))
	{
		return false;
	}
	// RW 0x50BF7B .. 0x50BF8E: addShadow makes no decal shadow unless [RW 0xDE4364] + 0x61 (UseShadowDecals) or the shadow's + 0x24
	// (ShadowOverrideLODVisibility, template + 0x5FD, RW 0x4C5050)
	if (!useShadowDecals && !desc.overrideLODVisibility)
	{
		return false;
	}

	// Size resolution: if sizeX or sizeY is 0, query fallback from model box (RW 0x50BFF3..0x50C066)
	if (desc.sizeX == 0.0f || desc.sizeY == 0.0f)
	{
		if (fallbackSize)
		{
			fallbackSize(desc.sizeX, desc.sizeY);
		}
	}
	if (desc.sizeX == 0.0f && desc.sizeY == 0.0f)
	{
		return false;
	}

	// Texture file name (append .tga if no extension, RW 0x50BF6A)
	const std::string file = desc.texture.find('.') == std::string::npos ? desc.texture + ".tga" : desc.texture;

	// Orientation: rotating with object vs fixed top-down (RW 0x4C4F50, RW 0x4C50CE)
	const float angle = desc.isRotatingWithObject ? drawable.getOrientation() : 0.0f;
	const Coord3D &p = *drawable.getPosition();
	const Coord3D center = DecalShadowCentre(p, desc.offsetX, desc.offsetY, angle);

	// Color resolution: house color (RW 0x4C50D2, RW 0x73232B) vs white
	std::uint32_t rgb = 0xFFFFFFu;
	if (desc.useHouseColor)
	{
		const std::uint32_t house = drawable.getHouseColor();
		if ((house & 0xFFFFFFu) != 0)
		{
			rgb = house & 0xFFFFFFu;
		}
	}

	out = SelectionDecal();
	out.object = drawable.getObjectID();
	out.drawable = (unsigned)drawable.getID();
	out.position = center;
	out.drawZ = p.z;
	out.size = desc.sizeX;
	out.sizeY = desc.sizeY;
	out.angle = angle;
	out.texture = file;
	out.style = desc.style;
	out.color = ((std::uint32_t)desc.opacityPeak << 24) | (rgb & 0xFFFFFFu);
	out.terrainOnly = false; // RW 0x50CDE4: projects on terrain and wall/bridge geometry
	return true;
}

} // namespace ObjectShadow
