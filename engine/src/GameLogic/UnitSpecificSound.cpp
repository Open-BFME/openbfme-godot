// OpenBFME. GPL-3.0. See GameLogic/UnitSpecificSound.h.

#include "GameLogic/UnitSpecificSound.h"

#include "Common/AsciiString.h"
#include "Common/Thing/ThingTemplate.h"

namespace
{
std::string trim(const std::string &s)
{
	const size_t a = s.find_first_not_of(" \t\r\n");
	const size_t z = s.find_last_not_of(" \t\r\n");
	return a == std::string::npos ? std::string() : s.substr(a, z - a + 1);
}
} // namespace

std::string UnitSpecificSound::rawValue(const ThingTemplate &tt, const std::string &name)
{
	// the block is kept as raw lines; the map key compare is the name map's (case sensitive std::map<AsciiString> order: INFERENCE that the lookup is exact)
	if (name.empty())
	{
		return std::string();
	}
	for (const RawBlock &b : tt.rawBlocks())
	{
		if (b.field != "UnitSpecificSounds")
		{
			continue;
		}
		std::string found;
		for (size_t i = 1; i < b.lines.size(); ++i)
		{
			std::string text = b.lines[i].text;
			const size_t semi = text.find(';');
			if (semi != std::string::npos)
			{
				text.resize(semi);
			}
			const size_t eq = text.find('=');
			if (eq == std::string::npos)
			{
				continue;
			}
			std::string value = trim(text.substr(eq + 1));
			const size_t ws = value.find_first_of(" \t");
			if (ws != std::string::npos)
			{
				value.resize(ws);
			}
			if (trim(text.substr(0, eq)) == name)
			{
				found = value; // a later line of the map replaces an earlier one
			}
		}
		return found;
	}
	return std::string();
}

std::string UnitSpecificSound::templateSound(const ThingTemplate &tt, const char *field)
{
	const FieldValue *f = tt.findField(field);
	const RawTokens *raw = f ? std::get_if<RawTokens>(f) : nullptr;
	if (!raw || raw->tokens.empty() || AsciiStringUtil::compareNoCase(raw->tokens.front(), "NoSound") == 0)
	{
		return std::string();
	}
	return raw->tokens.front();
}
