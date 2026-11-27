// OpenBFME. GPL-3.0. See ClearanceTestingSlowDeathGeometry.h.
#include "GameLogic/Module/ClearanceTestingSlowDeathGeometry.h"
#include "GameLogic/SimMath.h"

#include <algorithm>
#include <array>
#include <cstdint>

namespace {
using SimMath::addf32;
using SimMath::mulf32;
using SimMath::subf32;

// RW 0x67EF1F; the sampler accepts either triangle, including its edges.
bool triangle(const Coord2D &a, const Coord2D &b, const Coord2D &c, float x, float y) {
    const float area = subf32(mulf32(subf32(b.x, a.x), subf32(c.y, a.y)),
                             mulf32(subf32(b.y, a.y), subf32(c.x, a.x)));
    if (area == 0.0f) {
        // RW 0x67F09F: a degenerate triangle reduces to its longest edge,
        // including a single point when all three corners coincide.
        const auto length2 = [](const Coord2D &p, const Coord2D &q) {
            const float dx = subf32(q.x, p.x), dy = subf32(q.y, p.y);
            return addf32(mulf32(dx, dx), mulf32(dy, dy));
        };
        const Coord2D *p = &a, *q = &b;
        float longest = length2(a, b);
        const float bc = length2(b, c), ca = length2(c, a);
        if (bc > longest) { p = &b; q = &c; longest = bc; }
        if (ca > longest) { p = &c; q = &a; longest = ca; }
        const Coord2D point{x, y};
        if (longest == 0.0f) return length2(*p, point) == 0.0f;
        const float dx = subf32(q->x, p->x), dy = subf32(q->y, p->y);
        const float px = subf32(x, p->x), py = subf32(y, p->y);
        return subf32(mulf32(dx, py), mulf32(dy, px)) == 0.0f
            && length2(*p, point) <= longest && length2(*q, point) <= longest;
    }
    const float d1 = subf32(mulf32(subf32(b.x, a.x), subf32(y, a.y)), mulf32(subf32(b.y, a.y), subf32(x, a.x)));
    const float d2 = subf32(mulf32(subf32(c.x, b.x), subf32(y, b.y)), mulf32(subf32(c.y, b.y), subf32(x, b.x)));
    const float d3 = subf32(mulf32(subf32(a.x, c.x), subf32(y, c.y)), mulf32(subf32(a.y, c.y), subf32(x, c.x)));
    return !((d1 < 0.0f || d2 < 0.0f || d3 < 0.0f) && (d1 > 0.0f || d2 > 0.0f || d3 > 0.0f));
}
}

std::vector<Coord2D> ClearanceDeathGeometry::sample(const ObjectGeometry::Shape &shape, float angle, const Coord3D &center) {
    float minX, maxX, minY, maxY;
    std::array<Coord2D, 4> corners{};
    const bool box = shape.type == ObjectGeometry::SHAPE_BOX;
    if (box) {
        const float c = SimMath::cosf32(angle), s = SimMath::sinf32(angle);
        const float mc = mulf32(shape.majorRadius, c), ms = mulf32(shape.majorRadius, s);
        const float nc = mulf32(shape.minorRadius, c), ns = mulf32(shape.minorRadius, s);
        corners[0] = { subf32(subf32(center.x, mc), ns), subf32(addf32(center.y, nc), ms) };
        corners[1] = { subf32(addf32(mc, center.x), ns), addf32(addf32(center.y, ms), nc) };
        corners[2] = { addf32(addf32(ns, mc), center.x), addf32(subf32(center.y, nc), ms) };
        corners[3] = { addf32(subf32(center.x, mc), ns), subf32(subf32(center.y, nc), ms) };
        minX = maxX = corners[0].x;
        minY = maxY = corners[0].y;
        for (const Coord2D &p : corners) {
            minX = std::min(minX, p.x); maxX = std::max(maxX, p.x);
            minY = std::min(minY, p.y); maxY = std::max(maxY, p.y);
        }
    } else {
        minX = subf32(center.x, shape.majorRadius); maxX = addf32(center.x, shape.majorRadius);
        minY = subf32(center.y, shape.majorRadius); maxY = addf32(center.y, shape.majorRadius);
    }
    const int xmin = SimMath::floorToInt(mulf32(minX, 0.1f));
    const int xmax = SimMath::floorToInt(mulf32(maxX, 0.1f));
    const int ymin = SimMath::floorToInt(mulf32(minY, 0.1f));
    const int ymax = SimMath::floorToInt(mulf32(maxY, 0.1f));
    std::vector<Coord2D> result;
    for (std::int64_t xi = xmin; xi <= xmax; ++xi) {
        for (std::int64_t yi = ymin; yi <= ymax; ++yi) {
            const float x = mulf32(SimMath::sseFromInt32((int)xi), 10.0f);
            const float y = mulf32(SimMath::sseFromInt32((int)yi), 10.0f);
            bool inside;
            if (box) {
                inside = triangle(corners[0], corners[1], corners[3], x, y)
                    || triangle(corners[1], corners[2], corners[3], x, y);
            } else {
                const float dx = subf32(x, center.x), dy = subf32(y, center.y);
                inside = addf32(mulf32(dx, dx), mulf32(dy, dy)) < mulf32(shape.majorRadius, shape.majorRadius);
            }
            if (inside) result.push_back(Coord2D{x, y});
        }
    }
    return result;
}
