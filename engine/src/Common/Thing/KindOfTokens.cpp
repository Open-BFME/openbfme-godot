// OpenBFME. GPL-3.0.
// See Common/Thing/KindOfTokens.h.

#include "Common/Thing/KindOfTokens.h"

#include "Common/AsciiString.h"

#include <algorithm>
#include <string>
#include <vector>

// RW 0xDA0E68 (generated: GameLogic/BitFlagNames.cpp)
extern const char *const TheKindOfNames[];

namespace
{
char lowerAscii(char c)
{
	return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c; // AsciiStringUtil::compareNoCase's folding
}

// lane PERF-1: the names lower-cased and sorted, built once (a function-local static: thread-safe), so a lookup is a binary search without an
// allocation instead of a scan that built a std::string per name (the skirmish AI's isKindOfName calls were half of a 4-player game's logic time).
// Two names that fold to the same text keep the lowest index, which is what the scan in table order returned.
struct FoldedName
{
	std::string folded;
	int index;
};

int compareFolded(std::string_view a, const std::string &folded)
{
	const size_t n = a.size() < folded.size() ? a.size() : folded.size();
	for (size_t i = 0; i < n; ++i)
	{
		const unsigned char ca = (unsigned char)lowerAscii(a[i]), cb = (unsigned char)folded[i];
		if (ca != cb)
		{
			return ca < cb ? -1 : 1;
		}
	}
	return a.size() == folded.size() ? 0 : (a.size() < folded.size() ? -1 : 1);
}

const std::vector<FoldedName> &foldedNames()
{
	static const std::vector<FoldedName> table = [] {
		std::vector<FoldedName> t;
		for (int i = 0; TheKindOfNames[i]; ++i)
		{
			std::string f = TheKindOfNames[i];
			for (char &c : f)
			{
				c = lowerAscii(c);
			}
			t.push_back(FoldedName{ std::move(f), i });
		}
		std::stable_sort(t.begin(), t.end(), [](const FoldedName &a, const FoldedName &b) { return a.folded < b.folded; }); // equal names stay in index order
		return t;
	}();
	return table;
}
} // namespace

int KindOfTokens::indexOf(std::string_view name)
{
	const std::vector<FoldedName> &t = foldedNames();
	size_t lo = 0, hi = t.size();
	while (lo < hi) // the first entry not below the name
	{
		const size_t mid = lo + (hi - lo) / 2;
		if (compareFolded(name, t[mid].folded) > 0)
		{
			lo = mid + 1;
		}
		else
		{
			hi = mid;
		}
	}
	return lo < t.size() && compareFolded(name, t[lo].folded) == 0 ? t[lo].index : -1;
}

// Mirror of the bit-string loop of RW 0x65621C / 0x655B0B for a token list (the shared loop is documented at GameLogic/BitFlags.h):
// a macro token (INI #define) expands to its whitespace separated words, each handled as a token, and a NONE inside a macro ends
// that macro's words only; NONE clears the set and ends the whole list; the first plain name clears the set; +X / -X edit it; mixing
// is an error. Unknown names are reported (retail's INI load would have thrown).
void KindOfTokens::parse(const RawTokens &raw, std::array<std::uint32_t, 7> &mask, std::string &error)
{
	bool sawNormal = false, sawPlusMinus = false;
	// returns false when the list (or the macro's pieces) ends
	auto handle = [&](const std::string &t) -> bool {
		if (AsciiStringUtil::compareNoCase(t, "NONE") == 0)
		{
			if (sawNormal || sawPlusMinus)
			{
				error = "NONE after other KindOf names";
			}
			mask.fill(0);
			return false;
		}
		const bool plus = !t.empty() && t[0] == '+';
		const bool minus = !t.empty() && t[0] == '-';
		const std::string name = (plus || minus) ? t.substr(1) : t;
		const int bit = indexOf(name);
		if (bit < 0)
		{
			error = "unknown KindOf name '" + name + "'";
			return true;
		}
		if (plus || minus)
		{
			if (sawNormal)
			{
				error = "KindOf: +/- after plain names";
			}
			sawPlusMinus = true;
			if (plus)
			{
				mask[(size_t)bit >> 5] |= 1u << (bit & 31);
			}
			else
			{
				mask[(size_t)bit >> 5] &= ~(1u << (bit & 31));
			}
		}
		else
		{
			if (sawPlusMinus)
			{
				error = "KindOf: plain name after +/-";
			}
			if (!sawNormal)
			{
				mask.fill(0);
			}
			sawNormal = true;
			mask[(size_t)bit >> 5] |= 1u << (bit & 31);
		}
		return true;
	};
	for (size_t ti = 0; ti < raw.tokens.size(); ++ti)
	{
		const std::string &t = raw.tokens[ti];
		if (t == "=")
		{
			continue;
		}
		const bool isMacro = ti < raw.macros.size() && raw.macros[ti].isMacro;
		if (isMacro)
		{
			const std::string &value = raw.macros[ti].value;
			size_t pos = 0;
			while (pos < value.size())
			{
				const size_t b = value.find_first_not_of(" \n\r\t", pos);
				if (b == std::string::npos)
				{
					break;
				}
				size_t e = value.find_first_of(" \n\r\t", b);
				if (e == std::string::npos)
				{
					e = value.size();
				}
				pos = e;
				if (!handle(value.substr(b, e - b)))
				{
					break; // only this macro's pieces end; the line goes on
				}
			}
			continue;
		}
		if (!handle(t))
		{
			return;
		}
	}
}

void KindOfTokens::parseTemplate(const ThingTemplate &tt, std::array<std::uint32_t, 7> &mask, std::string &error)
{
	mask.fill(0);
	if (!tt.kindOfRows().empty())
	{
		for (const RawTokens &raw : tt.kindOfRows())
		{
			parse(raw, mask, error);
		}
		return;
	}
	if (const FieldValue *k = tt.findField("KindOf"))
	{
		if (const RawTokens *raw = std::get_if<RawTokens>(k))
		{
			parse(*raw, mask, error);
		}
	}
}
