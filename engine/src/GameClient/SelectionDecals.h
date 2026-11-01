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
//   RadiusDecal.cpp): nothing without a Texture, a count or MinRadius / MaxRadius; the decal is Texture / Texture2 in the Style, its size in
//   both axes min + (count - 1) * (max - min) / MaxSelectedUnits, at most max (RW 0x7326CC; MinRadius + 0x20, MaxRadius + 0x24, MaxSelectedUnits + 0x28),
//   angle 0, the colour given, at the object's position;
// - each frame (BFME2 decomp RadiusDecal.cpp RadiusDecal::update): opacity = (min + (max - min) * (sin(2 pi * (client frame mod period) / period) + 1) / 2)
//   * 255 with period = max(ceil(OpacityThrobTime * 30), 1) client frames, 0 while the in-game UI is hidden; RotationsPerMinute turns it;
// - a decal whose Style is SHADOW_MERGE_DECAL (0x1000) is drawn by the projected shadow manager's stencil list (RW 0x50D46A, tier A: identical to BFME2
//   0x50DA71 = BFME2 decomp W3DProjectedShadowRenderShadows.cpp rva0010DA71 (RVA 0x10DA71; RotWK's own 0x50DA71 lies in the ordinary shadow renderer
//   RW 0x50D92C); the stencil states are flushDecals' (RW 0x50977A, tier B: BFME2 decomp W3DProjectedShadowManagerFlushDecals.cpp, byte-matched C++)):
//   stencil on, ref 255, fail / z-fail KEEP;
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
// Lane SELPERF-1 (the owner: "select a bunch of troops, fps drops to 25-12"): composing the merge decals on the CPU every frame they move cost seconds a
//   frame for 150 hordes (2250 members). The device now hands the terrain shader the decal list binned into a grid (BuildMergeDecalGrid) and the
//   shader runs the two stencil passes per screen pixel, as RotWK's GPU does (godot/shaders/selection_decals.gdshaderinc); EvaluateMergeDecalGrid is
//   that shader's arithmetic in C++, tested against ComposeMergeDecals, which stays the reference (and the device's state report).

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
	bool operator==(const SelectionDecalSettings &o) const
	{
		return showSelectedUnitMarker == o.showSelectedUnitMarker && useSimpleHordeDecals == o.useSimpleHordeDecals && useSimpleMergeDecals == o.useSimpleMergeDecals
			&& opacityOfSimpleMergeDecals == o.opacityOfSimpleMergeDecals && drawIconUI == o.drawIconUI;
	}
};

struct SelectionDecal
{
	ObjectID object = INVALID_ID; ///< whose drawable carries it
	unsigned drawable = 0;        ///< lane SELPERF-1: that drawable's DrawableID when it had one (0: none), RefreshSelectionDecals' direct lookup
	Coord3D position{ 0.0f, 0.0f, 0.0f };
	float size = 0.0f;            ///< the decal's width and height in world units
	float angle = 0.0f;           ///< radians
	std::string texture, texture2;
	std::uint32_t style = 0;
	std::uint32_t color = 0;      ///< 0xAARRGGBB, the alpha the opacity
	// lane SELPERF-1: the decal template's throb and turn (RadiusDecal::update), for RefreshSelectionDecals
	unsigned throbCycle = 1;      ///< max(ceil(OpacityThrobTime * 30), 1) client frames
	float opacityMin = 0.0f, opacityMax = 0.0f, rotationsPerMinute = 0.0f;
};

// the decals of `selected` this client frame
std::vector<SelectionDecal> BuildSelectionDecals(GameLogic &logic, const DrawableManager *drawables, const Player *local, const std::vector<ObjectID> &selected,
	const SelectionDecalSettings &settings, unsigned clientFrame);
// the same into `out` (lane SELPERF-1: the HUD's list keeps its capacity from frame to frame)
void BuildSelectionDecals(GameLogic &logic, const DrawableManager *drawables, const Player *local, const std::vector<ObjectID> &selected,
	const SelectionDecalSettings &settings, unsigned clientFrame, std::vector<SelectionDecal> &out);

// lane SELPERF-1: `list` (BuildSelectionDecals' of this logic frame and selection) at a later client frame of the same logic frame: each decal at its
// drawable's position (kept where the drawable is gone), the throb's opacity and the turn of `clientFrame`; what BuildSelectionDecals would give then
// (objects, levels, colours and horde members change only in a logic frame)
// True when any decal's place, colour or angle changed (the device rebuilds the shader's grid only then).
bool RefreshSelectionDecals(const DrawableManager *drawables, const SelectionDecalSettings &settings, unsigned clientFrame, std::vector<SelectionDecal> &list);

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

// lane SELPERF-1: a texture's place in the merge decals' atlas, in texels (row 0 the top)
struct MergeDecalAtlasRect
{
	int x = 0, y = 0, w = 0, h = 0;
};

// lane SELPERF-1: the merge decals as the terrain shader reads them (godot/shaders/selection_decals.gdshaderinc): RGBA32F texels, kWidth a row:
//   decal i at texels 4i .. 4i + 3: (x, y, size, opacity 0..255), (r, g, b 0..255, 0), Texture's atlas rect (x, y, w, h), Texture2's (w 0 in the simple mode);
//   cell c of the grid at cellOffset + c: (first, count) into the index list; the index list's entry j at indexOffset + j: (x, y, size, decal) (the place
//   repeated: the shader's bounds test reads one texel), each cell's decals in the list's order (pass 1's "the last disc drawn wins" and the simple
//   pass's "the first one wins")
struct MergeDecalGrid
{
	static constexpr int kWidth = 1024;
	static constexpr int kMaxCellsPerAxis = 128;
	std::vector<float> texels; ///< 4 floats a texel, rows * kWidth texels
	int rows = 0;
	float minX = 0.0f, minY = 0.0f, cellSize = 1.0f;
	int cellsX = 0, cellsY = 0;
	int cellOffset = 0, indexOffset = 0;
	size_t decals = 0;     ///< how many merge decals went in (0: the shader draws nothing)
	int maxPerCell = 0;    ///< the longest cell list (the shader's loop)
	bool simple = false;   ///< UseSimpleMergeDecals: one blended pass
	int alphaRef = 0x60;   ///< the alpha test's reference (0..255)
};

// lane SELPERF-1: the merge decals' textures in one RGBA8 atlas, kWidth texels wide (rows of textures). update() adds the textures a decal list needs that
// are not in it yet; the new atlas is packed in local copies and committed whole (the rects, the names and the pixels), so a texture that is missing
// (`image` answers width 0: BuildMergeDecalGrid reports it, and it is asked for again next time) or too wide (an error here, once) never enters it, and
// the textures already in it keep valid places
class MergeDecalAtlas
{
public:
	static constexpr int kWidth = 1024;
	// true when the atlas changed (the device uploads pixels()); `errors` gets the too-wide textures
	bool update(const std::vector<SelectionDecal> &decals, const SelectionDecalSettings &settings, const std::function<DecalImage(const std::string &)> &image,
		std::vector<std::string> &errors);
	bool rect(const std::string &name, MergeDecalAtlasRect &out) const;
	const std::vector<std::uint8_t> &pixels() const { return m_pixels; }
	int width() const { return kWidth; }
	int height() const { return m_height; }
	size_t textures() const { return m_names.size(); }
	void clear();

private:
	std::vector<std::string> m_names; // in the order packed
	std::vector<MergeDecalAtlasRect> m_rects;
	std::vector<std::string> m_rejected;
	std::vector<std::uint8_t> m_pixels;
	int m_height = 0;
};

// the decals of `decals` whose Style has SHADOW_MERGE_DECAL into `out`; `rect` answers a texture's atlas rectangle (false: not in the atlas, an error).
// False with `error` when a texture is missing (as ComposeMergeDecals).
bool BuildMergeDecalGrid(const std::vector<SelectionDecal> &decals, const std::function<bool(const std::string &, MergeDecalAtlasRect &)> &rect,
	const SelectionDecalSettings &settings, MergeDecalGrid &out, std::string *error);

// the shader's result at the world point (x, y) (selection_decals.gdshaderinc sel_apply, in C++): false where nothing is drawn; else the colour bytes
// and the alpha the terrain is covered with (255 for the two passes, the opacity for the simple pass)
bool EvaluateMergeDecalGrid(const MergeDecalGrid &grid, const DecalImage &atlas, float x, float y, std::uint8_t rgba[4]);
