// OpenBFME. GPL-3.0.
#pragma once

// Lane BLOBSHADOW-1: retail's SHADOW_DECAL blob shadows (the projected shadow manager's shadow list) as data: which drawable entry has one, its
// texture, size and offset, whether the current ShadowLOD draws it, and the quad it covers on the ground. The device draws the quads into a
// top-down accumulation texture the terrain multiplies (GodotBlobShadows.h, godot/shaders/blob_shadows.gdshaderinc).
//
// Retail (RotWK 2.01; BFME2 1.06 decompilation Code/GameEngineDevice/Source/W3DDevice/GameClient/Shadow/W3DProjectedShadow.cpp as the donor, tier B):
//   * W3DShadowManager::addShadow (RW 0x49A2E0 .. 0x49A41F) hands SHADOW_DECAL (0x1) to the projected manager (RW 0x50BF6A); the *_OR_DECAL forms
//     (0x4000 / 0x8000) become a decal while GlobalData + 0x60 (UseShadowVolumes) is off;
//   * the projected manager makes no shadow while GlobalData + 0x61 (UseShadowDecals: ShadowLOD Low and Medium, cleared while shadow mapping is on,
//     RW 0x601D5B) is off, unless the template's ShadowOverrideLODVisibility is set (RW 0x50BF7A .. 0x50BF8A);
//   * an empty ShadowTexture is "shadow" (RW 0x50BFA6), loaded as "<name>.tga" (RW 0x50B690);
//   * ShadowSizeX / Y of 0 take twice the object's local bounding box extent (RW 0x50BFE8 .. 0x50C06F);
//   * the blob's u axis is the object's facing (cos, sin) with ShadowIsRotatingWithObject (default Yes), else the world X axis; v is (sin, -cos);
//     its centre is the position moved by ShadowOffsetX along u and ShadowOffsetY along v (DecalShadowCentre, RW 0x50C07E / 0x50C083);
//   * it is projected straight down and not drawn while the object is ShadowMaxHeight (default 20) or more above the ground (BFME2 rva0010C841).

#include <string>

class ThingTemplate;
struct RenderObjPrototype;
struct ModelConditionInfo;
class W3DModelDrawModuleData;

namespace BlobShadow
{
struct Spec
{
	std::string texture;  ///< the file name as the asset manager is asked for it ("ShadowI.tga")
	float sizeX = 0.0f;   ///< 0: twice the local bounding box extent
	float sizeY = 0.0f;
	float offsetX = 0.0f;
	float offsetY = 0.0f;
	float maxHeight = 20.0f;
	bool rotating = true;
	bool overrideLOD = false;
};

// The drawable entry's SHADOW_DECAL, from its model state's shadow when the state names one, else from the object template's fields. False when
// the entry has none (another shadow type, ShadowForceDisable, no template). `useShadowVolumes`: GlobalData + 0x60 (an *_OR_DECAL form is a decal
// while it is off).
bool resolve(const ThingTemplate *object, const W3DModelDrawModuleData *module, const ModelConditionInfo *state, bool useShadowVolumes, Spec &out);

// RW 0x50BF7A: whether the projected manager keeps the shadow at the current ShadowLOD (`useShadowDecals`: GlobalData + 0x61).
inline bool drawnAtLOD(const Spec &spec, bool useShadowDecals)
{
	return useShadowDecals || spec.overrideLOD;
}

// BFME2 rva0010C841: no terrain blob while the object is ShadowMaxHeight or more above the ground under it.
inline bool belowMaxHeight(const Spec &spec, float objectZ, float groundZ)
{
	return objectZ - groundZ < spec.maxHeight;
}

// The ground quad: its centre and its two half axes (SAGE x / y). `angle`: the object's facing; `extentX` / `extentY`: the local bounding box's
// half extents (used when the size is 0). False when the quad is empty (retail draws nothing with a zero size).
struct Quad
{
	float cx = 0.0f, cy = 0.0f;
	float ux = 0.0f, uy = 0.0f; ///< half of the u side (sizeX / 2 along the u axis)
	float vx = 0.0f, vy = 0.0f; ///< half of the v side (sizeY / 2 along the v axis; negative sizeY mirrors the texture, as retail's negated SizeY)
};
bool quad(const Spec &spec, float x, float y, float angle, float extentX, float extentY, Quad &out);

// Lane BLOBSHADOW-2: the half extents (SAGE x / y) a zero ShadowSizeX / Y takes. RW 0x50C01D asks the render object for its object-space box
// (vslot 0x110) and doubles Extent.X / Extent.Y (RW 0x50C034 / 0x50C054). INFERENCE (S-3304): the box is the bounds of the model's drawn meshes in
// the hierarchy's bind pose (DrawablePick::bindPoseBounds; the HLOD's own box, its hidden meshes and collision boxes are not read) and the drawable's
// instance scale is not applied. False (both 0: no blob) for a model without triangles
bool modelExtent(const RenderObjPrototype *model, float &extentX, float &extentY);
} // namespace BlobShadow
