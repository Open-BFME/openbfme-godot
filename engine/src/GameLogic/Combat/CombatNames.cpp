// OpenBFME. GPL-3.0.
// See GameLogic/Combat/CombatNames.h.

#include "GameLogic/Combat/CombatNames.h"

#include "GameLogic/ArmorSet.h"
#include "GameLogic/BitFlags.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/WeaponSet.h"

#include <cstring>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <unordered_map>


namespace
{
// A registry's names hashed once (the first index of a name wins, as the linear scans' first match); built at the first lookup (thread-safe static
// initialisation), read-only afterwards. Hot per-frame lookups (`status("NOT_IN_WORLD")` in the attack states, `modelCondition("MOVING")`) were
// linear scans, the status one with a std::string temporary per entry.
class NameIndex
{
public:
	NameIndex(const char *const *names, bool foldCase)
		: m_fold(foldCase)
	{
		size_t n = 0;
		while (names[n])
		{
			++n;
		}
		m_folded.reset(new std::string[n]);
		for (size_t i = 0; i < n; ++i)
		{
			std::string_view key = names[i];
			if (m_fold)
			{
				m_folded[i] = fold(key);
				key = m_folded[i];
			}
			m_index.emplace(key, (int)i);
		}
	}
	// -1: not a registry name
	int find(const char *name) const
	{
		if (!m_fold)
		{
			auto it = m_index.find(std::string_view(name));
			return it == m_index.end() ? -1 : it->second;
		}
		char buf[96];
		size_t len = std::strlen(name);
		if (len >= sizeof(buf))
		{
			return -2; // longer than any registry name: the caller's slow path decides
		}
		for (size_t i = 0; i < len; ++i)
		{
			buf[i] = lower(name[i]);
		}
		auto it = m_index.find(std::string_view(buf, len));
		return it == m_index.end() ? -1 : it->second;
	}

private:
	// AsciiStringUtil::compareNoCase's folding (ASCII letters only)
	static char lower(char c)
	{
		return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
	}
	static std::string fold(std::string_view s)
	{
		std::string out(s);
		for (char &c : out)
		{
			c = lower(c);
		}
		return out;
	}

	bool m_fold;
	std::unique_ptr<std::string[]> m_folded; // the folded keys the views point into (never reallocated)
	std::unordered_map<std::string_view, int> m_index;
};

const NameIndex &statusIndex()
{
	static const NameIndex index(TheObjectStatusNames, true); // ObjectTemplateInfoBuilder::objectStatusIndex compares without case
	return index;
}

const NameIndex &modelConditionIndex()
{
	static const NameIndex index(TheModelConditionNames, false); // strcmp
	return index;
}
} // namespace

namespace CombatNames
{
int status(const char *name)
{
	int bit = statusIndex().find(name);
	if (bit == -2)
	{
		bit = ObjectTemplateInfoBuilder::objectStatusIndex(name);
	}
	if (bit < 0)
	{
		throw std::logic_error(std::string("object status registry has no ") + name);
	}
	return bit;
}

int kindOf(const char *name)
{
	const int bit = ObjectTemplateInfoBuilder::kindOfIndex(name);
	if (bit < 0)
	{
		throw std::logic_error(std::string("KindOf registry has no ") + name);
	}
	return bit;
}

int modelCondition(const char *name)
{
	const int i = modelConditionIndex().find(name);
	if (i >= 0)
	{
		return i;
	}
	throw std::logic_error(std::string("model condition registry has no ") + name);
}

int weaponSetBit(const char *name)
{
	for (int i = 0; TheWeaponConditionNames[i]; ++i)
	{
		if (std::strcmp(TheWeaponConditionNames[i], name) == 0)
		{
			return i;
		}
	}
	throw std::logic_error(std::string("weapon set condition registry has no ") + name);
}

int armorSetBit(const char *name)
{
	for (int i = 0; TheArmorSetNames[i]; ++i)
	{
		if (std::strcmp(TheArmorSetNames[i], name) == 0)
		{
			return i;
		}
	}
	throw std::logic_error(std::string("armor set registry has no ") + name);
}

const Status &statuses()
{
	static const Status s;
	return s;
}

const Kind &kinds()
{
	static const Kind k;
	return k;
}
} // namespace CombatNames
