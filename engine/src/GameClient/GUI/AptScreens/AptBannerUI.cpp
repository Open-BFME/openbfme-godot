// OpenBFME. GPL-3.0.
// See GameClient/GUI/AptScreens/AptBannerUI.h (WOTR2 milestone 7 step 5).

#include "GameClient/GUI/AptScreens/AptBannerUI.h"

#include "GameClient/GUI/WindowManager.h"
#include "Libraries/Source/Apt/AptValue.h"

#include <cstdio>
#include <cstdlib>

AptBannerUI::AptBannerUI(WindowManager &windows, Shell &shell, std::vector<Banner> banners)
	: AptScreen(windows, shell, "BannerUI.apt", "AptBannerUI"), m_banners(std::move(banners))
{
	if (m_banners.size() > 4)
	{
		m_banners.resize(4); // CreateBanner RW 0x616AF0: slots 0 .. 3
	}
	m_slots.resize(m_banners.size());
	registerCommand("AptBannerUI::OnInitialized", [this](const std::string &) { onInitialized(); });
	registerCommand("AptBannerUI::OnBttnBanner", [this](const std::string &argument) { onButton(std::atoi(argument.c_str())); });
}

const char *AptBannerUI::stateString(int bannerState)
{
	switch (bannerState)
	{
	case 0: return "_available";
	case 1: return "_engaging";
	case 2: return "_waiting";
	case 3: return "_disabled";
	default: return nullptr;
	}
}

void AptBannerUI::onInitialized()
{
	// RW 0x616DA6 -> CreateBanner RW 0x616AF0 per banner: AddBanner(slot, FlagObj, GlowObj), APT:BannerTimer<slot> = ""
	for (size_t slot = 0; slot < m_banners.size(); ++slot)
	{
		std::string error;
		if (!windows().invokeASAtValues(level(), "", "AddBanner",
				{ AptValue::integer((std::int32_t)slot), AptValue::string(m_banners[slot].flagObj), AptValue::string(m_banners[slot].glowObj) }, &error))
		{
			windows().note("invoke-failed", "BannerUI AddBanner: " + error);
		}
		windows().setAptText("APT:BannerTimer" + std::to_string(slot), "");
	}
	m_initialized = true;
}

void AptBannerUI::update(int slot, int summaryState, float progress, unsigned remainingFrames)
{
	if (!m_initialized || slot < 0 || slot >= (int)m_slots.size())
	{
		return;
	}
	Slot &s = m_slots[(size_t)slot];
	const int state = summaryState == 0 ? 0 : summaryState == 2 ? 1 : summaryState == 3 ? 2 : 3; // RW 0x6164D7
	std::string error;
	if (state != s.state)
	{
		if (!windows().invokeASAtValues(level(), "", "SetBannerState", { AptValue::integer(slot), AptValue::string(stateString(state)) }, &error))
		{
			windows().note("invoke-failed", "BannerUI SetBannerState: " + error);
		}
		s.state = state;
	}
	if (s.state == 0)
	{
		return;
	}
	const int p = s.state == 1 ? (int)(progress * 100.0f) : 0;
	if (p != s.progress)
	{
		if (!windows().invokeASAtValues(level(), "", "SetBannerProgress", { AptValue::integer(slot), AptValue::integer(p) }, &error))
		{
			windows().note("invoke-failed", "BannerUI SetBannerProgress: " + error);
		}
		s.progress = p;
	}
	std::string timer;
	if (s.state == 1)
	{
		const unsigned seconds = remainingFrames / 5u; // RW 0x80FF04 / LOGICFRAMES_PER_SECOND
		char buf[32];
		std::snprintf(buf, sizeof(buf), "%u:%02u", seconds / 60u, seconds % 60u);
		timer = buf;
	}
	if (timer != s.timer)
	{
		windows().setAptText("APT:BannerTimer" + std::to_string(slot), timer);
		s.timer = timer;
	}
}

void AptBannerUI::onButton(int slot)
{
	// OnBannerButton: banner state 2 (ready) -> MSG_CALL_IN_REINFORCEMENTS once; state 0 (MSG_SUMMON_REINFORCEMENTS) never shows in a War of the Ring battle
	if (slot < 0 || slot >= (int)m_slots.size() || m_slots[(size_t)slot].handled)
	{
		return;
	}
	if (m_slots[(size_t)slot].state == 2)
	{
		m_callIns.push_back(m_banners[(size_t)slot].armyId);
		m_slots[(size_t)slot].handled = true;
	}
}

std::vector<int> AptBannerUI::takeCallIns()
{
	std::vector<int> out;
	out.swap(m_callIns);
	return out;
}
