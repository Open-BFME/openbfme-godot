// OpenBFME. GPL-3.0.
// See GameLogic/Module/ExtraUpgradeModules.h for the target facts and the addresses.

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameLogic/Module/ExtraUpgradeModules.h"

#include "GameLogic/Object/Object.h"

#include <cstddef>

namespace
{
const FieldParse kAttributeModifierUpgrade[] = {
	{ "AttributeModifier", INI::parseAsciiString, nullptr, (int)offsetof(AttributeModifierUpgradeModuleData, m_attributeModifier) }, // RW 0xC6F478
	{ nullptr, nullptr, nullptr, 0 }
};
} // namespace

void AttributeModifierUpgradeModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	buildBaseFieldParse(p); // RW 0xC76AD8
	p.add(kAttributeModifierUpgrade);
}

// RW 0x8BA8C3
void AttributeModifierUpgrade::upgradeImplementation()
{
	setCustomAnim(true);                                          // RW 0x8BA8C9
	getObject()->addAttributeModifier(m_data->m_attributeModifier, -1); // RW 0x8BA8DC
}

// RW 0x8BA8E3
void AttributeModifierUpgrade::processUpgradeRemoval()
{
	if (!isAlreadyUpgraded()) // slot 0
	{
		return;
	}
	setCustomAnim(false);                                          // RW 0x8BA8F1
	getObject()->removeAttributeModifier(m_data->m_attributeModifier); // RW 0x68F259
	setUpgradeExecuted(false);                                     // slot 9 (0)
}
