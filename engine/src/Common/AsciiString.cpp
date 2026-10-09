// OpenBFME. Derived from Command & Conquer Generals Zero Hour (c) EA, GPL-3.0.

#include "Common/AsciiString.h"

#include <cstring>

namespace AsciiStringUtil
{

namespace
{
char lowerChar(char c)
{
	return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}
}

void toLower(std::string &s)
{
	for (char &c : s)
	{
		c = lowerChar(c);
	}
}

std::string lowered(const std::string &s)
{
	std::string out = s;
	toLower(out);
	return out;
}

bool nextToken(std::string &s, std::string *tok, const char *seps)
{
	if (s.empty())
	{
		return false;
	}
	if (seps == nullptr)
	{
		seps = " \n\r\t";
	}
	size_t start = 0;
	while (start < s.size() && std::strchr(seps, s[start]) != nullptr)
	{
		++start;
	}
	size_t end = start;
	while (end < s.size() && std::strchr(seps, s[end]) == nullptr)
	{
		++end;
	}
	if (end > start)
	{
		*tok = s.substr(start, end - start);
		s = s.substr(end);
		return true;
	}
	s.clear();
	tok->clear();
	return false;
}

int compareNoCase(const std::string &a, const std::string &b)
{
	size_t n = a.size() < b.size() ? a.size() : b.size();
	for (size_t i = 0; i < n; ++i)
	{
		unsigned char ca = (unsigned char)lowerChar(a[i]);
		unsigned char cb = (unsigned char)lowerChar(b[i]);
		if (ca != cb)
		{
			return ca < cb ? -1 : 1;
		}
	}
	if (a.size() == b.size())
	{
		return 0;
	}
	return a.size() < b.size() ? -1 : 1;
}

} // namespace AsciiStringUtil
