// OpenBFME. GPL-3.0.
// See Common/LogPrivacy.h (lane RELEASE-1).

#include "Common/LogPrivacy.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <vector>

namespace LogPrivacy
{

namespace
{
std::string env(const char *name)
{
	const char *v = std::getenv(name);
	return v ? std::string(v) : std::string();
}

bool sameChar(char a, char b, bool ci)
{
	return ci ? std::tolower((unsigned char)a) == std::tolower((unsigned char)b) : a == b;
}

size_t findFrom(const std::string &text, const std::string &what, size_t from, bool ci)
{
	if (what.empty() || what.size() > text.size())
		return std::string::npos;
	for (size_t i = from; i + what.size() <= text.size(); ++i)
	{
		size_t k = 0;
		while (k < what.size() && sameChar(text[i + k], what[k], ci))
			++k;
		if (k == what.size())
			return i;
	}
	return std::string::npos;
}

bool isSeparator(char c)
{
	return c == '/' || c == '\\';
}

// A character that can continue a file or user name: letters, digits, '_', '-', '.', and every byte of a UTF-8 sequence. Anything else
// (separators, "=", ";", quotes, brackets, spaces, the line end) bounds a name, so "home=<home>;" and "[<home>]" are found too.
bool isNameChar(char c)
{
	const unsigned char u = (unsigned char)c;
	return u >= 0x80 || std::isalnum(u) || c == '_' || c == '-' || c == '.';
}

bool isNamePunct(char c)
{
	return c == '.' || c == '-' || c == '_';
}

// A name is bounded by any character that cannot continue it, by the text's end, and also by a run of '.', '-', '_' that is itself bounded
// ("jo." at a sentence's end, "--jo", "/.jo"): conservative, a dot or a dash next to the name hides nothing (review r2).
bool boundedBefore(const std::string &text, size_t at)
{
	size_t i = at;
	while (i > 0 && isNamePunct(text[i - 1]))
		--i;
	return i == 0 || !isNameChar(text[i - 1]);
}

bool boundedAfter(const std::string &text, size_t at)
{
	size_t i = at;
	while (i < text.size() && isNamePunct(text[i]))
		++i;
	return i >= text.size() || !isNameChar(text[i]);
}

std::string replaceHome(std::string text, std::string home, bool ci)
{
	while (home.size() > 1 && isSeparator(home.back()))
		home.pop_back();
	if (home.size() < 2)
		return text;
	for (int variant = 0; variant < 2; ++variant)
	{
		std::string h = home;
		std::replace(h.begin(), h.end(), variant ? '/' : '\\', variant ? '\\' : '/');
		size_t pos = 0;
		while ((pos = findFrom(text, h, pos, ci)) != std::string::npos)
		{
			// "<prefix>x<home>" is not the home (a name character before it), nor is "<home>s" (one after it)
			if (!boundedBefore(text, pos) || !boundedAfter(text, pos + h.size()))
			{
				pos += 1;
				continue;
			}
			text.replace(pos, h.size(), "~");
			pos += 1;
		}
	}
	return text;
}
} // namespace

Rules hostRules()
{
	Rules r;
#ifdef _WIN32
	r.home = env("USERPROFILE");
	r.user = env("USERNAME");
	r.caseInsensitive = true;
#else
	r.home = env("HOME");
	r.user = env("USER");
	if (r.user.empty())
		r.user = env("LOGNAME");
#endif
	return r;
}

std::string redact(const std::string &input, const Rules &rules)
{
	std::string text = replaceHome(input, rules.home, rules.caseInsensitive);
	const std::string &user = rules.user;
	if (user.empty())
		return text;
	// The name goes wherever it stands as a whole token, whatever its length (review r2: a short-name exception left `username="jo"`
	// and `user: jo`): a one-letter user name also turns the word "a" into "<user>", which is the price of never leaking it.
	size_t pos = 0;
	while ((pos = findFrom(text, user, pos, rules.caseInsensitive)) != std::string::npos)
	{
		const bool replace = boundedBefore(text, pos) && boundedAfter(text, pos + user.size());
		if (replace)
		{
			text.replace(pos, user.size(), "<user>");
			pos += 6;
		}
		else
		{
			pos += 1;
		}
	}
	return text;
}

namespace
{
std::vector<std::string> needles(const Rules &rules)
{
	std::vector<std::string> out;
	std::string home = rules.home;
	while (home.size() > 1 && isSeparator(home.back()))
		home.pop_back();
	if (home.size() >= 2)
	{
		std::string a = home, b = home;
		std::replace(a.begin(), a.end(), '\\', '/');
		std::replace(b.begin(), b.end(), '/', '\\');
		out.push_back(a);
		if (b != a)
			out.push_back(b);
	}
	if (!rules.user.empty())
		out.push_back(rules.user);
	return out;
}

// the longest suffix of `text` that is a prefix of a needle; `properOnly`: shorter than that needle (a name that is not complete yet)
size_t prefixSuffix(const std::string &text, const std::vector<std::string> &ns, bool ci, bool properOnly)
{
	size_t held = 0;
	for (const std::string &needle : ns)
	{
		const size_t top = std::min(properOnly ? needle.size() - 1 : needle.size(), text.size());
		for (size_t k = top; k > held; --k)
		{
			bool same = true;
			for (size_t i = 0; i < k && same; ++i)
				same = sameChar(text[text.size() - k + i], needle[i], ci);
			if (same)
			{
				held = k;
				break;
			}
		}
	}
	return held;
}

// moves `cut` back until no occurrence of a needle spans it
size_t outsideMatches(const std::string &text, const std::vector<std::string> &ns, bool ci, size_t cut)
{
	for (bool moved = true; moved;)
	{
		moved = false;
		for (const std::string &needle : ns)
		{
			for (size_t pos = 0; (pos = findFrom(text, needle, pos, ci)) != std::string::npos; ++pos)
			{
				if (pos < cut && cut < pos + needle.size())
				{
					cut = pos;
					moved = true;
				}
			}
		}
	}
	return cut;
}
} // namespace

size_t safeCut(const std::string &text, const Rules &rules)
{
	const std::vector<std::string> ns = needles(rules);
	const size_t held = prefixSuffix(text, ns, rules.caseInsensitive, false);
	return outsideMatches(text, ns, rules.caseInsensitive, text.size() - held);
}

std::string redactFinal(const std::string &text, const Rules &rules)
{
	const std::vector<std::string> ns = needles(rules);
	const size_t partial = prefixSuffix(text, ns, rules.caseInsensitive, true);
	if (partial == 0)
		return redact(text, rules); // the end bounds every complete name
	const size_t cut = outsideMatches(text, ns, rules.caseInsensitive, text.size() - partial);
	return redact(text.substr(0, cut), rules) + "\xE2\x80\xA6"; // "…": the start of a name that never completed is not written
}

} // namespace LogPrivacy
