// OpenBFME. GPL-3.0.
// See GameLogic/Module/UpgradeModules.h for the target facts and the addresses.

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameLogic/Module/UpgradeModules.h"
#include "GameLogic/SnapshotXfer.h"

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

#define SR_OFF(member) (int)offsetof(SpellRechargeModifierUpgradeModuleData, member)
// the names and the case are the game's table RW 0xC6F0D0; Percentage -> RW 0x8BA26F, StartsActive -> RW 0x42E558, LabelForPalantirString -> RW 0x42EE5E
const FieldParse kSpellRechargeFieldParse[] = {
	{ "Percentage", parsePercentageAppend, nullptr, SR_OFF(m_percentage) },
	{ "StartsActive", INI::parseBool, nullptr, SR_OFF(m_startsActive) },
	{ "LabelForPalantirString", INI::parseAsciiString, nullptr, SR_OFF(m_labelForPalantirString) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef SR_OFF

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

// RW 0x8BA294 (the data proc RW 0x8BA1AB pushes into the factory; the base's table has 8 entries, then RW 0xC6F0D0)
void SpellRechargeModifierUpgradeModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	buildBaseFieldParse(p);
	p.add(kSpellRechargeFieldParse);
}

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

// ---- SpellRechargeModifierUpgrade (lane PORT-65) -------------------------------------------------------------------------------------
SpellRechargeModifierUpgrade::SpellRechargeModifierUpgrade(Thing *thing, const SpellRechargeModifierUpgradeModuleData *data)
	: UpgradeModule(thing, data)
	, m_data(data)
{
}

void SpellRechargeModifierUpgrade::giveUpgrade()
{
	// RW 0x8B9F9A's path (the class's give, as CostModifierUpgrade's): the implementation unless executed, then executed
	if (!isAlreadyUpgraded())
	{
		giveSelfUpgrade();
	}
}

void SpellRechargeModifierUpgrade::takeUpgrade()
{
	processUpgradeRemoval();
}

void SpellRechargeModifierUpgrade::upgradeImplementation()
{
	// RW 0x8BA139: the controlling player's count + 1, then the discount from the Percentage vector at that count
	if (Player *owner = getObject()->getControllingPlayer())
	{
		owner->science().addSpellRechargeModifier(m_data->m_percentage); // RW 0x6AAAE0, RW 0x6AAAEE
	}
}

void SpellRechargeModifierUpgrade::processUpgradeRemoval()
{
	// RW 0x8B9FE0: only an executed upgrade (mux slot 0), then the count - 1 and the discount again; the flag stays (the mux's removeUpgrade
	// RW 0x8D2688 clears it after the removal)
	if (!isAlreadyUpgraded())
	{
		return;
	}
	if (Player *owner = getObject()->getControllingPlayer())
	{
		owner->science().removeSpellRechargeModifier(m_data->m_percentage); // RW 0x6AAAE7, RW 0x6AAAEE
	}
}

void SpellRechargeModifierUpgrade::onBuildComplete()
{
	// RW 0x8B9FB6: StartsActive (+ 0x144) runs the implementation and marks the upgrade executed (mux slot 9); the + 0x20 flag is set either way
	if (m_data->m_startsActive)
	{
		upgradeImplementation();
		setUpgradeExecuted(true);
	}
	m_flag20 = true; // RW 0x8B9FDA
}

void SpellRechargeModifierUpgrade::onCapture(Player *oldOwner, Player *newOwner)
{
	// RW 0x8BA05E: the old owner loses one, the new one gains one, both re-apply the discount for their new count; the upgrade is marked executed.
	// No executed test (unlike CostModifierUpgrade's onCapture RW 0x8B9C4D).
	if (oldOwner)
	{
		oldOwner->science().removeSpellRechargeModifier(m_data->m_percentage); // RW 0x8BA073 .. 0x8BA0C4
	}
	if (newOwner)
	{
		newOwner->science().addSpellRechargeModifier(m_data->m_percentage); // RW 0x8BA0D2 .. 0x8BA123
	}
	setUpgradeExecuted(true); // RW 0x8BA12D (mux slot 9)
}

void SpellRechargeModifierUpgrade::xfer(Xfer &x)
{
	// RW 0x8B9D39, version (1, 2): the version, the + 0x20 byte at 2 and above (RW 0x8B9D57: slot 0x90 = xferBool), then UpgradeModule's (the mux)
	XferVersion v{ 1, 2 };
	x.xferVersion(v);
	if (v.current >= 2)
	{
		x.xferBool(&m_flag20);
	}
	UpgradeModule::xfer(x);
}

// ---- lane SAVE-4b: the snapshots (UpgradeModule's: the classes keep no state of their own) ----

void CommandPointsUpgrade::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	UpgradeModule::xfer(x);
}

void CostModifierUpgrade::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	UpgradeModule::xfer(x);
}
