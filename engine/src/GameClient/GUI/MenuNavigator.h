// OpenBFME. GPL-3.0.
//
// Lane OPTS-6: the menus without a mouse: keyboard and game controller navigation. NOT RETAIL: RotWK's menus are mouse driven (a few screens read
// single keys: Esc on Options / the powers screen / the tribute screen, Enter in a text entry); this is an OpenBFME improvement on top of the retail
// mouse behaviour, which stays exactly as it was (the owner, 2026-10-15: "we need to properly fix the menus so they ... can be used with a controller
// or keyboard"). The device maps keys and the controller to MenuNavKey (GodotAptPlayer / GodotInGameHud; a controller's buttons arrive as the same
// keys, godot/scripts/menu_pad.gd).
//
// What it works on: the menu that has the input (Shell::inputTop: the top screen, or the quit menu over a game) and its level:
//   * the level's Apt buttons the pointer could press there now (AptInput::hitTestButtons at the button's centre answers the button: enabled, shown up
//     its chain, not covered), without the full-stage catchers (INFERENCE: a button covering 90 % of the stage both ways is a click-away area, such as
//     Skirmish.apt's alpha 0 one behind its Profile pop-up, not a control);
//   * the level's gadgets (WindowManager component windows: combo boxes, check boxes, sliders, text entries, list boxes, push buttons) shown and enabled;
//   * a screen's own targets (MenuNavigationSource: the OpenBFME page's tabs and rows).
// A click-away area that covers the stage (Skirmish's Profile nav puts one behind its open sub-menu) closes the sub-menu when the pointer reaches it.
// It is how a mouse leaves that sub-menu: when a key finds no target its way (an arrow at the edge, Tab past
// the last) or Back is pressed while one is up, the pointer goes onto a point only that area takes, the movie closes the sub-menu, and the next key goes
// on from where the focus was.
// Focus: Tab / Shift+Tab walk them in reading order (top to bottom, then left to right), the arrows go to the nearest target that way; the focused
// target gets the pointer's hover (the virtual pointer moves onto its centre through WindowManager::postMouseMove, so a button shows its own Over
// state, a page row its help line, as the mouse would). A frame is drawn only when no native button highlight is available (appendFocusFrame). Enter / Space press and release it there
// through WindowManager::postMouseButton: the click a player makes, through the same input path. Left / Right change a slider (its position by a
// step, then the messages a drag sends: GSM_SLIDER_TRACK and, at the end, GSM_SLIDER_CLICKED, RW 0x7234EC's 0x4010) ; closed combo boxes keep the directions for moving between controls. A opens a combo box; a list box takes Up / Down / Enter itself (ZH GadgetListBoxInput's GWM_CHAR keys); an open drop-down
// list takes Up / Down (preview only), Enter confirms, and Esc cancels. Maps/Rules lobby tabs switch with PageUp/PageDown (LB/RB). Esc (Back) and PageUp / PageDown go to the screen's source;
// without one Back presses the screen's back button when it has exactly one (backButtonNames). A text entry with the keyboard focus keeps the keys
// it edits with (Left / Right / Enter / Space and characters). Keys arriving before any focus only show the focus (the first target), they press
// nothing.
#pragma once

#include "GameClient/GUI/GadgetDrawList.h"
#include "GameClient/GUI/GameWindow.h"

#include <cstdint>
#include <string>
#include <vector>

class WindowManager;

enum class MenuNavKey : std::uint8_t
{
	Up,
	Down,
	Left,
	Right,
	Next,         // Tab
	Previous,     // Shift+Tab
	Activate,     // Enter, Space (controller A)
	Back,         // Esc (controller B)
	PageNext,     // PageDown (controller RB)
	PagePrevious, // PageUp (controller LB)
};
const char *menuNavKeyName(MenuNavKey key);

struct MenuNavTarget
{
	enum class Kind : std::uint8_t
	{
		Button, // an Apt button of the level (id: its target path)
		Gadget, // a gadget window (id: its component path, or the source's name)
		Item    // a source's own hit area (id: the source's name, e.g. "tab:2")
	};
	Kind kind = Kind::Button;
	std::string id;
	float x0 = 0, y0 = 0, x1 = 0, y1 = 0; // stage box
	GameWindow *window = nullptr;         // Gadget
	float cx() const { return (x0 + x1) * 0.5f; }
	float cy() const { return (y0 + y1) * 0.5f; }
};

// A screen with controls of its own (lane OPTS-6: the OpenBFME page of Options.apt). The navigator asks it while it is registered with the window
// manager (WindowManager::setNavigationSource).
class MenuNavigationSource
{
public:
	virtual ~MenuNavigationSource() = default;
	// Adds the screen's own targets; may drop collected ones it covers (`targets` holds the level's buttons and gadgets).
	virtual void navTargets(std::vector<MenuNavTarget> &targets) = 0;
	// A key the screen takes itself for the focused target (null: none yet): true when it did.
	virtual bool navKey(MenuNavKey key, const MenuNavTarget *focus) = 0;
	// The target to focus first (empty: the first in reading order).
	virtual std::string navFirst() const { return std::string(); }
};

class MenuNavigator
{
public:
	explicit MenuNavigator(WindowManager &windows);

	// The menu that has the input: its level and the screen (identity only; the focus resets when either changes). Level -1: no menu (keys are not taken).
	void setMenu(int level, const void *screen);
	int level() const { return m_level; }
	// True when the key was the navigation's (the caller passes it on to nothing else).
	bool key(MenuNavKey key);
	void setControllerSections(bool enabled);
	bool supportsControllerSections() const;
	// OpenBFME result-screen defaults, refreshed as the authored movie changes pages.
	void setControllerResults(bool enabled);
	void switchControllerSection(int delta);
	int controllerSection() const { return m_controllerSections ? m_section : -1; }
	// The real pointer moved (or pressed): the focus frame goes until the next key; the focus itself stays.
	void pointerMoved() { m_frameShown = false; }
	void clearFocus();

	// The targets now, in reading order.
	std::vector<MenuNavTarget> targets() const;
	// The click-away areas now (buttons over 90 % of the stage that take the pointer): Esc and Tab past the last first move the pointer onto one (leave)
	std::vector<MenuNavTarget> catchers() const;
	const std::string &focusId() const { return m_focusId; }
	// The focused target's stage box now; false: no focus or it is gone.
	bool focusBox(float out[4]) const;
	bool frameShown() const { return m_frameShown && !m_focusId.empty(); }
	// The focus frame over everything (an OpenRect two stage pixels outside the target, in the menus' highlight colour)
	void appendFocusFrame(GadgetDrawList &out) const;
	static constexpr Color kFrameColor = 0xFFBFEDFFu; // the pale blue of Options.apt's row labels (OpenBFMEOptionsPage::kLabelColor); not retail
	static constexpr int kSliderSteps = 20;           // Left / Right move a slider by a twentieth of its range (at least 1)

	// What the keys did, oldest first ("focus <id>", "press <id>", "slider <id> <position>", "choice <id> <index>", "list <id> <key>", "back <id>",
	// "source <key>"), for the tests and the scripted runs
	const std::vector<std::string> &log() const { return m_log; }
	void clearLog() { m_log.clear(); }

	// The leaf names Back presses when the screen has exactly one such shown button (case-insensitive, the last path element): retail screens' own
	// back / cancel / close buttons (INFERENCE from the movies' button names, not a retail key binding)
	static const std::vector<std::string> &backButtonNames();

private:
	void collect(std::vector<MenuNavTarget> &out, std::vector<MenuNavTarget> *catchers = nullptr, bool section = true) const;
	bool leave(const std::vector<MenuNavTarget> &catchers, const std::vector<MenuNavTarget> &list);
	const MenuNavTarget *find(const std::vector<MenuNavTarget> &list, const std::string &id) const;
	void focus(const MenuNavTarget &t);
	void press(const MenuNavTarget &t);
	bool gadgetKey(MenuNavKey key, const MenuNavTarget &t);
	bool dropDownKey(MenuNavKey key, GameWindow *combo);
	bool textEntryFocused() const;
	int nearest(const std::vector<MenuNavTarget> &list, int from, MenuNavKey dir) const;
	void back(const std::vector<MenuNavTarget> &list);

	WindowManager &m_windows;
	int m_level = -1;
	const void *m_screen = nullptr;
	std::string m_focusId;
	MenuNavTarget m_focusTarget; // the focused target as it was focused (Tab goes on from its place when the screen changed under it)
	bool m_frameShown = false;
	bool m_controllerSections = false;
	int m_section = 1; // bottom bar, player setup, maps/rules
	bool m_controllerResults = false;
	std::string m_resultPage;
	bool m_noted = false; // the S-4981 note was given
	std::vector<std::string> m_log;
};
