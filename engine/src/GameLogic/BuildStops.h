// OpenBFME. GPL-3.0.
//
// The acceptance stops of lane BUILD-1 (S-300 .. S-307) as one list: construction, the castle layouts, the placement, the dozer, the build commands. GameLogic::report() adds them
// to every report (the starting fortress, the placement and the construction exist in every live game).

#pragma once

#include <string>
#include <vector>

namespace BuildStops
{
std::vector<std::string> lines();
}
