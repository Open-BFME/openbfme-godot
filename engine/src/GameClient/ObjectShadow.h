// OpenBFME. GPL-3.0.
// Object shadow resolution for shadow maps and decal shadows.
//
// TARGET FACTS (RotWK game.dat):
//   ShadowType bits (RW 0xD99EB0):
//     0x0001 SHADOW_DECAL: multiplicative projected texture (RW 0x509A70: ShaderClass 0x00111853, SRCBLEND_ZERO / DSTBLEND_SRC_COLOR, MODULATE)
//     0x0002 SHADOW_VOLUME / 0x0004 SHADOW_VOLUME_NEW: volume shadow geometry (casts shadow map, S-3304)
//     0x0020 SHADOW_ALPHA_DECAL / 0x0400 SHADOW_ALPHA_DECAL_DYNAMIC: alpha decal (RW 0x509A1F: ShaderClass 0x001198B3, SRC_ALPHA / ONE_MINUS_SRC_ALPHA)
//     0x0040 SHADOW_ADDITIVE_DECAL / 0x0800 SHADOW_ADDITIVE_DECAL_DYNAMIC: additive decal (RW 0x509A1F: ShaderClass 0x00515833, ONE / ONE)
//     0x1000 SHADOW_MERGE_DECAL: merge decal stencil passes (RW 0x50D46A)
//     0x2000 SHADOW_DECAL_TERRAIN_CLAIM: terrain claim grid decal (RW 0x50C48B)
//     0x4000 SHADOW_VOLUME_OR_DECAL / 0x8000 SHADOW_VOLUME_NEW_OR_DECAL: volume shadow if shadow volumes enabled, else decal (RW 0x49A2EB)
//   Condition shadow vs Template shadow precedence:
//     W3DScriptedModelDraw::setModelState (RW 0x4C4EE0..0x4C5175) and W3DHordeModelDraw::setModelState (RW 0x47A0AD):
//       Branch A (RW 0x4C4EE0): if current ModelConditionInfo has hasShadow (type != 0), it takes precedence over template.
//         isRotatingWithObject = true (RW 0x4C4F50).
//       Branch B (RW 0x4C4FD6): else if ThingTemplate has Shadow (+0x5F2 != 0), reads Shadow, ShadowTexture, ShadowSizeX,
//         ShadowSizeY, ShadowOffsetX, ShadowOffsetY, ShadowSunAngle, ShadowMaxHeight, ShadowOverrideLODVisibility,
//         ShadowUseHouseColor, ShadowIsRotatingWithObject, ShadowOpacityStart/Peak/End, ShadowOpacityFadeIn/OutTime.
//       ShadowForceDisable (RW 0x4C4EF8 / 0x4C5013): when true, no shadow is created.
//   Visibility & corpse switch-off:
//     Drawable::setShadowsEnabled (RW 0x672D28): false when dead with SlowDeathBehavior/ShadowWhenDead = No.
//   Shadow creation & parameters (W3DProjectedShadowManager::addShadow, RW 0x50BF6A):
//     Texture defaults to "shadow" when length <= 1 (RW 0x50BF93).
//     Size falls back to model bounding box if sizeX or sizeY is 0 (RW 0x50BFF3..0x50C066).
//     Center: DecalShadowCentre(drawablePos, offsetX, offsetY, angle) (RW 0x50C07B).
//     House color: Shadow::setColor from player color when ShadowUseHouseColor is true (RW 0x4C50D2, RW 0x73232B).

#pragma once

#include "Common/Thing/ThingTemplate.h"
#include "GameClient/SelectionDecals.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DModelDraw.h"

#include <cstdint>
#include <functional>
#include <string>

class Drawable;
struct RenderObjPrototype;

namespace ObjectShadow
{
// Volume shadow map mask (casts Godot directional shadow map)
constexpr int MapMask = (1 << 1) | (1 << 2) | (1 << 14) | (1 << 15);

// Decal shadow styles mask: SHADOW_DECAL (0x1), SHADOW_ALPHA_DECAL (0x20), SHADOW_ADDITIVE_DECAL (0x40),
// SHADOW_ALPHA_DECAL_DYNAMIC (0x400), SHADOW_ADDITIVE_DECAL_DYNAMIC (0x800)
constexpr unsigned kDecalMask = 0x0001u | 0x0020u | 0x0040u | 0x0400u | 0x0800u;

struct DecalShadowDescriptor
{
	bool enabled = false;
	std::string texture;
	std::uint32_t style = 0;
	float sizeX = 0.0f;
	float sizeY = 0.0f;
	float offsetX = 0.0f;
	float offsetY = 0.0f;
	float sunAngle = 0.0f;
	float maxHeight = 20.0f; // RW 0x74015E default 20.0f
	bool isRotatingWithObject = false; // RW 0x740172 default false
	bool useHouseColor = false; // RW 0x74016C default false
	bool overrideLODVisibility = false; // RW 0x740166 default false
	unsigned opacityStart = 0;
	float opacityFadeInTime = 0.0f;
	unsigned opacityPeak = 255; // RW 0x740136 default 0xFF
	float opacityFadeOutTime = 0.0f;
	unsigned opacityEnd = 0;
};

// Returns whether the object/state casts a volume shadow map
inline bool castsMap(const ThingTemplate *object, const W3DModelDrawModuleData *module, const ModelConditionInfo *state)
{
	if (module && module->m_shadowForceDisable) return false;
	if (state && state->hasShadow) return (state->shadow.type & MapMask) != 0;
	if (!object) return false;
	const FieldValue *field = object->findField("Shadow");
	const long long *bits = field ? std::get_if<long long>(field) : nullptr;
	return bits && (*bits & MapMask) != 0;
}

// Queries object-space bounding box extents (Extent.X, Extent.Y) of a model prototype (RW 0x50C01D)
bool getModelBoundingBox(const RenderObjPrototype *proto, float &extentX, float &extentY);

// Resolves decal shadow descriptor from template, module data, and condition state (RW 0x4C4EE0..0x4C5175)
bool resolveDecalShadow(const ThingTemplate *tt, const W3DModelDrawModuleData *module, const ModelConditionInfo *state, DecalShadowDescriptor &out);

// Builds SelectionDecal for a live Drawable (respecting shadowsEnabled, shroud/stealth, rotation, house color). useShadowDecals is
// TheGlobalData + 0x61 (UseShadowDecals after the static LOD apply, RW 0x601C62: cleared when UseShadowMapping is set): addShadow's gate RW 0x50BF7B
bool buildDrawableDecalShadow(const Drawable &drawable, SelectionDecal &out, bool useShadowDecals,
	const std::function<void(float &, float &)> &fallbackSize = nullptr);

} // namespace ObjectShadow
