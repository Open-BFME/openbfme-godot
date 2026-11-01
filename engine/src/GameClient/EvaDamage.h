// OpenBFME. GPL-3.0.
//
// Lane EVADAMAGE-1: the client half of RW 0x67B4B7's Eva part (see GameLogic/Object/EvaDamageNotice.h for the whole rule and its sources). Applied for every
// ClientEvent::EVA_DAMAGED in event order (DrawableManager::applyEvents): the event is picked from the victim drawable's template (Ambushed / ByFire /
// FromShroudedSource / DamagedOwner, the attacker drawable's +0x440 for the shrouded row), TheEva gets it or SecondDamageFarFromFirst (AudioApi::reportEvaDamage,
// the far frame +0x388 against the event's logic frame), then RW 0x671DFD raises the far frame of the victim's drawable or of the scanned objects' drawables.
// Client state only: nothing here reaches the simulation.

#pragma once

#include "Common/GameCommon.h"
#include "GameLogic/ObjectTypes.h"

#include <cstdint>
#include <string>
#include <vector>

struct ClientEvent;
class DrawableManager;

namespace EvaDamage
{
struct Stats
{
	std::uint64_t notices = 0;       ///< EVA_DAMAGED events applied
	std::uint64_t picked = 0;        ///< ... whose template had an event for the hit (RW: event != -1)
	std::uint64_t reported = 0;      ///< reports made to TheEva (AudioApi::reportEvaDamage named one; TheEva may still drop it as identical / quiet)
	std::uint64_t ambushed = 0, byFire = 0, shrouded = 0, damaged = 0, secondFar = 0; ///< `reported` by the event chosen
	std::uint64_t farFramesRaised = 0; ///< RW 0x671DFD calls
	std::string lastEvent;           ///< the last event name reported to TheEva ("" none)
};

// RW 0x67B596 .. 0x67B776 for one EVA_DAMAGED event
void apply(const ClientEvent &ev, DrawableManager &drawables, Stats &stats);

// the stop of the port's inferences (S-7090), for the live audio report (GameWorld.get_audio_report().stops)
std::vector<std::string> acceptanceStops();
} // namespace EvaDamage
