// OpenBFME. GPL-3.0.
//
// HudInput (lane HUD-1): the entry point the live game installs for player input. It owns the client message stream and its translators (ZH GameClient.cpp:297-315:
// MetaEvent 20, PlaceEvent 30, GUICommand 40, Selection 50, Command 70), takes raw input events from the device layer, runs the stream once per update and appends
// every logic message that reaches its end to the lockstep CommandList as a GameMessage of the local player (PLAN rule 4; the HUD never changes the
// simulation directly). The InGameUI it owns is what the control bar and the cursor code read.
//
// Raw input events carry window pixels of the tactical view and a millisecond time stamp; the device layer decides which events belong to the game (a click on
// the Palantir belongs to its APT movie: `overGui`), the translators never see those.

#pragma once

#include "GameClient/HudContext.h"
#include "GameClient/FormationPlacement.h"
#include "GameClient/MessageStream/CommandXlat.h"
#include "GameClient/MessageStream/GUICommandTranslator.h"
#include "GameClient/MessageStream/HotKey.h"
#include "GameClient/MessageStream/LookAtXlat.h"
#include "GameClient/MessageStream/MessageStream.h"
#include "GameClient/MessageStream/MetaEvent.h"
#include "GameClient/MessageStream/PlaceEventTranslator.h"
#include "GameClient/MessageStream/SelectionXlat.h"
#include "GameClient/TacticalCamera.h"
#include "GameLogic/GameLogicDispatch.h"

#include <functional>
#include <memory>

class HudInput
{
public:
	enum class Button
	{
		Left,
		Middle,
		Right
	};

	// `commands` is the list the logic's command row drains each frame; `view` is the tactical view the pointer is in; `metaMap` the loaded CommandMap
	HudInput(GameLogic &logic, AIWorld *ai, TacticalView &view, CommandList &commands, const MouseSettings &mouse, const MetaMap &metaMap);
	HudInput(const HudInput &) = delete;
	HudInput &operator=(const HudInput &) = delete;

	// ---- raw input (ZH Mouse / Keyboard createStreamMessages) ----
	void mouseMove(int x, int y, int keyState = 0);
	void mouseButton(Button button, bool down, int x, int y, int keyState, int timeMs, bool doubleClick = false, bool overGui = false);
	// a key: `key` a KeyCode, `keyState` the KeyState flags including the modifier flags after this event (KEY_STATE_DOWN / UP, AUTOREPEAT)
	// `character`: the keyboard layout's character of the key (lane INPUT-1: RotWK's hotkeys look the character up, RW 0x63F14D), 0 when unknown
	void key(int key, int keyState, char32_t character = 0);
	// lane BACKLOG3 (S-454): the keyboard's state the LookAt translator reads (TheKeyboard's key table, RW 0x63F0E2): a key is down from its press to its
	// release. resetKeys is ZH Keyboard::resetKeys (the window lost the focus: every key is up, no release reaches the stream). INFERENCE (S-454): that
	// RotWK resets its keyboard there too was not read
	bool isKeyDown(int key) const { return m_keyDown[key & 0xFF]; }
	void resetKeys();
	// lane OPTS-6: input that began before a modal menu opened is cancelled: no key is down, no scroll / rotate / zoom is held (S-4982)
	void cancelHeld();
	// the wheel: `spin` notches (positive = away from the player = zoom in; ZH MSG_RAW_MOUSE_WHEEL carries the pixel and the spin)
	void mouseWheel(int spin, int x, int y);

	// Lane CAM-1: the retail tactical camera. Attaches the LookAt translator (ZH priority 60: arrow keys, right button drag, screen edge, middle button rotation, wheel) on
	// `camera`, which must be the TacticalView this input was built with and outlive the input. Without it the view is whatever the device layer fills in (a test's PinholeView).
	void attachCamera(TacticalCamera &camera);
	TacticalCamera *camera() { return m_camera; }
	LookAtTranslator *lookAt() { return m_lookAt.get(); }
	// One client frame of the camera (30 a second, RW 0x64849E: the LookAt tick, then the view's update). `nowMs` is the clock of the edge scroll ramp.
	void cameraFrame(unsigned nowMs, bool gamePaused = false);

	// Runs the translators over the queued events. Returns the number of logic messages that were appended to the command list.
	size_t update();
	// lane OWNERFIX-1: a logic message the HUD itself makes (the spell book bar, the spell store: GameClient/SpellBookUI.h) enters the stream like every translator's
	// message and so passes the CommandTranslator's input guard (RW 0x81F8F2 .. 0x81F979); flush() runs the translators over the stream and appends what reaches its
	// end to the command list (what update() does after its own bookkeeping). Retail's controls (ControlBar::processCommandUI) always append to TheMessageStream
	void submit(const GameMessage &message);
	size_t flush();
	// lane OWNERFIX-1: the map scripts' DISABLE_INPUT / ENABLE_INPUT (RW 0x7BC586 / 0x7BC5E9): InGameUI::applyScriptInput, then (disabling) the LookAt translator's
	// flags (RW 0x83AB5D). InGameHud::scriptInput adds the control bar's help provider (RW 0x8076CD)
	// `controlBarReset` is what RW 0x8076CD does (the control bar's help provider: InGameHud), run before the LookAt flags as in RW 0x7BC586
	void scriptInput(bool enable, const std::function<void()> &controlBarReset = std::function<void()>());
	// the steps of the last scriptInput in the order they ran (the tests pin retail's order)
	const std::vector<std::string> &lastScriptSteps() const { return m_scriptSteps; }
	void noteScriptStep(const char *step) { m_scriptSteps.push_back(step); } // a step a caller's reset ran
	// lane BUILD-5 r2: the placement update's ghost step (PlaceEventTranslator::handleBuildPlacements, RW 0x6A2AE5) belongs to the client's draw, after the
	// camera moved: with `deferred` set update() leaves it out and the device calls placementUpdate() once the displayed camera was applied (the ghost then
	// stands under the pointer of the frame drawn, also while the view scrolls); unset (tests without a device) update() runs it after the stream
	void setPlacementDeferred(bool deferred) { m_placementDeferred = deferred; }
	void placementUpdate(bool judge = true); // lane BUILD-5 r3: judge false on the device's busy-worker frames (PlaceEventTranslator::handleBuildPlacements)
	// AUDIO-2: called with every logic message as it goes to the command list, after the translators updated the client selection (the unit voice of ZH
	// CommandXlat / SelectionXlat pickAndPlayUnitVoiceResponse is played from here; client presentation only)
	void setLogicMessageObserver(std::function<void(const GameMessage &)> observer) { m_observer = std::move(observer); }

	InGameUI &ui() { return m_ui; }
	const InGameUI &ui() const { return m_ui; }
	HudContext &context() { return m_ctx; }
	CommandTranslator &commandTranslator() { return m_command; }
	GUICommandTranslator &guiCommandTranslator() { return m_guiCommand; } // lane SPELLRC-1
	const MetaEventTranslator &metaTranslator() const { return m_meta; }
	MetaEventTranslator &metaTranslator() { return m_meta; }
	PlaceEventTranslator &placeTranslator() { return m_place; }
	FormationPlacement &formationPlacement() { return m_formation; }
	SelectionTranslator &selectionTranslator() { return m_selection; }
	HotKeyTranslator &hotKeyTranslator() { return m_hotKey; }
	MessageStream &stream() { return m_stream; }
	// the logic messages that went to the command list so far (as text): two runs of the same input produce the same list
	const std::vector<std::string> &messageLog() const { return m_stream.log(); }
	unsigned long long sentMessages() const { return m_sent; }

	// The acceptance stops of the HUD (docs/STOPS.md S-280 .. S-299): one "[S-2xx] ..." line each; stops() adds what this run met (counted unported meta commands).
	static std::vector<std::string> acceptanceStops();
	std::vector<std::string> stops() const;

private:
	void hintSpy(const ClientMessage &m); // lane PLAY-1: the move hints (ZH HintSpyTranslator)
	GameLogic &m_logic;
	CommandList &m_commands;
	InGameUI m_ui;
	MessageStream m_stream;
	HudContext m_ctx;
	MetaEventTranslator m_meta;
	FormationPlacement m_formation;
	PlaceEventTranslator m_place; // BUILD-1
	GUICommandTranslator m_guiCommand;
	CommandTranslator m_command;
	SelectionTranslator m_selection;
	HotKeyTranslator m_hotKey; // lane INPUT-1
	TacticalCamera *m_camera = nullptr;
	std::unique_ptr<LookAtTranslator> m_lookAt;
	bool m_keyDown[256] = {}; // lane BACKLOG3
	bool m_ignoreLeftUp = false, m_ignoreRightUp = false, m_ignoreMiddleUp = false;
	unsigned long long m_sent = 0;
	std::function<void(const GameMessage &)> m_observer;
	std::vector<std::string> m_scriptSteps; ///< lane OWNERFIX-1 r2
	bool m_placementDeferred = false; // lane BUILD-5 r2
	unsigned m_fogDeselectFrame = 0;  // lane VISUAL-2 r2: the client frame RW 0x6A2449's fogged deselection last ran in
};
