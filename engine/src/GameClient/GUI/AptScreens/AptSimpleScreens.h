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

struct ShellEnvironment;
class ShellServices;

#include <vector>

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
// pop request), Save writes Options.ini (GameClient/OptionPreferences.h); the retail controls stay unported (S-175). OpenBFME addition (owner
// decision 2026-10-07, not a retail control): a "Soft particles" check box (BFME2's window\Apt\CheckBox.wnd) in the band below the Controls panel,
// saved as Options.ini's "SoftParticles" and applied through ShellServices::applyOption [S-1484].
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

protected:
	WindowMsgHandledType gadgetMessage(GameWindow *from, std::uint32_t msg, WindowMsgData data1, WindowMsgData data2) override;

private:
	void ensureSoftBox();
	void save();
	ShellEnvironment &m_env;
	ShellServices &m_services;
	GameWindow *m_softBox = nullptr;
	bool m_pendingSoft = true;
};

// Palantir.apt (CodePrefix AptPalantir) is lane HUD-1's AptPalantir (AptPalantir.h); the factory below creates it.
