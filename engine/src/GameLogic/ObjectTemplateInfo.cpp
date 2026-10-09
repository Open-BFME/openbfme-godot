// OpenBFME. GPL-3.0.
// See GameLogic/ObjectTemplateInfo.h.

#include "GameLogic/ObjectTemplateInfo.h"

#include "Common/AsciiString.h"
#include "Common/Thing/KindOfTokens.h"
#include "Common/Thing/ThingTemplate.h"

#include <variant>

extern const char *const TheObjectStatusNames[];

namespace
{
int indexIn(const char *const *names, const std::string &name)
{
	for (int i = 0; names[i]; ++i)
	{
		if (AsciiStringUtil::compareNoCase(name, names[i]) == 0)
		{
			return i;
		}
	}
	return -1;
}

std::vector<std::string> tokensOf(const std::string &line)
{
	std::vector<std::string> out;
	size_t pos = 0;
	while (pos < line.size())
	{
		const size_t b = line.find_first_not_of(" \t\r\n=", pos);
		if (b == std::string::npos)
		{
			break;
		}
		size_t e = line.find_first_of(" \t\r\n=", b);
		if (e == std::string::npos)
		{
			e = line.size();
		}
		out.push_back(line.substr(b, e - b));
		pos = e;
	}
	return out;
}

// the INI line without a trailing ';' comment
std::string stripComment(const std::string &line)
{
	const size_t semi = line.find(';');
	return semi == std::string::npos ? line : line.substr(0, semi);
}

bool weaponSetHasWeapon(const RawBlock &block)
{
	for (size_t i = 1; i < block.lines.size(); ++i)
	{
		const std::vector<std::string> t = tokensOf(stripComment(block.lines[i].text));
		if (t.size() >= 3 && t[0] == "Weapon" && AsciiStringUtil::compareNoCase(t[2], "NONE") != 0)
		{
			return true;
		}
	}
	return false;
}
} // namespace

int ObjectTemplateInfoBuilder::kindOfIndex(std::string_view name)
{
	return KindOfTokens::indexOf(name);
}

int ObjectTemplateInfoBuilder::objectStatusIndex(const std::string &name)
{
	return indexIn(TheObjectStatusNames, name);
}

ObjectTemplateInfo ObjectTemplateInfoBuilder::build(const ThingTemplate &tt)
{
	ObjectTemplateInfo info;
	KindOfTokens::parseTemplate(tt, info.kindOf, info.kindOfError); // lane BUILD-3: every row, the inherited ones first
	if (const FieldValue *v = tt.findField("BuildVariations"))
	{
		if (const std::vector<std::string> *list = std::get_if<std::vector<std::string>>(v))
		{
			info.buildVariations = *list;
		}
	}
	for (const RawBlock &b : tt.rawBlocks())
	{
		if (b.field == "WeaponSet" && weaponSetHasWeapon(b))
		{
			info.canPossiblyHaveAnyWeapon = true;
			break;
		}
	}
	return info;
}
