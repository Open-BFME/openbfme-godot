// OpenBFME. GPL-3.0.
// See GUICommandTranslator.h.

#include "GameClient/MessageStream/GUICommandTranslator.h"

#include "GameClient/ControlBarCommands.h"
#include "GameClient/HudObjects.h"
#include "GameLogic/Object/Object.h"

MessageDisposition GUICommandTranslator::translate(const ClientMessage &msg)
{
	MessageDisposition disp = MessageDisposition::Keep;
	const CommandButton *command = m_ctx.ui.getGUICommand();
	if (!command)
	{
		return disp;
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
				case GUI_COMMAND_ATTACK_MOVE:
				{
					Coord3D world;
					if (m_ctx.ui.firstSelected() != INVALID_ID && m_ctx.view.screenToTerrain(mouse, m_ctx.logic, world))
					{
						m_ctx.stream.append(MSG_DO_ATTACKMOVETO).appendLocation(world);
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
