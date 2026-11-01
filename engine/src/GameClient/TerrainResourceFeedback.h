// OpenBFME. GPL-3.0.
//
// TerrainResourceFeedback (lane OWNERFIX-2, the owner's playtest of 2026-10-17: "retail shows, while placing and on the building, the overlap with other
// resource buildings and how effective the building will be"): the client half of RotWK's TerrainResourceManager. Client only: it reads the logic's claim
// grid and the local player's shroud, it never writes them (the simulation half, the income share of overlapping claims, is TerrainResourceManager /
// TerrainResourceBehavior).
//
// TARGET FACTS (rotwk201_game.exe, caveat S-001; the BFME2 twins are tier A with byte-matched decomp C++ unless marked):
//   * the placement feedback (InGameUI::handleBuildPlacements, BFME2 decomp InGameUIHandleBuildPlacements.cpp): when a building starts waiting for its site
//     and its template has a TerrainResourceBehavior (the first behaviour module answering resourceData(), rva0029B9E5) with MaxIncome > 0, a
//     PlaceTerrainResourceClaimantFeedback is made for the ghost drawable (InGameUI + 0x574 the params: PlaceTerrainResourceClaimantDecal, Font, FontColor);
//   * its update each client frame (RW twin of BFME2 0x4E6455, decomp Rva004E65CFLeaf.cpp Impl::update): fraction = TerrainResourceManager RW 0x75B561
//     (BFME2 rva00359A0C) at the ghost's position, the module's Radius and HighPriority, the local player's index; the text is "GUI:TerrainResourcePercentage
//     Claimable" formatted with floor(fraction * 100 + 0.5) (WWMath::Float_To_Long), its colour (and the decal's) 0xFF00FF00 above 0.75, 0xFFFFFF00 from 0.5,
//     0xFFFF0000 from 0.25, else 0xFF000000; the text sits centred on the pointer's x, its height + 20 above the pointer, clamped into the view (updateStringPos);
//   * the placement decal (createPotentialClaimDecal, RW twin of BFME2 0x4E65CF): PlaceTerrainResourceClaimantDecal as a shadow of type 0x2000, 2 * Radius on
//     both axes, at the ghost drawable's position, opacity 0x80, the colour above; the projected decal of type 0x2000 (RW 0x50C23A) snaps its centre down to the
//     claim grid (x - fmodf(x, cell), RW 0x492AC8) and gives every terrain vertex the alpha RW 0x75C1A9 (BFME2 rva0035A61F) computes for the local player;
//   * RW 0x75B561: the disc of RW 0x75BA1D's arithmetic (centre floor((x - origin) / cell + 0.5), radius ceil(radius / cell)), each cell counted and tested
//     with RW 0x75C02D (BFME2 isCellClaimable) as (cell, HighPriority ? 0x7FFFF43 : 0, 0, force false, the local player); the share claimed / total (0 for none);
//   * RW 0x75C02D with a player >= 0 and no force: the player's cell shroud at the cell's centre ((i + 0.5) * cell + origin) SHROUDED -> no; FREE yes,
//     BLOCKED no; SHARED: fogged -> yes, else no when the player already holds an entry; OWNED: fogged -> yes, no entry -> yes, the first entry's object gone ->
//     yes, that object FOGGED / SHROUDED for the player -> yes, else no;
//   * RW 0x75C1A9 (vx, vy in map vertices of 10 units, the cell of a vertex is (v / 2)): the vertex's own cell not claimable -> alpha 0; else weight 12 minus
//     4 per unclaimable edge neighbour (x +- 1, y +- 1 halved), 2 for (+1, +1) and (-1, -1), 3 for (-1, +1) and (+1, -1), 1 per unclaimable (x +- 2, y +- 2);
//     weight <= 0 -> 0, below 12 -> alpha * weight / 12;
//   * the selection feedback (TerrainResourceClientBehavior::createSelectionFeedback, BFME2 decomp TerrainResourceClientBehavior.cpp, slot 17): the holder
//     at InGameUI + 0x58C counts its users; while it has any, each client frame (ResourceEntryOwner RW twin of BFME2 0x4E7E3A) every claimant whose owner is the
//     local player or an ally (getPlayersWithRelationship(local, ALLOW_SAME_PLAYER | ALLOW_ALLIES = 3)) and is Visible (the claim's flag, RW twin of 0x4E81DE)
//     keeps one decal (TerrainResourceClaimFeedback::Impl::updateClaimedDecalForClaimant, BFME2 0x4E7FCA): TerrainResourceClaimDecal, type 0x20, 2 * radius,
//     colour 0xFF6464C8, opacity 0x80, at the claimant's position; a claimant FOGGED / SHROUDED for the local player has none; a claimant gone loses it.
// INFERENCE (stop S-5030): the user count follows the local selection (one user while a selected object's template lists a TerrainResourceClientBehavior;
//   retail's Drawable select path into slot 17 was not read); the collector's outline arcs of the own / allied claim circles (RW twin of BFME2 0x4E5372,
//   W3D "cloud arcs") are not drawn; the device interpolates the vertex alphas bilinearly across a terrain quad (retail splits it into two triangles); the
//   client form never erases a dead owner from a cell (retail's draw-path cell write of RW 0x75C02D): such a cell reads as claimable, the value retail returns.

#pragma once

#include "Common/INIDataTypes.h"
#include "GameLogic/ObjectTypes.h"

#include <cstdint>
#include <string>
#include <vector>

class GameLogic;
class ThingTemplate;
class TerrainResourceBehaviorModuleData;
struct TerrainResourceUiSettings;

namespace TerrainResourceClaimView
{
// RW 0x75C02D's client form (see above); `player` < 0 skips the shroud as retail does
bool claimable(const GameLogic &logic, int x, int y, std::uint32_t mask, ObjectID ownId, bool force, int player);
// RW 0x75B561 (BFME2 rva00359A0C)
float claimableFraction(const GameLogic &logic, float x, float y, float radius, bool highPriority, int player);
// RW 0x75C1A9 (BFME2 rva0035A61F): `color` ARGB of the decal at a vertex, returned with its alpha weighted
std::uint32_t vertexColor(const GameLogic &logic, std::uint32_t color, int vx, int vy, int player);
// rva0029B9E5: the first behaviour module data of the template that is a TerrainResourceBehavior's, or null
const TerrainResourceBehaviorModuleData *resourceDataOf(const ThingTemplate &tt);
// the colour band of a claimable fraction (Impl::update)
std::uint32_t bandColor(float fraction);
} // namespace TerrainResourceClaimView

struct ResourceClaimDecal
{
	ObjectID id = INVALID_ID;
	Coord3D position{ 0.0f, 0.0f, 0.0f };
	float size = 0.0f;               ///< 2 * radius
	std::uint32_t color = 0xFF6464C8u;
	int opacity = 0x80;
};

struct ResourcePlacementFeedback
{
	bool active = false;
	std::string templateName;
	float fraction = 0.0f;
	int percent = 0;
	std::u16string text;             ///< GUI:TerrainResourcePercentageClaimable formatted with the percent
	std::uint32_t color = 0;         ///< the band colour, ARGB (the text and the decal)
	int pointerX = 0, pointerY = 0;  ///< the pointer in window pixels (updateStringPos: the text's centre x, its bottom 20 pixels above)
	Coord3D decalCenter{ 0.0f, 0.0f, 0.0f }; ///< the ghost's position snapped down to the claim grid (type 0x2000)
	float decalSize = 0.0f;          ///< 2 * Radius
	int decalOpacity = 0x80;
	// the alpha (0 .. 255) of every map vertex the decal covers: maskW x maskH vertices from (maskVx0, maskVy0), 10 units apart (RW 0x75C1A9 with the decal's
	// opacity as the colour's alpha)
	int maskVx0 = 0, maskVy0 = 0, maskW = 0, maskH = 0;
	std::vector<std::uint8_t> maskAlpha;
	unsigned version = 0;            ///< bumped when the mask or the colour changed (the device rebuilds its texture then)
};

class TerrainResourceFeedback
{
public:
	// `selectionUsers`: the selected objects of the local player whose template lists a TerrainResourceClientBehavior (S-5030)
	void updateSelection(const GameLogic &logic, int localPlayer, int selectionUsers);
	// `pending` the template waiting for its site (null: none), `ghost` its drawable's position, `format` the GUI:TerrainResourcePercentageClaimable text
	void updatePlacement(const GameLogic &logic, int localPlayer, const ThingTemplate *pending, const Coord3D &ghost, const std::u16string &format,
		int pointerX = 0, int pointerY = 0);
	const std::vector<ResourceClaimDecal> &claimedDecals() const { return m_claimed; }
	const ResourcePlacementFeedback &placement() const { return m_placement; }
	static bool hasClientBehavior(const ThingTemplate &tt);
	static std::string stopLine(); ///< "[S-5030] ..." (the HUD's stops)
	static constexpr float kVertexSpacing = 10.0f; ///< MAP_XY_FACTOR (RW 0xBD83D4 = 0.1 the inverse)

private:
	std::vector<ResourceClaimDecal> m_claimed;
	ResourcePlacementFeedback m_placement;
	std::vector<std::int8_t> m_claimMemo; ///< lane FOGFX-1: updatePlacement's per-cell answers of RW 0x75C02D (-1 not asked yet)
};
