// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0 (Common/UserPreferences.cpp), as RotWK uses it.
//
// The player's options file (lane UI-2): RotWK keeps its options in "Options.ini" in the user data folder as ZH's UserPreferences does: one
// `key = value` line per entry. TARGET FACTS (RotWK game.dat): AptOptions::Save (RW 0x91FC9C) builds an OptionPreferences (RW 0x6E56F3), sets
// entries through the map (RW 0x602B76) - "Brightness", "ScrollFactor", "AllHealthBars", "AlternateMouseSetup", "UseEAX3", "SendDelay" ... with the
// literals "yes" / "no" (RW 0xBD3D80 / 0xBD3D7C) for the flags - and writes it. DONOR (ZH UserPreferences::load / write): a line is trimmed, the key
// is the text before the first '=' (leading '=' skipped), the value the text after it, both trimmed; a line with an empty key or value is skipped;
// the file is written in key order (std::map of AsciiString, strcmp) as "%s = %s\n". Every entry read is kept and written back, so keys another
// program wrote survive.
//
// OpenBFME addition (owner decision 2026-10-07, lane UI-2): "SoftParticles" = yes / no, the soft particle edges of lane FX-3; absent = yes (soft by
// default, the retail look one click away). Retail ignores the key; the port writes it into the same file. INFERENCE [S-1484]: the folder is the
// device's user data folder (retail's is "My The Lord of the Rings, The Rise of the Witch-king Files"), a line longer than ZH's 2047 characters is
// read whole, and only the port's own entries are set by its Options screen (the retail controls are S-175).

#pragma once

#include <map>
#include <string>

class OptionPreferences
{
public:
	static constexpr const char *kSoftParticles = "SoftParticles";

	// The file's text (ZH UserPreferences::load). Every entry replaces what was there.
	void parse(const std::string &text);
	// The file's text (ZH UserPreferences::write).
	std::string text() const;
	// Files by path; false (and `*error`) when the file cannot be read / written. A missing file reads as no entries and returns false.
	bool load(const std::string &path, std::string *error = nullptr);
	bool save(const std::string &path, std::string *error = nullptr) const;

	bool has(const std::string &key) const { return m_values.count(key) != 0; }
	std::string get(const std::string &key) const;
	void set(const std::string &key, const std::string &value) { m_values[key] = value; }
	const std::map<std::string, std::string> &values() const { return m_values; }

	// "yes" (any case) is true, another value false, an absent key `defaultValue`
	bool getYesNo(const std::string &key, bool defaultValue) const;
	void setYesNo(const std::string &key, bool value) { set(key, value ? "yes" : "no"); }

	bool softParticles() const { return getYesNo(kSoftParticles, true); }
	void setSoftParticles(bool soft) { setYesNo(kSoftParticles, soft); }

private:
	std::map<std::string, std::string> m_values;
};
