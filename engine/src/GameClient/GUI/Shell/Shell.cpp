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

void Shell::showShellMap(bool use)
{
	// RW 0x75DE01 (the shell map branch is not ported: S-1912)
	m_lowLodBackdrop = use && !m_environment.shellMapOn;
	m_lowLodShown = false;
	m_backdropImage.clear(); // RW 0x65CFF6
	if (use && m_environment.shellMapOn)
	{
		m_windows.note("shell-map", "[S-1912] GameData ShellMapOn = Yes: the shell map is not ported, nothing is drawn behind the shell");
	}
}

void Shell::update()
{
	// RW 0x75E1D3 (the low-LOD branch; the movie flag + 0x5D is never set by the port)
	if (!m_lowLodShown && m_lowLodBackdrop)
	{
		m_lowLodShown = true;
		m_backdropImage = "ShellMapLowLOD";
		m_windows.note("shell-map", "[S-1912] the backdrop ShellMapLowLOD is shown without its window transition FadeInGameMovie_NoAudio");
	}
}

bool Shell::readShellMapOn(const std::string &text, bool &on, std::string *error)
{
	bool found = false;
	if (!readGameDataBool(text, "ShellMapOn", on, found, error))
	{
		return false;
	}
	if (!found && error)
	{
		*error = "GameData has no ShellMapOn";
	}
	return found;
}

bool Shell::readGameDataBool(const std::string &text, const std::string &field, bool &value, bool &found, std::string *error)
{
	// the GameData block's "<field> = Yes|No" (INI::parseBool: yes / no, case-insensitive); the last one in the block wins
	bool inBlock = false;
	found = false;
	std::string key = field;
	for (char &c : key)
	{
		c = (char)std::tolower((unsigned char)c);
	}
	std::size_t pos = 0;
	while (pos <= text.size())
	{
		std::size_t end = text.find('\n', pos);
		if (end == std::string::npos)
		{
			end = text.size();
		}
		std::string line = text.substr(pos, end - pos);
		pos = end + 1;
		const std::size_t comment = line.find_first_of(";");
		if (comment != std::string::npos)
		{
			line.erase(comment);
		}
		std::string tokens[3];
		int n = 0;
		std::size_t i = 0;
		while (n < 3 && i < line.size())
		{
			while (i < line.size() && (std::isspace((unsigned char)line[i]) || line[i] == '='))
			{
				++i;
			}
			const std::size_t start = i;
			while (i < line.size() && !std::isspace((unsigned char)line[i]) && line[i] != '=')
			{
				++i;
			}
			if (i > start)
			{
				tokens[n++] = line.substr(start, i - start);
			}
		}
		auto lower = [](std::string v) {
			for (char &c : v)
			{
				c = (char)std::tolower((unsigned char)c);
			}
			return v;
		};
		if (n == 0)
		{
			continue;
		}
		const std::string t0 = lower(tokens[0]);
		if (!inBlock)
		{
			inBlock = t0 == "gamedata";
			continue;
		}
		if (t0 == "end")
		{
			inBlock = false;
			continue;
		}
		if (t0 == key)
		{
			const std::string v = n > 1 ? lower(tokens[1]) : std::string();
			if (v != "yes" && v != "no")
			{
				if (error)
				{
					*error = "GameData " + field + ": '" + (n > 1 ? tokens[1] : std::string()) + "' is not Yes / No";
				}
				return false;
			}
			value = v == "yes";
			found = true;
		}
	}
	return true;
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
