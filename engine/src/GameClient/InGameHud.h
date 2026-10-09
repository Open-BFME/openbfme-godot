// OpenBFME. GPL-3.0.
//
// InGameHud (lane HUD-1): the in-game interface of one live game and its local player, as ONE component the live game installs (lane START-1 owns the game scene and
// the start flow; this class is what it creates after the loading screen). It owns
//   * the input stack (HudInput: raw input -> translators -> lockstep command list),
//   * the control bar model (ControlBar: the selection's CommandSet, the button states, the production queue, money and command points),
//   * the Palantir (Palantir.apt in a WindowManager: AptPalantir with its callbacks, the command button frames, the text records and the render components),
//   * the radar (Radar: the map picture, the object blips, the view box, clicking it),
// and exposes what the device layer needs: raw input entry points in window pixels, update(seconds), the Apt player to draw, and the list of native draws (the
// clips of the movie whose content the engine renders: the radar, the command button images, timer overlays, the portrait).
//
// THE LOCKSTEP RULE: nothing in here changes the simulation. Every player action ends as a GameMessage appended to the CommandList of the live game
// (GameLogicDispatch); the HUD reads the logic (selection, production queue, money) and the client views.

#pragma once

#include "GameClient/GUI/AptScreens/AptPalantir.h"
#include "GameClient/GUI/AptScreens/AptSpellStore.h"
#include "GameClient/SpellBookUI.h"
#include "GameClient/GUI/Shell/Shell.h"
#include "GameClient/GUI/ShellEnvironment.h"
#include "GameClient/GUI/ShellServices.h"
#include "GameClient/GUI/WindowManager.h"
#include "GameClient/ControlBar.h"
#include "GameClient/HudInput.h"
#include "GameClient/Radar.h"
#include "GameClient/UnitVoiceResponse.h"
#include "GameLogic/Object/RetailObjectWorld.h"
#include "GameLogic/GameLogicDispatch.h"
#include "Libraries/Source/Apt/AptLoad.h"

#include <memory>
#include <string>
#include <vector>

class ArchiveFileSystem;
class GameTextSource;
class LiveGame;
class RetailObjectWorld;

class InGameHud
{
public:
	struct Config
	{
		LiveGame &game;
		RetailObjectWorld &world;      ///< the retail templates and the command store
		ArchiveFileSystem &fs;
		ShellServices &services;       ///< sounds, cursor tooltip (the device implements them)
		TacticalView &view;
		MouseSettings mouse;
		const MetaMap &metaMap;
		GameTextSource *gameText = nullptr;
		// The shell-hosted form (lane START-1's game scene): the window manager and shell that already run the game's movies, with Palantir.apt pushed through the screen factory
		// table (AptScreenFactories: "Palantir.apt" -> AptPalantir). The HUD then finds that screen instead of making its own, and the owner steps the window manager.
		WindowManager *windows = nullptr;
		Shell *shell = nullptr;
	};
	explicit InGameHud(Config config);
	// the retail world's context (its command store, locomotor / weapon stores, audio validator) for a scope: every entry point below holds one, because the control bar and the
	// translators read the process-wide stores (TheCommandStore in BuildAssistant::canMakeUnit) and other live worlds may be the top owner; a caller that reaches into
	// controlBar() / input() directly holds its own (lane BUILD-1: the order-dependent failure of the Palantir press test)
	// SMOOTH-1 (S-810): every entry point is also where the HUD takes the live game: it waits until the logic worker finished its frame (LiveGame::waitIdle)
	std::unique_ptr<RetailObjectWorld::ContextScope> enterContext();
	~InGameHud();
	InGameHud(const InGameHud &) = delete;
	InGameHud &operator=(const InGameHud &) = delete;

	// Loads the HUD's resources and Palantir.apt; false + *error when something fails (no silent default).
	bool boot(std::string *error);

	// seconds of render time: the Apt player steps, the control bar and the radar refresh
	void update(double seconds);

	// the window size in pixels the stage (1024 x 768) is stretched to
	void setWindowSize(int width, int height);

	// ---- raw input in window pixels; the HUD decides whether the pointer is over its own movie (then the movie gets the event) or over the world ----
	void mouseMove(int x, int y, int keyState = 0);
	void mouseButton(HudInput::Button button, bool down, int x, int y, int keyState, int timeMs, bool doubleClick = false);
	// the wheel at a window pixel: over the Palantir it belongs to the movie, over the world it zooms the tactical camera (the LookAt translator)
	void mouseWheel(int delta, int x, int y);
	void key(int keyCode, int keyState);
	// true when the pointer is over the Palantir (a button of the movie, the radar or the frame): the world does not get that click
	bool isOverGui(int x, int y);
	// the radar picture's square in window pixels (the largest square inside the movie's RenderRadar clip, centred); false before the movie shows it
	bool radarSquare(float out[4]);
	// a click at a window pixel inside the radar square: the left button moves the view there, the right button orders the selection to that ground point (MSG_DO_MOVETO) when it is
	// controllable. True when the click was the radar's.
	bool radarClick(HudInput::Button button, int x, int y, int timeMs);

	WindowManager &windows() { return *m_wm; }
	Apt &apt() { return m_wm->apt(); }
	AptPalantir *palantir() { return m_palantirRef; }
	HudInput &input() { return *m_input; }
	ControlBar &controlBar() { return *m_bar; }
	Radar &radar() { return *m_radar; }
	// AUDIO-2: the unit voices of the local player's selection and commands (through the installed audio manager and Eva)
	UnitVoiceResponse &voice() { return *m_voice; }
	const std::vector<std::string> &errors() const { return m_errors; }
	std::vector<std::string> stops() const;

	// ---- lane SPELL-2: the spell book (the Palantir's InGameSpellBook and the SpellStore.apt screen) ----
	// the cast bar's press (the movie's OnAptInGameSpellBookButtonPressed, by command set slot): a no-target power casts, a NEED_TARGET_POS one starts the
	// targeting (the next left click on the ground casts there, a right click cancels)
	bool pressSpell(int buttonIndex);
	const InGameSpellBookModel &spellBar() const { return m_spellBar; }
	// the store (AptPalantir::OnBttnSpellStore): SpellStore.apt in a window slot over the Palantir, bound to the local player; closing sends the purchases
	bool openSpellStore(std::string *error = nullptr);
	void closeSpellStore();
	AptSpellStore *spellStore() { return m_store.get(); }
	// the native image of a RenderImage clip of the store ("" none), for the device's draw hook
	std::string storeImageForClip(const std::string &clipPath) const { return m_store ? m_store->imageForClip(clipPath) : std::string(); }
	// SMOOTH-1: render frames whose update was skipped because the logic worker was busy
	unsigned long long skippedUpdates() const { return m_skippedUpdates; }

private:
	unsigned long long m_skippedUpdates = 0;
	Config m_config;
	AptArchiveFileSource m_source;
	ShellEnvironment m_environment;
	AptScreenFactoryTable m_factories;
	std::unique_ptr<WindowManager> m_ownedWindows;
	std::unique_ptr<Shell> m_ownedShell;
	WindowManager *m_wm = nullptr;
	Shell *m_shellRef = nullptr;
	std::unique_ptr<AptPalantir> m_ownedPalantir;
	AptPalantir *m_palantirRef = nullptr;
	std::unique_ptr<HudInput> m_input;
	std::unique_ptr<ControlBar> m_bar;
	std::unique_ptr<Radar> m_radar;
	unsigned m_clientFrames = 0; ///< lane HUD-4
	AudioApiVoiceSink m_voiceSink;
	std::unique_ptr<UnitVoiceResponse> m_voice;
	std::vector<std::string> m_errors;
	InGameSpellBookModel m_spellBar;          // lane SPELL-2
	std::unique_ptr<AptSpellStore> m_store;   // lane SPELL-2
	void syncSpellBook();
	void send(const std::vector<GameMessage> &msgs);
	int m_windowW = 1024, m_windowH = 768;
	bool m_booted = false;
};
