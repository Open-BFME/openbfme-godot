// OpenBFME. GPL-3.0.
// Text-only replacement for the localized UnicodeString::format calls cited at the callers.
// RW 0x92FACB / 0x6446A1 / 0x9F09FC: tier A donors Rva0052906BUpdate.cpp,
// VersionUnicode.cpp and Rva005DD772Ctor.cpp pass fetched text to varargs formatting.
// Deliberate security departure: only the substitutions below are interpreted.

#pragma once

#include <cstdio>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace LocalizedText
{
template <typename Char> class Argument
{
public:
	Argument(int value) : m_integer(true) { ascii(std::to_string(value)); }
	Argument(unsigned value) : m_integer(true) { ascii(std::to_string(value)); }
	Argument(double value)
	{
		// CONTROLBAR:UnderConstructionDesc uses precisely %.0f, not an arbitrary floating-point format.
		char buffer[512];
		std::snprintf(buffer, sizeof(buffer), "%.0f", value);
		ascii(buffer);
	}
	Argument(const std::basic_string<Char> &value) : m_text(value) {}
	Argument(const Char *value) : m_text(value ? value : std::basic_string<Char>()) {}
	const std::basic_string<Char> &text() const { return m_text; }
	bool integer() const { return m_integer; }

private:
	void ascii(const std::string &value) { m_text.assign(value.begin(), value.end()); }
	std::basic_string<Char> m_text;
	bool m_integer = false;
};

// Only %d, %i, %u, %s, %ls, %S, %% and the retail %02d / %.0f forms are interpreted.
// Unsupported sequences remain literal; missing arguments are empty. A type mismatch uses the argument's text,
// never a conversion selected by the template. Substituted text is not scanned again.
template <typename Char>
std::basic_string<Char> substitute(std::basic_string_view<Char> pattern, const Argument<Char> *args, std::size_t count)
{
	std::basic_string<Char> out;
	std::size_t next = 0;
	for (std::size_t i = 0; i < pattern.size(); ++i)
	{
		if (pattern[i] != '%' || i + 1 == pattern.size())
		{
			out.push_back(pattern[i]);
			continue;
		}
		std::size_t end = i + 1;
		if (pattern[end] == '%')
		{
			out.push_back(Char('%'));
			i = end;
			continue;
		}
		bool pad = false;
		if (pattern[end] == 'l' && end + 1 < pattern.size() && pattern[end + 1] == 's')
		{
			++end;
		}
		else if (pattern[end] == '0' && end + 2 < pattern.size() && pattern[end + 1] == '2' && pattern[end + 2] == 'd')
		{
			end += 2;
			pad = true;
		}
		else if (pattern[end] == '.' && end + 2 < pattern.size() && pattern[end + 1] == '0' && pattern[end + 2] == 'f')
		{
			end += 2;
		}
		const Char conversion = pattern[end];
		if (conversion != 'd' && conversion != 'i' && conversion != 'u' && conversion != 's' && conversion != 'S' &&
			!(conversion == 'f' && end == i + 3))
		{
			out.push_back(pattern[i]);
			continue;
		}
		if (next < count)
		{
			const Argument<Char> &arg = args[next];
			if (pad && arg.integer() && arg.text().size() == 1)
			{
				out.push_back(Char('0'));
			}
			out.append(arg.text());
		}
		++next;
		i = end;
	}
	return out;
}

inline std::string format(std::string_view pattern, std::initializer_list<Argument<char>> args)
{
	return substitute(pattern, args.begin(), args.size());
}
inline std::u16string format(std::u16string_view pattern, std::initializer_list<Argument<char16_t>> args)
{
	return substitute(pattern, args.begin(), args.size());
}
inline std::u16string format(std::u16string_view pattern, const std::vector<Argument<char16_t>> &args)
{
	return substitute(pattern, args.data(), args.size());
}
} // namespace LocalizedText
