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
#include "GameClient/ControlBarRadialMenu.h"
#include "GameClient/CommandButtonHelp.h"
#include "GameClient/InGameHelpBox.h"
#include "GameClient/GUI/GameWindowManager.h"
#include "GameClient/GUI/Image.h"
#include "GameClient/DrawableIconUI.h"
#include "GameClient/HudInput.h"
#include "GameClient/InGameHeroSelect.h"
#include "GameClient/SelectionDecals.h"
#include "GameClient/Radar.h"
#include "GameClient/UnitVoiceResponse.h"
#include "GameLogic/Object/RetailObjectWorld.h"
#include "GameLogic/GameLogicDispatch.h"
#include "Libraries/Source/Apt/AptLoad.h"

#include <memory>
#include <set>
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
	void updateRadarEvents(); ///< lane RADAR-1
	int windowWidth() const { return m_windowW; } ///< lane RADAR-1: TheDisplay's width (the view box band, RW 0x503CDA)

	// ---- raw input in window pixels; the HUD decides whether the pointer is over its own movie (then the movie gets the event) or over the world ----
	void mouseMove(int x, int y, int keyState = 0);
	void mouseButton(HudInput::Button button, bool down, int x, int y, int keyState, int timeMs, bool doubleClick = false);
	// the wheel at a window pixel: over the Palantir it belongs to the movie, over the world it zooms the tactical camera (the LookAt translator)
	void mouseWheel(int delta, int x, int y);
	void key(int keyCode, int keyState, char32_t character = 0); // lane INPUT-1: the layout's character (the command button hotkeys)
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
	// lane UI-4: the selection markers of this frame (SelectionDecals.h); the device gives GameData's settings and draws them
	void setSelectionDecalSettings(const SelectionDecalSettings &s) { m_decalSettings = s; }
	const std::vector<SelectionDecal> &selectionDecals() const { return m_selectionDecals; }
	const SelectionDecalSettings &selectionDecalSettings() const { return m_decalSettings; }
	// lane UI-4: the Palantir's hero bar (InGameHeroSelect.h)
	InGameHeroSelect *heroSelect() { return m_heroSelect.get(); }
	// lane HUD-5: the drawable decorations (health bars, construction text, veterancy marks, GameClient/DrawableIconUI.h): the device gives the settings (GameData,
	// Options.ini, its mapped images, the camera's zoom) and draws the ops of the last update under the Palantir
	void setIconUISettings(const IconUISettings &s) { m_iconSettings = s; }
	const IconUISettings &iconSettings() const { return m_iconSettings; }
	const std::vector<IconUIOp> &iconOps() const { return m_iconOps; }
	// lane PLAY-3: the drag selection box of this render frame (W3DInGameUI::drawSelectionRegion, RW 0x48ECF4): an OPEN_RECT over the region the
	// SelectionTranslator's drag holds (InGameUI's area select hint), drawn while the drag lasts; false when there is none. Computed from the client
	// state on every call (no logic frame needed: the box follows the pointer even while the logic worker runs)
	bool selectionRegionOp(IconUIOp &out) const { return selectionRegionOp(m_input->ui(), out); }
	static bool selectionRegionOp(const InGameUI &ui, IconUIOp &out);
	const DrawableIconUI &iconUI() const { return m_iconUI; }
	// lane PLAY-1: the Palantir's powers button presses (OnBttnSpellStore) and why the last one opened no store ("" when it did); never silent
	unsigned spellStoreRequests() const { return m_storeRequests; }
	const std::string &spellStoreError() const { return m_storeError; }
	// the native image of a RenderImage clip of the store ("" none), for the device's draw hook
	std::string storeImageForClip(const std::string &clipPath) const { return m_store ? m_store->imageForClip(clipPath) : std::string(); }
	// SMOOTH-1: render frames whose update was skipped because the logic worker was busy
	unsigned long long skippedUpdates() const { return m_skippedUpdates; }

	// ---- lane HUD-6 (InGameHudHud6.cpp): the radial command bubbles, the help box ----
	ControlBarRadialMenu &radialMenu() { return m_radial; }
	InGameHelpBox &helpBox() { return m_helpBox; }
	const HelpBoxSettings &helpBoxSettings() const { return m_helpSettings; }
	// the device's mapped images (the overlays' and icons' sizes) and font metrics (the help's text); without metrics the help measures with the
	// headless stand-in (S-176)
	void setMappedImages(const MappedImageCollection *images) { m_hud6Images = images; }
	void setFontMetrics(FontMetricsSource *metrics) { m_hud6Metrics = metrics; }
	// the device draws the help box's content clip (its `_type` is helpBox().renderName()) at (x, y, w, h) window pixels: RW 0x92E2AC
	void helpBoxRender(float x, float y, float w, float h, std::vector<HelpDrawOp> &out);
	int tooltipDelayMs() const { return m_tooltipDelayMs; }

private:
	void bootHud6();
	void updateHud6(ObjectID context, const std::string &faction, double seconds);
	bool hud6OverGui(int x, int y) const;
	void hud6MouseMove(int x, int y);
	bool hud6MouseButton(HudInput::Button button, bool down, int x, int y);
	void hud6Stops(std::vector<std::string> &out) const;
	bool hoveredAptButton(InGameHelpBox::Provider &p, ControlBarButton &b);
	ControlBarRadialMenu m_radial;
	InGameHelpBox m_helpBox;
	HelpBoxSettings m_helpSettings;
	const MappedImageCollection *m_hud6Images = nullptr;
	FontMetricsSource *m_hud6Metrics = nullptr;
	DefaultFontMetrics m_hud6DefaultMetrics;
	std::uint32_t m_hud6ClockMs = 0;
	int m_tooltipDelayMs = 0;
	std::vector<std::string> m_hud6Errors;
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
	std::unique_ptr<InGameHeroSelect> m_heroSelect; // lane UI-4: the Palantir's hero bar
	unsigned m_heroTrackFrame = ~0u;               // lane UI-4: the logic frame the hero bar's object list was taken at
	bool m_heroHotkeysSelectAll = false;           // lane UI-4: the select-all key is registered
	std::set<std::string> m_heroCommands;          // lane UI-4: the hero bar command prefixes registered
	SelectionDecalSettings m_decalSettings;        // lane UI-4
	std::vector<SelectionDecal> m_selectionDecals; // lane UI-4
	DrawableIconUI m_iconUI;                  // lane HUD-5
	IconUISettings m_iconSettings;
	std::vector<IconUIOp> m_iconOps;
	unsigned m_spellToggleSeen = 0;           // lane INPUT-1: the SPELL_STORE metas done
	unsigned m_diplomacySeen = 0;             // lane INPUT-1: the DIPLOMACY metas done
	unsigned m_hotkeyVersion = ~0u;           // lane INPUT-1: the control bar version the hotkeys were registered for
	void registerHotkeys();
	unsigned m_storeRequests = 0;             // lane PLAY-1
	std::string m_storeError;
	void syncSpellBook();
	void send(const std::vector<GameMessage> &msgs);
	int m_windowW = 1024, m_windowH = 768;
	bool m_booted = false;
};
