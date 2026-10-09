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
#include "GameClient/MessageStream/CommandXlat.h"
#include "GameClient/MessageStream/GUICommandTranslator.h"
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
	void key(int key, int keyState);
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
	// AUDIO-2: called with every logic message as it goes to the command list, after the translators updated the client selection (the unit voice of ZH
	// CommandXlat / SelectionXlat pickAndPlayUnitVoiceResponse is played from here; client presentation only)
	void setLogicMessageObserver(std::function<void(const GameMessage &)> observer) { m_observer = std::move(observer); }

	InGameUI &ui() { return m_ui; }
	const InGameUI &ui() const { return m_ui; }
	HudContext &context() { return m_ctx; }
	CommandTranslator &commandTranslator() { return m_command; }
	PlaceEventTranslator &placeTranslator() { return m_place; }
	SelectionTranslator &selectionTranslator() { return m_selection; }
	MessageStream &stream() { return m_stream; }
	// the logic messages that went to the command list so far (as text): two runs of the same input produce the same list
	const std::vector<std::string> &messageLog() const { return m_stream.log(); }
	unsigned long long sentMessages() const { return m_sent; }

	// The acceptance stops of the HUD (docs/STOPS.md S-280 .. S-299): one "[S-2xx] ..." line each; stops() adds what this run met (counted unported meta commands).
	static std::vector<std::string> acceptanceStops();
	std::vector<std::string> stops() const;

private:
	GameLogic &m_logic;
	CommandList &m_commands;
	InGameUI m_ui;
	MessageStream m_stream;
	HudContext m_ctx;
	MetaEventTranslator m_meta;
	PlaceEventTranslator m_place; // BUILD-1
	GUICommandTranslator m_guiCommand;
	CommandTranslator m_command;
	SelectionTranslator m_selection;
	TacticalCamera *m_camera = nullptr;
	std::unique_ptr<LookAtTranslator> m_lookAt;
	bool m_ignoreLeftUp = false, m_ignoreRightUp = false, m_ignoreMiddleUp = false;
	unsigned long long m_sent = 0;
	std::function<void(const GameMessage &)> m_observer;
};
