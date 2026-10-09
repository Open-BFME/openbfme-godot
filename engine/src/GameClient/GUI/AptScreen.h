// OpenBFME. GPL-3.0.
//
// Base of the per-movie screen classes (one per CodePrefix: AptMainMenu, AptSkirmish ...; spec menus-apt.md 3.1 "AptScreens/*").
//
// Donor facts (BFME1 AptScreenFactories.cpp, AptSkirmishConstructor.cpp, AptMainMenuConstructor.cpp): a screen object derives from
// _bfme_AptGameWindow (loads its movie through WindowManager::loadAptWindow) and, in its constructor, registers its callbacks by name:
// commands (bindShown), providers (bindShownWithArg), tooltips (bind), the `<Prefix>::InitGadgets` screen reference
// (_bfme_setAptScreenRef) and render components (registerAptCallback).  Closing a screen runs _bfme_closeAptScreen(name), which
// erases the screen reference.  Inference [S-171]: the screen also removes the other names it registered (the decompile has no
// destructor body that does; the registries keep the first registration of a name, so a screen opened twice needs them gone).

#pragma once

#include "GameClient/GUI/GameWindow.h"
#include "GameClient/GUI/WindowManager.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

class Shell;

class AptScreen
{
public:
	// requestedSlot: the `_level` the screen asks for (-1 = the first free one, as loadAptWindow's last argument); adoptLoaded: take over
	// a window of that file the manager already holds (AptLevel0, which WindowManager::init loads) instead of loading it again.
	AptScreen(WindowManager &windows, Shell &shell, std::string filename, std::string codePrefix, int requestedSlot = -1, bool adoptLoaded = false);
	virtual ~AptScreen();

	AptScreen(const AptScreen &) = delete;
	AptScreen &operator=(const AptScreen &) = delete;

	const std::string &filename() const { return m_filename; }    // "MainMenu.apt"
	const std::string &codePrefix() const { return m_codePrefix; } // "AptMainMenu"
	int level() const { return m_level; }                          // the `_level` slot, -1 when the load was refused
	bool isHidden() const { return m_hidden; }

	// WindowLayout: runInit runs when the screen is pushed or becomes the top again, runShutdown when it is covered or popped.
	virtual void runInit();
	virtual void runShutdown(bool *immediate);

	// Registers `name` (and remembers it for the close).  Returns false when the registry already holds the name (reported).
	bool registerCommand(const std::string &name, WindowManager::CommandFn fn);
	bool registerProvider(const std::string &name, WindowManager::ProviderFn fn);
	bool registerComponent(const std::string &name, WindowManager::ComponentFn fn);
	bool registerScreenRef(const std::string &name, WindowManager::ScreenRefFn fn);
	bool registerTooltip(const std::string &name, WindowManager::TooltipFn fn);

	// How often the movie ran the command `name` this screen registered (every registerCommand is counted).
	int commandCalls(const std::string &name) const
	{
		auto it = m_commandCalls->find(name);
		return it == m_commandCalls->end() ? 0 : it->second;
	}

	// A provider answering a constant string / undefined, for externs the screen declares but whose data is not wired (reported on
	// every read as "provider-unwired").
	WindowManager::ProviderFn unwiredProvider(const std::string &name);

	WindowManager &windows() { return m_windows; }
	Shell &shell() { return m_shell; }

	// The pseudo window the screen's gadgets report to (BFME1: the screen object is a GameWindow, the gadgets' owner); null when the
	// window manager has no gadget layer.  Gadget messages (GBM_SELECTED, GCM_SELECTED, GLM_SELECTED, GEM_EDIT_DONE, GSM_SLIDER_TRACK ...)
	// arrive in gadgetMessage().  [S-172]
	GameWindow *ownerWindow() const { return m_owner; }

protected:
	virtual WindowMsgHandledType gadgetMessage(GameWindow *from, std::uint32_t msg, WindowMsgData data1, WindowMsgData data2);
	// Reports a command whose engine behaviour is not ported yet; the call is never silent.
	WindowManager::CommandFn unportedCommand(const std::string &name);

private:
	enum class Kind
	{
		Command,
		Provider,
		Component,
		ScreenRef,
		Tooltip
	};
	void remember(Kind kind, const std::string &name) { m_registered.push_back({ kind, name }); }

	WindowManager &m_windows;
	Shell &m_shell;
	std::string m_filename;
	std::string m_codePrefix;
	int m_level = -1;
	GameWindow *m_owner = nullptr;
	bool m_ownsWindow = true;
	bool m_hidden = false;
	struct Registered
	{
		Kind kind;
		std::string name;
	};
	std::vector<Registered> m_registered;
	std::shared_ptr<std::map<std::string, int>> m_commandCalls = std::make_shared<std::map<std::string, int>>();
};
