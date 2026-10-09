// OpenBFME. GPL-3.0.
//
// ScriptedPlayer (lane MP-1): the scripted human of the network tests, the headless peer (--script) and the two-window demo: an input source like the HUD.
// Deterministic from the shared logic state and run only by the peer that owns the player, it issues its commands through the lockstep command list:
// every 15 logic frames one of eight actions: up to 12 of the player's own mobile objects are selected (MSG_CREATE_SELECTED_GROUP) and moved around the
// player's start, sent a third of the way toward another start position (MSG_DO_ATTACKMOVETO) or stopped (MSG_DO_STOP); a unit or hero is queued at a
// structure (its CommandSet's UNIT_BUILD buttons the player can make) and a builder constructs a structure on a ring around the start, an upgrade is queued, a fortress pad builds (a wall hub when offered) and a finished
// wall hub builds a span, a science is bought and a ready spell book power is cast. Not simulation code (excluded in tools/sim/sim_policy.json):
// the floats it computes are command arguments, like a mouse click's ground position.

#pragma once

#include "GameClient/LiveGame.h"

namespace ScriptedPlayer
{
// appends this frame's commands (if any) to `out`; returns how many
unsigned issue(LiveGame &game, int playerIndex, int startPos, CommandList &out);
// lane HERO-2: the player's Create-a-Hero (installed from the game setup) is recruited at its fortress: the structure with ProductionUpdate whose command set
// has REVIVE buttons is selected and MSG_QUEUE_UNIT_CREATE { fromBuildIndex, the hero list's index of the CreateAHero template } queued (as the fortress's
// hero button sends it). Returns the messages appended (0: no hero, no fortress, or the hero list does not hold it)
unsigned recruitCreateAHero(LiveGame &game, int playerIndex, CommandList &out);
} // namespace ScriptedPlayer
