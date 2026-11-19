// OpenBFME. GPL-3.0.
// See GameClient/GUI/AptScreens/AptScoreScreen.h.

#include "GameClient/GUI/AptScreens/AptScoreScreen.h"

#include "GameClient/GUI/ShellEnvironment.h"
#include "GameClient/GUI/ShellServices.h"

#include <algorithm>
#include <cstdio>

namespace
{
// VA 0x012B7F9C
const char *const kProviders[11] = { "NumOfPlayers", "ShowSaveReplay", "ShowCampaign", "ScoreGrandTotal", "heroVetCount", "unitsVetCount",
	"PlayerFaction", "numberOfObjectives", "ShowVictory", "objectivesPerScore", "NumberFormatter" };
} // namespace

AptScoreScreen::AptScoreScreen(WindowManager &windows, Shell &shell, ShellEnvironment &environment)
	: AptScreen(windows, shell, "ScoreScreen.apt", "AptScoreScreen"), m_env(environment)
{
	windows.setBackground(1); // the constructor's bfme_showBackground(1)
	registerCommand("AptScoreScreen::OnInitialized", [this](const std::string &) { populate(); });
	auto leave = [this](const std::string &argument) {
		++m_continue;
		if (m_env.services)
		{
			m_env.services->request(ShellRequest{ ShellAction::ScoreScreenContinue, argument });
		}
		else
		{
			this->windows().note("command-unwired", "AptScoreScreen::Continue: the host gave no shell services");
		}
	};
	registerCommand("AptScoreScreen::Continue", leave);
	registerCommand("AptScoreScreen::Exit", leave);
	registerCommand("AptScoreScreen::Save", unportedCommand("AptScoreScreen::Save"));
	registerCommand("AptScoreScreen::RestartGame", unportedCommand("AptScoreScreen::RestartGame"));
	registerCommand("AptScoreScreen::RenameAccept", unportedCommand("AptScoreScreen::RenameAccept"));
	registerCommand("AptScoreScreen::RenameCancel", unportedCommand("AptScoreScreen::RenameCancel"));

	// VA 0x005731E0: every provider answers "0" before it answers
	for (int i = 0; i < 11; ++i)
	{
		registerProvider(kProviders[i], [this, i](const std::string &, std::string &value, bool setting) {
			if (setting)
			{
				if (i == 10)
				{
					m_number = value;
				}
				return true;
			}
			value = "0";
			const ScoreScreenData *d = data();
			switch (i)
			{
				case 0:
					value = std::to_string(m_rows.size());
					break;
				case 1:
					value = d && (d->type == 3 || d->type == 4) ? "1" : "0"; // BFME1 types 2 LAN / 3 Internet are this port's 3 / 4
					break;
				case 8:
					value = "_survived"; // + 0x290 is set by the single player population only
					break;
				case 10:
					value = m_number;
					break;
				default:
					break; // the campaign values: 0 after a skirmish
			}
			return true;
		});
	}
	for (const char *name : { "NumOfPlayers", "ShowSaveReplay", "ShowCampaign", "ScoreGrandTotal", "heroVetCount", "unitsVetCount", "numberOfObjectives", "objectivesPerScore" })
	{
		windows.markNumericProvider(name);
	}
	auto zero = [](const std::string &, std::string &value, bool setting) {
		if (!setting)
		{
			value = "0";
		}
		return true;
	};
	for (const char *name : { "ScoreBattleStat", "ScoreBonusObjectives", "ScoreHeroVeterancy", "ScoreUnitVeterancy", "ScoreTerritoryBonus" })
	{
		registerProvider(name, zero);
	}
	for (char group = 'A'; group <= 'G'; ++group)
	{
		for (int stat = 1; stat <= 3; ++stat)
		{
			registerProvider(std::string("battleStat") + group + std::to_string(stat), zero);
		}
	}
	for (int i = 1; i <= 8; ++i)
	{
		registerProvider("objectiveChecked" + std::to_string(i), zero);
	}
	for (int i = 1; i <= 12; ++i)
	{
		registerProvider("heroVetUpgrade" + std::to_string(i), zero);
	}
	for (int i = 0; i < 8; ++i)
	{
		// VA 0x00571DF0
		registerProvider("ScoreScreen:PlayerColor:" + std::to_string(i), [this, i](const std::string &, std::string &value, bool setting) {
			if (setting)
			{
				return true;
			}
			value = "0";
			const ScoreScreenData *d = data();
			if (d && i < (int)m_rows.size())
			{
				value = std::to_string((int)(d->entries[(size_t)m_rows[(size_t)i]].color & 0xFFFFFFu));
			}
			return true;
		});
		// VA 0x00571E50
		registerProvider("ScoreScreen:PlayerFaction:" + std::to_string(i), [this, i](const std::string &, std::string &value, bool setting) {
			if (setting)
			{
				return true;
			}
			value.clear();
			const ScoreScreenData *d = data();
			if (d && i < (int)m_rows.size())
			{
				value = d->entries[(size_t)m_rows[(size_t)i]].side;
			}
			return true;
		});
		if (i != 0)
		{
			registerProvider("Separator" + std::to_string(i), [this, i](const std::string &, std::string &value, bool setting) {
				if (!setting)
				{
					value = m_teamBreak[(size_t)i] ? "1" : "0";
				}
				return true;
			});
			windows.markNumericProvider("Separator" + std::to_string(i));
		}
	}
	registerScreenRef("AptScoreScreen::InitGadgets", [](const std::string &, GameWindow *) {}); // the campaign's persistent unit list
}

const ScoreScreenData *AptScoreScreen::data() const
{
	return m_env.scoreScreen && m_env.scoreScreen->valid ? m_env.scoreScreen : nullptr;
}

void AptScoreScreen::runInit()
{
	AptScreen::runInit();
	populate();
}

// VA 0x005770E0 / 0x00576C20
void AptScoreScreen::populate()
{
	const ScoreScreenData *d = data();
	if (!d)
	{
		windows().note("scorescreen-unwired", "no ScoreScreenData: the score screen shows the movie's own texts");
		return;
	}
	if (m_populated)
	{
		return;
	}
	m_populated = true;
	m_rows.clear();
	for (size_t i = 0; i < d->entries.size() && m_rows.size() < 8; ++i)
	{
		m_rows.push_back((int)i);
	}
	std::stable_sort(m_rows.begin(), m_rows.end(), [d](int a, int b) { return d->entries[(size_t)a].score > d->entries[(size_t)b].score; });
	m_teamBreak.fill(false);
	for (size_t row = 0; row < m_rows.size(); ++row)
	{
		const ScoreScreenData::Entry &e = d->entries[(size_t)m_rows[row]];
		if (row > 0)
		{
			m_teamBreak[row] = true; // INFERENCE: no alliances in ScoreScreenData (header)
		}
		const std::string cells[8] = { e.name, std::to_string(e.unitsBuilt), std::to_string(e.unitsLost), std::to_string(e.unitsDestroyed),
			std::to_string(e.structuresBuilt), std::to_string(e.structuresLost), std::to_string(e.structuresDestroyed), std::to_string(e.moneyEarned) };
		for (int field = 0; field < 8; ++field)
		{
			char name[32];
			std::snprintf(name, sizeof(name), "PlayerTable:%d:%d", (int)row, field);
			windows().setAptText(name, cells[field]);
		}
	}
	if (m_rows.size() < 8)
	{
		m_teamBreak[m_rows.size()] = true;
	}
}
