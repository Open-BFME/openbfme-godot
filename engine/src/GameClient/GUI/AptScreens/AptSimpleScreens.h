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

#include <memory>
#include <string>
#include <vector>

struct ShellEnvironment;
class ShellServices;

#include <map>
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
	AptGuiFXScreen(WindowManager &windows, Shell &shell);
};

// lane PLAY-1: PlayerTribute.apt (CodePrefix AptPlayerTribute), the screen the Palantir's flag opens in a skirmish / multiplayer game (RW 0x914EF0 pushes it).
// TARGET FACTS (RotWK game.dat, caveat S-001): the screen registers its commands under the movie's own path (the movie sets gameCodePrefix = String(this)):
// "<path>_OnInitialized", "_ReturnToGame" (RW 0x9175AD -> RW 0x914E89 -> RW 0x914C51: + 0x278 closing, TheShell + 0x54 = 1, RW 0x62215B: the shell pops
// the screen), "_OnPageLoaded" / "_OnPageUnloaded" / "_OnPageSelected" (RW 0x917615 / 0x91767D / 0x9176E5), "_Send" / "_Reset" (RW 0x916B12 / 0x916AB4), the
// extern "<path>_TributeEnabled" (RW 0x91774D) and the pages "TributePage" / "StatusPage"; its key handler (RW 0x914E91) closes it on Escape.
// NOT PORTED (stop S-1922): the tribute itself (MSG_GIVE_MONEY 1126 has no logic port: the extern answers false, so the movie shows its status page only), the
// pause / input calls of the opening (RW 0x914FA2 .. 0x914FE2).
// lane HUD-5: the Status page (the owner's report: every cell showed its text record's name). TARGET FACTS (RotWK game.dat, caveat S-001; BFME2 decomp map tier A):
//   * "_OnPageLoaded" (RW 0x91741F, bound at RW 0x917615; the decomp's AptTributePageCallbacks.cpp OnPageLoaded): the parameters "name" (the page clip's path) and "type"; a page on
//     the screen's own level (AptUtils::LevelIndexFromTarget RW 0x8155D9) of type "StatusPage" is a row list page (RW 0x9166AC = BFME2 0x9103A3), of type
//     "TributePage" the tribute page (play1d's, not here); "_OnPageUnloaded" (RW 0x9171E3) / "_OnPageSelected" (RW 0x917258) drop / switch
//     the page by its path.
//   * the row list page registers "_level<n>." + <path without "_level<n>."> (AptUtils::SkipLevelN RW 0x815563) + "_OnRowShown" / "_OnRowHidden" and the externs
//     "_NumOfPlayers" (the row count) / "_InSkirmish" (RW 0x914AA1); its rows are GUI/PlayerStatusInfo.h's.
//   * "_OnRowShown" (RW 0x915E00 = AptRowListCallbacks.cpp OnRowShown): "index" in [0, count] and "name" on the page's level: the row object (RW 0x915BF6) sets
//     the records "APT:_level%u.%s_field%d" (RW 0x915159, format RW 0xC7C3E4) of the row's path to the four texts and registers the extern
//     "_level%u.%s_color" (RW 0x915B4A, format RW 0xC7C430; RW 0x914A69 prints the colour); "_OnRowHidden" (RW 0x915269) drops the row object.
//   * the command parameters are "key=value" pairs joined by '&' (RW 0x81560E).
// INFERENCE: retail accepts index == count (one past the rows; the slot then holds no player): the port refuses it and reports it.
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

	ShellServices &m_services;
	ShellEnvironment &m_env;
	std::unique_ptr<StatusPage> m_status;
	std::string m_statusName; ///< the full path the movie loaded it under (the page map's key)
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
// pop request), Save writes Options.ini (GameClient/OptionPreferences.h). OpenBFME addition (owner decision 2026-10-07, not a retail control): a
// "Soft particles" check box (BFME2's window\Apt\CheckBox.wnd) in the band below the Controls panel, saved as Options.ini's "SoftParticles" and applied
// through ShellServices::applyOption [S-1484].
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
// NOT PORTED (stop S-1913, S-2481): applying a resolution change and the brightness (TheDisplay), the LOD manager behind MasterOption0ResetDefault and the
// health bars box's enable (RW 0x91EB77), applying a level or the custom settings to the renderer (RW 0x6020B4 / 0x6019D1: INFERENCE, the level is
// taken as accepted), the warnings (RW 0x91F2AB: APT:WarnHighGraphicSettings / Detail / Resolution), the "(level)" labels of RW 0x91F175 / 0x920E0F,
// RefreshNat, the live tracking of the sliders (the volumes take effect when saved).
class AptOptionsScreen : public AptScreen
{
public:
	AptOptionsScreen(WindowManager &windows, Shell &shell, ShellEnvironment &environment, ShellServices &services);
	~AptOptionsScreen() override;
	void runInit() override;
	void runShutdown(bool *immediate) override;
	GameWindow *softParticlesBox() const { return m_softBox; }
	bool pendingSoftParticles() const { return m_pendingSoft; }
	// the box's stage rectangle (x, y, width, height): the band under the Controls panel of Options.apt's 1024 x 768 stage [S-1484]
	static constexpr int kSoftBoxX = 116, kSoftBoxY = 618, kSoftBoxW = 240, kSoftBoxH = 28;

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

protected:
	WindowMsgHandledType gadgetMessage(GameWindow *from, std::uint32_t msg, WindowMsgData data1, WindowMsgData data2) override;

private:
	void ensureSoftBox();
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
	bool checked(const std::string &name) const; // -1 / not created: false
	int sliderValue(const std::string &name) const; // -1 when the gadget does not exist (RW 0x91EA1D)
	ShellEnvironment &m_env;
	ShellServices &m_services;
	GameWindow *m_softBox = nullptr;
	bool m_pendingSoft = true;
	std::map<std::string, GameWindow *> m_gadgets; // "MusicVolume" ... (the name after "Options::")
	int m_state = 0;               // + 0x27C
	bool m_allowResolution = false; // + 0x280 / + 0x282
	bool m_allowAdvanced = false;   // + 0x281
	bool m_networkEnabled = false;  // + 0x283
	bool m_advancedOnly = false;    // + 0x284
	int m_masterOption = 5;         // + 0x310: Options.ini's StaticGameLOD, else Custom (the LOD manager's own level: S-2481)
	std::string m_customTemplate;   // + 0x314 (lane PLAY-2)
	bool m_detailRefreshing = false; // RW 0x91EB30's cleared + 0x2B0
};

// Palantir.apt (CodePrefix AptPalantir) is lane HUD-1's AptPalantir (AptPalantir.h); the factory below creates it.
