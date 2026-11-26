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
struct SelectionDecal;

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

// Lane DRAWDECAL-1: a drawable's alpha or additive decal shadow (a catapult shot's glow, the Eye's and the crows' eye, a lamp's light, Sting's glow,
// the in-game markers). The drawables' shadow list draws every decal type through the same projection as the blobs (BFME2 renderShadows mask
// 0x2c61 -> rva0010C841): the LOD gate (drawnAtLOD), ShadowMaxHeight (belowMaxHeight; Shadow + 0x64, the terrain-only flag that skips it, is set
// only for the addDecal list's claim decals, RW 0x50C792) and the offsets are the blobs'. The device draws them with the selection decals' shaders,
// as the placement ghost's (PLACEDECAL-1, S-5032): SHADOW_ALPHA_DECAL 0x20, SHADOW_ADDITIVE_DECAL 0x40, their _DYNAMIC forms 0x400 / 0x800 and
// RotWK's 0x10000 (refused by the grid with its error).
constexpr int kDecalStyles = 0x10C60;
struct DecalSpec : Spec
{
	int type = 0;                ///< the decal style bits (type & kDecalStyles)
	unsigned opacityPeak = 0xFF; ///< ShadowOpacityPeak: the diffuse's alpha (DecalShadowColor)
	// ShadowOpacityStart / FadeInTime / FadeOutTime / End (the condition info's constructor: Start and End 0, RW 0x4C30A8 / 0x4C308E): a _DYNAMIC
	// decal's fade (DecalFade)
	unsigned opacityStart = 0, opacityEnd = 0;
	float fadeInMs = 0.0f, fadeOutMs = 0.0f;
	bool operator==(const DecalSpec &o) const
	{
		return texture == o.texture && sizeX == o.sizeX && sizeY == o.sizeY && offsetX == o.offsetX && offsetY == o.offsetY && maxHeight == o.maxHeight
			&& overrideLOD == o.overrideLOD && type == o.type && opacityPeak == o.opacityPeak && opacityStart == o.opacityStart && opacityEnd == o.opacityEnd
			&& fadeInMs == o.fadeInMs && fadeOutMs == o.fadeOutMs;
	}
};
// The drawable entry's alpha / additive decal shadow, from its model state's shadow when the state names one, else from the template's fields;
// false when it has none (another type, ShadowForceDisable, no template)
bool resolveDecal(const ThingTemplate *object, const W3DModelDrawModuleData *module, const ModelConditionInfo *state, DecalSpec &out);
// The decal of `spec` for a drawable at (x, y, z) facing `angle` (the shadow turns with its drawable, + 0x30 = 1, RW 0x4C4F53), as the placement
// ghost's: centre DecalShadowCentre, white at ShadowOpacityPeak (DecalShadowColor). False with a zero size (rva0010C841 draws nothing; no retail
// row has an alpha or additive decal without its size, so the model's box is not taken here). The colour's alpha is Peak; a _DYNAMIC decal's follows its DecalFade
bool decal(const DecalSpec &spec, float x, float y, float z, float angle, SelectionDecal &out);

// A _DYNAMIC decal's (0x400 / 0x800 / 0x10000, RW 0x4B1BC6) opacity plan, in client frames (TheGameClient's frame, 30 a second), as the shadow keeps it
// (Shadow + 0x38 .. + 0x54, set by RW 0x7323CE): from `t0` the opacity goes from `start` to `peak` until `t1`, holds `peak` until `hold`, goes to
// `endOpacity` until `end`, then holds that; the shadow lives until `life` (~0u: no limit). Times are unsigned as retail compares them.
bool isDynamic(int type);
struct DecalFade
{
	unsigned t0 = 0, t1 = 0, hold = ~0u, end = ~0u, life = ~0u;
	unsigned start = 0, peak = 0xFF, endOpacity = 0;
	unsigned fadeOutFrames = 0; ///< the planned fade-out's length (Shadow + 0x50 while `hold` is open)
};
// The model draw's shadow setup (RW 0x4C4F5A .. 0x4C4FBC): setOpacity(Start), then the plan t0 = now, t1 = now + FadeInTime * 0.03 (the ms in frames,
// truncated), no hold end, no life limit; FadeOutTime * 0.03 and End kept for the fade-out
DecalFade fadeIn(const DecalSpec &spec, unsigned now);
// The shadow's release while it lives (RW 0x508B40 .. 0x508B6F): planned again from now: no fade-in (straight to Peak), Peak held 0 frames, then down
// to End over the fade-out's frames, and the shadow ends with it (a zero FadeOutTime ends it at once)
DecalFade fadeOut(const DecalFade &fade, unsigned now);
// RW 0x50B8C9 .. 0x50B9F9 (the projected manager's frame, before the projection): false once `life` is reached (the shadow goes, RW 0x5FFFBF); else
// the opacity: 0 before t0, then the two ramps (Start + (now - t0) / (t1 - t0) * (Peak - Start), rounded up) and holds
bool opacityAt(const DecalFade &fade, unsigned now, unsigned &opacity);
} // namespace BlobShadow
