// OpenBFME. GPL-3.0.
// See GameClient/LinearCampaign.h (lane CAMP-1).

#include "GameClient/LinearCampaign.h"

#include "Common/AsciiString.h"
#include "Common/INI.h"
#include "Common/INIException.h"
#include "GameLogic/ContainParseHooks.h"

#include <cstddef>
#include <sstream>

thread_local LinearCampaignManager *TheLinearCampaignManager = nullptr;

namespace
{
// RW 0x73B217 -> 0x73AA94: "NoSound" clears; any other name must be an audio event of this world (the hook RetailObjectWorld installs)
void parseLoadScreenMusic(INI *ini, void *, void *store, const void *)
{
	const std::string name = ini->getNextToken();
	if (AsciiStringUtil::compareNoCase(name, "NoSound") == 0)
	{
		static_cast<std::string *>(store)->clear();
		return;
	}
	const ContainParseHooks &hooks = TheContainParseHooks();
	if (!hooks.audioEventExists)
	{
		throw INIException(8, "audio event '%s' cannot be resolved: no TheAudio lookup is installed", name.c_str());
	}
	if (!hooks.audioEventExists(name))
	{
		throw INIException(3, "Invalid Sound '%s'", name.c_str()); // RW 0xC2500C
	}
	*static_cast<std::string *>(store) = name;
}

// RW 0xBF5268
const FieldParse kMissionParse[] = {
	{ "Map", INI::parseAsciiString, nullptr, (int)offsetof(LinearCampaignMission, map) },
	{ "IntroMovie", INI::parseAsciiString, nullptr, (int)offsetof(LinearCampaignMission, introMovie) },
	{ "LoadScreenImage", INI::parseAsciiString, nullptr, (int)offsetof(LinearCampaignMission, loadScreenImage) },
	{ "LoadScreenMusicTrack", parseLoadScreenMusic, nullptr, (int)offsetof(LinearCampaignMission, loadScreenMusicTrack) },
	{ "MillisecondsAfterStartToStartFadeUp", INI::parseDurationUnsignedInt, nullptr, (int)offsetof(LinearCampaignMission, fadeUpFrames) },
	{ "DelayCarryoverSpawningOf", INI::parseAsciiStringVectorAppend, nullptr, (int)offsetof(LinearCampaignMission, delayCarryover) },
	{ nullptr, nullptr, nullptr, 0 }
};

// RW 0x5EC9FE: the mission record, appended, then its fields (RW 0x5EAA86); a mission without a Map throws
void parseMission(INI *ini, void *, void *store, const void *)
{
	std::vector<LinearCampaignMission> &missions = *static_cast<std::vector<LinearCampaignMission> *>(store);
	LinearCampaignMission m;
	m.name = ini->getNextToken();
	missions.push_back(m);
	LinearCampaignMission &added = missions.back();
	ini->initFromINI(&added, kMissionParse);
	if (added.map.empty())
	{
		throw INIException(3, "Campaign missions must have a Map. %s does not", added.name.c_str()); // RW 0xBF5238
	}
}

// RW 0xBF5354
const FieldParse kCampaignParse[] = {
	{ "CampaignDisplayNameLabel", INI::parseAsciiString, nullptr, (int)offsetof(LinearCampaign, displayNameLabel) },
	{ "OverallCampaignIntroMovie", INI::parseAsciiString, nullptr, (int)offsetof(LinearCampaign, overallIntroMovie) },
	{ "CarryoverUnit", INI::parseAsciiStringVectorAppend, nullptr, (int)offsetof(LinearCampaign, carryoverUnits) },
	{ "Mission", parseMission, nullptr, (int)offsetof(LinearCampaign, missions) },
	{ nullptr, nullptr, nullptr, 0 }
};
} // namespace

void LinearCampaignManager::registerBlock(INIBlockRegistry &registry)
{
	registry.registerBlock("LinearCampaign", [](INI *ini) {
		if (!TheLinearCampaignManager)
		{
			throw INIException(8, "LinearCampaign block: TheLinearCampaignManager is not installed");
		}
		TheLinearCampaignManager->parseLinearCampaign(ini);
	});
}

void LinearCampaignManager::parseLinearCampaign(INI *ini)
{
	if (ini->getLoadType() != INI_LOAD_OVERWRITE)
	{
		throw INIException(8, "Sorry, you cannot define a 'LinearCampaign' block anywhere but the main INI files."); // RW 0xBF53F0
	}
	LinearCampaign c;
	c.name = ini->getNextToken();
	m_campaigns.push_back(c); // RW 0x5ECC82
	ini->initFromINI(&m_campaigns.back(), kCampaignParse); // RW 0x5ECA71
}

const LinearCampaign *LinearCampaignManager::find(const std::string &name) const
{
	for (const LinearCampaign &c : m_campaigns)
	{
		if (AsciiStringUtil::compareNoCase(c.name, name) == 0)
		{
			return &c;
		}
	}
	return nullptr;
}

std::vector<std::string> LinearCampaignManager::stopLines()
{
	return {
		"[S-1360] the campaign between missions: the auto-save after a won mission (RW 0x6DE8F1) and the saves of CampaignMenu.apt are OpenBFME saves of "
		"the campaign's state (the campaign menu save point), not retail's GameState snapshot; the carry-over heroes are not carried (S-4903)",
		"[S-1364] the campaign presentation: MillisecondsAfterStartToStartFadeUp is not applied (its reader is not decoded); lane CAMP-2: the movies (OverallCampaignIntroMovie, "
		"IntroMovie, PLAY_MOVIE_IN_GAME) show their VP6 picture with their narration (S-2340)",
		"[S-4900] the campaign menu's Save / Load: CampaignMenu.apt is pushed again once SaveLoad.apt has left the stack (INFERENCE: retail's refocus of the "
		"covered menu was not read)",
		"[S-4901] the campaign's state after the game (RW 0x5EAB0A: ready, a won mission's index moved on) is set when the game is cleared for the score "
		"screen (INFERENCE: its caller, the manager's subsystem slot 9, was not located)",
		"[S-4902] the score screen of a campaign game lists the local player only (the port's campaign start builds no game slots), and its music plays on "
		"under CampaignMenu.apt (RW 0x925699 restarts the shell's music for the types 2 / 3 only)",
		"[S-4903] the campaign's carry-over units (RW 0x5EBAEE's record of the CarryoverUnit objects of a won mission, their spawn in the next mission) "
		"are not ported: a mission starts with the map's own units",
		"[S-1711] the military caption (SHOW_MILITARY_CAPTION) is typed as retail types it (RW 0x69CE5F, lane CAMPUI-1) with InGameUI.ini's MilitaryCaption* "
		"settings and language.ini's caption font; the incoming character's slide (RW 0x69B7BE's + 0x28 string) is not drawn",
	};
}

// ---- CampaignProgress ----------------------------------------------------------------------------------------------------------------------------------------------

int CampaignProgress::difficultyFromCommand(const std::string &argument)
{
	const char c = argument.empty() ? '\0' : argument[0];
	return c == 'E' ? 0 : (c == 'H' ? 2 : 1); // RW 0x91C108
}

const LinearCampaignMission *CampaignProgress::current(const LinearCampaignManager &m) const
{
	const LinearCampaign *c = m.find(campaign);
	if (!c || mission < 0 || mission >= (int)c->missions.size())
	{
		return nullptr;
	}
	return &c->missions[(size_t)mission];
}

bool CampaignProgress::isFinalMission(const LinearCampaignManager &m) const
{
	// RW 0x5EAE75 (no campaign: true) -> RW 0x5EAB6E: the mission after the current one ((+ 0xD3 ? mission : mission + 1), RW 0x5EAADF's bounds check)
	const LinearCampaign *c = m.find(campaign);
	const int next = advanced ? mission : mission + 1;
	return !c || next < 0 || next >= (int)c->missions.size();
}

bool CampaignProgress::advance(const LinearCampaignManager &m)
{
	if (isFinalMission(m))
	{
		return false;
	}
	++mission;
	victorious = false;
	return true;
}

void CampaignProgress::missionStarted()
{
	// RW 0x5EAB8F (decomp LinearCampaignMissionStart.cpp: flagC0 / C1 / C3 cleared, then C2)
	victorious = false;
	ready = false;
	advanced = false;
}

void CampaignProgress::gameOver()
{
	// RW 0x5EAB0A: + 0xD2 = 1; won: + 0xC + 1 once (+ 0xD3)
	ready = true;
	if (victorious && !advanced)
	{
		++mission;
		advanced = true;
	}
}

CampaignProgress::AfterScore CampaignProgress::afterScore(const LinearCampaignManager &m) const
{
	// RW 0x927EC6: RW 0x5EAE75 (the campaign is over) and RW 0x5EA8CD (+ 0xD0) -> the unlock; else + 0xD0 -> the auto-save (RW 0x6DE8F1); then the menu
	if (isFinalMission(m) && victorious)
	{
		return AfterScore::BonusUnlock;
	}
	return victorious ? AfterScore::AutoSaveMenu : AfterScore::Menu;
}

std::string CampaignProgress::menuSaveRefusal(const LinearCampaignManager &m) const
{
	const LinearCampaign *c = m.find(campaign);
	if (!c)
	{
		return "the game data has no campaign '" + campaign + "'";
	}
	if (difficulty < 0 || difficulty > 2)
	{
		return "the save's difficulty " + std::to_string(difficulty) + " is not 0 .. 2";
	}
	if (advanced && !ready)
	{
		return "the save's campaign state is not one a game reaches (the mission index moved on before the game was over)";
	}
	if (mission < 0 || mission > (int)c->missions.size())
	{
		return "the campaign " + c->name + " has no mission " + std::to_string(mission) + " (it has " + std::to_string(c->missions.size()) + ")";
	}
	if (menuMissionIndex(m) < 0)
	{
		const int shown = mission + ((!advanced && victorious) ? 1 : 0);
		return "the campaign " + c->name + " has no mission " + std::to_string(shown) + " for its menu to name (it has " + std::to_string(c->missions.size()) + ")";
	}
	return std::string();
}

int CampaignProgress::menuMissionIndex(const LinearCampaignManager &m) const
{
	// RW 0x5EADDE: + 0xC + ((+ 0xD3 == 0 and + 0xD0) ? 1 : 0), looked up by RW 0x5EAADF (null past the list: the menu shows nothing)
	const int index = mission + ((!advanced && victorious) ? 1 : 0);
	const LinearCampaign *c = m.find(campaign);
	return (c && index >= 0 && index < (int)c->missions.size()) ? index : -1;
}

bool CampaignProgress::nextMission(bool *introMovie)
{
	// RW 0x5EAE37: nothing unless + 0xD2; + 0xC + 1 unless + 0xD3 (then + 0xD3 = 1); RW 0x5EAB8F(1, 1)
	if (!ready)
	{
		return false;
	}
	if (!advanced)
	{
		++mission;
		advanced = true;
	}
	if (introMovie)
	{
		*introMovie = true;
	}
	return true;
}

void CampaignProgress::lastMission(bool *introMovie)
{
	// RW 0x5EC5D1: + 0xD0 or + 0xD3 -> RW 0x5EADC3 (+ 0xC + 1 unless + 0xD3, RW 0x5EAB8F(0, 1)); else the carry-over lists restored and RW 0x5EAB8F(1, 0)
	if (victorious || advanced)
	{
		if (!advanced)
		{
			++mission;
			advanced = true;
		}
		if (introMovie)
		{
			*introMovie = true;
		}
		return;
	}
	if (introMovie)
	{
		*introMovie = false;
	}
}

std::string CampaignProgress::serialize() const
{
	std::ostringstream s;
	s << "campaign=" << campaign << "\nmission=" << mission << "\ndifficulty=" << difficulty << "\nvictorious=" << (victorious ? 1 : 0) << "\n";
	return s.str();
}

bool CampaignProgress::parse(const std::string &text, CampaignProgress &out, std::string *error)
{
	CampaignProgress p;
	bool seen[4] = { false, false, false, false };
	std::istringstream in(text);
	std::string line;
	auto fail = [&](const std::string &why) {
		if (error)
		{
			*error = "campaign progress: " + why;
		}
		return false;
	};
	auto number = [](const std::string &v, int &n) {
		if (v.empty() || v.size() > 9 || v.find_first_not_of("0123456789") != std::string::npos)
		{
			return false;
		}
		n = std::stoi(v);
		return true;
	};
	while (std::getline(in, line))
	{
		if (line.empty())
		{
			continue;
		}
		const size_t eq = line.find('=');
		if (eq == std::string::npos)
		{
			return fail("line without '=': " + line);
		}
		const std::string k = line.substr(0, eq), v = line.substr(eq + 1);
		int n = 0;
		if (k == "campaign" && !seen[0])
		{
			p.campaign = v;
			seen[0] = true;
		}
		else if (k == "mission" && !seen[1] && number(v, n))
		{
			p.mission = n;
			seen[1] = true;
		}
		else if (k == "difficulty" && !seen[2] && number(v, n) && n <= 2)
		{
			p.difficulty = n;
			seen[2] = true;
		}
		else if (k == "victorious" && !seen[3] && (v == "0" || v == "1"))
		{
			p.victorious = v == "1";
			seen[3] = true;
		}
		else
		{
			return fail("bad or repeated line: " + line);
		}
	}
	if (!(seen[0] && seen[1] && seen[2] && seen[3]) || p.campaign.empty())
	{
		return fail("a field is missing");
	}
	out = p;
	return true;
}
