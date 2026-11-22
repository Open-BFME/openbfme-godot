// OpenBFME. GPL-3.0.
// See GameLogic/Module/Mod4Stops.h.

#include "GameLogic/Module/Mod4Stops.h"

const std::vector<std::string> &Mod4Stops::classes()
{
	// lane MODCOMPAT-MOD1 ported GeometryUpgrade (S-1423), the rebuild holes (S-1427) and PickupStuffUpdate (S-1422); PORT 17 ported ThreatFinderUpdate
	static const std::vector<std::string> names = {};
	return names;
}

std::vector<std::string> Mod4Stops::lines()
{
	// PORT 17 (issue #73) ported the lane's last class, ThreatFinderUpdate: [S-1424] is closed, and what remains of it (old saves without a finder; lane
	// THREATLEVEL-1 ported RW 0x608BCA, the threat counter actions, which is not a condition) is the module's own stop, [S-7184] (ThreatFinderUpdate::stopLines,
	// in the world's acceptance stops too)
	return {};
}
