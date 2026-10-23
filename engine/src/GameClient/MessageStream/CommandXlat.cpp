// OpenBFME. GPL-3.0.
// See CommandXlat.h.

#include "GameClient/MessageStream/CommandXlat.h"

#include "Common/Team.h"
#include "GameClient/Radar.h"
#include "GameLogic/AI/AIGarrisonStates.h"

#include "Common/BuildAssistant.h"
#include "Common/SpecialPower.h"
#include "GameClient/ControlBarCommands.h"
#include "Common/Player.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameClient/HudObjects.h"
#include "GameLogic/GameMessage.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Module/BloodthirstyUpdate.h"
#include "GameLogic/Module/SiegeModules.h"
#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SimMath.h"

#include <cstdlib>

// RW 0x81F791: a double click is a gate for expanding the CURRENT selection across the screen; the clicked object does not seed it.
void CommandTranslator::doubleClick(const ClientMessage &msg)
{
	if (m_ctx.ui.isInForceAttackMode())
	{
		return;
	}
	const IRegion2D &region = msg.arg(0).region;
	if (region.lo.x != region.hi.x || region.lo.y != region.hi.y)
	{
		return;
	}
	Object *picked = HudObjects::pickForDoubleClick(m_ctx, region.lo);
	if (!picked)
	{
		return;
	}
	// RW 0x81F7DC..0x81F81F: HORDE_MEMBER -> containedBy, else producer ID; only a HordeContain can stand for it.
	if (picked->testStatus(38))
	{
		Object *horde = picked->getContainedBy();
		if (!horde)
		{
			horde = m_ctx.logic.findObjectByID(picked->getProducerID());
		}
		if (!horde || !horde->getContain() || !horde->getContain()->getHordeContainInterface())
		{
			return;
		}
		picked = horde;
	}
	// RW 0x9403ED -> InGameUI vslot 0x188 (RW 0x69D90C). No Alt / Shift reselection, area-selection message or map-wide fallback.
	if (HudObjects::isLocallyControlled(m_ctx, *picked) && HudObjects::isSelectable(*picked, m_ctx.ui.getGUICommand()) && !picked->isKindOfName("STRUCTURE"))
	{
		m_ctx.ui.selectMatching(m_ctx, false);
	}
}

namespace
{
bool isSystemMessage(const ClientMessage &m)
{
	// ZH isSystemMessage (CommandXlat.cpp:5163): what still works while input is disabled (the options menu, a game data reset)
	// lane OWNERFIX-1: RotWK's CommandTranslator::translateGameMessage (RW 0x81F8F2 .. 0x81F979) also lets these logic messages through while either
	// input flag (InGameUI + 0x15 / + 0x16) is off: 0x3EC, 0x447, 0x448, 0x44A, 0x45F, 0x469. The script's DISABLE_INPUT (RW 0x7BC586) appends
	// MSG_DESTROY_SELECTED_GROUP after turning the input off, so the logic's selection group must reach the logic. The client message numbers of
	// that list (0x1D, 0x70, 0x6A5, 0x6A6, 0x6B8, 0x6BA, 0x6BB, 0x7D9, 0x7ED) are RotWK's own numbering and are not mapped here (stop S-4964)
	switch (m.type())
	{
		case CMSG_META_OPTIONS:
		case MSG_DESTROY_SELECTED_GROUP: // 0x3EC
		case MSG_SET_REPLAY_CAMERA:      // 0x447
		case MSG_SELF_DESTRUCT:          // 0x448
		case MSG_LOGIC_CRC:              // 0x44A
		case MSG_CALL_IN_REINFORCEMENTS: // 0x45F
		case MSG_CHANGE_ORDERMODE:       // 0x469
			return true;
		default:
			return false;
	}
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
	// lane BUILD-5: RotWK's createCommandHint (RW 0x6A3B77). In the default mouse mode the hint's cursor goes through RW 0x69BD53 (no change while selecting
	// or scrolling); while a building waits for its site (mode 1, RW 0x6A3E0F) only the move hints (types 0xBD / 0xBE / 0x432: Move, AttackMove) show Build
	// (10) and the attack hints (0xB1 / 0xCA) InvalidBuild (11); any other hint leaves the cursor
	const std::string c = cursor;
	if (m_ctx.ui.mouseMode() == 1)
	{
		if (c == MouseCursorName::Move || c == MouseCursorName::AttackMove)
		{
			m_ctx.ui.hintCursor(MouseCursorName::Build);
		}
		else if (c == MouseCursorName::AttackObj)
		{
			m_ctx.ui.hintCursor(MouseCursorName::InvalidBuild);
		}
		return;
	}
	m_ctx.ui.hintCursor(c);
}

int CommandTranslator::issueMove(Object *target, const Coord3D *pos, EvaluateType type)
{
	// ZH issueMoveToLocationCommand (CommandXlat.cpp:865) as RotWK's RW 0x81DB0C
	int msgType = 0;
	if (m_teamExists)
	{
		const bool forceAttackable = target && target->isKindOfName("FORCEATTACKABLE");
		// RW 0x81DB0C (issueMoveToLocationCommand): waypoint mode (InGameUI + 0x8B0) 0x432, force move (+ 0x8B9) 0x431, force attack (+ 0x8B8) on a
		// FORCEATTACKABLE target 0x425, else 0x42F; RotWK has no attack-move branch here (lane INPUT-1 r2)
		if (m_ctx.ui.isInWaypointMode())
		{
			msgType = MSG_ADD_WAYPOINT;
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
	// RW 0x81E4B5 (lane INPUT-1 r2; reached in the force-attack mode, which RotWK 2.01 itself never turns on: see the BEGIN_FORCEATTACK case). The FIRST
	// selected object with an object decides (RW 0x81DFB5 -> RW 0x81D9DE: Object::isAbleToAttack RW 0x691269, then getAbleToAttackSpecificObject RW
	// 0x68D6A6 (forced) for an object, getAbleToUseWeaponAgainstTarget RW 0x68B5FA for the ground: POSSIBLE / POSSIBLE_AFTER_MOVING give the command).
	// MSG_DO_FORCE_ATTACK_OBJECT (0x426) carries the object and the position, MSG_DO_FORCE_ATTACK_GROUND (0x427) the position.
	// INFERENCE: the attack tests are canPossiblyHaveAnyWeapon of the object or a member of its horde (objectCanAttack), as elsewhere here (S-286).
	if (!target && !pos)
	{
		return 0;
	}
	Object *first = nullptr;
	for (ObjectID id : m_ctx.ui.selected())
	{
		if ((first = m_ctx.logic.findObjectByID(id)) != nullptr)
		{
			break;
		}
	}
	if (!first || !objectCanAttack(*first))
	{
		// RW 0x81E594 .. 0x81E5A3: the invalid hint (0xB2, GenericInvalid) is only for the eligibility result 1; result 0 (no attacker) gives no hint.
		// The boolean test here stands for result 0 (S-286), so no hint (lane INPUT-1 r4)
		return 0;
	}
	const int msgType = target ? MSG_DO_FORCE_ATTACK_OBJECT : MSG_DO_FORCE_ATTACK_GROUND;
	if (type == EvaluateType::DoCommand)
	{
		ClientMessage &m = m_ctx.stream.append(msgType);
		if (target)
		{
			m.appendObjectID(target->getID());
		}
		if (pos)
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
	// lane FACT-C: a special power button waiting for its target (CONTEXTMODE_COMMAND: GUICommandTranslator leaves its click to this translator)
	if (const CommandButton *command = m_ctx.ui.getGUICommand())
	{
		if (command->m_command == GUI_COMMAND_BLOODTHIRSTY)
		{
			// RW 0x81E60B: the pressed button's relationship bits (NEED_TARGET_ALLY_OBJECT in retail's data) drop the target first, then the action check
			const Player *local = m_ctx.localPlayer();
			if (obj && (!local || !BloodthirstyUpdate::relationshipAllowed(*command, *local, *obj)))
			{
				obj = nullptr;
			}
			Object *source = m_ctx.logic.findObjectByID(m_ctx.ui.firstSelected());
			BloodthirstyUpdate *module = source ? BloodthirstyUpdate::find(*source) : nullptr;
			Object *sourceHorde = source ? source->getHordeObject(false) : nullptr;
			Object *targetHorde = obj ? obj->getHordeObject(false) : nullptr;
			const bool valid = module && sourceHorde && targetHorde && targetHorde != sourceHorde && module->canSacrifice(obj);
			if (!valid)
			{
				if (type == EvaluateType::DoHint && !command->m_invalidCursorName.empty())
				{
					hint(command->m_invalidCursorName.c_str());
				}
				return 0;
			}
			if (type == EvaluateType::DoHint)
			{
				if (!command->m_cursorName.empty())
				{
					hint(command->m_cursorName.c_str());
				}
				return 0;
			}
			ClientMessage &m = m_ctx.stream.append(MSG_SACRIFICE);
			m.appendObjectID(obj->getID());
			m_ctx.ui.setGUICommand(nullptr);
			return MSG_SACRIFICE;
		}
		if (command->m_command == GUI_COMMAND_SPECIAL_POWER)
		{
			return evaluateGUISpecialPower(*command, obj, pos, type);
		}
	}
	// lane SIEGE-1: RotWK's siege branch comes before the enter, attack and capture tests (RW 0x81E7A5 after the GUI command and the force-move tests)
	if (obj)
	{
		if (const int siegeType = evaluateSiegeDeploy(obj, type))
		{
			return siegeType;
		}
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
	// lane FACT-C (S-2002): a building the selection can capture (an inn, an outpost, a signal fire): its capture button's power at the building
	if (obj && !m_ctx.ui.isInForceAttackMode())
	{
		if (const int captureType = evaluateCapture(obj, pos, type))
		{
			return captureType;
		}
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
	// no command (it is a selection): only a location click moves. Lane WALL-1 (INFERENCE, S-4840): a wall piece units walk on (KindOf WALK_ON_TOP_OF_WALL: RotWK
	// gives it the selectable pick type, RW 0x4B583C, so the order click picks the wall section under the cursor) is ground to walk on, like ZH's own mine
	// (CommandXlat.cpp:1448): the click moves onto the wall top it lands on. RotWK's evaluateContextCommand (RW 0x81E60B) was not read for this case
	const bool walkOn = obj && obj->isKindOfName("WALK_ON_TOP_OF_WALL");
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
		else if (!obj || walkOn)
		{
			msgType = issueMove(obj, pos, type);
		}
	}
	return msgType;
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
	m_ctx.ui.deselectAll(false); // vslot 0x110 RW 0x81FE24: RotWK's InGameUI::deselectAllDrawables(void) posts no message (BFME2 decomp InGameUISelection.cpp)
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
			// lane INPUT-1: RotWK's CommandTranslator::translateGameMessage (RW 0x81F8D8; the meta jump tables RW 0x820780, 0x8207A0, 0x8207F8)
			case CMSG_META_DEPLOY:        // RW 0x820680: eaten, nothing done
			case CMSG_META_AUTO_SAVE:     // RW 0x820680
			case CMSG_META_FOLLOW:        // RW 0x81FD50 (case 0x6A)
			case CMSG_META_DELETE_BEACON: // RW 0x820680
				m_retailNoOp[metaNameOf(t)]++;
				return MessageDisposition::Destroy;
			case CMSG_META_SELL: // RW 0x820460: MSG_SELL (1052) without arguments, the meta is kept
				m_ctx.stream.append(MSG_SELL);
				return MessageDisposition::Keep;
			case CMSG_META_SPELL_STORE: // RW 0x820638: the spell store toggles (RW 0x71C6AF); the HUD opens or closes it
				m_ctx.ui.requestSpellStoreToggle();
				return MessageDisposition::Destroy;
			case CMSG_META_DIPLOMACY: // RW 0x81FFAA: in a game the Palantir flag's screen opens (RW 0x8E8843 / 0x914EF0); the HUD asks the shell
				m_ctx.ui.requestDiplomacy();
				return MessageDisposition::Destroy;
			case CMSG_META_TAKE_SCREENSHOT: // RW 0x82019D: TheDisplay->takeScreenShot (vslot 0x14C); the device saves the picture
				m_ctx.ui.requestScreenshot();
				return MessageDisposition::Destroy;
			case CMSG_META_STOP: // RW 0x81FEBF: MSG_DO_STOP (0x435)
				m_ctx.stream.append(MSG_DO_STOP);
				return MessageDisposition::Destroy;
			case CMSG_META_SCATTER:
				m_ctx.stream.append(MSG_DO_SCATTER);
				return MessageDisposition::Destroy;
			case CMSG_META_CREATE_FORMATION:
				m_ctx.stream.append(MSG_CREATE_FORMATION);
				return MessageDisposition::Destroy;
			// lane INPUT-1 r2: RotWK's CommandTranslator has no case for TOGGLE_ATTACKMOVE (0x7E): the meta passes (jump table RW 0x8207A0 -> RW 0x82076C,
			// keep). Attack-move is the horde's Attack Move button ("&Attack Move"): ControlBar::setControlCommand (RW 0x71CF3E) registers each command
			// window's hotkey (RW 0x71D139 -> RW 0x75AE14), the HotKeyTranslator presses it (RW 0x75B068), a GUI command whose click sends MSG_DO_ATTACKMOVETO
			case CMSG_META_TOGGLE_ATTACKMOVE:
				m_retailNoOp[metaNameOf(t)]++;
				return MessageDisposition::Keep;
			// Force attack. TARGET FACT (lane BACKLOG3; RotWK game.dat, caveat S-001): RotWK 2.01 has no force-attack activation. BEGIN_ / END_FORCEATTACK
			// (0x74 / 0x75, the name table RW 0xBF0948 / 0xBF0950) go to the default RW 0x82076C in the meta jump table RW 0x8207A0 (entries 8 and 9), while
			// its neighbours 0x76 .. 0x7B are the inlined setters of force move (+ 0x8B9, RW 0x8200E3 / 0x8200EF), waypoints (+ 0x8B0, RW 0x8200FA /
			// 0x82011D) and prefer selection (+ 0x8BA, RW 0x820106 / 0x820112). The only stores to InGameUI + 0x8B8 in the code are the setter RW 0x69AD23
			// (no call, jump or pointer reaches RW 0x69AD1F) and the clears RW 0x69B638, 0x69B6F0, 0x6A6A49, 0x6A7250, 0x83C42B (pinned by
			// test_input1.cpp's binary facts). So in retail Ctrl sets nothing and the mode's readers (RW 0x81FBBC -> evaluateForceAttack RW 0x81E4B5,
			// RW 0x81DB0C) are never reached.
			// NOT RETAIL (stop S-1675): lane INPUT-1 r3 set the mode here as ZH's CommandXlat.cpp:3483 / 3488 do, and Ctrl force attack was released
			// (v0.3.0-preview.1 / .2). It is kept as an OpenBFME addition until the owner decides; retail's behaviour is these two cases as
			// retail no-ops (m_retailNoOp, like TOGGLE_ATTACKMOVE above)
			case CMSG_META_BEGIN_FORCEATTACK: m_ctx.ui.setForceAttackMode(true); return MessageDisposition::Keep;
			case CMSG_META_END_FORCEATTACK: m_ctx.ui.setForceAttackMode(false); return MessageDisposition::Keep;
			case CMSG_META_BEGIN_FORCEMOVE: m_ctx.ui.setForceMoveToMode(true); return MessageDisposition::Keep;   // RW 0x8200E3: InGameUI + 0x8B9
			case CMSG_META_END_FORCEMOVE: m_ctx.ui.setForceMoveToMode(false); return MessageDisposition::Keep;
			case CMSG_META_BEGIN_WAYPOINTS: m_ctx.ui.setWaypointMode(true); return MessageDisposition::Keep;     // RW 0x8200FA: InGameUI + 0x8B0
			case CMSG_META_END_WAYPOINTS: m_ctx.ui.setWaypointMode(false); return MessageDisposition::Keep;
			case CMSG_META_ORDERMODE_WAYPOINT:
			case CMSG_META_ORDERMODE_IMMEDIATE:
			{
				// lane INPUT-1 r2: RW 0x820249 / 0x820265 (Alt down / up): MSG_CHANGE_ORDERMODE (1129) with the new order mode and the mode it
				// replaces: (1, 0) for queued orders, (0, 1) back to immediate; the logic stores the player's order mode (RW 0x77BCA4). RotWK's
				// AiOrdersManager queue that reads the mode is not ported (stop S-281, lane INPUT-1 r3): the orders ignore it
				const bool queued = t == CMSG_META_ORDERMODE_WAYPOINT;
				ClientMessage &m = m_ctx.stream.append(MSG_CHANGE_ORDERMODE);
				m.appendInteger(queued ? 1 : 0);
				m.appendInteger(queued ? 0 : 1);
				return MessageDisposition::Destroy;
			}
			case CMSG_META_BEGIN_PREFER_SELECTION: m_ctx.ui.setPreferSelectionMode(true); return MessageDisposition::Keep;
			case CMSG_META_END_PREFER_SELECTION: m_ctx.ui.setPreferSelectionMode(false); return MessageDisposition::Keep;
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
			// lane INPUT-1: cases RotWK's handler executes whose effect is not ported (stop S-287): counted, never dropped silently. CHAT_BUDDIES
			// RW 0x81FF84, CHAT_ALLIES / CHAT_EVERYONE RW 0x81FEE0 / 0x81FF34 (the chat box), TOGGLE_CONTROL_BAR
			// RW 0x82001B (hides the Palantir), ALL_CHEER RW 0x820128 (MSG_DO_CHEER 1080 and its text), PLACE_BEACON RW 0x820298, ORDER_SYNCHRONIZE RW
			// 0x82028E (MSG_ORDER_SYNCHRONIZE 1136), TOGGLE_PLANNING_MODE RW 0x82065C (the War of the Ring planning), TOGGLE_FAST_FORWARD_MODE RW 0x820542
			// (replays only), the camera key flags BEGIN_ / END_CAMERA_* RW 0x8200C0 .. 0x820455 (InGameUI + 0x8BB .. 0x8C2; no retail binding)
			case CMSG_META_CHAT_BUDDIES:
			case CMSG_META_CHAT_ALLIES:
			case CMSG_META_CHAT_EVERYONE:
			case CMSG_META_TOGGLE_CONTROL_BAR:
			case CMSG_META_ALL_CHEER:
			case CMSG_META_PLACE_BEACON:
			case CMSG_META_ORDER_SYNCHRONIZE:
			case CMSG_META_TOGGLE_PLANNING_MODE:
			case CMSG_META_TOGGLE_FAST_FORWARD_MODE:
			case CMSG_META_BEGIN_CAMERA_ROTATE_LEFT:
			case CMSG_META_END_CAMERA_ROTATE_LEFT:
			case CMSG_META_BEGIN_CAMERA_ROTATE_RIGHT:
			case CMSG_META_END_CAMERA_ROTATE_RIGHT:
			case CMSG_META_BEGIN_CAMERA_ZOOM_IN:
			case CMSG_META_END_CAMERA_ZOOM_IN:
			case CMSG_META_BEGIN_CAMERA_ZOOM_OUT:
			case CMSG_META_END_CAMERA_ZOOM_OUT:
			case CMSG_META_BEGIN_CAMERA_SCROLL_LEFT:
			case CMSG_META_END_CAMERA_SCROLL_LEFT:
			case CMSG_META_BEGIN_CAMERA_SCROLL_RIGHT:
			case CMSG_META_END_CAMERA_SCROLL_RIGHT:
			case CMSG_META_BEGIN_CAMERA_SCROLL_UP:
			case CMSG_META_END_CAMERA_SCROLL_UP:
			case CMSG_META_BEGIN_CAMERA_SCROLL_DOWN:
			case CMSG_META_END_CAMERA_SCROLL_DOWN:
				m_unportedMeta[metaNameOf(t)]++;
				return MessageDisposition::Keep;
			// RW 0x82037A / 0x8203A3: only with GlobalData + 0x11C9 (a development switch, off in retail): nothing in a retail game
			case CMSG_META_RELOAD_RAPID_ITERATION_FEATURE:
			case CMSG_META_REFRESH_RAPID_ITERATION_FEATURE:
				m_retailNoOp[metaNameOf(t)]++;
				return MessageDisposition::Keep;
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
		// lane BUILD-5: RotWK's CommandTranslator (RW 0x81F8D8) keeps the right press's pixel and time and clears its dragged flag (case 0xE, RW 0x81F9D6), sets the
		// flag on a move that leaves the drag tolerance from that pixel (case 3, RW 0x81FA2C -> RW 0x5ED4B9) and keeps the release's pixel and time (case 0x10,
		// RW 0x81FA5D). Unlike ZH's (CommandXlat.cpp:3640) its release does NOT end a placement: RotWK's PlaceEventTranslator does that on the right click
		case CMSG_RAW_MOUSE_RIGHT_BUTTON_DOWN:
			m_rightAnchor = msg.arg(0).pixel;
			m_rightDown = msg.arg(2).integer;
			m_rightDragged = false;
			break;
		case CMSG_RAW_MOUSE_POSITION:
			if (m_ctx.mouse.isDragging(m_rightAnchor, msg.arg(0).pixel))
			{
				m_rightDragged = true;
			}
			break;
		case CMSG_RAW_MOUSE_RIGHT_BUTTON_UP:
			m_rightLift = msg.arg(0).pixel;
			m_rightUp = msg.arg(2).integer;
			break;
		case CMSG_MOUSE_LEFT_DOUBLE_CLICK:
			doubleClick(msg); // RW 0x81FBE4: independent of the order-click / alternate-mouse path
			break;
		case CMSG_MOUSE_RIGHT_DOUBLE_CLICK:
		case CMSG_MOUSE_RIGHT_CLICK:
		case CMSG_MOUSE_LEFT_CLICK:
		{
			const bool left = t == CMSG_MOUSE_LEFT_CLICK;
			// the standard setup orders with the left click, the alternate setup with the right one (CommandXlat.cpp:3678, 3741). lane FACT-C: RotWK's two
			// cases read TheInGameUI's GUI command first (RW 0x81FAE2 / 0x81FC57): while one other than ATTACK_MOVE (0xA) waits for its target, the right click
			// orders nothing (RW 0x81FAFE) and the left click carries it out in either setup (RW 0x81FC73 skips the alternate-mouse test); a special power's target
			// is a left click with the default (alternate) mouse
			const CommandButton *pending = m_ctx.ui.getGUICommand();
			const bool guiTarget = pending && pending->m_command != GUI_COMMAND_ATTACK_MOVE;
			if (left ? (m_alternateMouse && !guiTarget) : (!m_alternateMouse || guiTarget))
			{
				++m_clickOutcomes["other-button"];
				break;
			}
			// lane BUILD-5: RotWK's right click test (RW 0x81FAE0 .. 0x81FB02): not dragged, or released within DragToleranceMS
			if (!left && !m_ctx.mouse.isRightClick(m_rightDragged, (unsigned)m_rightDown, (unsigned)m_rightUp))
			{
				++m_clickOutcomes["right-not-a-click"];
				break;
			}
			const IRegion2D &region = msg.arg(0).region;
			const bool isPoint = region.lo.x == region.hi.x && region.lo.y == region.hi.y;
			Coord3D pos;
			if (!m_ctx.view.screenToTerrain(region.lo, m_ctx.logic, pos))
			{
				++m_clickOutcomes["off-terrain"];
				break;
			}
			if (!isPoint)
			{
				++m_clickOutcomes["region"];
			}
			else if (!areSelectedObjectsControllable())
			{
				++m_clickOutcomes["not-controllable"];
			}
			if (isPoint && areSelectedObjectsControllable())
			{
				Object *draw = HudObjects::pickObject(m_ctx, region.lo);
				int issued = 0;
				if (m_ctx.ui.isInForceAttackMode())
				{
					issued = evaluateForceAttack(draw, &pos, EvaluateType::DoCommand);
				}
				else
				{
					issued = evaluateContextCommand(draw, &pos, EvaluateType::DoCommand);
				}
				++m_clickOutcomes[std::string(issued ? "command" : "no command") + (draw ? " on an object" : " on the ground")];
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

// Lane FACT-C. TARGET FACTS (RotWK game.dat, caveat S-001): evaluateContextCommand (RW 0x81E60B; tier B same-shape with BFME2 0x829E11, no decomp source) reads
// TheInGameUI's GUI command (vslot 0xC0) after the waypoint mode test; a command of type SPECIAL_POWER (0x18) takes the click: the object under the cursor is
// dropped unless its relationship to the local player (RW 0x6ADBEB: 0 enemies, 1 neutral, 2 allies) has its option bit (0x1 / 0x2 / 0x4), the action check
// RW 0x6A4260 decides, and on success the DO_COMMAND evaluation calls issueSpecialPowerCommand (RW 0x81E147) and clears the GUI command (vslot 0xBC with 0).
// issueSpecialPowerCommand (RW 0x81E147, tier A with BFME2 0x82994D, decomp Rva00428EEBIssueFireWeapon.cpp:427 CommandTranslator::issueSpecialPowerCommand):
// options & 7 with a target: MSG_DO_SPECIAL_POWER_AT_OBJECT (0x412) { id, target, options, source NONE, location }; else options & 0x20 with a position:
// MSG_DO_SPECIAL_POWER_AT_LOCATION (0x411) { id, location, target or NONE, options, source NONE } and one more 0x411 for every button the command's chained
// list names (CommandTrigger, +0x188) that is a SPECIAL_POWER with a template; else MSG_DO_SPECIAL_POWER (0x410) { id, options, NONE, NONE }. Source NONE: the
// logic casts with the issuer's selection.
// NOT PORTED (stop S-2000): the action check RW 0x6A4260 (ActionManager's special power target rules: range, KindOf, the module's own target filter) and the
// kind-of / option 0x10 / 0x200000 target drops before it; here a click is valid when the option's target kind is there (an object of an allowed
// relationship, or a ground point for NEED_TARGET_POS). The radius cursor, the unit voice and the selection flash are client presentation, not ported.
int CommandTranslator::issueSpecialPower(const CommandButton &command, Object *target, const Coord3D *pos, EvaluateType type)
{
	const SpecialPowerTemplate *t = TheSpecialPowerStore && !command.m_specialPowerName.empty() ? TheSpecialPowerStore->findSpecialPowerTemplate(command.m_specialPowerName) : nullptr;
	if (!t)
	{
		return 0;
	}
	int msgType = 0;
	if ((command.m_options & 7) && target)
	{
		msgType = MSG_DO_SPECIAL_POWER_AT_OBJECT;
		if (type == EvaluateType::DoCommand)
		{
			ClientMessage &m = m_ctx.stream.append(msgType);
			m.appendInteger((int)t->getID());
			m.appendObjectID(target->getID());
			m.appendInteger((int)command.m_options);
			m.appendObjectID(INVALID_ID);
			m.appendLocation(pos ? *pos : *target->getPosition());
		}
	}
	else if ((command.m_options & COMMAND_OPTION_NEED_TARGET_POS) && pos)
	{
		msgType = MSG_DO_SPECIAL_POWER_AT_LOCATION;
		if (type == EvaluateType::DoCommand)
		{
			const ObjectID targetId = target ? target->getID() : INVALID_ID;
			auto emit = [&](const SpecialPowerTemplate &tt, std::uint32_t options) {
				ClientMessage &m = m_ctx.stream.append(msgType);
				m.appendInteger((int)tt.getID());
				m.appendLocation(*pos);
				m.appendObjectID(targetId);
				m.appendInteger((int)options);
				m.appendObjectID(INVALID_ID);
			};
			emit(*t, command.m_options);
			for (const std::string &name : command.m_commandTrigger)
			{
				const CommandButton *cb = TheCommandStore ? TheCommandStore->findCommandButton(name) : nullptr;
				const SpecialPowerTemplate *ct = cb && cb->m_command == GUI_COMMAND_SPECIAL_POWER && TheSpecialPowerStore && !cb->m_specialPowerName.empty()
					? TheSpecialPowerStore->findSpecialPowerTemplate(cb->m_specialPowerName) : nullptr;
				if (ct)
				{
					emit(*ct, cb->m_options);
				}
			}
		}
	}
	else
	{
		msgType = MSG_DO_SPECIAL_POWER;
		if (type == EvaluateType::DoCommand)
		{
			ClientMessage &m = m_ctx.stream.append(msgType);
			m.appendInteger((int)t->getID());
			m.appendInteger((int)command.m_options);
			m.appendObjectID(INVALID_ID);
			m.appendObjectID(INVALID_ID);
		}
	}
	return msgType;
}

int CommandTranslator::evaluateGUISpecialPower(const CommandButton &command, Object *obj, const Coord3D *pos, EvaluateType type)
{
	if (obj && (command.m_options & 7))
	{
		const Relationship rel = HudObjects::relationshipToLocal(m_ctx, *obj);
		const std::uint32_t bit = rel == ENEMIES ? COMMAND_OPTION_NEED_TARGET_ENEMY_OBJECT : rel == NEUTRAL ? COMMAND_OPTION_NEED_TARGET_NEUTRAL_OBJECT : COMMAND_OPTION_NEED_TARGET_ALLY_OBJECT;
		if (!(command.m_options & bit))
		{
			obj = nullptr;
		}
	}
	const bool valid = ((command.m_options & 7) && obj) || ((command.m_options & COMMAND_OPTION_NEED_TARGET_POS) && pos);
	if (!valid)
	{
		if (type == EvaluateType::DoHint)
		{
			hint(MouseCursorName::GenericInvalid);
		}
		return 0;
	}
	const int msgType = issueSpecialPower(command, obj, pos, type);
	if (type == EvaluateType::DoHint && !command.m_cursorName.empty())
	{
		hint(command.m_cursorName.c_str());
	}
	if (type == EvaluateType::DoCommand)
	{
		m_ctx.ui.setGUICommand(nullptr);
	}
	return msgType;
}

// Lane FACT-C. TARGET FACTS (caveat S-001): after its attack test, evaluateContextCommand (RW 0x81E60B) asks ActionManager check 0xC (RW 0x69D27E case 0xC: some
// selected object passes canCaptureBuilding RW 0x82D0A1 with its module of power type 0x1D, Object::findSpecialPowerModuleInterface RW 0x691526); then it walks
// the 33 buttons of the first selected object's command set (RW 0x71EFA2 on RW 0x69156B) for a SPECIAL_POWER button whose power's type (template + 0x1C) is
// 0x1A or 0x1D (SPECIAL_BLACKLOTUS_CAPTURE_BUILDING / SPECIAL_INFANTRY_CAPTURE_BUILDING) and issues it at the building (issueSpecialPowerCommand RW 0x81E147); the
// hint is the CaptureBuilding cursor (0xC3) for type 0x1D.
// canCaptureBuilding (RW 0x82D0A1, tier B same-shape with BFME2 0x81C79C, decomp ActionManagerSpecialPowerChecks.cpp:660), the parts ported: the capturer
// has the power module and it is fully ready; the target is a STRUCTURE (template + 0x108 bit 7), not IMMUNE_TO_CAPTURE (+ 0x110 bit 17); the power has no
// ObjectFilter (none in retail data; a filter is S-2002) so the target must be CAPTURABLE (+ 0x10C bit 17) with a LINKED_TO_FLAG object (KindOf 50) within 150
// of it (ThePartitionManager::getClosestObject, 3D distance); not UNDER_CONSTRUCTION or SOLD; not an ally of the capturer; no garrisoned riders.
// NOT PORTED (S-2002): the + 0x438 / + 0x110 status pair, the + 0x80 word, the shroud test (isObjectShroudedForAction), CAPTURE_FORBIDDEN, the per-player
// refusal RW 0x6943B2, the stealth riders and the vision test RW 0x41B80F (BFME2).
bool CommandTranslator::canCaptureBuilding(const Object &capturer, const Object &target)
{
	const SpecialPowerModuleInterface *module = nullptr;
	for (const std::unique_ptr<BehaviorModule> &m : capturer.modules())
	{
		SpecialPowerModuleInterface *sp = m->getSpecialPower();
		if (!module && sp && sp->getSpecialPowerTemplate() && sp->getSpecialPowerTemplate()->getSpecialPowerType() == 0x1D)
		{
			module = sp;
		}
	}
	if (!module || module->getPercentReadyForDisplay() < 1.0f)
	{
		return false;
	}
	if (!target.isKindOfName("STRUCTURE") || target.isKindOfName("IMMUNE_TO_CAPTURE"))
	{
		return false;
	}
	if (module->getSpecialPowerTemplate()->m_objectFilter)
	{
		++m_unportedMeta["capture with a power ObjectFilter (S-2002)"];
		return false;
	}
	if (!target.isKindOfName("CAPTURABLE"))
	{
		return false;
	}
	bool flag = false;
	for (Object *o = m_ctx.logic.getFirstObject(); o && !flag; o = o->getNextObject())
	{
		if (o->isKindOfName("LINKED_TO_FLAG"))
		{
			const float dx = SimMath::subf32(o->getPosition()->x, target.getPosition()->x), dy = SimMath::subf32(o->getPosition()->y, target.getPosition()->y),
			            dz = SimMath::subf32(o->getPosition()->z, target.getPosition()->z);
			flag = SimMath::addf32(SimMath::addf32(SimMath::mulf32(dx, dx), SimMath::mulf32(dy, dy)), SimMath::mulf32(dz, dz)) <= 22500.0f; // 150^2
		}
	}
	if (!flag)
	{
		return false;
	}
	static const int kSold = ObjectTemplateInfoBuilder::objectStatusIndex("SOLD");
	if (target.isUnderConstruction() || (kSold >= 0 && target.testStatus((unsigned)kSold)))
	{
		return false;
	}
	if (HudObjects::relationshipToLocal(m_ctx, target) == ALLIES)
	{
		return false;
	}
	if (const ContainModuleInterface *contain = target.getContain())
	{
		if (contain->getContainCount() > 0)
		{
			return false;
		}
	}
	return true;
}

int CommandTranslator::evaluateCapture(Object *obj, const Coord3D *pos, EvaluateType type)
{
	bool any = false;
	for (ObjectID id : m_ctx.ui.selected())
	{
		const Object *o = m_ctx.logic.findObjectByID(id);
		any = any || (o && canCaptureBuilding(*o, *obj));
	}
	Object *first = any ? m_ctx.logic.findObjectByID(m_ctx.ui.firstSelected()) : nullptr;
	const CommandSet *set = first && TheCommandStore ? TheCommandStore->findCommandSet(first->getCommandSetName()) : nullptr;
	if (!set)
	{
		return 0;
	}
	for (int i = 0; i < CommandSet::MAX_BUTTONS; ++i)
	{
		const CommandButton *b = set->getCommandButton(i);
		const SpecialPowerTemplate *t = b && b->m_command == GUI_COMMAND_SPECIAL_POWER && TheSpecialPowerStore && !b->m_specialPowerName.empty()
			? TheSpecialPowerStore->findSpecialPowerTemplate(b->m_specialPowerName) : nullptr;
		if (t && (t->getSpecialPowerType() == 0x1A || t->getSpecialPowerType() == 0x1D))
		{
			if (type == EvaluateType::DoHint)
			{
				if (t->getSpecialPowerType() == 0x1D)
				{
					hint("CaptureBuilding");
				}
				return MSG_DO_SPECIAL_POWER_AT_OBJECT;
			}
			return issueSpecialPower(*b, obj, pos, type);
		}
	}
	return 0;
}

// Lane SIEGE-1. TARGET FACTS (RotWK game.dat, caveat S-001): InGameUI's siege test RW 0x69D674 (BFME2 0x69CC19, decomp InGameUISiegeDeployTarget.cpp): a target
// that is WALK_ON_TOP_OF_WALL (template + 0x10F bit 4, KindOf 60) with a selection; for every selected drawable's object that is not contained (+ 0x27C): the
// button Command_SpecialAbilitySiegeLadderDeploy for a SIEGE_LADDER (+ 0x119 bit 3), else Command_SpecialAbilitySiegeDeploy (RW 0x71D6EA); the object must be
// a SIEGE_TOWER (+ 0x113 bit 5) without status DEPLOYED (0x3B, RW 0x44DDEC), able to use the button (RW 0x692A75: its power's module, the button in its
// command set) and the button ready for it (RW 0x75CC3E: the power's module fully ready); then an object without an AI answers yes, and so does one whose
// target has a SiegeDockingBehavior that gives it no approach point (dock interface slot 1, RW 0x85CA8A) or an approach point its AI reaches by a quick path
// (RW 0x66403D -> RW 0x6F5BB0 clientSafeQuickDoesPathExist from the object's position with its locomotor set). evaluateContextCommand (RW 0x81E60B, after the
// GUI command and the force-move tests, before the enter and attack tests): a hint is the attack hint (0xB1, the AttackObj cursor); a command is
// MSG_DO_SPECIAL_POWER_AT_OBJECT (0x412) with Command_SpecialAbilitySiegeDeploy's power { the template id, the target, the button's options, no source, the
// target's position } (the unit voice of the button, RW 0x8DD5FF, is the client's audio: not played).
// INFERENCE (S-4944): the TheInGameUI byte + 0x8B0 the branch requires clear is not identified (taken as clear); the selection is the port's selected object ids.
bool CommandTranslator::canSiegeDeploy(const Object &target)
{
	if (!target.isKindOfName("WALK_ON_TOP_OF_WALL") || m_ctx.ui.selected().empty())
	{
		return false;
	}
	static const int kDeployed = ObjectTemplateInfoBuilder::objectStatusIndex("DEPLOYED");
	for (ObjectID id : m_ctx.ui.selected())
	{
		Object *o = m_ctx.logic.findObjectByID(id);
		if (!o || o->getContainedBy())
		{
			continue;
		}
		const CommandButton *button = TheCommandStore ? TheCommandStore->findCommandButton(o->isKindOfName("SIEGE_LADDER") ? "Command_SpecialAbilitySiegeLadderDeploy"
		                                                                                                                  : "Command_SpecialAbilitySiegeDeploy")
		                                              : nullptr;
		if (!button || !o->isKindOfName("SIEGE_TOWER") || (kDeployed >= 0 && o->testStatus((unsigned)kDeployed)))
		{
			continue;
		}
		const SpecialPowerTemplate *t = TheSpecialPowerStore && !button->m_specialPowerName.empty() ? TheSpecialPowerStore->findSpecialPowerTemplate(button->m_specialPowerName) : nullptr;
		SpecialPowerModuleInterface *sp = t ? SpecialPowerModules::findModule(*o, t) : nullptr;
		const CommandSet *set = TheCommandStore->findCommandSet(o->getCommandSetName());
		bool inSet = false;
		for (int i = 0; set && i < CommandSet::MAX_BUTTONS; ++i)
		{
			inSet = inSet || set->getCommandButton(i) == button;
		}
		if ((t && !sp) || !inSet) // RW 0x692A75
		{
			continue;
		}
		if (!sp || sp->getPercentReadyForDisplay() != 1.0f) // RW 0x75CC3E
		{
			continue;
		}
		AIUpdateInterface *ai = o->getAIUpdateInterface();
		if (!ai)
		{
			return true;
		}
		SiegeDockingBehavior *dock = SiegeDockingBehavior::of(target);
		if (dock)
		{
			Coord3D approach{};
			if (!dock->approachPointFor(o->getID(), approach))
			{
				return true;
			}
			AIWorld *world = m_ctx.logic.aiWorld();
			if (world && world->mapReady())
			{
				const PathfindLocomotorInfo loco = ai->locomotorInfo();
				if (world->pathfinder().clientSafeQuickDoesPathExist(loco, o->getPosition(), &approach)) // RW 0x66403D -> RW 0x6F5BB0
				{
					return true;
				}
			}
		}
	}
	return false;
}

int CommandTranslator::evaluateSiegeDeploy(Object *obj, EvaluateType type)
{
	if (!canSiegeDeploy(*obj))
	{
		return 0;
	}
	if (type == EvaluateType::DoHint)
	{
		hint(MouseCursorName::AttackObj); // hint 0xB1
		return MSG_DO_SPECIAL_POWER_AT_OBJECT;
	}
	const CommandButton *button = TheCommandStore ? TheCommandStore->findCommandButton("Command_SpecialAbilitySiegeDeploy") : nullptr;
	if (!button)
	{
		return MSG_DO_SPECIAL_POWER_AT_OBJECT; // RW 0x81E80A: no button, the type alone
	}
	const SpecialPowerTemplate *t = TheSpecialPowerStore && !button->m_specialPowerName.empty() ? TheSpecialPowerStore->findSpecialPowerTemplate(button->m_specialPowerName) : nullptr;
	if (type == EvaluateType::DoCommand && t)
	{
		ClientMessage &m = m_ctx.stream.append(MSG_DO_SPECIAL_POWER_AT_OBJECT);
		m.appendInteger((int)t->getID());
		m.appendObjectID(obj->getID());
		m.appendInteger((int)button->m_options);
		m.appendObjectID(INVALID_ID);
		m.appendLocation(*obj->getPosition());
	}
	return MSG_DO_SPECIAL_POWER_AT_OBJECT;
}
