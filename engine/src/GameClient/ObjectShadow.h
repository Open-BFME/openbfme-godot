// OpenBFME. GPL-3.0.
#pragma once

#include "Common/Thing/ThingTemplate.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DModelDraw.h"

namespace ObjectShadow
{
// RW 0xD99EB0's shadow bits (W3DModelDrawNameLists.inc kShadowTypeNames): the volume geometries SHADOW_VOLUME, SHADOW_VOLUME_NEW
// and the two *_OR_DECAL forms; their volume shadows are drawn as Godot shadow maps (S-3304) at every map level (ShadowLOD Medium and up).
constexpr int MapMask = (1 << 1) | (1 << 2) | (1 << 14) | (1 << 15);

// lane MAPCAST-1: what an object's model throws into the Godot shadow maps (W3DInstancer::set_instance_shadow_caster)
enum CasterKind
{
	CASTER_NONE = 0,    // never (ShadowForceDisable)
	CASTER_ALWAYS = 1,  // whenever maps are drawn: the volume shadows' substitute
	CASTER_MAPPING = 2, // only while retail's own shadow map is on (ShadowLOD High / UltraHigh)
};

// lane MAPCAST-1: RotWK's shadow map (W3DShadowMapManager::updateShadowMap RW 0x47D5C9, BFME2 0x7DC01: the scene rendered from the shadow
// camera through RTS3DScene::Render, the shadow RenderingMethod RW 0x47B1BB swapping each effect's technique for its _CreateShadowMap one) has
// no caster filter on the Shadow field: RTS3DScene's visibility check (BFME2 RTS3DSceneVisibilityCheck.cpp) culls only hidden, out-of-frustum
// and shrouded objects, and MeshClass::Render (RW 0x54B75D) draws every FX mesh. So every visible model casts there: SHADOW_DECAL infantry,
// cavalry and trees, SHADOW_NONE objects too (BFME2 decomp, tier B; RotWK's update, method and mesh render read). Retail draws no decal blobs
// while it maps (RW 0x601D5B), and no map at ShadowLOD Low / Medium, where those objects keep to S-3304's missing decal blobs.
// INFERENCE (S-3304): a model's ShadowForceDisable keeps it out of the map too (retail's map path was not seen to read it).
inline CasterKind casterKind(const ThingTemplate *object, const W3DModelDrawModuleData *module, const ModelConditionInfo *state)
{
	if (module && module->m_shadowForceDisable) return CASTER_NONE;
	if (state && state->hasShadow) return (state->shadow.type & MapMask) != 0 ? CASTER_ALWAYS : CASTER_MAPPING;
	const FieldValue *field = object ? object->findField("Shadow") : nullptr;
	const long long *bits = field ? std::get_if<long long>(field) : nullptr;
	return bits && (*bits & MapMask) != 0 ? CASTER_ALWAYS : CASTER_MAPPING;
}

// a volume shadow's substitute (CASTER_ALWAYS)
inline bool castsMap(const ThingTemplate *object, const W3DModelDrawModuleData *module, const ModelConditionInfo *state)
{
	return casterKind(object, module, state) == CASTER_ALWAYS;
}
}
