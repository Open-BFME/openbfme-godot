// OpenBFME. GPL-3.0.
// See GameLogic/Module/ExtraUpgradeModules.h for the target facts and the addresses.

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameLogic/Module/ExtraUpgradeModules.h"
#include "GameLogic/SnapshotXfer.h"

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

// ---- lane SAVE-4b: the snapshots (UpgradeModule's: the classes keep no state of their own) ----

void AttributeModifierUpgrade::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	UpgradeModule::xfer(x);
}

// lane BANNER-1: AllowBannerSpawnUpgrade keeps no state of its own (RW 0x650650 makes 0x1C bytes, the UpgradeModule's); as AttributeModifierUpgrade
void AllowBannerSpawnUpgrade::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	UpgradeModule::xfer(x);
}
