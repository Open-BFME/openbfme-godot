// OpenBFME. GPL-3.0.
#pragma once

#include "Common/Thing/ThingTemplate.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DModelDraw.h"

namespace ObjectShadow
{
// RW 0xD99EB0's shadow bits (W3DModelDrawNameLists.inc kShadowTypeNames): the volume geometries SHADOW_VOLUME, SHADOW_VOLUME_NEW
// and the two *_OR_DECAL forms cast a Godot shadow map (S-3304). SHADOW_DECAL is a projected blob texture in retail, not a
// silhouette, so it casts none here; it, the projections and the NON_SELF volumes remain S-3304.
constexpr int MapMask = (1 << 1) | (1 << 2) | (1 << 14) | (1 << 15);

inline bool castsMap(const ThingTemplate *object, const W3DModelDrawModuleData *module, const ModelConditionInfo *state)
{
	if (module && module->m_shadowForceDisable) return false;
	if (state && state->hasShadow) return (state->shadow.type & MapMask) != 0;
	if (!object) return false;
	const FieldValue *field = object->findField("Shadow");
	const long long *bits = field ? std::get_if<long long>(field) : nullptr;
	return bits && (*bits & MapMask) != 0;
}
}
