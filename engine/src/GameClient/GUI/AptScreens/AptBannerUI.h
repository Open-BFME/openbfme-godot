// OpenBFME. GPL-3.0.
//
// BannerUI.apt (CodePrefix AptBannerUI, WOTR2 milestone 7 step 5): a War of the Ring battle's reinforcement banners (TheBannerUI; BFME2 decomp
// GameClient/GUI/InGame/Tactical/BannerUI.cpp and BannerUIUpdate.cpp, tier B).
//
// TARGET FACTS (RotWK game.dat, caveat S-001):
//   * the window: "Apt\\", "BannerUI.apt" through the Apt window manager (BannerUI::init); the commands AptBannerUI::OnInitialized (RW 0x616DA6) and
//     AptBannerUI::OnBttnBanner (its argument the slot, atoi; OnBannerButton);
//   * OnInitialized, in a War of the Ring battle (+ 0xB4, the map not up RW 0x441E4A) whose local player is not out (TheVictoryConditions vslot 0x40): a
//     banner for each of the local player's battle armies that is a reinforcement (RW 0x7F7219 / 0x7F560C), at most 4 slots (CreateBanner RW 0x616AF0):
//     its BannerType (the army's banner name, else BannerMen; bannerui.ini, the field table RW 0xBFBD68: FlagObj + 0, GlowObj + 4, Icon + 8) ->
//     AddBanner(slot, FlagObj, GlowObj), then the text APT:BannerTimer<slot> empty; then SetBannerXOffset per slot whose offset is not 0;
//   * the update RW 0x6164D7: the army's summary state mapped 0 -> 0, 2 -> 1, 3 -> 2, else 3; a changed state -> SetBannerState(slot, RW 0x615C01:
//     "_available" / "_engaging" / "_waiting" / "_disabled"); in state 1 the progress (RW 0x80FED2 elapsed / (arrival - start), x 100, int) ->
//     SetBannerProgress(slot, p) when it changed, and APT:BannerTimer<slot> = "%d:%02d" of the remaining frames / 5 (RW 0x80FF04);
//   * OnBannerButton: banner state 0 -> MSG_SUMMON_REINFORCEMENTS (0x45E, the army); state 2 -> the script notification with the army's name, then
//     MSG_CALL_IN_REINFORCEMENTS (0x45F, the army), the banner marked handled (once).
// Not ported (S-1329): the over-button handlers' summary tooltip (BANNERUI:SummaryTitle on the control bar), the script notification, the X offsets
// (none set by RotWK's data path that was read).

#pragma once

#include "GameClient/GUI/AptScreen.h"

#include <string>
#include <vector>

class AptBannerUI : public AptScreen
{
public:
	struct Banner
	{
		int armyId = 0;
		std::string flagObj, glowObj; // the BannerType's FlagObj / GlowObj
	};
	AptBannerUI(WindowManager &windows, Shell &shell, std::vector<Banner> banners);

	// RW 0x6164D7 for one banner: the summary state (2 counting, 3 ready, 4 on the field), its progress (0 .. 1) and the remaining frames
	void update(int slot, int summaryState, float progress, unsigned remainingFrames);
	bool initialized() const { return m_initialized; }
	// the armies whose banners asked for a call-in since the last call (OnBannerButton in banner state 2, each once)
	std::vector<int> takeCallIns();
	static const char *stateString(int bannerState); // RW 0x615C01
	const std::vector<Banner> &banners() const { return m_banners; }

private:
	void onInitialized();
	void onButton(int slot);
	struct Slot
	{
		int state = -1;
		int progress = -1;
		bool handled = false; // + 0x18
		std::string timer;
	};
	std::vector<Banner> m_banners;
	std::vector<Slot> m_slots;
	std::vector<int> m_callIns;
	bool m_initialized = false;
};
