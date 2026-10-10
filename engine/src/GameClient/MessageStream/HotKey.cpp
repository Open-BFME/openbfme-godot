// OpenBFME. GPL-3.0.
// See HotKey.h.

#include "GameClient/MessageStream/HotKey.h"

#include "GameClient/MessageStream/MetaEvent.h"

#include <cwctype>

char32_t HotKeyTranslator::hotkeyOf(const std::u16string &text)
{
	// RW 0x75A67F: the first '&' gives the character after it (an '&' at the end gives none)
	for (size_t i = 0; i < text.size(); ++i)
	{
		if (text[i] == u'&')
		{
			return i + 1 < text.size() ? (char32_t)text[i + 1] : 0;
		}
	}
	return 0;
}

char32_t HotKeyTranslator::usCharOf(int scanCode)
{
	static const struct
	{
		int code;
		char c;
	} kChars[] = { { KEY_A, 'a' }, { KEY_B, 'b' }, { KEY_C, 'c' }, { KEY_D, 'd' }, { KEY_E, 'e' }, { KEY_F, 'f' }, { KEY_G, 'g' }, { KEY_H, 'h' }, { KEY_I, 'i' },
		{ KEY_J, 'j' }, { KEY_K, 'k' }, { KEY_L, 'l' }, { KEY_M, 'm' }, { KEY_N, 'n' }, { KEY_O, 'o' }, { KEY_P, 'p' }, { KEY_Q, 'q' }, { KEY_R, 'r' }, { KEY_S, 's' },
		{ KEY_T, 't' }, { KEY_U, 'u' }, { KEY_V, 'v' }, { KEY_W, 'w' }, { KEY_X, 'x' }, { KEY_Y, 'y' }, { KEY_Z, 'z' }, { KEY_1, '1' }, { KEY_2, '2' }, { KEY_3, '3' },
		{ KEY_4, '4' }, { KEY_5, '5' }, { KEY_6, '6' }, { KEY_7, '7' }, { KEY_8, '8' }, { KEY_9, '9' }, { KEY_0, '0' } };
	for (const auto &k : kChars)
	{
		if (k.code == scanCode)
		{
			return (char32_t)k.c;
		}
	}
	return 0;
}

const HotKeyTranslator::Entry *HotKeyTranslator::find(char32_t ch, bool &disabledSeen) const
{
	// the manager's maps hold one action per key: the first registration of the key stands for it (INFERENCE: the registration order is the command
	// bar's window order)
	for (const Entry &e : m_entries)
	{
		if (e.key != ch)
		{
			continue;
		}
		if (e.availability == Availability::Hidden)
		{
			return nullptr; // vslot 4: passed over
		}
		if (e.availability == Availability::Disabled)
		{
			disabledSeen = true;
			return nullptr;
		}
		return &e;
	}
	return nullptr;
}

MessageDisposition HotKeyTranslator::translate(const ClientMessage &msg)
{
	if (msg.type() != CMSG_RAW_KEY_UP)
	{
		return MessageDisposition::Keep;
	}
	// RW 0x75B0E1: Shift alone or no modifier; Ctrl or Alt refuse the key
	const int state = msg.arg(1).integer;
	if ((state & KEY_STATE_CONTROL) || (state & KEY_STATE_ALT))
	{
		return MessageDisposition::Keep;
	}
	if (!m_ctx.ui.getInputEnabled() || m_entries.empty())
	{
		return MessageDisposition::Keep;
	}
	char32_t ch = msg.argumentCount() > 2 ? (char32_t)msg.arg(2).integer : 0;
	if (ch == 0)
	{
		ch = usCharOf(msg.arg(0).integer);
	}
	if (ch == 0)
	{
		return MessageDisposition::Keep;
	}
	// RW 0x75AEFA: the lower case first, then the upper case
	const char32_t lower = (char32_t)std::towlower((wint_t)ch), upper = (char32_t)std::towupper((wint_t)ch);
	bool disabled = false;
	const Entry *e = find(lower, disabled);
	if (!e && upper != lower)
	{
		e = find(upper, disabled);
	}
	if (!e)
	{
		++m_outcomes[disabled ? "disabled" : "none"]; // RW 0x75A5BC: DisabledHotKeyPressed (not played)
		return MessageDisposition::Keep;
	}
	if (m_press && m_press(e->slot, e->inPalantir))
	{
		++m_outcomes["enabled"]; // RW 0x75A544: EnabledHotKeyPressed (not played)
	}
	else
	{
		++m_outcomes["press refused"];
	}
	return MessageDisposition::Destroy;
}
