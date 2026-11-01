// OpenBFME. GPL-3.0.
// See GameLogic/Module/Mod4Stops.h.

#include "GameLogic/Module/Mod4Stops.h"

const std::vector<std::string> &Mod4Stops::classes()
{
	static const std::vector<std::string> names = { "ThreatFinderUpdate" }; // lane MODCOMPAT-MOD1 ported GeometryUpgrade (S-1423), the rebuild holes (S-1427) and PickupStuffUpdate (S-1422)
	return names;
}

std::vector<std::string> Mod4Stops::lines()
{
	return {
		"[S-1424] ThreatFinderUpdate (lane MOD-4, not ported; create RW 0x64F96A, constructor RW 0x7EDD3D, table RW 0xC4C320: DefaultRadius + 0x08): only a "
		"map object gets a finder: behaviour slot 0xB4 (RW 0x7EDECE, from Object RW 0x694692 with the map object's properties) makes an AIThreatFinder (RW "
		"0x7EDDFB) named by the object's name, the radius the property's real or DefaultRadius, registered by name in TheThreatFinderManager (RW 0xDE8888, RW "
		"0x7EE33A); the update (RW 0x7EDDB6) copies the object's position into it every frame. Its readers are the script action RW 0x7C68AA and condition "
		"RW 0x608BCA (lookup RW 0x7EE260), not ported (the script lanes): the finder would have no reader",
	};
}
