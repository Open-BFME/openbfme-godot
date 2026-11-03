// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// The shell: a stack of screens, each an APT movie in one of the WindowManager's `_level` slots (spec menus-apt.md 3.3 "Screen
// lifecycle", build step A4).
//
// Donor facts (ZH GameClient/GUI/Shell/Shell.cpp 264-403 and 560-720; BFME1 Shell_doPush.cpp is the same body with a 16-entry
// stack): push() sets a pending push and runs the top screen's shutdown (or completes at once when the stack is empty or the top
// is hidden); shutdownComplete() performs the pending push or pop; pop() sets a pending pop and runs the top's shutdown;
// popImmediate() pops without waiting for the shutdown; doPush creates the screen through the window manager and runs its
// init; doPop unlinks and destroys the top and runs the init of the new top unless a push is impending.  The stack is 16 deep
// (MAX_SHELL_STACK); a full stack refuses the push.
// Target facts: the screen is made by the factory registered for its file name (the FunctionLexicon table, 0x0046A870
// createMainWindow: no factory -> no window); the factory table pairs the 15 APT screen files with their constructors
// (AptScreenFactories.cpp, BFME1).  Inference [S-171]: an APT screen's shutdown is immediate (no animation).

#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

class AptScreen;
class Shell;
class ShellServices;
class WindowManager;
struct ShellEnvironment;

// What a screen factory is given: everything a screen constructor in the decompile reaches through globals (TheWindowManager,
// TheShell, TheGameText ...).
struct AptScreenContext
{
	WindowManager &windows;
	Shell &shell;
	ShellServices &services;
	ShellEnvironment &environment;
};

using AptScreenFactory = std::function<std::unique_ptr<AptScreen>(AptScreenContext &context)>;

// filename (ASCII case-insensitive, e.g. "MainMenu.apt") -> factory
class AptScreenFactoryTable
{
public:
	bool registerFactory(const std::string &filename, AptScreenFactory factory); // false: already registered or null factory
	bool has(const std::string &filename) const;
	std::unique_ptr<AptScreen> create(const std::string &filename, AptScreenContext &context) const;
	std::vector<std::string> filenames() const;

private:
	static std::string key(const std::string &filename);
	std::map<std::string, std::pair<std::string, AptScreenFactory>> m_factories;
};

class Shell
{
public:
	static constexpr int MAX_SHELL_STACK = 16;

	Shell(WindowManager &windows, AptScreenFactoryTable &factories, ShellServices &services, ShellEnvironment &environment);
	~Shell();

	Shell(const Shell &) = delete;
	Shell &operator=(const Shell &) = delete;

	void push(const std::string &filename, bool shutdownImmediate = false);
	void pop();
	void popImmediate();
	// Pops until the stack is empty (reset).
	void clear();
	// Called by a screen when its shutdown is done (Shell::shutdownComplete).
	void shutdownComplete(AptScreen *screen, bool impendingPush = false);
	// Runs the init of the top as if it had just been pushed (Shell::showShell).
	void showShell(bool runInit);

	// Lane FB7-1: the backdrop behind the shell when no shell map runs. TARGET FACTS (RotWK game.dat, caveat S-001): Shell::showShellMap(use) (RW 0x75DE01)
	// sets + 0x52 = use && !GameData ShellMapOn (GlobalData + 0xAF0) and clears + 0x53, and the display's backdrop image goes (RW 0x65CFF6); with the shell
	// map on it loads the map (not ported: RotWK's GameData has ShellMapOn = No; a mod that turns it on is reported, S-1912). Shell::update (RW 0x75E1D3)
	// then, once (+ 0x53) while + 0x52 is set and no movie runs (+ 0x5D), looks up the mapped image "ShellMapLowLOD" (RW 0x6DA34C) and gives it to the
	// display's backdrop slot 0 (RW 0x65C42C(image, 0, 0, 0, 1.0)), with the window transition FadeInGameMovie_NoAudio (not ported: S-1912). Callers:
	// the engine start (RW 0x5EAB8F: showShellMap(1)), a game's start (RW 0x601C62: 0) and the return to the shell (RW 0x7792BC / 0x779A3D: 1).
	void showShellMap(bool use);
	void update();
	// the mapped image the display draws behind everything now ("" none)
	const std::string &backdropImage() const { return m_backdropImage; }
	// GameData's ShellMapOn of `gameDataText` (data\ini\gamedata.ini); false + *error when the GameData block has no such key or a bad value
	static bool readShellMapOn(const std::string &gameDataText, bool &on, std::string *error);
	// lane BFME2MODE-0: the same in BFME2 1.06 data (bfme2Data), whose gamedata.ini has no ShellMapOn: an absent key is GlobalData's constructor default, 1
	// (BFME2 0x6369FD in GlobalData::GlobalData 0x63631C; RW 0x64308E writes 1 too, but RotWK's data sets the key, and RotWK data without it stays an error)
	static bool readShellMapOnForData(const std::string &gameDataText, bool bfme2Data, bool &on, std::string *error);
	// lane CAMP-2: any Bool field of the GameData block (the last one wins); false + *error when it is there but not Yes / No; `found` false when absent
	static bool readGameDataBool(const std::string &gameDataText, const std::string &field, bool &value, bool &found, std::string *error);

	AptScreen *top() const { return m_stack.empty() ? nullptr : m_stack.back().get(); }
	// Lane OPTS-6: the in-game menu over the stack. TARGET FACT (RotWK game.dat, caveat S-001; BFME2 decomp AptQuitMenuCallbacks.cpp ShowQuitMenu, tier A):
	// the quit menu's opening (RW 0x921B0F) pushes QuitMenu.apt on TheShell (RW 0x75E3B7), so the Options screen it opens (RW 0x921783 -> RW 0x91ED91:
	// Shell::push("Options.apt")) shuts it down (its window hidden, its focus popped) and that screen's pop runs its init again (shown, focused). The port
	// keeps the quit menu outside the stack (the host owns AptQuitMenu, lane END-2); setOverlay gives it that place: it gets its init now (the push's), a push
	// while it is the top shuts it down instead of the screen under it, and the pop back to its depth runs its init instead of that screen's.
	// clearOverlay (the menu closes) pops its focus when it is shown (its pop's shutdown). inputTop: the screen the menus' input goes to.
	void setOverlay(AptScreen *screen);
	void clearOverlay();
	AptScreen *overlay() const { return m_overlay; }
	// lane SAVE-UI1: the overlay's owner destroyed it (AptScreen::~AptScreen): the shell keeps no pointer to it (a borrowed pointer must not outlive the screen)
	void forgetOverlay(const AptScreen *screen)
	{
		if (screen && m_overlay == screen)
		{
			m_overlay = nullptr;
			m_overlayDepth = 0;
		}
	}
	// lane SAVE-UI1: a push is waiting for the top screen's shutdown (the input is the menus' until it has come)
	bool transitioning() const { return m_pendingPush || m_pendingPop; }
	AptScreen *inputTop() const { return overlayIsTop() ? m_overlay : top(); }
	int screenCount() const { return (int)m_stack.size(); }
	AptScreen *screenAt(int index) const { return m_stack[(std::size_t)index].get(); }
	AptScreen *findScreenByFilename(const std::string &filename) const;

	// Everything that went wrong the retail way (no factory for a file, a full stack, a screen that failed to load): never silent.
	const std::vector<std::string> &errors() const { return m_errors; }
	void clearErrors() { m_errors.clear(); }

	WindowManager &windows() { return m_windows; }
	ShellEnvironment &environment() { return m_environment; }

private:
	void doPush(const std::string &filename);
	void doPop(bool impendingPush);
	bool overlayIsTop() const { return m_overlay && (int)m_stack.size() == m_overlayDepth; }

	WindowManager &m_windows;
	AptScreenFactoryTable &m_factories;
	ShellServices &m_services;
	ShellEnvironment &m_environment;
	std::vector<std::unique_ptr<AptScreen>> m_stack;
	AptScreen *m_overlay = nullptr; // lane OPTS-6 (not owned)
	int m_overlayDepth = 0;         // the stack's size when the overlay came: the overlay is the top while the stack has this size
	bool m_lowLodBackdrop = false; // + 0x52
	bool m_lowLodShown = false;    // + 0x53
	std::string m_backdropImage;
	bool m_pendingPush = false;
	bool m_pendingPop = false;
	std::string m_pendingPushName;
	std::vector<std::string> m_errors;
};
