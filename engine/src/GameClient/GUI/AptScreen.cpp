// OpenBFME. GPL-3.0.
// See GameClient/GUI/AptScreen.h.

#include "GameClient/GUI/AptScreen.h"

#include "GameClient/GUI/AptGadgetLayer.h"
#include "GameClient/GUI/Shell/Shell.h"

AptScreen::AptScreen(WindowManager &windows, Shell &shell, std::string filename, std::string codePrefix, int requestedSlot, bool adoptLoaded)
	: m_windows(windows), m_shell(shell), m_filename(std::move(filename)), m_codePrefix(std::move(codePrefix))
{
	// _bfme_AptGameWindow: loadAptWindow("Apt\\", file, 1, 0, slot): the movie loads on the next update
	if (adoptLoaded && m_windows.findAptMovieIndex(m_filename) >= 0)
	{
		m_level = m_windows.findAptMovieIndex(m_filename);
		m_ownsWindow = false;
		return;
	}
	m_level = m_windows.loadAptWindow("Apt\\", m_filename, true, 0, requestedSlot);
	if (m_level < 0)
	{
		m_windows.note("screen-load-refused", m_filename + " (loadAptWindow answered " + std::to_string(m_level) + ")");
	}
	else if (AptGadgetLayer *layer = m_windows.gadgetLayer())
	{
		m_owner = layer->createOwnerWindow([this](GameWindow *from, std::uint32_t msg, WindowMsgData d1, WindowMsgData d2) { return gadgetMessage(from, msg, d1, d2); }, m_filename);
		layer->setLevelOwner(m_level, m_owner);
	}
}

WindowMsgHandledType AptScreen::gadgetMessage(GameWindow *, std::uint32_t, WindowMsgData, WindowMsgData)
{
	return MSG_IGNORED;
}

AptScreen::~AptScreen()
{
	m_windows.removeUpdateListeners(this); // nothing of this screen may run after it is gone
	for (const Registered &r : m_registered)
	{
		switch (r.kind)
		{
			case Kind::Command:
				m_windows.unregisterCommand(r.name);
				break;
			case Kind::Provider:
				m_windows.unregisterProvider(r.name);
				break;
			case Kind::Component:
				m_windows.unregisterComponent(r.name);
				break;
			case Kind::ScreenRef:
				m_windows.closeAptScreen(r.name);
				break;
			case Kind::Tooltip:
				m_windows.unregisterTooltip(r.name);
				break;
		}
	}
	if (m_level >= 0 && m_ownsWindow)
	{
		m_windows.unloadAptWindow(m_level);
	}
	if (m_owner)
	{
		if (AptGadgetLayer *layer = m_windows.gadgetLayer())
		{
			layer->destroyOwnerWindow(m_owner);
		}
	}
}

void AptScreen::runInit()
{
	if (m_hidden && m_level >= 0)
	{
		m_windows.showAptWindow(m_level);
	}
	m_hidden = false;
	if (m_level >= 0)
	{
		m_windows.pushFocus(m_level);
	}
}

void AptScreen::runShutdown(bool *)
{
	// [S-171] an APT screen shuts down at once: its window is hidden (and loses focus), then the shell is told. The screen counts as hidden before the window
	// goes (lane END-1: the movie's own close callbacks run while it is hidden, and the screen's commands must see it closing)
	m_hidden = true;
	if (m_level >= 0)
	{
		m_windows.hideAptWindow(m_level);
		m_windows.popFocus();
	}
	m_shell.shutdownComplete(this);
}

bool AptScreen::registerCommand(const std::string &name, WindowManager::CommandFn fn)
{
	std::shared_ptr<std::map<std::string, int>> calls = m_commandCalls;
	if (!m_windows.registerCommand(name, [calls, name, fn = std::move(fn)](const std::string &argument) {
		    ++(*calls)[name];
		    fn(argument);
	    }))
	{
		m_windows.note("registration-refused", "command " + name);
		return false;
	}
	remember(Kind::Command, name);
	return true;
}

bool AptScreen::registerProvider(const std::string &name, WindowManager::ProviderFn fn)
{
	if (!m_windows.registerProvider(name, std::move(fn)))
	{
		m_windows.note("registration-refused", "provider " + name);
		return false;
	}
	remember(Kind::Provider, name);
	return true;
}

bool AptScreen::registerComponent(const std::string &name, WindowManager::ComponentFn fn)
{
	if (!m_windows.registerComponent(name, std::move(fn)))
	{
		m_windows.note("registration-refused", "component " + name);
		return false;
	}
	remember(Kind::Component, name);
	return true;
}

bool AptScreen::registerScreenRef(const std::string &name, WindowManager::ScreenRefFn fn)
{
	if (!m_windows.registerScreenRef(name, std::move(fn)))
	{
		m_windows.note("registration-refused", "screen ref " + name);
		return false;
	}
	remember(Kind::ScreenRef, name);
	return true;
}

bool AptScreen::registerTooltip(const std::string &name, WindowManager::TooltipFn fn)
{
	if (!m_windows.registerTooltip(name, std::move(fn)))
	{
		m_windows.note("registration-refused", "tooltip " + name);
		return false;
	}
	remember(Kind::Tooltip, name);
	return true;
}

WindowManager::ProviderFn AptScreen::unwiredProvider(const std::string &name)
{
	return [this, name](const std::string &, std::string &, bool setting) {
		m_windows.note("provider-unwired", name + (setting ? " (write)" : " (read)"));
		return false;
	};
}

WindowManager::CommandFn AptScreen::unportedCommand(const std::string &name)
{
	return [this, name](const std::string &argument) {
		m_windows.note("unported-command", name + (argument.empty() ? std::string() : "(" + argument + ")"));
	};
}
