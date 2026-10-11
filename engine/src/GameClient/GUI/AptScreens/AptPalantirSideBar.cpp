// OpenBFME. GPL-3.0.
//
// Lane HUD-6: the side command bar's update (InGameSideCommandBar.apt, the builder's build list in its carved frame). See AptPalantir.h.
//
// TARGET FACTS (RotWK game.dat, caveat S-001; AptInGameSideCommandBar::Impl, BFME2 decomp reverse/attempts/0x005285ef.cpp is the donor for the class layout,
// tier B same-shape for RW 0x92F082):
//   * the update RW 0x92F27D: nothing before the movie loaded (+ 0x14 == 0); the context object (+ 0x1C) must exist, have the template KindOf DOZER
//     (+ 0x109 bit 0x40), be controlled by the local player (RW 0x68B678 == RW 0x6A8839) whose + 0x770 is 0, and UpdateButtonVisiblity must show at least one
//     button; then FadeIn when the bar is neither fading in nor shown (RW 0x92EDDC, states 2 / 3), else FadeOut when it is (RW 0x92EE1C). The content updaters
//     run only while the bar fades in or is shown (states 2 / 3, RW 0x92F316): a bar that fades out keeps what it showed;
//   * UpdateButtonVisiblity RW 0x92F082: the control bar's 33 windows in order (TheControlBar + 0xDC), each visible one whose CommandButton is Radial (+ 0x101)
//     takes the next frame (at most 15, stopping at the first frame the movie did not report); a frame that showed another window is hidden first
//     (RW 0x92F015 from it on); a new frame gets SetButtonState(<index>, "_show") on the movie (RW 0x92BDC5 with the "%d" of the index, strings RW 0xC7EF50 /
//     0xC19534) and its updater; afterwards every frame from the count on is hidden;
//   * HideButtons RW 0x92F015: from the last shown frame down to `from`: SetButtonState(<index>, "_hide") (RW 0xC1952C), its updater released, its window -1.
// The movie's SetButtonState shows the frame piece of that button (SideCommandBar01 .. 12.tga with the gold curls, UpdateFrameState / UpdateNeighborFrameStates
// pick _top / _middle / _bottom / _topbottom from the neighbours): without these calls the frame stays hidden and only the bare icons show.
// INFERENCE (S-2700): the local player's + 0x770 (unidentified) is taken as 0; the content of a frame whose window changed is updated in place (syncFrames) where
// retail releases the frame's updater and makes a new one.

#include "GameClient/GUI/AptScreens/AptPalantir.h"

void AptPalantir::sideBarLoaded(const std::string &arg)
{
	// RW 0x92EE5C: the argument (the movie's clip) is kept and the bar is loaded (state 1); RW 0x92ED9C (unloaded) forgets the frames
	m_sidePrefix = arg;
	if (m_sidePrefix.rfind("_level", 0) == 0) // the clip path without its "_level<N>." prefix (RW 0x815563, as the frames' names)
	{
		const size_t dot = m_sidePrefix.find('.');
		m_sidePrefix = dot == std::string::npos ? std::string() : m_sidePrefix.substr(dot + 1);
	}
	m_sideCount = 0;
	for (int &k : m_sideKey)
	{
		k = -1;
	}
}

void AptPalantir::hideSideButtons(int from)
{
	while (from < m_sideCount)
	{
		--m_sideCount;
		call(m_sidePrefix, "SetButtonState", { std::to_string(m_sideCount), "_hide" });
		if (m_sideBarCalls.size() >= 64)
		{
			m_sideBarCalls.erase(m_sideBarCalls.begin());
		}
		m_sideBarCalls.push_back(std::to_string(m_sideCount) + " _hide");
		m_sideKey[m_sideCount] = -1;
	}
}

bool AptPalantir::updateSideButtonVisibility(const std::vector<ControlBarButton> &buttons)
{
	int slotNum = 0;
	for (const ControlBarButton &b : buttons) // ControlBar::sideButtons: the visible Radial buttons in window order
	{
		if (slotNum > 14 || m_sideFramePath[slotNum].empty())
		{
			break; // RW 0x92F0A1 (15 frames) / RW 0x92F0E3 (a frame the movie has not reported)
		}
		if (slotNum < m_sideCount && m_sideKey[slotNum] != b.slot)
		{
			hideSideButtons(slotNum);
		}
		if (slotNum >= m_sideCount)
		{
			call(m_sidePrefix, "SetButtonState", { std::to_string(slotNum), "_show" });
			if (m_sideBarCalls.size() >= 64)
			{
				m_sideBarCalls.erase(m_sideBarCalls.begin());
			}
			m_sideBarCalls.push_back(std::to_string(slotNum) + " _show");
			m_sideKey[slotNum] = b.slot;
			m_sideCount = slotNum + 1;
		}
		++slotNum;
	}
	hideSideButtons(slotNum);
	return slotNum > 0;
}

void AptPalantir::syncSideBar(const ControlBar &bar)
{
	if (!m_sideBarLoaded)
	{
		return; // RW 0x92F283: + 0x14 == 0
	}
	const bool shown = m_sideBarGate && updateSideButtonVisibility(bar.sideButtons());
	if (shown != m_sideShown)
	{
		m_sideShown = shown;
		call(m_sidePrefix, shown ? "FadeIn" : "FadeOut", {});
	}
	if (m_sideShown)
	{
		syncFrames(bar.sideButtons(), false); // the updaters (RW 0x92F316): only while the bar fades in or is shown
	}
}

bool AptPalantir::contentSlotAt(const std::string &path, bool &arc, int &slot) const
{
	// lane HUD-6 (INFERENCE, S-2703): a clip inside "<level>.<frame>.content" belongs to the button that frame shows (syncFrames' keys)
	for (int pass = 0; pass < 2; ++pass)
	{
		const bool inArc = pass == 0;
		const int n = inArc ? kArcPositions : kSidePositions;
		for (int i = 0; i < n; ++i)
		{
			const std::string &frame = inArc ? m_arcFramePath[i] : m_sideFramePath[i];
			const Slot &s = inArc ? m_arc[i] : m_side[i];
			if (frame.empty() || !s.created || s.slot < 0)
			{
				continue;
			}
			const std::string key = levelPrefix() + frame + ".content";
			if (path == key || (path.size() > key.size() && path.compare(0, key.size(), key) == 0 && path[key.size()] == '.'))
			{
				arc = inArc;
				slot = s.slot;
				return true;
			}
		}
	}
	return false;
}
