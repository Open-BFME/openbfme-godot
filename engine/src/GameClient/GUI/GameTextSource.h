// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// What the APT screens ask of the game text (ZH GameClient/GameText.h GameTextInterface::fetch(label, &exists)).  Retail screens reach it
// through TheGameText; the port passes a source in the ShellEnvironment so a screen can be built in a test with exactly the strings it is
// given.  No CSF reader exists in the engine yet (the `lang` archives hold the string tables): the real implementation is a later lane's, the
// interface is the contract.
//
// DONOR (ZH GameText.cpp:1296-1316): fetching an unknown label does not fail, it answers the text `MISSING: '<label>'` and reports
// exists = false.  fetchOrMissing() is that behaviour; the screens never invent any other text for a label.

#pragma once

#include <string>

class GameTextSource
{
public:
	virtual ~GameTextSource() = default;
	// True and `out` filled when the table has the label.
	virtual bool fetch(const std::string &label, std::u16string &out) const = 0;
	// ZH GameText::initMapStringFile(path) + fetch + reset(): the label looked up in the map's own string file (`<map directory>\\map.str`).  The
	// default answers "unknown" (no string file reader exists yet): the callers then take the retail fallbacks of MapMetaData.
	virtual bool fetchMapLabel(const std::string &mapStringFile, const std::string &label, std::u16string &out) const
	{
		(void)mapStringFile;
		(void)label;
		(void)out;
		return false;
	}
};

// ZH GameText::fetch: the table text, or `MISSING: '<label>'` (exists = false) for an unknown label.
inline std::u16string fetchOrMissing(const GameTextSource *source, const std::string &label, bool *exists = nullptr)
{
	std::u16string text;
	if (source && source->fetch(label, text))
	{
		if (exists)
		{
			*exists = true;
		}
		return text;
	}
	if (exists)
	{
		*exists = false;
	}
	std::u16string missing = u"MISSING: '";
	for (char c : label)
	{
		missing.push_back((char16_t)(unsigned char)c);
	}
	missing.push_back(u'\'');
	return missing;
}

// UTF-8 / ASCII to UTF-16 for labels and file names (ZH UnicodeString::translate on an ASCII string).
inline std::u16string asciiToU16(const std::string &s)
{
	std::u16string out;
	out.reserve(s.size());
	for (char c : s)
	{
		out.push_back((char16_t)(unsigned char)c);
	}
	return out;
}
