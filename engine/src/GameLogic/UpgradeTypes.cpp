// OpenBFME. GPL-3.0.
// See GameLogic/UpgradeTypes.h.

#include "GameLogic/UpgradeTypes.h"

#include "Common/Upgrade.h"
#include "Common/AsciiString.h"

#include <cctype>

namespace
{
std::string trim(const std::string &s)
{
	size_t a = 0, b = s.size();
	while (a < b && std::isspace((unsigned char)s[a]))
	{
		++a;
	}
	while (b > a && std::isspace((unsigned char)s[b - 1]))
	{
		--b;
	}
	return s.substr(a, b - a);
}
} // namespace

bool UpgradeTypeTable::scan(const std::string &text, std::string *error)
{
	size_t pos = 0;
	std::string current;
	int type = UPGRADE_TYPE_UNKNOWN;
	bool inBlock = false;
	while (pos <= text.size())
	{
		size_t nl = text.find('\n', pos);
		if (nl == std::string::npos)
		{
			nl = text.size();
		}
		std::string line = text.substr(pos, nl - pos);
		pos = nl + 1;
		for (const char *c : { ";", "//" })
		{
			const size_t at = line.find(c);
			if (at != std::string::npos)
			{
				line.erase(at);
			}
		}
		line = trim(line);
		if (line.empty())
		{
			continue;
		}
		if (!inBlock)
		{
			if (line.size() > 8 && AsciiStringUtil::compareNoCase(line.substr(0, 8), "Upgrade ") == 0)
			{
				inBlock = true;
				current = trim(line.substr(8));
				type = UPGRADE_TYPE_UNKNOWN;
			}
			continue;
		}
		if (AsciiStringUtil::compareNoCase(line, "End") == 0)
		{
			if (type == UPGRADE_TYPE_UNKNOWN)
			{
				type = UPGRADE_TYPE_PLAYER; // ZH UpgradeTemplate's constructor default (two retail blocks, Upgrade_FireArrows and Upgrade_HeavyArmor, give no Type): INFERENCE for RotWK
			}
			m_types.emplace(current, type); // a duplicate keeps the first (RW: findUpgrade returns the first)
			inBlock = false;
			continue;
		}
		const size_t eq = line.find('=');
		if (eq != std::string::npos && AsciiStringUtil::compareNoCase(trim(line.substr(0, eq)), "Type") == 0)
		{
			const std::string v = trim(line.substr(eq + 1));
			if (AsciiStringUtil::compareNoCase(v, "OBJECT") == 0)
			{
				type = UPGRADE_TYPE_OBJECT;
			}
			else if (AsciiStringUtil::compareNoCase(v, "PLAYER") == 0)
			{
				type = UPGRADE_TYPE_PLAYER;
			}
			else
			{
				if (error)
				{
					*error = "Upgrade " + current + ": unknown Type '" + v + "'";
				}
				return false;
			}
		}
	}
	return true;
}

void UpgradeTypeTable::assign(const UpgradeCenter &center)
{
	m_types.clear();
	for (const UpgradeTemplate *t : center.templates())
	{
		if (t)
		{
			m_types.emplace(t->getUpgradeName(), t->getUpgradeType() == ::UPGRADE_TYPE_OBJECT ? UPGRADE_TYPE_OBJECT : UPGRADE_TYPE_PLAYER);
		}
	}
}
