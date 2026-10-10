// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// The control group messages (lane HUD-1): MSG_CREATE_TEAM0..9, MSG_SELECT_TEAM0..9, MSG_ADD_TEAM0..9, MSG_ADD_TO_TEAM0..9 and MSG_AREA_SELECTION as GameLogicDispatch handlers.
//
// DONOR FACTS (ZH GameLogicDispatch.cpp:1854-1925, Player.cpp:3661-3736 processCreateTeamGameMessage / processSelectTeamGameMessage / processAddTeamGameMessage):
// CREATE_TEAM n clears the player's hotkey squad n and fills it with the objects named by the arguments (each first removed from every other squad);
// SELECT_TEAM n replaces the player's current selection with the squad's live objects; ADD_TEAM n appends them. The squads are Player state (hashed).
// TARGET FACTS (lane INPUT-1): the dispatcher RW 0x779A3D sends 1006 .. 1015 to RW 0x6AD555, 1016 .. 1025 to RW 0x6AD5CA, 1026 .. 1035 to RW 0x6AD677
// (BFME2 decomp byte-matched: Player::processCreateTeamGameMessage / processSelectTeamGameMessage / processAddTeamGameMessage, ZH's semantics) and
// MSG_ADD_TO_TEAM0..9 (1138 .. 1147, RotWK only, RW 0x77CDDB) to RW 0x6AD722: the named objects join squad n without clearing it. MSG_AREA_SELECTION
// (1060) changes no logic state (ZH GameLogicDispatch has no case for it: it only travels the command list); a handler accepts it so it is not counted unhandled.

#pragma once

#include "GameLogic/GameLogicDispatch.h"

class Object;

class PlayerCommands
{
public:
	void registerHandlers(GameLogicDispatch &dispatcher);
	// RotWK's Object::isSelectable (RW 0x68DE58), the filter of Squad::getLiveObjects (RW 0x8DB103): what a control group recalls (logic and HUD)
	static bool isSelectable(const Object &object);
};
