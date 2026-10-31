// OpenBFME. GPL-3.0.
//
// CampaignMenu.apt (CodePrefix AptCampaignMenu, lane CAMPSAVE-1): the campaign's screen between missions, pushed by RW 0x927EC6 when the score screen of a
// campaign game ends (GameClient/LinearCampaign.h has the campaign's side).
//
// TARGET FACTS (RotWK game.dat, caveat S-001; the BFME2 decomp has no tier A counterpart of the class, AptCampaignMenu.cpp only its destructor):
//   * the constructor (RW 0x927B9F) runs only for the first instance (RW 0xDEA394 is the singleton) and registers, in this order, the commands
//     AptCampaignMenu::OnBttnMainMenu (RW 0x927A88), OnBttnSaveGame (RW 0x927A93), OnBttnLoadGame (RW 0x927AA4), OnBttnLastMission (RW 0x927AB5),
//     OnBttnNextMission (RW 0x927ACF) and the extern CampaignMenu:Victorious (RW 0x927A45, string table RW 0xDB6B7C); then it asks the campaign for
//     the menu's content (TheLinearCampaignManager RW 0x5EAE84 -> RW 0x5EADDE: the mission's name, its LoadScreenImage, the won flag, into + 0x27C /
//     + 0x280 / + 0x284), sets the Apt text APT:CampaignMissionName (RW 0x624FFD) to TheGameText("CampaignName:" + the name) and the image record
//     AptCampaign::LoadImage (RW 0x6236F6) to the image;
//   * CampaignMenu:Victorious (RW 0x927A45): the value starts "0"; read with index 0 it is "1" when + 0x284 is set (the data string RW 0xBD5D90);
//   * OnBttnMainMenu: RW 0x927A07(1); OnBttnSaveGame: AptSaveLoad::OpenScreen(3, 1, 0) (RW 0x816655: the save page, campaign saves, not over a game);
//     OnBttnLoadGame: OpenScreen(2, 1, 0); OnBttnLastMission: TheLinearCampaignManager RW 0x5EC62F (restartCurrentCampaignMission, RW 0x5EC5D1),
//     then RW 0x927A07(0); OnBttnNextMission: TheMessageStream's message 0x21 (the logic dispatch's RW 0x5EAE95 -> RW 0x5EAE37), then RW 0x927A07(0);
//   * RW 0x927A07(mainMenu): with the menu up, TheShell + 0x54 = 1 when not mainMenu, then TheGameEngine RW 0x62215B (+ 0x310: the engine's next
//     update pops the shell's top screen, RW 0x75DB34);
//   * the movie (CampaignMenu.apt, sprite 21 / 16 / 18): every button's DoButtonCallBack disables the others, runs its command and plays _close; won ->
//     LastMission hidden, Text0 = $APT:CampaignNextMission, the subtitle $EndVictorious; lost -> SaveGame and NextMission hidden, LoadGame at x 512,
//     Text0 = $APT:CampaignFailedMission, the subtitle $EndDefeat; Text1 = $CampaignMissionName; the picture is the RenderImage clip with
//     _imageMap "AptCampaign::LoadImage"; outside a game (no _global.InGame) the movie sets CampaignMenu:Victorious itself.
// OpenBFME: the commands are shell requests the host acts on (the campaign's state is the host's, scripts/campaign_flow.gd); the host gives the
// menu's content (CampaignMenuContext) before it pushes the screen.

#pragma once

#include "GameClient/GUI/AptScreen.h"

#include <string>
#include <vector>

struct ShellEnvironment;

// what RW 0x5EADDE gives the menu (+ 0x27C / + 0x280 / + 0x284)
struct CampaignMenuContext
{
	bool haveMission = false;     ///< RW 0x5EADDE answered true (a campaign with that mission)
	std::string missionName;      ///< the Mission block's name (record + 0x0): "Foundation", ...
	std::string loadScreenImage;  ///< its LoadScreenImage (record + 0xC)
	bool victorious = false;      ///< the campaign's + 0xD0
};

class AptCampaignMenu : public AptScreen
{
public:
	AptCampaignMenu(WindowManager &windows, Shell &shell, ShellEnvironment &environment);
	~AptCampaignMenu() override;

	// the names the constructor registers, in its order (tests)
	static const std::vector<std::string> &retailNames();
	// the extern's answer (RW 0x927A45)
	std::string victorious() const;
	// the text and image the constructor set (tests): APT:CampaignMissionName, AptCampaign::LoadImage
	const std::string &missionText() const { return m_missionText; }
	const std::string &image() const { return m_image; }
	const std::vector<std::string> &requests() const { return m_requests; }

private:
	void post(int action, const std::string &name);

	ShellEnvironment &m_env;
	CampaignMenuContext m_content;
	std::string m_missionText;
	std::string m_image;
	std::vector<std::string> m_requests;
};
