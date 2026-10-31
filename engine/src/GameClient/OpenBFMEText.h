// OpenBFME. GPL-3.0.
// Lane L10N-1: OpenBFME's own text catalog for the engine (docs/L10N.md). No retail counterpart: retail text stays on GameText
// (the player's CSF files); this holds the text OpenBFME adds, by stable id, from tools/l10n/catalog/<language>.json compiled by
// tools/l10n/l10n.py into OpenBFMETextCatalog.inc. The same rules as the launcher's and the game's runtime (scripts/l10n/l10n.gd):
// named typed placeholders ({name}, {name:d}, {name:.1f}; {{ / }} literal), read once (an argument's text is never read again,
// LocalizedText's rule), CLDR plural forms selected by the integer argument "count". No silent default: an unknown id, a missing
// argument or one of the wrong type is reported in `errors` (or on stderr when none is given) and stays visible in the text; a
// message the language has not translated shows the English source (tools/l10n/l10n.py coverage lists those).

#pragma once

#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace OpenBFMEText
{
class Argument
{
public:
	Argument(std::string_view name, int value) : m_name(name), m_number(true), m_integer(true), m_value(value) {}
	Argument(std::string_view name, long long value) : m_name(name), m_number(true), m_integer(true), m_value(double(value)) {}
	Argument(std::string_view name, double value) : m_name(name), m_number(true), m_value(value) {}
	Argument(std::string_view name, std::u16string_view value) : m_name(name), m_text(value) {}
	Argument(std::string_view name, const char16_t *value) : m_name(name), m_text(value ? value : u"") {}
	const std::string &name() const { return m_name; }
	bool number() const { return m_number; }
	bool integer() const { return m_integer; }
	double value() const { return m_value; }
	const std::u16string &text() const { return m_text; }

private:
	std::string m_name;
	std::u16string m_text;
	bool m_number = false;
	bool m_integer = false;
	double m_value = 0;
};

struct Language
{
	std::string code;
	std::string name;   // the language's own name, UTF-8
	std::string status; // source / complete / partial / sample
};

constexpr std::string_view kSource = "en";

// every catalog language, the source first
const std::vector<Language> &languages();
bool hasLanguage(std::string_view code);
// whether the catalog defines `id`
bool has(std::string_view id);
// the text of `id` in `language` (English where it has no translation)
std::u16string text(std::string_view language, std::string_view id, std::initializer_list<Argument> args = {},
	std::vector<std::string> *errors = nullptr);
// the CLDR plural category of the whole number `n` in `language`
std::string_view pluralCategory(std::string_view language, long long n);
// `pattern` (UTF-16) with its placeholders filled; `id` names the message in errors
std::u16string format(std::u16string_view pattern, const std::vector<Argument> &args, std::string_view language,
	std::string_view id, std::vector<std::string> *errors);
} // namespace OpenBFMEText
