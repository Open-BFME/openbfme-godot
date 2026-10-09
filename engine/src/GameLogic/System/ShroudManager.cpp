// OpenBFME. GPL-3.0.
// ShroudManager (lane VIS-1): the port of RotWK's shroud implementation object (RW 0xB4D8B0 .. 0xB53000). See ShroudManager.h for the target facts.

#include "GameLogic/System/ShroudManager.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/StateHash.h"
#include "Common/Team.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/BitFlags.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/CombatQueries.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/ObjectGeometry.h"
#include "GameLogic/SimMath.h"

#include <algorithm>
#include <stdexcept>

namespace
{
const std::uint32_t kPlayerBits = 0xFFFFFu; // RW `and 0xFFFFF` (20 players)
const float kAngleStep = 0.392625f;          // RW 0xD09F08
const float kQuarterTurn = 1.5705f;          // RW 0xD09F0C

CellShroudStatus statusOf(std::int16_t v)
{
	// RW 0xB52E10 / 0xB4FB20: -1 SHROUDED, 0 FOGGED, otherwise CLEAR
	return v == -1 ? CELLSHROUD_SHROUDED : (v == 0 ? CELLSHROUD_FOGGED : CELLSHROUD_CLEAR);
}

// RW 0xA3CFA4 _ftol2 of a register value: truncation toward zero; the callers keep the low word
int ftolLow(double v)
{
	return (int)SimMath::ftol2Low32(v);
}

// `(v + 0x80) / 256` with C truncation (RW 0xB501A0: `(v + 0x80 + ((v + 0x80) >> 31 & 0xFF)) >> 8`)
int fixedRound(int v)
{
	return (v + 0x80) / 256;
}

int kindIndex(const char *name)
{
	return CombatNames::kindOf(name);
}

float groundReference(const Object &obj, float radius, int segments);

// GeometryIsSmall (the template's last row; RW geometry + 4)
bool geometryIsSmall(const ThingTemplate &tt)
{
	bool small = false;
	for (const ThingTemplate::GeometryEvent &e : tt.geometryEvents())
	{
		if (e.row == "GeometryIsSmall" && !e.tokens.empty())
		{
			const std::string &t = e.tokens[0];
			small = (t == "Yes" || t == "yes" || t == "YES");
		}
	}
	return small;
}
} // namespace

ShroudManager::Cell::Cell()
{
	lookers.fill(-1); // RW 0xB4F7F0 (the cell constructor): never seen
	for (auto &c : channels)
	{
		c.fill(0);
	}
}

ShroudManager::ShroudManager(GameLogic &logic)
	: m_logic(logic)
	, m_source(retailObjectSource())
{
}

ShroudManager::~ShroudManager()
{
	detach();
}

void ShroudManager::attach()
{
	if (m_attached)
	{
		return;
	}
	m_attached = true;
	m_logic.setShroud(this);
	m_hookToken = m_logic.addWorldHooks([this](Object &o) { registerObject(o); }, [this](Object &o) { unregisterObject(o); }, true); // part of RW 0x68E31F / 0x68C18F (lane GARRISON-1: a contain's world exit too)
	// RW 0x62EB85: the shroud manager is the first of the four subsystem updates before the destroy list (the other three, RW 0xDE435C / 0xDE8200 / 0xDE3BE8,
	// are not ported: the row is this lane's until a lane ports one of them)
	m_logic.installPhaseWork("subsystemsBeforeDestroy", [this]() { update(); });
	m_hashToken = m_logic.addStateHashContributor([this](StateHasher &h) { hash(h); }, "shroud");
	for (const std::string &s : stopLines())
	{
		m_logic.noteStop(s); // the stops of this subsystem reach GameLogic::report().stops
	}
}

void ShroudManager::detach()
{
	if (!m_attached)
	{
		return;
	}
	m_attached = false;
	if (m_logic.shroud() == this)
	{
		m_logic.setShroud(nullptr);
	}
	m_logic.removeWorldHooks(m_hookToken);
	m_logic.installPhaseWork("subsystemsBeforeDestroy", std::function<void()>());
	m_logic.removeStateHashContributor(m_hashToken);
}

void ShroudManager::init(float cellSize, unsigned unlookPersistFrames)
{
	m_unlookPersist = unlookPersistFrames; // RW 0x587790 via 0xB4D8D0
	setExtent(0.0f, 0.0f, 0.0f, 0.0f, cellSize);
}

void ShroudManager::setExtent(float loX, float loY, float hiX, float hiY, float cellSize)
{
	// RW 0xB520F0: a non-positive size keeps the current one; a negative extent is ignored
	if (cellSize <= 0.0f)
	{
		cellSize = m_cellSize;
	}
	if (!(SimMath::subf32(hiX, loX) >= 0.0f) || !(SimMath::subf32(hiY, loY) >= 0.0f))
	{
		return;
	}
	// RW 0xB51D90: settle every record (update, take every look and channel off, queue flushed), resize, copy the overlap, look again
	update();
	for (Record &r : m_records)
	{
		if (r.inUse)
		{
			clearCoverage(r);
			removeLookOf(r);
		}
	}
	processPending(false);
	if (SimMath::subf32(hiX, loX) < 1.0f)
	{
		hiX = SimMath::addf32(loX, 1.0f);
	}
	if (SimMath::subf32(hiY, loY) < 1.0f)
	{
		hiY = SimMath::addf32(loY, 1.0f);
	}
	const float inv = SimMath::pc24Div(1.0f, cellSize);
	int nx = SimMath::ceilToInt(SimMath::pc24Mul(SimMath::pc24Sub(hiX, loX), inv));
	int ny = SimMath::ceilToInt(SimMath::pc24Mul(SimMath::pc24Sub(hiY, loY), inv));
	nx = std::max(nx, 1);
	ny = std::max(ny, 1);
	std::vector<Cell> cells((size_t)nx * (size_t)ny);
	for (int y = 0; y < ny; ++y)
	{
		const int oy = SimMath::floorToInt(SimMath::pc24Mul(SimMath::pc24Sub(SimMath::pc24Add(SimMath::pc24MulD((double)y, cellSize), loY), m_loY), m_cellSizeInv));
		if (oy < 0 || oy >= m_countY)
		{
			continue;
		}
		for (int x = 0; x < nx; ++x)
		{
			const int ox = SimMath::floorToInt(SimMath::pc24Mul(SimMath::pc24Sub(SimMath::pc24Add(SimMath::pc24MulD((double)x, cellSize), loX), m_loX), m_cellSizeInv));
			if (ox >= 0 && ox < m_countX)
			{
				const Cell &old = m_cells[(size_t)oy * (size_t)m_countX + (size_t)ox];
				cells[(size_t)y * (size_t)nx + (size_t)x].lookers = old.lookers;
				cells[(size_t)y * (size_t)nx + (size_t)x].channels = old.channels;
			}
		}
	}
	m_cells.swap(cells);
	m_loX = loX;
	m_loY = loY;
	m_hiX = hiX;
	m_hiY = hiY;
	m_cellSize = cellSize;
	m_cellSizeInv = inv;
	m_countX = nx;
	m_countY = ny;
	if (recordCount() == 0)
	{
		m_counter = 0;
		return;
	}
	// every record re-looks on the new grid (RW 0xB51D90's first loop marks them dirty, forced)
	for (size_t i = m_registration.size(); i-- > 0;)
	{
		Record &r = m_records[m_registration[i]];
		r.lookX = (int)0xDEADBEEF;
		r.lookY = (int)0x0BADF00D;
		r.force = true;
		if (!r.dirty)
		{
			r.dirty = true;
			m_dirty.push_back(m_registration[i]);
		}
	}
	update();
	refreshLocalPlayer();
}

void ShroudManager::setLocalPlayer(int playerIndex, std::function<void(int, int, CellShroudStatus)> callback)
{
	m_localPlayer = playerIndex;
	m_callback = std::move(callback);
}

void ShroudManager::refreshLocalPlayer()
{
	// RW 0xB4F9C0 zeroes the local player's cached status of every record, so the next query recomputes it (a status can depend on more than the cells: the
	// fog rule RW 0x68EDD0 reads the relationship and the "seen" byte). The cache is logic state every peer hashes, so here EVERY live record's statuses
	// are cleared and recomputed for EVERY live player, in registration order, before (and whatever) the local player: the same on every peer
	reevaluateAll();
	if (m_localPlayer < 0 || m_localPlayer >= kMaxPlayers)
	{
		return;
	}
	for (int y = 0; y < m_countY; ++y)
	{
		for (int x = 0; x < m_countX; ++x)
		{
			if (m_callback)
			{
				m_callback(x, y, statusOf(m_cells[(size_t)y * (size_t)m_countX + (size_t)x].lookers[(size_t)m_localPlayer]));
			}
		}
	}
}

void ShroudManager::reevaluateAll()
{
	const int players = playerCountLimit();
	for (std::uint32_t slot : m_registration)
	{
		Record &r = m_records[slot];
		r.status.fill(0);
		const Object *obj = m_logic.findObjectByID(r.id);
		for (int p = 0; p < players; ++p)
		{
			evaluate(r, p, obj);
		}
	}
}

// ---- cells ----------------------------------------------------------------------------------------------------------------------
ShroudManager::Cell *ShroudManager::cellAt(int x, int y)
{
	if (x < 0 || y < 0 || x >= m_countX || y >= m_countY)
	{
		return nullptr;
	}
	return &m_cells[(size_t)y * (size_t)m_countX + (size_t)x];
}

const ShroudManager::Cell *ShroudManager::cellAt(int x, int y) const
{
	return const_cast<ShroudManager *>(this)->cellAt(x, y);
}

void ShroudManager::edge(Cell &c, int index, int player, CellShroudStatus s)
{
	++m_stats.edges;
	for (std::uint32_t slot : c.coveredBy)
	{
		m_records[slot].status[(size_t)player] = 0;
	}
	if (player == m_localPlayer && m_callback)
	{
		m_callback(index % m_countX, index / m_countX, s);
	}
}

void ShroudManager::addLooker(Cell &c, int index, int player)
{
	// RW 0xB52E10
	const std::int16_t before = c.lookers[(size_t)player];
	std::int16_t after = (std::int16_t)(before + 1);
	if (after == 0)
	{
		after = 1;
	}
	c.lookers[(size_t)player] = after;
	if (statusOf(before) != statusOf(after))
	{
		edge(c, index, player, statusOf(after));
	}
}

void ShroudManager::removeLooker(Cell &c, int index, int player)
{
	// RW 0xB52EC0
	const std::int16_t before = c.lookers[(size_t)player];
	const std::int16_t after = (std::int16_t)(before - 1);
	c.lookers[(size_t)player] = after;
	if (statusOf(before) != statusOf(after))
	{
		edge(c, index, player, statusOf(after));
	}
}

void ShroudManager::resetLooker(Cell &c, int index, int player)
{
	// RW 0xB52F60: a fogged cell goes back to never seen
	if (c.lookers[(size_t)player] == 0)
	{
		c.lookers[(size_t)player] = -1;
		edge(c, index, player, CELLSHROUD_SHROUDED);
	}
}

// RW 0xB4FC80 / 0xB4FD20 (the row clip RW 0xB4E460): for every player bit, the cells x1 .. x2 of row y
bool ShroudManager::hline(int x1, int x2, int y, std::uint32_t mask, bool add)
{
	if (x2 < 0 || x1 >= m_countX || y < 0 || y >= m_countY)
	{
		return true;
	}
	if (x2 < x1)
	{
		x2 = x1;
	}
	const int start = x1 > 0 ? x1 : 0;
	const int end = x2 < m_countX ? x2 + 1 : m_countX;
	const int row = y * m_countX;
	int player = 0;
	for (std::uint32_t m = mask; m != 0; m >>= 1, ++player)
	{
		if (!(m & 1u))
		{
			continue;
		}
		for (int x = start; x < end; ++x)
		{
			// the cell predicate RW 0x5879B0 returns 1 (no view blocking)
			Cell &c = m_cells[(size_t)(row + x)];
			if (add)
			{
				addLooker(c, row + x, player);
			}
			else
			{
				removeLooker(c, row + x, player);
			}
		}
	}
	return true;
}

// RW 0xB50100 / 0xB50570: the integer circle of hlines
void ShroudManager::circle(int cx, int cy, int r, std::uint32_t mask, bool add)
{
	int x = 0, xNext = 0;
	int err = 2 - 2 * r;
	int left = cx, right = cx;
	int dy = r;
	for (;;)
	{
		if (err + dy > 0)
		{
			if (dy == 0 && r == 1)
			{
				xNext = x + 1;
				++right;
				--left;
			}
			hline(left, right, cy + dy, mask, add);
			if (dy == 0)
			{
				return;
			}
			hline(left, right, cy - dy, mask, add);
			--dy;
			err += -2 * dy + 1;
			x = xNext;
		}
		if (err < x)
		{
			++x;
			++right;
			--left;
			err += 1 + 2 * x;
			xNext = x;
		}
	}
}

// RW 0xB501A0 / 0xB50610: scan conversion of a polygon in 8.8 fixed point (the arrays are edited in place like retail's)
void ShroudManager::polygon(const int *xsIn, const int *ysIn, int n, std::uint32_t mask, bool add)
{
	int xs[16], ys[16];
	for (int i = 0; i < n; ++i)
	{
		xs[i] = xsIn[i];
		ys[i] = ysIn[i];
	}
	int m = 1;
	for (int i = 1; i < n; ++i)
	{
		if (xs[i] != xs[m - 1] || ys[i] != ys[m - 1])
		{
			xs[m] = xs[i];
			ys[m] = ys[i];
			++m;
		}
	}
	while (m > 2 && xs[0] == xs[m - 1] && ys[0] == ys[m - 1])
	{
		--m;
	}
	if (m < 3)
	{
		return;
	}
	auto next = [m](int i) { return i + 1 == m ? 0 : i + 1; };
	int top = 0;
	for (int i = 1; i < m; ++i)
	{
		if (ys[i] < ys[top] || (ys[i] == ys[top] && xs[i] < xs[top]))
		{
			top = i;
		}
	}
	int y = ys[top];
	// the left chain walks the indices down, the right chain up
	int li = (top == 0 ? m : top) - 1;
	int dyL = ys[li] - y;
	// INFERENCE (S-563): retail leaves the slope and position uninitialised when the first left edge is horizontal; taken as the top vertex, slope 0
	int slopeL = 0, xL = xs[top] * 256;
	if (dyL > 0)
	{
		slopeL = ((xs[li] - xs[top]) * 256) / dyL;
		xL = xs[top] * 256 + slopeL / 2;
	}
	int ri = top;
	while (ys[ri] == ys[next(ri)])
	{
		ri = next(ri);
	}
	int rn = next(ri);
	int dyR = ys[rn] - ys[ri];
	int slopeR = 0, xR = xs[ri] * 256;
	if (dyR > 0)
	{
		slopeR = ((xs[rn] - xs[ri]) * 256) / dyR;
		xR = slopeR / 2 + xs[ri] * 256;
	}
	ri = rn;
	hline(fixedRound(xL), fixedRound(xR), y, mask, add);
	for (int guard = 0; guard < 4096; ++guard)
	{
		xL += slopeL;
		++y;
		--dyL;
		while (dyL == 0)
		{
			const int a = li != 0 ? li : m;
			dyL = ys[a - 1] - ys[li];
			if (dyL > 0)
			{
				slopeL = ((xs[a - 1] - xs[li]) * 256) / dyL;
				xL = xs[li] * 256 + slopeL / 2;
			}
			li = a - 1;
			if (dyL < 0)
			{
				return;
			}
		}
		xR += slopeR;
		--dyR;
		while (dyR == 0)
		{
			const int b = next(ri);
			dyR = ys[b] - ys[ri];
			if (dyR > 0)
			{
				slopeR = ((xs[b] - xs[ri]) * 256) / dyR;
				xR = xs[ri] * 256 + slopeR / 2;
			}
			ri = b;
			if (dyR < 0)
			{
				return;
			}
		}
		hline(fixedRound(xL), fixedRound(xR), y, mask, add);
	}
}

// RW 0xB50C90 / 0xB50E60
void ShroudManager::shape(int cx, int cy, const Radii &r, float facing, std::uint32_t mask, bool add)
{
	if (mask == 0 || r.forward < 0)
	{
		return;
	}
	mask &= kPlayerBits;
	if (r.forward == r.side && r.side == r.rear)
	{
		circle(cx, cy, r.forward, mask, add);
		return;
	}
	++m_stats.polygonLooks;
	// the x87 sequence of RW 0xB50D0F .. 0xB50DBC; INFERENCE (S-563): the extended-precision register values are carried in binary64, fsin / fcos are
	// SimMath's deterministic sin / cos
	const double theta = SimMath::subD((double)kQuarterTurn, (double)facing);
	double sT, cT;
	SimMath::sinCosDet(theta, sT, cT);
	const float c = (float)cT;           // `fstp dword [esp + 0x14]`
	const double s = sT;                 // kept in the register
	const float side = (float)r.side;    // `fild; fstp dword`
	const float cyF = (float)cy;
	int xs[16], ys[16];
	for (int i = 0; i < 16; ++i)
	{
		const double a = SimMath::mulD((double)(i - 4), (double)kAngleStep);
		double sA, cA;
		SimMath::sinCosDet(a, sA, cA);
		const int radius = ((i >> 2) & 2) ? r.rear : r.forward; // `shr ecx, 2; and ecx, 2`: radii[0] or radii[2]
		const double X = SimMath::mulD(cA, (double)radius);
		const double Y = SimMath::mulD(sA, (double)side);
		const double px = SimMath::addD(SimMath::addD(SimMath::addD(SimMath::mulD((double)c, Y), SimMath::mulD(X, s)), (double)cx), 0.5);
		const double py = SimMath::addD(SimMath::addD(SimMath::addD(SimMath::mulD(Y, -s), SimMath::mulD(X, (double)c)), (double)cyF), 0.5);
		// stored from the top of the arrays down (`[esp + esi + 0xA0]`, esi = 0, -4, ...): array index 15 - i
		xs[15 - i] = ftolLow(px);
		ys[15 - i] = ftolLow(py);
	}
	polygon(xs, ys, 16, mask, add);
}

void ShroudManager::lookAt(int cellX, int cellY, const Radii &r, float facing, std::uint32_t mask)
{
	++m_stats.looks;
	shape(cellX, cellY, r, facing, mask, true);
}

void ShroudManager::unlookAt(int cellX, int cellY, const Radii &r, float facing, std::uint32_t mask)
{
	++m_stats.unlooks;
	shape(cellX, cellY, r, facing, mask, false);
}

// ---- map-wide --------------------------------------------------------------------------------------------------------------------
void ShroudManager::revealMapForPlayer(int player)
{
	if (player < 0 || player >= kMaxPlayers)
	{
		return;
	}
	for (size_t i = 0; i < m_cells.size(); ++i)
	{
		addLooker(m_cells[i], (int)i, player);
		removeLooker(m_cells[i], (int)i, player);
	}
}

void ShroudManager::revealMapForPlayerPermanently(int player)
{
	if (player < 0 || player >= kMaxPlayers)
	{
		return;
	}
	for (size_t i = 0; i < m_cells.size(); ++i)
	{
		addLooker(m_cells[i], (int)i, player);
	}
}

void ShroudManager::undoRevealMapForPlayerPermanently(int player)
{
	if (player < 0 || player >= kMaxPlayers)
	{
		return;
	}
	processPending(false);
	for (size_t i = 0; i < m_cells.size(); ++i)
	{
		removeLooker(m_cells[i], (int)i, player);
	}
}

void ShroudManager::shroudMapForPlayer(int player)
{
	if (player < 0 || player >= kMaxPlayers)
	{
		return;
	}
	processPending(false);
	for (size_t i = 0; i < m_cells.size(); ++i)
	{
		resetLooker(m_cells[i], (int)i, player);
	}
}

void ShroudManager::applyNewGameShroud(bool useShroud, const std::vector<int> &slotPlayers, const std::vector<int> &observerPlayers)
{
	// RW 0x62F91A: the ReplayObserver side first, then the slots in order
	for (int p : observerPlayers)
	{
		revealMapForPlayerPermanently(p);
	}
	if (!useShroud)
	{
		for (int p : slotPlayers)
		{
			revealMapForPlayer(p);
		}
	}
}

// ---- queries ---------------------------------------------------------------------------------------------------------------------
bool ShroudManager::worldToCell(float x, float y, int &cx, int &cy) const
{
	cx = SimMath::floorToInt(SimMath::pc24Mul(SimMath::pc24Sub(x, m_loX), m_cellSizeInv));
	cy = SimMath::floorToInt(SimMath::pc24Mul(SimMath::pc24Sub(y, m_loY), m_cellSizeInv));
	return cx >= 0 && cy >= 0 && cx < m_countX && cy < m_countY;
}

int ShroudManager::channelSum(float x, float y, int channel, std::uint32_t playerMask) const
{
	// RW 0xB4FF50: the mask is cut to 20 players; the cell's uint16 of each set player is added (int)
	if (playerMask == 0 || channel < 0 || channel >= kChannels)
	{
		return 0;
	}
	playerMask &= 0xFFFFFu;
	int cx, cy;
	if (!worldToCell(x, y, cx, cy))
	{
		return 0;
	}
	const Cell *c = cellAt(cx, cy);
	if (!c)
	{
		return 0;
	}
	int sum = 0;
	for (int p = 0; playerMask != 0 && p < kMaxPlayers; ++p, playerMask >>= 1)
	{
		if (playerMask & 1u)
		{
			sum += (int)c->channels[(size_t)p][(size_t)channel];
		}
	}
	return sum;
}

CellShroudStatus ShroudManager::getCellStatus(int player, int cellX, int cellY) const
{
	const Cell *c = cellAt(cellX, cellY);
	if (!c || player < 0 || player >= kMaxPlayers)
	{
		return CELLSHROUD_SHROUDED;
	}
	return statusOf(c->lookers[(size_t)player]);
}

CellShroudStatus ShroudManager::getStatusAt(int player, float x, float y) const
{
	int cx, cy;
	if (!worldToCell(x, y, cx, cy))
	{
		return CELLSHROUD_SHROUDED;
	}
	return getCellStatus(player, cx, cy);
}

int ShroudManager::lookerCount(int player, int cellX, int cellY) const
{
	const Cell *c = cellAt(cellX, cellY);
	return c && player >= 0 && player < kMaxPlayers ? c->lookers[(size_t)player] : -1;
}

size_t ShroudManager::recordCount() const
{
	size_t n = 0;
	for (const Record &r : m_records)
	{
		n += r.inUse ? 1u : 0u;
	}
	return n;
}

ShroudManager::Record *ShroudManager::recordOf(const Object &obj)
{
	const ObjectID id = obj.getID();
	if (id >= m_slotById.size() || m_slotById[id] == 0)
	{
		return nullptr;
	}
	return &m_records[m_slotById[id] - 1];
}

const ShroudManager::Record *ShroudManager::recordOf(const Object &obj) const
{
	return const_cast<ShroudManager *>(this)->recordOf(obj);
}

bool ShroudManager::isRegistered(const Object &obj) const
{
	return recordOf(obj) != nullptr;
}

bool ShroudManager::hasSeen(const Object &obj, int player) const
{
	const Record *r = recordOf(obj);
	return r && player >= 0 && player < kMaxPlayers && r->seen[(size_t)player] != 0;
}

ObjectShroudStatus ShroudManager::peekObjectStatus(const Object &obj, int player) const
{
	const Record *r = recordOf(obj);
	if (!r || player < 0 || player >= kMaxPlayers)
	{
		return OBJECTSHROUD_INVALID;
	}
	return (ObjectShroudStatus)r->status[(size_t)player];
}

ObjectShroudStatus ShroudManager::getObjectStatus(const Object &obj, int player)
{
	// RW 0x68D8F7: no record, or ALWAYS_VISIBLE -> 0
	static const int alwaysVisible = kindIndex("ALWAYS_VISIBLE");
	Record *r = recordOf(obj);
	if (!r || obj.isKindOf((unsigned)alwaysVisible))
	{
		return OBJECTSHROUD_INVALID;
	}
	if (player < 0 || player >= kMaxPlayers)
	{
		return OBJECTSHROUD_SHROUDED; // RW 0xB4E890 returns 4
	}
	if (r->status[(size_t)player] == 0)
	{
		evaluate(*r, player, &obj);
	}
	return (ObjectShroudStatus)r->status[(size_t)player];
}

// RW 0xB4E890, the status part (SMOOTH-1: shared by evaluate, which stores it, and clientObjectStatus, which does not). `seen` gets 0 (no cell or every cell
// shrouded: the seen flag clears), 1 (a cell is clear: it sets) or -1 (all fogged / shrouded: unchanged)
ObjectShroudStatus ShroudManager::computeStatus(const Record &r, int player, const Object *obj, int &seen) const
{
	const size_t p = (size_t)player;
	int total = 0, fogged = 0, shrouded = 0;
	for (int index : r.covered)
	{
		++total;
		const CellShroudStatus s = statusOf(m_cells[(size_t)index].lookers[p]);
		if (s == CELLSHROUD_FOGGED)
		{
			++fogged;
		}
		else if (s == CELLSHROUD_SHROUDED)
		{
			++shrouded;
		}
	}
	if (total == 0 || shrouded == total)
	{
		seen = 0;
		return OBJECTSHROUD_SHROUDED;
	}
	if (fogged + shrouded != total)
	{
		seen = 1;
		return (shrouded != 0 || fogged != 0) ? OBJECTSHROUD_PARTIAL_CLEAR : OBJECTSHROUD_CLEAR;
	}
	seen = -1;
	// the ghost branch needs a ghost object (RW record + 8, not kept: S-561); the object's own test (vtable slot 0x14 = RW 0x68EDD0)
	if (obj && m_source.hidesWhenFogged && m_source.hidesWhenFogged(*obj, player))
	{
		return OBJECTSHROUD_SHROUDED;
	}
	return OBJECTSHROUD_FOGGED;
}

// RW 0xB4E890
void ShroudManager::evaluate(Record &r, int player, const Object *obj)
{
	const size_t p = (size_t)player;
	int seen = -1;
	r.status[p] = computeStatus(r, player, obj, seen);
	if (seen >= 0)
	{
		r.seen[p] = (std::uint8_t)seen;
	}
	if (!r.covered.empty())
	{
		r.previous[p] = r.status[p];
	}
}

ObjectShroudStatus ShroudManager::clientObjectStatus(const Object &obj, int player) const
{
	// getObjectStatus without its cache write (the client's read must not change state the simulation hashes)
	static const int alwaysVisible = kindIndex("ALWAYS_VISIBLE");
	const Record *r = recordOf(obj);
	if (!r || obj.isKindOf((unsigned)alwaysVisible))
	{
		return OBJECTSHROUD_INVALID;
	}
	if (player < 0 || player >= kMaxPlayers)
	{
		return OBJECTSHROUD_SHROUDED;
	}
	if (r->status[(size_t)player] != 0)
	{
		return (ObjectShroudStatus)r->status[(size_t)player];
	}
	int seen = -1;
	return computeStatus(*r, player, &obj, seen);
}

// ---- objects ---------------------------------------------------------------------------------------------------------------------
void ShroudManager::registerObject(Object &obj)
{
	// RW 0xB515E0: once (an object entering again keeps its record)
	if (recordOf(obj))
	{
		markDirty(obj, true);
		return;
	}
	std::uint32_t slot;
	if (!m_freeSlots.empty())
	{
		slot = m_freeSlots.back();
		m_freeSlots.pop_back();
	}
	else
	{
		slot = (std::uint32_t)m_records.size();
		m_records.emplace_back();
	}
	Record &r = m_records[slot];
	r = Record();
	r.inUse = true;
	r.id = obj.getID();
	if (r.id >= m_slotById.size())
	{
		m_slotById.resize((size_t)r.id + 1, 0);
	}
	m_slotById[r.id] = slot + 1;
	m_registration.push_back(slot);
	// RW 0x68E3A3: entering the world marks the record dirty, forced
	markDirty(obj, true);
}

void ShroudManager::unregisterObject(Object &obj)
{
	// RW 0xB4F510 -> 0xB4EAC0 (the ghost retention of RW 0xB4E190 is not ported, S-561): the look is queued off, the channels and the coverage go
	Record *r = recordOf(obj);
	if (!r)
	{
		return;
	}
	const std::uint32_t slot = m_slotById[r->id] - 1;
	removeLookOf(*r);
	clearCoverage(*r);
	m_dirty.erase(std::remove(m_dirty.begin(), m_dirty.end(), slot), m_dirty.end());
	m_registration.erase(std::remove(m_registration.begin(), m_registration.end(), slot), m_registration.end());
	m_slotById[r->id] = 0;
	*r = Record();
	m_freeSlots.push_back(slot);
}

ShroudManager::TemplateVision ShroudManager::templateVision(const ThingTemplate &tt)
{
	TemplateVision v;
	std::vector<ObjectGeometry::Shape> shapes;
	try
	{
		shapes = ObjectGeometry::shapesOf(tt);
	}
	catch (const std::exception &e)
	{
		throw std::logic_error("shroud: template " + tt.getName() + ": " + e.what());
	}
	if (!shapes.empty())
	{
		v.shape = shapes.back(); // INFERENCE (S-562): the record reads the object's geometry info, here the template's last shape (see S-341)
	}
	v.small = geometryIsSmall(tt);
	// the slots of the eight rows in the template's row-name table, resolved once per table and checked by name at every use (a table at a reused address
	// with another layout resolves again); the VALUES are always the template's current ones
	enum
	{
		F_RANGE, F_SIDE, F_REAR, F_PERFOOT, F_RADIUS, F_MAX, F_MIN, F_SEGMENTS, F_COUNT
	};
	static const char *const names[F_COUNT] = { "ShroudClearingRange", "VisionSide", "VisionRear", "VisionBonusPercentPerFoot", "VisionBonusTestRadius",
		"MaxVisionBonusPercent", "MinVisionBonusPercent", "VisionBonusTestSegments" };
	struct Slots
	{
		const std::vector<std::string> *table = nullptr;
		size_t index[F_COUNT];
	};
	thread_local Slots slots;
	const std::vector<std::string> *table = tt.fieldNameTable();
	bool valid = table && slots.table == table;
	for (int k = 0; valid && k < F_COUNT; ++k)
	{
		valid = slots.index[k] == (size_t)-1 || (slots.index[k] < table->size() && (*table)[slots.index[k]] == names[k]);
	}
	if (!valid)
	{
		slots.table = table;
		for (int k = 0; k < F_COUNT; ++k)
		{
			slots.index[k] = (size_t)-1;
			for (size_t i = 0; table && i < table->size(); ++i)
			{
				if ((*table)[i] == names[k])
				{
					slots.index[k] = i;
					break;
				}
			}
		}
	}
	auto field = [&](int k) -> const FieldValue * { return slots.index[k] == (size_t)-1 ? nullptr : tt.fieldAt(slots.index[k]); };
	auto real = [&](int k, float fallback) {
		if (const FieldValue *f = field(k))
		{
			if (const float *x = std::get_if<float>(f))
			{
				return *x;
			}
			if (const long long *i = std::get_if<long long>(f))
			{
				return (float)*i;
			}
		}
		return fallback;
	};
	v.shroudClearingRange = real(F_RANGE, 0.0f);
	v.visionSide = real(F_SIDE, 1.0f);
	v.visionRear = real(F_REAR, 1.0f);
	v.bonusPerFoot = real(F_PERFOOT, 0.0f);
	v.bonusTestRadius = real(F_RADIUS, 0.0f);
	v.maxBonus = real(F_MAX, 0.0f);
	v.minBonus = real(F_MIN, 0.0f);
	if (const FieldValue *f = field(F_SEGMENTS))
	{
		if (const long long *i = std::get_if<long long>(f))
		{
			v.bonusTestSegments = (int)*i;
		}
	}
	return v;
}

float ShroudManager::groundReferenceFor(const Object &obj, const TemplateVision &tv)
{
	// RW 0x69264D: `(int)x`, `(int)y` of the position (cvttss2si); the circle is sampled again only when they change
	Record *r = recordOf(obj);
	const int ix = SimMath::cvttss2si(obj.getPosition()->x), iy = SimMath::cvttss2si(obj.getPosition()->y);
	if (r && r->groundValid && r->groundX == ix && r->groundY == iy)
	{
		return r->groundRef;
	}
	const float ref = groundReference(obj, tv.bonusTestRadius, tv.bonusTestSegments);
	if (r)
	{
		r->groundValid = true;
		r->groundX = ix;
		r->groundY = iy;
		r->groundRef = ref;
	}
	return ref;
}

void ShroudManager::markDirty(const Object &obj, bool force)
{
	Record *r = recordOf(obj);
	if (!r)
	{
		return;
	}
	if (!r->dirty)
	{
		r->dirty = true;
		m_dirty.push_back(m_slotById[r->id] - 1);
	}
	if (force)
	{
		r->force = true; // RW + 0x118: set by the forced mark and by a turn, never cleared (only the record's constructor writes 0)
	}
}

// RW 0xB4E100: the last look is queued off and the channels are taken off now
void ShroudManager::removeLookOf(Record &r)
{
	if (r.radii.forward >= 0)
	{
		queueUnlook(r);
		r.radii.forward = -1;
	}
	r.channelRadius.fill(-1); // the channels are not added (S-560)
}

void ShroudManager::queueUnlook(const Record &r)
{
	// RW 0xB526A0: due at counter + UnlookPersistDuration
	if (r.mask == 0 || r.radii.forward < 0)
	{
		return;
	}
	Pending p;
	p.due = m_counter + m_unlookPersist;
	p.x = r.lookX;
	p.y = r.lookY;
	p.radii = r.radii;
	p.facing = r.facing;
	p.mask = r.mask & kPlayerBits;
	m_pending.push_back(p);
}

void ShroudManager::processPending(bool considerTime)
{
	// RW 0xB51690: front first, while the entry's due frame is below the counter (everything when not considering time)
	const std::uint32_t limit = considerTime ? m_counter : 0xFFFFFFFFu;
	while (!m_pending.empty())
	{
		const Pending p = m_pending.front();
		if (limit <= p.due)
		{
			return;
		}
		m_pending.pop_front();
		unlookAt(p.x, p.y, p.radii, p.facing, p.mask);
	}
}

void ShroudManager::clearCoverage(Record &r)
{
	const std::uint32_t slot = m_slotById.size() > r.id && m_slotById[r.id] ? m_slotById[r.id] - 1 : 0xFFFFFFFFu;
	for (int index : r.covered)
	{
		std::vector<std::uint32_t> &v = m_cells[(size_t)index].coveredBy;
		v.erase(std::remove(v.begin(), v.end(), slot), v.end());
	}
	r.covered.clear();
}

// the cells the object's geometry covers (RW 0xB4EB70 sphere / cylinder, RW 0xB4E660 box, RW 0xB4ECA0 small geometry)
void ShroudManager::cover(Record &r, const Object &obj, const TemplateVision &tv)
{
	const std::uint32_t slot = m_slotById[r.id] - 1;
	auto addCell = [&](int index) {
		if (std::find(r.covered.begin(), r.covered.end(), index) != r.covered.end())
		{
			return;
		}
		r.covered.push_back(index);
		m_cells[(size_t)index].coveredBy.push_back(slot);
	};
	const Coord3D &pos = *obj.getPosition();
	ObjectGeometry::Shape g = tv.shape;
	if (m_source.coverageRadius)
	{
		g.majorRadius = m_source.coverageRadius(obj);
		g.type = ObjectGeometry::SHAPE_SPHERE;
	}
	const bool small = !m_source.coverageRadius && tv.small;
	if (small)
	{
		// RW 0xB4ECA0: the square of half-size min(radius, cellSize / 2)
		float half = g.majorRadius;
		const float limit = SimMath::pc24Mul(m_cellSize, 0.5f);
		if (limit < half)
		{
			half = limit;
		}
		const int x0 = SimMath::floorToInt(SimMath::pc24Mul(SimMath::pc24Sub(SimMath::pc24Sub(pos.x, half), m_loX), m_cellSizeInv));
		const int x1 = SimMath::floorToInt(SimMath::pc24Mul(SimMath::pc24Sub(SimMath::pc24Add(pos.x, half), m_loX), m_cellSizeInv));
		const int y0 = SimMath::floorToInt(SimMath::pc24Mul(SimMath::pc24Sub(SimMath::pc24Sub(pos.y, half), m_loY), m_cellSizeInv));
		const int y1 = SimMath::floorToInt(SimMath::pc24Mul(SimMath::pc24Sub(SimMath::pc24Add(pos.y, half), m_loY), m_cellSizeInv));
		for (int y = y0; y <= y1; ++y)
		{
			for (int x = std::max(x0, 0); x <= x1 && x < m_countX; ++x)
			{
				if (y >= 0 && y < m_countY)
				{
					addCell(y * m_countX + x);
				}
			}
		}
		return;
	}
	if (g.type == ObjectGeometry::SHAPE_BOX)
	{
		// RW 0xB4E660: a lattice of 4 samples per cell along both axes of the rotated box
		const float angle = obj.getOrientation();
		const float c = SimMath::cosDet(angle), s = SimMath::sinDet(angle);
		const float cs = SimMath::pc24Mul(c, m_cellSize);
		const float stepXx = SimMath::pc24Mul(0.5f, cs);
		const float stepXy = SimMath::pc24Mul(SimMath::pc24Mul(s, m_cellSize), 0.5f);
		const float stepYy = SimMath::pc24Mul(cs, -0.5f);
		const int nx = SimMath::ceilToInt(SimMath::pc24Mul(SimMath::pc24Mul(m_cellSizeInv, g.majorRadius), 4.0f));
		const int ny = SimMath::ceilToInt(SimMath::pc24Mul(SimMath::pc24Mul(m_cellSizeInv, g.minorRadius), 4.0f));
		float px = SimMath::pc24Sub(SimMath::pc24Sub(pos.x, SimMath::pc24Mul(c, g.majorRadius)), SimMath::pc24Mul(s, g.minorRadius));
		float py = SimMath::pc24Sub(SimMath::pc24Add(SimMath::pc24Mul(c, g.minorRadius), pos.y), SimMath::pc24Mul(s, g.majorRadius));
		for (int row = 0; row < ny; ++row)
		{
			float qx = px, qy = py;
			for (int col = 0; col < nx; ++col)
			{
				int cx, cy;
				if (worldToCell(qx, qy, cx, cy))
				{
					addCell(cy * m_countX + cx);
				}
				qx = SimMath::pc24Add(qx, stepXx);
				qy = SimMath::pc24Add(qy, stepXy);
			}
			px = SimMath::pc24Add(px, stepXy);
			py = SimMath::pc24Add(py, stepYy);
		}
		return;
	}
	// RW 0xB4EB70: radius ceil(r / cell) - 1 cells; below 1 the object's own cell
	const int cx = SimMath::floorToInt(SimMath::pc24Mul(SimMath::pc24Sub(pos.x, m_loX), m_cellSizeInv));
	const int cy = SimMath::floorToInt(SimMath::pc24Mul(SimMath::pc24Sub(pos.y, m_loY), m_cellSizeInv));
	const int rc = SimMath::ceilToInt(SimMath::pc24Mul(g.majorRadius, m_cellSizeInv)) - 1;
	if (rc < 1)
	{
		if (cellAt(cx, cy))
		{
			addCell(cy * m_countX + cx);
		}
		return;
	}
	// RW 0xB4EA20: the same integer circle as the look, its rows clipped by RW 0xB4E460
	int x = 0, xNext = 0, err = 2 - 2 * rc, left = cx, right = cx, dy = rc;
	auto row = [&](int l, int rr, int y) {
		if (rr < 0 || l >= m_countX || y < 0 || y >= m_countY)
		{
			return;
		}
		if (rr < l)
		{
			rr = l;
		}
		for (int xx = std::max(l, 0); xx < std::min(rr + 1, m_countX); ++xx)
		{
			addCell(y * m_countX + xx);
		}
	};
	for (;;)
	{
		if (err + dy > 0)
		{
			if (dy == 0 && rc == 1)
			{
				xNext = x + 1;
				++right;
				--left;
			}
			row(left, right, cy + dy);
			if (dy == 0)
			{
				return;
			}
			row(left, right, cy - dy);
			--dy;
			err += -2 * dy + 1;
			x = xNext;
		}
		if (err < x)
		{
			++x;
			++right;
			--left;
			err += 1 + 2 * x;
			xNext = x;
		}
	}
}

// RW 0xB4EF50
void ShroudManager::processRecord(Record &r)
{
	Object *obj = m_logic.findObjectByID(r.id);
	if (!obj)
	{
		return;
	}
	clearCoverage(r);
	// the template is read once per processing (RW reads its fields directly: always the current values)
	const TemplateVision tv = templateVision(*obj->getTemplate()->getFinalOverride());
	cover(r, *obj, tv);
	const Coord3D &pos = *obj->getPosition();
	const int cx = SimMath::floorToInt(SimMath::pc24Mul(SimMath::pc24Sub(pos.x, m_loX), m_cellSizeInv));
	const int cy = SimMath::floorToInt(SimMath::pc24Mul(SimMath::pc24Sub(pos.y, m_loY), m_cellSizeInv));
	const float facing = obj->getOrientation();
	if (cx == r.lookX && cy == r.lookY && r.radii.forward >= 0 && (r.radii.forward != r.radii.side || r.radii.forward != r.radii.rear) && facing != r.facing)
	{
		r.force = true;
	}
	if (cx != r.lookX || cy != r.lookY || r.force)
	{
		if (r.radii.forward >= 0)
		{
			queueUnlook(r);
		}
		LookInputs in;
		in.mask = r.mask; // the object writes the mask only when it looks (RW 0x694A45)
		if (m_source.look)
		{
			m_source.look(*obj, tv, in);
		}
		r.mask = in.mask;
		r.facing = facing;
		const float ranges[3] = { in.forward, in.side, in.rear };
		int cells[3];
		for (int i = 0; i < 3; ++i)
		{
			cells[i] = ranges[i] >= 0.0f ? SimMath::ceilToInt(SimMath::pc24Mul(ranges[i], m_cellSizeInv)) : -1;
		}
		r.radii.forward = cells[0];
		r.radii.side = cells[1];
		r.radii.rear = cells[2];
		if (r.radii.forward >= 0)
		{
			lookAt(cx, cy, r.radii, r.facing, r.mask);
		}
		r.lookX = cx;
		r.lookY = cy;
	}
	r.status.fill(0); // RW `param_1[9 .. 0x1C] = 0`
}

void ShroudManager::update()
{
	// RW 0xB51970
	++m_counter;
	while (!m_dirty.empty())
	{
		const std::uint32_t slot = m_dirty.back(); // the head of RW's prepend list
		m_dirty.pop_back();
		Record &r = m_records[slot];
		r.dirty = false;
		processRecord(r);
	}
	processPending(true);
	// INFERENCE (S-562): the cached statuses are evaluated here for every live player instead of on demand, so the "seen" bytes do not depend on who asked
	const int players = playerCountLimit();
	for (std::uint32_t slot : m_registration)
	{
		Record &r = m_records[slot];
		const Object *obj = m_logic.findObjectByID(r.id);
		for (int p = 0; p < players; ++p)
		{
			if (r.status[(size_t)p] == 0)
			{
				evaluate(r, p, obj);
			}
		}
	}
}

int ShroudManager::playerCountLimit() const
{
	return std::min(m_logic.players().getPlayerCount(), (int)kMaxPlayers);
}

void ShroudManager::hash(StateHasher &h) const
{
	// every piece of logic state, collections with their lengths first: the grid (origin, extent, size, inverse, counts), the settings, the cells, the unlook
	// queue in order, the records in registration order (with their look, facing, flags, channels, coverage, cached statuses and ground reference) and the
	// dirty list in processing order by object id
	h.addU32(0x56495331u); // 'VIS1'
	h.addU32(m_counter);
	h.addU32(m_unlookPersist);
	h.addFloat(m_loX);
	h.addFloat(m_loY);
	h.addFloat(m_hiX);
	h.addFloat(m_hiY);
	h.addFloat(m_cellSize);
	h.addFloat(m_cellSizeInv);
	h.addI32(m_countX);
	h.addI32(m_countY);
	h.addU32((std::uint32_t)m_cells.size());
	for (const Cell &c : m_cells)
	{
		for (int p = 0; p < kMaxPlayers; ++p)
		{
			h.addI32(c.lookers[(size_t)p]);
			for (int k = 0; k < kChannels; ++k)
			{
				h.addU32(c.channels[(size_t)p][(size_t)k]);
			}
		}
	}
	h.addU32((std::uint32_t)m_pending.size());
	for (const Pending &p : m_pending)
	{
		h.addU32(p.due);
		h.addI32(p.x);
		h.addI32(p.y);
		h.addI32(p.radii.forward);
		h.addI32(p.radii.side);
		h.addI32(p.radii.rear);
		h.addFloat(p.facing);
		h.addU32(p.mask);
	}
	h.addU32((std::uint32_t)m_registration.size());
	for (std::uint32_t slot : m_registration)
	{
		const Record &r = m_records[slot];
		h.addU32(r.id);
		h.addBool(r.dirty);
		h.addBool(r.force);
		h.addI32(r.lookX);
		h.addI32(r.lookY);
		h.addI32(r.radii.forward);
		h.addI32(r.radii.side);
		h.addI32(r.radii.rear);
		h.addU32(r.mask);
		h.addFloat(r.facing);
		for (int k = 0; k < kChannels; ++k)
		{
			h.addI32(r.channelRadius[(size_t)k]);
			h.addU32(r.channelMask[(size_t)k]);
			h.addI32(r.channelAmount[(size_t)k]);
		}
		h.addU32((std::uint32_t)r.covered.size());
		for (int index : r.covered)
		{
			h.addI32(index);
		}
		for (int p = 0; p < kMaxPlayers; ++p)
		{
			h.addU32(r.status[(size_t)p]);
			h.addU32(r.previous[(size_t)p]);
			h.addU32(r.seen[(size_t)p]);
		}
		h.addBool(r.groundValid);
		h.addI32(r.groundX);
		h.addI32(r.groundY);
		h.addFloat(r.groundRef);
	}
	h.addU32((std::uint32_t)m_dirty.size());
	for (size_t i = m_dirty.size(); i-- > 0;)
	{
		h.addU32(m_records[m_dirty[i]].id); // processing order: the head (back) first
	}
}

bool ShroudManager::isShroudedForAction(const Object &source, const Object &target)
{
	ShroudManager *sm = source.logic().shroud();
	const Player *owner = source.getControllingPlayer();
	if (!sm || !owner || owner->getPlayerType() != PLAYER_HUMAN)
	{
		return false;
	}
	return sm->getObjectStatus(target, owner->getPlayerIndex()) >= OBJECTSHROUD_FOGGED;
}

std::vector<std::string> ShroudManager::stopLines()
{
	return {
		"[S-560] shroud channels: the three per-cell channels of RW 0xB51030 (BuildCost / ThreatValue / CampnessValue maps the AI reads, RW 0x68EC3B) are not "
		"added; no AI consumer is ported",
		"[S-561] ghost objects: a destroyed immobile object's record is not kept for the players that saw it fogged (RW 0xB4E190, the ghost object of RW "
		"0x691602 / TheGhostObjectManager); its remembered building disappears when it dies",
		"[S-562] shroud inference: the object statuses are evaluated eagerly in update() (retail on demand); the covered cells use the template's last geometry "
		"shape; TheTerrainLogic::getExtent is boundary 0 (BFME2 donor, present-unmatched)",
		"[S-563] shroud numerics: the look polygon's x87 extended-precision vertices are computed in binary64 with SimMath's sin / cos (RW fsin / fcos); "
		"a horizontal first left edge (uninitialised slope in RW 0xB501A0) starts at the top vertex with slope 0",
		"[S-564] object look: the vision attribute modifiers (RW 0x804F39 types 0x10 / 0x14), the spied mask (player + 0x3C8), object + 0x30 / + 0x3F4 (radii "
		"0.1), DynamicShroudClearingRangeUpdate and the HordeContain VisionSide / VisionRearOverride are not read; object + 0x480 (hide when fogged) is taken as set",
		"[S-565] fogged targets: a human player's units cannot attack an object FOGGED or SHROUDED for that player unless the order comes from a script (RW "
		"0x82C167, the action helper: human and not script, as ZH ActionManager isObjectShroudedForAction); its other callers and the other action types were not read",
		"[S-566] stealth and detection: RotWK's invisibility (InvisibilityUpdate, StealthDetectorUpdate, the InvisibilityManager, the INVISIBLE_STEALTH / CAMOUFLAGE "
		"conditions and the INVISIBLE_DETECTED statuses) is lane STEALTH-1's (S-1040 .. S-1042); the shroud does not change for a stealthed object",
		"[S-567] shroud drawing: one texel per cell with bilinear filtering multiplies the terrain by ShroudColor * level / 255 (ClearAlpha / FogAlpha / "
		"ShroudAlpha); objects the local player cannot see are not drawn, fogged ones are not darkened; water, rivers and roads are not shrouded; the radar "
		"picture is multiplied per pixel; W3DShroud / W3DRadar's own textures were not read",
	};
}

// ---- the object side (RW vtable 0xC123B0) ----------------------------------------------------------------------------------------
namespace
{
// RW 0x69264D: the mean ground height of VisionBonusTestSegments points at VisionBonusTestRadius
float groundReference(const Object &obj, float radius, int segments)
{
	const GameLogic &logic = obj.logic();
	const Coord3D &pos = *obj.getPosition();
	if (segments <= 0)
	{
		return 0.0f;
	}
	const float step = SimMath::pc24Div(6.2831855f, (float)segments); // RW DAT_00C12150 / n
	float angle = 0.0f;
	float sum = 0.0f;
	for (int i = 0; i < segments; ++i)
	{
		double s, c;
		SimMath::sinCosDet((double)angle, s, c);
		const float px = (float)SimMath::addD(SimMath::mulD(c, (double)radius), (double)pos.x);
		const float py = (float)SimMath::addD(SimMath::mulD(s, (double)radius), (double)pos.y);
		sum = SimMath::pc24Add(logic.getGroundHeight(px, py), sum);
		angle = SimMath::sseAdd(angle, step);
	}
	return SimMath::sseDiv(sum, SimMath::sseFromInt32(segments));
}

// RW 0x68E4E2
float shroudClearingRange(const Object &obj, ShroudManager &sm, const ShroudManager::TemplateVision &tv)
{
	float range = tv.shroudClearingRange;
	if (obj.isUnderConstruction())
	{
		range = CombatQueries::boundingCircleRadius(obj); // RW + 0xB8
	}
	if (tv.bonusPerFoot > 0.0f)
	{
		const float ref = sm.groundReferenceFor(obj, tv);
		float bonus = SimMath::pc24Mul(SimMath::pc24Sub(obj.getPosition()->z, ref), tv.bonusPerFoot);
		// RW 0x68AF8E
		if (bonus > tv.maxBonus)
		{
			bonus = tv.maxBonus;
		}
		if (tv.minBonus > bonus)
		{
			bonus = tv.minBonus;
		}
		range = SimMath::pc24Mul(SimMath::pc24Add(bonus, 1.0f), range);
	}
	return range;
}
} // namespace

float ShroudManager::visionRange(const Object &obj)
{
	const ThingTemplate *tt = static_cast<const ThingTemplate *>(obj.getTemplate())->getFinalOverride();
	return shroudClearingRange(obj, *this, templateVision(*tt));
}

ShroudManager::ObjectSource ShroudManager::retailObjectSource()
{
	ObjectSource s;
	s.look = [](const Object &obj, const TemplateVision &tv, LookInputs &in) {
		// RW 0x694A45
		static const int revealToAll = kindIndex("REVEAL_TO_ALL"), horde = kindIndex("HORDE");
		static const int hordeMember = CombatNames::status("HORDE_MEMBER");
		in.forward = in.side = in.rear = -1.0f;
		const Player *owner = obj.getControllingPlayer();
		if (!owner)
		{
			return;
		}
		ShroudManager *sm = obj.logic().shroud();
		if (!sm)
		{
			return;
		}
		const float range = shroudClearingRange(obj, *sm, tv);
		if (range <= 0.0f)
		{
			return;
		}
		if (obj.testStatus((unsigned)hordeMember))
		{
			// RW 0x693A1A(0) the horde (itself or its container), RW 0x66352C the distance between the two bounding circles: the member does not look within 400
			const Object *h = obj.isKindOf((unsigned)horde) ? &obj : obj.getContainedBy();
			if (h && h->isKindOf((unsigned)horde))
			{
				const float dx = SimMath::subf32(obj.getPosition()->x, h->getPosition()->x);
				const float dy = SimMath::subf32(obj.getPosition()->y, h->getPosition()->y);
				const double d = SimMath::subD(SimMath::subD(SimMath::sqrtd((double)SimMath::sumSquares2(dx, dy)), (double)CombatQueries::boundingCircleRadius(obj)),
					(double)CombatQueries::boundingCircleRadius(*h));
				if (d < 400.0)
				{
					return;
				}
			}
		}
		const PlayerList &players = obj.logic().players();
		if (obj.isKindOf((unsigned)revealToAll))
		{
			in.mask = kPlayerBits;
		}
		else
		{
			// RW 0x6A8695(index, 3, 0): the owner and every player the owner counts as an ally
			std::uint32_t mask = 0;
			const int self = owner->getPlayerIndex();
			if (self >= 0 && self < 32)
			{
				mask |= 1u << self;
			}
			for (int i = 0; i < players.getPlayerCount(); ++i)
			{
				const Player *p = players.getNthPlayer(i);
				if (p && p != owner && p->getPlayerIndex() >= 0 && p->getPlayerIndex() < 32 && owner->getRelationship(p->getDefaultTeam()) == ALLIES)
				{
					mask |= 1u << p->getPlayerIndex();
				}
			}
			in.mask = mask;
		}
		in.forward = in.side = in.rear = range;
		in.side = SimMath::pc24Mul(in.side, tv.visionSide);
		in.rear = SimMath::pc24Mul(in.rear, tv.visionRear);
	};
	s.hidesWhenFogged = [](const Object &obj, int player) {
		// RW 0x68EDD0
		static const int dontHide = kindIndex("DONT_HIDE_IF_FOGGED"), hide = kindIndex("HIDE_IF_FOGGED"), immobile = kindIndex("IMMOBILE");
		if (obj.isKindOf((unsigned)dontHide))
		{
			return false;
		}
		const Player *p = obj.logic().players().getNthPlayer(player);
		const Relationship rel = p ? p->getRelationship(obj.getTeam()) : NEUTRAL;
		if (rel == NEUTRAL)
		{
			return !(obj.isKindOf((unsigned)immobile) && !obj.isKindOf((unsigned)hide));
		}
		const ShroudManager *sm = obj.logic().shroud();
		if (obj.isKindOf((unsigned)immobile) && sm && sm->hasSeen(obj, player))
		{
			return obj.isKindOf((unsigned)hide);
		}
		return true;
	};
	return s;
}
