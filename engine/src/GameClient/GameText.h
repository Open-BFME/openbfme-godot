// OpenBFME. GPL-3.0.
//
// The game string table (`data/lotr.str`, the text STR format) and the Apt text label rule.
//
// Parser (target facts: none read yet; donor facts: Zero Hour GameText.cpp GameTextManager::parseStringFile,
// readToEndOfQuote, translateCopy, stripSpaces, removeLeadingAndTrailing):
//   - a line that is empty or starts with "//" outside a string is skipped;
//   - the next non-empty line is a label; then lines until `END` (case-insensitive): a line that starts with `"` opens the
//     string, which runs to the closing `"` (a backslash escapes the next character, a newline inside the quotes is a space);
//     the rest of that line after the closing quote is the speech file name; any other line (a `// Context` comment) is skipped;
//   - the text goes through translateCopy (`\\ \' \" \? \t \n` escapes) and stripSpaces (leading spaces, runs of spaces and
//     spaces around a newline or tab are removed);
//   - the text bytes are Windows-1252, converted here to UTF-8.
// Inference: the RotWK parser is that of the BFME2 binary; it was not read (stop S-131).  A label defined twice keeps its first
// string and is reported in `duplicateLabels` (the donor asserts and its sorted lookup picks either).
//
// Apt label rule (donor, Open-BFME-1 AptDisplayStringAllocation_ctor.cpp:133-151, spec menus-apt.md 2.6): a text that starts with
// `$` is a label (the rest of the text; `APT:` is put in front unless it already holds a `:`); a trailing `&dropShadow`
// (case-insensitive) is removed first and turns the shadow on; any other text is shown as it is.

#pragma once

#include <cstddef>
#include <functional>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

class GameTextTable
{
public:
	// Parse one STR file (appends to the table; a label that is already present keeps its first string).
	bool parse(const std::uint8_t *data, std::size_t size, std::string *error);
	bool parse(const std::vector<std::uint8_t> &data, std::string *error) { return parse(data.data(), data.size(), error); }

	// Case-insensitive lookup; the text is UTF-8.
	bool lookup(const std::string &label, std::string &text) const;
	std::size_t size() const { return m_strings.size(); }
	const std::vector<std::string> &duplicateLabels() const { return m_duplicates; }

private:
	std::map<std::string, std::string> m_strings; // key: lower-cased label
	std::vector<std::string> m_duplicates;
};

struct AptTextResolution
{
	std::string text;        // what to draw (UTF-8)
	std::string label;       // the table key when the text was a label, else empty
	bool wasLabel = false;
	bool found = true;       // false: a label with no table entry (`text` is then the label, never silently empty)
	bool dropShadow = false;
};

// Apply the Apt label rule to the raw text of an edit-text field.  Text that is not a label is converted from Windows-1252.
AptTextResolution ResolveAptText(const std::string &raw, const GameTextTable &table);
// With the engine's Apt text records (WindowManager bfme_setAptText / bfme_bindAptText: name -> text): a field whose label the lookup answers shows that text
// (WindowManager::aptTextShown: a non-empty record, or one bfme_setAptText wrote, even empty: lane UI-1) (bfme_bindAptText takes the record's text when it has one and the label's game text only as the default, WindowManager_setAptText.cpp).  `records`
// answers false for a name with no record; it is asked with the label ("LoadingScreen::PlayerName0", "APT:MapTitle") only.
typedef std::function<bool(const std::string &name, std::string &utf8)> AptTextRecordLookup;
AptTextResolution ResolveAptText(const std::string &raw, const GameTextTable &table, const AptTextRecordLookup *records);

std::string Windows1252ToUtf8(const std::string &bytes);
