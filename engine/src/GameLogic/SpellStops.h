// OpenBFME. GPL-3.0.
//
// The acceptance stops of lane SPELL-1 (sciences, ranks, special powers), reported at runtime by RetailObjectWorld::acceptanceStops and the
// live game. Each line starts with its id; docs/STOPS.md has the evidence and the cheapest resolution.

#pragma once

#include <string>
#include <vector>

namespace SpellStops
{
std::vector<std::string> lines();
}
