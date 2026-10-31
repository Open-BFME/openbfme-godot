// OpenBFME. GPL-3.0.
// See GameClient/InGameChat.h (lane CHAT-1).

#include "GameClient/InGameChat.h"

const char *InGameChat::receiversLabel(InGameChatMode mode, bool localActive)
{
	switch (mode)
	{
	case InGameChatMode::Allies:
		return "Chat:Allies";
	case InGameChatMode::Everyone:
		return localActive ? "Chat:Everyone" : "Chat:Observers";
	case InGameChatMode::Players:
		return "Chat:Players";
	case InGameChatMode::Console:
		return "Chat:Console";
	case InGameChatMode::Observers:
		return "Chat:Observers";
	}
	return "Chat:Everyone";
}

const char *InGameChat::formatLabel(InGameChatMode mode)
{
	switch (mode)
	{
	case InGameChatMode::Allies:
		return "APT:TeamChat";
	case InGameChatMode::Everyone:
		return "APT:GlobalChat";
	case InGameChatMode::Players:
		return "APT:PrivateChat";
	case InGameChatMode::Console:
		return "APT:ConsoleCommand";
	case InGameChatMode::Observers:
		return "APT:ObserverChat";
	}
	return "APT:GlobalChat";
}

std::uint32_t InGameChat::recipientMask(InGameChatMode mode, const std::vector<Slot> &slots)
{
	// RW 0x8ECF3E: the loop over the slots; a slot without a player (or no local player: the caller passes no local slot) adds nothing
	bool haveLocal = false;
	for (const Slot &s : slots)
	{
		haveLocal = haveLocal || (s.player && s.local);
	}
	std::uint32_t mask = 0;
	for (std::size_t i = 0; i < slots.size() && i < 32; ++i)
	{
		const Slot &s = slots[i];
		if (!s.player || !haveLocal)
		{
			continue;
		}
		bool take = false;
		switch (mode)
		{
		case InGameChatMode::Allies:
			take = s.alliedBothWays || s.local;
			break;
		case InGameChatMode::Everyone:
			take = !s.muted;
			break;
		case InGameChatMode::Players:
			take = s.local;
			break;
		case InGameChatMode::Console:
			take = false;
			break;
		case InGameChatMode::Observers:
			take = !s.active;
			break;
		}
		if (take)
		{
			mask |= 1u << i;
		}
	}
	return mask;
}

std::u16string InGameChat::trim(const std::u16string &text)
{
	auto space = [](char16_t c) { return c == u' ' || c == u'\t' || c == u'\r' || c == u'\n' || c == u'\v' || c == u'\f'; };
	std::size_t a = 0, b = text.size();
	while (a < b && space(text[a]))
	{
		++a;
	}
	while (b > a && space(text[b - 1]))
	{
		--b;
	}
	return text.substr(a, b - a);
}

std::u16string InGameChat::formatLine(const std::u16string &format, const std::u16string &text)
{
	for (std::size_t i = 0; i + 1 < format.size(); ++i)
	{
		if (format[i] != u'%')
		{
			continue;
		}
		if (format[i + 1] == u'%')
		{
			++i;
			continue;
		}
		if (format[i + 1] == u's')
		{
			return format.substr(0, i) + text + format.substr(i + 2);
		}
		if (i + 2 < format.size() && format[i + 1] == u'l' && format[i + 2] == u's')
		{
			return format.substr(0, i) + text + format.substr(i + 3);
		}
	}
	return format;
}

void InGameChat::addHistory(const std::u16string &text, std::uint32_t argb, std::uint32_t frame)
{
	m_history.push_back({ text, argb, frame });
	while (m_history.size() > kHistoryLines)
	{
		m_history.pop_front();
	}
}

std::vector<std::string> InGameChat::stopLines()
{
	return { "[S-5150] in-game chat (CHAT-1, RW 0x8ECF3E / 0x8D54D1): ported with OpenBFME's differences (a line goes to its recipients only, a sender's flood "
		"beyond 8 lines is dropped, the scroll-back is an addition); not ported: the console mode, /tribute (sent as text), the language filter (RW 0x78A594), "
		"the Messenger's chat log, the disconnect screen's chat (S-1123)" };
}
