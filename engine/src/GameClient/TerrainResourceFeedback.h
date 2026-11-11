// OpenBFME. GPL-3.0.
//
// TerrainResourceFeedback (lanes OWNERFIX-2 and ECOCIRCLE-1/2: the owner's playtests of 2026-10-17 "retail shows, while placing and on the building, the
// overlap with other resource buildings and how effective the building will be" and of 2026-11-06 "placing economy buildings needs to show the circles at
// all times, also circles of other economy buildings and of enemy ones"): the client half of RotWK's TerrainResourceManager. Client only: it reads the
// logic's claim grid and the local player's shroud, it never writes them (the simulation half, the income share of overlapping claims, is
// TerrainResourceManager / TerrainResourceBehavior).
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
//     both axes, at the ghost drawable's position, setColor 0xFFFF8000 then the colour above and setOpacity 0x80 (neither reaches the decal: below); the
//     projected decal of type 0x2000 (RW 0x50C23A) snaps its centre down to the
//     claim grid (x - fmodf(x, cell), RW 0x492AC8) and gives every terrain vertex the alpha RW 0x75C1A9 (BFME2 rva0035A61F) computes for the local player;
//     lane PLACEDECAL-1: from the decal's diffuse 0xFFFFFFFF (RW 0x50D264): setColor / setOpacity (RW 0x73232B / 0x73228B) leave a type 0x2000 decal's
//     diffuse (their masks 0x1420 / 0x10840), so the colour and the opacity 0x80 never reach it; it is drawn on the terrain only (+ 0x64, RW 0x50C792) with
//     flushDecals' blended shader (RW 0x509A1F: ShaderClass 0x1198B3, texture x diffuse);
//   * RW 0x75B561: the disc of RW 0x75BA1D's arithmetic (centre floor((x - origin) / cell + 0.5), radius ceil(radius / cell)), each cell counted and tested
//     with RW 0x75C02D (BFME2 isCellClaimable) as (cell, HighPriority ? 0x7FFFF43 : 0, 0, force false, the local player); the share claimed / total (0 for none);
//   * RW 0x75C02D with a player >= 0 and no force: the player's cell shroud at the cell's centre ((i + 0.5) * cell + origin) SHROUDED -> no; FREE yes,
//     BLOCKED no; SHARED: fogged -> yes, else no when the player already holds an entry; OWNED: fogged -> yes, no entry -> yes, the first entry's object gone ->
//     yes, that object FOGGED / SHROUDED for the player -> yes, else no;
//   * RW 0x75C1A9 (vx, vy in map vertices of 10 units, the cell of a vertex is (v / 2)): the vertex's own cell not claimable -> alpha 0; else weight 12 minus
//     4 per unclaimable edge neighbour (x +- 1, y +- 1 halved), 2 for (+1, +1) and (-1, -1), 3 for (-1, +1) and (+1, -1), 1 per unclaimable (x +- 2, y +- 2);
//     weight <= 0 -> 0, below 12 -> alpha * weight / 12;
//   * the placement claim circles (RW 0x6A2CC1 .. 0x6A2CFC, the twin of BFME2 InGameUI::handleBuildPlacements 0x2A21E1's feedbackState == 2 branch): when
//     the pending template's resource data has MaxIncome > 0 and the manager exists, the global collector (RW 0xDE483C) is cleared (RW 0x8E9B9A), filled by
//     TerrainResourceManager::enumerateRegisteredClaimants (RW 0x75B8CC) with mask = getPlayersWithRelationship(local, 3, false) (RW 0x6A8695: 3 = the same
//     player | allies, so ENEMY claimants are not collected), then finished (RW 0x8E9B34 -> RW 0x8E99EE the arc sampler, RW 0x8E9958 the segment builder);
//   * the collector's callback (RW 0x8E9C35, slot 0 of the vtable RW 0xC790A4): an entry whose object is gone or whose Visible flag (record + 0x10) is false
//     is dropped; else the object's position (+ 0x38), the record's radius (+ 0xC) and Object::getIndicatorColor (RW 0x68B6F5) are added (RW 0x8E9BD5);
//   * the arc sampler (RW 0x8E99EE, BFME2 0x4E5372): every angle of every collected circle, at step 0.00981747732 rad from 0 to 2 pi (RW 0xBDD38C), is tested
//     against every OTHER collected circle (point = centre + (sin a, cos a) * radius, covered when its 2D distance to the other centre <= that radius); a
//     circle with no covered/free transition adds its whole range (0 .. 2 pi), else the free ranges are added (the scan runs one step past 2 pi, RW 0xC7909C);
//   * the segment builder (RW 0x8E9958 -> RW 0x8E9744, BFME2 0x4E50C8): each free range is cut into count = min(max(32 * length / circumference,
//     (int)(length / 60)), (int)(length / 20)) segments, pieceLength = length / count, step = range / count; each segment is a TerrainClaimSegment shadow
//     (RW 0xC79088, type 0x40 SHADOW_ADDITIVE_DECAL) at centre + (sin mid, cos mid) * (radius - pieceLength / 2), rotated -mid, opacity 1.0, the object's
//     indicator colour, its two size floats pieceLength (BFME2 0x4E4F82: minLength 20, maxLength 60, opacity 1, colour -1 until set);
//   * a claimant with Radius 0 (the module's default; TerrainResourceManager::claim, RW 0x75BDC2, records it unchanged): the sampler still returns the whole
//     range, but the segment builder's length / circumference is 0 / 0; on x86 the float-to-int conversion gives a non-positive count, so retail draws no
//     segments. The port skips a non-positive radius before that division (the C++ conversion would be undefined), drawing nothing as retail does;
//   * the collector is cleared when the placement ends (placeBuildAvailable null, RW 0x69C606 / 0x69C80B) and refilled at the next placement start;
//   * the draw order (RW 0x6A2CA8 .. 0x6A2D01, read in the binary): handleBuildPlacements constructs PlaceTerrainResourceClaimantFeedback (RW 0x4EAE72 ->
//     its constructor RW 0x4EADC7 ends in createPotentialClaimDecal RW 0x8EACFC) before it finishes the collector (RW 0x4E9B34 -> RW 0x4E9958 ->
//     RW 0x4E9744); the shadow factory puts a new decal before the first one of its texture, else at the head (BFME2 0x10BB11), so the TerrainClaimSegment
//     shadows (all one texture) precede the placement claim decal and the older claims in the list: RotWK draws the arcs first (they do not overlap);
//   * the selection feedback (TerrainResourceClientBehavior::createSelectionFeedback, BFME2 decomp TerrainResourceClientBehavior.cpp, slot 17): the holder
//     at InGameUI + 0x58C counts its users; while it has any, each client frame (ResourceEntryOwner RW twin of BFME2 0x4E7E3A) every claimant whose owner is the
//     local player or an ally (getPlayersWithRelationship(local, ALLOW_SAME_PLAYER | ALLOW_ALLIES = 3)) and is Visible (the claim's flag, RW twin of 0x4E81DE)
//     keeps one decal (TerrainResourceClaimFeedback::Impl::updateClaimedDecalForClaimant, BFME2 0x4E7FCA): TerrainResourceClaimDecal, type 0x20, 2 * radius,
//     colour 0xFF6464C8, opacity 0x80, at the claimant's position; a claimant FOGGED / SHROUDED for the local player has none; a claimant gone loses it.
// INFERENCE (stop S-5030): the user count follows the local selection (one user while a selected object's template lists a TerrainResourceClientBehavior;
//   retail's Drawable select path into slot 17 was not read); the device draws a claim segment as an additive decal in the selection decals' grid (lane
//   ECOCIRCLE-2: the shaders' sel_alpha_at adds texel x colour x opacity, RotWK's SHADOW_ADDITIVE_DECAL blending, without a shader varying) rather than as a
//   projected shadow; retail's collector has no shroud test (RW 0x8E9C35) but its shroud pass covers the terrain under the shadows, so the device dims a
//   segment by the cell's display level and drops it when the cell is SHROUDED; the device interpolates the vertex alphas bilinearly across a terrain quad (retail splits it into two triangles); the client form
//   never erases a dead owner from a cell (retail's draw-path cell write of RW 0x75C02D): such a cell reads as claimable, the value retail returns; the
//   port-placement branch's collector fill (RW 0x6A38C3 .. 0x6A38ED, the build port's special-power positions) is not ported (no resource-building
//   placement reaches it).

#pragma once

#include "Common/INIDataTypes.h"
#include "GameClient/SelectionDecals.h"
#include "GameLogic/ObjectTypes.h"

#include <cstddef>
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

// one angular interval of a claim circle's outline that no other collected circle covers (RW 0x8E99EE's AngleRange)
struct ResourceClaimAngle
{
	float first = 0.0f;
	float last = 0.0f;
};

// one collected claimant circle while a resource building waits for its site (RW 0xDE483C's entries)
struct ResourceClaimCircle
{
	ObjectID id = INVALID_ID;
	Coord3D position{ 0.0f, 0.0f, 0.0f };
	float radius = 0.0f;
	std::uint32_t color = 0xFF000000u; ///< Object::getIndicatorColor (RW 0x68B6F5)
	std::vector<ResourceClaimAngle> uncovered; ///< the ranges RW 0x8E99EE leaves free (a fully covered circle keeps the whole range, as retail)
};

// one TerrainClaimSegment shadow of RW 0x8E9744: pieceLength long, centred radius - pieceLength / 2 out, rotated -angle
struct ResourceClaimArc
{
	ObjectID id = INVALID_ID;
	Coord3D position{ 0.0f, 0.0f, 0.0f };
	float angle = 0.0f;  ///< mid
	float length = 0.0f; ///< pieceLength (the shadow's two size floats)
	std::uint32_t color = 0xFF000000u;
	unsigned shroudLevel = 255; ///< the local player's shroud display level at the segment's centre (0: do not draw; INFERENCE, see the header)
};

// how many decals appendDecals added of each kind (the HUD counts what the shaders' grid took of them)
struct TerrainResourceDecalCounts
{
	std::size_t place = 0;
	std::size_t claims = 0;
	std::size_t arcs = 0;
};

struct ResourcePlacementFeedback
{
	bool active = false;
	std::string templateName;
	float fraction = 0.0f;
	int percent = 0;
	std::u16string text;             ///< GUI:TerrainResourcePercentageClaimable formatted with the percent
	std::uint32_t color = 0;         ///< the band colour, ARGB (the text's; RotWK sets it on the decal too, where it has no effect: lane PLACEDECAL-1)
	int pointerX = 0, pointerY = 0;  ///< the pointer in window pixels (updateStringPos: the text's centre x, its bottom 20 pixels above)
	Coord3D decalCenter{ 0.0f, 0.0f, 0.0f }; ///< the ghost's position snapped down to the claim grid (type 0x2000)
	float decalSize = 0.0f;          ///< 2 * Radius
	int decalOpacity = 0x80;         ///< the setOpacity RotWK calls (RW 0x73228B), which leaves a type 0x2000 decal as it is (lane PLACEDECAL-1)
	// the alpha (0 .. 255) of every map vertex the decal covers: maskW x maskH vertices from (maskVx0, maskVy0), 10 units apart (RW 0x75C1A9 from the decal's
	// diffuse 0xFFFFFFFF: lane PLACEDECAL-1)
	int maskVx0 = 0, maskVy0 = 0, maskW = 0, maskH = 0;
	std::vector<std::uint8_t> maskAlpha;
	unsigned version = 0;            ///< bumped when the mask or the colour changed
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
	const std::vector<ResourceClaimCircle> &claimCircles() const { return m_claimCircles; } ///< the placement's collected claimants (empty: no placement)
	const std::vector<ResourceClaimArc> &claimArcs() const { return m_claimArcs; } ///< the segments the device draws
	// lane PLACEDECAL-1: the placement claim decal and the claims, in RotWK's draw order, as the selection decals' alpha decals (the shaders' grid,
	// BuildMergeDecalGrid); lane ECOCIRCLE-2: the placement's claim arcs (TerrainClaimSegment.dds, SHADOW_ADDITIVE_DECAL) first: they are the newest of
	// RotWK's addDecal list
	TerrainResourceDecalCounts appendDecals(const std::string &claimTexture, const std::string &placeTexture, std::vector<SelectionDecal> &out) const;
	const ResourcePlacementFeedback &placement() const { return m_placement; }
	static bool hasClientBehavior(const ThingTemplate &tt);
	static std::string stopLine(); ///< "[S-5030] ..." (the HUD's stops)
	static constexpr float kVertexSpacing = 10.0f; ///< MAP_XY_FACTOR (RW 0xBD83D4 = 0.1 the inverse)
	static constexpr const char *kClaimArcTexture = "TerrainClaimSegment.dds"; ///< the shadow's texture (RW 0xC79088)
	// RW 0x8E9744's segment count for one free range of a circle of `circumference`: 0 when the circle has no positive circumference (a Radius-0 claim's
	// length / circumference is 0 / 0; retail's float-to-int conversion then gives no positive count, so it draws nothing, and C++'s is undefined)
	static int claimSegmentCount(float length, float circumference);

private:
	void buildClaimCircles(const GameLogic &logic, int localPlayer);
	void buildClaimArcs(const GameLogic &logic, int localPlayer);

	std::vector<ResourceClaimDecal> m_claimed;
	ResourcePlacementFeedback m_placement;
	std::vector<std::int8_t> m_claimMemo; ///< lane FOGFX-1: updatePlacement's per-cell answers of RW 0x75C02D (-1 not asked yet)
	std::vector<ResourceClaimCircle> m_claimCircles; ///< the collector of RW 0xDE483C (own / allied Visible claimants only)
	std::vector<ResourceClaimArc> m_claimArcs;       ///< RW 0x8E9744's TerrainClaimSegment shadows, local player's shroud applied
	std::uint32_t m_circleSignature = 0;             ///< the claimant set the circles were built from (0: none)
};
