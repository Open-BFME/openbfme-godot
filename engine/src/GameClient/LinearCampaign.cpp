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
		"[S-1360] the campaign progress: retail auto-saves a won mission as \"00000000.sav\" (\"__AUTO_SAVE__\", RW 0x6DE8F1) and shows CampaignMenu.apt (RW 0x75E3B7: "
		"next / last mission, save, load, main menu); the port keeps CampaignProgress in a sidecar text file of its own and loads the next mission at once",
		"[S-1364] the campaign presentation: LoadScreen.apt does not show the mission's LoadScreenImage, the bonus unlock writes no preference file (\"BCU\", "
		"RW 0x927F08), MillisecondsAfterStartToStartFadeUp is not applied (its reader is not decoded); the movies (OverallCampaignIntroMovie, IntroMovie, "
		"PLAY_MOVIE_IN_GAME) play their narration over black (lane CAMP-1H: S-1710)",
		"[S-1711] the military caption (SHOW_MILITARY_CAPTION, the campaign's subtitles) is drawn whole in the client's own style: retail types it (RW "
		"0x69CE5F: a character per InGameUI MilitaryCaptionDelayMS on the real-time clock, MiscAudio MissionBriefingCharacterClick per character, up to 4 "
		"lines, held 2 s after the last character, then faded) with InGameUI.ini's MilitaryCaption* colour / position / font, which the port does not parse",
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
	const LinearCampaign *c = m.find(campaign);
	return !c || mission + 1 >= (int)c->missions.size();
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
