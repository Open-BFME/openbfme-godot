// OpenBFME. GPL-3.0.
//
// PlacementGhost (lane PLAY-1): what the building placement ghost looks like. RotWK's InGameUI::placeBuildAvailable (RW 0x69C5E6) makes a drawable of the
// template (RW 0x6CFE8C) and sets its model condition BUILD_PLACEMENT_CURSOR (bit 0x6C, RW 0x69C708 -> RW 0x679512) and opacity 0.45 (drawable + 0xB0 =
// RW 0xC12408). The look is the W3D model of the best ModelConditionState for that condition (W3DModelDrawModuleData::findBestInfo RW 0x4B4379) and the sub
// objects the best AnimationState's BeginScript hides / shows (findBestAnimationState RW 0x4B4443; its CurDrawableHideSubObject / CurDrawableShowSubObject
// calls). INFERENCE (stop S-3302): the script is read for those two calls, not run through the drawable Lua host; any other statement is listed in `unread`.
// Client only: it reads the template, never the logic.
#pragma once

#include <functional>
#include <string>
#include <vector>

class CastleTemplateStore;
class ThingTemplate;

namespace PlacementGhost
{
struct Look
{
	std::string model; ///< "" when no draw module has a model for BUILD_PLACEMENT_CURSOR
	std::string state; ///< the AnimationState's name ("" none)
	std::vector<std::string> hidden, shown, unread;
};

Look lookOf(const ThingTemplate &tt);

// lane PLAY-3: a castle's ghost (the owner's "no placement preview" for a builder's fortress). TARGET FACTS, the placement update RW 0x6A2AE5: when the
// template has a castle module (RW 0x73CD88: the first behaviour module whose vslot 0x14 answers true, CastleBehavior), the layout of the local player's
// faction (RW 0x798F70: CastleToUnpackForFaction) is read entry by entry from the CastleTemplates store (RW 0x72D72F); each entry whose template exists
// (RW 0x6D1305) gets a drawable of its own (RW 0x6CFE8C) with BUILD_PLACEMENT_CURSOR (bit 0x6C, RW 0x6A3740) and the opacity 0.45 (RW 0xC12408), at the
// entry's position turned by the ghost's angle about the site (RW 0x6A359E .. 0x6A3690: x' = x cos - y sin, y' = x sin + y cos), on the ground
// (TerrainLogic vslot 0x18, RW 0x6A36A8). The castle's own drawable keeps its BUILD_PLACEMENT_CURSOR look (MenFortress: Model = None, nothing).
struct Piece
{
	std::string templateName;
	Look look;
	float x = 0.0f, y = 0.0f, z = 0.0f; ///< the entry's offset from the castle centre (unrotated)
	float angle = 0.0f;                 ///< the entry's angle (INFERENCE: drawn at the ghost's angle plus this, as the unpack RW 0x798899 places it)
};
struct CastleLook
{
	bool castle = false;      ///< the template has a CastleBehavior
	std::string base;         ///< the layout's name for the side ("" none)
	std::vector<Piece> pieces;
	std::string error;        ///< why there are no pieces (no base for the side, a layout that does not load), "" otherwise
};
// `findTemplate` resolves an entry's template name (null: the entry draws nothing, as RW 0x6D1305 returning null)
CastleLook castleLookOf(const ThingTemplate &tt, const std::string &side, CastleTemplateStore &store,
	const std::function<const ThingTemplate *(const std::string &)> &findTemplate);

// lane BUILD-5: the faction bib under the ghost (the placement update RW 0x6A3AD9 / 0x6A3B0F calls TheTerrainVisual vslot 0x30 (drawable, highlight) for a refused
// site, vslot 0x34 (drawable) to remove it for a legal one: highlight 1 for every code but 9, 0 for code 9). TARGET FACTS, W3DTerrainVisual::addFactionBibDrawable
// RW 0x49249D (tier B "same-shape" with BFME2 0x492A95, decomp W3DTerrainVisualAddFactionBibDrawable.cpp): about the drawable, x from -(sx + extra) to
// sx + FactoryExitWidth + extra, y from -(sy + extra) to sy + extra, extra = FactoryExtraBibWidth (template + 0x4E4 / + 0x4E8), sx / sy = the template
// GeometryInfo's + 0x24 / + 0x28, the half extents of the box around its active shapes (RW 0xAD2860; no geometry type test); turned and placed by the
// drawable's transform (RW 0x6765B9). The textures are W3DBibBuffer's TBBib.tga and TBRedBib.tga (the highlight; RW 0xBE4368 / 0xBE4358), u 0 .. 1 along
// x, v 1 .. 0 along y (DONOR: ZH W3DBibBuffer; RotWK's buffer was not read, stop S-3302).
struct Bib
{
	bool valid = false;
	float x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f; ///< the rectangle in the ghost's own frame
	std::string error;                                 ///< why there is none ("" when valid)
};
Bib bibOf(const ThingTemplate &tt);
// the bib's texture for a legality code: "" none (legal), "TBBib.tga" for code 9, "TBRedBib.tga" for any other refusal
const char *bibTextureFor(int legalCode);
} // namespace PlacementGhost
