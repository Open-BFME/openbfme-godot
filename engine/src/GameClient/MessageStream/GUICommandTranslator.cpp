// OpenBFME. GPL-3.0.
// See GUICommandTranslator.h.

#include "GameClient/MessageStream/GUICommandTranslator.h"

#include "GameClient/ControlBarCommands.h"
#include "GameClient/HudObjects.h"
#include "GameClient/MessageStream/CommandXlat.h"
#include "GameLogic/ActionManager.h"
#include "GameLogic/Object/Object.h"

MessageDisposition GUICommandTranslator::translate(const ClientMessage &msg)
{
	MessageDisposition disp = MessageDisposition::Keep;
	// lane SPELLRC-1: RotWK's GUI command translator (RW 0x83D7AD) follows the right button before it reads the GUI command: the press keeps its pixel and
	// clears the dragged flag (case 0xE: + 0x20, + 0x30), a pointer move beyond the drag tolerance from it sets the flag (case 3: RW 0x5ED4B9 -> + 0x30)
	// BFME2 0x004314B1 follows the left button the same way (case 4: + 0x04, + 0x14 cleared; case 3: + 0x14 set beyond the drag tolerance)
	switch (msg.type())
	{
		case CMSG_RAW_MOUSE_LEFT_BUTTON_DOWN:
			m_leftAnchor = msg.arg(0).pixel;
			m_leftDragged = false;
			break;
		case CMSG_RAW_MOUSE_RIGHT_BUTTON_DOWN:
			m_rightAnchor = msg.arg(0).pixel;
			m_rightDragged = false;
			break;
		case CMSG_RAW_MOUSE_POSITION:
			if (m_ctx.mouse.isDragging(m_leftAnchor, msg.arg(0).pixel))
			{
				m_leftDragged = true;
			}
			if (m_ctx.mouse.isDragging(m_rightAnchor, msg.arg(0).pixel))
			{
				m_rightDragged = true;
			}
			break;
		case CMSG_MOUSE_RIGHT_CLICK:
		case CMSG_MOUSE_RIGHT_DOUBLE_CLICK:
			// RW 0x83D8C2 .. 0x83DB8D: a right click that was not dragged ends the waiting command (setGUICommand(0)) and is destroyed: no order, no
			// deselection follows from it. The port's spell book power waits outside InGameUI (the hook); InGameUI's own GUI command keeps the port's
			// SelectionTranslator cancel (ZH SelectionXlat.cpp:930)
			if (!m_rightDragged && m_hudTargetCancel && m_hudTargetCancel())
			{
				return MessageDisposition::Destroy;
			}
			break;
		default:
			break;
	}
	const CommandButton *command = m_ctx.ui.getGUICommand();
	if (!command)
	{
		return disp;
	}
	if (command->m_command == GUI_COMMAND_ATTACK_MOVE)
	{
		return translateAttackMove(msg);
	}
	switch (msg.type())
	{
		case CMSG_RAW_MOUSE_LEFT_BUTTON_DOWN:
			disp = MessageDisposition::Destroy;
			break;
		case CMSG_MOUSE_LEFT_DOUBLE_CLICK:
		case CMSG_MOUSE_LEFT_CLICK:
		{
			const ICoord2D mouse = msg.arg(0).region.hi;
			// RW 0x83D7AD case 0x18 leaves SPECIAL_POWER to CommandTranslator,
			// including CONTEXTMODE_COMMAND (Gimli / Rogash Leap). Keep a single
			// target-validation and message path: the old duplicate lost options,
			// the object click's location and CommandTrigger, and ended invalid aims.
			if (command->hasOption(COMMAND_OPTION_CONTEXTMODE_COMMAND) || command->m_command == GUI_COMMAND_SPECIAL_POWER)
			{
				break;
			}
			bool complete = true;
			switch (command->m_command)
			{
				case GUI_COMMAND_GUARD:
				case GUI_COMMAND_GUARD_WITHOUT_PURSUIT:
				case GUI_COMMAND_GUARD_FLYING_UNITS_ONLY:
				{
					if (m_ctx.ui.getSelectCount() == 0)
					{
						break;
					}
					const int mode = command->m_command == GUI_COMMAND_GUARD ? GUARDMODE_NORMAL
						: command->m_command == GUI_COMMAND_GUARD_WITHOUT_PURSUIT ? GUARDMODE_GUARD_WITHOUT_PURSUIT : GUARDMODE_GUARD_FLYING_UNITS_ONLY;
					Object *target = nullptr;
					if (command->hasOption(COMMAND_OPTION_NEED_TARGET_ENEMY_OBJECT | COMMAND_OPTION_NEED_TARGET_NEUTRAL_OBJECT | COMMAND_OPTION_NEED_TARGET_ALLY_OBJECT))
					{
						target = HudObjects::pickForGuiCommand(m_ctx, mouse, *command); // RW 0x83D41A
						// ZH validUnderCursor: CommandButton::isValidObjectTarget by the relationship options
						if (target)
						{
							const Relationship rel = HudObjects::relationshipToLocal(m_ctx, *target);
							const bool ok = (rel == ENEMIES && command->hasOption(COMMAND_OPTION_NEED_TARGET_ENEMY_OBJECT))
								|| (rel == NEUTRAL && command->hasOption(COMMAND_OPTION_NEED_TARGET_NEUTRAL_OBJECT))
								|| (rel == ALLIES && command->hasOption(COMMAND_OPTION_NEED_TARGET_ALLY_OBJECT));
							if (!ok)
							{
								target = nullptr;
							}
						}
					}
					if (target)
					{
						ClientMessage &m = m_ctx.stream.append(MSG_DO_GUARD_OBJECT);
						m.appendObjectID(target->getID());
						m.appendInteger(mode);
					}
					else
					{
						Coord3D world;
						bool have = false;
						if (command->hasOption(COMMAND_OPTION_NEED_TARGET_POS))
						{
							have = m_ctx.view.screenToTerrain(mouse, m_ctx.logic, world);
						}
						else if (Object *first = m_ctx.logic.findObjectByID(m_ctx.ui.firstSelected()))
						{
							world = *first->getPosition();
							have = true;
						}
						if (have)
						{
							ClientMessage &m = m_ctx.stream.append(MSG_DO_GUARD_POSITION);
							m.appendLocation(world);
							m.appendInteger(mode);
						}
					}
					break;
				}
				case GUI_COMMAND_SET_RALLY_POINT:
				{
					Coord3D world;
					if (m_ctx.ui.firstSelected() != INVALID_ID && m_ctx.view.screenToTerrain(mouse, m_ctx.logic, world))
					{
						ClientMessage &m = m_ctx.stream.append(MSG_SET_RALLY_POINT);
						m.appendObjectID(m_ctx.ui.firstSelected());
						m.appendLocation(world);
						m.appendBoolean(false);
						m.appendObjectID(INVALID_ID);
					}
					break;
				}
				case GUI_COMMAND_PLACE_BEACON:
				{
					Coord3D world;
					if (m_ctx.view.screenToTerrain(mouse, m_ctx.logic, world))
					{
						m_ctx.stream.append(MSG_PLACE_BEACON).appendLocation(world);
					}
					break;
				}
				case GUI_COMMAND_FIRE_WEAPON:
				{
					// lane FACT-A: the target click of a FIRE_WEAPON button (Gimli's axe throw, the bombards). INFERENCE (the RotWK translator was not read; ZH
					// CommandTranslator's fire-weapon branch): an object of a relationship the options name -> MSG_DO_WEAPON_AT_OBJECT { int WeaponSlot, objectID,
					// int MaxShotsToFire }; else with NEED_TARGET_POS the ground -> MSG_DO_WEAPON_AT_LOCATION { int WeaponSlot, location, int MaxShotsToFire }. The
					// argument layouts are the ones the RotWK dispatcher reads (RW 0x77A34C / 0x77A3CB)
					if (m_ctx.ui.firstSelected() == INVALID_ID)
					{
						break;
					}
					Object *target = nullptr;
					if (command->hasOption(COMMAND_OPTION_NEED_TARGET_ENEMY_OBJECT | COMMAND_OPTION_NEED_TARGET_NEUTRAL_OBJECT | COMMAND_OPTION_NEED_TARGET_ALLY_OBJECT))
					{
						target = HudObjects::pickObject(m_ctx, mouse);
						if (target)
						{
							const Relationship rel = HudObjects::relationshipToLocal(m_ctx, *target);
							const bool ok = (rel == ENEMIES && command->hasOption(COMMAND_OPTION_NEED_TARGET_ENEMY_OBJECT))
								|| (rel == NEUTRAL && command->hasOption(COMMAND_OPTION_NEED_TARGET_NEUTRAL_OBJECT))
								|| (rel == ALLIES && command->hasOption(COMMAND_OPTION_NEED_TARGET_ALLY_OBJECT));
							if (!ok)
							{
								target = nullptr;
							}
						}
					}
					if (target)
					{
						ClientMessage &m = m_ctx.stream.append(MSG_DO_WEAPON_AT_OBJECT);
						m.appendInteger(command->m_weaponSlot);
						m.appendObjectID(target->getID());
						m.appendInteger(command->m_maxShotsToFire);
					}
					else
					{
						Coord3D world;
						if (command->hasOption(COMMAND_OPTION_NEED_TARGET_POS) && m_ctx.view.screenToTerrain(mouse, m_ctx.logic, world))
						{
							ClientMessage &m = m_ctx.stream.append(MSG_DO_WEAPON_AT_LOCATION);
							m.appendInteger(command->m_weaponSlot);
							m.appendLocation(world);
							m.appendInteger(command->m_maxShotsToFire);
						}
					}
					break;
				}
				default:
					break;
			}
			disp = MessageDisposition::Destroy;
			if (complete)
			{
				m_ctx.ui.setGUICommand(nullptr);
			}
			break;
		}
		default:
			break;
	}
	if (disp == MessageDisposition::Destroy)
	{
		m_ctx.ui.clearAttackMoveToMode();
	}
	return disp;
}

MessageDisposition GUICommandTranslator::translateAttackMove(const ClientMessage &msg)
{
	// BFME2 0x004314B1 (GUICommandTranslatorTranslateGameMessage.cpp, matched) for a waiting ATTACK_MOVE (type 0xA): a click that was not dragged is one
	// of two roles. With GlobalData m_useAlternateMouse (+ 0x5C; RotWK's default, RW 0x642A4B) the right click carries the command out and the left click
	// ends it; without it the left click carries it out and the right click ends it. The left press is destroyed; every other message is kept
	const int t = msg.type();
	const bool leftClick = (t == CMSG_MOUSE_LEFT_CLICK || t == CMSG_MOUSE_LEFT_DOUBLE_CLICK) && !m_leftDragged;
	const bool rightClick = (t == CMSG_MOUSE_RIGHT_CLICK || t == CMSG_MOUSE_RIGHT_DOUBLE_CLICK) && !m_rightDragged;
	const bool alternate = m_commands.useAlternateMouse();
	const bool cancels = alternate ? leftClick : rightClick;
	const bool carriesOut = alternate ? rightClick : leftClick;
	MessageDisposition disp = MessageDisposition::Keep;
	if (t == CMSG_RAW_MOUSE_LEFT_BUTTON_DOWN)
	{
		disp = MessageDisposition::Destroy;
	}
	else if (cancels)
	{
		disp = MessageDisposition::Destroy;
		m_ctx.ui.setGUICommand(nullptr);
	}
	else if (carriesOut)
	{
		// doAttackMoveCommand (0x004311A7, doAttackMoveCommand.cpp, matched): nothing without a first selected drawable with an object, else MSG_DO_ATTACKMOVETO
		// (0x430) { location: the terrain point under the click's region corner (hi) } and the unit voice (pickAndPlayUnitVoiceResponse 0x004DAAFD: the
		// port plays it from the logic message, HudInput's observer). It returns 1: the command ends either way
		const ICoord2D mouse = msg.arg(0).region.hi;
		Coord3D world;
		if (m_ctx.ui.firstSelected() != INVALID_ID && m_ctx.view.screenToTerrain(mouse, m_ctx.logic, world))
		{
			m_ctx.stream.append(MSG_DO_ATTACKMOVETO).appendLocation(world);
		}
		disp = MessageDisposition::Destroy;
		m_ctx.ui.setGUICommand(nullptr);
	}
	if (disp == MessageDisposition::Destroy)
	{
		m_ctx.ui.clearAttackMoveToMode();
	}
	return disp;
}
