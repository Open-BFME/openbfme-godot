// OpenBFME. GPL-3.0.
// See SelectionXlat.h.

#include "GameClient/MessageStream/SelectionXlat.h"

#include "Common/Player.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameClient/HudObjects.h"
#include "GameLogic/PlayerCommands.h"
#include "GameLogic/Object/Object.h"

#include <algorithm>
#include <cstdlib>
#include <set>

namespace
{
// ZH SelectionInfo.cpp addDrawableToList: what a pick may add. A pick that is not selectable but is contained stands for its container when that is
// selectable (the horde); the shroud (VIS-1) is not ported (stop S-288); an enemy's invisible object is not picked (HudObjects, lane STEALTH-1).
Object *listable(HudContext &ctx, Object *o)
{
	if (!o)
	{
		return nullptr;
	}
	if (!HudObjects::isSelectable(*o))
	{
		Object *c = o->getContainedBy();
		if (c && HudObjects::isSelectable(*c))
		{
			return c;
		}
		return nullptr;
	}
	(void)ctx;
	return o;
}

std::vector<Object *> drawablesThatWillSelect(HudContext &ctx, const IRegion2D &region)
{
	std::vector<Object *> out;
	for (Object *o : HudObjects::objectsInRegion(ctx, region))
	{
		if (Object *l = listable(ctx, o))
		{
			if (std::find(out.begin(), out.end(), l) == out.end())
			{
				out.push_back(l);
			}
		}
	}
	return out;
}

bool areAllSelected(const HudContext &ctx, const std::vector<Object *> &list)
{
	for (Object *o : list)
	{
		if (!ctx.ui.isSelected(o->getID()))
		{
			return false;
		}
	}
	return !list.empty();
}
} // namespace

void SelectionTranslator::buildRegion(const ICoord2D &a, const ICoord2D &b, IRegion2D &out) const
{
	// ZH InGameUI::buildRegion
	out.lo.x = std::min(a.x, b.x);
	out.hi.x = std::max(a.x, b.x);
	out.lo.y = std::min(a.y, b.y);
	out.hi.y = std::max(a.y, b.y);
}

bool SelectionTranslator::contextCommandForNewSelection(const std::vector<Object *> &newlySelected, SelectionInfo &si, bool selectionIsPoint)
{
	// ZH contextCommandForNewSelection (SelectionInfo.cpp:69)
	if (m_ctx.ui.isInForceAttackMode() || m_ctx.ui.isInForceMoveToMode())
	{
		return false;
	}
	for (ObjectID id : m_ctx.ui.selected())
	{
		Object *obj = m_ctx.logic.findObjectByID(id);
		if (!obj)
		{
			continue;
		}
		if (HudObjects::isLocallyControlled(m_ctx, *obj))
		{
			++si.currentCountMine;
			if (obj->isKindOfName("INFANTRY"))
			{
				++si.currentCountMineInfantry;
			}
			else if (obj->isKindOfName("STRUCTURE"))
			{
				++si.currentCountMineBuildings;
			}
		}
		else
		{
			switch (HudObjects::relationshipToLocal(m_ctx, *obj))
			{
				case ALLIES: ++si.currentCountFriends; break;
				case ENEMIES: ++si.currentCountEnemies; break;
				case NEUTRAL: ++si.currentCountCivilians; break;
			}
		}
	}
	Object *newMine = nullptr, *newFriendly = nullptr, *newEnemy = nullptr, *newCivilian = nullptr;
	for (Object *obj : newlySelected)
	{
		// a garrisonable building (ActionManager::canPlayerGarrison) is not ported (stop S-286): newCountGarrisonableBuildings stays 0
		if (obj->isKindOfName("CRATE"))
		{
			++si.newCountCrates;
		}
		if (HudObjects::isLocallyControlled(m_ctx, *obj))
		{
			++si.newCountMine;
			newMine = obj;
			if (obj->isKindOfName("STRUCTURE"))
			{
				++si.newCountMineBuildings;
			}
		}
		else
		{
			switch (HudObjects::relationshipToLocal(m_ctx, *obj))
			{
				case ALLIES: newFriendly = obj; ++si.newCountFriends; break;
				case ENEMIES: newEnemy = obj; ++si.newCountEnemies; break;
				case NEUTRAL: newCivilian = obj; ++si.newCountCivilians; break;
			}
		}
	}
	if (si.currentCountEnemies > 0 || si.currentCountFriends > 0 || si.currentCountCivilians > 0)
	{
		return false; // a foreign object selected: no context command
	}
	if (m_commands.useAlternateMouse())
	{
		return false; // context commands never apply when selecting in the alternate mouse setup
	}
	auto evaluates = [&](Object *o) { return m_commands.evaluateContextCommand(o, o->getPosition(), EvaluateType::EvaluateOnly) != 0; };
	if (si.currentCountMine > 0)
	{
		if (si.newCountEnemies > 0)
		{
			if (si.newCountEnemies == 1 && selectionIsPoint)
			{
				return evaluates(newEnemy);
			}
			return selectionIsPoint;
		}
		if (si.newCountMine > 0)
		{
			if (si.newCountMine == 1 && selectionIsPoint && !m_ctx.ui.isInPreferSelectionMode())
			{
				return evaluates(newMine);
			}
			return false;
		}
		if (si.newCountFriends > 0)
		{
			if (si.newCountFriends == 1 && selectionIsPoint)
			{
				return evaluates(newFriendly);
			}
			return false;
		}
		if (si.currentCountMineInfantry > 0 && si.newCountGarrisonableBuildings == 1)
		{
			return true;
		}
		if (si.newCountCivilians > 0)
		{
			if (si.newCountCivilians == 1 && selectionIsPoint)
			{
				return evaluates(newCivilian);
			}
			return false;
		}
		if (si.newCountCrates > 0)
		{
			return si.newCountCrates == 1 && selectionIsPoint;
		}
	}
	if (si.currentCountMine == 0)
	{
		return false;
	}
	return selectionIsPoint;
}

bool SelectionTranslator::selectSingleWithoutSound(Object &obj)
{
	// ZH selectSingleDrawableWithoutSound: unselect everything else, select it, MSG_CREATE_SELECTED_GROUP_NO_SOUND (new group, id)
	m_ctx.ui.deselectAll();
	m_ctx.ui.selectObject(obj.getID());
	ClientMessage &m = m_ctx.stream.append(MSG_CREATE_SELECTED_GROUP_NO_SOUND);
	m.appendBoolean(true);
	m.appendObjectID(obj.getID());
	return true;
}

int SelectionTranslator::selectMatching(Object *picked, bool acrossMap)
{
	// ZH InGameUI::selectUnitsMatchingCurrentSelection / selectMatchingAcrossScreen / AcrossMap (InGameUI.cpp:4690-4940): the templates of the locally controlled
	// selection, every object of those templates on the screen (or the map), one MSG_CREATE_SELECTED_GROUP_NO_SOUND that adds them
	std::set<const ThingTemplate *> templates;
	for (ObjectID id : m_ctx.ui.selected())
	{
		if (Object *o = m_ctx.logic.findObjectByID(id))
		{
			if (HudObjects::isLocallyControlled(m_ctx, *o) && o->getTemplate())
			{
				templates.insert(o->getTemplate());
			}
		}
	}
	(void)picked;
	if (templates.empty())
	{
		return 0;
	}
	const ICoord2D size = m_ctx.view.size();
	std::vector<Object *> found;
	for (Object *o = m_ctx.logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (o->isDestroyed() || o->getContainedBy() || !HudObjects::isLocallyControlled(m_ctx, *o) || !HudObjects::isSelectable(*o) || o->isKindOfName("STRUCTURE"))
		{
			continue;
		}
		if (!templates.count(o->getTemplate()) || m_ctx.ui.isSelected(o->getID()))
		{
			continue;
		}
		if (!acrossMap)
		{
			ICoord2D px;
			if (!m_ctx.view.worldToScreen(*o->getPosition(), px) || px.x < 0 || px.y < 0 || px.x > size.x || px.y > size.y)
			{
				continue;
			}
		}
		if (m_ctx.ui.getMaxSelectCount() > 0 && m_ctx.ui.getSelectCount() + (int)found.size() >= m_ctx.ui.getMaxSelectCount())
		{
			break;
		}
		found.push_back(o);
	}
	if (found.empty())
	{
		return 0;
	}
	ClientMessage &m = m_ctx.stream.append(MSG_CREATE_SELECTED_GROUP_NO_SOUND);
	m.appendBoolean(false);
	for (Object *o : found)
	{
		m_ctx.ui.selectObject(o->getID());
		m.appendObjectID(o->getID());
	}
	return (int)found.size();
}

const std::vector<ObjectID> &SelectionTranslator::squadFor(Player *player, int group)
{
	if (m_predicting && m_ctx.logic.getFrame() != m_predictionFrame)
	{
		m_predicting = false; // the queued messages ran: the player's squads are the truth again
	}
	return m_predicting ? m_predicted[group] : player->hotkeySquad(group);
}

void SelectionTranslator::predict(Player *player, int group, const std::vector<ObjectID> &members, bool create)
{
	if (!player)
	{
		return;
	}
	squadFor(player, group); // drops a stale prediction
	if (!m_predicting)
	{
		for (int s = 0; s < Player::NUM_HOTKEY_SQUADS; ++s)
		{
			m_predicted[s] = player->hotkeySquad(s);
		}
		m_predicting = true;
		m_predictionFrame = m_ctx.logic.getFrame();
	}
	// PlayerCommands createTeam (RW 0x6AD555): the group is replaced, each member leaves every other squad; addToTeam (RW 0x6AD722) keeps the group
	if (create)
	{
		m_predicted[group].clear();
	}
	for (ObjectID id : members)
	{
		for (int s = 0; s < Player::NUM_HOTKEY_SQUADS; ++s)
		{
			std::vector<ObjectID> &other = m_predicted[s];
			other.erase(std::remove(other.begin(), other.end(), id), other.end());
		}
		m_predicted[group].push_back(id);
	}
}

void SelectionTranslator::viewSquad(int group)
{
	Player *player = m_ctx.localPlayer();
	if (!player || group < 0 || group >= Player::NUM_HOTKEY_SQUADS)
	{
		return;
	}
	const std::vector<ObjectID> &squad = squadFor(player, group);
	// the last live member (RW 0x83D027: Squad::getLiveObjects()[size - 1], TacticalView::lookAt its drawable's position); getLiveObjects (RW 0x8DB103)
	// hands out the members Object::isSelectable (RW 0x68DE58) passes: a garrisoned, dead or unselectable member is skipped, not dropped (lane INPUT-1 r2)
	for (size_t i = squad.size(); i-- > 0;)
	{
		Object *o = m_ctx.logic.findObjectByID(squad[i]);
		if (o && PlayerCommands::isSelectable(*o))
		{
			m_ctx.view.lookAt(*o->getPosition());
			return;
		}
	}
}

MessageDisposition SelectionTranslator::doubleClick(const ClientMessage &msg)
{
	const int modifiers = msg.arg(1).integer;
	(void)modifiers;
	if (m_ctx.ui.isInForceAttackMode())
	{
		return MessageDisposition::Keep; // ctrl is disallowed for double clicking
	}
	const IRegion2D &region = msg.arg(0).region;
	if (region.lo.x != region.hi.x || region.lo.y != region.hi.y)
	{
		return MessageDisposition::Keep;
	}
	Object *picked = HudObjects::pickForDoubleClick(m_ctx, region.lo); // RotWK picks with SELECTABLE only (RW 0x81F7C5, S-1954)
	if (!picked || !HudObjects::isSelectable(*picked) || !HudObjects::isLocallyControlled(m_ctx, *picked))
	{
		return MessageDisposition::Keep; // nobody to pick: propagate the double click
	}
	std::vector<ObjectID> before;
	if (m_ctx.ui.isInPreferSelectionMode())
	{
		before = m_ctx.ui.selected();
	}
	selectSingleWithoutSound(*picked);
	// the alt key (KEY_STATE_ALT = 0x...) selects across the map; the screen otherwise (the key state flag values are the host's: ALT = 4)
	selectMatching(picked, (modifiers & 4) != 0);
	m_ctx.stream.append(MSG_AREA_SELECTION).appendObjectID(picked->getID());
	if (!before.empty())
	{
		ClientMessage &more = m_ctx.stream.append(MSG_CREATE_SELECTED_GROUP_NO_SOUND);
		more.appendBoolean(false);
		for (ObjectID id : before)
		{
			if (Object *o = m_ctx.logic.findObjectByID(id))
			{
				if (HudObjects::isSelectable(*o))
				{
					m_ctx.ui.selectObject(id);
					more.appendObjectID(id);
				}
			}
		}
	}
	return MessageDisposition::Destroy;
}

MessageDisposition SelectionTranslator::leftClick(const ClientMessage &msg)
{
	if (m_ctx.ui.isQuitMenuVisible())
	{
		return MessageDisposition::Destroy;
	}
	const IRegion2D selectionRegion = msg.arg(0).region;
	const bool isPoint = selectionRegion.lo.x == selectionRegion.hi.x && selectionRegion.lo.y == selectionRegion.hi.y;
	std::vector<Object *> will = drawablesThatWillSelect(m_ctx, selectionRegion);
	if (will.empty())
	{
		return MessageDisposition::Keep;
	}
	if (m_ctx.ui.getGUICommand() != nullptr)
	{
		return MessageDisposition::Keep; // currentlyLookingForSelection: a GUI command waits for its target
	}
	SelectionInfo si;
	if (contextCommandForNewSelection(will, si, isPoint))
	{
		return MessageDisposition::Keep;
	}
	bool addToGroup = m_ctx.ui.isInPreferSelectionMode();
	if (si.currentCountEnemies > 0 || si.currentCountCivilians > 0 || si.currentCountFriends > 0 || si.currentCountMineBuildings > 0)
	{
		addToGroup = false; // force a new group creation
	}
	if (si.newCountMine > 0)
	{
		si.selectMine = true;
		if (si.newCountMineBuildings == 1 && si.newCountMine == 1)
		{
			addToGroup = false;
			si.selectMineBuildings = true;
		}
		else if (si.newCountMineBuildings > 0)
		{
			// B1: the drag may take the building when everything else in the list is unselectable anyway
			bool onlyTheOneBuildingIsSelectableAnyway = true;
			ObjectID buildingID = INVALID_ID;
			for (Object *d : will)
			{
				if (d->isKindOfName("STRUCTURE"))
				{
					if (buildingID == INVALID_ID)
					{
						buildingID = d->getID();
					}
					else if (buildingID != d->getID())
					{
						onlyTheOneBuildingIsSelectableAnyway = false;
					}
				}
				else if (HudObjects::isSelectable(*d))
				{
					onlyTheOneBuildingIsSelectableAnyway = false;
				}
				if (!onlyTheOneBuildingIsSelectableAnyway)
				{
					break;
				}
			}
			if (onlyTheOneBuildingIsSelectableAnyway)
			{
				addToGroup = false;
				si.selectMineBuildings = true;
			}
		}
	}
	else if (si.newCountEnemies > 0 && si.newCountCivilians > 0 && si.newCountFriends > 0)
	{
		return MessageDisposition::Keep;
	}
	else if (si.newCountEnemies == 1)
	{
		addToGroup = false;
		si.selectEnemies = true;
	}
	else if (si.newCountCivilians == 1)
	{
		addToGroup = false;
		si.selectCivilians = true;
	}
	else if (si.newCountFriends == 1)
	{
		addToGroup = false;
		si.selectFriends = true;
	}
	if (!(si.selectMine || si.selectEnemies || si.selectCivilians || si.selectFriends))
	{
		return MessageDisposition::Keep;
	}
	// it is a selection
	m_lastGroupSelGroup = -1;
	if (m_ctx.ui.isInPreferSelectionMode() && isPoint && areAllSelected(m_ctx, will))
	{
		// a point click with the add key on a selected unit deselects it
		ClientMessage &rm = m_ctx.stream.append(MSG_REMOVE_FROM_SELECTED_GROUP);
		for (Object *o : will)
		{
			rm.appendObjectID(o->getID());
			m_ctx.ui.deselectObject(o->getID());
		}
	}
	else
	{
		if (!addToGroup)
		{
			m_ctx.ui.deselectAll();
		}
		ClientMessage &group = m_ctx.stream.append(MSG_CREATE_SELECTED_GROUP);
		group.appendBoolean(!addToGroup);
		for (Object *obj : will)
		{
			if (obj->getContainedBy())
			{
				continue; // contained: not selectable
			}
			bool take = false;
			if (si.selectMine && HudObjects::isLocallyControlled(m_ctx, *obj))
			{
				take = !obj->isKindOfName("STRUCTURE") || si.selectMineBuildings;
			}
			else
			{
				const Relationship rel = HudObjects::relationshipToLocal(m_ctx, *obj);
				take = (si.selectEnemies && rel == ENEMIES) || (si.selectCivilians && rel == NEUTRAL) || (si.selectFriends && rel == ALLIES);
			}
			if (take)
			{
				group.appendObjectID(obj->getID());
				m_ctx.ui.selectObject(obj->getID());
			}
		}
	}
	m_ctx.ui.clearAttackMoveToMode();
	return MessageDisposition::Destroy;
}

MessageDisposition SelectionTranslator::translate(const ClientMessage &msg)
{
	MessageDisposition disp = MessageDisposition::Keep;
	if (!m_ctx.ui.getInputEnabled())
	{
		if (m_dragSelecting)
		{
			m_dragSelecting = false;
			m_ctx.ui.setSelecting(false);
			m_ctx.ui.endAreaSelectHint();
		}
		return MessageDisposition::Keep;
	}
	const int t = msg.type();
	switch (t)
	{
		case CMSG_META_SELECT_MATCHING_UNITS:
			// lane QA-1 (stop S-1202): the E key. DONOR (ZH InGameUI::selectUnitsMatchingCurrentSelection, InGameUI.cpp:4924): the templates of the selection on the
			// screen, and across the map when the screen adds none. RotWK's SELECT_MATCHING_UNITS handler is not read
			if (selectMatching(nullptr, false) == 0)
			{
				selectMatching(nullptr, true);
			}
			return MessageDisposition::Destroy;
		case CMSG_RAW_MOUSE_POSITION:
		{
			const ICoord2D pixel = msg.arg(0).pixel;
			if (m_leftDown)
			{
				const int dx = std::abs(pixel.x - m_selectAnchor.x), dy = std::abs(pixel.y - m_selectAnchor.y);
				if (dx > m_ctx.mouse.dragTolerance || dy > m_ctx.mouse.dragTolerance)
				{
					if (!m_dragSelecting)
					{
						m_dragSelecting = true;
						m_ctx.ui.setSelecting(true);
					}
				}
				if (m_dragSelecting)
				{
					IRegion2D r;
					buildRegion(m_selectAnchor, pixel, r);
					m_ctx.ui.setAreaSelectHint(r);
					m_ctx.stream.append(CMSG_AREA_SELECTION_HINT).appendPixelRegion(r);
				}
			}
			else
			{
				// the mouseover hint for the command translator and the cursor
				Object *under = HudObjects::pickForHover(m_ctx, pixel); // RW 0x83CC13
				if (under && (!HudObjects::isEffectivelyDead(*under) || under->isKindOfName("ALWAYS_SELECTABLE")))
				{
					m_ctx.ui.setMouseover(under->getID());
					m_ctx.stream.append(CMSG_MOUSEOVER_DRAWABLE_HINT).appendObjectID(under->getID());
				}
				else
				{
					Coord3D position;
					if (m_ctx.view.screenToTerrain(pixel, m_ctx.logic, position))
					{
						m_ctx.ui.setMouseoverLocation(position);
						m_ctx.stream.append(CMSG_MOUSEOVER_LOCATION_HINT).appendLocation(position);
					}
				}
				m_ctx.ui.setCursor(MouseCursorName::Arrow);
				if (under && m_ctx.ui.getGUICommand() == nullptr && HudObjects::canSelect(m_ctx, *under, false) && !under->isKindOfName("SHRUBBERY"))
				{
					m_ctx.ui.setCursor(MouseCursorName::Select);
				}
			}
			break;
		}
		case CMSG_MOUSE_LEFT_DOUBLE_CLICK:
			disp = doubleClick(msg);
			break;
		case CMSG_MOUSE_LEFT_CLICK:
			disp = leftClick(msg);
			break;
		case CMSG_RAW_MOUSE_LEFT_BUTTON_DOWN:
			m_leftDown = true;
			m_selectAnchor = msg.arg(0).pixel;
			break;
		case CMSG_RAW_MOUSE_LEFT_BUTTON_UP:
			m_leftDown = false;
			if (m_dragSelecting)
			{
				m_dragSelecting = false;
				m_ctx.ui.setSelecting(false);
				m_ctx.ui.endAreaSelectHint();
				// ZH appends MSG_AREA_SELECTION carrying the pixel region here; GameMessage has no pixel argument types, and the logic does not act on the
				// message (S-208), so it is not emitted for a drag (stop S-280). The selection itself is the left click the MetaEventTranslator derives from
				// this button up with the drag rectangle as its region.
			}
			break;
		case CMSG_RAW_MOUSE_RIGHT_BUTTON_DOWN:
			m_deselectAnchor = msg.arg(0).pixel;
			m_lastClick = msg.arg(2).integer;
			m_deselectDownCamera = m_ctx.view.position();
			break;
		case CMSG_RAW_MOUSE_RIGHT_BUTTON_UP:
		{
			const ICoord2D pixel = msg.arg(0).pixel;
			const int now = msg.arg(2).integer;
			bool isClick = true;
			if (std::abs(m_deselectAnchor.x - pixel.x) > m_ctx.mouse.dragTolerance || std::abs(m_deselectAnchor.y - pixel.y) > m_ctx.mouse.dragTolerance)
			{
				isClick = false;
			}
			if (isClick && now - m_lastClick > m_ctx.mouse.dragToleranceMS)
			{
				isClick = false;
			}
			if (isClick && HudObjects::cameraMovedBeyond(m_deselectDownCamera, m_ctx.view.position(), m_ctx.mouse.dragTolerance3D))
			{
				isClick = false;
			}
			if (isClick)
			{
				if (m_ctx.ui.getGUICommand())
				{
					// cancel the GUI command mode without deselecting
					m_ctx.ui.setGUICommand(nullptr);
					disp = MessageDisposition::Destroy;
					m_ctx.ui.setScrolling(false);
				}
				else if (!m_commands.useAlternateMouse())
				{
					m_ctx.ui.deselectAll();
				}
			}
			break;
		}
		default:
			break;
	}
	// the control groups: RotWK's SelectionTranslator::translateGameMessage (RW 0x83C29E, the meta cases 0x34 .. 0x5B and 0xA1 .. 0xAA; lane INPUT-1)
	if ((t >= CMSG_META_CREATE_TEAM0 && t <= CMSG_META_CREATE_TEAM9) || (t >= CMSG_META_ADD_TO_TEAM0 && t <= CMSG_META_ADD_TO_TEAM9))
	{
		// RW 0x83CD2C (CREATE_TEAMn: MSG_CREATE_TEAM0 + n) / 0x83D25A (ADD_TO_TEAMn: MSG_ADD_TO_TEAM0 + n): the message carries the ids of the
		// selected drawables whose object is locally controlled, in the client's drawable list order (GameClient + 0x8C, next at + 0x104).
		// INFERENCE: that list is newest first (ZH GameClient::addDrawable prepends), here the selection by descending object id.
		const bool create = t <= CMSG_META_CREATE_TEAM9;
		const int group = t - (create ? CMSG_META_CREATE_TEAM0 : CMSG_META_ADD_TO_TEAM0);
		ClientMessage &m = m_ctx.stream.append((create ? MSG_CREATE_TEAM0 : MSG_ADD_TO_TEAM0) + group);
		std::vector<ObjectID> members;
		for (ObjectID id : m_ctx.ui.selected())
		{
			if (Object *o = m_ctx.logic.findObjectByID(id))
			{
				if (HudObjects::isLocallyControlled(m_ctx, *o))
				{
					members.push_back(id);
				}
			}
		}
		std::sort(members.begin(), members.end(), [](ObjectID a, ObjectID b) { return a > b; });
		for (ObjectID id : members)
		{
			m.appendObjectID(id);
		}
		predict(m_ctx.localPlayer(), group, members, create);
		return MessageDisposition::Destroy;
	}
	if (t >= CMSG_META_SELECT_TEAM0 && t <= CMSG_META_SELECT_TEAM9)
	{
		// RW 0x83CF3A
		const int group = t - CMSG_META_SELECT_TEAM0;
		const unsigned now = m_ctx.logic.getFrame(); // TheGameLogic + 0x40
		if (m_lastGroupSelTime == 0)
		{
			m_lastGroupSelTime = now;
		}
		// RW 0x83CF64: the last group counts for a double press only while the selection has not changed since (InGameUI::getFrameSelectionChanged,
		// vtable + 0x120) and every live member with a drawable is still selected
		if (m_lastGroupSelGroup >= 0)
		{
			Player *player = m_ctx.localPlayer();
			bool keep = m_ctx.ui.getFrameSelectionChanged() <= m_lastGroupSelTime && player;
			if (keep)
			{
				for (ObjectID id : squadFor(player, m_lastGroupSelGroup))
				{
					const Object *o = m_ctx.logic.findObjectByID(id);
					if (o && PlayerCommands::isSelectable(*o) && !m_ctx.ui.isSelected(id))
					{
						keep = false;
						break;
					}
				}
			}
			if (!keep)
			{
				m_lastGroupSelGroup = -1;
			}
		}
		if (now - m_lastGroupSelTime < kGroupDoublePressFrames && group == m_lastGroupSelGroup)
		{
			viewSquad(group); // RW 0x83CFEF
		}
		else
		{
			// RW 0x83D04E: MSG_DESTROY_SELECTED_GROUP (true), deselect all, MSG_SELECT_TEAM0 + n, then the members the local player controls
			m_ctx.stream.append(MSG_DESTROY_SELECTED_GROUP).appendBoolean(true);
			m_ctx.ui.deselectAll(false);
			m_ctx.stream.append(MSG_SELECT_TEAM0 + group);
			if (Player *player = m_ctx.localPlayer())
			{
				for (ObjectID id : squadFor(player, group))
				{
					Object *o = m_ctx.logic.findObjectByID(id);
					if (o && PlayerCommands::isSelectable(*o) && o->getControllingPlayer() == player)
					{
						m_ctx.ui.selectObject(id);
					}
				}
			}
		}
		m_lastGroupSelTime = now;
		m_lastGroupSelGroup = group;
		return MessageDisposition::Destroy;
	}
	if (t >= CMSG_META_ADD_TEAM0 && t <= CMSG_META_ADD_TEAM9)
	{
		// RW 0x83CE30: as SELECT_TEAM without the selection-changed test; MSG_ADD_TEAM0 + n, every live member is selected (no controlling player test)
		const int group = t - CMSG_META_ADD_TEAM0;
		const unsigned now = m_ctx.logic.getFrame();
		if (m_lastGroupSelTime == 0)
		{
			m_lastGroupSelTime = now;
		}
		if (now - m_lastGroupSelTime < kGroupDoublePressFrames && group == m_lastGroupSelGroup)
		{
			viewSquad(group);
		}
		else
		{
			m_ctx.stream.append(MSG_ADD_TEAM0 + group);
			if (Player *player = m_ctx.localPlayer())
			{
				for (ObjectID id : squadFor(player, group))
				{
					Object *o = m_ctx.logic.findObjectByID(id);
					if (o && PlayerCommands::isSelectable(*o))
					{
						m_ctx.ui.selectObject(id);
					}
				}
			}
		}
		m_lastGroupSelTime = now;
		m_lastGroupSelGroup = group;
		return MessageDisposition::Destroy;
	}
	if (t >= CMSG_META_VIEW_TEAM0 && t <= CMSG_META_VIEW_TEAM9)
	{
		// RW 0x83CDC0: the index test is `0 < n && n < 11`: VIEW_TEAM0 (Alt+0) looks nowhere in RotWK 2.01 (TARGET FACT, kept)
		const int group = t - CMSG_META_VIEW_TEAM0;
		if (group > 0)
		{
			viewSquad(group);
		}
		return MessageDisposition::Destroy;
	}
	if (t == CMSG_META_OPTIONS)
	{
		m_leftDown = false;
	}
	return disp;
}
