// OpenBFME. GPL-3.0.
// Lane L10N-1: see OpenBFMEText.h. The rules mirror launcher/scripts/l10n/l10n.gd (format, plural_category); tools/l10n/l10n.py
// validates the catalog (placeholders, plural forms) before it is compiled into OpenBFMETextCatalog.inc.

#include "GameClient/OpenBFMEText.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <tuple>

namespace OpenBFMEText
{
namespace
{
struct Row
{
	const char *language;
	const char *id;
	const char *form; // "" for a plain message, else the CLDR category of a plural form
	const char *text;
};

const Row kRows[] = {
#define OPENBFME_LANGUAGE(code, name, status)
#define OPENBFME_TEXT(language, id, form, text) { language, id, form, text },
#include "GameClient/OpenBFMETextCatalog.inc"
#undef OPENBFME_TEXT
#undef OPENBFME_LANGUAGE
	{ nullptr, nullptr, nullptr, nullptr } // the end (the engine's share of the catalog may be empty)
};

const Language kLanguages[] = {
#define OPENBFME_LANGUAGE(code, name, status) { code, name, status },
#define OPENBFME_TEXT(language, id, form, text)
#include "GameClient/OpenBFMETextCatalog.inc"
#undef OPENBFME_TEXT
#undef OPENBFME_LANGUAGE
};

// (language, id) -> {form -> text}
using Table = std::map<std::pair<std::string_view, std::string_view>, std::map<std::string_view, std::string_view>>;

const Table &table()
{
	static const Table t = [] {
		Table out;
		for (const Row &r : kRows)
		{
			if (!r.language)
			{
				break;
			}
			out[{ r.language, r.id }][r.form] = r.text;
		}
		return out;
	}();
	return t;
}

std::u16string utf16(std::string_view in)
{
	// The catalog is validated UTF-8 (tools/l10n/l10n.py reads it as such), so no error path is needed here.
	std::u16string out;
	for (std::size_t i = 0; i < in.size();)
	{
		const unsigned char c = (unsigned char)in[i];
		std::uint32_t cp = c;
		std::size_t n = 1;
		if (c >= 0xF0)
		{
			cp = c & 0x07;
			n = 4;
		}
		else if (c >= 0xE0)
		{
			cp = c & 0x0F;
			n = 3;
		}
		else if (c >= 0xC0)
		{
			cp = c & 0x1F;
			n = 2;
		}
		for (std::size_t k = 1; k < n && i + k < in.size(); ++k)
		{
			cp = (cp << 6) | ((unsigned char)in[i + k] & 0x3F);
		}
		i += n;
		if (cp >= 0x10000)
		{
			cp -= 0x10000;
			out.push_back(char16_t(0xD800 + (cp >> 10)));
			out.push_back(char16_t(0xDC00 + (cp & 0x3FF)));
		}
		else
		{
			out.push_back(char16_t(cp));
		}
	}
	return out;
}

std::u16string ascii(const std::string &s) { return std::u16string(s.begin(), s.end()); }

void report(std::vector<std::string> *errors, std::string message)
{
	if (errors)
	{
		errors->push_back(std::move(message));
	}
	else
	{
		std::fprintf(stderr, "OpenBFMEText: %s\n", message.c_str());
	}
}

bool decimalComma(std::string_view language)
{
	for (std::string_view l : { "de", "fr", "es", "it", "nl", "pl", "ru", "cs", "hu", "sv", "no", "pt" })
	{
		if (l == language)
		{
			return true;
		}
	}
	return false;
}
} // namespace

const std::vector<Language> &languages()
{
	static const std::vector<Language> out = [] {
		std::vector<Language> v;
		for (const Language &l : kLanguages)
		{
			v.push_back(l);
		}
		std::stable_partition(v.begin(), v.end(), [](const Language &l) { return l.code == kSource; });
		return v;
	}();
	return out;
}

bool hasLanguage(std::string_view code)
{
	for (const Language &l : languages())
	{
		if (l.code == code)
		{
			return true;
		}
	}
	return false;
}

bool has(std::string_view id) { return table().count({ kSource, id }) != 0; }

std::string_view pluralCategory(std::string_view language, long long n)
{
	const long long a = n < 0 ? -n : n;
	const bool few = (a % 10 >= 2 && a % 10 <= 4) && !(a % 100 >= 12 && a % 100 <= 14);
	if (language == "fr" || language == "pt")
	{
		return a <= 1 ? "one" : "other";
	}
	if (language == "pl")
	{
		return a == 1 ? "one" : few ? "few" : "many";
	}
	if (language == "ru")
	{
		return (a % 10 == 1 && a % 100 != 11) ? "one" : few ? "few" : "many";
	}
	if (language == "cs")
	{
		return a == 1 ? "one" : (a >= 2 && a <= 4) ? "few" : "other";
	}
	if (language == "zh" || language == "ko")
	{
		return "other";
	}
	return a == 1 ? "one" : "other";
}

std::u16string format(std::u16string_view pattern, const std::vector<Argument> &args, std::string_view language,
	std::string_view id, std::vector<std::string> *errors)
{
	std::u16string out;
	for (std::size_t i = 0; i < pattern.size();)
	{
		const char16_t ch = pattern[i];
		if ((ch == u'{' || ch == u'}') && i + 1 < pattern.size() && pattern[i + 1] == ch)
		{
			out.push_back(ch);
			i += 2;
			continue;
		}
		if (ch != u'{')
		{
			out.push_back(ch);
			++i;
			continue;
		}
		const std::size_t close = pattern.find(u'}', i);
		if (close == std::u16string_view::npos)
		{
			report(errors, std::string(id) + ": an unclosed '{'");
			out.append(pattern.substr(i));
			break;
		}
		const std::u16string_view literal = pattern.substr(i, close - i + 1);
		const std::u16string_view spec = pattern.substr(i + 1, close - i - 1);
		const std::size_t colon = spec.find(u':');
		const std::string name(spec.begin(), colon == std::u16string_view::npos ? spec.end() : spec.begin() + colon);
		const std::string kind = colon == std::u16string_view::npos ? std::string() : std::string(spec.begin() + colon + 1, spec.end());
		i = close + 1;
		const Argument *arg = nullptr;
		for (const Argument &a : args)
		{
			if (a.name() == name)
			{
				arg = &a;
				break;
			}
		}
		if (!arg)
		{
			report(errors, std::string(id) + " needs the argument '" + name + "'");
			out.append(literal);
			continue;
		}
		if (kind.empty())
		{
			if (!arg->number())
			{
				out.append(arg->text());
			}
			else if (arg->integer())
			{
				out.append(ascii(std::to_string((long long)arg->value())));
			}
			else
			{
				char buffer[64];
				std::snprintf(buffer, sizeof(buffer), "%g", arg->value());
				out.append(ascii(buffer));
			}
			continue;
		}
		if (!arg->number())
		{
			report(errors, std::string(id) + ": the argument '" + name + "' is a number ({" + name + ":" + kind + "})");
			out.append(arg->text());
			continue;
		}
		if (kind == "d")
		{
			out.append(ascii(std::to_string((long long)arg->value())));
			continue;
		}
		if (kind.size() == 3 && kind[0] == '.' && kind[2] == 'f' && kind[1] >= '0' && kind[1] <= '3')
		{
			char buffer[512];
			std::snprintf(buffer, sizeof(buffer), "%.*f", kind[1] - '0', arg->value());
			std::string s = buffer;
			if (decimalComma(language))
			{
				for (char &c : s)
				{
					c = c == '.' ? ',' : c;
				}
			}
			out.append(ascii(s));
			continue;
		}
		report(errors, std::string(id) + ": unknown placeholder type {" + name + ":" + kind + "}");
		out.append(literal);
	}
	return out;
}

std::u16string text(std::string_view language, std::string_view id, std::initializer_list<Argument> args, std::vector<std::string> *errors)
{
	const Table &t = table();
	auto it = t.find({ language, id });
	std::string_view lang = language;
	if (it == t.end())
	{
		it = t.find({ kSource, id });
		lang = kSource;
	}
	if (it == t.end())
	{
		report(errors, "unknown text id '" + std::string(id) + "'");
		return ascii(std::string(id));
	}
	const std::vector<Argument> list(args);
	std::string_view message;
	if (it->second.count(""))
	{
		message = it->second.at("");
	}
	else
	{
		long long n = 0;
		bool found = false;
		for (const Argument &a : list)
		{
			if (a.name() == "count" && a.number() && a.value() == std::floor(a.value()))
			{
				n = (long long)a.value();
				found = true;
			}
		}
		if (!found)
		{
			report(errors, std::string(id) + " is a plural message and needs an integer count");
		}
		message = it->second.at(pluralCategory(lang, n));
	}
	return format(utf16(message), list, lang, id, errors);
}
} // namespace OpenBFMEText
