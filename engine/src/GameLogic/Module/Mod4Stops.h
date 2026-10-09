// OpenBFME. GPL-3.0.
//
// Lane MOD-4 (QA-1 U2): the modules of the skirmish's unported list this lane read in the RotWK binary (caveat S-001) but does not port. Each stays an
// UnportedModule (S-140 counts its objects in the live report's `unported_modules`); its facts and the reason it is not ported are a registered stop, reported
// with the retail object world's stops (RetailObjectWorld::acceptanceStops).

#pragma once

#include <string>
#include <vector>

namespace Mod4Stops
{
// the module classes these stops cover, in stop order (each must stay an UnportedModule while its stop stands: test_mod4_modules.cpp pins it)
const std::vector<std::string> &classes();
// [S-1422] PickupStuffUpdate, [S-1423] GeometryUpgrade, [S-1424] ThreatFinderUpdate, [S-1427] RebuildHoleExposeDie / RebuildHoleBehavior
std::vector<std::string> lines();
} // namespace Mod4Stops
