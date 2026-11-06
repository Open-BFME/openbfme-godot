// OpenBFME. GPL-3.0.
// See GameClient/TerrainResourceFeedback.h for the target facts and the addresses.

#include "GameClient/TerrainResourceFeedback.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Team.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameClient/UnitHelpSource.h"
#include "GameLogic/Economy.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/TerrainResourceBehavior.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/System/ShroudManager.h"
#include "GameLogic/System/TerrainResourceManager.h"

#include <cmath>
#include <cstring>
#include <string>

namespace
{
CellShroudStatus cellShroud(const GameLogic &logic, int player, float x, float y)
{
	const ShroudManager *s = logic.shroud();
	return s ? s->getStatusAt(player, x, y) : CELLSHROUD_CLEAR; // a game without a shroud manager (a bare map) sees everything
}

ObjectShroudStatus objectShroud(const GameLogic &logic, const Object &obj, int player)
{
	const ShroudManager *s = logic.shroud();
	return s ? s->clientObjectStatus(obj, player) : OBJECTSHROUD_CLEAR;
}

// the claim disc of RW 0x75BA1D / 0x75B561 (BFME2 rva00359A0C: floor((x - origin) / cell + 0.5), ceil(radius / cell)), then the midpoint circle of RW 0x75B4BA
// (BFME2 rva0035997F: the rows from the middle out)
template <class Visit>
void forEachInDisc(const TerrainResourceManager &grid, float x, float y, float radius, Visit &&visit)
{
	const int cx = (int)std::floor((x - grid.originX()) / grid.cellSize() + 0.5f);
	const int cy = (int)std::floor((y - grid.originY()) / grid.cellSize() + 0.5f);
	const int r = (int)std::ceil(radius / grid.cellSize());
	auto row = [&](int a, int b, int yy) {
		for (int i = a; i <= b; ++i)
		{
			visit(i, yy);
		}
	};
	int xi = 0;
	int yi = r;
	int e = 2 * (1 - r);
	for (;;)
	{
		if (e + yi > 0)
		{
			if (yi == 0 && r == 1)
			{
				++xi;
			}
			row(cx - xi, cx + xi, cy + yi);
			if (yi == 0)
			{
				return;
			}
			row(cx - xi, cx + xi, cy - yi);
			--yi;
			e -= 2 * yi - 1;
		}
		if (xi > e)
		{
			++xi;
			e += 2 * xi + 1;
		}
	}
}

// Object::getIndicatorColor (RW 0x68B6F5): the team's controlling player's colour (the custom indicator and the disguise colour are S-2522 / S-2456's)
std::uint32_t indicatorColorOf(const Object &obj)
{
	const Team *team = obj.getTeam();
	const Player *owner = team ? team->getControllingPlayer() : nullptr;
	return owner ? (0xFF000000u | (owner->getPlayerColor() & 0x00FFFFFFu)) : 0xFF000000u;
}

bool alliedOrSelf(const GameLogic &logic, const Player *owner, int localPlayer)
{
	// getPlayersWithRelationship(local, ALLOW_SAME_PLAYER | ALLOW_ALLIES): the mask bit of the claimant's controlling player (ZH PlayerList.cpp)
	const Player *local = localPlayer >= 0 ? logic.players().getNthPlayer(localPlayer) : nullptr;
	if (!owner || !local)
	{
		return false;
	}
	if (owner == local)
	{
		return true;
	}
	const Relationship rel = owner->getDefaultTeam() ? local->getRelationship(owner->getDefaultTeam()) : local->getRelationship(owner);
	return rel == ALLIES;
}

std::uint32_t floatBits(float f)
{
	std::uint32_t bits = 0;
	std::memcpy(&bits, &f, sizeof(bits));
	return bits;
}

void mixCircleSignature(std::uint32_t &hash, std::uint32_t v)
{
	hash = (hash ^ v) * 16777619u; // FNV-1a; only compared with itself, never hashed state
}

// RW 0x8E951F (BFME2 0x4E4EA3): the point at `angle` on `circle` is inside another collected circle (2D distance <= its radius)
bool coveredByAnother(const std::vector<ResourceClaimCircle> &circles, std::size_t index, float angle)
{
	const ResourceClaimCircle &c = circles[index];
	const float x = c.position.x + std::sin(angle) * c.radius;
	const float y = c.position.y + std::cos(angle) * c.radius;
	for (std::size_t j = 0; j < circles.size(); ++j)
	{
		if (j == index)
		{
			continue;
		}
		const float dx = circles[j].position.x - x;
		const float dy = circles[j].position.y - y;
		if (std::sqrt(dx * dx + dy * dy) <= circles[j].radius)
		{
			return true;
		}
	}
	return false;
}

// RW 0x8E99EE (BFME2 0x4E5372): the free angular ranges of every circle, at retail's sampling step
void computeUncoveredRanges(std::vector<ResourceClaimCircle> &circles)
{
	const float step = 0.00981747732f; // RW 0xC790A0
	const float turn = 6.28318548f;    // RW 0xBDD38C
	for (std::size_t i = 0; i < circles.size(); ++i)
	{
		ResourceClaimCircle &c = circles[i];
		c.uncovered.clear();
		bool covered = false;
		bool noTransition = true;
		float first = 0.0f;
		while (first <= turn)
		{
			const bool hit = coveredByAnother(circles, i, first);
			if (!covered)
			{
				if (hit)
				{
					covered = true;
				}
			}
			else if (!hit)
			{
				noTransition = false;
				break;
			}
			first += step;
		}
		if (noTransition)
		{
			// RW 0x8E99EE: no covered/free transition at all (the whole circle free, and retail's whole-circle answer too when it is fully covered)
			c.uncovered.push_back(ResourceClaimAngle{ 0.0f, turn });
			continue;
		}
		float angle = first;
		float start = first;
		covered = false;
		const float end = first + 6.29300308f; // RW 0xC7909C: one step past 2 pi closes the last free range
		for (; angle <= end; angle += step)
		{
			const bool hit = coveredByAnother(circles, i, angle);
			if (!covered)
			{
				if (hit)
				{
					covered = true;
					c.uncovered.push_back(ResourceClaimAngle{ start, angle });
				}
			}
			else if (!hit)
			{
				start = angle;
				covered = false;
			}
		}
	}
}
} // namespace

bool TerrainResourceClaimView::claimable(const GameLogic &logic, int x, int y, std::uint32_t mask, ObjectID ownId, bool force, int player)
{
	// RW 0x75C02D (BFME2 decomp TerrainResourceManagerCellClaim.cpp TerrainResourceManager::isCellClaimable)
	const TerrainResourceManager &grid = logic.economy().resources();
	const TerrainResourceManager::Cell *cell = grid.cellAt(x, y);
	if (!cell)
	{
		return false;
	}
	CellShroudStatus shroud = CELLSHROUD_CLEAR;
	if (!force && player >= 0)
	{
		shroud = cellShroud(logic, player, ((float)x + 0.5f) * grid.cellSize() + grid.originX(), ((float)y + 0.5f) * grid.cellSize() + grid.originY());
		if (shroud == CELLSHROUD_SHROUDED)
		{
			return false;
		}
	}
	switch (cell->state)
	{
	case TerrainResourceManager::CELL_FREE:
		return true;
	case TerrainResourceManager::CELL_BLOCKED:
		return false;
	case TerrainResourceManager::CELL_SHARED:
		if (force || player == -1 || shroud != CELLSHROUD_CLEAR)
		{
			return true;
		}
		for (const TerrainResourceManager::ClaimEntry &e : cell->claims)
		{
			if (e.playerIndex == player)
			{
				return false;
			}
		}
		return true;
	case TerrainResourceManager::CELL_OWNED:
	{
		if (!force && shroud != CELLSHROUD_CLEAR)
		{
			return true;
		}
		if (cell->claims.empty())
		{
			return true; // retail resets the cell to free here (a draw-path write): read as free
		}
		const TerrainResourceManager::ClaimEntry &first = cell->claims.front();
		if (player >= 0)
		{
			const Object *obj = logic.findObjectByID(first.objectId);
			if (!obj)
			{
				return true; // retail erases the entries and frees the cell (a draw-path write): read as free
			}
			const ObjectShroudStatus st = objectShroud(logic, *obj, player);
			if (st == OBJECTSHROUD_FOGGED || st == OBJECTSHROUD_SHROUDED)
			{
				return true;
			}
		}
		return force && (first.playerIndex == player || mask == TerrainResourceManager::kHighPriorityMask || mask == first.objectId || ownId == first.objectId);
	}
	default:
		return false;
	}
}

float TerrainResourceClaimView::claimableFraction(const GameLogic &logic, float x, float y, float radius, bool highPriority, int player)
{
	// RW 0x75B561 (BFME2 rva00359A0C) with the visitor RW twin of BFME2 0x35A994: every cell counted, the claimable ones counted again; (float) claimed / total
	const TerrainResourceManager &grid = logic.economy().resources();
	if (!grid.hasGrid() || !(grid.cellSize() > 0.0f))
	{
		return 0.0f;
	}
	const std::uint32_t mask = highPriority ? TerrainResourceManager::kHighPriorityMask : 0u;
	int total = 0, claimed = 0;
	forEachInDisc(grid, x, y, radius, [&](int cx, int cy) {
		++total;
		if (claimable(logic, cx, cy, mask, INVALID_ID, false, player))
		{
			++claimed;
		}
	});
	return total == 0 ? 0.0f : (float)claimed / (float)total;
}

namespace
{
// RW 0x75C1A9's weighting of the cells around vertex (x, y); `ok(cx, cy)`: RW 0x75C02D's isCellClaimable(cell, 0, 0, false, player)
template <class Ok> std::uint32_t weightedVertexColor(std::uint32_t color, int x, int y, Ok &&ok)
{
	if (!ok(x / 2, y / 2))
	{
		return 0u;
	}
	float weight = 12.0f;
	weight -= ok((x + 1) / 2, y / 2) ? 0.0f : 4.0f;
	weight -= ok(x / 2, (y + 1) / 2) ? 0.0f : 4.0f;
	weight -= ok((x - 1) / 2, y / 2) ? 0.0f : 4.0f;
	weight -= ok(x / 2, (y - 1) / 2) ? 0.0f : 4.0f;
	weight -= ok((x + 1) / 2, (y + 1) / 2) ? 0.0f : 2.0f;
	weight -= ok((x - 1) / 2, (y - 1) / 2) ? 0.0f : 2.0f;
	weight -= ok((x - 1) / 2, (y + 1) / 2) ? 0.0f : 3.0f;
	weight -= ok((x + 1) / 2, (y - 1) / 2) ? 0.0f : 3.0f;
	weight -= ok((x + 2) / 2, y / 2) ? 0.0f : 1.0f;
	weight -= ok((x - 2) / 2, y / 2) ? 0.0f : 1.0f;
	weight -= ok(x / 2, (y + 2) / 2) ? 0.0f : 1.0f;
	weight -= ok(x / 2, (y - 2) / 2) ? 0.0f : 1.0f;
	if (weight <= 0.0f)
	{
		return 0u;
	}
	if (weight < 12.0f)
	{
		const float alpha = (float)(color >> 24);
		return (color & 0x00FFFFFFu) | ((std::uint32_t)(int)(alpha * (weight / 12.0f)) << 24);
	}
	return color;
}
} // namespace

std::uint32_t TerrainResourceClaimView::vertexColor(const GameLogic &logic, std::uint32_t color, int x, int y, int player)
{
	// RW 0x75C1A9 (BFME2 decomp TerrainResourceManager.cpp rva0035A61F): isCellClaimable(cell, 0, 0, false, player) around the vertex
	return weightedVertexColor(color, x, y, [&](int cx, int cy) { return claimable(logic, cx, cy, 0u, INVALID_ID, false, player); });
}

const TerrainResourceBehaviorModuleData *TerrainResourceClaimView::resourceDataOf(const ThingTemplate &tt)
{
	// rva0029B9E5 (BFME2 decomp InGameUIHandleBuildPlacements.cpp): the behaviour modules in order, the first whose data is a TerrainResourceBehavior's
	for (const ThingTemplate::Nugget &n : tt.moduleList(MODULETYPE_BEHAVIOR).nuggets())
	{
		if (const auto *d = dynamic_cast<const TerrainResourceBehaviorModuleData *>(n.data.get()))
		{
			return d;
		}
	}
	return nullptr;
}

std::uint32_t TerrainResourceClaimView::bandColor(float fraction)
{
	// Impl::update (RW twin of BFME2 0x4E6455): > 0.75 green, >= 0.5 yellow, >= 0.25 red, else black; alpha 255
	if (fraction > 0.75f)
	{
		return 0xFF00FF00u;
	}
	if (fraction >= 0.5f)
	{
		return 0xFFFFFF00u;
	}
	if (fraction >= 0.25f)
	{
		return 0xFFFF0000u;
	}
	return 0xFF000000u;
}

std::string TerrainResourceFeedback::stopLine()
{
	return "[S-5030] resource claim feedback (lanes OWNERFIX-2, ECOCIRCLE-1/2): the placement's claimable percentage, colour and masked decal (RW 0x75B561 / "
		   "0x75C02D / 0x75C1A9), the selected resource building's claim decals (TerrainResourceClaimFeedback) and the placement's claim circles (RW 0x6A2CC1 .. "
		   "0x6A2CFC with the collector RW 0xDE483C: mask 3 = the local player and allies only, the arcs RW 0x8E99EE and the TerrainClaimSegment shadows RW "
		   "0x8E9744) are ported; INFERENCE: the claim decals show while a selected object of the local player lists a TerrainResourceClientBehavior (retail's "
		   "select path into slot 17 not read), the claim segments are additive decals in the selection decals' grid (SHADOW_ADDITIVE_DECAL's texel x colour "
		   "x alpha added, not a projected shadow; they are the newest of RotWK's addDecal list, so they are drawn first), shrouded segments are dropped and "
		   "fogged ones dimmed by the shroud display level, the vertex alphas are bilinear across a terrain quad, the text uses the fallback font at the INI's size";
}

bool TerrainResourceFeedback::hasClientBehavior(const ThingTemplate &tt)
{
	for (const ThingTemplate::Nugget &n : tt.moduleList(MODULETYPE_CLIENT_BEHAVIOR).nuggets())
	{
		if (n.name == "TerrainResourceClientBehavior")
		{
			return true;
		}
	}
	return false;
}

void TerrainResourceFeedback::buildClaimCircles(const GameLogic &logic, int localPlayer)
{
	std::vector<ResourceClaimCircle> next;
	const TerrainResourceManager &grid = logic.economy().resources();
	if (localPlayer >= 0 && grid.hasGrid())
	{
		// RW 0x75B8CC over the manager's +0x14 list with the mask RW 0x6A8695 returns for (local, 3, false), then RW 0x8E9C35 for each record:
		// the object must exist, the Visible flag (record + 0x10) must be set and the owner must be the local player or an ally
		for (const TerrainResourceManager::Claimant &c : grid.claimants())
		{
			if (!c.visible)
			{
				continue;
			}
			const Object *obj = logic.findObjectByID(c.id);
			if (!obj || !alliedOrSelf(logic, obj->getControllingPlayer(), localPlayer))
			{
				continue;
			}
			ResourceClaimCircle circle;
			circle.id = c.id;
			circle.position = *obj->getPosition();
			circle.radius = c.radius;
			circle.color = indicatorColorOf(*obj);
			next.push_back(std::move(circle));
		}
	}
	std::uint32_t signature = 2166136261u;
	mixCircleSignature(signature, (std::uint32_t)localPlayer);
	mixCircleSignature(signature, (std::uint32_t)next.size());
	for (const ResourceClaimCircle &c : next)
	{
		mixCircleSignature(signature, (std::uint32_t)c.id);
		mixCircleSignature(signature, floatBits(c.position.x));
		mixCircleSignature(signature, floatBits(c.position.y));
		mixCircleSignature(signature, floatBits(c.radius));
		mixCircleSignature(signature, c.color);
	}
	if (signature != m_circleSignature || next.size() != m_claimCircles.size())
	{
		computeUncoveredRanges(next); // RW 0x8E99EE: only the claimant set (the collector's contents) changes the arcs
		m_circleSignature = signature;
	}
	else
	{
		for (std::size_t i = 0; i < next.size(); ++i)
		{
			next[i].uncovered = std::move(m_claimCircles[i].uncovered);
		}
	}
	m_claimCircles = std::move(next);
}

void TerrainResourceFeedback::buildClaimArcs(const GameLogic &logic, int localPlayer)
{
	m_claimArcs.clear();
	if (localPlayer < 0)
	{
		return;
	}
	const ShroudManager *shroud = logic.shroud();
	for (const ResourceClaimCircle &c : m_claimCircles)
	{
		const float circumference = c.radius * 6.28318548f; // RW 0xBDD38C
		for (const ResourceClaimAngle &range : c.uncovered)
		{
			// RW 0x8E9744 (BFME2 0x4E50C8): count = min(max(32 * length / circumference, (int)(length / 60)), (int)(length / 20))
			const float difference = range.last - range.first;
			const float length = circumference * (difference * 0.159154937f); // RW 0xC36090
			int count = (int)(length / circumference * 32.0f);               // RW 0xC18DE0
			const int byMaximum = (int)(length / 60.0f);                      // RW 0xBDC1F8
			if (count < byMaximum)
			{
				count = byMaximum;
			}
			const int byMinimum = (int)(length / 20.0f); // RW 0xBDBC6C
			if (count > byMinimum)
			{
				count = byMinimum;
			}
			if (count <= 0)
			{
				continue;
			}
			const float pieceLength = length / (float)count;
			const float step = difference / (float)count;
			for (float angle = range.first; angle < range.last - step * 0.100000001f; angle += step)  // RW 0xBD83D4
			{
				const float mid = angle + step * 0.5f; // RW 0xBD869C
				const float localRadius = c.radius - pieceLength * 0.5f;
				ResourceClaimArc arc;
				arc.id = c.id;
				arc.position = c.position;
				arc.position.x = c.position.x + std::sin(mid) * localRadius;
				arc.position.y = c.position.y + std::cos(mid) * localRadius;
				arc.angle = mid;
				arc.length = pieceLength;
				arc.color = c.color;
				if (shroud)
				{
					// INFERENCE (S-5030): retail's collector has no shroud test (RW 0x8E9C35); its shroud pass covers the terrain under the shadow
					const CellShroudStatus st = shroud->getStatusAt(localPlayer, arc.position.x, arc.position.y);
					arc.shroudLevel = shroud->displayLevel(st);
				}
				if (arc.shroudLevel == 0)
				{
					continue;
				}
				m_claimArcs.push_back(arc);
			}
		}
	}
}

void TerrainResourceFeedback::updateSelection(const GameLogic &logic, int localPlayer, int selectionUsers)
{
	m_claimed.clear();
	if (selectionUsers <= 0)
	{
		return; // ResourceEntryOwner::rva004E7CEF: no user left, every decal goes
	}
	// ResourceEntryOwner RW twin of BFME2 0x4E7E3A: the claimants in the manager's order (front = high priority)
	for (const TerrainResourceManager::Claimant &c : logic.economy().resources().claimants())
	{
		if (!c.visible)
		{
			continue; // RW twin of BFME2 0x4E81DE: only a Visible claim
		}
		const Object *obj = logic.findObjectByID(c.id);
		if (!obj || !alliedOrSelf(logic, obj->getControllingPlayer(), localPlayer))
		{
			continue;
		}
		const ObjectShroudStatus st = objectShroud(logic, *obj, localPlayer >= 0 ? localPlayer : 0);
		if (st > OBJECTSHROUD_PARTIAL_CLEAR)
		{
			continue; // updateClaimedDecalForClaimant: fogged / shrouded for the local player: no decal
		}
		ResourceClaimDecal d;
		d.id = c.id;
		d.position = *obj->getPosition();
		d.size = c.radius * 2.0f;
		m_claimed.push_back(d);
	}
}

TerrainResourceDecalCounts TerrainResourceFeedback::appendDecals(const std::string &claimTexture, const std::string &placeTexture, std::vector<SelectionDecal> &out) const
{
	// lane PLACEDECAL-1: RotWK's addDecal decals (list + 8, RW 0x50BA57), in its draw order (the shadow factory, BFME2 0x10BB11, puts a new decal before
	// the first one of its texture, else at the head: the newest first): the placement claim decal (RW 0x8EACFC: SHADOW_DECAL_TERRAIN_CLAIM, the diffuse
	// white, its alpha weighted per vertex; made as the placement starts), then the claims (INFERENCE S-2522: made before the placement started; a claimant
	// that shows up during it gets its decal then, RotWK draws that one first) (RW 0x8EC81A .. 0x8EC899: SHADOW_ALPHA_DECAL, setColor
	// 0xFF6464C8, + 0x64 = 1, setOpacity 0x80: the diffuse 0x806464C8), both on the terrain only (+ 0x64).
	// Lane ECOCIRCLE-2: the claim arcs are newer still, so RotWK draws them first: RW 0x6A2CA8 constructs PlaceTerrainResourceClaimantFeedback (whose
	// constructor RW 0x4EADC7 ends in createPotentialClaimDecal, RW 0x8EACFC) and only then RW 0x6A2CC1 .. 0x6A2D01 finishes the collector (RW 0x4E9B34
	// -> RW 0x4E9958 -> RW 0x4E9744), whose TerrainClaimSegment shadows enter the same addDecal list (RW 0x8E98AE, [0xDD1D14] vslot + 8) as the placement
	// decal (RW 0x8EAD58): they are inserted at the head, ahead of the placement decal the constructor just made. The segments all share one texture and do
	// not overlap each other, so their own order is moot.
	TerrainResourceDecalCounts counts;
	for (const ResourceClaimArc &a : m_claimArcs)
	{
		SelectionDecal d;
		d.object = a.id;
		d.position = a.position;
		d.size = a.length;
		d.angle = -a.angle; // the shadow's normalizeAngle(-mid) RW 0x8E98C8: the grid's u axis is (cos angle, sin angle), so -mid puts it on the tangent
		d.texture = kClaimArcTexture;
		d.style = 0x40u; // SHADOW_ADDITIVE_DECAL
		d.color = ((std::uint32_t)a.shroudLevel << 24) | (a.color & 0xFFFFFFu); // opacity 1.0 (RW 0x33228B), dimmed by the shroud's display level
		d.terrainOnly = true;
		out.push_back(std::move(d));
		++counts.arcs;
	}
	const ResourcePlacementFeedback &p = m_placement;
	if (p.active && p.maskW > 0 && p.maskH > 0 && p.decalSize > 0.0f)
	{
		SelectionDecal d;
		d.position = p.decalCenter;
		d.size = p.decalSize;
		d.texture = placeTexture;
		d.style = 0x2000u;
		d.color = 0xFFFFFFFFu;
		d.terrainOnly = true;
		d.maskVx0 = p.maskVx0;
		d.maskVy0 = p.maskVy0;
		d.maskW = p.maskW;
		d.maskH = p.maskH;
		d.maskAlpha = p.maskAlpha;
		out.push_back(std::move(d));
		++counts.place;
	}
	for (const ResourceClaimDecal &c : m_claimed)
	{
		SelectionDecal d;
		d.object = c.id;
		d.position = c.position;
		d.size = c.size;
		d.texture = claimTexture;
		d.style = 0x20u;
		d.color = ((std::uint32_t)c.opacity << 24) | (c.color & 0xFFFFFFu);
		d.terrainOnly = true;
		out.push_back(std::move(d));
		++counts.claims;
	}
	return counts;
}

void TerrainResourceFeedback::updatePlacement(const GameLogic &logic, int localPlayer, const ThingTemplate *pending, const Coord3D &ghost,
	const std::u16string &format, int pointerX, int pointerY)
{
	const TerrainResourceBehaviorModuleData *data = pending ? TerrainResourceClaimView::resourceDataOf(*pending) : nullptr;
	const TerrainResourceManager &grid = logic.economy().resources();
	if (!data || data->m_maxIncome <= 0 || !grid.hasGrid())
	{
		if (m_placement.active)
		{
			const unsigned v = m_placement.version + 1;
			m_placement = ResourcePlacementFeedback();
			m_placement.version = v;
		}
		if (!m_claimCircles.empty() || !m_claimArcs.empty())
		{
			m_claimCircles.clear(); // placeBuildAvailable(null): RW 0x69C606 / 0x69C80B clears the collector
			m_claimArcs.clear();
			m_circleSignature = 0;
		}
		return;
	}
	// the collector of the placement start (RW 0x6A2CC1 .. 0x6A2CFC): the own / allied Visible claimants stay while the ghost waits for its site
	buildClaimCircles(logic, localPlayer);
	buildClaimArcs(logic, localPlayer);
	ResourcePlacementFeedback f;
	f.active = true;
	f.templateName = pending->getName();
	f.fraction = TerrainResourceClaimView::claimableFraction(logic, ghost.x, ghost.y, data->m_radius, data->m_highPriority, localPlayer);
	f.percent = (int)std::floor(f.fraction * 100.0f + 0.5f);
	f.text = UnitHelpSource::format(format, { [&] {
		const std::string s = std::to_string(f.percent);
		return std::u16string(s.begin(), s.end());
	}() });
	f.color = TerrainResourceClaimView::bandColor(f.fraction);
	f.pointerX = pointerX;
	f.pointerY = pointerY;
	// the projected decal of type 0x2000 (RW 0x50C23A): the centre snapped down to the claim grid, the size 2 * Radius
	const float cell = grid.cellSize();
	f.decalCenter = ghost;
	f.decalCenter.x = ghost.x - std::fmod(ghost.x, cell);
	f.decalCenter.y = ghost.y - std::fmod(ghost.y, cell);
	f.decalSize = data->m_radius * 2.0f;
	// the vertices under the decal (10 units apart) and their alphas (the decal colour with the opacity as its alpha, RW 0x75C1A9)
	const float half = f.decalSize * 0.5f;
	f.maskVx0 = (int)std::floor((f.decalCenter.x - half) / kVertexSpacing);
	f.maskVy0 = (int)std::floor((f.decalCenter.y - half) / kVertexSpacing);
	const int vx1 = (int)std::ceil((f.decalCenter.x + half) / kVertexSpacing);
	const int vy1 = (int)std::ceil((f.decalCenter.y + half) / kVertexSpacing);
	f.maskW = vx1 - f.maskVx0 + 1;
	f.maskH = vy1 - f.maskVy0 + 1;
	f.maskAlpha.resize((size_t)f.maskW * (size_t)f.maskH);
	// lane PLACEDECAL-1: the vertices' diffuse before the weight is the decal's +0x28, 0xFFFFFFFF: setColor / setOpacity (RW 0x73232B / 0x73228B) leave it
	// for type 0x2000 (outside their masks 0x1420 / 0x10840), so neither the band colour nor the opacity 0x80 reach the decal (the band colour is the text's)
	const std::uint32_t base = 0xFFFFFFFFu;
	// lane FOGFX-1 (FB-0055): every vertex asks 13 cells around it and neighbouring vertices share them (a farm's decal: about 1000 vertices, 13000
	// asks of RW 0x75C02D on every render frame); each cell is asked once here and its answer reused (the same arguments give the same answer within
	// this update: the values are retail's). The cells of vertex v are (v - 2) / 2 .. (v + 2) / 2 (truncating division, monotonic in v)
	const int cx0 = (f.maskVx0 - 2) / 2, cy0 = (f.maskVy0 - 2) / 2;
	const int cw = (vx1 + 2) / 2 - cx0 + 1, ch = (vy1 + 2) / 2 - cy0 + 1;
	m_claimMemo.assign((size_t)cw * (size_t)ch, -1);
	auto ok = [&](int cx, int cy) {
		std::int8_t &m = m_claimMemo[(size_t)(cy - cy0) * (size_t)cw + (size_t)(cx - cx0)];
		if (m < 0)
		{
			m = TerrainResourceClaimView::claimable(logic, cx, cy, 0u, INVALID_ID, false, localPlayer) ? 1 : 0;
		}
		return m == 1;
	};
	for (int j = 0; j < f.maskH; ++j)
	{
		for (int i = 0; i < f.maskW; ++i)
		{
			f.maskAlpha[(size_t)j * (size_t)f.maskW + (size_t)i] = (std::uint8_t)(weightedVertexColor(base, f.maskVx0 + i, f.maskVy0 + j, ok) >> 24);
		}
	}
	const bool changed = !m_placement.active || f.color != m_placement.color || f.maskVx0 != m_placement.maskVx0 || f.maskVy0 != m_placement.maskVy0
		|| f.maskAlpha != m_placement.maskAlpha || f.decalSize != m_placement.decalSize;
	f.version = m_placement.version + (changed ? 1u : 0u);
	m_placement = std::move(f);
}
