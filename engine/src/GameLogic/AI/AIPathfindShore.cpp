// OpenBFME. GPL-3.0.
//
// Lane SHIP-1: the pathfinder's shore spot for a ship (RW 0x6EFBB8, the World Builder's getClosestPointOnLand), the rotated cell rectangle it scans (RW 0x6EC931
// with the polygon walk RW 0x6E9B92) and the geometry's bounding shape it sizes the rectangle with (RW 0xAD1B20).
//
// TARGET FACTS (RotWK game.dat, caveat S-001), read from the disassembly; the BFME2 decomp is donor evidence (tier A for each: BFME2 0x6EE7EE is a recovered,
// not byte-matching attempt; 0xABD9C0 GeometryInfo::rva006BD9C0 is byte-matched; 0x6EB4DF / 0x6E8B... have no source):
//   * RW 0x6EFBB8(pos, obj, out): TheTerrainLogic vslot 0x4C (isUnderwater) at pos; dry: out = pos, true. Wet: the bounding shape of obj's geometry (obj + 0xA8,
//     RW 0xAD1B20); the cell size x = trunc(((major + 20 (RW 0xDA246C)) * 2) * 0.1), y = trunc(((minor + 20 (RW 0xDA2470)) * 2) * 0.1) (SSE, cvttss2si); the
//     visitor (RW 0x6E9275: the pathfinder, pos, best = FLT_MAX RW 0xBD1910, found = 0); the centre cell floor(pos * 0.1 + 0.5) (x87 fmul, fadd, CRT floor, fistp);
//     RW 0x6EC931(centre, size, obj angle (+ 0x44), layer 1 (ground), visitor); out = the visitor's point; the result is its found byte.
//   * the visitor RW 0x6E92A6(cell, x, y): a cell whose type (cell + 0xC & 0xF) is 0 (CLEAR); d = ((z - z)^2 + dy^2) + dx^2 with dx = pos.x - 10x, dy = pos.y - 10y
//     (SSE in that order); d below the best: the point (10x, 10y, pos.z), best = d, found. Returns 0: every cell is visited.
//   * RW 0x6EC931: a 1 x 1 size visits the centre cell alone. Angle 0 (ucomiss equal): the axis rectangle x0 = cx - w / 2, x1 = x0 + w - 1, y0 = cy - h / 2,
//     y1 = y0 + h - 1 (integer halves truncated), corners (x0, y0) (x1, y0) (x1, y1) (x0, y1). Otherwise fsincos of the angle stored as floats, then SSE:
//     hw = w * 0.5, hh = h * 0.5; X0 = t(hh s + (cx - hw c)), Y0 = t((cy - hh c) - hw s), X1 = t((hh s + hw c) + cx), Y1 = t(hw s + (cy - hh c)),
//     X2 = t((hw c + cx) - hh s), Y2 = t((hw s + hh c) + cy), X3 = t((cx - hw c) - hh s), Y3 = t((hh c + cy) - hw s), t = cvttss2si.
//   * RW 0x6E9B92(xs, ys, n, layer, visitor): consecutive duplicate corners are dropped, then trailing corners equal to the first; under 3 corners: -1. The top corner
//     is the lowest y (the lowest x among equal y); the bottom row maxY starts at 0 and takes the corners after the first. Two edges walk from the top: one backward
//     through the corner list, one forward past the top's run of equal y. An edge (RW 0x6E8190) from x0 to x1 over dy rows: count = dy; when dy > 0, step =
//     ((x1 - x0) << 8) / dy and x = (x0 << 8) + step / 2 (integer division toward zero); a dy of 0 or less leaves x and step as they were. Each row y (from the
//     top to maxY): the cells from (xBack + 0x80) / 0x100 to (xFwd + 0x80) / 0x100, left to right, each one that exists visited (a nonzero result ends the walk
//     with it); then both edges step (x += step, count - 1) and an edge whose count reaches 0 takes its next corner pair while the next dy is not negative.
// INFERENCE (S-4161): an edge set up with dy <= 0 keeps RW's stale x / step (the stack slot of the caller); the port starts both at 0, which a rectangle can only
// meet when its backward neighbour of the top corner shares the top row (reported: Pathfinder::stops(), pathstops::kShoreScanStaleEdge).

#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/AI/AIPathfindStops.h"

#include "Common/NumericState.h"
#include "GameLogic/SimMath.h"

#include <cfloat>
#include <stdexcept>

namespace
{
// RW 0x6E8190
struct ScanEdge
{
	int x = 0;
	int step = 0;
	int count = 0;
	void setup(int x0, int x1, int dy)
	{
		count = dy;
		if (dy > 0)
		{
			step = ((x1 - x0) * 256) / dy; // shl 8, idiv
			x = x0 * 256 + step / 2;        // (step - (step >> 31)) >> 1: a division by 2 toward zero
		}
	}
};
}

void PathfindGeometry::boundingShape2D(float &major, float &minor) const
{
	// RW 0xAD1B20
	float loX = 0.0f, loY = 0.0f, hiX = 0.0f, hiY = 0.0f;
	const size_t n = shapeCount();
	for (size_t i = 0; i < n; ++i)
	{
		const PathfindShape s = shape(i);
		if (!s.active)
		{
			continue;
		}
		if (n != 1 && s.majorRadius < 1.0f && s.minorRadius < 1.0f && s.height < 1.0f)
		{
			continue; // RW 0xAD1BC0 .. 0xAD1BEE
		}
		if (s.type != PATHFIND_GEOMETRY_BOX)
		{
			major = boundingCircleRadius(); // RW 0xAD1D00: GeometryInfo + 0x10
			minor = major;
			return;
		}
		const float a = NumericState::pc24Sub(s.offsetX, s.majorRadius);
		loX = loX <= a ? loX : a;
		const float b = NumericState::pc24Sub(s.offsetY, s.minorRadius);
		loY = loY <= b ? loY : b;
		const float c = NumericState::pc24Add(s.majorRadius, s.offsetX);
		hiX = hiX > c ? hiX : c;
		const float d = NumericState::pc24Add(s.minorRadius, s.offsetY);
		hiY = hiY > d ? hiY : d;
	}
	major = NumericState::pc24Mul(NumericState::pc24Sub(hiX, loX), 0.5f);
	minor = NumericState::pc24Mul(NumericState::pc24Sub(hiY, loY), 0.5f);
}

int Pathfinder::iterateCellsInRotatedRect(const ICoord2D &center, const ICoord2D &size, float angle, PathfindLayerEnum layer,
                                          const std::function<int(PathfindCell &, int, int)> &visit)
{
	// RW 0x6EC931
	if (size.x == 1 && size.y == 1)
	{
		PathfindCell *cell = getCell(layer, center.x, center.y);
		return cell ? visit(*cell, center.x, center.y) : 0;
	}
	int xs[4], ys[4];
	if (angle == 0.0f)
	{
		const int x0 = center.x - size.x / 2, y0 = center.y - size.y / 2;
		const int x1 = x0 + size.x - 1, y1 = y0 + size.y - 1;
		xs[0] = x0; ys[0] = y0;
		xs[1] = x1; ys[1] = y0;
		xs[2] = x1; ys[2] = y1;
		xs[3] = x0; ys[3] = y1;
	}
	else
	{
		const float c = SimMath::cosf32(angle), s = SimMath::sinf32(angle); // fsincos, both stored as floats
		const float cx = (float)center.x, cy = (float)center.y;
		const float hw = SimMath::mulf32((float)size.x, 0.5f), hh = SimMath::mulf32((float)size.y, 0.5f); // RW 0xBD869C
		const float hhS = SimMath::mulf32(hh, s), hhC = SimMath::mulf32(hh, c), hwC = SimMath::mulf32(hw, c), hwS = SimMath::mulf32(hw, s);
		const float cxMinusHwC = SimMath::subf32(cx, hwC), cyMinusHhC = SimMath::subf32(cy, hhC);
		xs[0] = SimMath::truncToInt32(SimMath::addf32(hhS, cxMinusHwC));
		ys[0] = SimMath::truncToInt32(SimMath::subf32(cyMinusHhC, hwS));
		xs[1] = SimMath::truncToInt32(SimMath::addf32(SimMath::addf32(hhS, hwC), cx));
		ys[1] = SimMath::truncToInt32(SimMath::addf32(hwS, cyMinusHhC));
		xs[2] = SimMath::truncToInt32(SimMath::subf32(SimMath::addf32(hwC, cx), hhS));
		ys[2] = SimMath::truncToInt32(SimMath::addf32(SimMath::addf32(hwS, hhC), cy));
		xs[3] = SimMath::truncToInt32(SimMath::subf32(cxMinusHwC, hhS));
		ys[3] = SimMath::truncToInt32(SimMath::subf32(SimMath::addf32(hhC, cy), hwS));
	}
	return iterateCellsInPolygon(xs, ys, 4, layer, visit);
}

int Pathfinder::iterateCellsInPolygon(int *xs, int *ys, int count, PathfindLayerEnum layer, const std::function<int(PathfindCell &, int, int)> &visit)
{
	// RW 0x6E9B92: drop repeated corners (in place, as RW does)
	int n = 1;
	for (int i = 1; i < count; ++i)
	{
		if (xs[i] != xs[n - 1] || ys[i] != ys[n - 1])
		{
			xs[n] = xs[i];
			ys[n] = ys[i];
			++n;
		}
	}
	while (n > 2 && xs[0] == xs[n - 1] && ys[0] == ys[n - 1])
	{
		--n;
	}
	if (n < 3)
	{
		return -1;
	}
	int top = 0, maxY = 0;
	for (int i = 1; i < n; ++i)
	{
		if (ys[i] < ys[top] || (ys[i] == ys[top] && xs[i] < xs[top]))
		{
			top = i;
		}
		if (ys[i] > maxY)
		{
			maxY = ys[i];
		}
	}
	auto prevOf = [n](int i) { return i == 0 ? n - 1 : i - 1; };
	auto nextOf = [n](int i) { return i + 1 == n ? 0 : i + 1; };
	int y = ys[top];
	ScanEdge back, fwd;
	int backIdx = top;
	if (ys[prevOf(backIdx)] - y <= 0)
	{
		note(pathstops::kShoreScanStaleEdge);
	}
	back.setup(xs[backIdx], xs[prevOf(backIdx)], ys[prevOf(backIdx)] - y);
	backIdx = prevOf(backIdx);
	int fwdIdx = top;
	while (ys[fwdIdx] == ys[nextOf(fwdIdx)])
	{
		fwdIdx = nextOf(fwdIdx);
	}
	fwd.setup(xs[fwdIdx], xs[nextOf(fwdIdx)], ys[nextOf(fwdIdx)] - ys[fwdIdx]);
	fwdIdx = nextOf(fwdIdx);
	while (y <= maxY)
	{
		const int xEnd = (fwd.x + 0x80) / 0x100;
		for (int x = (back.x + 0x80) / 0x100; x <= xEnd; ++x)
		{
			if (PathfindCell *cell = getCell(layer, x, y))
			{
				const int r = visit(*cell, x, y);
				if (r != 0)
				{
					return r;
				}
			}
		}
		++y;
		back.x += back.step;
		--back.count;
		while (back.count == 0)
		{
			const int prev = prevOf(backIdx);
			const int dy = ys[prev] - ys[backIdx];
			if (dy < 0)
			{
				break;
			}
			back.setup(xs[backIdx], xs[prev], dy);
			backIdx = prev;
		}
		fwd.x += fwd.step;
		--fwd.count;
		while (fwd.count == 0)
		{
			const int next = nextOf(fwdIdx);
			const int dy = ys[next] - ys[fwdIdx];
			if (dy < 0)
			{
				break;
			}
			fwd.setup(xs[fwdIdx], xs[next], dy);
			fwdIdx = next;
			if (fwd.count < 0)
			{
				return 0; // RW 0x6E9E49 (never taken: the count is the dy just tested)
			}
		}
	}
	return 0;
}

bool Pathfinder::getClosestPointOnLand(const Coord3D &pos, const PathfindObject &obj, Coord3D &out)
{
	// RW 0x6EFBB8
	if (!m_terrain)
	{
		throw std::logic_error("Pathfinder::getClosestPointOnLand: no map terrain (TheTerrainLogic's water test)");
	}
	if (!m_terrain->isUnderwater(pos.x, pos.y, nullptr, nullptr))
	{
		out = pos;
		return true;
	}
	float major = 0.0f, minor = 0.0f;
	obj.getGeometry().boundingShape2D(major, minor); // RW 0xAD1B20 on obj + 0xA8
	const ICoord2D size{ SimMath::truncToInt32(SimMath::mulf32(SimMath::mulf32(SimMath::addf32(major, 20.0f), 2.0f), 0.1f)),
	                     SimMath::truncToInt32(SimMath::mulf32(SimMath::mulf32(SimMath::addf32(minor, 20.0f), 2.0f), 0.1f)) }; // RW 0xDA246C / 0xDA2470
	// RW 0x6EFC91 .. 0x6EFCD7: floor(p * 0.1 + 0.5) (x87 at 24 bits, the CRT floor, fistp)
	const ICoord2D center{ NumericState::fistp32(NumericState::floorD((double)NumericState::pc24Add(NumericState::pc24Mul(pos.x, 0.1f), 0.5f))),
	                       NumericState::fistp32(NumericState::floorD((double)NumericState::pc24Add(NumericState::pc24Mul(pos.y, 0.1f), 0.5f))) };
	float best = FLT_MAX; // RW 0xBD1910
	bool found = false;
	Coord3D spot{ 0.0f, 0.0f, 0.0f };
	iterateCellsInRotatedRect(center, size, obj.getOrientation(), LAYER_GROUND, [&](PathfindCell &cell, int x, int y) {
		if (cell.getType() != PathfindCell::CELL_CLEAR)
		{
			return 0;
		}
		const float cellX = (float)(x * 10), cellY = (float)(y * 10);
		const float dx = SimMath::subf32(pos.x, cellX), dy = SimMath::subf32(pos.y, cellY);
		const float dz = SimMath::subf32(pos.z, pos.z);
		const float d = SimMath::addf32(SimMath::addf32(SimMath::mulf32(dz, dz), SimMath::mulf32(dy, dy)), SimMath::mulf32(dx, dx));
		if (d < best)
		{
			spot = Coord3D{ cellX, cellY, pos.z };
			best = d;
			found = true;
		}
		return 0;
	});
	if (found)
	{
		out = spot;
	}
	return found;
}
