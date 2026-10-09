// OpenBFME. GPL-3.0.
//
// Lane CAMP-1: TheLinearCampaignManager (RW 0xDE36CC, created at RW 0x63BE2D with no INI file of its own): the LinearCampaign blocks of data\ini\*.ini
// and the state of the campaign being played (its current mission, the difficulty, the victory of the last mission).
//
// TARGET FACTS (rotwk201_game.exe):
//   * block LinearCampaign <name> (RW 0x5ECCB9): only in the main INI files - another load type (map.ini, INI_LOAD_CREATE_OVERRIDES) throws code 8
//     "Sorry, you cannot define a 'LinearCampaign' block anywhere but the main INI files." (RW 0xBF53F0); the campaign (ctor RW 0x5EC7E2) is appended
//     to the manager's list (RW 0x5ECC82; no duplicate check) and its fields parsed (RW 0x5ECA71) with the table at RW 0xBF5354:
//       CampaignDisplayNameLabel  parseAsciiString            (+0x4)
//       OverallCampaignIntroMovie parseAsciiString            (+0x8)
//       CarryoverUnit             parseAsciiStringVectorAppend (+0xC, RW 0x42E59E)
//       Mission                   RW 0x5EC9FE                  (+0x18)
//   * Mission <name> (RW 0x5EC9FE): a mission record (0x24 bytes, ctor RW 0x5EBA4A) appended to the campaign's list (RW 0x5EC9BE), its fields parsed
//     (RW 0x5EAA86) with the table at RW 0xBF5268:
//       Map                                 parseAsciiString             (+0x4)
//       IntroMovie                          parseAsciiString             (+0x8)
//       LoadScreenImage                     parseAsciiString             (+0xC)
//       LoadScreenMusicTrack                RW 0x73B217 (an audio event; "NoSound" clears, an unknown name throws "Invalid Sound") (+0x10)
//       MillisecondsAfterStartToStartFadeUp parseDurationUnsignedInt (RW 0x73A429: logic frames) (+0x14; the ctor's default is 7, RW 0x5EBA4A:
//                                           [0xBFD298] + 1 = 6 + 1)
//       DelayCarryoverSpawningOf            parseAsciiStringVectorAppend (+0x18)
//     then a mission without a Map throws code 3 "Campaign missions must have a Map. %s does not" (RW 0xBF5238);
//   * the campaign lookup by name is case-insensitive (RW 0x5EB010: compareNoCase, -1 when absent);
//   * the main menu's Expansion1Campaign (RW 0x91AF8C) / BonusCampaign (RW 0x91AFAF) store the command's first character and state 0xD / 0xE; the menu
//     update (RW 0x91C349, table RW 0x91C70B) starts ANGMAR_CAMPAIGN (ANGMAR_CAMPAIGN_DEMO when [TheGlobalData-like 0xDE4324] + 0x60 is set, RW 0x91BE64)
//     or ANGMAR_BONUS_CAMPAIGN (RW 0x91BEFA). The character is the difficulty (RW 0x91C108): 'E' easy (0), 'H' hard (2), anything else normal (1).
//     GOOD_CAMPAIGN / EVIL_CAMPAIGN have handlers (RW 0x91AF46 / 0x91AF69, states 0xB / 0xC) that RotWK's main menu never registers (no reference to
//     either address): RotWK's menu exposes only the Angmar campaign and its bonus mission;
//   * after a campaign game (RW 0x927EC6): when the campaign has no next mission (RW 0x5EAB6E: mission + 1, or the same mission when + 0xD3 is set,
//     is past the list) and the last mission was won (+ 0xD0, RW 0x5EA8CD), the preference "BCU" is set to "true" (OptionPreferences, RW 0x7B274C),
//     TheGlobalData + 0xAF5 = 1 (the main menu's MainMenuUnlockBonusCampaign, RW 0x91BBAD) and the shell returns to the main menu (RW 0x75E43C);
//     otherwise a won mission is auto-saved ("00000000.sav", "__AUTO_SAVE__", RW 0x6DE8F1) and CampaignMenu.apt is pushed (RW 0x75E3B7);
//   * MainMenuContinueCampaign (RW 0x91BBBB) answers whether "00000000.sav" exists (RW 0x6DDE0F).
// NOT PORTED (stop S-1360): the campaign auto-save itself (RW 0x6DE8F1: a retail save game) - the progress is kept in a sidecar text file of this
// engine (CampaignProgress::serialize), never in a retail format; the carry-over heroes between missions (S-1180).
#pragma once

#include <string>
#include <vector>

class INI;
class INIBlockRegistry;

struct LinearCampaignMission
{
	std::string name;                         ///< +0x0 (the block's token)
	std::string map;                          ///< +0x4 Map
	std::string introMovie;                   ///< +0x8 IntroMovie
	std::string loadScreenImage;              ///< +0xC LoadScreenImage
	std::string loadScreenMusicTrack;         ///< +0x10 LoadScreenMusicTrack (an audio event; empty for NoSound)
	unsigned fadeUpFrames = 7;                ///< +0x14 MillisecondsAfterStartToStartFadeUp (logic frames, RW 0x73A429); default RW 0x5EBA4A
	std::vector<std::string> delayCarryover;  ///< +0x18 DelayCarryoverSpawningOf
};

struct LinearCampaign
{
	std::string name;
	std::string displayNameLabel;             ///< +0x4 CampaignDisplayNameLabel
	std::string overallIntroMovie;            ///< +0x8 OverallCampaignIntroMovie
	std::vector<std::string> carryoverUnits;  ///< +0xC CarryoverUnit
	std::vector<LinearCampaignMission> missions; ///< +0x18 Mission, in file order
};

class LinearCampaignManager
{
public:
	void registerBlock(INIBlockRegistry &registry);
	void parseLinearCampaign(INI *ini); ///< RW 0x5ECCB9
	const LinearCampaign *find(const std::string &name) const; ///< RW 0x5EB010 (case-insensitive)
	const std::vector<LinearCampaign> &campaigns() const { return m_campaigns; }
	// the campaign flow's stops (S-1360 progress / CampaignMenu, S-1364 movies and the load screen image)
	static std::vector<std::string> stopLines();

private:
	std::vector<LinearCampaign> m_campaigns;
};

// the manager the LinearCampaign block fills (RetailObjectWorld installs its own); nullptr when none
extern thread_local LinearCampaignManager *TheLinearCampaignManager;

// The campaign being played (TheLinearCampaignManager + 0x10's state): the campaign, the current mission, the difficulty, whether that mission was won.
struct CampaignProgress
{
	std::string campaign;     ///< the LinearCampaign's name (empty: no campaign)
	int mission = 0;          ///< index into its missions (+ 0xC)
	int difficulty = 1;       ///< 0 easy, 1 normal, 2 hard (RW 0x91C108)
	bool victorious = false;  ///< + 0xD0: the current mission was won

	static int difficultyFromCommand(const std::string &argument); ///< RW 0x91C108: 'E' -> 0, 'H' -> 2, else 1
	// the mission to play now (nullptr: past the end, or no campaign / an unknown one)
	const LinearCampaignMission *current(const LinearCampaignManager &m) const;
	// RW 0x5EAB6E: no mission after the current one
	bool isFinalMission(const LinearCampaignManager &m) const;
	// after a won mission: the next one (false when the campaign is over: RW 0x927EC6's bonus unlock branch)
	bool advance(const LinearCampaignManager &m);
	// the sidecar text form ("campaign=..", "mission=..", "difficulty=..", "victorious=..", one per line) and its strict reader (false + *error on
	// anything else: no silent default)
	std::string serialize() const;
	static bool parse(const std::string &text, CampaignProgress &out, std::string *error);
};
