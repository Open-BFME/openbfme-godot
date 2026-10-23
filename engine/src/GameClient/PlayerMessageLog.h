// OpenBFME. GPL-3.0.
#pragma once
#include "GameClient/GlobalLanguage.h"
#include "Common/INIDataTypes.h"
#include <algorithm>
#include <cstdint>
#include <deque>
#include <string>

// DONOR: BFME2 InGameUIMessages.cpp addMessageText (rva0029BA58),
// InGameUIPostDraw.cpp postDraw (rva002A0161), InGameUIUpdate.cpp update.
// Six entries, newest at index zero; colors alternate against the preceding
// entry's complete ARGB value. Fade subtracts age * .01 each client update.
// The unusual integer timeout division is preserved, not replaced by a timer.
// Presentation only: the host supplies the current logic frame; no RNG/orders.
struct PlayerMessageSettings
{
	std::uint32_t color1 = 0xffffffff, color2 = 0xffb4b4b4;
	ICoord2D position{10, 10};
	std::string font = "Arial";
	int pointSize = 10;
	bool bold = false;
	int delayMs = 5000;
};
class PlayerMessageLog
{
public:
	struct Entry { std::u16string text; unsigned frame; std::uint32_t color; };
	PlayerMessageSettings settings;
	void add(const std::u16string &text, unsigned frame)
	{
		const auto color = m_entries.empty() || m_entries.front().color == settings.color2 ? settings.color1 : settings.color2;
		m_entries.push_front({text, frame, color});
		if (m_entries.size() == 7) m_entries.pop_back();
	}
	void advanceClientTicks(unsigned frame, unsigned ticks)
	{
		for (unsigned i = 0; i < ticks; ++i) update(frame);
	}
	void update(unsigned frame, unsigned logicFps = 5)
	{
		const int timeout = settings.delayMs / (int)logicFps / 1000;
		for (auto &entry : m_entries)
		{
			const unsigned age = frame - entry.frame;
			if (age > (unsigned)timeout)
			{
				const int alpha = std::max(0, (int)(entry.color >> 24) - (int)(age * 0.01f));
				entry.color = (entry.color & 0xffffff) | ((std::uint32_t)alpha << 24);
			}
		}
		m_entries.erase(std::remove_if(m_entries.begin(), m_entries.end(), [](const Entry &e) { return !(e.color >> 24); }), m_entries.end());
	}
	const std::deque<Entry> &entries() const { return m_entries; }
private:
	std::deque<Entry> m_entries;
};
