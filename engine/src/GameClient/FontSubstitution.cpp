// OpenBFME. GPL-3.0.
// See FontSubstitution.h.

#include "GameClient/FontSubstitution.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>

namespace
{

std::string lowerAscii(std::string s)
{
	for (char &c : s)
	{
		c = (char)std::tolower((unsigned char)c);
	}
	return s;
}

std::string trim(const std::string &s)
{
	std::size_t a = 0, b = s.size();
	while (a < b && std::isspace((unsigned char)s[a]))
	{
		++a;
	}
	while (b > a && std::isspace((unsigned char)s[b - 1]))
	{
		--b;
	}
	return s.substr(a, b - a);
}

// `"name"` or a bare word; returns the rest of the string after it.
bool takeName(const std::string &in, std::string &name, std::string &rest)
{
	std::string s = trim(in);
	if (s.empty())
	{
		return false;
	}
	if (s[0] == '"')
	{
		std::size_t e = s.find('"', 1);
		if (e == std::string::npos)
		{
			return false;
		}
		name = s.substr(1, e - 1);
		rest = s.substr(e + 1);
		return true;
	}
	std::size_t e = 0;
	while (e < s.size() && !std::isspace((unsigned char)s[e]))
	{
		++e;
	}
	name = s.substr(0, e);
	rest = s.substr(e);
	return true;
}

} // namespace

bool FontSubstitution::parse(const std::string &text, std::string *error)
{
	std::istringstream in(text);
	std::string raw;
	int lineNo = 0;
	std::string current;
	bool inBlock = false;
	auto fail = [&](const std::string &what) {
		if (error)
		{
			*error = "fontsubstitution.ini line " + std::to_string(lineNo) + ": " + what;
		}
		return false;
	};
	while (std::getline(in, raw))
	{
		++lineNo;
		std::size_t semi = raw.find(';');
		std::string line = trim(semi == std::string::npos ? raw : raw.substr(0, semi));
		if (line.empty())
		{
			continue;
		}
		std::string word, rest;
		if (!takeName(line, word, rest))
		{
			return fail("cannot read '" + line + "'");
		}
		const std::string w = lowerAscii(word);
		if (w == "fontsubstitution" && !inBlock)
		{
			std::string name, tail;
			if (!takeName(rest, name, tail) || !trim(tail).empty())
			{
				return fail("FontSubstitution needs one font name");
			}
			current = lowerAscii(name);
			inBlock = true;
			m_blocks[current];
		}
		else if (w == "end" && inBlock)
		{
			std::vector<Entry> &v = m_blocks[current];
			std::sort(v.begin(), v.end(), [](const Entry &a, const Entry &b) { return a.requested < b.requested; });
			inBlock = false;
		}
		else if (w == "size" && inBlock)
		{
			// Size N = M [+BOLD|-BOLD] Name
			std::size_t eq = rest.find('=');
			if (eq == std::string::npos)
			{
				return fail("Size needs '='");
			}
			char *end = nullptr;
			const std::string reqText = trim(rest.substr(0, eq));
			float requested = std::strtof(reqText.c_str(), &end);
			if (reqText.empty() || *end)
			{
				return fail("bad requested size '" + reqText + "'");
			}
			std::string right = trim(rest.substr(eq + 1));
			std::size_t sp = 0;
			while (sp < right.size() && !std::isspace((unsigned char)right[sp]))
			{
				++sp;
			}
			const std::string sizeText = right.substr(0, sp);
			float size = std::strtof(sizeText.c_str(), &end);
			if (sizeText.empty() || *end)
			{
				return fail("bad substitute size '" + sizeText + "'");
			}
			std::string tail = trim(right.substr(sp));
			Entry e;
			e.requested = requested;
			e.size = size;
			if (!tail.empty() && (tail[0] == '+' || tail[0] == '-'))
			{
				std::size_t b = 0;
				while (b < tail.size() && !std::isspace((unsigned char)tail[b]))
				{
					++b;
				}
				const std::string flag = lowerAscii(tail.substr(0, b));
				if (flag == "+bold")
				{
					e.bold = 1;
				}
				else if (flag == "-bold")
				{
					e.bold = -1;
				}
				else
				{
					return fail("unknown flag '" + flag + "'");
				}
				tail = trim(tail.substr(b));
			}
			std::string name, after;
			if (!takeName(tail, name, after) || !trim(after).empty())
			{
				return fail("bad substitute font name '" + tail + "'");
			}
			e.name = name;
			m_blocks[current].push_back(e);
		}
		else
		{
			return fail("unexpected '" + word + "'");
		}
	}
	if (inBlock)
	{
		return fail("FontSubstitution block without End");
	}
	return true;
}

FontRequestResult FontSubstitution::resolve(const std::string &fontName, float size) const
{
	FontRequestResult r;
	r.name = fontName;
	r.size = size;
	auto it = m_blocks.find(lowerAscii(fontName));
	if (it == m_blocks.end() || it->second.empty())
	{
		return r;
	}
	const std::vector<Entry> &v = it->second;
	r.substituted = true;
	if (size <= v.front().requested)
	{
		r.name = v.front().name;
		r.size = v.front().size;
		r.bold = v.front().bold;
		return r;
	}
	if (size >= v.back().requested)
	{
		r.name = v.back().name;
		r.size = v.back().size;
		r.bold = v.back().bold;
		return r;
	}
	std::size_t lo = 0;
	while (lo + 1 < v.size() && v[lo + 1].requested <= size)
	{
		++lo;
	}
	const Entry &a = v[lo];
	const Entry &b = v[lo + 1];
	r.name = a.name; // the name of the value less than or equal to the request
	r.bold = a.bold;
	const float t = (size - a.requested) / (b.requested - a.requested);
	r.size = (float)(int)(a.size + t * (b.size - a.size)); // truncated: the comment does not say (S-132)
	return r;
}
