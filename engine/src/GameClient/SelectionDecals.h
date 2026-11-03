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
// - lane SELWALL-1: every selection decal (the merge list RW 0x50D46A and the other styles' renderShadows RW 0x50D92C alike) is projected by RW 0x50C23A
//   (= BFME2 0x50C841, W3DProjectedShadowDecalHelpers.cpp rva0010C841, 0 instructions apart) with the height range the shadow's + 0x60 holds (RW 0x50B597):
//   the selected drawable's current condition state's ShadowMaxHeight (RW 0x4B2AD6 .. 0x4B2AF4; field table RW 0xBE0BC8), 20 without a state or a shadow
//   record (20.0 at RW 0xBDBC6C; the record's own default is 20 too); its terrain part only while the decal's z - the floor < that height (RW 0x50CDE4 ..
//   0x50CDF9; the floor max(the GROUND layer's height, a bridge's height + 1), RW 0x50C4DE .. 0x50C569), and onto the parts of at most 16 KINDOF
//   WALK_ON_TOP_OF_WALL objects whose centre lies within sqrt(sx^2 + sy^2) + 50 in 3D (RW 0x50C876 .. 0x50C8B1) and whose bounding box meets the box
//   (half extents sx, sy, the height) around the decal (RW 0x50C79C .. 0x50C7E7), every triangle of them, top-down from the decal's place; the decal stands
//   at Object's visual position (RW 0x68DB35: a contained object's container's; an AIRBORNE_TARGET object's at the height of its layer, RW 0x68DB8B ..
//   0x68DBE1; else the drawable's), so a unit on a wall top has it at the wall's height and a flying unit on the ground or wall under it.
// INFERENCE (stop S-2522): the device draws each other decal (lane HERORING-1: an alpha decal, in the shaders' grid with the merge decals, blended as
//   RotWK's ShaderClass 0x1198B3 before them) as a texture of the given size at the drawable's position, tinted by the colour at the opacity (Texture2 of
//   a non-merge decal is not used; lane SELWALL-1: on every wall top / bridge model within 20 of the decal's height, not RotWK's
//   caster set (at most 16 objects near the decal, their walkway sub-meshes RampMesh1 / RampMesh2 / WallBoundsMesh / RaisedWallMesh, RW 0x6729A3 /
//   0x6729E9 / 0x672943 / 0x672973; the port: the upward faces of the visible models); its terrain part where it is not raised, MarkRaisedSelectionDecals with
//   the terrain's height as the floor, no terrain bridges, S-161; the height range is the constant 20, kSelectionDecalMaxHeight: no retail selectable
//   object sets ShadowMaxHeight; a contained object's decal stays at its own drawable); the client frame is 30 a second of render time; the merge decals
//   are drawn per pixel by the terrain shader and, lane SELWALL-1, the wall models' shader (SELPERF-1; ComposeMergeDecals is the CPU reference), the alpha test of the two
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
	// lane SELWALL-1: the decal stands kSelectionDecalMaxHeight or more above the terrain's floor at its place (MarkRaisedSelectionDecals): retail draws
	// no terrain part of it, only the wall tops and bridges it lies on
	bool raised = false;
	// lane SELWALL-1: the drawable's height (position.z before an AIRBORNE_TARGET object's layer height): RefreshSelectionDecals compares with it, so a
	// grounded decal is not a change every frame
	float drawZ = 0.0f;
};

// lane SELWALL-1: the projected decal's height range (the shadow's + 0x60): the drawable's condition state's ShadowMaxHeight, 20 without one
// (RW 0x4B2AD6 .. 0x4B2AF4); INFERENCE (S-2522): the constant, since no retail selectable object sets ShadowMaxHeight
constexpr float kSelectionDecalMaxHeight = 20.0f;

// lane SELWALL-1: each decal's `raised` from `floorHeight(x, y)` (the terrain part's floor at the decal's place): raised when position.z - floor is not below
// kSelectionDecalMaxHeight. `groundedHeight` (may be empty) first answers the height of a decal whose object stands on its layer for the decal rather than at its
// drawable (RW 0x68DB35: an AIRBORNE_TARGET object's visual position takes the height of the layer below it, so a flying unit's decal lies on the ground or the
// wall top under it): true with `z` set. True when any decal's flag or height changed
bool MarkRaisedSelectionDecals(std::vector<SelectionDecal> &list, const std::function<float(float, float)> &floorHeight,
	const std::function<bool(ObjectID, const Coord3D &, float &)> &groundedHeight = {});
// the same with the game's own answers (the HUD's): the floor GameLogic::getGroundHeight; an AIRBORNE_TARGET object's decal at
// GameLogic::getLayerHeightForDestination at its drawable's point (INFERENCE, S-2522: the point's layer, not the object's footprint shift). The logic must
// be held (RENDERWAIT-1)
bool MarkRaisedSelectionDecals(std::vector<SelectionDecal> &list, const GameLogic &logic);

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

// the decals of `decals` whose Style has SHADOW_MERGE_DECAL (0x1000) as the terrain shows them (lane SELWALL-1: a raised decal is left out); `image` answers
// a texture's pixels (width 0: not found, an error). At most
// maxPixels a side, at the textures' own texel density when that fits (and at most maxTexelsPerUnit when that is not 0). False with `error` when a texture is missing.
bool ComposeMergeDecals(const std::vector<SelectionDecal> &decals, const std::function<DecalImage(const std::string &)> &image, const SelectionDecalSettings &settings,
	MergedSelectionDecals &out, std::string *error, int maxPixels = 1024, float maxTexelsPerUnit = 0.0f);

// lane SELPERF-1: a texture's place in the merge decals' atlas, in texels (row 0 the top)
struct MergeDecalAtlasRect
{
	int x = 0, y = 0, w = 0, h = 0;
};

// lane SELPERF-1: the merge decals as the terrain shader reads them (godot/shaders/selection_decals.gdshaderinc): RGBA32F texels, kWidth a row:
//   decal i at texels 4i .. 4i + 3: (x, y, size, opacity 0..255), (r, g, b 0..255, z), Texture's atlas rect (x, y, w, h), Texture2's (w 0 in the simple mode;
//   lane HERORING-1: an alpha decal's (cos, sin) of its turn, 0, 0); the merge decals are records 0 .. decals - 1, the alpha decals the next alphaDecals;
//   cell c of the grid at cellOffset + c: (first, count) into the index list (lane HERORING-1: then (alphaFirst, alphaCount), its alpha entries, which follow
//   every merge entry); the index list's entry j at indexOffset + j: (x, y, size, decal) (the place
//   repeated: the shader's bounds test reads one texel; lane SELWALL-1: decal is -(i + 1) for a raised decal i, which the terrain skips before any other
//   fetch), each cell's decals in the list's order (pass 1's "the last disc drawn wins" and the simple pass's "the first one wins")
struct MergeDecalGrid
{
	static constexpr int kWidth = 1024;
	static constexpr int kMaxCellsPerAxis = 128;
	std::vector<float> texels; ///< 4 floats a texel, rows * kWidth texels
	int rows = 0;
	float minX = 0.0f, minY = 0.0f, cellSize = 1.0f;
	int cellsX = 0, cellsY = 0;
	int cellOffset = 0, indexOffset = 0;
	size_t decals = 0;     ///< how many merge decals went in (records 0 .. decals - 1)
	int maxPerCell = 0;    ///< the longest cell list (the shader's loop)
	size_t alphaDecals = 0; ///< lane HERORING-1: how many alpha decals went in (records decals .. decals + alphaDecals - 1)
	int maxAlphaPerCell = 0; ///< lane HERORING-1: the longest alpha list of a cell
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

// the decals of `decals` whose Style has SHADOW_MERGE_DECAL into `out` and, lane HERORING-1, the alpha decals (SHADOW_ALPHA_DECAL 0x20, SHADOW_ALPHA_DECAL_DYNAMIC 0x400);
// `rect` answers a texture's atlas rectangle (false: not in the atlas, an error). False with `error` when a merge decal's texture is missing (as
// ComposeMergeDecals); an alpha decal whose texture is missing, or a decal of another style, is left out with its error in `error` (true)
// lane HERORING-1: the shaders' mode (sel_grid2.w): the merge decals' (0 none, 1 the two passes, 2 the simple pass), + 4 when there are alpha decals
int SelectionDecalShaderMode(const MergeDecalGrid &grid);
bool BuildMergeDecalGrid(const std::vector<SelectionDecal> &decals, const std::function<bool(const std::string &, MergeDecalAtlasRect &)> &rect,
	const SelectionDecalSettings &settings, MergeDecalGrid &out, std::string *error);
// the grid the shaders draw (the HUD's): BuildMergeDecalGrid, and when its merge list fails (a merge decal's texture missing: no outline that frame,
// as ComposeMergeDecals) the grid of the other decals, so a hero's ring does not go with an unrelated marker's texture (the merge error stays in
// `error`). False when not even that grid is built (`out` is then empty)
bool BuildSelectionDecalGrid(const std::vector<SelectionDecal> &decals, const std::function<bool(const std::string &, MergeDecalAtlasRect &)> &rect,
	const SelectionDecalSettings &settings, MergeDecalGrid &out, std::string *error);

// the shader's result at the world point (x, y) (selection_decals.gdshaderinc sel_apply, in C++): false where nothing is drawn; else the colour bytes
// and the alpha the terrain is covered with (255 for the two passes, the opacity for the simple pass)
bool EvaluateMergeDecalGrid(const MergeDecalGrid &grid, const DecalImage &atlas, float x, float y, std::uint8_t rgba[4]);

// lane SELWALL-1: the surfaces the merge decals are drawn on: the terrain (raised decals left out) or a wall top / bridge model (sel_merge_at's `wall`:
// the decals within kSelectionDecalMaxHeight of the surface point's height)
enum class DecalReceiver
{
	Terrain,
	Wall
};
// the shader's result at the world point (x, y, z) of `receiver` (selection_decals.gdshaderinc sel_merge_at, in C++); Terrain ignores z and is the above
bool EvaluateMergeDecalGrid(const MergeDecalGrid &grid, const DecalImage &atlas, float x, float y, float z, DecalReceiver receiver, std::uint8_t rgba[4]);

// lane HERORING-1: the surface's colour bytes `surface` (gamma space) under every selection decal at (x, y, z) of `receiver`, as the shaders draw it
// (selection_decals.gdshaderinc sel_alpha_at, then sel_merge_at): the alpha decals blended in their list order, then the merge result over them. False
// where no decal covers the point (out = surface). The merge result is blended in gamma space here; the shaders mix it in linear space: the same in the two
// passes (it covers 0 or 1), not in the simple pass (UseSimpleMergeDecals, a blended pass)
// `shadow` (optional, per channel, 1 lit .. 0 black): the object shadow on the surface; the decals are drawn over the shadowed surface, as the
// shaders split it (terrain.gdshader light(): the alpha decals over the shadowed ground, the merge decals above)
bool EvaluateSelectionDecals(const MergeDecalGrid &grid, const DecalImage &atlas, float x, float y, float z, DecalReceiver receiver,
	const std::uint8_t surface[3], std::uint8_t out[3], const float *shadow = nullptr);
