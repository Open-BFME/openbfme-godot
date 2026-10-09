// OpenBFME. GPL-3.0.
//
// The garrison messages of the dispatcher (lane GARRISON-1): MSG_ENTER (1068, RW 0x77AC26), MSG_EVACUATE (1054, RW 0x77AD4E) and MSG_EXIT (1053, RW 0x77AC92),
// executed on the one dispatch path (GameLogicDispatch::registerHandler). See GarrisonCommands.cpp for each case's target facts. MSG_EVACUATE_CONTESTERS (1055)
// belongs to the contested buildings (not ported: S-208 counts it).

#pragma once

class GameLogicDispatch;

namespace GarrisonCommands
{
void registerHandlers(GameLogicDispatch &d);

struct Stats
{
	unsigned long long enters = 0, enterOrders = 0, evacuates = 0, exits = 0, rejected = 0;
};
// process-wide counters (diagnostics, not simulation state)
const Stats &stats();
} // namespace GarrisonCommands
