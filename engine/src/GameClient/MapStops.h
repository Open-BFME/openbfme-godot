// OpenBFME. GPL-3.0.
//
// The acceptance stops of the maps-and-terrain lane (docs/STOPS.md, IDs S-030..S-039 and S-060 (the lane's extended range is S-060..S-069; S-040..S-049 belong to the oracle lane)). Each stop is
// reported by the code (the terrain builder lists the ones that apply to a render; the map reader lists
// the loader ones), pinned by a test (tests/test_map_stops.cpp checks this table against the register),
// and blocks only the item named here.

#pragma once

#include <vector>

struct MapStop
{
	const char *id;
	const char *area;
	const char *gap;
};

namespace MapStops
{
const std::vector<MapStop> &all();
// Stops that apply to every terrain render by this lane.
const std::vector<const MapStop *> &terrainRender();
// Stops that apply to every use of the ground-height queries (TerrainLogic): S-060.
const std::vector<const MapStop *> &terrainLogic();
// Stops that apply to every map load.
const std::vector<const MapStop *> &mapLoad();
const MapStop *find(const char *id);
} // namespace MapStops
