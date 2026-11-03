// OpenBFME. GPL-3.0.
// See GameClient/GUI/AptScreens/AptStrategicConflictResults.h (WOTR2 milestone 5).

#include "GameClient/GUI/AptScreens/AptStrategicConflictResults.h"

#include "GameClient/GUI/WindowManager.h"

AptStrategicConflictResults::AptStrategicConflictResults(WindowManager &windows, Shell &shell, const ConflictResultsData &data)
	: AptScreen(windows, shell, "StrategicConflictResults.apt", "AptStrategicConflictResults"), m_data(data)
{
	registerCommand("AptStrategicConflictResults::OnInitialized", [this](const std::string &) { // RW 0x9FEF35
		if (m_state == 1)
		{
			m_state = 2;
		}
	});
	registerCommand("AptStrategicConflictResults::OnFadeOut", [this](const std::string &) { m_state = 4; }); // RW 0x9FEF45
	registerCommand("AptStrategicConflictResults::Continue", [this](const std::string &) { m_state = 5; });  // RW 0x9FEF4F
	for (int d = 0; d < 6; ++d) // RW 0x9FE795
	{
		registerProvider("Player_" + std::to_string(d) + "_Color", [this, d](const std::string &, std::string &value, bool setting) {
			value = "0";
			if (!setting && d < (int)m_data.rows.size())
			{
				value = std::to_string((std::int32_t)(m_data.rows[(size_t)d].color | 0xFF000000u)); // a signed decimal (the alpha bit)
			}
			return true;
		});
	}
	registerProvider("StrategicConflicResults::NumWinners", [this](const std::string &, std::string &value, bool setting) { // RW 0x9FE5BD
		if (!setting)
		{
			value = std::to_string(m_data.numWinners);
		}
		return true;
	});
	registerProvider("StrategicConflicResults::NumPlayers", [this](const std::string &, std::string &value, bool setting) {
		if (!setting)
		{
			value = std::to_string(m_data.rows.size());
		}
		return true;
	});
	fill();
}

void AptStrategicConflictResults::fill()
{
	// SetCurrentBattle RW 0x9FEBFF
	for (size_t row = 0; row < m_data.rows.size(); ++row)
	{
		const ConflictResultsData::Row &r = m_data.rows[row];
		const std::string p = "APT:Player_" + std::to_string(row) + "_";
		windows().setAptText(p + "Name_String", r.name.empty() ? std::string("Unknown") : r.name);
		windows().setAptText(p + "Destoyed_String", std::to_string(r.destroyed));
		windows().setAptText(p + "HeroLost_String", std::to_string(r.heroesLost));
		windows().setAptText(p + "Lost_String", std::to_string(r.lost));
		windows().setAptText(p + "Veterancy_String", std::to_string(r.veterancy));
		windows().setAptImage("StrategicConflictResults::PlayerFaction_" + std::to_string(row), r.factionImage);
	}
}

void AptStrategicConflictResults::update()
{
	// RW 0x9FE6CF (the timer of state 2 never fires: its registration RW 0x8F8014 returns 0)
	if (m_state == 4)
	{
		m_state = 0; // HideLevel (RW 0x622463): the host drops the screen
	}
	else if (m_state == 5)
	{
		std::string err;
		if (!windows().invokeASAt(level(), "", "SetState", { "_fadeOut" }, nullptr, &err)) // RW 0x9FE665
		{
			windows().note("invoke-failed", "StrategicConflictResults SetState(_fadeOut): " + err);
		}
		m_state = 3;
	}
}
