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
//   * MainMenuContinueCampaign (RW 0x91BBBB) answers whether "00000000.sav" exists in the Save folder (RW 0x6DDE0F -> RW 0x6DD85E); RotWK's
//     MainMenu.apt never asks it (no ContinueCampaign button, no GetExtern of it: the single player page holds Skirmish, War of the Ring,
//     BonusCampaign, Expansion1Campaign and Load Game), so a campaign is continued from Load Game;
// lane CAMPSAVE-1 (RotWK game.dat; BFME2 decomp LinearCampaign*.cpp / Rva001EB*.cpp tier B same-shape for the campaign's flags, which sit 0x10 higher
// in RotWK: + 0xD0 won, + 0xD1, + 0xD2 ready, + 0xD3 advanced):
//   * the mission start (RW 0x5EAB8F, decomp LinearCampaignMissionStart.cpp): + 0xD0 / + 0xD1 / + 0xD3 and + 0xD2 cleared, the mission's map loaded with
//     the campaign's difficulty; its second argument 1 plays the mission's IntroMovie (+ 0x8), 0 does not;
//   * a won mission (RW 0x5EBAEE, the decomp's onCurrentMissionWon): + 0xD0 = 1 (and the carry-over units, S-4903);
//   * after the game (RW 0x5EAB0A, the manager's subsystem slot 9, RW 0xBF53CC; INFERENCE: run when the game is cleared, before the score screen):
//     + 0xD2 = 1; when won and not yet advanced, the mission index + 1 and + 0xD3 = 1; the next mission's map becomes the GameState's map name
//     (RW 0x5EAA52), which names the auto-save;
//   * the score screen's end (RW 0x926E0A, the TimeLine screen's destructor, with its game type + 0x284 == 1) runs RW 0x927EC6: the bonus unlock
//     above when the campaign is over (RW 0x5EAE75 -> RW 0x5EAB6E: (+ 0xD3 ? mission : mission + 1) past the list) and won, else the auto-save of a
//     won mission (TheGameState RW 0x6DE8F1: the file named GUI:AutoSaveName ("Auto Save for %ls") with the map's display name + the mode's
//     extension, the description "__AUTO#SAVE__", SaveGameInfo + 0x28 IsAutoSaveOrNot = 1) and CampaignMenu.apt pushed (won or lost);
//   * CampaignMenu.apt (AptCampaignMenu, ctor RW 0x927B9F): the mission it shows is RW 0x5EADDE's (fillCampaignMenuContent): mission + (won and not
//     advanced); "CampaignName:" + its name -> TheGameText -> the Apt text APT:CampaignMissionName (RW 0x624FFD), its LoadScreenImage -> the image
//     record AptCampaign::LoadImage (RW 0x6236F6), the won flag -> the extern CampaignMenu:Victorious (RW 0x927A45: "1" / "0"). Its commands:
//     OnBttnMainMenu (RW 0x927A88), OnBttnSaveGame (RW 0x927A93: AptSaveLoad::OpenScreen(3, 1, 0)), OnBttnLoadGame (RW 0x927AA4: (2, 1, 0)),
//     OnBttnLastMission (RW 0x927AB5 -> RW 0x5EC5D1 restartCurrentCampaignMission: won or advanced -> RW 0x5EADC3, else the same mission again
//     without its IntroMovie), OnBttnNextMission (RW 0x927ACF: message 0x21 -> GameLogic dispatch -> RW 0x5EAE37: only when + 0xD2, the mission
//     index + 1 unless advanced, the start with the IntroMovie); each closes the menu (RW 0x927A07);
//   * the movie: won -> "Next Mission:" (APT:CampaignNextMission), the buttons MainMenu / SaveGame / LoadGame / NextMission; lost -> "Failed Mission:"
//     (APT:CampaignFailedMission), MainMenu / LoadGame (moved to x 512) / LastMission (APT:ReplayMissionBttn).
// OpenBFME DIFFERENCES (stop S-1360): the auto-save and the menu's saves are OpenBFME saves of the campaign's state between missions
// (SaveGameFile's campaign menu save point), never a retail format; the carry-over heroes between missions are not ported (S-4903; lane BFME2CAMP1 ported the first mission's spawn,
// GameLogic/CampaignCarryover.h).
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
	// the campaign flow's stops (S-1360 progress / CampaignMenu, S-1364 the bonus unlock and the fade-up)
	static std::vector<std::string> stopLines();

private:
	std::vector<LinearCampaign> m_campaigns;
};

// the manager the LinearCampaign block fills (RetailObjectWorld installs its own); nullptr when none
extern thread_local LinearCampaignManager *TheLinearCampaignManager;

// The campaign being played (TheLinearCampaignManager + 0x10's state): the campaign, the current mission, the difficulty, its flags.
struct CampaignProgress
{
	std::string campaign;     ///< the LinearCampaign's name (empty: no campaign)
	int mission = 0;          ///< index into its missions (+ 0xC)
	int difficulty = 1;       ///< 0 easy, 1 normal, 2 hard (RW 0x91C108)
	bool victorious = false;  ///< + 0xD0: the current mission was won (RW 0x5EBAEE)
	bool ready = false;       ///< + 0xD2: the game is over, the next mission may start (RW 0x5EAB0A)
	bool advanced = false;    ///< + 0xD3: the index already moved past the won mission (RW 0x5EAB0A / 0x5EADC3 / 0x5EAE37)

	static int difficultyFromCommand(const std::string &argument); ///< RW 0x91C108: 'E' -> 0, 'H' -> 2, else 1
	// the mission to play now (nullptr: past the end, or no campaign / an unknown one)
	const LinearCampaignMission *current(const LinearCampaignManager &m) const;
	// RW 0x5EAB6E: no mission after the current one ((advanced ? mission : mission + 1) past the list); true without a known campaign (RW 0x5EAE75)
	bool isFinalMission(const LinearCampaignManager &m) const;
	// after a won mission: the next one (false when the campaign is over: RW 0x927EC6's bonus unlock branch)
	bool advance(const LinearCampaignManager &m);

	// lane CAMPSAVE-1: the flags of the mission start (RW 0x5EAB8F): a mission being played has none
	void missionStarted();
	// RW 0x5EAB0A: the game is over (ready); a won mission moves the index on once
	void gameOver();
	// what RW 0x927EC6 does at the end of the score screen
	enum class AfterScore
	{
		BonusUnlock,  ///< the last mission won: the preference BCU = "true", TheGlobalData + 0xAF5, the main menu (no auto-save, no campaign menu)
		AutoSaveMenu, ///< a won mission: the auto-save, then CampaignMenu.apt
		Menu          ///< a lost one: CampaignMenu.apt
	};
	AfterScore afterScore(const LinearCampaignManager &m) const;
	// lane CAMPSAVE-1 (review): why a campaign menu save of this state cannot be shown by these campaigns ("" when it can): an unknown campaign, a mission
	// index outside its list or one the menu names no mission for, a difficulty outside 0 .. 2, or a state no game reaches (the index moved on before
	// the game was over: advanced without ready)
	std::string menuSaveRefusal(const LinearCampaignManager &m) const;
	// RW 0x5EADDE: the index of the mission CampaignMenu.apt names (mission + (won and not advanced)); -1 when that mission does not exist
	int menuMissionIndex(const LinearCampaignManager &m) const;
	// OnBttnNextMission (message 0x21 -> RW 0x5EAE37): false (nothing starts) unless ready; else the index moves on unless advanced and the mission
	// starts with its IntroMovie (*introMovie = true)
	bool nextMission(bool *introMovie);
	// OnBttnLastMission (RW 0x5EC5D1): won or advanced -> RW 0x5EADC3 (the index moves on unless advanced; started with the IntroMovie), else the same
	// mission again without it
	void lastMission(bool *introMovie);

	// the sidecar text form of lanes CAMP-1 .. SAVE-4 ("campaign=..", "mission=..", "difficulty=..", "victorious=..", one per line) and its strict reader
	// (false + *error on anything else: no silent default). Lane CAMPSAVE-1: no build writes it any more (the campaign state is in the saves); an old
	// sidecar is read once and migrated into the auto-save
	std::string serialize() const;
	static bool parse(const std::string &text, CampaignProgress &out, std::string *error);
};
