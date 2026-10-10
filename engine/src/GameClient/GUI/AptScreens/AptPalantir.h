// OpenBFME. GPL-3.0.
//
// AptPalantir: the engine side of Palantir.apt, the in-game HUD movie (lane HUD-1). CodePrefix "AptPalantir"; spec menus-apt.md 1.4, 3.4 row "AptPalantir".
//
// TARGET FACTS (RotWK game.dat, caveat S-001): the Palantir registers (strings at RW 0xC8C...): AptPalantir::RenderGlobe, RenderMovie, RenderRadarViewBox, RenderRadar, ClipRadar
// (render callbacks: the clips tagged `_type` = that name), OnPlanningModeUIUnloaded / Loaded, OnHeroSelectUnloaded / Loaded, OnHelpBoxUnloaded / Loaded, OnBttnMessenger,
// OnBttnObservePriorPlayer, OnBttnObserveNextPlayer, OnBttnMovie, OnBttnObjectives, OnBttnOptions, OnBttnSpellStore, OnClosed, OnInitialized; the extern PalantirMinLOD; the
// command UI's PalantirCommandUI::OnButtonFrameLoaded / Unloaded, OnSubMenuLoaded / Unloaded, OnToggleFlashLoaded / Unloaded and the side bar's OnAptInGameSideCommandBar{Loaded, Unloaded,
// FadeInComplete, FadeOutComplete, ButtonFrameLoaded, ButtonFrameUnloaded} and OnAptInGameSpellBook{Loaded, Unloaded, Shown, ButtonPressed}. The engine calls these functions of the
// movie (the movie's own definitions, read from Palantir.apt): InitGlobeUIs, ShowCommandInterface, HideCommandInterface, SetPalantirFrameState, SetPlayerPowerCapState,
// SetResourceIconState, SetPlayerFaction, SetMovieButtonState, FadePalantirButtons, SetObserverStuffState, SetPlayerButtonsState, FlashObjectivesButton, CreateRadarPing, MoveRadarPing,
// FadeOutRadarPing, ShowRankInterface, SetRankProgressBar ... and writes the text records APT:PalantirResources, APT:PalantirCommandPoints, APT:PalantirResourceMultiplier, APT:PlayerPowerCap,
// APT:PlayerRank, APT:HeroRank.
// DONOR (BFME1 AptPalantir*.cpp): the callbacks are registered once, the screen loads Palantir.apt into a window slot of the manager (WindowManager::setupPalantir), RenderRadar draws the
// radar through the engine's Radar into the clip's rectangle, OnInitialized brightens the jewel.
// INFERENCE (stop S-289): what each command of the movie asks of the engine beyond its name (the handler bodies of RotWK were not read); the protocol of the command button frames is derived from
// the movie's scripts (libInGameUI.apt MovieClipFrame, InGameSideCommandBar.apt) and the binary's strings, not from the engine's functions.

#pragma once

#include "GameClient/ControlBar.h"
#include "GameClient/GUI/AptScreen.h"

#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

class AptPalantir : public AptScreen
{
public:
	AptPalantir(WindowManager &windows, Shell &shell);

	// the commands the Palantir registers, in the binary's order (without the screen prefix of the command UI names)
	static const std::vector<std::string> &retailCommandNames();

	bool initialized() const { return m_initialized; }
	int initializedCount() const { return commandCalls("AptPalantir::OnInitialized"); }
	// the paths the movie reported through the button frame commands (index -> clip path), in report order
	struct ButtonFrame
	{
		std::string index; ///< the frame's `_name` ("Button3")
		std::string path;  ///< the clip's target path as the movie reported it
	};
	const std::vector<ButtonFrame> &buttonFrames() const { return m_buttonFrames; }
	// names of the movies the Palantir loaded into its clips (OnHelpBoxLoaded ...), by command name
	const std::map<std::string, int> &loadedCounts() const { return m_loaded; }
	// every command the movie ran with its argument, in order (tests, reports)
	const std::vector<std::string> &commandLog() const { return m_log; }

	// ---- the engine's side of the movie (lane HUD-1) ----
	// What a press of a command button does: ControlBar::pressButton / cancelQueued (set by the HUD)
	void setButtonHandler(std::function<void(int slot, bool inPalantir)> press) { m_press = std::move(press); }
	// Pushes the control bar's state into the movie: the text records (money, command points), the command buttons of the Palantir arc (six positions) and of the side command bar
	// (twelve): content, state, production count, timer, image. Idempotent: only what changed since the last call is sent.
	void sync(const ControlBar &bar);
	// the tables the movie's native components read: RenderImage clips carry `_imageMap` = "<clip path>_Image", TimerOverlay clips `_timerId` = "<clip path>_Timer"
	struct NativeImage
	{
		std::string image;     ///< a MappedImage name ("" none)
		bool grayscale = false; ///< `_mode` GRAYSCALE: a disabled button
	};
	const NativeImage *imageFor(const std::string &key) const;
	float timerFor(const std::string &key) const; // 0..1, -1 for no timer
	// the paths of the content clips by key, for the report
	const std::map<std::string, NativeImage> &images() const { return m_images; }
	// the local player's side (good / evil): the frame, the faction icon and the radar of the movie follow it (SetPalantirFrameState, SetPlayerFaction)
	void setEvil(bool evil) { m_evil = evil; }
	// lane HUD-2: what the engine's Palantir update reads besides the control bar (AptPalantir.cpp syncLocal cites the binary): the local player's faction name
	// (SetPlayerFaction), its template's Evil flag (SetPlayerButtonsState) and the control bar's context with the portrait of the selection
	// (PalantirCommandUI: ShowCommandInterface / HideCommandInterface, the image of the clip CommandUI.Portrait)
	struct LocalState
	{
		std::string faction;     ///< the local player's side name ("" none)
		bool evil = false;       ///< the local player's template Evil
		bool context = false;    ///< the control bar has switched to a context (retail: from its first update on)
		ObjectID contextObject = INVALID_ID;
		std::string commandSet;  ///< the context's CommandSet name
		std::string portrait;    ///< the mapped image of the selection's portrait ("" none)
		// lane HUD-5: Palantir::Impl::UpdatePlayerStats (RW 0x6D5C0F) reads the local player's science state (Player + 0x14 .. + 0x2C = PlayerScience + 0x0C ..
		// + 0x24) and ControlBar RW 0x71FA12 for the 20 buttons of the player's science purchase command set
		bool havePlayer = false;
		float skillPoints = 0.0f;   ///< Player + 0x14 (PlayerScience + 0x0C)
		int rankLevel = 1;          ///< Player + 0x1C (+ 0x14)
		int purchasePoints = 0;     ///< Player + 0x24 (+ 0x1C): APT:PlayerRank shows it
		int skillPointsNext = 0;    ///< Player + 0x28 (+ 0x20)
		int skillPointsThis = 0;    ///< Player + 0x2C (+ 0x24)
		bool purchasable[20] = {};  ///< RW 0x71FA12(player, i): the store's button i could be bought now
		bool storeOpen = false;     ///< RW 0x822A35: the powers screen (SpellStore.apt) is up
		// lane HUD-5: the rank interface of the selection (PalantirCommandUI::rankInfoFor, RW 0x9305CE) and the game texts it shows (UTF-8): APT:RankLabel's format
		// ("%d" of the rank, RW 0x92FACB) and APT:PalantirTimeRemaining (RW 0x93079B)
		int rankType = 2;
		int rank = 0;
		float rankProgress = 0.0f;
		std::string rankLabelFormat, timeRemainingText;
	};
	void setLocalState(const LocalState &s) { m_local = s; }
	// lane HUD-5: the movie calls UpdatePlayerStats made ("SetPlayerMagicProgress(37)", ...), the last 64, for tests and reports
	const std::vector<std::string> &playerStatsCalls() const { return m_statsCalls; }
	bool commandInterfaceShown() const { return m_commandShown; }
	// lane HUD-5: the rank interface (RW 0x9305CE): shown, the APT:HeroRank text set, the bar's last value (-1 hidden), and the movie calls made (the last 64)
	bool rankInterfaceShown() const { return m_rankShown; }
	float rankProgressShown() const { return m_rankProgress; }
	const std::vector<std::string> &rankCalls() const { return m_rankCalls; }
	const std::string &portraitShown() const { return m_portraitShown; }
	// the key under which the movie's RenderImage clip at `clipPath` finds its image: its `_imageMap` variable, else the clip's own path (retail keys the portrait
	// by the clip path "CommandUI/Portrait", RW 0x9308F3)
	const NativeImage *imageForClip(const std::string &clipPath, const std::string &imageMapVar) const { return imageFor(imageMapVar.empty() ? clipPath : imageMapVar); }
	std::string portraitKey() const { return levelPrefix() + "CommandUI.Portrait"; }
	static std::vector<std::string> acceptanceStops();
	static constexpr int kArcPositions = 6, kSidePositions = 12;

	// ---- lane SPELL-2: the spell book inside the Palantir (InGameSpellBook.apt; RotWK controller RW 0x93178C) ----
	// TARGET FACTS (RotWK game.dat, caveat S-001): the controller registers OnAptInGameSpellBookLoaded (RW 0x931698: the movie's clip path, e.g. "SpellBookUI",
	// is kept), OnAptInGameSpellBookUnloaded, OnAptInGameSpellBookShown (RW 0x9310B5 -> 0x930F0F: the slot cache reset, shown) and
	// OnAptInGameSpellBookButtonPressed (RW 0x930DE5: atoi(argument) in [0, 24), the slot's command button through ControlBar::processCommandUI RW 0x940435),
	// and 24 timer keys "InGameSpellBookSpell%dTimer" (RW 0x93191F); the update (RW 0x9312B9): before Shown, SetState("_show") on the clip; after it, the
	// visible buttons of the spell book's command set take the slots in order: the image key "InGameSpellBookSpell%dImage" (RW 0x930E6E) gets the button's
	// image (RW 0x6236DE; cleared with RW 0x623790), SetButtonState(slot + 1, state) is called when the state changed (RW 0x93125F, names RW 0xC7F4DC:
	// _unused, _disabled, _cantAfford, _static, _notReady, _up, _visuallyEnabled), FlashButton(slot + 1) when a button becomes usable (_up / _static), and the
	// slots past the last button are _unused without an image.
	enum SpellButtonState
	{
		SPELL_UNUSED = 0,
		SPELL_DISABLED = 1,
		SPELL_CANT_AFFORD = 2,
		SPELL_STATIC = 3,
		SPELL_NOT_READY = 4,
		SPELL_UP = 5,
		SPELL_VISUALLY_ENABLED = 6
	};
	static constexpr int kSpellSlots = 24;
	struct SpellSlot
	{
		std::string image;    ///< the button's MappedImage
		int state = SPELL_UNUSED;
		float timer = -1.0f;  ///< 0..1 while recharging, -1 none
		int buttonIndex = -1; ///< the command set slot the press goes to
	};
	void setSpellPressHandler(std::function<void(int buttonIndex)> press) { m_spellPress = std::move(press); }
	void setSpellStoreHandler(std::function<void()> open) { m_spellStoreOpen = std::move(open); }
	// lane END-2: OnBttnOptions toggles the quit menu (RW 0x6D40C1 -> 0x9220BD): the request goes to the shell's services
	void setServices(class ShellServices *services) { m_services = services; }
	// lane INPUT-1: the DIPLOMACY key (Tab, RW 0x81FFAA) asks for what the flag above the radar opens (RW 0x625456 -> 0x914EF0 / 0x8E8843);
	// false when no shell services are wired
	bool requestObjectives();
	// lane RADAR-1: the radar's pings (Palantir::Impl::RadarPing, RW 0x6D5A9D / 0x6D5B23 / 0x6D5BD9): CreateRadarPing(id, name), MoveRadarPing(id, x, y) in stage
	// units, FadeOutRadarPing(id), called on the movie's root with the integer as "%d" and the floats as "%f" (RW 0x6D5445 / 0x6D54BA / 0x6D5587)
	bool createRadarPing(int id, const std::string &name);
	bool moveRadarPing(int id, float stageX, float stageY);
	bool fadeOutRadarPing(int id);
	unsigned radarPingCalls() const { return m_radarPingCalls; }
	void syncSpellBook(const std::vector<SpellSlot> &slots); ///< RW 0x9312B9
	bool pressSpellSlot(int slot); ///< RW 0x930DE5 (OnAptInGameSpellBookButtonPressed)
	const std::string &spellBookPath() const { return m_spellPath; }
	bool spellBookShown() const { return m_spellShown; }
	int spellSlotState(int slot) const { return slot >= 0 && slot < kSpellSlots ? m_spellState[slot] : SPELL_UNUSED; }
	static const char *spellStateName(int state);
	// the clip state a command button of `state` with the CommandButton `options` shows (SetState); lane HUD-5: RW 0x9D2BEE (an active button is _up, a
	// NONPRESSABLE one _static)
	static const char *commandStateName(ButtonState state, std::uint32_t options);
	unsigned long long moviesCalls() const { return m_calls; }
	const std::vector<std::string> &callErrors() const { return m_callErrors; }

private:
	// RW 0x9D26D2: CreateContent with the clip's "_ContentName" extern provider registered for the call (lane QA-1)
	bool createContent(const std::string &frame);
	void hook(const std::string &name);
	struct Slot
	{
		bool created = false;
		bool initialized = false; ///< lane HUD-4: the content reported <content>_OnInitialized (RW 0x9D6E4D)
		int slot = -1;
		std::string sig;
	};
	std::set<std::string> m_pressRegistered; // the press callbacks registered, once per position
	std::set<std::string> m_initRegistered;  // lane HUD-4: the _OnInitialized callbacks, once per position
	bool call(const std::string &path, const std::string &fn, const std::vector<std::string> &args);
	unsigned m_radarPingCalls = 0; ///< lane RADAR-1
	void syncFrames(const std::vector<ControlBarButton> &buttons, bool arc);
	void syncLocal();
	void syncPlayerStats(); // lane HUD-5: RW 0x6D5C0F
	std::string levelPrefix() const;

	std::function<void(int, bool)> m_press;
	std::function<void(int)> m_spellPress;     // lane SPELL-2
	std::function<void()> m_spellStoreOpen;    // lane SPELL-2: AptPalantir::OnBttnSpellStore
	class ShellServices *m_services = nullptr; // lane END-2
	std::string m_spellPath;
	bool m_spellShown = false;
	int m_spellState[kSpellSlots] = {};
	int m_spellButton[kSpellSlots] = {};
	std::string m_spellImage[kSpellSlots];
	std::map<std::string, NativeImage> m_images;
	std::map<std::string, float> m_timers;
	Slot m_arc[kArcPositions], m_side[kSidePositions];
	bool m_arcShown = false, m_sideShown = false, m_started = false, m_evil = false;
	LocalState m_local;
	void syncRank();     // lane HUD-5: RW 0x9305CE
	void hideRank();     // RW 0x92F636
	void rankCall(const std::string &fn, const std::vector<std::string> &args);
	bool m_rankShown = false, m_rankSet = false; ///< + 4 / + 0xC of the rank interface (PalantirCommandUI + 0x38)
	int m_rankType = 2, m_rankValue = 0;          ///< + 8 / + 0x10
	float m_rankProgress = -1.0f;                 ///< + 0x14
	std::vector<std::string> m_rankCalls;
	bool m_buttonsStateSent = false, m_buttonsStateEvil = false, m_magicEnabledSent = false, m_commandContext = false, m_commandShown = false;
	// lane HUD-5: UpdatePlayerStats' cache (Palantir::Impl + 0x7E bits 0 / 4, + 0x80 / + 0x84 / + 0x88, + 0xD0 .. + 0xE4; reset RW 0x6D76D3: -1, 1, cleared)
	bool m_rankSent = false, m_magicHighlightedSent = false, m_highlighted = false;
	int m_lastLevel = -1, m_lastRank = 0, m_lastProgress = 1;
	bool m_previousPurchasable[20] = {};
	std::vector<std::string> m_statsCalls;
	void statsCall(const std::string &fn, const std::vector<std::string> &args);
	ObjectID m_contextObject = INVALID_ID;
	std::string m_contextSet, m_factionSent, m_portraitShown;
	std::string m_lastTexts;
	unsigned long long m_calls = 0;
	std::vector<std::string> m_callErrors;
	std::set<std::string> m_overflowReported; ///< lane QA-1: the buttons beyond the positions, reported once each
	// lane HUD-4: the button frames the movie reported (clip path without the level prefix, "" none) and the side bar movie's presence (RW 0x930088 / 0x92EE8B)
	std::string m_arcFramePath[kArcPositions];
	std::string m_sideFramePath[16];
	bool m_sideBarLoaded = false;
	void frameLoaded(bool arc, const std::string &arg, bool loaded);

	bool m_initialized = false;
	std::vector<ButtonFrame> m_buttonFrames;
	std::map<std::string, int> m_loaded;
	std::vector<std::string> m_log;
};
