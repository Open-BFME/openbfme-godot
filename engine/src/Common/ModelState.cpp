// OpenBFME. GPL-3.0.
//
// ModelConditionFlags name table and parser. See ModelState.h for the sources (RW 0xD9FAD8, 0x4B5E05, 0x4B8B37).

#include "Common/ModelState.h"

#include "Common/AsciiString.h"
#include "Common/INIException.h"

#include <cstring>
#include <vector>

namespace
{
const char *const kNames[] = {
#include "Common/ModelConditionNames.inc"
	nullptr,
};
static_assert(sizeof(kNames) / sizeof(kNames[0]) == MODELCONDITION_COUNT + 1, "ModelConditionNames.inc has the wrong number of names");

// RW 0x4B5E05, per token. Returns false when the list ends (NONE).
bool applyToken(const char *name, ModelConditionFlags &flags, bool &sawNormal, bool &sawPlusMinus)
{
	if (AsciiStringUtil::compareNoCase(name, "NONE") == 0)
	{
		if (sawNormal || sawPlusMinus)
		{
			throw INIException(2, "you may not mix normal and +- ops in bitstring lists");
		}
		flags.clear();
		return false;
	}
	if (name[0] == '+' || name[0] == '-')
	{
		if (sawNormal)
		{
			throw INIException(2, "you may not mix normal and +- ops in bitstring lists");
		}
		const int bit = INI::scanIndexList(name + 1, ModelCondition::bitNames());
		flags.set(bit, name[0] == '+');
		sawPlusMinus = true;
		return true;
	}
	if (sawPlusMinus)
	{
		throw INIException(2, "you may not mix normal and +- ops in bitstring lists");
	}
	if (!sawNormal)
	{
		flags.clear();
	}
	const int bit = INI::scanIndexList(name, ModelCondition::bitNames());
	flags.set(bit);
	sawNormal = true;
	return true;
}

std::vector<std::string> splitWords(const std::string &text)
{
	std::vector<std::string> words;
	size_t i = 0;
	while (i < text.size())
	{
		while (i < text.size() && (text[i] == ' ' || text[i] == '\t' || text[i] == '\n' || text[i] == '\r'))
		{
			++i;
		}
		size_t j = i;
		while (j < text.size() && !(text[j] == ' ' || text[j] == '\t' || text[j] == '\n' || text[j] == '\r'))
		{
			++j;
		}
		if (j > i)
		{
			words.push_back(text.substr(i, j - i));
		}
		i = j;
	}
	return words;
}
} // namespace

namespace ModelCondition
{
const char *const *bitNames() { return kNames; }
int count() { return MODELCONDITION_COUNT; }

int indexOf(const std::string &name)
{
	for (int i = 0; i < MODELCONDITION_COUNT; ++i)
	{
		if (AsciiStringUtil::compareNoCase(name, kNames[i]) == 0)
		{
			return i;
		}
	}
	return -1;
}

const char *nameOf(int bit) { return (bit >= 0 && bit < MODELCONDITION_COUNT) ? kNames[bit] : nullptr; }

std::string describe(const ModelConditionFlags &flags)
{
	std::string out;
	for (int i = 0; i < MODELCONDITION_COUNT; ++i)
	{
		if (flags.test(i))
		{
			if (!out.empty())
			{
				out += ' ';
			}
			out += kNames[i];
		}
	}
	return out.empty() ? std::string("NONE") : out;
}

void parseFromLine(INI *ini, ModelConditionFlags &flags)
{
	// RW 0x4B8B37. `flags` is cleared by the first plain name or by NONE, and the loop starts from the caller's value, so
	// "+X" / "-X" edit what the caller passed in.
	bool sawNormal = false;
	bool sawPlusMinus = false;
	for (const char *token = ini->getNextTokenOrNull(); token != nullptr; token = ini->getNextTokenOrNull())
	{
		const std::string name = token; // a macro expansion below may reuse the token storage
		const char *expanded = ini->preprocessMacro(name.c_str());
		if (expanded != nullptr && name != expanded)
		{
			// RW 0x4B8B65: the expansion is tokenised and each word handled as a token of its own; NONE ends only the expansion.
			const std::string text = expanded;
			for (const std::string &word : splitWords(text))
			{
				if (!applyToken(word.c_str(), flags, sawNormal, sawPlusMinus))
				{
					break;
				}
			}
			continue;
		}
		if (!applyToken(name.c_str(), flags, sawNormal, sawPlusMinus))
		{
			return; // RW 0x4B8BF7: NONE on the line itself ends the parse
		}
	}
}

void parseFromINI(INI *ini, void *, void *store, const void *)
{
	parseFromLine(ini, *static_cast<ModelConditionFlags *>(store));
}
} // namespace ModelCondition
