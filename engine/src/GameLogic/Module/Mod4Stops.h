// OpenBFME. GPL-3.0.
//
// Lane MOD-4 (QA-1 U2): the modules of the skirmish's unported list this lane read in the RotWK binary (caveat S-001) but did not port. Every class of
// the lane has since been ported - MODCOMPAT-MOD1's PickupStuffUpdate (its stop S-1422 is PickupStuffUpdate::stopLines) and its siblings, and PORT 17's
// ThreatFinderUpdate (its stop S-1424 is closed; what PORT 17 still does not port is S-7184, ThreatFinderUpdate::stopLines) - so classes() is empty. The
// functions stay: the lane's shape is what a next unported class of it is added to.

#pragma once

#include <string>
#include <vector>

namespace Mod4Stops
{
// the module classes these stops cover, in stop order (each must stay an UnportedModule while its stop stands: test_mod4_modules.cpp pins it). Empty since
// PORT 17 ported the last one, ThreatFinderUpdate
const std::vector<std::string> &classes();
// the lane's remaining stop lines. Empty: [S-1424] (ThreatFinderUpdate) is closed by PORT 17, whose own stop is ThreatFinderUpdate::stopLines ([S-7184])
std::vector<std::string> lines();
} // namespace Mod4Stops
