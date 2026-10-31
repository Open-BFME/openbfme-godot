// OpenBFME. GPL-3.0.
// See AptObjectivesMenu.h (lane CAMPUI-1).

#include "GameClient/GUI/AptScreens/AptObjectivesMenu.h"

#include "GameClient/GUI/ShellEnvironment.h"
#include "GameClient/GUI/ShellServices.h"

namespace
{
// RW 0x8E8A8A: the index of the n-th shown objective, -1 none
int nthShownIndex(const ObjectivesScreenInfo &info, int n)
{
	for (size_t i = 0; i < info.objectives.size(); ++i)
	{
		if (info.objectives[i].shown && n-- == 0)
		{
			return (int)i;
		}
	}
	return -1;
}
} // namespace

std::string AptObjectivesMenu::objectiveText(const ObjectivesScreenInfo &info, int row)
{
	if (row < 0 || row >= kRows)
	{
		return std::string();
	}
	const int i = nthShownIndex(info, row);
	if (i < 0)
	{
		return std::string();
	}
	const std::string &label = info.objectives[(size_t)i].label;
	if (label.empty() || label.size() + 2 >= 0xFF) // RW 0x8E8BEB: the provider's buffer
	{
		return std::string();
	}
	return "$" + label; // the movie's text field looks the label up (Apt's "$" game text)
}

std::string AptObjectivesMenu::objectiveStatus(const ObjectivesScreenInfo &info, int row)
{
	std::string out = "00"; // RW 0xC78F04
	if (row < 0 || row >= kRows)
	{
		return out;
	}
	const int i = nthShownIndex(info, row);
	if (i < 0)
	{
		return out;
	}
	out[0] = info.objectives[(size_t)i].completed ? '1' : '0'; // RW 0x836079
	out[1] = info.objectives[(size_t)i].bonus ? '1' : '0';     // RW 0x83617A
	return out;
}

AptObjectivesMenu::AptObjectivesMenu(WindowManager &windows, Shell &shell, ShellServices &services, ShellEnvironment &environment)
	: AptScreen(windows, shell, "Objectives.apt", "AptObjectivesMenu"), m_services(services), m_env(environment)
{
	if (level() < 0)
	{
		return;
	}
	if (!m_env.objectivesScreen)
	{
		windows.note("objectives-unwired", "Objectives.apt: no game objectives (ShellEnvironment::objectivesScreen is null): the rows are empty");
	}
	registerCommand("AptObjectivesMenu::OnInitialized", [](const std::string &) {}); // RW 0x8E90CC: an empty body
	registerCommand("AptObjectivesMenu::ReturnToGame", [this](const std::string &argument) {
		if (!m_env.objectivesScreen || returnAllowed(*m_env.objectivesScreen))
		{
			m_services.request(ShellRequest{ ShellAction::ObjectivesReturnToGame, argument }); // RW 0x8E873E -> RW 0x8E86A5
		}
	});
	// RW 0x8E89E4 (a read; a write is ignored)
	registerProvider("NumOfPlayers", [](const std::string &, std::string &value, bool setting) {
		if (!setting)
		{
			value = "0"; // the PlayerStatus page's count (+ 0x288 == 1) only
		}
		return true;
	});
	registerProvider("InSkirmish", [this](const std::string &, std::string &value, bool setting) {
		if (!setting)
		{
			value = m_env.objectivesScreen && m_env.objectivesScreen->gameMode == 2 ? "1" : "0";
		}
		return true;
	});
	registerProvider("AptObjectivesMenu::InputEnabled", [this](const std::string &, std::string &value, bool setting) {
		if (!setting)
		{
			value = m_env.objectivesScreen && !returnAllowed(*m_env.objectivesScreen) ? "0" : "1";
		}
		return true;
	});
	windows.setBackground(2); // RW 0x8E90DD ends with RW 0x6230B6(2): ShowInGameBackground (the game dimmed behind the screen, as the quit menu)
	for (int row = 0; row < kRows; ++row)
	{
		const std::string name = "Objective" + std::to_string(row + 1); // "Objective%d" (RW 0xC78FF8)
		registerProvider(name, [this, row](const std::string &, std::string &value, bool setting) {
			if (!setting)
			{
				value = m_env.objectivesScreen ? objectiveText(*m_env.objectivesScreen, row) : std::string();
			}
			return true;
		});
		registerProvider(name + "Status", [this, row](const std::string &, std::string &value, bool setting) {
			if (!setting)
			{
				value = m_env.objectivesScreen ? objectiveStatus(*m_env.objectivesScreen, row) : std::string("00");
			}
			return true;
		});
	}
}

AptObjectivesMenu::~AptObjectivesMenu()
{
	if (level() >= 0)
	{
		windows().hideBackground(false); // the dtor RW 0x8E8CED hides the in-game background (RW 0x622C88(0))
	}
}
