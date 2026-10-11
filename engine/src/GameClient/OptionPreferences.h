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
// OpenBFME additions (not retail; retail ignores the keys and keeps them, as it keeps every entry): their keys start with "OpenBFME" (lane OPTS-2,
// the owner's rule), e.g. "OpenBFMESoftParticles" = yes / no, the soft particle edges of lane FX-3 (owner decision 2026-10-07, lane UI-2: absent = yes,
// the retail look one click away; UI-2 wrote it as "SoftParticles": migrateLegacyKeys renames it). INFERENCE [S-1484]: the folder is the device's user
// data folder (retail's is "My The Lord of the Rings, The Rise of the Witch-king Files"), a line longer than ZH's 2047 characters is read whole.

#pragma once

#include <map>
#include <string>
#include <vector>

class OptionPreferences
{
public:
	static constexpr const char *kOpenBFMEPrefix = "OpenBFME";
	static constexpr const char *kSoftParticles = "OpenBFMESoftParticles";
	static constexpr const char *kLegacySoftParticles = "SoftParticles"; // lane UI-2's key

	// The file's text (ZH UserPreferences::load). Every entry replaces what was there.
	void parse(const std::string &text);
	// The file's text (ZH UserPreferences::write). Lane OPTS-2: RotWK writes it with _wfopen(..., L"w") (RW 0x7B274C), a text mode stream, so each
	// "%s = %s\n" ends in CR LF on its platform. The port keeps the ending of the file it read (CR LF when every break is CR LF or the breaks are
	// mixed, LF when every break is a bare LF) and writes CR LF for a new file, so a file retail wrote comes back byte for byte.
	std::string text() const;
	const std::string &lineEnding() const { return m_eol; }
	void setLineEnding(const std::string &eol) { m_eol = eol; }
	// Files by path; false (and `*error`) when the file cannot be read / written. A missing file reads as no entries and returns false.
	bool load(const std::string &path, std::string *error = nullptr);
	bool save(const std::string &path, std::string *error = nullptr) const;

	bool has(const std::string &key) const { return m_values.count(key) != 0; }
	std::string get(const std::string &key) const;
	void set(const std::string &key, const std::string &value) { m_values[key] = value; }
	void erase(const std::string &key) { m_values.erase(key); } // lane PLAY-2: RW 0x6E6357 (the map's erase)
	const std::map<std::string, std::string> &values() const { return m_values; }

	// "yes" (any case) is true, another value false, an absent key `defaultValue`
	bool getYesNo(const std::string &key, bool defaultValue) const;
	void setYesNo(const std::string &key, bool value) { set(key, value ? "yes" : "no"); }

	// ---- lane OPTS-2: the getters RotWK's GlobalData::applyOptionPreferences (RW 0x641DED, called after GameData.ini, RW 0x6441DE / 0x644374) reads ----
	// RW 0x6E59A9: atoi of ScrollFactor, below 0 -> 1, above 100 -> 100, times 0.02f; missing: `defaultFactor` (GlobalData + 0xAFC,
	// KeyboardDefaultScrollSpeedFactor). GlobalData + 0xA9C and + 0xAA0 (HorizontalScrollSpeedFactor / VerticalScrollSpeedFactor) both take it.
	float getScrollFactor(float defaultFactor) const;
	// RW 0x6E5ECD: (float)atoi of Brightness, missing 50.0f
	float getBrightness() const;
	// RW 0x641E8B .. 0x641EE8: GlobalData + 0xBCC (the display gamma) from the brightness (truncated, RW 0xA3CFA4): below 50: 0.6f at 0 and below, else
	// 1.0f - (50 - b) * 0.008f; above 50: 1.0f + (b - 50) * 0.02f (SSE single precision); 50 leaves `current` (the constructor's 1.0f, RW 0x643298)
	static float displayGamma(int brightness, float current);
	// RW 0x6E5F27: "%d%d" of Resolution (sscanf); false (the outputs untouched) when missing or not two numbers
	bool getResolution(int *x, int *y) const;

	// lane OPTS-2: an OpenBFME key written before the prefix rule takes its prefixed name (its value kept unless the new key is set); returns the keys
	// renamed
	std::vector<std::string> migrateLegacyKeys();

	bool softParticles() const { return getYesNo(kSoftParticles, true); }
	void setSoftParticles(bool soft) { setYesNo(kSoftParticles, soft); }

private:
	std::map<std::string, std::string> m_values;
	std::string m_eol = "\r\n";
};
