// OpenBFME. GPL-3.0.
// See GameClient/TerrainResourceFeedback.h for the target facts and the addresses.

#include "GameClient/TerrainResourceFeedback.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameClient/UnitHelpSource.h"
#include "GameLogic/Economy.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/TerrainResourceBehavior.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/System/ShroudManager.h"
#include "GameLogic/System/TerrainResourceManager.h"

#include <cmath>
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
	return "[S-5030] resource claim feedback (lane OWNERFIX-2): the placement's claimable percentage, colour and masked decal (RW 0x75B561 / 0x75C02D / 0x75C1A9) and "
		   "the selected resource building's claim decals (TerrainResourceClaimFeedback) are ported; INFERENCE: the claim decals show while a selected object of the "
		   "local player lists a TerrainResourceClientBehavior (retail's select path into slot 17 not read), the collector's outline arcs (RW twin of BFME2 "
		   "0x4E5372) are not drawn, the vertex alphas are bilinear across a terrain quad, the text uses the fallback font at the INI's size";
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
		return;
	}
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
	const std::uint32_t base = ((std::uint32_t)f.decalOpacity << 24) | (f.color & 0xFFFFFFu);
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
