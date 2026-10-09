// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// GUICommandTranslator (ZH Include/GameClient/GUICommandTranslator.h, Source/GameClient/MessageStream/GUICommandTranslator.cpp), lane HUD-1: while a command button
// waits for its target (InGameUI::getGUICommand), the next left click carries it out: attack move, guard (position or object), set rally point, place
// beacon, evacuate at a position. A left button down is eaten; a click ends the command mode, a context command (special power, fire weapon)
// is left to the CommandTranslator.
//
// DONOR FACTS (ZH GUICommandTranslator.cpp:60-517): doAttackMoveCommand -> MSG_DO_ATTACKMOVETO(location); doGuardCommand -> MSG_DO_GUARD_OBJECT(object, mode) when the
// command needs an object target and one is under the cursor, else MSG_DO_GUARD_POSITION(location, mode) with the cursor's terrain point (or the first
// selected object's position when the command has no position target); doSetRallyPointCommand -> MSG_SET_RALLY_POINT(first selected object, location).
// TARGET: the RotWK set rally point message carries { object id, location, global byte, target object id } (RW 0x77A26C, GameLogicDispatch.h): the two extra
// arguments are sent as false and no object.

#pragma once

#include "GameClient/HudContext.h"
#include "GameClient/MessageStream/MessageStream.h"

class GUICommandTranslator : public MessageTranslator
{
public:
	explicit GUICommandTranslator(HudContext &ctx) : m_ctx(ctx) {}
	MessageDisposition translate(const ClientMessage &message) override;

	// guard modes (ZH GuardMode)
	enum GuardMode
	{
		GUARDMODE_NORMAL = 0,
		GUARDMODE_GUARD_WITHOUT_PURSUIT = 1,
		GUARDMODE_GUARD_FLYING_UNITS_ONLY = 2
	};

private:
	HudContext &m_ctx;
};
