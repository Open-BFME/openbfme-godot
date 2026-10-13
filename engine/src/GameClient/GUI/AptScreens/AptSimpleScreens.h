// OpenBFME. GPL-3.0.
//
// The small screens of the A4 factory table: AptLevel0, Background, GuiFX, LoadScreen, Options (spec menus-apt.md 1.1, 1.3, 1.10, 3.4).
//
// Target facts (RotWK game.dat): AptLevel0.apt, Background.apt, GuiFX.apt, LoadScreen.apt and Options.apt are named in the
// binary; the names registered for them are the binary's strings: AptGuiFX::OnInitialized (0x0082F674) and the component ToolTipText
// (0x0082F668); AptOptions::{InitGadgets, EnterAdvancedSettings, RefreshNat, Cancel, Reset, Save, OnInitialized}
// (0x0087D5AC..0x0087DB0C); the loading screen's providers LoadingScreen::{ArmyName,TeamNumber,PlayerName,Rank}%d (0x00850964..),
// GameLoadingType (0x008508C8) and GameLoading:PlayerColor:%d (0x00850900).  There is no AptGameLoading::* string in the binary
// (spec 1.3: its OnInitialized may go unhandled, UNVERIFIED).
// Donor facts (BFME1): WindowManagerInit.cpp loads AptLevel0.apt into slot 0 itself; WindowManager_setupPalantir.cpp loads
// Background.apt into the first free slot; AptGuiFXRegisterCallbacks.cpp:104-135 loads GuiFX.apt into slot 11 and registers its
// callback and the ToolTipText render component; the BFME1 shell factory table (AptScreenFactories.cpp) holds 15 screens and does
// NOT hold GuiFX, Background or AptLevel0.  The lane brief lists them in the Shell factory table; they are registered there too and
// the difference is reported in the stop table (S-171).
// Not ported (stop S-175): every handler body of Options and the loading screen, and the provider values.

#pragma once

#include "GameClient/GUI/AptScreen.h"
#include "GameClient/GUI/AptScreens/OpenBFMEOptionsPage.h"
#include "Libraries/Source/Apt/AptValue.h"

#include <memory>
#include <string>
#include <vector>

struct ShellEnvironment;
class ShellServices;
class AptCharacterInst;
class AptTextInst;

#include <map>
#include <set>
#include <string>
#include <vector>

class OptionPreferences;

class GameTextSource;

class AptLevel0Screen : public AptScreen
{
public:
	AptLevel0Screen(WindowManager &windows, Shell &shell);
	void runShutdown(bool *immediate) override;
};

class AptBackgroundScreen : public AptScreen
{
public:
	AptBackgroundScreen(WindowManager &windows, Shell &shell);
};

class AptGuiFXScreen : public AptScreen
{
public:
	// lane SAVE-2: adoptLoaded = the GuiFX.apt window the manager already holds is used (RotWK has one GuiFX level for the end screen and TheMessageBox);
	// its owner registered the movie's commands, so an adopting screen registers none
	AptGuiFXScreen(WindowManager &windows, Shell &shell, bool adoptLoaded = false);
};

// lane PLAY-1: PlayerTribute.apt (CodePrefix AptPlayerTribute), the screen the Palantir's flag opens in a skirmish / multiplayer game (RW 0x914EF0 pushes it).
// TARGET FACTS (RotWK game.dat, caveat S-001): the screen registers its commands under the movie's own path (the movie sets gameCodePrefix = String(this)):
//   * "<path>_ReturnToGame" (RW 0x9175AD -> RW 0x914E89 -> RW 0x914C51: + 0x278 closing, TheShell + 0x54 = 1, RW 0x62215B: the shell pops the screen); the key
//     handler (RW 0x914E91) closes on Escape; "<path>_OnInitialized" (empty in retail, BFME2 decomp: the folded empty method 0x0047A69C);
//   * "<path>_TributeEnabled" (RW 0x914C08): "1" when the local player is active (RW 0x6AAC52), else "0";
//   * "<path>_OnPageLoaded" (RW 0x91741F, "name=<page>&type=StatusPage|TributePage" -> RW 0x9170AF) / "_OnPageUnloaded" (RW 0x9171E3) / "_OnPageSelected"
//     (RW 0x917258); the command parameters are "key=value" pairs joined by '&' (RW 0x81560E).
// lane HUD-5: the Status page (the owner's report: every cell showed its text record's name). TARGET FACTS (RotWK game.dat, caveat S-001; BFME2 decomp map tier A):
//   * a page on the screen's own level (AptUtils::LevelIndexFromTarget RW 0x8155D9) of type "StatusPage" is a row list page (RW 0x9166AC = BFME2 0x9103A3);
//     "_OnPageUnloaded" / "_OnPageSelected" drop / switch the page by its path.
//   * the row list page registers "_level<n>." + <path without "_level<n>."> (AptUtils::SkipLevelN RW 0x815563) + "_OnRowShown" / "_OnRowHidden" and the externs
//     "_NumOfPlayers" (the row count) / "_InSkirmish" (RW 0x914AA1); its rows are GUI/PlayerStatusInfo.h's.
//   * "_OnRowShown" (RW 0x915E00 = AptRowListCallbacks.cpp OnRowShown): "index" in [0, count] and "name" on the page's level: the row object (RW 0x915BF6) sets
//     the records "APT:_level%u.%s_field%d" (RW 0x915159, format RW 0xC7C3E4) of the row's path to the four texts and registers the extern
//     "_level%u.%s_color" (RW 0x915B4A, format RW 0xC7C430; RW 0x914A69 prints the colour); "_OnRowHidden" (RW 0x915269) drops the row object.
// lane PLAY-1 r2: the tribute page (RW 0x916E8F): "<page>_OnEnabledContentLoaded" (RW 0x916BF4, the content clip) makes the content (RW 0x916973):
//   "<content>_OnRowShown" (RW 0x9162F5) / "_OnRowHidden" (RW 0x9153F2) / "_Reset" (RW 0x915862, string RW 0xC7C4E4) / "_Send" (RW 0x914DD6, string RW 0xC7C4DC)
//   and the extern "<content>_NumOfPlayers"; its players (RW 0x914B54) are the local player then the active allies (at most 7); a row (RW 0x915F5D) registers
//   the gadget inits "_level%u.<row>_InitSlider" (RW 0x914B07: range 0 .. 99999, position = the amount) and "_InitTextEntry" (RW 0x91565C), writes field 0
//   (the name) and field 1 (RW 0x9155C2: "%d" of the amount plus the player's cash; the local row's amount is minus the sum of the others', RW 0x914D8D);
//   slider tracks (RW 0x9156DC) and edits (RW 0x91570C) set the amount, capped by the row's maximum, which RW 0x91578C keeps at what the local cash leaves;
//   Reset zeroes every row (RW 0x91554C); Send (RW 0x914DD6) sends MSG_GIVE_MONEY(local, row player, amount) for every active non-local row with an amount;
//   the TributePage's update (RW 0x9164EB): once NumMinutesBeforePlayersCanTransferMoney have passed (RW 0x626087) the page's SetState("_enabled") is called
//   once (before, the movie shows its "disabled at the start of the game" text); with the content up, RW 0x91578C and every row's refresh (RW 0x916225).
// INFERENCE: retail accepts a status row index == count (one past the rows; the slot then holds no player): the port refuses it and reports it.
// NOT PORTED / INFERENCE (stop S-1922): the opening's pause / input calls (RW 0x914FA2 .. 0x914FE2); a skirmish AI ally's extra money term (RW 0x6A950B /
// 0x6A9999 -> + 0xC -> + 0x14) in a tribute row's field 1; the text entry's 5-digit limit and its character filter (RW 0x81606D / 0x721FA9); when RW 0x91578C
// runs (here: after every change); the campaign's objectives screen.
class TributeSource;
struct TributePlayerRow;
class AptPlayerTribute : public AptScreen
{
public:
	AptPlayerTribute(WindowManager &windows, Shell &shell, ShellServices &services, ShellEnvironment &environment);
	~AptPlayerTribute() override;
	// the screen's own path prefix ("_level4"): its commands' names
	std::string pathPrefix() const { return "_level" + std::to_string(level()); }
	static const char *stopLine();
	// lane HUD-5: the Status page (null until the movie loaded it) and the rows shown
	bool statusPageLoaded() const { return m_status != nullptr; }
	int statusRowsShown() const;

	// RW 0x81560E: the value of `key` in an Apt command's "a=1&b=2" parameters; false when absent
	static bool commandParam(const std::string &params, const std::string &key, std::string &value);
	// RW 0x8155D9: the N of "_levelN..." (-1 when the path does not start so); RW 0x815563: the path after its "_levelN." / "_levelN/" prefix
	static int levelIndexFromTarget(const std::string &path);
	static std::string skipLevelN(const std::string &path);

	// lane PLAY-1 r2, tests / the report: the tribute page's rows (player index, amount, maximum, has slider) and the sends made
	struct RowState
	{
		int playerIndex = -1;
		bool local = false;
		std::uint32_t amount = 0, maximum = 99999;
		bool slider = false, entry = false;
	};
	std::vector<RowState> tributeRows() const;
	int sends() const { return m_sends; }
	// the slider position of a tribute row, as a player's drag would leave it (tests: the gadget's own message path)
	bool setRowAmountBySlider(int row, int position);

protected:
	WindowMsgHandledType gadgetMessage(GameWindow *from, std::uint32_t msg, WindowMsgData data1, WindowMsgData data2) override;

private:
	struct StatusPage
	{
		std::string path;                ///< the page's path without the level prefix
		std::vector<bool> rowShown;      ///< a row object exists (OnRowShown made it)
		std::vector<std::string> rowPath;
	};
	void pageLoaded(const std::string &params);
	void pageUnloaded(const std::string &name);
	void rowShown(const std::string &params);
	void rowHidden(const std::string &params);
	void dropStatusPage();
	std::string statusPrefix() const { return "_level" + std::to_string(level()) + "." + m_status->path; }

	// lane PLAY-1 r2: the tribute page
	struct TRow
	{
		int playerIndex = -1;
		std::string name; ///< UTF-8
		std::uint32_t cash = 0;
		std::uint32_t color = 0;
		bool local = false, active = true;
		std::string path;             ///< the row clip's path without "_levelN." ("" until the row is shown)
		std::uint32_t amount = 0;     ///< + 0x6C
		std::uint32_t maximum = 99999; ///< + 0x68
		GameWindow *slider = nullptr; ///< + 0x78
		GameWindow *entry = nullptr;  ///< + 0x7C
		long long shownValue = -1;    ///< + 0x70: the field 1 value last written
	};
	void contentLoaded(const std::string &argument);
	void tributeRowShown(const std::string &argument);
	void tributeRowHidden(const std::string &argument);
	void dropDeadGadgets(TRow &row);
	void setFieldUtf8(const std::string &row, int field, const std::string &text);
	void registerRowColor(const std::string &row, std::uint32_t color); // RW 0x915B4A: the extern "_level%u.<row>_color" ("%d")
	void refreshRow(TRow &row);
	void rebalance(); // RW 0x91578C
	void setAmount(TRow &row, std::uint32_t amount, bool moveSlider);
	void reset();     // RW 0x915862
	void send();      // RW 0x914DD6
	void update();    // the TributePage's update RW 0x9164EB (a window manager update listener)

	ShellServices &m_services;
	ShellEnvironment &m_env;
	std::unique_ptr<StatusPage> m_status;
	std::string m_statusName; ///< the full path the movie loaded it under (the page map's key)
	std::string m_content;           ///< the tribute content clip's path ("" until loaded)
	std::string m_tributePage;       ///< the TributePage clip's path without "_levelN." ("" until its _OnPageLoaded)
	bool m_enabledSet = false;       ///< + 0x20: SetState("_enabled") was called
	std::vector<TRow> m_rows;        ///< the tribute rows (RW 0x914B54's players)
	int m_sends = 0;
};

struct ShellEnvironment;
struct LoadScreenInfo;

// LoadScreen.apt (CodePrefix AptGameLoading): the match loading screen (lane START-1).  Retail behaviour (RotWK RW 0x81C5D3 .. 0x81D4xx, S-001 caveat; the data
// contract is documented in GUI/LoadScreenInfo.h): the cards' texts are Apt text records, the colour and the loading type are extern providers, the bar is the
// movie's SetBarTo(card, percent).  There is no AptGameLoading::* command in the binary: the movie's own OnInitialized goes unhandled and is reported.
class AptLoadScreen : public AptScreen
{
public:
	AptLoadScreen(WindowManager &windows, Shell &shell, ShellEnvironment &environment);
	// processProgress(slot, percent): SetBarTo(card, percent) on the movie (arguments as decimal strings); false when the movie is not loaded yet (the call is kept
	// and made by the next update) or the call failed (reported through the window manager's notes)
	bool setProgress(int card, int percent);
	// updateLoadProgress(percent): the local card's bar
	bool setLocalProgress(int percent);
	// every SetBarTo made so far as (card, percent), in order (tests, the report)
	const std::vector<std::pair<int, int>> &progressCalls() const { return m_calls; }
	// the cards populated: the texts were set
	int cardCount() const { return m_cards; }

private:
	void populate();
	void flush();
	bool callBar(int card, int percent);

	ShellEnvironment &m_env;
	std::vector<std::pair<int, int>> m_calls;
	std::vector<std::pair<int, int>> m_pending;
	int m_cards = 0;
	bool m_zeroed = false;
};

// lane UI-2: Options.apt's Save / Cancel close the screen as RotWK's do (RW 0x91FC9C / 0x91EC52 end in RW 0x91EA39 -> 0x62215B, the window manager's
// pop request), Save writes Options.ini (GameClient/OptionPreferences.h). Lane OPTS-2: UI-2's OpenBFME "Soft particles" box left this screen for the
// OpenBFME screen's Graphics+ tab (Options.ini "OpenBFMESoftParticles"): Options.apt shows retail's controls only [S-1484].
// Lane FB7-1 (FB-0007 r2): the retail controls. TARGET FACTS (RotWK game.dat, caveat S-001):
// - opening (RW 0x91ED91(a, b, c, d)): Options.apt is pushed, then + 0x283 = a (the extern NetworkEnabled), + 0x280 = + 0x282 = b (AllowResolutionChange),
//   + 0x281 = c (AllowAdvancedOptions), + 0x284 = d (AdvancedOnly). The main menu passes (0, 1, 1, advanced) (RW 0x91C37E .. 0x91C38A), the LAN lobby
//   (0, 0, 1, 0) (RW 0x846F58), the quit menu (0, 0, 0, 0) (RW 0x921783);
// - the constructor (RW 0x920F34) registers the externs MasterOption0Num, MasterOption0Current, MasterOption0ResetDefault, AllowResolutionChange,
//   AllowAdvancedOptions, NetworkEnabled, AdvancedOnly (table RW 0xDB6A18, one provider RW 0x91F7F5: "5"; the master option; the LOD manager's
//   + 0x17C4; then the four mode bytes as "1" / "0") and binds APT:VersionNum to Version::getUnicodeVersion (RW 0x6446A1: the game text Version:Format2
//   with the build's major and minor, 2 and 1: "VERSION=2.1.2614.37001" of the embedded build string, RW 0x644CC2);
// - InitGadgets (RW 0x9205C4) by gadget name: the five volume sliders take OptionPreferences' SFXVolume / VoiceVolume / MusicVolume / AmbientVolume /
//   MovieVolume (RW 0x6E5FB3: atof, at least 0; missing: the AudioSettings default * 100), Brightness its "Brightness" (atoi; missing: 50.0, RW
//   0x6E5ECD), ScrollSpeed "ScrollFactor" (atoi, < 0 -> 1, > 100 -> 100, * 0.02; missing: GameData KeyboardDefaultScrollSpeedFactor) * 50
//   (RW 0x920A0B), each truncated (ftol) and set with GSM_SET_SLIDER (RW 0x914A15); the check boxes AlternateMouseSetUp (checked when
//   AlternateMouseSetup is "yes"), SendDelay, DisplayForeignLanguage, TurnOffMessengerInGame, FilterLanguage (LanguageFilter), EAX3 (UseEAX3),
//   HighAudioQuality (AudioLOD == "High", RW 0x6025F1 table Low / High); OnlineIp lists the machine's addresses and selects GameSpyIPAddress's;
//   OnlinePortNum shows FirewallPortOverride ("%d", empty for 0) and takes 5 characters (RW 0x81606D); Resolution lists the display modes ("%dx%d")
//   and selects Options.ini's "Resolution"; Detail lists GUI:UltraHigh .. GUI:VeryLow, GUI:Custom (item data 4 .. 0, 5). Without NetworkEnabled
//   OnlineIp, OnlinePortNum, SendDelay and TurnOffMessengerInGame are disabled; without AllowResolutionChange (+ 0x280) Resolution is; without
//   AllowAdvancedOptions (+ 0x281) Detail is;
// - Save (RW 0x91FC9C) on the basic page (+ 0x27C == 1, set by OnInitialized RW 0x816157): Brightness ("%d"), ScrollFactor (the slider, at least 1),
//   the five volumes ("%f", RW 0x6E68F6), AllHealthBars, AlternateMouseSetup, UseEAX3, SendDelay ("yes" / "no"), AudioLOD (High / Low), GameSpyIPAddress
//   (the selected address, "%d.%d.%d.%d"), FirewallPortOverride (the entry when 8088 .. 65534, else 0), then the screen closes (RW 0x91EA39);
// - Reset (RW 0x91F4D1) puts the sliders to the defaults above (brightness the middle of its range), the boxes unchecked, HighAudioQuality as
//   AudioLOD's default, the port entry empty; Cancel (RW 0x91EC52) re-applies the saved volumes and closes.
// Lane PLAY-2: the advanced page (TARGET FACTS, RotWK game.dat, caveat S-001; GameClient/GameLODManager.h for the presets and the option table):
// - the constructor (RW 0x921325 .. 0x921580) registers MasterOption0Template0 .. 4 and MasterOption0TemplateCustom (provider RW 0x91F3B3) and
//   AdvancedOption0Num .. 8Num (provider RW 0x91EA4E: the option's choice count, "%d") and binds the labels the movie shows: APT:MasterOption0_<n> /
//   APT:MasterOption0_Custom = the game text APT:MasterOption_<level name>, APT:AdvancedOption<i> = APT:AdvancedOption_<key>, APT:AdvancedOption<i>_<j> =
//   APT:AdvancedOption_<key>_<choice> (ShadowLOD_UltraHigh reads ShaderLOD_UltraHigh's text, RW 0x921515); + 0x310 is the LOD manager's level (+ 0x1768);
// - MasterOption0Template<n> (RW 0x91F3B3; BFME2 decomp AptOptionsCallbacks.cpp:ExternsLODTemplate, tier A) answers preset n's nine settings ("%d"
//   joined by ",", RW 0x91EE9A), Custom the text kept at + 0x314; a write to Custom replaces + 0x314;
// - EnterAdvancedSettings (RW 0x91F043, tier A): the state becomes 2; with a level chosen (+ 0x310 != -1) + 0x314 = that preset's settings;
// - the Detail combo (RW 0x919167's twin in RotWK): Custom (item data 5) calls the movie's ShowAdvancedSettings, another level becomes + 0x310;
// - Save (RW 0x9204F3 ..), with AllowAdvancedOptions and + 0x310 in 0 .. 5: Custom writes + 0x314's nine values into Options.ini (RW 0x91EE11), another
//   level removes the nine keys (RW 0x6E6986); then StaticGameLOD = the level's name (RW 0x6E6798).
// Lane OPTS-2: Save applies the level (RW 0x6020B4 through GameLODManager::setStaticLODLevel, the Custom record from RW 0x6019D1 first) and the
// device draws with it (ShellServices::applyOption "StaticGameLOD"); the health bars box follows the detail (RW 0x91EB77 / 0x91EBD1: VeryLow and a
// Custom DecalLOD Off check and disable it; Save writes AllHealthBars only while it is allowed); Brightness / ScrollFactor reach the device as options.
// NOT PORTED (stop S-1913, S-2481): the display mode list (the device offers the window's and the screen's size), the LOD manager's ideal level behind
// MasterOption0ResetDefault, the warnings (RW 0x91F2AB: APT:WarnHighGraphicSettings / Detail / Resolution), the "(level)" labels of RW 0x91F175 /
// 0x920E0F, RefreshNat, the live tracking of the sliders (the volumes take effect when saved).
class AptOptionsScreen : public AptScreen
{
public:
	AptOptionsScreen(WindowManager &windows, Shell &shell, ShellEnvironment &environment, ShellServices &services);
	~AptOptionsScreen() override;
	void runInit() override;
	void runShutdown(bool *immediate) override;
	// lane OPTS-2 [S-3402]: the advanced page's rows of the options the device does not draw are greyed and take no input
	void frameUpdate() override;
	const std::set<int> &greyedRows() const { return m_greyedRows; }

	// RW 0x91ED91: push Options.apt (unless it is up) and set its mode bytes
	static void open(Shell &shell, bool networkEnabled, bool allowResolutionChange, bool allowAdvanced, bool advancedOnly);
	void setMode(bool networkEnabled, bool allowResolutionChange, bool allowAdvanced, bool advancedOnly);
	// the gadget InitGadgets was given for "Options::<name>" (tests); null when the movie has not created it
	GameWindow *gadget(const std::string &name) const;
	// the values InitGadgets puts in (tests): the slider position of a volume (0 SFX .. 4 Movie), the brightness and the scroll speed
	int initialVolume(int type) const;
	int initialBrightness() const;
	int initialScrollSpeed() const;
	// lane PLAY-2 (tests): + 0x314, + 0x310, + 0x27C
	const std::string &customTemplate() const { return m_customTemplate; }
	int masterOption() const { return m_masterOption; }
	int pageState() const { return m_state; }
	// RW 0x6446A1's text: Version:Format2 with the build's 2 and 1
	static std::u16string versionText(const GameTextSource *text);

	// ---- lane OPTS-3: the OpenBFME page (NOT RETAIL: GUI/AptScreens/OpenBFMEOptionsPage.h) -------------------------------------------------------
	// RotWK's page state + 0x27C takes 1 (basic) and 2 (advanced); 3 is the port's OpenBFME page. While it is up the basic page's three settings frames
	// (Main.FrameLeft / FrameMiddle / FrameRight) and the movie's tooltip areas are hidden, the subtitle names the page, and the bottom bar's commands
	// become the page's: a click on CANCEL / RESET TO DEFAULTS / ACCEPT is taken before the movie sees it (Options.apt plays its closing animation
	// before it sends AptOptions::Save / Cancel), the commands themselves are redirected too; the buttons' art and hover animation are unchanged.
	static constexpr int kExtensionState = 3;
	// The device has the page's settings: the basic page shows the OPENBFME button. The button is a copy of the movie's own CUSTOM SETTINGS button
	// (Main.FrameLeft.Advanced, duplicated as Main.FrameLeft.OpenBFMEEntry: the same art, font and hover animation) captioned OPENBFME, in the free corner
	// above CANCEL that mirrors the "Version 2.1" text above ACCEPT (stage centre kEntryCentreX / Y); its click is the engine's (a mouse filter), never the
	// copied button's own action.
	static constexpr float kEntryCentreX = 151.0f;
	static constexpr float kEntryCentreY = 676.0f;
	void setExtensionAvailable(bool available) { m_extensionAvailable = available; }
	bool extensionAvailable() const { return m_extensionAvailable; }
	// From the basic page: the page goes up (empty until the device shows a spec through extensionPage()). False and `*error` when it cannot.
	bool openExtension(std::string *error);
	// Back to the basic page (the frames, the subtitle and the retail commands back; the retail gadgets the page mirrors re-read Options.ini).
	void closeExtension();
	bool extensionOpen() const { return m_state == kExtensionState; }
	OpenBFMEOptionsPage *extensionPage() const { return m_page.get(); }
	// The OPENBFME button's stage box (x0 y0 x1 y1); false when it is not on screen.
	bool entryBox(float out[4]) const;
	// The page's events and the button's (Entry), oldest first.
	std::vector<OpenBFMEPageEvent> takeExtensionEvents();
	// Esc while the page is up: the page's CANCEL (the movie never sees the key)
	void extensionCancel() { m_extensionEvents.push_back({ OpenBFMEPageEvent::Kind::Cancel, std::string(), 0 }); }

protected:
	WindowMsgHandledType gadgetMessage(GameWindow *from, std::uint32_t msg, WindowMsgData data1, WindowMsgData data2) override;

private:
	void save();
	void reset();
	void cancel();
	void initGadget(const std::string &name, GameWindow *w);
	bool provide(int index, std::string &value, bool setting);
	// lane PLAY-2: the advanced page
	void bindAdvancedLabels();
	bool provideTemplate(int preset, std::string &value, bool setting); // RW 0x91F3B3
	void enterAdvancedSettings();                                       // RW 0x91F043
	std::string presetTemplate(int preset);                            // RW 0x601BBD + RW 0x91EE9A
	void selectDetail();                                                 // RW 0x91EB30: the Detail combo shows + 0x310
	void saveAdvanced(OptionPreferences &prefs);                         // RW 0x920500 .. 0x92057A (a level in 0 .. 5)
	void writeOptions();                                                 // RW 0x7B274C: Options.ini written
	bool healthBarsAllowed() const;                                      // RW 0x91EB77 (lane OPTS-2)
	void refreshHealthBarsBox(GameWindow *box);                          // RW 0x91EBD1 (lane OPTS-2)
	bool checked(const std::string &name) const; // -1 / not created: false
	int sliderValue(const std::string &name) const; // -1 when the gadget does not exist (RW 0x91EA1D)
	ShellEnvironment &m_env;
	ShellServices &m_services;
	std::map<std::string, GameWindow *> m_gadgets; // "MusicVolume" ... (the name after "Options::")
	int m_state = 0;               // + 0x27C
	bool m_allowResolution = false; // + 0x280 / + 0x282
	bool m_allowAdvanced = false;   // + 0x281
	bool m_networkEnabled = false;  // + 0x283
	bool m_advancedOnly = false;    // + 0x284
	int m_masterOption = 5;         // + 0x310: Options.ini's StaticGameLOD, else Custom (the LOD manager's own level: S-2481)
	std::string m_customTemplate;   // + 0x314 (lane PLAY-2)
	bool m_detailRefreshing = false; // RW 0x91EB30's cleared + 0x2B0
	std::set<int> m_greyedRows;      // lane OPTS-2: the advanced options whose rows frameUpdate greyed
	// lane OPTS-3
	bool mouseFilter(int kind, float x, float y);
	void updateEntry();
	void setExtensionHidden(bool hidden);
	AptCharacterInst *find(const std::string &path) const;
	AptCharacterInst *entryClip() const;
	std::vector<AptTextInst *> subtitleTexts() const;
	static AptTextInst *firstText(AptCharacterInst *under, const std::string &name);
	bool m_extensionAvailable = false;
	bool m_entryPressed = false;
	int m_barPressed = -1; // the bottom bar button a press on the page went to (0 Cancel, 1 Reset, 2 Accept)
	std::unique_ptr<OpenBFMEOptionsPage> m_page;
	std::vector<OpenBFMEPageEvent> m_extensionEvents;
	struct SavedField
	{
		std::string text;
		bool assigned = false;
		bool hasVariable = false;
		AptValue variableValue;
	};
	std::vector<SavedField> m_savedSubtitles;
	std::vector<std::string> m_mirroredAtOpen; // Options.ini's ScrollFactor, AllHealthBars, AlternateMouseSetup when the page opened // Main.SubTitle's text fields when the page opened
	static void setFieldText(AptTextInst *field, const std::string &text);
};

// Palantir.apt (CodePrefix AptPalantir) is lane HUD-1's AptPalantir (AptPalantir.h); the factory below creates it.
