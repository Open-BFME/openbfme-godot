// OpenBFME. GPL-3.0.
// See GameLogic/Combat/CombatNames.h.

#include "GameLogic/Combat/CombatNames.h"

#include "GameLogic/ArmorSet.h"
#include "GameLogic/BitFlags.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/WeaponSet.h"

#include <cstring>
#include <stdexcept>


namespace CombatNames
{
int status(const char *name)
{
	const int bit = ObjectTemplateInfoBuilder::objectStatusIndex(name);
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
	for (int i = 0; TheModelConditionNames[i]; ++i)
	{
		if (std::strcmp(TheModelConditionNames[i], name) == 0)
		{
			return i;
		}
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
