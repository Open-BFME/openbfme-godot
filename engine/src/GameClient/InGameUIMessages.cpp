// OpenBFME. GPL-3.0.
// See GameClient/InGameUIMessages.h (lane CHAT-1). The settings' INI rows are read with HelpBoxSettings' InGameUI loader (CommandButtonHelp.cpp).

#include "GameClient/InGameUIMessages.h"

void InGameUIMessages::addMessageText(const std::u16string &text, const std::uint32_t *rgb, std::uint32_t logicFrame)
{
	std::uint32_t color1 = m_settings.color1, color2 = m_settings.color2;
	if (rgb)
	{
		color1 = color2 = *rgb | 0xFF000000u;
	}
	for (int i = kMaxLines - 1; i >= 1; --i)
	{
		m_lines[(size_t)i] = m_lines[(size_t)i - 1];
	}
	Line &l = m_lines[0];
	l.text = text;
	l.timestamp = logicFrame;
	l.used = true;
	// the colour is compared with line 1's colour as it is now (faded or not), as retail and ZH do
	l.argb = !m_lines[1].used || m_lines[1].argb == color2 ? color1 : color2;
}

void InGameUIMessages::update(std::uint32_t logicFrame)
{
	const int timeout = m_settings.delayMS / 5 / 1000; // LOGICFRAMES_PER_SECOND (RW 0xD9F608 = 5), ZH's order of the divisions
	for (int i = kMaxLines - 1; i >= 0; --i)
	{
		Line &l = m_lines[(size_t)i];
		if (!l.used || logicFrame - l.timestamp <= (std::uint32_t)timeout)
		{
			continue;
		}
		int a = (int)(l.argb >> 24);
		const int amount = (int)((float)(logicFrame - l.timestamp) * 0.01f);
		a = a - amount < 0 ? 0 : a - amount;
		l.argb = (l.argb & 0x00FFFFFFu) | ((std::uint32_t)a << 24);
		if (a == 0)
		{
			l = Line(); // removeMessageAtIndex: the slot is emptied, the others stay where they are
		}
	}
}

int InGameUIMessages::lineCount() const
{
	int n = 0;
	for (const Line &l : m_lines)
	{
		n += l.used ? 1 : 0;
	}
	return n;
}
