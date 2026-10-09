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

	AptScreen *top() const { return m_stack.empty() ? nullptr : m_stack.back().get(); }
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

	WindowManager &m_windows;
	AptScreenFactoryTable &m_factories;
	ShellServices &m_services;
	ShellEnvironment &m_environment;
	std::vector<std::unique_ptr<AptScreen>> m_stack;
	bool m_pendingPush = false;
	bool m_pendingPop = false;
	std::string m_pendingPushName;
	std::vector<std::string> m_errors;
};
