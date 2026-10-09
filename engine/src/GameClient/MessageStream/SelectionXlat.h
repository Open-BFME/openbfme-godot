// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// SelectionTranslator (ZH Include/GameClient/SelectionXlat.h, Source/GameClient/MessageStream/SelectionXlat.cpp, SelectionInfo.cpp; B1 SelectionXlat.cpp), lane HUD-1:
// click selection, drag (area) selection, double click "select all of the type", shift add / toggle, right click deselect, the control groups
// (CREATE / SELECT / ADD / VIEW team) and the mouseover hints. Ported from the B1 body (ZH's with the BFME changes of the decompile); the debug
// modes (hand of god, hurt me, debug selection) are not ported.
//
// DONOR FACTS (B1 SelectionXlat.cpp): the raw left messages only draw feedback; every selection happens at MSG_MOUSE_LEFT_CLICK / DOUBLE_CLICK, which are
// kept for the CommandTranslator when the click selects nothing or is a context command (contextCommandForNewSelection, SelectionInfo.cpp); a left
// click selects through MSG_CREATE_SELECTED_GROUP (arg: create a new group, then the object ids); a point click with the add mode on an already
// selected object removes it (MSG_REMOVE_FROM_SELECTED_GROUP); a drag ends in MSG_AREA_SELECTION (the region) which falls through to
// the left click handling with the region; a click on a group key twice within 20 logic frames looks at the group.
//
// TARGET: RotWK 2.01's translator was not read (stop S-280); the double click selects units of the same template on the screen (ZH selectMatchingAcrossScreen:
// here the objects of the same template whose position projects on the screen) and the alt key across the map.

#pragma once

#include "GameClient/HudContext.h"
#include "GameClient/MessageStream/CommandXlat.h"
#include "GameClient/MessageStream/MessageStream.h"

class Object;

// ZH SelectionInfo (SelectionInfo.h)
struct SelectionInfo
{
	int currentCountEnemies = 0, currentCountCivilians = 0, currentCountMine = 0, currentCountMineInfantry = 0, currentCountMineBuildings = 0, currentCountFriends = 0;
	int newCountEnemies = 0, newCountCivilians = 0, newCountCrates = 0, newCountMine = 0, newCountMineBuildings = 0, newCountFriends = 0, newCountGarrisonableBuildings = 0;
	bool selectEnemies = false, selectCivilians = false, selectMine = false, selectMineBuildings = false, selectFriends = false;
};

class SelectionTranslator : public MessageTranslator
{
public:
	SelectionTranslator(HudContext &ctx, CommandTranslator &commands) : m_ctx(ctx), m_commands(commands) {}
	MessageDisposition translate(const ClientMessage &message) override;

	// ZH contextCommandForNewSelection (SelectionInfo.cpp): true when the click is a command, not a selection
	bool contextCommandForNewSelection(const std::vector<Object *> &newlySelected, SelectionInfo &out, bool selectionIsPoint);
	void setDragSelecting(bool on) { m_dragSelecting = on; }
	void setLeftMouseButton(bool down) { m_leftDown = down; }
	bool isDragSelecting() const { return m_dragSelecting; }

private:
	bool selectSingleWithoutSound(Object &obj);
	// the selection's templates on the screen (or the map); the number of objects it added
	int selectMatching(Object *picked, bool acrossMap);
	void buildRegion(const ICoord2D &a, const ICoord2D &b, IRegion2D &out) const;
	void viewSquad(int group);
	// The squads as they stand once the queued create-group messages have run: the translator creates a group and may read it again before the
	// next logic frame dispatches the message (review HUD-1 r1 #2). The prediction is dropped when the logic frame has moved on (the dispatch ran).
	const std::vector<ObjectID> &squadFor(Player *player, int group);
	void predictCreate(Player *player, int group, const std::vector<ObjectID> &members);
	MessageDisposition leftClick(const ClientMessage &msg);
	MessageDisposition doubleClick(const ClientMessage &msg);

	HudContext &m_ctx;
	CommandTranslator &m_commands;
	bool m_leftDown = false, m_dragSelecting = false;
	ICoord2D m_selectAnchor{}, m_deselectAnchor{};
	int m_lastClick = 0;
	Coord3D m_deselectDownCamera;
	unsigned m_lastGroupSelTime = 0;
	bool m_predicting = false;
	unsigned m_predictionFrame = 0;
	std::vector<ObjectID> m_predicted[10];
	int m_lastGroupSelGroup = -1;
	bool m_displayedMaxWarning = false;
};
