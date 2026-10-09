// OpenBFME. GPL-3.0.
//
// See ObjectFilter.h for the target facts. Lane HORDE-1.

#include "GameLogic/ObjectFilter.h"

#include "Common/AsciiString.h"

#include <cstring>

bool ObjectFilter::operator==(const ObjectFilter &o) const
{
	return includeNames == o.includeNames && excludeNames == o.excludeNames && includeKindOf == o.includeKindOf && excludeKindOf == o.excludeKindOf &&
		rule == o.rule && relationships == o.relationships && flag == o.flag && side == o.side;
}

ObjectFilter ObjectFilter::none(const KindOfMaskType &include, const KindOfMaskType &exclude)
{
	ObjectFilter f;
	f.rule = RULE_NONE;
	f.includeKindOf = include;
	f.excludeKindOf = exclude;
	f.flag = false;
	for (std::uint32_t w : include)
	{
		if (w != 0)
		{
			f.flag = true;
		}
	}
	return f;
}

ObjectFilter ObjectFilter::all(const KindOfMaskType &exclude)
{
	ObjectFilter f;
	f.rule = RULE_ALL;
	f.flag = true;
	f.excludeKindOf = exclude;
	return f;
}

namespace
{
bool eqNoCase(const std::string &a, const char *b)
{
	return AsciiStringUtil::compareNoCase(a.c_str(), b) == 0;
}

// RW 0x6AAD1A: stricmp over TheKindOfNames, -1 when absent.
int kindOfIndex(const char *name)
{
	for (int i = 0; TheKindOfNames[i]; ++i)
	{
		if (AsciiStringUtil::compareNoCase(TheKindOfNames[i], name) == 0)
		{
			return i;
		}
	}
	return -1;
}
}

void ParseObjectFilter(INI *ini, void *instance, void *store, const void *)
{
	ObjectFilter filter; // RW 0x762BDF
	std::vector<std::string> tokens;
	INI::parseAsciiStringVector(ini, instance, &tokens, nullptr); // RW 0x42EED6

	bool first = true;
	bool ruleSeen = false;
	for (const std::string &tok : tokens)
	{
		const char *t = tok.c_str();
		if (eqNoCase(tok, "ALL") || eqNoCase(tok, "ANY") || eqNoCase(tok, "NONE"))
		{
			const char *word = eqNoCase(tok, "ALL") ? "ALL" : eqNoCase(tok, "ANY") ? "ANY" : "NONE";
			if (!first)
			{
				throw INIException(3, "When using %s in iniParseObjectFilter, %s must be the first entry.", word, word);
			}
			if (std::strcmp(t, word) != 0)
			{
				throw INIException(3, "iniParseObjectFilter %s keyword is case sensitive. You specified %s.", word, t);
			}
			if (word[1] == 'L') // ALL
			{
				filter.rule = ObjectFilter::RULE_ALL;
				filter.flag = true;
			}
			else if (word[1] == 'N') // ANY
			{
				filter.rule = ObjectFilter::RULE_ANY;
				filter.flag = true;
			}
			else // NONE
			{
				filter.rule = ObjectFilter::RULE_NONE;
				filter.flag = false;
			}
			ruleSeen = true;
		}
		else if (eqNoCase(tok, "ALLIES") || eqNoCase(tok, "ENEMIES") || eqNoCase(tok, "NEUTRAL") || eqNoCase(tok, "SAME_PLAYER"))
		{
			if (!ruleSeen)
			{
				throw INIException(3, "iniParseObjectFilter: You must specify a ruleset for your data (ANY, ALL, or NONE). You specified %s.", t);
			}
			const char *word = eqNoCase(tok, "ALLIES") ? "ALLIES" : eqNoCase(tok, "ENEMIES") ? "ENEMIES" : eqNoCase(tok, "NEUTRAL") ? "NEUTRAL" : "SAME_PLAYER";
			if (std::strcmp(t, word) != 0)
			{
				throw INIException(3, "iniParseObjectFilter %s keyword is case sensitive. You specified %s.", word, t);
			}
			filter.relationships |= word[0] == 'A' ? ObjectFilter::REL_ALLIES : word[0] == 'E' ? ObjectFilter::REL_ENEMIES : word[0] == 'N' ? ObjectFilter::REL_NEUTRAL : ObjectFilter::REL_SAME_PLAYER;
		}
		else if (eqNoCase(tok, "EVIL") || eqNoCase(tok, "GOOD"))
		{
			if (!ruleSeen)
			{
				throw INIException(3, "iniParseObjectFilter: You must specify a ruleset for your data (ANY, ALL, or NONE). You specified %s.", t);
			}
			filter.side = eqNoCase(tok, "EVIL") ? ObjectFilter::SIDE_EVIL : ObjectFilter::SIDE_GOOD;
		}
		else if (t[0] == '+')
		{
			if (!ruleSeen)
			{
				throw INIException(3, "iniParseObjectFilter: You must specify a ruleset for your data (ANY, ALL, or NONE). You specified %s.", t);
			}
			if (filter.rule == ObjectFilter::RULE_ALL)
			{
				throw INIException(3, "ALL is specified for iniParseObjectFilter, so adding %s has no effect. Please remove entry.", t);
			}
			const int idx = kindOfIndex(t + 1);
			if (idx != -1)
			{
				BitFlagsSet(filter.includeKindOf, (size_t)idx);
			}
			else
			{
				filter.includeNames.push_back(t + 1);
			}
			filter.flag = true;
		}
		else if (t[0] == '-')
		{
			if (!ruleSeen)
			{
				throw INIException(3, "iniParseObjectFilter: You must specify a ruleset for your data (ANY, ALL, or NONE). You specified %s.", t);
			}
			const int idx = kindOfIndex(t + 1);
			if (idx != -1)
			{
				BitFlagsSet(filter.excludeKindOf, (size_t)idx);
			}
			else
			{
				filter.excludeNames.push_back(t + 1);
			}
		}
		else
		{
			throw INIException(3, "iniParseObjectFilter expecting a + or - token as it is required for classification. Instead it found %s", t);
		}
		first = false;
	}
	*static_cast<ObjectFilter *>(store) = filter;
}
