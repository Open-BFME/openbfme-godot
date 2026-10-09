// OpenBFME. GPL-3.0.
// See GameClient/GUI/Shell/Shell.h.

#include "GameClient/GUI/Shell/Shell.h"

#include "GameClient/GUI/AptScreen.h"
#include "GameClient/GUI/ShellEnvironment.h"
#include "GameClient/GUI/WindowManager.h"

#include <cctype>

std::string AptScreenFactoryTable::key(const std::string &filename)
{
	std::string k = filename;
	for (char &c : k)
	{
		c = (char)std::tolower((unsigned char)c);
	}
	return k;
}

bool AptScreenFactoryTable::registerFactory(const std::string &filename, AptScreenFactory factory)
{
	if (!factory)
	{
		return false;
	}
	return m_factories.emplace(key(filename), std::make_pair(filename, std::move(factory))).second;
}

bool AptScreenFactoryTable::has(const std::string &filename) const
{
	return m_factories.count(key(filename)) != 0;
}

std::unique_ptr<AptScreen> AptScreenFactoryTable::create(const std::string &filename, AptScreenContext &context) const
{
	auto it = m_factories.find(key(filename));
	if (it == m_factories.end())
	{
		return nullptr;
	}
	return it->second.second(context);
}

std::vector<std::string> AptScreenFactoryTable::filenames() const
{
	std::vector<std::string> out;
	for (const auto &e : m_factories)
	{
		out.push_back(e.second.first);
	}
	return out;
}

Shell::Shell(WindowManager &windows, AptScreenFactoryTable &factories, ShellServices &services, ShellEnvironment &environment)
	: m_windows(windows), m_factories(factories), m_services(services), m_environment(environment)
{
	m_windows.setShell(this);
}

Shell::~Shell()
{
	clear();
	if (m_windows.shell() == this)
	{
		m_windows.setShell(nullptr);
	}
}

void Shell::clear()
{
	while (!m_stack.empty())
	{
		doPop(true);
	}
	m_pendingPush = m_pendingPop = false;
}

AptScreen *Shell::findScreenByFilename(const std::string &filename) const
{
	// Shell_findScreenByFilename.cpp: a case-insensitive match on the layout file name
	auto lower = [](std::string s) {
		for (char &c : s)
		{
			c = (char)std::tolower((unsigned char)c);
		}
		return s;
	};
	const std::string want = lower(filename);
	for (const auto &s : m_stack)
	{
		if (lower(s->filename()) == want)
		{
			return s.get();
		}
	}
	return nullptr;
}

void Shell::push(const std::string &filename, bool shutdownImmediate)
{
	if (filename.empty())
	{
		return;
	}
	if ((int)m_stack.size() >= MAX_SHELL_STACK)
	{
		m_errors.push_back("Unable to load screen '" + filename + "', max " + std::to_string(MAX_SHELL_STACK) + " reached");
		return;
	}
	m_pendingPush = true;
	m_pendingPushName = filename;
	AptScreen *currentTop = top();
	if (currentTop && !currentTop->isHidden())
	{
		bool immediate = shutdownImmediate;
		currentTop->runShutdown(&immediate);
	}
	else
	{
		shutdownComplete(nullptr);
	}
}

void Shell::pop()
{
	AptScreen *screen = top();
	if (!screen)
	{
		return;
	}
	m_pendingPop = true;
	bool immediate = false;
	screen->runShutdown(&immediate);
}

void Shell::popImmediate()
{
	AptScreen *screen = top();
	if (!screen)
	{
		return;
	}
	m_pendingPop = false;
	bool immediate = true;
	screen->runShutdown(&immediate);
	// runShutdown reports shutdownComplete with no pending pop: the pop is forced here
	if (top() == screen)
	{
		doPop(false);
	}
}

void Shell::showShell(bool runInit)
{
	if (runInit && top())
	{
		top()->runInit();
	}
}

void Shell::shutdownComplete(AptScreen *, bool impendingPush)
{
	if (m_pendingPush)
	{
		const std::string name = m_pendingPushName;
		doPush(name);
		m_pendingPush = false;
		m_pendingPushName.clear();
	}
	else if (m_pendingPop)
	{
		doPop(impendingPush);
		m_pendingPop = false;
	}
}

void Shell::doPush(const std::string &filename)
{
	AptScreenContext context{ m_windows, *this, m_services, m_environment };
	std::unique_ptr<AptScreen> screen = m_factories.create(filename, context);
	if (!screen)
	{
		// 0x0046A870 createMainWindow: no factory registered for the file name -> no window; ZH asserts "Shell unable to load pending
		// push layout" on a null layout
		m_errors.push_back("Shell unable to load pending push layout '" + filename + "': no screen factory is registered for it");
		return;
	}
	if (screen->level() < 0)
	{
		m_errors.push_back("Shell: the window manager refused the movie of '" + filename + "'");
		return;
	}
	AptScreen *raw = screen.get();
	m_stack.push_back(std::move(screen));
	raw->runInit();
}

void Shell::doPop(bool impendingPush)
{
	if (m_stack.empty())
	{
		return;
	}
	m_stack.pop_back(); // the destructor unloads the window and unregisters the screen's names
	AptScreen *newTop = top();
	if (newTop && !impendingPush)
	{
		newTop->runInit();
	}
}
