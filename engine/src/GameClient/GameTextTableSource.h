// OpenBFME. GPL-3.0.
//
// GameTextTableSource (lane SPELL-2, review r2): a parsed string table as the GameTextSource of a HUD without a shell (the standalone in-game HUD and the
// tests): the spell store's help records fetch the buttons' TextLabel / DescriptLabel and TOOLTIP:ScienceDisabled through it.

#pragma once

#include "GameClient/GameText.h"
#include "GameClient/GUI/GameTextSource.h"

#include <string>

// a GameTextTable (data/lotr.str) as a GameTextSource: the labels the screens fetch, UTF-8 -> UTF-16
class GameTextTableSource : public GameTextSource
{
public:
	GameTextTable table;
	bool fetch(const std::string &label, std::u16string &out) const override
	{
		std::string text;
		if (!table.lookup(label, text))
		{
			return false;
		}
		out.clear();
		for (size_t i = 0; i < text.size();)
		{
			const unsigned char c = (unsigned char)text[i];
			char32_t cp = c;
			size_t n = 1;
			if (c >= 0xF0 && i + 3 < text.size())
			{
				cp = ((c & 0x07u) << 18) | (((unsigned char)text[i + 1] & 0x3Fu) << 12) | (((unsigned char)text[i + 2] & 0x3Fu) << 6) | ((unsigned char)text[i + 3] & 0x3Fu);
				n = 4;
			}
			else if (c >= 0xE0 && i + 2 < text.size())
			{
				cp = ((c & 0x0Fu) << 12) | (((unsigned char)text[i + 1] & 0x3Fu) << 6) | ((unsigned char)text[i + 2] & 0x3Fu);
				n = 3;
			}
			else if (c >= 0xC0 && i + 1 < text.size())
			{
				cp = ((c & 0x1Fu) << 6) | ((unsigned char)text[i + 1] & 0x3Fu);
				n = 2;
			}
			if (cp >= 0x10000)
			{
				cp -= 0x10000;
				out.push_back((char16_t)(0xD800 + (cp >> 10)));
				out.push_back((char16_t)(0xDC00 + (cp & 0x3FF)));
			}
			else
			{
				out.push_back((char16_t)cp);
			}
			i += n;
		}
		return true;
	}
};

