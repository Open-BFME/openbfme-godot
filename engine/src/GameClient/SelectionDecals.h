// OpenBFME. GPL-3.0.
//
// SelectionDecals (lane UI-4): the selection marker under a selected unit, horde or building of the local player (RotWK's selection RadiusDecal).
//
// TARGET FACTS (RotWK game.dat, caveat S-001; the BFME2 twins are tier A / B with byte-matched decomp source where named):
// - InGameUI::selectDrawable (BFME2 decomp InGameUISelectDrawable.cpp) sets the drawable's selected flag through RW 0x6799E3, which runs RW 0x67580E
//   (BFME2 decomp Drawable_rva00275545.cpp:Drawable::rva002754E3): a horde (the object's horde interface, RW 0x68C866) does its own (vslot 0xE4, HordeContain
//   RW 0x86F4EE = BFME2 decomp HordeContain::rva0046C20B); another object only when it is locally controlled (RW 0x68B749): Drawable RW 0x672ED1(1, the
//   object's indicator colour RW 0x6916CD) -> the draw module's vslot 0x6C (RW 0x4B2A9B);
// - the horde (RW 0x86F4EE): only for the local player's horde; with GameData UseSimpleHordeDecals (GlobalData + 0x9A6) one decal on the horde for its
//   member count, else one decal of count 1 on every member (contained and registered);
// - the draw module (RW 0x4B2A9B): nothing unless GameData ShowSelectedUnitMarker (GlobalData + 0x9A5) and, with a player list, the object controlled by the
//   local player; the decal template is the one the drawable was given (Drawable RW 0x672C32, + 0x44C) by ExperienceTracker::setLevel (RW 0x79DACD: the
//   object's ExperienceLevel's SelectionDecal block, level + 0xA4); RadiusDecalTemplate::createSelectionRadiusDecal (RW 0x732BAD, BFME2 decomp
//   RadiusDecalTemplateCreate.cpp): nothing without a Texture, a count or MinRadius / MaxRadius; the decal is Texture / Texture2 in the Style, its size in
//   both axes min + (count - 1) * (max - min) / MaxSelectedUnits, at most max (RW 0x7326CC; MinRadius + 0x20, MaxRadius + 0x24, MaxSelectedUnits + 0x28),
//   angle 0, the colour given, at the object's position;
// - each frame (BFME2 decomp RadiusDecal.cpp RadiusDecal::update): opacity = (min + (max - min) * (sin(2 pi * (client frame mod period) / period) + 1) / 2)
//   * 255 with period = max(ceil(OpacityThrobTime * 30), 1) client frames, 0 while the in-game UI is hidden; RotationsPerMinute turns it;
// - a decal whose Style is SHADOW_MERGE_DECAL (0x1000) is drawn by the projected shadow manager's stencil list (RW 0x50DA71 = BFME2 decomp
//   W3DProjectedShadowRenderShadows.cpp rva0010DA71, flushDecals W3DProjectedShadowManagerFlushDecals.cpp): stencil on, ref 255, fail / z-fail KEEP;
//   two passes over the whole list when the device has a stencil and GameData UseSimpleMergeDecals (+ 0x9A7) is off, else one:
//     * pass 0: each decal's Texture2 (the cut-out, e.g. decal_good_CO) with the stencil ALWAYS / REPLACE, colour writes off, the alpha test on;
//     * pass 1: each decal's Texture (e.g. decal_G_level4) where the stencil is NOT 255, opaque (blend ONE / ZERO), the alpha test on;
//     so the selected units' discs show only where no unit's cut-out lies: one jagged outline around all of them (the stencil is cleared after the list);
//     * with UseSimpleMergeDecals: one pass of Texture, stencil NOT EQUAL / REPLACE (each pixel drawn once), blended (SRCALPHA / INVSRCALPHA) with the
//       alpha test at OpacityOfSimpleMergeDecals (+ 0x9A8) * 255 (GREATER EQUAL);
// - GameData's ShowSelectedUnitMarker and UseSimpleMergeDecals are overwritten when the static LOD level is applied (RW 0x601C62): ShowSelectedUnitMarker =
//   the level's DecalLOD (record + 0x40: Off 0, Low 1, High 2) > 0, UseSimpleMergeDecals = DecalLOD < 2.
// - nothing else draws the decal: the draw module's vslot 0x6C has no hover caller (its callers are RW 0x67580E, the horde RW 0x86F4EE, RW 0x872D0A, RW 0x664F93),
//   so a hovered unit shows no marker.
// INFERENCE (stop S-2522): the device draws each other decal as a projected texture of the given size at the drawable's position, tinted by the colour at the
//   opacity (Texture2 of a non-merge decal is not used; the descriptor's + 0x20 value, 20 by default, is not used); the client frame is 30 a second of
//   render time; the merge decals are composed on the CPU (ComposeMergeDecals) into one texture laid flat over their extent, the alpha test of the two
//   passes compares with 0x60 (ZH ShaderClass::Apply's ALPHAREF; RotWK's value not read); the texel is the texture's alpha times the colour's alpha
//   (stage 0 MODULATE), the colour the texture's times the decal's; the static LOD level is not applied to the renderer (S-2481): the device takes the
//   level the renderer draws at, UltraHigh.

#pragma once

#include "Common/INIDataTypes.h"
#include "GameLogic/ObjectTypes.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class DrawableManager;
class GameLogic;
class Object;
class Player;

struct SelectionDecalSettings
{
	bool showSelectedUnitMarker = false; ///< GameData ShowSelectedUnitMarker (GlobalData + 0x9A5)
	bool useSimpleHordeDecals = false;   ///< GameData UseSimpleHordeDecals (+ 0x9A6)
	bool useSimpleMergeDecals = false;   ///< GameData UseSimpleMergeDecals (+ 0x9A7)
	float opacityOfSimpleMergeDecals = 0.0f; ///< GameData OpacityOfSimpleMergeDecals (+ 0x9A8)
	bool drawIconUI = true;              ///< TheGameLogic's draw-icon-UI flag (false: every decal at opacity 0)
};

struct SelectionDecal
{
	ObjectID object = INVALID_ID; ///< whose drawable carries it
	Coord3D position{ 0.0f, 0.0f, 0.0f };
	float size = 0.0f;            ///< the decal's width and height in world units
	float angle = 0.0f;           ///< radians
	std::string texture, texture2;
	std::uint32_t style = 0;
	std::uint32_t color = 0;      ///< 0xAARRGGBB, the alpha the opacity
};

// the decals of `selected` this client frame
std::vector<SelectionDecal> BuildSelectionDecals(GameLogic &logic, const DrawableManager *drawables, const Player *local, const std::vector<ObjectID> &selected,
	const SelectionDecalSettings &settings, unsigned clientFrame);

// RW 0x601C62: the static LOD level's DecalLOD (0 Off, 1 Low, 2 High) over GameData's ShowSelectedUnitMarker / UseSimpleMergeDecals
void ApplyDecalLOD(SelectionDecalSettings &settings, int decalLOD);

// a texture's pixels, RGBA8 rows from the top
struct DecalImage
{
	int width = 0, height = 0;
	const std::uint8_t *rgba = nullptr;
};

// the merge decals composed as RotWK's stencil passes draw them: one RGBA8 image laid flat over [minX, maxX] x [minY, maxY] (row 0 at maxY)
struct MergedSelectionDecals
{
	float minX = 0.0f, minY = 0.0f, maxX = 0.0f, maxY = 0.0f;
	float z = 0.0f;   ///< the lowest decal's height
	int width = 0, height = 0;
	std::vector<std::uint8_t> rgba;
	size_t decals = 0; ///< how many decals went in
};

// the decals of `decals` whose Style has SHADOW_MERGE_DECAL (0x1000); `image` answers a texture's pixels (width 0: not found, an error). At most
// maxPixels a side, at the textures' own texel density when that fits (and at most maxTexelsPerUnit when that is not 0). False with `error` when a texture is missing.
bool ComposeMergeDecals(const std::vector<SelectionDecal> &decals, const std::function<DecalImage(const std::string &)> &image, const SelectionDecalSettings &settings,
	MergedSelectionDecals &out, std::string *error, int maxPixels = 1024, float maxTexelsPerUnit = 0.0f);
