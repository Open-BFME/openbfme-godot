// OpenBFME. GPL-3.0. MODGAP-2, RotWK 2.01 and BFME2 1.06.
// Grid traversal RW 0x8E0354 / 0x8E047D; BFME2 0x8DC096 / 0x8DC1BF,
// ClearanceTestingSlowDeathGeometrySampler.cpp. RW provenance caveat S-001.
#pragma once

#include "Common/INIDataTypes.h"
#include "GameLogic/Object/ObjectGeometry.h"
#include <vector>

namespace ClearanceDeathGeometry {
// The clearance INI declares at most one shape. For a box the half extents
// are its major/minor radii; for a sphere or cylinder the circle radius is
// its major radius. Returned points are ordered x first, then y.
std::vector<Coord2D> sample(const ObjectGeometry::Shape &shape, float angle, const Coord3D &center);
}
