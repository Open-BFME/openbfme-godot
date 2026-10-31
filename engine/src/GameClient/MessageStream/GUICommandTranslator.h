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
//
// TARGET FACTS (BFME2 GUICommandTranslator::translateGameMessage 0x004314B1, Open-BFME-2 GUICommandTranslatorTranslateGameMessage.cpp, matched): the click
// roles. A left click (or double click) counts only when the pointer stayed within the drag tolerance since the left press (+ 0x14), a right click only
// when it did since the right press (+ 0x30). One of them carries the waiting command out, the other ends it (setGUICommand(0), the click destroyed): the
// left one carries it out and the right one ends it, except for an ATTACK_MOVE (type 0xA) with GlobalData m_useAlternateMouse (+ 0x5C) set, where the
// right click carries it out (doAttackMoveCommand 0x004311A7: MSG_DO_ATTACKMOVETO 0x430 at the clicked terrain point plus the unit voice) and the left
// click ends it. The left press is destroyed while a command waits. The port applies these roles to ATTACK_MOVE; the other commands keep the port's
// left-click path and SelectionTranslator's right-button cancel (INFERENCE: RotWK 2.01 keeps BFME2's swap; its translator RW 0x83D7AD was not read
// for it).

#pragma once

#include "GameClient/HudContext.h"
#include "GameClient/MessageStream/MessageStream.h"

#include <functional>

class CommandTranslator;

class GUICommandTranslator : public MessageTranslator
{
public:
	// `commands` answers the mouse setup (CommandTranslator::useAlternateMouse, GlobalData m_useAlternateMouse); it must outlive this translator
	GUICommandTranslator(HudContext &ctx, const CommandTranslator &commands) : m_ctx(ctx), m_commands(commands) {}
	MessageDisposition translate(const ClientMessage &message) override;

	// lane SPELLRC-1: the spell book's power waiting for its target (InGameHud: InGameSpellBookModel, which the port keeps beside InGameUI's GUI command).
	// RotWK's GUI command translator (RW 0x83D7AD) ends a waiting command on a right click that was not dragged (MSG_MOUSE_RIGHT_CLICK / _DOUBLE_CLICK with
	// + 0x30 clear: setGUICommand(0) at RW 0x83DB8D, the click destroyed); a right drag leaves it waiting and the raw press reaches the LookAt translator (the
	// camera's RMB scroll). The hook answers whether such a power was waiting and cancels it
	void setHudTargetCancel(std::function<bool()> cancel) { m_hudTargetCancel = std::move(cancel); }

	// guard modes (ZH GuardMode)
	enum GuardMode
	{
		GUARDMODE_NORMAL = 0,
		GUARDMODE_GUARD_WITHOUT_PURSUIT = 1,
		GUARDMODE_GUARD_FLYING_UNITS_ONLY = 2
	};

private:
	MessageDisposition translateAttackMove(const ClientMessage &msg); // 0x004314B1's click roles for ATTACK_MOVE

	HudContext &m_ctx;
	const CommandTranslator &m_commands;
	std::function<bool()> m_hudTargetCancel; ///< lane SPELLRC-1
	ICoord2D m_leftAnchor{ 0, 0 };           ///< 0x004314B1 + 0x04: the left press's pixel
	bool m_leftDragged = false;              ///< 0x004314B1 + 0x14: the pointer left the drag tolerance since the left press
	ICoord2D m_rightAnchor{ 0, 0 };          ///< lane SPELLRC-1: RW 0x83D7AD + 0x20: the right press's pixel
	bool m_rightDragged = false;             ///< lane SPELLRC-1: RW 0x83D7AD + 0x30: the pointer left the drag tolerance since the right press
};
