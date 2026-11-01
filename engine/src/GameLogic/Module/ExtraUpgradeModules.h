// OpenBFME. GPL-3.0.
// Lane MODULES-1: upgrade module classes the base game uses that UPGRADE-1 left unported. RotWK classes (no ZH donor); the UpgradeModule base is
// GameLogic/Module/UpgradeModule.h.
//
// TARGET FACTS (RotWK game.dat, caveat S-001; registry golden engine/data/rotwk-201/module-registry.json, field-tables.json):
//   * AttributeModifierUpgrade (create RW 0x650334, constructor RW 0x8BA876 on the UpgradeModule base RW 0x863B8F, mux vtable RW 0xC6F400; data: the upgrade
//     table RW 0xC76AD8 (extra 8) and RW 0xC6F478: AttributeModifier (+0x138, parseAsciiString RW 0x42EE5E, the ModifierList name as written)).
//     Implementation (mux slot 10, RW 0x8BA8C3): the custom anim (RW 0x8D28F1) FIRST, then Object::addAttributeModifier(name, -1) (RW 0x68F1A8: the list's own
//     duration). Removal (mux slot 8, RW 0x8BA8E3): only when isAlreadyUpgraded (slot 0): the custom anim clears (RW 0x8D28F9),
//     Object::removeAttributeModifier(name) (RW 0x68F259), setUpgradeExecuted(false) (slot 9). isPermanent / postUpgradeCheck are the base's.
//     (removeUpgrade clears the executed flag before the removal, so the removal does nothing on that path: retail behaviour, ported as is; see UpgradeModule.h.)
//   * AllowBannerSpawnUpgrade (lane BANNER-1; create RW 0x650650, 0x1C bytes, constructor RW 0x8BC6CE on the UpgradeModule base RW 0x863B8F, mux vtable RW
//     0xC6FC90; data: the upgrade table RW 0xC76AD8 only). Implementation (mux slot 10, RW 0x8BC657): the custom anim (RW 0x8D28F1); removal (slot 8, RW
//     0x8BC65F): the custom anim clears (RW 0x8D28F9). Its state is the mux's executed flag, which BannerCarrierUpdate's spawn test reads (RW 0x89A308: a GARRISON
//     container with this module lets its banner carriers make members only once it is upgraded). Donor: Open-BFME-2 AllowBannerSpawnUpgradeUpgradeImplementation.cpp.

#pragma once

#include "GameLogic/Module/UpgradeModule.h"

#include <string>

class AttributeModifierUpgradeModuleData : public UpgradeModuleData
{
public:
	std::string m_attributeModifier; ///< +0x138
	static void buildFieldParse(MultiIniFieldParse &p);
};

class AttributeModifierUpgrade : public UpgradeModule
{
public:
	OBFME_MODULE_XFER(AttributeModifierUpgrade) // lane SAVE-4b: UpgradeModule's
	AttributeModifierUpgrade(Thing *thing, const AttributeModifierUpgradeModuleData *data) : UpgradeModule(thing, data), m_data(data) {}

protected:
	void upgradeImplementation() override;
	void processUpgradeRemoval() override;

private:
	const AttributeModifierUpgradeModuleData *m_data;
};

class AllowBannerSpawnUpgrade : public UpgradeModule
{
public:
	OBFME_MODULE_XFER(AllowBannerSpawnUpgrade) // lane BANNER-1 (review r1): its own snapshot, UpgradeModule's state (the mux's executed flag)
	AllowBannerSpawnUpgrade(Thing *thing, const UpgradeModuleData *data) : UpgradeModule(thing, data) {}

protected:
	void upgradeImplementation() override { setCustomAnim(true); }   // RW 0x8BC657 -> 0x8D28F1
	void processUpgradeRemoval() override { setCustomAnim(false); }  // RW 0x8BC65F -> 0x8D28F9
};
