// OpenBFME. GPL-3.0.
// See GameLogic/Module/UpgradeModules.h for the target facts and the addresses.

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameLogic/Module/UpgradeModules.h"

#include "Common/Player.h"
#include "Common/StateHash.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/ObjectFilter.h"
#include "GameLogic/Object/Object.h"

#include <cstddef>

namespace
{
void parseOptionalObjectFilter(INI *ini, void *instance, void *store, const void *)
{
	ObjectFilter f;
	ParseObjectFilter(ini, instance, &f, nullptr);
	*static_cast<std::shared_ptr<const ObjectFilter> *>(store) = std::make_shared<const ObjectFilter>(std::move(f));
}
// RW 0x8BA26F: parsePercentToReal, appended to the vector
void parsePercentageAppend(INI *ini, void *, void *store, const void *)
{
	float value = 0.0f;
	INI::parsePercentToReal(ini, nullptr, &value, nullptr);
	static_cast<std::vector<float> *>(store)->push_back(value);
}
// RW 0x42EED6 (ApplyToTheseUpgrades is a plain name list in RW)
void parseNameList(INI *ini, void *instance, void *store, const void *)
{
	INI::parseAsciiStringVector(ini, instance, store, nullptr);
}

#define CP_OFF(member) (int)offsetof(CommandPointsUpgradeModuleData, member)
const FieldParse kCommandPointsFieldParse[] = {
	{ "CommandPoints", INI::parseInt, nullptr, CP_OFF(m_commandPoints) },
	{ "RequiredObject", parseOptionalObjectFilter, nullptr, CP_OFF(m_requiredObject) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef CP_OFF

#define CM_OFF(member) (int)offsetof(CostModifierUpgradeModuleData, member)
const FieldParse kCostModifierFieldParse[] = {
	{ "ObjectFilter", parseOptionalObjectFilter, nullptr, CM_OFF(m_objectFilter) },
	{ "Percentage", parsePercentageAppend, nullptr, CM_OFF(m_percentage) },
	{ "UpgradeDiscount", INI::parseBool, nullptr, CM_OFF(m_upgradeDiscount) },
	{ "ApplyToTheseUpgrades", parseNameList, nullptr, CM_OFF(m_applyToTheseUpgrades) },
	{ "StartsActive", INI::parseBool, nullptr, CM_OFF(m_startsActive) },
	{ "Slaughter", INI::parseBool, nullptr, CM_OFF(m_slaughter) },
	{ "LabelForPalantirString", INI::parseAsciiString, nullptr, CM_OFF(m_labelForPalantirString) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef CM_OFF
} // namespace

void CommandPointsUpgradeModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	buildBaseFieldParse(p);
	p.add(kCommandPointsFieldParse);
}

CostModifierUpgradeModuleData::CostModifierUpgradeModuleData()
	: m_objectFilter(std::make_shared<const ObjectFilter>(ObjectFilter::none(KindOfMaskType{}, KindOfMaskType{}))) // RW 0x8B9E08 -> 0x763D11: NONE, empty masks
{
}

void CostModifierUpgradeModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	buildBaseFieldParse(p);
	p.add(kCostModifierFieldParse);
}

// ---- CommandPointsUpgrade ----------------------------------------------------------------------------------------------------------
CommandPointsUpgrade::CommandPointsUpgrade(Thing *thing, const CommandPointsUpgradeModuleData *data)
	: UpgradeModule(thing, data)
	, m_data(data)
{
}

void CommandPointsUpgrade::giveUpgrade()
{
	if (!isAlreadyUpgraded())
	{
		giveSelfUpgrade();
	}
}

void CommandPointsUpgrade::takeUpgrade()
{
	processUpgradeRemoval();
}

void CommandPointsUpgrade::upgradeImplementation()
{
	Object *obj = getObject();
	if (Player *owner = obj->getControllingPlayer())
	{
		owner->commandPoints().addRecord(m_data->m_commandPoints, obj->getID(), m_data->m_requiredObject); // RW 0x8BC7F5 -> 0x6A81E3
	}
}

void CommandPointsUpgrade::processUpgradeRemoval()
{
	if (!isAlreadyUpgraded())
	{
		return; // RW 0x8BC826: the removal only runs when the upgrade was executed
	}
	Object *obj = getObject();
	if (Player *owner = obj->getControllingPlayer())
	{
		owner->commandPoints().removeRecord(m_data->m_commandPoints, obj->getID()); // RW 0x8BC846 -> 0x6A8033
	}
	setUpgradeExecuted(false);
}

// ---- CostModifierUpgrade ------------------------------------------------------------------------------------------------------------
CostModifierUpgrade::CostModifierUpgrade(Thing *thing, const CostModifierUpgradeModuleData *data)
	: UpgradeModule(thing, data)
	, m_data(data)
{
}

void CostModifierUpgrade::addTo(Player &player)
{
	Object *obj = getObject();
	if (m_data->m_upgradeDiscount)
	{
		player.addUpgradeDiscount(obj->getTemplate()->getName(), m_data->m_percentage, m_data->m_applyToTheseUpgrades); // RW 0x6B2C67
	}
	else
	{
		player.addCostModifier(m_data->m_objectFilter, m_data->m_percentage, obj->getID(), m_data->m_slaughter); // RW 0x6AD845
	}
}

void CostModifierUpgrade::removeFrom(Player &player)
{
	Object *obj = getObject();
	if (m_data->m_upgradeDiscount)
	{
		player.removeUpgradeDiscount(obj->getTemplate()->getName(), m_data->m_percentage, m_data->m_applyToTheseUpgrades); // RW 0x6B1992
	}
	else
	{
		player.removeCostModifier(m_data->m_objectFilter.get(), m_data->m_percentage, obj->getID()); // RW 0x6AE74C
	}
}

void CostModifierUpgrade::giveUpgrade()
{
	if (!isAlreadyUpgraded())
	{
		giveSelfUpgrade();
	}
}

void CostModifierUpgrade::takeUpgrade()
{
	processUpgradeRemoval();
}

void CostModifierUpgrade::upgradeImplementation()
{
	if (Player *owner = getObject()->getControllingPlayer())
	{
		addTo(*owner);
	}
}

void CostModifierUpgrade::processUpgradeRemoval()
{
	if (!isAlreadyUpgraded())
	{
		return;
	}
	if (Player *owner = getObject()->getControllingPlayer())
	{
		removeFrom(*owner);
	}
	setUpgradeExecuted(false);
}

void CostModifierUpgrade::onBuildComplete()
{
	// RW 0x8B9D0F: StartsActive runs the implementation and marks it executed
	if (m_data->m_startsActive)
	{
		if (Player *owner = getObject()->getControllingPlayer())
		{
			addTo(*owner);
		}
		setUpgradeExecuted(true);
	}
}

void CostModifierUpgrade::onCapture(Player *oldOwner, Player *newOwner)
{
	// RW 0x8B9C4D: an executed upgrade's entry moves with the building (the discount form only moves its reference count)
	if (!isAlreadyUpgraded())
	{
		return;
	}
	if (oldOwner)
	{
		removeFrom(*oldOwner);
		if (!m_data->m_upgradeDiscount)
		{
			setUpgradeExecuted(false); // RW 0x8B9CAD: setUpgradeExecuted(false) after the removal (the discount form leaves the flag)
		}
	}
	if (newOwner)
	{
		addTo(*newOwner);
		if (!m_data->m_upgradeDiscount)
		{
			setUpgradeExecuted(true); // RW 0x8B9CCA: setUpgradeExecuted(true)
		}
	}
}
