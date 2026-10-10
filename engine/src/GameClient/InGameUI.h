// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// InGameUI (ZH Include/GameClient/InGameUI.h, Source/GameClient/InGameUI.cpp; B1 InGameUI*.cpp), lane HUD-1: the in-game interface state the input translators and the
// control bar share. It is CLIENT state. The one thing it mirrors from the simulation is the local player's selection (ZH keeps "drawable selected"
// flags and a list with push_front; here the ids of the selected objects, the NEWEST first: the first one drives the control bar); the simulation's selection is Player::selection(),
// changed only by the selection messages the translators append (MSG_CREATE_SELECTED_GROUP ...), never from here.
//
// What is not ported (stop S-283): the radius cursor decals, the military subtitles, floating text, superweapon timers, the popup messages, the
// beacon / replay controls, the "can selected objects do X" queries beyond what the translators call and
// the alternate mouse bookkeeping (RotWK's default setup is the alternate one, RW 0x642A4B: CommandTranslator::setUseAlternateMouse, lane PLAY-1).

#pragma once

#include "Common/INIDataTypes.h"
#include "GameClient/GUI/GameWindow.h"
#include "GameLogic/ObjectTypes.h"

#include <string>
#include <vector>

class CommandButton;
class GameLogic;
class MessageStream;

// ZH Mouse::MouseCursor, as the NAMES of the `MouseCursor <Name>` blocks of Data\INI\Mouse.ini (the table the engine picks from)
namespace MouseCursorName
{
inline constexpr const char *Arrow = "Arrow";
inline constexpr const char *Select = "Select";
inline constexpr const char *Move = "Move";
inline constexpr const char *AttackMove = "AttackMove";
inline constexpr const char *AttackObj = "AttackObj";
inline constexpr const char *ForceAttackObj = "ForceAttackObj";
inline constexpr const char *ForceAttackGround = "ForceAttackGround";
inline constexpr const char *GenericInvalid = "GenericInvalid";
inline constexpr const char *EnterFriendly = "EnterFriendly";
inline constexpr const char *EnterAggressive = "EnterAggressive";
inline constexpr const char *SetRallyPoint = "SetRallyPoint";
inline constexpr const char *ResumeConstruction = "ResumeConstruction"; // lane QA-1 (Mouse.ini MouseCursor ResumeConstruction)
inline constexpr const char *DoRepair = "DoRepair";                     // lane QA-1 (Mouse.ini MouseCursor DoRepair)
inline constexpr const char *Scroll = "Scroll";
} // namespace MouseCursorName

class InGameUI
{
public:
	InGameUI() = default;
	InGameUI(const InGameUI &) = delete;
	InGameUI &operator=(const InGameUI &) = delete;

	// the stream the selection changes append their messages to (ZH TheMessageStream)
	void setMessageStream(MessageStream *stream) { m_stream = stream; }

	// ---- the selection (ZH selectDrawable / deselectDrawable / deselectAllDrawables) ----
	const std::vector<ObjectID> &selected() const { return m_selected; }
	int getSelectCount() const { return (int)m_selected.size(); }
	ObjectID firstSelected() const { return m_selected.empty() ? (ObjectID)INVALID_ID : m_selected.front(); }
	bool isSelected(ObjectID id) const;
	void selectObject(ObjectID id);
	void deselectObject(ObjectID id);
	// ZH deselectAllDrawables(postMessage): the message is MSG_DESTROY_SELECTED_GROUP
	void deselectAll(bool postMessage = true);
	// the logic dropped the object (it died): the mirror forgets it without a message
	void forgetObject(ObjectID id) { deselectObject(id); }
	// ZH / RotWK InGameUI::getFrameSelectionChanged (vtable + 0x120, the field + 0x570): the logic frame of the last select / deselect of a drawable
	// (InGameUI::selectDrawable, BFME2 decomp InGameUISelectDrawable.cpp; deselectDrawable); setLogic gives it the logic whose frame it records
	unsigned getFrameSelectionChanged() const { return m_frameSelectionChanged; }
	void setLogic(const GameLogic *logic) { m_logic = logic; }
	int getMaxSelectCount() const { return m_maxSelect; }
	void setMaxSelectCount(int n) { m_maxSelect = n; }

	// ---- the GUI command (ZH setGUICommand / getGUICommand: the command button waiting for a target) ----
	const CommandButton *getGUICommand() const { return m_guiCommand; }
	void setGUICommand(const CommandButton *command);

	// ---- modes ----
	bool isInForceAttackMode() const { return m_forceAttack; }
	void setForceAttackMode(bool on) { m_forceAttack = on; }
	bool isInForceMoveToMode() const { return m_forceMove; }
	void setForceMoveToMode(bool on) { m_forceMove = on; }
	bool isInWaypointMode() const { return m_waypoint; }
	void setWaypointMode(bool on) { m_waypoint = on; }
	bool isInAttackMoveToMode() const { return m_attackMoveTo; }
	void setAttackMoveToMode(bool on) { m_attackMoveTo = on; }
	void clearAttackMoveToMode() { m_attackMoveTo = false; }
	// the "prefer selection" key (shift): selections add to the group
	bool isInPreferSelectionMode() const { return m_preferSelection; }
	void setPreferSelectionMode(bool on) { m_preferSelection = on; }
	bool getInputEnabled() const { return m_inputEnabled; }
	void setInputEnabled(bool on) { m_inputEnabled = on; }
	bool isScrolling() const { return m_scrolling; }
	void setScrolling(bool on) { m_scrolling = on; }
	bool isSelecting() const { return m_selecting; }
	void setSelecting(bool on) { m_selecting = on; }
	bool isQuitMenuVisible() const { return m_quitMenuVisible; }
	void setQuitMenuVisible(bool on) { m_quitMenuVisible = on; }
	// the rectangle of a drag selection under construction, in pixels (valid while isSelecting())
	void setAreaSelectHint(const IRegion2D &r) { m_areaHint = r; m_hasAreaHint = true; }
	void endAreaSelectHint() { m_hasAreaHint = false; }
	bool hasAreaSelectHint() const { return m_hasAreaHint; }
	const IRegion2D &areaSelectHint() const { return m_areaHint; }

	// ---- the pointer ----
	// the cursor the translators chose this frame (a name of Mouse.ini)
	const std::string &cursor() const { return m_cursor; }
	void setCursor(const std::string &name) { m_cursor = name; }
	ObjectID mouseoverObject() const { return m_mouseoverObject; }
	bool hasMouseoverLocation() const { return m_hasMouseoverLocation; }
	const Coord3D &mouseoverLocation() const { return m_mouseoverLocation; }
	void setMouseover(ObjectID id) { m_mouseoverObject = id; m_hasMouseoverLocation = false; }
	void setMouseoverLocation(const Coord3D &c) { m_mouseoverObject = INVALID_ID; m_mouseoverLocation = c; m_hasMouseoverLocation = true; }

	// ---- lane INPUT-1: what a meta command asks of the screen outside the stream (the HUD / the device takes the counts) ----
	// SPELL_STORE (RW 0x820638 -> RW 0x71C6AF: the spell store opens, or closes when it is open), TAKE_SCREENSHOT (RW 0x82019D: Display vslot 0x14C)
	void requestSpellStoreToggle() { ++m_spellStoreToggles; }
	unsigned spellStoreToggles() const { return m_spellStoreToggles; }
	void requestScreenshot() { ++m_screenshots; }
	// DIPLOMACY (Tab, RW 0x81FFAA): the Palantir flag's screen (the players / tribute screen in a skirmish, the objectives in a campaign)
	void requestDiplomacy() { ++m_diplomacy; }
	unsigned diplomacyRequests() const { return m_diplomacy; }
	unsigned screenshotRequests() const { return m_screenshots; }

	// ---- messages for the player (ZH InGameUI::message(label)): game text labels, drained by the HUD ----
	void message(const std::string &label) { m_messages.push_back(label); }
	std::vector<std::string> takeMessages();
	const std::vector<std::string> &messages() const { return m_messages; }

	// ---- the building waiting for placement (ZH placeBuildAvailable; lane BUILD-1: PlaceEventTranslator places it) ----
	// an empty name ends the mode. The ghost (location, angle and the legality code of BuildPlacement) is what the translator sets from the pointer; the device layer draws it
	// `command` is the build button that asked for the site (lane QA2-FIX: RotWK's processCommandUI DOZER_CONSTRUCT case RW 0x940A86 calls placeBuildAvailable, then
	// jumps to RW 0x94089B: setGUICommand(button); the wall span message takes its Options from getGUICommand (RW 0x83E999). The port keeps the button with the placement
	// instead of making it the GUI command, so the translators that wait for a GUI command target are not entered (inference: RotWK's placement end RW 0x83E6A4 clears
	// both together, vslots 0xBC(0) and 0xDC(0, 0))
	void placeBuildAvailable(const std::string &templateName, ObjectID sourceObject, const CommandButton *command = nullptr)
	{
		m_placeTemplate = templateName;
		m_placeSource = sourceObject;
		m_placeCommand = templateName.empty() ? nullptr : command;
		m_placeHasGhost = false;
		m_placeAngle = 0.0f;
		m_placeLegal = 0;
	}
	const std::string &placeBuildTemplate() const { return m_placeTemplate; }
	ObjectID placeBuildSource() const { return m_placeSource; }
	const CommandButton *placeBuildCommand() const { return m_placeCommand; }
	// RotWK's line build flag (InGameUI + 0x8C6; vslot 0xFC reads it, vslot 0x100 sets it, RW 0x48E8ED / 0x48E8F4): the placement update RW 0x6A2D26 sets it on the
	// first client frame a pending line build (a wall hub from a wall hub) is not anchored; the placement release sends the span only while it is set (RW 0x83E924)
	bool isLineBuildStarted() const { return m_lineBuildStarted; }
	void setLineBuildStarted(bool on) { m_lineBuildStarted = on; }
	bool isPlacing() const { return !m_placeTemplate.empty(); }
	void setPlaceGhost(const Coord3D &location, float angle, int legalCode)
	{
		m_placeLocation = location;
		m_placeAngle = angle;
		m_placeLegal = legalCode;
		m_placeHasGhost = true;
	}
	bool placeHasGhost() const { return m_placeHasGhost; }
	const Coord3D &placeLocation() const { return m_placeLocation; }
	float placeAngle() const { return m_placeAngle; }
	// LegalBuildCode (GameLogic/BuildPlacement.h): 0 = the ghost may be placed
	int placeLegalCode() const { return m_placeLegal; }

	// ---- the move hints (lane PLAY-1): the marker drawn where a move order was given ----
	// DONOR FACTS (ZH InGameUI::createMoveHint InGameUI.cpp:2066, HintSpy.cpp:114 .. 119, W3DInGameUI::drawMoveHints W3DInGameUI.cpp:468): HintSpy (translator
	// priority 100) passes every MSG_DO_MOVETO, MSG_DO_ATTACKMOVETO, MSG_DO_FORCEMOVETO and MSG_ADD_WAYPOINT that reaches the end of the stream to createMoveHint,
	// which takes the next of 256 slots (round robin) with the client frame and the message's location, unless the selection is one IMMOBILE object; the device
	// draws GlobalData's MoveHintName model (its "%s.%s" animation once) at every hint whose age is <= 40 client frames, aligned on the terrain.
	// TARGET FACTS: RotWK keeps MoveHintName (GlobalData + 0x10, parseAsciiString RW 0x42EE5E, field row RW 0xBFF5C0; the retail GameData says SCMoveHint).
	// INFERENCE (stop S-1920): RotWK's own hint translator and draw were not read (the binary has no "MoveHint" / "AttackHint" string use beyond the field);
	// MSG_DO_MOVETO_FORMATION (RotWK's formation move) is taken as a move for the hint; ZH's "same source" expiry compares an object id with the location
	// argument and never matches, so it is left out.
	struct MoveHint
	{
		Coord3D pos{};
		unsigned frame = 0; ///< the client frame it was made in; 0 = unused
	};
	static constexpr int MAX_MOVE_HINTS = 256;
	static constexpr unsigned MOVE_HINT_FRAMES = 40;
	void createMoveHint(const Coord3D &pos);
	const MoveHint *moveHints() const { return m_moveHints; }
	// the client frame (ZH TheGameClient->getFrame(): one per 30 Hz client frame; the HUD's camera frame advances it)
	unsigned clientFrame() const { return m_clientFrame; }
	void advanceClientFrame() { ++m_clientFrame; }
	// the hints to draw now: age (client frames) <= MOVE_HINT_FRAMES
	int liveMoveHintCount() const;
	unsigned moveHintsMade() const { return m_moveHintsMade; }

private:
	void markSelectionChanged();
	MoveHint m_moveHints[MAX_MOVE_HINTS];
	int m_nextMoveHint = 0;
	unsigned m_clientFrame = 1; ///< ZH's client frame starts above 0 (a hint's frame 0 means unused)
	unsigned m_moveHintsMade = 0;
	MessageStream *m_stream = nullptr;
	unsigned m_frameSelectionChanged = 0;
	unsigned m_spellStoreToggles = 0, m_screenshots = 0, m_diplomacy = 0;
	const GameLogic *m_logic = nullptr;
	std::vector<ObjectID> m_selected;
	const CommandButton *m_guiCommand = nullptr;
	bool m_forceAttack = false, m_forceMove = false, m_waypoint = false, m_attackMoveTo = false, m_preferSelection = false;
	bool m_inputEnabled = true, m_scrolling = false, m_selecting = false, m_quitMenuVisible = false;
	IRegion2D m_areaHint{};
	bool m_hasAreaHint = false;
	std::string m_cursor = MouseCursorName::Arrow;
	ObjectID m_mouseoverObject = INVALID_ID;
	Coord3D m_mouseoverLocation;
	bool m_hasMouseoverLocation = false;
	std::vector<std::string> m_messages;
	std::string m_placeTemplate;
	ObjectID m_placeSource = INVALID_ID;
	const CommandButton *m_placeCommand = nullptr;
	bool m_lineBuildStarted = false;
	Coord3D m_placeLocation;
	float m_placeAngle = 0.0f;
	int m_placeLegal = 0;
	bool m_placeHasGhost = false;
	int m_maxSelect = 0;
};
