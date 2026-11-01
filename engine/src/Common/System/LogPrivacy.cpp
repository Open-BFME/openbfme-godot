// OpenBFME. GPL-3.0.
// See Common/LogPrivacy.h (lane RELEASE-1).

#include "Common/LogPrivacy.h"
#include "Common/PathIO.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <regex>
#include <vector>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace LogPrivacy
{

namespace
{
std::string env(const char *name)
{
	return PathIO::environment(name);
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
			text.replace(pos, h.size(), "<user>");
			pos += 6;
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

namespace
{
bool startsAt(const std::string &text, size_t at, const char *prefix)
{
	for (; *prefix; ++prefix, ++at)
		if (at >= text.size() || std::tolower((unsigned char)text[at]) != *prefix)
			return false;
	return true;
}

std::string redactLocations(const std::string &text)
{
	std::string result;
	for (size_t at = 0; at < text.size();)
	{
		size_t user = at;
		if (startsAt(text, at, "/home/"))
			user = at + 6;
		else if (startsAt(text, at, "/users/"))
			user = at + 7;
		else if (at + 2 < text.size() && std::isalpha((unsigned char)text[at]) && text[at + 1] == ':' && isSeparator(text[at + 2]))
		{
			size_t folder = at + 2;
			while (folder < text.size() && isSeparator(text[folder]))
				++folder;
			if (startsAt(text, folder, "users") && folder + 5 < text.size() && isSeparator(text[folder + 5]))
			{
				user = folder + 5;
				while (user < text.size() && isSeparator(text[user]))
					++user;
			}
		}
		if (user > at)
		{
			size_t end = user;
			while (end < text.size() && !isSeparator(text[end]) && std::string("\r\n\"'<>").find(text[end]) == std::string::npos)
				++end;
			if (end > user)
			{
				result += "<user>";
				at = end;
				continue;
			}
		}
		result += text[at++];
	}
	return result;
}

std::string redactIpv6(const std::string &text)
{
	std::string result;
	for (size_t at = 0; at < text.size();)
	{
		if ((std::isxdigit((unsigned char)text[at]) || text[at] == ':') && (at == 0 || !isNameChar(text[at - 1])))
		{
			size_t end = at;
			unsigned colons = 0;
			bool compressed = false;
			while (end < text.size() && (std::isxdigit((unsigned char)text[end]) || text[end] == ':' || text[end] == '.'))
			{
				if (text[end] == ':')
				{
					++colons;
					compressed |= end > at && text[end - 1] == ':';
				}
				++end;
			}
			if (end < text.size() && text[end] == '%')
			{
				++end;
				while (end < text.size() && isNameChar(text[end]))
					++end;
			}
			if ((compressed || colons == 7) && (end == text.size() || !isNameChar(text[end])))
			{
				result += "<address>";
				at = end;
				continue;
			}
		}
		result += text[at++];
	}
	return result;
}

std::string redactValues(std::string text)
{
	for (size_t at = 0; (at = text.find("://", at)) != std::string::npos;)
	{
		size_t scheme = at;
		while (scheme > 0 && std::isalpha((unsigned char)text[scheme - 1]))
			--scheme;
		const size_t begin = at + 3;
		at = begin;
		if (!startsAt(text, scheme, "https://") && !startsAt(text, scheme, "http://") && !startsAt(text, scheme, "wss://") && !startsAt(text, scheme, "ws://"))
			continue;
		size_t end = begin;
		while (end < text.size() && !std::isspace((unsigned char)text[end]) && std::string("/\\?#\"'<>").find(text[end]) == std::string::npos)
			++end;
		const size_t userInfo = text.rfind('@', end - 1);
		if (userInfo != std::string::npos && userInfo >= begin && userInfo < end)
			text.replace(begin, userInfo - begin, kOmitted);
	}
	for (const char *label : { "GAME ONLINE signed in as ", "GAME ONLINE remembered name ", "GAME ONLINE chat ", "GAME ONLINE WALK" })
	{
		const size_t at = text.find(label);
		if (at != std::string::npos && !startsAt(text, at, "game online walk result:"))
			return text.substr(0, at + std::char_traits<char>::length(label)) + kOmitted;
	}
	static const std::regex keys(
		R"(\b(authorization|bearer|cookie|password|passwd|secret|token|(access|refresh|id|auth)[_ -]?token|session[_ -]?(key|token)|api[_ -]?key|recovery[_ -]?code|client[_ -]?secret|player[_ -]?name|display[_ -]?name|nick(name)?)\b)", std::regex::icase);
	std::vector<std::pair<size_t, size_t>> values;
	for (auto it = std::sregex_iterator(text.begin(), text.end(), keys); it != std::sregex_iterator(); ++it)
	{
		size_t at = (size_t)it->position() + (size_t)it->length();
		const bool bearer = startsAt(it->str(), 0, "bearer");
		if (at < text.size() && (text[at] == '\'' || text[at] == '"'))
			++at;
		while (at < text.size() && std::isspace((unsigned char)text[at]))
			++at;
		if (!bearer)
		{
			if (at == text.size() || (text[at] != ':' && text[at] != '='))
				continue;
			// A parser's "Unexpected token: ..." is not a credential field.
			if (text[at] == ':' && startsAt(it->str(), 0, "token"))
			{
				size_t before = (size_t)it->position();
				while (before > 0 && std::isspace((unsigned char)text[before - 1]))
					--before;
				size_t word = before;
				while (word > 0 && std::isalpha((unsigned char)text[word - 1]))
					--word;
				const std::string preceding = text.substr(word, before - word);
				if (preceding.size() == 10 && startsAt(preceding, 0, "unexpected"))
					continue;
				if (preceding.size() == 8 && startsAt(preceding, 0, "expected"))
					continue;
			}
			++at;
			while (at < text.size() && std::isspace((unsigned char)text[at]))
				++at;
		}
		size_t end = at;
		if (at < text.size() && (text[at] == '\'' || text[at] == '"'))
		{
			const char quote = text[at++];
			end = at;
			while (end < text.size() && text[end] != quote)
			{
				if (text[end] == '\\' && end + 1 < text.size())
					++end;
				++end;
			}
		}
		else
			while (end < text.size() && std::string(",;&\r\n}]").find(text[end]) == std::string::npos && (!bearer || !std::isspace((unsigned char)text[end])))
				++end;
		if (text.compare(at, std::char_traits<char>::length(kOmitted), kOmitted) == 0)
			continue;
		if (end > at && (values.empty() || at >= values.back().second))
			values.emplace_back(at, end);
	}
	for (auto it = values.rbegin(); it != values.rend(); ++it)
		text.replace(it->first, it->second - it->first, kOmitted);
	for (size_t at = 0; at < text.size();)
	{
		const size_t begin = at;
		while (at < text.size() && isNameChar(text[at]))
			++at;
		const std::string value = text.substr(begin, at - begin);
		const size_t first = value.find('.'), second = value.find('.', first == std::string::npos ? value.size() : first + 1);
		if (value.rfind("eyJ", 0) == 0 && first >= 8 && first != std::string::npos && value.compare(first + 1, 3, "eyJ") == 0 &&
			second != std::string::npos && second - first > 8 && value.size() - second > 8 && value.find('.', second + 1) == std::string::npos)
		{
			text.replace(begin, at - begin, kOmitted);
			at = begin + std::char_traits<char>::length(kOmitted);
		}
		if (at == begin)
			++at;
	}
	return text;
}

bool versionAt(const std::string &text, size_t at)
{
	while (at > 0 && (std::isspace((unsigned char)text[at - 1]) || std::string(":=\"'").find(text[at - 1]) != std::string::npos))
		--at;
	const size_t end = at;
	while (at > 0 && isNameChar(text[at - 1]))
		--at;
	std::string label = text.substr(at, end - at);
	std::transform(label.begin(), label.end(), label.begin(), [](unsigned char c) { return (char)std::tolower(c); });
	return label == "version" || label == "engine" || label == "godot" || label == "openbfme" || label == "engine_version" || label == "build_version";
}

std::string redactLine(const std::string &input, const Rules &rules)
{
	if (input.size() > kMaxFieldBytes)
		return kOversized;
	std::string text = redactIpv6(redactLocations(replaceHome(redactValues(input), rules.home, rules.caseInsensitive)));
	static const std::regex ipv4(R"(\b[0-9]{1,3}(\.[0-9]{1,3}){3}\b)");
	std::string addresses;
	size_t copied = 0;
	for (auto it = std::sregex_iterator(text.begin(), text.end(), ipv4); it != std::sregex_iterator(); ++it)
	{
		const size_t start = (size_t)it->position(), end = start + (size_t)it->length();
		if (versionAt(text, start))
			continue;
		bool valid = true;
		unsigned octet = 0;
		for (char c : it->str() + ".")
			if (c == '.') { valid &= octet <= 255; octet = 0; } else octet = octet * 10 + (unsigned)(c - '0');
		if (!valid)
			continue;
		addresses += text.substr(copied, start - copied) + "<address>";
		copied = end;
	}
	text = addresses + text.substr(copied);
	for (size_t at = 0; (at = text.find('@', at)) != std::string::npos;)
	{
		size_t begin = at, end = at + 1;
		while (begin > 0 && (isNameChar(text[begin - 1]) || text[begin - 1] == '+' || text[begin - 1] == '%'))
			--begin;
		while (end < text.size() && isNameChar(text[end]))
			++end;
		if (begin == at || end == at + 1)
		{
			++at;
			continue;
		}
		text.replace(begin, end - begin, "<address>");
		at = begin + 9;
	}
	const std::string &user = rules.user;
	if (user.empty())
		return text.size() > kMaxFieldBytes ? kOversized : text;
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
	return text.size() > kMaxFieldBytes ? kOversized : text;
}
} // namespace

std::string redact(const std::string &input, const Rules &rules)
{
	if (input.size() > kMaxLogBytes)
		return kOversized;
	std::string out;
	for (size_t start = 0; start < input.size();)
	{
		const size_t newline = input.find('\n', start);
		const size_t end = newline == std::string::npos ? input.size() : newline;
		out += redactLine(input.substr(start, end - start), rules);
		if (newline == std::string::npos)
			break;
		out += '\n';
		start = end + 1;
	}
	return out;
}

std::string Budget::record(const std::string &text, const Rules &rules)
{
	rewrite = false;
	std::string clean = redact(text, rules);
	if (clean.empty() || clean.back() != '\n')
		clean += '\n';
	if (clean.size() > kMaxLogBytes - kHeadBytes)
		clean = std::string(kOversized) + '\n';
	if (!headClosed && head.size() + clean.size() <= kHeadBytes)
		head += clean;
	else
	{
		headClosed = true;
		tail += clean;
	}
	if (head.size() + tail.size() > kMaxLogBytes)
	{
		const size_t keep = std::max((kMaxLogBytes - kHeadBytes) / 2, clean.size());
		const size_t cut = tail.find('\n', tail.size() - keep - 1);
		tail.erase(0, cut + 1);
		rewrite = true;
	}
	written = head.size() + tail.size();
	return clean;
}

bool Budget::write(std::FILE *file, const std::string &text, const Rules &rules)
{
	std::string clean = record(text, rules);
	if (rewrite)
	{
		if (std::fseek(file, 0, SEEK_SET) != 0)
			return false;
		clean = snapshot();
	}
	if (std::fwrite(clean.data(), 1, clean.size(), file) != clean.size() || std::fflush(file) != 0)
		return false;
	if (!rewrite)
		return true;
#ifdef _WIN32
	return _chsize_s(_fileno(file), written) == 0;
#else
	return ftruncate(fileno(file), (off_t)written) == 0;
#endif
}

std::string errorMessage(const std::string &code, const std::string &rationale, const Rules &rules)
{
	const std::string &message = rationale.empty() ? code : rationale;
	// Reserve room for the severity prefix and truncation suffix in the bounded log line.
	constexpr size_t limit = kMaxFieldBytes - 32;
	if (message.size() <= limit)
		return redact(message, rules);
	size_t cut = message.find_last_of(" \t\r\n", limit);
	if (cut == std::string::npos)
		cut = limit;
	while (cut > 0 && ((unsigned char)message[cut] & 0xc0) == 0x80)
		--cut;
	return redactFinal(message.substr(0, cut), rules) + " [truncated]";
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
