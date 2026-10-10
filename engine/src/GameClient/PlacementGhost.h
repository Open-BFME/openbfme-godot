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
} // namespace PlacementGhost
