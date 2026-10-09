// OpenBFME. GPL-3.0.
// See CommandXlat.h.

#include "GameClient/MessageStream/CommandXlat.h"

#include "Common/Team.h"
#include "GameClient/Radar.h"
#include "GameLogic/AI/AIGarrisonStates.h"

#include "Common/BuildAssistant.h"
#include "Common/Player.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameClient/HudObjects.h"
#include "GameLogic/GameMessage.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/Object/Object.h"

#include <cstdlib>

namespace
{
bool isSystemMessage(const ClientMessage &m)
{
	// ZH isSystemMessage (CommandXlat.cpp:5163): what still works while input is disabled (the options menu, a game data reset)
	return m.type() == CMSG_META_OPTIONS;
}

const char *metaNameOf(int type)
{
	return ClientMessageMetaName(type);
}
} // namespace

bool CommandTranslator::areSelectedObjectsControllable() const
{
	Object *first = m_ctx.logic.findObjectByID(m_ctx.ui.firstSelected());
	return first && !first->isDestroyed() && HudObjects::isLocallyControlled(m_ctx, *first);
}

bool CommandTranslator::objectCanAttack(Object &obj)
{
	const ThingTemplate *tt = obj.getTemplate();
	if (tt)
	{
		auto it = m_weaponCache.find(tt);
		if (it == m_weaponCache.end())
		{
			it = m_weaponCache.emplace(tt, ObjectTemplateInfoBuilder::build(*tt).canPossiblyHaveAnyWeapon).first;
		}
		if (it->second)
		{
			return true;
		}
	}
	if (ContainModuleInterface *contain = obj.getContain())
	{
		if (const ContainModuleInterface::ContainedItemsList *items = contain->getContainedItemsList())
		{
			for (Object *member : *items)
			{
				if (member && member != &obj && objectCanAttack(*member))
				{
					return true;
				}
			}
		}
	}
	return false;
}

bool CommandTranslator::selectionCanSetRallyPoint()
{
	// ZH ActionManager::canSetRallyPoint through InGameUI::canSelectedObjectsDoAction(SET_RALLY_POINT, NULL, SELECTION_ALL): every selected object is ours and
	// has a production queue with an exit (the producer's ExitInterface, RW 0x68BB14)
	if (m_ctx.ui.selected().empty())
	{
		return false;
	}
	for (ObjectID id : m_ctx.ui.selected())
	{
		Object *o = m_ctx.logic.findObjectByID(id);
		if (!o || !HudObjects::isLocallyControlled(m_ctx, *o) || !o->getProductionUpdate() || !o->getObjectExitInterface())
		{
			return false;
		}
	}
	return true;
}

bool CommandTranslator::selectionCanAttack()
{
	for (ObjectID id : m_ctx.ui.selected())
	{
		if (Object *o = m_ctx.logic.findObjectByID(id))
		{
			if (objectCanAttack(*o))
			{
				return true;
			}
		}
	}
	return false;
}

void CommandTranslator::hint(const char *cursor)
{
	m_ctx.ui.setCursor(cursor);
}

int CommandTranslator::issueMove(Object *target, const Coord3D *pos, EvaluateType type)
{
	// ZH issueMoveToLocationCommand (CommandXlat.cpp:865)
	int msgType = 0;
	if (m_teamExists)
	{
		const bool forceAttackable = target && target->isKindOfName("FORCEATTACKABLE");
		if (m_ctx.ui.isInWaypointMode())
		{
			msgType = MSG_ADD_WAYPOINT;
		}
		else if (m_ctx.ui.isInAttackMoveToMode())
		{
			msgType = MSG_DO_ATTACKMOVETO;
		}
		else if (m_ctx.ui.isInForceMoveToMode())
		{
			msgType = MSG_DO_FORCEMOVETO;
		}
		else if (m_ctx.ui.isInForceAttackMode() && forceAttackable)
		{
			msgType = MSG_DO_ATTACK_OBJECT;
		}
		else
		{
			msgType = MSG_DO_MOVETO;
		}
		if (type == EvaluateType::DoCommand && pos)
		{
			ClientMessage &m = m_ctx.stream.append(msgType);
			if (msgType == MSG_DO_ATTACK_OBJECT)
			{
				m.appendObjectID(target->getID());
			}
			else
			{
				m.appendLocation(*pos);
			}
		}
	}
	return msgType;
}

int CommandTranslator::evaluateForceAttack(Object *target, const Coord3D *pos, EvaluateType type)
{
	// ZH evaluateForceAttack (CommandXlat.cpp:1339): a selection that can fire may force attack an object or the ground
	if (!target && !pos)
	{
		return 0;
	}
	if (!selectionCanAttack())
	{
		if (type == EvaluateType::DoHint)
		{
			hint(MouseCursorName::GenericInvalid);
		}
		return 0;
	}
	int msgType = target ? MSG_DO_FORCE_ATTACK_OBJECT : MSG_DO_FORCE_ATTACK_GROUND;
	if (type == EvaluateType::DoCommand)
	{
		ClientMessage &m = m_ctx.stream.append(msgType);
		if (target)
		{
			m.appendObjectID(target->getID());
		}
		else
		{
			m.appendLocation(*pos);
		}
	}
	else if (type == EvaluateType::DoHint)
	{
		hint(target ? MouseCursorName::ForceAttackObj : MouseCursorName::ForceAttackGround);
	}
	return msgType;
}

int CommandTranslator::evaluateContextCommand(Object *obj, const Coord3D *pos, EvaluateType type)
{
	// ZH evaluateContextCommand (CommandXlat.cpp:1425), in its order, for the cases whose logic exists (stop S-286)
	if (obj && obj->isKindOfName("MINE") && HudObjects::isLocallyControlled(m_ctx, *obj))
	{
		obj = nullptr;
	}
	if (m_ctx.ui.isInForceMoveToMode())
	{
		obj = nullptr;
	}
	int msgType = 0;
	// the game prefers selection events: no command
	if (obj && HudObjects::isLocallyControlled(m_ctx, *obj) && m_ctx.ui.isInPreferSelectionMode())
	{
		return msgType;
	}
	if (!areSelectedObjectsControllable())
	{
		return msgType;
	}
	if (m_ctx.ui.isInWaypointMode())
	{
		// override every other command with waypoint commands
		if (type == EvaluateType::DoHint)
		{
			hint(MouseCursorName::Move);
			return MSG_ADD_WAYPOINT;
		}
		return pos ? issueMove(obj, pos, type) : 0;
	}
	// lane GARRISON-1 (ZH evaluateContextCommand's ACTIONTYPE_ENTER_OBJECT): a click on a container some selected object may enter (ActionManager::canEnterObject,
	// RW 0x82CBD5, mode 0) is MSG_ENTER (argument 0 the first selected object, argument 1 the container: the logic case RW 0x77AC26 reads argument 1); the cursor
	// is EnterFriendly. INFERENCE (S-286): RotWK's order of the context checks is not read; the enter check comes before the attack check here (an enemy
	// container is not enterable by the garrison rules)
	if (obj && !m_ctx.ui.isInForceAttackMode() && obj->getContain())
	{
		Object *enterer = nullptr;
		for (ObjectID id : m_ctx.ui.selected())
		{
			Object *o = m_ctx.logic.findObjectByID(id);
			if (o && o != obj && GarrisonRules::canEnterObject(*o, obj, CMD_FROM_PLAYER, 0, false))
			{
				enterer = o;
				break;
			}
		}
		if (enterer)
		{
			msgType = MSG_ENTER;
			if (type == EvaluateType::DoCommand)
			{
				ClientMessage &m = m_ctx.stream.append(msgType);
				m.appendObjectID(enterer->getID());
				m.appendObjectID(obj->getID());
			}
			else if (type == EvaluateType::DoHint)
			{
				hint(MouseCursorName::EnterFriendly);
			}
			return msgType;
		}
	}
	if (obj && !m_ctx.ui.isInForceAttackMode() && HudObjects::relationshipToLocal(m_ctx, *obj) == ENEMIES && selectionCanAttack())
	{
		msgType = MSG_DO_ATTACK_OBJECT;
		if (type == EvaluateType::DoCommand)
		{
			m_ctx.stream.append(msgType).appendObjectID(obj->getID());
		}
		else if (type == EvaluateType::DoHint)
		{
			hint(MouseCursorName::AttackObj);
		}
		return msgType;
	}
	// lane QA-1 (stop S-1201): ZH evaluateContextCommand's ACTIONTYPE_RESUME_CONSTRUCTION and ACTIONTYPE_REPAIR_OBJECT (CommandXlat.cpp:1712 / 1785, SELECTION_ANY): a
	// click with a builder selected on our structure that is still rising resumes it (MSG_RESUME_CONSTRUCTION, the logic case RW 0x77AEB6: every selected dozer of
	// the issuer, DozerAIUpdate::resumeConstruction), on our damaged finished structure repairs it (MSG_DO_REPAIR, S-656); argument 0 the structure. Without it a
	// building whose builder was sent elsewhere could never be finished by a player (the scripted player's Dwarven and Men sites stopped at 7 .. 77 %).
	// DONOR (ZH ActionManager::canResumeConstructionOf / canRepairObject): a DOZER of the same controlling player; resume: the target is UNDER_CONSTRUCTION;
	// repair: the target is a finished STRUCTURE below its maximum health. INFERENCE: RotWK's ActionManager checks and the order of its context checks are not read.
	if (obj && !m_ctx.ui.isInForceAttackMode() && obj->isKindOfName("STRUCTURE") && HudObjects::isLocallyControlled(m_ctx, *obj) && !HudObjects::isEffectivelyDead(*obj))
	{
		bool dozer = false;
		for (ObjectID id : m_ctx.ui.selected())
		{
			const Object *o = m_ctx.logic.findObjectByID(id);
			dozer = dozer || (o && o != obj && o->isKindOfName("DOZER") && o->getControllingPlayer() == obj->getControllingPlayer());
		}
		const BodyModuleInterface *body = obj->getBodyModule();
		const bool rising = obj->isUnderConstruction();
		const bool damaged = !rising && body && body->getHealth() < body->getMaxHealth();
		if (dozer && (rising || damaged))
		{
			msgType = rising ? MSG_RESUME_CONSTRUCTION : MSG_DO_REPAIR;
			if (type == EvaluateType::DoCommand)
			{
				m_ctx.stream.append(msgType).appendObjectID(obj->getID());
			}
			else if (type == EvaluateType::DoHint)
			{
				hint(rising ? MouseCursorName::ResumeConstruction : MouseCursorName::DoRepair);
			}
			return msgType;
		}
	}
	// a click on the ground with only producers selected sets their rally points (ZH ACTIONTYPE_SET_RALLY_POINT, CommandXlat.cpp:2196): one message per object
	if (pos && !obj && selectionCanSetRallyPoint())
	{
		msgType = MSG_SET_RALLY_POINT;
		if (type == EvaluateType::DoCommand)
		{
			for (ObjectID id : m_ctx.ui.selected())
			{
				ClientMessage &m = m_ctx.stream.append(msgType);
				m.appendObjectID(id);
				m.appendLocation(*pos);
				m.appendBoolean(false); // the RotWK message's global flag and target object (RW 0x77A26C)
				m.appendObjectID(INVALID_ID);
			}
		}
		else if (type == EvaluateType::DoHint)
		{
			hint(MouseCursorName::SetRallyPoint);
		}
		return msgType;
	}
	// everything else: move to the location (ZH's final else branch, CommandXlat.cpp:2252). A click on an object of ours that has nothing more specific to do is
	// no command (it is a selection): only a location click moves.
	if (pos)
	{
		if (type == EvaluateType::DoHint)
		{
			msgType = m_teamExists ? (m_ctx.ui.isInAttackMoveToMode() ? MSG_DO_ATTACKMOVETO : MSG_DO_MOVETO) : 0;
			if (msgType != 0)
			{
				hint(msgType == MSG_DO_ATTACKMOVETO ? MouseCursorName::AttackMove : MouseCursorName::Move);
			}
		}
		else if (!obj)
		{
			msgType = issueMove(obj, pos, type);
		}
	}
	return msgType;
}

bool CommandTranslator::isClick(const ICoord2D &anchor, const ICoord2D &lift, int downMs, int upMs) const
{
	// ZH Mouse::isClick: within the drag tolerance in pixels and in time
	return std::abs(anchor.x - lift.x) <= m_ctx.mouse.dragTolerance && std::abs(anchor.y - lift.y) <= m_ctx.mouse.dragTolerance
		&& (upMs - downMs) <= m_ctx.mouse.dragToleranceMS;
}

void CommandTranslator::viewHomeBase()
{
	// lane QA-1 (stop S-1202): the H key (CommandMap VIEW_HOME_BASE, meta type 0x61, name table RW 0xBF0890). TARGET FACT (RotWK game.dat, S-001 caveat; Sol's review
	// r1 found the route): the key case RW 0x81FD5B (the meta handler RW 0x81F8D8, type 0x61) calls RW 0x6AC722(1, null), which walks the local player's object list
	// with RW 0x6ABABD / 0x6ABA4E and keeps, among its STRUCTURE objects (template kind bit 7), the best one by RW 0x6AAE79(candidate, best) > 0 (strictly better):
	// a COMMANDCENTER (kind bit 17) beats any other structure; of two command centres the lower object id wins; of two other structures the larger
	// calcCostToBuild(owner, no producer, no override) wins (a tie keeps the earlier). The view then looks at it (TheTacticalView vslot 0x54). The walk skips an
	// object whose status word (+ 0x458) has bit 0 set, and with the argument 1 one with bit 3 set: the two bits are not identified; INFERENCE: bit 0 is taken as
	// destroyed, bit 3 is not ported. The look-at position is the object's position (inference: the argument of RW 0x81FD8A was not traced)
	const Player *local = m_ctx.localPlayer();
	if (!local)
	{
		return;
	}
	const Object *best = nullptr;
	int bestCost = 0;
	for (const Object *o = m_ctx.logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getControllingPlayer() != local || o->isDestroyed() || !o->isKindOfName("STRUCTURE"))
		{
			continue;
		}
		const bool cc = o->isKindOfName("COMMANDCENTER");
		const int cost = o->getTemplate() ? BuildAssistant::calcCostToBuild(*o->getTemplate(), local, nullptr, -1) : 0;
		bool better = best == nullptr;
		if (!better)
		{
			const bool bestCC = best->isKindOfName("COMMANDCENTER");
			if (cc && bestCC)
			{
				better = o->getID() < best->getID(); // RW 0x6AAE79: best.id - candidate.id > 0
			}
			else if (cc != bestCC)
			{
				better = cc;
			}
			else
			{
				better = cost > bestCost;
			}
		}
		if (better)
		{
			best = o;
			bestCost = cost;
		}
	}
	if (best)
	{
		m_ctx.view.lookAt(*best->getPosition());
	}
}

// lane HUD-4: SELECT_HERO (Ctrl+H). TARGET FACTS (RotWK game.dat, caveat S-001), RW 0x81FDA2 .. 0x81FE76:
//   * with a selection, the first selected drawable's object (TheInGameUI vslot 0x12C, drawable + 0xFC) is the anchor when its template is KindOf HERO
//     (template + 0x113 bit 2, kind-of mask at + 0x108);
//   * RW 0x81DABC: the local player's objects (RW 0x6ABABD: the player's team list + 0x34C, each team's members, RW 0x7A18D7) are walked for the first HERO
//     after the anchor (callback RW 0x81DA8C); none after it: the walk starts again from the first (a single hero is found again);
//   * a hero inside a container (object + 0x27C) stands for its container; no drawable: nothing;
//   * TheInGameUI deselects everything (vslot 0x110), MSG_CREATE_SELECTED_GROUP (1001) with the boolean 1 and the object's id, the drawable is selected
//     (vslot 0x108) and TheTacticalView looks at the drawable's position (vslot 0x54).
// INFERENCE (S-1673): the gate RW 0x81FDA8 (TheGameClient-like global 0xDE4388 vslot 0x44) is taken as "a local player exists"; the player's team list is the
// player's team prototypes in creation order, each prototype's teams in order, each team's members in list order; the drawable test is not made
void CommandTranslator::selectHero()
{
	const Player *local = m_ctx.localPlayer();
	if (!local)
	{
		return;
	}
	const Object *anchor = nullptr;
	if (m_ctx.ui.getSelectCount() > 0 && !m_ctx.ui.selected().empty())
	{
		const Object *first = m_ctx.logic.findObjectByID(m_ctx.ui.selected().front());
		if (first && first->isKindOfName("HERO"))
		{
			anchor = first;
		}
	}
	auto findAfter = [&](const Object *after) -> Object * {
		bool skipping = after != nullptr;
		for (const auto &proto : m_ctx.logic.players().teams().prototypes())
		{
			if (proto->getControllingPlayer() != local)
			{
				continue;
			}
			for (Team *team : proto->teams())
			{
				for (Object *o = team->getFirstMember(); o; o = o->friend_teamNext())
				{
					if (o->isDestroyed())
					{
						continue;
					}
					if (skipping)
					{
						skipping = o != after; // RW 0x81DA9F: the anchor itself ends the skip
						continue;
					}
					if (o->isKindOfName("HERO"))
					{
						return o;
					}
				}
			}
		}
		return nullptr;
	};
	Object *hero = findAfter(anchor);
	if (!hero && anchor)
	{
		hero = findAfter(nullptr);
	}
	if (!hero)
	{
		return;
	}
	if (Object *c = hero->getContainedBy())
	{
		hero = c;
	}
	m_ctx.ui.deselectAll(); // vslot 0x110 (INFERENCE: posting MSG_DESTROY_SELECTED_GROUP as the port's other deselect-all callers do)
	ClientMessage &group = m_ctx.stream.append(MSG_CREATE_SELECTED_GROUP);
	group.appendBoolean(true);
	group.appendObjectID(hero->getID());
	m_ctx.ui.selectObject(hero->getID());
	m_ctx.view.lookAt(*hero->getPosition());
}

void CommandTranslator::changeStance(int stance)
{
	ClientMessage &m = m_ctx.stream.append(MSG_CHANGE_STANCE); // the logic case RW 0x77BC59 (StancesBehavior::registerHandlers)
	m.appendInteger(stance);
}

void CommandTranslator::viewLastRadarEvent()
{
	if (m_radar) // RW 0x81FE7B: TheRadar
	{
		m_radar->tryJumpToNextEvent(m_ctx.view);
	}
}

void CommandTranslator::selectAllUnits()
{
	// ZH InGameUI::selectAllUnitsByType with the disqualifying kinds DOZER, HARVESTER, IGNORES_SELECT_ALL (CommandXlat.cpp:2888): every mobile
	// object of the local player that is not contained, structure or dead. ZH selects across the whole map; the radius / screen rule of
	// RotWK is not read (stop S-286).
	m_ctx.ui.deselectAll();
	ClientMessage &group = m_ctx.stream.append(MSG_CREATE_SELECTED_GROUP);
	group.appendBoolean(true);
	for (Object *o = m_ctx.logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (!HudObjects::isLocallyControlled(m_ctx, *o) || o->getContainedBy() || HudObjects::isEffectivelyDead(*o) || !o->getAIUpdateInterface() || !HudObjects::isSelectable(*o))
		{
			continue;
		}
		if (o->isKindOfName("DOZER") || o->isKindOfName("HARVESTER") || o->isKindOfName("IGNORES_SELECT_ALL") || o->isKindOfName("STRUCTURE"))
		{
			continue;
		}
		if (m_ctx.ui.getMaxSelectCount() > 0 && m_ctx.ui.getSelectCount() >= m_ctx.ui.getMaxSelectCount())
		{
			break;
		}
		m_ctx.ui.selectObject(o->getID());
		group.appendObjectID(o->getID());
	}
}

MessageDisposition CommandTranslator::translate(const ClientMessage &msg)
{
	const int t = msg.type();
	MessageDisposition disp = MessageDisposition::Keep;
	// the options menu always works; with input disabled everything else is dropped
	if (t != CMSG_META_OPTIONS && !m_ctx.ui.getInputEnabled() && !isSystemMessage(msg))
	{
		return MessageDisposition::Destroy;
	}
	if (t >= CMSG_META_BEGIN && t < CMSG_META_END)
	{
		switch (t)
		{
			case CMSG_META_STOP:
				m_ctx.stream.append(MSG_DO_STOP);
				return MessageDisposition::Destroy;
			case CMSG_META_SCATTER:
				m_ctx.stream.append(MSG_DO_SCATTER);
				return MessageDisposition::Destroy;
			case CMSG_META_CREATE_FORMATION:
				m_ctx.stream.append(MSG_CREATE_FORMATION);
				return MessageDisposition::Destroy;
			case CMSG_META_TOGGLE_ATTACKMOVE:
				m_ctx.ui.setAttackMoveToMode(!m_ctx.ui.isInAttackMoveToMode());
				return MessageDisposition::Keep;
			case CMSG_META_BEGIN_FORCEMOVE: m_ctx.ui.setForceMoveToMode(true); return MessageDisposition::Keep;
			case CMSG_META_END_FORCEMOVE: m_ctx.ui.setForceMoveToMode(false); return MessageDisposition::Keep;
			case CMSG_META_BEGIN_WAYPOINTS:
			case CMSG_META_ORDERMODE_WAYPOINT: m_ctx.ui.setWaypointMode(true); return MessageDisposition::Keep;
			case CMSG_META_END_WAYPOINTS:
			case CMSG_META_ORDERMODE_IMMEDIATE: m_ctx.ui.setWaypointMode(false); return MessageDisposition::Keep;
			case CMSG_META_BEGIN_PREFER_SELECTION: m_ctx.ui.setPreferSelectionMode(true); return MessageDisposition::Keep;
			case CMSG_META_END_PREFER_SELECTION: m_ctx.ui.setPreferSelectionMode(false); return MessageDisposition::Keep;
			case CMSG_META_BEGIN_FORCEATTACK: m_ctx.ui.setForceAttackMode(true); return MessageDisposition::Keep;
			case CMSG_META_END_FORCEATTACK: m_ctx.ui.setForceAttackMode(false); return MessageDisposition::Keep;
			case CMSG_META_SELECT_ALL:
				selectAllUnits();
				return MessageDisposition::Destroy;
			case CMSG_META_SELECT_NEXT_UNIT:
			case CMSG_META_SELECT_PREV_UNIT:
			case CMSG_META_SELECT_NEXT_WORKER:
			case CMSG_META_SELECT_PREV_WORKER:
				// lane HUD-4 (S-1673): RW 0x81F8D8 has no case for 0x5D .. 0x60: the message is kept and nobody acts on it
				m_retailNoOp[metaNameOf(t)]++;
				return MessageDisposition::Keep;
			case CMSG_META_SELECT_HERO:
				selectHero();
				return MessageDisposition::Destroy;
			case CMSG_META_VIEW_LAST_RADAR_EVENT:
				viewLastRadarEvent();
				return MessageDisposition::Destroy;
			case CMSG_META_STANCE_AGGRESSIVE: // RW 0x8201F5: MSG_CHANGE_STANCE (1128) with the integer 2
				changeStance(2);
				return MessageDisposition::Destroy;
			case CMSG_META_STANCE_HOLDGROUND: // RW 0x820235: 3
				changeStance(3);
				return MessageDisposition::Destroy;
			case CMSG_META_STANCE_BATTLE: // RW 0x820212: 1
				changeStance(1);
				return MessageDisposition::Destroy;
			case CMSG_META_VIEW_HOME_BASE:
				viewHomeBase();
				return MessageDisposition::Destroy;
			default:
				break;
		}
		return MessageDisposition::Keep;
	}
	switch (t)
	{
		case MSG_CREATE_SELECTED_GROUP:
		case MSG_CREATE_SELECTED_GROUP_NO_SOUND:
		case MSG_CREATE_SELECTED_GROUP_IDLE_WORKER_VOICE:
		case MSG_SELECT_TEAM0: case MSG_SELECT_TEAM1: case MSG_SELECT_TEAM2: case MSG_SELECT_TEAM3: case MSG_SELECT_TEAM4:
		case MSG_SELECT_TEAM5: case MSG_SELECT_TEAM6: case MSG_SELECT_TEAM7: case MSG_SELECT_TEAM8: case MSG_SELECT_TEAM9:
		case MSG_ADD_TEAM0: case MSG_ADD_TEAM1: case MSG_ADD_TEAM2: case MSG_ADD_TEAM3: case MSG_ADD_TEAM4:
		case MSG_ADD_TEAM5: case MSG_ADD_TEAM6: case MSG_ADD_TEAM7: case MSG_ADD_TEAM8: case MSG_ADD_TEAM9:
			m_teamExists = true;
			break;
		case MSG_DESTROY_SELECTED_GROUP:
			m_teamExists = false;
			break;
		case CMSG_MOUSEOVER_DRAWABLE_HINT:
		{
			if (m_ctx.ui.getSelectCount() > 0)
			{
				if (Object *o = m_ctx.logic.findObjectByID(msg.arg(0).objectID))
				{
					if (m_ctx.ui.isInForceAttackMode())
					{
						evaluateForceAttack(o, o->getPosition(), EvaluateType::DoHint);
					}
					else
					{
						evaluateContextCommand(o, o->getPosition(), EvaluateType::DoHint);
					}
				}
			}
			break;
		}
		case CMSG_MOUSEOVER_LOCATION_HINT:
		{
			const Coord3D position = msg.arg(0).location;
			if (m_ctx.ui.getSelectCount() > 0)
			{
				if (m_ctx.ui.isInForceAttackMode())
				{
					evaluateForceAttack(nullptr, &position, EvaluateType::DoHint);
				}
				else
				{
					evaluateContextCommand(nullptr, &position, EvaluateType::DoHint);
				}
			}
			break;
		}
		case CMSG_RAW_MOUSE_RIGHT_BUTTON_DOWN:
			m_rightAnchor = msg.arg(0).pixel;
			m_rightDown = msg.arg(2).integer;
			break;
		case CMSG_RAW_MOUSE_RIGHT_BUTTON_UP:
			m_rightLift = msg.arg(0).pixel;
			m_rightUp = msg.arg(2).integer;
			if (isClick(m_rightAnchor, m_rightLift, m_rightDown, m_rightUp))
			{
				m_ctx.ui.placeBuildAvailable(std::string(), INVALID_ID);
			}
			break;
		case CMSG_MOUSE_RIGHT_DOUBLE_CLICK:
		case CMSG_MOUSE_RIGHT_CLICK:
		case CMSG_MOUSE_LEFT_DOUBLE_CLICK:
		case CMSG_MOUSE_LEFT_CLICK:
		{
			const bool left = t == CMSG_MOUSE_LEFT_CLICK || t == CMSG_MOUSE_LEFT_DOUBLE_CLICK;
			// the standard setup orders with the left click, the alternate setup with the right one (CommandXlat.cpp:3678, 3741)
			if (left == m_alternateMouse)
			{
				break;
			}
			if (!left && !isClick(m_rightAnchor, m_rightLift, m_rightDown, m_rightUp))
			{
				break;
			}
			const IRegion2D &region = msg.arg(0).region;
			const bool isPoint = region.lo.x == region.hi.x && region.lo.y == region.hi.y;
			Coord3D pos;
			if (!m_ctx.view.screenToTerrain(region.lo, m_ctx.logic, pos))
			{
				break;
			}
			if (isPoint && areSelectedObjectsControllable())
			{
				Object *draw = HudObjects::pickObject(m_ctx, region.lo);
				if (m_ctx.ui.isInForceAttackMode())
				{
					evaluateForceAttack(draw, &pos, EvaluateType::DoCommand);
				}
				else
				{
					evaluateContextCommand(draw, &pos, EvaluateType::DoCommand);
				}
				disp = MessageDisposition::Destroy;
				m_ctx.ui.clearAttackMoveToMode();
			}
			break;
		}
		default:
			break;
	}
	return disp;
}
