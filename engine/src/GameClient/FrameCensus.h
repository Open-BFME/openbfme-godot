// OpenBFME. GPL-3.0.
//
// FrameCensus (lane MP-3, the owner's 8-player measurements): what a logic frame holds, counted on the simulation owner right after the frame when the
// installed frame driver asks for it (LiveGameFrameDriver::Capture::census): living battalions (objects of KindOf HORDE: the horde containers), living
// troops (KindOf INFANTRY, CAVALRY or MONSTER: horde members count one by one) and every object. Diagnostics only: it reads the logic between frames,
// writes nothing, and nothing reads it back into the game (excluded from the simulation audit in tools/sim/sim_policy.json).

#pragma once

class GameLogic;

namespace FrameCensus
{
struct Counts
{
	int battalions = 0, troops = 0, objects = 0;
};
Counts count(const GameLogic &logic);
} // namespace FrameCensus
