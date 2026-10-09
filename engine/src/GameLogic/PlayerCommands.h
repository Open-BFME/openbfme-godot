// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// The control group messages (lane HUD-1): MSG_CREATE_TEAM0..9, MSG_SELECT_TEAM0..9, MSG_ADD_TEAM0..9 and MSG_AREA_SELECTION as GameLogicDispatch handlers.
//
// DONOR FACTS (ZH GameLogicDispatch.cpp:1854-1925, Player.cpp:3661-3736 processCreateTeamGameMessage / processSelectTeamGameMessage / processAddTeamGameMessage):
// CREATE_TEAM n clears the player's hotkey squad n and fills it with the objects named by the arguments (each first removed from every other squad);
// SELECT_TEAM n replaces the player's current selection with the squad's live objects; ADD_TEAM n appends them. The squads are Player state (hashed).
// TARGET: the dispatcher's jump table covers 1006 .. 1035 the same way (GameMessage.h numbering); the handler bodies of RotWK were not read (stop S-281):
// ZH's semantics are used. MSG_ADD_TO_TEAM0..9 (1138 .. 1147, RotWK only) has an unknown meaning and stays unhandled (counted by S-208). MSG_AREA_SELECTION
// (1060) changes no logic state (ZH GameLogicDispatch has no case for it: it only travels the command list); a handler accepts it so it is not counted unhandled.

#pragma once

#include "GameLogic/GameLogicDispatch.h"

class PlayerCommands
{
public:
	void registerHandlers(GameLogicDispatch &dispatcher);
};
