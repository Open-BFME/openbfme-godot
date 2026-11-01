// OpenBFME. GPL-3.0.
// AptPalantir's flashing buttons (lane CAMPUI-1; see AptPalantir.h): the objectives flag of a new objective (RW 0x6D7647 / 0x80043E) and the scripts'
// button flashes (RW 0x6D455F / 0x6D72DF).

#include "GameClient/GUI/AptScreens/AptPalantir.h"

#include <cmath>

void AptPalantir::syncObjectiveFlash(const std::vector<bool> &shown, bool flashableGame, bool screenOpen)
{
	if (flashableGame && !m_objFlash)
	{
		for (size_t i = 0; i < shown.size(); ++i)
		{
			const bool was = i < m_objShown.size() && m_objShown[i]; // nothing remembered: hidden
			if (shown[i] != was)
			{
				if (!was && shown[i])
				{
					m_objFlash = true;
				}
				if (m_objShown.size() <= i)
				{
					m_objShown.resize(i + 1, false);
				}
				m_objShown[i] = shown[i];
			}
		}
	}
	else if (screenOpen)
	{
		m_objFlash = false;
	}
	if (m_objFlashSent != m_objFlash)
	{
		call("", "FlashObjectivesButton", { m_objFlash ? "_show" : "_hide" }); // RW 0x80043E
		m_objFlashSent = m_objFlash;
	}
}

void AptPalantir::flashButton(int slot, int seconds)
{
	if (slot < 0 || slot >= 3 || seconds < 0)
	{
		return;
	}
	const int frames = seconds * 30; // DAT_00D9F60C = 30
	if (frames > m_flashTimer[slot])
	{
		m_flashTimer[slot] = frames;
	}
}

void AptPalantir::tickFlashTimers(double seconds)
{
	static const char *const kNames[3] = { "PlayerMagic", "Objectives", "PlanningMode" }; // RW 0xC1953C
	m_flashClock += seconds * 30.0;
	const int frames = (int)std::floor(m_flashClock);
	m_flashClock -= frames;
	for (int slot = 0; slot < 3; ++slot)
	{
		// one count per client frame (RW 0x6D72DF runs once a frame; here the frames the clock covered since the last update)
		m_flashTimer[slot] = m_flashTimer[slot] > frames ? m_flashTimer[slot] - frames : 0;
		const bool on = m_flashTimer[slot] > 0;
		if (on != m_flashOn[slot])
		{
			call("", std::string("Set") + kNames[slot] + "ButtonFlashEffectState", { on ? "_show" : "_hide" }); // "Set%s%s" RW 0xC19510
			m_flashOn[slot] = on;
		}
	}
}
